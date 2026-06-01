/* amiga_min.h -- minimal Amiga type/struct stubs to use Intuition and
 * graphics.library via vbcc's inline/*_protos.h without needing the
 * full NDK installed.
 *
 * The struct layouts here MUST match Amiga's official intuition.h /
 * exec/ports.h binary layout because Intuition writes into our
 * NewWindow / reads our Message ports etc.  Only the fields we
 * actually touch are filled in -- the rest are anonymous padding
 * sized to keep subsequent fields at the right offsets.
 */

#ifndef AMIGA_MIN_H
#define AMIGA_MIN_H

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

/* exec/memory.h MEMF_* flags for AllocMem / AllocVec. */
#define MEMF_ANY            0x00000000L
#define MEMF_PUBLIC         0x00000001L
#define MEMF_CHIP           0x00000002L
#define MEMF_FAST           0x00000004L
#define MEMF_CLEAR          0x00010000L

/* Forward declarations -- callers don't access fields. */
struct Library;
struct DosLibrary;
struct ExecBase;
struct GfxBase;
struct IntuitionBase;
struct Screen;
struct BitMap;
struct Gadget;
struct Image;
struct Menu;
struct DrawInfo;
struct Resident;
struct InputEvent;
struct Requester;

/* exec/lists.h Node + List.  Only used so MsgPort / Message can have
 * exact binary layout below. */
struct Node {
    struct Node *ln_Succ;
    struct Node *ln_Pred;
    UBYTE        ln_Type;
    BYTE         ln_Pri;
    char        *ln_Name;
};

struct List {
    struct Node *lh_Head;
    struct Node *lh_Tail;
    struct Node *lh_TailPred;
    UBYTE        lh_Type;
    UBYTE        l_pad;
};

/* exec/tasks.h -- a Task pointer is opaque to us. */
struct Task;

/* exec/ports.h */
struct MsgPort {
    struct Node    mp_Node;
    UBYTE          mp_Flags;
    UBYTE          mp_SigBit;
    struct Task   *mp_SigTask;
    struct List    mp_MsgList;
};

struct Message {
    struct Node    mn_Node;
    struct MsgPort *mn_ReplyPort;
    UWORD          mn_Length;
};

/* graphics/rastport.h -- we only pass RastPort pointers around. */
struct RastPort;

/* intuition/intuition.h NewWindow.  Layout EXACTLY as documented. */
struct NewWindow {
    WORD            LeftEdge, TopEdge;
    WORD            Width, Height;
    UBYTE           DetailPen, BlockPen;
    ULONG           IDCMPFlags;
    ULONG           Flags;
    struct Gadget  *FirstGadget;
    struct Image   *CheckMark;
    UBYTE          *Title;
    struct Screen  *Screen;
    struct BitMap  *BitMap;
    WORD            MinWidth, MinHeight;
    UWORD           MaxWidth, MaxHeight;
    UWORD           Type;
};

/* intuition/intuition.h Window.  Layout matches the official struct so
 * field offsets line up with what Intuition writes.  We only touch
 * UserPort, RPort, Width, Height. */
struct Window {
    struct Window  *NextWindow;          /* offset  0 */
    WORD            LeftEdge, TopEdge;   /*  4 */
    WORD            Width, Height;       /*  8  -- used */
    WORD            MouseY, MouseX;      /* 12 */
    WORD            MinWidth, MinHeight; /* 16 */
    UWORD           MaxWidth, MaxHeight; /* 20 */
    ULONG           Flags;               /* 24 */
    struct Menu    *MenuStrip;           /* 28 */
    UBYTE          *Title;               /* 32 */
    struct Requester *FirstRequest;      /* 36 */
    struct Requester *DMRequest;         /* 40 */
    WORD            ReqCount;            /* 44 */
    struct Screen  *WScreen;             /* 46 */
    struct RastPort *RPort;              /* 50  -- used */
    BYTE            BorderLeft, BorderTop, BorderRight, BorderBottom; /* 54 */
    struct RastPort *BorderRPort;        /* 58 */
    struct Gadget  *FirstGadget;         /* 62 */
    struct Window  *Parent, *Descendant; /* 66 */
    UWORD          *Pointer;             /* 74  -- sprite data, single ptr */
    BYTE            PtrHeight;           /* 78 */
    BYTE            PtrWidth;            /* 79 */
    BYTE            XOffset, YOffset;    /* 80 */
    ULONG           IDCMPFlags;          /* 82 */
    struct MsgPort *UserPort;            /* 86  -- used */
    struct MsgPort *WindowPort;          /* 90 */
    struct IntuiMessage *MessageKey;     /* 94 */
    /* ... fields beyond here we don't use ... */
};

/* intuition/intuition.h IntuiMessage -- starts with an exec Message. */
struct IntuiMessage {
    struct Message  ExecMessage;
    ULONG           Class;
    UWORD           Code;
    UWORD           Qualifier;
    APTR            IAddress;
    WORD            MouseX, MouseY;
    ULONG           Seconds;
    ULONG           Micros;
    struct Window  *IDCMPWindow;
    struct IntuiMessage *SpecialLink;
};

/* IDCMP flags we use */
#define IDCMP_CLOSEWINDOW   0x00000200L
#define IDCMP_VANILLAKEY    0x00200000L
#define IDCMP_RAWKEY        0x00000400L
#define IDCMP_INTUITICKS    0x00000800L

/* WFLG_* window flags */
#define WFLG_SIZEGADGET     0x00000001L
#define WFLG_DRAGBAR        0x00000002L
#define WFLG_DEPTHGADGET    0x00000004L
#define WFLG_CLOSEGADGET    0x00000008L
#define WFLG_SIZEBRIGHT     0x00000010L
#define WFLG_SIZEBBOTTOM    0x00000020L
#define WFLG_SMART_REFRESH  0x00000000L
#define WFLG_BACKDROP       0x00000100L
#define WFLG_REPORTMOUSE    0x00000200L
#define WFLG_GIMMEZEROZERO  0x00000400L
#define WFLG_BORDERLESS     0x00000800L
#define WFLG_ACTIVATE       0x00001000L
#define WFLG_RMBTRAP        0x00010000L
#define WFLG_NOCAREREFRESH  0x00020000L

/* Window Types */
#define WBENCHSCREEN        0x0001
#define CUSTOMSCREEN        0x000F

/* intuition/screens.h NewScreen */
struct NewScreen {
    WORD            LeftEdge, TopEdge;
    WORD            Width, Height;
    WORD            Depth;
    UBYTE           DetailPen, BlockPen;
    UWORD           ViewModes;
    UWORD           Type;
    struct TextAttr *Font;
    UBYTE          *DefaultTitle;
    struct Gadget  *Gadgets;
    struct BitMap  *CustomBitMap;
};

/* ViewPort is in graphics.library but we access it via offset 44 inside
 * Screen (where the embedded ViewPort lives, per official intuition.h).
 * That's the only thing we need for SetRGB4 / LoadRGB4. */
struct ViewPort;
#define SCREEN_VIEWPORT_OFFSET  44

/* graphics/view.h ViewMode bits */
#define V_HIRES             0x8000
#define V_LACE              0x0004
#define V_PAL               0x0008

/* graphics/gfx.h BitMap, layout matches the official struct so we can
 * point its Planes[] at our own static bitplane data. */
typedef UBYTE *PLANEPTR;
struct BitMap {
    UWORD     BytesPerRow;
    UWORD     Rows;
    UBYTE     Flags;
    UBYTE     Depth;
    UWORD     Pad;
    PLANEPTR  Planes[8];
};

/* graphics/rastport.h draw modes for SetDrMd() */
#define JAM1                0
#define JAM2                1
#define COMPLEMENT          2
#define INVERSVID           4

/* Library bases.  amiga.lib startup auto-opens dos.library; we open
 * intuition and graphics ourselves at startup. */
extern struct ExecBase     *SysBase;
extern struct DosLibrary   *DOSBase;
extern struct IntuitionBase *IntuitionBase;
extern struct GfxBase      *GfxBase;

/* Skip clib (we don't have it); pull in vbcc's inline-asm prototypes. */
#define CLIB_EXEC_PROTOS_H
#define CLIB_DOS_PROTOS_H
#define CLIB_INTUITION_PROTOS_H
#define CLIB_GRAPHICS_PROTOS_H
#include <inline/exec_protos.h>
#include <inline/dos_protos.h>
#include <inline/intuition_protos.h>
#include <inline/graphics_protos.h>

#endif /* AMIGA_MIN_H */
