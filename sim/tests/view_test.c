/* The view transform, the projection and the lights (es2/sim/view.h,
 * src/view.c) on a synthetic image. Zeroed host memory with the camera, the
 * context, the lights and the light table written by this test at the
 * original's addresses, the game heap over an emulated OS region, the main
 * loop's call area at its own host address; the sim's real services answer the
 * heap rows and the light rows of mission_load.h (0x48bc40, 0x48be50, 0x48bcec, 0x4800c0);
 * the registry hooks the module calls (matrix_ctor, vector_subtract,
 * euler_matrix, matrix_compose, matrix_transpose, rotate_point,
 * transform_point, matrix_point, dist3d_forward, vector_angle) are recording
 * stand-ins of this test at the hooks' VAs with simple arithmetic (compose
 * adds, transform adds the translation, rotate copies, the distance is the
 * Manhattan length). The real callees are covered by the in-vitro differential
 * (differential_view_test). No game data. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "es2/sim/view.h"
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

/* The hooks the module calls, as recording stand-ins. */
#define VA_MATRIX_CTOR 0x00495132u
#define VA_VECTOR_SUBTRACT 0x00492a30u
#define VA_EULER_MATRIX 0x0047ebb8u
#define VA_MATRIX_COMPOSE 0x0047fa20u
#define VA_MATRIX_TRANSPOSE 0x0047df19u
#define VA_ROTATE_POINT 0x00480304u
#define VA_TRANSFORM_POINT 0x0048043cu
#define VA_MATRIX_POINT 0x004801d4u
#define VA_DIST3D 0x00492aafu
#define VA_VECTOR_ANGLE 0x0049290cu

/* The module's globals (view.c). */
#define CAMERA_MATRIX 0x006c5f94u
#define VIEW_MATRIX 0x006c5fb4u
#define INVERSE_MATRIX 0x006c5fd4u
#define VIEW_POINTER 0x006c5ff4u
#define OBJECT_POINTER 0x006c5ff8u
#define IDENTITY 0x006c5ffcu
#define EYE 0x006c601cu
#define FOCAL_SHIFT 0x006c6028u
#define VIEW_FLAG 0x006c602cu
#define FOCAL_DIVISOR 0x006c6030u
#define CENTRE_X 0x006c6034u
#define CENTRE_Y 0x006c6038u
#define VIEWPORT 0x006c603cu
#define LIGHT_COUNT 0x006c60a8u
#define LIGHT_LIST 0x006c60acu
#define CAMERA_BUILT 0x004a5b0cu
#define LIGHTS_ON 0x006cbc04u
#define LIGHT_TABLE 0x004a9608u

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
static es2_vt vt;
static unsigned refusals;
static uint32_t first_refused;
static void on_refuse(const es2_sim_refusal *r, void *user) {
    (void)user;
    if (!refusals) first_refused = r->va;
    ++refusals;
    if (getenv("VIEW_TEST_VERBOSE")) fprintf(stderr, "refused 0x%08x: %s\n", (unsigned)r->va, r->reason ? r->reason : "");
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
static void vec(uint32_t a, int32_t x, int32_t y, int32_t z) { w32(a, (uint32_t)x); w32(a + 4u, (uint32_t)y); w32(a + 8u, (uint32_t)z); }
static int is_vec(uint32_t a, int32_t x, int32_t y, int32_t z) { return r32(a) == (uint32_t)x && r32(a + 4u) == (uint32_t)y && r32(a + 8u) == (uint32_t)z; }

/* The recording stand-ins: the call order and the arguments. */
typedef struct hook_call { uint32_t va, a0, a1, a2; } hook_call;
static hook_call hooks[64];
static unsigned hook_count;
static uint32_t angle_result;
static uint32_t absval(uint32_t v) { return (int32_t)v < 0 ? 0u - v : v; }
static void svc_hook(es2_sim_service_call *c, void *u) {
    (void)u;
    uint32_t a0 = c->slots[0], a1 = c->slots[1], a2 = c->slots[2];
    if (hook_count < 64u) { hook_call *h = &hooks[hook_count]; h->va = c->va; h->a0 = a0; h->a1 = a1; h->a2 = a2; }
    ++hook_count;
    c->eax = a0;
    switch (c->va) {
    case VA_VECTOR_SUBTRACT: w32(a0, r32(a0) - r32(a1)); w32(a0 + 4u, r32(a0 + 4u) - r32(a1 + 4u)); break;
    case VA_MATRIX_COMPOSE: for (uint32_t i = 0; i < 0x20u; i += 4u) w32(a2 + i, r32(a0 + i) + r32(a1 + i)); c->eax = a2; break;
    case VA_ROTATE_POINT: case VA_MATRIX_POINT: vec(a2, (int32_t)r32(a0), (int32_t)r32(a0 + 4u), (int32_t)r32(a0 + 8u)); c->eax = a2; break;
    case VA_TRANSFORM_POINT:
        vec(a2, (int32_t)(r32(a0) + r32(a1 + 0x14u)), (int32_t)(r32(a0 + 4u) + r32(a1 + 0x18u)), (int32_t)(r32(a0 + 8u) + r32(a1 + 0x1cu)));
        c->eax = a2;
        break;
    case VA_DIST3D: c->eax = absval(a0) + absval(a1) + absval(a2); break;
    case VA_VECTOR_ANGLE: c->eax = angle_result; break;
    case VA_EULER_MATRIX: c->eax = a1; break;
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
        {VA_MATRIX_CTOR, 1}, {VA_VECTOR_SUBTRACT, 2}, {VA_EULER_MATRIX, 2}, {VA_MATRIX_COMPOSE, 3}, {VA_MATRIX_TRANSPOSE, 1},
        {VA_ROTATE_POINT, 3}, {VA_TRANSFORM_POINT, 3}, {VA_MATRIX_POINT, 3}, {VA_DIST3D, 3}, {VA_VECTOR_ANGLE, 2}};
    for (unsigned i = 0; i < sizeof(h) / sizeof(h[0]); ++i)
        if (es2_sim_dispatch_add_service(&table, h[i].va, "hook", svc_hook, NULL, 0, h[i].slots)) return 1;
    const char *error = NULL;
    uint32_t bad = 0;
    return es2_sim_dispatch_seal(&table, &error, &bad);
}
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
    memset(&view, 0, sizeof(view));
    view.loop = &loop;
    es2_sim_view_bind(&view);
    /* The vtable slots the module calls through and the identity's pointers. */
    w32(0x0049f8c0u + 4u, 0x0048bcecu);
    w32(0x00497ac8u + 4u, 0x0048bd6cu);
    w32(OBJECT_POINTER, IDENTITY);
    w32(VIEW_POINTER, IDENTITY);
    for (uint32_t i = 0; i < 0x14u; i += 4u) w32(IDENTITY + i, 0x00010000u * (i / 4u + 1u));
    refusals = 0;
    hook_count = 0;
    angle_result = 0;
}
static int row(uint32_t va, const uint32_t *slots, unsigned n, es2_sim_result *r) {
    uint32_t regs[3] = {0, 0, 0};
    uint32_t at = local_guest + 0x20000u;
    for (unsigned i = 0; i < n; ++i) w32(at + 4u * i, slots[i]);
    memset(r, 0, sizeof(*r));
    return es2_sim_call(&table, va, (const uint32_t *)(uintptr_t)at, n, regs, r);
}
#define ROW(va, r, ...) row((va), (const uint32_t[]){__VA_ARGS__}, (unsigned)(sizeof((const uint32_t[]){__VA_ARGS__}) / sizeof(uint32_t)), (r))

/* ---- the projection leaves 0x48c5cc, 0x48c5fc, 0x48c6d0 and the #DE refusal. */
static int projection(void) {
    es2_sim_result r;
    fresh();
    uint32_t v = HEAP_BASE + 0x100u, s = HEAP_BASE + 0x200u, t = HEAP_BASE + 0x300u;
    vec(v, 100, 50, -20);
    w32(FOCAL_SHIFT, 8);
    w32(CENTRE_X, 160);
    w32(CENTRE_Y, 100);
    CHECK(ROW(0x0048c5fcu, &r, v, s) == 0 && r.eax == s);
    CHECK(r32(s) == 512u && (int32_t)r32(s + 4u) == -102);
    CHECK(ROW(0x0048c6d0u, &r, s, t) == 0 && r.eax == t && r.edx == s);
    CHECK(r32(t) == 672u && r32(t + 4u) == 202u);
    CHECK(ROW(0x0048c5ccu, &r, 10, 50) == 0 && r.eax == 51u && r.edx == 10u && r.ecx == 8u);
    w32(FOCAL_SHIFT, 0);
    w32(FOCAL_DIVISOR, 64);
    CHECK(ROW(0x0048c5fcu, &r, v, s) == 0 && r32(s) == 400u && (int32_t)r32(s + 4u) == -80);
    CHECK(ROW(0x0048c5ccu, &r, 10, 0) == 0 && r.eax == 40u && r.edx == 0u);
    w32(FOCAL_DIVISOR, 0);
    CHECK(ROW(0x0048c5ccu, &r, 10, 50) != 0 && refusals == 2u && first_refused == 0x0048c5e2u);
    w32(FOCAL_SHIFT, 8);
    vec(v, 100, 0, -20);
    CHECK(ROW(0x0048c5fcu, &r, v, s) != 0 && refusals == 4u);
    return 0;
}

/* ---- the view set-up 0x48c2e4: the constants, the viewport and centre,
 * the camera pose, its inverse, 0x48c3b0's composition and the eye. */
static int view_setup(void) {
    es2_sim_result r;
    fresh();
    uint32_t cam = HEAP_BASE + 0x1000u, context = HEAP_BASE + 0x2000u;
    vec(cam + 4u, 1, 2, 3);
    vec(cam + 0x10u, 0x100, 0x200, 0x300);
    w32(cam + 0x16u, context);
    w32(cam + 0x1au, 8);
    w32(cam + 0x1eu, 1);
    w32(cam + 0x22u, 64);
    vec(context + 0x210u, 10, 20, 30);
    w32(context + 0x21cu, 40);
    vec(context + 0x220u, 3, 4, 0);
    CHECK(ROW(0x0048c2e4u, &r, cam) == 0 && refusals == 0);
    CHECK(r.eax == EYE);
    CHECK(r8(CAMERA_BUILT) == 1u);
    CHECK(r32(FOCAL_SHIFT) == 8u && r32(VIEW_FLAG) == 1u && r32(FOCAL_DIVISOR) == 0u);
    CHECK(is_vec(VIEWPORT, 10, 20, 30) && r32(VIEWPORT + 12u) == 40u);
    CHECK(r32(CENTRE_X) == 7u && r32(CENTRE_Y) == 16u);
    CHECK(is_vec(CAMERA_MATRIX + 0x14u, -1, -2, -3));
    CHECK(r32(VIEW_POINTER) == CAMERA_MATRIX && r32(OBJECT_POINTER) == IDENTITY);
    CHECK(is_vec(VIEW_MATRIX + 0x14u, -1, -2, -3) && r32(VIEW_MATRIX) == 0x00010000u);
    CHECK(is_vec(INVERSE_MATRIX + 0x14u, 1, 2, 3));
    CHECK(is_vec(EYE, 1, 2, 3));
    static const uint32_t order[] = {VA_MATRIX_CTOR, VA_VECTOR_SUBTRACT, VA_EULER_MATRIX, VA_MATRIX_TRANSPOSE, VA_ROTATE_POINT,
                                     VA_MATRIX_COMPOSE, VA_MATRIX_TRANSPOSE, VA_ROTATE_POINT, VA_TRANSFORM_POINT};
    CHECK(hook_count == 9u);
    for (unsigned i = 0; i < 9u; ++i) CHECK(hooks[i].va == order[i]);
    CHECK(hooks[0].a0 == CAMERA_MATRIX && hooks[1].a0 == CENTRE_X && hooks[1].a1 == context + 0x220u);
    CHECK(hooks[2].a0 == cam + 0x10u && hooks[2].a1 == CAMERA_MATRIX);
    CHECK(hooks[5].a0 == IDENTITY && hooks[5].a1 == CAMERA_MATRIX && hooks[5].a2 == VIEW_MATRIX);
    CHECK(hooks[8].a1 == INVERSE_MATRIX && hooks[8].a2 == EYE);
    /* The second frame: the constructor does not run again; the shift 0 takes the divisor. */
    w32(cam + 0x1au, 0);
    CHECK(ROW(0x0048c2e4u, &r, cam) == 0 && r8(CAMERA_BUILT) == 1u && hooks[9].va == VA_VECTOR_SUBTRACT);
    CHECK(r32(FOCAL_SHIFT) == 0u && r32(FOCAL_DIVISOR) == 64u);
    return 0;
}

/* ---- the object selection 0x48c444 with lights on: the composition
 * order, the object matrix's inverse, every active light transformed. */
static int object_select(void) {
    es2_sim_result r;
    fresh();
    uint32_t m = HEAP_BASE + 0x3000u, l1 = HEAP_BASE + 0x4000u, l2 = HEAP_BASE + 0x4100u;
    for (uint32_t i = 0; i < 0x14u; i += 4u) w32(m + i, 0x00100000u);
    vec(m + 0x14u, 10, 20, 30);
    vec(CAMERA_MATRIX + 0x14u, -1, -2, -3);
    w32(VIEW_POINTER, CAMERA_MATRIX);
    vec(l1 + 4u, 100, 200, 300);
    vec(l2 + 4u, 5, 6, 7);
    w32(LIGHT_COUNT, 2);
    w32(LIGHT_LIST, l1);
    w32(LIGHT_LIST + 4u, l2);
    w8(LIGHTS_ON, 1);
    CHECK(ROW(0x0048c444u, &r, m) == 0 && refusals == 0);
    CHECK(r.eax == l2 + 0x1eu);
    CHECK(r32(OBJECT_POINTER) == m && r32(VIEW_POINTER) == CAMERA_MATRIX);
    CHECK(hooks[0].va == VA_MATRIX_COMPOSE && hooks[0].a0 == m && hooks[0].a1 == CAMERA_MATRIX && hooks[0].a2 == VIEW_MATRIX);
    CHECK(r32(VIEW_MATRIX) == 0x00100000u && is_vec(VIEW_MATRIX + 0x14u, 9, 18, 27));
    CHECK(is_vec(INVERSE_MATRIX + 0x14u, -9, -18, -27) && is_vec(EYE, -9, -18, -27));
    CHECK(is_vec(l1 + 0x22u, 90, 180, 270) && is_vec(l2 + 0x22u, -5, -14, -23));
    CHECK(is_vec(m + 0x14u, 10, 20, 30));  /* the inverse is taken on a copy */
    /* Lights off: the lights stay, EAX is the local copy's address. */
    w8(LIGHTS_ON, 0);
    vec(l1 + 0x22u, 0, 0, 0);
    CHECK(ROW(0x0048c444u, &r, 0) == 0 && is_vec(l1 + 0x22u, 0, 0, 0) && r32(OBJECT_POINTER) == IDENTITY);
    CHECK(r.eax >= local_guest && r.eax < local_guest + 0x10000u);
    return 0;
}

/* ---- the lighting sums 0x48bfe8 (polygon) and 0x48c16c (terrain) over
 * one light of each kind, and the clamp. */
static int lighting(void) {
    es2_sim_result r;
    fresh();
    uint32_t l0 = HEAP_BASE + 0x5000u, l1 = HEAP_BASE + 0x5100u, l2 = HEAP_BASE + 0x5200u;
    uint32_t n = HEAP_BASE + 0x6000u, p = HEAP_BASE + 0x6100u;
    w32(l0 + 0x16u, 0); w32(l0 + 0x1au, 0x10);
    w32(l1 + 0x16u, 1); w32(l1 + 0x1au, 0x80);
    w16(l1 + 0x2eu, 0); w16(l1 + 0x30u, 0); w16(l1 + 0x32u, (uint16_t)-0x100);
    w32(l2 + 0x16u, 2); w32(l2 + 0x1au, 4); w32(l2 + 0x34u, 16); w32(l2 + 0x38u, 0x100);
    vec(l2 + 0x22u, 100, 0, 0);
    w32(LIGHT_COUNT, 3);
    w32(LIGHT_LIST, l0); w32(LIGHT_LIST + 4u, l1); w32(LIGHT_LIST + 8u, l2);
    w16(n, 0x4000); w16(n + 2u, 0); w16(n + 4u, 0x4000);
    w16(p, 0); w16(p + 2u, 0); w16(p + 4u, 0);
    CHECK(ROW(0x0048bfe8u, &r, n, p) == 0 && refusals == 0);
    CHECK(r.eax == 16u + 128u + 69u);
    CHECK(hook_count == 1u && hooks[0].va == VA_DIST3D && hooks[0].a0 == 100u);
    vec(p, 10, 0, 0);
    CHECK(ROW(0x0048c16cu, &r, n, p) == 0 && r.eax == 16u + 128u + 76u);
    CHECK(hooks[1].va == VA_VECTOR_SUBTRACT && hooks[2].va == VA_DIST3D && hooks[2].a0 == 90u);
    /* The clamp, and a light behind the face contributes nothing. */
    w32(l0 + 0x1au, 0x200);
    CHECK(ROW(0x0048bfe8u, &r, n, p) == 0 && r.eax == 0xffu);
    w32(l0 + 0x1au, 0);
    w16(l1 + 0x32u, 0x100);
    vec(l2 + 0x22u, -100, 0, 0);
    w16(p, 0);
    CHECK(ROW(0x0048bfe8u, &r, n, p) == 0 && r.eax == 0u);
    /* A zero divisor (len + inner = 0) is refused. */
    vec(l2 + 0x22u, 100, 0, 0);
    w32(l2 + 0x34u, (uint32_t)-100);
    CHECK(ROW(0x0048bfe8u, &r, n, p) != 0 && refusals == 2u && first_refused == 0x0048c133u);
    return 0;
}

/* ---- the active list 0x48be98 and the saved view 0x48d7ec. */
static int list_and_load(void) {
    es2_sim_result r;
    fresh();
    uint32_t a = HEAP_BASE + 0x7000u, b = HEAP_BASE + 0x7100u, c = HEAP_BASE + 0x7200u;
    w32(LIGHT_COUNT, 3);
    w32(LIGHT_LIST, a); w32(LIGHT_LIST + 4u, b); w32(LIGHT_LIST + 8u, c);
    CHECK(ROW(0x0048be98u, &r, a) == 0 && (r.eax & 0xffu) == 1u && r.edx == c && r.ecx == 2u);
    CHECK(r32(LIGHT_COUNT) == 2u && r32(LIGHT_LIST) == c && r32(LIGHT_LIST + 4u) == b);
    CHECK(ROW(0x0048be98u, &r, a) == 0 && r.eax == 0u && r32(LIGHT_COUNT) == 2u);
    /* A saved view with two lights: both copies land in the first active
     * light (c, after the removal above), as in the original. */
    uint32_t p = HEAP_BASE + 0x7800u;
    for (uint32_t i = 0; i < 0x20u; i += 4u) w32(p + i, 0x1000u + i);
    w32(p + 0x20u, 0x11111111u);
    w32(p + 0x24u, 0x22222222u);
    vec(p + 0x28u, 7, 8, 9);
    vec(p + 0x34u + 4u, 1, 2, 3); w32(p + 0x34u + 0x10u, 0xaabbccddu); w16(p + 0x34u + 0x14u, 0xeeffu);
    vec(p + 0x4au + 4u, 4, 5, 6); w32(p + 0x4au + 0x10u, 0x01020304u); w16(p + 0x4au + 0x14u, 0x0506u);
    w8(LIGHTS_ON, 1);
    CHECK(ROW(0x0048d7ecu, &r, p) == 0 && refusals == 0 && r.eax == c + 0x22u);
    CHECK(r32(OBJECT_POINTER) == 0x11111111u && r32(VIEW_POINTER) == 0x22222222u);
    for (uint32_t i = 0; i < 0x20u; i += 4u) CHECK(r32(VIEW_MATRIX + i) == 0x1000u + i);
    CHECK(is_vec(INVERSE_MATRIX + 0x14u, -0x1014, -0x1018, -0x101c) && is_vec(EYE, 7, 8, 9));
    CHECK(is_vec(c + 0x22u, 4, 5, 6) && r32(c + 0x2eu) == 0x01020304u && r16(c + 0x32u) == 0x0506u);
    CHECK(r32(b + 0x22u) == 0u && r32(b + 0x2eu) == 0u);
    CHECK(hook_count == 2u && hooks[0].va == VA_MATRIX_TRANSPOSE && hooks[1].va == VA_ROTATE_POINT);
    return 0;
}

/* ---- the light table: the entry life cycle 0x406ee4/0x406f38/0x407048/
 * 0x406fbc, the per-object update 0x407098 with the spot and point lights
 * from the heap and the pools, and the full table. */
static int light_table(void) {
    es2_sim_result r;
    fresh();
    uint32_t t = HEAP_BASE + 0x8000u, obj = HEAP_BASE + 0x9000u, e0 = t + 0x6cu;
    w32(LIGHT_TABLE, t);
    for (uint32_t i = 0; i < 20u; ++i) w8(t + 0x6cu + 0x23u * i, 1);
    CHECK(ROW(0x00406ee4u, &r, t, 32 * 5, 32 * 7) == 0 && r32(t + 0x10u) == 5u && r32(t + 0x14u) == 7u);
    vec(obj, 1000, 0, 0);
    CHECK(ROW(0x00406f38u, &r, t, obj) == 0 && r.eax == 0u);
    CHECK(r8(e0) == 0u && is_vec(e0 + 1u, 1000, 0, 0) && r16(e0 + 0xdu) == 0xffu);
    CHECK(r32(e0 + 0xfu) == 5u && r32(e0 + 0x13u) == 7u && r32(t + 0x18u) == 1u && r32(t + 0x1cu) == e0);
    CHECK(r32(e0 + 0x17u) == 5872u);
    CHECK(ROW(0x00407048u, &r, t, 0, 0x80) == 0 && r.eax == 3027u && r.edx == 160u);
    CHECK(r16(e0 + 0xdu) == 0x80u && r32(e0 + 0x1bu) == 0x80u && r32(e0 + 0x17u) == 3027u);
    /* The object 100 units away, heading inside the cone: a spot light. */
    vec(obj, 1100, 0, 0);
    w32(obj + 0xcu, 32 * 9);
    angle_result = 0x1000;
    CHECK(ROW(0x00407098u, &r, t, obj) == 0 && refusals == 0);
    uint32_t l = r32(e0 + 0x1fu);
    CHECK(l >= HEAP_BASE && l < HEAP_BASE + HEAP_SIZE);
    CHECK(r32(LIGHT_COUNT) == 1u && r32(LIGHT_LIST) == l);
    CHECK(r32(l) == 0x0049f8c0u && r32(l + 0x16u) == 1u && r32(l + 0x1au) == 112u);
    CHECK(r32(l + 0x10u) == 0x800u && r16(l + 0x14u) == 0u);
    CHECK(hooks[0].va == VA_VECTOR_SUBTRACT && hooks[1].va == VA_DIST3D && hooks[2].va == VA_VECTOR_ANGLE);
    CHECK(hooks[2].a0 == 3u && hooks[2].a1 == 9u);
    /* Heading outside the cone: the spot light returns to its pool, a point light takes over. */
    angle_result = 0x2000;
    CHECK(ROW(0x00407098u, &r, t, obj) == 0 && refusals == 0);
    uint32_t l2 = r32(e0 + 0x1fu);
    CHECK(l2 != l && l2 >= HEAP_BASE && l2 < HEAP_BASE + HEAP_SIZE);
    CHECK(r32(t + 0x35cu) == 1u && r32(t + 0x360u) == l);
    CHECK(r32(LIGHT_COUNT) == 1u && r32(LIGHT_LIST) == l2);
    CHECK(r32(l2) == 0x00497ac8u && r32(l2 + 0x16u) == 2u && r32(l2 + 0x1au) == 0x80u);
    CHECK(r32(l2 + 0x34u) == 160u && r32(l2 + 0x38u) == 224u && r32(l2 + 0x3cu) == 0x80u * 224u);
    CHECK(is_vec(l2 + 4u, 1000, 0, 0));
    /* Out of range: released; the entry freed and taken again. */
    vec(obj, 6000, 0, 0);
    CHECK(ROW(0x00407098u, &r, t, obj) == 0 && r32(e0 + 0x1fu) == 0u && r32(LIGHT_COUNT) == 0u);
    CHECK(r32(t + 0x3b0u) == 1u && r32(t + 0x3b4u) == l2);
    CHECK(ROW(0x00406fbcu, &r, t, 0) == 0 && r.eax == 0u && r8(e0) == 1u && r32(t + 0x18u) == 0u);
    CHECK(ROW(0x00406fbcu, &r, t, 0) == 0 && r.eax == t);
    CHECK(ROW(0x00406f38u, &r, t, obj) == 0 && r.eax == 0u && r32(t + 0x18u) == 1u);
    /* Back in range: the pooled point light is reused, no new allocation. */
    angle_result = 0x2000;
    CHECK(ROW(0x00407048u, &r, t, 0, 0x80) == 0);
    vec(obj, 6100, 0, 0);
    uint32_t heap_before = next_heap, c0 = r32(COUNTER);
    CHECK(ROW(0x00407098u, &r, t, obj) == 0 && r32(e0 + 0x1fu) == l2 && r32(t + 0x3b0u) == 0u && r32(LIGHT_COUNT) == 1u);
    CHECK(next_heap == heap_before && r32(COUNTER) == c0);
    /* Nineteen more entries fill the table; the twenty-first is refused. */
    for (uint32_t i = 1; i < 20u; ++i) CHECK(ROW(0x00406f38u, &r, t, obj) == 0 && r.eax == i);
    CHECK(ROW(0x00406f38u, &r, t, obj) != 0 && refusals == 2u && first_refused == 0x00406f69u);
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
    if (projection() || view_setup() || object_select() || lighting() || list_and_load() || light_table()) {
        if (refusals) fprintf(stderr, "first refusal 0x%08x\n", (unsigned)first_refused);
        return 1;
    }
    printf("PASS: view: projection leaves and #DE refusals, view set-up (%u hook calls), object select with lights, "
           "polygon and terrain lighting, active list and saved view, light table with pools (%llu service calls)\n",
           hook_count, (unsigned long long)view.calls);
    return 0;
}
