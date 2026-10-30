/* The presentation frame (sim/src/present.c) on a synthetic image: zeroed host
 * memory with the palettes, range records, surface and DIB this test writes at
 * the original's addresses. The clock 0x467828, the display calls
 * 0x42a9c0/0x42a4c4, the flush 0x43048c and the two driver slots are this
 * test's recording services; 0x4303c4 is hud_load.h's row. The capture is written to
 * TEST_TMPDIR and read back; its hashes are recomputed here. No game data. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "es2/sim/present.h"
#include "es2/sim/load.h"
#include "es2/sim/machine_sim.h"
#include "es2/sim/memory.h"
#include "es2/sim/services.h"
#include "es2/sim/trap.h"
#include "tools/reference-game/proxy/sha256.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

#define IMAGE_BASE 0x00400000u
#define IMAGE_SIZE 0x002d8000u
#define HEAP_BASE 0x09000000u
#define HEAP_SIZE 0x00100000u
#define LOCAL_SIZE 0x00080000u
#define SLOT_DRAW 0x00401020u   /* VAs no row of services.h uses */
#define SLOT_SHOW 0x00401024u
/* Synthetic objects in the heap region. */
#define PAL_P (HEAP_BASE + 0x0000u)       /* the current palette record P */
#define PAL_P_ENTRIES (HEAP_BASE + 0x0100u)
#define PAL_A (HEAP_BASE + 0x0800u)
#define PAL_A_ENTRIES (HEAP_BASE + 0x0900u)
#define PAL_B (HEAP_BASE + 0x1000u)
#define PAL_B_ENTRIES (HEAP_BASE + 0x1100u)
#define RANGE0 (HEAP_BASE + 0x2000u)
#define RANGE1 (HEAP_BASE + 0x2010u)
#define SURFACE (HEAP_BASE + 0x3000u)
#define DIB_BITS (HEAP_BASE + 0x4000u)
#define DD_OBJECTS (HEAP_BASE + 0x5000u)   /* four objects of 0x10 and their vtable */
#define DD_VTABLE (HEAP_BASE + 0x5100u)
#define DD_RELEASE 0x00401030u              /* the objects' Release (a VA no row uses) */

static uint8_t *image, *heap_bytes, *local;
static uint32_t local_guest;
static es2_sim_arena arena;
static es2_guest_memory memory;
static es2_sim_heap heap;
static es2_sim_services services;
static es2_sim_entry storage[4096];
static es2_sim_dispatch table;
static es2_sim_loop loop;
static es2_sim_present present;
static es2_vt vt;
static uint32_t first_refused, now, clock_calls, slot_args[2];
static unsigned refusals;
static char order[64];
static unsigned order_n;
static void on_refuse(const es2_sim_refusal *r, void *user) {
    (void)user;
    if (!refusals) first_refused = r->va;
    ++refusals;
    if (getenv("PRESENT_TEST_VERBOSE")) fprintf(stderr, "refused 0x%08x: %s\n", (unsigned)r->va, r->reason ? r->reason : "");
}
static void note(char c) { if (order_n + 1 < sizeof(order)) { order[order_n++] = c; order[order_n] = 0; } }

static uint32_t r32(uint32_t a) { return memory.read32(memory.context, a); }
static uint16_t r16(uint32_t a) { return memory.read16(memory.context, a); }
static uint8_t r8(uint32_t a) { return es2_sim_read8(&memory, a); }
static void w32(uint32_t a, uint32_t v) { memory.write32(memory.context, a, v); }
static void w16(uint32_t a, uint16_t v) { memory.write16(memory.context, a, v); }
static void w8(uint32_t a, uint8_t v) { es2_sim_write8(&memory, a, v); }

static void svc_clock(es2_sim_service_call *c, void *u) { (void)u; ++clock_calls; note('T'); c->eax = now; c->edx = 0x11u; c->ecx = 0x22u; }
static void svc_enter(es2_sim_service_call *c, void *u) { (void)u; note(c->slots[0] == 0x00abc000u ? 'E' : 'e'); }
static void svc_leave(es2_sim_service_call *c, void *u) { (void)u; note(c->slots[0] == 0x00abc000u ? 'L' : 'l'); }
static void svc_flush(es2_sim_service_call *c, void *u) { (void)u; (void)c; note('F'); }
static void svc_slot(es2_sim_service_call *c, void *u) {
    (void)u;
    note(c->va == SLOT_DRAW ? 'D' : 'S');
    slot_args[c->va == SLOT_DRAW ? 0 : 1] = c->slots[0];
}
static void on_present(void *user, const es2_sim_present_record *r) { (void)user; (void)r; note('P'); }
/* The frame clear 0x46548c and the objects' Release */
static void svc_clear(es2_sim_service_call *c, void *u) { (void)u; (void)c; note('C'); }
static void svc_release(es2_sim_service_call *c, void *u) { (void)u; note((char)('0' + (c->slots[0] - DD_OBJECTS) / 0x10u)); }
/* The switch's callees: GetWindowRect, SetCursorPos, the timer kill and the panel widget's slot +4 */
#define WIDGET_SLOT 0x00401038u
static uint32_t cursor_args[2], widget_arg;
static uint32_t window_pos[7];
static void svc_window_rect(es2_sim_service_call *c, void *u) {
    (void)u;
    note('R');
    static const uint32_t rc[4] = {5, 6, 645, 486};
    for (unsigned i = 0; i < 4u; ++i) w32(c->slots[1] + 4u * i, rc[i]);
    c->eax = 1;
}
static void svc_cursor_set(es2_sim_service_call *c, void *u) { (void)u; note('X'); cursor_args[0] = c->slots[0]; cursor_args[1] = c->slots[1]; c->eax = 1; }
static void svc_timer_kill(es2_sim_service_call *c, void *u) { (void)u; (void)c; note('K'); }
static void svc_widget(es2_sim_service_call *c, void *u) { (void)u; note('W'); widget_arg = c->slots[0]; c->eax = 0x55u; }
static void on_dd(void *user, uint32_t id, const uint32_t a[7]) {
    (void)user;
    if (id == ES2_SIM_DD_WINDOW_POS) memcpy(window_pos, a, sizeof(window_pos));
}

static int setup_dispatch(void) {
    es2_sim_dispatch_init(&table, storage, 4096, on_refuse, NULL);
    services.memory = &memory;
    services.heap = &heap;
    services.vt = &vt;
    services.fs0 = NULL;
    if (es2_sim_register_services(&table, &services)) return 1;
    struct { uint32_t va; es2_sim_service_fn f; uint8_t slots; } extra[] = {
        {0x00467828u, svc_clock, 0}, {0x0042a9c0u, svc_enter, 1}, {0x0042a4c4u, svc_leave, 1},
        {0x0043048cu, svc_flush, 0}, {SLOT_DRAW, svc_slot, 1}, {SLOT_SHOW, svc_slot, 1},
        {0x0046548cu, svc_clear, 0}, {DD_RELEASE, svc_release, 1},
        {0x0049694cu, svc_window_rect, 2}, {0x004969e2u, svc_cursor_set, 2}, {0x004650d0u, svc_timer_kill, 0},
        {WIDGET_SLOT, svc_widget, 1},
    };
    for (unsigned i = 0; i < sizeof(extra) / sizeof(extra[0]); ++i) {
        unsigned k = 0;
        while (k < table.count && storage[k].va != extra[i].va) ++k;
        if (k < table.count) { storage[k].service = extra[i].f; storage[k].service_user = NULL; continue; }
        if (es2_sim_dispatch_add_service(&table, extra[i].va, "test", extra[i].f, NULL, 0, extra[i].slots)) return 1;
    }
    const char *error = NULL;
    uint32_t bad = 0;
    return es2_sim_dispatch_seal(&table, &error, &bad);
}
static void fresh(void) {
    memset(image, 0, IMAGE_SIZE);
    memset(heap_bytes, 0, HEAP_SIZE);
    memset(local, 0, LOCAL_SIZE);
    memset(&vt, 0, sizeof(vt));
    es2_vt_enable(&vt, 1, 47, 0x01000000u, 0, 1);
    es2_sim_loop_defaults(&loop);
    loop.memory = &memory;
    loop.dispatch = &table;
    loop.vt = &vt;
    loop.stack_lo = local_guest;
    loop.stack_hi = local_guest + 0x10000u;
    es2_sim_loop_bind(&loop);
    es2_sim_load_bind(NULL);
    es2_sim_present_headless(&present, &loop);
    es2_sim_present_bind(&present);
    /* P with 256 entries 0x00iiiiii + a marker in byte 3. */
    w32(0x0049b020u, PAL_P);
    w16(PAL_P + 4u, 256);
    w32(PAL_P + 8u, PAL_P_ENTRIES);
    for (uint32_t i = 0; i < 256u; ++i) w32(PAL_P_ENTRIES + 4u * i, (i << 16) | (i << 8) | i | 0x7f000000u);
    refusals = 0;
    first_refused = 0;
    clock_calls = 0;
    order_n = 0;
    order[0] = 0;
    now = 100;
}
static int row(uint32_t va, const uint32_t *slots, unsigned n, es2_sim_result *r) {
    uint32_t regs[3] = {0, 0, 0};
    uint32_t at = local_guest + 0x20000u;
    for (unsigned i = 0; i < n; ++i) w32(at + 4u * i, slots[i]);
    memset(r, 0, sizeof(*r));
    return es2_sim_call(&table, va, (const uint32_t *)(uintptr_t)at, n, regs, r);
}
static uint32_t entry(uint32_t i) { return r32(PAL_P_ENTRIES + 4u * i); }
static uint32_t initial(uint32_t i) { return (i << 16) | (i << 8) | i | 0x7f000000u; }

/* ---- 1. the cycler 0x4306ac: a slot due (with the wrap), a slot waiting, empty slots, n = 0, n > 256 */
static int cycler(void) {
    es2_sim_result r;
    fresh();
    w16(RANGE0, 10); w16(RANGE0 + 2u, 4); w16(RANGE0 + 4u, 7);
    w16(RANGE1, 20); w16(RANGE1 + 2u, 3); w16(RANGE1 + 4u, 9);
    w32(0x004cfd64u, RANGE0); w32(0x004cfd78u, 99); w16(0x004cfd8cu, 1);      /* due: 99 < 100 */
    w32(0x004cfd68u, RANGE1); w32(0x004cfd7cu, 100); w16(0x004cfd8eu, 2);     /* waiting: 100 is not below 100 */
    CHECK(row(0x004306acu, NULL, 0, &r) == 0 && refusals == 0 && clock_calls == 1);
    CHECK(entry(10) == initial(11) && entry(11) == initial(12) && entry(12) == initial(13) && entry(13) == initial(10));
    CHECK(entry(9) == initial(9) && entry(14) == initial(14) && entry(20) == initial(20));
    CHECK(r16(0x004cfd8cu) == 2 && r32(0x004cfd78u) == 107u && r16(0x004cfd8eu) == 2 && r32(0x004cfd7cu) == 100u);
    CHECK(r.eax == 0x004cfd7cu && r.edx == 0x004cfd74u && r.ecx == 0x004cfd8cu);
    CHECK(r8(0x0049b030u) == 1 && r32(0x0049b028u) == 10u && r32(0x0049b02cu) == 13u);   /* hud_load.h's 0x4303c4 */
    /* the index reaching n resets; a second pass with the wrap at index 3 */
    w16(0x004cfd8cu, 3); w32(0x004cfd78u, 0);
    w32(0x004cfd68u, 0);
    CHECK(row(0x004306acu, NULL, 0, &r) == 0 && refusals == 0 && clock_calls == 2);
    CHECK(entry(10) == initial(10) && entry(11) == initial(11) && entry(12) == initial(12) && entry(13) == initial(13));
    CHECK(r16(0x004cfd8cu) == 0 && r.eax == 107u && r.edx == 0x004cfd74u && r.ecx == 0x004cfd8cu);
    /* no slot: EAX 0, EDX the last slot, ECX the clock's */
    w32(0x004cfd64u, 0);
    CHECK(row(0x004306acu, NULL, 0, &r) == 0 && r.eax == 0 && r.edx == 0x004cfd74u && r.ecx == 0x22u);
    /* n = 0: 0x4303c4 still called, the index counts up (1 is not 0) */
    w32(0x004cfd6cu, RANGE0); w16(RANGE0 + 2u, 0); w32(0x004cfd80u, 0); w16(0x004cfd90u, 0);
    CHECK(row(0x004306acu, NULL, 0, &r) == 0 && refusals == 0 && r16(0x004cfd90u) == 1 && r32(0x004cfd80u) == 107u);
    /* n > 256 refused */
    w16(RANGE0 + 2u, 257); w32(0x004cfd80u, 0);
    CHECK(row(0x004306acu, NULL, 0, &r) != 0 && first_refused == 0x00430733u);
    return 0;
}

/* ---- 2. the fade set-up 0x4308fc: modes 2, 1, 0 and the refusals */
static int fade_setup(void) {
    es2_sim_result r;
    fresh();
    w16(PAL_A + 4u, 2); w32(PAL_A + 8u, PAL_A_ENTRIES);
    w16(PAL_B + 4u, 2); w32(PAL_B + 8u, PAL_B_ENTRIES);
    w32(PAL_A_ENTRIES, 0x00102030u); w32(PAL_A_ENTRIES + 4u, 0x00405060u);
    w32(PAL_B_ENTRIES, 0x00405060u); w32(PAL_B_ENTRIES + 4u, 0x00102030u);
    w8(0x0049b03cu, 1);
    uint32_t a[3] = {PAL_A, PAL_B, 16};
    CHECK(row(0x004308fcu, a, 3, &r) == 0 && refusals == 0 && r.eax == 2u);
    CHECK(r32(0x004cfd98u) == PAL_A && r32(0x004cfda0u) == 16u && r8(0x0049b03cu) == 0 && r16(0x004d03a4u) == 2 && r16(0x004cfd9eu) == 2);
    CHECK(r16(0x004cfda4u) == 0x0300u && r16(0x004cffa4u) == 0x0300u && r16(0x004d01a4u) == 0x0300u);
    CHECK(r16(0x004cfda6u) == 0xfd00u && r16(0x004cffa6u) == 0xfd00u && r16(0x004d01a6u) == 0xfd00u);
    a[1] = 0;
    CHECK(row(0x004308fcu, a, 3, &r) == 0 && r.eax == 2u && r16(0x004d03a4u) == 1);
    CHECK(r16(0x004cfda4u) == 0xfd00u && r16(0x004cffa4u) == 0xfe00u && r16(0x004d01a4u) == 0xff00u);
    a[0] = 0; a[1] = PAL_B;
    CHECK(row(0x004308fcu, a, 3, &r) == 0 && r.eax == 2u && r16(0x004d03a4u) == 0);
    CHECK(r16(0x004cfda4u) == 0x0600u && r16(0x004cffa4u) == 0x0500u && r16(0x004d01a4u) == 0x0400u);
    CHECK(r16(0x004cfda6u) == 0x0300u && r16(0x004cffa6u) == 0x0200u && r16(0x004d01a6u) == 0x0100u);
    /* a negative count: EAX 0 (modes 0 and 2), the word keeps it */
    w16(PAL_B + 4u, 0xffffu);
    CHECK(row(0x004308fcu, a, 3, &r) == 0 && r.eax == 0 && r16(0x004cfd9eu) == 0xffffu);
    w16(PAL_A + 4u, 0xffffu);
    a[0] = PAL_A; a[1] = 0;
    CHECK(row(0x004308fcu, a, 3, &r) == 0 && r.eax == 0xffffffffu);
    CHECK(refusals == 0);
    a[0] = 0; a[1] = 0;
    CHECK(row(0x004308fcu, a, 3, &r) != 0 && first_refused == 0x00430b20u);
    refusals = 0;
    w16(PAL_A + 4u, 2);
    a[0] = PAL_A; a[2] = 0;
    CHECK(row(0x004308fcu, a, 3, &r) != 0 && first_refused == 0x00430984u);
    return 0;
}

/* ---- 3. the fade step 0x430b34: start, modes 0/1/other, the dirty range both ways, expiry */
static int fade_step(void) {
    es2_sim_result r;
    fresh();
    w16(PAL_B + 4u, 2); w32(PAL_B + 8u, PAL_B_ENTRIES);
    w32(PAL_B_ENTRIES, 0x00102030u); w32(PAL_B_ENTRIES + 4u, 0x00102030u);
    uint32_t a[3] = {0, PAL_B, 16};
    CHECK(row(0x004308fcu, a, 3, &r) == 0);
    w16(0x004cfd9cu, 2);   /* the start word (no code writes it) */
    /* the first step starts the fade: elapsed 0 */
    CHECK(row(0x00430b34u, NULL, 0, &r) == 0 && refusals == 0 && clock_calls == 1);
    CHECK(r8(0x0049b03cu) == 1 && r32(0x0049b038u) == 116u && r.eax == 16u && r.edx == 3u && r.ecx == 2u);
    CHECK(entry(2) == 0x7f000000u && entry(3) == 0x7f000000u && entry(4) == initial(4));
    CHECK(r8(0x0049b030u) == 1 && r32(0x0049b028u) == 2u && r32(0x0049b02cu) == 3u);
    /* half way; the dirty range widened (signed) */
    w32(0x0049b028u, 5); w32(0x0049b02cu, 0xffffffffu);
    now = 108;
    CHECK(row(0x00430b34u, NULL, 0, &r) == 0 && r.eax == 8u);
    CHECK(entry(2) == 0x7f081018u && entry(3) == 0x7f081018u);
    CHECK(r32(0x0049b028u) == 2u && r32(0x0049b02cu) == 3u);
    w32(0x0049b028u, 0); w32(0x0049b02cu, 9);
    CHECK(row(0x00430b34u, NULL, 0, &r) == 0 && r32(0x0049b028u) == 0 && r32(0x0049b02cu) == 9u);
    /* mode 1 from A: A's entry plus the (negative) steps, byte-wise */
    w16(PAL_A + 4u, 2); w32(PAL_A + 8u, PAL_A_ENTRIES);
    w32(PAL_A_ENTRIES, 0x55102030u); w32(PAL_A_ENTRIES + 4u, 0x55405060u);
    w32(PAL_A_ENTRIES + 8u, 0x55102030u); w32(PAL_A_ENTRIES + 12u, 0x55405060u);
    a[0] = PAL_A; a[1] = 0;
    CHECK(row(0x004308fcu, a, 3, &r) == 0);
    w16(0x004cfd9cu, 2);
    now = 200;
    CHECK(row(0x00430b34u, NULL, 0, &r) == 0 && r32(0x0049b038u) == 216u);
    CHECK(entry(2) == 0x55102030u && entry(3) == 0x55405060u);
    now = 208;
    CHECK(row(0x00430b34u, NULL, 0, &r) == 0);
    CHECK(entry(2) == 0x55081018u && entry(3) == 0x55202830u);
    /* another mode: no entry written, the range still marked */
    w16(0x004d03a4u, 5);
    w32(PAL_P_ENTRIES + 8u, 0x12345678u);
    w8(0x0049b030u, 0);
    CHECK(row(0x00430b34u, NULL, 0, &r) == 0 && entry(2) == 0x12345678u && r8(0x0049b030u) == 1);
    /* expired: the byte cleared, EAX 0, EDX/ECX the clock's */
    now = 217;
    CHECK(row(0x00430b34u, NULL, 0, &r) == 0 && r.eax == 0 && r.edx == 0x11u && r.ecx == 0x22u && r8(0x0049b03cu) == 0);
    /* the deadline equal to the clock is not expired */
    now = 300;
    CHECK(row(0x00430b34u, NULL, 0, &r) == 0 && r32(0x0049b038u) == 316u);
    now = 316;
    CHECK(row(0x00430b34u, NULL, 0, &r) == 0 && r.eax == 0 && r8(0x0049b03cu) == 1);
    CHECK(refusals == 0 && clock_calls == 9);
    return 0;
}

/* ---- 4. the upload 0x465cc4: static colours, the DIB DC both ways, refusals */
static int upload(void) {
    es2_sim_result r;
    fresh();
    present.platform.num_colors = 20;
    present.platform.system_count = 256;
    for (unsigned i = 0; i < 256u; ++i) {
        present.platform.system[4 * i] = (uint8_t)(0xa0u + i); present.platform.system[4 * i + 1] = 0xb0u;
        present.platform.system[4 * i + 2] = 0xc0u; present.platform.system[4 * i + 3] = 0xffu;
    }
    uint32_t a[1] = {PAL_P};
    w32(0x004d3034u, 0x1234u);
    CHECK(row(0x00465cc4u, a, 1, &r) == 0 && refusals == 0 && r.eax == 1u);
    const uint8_t *e = present.platform.animate_table;
    CHECK(e[0] == 0xa0u && e[1] == 0xb0u && e[2] == 0xc0u && e[3] == 0);
    CHECK(e[36] == (uint8_t)(0xa0u + 9u) && e[39] == 0);
    CHECK(e[40] == 40u && e[41] == 40u && e[42] == 40u && e[43] == 5u);           /* entry 10: 10 << 2 */
    CHECK(e[4 * 100] == (uint8_t)(100u << 2) && e[4 * 245 + 3] == 5u);
    CHECK(e[4 * 246] == (uint8_t)(0xa0u + 246u) && e[4 * 246 + 3] == 0);
    const es2_sim_load_platform *t = es2_sim_present_table(&present);
    CHECK(t && t->color_table_uploads == 1 && t->color_table[4 * 10] == 40u && t->color_table[4 * 10 + 2] == 40u);
    CHECK(t->color_table[0] == 0xc0u && t->color_table[1] == 0xb0u && t->color_table[2] == 0xa0u && t->color_table[3] == 0);
    CHECK(present.platform.get_dc == 1 && present.platform.release_dc == 1 && present.platform.animate == 1);
    /* headless -1: r 0, every entry the palette's; without the DIB DC no table */
    present.platform.num_colors = -1;
    w32(0x004d3034u, 0);
    CHECK(row(0x00465cc4u, a, 1, &r) == 0 && e[0] == 0 && e[3] == 5u && e[4 * 255] == (uint8_t)(255u << 2) && e[4 * 255 + 3] == 5u);
    CHECK(t->color_table_uploads == 1 && present.platform.animate == 2);
    /* 257 (r 128) and 513 (r 256): every flag 0; 514 and -2 write outside */
    present.platform.num_colors = 257;
    CHECK(row(0x00465cc4u, a, 1, &r) == 0 && e[4 * 127 + 3] == 0 && e[4 * 128 + 3] == 0);
    present.platform.num_colors = 513;
    CHECK(row(0x00465cc4u, a, 1, &r) == 0 && e[4 * 128 + 3] == 0 && e[4 * 255 + 3] == 0 && e[4 * 128] == (uint8_t)(0xa0u + 128u));
    present.platform.num_colors = 514;
    CHECK(row(0x00465cc4u, a, 1, &r) != 0 && first_refused == 0x00465d0fu);
    refusals = 0;
    present.platform.num_colors = -2;
    CHECK(row(0x00465cc4u, a, 1, &r) != 0);
    refusals = 0;
    present.platform.num_colors = -1;
    w32(0x004d255eu, 1);
    CHECK(row(0x00465cc4u, a, 1, &r) != 0 && first_refused == 0x00465e1eu);
    return 0;
}

/* ---- 5. the present and the capture on synthetic frames */
static void hash_rows(uint32_t origin, uint32_t stride, uint32_t w, uint32_t h, char out[65]) {
    sha256_ctx s;
    sha256_init(&s);
    for (uint32_t y = 0; y < h; ++y) sha256_update(&s, heap_bytes + (origin - HEAP_BASE) + y * stride, w);
    sha256_hex(&s, out);
}
static int contains(const char *text, const char *needle) { return strstr(text, needle) != NULL; }
static int capture(void) {
    es2_sim_result r;
    fresh();
    const char *dir = getenv("TEST_TMPDIR");
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s\\present_test_capture.ndjson", dir ? dir : ".");
    CHECK(es2_sim_present_capture_open(&present, path) == 0);
    w32(0x004d253eu, 16); w32(0x004d2542u, 8);
    w32(0x004d2546u, 16); w32(0x004d254au, 8);
    w32(0x004d3028u, DIB_BITS); w32(0x004d3034u, 0x1234u); w32(0x004d302cu, 0x5678u);
    for (uint32_t i = 0; i < 128u; ++i) w8(DIB_BITS + i, (uint8_t)(i * 7u + 3u));
    w32(0x004d2ff8u, 0); w32(0x004d2ffcu, 0); w32(0x004d3000u, 15); w32(0x004d3004u, 7);
    w32(0x004d257bu, 5);
    /* no table yet: palette null */
    CHECK(es2_sim_present_frame(&present, 0x0045fbe4u) == 0 && present.records == 1);
    CHECK(present.platform.stretch_blt == 1 && present.platform.select_palette == 2 && present.platform.realize_palette == 2 && present.platform.dst[2] == 16 && present.platform.dst[3] == 8);
    /* an upload, then a partial window scaled 2x into a 32 x 16 client */
    uint32_t a[1] = {PAL_P};
    CHECK(row(0x00465cc4u, a, 1, &r) == 0);
    present.platform.client_w = 32; present.platform.client_h = 16;
    w32(0x004d2ff8u, 4); w32(0x004d2ffcu, 2); w32(0x004d3000u, 7); w32(0x004d3004u, 5);
    w32(0x004d3018u, 4); w32(0x004d301cu, 2);
    CHECK(es2_sim_present_frame(&present, 0x0045fbe4u) == 0);
    CHECK(present.platform.dst[0] == 8 && present.platform.dst[1] == 4 && present.platform.dst[2] == 8 && present.platform.dst[3] == 8);
    CHECK(present.platform.src[0] == 4 && present.platform.src[1] == 2 && present.platform.src[2] == 4 && present.platform.src[3] == 4);
    /* the same site again (ordinal 1), then outside the DIB, then a new frame */
    CHECK(es2_sim_present_frame(&present, 0x0045fbe4u) == 0);
    w32(0x004d3018u, 14);
    CHECK(es2_sim_present_frame(&present, 0x0045fbe4u) == 0);
    w32(0x004d257bu, 6);
    /* A display object D in mode 4 for frame 6 (the region table's view and panel bands) */
    w32(0x004d24c0u, HEAP_BASE + 0x2800u); w32(HEAP_BASE + 0x2814u, 4);
    w32(0x004d3018u, 0); w32(0x004d301cu, 0);
    w32(0x004d2ff8u, 0); w32(0x004d2ffcu, 0); w32(0x004d3000u, 15); w32(0x004d3004u, 7);
    CHECK(es2_sim_present_frame(&present, 0x00401234u) == 0);
    /* nothing without the DIB DC; DirectDraw without a primary surface refused */
    w32(0x004d3034u, 0);
    CHECK(es2_sim_present_frame(&present, 0x0045fbe4u) == 0 && present.records == 5);
    w32(0x004d255eu, 1);
    CHECK(es2_sim_present_frame(&present, 0x0045fbe4u) != 0 && present.refused_va == 0x00465647u);
    w32(0x004d255eu, 0);
    refusals = 0;
    es2_sim_present_capture_close(&present);
    CHECK(refusals == 0);

    /* read back */
    FILE *f = fopen(path, "rb");
    CHECK(f);
    static char lines[8][1024];
    unsigned n = 0;
    while (n < 8 && fgets(lines[n], sizeof(lines[n]), f)) ++n;
    fclose(f);
    CHECK(n == 7);
    CHECK(contains(lines[0], "\"event\":\"start\"") && contains(lines[0], "\"key\":\"frame_counter_0x4d257b\""));
    CHECK(contains(lines[0], ",\"regions\":\"t66-1\"}"));
    char full[65], part[65], palette[65];
    hash_rows(DIB_BITS, 16, 16, 8, full);
    hash_rows(DIB_BITS + 2u * 16u + 4u, 16, 4, 4, part);
    {
        uint8_t rgb[768];
        for (unsigned i = 0; i < 256u; ++i) rgb[3 * i] = rgb[3 * i + 1] = rgb[3 * i + 2] = (uint8_t)(i << 2);
        sha256_ctx s;
        sha256_init(&s);
        sha256_update(&s, rgb, sizeof(rgb));
        sha256_hex(&s, palette);
    }
    char want[512];
    snprintf(want, sizeof(want), "{\"frame\":5,\"site\":\"0x45fbe4\",\"ordinal\":0,\"mode\":0,\"S\":[0,0],\"R\":[0,0,15,7],"
             "\"client\":[0,0,16,8],\"src\":[0,0,16,8],\"dst\":[0,0,16,8],\"source\":\"dib\",\"index_sha256\":\"%s\","
             "\"palette_sha256\":null,\"uploads\":0,\"presented\":\"headless\",\"display\":-1,\"regions\":{\"view\":null,"
             "\"hud\":null,\"panel\":null,\"mfd\":null,\"rest\":\"%.16s\"}}", full, full);   /* no D, all pixels rest */
    CHECK(!strncmp(lines[1], want, strlen(want)));
    snprintf(want, sizeof(want), "{\"frame\":5,\"site\":\"0x45fbe4\",\"ordinal\":1,\"mode\":0,\"S\":[4,2],\"R\":[4,2,7,5],"
             "\"client\":[0,0,32,16],\"src\":[4,2,4,4],\"dst\":[8,4,8,8],\"source\":\"dib\",\"index_sha256\":\"%s\","
             "\"palette_sha256\":\"%s\",\"uploads\":1,", part, palette);
    CHECK(!strncmp(lines[2], want, strlen(want)));
    CHECK(contains(lines[3], "\"ordinal\":2") && contains(lines[3], part));
    CHECK(contains(lines[4], "\"ordinal\":3") && contains(lines[4], "\"S\":[14,2]") &&
          contains(lines[4], "\"source\":\"dib_outside\",\"index_sha256\":null"));
    CHECK(contains(lines[5], "{\"frame\":6,\"site\":\"0x401234\",\"ordinal\":0,") && contains(lines[5], "\"dst\":[0,0,32,16]") &&
          contains(lines[5], full));
    {   /* mode 4 at 16 x 8, rows 0..5 view (390 * 8 / 480), rows 6..7 panel */
        char view[65], band[65], fields[256];
        hash_rows(DIB_BITS, 16, 16, 6, view);
        hash_rows(DIB_BITS + 6u * 16u, 16, 16, 2, band);
        snprintf(fields, sizeof(fields), ",\"display\":4,\"regions\":{\"view\":\"%.16s\",\"hud\":null,\"panel\":\"%.16s\","
                 "\"mfd\":null,\"rest\":null}}\r\n", view, band);
        CHECK(contains(lines[5], fields));
    }
    CHECK(contains(lines[6], "{\"event\":\"stats\",\"frame\":6,\"records\":5,"));
    for (unsigned i = 0; i < n; ++i) CHECK(strlen(lines[i]) >= 2 && !strcmp(lines[i] + strlen(lines[i]) - 2, "\r\n"));
    if (getenv("PRESENT_TEST_KEEP")) printf("capture: %s\n", path);
    return 0;
}

/* ---- 5b. the frame dumps (es2_sim_present_dump_open, ES2F files) */
static int read_file(const char *path, uint8_t *out, size_t cap, size_t *n) {
    FILE *f = fopen(path, "rb");
    if (!f) return 1;
    *n = fread(out, 1, cap, f);
    fclose(f);
    return 0;
}
static uint32_t le32(const uint8_t *b) { return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24; }
static int dumps(void) {
    es2_sim_result r;
    fresh();
    const char *tmp = getenv("TEST_TMPDIR");
    char dir[MAX_PATH], path[MAX_PATH], capture_path[MAX_PATH];
    snprintf(dir, sizeof(dir), "%s\\present_test_dumps", tmp ? tmp : ".");
    CreateDirectoryA(dir, NULL);
    /* the list: refusals, then a valid one */
    CHECK(es2_sim_present_dump_open(&present, dir, "") == 1 && present.dump_items == 0);
    CHECK(es2_sim_present_dump_open(&present, dir, " , ") == 1);
    CHECK(es2_sim_present_dump_open(&present, dir, "5-3") == 1);
    CHECK(es2_sim_present_dump_open(&present, dir, "7x") == 1);
    CHECK(es2_sim_present_dump_open(&present, dir, "-7") == 1);
    CHECK(es2_sim_present_dump_open(&present, dir, "7-") == 1);
    CHECK(es2_sim_present_dump_open(&present, dir, "1234567890") == 1);
    CHECK(es2_sim_present_dump_open(&present, dir, "0-4096") == 1);
    CHECK(es2_sim_present_dump_open(&present, dir, "0-4095") == 0 && present.dump_items == 1 && present.dump_to[0] == 4095u);
    {
        char many[512];
        size_t used = 0;
        for (unsigned i = 0; i <= ES2_SIM_PRESENT_DUMP_ITEMS_MAX; ++i) used += (size_t)snprintf(many + used, sizeof(many) - used, "%u,", i);
        CHECK(es2_sim_present_dump_open(&present, dir, many) == 1);
        char long_dir[300];
        memset(long_dir, 'd', sizeof(long_dir) - 1u);
        long_dir[sizeof(long_dir) - 1u] = 0;
        CHECK(es2_sim_present_dump_open(&present, long_dir, "5") == 1);
    }
    CHECK(es2_sim_present_dump_open(&present, dir, "5, 7-8") == 0 && present.dump_items == 2);
    CHECK(present.dump_from[0] == 5u && present.dump_to[0] == 5u && present.dump_from[1] == 7u && present.dump_to[1] == 8u);
    snprintf(capture_path, sizeof(capture_path), "%s\\present_test_dumps.ndjson", tmp ? tmp : ".");
    CHECK(es2_sim_present_capture_open(&present, capture_path) == 0);
    /* the capture test's DIB: 16 x 8, bytes i * 7 + 3 */
    w32(0x004d253eu, 16); w32(0x004d2542u, 8);
    w32(0x004d2546u, 16); w32(0x004d254au, 8);
    w32(0x004d3028u, DIB_BITS); w32(0x004d3034u, 0x1234u); w32(0x004d302cu, 0x5678u);
    for (uint32_t i = 0; i < 128u; ++i) w8(DIB_BITS + i, (uint8_t)(i * 7u + 3u));
    w32(0x004d2ff8u, 0); w32(0x004d2ffcu, 0); w32(0x004d3000u, 15); w32(0x004d3004u, 7);
    w32(0x004d257bu, 5);
    /* frame 5, ordinal 0: no table uploaded yet, no dump */
    CHECK(es2_sim_present_frame(&present, 0x0045fbe4u) == 0 && present.dumps == 0);
    uint32_t a[1] = {PAL_P};
    CHECK(row(0x00465cc4u, a, 1, &r) == 0);
    /* frame 5, ordinal 1: the window (4, 2) 4 x 4 */
    present.platform.client_w = 32; present.platform.client_h = 16;
    w32(0x004d2ff8u, 4); w32(0x004d2ffcu, 2); w32(0x004d3000u, 7); w32(0x004d3004u, 5);
    w32(0x004d3018u, 4); w32(0x004d301cu, 2);
    CHECK(es2_sim_present_frame(&present, 0x0045fbe4u) == 0 && present.dumps == 1);
    /* ordinal 2 outside the DIB: no dump */
    w32(0x004d3018u, 14);
    CHECK(es2_sim_present_frame(&present, 0x0045fbe4u) == 0 && present.dumps == 1);
    /* frame 6 is not named; frame 7 (the full window) is */
    w32(0x004d3018u, 0); w32(0x004d301cu, 0);
    w32(0x004d2ff8u, 0); w32(0x004d2ffcu, 0); w32(0x004d3000u, 15); w32(0x004d3004u, 7);
    w32(0x004d257bu, 6);
    CHECK(es2_sim_present_frame(&present, 0x00401234u) == 0 && present.dumps == 1);
    w32(0x004d257bu, 7);
    CHECK(es2_sim_present_frame(&present, 0x00401234u) == 0 && present.dumps == 2 && present.dump_failures == 0);
    refusals = 0;
    es2_sim_present_capture_close(&present);
    CHECK(refusals == 0);
    /* the files */
    static uint8_t data[2048];
    size_t n = 0;
    snprintf(path, sizeof(path), "%s\\frame-000005-45fbe4-0.es2f", dir);
    CHECK(read_file(path, data, sizeof(data), &n) != 0);
    snprintf(path, sizeof(path), "%s\\frame-000005-45fbe4-2.es2f", dir);
    CHECK(read_file(path, data, sizeof(data), &n) != 0);
    snprintf(path, sizeof(path), "%s\\frame-000006-401234-0.es2f", dir);
    CHECK(read_file(path, data, sizeof(data), &n) != 0);
    snprintf(path, sizeof(path), "%s\\frame-000005-45fbe4-1.es2f", dir);
    CHECK(read_file(path, data, sizeof(data), &n) == 0 && n == 800u + 16u);
    CHECK(!memcmp(data, "ES2F", 4) && le32(data + 4) == 1u && le32(data + 8) == 5u && le32(data + 12) == 0x0045fbe4u);
    CHECK(le32(data + 16) == 1u && le32(data + 20) == 4u && le32(data + 24) == 4u && le32(data + 28) == 1u);
    for (unsigned i = 0; i < 256u; ++i)
        CHECK(data[32 + 3 * i] == (uint8_t)(i << 2) && data[33 + 3 * i] == (uint8_t)(i << 2) && data[34 + 3 * i] == (uint8_t)(i << 2));
    for (uint32_t y = 0; y < 4u; ++y)
        for (uint32_t x = 0; x < 4u; ++x) CHECK(data[800 + 4 * y + x] == (uint8_t)(((2u + y) * 16u + 4u + x) * 7u + 3u));
    snprintf(path, sizeof(path), "%s\\frame-000007-401234-0.es2f", dir);
    CHECK(read_file(path, data, sizeof(data), &n) == 0 && n == 800u + 128u);
    CHECK(le32(data + 8) == 7u && le32(data + 12) == 0x00401234u && le32(data + 16) == 0u && le32(data + 20) == 16u && le32(data + 24) == 8u);
    for (uint32_t i = 0; i < 128u; ++i) CHECK(data[800 + i] == (uint8_t)(i * 7u + 3u));
    /* the stats line counts them */
    static char text[4096];
    CHECK(read_file(capture_path, (uint8_t *)text, sizeof(text) - 1u, &n) == 0);
    text[n] = 0;
    CHECK(contains(text, ",\"dumps\":2,\"dump_failures\":0}"));
    /* a directory that does not exist: counted as a failure, no file */
    char missing[MAX_PATH];
    snprintf(missing, sizeof(missing), "%s\\no_such_directory", dir);
    CHECK(es2_sim_present_dump_open(&present, missing, "7") == 0);
    CHECK(es2_sim_present_frame(&present, 0x00401234u) == 0 && present.dumps == 0 && present.dump_failures == 1);
    /* without a capture and without on_present the dump still runs */
    CHECK(es2_sim_present_dump_open(&present, dir, "9") == 0);
    w32(0x004d257bu, 9);
    CHECK(present.capture == NULL && present.on_present == NULL);
    CHECK(es2_sim_present_frame(&present, 0x00401234u) == 0 && present.dumps == 1);
    snprintf(path, sizeof(path), "%s\\frame-000009-401234-0.es2f", dir);
    CHECK(read_file(path, data, sizeof(data), &n) == 0 && n == 800u + 128u);
    return 0;
}

/* ---- 6. the frame sequence 0x45fba4: present, page swap (current surface both ways), the surface switch */
static int frame(void) {
    es2_sim_result r;
    fresh();
    present.on_present = on_present;
    w32(0x004d24c0u, 0x00abc000u);
    w32(0x004d253eu, 16); w32(0x004d2542u, 8);
    w32(0x004d3034u, 0x1234u);
    w16(0x004d2566u, 1);
    CHECK(row(0x0045fba4u, NULL, 0, &r) == 0 && refusals == 0);
    CHECK(!strcmp(order, "ETFPL") && clock_calls == 1 && present.presents == 1);
    /* page swap, the surface current */
    order_n = 0; order[0] = 0; clock_calls = 0;
    w16(0x004d2566u, 0);
    w16(0x004d24f2u, 0);
    w32(0x004d24bcu, SURFACE); w32(0x006c5ed0u, SURFACE);
    w32(SURFACE + 0x88u, 0xaaaau); w32(SURFACE + 0x8cu, 0xbbbbu);
    w32(0x004a57f4u, SLOT_DRAW); w32(0x004a57f0u, SLOT_SHOW);
    CHECK(row(0x0045fba4u, NULL, 0, &r) == 0 && refusals == 0);
    CHECK(!strcmp(order, "ETFDSL") && clock_calls == 1 && present.presents == 1);
    CHECK(r16(0x004d267eu) == 1 && r32(SURFACE + 0x34u) == 0xbbbbu && r32(SURFACE + 0x3cu) == 0xaaaau);
    CHECK(r32(0x004a3682u) == 0xbbbbu && r32(0x004a368au) == 0xaaaau && slot_args[0] == 0xbbbbu && slot_args[1] == 0xaaaau);
    CHECK(r32(SURFACE + 0x88u) == 0xbbbbu && r32(SURFACE + 0x8cu) == 0xaaaau);
    /* not current: no slot calls, the view words kept */
    order_n = 0; order[0] = 0;
    w16(0x004d24f2u, 1);
    w32(0x006c5ed0u, 0);
    w32(0x004a3682u, 0); w32(0x004a368au, 0);
    CHECK(row(0x0045fba4u, NULL, 0, &r) == 0 && !strcmp(order, "ETFL") && r16(0x004d267eu) == 0);
    CHECK(r32(SURFACE + 0x34u) == 0xaaaau && r32(SURFACE + 0x3cu) == 0xbbbbu && r32(0x004a3682u) == 0 && r32(0x004a368au) == 0);
    CHECK(r32(SURFACE + 0x88u) == 0xaaaau && r32(SURFACE + 0x8cu) == 0xbbbbu);
    /* video kind 1: the surface switch first (a bare surface is refused inside it) */
    order_n = 0; order[0] = 0;
    w16(0x004d2566u, 1);
    w8(0x004d2537u, 1);
    w32(0x004d2568u, SURFACE);
    CHECK(row(0x0045fba4u, NULL, 0, &r) != 0 && !strchr(order, 'E'));   /* refused inside 0x481224 (no driver table), before 0x42a9c0 */
    /* video kind 2 (signed byte compare): no switch */
    refusals = 0;
    w8(0x004d2537u, 2);
    order_n = 0; order[0] = 0;
    CHECK(row(0x0045fba4u, NULL, 0, &r) == 0 && !strcmp(order, "ETFPL"));
    return 0;
}

/* ---- 7. the present's site rules, the help call 0x4669c4, the release 0x466730 */
static uint32_t last_site;
static void on_site(void *user, const es2_sim_present_record *r) { (void)user; last_site = r->site; }
static int site_of(uint32_t ret, uint32_t *site) {
    es2_sim_service_call sc = {0x00464a14u, ret, 0, NULL, 0, 0, 0, 0, 0, NULL};
    last_site = 0xffffffffu;
    es2_sim_service_present_capture(&sc, NULL);
    *site = last_site;
    return sc.refused || sc.eax != 1u;
}
static int attribution(void) {
    es2_sim_result r;
    uint32_t site = 0;
    fresh();
    present.on_present = on_site;
    w32(0x004d253eu, 16); w32(0x004d2542u, 8); w32(0x004d2546u, 16); w32(0x004d254au, 8);
    w32(0x004d3028u, DIB_BITS); w32(0x004d3034u, 0x1234u);
    w32(0x004d3000u, 15); w32(0x004d3004u, 7);
    /* a C call (return address 0) without a pending site: raw, unattributed */
    last_site = 0xffffffffu;
    CHECK(row(0x00464a14u, NULL, 0, &r) == 0 && r.eax == 1u && last_site == 0 && present.unattributed == 1);
    /* a pending site serves one present */
    es2_sim_present_site(0x0045490bu);
    CHECK(row(0x00464a14u, NULL, 0, &r) == 0 && last_site == 0x0045490bu && present.unattributed == 1);
    CHECK(row(0x00464a14u, NULL, 0, &r) == 0 && last_site == 0 && present.unattributed == 2);
    /* a return address inside CODE is the site and clears a pending one */
    es2_sim_present_site(0x00454b65u);
    CHECK(site_of(0x0045fbe4u, &site) == 0 && site == 0x0045fbe4u && present.unattributed == 2);
    CHECK(row(0x00464a14u, NULL, 0, &r) == 0 && last_site == 0 && present.unattributed == 3);
    /* CODE's bounds [0x401000, 0x497000); outside it and outside a trap: raw */
    CHECK(site_of(0x00401000u, &site) == 0 && site == 0x00401000u && present.unattributed == 3);
    CHECK(site_of(0x00496fffu, &site) == 0 && site == 0x00496fffu && present.unattributed == 3);
    CHECK(site_of(0x00497000u, &site) == 0 && site == 0x00497000u && present.unattributed == 4);
    CHECK(site_of(0x00400fffu, &site) == 0 && site == 0x00400fffu && present.unattributed == 5);
    es2_sim_present_site(0x004614a8u);
    CHECK(site_of(0x10014097u, &site) == 0 && site == 0x004614a8u && present.unattributed == 5);
    CHECK(site_of(0x10014097u, &site) == 0 && site == 0x10014097u && present.unattributed == 6);
    CHECK(es2_sim_guest_entry_va(0) == 0 && refusals == 0);
    return 0;
}
static int help_release(void) {
    es2_sim_result r;
    fresh();
    /* 0x4669c4, GDI: WinHelpA(hwnd, file, HELP_CONTENTS, 0) counted, the platform's answer */
    w32(0x004d3610u, 0x00010001u);
    present.platform.win_help_answer = 7u;
    uint32_t file[1] = {0x00abcdefu};
    CHECK(row(0x004669c4u, file, 1, &r) == 0 && r.eax == 7u && present.platform.win_help == 1);
    CHECK(present.platform.win_help_args[0] == 0x00010001u && present.platform.win_help_args[1] == 0x00abcdefu &&
          present.platform.win_help_args[2] == 3u && present.platform.win_help_args[3] == 0);
    /* DirectDraw on: the switch 0x4667c8 back to the window first, then the help call */
    w32(0x004d255eu, 1);
    CHECK(row(0x004669c4u, file, 1, &r) == 0 && refusals == 0 && present.platform.win_help == 2 && r32(0x004d255eu) == 0);
    CHECK(present.platform.display_requests == 1 && present.platform.fullscreen == 0 && present.platform.clip_cursor == 1);
    present.platform.clip_cursor = 0; present.platform.system_metrics = 0;
    /* 0x466730 without objects: only ClipCursor to the screen (640 x 480 headless, or the platform's) */
    CHECK(row(0x00466730u, NULL, 0, &r) == 0 && r.eax == 1u && !strcmp(order, "") && present.platform.clip_cursor == 1);
    CHECK(present.platform.system_metrics == 2 && present.platform.clip_rect[0] == 0 && present.platform.clip_rect[1] == 0 &&
          present.platform.clip_rect[2] == 639 && present.platform.clip_rect[3] == 479);
    present.platform.screen_cx = 800; present.platform.screen_cy = 600;
    /* with objects: the clear (only with [0x4d3710]), then Release of 0x4d3718, 0x4d3714, 0x4d371c, 0x4d3710, each zeroed */
    w32(DD_VTABLE + 8u, DD_RELEASE);
    for (uint32_t k = 0; k < 4u; ++k) { w32(DD_OBJECTS + 0x10u * k, DD_VTABLE); w32(0x004d3710u + 4u * k, DD_OBJECTS + 0x10u * k); }
    CHECK(row(0x00466730u, NULL, 0, &r) == 0 && !strcmp(order, "C2130") && present.platform.clip_cursor == 2);
    CHECK(present.platform.clip_rect[2] == 799 && present.platform.clip_rect[3] == 599);
    for (uint32_t k = 0; k < 4u; ++k) CHECK(r32(0x004d3710u + 4u * k) == 0);
    /* without [0x4d3710] no clear; a missing object is skipped */
    order_n = 0; order[0] = 0;
    w32(0x004d3714u, DD_OBJECTS + 0x10u);
    CHECK(row(0x00466730u, NULL, 0, &r) == 0 && !strcmp(order, "1") && r32(0x004d3714u) == 0);
    /* a method no row serves is refused */
    w32(0x004d371cu, DD_OBJECTS + 0x30u);
    w32(DD_VTABLE + 8u, 0x00401034u);
    CHECK(row(0x00466730u, NULL, 0, &r) != 0 && first_refused == 0x00401034u && r32(0x004d371cu) == DD_OBJECTS + 0x30u);
    CHECK(present.platform.clip_cursor == 3);
    refusals = 0;
    return 0;
}

/* ---- 8. the full-screen switch 0x4667c8 and the DirectDraw tails */
static int fullscreen(void) {
    es2_sim_result r;
    fresh();
    es2_sim_present_platform *pl = &present.platform;
    pl->dd_note = on_dd;
    pl->adjust[0] = -3; pl->adjust[1] = -20; pl->adjust[2] = 3; pl->adjust[3] = 3;
    w32(0x004d3610u, 0x00010001u);
    w32(0x004d253eu, 320); w32(0x004d2542u, 201);
    w32(0x004d3700u, 1); w32(0x004d3724u, 1);
    /* to full screen, below 640, the start-up's rectangle kept: no GetWindowRect */
    CHECK(row(0x004667c8u, NULL, 0, &r) == 0 && r.eax == 0 && refusals == 0 && !strcmp(order, "X"));
    CHECK(r32(0x004d3700u) == 0 && r32(0x004d36fcu) == 1 && r32(0x004d255eu) == 1);
    for (uint32_t k = 0; k < 4u; ++k) CHECK(r32(0x004d3710u + 4u * k) == ES2_SIM_DD_STANDIN + k);
    CHECK(pl->fullscreen == 1 && pl->display_requests == 1 && cursor_args[0] == 160u && cursor_args[1] == 100u);
    CHECK(pl->dd_calls[ES2_SIM_DD_METRICS] == 3 && pl->dd_calls[ES2_SIM_DD_SURFACE] == 1 && pl->dd_calls[ES2_SIM_DD_ATTACHED] == 1 &&
          pl->dd_calls[ES2_SIM_DD_SET_PALETTE] == 1 && pl->dd_calls[ES2_SIM_DD_CLIP] == 1 && pl->dd_calls[ES2_SIM_DD_SHOW_CURSOR] == 1);
    CHECK(window_pos[0] == 0x00010001u && window_pos[1] == 0xffffffffu && window_pos[2] == 0xfffffffdu && window_pos[3] == 0xffffffecu &&
          window_pos[4] == 326u && window_pos[5] == 224u && window_pos[6] == 0x108u);
    /* the DirectDraw tails: the frame clear's Lock and Flip, the palette apply */
    uint32_t eax = 7;
    CHECK(es2_sim_present_dd_clear(&eax) == 0 && eax == 0 && pl->dd_calls[ES2_SIM_DD_LOCK] == 1 && pl->dd_calls[ES2_SIM_DD_FLIP] == 1);
    pl->dd_fail = ES2_SIM_DD_FAIL_LOCK | ES2_SIM_DD_FAIL_LOST;
    CHECK(es2_sim_present_dd_clear(&eax) == 0 && eax == ES2_SIM_DD_ERROR && pl->dd_calls[ES2_SIM_DD_RESTORE] == 1 &&
          pl->dd_calls[ES2_SIM_DD_UNLOCK] == 1);
    pl->dd_fail = 0;
    uint8_t quads[1024];
    for (unsigned i = 0; i < 1024u; ++i) quads[i] = (uint8_t)i;
    CHECK(es2_sim_present_dd_palette(quads) == 0 && pl->dd_calls[ES2_SIM_DD_PALETTE] == 2 && pl->dd_calls[ES2_SIM_DD_SET_PALETTE] == 2);
    CHECK(r8(0x004d372cu) == 2 && r8(0x004d372du) == 1 && r8(0x004d372eu) == 0 && r8(0x004d372fu) == 1 && r8(0x004d372cu + 4u * 255u) == 0xfe);
    /* back to the window: the release (the clear, four stand-ins), the saved rectangle, the panel widget's slot */
    w32(0x004d36ccu, 10); w32(0x004d36d0u, 20); w32(0x004d36d4u, 650); w32(0x004d36d8u, 500);
    w32(0x0049b088u, HEAP_BASE + 0x3000u); w32(HEAP_BASE + 0x3000u + 0x1e9u, HEAP_BASE + 0x3100u);
    w32(HEAP_BASE + 0x3100u, HEAP_BASE + 0x3200u); w32(HEAP_BASE + 0x3204u, WIDGET_SLOT);
    CHECK(row(0x004667c8u, NULL, 0, &r) == 0 && r.eax == 0x55u && widget_arg == HEAP_BASE + 0x3100u && !strcmp(order, "XCW"));
    CHECK(r32(0x004d255eu) == 0 && pl->fullscreen == 0 && pl->display_requests == 2 && pl->dd_calls[ES2_SIM_DD_RELEASE] == 4);
    for (uint32_t k = 0; k < 4u; ++k) CHECK(r32(0x004d3710u + 4u * k) == 0);
    CHECK(window_pos[1] == 0xfffffffeu && window_pos[2] == 10u && window_pos[3] == 20u && window_pos[4] == 640u &&
          window_pos[5] == 480u && window_pos[6] == 0x10u && pl->dd_calls[ES2_SIM_DD_SHOW_CURSOR] == 2);
    /* to full screen at 640, no sprite, a back buffer left over, the window's rectangle read; a widget-less panel */
    w32(HEAP_BASE + 0x3000u + 0x1e9u, 0);
    w32(0x004d253eu, 640); w32(0x004d2542u, 480); w32(0x004d3724u, 0); w32(0x004d3718u, ES2_SIM_DD_STANDIN + 2u);
    order_n = 0; order[0] = 0;
    CHECK(row(0x004667c8u, NULL, 0, &r) == 0 && r.eax == HEAP_BASE + 0x3000u && !strcmp(order, "RX"));
    CHECK(r32(0x004d36ccu) == 5u && r32(0x004d36d8u) == 486u && r32(0x004d3718u) == 0 && pl->dd_calls[ES2_SIM_DD_RELEASE] == 5);
    CHECK(pl->dd_calls[ES2_SIM_DD_ATTACHED] == 1 && pl->dd_calls[ES2_SIM_DD_SHOW_CURSOR] == 2 && pl->fullscreen == 1);
    /* the failure path: back, then a surface that fails twice: the timer, PostQuitMessage, no request, DirectDraw left on */
    CHECK(row(0x004667c8u, NULL, 0, &r) == 0 && pl->fullscreen == 0);
    order_n = 0; order[0] = 0;
    pl->dd_fail = ES2_SIM_DD_FAIL_SURFACE | ES2_SIM_DD_FAIL_SURFACE_RETRY;
    unsigned surfaces = pl->dd_calls[ES2_SIM_DD_SURFACE], requests = pl->display_requests;
    CHECK(row(0x004667c8u, NULL, 0, &r) == 0 && r.eax == 0 && !strcmp(order, "RK") && loop.quit_posted);
    CHECK(pl->dd_calls[ES2_SIM_DD_SURFACE] == surfaces + 2u && pl->dd_calls[ES2_SIM_DD_POST_QUIT] == 1 &&
          pl->display_requests == requests && r32(0x004d255eu) == 1 && r32(0x004d3714u) == 0);
    pl->dd_fail = 0;
    /* a method through a null object faults: refused */
    CHECK(es2_sim_present_dd_clear(&eax) != 0 && refusals == 1);
    refusals = 0;
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
    CHECK(es2_sim_arena_add(&arena, local_guest, local, LOCAL_SIZE));
    es2_sim_arena_memory(&arena, &memory);
    fresh();
    CHECK(setup_dispatch() == 0);
    if (cycler() || fade_setup() || fade_step() || upload() || capture() || dumps() || frame() || attribution() || help_release() || fullscreen()) return 1;
    puts("PASS: present: cycler (due, waiting, wrap, reset, n 0, n > 256), fade set-up (modes 2/1/0, refusals), fade "
         "step (start, modes 0/1/other, dirty range, expiry), upload (static colours, DIB DC, refusals), capture "
         "(5 records, ordinals, dib_outside, stats), frame dumps (list refusals, ES2F header, palette and pixels, no table, outside, unnamed frame, missing directory, without capture), frame sequence (present, page swap both ways, surface switch), "
         "site rules (CODE, pending, raw), help 0x4669c4 (GDI, DirectDraw), release 0x466730 (none, objects, refusal), "
         "full-screen switch 0x4667c8 (both ways, kept and read rectangle, below and at 640, failure, DirectDraw tails)");
    return 0;
}
