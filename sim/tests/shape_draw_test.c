/* The DTS shape draw slots (es2/sim/shape_draw.h, src/shape_draw.c) on a
 * synthetic image. Zeroed host memory with a shape world written by this test
 * (a polygon group of solid, shaded, skipped and clipped polygons, part lists,
 * a cell animation, a detail part, a BSP part, an instance, an actor) and the
 * module's globals at the original's addresses; the main loop's call area at
 * its own host address. The sim's real services answer the module's rows and
 * view.c's simple leaves (0x48dadc, 0x48d9cf, 0x48d99e); the fill layer
 * 0x483eb8/0x484222/ 0x4842bb, the haze table 0x468158, the shade lookup
 * 0x430e34, the leaf parts' draw slot, the registry hooks and view.c's rows
 * the module calls (0x48c444, 0x48d7ec, 0x48c57c, 0x48c5fc, 0x48c6d0, 0x48bfe8,
 * 0x4954e4, 0x48db93, 0x48dbd6) are recording stand-ins of this test with
 * simple arithmetic. The real callees are covered by the in-vitro differential
 * (differential_shape_draw_test). No game data. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "es2/sim/shape_draw.h"
#include "es2/sim/view.h"
#include "es2/sim/load.h"
#include "es2/sim/memory.h"
#include "es2/sim/services.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

#define IMAGE_BASE 0x00400000u
#define IMAGE_SIZE 0x002d8000u
#define HEAP_BASE 0x09000000u
#define HEAP_SIZE 0x00100000u
#define LOCAL_SIZE 0x00080000u
#define COUNTER (IMAGE_BASE + 0x2d7ff0u)

/* Stand-in VAs. */
#define VA_FILL_FLAT 0x00483eb8u
#define VA_FILL_BACK 0x00484222u
#define VA_FILL_PLAIN 0x004842bbu
#define VA_HAZE 0x00468158u
#define VA_SHADE 0x00430e34u
#define VA_LEAF 0x00400010u           /* the leaf parts' draw slot (no row of its own) */
static const uint32_t hook_vas[][2] = {
    {0x0047fa20u, 3}, {0x0047ebb8u, 2}, {0x0047d32cu, 2}, {0x0047dff0u, 1}, {0x0047e0b0u, 2}, {0x0047de72u, 3}, {0x004801d4u, 3},
};
static const uint32_t replaced_vas[] = {0x0048c444u, 0x0048d7ecu, 0x0048c57cu, 0x0048c5fcu, 0x0048c6d0u,
                                         0x0048bfe8u, 0x004954e4u, 0x0048db93u, 0x0048dbd6u};

/* Globals of the module. */
#define GC 0x006c5f70u
#define OBJECT_POINTER 0x006c5ff8u
#define IDENTITY 0x006c5ffcu
#define EYE 0x006c601cu
#define SHIFT 0x006c6028u
#define G_NEAR 0x006c602cu
#define CENTRE 0x006c6034u
#define COLOUR 0x006c6050u
#define MODE 0x006c6054u
#define COLOUR2 0x006c6058u
#define RECORD 0x006c6064u
#define G_OUT 0x006cbb02u
#define DETAIL_TEST 0x004a62e0u
#define PART_SCALE 0x004a101cu

/* The world (guest addresses in the heap region). */
#define W HEAP_BASE
#define W_GC (W + 0x0000u)
#define W_VT (W + 0x0400u)            /* vtables of 0x20 */
#define W_POINTS (W + 0x0600u)
#define W_INDEX (W + 0x0700u)
#define W_COLOURS (W + 0x0780u)
#define W_POLYS (W + 0x0800u)
#define W_POLY_PTRS (W + 0x0900u)
#define W_PARTS (W + 0x0a00u)         /* parts of 0x30 */
#define W_PTRS (W + 0x0e00u)
#define W_HAZE (W + 0x1000u)
#define W_TRANSFORMS (W + 0x1400u)
#define W_CELLS (W + 0x1500u)
#define W_BSP (W + 0x1600u)
#define W_BSPT (W + 0x1700u)
#define W_INST (W + 0x1800u)
#define W_ACTOR (W + 0x1880u)
#define W_TABLE (W + 0x1900u)
enum { K_SOLID, K_SHADED, K_GOURAUD, K_LIST, K_GROUP, K_CELLS, K_DETAIL, K_BSP, K_LEAF, K_INSTANCE, K_COUNT };
static uint32_t vt(unsigned k) { return W_VT + 0x20u * k; }
static uint32_t part(unsigned i) { return W_PARTS + 0x30u * i; }

static uint8_t *image, *heap_bytes, *local;
static uint32_t local_guest;
static es2_sim_arena arena;
static es2_guest_memory memory;
static es2_sim_heap heap;
static es2_sim_services services;
static es2_sim_entry storage[4096];
static es2_sim_dispatch table;
static es2_sim_loop loop;
static es2_sim_load load;
static es2_sim_view view;
static es2_sim_shape_draw draw;
static es2_vt vt_state;
static unsigned refusals;
static uint32_t first_refused;
static void on_refuse(const es2_sim_refusal *r, void *user) {
    (void)user;
    if (!refusals) first_refused = r->va;
    ++refusals;
    if (getenv("SHAPE_DRAW_TEST_VERBOSE")) fprintf(stderr, "refused 0x%08x: %s\n", (unsigned)r->va, r->reason ? r->reason : "");
}
static uint32_t heap_alloc(void *c, uint32_t address, uint32_t size, uint32_t type, uint32_t protect) {
    (void)c; (void)size; (void)type; (void)protect;
    return address;
}
static uint32_t os_free(void *c, uint32_t a, uint32_t s, uint32_t t) { (void)c; (void)a; (void)s; (void)t; return 1; }

static uint32_t r32(uint32_t a) { return memory.read32(memory.context, a); }
static uint16_t r16(uint32_t a) { return memory.read16(memory.context, a); }
static void w32(uint32_t a, uint32_t v) { memory.write32(memory.context, a, v); }
static void w16(uint32_t a, uint16_t v) { memory.write16(memory.context, a, v); }
static void w8(uint32_t a, uint8_t v) { es2_sim_write8(&memory, a, v); }
static int32_t sx(uint32_t a) { return (int32_t)(int16_t)r16(a); }
static void wvec(uint32_t a, int x, int y, int z) { w16(a, (uint16_t)x); w16(a + 2u, (uint16_t)y); w16(a + 4u, (uint16_t)z); }

/* The log of the stand-ins: fills and leaf draws in order. */
typedef struct entry { uint32_t kind, n, colour, mode, colour2, x, y, clip, record; } entry;
static entry log_[64];
static unsigned log_count, compose_count, load_count;
static uint32_t dist_result, cos_result;
static void add(entry e) { if (log_count < 64u) log_[log_count] = e; ++log_count; }
static void svc_stand_in(es2_sim_service_call *c, void *u) {
    (void)u;
    uint32_t a0 = c->slots[0], a1 = c->slots[1], a2 = c->slots[2];
    c->eax = a0;
    switch (c->va) {
    case VA_FILL_FLAT: case VA_FILL_BACK: case VA_FILL_PLAIN: {
        entry e = {c->va, a0, r32(COLOUR), r32(MODE), r32(COLOUR2), r32(a1), r32(a1 + 4u), 0, 0};
        add(e);
        c->eax = 0x7000u + a0;
        break;
    }
    case VA_LEAF: {
        entry e = {VA_LEAF, a0, 0, 0, 0, 0, 0, r32(r32(GC) + 0x208u), r32(RECORD)};
        add(e);
        c->eax = a0 + 0x100u;
        break;
    }
    case VA_HAZE: c->eax = W_HAZE; break;
    case VA_SHADE: c->eax = (a0 + a1) & 0xffu; break;
    case 0x0048bfe8u: c->eax = 5; break;                                            /* polygon lighting */
    case 0x0048c444u: w32(OBJECT_POINTER, a0); break;                              /* object select */
    case 0x0048d7ecu: ++load_count; break;                                          /* view load */
    case 0x0048c57cu: for (uint32_t i = 0; i < 12u; i += 4u) w32(a1 + i, r32(a0 + i)); break;   /* view transform */
    case 0x0048c5fcu: w32(a1, r32(a0) + 160u); w32(a1 + 4u, 100u - r32(a0 + 8u)); break;     /* project point */
    case 0x0048c6d0u: w32(a1, r32(a0)); w32(a1 + 4u, r32(a0 + 4u)); break;                    /* to screen */
    case 0x004954e4u: for (uint32_t i = 0; i < 12u; i += 4u) w32(a2 + i, r32(a0 + i)); break;   /* rotate */
    case 0x004801d4u: case 0x0048db93u:                                                       /* matrix point */
        w32(a2, (uint32_t)sx(a0)); w32(a2 + 4u, (uint32_t)sx(a0 + 2u)); w32(a2 + 8u, (uint32_t)sx(a0 + 4u));
        c->eax = a2;
        break;
    case 0x0048dbd6u: c->eax = a0 + 0x1eu; break;
    case 0x0047fa20u: ++compose_count; c->eax = a2; break;
    case 0x0047de72u: c->eax = dist_result; break;
    case 0x0047e0b0u: c->eax = (uint32_t)(((int64_t)(int32_t)a0 * (int32_t)a1) >> 10); break;
    case 0x0047d32cu: c->eax = 0x100u; break;
    case 0x0047dff0u: c->eax = cos_result; break;
    default: break;
    }
}
/* Whether a services.h row exists at va (the dispatch's lookup works only after the seal). */
static int has_row(uint32_t va) {
    for (unsigned i = 0; i < es2_sim_service_count(); ++i) {
        uint32_t row = 0;
        es2_sim_service_row(i, &row);
        if (row == va) return 1;
    }
    return 0;
}
static int setup_dispatch(void) {
    es2_sim_dispatch_init(&table, storage, 4096, on_refuse, NULL);
    services.memory = &memory;
    services.heap = &heap;
    services.vt = &vt_state;
    static uint32_t fs0 = 0x00abcdefu;
    services.fs0 = &fs0;
    if (es2_sim_register_services(&table, &services)) return 1;
    /* A stand-in whose VA already has another package's row replaces that row after the seal. */
    uint32_t deferred[16];
    unsigned deferred_count = 0;
    for (unsigned i = 0; i < sizeof(hook_vas) / sizeof(hook_vas[0]); ++i) {
        if (has_row(hook_vas[i][0])) { deferred[deferred_count++] = hook_vas[i][0]; continue; }
        if (es2_sim_dispatch_add_service(&table, hook_vas[i][0], "hook", svc_stand_in, NULL, 0, (uint8_t)hook_vas[i][1])) return 1;
    }
    static const uint32_t own[][2] = {{VA_FILL_FLAT, 2}, {VA_FILL_BACK, 2}, {VA_FILL_PLAIN, 2}, {VA_HAZE, 1}, {VA_SHADE, 2}, {VA_LEAF, 1}};
    for (unsigned i = 0; i < sizeof(own) / sizeof(own[0]); ++i) {
        if (has_row(own[i][0])) { deferred[deferred_count++] = own[i][0]; continue; }
        if (es2_sim_dispatch_add_service(&table, own[i][0], "stand-in", svc_stand_in, NULL, 0, (uint8_t)own[i][1])) return 1;
    }
    const char *error = NULL;
    uint32_t bad = 0;
    if (es2_sim_dispatch_seal(&table, &error, &bad)) { fprintf(stderr, "seal: %s at 0x%08x\n", error ? error : "?", (unsigned)bad); return 1; }
    for (unsigned i = 0; i < deferred_count; ++i) {
        es2_sim_entry *e = (es2_sim_entry *)es2_sim_dispatch_find(&table, deferred[i]);
        if (!e || e->kind != ES2_SIM_SERVICE) { fprintf(stderr, "row at 0x%08x is not a service\n", (unsigned)deferred[i]); return 1; }
        e->service = svc_stand_in;
        e->service_user = NULL;
    }
    for (unsigned i = 0; i < sizeof(replaced_vas) / sizeof(replaced_vas[0]); ++i) {
        es2_sim_entry *e = (es2_sim_entry *)es2_sim_dispatch_find(&table, replaced_vas[i]);
        if (!e) { fprintf(stderr, "no row at 0x%08x\n", (unsigned)replaced_vas[i]); return 1; }
        e->service = svc_stand_in;
        e->service_user = NULL;
    }
    return 0;
}

static void polygon(unsigned i, unsigned kind, unsigned normal, unsigned point, unsigned count, unsigned first, unsigned colour) {
    uint32_t p = W_POLYS + 0x10u * i;
    w32(p, vt(kind));
    w16(p + 4u, (uint16_t)normal);
    w16(p + 6u, (uint16_t)point);
    w16(p + 8u, (uint16_t)count);
    w16(p + 0xau, (uint16_t)first);
    w16(p + 0xcu, (uint16_t)colour);
    w16(p + 0xeu, (uint16_t)first);
}
static void children(uint32_t p, const uint32_t *list, unsigned n, uint32_t ptrs) {
    w16(p + 0x10u, (uint16_t)n);
    w32(p + 0x12u, ptrs);
    for (unsigned i = 0; i < n; ++i) w32(ptrs + 4u * i, list[i]);
}
/* Parts: 0 the shape (a list of the group and leaf 2), 1 the group, 2..4
 * leaves, 5 cells, 6 detail, 7 BSP. */
static void fresh(void) {
    memset(image, 0, IMAGE_SIZE);
    w32(0x006cbee4u, COUNTER);
    memset(heap_bytes, 0, HEAP_SIZE);
    memset(local, 0, LOCAL_SIZE);
    es2_sim_heap_os os = {NULL, heap_alloc, os_free};
    es2_sim_heap_init(&heap, &memory, &os, local_guest + 0x7f000u, 0, 0, 0);
    memset(&vt_state, 0, sizeof(vt_state));
    es2_vt_enable(&vt_state, 1, 47, 0x01000000u, 0, 1);
    es2_sim_loop_defaults(&loop);
    loop.memory = &memory;
    loop.dispatch = &table;
    loop.vt = &vt_state;
    loop.stack_lo = local_guest;
    loop.stack_hi = local_guest + 0x10000u;
    es2_sim_loop_bind(&loop);
    es2_sim_load_headless(&load, &loop);
    es2_sim_load_bind(&load);
    memset(&view, 0, sizeof(view));
    view.loop = &loop;
    es2_sim_view_bind(&view);
    memset(&draw, 0, sizeof(draw));
    draw.loop = &loop;
    es2_sim_shape_draw_bind(&draw);
    refusals = 0;
    log_count = compose_count = load_count = 0;
    dist_result = 0;
    cos_result = 0x4000u;
    /* The context: a clipping viewport (0, 0)..(320, 200). */
    w32(GC, W_GC);
    w32(W_GC + 0x208u, 1);
    w32(W_GC + 0x20cu, 0x20cu);
    w32(W_GC + 0x218u, 320);
    w32(W_GC + 0x21cu, 200);
    w32(W_GC + 0x228u, 0x228u);
    w32(W_GC + 0x22cu, 0x22cu);
    w32(OBJECT_POINTER, IDENTITY);
    w32(SHIFT, 8);
    w32(G_NEAR, 0);
    w32(CENTRE, 160);
    w32(CENTRE + 4u, 100);
    w32(PART_SCALE, 0x400u);
    static const uint32_t slots[K_COUNT] = {0x00474ec0u, 0x00475538u, 0x004756d4u, 0x00476808u, 0x004759d4u,
                                            0x004768f0u, 0x004769c8u, 0x00476c18u, VA_LEAF, 0x00478da4u};
    for (unsigned k = 0; k < K_COUNT; ++k) w32(vt(k) + 0x1cu, slots[k]);
    for (uint32_t i = 0; i < 0x300u; ++i) w8(W_HAZE + i, (uint8_t)(i + 1u));
    /* Points: a square at depth 0x100, the normals (0, -1, 0) and (0, 1, 0),
     * a point behind the near plane, the square's centre. */
    wvec(W_POINTS + 0u, -16, 0x100, -16);
    wvec(W_POINTS + 6u, 16, 0x100, -16);
    wvec(W_POINTS + 12u, 16, 0x100, 16);
    wvec(W_POINTS + 18u, -16, 0x100, 16);
    wvec(W_POINTS + 24u, 0, -1, 0);
    wvec(W_POINTS + 30u, 0, 1, 0);
    wvec(W_POINTS + 36u, 0, -16, 0);
    wvec(W_POINTS + 42u, 0, 0x100, 0);
    static const uint16_t index[] = {0, 1, 2, 3, 0, 6, 1};
    for (unsigned i = 0; i < 7u; ++i) w16(W_INDEX + 2u * i, index[i]);
    static const uint32_t colours[] = {0x10, 0x11, 0x20, 0x21, 0x30, 0x31, 0x40, 0x41, 0x14000001u, 0x14000002u, 0x14000003u, 0x14000004u};
    for (unsigned i = 0; i < 12u; ++i) w32(W_COLOURS + 4u * i, colours[i]);
    /* Polygons: 0 solid front, 1 shaded back, 2 not drawn, 3 solid clipped. */
    polygon(0, K_SOLID, 4, 7, 4, 0, 0);
    polygon(1, K_SHADED, 5, 7, 3, 0, 4);
    polygon(2, K_SOLID, 4, 7, 3, 0, 8);
    polygon(3, K_SOLID, 4, 7, 3, 4, 0);
    for (unsigned i = 0; i < 4u; ++i) w32(W_POLY_PTRS + 4u * i, W_POLYS + 0x10u * i);
    for (unsigned i = 0; i < 8u; ++i) { w32(part(i), vt(K_LEAF)); w16(part(i) + 4u, 0xffffu); }
    /* The shape. */
    w32(part(0), vt(K_LIST));
    w16(part(0) + 8u, 0x10);
    wvec(part(0) + 0xau, 0, 0x100, 0);
    const uint32_t shape_children[] = {part(1), part(2)};
    children(part(0), shape_children, 2, W_PTRS);
    w32(part(0) + 0x18u, W_TRANSFORMS);
    w32(part(0) + 0x1cu, W_CELLS);
    w32(part(0) + 0x26u, 0x12345678u);
    /* The group. */
    w32(part(1), vt(K_GROUP));
    w32(part(1) + 0x10u, W_INDEX);
    w32(part(1) + 0x14u, W_POINTS);
    w32(part(1) + 0x18u, W_COLOURS);
    w32(part(1) + 0x1cu, W_POLY_PTRS);
    w16(part(1) + 0x22u, 8);
    w16(part(1) + 0x26u, 4);
    /* Cells, detail and BSP parts over leaves 2, 3, 4. */
    const uint32_t leaves[] = {part(2), part(3), part(4)};
    w32(part(5), vt(K_CELLS));
    children(part(5), leaves, 3, W_PTRS + 0x10u);
    w16(part(5) + 0x16u, 1);
    w16(W_CELLS, 5);
    w16(W_CELLS + 2u, 7);
    w32(part(6), vt(K_DETAIL));
    children(part(6), leaves, 3, W_PTRS + 0x20u);
    w16(part(6) + 8u, 0x10);
    w32(part(6) + 0x16u, W_TABLE);
    w16(W_TABLE, 10);
    w16(W_TABLE + 2u, 100);
    w32(part(7), vt(K_BSP));
    children(part(7), leaves, 2, W_PTRS + 0x30u);
    w32(part(7) + 0x18u, W_BSP);
    w32(part(7) + 0x1cu, W_BSPT);
    wvec(W_BSP, 1, 0, 0);
    w16(W_BSP + 0xau, 0x4000u);
    w16(W_BSP + 0xcu, 0x4001u);
    w16(W_BSPT, 0xffffu);
}
static int row(uint32_t va, const uint32_t *slots, unsigned n, es2_sim_result *r) {
    uint32_t regs[3] = {0, 0, 0};
    uint32_t at = local_guest + 0x20000u;
    for (unsigned i = 0; i < n; ++i) w32(at + 4u * i, slots[i]);
    memset(r, 0, sizeof(*r));
    return es2_sim_call(&table, va, (const uint32_t *)(uintptr_t)at, n, regs, r);
}
#define ROW(va, r, ...) row((va), (const uint32_t[]){__VA_ARGS__}, (unsigned)(sizeof((const uint32_t[]){__VA_ARGS__}) / sizeof(uint32_t)), (r))
static int is_fill(unsigned i, uint32_t kind, uint32_t n, uint32_t colour, uint32_t mode, uint32_t colour2) {
    const entry *e = &log_[i];
    return i < log_count && e->kind == kind && e->n == n && e->colour == colour && e->mode == mode && e->colour2 == colour2;
}
static int is_leaf(unsigned i, uint32_t p) { return i < log_count && log_[i].kind == VA_LEAF && log_[i].n == p; }

/* ---- the shape draw over the group: the colours through the haze table
 * and the shade lookup, front and back faces, the second pass, the skipped
 * polygon, the clip, the slot order and the restored context. */
static int group(void) {
    es2_sim_result r;
    fresh();
    CHECK(ROW(0x00490ab8u, &r, part(0)) == 0 && refusals == 0);
    CHECK(r.eax == 0 && r32(RECORD) == 0);
    CHECK(log_count == 7u);
    /* Polygon 0: front, colours 0x11 -> 0x12 and 0x10 -> 0x11, a second pass. */
    CHECK(is_fill(0, VA_FILL_FLAT, 4, 0x12, 0, 0x11) && is_fill(1, VA_FILL_FLAT, 4, 0x12, 4, 0x12));
    CHECK(log_[0].x == 144u && log_[0].y == 116u);
    /* Polygon 1: back, light level 5: shade(0x41, 5) -> 0x47, shade(0x40, 5) -> 0x46. */
    CHECK(is_fill(2, VA_FILL_BACK, 3, 0x47, 0, 0x46) && is_fill(3, VA_FILL_BACK, 3, 0x47, 4, 0x47));
    /* Polygon 2 is not drawn; polygon 3 crosses the near plane: four points. */
    CHECK(is_fill(4, VA_FILL_FLAT, 4, 0x12, 0, 0x11) && is_fill(5, VA_FILL_FLAT, 4, 0x12, 4, 0x12));
    CHECK(r32(G_OUT + 8u) == 159u && r32(G_OUT + 12u) == 101u);
    CHECK(is_leaf(6, part(2)) && log_[6].record == 0x12345678u && log_[6].clip == 1u);
    CHECK(r32(W_GC + 0x228u) == 0x228u && r32(W_GC + 0x22cu) == 0x22cu && r32(W_GC + 0x20cu) == 0x20cu);
    /* The skipped polygon called alone: EAX 0x14, nothing filled. */
    log_count = 0;
    CHECK(ROW(0x00474ec0u, &r, W_POLYS + 0x20u) == 0 && r.eax == 0x14u && log_count == 0);
    /* A Gouraud polygon of 33 vertices is refused (the original overwrites its locals). */
    polygon(2, K_GOURAUD, 4, 7, 33, 0, 0);
    CHECK(ROW(0x004756d4u, &r, W_POLYS + 0x20u) != 0 && first_refused == 0x004756d4u);
    /* Through the instance: its matrix becomes the shape's transform table. */
    fresh();
    w32(W_INST, vt(K_INSTANCE));
    w32(W_INST + 4u, part(0));
    w32(W_INST + 8u, W_CELLS);
    w32(W_INST + 0x16u, W_TRANSFORMS + 0x40u);
    CHECK(ROW(0x00478da4u, &r, W_INST) == 0 && refusals == 0 && log_count == 7u);
    CHECK(r32(part(0) + 0x18u) == W_TRANSFORMS + 0x40u && compose_count == 1u && r32(OBJECT_POINTER) == IDENTITY);
    return 0;
}

/* ---- the detail test: behind the near plane nothing is drawn; inside a
 * clipping viewport the clip flag is off for the draw; the #DE refusal. */
static int detail(void) {
    es2_sim_result r;
    fresh();
    w8(DETAIL_TEST, 1);
    w32(W_GC + 0x210u, 10);
    CHECK(ROW(0x00476248u, &r, part(0)) == 0 && r.eax == 2u && r32(0x006bb2f4u) == 0x400u && r32(0x004a0fe8u) == 10u);
    CHECK(ROW(0x00490ab8u, &r, part(0)) == 0 && log_count == 7u && log_[6].clip == 0u && r32(W_GC + 0x208u) == 1u);
    wvec(part(0) + 0xau, 0, -0x100, 0);
    log_count = 0;
    CHECK(ROW(0x00490ab8u, &r, part(0)) == 0 && r.eax == 0 && log_count == 0);
    wvec(part(0) + 0xau, 0, 0x100, 0);
    w32(W_GC + 0x210u, 300);       /* the square misses the viewport: 0 */
    CHECK(ROW(0x00476248u, &r, part(0)) == 0 && r.eax == 0u);
    w32(W_GC + 0x210u, 150);
    CHECK(ROW(0x00476248u, &r, part(0)) == 0 && r.eax == 1u);
    w32(W_GC + 0x210u, 11);
    cos_result = 0;
    CHECK(ROW(0x00476248u, &r, part(0)) != 0 && first_refused == 0x004762f7u);
    return 0;
}

/* ---- part kinds: cells, detail choices, BSP order, the refusals. */
static int kinds(void) {
    es2_sim_result r;
    fresh();
    w32(0x006b7b6cu, W_CELLS);
    CHECK(ROW(0x004768f0u, &r, part(5)) == 0 && log_count == 1u && is_leaf(0, part(3)) && r.eax == part(3) + 0x100u);
    w16(part(5) + 0x10u, 0);
    CHECK(ROW(0x004768f0u, &r, part(5)) != 0 && first_refused == 0x0047691cu);
    /* Detail: s = (0x10 << 8) / (d - 0x10). */
    static const struct { uint32_t dist; unsigned leaf; } d[] = {{98, 3}, {100000, 2}, {17, 4}, {0, 4}};
    for (unsigned i = 0; i < 4u; ++i) {
        fresh();
        dist_result = d[i].dist;
        CHECK(ROW(0x004769c8u, &r, part(6)) == 0 && log_count == 1u && is_leaf(0, part(d[i].leaf)));
    }
    /* The offset 0x4a1020 starts the scan and is subtracted again. */
    fresh();
    dist_result = 98;
    CHECK(ROW(0x004769b8u, &r, 1) == 0 && r.eax == 1u);
    CHECK(ROW(0x004769c8u, &r, part(6)) == 0 && is_leaf(0, part(2)));
    /* BSP: the eye's side picks the order. */
    fresh();
    w32(EYE, 5);
    CHECK(ROW(0x00476c18u, &r, part(7)) == 0 && log_count == 2u && is_leaf(0, part(3)) && is_leaf(1, part(2)));
    fresh();
    w32(EYE, (uint32_t)-5);
    CHECK(ROW(0x00476c18u, &r, part(7)) == 0 && log_count == 2u && is_leaf(0, part(2)) && is_leaf(1, part(3)));
    fresh();
    w32(EYE, (uint32_t)-5);
    w32(0x0049f248u, 1);
    w16(W_BSP + 0xcu, 0xffffu);
    CHECK(ROW(0x00476c18u, &r, part(7)) == 0 && log_count == 1u && is_leaf(0, part(2)));
    /* An actor with detail levels and no instance is refused (address 8). */
    fresh();
    w16(W_ACTOR + 0x32u, 1);
    w32(W_ACTOR + 0x41u, W_TABLE + 0x10u);
    w16(W_TABLE + 0x1cu, 1);
    CHECK(ROW(0x004033e4u, &r, W_ACTOR) != 0 && first_refused == 0x0040341du);
    return 0;
}

/* ---- the shade ranges 0x430518/0x42e8e8 and the view-space
 * projection 0x48cce4. */
#define W_RANGES (W + 0x1a00u)   /* range records of 8 bytes */
#define W_WORDS (W + 0x1b00u)
#define W_SHADE (W + 0x1c00u)
#define W_VIEWPTS (W + 0x1d00u)
static int ranges(void) {
    es2_sim_result r;
    fresh();
    w32(0x0049b034u, W_RANGES);
    w32(W_RANGES + 0x18u, W_WORDS);   /* record 3 */
    w16(W_TABLE, 0x20u);
    w16(W_TABLE + 2u, 3u);
    CHECK(ROW(0x00430518u, &r, 3, 1, W_TABLE) == 0 && r16(W_WORDS) == 0x20u && r16(W_WORDS + 4u) == 0x22u && r16(W_WORDS + 6u) == 0 &&
          r16(W_RANGES + 0x1cu) == 3u && r32(0x004a4fa5u + 0x18u) == W_WORDS && r16(0x004a4fa5u + 0x1cu) == 3u && r.eax == 3u && r.edx == 1u &&
          r.ecx == 0);
    CHECK(ROW(0x00430518u, &r, 3, 0xffffffffu, W_TABLE) != 0 && first_refused == 0x00430518u);
    /* 0x42e8e8(obj kind 2, level 15, flag 0): thresholds {10, 20} give level
     * index 1; the pair {range 3, row 1} with 2 run pairs per row takes the
     * run pair at [0x4cfcf8] + 4 + 8 = {0x40, 2}. */
    fresh();
    w32(0x0049b034u, W_RANGES);
    w32(W_RANGES + 0x18u, W_WORDS);
    w32(W_SHADE, vt(0));
    w16(W_SHADE + 4u, 2u);
    w32(0x004cfce0u, W_SHADE + 0x40u);
    w8(W_SHADE + 0x40u + 10u, 1u);
    w32(W_SHADE + 0x40u + 11u, W_SHADE + 0x60u);
    w8(W_SHADE + 0x60u, 3u);
    w8(W_SHADE + 0x61u, 1u);
    w32(0x004cfd00u, W_SHADE + 0x80u);
    w32(W_SHADE + 0x80u, 10u);
    w32(W_SHADE + 0x84u, 20u);
    w16(0x004cfcd8u, 3u);
    w32(0x004cfcf8u, W_SHADE + 0xa0u);
    w32(0x004cfcfcu, W_SHADE + 0xc0u);
    w16(0x004cfcdeu, 2u);
    w16(W_SHADE + 0xacu, 0x40u);
    w16(W_SHADE + 0xaeu, 2u);
    CHECK(ROW(0x0042e8e8u, &r, W_SHADE, 15, 0) == 0 && r.eax == 1u && r.edx == 1u && r.ecx == 0 && r16(W_WORDS) == 0x40u &&
          r16(W_WORDS + 2u) == 0x41u && r16(W_WORDS + 4u) == 0 && r16(0x004a4fa5u + 0x1cu) == 2u);
    /* An empty kind entry: nothing happens. */
    w16(W_SHADE + 4u, 1u);
    CHECK(ROW(0x0042e8e8u, &r, W_SHADE, 15, 0) == 0 && r.eax == ((W_SHADE & 0xffff0000u) | 1u) && r.edx == 1u);
    /* 0x48cce4 without a shift: (4 << 8) / 2 + 10, -(2 << 8) / 2 + 20; the state stays 0. */
    fresh();
    w32(SHIFT, 0);
    w16(0x006c68e4u, 1u);
    w16(0x006c68e6u, 0);
    w32(0x006c68f2u, W_INDEX);
    w16(W_INDEX, 0);
    w32(0x006c68ecu, W_VIEWPTS);
    w32(W_VIEWPTS, 4u);
    w32(W_VIEWPTS + 8u, 2u);
    w32(0x006c6030u, 2u);
    w32(CENTRE, 10u);
    w32(CENTRE + 4u, 20u);
    CHECK(row(0x0048cce4u, NULL, 0, &r) == 0 && r.eax == 0 && r.ecx == 0 && r32(G_OUT) == 522u && r32(G_OUT + 4u) == (uint32_t)-236 &&
          r16(0x006cbc02u) == 1u && image[0x006c68fau - IMAGE_BASE] == 0);
    w32(0x006c6030u, 0);
    CHECK(row(0x0048cce4u, NULL, 0, &r) != 0 && first_refused == 0x0048cd38u);
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
    if (group() || detail() || kinds() || ranges()) {
        if (refusals) fprintf(stderr, "first refusal 0x%08x\n", (unsigned)first_refused);
        return 1;
    }
    printf("PASS: shape draw: polygon group (haze, shade, back faces, second pass, skip, clip), instance, detail test, "
           "cells, detail parts, BSP order, refusals, shade ranges, view projection (%llu service calls)\n", (unsigned long long)draw.calls);
    return 0;
}
