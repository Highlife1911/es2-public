/* The polygon fills of DBSIM, original machine code against sim/src/poly.c
 * (--poly; target :differential_poly_test). Included after gfx_cases.h; reuses
 * startup_cases.h's DATA reset and snapshots (su_take, su_compare) and
 * loop_cases.h's loop reset (lc_reset, lc_machine).
 *
 * Both sides start from one synthetic world the case builds at random: a
 * 64x48 8-bit surface with its row table, the GC object (+0x208 the region
 * mode, the clip rect +0x210..+0x21c), polygons, a depth buffer, per-row
 * run lists, a shade table of 0x101 rows and a texture page table. The
 * DATA globals the fills read (the view 0x4a3652/0x4a3676, the GC pointer
 * 0x6c5f70, the recip table 0x4d3cbc, the shade and haze levels, four
 * texture pages at 0x4d4f04, the depth switch, buffer and tag, the run
 * pointer, the projection) are set identically after each side's reset.
 * The original side runs the machine code (lc_machine); the own side calls
 * poly.c directly on the harness loop, whose dispatch answers the point
 * transform 0x48043c with the original's code. Compared after every case:
 * EAX where the function defines it, DATA and the world (differing bytes
 * are counted; no pixels are kept). The span loops save the caller's ESP
 * at 0x4a09cc; that dword is a host address on both sides and is copied
 * from the original's snapshot into the own one before the comparison.
 *
 * - helpers: 0x47e068, 0x47e07d, 0x47e08d, 0x471cd8, 0x471cf0, 0x471d08,
 *   0x471d26, 0x471d61, 0x4680e0, 0x4680f0, 0x468158, 0x469dc8, 0x469ddc.
 * - cut: 0x469894, 0x469b2c with every flag word, on-bound end points.
 * - clip: 0x4699c8, 0x469c64 over random polygons of 1..8 vertices.
 * - edges: 0x477d48 with flag words 0..15 (2 only with 1), duplicates,
 *   degenerate and clipped polygons.
 * - span: 0x46ac14, 0x46ad4c, 0x46aeb1 (odd and even ends, opaque and
 *   transparent), the run clippers 0x48b524, 0x48b5af, 0x48b63a and the
 *   depth runs 0x48b854.
 * - fill: 0x468414 in modes 0..2, depth on and off, region mode 2 with and
 *   without run lists.
 * - fill3d: 0x46817c with transformed and untransformed points, the near
 *   clip, modes 0..2. */
#pragma once
/* The headers this suite builds on (sim_main.c). */
#include "init_records_cases.h"
#include "startup_cases.h"
#include "loop_cases.h"
#include "input_layer_cases.h"
#include "gfx_cases.h"
#include "es2/sim/poly.h"

#define PY_SIZE 0x30000u
#define PY_W 64
#define PY_H 48
/* World offsets. */
#define PY_ROWS 0x000u       /* the row table: dword bits, then y * pitch */
#define PY_GC 0x400u         /* the GC object (0x240 bytes) */
#define PY_VERTS 0x800u      /* 8 vertices of 28 bytes; a second set at +0x100 */
#define PY_POINTS 0xa00u     /* 8 points of 12 bytes */
#define PY_UV 0xb00u         /* 8 pairs */
#define PY_SHADES 0xb80u     /* 8 dwords */
#define PY_SHADE 0xbc0u      /* the mode-1 shade */
#define PY_RUN_POINTER 0xf00u
#define PY_RUN_ROWS 0x1000u  /* 64 rows {count, pointer} */
#define PY_RUNS 0x1400u      /* 64 rows x 4 runs {start, length} */
#define PY_OUT 0x2000u       /* clip and helper outputs */
#define PY_PAGE 0x2400u      /* a page table (0x808 bytes) */
#define PY_BITS 0x3000u      /* the surface (0xc00 bytes and slack) */
#define PY_DEPTH 0x4000u     /* the depth buffer (64 x 48 dwords and slack) */
#define PY_SHADE_TABLE 0x10000u  /* 0x101 rows of 256 bytes */

static BYTE *py_world, *py_world_start, *py_snap[2];
static int py_bad;
static unsigned py_cov_transparent, py_cov_odd, py_cov_runs_skip, py_cov_depth_hits, py_cov_near_cut, py_cov_flag2, py_cov_empty;
static uint32_t py_va(uint32_t off) { return (uint32_t)(uintptr_t)(py_world + off); }
static void py_put32(uint32_t off, uint32_t v) { put32(py_world_start + off, v); }
static uint32_t py_r(uint32_t n) { return random32() % n; }
static int32_t py_ri(int32_t lo, int32_t hi) { return lo + (int32_t)py_r((uint32_t)(hi - lo + 1)); }
static void py_fill_random(uint32_t off, uint32_t n) { for (uint32_t i = 0; i < n; ++i) py_world_start[off + i] = (BYTE)random32(); }

/* The own side's dispatch: the service rows and the point transform as the original. */
static es2_sim_entry py_entries[4096];
static es2_sim_dispatch py_dispatch;
static void py_refuse(const es2_sim_refusal *r, void *user) {
    (void)user;
    fprintf(stderr, "FAIL: own poly: refused call to 0x%08lx (%s): %s\n", (unsigned long)r->va, r->name ? r->name : "?", r->reason ? r->reason : "");
    py_bad = 1;
}
static int py_dispatch_setup(void) {
    es2_sim_dispatch_init(&py_dispatch, py_entries, 4096, py_refuse, NULL);
    static es2_sim_services s;
    s.memory = &ir_memory;
    s.heap = &ir_heap;
    s.vt = &lc_vt;
    s.fs0 = NULL;
    if (es2_sim_register_services(&py_dispatch, &s)) return fail("poly: dispatch");
    if (es2_sim_dispatch_add_service(&py_dispatch, 0x0048043cu, "transform_point (original)", gx_original, (void *)(uintptr_t)3u, 0, 3))
        return fail("poly: dispatch");
    const char *error = NULL;
    uint32_t bad = 0;
    if (es2_sim_dispatch_seal(&py_dispatch, &error, &bad)) return fail("poly: dispatch seal");
    return 0;
}

/* The DATA globals both sides read, set after the side's reset. */
static uint32_t py_depth_on, py_runs_on, py_shades, py_levels, py_haze, py_far, py_near, py_shift, py_divisor, py_tag;
static int32_t py_matrix[8];
static BYTE py_texture[4][0x100];   /* rows of each page's first 256 bytes, repeated */
static void py_side(unsigned side) {
    lc_reset(side, 47, 0, 1);
    lc_loop.dispatch = &py_dispatch;
    memcpy(py_world, py_world_start, PY_SIZE);
    BYTE *d = mapped - 0x400000u;
    put32(d + 0x4a3652u, py_va(PY_ROWS));
    put32(d + 0x4a3676u, PY_W);
    put32(d + 0x6c5f70u, py_va(PY_GC));
    put32(d + 0x4d3cbcu, 0);
    for (uint32_t i = 1; i < 0x280u; ++i) put32(d + 0x4d3cbcu + 4u * i, 0x10000u / i);
    put32(d + 0x4d3cb4u, py_shades);
    put32(d + 0x4d3cb8u, py_levels);
    put32(d + 0x4a08acu, py_far);
    put32(d + 0x4a08b0u, py_haze);
    put32(d + 0x4a0998u, py_va(PY_SHADE_TABLE));
    put32(d + 0x4a08b4u, py_va(PY_PAGE));
    for (uint32_t p = 0; p < 4u; ++p)
        for (uint32_t r = 0; r < 0x100u; ++r) memcpy(d + 0x4d4f04u + 0x10000u * p + 0x100u * r, py_texture[(p + r) & 3u], 0x100u);
    put32(d + 0x49f248u, py_depth_on);
    put32(d + 0x6c5f90u, py_va(PY_DEPTH));
    put32(d + 0x4a5aecu, py_tag);
    put32(d + 0x4a5af8u, py_runs_on ? py_va(PY_RUN_POINTER) : 0);
    put32(d + 0x6c6028u, py_shift);
    put32(d + 0x6c602cu, py_near);
    put32(d + 0x6c6030u, py_divisor);
    put32(d + 0x6c6034u, 32u);
    put32(d + 0x6c6038u, 24u);
    for (unsigned i = 0; i < 8u; ++i) put32(d + 0x6c5fb4u + 4u * i, (uint32_t)py_matrix[i]);
}
static void py_end(unsigned side) {
    __asm__ volatile("cld" ::: "cc");
    su_take(side);
    memcpy(py_snap[side], py_world, PY_SIZE);
}
static char py_desc[256];
static int py_compare(const char *what, unsigned k, uint32_t eax0, uint32_t eax1, int eax_defined) {
    /* The saved ESP of the span loops (0x4a09cc) is a host address. */
    su_snap_same(0x4a09ccu, 4u);
    if (py_bad || (eax_defined && eax0 != eax1) || memcmp(py_snap[0], py_snap[1], PY_SIZE)) fprintf(stderr, "  case %u: %s\n", k, py_desc);
    if (py_bad) { fprintf(stderr, "FAIL: poly %s case %u: a call was refused\n", what, k); return 1; }
    if (eax_defined && eax0 != eax1) {
        fprintf(stderr, "FAIL: poly %s case %u: EAX %08lx original, %08lx own\n", what, k, (unsigned long)eax0, (unsigned long)eax1);
        return 1;
    }
    if (su_compare(what, k)) { fprintf(stderr, "  case %u: %s\n", k, py_desc); return 1; }
    if (memcmp(py_snap[0], py_snap[1], PY_SIZE)) {
        unsigned count = 0, shown = 0;
        for (uint32_t i = 0; i < PY_SIZE; ++i) {
            if (py_snap[0][i] == py_snap[1][i]) continue;
            if (shown++ < 16) fprintf(stderr, "  world +0x%05lx original %02x own %02x\n", (unsigned long)i, py_snap[0][i], py_snap[1][i]);
            ++count;
        }
        fprintf(stderr, "FAIL: poly %s case %u: %u world bytes differ\n", what, k, count);
        return 1;
    }
    return 0;
}

/* ---- the world a case starts from */
static void py_world_reset(void) {
    memset(py_world_start, 0, PY_SIZE);
    py_put32(PY_ROWS, py_va(PY_BITS));
    for (int32_t y = 0; y < PY_H + 16; ++y) py_put32(PY_ROWS + 4u + 4u * (uint32_t)y, (uint32_t)(y * PY_W));
    py_fill_random(PY_BITS, 0x1000u);
    py_fill_random(PY_DEPTH, 0x3400u);
    py_fill_random(PY_SHADE_TABLE, 0x10100u);
    py_fill_random(PY_OUT, 0x400u);
    py_fill_random(PY_PAGE, 0x810u);
    for (unsigned p = 0; p < 4u; ++p)
        for (unsigned i = 0; i < 0x100u; ++i) py_texture[p][i] = py_r(4u) ? (BYTE)random32() : 0;
    /* The GC: region mode 0 or 2; the clip rect inside the surface. */
    py_put32(PY_GC + 0x208u, py_r(3u) ? 0 : 2u);
    int32_t l = py_ri(0, 24), t = py_ri(0, 20);
    py_put32(PY_GC + 0x210u, (uint32_t)l);
    py_put32(PY_GC + 0x214u, (uint32_t)t);
    py_put32(PY_GC + 0x218u, (uint32_t)py_ri(l, PY_W - 2));
    py_put32(PY_GC + 0x21cu, (uint32_t)py_ri(t, PY_H - 2));
    /* Run lists: 64 rows of 0..4 sorted runs; the pointer cell. */
    py_put32(PY_RUN_POINTER, py_va(PY_RUN_ROWS));
    for (uint32_t y = 0; y < 64u; ++y) {
        uint32_t n = py_r(5u), runs = PY_RUNS + 0x20u * y;
        int32_t x = py_ri(-6, 20);
        py_put32(PY_RUN_ROWS + 8u * y, n);
        py_put32(PY_RUN_ROWS + 8u * y + 4u, py_va(runs));
        for (uint32_t i = 0; i < 4u; ++i) {
            int32_t len = py_ri(1, 16);
            py_put32(runs + 8u * i, (uint32_t)x);
            py_put32(runs + 8u * i + 4u, (uint32_t)len);
            x += len + py_ri(0, 12);
        }
    }
    py_depth_on = py_r(3u) == 0;
    py_runs_on = py_r(4u) != 0;
    py_shades = 1u + py_r(32u);
    py_levels = 1u + py_r(16u);
    py_haze = py_r(0x40u) << 8;
    py_far = (uint32_t)py_ri(1, 0x4000);
    py_near = (uint32_t)py_ri(1, 40);
    py_shift = py_r(4u) ? 8u : 0;
    py_divisor = (uint32_t)py_ri(1, 0x200);
    py_tag = py_r(0x100u);
    for (unsigned i = 0; i < 8u; ++i) py_matrix[i] = py_ri(-0x400, 0x400);
}
/* A vertex at `off`: x, y in the given ranges, the fields at random within
 * the ranges the fills use (z small, u/v Q16 texture coordinates, w a
 * positive divisor, the shade a Gouraud accumulator). */
static void py_vertex(uint32_t off, int32_t x0, int32_t x1, int32_t y0, int32_t y1) {
    py_put32(off, (uint32_t)py_ri(x0, x1));
    py_put32(off + 4u, (uint32_t)py_ri(y0, y1));
    py_put32(off + 8u, (uint32_t)py_ri(0, 0x3fffff));
    py_put32(off + 0xcu, (uint32_t)py_ri(-0x40000, 0x400000));
    py_put32(off + 0x10u, (uint32_t)py_ri(-0x40000, 0x400000));
    py_put32(off + 0x14u, (uint32_t)py_ri(0x100, 0x10000));
    py_put32(off + 0x18u, (uint32_t)py_ri(0x40000, 0xc00000));
}
static void py_polygon(uint32_t off, unsigned n, int32_t x0, int32_t x1, int32_t y0, int32_t y1) {
    for (unsigned i = 0; i < n; ++i) {
        py_vertex(off + 28u * i, x0, x1, y0, y1);
        if (i && py_r(8u) == 0) {   /* a duplicate of the previous or the first point */
            uint32_t from = off + 28u * (py_r(2u) ? i - 1u : 0);
            py_put32(off + 28u * i, get32(py_world_start + from));
            py_put32(off + 28u * i + 4u, get32(py_world_start + from + 4u));
        }
    }
}
static uint32_t py_flags(void) {
    uint32_t f = py_r(16u);
    if ((f & 2u) && !(f & 1u)) f &= ~2u;
    return f;
}

/* ---- helpers */
static int py_helper_case(unsigned k) {
    py_world_reset();
    uint32_t which = py_r(13u), eax[2] = {0, 0}, a0, a1, a2, a3;
    int defined = 1;
    int32_t x = py_ri(-0x7fff, 0x7fff), y;
    do y = py_r(2u) ? py_ri(-0x7fffffff, 0x7fffffff) : py_ri(-0x100, 0x100); while (y == 0 || ((int64_t)x * 65536) / y > INT32_MAX || ((int64_t)x * 65536) / y < INT32_MIN);
    a0 = (uint32_t)x; a1 = (uint32_t)y;
    a2 = random32(); a3 = random32();
    uint32_t distance = (uint32_t)py_ri(-0x100, 0x5000);
    py_fill_random(PY_OUT, 0x40u);
    if (which == 6u || which == 7u) {
        /* Dividends whose quotients fit: small 64-bit values over a divisor. */
        int32_t dv = py_ri(1, 0x10000) * (py_r(2u) ? 1 : -1);
        for (unsigned i = 0; i < 2u; ++i) {
            /* q d + r with |q| below 2^15 (0x471d26 shifts the dividend by 16) or 2^30. */
            int64_t q = which == 6u ? py_ri(-0x7ffe, 0x7ffe) : py_ri(-0x3fffffff, 0x3fffffff);
            int64_t v = q * dv + py_ri(0, dv < 0 ? -dv - 1 : dv - 1) * (q < 0 ? -1 : 1);
            py_put32(PY_OUT + 8u * i, (uint32_t)v);
            py_put32(PY_OUT + 8u * i + 4u, (uint32_t)((uint64_t)v >> 32));
        }
        py_put32(PY_OUT + 0x10u, which == 6u ? (py_r(5u) ? (uint32_t)dv : 0) : (uint32_t)py_ri(-0x7fff, 0x7fff));
        a2 = (uint32_t)dv;
        a3 = py_r(16u);
    }
    snprintf(py_desc, sizeof(py_desc), "helper %u args %08lx %08lx %08lx %08lx", which, (unsigned long)a0, (unsigned long)a1, (unsigned long)a2, (unsigned long)a3);
    for (unsigned side = 0; side < 2; ++side) {
        py_side(side);
        uint32_t o = py_va(PY_OUT), r = py_va(PY_OUT + 0x20u), e = 0;
        int bad = 0;
        switch (which) {
        case 0: { uint32_t a[2] = {a0, a1}; if (!side) e = lc_machine(0x0047e068u, a, 2, NULL); else bad = es2_sim_poly_div16(&lc_loop, 0x0047e068u, (int32_t)a0, (int32_t)a1, &e); break; }
        case 1: { uint32_t a[2] = {a2, a3}; if (!side) e = lc_machine(0x0047e07du, a, 2, NULL); else e = es2_sim_poly_mul16((int32_t)a2, (int32_t)a3); break; }
        case 2: { uint32_t a[2] = {a2, a3}; if (!side) e = lc_machine(0x0047e08du, a, 2, NULL); else e = es2_sim_poly_mul16((int32_t)a2, (int32_t)a3); break; }
        case 3: { uint32_t a[4] = {o, o + 4u, a2, a3}; if (!side) e = lc_machine(0x00471cd8u, a, 4, NULL); else { bad = es2_sim_poly_add64(&lc_loop, o, o + 4u, a2, a3); e = a2; } break; }
        case 4: { uint32_t a[4] = {o, o + 4u, a2, a3}; if (!side) e = lc_machine(0x00471cf0u, a, 4, NULL); else { bad = es2_sim_poly_sub64(&lc_loop, o, o + 4u, a2, a3); e = a2; } break; }
        case 5: { uint32_t a[3] = {o, o + 4u, a3 & 63u}; if (!side) e = lc_machine(0x00471d08u, a, 3, NULL); else { e = get32((BYTE *)(uintptr_t)(o + 4u)); bad = es2_sim_poly_shift64(&lc_loop, o, o + 4u, a3 & 63u); } break; }
        case 6: { uint32_t a[2] = {r, o}; defined = 0; if (!side) lc_machine(0x00471d26u, a, 2, NULL); else bad = es2_sim_poly_divide_pair(&lc_loop, r, o); break; }
        case 7: { uint32_t a[4] = {r, o, a2, a3}; defined = 0; if (!side) lc_machine(0x00471d61u, a, 4, NULL); else bad = es2_sim_poly_divide_three(&lc_loop, r, o, (int32_t)a2, a3); break; }
        case 8: { uint32_t a[1] = {a2}; if (!side) e = lc_machine(0x004680e0u, a, 1, NULL); else es2_sim_poly_set_far(&lc_loop, a2, &e); break; }
        case 9: { uint32_t a[1] = {distance}; if (!side) e = lc_machine(0x004680f0u, a, 1, NULL); else bad = es2_sim_poly_haze(&lc_loop, (int32_t)distance, &e); break; }
        case 10: { uint32_t a[1] = {a3 & 0xffu}; if (!side) e = lc_machine(0x00468158u, a, 1, NULL); else es2_sim_poly_shade_colour(&lc_loop, (int32_t)(a3 & 0xffu), &e); break; }
        case 11: { uint32_t a[1] = {a2}; if (!side) e = lc_machine(0x00469dc8u, a, 1, NULL); else es2_sim_poly_page_set(&lc_loop, a2, &e); break; }
        default: { uint32_t a[2] = {py_va(PY_PAGE), a2}; if (!side) e = lc_machine(0x00469ddcu, a, 2, NULL); else es2_sim_poly_page_init(&lc_loop, py_va(PY_PAGE), a2, &e); break; }
        }
        if (bad) py_bad = 1;
        eax[side] = e;
        py_end(side);
    }
    return py_compare("helpers", k, eax[0], eax[1], defined);
}

/* ---- cut */
static int py_cut_case(unsigned k) {
    py_world_reset();
    uint32_t axis = py_r(2u), flags = py_r(16u), eax[2] = {0, 0}, a = PY_VERTS, b = PY_VERTS + 0x100u;
    py_vertex(a, -100, 200, -100, 200);
    py_vertex(b, -100, 200, -100, 200);
    uint32_t k4 = 4u * axis;
    int32_t ca = (int32_t)get32(py_world_start + a + k4), cb = (int32_t)get32(py_world_start + b + k4);
    if (ca == cb) py_put32(b + k4, (uint32_t)(cb += 1));
    int32_t c = py_ri(ca < cb ? ca : cb, ca < cb ? cb : ca);
    if (py_r(8u) == 0) c = py_r(2u) ? ca : cb;
    snprintf(py_desc, sizeof(py_desc), "cut axis %u flags %u c %ld a %ld b %ld", axis, flags, (long)c, (long)ca, (long)cb);
    for (unsigned side = 0; side < 2; ++side) {
        py_side(side);
        uint32_t args[5] = {(uint32_t)c, py_va(a), py_va(b), py_va(PY_OUT), flags};
        if (!side) eax[0] = lc_machine(axis ? 0x00469b2cu : 0x00469894u, args, 5, NULL);
        else if ((axis ? es2_sim_poly_cut_y : es2_sim_poly_cut_x)(&lc_loop, c, args[1], args[2], args[3], flags, &eax[1])) py_bad = 1;
        py_end(side);
    }
    return py_compare("cut", k, eax[0], eax[1], 1);
}

/* ---- clip */
static int py_clip_case(unsigned k) {
    py_world_reset();
    uint32_t axis = py_r(2u), flags = py_r(16u), n = 1u + py_r(8u), eax[2] = {0, 0};
    py_polygon(PY_VERTS, n, -30, 90, -30, 80);
    snprintf(py_desc, sizeof(py_desc), "clip axis %u flags %u n %u", axis, flags, n);
    for (unsigned side = 0; side < 2; ++side) {
        py_side(side);
        uint32_t args[4] = {n, py_va(PY_VERTS), py_va(PY_OUT), flags};
        if (!side) eax[0] = lc_machine(axis ? 0x00469c64u : 0x004699c8u, args, 4, NULL);
        else if ((axis ? es2_sim_poly_clip_y : es2_sim_poly_clip_x)(&lc_loop, (int32_t)n, args[1], args[2], flags, &eax[1])) py_bad = 1;
        py_end(side);
    }
    return py_compare("clip", k, eax[0], eax[1], 1);
}

/* ---- edges */
static int py_edges_case(unsigned k) {
    py_world_reset();
    uint32_t flags = py_flags(), n = 1u + py_r(8u), eax[2] = {0, 0};
    int32_t t = (int32_t)get32(py_world_start + PY_GC + 0x214u), b = (int32_t)get32(py_world_start + PY_GC + 0x21cu);
    if (py_r(4u)) py_polygon(PY_VERTS, n, -10, 70, t, b + 1);
    else py_polygon(PY_VERTS, n, -10, 70, t - 6, b + 8);
    if (py_r(10u) == 0) for (uint32_t i = 0; i < n; ++i) py_put32(PY_VERTS + 28u * i + 4u, (uint32_t)t);   /* flat */
    if (flags & 2u) ++py_cov_flag2;
    snprintf(py_desc, sizeof(py_desc), "edges flags %u n %u clip y %ld..%ld", flags, n, (long)t, (long)b);
    for (unsigned side = 0; side < 2; ++side) {
        py_side(side);
        uint32_t args[3] = {n, py_va(PY_VERTS), flags};
        if (!side) eax[0] = lc_machine(0x00477d48u, args, 3, NULL);
        else if (es2_sim_poly_edges(&lc_loop, (int32_t)n, args[1], flags, &eax[1])) py_bad = 1;
        py_end(side);
    }
    return py_compare("edges", k, eax[0], eax[1], 1);
}

/* ---- span */
static int py_span_case(unsigned k) {
    py_world_reset();
    uint32_t kind = py_r(7u), eax[2] = {0, 0};
    int32_t y = py_ri(0, PY_H - 1), x = py_ri(0, PY_W - 1), w = py_ri(1, PY_W - x);
    uint32_t transparent = py_r(2u) ? 0 : (py_r(2u) ? 1u : 0x80u);
    uint32_t u = random32(), v = random32(), du = (uint32_t)py_ri(-0x30000, 0x30000), dv = (uint32_t)py_ri(-0x30000, 0x30000);
    uint32_t page = py_r(4u), shade = (0x20u + py_r(0xa0u)) << 16 | py_r(0x10000u), dshade = (uint32_t)py_ri(-0x8000, 0x8000);
    uint32_t runs = py_va(PY_RUNS + 0x20u * py_r(64u)), count = py_r(5u);
    uint32_t dz = (uint32_t)py_ri(-0x40000, 0x40000), z = random32();
    if (transparent) ++py_cov_transparent;
    if (x & 1) ++py_cov_odd;
    snprintf(py_desc, sizeof(py_desc), "span kind %u x %ld y %ld w %ld transparent %u", kind, (long)x, (long)y, (long)w, transparent);
    for (unsigned side = 0; side < 2; ++side) {
        py_side(side);
        BYTE *d = mapped - 0x400000u;
        put32(d + 0x4a0968u, py_va(PY_BITS) + (uint32_t)(y * PY_W + x));
        put32(d + 0x4a096cu, (uint32_t)w);
        put32(d + 0x4a0978u, u);
        put32(d + 0x4a0980u, v);
        put32(d + 0x4a0988u, du);
        put32(d + 0x4a0990u, dv);
        put32(d + 0x4a0994u, transparent);
        put32(d + 0x4a099cu, page);
        put32(d + 0x4a09acu, shade);
        put32(d + 0x4a09b0u, dshade);
        put32(d + 0x4a593cu, runs);
        put32(d + 0x4a5938u, count);
        put32(d + 0x4a5940u, (uint32_t)x);
        put32(d + 0x4a5adcu, py_va(PY_DEPTH) + 4u * (uint32_t)(y * PY_W + x));
        put32(d + 0x4a5ad8u, (uint32_t)x);
        put32(d + 0x4a5ae0u, z);
        put32(d + 0x4a5ae4u, dz);
        put32(d + 0x4a5ae8u, (uint32_t)w);
        static const uint32_t va[7] = {0x0046ac14u, 0x0046ad4cu, 0x0046aeb1u, 0x0048b524u, 0x0048b5afu, 0x0048b63au, 0x0048b854u};
        if (!side) eax[0] = lc_machine(va[kind], NULL, 0, NULL);
        else if (kind < 3u) es2_sim_poly_span(&lc_loop, kind, &eax[1]);
        else if (kind < 6u) es2_sim_poly_span_runs(&lc_loop, kind - 3u, &eax[1]);
        else es2_sim_poly_depth_runs(&lc_loop, &eax[1]);
        if (kind == 6u && side && get32(d + 0x4a5938u)) ++py_cov_depth_hits;
        py_end(side);
    }
    return py_compare("span", k, eax[0], eax[1], kind < 3u || kind == 6u);
}

/* ---- fill */
static int py_fill_case(unsigned k) {
    py_world_reset();
    uint32_t n = 1u + py_r(8u), mode = py_r(3u), word = py_r(4u), flags = py_r(2u) ? 0 : 1u, eax[2] = {0, 0};
    py_polygon(PY_VERTS, n, -20, 84, -20, 68);
    py_put32(PY_SHADE, py_r(16u));
    snprintf(py_desc, sizeof(py_desc), "fill n %u mode %u depth %u runs %u region %u", n, mode, py_depth_on, py_runs_on,
             get32(py_world_start + PY_GC + 0x208u));
    for (unsigned side = 0; side < 2; ++side) {
        py_side(side);
        uint32_t args[6] = {n, py_va(PY_VERTS), mode, word, py_va(PY_SHADE), flags};
        if (!side) eax[0] = lc_machine(0x00468414u, args, 6, NULL);
        else if (es2_sim_poly_fill(&lc_loop, (int32_t)n, args[1], (int32_t)mode, word, args[4], flags, &eax[1])) py_bad = 1;
        if (side && !eax[1]) ++py_cov_empty;
        if (side && get32(mapped - 0x400000u + 0x4a5938u) == 0 && py_runs_on) ++py_cov_runs_skip;
        py_end(side);
    }
    return py_compare("fill", k, eax[0], eax[1], 1);
}

/* ---- fill3d */
static int py_fill3d_case(unsigned k) {
    py_world_reset();
    uint32_t n = 1u + py_r(6u), mode = py_r(3u), word = py_r(4u), flags = py_r(2u) ? 0 : 1u, transformed = py_r(4u) != 0, eax[2] = {0, 0};
    for (uint32_t i = 0; i < n; ++i) {
        py_put32(PY_POINTS + 12u * i, (uint32_t)py_ri(-400, 400));
        py_put32(PY_POINTS + 12u * i + 4u, (uint32_t)py_ri(-60, 400));
        py_put32(PY_POINTS + 12u * i + 8u, (uint32_t)py_ri(-300, 300));
        py_put32(PY_UV + 8u * i, py_r(0x10000u));
        py_put32(PY_UV + 8u * i + 4u, py_r(0x10000u));
        py_put32(PY_SHADES + 4u * i, py_r(16u));
        if ((int32_t)get32(py_world_start + PY_POINTS + 12u * i + 4u) < (int32_t)py_near) ++py_cov_near_cut;
    }
    snprintf(py_desc, sizeof(py_desc), "fill3d n %u mode %u transformed %u near %lu shift %lu depth %u", n, mode, transformed,
             (unsigned long)py_near, (unsigned long)py_shift, py_depth_on);
    for (unsigned side = 0; side < 2; ++side) {
        py_side(side);
        uint32_t args[8] = {py_va(PY_POINTS), py_va(PY_UV), n, word, py_va(PY_SHADES), mode, transformed, flags};
        if (!side) eax[0] = lc_machine(0x0046817cu, args, 8, NULL);
        else if (es2_sim_poly_fill_3d(&lc_loop, args[0], args[1], (int32_t)n, word, args[4], (int32_t)mode, transformed, flags, &eax[1])) py_bad = 1;
        py_end(side);
    }
    return py_compare("fill3d", k, eax[0], eax[1], 1);
}

static LONG CALLBACK py_fault(EXCEPTION_POINTERS *fault) {
    DWORD code = fault->ExceptionRecord->ExceptionCode;
    if ((code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_INT_DIVIDE_BY_ZERO || code == EXCEPTION_INT_OVERFLOW) && py_desc[0])
        fprintf(stderr, "  case parameters: %s\n", py_desc);
    return EXCEPTION_CONTINUE_SEARCH;
}
static int poly_cases(void) {
    current_function = "poly";
    AddVectoredExceptionHandler(1, py_fault);
    es2_sim_identity_memory(&ir_memory);
    es2_sim_rtl_memory(&ir_memory, &su_rtl);
    su_data_pristine = malloc(SU_DATA_SIZE);
    su_snap[0] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    su_snap[1] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    ir_region = ww_alloc(IR_REGION_SIZE);
    lc_stack = VirtualAlloc(NULL, 0x10000u, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    lc_bits = ww_alloc(LC_BITS_SIZE);
    py_world = VirtualAlloc(NULL, PY_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    py_world_start = malloc(PY_SIZE);
    py_snap[0] = malloc(PY_SIZE);
    py_snap[1] = malloc(PY_SIZE);
    if (!su_data_pristine || !su_snap[0] || !su_snap[1] || !ir_region || !lc_stack || !lc_bits || !py_world || !py_world_start || !py_snap[0] || !py_snap[1])
        return fail("poly: memory");
    if (get32(mapped + (SU_COUNTER - 0x400000u))) return fail("poly: the counter word is not free");
    su_pristine_take();
    static const struct { const char *dll, *name; void *stub; } stubs[] = {
        {"cw3220.dll", "_calloc", (void *)su_calloc}, {"cw3220.dll", "_free", (void *)su_free},
        {"cw3220.dll", "_memset", (void *)ic_memset}, {"KERNEL32.dll", "GetTickCount", (void *)lc_tick},
    };
    for (unsigned i = 0; i < sizeof(stubs) / sizeof(stubs[0]); ++i)
        if (!ir_patch_import(stubs[i].dll, stubs[i].name, stubs[i].stub)) {
            fprintf(stderr, "import %s!%s\n", stubs[i].dll, stubs[i].name);
            return fail("poly: import slot not found");
        }
    if (py_dispatch_setup()) return 1;
    int bad = 0;
    static const struct { const char *name; int (*run)(unsigned); unsigned cases; } parts[] = {
        {"helpers", py_helper_case, 600}, {"cut", py_cut_case, 600}, {"clip", py_clip_case, 800}, {"edges", py_edges_case, 1200},
        {"span", py_span_case, 1500}, {"fill", py_fill_case, 1200}, {"fill3d", py_fill3d_case, 800},
    };
    for (unsigned p = 0; !bad && p < sizeof(parts) / sizeof(parts[0]); ++p) {
        for (unsigned k = 0; !bad && k < parts[p].cases; ++k) {
            current_case = k; current_case_valid = 1;
            py_bad = 0;
            py_desc[0] = 0;
            bad = parts[p].run(k);
        }
        if (!bad) printf("PASS: poly %s: %u cases identical\n", parts[p].name, parts[p].cases);
    }
    if (!bad) printf("POLY coverage: %u transparent spans, %u odd starts, %u depth spans with runs, %u fills with an empty run row, %u near cuts, %u flag-2 edge tables, %u empty fills\n",
                     py_cov_transparent, py_cov_odd, py_cov_depth_hits, py_cov_runs_skip, py_cov_near_cut, py_cov_flag2, py_cov_empty);
    current_case_valid = 0;
    put32(mapped + 0x2cbee4u, 0);
    memcpy(mapped + SU_DATA_RVA, su_data_pristine, SU_DATA_SIZE);
    return bad;
}
