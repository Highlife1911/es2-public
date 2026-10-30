/* The shell sound mixer of the web build (sound_mix.h). */
#include "web/sound_mix.h"

#include <stdlib.h>
#include <string.h>

es2_sound_stream *es2_sound_mix_open(es2_sound_mix *m, uint32_t rate, int channels, int bits) {
    if (!m || !rate || (channels != 1 && channels != 2) || (bits != 8 && bits != 16)) return NULL;
    for (int i = 0; i < ES2_SOUND_MIX_STREAMS; i++) {
        es2_sound_stream *s = &m->stream[i];
        if (s->used) continue;
        memset(s, 0, sizeof *s);
        s->used = 1;
        s->rate = rate;
        s->channels = channels;
        s->bits = bits;
        s->gain = 1.0f;
        return s;
    }
    return NULL;
}

void es2_sound_mix_close(es2_sound_stream *s) {
    if (!s) return;
    free(s->data);
    memset(s, 0, sizeof *s);
}

void es2_sound_mix_clear(es2_sound_stream *s) {
    if (!s) return;
    s->start = s->length = 0;
    s->phase = 0;
}

int es2_sound_mix_put(es2_sound_stream *s, const uint8_t *data, size_t size) {
    if (!s || !s->used) return 0;
    if (!size) return 1;
    if (s->start) {
        memmove(s->data, s->data + s->start, s->length - s->start);
        s->length -= s->start;
        s->start = 0;
    }
    if (s->length + size > s->capacity) {
        size_t capacity = s->capacity ? s->capacity : 4096;
        while (capacity < s->length + size) capacity *= 2;
        uint8_t *grown = realloc(s->data, capacity);
        if (!grown) return 0;
        s->data = grown;
        s->capacity = capacity;
    }
    memcpy(s->data + s->length, data, size);
    s->length += size;
    return 1;
}

size_t es2_sound_mix_queued(const es2_sound_stream *s) { return s && s->used ? s->length - s->start : 0; }

void es2_sound_mix_gain(es2_sound_stream *s, float gain) {
    if (s) s->gain = gain;
}

static float sample(const es2_sound_stream *s, size_t frame, int channel) {
    const uint8_t *p = s->data + s->start + frame * (size_t)(s->channels * s->bits / 8);
    if (s->channels == 1) channel = 0;
    if (s->bits == 8) return (float)((int)p[channel] - 128) / 128.0f;
    int16_t v = (int16_t)(uint16_t)(p[2 * channel] | p[2 * channel + 1] << 8);
    return (float)v / 32768.0f;
}

/* Adds one stream's frames (gain applied) to out; returns 1 when it had input. */
static int add_stream(es2_sound_stream *s, float *out, size_t frames) {
    size_t frame_bytes = (size_t)(s->channels * s->bits / 8);
    size_t available = (s->length - s->start) / frame_bytes;
    uint64_t step = ((uint64_t)s->rate << 32) / ES2_SOUND_MIX_RATE;
    int any = 0;
    for (size_t f = 0; f < frames; f++) {
        size_t i = (size_t)(s->phase >> 32);
        if (i >= available) break;
        float t = (float)(uint32_t)s->phase / 4294967296.0f;
        for (int c = 0; c < 2; c++) {
            float a = sample(s, i, c);
            float v = t != 0.0f && i + 1 < available ? a + (sample(s, i + 1, c) - a) * t : a;
            out[2 * f + c] += v * s->gain;
        }
        s->phase += step;
        any = 1;
    }
    size_t consumed = (size_t)(s->phase >> 32);
    if (consumed > available) consumed = available;
    s->start += consumed * frame_bytes;
    s->phase -= (uint64_t)consumed << 32;
    if (s->start == s->length) {
        s->start = s->length = 0;
        s->phase &= 0xffffffffu;
    }
    return any;
}

int es2_sound_mix_run(es2_sound_mix *m, float *out, size_t frames) {
    int any = 0;
    memset(out, 0, frames * 2 * sizeof *out);
    for (int i = 0; i < ES2_SOUND_MIX_STREAMS; i++)
        if (m->stream[i].used) any |= add_stream(&m->stream[i], out, frames);
    for (size_t i = 0; i < frames * 2; i++) {
        if (out[i] > 1.0f) out[i] = 1.0f;
        else if (out[i] < -1.0f) out[i] = -1.0f;
    }
    return any;
}

void es2_sound_mix_free(es2_sound_mix *m) {
    for (int i = 0; i < ES2_SOUND_MIX_STREAMS; i++) es2_sound_mix_close(&m->stream[i]);
}
