/* The SOS mixing device (sim/src/audio.c, audio.md, "The mixing device") on
 * synthetic voices: a host arena holding the caps record, the handle words, the
 * sample structures and their data, all written by this test, and a recording
 * sink of this test in place of the waveOut device (no sound card). Every
 * expectation is computed by hand from the arithmetic the contract describes.
 * The resample DIV's fault ends the run through a recording process record.
 * No game data. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "es2/sim/audio.h"
#include "es2/sim/loop.h"
#include "es2/sim/memory.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

#define BASE 0x00100000u
#define SIZE 0x00010000u
#define CAPS (BASE + 0x0000u)
#define HDRV (BASE + 0x0100u)   /* the driver handle word */
#define HSMP (BASE + 0x0110u)   /* a sample handle word */
#define SMP(i) (BASE + 0x1000u + 0x80u * (uint32_t)(i))
#define DATA(i) (BASE + 0x4000u + 0x100u * (uint32_t)(i))

static uint8_t *bytes;
static es2_sim_arena arena;
static es2_guest_memory memory;
static es2_vt vt;
static es2_sim_sos_mixer mixer;
static es2_sim_audio_device device;

typedef struct fake {
    uint32_t opens, closes, resets, writes, fail_open;
    uint32_t rate, channels, bits;
    uint32_t queued;            /* bytes not yet "played" */
    uint32_t hold, flushed, flushes;   /* a resampling stream's held tail */
    uint8_t last[0x1000];
    uint32_t last_n;
} fake;
static fake sink;

static uint32_t f_open(void *c, uint32_t rate, uint32_t channels, uint32_t bits) {
    fake *f = c;
    if (f->fail_open) return f->fail_open;
    ++f->opens; f->rate = rate; f->channels = channels; f->bits = bits;
    return 0;
}
static uint32_t f_write(void *c, const uint8_t *d, uint32_t n) {
    fake *f = c;
    ++f->writes;
    f->last_n = n;
    memcpy(f->last, d, n < sizeof(f->last) ? n : sizeof(f->last));
    f->queued += n;
    f->flushed = 0;
    return 0;
}
static uint32_t f_queued(void *c) { return ((fake *)c)->queued; }
static void f_reset(void *c) { ++((fake *)c)->resets; ((fake *)c)->queued = 0; }
static void f_close(void *c) { ++((fake *)c)->closes; }
static void f_flush(void *c) { ++((fake *)c)->flushes; ((fake *)c)->flushed = 1; }
/* The device takes what the stream can convert: everything once flushed, else
 * all but the last `hold` bytes (SDL keeps the resampler's padding frames of
 * the newest input: 14 bytes, audio_sdl_test). */
static void play(void) { sink.queued = sink.flushed ? 0 : sink.queued < sink.hold ? sink.queued : sink.hold; }

/* A recording PostMessageA and IsWindow (0xdead is no window). */
typedef struct posts {
    uint32_t n, fail;
    uint32_t window[8], message[8], wparam[8], lparam[8];
} posts;
static posts rec;
static int rec_post(void *u, uint32_t window, uint32_t message, uint32_t wparam, uint32_t lparam) {
    posts *p = u;
    if (p->n < 8u) {
        p->window[p->n] = window; p->message[p->n] = message; p->wparam[p->n] = wparam; p->lparam[p->n] = lparam;
    }
    ++p->n;
    return p->fail ? 1 : 0;
}
static int rec_is_window(void *u, uint32_t window) { (void)u; return window != 0xdeadu; }

/* A process record that records the end instead of ending the process. */
static struct { unsigned n; uint32_t code, site; } ended;
static void rec_fault(void *u, uint32_t code, uint32_t site) { (void)u; ++ended.n; ended.code = code; ended.site = site; }
static es2_sim_process_platform recorder = {NULL, NULL, NULL, NULL, rec_fault};
static es2_sim_loop loop;
static void bind_recorder(void) {
    es2_sim_loop_defaults(&loop);
    loop.memory = &memory;
    loop.process = &recorder;
    es2_sim_loop_bind(&loop);
    memset(&ended, 0, sizeof(ended));
}

static void w8(uint32_t a, uint8_t v) { bytes[a - BASE] = v; }
static void w16(uint32_t a, uint32_t v) { w8(a, (uint8_t)v); w8(a + 1u, (uint8_t)(v >> 8)); }
static void w32(uint32_t a, uint32_t v) { w16(a, v); w16(a + 2u, v >> 16); }
static uint32_t r16(uint32_t a) { return bytes[a - BASE] | ((uint32_t)bytes[a - BASE + 1u] << 8); }

static uint32_t call(es2_sim_sos_call k, uint32_t a0, uint32_t a1, uint32_t a2) {
    uint32_t a[3] = {a0, a1, a2};
    return device.sos(device.context, &memory, k, a, 3);
}
/* A fresh mixer over the recording sink with caps format, buffer count and
 * block word. */
static uint32_t setup(uint32_t format, uint32_t buffers, uint32_t block) {
    memset(bytes, 0, SIZE);
    memset(&sink, 0, sizeof(sink));
    es2_sim_sos_sink s = {&sink, f_open, f_write, f_queued, f_reset, f_close, f_flush};
    es2_sim_sos_mixer_init(&mixer, &vt, &s);
    es2_sim_sos_mixer_device(&mixer, &device);
    w16(CAPS + 0x4eu, format);
    w16(CAPS + 0x6cu, buffers);
    w16(CAPS + 0x6eu, block);
    w16(CAPS + 0x70u, 1u);
    w16(HDRV, 0xffffu);
    return call(ES2_SOS_INIT_DRIVER, CAPS, HDRV, 0);
}
/* A sample structure as DBSIM's sample load writes it: +0 data, +10h
 * length, +30h volume, +34h loops - 1, +38h rate, +3Ch bits, +40h channels,
 * +44h 8000h for 8-bit (unsigned), +48h pan. */
static void sample(unsigned i, const uint8_t *d, uint32_t n, uint32_t volume, uint32_t loops, uint32_t rate,
                   uint32_t bits, uint32_t channels, uint32_t pan) {
    for (uint32_t k = 0; k < n; ++k) w8(DATA(i) + k, d[k]);
    w32(SMP(i) + 0x00u, DATA(i));
    w32(SMP(i) + 0x10u, n);
    w32(SMP(i) + 0x30u, volume);
    w32(SMP(i) + 0x34u, loops);
    w32(SMP(i) + 0x38u, rate);
    w32(SMP(i) + 0x3cu, bits);
    w32(SMP(i) + 0x40u, channels);
    w32(SMP(i) + 0x44u, bits == 8u ? 0x8000u : 0u);
    w32(SMP(i) + 0x48u, pan);
}
static uint32_t start(unsigned i) { return call(ES2_SOS_START_SAMPLE, 0, SMP(i), HSMP); }
static int same(const uint8_t *want, uint32_t n) {
    if (sink.last_n != n || memcmp(sink.last, want, n)) {
        fprintf(stderr, "  got:");
        for (uint32_t k = 0; k < sink.last_n && k < 32u; ++k) fprintf(stderr, " %02x", sink.last[k]);
        fprintf(stderr, "\n");
        return 0;
    }
    return 1;
}

#define CENTRE 0x8000u
#define FULL 0x7fff7fffu
#define ONCE 0u
#define ENDLESS 0xffffffffu

int main(void) {
    bytes = calloc(1, SIZE);
    CHECK(bytes);
    es2_vt_enable(&vt, 1, 47, 0x01000000u, 0, 1);
    CHECK(es2_sim_arena_add(&arena, BASE, bytes, SIZE));
    es2_sim_arena_memory(&arena, &memory);
    unsigned cases = 0;

    /* The format decode: 29h 22050 Hz stereo 8-bit unsigned, the block word
     * * 2 (rate) * 2 (stereo); 15h 11025 Hz mono 8-bit; 2Ah 16-bit stereo
     * 22050 Hz (* 8); no channel bits: 320020h; an open failure: 320000h |
     * MMRESULT. The handle word is written 0. */
    CHECK(setup(0x29u, 4, 0x200u) == 0);
    CHECK(sink.opens == 1 && sink.rate == 22050u && sink.channels == 2u && sink.bits == 8u);
    CHECK(mixer.block == 0x800u && mixer.buffers == 4u && mixer.out_flags == 0x8000u && r16(HDRV) == 0);
    CHECK(setup(0x15u, 2, 0x200u) == 0);
    CHECK(sink.rate == 11025u && sink.channels == 1u && sink.bits == 8u && mixer.block == 0x200u);
    CHECK(setup(0x2au, 2, 0x200u) == 0);
    CHECK(sink.rate == 22050u && sink.channels == 2u && sink.bits == 16u && mixer.block == 0x1000u && mixer.out_flags == 0);
    CHECK(setup(0x21u, 2, 0x200u) == 0x320020u && sink.opens == 0);
    memset(&sink, 0, sizeof(sink));
    sink.fail_open = 4;
    w16(CAPS + 0x4eu, 0x15u);
    CHECK(call(ES2_SOS_INIT_DRIVER, CAPS, HDRV, 0) == 0x320004u);
    cases += 5;

    {   /* Unscaled, centre pan: an 8-bit mono source to stereo output, both
         * channels the byte; after the data the accumulators stay 0 (80h).
         * The priming mixes the one buffer at the start; +4Ch is 1 from the
         * mix and 2 after the start's own increment. samples: the
         * accumulator index, 8. */
        static const uint8_t d[] = {0x90, 0x70, 0x80, 0xff};
        static const uint8_t want[] = {0x90, 0x90, 0x70, 0x70, 0x80, 0x80, 0xff, 0xff,
                                       0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80};
        CHECK(setup(0x29u, 1, 4) == 0);
        sample(0, d, 4, FULL, ONCE, 22050u, 8, 1, CENTRE);
        CHECK(start(0) == 0 && r16(HSMP) == 0);
        CHECK(sink.writes == 1 && same(want, 16));
        CHECK(mixer.count == 2u && mixer.voice[0].samples == 8u && (mixer.voice[0].flags & 0x4000u));
        CHECK(call(ES2_SOS_SAMPLE_DONE, 0, 0, 0) == 0);
        ++cases;
    }
    {   /* Scaled, centre pan: volume 4000h per channel (< 7FF0h), mono output:
         * word 1000h * 4000h: high word 400h, doubled 800h; word 9000h
         * (-7000h): -3800h. Output bytes (acc ^ 8000h) >> 8: 88h, 48h. */
        static const uint8_t d[] = {0x90, 0x10};
        static const uint8_t want[] = {0x88, 0x48, 0x80, 0x80};
        CHECK(setup(0x15u, 1, 4) == 0);
        sample(0, d, 2, 0x40004000u, ONCE, 11025u, 8, 1, CENTRE);
        CHECK(start(0) == 0 && same(want, 4));
        ++cases;
    }
    {   /* The pan law: pan 4000h: factors L 7FFFh, R 4000h; volume 7FFFh:
         * gains 7FFEh and 3FFEh (scaled, R < 7FF0h); word 4000h: 3FFEh and
         * 1FFEh: BFh, 9Fh. Pan C000h mirrors it. */
        static const uint8_t d[] = {0xc0};
        static const uint8_t want[] = {0xbf, 0x9f, 0x80, 0x80};
        static const uint8_t mirror[] = {0x9f, 0xbf, 0x80, 0x80};
        CHECK(setup(0x29u, 1, 1) == 0);
        sample(0, d, 1, 0x00007fffu, ONCE, 22050u, 8, 1, 0x4000u);
        CHECK(start(0) == 0 && same(want, 4));
        CHECK(setup(0x29u, 1, 1) == 0);
        sample(0, d, 1, 0x00007fffu, ONCE, 22050u, 8, 1, 0xc000u);
        CHECK(start(0) == 0 && same(mirror, 4));
        cases += 2;
    }
    {   /* Resampled 11025 into 22050 Hz: quotient 8000h, frac 80000000h, steps
         * 0 and 1 from ECX = 80000000h: the first frame advances, then every
         * second one: d0 d1 d1 d2 d2 d3 d3. */
        static const uint8_t d[] = {0x90, 0xa0, 0xb0, 0xc0};
        static const uint8_t want[] = {0x90, 0x90, 0xa0, 0xa0, 0xa0, 0xa0, 0xb0, 0xb0,
                                       0xb0, 0xb0, 0xc0, 0xc0, 0xc0, 0xc0, 0x80, 0x80};
        CHECK(setup(0x29u, 1, 4) == 0);
        sample(0, d, 4, FULL, ONCE, 11025u, 8, 1, CENTRE);
        CHECK(start(0) == 0 && same(want, 16));
        ++cases;
    }
    {   /* A loop restart inside the buffer: loops - 1 = 1: two plays, then
         * ended; samples += EDI after each run: 2 + 4. */
        static const uint8_t d[] = {0x90, 0xa0};
        static const uint8_t want[] = {0x90, 0xa0, 0x90, 0xa0, 0x80, 0x80, 0x80, 0x80};
        CHECK(setup(0x15u, 1, 8) == 0);
        sample(0, d, 2, FULL, 1u, 11025u, 8, 1, CENTRE);
        CHECK(start(0) == 0 && same(want, 8));
        CHECK(mixer.voice[0].samples == 6u && mixer.voice[0].loops == 0 && (mixer.voice[0].flags & 0x4000u));
        ++cases;
    }
    {   /* The phase restarts with every run and every buffer: 11025 into
         * 22050 Hz mono (25h), an endless two-byte voice: d0 d1 d1 per run;
         * the second buffer starts at d1 with the phase reset. */
        static const uint8_t d[] = {0x90, 0xa0};
        static const uint8_t first[] = {0x90, 0xa0, 0xa0, 0x90, 0xa0, 0xa0, 0x90, 0xa0};
        static const uint8_t second[] = {0xa0, 0x90, 0xa0, 0xa0, 0x90, 0xa0, 0xa0, 0x90};
        CHECK(setup(0x25u, 1, 4) == 0);
        sample(0, d, 2, FULL, ENDLESS, 11025u, 8, 1, CENTRE);
        CHECK(start(0) == 0 && same(first, 8));
        sink.queued = 0;   /* the buffer played */
        CHECK(call(ES2_SOS_USER_SERVICE, 0, 0, 0) == 0);
        CHECK(sink.writes == 2 && same(second, 8));
        cases += 2;
    }
    {   /* Clipping when the list holds more than one voice: two FFh voices
         * add 7F00h twice: FE00h clips to 7FFFh: FFh (unclipped: 7Eh). */
        static const uint8_t d[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
        static const uint8_t want[] = {0xff, 0xff, 0xff, 0xff};
        CHECK(setup(0x15u, 1, 4) == 0);
        sample(0, d, 8, FULL, ONCE, 11025u, 8, 1, CENTRE);
        sample(1, d, 8, FULL, ONCE, 11025u, 8, 1, CENTRE);
        CHECK(start(0) == 0 && start(1) == 0 && r16(HSMP) == 1u);
        CHECK(sink.writes == 1);   /* the second start does not prime */
        sink.queued = 0;
        CHECK(call(ES2_SOS_USER_SERVICE, 0, 0, 0) == 0);
        CHECK(sink.writes == 2 && mixer.count == 2u && same(want, 4));
        ++cases;
    }
    {   /* SampleDone: nonzero only after the buffer holding the end has come
         * back: the end in buffer 1; its return lets the refill notice the
         * voice (+70h = 1 written) and the removal match +996h = 1. */
        static const uint8_t d[] = {0x90, 0x90};
        CHECK(setup(0x15u, 1, 4) == 0);
        sample(0, d, 2, FULL, ONCE, 11025u, 8, 1, CENTRE);
        CHECK(start(0) == 0 && call(ES2_SOS_SAMPLE_DONE, 0, 0, 0) == 0);
        CHECK(call(ES2_SOS_USER_SERVICE, 0, 0, 0) == 0 && call(ES2_SOS_SAMPLE_DONE, 0, 0, 0) == 0);
        sink.queued = 0;
        CHECK(call(ES2_SOS_SAMPLE_DONE, 0, 0, 0) == 1);
        CHECK(mixer.voice[0].flags == 0x2000u && mixer.voice[0].done_at == 1u && mixer.list_n == 0 && sink.writes == 2);
        /* The list empty, +4Ch still 1: one more (silent) buffer, then none. */
        sink.queued = 0;
        CHECK(call(ES2_SOS_USER_SERVICE, 0, 0, 0) == 0 && sink.writes == 3 && mixer.count == 0);
        sink.queued = 0;
        CHECK(call(ES2_SOS_USER_SERVICE, 0, 0, 0) == 0 && sink.writes == 3);
        cases += 2;
    }
    {   /* StopSample keeps the voice until the buffers holding it are back:
         * two buffers primed (written 2); stopped; buffer 1 back: noticed
         * with +70h = 2; buffer 2 back: removed. */
        static const uint8_t d[] = {0x90, 0xa0, 0xb0};
        CHECK(setup(0x15u, 2, 4) == 0);
        sample(0, d, 3, FULL, ENDLESS, 11025u, 8, 1, CENTRE);
        CHECK(start(0) == 0 && sink.writes == 2);
        CHECK(call(ES2_SOS_STOP_SAMPLE, 0, 0, 0) == 0 && call(ES2_SOS_SAMPLE_DONE, 0, 0, 0) == 0);
        sink.queued = 4;   /* buffer 1 back */
        CHECK(call(ES2_SOS_SAMPLE_DONE, 0, 0, 0) == 0 && mixer.voice[0].done_at == 2u && sink.writes == 3);
        sink.queued = 4;   /* buffer 2 back */
        CHECK(call(ES2_SOS_SAMPLE_DONE, 0, 0, 0) == 1);
        ++cases;
    }
    {   /* 32 voices; the 33rd start answers 17h. The setters on a used voice
         * answer the old low word, on an unused one 18h (volume) or
         * handle | 180000h (pan, rate). */
        static const uint8_t d[] = {0x80, 0x80};
        CHECK(setup(0x15u, 1, 4) == 0);
        sample(0, d, 2, 0x12345678u, ENDLESS, 11025u, 8, 1, CENTRE);
        for (unsigned i = 0; i < 32u; ++i) CHECK(start(0) == 0 && r16(HSMP) == i);
        CHECK(start(0) == 0x17u);
        CHECK(call(ES2_SOS_SET_VOLUME, 0, 3, 0x40004000u) == 0x5678u && mixer.voice[3].volume == 0x40004000u);
        CHECK(call(ES2_SOS_SET_PAN, 0, 3, 0x14000u) == 0x8000u && mixer.voice[3].pan == 0x4000u);
        CHECK(call(ES2_SOS_SET_RATE, 0, 3, 22050u) == 11025u && mixer.voice[3].rate == 22050u);
        CHECK(call(ES2_SOS_STOP_ALL, 0, 0, 0) == 0);
        sink.queued = 0;
        CHECK(call(ES2_SOS_USER_SERVICE, 0, 0, 0) == 0);   /* noticed (+70h = 1), removed */
        CHECK(call(ES2_SOS_SAMPLE_DONE, 0, 31, 0) == 1 && mixer.list_n == 0);
        CHECK(call(ES2_SOS_SET_VOLUME, 0, 3, 1) == 0x18u);
        CHECK(call(ES2_SOS_SET_PAN, 0, 3, 1) == 0x180003u);
        CHECK(call(ES2_SOS_SET_RATE, 0, 40, 1) == 0x180028u);
        CHECK(call(ES2_SOS_STOP_ALL, 1, 0, 0) == 0x180001u);
        CHECK(call(ES2_SOS_START_SAMPLE, 0, 0, HSMP) == 0x80000u);
        CHECK(call(ES2_SOS_UNINIT_DRIVER, 0, 0, 0) == 0 && sink.closes == 1);
        CHECK(start(0) == 0x50000u && call(ES2_SOS_USER_SERVICE, 0, 0, 0) == 0x50000u);
        cases += 3;
    }
    {   /* Release without flag 1000h: waveOutReset, every buffer back,
         * closed; acquire: open again, the priming with a voice listed. */
        static const uint8_t d[] = {0x90, 0xa0};
        CHECK(setup(0x15u, 2, 4) == 0);
        sample(0, d, 2, FULL, ENDLESS, 11025u, 8, 1, CENTRE);
        CHECK(start(0) == 0 && sink.writes == 2);
        CHECK(call(ES2_SOS_RELEASE_DRIVER, 0, 0, 0) == 0);
        CHECK(sink.resets == 1 && sink.closes == 1 && mixer.outstanding == 0 && mixer.done == 2u);
        CHECK(call(ES2_SOS_USER_SERVICE, 0, 0, 0) == 0 && sink.writes == 2);   /* closed: nothing */
        CHECK(call(ES2_SOS_ACQUIRE_DRIVER, 0, 0, 0) == 0 && sink.opens == 2 && sink.writes == 4 && mixer.outstanding == 2u);
        ++cases;
    }
    {   /* The pause: stop all, then release with flag 1000h. The ring drains
         * first: buffer 1 back, its refill notices both voices (+70h = 2);
         * buffer 2 back, its refill, then both removed; buffer 3 back, one
         * silent buffer (+4Ch was 2); buffers 4 and 5 back, none; reset and
         * close. The voices are free again; the acquire primes nothing; a new
         * start primes. */
        static const uint8_t d[] = {0x90, 0xa0};
        CHECK(setup(0x15u, 2, 4) == 0);
        sample(0, d, 2, FULL, ENDLESS, 11025u, 8, 1, CENTRE);
        CHECK(start(0) == 0 && start(0) == 0 && sink.writes == 2 && mixer.list_n == 2u);
        CHECK(call(ES2_SOS_STOP_ALL, 0, 0, 0) == 0);
        CHECK(call(ES2_SOS_RELEASE_DRIVER, 0, 0x1000u, 0) == 0);
        CHECK(mixer.list_n == 0 && mixer.voice[0].flags == 0x2000u && mixer.voice[1].flags == 0x2000u);
        CHECK(mixer.outstanding == 0 && mixer.count == 0 && sink.writes == 5 && sink.resets == 1 && sink.closes == 1);
        CHECK(call(ES2_SOS_SAMPLE_DONE, 0, 0, 0) == 1 && call(ES2_SOS_SAMPLE_DONE, 0, 1, 0) == 1);
        CHECK(call(ES2_SOS_ACQUIRE_DRIVER, 0, 0, 0) == 0 && sink.opens == 2 && sink.writes == 5);
        CHECK(start(0) == 0 && r16(HSMP) == 0u && sink.writes == 7);
        /* A voice still playing: the original would spin; bounded here. */
        CHECK(call(ES2_SOS_RELEASE_DRIVER, 0, 0x1000u, 0) == 0 && mixer.outstanding == 0 && sink.closes == 2);
        cases += 2;
    }
    {   /* 16-bit: a signed mono source to 16-bit mono output (16h): the
         * accumulator's low word, little-endian. */
        static const uint8_t d[] = {0x34, 0x12, 0x00, 0x80};
        static const uint8_t want[] = {0x34, 0x12, 0x00, 0x80, 0, 0, 0, 0};
        CHECK(setup(0x16u, 1, 4) == 0 && mixer.block == 8u && sink.bits == 16u);
        sample(0, d, 4, FULL, ONCE, 11025u, 16, 1, CENTRE);
        CHECK(start(0) == 0 && same(want, 8));
        ++cases;
    }
    {   /* sosDIGISetActiveWindow's answers: driver 1: 180001h; a handle
         * IsWindow refuses: handle | 40000h, nothing stored; stored (0 too);
         * not initialised: 50000h. */
        CHECK(setup(0x15u, 1, 4) == 0);
        mixer.post = rec_post;
        mixer.is_window = rec_is_window;
        mixer.post_user = &rec;
        CHECK(call(ES2_SOS_SET_ACTIVE_WINDOW, 1, 0x1234u, 0) == 0x180001u && mixer.window == 0);
        CHECK(call(ES2_SOS_SET_ACTIVE_WINDOW, 0, 0xdeadu, 0) == 0x4deadu && mixer.window == 0);
        CHECK(call(ES2_SOS_SET_ACTIVE_WINDOW, 0, 0x1234u, 0) == 0 && mixer.window == 0x1234u);
        CHECK(call(ES2_SOS_SET_ACTIVE_WINDOW, 0, 0, 0) == 0 && mixer.window == 0);
        CHECK(call(ES2_SOS_UNINIT_DRIVER, 0, 0, 0) == 0 && call(ES2_SOS_SET_ACTIVE_WINDOW, 0, 0x1234u, 0) == 0x50000u);
        ++cases;
    }
    {   /* The notices. One buffer; voice 0 endless, voice 1 once (two bytes):
         * the refill after the first buffer's return mixes voice 1's end:
         * 0xa000 (0xa003, 1 << 16 | 4000h); the next return notices and removes
         * it: (0xa003, 1 << 16 | 2000h), here refused by the queue (lost).
         * Window 0: no notice. */
        static const uint8_t endless[] = {0x90, 0xa0}, once[] = {0x90, 0x90};
        CHECK(setup(0x15u, 1, 4) == 0);
        memset(&rec, 0, sizeof(rec));
        mixer.post = rec_post;
        mixer.post_user = &rec;
        CHECK(call(ES2_SOS_SET_ACTIVE_WINDOW, 0, 0x1234u, 0) == 0);
        sample(0, endless, 2, FULL, ENDLESS, 11025u, 8, 1, CENTRE);
        sample(1, once, 2, FULL, ONCE, 11025u, 8, 1, CENTRE);
        CHECK(start(0) == 0 && start(1) == 0 && r16(HSMP) == 1u && sink.writes == 1 && rec.n == 0);
        sink.queued = 0;
        CHECK(call(ES2_SOS_USER_SERVICE, 0, 0, 0) == 0 && sink.writes == 2 && rec.n == 1 && rec.window[0] == 0x1234u &&
              rec.message[0] == 0xa000u && rec.wparam[0] == 0xa003u && rec.lparam[0] == 0x14000u);
        sink.queued = 0;
        rec.fail = 1;
        CHECK(call(ES2_SOS_SAMPLE_DONE, 0, 1, 0) == 1 && rec.n == 2 && rec.lparam[1] == 0x12000u && mixer.posts == 2u &&
              mixer.posts_lost == 1u);
        rec.fail = 0;
        CHECK(call(ES2_SOS_SET_ACTIVE_WINDOW, 0, 0, 0) == 0 && call(ES2_SOS_STOP_SAMPLE, 0, 0, 0) == 0);
        sink.queued = 0;
        CHECK(call(ES2_SOS_USER_SERVICE, 0, 0, 0) == 0);
        sink.queued = 0;
        CHECK(call(ES2_SOS_SAMPLE_DONE, 0, 0, 0) == 1 && rec.n == 2 && mixer.posts == 2u);
        cases += 2;
    }
    {   /* The last buffer comes back on a sink that holds a tail. Two buffers
         * of 4 bytes; the stream keeps the newest 2 bytes until flushed. Voice
         * 0 (once, 2 bytes) ends in buffer 1 (4000h); buffer 1 back: the refill
         * mixes it once more, the removal (2000h); buffer 2 back: the refill
         * mixes no voice (+4Ch 0) and the sink is flushed; then both buffers
         * come back and nothing is refilled. The next start finds no buffer
         * outstanding and primes the ring (0x406167.. 0x406188). Without the
         * flush one buffer stays outstanding on the held tail, the start does
         * not prime and the voice is never mixed. */
        static const uint8_t once[] = {0x90, 0x90};
        CHECK(setup(0x15u, 2, 4) == 0);
        sink.hold = 2;
        memset(&rec, 0, sizeof(rec));
        mixer.post = rec_post;
        mixer.post_user = &rec;
        CHECK(call(ES2_SOS_SET_ACTIVE_WINDOW, 0, 0x1234u, 0) == 0);
        sample(0, once, 2, FULL, ONCE, 11025u, 8, 1, CENTRE);
        CHECK(start(0) == 0 && sink.writes == 2 && rec.n == 1 && rec.lparam[0] == 0x4000u);
        play();
        CHECK(call(ES2_SOS_USER_SERVICE, 0, 0, 0) == 0 && sink.writes == 3 && mixer.list_n == 0 && rec.n == 2 &&
              rec.lparam[1] == 0x2000u && sink.flushes == 0);
        play();
        CHECK(call(ES2_SOS_USER_SERVICE, 0, 0, 0) == 0 && sink.writes == 4 && mixer.count == 0 && sink.flushes == 1 &&
              mixer.flushes == 1u && mixer.outstanding == 2u);
        play();
        CHECK(call(ES2_SOS_USER_SERVICE, 0, 0, 0) == 0 && sink.queued == 0 && mixer.outstanding == 0 && sink.writes == 4);
        CHECK(call(ES2_SOS_SAMPLE_DONE, 0, 0, 0) == 1);
        sample(1, once, 2, FULL, ONCE, 11025u, 8, 1, CENTRE);
        CHECK(call(ES2_SOS_START_SAMPLE, 0, SMP(1), HSMP) == 0 && r16(HSMP) == 0 && sink.writes == 6 && rec.n == 3 &&
              rec.lparam[2] == 0x4000u);
        /* One buffer back: the refill mixes the listed (ended) voice and flushes nothing. */
        sink.queued = 4;
        CHECK(call(ES2_SOS_USER_SERVICE, 0, 0, 0) == 0 && sink.writes == 7 && sink.flushes == 1);
        cases += 2;
    }
    {   /* The resample DIV 0x406cf0: EDX:EAX = rate:0 by ECX = 11025 << 16 =
         * 2B110000h. Rate 2B10FFFFh: quotient FFFFFFFFh, no fault (frac
         * FFFF0000h carries on every frame, step 1 = 10000h wraps to 0: d0
         * throughout). Rate
         * 2B110000h: DIV overflow C0000095h at the DLL's DIV, the run ends
         * (recorder), nothing is written, the refill writes nothing more.
         * Without a process record the quotient saturates as before. */
        static const uint8_t d[] = {0x90, 0xa0, 0xb0, 0xc0};
        static const uint8_t first[] = {0x90, 0x90, 0x90, 0x90};
        bind_recorder();
        CHECK(setup(0x15u, 1, 4) == 0);
        sample(0, d, 4, FULL, ONCE, 0x2b10ffffu, 8, 1, CENTRE);
        CHECK(start(0) == 0 && sink.writes == 1 && same(first, 4) && ended.n == 0 && !mixer.faulted);
        bind_recorder();
        CHECK(setup(0x15u, 1, 4) == 0);
        sample(0, d, 4, FULL, ONCE, 0x2b110000u, 8, 1, CENTRE);
        start(0);
        CHECK(ended.n == 1 && ended.code == 0xc0000095u && ended.site == 0x00406cf0u && ended.site == ES2_SIM_SOS_DIV_SITE);
        CHECK(loop.outcome == ES2_SIM_LOOP_EXITED && loop.exit_code == 0xc0000095u);
        CHECK(mixer.faulted && sink.writes == 0 && mixer.outstanding == 0);
        call(ES2_SOS_USER_SERVICE, 0, 0, 0);
        CHECK(sink.writes == 0 && ended.n == 1);
        es2_sim_loop_bind(NULL);
        CHECK(setup(0x15u, 1, 4) == 0);
        sample(0, d, 4, FULL, ONCE, 0x2b110000u, 8, 1, CENTRE);
        CHECK(start(0) == 0 && sink.writes == 1 && same(first, 4) && !mixer.faulted);
        cases += 3;
    }
    {   /* The silent device answers what the mixer does not model (MCI). */
        CHECK(setup(0x15u, 1, 4) == 0);
        CHECK(device.mci != NULL);
        ++cases;
    }

    printf("sos_mixer_test: %u cases passed\n", cases);
    free(bytes);
    return 0;
}
