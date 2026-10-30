#ifndef ES2_SHELL_GUI_REGISTRATION_H
#define ES2_SHELL_GUI_REGISTRATION_H

/* The Registration screen (VSHELL 0x43bd2e, "new career") on the
 * widget toolkit, a presenter of the shell flow. Built once from the
 * layout table (screen "registration"); the handlers feed the flow:
 *
 *   name field   0x43c47e  after every key and command event: ACCEPT
 *                          disabled (label colour 38, frame 38) while the
 *                          text is empty, else enabled (41, 34)
 *   SKILL LEVEL  0x43c6ad  flow SKILL ((v + 1) mod 4), then the label
 *                          0x43c671 := ESTEXT 0x35 + v
 *   CANCEL       0x43c728  hide 0x43c349, flow CANCEL (main menu)
 *   ACCEPT       0x43c78b  hide 0x43c349, flow ACCEPT with the field's text
 *                          (new game, page word 0x481108, bay)
 *
 * Showing the screen (0x43c29a) does not reset the skill word 0x476248, the
 * typed name or ACCEPT's state: they stay from the last visit. The dead
 * second screen 0x43b8f0 is not built. Contract:
 * docs/reverse-engineering/vshell_ui.md section 24. No file I/O: the host
 * supplies fonts, the background bitmap and the ESTEXT lookup. */
#include "es2/shell/flow.h"
#include "es2/shell/gui/layout.h"

#define ES2_SCREEN_REGISTRATION_CONTROLS 16 /* registration.00 .. .15 */

/* Controls by layout table index (registration.NN). */
enum {
    ES2_REGISTRATION_BACKGROUND = 0,  /* [0x48d574] ESBitmap, background [0x46de0c] */
    ES2_REGISTRATION_PANEL = 1,       /* [0x48d578] ESTitle, ESTEXT 0x30 */
    ES2_REGISTRATION_BODY = 3,        /* [0x48d57c] ESRegionFill */
    ES2_REGISTRATION_PROMPT = 4,      /* [0x48d580] ESTEXT 0x31 */
    ES2_REGISTRATION_FIELD_FRAME = 5, /* [0x48d584] disabled ESButtonFont around the field */
    ES2_REGISTRATION_FIELD = 7,       /* [0x48d588] ESDialog, set 0x476205 */
    ES2_REGISTRATION_SKILL_VALUE = 8, /* [0x48d58c] disabled ESButtonFont, label = the skill */
    ES2_REGISTRATION_SKILL_LABEL = 9, /* its label (opaque, colour 23) */
    ES2_REGISTRATION_SKILL = 10,      /* [0x48d590] SKILL LEVEL 0x43c6ad */
    ES2_REGISTRATION_CANCEL = 12,     /* [0x48d594] 0x43c728 */
    ES2_REGISTRATION_ACCEPT = 14,     /* [0x48d598] 0x43c78b */
    ES2_REGISTRATION_ACCEPT_LABEL = 15
};

typedef struct es2_screen_registration {
    /* set by the host before es2_screen_registration_build */
    es2_shell_flow *flow;               /* required */
    const es2_dfn *font_button;         /* [0x46ddfc] dfn\font2 (first load) */
    const es2_dfn *font_label;          /* [0x46de00] dfn\font2 (second load) */
    const es2_bitmap *background;       /* [0x46de0c] dbm\bay2a_84 */
    /* 0x4087f0 on [0x46ddf8]: the ESTEXT entry `id` up to its NUL; NULL
     * (or a NULL answer) is the empty text */
    const char *(*estext)(void *context, int id);
    void *estext_context;
    /* filled by the build */
    es2_ui *ui;
    es2_ui_id ids[ES2_SCREEN_REGISTRATION_CONTROLS];
} es2_screen_registration;

/* The constructor 0x43bd2e: the table's controls (hidden or pending), the
 * field's handler 0x43c47e and set 0x476205, ACCEPT disabled with label
 * colour 38, and the skill label 0x43c671 from flow->skill. context is an
 * es2_screen_registration. Returns 0, or -1 (null argument, no flow, the
 * table could not be built). */
int es2_screen_registration_build(es2_ui *ui, void *context);
/* 0x43c29a (START NEW GAME 0x431a08 after the mode store): show the
 * background, the panel and its body; post a left press to the name field
 * (its edit starts when the queue is drained); hover := the field (the
 * leave/enter pair 0x469ffc) and capture := 1. */
void es2_screen_registration_show(es2_screen_registration *r);
/* 0x43c349: hide the three frames. */
void es2_screen_registration_hide(es2_screen_registration *r);
/* The typed name (the field's text +0x45). */
const char *es2_screen_registration_name(const es2_screen_registration *r);

#endif
