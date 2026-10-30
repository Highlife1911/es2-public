#ifndef ES2_SHELL_GUI_HEADLESS_H
#define ES2_SHELL_GUI_HEADLESS_H

/* The headless platform: no window, window messages from a script,
 * frames to memory and one record per present with SHA-256 hashes of the
 * indexed frame, the uploaded colour table and the screen. Every test of
 * the graphical shell runs on it (the screen harness builds on it).
 *
 * Script: one command per line, '#' starts a comment. Messages queue up and
 * are delivered by the pump of the next `step`:
 *   move X Y | ldown | lup | rdown | rup        mouse messages at frame (X, Y);
 *                                                MK_ bits follow the held buttons
 *   key NAME [shift] [alt] [ctrl]               key down then key up
 *   keydown NAME [...] | keyup NAME [...]       NAME: esc enter tab backspace space
 *                                                up down left right home end pgup
 *                                                pgdn ins del f1..f10 a..z 0..9
 *                                                ctrl alt caps numlock scroll
 *                                                kpminus kpplus clear, or vk:0xNN
 *   focus 0|1 | paint | close | quit            WM_KILLFOCUS/SETFOCUS, WM_PAINT,
 *                                                WM_CLOSE, WM_QUIT
 *   step [N]                                    N main-loop passes (default 1)
 * Shell-side actions (run at once, between passes, as code of a screen would):
 *   interactive 0|1 | movie 0|1 | gate 0|1 | busy 0|1 | fullscreen 0|1
 *   pattern                                     es2_gui_test_pattern
 *   fill L T R B INDEX                          draw a rectangle and invalidate it (slot 0)
 *   invalidate L T R B                          slot 1
 *   palette SEED                                select a synthetic palette
 *   click TARGET                                synthetic click (0x413305's posts)
 *   clear | present
 * Report lines:
 *   event PASS type=0xTTT code=0xCCC x=X y=Y synthetic=S target=T
 *   present SEQ pass=PASS kind=dirty|full|clear rects=N frame=SHA table=SHA screen=SHA
 *   system PASS quit_box|leave_fullscreen|toggle_fullscreen
 *   cursor busy|arrow / fullscreen 0|1 / error LINE text / end passes=N presents=N quit=Q
 * frame = the 307,200 indices; table = 768 bytes, per entry blue, green, red
 * (the DIB table without its reserved byte); screen = the 640 x 480
 * 0x00RRGGBB pixels as little-endian dwords. */
#include "es2/shell/gui/loop.h"

#include <stddef.h>

typedef struct es2_gui_headless_result {
    char *report;      /* malloc'd, NUL-terminated */
    size_t length;
    int errors;        /* script lines that could not be parsed */
    int passes;
} es2_gui_headless_result;

/* Runs the script on a fresh shell (args may be NULL). hooks->handle, if
 * set, is called after the event is reported. Returns 0 on success, -1 when
 * memory ran out. The caller frees result->report. */
int es2_gui_headless_run(const char *script, const es2_gui_args *args, const es2_gui_hooks *hooks,
                         es2_gui_headless_result *result);

/* The hashes a present record carries. */
void es2_gui_hash_frame(const es2_gui_frame *frame, char hex[65]);
void es2_gui_hash_table(const es2_gui_frame *frame, char hex[65]);
void es2_gui_hash_screen(const es2_gui_screen *screen, char hex[65]);

/* Virtual-key code of a script key name, 0 = unknown. */
unsigned es2_gui_key_by_name(const char *name);

#endif
