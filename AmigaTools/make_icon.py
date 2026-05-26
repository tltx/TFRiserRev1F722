#!/usr/bin/env python3
"""Generate risergamepad.info -- a classic AmigaOS tool icon showing a
stylised CD32 gamepad.

Double-clicking the icon on Workbench launches `risergamepad` with no
CLI arguments; the program detects the Workbench launch (argc == 0) and
opens the graphical mapper.

The icon image uses only the four default Workbench pens, so it looks
right on a stock WB2.0/3.x palette without carrying its own colours:
    0 = grey  (desktop background)
    1 = black
    2 = white
    3 = blue
"""

import struct
from PIL import Image, ImageDraw

W, H, DEPTH = 80, 50, 2          # 4-colour planar icon

PEN_GREY, PEN_BLACK, PEN_WHITE, PEN_BLUE = 0, 1, 2, 3

# ---------------------------------------------------------------------------
# Draw the gamepad into a palette-index image (each pixel value = WB pen).
# ImageDraw in "P" mode is not anti-aliased, so every pixel stays on-palette.
# ---------------------------------------------------------------------------
img = Image.new("P", (W, H), PEN_GREY)
d = ImageDraw.Draw(img)

def disc(cx, cy, r, fill, outline=PEN_WHITE):
    d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=fill, outline=outline)

def rrect(box, r, fill):
    d.rounded_rectangle(box, radius=r, fill=fill)

# --- Body: the CD32 "yoke" silhouette -- two tall rounded wings (left
# holds the D-pad, right holds the buttons) joined by a lower central
# bridge.  Drawn as the union of three rounded rectangles: a white rim
# layer, then a 2px-smaller black layer.  Overlapping the black pieces
# covers the internal seams, leaving a clean rim only on the outside. ---
LWING = (3, 7, 33, 44)
RWING = (46, 7, 76, 44)
BRIDGE = (20, 21, 59, 44)
rrect(LWING,  9, PEN_WHITE)
rrect(RWING,  9, PEN_WHITE)
rrect(BRIDGE, 10, PEN_WHITE)
rrect((5, 9, 31, 42),  7, PEN_BLACK)
rrect((48, 9, 74, 42), 7, PEN_BLACK)
rrect((22, 23, 57, 42), 8, PEN_BLACK)

# --- Round D-pad on the left wing: a plain disc with four dimple dots
# in diagonal positions (no cross). ---
disc(16, 24, 9, PEN_WHITE)                       # raised ring
disc(16, 24, 7, PEN_BLACK, outline=PEN_BLACK)    # recessed face
for dx, dy in ((-3, -3), (3, -3), (-3, 3), (3, 3)):
    disc(16 + dx, 24 + dy, 1, PEN_WHITE, outline=PEN_WHITE)

# --- Four action buttons in a square on the right wing. ---
disc(56, 20, 4, PEN_WHITE)   # top-left
disc(68, 20, 4, PEN_BLUE)    # top-right
disc(56, 32, 4, PEN_BLUE)    # bottom-left
disc(68, 32, 4, PEN_WHITE)   # bottom-right

# --- Centre slot (the PLAY/pause slider on the bridge). ---
d.rounded_rectangle((34, 35, 47, 38), radius=1, fill=PEN_GREY, outline=PEN_WHITE)

# ---------------------------------------------------------------------------
# Pack the index image into Amiga planar bitplanes (plane0 then plane1).
# ---------------------------------------------------------------------------
row_words = (W + 15) // 16
row_bytes = row_words * 2
planes = [bytearray(row_bytes * H) for _ in range(DEPTH)]
px = img.load()
for y in range(H):
    for x in range(W):
        idx = px[x, y] & ((1 << DEPTH) - 1)
        byte_off = y * row_bytes + (x >> 3)
        bit = 7 - (x & 7)
        for p in range(DEPTH):
            if idx & (1 << p):
                planes[p][byte_off] |= (1 << bit)
image_data = b"".join(bytes(p) for p in planes)

# ---------------------------------------------------------------------------
# Build the .info file: DiskObject + one Image header + planar data.
# All fields big-endian (m68k).
# ---------------------------------------------------------------------------
NO_ICON_POSITION = 0x80000000
WB_DISKMAGIC, WB_DISKVERSION = 0xE310, 1
WBTOOL = 3
GADGIMAGE, GADGHCOMP = 0x0004, 0x0000

disk_object = struct.pack(
    ">HH"          # do_Magic, do_Version
    "I"            # Gadget.NextGadget
    "HHHH"         # LeftEdge, TopEdge, Width, Height
    "HHH"          # Flags, Activation, GadgetType
    "I"            # GadgetRender  (non-zero => image follows)
    "I"            # SelectRender  (0 => no second image)
    "I"            # GadgetText
    "i"            # MutualExclude
    "I"            # SpecialInfo
    "H"            # GadgetID
    "I"            # UserData
    "BB"           # do_Type, pad
    "IIIIIII",     # DefaultTool, ToolTypes, CurrentX, CurrentY,
                   # DrawerData, ToolWindow, StackSize
    WB_DISKMAGIC, WB_DISKVERSION,
    0,
    0, 0, W, H,
    GADGIMAGE | GADGHCOMP, 0x0003, 0x0001,
    1, 0, 0, 0, 0, 0, 0,
    WBTOOL, 0,
    0, 0, NO_ICON_POSITION, NO_ICON_POSITION, 0, 0, 16384,
)

image_hdr = struct.pack(
    ">hhhhh"       # LeftEdge, TopEdge, Width, Height, Depth
    "I"            # ImageData
    "BB"           # PlanePick, PlaneOnOff
    "I",           # NextImage
    0, 0, W, H, DEPTH,
    1, (1 << DEPTH) - 1, 0, 0,
)

with open("risergamepad.info", "wb") as f:
    f.write(disk_object)
    f.write(image_hdr)
    f.write(image_data)

# Scaled-up RGB preview so the result can be eyeballed without an Amiga.
wb_rgb = {PEN_GREY: (170, 170, 170), PEN_BLACK: (0, 0, 0),
          PEN_WHITE: (255, 255, 255), PEN_BLUE: (90, 130, 200)}
preview = Image.new("RGB", (W, H))
ppx = preview.load()
for y in range(H):
    for x in range(W):
        ppx[x, y] = wb_rgb[px[x, y] & 3]
preview.resize((W * 6, H * 6), Image.NEAREST).save("risergamepad_icon_preview.png")

print("Wrote risergamepad.info (%d bytes) and risergamepad_icon_preview.png"
      % (len(disk_object) + len(image_hdr) + len(image_data)))
