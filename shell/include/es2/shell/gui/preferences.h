#ifndef ES2_SHELL_GUI_PREFERENCES_H
#define ES2_SHELL_GUI_PREFERENCES_H

/* VSHELL's Preferences screen 0x435598 on the widget toolkit
 * as a presenter of the shell flow. Contract: vshell_ui.md sections
 * 4.5, 11.4, 22.2 and 27; layout: the layout table's preferences
 * (shell/data/vshell_layout.json).
 *
 * The screen builds the table's 56 controls: the background, the panel with its
 * five boxes (Audio/Speech Options with the Music and Sound Effects check
 * boxes, Game Resolution, the repair and weapons-building radios, Window /
 * Full Screen), CANCEL and ACCEPT, and the Alert box (shield, ESAlert,
 * ACCEPT) whose two handlers are the boxes' (gui/boxes.h). Each control's
 * handler stores its byte in the flow's current Preferences block
 * (es2_shell_flow_prefs_control), redraws its radio group from the block and
 * asks the host for the music ramps (es2/shell/sounds.h runs them);
 * the display switch (0x407636) is the shell loop's (gui/loop.h). Portable C17,
 * no platform call, no file I/O. */
#include <stdint.h>

#include "es2/shell/flow.h"
#include "es2/shell/gui/boxes.h"
#include "es2/shell/gui/layout.h"
#include "es2/shell/gui/main_menu.h"

/* Controls of the layout table's preferences (index NN of "preferences.NN"). */
enum {
    ES2_PREFERENCES_BACKGROUND = 0,   /* [0x48d384] ESBitmap of [0x46de0c] (bay2a_84) */
    ES2_PREFERENCES_SHIELD = 1,       /* [0x48d3bc] ESWindow on the display: the Alert box's shield */
    ES2_PREFERENCES_ALERT = 2,        /* [0x48d3c0] ESAlert, ESTEXT 0x126 */
    ES2_PREFERENCES_ALERT_ACCEPT = 5, /* [0x48d3c8] ESTEXT 16, 0x437678 (gui/boxes.h) */
    ES2_PREFERENCES_PANEL = 7,        /* [0x48d388] ESTitle, ESTEXT 0x103 */
    ES2_PREFERENCES_CANCEL = 9,       /* [0x48d38c] ESTEXT 0x10b, 0x437220 */
    ES2_PREFERENCES_ACCEPT = 11,      /* [0x48d390] ESTEXT 0x10c, 0x4372e1 */
    ES2_PREFERENCES_WINDOW = 20,      /* [0x48d3b0] 0x437597 */
    ES2_PREFERENCES_FULL_SCREEN = 22, /* [0x48d3b4] 0x437608 (gui/boxes.h) */
    ES2_PREFERENCES_HIGH_RES = 30,    /* [0x48d3e4] 0x437758 */
    ES2_PREFERENCES_LOW_RES = 32,     /* [0x48d3e8] 0x4376f7 */
    ES2_PREFERENCES_MUSIC = 35,       /* [0x48d3ec] 0x437351 */
    ES2_PREFERENCES_SFX = 37,         /* [0x48d3f0] 0x4373b2 */
    ES2_PREFERENCES_REPAIR_0 = 43,    /* [0x48d404] 0x437413 */
    ES2_PREFERENCES_REPAIR_1 = 45,    /* [0x48d408] 0x437474 */
    ES2_PREFERENCES_REPAIR_2 = 47,    /* [0x48d40c] 0x4374d5 */
    ES2_PREFERENCES_BUILD_0 = 52,     /* [0x48d41c] 0x437536 */
    ES2_PREFERENCES_BUILD_1 = 54,     /* [0x48d420] 0x437696 */
    ES2_PREFERENCES_CONTROLS = 56
};

/* VSHELL handler addresses of the screen's own controls (the layout table). */
enum {
    ES2_PREFERENCES_ON_MUSIC = 0x437351u,
    ES2_PREFERENCES_ON_SFX = 0x4373b2u,
    ES2_PREFERENCES_ON_REPAIR_0 = 0x437413u,
    ES2_PREFERENCES_ON_REPAIR_1 = 0x437474u,
    ES2_PREFERENCES_ON_REPAIR_2 = 0x4374d5u,
    ES2_PREFERENCES_ON_BUILD_0 = 0x437536u,
    ES2_PREFERENCES_ON_BUILD_1 = 0x437696u,
    ES2_PREFERENCES_ON_WINDOW = 0x437597u,
    ES2_PREFERENCES_ON_LOW_RES = 0x4376f7u,
    ES2_PREFERENCES_ON_HIGH_RES = 0x437758u,
    /* CANCEL 0x437220 and ACCEPT 0x4372e1 are ES2_FLOW_PREFS_CANCEL/_ACCEPT;
     * Full Screen 0x437608 and the Alert ACCEPT 0x437678 are the boxes'. */
    ES2_PREFERENCES_MUSIC_OUT = 0x42f7f0u, /* the music ramp down (audio.md, "the own shell's sounds") */
    ES2_PREFERENCES_MUSIC_IN = 0x42f894u   /* the music ramp up */
};

/* The host's parts. Every member may be NULL (no resource, no music). */
typedef struct es2_preferences_host {
    void *ctx;
    /* Fonts by VSHELL global (0x46ddfc buttons, 0x46de00 titles and labels). */
    const es2_dfn *(*font)(void *ctx, uint32_t global);
    /* A bitmap by file name and member: "dba\\chk_box.dba" members 0 (set)
     * and 1 (clear) for the radios; "dbm\\bay2a_84.dbm" (the global
     * 0x46de0c) with member -1 for the background. */
    const es2_bitmap *(*bitmap)(void *ctx, const char *file, int member);
    /* An ESTEXT entry (0x10, 0x103, 0x106, 0x107, 0x108, 0x10b, 0x10c,
     * 0x111..0x117, 0x120..0x127). */
    const char *(*text)(void *ctx, uint32_t id);
    /* A music ramp by VSHELL address (ES2_PREFERENCES_MUSIC_OUT/_IN) with
     * the current block as it is at the call:
     * es2_shell_sounds_ramp_begin(sounds, prefs, up, ...) runs only when
     * prefs byte 0 is set. */
    void (*music)(void *ctx, uint32_t address, const uint8_t prefs[ES2_SHELL_PREFS_SIZE]);
    /* After every flow event (CANCEL, ACCEPT, a control): the flow's state
     * may name another screen (the main menu is shown already when
     * es2_preferences.menu is set). */
    void (*changed)(void *ctx, uint32_t handler, es2_shell_flow_result result);
} es2_preferences_host;

typedef struct es2_preferences {
    es2_ui *ui;                     /* set by the build */
    es2_shell_flow *flow;
    es2_main_menu *menu;            /* 0x431718 after CANCEL and ACCEPT (NULL: none) */
    es2_boxes *boxes;               /* the Full Screen radio and the Alert ACCEPT (NULL: inert) */
    es2_preferences_host host;
    /* the write-only shadows 0x474d60 (+0x2c), 0x474d62 (+4), 0x474d64
     * (+0x2d), 0x474d66 (+6) (vshell_state.md; image -1) */
    int16_t shadow_repair, shadow_resolution, shadow_build, shadow_display;
    es2_ui_id ids[ES2_PREFERENCES_CONTROLS];
    /* the last flow event (for presenters and tests) */
    uint32_t last_handler;
    es2_shell_flow_result last_result;
    unsigned events;
} es2_preferences;

/* A fresh state: nothing built, the shadows at their image value -1. */
void es2_preferences_init(es2_preferences *p, es2_shell_flow *flow, es2_main_menu *menu, es2_boxes *boxes,
                          const es2_preferences_host *host);

/* The screen constructor 0x435598 (registration table, screens.c): the
 * table's controls under the toolkit's root, the ten own handlers bound, the
 * Full Screen radio and the Alert ACCEPT to the boxes' handlers (bound to the
 * boxes by es2_boxes_bind_preferences when p->boxes is set; their `user` is the
 * boxes). ctx = the es2_preferences. 0 = built, -1 = bad argument or the
 * pool is full. */
int es2_screen_preferences_build(es2_ui *ui, void *ctx);

/* 0x436d45 (PREFERENCES 0x431b9b after the main menu's hide): every radio
 * group from the current block, the setters' re-stores of +0x2c, +0x2d, +4
 * and +6 with their own values (only the shadows change), then the
 * background and the panel shown. */
void es2_preferences_show(es2_preferences *p);
/* 0x436da7: hide the background and the panel. */
void es2_preferences_hide(es2_preferences *p);
/* 0x436e5d: the Window / Full Screen radios from byte 6 (0: Window, 1: Full
 * Screen, other values: unchanged). The boxes' es2_boxes_host.prefs_refresh
 * (context = the es2_preferences). */
void es2_preferences_display_refresh(void *p);

/* One control's handler by its VSHELL address (what a click runs; Full
 * Screen and the Alert ACCEPT are the boxes' handlers). Returns the flow's
 * answer, ES2_FLOW_IGNORED for an unknown address. */
es2_shell_flow_result es2_preferences_press(es2_preferences *p, uint32_t handler);

#endif
