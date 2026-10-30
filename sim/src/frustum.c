/* The terrain draw's view frustum library as own code (es2/sim/frustum.h).
 * Contract: terrain.md, "the frustum library as own code" (every store and call
 * below with its listing address range); "listing" = llvm-objdump -d -M intel
 * of the reference DBSIM.EXE. Internal callees of the original are own
 * functions here; hooks and other packages' rows are called through the
 * dispatch over the bound main loop's call area. Guest state is read and
 * written only through the guest view.
 *
 * Records (byte offsets):
 * - frustum F: +0 the reach e, +4 the depth d, +8 the plane
 *   mask byte (0x02..0x40: planes 0..5), +0xc the eye (3 dwords), +0x18 six
 *   planes of 16 bytes (a normal of length 0x800 and the distance term).
 * - polygon: +0 the point count, +4 a pointer to 12-byte points.
 * The original's 0x4953c4 (the empty vector constructor, EAX its argument,
 * no store) is computed inline where an own function calls it. */
#include <stdint.h>
#include <string.h>
#include "es2/core/logic.h"
#include "es2/sim/frustum.h"
#include "es2/sim/memory.h"
#include "es2/sim/services.h"

/* callees (dispatch) */
#define VA_VECTOR_NEW 0x0049667cu       /* service */
#define VA_VECTOR_CTOR 0x004953c4u
#define VA_PLANE_CTOR 0x0049520eu       /* own row (below) */
#define VA_OBJECT_PLUS_4 0x00495157u    /* services (view.c) */
#define VA_CAMERA_SHIFT 0x00495162u
#define VA_CAMERA_CONTEXT 0x0049516du
#define VA_CAMERA_POSE 0x00495178u
#define VA_NEGATE3 0x004951d6u
#define VA_VECTOR_LENGTH 0x00492a7du
#define VA_DOT_DWORDS 0x0048d97au
#define VA_ROTATE 0x004954e4u           /* service (load.c): the thunk to the hook rotate_point */
#define VA_MATRIX_CTOR 0x00495132u      /* hooks */
#define VA_LINE_PLANE 0x0047e610u

static es2_sim_frustum *bound_frustum, own_frustum;
void es2_sim_frustum_bind(es2_sim_frustum *frustum) { bound_frustum = frustum; }

/* ---- guest view and calls */
typedef struct ctx { es2_sim_frustum *f; es2_sim_loop *l; const es2_guest_memory *m; } ctx;
static uint32_t r32(const ctx *c, uint32_t a) { return c->m->read32(c->m->context, a); }
static void w32(const ctx *c, uint32_t a, uint32_t v) { c->m->write32(c->m->context, a, v); }
static int32_t s32(const ctx *c, uint32_t a) { return (int32_t)r32(c, a); }
static int refuse(ctx *c, uint32_t va, const char *name, const char *reason) {
    c->f->refused_va = va;
    return es2_sim_loop_refuse(c->l, va, name, reason);
}
/* D: the original's IDIV of EDX:EAX = dividend by the divisor faults at va
 * (es2_sim_loop_divide_refuse). */
static int divide_refuse(ctx *c, uint32_t va, uint64_t dividend, uint32_t divisor, const char *name, const char *reason) {
    c->f->refused_va = va;
    return es2_sim_loop_divide_refuse(c->l, va, dividend, divisor, 1, name, reason);
}
/* A call through the dispatch with the callee's registers in *r (may be NULL). */
static int call(ctx *c, uint32_t va, const uint32_t *slots, unsigned count, es2_sim_result *out) {
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
        c->f->refused_va = va;
        if (l->outcome == ES2_SIM_LOOP_EXITED) return 1;   /* the process ended inside */
        l->outcome = ES2_SIM_LOOP_REFUSED;
        l->refused_va = va;
        return 1;
    }
    if (out) *out = r;
    return 0;
}
#define CALL(c, va, r, ...) call((c), (va), (const uint32_t[]){__VA_ARGS__}, \
    (unsigned)(sizeof((const uint32_t[]){__VA_ARGS__}) / sizeof(uint32_t)), (r))
/* A zero-filled block of the call area for the original's stack locals:
 * the original's EBP-relative local -o is at frame + size - o. */
static uint32_t frame_push(ctx *c, uint32_t bytes) {
    bytes = (bytes + 3u) & ~3u;
    if (c->l->stack_hi - c->l->stack_lo < bytes + 0x400u) return 0;
    c->l->stack_hi -= bytes;
    for (uint32_t i = 0; i < bytes; i += 4u) w32(c, c->l->stack_hi + i, 0);
    return c->l->stack_hi;
}
static void frame_pop(ctx *c, uint32_t top) { c->l->stack_hi = top; }
/* SAR r32, CL: the count masked to five bits like the processor. */
static uint32_t sar(uint32_t v, uint32_t n) {
    n &= 31u;
    int32_t s = (int32_t)v;
    return (uint32_t)(s < 0 ? ~(~s >> n) : s >> n);
}

static int serving(es2_sim_service_call *sc, ctx *c) {
    c->f = bound_frustum ? bound_frustum : &own_frustum;
    if (!c->f->loop) c->f->loop = es2_sim_loop_bound();
    if (!c->f->loop) { sc->refused = 1; sc->reason = "no main loop bound (RD-05 rest)"; return 1; }
    c->l = c->f->loop;
    c->m = c->l->memory;
    ++c->f->calls;
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

/* ======================================================================
 * Leaves */

/* 0x4950a4(in, eye, out) / 0x4950ec (listing 0x4950a4..0x4950e9,
 * 0x4950ec..0x495131): out's count <- in's count, then each point of in
 * minus (sign -1) or plus (sign +1) the eye into out's points, x, y, z in
 * turn. The pointers and the count are read before the count is stored.
 * Registers: EAX/EDX past the last point read/written, ECX the eye. */
static void poly_offset(const ctx *c, uint32_t in, uint32_t eye, uint32_t out, int add, uint32_t reg[3]) {
    uint32_t src = r32(c, in + 4u), dst = r32(c, out + 4u), n = r32(c, in);
    w32(c, out, n);
    for (int32_t k = (int32_t)n; k > 0; --k, src += 12u, dst += 12u)
        for (uint32_t i = 0; i < 12u; i += 4u)
            w32(c, dst + i, add ? r32(c, eye + i) + r32(c, src + i) : r32(c, src + i) - r32(c, eye + i));
    reg[0] = src; reg[1] = dst; reg[2] = eye;
}

/* 0x495236(P) (listing 0x495236..0x495253): 1 when the plane's normal has
 * x or y nonzero and z zero; in DL over the caller's EDX (EAX = EDX). */
static uint32_t plane_vertical(const ctx *c, uint32_t p, uint32_t edx) {
    uint32_t r = (r32(c, p) != 0u || r32(c, p + 4u) != 0u) && r32(c, p + 8u) == 0u;
    return (edx & ~0xffu) | r;
}

/* 0x495254(P, k) (listing 0x495254..0x495265): P+0xc -= k * P[2] (low
 * dword). EDX the product. */
static uint32_t plane_shift(const ctx *c, uint32_t p, uint32_t k) {
    uint32_t prod = k * r32(c, p + 8u);
    w32(c, p + 0xcu, r32(c, p + 0xcu) - prod);
    return prod;
}

/* 0x47eaf8(P, pt) (listing 0x47eaf8..0x47eb2d): the dot product 0x48d97a
 * against -P[3]: 1 above, -1 below, 0 equal (signed). EAX = ECX the side,
 * EDX -P[3]. */
static int plane_side(ctx *c, uint32_t p, uint32_t pt, int32_t *side, uint32_t *edx) {
    es2_sim_result r;
    if (CALL(c, VA_DOT_DWORDS, &r, p, pt)) return 1;
    int32_t d = (int32_t)r.eax, k = (int32_t)(0u - r32(c, p + 0xcu));
    *side = d > k ? 1 : d < k ? -1 : 0;
    if (edx) *edx = (uint32_t)k;
    return 0;
}

/* 0x47e450(out, P, N) (listing 0x47e450..0x47e55a): the plane through P
 * with the normal N scaled to length 0x800. The length: 0x492a7d(N) when
 * a component is above 15000 or below -15000 (signed compares x, y, z in
 * turn), else the bit-pair square root 0x47df9c of the low dword of
 * x*x + y*y + z*z (0x47ea64 -> 0x47ea92). Each component: the 64-bit
 * product N[i] * 0x800 (0x47eaa1 -> 0x47df84) divided by the length with
 * IDIV (0x47eabe -> 0x47de2d), stored x, y, z; then out+0xc <-
 * -(P0*o0 + P1*o1 + P2*o2) (low dwords). A zero length or a quotient
 * outside 32 bits is the original's divide error at 0x47de3c: refused.
 * Registers: EAX P2*o2, EDX the distance term, ECX P. */
static int plane_from_normal(ctx *c, uint32_t out, uint32_t p, uint32_t n, uint32_t reg[3]) {
    int big = 0;
    for (uint32_t i = 0; i < 12u && !big; i += 4u) {
        int32_t v = s32(c, n + i);
        big = v > 15000 || v < -15000;
    }
    uint32_t len;
    if (big) {
        ++c->f->long_normals;
        es2_sim_result r;
        if (CALL(c, VA_VECTOR_LENGTH, &r, n)) return 1;
        len = r.eax;
    } else {
        uint32_t x = r32(c, n), y = r32(c, n + 4u), z = r32(c, n + 8u);
        len = es2_logic_root(x * x + y * y + z * z);
    }
    for (uint32_t i = 0; i < 12u; i += 4u) {
        int64_t prod = (int64_t)s32(c, n + i) * 0x800;
        if ((int32_t)len == 0)
            return divide_refuse(c, 0x0047de3cu, (uint64_t)prod, 0, "plane_from_normal", "divide by a zero normal length (the original's divide error)");
        int64_t q = prod / (int32_t)len;
        if (q > INT32_MAX || q < INT32_MIN)
            return divide_refuse(c, 0x0047de3cu, (uint64_t)prod, len, "plane_from_normal", "quotient overflow (the original's divide error)");
        w32(c, out + i, (uint32_t)(int32_t)q);
    }
    uint32_t a = r32(c, p) * r32(c, out), b = r32(c, p + 4u) * r32(c, out + 4u), e = r32(c, p + 8u) * r32(c, out + 8u);
    uint32_t d = 0u - (a + b + e);
    w32(c, out + 0xcu, d);
    reg[0] = e; reg[1] = d; reg[2] = p;
    return 0;
}

/* 0x47e73c(P, in, front, back) (listing 0x47e73c..0x47ea63, jump table
 * 0x47e827): splits the polygon at the plane. Fewer than three points: both
 * counts 0 (EAX 0, ECX 0, EDX back). Otherwise the edge from the last point
 * runs round the polygon; per point the case prev_side*3 + side + 4:
 * - 0, 3 (back, on -> back): the point to back, back's flag cleared;
 * - 1, 4, 7 (to on): the point to back and to front;
 * - 5, 8 (to front): the point to front, front's flag cleared;
 * - 2 (back -> front): when line_plane (0x47e610) cuts the edge, the cut to
 *   back and to front, the point to front, front's flag cleared;
 * - 6 (front -> back): when it cuts, the cut to back and front, the point
 *   to back, back's flag cleared.
 * Points are copied +8, +0, +4. A side with fewer than three points gets
 * the count 0 and its flag set; with both flags set front becomes a copy
 * of in. The counts are stored front then back; EAX 1 when both are
 * nonzero, EDX front's count, ECX back's. */
static int poly_plane_clip(ctx *c, uint32_t p, uint32_t in, uint32_t front, uint32_t back, uint32_t reg[3]) {
    ++c->f->splits;
    if (s32(c, in) < 3) {
        ++c->f->short_inputs;
        w32(c, front, 0);
        w32(c, back, 0);
        reg[0] = 0; reg[1] = back; reg[2] = 0;
        return 0;
    }
    uint32_t top = c->l->stack_hi, fr = frame_push(c, 0x44u);
    if (!fr) return refuse(c, 0x0047e73cu, "poly_plane_clip", "the own code's call area is exhausted");
    uint32_t edge = fr + 0x44u - 0x38u, cut = fr + 0x44u - 0x44u;   /* 0x47eadb(edge): two empty vectors */
    int flag_front = 1, flag_back = 1, bad = 0;
    copy3(c, edge, r32(c, in + 4u) + 12u * r32(c, in) - 12u);
    int32_t prev, side;
    if ((bad = plane_side(c, p, edge, &prev, NULL)) != 0) goto done;
    uint32_t fp = r32(c, front + 4u), bp = r32(c, back + 4u), src = r32(c, in + 4u);
    uint32_t nf = 0, nb = 0;
    for (int32_t k = s32(c, in); k > 0; --k) {
        copy3(c, edge + 0xcu, src);
        src += 12u;
        if ((bad = plane_side(c, p, edge + 0xcu, &side, NULL)) != 0) goto done;
        es2_sim_result r;
        ++c->f->cases[prev * 3 + side + 4];
        switch (prev * 3 + side + 4) {
        case 0: case 3:
            ++nb; copy3(c, bp, edge + 0xcu); bp += 12u; flag_back = 0;
            break;
        case 1: case 4: case 7:
            ++nb; copy3(c, bp, edge + 0xcu); bp += 12u;
            ++nf; copy3(c, fp, edge + 0xcu); fp += 12u;
            break;
        case 5: case 8:
            ++nf; copy3(c, fp, edge + 0xcu); fp += 12u; flag_front = 0;
            break;
        case 2:
            if ((bad = CALL(c, VA_LINE_PLANE, &r, p, edge, cut)) != 0) goto done;
            if (!(r.eax & 0xffu)) { ++c->f->misses; break; }
            ++nb; copy3(c, bp, cut); bp += 12u;
            ++nf; copy3(c, fp, cut); fp += 12u;
            ++nf; copy3(c, fp, edge + 0xcu); fp += 12u; flag_front = 0;
            break;
        default:   /* 6 */
            if ((bad = CALL(c, VA_LINE_PLANE, &r, p, edge, cut)) != 0) goto done;
            if (!(r.eax & 0xffu)) { ++c->f->misses; break; }
            ++nb; copy3(c, bp, cut); bp += 12u;
            ++nf; copy3(c, fp, cut); fp += 12u;
            ++nb; copy3(c, bp, edge + 0xcu); bp += 12u; flag_back = 0;
            break;
        }
        prev = side;
        copy3(c, edge, edge + 0xcu);
    }
    if ((int32_t)nf < 3) { flag_front = 1; nf = 0; }
    if ((int32_t)nb < 3) { flag_back = 1; nb = 0; }
    if (flag_front && flag_back) {
        ++c->f->kept;
        src = r32(c, in + 4u);
        fp = r32(c, front + 4u);
        nf = r32(c, in);
        for (int32_t k = (int32_t)nf; k > 0; --k, src += 12u, fp += 12u) copy3(c, fp, src);
    }
    w32(c, front, nf);
    w32(c, back, nb);
    reg[0] = nf && nb; reg[1] = nf; reg[2] = nb;
done:
    frame_pop(c, top);
    return bad;
}

/* ======================================================================
 * The frustum */

/* 0x4949a4(F, d, e) (listing 0x4949a4..0x4949e2): the eye's empty
 * constructor, the six planes by 0x49667c(F+0x18, 0x10, 6, 1, 0x49520e),
 * then F+4 <- d, F+0 <- e, F+8 <- 0xffffffff. EAX F, ECX d, EDX
 * 0x49667c's. */
static int frustum_new(ctx *c, uint32_t f, uint32_t a, uint32_t b, uint32_t reg[3]) {
    es2_sim_result r;
    if (CALL(c, VA_VECTOR_NEW, &r, f + 0x18u, 0x10u, 6u, 1u, VA_PLANE_CTOR)) return 1;
    w32(c, f + 4u, a);
    w32(c, f, b);
    w32(c, f + 8u, 0xffffffffu);
    reg[0] = f; reg[1] = r.edx; reg[2] = a;
    return 0;
}

/* 0x4949e4(F, cam, d, e) (listing 0x4949e4..0x494cb0, frame 0x80):
 * F+4 <- d, F+0 <- e, the eye F+0xc <- the camera position
 * (0x495157, copied +8, +0, +4). From the camera's context v (0x49516d):
 * W = v[0x218] - v[0x210], H = v[0x21c] - v[0x214] and with the shift s
 * (0x495162, SAR count masked) the rectangle's edges at the depth d
 * l = -v[0x220]*d >> s, r = (v[0x220] + W)*d >> s,
 * t = -v[0x224]*d >> s, u = (v[0x224] + H)*d >> s (low dwords). The camera's matrix M
 * (0x495132, 0x495178) turns (0, 0, e), (e, 0, 0), (0, -e, 0) into
 * the points p1, p2, p3 and each normal into n (0x4954e4); the planes
 * (0x47e450, out F+o, point, n):
 *   (0, t, -d) -> +0x38 through p1, then p1 negated (0x4951d6);
 *   (0, u, d) -> +0x48 through -p1;
 *   (-d, r, 0) -> +0x68 through p2, then p2 negated;
 *   (d, l, 0) -> +0x58 through -p2;
 *   (0, d, 0) -> +0x18 through p3;
 *   -n -> +0x28 through n (n copied +8, +0, +4, then negated).
 * Registers: the last 0x47e450's. */
static int frustum_place(ctx *c, uint32_t f, uint32_t cam, uint32_t a, uint32_t b, uint32_t reg[3]) {
    uint32_t top = c->l->stack_hi, fr = frame_push(c, 0x80u);
    if (!fr) return refuse(c, 0x004949e4u, "frustum_place", "the own code's call area is exhausted");
#define L(o) (fr + 0x80u - (o))
    es2_sim_result r;
    int bad = 1;
    w32(c, f + 4u, a);
    w32(c, f, b);
    if (CALL(c, VA_OBJECT_PLUS_4, &r, cam)) goto done;
    copy3(c, f + 0xcu, r.eax);
    if (CALL(c, VA_CAMERA_CONTEXT, &r, cam)) goto done;
    uint32_t v = r.eax;
    uint32_t w = r32(c, v + 0x218u) - r32(c, v + 0x210u), h = r32(c, v + 0x21cu) - r32(c, v + 0x214u);
    uint32_t e[4];
    const uint32_t base[4] = {0u - r32(c, v + 0x220u), 0, 0u - r32(c, v + 0x224u), 0};
    for (unsigned i = 0; i < 4u; ++i) {
        if (CALL(c, VA_CAMERA_SHIFT, &r, cam)) goto done;
        uint32_t x = i == 1u ? r32(c, v + 0x220u) + w : i == 3u ? r32(c, v + 0x224u) + h : base[i];
        e[i] = sar(x * r32(c, f + 4u), r.eax);
    }
    uint32_t nd = r32(c, f + 4u), m = L(0x80u), pt = L(0x30u), n = L(0x3cu);   /* nd: the depth d */
    if (CALL(c, VA_MATRIX_CTOR, NULL, m) || CALL(c, VA_CAMERA_POSE, NULL, cam, m)) goto done;
    /* The three points: (0, 0, e) -> -0x48, (e, 0, 0) -> -0x54, (0, -e, 0) -> -0x60. */
    w32(c, pt + 4u, 0); w32(c, pt, 0); w32(c, pt + 8u, r32(c, f));
    if (CALL(c, VA_ROTATE, NULL, pt, m, L(0x48u))) goto done;
    w32(c, pt + 8u, 0); w32(c, pt, r32(c, f));
    if (CALL(c, VA_ROTATE, NULL, pt, m, L(0x54u))) goto done;
    w32(c, pt, 0); w32(c, pt + 4u, 0u - r32(c, f));
    if (CALL(c, VA_ROTATE, NULL, pt, m, L(0x60u))) goto done;
    w32(c, pt, 0);
    /* (0, t, -d) */
    w32(c, pt + 4u, e[2]); w32(c, pt + 8u, 0u - nd);
    if (CALL(c, VA_ROTATE, NULL, pt, m, n) || plane_from_normal(c, f + 0x38u, L(0x48u), n, reg) ||
        CALL(c, VA_NEGATE3, NULL, L(0x48u)))
        goto done;
    /* (0, u, d) */
    w32(c, pt + 4u, e[3]); w32(c, pt + 8u, nd);
    if (CALL(c, VA_ROTATE, NULL, pt, m, n) || plane_from_normal(c, f + 0x48u, L(0x48u), n, reg)) goto done;
    /* (-d, r, 0) */
    w32(c, pt, 0u - nd); w32(c, pt + 4u, e[1]); w32(c, pt + 8u, 0);
    if (CALL(c, VA_ROTATE, NULL, pt, m, n) || plane_from_normal(c, f + 0x68u, L(0x54u), n, reg) ||
        CALL(c, VA_NEGATE3, NULL, L(0x54u)))
        goto done;
    /* (d, l, 0) */
    w32(c, pt + 4u, e[0]); w32(c, pt, nd);
    if (CALL(c, VA_ROTATE, NULL, pt, m, n) || plane_from_normal(c, f + 0x58u, L(0x54u), n, reg)) goto done;
    /* (0, d, 0) */
    w32(c, pt + 8u, 0); w32(c, pt, 0); w32(c, pt + 4u, nd);
    if (CALL(c, VA_ROTATE, NULL, pt, m, n) || plane_from_normal(c, f + 0x18u, L(0x60u), n, reg)) goto done;
    copy3(c, pt, n);
    if (CALL(c, VA_NEGATE3, NULL, n) || plane_from_normal(c, f + 0x28u, pt, n, reg)) goto done;
    bad = 0;
done:
#undef L
    frame_pop(c, top);
    return bad;
}

/* 0x494e58(F, poly, a, b, out) (listing 0x494e58..0x4950a1, frame
 * 0x258): two local polygons of 20 points and six local planes
 * (0x49667c). Each of F's planes i is copied (+0xc, +8, +0, +4) and, unless
 * vertical (0x495236), shifted (0x495254) by -b when its z is above 0
 * (signed), else by a. out <- poly minus the eye (0x4950a4); then for
 * the mask bits of F+8 in the order 0x20, 0x40, 0x08, 0x10, 0x04, 0x02
 * (planes 4, 5, 2, 3, 1, 0) the current polygon is split (0x47e73c) into
 * the other one (front) and a scratch polygon (back) and the two swap
 * (0x495221). out <- the current polygon plus the eye (0x4950ec). EAX 1
 * when out's count is nonzero, ECX out, EDX 0x4950ec's. */
static int frustum_clip(ctx *c, uint32_t f, uint32_t poly, uint32_t a, uint32_t b, uint32_t out, uint32_t reg[3]) {
    uint32_t top = c->l->stack_hi, fr = frame_push(c, 0x258u);
    if (!fr) return refuse(c, 0x00494e58u, "frustum_clip", "the own code's call area is exhausted");
#define L(o) (fr + 0x258u - (o))
    int bad = 1;
    if (CALL(c, VA_VECTOR_NEW, NULL, L(0x108u), 0xcu, 0x14u, 1u, VA_VECTOR_CTOR) ||
        CALL(c, VA_VECTOR_NEW, NULL, L(0x1f8u), 0xcu, 0x14u, 1u, VA_VECTOR_CTOR) ||
        CALL(c, VA_VECTOR_NEW, NULL, L(0x258u), 0x10u, 6u, 1u, VA_PLANE_CTOR))
        goto done;
    w32(c, L(4u), L(0x108u));
    w32(c, L(0xcu), L(0x1f8u));
    for (uint32_t i = 0; i < 6u; ++i) {
        uint32_t s = f + 0x18u + 16u * i, d = L(0x258u) + 16u * i;
        w32(c, d + 0xcu, r32(c, s + 0xcu));
        w32(c, d + 8u, r32(c, s + 8u));
        w32(c, d, r32(c, s));
        w32(c, d + 4u, r32(c, s + 4u));
        if (plane_vertical(c, d, 0) & 0xffu) { ++c->f->vertical; continue; }
        if (s32(c, d + 8u) > 0) { ++c->f->b_shifts; plane_shift(c, d, 0u - b); }
        else { ++c->f->a_shifts; plane_shift(c, d, a); }
    }
    uint32_t regs[3];
    poly_offset(c, poly, f + 0xcu, out, 0, regs);
    uint32_t cur = out, other = L(8u);
    static const struct { uint8_t bit; uint16_t local; } order[6] = {
        {0x20u, 0x218u}, {0x40u, 0x208u}, {0x08u, 0x238u}, {0x10u, 0x228u}, {0x04u, 0x248u}, {0x02u, 0x258u},
    };
    for (unsigned i = 0; i < 6u; ++i) {
        if (!(es2_sim_read8(c->m, f + 8u) & order[i].bit)) continue;
        if (poly_plane_clip(c, L(order[i].local), cur, other, L(0x10u), regs)) goto done;
        uint32_t t = cur; cur = other; other = t;
    }
    poly_offset(c, cur, f + 0xcu, out, 1, regs);
    reg[0] = r32(c, out) != 0u; reg[1] = regs[1]; reg[2] = out;
    bad = 0;
done:
#undef L
    frame_pop(c, top);
    return bad;
}

/* ======================================================================
 * Service rows (sim/dispatch/services/frustum.h). */
static void set3(es2_sim_service_call *sc, const uint32_t reg[3]) { sc->eax = reg[0]; sc->edx = reg[1]; sc->ecx = reg[2]; }

void es2_sim_service_frustum_new(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t reg[3];
    if (frustum_new(&c, A(0), A(1), A(2), reg)) { failed(sc); return; }
    set3(sc, reg);
}
void es2_sim_service_frustum_place(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t reg[3];
    if (frustum_place(&c, A(0), A(1), A(2), A(3), reg)) { failed(sc); return; }
    set3(sc, reg);
}
void es2_sim_service_frustum_clip(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t reg[3];
    if (frustum_clip(&c, A(0), A(1), A(2), A(3), A(4), reg)) { failed(sc); return; }
    set3(sc, reg);
}
void es2_sim_service_poly_offset_sub(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t reg[3];
    poly_offset(&c, A(0), A(1), A(2), 0, reg);
    set3(sc, reg);
}
void es2_sim_service_poly_offset_add(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t reg[3];
    poly_offset(&c, A(0), A(1), A(2), 1, reg);
    set3(sc, reg);
}
/* 0x49520e(e) (listing 0x49520e..0x495220): 0x4953c4(e); EAX = ECX = e,
 * EDX unchanged. */
void es2_sim_service_plane_ctor(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    sc->eax = A(0); sc->ecx = A(0);
}
/* 0x495221(a, b) (listing 0x495221..0x495235): swaps the dwords *a and *b
 * (*a written first). EAX a, ECX the old *a, EDX b. */
void es2_sim_service_swap_dwords(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t x = r32(&c, A(0)), y = r32(&c, A(1));
    w32(&c, A(0), y);
    w32(&c, A(1), x);
    sc->eax = A(0); sc->ecx = x; sc->edx = A(1);
}
void es2_sim_service_plane_vertical(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    sc->eax = sc->edx = plane_vertical(&c, A(0), sc->edx);
}
void es2_sim_service_plane_shift(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    sc->edx = plane_shift(&c, A(0), A(1));
    sc->eax = A(0);
}
void es2_sim_service_plane_from_normal(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t reg[3];
    if (plane_from_normal(&c, A(0), A(1), A(2), reg)) { failed(sc); return; }
    set3(sc, reg);
}
void es2_sim_service_poly_plane_clip(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t reg[3];
    if (poly_plane_clip(&c, A(0), A(1), A(2), A(3), reg)) { failed(sc); return; }
    set3(sc, reg);
}
void es2_sim_service_plane_side(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    int32_t side;
    uint32_t edx;
    if (plane_side(&c, A(0), A(1), &side, &edx)) { failed(sc); return; }
    sc->eax = sc->ecx = (uint32_t)side;
    sc->edx = edx;
}
