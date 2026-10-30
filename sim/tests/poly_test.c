/* The polygon fills (sim/src/poly.c) on a synthetic image: zeroed host memory
 * at the original's addresses with the globals the fills read (the view, the GC
 * pointer, the recip table, the shade levels, a texture page, the projection)
 * written by this test, a 64 x 48 8-bit surface, a depth buffer and a shade
 * table in an identity-mapped call area. Pixels are checked as counts of
 * changed bytes and single samples against expectations derived from the
 * contract (rasterizer.md, "Software polygon fills"); no pixel data is
 * stored. The comparison with the original's machine code is
 * //tools/reference-game/invitro:differential_poly_test. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "es2/sim/poly.h"
#include "es2/sim/loop.h"
#include "es2/sim/memory.h"
#include "es2/sim/services.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

#define IMAGE_BASE 0x00400000u
#define IMAGE_SIZE 0x002d0000u
#define LOCAL_SIZE 0x00040000u   /* call area 0..0x10000, the world 0x10000.. */
#define W 64
#define H 48
/* World offsets (from local_guest + 0x10000). */
#define O_ROWS 0x0000u
#define O_GC 0x0400u
#define O_VERTS 0x0800u
#define O_POINTS 0x0a00u
#define O_UV 0x0b00u
#define O_SHADES 0x0b80u
#define O_OUT 0x0c00u
#define O_PAGE 0x0e00u
#define O_BITS 0x2000u
#define O_DEPTH 0x3000u
#define O_SHADE_TABLE 0x10000u

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
static uint32_t at(uint32_t off) { return world + off; }
static uint8_t *host(uint32_t off) { return local + 0x10000u + off; }
static uint8_t *pixel(int32_t x, int32_t y) { return host(O_BITS) + y * W + x; }
static unsigned count_not(uint8_t v) { unsigned n = 0; for (unsigned i = 0; i < W * H; ++i) n += host(O_BITS)[i] != v; return n; }
/* A vertex {x, y, z, u, v, w, shade}. */
static void vertex(uint32_t off, int32_t x, int32_t y, uint32_t z, uint32_t u, uint32_t v, uint32_t w, uint32_t s) {
    uint32_t a = at(off);
    w32(a, (uint32_t)x); w32(a + 4u, (uint32_t)y); w32(a + 8u, z); w32(a + 0xcu, u);
    w32(a + 0x10u, v); w32(a + 0x14u, w); w32(a + 0x18u, s);
}
/* The square (10, 5)..(19, 14) as four vertices; u, v constant inside page 0. */
static void square(uint32_t off) {
    vertex(off, 10, 5, 0x100, 0x10000, 0x20000, 0x100, 0x40000);
    vertex(off + 28u, 19, 5, 0x100, 0x10000, 0x20000, 0x100, 0x40000);
    vertex(off + 56u, 19, 14, 0x100, 0x10000, 0x20000, 0x100, 0x40000);
    vertex(off + 84u, 10, 14, 0x100, 0x10000, 0x20000, 0x100, 0x40000);
}

static int setup_dispatch(void) {
    es2_sim_dispatch_init(&table, storage, 4096, on_refuse, NULL);
    services.memory = &memory;
    services.heap = &heap;
    services.vt = &vt;
    services.fs0 = NULL;
    if (es2_sim_register_services(&table, &services)) return 1;
    const char *error = NULL;
    uint32_t bad = 0;
    return es2_sim_dispatch_seal(&table, &error, &bad);
}

static void fresh(void) {
    memset(image, 0, IMAGE_SIZE);
    memset(local, 0, LOCAL_SIZE);
    w32(0x004a3652u, at(O_ROWS));
    w32(0x004a3676u, W);
    w32(at(O_ROWS), at(O_BITS));
    w32(0x006c5f70u, at(O_GC));
    /* The clip rect: 0..63 x 0..47; region mode 0. */
    w32(at(O_GC) + 0x210u, 0); w32(at(O_GC) + 0x214u, 0);
    w32(at(O_GC) + 0x218u, W - 1); w32(at(O_GC) + 0x21cu, H - 1);
    for (uint32_t i = 1; i < 0x280u; ++i) w32(ES2_SIM_VA_POLY_RECIP + 4u * i, 0x10000u / i);
    w32(ES2_SIM_VA_POLY_SHADES, 16u);
    w32(0x004d3cb8u, 8u);
    w32(0x004a0998u, at(O_SHADE_TABLE));
    memset(image + (ES2_SIM_VA_POLY_TEXTURE - IMAGE_BASE), 0x33, 0x10000u);   /* page 0: every texel 0x33 */
    memset(host(O_SHADE_TABLE), 0x44, 0x10100u);
    w32(0x004a08b4u, at(O_PAGE));
    /* The projection: shift 8, near 16, centre (32, 24). */
    w32(0x006c6028u, 8u);
    w32(0x006c602cu, 16u);
    w32(0x006c6034u, 32u);
    w32(0x006c6038u, 24u);
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
}

/* ---- 1. fixed-point helpers and haze */
static int helpers(void) {
    fresh();
    uint32_t eax = 0;
    CHECK(es2_sim_poly_div16(&loop, 0, 1, 2, &eax) == 0 && eax == 0x8000u);
    CHECK(es2_sim_poly_div16(&loop, 0, -3, 2, &eax) == 0 && eax == 0xfffe8000u);
    CHECK(es2_sim_poly_div16(&loop, 0, 1, 0, &eax) == 1 && refusals == 1);
    CHECK(es2_sim_poly_div16(&loop, 0, 0x8000, 1, &eax) == 1 && refusals == 2);
    CHECK(es2_sim_poly_mul16(0x18000, 0x20000) == 0x30000u && es2_sim_poly_mul16(-0x10000, 5) == 0xfffffffbu);
    uint32_t o = at(O_OUT);
    w32(o, 0xffffffffu); w32(o + 4u, 1);
    CHECK(es2_sim_poly_add64(&loop, o, o + 4u, 1, 2) == 0 && r32(o) == 0 && r32(o + 4u) == 4u);
    CHECK(es2_sim_poly_sub64(&loop, o, o + 4u, 1, 0) == 0 && r32(o) == 0xffffffffu && r32(o + 4u) == 3u);
    w32(o, 0x00000010u); w32(o + 4u, 0x80000001u);
    CHECK(es2_sim_poly_shift64(&loop, o, o + 4u, 4) == 0 && r32(o) == 0x10000001u && r32(o + 4u) == 0xf8000000u);
    /* divide_pair: {3 << 16 ... } over 2 -> 1.5 in Q16 = 0x18000 after the << 16. */
    uint32_t rec = at(O_OUT + 0x40u);
    w32(rec, 3); w32(rec + 4u, 0); w32(rec + 8u, (uint32_t)-4); w32(rec + 0xcu, 0xffffffffu); w32(rec + 0x10u, 2);
    CHECK(es2_sim_poly_divide_pair(&loop, o, rec) == 0 && r32(o) == 0x18000u && r32(o + 4u) == 0xfffe0000u);
    w32(rec + 0x10u, 0); w32(o, 7);
    CHECK(es2_sim_poly_divide_pair(&loop, o, rec) == 0 && r32(o) == 7u);
    /* divide_three: {10, -10} / 5 << 4, 3 << 4 / 5. */
    w32(rec, 10); w32(rec + 4u, 0); w32(rec + 8u, (uint32_t)-10); w32(rec + 0xcu, 0xffffffffu); w32(rec + 0x10u, 3);
    CHECK(es2_sim_poly_divide_three(&loop, o, rec, 5, 4) == 0);
    CHECK(r32(o) == 32u && r32(o + 4u) == 0 && r32(o + 8u) == (uint32_t)-32 && r32(o + 0xcu) == 0xffffffffu && r32(o + 0x10u) == 9u);
    /* Haze: far 1000; d 400 is below the half: 0, EAX 400; d 2000 clamps to
     * far: r 0x10000 -> (levels - 1) * shades << 8 = 7 * 16 << 8. */
    es2_sim_poly_set_far(&loop, 1000, &eax);
    CHECK(eax == 1000u && r32(ES2_SIM_VA_POLY_FAR) == 1000u);
    CHECK(es2_sim_poly_haze(&loop, 400, &eax) == 0 && eax == 400u && r32(ES2_SIM_VA_POLY_HAZE) == 0);
    CHECK(es2_sim_poly_haze(&loop, 2000, &eax) == 0 && eax == (7u * 16u) << 8 && r32(ES2_SIM_VA_POLY_HAZE) == eax);
    es2_sim_poly_shade_colour(&loop, 2, &eax);
    CHECK(eax == ((2u * 15u + (112u << 8)) & 0xffffff00u) + at(O_SHADE_TABLE));
    /* Pages. */
    es2_sim_poly_page_init(&loop, at(O_PAGE), 5, &eax);
    CHECK(eax == at(O_PAGE) && r32(at(O_PAGE) + 0x804u) == 5u && r32(at(O_PAGE) + 0x800u) == 0 && r32(at(O_PAGE) + 0x7fcu) == 0x100u);
    es2_sim_poly_page_set(&loop, 9, &eax);
    CHECK(eax == at(O_PAGE) && r32(at(O_PAGE) + 0x804u) == 9u);
    return 0;
}

/* ---- 2. cuts and clippers */
static int clippers(void) {
    fresh();
    uint32_t eax = 0;
    vertex(O_VERTS, 0, 0, 0, 0, 0, 0, 0);
    vertex(O_VERTS + 28u, 10, 20, 100, 200, 300, 400, 500);
    CHECK(es2_sim_poly_cut_x(&loop, 5, at(O_VERTS), at(O_VERTS + 28u), at(O_OUT), 0xfu, &eax) == 0 && eax == 1u);
    CHECK(r32(at(O_OUT)) == 5u && r32(at(O_OUT) + 4u) == 10u && r32(at(O_OUT) + 8u) == 50u && r32(at(O_OUT) + 0x14u) == 200u && r32(at(O_OUT) + 0x18u) == 250u);
    w32(at(O_OUT) + 0x14u, 0xdeadu);
    CHECK(es2_sim_poly_cut_y(&loop, 5, at(O_VERTS + 28u), at(O_VERTS), at(O_OUT), 1u, &eax) == 0 && eax == 1u);
    CHECK(r32(at(O_OUT)) == 2u && r32(at(O_OUT) + 4u) == 5u && r32(at(O_OUT) + 0xcu) == 50u && r32(at(O_OUT) + 0x14u) == 0xdeadu);
    CHECK(es2_sim_poly_cut_x(&loop, 10, at(O_VERTS), at(O_VERTS + 28u), at(O_OUT), 0, &eax) == 0 && eax == 0);
    /* The square shifted to x -5..4 against the left bound 0: 4 vertices. */
    vertex(O_VERTS, -5, 5, 0, 0, 0, 0, 0);
    vertex(O_VERTS + 28u, 4, 5, 0, 0, 0, 0, 0);
    vertex(O_VERTS + 56u, 4, 14, 0, 0, 0, 0, 0);
    vertex(O_VERTS + 84u, -5, 14, 0, 0, 0, 0, 0);
    CHECK(es2_sim_poly_clip_x(&loop, 4, at(O_VERTS), at(O_OUT), 0, &eax) == 0 && eax == 4u);
    for (unsigned i = 0; i < 4u; ++i) CHECK((int32_t)r32(at(O_OUT) + 28u * i) >= 0);
    /* Entirely left: nothing. */
    for (unsigned i = 0; i < 4u; ++i) w32(at(O_VERTS) + 28u * i, (uint32_t)-3);
    CHECK(es2_sim_poly_clip_x(&loop, 4, at(O_VERTS), at(O_OUT), 0, &eax) == 0 && eax == 0);
    /* y: the square 10..19 x -5..4 clipped at the top. */
    square(O_VERTS);
    for (unsigned i = 0; i < 4u; ++i) w32(at(O_VERTS) + 28u * i + 4u, r32(at(O_VERTS) + 28u * i + 4u) - 10u);
    CHECK(es2_sim_poly_clip_y(&loop, 4, at(O_VERTS), at(O_OUT), 0, &eax) == 0 && eax == 4u);
    return refusals != 0;
}

/* ---- 3. edge tables */
static int edges(void) {
    fresh();
    uint32_t eax = 0;
    square(O_VERTS);
    CHECK(es2_sim_poly_edges(&loop, 4, at(O_VERTS), 0, &eax) == 0 && eax == ES2_SIM_VA_POLY_EDGES);
    CHECK(r32(eax) == 5u && r32(eax + 4u) == 9u && r32(eax + 8u) == 0x006c0890u && r32(eax + 0xcu) == 0x006bbd90u);
    CHECK(r32(0x006c0890u) == 10u && r32(0x006bbd90u) == 19u && r32(0x006c0890u + 8u * 0x28u) == 10u);
    /* A triangle (0, 0), (8, 8), (0, 8): the right edge steps one pixel per row. */
    vertex(O_VERTS, 0, 0, 0, 0, 0, 0, 0);
    vertex(O_VERTS + 28u, 8, 8, 0, 0, 0, 0, 0);
    vertex(O_VERTS + 56u, 0, 8, 0, 0, 0, 0, 0);
    CHECK(es2_sim_poly_edges(&loop, 3, at(O_VERTS), 0, &eax) == 0 && r32(eax + 4u) == 8u);
    CHECK(r32(0x006bbd90u + 3u * 0x28u) == 3u && r32(0x006c0890u + 3u * 0x28u) == 0);
    /* Duplicates leave two points: rows 0. */
    vertex(O_VERTS + 28u, 0, 0, 0, 0, 0, 0, 0);
    CHECK(es2_sim_poly_edges(&loop, 3, at(O_VERTS), 0, &eax) == 0 && r32(eax + 4u) == 0);
    /* Flag 2 without flag 1 is refused. */
    square(O_VERTS);
    CHECK(es2_sim_poly_edges(&loop, 4, at(O_VERTS), 2u, &eax) == 1 && refusals == 1 && last.va == 0x00478449u);
    /* Flags 1|2|4|8 run through. */
    CHECK(es2_sim_poly_edges(&loop, 4, at(O_VERTS), 0xfu, &eax) == 0 && r32(eax + 4u) == 9u);
    return 0;
}

/* ---- 4. the fills */
static int fills(void) {
    /* Mode 0: 10 x 9 texels of 0x33. */
    fresh();
    uint32_t eax = 0;
    square(O_VERTS);
    CHECK(es2_sim_poly_fill(&loop, 4, at(O_VERTS), 0, 0, at(O_SHADES), 0, &eax) == 0 && eax == ES2_SIM_VA_POLY_EDGES);
    CHECK(count_not(0) == 90u && *pixel(10, 5) == 0x33 && *pixel(19, 13) == 0x33 && *pixel(20, 5) == 0 && *pixel(10, 14) == 0);
    /* Transparent texel 0 keeps the surface. */
    fresh();
    memset(image + (ES2_SIM_VA_POLY_TEXTURE - IMAGE_BASE), 0, 0x10000u);
    square(O_VERTS);
    CHECK(es2_sim_poly_fill(&loop, 4, at(O_VERTS), 0, 0, at(O_SHADES), 1u, &eax) == 0 && count_not(0) == 0);
    /* Mode 1: through the shade table (0x44); the shade becomes 15 s + haze. */
    fresh();
    square(O_VERTS);
    w32(at(O_SHADES), 2);
    CHECK(es2_sim_poly_fill(&loop, 4, at(O_VERTS), 1, 0, at(O_SHADES), 0, &eax) == 0 && count_not(0) == 90u && *pixel(15, 9) == 0x44);
    CHECK(r32(at(O_SHADES)) == 30u && r32(0x004a09acu) == 30u << 8);
    /* Mode 2 (Gouraud). */
    fresh();
    square(O_VERTS);
    CHECK(es2_sim_poly_fill(&loop, 4, at(O_VERTS), 2, 0, at(O_SHADES), 0, &eax) == 0 && count_not(0) == 90u);
    /* Clipped: the clip rect 0..14 x 0..9 cuts at x 15 and y 10 (hi + 1):
     * columns 10..15 of rows 5..9. */
    fresh();
    w32(at(O_GC) + 0x218u, 14); w32(at(O_GC) + 0x21cu, 9);
    square(O_VERTS);
    CHECK(es2_sim_poly_fill(&loop, 4, at(O_VERTS), 0, 0, at(O_SHADES), 0, &eax) == 0 && count_not(0) == 30u && *pixel(16, 5) == 0 && *pixel(15, 9) == 0x33);
    /* Entirely outside: EAX 0. */
    fresh();
    square(O_VERTS);
    for (unsigned i = 0; i < 4u; ++i) w32(at(O_VERTS) + 28u * i, r32(at(O_VERTS) + 28u * i) + 100u);
    CHECK(es2_sim_poly_fill(&loop, 4, at(O_VERTS), 0, 0, at(O_SHADES), 0, &eax) == 0 && eax == 0 && count_not(0) == 0);
    /* Depth on (with a run-list pointer: without one the rows are drawn
     * whole): a buffer of 0x10000000 passes everything; a second fill at
     * the same depth draws nothing (z must be below the buffer). */
    fresh();
    w32(ES2_SIM_VA_POLY_DEPTH_ON, 1);
    w32(ES2_SIM_VA_POLY_RUNS, at(O_OUT + 0x100u));
    w32(at(O_OUT + 0x100u), at(O_OUT + 0x120u));
    w32(0x006c5f90u, at(O_DEPTH));
    for (unsigned i = 0; i < W * H; ++i) w32(at(O_DEPTH) + 4u * i, 0x10000000u);
    square(O_VERTS);
    CHECK(es2_sim_poly_fill(&loop, 4, at(O_VERTS), 0, 0, at(O_SHADES), 0, &eax) == 0 && count_not(0) == 90u);
    CHECK(r32(at(O_DEPTH) + 4u * (5u * W + 10u)) == ((0x100u << 4) | (5u << 24)));
    memset(host(O_BITS), 0, W * H);
    CHECK(es2_sim_poly_fill(&loop, 4, at(O_VERTS), 0, 0, at(O_SHADES), 0, &eax) == 0 && count_not(0) == 0);
    return refusals != 0;
}

/* ---- 5. depth runs */
static int depth(void) {
    fresh();
    uint32_t eax = 0;
    for (unsigned i = 0; i < 16u; ++i) w32(at(O_DEPTH) + 4u * i, i >= 5u && i < 9u ? 0x10u : 0x1000u);
    w32(0x004a5adcu, at(O_DEPTH));
    w32(0x004a5ad8u, 20);
    w32(0x004a5ae0u, 0x100);
    w32(0x004a5ae4u, 0);
    w32(0x004a5ae8u, 12);
    es2_sim_poly_depth_runs(&loop, &eax);
    CHECK(eax == 0x004a5944u && r32(0x004a5938u) == 2u && r32(0x004a593cu) == 0x004a5944u);
    CHECK(r32(0x004a5944u) == 20u && r32(0x004a5948u) == 5u && r32(0x004a594cu) == 29u && r32(0x004a5950u) == 3u);
    CHECK(r32(at(O_DEPTH)) == 0x100u && r32(at(O_DEPTH) + 20u) == 0x10u);
    return 0;
}

/* ---- 6. the 3D fill: depth 256 projects x, z unchanged around the centre */
static void points(void) {
    static const int32_t p[4][2] = {{-22, 19}, {-13, 19}, {-13, 10}, {-22, 10}};
    for (unsigned i = 0; i < 4u; ++i) {
        w32(at(O_POINTS) + 12u * i, (uint32_t)p[i][0]);
        w32(at(O_POINTS) + 12u * i + 4u, 256u);
        w32(at(O_POINTS) + 12u * i + 8u, (uint32_t)p[i][1]);
        w32(at(O_UV) + 8u * i, 0x100u);
        w32(at(O_UV) + 8u * i + 4u, 0x200u);
    }
}
static int fill_3d(void) {
    fresh();
    uint32_t eax = 0;
    points();
    CHECK(es2_sim_poly_fill_3d(&loop, at(O_POINTS), at(O_UV), 4, 0, at(O_SHADES), 0, 1, 0, &eax) == 0 && eax == ES2_SIM_VA_POLY_EDGES);
    CHECK(count_not(0) == 90u && *pixel(10, 5) == 0x33 && *pixel(19, 13) == 0x33);
    CHECK(r32(0x004d4d74u + 0xcu) == 0x10000u && r32(0x004d4d74u) == 10u && r32(0x004d4d74u + 4u) == 5u);
    /* Two points behind the near distance: the crossings get depth near and are
     * placed by 0x48c6d0 alone (view x + centre), here x 10 again. */
    fresh();
    points();
    w32(at(O_POINTS) + 4u, 0); w32(at(O_POINTS) + 12u * 3u + 4u, 0);
    CHECK(es2_sim_poly_fill_3d(&loop, at(O_POINTS), at(O_UV), 4, 0, at(O_SHADES), 0, 1, 0, &eax) == 0 && refusals == 0);
    CHECK(r32(0x004d46e4u + 8u) == 16u && r32(0x004d46e4u + 3u * 28u + 8u) == 16u && r32(0x004d46e4u) == 10u && r32(0x004d46e4u + 28u) == 19u && count_not(0) == 90u);
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
    CHECK(setup_dispatch() == 0);
    if (helpers() || clippers() || edges() || fills() || depth() || fill_3d()) return 1;
    puts("PASS: poly: fixed-point helpers and their #DE refusals, haze, pages, the x/y cuts and clippers, edge tables with "
         "every flag and the flag-2 refusal, texture/flat/Gouraud fills, transparency, clipping, the depth path and its "
         "runs, and the 3D fill with the near cut");
    return 0;
}
