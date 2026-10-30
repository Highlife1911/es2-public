#ifndef ES2_SHELL_SOUNDS_H
#define ES2_SHELL_SOUNDS_H
/* The shell's sound logic (docs/reverse-engineering/audio.md, "the VSHELL
 * shell audio contract": "Events and effects", "Start-up sequence and the
 * music choice", "What an own shell must reproduce"): which sound plays on
 * which event, the gates (sound flag, prefs byte 1, sound focus), the music choice
 * and prefs byte 5 flip at start-up, focus loss/gain and the "Music" volume
 * ramp. No I/O and no audio: every effect goes to the player callbacks
 * (es2/shell/gui/sound.h implements them with SDL3; tests use a fake). */
#include "es2/shell/campaign.h"

#include <stdint.h>

/* The four samples VSHELL opens (0x42f3c8..0x42f44f). */
enum {
    ES2_SHELL_SOUND_GM_69 = 0,   /* [0x48ceac] button press */
    ES2_SHELL_SOUND_BPTLT2,      /* [0x48ceb4] bay-bar screen change */
    ES2_SHELL_SOUND_LSWITCH2,    /* [0x48ceb8] main-menu background, once */
    ES2_SHELL_SOUND_MUSIC,       /* [0x48ceb0] hmi\shell1.wav or hmi\shell2.wav */
    ES2_SHELL_SOUND_COUNT
};
enum { ES2_SHELL_PREFS_MUSIC = 0, ES2_SHELL_PREFS_EFFECTS = 1, ES2_SHELL_PREFS_MUSIC_CHOICE = 5 };

/* The player the logic drives (one voice per sample). */
typedef struct es2_shell_sound_player {
    void *ctx;
    /* Start the sample from its beginning at volume 0..100; loop != 0 loops endlessly. */
    void (*play)(void *ctx, int sound, int volume, int loop);
    /* Volume 0..100 of a sample (applies at once while it plays). */
    void (*set_volume)(void *ctx, int sound, int volume);
    /* Stop every sample at once (SFX::stopAll 0x42dcf9). */
    void (*stop_all)(void *ctx);
} es2_shell_sound_player;

typedef struct es2_shell_sounds {
    int active;          /* [0x473338] != 0: the sound flag was set at start-up */
    int focus;           /* [0x473348] */
    int music_volume;    /* [0x473344], 0..100 */
    int lswitch_done;    /* [0x473750] */
    const char *music;   /* "hmi\\shell1.wav" or "hmi\\shell2.wav", NULL without sound */
    es2_shell_sound_player player;
} es2_shell_sounds;

/* The VFS paths of the samples (music as chosen at start-up). */
const char *es2_shell_sound_path(const es2_shell_sounds *s, int sound);

/* Start-up 0x42f2f4. sound_flag 0 (-s/-v/-?): nothing, returns 0. Otherwise
 * the music is chosen by prefs byte 5 (!= 0 shell1, = 0 shell2), byte 5 :=
 * byte 5 xor 1, the whole block is committed and `out` receives the 54
 * bytes VSHELL writes to data\prefs.cfg; returns 1 (the caller writes). The
 * music volume starts at 0. */
int es2_shell_sounds_startup(es2_shell_sounds *s, const es2_shell_sound_player *player, int sound_flag,
                             es2_shell_prefs_session *prefs, uint8_t out[ES2_SHELL_PREFS_SIZE]);

/* Focus gained 0x42f5d3: when active and not focused: focus := 1, the music
 * starts looping from its beginning at the stored volume (the inaudible
 * gm_69 play at volume 0 is left out, audio.md, "What an own shell must
 * reproduce"). */
void es2_shell_sounds_focus_gain(es2_shell_sounds *s);
/* Focus lost 0x42f6a8: when active and focused: stop everything, focus := 0. */
void es2_shell_sounds_focus_lose(es2_shell_sounds *s);

/* The three effects 0x42f547/0x42f501/0x42f58d: played at volume 100 when
 * active, prefs byte 1 != 0 and focused. Returns 1 when played. */
int es2_shell_sounds_effect(es2_shell_sounds *s, const uint8_t prefs[ES2_SHELL_PREFS_SIZE], int sound);

/* Button classes and their press gates (audio.md, "Events and effects"). */
enum {
    ES2_SHELL_BUTTON_TEXT = 0,   /* vtable 0x46e9b0, handler 0x40a0bf */
    ES2_SHELL_BUTTON_IMAGE,      /* vtable 0x46ea5c, handler 0x40a3a2 */
    ES2_SHELL_BUTTON_IMAGE_SUB   /* vtable 0x46ead8, handler 0x40a6e9 */
};
typedef struct es2_shell_button_press {
    int button_class;
    int press;         /* event byte +0x24: 2 left press, 4 right press (1/3 releases) */
    int enabled;       /* button +0x49 */
    uint32_t extra;    /* event dword +0x25 (text buttons only) */
    int movie_open;    /* [0x470eb0] (image buttons only) */
    int movie_sequence;/* [0x470fb0] (image buttons only) */
} es2_shell_button_press;
/* gm_69 for a press event of type 0x20 when the class's gate holds. */
int es2_shell_sounds_button(es2_shell_sounds *s, const uint8_t prefs[ES2_SHELL_PREFS_SIZE],
                            const es2_shell_button_press *press);
/* Bay bar 0x43a66c..0x43ad5a: bptlt2 only when the screen word changed
 * (old != new), called after the new screen was built. */
int es2_shell_sounds_bay_change(es2_shell_sounds *s, const uint8_t prefs[ES2_SHELL_PREFS_SIZE],
                                int old_screen, int new_screen);
/* Main-menu background callback 0x431830: lswitch2 on the first call of the
 * process only; later calls do nothing (the flag is set whether or not the
 * gate let it sound). */
int es2_shell_sounds_background_step(es2_shell_sounds *s, const uint8_t prefs[ES2_SHELL_PREFS_SIZE]);

/* Music ramps 0x42f894 (in) and 0x42f7f0 (out). es2_shell_sounds_ramp_begin
 * returns 0 when the ramp does not run (not active or prefs byte 0 = 0).
 * The caller then loops: pump messages (a nonzero pump ends the loop), and
 * call es2_shell_sounds_ramp_tick with GetTickCount-like milliseconds until
 * it returns 0. Each call that sees more than 10 ms since the last step
 * moves the volume by one; fade-in ends at 100, fade-out at 1 (not 0). */
typedef struct es2_shell_sound_ramp { int up; uint32_t last; } es2_shell_sound_ramp;
int es2_shell_sounds_ramp_begin(const es2_shell_sounds *s, const uint8_t prefs[ES2_SHELL_PREFS_SIZE], int up,
                                uint32_t now, es2_shell_sound_ramp *ramp);
int es2_shell_sounds_ramp_tick(es2_shell_sounds *s, es2_shell_sound_ramp *ramp, uint32_t now);
#endif
