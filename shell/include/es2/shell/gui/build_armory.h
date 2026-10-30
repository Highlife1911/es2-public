#ifndef ES2_SHELL_GUI_BUILD_ARMORY_H
#define ES2_SHELL_GUI_BUILD_ARMORY_H

/* VSHELL's BUILD screen 0x445de8 (Herc construction), the ARMORY
 * screen 0x4484c4 (the five-entry weapons build bank) and the weapons scrap
 * warning 0x447e3e on the widget toolkit. Contract: vshell_ui.md
 * sections 4.7, 12.4, 17.4, 17.5 and 31; layout: the layout table's build,
 * armory and weapon_scrap_box (shell/data/vshell_layout.json).
 *
 * Every state change goes through the editors library
 * (es2/shell/editors.h) on the loaded game's SAV (es2_build_armory_game):
 * the screen keeps the library's session (the globals 0x478780, 0x479210,
 * 0x482bc9, the screen word) and replaces the SAV after every operation.
 * What the screens draw (row enables, counts, costs, the bank, salvage) is
 * read from that SAV and the game tables after each operation. Portable C17,
 * no platform call, no file I/O: game files and texts come from the host.
 *
 * The service bay (gui/service_bay.h) reaches the screens through its
 * host: screen(4|5, 1) -> es2_build_show / es2_armory_show, screen(4|5, 0)
 * -> es2_build_hide / es2_armory_hide, selected(4, k, assign) ->
 * es2_build_select_bay; after a Build, host.squad runs the panel's
 * es2_squad_panel_hercs, _pilots and _labels. The Herc scrap warning that
 * BUILD's Scrap opens is the REPAIR screen's (host.herc_scrap), whose ACCEPT
 * ends in es2_build_refresh. */
#include <stdint.h>

#include "es2/formats/vfs.h"
#include "es2/shell/editors.h"
#include "es2/shell/flow.h"
#include "es2/shell/gui/layout.h"

/* Controls of the layout table's build (index NN of "build.NN"). */
enum {
    ES2_BUILD_PANEL = 0,          /* [0x48d968] ESTitle, ESTEXT 186 */
    ES2_BUILD_GRID = 2,           /* [0x48d96c + 4t] ESGrid of Herc type t = 0..8: build.02 + t */
    ES2_BUILD_LIST = 11,          /* [0x48d990] ESRegionFill under the nine rows */
    ES2_BUILD_ROW = 12,           /* [0x48d994 + 4t] ESQuad of type t: build.12 + 5t */
    ES2_BUILD_LOWER = 58,         /* [0x48d9bc] ESRegionFill under the panel */
    ES2_BUILD_TONS = 68,          /* [0x48d9e4] HERC_INF +2 "%d %s" (ESTEXT 0xc6) */
    ES2_BUILD_SPEED = 69,         /* [0x48d9e8] HERC_INF +4 "%d %s" (ESTEXT 0xc7) */
    ES2_BUILD_WORD6 = 70,         /* [0x48d9ec] HERC_INF +6 "%d" */
    ES2_BUILD_COST = 71,          /* [0x48d9f0] HERC_INF +8 "%d %s" (ESTEXT 0xc6) */
    ES2_BUILD_AVAILABLE = 73,     /* [0x48d9f8] (salvage - bank cost) / 1000 "%ld %s" */
    ES2_BUILD_SCRAP = 76,         /* [0x48da08] ESTEXT 196, 0x447570 */
    ES2_BUILD_BUILD = 78,         /* [0x48da0c] ESTEXT 197, 0x4475ce */
    ES2_BUILD_CONTROLS = 80
};

/* Controls of the layout table's armory. */
enum {
    ES2_ARMORY_PANEL = 0,         /* [0x48da58] ESTitle, ESTEXT 207 */
    ES2_ARMORY_LIST = 2,          /* [0x48da5c] ESTitle, ESTEXT 208 */
    ES2_ARMORY_ARM = 4,           /* [0x48da60] ESArm: the weapon images' parent */
    ES2_ARMORY_IMAGE = 5,         /* template of the gam\arm_weap.dat images ([0x48da64 + 4 slot]) */
    ES2_ARMORY_CLEAR = 8,         /* [0x48dad8] ESTEXT 209, 0x44a584 */
    ES2_ARMORY_SCRAP = 10,        /* [0x48dad4] ESTEXT 210, 0x44a508 */
    ES2_ARMORY_AVAILABLE = 12,    /* [0x48dadc] "+" 0x44a63b (empty body): salvage - bank cost */
    ES2_ARMORY_BANK = 14,         /* [0x48dae0] "-" 0x44a694 (empty body): bank cost */
    ES2_ARMORY_FREE = 17,         /* [0x48dae8] free entries of the bank */
    ES2_ARMORY_USED = 19,         /* [0x48daf0] 5 - free */
    ES2_ARMORY_INFO = 22,         /* [0x48dafc + 4k] the five wpn_info.bin lines, k = 0..4 */
    ES2_ARMORY_ROW = 33,          /* [0x48db28 + 4r] ESQuad of row r: armory.33 + 5r */
    ES2_ARMORY_CONTROLS = 163
};

/* Controls of the layout table's weapon_scrap_box. */
enum {
    ES2_WEAPON_SCRAP_SHIELD = 0,  /* [0x48da30] ESWindow on the display */
    ES2_WEAPON_SCRAP_ALERT = 1,   /* [0x48da34] ESAlert, ESTEXT 201 */
    ES2_WEAPON_SCRAP_NUMBER = 6,  /* [0x48da44] "%d %s" (0x412c8e, ESTEXT 0xcd) */
    ES2_WEAPON_SCRAP_CANCEL = 7,  /* [0x48da48] ESTEXT 202, 0x4483e9 */
    ES2_WEAPON_SCRAP_ACCEPT = 9,  /* [0x48da4c] ESTEXT 203, 0x448447 */
    ES2_WEAPON_SCRAP_CONTROLS = 11
};

/* VSHELL handler addresses of the three screens' controls. */
enum {
    ES2_BUILD_ON_ROW0 = 0x44764fu,      /* rows 0..8: 0x44764f + 0x61 t -> 0x4472cb(t) */
    ES2_BUILD_ON_SCRAP = 0x447570u,     /* -> the Herc scrap warning 0x447da1 */
    ES2_BUILD_ON_BUILD = 0x4475ceu,     /* -> 0x40eed4(type) */
    ES2_ARMORY_ON_ROW0 = 0x44a746u,     /* rows 0..25: 0x44a746 + 0x74 r */
    ES2_ARMORY_ON_CLEAR = 0x44a584u,    /* -> 0x449ccc */
    ES2_ARMORY_ON_SCRAP = 0x44a508u,    /* -> 0x448227(category) */
    ES2_ARMORY_ON_PLUS = 0x44a63bu,     /* empty body */
    ES2_ARMORY_ON_MINUS = 0x44a694u,    /* empty body */
    ES2_WEAPON_SCRAP_ON_CANCEL = 0x4483e9u,
    ES2_WEAPON_SCRAP_ON_ACCEPT = 0x448447u
};
#define ES2_BUILD_TYPES 9

/* The loaded game the editors work on. The SAV is owned by the game
 * (malloc'd; every applied operation replaces it). The session is the
 * library's, shared with the service bay (es2_service_bay.session:
 * screen word, bay selection), required. */
typedef struct es2_build_armory_game {
    const es2_vfs *vfs;           /* GAM/HERC_INF.DAT, GAM/DAMAGE.DAT, GAM/WEAPONS.DAT */
    es2_span tables;              /* ES2_SHELL_RESULTS_TABLES_SIZE bytes */
    es2_span editor_tables;       /* ES2_SHELL_EDITOR_TABLES_SIZE bytes */
    es2_shell_buffer save;
    es2_shell_editor_session *session;
} es2_build_armory_game;

/* The host's parts. Every member may be NULL. */
typedef struct es2_build_armory_host {
    void *ctx;
    /* Fonts by VSHELL global (0x46ddfc buttons, 0x46de00 labels). */
    const es2_dfn *(*font)(void *ctx, uint32_t global);
    /* A DBA member: "dba\\arm_weap.dba" (ARMORY images), "dba\\rpr_<herc>.dba"
     * (the BUILD Herc pictures, es2_build_picture_files). */
    const es2_bitmap *(*bitmap)(void *ctx, const char *file, int member);
    /* An ESTEXT entry. */
    const char *(*text)(void *ctx, uint32_t id);
    /* The weapon name of an item ID: <lang>\weapons.bin entry id (0x4087f0([0x4840c8], id)). */
    const char *(*weapon_name)(void *ctx, int id);
    /* <lang>\wpn_info.bin entry index ([0x48dbf8], five lines per row). */
    const char *(*weapon_info)(void *ctx, int index);
    /* A game file's bytes: "gam\\arm_weap.dat", "gam\\rpr_<herc>.dat"; an empty span if absent. */
    es2_span (*file)(void *ctx, const char *name);
    /* BUILD's Scrap 0x447570: the Herc scrap warning 0x447da1 (gui/repair.h).
     * */
    void (*herc_scrap)(void *ctx);
    /* After a Build 0x4475ce: the squad panel's 0x43e0d7, 0x43e167 and 0x43da0c. */
    void (*squad)(void *ctx);
    /* After every handler: its address and the library's `applied` (-1 refused). */
    void (*changed)(void *ctx, uint32_t handler, int applied);
} es2_build_armory_host;

typedef struct es2_build_armory {
    es2_ui *ui;                   /* set by the first build */
    es2_shell_flow *flow;         /* prefs +0x2d (weapons building); NULL: the session's byte */
    es2_build_armory_game *game;
    es2_build_armory_host host;
    es2_ui_id build_ids[ES2_BUILD_CONTROLS];
    es2_ui_id armory_ids[ES2_ARMORY_CONTROLS];
    es2_ui_id scrap_ids[ES2_WEAPON_SCRAP_CONTROLS];
    es2_ui_id images[ES2_SHELL_ARMORY_ROWS];  /* [0x48da64 + 4 slot] (0: no record for the row) */
    int16_t box_category;         /* 0x48da50: the category the warning was shown for */
    es2_shell_editor_view view;   /* the library's view after the last operation */
    es2_status last_status;       /* the last library call (ES2_OK, or why it was refused) */
    uint32_t last_handler;
    int last_applied;
    unsigned events;
} es2_build_armory;

/* The nine "gam\\rpr_*.dat" and "dba\\rpr_*.dba" files of the BUILD Herc
 * pictures by type (0x46ffd4, 0x470124). */
const char *es2_build_picture_file(int type, int dba);

void es2_build_armory_init(es2_build_armory *b, es2_shell_flow *flow, es2_build_armory_game *game,
                           const es2_build_armory_host *host);

/* The constructors 0x445de8, 0x4484c4 and 0x447e3e (registration table,
 * screens.c); ctx = the es2_build_armory. ARMORY also builds one ESBitmap per
 * record of the first list of gam\arm_weap.dat. 0 = built, -1 = bad argument
 * or the pool is full. */
int es2_screen_build_build(es2_ui *ui, void *ctx);
int es2_screen_armory_build(es2_ui *ui, void *ctx);
int es2_screen_weapon_scrap_box_build(es2_ui *ui, void *ctx);

/* 0x446f9d after the library's OPEN of screen 4 (which also leaves the
 * previous screen, 0x43a537): row enables 0x446ec5, texts 0x44738a, enables
 * 0x447064, the Herc pictures 0x415dc1, panel, lower panel and grid 0 shown,
 * then type 0 selected (0x4472cb(0)). Returns the library's status. */
es2_status es2_build_show(es2_build_armory *b);
/* 0x446fde: pictures cleared (0x415f4c), panel, lower panel and the
 * selected type's grid hidden. The SAV state's leave (0x412a37) is the
 * library's at the next OPEN or CLOSE. */
void es2_build_hide(es2_build_armory *b);
/* 0x447064: Scrap and Build enabled from the library's view. */
void es2_build_refresh(es2_build_armory *b);
/* The squad panel's selector in BUILD (0x43dcdd -> 0x447064): the library's
 * BAY operation, then the refresh. */
es2_status es2_build_select_bay(es2_build_armory *b, int16_t bay);

/* 0x449b87 after the library's OPEN of screen 5: Clear enabled by the
 * weapons-building byte, rows 0x4499b9, texts 0x44a33b, the panels shown,
 * row 0 selected (0x449d2f(0)). */
es2_status es2_armory_show(es2_build_armory *b);
/* 0x449c46: the selected row's image and the panels hidden, row -1. */
void es2_armory_hide(es2_build_armory *b);

/* One control's handler by its VSHELL address (what a click runs); click =
 * the event's button byte (1 left release, 3 right release). Returns the
 * library's `applied` (1 changed, 0 nothing), -1 when refused or unknown. */
int es2_build_armory_press(es2_build_armory *b, uint32_t handler, int click);

#endif
