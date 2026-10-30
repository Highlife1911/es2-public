#ifndef ES2_SHELL_GUI_REPAIR_H
#define ES2_SHELL_GUI_REPAIR_H

/* VSHELL's REPAIR screen 0x4326c7 and the Herc scrap warning 0x4479b8
 * on the widget toolkit, over the editor library
 * (es2/shell/editors.h). Contract: vshell_ui.md sections 4.7, 12 and
 * 30; layout: the layout table's "repair" and "herc_scrap_box"
 * (shell/data/vshell_layout.json).
 *
 * The screen builds the table's 168 controls (eight per-bay grids, the panel
 * with the External list of sixteen rows and the Internal list of nine, the
 * mode text, the salvage, cost, condition and REPAIR ALL cost labels and the
 * four buttons) and the box's eleven. Every state change is an operation of
 * es2_shell_editors on the SAV the host keeps (*save, replaced after each
 * applied operation); the screen draws what the original's refresh routines
 * draw from the state: the row texts 0x434042, the labels and enable rules
 * 0x433ad5, the row colours 0x4344cc, the grid swap 0x433fcd, the vehicle-8
 * labels 0x43436f. What belongs to the squad panel (its grids
 * [0x48d59c + 4 bay], its rows, its labels, the Herc diagram) and to BUILD
 * is reported through host.squad with the VSHELL routine's address.
 * Portable C17, no platform call, no file I/O. */
#include <stdint.h>

#include "es2/shell/editors.h"
#include "es2/shell/flow.h"
#include "es2/shell/gui/layout.h"

/* Controls of the layout table's repair (index NN of "repair.NN"). */
enum {
    ES2_REPAIR_GRID0 = 0,           /* [0x48d1f8 + 4 bay] ESGrid of bay 0..7 (0..7) */
    ES2_REPAIR_PANEL = 8,           /* [0x48d218] ESTitle, ESTEXT 0x3d, handler 0x434693 (empty) */
    ES2_REPAIR_EXTERNAL = 10,       /* [0x48d21c] ESTitle, ESTEXT 0x3e, handler 0x4346ec (empty) */
    ES2_REPAIR_EXTERNAL_ROW0 = 12,  /* [0x48d220 + 4 r] ESQuad of External row r at 12 + 5 r (r 0..15) */
    ES2_REPAIR_INTERNAL = 92,       /* [0x48d260] ESTitle, ESTEXT 0x3f, handler 0x434d95 */
    ES2_REPAIR_INTERNAL_ROW0 = 94,  /* [0x48d264 + 4 r] ESQuad of Internal row r at 94 + 5 r (r 0..8) */
    ES2_REPAIR_MODE = 140,          /* [0x48d28c] ESButtonFont: "Auto/Manual Repair" (0x433995) */
    ES2_REPAIR_SALVAGE = 143,       /* [0x48d294] salvage - bank cost */
    ES2_REPAIR_COST = 148,          /* [0x48d2a4] cost of the selected item */
    ES2_REPAIR_CONDITION = 151,     /* [0x48d2ac] condition word of the selected item (ESTEXT 0x68 + band) */
    ES2_REPAIR_REPAIR = 153,        /* [0x48d2b0] REPAIR 0x4351bd */
    ES2_REPAIR_ALL_COST = 158,      /* [0x48d2c0] cost of REPAIR ALL */
    ES2_REPAIR_REPAIR_ALL = 160,    /* [0x48d2c4] REPAIR ALL 0x4352e9 */
    ES2_REPAIR_SCRAP = 164,         /* [0x48d2d0] SCRAP 0x4353a5 */
    ES2_REPAIR_CANCEL = 166,        /* [0x48d2d4] CANCEL 0x435403 */
    ES2_REPAIR_CONTROLS = 168,
    ES2_REPAIR_EXTERNAL_ROWS = 16,
    ES2_REPAIR_INTERNAL_ROWS = 9
};
/* Controls of the layout table's herc_scrap_box. */
enum {
    ES2_HERC_SCRAP_SHIELD = 0,      /* [0x48da10] Window on the display */
    ES2_HERC_SCRAP_BOX = 1,         /* [0x48da14] ESAlert, ESTEXT 0xc9 */
    ES2_HERC_SCRAP_TEXT = 6,        /* [0x48da24] "<value / 1000> <ESTEXT 0xcd>" (0x447da1) */
    ES2_HERC_SCRAP_CANCEL = 7,      /* [0x48da28] CANCEL 0x4482c8 */
    ES2_HERC_SCRAP_ACCEPT = 9,      /* [0x48da2c] ACCEPT 0x448326 */
    ES2_HERC_SCRAP_CONTROLS = 11
};

/* VSHELL handler addresses of the screen and the box. */
enum {
    ES2_REPAIR_PANEL_HANDLER = 0x434693u,
    ES2_REPAIR_EXTERNAL_HANDLER = 0x4346ecu,
    ES2_REPAIR_INTERNAL_HANDLER = 0x434d95u,
    ES2_REPAIR_EXTERNAL_ROW_HANDLER0 = 0x434745u, /* row r: + 0x65 r (r 0..15) */
    ES2_REPAIR_INTERNAL_ROW_HANDLER0 = 0x434e30u, /* row r: + 0x65 r (r 0..8) */
    ES2_REPAIR_REPAIR_HANDLER = 0x4351bdu,
    ES2_REPAIR_ALL_HANDLER = 0x4352e9u,
    ES2_REPAIR_SCRAP_HANDLER = 0x4353a5u,
    ES2_REPAIR_CANCEL_HANDLER = 0x435403u,
    ES2_HERC_SCRAP_CANCEL_HANDLER = 0x4482c8u,
    ES2_HERC_SCRAP_ACCEPT_HANDLER = 0x448326u
};

/* Routines of other screens the REPAIR code calls (host.squad's `routine`). */
enum {
    ES2_REPAIR_SQUAD_GRID_HIDE = 0x41fabdu,  /* hide the squad panel's grid [0x48d59c + 4 a] */
    ES2_REPAIR_SQUAD_GRID_SHOW = 0x41f93au,  /* show it */
    ES2_REPAIR_SQUAD_SELECT = 0x43dcddu,     /* the squad panel's selection 0x43dcdd(b) on the present screen
                                              * word (the previous one during a show), called while the
                                              * session's bay is still a. A host with the squad panel (gui/service_bay.h:
                                              * es2_squad_panel_select(b, 0)) stores the bay and runs its
                                              * word's case, which on word 3 calls es2_repair_screen_selected;
                                              * when the bay is unchanged after the call the screen stores it
                                              * and draws REPAIR's case itself. */
    ES2_REPAIR_SQUAD_LABELS = 0x43da0cu,     /* end of the refresh 0x433ad5: the squad labels of bay a */
    ES2_REPAIR_SQUAD_CONDITION = 0x43e0d7u,  /* then the condition label of bay a */
    ES2_REPAIR_SQUAD_DIAGRAM = 0x414cbeu,    /* then the Herc diagram of bay a with the selected row */
    ES2_REPAIR_SQUAD_REFRESH = 0x43d80cu,    /* after a Herc scrap: the squad panel */
    ES2_REPAIR_SQUAD_REDRAW = 0x41f3edu,     /* after a Herc scrap: redraw [0x48d5d4] */
    ES2_REPAIR_BUILD_REFRESH = 0x447064u     /* after a Herc scrap on BUILD (screen word 4) */
};

typedef struct es2_repair_screen_host {
    void *ctx;
    /* The font of a VSHELL global (0x46de00: every control of the screen and the box). */
    const es2_dfn *(*font)(void *ctx, uint32_t global);
    /* An ESTEXT entry (0x3d.., 0x4e..0x63, 0x7d.., 0xc8, 0xc9.., 0xcd, 1, 0x41, 0x42, 0x68..0x6d). NULL: empty. */
    const char *(*text)(void *ctx, uint32_t id);
    /* A literal of VSHELL.EXE by address (the empty initial labels 0x474075..0x474084, 0x47408d). */
    const char *(*literal)(void *ctx, uint32_t address);
    /* The squad panel's and BUILD's parts (ES2_REPAIR_SQUAD_*, ES2_REPAIR_BUILD_REFRESH). May be NULL. */
    void (*squad)(void *ctx, uint32_t routine, int16_t a, int16_t b);
    /* After a handler changed the state (handler = the VSHELL address). May be NULL. */
    void (*changed)(void *ctx, uint32_t handler, es2_shell_flow_result result);
} es2_repair_screen_host;

typedef struct es2_repair_screen {
    es2_ui *ui;                          /* set by the build */
    es2_shell_flow *flow;                /* the prefs blocks (repair option +0x2c, building +0x2d) */
    es2_shell_editor_session *session;   /* the editor globals (screen word, bay, row, snapshot) */
    const es2_vfs *vfs;                  /* GAM/HERC_INF.DAT, GAM/DAMAGE.DAT, GAM/WEAPONS.DAT */
    es2_span tables;                     /* ES2_SHELL_RESULTS_TABLES_SIZE bytes from the checked EXE */
    es2_span editor_tables;              /* ES2_SHELL_EDITOR_TABLES_SIZE bytes from the checked EXE */
    es2_shell_buffer *save;              /* the memory state (a SAV image, malloc'd; replaced after changes) */
    es2_repair_screen_host host;
    es2_ui_id ids[ES2_REPAIR_CONTROLS];
    es2_ui_id box[ES2_HERC_SCRAP_CONTROLS];
    es2_shell_editor_view view;          /* the library's view after the last operation */
    es2_status status;                   /* the last library answer (ES2_OK, or the refusal) */
    es2_error error;
    /* the last handler (for presenters and tests) */
    uint32_t last_handler;
    es2_shell_flow_result last_result;
    unsigned events;
} es2_repair_screen;

/* The screen constructor 0x4326c7 and the box's 0x4479b8 (registration
 * table, screens.c); ctx = the es2_repair_screen. 0 = built, -1 = bad
 * argument or the pool is full. */
int es2_screen_repair_build(es2_ui *ui, void *ctx);
int es2_screen_herc_scrap_box_build(es2_ui *ui, void *ctx);

/* The REPAIR bar button after the flow's ES2_FLOW_BAY_REPAIR (which left the
 * previous screen with its rebalance): the show 0x43397c. previous_word =
 * the flow's bay word before the button (the library's OPEN runs with it; a
 * WEAPONS or BUILD word, whose rebalance the flow already ran, is given to
 * the library as the bay screen -1, which decides the bay selection the same
 * way). DONE, or FAILED when the library refused (rs->status). */
es2_shell_flow_result es2_repair_screen_show(es2_repair_screen *rs, int previous_word);
/* 0x433a7b: hide the panel and the two lists (leaving; the rebalance is the flow's). */
void es2_repair_screen_hide(es2_repair_screen *rs);
/* The squad panel's bay selector on screen word 3 (0x43dcdd, 0x43dd1c..0x43ddf2).
 * 1 = the bay changed, 0 = nothing (the bay itself, no finished Herc), -1 = refused. */
int es2_repair_screen_bay(es2_repair_screen *rs, int16_t bay);
/* REPAIR's branch of 0x43dcdd after a squad panel stored the new bay in the
 * session (the service bay's host.selected(3, bay, assign)): the row (0, 0)
 * 0x434549, the names 0x43436f, the rows 0x43433f and the snapshot 0x433f86. 1
 * = done, -1 = refused. */
int es2_repair_screen_selected(es2_repair_screen *rs);
/* REPAIR's grid of a bay [0x48d1f8 + 4 bay] (the service bay's
 * host.repair_grid); 0 outside 0..7. */
es2_ui_id es2_repair_screen_grid(es2_repair_screen *rs, int16_t bay);
/* 0x433f43: the salvage label without the unit (the squad panel's 0x43d80c on word 3). */
void es2_repair_screen_salvage(es2_repair_screen *rs);
/* The warning 0x447da1 (SCRAP of REPAIR and BUILD) and its hide 0x447e25. */
void es2_herc_scrap_box_show(es2_repair_screen *rs);
void es2_herc_scrap_box_hide(es2_repair_screen *rs);
/* One handler by its VSHELL address as a release runs it (the button class
 * fires only while enabled: a disabled REPAIR, REPAIR ALL or SCRAP is
 * ES2_FLOW_IGNORED). DONE when it ran, IGNORED when it changed nothing,
 * REFUSED when the library refused (no bay selected: vshell_ui.md 12.3). */
es2_shell_flow_result es2_repair_screen_press(es2_repair_screen *rs, uint32_t handler);

/* For tests and presenters: the row's control index (External tab 0, Internal tab 1), -1 out of range. */
int es2_repair_row_control(int tab, int row);

#endif
