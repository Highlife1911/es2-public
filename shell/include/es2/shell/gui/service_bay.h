#ifndef ES2_SHELL_GUI_SERVICE_BAY_H
#define ES2_SHELL_GUI_SERVICE_BAY_H

/* The service bay 0x43afd4 (frame, bar of nine image buttons, the
 * palette window) and the squad panel 0x43d029 on the widget toolkit, a
 * presenter of the shell flow. Built once from the layout table (screens
 * "service_bay" and "squad_panel"); one context serves both.
 *
 *   show 0x43b7f2(8) + 0x43b758   frame and bar shown; WEAPONS, REPAIR,
 *                                 BUILD, ARMORY, CREW enabled by the mode
 *                                 word (mode 0: WEAPONS and CREW only; the
 *                                 other three keep their state); word -1
 *   bar buttons 0x43a66c..0x43ad5a nothing before main-loop step 5 has run
 *                                 once (0x46c280) or when the word is the
 *                                 button's; otherwise every button drawn
 *                                 released (0x43a45b), the editor buttons
 *                                 drawn pressed, the current screen left
 *                                 (0x43a537), the next shown, the word
 *                                 stored and bptlt2 played (0x42f501)
 *   online image 0x43a5bd         the flow's BAY_ONLINE
 *   squad panel 0x43d677/0x43d779 shown with WEAPONS, REPAIR, BUILD, CREW;
 *                                 the eight bay rows select 0x43dcdd
 *
 * Contract: docs/reverse-engineering/vshell_ui.md sections 4.6 and 28. The
 * editors' own screens (WEAPONS, REPAIR, BUILD/ARMORY,
 * CREW, the mission screen) hang off the host's `screen` and `selected`
 * hooks. No file I/O: the host supplies fonts, bitmaps, texts, sounds, the
 * game-state view of the eight bays and the executable's two band tables. */
#include <stdint.h>

#include "es2/shell/editors.h"
#include "es2/shell/flow.h"
#include "es2/shell/gui/layout.h"

#define ES2_SCREEN_SERVICE_BAY_CONTROLS 21 /* service_bay.00 .. .20 */
#define ES2_SCREEN_SQUAD_PANEL_CONTROLS 57 /* squad_panel.00 .. .56 */

/* service_bay.NN */
enum {
    ES2_BAY_FRAME = 0,      /* [0x48d520] ESBitmap of [0x46de0c] */
    ES2_BAY_BAR = 1,        /* [0x48d528] ESRect, the bar */
    ES2_BAY_PALETTE = 2,    /* [0x48d524] ESPal (0, 30)-(639, 479) */
    ES2_BAY_ONLINE = 3,     /* [0x48d52c] dba\online.dba, 0x43a5bd */
    ES2_BAY_MAIN_MENU = 5,  /* [0x48d530] ESTEXT 0x13, 0x43a66c */
    ES2_BAY_SAVE = 7,       /* [0x48d534] 0x14, 0x43a768 */
    ES2_BAY_WEAPONS = 9,    /* [0x48d538] 0x15, 0x43a862 */
    ES2_BAY_REPAIR = 11,    /* [0x48d53c] 0x16, 0x43a962 */
    ES2_BAY_BUILD = 13,     /* [0x48d540] 0x17, 0x43aa62 */
    ES2_BAY_ARMORY = 15,    /* [0x48d544] 0x18, 0x43ab62 (built disabled) */
    ES2_BAY_CREW = 17,      /* [0x48d548] 0x19, 0x43ac5a */
    ES2_BAY_MISSION = 19    /* [0x48d54c] 0x1a, 0x43ad5a */
};

/* squad_panel.NN */
enum {
    ES2_SQUAD_GRID = 0,         /* [0x48d59c + 4k], k = 0..7: the bay's picture panel */
    ES2_SQUAD_GRID_NONE = 8,    /* [0x48d5bc]: the panel without a selection */
    ES2_SQUAD_PILOT_CAPTION = 9,     /* [0x48d5c0] ESTEXT 0x65 */
    ES2_SQUAD_PILOT = 10,            /* [0x48d5c4] */
    ES2_SQUAD_SKILL_CAPTION = 11,    /* [0x48d5c8] ESTEXT 0x66 */
    ES2_SQUAD_SKILL = 12,            /* [0x48d5cc] */
    ES2_SQUAD_CONDITION_CAPTION = 13,/* [0x48d5d0] ESTEXT 0x67 */
    ES2_SQUAD_CONDITION = 14,        /* [0x48d5d4] */
    ES2_SQUAD_INVENTORY = 15,        /* [0x48d5d8] ESTitle "Squad Inventory" (ESTEXT 0x64), handler 0x43e41e */
    ES2_SQUAD_ROW = 17               /* [0x48d5dc + 4k] = squad_panel.(17 + 5k): ESQuad, 0x43e477 + 0x6f k */
};

/* VSHELL addresses the screen hands to its host. */
enum {
    ES2_SERVICE_BAY_SOUND = 0x42f501u, /* bptlt2.wav (audio.md, "the own shell's sounds") */
    ES2_SERVICE_BAY_SHOW = 0x43b758u   /* `changed` after es2_service_bay_show */
};

/* What the squad panel reads of one bay (the hangar record [0x482ba7 + 4k]
 * and the team's pilot of the bay 0x410844(0x482b5c, k)). */
typedef struct es2_squad_bay {
    int record;              /* the bay holds a hangar record */
    int finished;            /* 0x411088: built (not under construction) */
    int16_t vehicle;         /* 0x411164: the record's vehicle word +0 */
    const char *herc;        /* the record's name (pointer at +4) */
    int16_t condition;       /* 0x4121f8(record + 8, record +0x4c) */
    int16_t progress;        /* record +0x4a (construction) */
    const char *pilot;       /* the pilot's name (+4), NULL: no pilot has the bay */
    int16_t skill;           /* the pilot's +0x25 */
} es2_squad_bay;

/* The host's parts. Every member may be NULL (no resource, no sound, an
 * empty bay, no editor). */
typedef struct es2_service_bay_host {
    void *ctx;
    /* Fonts by VSHELL global (0x46de00 squad panel, 0x46de04 the bar). */
    const es2_dfn *(*font)(void *ctx, uint32_t global);
    /* A DBA member ("dba\\mnu_bttn.dba" 1..3, "dba\\online.dba" 0..1) or,
     * with frame -1, the background "dbm\\bay2a_84.dbm" ([0x46de0c]). */
    const es2_bitmap *(*bitmap)(void *ctx, const char *file, int frame);
    /* An ESTEXT entry up to its NUL (NULL: the empty text). */
    const char *(*text)(void *ctx, uint32_t id);
    /* A sound call by VSHELL address (ES2_SERVICE_BAY_SOUND). */
    void (*sound)(void *ctx, uint32_t address);
    /* The CAREER group 0x46fc58, its position 0x46fc5a and the once-flag
     * 0x477946 (the group's movies shown). */
    void (*career)(void *ctx, int16_t *group, int16_t *position, int *movies_shown);
    /* A screen of the bar: show = 1 its show (0 main menu 0x431718, 1 Saved
     * games 0x43a19c, 2 WEAPONS 0x43fbd8, 3 REPAIR 0x43397c, 4 BUILD
     * 0x446f9d, 5 ARMORY 0x449b87, 6 CREW 0x442091, 7 the mission screen
     * 0x444873(flow->page)); show = 0 its leave UI in 0x43a537 (2 0x43fc62,
     * 3 0x433a7b, 4 0x446fde, 5 0x449c46, 6 0x44218a, 7 0x445095). */
    void (*screen)(void *ctx, int word, int show);
    /* The bay record view (0 = no record and no pilot). */
    int (*bay)(void *ctx, int bay, es2_squad_bay *out);
    /* 0x43cfa5 (load = 1, the panel's show) / 0x43cfea (0, its hide): the
     * screen's hot-spot images and grid layers (gam\arm_hots.dat for
     * WEAPONS, gam\rpr_hots.dat for REPAIR: 0x43c830, 0x43ce7a). */
    void (*panel)(void *ctx, int word, int load);
    /* The editor's part of a selection 0x43dcdd on screen `word`: 2 WEAPONS
     * before the panel's part (0x476658 := -1, 0x40bdf4(grid, 12),
     * 0x43fdac(0)); 3 REPAIR after it (0x434549(0, 0), 0x43436f, 0x43433f,
     * 0x433f86); 4 BUILD after it for a bay (0x447064); 6 CREW after it with
     * assign set (0x4426e5, then the panel's pilots 0x43e167 follow). */
    void (*selected)(void *ctx, int word, int16_t bay, int assign);
    /* REPAIR's picture of a bay ([0x48d1f8 + 4k]), hidden instead of
     * the squad grid while REPAIR's tab 0x474094 is not 0. */
    es2_ui_id (*repair_grid)(void *ctx, int16_t bay);
    /* After every flow event, and after the show with ES2_SERVICE_BAY_SHOW. */
    void (*changed)(void *ctx, uint32_t handler, es2_shell_flow_result result);
} es2_service_bay_host;

typedef struct es2_service_bay {
    es2_shell_flow *flow;               /* required */
    es2_service_bay_host host;
    /* The executable's tables (read from VSHELL.EXE by the host, as the
     * editor tables): the condition band limits 0x47665c (5 words) and the
     * band colours 0x476666 (6 words). */
    int16_t band_limit[5];
    int16_t band_colour[6];
    /* main-loop step 5 has run once (0x46c280, set at 0x4020b8); the host
     * sets it */
    int loop_started;
    /* the screen word 0x4758b8 as VSHELL stores it (session.screen), the
     * bay selection 0x482bc9 (session.bay), the CREW order 0x477778
     * (session.order) and REPAIR's tab 0x474094 (session.tab), shared with
     * the editors */
    es2_shell_editor_session session;
    /* filled by the builds */
    es2_ui *ui;
    es2_ui_id bay[ES2_SCREEN_SERVICE_BAY_CONTROLS];
    es2_ui_id squad[ES2_SCREEN_SQUAD_PANEL_CONTROLS];
    /* the last flow event (tests, presenters) */
    uint32_t last_handler;
    es2_shell_flow_result last_result;
    unsigned events;
    /* The Herc pictures (squad_pictures.h); NULL: the panels stay
     * empty. A host wires its `panel` hook to es2_squad_pictures_panel. */
    struct es2_squad_pictures *pictures;
} es2_service_bay;

/* The constructors 0x43afd4 and 0x43d029 (registration table): the layout
 * table's controls, the bar buttons and the eight rows bound to the screen. ctx
 * = the es2_service_bay (flow and host set by the caller). The service bay
 * build also sets the session to its image values (selection 0, word -1).
 * 0 = built, -1 = bad argument or the table could not be built. */
int es2_screen_service_bay_build(es2_ui *ui, void *ctx);
int es2_screen_squad_panel_build(es2_ui *ui, void *ctx);

/* The bay show 0x43b7f2(8) + 0x43b758 (every path into the bay: new game,
 * CONTINUE, RESTORE, Saved games EXIT, the next mission). */
void es2_service_bay_show(es2_service_bay *b);
/* 0x43b8cd: bar and frame hidden, palette 1 selected. */
void es2_service_bay_hide(es2_service_bay *b);
/* One bar handler by its VSHELL address (what a press on the button runs).
 * Returns the flow's answer, ES2_FLOW_IGNORED when VSHELL does nothing. */
es2_shell_flow_result es2_service_bay_press(es2_service_bay *b, uint32_t handler);
/* The hangar clear 0x410aaf of a new game 0x410b11 and a load 0x410e2e:
 * the bay selection becomes 0 (vshell_state.md). The host calls it from its
 * new_game and load callbacks. */
void es2_service_bay_game_loaded(es2_service_bay *b);

/* ---- the squad panel, for the editor screens ----------------------------- */
/* 0x43d677(word): load, labels, rows, the grid of the selection, the seven
 * labels and the title shown, the rows coloured. */
void es2_squad_panel_show(es2_service_bay *b, int word);
/* 0x43d779(word): unload, the grid of the selection (and REPAIR's for word
 * 3) and the seven labels hidden. */
void es2_squad_panel_hide(es2_service_bay *b, int word);
/* 0x43dcdd(k): the selection by the screen word; assign is the selector
 * flag 0x47665a (1 inside the eight row handlers and CREW's CLEAR). */
void es2_squad_panel_select(es2_service_bay *b, int16_t bay, int assign);
/* 0x43da1a(k): the Pilot, Skill and Condition values. */
void es2_squad_panel_labels(es2_service_bay *b, int16_t bay);
/* 0x43e0d7: every row's Herc column; 0x43e167: every row's pilot column. */
void es2_squad_panel_hercs(es2_service_bay *b);
void es2_squad_panel_pilots(es2_service_bay *b);
/* 0x43e05f: the band of a condition (first limit it exceeds, 5 if none). */
int es2_squad_band(const es2_service_bay *b, int16_t condition);

#endif
