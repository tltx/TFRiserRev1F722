/* risergamepad-gui.c -- GUI variant of the learn command.
 *
 * Interactive CD32 gamepad mapper.  Highlights one button at a time
 * on a stylised CD32 picture; user presses that button on their
 * physical USB gamepad and the firmware mapping byte is updated.
 *
 * Skip/unmap/quit via keyboard.  After all buttons captured (or the
 * user quits), mappings are written to the firmware and the window
 * stays open in "verify mode" -- pressing buttons on the gamepad
 * lights them up on the picture so you can confirm the mapping
 * matches your intent before closing.
 */

#include "amiga_min.h"
#include <stdio.h>
#include <string.h>

/* Riser memory map -- duplicate of the constants from risergamepad.c
 * so the GUI module is self-contained.  The base address and the
 * register offsets must match what the firmware exposes. */
#define RISER_BASE_ADDR  0x00BA0000UL
#define SLOTS            10
#define UNMAPPED         0xFFu
#define PAD1_REG_BASE    0x20
#define PAD2_REG_BASE    0x2A
#define PAD1_LIVE_DATA   0x10
#define PAD1_LIVE_EXTRA  0x11
#define PAD2_LIVE_DATA   0x12
#define PAD2_LIVE_EXTRA  0x13
static volatile unsigned char * const riser_gui =
    (volatile unsigned char *)RISER_BASE_ADDR;

/* Special return codes from gui_capture_button(). */
#define CAP_ABORT  -1
#define CAP_SKIP   -2
#define CAP_UNMAP  -3

/* Library-base storage.  amiga.lib startup auto-opens dos.library
 * and sets DOSBase / SysBase; we open intuition and graphics
 * manually in cmd_gui() and stash the bases here. */
struct IntuitionBase *IntuitionBase = 0L;
struct GfxBase       *GfxBase       = 0L;

/* Forward decl from risergamepad.c -- defined there because the GUI
 * runs alongside the existing text-mode commands. */
int cmd_gui(int pad);

/* Slot indices used by the firmware mapping.  Match the enum in
 * gamepad_map.h, but only the CD32-relevant ones get prompted. */
enum {
    PAD_FIRE1 = 0, PAD_FIRE2, PAD_FIRE3,
    PAD_PLAY, PAD_RW, PAD_FF,
    PAD_GREEN, PAD_YELLOW, PAD_RED, PAD_BLUE
};

/* CD32 button layout in our 440x200 window.  Coordinates are within
 * the window's interior (Intuition's RPort origin is the window's
 * upper-left, but drawing into the title-bar / borders is clipped). */
typedef struct {
    WORD x, y, w, h;
    const char *label;
    int slot;            /* index into the firmware mapping array */
} pad_button_t;

static const pad_button_t pad_buttons[] = {
    /* Shoulder buttons at the top, pulled in slightly from the edges */
    {  50,  35,  50, 22, "RW",     PAD_RW     },
    { 340,  35,  50, 22, "FF",     PAD_FF     },
    /* Action buttons on the right, two rows.  Top row green+yellow,
     * bottom row red+blue.  RED (= fire1) is slightly larger to
     * reflect that it's the primary fire button. */
    { 260,  75,  50, 26, "GREEN",  PAD_GREEN  },
    { 340,  75,  50, 26, "YELLOW", PAD_YELLOW },
    { 254, 110,  62, 32, "RED",    PAD_RED    },
    { 340, 113,  50, 26, "BLUE",   PAD_BLUE   },
    /* PLAY centered, bottom edge aligned with the bottom of RED */
    { 170, 120,  60, 22, "PLAY",   PAD_PLAY   },
};
#define N_PAD_BUTTONS  (sizeof(pad_buttons)/sizeof(pad_buttons[0]))

/* Draw one button: outline + label.  If `highlighted` is set, fill
 * the interior with pen 3 (the highlight color, typically blue on a
 * default Workbench palette) before drawing the outline. */
static void draw_button(struct RastPort *rp, const pad_button_t *b,
                        int highlighted)
{
    WORD x1 = b->x, y1 = b->y;
    WORD x2 = b->x + b->w, y2 = b->y + b->h;
    int len = (int)strlen(b->label);

    /* Background: highlight=pen 3, normal=pen 0 (Workbench grey). */
    SetAPen(rp, (LONG)(highlighted ? 3 : 0));
    RectFill(rp, x1, y1, x2, y2);

    /* Outline in pen 1 (black). */
    SetAPen(rp, (LONG)1);
    Move(rp, x1, y1);  Draw(rp, x2, y1);
    Draw(rp, x2, y2);  Draw(rp, x1, y2);  Draw(rp, x1, y1);

    /* Label centered-ish.  Each Topaz character is 8 px wide. */
    SetDrMd(rp, (LONG)JAM1);
    SetAPen(rp, (LONG)1);
    Move(rp, x1 + (b->w - len * 8) / 2, y1 + (b->h + 8) / 2);
    Text(rp, (UBYTE*)b->label, (LONG)len);
}

/* Draw the D-pad as a plus-shape outline on the left side.
 * Vertical arm: x=70..100, y=70..145; horizontal arm: x=40..130, y=95..120.
 * Bottom of the D-pad (y=145) lines up with the top of PLAY. */
static void draw_dpad(struct RastPort *rp)
{
    SetAPen(rp, (LONG)1);
    Move(rp,  70,  70);  Draw(rp, 100,  70);
    Draw(rp, 100,  95);  Draw(rp, 130,  95);
    Draw(rp, 130, 120);  Draw(rp, 100, 120);
    Draw(rp, 100, 145);  Draw(rp,  70, 145);
    Draw(rp,  70, 120);  Draw(rp,  40, 120);
    Draw(rp,  40,  95);  Draw(rp,  70,  95);
    Draw(rp,  70,  70);
    /* Label inside the horizontal arm */
    Move(rp, 65, 112);
    Text(rp, (UBYTE*)"D-PAD", (LONG)5);
}

/* Draw header / status / hint text. */
static void draw_chrome(struct RastPort *rp, int pad,
                        const char *prompt, const char *hint)
{
    SetAPen(rp, (LONG)1);
    SetDrMd(rp, (LONG)JAM1);

    Move(rp, 8, 22);
    if (pad == 1) {
        Text(rp, (UBYTE*)"Mapping the joystick-port gamepad",
             (LONG)33);
    } else {
        Text(rp, (UBYTE*)"Mapping the mouse-port gamepad",
             (LONG)30);
    }

    if (prompt) {
        Move(rp, 8, 183);
        Text(rp, (UBYTE*)prompt, (LONG)strlen(prompt));
    }
    if (hint) {
        Move(rp, 8, 195);
        Text(rp, (UBYTE*)hint, (LONG)strlen(hint));
    }
}

/* Redraw the full pad picture with one button highlighted (pass -1
 * for no highlight). */
static void draw_pad(struct Window *win, int pad, int highlight_slot,
                     const char *prompt, const char *hint)
{
    struct RastPort *rp = win->RPort;
    int i;

    /* Clear the content area to pen 0. */
    SetAPen(rp, (LONG)0);
    RectFill(rp, 4, 12, win->Width - 4, win->Height - 4);

    draw_chrome(rp, pad, prompt, hint);
    draw_dpad(rp);
    for (i = 0; i < (int)N_PAD_BUTTONS; i++) {
        int hl = (pad_buttons[i].slot == highlight_slot);
        draw_button(rp, &pad_buttons[i], hl);
    }
}

#define WIN_WIDTH   440
#define WIN_HEIGHT  200

static struct NewWindow gui_newwindow = {
    20, 20,                                          /* LeftEdge, TopEdge */
    WIN_WIDTH, WIN_HEIGHT,                           /* Width, Height */
    0, 1,                                            /* DetailPen, BlockPen */
    IDCMP_CLOSEWINDOW | IDCMP_VANILLAKEY,            /* IDCMPFlags */
    WFLG_CLOSEGADGET | WFLG_DRAGBAR | WFLG_DEPTHGADGET
        | WFLG_ACTIVATE | WFLG_SMART_REFRESH
        | WFLG_NOCAREREFRESH,                        /* Flags */
    0L,                                              /* FirstGadget */
    0L,                                              /* CheckMark */
    (UBYTE*)"risergamepad: map your gamepad",        /* Title */
    0L,                                              /* Screen */
    0L,                                              /* BitMap */
    100, 50,                                         /* MinWidth, MinHeight */
    640, 256,                                        /* MaxWidth, MaxHeight */
    WBENCHSCREEN
};

/* Read the live USB-pad state via the riser ($10/$11 for pad 1,
 * $12/$13 for pad 2).  Returns the 16-bit combined word:
 *   bits 0..3  = D-pad (R/L/D/U)
 *   bits 4..15 = USB buttons 0..11 */
static unsigned int gui_read_live(int pad)
{
    unsigned char d, e;
    if (pad == 1) {
        d = riser_gui[PAD1_LIVE_DATA];
        e = riser_gui[PAD1_LIVE_EXTRA];
    } else {
        d = riser_gui[PAD2_LIVE_DATA];
        e = riser_gui[PAD2_LIVE_EXTRA];
    }
    return ((unsigned int)e << 8) | (unsigned int)d;
}

static unsigned char gui_reg_addr(int pad, int slot)
{
    return (unsigned char)((pad == 1 ? PAD1_REG_BASE : PAD2_REG_BASE) + slot);
}

/* Wait for the user to either press a button on the USB pad (returns
 * its bit index, 4..15) or hit a keyboard shortcut (returns one of
 * CAP_* codes).  Polls the live USB state on each Delay() tick.
 * Requires the same button bit pattern to be seen for two
 * consecutive ticks before accepting, so brief noise doesn't win. */
static int gui_capture_button(struct Window *win, int pad)
{
    unsigned int prev_btns = 0;
    int stable = 0;
    int candidate = -1;
    unsigned int debug_tick = 0;

    for (;;) {
        struct IntuiMessage *msg;
        unsigned int w, btns;

        /* Drain any queued IDCMP messages (close gadget / keyboard). */
        while ((msg = (struct IntuiMessage*)GetMsg(win->UserPort)) != 0L) {
            ULONG class = msg->Class;
            UWORD code  = msg->Code;
            ReplyMsg((struct Message*)msg);

            if (class == IDCMP_CLOSEWINDOW) return CAP_ABORT;
            if (class == IDCMP_VANILLAKEY) {
                if (code == 'q' || code == 'Q' || code == 27) return CAP_ABORT;
                if (code == 's' || code == 'S' || code == ' ') return CAP_SKIP;
                if (code == 'u' || code == 'U') return CAP_UNMAP;
            }
        }
        if (CheckSignal(SIGBREAKF_CTRL_C)) return CAP_ABORT;

        /* Poll USB state. */
        w = gui_read_live(pad);
        btns = w & 0xFFF0u;     /* mask out D-pad */

        /* DEBUG: print live state above the prompt every loop so we
         * can see whether the read works. */
        {
            char dbg[48];
            struct RastPort *rp = win->RPort;
            int n;
            debug_tick++;
            n = sprintf(dbg, "tick=%u $10=%02X $11=%02X w=%04X stable=%d",
                        debug_tick,
                        (unsigned)riser_gui[PAD1_LIVE_DATA],
                        (unsigned)riser_gui[PAD1_LIVE_EXTRA],
                        w, stable);
            SetAPen(rp, (LONG)0);
            RectFill(rp, 8, 162, win->Width - 8, 174);
            SetAPen(rp, (LONG)1);
            SetDrMd(rp, (LONG)JAM1);
            Move(rp, 8, 172);
            Text(rp, (UBYTE*)dbg, (LONG)n);
        }

        if (btns == 0) {
            stable = 0;
            candidate = -1;
        } else {
            int b;
            for (b = 4; b < 16; b++) {
                if (btns & (1u << b)) { candidate = b; break; }
            }
            if (btns == prev_btns) {
                if (++stable >= 3) {
                    /* Capture confirmed.  Wait for the user to
                     * release before returning so the NEXT slot's
                     * capture starts from a clean idle state -- this
                     * is what stops the GUI from speed-running
                     * through all slots while one button is held. */
                    int captured = candidate;
                    while (gui_read_live(pad) & 0xFFF0u) {
                        struct IntuiMessage *mm;
                        while ((mm = (struct IntuiMessage*)
                                GetMsg(win->UserPort)) != 0L) {
                            ULONG cl = mm->Class;
                            UWORD cd = mm->Code;
                            ReplyMsg((struct Message*)mm);
                            if (cl == IDCMP_CLOSEWINDOW) return CAP_ABORT;
                            if (cl == IDCMP_VANILLAKEY
                                && (cd == 'q' || cd == 'Q' || cd == 27))
                                return CAP_ABORT;
                        }
                        if (CheckSignal(SIGBREAKF_CTRL_C)) return CAP_ABORT;
                        Delay((LONG)1);
                    }
                    return captured;
                }
            } else {
                stable = 1;
            }
        }
        prev_btns = btns;

        Delay((LONG)1);   /* 1 tick = 20ms */
    }
}

/* Order to walk through the CD32 buttons.  RED first because it's the
 * primary fire button -- gets the user starting with the most common
 * mapping action. */
static const int gui_prompt_order[] = {
    PAD_RED, PAD_GREEN, PAD_BLUE, PAD_YELLOW,
    PAD_PLAY, PAD_RW, PAD_FF
};
#define N_PROMPTS  (sizeof(gui_prompt_order)/sizeof(gui_prompt_order[0]))

/* Pretty name for a slot index (used in the per-slot prompt line). */
static const char *gui_slot_name(int slot)
{
    switch (slot) {
    case PAD_RED:    return "RED";
    case PAD_GREEN:  return "GREEN";
    case PAD_BLUE:   return "BLUE";
    case PAD_YELLOW: return "YELLOW";
    case PAD_PLAY:   return "PLAY";
    case PAD_RW:     return "RW";
    case PAD_FF:     return "FF";
    }
    return "?";
}

/* Verify mode: stay open after save, light up each picture button
 * while its mapped USB bit is held.  User exits via close gadget /
 * q / Esc / Ctrl-C. */
static void gui_verify_mode(struct Window *win, int pad,
                            const unsigned char *src)
{
    unsigned int prev_btns = 0xFFFFFFFFu;   /* force first redraw */
    int done = 0;

    draw_pad(win, pad, -1,
             "Saved!  Press buttons -- they light up if mapped correctly.",
             "q / Esc / close window = exit");

    while (!done) {
        struct IntuiMessage *msg;
        unsigned int w, btns;

        /* Drain any messages without blocking. */
        while ((msg = (struct IntuiMessage*)GetMsg(win->UserPort)) != 0L) {
            ULONG class = msg->Class;
            UWORD code  = msg->Code;
            ReplyMsg((struct Message*)msg);

            if (class == IDCMP_CLOSEWINDOW) { done = 1; }
            else if (class == IDCMP_VANILLAKEY) {
                if (code == 'q' || code == 'Q' || code == 27) done = 1;
            }
        }
        if (CheckSignal(SIGBREAKF_CTRL_C)) break;

        w = gui_read_live(pad);
        btns = w & 0xFFF0u;
        if (btns != prev_btns) {
            int i;
            struct RastPort *rp = win->RPort;
            SetAPen(rp, (LONG)0);
            RectFill(rp, 4, 12, win->Width - 4, win->Height - 4);
            draw_chrome(rp, pad,
                "Press buttons -- they light up if mapped correctly.",
                "q / Esc / close window = exit");
            draw_dpad(rp);
            for (i = 0; i < (int)N_PAD_BUTTONS; i++) {
                int slot = pad_buttons[i].slot;
                int s = (slot >= 0 && slot < SLOTS) ? src[slot] : UNMAPPED;
                int hl = (s != UNMAPPED) && (btns & (1u << s)) ? 1 : 0;
                draw_button(rp, &pad_buttons[i], hl);
            }
            prev_btns = btns;
        }

        Delay((LONG)2);   /* 40ms */
    }
}

/* Capture flow: iterate through the CD32 buttons, prompt the user
 * for each, write mappings, then go into verify mode.  Returns 0 on
 * normal exit, non-zero if the user aborted. */
static int cmd_gui_run(struct Window *win, int pad)
{
    unsigned char new_src[SLOTS];
    int i, slot;
    char prompt[80];
    const char *hint = "Space=skip  u=unmap  q=quit";

    /* Preload current mappings so skip keeps them. */
    for (i = 0; i < SLOTS; i++) {
        new_src[i] = riser_gui[gui_reg_addr(pad, i)];
    }

    /* CD32 has no separate fire2/fire3 -- force them unmapped per
     * the GUI's design.  User can still set them via text learn if
     * they want something there. */
    new_src[PAD_FIRE2] = UNMAPPED;
    new_src[PAD_FIRE3] = UNMAPPED;

    for (i = 0; i < (int)N_PROMPTS; i++) {
        int captured;
        slot = gui_prompt_order[i];

        sprintf(prompt, "Press the %s button on your gamepad.",
                gui_slot_name(slot));
        draw_pad(win, pad, slot, prompt, hint);

        captured = gui_capture_button(win, pad);
        if (captured == CAP_ABORT) return 1;
        if (captured == CAP_SKIP)  continue;       /* keep current value */
        if (captured == CAP_UNMAP) {
            new_src[slot] = UNMAPPED;
            if (slot == PAD_RED) new_src[PAD_FIRE1] = UNMAPPED;
            continue;
        }

        /* Real button capture.  RED also writes the fire1 slot so
         * both labels point at the same USB bit. */
        new_src[slot] = (unsigned char)captured;
        if (slot == PAD_RED) new_src[PAD_FIRE1] = (unsigned char)captured;
    }

    /* Apply all mappings.  Auto-save triggers in firmware on each
     * write to $20..$33. */
    for (i = 0; i < SLOTS; i++) {
        riser_gui[gui_reg_addr(pad, i)] = new_src[i];
    }

    gui_verify_mode(win, pad, new_src);
    return 0;
}

int cmd_gui(int pad)
{
    struct Window *win;
    int done = 0;

    IntuitionBase = (struct IntuitionBase*)OpenLibrary("intuition.library", 36);
    if (!IntuitionBase) {
        printf("Couldn't open intuition.library (need 36+)\n");
        return 1;
    }
    GfxBase = (struct GfxBase*)OpenLibrary("graphics.library", 36);
    if (!GfxBase) {
        printf("Couldn't open graphics.library (need 36+)\n");
        CloseLibrary((struct Library*)IntuitionBase);
        return 1;
    }

    win = OpenWindow(&gui_newwindow);
    if (!win) {
        printf("Couldn't open window\n");
        CloseLibrary((struct Library*)GfxBase);
        CloseLibrary((struct Library*)IntuitionBase);
        return 1;
    }

    cmd_gui_run(win, pad);
    (void)done;

    CloseWindow(win);
    CloseLibrary((struct Library*)GfxBase);
    CloseLibrary((struct Library*)IntuitionBase);
    return 0;
}
