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
/* Presets.                                                            */
/* ------------------------------------------------------------------ */

typedef struct {
    const char *name;
    const char *desc;
    unsigned char src[SLOTS];
} preset_t;

static const preset_t presets[] = {
    {"default",  "SNES-style USB pad as CD32 (firmware factory default)",
        {  6, 5, 4, 13, 8, 9, 7, 4, 6, 5 } },
    {"joystick", "fires only, no CD32 buttons (clean Atari-joystick mode)",
        {  6, 5, 4, UNMAPPED, UNMAPPED, UNMAPPED,
           UNMAPPED, UNMAPPED, UNMAPPED, UNMAPPED } },
    {"swap",     "factory default with Fire1 and Fire2 swapped",
        {  5, 6, 4, 13, 8, 9, 7, 4, 6, 5 } },
    {"xbox",     "Xbox-style USB pad (A/B/X/Y); button colors -> CD32 colors",
        {  4, 5, 6, 11, 8, 9, 4, 7, 5, 6 } },
    {"ps",       "PlayStation-style USB pad (Cross/Circle/Square/Triangle)",
        {  4, 5, 7, 13, 8, 9, 7, 6, 5, 4 } },
};
#define PRESET_COUNT (sizeof(presets)/sizeof(presets[0]))

static const preset_t *find_preset(const char *s)
{
    int i;
    for (i = 0; i < (int)PRESET_COUNT; i++)
        if (strcmp(s, presets[i].name) == 0) return &presets[i];
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Slot/register helpers.                                              */
/* ------------------------------------------------------------------ */

static int find_slot(const char *s)
{
    int i;
    for (i = 0; i < SLOTS; i++)
        if (strcmp(s, slot_name[i]) == 0) return i;
    return -1;
}

static unsigned char reg_addr(int pad, int slot)
{
    return (unsigned char)((pad == 1 ? PAD1_REG_BASE : PAD2_REG_BASE) + slot);
}

/* Parse a source token: decimal 0..15, "bN" (N=0..11) for USB-button N,
 * "u"/"unmap" for unmapped. Returns 0..15 or 0xFF, or -1 on bad input. */
static int parse_source(const char *s)
{
    if (!s || !*s) return -1;
    if (strcmp(s, "u") == 0 || strcmp(s, "unmap") == 0) return UNMAPPED;
    if ((s[0] == 'b' || s[0] == 'B') && s[1]) {
        int n = atoi(s + 1);
        if (n < 0 || n > 11) return -1;
        return n + 4;   /* USB button N -> combined-word bit (N+4) */
    }
    {
        int n = atoi(s);
        if (n < 0 || n > 15) return -1;
        return n;
    }
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

static void show_ports(void)
{
    unsigned char p1 = riser[PAD1_PORT_TYPE];
    unsigned char p2 = riser[PAD2_PORT_TYPE];
    unsigned char sentinel = riser[SENTINEL_REG];
    unsigned char hs = riser[0x34];   /* gState moved off $1C to avoid */
    unsigned char fs = riser[0x35];   /* shadowing the raw window $18..$1F */
    printf("USB ports:\n");
    printf("  Pad 1 port (HS): %s   [$14=%02X]\n", port_name(p1), p1);
    printf("  Pad 2 port (FS): %s   [$15=%02X]\n", port_name(p2), p2);
    printf("  bus-read sentinel: $16=%02X (expect A5)\n", sentinel);
    unsigned char hs_enum = riser[0x07];
    unsigned char fs_enum = riser[0x05];
    unsigned char hs_iface = riser[0x02];
    unsigned char fs_iface = riser[0x00];
    unsigned char hs_inum = riser[0x04];
    unsigned char fs_inum = riser[0x03];
    static const char *enames[] = {
        "IDLE","GET_FULL_DEV_DESC","SET_ADDR","GET_CFG_DESC",
        "GET_FULL_CFG_DESC","GET_MFC_STR","GET_PROD_STR","GET_SN_STR"
    };
    static const char *inames[] = {
        "INIT","READHID","READHIDRPTDESC",
        "INITSUBCLASS","INITENDPNT","SELECTIFACE"
    };
    const char *hsen = (hs_enum < 8) ? enames[hs_enum] : "?";
    const char *fsen = (fs_enum < 8) ? enames[fs_enum] : "?";
    const char *hsif = (hs_iface < 6) ? inames[hs_iface] : "?";
    const char *fsif = (fs_iface < 6) ? inames[fs_iface] : "?";
    unsigned char hs_ctl = riser[0x36];
    unsigned char fs_ctl = riser[0x37];
    static const char *ctlnames[] = {
        "INIT","IDLE","GET_REPORT_DESC","GET_HID_DESC",
        "SET_IDLE","SET_PROTOCOL","SET_REPORT"
    };
    const char *hsct = (hs_ctl < 7) ? ctlnames[hs_ctl] : "?";
    const char *fsct = (fs_ctl < 7) ? ctlnames[fs_ctl] : "?";
    printf("  HS host: gState=%-12s  conn=%d  EnumState=%s  iface=%s/i%u\n",
           gstate_name(hs), (hs & 0x80) ? 1 : 0, hsen, hsif, (unsigned)hs_inum);
    printf("    HID class request ctl_state for interface 0: %s\n", hsct);
    printf("  FS host: gState=%-12s  conn=%d  EnumState=%s  iface=%s/i%u\n",
           gstate_name(fs), (fs & 0x80) ? 1 : 0, fsen, fsif, (unsigned)fs_inum);
    printf("    HID class request ctl_state for interface 0: %s\n", fsct);

    unsigned char mx = riser[0x38];
    unsigned char my = riser[0x39];
    unsigned char mb = riser[0x3A];
    printf("  mouse: dx=%d dy=%d  btn=%c%c%c  alive=%c\n",
           (signed char)mx, (signed char)my,
           (mb & 1) ? 'L' : '-',
           (mb & 2) ? 'R' : '-',
           (mb & 4) ? 'M' : '-',
           (mb & 0x80) ? 'y' : 'n');
}

static void show(int pad)
{
    int i;
    printf("Pad %d:\n", pad);
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

static void list_presets(void)
{
    int i, j;
    printf("Available presets:\n");
    for (i = 0; i < (int)PRESET_COUNT; i++) {
        printf("  %-9s %s\n", presets[i].name, presets[i].desc);
        printf("            ");
        for (j = 0; j < SLOTS; j++) {
            unsigned char v = presets[i].src[j];
            if (v == UNMAPPED) printf(" %s=-", slot_name[j]);
            else               printf(" %s=%u", slot_name[j], v);
        }
        printf("\n");
    }
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
    printf("Watching pad %d.  Press buttons on your USB controller.\n", pad);
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
                printf("bits set:");
                int any = 0;
                int byte;
                for (byte = 0; byte < RAW_REPORT_TOTAL; byte++) {
                    int bit;
                    for (bit = 0; bit < 8; bit++) {
                        if (curr[byte] & (1u << bit)) {
                            int idx = byte * 8 + bit;
                            printf("%s byte%d.bit%d(=raw%d)",
                                   any ? "," : "", byte, bit, idx);
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

/* Number of bits different between two raw report snapshots. */
static int raw_popcount_diff(const unsigned char *a, const unsigned char *b)
{
    static const unsigned char nbits[16] = {
        0,1,1,2,1,2,2,3,1,2,2,3,2,3,3,4
    };
    int sum = 0, i;
    for (i = 0; i < RAW_REPORT_TOTAL; i++) {
        unsigned char d = a[i] ^ b[i];
        sum += nbits[d & 0xF] + nbits[(d >> 4) & 0xF];
    }
    return sum;
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
    unsigned char best[RAW_REPORT_TOTAL];
    unsigned char curr[RAW_REPORT_TOTAL];
    unsigned char captured[N_RAW_SLOTS][RAW_REPORT_TOTAL];
    int taken[N_RAW_SLOTS];
    int i, k;
    char line[8];
    BPTR cin = Input();

    printf("Learn-raw: discovers which raw HID bytes/bits each button uses.\n");
    printf("Release everything and press Enter to capture idle baseline\n");
    printf("('q' Enter to quit, 's' Enter to skip a slot):\n");
    fflush(stdout);

    /* baseline: cooked-mode Read blocks until Enter */
    if (Read(cin, line, sizeof(line)) <= 0) return 0;
    if (line[0] == 'q' || line[0] == 'Q') return 0;
    read_all_raw(baseline);
    printf("Baseline captured.\n\n");
    fflush(stdout);

    for (i = 0; i < N_RAW_SLOTS; i++) taken[i] = 0;

    for (i = 0; i < N_RAW_SLOTS; i++) {
        printf("Hold [%s], then press Enter: ", raw_slot_names[i]);
        fflush(stdout);

        /* Sample raw bytes as fast as we can while user types.  Switch
         * to raw mode just long enough to do the polling, then back to
         * cooked.  Pick the snapshot that differs most from baseline. */
        SetMode(cin, 1);
        drain_input(cin);
        for (k = 0; k < RAW_REPORT_TOTAL; k++) best[k] = baseline[k];
        int best_diff = 0;
        int got = 0;
        char c = 0;
        for (;;) {
            read_all_raw(curr);
            int diff = raw_popcount_diff(curr, baseline);
            if (diff > best_diff) {
                best_diff = diff;
                for (k = 0; k < RAW_REPORT_TOTAL; k++) best[k] = curr[k];
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

        for (k = 0; k < RAW_REPORT_TOTAL; k++) captured[i][k] = best[k];
        taken[i] = 1;

        int any = 0;
        for (k = 0; k < RAW_REPORT_TOTAL; k++) {
            unsigned char d = best[k] ^ baseline[k];
            if (d) {
                printf("%sbyte%d^%02X", any ? ", " : "", k, d);
                any = 1;
            }
        }
        if (!any) printf("(no change detected)");
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

    printf("Learn mode for pad %d.\n", pad);
    printf("Plug in your USB pad, then for each entry below:\n");
    printf("  - HOLD the button you want for that slot, OR\n");
    printf("  - press [Enter] to keep current setting, OR\n");
    printf("  - press [u] to mark unmapped, OR\n");
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
      "Usage:\n"
      "  risergamepad                              show current mapping\n"
      "  risergamepad ports                        show what's on each USB port\n"
      "  risergamepad presets                      list presets\n"
      "  risergamepad watch <1|2>                  show live USB button state\n"
      "  risergamepad raw                          show raw HID report bytes\n"
      "  risergamepad learn-raw                    discover which raw bytes a pad uses\n"
      "  risergamepad learn <1|2>                  interactive button learning\n"
      "  risergamepad <1|2> <button> <source>      set one slot\n"
      "  risergamepad <1|2> reset                  factory defaults\n"
      "  risergamepad <1|2> preset <name>          apply a preset\n"
      "\n"
      " button = fire1 fire2 fire3 play rw ff green yellow red blue\n"
      " source = bN (USB button 0..11), 0..15 (raw bit), or 'u'/'unmap'\n"
      "\n"
      "Tip: run 'risergamepad watch 1' and press each button on your pad to\n"
      "see which 'bN' label it has, or use 'risergamepad learn 1' to assign\n"
      "every slot interactively.\n");
}

int main(int argc, char *argv[])
{
    if (argc == 1) {
        show_ports();
        printf("\n");
        show(1);
        show(2);
        return 0;
    }

    if (argc == 2 && strcmp(argv[1], "presets") == 0) {
        list_presets();
        return 0;
    }

    if (argc == 2 && strcmp(argv[1], "ports") == 0) {
        show_ports();
        return 0;
    }

    if (argc == 2 && strcmp(argv[1], "raw") == 0) {
        return cmd_raw();
    }

    if (argc == 2 && strcmp(argv[1], "learn-raw") == 0) {
        return cmd_learn_raw();
    }

    if (argc == 3 &&
        (strcmp(argv[1], "watch") == 0 || strcmp(argv[1], "learn") == 0)) {
        int pad = atoi(argv[2]);
        if (pad != 1 && pad != 2) { printf("pad must be 1 or 2\n"); return 1; }
        return strcmp(argv[1], "watch") == 0 ? cmd_watch(pad) : cmd_learn(pad);
    }

    if (argc < 3) { usage(); return 1; }

    int pad = atoi(argv[1]);
    if (pad != 1 && pad != 2) {
        printf("pad must be 1 or 2\n");
        return 1;
    }

    if (argc == 3 && strcmp(argv[2], "reset") == 0) {
        apply(pad, presets[0].src);   /* index 0 == "default" */
        printf("Pad %d reset to defaults.\n", pad);
        show(pad);
        return 0;
    }

    if (argc == 4 && strcmp(argv[2], "preset") == 0) {
        const preset_t *p = find_preset(argv[3]);
        if (!p) {
            printf("unknown preset '%s'.  Try: risergamepad presets\n", argv[3]);
            return 1;
        }
        apply(pad, p->src);
        printf("Pad %d set to preset '%s'.\n", pad, p->name);
        show(pad);
        return 0;
    }

    if (argc != 4) { usage(); return 1; }

    int slot = find_slot(argv[2]);
    if (slot < 0) { printf("unknown button '%s'\n", argv[2]); usage(); return 1; }

    int v = parse_source(argv[3]);
    if (v < 0) {
        printf("source must be bN (0..11), 0..15, or 'unmap'\n");
        return 1;
    }

    unsigned char r = reg_addr(pad, slot);
    riser[r] = (unsigned char)v;

    unsigned char rb = riser[r];
    printf("Pad %d %s [reg $%02X] = %s (read-back %s)\n",
           pad, argv[2], r, describe_bit(v), describe_bit(rb));
    return 0;
}
