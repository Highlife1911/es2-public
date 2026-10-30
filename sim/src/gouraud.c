/* The Gouraud fill 0x487acc, its triangle 0x486ee8 and the terrain's flat fill
 * 0x46bc44 as own code (rasterizer.md, "Gouraud and flat fill";
 * es2/sim/gouraud.h). "Listing" = llvm-objdump -d -M intel of the reference
 * DBSIM.EXE (SHA-256 8561aa47...c4ad), ranges 0x487acc..0x487bd9,
 * 0x486ee8..0x487acb and 0x46bc44..0x46bd9a.
 *
 * C = [0x4a3614] the graphics context record (+0x228 the fill record F);
 * F: +0 kind (1 Gouraud), +4 colour word (low word: the ramp entry of
 * 0x4a4fa5), +0xc the shade words (one per point, 0..255). */
#include <string.h>
#include "es2/sim/gouraud.h"
#include "es2/sim/fill.h"
#include "es2/sim/gfx.h"
#include "es2/sim/memory.h"
#include "es2/sim/dispatch.h"
#include "es2/sim/services.h"

#define VA_CONTEXT ES2_SIM_VA_GFX_CONTEXT
#define VA_RAMPS ES2_SIM_VA_GFX_RAMPS
#define VA_COUNTER ES2_SIM_VA_COUNTER_POINTER
#define VA_PROJECT 0x0048cce4u          /* project the polygon's points (no row) */
#define VA_CLIP 0x0048cf20u             /* the near-plane clip */
#define VA_CLIP_SHADES 0x0048d38cu      /* the same with the shade words */
#define VA_SHAPE_FILL 0x0048d5c0u       /* fill the output polygon (0x4842bb) */
#define VA_SHADE_BASE 0x00468158u       /* the shade table of a colour */
#define VA_SHADE_LEVEL 0x00430e34u      /* the shade index of a level */
#define VA_MARKS 0x0042e8e8u            /* (object, 0, 0) (no row) */
#define VA_GC 0x006c5f70u               /* the current graphics context (C = GC + 4) */
#define VA_FILL_MODE 0x004aaaacu        /* 2: shaded (Gouraud) terrain */
#define VA_POINT_COUNT 0x006c68f0u      /* word */
#define VA_POINTS 0x006c68ecu
#define VA_INDEX_LIST 0x006c68f2u
#define VA_INDEX_DEFAULT 0x006c6068u
#define VA_VERTEX_COUNT 0x006c68e4u     /* word */
#define VA_VERTEX_FIRST 0x006c68e6u     /* word */
#define VA_STATES 0x006c68fau           /* one byte per point, up to 0x6c6ce2 */
#define VA_OUT_COUNT 0x006cbc02u        /* word: the clip's output count */
#define STATES 1000u                    /* 0x6c68fa..0x6c6ce1 */

static uint32_t r32(const es2_sim_loop *l, uint32_t a) { return l->memory->read32(l->memory->context, a); }
static uint16_t r16(const es2_sim_loop *l, uint32_t a) { return l->memory->read16(l->memory->context, a); }
static void w32(const es2_sim_loop *l, uint32_t a, uint32_t v) { l->memory->write32(l->memory->context, a, v); }
static void w16(const es2_sim_loop *l, uint32_t a, uint16_t v) { l->memory->write16(l->memory->context, a, v); }
static uint8_t r8(const es2_sim_loop *l, uint32_t a) { return es2_sim_read8(l->memory, a); }
static void w8(const es2_sim_loop *l, uint32_t a, uint8_t v) { es2_sim_write8(l->memory, a, v); }

static int refuse(es2_sim_loop *l, uint32_t va, const char *name, const char *reason) { return es2_sim_loop_refuse(l, va, name, reason); }
/* The original reads, writes or calls through NULL at va (es2_sim_loop_null_refuse). */
static int null_refuse(es2_sim_loop *l, uint32_t va, const char *name, const char *reason) { return es2_sim_loop_null_refuse(l, va, name, reason); }
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
static uint32_t fill_record(const es2_sim_loop *l) { return r32(l, r32(l, VA_CONTEXT) + 0x228u); }

/* ======================================================================
 * The triangle 0x486ee8(P, S). The points are sorted by shade: T0 the
 * (last) highest, T2 the (last) lowest, T1 the third; the edges are cut
 * into one step per shade level: A from T0 to T2 over D = hi - lo steps,
 * B from T0 to T1 over Dh = hi - mid, C from T1 to T2 over Dl = mid - lo
 * (step i: (low32((Tb - Ta) * i) / d) + Ta per coordinate, idiv). Band i is
 * the quad between the long edge A and the short edge B (or C), filled
 * with colour word hi - i (mid - i on C) through 0x483eb8(4, Q); its
 * winding follows the order of the input (flag: T2 does not follow T0).
 * The edge tables are 257-entry locals of the original: D > 256 runs them
 * into the locals (D >= 285) and the return address (D >= 295), refused. */
typedef struct { int32_t x, y; } pt;
static pt step(pt a, pt b, uint32_t i, int32_t d) {
    pt r;
    int32_t dx = (int32_t)((uint32_t)b.x - (uint32_t)a.x), dy = (int32_t)((uint32_t)b.y - (uint32_t)a.y);
    int32_t px = (int32_t)((uint32_t)dx * i), py = (int32_t)((uint32_t)dy * i);
    r.x = (int32_t)((uint32_t)(px / d) + (uint32_t)a.x);
    r.y = (int32_t)((uint32_t)(py / d) + (uint32_t)a.y);
    return r;
}
static void edge(pt *e, pt a, pt b, int32_t d) {
    e[0] = a;
    for (int32_t i = 1; i < d; ++i) e[i] = step(a, b, (uint32_t)i, d);
    e[d] = b;
}
static void put_pt(const es2_sim_loop *l, uint32_t at, pt p) {
    w32(l, at, (uint32_t)p.x);
    w32(l, at + 4u, (uint32_t)p.y);
}
/* One band: Q = {s0, s1, a1, a0} (flag) or {a0, a1, s1, s0}; the colour into F+4; the fill. */
static int band(es2_sim_loop *l, uint32_t q, int flag, pt a0, pt a1, pt s0, pt s1, uint32_t colour, uint32_t *eax) {
    if (flag) { put_pt(l, q, s0); put_pt(l, q + 8u, s1); put_pt(l, q + 16u, a1); put_pt(l, q + 24u, a0); }
    else { put_pt(l, q, a0); put_pt(l, q + 8u, a1); put_pt(l, q + 16u, s1); put_pt(l, q + 24u, s0); }
    w32(l, fill_record(l) + 4u, colour);
    return es2_sim_fill_polygon(l, 4u, q, eax);
}
int es2_sim_fill_gouraud_triangle(es2_sim_loop *l, uint32_t p, uint32_t s, uint32_t *eax) {
    static const char *name = "gouraud_triangle";
    const uint32_t va = ES2_SIM_VA_GOURAUD_TRIANGLE;
    pt in[3];
    for (unsigned k = 0; k < 3u; ++k) { in[k].x = (int32_t)r32(l, p + 8u * k); in[k].y = (int32_t)r32(l, p + 8u * k + 4u); }
    /* Two equal points: nothing; EAX the y the last test read. */
    if (in[0].x == in[1].x && in[0].y == in[1].y) { if (eax) *eax = (uint32_t)in[0].y; return 0; }
    if (in[0].x == in[2].x && in[0].y == in[2].y) { if (eax) *eax = (uint32_t)in[0].y; return 0; }
    if (in[2].x == in[1].x && in[2].y == in[1].y) { if (eax) *eax = (uint32_t)in[2].y; return 0; }
    uint16_t sh[3] = {r16(l, s), r16(l, s + 2u), r16(l, s + 4u)};
    uint16_t hi = sh[0] > sh[1] ? sh[0] : sh[1], lo = sh[0] < sh[1] ? sh[0] : sh[1];
    if (sh[2] > hi) hi = sh[2];
    if (sh[2] < lo) lo = sh[2];
    if (hi == lo) {   /* one colour: the triangle as it is */
        w32(l, fill_record(l) + 4u, hi);
        return es2_sim_fill_polygon(l, 3u, p, eax);
    }
    unsigned ihi = 0, ilo = 0;
    for (unsigned k = 0; k < 3u; ++k) {
        if (sh[k] == hi) ihi = k;
        if (sh[k] == lo) ilo = k;
    }
    int flag = (ihi + 1u) % 3u != ilo;
    unsigned imid = ihi + ilo == 1u ? 2u : ihi + ilo == 2u ? 1u : 0u;
    uint16_t mid = sh[imid];
    pt t0 = in[ihi], t1 = in[imid], t2 = in[ilo];
    int32_t d = (int32_t)hi - (int32_t)lo, dh = (int32_t)hi - (int32_t)mid, dl = (int32_t)mid - (int32_t)lo;
    if (d > 256) return refuse(l, va, name, "more than 256 shade steps (the edge tables overrun the original's frame)");
    static pt a[257], b[257], c[257];
    uint32_t top = l->stack_hi, q = frame_push(l, 0x20u);
    if (!q) return refuse(l, va, name, "the own code's call area is exhausted");
    edge(a, t0, t2, d);
    int bad = 0;
    if (dh == 0) {
        edge(c, t1, t2, dl);
        for (int32_t i = 0; !bad && i < d; ++i) bad = band(l, q, flag, a[i], a[i + 1], c[i], c[i + 1], (uint32_t)hi - (uint32_t)i, eax);
    } else if (dl == 0) {
        edge(b, t0, t1, dh);
        for (int32_t i = 0; !bad && i < d; ++i) bad = band(l, q, flag, a[i], a[i + 1], b[i], b[i + 1], (uint32_t)hi - (uint32_t)i, eax);
    } else {
        edge(b, t0, t1, dh);
        edge(c, t1, t2, dl);
        for (int32_t i = 0; !bad && i < dh; ++i) bad = band(l, q, flag, a[i], a[i + 1], b[i], b[i + 1], (uint32_t)hi - (uint32_t)i, eax);
        for (int32_t i = 0; !bad && i < dl; ++i)
            bad = band(l, q, flag, a[dh + i], a[dh + i + 1], c[i], c[i + 1], (uint32_t)mid - (uint32_t)i, eax);
    }
    frame_pop(l, top);
    return bad;
}

/* ======================================================================
 * The Gouraud fill 0x487acc(n, points), frame 0x60 laid out as the
 * original's (ebp-relative): -0x60 the triangle's three points, -0x48 the
 * shade colours L[] (words), -0x20 and -0x1c the point pointers of the
 * current triangle, -0x18 its three colour words, -8 the ramp table
 * pointer, -4 the saved colour word. F's kind becomes 0 for the run and 1
 * at the end, its colour word is restored. L[i] = the ramp table's word
 * (S[i] * count) >> 8 (the entry of F's low colour word; the 32-bit product
 * of the unsigned shade and the signed count). Past 20 points L runs into
 * the pointers, the colour words (24..26), the table pointer (32, 33, read
 * again per point) and the saved colour (34, 35); the triangle k is
 * {P0, P[k+1], P[k+2]} with colours {L0, L[k+1], L[k+2]} read from the
 * frame after the pointers are stored. From 37 points L overwrites the
 * saved EBP: refused. */
int es2_sim_fill_gouraud(es2_sim_loop *l, uint32_t n, uint32_t p, uint32_t *eax, uint32_t *edx, uint32_t *ecx) {
    static const char *name = "gouraud";
    const uint32_t va = ES2_SIM_VA_GOURAUD;
    int32_t sn = (int32_t)n, fan = (int32_t)(n - 2u);
    if (sn > 36) return refuse(l, va, name, "more than 36 points (the shade colours overwrite the saved EBP)");
    if (sn < 0 && fan > 0) return refuse(l, va, name, "a count of 0x80000000 or 0x80000001 (the fan loop runs over unset colours)");
    uint32_t top = l->stack_hi, f = frame_push(l, 0x60u);
    if (!f) return refuse(l, va, name, "the own code's call area is exhausted");
    const uint32_t bp = f + 0x60u, tri = bp - 0x60u, cols = bp - 0x48u, ptr_b = bp - 0x20u, ptr_a = bp - 0x1cu, trio = bp - 0x18u,
                   table = bp - 8u, saved = bp - 4u;
    uint32_t fr = fill_record(l);
    w32(l, fr, 0);
    uint32_t colour = r32(l, fr + 4u);
    w32(l, saved, colour);
    uint32_t e = VA_RAMPS + 8u * (colour & 0xffffu);
    int32_t count = (int16_t)r16(l, e + 4u);
    w32(l, table, r32(l, e));
    uint32_t shades = r32(l, fr + 0xcu);
    for (int32_t i = 0; i < sn; ++i) {
        uint32_t k = ((uint32_t)r16(l, shades + 2u * (uint32_t)i) * (uint32_t)count) >> 8;
        w16(l, cols + 2u * (uint32_t)i, r16(l, r32(l, table) + 2u * k));
    }
    w32(l, tri, r32(l, p));
    w32(l, tri + 4u, r32(l, p + 4u));
    w16(l, trio, r16(l, cols));
    int bad = 0;
    for (int32_t k = 0; !bad && k < fan; ++k) {
        w32(l, ptr_a, p + 8u * (uint32_t)k + 8u);
        uint32_t a = r32(l, ptr_a);
        w32(l, tri + 8u, r32(l, a));
        w32(l, tri + 12u, r32(l, r32(l, ptr_a) + 4u));
        w32(l, ptr_b, p + 8u * (uint32_t)k + 16u);
        uint32_t b = r32(l, ptr_b);
        w32(l, tri + 16u, r32(l, b));
        w32(l, tri + 20u, r32(l, r32(l, ptr_b) + 4u));
        w16(l, trio + 2u, r16(l, cols + 2u + 2u * (uint32_t)k));
        w16(l, trio + 4u, r16(l, cols + 4u + 2u * (uint32_t)k));
        bad = es2_sim_fill_gouraud_triangle(l, tri, trio, NULL);
    }
    if (!bad) {
        fr = fill_record(l);
        uint32_t c = r32(l, saved);
        w32(l, fr + 4u, c);
        w32(l, fr, 1u);
        if (eax) *eax = fr;
        if (edx) *edx = c;
        if (ecx) *ecx = n - 2u;
    }
    frame_pop(l, top);
    return bad;
}

/* ======================================================================
 * The flat fill 0x46bc44(points, n, shades), frame 0x50 laid out as the
 * original's: -0x50 the Gouraud record {1, 2, 0 (-0x48), shades (-0x44)},
 * -0x40 the clipped shade words, -0x34 the plain record {0, colour (-0x30)},
 * -0x2c the frame record of 0x49630c (not linked: own code; FS:0 is the
 * same at the return, as in the original; its context word -0x1c is
 * written), -0xc an object {0x4a0ca0, word 7} passed to 0x42e8e8. The
 * polygon is the render context's (0x6c68ec the points, n vertices from 0
 * of the list 0x6c6068, their states cleared); 0x48cce4 projects it (1: a
 * point lies behind the near plane, then 0x48cf20 clips it, and with the
 * shaded terrain [0x4aaaac] == 2 0x48d38c the shade words with it). GC+0x22c
 * ([0x6c5f70]) gets the Gouraud record (fill mode 2: ramp entry 2, the
 * shade words) or the plain record (colour byte [0x468158(0x4b) +
 * 0x430e34(2, shades[0])]); it is left on the dead local after the return.
 * The construction counter is up by two around 0x42e8e8 and 0x48d5c0. A
 * clip of more than 10 shade words overwrites the saved FS:0 head:
 * refused. More than 1000 points run the state bytes into the point
 * copies 0x6c6ce2: refused. */
static uint32_t fs0(void) {
#if defined(__i386__)
    uint32_t v;
    __asm__ volatile("movl %%fs:0, %0" : "=r"(v));
    return v;
#else
    return 0;
#endif
}
int es2_sim_flat_fill(es2_sim_loop *l, uint32_t points, uint32_t n, uint32_t shades, uint32_t *eax, uint32_t *edx, uint32_t *ecx) {
    static const char *name = "flat_fill";
    const uint32_t va = ES2_SIM_VA_FLAT_FILL;
    if (!r32(l, VA_COUNTER)) return null_refuse(l, va, name, "construction counter pointer [0x6cbee4] not set (the frame record faults)");
    if ((int32_t)n > (int32_t)STATES) return refuse(l, va, name, "more than 1000 points (the state bytes run into the point copies)");
    uint32_t top = l->stack_hi, f = frame_push(l, 0x50u);
    if (!f) return refuse(l, va, name, "the own code's call area is exhausted");
    const uint32_t bp = f + 0x50u, shaded = bp - 0x50u, clipped = bp - 0x40u, plain = bp - 0x34u, object = bp - 0xcu;
    w32(l, plain, 0);
    w32(l, shaded, 1u);
    w32(l, shaded + 8u, 0);
    w32(l, shaded + 0xcu, 0);
    w16(l, VA_POINT_COUNT, (uint16_t)n);
    w32(l, VA_POINTS, points);
    w32(l, VA_INDEX_LIST, VA_INDEX_DEFAULT);
    w16(l, VA_VERTEX_COUNT, (uint16_t)n);
    w16(l, VA_VERTEX_FIRST, 0);
    for (int32_t i = 0; i < (int32_t)n; ++i) w8(l, VA_STATES + (uint32_t)i, 0);
    uint32_t behind = 0;
    int bad = call(l, VA_PROJECT, NULL, 0, &behind);
    if (!bad && behind) {
        bad = call(l, VA_CLIP, NULL, 0, NULL);
        if (!bad && r32(l, VA_FILL_MODE) == 2u) {
            uint32_t args[2] = {shades, clipped};
            bad = call(l, VA_CLIP_SHADES, args, 2u, NULL);
            if (!bad && r16(l, VA_OUT_COUNT) > 10u) bad = refuse(l, va, name, "more than 10 clipped shade words (the saved FS:0 head is overwritten)");
            if (!bad) w32(l, shaded + 0xcu, clipped);
        }
    } else if (!bad && r32(l, VA_FILL_MODE) == 2u) {
        w32(l, shaded + 0xcu, shades);
    }
    if (!bad) {
        uint32_t gc = r32(l, VA_GC);
        if (r32(l, VA_FILL_MODE) == 2u) {
            w32(l, gc + 0x22cu, shaded);
            w32(l, shaded + 4u, 2u);
            w32(l, shaded, 1u);
        } else {
            w32(l, gc + 0x22cu, plain);
            uint32_t base = 0, level = 0, a1 = 0x4bu, a2[2] = {2u, r16(l, shades)};
            bad = call(l, VA_SHADE_BASE, &a1, 1u, &base) || call(l, VA_SHADE_LEVEL, a2, 2u, &level);
            if (!bad) {
                w32(l, plain + 4u, r8(l, base + level));
                w32(l, plain, 0);
            }
        }
    }
    if (!bad) {
        w16(l, bp - 0x1cu, 8u);
        uint32_t counter = r32(l, VA_COUNTER);
        w32(l, counter, r32(l, counter) + 1u);
        w32(l, object, 0x004a0ca0u);
        w16(l, object + 4u, 7u);
        counter = r32(l, VA_COUNTER);
        w32(l, counter, r32(l, counter) + 1u);
        uint32_t args[3] = {object, 0, 0};
        bad = call(l, VA_MARKS, args, 3u, NULL) || call(l, VA_SHAPE_FILL, NULL, 0, NULL);
    }
    if (!bad) {
        uint32_t counter = r32(l, VA_COUNTER);
        w32(l, counter, r32(l, counter) - 1u);
        counter = r32(l, VA_COUNTER);
        w32(l, counter, r32(l, counter) - 1u);
        if (eax) *eax = counter;
        if (edx) *edx = counter;
        if (ecx) *ecx = fs0();
    }
    frame_pop(l, top);
    return bad;
}

/* ======================================================================
 * The service rows (sim/dispatch/services/gouraud_fill.h). */
static int serve(es2_sim_service_call *sc, es2_sim_loop **l) {
    *l = es2_sim_loop_bound();
    if (!*l) { sc->refused = 1; sc->reason = "no main loop bound"; return 1; }
    return 0;
}
static void failed(es2_sim_service_call *sc) { sc->refused = 1; sc->reason = "a call inside the service was refused"; }
void es2_sim_service_fill_gouraud(es2_sim_service_call *sc, void *user) {
    es2_sim_loop *l; (void)user;
    if (serve(sc, &l)) return;
    uint32_t eax = sc->eax, edx = sc->edx, ecx = sc->ecx;
    if (es2_sim_fill_gouraud(l, sc->slots[0], sc->slots[1], &eax, &edx, &ecx)) { failed(sc); return; }
    sc->eax = eax; sc->edx = edx; sc->ecx = ecx;
}
void es2_sim_service_fill_gouraud_triangle(es2_sim_service_call *sc, void *user) {
    es2_sim_loop *l; (void)user;
    if (serve(sc, &l)) return;
    uint32_t eax = sc->eax;
    if (es2_sim_fill_gouraud_triangle(l, sc->slots[0], sc->slots[1], &eax)) { failed(sc); return; }
    sc->eax = eax;
}
void es2_sim_service_flat_fill(es2_sim_service_call *sc, void *user) {
    es2_sim_loop *l; (void)user;
    if (serve(sc, &l)) return;
    uint32_t eax = sc->eax, edx = sc->edx, ecx = sc->ecx;
    if (es2_sim_flat_fill(l, sc->slots[0], sc->slots[1], sc->slots[2], &eax, &edx, &ecx)) { failed(sc); return; }
    sc->eax = eax; sc->edx = edx; sc->ecx = ecx;
}
