#ifndef ES2_SHELL_GUI_WEAPONS_H
#define ES2_SHELL_GUI_WEAPONS_H

/* VSHELL's WEAPONS screen 0x43ebbc (warmingi) on the widget toolkit
 * over the service-bay operations through the state engine
 * (es2/shell/engine.h: the memory SAV, the 27 row IDs of 0x476a4c, the ARM
 * answers of 0x41501f). Contract: vshell_ui.md sections 4.7, 10.2, 17.1 and
 * 29; layout: the layout table's "weapons" (shell/data/vshell_layout.json)
 * with the per-record images of gam\arm_weap.dat (vshell_widgets.md 4.4).
 *
 * The screen body only: the service-bay bar, the squad panel (its bay
 * selectors, the hardpoint hot spots that select a slot and the slot marker
 * 0x415bff) are the service bay's, the leave with its rebalance 0x412a37 is the
 * flow's (bay bar events, es2_shell_flow_host.rebalance). The bay selection
 * 0x482bc9 is shared with the squad panel and the editors: the screen reads
 * and keeps it through `bay` and changes it only through the host's
 * select_bay (0x43dcdd), so a revisit keeps VSHELL's selection
 * (vshell_state.md: 0 after a new game or a load, the hangar clear's reset, not
 * this screen's). Portable C17, no platform call, no file I/O. */
#include <stdint.h>

#include "es2/shell/engine.h"
#include "es2/shell/gui/layout.h"

/* Controls of the layout table's weapons (index NN of "weapons.NN"). */
enum {
    ES2_WEAPONS_PANEL = 0,          /* [0x48d674] ESTitle, ESTEXT 0x9f */
    ES2_WEAPONS_LIST_PANEL = 2,     /* [0x48d678] ESTitle of the 27 rows, ESTEXT 0xa0 */
    ES2_WEAPONS_ARM = 4,            /* [0x48d67c] ESArm: images, guidance panel, three text lines */
    ES2_WEAPONS_NO_IMAGE = 7,       /* [0x48d6f8] ESRect (frame off) shown for the ID-0 row */
    ES2_WEAPONS_GUIDE_ARM = 8,      /* [0x48d6fc] ESTEXT 0xa1, 0x440626: mode 2 */
    ES2_WEAPONS_GUIDE_ARH = 10,     /* [0x48d700] ESTEXT 0xa2, 0x44068b: mode 1 */
    ES2_WEAPONS_GUIDE_SARH = 12,    /* [0x48d704] ESTEXT 0xa3, 0x4406f0: mode 0 */
    ES2_WEAPONS_GUIDE_EO = 14,      /* [0x48d708] ESTEXT 0xa4, 0x440755: mode 3 */
    ES2_WEAPONS_DESCRIPTION = 16,   /* [0x48d70c] 0x4407ba; label ESTEXT 0x7e + ID */
    ES2_WEAPONS_TEXT0 = 18,         /* [0x48d710], [0x48d714], [0x48d718]: wpn_desc lines */
    ES2_WEAPONS_ROW0 = 21,          /* [0x48d71c + 4r] ESQuad of row r: 21 + 5r (columns 22..25 + 5r) */
    ES2_WEAPONS_SLOT_LABEL = 156,   /* [0x48d7fc] ESTEXT 0xa5 */
    ES2_WEAPONS_SLOT_PREV = 157,    /* [0x48d800] 0x4408d4 -> 0x43e3d9 */
    ES2_WEAPONS_SLOT_NEXT = 159,    /* [0x48d804] 0x440932 -> 0x43e399 */
    ES2_WEAPONS_CONTROLS = 161
};
enum { ES2_WEAPONS_ROWS = ES2_SHELL_WEAPON_ROWS };

/* VSHELL addresses of the screen's handlers. */
enum {
    ES2_WEAPONS_ROW_HANDLER0 = 0x440990u,   /* rows 0..26: 0x440990 + 0x6f r */
    ES2_WEAPONS_ARM_HANDLER = 0x440626u,
    ES2_WEAPONS_ARH_HANDLER = 0x44068bu,
    ES2_WEAPONS_SARH_HANDLER = 0x4406f0u,
    ES2_WEAPONS_EO_HANDLER = 0x440755u,
    ES2_WEAPONS_DESCRIPTION_HANDLER = 0x4407bau,
    ES2_WEAPONS_PREV_HANDLER = 0x4408d4u,
    ES2_WEAPONS_NEXT_HANDLER = 0x440932u
};

/* The window pointer array 0x48d680: 27 dwords of list 1 (images by row,
 * 0x48d680 + 4 row), four of list 2 (guidance images by mode, 0x48d6e8 + 4
 * mode), then [0x48d6f8] and [0x48d6fc]; index -1 is [0x48d67c] (the ESArm).
 * VSHELL indexes it past its parts (row 26, ID 0, has no list-1 record and
 * reads list 2's mode 0): ES2_WEAPONS_POINTERS keeps that layout. */
enum { ES2_WEAPONS_POINTERS = 32 };

/* The host's parts. Every member may be NULL (then: no resource, no call). */
typedef struct es2_weapons_screen_host {
    void *ctx;
    /* Fonts by VSHELL global (0x46de00 for every control of this screen). */
    const es2_dfn *(*font)(void *ctx, uint32_t global);
    /* Frame `frame` of a DBA (dba\arm_weap.dba). */
    const es2_bitmap *(*frame)(void *ctx, const char *file, int frame);
    /* A game file's bytes (gam\arm_weap.dat); an empty span: no images. */
    es2_span (*file)(void *ctx, const char *name);
    /* ESTEXT entry (0x9f..0xa5, row names 0x7e + ID, the description label). */
    const char *(*text)(void *ctx, uint32_t id);
    /* <lang>\wpn_desc.bin entry (3 ID .. 3 ID + 2, 0x63 + 3 mode ..). */
    const char *(*desc)(void *ctx, uint32_t index);
    /* A literal of VSHELL.EXE by address (0x476dd3..0x476de5, 0x4774c1). */
    const char *(*literal)(void *ctx, uint32_t address);
    /* The squad panel's selection 0x43dcdd(bay) in the case of the screen
     * word `previous` (the show runs before the bar stores 2); NULL: the
     * selection word alone is set. */
    void (*select_bay)(void *ctx, int16_t bay, int previous);
    /* The squad panel's slot marker 0x415bff(slot) and its grid's redraw
     * (after a mount attempt and a slot change). */
    void (*slot_shown)(void *ctx, int16_t slot);
    /* After a handler changed the memory state (a mount or a guidance store). */
    void (*changed)(void *ctx, uint32_t handler);
} es2_weapons_screen_host;

typedef struct es2_weapons_screen {
    es2_ui *ui;                     /* set by the build */
    es2_shell_engine *engine;       /* the memory state (NULL or nothing loaded: no rows, no mounts) */
    int16_t *bay;                   /* the bay selection 0x482bc9 (NULL: own_bay) */
    int16_t own_bay;
    es2_weapons_screen_host host;
    /* screen globals (vshell_state.md); their image values are set by the build */
    int16_t slot;                   /* 0x476658, -1 */
    int16_t row;                    /* 0x476df6 (selected list row), -1 */
    int16_t guidance;               /* 0x476df8 (guidance image shown), -1 */
    int32_t guidance_previous;      /* 0x476dfc, -1 */
    int panel;                      /* 0x476a82: the guidance panel is shown */
    es2_ui_id ids[ES2_WEAPONS_CONTROLS];
    es2_ui_id pointers[ES2_WEAPONS_POINTERS];
    /* the last handler (for presenters and tests) */
    uint32_t last_handler;
    unsigned events, mounts, stores;
    unsigned refused;               /* own refusals where VSHELL reads outside its objects */
} es2_weapons_screen;

/* The screen constructor 0x43ebbc (registration table, screens.c): the
 * layout table's controls, the images of gam\arm_weap.dat's two lists, the rows
 * enabled by the availability byte at construction, the handlers bound; the
 * screen globals at their image values. ctx = the es2_weapons_screen (engine,
 * bay and host set by the caller). 0 = built, -1 = bad argument, a malformed
 * arm_weap.dat or a full pool. */
int es2_screen_weapons_build(es2_ui *ui, void *ctx);

/* 0x43fbd8: show [0x48d674], [0x48d678], [0x48d67c]; a selection that is -1,
 * without a record or not finished (0x411088: record word +0x4a, SAV fixed
 * +70, is 100) is replaced through select_bay(0x41124e: the first finished
 * bay, or -1; `previous` = the screen word before WEAPONS); slot := -1;
 * 0x43fdac(0). The WEAPONS bar button 0x43a862 calls it between the leave
 * and the store of word 2 (the flow's ES2_FLOW_BAY_WEAPONS event). */
void es2_weapons_screen_show(es2_weapons_screen *ws, int previous);
/* 0x43fc62: hide the same three (the leave 0x43a537 for word 2). */
void es2_weapons_screen_hide(es2_weapons_screen *ws);
/* 0x43fdac(row): the row selected; with `mount` (the row handlers' flag
 * 0x476df4) the mount 0x43e2d4 -> 0x411b10 when a slot is selected and VSHELL's
 * rule allows it. 1 = ran, 0 = VSHELL returns early, -1 refused (row
 * outside 0..26). The squad panel's WEAPONS case (0x43df86) calls it with
 * row 0 after slot := -1 (es2_weapons_screen_bay_changed). */
int es2_weapons_screen_row(es2_weapons_screen *ws, int row, int mount);
/* 0x43e242(slot): the slot selection (the squad panel's hot spots and the
 * prev/next buttons). 1 = changed, 0 = already selected, -1 refused. */
int es2_weapons_screen_select_slot(es2_weapons_screen *ws, int slot);
/* The squad panel's WEAPONS case 0x43df86 before it stores a new selection:
 * slot := -1, 0x43fdac(0). */
void es2_weapons_screen_bay_changed(es2_weapons_screen *ws);
/* 0x4403f9(mode, flag): the guidance buttons (flag 1) and 0x43fdac (flag 0). */
void es2_weapons_screen_guidance(es2_weapons_screen *ws, int mode, int flag);
/* One handler by its VSHELL address. Returns 1 when it ran, 0 when it did
 * nothing, -1 for an unknown address or a refusal. */
int es2_weapons_screen_press(es2_weapons_screen *ws, uint32_t handler);

/* The parsed records of gam\arm_weap.dat (vshell_widgets.md 4.4: per list a
 * word count, then records of a word ID, dword x, dword y, word frame). Returns
 * the number of records of both lists, -1 when malformed (a list beyond the
 * bytes, bytes left over). out may be NULL. */
typedef struct es2_weapons_record {
    int16_t id, frame;
    int32_t x, y;
    int list;                       /* 1 or 2 */
} es2_weapons_record;
int es2_weapons_records(es2_span file, es2_weapons_record *out, int capacity);

#endif
