/* The R3D view transform, the projection and the lights as own code
 * (es2/sim/view.h). Contract: rasterizer.md, "View transform, projection and
 * lights" and camera.md, "Own code state"
 * (every store and call below with its listing address range); "listing" =
 * llvm-objdump -d -M intel of the reference DBSIM.EXE. Internal callees of the
 * original are own functions; hooks, other packages' services and the heap are
 * called through the dispatch over the bound main loop's call area. Guest state
 * is read and written only through the guest view.
 *
 * Nothing here writes pixels: the module's state is the matrices and
 * constants 0x6c5f94..0x6c604b, the light list 0x6c60a8/0x6c60ac, the
 * light objects on the game heap and the light table 0x4a9608's entries.
 * Where the original faults (an integer division by zero, a NULL object,
 * a full light table) the call is refused with the faulting address. */
#include <stdint.h>
#include <string.h>
#include "es2/sim/view.h"
#include "es2/sim/memory.h"
#include "es2/sim/services.h"

/* ---- guest VAs */
#define VA_COUNTER_POINTER 0x006cbee4u
#define VA_DEVICES 0x006c5eb4u          /* the graphics library's current object per kind */
#define VA_CAMERA_MATRIX 0x006c5f94u    /* the camera pose (0x20 bytes: 9 words, kind, pad, 3 dwords) */
#define VA_VIEW_MATRIX 0x006c5fb4u      /* object matrix x view matrix: what 0x48c57c transforms with */
#define VA_INVERSE_MATRIX 0x006c5fd4u   /* its inverse; its translation 0x6c5fe8..0x6c5ff3 */
#define VA_VIEW_POINTER 0x006c5ff4u     /* the view matrix in use (0x48c3b0's argument or the identity) */
#define VA_OBJECT_POINTER 0x006c5ff8u   /* the object matrix in use (0x48c444's argument or the identity) */
#define VA_IDENTITY 0x006c5ffcu         /* the identity matrix (0x20 bytes) */
#define VA_EYE 0x006c601cu              /* the eye in object space (3 dwords) */
#define VA_FOCAL_SHIFT 0x006c6028u      /* [cam+0x1a]: the perspective shift; 0: divide by the divisor */
#define VA_VIEW_FLAG 0x006c602cu        /* [cam+0x1e] */
#define VA_FOCAL_DIVISOR 0x006c6030u    /* [cam+0x22], taken when the shift is 0 */
#define VA_CENTRE_X 0x006c6034u         /* the screen centre: viewport origin minus the context's +0x220 */
#define VA_CENTRE_Y 0x006c6038u
#define VA_VIEWPORT 0x006c603cu         /* 4 dwords from the context's +0x210 */
#define VA_LIGHT_COUNT 0x006c60a8u      /* the active light list (0x48be50 appends, at most 10) */
#define VA_LIGHT_LIST 0x006c60acu
#define VA_CAMERA_BUILT 0x004a5b0cu     /* byte: the camera matrix's constructor has run */
#define VA_LIGHTS_ON 0x006cbc04u        /* byte: lights are transformed into object space */
#define VA_SCENE_LIGHTS 0x004a960cu     /* the scene's light pointers, word count 0x497ae4 (0x407800) */
#define VA_SCENE_COUNT 0x00497ae4u
#define VA_LIGHT_TABLE 0x004a9608u      /* the light table T of 0x406e44 */
/* vtables */
#define VT_GL_LIGHT 0x004a5b39u         /* the graphics library's light (0x48bc40/0x48bc70) */
#define VT_LIGHT_BASE 0x0049f8d4u
#define VT_SPOT_LIGHT 0x0049f8c0u       /* kind 1: direction only (slot 1 = 0x48bcec) */
#define VT_POINT_LIGHT 0x00497ac8u      /* kind 2: position, range and falloff (slot 1 = 0x48bd6c) */
#define VT_LIGHT_OBJECT 0x00497b8au     /* 0x407604's object: a table entry index at +0xc */
/* callees (dispatch) */
#define VA_NEW 0x0047749cu
#define VA_DELETE 0x00477548u
#define VA_GL_LIGHT_CTOR 0x0048bc40u    /* service */
#define VA_LIGHT_LIST_ADD 0x0048be50u   /* service */
#define VA_MATRIX_APPLY 0x004800c0u     /* service */
#define VA_MATRIX_CTOR 0x00495132u      /* hooks */
#define VA_VECTOR_SUBTRACT 0x00492a30u
#define VA_EULER_MATRIX 0x0047ebb8u
#define VA_MATRIX_COMPOSE 0x0047fa20u
#define VA_MATRIX_TRANSPOSE 0x0047df19u
#define VA_ROTATE_POINT 0x00480304u
#define VA_TRANSFORM_POINT 0x0048043cu
#define VA_MATRIX_POINT 0x004801d4u
#define VA_DIST3D 0x00492aafu
#define VA_VECTOR_ANGLE 0x0049290cu

static es2_sim_view *bound_view, own_view;
void es2_sim_view_bind(es2_sim_view *view) { bound_view = view; }

/* ---- guest view and calls */
typedef struct ctx { es2_sim_view *v; es2_sim_loop *l; const es2_guest_memory *m; } ctx;
static uint32_t r32(const ctx *c, uint32_t a) { return c->m->read32(c->m->context, a); }
static uint16_t r16(const ctx *c, uint32_t a) { return c->m->read16(c->m->context, a); }
static uint8_t r8(const ctx *c, uint32_t a) { return es2_sim_read8(c->m, a); }
static void w32(const ctx *c, uint32_t a, uint32_t v) { c->m->write32(c->m->context, a, v); }
static void w16(const ctx *c, uint32_t a, uint16_t v) { c->m->write16(c->m->context, a, v); }
static void w8(const ctx *c, uint32_t a, uint8_t v) { es2_sim_write8(c->m, a, v); }
static int32_t s32(const ctx *c, uint32_t a) { return (int32_t)r32(c, a); }
static int32_t sw(const ctx *c, uint32_t a) { return (int32_t)(int16_t)r16(c, a); }
static void counter(const ctx *c, int32_t n) {
    uint32_t p = r32(c, VA_COUNTER_POINTER);
    w32(c, p, r32(c, p) + (uint32_t)n);
}
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
static int call_regs(ctx *c, uint32_t va, const uint32_t *slots, unsigned count, const uint32_t regs[3], es2_sim_result *out) {
    es2_sim_loop *l = c->l;
    uint32_t bytes = 4u * (count ? count : 1u), top = l->stack_hi;
    if (top - l->stack_lo < bytes) return refuse(c, va, NULL, "the own code's call area is exhausted");
    l->stack_hi -= bytes;
    for (unsigned i = 0; i < count; ++i) w32(c, l->stack_hi + 4u * i, slots[i]);
    uint32_t zero[3] = {0, 0, 0};
    es2_sim_result r;
    memset(&r, 0, sizeof(r));
    int refused = es2_sim_call(l->dispatch, va, c->m->host(c->m->context, l->stack_hi), count, regs ? regs : zero, &r);
    l->stack_hi = top;
    if (refused) {
        c->v->refused_va = va;
        if (l->outcome == ES2_SIM_LOOP_EXITED) return 1;   /* the process ended inside */
        l->outcome = ES2_SIM_LOOP_REFUSED;
        l->refused_va = va;
        return 1;
    }
    if (out) *out = r;
    return 0;
}
static int call(ctx *c, uint32_t va, const uint32_t *slots, unsigned count, uint32_t *eax) {
    es2_sim_result r;
    if (call_regs(c, va, slots, count, NULL, &r)) return 1;
    if (eax) *eax = r.eax;
    return 0;
}
#define CALL(c, va, eax, ...) call((c), (va), (const uint32_t[]){__VA_ARGS__}, \
    (unsigned)(sizeof((const uint32_t[]){__VA_ARGS__}) / sizeof(uint32_t)), (eax))
/* A virtual call through slot `offset` of the object's vtable. */
static int vcall(ctx *c, uint32_t object, uint32_t offset, const uint32_t *slots, unsigned count, uint32_t *eax) {
    return call(c, r32(c, r32(c, object) + offset), slots, count, eax);
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
/* The original's CDQ; IDIV r/m32: quotient and remainder, or the #DE fault. */
static int sdiv(ctx *c, uint32_t va, const char *name, int32_t a, int32_t b, int32_t *q, int32_t *rem) {
    if (b == 0 || (a == INT32_MIN && b == -1))
        return divide_refuse(c, va, (uint64_t)(int64_t)a, (uint32_t)b, name, "integer division fault (the original raises #DE)");
    *q = a / b;
    if (rem) *rem = a % b;
    return 0;
}
/* The low dword of a product (IMUL r32, r/m32 and the low half of the one-operand IMUL). */
static int32_t mul32(int32_t a, int32_t b) { return (int32_t)((uint32_t)a * (uint32_t)b); }

static int serving(es2_sim_service_call *sc, ctx *c) {
    c->v = bound_view ? bound_view : &own_view;
    if (!c->v->loop) c->v->loop = es2_sim_loop_bound();
    if (!c->v->loop) { sc->refused = 1; sc->reason = "no main loop bound"; return 1; }
    c->l = c->v->loop;
    c->m = c->l->memory;
    ++c->v->calls;
    return 0;
}
static void failed(es2_sim_service_call *sc) { sc->refused = 1; sc->reason = "a call inside the service was refused"; }
#define SERVE(sc, c) ctx c; if (serving((sc), &c)) return
#define A(n) (sc->slots[n])

/* Three dwords in the original's order (+8, +0, +4). */
static void copy3(const ctx *c, uint32_t d, uint32_t s) {
    w32(c, d + 8u, r32(c, s + 8u));
    w32(c, d, r32(c, s));
    w32(c, d + 4u, r32(c, s + 4u));
}
/* The 0x20-byte matrix as 0x48c3b0/0x48c444/0x48d7ec copy it: five dwords,
 * then the translation +0x1c, +0x14, +0x18. */
static void copy_matrix(const ctx *c, uint32_t d, uint32_t s) {
    for (uint32_t i = 0; i < 0x14u; i += 4u) w32(c, d + i, r32(c, s + i));
    w32(c, d + 0x1cu, r32(c, s + 0x1cu));
    w32(c, d + 0x14u, r32(c, s + 0x14u));
    w32(c, d + 0x18u, r32(c, s + 0x18u));
}

/* ======================================================================
 * MATH leaves (listing 0x495157..0x49522f, 0x492a5e..0x492aae,
 * 0x47dce7..0x47dd3e, 0x47d734..0x47d76b). */
/* 0x47dce7(a, b): the dot product of two word vectors (sign-extended). */
static int32_t wdot(const ctx *c, uint32_t a, uint32_t b) {
    uint32_t sum = (uint32_t)mul32(sw(c, a), sw(c, b)) + (uint32_t)mul32(sw(c, a + 2u), sw(c, b + 2u)) +
                   (uint32_t)mul32(sw(c, a + 4u), sw(c, b + 4u));
    return (int32_t)sum;
}
/* 0x47dd1a(a, b): the dot product of two dword vectors (the sum wraps like the 32-bit adds). */
static int32_t ddot(const ctx *c, uint32_t a, uint32_t b) {
    uint32_t sum = (uint32_t)mul32(s32(c, a), s32(c, b)) + (uint32_t)mul32(s32(c, a + 4u), s32(c, b + 4u)) +
                   (uint32_t)mul32(s32(c, a + 8u), s32(c, b + 8u));
    return (int32_t)sum;
}
/* 0x492a5e(a, b): a.z -= b.z, then the hook vector_subtract(a, b) for x and y. EAX a. */
static int vsub(ctx *c, uint32_t a, uint32_t b) {
    w32(c, a + 8u, r32(c, a + 8u) - r32(c, b + 8u));
    return CALL(c, VA_VECTOR_SUBTRACT, NULL, a, b);
}
/* 0x492a7d(v): the hook dist3d_forward(v.x, v.y, v.z). */
static int vlen(ctx *c, uint32_t v, uint32_t *len) { return CALL(c, VA_DIST3D, len, r32(c, v), r32(c, v + 4u), r32(c, v + 8u)); }
/* 0x47d734(v, p): the distance from v to p: a copy of v minus p, its length. */
static int vdist(ctx *c, uint32_t v, uint32_t p, uint32_t *len) {
    uint32_t top = c->l->stack_hi, f = frame_push(c, 0xcu);
    if (!f) return refuse(c, 0x0047d734u, "distance", "the own code's call area is exhausted");
    copy3(c, f, v);
    int bad = vsub(c, f, p) || vlen(c, f, len);
    frame_pop(c, top);
    return bad;
}
/* 0x4951d6(v): negate z, then 0x495201: negate x and y. */
static void vneg2(const ctx *c, uint32_t v) { w32(c, v, 0u - r32(c, v)); w32(c, v + 4u, 0u - r32(c, v + 4u)); }
static void vneg3(const ctx *c, uint32_t v) { w32(c, v + 8u, 0u - r32(c, v + 8u)); vneg2(c, v); }
/* 0x4951a7(M, angles, pos): the translation from pos, the rotation from the
 * hook euler_matrix(angles, M) (0x4951ec). EAX M. */
static int pose(ctx *c, uint32_t m, uint32_t angles, uint32_t p) {
    copy3(c, m + 0x14u, p);
    return CALL(c, VA_EULER_MATRIX, NULL, angles, m);
}

/* ======================================================================
 * The view transform (listing 0x48c2e4..0x48c6f3, 0x48d7ec..0x48d8b0,
 * 0x48d97a..0x48dc51). */
/* 0x48db12(M): the inverse in place: transpose (hook matrix_transpose),
 * negate the translation, rotate it by the transposed matrix into M+0x14
 * (hook rotate_point(copy, M, M+0x14)). EAX M. */
static int invert(ctx *c, uint32_t m) {
    if (CALL(c, VA_MATRIX_TRANSPOSE, NULL, m)) return 1;
    vneg3(c, m + 0x14u);
    uint32_t top = c->l->stack_hi, f = frame_push(c, 0xcu);
    if (!f) return refuse(c, 0x0048db12u, "matrix inverse", "the own code's call area is exhausted");
    copy3(c, f, m + 0x14u);
    int bad = CALL(c, VA_ROTATE_POINT, NULL, f, m, m + 0x14u);
    frame_pop(c, top);
    return bad;
}
/* The tail shared by 0x48c3b0 and 0x48c444: the inverse of the view matrix
 * and the eye (0, 0, 0) through it (hook transform_point) into 0x6c601c. */
static int eye(ctx *c, uint32_t *eax) {
    copy_matrix(c, VA_INVERSE_MATRIX, VA_VIEW_MATRIX);
    if (invert(c, VA_INVERSE_MATRIX)) return 1;
    uint32_t top = c->l->stack_hi, f = frame_push(c, 0xcu);
    if (!f) return refuse(c, 0x0048c3b0u, "eye", "the own code's call area is exhausted");
    int bad = CALL(c, VA_TRANSFORM_POINT, eax, f, VA_INVERSE_MATRIX, VA_EYE);
    frame_pop(c, top);
    return bad;
}
/* 0x48c3b0(M) (0x48c3b0..0x48c441): the view matrix M (NULL: the identity)
 * composed with the object matrix in use (hook matrix_compose(object, view,
 * 0x6c5fb4)); the inverse and the eye. EAX transform_point's. */
static int view_select(ctx *c, uint32_t m, uint32_t *eax) {
    uint32_t p = m ? m : VA_IDENTITY;
    w32(c, VA_VIEW_POINTER, p);
    if (CALL(c, VA_MATRIX_COMPOSE, NULL, r32(c, VA_OBJECT_POINTER), p, VA_VIEW_MATRIX)) return 1;
    return eye(c, eax);
}
/* 0x48bcfc(L, M) (0x48bcfc..0x48bd6a): the light's position through M
 * (hook transform_point) and its direction through M (0x4800c0, word
 * matrix) into the light's transformed copy at L+0x1e. EAX L+0x1e. */
static int light_transform(ctx *c, uint32_t l, uint32_t m) {
    uint32_t top = c->l->stack_hi, f = frame_push(c, 0x14u);
    if (!f) return refuse(c, 0x0048bcfcu, "light transform", "the own code's call area is exhausted");
    f += 0x14u;
    int bad = CALL(c, VA_TRANSFORM_POINT, NULL, l + 4u, m, f - 0x14u) || CALL(c, VA_MATRIX_APPLY, NULL, l + 0x10u, m, f - 8u);
    if (!bad) {
        copy3(c, l + 0x1eu + 4u, f - 0x14u);
        w32(c, l + 0x1eu + 0x10u, r32(c, f - 8u));
        w16(c, l + 0x1eu + 0x14u, r16(c, f - 4u));
    }
    frame_pop(c, top);
    return bad;
}
/* 0x48c444(M) (0x48c444..0x48c57b): the object matrix M (NULL: the
 * identity) composed with the view matrix in use (hook
 * matrix_compose(object, view, 0x6c5fb4)); the inverse and the eye; a local
 * copy of the object matrix inverted and, when lights are on, every active
 * light transformed by it. EAX the last light's L+0x1e, else the local
 * copy's (stack) address. */
static int object_select(ctx *c, uint32_t m, uint32_t *eax) {
    uint32_t p = m ? m : VA_IDENTITY;
    w32(c, VA_OBJECT_POINTER, p);
    if (CALL(c, VA_MATRIX_COMPOSE, NULL, p, r32(c, VA_VIEW_POINTER), VA_VIEW_MATRIX)) return 1;
    if (eye(c, eax)) return 1;
    uint32_t top = c->l->stack_hi, f = frame_push(c, 0x20u);
    if (!f) return refuse(c, 0x0048c444u, "object select", "the own code's call area is exhausted");
    uint32_t o = r32(c, VA_OBJECT_POINTER);
    for (uint32_t i = 0; i < 0x20u; i += 4u) w32(c, f + i, r32(c, o + i));
    int bad = invert(c, f);
    *eax = f;
    if (!bad && r8(c, VA_LIGHTS_ON)) {
        int32_t n = s32(c, VA_LIGHT_COUNT);
        for (int32_t i = 0; i < n && !bad; ++i) {
            uint32_t l = r32(c, VA_LIGHT_LIST + 4u * (uint32_t)i);
            bad = light_transform(c, l, f);
            *eax = l + 0x1eu;
        }
    }
    frame_pop(c, top);
    return bad;
}
/* 0x48c2e4(cam) (0x48c2e4..0x48c3ad): the per-frame view set-up. The camera
 * matrix's constructor (hook matrix_ctor) once (byte 0x4a5b0c); the
 * projection constants from the camera (+0x1a, +0x1e, +0x22 when the
 * shift is 0); the viewport from the camera's context (+0x210, 4 dwords)
 * and the centre = viewport origin minus the context's +0x220 (hook
 * vector_subtract); the camera pose into 0x6c5f94 (0x495178: translation
 * cam+4, hook euler_matrix(cam+0x10, M)); its inverse; 0x48c3b0(0x6c5f94).
 * EAX 0x48c3b0's. */
static int view_setup(ctx *c, uint32_t cam, uint32_t *eax) {
    if (r8(c, VA_CAMERA_BUILT) == 0) {
        if (CALL(c, VA_MATRIX_CTOR, NULL, VA_CAMERA_MATRIX)) return 1;
        w8(c, VA_CAMERA_BUILT, (uint8_t)(r8(c, VA_CAMERA_BUILT) + 1u));
    }
    w32(c, VA_FOCAL_SHIFT, r32(c, cam + 0x1au));
    w32(c, VA_VIEW_FLAG, r32(c, cam + 0x1eu));
    if (r32(c, VA_FOCAL_SHIFT) == 0) w32(c, VA_FOCAL_DIVISOR, r32(c, cam + 0x22u));
    uint32_t context = r32(c, cam + 0x16u);
    for (uint32_t i = 0; i < 0x10u; i += 4u) w32(c, VA_VIEWPORT + i, r32(c, context + 0x210u + i));
    w32(c, VA_CENTRE_X, r32(c, VA_VIEWPORT));
    w32(c, VA_CENTRE_Y, r32(c, VA_VIEWPORT + 4u));
    if (CALL(c, VA_VECTOR_SUBTRACT, NULL, VA_CENTRE_X, context + 0x220u)) return 1;
    if (pose(c, VA_CAMERA_MATRIX, cam + 0x10u, cam + 4u)) return 1;
    if (invert(c, VA_CAMERA_MATRIX)) return 1;
    return view_select(c, VA_CAMERA_MATRIX, eax);
}
/* 0x48dbe1(L, src) (0x48dbe1..0x48dc14): the transformed copy at L+0x1e
 * from a 0x16-byte record: direction dword +0x10 and word +0x14, position
 * +4..+0xf. EAX L+0x22. */
static void light_copy(const ctx *c, uint32_t l, uint32_t src) {
    uint32_t d = l + 0x1eu;
    w32(c, d + 0x10u, r32(c, src + 0x10u));
    w16(c, d + 0x14u, r16(c, src + 0x14u));
    copy3(c, d + 4u, src + 4u);
}
/* 0x48d7ec(P) (0x48d7ec..0x48d8b0): a saved view P: the pointers from
 * P+0x20/+0x24, the view matrix from P+0..0x1f (as copy_matrix), its copy
 * and inverse into 0x6c5fd4, the eye from P+0x28..0x33, and when lights are
 * on the saved copies P+0x34 + 0x16 i (i < the active count), all of them
 * into the first active light. EAX that light's L+0x22, else [P+0x2c]. */
static int view_load(ctx *c, uint32_t p, uint32_t *eax) {
    w32(c, VA_OBJECT_POINTER, r32(c, p + 0x20u));
    w32(c, VA_VIEW_POINTER, r32(c, p + 0x24u));
    copy_matrix(c, VA_VIEW_MATRIX, p);
    copy_matrix(c, VA_INVERSE_MATRIX, VA_VIEW_MATRIX);
    if (invert(c, VA_INVERSE_MATRIX)) return 1;
    w32(c, VA_EYE + 8u, r32(c, p + 0x30u));
    w32(c, VA_EYE, r32(c, p + 0x28u));
    w32(c, VA_EYE + 4u, r32(c, p + 0x2cu));
    *eax = r32(c, p + 0x2cu);
    if (r8(c, VA_LIGHTS_ON)) {
        /* The original (0x48d884..0x48d8aa) never advances its list pointer:
         * every saved copy lands in the FIRST active light (in vitro:
         * differential_view_test, case 2 of the view part). */
        int32_t n = s32(c, VA_LIGHT_COUNT);
        uint32_t l = r32(c, VA_LIGHT_LIST);
        for (int32_t i = 0; i < n; ++i) {
            light_copy(c, l, p + 0x34u + 0x16u * (uint32_t)i);
            *eax = l + 0x22u;
        }
    }
    return 0;
}
/* 0x48c5cc(size, depth) (0x48c5cc..0x48c5fb): the projected size: shift 0:
 * (size << 8) / divisor; else (size << shift) / depth. EAX the quotient,
 * EDX the remainder. */
static int project_size(ctx *c, int32_t size, int32_t depth, int32_t *q, int32_t *rem, uint32_t *ecx) {
    uint32_t f = r32(c, VA_FOCAL_SHIFT);
    if (f == 0) return sdiv(c, 0x0048c5e2u, "projected size", (int32_t)((uint32_t)size << 8), s32(c, VA_FOCAL_DIVISOR), q, rem);
    *ecx = f;
    return sdiv(c, 0x0048c5f6u, "projected size", (int32_t)((uint32_t)size << (f & 31u)), depth, q, rem);
}
/* 0x48c5fc(v, s) (0x48c5fc..0x48c657): the point v (x, depth, z) onto the
 * screen: shift 0: s = ((x << 8) / divisor, (z << 8) / divisor); else
 * ((x << shift) / depth, (z << shift) / depth). EAX s. */
static int project_point(ctx *c, uint32_t v, uint32_t s) {
    uint32_t f = r32(c, VA_FOCAL_SHIFT);
    int32_t q;
    if (f == 0) {
        int32_t d = s32(c, VA_FOCAL_DIVISOR);
        if (sdiv(c, 0x0048c616u, "projection", (int32_t)(r32(c, v) << 8), d, &q, NULL)) return 1;
        w32(c, s, (uint32_t)q);
        if (sdiv(c, 0x0048c625u, "projection", (int32_t)(r32(c, v + 8u) << 8), d, &q, NULL)) return 1;
        w32(c, s + 4u, (uint32_t)q);
        return 0;
    }
    if (sdiv(c, 0x0048c63bu, "projection", (int32_t)(r32(c, v) << (f & 31u)), s32(c, v + 4u), &q, NULL)) return 1;
    w32(c, s, (uint32_t)q);
    if (sdiv(c, 0x0048c64cu, "projection", (int32_t)(r32(c, v + 8u) << (f & 31u)), s32(c, v + 4u), &q, NULL)) return 1;
    w32(c, s + 4u, (uint32_t)q);
    return 0;
}

/* ======================================================================
 * The light objects and the active list (listing 0x48bc70..0x48bd82,
 * 0x48be98..0x48bed5, 0x48bfe8..0x48c2e2, 0x48d94c..0x48d979). */
/* 0x48bc70(L, pos, dir): the light with a position (3 dwords) and a word
 * direction (dword + word) after 0x48bc40's shape; counter + 1. EAX L. */
static void light_ctor(const ctx *c, uint32_t l, uint32_t p, uint32_t dir) {
    w32(c, l, VT_GL_LIGHT);
    copy3(c, l + 4u, p);
    w32(c, l + 0x10u, r32(c, dir));
    w16(c, l + 0x14u, r16(c, dir + 4u));
    counter(c, 1);
}
/* 0x48bcc4(L, flags): counter - 1; the base vtable back; flags bit 0: delete. */
static int light_dtor(ctx *c, uint32_t l, uint32_t flags, uint32_t *eax) {
    counter(c, -1);
    *eax = l;
    if (!l) return 0;
    w32(c, l, VT_GL_LIGHT);
    if (flags & 1u) return CALL(c, VA_DELETE, eax, l);
    return 0;
}
/* 0x48be98(L): remove L from the active list: the last entry moves into
 * its slot. Found: AL 1 (EAX the slot's address with its low byte 1), EDX
 * the moved entry, ECX the new count; else EAX 0. */
static int light_list_remove(const ctx *c, uint32_t l, uint32_t *eax, uint32_t *edx, uint32_t *ecx) {
    int32_t n = s32(c, VA_LIGHT_COUNT);
    for (int32_t i = 0; i < n; ++i) {
        uint32_t slot = VA_LIGHT_LIST + 4u * (uint32_t)i;
        if (r32(c, slot) != l) continue;
        w32(c, VA_LIGHT_COUNT, (uint32_t)(n - 1));
        uint32_t moved = r32(c, VA_LIGHT_LIST + 4u * (uint32_t)(n - 1));
        w32(c, slot, moved);
        *eax = (slot & 0xffffff00u) | 1u;
        *edx = moved;
        *ecx = (uint32_t)(n - 1);
        return 1;
    }
    *eax = 0;
    *edx = n > 0 ? 0u : (uint32_t)n;
    *ecx = l;
    return 0;
}
/* 0x48bfe8(n, p) (polygon) and 0x48c16c(n, p) (terrain): the light sum of
 * a face with the word normal n at the word position p (polygon) or the
 * dword position p (terrain) over the active lights, clamped to 0xff:
 *   kind 0  + intensity [L+0x1a];
 *   kind 1  d = n . transformed direction (L+0x2e, words); polygon: d =
 *           (d - 0x400000) >> 1; d < 0: - (intensity * d) >> 22;
 *   kind 2  v = transformed position (L+0x22) - p; w = n as dwords; dot =
 *           w . v; dot > 0: len = |v| (hook dist3d_forward; terrain takes
 *           it before the dot), dot /= len + 1, dot *= [L+0x38], dot /=
 *           len + [L+0x34], + (intensity * dot) >> 11.
 * EAX the sum. */
static int lighting(ctx *c, uint32_t n, uint32_t p, int terrain, uint32_t *eax) {
    int32_t sum = 0, count = s32(c, VA_LIGHT_COUNT);
    uint32_t top = c->l->stack_hi, f = frame_push(c, 0x18u);
    if (!f) return refuse(c, terrain ? 0x0048c16cu : 0x0048bfe8u, "lighting", "the own code's call area is exhausted");
    uint32_t v = f, w = f + 0xcu;
    int bad = 0;
    for (int32_t i = 0; i < count && !bad; ++i) {
        uint32_t l = r32(c, VA_LIGHT_LIST + 4u * (uint32_t)i), kind = r32(c, l + 0x16u);
        int32_t intensity = s32(c, l + 0x1au);
        if (kind == 0) {
            sum += intensity;
        } else if (kind == 1) {
            int32_t d = wdot(c, n, l + 0x2eu);
            if (!terrain) d = (int32_t)((uint32_t)d + 0xffc00000u) >> 1;
            if (d < 0) sum -= mul32(intensity, d) >> 22;
        } else if (kind == 2) {
            copy3(c, v, l + 0x22u);
            uint32_t len = 0;
            int32_t dot, q;
            if (terrain) {
                if ((bad = vsub(c, v, p) || vlen(c, v, &len))) break;
            } else {
                w32(c, v + 8u, r32(c, v + 8u) - (uint32_t)sw(c, p + 4u));
                w32(c, v, r32(c, v) - (uint32_t)sw(c, p));
                w32(c, v + 4u, r32(c, v + 4u) - (uint32_t)sw(c, p + 2u));
            }
            w32(c, w + 8u, (uint32_t)sw(c, n + 4u));
            w32(c, w, (uint32_t)sw(c, n));
            w32(c, w + 4u, (uint32_t)sw(c, n + 2u));
            dot = ddot(c, w, v);
            if (dot <= 0) continue;
            if (!terrain && (bad = vlen(c, v, &len))) break;
            if ((bad = sdiv(c, terrain ? 0x0048c281u : 0x0048c10au, "lighting", dot, (int32_t)len + 1, &q, NULL))) break;
            dot = mul32(s32(c, l + 0x38u), q);
            if ((bad = sdiv(c, terrain ? 0x0048c2aau : 0x0048c133u, "lighting", dot, s32(c, l + 0x34u) + (int32_t)len, &q, NULL))) break;
            sum += mul32(intensity, q) >> 11;
        }
    }
    frame_pop(c, top);
    if (bad) return 1;
    if (sum > 0xff) sum = 0xff;
    *eax = (uint32_t)sum;
    return 0;
}

/* ======================================================================
 * The light table T (0x406e44; entries of 0x23 bytes at T+0x6c, 20 of
 * them; the active entries' pointers at T+0x1c, count T+0x18; pools of
 * spot lights T+0x35c/T+0x360 and point lights T+0x3b0/T+0x3b4). Listing
 * 0x406ee4..0x407097, 0x407098..0x407280, 0x40735c..0x407384,
 * 0x407388..0x407603, 0x407604..0x4076e3, 0x407878..0x40789d. */
static uint32_t entry(const ctx *c, uint32_t t, uint32_t index) { (void)c; return t + 0x6cu + 0x23u * (uint32_t)(int32_t)(int16_t)index; }
/* 0x40735c(T, E): the entry's cut-off distance [E+0x17] = (word [E+0xd] *
 * ([E+0x13] << 5)) / 10 + ([E+0xf] << 5) (the low dword of the product,
 * the quotient truncated toward zero). EAX it, EDX [E+0xf] << 5, ECX E. */
static void entry_range(const ctx *c, uint32_t e, uint32_t *eax, uint32_t *edx) {
    int32_t a = mul32(sw(c, e + 0xdu), (int32_t)(r32(c, e + 0x13u) << 5));
    uint32_t base = r32(c, e + 0xfu) << 5;
    uint32_t r = (uint32_t)(a / 10) + base;
    w32(c, e + 0x17u, r);
    *eax = r;
    *edx = base;
}
/* 0x407500(T, L): L leaves the active list and returns to its kind's pool. */
static void light_release(const ctx *c, uint32_t t, uint32_t l) {
    uint32_t eax, edx, ecx;
    if (!l) return;
    light_list_remove(c, l, &eax, &edx, &ecx);
    if (r32(c, l + 0x16u) == 1u) {
        uint32_t n = r32(c, t + 0x35cu);
        w32(c, t + 0x35cu, n + 1u);
        w32(c, t + 0x360u + 4u * n, l);
    } else {
        uint32_t n = r32(c, t + 0x3b0u);
        w32(c, t + 0x3b0u, n + 1u);
        w32(c, t + 0x3b4u + 4u * n, l);
    }
}
/* 0x407388(T) (spot, 0x34 bytes, vtable 0x49f8c0, kind 1) and 0x407444(T)
 * (point, 0x40 bytes, vtable 0x497ac8, kind 2): a light from the kind's
 * pool, else new + 0x48bc40 twice (L and L+0x1e), counter + 1 + 1 - 4;
 * appended to the active list (0x48be50). EAX L (NULL when new fails: the
 * original appends NULL). The frame record 0x49630c of the original is
 * not linked by own code. */
static int light_new(ctx *c, uint32_t t, int point, uint32_t *eax) {
    uint32_t pool = point ? 0x3b0u : 0x35cu, l;
    uint32_t n = r32(c, t + pool);
    if (n) {
        w32(c, t + pool, n - 1u);
        l = r32(c, t + pool + 4u + 4u * (n - 1u));
    } else {
        if (CALL(c, VA_NEW, &l, point ? 0x40u : 0x34u)) return 1;
        if (l) {
            if (CALL(c, VA_GL_LIGHT_CTOR, NULL, l)) return 1;
            w32(c, l, VT_LIGHT_BASE);
            if (CALL(c, VA_GL_LIGHT_CTOR, NULL, l + 0x1eu)) return 1;
            counter(c, 1);
            w32(c, l, point ? VT_POINT_LIGHT : VT_SPOT_LIGHT);
            w32(c, l + 0x16u, point ? 2u : 1u);
            counter(c, 1);
            counter(c, -4);
        }
    }
    if (CALL(c, VA_LIGHT_LIST_ADD, NULL, l)) return 1;
    *eax = l;
    return 0;
}
/* 0x407098(T, obj) (0x407098..0x407280): the per-object lights. T's own
 * light T+0x328 leaves the active list; for every active entry E with a
 * nonzero level [E+0x1b]: d = |obj.pos - E.pos| (0x47d734); d >= [E+0x17]:
 * release; else s = d >> 5 and, when s != 0 and the word vector_angle(s,
 * [obj+0xc] >> 5) < 0x1f40 (hook), a spot light: level = min(([E+0x13] <<
 * 8) / (s + [E+0xf]), 0xff), the entry's light replaced unless it is a
 * spot, slot 1 (L, (([E+0x1b] * 0x100) >> 8) * level >> 8), direction =
 * ((obj - E.pos) << 11) / max(d, 1) per axis as words into L+0x10/+0x14;
 * else a point light: slot 1 (L, ([E+0x1b] * 0x100) >> 8), [L+0x34] =
 * [E+0xf] << 5, [L+0x38] = [E+0x13] << 5, [L+0x3c] = [L+0x1a] * [L+0x38],
 * position = E.pos. EAX: unspecified (the last leaf's). */
static int light_update(ctx *c, uint32_t t, uint32_t obj) {
    int32_t heading = s32(c, obj + 0xcu) >> 5;
    uint32_t eax, edx, ecx;
    light_list_remove(c, t + 0x328u, &eax, &edx, &ecx);
    for (uint32_t i = 0; (int32_t)i < s32(c, t + 0x18u); ++i) {
        uint32_t e = r32(c, t + 0x1cu + 4u * i);
        if (r32(c, e + 0x1bu) == 0) continue;
        uint32_t d = 0;
        if (vdist(c, obj, e + 1u, &d)) return 1;
        if ((int32_t)d >= s32(c, e + 0x17u)) {
            light_release(c, t, r32(c, e + 0x1fu));
            w32(c, e + 0x1fu, 0);
            continue;
        }
        int32_t s = (int32_t)d >> 5;
        uint32_t angle = 0;
        if (s != 0 && CALL(c, VA_VECTOR_ANGLE, &angle, (uint32_t)s, (uint32_t)heading)) return 1;
        uint32_t l = r32(c, e + 0x1fu);
        int32_t level = mul32(s32(c, e + 0x1bu), 0x100) >> 8;
        if (s != 0 && (int16_t)angle < 0x1f40) {
            int32_t q;
            if (sdiv(c, 0x0040712fu, "spot light", (int32_t)(r32(c, e + 0x13u) << 8), s + s32(c, e + 0xfu), &q, NULL)) return 1;
            if (q > 0xff) q = 0xff;
            if (!l || r32(c, l + 0x16u) != 1u) {
                light_release(c, t, l);
                if (light_new(c, t, 0, &l)) return 1;
                w32(c, e + 0x1fu, l);
            }
            level = mul32(level, q) >> 8;
            if (!l) return null_refuse(c, 0x0040717au, "spot light", "the light is NULL (the original calls through NULL)");
            if (vcall(c, l, 4u, (const uint32_t[]){l, (uint32_t)level}, 2, NULL)) return 1;
            int32_t dd = (int32_t)d ? (int32_t)d : 1;
            uint16_t dir[3];
            for (unsigned k = 0; k < 3u; ++k) {
                int32_t delta = (int32_t)((r32(c, obj + 4u * k) - r32(c, e + 1u + 4u * k)) << 11);
                if (sdiv(c, 0x00407197u + 0x13u * k, "spot light", delta, dd, &q, NULL)) return 1;
                dir[k] = (uint16_t)q;
            }
            w32(c, l + 0x10u, (uint32_t)dir[0] | ((uint32_t)dir[1] << 16));
            w16(c, l + 0x14u, dir[2]);
        } else {
            if (!l || r32(c, l + 0x16u) != 2u) {
                light_release(c, t, l);
                if (light_new(c, t, 1, &l)) return 1;
                w32(c, e + 0x1fu, l);
            }
            if (!l) return null_refuse(c, 0x00407210u, "point light", "the light is NULL (the original calls through NULL)");
            if (vcall(c, l, 4u, (const uint32_t[]){l, (uint32_t)level}, 2, NULL)) return 1;
            uint32_t range = r32(c, e + 0x13u) << 5, inner = r32(c, e + 0xfu) << 5;
            w32(c, l + 0x34u, inner);
            w32(c, l + 0x38u, range);
            w32(c, l + 0x3cu, (uint32_t)mul32(s32(c, l + 0x1au), (int32_t)range));
            copy3(c, l + 4u, e + 1u);
        }
    }
    return 0;
}
/* 0x406f38(T, pos): the first free entry (byte [E] nonzero) becomes active:
 * position from pos, word level 0xff, ranges from T+0x10/T+0x14, appended
 * to T+0x1c, 0x40735c. EAX its index. A full table (the original writes
 * through a stale register) is refused. */
static int entry_alloc(ctx *c, uint32_t t, uint32_t p, uint32_t *eax) {
    uint32_t i = 0, e = 0;
    for (; i < 20u; ++i) {
        e = entry(c, t, i);
        if (r8(c, e)) break;
    }
    if (i == 20u) return refuse(c, 0x00406f69u, "light entry", "the light table is full (the original writes through a stale pointer)");
    w8(c, e, 0);
    copy3(c, e + 1u, p);
    w16(c, e + 0xdu, 0xffu);
    w32(c, e + 0xfu, r32(c, t + 0x10u));
    w32(c, e + 0x13u, r32(c, t + 0x14u));
    uint32_t n = r32(c, t + 0x18u);
    w32(c, t + 0x18u, n + 1u);
    w32(c, t + 0x1cu + 4u * n, e);
    uint32_t r, rem;
    entry_range(c, e, &r, &rem);
    *eax = i;
    return 0;
}
/* 0x406fbc(T, index): the entry is free again (byte [E] 1) and, when it is
 * in T+0x1c, the last pointer moves into its slot, its light is released
 * and [E+0x1f] cleared (EAX 0); else EAX T. */
static void entry_free(const ctx *c, uint32_t t, uint32_t index, uint32_t *eax) {
    uint32_t e = entry(c, t, index);
    w8(c, e, 1);
    *eax = t;
    for (uint32_t i = 0; (int32_t)i < s32(c, t + 0x18u); ++i) {
        if (r32(c, t + 0x1cu + 4u * i) != e) continue;
        uint32_t n = r32(c, t + 0x18u) - 1u;
        w32(c, t + 0x18u, n);
        w32(c, t + 0x1cu + 4u * i, r32(c, t + 0x1cu + 4u * n));
        light_release(c, t, r32(c, e + 0x1fu));
        w32(c, e + 0x1fu, 0);
        *eax = 0;
        return;
    }
}
/* 0x407048(T, index, level): word [E+0xd] and dword [E+0x1b] = the level
 * (low word, sign-extended), 0x40735c. */
static void entry_level(const ctx *c, uint32_t t, uint32_t index, uint32_t level, uint32_t *eax, uint32_t *edx) {
    uint32_t e = entry(c, t, index);
    w16(c, e + 0xdu, (uint16_t)level);
    w32(c, e + 0x1bu, (uint32_t)(int32_t)(int16_t)level);
    entry_range(c, e, eax, edx);
}

/* ======================================================================
 * Service rows (sim/dispatch/services/view_transform.h). */
void es2_sim_service_view_setup(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t eax = 0;
    if (view_setup(&c, A(0), &eax)) { failed(sc); return; }
    sc->eax = eax;
}
void es2_sim_service_view_select(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t eax = 0;
    if (view_select(&c, A(0), &eax)) { failed(sc); return; }
    sc->eax = eax;
}
void es2_sim_service_object_select(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t eax = 0;
    if (object_select(&c, A(0), &eax)) { failed(sc); return; }
    sc->eax = eax;
}
/* 0x48c57c(p, v): p through the view matrix into v (hook transform_point). */
void es2_sim_service_view_transform(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t eax = 0;
    if (CALL(&c, VA_TRANSFORM_POINT, &eax, A(0), VA_VIEW_MATRIX, A(1))) { failed(sc); return; }
    sc->eax = eax;
}
void es2_sim_service_project_size(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    int32_t q = 0, rem = 0;
    if (project_size(&c, (int32_t)A(0), (int32_t)A(1), &q, &rem, &sc->ecx)) { failed(sc); return; }
    sc->eax = (uint32_t)q; sc->edx = (uint32_t)rem;
}
void es2_sim_service_project_point(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    if (project_point(&c, A(0), A(1))) { failed(sc); return; }
    sc->eax = A(1);
}
/* 0x48c6d0(s, t): t = (s.x + centre.x, centre.y - s.y). EAX t, EDX s, ECX t.y. */
void es2_sim_service_to_screen(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t s = A(0), t = A(1);
    w32(&c, t, r32(&c, s) + r32(&c, VA_CENTRE_X));
    uint32_t y = r32(&c, VA_CENTRE_Y) - r32(&c, s + 4u);
    w32(&c, t + 4u, y);
    sc->eax = t; sc->edx = s; sc->ecx = y;
}
void es2_sim_service_view_load(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t eax = 0;
    if (view_load(&c, A(0), &eax)) { failed(sc); return; }
    sc->eax = eax;
}
void es2_sim_service_matrix_invert(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    if (invert(&c, A(0))) { failed(sc); return; }
    sc->eax = A(0);
}
/* 0x48db57(a, b, out) -> matrix_compose; 0x48dc43(M) -> matrix_transpose;
 * 0x48d9e4(p, M, out) -> transform_point; 0x48db93(p, M, out) ->
 * matrix_point; 0x48da08(v, M, out) -> 0x4800c0: the hooks' EAX. */
void es2_sim_service_matrix_compose_48db57(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t eax = 0;
    if (CALL(&c, VA_MATRIX_COMPOSE, &eax, A(0), A(1), A(2))) { failed(sc); return; }
    sc->eax = eax;
}
void es2_sim_service_matrix_transpose_48dc43(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t eax = 0;
    if (CALL(&c, VA_MATRIX_TRANSPOSE, &eax, A(0))) { failed(sc); return; }
    sc->eax = eax;
}
void es2_sim_service_transform_point_48d9e4(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t eax = 0;
    if (CALL(&c, VA_TRANSFORM_POINT, &eax, A(0), A(1), A(2))) { failed(sc); return; }
    sc->eax = eax;
}
void es2_sim_service_matrix_point_48db93(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t eax = 0;
    if (CALL(&c, VA_MATRIX_POINT, &eax, A(0), A(1), A(2))) { failed(sc); return; }
    sc->eax = eax;
}
void es2_sim_service_matrix_apply_48da08(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t eax = 0;
    if (CALL(&c, VA_MATRIX_APPLY, &eax, A(0), A(1), A(2))) { failed(sc); return; }
    sc->eax = eax;
}
/* 0x48d97a(a, b) and 0x47dd1a: the dword dot product; 0x48daa7 and
 * 0x47dce7: the word dot product. */
void es2_sim_service_dot_dwords(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    sc->eax = (uint32_t)ddot(&c, A(0), A(1));
}
void es2_sim_service_dot_words(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    sc->eax = (uint32_t)wdot(&c, A(0), A(1));
}
/* 0x48d98f(k): the current object of kind k; 0x48d99e: kind 7 (the surface). */
void es2_sim_service_device_of_kind(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    sc->eax = r32(&c, VA_DEVICES + 4u * A(0));
}
void es2_sim_service_device_surface(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    sc->eax = r32(&c, VA_DEVICES + 4u * 7u);
}
/* 0x48d9a7(v, x, y): v = (x, y). EAX v, EDX x, ECX y. 0x48db70(v, x, y, z):
 * then v.z = z; EAX v. */
void es2_sim_service_vector2_set(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    w32(&c, A(0), A(1));
    w32(&c, A(0) + 4u, A(2));
    sc->eax = A(0); sc->edx = A(1); sc->ecx = A(2);
}
void es2_sim_service_vector3_set(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    w32(&c, A(0), A(1));
    w32(&c, A(0) + 4u, A(2));
    w32(&c, A(0) + 8u, A(3));
    sc->eax = A(0); sc->edx = A(1); sc->ecx = A(3);
}
/* 0x48d9cf(a, b): a.x += b.x, a.y += b.y. EAX a, EDX b.y, ECX b.x. */
void es2_sim_service_vector2_add(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t bx = r32(&c, A(1)), by = r32(&c, A(1) + 4u);
    w32(&c, A(0), r32(&c, A(0)) + bx);
    w32(&c, A(0) + 4u, r32(&c, A(0) + 4u) + by);
    sc->eax = A(0); sc->edx = by; sc->ecx = bx;
}
/* 0x48dabc(v, p): v -= p (p words: z, then x and y by 0x48dc15); EAX v.
 * 0x48dc15(a, p): a.x -= p.x, a.y -= p.y (words). */
void es2_sim_service_vector_sub_words(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t v = A(0), p = A(1);
    w32(&c, v + 8u, r32(&c, v + 8u) - (uint32_t)sw(&c, p + 4u));
    w32(&c, v, r32(&c, v) - (uint32_t)sw(&c, p));
    w32(&c, v + 4u, r32(&c, v + 4u) - (uint32_t)sw(&c, p + 2u));
    sc->eax = v;
}
void es2_sim_service_vector2_sub_words(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t v = A(0), p = A(1);
    w32(&c, v, r32(&c, v) - (uint32_t)sw(&c, p));
    w32(&c, v + 4u, r32(&c, v + 4u) - (uint32_t)sw(&c, p + 2u));
    sc->eax = v; sc->edx = (uint32_t)sw(&c, p + 2u); sc->ecx = (uint32_t)sw(&c, p);
}
/* 0x48dadc(w, n): w = n (words to dwords: z, then x and y by 0x48dc2c); EAX w. */
void es2_sim_service_vector_from_words(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t w = A(0), n = A(1);
    w32(&c, w + 8u, (uint32_t)sw(&c, n + 4u));
    w32(&c, w, (uint32_t)sw(&c, n));
    w32(&c, w + 4u, (uint32_t)sw(&c, n + 2u));
    sc->eax = w;
}
void es2_sim_service_vector2_from_words(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t w = A(0), n = A(1);
    w32(&c, w, (uint32_t)sw(&c, n));
    w32(&c, w + 4u, (uint32_t)sw(&c, n + 2u));
    sc->eax = w; sc->edx = (uint32_t)sw(&c, n + 2u); sc->ecx = (uint32_t)sw(&c, n);
}
/* The light object's accessors: 0x48d9fd L+0x10 (direction), 0x48da57
 * [L+0x16] (kind), 0x48da62 [L+0x1a] (intensity), 0x48da6d L+0x22
 * (transformed position), 0x48da7f [L+0x38], 0x48da8a [L+0x34], 0x48da95
 * L+0x2e (transformed direction), 0x48dafc [L+0x1e], 0x48db07 [L+0x22],
 * 0x48dbd6 L+0x1e; 0x495157 o+4, 0x495162 [cam+0x1a], 0x49516d [cam+0x16]. */
#define ACCESSOR(name, expr) \
    void es2_sim_service_##name(es2_sim_service_call *sc, void *user) { (void)user; SERVE(sc, c); sc->eax = (expr); }
ACCESSOR(light_direction, A(0) + 0x10u)
ACCESSOR(light_kind, r32(&c, A(0) + 0x16u))
ACCESSOR(light_level, r32(&c, A(0) + 0x1au))
ACCESSOR(light_view_position, A(0) + 0x22u)
ACCESSOR(light_range, r32(&c, A(0) + 0x38u))
ACCESSOR(light_inner, r32(&c, A(0) + 0x34u))
ACCESSOR(light_view_direction, A(0) + 0x2eu)
ACCESSOR(light_view_vtable, r32(&c, A(0) + 0x1eu))
ACCESSOR(light_view_x, r32(&c, A(0) + 0x22u))
ACCESSOR(light_view, A(0) + 0x1eu)
ACCESSOR(object_plus_4, A(0) + 4u)
ACCESSOR(camera_shift, r32(&c, A(0) + 0x1au))
ACCESSOR(camera_context, r32(&c, A(0) + 0x16u))
#undef ACCESSOR
/* 0x48da21(L, v): the position L+4 = v; EAX L+4. 0x48da3f(L, w): the
 * direction [L+0x10] = dword w, word [L+0x14] = word w+4; EAX L. */
void es2_sim_service_light_set_position(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    copy3(&c, A(0) + 4u, A(1));
    sc->eax = A(0) + 4u;
}
void es2_sim_service_light_set_direction(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    w32(&c, A(0) + 0x10u, r32(&c, A(1)));
    w16(&c, A(0) + 0x14u, r16(&c, A(1) + 4u));
    sc->eax = A(0);
}
void es2_sim_service_light_view_copy(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    light_copy(&c, A(0), A(1));
    sc->eax = A(0) + 0x22u;
}
void es2_sim_service_light_ctor_48bc70(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    light_ctor(&c, A(0), A(1), A(2));
    sc->eax = A(0);
}
void es2_sim_service_light_dtor_48bcc4(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t eax = 0;
    if (light_dtor(&c, A(0), A(1), &eax)) { failed(sc); return; }
    sc->eax = eax;
}
/* 0x48bd6c(L, v): the point light's level: [L+0x1a] = v, [L+0x3c] = [L+0x38] * v. */
void es2_sim_service_point_light_level(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t p = (uint32_t)mul32(s32(&c, A(0) + 0x38u), (int32_t)A(1));
    w32(&c, A(0) + 0x1au, A(1));
    w32(&c, A(0) + 0x3cu, p);
    sc->eax = A(0); sc->edx = A(1); sc->ecx = p;
}
void es2_sim_service_light_transform_48bcfc(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    if (light_transform(&c, A(0), A(1))) { failed(sc); return; }
    sc->eax = A(0) + 0x1eu;
}
void es2_sim_service_light_list_remove(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    light_list_remove(&c, A(0), &sc->eax, &sc->edx, &sc->ecx);
}
void es2_sim_service_polygon_lighting(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t eax = 0;
    if (lighting(&c, A(0), A(1), 0, &eax)) { failed(sc); return; }
    sc->eax = eax;
}
void es2_sim_service_terrain_lighting(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t eax = 0;
    if (lighting(&c, A(0), A(1), 1, &eax)) { failed(sc); return; }
    sc->eax = eax;
}
/* 0x48d94c(L, flags) (counter - 1, 0x48bcc4(L, 0), flags bit 0: delete) has
 * no static reference in the image (table_test): no row.
 * 0x4075c1(L, flags): the point light's destructor: counter - 2,
 * 0x48bcc4(L+0x1e, 2), 0x48bcc4(L, 0), flags bit 0: delete. */
void es2_sim_service_point_light_dtor(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t l = A(0), eax = l;
    if (l) {
        counter(&c, -2);
        if (light_dtor(&c, l + 0x1eu, 2, &eax) || light_dtor(&c, l, 0, &eax)) { failed(sc); return; }
        if ((A(1) & 1u) && CALL(&c, VA_DELETE, &eax, l)) { failed(sc); return; }
    }
    sc->eax = eax;
}
/* MATH leaves as rows. */
void es2_sim_service_vector_negate3(es2_sim_service_call *sc, void *user) { (void)user; SERVE(sc, c); vneg3(&c, A(0)); sc->eax = A(0); }
void es2_sim_service_vector_negate2(es2_sim_service_call *sc, void *user) { (void)user; SERVE(sc, c); vneg2(&c, A(0)); sc->eax = A(0); }
void es2_sim_service_vector_subtract_492a5e(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    if (vsub(&c, A(0), A(1))) { failed(sc); return; }
    sc->eax = A(0);
}
void es2_sim_service_vector_length(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t eax = 0;
    if (vlen(&c, A(0), &eax)) { failed(sc); return; }
    sc->eax = eax;
}
void es2_sim_service_vector_distance(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t eax = 0;
    if (vdist(&c, A(0), A(1), &eax)) { failed(sc); return; }
    sc->eax = eax;
}
/* 0x495178(cam, M) -> 0x4951a7(M, cam+0x10, cam+4); 0x4951ec(M, angles) ->
 * euler_matrix(angles, M). EAX M. */
void es2_sim_service_camera_pose(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    if (pose(&c, A(1), A(0) + 0x10u, A(0) + 4u)) { failed(sc); return; }
    sc->eax = A(1);
}
void es2_sim_service_pose_4951a7(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    if (pose(&c, A(0), A(1), A(2))) { failed(sc); return; }
    sc->eax = A(0);
}
void es2_sim_service_euler_4951ec(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    if (CALL(&c, VA_EULER_MATRIX, NULL, A(1), A(0))) { failed(sc); return; }
    sc->eax = A(0);
}
/* The light table. */
/* 0x406ee4(T, a, b): the default ranges [T+0x10] = a >> 5, [T+0x14] = b >> 5. */
void es2_sim_service_light_table_ranges(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t a = (uint32_t)((int32_t)A(1) >> 5), b = (uint32_t)((int32_t)A(2) >> 5);
    w32(&c, A(0) + 0x10u, a);
    w32(&c, A(0) + 0x14u, b);
    sc->eax = A(0); sc->edx = a; sc->ecx = b;
}
/* 0x406f00(T, index, a, b) (the entry's ranges [E+0xf] = a >> 5, [E+0x13] =
 * b >> 5, 0x40735c) has no static reference in the image: no row. */
void es2_sim_service_light_entry_alloc(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t eax = 0;
    if (entry_alloc(&c, A(0), A(1), &eax)) { failed(sc); return; }
    sc->eax = eax;
}
void es2_sim_service_light_entry_free(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    entry_free(&c, A(0), A(1), &sc->eax);
}
/* 0x407018(T, index, pos): the entry's position E+1 = pos. EAX E+1. */
void es2_sim_service_light_entry_position(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t e = entry(&c, A(0), A(1));
    copy3(&c, e + 1u, A(2));
    sc->eax = e + 1u;
}
void es2_sim_service_light_entry_level(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    entry_level(&c, A(0), A(1), A(2), &sc->eax, &sc->edx);
}
void es2_sim_service_light_entry_range(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    entry_range(&c, A(1), &sc->eax, &sc->edx);
    sc->ecx = A(1);
}
void es2_sim_service_light_update(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    if (light_update(&c, A(0), A(1))) { failed(sc); return; }
}
void es2_sim_service_spot_light_new(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t eax = 0;
    if (light_new(&c, A(0), 0, &eax)) { failed(sc); return; }
    sc->eax = eax;
}
void es2_sim_service_point_light_new(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t eax = 0;
    if (light_new(&c, A(0), 1, &eax)) { failed(sc); return; }
    sc->eax = eax;
}
void es2_sim_service_light_release(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    light_release(&c, A(0), A(1));
}
/* 0x407604(obj, pos, level), the light-carrying object's constructor, and
 * 0x4076a0(obj, level) are the core hooks light_create and light_level
 * (vtables 0x4a14a4/0x497b8a); their callees 0x406f38 and 0x407048 are
 * rows above. 0x40707c (three dwords copied) is the hook vector_copy.
 * 0x40765c(obj, flags): counter - 1; the entry freed (0x406fbc), counter - 1,
 * flags bit 0: delete. EAX: the original's is the counter word's address
 * (no caller reads it; not compared in vitro). */
void es2_sim_service_light_object_dtor(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t obj = A(0), eax = obj;
    counter(&c, -1);
    if (obj) {
        w32(&c, obj, VT_LIGHT_OBJECT);
        entry_free(&c, r32(&c, VA_LIGHT_TABLE), r16(&c, obj + 0xcu), &eax);
        counter(&c, -1);
        if ((A(1) & 1u) && CALL(&c, VA_DELETE, &eax, obj)) { failed(sc); return; }
    }
    sc->eax = eax;
}
/* 0x4076c4(obj, pos) (the object's entry position through 0x407018) has no
 * static reference in the image: no row; 0x407018 keeps its row (0x4076da
 * calls it). */
/* 0x407878(): every scene light (0x4a960c, word count 0x497ae4) into the active list. */
void es2_sim_service_scene_lights_activate(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    for (int32_t i = 0; i < sw(&c, VA_SCENE_COUNT); ++i)
        if (CALL(&c, VA_LIGHT_LIST_ADD, NULL, r32(&c, VA_SCENE_LIGHTS + 4u * (uint32_t)i))) { failed(sc); return; }
}

/* ======================================================================
 * The grid's horizon point, the renderer's small leaves and the stream-read
 * thunks (rasterizer.md, "Renderer leftovers"; rows in
 * sim/dispatch/services/renderer_leaves.h). Every row sets EAX, EDX and ECX as the
 * original leaves them; where the original returns right after a call the
 * callee's registers pass through. */
#define VA_SINCOS_PAIR_13 0x0047eb30u   /* hook */
#define VA_RT_MEMSET_13 0x0049681au     /* service */
#define VA_PART_LIST_CTOR_13 0x00476710u /* service */

static void regs_out(es2_sim_service_call *sc, const es2_sim_result *r) { sc->eax = r->eax; sc->edx = r->edx; sc->ecx = r->ecx; }
/* SAR r32, CL (the count masked to 5 bits). */
static int32_t sar13(uint32_t v, uint32_t n) {
    n &= 31u;
    return (int32_t)((v >> n) | ((v & 0x80000000u) && n ? ~(0xffffffffu >> n) : 0u));
}

/* 0x480050(a, m, out) (listing 0x480050..0x4800bc; runtime_leaves.h's catalogue VA,
 * computed here only for 0x470a1c): with the sign-extended words m0..m3 at
 * m+0, +2, +4, +6, out0 = low dword of ((a0*m0 + a1*m2 + 0x2000) >> 14)
 * + [m+8], out1 the same with m1, m3 and [m+0xc] (64-bit products and
 * sums, SHRD). */
static void rotate_offset(const ctx *c, uint32_t a, uint32_t m, uint32_t out) {
    int64_t a0 = s32(c, a), a1 = s32(c, a + 4u);
    uint64_t x = (uint64_t)(a0 * sw(c, m)) + (uint64_t)(a1 * sw(c, m + 4u)) + 0x2000u;
    w32(c, out, (uint32_t)(x >> 14) + r32(c, m + 8u));
    uint64_t y = (uint64_t)(a0 * sw(c, m + 2u)) + (uint64_t)(a1 * sw(c, m + 6u)) + 0x2000u;
    w32(c, out + 4u, (uint32_t)(y >> 14) + r32(c, m + 0xcu));
}

/* 0x470a1c(G, cam, out) (listing 0x470a1c..0x470b26): the screen point of
 * the grid's horizon ahead of the camera. With s = [G+0x108] and
 * F = [G+0x10c] the distance d = F << s, or 1000 when the camera's cell
 * (cam.x >> s, cam.y >> s) lies within F cells of the grid's edge (signed
 * compares against (1 << [G+0x100]) - F and (1 << [G+0x104]) - F). The
 * heading word [cam+0x14] (the dword's high half is the camera pointer's)
 * gives the rotation (hook 0x47eb30) with the camera's x, y as offset;
 * (0, d) rotated (0x480050) with the height [G+0x110] is transformed by
 * the view in use 0x6c5ff4 (hook 0x48043c), projected (0x48c5fc) and put
 * on the screen (0x48c6d0). out <- the screen point. EAX out, EDX its y,
 * ECX its x. A projection's division fault refuses the call. */
static int horizon_point(ctx *c, uint32_t g, uint32_t cam, uint32_t out, es2_sim_result *res) {
    uint32_t top = c->l->stack_hi, f = frame_push(c, 0x48u);
    if (!f) return refuse(c, 0x00470a1cu, "horizon point", "the own code's call area is exhausted");
#define L(o) (f + 0x48u - (o))
    uint32_t sh = r32(c, g + 0x108u);
    int32_t x = sar13(r32(c, cam + 4u), sh), y = sar13(r32(c, cam + 8u), sh);
    w32(c, L(0x8), r32(c, VA_VIEW_POINTER));
    w32(c, L(0x10), 0);
    int32_t edge = s32(c, g + 0x10cu);
    w32(c, L(0xc), (uint32_t)edge << (sh & 31u));
    int inside = x >= edge && x < (int32_t)((1u << (r32(c, g + 0x100u) & 31u)) - (uint32_t)edge) &&
                 edge <= y && (int32_t)((1u << (r32(c, g + 0x104u) & 31u)) - (uint32_t)edge) > y;
    if (!inside) w32(c, L(0xc), 1000u);
    w32(c, L(0x40), r32(c, cam + 4u));
    w32(c, L(0x3c), r32(c, cam + 8u));
    uint32_t arg = (cam & 0xffff0000u) | r16(c, cam + 0x14u);
    es2_sim_result r;
    uint32_t in1[3] = {r32(c, cam + 8u), arg, L(0x48)};
    if (call_regs(c, VA_SINCOS_PAIR_13, (const uint32_t[]){arg, L(0x48)}, 2, in1, &r)) { frame_pop(c, top); return 1; }
    rotate_offset(c, L(0x10), L(0x48), L(0x2c));
    w32(c, L(0x24), r32(c, g + 0x110u));
    uint32_t view = r32(c, L(0x8));
    uint32_t in2[3] = {view, L(0x38), L(0x2c)};
    if (call_regs(c, VA_TRANSFORM_POINT, (const uint32_t[]){L(0x2c), view, L(0x38)}, 3, in2, &r)) { frame_pop(c, top); return 1; }
    if (project_point(c, L(0x38), L(0x18))) { frame_pop(c, top); return 1; }
    w32(c, L(0x20), r32(c, L(0x18)) + r32(c, VA_CENTRE_X));
    w32(c, L(0x1c), r32(c, VA_CENTRE_Y) - r32(c, L(0x14)));
    w32(c, out, r32(c, L(0x20)));
    w32(c, out + 4u, r32(c, L(0x1c)));
    res->eax = out; res->edx = r32(c, L(0x1c)); res->ecx = r32(c, L(0x20));
#undef L
    frame_pop(c, top);
    return 0;
}
void es2_sim_service_grid_horizon_point(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    es2_sim_result r;
    if (horizon_point(&c, A(0), A(1), A(2), &r)) { failed(sc); return; }
    regs_out(sc, &r);
}
/* 0x48de70() (listing 0x48de70..0x48de77): byte 0x6cbc04 <- 1; no
 * register changes. */
void es2_sim_service_view_lights_on(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    w8(&c, VA_LIGHTS_ON, 1);
}
/* 0x48de78(P) (listing 0x48de78..0x48de9f): memset(P, 0, 0x20), then the
 * words +0x10, +6, +0 <- 0x4000. EAX P, EDX and ECX the memset's. */
void es2_sim_service_matrix_identity_48de78(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t p = A(0);
    es2_sim_result r;
    if (call_regs(&c, VA_RT_MEMSET_13, (const uint32_t[]){p, 0u, 0x20u}, 3, NULL, &r)) { failed(sc); return; }
    w16(&c, p + 0x10u, 0x4000u);
    w16(&c, p + 6u, 0x4000u);
    w16(&c, p, 0x4000u);
    sc->eax = p; sc->edx = r.edx; sc->ecx = r.ecx;
}
/* 0x4758b4(P) (listing 0x4758b4..0x4758ff): the vtable chain 0x4a69f0,
 * 0x4a6a14 (counter + 1), 0x4a6250 (counter + 1), 0x4a0fbf; +0x10, +0x14,
 * +0x18, +0x1c <- 0; counter + 1. EAX P, EDX the counter pointer, ECX 0. */
static void group_ctor(const ctx *c, uint32_t p) {
    w32(c, p, 0x004a69f0u);
    w32(c, p, 0x004a6a14u);
    counter(c, 1);
    w32(c, p, 0x004a6250u);
    counter(c, 1);
    w32(c, p, 0x004a0fbfu);
    for (uint32_t o = 0x10u; o <= 0x1cu; o += 4u) w32(c, p + o, 0);
    counter(c, 1);
}
void es2_sim_service_group_ctor_4758b4(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    group_ctor(&c, A(0));
    sc->eax = A(0); sc->edx = r32(&c, VA_COUNTER_POINTER); sc->ecx = 0;
}
/* 0x42eb5c(S) (listing 0x42eb5c..0x42ebba): 0x4758b4(S); [S] <- 0x49afb0;
 * +0x6c 1, +0x54 3, +0x58 0x10, +0x5c 1, +0x60 8, +0x64 1, +0x68 1, +0x50
 * 0; bytes +0x70 0, +0x71 1; counter + 1. EAX S, EDX the counter pointer,
 * ECX S. */
void es2_sim_service_sky_ctor_42eb5c(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t s = A(0);
    group_ctor(&c, s);
    w32(&c, s, 0x0049afb0u);
    w32(&c, s + 0x6cu, 1u);
    w32(&c, s + 0x54u, 3u);
    w32(&c, s + 0x58u, 0x10u);
    w32(&c, s + 0x5cu, 1u);
    w32(&c, s + 0x60u, 8u);
    w32(&c, s + 0x64u, 1u);
    w32(&c, s + 0x68u, 1u);
    w32(&c, s + 0x50u, 0u);
    w8(&c, s + 0x70u, 0);
    w8(&c, s + 0x71u, 1);
    counter(&c, 1);
    sc->eax = s; sc->edx = r32(&c, VA_COUNTER_POINTER); sc->ecx = s;
}
/* 0x42ebbc(S, a, b, d) (listing 0x42ebbc..0x42ebe5): +0x28 <- (a + [S+0x58]
 * - 1) & 0xffffff, +0x2c <- b & 0xffffff, +0x30 <- d. EAX S, EDX d, ECX
 * b & 0xffffff. */
void es2_sim_service_sky_bands_42ebbc(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t s = A(0), b = A(2) & 0xffffffu;
    w32(&c, s + 0x28u, (A(1) + r32(&c, s + 0x58u) - 1u) & 0xffffffu);
    w32(&c, s + 0x2cu, b);
    w32(&c, s + 0x30u, A(3));
    sc->eax = s; sc->edx = A(3); sc->ecx = b;
}
/* 0x476940(P) (listing 0x476940..0x476965): 0x476710(P); [P] <- 0x4a1088;
 * dword +0x16 <- 0; counter + 1. EAX P, EDX the counter pointer, ECX P.
 * 0x476aac(P) (listing 0x476aac..0x476adc): 0x476710(P); [P] <- 0x4a1054;
 * word +0x16 <- 0; +0x18, +0x1c <- 0; counter + 1. EAX P, EDX 0, ECX the
 * counter pointer. */
void es2_sim_service_part_list_ctor_476940(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t p = A(0);
    if (CALL(&c, VA_PART_LIST_CTOR_13, NULL, p)) { failed(sc); return; }
    w32(&c, p, 0x004a1088u);
    w32(&c, p + 0x16u, 0);
    counter(&c, 1);
    sc->eax = p; sc->edx = r32(&c, VA_COUNTER_POINTER); sc->ecx = p;
}
void es2_sim_service_part_list_ctor_476aac(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t p = A(0);
    if (CALL(&c, VA_PART_LIST_CTOR_13, NULL, p)) { failed(sc); return; }
    w32(&c, p, 0x004a1054u);
    w16(&c, p + 0x16u, 0);
    w32(&c, p + 0x18u, 0);
    w32(&c, p + 0x1cu, 0);
    counter(&c, 1);
    sc->eax = p; sc->edx = 0; sc->ecx = r32(&c, VA_COUNTER_POINTER);
}
/* The stream thunks: S.read(n, p) through the vtable slot +0x18 or
 * S.write(n, p) through +0x14, entered with EAX the vtable, EDX p and ECX n
 * where the thunk computes it (else the entry ECX); EAX, EDX and ECX the
 * slot's. */
static void stream_slot(es2_sim_service_call *sc, uint32_t slot, uint32_t s, uint32_t n, uint32_t p, uint32_t ecx) {
    SERVE(sc, c);
    uint32_t vt = r32(&c, s), in[3] = {vt, p, ecx};
    es2_sim_result r;
    if (call_regs(&c, r32(&c, vt + slot), (const uint32_t[]){s, n, p}, 3, in, &r)) { failed(sc); return; }
    regs_out(sc, &r);
}
static void stream_read_slot(es2_sim_service_call *sc, uint32_t s, uint32_t n, uint32_t p, uint32_t ecx) {
    stream_slot(sc, 0x18u, s, n, p, ecx);
}
/* 0x48f3ea(S, k, p) (listing 0x48f3ea..0x48f405): n = k << 2. */
void es2_sim_service_stream_read_dwords_48f3ea(es2_sim_service_call *sc, void *user) {
    (void)user; uint32_t n = A(1) << 2; stream_read_slot(sc, A(0), n, A(2), n);
}
/* 0x490874(S, p) (listing 0x490874..0x49088a): n = 1. */
void es2_sim_service_stream_read_byte_490874(es2_sim_service_call *sc, void *user) {
    (void)user; stream_read_slot(sc, A(0), 1u, A(1), sc->ecx);
}
/* 0x491171(S, k, p) (listing 0x491171..0x49118b) and 0x49245e (listing
 * 0x49245e..0x492478): n = 2k. */
void es2_sim_service_stream_read_words_491171(es2_sim_service_call *sc, void *user) {
    (void)user; uint32_t n = A(1) + A(1); stream_read_slot(sc, A(0), n, A(2), n);
}
void es2_sim_service_stream_read_words_49245e(es2_sim_service_call *sc, void *user) {
    (void)user; uint32_t n = A(1) + A(1); stream_read_slot(sc, A(0), n, A(2), n);
}
/* 0x49118c(S, p) (listing 0x49118c..0x4911a2) and 0x492430 (listing
 * 0x492430..0x492446): n = 2. */
void es2_sim_service_stream_read_word_49118c(es2_sim_service_call *sc, void *user) {
    (void)user; stream_read_slot(sc, A(0), 2u, A(1), sc->ecx);
}
void es2_sim_service_stream_read_word_492430(es2_sim_service_call *sc, void *user) {
    (void)user; stream_read_slot(sc, A(0), 2u, A(1), sc->ecx);
}
/* 0x4930ab(S) (listing 0x4930ab..0x4930b5): EAX the dword S+4 (the
 * stream's error state); EDX and ECX unchanged. */
void es2_sim_service_stream_error_4930ab(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    sc->eax = r32(&c, A(0) + 4u);
}

/* The stream-write thunks (rows in sim/dispatch/services/stream_writes.h):
 * S.write(n, p) through the vtable slot +0x14. 0x48f3ce is called by the
 * font write 0x4828f4 (src/fill.c) and 0x491156 by the same and by the
 * shape write slots; 0x4911a3 and 0x492479 only by the persistent classes'
 * write slots. */
/* 0x48f3ce(S, k, p) (listing 0x48f3ce..0x48f3e9): n = k << 2. */
void es2_sim_service_stream_write_dwords_48f3ce(es2_sim_service_call *sc, void *user) {
    (void)user; uint32_t n = A(1) << 2; stream_slot(sc, 0x14u, A(0), n, A(2), n);
}
/* 0x491156(S, k, p) (listing 0x491156..0x491170) and 0x492479 (listing
 * 0x492479..0x492493): n = 2k. */
void es2_sim_service_stream_write_words_491156(es2_sim_service_call *sc, void *user) {
    (void)user; uint32_t n = A(1) + A(1); stream_slot(sc, 0x14u, A(0), n, A(2), n);
}
void es2_sim_service_stream_write_words_492479(es2_sim_service_call *sc, void *user) {
    (void)user; uint32_t n = A(1) + A(1); stream_slot(sc, 0x14u, A(0), n, A(2), n);
}
/* 0x4911a3(S, w) (listing 0x4911a3..0x4911bd, the bytes of 0x492447): n = 2
 * from the caller's second argument slot; ECX the entry ECX. */
void es2_sim_service_stream_write_word_4911a3(es2_sim_service_call *sc, void *user) {
    (void)user; stream_slot(sc, 0x14u, A(0), 2u, sc->stack + 4u, sc->ecx);
}
