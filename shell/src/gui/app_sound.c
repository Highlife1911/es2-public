/* The graphical shell's sounds (gui/app_sound.h) over es2/shell/sounds.h. */
#include "gui/app_sound.h"
#include "es2/shell/gui/sound.h"
#include "es2/shell/sounds.h"

#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct es2_app_sound {
    es2_shell_sounds sounds;
    es2_shell_audio audio;
    es2_shell_sound_player inner;   /* the audio player the logging player forwards to */
    int open, device, loaded;
    void (*log)(void *ctx, const char *line);
    void *log_ctx;
};

es2_app_sound *es2_app_sound_create(void) { return calloc(1, sizeof(es2_app_sound)); }

void es2_app_sound_destroy(es2_app_sound *s) {
    if (!s) return;
    if (s->open) es2_shell_audio_close(&s->audio);
    free(s);
}

/* ---- The session log of the sounds (shell.log) --------------------------------------- */
static void say(es2_app_sound *s, const char *format, ...) {
    char line[256];
    va_list args;
    if (!s->log) return;
    va_start(args, format);
    vsnprintf(line, sizeof line, format, args);
    va_end(args);
    s->log(s->log_ctx, line);
}

static const char *name_of(const es2_app_sound *s, int sound) {
    const char *path = es2_shell_sound_path(&s->sounds, sound);
    return path ? path : "?";
}

static void log_play(void *ctx, int sound, int volume, int loop) {
    es2_app_sound *s = ctx;
    int loaded = sound >= 0 && sound < ES2_SHELL_SOUND_COUNT && s->open && s->audio.loaded[sound];
    say(s, "sound play %s volume %d%s%s\n", name_of(s, sound), volume, loop ? " loop" : "", loaded ? "" : " (not loaded)");
    if (s->inner.play) s->inner.play(s->inner.ctx, sound, volume, loop);
}
static void log_volume(void *ctx, int sound, int volume) {
    es2_app_sound *s = ctx;
    if (s->inner.set_volume) s->inner.set_volume(s->inner.ctx, sound, volume);
}
static void log_stop(void *ctx) {
    es2_app_sound *s = ctx;
    say(s, "sound stop all\n");
    if (s->inner.stop_all) s->inner.stop_all(s->inner.ctx);
}

void es2_app_sound_set_log(es2_app_sound *s, void (*log)(void *ctx, const char *line), void *ctx) {
    if (!s) return;
    s->log = log;
    s->log_ctx = ctx;
}

int es2_app_sound_startup(es2_app_sound *s, int sound_flag, int (*device_open)(void *device), const es2_vfs *vfs,
                          es2_shell_prefs_session *prefs, uint8_t out[ES2_SHELL_PREFS_SIZE]) {
    es2_shell_sound_player silent = {0}, logged = {s, log_play, log_volume, log_stop};
    s->inner = device_open ? es2_shell_audio_player(&s->audio) : silent;
    int write = es2_shell_sounds_startup(&s->sounds, &logged, sound_flag, prefs, out);
    if (s->sounds.active && device_open) {
        es2_shell_audio_device device;
        memset(&device, 0, sizeof device);
        if (device_open(&device)) {
            s->device = 1;
            /* `vfs` must hold SHLSOUND.VOL (audio.md, "Files and playback
             * format"); a sample not found stays silent, as VSHELL's failed
             * SFX::cache */
            s->loaded = es2_shell_audio_open(&s->audio, &device, vfs, &s->sounds);
            s->open = 1;
        }
    }
    say(s, "sound start-up: flag %d active %d music %s device %s samples %d of %d\n", sound_flag != 0,
        s->sounds.active, s->sounds.music ? s->sounds.music : "-",
        !device_open || !s->sounds.active ? "none" : s->device ? "open" : "failed", s->loaded, (int)ES2_SHELL_SOUND_COUNT);
    return write;
}

void es2_app_sound_effect(es2_app_sound *s, const uint8_t prefs[ES2_SHELL_PREFS_SIZE], int which) {
    es2_shell_sounds_effect(&s->sounds, prefs, which == ES2_APP_SOUND_BAY ? ES2_SHELL_SOUND_BPTLT2 : ES2_SHELL_SOUND_GM_69);
}

void es2_app_sound_background(es2_app_sound *s, const uint8_t prefs[ES2_SHELL_PREFS_SIZE]) {
    es2_shell_sounds_background_step(&s->sounds, prefs);
}

void es2_app_sound_focus(es2_app_sound *s, int gained) {
    if (gained) es2_shell_sounds_focus_gain(&s->sounds);
    else es2_shell_sounds_focus_lose(&s->sounds);
}

int es2_app_sound_ramp(es2_app_sound *s, int up, const uint8_t prefs[ES2_SHELL_PREFS_SIZE],
                       uint32_t (*clock)(void *context), void *context, int (*pump)(void *context),
                       void *pump_context) {
    /* Original: VSHELL.EXE 0x42f894 (up) and 0x42f7f0 (down): each
     * pass runs the Windows pump 0x406328 first (0x42f8e9 / 0x42f86f; a
     * nonzero return, WM_QUIT, ends the fade at once), then tests the volume
     * (up: < 100 at 0x42f8f2..0x42f8fa; down: > 1 at 0x42f878..0x42f87e),
     * then steps it when GetTickCount advanced by more than 10 ms
     * (0x42f8b7..0x42f8e1). The pump only dispatches to the window
     * procedure: clicks and keys are queued (button messages, key ring) and
     * reach the screen after the fade; paints, focus and close are handled
     * as in the main loop. */
    es2_shell_sound_ramp r;
    uint32_t now = clock ? clock(context) : 0;
    if (!s || !s->open || !es2_shell_sounds_ramp_begin(&s->sounds, prefs, up, now, &r)) return 0;
    int quit = 0;
    for (int guard = 0; guard < 100000; guard++) {
        if (pump && pump(pump_context)) {
            quit = 1;
            break;
        }
        if (up ? s->sounds.music_volume >= 100 : s->sounds.music_volume <= 1) break;
        now = clock ? clock(context) : now + 11;
        es2_shell_sounds_ramp_tick(&s->sounds, &r, now);
        es2_shell_audio_service(&s->audio);
        if (!clock) continue;
        Sleep(1);   /* own: VSHELL spins between steps */
    }
    say(s, "sound music volume %d%s\n", s->sounds.music_volume, quit ? " (fade ended by quit)" : "");
    return quit;
}

void es2_app_sound_service(es2_app_sound *s) {
    if (s->open) es2_shell_audio_service(&s->audio);
}

int es2_app_sound_sdl_device(void *device) { return es2_shell_audio_sdl_device(device); }
void es2_app_sound_sdl_shutdown(void) { es2_shell_audio_sdl_shutdown(); }

/* ---- The headless device (accepts every voice, outputs nothing) ----------------------- */
static int null_token;
static void *null_open(void *ctx, uint32_t rate, int channels, int bits) {
    (void)ctx; (void)rate; (void)channels; (void)bits;
    return &null_token;
}
static void null_close(void *ctx, void *stream) { (void)ctx; (void)stream; }
static void null_clear(void *ctx, void *stream) { (void)ctx; (void)stream; }
static void null_put(void *ctx, void *stream, const uint8_t *data, size_t size) {
    (void)ctx; (void)stream; (void)data; (void)size;
}
static size_t null_queued(void *ctx, void *stream) { (void)ctx; (void)stream; return 0; }
static void null_gain(void *ctx, void *stream, float gain) { (void)ctx; (void)stream; (void)gain; }

int es2_app_sound_null_device(void *device) {
    if (!device) return 0;
    *(es2_shell_audio_device *)device =
        (es2_shell_audio_device){NULL, null_open, null_close, null_clear, null_put, null_queued, null_gain};
    return 1;
}
