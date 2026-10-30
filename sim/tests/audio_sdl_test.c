/* The SDL3 sink (sim/src/audio_sdl.c) on SDL's dummy audio driver, so no sound
 * card is needed: the open in the caps record's format, writes, the queued
 * count falling as the dummy device consumes, the clear and the close; then the
 * mixing device over that sink: format 29h, one endless synthetic voice,
 * refills as the device plays. No game data. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "es2/sim/audio_sdl.h"
#include "es2/sim/memory.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

#define BASE 0x00100000u
#define SIZE 0x00010000u

static void w16(uint8_t *b, uint32_t a, uint32_t v) { b[a - BASE] = (uint8_t)v; b[a - BASE + 1u] = (uint8_t)(v >> 8); }
static void w32(uint8_t *b, uint32_t a, uint32_t v) { w16(b, a, v); w16(b, a + 2u, v >> 16); }

static es2_sim_audio_sdl sdl;
/* The notices' flags (lParam's low word), in order. */
static uint32_t notices, notice[8];
static int rec_post(void *u, uint32_t window, uint32_t message, uint32_t wparam, uint32_t lparam) {
    (void)u; (void)window; (void)message; (void)wparam;
    if (notices < 8u) notice[notices] = lparam & 0xffffu;
    ++notices;
    return 0;
}
static es2_sim_sos_mixer mixer;

/* Everything the stream can convert now, taken out. */
static void drain(SDL_AudioStream *st) {
    static float out[4096];
    while (SDL_GetAudioStreamData(st, out, (int)sizeof(out)) > 0) {}
}
/* Polls the sink's queued count until it is at most `want` or 5 s pass. */
static uint32_t wait_queued(es2_sim_sos_sink *sink, uint32_t want) {
    uint64_t until = SDL_GetTicks() + 5000u;
    uint32_t q;
    while ((q = sink->queued(sink->context)) > want && SDL_GetTicks() < until) SDL_Delay(5);
    return q;
}

int main(void) {
    CHECK(SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy"));

    /* The tail on a synthetic stream (no device): one 800h-byte buffer of 8-bit
     * unsigned 22,050 Hz stereo into a stream that resamples to 48,000 Hz.
     * Unflushed, everything convertible taken, 14 bytes stay queued (the
     * resampler's 7 padding frames); flushed, none. Without resampling nothing
     * stays. */
    {
        static uint8_t one[0x800];
        memset(one, 0x80, sizeof(one));
        SDL_AudioSpec in = {SDL_AUDIO_U8, 2, 22050}, out = {SDL_AUDIO_F32, 2, 48000}, same_rate = {SDL_AUDIO_F32, 2, 22050};
        SDL_AudioStream *st = SDL_CreateAudioStream(&in, &out);
        CHECK(st != NULL);
        CHECK(SDL_PutAudioStreamData(st, one, (int)sizeof(one)));
        drain(st);
        int tail = SDL_GetAudioStreamQueued(st);
        printf("audio_sdl_test: unflushed tail %d bytes\n", tail);
        CHECK(tail == 14);
        drain(st);
        CHECK(SDL_GetAudioStreamQueued(st) == 14);   /* no further input: it stays */
        CHECK(SDL_FlushAudioStream(st));
        drain(st);
        CHECK(SDL_GetAudioStreamQueued(st) == 0);
        /* Input after a flush plays on; its own tail is held again. */
        CHECK(SDL_PutAudioStreamData(st, one, (int)sizeof(one)));
        drain(st);
        CHECK(SDL_GetAudioStreamQueued(st) == 14);
        SDL_DestroyAudioStream(st);
        st = SDL_CreateAudioStream(&in, &same_rate);
        CHECK(st != NULL);
        CHECK(SDL_PutAudioStreamData(st, one, (int)sizeof(one)));
        drain(st);
        CHECK(SDL_GetAudioStreamQueued(st) == 0);
        SDL_DestroyAudioStream(st);
    }

    es2_sim_sos_sink sink;
    es2_sim_audio_sdl_sink(&sdl, &sink);

    /* The sink alone. */
    static uint8_t buffer[0x800];
    memset(buffer, 0x80, sizeof(buffer));
    CHECK(sink.write(sink.context, buffer, sizeof(buffer)) == 5u);   /* not open */
    CHECK(sink.open(sink.context, 22050u, 2u, 8u) == 0);
    CHECK(sink.open(sink.context, 22050u, 2u, 8u) == 4u);            /* already open */
    CHECK(sink.write(sink.context, buffer, sizeof(buffer)) == 0);
    CHECK(sink.write(sink.context, buffer, sizeof(buffer)) == 0);
    CHECK(sink.queued(sink.context) <= 2u * sizeof(buffer));
    uint64_t until = SDL_GetTicks() + 5000u;
    while (sink.queued(sink.context) >= 2u * sizeof(buffer) && SDL_GetTicks() < until) SDL_Delay(5);
    CHECK(sink.queued(sink.context) < 2u * sizeof(buffer));
    CHECK(sink.write(sink.context, buffer, sizeof(buffer)) == 0);
    sink.reset(sink.context);
    CHECK(sink.queued(sink.context) == 0);
    /* The same tail on the dummy device: one buffer played, 14 bytes stay
     * queued however long the device runs; the sink's flush lets them play. */
    CHECK(sink.write(sink.context, buffer, sizeof(buffer)) == 0);
    CHECK(wait_queued(&sink, 14u) == 14u);
    SDL_Delay(250);
    CHECK(sink.queued(sink.context) == 14u);
    CHECK(sink.flush != NULL);
    sink.flush(sink.context);
    CHECK(wait_queued(&sink, 0) == 0);
    sink.close(sink.context);
    CHECK(sdl.stream == NULL && sdl.subsystem == 0 && sdl.opens == 1u && sdl.writes == 4u);

    /* The mixing device over it. */
    uint8_t *bytes = calloc(1, SIZE);
    CHECK(bytes);
    es2_sim_arena arena;
    memset(&arena, 0, sizeof(arena));
    es2_guest_memory memory;
    CHECK(es2_sim_arena_add(&arena, BASE, bytes, SIZE));
    es2_sim_arena_memory(&arena, &memory);
    es2_vt vt;
    es2_vt_enable(&vt, 1, 47, 0x01000000u, 0, 1);
    es2_sim_audio_sdl_sink(&sdl, &sink);
    es2_sim_sos_mixer_init(&mixer, &vt, &sink);
    es2_sim_audio_device device;
    es2_sim_sos_mixer_device(&mixer, &device);
    const uint32_t caps = BASE, handle = BASE + 0x100u, smp = BASE + 0x200u, data = BASE + 0x1000u;
    w16(bytes, caps + 0x4eu, 0x29u);
    w16(bytes, caps + 0x6cu, 4u);
    w16(bytes, caps + 0x6eu, 0x200u);
    w16(bytes, caps + 0x70u, 1u);
    for (uint32_t k = 0; k < 0x100u; ++k) bytes[data - BASE + k] = (uint8_t)(0x80u + (k & 0x1fu));
    w32(bytes, smp + 0x00u, data);
    w32(bytes, smp + 0x10u, 0x100u);
    w32(bytes, smp + 0x30u, 0x7fff7fffu);
    w32(bytes, smp + 0x34u, 0xffffffffu);
    w32(bytes, smp + 0x38u, 11025u);
    w32(bytes, smp + 0x3cu, 8u);
    w32(bytes, smp + 0x40u, 1u);
    w32(bytes, smp + 0x44u, 0x8000u);
    w32(bytes, smp + 0x48u, 0x8000u);
    uint32_t a[3] = {caps, handle, 0};
    CHECK(device.sos(device.context, &memory, ES2_SOS_INIT_DRIVER, a, 2) == 0);
    a[0] = 0; a[1] = smp; a[2] = handle + 2u;
    CHECK(device.sos(device.context, &memory, ES2_SOS_START_SAMPLE, a, 3) == 0);
    CHECK(sdl.writes == 4u && sdl.write_bytes == 4u * 0x800u);
    a[0] = 0;
    until = SDL_GetTicks() + 5000u;
    while (sdl.writes < 8u && SDL_GetTicks() < until) {
        CHECK(device.sos(device.context, &memory, ES2_SOS_USER_SERVICE, a, 1) == 0);
        SDL_Delay(5);
    }
    CHECK(sdl.writes >= 8u && mixer.done >= 4u && mixer.outstanding <= 4u);
    a[1] = 0x1000u;
    CHECK(device.sos(device.context, &memory, ES2_SOS_RELEASE_DRIVER, a, 2) == 0 && sdl.stream == NULL);
    CHECK(device.sos(device.context, &memory, ES2_SOS_ACQUIRE_DRIVER, a, 1) == 0 && sdl.stream != NULL && sdl.opens == 2u);
    CHECK(device.sos(device.context, &memory, ES2_SOS_UNINIT_DRIVER, a, 1) == 0 && sdl.stream == NULL);

    /* A voice that ends, on the dummy device. The ring runs dry as waveOut's
     * would: the end (4000h) at its mix, the buffer with no voice listed
     * written and the sink flushed, every buffer back (none stays outstanding
     * on the 14-byte tail), the removal (2000h); the next start finds nothing
     * outstanding and primes the whole ring again. */
    w32(bytes, smp + 0x34u, 0u);   /* once */
    a[0] = caps; a[1] = handle;
    CHECK(device.sos(device.context, &memory, ES2_SOS_INIT_DRIVER, a, 2) == 0 && sdl.stream != NULL);
    mixer.post = rec_post;
    a[0] = 0; a[1] = 0x1234u;
    CHECK(device.sos(device.context, &memory, ES2_SOS_SET_ACTIVE_WINDOW, a, 2) == 0);
    uint32_t w0 = sdl.writes;
    a[0] = 0; a[1] = smp; a[2] = handle + 2u;
    CHECK(device.sos(device.context, &memory, ES2_SOS_START_SAMPLE, a, 3) == 0 && sdl.writes == w0 + 4u);
    a[0] = 0;
    until = SDL_GetTicks() + 5000u;
    while ((mixer.outstanding || mixer.list_n) && SDL_GetTicks() < until) {
        CHECK(device.sos(device.context, &memory, ES2_SOS_USER_SERVICE, a, 1) == 0);
        SDL_Delay(5);
    }
    printf("audio_sdl_test: drained: %u outstanding, %u queued, %u listed, %llu flushes, %u notices\n",
           (unsigned)mixer.outstanding, sink.queued(sink.context), (unsigned)mixer.list_n,
           (unsigned long long)mixer.flushes, notices);
    CHECK(mixer.outstanding == 0 && mixer.list_n == 0 && mixer.flushes >= 1u && sink.queued(sink.context) == 0);
    CHECK(notices == 2u && notice[0] == 0x4000u && notice[1] == 0x2000u && mixer.voice[0].flags == 0x2000u);
    w0 = sdl.writes;
    a[0] = 0; a[1] = smp; a[2] = handle + 2u;
    CHECK(device.sos(device.context, &memory, ES2_SOS_START_SAMPLE, a, 3) == 0 && sdl.writes == w0 + 4u);
    a[0] = 0;
    CHECK(device.sos(device.context, &memory, ES2_SOS_UNINIT_DRIVER, a, 1) == 0 && sdl.stream == NULL);
    free(bytes);
    printf("audio_sdl_test: passed (%u writes on the dummy driver)\n", sdl.writes);
    return 0;
}
