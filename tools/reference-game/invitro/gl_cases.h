/* The graphics library's 2D polygon fill layer and its line of DBSIM, original
 * machine code against sim/src/fill.c (--gl-fill; target
 * :differential_gl_fill_test). Included after gfx_cases.h (and the later case
 * files); reuses gfx_cases.h's world (gx_world_reset, gx_context, gx_side,
 * gx_end, gx_compare, the BMP_8 driver table copied to 0x4a57ac) and the DATA
 * reset and snapshots of startup_cases.h and loop_cases.h.
 *
 * The own side calls fill.c directly on the harness loop (gl_dispatch: the
 * service rows, slot 33's BMP_8 body 0x48a924 and the rotation helpers as
 * the original; the Gouraud fill 0x487acc as the original only while it has
 * no row: gouraud_fill.h makes it own code). Slots 33 and 34 can be
 * pointed at harness recorders (host functions the original calls through
 * the table and the own side through the dispatch) that log their
 * arguments and the points (and a kind-5 fill's per-point table) into the
 * world. Compared after every case: EAX where defined, DATA (the span
 * buffers A and B among it) and the world. The span builder's saved frame
 * pointer 0x4a33d4 is a host value on both sides and is copied from the
 * original's snapshot into the own one before the comparison.
 *
 * - clip: 0x484338 over random polygons of 0..10 points (and n = -1)
 *   against random rects, points on the bounds.
 * - spans: 0x484720 over random walks of vertical, horizontal, diagonal,
 *   x-major (both sides) and y-major edges, duplicates, flat polygons.
 * - line clip / line spans: 0x483c6c, 0x483d6c.
 * - line: 0x483a04 with pens of kinds 0 (plain, ramp, undrawable), 2 and
 *   others, modes 0..2 (region kinds 0, 2, 5; counts 0 and 0x10000), the
 *   origin, slot 34 (0x48a3b0) as the body and as a recorder.
 * - polygon: 0x483eb8, 0x484222, 0x4842bb with fill kinds 0, 1 (Gouraud,
 *   gouraud.c), 4 (the outline), 5 and 7 (slot 33 recorder), 6 and
 *   others, n 0..21 and 0x10000 / 0xffff0000.
 * - orientation: 0x4842bb/0x484222 with coordinates up to 2^30 (the 64-bit
 *   area's high dword) and fills that draw nothing or record.
 * - rotate: gfx_cases.h's rotated blit with the fill layer as own code
 *   (kind 5 through the real slot 33). */
#pragma once
/* The headers this suite builds on (sim_main.c). */
#include "init_records_cases.h"
#include "startup_cases.h"
#include "loop_cases.h"
#include "input_layer_cases.h"
#include "gfx_cases.h"
#include "es2/sim/fill.h"

#define GL_LOG GX_SAVE                 /* the recorders' log: count, then dwords */
#define GL_LOG_SIZE 0x800u
#define GL_TABLE GX_SCAN               /* kind 5: the per-point table [F+8] */
#define GL_SHADES (GX_SCAN + 0x200u)   /* kind 1: the shade words [F+0xc] */

static es2_sim_entry gl_entries[4096];
static es2_sim_dispatch gl_dispatch;
static unsigned gl_cov_kind[9], gl_cov_mode[3], gl_cov_origin, gl_cov_outline[3], gl_cov_clip_empty, gl_cov_clip_small, gl_cov_clip_grow,
    gl_cov_flat, gl_cov_edge[6], gl_cov_pen[3], gl_cov_slot34_stub, gl_cov_count_odd, gl_cov_orient[2], gl_cov_lclip[2], gl_cov_lspan[5],
    gl_cov_n_odd;

static uint32_t gl_g32(uint32_t a) { return get32((const BYTE *)(uintptr_t)a); }
static void gl_log(const uint32_t *v, unsigned n) {
    BYTE *log = gx_world + GL_LOG;
    uint32_t used = get32(log);
    if (4u + 4u * (used + n) > GL_LOG_SIZE) return;
    for (unsigned i = 0; i < n; ++i) put32(log + 4u + 4u * (used + i), v[i]);
    put32(log, used + n);
}
/* Slot 33 (n, points): n, the points and, for a kind-5 fill, its table. */
static uint32_t gl_slot33(uint32_t n, uint32_t p) {
    uint32_t head[3] = {0x33u, n, gl_g32(gl_g32(0x004a3614u) + 0x208u)};
    gl_log(head, 3);
    uint32_t m = n > 22u ? 22u : n;
    for (uint32_t i = 0; i < m; ++i) { uint32_t v[2] = {gl_g32(p + 8u * i), gl_g32(p + 8u * i + 4u)}; gl_log(v, 2); }
    uint32_t f = gl_g32(gl_g32(0x004a3614u) + 0x228u);
    if (gl_g32(f) == 5u) {
        uint32_t t = gl_g32(f + 8u);
        for (uint32_t i = 0; i < m; ++i) { uint32_t v[2] = {gl_g32(t + 8u * i), gl_g32(t + 8u * i + 4u)}; gl_log(v, 2); }
    }
    return 0x33000000u | n;
}
/* Slot 34 (colour, p0, p1). */
static uint32_t gl_slot34(uint32_t colour, uint32_t p0, uint32_t p1) {
    uint32_t v[6] = {0x34u, colour, gl_g32(p0), gl_g32(p0 + 4u), gl_g32(p1), gl_g32(p1 + 4u)};
    gl_log(v, 6);
    return 0x34u;
}
static uint32_t __cdecl gl_o33(uint32_t a, uint32_t b) { return gl_slot33(a, b); }
static uint32_t __cdecl gl_o34(uint32_t a, uint32_t b, uint32_t c) { return gl_slot34(a, b, c); }
static void gl_s33(es2_sim_service_call *sc, void *user) { (void)user; sc->eax = gl_slot33(sc->slots[0], sc->slots[1]); }
static void gl_s34(es2_sim_service_call *sc, void *user) { (void)user; sc->eax = gl_slot34(sc->slots[0], sc->slots[1], sc->slots[2]); }

static int gl_dispatch_setup(void) {
    es2_sim_dispatch_init(&gl_dispatch, gl_entries, 4096, gx_refuse, NULL);
    static es2_sim_services s;
    s.memory = &ir_memory;
    s.heap = &ir_heap;
    s.vt = &lc_vt;
    s.fs0 = NULL;
    if (es2_sim_register_services(&gl_dispatch, &s)) return fail("gl fill: dispatch");
    static const struct { uint32_t va; unsigned slots; const char *name; } passed[] = {
        {0x00487accu, 2, "gouraud (original)"}, {0x0048a924u, 2, "slot 33 (original)"},
        {0x0047eb30u, 2, "sincos_pair (original)"}, {0x0047ffe8u, 3, "rotate_2d (original)"},
    };
    for (unsigned i = 0; i < sizeof(passed) / sizeof(passed[0]); ++i) {
        /* a VA with a row (0x487acc) runs as own code. */
        int row = 0;
        for (unsigned k = 0; k < es2_sim_service_count(); ++k) {
            uint32_t va = 0;
            es2_sim_service_row(k, &va);
            if (va == passed[i].va) row = 1;
        }
        if (!row && es2_sim_dispatch_add_service(&gl_dispatch, passed[i].va, passed[i].name, gx_original, (void *)(uintptr_t)passed[i].slots, 0,
                                                 (uint8_t)passed[i].slots))
            return fail("gl fill: dispatch");
    }
    if (es2_sim_dispatch_add_service(&gl_dispatch, (uint32_t)(uintptr_t)gl_o33, "slot 33 recorder", gl_s33, NULL, 0, 2) ||
        es2_sim_dispatch_add_service(&gl_dispatch, (uint32_t)(uintptr_t)gl_o34, "slot 34 recorder", gl_s34, NULL, 0, 3))
        return fail("gl fill: dispatch");
    const char *error = NULL;
    uint32_t bad = 0;
    if (es2_sim_dispatch_seal(&gl_dispatch, &error, &bad)) return fail("gl fill: dispatch seal");
    return 0;
}

static void gl_side(unsigned side, int stub33, int stub34) {
    gx_side(side);
    BYTE *d = mapped - 0x400000u;
    if (stub33) put32(d + 0x4a5830u, (uint32_t)(uintptr_t)gl_o33);
    if (stub34) put32(d + 0x4a5834u, (uint32_t)(uintptr_t)gl_o34);
}
static int gl_compare(const char *what, unsigned k, uint32_t eax0, uint32_t eax1, int eax_defined) {
    su_snap_same(ES2_SIM_VA_FILL_SAVED_FRAME, 4u);
    return gx_compare(what, k, eax0, eax1, eax_defined);
}
static void gl_points_random(uint32_t off, uint32_t n, int32_t x0, int32_t x1, int32_t y0, int32_t y1) {
    for (uint32_t i = 0; i < n; ++i) gx_point(off + 8u * i, gx_ri(x0, x1), gx_ri(y0, y1));
}

/* ---- the polygon clipper 0x484338 */
static int gl_clip_case(unsigned k) {
    gx_world_reset();
    uint32_t n = gx_r(10u) ? 2u + gx_r(9u) : gx_r(3u), eax[2] = {0, 0};
    if (gx_r(50u) == 0) n = 0xffffffffu;
    int32_t l = gx_ri(-10, 50), t = gx_ri(-10, 40), r = gx_ri(l, 75), b = gx_ri(t, 60);
    gx_rect(GX_RECT, l, t, r, b);
    int inside = gx_r(6u) == 0;
    for (uint32_t i = 0; i < 12u; ++i) {
        int32_t x = inside ? gx_ri(l, r) : gx_ri(l - 30, r + 30), y = inside ? gx_ri(t, b) : gx_ri(t - 30, b + 30);
        if (gx_r(6u) == 0) x = gx_r(2u) ? l : r;
        if (gx_r(6u) == 0) y = gx_r(2u) ? t : b;
        gx_point(GX_POINTS + 8u * i, x, y);
    }
    gx_fill_random(GX_OUT, 0x400u);
    snprintf(gx_desc, sizeof(gx_desc), "clip n %ld rect (%ld,%ld)-(%ld,%ld)", (long)(int32_t)n, (long)l, (long)t, (long)r, (long)b);
    for (unsigned side = 0; side < 2u; ++side) {
        gl_side(side, 0, 0);
        uint32_t args[4] = {n, gx_va(GX_POINTS), gx_va(GX_OUT), gx_va(GX_RECT)};
        if (!side) eax[0] = lc_machine(0x00484338u, args, 4, NULL);
        else if (es2_sim_fill_clip(&lc_loop, args[0], args[1], args[2], args[3], &eax[1])) gx_bad = 1;
        gx_end(side);
    }
    if ((int32_t)n > 1) {
        if (eax[0] == 0) ++gl_cov_clip_empty;
        else if (eax[0] < n) ++gl_cov_clip_small;
        else if (eax[0] > n) ++gl_cov_clip_grow;
    }
    return gl_compare("clip", k, eax[0], eax[1], 1);
}

/* ---- the span builder 0x484720: a random walk of typed edges */
static void gl_walk(uint32_t off, uint32_t n, int32_t lo_x, int32_t hi_x, int32_t lo_y, int32_t hi_y) {
    int32_t x = gx_ri(lo_x, hi_x), y = gx_ri(lo_y, hi_y);
    for (uint32_t i = 0; i < n; ++i) {
        gx_point(off + 8u * i, x, y);
        int32_t dx = 0, dy = 0, s = gx_r(2u) ? 1 : -1, s2 = gx_r(2u) ? 1 : -1;
        switch (gx_r(6u)) {
        case 0: dy = s * gx_ri(1, 15); break;                          /* vertical */
        case 1: dx = s * gx_ri(1, 20); break;                          /* horizontal */
        case 2: dx = s * gx_ri(1, 12); dy = s2 * (dx < 0 ? -dx : dx); break;   /* diagonal */
        case 3: dx = gx_ri(-20, 20); dy = gx_ri(-15, 15); break;
        case 4: break;                                                 /* a duplicate */
        default: dx = s * gx_ri(5, 30); dy = s2 * gx_ri(1, 4); break; /* x-major */
        }
        x += dx; y += dy;
        if (x < lo_x) x = lo_x; else if (x > hi_x) x = hi_x;
        if (y < lo_y) y = lo_y; else if (y > hi_y) y = hi_y;
    }
}
static void gl_edge_coverage(uint32_t off, uint32_t n) {
    for (uint32_t i = 0; i < n; ++i) {
        int32_t x0 = (int32_t)get32(gx_p(off + 8u * i)), y0 = (int32_t)get32(gx_p(off + 8u * i + 4u));
        uint32_t j = i + 1u == n ? 0 : i + 1u;
        int32_t dx = (int32_t)get32(gx_p(off + 8u * j)) - x0, dy = (int32_t)get32(gx_p(off + 8u * j + 4u)) - y0;
        dx = dx < 0 ? -dx : dx; dy = dy < 0 ? -dy : dy;
        ++gl_cov_edge[dx == 0 ? 0 : dy == 0 ? 1 : dx == dy ? 2 : dx < dy ? 3 : 4];
    }
}
static int gl_spans_case(unsigned k) {
    gx_world_reset();
    uint32_t n = 2u + gx_r(12u), eax[2] = {0, 0};
    gl_walk(GX_POINTS, n, -20, 90, -20, 70);
    if (gx_r(15u) == 0) {   /* a flat polygon: ymin == ymax */
        int32_t y = (int32_t)get32(gx_p(GX_POINTS + 4u));
        for (uint32_t i = 0; i < n; ++i) gx_put32(GX_POINTS + 8u * i + 4u, (uint32_t)y);
        ++gl_cov_flat;
    }
    gl_edge_coverage(GX_POINTS, n);
    gx_fill_random(GX_OUT, 0x400u);
    snprintf(gx_desc, sizeof(gx_desc), "spans n %lu", (unsigned long)n);
    for (unsigned side = 0; side < 2u; ++side) {
        gl_side(side, 0, 0);
        uint32_t args[3] = {n, gx_va(GX_POINTS), gx_va(GX_OUT)};
        if (!side) eax[0] = lc_machine(0x00484720u, args, 3, NULL);
        else if (es2_sim_fill_spans(&lc_loop, args[0], args[1], args[2], &eax[1])) gx_bad = 1;
        gx_end(side);
    }
    return gl_compare("spans", k, eax[0], eax[1], 1);
}

/* ---- the line's clip 0x483c6c and span list 0x483d6c */
static void gl_line_points(int32_t x0, int32_t x1, int32_t y0, int32_t y1) {
    int32_t ax = gx_ri(x0, x1), ay = gx_ri(y0, y1), bx = gx_ri(x0, x1), by = gx_ri(y0, y1);
    switch (gx_r(8u)) {
    case 0: bx = ax; break;
    case 1: by = ay; break;
    case 2: bx = ax; by = ay; break;
    case 3: { int32_t d = gx_ri(-30, 30); bx = ax + d; by = ay + (gx_r(2u) ? d : -d); break; }
    default: break;
    }
    gx_point(GX_RECT + 0x10u, ax, ay);
    gx_point(GX_RECT + 0x18u, bx, by);
}
static int gl_line_clip_case(unsigned k) {
    gx_world_reset();
    int32_t l = gx_ri(-5, 50), t = gx_ri(-5, 40), r = gx_ri(l, 70), b = gx_ri(t, 55);
    gx_rect(GX_RECT, l, t, r, b);
    gl_line_points(l - 40, r + 40, t - 40, b + 40);
    for (unsigned e = 0; e < 2u; ++e) {
        if (gx_r(5u) == 0) gx_put32(GX_RECT + 0x10u + 8u * e, (uint32_t)(gx_r(2u) ? l : r));
        if (gx_r(5u) == 0) gx_put32(GX_RECT + 0x14u + 8u * e, (uint32_t)(gx_r(2u) ? t : b));
    }
    uint32_t eax[2] = {0, 0};
    snprintf(gx_desc, sizeof(gx_desc), "line clip (%ld,%ld)-(%ld,%ld) rect (%ld,%ld)-(%ld,%ld)", (long)(int32_t)get32(gx_p(GX_RECT + 0x10u)),
             (long)(int32_t)get32(gx_p(GX_RECT + 0x14u)), (long)(int32_t)get32(gx_p(GX_RECT + 0x18u)), (long)(int32_t)get32(gx_p(GX_RECT + 0x1cu)),
             (long)l, (long)t, (long)r, (long)b);
    for (unsigned side = 0; side < 2u; ++side) {
        gl_side(side, 0, 0);
        uint32_t args[3] = {gx_va(GX_RECT + 0x10u), gx_va(GX_RECT + 0x18u), gx_va(GX_RECT)};
        if (!side) eax[0] = lc_machine(0x00483c6cu, args, 3, NULL);
        else if (es2_sim_fill_line_clip(&lc_loop, args[0], args[1], args[2], &eax[1])) gx_bad = 1;
        gx_end(side);
    }
    ++gl_cov_lclip[eax[0] != 0];
    return gl_compare("line clip", k, eax[0], eax[1], 1);
}
static int gl_line_spans_case(unsigned k) {
    gx_world_reset();
    gl_line_points(-50, 120, -50, 100);
    gx_fill_random(GX_OUT, 0x600u);
    int32_t ax = (int32_t)get32(gx_p(GX_RECT + 0x10u)), ay = (int32_t)get32(gx_p(GX_RECT + 0x14u));
    int32_t dx = (int32_t)get32(gx_p(GX_RECT + 0x18u)) - ax, dy = (int32_t)get32(gx_p(GX_RECT + 0x1cu)) - ay;
    dx = dx < 0 ? -dx : dx; dy = dy < 0 ? -dy : dy;
    ++gl_cov_lspan[dx == 0 ? 0 : dy == 0 ? 1 : dx == dy ? 2 : dx > dy ? 3 : 4];
    snprintf(gx_desc, sizeof(gx_desc), "line spans (%ld,%ld) d (%ld,%ld)", (long)ax, (long)ay, (long)dx, (long)dy);
    for (unsigned side = 0; side < 2u; ++side) {
        gl_side(side, 0, 0);
        uint32_t args[3] = {gx_va(GX_RECT + 0x10u), gx_va(GX_RECT + 0x18u), gx_va(GX_OUT)};
        if (!side) lc_machine(0x00483d6cu, args, 3, NULL);
        else if (es2_sim_fill_line_spans(&lc_loop, args[0], args[1], args[2])) gx_bad = 1;
        gx_end(side);
    }
    return gl_compare("line spans", k, 0, 0, 0);
}

/* Slot 34 0x48a3b0 is not called directly: the BMP_8 slot bodies use
 * EBX, ESI and EDI without saving them (their callers save them), which the
 * machine-call thunk does not allow. The line's modes 0 and 1 with a kind-0
 * pen reach it with every direction. */

/* The region count: rarely 0, or for the line 0x10000 (its jcxz tests the
 * low word; the fill tests the dword and would walk 0x10000 entries). */
static void gl_odd_count(int line) {
    if (gx_mode != 2 || gx_r(12u)) return;
    gx_put32(GX_CONTEXT, line && gx_r(2u) ? 0x10000u : 0);
    ++gl_cov_count_odd;
}

/* ---- the line 0x483a04 */
static int gl_line_case(unsigned k) {
    gx_world_reset();
    gx_context(1);
    gl_odd_count(1);
    uint32_t r = gx_r(10u), pen = r < 5u ? 0 : r < 8u ? 2u : (gx_r(2u) ? 1u : 3u);
    gx_put32(GX_PEN, pen);
    if (pen == 0) gx_put32(GX_PEN + 4u, gx_colour());
    ++gl_cov_pen[pen == 0 ? 0 : pen == 2u ? 1 : 2];
    ++gl_cov_mode[gx_mode];
    if (get32(gx_p(GX_CONTEXT + 0x208u))) ++gl_cov_origin;
    if (gx_mode == 0) gl_line_points(3, GX_W - 4, 3, GX_H - 4);
    else gl_line_points(-30, GX_W + 30, -30, GX_H + 30);
    if (gx_mode == 0)
        for (unsigned e = 0; e < 2u; ++e) {
            int32_t x = (int32_t)get32(gx_p(GX_RECT + 0x10u + 8u * e)), y = (int32_t)get32(gx_p(GX_RECT + 0x14u + 8u * e));
            gx_point(GX_RECT + 0x10u + 8u * e, x < 3 ? 3 : x > GX_W - 4 ? GX_W - 4 : x, y < 3 ? 3 : y > GX_H - 4 ? GX_H - 4 : y);
        }
    int stub34 = gx_r(5u) == 0;
    gl_cov_slot34_stub += (unsigned)stub34;
    snprintf(gx_desc, sizeof(gx_desc), "line pen %lu mode %d regions %lu origin %lu stub34 %d", (unsigned long)pen, gx_mode,
             (unsigned long)get32(gx_p(GX_CONTEXT)), (unsigned long)get32(gx_p(GX_CONTEXT + 0x208u)), stub34);
    for (unsigned side = 0; side < 2u; ++side) {
        gl_side(side, 0, stub34);
        uint32_t args[2] = {gx_va(GX_RECT + 0x10u), gx_va(GX_RECT + 0x18u)};
        if (!side) lc_machine(0x00483a04u, args, 2, NULL);
        else if (es2_sim_fill_line(&lc_loop, args[0], args[1])) gx_bad = 1;
        gx_end(side);
    }
    return gl_compare("line", k, 0, 0, 0);
}

/* ---- the polygon fills 0x483eb8 (which 0), 0x484222 (1), 0x4842bb (2) */
static const uint32_t gl_poly_va[3] = {0x00483eb8u, 0x00484222u, 0x004842bbu};
static int gl_poly_run(unsigned which, uint32_t n, uint32_t points, uint32_t *eax) {
    switch (which) {
    case 0: return es2_sim_fill_polygon(&lc_loop, n, points, eax);
    case 1: return es2_sim_fill_polygon_reversed(&lc_loop, n, points, eax);
    default: return es2_sim_fill_polygon_any(&lc_loop, n, points, eax);
    }
}
static int gl_poly_case(unsigned k) {
    gx_world_reset();
    gx_context(1);
    gl_odd_count(0);
    unsigned which = gx_r(3u);
    uint32_t r = gx_r(20u), kind = r < 8u ? 0 : r < 10u ? 6u : r < 12u ? 4u : r < 14u ? 5u : r < 15u ? 7u : r < 17u ? 1u : (r == 17u ? 2u : r == 18u ? 3u : 8u);
    gx_put32(GX_FILL, kind);
    if (kind == 0 || kind == 4u) gx_put32(GX_FILL + 4u, gx_colour());
    /* The outline of n < 3 draws with the pen {0, [F+4]} for every kind; a
     * random [F+4] with high byte 4 is a ramp colour, and 0x483a04 reads the
     * ramp pointer at 0x4a4fa5 + 8 * (its low word), which the original
     * faults on (0x483a7f) outside the four ramps gx_colour uses. */
    else if ((get32(gx_p(GX_FILL + 4u)) >> 24) == 4u) gx_put16(GX_FILL + 4u, gx_r(4u));
    if (kind == 5u) {
        gx_put32(GX_FILL + 8u, gx_va(GL_TABLE));
        gx_fill_random(GL_TABLE, 0x100u);
    }
    if (kind == 1u) {   /* the Gouraud fill: a ramp index, shade words below 256; mode 0 on the surface */
        gx_put32(GX_FILL + 4u, gx_r(4u) | (gx_r(0x10000u) << 16));
        gx_put32(GX_FILL + 0xcu, gx_va(GL_SHADES));
        for (uint32_t i = 0; i < 32u; ++i) gx_put16(GL_SHADES + 2u * i, gx_r(256u));
        /* Ramp entries are palette indices (0x486ee8 sizes its colour tables by their differences). */
        for (uint32_t i = 0; i < 0x200u; ++i) gx_put16(GX_RAMP + 2u * i, gx_r(256u));
        gx_put32(GX_CONTEXT + 0x204u, 0);
        gx_mode = 0;
    }
    ++gl_cov_kind[kind];
    ++gl_cov_mode[gx_mode];
    int origin = get32(gx_p(GX_CONTEXT + 0x208u)) != 0;
    gl_cov_origin += (unsigned)origin;
    uint32_t n;
    if (kind == 1u) n = 3u + gx_r(4u);
    else if (gx_mode == 0) n = gx_r(4u) ? 3u + gx_r(6u) : gx_r(22u);
    else n = gx_r(4u) ? 3u + gx_r(6u) : gx_r(11u);
    if (which != 0 && n > 20u) n = 20u;   /* 0x4842bb may take 0x484222, whose copy holds 20 */
    if (origin && n == 0 && which == 0) n = 1u;
    /* The outline's pen [ebp-0x1c] is the origin copy's point 20: with 21
     * points the last line ends at {0, colour}, which mode 0 (no clip)
     * draws outside the surface. */
    if (origin && n > 20u && kind == 4u && gx_mode == 0) n = 20u;
    if (!origin && gx_r(40u) == 0) {
        n = (kind == 4u && which == 0) ? 0x10000u : 0xffff0000u;
        ++gl_cov_n_odd;
    }
    uint32_t m = n > 22u ? 0 : n;
    if (gx_mode == 0) gl_points_random(GX_POINTS, 22u, 3, GX_W - 4, 3, GX_H - 4);
    else if (gx_r(2u)) gl_points_random(GX_POINTS, 22u, -30, GX_W + 30, -30, GX_H + 30);
    else gl_walk(GX_POINTS, 22u, -30, GX_W + 30, -30, GX_H + 30);
    if (n < 3u) ++gl_cov_outline[n == 1u ? 0 : n == 2u ? 1 : 2];
    (void)m;
    int stub33 = kind == 5u || kind == 7u, stub34 = gx_r(5u) == 0;
    snprintf(gx_desc, sizeof(gx_desc), "polygon %lx n %lu kind %lu mode %d regions %lu origin %lu stub34 %d", (unsigned long)gl_poly_va[which],
             (unsigned long)n, (unsigned long)kind, gx_mode, (unsigned long)get32(gx_p(GX_CONTEXT)), (unsigned long)get32(gx_p(GX_CONTEXT + 0x208u)),
             stub34);
    uint32_t eax[2] = {0, 0};
    for (unsigned side = 0; side < 2u; ++side) {
        gl_side(side, stub33, stub34);
        uint32_t args[2] = {n, gx_va(GX_POINTS)};
        if (!side) eax[0] = lc_machine(gl_poly_va[which], args, 2, NULL);
        else if (gl_poly_run(which, n, args[1], &eax[1])) gx_bad = 1;
        gx_end(side);
    }
    return gl_compare("polygon", k, eax[0], eax[1], which == 0 || n != 0);
}

/* ---- the orientation test with large coordinates (fills that draw nothing or record) */
static int gl_orient_case(unsigned k) {
    gx_world_reset();
    gx_context(1);
    unsigned which = gx_r(4u) ? 2u : 1u;
    uint32_t kind = gx_r(2u) ? 3u : 5u, n = 3u + gx_r(18u);
    gx_put32(GX_FILL, kind);
    gx_put32(GX_FILL + 8u, gx_va(GL_TABLE));
    gx_fill_random(GL_TABLE, 0x100u);
    int32_t span = (int32_t)(1u << gx_ri(2, 29));
    for (uint32_t i = 0; i < n; ++i) gx_point(GX_POINTS + 8u * i, gx_ri(-span, span - 1), gx_ri(-span, span - 1));
    snprintf(gx_desc, sizeof(gx_desc), "orient %lx n %lu kind %lu span %ld", (unsigned long)gl_poly_va[which], (unsigned long)n, (unsigned long)kind, (long)span);
    uint32_t eax[2] = {0, 0};
    for (unsigned side = 0; side < 2u; ++side) {
        gl_side(side, 1, 0);
        uint32_t args[2] = {n, gx_va(GX_POINTS)};
        if (!side) eax[0] = lc_machine(gl_poly_va[which], args, 2, NULL);
        else if (gl_poly_run(which, n, args[1], &eax[1])) gx_bad = 1;
        gx_end(side);
    }
    /* Which way it went: the recorder's first point is the input's first (0x483eb8) or last (0x484222). */
    const BYTE *log = gx_snap[0] + GL_LOG;
    if (kind == 5u && get32(log)) ++gl_cov_orient[get32(log + 16u) != get32(gx_p(GX_POINTS))];
    return gl_compare("orient", k, eax[0], eax[1], 1);
}

/* The Gouraud fill's triangle 0x486ee8 has a frame of 0x1980 bytes that
 * it probes one page deep only (0x486eeb..0x486ef2); called through the
 * own side's dispatch it runs on the harness thread's stack, whose pages
 * below the guard page are committed here first (the probe of a large
 * local array touches them in order). */
static __attribute__((noinline)) void gl_commit_stack(void) {
    volatile BYTE b[0x20000];
    for (uint32_t i = sizeof(b); i > 0; i -= 0x1000u) b[i - 1u] = 0;
}
static int gl_fill_cases(void) {
    current_function = "gl fill";
    gl_commit_stack();
    AddVectoredExceptionHandler(1, gx_fault);
    es2_sim_identity_memory(&ir_memory);
    es2_sim_rtl_memory(&ir_memory, &su_rtl);
    su_data_pristine = malloc(SU_DATA_SIZE);
    su_snap[0] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    su_snap[1] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    ir_region = ww_alloc(IR_REGION_SIZE);
    lc_stack = VirtualAlloc(NULL, 0x10000u, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    lc_bits = ww_alloc(LC_BITS_SIZE);
    gx_world = VirtualAlloc(NULL, GX_WORLD_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    gx_world_start = malloc(GX_WORLD_SIZE);
    gx_snap[0] = malloc(GX_WORLD_SIZE);
    gx_snap[1] = malloc(GX_WORLD_SIZE);
    if (!su_data_pristine || !su_snap[0] || !su_snap[1] || !ir_region || !lc_stack || !lc_bits || !gx_world || !gx_world_start || !gx_snap[0] || !gx_snap[1])
        return fail("gl fill: memory");
    if (get32(mapped + (SU_COUNTER - 0x400000u))) return fail("gl fill: the counter word is not free");
    if (get32(mapped + (0x4a586cu - 0x400000u) + 4u * 34u) != 0x0048a3b0u || get32(mapped + (0x4a586cu - 0x400000u) + 4u * 33u) != 0x0048a924u)
        return fail("gl fill: the BMP_8 driver table is not at 0x4a586c");
    su_pristine_take();
    static const struct { const char *dll, *name; void *stub; } stubs[] = {
        {"cw3220.dll", "_calloc", (void *)su_calloc}, {"cw3220.dll", "_free", (void *)su_free},
        {"cw3220.dll", "_memset", (void *)ic_memset}, {"cw3220.dll", "_sprintf", (void *)gx_sprintf},
        {"KERNEL32.dll", "GetTickCount", (void *)lc_tick},
    };
    for (unsigned i = 0; i < sizeof(stubs) / sizeof(stubs[0]); ++i)
        if (!ir_patch_import(stubs[i].dll, stubs[i].name, stubs[i].stub)) {
            fprintf(stderr, "import %s!%s\n", stubs[i].dll, stubs[i].name);
            return fail("gl fill: import slot not found");
        }
    if (gl_dispatch_setup()) return 1;
    gx_active_dispatch = &gl_dispatch;
    int bad = 0;
    static const struct { const char *name; int (*run)(unsigned); unsigned cases; } parts[] = {
        {"clip", gl_clip_case, 3000}, {"spans", gl_spans_case, 3000}, {"line clip", gl_line_clip_case, 3000},
        {"line spans", gl_line_spans_case, 3000}, {"line", gl_line_case, 5000},
        {"polygon", gl_poly_case, 6000}, {"orient", gl_orient_case, 2000}, {"rotate", gx_rotate_case, 1000},
    };
    /* Each part's cases run in chunks of GL_FILL_CHUNK; chunk c of the whole
     * run belongs to test shard c % count (suite_part_in_shard) and starts from
     * its own seed, so a chunk's cases do not depend on the shard layout. The
     * coverage lines count the shard's own chunks. */
    enum { GL_FILL_CHUNK = 500 };
    unsigned chunk = 0;
    for (unsigned p = 0; !bad && p < sizeof(parts) / sizeof(parts[0]); ++p) {
        unsigned owned = 0;
        for (unsigned first = 0; !bad && first < parts[p].cases; first += GL_FILL_CHUNK, ++chunk) {
            if (!suite_part_in_shard(chunk)) continue;
            unsigned end = parts[p].cases - first < GL_FILL_CHUNK ? parts[p].cases : first + GL_FILL_CHUNK;
            random_state = 0x48c06e21u ^ (chunk * 0x9e3779b9u);
            for (unsigned k = first; !bad && k < end; ++k) {
                current_case = k; current_case_valid = 1;
                gx_bad = 0;
                gx_desc[0] = 0;
                bad = parts[p].run(k);
            }
            owned += end - first;
        }
        if (!bad && owned) printf("PASS: gl fill %s: %u of %u cases identical\n", parts[p].name, owned, parts[p].cases);
    }
    if (!bad) {
        printf("GL FILL coverage: fill kinds 0:%u 1:%u 2:%u 3:%u 4:%u 5:%u 6:%u 7:%u 8:%u; modes %u/%u/%u; %u with origin; outlines n=1 %u n=2 %u n=0 %u;"
               " %u odd n; %u odd region counts\n",
               gl_cov_kind[0], gl_cov_kind[1], gl_cov_kind[2], gl_cov_kind[3], gl_cov_kind[4], gl_cov_kind[5], gl_cov_kind[6], gl_cov_kind[7],
               gl_cov_kind[8], gl_cov_mode[0], gl_cov_mode[1], gl_cov_mode[2], gl_cov_origin, gl_cov_outline[0], gl_cov_outline[1],
               gl_cov_outline[2], gl_cov_n_odd, gl_cov_count_odd);
        printf("GL FILL coverage: clip outputs empty %u smaller %u larger %u; %u flat span polygons; edges vertical %u horizontal %u diagonal %u"
               " y-major %u x-major %u; pens 0:%u 2:%u other:%u; %u slot-34 recorders; line clips 0:%u 1:%u; line spans vertical %u"
               " horizontal %u diagonal %u x-major %u y-major %u; orientation forward %u reversed %u\n",
               gl_cov_clip_empty, gl_cov_clip_small, gl_cov_clip_grow, gl_cov_flat, gl_cov_edge[0], gl_cov_edge[1], gl_cov_edge[2], gl_cov_edge[3],
               gl_cov_edge[4], gl_cov_pen[0], gl_cov_pen[1], gl_cov_pen[2], gl_cov_slot34_stub, gl_cov_lclip[0], gl_cov_lclip[1], gl_cov_lspan[0],
               gl_cov_lspan[1], gl_cov_lspan[2], gl_cov_lspan[3], gl_cov_lspan[4], gl_cov_orient[0], gl_cov_orient[1]);
    }
    current_case_valid = 0;
    put32(mapped + 0x2cbee4u, 0);
    memcpy(mapped + SU_DATA_RVA, su_data_pristine, SU_DATA_SIZE);
    return bad;
}
