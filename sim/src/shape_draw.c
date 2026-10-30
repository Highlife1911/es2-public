/* The DTS shape draw slots as own code (es2/sim/shape_draw.h). Contract:
 * rasterizer.md, "DTS shape draw" (every store and call
 * below with its listing address range); "listing" = llvm-objdump -d -M intel
 * of the reference DBSIM.EXE. Internal callees of the original are own
 * functions; the registry hooks, other packages' rows (the view rows, the
 * fill layer, the haze table, the shade lookup) and every part's virtual draw
 * slot (+0x1c) are called through the dispatch over the bound main loop's call
 * area. Guest state is read and written only through the guest view.
 *
 * Nothing here writes pixels: the module's state is the render context
 * 0x6c68e4..0x6cbc03 (vertex list, point states, view points, the
 * projected cache and the output polygon), the fill state 0x6c6050..
 * 0x6c6067, the part transform state 0x6b7b68..0x6bb2f7 (the saved views
 * of 0x110 bytes), the cached detail scale 0x4a0fe8/0x6bb2f4 and the
 * detail level words 0x497368/0x49736c/0x4a9868/0x4a95b4/0x4a9dc4/
 * 0x4a101c/0x4a1020. Where the original faults (an integer division by
 * zero, a NULL instance, a Gouraud polygon of more than 32 vertices that
 * overwrites the original's locals) the call is refused with the faulting
 * address.
 *
 * The original's empty calls are not repeated: the vector constructor
 * 0x4953c4 (an empty function) before each local vector and 0x476224 (an
 * empty function) after each part draw; the memset call of 0x4760e4 and
 * 0x4759d4 is done as direct byte stores, the gc accessor 0x490d34 as a
 * direct read of 0x6c5f70 and the leaves 0x490d3a..0x490d58 as direct
 * stores (they are also rows of their own). */
#include <stdint.h>
#include <string.h>
#include "es2/sim/shape_draw.h"
#include "es2/sim/memory.h"
#include "es2/sim/services.h"

/* ---- guest VAs: the render context (0x48cf20 addresses it as esi = 0x6c60d4) */
#define VA_VERTEX_COUNT 0x006c68e4u    /* word: the polygon's vertex count */
#define VA_VERTEX_FIRST 0x006c68e6u    /* word: its first index in the index list */
#define VA_POINTS 0x006c68e8u          /* the shape's points (three words each) */
#define VA_POINT_COUNT 0x006c68f0u     /* word */
#define VA_INDEX_LIST 0x006c68f2u      /* the index words (default 0x6c6068) */
#define VA_COLOURS 0x006c68f6u         /* four colour dwords per polygon */
#define VA_STATES 0x006c68fau          /* bytes: 0 untransformed, 1 at or behind the near plane, 2 projected */
#define VA_VIEW_POINTS 0x006c6ce2u     /* the points in view space, three dwords each */
#define VA_CACHE 0x006c9bc2u           /* the projected points, two dwords each */
#define VA_OUT 0x006cbb02u             /* the output polygon, two dwords per vertex */
#define VA_OUT_COUNT 0x006cbc02u       /* word */
#define VA_DEFAULT_INDEX 0x006c6068u
/* the fill state */
#define VA_GC_PEN 0x006c604cu          /* what the group draw puts into the context's +0x228 */
#define VA_COLOUR 0x006c6050u
#define VA_MODE 0x006c6054u            /* 0 flat, 1 Gouraud, 4 second pass, 5 no second pass */
#define VA_COLOUR2 0x006c6058u
#define VA_INTENSITY 0x006c6060u       /* the Gouraud polygon's intensity words (a stack address) */
#define VA_SHAPE_RECORD 0x006c6064u
/* the view state (view.c) */
#define VA_GC 0x006c5f70u
#define VA_VIEW_MATRIX 0x006c5fb4u
#define VA_VIEW_POINTER 0x006c5ff4u
#define VA_OBJECT_POINTER 0x006c5ff8u
#define VA_EYE 0x006c601cu
#define VA_FOCAL_SHIFT 0x006c6028u
#define VA_NEAR 0x006c602cu
#define VA_DIVISOR 0x006c6030u
#define VA_CENTRE 0x006c6034u
#define VA_LIGHT_COUNT 0x006c60a8u
#define VA_LIGHT_LIST 0x006c60acu
#define VA_LIGHTS_ON 0x006cbc04u
/* the part transform state */
#define VA_TRANSFORMS 0x006b7b68u      /* the shape's transform table (0x20 bytes each) */
#define VA_CELLS 0x006b7b6cu           /* the instance's cell words */
#define VA_SAVED_VIEWS 0x006b7b70u     /* 0x32 saved views of 0x110 bytes */
#define VA_SAVED_OBJECT 0x006b7b90u    /* saved view 0's object matrix pointer */
#define VA_SAVED_FLAGS 0x006bb2b0u     /* 0x32 bytes */
#define VA_SAVED_CURRENT 0x006bb2e4u   /* 1 + the transform whose view is loaded, 0: the shape's own */
#define VA_BSP_EYE 0x006bb2e8u
#define VA_DETAIL_SCALE 0x006bb2f4u
#define VA_DETAIL_WIDTH 0x004a0fe8u
#define VA_DETAIL_TEST 0x004a62e0u     /* byte */
#define VA_BSP_SIDE 0x0049f248u
#define VA_PART_SCALE 0x004a101cu
#define VA_PART_OFFSET 0x004a1020u
/* the actor detail levels */
#define VA_ACTOR_SCALE 0x00497368u
#define VA_ACTOR_LIMIT 0x0049736cu
#define VA_LEVEL_OFFSET 0x004a95b4u
/* callees (dispatch) */
#define VA_MATRIX_COMPOSE 0x0047fa20u  /* hooks */
#define VA_EULER_MATRIX 0x0047ebb8u
#define VA_ANGLE_RATIO 0x0047d32cu
#define VA_HEADING_COS 0x0047dff0u
#define VA_MULTIPLY_Q10 0x0047e0b0u
#define VA_DIST3D_DELTA 0x0047de72u
#define VA_MATRIX_POINT 0x004801d4u
#define VA_OBJECT_SELECT 0x0048c444u   /* view.c */
#define VA_VIEW_TRANSFORM 0x0048c57cu
#define VA_PROJECT_POINT 0x0048c5fcu
#define VA_TO_SCREEN 0x0048c6d0u
#define VA_VIEW_LOAD 0x0048d7ecu
#define VA_MATRIX_POINT_48DB93 0x0048db93u
#define VA_VECTOR2_ADD 0x0048d9cfu
#define VA_DEVICE_SURFACE 0x0048d99eu
#define VA_LIGHT_VIEW 0x0048dbd6u
#define VA_VECTOR_FROM_WORDS 0x0048dadcu
#define VA_POLYGON_LIGHTING 0x0048bfe8u
#define VA_ROTATE_THUNK 0x004954e4u
#define VA_HAZE_TABLE 0x00468158u      /* poly.c's row */
#define VA_SHADE 0x00430e34u           /* poly.c's row */
#define VA_FILL_FLAT 0x00483eb8u       /* the fill layer (fill.c's rows) */
#define VA_FILL_BACK 0x00484222u
#define VA_FILL_PLAIN 0x004842bbu

static es2_sim_shape_draw *bound_draw, own_draw;
void es2_sim_shape_draw_bind(es2_sim_shape_draw *draw) { bound_draw = draw; }

/* ---- guest view and calls */
typedef struct ctx { es2_sim_shape_draw *v; es2_sim_loop *l; const es2_guest_memory *m; } ctx;
static uint32_t r32(const ctx *c, uint32_t a) { return c->m->read32(c->m->context, a); }
static uint16_t r16(const ctx *c, uint32_t a) { return c->m->read16(c->m->context, a); }
static uint8_t r8(const ctx *c, uint32_t a) { return es2_sim_read8(c->m, a); }
static void w32(const ctx *c, uint32_t a, uint32_t v) { c->m->write32(c->m->context, a, v); }
static void w16(const ctx *c, uint32_t a, uint16_t v) { c->m->write16(c->m->context, a, v); }
static void w8(const ctx *c, uint32_t a, uint8_t v) { es2_sim_write8(c->m, a, v); }
static int32_t s32(const ctx *c, uint32_t a) { return (int32_t)r32(c, a); }
static int32_t sw(const ctx *c, uint32_t a) { return (int32_t)(int16_t)r16(c, a); }
static int refuse(ctx *c, uint32_t va, const char *name, const char *reason) {
    c->v->refused_va = va;
    return es2_sim_loop_refuse(c->l, va, name, reason);
}
/* The original reads, writes or calls through NULL at va (es2_sim_loop_null_refuse). */
static int null_refuse(ctx *c, uint32_t va, const char *name, const char *reason) {
    c->v->refused_va = va;
    return es2_sim_loop_null_refuse(c->l, va, name, reason);
}
/* D: the original's IDIV of EDX:EAX = dividend by the divisor faults at va
 * (es2_sim_loop_divide_refuse). */
static int divide_refuse(ctx *c, uint32_t va, uint64_t dividend, uint32_t divisor, const char *name, const char *reason) {
    c->v->refused_va = va;
    return es2_sim_loop_divide_refuse(c->l, va, dividend, divisor, 1, name, reason);
}
static int call(ctx *c, uint32_t va, const uint32_t *slots, unsigned count, uint32_t *eax) {
    es2_sim_loop *l = c->l;
    uint32_t bytes = 4u * (count ? count : 1u), top = l->stack_hi;
    if (top - l->stack_lo < bytes) return refuse(c, va, NULL, "the own code's call area is exhausted");
    l->stack_hi -= bytes;
    for (unsigned i = 0; i < count; ++i) w32(c, l->stack_hi + 4u * i, slots[i]);
    uint32_t zero[3] = {0, 0, 0};
    es2_sim_result r;
    memset(&r, 0, sizeof(r));
    int refused = es2_sim_call(l->dispatch, va, c->m->host(c->m->context, l->stack_hi), count, zero, &r);
    l->stack_hi = top;
    if (refused) {
        c->v->refused_va = va;
        if (l->outcome == ES2_SIM_LOOP_EXITED) return 1;   /* the process ended inside */
        l->outcome = ES2_SIM_LOOP_REFUSED;
        l->refused_va = va;
        return 1;
    }
    if (eax) *eax = r.eax;
    return 0;
}
#define CALL(c, va, eax, ...) call((c), (va), (const uint32_t[]){__VA_ARGS__}, \
    (unsigned)(sizeof((const uint32_t[]){__VA_ARGS__}) / sizeof(uint32_t)), (eax))
/* A part's draw slot: `call [vtable + 0x1c]` with the part as the argument. */
static int slot(ctx *c, uint32_t part, uint32_t *eax) {
    return CALL(c, r32(c, r32(c, part) + 0x1cu), eax, part);
}
/* A zero-filled block of the call area for the original's stack locals. */
static uint32_t frame_push(ctx *c, uint32_t bytes) {
    bytes = (bytes + 3u) & ~3u;
    if (c->l->stack_hi - c->l->stack_lo < bytes + 0x400u) return 0;
    c->l->stack_hi -= bytes;
    for (uint32_t i = 0; i < bytes; i += 4u) w32(c, c->l->stack_hi + i, 0);
    return c->l->stack_hi;
}
static void frame_pop(ctx *c, uint32_t top) { c->l->stack_hi = top; }
#define FRAME(c, f, bytes, va, name) \
    uint32_t f##_top = (c)->l->stack_hi, f = frame_push((c), (bytes)); \
    if (!f) return refuse((c), (va), (name), "the own code's call area is exhausted")
/* The original's CDQ; IDIV r/m32: the quotient and remainder, or the #DE fault. */
static int sdiv(ctx *c, uint32_t va, const char *name, int32_t a, int32_t b, int32_t *q, int32_t *rem) {
    if (b == 0 || (a == INT32_MIN && b == -1))
        return divide_refuse(c, va, (uint64_t)(int64_t)a, (uint32_t)b, name, "integer division fault (the original raises #DE)");
    *q = a / b;
    if (rem) *rem = a % b;
    return 0;
}
static int32_t mul32(int32_t a, int32_t b) { return (int32_t)((uint32_t)a * (uint32_t)b); }
static int32_t shl32(int32_t a, uint32_t n) { return (int32_t)((uint32_t)a << (n & 31u)); }
/* 0x47dd1a(a, b): the dot product of two dword vectors (the sum wraps). */
static int32_t ddot(const ctx *c, uint32_t a, uint32_t b) {
    uint32_t sum = (uint32_t)mul32(s32(c, a), s32(c, b)) + (uint32_t)mul32(s32(c, a + 4u), s32(c, b + 4u)) +
                   (uint32_t)mul32(s32(c, a + 8u), s32(c, b + 8u));
    return (int32_t)sum;
}

static int serving(es2_sim_service_call *sc, ctx *c) {
    c->v = bound_draw ? bound_draw : &own_draw;
    if (!c->v->loop) c->v->loop = es2_sim_loop_bound();
    if (!c->v->loop) { sc->refused = 1; sc->reason = "no main loop bound (RD-04)"; return 1; }
    c->l = c->v->loop;
    c->m = c->l->memory;
    ++c->v->calls;
    return 0;
}
static void failed(es2_sim_service_call *sc) { sc->refused = 1; sc->reason = "a call inside the service was refused"; }
#define SERVE(sc, c) ctx c; if (serving((sc), &c)) return
#define A(n) (sc->slots[n])

/* ======================================================================
 * Part transforms and saved views (listing 0x4760e4..0x476220,
 * 0x48d718..0x48d7e9). */

/* 0x48d718(P): saves the view state into the 0x110-byte record P: the
 * view matrix (five dwords, then +0x1c, +0x14, +0x18), the object and
 * view matrix pointers, the eye (+0x30, +0x28, +0x2c) and, when lights are
 * on, one 0x16-byte light record per active light, each copied from the
 * light view of the FIRST light (the source pointer is never advanced).
 * EAX: P without lights, P+0x34 with lights on and none active, else the
 * last record + 4. */
static int save_view(ctx *c, uint32_t p, uint32_t *eax) {
    for (uint32_t i = 0; i < 0x14u; i += 4u) w32(c, p + i, r32(c, VA_VIEW_MATRIX + i));
    w32(c, p + 0x1cu, r32(c, 0x006c5fd0u));
    w32(c, p + 0x14u, r32(c, 0x006c5fc8u));
    w32(c, p + 0x18u, r32(c, 0x006c5fccu));
    w32(c, p + 0x20u, r32(c, VA_OBJECT_POINTER));
    w32(c, p + 0x24u, r32(c, VA_VIEW_POINTER));
    w32(c, p + 0x30u, r32(c, VA_EYE + 8u));
    w32(c, p + 0x28u, r32(c, VA_EYE));
    w32(c, p + 0x2cu, r32(c, VA_EYE + 4u));
    uint32_t result = p;
    if (r8(c, VA_LIGHTS_ON)) {
        uint32_t dst = p + 0x34u;
        result = dst;
        for (int32_t k = s32(c, VA_LIGHT_COUNT); k > 0; --k) {
            uint32_t e = 0;
            if (CALL(c, VA_LIGHT_VIEW, &e, r32(c, VA_LIGHT_LIST))) return 1;
            w32(c, dst + 0x10u, r32(c, e + 0x10u));
            w16(c, dst + 0x14u, r16(c, e + 0x14u));
            w32(c, dst + 0xcu, r32(c, e + 0xcu));
            w32(c, dst + 4u, r32(c, e + 4u));
            w32(c, dst + 8u, r32(c, e + 8u));
            result = dst + 4u;
            dst += 0x16u;
        }
    }
    *eax = result;
    return 0;
}

/* 0x4760e4(shape, T): the transform table; with one, the saved flags are
 * cleared, view 0 is saved and marked, and no transform is current. EAX T
 * when 0, else 0. */
static int transforms_begin(ctx *c, uint32_t t, uint32_t *eax) {
    w32(c, VA_TRANSFORMS, t);
    if (t) {
        for (uint32_t i = 0; i < 0x32u; ++i) w8(c, VA_SAVED_FLAGS + i, 0);
        uint32_t ignored;
        if (save_view(c, VA_SAVED_VIEWS, &ignored)) return 1;
        w8(c, VA_SAVED_FLAGS, 1);
        w32(c, VA_SAVED_CURRENT, 0);
    }
    *eax = 0;
    return 0;
}

/* 0x47613c(part, frame, i): selects transform i (negative: the shape's own
 * view) for a part; frame+0x20 receives what the caller's 0x476224 would
 * restore (unused by it). A transform's view is computed once per shape
 * draw (matrix_compose of its 0x20-byte matrix with saved view 0's object
 * matrix into the frame, 0x48c444, then saved) and loaded from its saved
 * record afterwards. */
static int part_transform(ctx *c, uint32_t part, uint32_t frame, int32_t i, uint32_t *eax) {
    (void)part;
    if (i < 0) {
        uint32_t cur = r32(c, VA_SAVED_CURRENT);
        if (cur) {
            if (CALL(c, VA_VIEW_LOAD, NULL, VA_SAVED_VIEWS)) return 1;
            cur = r32(c, VA_SAVED_CURRENT);
            w32(c, frame + 0x20u, cur);
            w32(c, VA_SAVED_CURRENT, 0);
            *eax = cur;
        } else {
            w32(c, frame + 0x20u, 0xffffffffu);
            *eax = (uint32_t)i;
        }
        return 0;
    }
    uint32_t next = (uint32_t)i + 1u, cur = r32(c, VA_SAVED_CURRENT);
    if (next == cur) {
        w32(c, frame + 0x20u, 0xffffffffu);
        *eax = (uint32_t)i;
        return 0;
    }
    w32(c, frame + 0x20u, cur);
    w32(c, VA_SAVED_CURRENT, next);
    cur = r32(c, VA_SAVED_CURRENT);
    if (!r8(c, VA_SAVED_FLAGS + cur)) {
        w8(c, VA_SAVED_FLAGS + r32(c, VA_SAVED_CURRENT), 1);
        uint32_t object = r32(c, VA_SAVED_OBJECT);
        if (CALL(c, VA_MATRIX_COMPOSE, NULL, ((uint32_t)i << 5) + r32(c, VA_TRANSFORMS), object, frame)) return 1;
        if (CALL(c, VA_OBJECT_SELECT, NULL, frame)) return 1;
        return save_view(c, r32(c, VA_SAVED_CURRENT) * 0x110u + VA_SAVED_VIEWS, eax);
    }
    return CALL(c, VA_VIEW_LOAD, eax, r32(c, VA_SAVED_CURRENT) * 0x110u + VA_SAVED_VIEWS);
}
/* 0x476120(part, frame): 0x47613c with the part's transform word +4. */
static int part_begin(ctx *c, uint32_t part, uint32_t frame, uint32_t *eax) {
    return part_transform(c, part, frame, sw(c, part + 4u), eax);
}

/* ======================================================================
 * Back-face tests, projection, clips and the fill dispatch (listing
 * 0x48c72c..0x48ca6c, 0x48cf20..0x48d0f5, 0x48d38c..0x48d716,
 * 0x4912a0..0x4913d8). */

/* 0x48c72c(a, b) / 0x48c844(a, b): the polygon faces the eye. a is the
 * normal (three words); b the point, three words (0x48c72c) or three
 * dwords (0x48c844). With a perspective shift the result is the dot
 * product of the normal with eye - b; without, 1 when the normal rotated
 * by the view matrix (a 0x14-byte copy: nine words and two bytes) points
 * away (its y negative), else 0. */
static int facing(ctx *c, uint32_t a, uint32_t b, int dwords, uint32_t *eax) {
    uint32_t va = dwords ? 0x0048c844u : 0x0048c72cu;
    FRAME(c, f, 0x38u, va, "back-face test");
    uint32_t v = f + 0x2cu, out = f + 0x20u, copy = f + 0xcu, e = f;   /* ebp-0xc, -0x18, -0x2c, -0x38 */
    int bad = CALL(c, VA_VECTOR_FROM_WORDS, NULL, v, a);
    if (!bad && !r32(c, VA_FOCAL_SHIFT)) {
        for (uint32_t i = 0; i < 0x12u; i += 2u) w16(c, copy + i, r16(c, VA_VIEW_MATRIX + i));
        w8(c, copy + 0x12u, r8(c, VA_VIEW_MATRIX + 0x12u));
        w8(c, copy + 0x13u, r8(c, VA_VIEW_MATRIX + 0x13u));
        uint32_t r = 0;
        bad = CALL(c, VA_ROTATE_THUNK, &r, v, copy, out);
        if (!bad) *eax = (uint32_t)(s32(c, out + 4u) < 0);
    } else if (!bad) {
        for (uint32_t i = 0; i < 3u; ++i) {
            int32_t bi = dwords ? s32(c, b + 4u * i) : sw(c, b + 2u * i);
            w32(c, e + 4u * i, (uint32_t)(s32(c, VA_EYE + 4u * i) - bi));
        }
        *eax = (uint32_t)ddot(c, v, e);
    }
    frame_pop(c, f_top);
    return bad;
}

/* 0x4912a0: the perspective projection of the polygon's vertices (shift
 * nonzero). Untransformed points go through matrix_point into the view
 * points; in front of the near plane they are projected
 * (x << shift) / depth + cx, cy - (z << shift) / depth into the output and
 * the cache and marked 2, else marked 1; projected points are copied from
 * the cache; a point marked 1 (or anything else) leaves its output entry.
 * EAX 1 when any point was not projected, else 0; 0 with no vertices or
 * no shift. */
static int project_shift(ctx *c, uint32_t *eax) {
    int32_t count = sw(c, VA_VERTEX_COUNT);
    uint32_t shift = r32(c, VA_FOCAL_SHIFT);
    if (!count || !shift) { *eax = 0; return 0; }
    uint32_t list = (uint32_t)r16(c, VA_VERTEX_FIRST) * 2u + r32(c, VA_INDEX_LIST);
    uint32_t result = 0;
    w16(c, VA_OUT_COUNT, (uint16_t)count);
    uint32_t n = 0;
    do {
        uint32_t idx = r16(c, list), state = r8(c, VA_STATES + idx);
        if (state == 0) {
            uint32_t view = VA_VIEW_POINTS + 12u * idx;
            if (CALL(c, VA_MATRIX_POINT, NULL, r32(c, VA_POINTS) + 6u * idx, VA_VIEW_MATRIX, view)) return 1;
            int32_t depth = s32(c, view + 4u);
            if (depth > s32(c, VA_NEAR)) {
                uint32_t cl = r32(c, VA_FOCAL_SHIFT);
                int32_t q;
                if (sdiv(c, 0x00491337u, "perspective projection", shl32(s32(c, view), cl), depth, &q, NULL)) return 1;
                uint32_t x = (uint32_t)q + r32(c, VA_CENTRE);
                w32(c, VA_OUT + 8u * n, x);
                w32(c, VA_CACHE + 8u * idx, x);
                if (sdiv(c, 0x00491356u, "perspective projection", shl32(s32(c, view + 8u), cl), depth, &q, NULL)) return 1;
                uint32_t y = (uint32_t)-q + r32(c, VA_CENTRE + 4u);
                w32(c, VA_OUT + 8u * n + 4u, y);
                w32(c, VA_CACHE + 8u * idx + 4u, y);
                w8(c, VA_STATES + idx, 2);
            } else {
                w8(c, VA_STATES + idx, 1);
                result = 1;
            }
        } else if (state == 2) {
            uint32_t x = r32(c, VA_CACHE + 8u * idx), y = r32(c, VA_CACHE + 8u * idx + 4u);
            w32(c, VA_OUT + 8u * n, x);
            w32(c, VA_OUT + 8u * n + 4u, y);
        } else {
            result = 1;
        }
        list += 2u;
        ++n;
    } while ((uint16_t)n != r16(c, VA_VERTEX_COUNT));
    *eax = result;
    return 0;
}

/* 0x48c954: the projection of the polygon's vertices. With a shift it is
 * 0x4912a0; without, untransformed points go through 0x48db93 into the
 * view points, are marked 2 and projected with the divisor:
 * (x << 8) / div + cx and (-(z << 8)) / div + cy into the cache and the
 * output; projected points are copied; others leave their entry. The
 * output count is the vertex count; EAX 0. */
static int project(ctx *c, uint32_t *eax) {
    if (r32(c, VA_FOCAL_SHIFT)) return project_shift(c, eax);
    uint32_t list = (uint32_t)r16(c, VA_VERTEX_FIRST) * 2u + r32(c, VA_INDEX_LIST);
    for (uint32_t i = 0; i < r16(c, VA_VERTEX_COUNT); ++i, list += 2u) {
        uint32_t idx = r16(c, list), out = VA_OUT + 8u * i;
        uint8_t state = r8(c, VA_STATES + idx);
        if (state == 0) {
            uint32_t view = VA_VIEW_POINTS + 12u * idx;
            if (CALL(c, VA_MATRIX_POINT_48DB93, NULL, r32(c, VA_POINTS) + 6u * idx, VA_VIEW_MATRIX, view)) return 1;
            idx = r16(c, list);
            w8(c, VA_STATES + idx, 2);
            int32_t q;
            if (sdiv(c, 0x0048c9ccu, "projection", shl32(s32(c, view), 8), s32(c, VA_DIVISOR), &q, NULL)) return 1;
            uint32_t x = (uint32_t)q + r32(c, VA_CENTRE);
            w32(c, VA_CACHE + 8u * idx, x);
            w32(c, out, x);
            idx = r16(c, list);
            if (sdiv(c, 0x0048c9edu, "projection", (int32_t)(0u - (uint32_t)shl32(s32(c, view + 8u), 8)), s32(c, VA_DIVISOR), &q, NULL))
                return 1;
            uint32_t y = (uint32_t)q + r32(c, VA_CENTRE + 4u);
            w32(c, VA_CACHE + 8u * idx + 4u, y);
            w32(c, out + 4u, y);
        } else if (state == 2) {
            w32(c, out, r32(c, VA_CACHE + 8u * idx));
            w32(c, out + 4u, r32(c, VA_CACHE + 8u * idx + 4u));
        }
    }
    w16(c, VA_OUT_COUNT, r16(c, VA_VERTEX_COUNT));
    *eax = 0;
    return 0;
}

/* The near-plane crossing of the edge prev -> cur: k = near - prev.depth,
 * d = cur.depth - prev.depth; x = low32((cur.x - prev.x) * k) / d + prev.x,
 * y = -(low32((cur.z - prev.z) * k) / d + prev.z) (the products' high
 * halves are dropped by a CDQ; there is no << 8 or divisor scaling, a
 * quirk of the original), then the centre is added by 0x48d9cf. */
static int crossing(ctx *c, uint32_t prev, uint32_t cur, uint32_t n, uint32_t va_x, uint32_t va_y, int32_t *k, int32_t *d) {
    uint32_t vp = VA_VIEW_POINTS + 12u * prev, vc = VA_VIEW_POINTS + 12u * cur, out = VA_OUT + 8u * n;
    *k = s32(c, VA_NEAR) - s32(c, vp + 4u);
    *d = s32(c, vc + 4u) - s32(c, vp + 4u);
    int32_t q;
    if (sdiv(c, va_x, "near-plane clip", mul32(s32(c, vc) - s32(c, vp), *k), *d, &q, NULL)) return 1;
    w32(c, out, (uint32_t)q + r32(c, vp));
    if (sdiv(c, va_y, "near-plane clip", mul32(s32(c, vc + 8u) - s32(c, vp + 8u), *k), *d, &q, NULL)) return 1;
    w32(c, out + 4u, 0u - ((uint32_t)q + r32(c, vp + 8u)));
    return 0;
}
static void copy_cached(ctx *c, uint32_t idx, uint32_t n) {
    w32(c, VA_OUT + 8u * n, r32(c, VA_CACHE + 8u * idx));
    w32(c, VA_OUT + 8u * n + 4u, r32(c, VA_CACHE + 8u * idx + 4u));
}

/* 0x48cf20: clips the polygon at the near plane into the output polygon:
 * for each edge prev -> cur (prev starting at the last vertex), both
 * projected: cur's cached point; one projected: the crossing, then cur's
 * cached point when cur is the projected one; neither: nothing. EAX the
 * output count. */
static int clip(ctx *c, uint32_t *eax) {
    uint32_t count = r16(c, VA_VERTEX_COUNT), list = (uint32_t)r16(c, VA_VERTEX_FIRST) * 2u + r32(c, VA_INDEX_LIST);
    uint32_t prev = count ? r16(c, list + 2u * count - 2u) : 0u;
    w16(c, VA_OUT_COUNT, 0);
    for (uint32_t i = 0; i < r16(c, VA_VERTEX_COUNT); ++i) {
        uint32_t cur = r16(c, list);
        list += 2u;
        int ps = r8(c, VA_STATES + prev) == 2, cs = r8(c, VA_STATES + cur) == 2;
        if (ps && cs) {
            uint32_t n = r16(c, VA_OUT_COUNT);
            w16(c, VA_OUT_COUNT, (uint16_t)(n + 1u));
            copy_cached(c, cur, n);
        } else if (ps || cs) {
            int32_t k, d;
            uint32_t n = r16(c, VA_OUT_COUNT);
            if (crossing(c, prev, cur, n, 0x0048d011u, 0x0048d049u, &k, &d)) return 1;
            n = r16(c, VA_OUT_COUNT);
            w16(c, VA_OUT_COUNT, (uint16_t)(n + 1u));
            if (CALL(c, VA_VECTOR2_ADD, NULL, VA_OUT + 8u * n, VA_CENTRE)) return 1;
            if (r8(c, VA_STATES + cur) == 2) {
                n = r16(c, VA_OUT_COUNT);
                w16(c, VA_OUT_COUNT, (uint16_t)(n + 1u));
                copy_cached(c, cur, n);
            }
        }
        prev = cur;
    }
    *eax = r16(c, VA_OUT_COUNT);
    return 0;
}

/* 0x48d38c(in, out): 0x48cf20 with the vertices' intensity words: a
 * copied point takes the vertex's intensity, a crossing
 * (u16)(low32((in[i] - prev) * k) / d) + prev (all 16-bit). The source
 * words are read from guest memory at each use: the original's output
 * buffer lies below its input on its stack and runs into it. EAX's low
 * word the output count (its high word is not defined by the original). */
static int clip_intensity(ctx *c, uint32_t in, uint32_t outi, uint32_t *eax) {
    uint32_t count = r16(c, VA_VERTEX_COUNT), list = (uint32_t)r16(c, VA_VERTEX_FIRST) * 2u + r32(c, VA_INDEX_LIST);
    uint32_t prev = count ? r16(c, list + 2u * count - 2u) : 0u;
    uint16_t previ = count ? r16(c, in + 2u * count - 2u) : 0u;
    w16(c, VA_OUT_COUNT, 0);
    uint32_t src = in;
    for (uint32_t i = 0; i < r16(c, VA_VERTEX_COUNT); ++i, src += 2u) {
        uint32_t cur = r16(c, list);
        list += 2u;
        int ps = r8(c, VA_STATES + prev) == 2, cs = r8(c, VA_STATES + cur) == 2;
        if (ps && cs) {
            uint32_t n = r16(c, VA_OUT_COUNT);
            copy_cached(c, cur, n);
            n = r16(c, VA_OUT_COUNT);
            w16(c, outi + 2u * n, r16(c, src));
            w16(c, VA_OUT_COUNT, (uint16_t)(n + 1u));
        } else if (ps || cs) {
            int32_t k, d, q;
            uint32_t n = r16(c, VA_OUT_COUNT);
            if (crossing(c, prev, cur, n, 0x0048d4a5u, 0x0048d4dbu, &k, &d)) return 1;
            if (CALL(c, VA_VECTOR2_ADD, NULL, VA_OUT + 8u * r16(c, VA_OUT_COUNT), VA_CENTRE)) return 1;
            if (sdiv(c, 0x0048d522u, "near-plane clip", mul32((int32_t)r16(c, src) - (int32_t)previ, k), d, &q, NULL)) return 1;
            n = r16(c, VA_OUT_COUNT);
            w16(c, outi + 2u * n, (uint16_t)((uint32_t)q + previ));
            w16(c, VA_OUT_COUNT, (uint16_t)(n + 1u));
            if (r8(c, VA_STATES + cur) == 2) {
                n = r16(c, VA_OUT_COUNT);
                copy_cached(c, cur, n);
                n = r16(c, VA_OUT_COUNT);
                w16(c, outi + 2u * n, r16(c, src));
                w16(c, VA_OUT_COUNT, (uint16_t)(n + 1u));
            }
        }
        previ = r16(c, src);
        prev = cur;
    }
    *eax = r16(c, VA_OUT_COUNT);
    return 0;
}

/* 0x48d9ba / 0x48dbac / 0x48dbc1 (surface, count, points): forward
 * (count, points) to the fill layer 0x4842bb / 0x484222 / 0x483eb8. EAX
 * the fill's. */
static int fill_forward(ctx *c, uint32_t fill, uint32_t count, uint32_t points, uint32_t *eax) {
    return CALL(c, fill, eax, count, points);
}
/* One fill pass: the output polygon through `forward` with 0x48d99e's surface. */
static int fill_pass(ctx *c, uint32_t fill, uint32_t *eax) {
    uint32_t count = r16(c, VA_OUT_COUNT), surface = 0;
    if (call(c, VA_DEVICE_SURFACE, NULL, 0, &surface)) return 1;
    return fill_forward(c, fill, count, VA_OUT, eax);
}
/* 0x48d624(back) and 0x48d5c0: fill the output polygon (nothing when it is
 * empty; 0x48d624 with 0x484222 for a back face, 0x483eb8 for a front
 * face, 0x48d5c0 with 0x4842bb). When the two colours differ (and, for
 * 0x48d624, the mode is not 5) the second colour becomes the first, the
 * mode 4, and the polygon is filled again. EAX: the entry EAX for an
 * empty polygon, the colour when there is no second pass, else the fill's. */
static int fill(ctx *c, uint32_t fill_va, int mode5, uint32_t *eax) {
    if (!r16(c, VA_OUT_COUNT)) return 0;
    if (fill_pass(c, fill_va, eax)) return 1;
    uint32_t colour = r32(c, VA_COLOUR);
    *eax = colour;
    if (colour == r32(c, VA_COLOUR2)) return 0;
    if (mode5 && r32(c, VA_MODE) == 5u) return 0;
    w32(c, VA_COLOUR2, colour);
    w32(c, VA_MODE, 4);
    return fill_pass(c, fill_va, eax);
}
static int fill_polygon(ctx *c, uint32_t back, uint32_t *eax) { return fill(c, back ? VA_FILL_BACK : VA_FILL_FLAT, 1, eax); }
/* 0x48d708(back): 0x48d624 through a thunk that loads its argument into EAX
 * first, so an empty polygon leaves EAX = back. */
static int fill_thunk(ctx *c, uint32_t back, uint32_t *eax) { *eax = back; return fill_polygon(c, back, eax); }

/* ======================================================================
 * Polygon parts (listing 0x474ec0..0x474fa5, 0x475538..0x4758b0).
 * Polygon P: +4 normal index, +6 point index, +8 vertex count, +0xa first
 * vertex, +0xc colour index, +0xe first normal (Gouraud). */

/* The common front: the colours by the side the polygon shows (front:
 * colour dwords 0 and 1, back: 2 and 3, into 0x6c6058 and 0x6c6050). 1 when
 * both colours' top bytes are 0x14 (the polygon is not drawn). */
static int polygon_front(ctx *c, uint32_t p, uint32_t *back, int *skip) {
    uint32_t pts = r32(c, VA_POINTS), cp = ((uint32_t)r16(c, p + 0xcu) << 2) + r32(c, VA_COLOURS), r = 0;
    if (facing(c, 6u * r16(c, p + 4u) + pts, 6u * r16(c, p + 6u) + pts, 0, &r)) return 1;
    if ((int32_t)r > 0) {
        w32(c, VA_COLOUR2, r32(c, cp));
        w32(c, VA_COLOUR, r32(c, cp + 4u));
        *back = 0;
    } else {
        w32(c, VA_COLOUR2, r32(c, cp + 8u));
        w32(c, VA_COLOUR, r32(c, cp + 0xcu));
        *back = 1;
    }
    *skip = (s32(c, VA_COLOUR2) >> 24) == 0x14 && (s32(c, VA_COLOUR) >> 24) == 0x14;
    return 0;
}
/* The common tail of the solid and shaded polygons: the vertex range,
 * the projection (the clip when a point was not projected), mode 0 and
 * the fill. EAX as the original leaves it. */
static int polygon_tail(ctx *c, uint32_t p, uint32_t back, int thunk, uint32_t *eax) {
    uint16_t first = r16(c, p + 0xau);
    w16(c, VA_VERTEX_COUNT, r16(c, p + 8u));
    w16(c, VA_VERTEX_FIRST, first);
    uint32_t r = 0;
    if (project(c, &r)) return 1;
    r -= 1u;
    if (!r && clip(c, &r)) return 1;
    w32(c, VA_MODE, 0);
    *eax = r;
    return thunk ? fill_thunk(c, back, eax) : fill_polygon(c, back, eax);
}
/* 0x474ec0(P): a flat polygon: both colours through the haze table
 * 0x468158(0x80). */
static int polygon_solid(ctx *c, uint32_t p, uint32_t *eax) {
    uint32_t back;
    int skip;
    if (polygon_front(c, p, &back, &skip)) return 1;
    if (skip) { *eax = 0x14; return 0; }
    uint32_t t = 0;
    if (CALL(c, VA_HAZE_TABLE, &t, 0x80u)) return 1;
    w32(c, VA_COLOUR, r8(c, t + r32(c, VA_COLOUR)));
    w32(c, VA_COLOUR2, r8(c, t + r32(c, VA_COLOUR2)));
    return polygon_tail(c, p, back, 0, eax);
}
/* 0x475538(P): a lit polygon: the light level 0x48bfe8(normal, point)
 * (the normal negated as words for a back face), both colours through
 * the shade lookup 0x430e34(colour, level) and the haze table. */
static int polygon_shaded(ctx *c, uint32_t p, uint32_t *eax) {
    uint32_t back;
    int skip;
    if (polygon_front(c, p, &back, &skip)) return 1;
    if (skip) { *eax = 0x14; return 0; }
    uint32_t pts = r32(c, VA_POINTS), level = 0, normal = 6u * r16(c, p + 4u) + pts;
    FRAME(c, f, 0xcu, 0x00475538u, "shaded polygon");
    int bad = 0;
    if (back) {
        for (uint32_t i = 0; i < 6u; i += 2u) w16(c, f + 4u + i, r16(c, normal + i));
        for (uint32_t i = 0; i < 6u; i += 2u) w16(c, f + 4u + i, (uint16_t)(0u - r16(c, f + 4u + i)));
        normal = f + 4u;
    }
    bad = CALL(c, VA_POLYGON_LIGHTING, &level, normal, 6u * r16(c, p + 6u) + r32(c, VA_POINTS));
    level &= 0xffu;
    uint32_t t = 0, shade = 0;
    if (!bad) bad = CALL(c, VA_HAZE_TABLE, &t, 0x80u);
    if (!bad) bad = CALL(c, VA_SHADE, &shade, r32(c, VA_COLOUR), level);
    if (!bad) {
        w32(c, VA_COLOUR, r8(c, t + shade));
        bad = CALL(c, VA_SHADE, &shade, r32(c, VA_COLOUR2), level);
    }
    if (!bad) w32(c, VA_COLOUR2, r8(c, t + shade));
    frame_pop(c, f_top);
    if (bad) return 1;
    return polygon_tail(c, p, back, 0, eax);
}
/* 0x4756d4(P): a Gouraud polygon: one light level per vertex (normal from
 * the index list at +0xe, point at +0xa, the normal negated for a back
 * face) into 32 words on the stack; when a point was not projected the
 * clip 0x48d38c carries them into a second buffer. Mode 1, the
 * intensities' address into 0x6c6060, then the fill. The original's
 * buffer holds 32 words; more vertices overwrite its own locals (the
 * buffer pointer first), so they are refused. */
static int polygon_gouraud(ctx *c, uint32_t p, uint32_t va, uint32_t *eax) {
    uint32_t back;
    int skip;
    if (polygon_front(c, p, &back, &skip)) return 1;
    if (skip) { *eax = r32(c, VA_COLOUR); return 0; }
    uint32_t count = r16(c, p + 8u);
    if (count > 32u) return refuse(c, va, "Gouraud polygon", "more than 32 vertices overwrite the original's locals");
    FRAME(c, f, 0x94u, va, "Gouraud polygon");
    uint32_t buf2 = f, buf = f + 0x40u, neg = f + 0x84u;   /* ebp-0x94, -0x54, -0x10 */
    uint32_t normals = 2u * r16(c, p + 0xeu) + r32(c, VA_INDEX_LIST), points = 2u * r16(c, p + 0xau) + r32(c, VA_INDEX_LIST);
    int bad = 0;
    for (uint32_t i = 0; !bad && i < r16(c, p + 8u); ++i) {
        uint32_t normal = 6u * r16(c, normals + 2u * i) + r32(c, VA_POINTS), level = 0;
        if (back) {
            for (uint32_t j = 0; j < 6u; j += 2u) w16(c, neg + j, r16(c, normal + j));
            for (uint32_t j = 0; j < 6u; j += 2u) w16(c, neg + j, (uint16_t)(0u - r16(c, neg + j)));
            normal = neg;
        }
        bad = CALL(c, VA_POLYGON_LIGHTING, &level, normal, 6u * r16(c, points + 2u * i) + r32(c, VA_POINTS));
        if (!bad) w16(c, buf + 2u * i, (uint16_t)(level & 0xffu));
    }
    if (!bad) {
        uint16_t first = r16(c, p + 0xau);
        w16(c, VA_VERTEX_COUNT, r16(c, p + 8u));
        w16(c, VA_VERTEX_FIRST, first);
        uint32_t r = 0;
        bad = project(c, &r);
        if (!bad && r == 1u) {
            bad = clip_intensity(c, buf, buf2, &r);
            if (!bad) { w32(c, VA_MODE, 1); w32(c, VA_INTENSITY, buf2); }
        } else if (!bad) {
            w32(c, VA_MODE, 1);
            w32(c, VA_INTENSITY, buf);
            r = buf;
        }
        *eax = r;
        /* 0x4756d4 calls 0x48d624, 0x475eec the thunk 0x48d708 */
        if (!bad) bad = va == 0x00475eecu ? fill_thunk(c, back, eax) : fill_polygon(c, back, eax);
    }
    frame_pop(c, f_top);
    return bad;
}

/* ======================================================================
 * Part lists (listing 0x476808..0x47684c, 0x4759d4..0x475abd,
 * 0x4768f0..0x47693c, 0x4769a8..0x476aa9, 0x476b28..0x476c6d,
 * 0x4911c0..0x49129f). A part: +4 transform word, +8 radius word, +0x10
 * child count, +0x12 child pointers, +0x16 the kind's word or table. */

/* 0x476808(p): every child's draw slot in order. EAX the child count. */
static int part_list(ctx *c, uint32_t p, uint32_t *eax) {
    FRAME(c, f, 0x24u, 0x00476808u, "part list");
    uint32_t r;
    int bad = part_begin(c, p, f, &r);
    uint32_t i = 0;
    for (; !bad && i < r16(c, p + 0x10u); ++i) bad = slot(c, r32(c, r32(c, p + 0x12u) + 4u * i), NULL);
    frame_pop(c, f_top);
    if (!bad) *eax = r16(c, p + 0x10u);
    return bad;
}

/* 0x4759d4(g): a polygon group: the context's +0x228/+0x22c/+0x20c are
 * saved and set to 0x6c604c, 0x6c6054 and 0; the group's colours, points
 * and point count become the render context's, the point states are
 * cleared (count bytes), the index list is the group's (or 0x6c6068); the
 * polygons' draw slots run in order; the three fields are restored. EAX
 * the restored +0x20c. */
static int group_body(ctx *c, uint32_t g, uint32_t *eax);
static int part_group(ctx *c, uint32_t g, uint32_t *eax) {
    FRAME(c, f, 0x34u, 0x004759d4u, "polygon group");
    uint32_t r;
    int bad = part_begin(c, g, f, &r);
    if (!bad) bad = group_body(c, g, eax);
    frame_pop(c, f_top);
    return bad;
}
static int group_body(ctx *c, uint32_t g, uint32_t *eax) {
    uint32_t gc = r32(c, VA_GC), s228 = r32(c, gc + 0x228u), s22c = r32(c, gc + 0x22cu), s20c = r32(c, gc + 0x20cu);
    w32(c, gc + 0x228u, VA_GC_PEN);
    w32(c, gc + 0x22cu, VA_MODE);
    w32(c, gc + 0x20cu, 0);
    w32(c, VA_COLOURS, r32(c, g + 0x18u));
    uint16_t points = r16(c, g + 0x22u);
    w32(c, VA_POINTS, r32(c, g + 0x14u));
    w16(c, VA_POINT_COUNT, points);
    for (uint32_t i = 0; i < points; ++i) w8(c, VA_STATES + i, 0);
    uint32_t list = r32(c, g + 0x10u);
    w32(c, VA_INDEX_LIST, list ? list : VA_DEFAULT_INDEX);
    uint32_t polys = r32(c, g + 0x1cu);
    for (uint32_t i = 0; i < r16(c, g + 0x26u); ++i)
        if (slot(c, r32(c, polys + 4u * i), NULL)) return 1;
    w32(c, gc + 0x228u, s228);
    w32(c, gc + 0x22cu, s22c);
    w32(c, gc + 0x20cu, s20c);
    *eax = s20c;
    return 0;
}

/* 0x4768f0(p): a cell animation: child (cell word number (int16)+0x16 of
 * the instance's cell words) mod the child count. EAX the child's. */
static int part_cells(ctx *c, uint32_t p, uint32_t *eax) {
    FRAME(c, f, 0x24u, 0x004768f0u, "cell animation");
    uint32_t r;
    int bad = part_begin(c, p, f, &r);
    if (!bad) {
        int32_t cell = (int32_t)r16(c, (uint32_t)(sw(c, p + 0x16u) * 2) + r32(c, VA_CELLS)), q, rem;
        bad = sdiv(c, 0x0047691cu, "cell animation", cell, (int32_t)r16(c, p + 0x10u), &q, &rem);
        if (!bad) bad = slot(c, r32(c, r32(c, p + 0x12u) + 4u * (uint32_t)rem), eax);
    }
    frame_pop(c, f_top);
    return bad;
}

/* 0x4769c8(p): a detail part: the projected size
 * s = (r << shift) / max(dist3d_delta(eye) - r, 1) (at least 1), scaled by
 * multiply_q10(0x4a101c, s); the child index starts at 0x4a1020 and
 * advances while it is below count - 1 and s exceeds the threshold word
 * (+0x16 table, read from the starting index on); then 0x4a1020 is
 * subtracted again and the index capped at count - 1. EAX the frame's
 * address: 0x476a96 loads it for the empty 0x476224 (the child's EAX is
 * dropped). */
static int detail_body(ctx *c, uint32_t p);
static int part_detail(ctx *c, uint32_t p, uint32_t *eax) {
    FRAME(c, f, 0x30u, 0x004769c8u, "detail part");
    uint32_t r0;
    int bad = part_begin(c, p, f, &r0);
    if (!bad) bad = detail_body(c, p);
    if (!bad) *eax = f;
    frame_pop(c, f_top);
    return bad;
}
static int detail_body(ctx *c, uint32_t p) {
    int32_t r = sw(c, p + 8u);
    uint32_t dist = 0;
    if (CALL(c, VA_DIST3D_DELTA, &dist, r32(c, VA_EYE), r32(c, VA_EYE + 4u), r32(c, VA_EYE + 8u))) return 1;
    int32_t d = (int32_t)(dist - (uint32_t)r), s;
    if (d <= 0) d = 1;
    if (sdiv(c, 0x00476a25u, "detail part", shl32(r, r32(c, VA_FOCAL_SHIFT)), d, &s, NULL)) return 1;
    if (!s) s = 1;
    uint32_t scaled = 0;
    if (CALL(c, VA_MULTIPLY_Q10, &scaled, r32(c, VA_PART_SCALE), (uint32_t)s)) return 1;
    int32_t start = s32(c, VA_PART_OFFSET), idx = start;
    uint32_t thr = r32(c, p + 0x16u) + 2u * (uint32_t)start;
    while (idx < (int32_t)r16(c, p + 0x10u) - 1) {
        if ((int32_t)scaled <= sw(c, thr)) break;
        ++idx;
        thr += 2u;
    }
    idx -= s32(c, VA_PART_OFFSET);
    int32_t last = (int32_t)r16(c, p + 0x10u) - 1;
    if (idx > last) idx = last;
    return slot(c, r32(c, r32(c, p + 0x12u) + 4u * (uint32_t)idx), NULL);
}

/* 0x4911c0(p, M, n): the plane distance through a transform: for each row
 * k the 64-bit sum of (p - M's translation) times the row's words
 * (+0/+2/+8, +4/+6/+0xa, +0xc/+0xe/+0x10) plus 0x2000, its low 32 bits
 * after >> 14, times the plane normal's word k; the three products summed
 * in 32 bits. */
static int32_t plane_distance(const ctx *c, uint32_t p, uint32_t m, uint32_t n, uint32_t *ecx, uint32_t *edx) {
    static const uint32_t rows[3][3] = {{0u, 2u, 8u}, {4u, 6u, 0xau}, {0xcu, 0xeu, 0x10u}};
    uint32_t sum = 0, part = 0;
    for (unsigned k = 0; k < 3u; ++k) {
        uint64_t acc = 0;
        for (unsigned j = 0; j < 3u; ++j) {
            int32_t delta = (int32_t)(r32(c, p + 4u * j) - r32(c, m + 0x14u + 4u * j));
            acc += (uint64_t)((int64_t)delta * (int64_t)sw(c, m + rows[k][j]));
        }
        acc += 0x2000u;
        int32_t row = (int32_t)(uint32_t)(acc >> 14);
        int64_t prod = (int64_t)row * (int64_t)sw(c, n + 2u * k);
        sum += (uint32_t)prod;
        if (k == 1u) part = sum;
        if (k == 2u) *edx = (uint32_t)((uint64_t)prod >> 32);
    }
    *ecx = part;
    return (int32_t)sum;
}

/* 0x476b28(bsp, node): node records of 14 bytes at +0x18 (normal words,
 * distance dword +6, links +0xa/+0xc), transform words at +0x1c (0xffff:
 * none). The eye side d = n . eye - dist (through the transform with
 * 0x4911c0); (d < 0) unequal to 0x49f248 visits +0xa first, else +0xc
 * first; a link with bit 0x4000 is a child's draw slot (index & 0x3fff),
 * a nonnegative one without is a subtree, a negative one nothing. EAX
 * the last visited link's (the child's or the subtree's), or the first
 * link itself (sign-extended) when neither is visited. */
static int bsp_node(ctx *c, uint32_t bsp, uint32_t node, unsigned depth, uint32_t *eax) {
    if (depth > 4096u) return refuse(c, 0x00476b28u, "BSP part", "the node links form a cycle (the original overflows its stack)");
    uint32_t rec = node * 14u + r32(c, bsp + 0x18u);
    uint16_t tw = r16(c, r32(c, bsp + 0x1cu) + 2u * node);
    int32_t d;
    if (tw == 0xffffu) {
        FRAME(c, f, 0xcu, 0x00476b28u, "BSP part");
        for (uint32_t i = 0; i < 3u; ++i) w32(c, f + 4u * i, (uint32_t)sw(c, rec + 2u * i));
        d = ddot(c, f, VA_BSP_EYE);
        frame_pop(c, f_top);
    } else {
        uint32_t ecx, edx;
        d = plane_distance(c, VA_BSP_EYE, ((uint32_t)(int32_t)(int16_t)tw << 5) + r32(c, VA_TRANSFORMS), rec, &ecx, &edx);
    }
    d = (int32_t)((uint32_t)d - r32(c, rec + 6u));
    int32_t first, second;
    if ((uint32_t)(d < 0) != r32(c, VA_BSP_SIDE)) { first = sw(c, rec + 0xau); second = sw(c, rec + 0xcu); }
    else { first = sw(c, rec + 0xcu); second = sw(c, rec + 0xau); }
    int32_t links[2] = {first, second};
    uint32_t result = (uint32_t)first;
    for (unsigned j = 0; j < 2u; ++j) {
        int32_t link = links[j];
        if (link < 0) continue;
        if (link & 0x4000) {
            if (slot(c, r32(c, r32(c, bsp + 0x12u) + 4u * (uint32_t)(link & 0x3fff)), &result)) return 1;
        } else if (bsp_node(c, bsp, (uint32_t)link, depth + 1u, &result)) return 1;
    }
    *eax = result;
    return 0;
}
/* 0x476c18(p): a BSP part: the eye into 0x6bb2e8 (z, x, y in that
 * order), then the tree from node 0. EAX the tree's (0x476224 is empty). */
static int part_bsp(ctx *c, uint32_t p, uint32_t *eax) {
    FRAME(c, f, 0x24u, 0x00476c18u, "BSP part");
    uint32_t r;
    int bad = part_begin(c, p, f, &r);
    if (!bad) {
        w32(c, VA_BSP_EYE + 8u, r32(c, VA_EYE + 8u));
        w32(c, VA_BSP_EYE, r32(c, VA_EYE));
        w32(c, VA_BSP_EYE + 4u, r32(c, VA_EYE + 4u));
        bad = bsp_node(c, p, 0, 0, eax);
    }
    frame_pop(c, f_top);
    return bad;
}

/* ======================================================================
 * Shapes and instances (listing 0x476248..0x4763f1, 0x490ab8..0x490b62,
 * 0x490c1c..0x490cdb, 0x490d3a..0x490d5e, 0x478d60..0x478de1). A shape:
 * +8 radius word, +0xa centre words, +0x18 transform table, +0x1c cell
 * words, +0x26 shape record. An instance: +4 shape, +8 cell words, +0x16
 * object matrix. */

/* 0x476248(shape): the detail test: 0 when the bounding sphere is behind
 * the near plane or its screen square (radius projected with the cached
 * scale, plus 3) misses the viewport, 2 when it lies strictly inside a
 * clipping viewport (+0x208 == 1), else 1. The scale 0x6bb2f4 =
 * 0x1000000 / heading_cos(angle_ratio(1 << shift, width)) is recomputed
 * when the viewport width changes. */
static int detail_test(ctx *c, uint32_t shape, uint32_t *eax) {
    FRAME(c, f, 0x40u, 0x00476248u, "detail test");
    uint32_t v = f + 0x10u, t = f + 0x1cu, pt = f + 0x38u;   /* ebp-0x30, -0x24, -0x8 */
    w32(c, v + 8u, (uint32_t)sw(c, shape + 0xeu));
    w32(c, v, (uint32_t)sw(c, shape + 0xau));
    w32(c, v + 4u, (uint32_t)sw(c, shape + 0xcu));
    int bad = CALL(c, VA_VIEW_TRANSFORM, NULL, v, t);
    uint32_t result = 0;
    while (!bad) {
        if ((int32_t)((uint32_t)sw(c, shape + 8u) + r32(c, t + 4u)) <= s32(c, VA_NEAR)) break;
        uint32_t gc = r32(c, VA_GC);
        int32_t w = (int32_t)(r32(c, gc + 0x210u) - r32(c, gc + 0x220u));
        if (w < 0) w = (int32_t)(r32(c, gc + 0x218u) + r32(c, gc + 0x220u));
        if ((uint32_t)w != r32(c, VA_DETAIL_WIDTH)) {
            w32(c, VA_DETAIL_WIDTH, (uint32_t)w);
            uint32_t a = 0, cosine = 0;
            if ((bad = CALL(c, VA_ANGLE_RATIO, &a, 1u << (r32(c, VA_FOCAL_SHIFT) & 31u), (uint32_t)w))) break;
            if ((bad = CALL(c, VA_HEADING_COS, &cosine, a & 0xffffu))) break;
            int32_t q;
            if ((bad = sdiv(c, 0x004762f7u, "detail test", 0x1000000, (int32_t)(int16_t)cosine, &q, NULL))) break;
            w32(c, VA_DETAIL_SCALE, (uint32_t)q);
        }
        uint32_t r = 0;
        if ((bad = CALL(c, VA_MULTIPLY_Q10, &r, (uint32_t)sw(c, shape + 8u), r32(c, VA_DETAIL_SCALE)))) break;
        if (!r32(c, t + 4u)) w32(c, t + 4u, 1);
        int32_t ty = s32(c, t + 4u), s;
        int32_t num = (int32_t)((uint32_t)shl32((int32_t)r, r32(c, VA_FOCAL_SHIFT)) + (uint32_t)(ty >> 1));
        if ((bad = sdiv(c, 0x0047633au, "detail test", num, ty, &s, NULL))) break;
        if (s < 0) s = (int32_t)(0u - (uint32_t)s);
        if ((bad = CALL(c, VA_PROJECT_POINT, NULL, t, pt))) break;
        if ((bad = CALL(c, VA_TO_SCREEN, NULL, pt, pt))) break;
        int32_t px = s32(c, pt), py = s32(c, pt + 4u);
        int32_t left = (int32_t)((uint32_t)px - (uint32_t)s - 3u), right = (int32_t)((uint32_t)px + (uint32_t)s + 3u);
        int32_t top = (int32_t)((uint32_t)py - (uint32_t)s - 3u), bottom = (int32_t)((uint32_t)py + (uint32_t)s + 3u);
        int32_t v0 = s32(c, gc + 0x210u), v1 = s32(c, gc + 0x214u), v2 = s32(c, gc + 0x218u), v3 = s32(c, gc + 0x21cu);
        if (v0 > right || v1 > bottom || v2 < left || v3 < top) break;
        result = (r32(c, gc + 0x208u) == 1u && v2 > right && v3 > bottom && v0 < left && v1 < top) ? 2u : 1u;
        break;
    }
    frame_pop(c, f_top);
    if (!bad) *eax = result;
    return bad;
}

/* 0x490ab8(shape, cells) / 0x490c1c(inst): the shape draw: the cell words,
 * the transform table (0x4760e4), the detail test when 0x4a62e0 is set (0:
 * nothing drawn, EAX 0; 2 with a clipping context: the clip flag off for
 * the draw and on again), the shape record into 0x6c6064 around the part
 * list 0x476808. EAX the restored shape record. */
static int shape_draw(ctx *c, uint32_t inst, int instance, uint32_t *eax) {
    uint32_t shape = instance ? r32(c, inst + 4u) : inst, r = 0;
    w32(c, VA_CELLS, r32(c, inst + (instance ? 8u : 0x1cu)));
    if (instance) shape = r32(c, inst + 4u);
    if (transforms_begin(c, r32(c, shape + 0x18u), &r)) return 1;
    uint32_t gc = 0;
    int unclip = 0;
    if (r8(c, VA_DETAIL_TEST)) {
        if (instance) shape = r32(c, inst + 4u);
        if (detail_test(c, shape, &r)) return 1;
        if (!r) { *eax = 0; return 0; }
        if (r == 2u) {
            gc = r32(c, VA_GC);
            unclip = r32(c, gc + 0x208u) == 1u;
        }
    }
    if (unclip) w32(c, gc + 0x208u, 0);
    uint32_t saved = r32(c, VA_SHAPE_RECORD);
    if (instance) shape = r32(c, inst + 4u);
    w32(c, VA_SHAPE_RECORD, r32(c, shape + 0x26u));
    if (instance) shape = r32(c, inst + 4u);
    if (part_list(c, shape, &r)) return 1;
    w32(c, VA_SHAPE_RECORD, saved);
    if (unclip) w32(c, gc + 0x208u, 1);
    *eax = saved;
    return 0;
}

/* 0x478d60(inst, M, obj): M = obj x the current object matrix
 * (matrix_compose), selected with 0x48c444. EAX the previous object
 * matrix pointer. */
static int instance_push(ctx *c, uint32_t m, uint32_t obj, uint32_t *eax) {
    uint32_t old = r32(c, VA_OBJECT_POINTER);
    if (CALL(c, VA_MATRIX_COMPOSE, NULL, obj, old, m)) return 1;
    if (CALL(c, VA_OBJECT_SELECT, NULL, m)) return 1;
    *eax = old;
    return 0;
}
/* 0x478d90(inst, M): selects M again unless it is NULL. EAX M or 0x48c444's. */
static int instance_pop(ctx *c, uint32_t m, uint32_t *eax) {
    *eax = m;
    return m ? CALL(c, VA_OBJECT_SELECT, eax, m) : 0;
}
/* 0x478da4(inst): the instance's draw slot: push its matrix, give the
 * shape the instance's transform table, draw, pop. */
static int instance_draw(ctx *c, uint32_t inst, uint32_t *eax) {
    FRAME(c, f, 0x20u, 0x00478da4u, "instance draw");
    uint32_t old = 0, r = 0;
    int bad = instance_push(c, f, r32(c, inst + 0x16u), &old);
    if (!bad) {
        w32(c, r32(c, inst + 4u) + 0x18u, r32(c, inst + 0x16u));
        bad = shape_draw(c, inst, 1, &r) || instance_pop(c, old, eax);
    }
    frame_pop(c, f_top);
    return bad;
}

/* ======================================================================
 * Actors and detail levels (listing 0x401fe4..0x40201a,
 * 0x4033e4..0x403518, 0x46b8ea..0x46b904; the level tables 0x45d490/
 * 0x45d50c that set its scale and limit are HUD rows). An actor:
 * +0xc the pose (euler words), +0x12 its matrix, +0x32 the matrix-built
 * word, +0x34 the shape instance, +0x41 the detail table (level shapes
 * +0, threshold dwords +4, count word +0xc). */

/* 0x401fe4(obj): builds the actor's matrix once (euler_matrix) and
 * selects it. EAX 0x48c444's. */
static int actor_matrix(ctx *c, uint32_t obj, uint32_t *eax) {
    if (!r16(c, obj + 0x32u)) {
        if (CALL(c, VA_EULER_MATRIX, NULL, obj + 0xcu, obj + 0x12u)) return 1;
        w16(c, obj + 0x32u, 1);
    }
    return CALL(c, VA_OBJECT_SELECT, eax, obj + 0x12u);
}
/* 0x46b8ea(obj): the matrix, then the instance's draw slot. EAX the slot's. */
static int actor_draw(ctx *c, uint32_t obj, uint32_t *eax) {
    uint32_t r;
    if (actor_matrix(c, obj, &r)) return 1;
    return slot(c, r32(c, obj + 0x34u), eax);
}
/* 0x4033e4(obj): an actor with detail levels: the projected size as in
 * 0x4769c8 with the scale 0x497368; the level starts at
 * min(count - 1, 0x49736c) (as a word) and advances while it is below
 * count - 1 and the size is below the threshold dwords, read from the
 * table's FIRST entry on (a quirk: not from the starting level); its shape
 * goes into the instance for the draw, the level 0 shape afterwards. EAX
 * the level 0 shape, 0 without a table. */
static int actor_levels(ctx *c, uint32_t obj, uint32_t *eax) {
    uint32_t r;
    if (actor_matrix(c, obj, &r)) return 1;
    uint32_t t = r32(c, obj + 0x41u);
    if (t && r16(c, t + 0xcu)) {
        uint32_t inst = r32(c, obj + 0x34u);
        if (!inst) return null_refuse(c, 0x0040341du, "actor detail levels", "no instance (the original reads address 8)");
        int32_t radius = sw(c, r32(c, inst + 4u) + 8u);
        uint32_t dist = 0;
        if (CALL(c, VA_DIST3D_DELTA, &dist, r32(c, VA_EYE), r32(c, VA_EYE + 4u), r32(c, VA_EYE + 8u))) return 1;
        int32_t d = (int32_t)(dist - (uint32_t)radius), s;
        if (d <= 0) d = 1;
        if (sdiv(c, 0x00403463u, "actor detail levels", shl32(radius, r32(c, VA_FOCAL_SHIFT)), d, &s, NULL)) return 1;
        if (!s) s = 1;
        uint32_t scaled = 0;
        if (CALL(c, VA_MULTIPLY_Q10, &scaled, r32(c, VA_ACTOR_SCALE), (uint32_t)s)) return 1;
        t = r32(c, obj + 0x41u);
        int32_t e = sw(c, t + 0xcu) - 1;
        if (e >= s32(c, VA_ACTOR_LIMIT)) e = s32(c, VA_ACTOR_LIMIT);
        e = (int16_t)e;
        uint32_t thr = r32(c, r32(c, obj + 0x41u) + 4u);
        while (e < sw(c, r32(c, obj + 0x41u) + 0xcu) - 1) {
            if ((int32_t)scaled >= s32(c, thr)) break;
            ++e;
            thr += 4u;
        }
        t = r32(c, obj + 0x41u);
        w32(c, r32(c, obj + 0x34u) + 4u, r32(c, r32(c, t) + 4u * (uint32_t)e));
    }
    if (actor_matrix(c, obj, &r)) return 1;
    if (slot(c, r32(c, obj + 0x34u), &r)) return 1;
    t = r32(c, obj + 0x41u);
    *eax = t;
    if (t) {
        uint32_t level0 = r32(c, r32(c, t));
        w32(c, r32(c, obj + 0x34u) + 4u, level0);
        *eax = level0;
    }
    return 0;
}

/* ======================================================================
 * The remaining shape-draw callees (rasterizer.md, "DTS shape draw"; listing
 * 0x474fa8..0x475536, 0x475b2c..0x475eea, 0x4763f4..0x47670f,
 * 0x48ca70..0x48cce1, 0x48d0f8..0x48d38b, 0x40bb4c..0x40bc11). */
#define VA_POINTS_DWORD 0x006c68ecu    /* dword points (three dwords each): 0x48ca70's source */
#define VA_TEX_RECORDS 0x006b7a18u     /* 4 vertices of three dwords (0x474fa8's 3D fill) */
#define VA_TEX_UV 0x006b7a48u          /* 4 UV pairs */
#define VA_TEX_CLIP 0x006b7a68u        /* 0x20 UV pairs: the clipped UV */
#define VA_TEX_GUARDS 0x004a0f4cu      /* three guard bytes of those static arrays */
#define VA_TEX_ON 0x0049f244u          /* textures through the fill layer (else the 3D fills) */
#define VA_TEX_PERSP 0x0049f24cu       /* the perspective 3D fill */
#define VA_LIGHT_BASE 0x004a5b00u
#define VA_LIGHT_LEVELS 0x004a5b04u
#define VA_LIGHT_MODE 0x004aaaacu
#define VA_TRANSFORM_POINT_48D9E4 0x0048d9e4u
#define VA_PROJECT_SIZE 0x0048c5ccu
#define VA_TRANSFORM_POINT 0x0048043cu /* hooks */
#define VA_DISTANCE_DISPLAY 0x0047d694u
#define VA_MULTIPLY_Q8 0x0047e0a0u
#define VA_SINCOS_PAIR 0x0047eb30u
#define VA_ROTATE_2D 0x0047ffe8u
#define VA_ROTATED_BLIT 0x0048185cu    /* rows of other packages */
#define VA_VECTOR_NEW 0x0049667cu
#define VA_VECTOR_CTOR 0x004953c4u
#define VA_EMPTY_CTOR 0x0049545bu
#define VA_FILL_3D 0x0046817cu
#define VA_FILL_PERSP 0x00468760u
#define VA_DIV16 0x0047e068u
#define VA_MUL16 0x0047e08du

/* 0x48ca70: the projection of the polygon's vertices from the dword points
 * 0x6c68ec (through 0x48d9e4 into the view points). Without a shift as
 * 0x48c954: an untransformed point is marked 2 and projected with the
 * divisor, a projected one copied, others leave their entry. With a shift
 * an untransformed point whose depth lies below the near plane (strictly)
 * is marked 1, else 2 and projected (x << shift) / depth + cx,
 * cy - (z << shift) / depth; a point marked 1 makes the result 1, a
 * projected one is copied. The index word is read again before each use.
 * The output count becomes the vertex count only when the result is 0.
 * EAX the result. */
static int project_dwords(ctx *c, uint32_t *eax) {
    uint32_t list = (uint32_t)r16(c, VA_VERTEX_FIRST) * 2u + r32(c, VA_INDEX_LIST), result = 0;
    int shifted = r32(c, VA_FOCAL_SHIFT) != 0;
    for (uint32_t i = 0; i < r16(c, VA_VERTEX_COUNT); ++i, list += 2u) {
        uint32_t idx = r16(c, list), out = VA_OUT + 8u * i;
        if (r8(c, VA_STATES + idx) == 0) {
            uint32_t view = VA_VIEW_POINTS + 12u * idx;
            if (CALL(c, VA_TRANSFORM_POINT_48D9E4, NULL, 12u * r16(c, list) + r32(c, VA_POINTS_DWORD), VA_VIEW_MATRIX, view)) return 1;
            int32_t q;
            if (shifted && s32(c, view + 4u) < s32(c, VA_NEAR)) {
                w8(c, VA_STATES + r16(c, list), 1);
                result = 1;
                continue;
            }
            idx = r16(c, list);
            w8(c, VA_STATES + idx, 2);
            if (!shifted) {
                if (sdiv(c, 0x0048cb08u, "projection", shl32(s32(c, view), 8), s32(c, VA_DIVISOR), &q, NULL)) return 1;
            } else if (sdiv(c, 0x0048cc26u, "perspective projection", shl32(s32(c, view), r32(c, VA_FOCAL_SHIFT)), s32(c, view + 4u), &q, NULL)) {
                return 1;
            }
            uint32_t x = (uint32_t)q + r32(c, VA_CENTRE);
            if (shifted) idx = r16(c, list);   /* 0x48cc29 reads it again, 0x48cb14 uses 0x48caf0's */
            w32(c, VA_CACHE + 8u * idx, x);
            w32(c, out, x);
            if (!shifted) {
                if (sdiv(c, 0x0048cb2eu, "projection", (int32_t)(0u - (uint32_t)shl32(s32(c, view + 8u), 8)), s32(c, VA_DIVISOR), &q, NULL))
                    return 1;
            } else if (sdiv(c, 0x0048cc4au, "perspective projection",
                            (int32_t)(0u - (uint32_t)shl32(s32(c, view + 8u), r32(c, VA_FOCAL_SHIFT))), s32(c, view + 4u), &q, NULL)) {
                return 1;
            }
            uint32_t y = (uint32_t)q + r32(c, VA_CENTRE + 4u);
            idx = r16(c, list);
            w32(c, VA_CACHE + 8u * idx + 4u, y);
            w32(c, out + 4u, y);
        } else {
            idx = r16(c, list);
            uint8_t state = r8(c, VA_STATES + idx);
            if (state == 1u && shifted) result = 1;
            else if (state == 2u) copy_cached(c, idx, i);
        }
    }
    if (!result) w16(c, VA_OUT_COUNT, r16(c, VA_VERTEX_COUNT));
    *eax = result;
    return 0;
}

/* 0x48d0f8(in, out): 0x48cf20 with the vertices' UV pairs (two dwords): a
 * copied point takes the vertex's pair in[i], a crossing
 * low32((in[i] - prev) * k) / d + prev for u and v (prev: the previous
 * vertex's pair, starting at in[count - 1]). The pairs are read from guest
 * memory at each use. EAX the output count in the low word; the high word
 * is the last pair's v's (in's with no vertices). */
static int clip_uv(ctx *c, uint32_t in, uint32_t outuv, uint32_t *eax) {
    uint32_t count = r16(c, VA_VERTEX_COUNT), list = (uint32_t)r16(c, VA_VERTEX_FIRST) * 2u + r32(c, VA_INDEX_LIST);
    uint32_t prev = r16(c, list + 2u * count - 2u);
    uint32_t pu = r32(c, in + 8u * count - 8u), pv = r32(c, in + 8u * count - 4u), high = in;
    w16(c, VA_OUT_COUNT, 0);
    for (uint32_t i = 0; i < r16(c, VA_VERTEX_COUNT); ++i) {
        uint32_t cur = r16(c, list), src = in + 8u * i;
        list += 2u;
        int ps = r8(c, VA_STATES + prev) == 2, cs = r8(c, VA_STATES + cur) == 2;
        if (ps && cs) {
            copy_cached(c, cur, r16(c, VA_OUT_COUNT));
            uint32_t n = r16(c, VA_OUT_COUNT);
            w32(c, outuv + 8u * n, r32(c, src));
            w32(c, outuv + 8u * n + 4u, r32(c, src + 4u));
            w16(c, VA_OUT_COUNT, (uint16_t)(n + 1u));
        } else if (ps || cs) {
            int32_t k, d, q;
            if (crossing(c, prev, cur, r16(c, VA_OUT_COUNT), 0x0048d233u, 0x0048d269u, &k, &d)) return 1;
            if (CALL(c, VA_VECTOR2_ADD, NULL, VA_OUT + 8u * r16(c, VA_OUT_COUNT), VA_CENTRE)) return 1;
            if (sdiv(c, 0x0048d2b1u, "near-plane clip", mul32((int32_t)(r32(c, src) - pu), k), d, &q, NULL)) return 1;
            w32(c, outuv + 8u * r16(c, VA_OUT_COUNT), (uint32_t)q + pu);
            if (sdiv(c, 0x0048d2d2u, "near-plane clip", mul32((int32_t)(r32(c, src + 4u) - pv), k), d, &q, NULL)) return 1;
            uint32_t n = r16(c, VA_OUT_COUNT);
            w32(c, outuv + 8u * n + 4u, (uint32_t)q + pv);
            w16(c, VA_OUT_COUNT, (uint16_t)(n + 1u));
            if (r8(c, VA_STATES + cur) == 2) {
                copy_cached(c, cur, r16(c, VA_OUT_COUNT));
                n = r16(c, VA_OUT_COUNT);
                w32(c, outuv + 8u * n, r32(c, src));
                w32(c, outuv + 8u * n + 4u, r32(c, src + 4u));
                w16(c, VA_OUT_COUNT, (uint16_t)(n + 1u));
            }
        }
        pu = r32(c, src);
        pv = r32(c, src + 4u);
        high = pv;
        prev = cur;
    }
    *eax = (high & 0xffff0000u) | r16(c, VA_OUT_COUNT);
    return 0;
}

/* 0x475cd0(P): a flat polygon without the haze table: the colours by the
 * side, the projection, the clip, mode 0 and the fill through the thunk
 * 0x48d708 (an empty polygon leaves EAX = the side). EAX 0x14 when it is
 * not drawn. */
static int polygon_plain(ctx *c, uint32_t p, uint32_t *eax) {
    uint32_t back;
    int skip;
    if (polygon_front(c, p, &back, &skip)) return 1;
    if (skip) { *eax = 0x14; return 0; }
    return polygon_tail(c, p, back, 1, eax);
}
/* 0x475d8c(P): a lit polygon without the haze table: the light level
 * 0x48bfe8(normal, point) & 0xff (the normal negated as words for a back
 * face) ORed into both colours' bits 16..23, then as 0x475cd0. */
static int polygon_lit(ctx *c, uint32_t p, uint32_t *eax) {
    uint32_t back;
    int skip;
    if (polygon_front(c, p, &back, &skip)) return 1;
    if (skip) { *eax = 0x14; return 0; }
    uint32_t pts = r32(c, VA_POINTS), level = 0, normal = 6u * r16(c, p + 4u) + pts;
    FRAME(c, f, 8u, 0x00475d8cu, "lit polygon");   /* ebp-8 */
    if (back) {
        for (uint32_t i = 0; i < 6u; i += 2u) w16(c, f + i, r16(c, normal + i));
        for (uint32_t i = 0; i < 6u; i += 2u) w16(c, f + i, (uint16_t)(0u - r16(c, f + i)));
        normal = f;
    }
    int bad = CALL(c, VA_POLYGON_LIGHTING, &level, normal, 6u * r16(c, p + 6u) + pts);
    frame_pop(c, f_top);
    if (bad) return 1;
    level = (level & 0xffu) << 16;
    w32(c, VA_COLOUR, r32(c, VA_COLOUR) | level);
    w32(c, VA_COLOUR2, r32(c, VA_COLOUR2) | level);
    return polygon_tail(c, p, back, 1, eax);
}

/* 0x474fa8(P): a textured polygon. The three static arrays are built once
 * (vector_new under the guard bytes 0x4a0f4c/d/e). The texture index is
 * colour dword 0 (front: the back-face test positive and the index not -1)
 * or 2 (back; nothing when it is -1, EAX -1). With 0x49f244 the texture is
 * the shape record's bitmap entry (+8 table) + 4 (0 when the entry is 0),
 * else the shape's texture record (+4 table, 0x14 bytes each); nothing is
 * drawn when that is 0. With lights (0x4a5b04) the light level (the normal
 * negated for a back face), with 0x4aaaac == 2 also the level, half the
 * level and min(level + half, 0xff) after it, and with 0x49f244 the shade
 * row ((0x4a5b04 - 1) * level & ~0xff) + 0x4a5b00 go into the original's
 * locals; 0x4aaaac (0 without lights) is passed on. With 0x49f244 the fill
 * layer draws the (clipped) polygon with the mode record
 * {7, count, UV, texture, shade} put into the context's +0x22c (0x6c6054
 * again afterwards) and the texture's corner UV 3 inside its edges;
 * without, the vertices (dwords) and the record's UV go to the 3D fill
 * 0x468760 (0x49f24c set) or 0x46817c, vertices 1 and 3 swapped for a back
 * face. EAX as the original leaves it. The original's block
 * 0x47533b..0x475385 has no entry (dead). */
static int polygon_textured(ctx *c, uint32_t p, uint32_t *eax) {
    static const struct { uint32_t array, size, count, ctor; } arrays[3] = {
        {VA_TEX_RECORDS, 0xcu, 4u, VA_VECTOR_CTOR}, {VA_TEX_UV, 8u, 4u, VA_EMPTY_CTOR}, {VA_TEX_CLIP, 8u, 0x20u, VA_EMPTY_CTOR}};
    for (uint32_t i = 0; i < 3u; ++i) {
        if (r8(c, VA_TEX_GUARDS + i)) continue;
        if (CALL(c, VA_VECTOR_NEW, NULL, arrays[i].array, arrays[i].size, arrays[i].count, 1u, arrays[i].ctor)) return 1;
        w8(c, VA_TEX_GUARDS + i, (uint8_t)(r8(c, VA_TEX_GUARDS + i) + 1u));
    }
    FRAME(c, f, 0x4cu, 0x00474fa8u, "textured polygon");
#define L(x) (f + 0x4cu - (x))
    uint32_t cp = ((uint32_t)r16(c, p + 0xcu) << 2) + r32(c, VA_COLOURS);
    w32(c, L(0x30), 7);
    uint32_t pts = r32(c, VA_POINTS), r = 0;
    w32(c, L(0x20), 0);
    int bad = facing(c, 6u * r16(c, p + 4u) + pts, 6u * r16(c, p + 6u) + pts, 0, &r);
    uint32_t back = 0, result = 0, chosen = 0;
    if (!bad) {
        uint32_t entry = cp;
        if (!((int32_t)r > 0 && r32(c, cp) != 0xffffffffu)) {
            entry = cp + 8u;
            back = 1;
        }
        if (back && r32(c, entry) == 0xffffffffu) {
            result = 0xffffffffu;
        } else {
            if (r32(c, VA_TEX_ON)) {
                uint32_t record = r32(c, r32(c, VA_SHAPE_RECORD)), e = r32(c, r32(c, record + 8u) + 4u * r32(c, entry));
                uint32_t tex = e ? e + 4u : 0u;
                w32(c, L(0x24), tex);
                result = back ? tex : e ? record : r32(c, entry);
            } else {
                uint32_t off = r32(c, entry) * 0x14u, rec = r32(c, r32(c, VA_SHAPE_RECORD) + 4u) + off;
                w32(c, L(8), rec);
                result = back ? rec : off;
            }
            w32(c, L(4), back);
            chosen = r32(c, r32(c, VA_TEX_ON) ? L(0x24) : L(8));
        }
    }
    if (bad || !chosen) {
        if (!bad) *eax = result;
        frame_pop(c, f_top);
        return bad;
    }
    uint32_t mode = 0;
    if (r32(c, VA_LIGHT_LEVELS)) {
        uint32_t points = r32(c, VA_POINTS), normal = 6u * r16(c, p + 4u) + points, level = 0;
        if (back) {
            for (uint32_t i = 0; i < 6u; i += 2u) w16(c, L(0x14) + i, r16(c, normal + i));
            for (uint32_t i = 0; i < 6u; i += 2u) w16(c, L(0x14) + i, (uint16_t)(0u - r16(c, L(0x14) + i)));
            normal = L(0x14);
        }
        bad = CALL(c, VA_POLYGON_LIGHTING, &level, normal, 6u * r16(c, p + 6u) + points);
        if (!bad) {
            w32(c, L(0x40), level & 0xffu);
            mode = r32(c, VA_LIGHT_MODE);
            if (mode == 2u) {
                w32(c, L(0x38), r32(c, L(0x40)));
                int32_t half = s32(c, L(0x40)) >> 1;
                w32(c, L(0x34), (uint32_t)half);
                half = (int32_t)((uint32_t)half + r32(c, L(0x40)));
                w32(c, L(0x3c), (uint32_t)half);
                if (half >= 0x100) w32(c, L(0x3c), 0xff);
            }
            if (r32(c, VA_TEX_ON))
                w32(c, L(0x20), (((r32(c, VA_LIGHT_LEVELS) - 1u) * r32(c, L(0x40))) & 0xffffff00u) + r32(c, VA_LIGHT_BASE));
        }
    }
    if (!bad) w32(c, L(0xc), mode);
    if (!bad && r32(c, VA_TEX_ON)) {
        uint32_t gc = r32(c, VA_GC);
        w32(c, gc + 0x22cu, L(0x30));
        uint16_t count = r16(c, p + 8u);
        w32(c, L(0x2c), count);
        w32(c, VA_TEX_UV, 3);
        w32(c, VA_TEX_UV + 4u, 3);
        uint32_t tex = r32(c, L(0x24)), u = (uint32_t)(sw(c, tex + 2u) - 3);
        w32(c, VA_TEX_UV + 8u, u);
        w32(c, VA_TEX_UV + 0xcu, 3);
        w32(c, VA_TEX_UV + 0x10u, u);
        uint32_t v = (uint32_t)(sw(c, tex) - 3);
        w32(c, VA_TEX_UV + 0x14u, v);
        w32(c, VA_TEX_UV + 0x18u, 3);
        w32(c, VA_TEX_UV + 0x1cu, v);
        uint16_t first = r16(c, p + 0xau);
        w16(c, VA_VERTEX_COUNT, count);
        w16(c, VA_VERTEX_FIRST, first);
        bad = project(c, &r);
        if (!bad && r == 1u) {
            bad = clip_uv(c, VA_TEX_UV, VA_TEX_CLIP, &r);
            if (!bad) {
                w32(c, L(0x2c), r & 0xffffu);
                w32(c, L(0x28), VA_TEX_CLIP);
                *eax = r32(c, L(4));
            }
        } else if (!bad) {
            w32(c, L(0x28), VA_TEX_UV);
            *eax = r - 1u;
        }
        if (!bad) bad = fill_polygon(c, r32(c, L(4)), eax);
        if (!bad) w32(c, gc + 0x22cu, VA_MODE);
    } else if (!bad) {
        for (uint32_t i = 0; i < r16(c, p + 8u); ++i) {
            uint32_t idx = r16(c, 2u * ((uint32_t)r16(c, p + 0xau) + i) + r32(c, VA_INDEX_LIST));
            uint32_t pt = 6u * idx + r32(c, VA_POINTS), rec = VA_TEX_RECORDS + 12u * i;
            w32(c, rec + 8u, (uint32_t)sw(c, pt + 4u));
            w32(c, rec, (uint32_t)sw(c, pt));
            w32(c, rec + 4u, (uint32_t)sw(c, pt + 2u));
        }
        static const uint8_t uv[8] = {0u, 4u, 8u, 4u, 8u, 0xcu, 0u, 0xcu};
        for (uint32_t i = 0; i < 8u; ++i) w32(c, VA_TEX_UV + 4u * i, r32(c, r32(c, L(8)) + uv[i]));
        if (r32(c, L(4))) {
            for (uint32_t i = 0; i < 0xcu; i += 4u) {   /* vertex 1 <-> vertex 3 (through the original's locals) */
                uint32_t t = r32(c, VA_TEX_RECORDS + 0xcu + i);
                w32(c, L(0x4c) + i, t);
                w32(c, VA_TEX_RECORDS + 0xcu + i, r32(c, VA_TEX_RECORDS + 0x24u + i));
                w32(c, VA_TEX_RECORDS + 0x24u + i, t);
            }
            for (uint32_t i = 0; i < 8u; i += 4u) {     /* UV 1 <-> UV 3 */
                uint32_t t = r32(c, VA_TEX_UV + 8u + i);
                w32(c, L(0x1c) + i, t);
                w32(c, VA_TEX_UV + 8u + i, r32(c, VA_TEX_UV + 0x18u + i));
                w32(c, VA_TEX_UV + 0x18u + i, t);
            }
        }
        uint32_t rec = r32(c, L(8));
        bad = CALL(c, r32(c, VA_TEX_PERSP) ? VA_FILL_PERSP : VA_FILL_3D, eax, VA_TEX_RECORDS, VA_TEX_UV, r16(c, p + 8u),
                   (uint32_t)sw(c, rec + 0x10u), L(0x40), r32(c, L(0xc)), 0u, (uint32_t)sw(c, rec + 0x12u));
    }
#undef L
    frame_pop(c, f_top);
    return bad;
}

/* 0x475b2c(g, node): the BSP walk of a polygon group: nodes of 10 bytes at
 * g+0x2a (distance dword, polygon word +4, links +6/+8). The eye side
 * d = (the polygon's point, sign-extended words) . eye - distance; d < 0
 * visits link +6 first, else +8 first; a link with bit 0x4000 is that
 * polygon's draw slot (index & 0x3fff), a nonnegative one without is a
 * subtree, a negative one nothing; the node's own polygon is drawn between
 * the two. EAX the last call's. */
static int group_bsp_node(ctx *c, uint32_t g, uint32_t node, unsigned depth, uint32_t *eax) {
    if (depth > 4096u) return refuse(c, 0x00475b2cu, "BSP polygon group", "the node links form a cycle (the original overflows its stack)");
    uint32_t rec = node * 10u + r32(c, g + 0x2au);
    uint32_t poly = r32(c, r32(c, g + 0x1cu) + 4u * r16(c, rec + 4u));
    uint32_t pt = 6u * r16(c, poly + 4u) + r32(c, g + 0x14u);
    FRAME(c, f, 0x10u, 0x00475b2cu, "BSP polygon group");   /* ebp-0x10: the vector, ebp-4: the polygon */
    w32(c, f + 0xcu, poly);
    w32(c, f + 8u, (uint32_t)sw(c, pt + 4u));
    w32(c, f, (uint32_t)sw(c, pt));
    w32(c, f + 4u, (uint32_t)sw(c, pt + 2u));
    int32_t d = (int32_t)((uint32_t)ddot(c, f, VA_EYE) - r32(c, rec));
    int32_t first = d < 0 ? sw(c, rec + 6u) : sw(c, rec + 8u), second = d < 0 ? sw(c, rec + 8u) : sw(c, rec + 6u);
    int bad = 0;
    for (unsigned j = 0; !bad && j < 3u; ++j) {
        int32_t link = j == 0u ? first : second;
        if (j == 1u) { bad = slot(c, poly, eax); continue; }
        if (link < 0) continue;
        if (link & 0x4000) bad = slot(c, r32(c, r32(c, g + 0x1cu) + 4u * (uint32_t)(link & 0x3fff)), eax);
        else bad = group_bsp_node(c, g, (uint32_t)link, depth + 1u, eax);
    }
    frame_pop(c, f_top);
    return bad;
}
/* 0x475c04(g): a BSP polygon group: as 0x4759d4 (the context's three
 * fields, the group's colours, points and index list, the point states
 * cleared) with the polygons drawn by 0x475b2c from node 0. EAX the
 * frame's address (loaded for the empty 0x476224). */
static int part_group_bsp(ctx *c, uint32_t g, uint32_t *eax) {
    FRAME(c, f, 0x2cu, 0x00475c04u, "BSP polygon group");
    uint32_t r;
    int bad = part_begin(c, g, f, &r);
    if (!bad) {
        uint32_t gc = r32(c, VA_GC), s228 = r32(c, gc + 0x228u), s22c = r32(c, gc + 0x22cu), s20c = r32(c, gc + 0x20cu);
        w32(c, gc + 0x228u, VA_GC_PEN);
        w32(c, gc + 0x22cu, VA_MODE);
        w32(c, gc + 0x20cu, 0);
        w32(c, VA_COLOURS, r32(c, g + 0x18u));
        uint16_t points = r16(c, g + 0x22u);
        w32(c, VA_POINTS, r32(c, g + 0x14u));
        w16(c, VA_POINT_COUNT, points);
        for (uint32_t i = 0; i < points; ++i) w8(c, VA_STATES + i, 0);
        uint32_t list = r32(c, g + 0x10u);
        w32(c, VA_INDEX_LIST, list ? list : VA_DEFAULT_INDEX);
        bad = group_bsp_node(c, g, 0, 0, &r);
        if (!bad) {
            w32(c, gc + 0x228u, s228);
            w32(c, gc + 0x22cu, s22c);
            w32(c, gc + 0x20cu, s20c);
            *eax = f;
        }
    }
    frame_pop(c, f_top);
    return bad;
}

/* 0x4763f4(part): a sprite part: the bitmap (the shape record's +8 table,
 * entry word +0x10; none: EAX the frame) at the part's centre (+0xa words)
 * in view space; nothing at or behind the near plane (EAX the depth) or
 * when the projected point lies outside +-1000 (EAX its x). Its scale is
 * 0x48c5cc(radius << 2, depth). The view's up direction on the screen:
 * (0,0,0) and (0,0,0x800) through transform_point give the x/z distance
 * (distance_display) and, projected (a zero depth taken as 1), the angle
 * angle_ratio(-dx, dy) + 0xc000 (16 bits; 0 when dx is 0). The height
 * h = ((w4 - w6) * dist, toward zero / 2048) + w6 between the bitmap's
 * words +6 and +4; the offsets multiply_q8(scale, ...) of
 * (byte +0x13 * h / w4) & 0xff and the signed byte +0x12, negated and
 * rotated by the angle (sincos_pair, rotate_2d), move the screen point;
 * then the rotated blit 0x48185c(bitmap, point, 0, {q8(scale, w6),
 * q8(scale, h)}, angle). EAX the blit's. */
static int sprite_body(ctx *c, uint32_t part, uint32_t f, uint32_t *eax);
static int part_sprite(ctx *c, uint32_t part, uint32_t *eax) {
    FRAME(c, f, 0xe8u, 0x004763f4u, "sprite part");
    uint32_t r;
    int bad = part_begin(c, part, f + 0xe8u - 0xb8u, &r);
    if (!bad) bad = sprite_body(c, part, f, eax);
    frame_pop(c, f_top);
    return bad;
}
static int sprite_body(ctx *c, uint32_t part, uint32_t f, uint32_t *eax) {
#define S(x) (f + 0xe8u - (x))
    uint32_t bitmap = r32(c, r32(c, r32(c, r32(c, VA_SHAPE_RECORD)) + 8u) + 4u * r16(c, part + 0x10u));
    if (!bitmap) { *eax = S(0xb8); return 0; }
    uint32_t v = S(0x94), t = S(0x88), p = S(8), s = S(0x18);
    w32(c, v + 8u, (uint32_t)sw(c, part + 0xeu));
    w32(c, v, (uint32_t)sw(c, part + 0xau));
    w32(c, v + 4u, (uint32_t)sw(c, part + 0xcu));
    if (CALL(c, VA_VIEW_TRANSFORM, NULL, v, t)) return 1;
    int32_t depth = s32(c, t + 4u);
    if (depth <= s32(c, VA_NEAR)) { *eax = (uint32_t)depth; return 0; }
    uint32_t scale = 0;
    if (CALL(c, VA_PROJECT_SIZE, &scale, (uint32_t)sw(c, part + 8u) << 2, (uint32_t)depth)) return 1;
    if (CALL(c, VA_PROJECT_POINT, NULL, t, p)) return 1;
    int32_t px = s32(c, p);
    if (px > 1000 || px < -1000 || s32(c, p + 4u) > 1000 || s32(c, p + 4u) < -1000) { *eax = (uint32_t)px; return 0; }
    if (CALL(c, VA_TO_SCREEN, NULL, p, s)) return 1;
    uint32_t a = S(0xc4), b = S(0xd0), pa = S(0xdc), pb = S(0xe8), qa = S(0x2c), qb = S(0x34);
    for (uint32_t i = 0; i < 0xcu; i += 4u) { w32(c, a + i, 0); w32(c, b + i, 0); }
    w32(c, b + 8u, 0x800);
    if (CALL(c, VA_TRANSFORM_POINT, NULL, a, VA_VIEW_MATRIX, pa)) return 1;
    if (CALL(c, VA_TRANSFORM_POINT, NULL, b, VA_VIEW_MATRIX, pb)) return 1;
    w32(c, qa, r32(c, pa));
    w32(c, qa + 4u, r32(c, pa + 8u));
    w32(c, qb, r32(c, pb));
    w32(c, qb + 4u, r32(c, pb + 8u));
    uint32_t dist = 0;
    if (CALL(c, VA_DISTANCE_DISPLAY, &dist, qa, qb)) return 1;
    w32(c, S(0x38), dist);
    if (!r32(c, pa + 4u)) w32(c, pa + 4u, 1);
    if (!r32(c, pb + 4u)) w32(c, pb + 4u, 1);
    if (CALL(c, VA_PROJECT_POINT, NULL, pa, qa)) return 1;
    if (CALL(c, VA_PROJECT_POINT, NULL, pb, qb)) return 1;
    w32(c, S(0x3c), r32(c, qb) - r32(c, qa));
    w32(c, S(0x40), r32(c, qb + 4u) - r32(c, qa + 4u));
    uint32_t angle = 0;
    if (r32(c, S(0x3c))) {
        w32(c, S(0x48), r32(c, S(0x40)));
        w32(c, S(0x4c), 0u - r32(c, S(0x3c)));
        if (CALL(c, VA_ANGLE_RATIO, &angle, r32(c, S(0x4c)), r32(c, S(0x48)))) return 1;
        angle = (angle + 0xc000u) & 0xffffu;
    }
    w16(c, S(0x42), (uint16_t)angle);
    w32(c, S(0x50), (uint32_t)sw(c, bitmap + 6u));
    int32_t width = sw(c, bitmap + 4u);
    int32_t span = mul32((int32_t)((uint32_t)width - r32(c, S(0x50))), s32(c, S(0x38)));
    if (span < 0) span += 0x7ff;
    w32(c, S(0x54), (uint32_t)(span >> 11) + r32(c, S(0x50)));
    int32_t q;
    if (sdiv(c, 0x0047664fu, "sprite part", mul32((int32_t)r8(c, part + 0x13u), s32(c, S(0x54))), width, &q, NULL)) return 1;
    uint32_t m = 0, m68 = 0, scale16 = (uint32_t)(int32_t)(int16_t)scale;
    if (CALL(c, VA_MULTIPLY_Q8, &m, scale16, (uint32_t)q & 0xffu)) return 1;
    w32(c, S(0x60), m);
    if (CALL(c, VA_MULTIPLY_Q8, &m, scale16, (uint32_t)(int32_t)(int8_t)r8(c, part + 0x12u))) return 1;
    w32(c, S(0x5c), m);
    w32(c, S(0x58), r32(c, S(0x60)));
    if (CALL(c, VA_MULTIPLY_Q8, &m, scale, r32(c, S(0x54)))) return 1;
    w32(c, S(0x6c), m);
    if (CALL(c, VA_MULTIPLY_Q8, &m68, scale, r32(c, S(0x50)))) return 1;
    w32(c, S(0x68), m68);
    w32(c, S(0x64), r32(c, S(0x6c)));
    w32(c, S(0x5c), 0u - r32(c, S(0x5c)));
    w32(c, S(0x58), 0u - r32(c, S(0x58)));
    /* The angle travels as a word; the argument's upper half is what the
     * register held (multiply_q8's result, then a local's address). Both
     * callees read the word only. */
    if (CALL(c, VA_SINCOS_PAIR, NULL, (m68 & 0xffff0000u) | r16(c, S(0x42)), S(0x74))) return 1;
    if (CALL(c, VA_ROTATE_2D, NULL, S(0x5c), S(0x74), S(0x7c))) return 1;
    w32(c, s, r32(c, s) + r32(c, S(0x7c)));
    w32(c, s + 4u, r32(c, s + 4u) + r32(c, S(0x78)));
    return CALL(c, VA_ROTATED_BLIT, eax, bitmap, s, 0u, S(0x68), (S(0x5c) & 0xffff0000u) | r16(c, S(0x42)));
#undef S
}

/* 0x40bb4c(a, b): clips the segment a-b (three dwords each) at the near
 * plane: nothing (EAX 0) when both depths lie below it; else the end below
 * it (a first) moves to the crossing: k = 0x47e068(near - its depth,
 * other depth - its depth), x and z += 0x47e08d(other's - its, k), depth = the
 * near plane. AL 1; the upper bytes are the last product's (b's depth when
 * nothing moved). */
static int segment_clip(ctx *c, uint32_t a, uint32_t b, uint32_t *eax) {
    int32_t plane = s32(c, VA_NEAR), ay = s32(c, a + 4u);
    if (plane > ay && plane > s32(c, b + 4u)) { *eax = 0; return 0; }
    uint32_t lo = a, hi = b;
    int32_t ly = ay;
    if (!(plane > ay)) {
        ly = s32(c, b + 4u);
        if (!(plane > ly)) { *eax = ((uint32_t)ly & 0xffffff00u) | 1u; return 0; }
        lo = b;
        hi = a;
    }
    uint32_t k = 0, r = 0;
    if (CALL(c, VA_DIV16, &k, (uint32_t)plane - (uint32_t)ly, r32(c, hi + 4u) - (uint32_t)ly)) return 1;
    if (CALL(c, VA_MUL16, &r, r32(c, hi) - r32(c, lo), k)) return 1;
    w32(c, lo, r32(c, lo) + r);
    if (CALL(c, VA_MUL16, &r, r32(c, hi + 8u) - r32(c, lo + 8u), k)) return 1;
    w32(c, lo + 8u, r32(c, lo + 8u) + r);
    w32(c, lo + 4u, (uint32_t)plane);
    *eax = (r & 0xffffff00u) | 1u;
    return 0;
}

/* ======================================================================
 * Service rows (sim/dispatch/services/shape_draw.h). */
#define ROW(name, body) \
    void es2_sim_service_##name(es2_sim_service_call *sc, void *user) { \
        (void)user; SERVE(sc, c); uint32_t eax = sc->eax; \
        if (body) { failed(sc); return; } \
        sc->eax = eax; \
    }
ROW(shape_instance_push, instance_push(&c, A(1), A(2), &eax))
ROW(shape_instance_pop, instance_pop(&c, A(1), &eax))
ROW(shape_instance_draw, instance_draw(&c, A(0), &eax))
ROW(shape_draw_cells, shape_draw(&c, A(0), 0, &eax))
ROW(shape_draw_instance, shape_draw(&c, A(0), 1, &eax))
ROW(shape_transforms_begin, transforms_begin(&c, A(1), &eax))
ROW(shape_part_begin, part_begin(&c, A(0), A(1), &eax))
ROW(shape_part_transform, part_transform(&c, A(0), A(1), (int32_t)A(2), &eax))
ROW(shape_detail_test, detail_test(&c, A(0), &eax))
ROW(shape_part_list, part_list(&c, A(0), &eax))
ROW(shape_view_save, save_view(&c, A(0), &eax))
ROW(shape_polygon_solid, polygon_solid(&c, A(0), &eax))
ROW(shape_polygon_shaded, polygon_shaded(&c, A(0), &eax))
ROW(shape_polygon_gouraud, polygon_gouraud(&c, A(0), 0x004756d4u, &eax))
ROW(shape_facing_words, facing(&c, A(0), A(1), 0, &eax))
ROW(shape_facing_dwords, facing(&c, A(0), A(1), 1, &eax))
ROW(shape_project, project(&c, &eax))
ROW(shape_project_shift, project_shift(&c, &eax))
ROW(shape_clip, clip(&c, &eax))
ROW(shape_clip_intensity, clip_intensity(&c, A(0), A(1), &eax))
ROW(shape_fill_plain, fill(&c, VA_FILL_PLAIN, 0, &eax))
ROW(shape_fill_polygon, sc->va == 0x0048d708u ? fill_thunk(&c, A(0), &eax) : fill_polygon(&c, A(0), &eax))
ROW(shape_fill_forward_plain, fill_forward(&c, VA_FILL_PLAIN, A(1), A(2), &eax))
ROW(shape_fill_forward_back, fill_forward(&c, VA_FILL_BACK, A(1), A(2), &eax))
ROW(shape_fill_forward_flat, fill_forward(&c, VA_FILL_FLAT, A(1), A(2), &eax))
ROW(shape_part_group, part_group(&c, A(0), &eax))
ROW(shape_part_cells, part_cells(&c, A(0), &eax))
ROW(shape_part_detail, part_detail(&c, A(0), &eax))
ROW(shape_bsp_node, bsp_node(&c, A(0), A(1), 0, &eax))
ROW(shape_part_bsp, part_bsp(&c, A(0), &eax))
ROW(shape_actor_matrix, actor_matrix(&c, A(0), &eax))
ROW(shape_actor_draw, actor_draw(&c, A(0), &eax))
ROW(shape_actor_levels, actor_levels(&c, A(0), &eax))
ROW(shape_project_dwords, project_dwords(&c, &eax))
ROW(shape_clip_uv, clip_uv(&c, A(0), A(1), &eax))
ROW(shape_polygon_plain, polygon_plain(&c, A(0), &eax))
ROW(shape_polygon_lit, polygon_lit(&c, A(0), &eax))
ROW(shape_polygon_gouraud_plain, polygon_gouraud(&c, A(0), 0x00475eecu, &eax))
ROW(shape_polygon_textured, polygon_textured(&c, A(0), &eax))
ROW(shape_group_bsp_node, group_bsp_node(&c, A(0), A(1), 0, &eax))
ROW(shape_part_group_bsp, part_group_bsp(&c, A(0), &eax))
ROW(shape_part_sprite, part_sprite(&c, A(0), &eax))
ROW(shape_segment_clip, segment_clip(&c, A(0), A(1), &eax))
#undef ROW

/* 0x48d708(back) -> 0x48d624: the same row. 0x48d700 (-> 0x48d5c0) has no
 * static reference (table_test) and no row. */
/* 0x4911c0(p, M, n). EAX the sum, ECX the first two products' sum, EDX the
 * last product's high half. */
void es2_sim_service_shape_plane_distance(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t ecx = 0, edx = 0;
    sc->eax = (uint32_t)plane_distance(&c, A(0), A(1), A(2), &ecx, &edx);
    sc->ecx = ecx; sc->edx = edx;
}
/* 0x4034f4(obj): 0x4033e4 with 0x4a1020 = 0x4a95b4 around it (0x4769b8),
 * 0 afterwards. EAX 0. */
void es2_sim_service_shape_actor_levels_offset(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t eax = 0;
    w32(&c, VA_PART_OFFSET, r32(&c, VA_LEVEL_OFFSET));
    if (actor_levels(&c, A(0), &eax)) { failed(sc); return; }
    w32(&c, VA_PART_OFFSET, 0);
    sc->eax = 0;
}
/* 0x4769b8(v): the detail part offset. EAX v. (The scale setter 0x4769a8
 * has no static reference and no row.) */
void es2_sim_service_shape_part_offset_set(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    w32(&c, VA_PART_OFFSET, A(0));
    sc->eax = A(0);
}
/* 0x490d3a(shape, v): the cell words. 0x490d47: the shape record.
 * 0x490d4d(shape): its +0x26. 0x490d58(v): the shape record = v. EAX v or
 * the value read. */
void es2_sim_service_shape_cells_set(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    w32(&c, VA_CELLS, A(1));
    sc->eax = A(1);
}
void es2_sim_service_shape_record_get(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    sc->eax = r32(&c, VA_SHAPE_RECORD);
}
void es2_sim_service_shape_record_of(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    sc->eax = r32(&c, A(0) + 0x26u);
}
void es2_sim_service_shape_record_set(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    w32(&c, VA_SHAPE_RECORD, A(0));
    sc->eax = A(0);
}

/* ======================================================================
 * The view-space projection and the shade ranges (rasterizer.md, "View-space
 * projection and shade ranges"; listing 0x42e878..0x42ea42, 0x430518..0x430637,
 * 0x486e10..0x486e3e, 0x48cce4..0x48cf1c): the projection of the view-space
 * dword points that gouraud.c's flat fill 0x46bc44 calls, and the shade
 * ranges of an object 0x42e8e8 (the flat fill's and 0x42876c's callee) with its
 * callees. The copy into the range table 0x486e10 is inlined (as load.c does). */
#define VA_SHADE_DEFAULT 0x0049aef2u   /* byte: 0x42e8e8 sets the default ranges 0x42ea0c instead */
#define VA_RANGE_RECORDS 0x0049b034u   /* the range records (8 bytes: word pointer, word length) */
#define VA_RANGES 0x004a4fa5u          /* 0x486e10's copy of them (ES2_SIM_VA_GFX_RAMPS) */
#define VA_SHADE_KINDS 0x004cfce0u     /* 5 bytes per object kind: count byte, pointer to {range, row} byte pairs */
#define VA_SHADE_RUNS 0x004cfcf8u      /* the run pairs {start, length} (words) of the rows */
#define VA_SHADE_LAST 0x004cfcfcu      /* the run pairs of the last row */
#define VA_LEVELS 0x004cfd00u          /* level thresholds (unsigned dwords) */
#define VA_LEVELS_KIND0 0x004cfd04u
#define VA_LEVEL_COUNT 0x004cfcd8u     /* words */
#define VA_LEVEL_COUNT_KIND0 0x004cfcdau
#define VA_SHADE_STRIDE 0x004cfcdeu    /* word: run pairs per row */
#define VA_LEVEL_BIAS 0x004cfce8u      /* two dwords: kind 5's level bias below / from 5000 */
#define VA_CENTRE_Y 0x006c6038u

typedef struct regs3 { uint32_t eax, edx, ecx; } regs3;

/* 0x48cce4: the projection of the polygon's vertices from the dword
 * points 0x6c68ec, which are already in view space (no transform). The
 * index word is read once per vertex and again before the y cache store.
 * Without a shift (0x48cd0c..0x48cdb3): an untransformed point (state 0)
 * is projected with the divisor, (x << 8) / div + cx and -(z << 8) / div
 * + cy, into the cache and the output, but its state is NOT set; a point
 * in state 2 is copied from the cache, others leave their entry; the
 * result is 0. With a shift (0x48cdb8..0x48cea1): an untransformed point
 * whose depth +4 lies below the near plane (signed) is marked 1 and makes
 * the result 1, else it is marked 2 and projected (x << shift) / depth +
 * cx, -(z << shift) / depth + cy (shift and depth read again for y); a
 * point in state 1 makes the result 1, one in state 2 is copied. Result 1
 * (0x48cead..0x48cefd): every vertex's point is copied into the view
 * points 0x6c6ce2 (+8, +0, +4); result 0: the output count becomes the
 * vertex count. EAX = ECX = the result; EDX the vertex count with a shift
 * (without one the original's uninitialised local: not a contract). */
static int project_view(ctx *c, regs3 *o) {
    uint32_t list = (uint32_t)r16(c, VA_VERTEX_FIRST) * 2u + r32(c, VA_INDEX_LIST), result = 0, i = 0;
    int32_t q;
    if (!r32(c, VA_FOCAL_SHIFT)) {
        for (; (int32_t)i < (int32_t)r16(c, VA_VERTEX_COUNT); ++i, list += 2u) {
            uint32_t idx = r16(c, list);
            if (r8(c, VA_STATES + idx) == 0) {
                uint32_t p = 12u * idx + r32(c, VA_POINTS_DWORD);
                if (sdiv(c, 0x0048cd38u, "projection", shl32(s32(c, p), 8), s32(c, VA_DIVISOR), &q, NULL)) return 1;
                uint32_t x = (uint32_t)q + r32(c, VA_CENTRE);
                w32(c, VA_CACHE + 8u * idx, x);
                w32(c, VA_OUT + 8u * i, x);
                if (sdiv(c, 0x0048cd56u, "projection", (int32_t)(0u - (uint32_t)shl32(s32(c, p + 8u), 8)), s32(c, VA_DIVISOR), &q, NULL))
                    return 1;
                uint32_t again = r16(c, list), y = (uint32_t)q + r32(c, VA_CENTRE_Y);
                w32(c, VA_CACHE + 8u * again + 4u, y);
                w32(c, VA_OUT + 8u * i + 4u, y);
            } else {
                idx = r16(c, list);
                if (r8(c, VA_STATES + idx) == 2u) copy_cached(c, idx, i);
            }
        }
    } else {
        for (; (int32_t)r16(c, VA_VERTEX_COUNT) > (int32_t)i; ++i, list += 2u) {
            uint32_t idx = r16(c, list);
            if (r8(c, VA_STATES + idx) == 0) {
                uint32_t p = 12u * idx + r32(c, VA_POINTS_DWORD);
                if (s32(c, p + 4u) < s32(c, VA_NEAR)) {
                    w8(c, VA_STATES + idx, 1);
                    result = 1;
                    continue;
                }
                w8(c, VA_STATES + idx, 2);
                if (sdiv(c, 0x0048ce12u, "perspective projection", shl32(s32(c, p), r32(c, VA_FOCAL_SHIFT)), s32(c, p + 4u), &q, NULL)) return 1;
                uint32_t x = (uint32_t)q + r32(c, VA_CENTRE);
                w32(c, VA_CACHE + 8u * idx, x);
                w32(c, VA_OUT + 8u * i, x);
                int32_t z = (int32_t)(0u - (uint32_t)shl32(s32(c, p + 8u), r32(c, VA_FOCAL_SHIFT)));
                if (sdiv(c, 0x0048ce33u, "perspective projection", z, s32(c, p + 4u), &q, NULL)) return 1;
                uint32_t again = r16(c, list), y = (uint32_t)q + r32(c, VA_CENTRE_Y);
                w32(c, VA_CACHE + 8u * again + 4u, y);
                w32(c, VA_OUT + 8u * i + 4u, y);
            } else {
                idx = r16(c, list);
                uint8_t state = r8(c, VA_STATES + idx);
                if (state == 1u) result = 1;
                else if (state == 2u) copy_cached(c, idx, i);
            }
        }
    }
    if (result == 1u) {
        list = (uint32_t)r16(c, VA_VERTEX_FIRST) * 2u + r32(c, VA_INDEX_LIST);
        for (uint32_t j = 0; (int32_t)j < (int32_t)r16(c, VA_VERTEX_COUNT); ++j, list += 2u) {
            uint32_t src = 12u * r16(c, list) + r32(c, VA_POINTS_DWORD), dst = VA_VIEW_POINTS + 12u * r16(c, list);
            w32(c, dst + 8u, r32(c, src + 8u));
            w32(c, dst, r32(c, src));
            w32(c, dst + 4u, r32(c, src + 4u));
        }
    } else {
        w16(c, VA_OUT_COUNT, r16(c, VA_VERTEX_COUNT));
    }
    o->eax = o->ecx = result;
    o->edx = i;
    return 0;
}

/* 0x486e10(a, n, src, 0), inlined: n records of 8 bytes from src to the
 * range table 0x4a4fa5 + 8a, dword by dword in ascending order. EAX the
 * last dword copied (eax with n = 0); ECX 0 afterwards. */
static uint32_t ranges_copy(ctx *c, uint32_t a, uint32_t n, uint32_t src, uint32_t eax) {
    uint32_t dst = 8u * a + VA_RANGES;
    for (uint32_t k = 0; k < 2u * n; ++k) {
        eax = r32(c, src + 4u * k);
        w32(c, dst + 4u * k, eax);
    }
    return eax;
}
/* The original's counters are words compared signed: a count of 0x8000 or
 * more never ends (the counter wraps to negative); a negative record count
 * makes 0x486e10's LOOP copy until it faults. */
static int ranges_count(ctx *c, uint32_t va, uint32_t n) {
    if ((int32_t)n < 0 || (int32_t)n > 0x7fff) return refuse(c, va, "shade ranges", "record count outside 0..0x7fff (the original faults or never ends)");
    return 0;
}
/* Writes the words v, v + 1, ... for (int16) [len_va] steps (read again each step) from *p on. */
static int ranges_fill(ctx *c, uint32_t va, uint32_t *p, uint32_t v, uint32_t len_va) {
    for (uint32_t k = 0; (int16_t)(uint16_t)k < (int16_t)r16(c, len_va); ++k) {
        if (k >= 0x8000u) return refuse(c, va, "shade ranges", "a run of 0x8000 words or more (the original's counter never ends)");
        w16(c, *p, (uint16_t)(v + k));
        *p += 2u;
    }
    return 0;
}
/* 0x430518(a, n, R): for the range records e = [0x49b034] + 8(a + i),
 * i < n: the words [R], [R] + 1, ... ([R + 2] of them, read again each
 * step) from the pointer [e], then the length [e + 4] = [R + 2]; then
 * 0x486e10(a, n, the first record, 0). EAX 0x486e10's, ECX 0, EDX n. */
static int ranges_runs(ctx *c, uint32_t a, uint32_t n, uint32_t rec, regs3 *o) {
    if (ranges_count(c, 0x00430518u, n)) return 1;
    uint32_t first = 8u * a + r32(c, VA_RANGE_RECORDS), e = first;
    for (uint32_t i = 0; i < n; ++i, e += 8u) {
        uint32_t p = r32(c, e);
        if (ranges_fill(c, 0x00430518u, &p, r16(c, rec), rec + 2u)) return 1;
        w16(c, e + 4u, r16(c, rec + 2u));
    }
    o->eax = ranges_copy(c, a, n, first, first);
    o->ecx = 0;
    o->edx = n;
    return 0;
}
/* 0x430590(a, n, R): records R_i = R + 6i {word count, dword list}; for
 * record i its pairs start at the pointer [[R_i + 2] + 4i] (indexed by i
 * twice; every caller passes n = 1) and follow each other (4 bytes: word
 * start, word length). Per pair (the count read again each step) the
 * length is added to a word sum and the words start, start + 1, ... are
 * written on from the pointer [e] (one running pointer for all pairs);
 * then [e + 4] = the sum. Then the copy as 0x430518; the same registers. */
static int ranges_spans(ctx *c, uint32_t a, uint32_t n, uint32_t rec, regs3 *o) {
    if (ranges_count(c, 0x00430590u, n)) return 1;
    uint32_t first = 8u * a + r32(c, VA_RANGE_RECORDS), e = first;
    for (uint32_t i = 0; i < n; ++i, rec += 6u, e += 8u) {
        uint32_t s = r32(c, r32(c, rec + 2u) + 4u * i), p = r32(c, e);
        uint16_t sum = 0;
        for (uint32_t j = 0; (int16_t)(uint16_t)j < (int16_t)r16(c, rec); ++j, s += 4u) {
            if (j >= 0x8000u) return refuse(c, 0x00430590u, "shade ranges", "0x8000 pairs or more (the original's counter never ends)");
            sum = (uint16_t)(sum + r16(c, s + 2u));
            if (ranges_fill(c, 0x00430590u, &p, r16(c, s), s + 2u)) return 1;
        }
        w16(c, e + 4u, sum);
    }
    o->eax = ranges_copy(c, a, n, first, first);
    o->ecx = 0;
    o->edx = n;
    return 0;
}
/* 0x42e878(obj, level): the level index of an object: kind (int16)
 * [obj + 4] 0 searches the thresholds [0x4cfd04] (count [0x4cfcda]), the
 * others [0x4cfd00] (count [0x4cfcd8]); kind 5 first adds the bias
 * [0x4cfce8 + 4 * (the vtable slot +0x10's (int16) AX >= 5000)] to the
 * level (a negative sum becomes 0). The index is the first i <
 * (int16)(count - 1) whose threshold is above the level (unsigned), else
 * (int16)(count - 1). EAX the index, EDX the threshold's address, ECX
 * (ECX & 0xffff0000 | count) - 1 (with the object as ECX after kind 5's
 * call). */
static int ranges_level(ctx *c, uint32_t obj, uint32_t level, uint32_t ecx, regs3 *o) {
    int32_t kind = sw(c, obj + 4u);
    uint32_t tab;
    if (kind == 0) {
        tab = r32(c, VA_LEVELS_KIND0);
        ecx = (ecx & 0xffff0000u) | r16(c, VA_LEVEL_COUNT_KIND0);
    } else {
        if (kind == 5) {
            uint32_t ax = 0;
            if (CALL(c, r32(c, r32(c, obj) + 0x10u), &ax, obj)) return 1;
            level += r32(c, VA_LEVEL_BIAS + ((int32_t)(int16_t)ax >= 5000 ? 4u : 0u));
            if ((int32_t)level < 0) level = 0;
            ecx = obj;
        }
        tab = r32(c, VA_LEVELS);
        ecx = (ecx & 0xffff0000u) | r16(c, VA_LEVEL_COUNT);
    }
    ecx -= 1u;
    uint32_t i = 0;
    while ((int32_t)i < (int32_t)(int16_t)ecx && level >= r32(c, tab)) {
        ++i;
        tab += 4u;
    }
    o->eax = i;
    o->edx = tab;
    o->ecx = ecx;
    return 0;
}
/* 0x42ea0c: the default ranges: 0x430518(2i, 1, R) for i = 0..6 and
 * 0x430518(0x18, 1, R) with the stack record R = {0x10, 0x10}. EAX and
 * ECX the last call's, EDX R's dword (0x00100010). */
static int ranges_default(ctx *c, regs3 *o) {
    FRAME(c, f, 8u, 0x0042ea0cu, "default shade ranges");
    w16(c, f, 0x10u);
    w16(c, f + 2u, 0x10u);
    for (uint32_t i = 0; i < 7u; ++i)
        if (ranges_runs(c, 2u * i, 1, f, o)) return 1;
    if (ranges_runs(c, 0x18u, 1, f, o)) return 1;
    o->edx = r32(c, f);
    frame_pop(c, f_top);
    return 0;
}
/* 0x42e8e8(obj, level, flag): the shade ranges of an object. With
 * [0x49aef2] set it is 0x42ea0c. Else the kind entry e = [0x4cfce0] +
 * 5 * (int16) [obj + 4]; with a zero count byte nothing happens (EAX
 * obj's high word and the kind word, EDX the kind, ECX unchanged). Else
 * (a negative level counts as 0) k = (int16) 0x42e878(obj, level), P =
 * [0x4cfcf8] + 4k, Q = [0x4cfcfc] + 4k, and for each byte pair {range,
 * row} of [e + 1] (count [e], read again each step) the run pair A = P +
 * 4 * low32(row * (int16) [0x4cfcde]): flag byte 0: 0x430518(range, 1,
 * A); else 0x430590(range, 1, D) with the stack record D = {2, list},
 * list = {&p1, &p2}, p1 = A's pair shortened by q + 1 (q = (int16) length
 * >> 2), p2 = Q's pair moved to its last q + 1 words. 0x430590 reads the
 * two pairs from &p1 on, so its second pair is the list's first dword
 * (p1's own stack address: low word the start, high word the length) and
 * p2 is never used (an original bug kept here: the words written depend
 * on the stack address). The frame is the original's: EBP = the arguments
 * - 8, its 0x20 bytes of locals below. EAX the count byte, EDX/ECX the
 * last call's (P and Q without a pair). */
static int ranges_select(ctx *c, uint32_t obj, uint32_t level, uint32_t flag, uint32_t ecx, regs3 *o) {
    if (r8(c, VA_SHADE_DEFAULT)) return ranges_default(c, o);
    uint32_t kind = r16(c, obj + 4u), e = 5u * (uint32_t)(int32_t)(int16_t)kind + r32(c, VA_SHADE_KINDS);
    if (!r8(c, e)) {
        o->eax = (obj & 0xffff0000u) | kind;
        o->edx = (uint32_t)(int32_t)(int16_t)kind;
        o->ecx = ecx;
        return 0;
    }
    if ((int32_t)level < 0) level = 0;
    uint32_t pairs = r32(c, e + 1u);
    FRAME(c, f, 0x34u, 0x0042e8e8u, "shade ranges");
    uint32_t bp = f_top - 8u;
    regs3 lv;
    if (ranges_level(c, obj, level, ecx, &lv)) return 1;
    uint32_t k4 = 4u * (uint32_t)(int32_t)(int16_t)lv.eax;
    uint32_t q_run = k4 + r32(c, VA_SHADE_LAST), p_run = k4 + r32(c, VA_SHADE_RUNS);
    w32(c, bp - 0x1cu, q_run);
    w32(c, bp - 0x20u, p_run);
    o->edx = p_run;
    o->ecx = q_run;
    for (uint32_t i = 0; (int32_t)i < (int32_t)r8(c, e); ++i, pairs += 2u) {
        uint32_t a = 4u * (uint32_t)mul32((int32_t)r8(c, pairs + 1u), sw(c, VA_SHADE_STRIDE)) + r32(c, bp - 0x20u);
        if (!(flag & 0xffu)) {
            if (ranges_runs(c, r8(c, pairs), 1, a, o)) return 1;
            continue;
        }
        w16(c, bp - 8u, 2);
        w32(c, bp - 6u, bp - 0x10u);
        w16(c, bp - 0x12u, r16(c, a + 2u));
        w16(c, bp - 0x14u, r16(c, a));
        w16(c, bp - 0x16u, r16(c, r32(c, bp - 0x1cu) + 2u));
        uint16_t start = r16(c, r32(c, bp - 0x1cu));
        w16(c, bp - 0x18u, start);
        int32_t quarter = (int32_t)(int16_t)r16(c, bp - 0x12u) >> 2;
        w16(c, bp - 0x12u, (uint16_t)(r16(c, bp - 0x12u) - (uint16_t)(quarter + 1)));
        start = (uint16_t)(start + r16(c, bp - 0x16u));
        start = (uint16_t)(start - (uint16_t)quarter);
        w16(c, bp - 0x18u, start);
        w16(c, bp - 0x16u, (uint16_t)(quarter + 1));
        w32(c, bp - 0x10u, bp - 0x14u);
        w32(c, bp - 0xcu, bp - 0x18u);
        if (ranges_spans(c, r8(c, pairs), 1, bp - 8u, o)) return 1;
    }
    o->eax = r8(c, e);
    frame_pop(c, f_top);
    return 0;
}

/* Service rows (sim/dispatch/services/shade_ranges.h): EAX, EDX, ECX. */
#define ROW3(name, body) \
    void es2_sim_service_##name(es2_sim_service_call *sc, void *user) { \
        (void)user; SERVE(sc, c); regs3 o = {sc->eax, sc->edx, sc->ecx}; \
        if (body) { failed(sc); return; } \
        sc->eax = o.eax; sc->edx = o.edx; sc->ecx = o.ecx; \
    }
ROW3(shape_project_view, project_view(&c, &o))
ROW3(shade_ranges, ranges_select(&c, A(0), A(1), A(2), sc->ecx, &o))
ROW3(shade_ranges_level, ranges_level(&c, A(0), A(1), sc->ecx, &o))
ROW3(shade_ranges_default, ranges_default(&c, &o))
ROW3(shade_ranges_runs, ranges_runs(&c, A(0), A(1), A(2), &o))
ROW3(shade_ranges_spans, ranges_spans(&c, A(0), A(1), A(2), &o))
#undef ROW3

/* ======================================================================
 * The actor draw slots and the explosion binning (rasterizer.md, "Actor draw
 * slots and explosion binning"; listing 0x40316c..0x4031d3, 0x408228..0x408282,
 * 0x408e6c..0x408e98, 0x409cd0..0x409d29, 0x40bc14..0x40bf71,
 * 0x40ded8..0x40defc, 0x4174c8..0x41752f, 0x4215cc..0x4215f0): the actor draw
 * slots (the beam, mech, flyer, debris and meteor slots, the
 * configuration part 0x40316c and its offset slot 0x40ded8) and the explosion
 * binning callees 0x408228/0x408240 of scene.c's 0x42841c. The detail offset
 * setter 0x4769b8 is a direct store (as in 0x4034f4); the empty 0x476224 is not
 * repeated; 0x40bc14's memset of the point states is done as direct byte
 * stores. The mech's part callee 0x4030d0, the hooks dist_octagonal 0x47de4c
 * and camera_attached 0x401078, the 2D fill 0x468414 and every part slot go
 * through the dispatch; 0x4030d0 is the registry hook part_shapes_bind, whose
 * dispatch entry runs core's es2_part_shapes_bind_registers (ECX stays the
 * entry ECX; the original's pool cursor in ECX is dead at 0x417510). */
#define VA_BEAM_KINDS 0x004a9804u      /* 6 bytes per beam kind: width word, gc record word, record index word */
#define VA_BEAM_RECORDS 0x004a9808u    /* 20 bytes each: four dwords, the page word +0x10 */
#define VA_BEAM_INDEX 0x004a9712u      /* the beam's index list (0x40bc85 tests the constant: always taken) */
#define VA_MECH_ON 0x004a9b82u         /* word */
#define VA_CONFIG_OFFSET 0x004a9868u   /* 0x40ded8's detail offset */
#define VA_FLYER_OFFSET 0x004a9dc4u    /* 0x4215cc's detail offset */
#define VA_CAMERA 0x004d24eau          /* the camera object 0x408240 hands to camera_attached */
#define VA_MECH_PART 0x004030d0u       /* the hook part_shapes_bind */
#define VA_DIST_OCTAGONAL 0x0047de4cu  /* hooks */
#define VA_CAMERA_ATTACHED 0x00401078u
#define VA_POLY_FILL 0x00468414u       /* poly.c's row */

/* 0x40bc14(beam): a beam. EBP = the frame + 0xcc; the gc's +0x22c points
 * at the stack record {0, the kind's record word} for the duration and
 * is restored at the end; 0x48c444(0); the beam's dword points +0x58 and
 * count +0x56 become the render context's, the states are cleared, the
 * index list is 0x4a9712. Per round i < the signed count byte +0x54
 * (re-read): kinds 1 and 7 first draw the 4-vertex polygon at index word
 * (i << (3 - shift byte +0x55)) & 0xffff through 0x48ca70, 0x48cf20 (when
 * it returns nonzero) and 0x48d5c0; then the segment of the first two
 * points is transformed (0x48c57c), clipped (0x40bb4c; skipped when AL is
 * 0), projected and put on screen; its end widths (0x48c5cc of the kind's
 * width word and each end's depth, at least 2), the unit normal of the
 * screen segment (each component << 11 divided by dist_octagonal; skipped
 * when 0) and the kind's record (four dwords << 8 as the corners' +0xc/
 * +0x10, the page word) give the quad handed to 0x468414(4, quad, 0,
 * page, 0, 0). EAX the saved +0x22c, ECX the gc, EDX the sign-extended
 * count byte (the loop test's). */
static int beam_body(ctx *c, uint32_t beam, uint32_t bp, regs3 *o) {
#define B(x) (bp - (x))
    w32(c, B(8u), 0);
    uint32_t gc = r32(c, VA_GC);
    w32(c, B(0xcu), r32(c, gc + 0x22cu));
    w32(c, B(4u), (uint32_t)sw(c, 6u * (uint32_t)sw(c, beam + 0x52u) + r32(c, VA_BEAM_KINDS) + 2u));
    w32(c, gc + 0x22cu, B(8u));
    if (CALL(c, VA_OBJECT_SELECT, NULL, 0u)) return 1;
    uint16_t count = r16(c, beam + 0x56u);
    w32(c, VA_POINTS_DWORD, r32(c, beam + 0x58u));
    w16(c, VA_POINT_COUNT, count);
    for (uint32_t i = 0; i < count; ++i) w8(c, VA_STATES + i, 0);
    w32(c, VA_INDEX_LIST, VA_BEAM_INDEX);
    uint32_t a = B(0x4cu), b = B(0x40u), pa = B(0x5cu), pb = B(0x54u), r = 0;
    for (int32_t i = 0; i < (int32_t)(int8_t)r8(c, beam + 0x54u); ++i) {
        int32_t kind = sw(c, beam + 0x52u);
        if (kind == 1 || kind == 7) {
            uint32_t n = (3u - (uint32_t)(int32_t)(int8_t)r8(c, beam + 0x55u)) & 31u;
            uint16_t first = (uint16_t)((uint32_t)i << n);
            w16(c, VA_VERTEX_COUNT, 4);
            w16(c, VA_VERTEX_FIRST, first);
            if (project_dwords(c, &r)) return 1;
            if (r && clip(c, &r)) return 1;
            if (fill(c, VA_FILL_PLAIN, 0, &r)) return 1;
        }
        if (CALL(c, VA_VECTOR_NEW, NULL, a, 0xcu, 2u, 1u, VA_VECTOR_CTOR)) return 1;
        if (CALL(c, VA_VECTOR_NEW, NULL, pa, 8u, 2u, 1u, VA_EMPTY_CTOR)) return 1;
        if (CALL(c, VA_VIEW_TRANSFORM, NULL, r32(c, beam + 0x58u), a)) return 1;
        if (CALL(c, VA_VIEW_TRANSFORM, NULL, r32(c, beam + 0x58u) + 0xcu, b)) return 1;
        if (segment_clip(c, a, b, &r)) return 1;
        if (!(r & 0xffu)) continue;
        if (CALL(c, VA_PROJECT_POINT, NULL, a, pa) || CALL(c, VA_PROJECT_POINT, NULL, b, pb)) return 1;
        if (CALL(c, VA_TO_SCREEN, NULL, pa, pa) || CALL(c, VA_TO_SCREEN, NULL, pb, pb)) return 1;
        w32(c, B(0x10u), (uint32_t)sw(c, 6u * (uint32_t)sw(c, beam + 0x52u) + r32(c, VA_BEAM_KINDS)));
        uint32_t wa = 0, wb = 0;
        if (CALL(c, VA_PROJECT_SIZE, &wa, r32(c, B(0x10u)), r32(c, a + 4u))) return 1;
        if (CALL(c, VA_PROJECT_SIZE, &wb, r32(c, B(0x10u)), r32(c, b + 4u))) return 1;
        w32(c, B(0x14u), wb);
        int32_t ea = (int32_t)wa;
        if (ea < 2) ea = 2;
        if (s32(c, B(0x14u)) < 2) w32(c, B(0x14u), 2);
        w32(c, B(0x1cu), r32(c, pb + 4u) - r32(c, pa + 4u));
        w32(c, B(0x18u), r32(c, pa) - r32(c, pb));
        w32(c, B(0x30u), r32(c, B(0x18u)));
        w32(c, B(0x34u), r32(c, B(0x1cu)));
        uint32_t len = 0;
        if (CALL(c, VA_DIST_OCTAGONAL, &len, r32(c, B(0x34u)), r32(c, B(0x30u)))) return 1;
        if (!len) continue;
        int32_t q;
        if (sdiv(c, 0x0040be1eu, "beam draw", shl32(s32(c, B(0x1cu)), 11), (int32_t)len, &q, NULL)) return 1;
        w32(c, B(0x1cu), (uint32_t)q);
        if (sdiv(c, 0x0040be2au, "beam draw", shl32(s32(c, B(0x18u)), 11), (int32_t)len, &q, NULL)) return 1;
        w32(c, B(0x18u), (uint32_t)q);
        int32_t nx = s32(c, B(0x1cu)), ny = s32(c, B(0x18u)), eb = s32(c, B(0x14u));
        w32(c, B(0x24u), (uint32_t)((int32_t)((uint32_t)mul32(nx, ea) + 0x400u) >> 11));
        w32(c, B(0x20u), (uint32_t)((int32_t)((uint32_t)mul32(ny, ea) + 0x400u) >> 11));
        w32(c, B(0x2cu), (uint32_t)((int32_t)((uint32_t)mul32(nx, eb) + 0x400u) >> 11));
        w32(c, B(0x28u), (uint32_t)((int32_t)((uint32_t)mul32(ny, eb) + 0x400u) >> 11));
        uint32_t rec = 20u * (uint32_t)sw(c, 6u * (uint32_t)sw(c, beam + 0x52u) + r32(c, VA_BEAM_KINDS) + 4u) + r32(c, VA_BEAM_RECORDS);
        /* The quad: four points of 0x1c bytes from EBP - 0xcc (x, y, and
         * +0xc/+0x10 from the record; +8, +0x14 and +0x18 stay as the
         * frame holds them, uninitialised stack in the original). */
        w32(c, B(0xccu), r32(c, pa) - r32(c, B(0x24u)));
        w32(c, B(0xc8u), r32(c, pa + 4u) - r32(c, B(0x20u)));
        w32(c, B(0xb0u), r32(c, pa) + r32(c, B(0x24u)));
        w32(c, B(0xacu), r32(c, pa + 4u) + r32(c, B(0x20u)));
        w32(c, B(0x94u), r32(c, pb) + r32(c, B(0x2cu)));
        w32(c, B(0x90u), r32(c, pb + 4u) + r32(c, B(0x28u)));
        w32(c, B(0x78u), r32(c, pb) - r32(c, B(0x2cu)));
        w32(c, B(0x74u), r32(c, pb + 4u) - r32(c, B(0x28u)));
        uint32_t v = r32(c, rec) << 8;
        w32(c, B(0xa4u), v);
        w32(c, B(0xc0u), v);
        v = r32(c, rec + 8u) << 8;
        w32(c, B(0x6cu), v);
        w32(c, B(0x88u), v);
        v = r32(c, rec + 4u) << 8;
        w32(c, B(0x68u), v);
        w32(c, B(0xbcu), v);
        v = r32(c, rec + 0xcu) << 8;
        w32(c, B(0x84u), v);
        w32(c, B(0xa0u), v);
        if (CALL(c, VA_POLY_FILL, NULL, 4u, B(0xccu), 0u, (uint32_t)sw(c, rec + 0x10u), 0u, 0u)) return 1;
    }
    o->edx = (uint32_t)(int32_t)(int8_t)r8(c, beam + 0x54u);
    gc = r32(c, VA_GC);
    o->eax = r32(c, B(0xcu));
    w32(c, gc + 0x22cu, o->eax);
    o->ecx = gc;
    return 0;
#undef B
}
static int beam_draw(ctx *c, uint32_t beam, regs3 *o) {
    FRAME(c, f, 0xccu, 0x0040bc14u, "beam draw");
    int bad = beam_body(c, beam, f + 0xccu, o);
    frame_pop(c, f_top);
    return bad;
}
/* 0x4174c8(obj): a mech. Nothing (EAX and ECX as on entry) while the word
 * 0x4a9b82 is 0; else, with cfg = [obj+0x1f2] kept in a local, 0x4030d0(e,
 * [obj+0x202] + 4, [obj+0x1f2] + 0xec) for each 6-byte entry e from
 * [cfg+0xe6] while the index is below the signed word [cfg+0xea] (re-read,
 * like both arguments), then 0x4033e4(obj). EAX 0x4033e4's, ECX cfg. */
static int mech_draw(ctx *c, uint32_t obj, regs3 *o) {
    if (!r16(c, VA_MECH_ON)) return 0;
    uint32_t cfg = r32(c, obj + 0x1f2u), e = r32(c, cfg + 0xe6u);
    for (uint32_t i = 0; (int16_t)i < (int16_t)r16(c, cfg + 0xeau); ++i, e += 6u) {
        uint32_t last = r32(c, obj + 0x1f2u) + 0xecu;
        if (CALL(c, VA_MECH_PART, NULL, e, r32(c, obj + 0x202u) + 4u, last)) return 1;
    }
    o->ecx = cfg;
    return actor_levels(c, obj, &o->eax);
}
/* 0x4215cc(obj): 0x4033e4 with the part offset 0x4a1020 = [0x4a9dc4], 0
 * afterwards. EAX = ECX = 0 (0x4769b8(0) and its argument). */
static int flyer_draw(ctx *c, uint32_t obj, regs3 *o) {
    w32(c, VA_PART_OFFSET, r32(c, VA_FLYER_OFFSET));
    uint32_t r;
    if (actor_levels(c, obj, &r)) return 1;
    w32(c, VA_PART_OFFSET, 0);
    o->eax = o->ecx = 0;
    return 0;
}
/* 0x408e6c(obj): debris: the part offset = [obj+0x50], 0x401fe4(obj), the
 * draw slot of [obj+0x34], the offset 0. EAX = ECX = 0. */
static int debris_draw(ctx *c, uint32_t obj, regs3 *o) {
    w32(c, VA_PART_OFFSET, r32(c, obj + 0x50u));
    uint32_t r;
    if (actor_matrix(c, obj, &r) || slot(c, r32(c, obj + 0x34u), &r)) return 1;
    w32(c, VA_PART_OFFSET, 0);
    o->eax = o->ecx = 0;
    return 0;
}
/* 0x409cd0(obj): a meteor: the matrix as 0x401fe4 (inlined when the byte
 * +0x4b is set: the same euler_matrix(obj+0xc, obj+0x12) once and
 * 0x48c444(obj+0x12)), then the draw slot of [obj+0x41] (byte set) or
 * [obj+0x34]. EAX the slot's, ECX the part (the popped argument). */
static int meteor_draw(ctx *c, uint32_t obj, regs3 *o) {
    uint32_t r;
    if (actor_matrix(c, obj, &r)) return 1;
    uint32_t part = r32(c, obj + (r8(c, obj + 0x4bu) ? 0x41u : 0x34u));
    o->ecx = part;
    return slot(c, part, &o->eax);
}
/* 0x40316c(part): a configuration part: nothing (EAX 0) without its shape
 * [part+0x10]; else the cell words 0x6b7b6c and the shape record 0x6c6064
 * are saved, the record = [shape+0x26], 0x476120(part, frame of 0x24),
 * the cells = [part+0x14], 0x476808([part+0x10]), both restored. EAX the
 * saved record, ECX the shape (0x476808's popped argument). */
static int config_body(ctx *c, uint32_t part, uint32_t f, regs3 *o) {
    uint32_t cells = r32(c, VA_CELLS), record = r32(c, VA_SHAPE_RECORD), r;
    w32(c, VA_SHAPE_RECORD, r32(c, r32(c, part + 0x10u) + 0x26u));
    if (part_begin(c, part, f, &r)) return 1;
    w32(c, VA_CELLS, r32(c, part + 0x14u));
    uint32_t shape = r32(c, part + 0x10u);
    if (part_list(c, shape, &r)) return 1;
    w32(c, VA_CELLS, cells);
    w32(c, VA_SHAPE_RECORD, record);
    o->eax = record;
    o->ecx = shape;
    return 0;
}
static int config_part(ctx *c, uint32_t part, regs3 *o) {
    if (!r32(c, part + 0x10u)) { o->eax = 0; return 0; }
    FRAME(c, f, 0x24u, 0x0040316cu, "configuration part");
    int bad = config_body(c, part, f, o);
    frame_pop(c, f_top);
    return bad;
}
/* 0x40ded8(part): 0x40316c with the part offset = [0x4a9868], 0
 * afterwards. EAX = ECX = 0. */
static int config_part_offset(ctx *c, uint32_t part, regs3 *o) {
    w32(c, VA_PART_OFFSET, r32(c, VA_CONFIG_OFFSET));
    regs3 inner = *o;
    if (config_part(c, part, &inner)) return 1;
    w32(c, VA_PART_OFFSET, 0);
    o->eax = o->ecx = 0;
    return 0;
}
/* 0x408228(obj): the explosion's parent [obj+0x57] + 0x1e8, 0 without one.
 * EDX and ECX as on entry. */
static int explosion_cells(ctx *c, uint32_t obj, regs3 *o) {
    uint32_t p = r32(c, obj + 0x57u);
    o->eax = p ? p + 0x1e8u : 0u;
    return 0;
}
/* 0x408240(obj): whether the explosion is hidden: never (EAX 0) for the
 * type bytes +0x41 2 and 0xb..0xe or without a parent [obj+0x57]; else 1
 * when camera_attached([0x4d24ea], parent) returns a nonzero AX. EDX and
 * ECX as on entry, except after the call: EDX camera_attached's result
 * (it leaves it there too, 0x401096). */
static int explosion_hidden(ctx *c, uint32_t obj, regs3 *o) {
    uint32_t parent = r32(c, obj + 0x57u);
    uint8_t type = r8(c, obj + 0x41u);
    if (type == 2u || (uint8_t)(type - 0xbu) < 4u || !parent) { o->eax = 0; return 0; }
    uint32_t r = 0;
    if (CALL(c, VA_CAMERA_ATTACHED, &r, r32(c, VA_CAMERA), parent)) return 1;
    o->edx = r;
    o->eax = (r & 0xffffu) ? 1u : 0u;
    return 0;
}

/* Service rows (sim/dispatch/services/actor_draw.h): EAX, EDX, ECX. */
#define ROW8(name, body) \
    void es2_sim_service_##name(es2_sim_service_call *sc, void *user) { \
        (void)user; SERVE(sc, c); regs3 o = {sc->eax, sc->edx, sc->ecx}; \
        if (body) { failed(sc); return; } \
        sc->eax = o.eax; sc->edx = o.edx; sc->ecx = o.ecx; \
    }
ROW8(shape_beam_draw, beam_draw(&c, A(0), &o))
ROW8(shape_mech_draw, mech_draw(&c, A(0), &o))
ROW8(shape_flyer_draw, flyer_draw(&c, A(0), &o))
ROW8(shape_debris_draw, debris_draw(&c, A(0), &o))
ROW8(shape_meteor_draw, meteor_draw(&c, A(0), &o))
ROW8(shape_config_part, config_part(&c, A(0), &o))
ROW8(shape_config_part_offset, config_part_offset(&c, A(0), &o))
ROW8(explosion_parent_cells, explosion_cells(&c, A(0), &o))
ROW8(explosion_hidden, explosion_hidden(&c, A(0), &o))
#undef ROW8
