#ifndef ES2_SHELL_WEB_SOUND_MIX_H
#define ES2_SHELL_WEB_SOUND_MIX_H

/* The web build's shell sound mixer (docs/web_build.md section 10): the
 * streams of es2_shell_audio_device, each in its sample's own format,
 * mixed into interleaved float stereo at ES2_SOUND_MIX_RATE as SDL3 mixes
 * the streams bound to a device: every stream converted to float (U8
 * (v - 128) / 128, S16 v / 32768), mono copied to both channels, times its
 * gain, the streams summed and the sum clamped to [-1, 1]. A rate other than
 * the device's is resampled linearly (SDL3 resamples with a windowed sinc;
 * the shell's samples have 11,025 or 22,050 Hz). Portable; sound_mix_test runs it
 * natively and under Node. */
#include <stddef.h>
#include <stdint.h>

#define ES2_SOUND_MIX_RATE 22050u
#define ES2_SOUND_MIX_STREAMS 8

typedef struct es2_sound_stream {
    int used;
    uint32_t rate;
    int channels, bits;
    float gain;
    uint8_t *data;        /* queued input bytes [start, length) */
    size_t start, length, capacity;
    uint64_t phase;       /* position of the next output frame in input frames, 32.32 */
} es2_sound_stream;

typedef struct es2_sound_mix {
    es2_sound_stream stream[ES2_SOUND_MIX_STREAMS];
} es2_sound_mix;

/* NULL when every stream is in use or the format is not U8/S16 mono/stereo. */
es2_sound_stream *es2_sound_mix_open(es2_sound_mix *m, uint32_t rate, int channels, int bits);
void es2_sound_mix_close(es2_sound_stream *s);
void es2_sound_mix_clear(es2_sound_stream *s);
/* Appends input bytes; 0 when out of memory. */
int es2_sound_mix_put(es2_sound_stream *s, const uint8_t *data, size_t size);
/* Input bytes not yet mixed (SDL_GetAudioStreamQueued). */
size_t es2_sound_mix_queued(const es2_sound_stream *s);
void es2_sound_mix_gain(es2_sound_stream *s, float gain);
/* Mixes `frames` stereo frames into out[2 * frames]; returns 1 when any
 * stream had input. */
int es2_sound_mix_run(es2_sound_mix *m, float *out, size_t frames);
void es2_sound_mix_free(es2_sound_mix *m);

#endif
