/*
 * risergamepad.c -- configure TFRiser USB gamepad button mapping.
 *
 * Compile on AmigaOS with vbcc:
 *     vc -+ -o risergamepad risergamepad.c
 *
 * The TFRiser is memory-mapped at $00BA0000.
 *
 *   $00BA0020..$00BA0029  pad 1 mapping slots 0..9
 *   $00BA002A..$00BA0033  pad 2 mapping slots 0..9
 *   $00BA0034  pad 1 live data byte    (D-pad + USB buttons 0..3)
 *   $00BA0035  pad 1 live extraBtn     (USB buttons 4..11)
 *   $00BA0036  pad 2 live data byte
 *   $00BA0037  pad 2 live extraBtn
 *   $00BA0038  HS port device type (pad-1 jack)  0=none 1=kb 2=mouse 3=pad
 *   $00BA0039  FS port device type (pad-2 jack)
 *
 * Slot order matches the firmware GMAP_* enum:
 *   0 fire1   1 fire2   2 fire3
 *   3 play    4 rw      5 ff
 *   6 green   7 yellow  8 red    9 blue
 *
 * Each mapping byte holds a "source" value:
 *   0..15 = bit index in the combined 16-bit USB-button word
 *           ((extraBtn << 8) | data); D-pad is bits 0..3.
 *   0xFF  = unmapped.
 *
 *   bit 4..7 = USB buttons 0..3   (= "b0".."b3" in this tool)
 *   bit 8..15 = USB buttons 4..11 (= "b4".."b11")
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Minimal <exec/types.h> stubs so <proto/dos.h> can be included without
 * needing the full AmigaOS NDK installed alongside vbcc. */
#ifndef EXEC_TYPES_H
#define EXEC_TYPES_H
typedef signed char        BYTE;
typedef unsigned char      UBYTE;
typedef short              WORD;
typedef unsigned short     UWORD;
typedef long               LONG;
typedef unsigned long      ULONG;
typedef long               BPTR;
typedef void              *APTR;
typedef char              *STRPTR;
typedef const char        *CONST_STRPTR;
typedef const void        *CONST_APTR;
typedef short              BOOL;
#define CONST const
#define VOID  void
#endif
#define SIGBREAKF_CTRL_C 0x00001000

/* Skip clib (we don't have it); pull in vbcc's inline-asm prototypes. */
#define CLIB_DOS_PROTOS_H
struct DosLibrary;
extern struct DosLibrary *DOSBase;
extern struct ExecBase *SysBase;
#include <inline/dos_protos.h>

#define RISER_BASE_ADDR 0x00BA0000UL

static volatile unsigned char * const riser =
    (volatile unsigned char *)RISER_BASE_ADDR;

#define SLOTS 10
#define UNMAPPED 0xFF
#define PAD1_REG_BASE 0x20
#define PAD2_REG_BASE 0x2A
/* NOTE: the riser's external address decoder only routes accesses to the
 * STM32 for the original used regions ($01-$0F and $20-$33), plus the gap
 * at $10-$1F.  Addresses in $34-$3F never reach the chip. */
#define PAD1_LIVE_DATA  0x10
#define PAD1_LIVE_EXTRA 0x11
#define PAD2_LIVE_DATA  0x12
#define PAD2_LIVE_EXTRA 0x13

/* Per-port device type: identifies which physical USB jack maps to which
 * pad slot, even when no gamepad is connected. */
#define PAD1_PORT_TYPE  0x14  /* HS port -> pad 1 slot */
#define PAD2_PORT_TYPE  0x15  /* FS port -> pad 2 slot */
#define SENTINEL_REG    0x16  /* always reads 0xA5 if bus path works */
#define RAW_WIN_SELECT  0x17  /* write 0/8/16/24 to pick raw window; read = HID report length */
#define RAW_REPORT_BASE 0x18  /* $18..$1F: 8-byte window into raw_report[window..window+7] */
#define RAW_WIN_SIZE    8
#define RAW_REPORT_TOTAL 32   /* firmware mirrors first 32 bytes of HID report */
#define PORT_NONE     0
#define PORT_KEYBOARD 1
#define PORT_MOUSE    2
#define PORT_GAMEPAD  3

static const char *slot_name[SLOTS] = {
    "fire1", "fire2", "fire3",
    "play",  "rw",    "ff",
    "green", "yellow","red",   "blue"
};

/* ------------------------------------------------------------------ */
/* Hardware presence check.                                            */
/* ------------------------------------------------------------------ */

/* The riser maps a fixed sentinel byte at $16 that reads back 0xA5
 * whenever the Zorro/STM32 bus path is alive.  With no board fitted the
 * address space floats and reads come back as 0xFF, so a single read
 * tells us whether there is a riser to talk to at all. */
static int riser_present(void)
{
    return riser[SENTINEL_REG] == 0xA5;
}

static void riser_not_found(void)
{
    printf("TF CD32 Riser not detected.\n\n");
    printf("No riser board responded at $%06lX.  Check that the board is\n"
           "fitted and that you are running this on the Amiga/CD32 it is\n"
           "installed in.\n", RISER_BASE_ADDR);
}

/* ------------------------------------------------------------------ */
/* Slot/register helpers.                                              */
/* ------------------------------------------------------------------ */

static unsigned char reg_addr(int pad, int slot)
{
    return (unsigned char)((pad == 1 ? PAD1_REG_BASE : PAD2_REG_BASE) + slot);
}

static const char *describe_bit(int bit)
{
    static char buf[24];
    if (bit == UNMAPPED) return "unmapped";
    if (bit < 4) {
        const char *d[4] = {"D-right","D-left","D-down","D-up"};
        sprintf(buf, "%s (bit %d)", d[bit], bit);
        return buf;
    }
    sprintf(buf, "USB btn %d (bit %d)", bit - 4, bit);
    return buf;
}

static const char *port_name(unsigned char t)
{
    switch (t) {
    case PORT_KEYBOARD: return "keyboard";
    case PORT_MOUSE:    return "mouse";
    case PORT_GAMEPAD:  return "gamepad";
    default:            return "(empty)";
    }
}

static const char *gstate_name(unsigned char s)
{
    switch (s & 0x7F) {
    case 0:  return "IDLE";
    case 1:  return "WAIT_ATTACH";
    case 2:  return "ATTACHED";
    case 3:  return "DISCONNECTED";
    case 4:  return "DETECT_SPEED";
    case 5:  return "ENUMERATION";
    case 6:  return "CLASS_REQUEST";
    case 7:  return "INPUT";
    case 8:  return "SET_CONFIG";
    case 9:  return "SET_WAKEUP";
    case 10: return "CHECK_CLASS";
    case 11: return "CLASS";
    case 12: return "SUSPENDED";
    case 13: return "ABORT";
    default: return "?";
    }
}

/* User-facing summary: what's driving each Amiga input.  Three rows
 * (joystick / mouse / keyboard), each pointing to either a USB device
 * (with VID:PID identifier) or the Amiga's built-in input.  Casual
 * users see this; `risergamepad debug` prints the USB-host
 * diagnostics for when something isn't enumerating. */
static void show_status(void)
{
    unsigned char hs_jack = riser[PAD1_PORT_TYPE];
    unsigned char fs_jack = riser[PAD2_PORT_TYPE];
    unsigned char sentinel = riser[SENTINEL_REG];

    /* Active gamepad VID/PIDs per Amiga port slot.  First USB pad
     * lands on the joystick port, second pad (if any) on the
     * mouse-shared port. */
    unsigned int p1_vid = (unsigned)riser[0x08] | ((unsigned)riser[0x09] << 8);
    unsigned int p1_pid = (unsigned)riser[0x3B] | ((unsigned)riser[0x3C] << 8);
    unsigned int p2_vid = (unsigned)riser[0x3D] | ((unsigned)riser[0x3E] << 8);

    int kbd_attached = (hs_jack == PORT_KEYBOARD) || (fs_jack == PORT_KEYBOARD);
    int mouse_attached = (hs_jack == PORT_MOUSE) || (fs_jack == PORT_MOUSE);

    if (sentinel != 0xA5) {
        printf("WARNING: bus sentinel $16=%02X (expected A5) "
               "-- Riser/STM32 path broken\n\n", sentinel);
    }

    printf("Amiga inputs (taken over when a USB device is connected):\n");

    /* Joystick port (Amiga port 2 / joy1dat): always the first USB
     * gamepad if any is plugged in.  No USB -> Amiga's own joystick
     * port is whatever's on its jack (we can't see non-USB input). */
    if (p1_vid) {
        printf("  Joystick port : USB gamepad [%04x:%04x]\n", p1_vid, p1_pid);
    } else {
        printf("  Joystick port : (no USB device)\n");
    }

    /* Mouse port (Amiga port 1 / joy0dat): USB mouse takes priority
     * (firmware blocks gamepad2 input when a USB mouse is present);
     * otherwise the second USB gamepad; otherwise nothing -- the
     * Amiga's own mouse jack is used. */
    if (mouse_attached) {
        printf("  Mouse port    : USB mouse\n");
    } else if (p2_vid) {
        printf("  Mouse port    : USB gamepad [VID %04x] (2nd gamepad)\n",
               p2_vid);
    } else {
        printf("  Mouse port    : (no USB device)\n");
    }

    /* Keyboard: USB keyboard if attached, else Amiga keyboard. */
    if (kbd_attached) {
        printf("  Keyboard      : USB keyboard\n");
    } else {
        printf("  Keyboard      : (no USB device)\n");
    }
}

/* Detailed debug view, mostly useful when something isn't enumerating
 * properly.  Shows USB-host gState / EnumState / iface_init /
 * ctl_state per host. */
static void show_debug(void)
{
    /* Formatted for a 77-column maximised AmigaShell window: each
     * line stays <= 77 chars even when every field hits its longest
     * label (e.g. "GET_FULL_CFG_DESC", "READHIDRPTDESC"). */
    unsigned char p1 = riser[PAD1_PORT_TYPE];
    unsigned char p2 = riser[PAD2_PORT_TYPE];
    unsigned char sentinel = riser[SENTINEL_REG];
    unsigned char hs = riser[0x34];
    unsigned char fs = riser[0x35];
    unsigned char hs_enum = riser[0x07];
    unsigned char fs_enum = riser[0x05];
    unsigned char hs_iface = riser[0x02];
    unsigned char fs_iface = riser[0x00];
    unsigned char hs_inum = riser[0x04];
    unsigned char fs_inum = riser[0x03];
    unsigned char hs_ctl = riser[0x36];
    unsigned char fs_ctl = riser[0x37];

    static const char *enames[] = {
        "IDLE","GET_FULL_DEV_DESC","SET_ADDR","GET_CFG_DESC",
        "GET_FULL_CFG_DESC","GET_MFC_STR","GET_PROD_STR","GET_SN_STR"
    };
    static const char *inames[] = {
        "INIT","READHID","READHIDRPTDESC",
        "INITSUBCLASS","INITENDPNT","SELECTIFACE"
    };
    static const char *ctlnames[] = {
        "INIT","IDLE","GET_REPORT_DESC","GET_HID_DESC",
        "SET_IDLE","SET_PROTOCOL","SET_REPORT"
    };

    const char *hsen = (hs_enum < 8) ? enames[hs_enum] : "?";
    const char *fsen = (fs_enum < 8) ? enames[fs_enum] : "?";
    const char *hsif = (hs_iface < 6) ? inames[hs_iface] : "?";
    const char *fsif = (fs_iface < 6) ? inames[fs_iface] : "?";
    const char *hsct = (hs_ctl < 7) ? ctlnames[hs_ctl] : "?";
    const char *fsct = (fs_ctl < 7) ? ctlnames[fs_ctl] : "?";

    unsigned int p1_vid = (unsigned)riser[0x08] | ((unsigned)riser[0x09] << 8);
    unsigned int p1_pid = (unsigned)riser[0x3B] | ((unsigned)riser[0x3C] << 8);
    unsigned int p2_vid = (unsigned)riser[0x3D] | ((unsigned)riser[0x3E] << 8);

    printf("USB ports (debug):\n");
    printf("  HS jack: %-8s [$14=%02X]   FS jack: %-8s [$15=%02X]\n",
           port_name(p1), p1, port_name(p2), p2);
    if (sentinel != 0xA5) {
        printf("  WARNING: bus sentinel $16=%02X (expected A5) "
               "-- riser/STM32 path broken\n", sentinel);
    }
    printf("\n");
    printf("  HS  gState=%-13s conn=%d  enum=%s\n",
           gstate_name(hs), (hs & 0x80) ? 1 : 0, hsen);
    printf("      iface=%s/i%u  ctl=%s\n",
           hsif, (unsigned)hs_inum, hsct);
    printf("\n");
    printf("  FS  gState=%-13s conn=%d  enum=%s\n",
           gstate_name(fs), (fs & 0x80) ? 1 : 0, fsen);
    printf("      iface=%s/i%u  ctl=%s\n",
           fsif, (unsigned)fs_inum, fsct);
    printf("\n");
    if (p1_vid) {
        printf("  Joystick port: VID:PID %04x:%04x\n", p1_vid, p1_pid);
    } else {
        printf("  Joystick port: (no gamepad)\n");
    }
    if (p2_vid) {
        printf("  Mouse port:    VID %04x\n", p2_vid);
    } else {
        printf("  Mouse port:    (no gamepad)\n");
    }
}

static void show(int pad)
{
    int i;
    printf("%s mapping:\n",
           (pad == 1) ? "Joystick port" : "Mouse port");
    for (i = 0; i < SLOTS; i++) {
        unsigned char r = reg_addr(pad, i);
        unsigned char v = riser[r];
        printf("  %-7s [reg $%02X] = %s\n",
               slot_name[i], r, describe_bit(v));
    }
}

static void apply(int pad, const unsigned char *src)
{
    int i;
    for (i = 0; i < SLOTS; i++) riser[reg_addr(pad, i)] = src[i];
}

/* ------------------------------------------------------------------ */
/* Live state helpers (watch / learn).                                 */
/* ------------------------------------------------------------------ */

static unsigned int read_live(int pad)
{
    unsigned char d, e;
    if (pad == 1) {
        d = riser[PAD1_LIVE_DATA];
        e = riser[PAD1_LIVE_EXTRA];
    } else {
        d = riser[PAD2_LIVE_DATA];
        e = riser[PAD2_LIVE_EXTRA];
    }
    return ((unsigned int)e << 8) | (unsigned int)d;
}

static void print_live(unsigned int w)
{
    static const char *dpad_label[4] = {"R","L","D","U"};
    int i, first;
    printf("$%04X  dpad:", w);
    first = 1;
    for (i = 0; i < 4; i++) {
        if (w & (1u << i)) {
            printf("%s%s", first ? " " : "+", dpad_label[i]);
            first = 0;
        }
    }
    if (first) printf(" -");
    printf("   buttons:");
    first = 1;
    for (i = 4; i < 16; i++) {
        if (w & (1u << i)) {
            printf("%sb%d", first ? " " : ",", i - 4);
            first = 0;
        }
    }
    if (first) printf(" -");
    printf("\n");
}

/* Drain any pending characters from CON: (e.g. the CR/LF that the shell
 * delivered when launching this program). Caller must already be in
 * raw mode. */
static void drain_input(BPTR cin)
{
    while (WaitForChar(cin, 1)) {
        char c;
        if (Read(cin, &c, 1) <= 0) break;
    }
}

/* Wait until at least one button bit (>=4) is held for ~60 ms steady,
 * then return the lowest such bit.  Returns -1 if user pressed Ctrl-C
 * or 'q'.  Returns -2 on Enter (skip).  Returns -3 on 'u'. */
static int capture_button(int pad)
{
    BPTR cin = Input();
    unsigned int prev = 0;
    int stable_ticks = 0;
    int chosen = -1;

    SetMode(cin, 1);     /* raw */
    drain_input(cin);    /* throw away the launching CR/LF */

    for (;;) {
        unsigned int w = read_live(pad);
        unsigned int btns = w & 0xFFF0u;

        if (btns == 0) {
            stable_ticks = 0;
            chosen = -1;
        } else {
            int b;
            for (b = 4; b < 16; b++) {
                if (btns & (1u << b)) { chosen = b; break; }
            }
            if (btns == prev) {
                if (++stable_ticks >= 3) {     /* 3 * 20ms = 60 ms steady */
                    SetMode(cin, 0);
                    return chosen;
                }
            } else {
                stable_ticks = 1;
            }
        }
        prev = btns;

        if (WaitForChar(cin, 1)) {
            char c = 0;
            if (Read(cin, &c, 1) > 0) {
                if (c == '\n' || c == '\r') { SetMode(cin, 0); return -2; }
                if (c == 'u' || c == 'U')   { SetMode(cin, 0); return -3; }
                if (c == 'q' || c == 'Q')   { SetMode(cin, 0); return -1; }
                /* any other key: keep going */
            }
        }

        if (CheckSignal(SIGBREAKF_CTRL_C)) {
            SetMode(cin, 0); return -1;
        }

        Delay(1);   /* one 50Hz tick = 20 ms; keeps polling sane */
    }
}

/* ------------------------------------------------------------------ */
/* Sub-commands.                                                       */
/* ------------------------------------------------------------------ */

static int cmd_watch(int pad)
{
    BPTR cin = Input();
    unsigned int last = ~0u;
    printf("Watching the %s-port gamepad.  Press buttons to see them light up.\n",
           (pad == 1) ? "joystick" : "mouse");
    printf("Press Enter or Ctrl-C to stop.\n\n");
    fflush(stdout);

    SetMode(cin, 1);
    drain_input(cin);   /* discard the shell's launching CR/LF */

    for (;;) {
        unsigned int w = read_live(pad);
        if (w != last) {
            print_live(w);
            fflush(stdout);
            last = w;
        }
        if (WaitForChar(cin, 50000)) {       /* 50 ms */
            char c = 0;
            if (Read(cin, &c, 1) > 0) {
                if (c == '\n' || c == '\r' || c == 0x03) break;
            }
        }
        if (CheckSignal(SIGBREAKF_CTRL_C)) break;
    }
    SetMode(cin, 0);
    return 0;
}

/* Read all 32 raw report bytes by stepping the window selector through
 * 0,8,16,24 and pulling each 8-byte window via $18..$1F. */
static void read_all_raw(unsigned char *out)
{
    int win, i;
    for (win = 0; win < RAW_REPORT_TOTAL; win += RAW_WIN_SIZE) {
        riser[RAW_WIN_SELECT] = (unsigned char)win;
        for (i = 0; i < RAW_WIN_SIZE; i++)
            out[win + i] = riser[RAW_REPORT_BASE + i];
    }
}

/* Show raw HID report bytes from the currently-connected gamepad.  Bypasses
 * the firmware's HID descriptor parser, so it works even when the parser
 * fails to decode this gamepad's button bits. */
static int cmd_raw(void)
{
    BPTR cin = Input();
    unsigned char curr[RAW_REPORT_TOTAL];
    unsigned char prev[RAW_REPORT_TOTAL];
    unsigned char hid_len;
    int i, first = 1;

    riser[RAW_WIN_SELECT] = 0;
    hid_len = riser[RAW_WIN_SELECT];   /* read-back returns actual HID report length */

    printf("Raw HID report (firmware mirrors first %d bytes; gamepad reports %u).\n",
           RAW_REPORT_TOTAL, (unsigned)hid_len);
    printf("Press buttons; changes shown.  Press Enter or Ctrl-C to stop.\n\n");
    fflush(stdout);

    for (i = 0; i < RAW_REPORT_TOTAL; i++) prev[i] = 0;

    SetMode(cin, 1);
    drain_input(cin);

    for (;;) {
        int changed = 0;
        read_all_raw(curr);
        for (i = 0; i < RAW_REPORT_TOTAL; i++)
            if (curr[i] != prev[i]) { changed = 1; break; }

        if (changed || first) {
            int row;
            for (row = 0; row < RAW_REPORT_TOTAL; row += 16) {
                printf("raw[%02d]: ", row);
                for (i = 0; i < 16 && row + i < RAW_REPORT_TOTAL; i++)
                    printf(" %02X", curr[row + i]);
                printf("\n");
            }
            if (!first) {
                printf("delta:  ");
                for (i = 0; i < RAW_REPORT_TOTAL; i++) {
                    unsigned char d = curr[i] ^ prev[i];
                    if (d) printf(" %02X", d);
                    else   printf(" ..");
                    if ((i & 0xF) == 0xF && i + 1 < RAW_REPORT_TOTAL) printf("\n        ");
                }
                printf("\n");
                /* Compact bit list, line-wrapped at ~72 chars.  Each
                 * entry is "b<byte>.<bit>=raw<idx>" so the user can
                 * read either the byte.bit position or the raw bit
                 * index (which is what `risergamepad <slot> N` takes). */
                printf("bits set:");
                int any = 0;
                int col = 9;             /* length of "bits set:" */
                int byte;
                for (byte = 0; byte < RAW_REPORT_TOTAL; byte++) {
                    int bit;
                    for (bit = 0; bit < 8; bit++) {
                        if (curr[byte] & (1u << bit)) {
                            int idx = byte * 8 + bit;
                            char piece[16];
                            int len = sprintf(piece, " b%d.%d=raw%d",
                                              byte, bit, idx);
                            if (col + len > 72) {
                                printf("\n         ");
                                col = 9;
                            }
                            printf("%s", piece);
                            col += len;
                            any = 1;
                        }
                    }
                }
                if (!any) printf(" (none)");
                printf("\n\n");
            } else {
                printf("\n");
            }
            fflush(stdout);
            for (i = 0; i < RAW_REPORT_TOTAL; i++) prev[i] = curr[i];
            first = 0;
        }

        if (WaitForChar(cin, 50000)) {
            char c = 0;
            if (Read(cin, &c, 1) > 0) {
                if (c == '\n' || c == '\r' || c == 0x03) break;
            }
        }
        if (CheckSignal(SIGBREAKF_CTRL_C)) break;
    }
    SetMode(cin, 0);
    return 0;
}

/* Interactive learn-raw.  For each slot, samples raw bytes continuously
 * while waiting for the user to press Enter, keeping the snapshot with
 * the largest XOR against baseline.  That way the user holds the button
 * THEN hits Enter -- the actual button-down state is captured even if
 * they release before/while pressing Enter.
 *
 * Uses line-buffered (cooked) reads so the terminal mode is never
 * altered -- no risk of leaving the shell in raw mode if the program
 * exits abruptly.  Output is one short line per slot for low-res
 * Amiga screens. */
static int cmd_learn_raw(void)
{
    static const char *raw_slot_names[] = {
        "UP   ", "DOWN ", "LEFT ", "RIGHT",
        "fire1", "fire2", "fire3",
        "play ", "rw   ", "ff   ",
        "green", "yelow", "red  ", "blue "
    };
    enum { N_RAW_SLOTS = 14 };
    unsigned char baseline[RAW_REPORT_TOTAL];
    unsigned char curr[RAW_REPORT_TOTAL];
    unsigned char captured[N_RAW_SLOTS][RAW_REPORT_TOTAL];
    int taken[N_RAW_SLOTS];
    int i, k;
    char line[8];
    BPTR cin = Input();

    printf("Learn-raw: discovers which raw HID bytes/bits each button uses.\n");
    printf("\n");

    /* Step 1: wait for the pad to start emitting reports.  Some pads
     * (e.g. 8BitDo SFC30) send NOTHING until the first state change,
     * so raw_report stays all-zeros after connect.  Poll non-blocking;
     * once we see any non-zero byte the pad is alive. */
    printf("Waiting for pad activity... ");
    fflush(stdout);
    {
        int t, k;
        int seen_nonzero = 0;
        int hinted = 0;
        for (t = 0; t < 2000; t++) {                  /* up to ~20 s */
            read_all_raw(baseline);
            for (k = 0; k < RAW_REPORT_TOTAL; k++) {
                if (baseline[k]) { seen_nonzero = 1; break; }
            }
            if (seen_nonzero) break;
            if (!hinted && t == 200) {                 /* ~2 s in */
                printf("\n  (no reports yet -- press and release any "
                       "button on the pad to\n  wake it, then this "
                       "should continue): ");
                fflush(stdout);
                hinted = 1;
            }
            (void)WaitForChar(cin, 10000);             /* 10 ms */
        }
        if (!seen_nonzero) {
            printf("\nWARNING: pad still all-zeros after 20 s.  "
                   "Continuing anyway.\n");
        } else {
            printf("ok.\n");
        }
    }

    /* Step 2: release everything and capture a baseline. */
    printf("Now RELEASE everything and press Enter to capture baseline\n");
    printf("('q' Enter to quit, 's' Enter to skip a slot):\n");
    fflush(stdout);
    if (Read(cin, line, sizeof(line)) <= 0) return 0;
    if (line[0] == 'q' || line[0] == 'Q') return 0;
    /* Sample for ~300 ms so the captured baseline is a steady frame
     * (the user may have just released a button and a transient
     * "release" report is still in flight). */
    {
        int t;
        for (t = 0; t < 30; t++) {
            read_all_raw(baseline);
            (void)WaitForChar(cin, 10000);
        }
    }
    printf("Baseline:");
    {
        int k;
        for (k = 0; k < RAW_REPORT_TOTAL; k++) {
            if ((k & 7) == 0) printf("\n  [%02d]", k);
            printf(" %02X", baseline[k]);
        }
        printf("\n\n");
    }
    fflush(stdout);

    for (i = 0; i < N_RAW_SLOTS; i++) taken[i] = 0;

    printf("Press and HOLD each button below until you press Enter.  "
           "If raw_report\nstill shows the previous button when a new slot "
           "starts, just release\nand wait a moment before pressing the new "
           "button.\nEnter alone = skip, s = skip, q = quit.\n\n");
    for (i = 0; i < N_RAW_SLOTS; i++) {
        printf("  [%s] : ", raw_slot_names[i]);
        fflush(stdout);

        SetMode(cin, 1);
        drain_input(cin);

        /* Step 1: wait up to 500 ms for raw_report to return to the
         * baseline state.  Stops the previous slot's button hold from
         * leaking into this slot's "last_diff" tracking. */
        {
            int t, k2;
            for (t = 0; t < 50; t++) {
                read_all_raw(curr);
                int differs = 0;
                for (k2 = 0; k2 < RAW_REPORT_TOTAL; k2++) {
                    if (curr[k2] != baseline[k2]) { differs = 1; break; }
                }
                if (!differs) break;
                if (WaitForChar(cin, 10000)) break;   /* user got impatient */
            }
        }

        /* Step 2: sample continuously until Enter.  Remember the most
         * recent non-baseline snapshot (last_diff) -- that's the
         * button-held state even if the user releases just before
         * pressing Enter (the common case). */
        unsigned char last_diff[RAW_REPORT_TOTAL];
        int have_diff = 0;
        int got = 0;
        char c = 0;
        for (;;) {
            read_all_raw(curr);
            int differs = 0;
            for (k = 0; k < RAW_REPORT_TOTAL; k++) {
                if (curr[k] != baseline[k]) { differs = 1; break; }
            }
            if (differs) {
                for (k = 0; k < RAW_REPORT_TOTAL; k++) last_diff[k] = curr[k];
                have_diff = 1;
            }
            if (WaitForChar(cin, 20000)) {
                if (Read(cin, &c, 1) > 0
                    && (c == '\n' || c == '\r' || c == 's' || c == 'S'
                        || c == 'q' || c == 'Q')) { got = 1; break; }
            }
            if (CheckSignal(SIGBREAKF_CTRL_C)) { c = 'q'; got = 1; break; }
        }
        SetMode(cin, 0);

        if (!got || c == 'q' || c == 'Q') { printf("done.\n"); break; }
        if (c == 's' || c == 'S')         { printf("skip.\n"); continue; }

        if (!have_diff) {
            printf("skip (no change from baseline).\n");
            continue;
        }

        for (k = 0; k < RAW_REPORT_TOTAL; k++) captured[i][k] = last_diff[k];
        taken[i] = 1;

        int any = 0;
        for (k = 0; k < RAW_REPORT_TOTAL; k++) {
            unsigned char d = last_diff[k] ^ baseline[k];
            if (d) {
                printf("%sbyte%d^%02X", any ? ", " : "", k, d);
                any = 1;
            }
        }
        printf("\n");
        fflush(stdout);
    }

    /* Final summary. */
    printf("\nBaseline:");
    for (k = 0; k < RAW_REPORT_TOTAL; k++) {
        if ((k & 0x7) == 0) printf("\n  [%02d]", k);
        printf(" %02X", baseline[k]);
    }
    printf("\n\nPer-button XOR (byte=value^delta):\n");
    for (i = 0; i < N_RAW_SLOTS; i++) {
        if (!taken[i]) continue;
        printf("  %s :", raw_slot_names[i]);
        int any = 0;
        for (k = 0; k < RAW_REPORT_TOTAL; k++) {
            unsigned char d = captured[i][k] ^ baseline[k];
            if (d) {
                printf("%s %d=%02X^%02X", any ? "," : "",
                       k, captured[i][k], d);
                any = 1;
            }
        }
        if (!any) printf(" (none)");
        printf("\n");
    }
    return 0;
}

static int cmd_learn(int pad)
{
    int i;
    unsigned char new_src[SLOTS];

    printf("Mapping the USB gamepad on the Amiga %s port.\n\n",
           (pad == 1) ? "joystick" : "mouse");
    printf("For each Amiga function below:\n");
    printf("  - HOLD the gamepad button you want, OR\n");
    printf("  - press [Enter] to keep the current setting, OR\n");
    printf("  - press [u] to leave it unmapped, OR\n");
    printf("  - press [q] to abort without saving.\n\n");

    /* preload current values so [Enter] keeps them */
    for (i = 0; i < SLOTS; i++)
        new_src[i] = riser[reg_addr(pad, i)];

    for (i = 0; i < SLOTS; i++) {
        printf("  %-7s (current = %s)  --> ",
               slot_name[i], describe_bit(new_src[i]));
        fflush(stdout);

        int b = capture_button(pad);
        if (b == -1) { printf("aborted, no changes saved.\n"); return 0; }
        if (b == -2) { printf("kept.\n"); continue; }
        if (b == -3) { new_src[i] = UNMAPPED; printf("unmapped.\n"); continue; }

        new_src[i] = (unsigned char)b;
        printf("captured %s\n", describe_bit(b));
    }

    apply(pad, new_src);
    printf("\nSaved.\n");
    show(pad);
    return 0;
}

/* ------------------------------------------------------------------ */

static void usage(void)
{
    printf(
      "risergamepad -- TFRiser USB gamepad mapper\n"
      "\n"
      "Most common:\n"
      "  risergamepad                  status + connected gamepad mappings\n"
      "  risergamepad learn            map your gamepad's buttons (interactive)\n"
      "  risergamepad watch            show live button state (which b<N> = which)\n"
      "\n"
      "How mappings work:\n"
      "  Each gamepad has its own button mapping, keyed by its USB VID:PID.\n"
      "  Plug the same gamepad back in and the mapping auto-loads regardless\n"
      "  of which USB jack you use.  Up to 4 gamepads stored.\n"
      "\n"
      "  A single connected gamepad drives the Amiga joystick port (labeled\n"
      "  \"1\" on the CD32).  A second gamepad (if plugged in alongside) drives\n"
      "  the mouse port (labeled \"2\" on the CD32) instead.\n"
      "\n"
      "Less common:\n"
      "  risergamepad ports            short status only\n"
      "  risergamepad watch-ports      status live (250 ms refresh)\n"
      "  risergamepad debug            USB-host diagnostic view\n"
      "  risergamepad watch-debug      diagnostic view live\n"
      "  risergamepad raw              raw HID report bytes (developer)\n"
      "  risergamepad learn-raw        discover raw bytes for an unsupported gamepad\n"
      "\n"
      "By default 'learn' and 'watch' work on the joystick-port gamepad; append\n"
      "'2' (e.g. 'risergamepad learn 2') to target the mouse-port one instead.\n");
}

int main(int argc, char *argv[])
{
    /* Launched from Workbench (double-clicked icon): the startup code
     * calls us with argc == 0 and no console attached.  There's nothing
     * useful to print to a non-existent shell, so go straight to the
     * graphical mapper -- which opens its own screen and shows the
     * "riser not detected" message there if the board is absent. */
    if (argc == 0) {
        extern int cmd_gui(int pad, int demo);
        return cmd_gui(1, 0);
    }

    /* Every command except `help` and `gui` reads the riser's
     * registers, so bail early with a clear message if the board isn't
     * there (instead of printing a screenful of 0xFF).  `gui` runs its
     * own detection -- it can operate in hardware-free demo mode and
     * shows the not-detected message on its own screen. */
    {
        int is_help = (argc == 2 && (strcmp(argv[1], "help") == 0
                                     || strcmp(argv[1], "--help") == 0));
        int is_gui  = (argc >= 2 && strcmp(argv[1], "gui") == 0);
        if (!is_help && !is_gui && !riser_present()) {
            riser_not_found();
            return 1;
        }
    }

    if (argc == 1) {
        /* Default invocation: status + brief profile explanation
         * + tip pointing at `learn`.  This is the main user-facing
         * landing page; debug-y stuff is moved behind `debug`. */
        show_status();
        printf("\n");
        printf("Profiles:\n");
        printf("  Each gamepad has its own button mapping, identified by "
               "its USB VID:PID.\n");
        printf("  Plug the same gamepad back in and the mapping is "
               "auto-loaded,\n  regardless of which USB jack you use.  "
               "Up to 4 gamepads stored.\n");
        printf("\n");
        printf("To map your gamepad's buttons:  risergamepad learn\n");
        printf("For all commands:               risergamepad help\n");

        int p1_present = (riser[0x08] | riser[0x09]) != 0;
        int p2_present = (riser[0x3D] | riser[0x3E]) != 0;
        if (p1_present || p2_present) {
            printf("\n");
            if (p1_present) show(1);
            if (p2_present) show(2);
        }
        return 0;
    }

    /* Standalone subcommands that don't take a pad arg. */
    if (argc == 2 && (strcmp(argv[1], "help") == 0
                      || strcmp(argv[1], "--help") == 0)) {
        usage();
        return 0;
    }

    if (argc == 2 && strcmp(argv[1], "ports") == 0) {
        show_status();
        return 0;
    }

    if (argc == 2 && strcmp(argv[1], "debug") == 0) {
        show_debug();
        return 0;
    }

    if (argc == 2 && strcmp(argv[1], "watch-ports") == 0) {
        /* Continuous user-friendly status -- press Return / Ctrl-C
         * to stop.  Use `risergamepad watch-debug` for the detailed
         * USB-host state if something's not enumerating cleanly. */
        BPTR cin = Input();
        printf("Watching ports.  Press Return to stop.\n\n");
        while (1) {
            printf("\033[H\033[2J");   /* clear screen */
            show_status();
            fflush(stdout);
            if (WaitForChar(cin, 250000)) {   /* 250 ms */
                char c = 0;
                if (Read(cin, &c, 1) > 0) {
                    if (c == '\n' || c == '\r' || c == 0x03) break;
                }
            }
            if (CheckSignal(SIGBREAKF_CTRL_C)) break;
        }
        return 0;
    }

    if (argc == 2 && strcmp(argv[1], "watch-debug") == 0) {
        /* Continuous detailed debug view. */
        BPTR cin = Input();
        printf("Watching debug.  Press Return to stop.\n\n");
        while (1) {
            printf("\033[H\033[2J");
            show_debug();
            fflush(stdout);
            if (WaitForChar(cin, 250000)) {
                char c = 0;
                if (Read(cin, &c, 1) > 0) {
                    if (c == '\n' || c == '\r' || c == 0x03) break;
                }
            }
            if (CheckSignal(SIGBREAKF_CTRL_C)) break;
        }
        return 0;
    }

    if (argc == 2 && strcmp(argv[1], "raw") == 0) {
        return cmd_raw();
    }

    if (argc >= 2 && strcmp(argv[1], "gui") == 0) {
        extern int cmd_gui(int pad, int demo);
        int demo = (argc >= 3 && strcmp(argv[2], "demo") == 0);
        return cmd_gui(1, demo);
    }

    if (argc == 2 && strcmp(argv[1], "learn-raw") == 0) {
        return cmd_learn_raw();
    }

    /* Pad-scoped subcommands.  Pad number is optional and defaults to
     * 1 (the joystick port, where a single USB gamepad always lands).
     * Explicit "1" or "2" at argv[1] overrides; otherwise argv[1] is
     * the subcommand or button name. */
    int pad = 1;
    int argi = 1;
    if (strcmp(argv[argi], "1") == 0 || strcmp(argv[argi], "2") == 0) {
        pad = atoi(argv[argi]);
        argi++;
    }
    int remaining = argc - argi;

    if (remaining == 0) {
        /* "risergamepad 1" or "risergamepad 2" -- show that pad. */
        show(pad);
        return 0;
    }

    if (remaining == 1 && strcmp(argv[argi], "watch") == 0) {
        return cmd_watch(pad);
    }
    if (remaining == 1 && strcmp(argv[argi], "learn") == 0) {
        return cmd_learn(pad);
    }

    /* No other subcommand recognised. */
    usage();
    return 1;
}
