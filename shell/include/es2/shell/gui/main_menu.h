#ifndef ES2_SHELL_GUI_MAIN_MENU_H
#define ES2_SHELL_GUI_MAIN_MENU_H

/* VSHELL's main menu screen 0x430fc4 on the widget toolkit as a
 * presenter of the shell flow. Contract: vshell_ui.md sections 4.1
 * and 23; layout: the layout table's main_menu (shell/data/vshell_layout.json).
 *
 * The screen builds the table's 25 controls, plays the animated background
 * (dbm\bay2a_80..84, 84 twice, one frame per 500 ms timer tick), shows the
 * menu panel when the animation reaches its sixth frame (0x431830), and
 * turns each of the ten buttons into the handler's effects: the flow event
 * keyed by the VSHELL handler address, the show/hide calls of the screen
 * objects, and the host's parts (practice writes and launch, movies, the
 * manual, sounds). Portable C17, no platform call, no file I/O. */
#include <stdint.h>

#include "es2/shell/flow.h"
#include "es2/shell/gui/layout.h"
#include "es2/shell/movies.h"

/* Controls of the layout table's main_menu (index NN of "main_menu.NN"). */
enum {
    ES2_MAIN_MENU_DISPLAY = 0,      /* [0x48d1a4] ESWindow on the display root: CREDITS' cover */
    ES2_MAIN_MENU_ANIMATION = 1,    /* [0x48d1a0] ESAnim2, handler 0x431830 */
    ES2_MAIN_MENU_BACKGROUND = 2,   /* [0x48d1a8] ESBitmap of [0x46de0c] (bay2a_84) */
    ES2_MAIN_MENU_PANEL = 3,        /* [0x48d1ac] ESTitle, ESTEXT 2 */
    ES2_MAIN_MENU_INSTANT_ACTION = 5,   /* [0x48d1b0] ESTEXT 3, 0x431935 */
    ES2_MAIN_MENU_START_NEW_GAME = 7,   /* [0x48d1bc] ESTEXT 4, 0x431a08 */
    ES2_MAIN_MENU_CONTINUE_GAME = 9,    /* [0x48d1b4] ESTEXT 5, 0x431a73 */
    ES2_MAIN_MENU_SAVE_RESTORE = 11,    /* [0x48d1b8] ESTEXT 6, 0x431b27 */
    ES2_MAIN_MENU_QUIT = 13,            /* [0x48d1c0] ESTEXT 0xe, 0x431db6 */
    ES2_MAIN_MENU_ONLINE_MANUAL = 15,   /* [0x48d1d4] ESTEXT 0xf, 0x431e1b */
    ES2_MAIN_MENU_PRACTICE = 17,        /* [0x48d1c8] ESTEXT 7, 0x431f3a */
    ES2_MAIN_MENU_PREFERENCES = 19,     /* [0x48d1c4] ESTEXT 0xa, 0x431b9b */
    ES2_MAIN_MENU_VIEW_DEMO = 21,       /* [0x48d1cc] ESTEXT 0xb, 0x431bfe */
    ES2_MAIN_MENU_CREDITS = 23,         /* [0x48d1d0] ESTEXT 0xd, 0x431c7b */
    ES2_MAIN_MENU_CONTROLS = 25
};

/* VSHELL addresses the screen hands to its host. */
enum {
    ES2_MAIN_MENU_ANIMATION_STEP = 0x431830u,  /* the ESAnim2 callback */
    ES2_MAIN_MENU_CREDITS_HANDLER = 0x431c7bu, /* not a flow event */
    ES2_MAIN_MENU_SOUND_SWITCH = 0x42f58du,    /* lswitch2.wav (audio.md, "the own shell's sounds") */
    ES2_MAIN_MENU_SOUND_FADE_IN = 0x42f894u,   /* music fade-in of the -X6 entry (0x4018c6) */
    ES2_MAIN_MENU_PREFS_NOTICE = 0x2f          /* prefs byte the first menu show tests for 1 (0x431890) */
};

/* The host's parts. Every member may be NULL (then: no resource, no sound,
 * no box, and the host steps of INSTANT ACTION and PRACTICE succeed). */
typedef struct es2_main_menu_host {
    void *ctx;
    /* Fonts by VSHELL global (0x46ddfc buttons, 0x46de00 titles and labels). */
    const es2_dfn *(*font)(void *ctx, uint32_t global);
    /* A bitmap by file name ("dbm\\bay2a_80.dbm" ... "dbm\\bay2a_84.dbm";
     * the background global 0x46de0c is "dbm\\bay2a_84.dbm"). */
    const es2_bitmap *(*bitmap)(void *ctx, const char *file);
    /* An ESTEXT entry (labels: 2, 3..7, 0xa, 0xb, 0xd, 0xe, 0xf). */
    const char *(*text)(void *ctx, uint32_t id);
    /* A sound or music call by VSHELL address (ES2_MAIN_MENU_SOUND_*). */
    void (*sound)(void *ctx, uint32_t address);
    /* 0x401a81(message, caption, MB_ICONEXCLAMATION) with leaving and
     * re-entering full screen around it (0x43189b..0x4318cb). */
    void (*notice)(void *ctx, int message, int caption);
    /* The host steps of a handler: INSTANT ACTION 0x431935 stage 0 (preset
     * flag 0x473784, the mode store and 0x44c58b's practice writes) and
     * stage 1 (new game 0x40e8a5, launch 0x40f68c, then the flow's
     * es2_shell_flow_game_created and es2_shell_flow_dbsim); PRACTICE
     * MISSIONS 0x431f3a stage 0 (the practice show 0x44c322 and its
     * writes). Return 0 to stop the handler there (refused or failed). */
    int (*entry)(void *ctx, uint32_t handler, int stage);
    /* The movie player 0x41e98c(build_palette) over the queue. */
    void (*play)(void *ctx, es2_shell_movies *movies, int build_palette);
    /* After every flow event: the flow's state may name another screen or a
     * box (the presenter shows it; the main menu has hidden itself where
     * VSHELL does). handler = the event, result = the flow's answer. */
    void (*changed)(void *ctx, uint32_t handler, es2_shell_flow_result result);
} es2_main_menu_host;

typedef struct es2_main_menu {
    es2_ui *ui;                     /* set by the build */
    es2_shell_flow *flow;
    es2_shell_movies *movies;       /* NULL: nothing queued */
    es2_main_menu_host host;
    int continue_flag;              /* slot 10's flag 0x482afd (slot table 0x4826f4, record 10, +0x5d) */
    /* process state of the screen (vshell_state.md) */
    int menu_shown;                 /* 0x47374c: the animation reached frame 5 once */
    int sound_played;               /* 0x473750: lswitch2 requested once */
    es2_ui_id ids[ES2_MAIN_MENU_CONTROLS];
    /* the last flow event (for presenters and tests) */
    uint32_t last_handler;
    es2_shell_flow_result last_result;
    unsigned events;
} es2_main_menu;

/* The screen constructor 0x430fc4 (registration table, screens.c): the
 * table's controls under the toolkit's root, the ten buttons bound to the
 * screen's handler, the ESAnim2 to 0x431830. ctx = the es2_main_menu (flow,
 * host and movies set by the caller). 0 = built, -1 = bad argument or the pool
 * is full. */
int es2_screen_main_menu_build(es2_ui *ui, void *ctx);

/* 0x431718: show the background bitmap and the panel; CONTINUE GAME is
 * enabled (label colour 0x29, button colour 0x22) with slot 10's flag, else
 * disabled (0x26, 0x26); redraw and draw it. */
void es2_main_menu_show(es2_main_menu *m);
/* 0x4317c3: hide the background bitmap and the panel. */
void es2_main_menu_hide(es2_main_menu *m);
/* The main menu's first appearance (0x4018d0..0x4018f2, 0x40199a..0x4019bc):
 * clear, realization gate := 1, palette 1 selected and applied, the
 * animation shown. */
void es2_main_menu_start(es2_main_menu *m);
/* The entry dispatch 0x40188a around the flow, after the start-up palette
 * 0x401d37 (palette 1 applied; it reaches the table only with -X3/-X4,
 * whose gate is already set): -X3/-X4 run the flow's
 * results path; -X6 the music fade-in; the others queue and play the two
 * intro movies. When the flow then shows the main menu without a
 * box, the animation starts (es2_main_menu_start; the results path's own
 * branches: vshell_ui.md section 23). Returns the entry kind. */
es2_shell_flow_entry es2_main_menu_enter(es2_main_menu *m, int state);
/* One button's handler by its VSHELL address (what a click on the button
 * runs). Returns the flow's answer; CREDITS (no flow event) returns
 * ES2_FLOW_DONE, an unknown address ES2_FLOW_IGNORED. */
es2_shell_flow_result es2_main_menu_press(es2_main_menu *m, uint32_t handler);
/* The ESAnim2 callback 0x431830 (any event of the animation window). */
void es2_main_menu_animation_step(es2_main_menu *m);

#endif
