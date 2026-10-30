#ifndef ES2_SHELL_GUI_PRACTICE_SCREEN_H
#define ES2_SHELL_GUI_PRACTICE_SCREEN_H

/* VSHELL's practice screen 0x44b310 on the widget toolkit as a
 * presenter of the shell flow over the practice library
 * (es2/shell/practice.h). Contract: vshell_ui.md sections 4.4, 15.2 and
 * 25; layout: the layout table's "practice" (shell/data/vshell_layout.json).
 *
 * The screen builds the table's 76 controls (the eight list rows, the five
 * parameter buttons with their value labels, Main Menu, Begin Mission), sets
 * the five value labels from the prefs block as the constructor does, and
 * runs the handlers: an entry 0x44c40c(i) recolours the old and the new row,
 * greys or enables Herc Type and relabels its value; a parameter button steps
 * its prefs byte forward on a left release and back on any other release and
 * relabels its value; Main Menu hides the screen and shows the main menu
 * (0x431718) without reverting the controls; Begin Mission commits and
 * writes the prefs block, then the host's launch (new game 0x40e8a5 in mode
 * 0 and the posted Use Default), then the flow's es2_shell_flow_game_created
 * and es2_shell_flow_dbsim. Portable C17, no platform call, no file I/O. */
#include <stdint.h>

#include "es2/shell/flow.h"
#include "es2/shell/gui/layout.h"
#include "es2/shell/gui/main_menu.h"
#include "es2/shell/practice.h"

/* Controls of the layout table's practice (index NN of "practice.NN"). */
enum {
    ES2_PRACTICE_BACKGROUND = 0,    /* [0x48dbfc] ESBitmap of [0x46de0c] (bay2a_84) */
    ES2_PRACTICE_PANEL = 1,         /* [0x48dc00] ESTitle, ESTEXT 0xf0 */
    ES2_PRACTICE_LIST_PANEL = 3,    /* [0x48dc04] ESRegionFill of the eight rows */
    ES2_PRACTICE_PARAMETERS = 4,    /* [0x48dc08] ESRegionFill of the parameters */
    ES2_PRACTICE_DAMAGE = 5,        /* [0x48dc0c] ESTEXT 0xfb, 0x44c5b9 */
    ES2_PRACTICE_AMMO = 7,          /* [0x48dc10] ESTEXT 0xfc, 0x44c676 */
    ES2_PRACTICE_DIFFICULTY = 9,    /* [0x48dc14] ESTEXT 0xfd, 0x44c733 */
    ES2_PRACTICE_TIME = 11,         /* [0x48dc18] ESTEXT 0xfe, 0x44c7f0 */
    ES2_PRACTICE_HERC = 13,         /* [0x48dc1c] ESTEXT 0xff, 0x44c8ad (+0x49 by the entry) */
    ES2_PRACTICE_VALUE_DAMAGE = 21, /* [0x48dc38] value of +0x26 (0x128 + v), +0x49 = 0 */
    ES2_PRACTICE_VALUE_AMMO = 23,   /* [0x48dc3c] value of +0x25 (0x12a + v) */
    ES2_PRACTICE_VALUE_DIFFICULTY = 25, /* [0x48dc40] value of +0x27 (0x35 + v) */
    ES2_PRACTICE_VALUE_TIME = 27,   /* [0x48dc44] value of +0x29 (0x12c + v) */
    ES2_PRACTICE_VALUE_HERC = 29,   /* [0x48dc48] value of +0x28 (0x6e + v) */
    ES2_PRACTICE_MAIN_MENU = 31,    /* [0x48dc4c] ESTEXT 0x100, 0x44c96a */
    ES2_PRACTICE_BEGIN = 33,        /* [0x48dc54] ESTEXT 0x102, 0x44ca26 */
    ES2_PRACTICE_ENTRY0 = 36,       /* [0x48dc5c + 4i] ESQuad of entry i at 36 + 5i (ESTEXT 0xf2 + i) */
    ES2_PRACTICE_CONTROLS = 76
};

/* VSHELL addresses of the screen's handlers (the flow knows only Main Menu). */
enum {
    ES2_PRACTICE_BEGIN_HANDLER = 0x44ca26u,
    ES2_PRACTICE_ENTRY_HANDLER0 = 0x44caa3u /* entries 0..7: 0x44caa3 + 0x61 i */
};

/* The host's parts. Every member may be NULL (then: no resource, and the
 * launch fails). */
typedef struct es2_practice_screen_host {
    void *ctx;
    /* Fonts by VSHELL global (0x46ddfc buttons and rows, 0x46de00 title and labels). */
    const es2_dfn *(*font)(void *ctx, uint32_t global);
    /* A bitmap by file name (the background global 0x46de0c is "dbm\\bay2a_84.dbm"). */
    const es2_bitmap *(*bitmap)(void *ctx, const char *file);
    /* An ESTEXT entry (0xf0..0xff, 0x100, 0x102 and the value labels 0x35..,
     * 0x6e.., 0x128.., 0x12a.., 0x12c..). NULL: an empty label (VSHELL
     * asserts for an index beyond the catalogue, vshell_ui.md 15.2). */
    const char *(*text)(void *ctx, uint32_t id);
    /* A literal of VSHELL.EXE by address (0x479c31..0x479c3d). */
    const char *(*literal)(void *ctx, uint32_t address);
    /* Begin Mission after the prefs write: the new game 0x40e8a5(name, rank)
     * in mode 0 and the posted Use Default (es2_shell_practice_run and its
     * files; with the cheat byte -@ the DEBUG dialog). 1 launched, 0 failed. */
    int (*launch)(void *ctx, const es2_shell_practice_launch *launch);
    /* After Main Menu and Begin Mission (handler = the VSHELL address, result
     * = the screen's answer): the flow's state may name another screen. */
    void (*changed)(void *ctx, uint32_t handler, es2_shell_flow_result result);
} es2_practice_screen_host;

typedef struct es2_practice_screen {
    es2_ui *ui;                     /* set by the build */
    es2_shell_flow *flow;           /* the prefs blocks (flow->prefs) and the screen */
    es2_shell_practice *practice;   /* selection 0x479c54, preset flag 0x473784, Herc Type +0x49 */
    const uint8_t *tables;          /* ES2_SHELL_PRACTICE_TABLES_SIZE bytes from the checked EXE */
    es2_main_menu *menu;            /* 0x431718 on Main Menu (NULL: none) */
    es2_practice_screen_host host;
    es2_ui_id ids[ES2_PRACTICE_CONTROLS];
    /* the last handler (for presenters and tests) */
    uint32_t last_handler;
    es2_shell_flow_result last_result;
    unsigned events;
} es2_practice_screen;

/* The screen constructor 0x44b310 (registration table, screens.c): the
 * table's controls under the toolkit's root, the five value labels from the
 * current prefs block, the handlers bound. ctx = the es2_practice_screen (flow,
 * practice, tables and host set by the caller). 0 = built, -1 = bad
 * argument or the pool is full. */
int es2_screen_practice_build(es2_ui *ui, void *ctx);

/* 0x44c322: show [0x48dbfc] and [0x48dc00], then entry 0 (0x44c40c(0)).
 * PRACTICE MISSIONS 0x431f3a calls it between the main menu's hide and the
 * mode store 0x40ec56(0), which is the host's (es2_shell_prefs_store; the
 * library's es2_shell_practice_show is this entry plus that store, so a host
 * calls this function instead of the library's entry). */
void es2_practice_screen_show(es2_practice_screen *ps);
/* 0x44c343: hide [0x48dbfc] and [0x48dc00]. */
void es2_practice_screen_hide(es2_practice_screen *ps);
/* 0x44c40c(i), i = 0..7, with the screen side. Returns 1 when the selection
 * changed, 0 when i was selected already (nothing happens), -1 refused. */
int es2_practice_screen_entry(es2_practice_screen *ps, int index);
/* One handler by its VSHELL address, as a release of `click` (1 left, 3
 * right; ES2_SHELL_CLICK_*) runs it. Entries and parameter buttons return
 * ES2_FLOW_DONE (IGNORED for Herc Type while disabled or an entry already
 * selected); Main Menu the flow's answer; Begin Mission DONE when launched,
 * REFUSED without a selection, FAILED when the write or the launch failed
 * (then, as the text menu does, the main menu: own behaviour). An unknown
 * address: ES2_FLOW_IGNORED. */
es2_shell_flow_result es2_practice_screen_press(es2_practice_screen *ps, uint32_t handler, int click);

#endif
