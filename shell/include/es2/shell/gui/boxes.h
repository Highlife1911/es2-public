#ifndef ES2_SHELL_GUI_BOXES_H
#define ES2_SHELL_GUI_BOXES_H

/* VSHELL's modal boxes and dialogs as graphical screens over the flow
 * (es2/shell/flow.h) and the widget toolkit (gui/widgets.h, gui/layout.h):
 * the Quit box 0x431fa8, the End of game box 0x44d2bc, the launch warning
 * 0x44d66c, the Replay box 0x44cdac, the CD prompt 0x4322a8 and the DEBUG
 * mission-file dialog 0x44dd38, built from the layout table; the
 * Preferences Alert box (built by the Preferences screen) through two
 * handlers; the Quit-box repaint of the main loop (step 10, 0x402151) and
 * the Alt+F4 system action. Contract: docs/reverse-engineering/vshell_ui.md,
 * section 22 (movies 19, CD prompt 20, help routine 21).
 *
 * The boxes draw nothing themselves: the toolkit's classes draw them. A box
 * button's handler does what VSHELL's handler does to the windows (hide, show,
 * the quit flag) and feeds the flow the same handler address; effects with
 * I/O stay the flow's host callbacks. Texts, fonts and the background bitmap
 * come from the host (no ESTEXT, executable literal or image is stored
 * here). */
#include <stdint.h>

#include "es2/shell/flow.h"
#include "es2/shell/gui/layout.h"
#include "es2/shell/gui/loop.h"
#include "es2/shell/gui/widgets.h"

/* VSHELL handler addresses of the box controls (the layout table). */
enum {
    ES2_BOXES_QUIT_CANCEL = 0x4321c1,
    ES2_BOXES_QUIT_ACCEPT = 0x432237,
    ES2_BOXES_CD_CONTINUE = 0x4324fa,
    ES2_BOXES_CD_QUIT = 0x432569,
    ES2_BOXES_END_OKAY = 0x44d60b,
    ES2_BOXES_WARNING_OKAY = 0x44da94,
    ES2_BOXES_REPLAY_YES = 0x44d1d4,
    ES2_BOXES_REPLAY_NO = 0x44d24d,
    ES2_BOXES_DEBUG_DEFAULT = 0x44dbea,
    ES2_BOXES_DEBUG_ACCEPT = 0x44dc4d,
    ES2_BOXES_PREFS_FULL_SCREEN = 0x437608,  /* preferences.22 */
    ES2_BOXES_ALERT_ACCEPT = 0x437678        /* preferences.05 */
};

/* Control counts of the six screens (the layout table). */
enum { ES2_BOXES_QUIT_N = 7, ES2_BOXES_END_N = 7, ES2_BOXES_WARNING_N = 7, ES2_BOXES_REPLAY_N = 9,
       ES2_BOXES_CD_N = 7, ES2_BOXES_DEBUG_N = 13 };

/* What the host supplies. Every member may be NULL. */
typedef struct es2_boxes_host {
    void *ctx;
    /* The fonts of the globals 0x46ddfc and 0x46de00 (dfn\font2.dfn). */
    const es2_dfn *(*font)(void *ctx, uint32_t global);
    /* The bitmap of a global: 0x46de0c (dbm\bay2a_84.dbm, the Replay and
     * DEBUG backgrounds). */
    const es2_bitmap *(*bitmap)(void *ctx, uint32_t global);
    /* An ESTEXT entry of the running language (NULL: empty). */
    const char *(*estext)(void *ctx, int id);
    /* A literal of VSHELL.EXE by address (the DEBUG dialog's own labels
     * 0x47a4c1, 0x47a4c7, 0x47a4df, 0x47a522, 0x47a52b, 0x47a52c). */
    const char *(*literal)(void *ctx, uint32_t address);
    /* Replay No's main menu 0x431718: shown even when flow.screen already
     * named the main menu (after -X3/-X4). */
    void (*screen)(void *ctx, es2_shell_flow *flow);
    /* DEBUG dialog: Use Default 0x44db5c and ACCEPT 0x44dc4d (msn\<text>.msn)
     * after the dialog is hidden. */
    void (*debug_default)(void *ctx);
    void (*debug_accept)(void *ctx, const char *text);
    /* Preferences: 0x436e5d after the Alert ACCEPT stored byte 6. */
    void (*prefs_refresh)(void *ctx);
} es2_boxes_host;

typedef struct es2_boxes {
    es2_ui *ui;
    es2_shell_flow *flow;
    es2_boxes_host host;
    int cheat;                       /* 0x482368 (-@): the DEBUG dialog stays open */
    es2_ui_id quit[ES2_BOXES_QUIT_N], end[ES2_BOXES_END_N], warning[ES2_BOXES_WARNING_N],
        replay[ES2_BOXES_REPLAY_N], cd[ES2_BOXES_CD_N], debug[ES2_BOXES_DEBUG_N];
    es2_shell_flow_box shown;        /* the flow box the boxes show now (es2_boxes_sync) */
    int cd_retry;                    /* 0x473bcc */
    uint32_t quit_passes;            /* 0x46c294 */
} es2_boxes;

/* A fresh state (nothing built). */
void es2_boxes_init(es2_boxes *boxes, es2_ui *ui, es2_shell_flow *flow, const es2_boxes_host *host, int cheat);

/* The six screen builders (es2_ui_screens[]; context = the es2_boxes):
 * es2_ui_layout_build of the screen, the handlers bound, every window's
 * `user` = the boxes. The Quit box also becomes ui->quit_box (Ctrl+Q,
 * Alt+F4). 0 = built, -1 = refused. */
int es2_screen_quit_box_build(es2_ui *ui, void *context);
int es2_screen_end_of_game_box_build(es2_ui *ui, void *context);
int es2_screen_launch_warning_box_build(es2_ui *ui, void *context);
int es2_screen_replay_box_build(es2_ui *ui, void *context);
int es2_screen_cd_prompt_build(es2_ui *ui, void *context);
int es2_screen_debug_dialog_build(es2_ui *ui, void *context);
/* All six in VSHELL's construction order (0x401812..0x40187b). */
int es2_boxes_build(es2_boxes *boxes);

/* The handler of a box control by VSHELL address (NULL: not a box handler);
 * for resolvers of other screens (Preferences' 0x437608 and 0x437678). */
es2_ui_handler es2_boxes_handler(uint32_t address);
/* After the Preferences screen is built from the table: its Full Screen radio
 * (preferences.22) and Alert ACCEPT (preferences.05) get these boxes as
 * `user`. -1 when the screen is not built. */
int es2_boxes_bind_preferences(es2_boxes *boxes);

/* The show functions of VSHELL (texts, then the modal show or show). */
void es2_boxes_end_show(es2_boxes *boxes, int16_t final_word);    /* 0x44d55f */
void es2_boxes_warning_show(es2_boxes *boxes, int16_t warning);   /* 0x44d90c */
void es2_boxes_replay_show(es2_boxes *boxes, int16_t kind);       /* 0x44d0e7 */
void es2_boxes_replay_hide(es2_boxes *boxes);                     /* 0x44d1b9 */
/* 0x413305: shows the DEBUG dialog with the CAREER entry's name as the
 * default (0x44e1b5) and, without -@, posts the two synthetic clicks on Use
 * Default (left down, left up). */
void es2_boxes_debug_show(es2_boxes *boxes, const char *default_name);
void es2_boxes_debug_hide(es2_boxes *boxes);                      /* 0x44e217 */

/* The presenter of flow.box: shows the End, warning or Replay box the flow
 * names (with flow.box_value) when it is not shown yet, and records it. The
 * box handlers hide their boxes themselves; the Preferences Alert box is
 * shown by its radio's handler. */
void es2_boxes_sync(es2_boxes *boxes);

/* The CD prompt of the movie player (0x41edda..0x41ee10): show the shield,
 * gate [0x470fb0] := 0, run the drain helper until Continue or the quit
 * flag, retry flag := 0, gate := 1. Returns 1 for Continue (retry), 0 when
 * the quit flag ended the wait (the movie player's ES2_SHELL_MOVIE_CD_PROMPT
 * answer). */
int es2_boxes_cd_prompt(es2_boxes *boxes);
/* The drain helper 0x402266: up to 24 rounds of pump, then (if focused)
 * input poll, timer and drain, palette apply, dirty flush; it stops on
 * WM_QUIT or the quit flag. */
void es2_boxes_drain_helper(es2_gui_shell *shell);

/* Main-loop step 10 (0x402151..0x40217b), as an es2_gui_hooks.map-shaped
 * hook (context = the boxes): while the Quit box is shown, every 51st pass
 * redraws it (0x41f3ed). */
void es2_boxes_pass(void *context, es2_gui_shell *shell);
/* The shell's system hook for Alt+F4 (ES2_GUI_SYS_QUIT_BOX): release the
 * capture, show the Quit box; other actions are not the boxes'. */
void es2_boxes_system(void *context, es2_gui_shell *shell, es2_gui_system_action action);

#endif
