/* The windowed audio (sim/src/audio_sdl.c, es2_sim_mci_send in sim/src/audio.c;
 * audio.md, "sound and CD music in the window") on a synthetic image, with a
 * recording sink in place of SDL and a fake mciSendCommandA in place of winmm:
 * no audio device and no CD drive is opened. The message-only notify window is
 * real (a window of this process, never shown). Cases:
 *   1. es2_sim_mci_send alone: the host blocks, the strings, the callback
 *      replaced by the notify window, the write-back, no block, the guards.
 *   2. The driver load 0x473708 over the window device with the CD's
 *  sound.cfg words: the sink opens 8-bit, 22,050 Hz, stereo; a
 *      voice primes the ring, a peek through the attached seam pumps it.
 *   3. The CD start 0x46313c with a CD: MCI_OPEN "cdaudio", MCI_SET TMSF,
 *      MCI_STATUS length, MCI_PLAY with notify to the host window; then a
 *      posted MM_MCINOTIFY retrieved by the peek and handed to the window
 *      procedure, whose handler 0x462398 stops and replays the track.
 *   4. The failure paths: MCI_OPEN failing (no CD drive), MCI_STATUS failing
 *      (no disc): the original's stop and EAX 0.
 *   5. The close restores the seam.
 * No game data. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "es2/sim/audio_sdl.h"
#include "es2/sim/memory.h"
#include "es2/sim/services.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

#define IMAGE_BASE 0x00400000u
#define IMAGE_SIZE 0x002d8000u
#define HEAP_BASE 0x09000000u
#define HEAP_SIZE 0x00400000u
#define GLOBAL_BASE 0x0a000000u
#define GLOBAL_SIZE 0x00100000u
#define LOCAL_SIZE 0x00080000u
#define COUNTER (IMAGE_BASE + 0x2d7ff0u)

#define VA_CD_DEVICE 0x004a0e2cu
#define VA_CD_LENGTH 0x006b558cu
#define VA_CONTEXT 0x0049f8ecu
#define VA_CD_TRACK 0x0049f8fcu
#define VA_CD_LOOP 0x0049f900u
#define VA_GATE_LOW 0x0049f8f4u

static uint8_t *image, *heap_bytes, *global_bytes, *local;
static uint32_t local_guest, global_next;
static es2_sim_arena arena;
static es2_guest_memory memory;
static es2_sim_heap heap;
static es2_sim_services services;
static es2_sim_entry storage[4096];
static es2_sim_dispatch table;
static es2_sim_loop loop;
static es2_sim_audio audio;
static es2_sim_audio_device device;
static es2_sim_audio_window window_audio;
static es2_vt vt;
static unsigned refusals;
static uint32_t first_refused;
static void on_refuse(const es2_sim_refusal *r, void *user) {
    (void)user;
    if (!refusals) first_refused = r->va;
    ++refusals;
    if (getenv("AUDIO_TEST_VERBOSE")) fprintf(stderr, "refused 0x%08x: %s\n", (unsigned)r->va, r->reason ? r->reason : "");
}
static uint32_t next_heap = HEAP_BASE;
static uint32_t heap_alloc(void *c, uint32_t address, uint32_t size, uint32_t type, uint32_t protect) {
    (void)c; (void)protect;
    if (type & ES2_HEAP_MEM_RESERVE) {
        if (!address) address = next_heap;
        if (address < HEAP_BASE || address + size > HEAP_BASE + HEAP_SIZE) return 0;
        next_heap = address + size;
    }
    return address;
}
static uint32_t os_free(void *c, uint32_t a, uint32_t s, uint32_t t) { (void)c; (void)a; (void)s; (void)t; return 1; }
static uint32_t g_alloc(void *u, uint32_t flags, uint32_t size) {
    (void)u; (void)flags;
    uint32_t a = global_next;
    if (a + size > GLOBAL_BASE + GLOBAL_SIZE) return 0;
    global_next += (size + 15u) & ~15u;
    return a;
}
static uint32_t g_free(void *u, uint32_t h) { (void)u; (void)h; return 0; }

static uint32_t r32(uint32_t a) { return memory.read32(memory.context, a); }
static uint16_t r16(uint32_t a) { return memory.read16(memory.context, a); }
static void w32(uint32_t a, uint32_t v) { memory.write32(memory.context, a, v); }
static void w16(uint32_t a, uint16_t v) { memory.write16(memory.context, a, v); }
static void w8(uint32_t a, uint8_t v) { es2_sim_write8(&memory, a, v); }
static void text(uint32_t a, const char *t) { do w8(a++, (uint8_t)*t); while (*t++); }

/* ---- the recording sink */
typedef struct fake_sink { uint32_t opens, closes, resets, writes, rate, channels, bits, queued; } fake_sink;
static fake_sink sink;
static uint32_t f_open(void *c, uint32_t rate, uint32_t channels, uint32_t bits) {
    fake_sink *f = c;
    ++f->opens; f->rate = rate; f->channels = channels; f->bits = bits;
    return 0;
}
static uint32_t f_write(void *c, const uint8_t *d, uint32_t n) { (void)d; fake_sink *f = c; ++f->writes; f->queued += n; return 0; }
static uint32_t f_queued(void *c) { return ((fake_sink *)c)->queued; }
static void f_reset(void *c) { ++((fake_sink *)c)->resets; ((fake_sink *)c)->queued = 0; }
static void f_close(void *c) { ++((fake_sink *)c)->closes; }

/* ---- the fake mciSendCommandA: records every command and its host block,
 * answers from a script. */
typedef struct mci_record { uint32_t device, message, flags; int block; uintptr_t f[5]; char type[32]; } mci_record;
static mci_record sent[32];
static unsigned sent_n;
static uint32_t answer_open, answer_set, answer_status, answer_play, open_id = 7u, status_value = 0x00042c03u;
static uint32_t fake_send(void *user, uint32_t dev, uint32_t message, uint32_t flags, void *params) {
    (void)user;
    mci_record *r = &sent[sent_n < 32u ? sent_n++ : 31u];
    memset(r, 0, sizeof(*r));
    r->device = dev; r->message = message; r->flags = flags; r->block = params != NULL;
    uintptr_t *p = params;
    unsigned n = message == 0x803u ? 5u : message == 0x814u ? 4u : (message == 0x806u || message == 0x80du) ? 3u : 1u;
    if (p) for (unsigned i = 0; i < n; ++i) r->f[i] = p[i];
    if (message == 0x803u && p && (flags & 0x3000u) == 0x2000u) snprintf(r->type, sizeof(r->type), "%s", (const char *)p[2]);
    switch (message) {
    case 0x803u: if (!answer_open) p[1] = open_id; return answer_open;
    case 0x80du: return answer_set;
    case 0x814u: if (!answer_status) p[1] = status_value; return answer_status;
    case 0x806u: return answer_play;
    default: return 0;
    }
}

/* ---- the synthetic image and the dispatch (as audio_test.c) */
static int setup_dispatch(void) {
    es2_sim_dispatch_init(&table, storage, 4096, on_refuse, NULL);
    services.memory = &memory;
    services.heap = &heap;
    services.vt = &vt;
    static uint32_t fs0 = 0x00abcdefu;
    services.fs0 = &fs0;
    if (es2_sim_register_services(&table, &services)) return 1;
    const char *error = NULL;
    uint32_t bad = 0;
    return es2_sim_dispatch_seal(&table, &error, &bad);
}
static void fresh(void) {
    memset(image, 0, IMAGE_SIZE);
    w32(0x006cbee4u, COUNTER);
    w32(0x004a0e30u, 100);
    w32(VA_CD_DEVICE, 0xffffffffu);
    text(0x004a0e34u, "cdaudio");
    text(0x004a0e3cu, "cdaudio");
    memset(heap_bytes, 0, HEAP_SIZE);
    memset(global_bytes, 0, GLOBAL_SIZE);
    memset(local, 0, LOCAL_SIZE);
    next_heap = HEAP_BASE;
    global_next = GLOBAL_BASE;
    es2_sim_heap_os os = {NULL, heap_alloc, os_free};
    es2_sim_heap_init(&heap, &memory, &os, local_guest + 0x7f000u, 0, 0, 0);
    memset(&vt, 0, sizeof(vt));
    es2_vt_enable(&vt, 1, 47, 0x01000000u, 0, 1);
    es2_sim_loop_defaults(&loop);
    loop.memory = &memory;
    loop.dispatch = &table;
    loop.vt = &vt;
    loop.stack_lo = local_guest;
    loop.stack_hi = local_guest + 0x10000u;
    es2_sim_loop_bind(&loop);
    memset(&sink, 0, sizeof(sink));
    sent_n = 0;
    answer_open = answer_set = answer_status = answer_play = 0;
    es2_sim_sos_sink s = {&sink, f_open, f_write, f_queued, f_reset, f_close, NULL};   /* no flush */
    es2_sim_audio_window_init(&window_audio, &vt, &s, fake_send, NULL, 1);
    es2_sim_audio_window_device(&window_audio, &device);
    es2_sim_audio_headless(&audio, &loop, &device);
    audio.platform.global_alloc = g_alloc;
    audio.platform.global_free = g_free;
    es2_sim_audio_bind(&audio);
    refusals = 0;
}
static int row(uint32_t va, const uint32_t *slots, unsigned n, es2_sim_result *r) {
    uint32_t regs[3] = {0x0e0e0e0eu, 0x0d0d0d0du, 0x0c0c0c0cu};
    uint32_t at = local_guest + 0x20000u;
    for (unsigned i = 0; i < n; ++i) w32(at + 4u * i, slots[i]);
    memset(r, 0, sizeof(*r));
    return es2_sim_call(&table, va, (const uint32_t *)(uintptr_t)at, n, regs, r);
}

/* ---- 1. es2_sim_mci_send alone */
static int translation(void) {
    fresh();
    es2_sim_mci_host *h = &window_audio.mci;
    CHECK(window_audio.notify_window && h->notify_window == (uintptr_t)window_audio.notify_window);
    uint32_t b = local_guest + 0x30000u, s = local_guest + 0x30100u;
    /* MCI_OPEN with TYPE | ELEMENT | ALIAS: three strings copied, the device
     * id written back, the guest's pointers left as they were. */
    text(s, "cdaudio"); text(s + 0x20u, "d:"); text(s + 0x40u, "music");
    w32(b, 0x11111111u); w32(b + 4u, 0xeeeeeeeeu); w32(b + 8u, s); w32(b + 0xcu, s + 0x20u); w32(b + 0x10u, s + 0x40u);
    CHECK(es2_sim_mci_send(h, &memory, 0, 0x803u, 0x2600u, b) == 0 && sent_n == 1u);
    CHECK(sent[0].block && !strcmp(sent[0].type, "cdaudio") && !strcmp((const char *)sent[0].f[3], "d:") &&
          !strcmp((const char *)sent[0].f[4], "music") && sent[0].f[0] == 0x11111111u && sent[0].f[1] == 0xeeeeeeeeu);
    CHECK(r32(b + 4u) == 7u && r32(b + 8u) == s && r32(b + 0xcu) == s + 0x20u && r32(b + 0x10u) == s + 0x40u && r32(b) == 0x11111111u);
    /* MCI_OPEN_TYPE_ID: the type field is an id, passed as is. */
    w32(b + 8u, 0x205u);
    CHECK(es2_sim_mci_send(h, &memory, 0, 0x803u, 0x3000u, b) == 0 && sent[1].f[2] == 0x205u);
    /* MCI_PLAY with NOTIFY: the callback becomes the host window, the guest's
     * stays in its block; without NOTIFY it is passed as is. */
    w32(b, 0x00010001u); w32(b + 4u, 3); w32(b + 8u, 0x02030405u);
    CHECK(es2_sim_mci_send(h, &memory, 7u, 0x806u, 0xdu, b) == 0);
    CHECK(sent[2].device == 7u && sent[2].f[0] == h->notify_window && sent[2].f[1] == 3u && sent[2].f[2] == 0x02030405u);
    CHECK(r32(b) == 0x00010001u && h->guest_window == 0x00010001u && h->notify_requests == 1u);
    CHECK(es2_sim_mci_send(h, &memory, 7u, 0x806u, 0xcu, b) == 0 && sent[3].f[0] == 0x00010001u);
    /* MCI_NOTIFY with a zero callback: passed as 0. */
    w32(b, 0);
    CHECK(es2_sim_mci_send(h, &memory, 7u, 0x806u, 0xdu, b) == 0 && sent[4].f[0] == 0 && h->notify_requests == 1u);
    /* MCI_STATUS: dwReturn written back. */
    w32(b, 0); w32(b + 4u, 0x99999999u); w32(b + 8u, 1); w32(b + 0xcu, 3);
    CHECK(es2_sim_mci_send(h, &memory, 7u, 0x814u, 0x112u, b) == 0 && r32(b + 4u) == status_value && sent[5].f[3] == 3u);
    /* No block (MCI_STOP, MCI_CLOSE as DBSIM sends them): NULL. */
    CHECK(es2_sim_mci_send(h, &memory, 7u, 0x808u, 0, 0) == 0 && !sent[6].block && sent[6].message == 0x808u);
    /* Another command with a block: 274, not sent; an unterminated string:
     * 263, not sent; no send function: 306. */
    CHECK(es2_sim_mci_send(h, &memory, 7u, 0x80bu, 0, b) == 274u && sent_n == 7u && h->unsupported == 1u);
    for (uint32_t i = 0; i < 200u; ++i) w8(s + 0x200u + i, 'x');
    w32(b + 8u, s + 0x200u);
    CHECK(es2_sim_mci_send(h, &memory, 0, 0x803u, 0x2000u, b) == 263u && sent_n == 7u);
    es2_sim_mci_host none;
    es2_sim_mci_host_init(&none, NULL, NULL, 0);
    CHECK(es2_sim_mci_send(&none, &memory, 0, 0x803u, 0x2000u, b) == 306u);
    es2_sim_audio_window_close(&window_audio);
    return 0;
}

/* ---- 2. the driver load and the mixer pumped through the seam */
static uint32_t settings(void) {
    /* The words 0x473708 reads (audio_test.c's): the CD's sound.cfg, 22 kHz, stereo, 5 buffers, block 200h. */
    uint32_t cfg = local_guest + 0x30000u;
    w16(cfg, 0x20); w16(cfg + 2u, 1); w16(cfg + 4u, 8); w16(cfg + 6u, 1); w16(cfg + 0xcu, 5); w16(cfg + 0xeu, 0x200);
    return cfg;
}
static int peeks_seen;
static int inner_peek(void *user, es2_sim_loop *l, uint32_t *m, uint32_t *w, uint32_t *p) {
    (void)user; (void)l; (void)m; (void)w; (void)p;
    ++peeks_seen;
    return 0;
}
static es2_sim_loop_window inner_ops;
static int mixing(void) {
    fresh();
    es2_sim_result r;
    uint32_t a[2] = {0x00010001u, settings()};
    CHECK(row(0x00473708u, a, 2, &r) == 0 && refusals == 0 && r.eax == 1u);
    CHECK(r16(0x006b5590u + 0x4eu) == 0x29u && sink.opens == 1u && sink.rate == 22050u && sink.channels == 2u && sink.bits == 8u);
    CHECK(window_audio.mixer.block == 0x800u && window_audio.mixer.buffers == 5u);
    /* A looping voice primes the five buffers. */
    uint32_t smp = GLOBAL_BASE + 0x1000u, data = GLOBAL_BASE + 0x2000u, handle = GLOBAL_BASE + 0x100u;
    for (uint32_t k = 0; k < 64u; ++k) w8(data + k, (uint8_t)(0x80u + k));
    w32(smp, data); w32(smp + 0x10u, 64u); w32(smp + 0x30u, 0x7fff7fffu); w32(smp + 0x34u, 0xffffffffu);
    w32(smp + 0x38u, 22050u); w32(smp + 0x3cu, 8u); w32(smp + 0x40u, 1u); w32(smp + 0x44u, 0x8000u); w32(smp + 0x48u, 0x8000u);
    uint32_t sa[3] = {0, smp, handle};
    CHECK(device.sos(device.context, &memory, ES2_SOS_START_SAMPLE, sa, 3) == 0 && sink.writes == 5u);
    /* The seam: two buffers played; the peek pumps, they come back and are refilled. */
    memset(&inner_ops, 0, sizeof(inner_ops));
    inner_ops.peek = inner_peek;
    loop.window = &inner_ops;
    es2_sim_audio_window_attach(&window_audio, &loop);
    CHECK(loop.window == &window_audio.ops && !window_audio.ops.pace && !window_audio.ops.def_window_proc);
    sink.queued = 3u * 0x800u;
    uint32_t m = 0, w = 0, p = 0;
    peeks_seen = 0;
    CHECK(loop.window->peek(loop.window->user, &loop, &m, &w, &p) == 0 && peeks_seen == 1);
    CHECK(window_audio.pumps == 1u && sink.writes == 7u && window_audio.mixer.done == 2u);
    /* The pause sleep pumps too. */
    sink.queued = 0;
    window_audio.ops.sleep(window_audio.ops.user, 100u);
    CHECK(window_audio.pumps == 2u && sink.writes == 12u);
    es2_sim_audio_window_close(&window_audio);
    CHECK(loop.window == &inner_ops && sink.closes == 1u);
    return 0;
}

/* ---- 3. the CD with a disc: start, notify, replay */
static int cd_music(void) {
    fresh();
    es2_sim_result r;
    memset(&inner_ops, 0, sizeof(inner_ops));
    inner_ops.peek = inner_peek;
    loop.window = &inner_ops;
    es2_sim_audio_window_attach(&window_audio, &loop);
    w32(VA_CONTEXT, 0x00123456u); w32(VA_CD_TRACK, 3); w32(VA_GATE_LOW, 1); w32(0x006b5588u, 0x00010001u);
    CHECK(row(0x0046313cu, NULL, 0, &r) == 0 && refusals == 0 && r.eax == 1u);
    CHECK(sent_n == 4u && r32(VA_CD_DEVICE) == 7u && r32(VA_CD_LENGTH) == status_value);
    CHECK(sent[0].message == 0x803u && sent[0].device == 0 && sent[0].flags == 0x2000u && !strcmp(sent[0].type, "cdaudio"));
    CHECK(sent[1].message == 0x80du && sent[1].device == 7u && sent[1].flags == 0x400u && sent[1].f[1] == 10u);
    CHECK(sent[2].message == 0x814u && sent[2].flags == 0x112u && sent[2].f[2] == 1u && sent[2].f[3] == 3u);
    /* TO = the track | the length's three low bytes << 8 (TMSF). */
    CHECK(sent[3].message == 0x806u && sent[3].flags == 0xdu && sent[3].f[0] == (uintptr_t)window_audio.notify_window);
    CHECK(sent[3].f[1] == 3u && sent[3].f[2] == (3u | (status_value & 0xffffffu) << 8));
    /* The drive posts MM_MCINOTIFY (successful) to the notify window; the
     * peek retrieves it before the window layer's queue. */
    CHECK(PostMessageA((HWND)window_audio.notify_window, ES2_MM_MCINOTIFY, 1, 7));
    uint32_t m = 0, w = 0, p = 0;
    peeks_seen = 0;
    CHECK(loop.window->peek(loop.window->user, &loop, &m, &w, &p) == 1 && m == 0x3b9u && w == 1u && p == 7u && peeks_seen == 0);
    CHECK(loop.window->peek(loop.window->user, &loop, &m, &w, &p) == 0 && peeks_seen == 1);
    /* The window procedure with the handler 0x462398 registered: without the
     * loop byte nothing; with it the stop, the close and the track again. */
    w32(ES2_SIM_VA_HANDLERS, 0x00462398u);
    uint32_t result = 1;
    sent_n = 0;
    CHECK(es2_sim_window_proc(&loop, 0x00010001u, 0x3b9u, 1u, 7u, &result) == 0 && result == 0 && sent_n == 0);
    w8(VA_CD_LOOP, 1);
    CHECK(es2_sim_window_proc(&loop, 0x00010001u, 0x3b9u, 4u, 7u, &result) == 0 && sent_n == 0);   /* aborted: ignored */
    CHECK(es2_sim_window_proc(&loop, 0x00010001u, 0x3b9u, 1u, 7u, &result) == 0 && refusals == 0);
    CHECK(sent_n == 6u && sent[0].message == 0x808u && !sent[0].block && sent[1].message == 0x804u && sent[1].device == 7u);
    CHECK(sent[2].message == 0x803u && sent[5].message == 0x806u && r32(VA_CD_DEVICE) == 7u);
    /* A notify the SDL pump dispatched lands in the queue the same way. */
    CHECK(PostMessageA((HWND)window_audio.notify_window, ES2_MM_MCINOTIFY, 2, 7));
    MSG msg;
    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageA(&msg);
    CHECK(window_audio.notify_count == 1u);
    CHECK(loop.window->peek(loop.window->user, &loop, &m, &w, &p) == 1 && m == 0x3b9u && w == 2u);
    /* The stop row 0x464874(context) (the pause path 0x45f4cf): STOP and
     * CLOSE without blocks, the word -1. */
    sent_n = 0;
    uint32_t x[1] = {0x00123456u};
    CHECK(row(0x00464874u, x, 1, &r) == 0 && refusals == 0 && sent_n == 2u && !sent[0].block && r32(VA_CD_DEVICE) == 0xffffffffu);
    es2_sim_audio_window_close(&window_audio);
    CHECK(loop.window == &inner_ops);
    return 0;
}

/* ---- 4. the failure paths */
static int no_disc(void) {
    fresh();
    es2_sim_result r;
    w32(VA_CONTEXT, 0x00123456u); w32(VA_CD_TRACK, 3); w32(VA_GATE_LOW, 1);
    /* No CD drive: MCI_OPEN fails; nothing else is sent. */
    answer_open = 275u;   /* MCIERR_DEVICE_OPEN-class answer */
    CHECK(row(0x0046313cu, NULL, 0, &r) == 0 && r.eax == 0 && sent_n == 1u && r32(VA_CD_DEVICE) == 0xffffffffu);
    /* A drive without a disc: the status fails, the length is 0: stop, close, EAX 0. */
    answer_open = 0;
    answer_status = 262u;  /* MCIERR_HARDWARE */
    sent_n = 0;
    CHECK(row(0x0046313cu, NULL, 0, &r) == 0 && r.eax == 0 && refusals == 0);
    CHECK(sent_n == 5u && sent[3].message == 0x808u && sent[4].message == 0x804u && r32(VA_CD_DEVICE) == 0xffffffffu &&
          r32(VA_CD_LENGTH) == 0);
    /* MCI_SET failing: stop, close. */
    answer_status = 0;
    answer_set = 0x113u;
    sent_n = 0;
    CHECK(row(0x0046313cu, NULL, 0, &r) == 0 && r.eax == 0 && sent_n == 4u && sent[2].message == 0x808u);
    /* MCI_PLAY failing: stop, close. */
    answer_set = 0;
    answer_play = 0x113u;
    sent_n = 0;
    CHECK(row(0x0046313cu, NULL, 0, &r) == 0 && r.eax == 0 && sent_n == 6u && sent[5].message == 0x804u &&
          r32(VA_CD_DEVICE) == 0xffffffffu);
    es2_sim_audio_window_close(&window_audio);
    return 0;
}

int main(void) {
    image = calloc(1, IMAGE_SIZE);
    heap_bytes = calloc(1, HEAP_SIZE);
    global_bytes = calloc(1, GLOBAL_SIZE);
    for (uint32_t a = 0x30000000u; !local && a < 0x38000000u; a += 0x01000000u)
        local = VirtualAlloc((void *)(uintptr_t)a, LOCAL_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    CHECK(image && heap_bytes && global_bytes && local);
    local_guest = (uint32_t)(uintptr_t)local;
    CHECK(es2_sim_arena_add(&arena, IMAGE_BASE, image, IMAGE_SIZE));
    CHECK(es2_sim_arena_add(&arena, HEAP_BASE, heap_bytes, HEAP_SIZE));
    CHECK(es2_sim_arena_add(&arena, GLOBAL_BASE, global_bytes, GLOBAL_SIZE));
    CHECK(es2_sim_arena_add(&arena, local_guest, local, LOCAL_SIZE));
    es2_sim_arena_memory(&arena, &memory);
    fresh();
    es2_sim_audio_window_close(&window_audio);
    CHECK(setup_dispatch() == 0);
    if (translation() || mixing() || cd_music() || no_disc()) {
        fprintf(stderr, "first refused 0x%08lx\n", (unsigned long)first_refused);
        return 1;
    }
    puts("PASS: windowed audio: MCI blocks through a fake winmm, the driver load 8-bit 22050 Hz stereo, the mixer pumped "
         "by the seam, CD start, MM_MCINOTIFY to the window procedure and the replay, the failure paths");
    return 0;
}
