/* The Gouraud fill, its triangle and the flat fill (sim/src/gouraud.c) on a
 * synthetic image: zeroed host memory at the original's addresses, a graphics
 * context whose fill kind 7 sends every polygon to a recording slot 33, and
 * recording stand-ins for the flat fill's callees. Expectations follow the
 * contract (rasterizer.md, "Gouraud and flat fill"). The comparison with the
 * original's machine code is
 * //tools/reference-game/invitro:differential_gl_gouraud_test. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "es2/sim/gouraud.h"
#include "es2/sim/gfx.h"
#include "es2/sim/loop.h"
#include "es2/sim/memory.h"
#include "es2/sim/services.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

#define IMAGE_BASE 0x00400000u
#define IMAGE_SIZE 0x002d0000u
#define LOCAL_SIZE 0x00020000u   /* call area 0..0x10000, the world 0x10000.. */
#define O_CONTEXT 0x0000u
#define O_FILL 0x0300u
#define O_POINTS 0x0400u
#define O_SHADES 0x0600u
#define O_RAMP 0x0800u
#define O_GC 0x0c00u
#define O_COUNTER 0x0f00u
#define SLOT33 0x10000100u        /* the recorder's VA (no row) */

static uint8_t *image, *local;
static uint32_t local_guest, world;
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
static void on_refuse(const es2_sim_refusal *r, void *user) { (void)user; last = *r; ++refusals; }

static uint32_t r32(uint32_t a) { return memory.read32(memory.context, a); }
static void w32(uint32_t a, uint32_t v) { memory.write32(memory.context, a, v); }
static void w16(uint32_t a, uint32_t v) { memory.write16(memory.context, a, (uint16_t)v); }
static uint32_t at(uint32_t off) { return world + off; }

/* Slot 33 (n, points): the call's n, F's colour word and first point. */
static struct { uint32_t n, colour, x0, y0; } rec[300];
static unsigned recs;
static void slot33(es2_sim_service_call *c, void *u) {
    (void)u;
    if (recs < 300u) {
        uint32_t f = r32(r32(ES2_SIM_VA_GFX_CONTEXT) + 0x228u);
        rec[recs].n = c->slots[0];
        rec[recs].colour = r32(f + 4u);
        rec[recs].x0 = r32(c->slots[1]);
        rec[recs].y0 = r32(c->slots[1] + 4u);
    }
    ++recs;
    c->eax = 0x33u;
}
/* The flat fill's callees: the projection (returns `behind`), the marks,
 * the clips, the lookups and the shape fill (records GC+0x22c's record and
 * the counter). */
static uint32_t behind, project_calls, marks_calls, clip_calls, shade_calls, filled_kind, filled_colour, filled_shades, filled_counter;
static void project(es2_sim_service_call *c, void *u) { (void)u; ++project_calls; w16(0x006cbc02u, 3u); c->eax = behind; }
static void marks(es2_sim_service_call *c, void *u) {
    (void)u;
    ++marks_calls;
    c->eax = r32(c->slots[0]) == 0x004a0ca0u && (r32(c->slots[0] + 4u) & 0xffffu) == 7u && c->slots[1] == 0 && c->slots[2] == 0;
}
static void clip(es2_sim_service_call *c, void *u) { (void)u; ++clip_calls; c->eax = 0; }
static void clip_shades(es2_sim_service_call *c, void *u) { (void)u; ++clip_calls; w16(c->slots[1], 0x77u); c->eax = 0; }
static void haze(es2_sim_service_call *c, void *u) { (void)u; ++shade_calls; c->eax = at(O_RAMP) + c->slots[0]; }
static void shade(es2_sim_service_call *c, void *u) { (void)u; ++shade_calls; c->eax = c->slots[0] + c->slots[1]; }
static void shape_fill(es2_sim_service_call *c, void *u) {
    (void)u;
    uint32_t r = r32(r32(0x006c5f70u) + 0x22cu);
    filled_kind = r32(r);
    filled_colour = r32(r + 4u);
    filled_shades = r32(r + 0xcu);
    filled_counter = r32(r32(ES2_SIM_VA_COUNTER_POINTER));
    c->eax = 0;
}

static void clip_many(es2_sim_service_call *c, void *u) { (void)u; ++clip_calls; w16(0x006cbc02u, 11u); c->eax = 0; }

static void replace(es2_sim_dispatch *d, uint32_t va, es2_sim_service_fn fn) {
    es2_sim_entry *e = (es2_sim_entry *)es2_sim_dispatch_find(d, va);
    e->service = fn;
    e->service_user = NULL;
}
static int setup_dispatch(void) {
    es2_sim_dispatch_init(&table, storage, 4096, on_refuse, NULL);
    services.memory = &memory;
    services.heap = &heap;
    services.vt = &vt;
    services.fs0 = NULL;
    if (es2_sim_register_services(&table, &services)) return 1;
    if (es2_sim_dispatch_add_service(&table, SLOT33, "slot 33 recorder", slot33, NULL, 0, 2)) return 1;
    const char *error = NULL;
    uint32_t bad = 0;
    if (es2_sim_dispatch_seal(&table, &error, &bad)) return 1;
    return 0;
}
static int has_row(uint32_t va) {
    for (unsigned i = 0; i < es2_sim_service_count(); ++i) {
        uint32_t row = 0;
        es2_sim_service_row(i, &row);
        if (row == va) return 1;
    }
    return 0;
}
static es2_sim_entry extra_storage[4096];
static es2_sim_dispatch extra;   /* the same with stand-ins for the callees without rows */
static int setup_extra(void) {
    es2_sim_dispatch_init(&extra, extra_storage, 4096, on_refuse, NULL);
    if (es2_sim_register_services(&extra, &services)) return 1;
    /* A callee with a row (now or from a later package) is replaced after the seal. */
    static const struct { uint32_t va; es2_sim_service_fn fn; unsigned slots; } stand[] = {
        {0x0048cce4u, project, 0}, {0x0042e8e8u, marks, 3}, {0x0048cf20u, clip, 0}, {0x0048d38cu, clip_shades, 2},
        {0x0048d5c0u, shape_fill, 0}, {0x00468158u, haze, 1}, {0x00430e34u, shade, 2},
    };
    for (unsigned i = 0; i < sizeof(stand) / sizeof(stand[0]); ++i)
        if (!has_row(stand[i].va) && es2_sim_dispatch_add_service(&extra, stand[i].va, "stand-in", stand[i].fn, NULL, 0, (uint8_t)stand[i].slots)) return 1;
    const char *error = NULL;
    uint32_t bad = 0;
    if (es2_sim_dispatch_seal(&extra, &error, &bad)) return 1;
    for (unsigned i = 0; i < sizeof(stand) / sizeof(stand[0]); ++i) replace(&extra, stand[i].va, stand[i].fn);
    return 0;
}

static void fresh(void) {
    memset(image, 0, IMAGE_SIZE);
    memset(local, 0, LOCAL_SIZE);
    w32(ES2_SIM_VA_GFX_CONTEXT, at(O_CONTEXT));
    w32(at(O_CONTEXT) + 0x228u, at(O_FILL));
    w32(at(O_FILL), 7u);
    w32(ES2_SIM_VA_GFX_SLOTS + 4u * 33u, SLOT33);
    /* Ramp entry 1: {table, count 0x80}; table word k = 100 + k. */
    w32(ES2_SIM_VA_GFX_RAMPS + 8u, at(O_RAMP));
    w16(ES2_SIM_VA_GFX_RAMPS + 12u, 0x80u);
    for (uint32_t k = 0; k < 0x100u; ++k) w16(at(O_RAMP) + 2u * k, 100u + k);
    w32(0x006c5f70u, at(O_GC));
    w32(ES2_SIM_VA_COUNTER_POINTER, at(O_COUNTER));
    memset(&vt, 0, sizeof(vt));
    es2_vt_enable(&vt, 1, 47, 0x01000000u, 0, 1);
    es2_sim_loop_defaults(&loop);
    loop.memory = &memory;
    loop.dispatch = &table;
    loop.vt = &vt;
    loop.stack_lo = local_guest;
    loop.stack_hi = local_guest + 0x10000u;
    es2_sim_loop_bind(&loop);
    refusals = 0;
    recs = 0;
}
static void point(unsigned i, int32_t x, int32_t y) { w32(at(O_POINTS) + 8u * i, (uint32_t)x); w32(at(O_POINTS) + 8u * i + 4u, (uint32_t)y); }
static void shades3(uint32_t a, uint32_t b, uint32_t c) { w16(at(O_SHADES), a); w16(at(O_SHADES) + 2u, b); w16(at(O_SHADES) + 4u, c); }

/* ---- 1. the triangle */
static int triangle(void) {
    uint32_t eax = 0;
    fresh();
    point(0, 0, 0); point(1, 8, 0); point(2, 0, 8);
    /* Equal points: nothing; EAX the y read last (P2 == P1: P2's y). */
    point(1, 0, 8);
    shades3(10, 0, 4);
    CHECK(es2_sim_fill_gouraud_triangle(&loop, at(O_POINTS), at(O_SHADES), &eax) == 0 && eax == 8u && recs == 0);
    /* One colour: one triangle with [F+4] = the shade; EAX that of 0x483eb8 (the saved region word, 0). */
    point(1, 8, 0);
    shades3(5, 5, 5);
    CHECK(es2_sim_fill_gouraud_triangle(&loop, at(O_POINTS), at(O_SHADES), &eax) == 0 && eax == 0u);
    CHECK(recs == 1u && rec[0].n == 3u && rec[0].colour == 5u);
    /* Shades 10, 0, 4: D 10, Dh 6, Dl 4; colours 10..5 then 4..1; T2 (P1)
     * follows T0 (P0): Q starts on the long edge at T0. */
    recs = 0;
    shades3(10, 0, 4);
    CHECK(es2_sim_fill_gouraud_triangle(&loop, at(O_POINTS), at(O_SHADES), &eax) == 0 && recs == 10u);
    for (unsigned i = 0; i < 6u; ++i) CHECK(rec[i].n == 4u && rec[i].colour == 10u - i);
    for (unsigned i = 0; i < 4u; ++i) CHECK(rec[6u + i].colour == 4u - i);
    CHECK(rec[0].x0 == 0 && rec[0].y0 == 0);
    /* The other winding: Q starts on the short edge (also T0 for band 0). */
    recs = 0;
    shades3(10, 4, 0);
    CHECK(es2_sim_fill_gouraud_triangle(&loop, at(O_POINTS), at(O_SHADES), &eax) == 0 && recs == 10u && rec[6].colour == 4u);
    /* A short edge empty: Dh 0, all bands on C with hi - i. */
    recs = 0;
    shades3(10, 10, 0);
    CHECK(es2_sim_fill_gouraud_triangle(&loop, at(O_POINTS), at(O_SHADES), &eax) == 0 && recs == 10u && rec[9].colour == 1u);
    /* 256 steps are drawn; 257 are refused (the edge tables overrun). */
    recs = 0;
    shades3(256, 0, 100);
    CHECK(es2_sim_fill_gouraud_triangle(&loop, at(O_POINTS), at(O_SHADES), &eax) == 0 && recs == 256u && refusals == 0);
    recs = 0;
    shades3(257, 0, 100);
    CHECK(es2_sim_fill_gouraud_triangle(&loop, at(O_POINTS), at(O_SHADES), &eax) == 1 && recs == 0 && refusals == 1u);
    CHECK(last.va == ES2_SIM_VA_GOURAUD_TRIANGLE);
    CHECK(loop.stack_hi == local_guest + 0x10000u);
    return 0;
}

/* ---- 2. the Gouraud fill */
static int gouraud(void) {
    uint32_t eax = 0, edx = 0, ecx = 0;
    fresh();
    w32(at(O_FILL), 7u);
    w32(at(O_FILL) + 4u, 0xabcd0001u);
    w32(at(O_FILL) + 0xcu, at(O_SHADES));
    /* Two points: no triangle. L[i] = table[(S * 0x80) >> 8] = 100 + 0x20;
     * the frame keeps L0 in the colour words (ebp-0x18) after the return. */
    for (unsigned i = 0; i < 4u; ++i) w16(at(O_SHADES) + 2u * i, 0x40u);
    CHECK(es2_sim_fill_gouraud(&loop, 2u, at(O_POINTS), &eax, &edx, &ecx) == 0);
    CHECK(eax == at(O_FILL) && edx == 0xabcd0001u && ecx == 0 && r32(at(O_FILL)) == 1u && r32(at(O_FILL) + 4u) == 0xabcd0001u);
    CHECK((r32(local_guest + 0x10000u - 0x18u) & 0xffffu) == 132u && (r32(local_guest + 0x10000u - 0x48u) & 0xffffu) == 132u);
    CHECK(r32(local_guest + 0x10000u - 0x60u) == 0 && r32(local_guest + 0x10000u - 8u) == at(O_RAMP) && r32(local_guest + 0x10000u - 4u) == 0xabcd0001u);
    CHECK(es2_sim_fill_gouraud(&loop, 0xffffffffu, at(O_POINTS), &eax, &edx, &ecx) == 0 && ecx == 0xfffffffdu);
    CHECK(refusals == 0);
    CHECK(es2_sim_fill_gouraud(&loop, 37u, at(O_POINTS), &eax, &edx, &ecx) == 1 && refusals == 1u && last.va == ES2_SIM_VA_GOURAUD);
    CHECK(es2_sim_fill_gouraud(&loop, 0x80000000u, at(O_POINTS), &eax, &edx, &ecx) == 1 && refusals == 2u);
    CHECK(es2_sim_fill_gouraud(&loop, 0x80000002u, at(O_POINTS), &eax, &edx, &ecx) == 0 && ecx == 0x80000000u);
    CHECK(loop.stack_hi == local_guest + 0x10000u);
    return 0;
}

/* ---- 3. the flat fill */
static int flat(void) {
    uint32_t eax = 0, edx = 0, ecx = 0;
    fresh();
    w16(at(O_SHADES), 9u);
    /* No counter pointer and more than 1000 points are refused. */
    w32(ES2_SIM_VA_COUNTER_POINTER, 0);
    CHECK(es2_sim_flat_fill(&loop, at(O_POINTS), 3u, at(O_SHADES), &eax, &edx, &ecx) == 1 && refusals == 1u);
    w32(ES2_SIM_VA_COUNTER_POINTER, at(O_COUNTER));
    CHECK(es2_sim_flat_fill(&loop, at(O_POINTS), 1001u, at(O_SHADES), &eax, &edx, &ecx) == 1 && refusals == 2u);
    /* Without a row for 0x48cce4 the call is refused there. */
    if (!has_row(0x0048cce4u)) {
        CHECK(es2_sim_flat_fill(&loop, at(O_POINTS), 3u, at(O_SHADES), &eax, &edx, &ecx) == 1 && refusals == 3u && last.va == 0x0048cce4u);
    }
    /* With stand-ins: the plain record (mode 0), the context's polygon, the counter. */
    fresh();
    loop.dispatch = &extra;
    w16(at(O_SHADES), 9u);
    for (uint32_t i = 0; i < 5u; ++i) image[0x006c68fau + i - IMAGE_BASE] = 0x5a;
    local[0x10000u + O_RAMP + 0x4bu + 2u + 9u] = 0xc3;   /* byte [0x468158(0x4b) + 0x430e34(2, 9)] */
    w32(at(O_COUNTER), 40u);
    behind = 0;
    project_calls = marks_calls = clip_calls = shade_calls = 0;
    CHECK(es2_sim_flat_fill(&loop, at(O_POINTS), 3u, at(O_SHADES), &eax, &edx, &ecx) == 0 && refusals == 0);
    CHECK(project_calls == 1u && marks_calls == 1u && clip_calls == 0 && shade_calls == 2u);
    CHECK(filled_kind == 0 && filled_colour == 0xc3u && filled_counter == 42u && r32(at(O_COUNTER)) == 40u);
    CHECK(eax == at(O_COUNTER) && edx == eax);
    CHECK((r32(0x006c68f0u) & 0xffffu) == 3u && r32(0x006c68ecu) == at(O_POINTS) && r32(0x006c68f2u) == 0x006c6068u);
    CHECK((r32(0x006c68e4u) & 0xffffffffu) == 3u);
    CHECK(image[0x006c68fau - IMAGE_BASE] == 0 && image[0x006c68fau + 2u - IMAGE_BASE] == 0 && image[0x006c68fau + 3u - IMAGE_BASE] == 0x5a);
    /* Mode 2 without a point behind: the Gouraud record {1, 2, 0, S}. */
    w32(0x004aaaacu, 2u);
    CHECK(es2_sim_flat_fill(&loop, at(O_POINTS), 3u, at(O_SHADES), &eax, &edx, &ecx) == 0);
    CHECK(filled_kind == 1u && filled_colour == 2u && filled_shades == at(O_SHADES) && clip_calls == 0);
    /* A point behind: both clips; the record takes the clipped words. */
    behind = 1;
    CHECK(es2_sim_flat_fill(&loop, at(O_POINTS), 3u, at(O_SHADES), &eax, &edx, &ecx) == 0 && clip_calls == 2u);
    CHECK(filled_kind == 1u && filled_shades != at(O_SHADES) && (r32(filled_shades) & 0xffffu) == 0x77u);
    /* More than 10 clipped words are refused. */
    replace(&extra, 0x0048cf20u, clip_many);
    CHECK(es2_sim_flat_fill(&loop, at(O_POINTS), 3u, at(O_SHADES), &eax, &edx, &ecx) == 1 && refusals == 1u && last.va == ES2_SIM_VA_FLAT_FILL);
    replace(&extra, 0x0048cf20u, clip);
    CHECK(loop.stack_hi == local_guest + 0x10000u);
    return 0;
}

int main(void) {
    image = calloc(1, IMAGE_SIZE);
    for (uint32_t a = 0x30000000u; !local && a < 0x38000000u; a += 0x01000000u)
        local = VirtualAlloc((void *)(uintptr_t)a, LOCAL_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    CHECK(image && local);
    local_guest = (uint32_t)(uintptr_t)local;
    world = local_guest + 0x10000u;
    CHECK(es2_sim_arena_add(&arena, IMAGE_BASE, image, IMAGE_SIZE));
    CHECK(es2_sim_arena_add(&arena, local_guest, local, LOCAL_SIZE));
    es2_sim_arena_memory(&arena, &memory);
    fresh();
    CHECK(setup_dispatch() == 0 && setup_extra() == 0);
    if (triangle() || gouraud() || flat()) return 1;
    puts("PASS: gouraud: the triangle's paths, colours, winding and the 257-step refusal; the Gouraud fill's registers, record and"
         " count refusals; the flat fill's refusals, the plain and Gouraud records, the clips and the counter");
    return 0;
}
