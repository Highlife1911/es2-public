/* The shell's sound device in the web build: the streams of
 * es2_shell_audio_device in the mixer of sound_mix.h, whose output fills
 * the shell sound ring whenever the shell touches a stream, as far as the
 * ring has room (the AudioWorklet drains it in real time), during every
 * wait (es2_web_sound_wait) and on every read of the shell's clock
 * (es2_web_sound_fill). */
#include "web/sound_web.h"
#include "web/sound_mix.h"
#include "es2/shell/gui/sound.h"
#include "es2/platform/os.h"
#include "web/web_imports.h"

static es2_sound_mix mix;

void es2_web_sound_fill(void) {
    static float block[2 * 1024];
    int32_t space = es2_web_sound_space();
    while (space > 0) {
        int32_t frames = space > 1024 ? 1024 : space;
        if (!es2_sound_mix_run(&mix, block, (size_t)frames)) return;
        es2_web_sound_write(block, frames);
        space -= frames;
    }
}

static void *open_stream(void *ctx, uint32_t rate, int channels, int bits) {
    (void)ctx;
    return es2_sound_mix_open(&mix, rate, channels, bits);
}
static void close_stream(void *ctx, void *s) {
    (void)ctx;
    es2_web_sound_fill();
    es2_sound_mix_close(s);
}
static void clear(void *ctx, void *s) {
    (void)ctx;
    es2_web_sound_fill();
    es2_sound_mix_clear(s);
}
static void put(void *ctx, void *s, const uint8_t *data, size_t size) {
    (void)ctx;
    es2_sound_mix_put(s, data, size);
    es2_web_sound_fill();
}
static size_t queued(void *ctx, void *s) {
    (void)ctx;
    es2_web_sound_fill();
    return es2_sound_mix_queued(s);
}
static void set_gain(void *ctx, void *s, float gain) {
    (void)ctx;
    es2_web_sound_fill();
    es2_sound_mix_gain(s, gain);
}

int es2_web_sound_device(void *device) {
    es2_shell_audio_device *out = device;
    if (!out) return 0;
    *out = (es2_shell_audio_device){NULL, open_stream, close_stream, clear, put, queued, set_gain};
    return 1;
}

void es2_web_sound_shutdown(void) { es2_sound_mix_free(&mix); }

/* 20 ms slices: the ring holds about 370 ms at 22,050 Hz. */
int32_t es2_web_sound_wait(uint32_t milliseconds) {
    for (;;) {
        uint32_t step = milliseconds < 20 ? milliseconds : 20;
        es2_web_sound_fill();
        if (es2_web_wait(step)) return 1;
        milliseconds -= step;
        if (!milliseconds) return 0;
    }
}

void es2_web_sound_sleep(uint32_t milliseconds) {
    while (milliseconds) {
        uint32_t step = milliseconds < 20 ? milliseconds : 20;
        es2_web_sound_fill();
        es2_os_sleep(step);
        milliseconds -= step;
    }
    es2_web_sound_fill();
}
