#ifndef ES2_SHELL_GUI_APP_SOUND_H
#define ES2_SHELL_GUI_APP_SOUND_H

/* The graphical shell's sounds (es2/shell/sounds.h over
 * es2/shell/gui/sound.h) behind a small interface: sounds.h and
 * gui/text.h both name ES2_SHELL_BUTTON_*, so app.c (which needs the
 * widgets) does not include them. */
#include "es2/formats/vfs.h"
#include "es2/shell/campaign.h"
#include <stdint.h>

typedef struct es2_app_sound es2_app_sound;
enum { ES2_APP_SOUND_BUTTON = 0, ES2_APP_SOUND_BAY = 1 };

es2_app_sound *es2_app_sound_create(void);
void es2_app_sound_destroy(es2_app_sound *s);
/* Start-up 0x42f2f4 with the sound flag; with `device_open` (the SDL device,
 * NULL headless) the samples of `vfs` are opened after the music choice.
 * Returns 1 when prefs `out` must be written. */
int es2_app_sound_startup(es2_app_sound *s, int sound_flag, int (*device_open)(void *device), const es2_vfs *vfs,
                          es2_shell_prefs_session *prefs, uint8_t out[ES2_SHELL_PREFS_SIZE]);
/* gm_69 (0x42f547) or bptlt2 (0x42f501) under the gates. */
void es2_app_sound_effect(es2_app_sound *s, const uint8_t prefs[ES2_SHELL_PREFS_SIZE], int which);
/* The main menu's background callback 0x431830: lswitch2 once. */
void es2_app_sound_background(es2_app_sound *s, const uint8_t prefs[ES2_SHELL_PREFS_SIZE]);
void es2_app_sound_focus(es2_app_sound *s, int gained);
/* A music ramp (0x42f894 up, 0x42f7f0 down) run to its end on `clock`
 * (NULL: a virtual 11 ms per pass, no idle); `pump` (the shell's
 * pump, VSHELL's 0x406328) runs before every volume test; its nonzero
 * return ends the fade and is returned. */
int es2_app_sound_ramp(es2_app_sound *s, int up, const uint8_t prefs[ES2_SHELL_PREFS_SIZE],
                       uint32_t (*clock)(void *context), void *context, int (*pump)(void *context),
                       void *pump_context);
/* Keeps the looping voices fed (each frame). */
void es2_app_sound_service(es2_app_sound *s);
/* The headless device: every voice opens, nothing is output, so the
 * samples are found and every play is logged as in the window. */
int es2_app_sound_null_device(void *device);
/* One line per start-up, play and stop for the session log (shell.log);
 * set before es2_app_sound_startup. */
void es2_app_sound_set_log(es2_app_sound *s, void (*log)(void *ctx, const char *line), void *ctx);

#endif
