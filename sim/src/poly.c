/* The software rasterizer's polygon fills of DBSIM.EXE as own code
 * (rasterizer.md, "Software polygon fills"; es2/sim/poly.h). "Listing" =
 * llvm-objdump -d -M intel of the reference DBSIM.EXE (SHA-256
 * 8561aa47...c4ad). Every function reads and writes guest memory through the
 * bound loop's guest view, in the listing's order; the view transform 0x48c57c,
 * the projection 0x48c5fc and to_screen 0x48c6d0 are called through the
 * dispatch. The hand-written span loops (0x46ac14, 0x46ad4c, 0x46aeb1) work on
 * byte registers with carries; they are reproduced register by register (struct
 * sregs) because their stores depend on those carries. Paths the original
 * leaves undefined or faulting are refused by name. */
#include <string.h>
#include "es2/sim/poly.h"
#include "es2/sim/memory.h"
#include "es2/sim/dispatch.h"

/* ---- guest VAs (DBSIM.EXE) */
#define VA_FAR ES2_SIM_VA_POLY_FAR
#define VA_HAZE ES2_SIM_VA_POLY_HAZE
#define VA_SHADES ES2_SIM_VA_POLY_SHADES     /* shade levels */
#define VA_HAZE_LEVELS 0x004d3cb8u
#define VA_RECIP ES2_SIM_VA_POLY_RECIP
#define VA_TEXTURE ES2_SIM_VA_POLY_TEXTURE
#define VA_DEPTH_ON ES2_SIM_VA_POLY_DEPTH_ON
#define VA_RUNS ES2_SIM_VA_POLY_RUNS
#define VA_GC 0x006c5f70u          /* -> the GC: +0x208 the region mode, clip rect +0x210..+0x21c */
#define VA_VIEW_ROWS 0x004a3652u   /* -> the row table; its first dword the bits */
#define VA_VIEW_PITCH 0x004a3676u
#define VA_NEAR 0x006c602cu        /* the near distance ([cam+0x1e]) */
#define VA_DEPTH_BUFFER 0x006c5f90u
#define VA_PAGE_TABLE 0x004a08b4u  /* -> the current texture page table */
/* The span state. */
#define VA_SPAN_DEST 0x004a0968u
#define VA_SPAN_WIDTH 0x004a096cu
#define VA_SPAN_U 0x004a0978u
#define VA_SPAN_V 0x004a0980u
#define VA_SPAN_DU 0x004a0988u
#define VA_SPAN_DV 0x004a0990u
#define VA_SPAN_FLAGS 0x004a0994u  /* byte: nonzero = texel 0 is transparent */
#define VA_SPAN_SHADES 0x004a0998u /* -> the shade table (256-byte rows) */
#define VA_SPAN_WORD 0x004a099cu   /* the texture page */
#define VA_SPAN_END 0x004a09a0u
#define VA_SPAN_SHADE 0x004a09acu
#define VA_SPAN_DSHADE 0x004a09b0u
#define VA_SPAN_END2 0x004a09b8u
#define VA_SPAN_SAVED_SP 0x004a09ccu
#define VA_RUN_COUNT 0x004a5938u
#define VA_RUN_LIST 0x004a593cu
#define VA_RUN_LEFT 0x004a5940u
#define VA_DEPTH_RUNS 0x004a5944u  /* {start, length} pairs 0x48b854 builds */
#define VA_DEPTH_X 0x004a5ad8u
#define VA_DEPTH_PTR 0x004a5adcu
#define VA_DEPTH_Z 0x004a5ae0u
#define VA_DEPTH_DZ 0x004a5ae4u
#define VA_DEPTH_WIDTH 0x004a5ae8u
#define VA_DEPTH_TAG 0x004a5aecu
/* The edge tables (0x477d48). */
#define VA_DEDUP 0x006c53a0u
#define VA_CHAIN1 0x006bb930u
#define VA_CHAIN2 0x006bbb60u
#define VA_EDGE1 0x006bbd90u
#define VA_EDGE2 0x006c0890u
#define VA_RESULT ES2_SIM_VA_POLY_EDGES
#define VA_QUAD 0x006c565cu        /* the perspective subdivision state 0x6c565c..0x6c56a7 */
/* The fill's buffers. */
#define VA_CLIP_X_OUT 0x004d4914u
#define VA_CLIP_Y_OUT 0x004d4b44u
#define VA_RECORDS 0x004d4d74u     /* 0x46817c's 0x28-byte records */
#define VA_NEAR_OUT 0x004d46e4u
/* Called. */
#define VA_VIEW_TRANSFORM 0x0048c57cu
#define VA_PROJECT 0x0048c5fcu
#define VA_TO_SCREEN 0x0048c6d0u
#define VA_DIV16_IDIV 0x0047e078u  /* the idiv of 0x47e068 */

/* ---- the guest view */
static uint32_t r32(const es2_sim_loop *l, uint32_t a) { return l->memory->read32(l->memory->context, a); }
static uint8_t r8(const es2_sim_loop *l, uint32_t a) { return es2_sim_read8(l->memory, a); }
static void w32(const es2_sim_loop *l, uint32_t a, uint32_t v) { l->memory->write32(l->memory->context, a, v); }
static void w16(const es2_sim_loop *l, uint32_t a, uint16_t v) { l->memory->write16(l->memory->context, a, v); }
static void w8(const es2_sim_loop *l, uint32_t a, uint8_t v) { es2_sim_write8(l->memory, a, v); }
static int32_t s32(const es2_sim_loop *l, uint32_t a) { return (int32_t)r32(l, a); }
static uint32_t sar(uint32_t v, unsigned n) { return (uint32_t)((int32_t)v >> (n & 31u)); }
static uint32_t imul32(uint32_t a, uint32_t b) { return (uint32_t)((int64_t)(int32_t)a * (int32_t)b); }
static void copy7(const es2_sim_loop *l, uint32_t dst, uint32_t src) {
    for (uint32_t i = 0; i < 28u; i += 4u) w32(l, dst + i, r32(l, src + i));
}

static int refuse(es2_sim_loop *l, uint32_t va, const char *name, const char *reason) {
    return es2_sim_loop_refuse(l, va, name, reason);
}
static int call(es2_sim_loop *l, uint32_t va, const uint32_t *slots, unsigned count, uint32_t *eax) {
    es2_sim_result r;
    if (es2_sim_loop_call(l, va, slots, count, &r)) return 1;
    if (eax) *eax = r.eax;
    return 0;
}
static uint32_t frame_push(es2_sim_loop *l, uint32_t bytes) {
    bytes = (bytes + 3u) & ~3u;
    if (l->stack_hi - l->stack_lo < bytes + 0x400u) return 0;
    l->stack_hi -= bytes;
    for (uint32_t i = 0; i < bytes; i += 4u) w32(l, l->stack_hi + i, 0);
    return l->stack_hi;
}
static void frame_pop(es2_sim_loop *l, uint32_t top) { l->stack_hi = top; }

/* ======================================================================
 * Fixed-point helpers (listing 0x47e068..0x47e09c, 0x471cd8..0x471daa). */

/* 0x47e068(a, b): EDX:EAX = a << 16 (sign-extended), idiv b. */
static int div16(es2_sim_loop *l, const char *caller, int32_t a, int32_t b, uint32_t *q) {
    uint64_t n = (uint64_t)((int64_t)a * 65536);
    if (b == 0) return es2_sim_loop_divide_refuse(l, VA_DIV16_IDIV, n, 0, 1, caller, "0x47e068 divides by zero (#DE)");
    int64_t r = ((int64_t)a * 65536) / b;
    if (r < INT32_MIN || r > INT32_MAX)
        return es2_sim_loop_divide_refuse(l, VA_DIV16_IDIV, n, (uint32_t)b, 1, caller, "0x47e068's quotient overflows (#DE)");
    *q = (uint32_t)(int32_t)r;
    return 0;
}
int es2_sim_poly_div16(es2_sim_loop *l, uint32_t va, int32_t a, int32_t b, uint32_t *eax) {
    (void)va;
    return div16(l, "fixed divide", a, b, eax);
}
/* 0x47e07d/0x47e08d(a, b): imul, shrd 16: bits 16..47 of the product. */
static uint32_t mul16(uint32_t a, uint32_t b) { return (uint32_t)((uint64_t)((int64_t)(int32_t)a * (int32_t)b) >> 16); }
uint32_t es2_sim_poly_mul16(int32_t a, int32_t b) { return mul16((uint32_t)a, (uint32_t)b); }

/* 0x471cd8(lo*, hi*, a, b): [lo] += a; [hi] += b + carry. EAX a. */
int es2_sim_poly_add64(es2_sim_loop *l, uint32_t lo, uint32_t hi, uint32_t add_lo, uint32_t add_hi) {
    uint32_t v = r32(l, lo), s = v + add_lo;
    w32(l, lo, s);
    w32(l, hi, r32(l, hi) + add_hi + (s < v ? 1u : 0u));
    return 0;
}
/* 0x471cf0(lo*, hi*, a, b): [lo] -= a; [hi] -= b + borrow. EAX a. */
int es2_sim_poly_sub64(es2_sim_loop *l, uint32_t lo, uint32_t hi, uint32_t sub_lo, uint32_t sub_hi) {
    uint32_t v = r32(l, lo);
    w32(l, lo, v - sub_lo);
    w32(l, hi, r32(l, hi) - sub_hi - (v < sub_lo ? 1u : 0u));
    return 0;
}
/* 0x471d08(lo*, hi*, n): shrd [lo], [hi], n; sar [hi], n (n mod 32). EAX
 * the old [hi]. */
int es2_sim_poly_shift64(es2_sim_loop *l, uint32_t lo, uint32_t hi, uint32_t count) {
    unsigned n = count & 31u;
    uint32_t a = r32(l, lo), b = r32(l, hi);
    if (n) w32(l, lo, (a >> n) | (b << (32u - n))); else w32(l, lo, a);
    w32(l, hi, sar(b, n));
    return 0;
}
static int idiv64(es2_sim_loop *l, uint32_t va, const char *name, uint64_t n, int32_t d, uint32_t *q) {
    if (d == 0) return es2_sim_loop_divide_refuse(l, va, n, 0, 1, name, "divides by zero (#DE)");
    int64_t s = (int64_t)n;
    if (d == -1 && s == INT64_MIN) return es2_sim_loop_divide_refuse(l, va, n, (uint32_t)d, 1, name, "the quotient overflows (#DE)");
    int64_t r = s / d;
    if (r < INT32_MIN || r > INT32_MAX) return es2_sim_loop_divide_refuse(l, va, n, (uint32_t)d, 1, name, "the quotient overflows (#DE)");
    *q = (uint32_t)(int32_t)r;
    return 0;
}
/* 0x471d26(out, rec): when [rec+0x10] != 0: out[0] = (rec[0..1] << 16) /
 * [rec+0x10], out[1] = (rec[2..3] << 16) / [rec+0x10] (64-bit dividends,
 * the top 16 bits dropped). */
static int divide_pair(es2_sim_loop *l, uint32_t out, uint32_t rec, uint32_t *eax) {
    int32_t d = s32(l, rec + 0x10u);
    if (d == 0) return 0;
    uint32_t q;
    uint64_t n = (((uint64_t)r32(l, rec + 4u) << 32) | r32(l, rec)) << 16;
    if (idiv64(l, 0x00471d40u, "perspective divide", n, d, &q)) return 1;
    w32(l, out, q);
    n = (((uint64_t)r32(l, rec + 0xcu) << 32) | r32(l, rec + 8u)) << 16;
    if (idiv64(l, 0x00471d55u, "perspective divide", n, d, &q)) return 1;
    w32(l, out + 4u, q);
    *eax = q;
    return 0;
}
int es2_sim_poly_divide_pair(es2_sim_loop *l, uint32_t out, uint32_t rec) {
    uint32_t eax = 0;
    return divide_pair(l, out, rec, &eax);
}
/* 0x471d61(out, in, d, n): out[0..1] = (in[0..1] / d) << n (sign-extended),
 * out[2..3] = (in[2..3] / d) << n, out[4] = (in[4] << n) / d. */
static int divide_three(es2_sim_loop *l, uint32_t out, uint32_t in, int32_t d, uint32_t count, uint32_t *eax) {
    unsigned n = count & 31u;
    uint32_t q;
    for (uint32_t i = 0; i < 2u; ++i) {
        uint64_t v = ((uint64_t)r32(l, in + 8u * i + 4u) << 32) | r32(l, in + 8u * i);
        if (idiv64(l, i ? 0x00471d89u : 0x00471d75u, "perspective divide", v, d, &q)) return 1;
        uint64_t s = (uint64_t)(int64_t)(int32_t)q << n;
        w32(l, out + 8u * i, (uint32_t)s);
        w32(l, out + 8u * i + 4u, (uint32_t)(s >> 32));
    }
    uint64_t v = (uint64_t)(int64_t)s32(l, in + 0x10u) << n;
    if (idiv64(l, 0x00471da1u, "perspective divide", v, d, &q)) return 1;
    w32(l, out + 0x10u, q);
    *eax = q;
    return 0;
}
int es2_sim_poly_divide_three(es2_sim_loop *l, uint32_t out, uint32_t in, int32_t d, uint32_t count) {
    uint32_t eax = 0;
    return divide_three(l, out, in, d, count, &eax);
}

/* ======================================================================
 * Haze (listing 0x4680e0..0x46817a). */

/* 0x4680e0(v): [0x4a08ac] = v. */
void es2_sim_poly_set_far(es2_sim_loop *l, uint32_t far_d, uint32_t *eax) {
    w32(l, VA_FAR, far_d);
    *eax = far_d;
}
/* 0x4680f0(d): e = min(d, far), h = far sar 1; e <= h: haze 0 (EAX e);
 * else r = min(0x47e068(2 (e - h), far), 0x10000); haze = (0x47e08d(r,
 * levels - 1) * shades) << 8 (EAX haze). */
int es2_sim_poly_haze(es2_sim_loop *l, int32_t distance, uint32_t *eax) {
    int32_t far_d = s32(l, VA_FAR), e = distance < far_d ? distance : far_d;
    int32_t h = (int32_t)sar((uint32_t)far_d, 1);
    if (e <= h) {
        w32(l, VA_HAZE, 0);
        *eax = (uint32_t)e;
        return 0;
    }
    uint32_t t = ((uint32_t)e - (uint32_t)h) * 2u, r;
    if (div16(l, "haze", (int32_t)t, far_d, &r)) return 1;
    if ((int32_t)r > 0x10000) r = 0x10000u;
    uint32_t v = imul32(mul16(r, r32(l, VA_HAZE_LEVELS) - 1u), r32(l, VA_SHADES)) << 8;
    w32(l, VA_HAZE, v);
    *eax = v;
    return 0;
}
/* 0x468158(s): ((s * (shades - 1) + haze) & ~0xff) + [0x4a0998]. */
void es2_sim_poly_shade_colour(es2_sim_loop *l, int32_t shade, uint32_t *eax) {
    uint32_t v = imul32((uint32_t)shade, r32(l, VA_SHADES) - 1u) + r32(l, VA_HAZE);
    *eax = (v & 0xffffff00u) + r32(l, VA_SPAN_SHADES);
}

/* ======================================================================
 * Screen clippers (listing 0x469894..0x469dc6). */

/* 0x469894 (axis 0) / 0x469b2c (axis 1)(c, A, B, out, flags): the point
 * of the edge A-B at coordinate c on the axis. 0 when B or A lies on c.
 * P = A when B's coordinate is above A's, else B; Q the other; r =
 * (c - P) / (Q - P) (0x47e068); out = P + r (Q - P) on the other axis and
 * on the flagged fields (8 z; 1 u, v and inside it 2 w; 4 shade); fields
 * not flagged are not written. EAX 1. */
static int cut(es2_sim_loop *l, unsigned axis, int32_t c, uint32_t a, uint32_t b, uint32_t out, uint32_t flags, uint32_t *eax) {
    uint32_t k = 4u * axis, o = 4u - k;
    *eax = 0;
    if (s32(l, b + k) == c || s32(l, a + k) == c) return 0;
    uint32_t p, q;
    if (s32(l, b + k) > s32(l, a + k)) { p = a; q = b; } else { p = b; q = a; }
    uint32_t d[7];
    for (uint32_t i = 0; i < 7u; ++i) d[i] = r32(l, q + 4u * i) - r32(l, p + 4u * i);
    uint32_t r;
    if (div16(l, axis ? "y cut" : "x cut", (int32_t)((uint32_t)c - r32(l, p + k)), (int32_t)d[axis], &r)) return 1;
    w32(l, out + k, (uint32_t)c);
    w32(l, out + o, mul16(r, d[1u - axis]) + r32(l, p + o));
    if (flags & 8u) w32(l, out + 8u, mul16(r, d[2]) + r32(l, p + 8u));
    if (flags & 1u) {
        w32(l, out + 0xcu, mul16(r, d[3]) + r32(l, p + 0xcu));
        w32(l, out + 0x10u, mul16(r, d[4]) + r32(l, p + 0x10u));
        if (flags & 2u) w32(l, out + 0x14u, mul16(r, d[5]) + r32(l, p + 0x14u));
    }
    if (flags & 4u) w32(l, out + 0x18u, mul16(r, d[6]) + r32(l, p + 0x18u));
    *eax = 1;
    return 0;
}
int es2_sim_poly_cut_x(es2_sim_loop *l, int32_t c, uint32_t a, uint32_t b, uint32_t out, uint32_t flags, uint32_t *eax) {
    return cut(l, 0, c, a, b, out, flags, eax);
}
int es2_sim_poly_cut_y(es2_sim_loop *l, int32_t c, uint32_t a, uint32_t b, uint32_t out, uint32_t flags, uint32_t *eax) {
    return cut(l, 1u, c, a, b, out, flags, eax);
}
/* 0x4699c8 (x: [GC+0x210], [GC+0x218] + 1) / 0x469c64 (y: +0x214, +0x21c
 * + 1)(n, in, out, flags): Sutherland-Hodgman against both bounds in one
 * pass. prev starts at the last vertex. prev inside: copied; prev outside
 * and cur not outside on the same side: the entering cut at prev's bound.
 * Then cur outside: the leaving cut at cur's bound. Cuts are
 * cut(bound, cur, prev); a cut returning 0 adds nothing. EAX the count. */
static int clip(es2_sim_loop *l, unsigned axis, int32_t n, uint32_t in, uint32_t out, uint32_t flags, uint32_t *eax) {
    uint32_t k = 4u * axis, gc = r32(l, VA_GC);
    int32_t lo = s32(l, gc + 0x210u + k), hi = s32(l, gc + 0x218u + k) + 1;
    uint32_t prev = in + 28u * (uint32_t)n - 28u, cur = in;
    int32_t count = 0;
    for (int32_t i = 0; i < n; ++i, prev = cur, cur += 28u) {
        int32_t pv = s32(l, prev + k), cv = s32(l, cur + k);
        int p_lo = pv < lo, p_hi = pv > hi, c_lo = cv < lo, c_hi = cv > hi;
        uint32_t got;
        if (!p_lo && !p_hi) {
            copy7(l, out + 28u * (uint32_t)count, prev);
            ++count;
        } else {
            if (p_lo ? c_lo : c_hi) continue;
            if (cut(l, axis, p_lo ? lo : hi, cur, prev, out + 28u * (uint32_t)count, flags, &got)) return 1;
            if (got) ++count;
        }
        if (c_lo || c_hi) {
            if (cut(l, axis, c_lo ? lo : hi, cur, prev, out + 28u * (uint32_t)count, flags, &got)) return 1;
            if (got) ++count;
        }
    }
    *eax = (uint32_t)count;
    return 0;
}
int es2_sim_poly_clip_x(es2_sim_loop *l, int32_t n, uint32_t in, uint32_t out, uint32_t flags, uint32_t *eax) {
    return clip(l, 0, n, in, out, flags, eax);
}
int es2_sim_poly_clip_y(es2_sim_loop *l, int32_t n, uint32_t in, uint32_t out, uint32_t flags, uint32_t *eax) {
    return clip(l, 1u, n, in, out, flags, eax);
}

/* ======================================================================
 * Texture pages (listing 0x469dc8..0x469e13). */

/* 0x469dc8(p): [[0x4a08b4] + 0x804] = p. EAX [0x4a08b4]. */
void es2_sim_poly_page_set(es2_sim_loop *l, uint32_t page, uint32_t *eax) {
    uint32_t t = r32(l, VA_PAGE_TABLE);
    w32(l, t + 0x804u, page);
    *eax = t;
}
/* 0x469ddc(t, p): [t+0x800] = 0, [t+0x804] = p, 256 pairs {0, 0x100}. EAX t. */
void es2_sim_poly_page_init(es2_sim_loop *l, uint32_t table, uint32_t page, uint32_t *eax) {
    w32(l, table + 0x800u, 0);
    w32(l, table + 0x804u, page);
    for (uint32_t i = 0; i < 0x100u; ++i) {
        w32(l, table + 8u * i, 0);
        w32(l, table + 8u * i + 4u, 0x100u);
    }
    *eax = table;
}

/* ======================================================================
 * Edge tables (listing 0x477d48..0x4785e7). */

/* The recip of a height or width: the table below 640, else 0x10000 / h
 * (edges, cdq idiv) or 0x47e068(1, w) (the same quotient). */
static uint32_t recip(const es2_sim_loop *l, int32_t h) {
    if (h < 0x280) return r32(l, VA_RECIP + 4u * (uint32_t)h);
    return (uint32_t)(0x10000 / h);
}
/* The perspective subdivision (flag 2) of one edge: the end points' u/w,
 * v/w and the midpoint's, then a quadratic's first and second steps per
 * row (rh = 1 / h). All in DATA 0x6c565c..0x6c56a7. */
static int quad_setup(es2_sim_loop *l, uint32_t s, uint32_t e, uint32_t rh) {
    const uint32_t Q = VA_QUAD;
    w32(l, Q + 0x00u, r32(l, s + 0xcu));
    w32(l, Q + 0x04u, r32(l, s + 0x10u));
    w32(l, Q + 0x08u, r32(l, s + 0x14u));
    w32(l, Q + 0x18u, r32(l, e + 0xcu));
    w32(l, Q + 0x1cu, r32(l, e + 0x10u));
    w32(l, Q + 0x20u, r32(l, e + 0x14u));
    w32(l, Q + 0x14u, r32(l, Q + 0x08u));
    w32(l, Q + 0x0cu, r32(l, Q + 0x00u));
    w32(l, Q + 0x10u, r32(l, Q + 0x04u));
    w32(l, Q + 0x14u, r32(l, Q + 0x14u) + r32(l, Q + 0x20u));
    w32(l, Q + 0x0cu, r32(l, Q + 0x0cu) + r32(l, Q + 0x18u));
    w32(l, Q + 0x10u, r32(l, Q + 0x10u) + r32(l, Q + 0x1cu));
    w32(l, Q + 0x14u, sar(r32(l, Q + 0x14u), 1));
    w32(l, Q + 0x0cu, sar(r32(l, Q + 0x0cu), 1));
    w32(l, Q + 0x10u, sar(r32(l, Q + 0x10u), 1));
    /* {u, v, w} at +0 (A), +0xc (M), +0x18 (B) -> u/w, v/w at +0x24 (A),
     * +0x2c (M), +0x34 (B); a point with w 0 keeps its old quotients. */
    static const uint32_t src[3] = {0x00u, 0x18u, 0x0cu}, dst[3] = {0x24u, 0x34u, 0x2cu};
    for (unsigned i = 0; i < 3u; ++i) {
        int32_t w = s32(l, Q + src[i] + 8u);
        if (w == 0) continue;
        uint32_t q;
        if (div16(l, "edge perspective", s32(l, Q + src[i]), w, &q)) return 1;
        w32(l, Q + dst[i], q);
        if (div16(l, "edge perspective", s32(l, Q + src[i] + 4u), w, &q)) return 1;
        w32(l, Q + dst[i] + 4u, q);
    }
    uint32_t b0 = ((r32(l, Q + 0x24u) + r32(l, Q + 0x34u)) - 2u * r32(l, Q + 0x2cu)) * 2u;
    uint32_t b1 = ((r32(l, Q + 0x28u) + r32(l, Q + 0x38u)) - 2u * r32(l, Q + 0x30u)) * 2u;
    w32(l, Q + 0x3cu, mul16((r32(l, Q + 0x34u) - r32(l, Q + 0x24u)) - b0, rh));
    w32(l, Q + 0x40u, mul16((r32(l, Q + 0x38u) - r32(l, Q + 0x28u)) - b1, rh));
    w32(l, Q + 0x44u, mul16(mul16(b0, rh), rh));
    w32(l, Q + 0x48u, mul16(mul16(b1, rh), rh));
    w32(l, Q + 0x3cu, r32(l, Q + 0x3cu) + r32(l, Q + 0x44u));
    w32(l, Q + 0x40u, r32(l, Q + 0x40u) + r32(l, Q + 0x48u));
    w32(l, Q + 0x44u, r32(l, Q + 0x44u) << 1);
    w32(l, Q + 0x48u, r32(l, Q + 0x48u) << 1);
    return 0;
}
/* 0x477d48(n, verts, flags): the polygon's two edge chains as per-row
 * tables. Consecutive duplicates (and copies of the first vertex) are
 * dropped into 0x6c53a0; fewer than 3 left: rows 0. The top vertex (min
 * y; ties: larger x) starts chain 1 forward, the top with ties to the
 * smaller x starts chain 2 backward, each until a vertex at max y. Each
 * chain's edges are stepped per row into 0x6bbd90 / 0x6c0890 from row
 * max(top, [GC+0x214]): an edge starting at or below [GC+0x21c] ends its
 * chain, one ending at or above [GC+0x214] is skipped. EAX 0x6c5390 =
 * {first row, chain 2's rows, 0x6c0890 (left), 0x6bbd90 (right)}. */
int es2_sim_poly_edges(es2_sim_loop *l, int32_t n, uint32_t verts, uint32_t flags, uint32_t *eax) {
    uint32_t gc = r32(l, VA_GC);
    int f4 = (flags & 4u) != 0, f1 = (flags & 1u) != 0, f2 = (flags & 2u) != 0, f8 = (flags & 8u) != 0;
    *eax = VA_RESULT;
    copy7(l, VA_DEDUP, verts);
    uint32_t last = VA_DEDUP, p = verts + 28u;
    int32_t count = 1;
    for (int32_t i = 1; i < n; ++i, p += 28u) {
        uint32_t x = r32(l, p), y = r32(l, p + 4u);
        if (x == r32(l, last) && y == r32(l, last + 4u)) continue;
        if (x == r32(l, VA_DEDUP) && y == r32(l, VA_DEDUP + 4u)) continue;
        last += 28u;
        copy7(l, last, p);
        ++count;
    }
    if (count < 3) {
        w32(l, VA_RESULT + 4u, 0);
        return 0;
    }
    int32_t top = -1, top2 = -1, ymin = 1000000, ymin2 = 1000000, xa = 0, xb = 0, ymax = -1000000;
    for (int32_t i = 0; i < count; ++i) {
        uint32_t v = VA_DEDUP + 28u * (uint32_t)i;
        int32_t x = s32(l, v), y = s32(l, v + 4u);
        if (y >= 1000000 || y <= -1000000)
            return refuse(l, 0x00477e53u, "edge tables", "a vertex y outside (-1000000, 1000000): the original compares an uninitialised local or never finds its bottom");
        if (y < ymin || (y == ymin && x > xa)) { top = i; xa = x; ymin = y; }
        if (y < ymin2 || (y == ymin2 && x < xb)) { top2 = i; xb = x; ymin2 = y; }
        if (y > ymax) ymax = y;
    }
    if (f2 && !f1) return refuse(l, 0x00478449u, "edge tables", "flag 2 without flag 1 (uninitialised accumulators)");
    /* The chains. */
    uint32_t dst = VA_CHAIN1, cur;
    int32_t idx = top, c1 = 0, c2 = 0;
    do {
        copy7(l, dst, VA_DEDUP + 28u * (uint32_t)idx);
        if (++idx == count) idx = 0;
        ++c1;
        cur = dst;
        dst += 28u;
    } while (s32(l, cur + 4u) != ymax);
    dst = VA_CHAIN2;
    idx = top2;
    do {
        copy7(l, dst, VA_DEDUP + 28u * (uint32_t)idx);
        if (--idx < 0) idx = count - 1;
        ++c2;
        cur = dst;
        dst += 28u;
    } while (s32(l, cur + 4u) != ymax);
    int32_t first = ymin;
    if (s32(l, gc + 0x214u) > first) first = s32(l, gc + 0x214u);
    unsigned sh = f2 ? 10u : 8u;
    int32_t rows = 0;
    for (unsigned pass = 0; pass < 2u; ++pass) {
        uint32_t base = pass ? VA_EDGE2 : VA_EDGE1, s = pass ? VA_CHAIN2 : VA_CHAIN1;
        uint32_t left = (uint32_t)(pass ? c2 : c1);
        rows = 0;
        for (; --left != 0; s += 28u) {
            uint32_t e = s + 28u;
            if (s32(l, gc + 0x21cu) <= s32(l, s + 4u)) break;
            if (s32(l, e + 4u) <= s32(l, gc + 0x214u)) continue;
            int32_t h = (int32_t)(r32(l, e + 4u) - r32(l, s + 4u));
            if (h <= 0) continue;
            uint32_t rc = recip(l, h);
            uint32_t u0 = 0, v0 = 0, w0 = 0, du = 0, dv = 0, dw = 0, ulo = 0, vlo = 0, duhi = 0, dvhi = 0;
            if (f1) {
                uint32_t a8 = rc << sh;
                u0 = r32(l, s + 0xcu); v0 = r32(l, s + 0x10u); w0 = r32(l, s + 0x14u);
                du = mul16(r32(l, e + 0xcu) - u0, a8);
                dv = mul16(r32(l, e + 0x10u) - v0, a8);
                dw = mul16(r32(l, e + 0x14u) - w0, a8);
                w0 <<= sh;
                if (f2) {
                    ulo = u0 << sh; u0 = sar(u0, 32u - sh);
                    vlo = v0 << sh; v0 = sar(v0, 32u - sh);
                    duhi = (int32_t)du < 0 ? 0xffffffffu : 0;
                    dvhi = (int32_t)dv < 0 ? 0xffffffffu : 0;
                } else {
                    u0 <<= sh; v0 <<= sh;
                }
            }
            uint32_t z = 0, dz = 0, sv = 0, ds = 0;
            if (f8) {
                z = r32(l, s + 8u);
                dz = sar(imul32(r32(l, e + 8u) - z, rc), 8);
                z <<= 8;
            }
            if (f4) {
                sv = r32(l, s + 0x18u);
                ds = mul16(r32(l, e + 0x18u) - sv, rc);
            }
            uint32_t x = r32(l, s), dx = imul32(r32(l, e) - x, rc);
            x <<= 16;
            uint32_t row = base + 0x28u * (r32(l, s + 4u) - (uint32_t)first);
            h = (int32_t)(r32(l, e + 4u) - r32(l, s + 4u));
            if (f2) {
                uint32_t rh;
                if (h < 0x280) rh = r32(l, VA_RECIP + 4u * (uint32_t)h);
                else if (div16(l, "edge tables", 1, h, &rh)) return 1;
                if (quad_setup(l, s, e, rh)) return 1;
            }
            for (int32_t i = 0; i < h; ++i, row += 0x28u) {
                if (f2) {
                    uint32_t t;
                    w32(l, row + 8u, ulo); w32(l, row + 0x20u, u0);
                    t = ulo + du; u0 += duhi + (t < ulo ? 1u : 0u); ulo = t;
                    w32(l, row + 0xcu, vlo); w32(l, row + 0x24u, v0);
                    t = vlo + dv; v0 += dvhi + (t < vlo ? 1u : 0u); vlo = t;
                    w32(l, row + 0x10u, w0); w0 += dw;
                    w32(l, row + 0x18u, r32(l, VA_QUAD + 0x24u));
                    w32(l, row + 0x1cu, r32(l, VA_QUAD + 0x28u));
                    w32(l, VA_QUAD + 0x24u, r32(l, VA_QUAD + 0x24u) + r32(l, VA_QUAD + 0x3cu));
                    w32(l, VA_QUAD + 0x28u, r32(l, VA_QUAD + 0x28u) + r32(l, VA_QUAD + 0x40u));
                    w32(l, VA_QUAD + 0x3cu, r32(l, VA_QUAD + 0x3cu) + r32(l, VA_QUAD + 0x44u));
                    w32(l, VA_QUAD + 0x40u, r32(l, VA_QUAD + 0x40u) + r32(l, VA_QUAD + 0x48u));
                } else if (f1) {
                    w32(l, row + 8u, u0);
                    w32(l, row + 0xcu, v0);
                    u0 += du; v0 += dv;
                }
                if (f8) { w32(l, row + 4u, sar(z, 4)); z += dz; }
                if (f4) { w32(l, row + 0x14u, sv); sv += ds; }
                w32(l, row, sar(x, 16));
                x += dx;
                ++rows;
            }
        }
    }
    w32(l, VA_RESULT, (uint32_t)first);
    w32(l, VA_RESULT + 4u, (uint32_t)rows);
    w32(l, VA_RESULT + 8u, VA_EDGE2);
    w32(l, VA_RESULT + 0xcu, VA_EDGE1);
    return 0;
}

/* ======================================================================
 * The span loops (listing 0x46ac14..0x46b180) as register machines. */

typedef struct sregs { uint32_t eax, ebx, ecx, edx, esi, ebp; } sregs;
static void set_lo(uint32_t *r, uint32_t v) { *r = (*r & 0xffffff00u) | (v & 0xffu); }
static void set_hi(uint32_t *r, uint32_t v) { *r = (*r & 0xffff00ffu) | ((v & 0xffu) << 8); }
/* The common entry: ECX page << 16 with CL/CH the u/v integer bytes, ESI
 * and EBP the u/v steps, EAX/EDX the u/v fractions (unaligned dword loads
 * of the Q16 values, then the byte overlays). */
static void span_load(const es2_sim_loop *l, sregs *r) {
    r->ecx = r32(l, VA_SPAN_WORD) << 16;
    r->esi = r32(l, 0x004a0986u);
    r->ebp = r32(l, 0x004a098eu);
    r->eax = r32(l, 0x004a0976u);
    r->edx = r32(l, 0x004a097eu);
    set_lo(&r->edx, r8(l, 0x004a098au));
    set_hi(&r->edx, r8(l, 0x004a0992u));
    set_lo(&r->ecx, r8(l, 0x004a097au));
    set_hi(&r->ecx, r8(l, 0x004a0982u));
}
/* add edx, ebp; adc ch, dh; add eax, esi; adc cl, dl. */
static void span_step(sregs *r) {
    uint32_t t = r->edx + r->ebp, c = t < r->edx;
    r->edx = t;
    set_hi(&r->ecx, ((r->ecx >> 8) & 0xffu) + ((r->edx >> 8) & 0xffu) + c);
    t = r->eax + r->esi;
    c = t < r->eax;
    r->eax = t;
    set_lo(&r->ecx, (r->ecx & 0xffu) + (r->edx & 0xffu) + c);
}
static uint8_t texel(const es2_sim_loop *l, const sregs *r) { return r8(l, r->ecx + VA_TEXTURE); }
static int lt(uint32_t a, uint32_t b) { return (int32_t)a < (int32_t)b; }

/* 0x46ac14: texels straight to [dest, dest + width), two bytes per store
 * from an even address; [0x4a0994] nonzero: texel 0 keeps the pixel. */
static void span_texture(es2_sim_loop *l, sregs *r) {
    span_load(l, r);
    uint32_t edi = r32(l, VA_SPAN_DEST), end = edi + r32(l, VA_SPAN_WIDTH) - 2u, cond;
    w32(l, VA_SPAN_END, end);
    end &= ~1u;
    edi -= 2u;
    set_lo(&r->eax, r8(l, VA_SPAN_FLAGS));
    if (!(r->eax & 0xffu)) {
        if (edi & 1u) {
            set_lo(&r->eax, texel(l, r)); span_step(r);
            w8(l, edi + 2u, (uint8_t)r->eax);
            ++edi;
        }
        if (edi != end) do {
            set_lo(&r->eax, texel(l, r)); span_step(r);
            edi += 2u;
            set_hi(&r->eax, texel(l, r)); span_step(r);
            cond = lt(edi, end);
            w16(l, edi, (uint16_t)r->eax);
        } while (cond);
        if (edi != r32(l, VA_SPAN_END)) {
            edi += 2u;
            set_lo(&r->eax, texel(l, r));
            w8(l, edi, (uint8_t)r->eax);
        }
        return;
    }
    if (edi & 1u) {
        set_lo(&r->eax, texel(l, r)); span_step(r);
        if (r->eax & 0xffu) w8(l, edi + 2u, (uint8_t)r->eax);
        ++edi;
    }
    if (edi != end) do {
        set_lo(&r->eax, texel(l, r)); span_step(r);
        edi += 2u;
        if (!(r->eax & 0xffu)) set_lo(&r->eax, r8(l, edi));
        set_hi(&r->eax, texel(l, r)); span_step(r);
        cond = lt(edi, end);
        if (r->eax & 0xff00u) w16(l, edi, (uint16_t)r->eax);
        else w8(l, edi, (uint8_t)r->eax);
    } while (cond);
    if (edi != r32(l, VA_SPAN_END)) {
        edi += 2u;
        set_lo(&r->eax, texel(l, r));
        if (r->eax & 0xffu) w8(l, edi, (uint8_t)r->eax);
    }
}
/* 0x46ad4c: each texel through the shade row ([0x4a09ac] >> 16) of the
 * table [0x4a0998]; transparency tests the texel. [0x4a09cc] holds the
 * caller's stack pointer while ESP is the end (the own code stores its
 * call-area top there). */
static void span_flat(es2_sim_loop *l, sregs *r) {
    span_load(l, r);
    uint32_t edi = r32(l, VA_SPAN_DEST), end = edi + r32(l, VA_SPAN_WIDTH) - 2u, cond;
    w32(l, VA_SPAN_END, end);
    end &= ~1u;
    w32(l, VA_SPAN_SAVED_SP, l->stack_hi);
    edi -= 2u;
    r->ebx = ((r32(l, VA_SPAN_SHADE) >> 16) << 8) + r32(l, VA_SPAN_SHADES);
    set_lo(&r->eax, r8(l, VA_SPAN_FLAGS));
    if (!(r->eax & 0xffu)) {
        if (edi & 1u) {
            set_lo(&r->ebx, texel(l, r)); span_step(r);
            set_lo(&r->eax, r8(l, r->ebx));
            w8(l, edi + 2u, (uint8_t)r->eax);
            ++edi;
        }
        if (edi != end) do {
            set_lo(&r->ebx, texel(l, r)); span_step(r);
            set_lo(&r->eax, r8(l, r->ebx));
            set_lo(&r->ebx, texel(l, r)); span_step(r);
            edi += 2u;
            cond = lt(edi, end);
            set_hi(&r->eax, r8(l, r->ebx));
            w16(l, edi, (uint16_t)r->eax);
        } while (cond);
        if (edi != r32(l, VA_SPAN_END)) {
            set_lo(&r->ebx, texel(l, r));
            set_lo(&r->eax, r8(l, r->ebx));
            w8(l, edi + 2u, (uint8_t)r->eax);
        }
        return;
    }
    if (edi & 1u) {
        set_lo(&r->ebx, texel(l, r)); span_step(r);
        set_lo(&r->eax, r8(l, r->ebx));
        if (r->ebx & 0xffu) w8(l, edi + 2u, (uint8_t)r->eax);
        ++edi;
    }
    if (edi != end) do {
        set_lo(&r->ebx, texel(l, r)); span_step(r);
        set_lo(&r->eax, r8(l, r->ebx));
        if (!(r->ebx & 0xffu)) set_lo(&r->eax, r8(l, edi + 2u));
        set_lo(&r->ebx, texel(l, r)); span_step(r);
        edi += 2u;
        set_hi(&r->eax, r8(l, r->ebx));
        cond = lt(edi, end);
        if (r->ebx & 0xffu) w16(l, edi, (uint16_t)r->eax);
        else w8(l, edi, (uint8_t)r->eax);
    } while (cond);
    if (edi != r32(l, VA_SPAN_END)) {
        set_lo(&r->ebx, texel(l, r));
        if (r->ebx & 0xffu) {
            set_lo(&r->eax, r8(l, r->ebx));
            w8(l, edi + 2u, (uint8_t)r->eax);
        }
    }
}
/* 0x46aeb1: the shade accumulator S = [0x4a09ac] (step [0x4a09b0]) picks
 * row S >> 8 of [0x4a0998] per pixel (the texel replaces the low byte);
 * no transparency. ESP walks the destination ([0x4a09cc] as above). */
static void span_gouraud(es2_sim_loop *l, sregs *r) {
    span_load(l, r);
    uint32_t end = r32(l, VA_SPAN_DEST) + r32(l, VA_SPAN_WIDTH) - 2u, cond;
    w32(l, VA_SPAN_END2, end);
    w32(l, VA_SPAN_END, end & ~1u);
    uint32_t p = r32(l, VA_SPAN_DEST) - 2u;
    w32(l, VA_SPAN_SAVED_SP, l->stack_hi);
    uint32_t s = r32(l, VA_SPAN_SHADE);
    if (p & 1u) {
        r->ebx = (s >> 8) + r32(l, VA_SPAN_SHADES);
        s += r32(l, VA_SPAN_DSHADE);
        set_lo(&r->ebx, texel(l, r)); span_step(r);
        set_lo(&r->eax, r8(l, r->ebx));
        w8(l, p + 2u, (uint8_t)r->eax);
        ++p;
    }
    if (p != r32(l, VA_SPAN_END)) {
        r->ebx = s;
        do {
            r->ebx = (r->ebx >> 8) + r32(l, VA_SPAN_SHADES);
            s += r32(l, VA_SPAN_DSHADE);
            set_lo(&r->ebx, texel(l, r)); span_step(r);
            p += 2u;
            set_lo(&r->eax, r8(l, r->ebx));
            r->ebx = (s >> 8) + r32(l, VA_SPAN_SHADES);
            s += r32(l, VA_SPAN_DSHADE);
            set_lo(&r->ebx, texel(l, r)); span_step(r);
            set_hi(&r->eax, r8(l, r->ebx));
            cond = lt(p, r32(l, VA_SPAN_END));
            r->ebx = s;
            w16(l, p, (uint16_t)r->eax);
        } while (cond);
    }
    if (p != r32(l, VA_SPAN_END2)) {
        r->ebx = (s >> 8) + r32(l, VA_SPAN_SHADES);
        set_lo(&r->ebx, texel(l, r));
        set_lo(&r->eax, r8(l, r->ebx));
        w8(l, p + 2u, (uint8_t)r->eax);
    }
}
void es2_sim_poly_span(es2_sim_loop *l, unsigned kind, uint32_t *eax) {
    sregs r;
    memset(&r, 0, sizeof(r));
    if (kind == ES2_SIM_POLY_SPAN_GOURAUD) span_gouraud(l, &r);
    else if (kind == ES2_SIM_POLY_SPAN_FLAT) span_flat(l, &r);
    else span_texture(l, &r);
    *eax = r.eax;
}
/* 0x48b524 / 0x48b5af / 0x48b63a: the span [left, left + width) through
 * the row's run list ([0x4a593c] {start, length} pairs, [0x4a5938] of
 * them, sorted; the first run is read even when the count is 0): a run
 * starting right of the current left moves the destination, the u/v
 * accumulators (and the shade for Gouraud) by the difference; a run
 * reaching the span's end is the last. */
void es2_sim_poly_span_runs(es2_sim_loop *l, unsigned kind, uint32_t *eax) {
    uint32_t i = 0, runs = r32(l, VA_RUN_LIST), ecx = r32(l, VA_RUN_LEFT), edx = r32(l, VA_SPAN_WIDTH);
    uint32_t width = edx, a = 0;
    edx += ecx;
    do {
        a = r32(l, runs + 8u * i);
        uint32_t b = r32(l, runs + 8u * i + 4u) + a;
        if (!lt(a, edx)) break;
        if (lt(ecx, b)) {
            if (lt(ecx, a)) {
                uint32_t d = a - ecx;
                width -= d;
                w32(l, VA_SPAN_DEST, r32(l, VA_SPAN_DEST) + d);
                w32(l, VA_SPAN_U, r32(l, VA_SPAN_U) + imul32(r32(l, VA_SPAN_DU), d));
                w32(l, VA_SPAN_V, r32(l, VA_SPAN_V) + imul32(r32(l, VA_SPAN_DV), d));
                if (kind == ES2_SIM_POLY_SPAN_GOURAUD) w32(l, VA_SPAN_SHADE, r32(l, VA_SPAN_SHADE) + imul32(r32(l, VA_SPAN_DSHADE), d));
                ecx = r32(l, runs + 8u * i);
            }
            if (lt(b, edx)) b -= edx;
            else { b = 0; i = r32(l, VA_RUN_COUNT); }
            w32(l, VA_SPAN_WIDTH, b + width);
            uint32_t ignored;
            es2_sim_poly_span(l, kind, &ignored);
        }
        ++i;
    } while (lt(i, r32(l, VA_RUN_COUNT)));
    *eax = a;
}
/* 0x48b854: the depth test of one span: the depth buffer row from
 * [0x4a5adc], z from [0x4a5ae0] stepping [0x4a5ae4], [0x4a5ae8] pixels.
 * Each run of pixels whose z is below the buffer's (unsigned) is written
 * into the buffer and recorded as {x, length} at 0x4a5944 (x from
 * [0x4a5ad8]); [0x4a5938] the count, [0x4a593c] = 0x4a5944. The search
 * reads four buffer dwords per step and may read up to three past the
 * span; a hit past it ends the search. */
void es2_sim_poly_depth_runs(es2_sim_loop *l, uint32_t *eax) {
    uint32_t edi = r32(l, VA_DEPTH_PTR), z = r32(l, VA_DEPTH_Z), dz = r32(l, VA_DEPTH_DZ), ecx = r32(l, VA_DEPTH_WIDTH), n = 0;
    for (;;) {
        z -= dz;
        int found = 0;
        for (;;) {
            z += dz; if (z < r32(l, edi)) { found = 1; break; }
            z += dz; if (z < r32(l, edi + 4u)) { edi += 4u; ecx -= 1u; break; }
            z += dz; if (z < r32(l, edi + 8u)) { edi += 8u; ecx -= 2u; break; }
            z += dz; if (z < r32(l, edi + 12u)) { edi += 12u; ecx -= 3u; break; }
            edi += 16u;
            int more = (int32_t)ecx > 4;
            ecx -= 4u;
            if (!more) goto done;
        }
        if (!found && (int32_t)ecx <= 0) goto done;
        w32(l, VA_DEPTH_RUNS + 8u * n, ((edi - r32(l, VA_DEPTH_PTR)) >> 2) + r32(l, VA_DEPTH_X));
        uint32_t len = 0;
        do {
            if (z >= r32(l, edi)) break;
            w32(l, edi, z);
            z += dz; edi += 4u; ++len;
        } while (--ecx != 0);
        w32(l, VA_DEPTH_RUNS + 8u * n + 4u, len);
        ++n;
        if (ecx == 0) break;
    }
done:
    w32(l, VA_RUN_COUNT, n);
    w32(l, VA_RUN_LIST, VA_DEPTH_RUNS);
    *eax = VA_DEPTH_RUNS;
}

/* ======================================================================
 * The fills (listing 0x46817c..0x46875e). */

/* 0x468414(n, verts, mode, word, shade*, flags): flag word 1 | 4 (mode 2)
 * | 8 (depth on); [0x4a0994] = flags; clip x into 0x4d4914, y into
 * 0x4d4b44, edge tables; nothing left: return. Mode 1: [shade] =
 * (shades - 1) [shade] + haze, [0x4a09ac] = [shade] << 8. Per row from
 * the first: width = right.x - left.x + 1 > 0: the span state (steps by
 * the width's recip), the depth runs (depth on), or the row's run list
 * ([GC+0x208] == 2, or depth on, with [0x4a5af8] set); a run list with
 * no runs skips the row; the span loop by mode (2 Gouraud, 1 flat, else
 * texture), through the runs when a list is in use. EAX 0 when a clip
 * left nothing, else 0x6c5390. */
int es2_sim_poly_fill(es2_sim_loop *l, int32_t n, uint32_t verts, int32_t mode, uint32_t word, uint32_t shade, uint32_t flags,
                      uint32_t *eax) {
    uint32_t fw = 1u | (mode == 2 ? 4u : 0) | (r32(l, VA_DEPTH_ON) ? 8u : 0), count, e;
    w32(l, VA_SPAN_FLAGS, flags);
    if (es2_sim_poly_clip_x(l, n, verts, VA_CLIP_X_OUT, fw, &count)) return 1;
    *eax = 0;
    if (!count) return 0;
    if (es2_sim_poly_clip_y(l, (int32_t)count, VA_CLIP_X_OUT, VA_CLIP_Y_OUT, fw, &count)) return 1;
    if (!count) return 0;
    if (es2_sim_poly_edges(l, (int32_t)count, VA_CLIP_Y_OUT, fw, &e)) return 1;
    *eax = e;
    if (r32(l, e + 4u) == 0) return 0;
    if (mode == 1) {
        w32(l, shade, imul32(r32(l, VA_SHADES) - 1u, r32(l, shade)));
        w32(l, shade, r32(l, shade) + r32(l, VA_HAZE));
        w32(l, VA_SPAN_SHADE, r32(l, shade) << 8);
    }
    uint32_t bits = r32(l, r32(l, VA_VIEW_ROWS)), pitch = r32(l, VA_VIEW_PITCH);
    uint32_t right = r32(l, e + 0xcu), left = r32(l, e + 8u), first = r32(l, e);
    uint32_t offset = imul32(first, pitch), tag = (first + r32(l, VA_DEPTH_TAG)) << 24;
    int runs = 1;
    if (!r32(l, VA_DEPTH_ON) && r32(l, r32(l, VA_GC) + 0x208u) != 2u) runs = 0;
    if (runs) {
        if (r32(l, VA_RUNS)) {
            uint32_t a = r32(l, r32(l, VA_RUNS));
            w32(l, VA_RUN_LIST, r32(l, a + 4u));
            w32(l, VA_RUN_COUNT, r32(l, a));
        } else {
            w32(l, VA_RUN_COUNT, 0);
            runs = 0;
        }
    }
    w32(l, VA_SPAN_WORD, word);
    unsigned kind = mode == 2 ? ES2_SIM_POLY_SPAN_GOURAUD : mode == 1 ? ES2_SIM_POLY_SPAN_FLAT : ES2_SIM_POLY_SPAN_TEXTURE;
    for (int32_t row = 0; s32(l, e + 4u) > row; ++row) {
        int32_t lx = s32(l, left);
        int32_t w = (int32_t)(r32(l, right) - (uint32_t)lx + 1u);
        if (w > 0) {
            uint32_t rc, ignored;
            if (w < 0x280) rc = r32(l, VA_RECIP + 4u * (uint32_t)w);
            else if (div16(l, "polygon fill", 1, w, &rc)) return 1;
            uint32_t pos = offset + (uint32_t)lx;
            if (r32(l, VA_DEPTH_ON)) {
                w32(l, VA_DEPTH_PTR, (pos << 2) + r32(l, VA_DEPTH_BUFFER));
                w32(l, VA_DEPTH_X, (uint32_t)lx);
                w32(l, VA_DEPTH_Z, r32(l, left + 4u) | tag);
                w32(l, VA_DEPTH_DZ, mul16(r32(l, right + 4u) - r32(l, left + 4u), rc));
                w32(l, VA_DEPTH_WIDTH, (uint32_t)w);
                es2_sim_poly_depth_runs(l, &ignored);
            } else if (runs) {
                uint32_t index = r32(l, e) + (uint32_t)row;
                if (r32(l, VA_RUNS)) {
                    uint32_t a = r32(l, r32(l, VA_RUNS)) + 8u * index;
                    w32(l, VA_RUN_LIST, r32(l, a + 4u));
                    w32(l, VA_RUN_COUNT, r32(l, a));
                } else {
                    w32(l, VA_RUN_COUNT, 0);
                }
            }
            if (!runs || r32(l, VA_RUN_COUNT)) {
                w32(l, VA_SPAN_WIDTH, (uint32_t)w);
                w32(l, VA_SPAN_DEST, bits + pos);
                w32(l, VA_SPAN_DU, mul16(r32(l, right + 8u) - r32(l, left + 8u), rc));
                w32(l, VA_SPAN_DV, mul16(r32(l, right + 0xcu) - r32(l, left + 0xcu), rc));
                w32(l, VA_SPAN_U, r32(l, left + 8u));
                w32(l, VA_SPAN_V, r32(l, left + 0xcu));
                w32(l, VA_RUN_LEFT, (uint32_t)lx);
                if (mode == 2) {
                    uint32_t s0 = r32(l, left + 0x14u);
                    w32(l, VA_SPAN_SHADE, s0);
                    w32(l, VA_SPAN_DSHADE, mul16(r32(l, right + 0x14u) - s0, rc));
                }
                if (runs) es2_sim_poly_span_runs(l, kind, &ignored);
                else es2_sim_poly_span(l, kind, &ignored);
            }
        }
        left += 0x28u;
        right += 0x28u;
        if (r32(l, VA_DEPTH_ON)) tag += 0x1000000u;
        offset += pitch;
    }
    return 0;
}

/* 0x46817c(points, uv, n, word, shades, mode, transformed, flags): each
 * point into a 0x28-byte record at 0x4d4d74: the view-space point at +0x1c
 * (copied when `transformed`, else 0x48c57c); at or beyond the near
 * distance [0x6c602c]: projected (0x48c5fc, 0x48c6d0) into +0/+4; u, v
 * << 8 at +0xc/+0x10; depth on: +8 = the depth; mode 2: +0x18 = ((shades
 * - 1) shade + haze) << 8. Then the near-plane clip into 0x4d46e4 (28-byte
 * vertices; prev starts at the last record): inside vertices are copied,
 * crossings interpolated at r = (near - prev.depth) / (cur.depth -
 * prev.depth) and projected, +8 = near. Then 0x468414. */
int es2_sim_poly_fill_3d(es2_sim_loop *l, uint32_t points, uint32_t uv, int32_t n, uint32_t word, uint32_t shades, int32_t mode,
                         uint32_t transformed, uint32_t flags, uint32_t *eax) {
    int32_t near_d = s32(l, VA_NEAR), count = 0;
    uint32_t top = l->stack_hi, loc = frame_push(l, 8u);
    if (!loc) return refuse(l, 0x0046817cu, "3D polygon", "the own code's call area is exhausted");
    uint32_t rec = VA_RECORDS, sp = shades;
    for (int32_t i = 0; i < n; ++i, sp += 4u, rec += 0x28u, uv += 8u, points += 0xcu) {
        if (transformed) {
            w32(l, rec + 0x24u, r32(l, points + 8u));
            w32(l, rec + 0x1cu, r32(l, points));
            w32(l, rec + 0x20u, r32(l, points + 4u));
        } else {
            uint32_t a[2] = {points, rec + 0x1cu};
            if (call(l, VA_VIEW_TRANSFORM, a, 2, NULL)) { frame_pop(l, top); return 1; }
        }
        if (s32(l, rec + 0x20u) >= near_d) {
            uint32_t a[2] = {rec + 0x1cu, loc}, b[2] = {loc, loc};
            if (call(l, VA_PROJECT, a, 2, NULL) || call(l, VA_TO_SCREEN, b, 2, NULL)) { frame_pop(l, top); return 1; }
            w32(l, rec, r32(l, loc));
            w32(l, rec + 4u, r32(l, loc + 4u));
        }
        w32(l, rec + 0xcu, r32(l, uv) << 8);
        w32(l, rec + 0x10u, r32(l, uv + 4u) << 8);
        if (r32(l, VA_DEPTH_ON)) w32(l, rec + 8u, r32(l, rec + 0x20u));
        if (mode == 2) {
            w32(l, rec + 0x18u, r32(l, sp));
            w32(l, rec + 0x18u, imul32(r32(l, VA_SHADES) - 1u, r32(l, rec + 0x18u)));
            w32(l, rec + 0x18u, r32(l, rec + 0x18u) + r32(l, VA_HAZE));
            w32(l, rec + 0x18u, r32(l, rec + 0x18u) << 8);
        }
    }
    uint32_t cur = VA_RECORDS, prev = VA_RECORDS - 0x28u + 0x28u * (uint32_t)n;
    for (int32_t i = 0; i < n; ++i, prev = cur, cur += 0x28u) {
        if (s32(l, prev + 0x20u) >= near_d) {
            copy7(l, VA_NEAR_OUT + 28u * (uint32_t)count, prev);
            ++count;
            if (s32(l, cur + 0x20u) >= near_d) continue;
        } else if (s32(l, cur + 0x20u) < near_d) {
            continue;
        }
        uint32_t o = VA_NEAR_OUT + 28u * (uint32_t)count, r;
        ++count;
        uint32_t pd = r32(l, prev + 0x20u);
        if (div16(l, "3D polygon near_d clip", (int32_t)((uint32_t)near_d - pd), (int32_t)(r32(l, cur + 0x20u) - pd), &r)) { frame_pop(l, top); return 1; }
        w32(l, loc, mul16(r, r32(l, cur + 0x1cu) - r32(l, prev + 0x1cu)) + r32(l, prev + 0x1cu));
        w32(l, loc + 4u, mul16(r, r32(l, cur + 0x24u) - r32(l, prev + 0x24u)) + r32(l, prev + 0x24u));
        uint32_t b[2] = {loc, loc};
        if (call(l, VA_TO_SCREEN, b, 2, NULL)) { frame_pop(l, top); return 1; }
        w32(l, o, r32(l, loc));
        w32(l, o + 4u, r32(l, loc + 4u));
        w32(l, o + 8u, (uint32_t)near_d);
        w32(l, o + 0xcu, mul16(r, r32(l, cur + 0xcu) - r32(l, prev + 0xcu)) + r32(l, prev + 0xcu));
        w32(l, o + 0x10u, mul16(r, r32(l, cur + 0x10u) - r32(l, prev + 0x10u)) + r32(l, prev + 0x10u));
        if (mode == 2) w32(l, o + 0x18u, mul16(r, r32(l, cur + 0x18u) - r32(l, prev + 0x18u)) + r32(l, prev + 0x18u));
    }
    frame_pop(l, top);
    return es2_sim_poly_fill(l, count, VA_NEAR_OUT, mode, word, shades, flags, eax);
}

/* ======================================================================
 * The perspective fill (listing 0x468760..0x4690ec). */

#define VA_FOCAL_SHIFT 0x006c6028u  /* [cam+0x1a]: the focal shift */
#define VA_FAR_FLAG 0x0049f250u     /* nonzero: u/w, v/w from the edge entry +0x18/+0x1c */

/* The perspective fill's locals in a guest frame at their ebp offsets: the
 * 5-dword perspective values {u lo, u hi, v lo, v hi, w} A (-0xd4), D
 * (-0xe8), B (-0x110), S (-0x124); the u/w, v/w pairs P (-0x64), Q
 * (-0x74); the projection pair (-0xc). */
#define PF_FRAME 0x124u
#define PF_LOC 0x0cu
#define PF_P 0x64u
#define PF_Q 0x74u
#define PF_A 0xd4u
#define PF_D 0xe8u
#define PF_B 0x110u
#define PF_S 0x124u

/* An edge entry's {u lo +8, u hi +0x20, v lo +0xc, v hi +0x24, w +0x10}
 * as a perspective value. */
static void pf_load(es2_sim_loop *l, uint32_t dst, uint32_t entry) {
    w32(l, dst, r32(l, entry + 8u));
    w32(l, dst + 8u, r32(l, entry + 0xcu));
    w32(l, dst + 0x10u, r32(l, entry + 0x10u));
    w32(l, dst + 4u, r32(l, entry + 0x20u));
    w32(l, dst + 0xcu, r32(l, entry + 0x24u));
}
static void pf_span(es2_sim_loop *l, int runs) {
    uint32_t ignored;
    if (runs) es2_sim_poly_span_runs(l, ES2_SIM_POLY_SPAN_FLAT, &ignored);
    else es2_sim_poly_span(l, ES2_SIM_POLY_SPAN_FLAT, &ignored);
}

/* 0x468760(points, uv, n, page, shades, mode, transformed, flags): the
 * perspective-correct textured fill through the flat-shade span loop
 * 0x46ad4c (runs 0x48b5af). Records at 0x4d4d74 as 0x46817c, but u, v <<
 * 16, +8 = the depth always, and for points at or beyond the near distance
 * +0x14 = 0x47e068(1 << [0x6c6028], depth). Near clip into 0x4d46e4:
 * inside vertices copied with u, v multiplied by +0x14 (0x47e08d);
 * crossings interpolated from the unmultiplied record u/v, +0x14 =
 * 0x10000. Flag word 0xb | 4 (mode 2) | 8 (depth on); clip x, clip y, the
 * edge tables (flag 2: 64-bit u/v). Per row: width = right.x - left.x (no
 * + 1); runs and depth runs as 0x468414; the u/v/w differences across the
 * row (64-bit); u/w, v/w at the left end (0x471d26, or the entry's
 * +0x18/+0x1c when [0x49f250]); steps of 1 << s pixels (s 10 for equal w,
 * else 4..7 by |dw| sar 12 above 3000, 1000, 300): each step's end divided
 * again, the span's du/dv = (Q - P) sar s; the remainder steps by the
 * recip table at the remaining width (no bound check). EAX: 0 (a clip left
 * nothing), the edge result (no rows), the page (no positive row count)
 * or the pitch (after the rows). P and Q are stack values kept across
 * rows: a read before their first definition (0x471d26 skips a zero w) is
 * refused. */
int es2_sim_poly_fill_persp(es2_sim_loop *l, uint32_t points, uint32_t uv, int32_t n, uint32_t page, uint32_t shades, int32_t mode,
                            uint32_t transformed, uint32_t flags, uint32_t *eax) {
    int32_t near_d = s32(l, VA_NEAR);
    uint32_t shift = r32(l, VA_FOCAL_SHIFT), count = 0, ignored;
    uint32_t top = l->stack_hi, f = frame_push(l, PF_FRAME);
    if (!f) return refuse(l, 0x00468760u, "perspective fill", "the own code's call area is exhausted");
    f += PF_FRAME; /* ebp: the locals at f - offset */
    int undefined = 0;
#define PF_FAIL() do { frame_pop(l, top); return 1; } while (0)
    w32(l, VA_SPAN_FLAGS, flags);
    uint32_t rec = VA_RECORDS;
    for (int32_t i = 0; i < n; ++i, rec += 0x28u, points += 0xcu, uv += 8u) {
        if (transformed) {
            w32(l, rec + 0x24u, r32(l, points + 8u));
            w32(l, rec + 0x1cu, r32(l, points));
            w32(l, rec + 0x20u, r32(l, points + 4u));
        } else {
            uint32_t a[2] = {points, rec + 0x1cu};
            if (call(l, VA_VIEW_TRANSFORM, a, 2, NULL)) PF_FAIL();
        }
        if (s32(l, rec + 0x20u) >= near_d) {
            uint32_t a[2] = {rec + 0x1cu, f - PF_LOC}, b[2] = {f - PF_LOC, f - PF_LOC}, q;
            if (call(l, VA_PROJECT, a, 2, NULL) || call(l, VA_TO_SCREEN, b, 2, NULL)) PF_FAIL();
            w32(l, rec, r32(l, f - PF_LOC));
            w32(l, rec + 4u, r32(l, f - PF_LOC + 4u));
            if (div16(l, "perspective fill", (int32_t)(1u << (shift & 31u)), s32(l, rec + 0x20u), &q)) PF_FAIL();
            w32(l, rec + 0x14u, q);
        }
        w32(l, rec + 0xcu, r32(l, uv) << 16);
        w32(l, rec + 0x10u, r32(l, uv + 4u) << 16);
        w32(l, rec + 8u, r32(l, rec + 0x20u));
        if (mode == 2) {
            w32(l, rec + 0x18u, r32(l, shades + 4u * (uint32_t)i));
            w32(l, rec + 0x18u, imul32(r32(l, VA_SHADES) - 1u, r32(l, rec + 0x18u)));
            w32(l, rec + 0x18u, r32(l, rec + 0x18u) + r32(l, VA_HAZE));
            w32(l, rec + 0x18u, r32(l, rec + 0x18u) << 8);
        }
    }
    uint32_t cur = VA_RECORDS, prev = VA_RECORDS - 0x28u + 0x28u * (uint32_t)n;
    for (int32_t i = 0; i < n; ++i, prev = cur, cur += 0x28u) {
        if (s32(l, prev + 0x20u) >= near_d) {
            uint32_t o = VA_NEAR_OUT + 28u * count++;
            copy7(l, o, prev);
            w32(l, o + 0xcu, mul16(r32(l, o + 0xcu), r32(l, o + 0x14u)));
            w32(l, o + 0x10u, mul16(r32(l, o + 0x10u), r32(l, o + 0x14u)));
            if (s32(l, cur + 0x20u) >= near_d) continue;
        } else if (s32(l, cur + 0x20u) < near_d) {
            continue;
        }
        uint32_t pd = r32(l, prev + 0x20u), r, loc = f - PF_LOC;
        uint32_t o = VA_NEAR_OUT + 28u * count++;
        if (div16(l, "perspective fill near clip", (int32_t)((uint32_t)near_d - pd), (int32_t)(r32(l, cur + 0x20u) - pd), &r)) PF_FAIL();
        w32(l, loc, mul16(r, r32(l, cur + 0x1cu) - r32(l, prev + 0x1cu)) + r32(l, prev + 0x1cu));
        w32(l, loc + 4u, mul16(r, r32(l, cur + 0x24u) - r32(l, prev + 0x24u)) + r32(l, prev + 0x24u));
        uint32_t b[2] = {loc, loc};
        if (call(l, VA_TO_SCREEN, b, 2, NULL)) PF_FAIL();
        w32(l, o, r32(l, loc));
        w32(l, o + 4u, r32(l, loc + 4u));
        w32(l, o + 8u, (uint32_t)near_d);
        w32(l, o + 0x14u, 0x10000u);
        w32(l, o + 0xcu, mul16(r, r32(l, cur + 0xcu) - r32(l, prev + 0xcu)) + r32(l, prev + 0xcu));
        w32(l, o + 0x10u, mul16(r, r32(l, cur + 0x10u) - r32(l, prev + 0x10u)) + r32(l, prev + 0x10u));
        if (mode == 2) w32(l, o + 0x18u, mul16(r, r32(l, cur + 0x18u) - r32(l, prev + 0x18u)) + r32(l, prev + 0x18u));
    }
    uint32_t fw = 0xbu | (mode == 2 ? 4u : 0) | (r32(l, VA_DEPTH_ON) ? 8u : 0), e;
    if (es2_sim_poly_clip_x(l, (int32_t)count, VA_NEAR_OUT, VA_CLIP_X_OUT, fw, &count)) PF_FAIL();
    *eax = count;
    if (!count) { frame_pop(l, top); return 0; }
    if (es2_sim_poly_clip_y(l, (int32_t)count, VA_CLIP_X_OUT, VA_CLIP_Y_OUT, fw, &count)) PF_FAIL();
    *eax = count;
    if (!count) { frame_pop(l, top); return 0; }
    if (es2_sim_poly_edges(l, (int32_t)count, VA_CLIP_Y_OUT, fw, &e)) PF_FAIL();
    *eax = e;
    if (r32(l, e + 4u) == 0) { frame_pop(l, top); return 0; }
    if (mode == 1) {
        w32(l, shades, imul32(r32(l, VA_SHADES) - 1u, r32(l, shades)));
        w32(l, shades, r32(l, shades) + r32(l, VA_HAZE));
        w32(l, VA_SPAN_SHADE, r32(l, shades) << 8);
    }
    uint32_t bits = r32(l, r32(l, VA_VIEW_ROWS)), pitch = r32(l, VA_VIEW_PITCH);
    uint32_t left = r32(l, e + 8u), right = r32(l, e + 0xcu);
    uint32_t offset = imul32(r32(l, e), pitch), tag = (r32(l, e) + r32(l, VA_DEPTH_TAG)) << 24;
    int runs = 1;
    if (!r32(l, VA_DEPTH_ON) && r32(l, r32(l, VA_GC) + 0x208u) != 2u) runs = 0;
    if (runs) {
        if (r32(l, VA_RUNS)) {
            uint32_t a = r32(l, r32(l, VA_RUNS));
            w32(l, VA_RUN_LIST, r32(l, a + 4u));
            w32(l, VA_RUN_COUNT, r32(l, a));
        } else {
            w32(l, VA_RUN_COUNT, 0);
            runs = 0;
        }
    }
    w32(l, VA_SPAN_WORD, page);
    *eax = page;
    int p_set = 0, q_set = 0;
    const uint32_t P = f - PF_P, Q = f - PF_Q, A = f - PF_A, D = f - PF_D, B = f - PF_B, S = f - PF_S;
    for (int32_t row = 0; !undefined && s32(l, e + 4u) > row; ++row) {
        int32_t lx = s32(l, left);
        int32_t w = (int32_t)(r32(l, right) - (uint32_t)lx);
        if (w > 0) {
            uint32_t pos = offset + (uint32_t)lx, rc;
            if (w < 0x280) rc = r32(l, VA_RECIP + 4u * (uint32_t)w);
            else if (div16(l, "perspective fill", 1, w, &rc)) PF_FAIL();
            if (r32(l, VA_DEPTH_ON)) {
                w32(l, VA_DEPTH_PTR, (pos << 2) + r32(l, VA_DEPTH_BUFFER));
                w32(l, VA_DEPTH_X, (uint32_t)lx);
                w32(l, VA_DEPTH_Z, r32(l, left + 4u) | tag);
                w32(l, VA_DEPTH_DZ, mul16(r32(l, right + 4u) - r32(l, left + 4u), rc));
                w32(l, VA_DEPTH_WIDTH, (uint32_t)w);
                es2_sim_poly_depth_runs(l, &ignored);
            } else if (runs) {
                uint32_t index = r32(l, e) + (uint32_t)row;
                if (r32(l, VA_RUNS)) {
                    uint32_t a = r32(l, r32(l, VA_RUNS)) + 8u * index;
                    w32(l, VA_RUN_LIST, r32(l, a + 4u));
                    w32(l, VA_RUN_COUNT, r32(l, a));
                } else {
                    w32(l, VA_RUN_COUNT, 0);
                }
            }
            if (!runs || r32(l, VA_RUN_COUNT)) {
                w32(l, VA_SPAN_DEST, bits + pos);
                pf_load(l, A, left);
                pf_load(l, D, right);
                es2_sim_poly_sub64(l, D, D + 4u, r32(l, A), r32(l, A + 4u));
                es2_sim_poly_sub64(l, D + 8u, D + 0xcu, r32(l, A + 8u), r32(l, A + 0xcu));
                w32(l, D + 0x10u, r32(l, D + 0x10u) - r32(l, A + 0x10u));
                if (r32(l, VA_FAR_FLAG)) {
                    w32(l, P, r32(l, left + 0x18u));
                    w32(l, P + 4u, r32(l, left + 0x1cu));
                    p_set = 1;
                } else {
                    if (r32(l, A + 0x10u)) p_set = 1;
                    if (divide_pair(l, P, A, &ignored)) PF_FAIL();
                }
                uint32_t s = 10u;
                int32_t lw = s32(l, left + 0x10u), rw = s32(l, right + 0x10u);
                if (lw != rw) {
                    int32_t d = (int32_t)sar(lw < rw ? (uint32_t)rw - (uint32_t)lw : (uint32_t)lw - (uint32_t)rw, 12);
                    s = d > 3000 ? 4u : d > 1000 ? 5u : d > 300 ? 6u : 7u;
                }
                int32_t step = 1 << s, rest = w;
                int dirty = 0;
                while (step <= rest) {
                    if (!dirty) {
                        if (divide_three(l, S, D, rest, s, &ignored)) PF_FAIL();
                        dirty = 1;
                    }
                    for (uint32_t k = 0; k < 0x14u; k += 4u) w32(l, B + k, r32(l, A + k));
                    es2_sim_poly_add64(l, B, B + 4u, r32(l, S), r32(l, S + 4u));
                    es2_sim_poly_add64(l, B + 8u, B + 0xcu, r32(l, S + 8u), r32(l, S + 0xcu));
                    w32(l, B + 0x10u, r32(l, B + 0x10u) + r32(l, S + 0x10u));
                    if (r32(l, B + 0x10u)) q_set = 1;
                    if (divide_pair(l, Q, B, &ignored)) PF_FAIL();
                    if (!p_set || !q_set) { undefined = 1; break; }
                    w32(l, VA_SPAN_WIDTH, (uint32_t)step);
                    w32(l, VA_SPAN_DU, sar(r32(l, Q) - r32(l, P), s));
                    w32(l, VA_SPAN_DV, sar(r32(l, Q + 4u) - r32(l, P + 4u), s));
                    w32(l, VA_SPAN_U, r32(l, P));
                    w32(l, VA_SPAN_V, r32(l, P + 4u));
                    w32(l, VA_RUN_LEFT, (uint32_t)lx);
                    for (uint32_t k = 0; k < 0x14u; k += 4u) w32(l, A + k, r32(l, B + k));
                    w32(l, P, r32(l, Q));
                    w32(l, P + 4u, r32(l, Q + 4u));
                    uint32_t saved = r32(l, VA_SPAN_DEST);
                    pf_span(l, runs);
                    lx += step;
                    rest -= step;
                    w32(l, VA_SPAN_DEST, saved + (uint32_t)step);
                }
                if (undefined) break;
                if (rest != 0) {
                    uint32_t rc2 = r32(l, VA_RECIP + 4u * (uint32_t)rest);
                    pf_load(l, B, right);
                    if (r32(l, VA_FAR_FLAG)) {
                        w32(l, Q, r32(l, right + 0x18u));
                        w32(l, Q + 4u, r32(l, right + 0x1cu));
                        q_set = 1;
                    } else {
                        if (r32(l, B + 0x10u)) q_set = 1;
                        if (divide_pair(l, Q, B, &ignored)) PF_FAIL();
                    }
                    if (!p_set || !q_set) { undefined = 1; break; }
                    w32(l, VA_SPAN_WIDTH, (uint32_t)rest);
                    w32(l, VA_SPAN_DU, mul16(r32(l, Q) - r32(l, P), rc2));
                    w32(l, VA_SPAN_DV, mul16(r32(l, Q + 4u) - r32(l, P + 4u), rc2));
                    w32(l, VA_SPAN_U, r32(l, P));
                    w32(l, VA_SPAN_V, r32(l, P + 4u));
                    w32(l, VA_RUN_LEFT, (uint32_t)lx);
                    pf_span(l, runs);
                }
            }
        }
        left += 0x28u;
        right += 0x28u;
        if (r32(l, VA_DEPTH_ON)) tag += 0x1000000u;
        offset += pitch;
        *eax = pitch;
    }
    frame_pop(l, top);
#undef PF_FAIL
    if (undefined) return refuse(l, 0x00468760u, "perspective fill", "u/w or v/w read before 0x471d26 defined it (a zero w: stack garbage)");
    return 0;
}

/* ======================================================================
 * The shade lookup 0x430e34(colour, level) (0x430e34..0x430e8c). T = [0x49b020]
 * (the current palette); n = int16 [T+0xc]; idx = colour & 0xff, 0 when idx >=
 * n (signed). n == 0: the result is 0 (EAX = EDX = 0, ECX = T). Otherwise E =
 * [T+0x10] + 8 idx (entries {pointer, int16 count}); i = 0x47e0a0(level,
 * count), the q8 product (low dword of the 64-bit product shifted right by 8;
 * computed here, the leaf has no row), and count - 1 when i equals count (other
 * values are not bounded); the result is int16 [[E] + 2 i] sign-extended into
 * EAX and EDX, ECX = [E]. */
#define VA_PALETTE 0x0049b020u
static uint32_t r16s(const es2_sim_loop *l, uint32_t a) {
    return (uint32_t)(int32_t)(int16_t)(uint16_t)(r8(l, a) | (uint32_t)r8(l, a + 1u) << 8);
}
void es2_sim_poly_shade(es2_sim_loop *l, uint32_t colour, uint32_t level, uint32_t *eax, uint32_t *edx, uint32_t *ecx) {
    uint32_t t = r32(l, VA_PALETTE);
    uint32_t n = r16s(l, t + 0xcu);
    uint32_t idx = colour & 0xffu;
    if (!((int32_t)idx < (int32_t)n)) idx = 0;
    *ecx = t;
    if ((n & 0xffffu) == 0) { *eax = *edx = 0; return; }
    uint32_t e = r32(l, t + 0x10u) + (idx << 3);
    uint32_t count = r16s(l, e + 4u);
    uint32_t i = (uint32_t)((uint64_t)((int64_t)(int32_t)level * (int32_t)count) >> 8);
    if (i == count) i = count - 1u;
    uint32_t table = r32(l, e);
    *ecx = table;
    *eax = *edx = r16s(l, table + 2u * i);
}

/* ======================================================================
 * The service rows (sim/dispatch/services/poly_fills.h). */

static int serve(es2_sim_service_call *sc, es2_sim_loop **l) {
    *l = es2_sim_loop_bound();
    if (!*l) { sc->refused = 1; sc->reason = "no main loop bound"; return 1; }
    return 0;
}
static void failed(es2_sim_service_call *sc) { sc->refused = 1; sc->reason = "a call inside the service was refused"; }
#define POLY_SERVICE(name, body) \
    void es2_sim_service_poly_##name(es2_sim_service_call *sc, void *user) { \
        es2_sim_loop *l; (void)user; \
        if (serve(sc, &l)) return; \
        uint32_t eax = sc->eax; /* the entry EAX: kept where the original leaves it */ \
        if (body) { failed(sc); return; } \
        sc->eax = eax; \
    }
#define S(i) (sc->slots[i])
#define I(i) ((int32_t)sc->slots[i])
POLY_SERVICE(div16, es2_sim_poly_div16(l, sc->va, I(0), I(1), &eax))
POLY_SERVICE(mul16, (eax = es2_sim_poly_mul16(I(0), I(1)), 0))
POLY_SERVICE(add64, (eax = S(2), es2_sim_poly_add64(l, S(0), S(1), S(2), S(3))))
POLY_SERVICE(sub64, (eax = S(2), es2_sim_poly_sub64(l, S(0), S(1), S(2), S(3))))
POLY_SERVICE(divide_pair, divide_pair(l, S(0), S(1), &eax))
POLY_SERVICE(divide_three, divide_three(l, S(0), S(1), I(2), S(3), &eax))
POLY_SERVICE(set_far, (es2_sim_poly_set_far(l, S(0), &eax), 0))
POLY_SERVICE(haze, es2_sim_poly_haze(l, I(0), &eax))
POLY_SERVICE(shade_colour, (es2_sim_poly_shade_colour(l, I(0), &eax), 0))
POLY_SERVICE(cut_x, es2_sim_poly_cut_x(l, I(0), S(1), S(2), S(3), S(4), &eax))
POLY_SERVICE(cut_y, es2_sim_poly_cut_y(l, I(0), S(1), S(2), S(3), S(4), &eax))
POLY_SERVICE(clip_x, es2_sim_poly_clip_x(l, I(0), S(1), S(2), S(3), &eax))
POLY_SERVICE(clip_y, es2_sim_poly_clip_y(l, I(0), S(1), S(2), S(3), &eax))
POLY_SERVICE(page_init, (es2_sim_poly_page_init(l, S(0), S(1), &eax), 0))
POLY_SERVICE(edges, es2_sim_poly_edges(l, I(0), S(1), S(2), &eax))
POLY_SERVICE(span_texture, (es2_sim_poly_span(l, ES2_SIM_POLY_SPAN_TEXTURE, &eax), 0))
POLY_SERVICE(span_flat, (es2_sim_poly_span(l, ES2_SIM_POLY_SPAN_FLAT, &eax), 0))
POLY_SERVICE(span_gouraud, (es2_sim_poly_span(l, ES2_SIM_POLY_SPAN_GOURAUD, &eax), 0))
POLY_SERVICE(runs_texture, (es2_sim_poly_span_runs(l, ES2_SIM_POLY_SPAN_TEXTURE, &eax), 0))
POLY_SERVICE(runs_flat, (es2_sim_poly_span_runs(l, ES2_SIM_POLY_SPAN_FLAT, &eax), 0))
POLY_SERVICE(runs_gouraud, (es2_sim_poly_span_runs(l, ES2_SIM_POLY_SPAN_GOURAUD, &eax), 0))
POLY_SERVICE(depth_runs, (es2_sim_poly_depth_runs(l, &eax), 0))
POLY_SERVICE(fill, es2_sim_poly_fill(l, I(0), S(1), I(2), S(3), S(4), S(5), &eax))
POLY_SERVICE(fill_3d, es2_sim_poly_fill_3d(l, S(0), S(1), I(2), S(3), S(4), I(5), S(6), S(7), &eax))
POLY_SERVICE(fill_persp, es2_sim_poly_fill_persp(l, S(0), S(1), I(2), S(3), S(4), I(5), S(6), S(7), &eax))
void es2_sim_service_poly_shade(es2_sim_service_call *sc, void *user) {
    es2_sim_loop *l;
    (void)user;
    if (serve(sc, &l)) return;
    es2_sim_poly_shade(l, sc->slots[0], sc->slots[1], &sc->eax, &sc->edx, &sc->ecx);
}
