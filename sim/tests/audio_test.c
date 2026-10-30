/* Audio (sim/src/audio.c) on a synthetic image: zeroed host memory with the
 * objects each case needs written by this test at the original's addresses, the
 * game heap over an emulated OS region, the main loop's call area at its own
 * host address, the silent device (or a recording device of this test) and
 * GlobalAlloc from an arena range. The sim's real services answer the heap,
 * string, clock and handler-list rows; the hooks the audio code calls
 * (sfx_voice, sfx_lip_due, sfx_play, sfx_rate) are recording services of this
 * test. No game data. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "es2/sim/audio.h"
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
static es2_sim_audio_silent silent;
static es2_sim_audio_device device;
static es2_vt vt;
static uint32_t first_refused;
static unsigned refusals;
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
static unsigned g_frees;
static uint32_t g_free(void *u, uint32_t h) { (void)u; (void)h; ++g_frees; return 0; }

static uint32_t r32(uint32_t a) { return memory.read32(memory.context, a); }
static uint16_t r16(uint32_t a) { return memory.read16(memory.context, a); }
static uint8_t r8(uint32_t a) { return es2_sim_read8(&memory, a); }
static void w32(uint32_t a, uint32_t v) { memory.write32(memory.context, a, v); }
static void w16(uint32_t a, uint16_t v) { memory.write16(memory.context, a, v); }
static void w8(uint32_t a, uint8_t v) { es2_sim_write8(&memory, a, v); }
static void text(uint32_t a, const char *t) { do w8(a++, (uint8_t)*t); while (*t++); }

/* Recording stand-ins for the hooks the audio code calls. */
static uint32_t hook_va[64], hook_arg[64];
static unsigned hooks;
static void svc_hook(es2_sim_service_call *c, void *u) {
    (void)u;
    if (hooks < 64u) { hook_va[hooks] = c->va; hook_arg[hooks] = c->slots[0]; ++hooks; }
    c->eax = 0x0000beefu; c->edx = 0x11u; c->ecx = 0x22u;
}
static int setup_dispatch(void) {
    es2_sim_dispatch_init(&table, storage, 4096, on_refuse, NULL);
    services.memory = &memory;
    services.heap = &heap;
    services.vt = &vt;
    static uint32_t fs0 = 0x00abcdefu;
    services.fs0 = &fs0;
    if (es2_sim_register_services(&table, &services)) return 1;
    static const uint32_t stand_ins[] = {0x00463400u, 0x004634a0u, 0x00462830u, 0x00463114u};
    static const uint8_t slots[] = {1, 1, 1, 2};
    for (unsigned i = 0; i < 4; ++i) if (es2_sim_dispatch_add_service(&table, stand_ins[i], "hook", svc_hook, NULL, 0, slots[i])) return 1;
    const char *error = NULL;
    uint32_t bad = 0;
    return es2_sim_dispatch_seal(&table, &error, &bad);
}
static void fresh(void) {
    memset(image, 0, IMAGE_SIZE);
    w32(0x006cbee4u, COUNTER);
    w32(0x004a0e30u, 100);          /* the master volume (static 100) */
    w32(0x004a0e2cu, 0xffffffffu);  /* no CD device (static) */
    memset(heap_bytes, 0, HEAP_SIZE);
    memset(global_bytes, 0, GLOBAL_SIZE);
    memset(local, 0, LOCAL_SIZE);
    next_heap = HEAP_BASE;
    global_next = GLOBAL_BASE;
    g_frees = 0;
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
    es2_sim_audio_silent_init(&silent, &vt);
    es2_sim_audio_silent_device(&silent, &device);
    es2_sim_audio_headless(&audio, &loop, &device);
    audio.platform.global_alloc = g_alloc;
    audio.platform.global_free = g_free;
    es2_sim_audio_bind(&audio);
    refusals = 0;
    hooks = 0;
}
static int row_regs(uint32_t va, const uint32_t *slots, unsigned n, const uint32_t regs[3], es2_sim_result *r) {
    uint32_t at = local_guest + 0x20000u;
    for (unsigned i = 0; i < n; ++i) w32(at + 4u * i, slots[i]);
    memset(r, 0, sizeof(*r));
    return es2_sim_call(&table, va, (const uint32_t *)(uintptr_t)at, n, regs, r);
}
static int row(uint32_t va, const uint32_t *slots, unsigned n, es2_sim_result *r) {
    uint32_t regs[3] = {0x0e0e0e0eu, 0x0d0d0d0du, 0x0c0c0c0cu};
    return row_regs(va, slots, n, regs, r);
}

/* ---- 1. the driver load 0x473708 with the silent device */
static uint32_t settings(void) {
    uint32_t cfg = local_guest + 0x30000u;
    w16(cfg, 0x20); w16(cfg + 2u, 1); w16(cfg + 4u, 8); w16(cfg + 6u, 1); w16(cfg + 0xcu, 5); w16(cfg + 0xeu, 0x200);
    return cfg;
}
static int driver(void) {
    fresh();
    es2_sim_result r;
    uint32_t a[2] = {0x00010001u, settings()};
    CHECK(row(0x00473708u, a, 2, &r) == 0 && refusals == 0 && r.eax == 1u);
    CHECK(r32(0x006b5588u) == 0x00010001u && r16(0x004a6a98u) == 1u && r16(0x006cbd5cu) == 1u);
    CHECK(r16(0x004a0e44u) == 0x20u && r16(0x004a0e48u) == 8u && r16(0x004a0e4au) == 0x200u && r16(0x004a0e4cu) == 5u);
    CHECK(r16(0x006b5590u) == 1u && r16(0x006b5590u + 0x4cu) == 0x7fu && r16(0x006b5590u + 0x4eu) == 0x29u);
    CHECK(r32(0x006b5590u + 0x48u) == 0xfffu && r32(0x006b5590u + 0x56u) == 0x00010001u && r16(0x006b5590u + 0x6cu) == 5u);
    CHECK(r16(0x004a0e54u) == 1u && r16(0x004a0e58u) == 1u && r16(0x004a0e5au) == 0 && r32(0x004a0e50u) == 0);
    CHECK(silent.calls[ES2_SOS_INIT_DRIVER] == 1u && silent.calls[ES2_SOS_SET_ACTIVE_WINDOW] == 1u);
    /* A device without 22 kHz stereo 8 bits but with one of the classes 15h:
     * the fallback format 15h. */
    fresh();
    silent.formats = 0x004u;   /* 11 kHz mono 16 bits only: classes 10h, 2, 4 */
    a[1] = settings();
    CHECK(row(0x00473708u, a, 2, &r) == 0 && refusals == 0 && r.eax == 1u && r16(0x006b5590u + 0x4eu) == 0x15u);
    /* No format at all: status caps | 1C0000h on both tries (the configured
     * device, then device 1), the DLL released, EAX 0. */
    fresh();
    silent.formats = 0;
    a[1] = settings();
    CHECK(row(0x00473708u, a, 2, &r) == 0 && refusals == 0 && r.eax == 0);
    CHECK(r32(0x004a0e50u) == 0x001c0000u && r16(0x004a6a98u) == 0 && r16(0x004a0e54u) == 0 && silent.calls[ES2_SOS_DETECT_INIT] == 2u);
    return 0;
}

/* A context with `records` records and `slots` slots in the game heap. */
static uint32_t alloc(uint32_t n) {
    es2_sim_result r;
    uint32_t s[1] = {n};
    return row(0x0047749cu, s, 1, &r) ? 0 : r.eax;
}
static uint32_t context(uint32_t records, uint32_t slots) {
    uint32_t x = alloc(0x4cu), rec = alloc(540u * records), sl = alloc(40u * slots);
    w32(x + 0x10u, records); w32(x + 0x14u, slots); w32(x + 0x18u, 2000000u); w32(x + 0x20u, 1);
    w32(x + 0x34u, rec); w32(x + 0x38u, sl);
    for (uint32_t i = 0; i < records; ++i) w32(rec + 540u * i, 0xffffffffu);
    for (uint32_t i = 0; i < slots; ++i) w32(sl + 40u * i, 0xffffffffu);
    w32(0x0049f8ecu, x);
    return x;
}
/* A cached record f with a sample structure and a slot h on it. */
static void cached(uint32_t x, uint32_t ri, uint32_t si, uint32_t f, uint32_t h, uint32_t bytes) {
    uint32_t rec = r32(x + 0x34u) + 540u * ri, s = r32(x + 0x38u) + 40u * si, p = alloc(0x100u);
    w32(rec, f); text(rec + 4u, "HMI\\x.wav");
    w32(rec + 4u + 0x204u, 1); w32(rec + 4u + 0x208u, 1); w32(rec + 4u + 0x20cu, bytes); w32(rec + 4u + 0x210u, p);
    w32(p + 0x10u, bytes); w32(p + 0x38u, 11025u); w32(p + 0x3cu, 8); w32(p + 0x40u, 1);
    /* S = s + 4: +0 file, +4 flags, +8 priority, +0Ch callback 8, +10h volume,
     * +14h loop count 100, +18h pan 1, +1Ch rate 10000h. */
    w32(s, h); w32(s + 4u, f); w32(s + 8u, 0); w32(s + 0xcu, 0); w32(s + 0x10u, 8); w32(s + 0x14u, 0);
    w32(s + 0x18u, 100); w32(s + 0x1cu, 1); w32(s + 0x20u, 0x10000u);
}

/* ---- 2. the manager rows the sfx hooks bridge to */
static int manager(void) {
    fresh();
    es2_sim_result r;
    uint32_t a2[2] = {0x00010001u, settings()};
    CHECK(row(0x00473708u, a2, 2, &r) == 0 && r.eax == 1u);
    uint32_t x = context(4, 8), h = 0x00030002u, f = 0x00020001u;
    cached(x, 1, 2, f, h, 11025u);   /* one second at 11025 Hz, 8 bits, mono */
    uint32_t S = r32(x + 0x38u) + 40u * 2u + 4u;
    /* Volume while not playing: EAX = EDX = S, ECX the volume. */
    uint32_t v[3] = {x, h, 6000u};
    CHECK(row(0x00464618u, v, 3, &r) == 0 && r.eax == S && r.edx == S && r.ecx == 6000u && r32(S + 0x10u) == 6000u);
    /* Stale handle: EAX X, EDX h, ECX the index; -1: the entry ECX. */
    uint32_t vs[3] = {x, 0x00090002u, 1};
    CHECK(row(0x00464618u, vs, 3, &r) == 0 && r.eax == x && r.edx == 0x00090002u && r.ecx == 2u);
    uint32_t vm[3] = {x, 0xffffffffu, 1};
    CHECK(row(0x00464618u, vm, 3, &r) == 0 && r.eax == x && r.edx == 0xffffffffu && r.ecx == 0x0c0c0c0cu);
    /* Play: the glue's volume ((6000 * 100 * 32767) mod 2^32 / 10000 as a
     * word, twice), pan, repeat count 99, callback 8; EAX 1, ECX the record
     * index, flag 100h, playing count 1. */
    uint32_t p2[2] = {x, h};
    CHECK(row(0x00464038u, p2, 2, &r) == 0 && refusals == 0 && r.eax == 1u && r.ecx == 1u && r.edx == 8u);
    uint32_t P = r32(r32(x + 0x34u) + 540u + 4u + 0x210u), vol = 6000u * 100u;
    vol = ((vol << 15) - vol) / 10000u;
    uint32_t vv = (uint32_t)(int32_t)(int16_t)vol;
    CHECK(r32(P + 0x30u) == ((vv << 16) | vv) && r32(P + 0x34u) == 99u && r32(P + 0x48u) == 1u && r32(P + 0x64u) == 8u);
    CHECK((r32(S + 4u) & 0x100u) && r32(x + 0x3cu) == 1u && r16(S + 0x20u) == 0 && silent.sample[0].used);
    /* Rate and pan while playing: ECX = S, EDX = S's high word | the sample handle. */
    uint32_t rt[3] = {x, h, 0x8000u};
    CHECK(row(0x0046466cu, rt, 3, &r) == 0 && r.eax == 0 && r.ecx == S && r.edx == (S & 0xffff0000u) && (r32(S + 4u) & 0x400u));
    uint32_t pn[3] = {x, h, 0x8000u};
    CHECK(row(0x004646dcu, pn, 3, &r) == 0 && r.ecx == S && !(r32(S + 4u) & 0x800u) && r32(S + 0x18u) == 0x8000u);
    CHECK(silent.calls[ES2_SOS_SET_RATE] == 1u && silent.calls[ES2_SOS_SET_PAN] == 1u);
    /* Is playing (row index 3 holds h): yes until the playing time passed on
     * the virtual clock (100 repeats of one second: 100 s). */
    w32(0x004d2c78u + 12u, h);
    uint32_t i3[1] = {3};
    CHECK(row(0x00462af0u, i3, 1, &r) == 0 && r.eax == 1u);
    vt.clock_ms += 99000u;
    CHECK(row(0x00462af0u, i3, 1, &r) == 0 && r.eax == 1u);
    vt.clock_ms += 1000u;
    CHECK(row(0x00462af0u, i3, 1, &r) == 0 && r.eax == 0 && !(r32(S + 4u) & 0x100u) && r32(x + 0x3cu) == 0);
    CHECK(!silent.sample[0].used);
    /* Stop when not playing: 1, EDX the index, ECX the slot table. */
    CHECK(row(0x00464118u, p2, 2, &r) == 0 && r.eax == 1u && r.edx == 2u && r.ecx == r32(x + 0x38u));
    /* Play again, stop while playing: EDX/ECX the record index and 135 times it. */
    CHECK(row(0x00464038u, p2, 2, &r) == 0 && r.eax == 1u);
    CHECK(row(0x00464118u, p2, 2, &r) == 0 && r.eax == 1u && r.edx == 1u && r.ecx == 135u && r32(x + 0x3cu) == 0);
    /* -1: the entry EDX/ECX. */
    uint32_t pm[2] = {x, 0xffffffffu};
    CHECK(row(0x00464118u, pm, 2, &r) == 0 && r.eax == 0 && r.edx == 0x0d0d0d0du && r.ecx == 0x0c0c0c0cu);
    CHECK(row(0x00464038u, pm, 2, &r) == 0 && r.eax == 0 && r.edx == 0xffffffffu && r.ecx == 0x0c0c0c0cu);
    CHECK(refusals == 0);
    return 0;
}

/* ---- 3. the lip pass 0x4632c4 over the event queue */
static int lip_pass(void) {
    fresh();
    es2_sim_result r;
    /* Three queued records (time, channel) at 0x4d2e76 + 8k. */
    static const uint32_t times[3] = {0x00100010u, 0x00100005u, 0x00200000u};   /* clock units: (ms - [0x4d3b78]) >> 4 */
    for (uint32_t k = 0; k < 3u; ++k) {
        w32(0x004d2e76u + 8u * k, times[k]);
        w32(0x004d2e7au + 8u * k, 0x00a00000u + k);
        w32(0x004d2ec8u + 4u * k, 0x004d2e76u + 8u * k);
    }
    w16(0x0049f964u, 3);
    /* The clock 0x100000: from the top (k = 2) down, the first time not above
     * the clock: none. */
    vt.clock_ms = 0x01000000u;
    CHECK(row(0x004632c4u, NULL, 0, &r) == 0 && hooks == 0 && r16(0x0049f964u) == 3u && r.eax == 0x004d2ec4u);
    vt.clock_ms = 0x01000060u;
    int rr = row(0x004632c4u, NULL, 0, &r);
    if (hooks != 1u) fprintf(stderr, "lip pass: %d hooks %u count %u eax %08lx edx %08lx\n", rr, hooks, r16(0x0049f964u), (unsigned long)r.eax, (unsigned long)r.edx);
    CHECK(rr == 0 && hooks == 1u && hook_va[0] == 0x004634a0u && hook_arg[0] == 0x00a00001u);
    /* k = 1 was due: entries 1, 2 moved to 0, 1; count 3 - 2 = 1. */
    CHECK(r16(0x0049f964u) == 1u && r32(0x004d2ec8u) == 0x004d2e7eu && r32(0x004d2ecc) == 0x004d2e86u && r.ecx == 0x00a00001u);
    return 0;
}

/* ---- 4. the voice start without a context, the window handler, CD start */
static int voices(void) {
    fresh();
    es2_sim_result r;
    uint32_t ch = alloc(0x42u * 5u), lips = alloc(500u);
    w32(0x004d2d6cu, ch); w32(0x004d2d70u, lips); w16(0x004d2d74u, 5);
    for (uint32_t k = 0; k < 5u; ++k) { w32(ch + 0x42u * k, 0xffffffffu); w32(ch + 0x42u * k + 0x37u, 100u - k); }
    uint32_t wav = local_guest + 0x30000u;
    text(wav, "simvoc\\a.wav");
    vt.clock_ms = 0x01000000u;
    uint32_t a[2] = {wav, 0};
    CHECK(row(0x00462b9cu, a, 2, &r) == 0 && refusals == 0);
    uint32_t c4 = ch + 0x42u * 4u;   /* the lowest start time (96) */
    CHECK(r.eax == c4 && r32(c4 + 0x2fu) == lips + 400u && r8(lips + 400u) == 0xffu && r32(c4 + 0x33u) == c4 + 0xeu);
    CHECK(r8(c4 + 0x3bu) == 1u && r32(c4 + 0x37u) == 0x00100000u && r32(c4) == 0);   /* +0 = the handle +3Eh (0, zeroed) */
    /* The same name again: the same channel, a new start time. */
    vt.clock_ms = 0x01000100u;
    CHECK(row(0x00462b9cu, a, 2, &r) == 0 && r.eax == c4 && r32(c4 + 0x37u) == 0x00100010u);
    /* The voice wrapper without a context keeps the entry registers. */
    uint32_t va[2] = {wav, 50};
    CHECK(row(0x00462940u, va, 2, &r) == 0 && r.eax == 0x0e0e0e0eu && hooks == 0);
    /* The window handler: WM_TIMER without a context, another message. */
    uint32_t m[4] = {1, 0x113u, 0, 0};
    CHECK(row(0x00462398u, m, 4, &r) == 0 && r.eax == 0 && r.edx == 0);
    m[1] = 0x200u;
    CHECK(row(0x00462398u, m, 4, &r) == 0 && r.eax == 0 && r.edx == 0x200u - 0x3b9u);
    /* CD start with a context, a track and the gate: MCI_OPEN fails (no CD
     * audio): EAX 0, the device stays -1. */
    w32(0x0049f8ecu, 0x00123456u); w32(0x0049f8fcu, 3); w32(0x0049f8f4u, 1);
    CHECK(row(0x0046313cu, NULL, 0, &r) == 0 && r.eax == 0 && r32(0x004a0e2cu) == 0xffffffffu && silent.mci_calls == 1u);
    CHECK(refusals == 0);
    return 0;
}

/* ---- 5. the close 0x462734 after the driver load */
static int closing(void) {
    fresh();
    es2_sim_result r;
    uint32_t a2[2] = {0x00010001u, settings()};
    CHECK(row(0x00473708u, a2, 2, &r) == 0 && r.eax == 1u);
    uint32_t x = context(2, 2);
    w32(0x004d3b30u, 0x00462398u);   /* the handler as 0x462410 registered it */
    CHECK(row(0x00462734u, NULL, 0, &r) == 0 && refusals == 0);
    CHECK(r32(0x0049f8ecu) == 0 && r32(x + 0x34u) == 0 && r32(x + 0x38u) == 0 && r32(0x004d3b30u) == 0);
    CHECK(r16(0x004a6a98u) == 0 && r16(0x004a0e54u) == 0 && silent.calls[ES2_SOS_UNINIT_DRIVER] == 1u && r32(COUNTER) == 0);   /* + 1, then the destructor's - 1 */
    CHECK(g_frees == 0);   /* the close deletes the tables; cached samples stay (as in the original) */
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
    CHECK(setup_dispatch() == 0);
    if (driver() || manager() || lip_pass() || voices() || closing()) {
        fprintf(stderr, "first refused 0x%08lx\n", (unsigned long)first_refused);
        return 1;
    }
    puts("PASS: audio: driver load with the silent device, manager rows (volume, play, rate, pan, done on the virtual clock, "
         "stop), lip pass, voice start, window handler, CD start without CD audio");
    return 0;
}
