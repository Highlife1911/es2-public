/* The web shell's mixer (src/web/sound_mix.h): conversion, gain, sum and
 * clamp, resampling and the queue. */
#include "web/sound_mix.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                                \
        }                                                              \
    } while (0)

static int near(float a, float b) { return fabsf(a - b) < 1e-6f; }

static void conversion(void) {
    es2_sound_mix m;
    memset(&m, 0, sizeof m);
    es2_sound_stream *u8 = es2_sound_mix_open(&m, 22050, 1, 8);
    CHECK(u8 != NULL);
    const uint8_t bytes[4] = {0, 128, 255, 192};
    CHECK(es2_sound_mix_put(u8, bytes, sizeof bytes));
    CHECK(es2_sound_mix_queued(u8) == 4);
    float out[10];
    CHECK(es2_sound_mix_run(&m, out, 5) == 1);
    CHECK(near(out[0], -1.0f) && near(out[1], -1.0f));
    CHECK(near(out[2], 0.0f) && near(out[3], 0.0f));
    CHECK(near(out[4], 127.0f / 128.0f) && near(out[5], 127.0f / 128.0f));
    CHECK(near(out[6], 0.5f));
    CHECK(out[8] == 0.0f && out[9] == 0.0f);
    CHECK(es2_sound_mix_queued(u8) == 0);
    CHECK(es2_sound_mix_run(&m, out, 5) == 0);

    es2_sound_stream *s16 = es2_sound_mix_open(&m, 22050, 2, 16);
    const uint8_t pcm[4] = {0x00, 0x80, 0x00, 0x40};   /* -32768, 16384 */
    CHECK(es2_sound_mix_put(s16, pcm, sizeof pcm));
    CHECK(es2_sound_mix_run(&m, out, 1) == 1);
    CHECK(near(out[0], -1.0f) && near(out[1], 0.5f));
    CHECK(es2_sound_mix_open(&m, 22050, 3, 8) == NULL);
    CHECK(es2_sound_mix_open(&m, 22050, 1, 24) == NULL);
    es2_sound_mix_free(&m);
}

static void sum_and_clamp(void) {
    es2_sound_mix m;
    memset(&m, 0, sizeof m);
    es2_sound_stream *a = es2_sound_mix_open(&m, 22050, 1, 8);
    es2_sound_stream *b = es2_sound_mix_open(&m, 22050, 1, 8);
    const uint8_t loud[2] = {224, 32};   /* 0.75, -0.75 */
    es2_sound_mix_put(a, loud, 2);
    es2_sound_mix_put(b, loud, 2);
    float out[4];
    es2_sound_mix_run(&m, out, 2);
    CHECK(out[0] == 1.0f && out[2] == -1.0f);
    es2_sound_mix_put(a, loud, 2);
    es2_sound_mix_put(b, loud, 2);
    es2_sound_mix_gain(b, 0.5f);
    es2_sound_mix_run(&m, out, 2);
    CHECK(out[0] == 1.0f && near(out[2], -1.0f));
    es2_sound_mix_put(a, loud, 2);
    es2_sound_mix_put(b, loud, 2);
    es2_sound_mix_gain(a, 0.0f);
    es2_sound_mix_run(&m, out, 2);
    CHECK(near(out[0], 0.375f) && near(out[2], -0.375f));
    es2_sound_mix_put(a, loud, 2);
    es2_sound_mix_clear(a);
    CHECK(es2_sound_mix_queued(a) == 0);
    for (int i = 2; i < ES2_SOUND_MIX_STREAMS; i++) CHECK(es2_sound_mix_open(&m, 11025, 1, 8) != NULL);
    CHECK(es2_sound_mix_open(&m, 11025, 1, 8) == NULL);
    es2_sound_mix_close(b);
    CHECK(es2_sound_mix_open(&m, 11025, 1, 8) != NULL);
    es2_sound_mix_free(&m);
}

static void resampling(void) {
    es2_sound_mix m;
    memset(&m, 0, sizeof m);
    es2_sound_stream *s = es2_sound_mix_open(&m, 11025, 1, 8);
    const uint8_t ramp[3] = {128, 192, 128};   /* 0, 0.5, 0 */
    es2_sound_mix_put(s, ramp, 3);
    float out[16];
    memset(out, 0, sizeof out);
    es2_sound_mix_run(&m, out, 8);
    CHECK(near(out[0], 0.0f) && near(out[2], 0.25f) && near(out[4], 0.5f));
    CHECK(near(out[6], 0.25f) && near(out[8], 0.0f));
    CHECK(out[10] == 0.0f);
    CHECK(es2_sound_mix_queued(s) == 0);
    /* split runs continue at the same phase */
    es2_sound_mix_put(s, ramp, 3);
    es2_sound_mix_run(&m, out, 1);
    CHECK(near(out[0], 0.0f));
    es2_sound_mix_run(&m, out, 1);
    CHECK(near(out[0], 0.25f));
    CHECK(es2_sound_mix_queued(s) == 2);
    es2_sound_mix_free(&m);
}

int main(void) {
    conversion();
    sum_and_clamp();
    resampling();
    if (failures) return 1;
    puts("sound_mix_test: ok");
    return 0;
}
