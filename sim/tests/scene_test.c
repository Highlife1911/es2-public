/* Sky, horizon, object binning and the sort tree (es2/sim/scene.h, src/scene.c)
 * on a synthetic image. Zeroed host memory with the module's DATA, the objects,
 * the horizon and the graphics context written by this test; the main loop's
 * call area at its own host address. Every callee the module calls (the
 * registry hooks, the other packages' rows, the graphics driver's slots and the
 * objects' vtable slots) is a recording stand-in of this test with a simple
 * scripted answer; the real callees are covered by the in-vitro differential
 * (differential_scene_test). No game data. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "es2/sim/scene.h"
#include "es2/sim/load.h"
#include "es2/sim/memory.h"
#include "es2/sim/services.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

#define IMAGE_BASE 0x00400000u
#define IMAGE_SIZE 0x002d8000u
#define HEAP_BASE 0x09000000u
#define HEAP_SIZE 0x00400000u
#define LOCAL_SIZE 0x00080000u
#define COUNTER (IMAGE_BASE + 0x2d7ff0u)

/* The callees, as recording stand-ins. */
#define VA_ASSERT 0x00479c78u
#define VA_GRID_BEGIN 0x0046e944u
#define VA_GRID_AT 0x0046e62cu
#define VA_GRID_CELL 0x0046e8c4u
#define VA_GRID_MARK 0x0046e5a4u
#define VA_HORIZON_EDGE 0x00470a1cu
#define VA_TERRAIN_DRAW 0x0046d1a8u
#define VA_POOL6_HIDDEN 0x00408240u
#define VA_POOL6_CELL 0x00408228u
#define VA_POOL9_CELL 0x0046b850u
#define VA_LIGHT_UPDATE 0x00407098u
#define VA_SHAPE_LIGHT 0x0042e8e8u
#define VA_DEPTH 0x004680f0u
#define VA_SCENE_LIGHTS 0x00407878u
#define VA_OBJECT_SELECT 0x0048c444u
#define VA_POLY_FILL 0x00484222u
#define VA_POLY_FILL_CLIP 0x00483eb8u
#define VA_RECT_FILL 0x00486704u
#define VA_PALETTE_SWAP 0x004303b0u
#define VA_SKY_AFTER 0x0042eb40u
#define VA_LINE_BASE_DTOR 0x00475900u
#define VA_DELETE 0x00477548u
#define VA_COMPARE_SHAPES 0x0042900cu
#define VA_COMPARE_KIND 0x0042937cu
#define VA_HEADING_COS 0x0047dff0u
#define VA_MULTIPLY_Q10 0x0047e0b0u
#define VA_DIST_GATE 0x0049288cu
#define VA_POOL_PREV 0x00471c70u
#define VA_CAMERA_ATTACHED 0x00401078u
/* Stand-ins reached through pointers (vtable slots, driver slots). */
#define FN_DRAW 0x006d1000u
#define FN_POS 0x006d1010u
#define FN_SIZE 0x006d1020u
#define FN_SHAPE 0x006d1030u
#define FN_SKY 0x006d1040u
#define FN_SIZE2 0x006d1050u
#define FN_DRV_GET 0x006d1060u
#define FN_DRV_SET 0x006d1070u
#define FN_DRV_PALETTE 0x006d1080u

/* The module's globals (scene.c). */
#define GRID 0x004a0be0u
#define CELL_TABLE 0x004cf4f0u
#define CELL_COUNT 0x004cf874u
#define BUCKETS 0x004aaafcu
#define RECORDS 0x004cf884u
#define DRAW_LIST 0x004cf888u
#define LOOSE 0x004cf88cu
#define LOOSE_COUNT 0x004cf92cu
#define PICK_ON 0x004cf930u
#define PICK_STATE 0x004cf931u
#define PICKED 0x004cf93cu
#define PICK_SLOTS 0x004cf940u
#define PICK_SAVED 0x004cf960u
#define PICK_DONE 0x004cf970u
#define PICK_TARGET 0x004cf974u
#define PICK_PREV 0x004cf97cu
#define PICK_RESULT 0x004cf980u
#define DRAW_DIST 0x004cf988u
#define DIST_FACTORS 0x0049abb0u
#define PICK_PENDING 0x0049abbcu
#define SORT_CAMERA 0x0049ac3cu
#define SORT_ROOT 0x0049ac40u
#define CAMERA 0x004d24eau
#define DEVICE 0x006c5ed0u
#define DRV_GET 0x004a57bcu
#define DRV_SET 0x004a57c0u
#define DRV_PALETTE 0x004a57ecu
#define GC 0x006c5f70u
#define HORIZON 0x0049aee0u
#define JOLT_PALETTE 0x0049aef8u
#define JOLT_FIRST 0x004cfd0cu
#define JOLT_CURRENT 0x004cfd08u
#define JOLT_ON 0x0049aef3u
#define CENTRE_X 0x006c6034u
#define CENTRE_Y 0x006c6038u
#define POOL_FLAT 0x004a968du
#define POOL_BUILDING 0x004a95a0u
#define POOL_MECH 0x004a9b7au
#define POOL_FLYER 0x004a9db9u
#define POOL_5 0x004a96c2u
#define POOL_6 0x004a961eu
#define POOL_7 0x004a9642u
#define POOL_8 0x004a966eu
#define POOL_9 0x006b4f30u
#define POOL_10 0x004a96aau

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
static es2_sim_scene scene;
static es2_vt vt;
static unsigned refusals;
static uint32_t first_refused;
static void on_refuse(const es2_sim_refusal *r, void *user) {
    (void)user;
    if (!refusals) first_refused = r->va;
    ++refusals;
    if (getenv("SCENE_TEST_VERBOSE")) fprintf(stderr, "refused 0x%08x: %s\n", (unsigned)r->va, r->reason ? r->reason : "");
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

/* ---- the recording stand-ins: the call order, the first four arguments */
typedef struct hook_call { uint32_t va, a[4]; } hook_call;
#define MAX_CALLS 512u
static hook_call calls[MAX_CALLS];
static unsigned call_count;
/* Scripted answers. */
static uint32_t pool_list[16][8];    /* per pool token 0..15: the objects in walk order, 0-terminated */
static uint32_t grid_answer, cell_answer, attached_answer, compare_answer, hidden_answer, pool6_cell_answer, pool9_cell_answer;
static uint32_t dist_answer;
static uint32_t pool_token(uint32_t t) { return t & 15u; }
static unsigned count_of(uint32_t va) {
    unsigned n = 0;
    for (unsigned i = 0; i < call_count && i < MAX_CALLS; ++i) n += calls[i].va == va;
    return n;
}
static const hook_call *nth(uint32_t va, unsigned k) {
    for (unsigned i = 0; i < call_count && i < MAX_CALLS; ++i)
        if (calls[i].va == va && !k--) return &calls[i];
    return NULL;
}
static void svc_stub(es2_sim_service_call *c, void *u) {
    (void)u;
    const uint32_t *s = c->slots;
    if (call_count < MAX_CALLS) {
        hook_call *h = &calls[call_count];
        h->va = c->va;
        for (unsigned i = 0; i < 4u; ++i) h->a[i] = s[i];
    }
    ++call_count;
    c->eax = 0;
    switch (c->va) {
    case VA_POOL_PREV: {
        const uint32_t *l = pool_list[pool_token(s[0])];
        unsigned i = 0;
        if (s[1]) { while (l[i] && l[i] != s[1]) ++i; if (l[i]) ++i; }
        c->eax = l[i];
        break;
    }
    case VA_DIST_GATE: c->eax = dist_answer + r32(s[0]); break;
    case VA_GRID_AT: c->eax = grid_answer; break;
    case VA_GRID_CELL: c->eax = cell_answer; break;
    case VA_CAMERA_ATTACHED: c->eax = attached_answer | r32(s[1] + 0x2dcu); break;
    case VA_COMPARE_KIND: case VA_COMPARE_SHAPES: c->eax = compare_answer; break;
    case VA_POOL6_HIDDEN: c->eax = hidden_answer; break;
    case VA_POOL6_CELL: c->eax = pool6_cell_answer; break;
    case VA_POOL9_CELL: c->eax = pool9_cell_answer; break;
    case VA_HEADING_COS: c->eax = s[0] == 0xc000u ? 0x4000u : s[0] == 0 ? 0u : (uint32_t)(int16_t)(s[0] & 0x3fffu); break;
    case VA_MULTIPLY_Q10: c->eax = (uint32_t)(((int64_t)(int32_t)s[0] * (int32_t)s[1]) >> 10); break;
    case VA_PALETTE_SWAP: c->eax = s[0] ^ 0x5au; break;
    case FN_POS: c->eax = s[0] + 0x2c0u; break;
    case FN_SIZE: c->eax = r32(s[0] + 0x2d0u); break;
    case FN_SHAPE: c->eax = r32(s[0] + 0x2d4u); break;
    case FN_SIZE2: c->eax = r32(s[0] + 0x2d8u); break;
    case FN_DRV_GET: c->eax = r32(s[0]); break;
    case FN_DRV_SET: w32(s[0], s[1]); break;
    default: break;
    }
}
static int setup_dispatch(void) {
    es2_sim_dispatch_init(&table, storage, 4096, on_refuse, NULL);
    services.memory = &memory;
    services.heap = &heap;
    services.vt = &vt;
    static uint32_t fs0 = 0x00abcdefu;
    services.fs0 = &fs0;
    if (es2_sim_register_services(&table, &services)) return 1;
    static const struct { uint32_t va; unsigned slots; } h[] = {
        {VA_LINE_BASE_DTOR, 2}, {VA_COMPARE_SHAPES, 3}, {VA_COMPARE_KIND, 2},
        {VA_HEADING_COS, 1}, {VA_MULTIPLY_Q10, 2}, {VA_DIST_GATE, 2}, {VA_POOL_PREV, 2}, {VA_CAMERA_ATTACHED, 2},
        {FN_DRAW, 1}, {FN_POS, 1}, {FN_SIZE, 1}, {FN_SHAPE, 1}, {FN_SKY, 2}, {FN_SIZE2, 1}, {FN_DRV_GET, 1}, {FN_DRV_SET, 2},
        {FN_DRV_PALETTE, 1}};
    for (unsigned i = 0; i < sizeof(h) / sizeof(h[0]); ++i)
        if (es2_sim_dispatch_add_service(&table, h[i].va, "stub", svc_stub, NULL, 0, (uint8_t)h[i].slots)) return 1;
    const char *error = NULL;
    uint32_t bad = 0;
    if (es2_sim_dispatch_seal(&table, &error, &bad)) return 1;
    /* Other packages' rows the module calls: replaced by the stand-in. */
    static const uint32_t replaced[] = {VA_LIGHT_UPDATE, VA_SCENE_LIGHTS, VA_OBJECT_SELECT, VA_RECT_FILL, VA_PALETTE_SWAP, VA_DELETE, VA_SKY_AFTER, VA_DEPTH, VA_GRID_MARK, VA_TERRAIN_DRAW, 0x0049667cu,
                                       VA_POLY_FILL, VA_POLY_FILL_CLIP,   /* the fill layer has rows */
                                       VA_SHAPE_LIGHT,   /* 0x42e8e8 has a row */
                                       VA_POOL6_HIDDEN, VA_POOL6_CELL,   /* the explosion binning callees have rows */
                                       VA_GRID_BEGIN, VA_GRID_AT, VA_GRID_CELL, VA_HORIZON_EDGE,   /* the grid leaves have rows */
                                       VA_POOL9_CELL,   /* 0x46b850 has a row */
                                       VA_ASSERT};   /* 0x479c78 has a row */
    for (unsigned i = 0; i < sizeof(replaced) / sizeof(replaced[0]); ++i) {
        es2_sim_entry *e = (es2_sim_entry *)es2_sim_dispatch_find(&table, replaced[i]);
        if (!e || e->kind != ES2_SIM_SERVICE) return 1;
        e->service = svc_stub;
        e->service_user = NULL;
    }
    return 0;
}

/* The objects' vtable (draw, pos, -, -, size, ..., shape +0x24, sky +0x34, size2 +0x5c). */
#define VTABLE (HEAP_BASE + 0x100u)
static void fresh(void) {
    memset(image, 0, IMAGE_SIZE);
    w32(0x006cbee4u, COUNTER);
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
    es2_sim_loop_bind(&loop);
    es2_sim_load_headless(&load, &loop);
    es2_sim_load_bind(&load);
    memset(&scene, 0, sizeof(scene));
    scene.loop = &loop;
    es2_sim_scene_bind(&scene);
    w32(VTABLE, FN_DRAW);
    w32(VTABLE + 4u, FN_POS);
    w32(VTABLE + 0x10u, FN_SIZE);
    w32(VTABLE + 0x24u, FN_SHAPE);
    w32(VTABLE + 0x34u, FN_SKY);
    w32(VTABLE + 0x5cu, FN_SIZE2);
    w32(DRV_GET, FN_DRV_GET);
    w32(DRV_SET, FN_DRV_SET);
    w32(DRV_PALETTE, FN_DRV_PALETTE);
    static const uint32_t pools[10] = {POOL_FLAT, POOL_BUILDING, POOL_MECH, POOL_FLYER, POOL_5, POOL_6, POOL_7, POOL_8, POOL_9, POOL_10};
    for (unsigned i = 0; i < 10u; ++i) w32(pools[i], 0x7000u + i);
    memset(pool_list, 0, sizeof(pool_list));
    grid_answer = cell_answer = attached_answer = compare_answer = hidden_answer = pool6_cell_answer = pool9_cell_answer = 0;
    dist_answer = 0;
    refusals = 0;
    call_count = 0;
}
static uint32_t object(unsigned i, uint16_t kind) {
    uint32_t o = HEAP_BASE + 0x1000u + 0x300u * i;
    w32(o, VTABLE);
    w16(o + 4u, kind);
    w32(o + 0x45u, HEAP_BASE + 0x200u);   /* the vehicles' skip record (+0x14 zero) */
    return o;
}
static int row(uint32_t va, const uint32_t *slots, unsigned n, es2_sim_result *r) {
    uint32_t regs[3] = {0x0e0e0e0eu, 0x0d0d0d0du, 0x0c0c0c0cu};
    uint32_t at = local_guest + 0x20000u;
    for (unsigned i = 0; i < n; ++i) w32(at + 4u * i, slots[i]);
    memset(r, 0, sizeof(*r));
    return es2_sim_call(&table, va, (const uint32_t *)(uintptr_t)at, n, regs, r);
}
#define ROW(va, r, ...) row((va), (const uint32_t[]){__VA_ARGS__}, (unsigned)(sizeof((const uint32_t[]){__VA_ARGS__}) / sizeof(uint32_t)), (r))

/* ---- the cell table 0x4281b0 and the stores 0x4282f8/0x42837c/0x4283b4/0x4282d8 */
static int cells(void) {
    es2_sim_result r;
    fresh();
    CHECK(ROW(0x004281b0u, &r, 3, 4) == 0 && r.eax == 0u && r32(CELL_COUNT) == 1u);
    CHECK(ROW(0x004281b0u, &r, 5, 6) == 0 && r.eax == 1u && r32(CELL_COUNT) == 2u);
    CHECK(ROW(0x004281b0u, &r, 3, 4) == 0 && r.eax == 0u && r32(CELL_COUNT) == 2u);
    CHECK(r16(CELL_TABLE + 6u) == 5u && r16(CELL_TABLE + 8u) == 6u && r16(CELL_TABLE + 10u) == 1u);
    CHECK(call_count == 0);
    w32(CELL_COUNT, 0x96u);
    CHECK(ROW(0x004281b0u, &r, 7, 8) == 0 && r.eax == 0x96u && r32(CELL_COUNT) == 0x00080008u);   /* entry 0x96 overwrites the count, as in the original */
    CHECK(call_count == 1u && calls[0].va == VA_ASSERT && calls[0].a[0] == 0x006c56c8u && calls[0].a[3] == 0x0049abbdu);
    /* The store: without a record onto the loose list, with one into the bucket. */
    fresh();
    uint32_t o = object(0, 3), rec = HEAP_BASE + 0x300u;
    CHECK(ROW(0x004282f8u, &r, o, 0) == 0 && r32(LOOSE) == o && r32(LOOSE_COUNT) == 1u && r.edx == 0u && r.eax == 0x0e0e0e0eu);
    w8(rec, 9); w8(rec + 1u, 2); w8(rec + 2u, 7);
    CHECK(ROW(0x004282f8u, &r, o, rec) == 0 && r32(BUCKETS + 4u * 7u) == o && r.eax == 7u && r.edx == 0u && r.ecx == BUCKETS);
    CHECK(ROW(0x004281b0u, &r, 1, 1) == 0 && r.eax == 1u);
    w8(rec, 1); w8(rec + 1u, 1); w8(rec + 2u, 0xfa);
    CHECK(ROW(0x004282f8u, &r, o, rec) == 0 && r32(BUCKETS + 1000u + 4u * 0xfau) == o && r.edx == 125u);
    CHECK(count_of(VA_ASSERT) == 1u);
    /* By position: the pos slot, the grid lookup with the size. */
    grid_answer = rec;
    w8(rec + 2u, 3);
    w32(o + 0x2d0u, 0x1234ffffu);
    call_count = 0;
    CHECK(ROW(0x004282d8u, &r, o) == 0 && r.eax == rec && r32(BUCKETS + 1000u + 12u) == o);
    CHECK(calls[0].va == FN_SIZE && calls[1].va == FN_POS && calls[2].va == VA_GRID_AT);
    CHECK(calls[2].a[1] == o + 0x2c0u && calls[2].a[2] == 0xffffffffu);
    cell_answer = 0;
    CHECK(ROW(0x004283b4u, &r, o, HEAP_BASE + 0x400u) == 0 && r.eax == 0u && r32(LOOSE + 4u) == o);
    return 0;
}

/* ---- the binning 0x42841c over every pool */
static int binning(void) {
    es2_sim_result r;
    fresh();
    uint32_t flat = object(0, 1), flat2 = object(1, 1), bld = object(2, 2), hidden = object(3, 2), mech = object(4, 3), linked = object(5, 3),
             p5 = object(6, 5), p6 = object(7, 6), p9 = object(8, 9), skip_rec = HEAP_BASE + 0x220u;
    w32(hidden + 0x45u, skip_rec);
    w32(skip_rec + 0x14u, 1);
    w32(linked + 0x2b0u, HEAP_BASE + 0x500u);
    pool_list[0][0] = flat; pool_list[0][1] = flat2;
    pool_list[1][0] = bld; pool_list[1][1] = hidden;
    pool_list[2][0] = mech; pool_list[2][1] = linked;
    pool_list[4][0] = p5;
    pool_list[5][0] = p6;
    pool_list[8][0] = p9;
    w32(LOOSE_COUNT, 5);
    dist_answer = 0x7530;   /* not below 30000: the flats stay out */
    CHECK(ROW(0x0042841cu, &r) == 0 && r.eax == 0u && refusals == 0);
    CHECK(count_of(VA_GRID_BEGIN) == 1u && count_of(VA_DIST_GATE) == 2u);
    /* No cells: bld, mech, linked, p5, p6, p9 on the loose list in pool order; the hidden one skipped. */
    CHECK(r32(LOOSE_COUNT) == 6u);
    CHECK(r32(LOOSE) == bld && r32(LOOSE + 4u) == mech && r32(LOOSE + 8u) == linked && r32(LOOSE + 12u) == p5);
    CHECK(r32(LOOSE + 16u) == p6 && r32(LOOSE + 20u) == p9);
    CHECK(r16(bld + 0x1e8u) == 0xffffu && r16(mech + 0x1eau) == 0xffffu && r16(hidden + 0x1e8u) == 0u);
    CHECK(nth(VA_GRID_CELL, 0) && nth(VA_GRID_CELL, 0)->a[1] == HEAP_BASE + 0x500u + 0x1e8u);
    CHECK(nth(VA_POOL9_CELL, 0) && count_of(VA_POOL6_HIDDEN) == 1u && count_of(VA_POOL6_CELL) == 1u);
    /* The flats near, the cells found, pool 6 hidden. */
    fresh();
    flat = object(0, 1);
    mech = object(4, 3);
    p6 = object(7, 6);
    uint32_t rec = HEAP_BASE + 0x300u;
    w8(rec, 2); w8(rec + 1u, 3); w8(rec + 2u, 1);
    pool_list[0][0] = flat;
    pool_list[2][0] = mech;
    pool_list[5][0] = p6;
    w32(LOOSE_COUNT, 0);
    dist_answer = 0x752f;
    grid_answer = rec;
    hidden_answer = 1;
    CHECK(ROW(0x0042841cu, &r) == 0 && refusals == 0);
    CHECK(r32(LOOSE_COUNT) == 0u && r32(CELL_COUNT) == 1u && r32(BUCKETS + 4u) == mech);
    CHECK(r16(mech + 0x1e8u) == 2u && r16(mech + 0x1eau) == 3u);
    CHECK(count_of(VA_GRID_AT) == 2u && count_of(VA_POOL6_CELL) == 0u);
    return 0;
}

/* ---- the draw distances 0x428bc0 and the cull 0x428c08 */
static int distances(void) {
    es2_sim_result r;
    fresh();
    for (unsigned k = 0; k < 5u; ++k) w16(DIST_FACTORS + 2u * k, (uint16_t)(0x100u * (k + 1u)));
    CHECK(ROW(0x00428bc0u, &r, 4096) == 0 && r.eax == 5120u);
    for (unsigned k = 0; k < 5u; ++k) CHECK(r32(DRAW_DIST + 4u * k) == 1024u * (k + 1u));
    uint32_t o = object(0, 0);
    call_count = 0;
    CHECK(ROW(0x00428c08u, &r, o, 2048) == 0 && r.eax == 0u);
    CHECK(ROW(0x00428c08u, &r, o, 2049) == 0 && r.eax == 1u);
    w16(o + 4u, 8);
    CHECK(ROW(0x00428c08u, &r, o, 3073) == 0 && r.eax == 1u);
    w16(o + 4u, 2);
    CHECK(ROW(0x00428c08u, &r, o, 1024) == 0 && r.eax == 0u && call_count == 0u);
    w16(o + 4u, 5);
    w32(o + 0x2d0u, 0x1b57u);
    CHECK(ROW(0x00428c08u, &r, o, 4097) == 0 && r.eax == 1u && call_count == 1u);
    w32(o + 0x2d0u, 0x1b58u);
    CHECK(ROW(0x00428c08u, &r, o, 4097) == 0 && r.eax == 0u);
    return 0;
}

/* ---- the sort tree 0x429620 and the cell callback 0x428c60 */
static int sorting(void) {
    es2_sim_result r;
    fresh();
    uint32_t nodes = HEAP_BASE + 0x8000u, list = HEAP_BASE + 0x9000u, recs = HEAP_BASE + 0xa000u;
    w32(SORT_ROOT, nodes);
    static const int32_t d[5] = {50, 10, 70, 30, 60};
    for (unsigned i = 0; i < 5u; ++i) {
        uint32_t rc = recs + 0x40u * i;
        w32(rc, VTABLE);
        w16(rc + 0x16u, 0);
        w32(rc + 0x12u, (uint32_t)d[i]);
        w32(list + 4u * i, rc);
    }
    CHECK(ROW(0x00429620u, &r, list, 5) == 0 && r.eax == 0u && refusals == 0);
    /* Kind 0 against kind 0: nearer goes right; the walk draws left first:
     * the farthest first. */
    CHECK(count_of(FN_DRAW) == 5u);
    static const unsigned order[5] = {2, 4, 0, 3, 1};
    for (unsigned i = 0; i < 5u; ++i) CHECK(nth(FN_DRAW, i)->a[0] == recs + 0x40u * order[i]);
    /* A zero word count: nothing, EAX kept. */
    call_count = 0;
    CHECK(ROW(0x00429620u, &r, list, 0x10000u) == 0 && r.eax == 0x0e0e0e0eu && call_count == 0u);
    /* Kinds 1/2 first, through the kind comparator; mixed through the shapes comparator. */
    fresh();
    w32(SORT_ROOT, nodes);
    for (unsigned i = 0; i < 3u; ++i) {
        uint32_t rc = recs + 0x40u * i;
        w32(rc, VTABLE);
        w16(rc + 0x16u, (uint16_t)(i == 1 ? 3u : 1u + i / 2u));
        w32(list + 4u * i, rc);
    }
    compare_answer = 0x10000u;   /* the word is zero: left */
    CHECK(ROW(0x00429620u, &r, list, 3) == 0 && refusals == 0);
    CHECK(count_of(VA_COMPARE_KIND) == 1u && count_of(VA_COMPARE_SHAPES) == 2u);
    CHECK(nth(VA_COMPARE_SHAPES, 0)->a[2] == 1u);
    CHECK(r32(nodes + 8u) == recs && r32(nodes + 0x1du + 8u) == recs + 0x80u && r32(nodes + 0x3au + 8u) == recs + 0x40u);
    CHECK(r32(nodes) == nodes + 0x1du && r32(nodes + 0x1du) == nodes + 0x3au);
    /* The cell callback: kind 9 at once, attached skipped, culled, accepted. */
    fresh();
    w32(SORT_ROOT, nodes);
    w32(RECORDS, recs);
    w32(DRAW_LIST, list);
    w32(CAMERA, HEAP_BASE + 0x600u);
    w32(0x004a5b08u, 3);
    w32(DRAW_DIST + 4u, 1000);
    uint32_t o9 = object(0, 9), oa = object(1, 0), of = object(2, 0), ok = object(3, 0);
    w32(oa + 0x2dcu, 0x00010001u);   /* attached: the low word */
    w32(ok + 0x2dcu, 0x00010000u);   /* not attached */
    w32(of + 0x2c0u, 1000);          /* the distance 1500: beyond kind 0's 1000 */
    w32(ok + 0x2c0u, 7);
    w32(ok + 0x2c8u, 9);
    w32(ok + 0x2d0u, 0xabcd1234u);
    for (unsigned i = 0; i < 4u; ++i) w32(recs + 0x36u * i, VTABLE);
    CHECK(ROW(0x004281b0u, &r, 4, 5) == 0);
    w32(BUCKETS + 4u, o9); w32(BUCKETS + 8u, oa); w32(BUCKETS + 12u, of); w32(BUCKETS + 16u, ok);
    call_count = 0;
    attached_answer = 0;
    dist_answer = 500;
    CHECK(ROW(0x00428c60u, &r, 4, 5, 4) == 0 && refusals == 0);
    CHECK(r32(0x004a5b04u) == 3u && nth(FN_DRAW, 0)->a[0] == o9);
    CHECK(r32(list) == recs && r32(recs + 0x32u) == ok && r32(recs + 0x12u) == 507u && r32(SORT_CAMERA) == HEAP_BASE + 0x600u);
    CHECK(r32(recs + 4u) == 7u && r32(recs + 0xcu) == 9u && r16(recs + 0x10u) == 0x1234u && r16(recs + 0x16u) == 0u);
    CHECK(count_of(FN_DRAW) == 2u && nth(FN_DRAW, 1)->a[0] == recs && count_of(VA_DIST_GATE) == 2u);
    return 0;
}

/* ---- the per-object draw 0x42876c, the finish 0x42883c, the pick */
static int picking(void) {
    es2_sim_result r;
    fresh();
    uint32_t o = object(0, 0), rec = HEAP_BASE + 0xa000u, dev = HEAP_BASE + 0xb000u;
    w32(rec + 4u, 1); w32(rec + 8u, 2); w32(rec + 0xcu, 3); w16(rec + 0x10u, 0xfff0u); w32(rec + 0x12u, 77); w32(rec + 0x32u, o);
    w16(0x004cf984u, 1);
    w8(PICK_ON, 1);
    w32(PICK_STATE, 5);
    w8(PICK_PENDING, 1);
    w32(PICK_TARGET, o);
    w32(DEVICE, dev);
    CHECK(ROW(0x0042876cu, &r, rec) == 0 && refusals == 0);
    CHECK(calls[0].va == VA_LIGHT_UPDATE && calls[1].va == VA_SHAPE_LIGHT && calls[1].a[2] == 1u && calls[2].va == VA_DEPTH);
    CHECK(calls[3].va == FN_DRAW && calls[4].va == VA_SCENE_LIGHTS);
    CHECK(r32(PICK_RESULT) == 5u && r32(PICKED) == o && r32(PICK_PREV) == 5u && r32(PICK_STATE) == 1u);
    /* The pick restore ran: four slots saved and set to 2. */
    for (unsigned k = 0; k < 4u; ++k) CHECK(r32(PICK_SLOTS + 8u * k) == 2u);
    /* The finish: loose draws, the pick close-down, the pick save. */
    call_count = 0;
    w32(LOOSE_COUNT, 1);
    w32(LOOSE, o);
    w32(0x004d24e6u, o);
    w32(o + 0x1f2u, HEAP_BASE + 0x700u);
    w16(HEAP_BASE + 0x750u, 1);
    w16(o + 0x1e8u, 4); w16(o + 0x1eau, 6);
    w32(dev + 0x8cu, 0x1234u);
    w32(PICK_SLOTS + 8u, 7);
    CHECK(ROW(0x0042883cu, &r, HEAP_BASE + 0x600u, 0) == 0 && refusals == 0);
    CHECK(calls[0].va == FN_DRAW && calls[1].va == VA_CAMERA_ATTACHED && calls[2].va == VA_GRID_MARK && calls[2].a[1] == 4u);
    CHECK(r32(dev + 0x38u) == 0x1234u && r32(0x004a3686u) == 0x1234u && r8(PICK_ON) == 0u && r32(PICKED) == 0u);
    CHECK(r8(PICK_DONE + 1u) == 1u && r8(PICK_DONE) == 0u && r8(PICK_PENDING) == 0u);
    CHECK(r32(PICK_SLOTS) == r32(PICK_SAVED));
    CHECK(ROW(0x004282c0u, &r) == 0 && r.eax == 0u);
    CHECK(ROW(0x004282c8u, &r) == 0 && r.eax == 1u);
    return 0;
}

/* ---- the jolt palette 0x42ea44/0x42ea70/0x42ea94 and the destructor 0x42f6d2 */
static int jolt(void) {
    es2_sim_result r;
    fresh();
    CHECK(ROW(0x0042ea44u, &r) == 0 && r.eax == 0x0e0e0e0eu && call_count == 0u);
    w32(JOLT_PALETTE, 0x100u);
    CHECK(ROW(0x0042ea44u, &r) == 0 && r.eax == 0x15au && r32(JOLT_FIRST) == 0x15au && r32(JOLT_CURRENT) == 0x15au && r8(JOLT_ON) == 1u);
    CHECK(ROW(0x0042ea70u, &r) == 0 && r.eax == 0x100u && r32(JOLT_CURRENT) == 0x100u && r8(JOLT_ON) == 0u);
    CHECK(ROW(0x0042ea94u, &r) == 0 && r.eax == 0x100u && r8(JOLT_ON) == 0u);
    w32(COUNTER, 5);
    call_count = 0;
    CHECK(ROW(0x0042f6d2u, &r, HEAP_BASE + 0x40u, 1) == 0 && r32(COUNTER) == 4u && count_of(VA_DELETE) == 1u);
    CHECK(ROW(0x0042f6d2u, &r, HEAP_BASE + 0x40u, 0) == 0 && r32(COUNTER) == 3u && count_of(VA_DELETE) == 1u);
    CHECK(ROW(0x0042f6d2u, &r, 0, 1) == 0 && r.eax == 0x0e0e0e0eu && r32(COUNTER) == 3u);
    return 0;
}

/* ---- the horizon: line, sky bands, ground on the level and rolled paths; the entry */
static int horizon(void) {
    es2_sim_result r;
    fresh();
    uint32_t h = HEAP_BASE + 0xc000u, gc = HEAP_BASE + 0xd000u, cam = HEAP_BASE + 0x600u, sky = object(0, 0);
    w32(GC, gc);
    w32(gc + 0x210u, 0); w32(gc + 0x214u, 0); w32(gc + 0x218u, 319); w32(gc + 0x21cu, 199);
    w32(CENTRE_X, 160);
    w32(CENTRE_Y, 100);
    w32(h + 0x28u, 40); w32(h + 0x2cu, 80);
    w32(h + 0x54u, 8); w32(h + 0x58u, 3); w32(h + 0x5cu, 8); w32(h + 0x60u, 4); w32(h + 0x6cu, 10);
    /* Level: pitch and roll 0. */
    CHECK(ROW(0x0042ec08u, &r, h, cam, 0, 0) == 0 && refusals == 0);
    CHECK(count_of(VA_POLY_FILL) == 0u && count_of(VA_POLY_FILL_CLIP) == 0u);
    CHECK(count_of(VA_RECT_FILL) == 3u + 4u);
    CHECK(r32(gc + 0x22cu) == 0u);
    /* Rolled: quads. */
    w16(cam + 0x12u, 0x0100u);
    call_count = 0;
    CHECK(ROW(0x0042ec08u, &r, h, cam, 6, 0) == 0 && refusals == 0);
    CHECK(count_of(VA_POLY_FILL) == 3u && count_of(VA_POLY_FILL_CLIP) == 4u && count_of(VA_RECT_FILL) == 0u);
    CHECK(r16(h + 0x36u) == 0x0100u);
    /* Flat ground: one band to the edge point. */
    w8(h + 0x71u, 1);
    call_count = 0;
    CHECK(ROW(0x0042ebe8u, &r, h, cam, 0) == 0 && refusals == 0 && count_of(VA_POLY_FILL_CLIP) == 1u);
    /* The entry: no horizon object is refused at 0x42e70c. */
    w32(0x0049aee4u, sky);
    w32(HORIZON, h);
    call_count = 0;
    CHECK(ROW(0x0042e700u, &r, cam) == 0 && refusals == 0);
    CHECK(calls[0].va == VA_HORIZON_EDGE && calls[0].a[2] == h + 0x72u && nth(FN_SKY, 0) && nth(FN_SKY, 0)->a[1] == cam);
    CHECK(count_of(VA_OBJECT_SELECT) == 1u && count_of(VA_TERRAIN_DRAW) == 1u && calls[call_count - 1u].va == VA_SKY_AFTER);
    w32(HORIZON, 0);
    CHECK(ROW(0x0042e700u, &r, cam) != 0 && refusals >= 1u && first_refused == 0x0042e70cu);
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
    if (cells() || binning() || distances() || sorting() || picking() || jolt() || horizon()) {
        if (refusals) fprintf(stderr, "first refusal 0x%08x\n", (unsigned)first_refused);
        return 1;
    }
    printf("PASS: scene: cell table and stores, binning over the pools, draw distances and cull, sort tree and cell callback, "
           "per-object draw with the pick and the finish, jolt palette and destructor, horizon level/rolled/flat and the entry "
           "(%llu service calls)\n",
           (unsigned long long)scene.calls);
    return 0;
}
