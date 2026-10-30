#ifndef ES2_SHELL_GUI_SAVED_GAMES_H
#define ES2_SHELL_GUI_SAVED_GAMES_H

/* VSHELL's Saved games screen 0x438c40 on the widget toolkit as
 * a presenter of the shell flow. Contract: vshell_ui.md sections 4.3,
 * 9.3, 13 and 26; layout: the layout table's saved_games
 * (shell/data/vshell_layout.json).
 *
 * Ten slot rows (edit fields) with the slot texts, the statistics panel of
 * the selected slot (the slot statistics records), naming a slot in its row
 * (the edit field's rules: set 0x475869, prefix 4, the pixel width of the row),
 * and SAVE, ACCEPT, CANCEL, RESTORE and EXIT as flow events keyed by the VSHELL
 * handler address with the screen's own enabling, colour and show/hide effects.
 * Entered from the main menu (caller 0) and the bay (caller 8). Portable
 * C17, no platform call, no file I/O: the slot table and the statistics
 * records are the host's, kept current by its save and load. */
#include <stdint.h>

#include "es2/shell/flow.h"
#include "es2/shell/gui/layout.h"
#include "es2/shell/gui/main_menu.h"
#include "es2/shell/movies.h"
#include "es2/shell/slots.h"

/* Controls of the layout table's saved_games (index NN of "saved_games.NN"). */
enum {
    ES2_SAVED_GAMES_BACKGROUND = 0,   /* [0x48d428] ESBitmap of [0x46de0c] (bay2a_84) */
    ES2_SAVED_GAMES_PANEL = 1,        /* [0x48d42c] ESTitle, ESTEXT 0x1b */
    ES2_SAVED_GAMES_ROWS = 3,         /* [0x48d430] ESRegionFill around the rows */
    ES2_SAVED_GAMES_ROW_FIRST = 4,    /* [0x48d434 + 4 slot] ESDialog rows 0..9 (controls 4..13) */
    ES2_SAVED_GAMES_CANCEL = 14,      /* [0x48d484] ESTEXT 0x33, 0x4384aa */
    ES2_SAVED_GAMES_ACCEPT = 16,      /* [0x48d488] ESTEXT 0x34, 0x43868a */
    ES2_SAVED_GAMES_SAVE = 18,        /* [0x48d48c] ESTEXT 0x1d, 0x438263 */
    ES2_SAVED_GAMES_RESTORE = 20,     /* [0x48d490] ESTEXT 0x1e, 0x438393 */
    ES2_SAVED_GAMES_EXIT = 22,        /* [0x48d494] ESTEXT 0x1f, 0x438424 */
    ES2_SAVED_GAMES_STATS = 24,       /* [0x48d498] ESRegionFill of the statistics */
    /* the twelve value labels the panel 0x4377bc writes */
    ES2_SAVED_GAMES_NAME = 26,        /* [0x48d4a0] */
    ES2_SAVED_GAMES_SKILL = 28,       /* [0x48d4a8] */
    ES2_SAVED_GAMES_RANK = 30,        /* [0x48d4b0] */
    ES2_SAVED_GAMES_HERC = 32,        /* [0x48d4b8] current, 33 [0x48d4bc] total */
    ES2_SAVED_GAMES_FLYER = 35,       /* [0x48d4c4] current, 36 [0x48d4c8] total */
    ES2_SAVED_GAMES_BASE = 38,        /* [0x48d4d0] current, 39 [0x48d4d4] total */
    ES2_SAVED_GAMES_SALVAGE = 43,     /* [0x48d4e4] */
    ES2_SAVED_GAMES_SECTOR = 45,      /* [0x48d4ec] */
    ES2_SAVED_GAMES_MISSION = 47,     /* [0x48d4f4] */
    ES2_SAVED_GAMES_SPARE = 48,       /* [0x48d4f8] ESRegionFill, hidden by every show */
    ES2_SAVED_GAMES_CONTROLS = 49
};

/* The ten row handlers 0x438873 + 0x61 k (each runs 0x437fef(k)). */
#define ES2_SAVED_GAMES_ROW_HANDLER(k) (0x438873u + 0x61u * (uint32_t)(k))

/* The host's parts; every member may be NULL. */
typedef struct es2_saved_games_host {
    void *ctx;
    /* Fonts by VSHELL global (0x46ddfc buttons, 0x46de00 titles, rows, labels). */
    const es2_dfn *(*font)(void *ctx, uint32_t global);
    /* A bitmap by file name (the background global 0x46de0c is "dbm\\bay2a_84.dbm"). */
    const es2_bitmap *(*bitmap)(void *ctx, const char *file);
    /* An ESTEXT entry (labels 0x1b, 0x1d..0x1f, 0x24..0x2e, 0x33, 0x34; the
     * panel's 0, 0x2f, 0x35 + skill, 0x39 + rank, 0x76 + sector). */
    const char *(*text)(void *ctx, uint32_t id);
    /* 0x430ec4(slot) after ACCEPT's save: the slot's statistics record from
     * the loaded game (es2_shell_stats_refresh). */
    void (*refresh)(void *ctx, int slot);
    /* After every flow event and the screen's side of it: the flow may name
     * another screen (the bay show of RESTORE and EXIT with caller 8 is the
     * host's; EXIT with caller 0 shows main_menu when it is set). */
    void (*changed)(void *ctx, uint32_t handler, es2_shell_flow_result result);
} es2_saved_games_host;

typedef struct es2_saved_games {
    es2_ui *ui;                          /* set by the build */
    es2_shell_flow *flow;
    es2_shell_movies *movies;            /* RESTORE clears its group flag 0x477946 (NULL: none) */
    es2_main_menu *main_menu;            /* EXIT with caller 0 runs its show 0x431718 (NULL: the host's) */
    es2_saved_games_host host;
    const es2_shell_slot_table *table;   /* 0x4826f4: texts and flags (NULL: empty, no flags) */
    const es2_shell_stats *stats;        /* records 0..10 at 0x48cebc (NULL: zero records) */
    es2_ui_id ids[ES2_SAVED_GAMES_CONTROLS];
    /* the last flow event (for presenters and tests) */
    uint32_t last_handler;
    es2_shell_flow_result last_result;
    unsigned events;
} es2_saved_games;

/* The screen constructor 0x438c40 (registration table, screens.c): the
 * table's controls under the toolkit's root, the rows bound to the row handlers
 * and the five buttons to the screen's handler. ctx = the es2_saved_games (flow
 * and host set by the caller). 0 = built, -1 = bad argument or full pool. */
int es2_screen_saved_games_build(es2_ui *ui, void *ctx);

/* 0x43a19c: the ten rows get their slot texts, the background, the panel
 * and the statistics shown, [0x48d4f8] hidden, the rows' region disabled,
 * ACCEPT and CANCEL disabled, SAVE by the selected slot and the loaded flag,
 * the selected row highlighted, RESTORE by the slot's flag, the panel. */
void es2_saved_games_show(es2_saved_games *sg);
/* 0x43a3f6: 0x437fef(10), then the panel, background, statistics and
 * [0x48d4f8] hidden. */
void es2_saved_games_hide(es2_saved_games *sg);
/* 0x437fef(slot): nothing while naming or for the selected slot; the old
 * row back to colour 0x27, the new one 0x29; SAVE and RESTORE by the flow's
 * rules; the flow's selected slot; the panel. */
void es2_saved_games_select(es2_saved_games *sg, int slot);
/* 0x4377bc: the twelve values of the selected slot's record (slot below 10
 * with its flag, or 10 with a game loaded), else ESTEXT 0 in each. */
void es2_saved_games_panel(es2_saved_games *sg);
/* One control's handler by its VSHELL address (a button or a row handler).
 * Returns the flow's answer; a row ES2_FLOW_DONE; a disabled control
 * ES2_FLOW_IGNORED; ACCEPT of a row of 80 bytes or more ES2_FLOW_REFUSED
 * (own: the slot record holds 79 characters, VSHELL's strcpy would run
 * past it). */
es2_shell_flow_result es2_saved_games_press(es2_saved_games *sg, uint32_t handler);

#endif
