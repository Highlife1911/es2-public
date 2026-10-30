/* The main loop, the pump, the window procedure, the limiter and the clock, the exit
 * path and the DIB section's objects (sim/src/loop.c, frame_buffer.c) on a synthetic
 * image: zeroed host memory with the literals and tables the code reads written by
 * this test at the original's addresses, the game heap over an emulated OS region, a
 * call area mapped at its own host address. The sim's real services answer the heap
 * and string calls and the loop's own rows; the hooks and original functions the
 * loop calls are recording services. No game data. The comparison with the
 * original's machine code is //tools/reference-game/invitro:differential_loop_test. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "es2/sim/loop.h"
#include "es2/sim/memory.h"
#include "es2/sim/services.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

#define IMAGE_BASE 0x00400000u
#define IMAGE_SIZE 0x002d8000u
#define HEAP_BASE 0x09000000u
#define HEAP_SIZE 0x00400000u
#define LOCAL_SIZE 0x00080000u          /* call area 0..0x10000, the DIB bits 0x10000.. (640 x 480) */
#define COUNTER (IMAGE_BASE + 0x2d7ff0u)

static uint8_t *image, *heap_bytes, *local;
static uint32_t local_guest;
static es2_sim_arena arena;
static es2_guest_memory memory;
static es2_sim_heap heap;
static es2_sim_services services;
static es2_sim_entry storage[4096];
static es2_sim_dispatch table;
static es2_sim_loop loop;
static es2_vt vt;
static es2_sim_refusal last;
static unsigned refusals;
static uint32_t last_end;   /* a refusal at __exit or _abort */
static void on_refuse(const es2_sim_refusal *r, void *user) {
    (void)user; last = *r; ++refusals;
    if (r->va == 0x0049679cu || r->va == 0x0049688cu) last_end = r->va;
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

static uint32_t r32(uint32_t a) { return memory.read32(memory.context, a); }
static uint16_t r16(uint32_t a) { return memory.read16(memory.context, a); }
static uint8_t r8(uint32_t a) { return es2_sim_read8(&memory, a); }
static void w32(uint32_t a, uint32_t v) { memory.write32(memory.context, a, v); }
static void w16(uint32_t a, uint16_t v) { memory.write16(memory.context, a, v); }
static void w8(uint32_t a, uint8_t v) { es2_sim_write8(&memory, a, v); }
static void text(uint32_t a, const char *t) { do w8(a++, (uint8_t)*t); while (*t++); }

/* ---- recording services */
static char calls[1024];
static unsigned call_count;
static void note(char c) { if (call_count + 1 < sizeof(calls)) calls[call_count++] = c; calls[call_count] = 0; }
static uint32_t stage_args[64], logic_frames_left, logic_result, handler_answer, handler_messages[32], handler_wparams[32], frees[8];
static unsigned stage_count, handler_count, free_count;
static void svc_stage(es2_sim_service_call *c, void *u) { (void)u; note('s'); if (stage_count < 64) stage_args[stage_count++] = c->slots[0]; }
static void svc_logic(es2_sim_service_call *c, void *u) {
    (void)u; note('L');
    if (logic_frames_left) { --logic_frames_left; c->eax = 0xffff0000u; }   /* AX = 0: only the low word is tested */
    else c->eax = logic_result;
}
static void svc_note(es2_sim_service_call *c, void *u) {
    (void)u;
    switch (c->va) {
    case 0x004011a0u: note('c'); break;
    case 0x0045fca8u: note('r'); break;
    case 0x0045fba4u: note('p'); break;
    case 0x00462734u: note('S'); break;
    case 0x0046548cu: note('F'); break;
    case 0x00461ff0u: note('E'); w32(0x004d27b8u, 3); break;
    case 0x00459958u: note('6'); break;
    case 0x00459b80u: note('W'); break;
    case 0x0046317cu: note('P'); break;
    case 0x00463238u: note('Q'); break;
    case 0x00464874u: note('D'); break;
    case 0x00412340u: note('X'); break;   /* a test atexit function */
    default: note('?'); break;
    }
}
/* time_scale 0x467894 (hooks.md: reference low word first, elapsed second):
 * (e << 8) / ref clamped to 64..450, ECX the sign-extended reference, EDX
 * the remainder. */
static void svc_time_scale(es2_sim_service_call *c, void *u) {
    (void)u; note('t');
    int32_t ref = (int16_t)c->slots[0], e = (int16_t)c->slots[1];
    int32_t q = (e << 8) / ref, rem = (e << 8) % ref;
    uint32_t eax = (uint32_t)q;
    if ((int16_t)q > 0x1c2) eax = (eax & 0xffff0000u) | 0x1c2u;
    else if ((int16_t)q < 0x40) eax = (eax & 0xffff0000u) | 0x40u;
    c->eax = eax; c->ecx = (uint32_t)ref; c->edx = (uint32_t)rem;
}
/* A window handler (stdcall, four arguments). */
static void svc_handler(es2_sim_service_call *c, void *u) {
    (void)u; note('h');
    if (handler_count < 32) { handler_wparams[handler_count] = c->slots[2]; handler_messages[handler_count++] = c->slots[1]; }
    c->eax = handler_answer;
}
static void svc_free(es2_sim_service_call *c, void *u) { (void)u; note('f'); if (free_count < 8) frees[free_count++] = c->slots[0]; }
/* The shell leftovers' callees: the flight-key capture set 0x45b68c, the rectangle fill
 * 0x486704, the string draw 0x482634 (x, y and the text recorded), the
 * wingman index 0x433134. */
static uint32_t capture_args[4], fill_rects[4][4], fill_colours[4], text_xy[4][2], wingman_answer, wingman_arg;
static char texts[4][80];
static unsigned capture_count, fill_count, text_count;
static void svc_capture(es2_sim_service_call *c, void *u) {
    (void)u; note('k');
    if (capture_count < 4) capture_args[capture_count++] = c->slots[0];
    c->eax = 0x77u; c->edx = 0x88u; c->ecx = 0x99u;
}
static void svc_fill(es2_sim_service_call *c, void *u) {
    (void)u; note('R');
    if (fill_count < 4) {
        for (unsigned i = 0; i < 4; ++i) fill_rects[fill_count][i] = r32(c->slots[0] + 4u * i);
        uint32_t gc = r32(0x006c5f70u);
        fill_colours[fill_count++] = gc ? r32(r32(gc + 0x22cu) + 4u) : 0;
    }
}
static void svc_text(es2_sim_service_call *c, void *u) {
    (void)u; note('T');
    if (text_count < 4) {
        text_xy[text_count][0] = r32(c->slots[1]); text_xy[text_count][1] = r32(c->slots[1] + 4u);
        unsigned i = 0;
        for (; i + 1u < sizeof(texts[0]) && r8(c->slots[2] + i); ++i) texts[text_count][i] = (char)r8(c->slots[2] + i);
        texts[text_count][i] = 0;
        ++text_count;
    }
    c->eax = 0x1234u + text_count; c->edx = 0x55u; c->ecx = 0x66u;
}
static void svc_wingman(es2_sim_service_call *c, void *u) { (void)u; note('w'); wingman_arg = c->slots[0]; c->eax = wingman_answer; }
/* 0x492b90(name, ext) (load.c's row needs the load layer): the name and ext
 * recorded, EAX a buffer with "voice.vol". */
static uint32_t path_args[2];
static void svc_path_ext(es2_sim_service_call *c, void *u) {
    (void)u; note('x'); path_args[0] = c->slots[0]; path_args[1] = c->slots[1];
    text(0x004c9000u, "voice.vol"); c->eax = 0x004c9000u;
}

static int setup_dispatch(void) {
    es2_sim_dispatch_init(&table, storage, 4096, on_refuse, NULL);
    services.memory = &memory;
    services.heap = &heap;
    services.vt = &vt;
    services.fs0 = NULL;
    if (es2_sim_register_services(&table, &services)) return 1;
    struct { uint32_t va; es2_sim_service_fn f; uint8_t slots; uint16_t pop; } extra[] = {
        {0x00401d94u, svc_stage, 1, 0}, {0x0045f570u, svc_logic, 0, 0}, {0x004011a0u, svc_note, 1, 0},
        {0x0045fca8u, svc_note, 0, 0}, {0x0045fba4u, svc_note, 0, 0}, {0x00462734u, svc_note, 0, 0},
        {0x0046548cu, svc_note, 0, 0}, {0x00461ff0u, svc_note, 0, 0}, {0x00459958u, svc_note, 3, 0},
        {0x00459b80u, svc_note, 1, 0}, {0x0046317cu, svc_note, 0, 0}, {0x00463238u, svc_note, 0, 0},
        {0x00464874u, svc_note, 1, 0}, {0x00412340u, svc_note, 0, 0}, {0x00467894u, svc_time_scale, 2, 0},
        {0x00477becu, svc_handler, 4, 16}, {0x004809f8u, svc_handler, 4, 16},
        {0x0045b68cu, svc_capture, 1, 0}, {0x00486704u, svc_fill, 1, 0}, {0x00482634u, svc_text, 3, 0},
        {0x00433134u, svc_wingman, 1, 0}, {0x00492b90u, svc_path_ext, 2, 0},
    };
    for (unsigned i = 0; i < sizeof(extra) / sizeof(extra[0]); ++i) {
        /* A row of services.h at the same VA (0x459958, 0x4809f8) takes the test's function. */
        unsigned k = 0;
        while (k < table.count && storage[k].va != extra[i].va) ++k;
        if (k < table.count) { storage[k].service = extra[i].f; storage[k].service_user = NULL; continue; }
        if (es2_sim_dispatch_add_service(&table, extra[i].va, "test", extra[i].f, NULL, extra[i].pop, extra[i].slots)) return 1;
    }
    /* _free records (the row's model would need CW3220's blocks). */
    for (unsigned i = 0; i < table.count; ++i)
        if (storage[i].va == 0x004967a8u) storage[i].service = svc_free;
    const char *error = NULL;
    uint32_t bad = 0;
    return es2_sim_dispatch_seal(&table, &error, &bad);
}

/* The literals and tables the loop and the DIB section read (each at its
 * VA in the reference image; the values here are this test's). */
static void image_literals(void) {
    memset(image, 0, IMAGE_SIZE);
    w32(0x006cbee4u, COUNTER);
    text(0x004a0841u, "BMP_8");
    text(0x004a0847u, "VGA_8");
    text(0x004a2ea4u, "VGA_8"); text(0x004a2eaau, "VGA_4"); text(0x004a2eb0u, "BMP_8");
    w32(0x004a2e88u, 0x004a2ea4u); w32(0x004a2e8cu, 0x004a2eaau); w32(0x004a2e90u, 0x004a2eb0u);
    w32(0x004a2e94u, 1); w32(0x004a2e98u, 0); w32(0x004a2e9cu, 3); w32(0x004a2ea0u, 4);
    w32(0x004a2f24u, 9);
    /* The driver pairs and the BMP_8 table (0x24 slots). */
    w32(0x004a5840u, 0); w32(0x004a5844u, 0); w32(0x004a5848u, 1); w32(0x004a584cu, 0);
    w32(0x004a5850u, 3); w32(0x004a5854u, 0x004a586cu); w32(0x004a5858u, 0xffffffffu);
    w32(0x004a583cu, 0x24u);
    static const uint32_t slots[0x24] = {
        0x489f84, 0x48a016, 0x48a6c4, 0x48a74c, 0x48a340, 0x48a361, 0x48a384, 0x48a324, 0x48a1cc, 0x489830, 0x489836,
        0x48a1d4, 0x48a32c, 0x48978c, 0x4897cb, 0x48990e, 0x4898fc, 0x489902, 0x489908, 0x48a08a, 0x48a7a7, 0x48a440,
        0x48a48e, 0x48a4d4, 0x48a5b3, 0x48a601, 0x4899ac, 0x489c6e, 0x489ede, 0x48afc8, 0x4898d4, 0x4898df, 0x4898f6,
        0x48a924, 0x48a3b0, 0x48a8e2};
    for (unsigned i = 0; i < 0x24u; ++i) w32(0x004a586cu + 4u * i, slots[i]);
    /* Video mode 0: game 320 x 240, DIB 640 x 480 (the display record's copy). */
    w32(0x004d253eu, 320); w32(0x004d2542u, 240);
    w32(0x004d3040u, 640); w32(0x004d3044u, 480);
    w32(0x004d3610u, 0x00010001u);
    w32(0x004a03c8u, 0xffffffffu); w32(0x004a03ccu, 0xffffffffu);
    /* A system palette entry to see the byte swap (bytes 0 and 2). */
    w8(0x004d3074u, 0x11); w8(0x004d3076u, 0x33);
    text(0x0049f4d2u, "tapes\\prefs.cfg"); text(0x0049f4e2u, "tapes\\keyjoy.cfg");
}

static unsigned ticks_seen, stop_at;
static int on_tick(es2_sim_loop *l, void *u) {
    (void)u;
    ++ticks_seen;
    return stop_at && r32(ES2_SIM_VA_FRAME_COUNTER) + 1u >= stop_at && l;
}
static uint32_t translate(void *u, uint32_t vk, uint32_t scan) { (void)u; (void)scan; return vk == 0x57u ? 'w' : vk == 0x0du ? 13u : 0u; }
/* A German layout's answer: VK 0xdc ('^') is a dead key; a key after it that
 * does not combine gives two characters, the dead key's first; VK 0x30 gives
 * the character 0 (flag ONE). */
static unsigned dead_pending;
static uint32_t translate_dead(void *u, uint32_t vk, uint32_t scan) {
    (void)u; (void)scan;
    if (vk == 0xdcu) { dead_pending = 1; return ES2_SIM_TRANSLATE_DEAD | '^'; }
    if (vk == 0x30u) return ES2_SIM_TRANSLATE_ONE;
    if (vk != 0x57u) return 0;
    if (dead_pending) { dead_pending = 0; return ES2_SIM_TRANSLATE_TWO | '^' | ('w' << 8); }
    return 'w';
}
static char deleted[2][64];
static unsigned delete_count;
static int delete_file(void *u, const char *name) { (void)u; if (delete_count < 2) strcpy(deleted[delete_count], name); ++delete_count; return 1; }

static void fresh(void) {
    image_literals();
    memset(heap_bytes, 0, HEAP_SIZE);
    memset(local, 0, LOCAL_SIZE);
    next_heap = HEAP_BASE;
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
    loop.platform.bits = local_guest + 0x10000u;
    loop.platform.bits_size = 640u * 480u;
    loop.platform.translate = translate;
    loop.tick = on_tick;
    loop.delete_file = delete_file;
    es2_sim_loop_bind(&loop);
    call_count = 0; calls[0] = 0;
    stage_count = handler_count = free_count = refusals = ticks_seen = stop_at = delete_count = 0;
    logic_frames_left = 0; logic_result = 1; handler_answer = 0;
    capture_count = fill_count = text_count = dead_pending = 0;
    wingman_answer = wingman_arg = 0;
}

/* ---- 1. the DIB section and the surface release */
static int dib(void) {
    fresh();
    CHECK(es2_sim_dib_section(&loop) == 0 && refusals == 0);
    const uint32_t d = 0x004d3020u;
    /* The GL table registered (pair 0), the header, the DIB, the colours. */
    CHECK(r32(0x004a1690u) == 1 && r32(0x006c5834u) == 0x004a2eb8u && r32(0x006c5838u) == 9);
    CHECK(r32(d + 0x2cu) == 0x28u && r16(d + 0x38u) == 1 && r32(d + 0x30u) == 640 && r32(d + 0x34u) == (uint32_t)-480);
    CHECK(r32(d + 0x40u) == 640u * 480u && r32(d + 8u) == loop.platform.bits && r32(d + 0x18u) == loop.platform.dib);
    CHECK(r32(d + 0x14u) == loop.platform.dc_memory && r32(d + 0x1cu) == loop.platform.old_bitmap);
    CHECK(r8(0x004d3074u) == 0x33 && r8(0x004d3076u) == 0x11);
    /* The descriptor: height, width, 8 bits, flags 0x10, size, bits. */
    uint32_t desc = r32(d);
    CHECK(desc && r16(desc) == 480 && r16(desc + 2u) == 640 && r8(desc + 4u) == 8 && r8(desc + 5u) == 0x10);
    CHECK(r32(desc + 6u) == 640u * 480u && r32(desc + 0xau) == loop.platform.bits);
    /* The surface: attached, format BMP_8 (3), current; size forced to the
     * game size, the descriptor's in +0x28/+0x2c; the row table. */
    uint32_t s = r32(d + 4u);
    CHECK(s && r32(s) == 1 && r32(s + 0x30u) == 3 && r32(0x006c5ed0u) == s);
    CHECK(r32(s + 8u) == 320 && r32(s + 0xcu) == 240 && r32(s + 0x28u) == 640 && r32(s + 0x2cu) == 480);
    CHECK(r32(s + 0x10u) == 1 && r32(s + 0x14u) == 8 && r32(s + 0x18u) == 0x100 && r32(s + 0x24u) == 1);
    CHECK(r32(0x004a03c8u) == 0xffffffffu && r32(0x004a03ccu) == 0xffffffffu);
    uint32_t rows = r32(s + 4u);
    CHECK(rows && rows == r32(0x004a03d0u) && r32(rows) == loop.platform.bits && r32(rows + 4u) == 0 &&
          r32(rows + 8u) == 640 && r32(rows + 4u * 480u) == 479u * 640u);
    CHECK(r32(0x004d3000u) == 319 && r32(0x004d3004u) == 239 && r32(0x004d3008u) == 640 && r32(0x004d300cu) == 480);
    /* The clip records, the current state copies, the driver table copy. */
    uint32_t p1 = r32(s + 0x40u), p2 = r32(s + 0x44u);
    CHECK(p1 && p2 && p1 != p2 && r32(p1 + 0x214u) == 319 && r32(p1 + 0x218u) == 239 && r32(p1 + 0x224u) == 0x004a3604u);
    CHECK(r32(p2 + 0x228u) == 0x004a360cu && r32(0x004a3614u) == p2 && r32(0x004a35ecu) == 319);
    CHECK(r32(0x004a57acu + 4u * 11u) == 0x0048a1d4u && r32(0x004a3652u) == rows && r32(0x004a3656u) == 320);
    CHECK(r32(s + 0x8cu) == 1 && r32(s + 0x3cu) == 1 && r32(0x004a368au) == 1);
    /* Abort notify and atexit; the counter unchanged; the surface count 1. */
    uint32_t node = r32(ES2_SIM_VA_NOTIFY_HEAD);
    CHECK(node && r32(node) == 0 && r32(node + 4u) == ES2_SIM_VA_SURFACE_NOTIFY);
    CHECK(loop.atexit_count == 1 && loop.atexit_table[0] == ES2_SIM_VA_SURFACE_RELEASE);
    CHECK(r32(COUNTER) == 0 && r32(0x004a2e2cu) == 1);
    CHECK(loop.platform.create_dib == 1 && loop.platform.get_dc == 1);
    /* The surface's name copied (strncpy 15). */
    CHECK(r8(s + 0x90u) == 'B' && r8(s + 0x94u) == '8' && r8(s + 0x95u) == 0);
    /* The release: delete p1, [s] = 0, the row table freed through _free, count 0. */
    CHECK(es2_sim_surface_release(&loop) == 0 && refusals == 0);
    CHECK(r32(s) == 0 && r32(0x004a2e2cu) == 0 && free_count == 1 && frees[0] == rows);
    /* A DIB larger than the host region is refused by name. */
    fresh();
    loop.platform.bits_size = 100;
    CHECK(es2_sim_dib_section(&loop) == 1 && refusals == 1 && last.va == 0x00464e1fu);
    return 0;
}

/* ---- 2. virtual time: timing init, clock, limiter */
static int timing(void) {
    fresh();
    uint32_t base = es2_sim_timing_init(&loop);
    CHECK(base == 0x01000000u && r32(0x004d3b78u) == base && r32(0x004d3b74u) == base);
    CHECK(r16(0x004d3b64u) == 256 && r16(0x004d3b66u) == 256 && r16(0x004d3b70u) == 1000 && r16(0x004d3b72u) == 125);
    CHECK(r16(0x004d3b68u) == 125 && r16(0x004d3b6au) == 125 && r32(0x004d3b6cu) == 125);
    /* Intra-frame reads: the second read of a frame is 1 ms later. */
    CHECK(es2_sim_clock(&loop) == 0 && es2_sim_clock(&loop) == 0);
    /* One limiter pass: one poll, e = 47, both scales 47*256/125 = 96. */
    CHECK(es2_sim_limiter(&loop) == 0 && vt.limiter_polls == 1);
    CHECK(r32(0x004d3b6cu) == 47 && r32(0x004d3b74u) == base + 47 && r16(0x004d3b64u) == 96 && r16(0x004d3b66u) == 96);
    CHECK(es2_sim_clock(&loop) == 47u >> 4);
    /* The service rows answer through the dispatch with the original's registers. */
    es2_sim_result r;
    uint32_t regs[3] = {0, 0x55u, 0x66u};
    CHECK(!es2_sim_call(&table, 0x00467840u, NULL, 0, regs, &r) && r.edx == 125 && r.ecx == 0x66u);
    CHECK(!es2_sim_call(&table, 0x00467828u, NULL, 0, regs, &r) && r.edx == 0x55u);
    return 0;
}

/* ---- 3. the pump with the journal, the window procedure and its handlers */
static int pump(void) {
    fresh();
    static const char lines[] =
        "{\"f\":0,\"api\":\"msg\",\"a\":256,\"b\":87,\"c\":1114113}\n"   /* W down: a WM_CHAR follows */
        "{\"f\":0,\"api\":\"msg\",\"a\":257,\"b\":87,\"c\":-1072758783}\n"
        "{\"f\":0,\"p\":4,\"api\":\"msg\",\"a\":512,\"b\":0,\"c\":1310730}\n"  /* a mouse move at peek 5 */
        "{\"f\":1,\"api\":\"msg\",\"a\":260,\"b\":121,\"c\":1}\n"          /* F10 down (SYSKEYDOWN): no character */
        "{\"f\":1,\"api\":\"msg\",\"a\":261,\"b\":121,\"c\":1}\n";         /* F10 up: menu activation, counted */
    es2_journal j;
    es2_journal_init(&j);
    CHECK(!es2_journal_load_text(&j, lines, sizeof(lines) - 1u, NULL) && j.count == 5);
    loop.journal = &j;
    w32(0x004d3720u, 0x00010003u);
    w32(ES2_SIM_VA_HANDLERS + 4u, 0x00477becu);   /* a handler in slot 1, slot 0 empty */
    uint32_t result = 7;
    /* First call: W down, its WM_CHAR, W up, then the empty peek (count 3);
     * the mouse move is keyed 4 and waits. */
    CHECK(es2_sim_pump(&loop, &result) == 0 && result == 0);
    CHECK(loop.platform.peeks == 4 && j.injected == 2 && handler_count == 3 && handler_messages[0] == 0x100u &&
          handler_messages[1] == 0x101u && handler_messages[2] == 0x102u && j.keys[0x57] == 0x01);
    CHECK(loop.platform.def_window_proc == 3 && loop.platform.set_cursor == 1 && loop.platform.last_cursor == 0x00010003u);
    /* Second call in the same frame: peek 5 delivers the mouse move. */
    CHECK(es2_sim_pump(&loop, &result) == 0 && j.injected == 3 && j.mouse_x == 10 && j.mouse_y == 20 && loop.platform.peeks == 6);
    /* A handler that answers nonzero ends the procedure before DefWindowProc. */
    handler_answer = 1;
    w32(ES2_SIM_VA_FRAME_COUNTER, 1);
    uint32_t before = loop.platform.def_window_proc;
    es2_journal_entry *keep = &j.entries[4];
    keep->a = 0x101;   /* make the second F10 message a plain key-up */
    CHECK(es2_sim_pump(&loop, &result) == 0 && loop.platform.def_window_proc == before && j.injected == 5);
    /* F10 and Alt released through DefWindowProc: counted, no effect. */
    handler_answer = 0;
    uint32_t r = 7, menus = loop.platform.menu_keys;
    CHECK(es2_sim_window_proc(&loop, 1, 0x105u, 0x79u, 1, &r) == 0 && r == 0 && loop.platform.menu_keys == menus + 1u);
    CHECK(es2_sim_window_proc(&loop, 1, 0x105u, 0x12u, 1, &r) == 0 && loop.platform.menu_keys == menus + 2u);
    CHECK(es2_sim_window_proc(&loop, 1, 0x105u, 0x41u, 1, &r) == 0 && loop.platform.menu_keys == menus + 2u);
    fresh();
    /* WM_QUIT: returned at once (no cursor), after the posted messages. */
    w32(0x004d3720u, 0x00010003u);
    es2_sim_post_quit(&loop);
    CHECK(es2_sim_pump(&loop, &result) == 0 && result == 1 && loop.platform.set_cursor == 0 && loop.platform.peeks == 1);
    /* A platform post (an SOS notice) is dispatched before WM_QUIT, one peek
     * each; a full queue refuses the post. */
    fresh();
    w32(ES2_SIM_VA_HANDLERS + 4u, 0x00477becu);
    CHECK(es2_sim_loop_post(&loop, 1, 0xa000u, 0xa003u, 0x14000u) == 0);
    es2_sim_post_quit(&loop);
    CHECK(es2_sim_pump(&loop, &result) == 0 && result == 1 && loop.platform.peeks == 2 && handler_count == 1 &&
          handler_messages[0] == 0xa000u && handler_wparams[0] == 0xa003u);
    for (uint32_t i = 0; i < ES2_SIM_POSTED_DEPTH; ++i) CHECK(es2_sim_loop_post(&loop, 1, 0xa000u, 0xa003u, i) == 0);
    CHECK(es2_sim_loop_post(&loop, 1, 0xa000u, 0xa003u, ES2_SIM_POSTED_DEPTH) == 1 && loop.posted_count == ES2_SIM_POSTED_DEPTH);
    fresh();
    /* Focus: pause and resume (no cockpit: no ports). */
    CHECK(es2_sim_window_proc(&loop, 1, ES2_WM_KILLFOCUS, 0, 0, &r) == 0 && r8(0x004d2572u) == 1);
    CHECK(es2_sim_window_proc(&loop, 1, ES2_WM_SETFOCUS, 0, 0, &r) == 0 && r8(0x004d2572u) == 0 && !strcmp(calls, "PQ"));
    CHECK(es2_sim_window_proc(&loop, 1, ES2_WM_SYSCHAR, 'x', 0, &r) == 0 && loop.platform.def_window_proc == 0);
    /* WM_CLOSE: demo playback only stops; otherwise the quit dialog is refused. */
    w16(0x004d2530u, 1);
    CHECK(es2_sim_window_proc(&loop, 1, ES2_WM_CLOSE, 0, 0, &r) == 0 && r16(0x004d2532u) == 1);
    w16(0x004d2530u, 0);
    CHECK(es2_sim_window_proc(&loop, 1, ES2_WM_CLOSE, 0, 0, &r) == 1 && last.va == 0x0045feb8u);
    /* The handler list rows: add fills the first hole, remove clears the first match. */
    es2_sim_result out;
    uint32_t slots[1] = {0x11111111u}, regs[3] = {0, 0, 0};
    w32(ES2_SIM_VA_HANDLERS, 0x22222222u);
    CHECK(!es2_sim_call(&table, 0x00465fecu, slots, 1, regs, &out) && r32(ES2_SIM_VA_HANDLERS + 4u) == 0x11111111u &&
          out.eax == ES2_SIM_VA_HANDLERS + 4u && out.edx == 1 && out.ecx == 0x11111111u);
    CHECK(!es2_sim_call(&table, 0x00466010u, slots, 1, regs, &out) && r32(ES2_SIM_VA_HANDLERS + 4u) == 0 && out.ecx == 0 &&
          out.edx == 1);
    slots[0] = 0x33333333u;
    CHECK(!es2_sim_call(&table, 0x00466010u, slots, 1, regs, &out) && out.ecx == 0x33333333u && out.edx == 10 &&
          out.eax == ES2_SIM_VA_HANDLERS + 40u);
    es2_journal_free(&j);
    return 0;
}

/* ---- 4. the main loop, the tail and the exit */
static int main_loop(void) {
    fresh();
    uint32_t ebp = local_guest + 0x8000u;
    es2_sim_timing_init(&loop);
    /* Three frames, then the logic frame ends the mission. */
    logic_frames_left = 3;
    logic_result = 0x00000001u;
    w32(0x004d24eau, 0x1234u);
    w32(0x004d255eu, 1);   /* DirectDraw on, prefs byte 6 off: the prefs write */
    CHECK(es2_sim_main_loop(&loop, ebp) == ES2_SIM_LOOP_RETURNED && refusals == 0);
    CHECK(!strcmp(calls, "ssttLcrpssttLcrpssttLcrpssttLSF6WE"));
    CHECK(stage_args[0] == 3 && stage_args[1] == 4 && stage_args[2] == 6 && ticks_seen == 3 && r32(ES2_SIM_VA_FRAME_COUNTER) == 3);
    CHECK(vt.limiter_polls == 4 && r16(0x004d2564u) == 1 && r32(0x004d3720u) == loop.platform.cursor_arrow);
    CHECK(r32(0x004d27b8u) == 3 && r32(COUNTER) == 0xffffffffu);   /* the holder's counter decrement */
    /* frame_limit: the callback stops after the tick; the counter still moves. */
    fresh();
    es2_sim_timing_init(&loop);
    logic_frames_left = 100;
    stop_at = 2;
    CHECK(es2_sim_main_loop(&loop, ebp) == ES2_SIM_LOOP_STOPPED && ticks_seen == 2 && r32(ES2_SIM_VA_FRAME_COUNTER) == 2);
    /* Tape playback: no limiter, the quit word ends it, the two deletes. */
    fresh();
    w16(0x004d24d6u, 1);
    w16(0x004d24feu, 1);
    CHECK(es2_sim_main_loop(&loop, ebp) == ES2_SIM_LOOP_RETURNED && vt.limiter_polls == 0 && ticks_seen == 0);
    CHECK(delete_count == 2 && !strcmp(deleted[0], "tapes\\prefs.cfg") && !strcmp(deleted[1], "tapes\\keyjoy.cfg"));
    /* The pause path: Sleep, the pump, then the quit word. */
    fresh();
    es2_sim_timing_init(&loop);
    w8(0x004d2572u, 1);
    es2_sim_post_quit(&loop);
    CHECK(es2_sim_main_loop(&loop, ebp) == ES2_SIM_LOOP_RETURNED && loop.platform.sleeps == 1 && ticks_seen == 0);
    /* A refused callee (no present frame) stops the loop by name. */
    fresh();
    es2_sim_timing_init(&loop);
    logic_frames_left = 1;
    w32(0x004d3608u, 1);   /* a timer: the tail refuses KillTimer */
    w16(0x004d24feu, 0);
    CHECK(es2_sim_main_loop(&loop, ebp) == ES2_SIM_LOOP_REFUSED && last.va == 0x004650d9u);
    /* Exit: the atexit functions last first (the surface release is own
     * code), then the exit records. */
    fresh();
    CHECK(es2_sim_atexit(&loop, 0x00412340u) == 0 && es2_sim_atexit(&loop, ES2_SIM_VA_SURFACE_RELEASE) == 0);
    for (unsigned i = 2; i < 32u; ++i) CHECK(es2_sim_atexit(&loop, 0x00412340u) == 0);
    CHECK(es2_sim_atexit(&loop, 0x00412340u) == 1 && loop.atexit_count == 32);
    CHECK(es2_sim_exit(&loop, NULL, NULL) == 0 && loop.atexit_count == 0 && call_count == 31);
    return 0;
}

/* ---- 5. TranslateMessage's dead keys and the shell leftovers' rows */
static void label_setup(void) {
    w32(0x004d1e2cu, 0x004c1000u); w16(0x004c100au, 8);   /* the font, height 8 */
    w32(0x004cfa4cu, 100);                                 /* the view rectangle's top */
    w32(0x006c5f70u, 0x004c2000u); w32(0x004c2208u, 0xaaaau); w32(0x004c220cu, 0xbbbbu);
    w16(0x004d3ba2u, 5); w16(0x0049f02eu, 10); w16(0x0049f030u, 100); w16(0x004d253au, 1);
    w32(0x004d24eau, 0x004c3000u); w32(0x004c3032u, 0x004c4000u); w32(0x004d24e6u, 0x004c4000u);
    w32(0x004d14dcu, 0x004c5000u); text(0x004c5000u, "Ace");
    w32(0x004d16a0u, 0x004c6000u); text(0x004c6000u, "Pilot ");
    w32(0x004d16a4u, 0x004c6010u); text(0x004c6010u, "Mode ");
    w32(0x004d16a8u, 0x004c6020u); text(0x004c6020u, "on");
    w32(0x004d16acu, 0x004c6030u); text(0x004c6030u, "off");
    w32(0x0049d230u, 0x004c7000u); w32(0x004c7000u + 334u + 0x264u, 0x004c8000u); text(0x004c8000u, "Bo");
}
static int shell_rest(void) {
    /* Dead keys: '^' gives WM_DEADCHAR, the next W two WM_CHARs ('^', 'w'),
     * all after the journal's four; the empty peek last (8 peeks). */
    fresh();
    loop.platform.translate = translate_dead;
    static const char lines[] =
        "{\"f\":0,\"api\":\"msg\",\"a\":256,\"b\":220,\"c\":2686977}\n"
        "{\"f\":0,\"api\":\"msg\",\"a\":257,\"b\":220,\"c\":-1071054847}\n"
        "{\"f\":0,\"api\":\"msg\",\"a\":256,\"b\":87,\"c\":1114113}\n"
        "{\"f\":0,\"api\":\"msg\",\"a\":257,\"b\":87,\"c\":-1072758783}\n"
        "{\"f\":1,\"api\":\"msg\",\"a\":260,\"b\":220,\"c\":539623425}\n"   /* Alt+^: WM_SYSDEADCHAR */
        "{\"f\":1,\"api\":\"msg\",\"a\":256,\"b\":48,\"c\":720897}\n";      /* '0' answered as the character 0 */
    es2_journal j;
    es2_journal_init(&j);
    CHECK(!es2_journal_load_text(&j, lines, sizeof(lines) - 1u, NULL) && j.count == 6);
    loop.journal = &j;
    w32(ES2_SIM_VA_HANDLERS, 0x00477becu);
    uint32_t result = 7;
    CHECK(es2_sim_pump(&loop, &result) == 0 && result == 0 && refusals == 0);
    CHECK(loop.platform.peeks == 8 && handler_count == 7);
    static const uint32_t order[7] = {0x100u, 0x101u, 0x100u, 0x101u, 0x103u, 0x102u, 0x102u};
    for (unsigned i = 0; i < 7; ++i) CHECK(handler_messages[i] == order[i]);
    CHECK(handler_wparams[4] == '^' && handler_wparams[5] == '^' && handler_wparams[6] == 'w');
    w32(ES2_SIM_VA_FRAME_COUNTER, 1);
    handler_count = 0;
    handler_answer = 1;   /* the handler takes WM_SYSKEYDOWN (no menu path) */
    CHECK(es2_sim_pump(&loop, &result) == 0 && refusals == 0 && handler_count == 4);
    CHECK(handler_messages[0] == 0x104u && handler_messages[1] == 0x100u && handler_messages[2] == 0x107u &&
          handler_wparams[2] == '^' && handler_messages[3] == 0x102u && handler_wparams[3] == 0);
    es2_journal_free(&j);

    es2_sim_result out;
    uint32_t regs[3] = {0x11u, 0x22u, 0x12345600u}, slots[2] = {0, 0};
    /* The voice archive name: the letter, except 'r'; EDX sign-extended. */
    fresh();
    text(0x0049f259u, "simvoice");
    w8(0x004d2536u, 'g');
    CHECK(!es2_sim_call(&table, 0x0045ef84u, NULL, 0, regs, &out) && out.eax == 0x0049f259u && out.edx == 'g' &&
          out.ecx == 0x12345600u && r8(0x0049f260u) == 'g');
    w8(0x004d2536u, 'r');
    CHECK(!es2_sim_call(&table, 0x0045ef84u, NULL, 0, regs, &out) && out.edx == 'r' && r8(0x0049f260u) == 'g');
    w8(0x004d2536u, 0xe4u);
    CHECK(!es2_sim_call(&table, 0x0045ef84u, NULL, 0, regs, &out) && out.edx == 0xffffffe4u && r8(0x0049f260u) == 0xe4u);
    /* The language path: "st<letter>\\" + 0x492b90(name, ext). */
    w8(0x004d2536u, 'f');
    text(0x0049f255u, "st?"); text(0x0049f3fcu, "\\");
    slots[0] = 0x004c9100u; slots[1] = 0x004c9200u;
    CHECK(!es2_sim_call(&table, 0x0045ef1cu, slots, 2, regs, &out) && out.eax == 0x004d291cu && refusals == 0);
    CHECK(path_args[0] == 0x004c9100u && path_args[1] == 0x004c9200u && r8(0x0049f257u) == 'f');
    {
        char got[32];
        unsigned i = 0;
        for (; i < 31u && r8(0x004d291cu + i); ++i) got[i] = (char)r8(0x004d291cu + i);
        got[i] = 0;
        CHECK(!strcmp(got, "stf\\voice.vol"));
    }
    /* The cursor store, the empty body, the timer kill. */
    slots[0] = 0x00010007u;
    CHECK(!es2_sim_call(&table, 0x00464c68u, slots, 1, regs, &out) && r32(0x004d3720u) == 0x00010007u &&
          out.eax == 0x00010007u && out.edx == 0x22u);
    CHECK(!es2_sim_call(&table, 0x004650ccu, NULL, 0, regs, &out) && out.eax == 0x11u && out.ecx == 0x12345600u);
    CHECK(!es2_sim_call(&table, 0x004650d0u, NULL, 0, regs, &out) && out.eax == 0 && refusals == 0);
    w32(0x004d3608u, 3);
    CHECK(es2_sim_call(&table, 0x004650d0u, NULL, 0, regs, &out) && refusals == 2 && loop.refused_va == 0x004650d9u &&
          last.va == 0x004650d0u);
    /* The HUD sub-object. */
    fresh();
    w32(0x0049b088u, 0x004c1000u); w32(0x004c11e1u, 0x00abcdefu);
    CHECK(!es2_sim_call(&table, 0x004610adu, NULL, 0, regs, &out) && out.eax == 0x00abcdefu && out.edx == 0x22u);
    /* The capture push and pop: the flag saved per depth, 0x45b68c(0) and
     * 0x45b68c(the saved flag in CL, ECX's upper bytes kept). */
    w8(0x0049eaa5u, 1); w8(0x0049f704u, 2);
    CHECK(!es2_sim_call(&table, 0x00461250u, NULL, 0, regs, &out) && r8(0x0049f704u) == 3 && r8(0x004d2a82u) == 1 &&
          capture_count == 1 && capture_args[0] == 0 && out.eax == 0x77u && out.edx == 0x88u && out.ecx == 0);
    w8(0x0049eaa5u, 0);
    CHECK(!es2_sim_call(&table, 0x00461274u, NULL, 0, regs, &out) && r8(0x0049f704u) == 2 && capture_count == 2 &&
          capture_args[1] == 0x12345601u && out.ecx == 0x12345601u && out.eax == 0x77u);

    /* The subject label: the player's name, the value with mode 0. */
    fresh();
    label_setup();
    uint32_t hi = loop.stack_hi;
    CHECK(!es2_sim_call(&table, 0x0045e208u, NULL, 0, regs, &out) && refusals == 0 && !strcmp(calls, "RTT"));
    CHECK(r8(0x0049f211u) == 1 && r32(0x004d27a4u) == 0x004c1000u && loop.stack_hi == hi);
    CHECK(fill_rects[0][0] == 0 && fill_rects[0][1] == 165 && fill_rects[0][2] == 319 && fill_rects[0][3] == 173 &&
          fill_colours[0] == 5);
    CHECK(text_xy[0][0] == 20 && text_xy[0][1] == 173 && !strcmp(texts[0], "Pilot Ace"));
    CHECK(text_xy[1][0] == 200 && text_xy[1][1] == 173 && !strcmp(texts[1], "Mode off"));
    CHECK(out.eax == 0x1236u && out.edx == 0x55u && out.ecx == 0x66u);
    CHECK(r32(0x004c2208u) == 0xaaaau && r32(0x004c220cu) == 0xbbbbu);
    /* A wingman without an index: "" (an empty label text gives the box). */
    fresh();
    label_setup();
    w32(0x004c3032u, 0x004c4400u);
    wingman_answer = 0x1234ffffu;
    text(0x004c6000u, "");
    w16(0x004d24f0u, 1);
    CHECK(!es2_sim_call(&table, 0x0045e208u, NULL, 0, regs, &out) && refusals == 0 && !strcmp(calls, "RwRT"));
    CHECK(wingman_arg == 0x004c4400u && fill_rects[1][0] == 20 && fill_rects[1][1] == 165 && fill_rects[1][2] == 120 &&
          fill_rects[1][3] == 173 && fill_colours[1] == 5 && !strcmp(texts[0], "Mode on"));
    /* A wingman with index 1 (only the low word), the y clamp (top 236). */
    fresh();
    label_setup();
    w32(0x004c3032u, 0x004c4400u);
    wingman_answer = 0x00020001u;
    w32(0x004cfa4cu, 236);
    CHECK(!es2_sim_call(&table, 0x0045e208u, NULL, 0, regs, &out) && refusals == 0);
    CHECK(fill_rects[0][1] == 230 && fill_rects[0][3] == 238 && !strcmp(texts[0], "Pilot Bo") && text_xy[0][1] == 238);
    /* A text that would overrun the frame: refused by name, the GC fields left at 0. */
    fresh();
    label_setup();
    text(0x004c6000u, "0123456789012345678901234567890123456789012345678901234567890123456");
    CHECK(es2_sim_call(&table, 0x0045e208u, NULL, 0, regs, &out) && loop.refused_va == 0x0045e208u && loop.stack_hi == hi &&
          r32(0x004c2208u) == 0);
    return 0;
}

/* ---- the error report's MessageBoxA, PostQuitMessage, __exit and
 * _abort through the process platform (es2/sim/process_platform.h). The
 * texts are this test's (no game strings); _open/_write/_close are
 * recording stubs for the test's duration. */
static struct {
    unsigned boxes, exits, quit_at_exit;
    uint32_t owner[4], style[4], answer, status;
    char text[4][0x200], caption[4][64];
} rec;
static uint32_t rec_box(void *user, uint32_t owner, const char *t, const char *c, uint32_t style) {
    (void)user;
    if (rec.boxes < 4) {
        rec.owner[rec.boxes] = owner; rec.style[rec.boxes] = style;
        snprintf(rec.text[rec.boxes], sizeof(rec.text[0]), "%s", t);
        snprintf(rec.caption[rec.boxes], sizeof(rec.caption[0]), "%s", c);
    }
    ++rec.boxes;
    return rec.answer;
}
static void rec_exit(void *user, uint32_t status) {
    (void)user;
    ++rec.exits;
    rec.status = status;
    rec.quit_at_exit = loop.quit_posted;
}
static uint32_t open_answer;
static unsigned writes;
static void svc_open(es2_sim_service_call *c, void *u) { (void)u; note('o'); c->eax = open_answer; }
static void svc_write(es2_sim_service_call *c, void *u) { (void)u; note('w'); ++writes; c->eax = c->slots[2]; }
static void svc_close(es2_sim_service_call *c, void *u) { (void)u; note('C'); c->eax = 0; }
/* The fake window's queue: one message, then empty. */
static unsigned window_left;
static int window_peek(void *u, es2_sim_loop *l, uint32_t *m, uint32_t *wp, uint32_t *lp) {
    (void)u; (void)l;
    if (!window_left) return 0;
    --window_left;
    *m = 0xa001u; *wp = 0x77u; *lp = 0;
    return 1;
}
static void report_literals(void) {
    text(0x004a159cu, "HEAD");          /* the box's head (0x4a159c) */
    text(0x004a1662u, "|");             /* the gap (0x4a1662) */
    text(0x004a1664u, "Cap5");          /* severity 5's caption */
    text(0x004a1656u, "Cap4");          /* severity 4's caption */
    text(0x004a1540u, "t.log");
    text(0x004a1635u, "%s:%d:%s");      /* the report format */
    text(0x004a1651u, "=%s");
    text(0x004a1614u, "\r\n");
    text(0x004c7000u, "Sev4"); text(0x004c7010u, "Sev5");
    w32(0x004a15f4u + 16u, 0x004c7000u); w32(0x004a15f4u + 20u, 0x004c7010u);
    text(0x004c7020u, "f.cpp"); text(0x004c7030u, "boom");
}
static int fatal_box(void) {
    static const uint32_t vas[3] = {0x004967c6u, 0x0049677eu, 0x00496874u};
    const es2_sim_service_fn stubs[3] = {svc_open, svc_write, svc_close};
    es2_sim_service_fn keep[3] = {NULL, NULL, NULL};
    unsigned at[3] = {0, 0, 0};
    for (unsigned v = 0; v < 3; ++v) {
        while (at[v] < table.count && storage[at[v]].va != vas[v]) ++at[v];
        CHECK(at[v] < table.count);
        keep[v] = storage[at[v]].service;
        storage[at[v]].service = stubs[v];
    }
    const uint32_t slots[6] = {0, 5u, 0, 0x004c7020u, 42u, 0x004c7030u};
    es2_sim_result r;
    /* 1. Headless (no record): the box counted, IDOK, WM_QUIT posted, the
     *    run stops refused at __exit 0x49679c. */
    fresh(); report_literals(); open_answer = 5; writes = 0; memset(&rec, 0, sizeof(rec)); last_end = 0;
    CHECK(es2_sim_loop_call(&loop, 0x00479c78u, slots, 6, &r) == 1 && loop.outcome == ES2_SIM_LOOP_REFUSED);
    CHECK(last_end == 0x0049679cu && loop.platform.message_box == 1 && loop.quit_posted == 1 && writes == 2);
    /* 2. The windowed platform's record (a recorder): MessageBoxA(0, the
     *    box text, the caption, 0), then __exit(1) after PostQuitMessage. */
    es2_sim_process_platform p = {rec_box, rec_exit, NULL, NULL, NULL};
    fresh(); report_literals(); writes = 0; memset(&rec, 0, sizeof(rec)); last_end = 0; rec.answer = 1;
    loop.process = &p;
    unsigned before = refusals;
    CHECK(es2_sim_loop_call(&loop, 0x00479c78u, slots, 6, &r) == 1 && loop.outcome == ES2_SIM_LOOP_EXITED);
    CHECK(loop.exit_code == 1 && rec.exits == 1 && rec.status == 1 && rec.quit_at_exit == 1 && last_end == 0);
    CHECK(rec.boxes == 1 && rec.owner[0] == 0 && rec.style[0] == 0 && !strcmp(rec.caption[0], "Cap5"));
    CHECK(!strcmp(rec.text[0], "HEAD|Sev5:42:f.cpp=boom") && loop.platform.message_box == 1 && writes == 2);
    CHECK(refusals == before + 1u);   /* the service's unwinding, logged once by the dispatch */
    /* The WM_QUIT path: after the posted messages and the window's queue. */
    loop.outcome = ES2_SIM_LOOP_RETURNED;
    w32(ES2_SIM_VA_HANDLERS + 4u, 0x00477becu);
    es2_sim_loop_window win;
    memset(&win, 0, sizeof(win));
    win.peek = window_peek;
    loop.window = &win;
    window_left = 1;
    handler_count = 0;
    CHECK(es2_sim_loop_post(&loop, 1, 0xa000u, 0xa003u, 0) == 0);
    uint32_t result = 0;
    CHECK(es2_sim_pump(&loop, &result) == 0 && result == 1 && handler_count == 2 && handler_messages[0] == 0xa000u &&
          handler_messages[1] == 0xa001u && loop.quit_posted == 0);
    loop.window = NULL;
    /* 3. Severity 4 (0x479f24): EAX is the box's answer (IDCANCEL here). */
    fresh(); report_literals(); memset(&rec, 0, sizeof(rec)); rec.answer = 2;
    loop.process = &p;
    const uint32_t four[5] = {4u, 0, 0x004c7020u, 7u, 0};
    CHECK(es2_sim_loop_call(&loop, 0x00479f24u, four, 5, &r) == 0 && r.eax == 2 && rec.boxes == 1);
    CHECK(!strcmp(rec.caption[0], "Cap4") && !strcmp(rec.text[0], "Sev4:7:f.cpp") && rec.exits == 0);
    /* 4. _open fails: headless refused at _abort 0x49688c; with the record
     *    CW3220's box (the module's name, 0x12010) and __exit(3). */
    fresh(); report_literals(); open_answer = 0xffffffffu; last_end = 0;
    CHECK(es2_sim_loop_call(&loop, 0x00479e8cu, &slots[5], 1, &r) == 1 && loop.outcome == ES2_SIM_LOOP_REFUSED &&
          last_end == 0x0049688cu);
    fresh(); report_literals(); memset(&rec, 0, sizeof(rec)); rec.answer = 1;
    p.module_name = "dbsim.exe";
    loop.process = &p;
    CHECK(es2_sim_loop_call(&loop, 0x00479e8cu, &slots[5], 1, &r) == 1 && loop.outcome == ES2_SIM_LOOP_EXITED);
    CHECK(loop.exit_code == 3 && rec.status == 3 && rec.boxes == 1 && rec.style[0] == 0x00012010u && rec.owner[0] == 0);
    CHECK(!strcmp(rec.text[0], "Abnormal program termination") && !strcmp(rec.caption[0], "dbsim.exe"));
    /* A record with only a box: the process end as headless. */
    fresh(); report_literals(); memset(&rec, 0, sizeof(rec)); open_answer = 5; last_end = 0;
    es2_sim_process_platform box_only = {rec_box, NULL, NULL, NULL, NULL};
    loop.process = &box_only;
    CHECK(es2_sim_loop_call(&loop, 0x00479c78u, slots, 6, &r) == 1 && loop.outcome == ES2_SIM_LOOP_REFUSED &&
          last_end == 0x0049679cu && rec.boxes == 1);
    /* The helpers without a record: IDOK, status 3. */
    CHECK(es2_sim_process_message_box(NULL, 0, "a", "b", 0) == ES2_SIM_IDOK && es2_sim_process_abort(NULL) == 3u);
    for (unsigned v = 0; v < 3; ++v) storage[at[v]].service = keep[v];
    fresh();
    return 0;
}

/* ---- the message instrument (main_loop.md, "the windowed notices measured, and the windowed replay") */
static char trace_text[16384];
static unsigned trace_size, trace_writes;
static uint32_t trace_clock;
static int trace_sink(void *u, const char *bytes, unsigned length) {
    (void)u;
    ++trace_writes;
    if (trace_size + length >= sizeof(trace_text)) return 1;
    memcpy(trace_text + trace_size, bytes, length);
    trace_size += length;
    trace_text[trace_size] = 0;
    return 0;
}
static uint32_t trace_tick(void *u) { (void)u; return trace_clock++; }
static int message_trace(void) {
    static const char lines[] = "{\"f\":0,\"api\":\"msg\",\"a\":512,\"b\":0,\"c\":1310730}\n";
    es2_journal j;
    uint32_t result = 7;
    /* Not attached: no note, the pump as before. */
    fresh();
    CHECK(loop.peek_note == NULL);
    w32(ES2_SIM_VA_HANDLERS + 4u, 0x00477becu);
    CHECK(es2_sim_pump(&loop, &result) == 0 && loop.platform.peeks == 1);
    /* Attached: frame 0 has two pump calls (the second a modal pass), each
     * ending in its empty peek; the journal's mouse move comes first. */
    fresh();
    es2_journal_init(&j);
    CHECK(!es2_journal_load_text(&j, lines, sizeof(lines) - 1u, NULL) && j.count == 1);
    loop.journal = &j;
    w32(0x004d3610u, 0x00010001u);   /* the main window */
    w32(0x004d3720u, 0x00010003u);
    w32(ES2_SIM_VA_HANDLERS + 4u, 0x00477becu);
    w32(ES2_SIM_VA_FRAME_COUNTER, 0);
    static es2_sim_message_trace t;
    trace_size = trace_writes = 0;
    trace_text[0] = 0;
    trace_clock = 100;
    es2_sim_message_trace_attach(&t, &loop, trace_sink, NULL);
    t.now = trace_tick;
    CHECK(loop.peek_note != NULL && loop.peek_note_user == &t && trace_size == 0);   /* the start record buffered */
    CHECK(es2_sim_message_trace_post(&t, 1, 0xa000u, 0xa003u, 0x24000u) == 0 && t.posted == 1 && loop.posted_count == 1);
    CHECK(es2_sim_message_trace_post(&t, 1, 0x0400u, 0, 0) == 0 && t.posted == 1);   /* another message: not counted */
    CHECK(es2_sim_pump(&loop, &result) == 0);
    CHECK(t.retrieved == 1 && t.retrieved_first == 1 && t.retrieved_modal == 0 && t.peeks == 4 && t.inject == 1);
    CHECK(es2_sim_message_trace_post(&t, 1, 0xa000u, 0xa003u, 0x32000u) == 0 && t.posted == 2);
    CHECK(es2_sim_pump(&loop, &result) == 0 && t.retrieved == 2 && t.retrieved_modal == 1 && t.peeks == 6);
    CHECK(trace_size == 0);   /* nothing written before the frame ends */
    w32(ES2_SIM_VA_FRAME_COUNTER, 1);
    CHECK(es2_sim_pump(&loop, &result) == 0);
    CHECK(strstr(trace_text, "{\"event\":\"start\",\"schema\":1,\"kind\":\"message_trace\",\"key\":\"frame_counter_0x4d257b\","
                             "\"pump\":\"pm_remove_calls_in_frame\",\"source\":\"es2sim\"}\r\n") == trace_text);
    CHECK(strstr(trace_text, "{\"k\":\"inject\",\"f\":0,\"p\":0,\"m\":512,\"w\":0,\"l\":1310730,\"h\":65537,\"t\":0,"
                             "\"c\":\"0x464ca2\",\"r\":\"0x464ca7\"}\r\n") != NULL);
    /* The notice's time is its post's (100), not its retrieval's. */
    CHECK(strstr(trace_text, "{\"k\":\"msg\",\"f\":0,\"p\":1,\"m\":40960,\"w\":40963,\"l\":147456,\"h\":65537,\"t\":100,") != NULL);
    CHECK(strstr(trace_text, "{\"k\":\"msg\",\"f\":0,\"p\":2,\"m\":1024,\"w\":0,\"l\":0,\"h\":65537,\"t\":102,") != NULL);
    CHECK(strstr(trace_text, "{\"k\":\"msg\",\"f\":0,\"p\":4,\"m\":40960,\"w\":40963,\"l\":204800,\"h\":65537,\"t\":103,") != NULL);
    CHECK(strstr(trace_text, "{\"k\":\"frame\",\"f\":0,\"pumps\":6,\"empty\":2,\"msgs\":3,\"inject\":1,\"drop\":0,\"noremove\":0}\r\n") != NULL);
    CHECK(strstr(trace_text, "\"f\":1,") == NULL);   /* frame 1 still open */
    /* A full posted queue refuses the notice: counted as refused, no time kept. */
    for (unsigned i = 0; i < ES2_SIM_POSTED_DEPTH; ++i) CHECK(es2_sim_message_trace_post(&t, 1, 0xa000u, 0xa003u, i) == 0);
    CHECK(es2_sim_message_trace_post(&t, 1, 0xa000u, 0xa003u, ES2_SIM_POSTED_DEPTH) == 1 && t.refused == 1 &&
          t.posted == 2u + ES2_SIM_POSTED_DEPTH && t.post_count == ES2_SIM_POSTED_DEPTH);
    loop.posted_count = 0;
    t.post_count = 0;
    es2_sim_message_trace_close(&t);
    CHECK(t.closed && t.frames == 2 && t.modal_passes == 1 && t.frames_modal == 1 && t.most_in_frame == 2 && t.most_frame == 0);
    CHECK(strstr(trace_text, "{\"k\":\"frame\",\"f\":1,\"pumps\":1,\"empty\":1,\"msgs\":0,\"inject\":0,\"drop\":0,\"noremove\":0}\r\n") != NULL);
    CHECK(strstr(trace_text, "{\"event\":\"end\",\"other_thread\":0,\"notices_posted\":10002,\"notices_refused\":1,\"notices_retrieved\":2,"
                             "\"retrieved_first_pass\":1,\"retrieved_modal\":1,\"modal_passes\":1,\"frames_modal\":1}\r\n") != NULL);
    unsigned size = trace_size;
    es2_sim_message_trace_close(&t);   /* once */
    CHECK(es2_sim_pump(&loop, &result) == 0 && trace_size == size);   /* closed: no further records */
    es2_journal_free(&j);
    fresh();
    return 0;
}

/* ---- the posted queue is a ring of ES2_SIM_POSTED_DEPTH entries
 * (main_loop.md, "the posted queue's depth"). The pump's retrievals are
 * recorded through the peek note (no handler involved). */
static uint32_t seen_message[ES2_SIM_POSTED_DEPTH + 64u], seen_lparam[ES2_SIM_POSTED_DEPTH + 64u];
static unsigned seen_count;
static void on_note(void *u, const es2_sim_loop *l, int source, uint32_t hwnd, uint32_t message, uint32_t wparam, uint32_t lparam) {
    (void)u; (void)l; (void)hwnd; (void)wparam;
    if (source == ES2_SIM_PEEK_EMPTY || seen_count >= sizeof(seen_message) / sizeof(seen_message[0])) return;
    seen_message[seen_count] = message;
    seen_lparam[seen_count++] = lparam;
}
static int posted_ring(void) {
    uint32_t result = 7;
    /* 1. A burst beyond the old 16: all accepted, retrieved in the order
     *    posted, one peek each plus the pass's empty one. */
    fresh();
    loop.peek_note = on_note;
    seen_count = 0;
    for (uint32_t i = 0; i < 100u; ++i) CHECK(es2_sim_loop_post(&loop, 1, 0xa000u, 0xa003u, i) == 0);
    CHECK(loop.posted_count == 100u && loop.posted_head == 0);
    CHECK(es2_sim_pump(&loop, &result) == 0 && loop.platform.peeks == 101u && loop.posted_count == 0 && loop.posted_head == 100u);
    CHECK(seen_count == 100u);
    for (uint32_t i = 0; i < 100u; ++i) CHECK(seen_message[i] == 0xa000u && seen_lparam[i] == i);
    /* 2. The ring wraps: the head three before the end, eight posts. */
    loop.posted_head = ES2_SIM_POSTED_DEPTH - 3u;
    seen_count = 0;
    for (uint32_t i = 0; i < 8u; ++i) CHECK(es2_sim_loop_post(&loop, 1, 0xa000u, 0xa003u, 500u + i) == 0);
    CHECK(loop.posted_count == 8u && loop.posted[ES2_SIM_POSTED_DEPTH - 3u].lparam == 500u && loop.posted[0].lparam == 503u &&
          loop.posted[4].lparam == 507u);
    CHECK(es2_sim_pump(&loop, &result) == 0 && seen_count == 8u && loop.posted_count == 0 && loop.posted_head == 5u);
    for (uint32_t i = 0; i < 8u; ++i) CHECK(seen_lparam[i] == 500u + i);
    /* 3. Full: DEPTH accepted, the next refused and nothing overwritten; all
     *    DEPTH retrieved in order; then the queue accepts again. */
    seen_count = 0;
    for (uint32_t i = 0; i < ES2_SIM_POSTED_DEPTH; ++i) CHECK(es2_sim_loop_post(&loop, 1, 0xa000u, 0xa003u, i) == 0);
    CHECK(es2_sim_loop_post(&loop, 1, 0xa000u, 0xa003u, 0xdeadu) == 1 && loop.posted_count == ES2_SIM_POSTED_DEPTH);
    CHECK(es2_sim_loop_post(&loop, 1, 0xa000u, 0xa003u, 0xdeadu) == 1);   /* still refused, still nothing lost from the queue */
    CHECK(es2_sim_pump(&loop, &result) == 0 && seen_count == ES2_SIM_POSTED_DEPTH && loop.posted_count == 0);
    for (uint32_t i = 0; i < ES2_SIM_POSTED_DEPTH; ++i) CHECK(seen_lparam[i] == i);
    CHECK(es2_sim_loop_post(&loop, 1, 0xa000u, 0xa003u, 1u) == 0 && loop.posted_count == 1u);
    /* 4. TranslateMessage's character queues behind the notices already
     *    posted: the key-down (journal), the 20 notices, then the WM_CHAR. */
    fresh();
    static const char lines[] = "{\"f\":0,\"api\":\"msg\",\"a\":256,\"b\":87,\"c\":1114113}\n";
    es2_journal j;
    es2_journal_init(&j);
    CHECK(!es2_journal_load_text(&j, lines, sizeof(lines) - 1u, NULL) && j.count == 1);
    loop.journal = &j;
    loop.peek_note = on_note;
    seen_count = 0;
    w32(0x004d3720u, 0x00010003u);
    for (uint32_t i = 0; i < 20u; ++i) CHECK(es2_sim_loop_post(&loop, 1, 0xa000u, 0xa003u, i) == 0);
    CHECK(es2_sim_pump(&loop, &result) == 0 && seen_count == 22u);
    CHECK(seen_message[0] == 0x100u);
    for (uint32_t i = 0; i < 20u; ++i) CHECK(seen_message[1u + i] == 0xa000u && seen_lparam[1u + i] == i);
    CHECK(seen_message[21] == 0x102u);
    es2_journal_free(&j);
    /* 5. The instrument keeps a post time for every queued notice, beyond
     *    the old 32: the 33rd..40th notice carry their own post's time. */
    fresh();
    static es2_sim_message_trace t2;
    trace_size = trace_writes = 0;
    trace_text[0] = 0;
    trace_clock = 1000;
    w32(0x004d3610u, 0x00010001u);
    w32(ES2_SIM_VA_FRAME_COUNTER, 0);
    es2_sim_message_trace_attach(&t2, &loop, trace_sink, NULL);
    t2.now = trace_tick;
    for (uint32_t i = 0; i < 40u; ++i) CHECK(es2_sim_message_trace_post(&t2, 1, 0xa000u, 0xa003u, 0x1000u + i) == 0);
    CHECK(t2.posted == 40u && t2.post_count == 40u);
    CHECK(es2_sim_pump(&loop, &result) == 0 && t2.retrieved == 40u && t2.post_count == 0);
    es2_sim_message_trace_close(&t2);
    for (uint32_t i = 30; i < 40u; ++i) {
        char needle[64];
        snprintf(needle, sizeof(needle), "\"l\":%lu,\"h\":65537,\"t\":%lu,", (unsigned long)(0x1000u + i), (unsigned long)(1000u + i));
        CHECK(strstr(trace_text, needle) != NULL);
    }
    fresh();
    return 0;
}

int main(void) {
    image = calloc(1, IMAGE_SIZE);
    heap_bytes = calloc(1, HEAP_SIZE);
    for (uint32_t a = 0x30000000u; !local && a < 0x38000000u; a += 0x01000000u)
        local = VirtualAlloc((void *)(uintptr_t)a, LOCAL_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    CHECK(image && heap_bytes && local);
    local_guest = (uint32_t)(uintptr_t)local;
    CHECK(es2_sim_arena_add(&arena, IMAGE_BASE, image, IMAGE_SIZE));
    CHECK(es2_sim_arena_add(&arena, HEAP_BASE, heap_bytes, HEAP_SIZE));
    CHECK(es2_sim_arena_add(&arena, local_guest, local, LOCAL_SIZE));   /* identity: slots are guest-addressable */
    es2_sim_arena_memory(&arena, &memory);
    fresh();
    CHECK(setup_dispatch() == 0);
    if (dib() || timing() || pump() || main_loop() || shell_rest() || fatal_box() || message_trace() || posted_ring()) return 1;
    puts("PASS: loop: the DIB section's objects and their release, timing init/clock/limiter under virtual time, "
         "the pump with the journal and TranslateMessage, the window procedure, the main loop, its tail, frame_limit and the exit, the dead keys and the shell leftovers' rows, the error report's box, WM_QUIT and process end through the process platform, the message instrument, the posted queue's ring");
    return 0;
}
