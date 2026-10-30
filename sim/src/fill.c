/* The 2D polygon fill layer and the line of DBSIM.EXE's graphics library as own
 * code (rasterizer.md, "GL fill layer and line"; es2/sim/
 * fill.h). "Listing" = llvm-objdump -d -M intel of the reference DBSIM.EXE
 * (SHA-256 8561aa47...c4ad), ranges 0x483a04..0x484338 (line, line clip, line
 * spans, fill, reversed fill, orientation test), 0x484338..0x484720 (polygon
 * clipper), 0x484720..0x486703 (span builder with its unrolled
 * vertical/diagonal block 0x484df0..0x4866f0) and 0x48a3b0..0x48a43f (the BMP_8
 * line writer).
 *
 * C = [0x4a3614] the graphics context record: +0 region count, +4 region
 * entries, +0x204 clip mode (0 none, 1 the clip rect, other: regions),
 * +0x208 origin flag, +0x20c the clip rect, +0x21c the origin, +0x224 the
 * pen, +0x228 the fill. Guest locals whose address escapes (clip outputs,
 * point copies, the outline pen, the reversed texture table) live in a
 * guest frame laid out like the original's, so overflows the original
 * tolerates land on the same neighbours; overflows that reach a saved
 * register, a counter or a return address are refused. */
#include <string.h>
#include "es2/sim/fill.h"
#include "es2/sim/gfx.h"
#include "es2/sim/memory.h"
#include "es2/sim/dispatch.h"
#include "es2/sim/services.h"

#define VA_CONTEXT ES2_SIM_VA_GFX_CONTEXT
#define VA_VIEW_ROWS ES2_SIM_VA_GFX_VIEW_ROWS
#define VA_VIEW_PITCH ES2_SIM_VA_GFX_VIEW_PITCH
#define VA_SPANS_A ES2_SIM_VA_GFX_SPANS_A
#define VA_SPANS_B ES2_SIM_VA_GFX_SPANS_B
#define VA_RAMPS ES2_SIM_VA_GFX_RAMPS
#define VA_SLOTS ES2_SIM_VA_GFX_SLOTS
#define VA_SAVED_FRAME ES2_SIM_VA_FILL_SAVED_FRAME
#define VA_GOURAUD 0x00487accu          /* the Gouraud fill (kind 1) */
#define VA_LINE_BODY 0x0048a3b0u        /* BMP_8 slot 34 */
#define SLOT_POLYGON 33u                /* [0x4a5830]: kinds 5 and 7 */
#define SLOT_LINE 34u                   /* [0x4a5834] */
#define SPAN_ROWS ES2_SIM_GFX_SPAN_ROWS

/* ---- the guest view */
static uint32_t r32(const es2_sim_loop *l, uint32_t a) { return l->memory->read32(l->memory->context, a); }
static uint16_t r16(const es2_sim_loop *l, uint32_t a) { return l->memory->read16(l->memory->context, a); }
static void w32(const es2_sim_loop *l, uint32_t a, uint32_t v) { l->memory->write32(l->memory->context, a, v); }
static void w16(const es2_sim_loop *l, uint32_t a, uint16_t v) { l->memory->write16(l->memory->context, a, v); }
static void w8(const es2_sim_loop *l, uint32_t a, uint8_t v) { es2_sim_write8(l->memory, a, v); }
static int32_t s32(const es2_sim_loop *l, uint32_t a) { return (int32_t)r32(l, a); }
static int32_t add32(int32_t a, int32_t b) { return (int32_t)((uint32_t)a + (uint32_t)b); }
static int32_t sub32(int32_t a, int32_t b) { return (int32_t)((uint32_t)a - (uint32_t)b); }
static int32_t neg32(int32_t a) { return (int32_t)(0u - (uint32_t)a); }
static int32_t shl1(int32_t a) { return (int32_t)((uint32_t)a << 1); }
static int32_t abs32(int32_t a) { return a < 0 ? neg32(a) : a; }   /* cdq/xor/sub */

static int refuse(es2_sim_loop *l, uint32_t va, const char *name, const char *reason) {
    return es2_sim_loop_refuse(l, va, name, reason);
}
/* The original reads, writes or calls through NULL at va (es2_sim_loop_null_refuse). */
static int null_refuse(es2_sim_loop *l, uint32_t va, const char *name, const char *reason) {
    return es2_sim_loop_null_refuse(l, va, name, reason);
}
static int call(es2_sim_loop *l, uint32_t va, const uint32_t *slots, unsigned count, uint32_t *eax) {
    es2_sim_result r;
    if (es2_sim_loop_call(l, va, slots, count, &r)) return 1;
    if (eax) *eax = r.eax;
    return 0;
}
/* Zero-filled guest locals below the own call area; 0 when exhausted. */
static uint32_t frame_push(es2_sim_loop *l, uint32_t bytes) {
    bytes = (bytes + 3u) & ~3u;
    if (l->stack_hi - l->stack_lo < bytes + 0x400u) return 0;
    l->stack_hi -= bytes;
    for (uint32_t i = 0; i < bytes; i += 4u) w32(l, l->stack_hi + i, 0);
    return l->stack_hi;
}
static void frame_pop(es2_sim_loop *l, uint32_t top) { l->stack_hi = top; }

/* idiv of the 64-bit product a * b by d; #DE (refused) for d == 0 or a
 * quotient outside int32. */
static int cut(es2_sim_loop *l, uint32_t va, const char *name, int32_t a, int32_t b, int32_t d, int32_t *q) {
    int64_t p = (int64_t)a * (int64_t)b;
    if (d == 0) return es2_sim_loop_divide_refuse(l, va, (uint64_t)p, 0, 1, name, "#DE: idiv by 0");
    int64_t r = p / (int64_t)d;
    if (r > INT32_MAX || r < INT32_MIN)
        return es2_sim_loop_divide_refuse(l, va, (uint64_t)p, (uint32_t)d, 1, name, "#DE: the idiv quotient does not fit 32 bits");
    *q = (int32_t)r;
    return 0;
}

/* The colour word of a kind-0 pen or fill (0x483a40..0x483a88,
 * 0x483fa9..0x483ff1): high byte 0 the word, 4 the ramp table 0x4a4fa5,
 * any other: nothing is drawn (0). */
static int resolve_colour(const es2_sim_loop *l, uint32_t c, uint32_t *out) {
    uint32_t hi = c >> 24;
    if (hi == 0) { *out = c; return 1; }
    if (hi != 4u) return 0;
    uint32_t e = VA_RAMPS + ((uint32_t)(int32_t)(int16_t)c << 3);
    uint32_t i = ((((c >> 16) & 0xffu) * (uint32_t)(int32_t)(int16_t)r16(l, e + 4u)) >> 7) & 0xfffeu;
    *out = (uint32_t)(int32_t)(int16_t)r16(l, r32(l, e) + i);
    return 1;
}

/* A span writer read from the driver table ([0x4a57ac] slot 0 or
 * [0x4a57b0] slot 1) called as `call [ebp-x]`: through gfx.c's slot when
 * the pointer is still the table's, else through the dispatch. */
static int call_writer(es2_sim_loop *l, uint32_t target, uint32_t spans, uint32_t colour) {
    uint32_t args[2] = {spans, colour};
    for (unsigned i = 0; i < 2u; ++i)
        if (r32(l, VA_SLOTS + 4u * i) == target) return es2_sim_gfx_slot(l, i, args, 2u, NULL);
    return call(l, target, args, 2u, NULL);
}

/* ======================================================================
 * The polygon clipper 0x484338(n, points, out, rect): Sutherland-Hodgman
 * against x (left/right, into a local of 50 points), then y (top/bottom,
 * into out). EAX = the output count; n <= 1: EAX = n. */
#define CLIP_LOCAL 50u
int es2_sim_fill_clip(es2_sim_loop *l, uint32_t count, uint32_t points, uint32_t out, uint32_t rect, uint32_t *eax) {
    static const char *name = "fill_clip";
    const uint32_t va = 0x00484338u;
    if ((int32_t)count <= 1) { *eax = count; return 0; }
    int32_t lx = s32(l, rect), rx = s32(l, rect + 8u);
    int32_t mx[CLIP_LOCAL][2];
    uint32_t m = 0;
#define CLIP_EMIT(X, Y) do { \
        if (m >= CLIP_LOCAL) return refuse(l, va, name, "the x pass emits a 51st point (the original overwrites its counter)"); \
        mx[m][0] = (X); mx[m][1] = (Y); ++m; } while (0)
    /* prev = the last point, cur = the first; outcodes 1 left, 2 right. */
    uint32_t prev = points + 8u * (count - 1u), cur = points;
#define XCODE(x) ((x) < lx ? 1u : (x) > rx ? 2u : 0u)
    unsigned cl = XCODE(s32(l, prev));
    for (uint32_t k = count; k != 0; --k) {
        int32_t px = s32(l, prev), py = s32(l, prev + 4u), qx = s32(l, cur), qy = s32(l, cur + 4u);
        unsigned ch = XCODE(qx);
        int32_t q;
        if (!(cl | ch)) {
            CLIP_EMIT(qx, qy);
        } else if (!(cl & ch)) {
            if (cl) {   /* the cut at prev's bound, from prev */
                int32_t b = (cl & 1u) ? lx : rx;
                if (cut(l, va, name, sub32(b, px), sub32(qy, py), sub32(qx, px), &q)) return 1;
                CLIP_EMIT(b, add32(q, py));
            }
            if (ch) {   /* the cut at cur's bound, from cur */
                int32_t b = (ch & 1u) ? lx : rx;
                if (cut(l, va, name, sub32(b, qx), sub32(py, qy), sub32(px, qx), &q)) return 1;
                CLIP_EMIT(b, add32(q, qy));
            } else {
                CLIP_EMIT(qx, qy);
            }
        }
        prev = cur;
        cl = ch;
        cur += 8u;
    }
#undef XCODE
    int32_t ty = s32(l, rect + 4u), by = s32(l, rect + 12u);
#define YCODE(y) ((y) > by ? 4u : (y) < ty ? 8u : 0u)
    if (m == 0) { *eax = 0; return 0; }
    if (m == 1) {   /* 0x4846da: unreachable (every step emits 0, 1 or 2 points, and a closed walk 0 or >= 2) */
        if (YCODE(mx[0][1])) { *eax = 0; return 0; }
        w32(l, out, (uint32_t)mx[0][0]);
        w32(l, out + 4u, (uint32_t)mx[0][1]);
        *eax = 1;
        return 0;
    }
    uint32_t w = out;
#define OUT_EMIT(X, Y) do { w32(l, w, (uint32_t)(X)); w32(l, w + 4u, (uint32_t)(Y)); w += 8u; } while (0)
    unsigned ip = m - 1u;
    cl = YCODE(mx[ip][1]);
    for (unsigned ic = 0; ic < m; ++ic) {
        int32_t px = mx[ip][0], py = mx[ip][1], qx = mx[ic][0], qy = mx[ic][1];
        unsigned ch = YCODE(qy);
        int32_t q;
        if (!(cl | ch)) {
            OUT_EMIT(qx, qy);
        } else if (!(cl & ch)) {
            if (cl) {
                int32_t b = (cl & 8u) ? ty : by;
                if (cut(l, va, name, sub32(b, py), sub32(qx, px), sub32(qy, py), &q)) return 1;
                OUT_EMIT(add32(q, px), b);
            }
            if (ch) {
                int32_t b = (ch & 8u) ? ty : by;
                if (cut(l, va, name, sub32(b, qy), sub32(px, qx), sub32(py, qy), &q)) return 1;
                OUT_EMIT(add32(q, qx), b);
            } else {
                OUT_EMIT(qx, qy);
            }
        }
        ip = ic;
        cl = ch;
    }
#undef YCODE
#undef OUT_EMIT
#undef CLIP_EMIT
    *eax = (w - out) >> 3;
    return 0;
}

/* ======================================================================
 * The span builder 0x484720(n, points, out): the polygon's left and right
 * edge per row into a span list. EAX = rows (or, for a polygon of one
 * row, the top point's x). */
#define SPAN_LOCAL 50u
typedef struct span_build {
    es2_sim_loop *l;
    uint32_t out;
    int32_t base;   /* ymin - 1 ([ebp-0xc]) */
} span_build;

/* 0x484dbf + 0x484df0..0x4866f0: count words x, x + step, ... at rows
 * row, row + 1, ... (the unrolled block; it saves EBP at 0x4a33d4). */
static int span_run(span_build *s, int32_t row, int32_t count, int32_t x, int32_t step, uint32_t side) {
    es2_sim_loop *l = s->l;
    if ((uint32_t)count > SPAN_ROWS)
        return refuse(l, 0x00484dbfu, "fill_spans", "a vertical or diagonal edge of more than 800 rows (the computed jump leaves the block)");
    w32(l, VA_SAVED_FRAME, l->stack_hi);
    uint32_t p = (uint32_t)row * 4u + s->out + side;
    for (int32_t i = 0; i < count; ++i, p += 4u, x = add32(x, step)) w16(l, p, (uint16_t)x);
    return 0;
}

/* One edge P(x0, y0) -> Q(x1, y1) of the chain on `side` (2 right, 0 left):
 * 0x484878..0x484dbf. */
static int span_edge(span_build *s, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t side) {
    es2_sim_loop *l = s->l;
    int32_t dx = abs32(sub32(x0, x1));
    if (dx == 0) {   /* vertical: 0x484935 */
        int32_t hi = y0, lo = y1;
        if (!(hi > lo)) { hi = y1; lo = y0; }
        return span_run(s, sub32(lo, s->base), add32(sub32(hi, lo), 1), x1, 0, side);
    }
    int32_t dy = abs32(sub32(y0, y1));
    if (dy == 0) {   /* horizontal: 0x4848c3 */
        int32_t lo = x0, hi = x1;
        if (!(x0 < x1)) { lo = x1; hi = x0; }
        w16(l, (uint32_t)sub32(y0, s->base) * 4u + s->out + side, (uint16_t)(side ? hi : lo));
        return 0;
    }
    if (dx == dy) {  /* diagonal: 0x484b3e */
        if (!(y0 < y1)) { int32_t t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }
        int32_t step = x0 < x1 ? 1 : -1;
        return span_run(s, sub32(y0, s->base), add32(neg32(sub32(y0, y1)), 1), x0, step, side);
    }
    if (dx < dy) {   /* y-major: 0x48495a, from the smaller x */
        if (!(x0 < x1)) { int32_t t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }
        uint32_t p = (uint32_t)sub32(y0, s->base) * 4u + s->out + side;
        int32_t d = sub32(y0, y1);
        uint32_t rows = (uint32_t)abs32(d) + 1u;
        int32_t ddx = sub32(x1, x0), two_dx = shl1(ddx);
        int32_t e = sub32(two_dx, abs32(d)), inc = shl1(sub32(ddx, abs32(d)));
        uint32_t step = d < 0 ? 4u : (uint32_t)-4;
        int32_t x = x0;
        for (; rows != 0; --rows, p += step) {
            w16(l, p, (uint16_t)x);
            if (e >= 0) { x = add32(x, 1); e = add32(e, inc); } else e = add32(e, two_dx);
        }
        return 0;
    }
    /* x-major: 0x484b74 (side 2, from the larger x, x--) / 0x484c97 (side
     * 0, from the smaller x, x++). */
    int right = side != 0;
    if (right ? !(x0 > x1) : !(x0 < x1)) { int32_t t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }
    uint32_t p = (uint32_t)sub32(y0, s->base) * 4u + s->out + side;
    uint32_t step = 4u;
    int32_t ady = sub32(y1, y0);
    if (!(y1 >= y0)) { ady = neg32(ady); step = (uint32_t)-4; }
    int32_t t = sub32(x1, x0);
    if (!(x1 <= x0)) t = neg32(t);
    int32_t inc = shl1(add32(t, ady)), two_dy = shl1(ady);
    int64_t e = add32(t, two_dy);
    int32_t x = x0, dir = right ? -1 : 1;
    uint32_t cnt = (uint32_t)ady;
    w16(l, p, (uint16_t)x); p += step; x = add32(x, dir);
    for (;;) {
        /* e: the true sum after an add (jl/jge after add test SF != OF) */
        while (e < 0) { x = add32(x, dir); e = (int64_t)(int32_t)e + two_dy; }
        w16(l, p, (uint16_t)x); p += step; x = add32(x, dir);
        if (--cnt == 0) break;
        e = (int64_t)(int32_t)e + inc;
    }
    return 0;
}

int es2_sim_fill_spans(es2_sim_loop *l, uint32_t count, uint32_t points, uint32_t out, uint32_t *eax) {
    static const char *name = "fill_spans";
    const uint32_t va = 0x00484720u;
    if ((int32_t)count < 2) return refuse(l, va, name, "fewer than 2 points (the top search's loop runs away)");
    /* The top: y < best, or equal y and x >= best (from i = n-1 down to 1). */
    uint32_t top = 0, bottom = 0;
    int32_t tx = s32(l, points), ty = s32(l, points + 4u);
    for (uint32_t i = count - 1u; i != 0; --i) {
        int32_t y = s32(l, points + 8u * i + 4u), x = s32(l, points + 8u * i);
        if (y < ty || (y == ty && x >= tx)) { top = i; tx = x; ty = y; }
    }
    int32_t bx = s32(l, points), by = s32(l, points + 4u);
    for (uint32_t i = count - 1u; i != 0; --i) {
        int32_t y = s32(l, points + 8u * i + 4u), x = s32(l, points + 8u * i);
        if (y > by || (y == by && x <= bx)) { bottom = i; bx = x; by = y; }
    }
    int32_t ymin = ty, ymax = by;
    if (ymax == ymin) {   /* 0x4847a9 */
        w16(l, out, (uint16_t)ymin);
        w16(l, out + 2u, 1u);
        w16(l, out + 4u, (uint16_t)r32(l, points + 8u * bottom));
        uint32_t x = r32(l, points + 8u * top);
        w16(l, out + 6u, (uint16_t)x);
        *eax = x;
        return 0;
    }
    if ((uint32_t)sub32(ymax, ymin) >= SPAN_ROWS)
        return refuse(l, va, name, "a polygon of more than 800 rows (the span list leaves its buffer)");
    /* The chains: from the top forward until y >= ymax (the right side),
     * from the bottom forward until y <= ymin (the left side). */
    int32_t cx[SPAN_LOCAL], cy[SPAN_LOCAL];
    uint32_t total = 0, count1;
    for (int chain = 0; chain < 2; ++chain) {
        uint32_t i = chain ? bottom : top;
        for (;;) {
            if (total >= SPAN_LOCAL) return refuse(l, va, name, "the edge chains pass 50 points (the original overwrites its locals)");
            int32_t x = s32(l, points + 8u * i), y = s32(l, points + 8u * i + 4u);
            cx[total] = x; cy[total] = y; ++total;
            if (chain ? !(y > ymin) : !(y < ymax)) break;
            if (++i >= count) i = 0;
        }
        if (!chain) count1 = total;
    }
    span_build s = {l, out, sub32(ymin, 1)};
    uint32_t j = 0;
    for (uint32_t e = 1; e < count1; ++e, ++j)
        if (span_edge(&s, cx[j], cy[j], cx[j + 1u], cy[j + 1u], 2u)) return 1;
    j = count1;
    for (uint32_t e = 1; e < total - count1; ++e, ++j)
        if (span_edge(&s, cx[j], cy[j], cx[j + 1u], cy[j + 1u], 0u)) return 1;
    uint32_t rows = (uint32_t)sub32(ymax, ymin) + 1u;
    w16(l, out, (uint16_t)ymin);
    w16(l, out + 2u, (uint16_t)rows);
    *eax = rows;
    return 0;
}

/* ======================================================================
 * The line's clip 0x483c6c(a, b, rect): top, left, bottom, right; the
 * outside end is moved onto the bound (the ends swap when b is the
 * outside one, and stay swapped). EAX 1 (a and b written) or 0. */
int es2_sim_fill_line_clip(es2_sim_loop *l, uint32_t pa, uint32_t pb, uint32_t rect, uint32_t *eax) {
    static const char *name = "fill_line_clip";
    const uint32_t va = 0x00483c6cu;
    int32_t ax = s32(l, pa), ay = s32(l, pa + 4u), bx = s32(l, pb), by = s32(l, pb + 4u), q, t;
    *eax = 0;
    for (unsigned bound = 0; bound < 4u; ++bound) {
        int ybound = !(bound & 1u), low = bound < 2u;
        int32_t v = s32(l, rect + (bound == 0 ? 4u : bound == 1u ? 0u : bound == 2u ? 12u : 8u));
        int32_t *ac = ybound ? &ay : &ax, *ao = ybound ? &ax : &ay, *bc = ybound ? &by : &bx, *bo = ybound ? &bx : &by;
        int a_out = low ? *ac < v : *ac > v, b_out = low ? *bc < v : *bc > v;
        if (a_out && b_out) return 0;
        if (!a_out && !b_out) continue;
        if (!a_out) { t = ax; ax = bx; bx = t; t = ay; ay = by; by = t; }
        if (cut(l, va, name, sub32(*ao, *bo), sub32(v, *ac), sub32(*ac, *bc), &q)) return 1;
        *ao = add32(*ao, q);
        *ac = v;
    }
    w32(l, pa, (uint32_t)ax); w32(l, pa + 4u, (uint32_t)ay);
    w32(l, pb, (uint32_t)bx); w32(l, pb + 4u, (uint32_t)by);
    *eax = 1;
    return 0;
}

/* The line's span list 0x483d6c(a, b, out): one {x, x} or {left, right}
 * pair per row (Bresenham), rows from the left end's row. */
int es2_sim_fill_line_spans(es2_sim_loop *l, uint32_t pa, uint32_t pb, uint32_t out) {
    static const char *name = "fill_line_spans";
    const uint32_t va = 0x00483d6cu;
    int32_t ax = s32(l, pa), ay = s32(l, pa + 4u), bx = s32(l, pb), by = s32(l, pb + 4u);
    if (!(ax < bx)) { int32_t t = ax; ax = bx; bx = t; t = ay; ay = by; by = t; }
    w16(l, out, (uint16_t)(ay < by ? ay : by));
    uint32_t p = out + 2u;
    if (ax == bx) {
        if (ay == by) { w16(l, p, 1u); w16(l, p + 2u, (uint16_t)ax); w16(l, p + 4u, (uint16_t)ax); return 0; }
        uint32_t rows = (uint32_t)abs32(sub32(by, ay)) + 1u;
        if (rows > SPAN_ROWS) return refuse(l, va, name, "a line of more than 800 rows (the span list leaves its buffer)");
        w16(l, p, (uint16_t)rows); p += 2u;
        for (uint32_t i = 0; i < rows; ++i, p += 4u) { w16(l, p, (uint16_t)ax); w16(l, p + 2u, (uint16_t)bx); }
        return 0;
    }
    if (ay == by) { w16(l, p, 1u); w16(l, p + 2u, (uint16_t)ax); w16(l, p + 4u, (uint16_t)bx); return 0; }
    int32_t dx = sub32(bx, ax), dy = sub32(by, ay);
    uint32_t adj = 0;
    int backward = !(by > ay);
    if (backward) dy = neg32(dy);
    if ((uint32_t)dy >= SPAN_ROWS) return refuse(l, va, name, "a line of more than 800 rows (the span list leaves its buffer)");
    w16(l, p, (uint16_t)(dy + 1)); p += 2u;
    if (backward) { adj = (uint32_t)-8; p += 4u * (uint32_t)dy; }
    int32_t x = ax;
#define PUT(v) do { w16(l, p, (uint16_t)(v)); p += 2u; } while (0)
    if (dx == dy) {
        for (uint32_t c = (uint32_t)dy + 1u; c != 0; --c) { PUT(x); PUT(x); p += adj; x = add32(x, 1); }
    } else if (dx > dy) {
        int32_t two_dy = shl1(dy), e = sub32(two_dy, dx), inc = sub32(e, dx);
        uint32_t c = (uint32_t)dx + 1u;
        for (;;) {
            PUT(x);                       /* the row's left end */
            while (e < 0) {
                e = add32(e, two_dy); x = add32(x, 1);
                if (--c == 0) { x = add32(x, -1); PUT(x); return 0; }
            }
            PUT(x); x = add32(x, 1); p += adj; e = add32(e, inc);
            if (--c == 0) return 0;
        }
    } else {
        int32_t two_dx = shl1(dx), e = sub32(two_dx, dy), inc = sub32(e, dy);
        PUT(x); PUT(x); p += adj;
        for (uint32_t c = (uint32_t)dy; c != 0; --c) {
            if (e >= 0) { x = add32(x, 1); e = add32(e, inc); } else e = add32(e, two_dx);
            PUT(x); PUT(x); p += adj;
        }
    }
#undef PUT
    return 0;
}

/* BMP_8 slot 34, 0x48a3b0(colour, p0, p1): the line drawn pixel by pixel
 * on the current surface from the left end (Bresenham, 2dx/2dy). */
int es2_sim_fill_line_slot(es2_sim_loop *l, uint32_t colour, uint32_t p0, uint32_t p1) {
    if (s32(l, p0) > s32(l, p1)) { uint32_t t = p0; p0 = p1; p1 = t; }
    int32_t a = shl1(sub32(s32(l, p1), s32(l, p0)));
    uint32_t step = r32(l, VA_VIEW_PITCH);
    int32_t y0 = s32(l, p0 + 4u), y1 = s32(l, p1 + 4u), d = sub32(y1, y0);
    if (!(y1 >= y0)) { d = neg32(d); step = 0u - step; }
    int32_t b = shl1(d);
    uint32_t rows = r32(l, VA_VIEW_ROWS);
    uint32_t dst = r32(l, rows) + r32(l, rows + 4u * (uint32_t)y0 + 4u) + r32(l, p0);
    int major_x = a > b;
    int32_t big = major_x ? a : b, small = major_x ? b : a;
    uint32_t half = (uint32_t)big >> 1;
    int32_t pixels = (int32_t)(half + 1u);
    if (pixels > 0x10000)
        return refuse(l, VA_LINE_BODY, "fill_line_slot", "a line of more than 65536 pixels (the original writes past any surface)");
    int64_t err = neg32((int32_t)half);
    for (int32_t i = 0; i < pixels; ++i) {
        w8(l, dst, (uint8_t)colour);
        dst += major_x ? 1u : step;
        err = (int64_t)(int32_t)err + small;
        if (err < 0) continue;
        dst += major_x ? step : 1u;
        err = sub32((int32_t)err, big);
    }
    return 0;
}

/* ======================================================================
 * The line 0x483a04(p0, p1) with the pen [C+0x224]: kind 0 slot 0 with
 * the resolved colour, kind 2 slot 1 with pen+4; shifted by the origin
 * when [C+0x208] is set; clip mode 0 as is, 1 inside the clip rect, other
 * per region (from the last; kind-0 rects, kind-2 span lists). EAX is
 * left undefined. */
int es2_sim_fill_line(es2_sim_loop *l, uint32_t p0, uint32_t p1) {
    static const char *name = "fill_line";
    const uint32_t va = 0x00483a04u;
    uint32_t c = r32(l, VA_CONTEXT), pen = r32(l, c + 0x224u), kind = r32(l, pen), writer, colour;
    if (kind == 0) {
        writer = r32(l, VA_SLOTS);
        if (!resolve_colour(l, r32(l, pen + 4u), &colour)) return 0;
    } else if (kind == 2u) {
        writer = r32(l, VA_SLOTS + 4u);
        colour = pen + 4u;
    } else {
        return 0;
    }
    c = r32(l, VA_CONTEXT);
    int32_t dx = 0, dy = 0;
    if (r32(l, c + 0x208u)) {
        dx = sub32(s32(l, c + 0x20cu), s32(l, c + 0x21cu));
        dy = sub32(s32(l, c + 0x210u), s32(l, c + 0x220u));
    }
    uint32_t top = l->stack_hi, f = frame_push(l, 0x20u);
    if (!f) return refuse(l, va, name, "the own code's call area is exhausted");
    /* [ebp-0x2c] cb, [ebp-0x24] ca, [ebp-0x1c] b, [ebp-0x14] a */
    uint32_t cb = f, ca = f + 8u, b = f + 0x10u, a = f + 0x18u, eax = 0;
    w32(l, a, (uint32_t)add32(s32(l, p0), dx)); w32(l, a + 4u, (uint32_t)add32(s32(l, p0 + 4u), dy));
    w32(l, b, (uint32_t)add32(s32(l, p1), dx)); w32(l, b + 4u, (uint32_t)add32(s32(l, p1 + 4u), dy));
    uint32_t mode = r32(l, c + 0x204u);
    int bad = 0;
    if (mode == 0 || mode == 1u) {
        if (mode == 1u && ((bad = es2_sim_fill_line_clip(l, a, b, c + 0x20cu, &eax)) || !eax)) goto done;
        if (writer == r32(l, VA_SLOTS)) {
            uint32_t target = r32(l, VA_SLOTS + 4u * SLOT_LINE), args[3] = {colour, a, b};
            bad = target == VA_LINE_BODY ? es2_sim_fill_line_slot(l, colour, a, b) : call(l, target, args, 3u, NULL);
        } else {
            bad = es2_sim_fill_line_spans(l, a, b, r32(l, VA_SPANS_A)) || call_writer(l, writer, r32(l, VA_SPANS_A), colour);
        }
        goto done;
    }
    uint32_t n = r32(l, c);
    if (!(n & 0xffffu)) goto done;   /* jcxz */
    int converted = 0;
    do {
        c = r32(l, VA_CONTEXT);
        --n;
        uint32_t e = r32(l, c + 4u + 4u * n), ek = r32(l, e);
        if (ek == 0) {
            w32(l, cb, r32(l, b)); w32(l, cb + 4u, r32(l, b + 4u));
            w32(l, ca, r32(l, a)); w32(l, ca + 4u, r32(l, a + 4u));
            if ((bad = es2_sim_fill_line_clip(l, ca, cb, e + 4u, &eax))) goto done;
            if (eax && ((bad = es2_sim_fill_line_spans(l, ca, cb, r32(l, VA_SPANS_A))) ||
                        (bad = call_writer(l, writer, r32(l, VA_SPANS_A), colour))))
                goto done;
        } else if (ek == 2u) {
            if (!converted) {
                converted = 1;
                if ((bad = es2_sim_fill_line_clip(l, a, b, r32(l, VA_CONTEXT) + 0x20cu, &eax)) || !eax) goto done;
                if ((bad = es2_sim_fill_line_spans(l, a, b, r32(l, VA_SPANS_B)))) goto done;
            }
            uint32_t r = 0;
            es2_sim_gfx_spans_clip_spans(l, r32(l, VA_SPANS_B), r32(l, e + 4u), r32(l, VA_SPANS_A), &r);
            if (r && (bad = call_writer(l, writer, r, colour))) goto done;
        }
    } while (n != 0);
done:
    frame_pop(l, top);
    return bad;
}

/* ======================================================================
 * The fill 0x483eb8(n, points) with the fill [C+0x228]: kind 0 slot 0
 * with the resolved colour, 6 slot 1 with fill+4, 1 the Gouraud fill
 * 0x487acc, 5 and 7 slot 33, 4 the outline; n < 3 the outline too. EAX =
 * the origin flag [C+0x208] on entry (restored on exit). */
#define FILL_FRAME 0x148u   /* [ebp-0x15c] out (20 points), [ebp-0xbc] the copy (21 points) */
#define FILL_COPY 0xa0u
#define FILL_PEN 0x140u     /* [ebp-0x1c]: the copy's point 20 */
static int outline(es2_sim_loop *l, uint32_t n, uint32_t p, uint32_t pen) {
    uint32_t c = r32(l, VA_CONTEXT), fill = r32(l, c + 0x228u);
    w32(l, pen, 0);
    w32(l, pen + 4u, r32(l, fill + 4u));
    uint32_t old = r32(l, c + 0x224u);
    w32(l, c + 0x224u, pen);
    int bad;
    if (n == 1u) {
        bad = es2_sim_gfx_point(l, p);
    } else if (n == 2u) {
        bad = es2_sim_fill_line(l, p + 8u, p);
    } else {
        int64_t k = (int32_t)(n - 1u);
        uint32_t q = p;
        bad = 0;
        do {
            if ((bad = es2_sim_fill_line(l, q + 8u, q))) break;
            q += 8u;
        } while (--k > 0);
        if (!bad) bad = es2_sim_fill_line(l, p, q);
    }
    w32(l, c + 0x224u, old);
    return bad;
}

int es2_sim_fill_polygon(es2_sim_loop *l, uint32_t n, uint32_t points, uint32_t *eax) {
    static const char *name = "fill_polygon";
    const uint32_t va = 0x00483eb8u;
    uint32_t c = r32(l, VA_CONTEXT), saved = 0, p = points;
    uint32_t top = l->stack_hi, f = frame_push(l, FILL_FRAME);
    if (!f) return refuse(l, va, name, "the own code's call area is exhausted");
    uint32_t out = f, copy = f + FILL_COPY;
    int bad = 0;
    uint32_t region = r32(l, c + 0x208u);
    if (region) {
        saved = region;
        w32(l, c + 0x208u, 0);
        if (n == 0 || n > 21u) { frame_pop(l, top); return refuse(l, va, name, "an origin copy of 0 or more than 21 points (a runaway loop or the saved flag overwritten)"); }
        int32_t dx = sub32(s32(l, c + 0x20cu), s32(l, c + 0x21cu)), dy = sub32(s32(l, c + 0x210u), s32(l, c + 0x220u));
        for (uint32_t i = 0; i < n; ++i) {
            w32(l, copy + 8u * i, (uint32_t)add32(s32(l, points + 8u * i), dx));
            w32(l, copy + 8u * i + 4u, (uint32_t)add32(s32(l, points + 8u * i + 4u), dy));
        }
        p = copy;
        c = r32(l, VA_CONTEXT);
    }
    uint32_t writer = 0, colour = 0;
    if ((int32_t)n < 3) goto line;
    uint32_t fill = r32(l, c + 0x228u), kind = r32(l, fill);
    switch (kind) {
    case 0:
        writer = r32(l, VA_SLOTS);
        if (!resolve_colour(l, r32(l, fill + 4u), &colour)) goto end;
        break;
    case 1: {
        uint32_t args[2] = {n, p};
        bad = call(l, VA_GOURAUD, args, 2u, NULL);
        goto end;
    }
    case 4:
        goto line;
    case 5:
    case 7: {
        uint32_t args[2] = {n, p};
        bad = es2_sim_gfx_slot(l, SLOT_POLYGON, args, 2u, NULL);
        goto end;
    }
    case 6:
        writer = r32(l, VA_SLOTS + 4u);
        colour = fill + 4u;
        break;
    default:
        goto end;
    }
    c = r32(l, VA_CONTEXT);
    uint32_t mode = r32(l, c + 0x204u), k = 0, ignored;
    if (mode == 0) {
        bad = es2_sim_fill_spans(l, n, p, r32(l, VA_SPANS_A), &ignored) || call_writer(l, writer, r32(l, VA_SPANS_A), colour);
        goto end;
    }
    if (mode == 1u) {
        if ((bad = es2_sim_fill_clip(l, n, p, out, c + 0x20cu, &k)) || !k) goto end;
        if (k > 41u) { bad = refuse(l, va, name, "a clip output past 41 points (the original overwrites its locals)"); goto end; }
        bad = es2_sim_fill_spans(l, k, out, r32(l, VA_SPANS_A), &ignored) || call_writer(l, writer, r32(l, VA_SPANS_A), colour);
        goto end;
    }
    uint32_t count = r32(l, c);
    if (!count) goto end;
    int converted = 0;
    do {
        c = r32(l, VA_CONTEXT);
        --count;
        uint32_t e = r32(l, c + 4u + 4u * count), ek = r32(l, e);
        if (ek == 0) {
            if ((bad = es2_sim_fill_clip(l, n, p, out, e + 4u, &k))) goto end;
            if (!k) continue;
            if (k > 41u) { bad = refuse(l, va, name, "a clip output past 41 points (the original overwrites its locals)"); goto end; }
            if ((bad = es2_sim_fill_spans(l, k, out, r32(l, VA_SPANS_A), &ignored)) || (bad = call_writer(l, writer, r32(l, VA_SPANS_A), colour)))
                goto end;
        } else if (ek == 2u) {
            if (!converted) {
                converted = 1;
                if ((bad = es2_sim_fill_clip(l, n, p, out, r32(l, VA_CONTEXT) + 0x20cu, &k)) || !k) goto end;
                if (k > 41u) { bad = refuse(l, va, name, "a clip output past 41 points (the original overwrites its locals)"); goto end; }
                if ((bad = es2_sim_fill_spans(l, k, out, r32(l, VA_SPANS_B), &ignored))) goto end;
            }
            uint32_t r = 0;
            es2_sim_gfx_spans_clip_spans(l, r32(l, VA_SPANS_B), r32(l, e + 4u), r32(l, VA_SPANS_A), &r);
            if (r && (bad = call_writer(l, writer, r, colour))) goto end;
        }
    } while (count != 0);
    goto end;
line:
    if (n & 0xffffu) bad = outline(l, n, p, f + FILL_PEN);   /* jcxz */
end:
    w32(l, r32(l, VA_CONTEXT) + 0x208u, saved);
    frame_pop(l, top);
    if (!bad && eax) *eax = saved;
    return bad;
}

/* 0x484222(n, points): the points reversed, then 0x483eb8; for a kind-5
 * fill the fill's per-point table [F+8] (8-byte entries) is reversed with
 * them and restored after (EAX = F then). n == 0: EAX undefined. */
int es2_sim_fill_polygon_reversed(es2_sim_loop *l, uint32_t n, uint32_t points, uint32_t *eax) {
    static const char *name = "fill_polygon_reversed";
    const uint32_t va = 0x00484222u;
    if (n == 0) return 0;
    if ((int32_t)n <= 2) return es2_sim_fill_polygon(l, n, points, eax);
    if (n > 20u) return refuse(l, va, name, "more than 20 points (the reversed copy overwrites the saved registers)");
    uint32_t top = l->stack_hi, f = frame_push(l, 0x140u);
    if (!f) return refuse(l, va, name, "the own code's call area is exhausted");
    uint32_t rev = f + 0xa0u, table = f;   /* [ebp-0xa0], [ebp-0x140] */
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t s = points + 8u * (n - 1u - i);
        w32(l, rev + 8u * i, r32(l, s));
        w32(l, rev + 8u * i + 4u, r32(l, s + 4u));
    }
    uint32_t fill = r32(l, r32(l, VA_CONTEXT) + 0x228u);
    int bad;
    if (r32(l, fill) == 5u) {
        uint32_t old = r32(l, fill + 8u);
        w32(l, fill + 8u, table);
        for (uint32_t i = 0; i < n; ++i) {
            uint32_t s = old + 8u * (n - 1u - i);
            w32(l, table + 8u * i, r32(l, s));
            w32(l, table + 8u * i + 4u, r32(l, s + 4u));
        }
        uint32_t ignored;
        bad = es2_sim_fill_polygon(l, n, rev, &ignored);
        if (!bad) {
            w32(l, fill + 8u, old);
            if (eax) *eax = fill;
        }
    } else {
        bad = es2_sim_fill_polygon(l, n, rev, eax);
    }
    frame_pop(l, top);
    return bad;
}

/* 0x4842bb(n, points): the doubled signed area (64-bit); positive ->
 * 0x483eb8, else 0x484222. n == 0: EAX undefined. */
int es2_sim_fill_polygon_any(es2_sim_loop *l, uint32_t n, uint32_t points, uint32_t *eax) {
    if (n == 0) return 0;
    if ((int32_t)n <= 2) return es2_sim_fill_polygon(l, n, points, eax);
    uint32_t last = points + 8u * (n - 1u);
    uint64_t s = (uint64_t)((int64_t)s32(l, last) * s32(l, points + 4u));
    s -= (uint64_t)((int64_t)s32(l, last + 4u) * s32(l, points));
    for (uint32_t i = 0, q = points; i < n - 1u; ++i, q += 8u) {
        s += (uint64_t)((int64_t)s32(l, q) * s32(l, q + 12u));
        s -= (uint64_t)((int64_t)s32(l, q + 4u) * s32(l, q + 8u));
    }
    int32_t hi = (int32_t)(uint32_t)(s >> 32), lo = (int32_t)(uint32_t)s;
    if (hi > 0 || (hi == 0 && lo > 0)) return es2_sim_fill_polygon(l, n, points, eax);
    return es2_sim_fill_polygon_reversed(l, n, points, eax);
}

/* ======================================================================
 * The service rows (sim/dispatch/services/fill_layer.h). Where the
 * original leaves EAX undefined the entry EAX is kept. */
static int serve(es2_sim_service_call *sc, es2_sim_loop **l) {
    *l = es2_sim_loop_bound();
    if (!*l) { sc->refused = 1; sc->reason = "no main loop bound"; return 1; }
    return 0;
}
static void failed(es2_sim_service_call *sc) { sc->refused = 1; sc->reason = "a call inside the service was refused"; }
#define FILL_SERVICE(name, body) \
    void es2_sim_service_fill_##name(es2_sim_service_call *sc, void *user) { \
        es2_sim_loop *l; (void)user; \
        if (serve(sc, &l)) return; \
        uint32_t eax = sc->eax; \
        if (body) { failed(sc); return; } \
        sc->eax = eax; \
    }
FILL_SERVICE(polygon, es2_sim_fill_polygon(l, sc->slots[0], sc->slots[1], &eax))
FILL_SERVICE(polygon_reversed, es2_sim_fill_polygon_reversed(l, sc->slots[0], sc->slots[1], &eax))
FILL_SERVICE(polygon_any, es2_sim_fill_polygon_any(l, sc->slots[0], sc->slots[1], &eax))
FILL_SERVICE(line, es2_sim_fill_line(l, sc->slots[0], sc->slots[1]))
FILL_SERVICE(line_slot, es2_sim_fill_line_slot(l, sc->slots[0], sc->slots[1], sc->slots[2]))

/* ======================================================================
 * The fill layer's rest (rasterizer.md, "Fill-layer object slots and exit
 * registers"). The exit registers of the clipper, the span builder and the
 * line's clip and span list for a direct call (the functions above give EAX
 * only; the shadows below read the same inputs before those functions write and
 * give EDX and ECX), and the small functions of 0x4820b7..0x482f94 and
 * 0x48358c..0x483655: the palette (vtable 0x4a30f8), font (0x4a31ac) and
 * bitmap-array (0x4a32ec) objects' slots, the stream thunks, the draw thunks
 * and the holder (0x4a3340). Every row sets EAX, EDX and ECX as the original
 * leaves them; "the callee's" is the last call's result. Where the original
 * pops a pushed argument into ECX, ECX is that argument (the callee is taken
 * not to write its own argument slot). */
typedef struct regs3 { uint32_t eax, edx, ecx; } regs3;
#define VA_COUNTER 0x006cbee4u          /* the construction counter's pointer */
#define VA_STREAM_WORD 0x00492447u      /* (stream, word): one word to the stream */
#define VA_DELETE 0x00477548u
#define VA_DELETE_ARRAY 0x0047758cu
#define VA_BASE_DESTROY 0x004838fdu
#define VA_DRIVER_SLOT13 0x004a57e0u

static int callr(es2_sim_loop *l, uint32_t va, const uint32_t *slots, unsigned count, regs3 *r) {
    es2_sim_result x;
    if (es2_sim_loop_call(l, va, slots, count, &x)) return 1;
    r->eax = x.eax; r->edx = x.edx; r->ecx = x.ecx;
    return 0;
}
/* call [[obj] + off] */
static int vcallr(es2_sim_loop *l, uint32_t obj, uint32_t off, const uint32_t *slots, unsigned count, regs3 *r) {
    return callr(l, r32(l, r32(l, obj) + off), slots, count, r);
}
static int32_t sx16(const es2_sim_loop *l, uint32_t a) { return (int32_t)(int16_t)r16(l, a); }
/* `mov r16, word; push r32`: the upper half is the register's. */
static uint32_t lo16(uint32_t reg, uint16_t w) { return (reg & 0xffff0000u) | w; }
/* [[0x6cbee4]] += delta; *ptr = the pointer. */
static int bump(es2_sim_loop *l, uint32_t va, const char *name, uint32_t delta, uint32_t *ptr) {
    uint32_t c = r32(l, VA_COUNTER);
    if (!c) return null_refuse(l, va, name, "the construction counter pointer [0x6cbee4] is 0 (the original faults)");
    w32(l, c, r32(l, c) + delta);
    if (ptr) *ptr = c;
    return 0;
}
static uint32_t fs0_head(void) {
    return es2_machine_fs0();
}
static void put_regs(es2_sim_service_call *sc, const regs3 *r) { sc->eax = r->eax; sc->edx = r->edx; sc->ecx = r->ecx; }
static regs3 entry_regs(const es2_sim_service_call *sc) { regs3 r = {sc->eax, sc->edx, sc->ecx}; return r; }

/* idiv of a * b by d for the shadows: 0 when it faults (the function then
 * refuses), else 1 with the quotient and EDX the remainder. */
static int rem_cut(int32_t a, int32_t b, int32_t d, int32_t *q, uint32_t *edx) {
    int64_t p = (int64_t)a * (int64_t)b;
    if (d == 0) return 0;
    int64_t r = p / (int64_t)d;
    if (r > INT32_MAX || r < INT32_MIN) return 0;
    *q = (int32_t)r;
    *edx = (uint32_t)(int32_t)(p - r * (int64_t)d);
    return 1;
}

/* 0x484338's EDX and ECX (n > 1): EDX the last idiv's remainder (the
 * entry EDX without a cut); ECX's low word is the last outcode in CL and
 * CH (`mov cl, ch` after every step), the high word the entry ECX's. With
 * no point out of the x pass the code is the x outcode of point n-1. One
 * point out of the x pass (0x4846da) is unreachable and not modelled. */
static void clip_regs(const es2_sim_loop *l, uint32_t count, uint32_t points, uint32_t rect, regs3 *r) {
    if ((int32_t)count <= 1) return;
    int32_t lx = s32(l, rect), rx = s32(l, rect + 8u), ty = s32(l, rect + 4u), by = s32(l, rect + 12u), q;
    int32_t mx[CLIP_LOCAL][2];
    uint32_t m = 0, edx = r->edx;
#define XCODE(x) ((x) < lx ? 1u : (x) > rx ? 2u : 0u)
#define YCODE(y) ((y) > by ? 4u : (y) < ty ? 8u : 0u)
#define SH_EMIT(X, Y) do { if (m >= CLIP_LOCAL) return; mx[m][0] = (X); mx[m][1] = (Y); ++m; } while (0)
    uint32_t prev = points + 8u * (count - 1u), cur = points;
    unsigned cl = XCODE(s32(l, prev));
    for (uint32_t k = count; k != 0; --k) {
        int32_t px = s32(l, prev), py = s32(l, prev + 4u), qx = s32(l, cur), qy = s32(l, cur + 4u);
        unsigned ch = XCODE(qx);
        if (!(cl | ch)) {
            SH_EMIT(qx, qy);
        } else if (!(cl & ch)) {
            if (cl) {
                int32_t b = (cl & 1u) ? lx : rx;
                if (!rem_cut(sub32(b, px), sub32(qy, py), sub32(qx, px), &q, &edx)) return;
                SH_EMIT(b, add32(q, py));
            }
            if (ch) {
                int32_t b = (ch & 1u) ? lx : rx;
                if (!rem_cut(sub32(b, qx), sub32(py, qy), sub32(px, qx), &q, &edx)) return;
                SH_EMIT(b, add32(q, qy));
            } else {
                SH_EMIT(qx, qy);
            }
        }
        prev = cur;
        cl = ch;
        cur += 8u;
    }
    if (m >= 2u) {
        unsigned ip = m - 1u;
        cl = YCODE(mx[ip][1]);
        for (unsigned ic = 0; ic < m; ++ic) {
            int32_t px = mx[ip][0], py = mx[ip][1], qx = mx[ic][0], qy = mx[ic][1];
            unsigned ch = YCODE(qy);
            if ((cl | ch) && !(cl & ch)) {
                if (cl) {
                    int32_t b = (cl & 8u) ? ty : by;
                    if (!rem_cut(sub32(b, py), sub32(qx, px), sub32(qy, py), &q, &edx)) return;
                }
                if (ch) {
                    int32_t b = (ch & 8u) ? ty : by;
                    if (!rem_cut(sub32(b, qy), sub32(px, qx), sub32(py, qy), &q, &edx)) return;
                }
            }
            ip = ic;
            cl = ch;
        }
    }
#undef SH_EMIT
#undef YCODE
#undef XCODE
    r->edx = edx;
    r->ecx = (r->ecx & 0xffff0000u) | (uint32_t)cl * 0x101u;
}

/* 0x484720's EDX and ECX. One row: ECX 0 (the `loop`s), EDX ymax.
 * Otherwise from the last edge drawn, the left chain's last (the left
 * chain always has two points or more: it starts at a point of ymax and
 * ends at one of ymin): vertical and diagonal leave ECX 2 (the block's
 * stride) and EDX the computed jump 0x4866f0 - 8 * rows; horizontal ECX
 * the edge's start y and EDX 0 (CDQ of 0); y-major ECX 0 and EDX the error
 * step of its last row (inc when e >= 0, else 2dx); x-major ECX 0 and EDX
 * the stride 2, or -6 for an upward edge. */
static void spans_regs(const es2_sim_loop *l, uint32_t count, uint32_t points, regs3 *r) {
    if ((int32_t)count < 2) return;
    uint32_t bottom = 0;
    int32_t tx = s32(l, points), ty = s32(l, points + 4u);
    for (uint32_t i = count - 1u; i != 0; --i) {
        int32_t y = s32(l, points + 8u * i + 4u), x = s32(l, points + 8u * i);
        if (y < ty || (y == ty && x >= tx)) { tx = x; ty = y; }
    }
    int32_t bx = s32(l, points), by = s32(l, points + 4u);
    for (uint32_t i = count - 1u; i != 0; --i) {
        int32_t y = s32(l, points + 8u * i + 4u), x = s32(l, points + 8u * i);
        if (y > by || (y == by && x <= bx)) { bottom = i; bx = x; by = y; }
    }
    int32_t ymin = ty, ymax = by;
    if (ymax == ymin) { r->ecx = 0; r->edx = (uint32_t)ymax; return; }
    /* the left chain: from the bottom forward until y <= ymin */
    int32_t x0 = 0, y0 = 0, x1 = s32(l, points + 8u * bottom), y1 = s32(l, points + 8u * bottom + 4u);
    uint32_t i = bottom;
    for (unsigned guard = 0; y1 > ymin; ++guard) {
        if (guard >= SPAN_LOCAL) return;   /* the function refuses */
        if (++i >= count) i = 0;
        x0 = x1; y0 = y1;
        x1 = s32(l, points + 8u * i); y1 = s32(l, points + 8u * i + 4u);
    }
    int32_t dx = abs32(sub32(x0, x1));
    if (dx == 0) {
        int32_t hi = y0, lo = y1;
        if (!(hi > lo)) { hi = y1; lo = y0; }
        r->ecx = 2u;
        r->edx = 0x004866f0u - 8u * (uint32_t)add32(sub32(hi, lo), 1);
        return;
    }
    int32_t dy = abs32(sub32(y0, y1));
    if (dy == 0) { r->ecx = (uint32_t)y0; r->edx = 0; return; }
    if (dx == dy) { r->ecx = 2u; r->edx = 0x004866f0u - 8u * (uint32_t)add32(dy, 1); return; }
    if (!(x0 < x1)) { int32_t t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }
    r->ecx = 0;
    if (dx < dy) {
        int32_t d = abs32(sub32(y0, y1)), ddx = sub32(x1, x0), two_dx = shl1(ddx);
        int32_t e = sub32(two_dx, d), inc = shl1(sub32(ddx, d));
        uint32_t edx = 0;
        for (uint32_t rows = (uint32_t)d + 1u; rows != 0; --rows) {
            edx = (uint32_t)(e >= 0 ? inc : two_dx);
            e = add32(e, (int32_t)edx);
        }
        r->edx = edx;
        return;
    }
    r->edx = y1 >= y0 ? 2u : 0xfffffffau;   /* x-major, after the swap to the smaller x */
}

/* 0x483c6c's EDX and ECX: ECX the final a.y (after the swaps and cuts, on
 * either exit), EDX the last idiv's remainder (the entry EDX without). */
static void line_clip_regs(const es2_sim_loop *l, uint32_t pa, uint32_t pb, uint32_t rect, regs3 *r) {
    int32_t ax = s32(l, pa), ay = s32(l, pa + 4u), bx = s32(l, pb), by = s32(l, pb + 4u), q, t;
    uint32_t edx = r->edx;
    for (unsigned bound = 0; bound < 4u; ++bound) {
        int ybound = !(bound & 1u), low = bound < 2u;
        int32_t v = s32(l, rect + (bound == 0 ? 4u : bound == 1u ? 0u : bound == 2u ? 12u : 8u));
        int32_t *ac = ybound ? &ay : &ax, *ao = ybound ? &ax : &ay, *bc = ybound ? &by : &bx, *bo = ybound ? &bx : &by;
        int a_out = low ? *ac < v : *ac > v, b_out = low ? *bc < v : *bc > v;
        if (a_out && b_out) break;
        if (!a_out && !b_out) continue;
        if (!a_out) { t = ax; ax = bx; bx = t; t = ay; ay = by; by = t; }
        if (!rem_cut(sub32(*ao, *bo), sub32(v, *ac), sub32(*ac, *bc), &q, &edx)) return;
        *ao = add32(*ao, q);
        *ac = v;
    }
    r->ecx = (uint32_t)ay;
    r->edx = edx;
}

/* 0x483d6c's registers (it defines all three): a point EAX ax, ECX ay,
 * EDX by; vertical EAX bx:ax (words), ECX 0 (REP STOSD), EDX the sign of
 * by - ay; horizontal EAX bx, ECX ay, EDX by; otherwise ECX 0 (LOOP) and
 * EAX/EDX the walk's final x and error (diagonal: x = ax + dy + 1, EDX
 * dy). */
static void line_spans_regs(const es2_sim_loop *l, uint32_t pa, uint32_t pb, regs3 *r) {
    int32_t ax = s32(l, pa), ay = s32(l, pa + 4u), bx = s32(l, pb), by = s32(l, pb + 4u);
    if (!(ax < bx)) { int32_t t = ax; ax = bx; bx = t; t = ay; ay = by; by = t; }
    if (ax == bx && ay == by) { r->eax = (uint32_t)ax; r->ecx = (uint32_t)ay; r->edx = (uint32_t)by; return; }
    if (ax == bx) {
        r->eax = ((uint32_t)bx << 16) | ((uint32_t)ax & 0xffffu);
        r->ecx = 0;
        r->edx = sub32(by, ay) < 0 ? 0xffffffffu : 0;
        return;
    }
    if (ay == by) { r->eax = (uint32_t)bx; r->ecx = (uint32_t)ay; r->edx = (uint32_t)by; return; }
    int32_t dx = sub32(bx, ax), dy = sub32(by, ay), x = ax, e;
    if (!(by > ay)) dy = neg32(dy);
    r->ecx = 0;
    if (dx == dy) { r->edx = (uint32_t)dy; r->eax = (uint32_t)add32(add32(ax, dy), 1); return; }
    if (dx > dy) {
        int32_t two_dy = shl1(dy), inc;
        e = sub32(two_dy, dx);
        inc = sub32(e, dx);
        uint32_t c = (uint32_t)dx + 1u;
        for (;;) {
            while (e < 0) {
                e = add32(e, two_dy); x = add32(x, 1);
                if (--c == 0) { x = add32(x, -1); goto walked; }
            }
            x = add32(x, 1); e = add32(e, inc);
            if (--c == 0) break;
        }
    } else {
        int32_t two_dx = shl1(dx), inc;
        e = sub32(two_dx, dy);
        inc = sub32(e, dy);
        for (uint32_t c = (uint32_t)dy; c != 0; --c) {
            if (e >= 0) { x = add32(x, 1); e = add32(e, inc); } else e = add32(e, two_dx);
        }
    }
walked:
    r->eax = (uint32_t)x;
    r->edx = (uint32_t)e;
}

/* ---- the rows */
#define RD10_SERVICE(fn) void es2_sim_service_##fn(es2_sim_service_call *sc, void *user)
#define RD10_BEGIN es2_sim_loop *l; (void)user; if (serve(sc, &l)) return
#define RD10_TRY(x) do { if (x) { failed(sc); return; } } while (0)

/* 0x484338(n, points, out, rect), 0x484720(n, points, out), 0x483c6c(a,
 * b, rect), 0x483d6c(a, b, out) for a direct call. */
RD10_SERVICE(fill_clip) {
    RD10_BEGIN;
    regs3 r = entry_regs(sc);
    clip_regs(l, sc->slots[0], sc->slots[1], sc->slots[3], &r);
    RD10_TRY(es2_sim_fill_clip(l, sc->slots[0], sc->slots[1], sc->slots[2], sc->slots[3], &r.eax));
    put_regs(sc, &r);
}
RD10_SERVICE(fill_spans) {
    RD10_BEGIN;
    regs3 r = entry_regs(sc);
    spans_regs(l, sc->slots[0], sc->slots[1], &r);
    RD10_TRY(es2_sim_fill_spans(l, sc->slots[0], sc->slots[1], sc->slots[2], &r.eax));
    put_regs(sc, &r);
}
RD10_SERVICE(fill_line_clip) {
    RD10_BEGIN;
    regs3 r = entry_regs(sc);
    line_clip_regs(l, sc->slots[0], sc->slots[1], sc->slots[2], &r);
    RD10_TRY(es2_sim_fill_line_clip(l, sc->slots[0], sc->slots[1], sc->slots[2], &r.eax));
    put_regs(sc, &r);
}
RD10_SERVICE(fill_line_spans) {
    RD10_BEGIN;
    regs3 r = entry_regs(sc);
    line_spans_regs(l, sc->slots[0], sc->slots[1], &r);
    RD10_TRY(es2_sim_fill_line_spans(l, sc->slots[0], sc->slots[1], sc->slots[2]));
    put_regs(sc, &r);
}

/* 0x4820b7(a0, a1, a2, a3, a4): 0x486e10(a1, a2, a3, a4); its registers. */
RD10_SERVICE(gl_palette_forward) {
    RD10_BEGIN;
    regs3 r;
    const uint32_t a[4] = {sc->slots[1], sc->slots[2], sc->slots[3], sc->slots[4]};
    RD10_TRY(callr(l, 0x00486e10u, a, 4, &r));
    put_regs(sc, &r);
}

/* 0x482154(P) / 0x482190(P), vtable 0x4a30f8 slots 1 and 2: the stream
 * size sx[P+4] * 4 + 8 (+ 0x14) plus (sx[E+4] + 1) * 2 for each of the
 * sx[P+0xc] entries E of [P+0x10] (8 bytes; the index compared as a
 * zero-extended word against the re-read count). EAX the size, EDX the
 * entries counted, ECX P. */
static void palette_size(const es2_sim_loop *l, uint32_t p, uint32_t base, es2_sim_service_call *sc) {
    uint32_t sum = (uint32_t)sx16(l, p + 4u) * 4u + base, i = 0, e = r32(l, p + 0x10u) + 4u;
    for (; (int32_t)(i & 0xffffu) < sx16(l, p + 0xcu); ++i, e += 8u) sum += (uint32_t)(sx16(l, e) + 1) * 2u;
    sc->eax = sum; sc->edx = i; sc->ecx = p;
}
RD10_SERVICE(gl_palette_size_a) { RD10_BEGIN; palette_size(l, sc->slots[0], 8u, sc); }
RD10_SERVICE(gl_palette_size_b) { RD10_BEGIN; palette_size(l, sc->slots[0], 0x14u, sc); }

/* 0x4822cc(P, S), slot 4: the words P+4, P+6 (0x492447), the sx[P+4]
 * dwords [P+8] (S slot +0x14), the words P+0xc, P+0xe, then per entry i <
 * sx[P+0xc] (a signed word compare, the count re-read) the entry's word
 * +4 and its sx(word) words [E] (E = [P+0x10] + 8i, re-read). The pushed
 * words' upper halves are the registers the previous call left (the first
 * the entry EAX; the entries' the zero upper half of MOVSX of the index).
 * EAX and EDX are the last call's; ECX is the dword the prologue pushed
 * (`push ecx`: the entry ECX) with its high word the last entry word (the
 * local [ebp-2]), popped at the exit. */
RD10_SERVICE(gl_palette_write) {
    RD10_BEGIN;
    uint32_t p = sc->slots[0], s = sc->slots[1];
    regs3 r = entry_regs(sc);
    uint32_t spill = sc->ecx, a[3] = {s, lo16(r.eax, r16(l, p + 4u)), 0};
    RD10_TRY(callr(l, VA_STREAM_WORD, a, 2, &r));
    a[1] = lo16(r.edx, r16(l, p + 6u));
    RD10_TRY(callr(l, VA_STREAM_WORD, a, 2, &r));
    a[1] = (uint32_t)sx16(l, p + 4u) << 2; a[2] = r32(l, p + 8u);
    RD10_TRY(vcallr(l, s, 0x14u, a, 3, &r));
    a[1] = lo16(r.ecx, r16(l, p + 0xcu));
    RD10_TRY(callr(l, VA_STREAM_WORD, a, 2, &r));
    a[1] = lo16(r.eax, r16(l, p + 0xeu));
    RD10_TRY(callr(l, VA_STREAM_WORD, a, 2, &r));
    for (uint32_t i = 0; (int16_t)i < (int16_t)r16(l, p + 0xcu); ++i) {
        uint32_t k = (uint32_t)(int32_t)(int16_t)i;
        uint16_t w = r16(l, r32(l, p + 0x10u) + 8u * k + 4u);
        a[1] = lo16(k, w);
        RD10_TRY(callr(l, VA_STREAM_WORD, a, 2, &r));
        a[1] = (uint32_t)(int32_t)(int16_t)w * 2u; a[2] = r32(l, r32(l, p + 0x10u) + 8u * k);
        RD10_TRY(vcallr(l, s, 0x14u, a, 3, &r));
        spill = (spill & 0xffffu) | (uint32_t)w << 16;
    }
    r.ecx = spill;
    put_regs(sc, &r);
}

/* 0x4823da(P): the palette constructor ([P] 0x4a69f0, 0x4a3368, counter
 * +1, [P] 0x4a30f8, the words P+4 and P+0xc 0, counter +1). EAX P, EDX
 * and ECX the counter pointer (read twice). */
RD10_SERVICE(gl_palette_ctor) {
    RD10_BEGIN;
    static const char *name = "gl_palette_ctor";
    uint32_t p = sc->slots[0], c0, c1;
    w32(l, p, 0x004a69f0u);
    w32(l, p, 0x004a3368u);
    RD10_TRY(bump(l, 0x004823dau, name, 1u, &c0));
    w32(l, p, 0x004a30f8u);
    w16(l, p + 4u, 0);
    w16(l, p + 0xcu, 0);
    RD10_TRY(bump(l, 0x004823dau, name, 1u, &c1));
    sc->eax = p; sc->edx = c0; sc->ecx = c1;
}

/* 0x482410(F, flags), font vtable 0x4a31ac slot 6: counter -1; F == 0:
 * EAX the counter pointer. Else [F] 0x4a31ac, delete[] (0x47758c, POP
 * ECX) [F+0x1e] if the word F+0x18 is set, [F+0x22] if the dword F+0x1a
 * is, [F+0x26] if the word F+4 is, [F+0x2a] if the word F+0xc is 0xffff,
 * [F+0x2e] if the word F+8 is; 0x4838fd(F, 0); flags & 1: delete
 * 0x477548(F) (ECX F). */
RD10_SERVICE(gl_font_destroy) {
    RD10_BEGIN;
    uint32_t f = sc->slots[0], c;
    regs3 r = entry_regs(sc);
    RD10_TRY(bump(l, 0x00482410u, "gl_font_destroy", 0xffffffffu, &c));
    r.eax = c;
    if (f) {
        w32(l, f, 0x004a31acu);
        const struct { uint32_t test, width, value, block; } arrays[5] = {
            {0x18u, 2u, 0, 0x1eu}, {0x1au, 4u, 0, 0x22u}, {4u, 2u, 0, 0x26u}, {0xcu, 2u, 0xffffu, 0x2au}, {8u, 2u, 0xffffu, 0x2eu},
        };
        for (unsigned i = 0; i < 5u; ++i) {
            uint32_t v = arrays[i].width == 4u ? r32(l, f + arrays[i].test) : r16(l, f + arrays[i].test);
            if (arrays[i].value ? v != arrays[i].value : v == 0) continue;
            uint32_t a = r32(l, f + arrays[i].block);
            RD10_TRY(callr(l, VA_DELETE_ARRAY, &a, 1, &r));
            r.ecx = a;
        }
        const uint32_t b[2] = {f, 0};
        RD10_TRY(callr(l, VA_BASE_DESTROY, b, 2, &r));
        if (sc->slots[1] & 1u) {
            RD10_TRY(callr(l, VA_DELETE, &f, 1, &r));
            r.ecx = f;
        }
    }
    put_regs(sc, &r);
}

/* 0x482528(F): EAX the word F+0xa sign-extended (EDX, ECX kept). */
RD10_SERVICE(gl_font_height) { RD10_BEGIN; sc->eax = (uint32_t)sx16(l, sc->slots[0] + 0xau); }

/* 0x4827a1(a0, a1, a2, a3): the driver's slot 13 [0x4a57e0](a1, a2, a3). */
RD10_SERVICE(gl_driver_slot13) {
    RD10_BEGIN;
    regs3 r;
    const uint32_t a[3] = {sc->slots[1], sc->slots[2], sc->slots[3]};
    RD10_TRY(callr(l, r32(l, VA_DRIVER_SLOT13), a, 3, &r));
    put_regs(sc, &r);
}

/* 0x48287c(F) / 0x4828b8(F), slots 1 and 2: sx[F+0x18] * 4 + [F+0x1a] +
 * sx[F+4] * 4 + 0x1a (0x32), + sx[F+4] * 4 if the word F+0xc is 0xffff, +
 * sx[F+6] * 4 if the word F+8 is. EAX = EDX = the size, ECX sx[F+4] * 4. */
static void font_size(const es2_sim_loop *l, uint32_t f, uint32_t base, es2_sim_service_call *sc) {
    uint32_t c = (uint32_t)sx16(l, f + 4u) << 2;
    uint32_t d = ((uint32_t)sx16(l, f + 0x18u) << 2) + r32(l, f + 0x1au) + c + base;
    if (r16(l, f + 0xcu) == 0xffffu) d += c;
    if (r16(l, f + 8u) == 0xffffu) d += (uint32_t)sx16(l, f + 6u) << 2;
    sc->eax = d; sc->edx = d; sc->ecx = c;
}
RD10_SERVICE(gl_font_size_a) { RD10_BEGIN; font_size(l, sc->slots[0], 0x1au, sc); }
RD10_SERVICE(gl_font_size_b) { RD10_BEGIN; font_size(l, sc->slots[0], 0x32u, sc); }

/* 0x4828f4(F, S), slot 4: the eleven words F+4..F+0x18 (0x492447; the
 * upper halves from EAX, EDX, ECX in turn, as the previous call left
 * them), 0x482f0a(S, [F+0x1a]), 0x48f3ce(S, sx[F+0x18] * 4, [F+0x1e]),
 * 0x482bf0(S, [F+0x1a], [F+0x22]), 0x482c6e(S, sx[F+4], [F+0x26]); the
 * word F+0xc 0xffff: 0x482bf0(S, sx[F+4], [F+0x2a]); the word F+8 0xffff:
 * 0x491156(S, sx[F+6], [F+0x2e]). The registers are the last call's. */
RD10_SERVICE(gl_font_write) {
    RD10_BEGIN;
    uint32_t f = sc->slots[0], s = sc->slots[1];
    regs3 r = entry_regs(sc);
    uint32_t a[3] = {s, 0, 0};
    for (unsigned i = 0; i < 11u; ++i) {
        uint32_t up = i % 3u == 0 ? r.eax : i % 3u == 1u ? r.edx : r.ecx;
        a[1] = lo16(up, r16(l, f + 4u + 2u * i));
        RD10_TRY(callr(l, VA_STREAM_WORD, a, 2, &r));
    }
    a[1] = r32(l, f + 0x1au);
    RD10_TRY(callr(l, 0x00482f0au, a, 2, &r));
    a[1] = (uint32_t)sx16(l, f + 0x18u) << 2; a[2] = r32(l, f + 0x1eu);
    RD10_TRY(callr(l, 0x0048f3ceu, a, 3, &r));
    a[1] = r32(l, f + 0x1au); a[2] = r32(l, f + 0x22u);
    RD10_TRY(callr(l, 0x00482bf0u, a, 3, &r));
    a[1] = (uint32_t)sx16(l, f + 4u); a[2] = r32(l, f + 0x26u);
    RD10_TRY(callr(l, 0x00482c6eu, a, 3, &r));
    if (r16(l, f + 0xcu) == 0xffffu) {
        a[1] = (uint32_t)sx16(l, f + 4u); a[2] = r32(l, f + 0x2au);
        RD10_TRY(callr(l, 0x00482bf0u, a, 3, &r));
    }
    if (r16(l, f + 8u) == 0xffffu) {
        a[1] = (uint32_t)sx16(l, f + 6u); a[2] = r32(l, f + 0x2eu);
        RD10_TRY(callr(l, 0x00491156u, a, 3, &r));
    }
    put_regs(sc, &r);
}

/* The stream thunks (S, a, b): S slot +0x18 (read) or +0x14 (write) with
 * (S, a, b), a * 4 for the dword forms; 0x482f0a(S, v) writes the 4 bytes
 * of its own argument slot (S, 4, &v), 0x482f21(S, p) reads 4 bytes to p.
 * The registers are the slot's. */
static void stream(es2_sim_service_call *sc, void *user, uint32_t off, uint32_t a, uint32_t b) {
    RD10_BEGIN;
    regs3 r;
    uint32_t s = sc->slots[0];
    const uint32_t args[3] = {s, a, b};
    RD10_TRY(vcallr(l, s, off, args, 3, &r));
    put_regs(sc, &r);
}
RD10_SERVICE(gl_stream_read) { stream(sc, user, 0x18u, sc->slots[1], sc->slots[2]); }
RD10_SERVICE(gl_stream_write) { stream(sc, user, 0x14u, sc->slots[1], sc->slots[2]); }
RD10_SERVICE(gl_stream_write_dwords) { stream(sc, user, 0x14u, sc->slots[1] << 2, sc->slots[2]); }
RD10_SERVICE(gl_stream_read_dwords) { stream(sc, user, 0x18u, sc->slots[1] << 2, sc->slots[2]); }
RD10_SERVICE(gl_stream_write_dword) { stream(sc, user, 0x14u, 4u, sc->stack + 4u); }
RD10_SERVICE(gl_stream_read_dword) { stream(sc, user, 0x18u, 4u, sc->slots[1]); }

/* 0x482cec(O): the device surface 0x48d99e(O+4), then 0x482d68(surface,
 * O+4), the rect fill of O+4. EAX, EDX the fill's, ECX O+4. */
RD10_SERVICE(gl_object_rect_fill) {
    RD10_BEGIN;
    regs3 r;
    uint32_t a[2] = {sc->slots[0] + 4u, 0};
    RD10_TRY(callr(l, 0x0048d99eu, a, 1, &r));
    a[1] = a[0]; a[0] = r.eax;
    RD10_TRY(callr(l, 0x00482d68u, a, 2, &r));
    r.ecx = a[1];
    put_regs(sc, &r);
}
/* 0x482d44(a, p0, p1): the line 0x483a04(p0, p1); its registers. */
RD10_SERVICE(gl_line_thunk) {
    RD10_BEGIN;
    regs3 r;
    const uint32_t a[2] = {sc->slots[1], sc->slots[2]};
    RD10_TRY(callr(l, 0x00483a04u, a, 2, &r));
    put_regs(sc, &r);
}
/* 0x482d59 / 0x482d68 / 0x482d77 (a, p): the point 0x4895ec, rect fill
 * 0x486704, ellipse 0x48817c of p; EAX, EDX the callee's, ECX p. */
static void draw_thunk(es2_sim_service_call *sc, void *user, uint32_t va) {
    RD10_BEGIN;
    regs3 r;
    uint32_t p = sc->slots[1];
    RD10_TRY(callr(l, va, &p, 1, &r));
    r.ecx = p;
    put_regs(sc, &r);
}
RD10_SERVICE(gl_point_thunk) { draw_thunk(sc, user, 0x004895ecu); }
RD10_SERVICE(gl_rect_fill_thunk) { draw_thunk(sc, user, 0x00486704u); }
RD10_SERVICE(gl_ellipse_thunk) { draw_thunk(sc, user, 0x0048817cu); }

/* 0x482e08(A), bitmap array vtable 0x4a32ec slot 1: 4 plus 0x47a55c(e)
 * for each element e of [A+8] while i < [A+4] (signed, re-read); a zero
 * result ends it with EAX 0. ECX the sum so far (the local the prologue's
 * `push ecx` made, popped at the exit), EDX the last callee's (the entry
 * EDX without an element). */
RD10_SERVICE(gl_bitmaps_size_a) {
    RD10_BEGIN;
    uint32_t a = sc->slots[0], sum = 4u, result;
    regs3 r = entry_regs(sc);
    for (int32_t i = 0;; ++i) {
        if (!(i < s32(l, a + 4u))) { result = sum; break; }
        uint32_t e = r32(l, r32(l, a + 8u) + 4u * (uint32_t)i);
        RD10_TRY(callr(l, 0x0047a55cu, &e, 1, &r));
        if (!r.eax) { result = 0; break; }
        sum += r.eax;
    }
    r.eax = result;
    r.ecx = sum;
    put_regs(sc, &r);
}
/* 0x482e48(A), slot 2: [A+4] * 4 + 0xc plus each element's slot +8 (e).
 * ECX the last element, EDX the last callee's. */
RD10_SERVICE(gl_bitmaps_size_b) {
    RD10_BEGIN;
    uint32_t a = sc->slots[0], sum = r32(l, a + 4u) * 4u + 0xcu;
    regs3 r = entry_regs(sc);
    for (int32_t i = 0; i < s32(l, a + 4u); ++i) {
        uint32_t e = r32(l, r32(l, a + 8u) + 4u * (uint32_t)i);
        RD10_TRY(vcallr(l, e, 8u, &e, 1, &r));
        r.ecx = e;
        sum += r.eax;
    }
    r.eax = sum;
    put_regs(sc, &r);
}
/* 0x482ed0(A, S), slot 4: 0x482f0a(S, [A+4]), then 0x47a1ac(e, S, 0) for
 * each element. The registers are the last call's. */
RD10_SERVICE(gl_bitmaps_write) {
    RD10_BEGIN;
    uint32_t a = sc->slots[0], s = sc->slots[1];
    regs3 r;
    uint32_t args[3] = {s, r32(l, a + 4u), 0};
    RD10_TRY(callr(l, 0x00482f0au, args, 2, &r));
    for (int32_t i = 0; i < s32(l, a + 4u); ++i) {
        args[0] = r32(l, r32(l, a + 8u) + 4u * (uint32_t)i); args[1] = s; args[2] = 0;
        RD10_TRY(callr(l, 0x0047a1acu, args, 3, &r));
    }
    put_regs(sc, &r);
}
/* 0x482f38(A): [A] 0x4a69f0, 0x4a32ec, [A+4] = [A+8] = 0, counter +1;
 * EAX A, EDX 0, ECX 0. 0x482f80(A, b, c): [A+4] b, [A+8] c; EAX A, EDX
 * b, ECX c. */
RD10_SERVICE(gl_bitmaps_ctor) {
    RD10_BEGIN;
    uint32_t a = sc->slots[0];
    w32(l, a, 0x004a69f0u);
    w32(l, a, 0x004a32ecu);
    w32(l, a + 4u, 0);
    w32(l, a + 8u, 0);
    RD10_TRY(bump(l, 0x00482f38u, "gl_bitmaps_ctor", 1u, NULL));
    sc->eax = a; sc->edx = 0; sc->ecx = 0;
}
RD10_SERVICE(gl_bitmaps_set) {
    RD10_BEGIN;
    uint32_t a = sc->slots[0];
    w32(l, a + 4u, sc->slots[1]);
    w32(l, a + 8u, sc->slots[2]);
    sc->eax = a; sc->edx = sc->slots[1]; sc->ecx = sc->slots[2];
}

/* 0x48358c(P): [P] 0x4a69f0, 0x4a3368, counter +1, [P] 0x4a3340,
 * 0x49545b(P+8), [P+4] 0, counter +1. EAX P, EDX the callee's, ECX 0. */
RD10_SERVICE(gl_holder_ctor) {
    RD10_BEGIN;
    static const char *name = "gl_holder_ctor";
    uint32_t p = sc->slots[0], q = p + 8u;
    regs3 r;
    w32(l, p, 0x004a69f0u);
    w32(l, p, 0x004a3368u);
    RD10_TRY(bump(l, 0x0048358cu, name, 1u, NULL));
    w32(l, p, 0x004a3340u);
    RD10_TRY(callr(l, 0x0049545bu, &q, 1, &r));
    w32(l, p + 4u, 0);
    RD10_TRY(bump(l, 0x0048358cu, name, 1u, NULL));
    sc->eax = p; sc->edx = r.edx; sc->ecx = 0;
}
/* 0x4835c8(P, flags): the frame record 0x49630c (it reads [[0x6cbee4]];
 * not linked by the own code, as gouraud.c), counter -1; P == 0: EAX the
 * FS:0 head, EDX the counter pointer. Else [P] 0x4a3340; the inner object
 * I = [P+4], if set: counter += 0x496406(I, 0), I slot +0x18 (I, 3); then
 * 0x4838fd(P, 0) and, flags & 1, delete 0x477548(P); EAX, EDX the last
 * call's. ECX is the FS:0 head the epilogue restores on every path. */
RD10_SERVICE(gl_holder_destroy) {
    RD10_BEGIN;
    static const char *name = "gl_holder_destroy";
    const uint32_t va = 0x004835c8u;
    uint32_t p = sc->slots[0], head = fs0_head(), c;
    regs3 r;
    if (!r32(l, VA_COUNTER)) { null_refuse(l, va, name, "the construction counter pointer [0x6cbee4] is 0 (the frame record 0x49630c faults)"); failed(sc); return; }
    RD10_TRY(bump(l, va, name, 0xffffffffu, &c));
    r.eax = head; r.edx = c;
    if (p) {
        w32(l, p, 0x004a3340u);
        uint32_t inner = r32(l, p + 4u);
        if (inner) {
            uint32_t a[2] = {inner, 0};
            RD10_TRY(callr(l, 0x00496406u, a, 2, &r));
            RD10_TRY(bump(l, va, name, r.eax, NULL));
            a[1] = 3u;
            RD10_TRY(vcallr(l, inner, 0x18u, a, 2, &r));
        }
        const uint32_t b[2] = {p, 0};
        RD10_TRY(callr(l, VA_BASE_DESTROY, b, 2, &r));
        if (sc->slots[1] & 1u) RD10_TRY(callr(l, VA_DELETE, &p, 1, &r));
    }
    r.ecx = head;
    put_regs(sc, &r);
}
#undef RD10_TRY
#undef RD10_BEGIN
#undef RD10_SERVICE
