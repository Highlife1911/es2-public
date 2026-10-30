/* The BMP_8 driver's cursor point 0x48993a and slot 33, the textured polygon
 * 0x48a924 (with 0x48aa50, 0x48abe4 and the hook tex_edge 0x48aaac), original
 * machine code against sim/src/gfx.c (target
 * :differential_gfx_polygon_test). Included after gfx_cases.h; reuses its world
 * (gx_world_reset, gx_context, gx_side, gx_end, gx_compare, the BMP_8 driver
 * table copied to 0x4a57ac) and the DATA reset and snapshots of startup_cases.h
 * and loop_cases.h.
 *
 * The own side runs on the harness loop with its own dispatch (the
 * service rows and a recorder for slot 31). Each case calls the own code
 * one of three ways: the function, the driver slot (es2_sim_gfx_slot) or
 * the row through the dispatch. Compared after every case: EAX (and EDX
 * for the cursor point), DATA and the world. The span kernel parks the
 * machine's ESP and EBP in 0x4a591c and 0x4a5920 (0x48af6d, 0x48af79):
 * both are zeroed on both sides before the snapshot.
 *
 * - cursor: [0x4a3646] and [0x4a361c] zero or not, slot 31 the BMP_8 body
 *   0x4898df or a recorder that logs its call and changes [0x4a361c].
 * - polygon: 0..8 corners (a convex octagon's corners in angle order, a
 *   flat row, any triangle; either winding) in and around the 64x48
 *   surface, kinds 5 and 7 (a shade table or none) and others, textures
 *   of 1..24 x 1..24 with corners on and past them, clip modes 0, 1, 2
 *   with kind-0, kind-2 (ordered pairs) and kind-5 regions, a mode-2
 *   region count of 0. Texture coordinates are never negative and corner
 *   lists are convex: the span kernel takes the integer v as an unsigned
 *   word, so a v below 0 addresses about 0xffff texture rows on, and the
 *   original faults at 0x48af1b (or, in the transparent kernel with ESP
 *   as an accumulator, dies); a star-shaped list with non-negative
 *  corners does the same (rasterizer.md, "Cursor point and textured slot 33").
 *
 * The own side's ECX for the cursor row is checked to be the point
 * (lc_machine does not return the original's). */
#pragma once
/* The headers this suite builds on (sim_main.c). */
#include "init_records_cases.h"
#include "startup_cases.h"
#include "loop_cases.h"
#include "input_layer_cases.h"
#include "gfx_cases.h"

#define TP_LOG GX_TEXT                /* the slot 31 recorder's log: count, then dwords */
#define TP_TEXTURE (GX_GLYPHS + 0x400u)
#define TP_SHADE GX_SAVE              /* 0x800 bytes of shade table */
#define TP_CORNERS GX_SCAN            /* the texture corners {u, v} */

static es2_sim_entry tp_entries[4096];
static es2_sim_dispatch tp_dispatch;
enum {
    TPC_CURSOR_ON, TPC_CURSOR_OFF, TPC_SHOWN, TPC_RECORDER, TPC_FEW, TPC_FLAT, TPC_KIND7_SHADE, TPC_TRANSPARENT,
    TPC_MODE0, TPC_MODE1, TPC_MODE2_RECT, TPC_MODE2_SPANS, TPC_MODE2_OTHER, TPC_MODE2_EMPTY, TPC_LEFT_CLIP,
    TPC_TRIANGLE, TPC_EDGE_END, TPC_BOTTOM, TPC_ABOVE_TOP, TPC_DU_NEG, TPC_DV_NEG, TPC_RISING_EDGE, TPC_VIA_SLOT,
    TPC_VIA_ROW, TPC_COUNT
};
static const char *const tp_cv_names[TPC_COUNT] = {
    "cursor on", "cursor off", "cursor shown", "slot 31 recorder", "n < 3", "flat", "kind 7 shaded", "transparent kernel",
    "mode 0", "mode 1", "mode 2 rect", "mode 2 spans", "mode 2 other kind", "mode 2 no region", "left clip",
    "any triangle", "edge list ends", "clip bottom", "rows above the clip top", "du < 0", "dv < 0", "rising edge",
    "through the slot", "through the row",
};
static unsigned tp_cv[TPC_COUNT];

/* Slot 31 recorder: logs the call and the shown flag, then sets the flag
 * from the world (the original restores it after the slot). */
static void tp_hide_body(void) {
    BYTE *log = gx_world + TP_LOG;
    uint32_t used = get32(log);
    if (used < 30u) { put32(log + 4u + 4u * used, get32(mapped - 0x400000u + 0x4a361cu)); put32(log, used + 1u); }
    put32(mapped - 0x400000u + 0x4a361cu, get32(gx_world + TP_LOG + 0x80u));
}
static uint32_t __cdecl tp_hide(void) { tp_hide_body(); return 0x31u; }
static void tp_s_hide(es2_sim_service_call *sc, void *user) { (void)user; tp_hide_body(); sc->eax = 0x31u; }

static int tp_dispatch_setup(void) {
    es2_sim_dispatch_init(&tp_dispatch, tp_entries, 4096, gx_refuse, NULL);
    static es2_sim_services s;
    s.memory = &ir_memory;
    s.heap = &ir_heap;
    s.vt = &lc_vt;
    s.fs0 = NULL;
    if (es2_sim_register_services(&tp_dispatch, &s)) return fail("gfx polygon: dispatch");
    if (es2_sim_dispatch_add_service(&tp_dispatch, (uint32_t)(uintptr_t)tp_hide, "slot 31 recorder", tp_s_hide, NULL, 0, 0))
        return fail("gfx polygon: dispatch");
    const char *error = NULL;
    uint32_t bad = 0;
    if (es2_sim_dispatch_seal(&tp_dispatch, &error, &bad)) return fail("gfx polygon: dispatch seal");
    return 0;
}
static void tp_mask(void) {
    put32(mapped - 0x400000u + 0x4a591cu, 0);
    put32(mapped - 0x400000u + 0x4a5920u, 0);
}

/* ---- cursor */
static int tp_cursor_case(unsigned k) {
    gx_world_reset();
    uint32_t on = gx_r(3u) ? (gx_r(4u) ? 1u : random32()) : 0, shown = gx_r(3u) ? (gx_r(4u) ? 1u : random32()) : 0;
    int recorder = (int)gx_r(2u);
    gx_point(GX_RECT, (int32_t)random32(), (int32_t)random32());
    gx_put32(TP_LOG + 0x80u, gx_r(2u) ? 0 : random32());
    tp_cv[on ? TPC_CURSOR_ON : TPC_CURSOR_OFF]++;
    if (on && shown) { tp_cv[TPC_SHOWN]++; if (recorder) tp_cv[TPC_RECORDER]++; }
    snprintf(gx_desc, sizeof(gx_desc), "cursor on %lx shown %lx recorder %d", (unsigned long)on, (unsigned long)shown, recorder);
    uint32_t eax[2] = {0, 0}, edx[2] = {0, 0};
    unsigned how = gx_r(2u);
    for (unsigned side = 0; side < 2u; ++side) {
        gx_side(side);
        BYTE *d = mapped - 0x400000u;
        put32(d + 0x4a3646u, on);
        put32(d + 0x4a361cu, shown);
        if (recorder) put32(d + 0x4a5828u, (uint32_t)(uintptr_t)tp_hide);
        uint32_t args[1] = {gx_va(GX_RECT)};
        if (!side) {
            eax[0] = lc_machine(0x0048993au, args, 1, &edx[0]);
        } else if (how) {
            es2_sim_result r;
            if (es2_sim_loop_call(&lc_loop, 0x0048993au, args, 1, &r)) gx_bad = 1;
            eax[1] = r.eax; edx[1] = r.edx;
            if (r.ecx != args[0]) { fprintf(stderr, "FAIL: gfx polygon: cursor ECX %08lx\n", (unsigned long)r.ecx); gx_bad = 1; }
        } else {
            uint32_t regs[3] = {0, 0, 0};
            if (es2_sim_gfx_cursor_point(&lc_loop, args[0], regs)) gx_bad = 1;
            eax[1] = regs[0]; edx[1] = regs[1];
        }
        gx_end(side);
    }
    if (edx[0] != edx[1]) {
        fprintf(stderr, "  case %u: %s\nFAIL: gfx polygon cursor case %u: EDX %08lx original, %08lx own\n", k, gx_desc, k,
                (unsigned long)edx[0], (unsigned long)edx[1]);
        return 1;
    }
    return gx_compare("cursor", k, eax[0], eax[1], 1);
}

/* ---- polygon */
static int32_t tp_ymin_index(uint32_t n) {
    int32_t best = 0x7fff, at = -1;
    for (uint32_t i = 0; i < n; ++i) {
        int32_t y = (int32_t)get32(gx_p(GX_POINTS + 8u * i + 4u));
        if (best > y) { best = y; at = (int32_t)i; }
    }
    return at;
}
static int tp_polygon_case(unsigned k) {
    gx_world_reset();
    gx_context(1);
    gx_regions_ordered();
    uint32_t c = GX_CONTEXT, mode = get32(gx_p(c + 0x204u));
    if (mode == 2u && !gx_r(12u)) gx_put32(c, 0);
    /* the texture */
    int32_t tw = gx_ri(1, 24), th = gx_ri(1, 24);
    gx_put16(GX_BITMAP, (uint32_t)th);
    gx_put16(GX_BITMAP + 2u, (uint32_t)tw);
    gx_put32(GX_BITMAP + 0xau, gx_va(TP_TEXTURE));
    for (uint32_t i = 0; i < 0x800u; ++i) gx_world_start[TP_TEXTURE + i] = (BYTE)(gx_r(3u) ? gx_r(256u) : 0);
    gx_fill_random(TP_SHADE, 0x800u);
    /* the fill */
    uint32_t kind = gx_r(10u) < 5u ? 5u : (gx_r(5u) ? 7u : gx_r(9u));
    uint32_t shade = kind == 7u && gx_r(4u) ? gx_va(TP_SHADE + gx_r(0x400u)) : 0;
    gx_put32(GX_FILL, kind);
    gx_put32(GX_FILL + 8u, gx_va(TP_CORNERS));
    gx_put32(GX_FILL + 0xcu, gx_va(GX_BITMAP));
    gx_put32(GX_FILL + 0x10u, shade);
    /* the corners */
    unsigned shape = gx_r(4u);
    uint32_t n = gx_r(12u) ? (shape == 2u ? 3u : 3u + gx_r(6u)) : gx_r(3u);
    int32_t cx = gx_ri(-8, GX_W + 8), cy = gx_ri(-8, GX_H + 8), rad = gx_ri(2, 40);
    for (uint32_t i = 0; i < n; ++i) {
        int32_t x, y;
        if (shape == 0 || shape == 3u) {    /* corners of a convex octagon around a centre, in angle order */
            static const int8_t dir[8][2] = {{0, -8}, {6, -6}, {8, 0}, {6, 6}, {0, 8}, {-6, 6}, {-8, 0}, {-6, -6}};
            uint32_t a = (i * 8u) / n;
            x = cx + dir[a][0] * rad / 8;
            y = cy + dir[a][1] * (shape == 3u ? rad / 2 : rad) / 8;
        } else if (shape == 1u) {             /* flat */
            x = gx_ri(-10, GX_W + 10);
            y = cy;
        } else {                              /* any triangle */
            x = gx_ri(-20, GX_W + 20);
            y = gx_ri(-20, GX_H + 20);
        }
        gx_point(GX_POINTS + 8u * i, x, y);
        int32_t u = gx_r(6u) ? gx_ri(0, tw - 1) : gx_ri(0, tw + 3), v = gx_r(6u) ? gx_ri(0, th - 1) : gx_ri(0, th + 3);
        gx_point(TP_CORNERS + 8u * i, u, v);
    }
    if (n >= 3u && gx_r(2u)) {                /* corners in the other order */
        for (uint32_t i = 0; i < n / 2u; ++i) {
            uint32_t a = GX_POINTS + 8u * i, b = GX_POINTS + 8u * (n - 1u - i);
            uint32_t ta = TP_CORNERS + 8u * i, tb = TP_CORNERS + 8u * (n - 1u - i);
            for (uint32_t w = 0; w < 8u; w += 4u) {
                uint32_t t = get32(gx_p(a + w)); gx_put32(a + w, get32(gx_p(b + w))); gx_put32(b + w, t);
                t = get32(gx_p(ta + w)); gx_put32(ta + w, get32(gx_p(tb + w))); gx_put32(tb + w, t);
            }
        }
    }
    unsigned how = gx_r(3u);
    snprintf(gx_desc, sizeof(gx_desc), "polygon n %lu shape %u kind %lu shade %d texture %ldx%ld mode %lu regions %lu how %u",
             (unsigned long)n, shape, (unsigned long)kind, shade != 0, (long)tw, (long)th, (unsigned long)mode,
             (unsigned long)get32(gx_p(c)), how);
    { size_t at = strlen(gx_desc);   /* the corners and the clip rect */
      for (uint32_t i = 0; i < n && at < sizeof(gx_desc) - 40u; ++i, at = strlen(gx_desc))
          snprintf(gx_desc + at, sizeof(gx_desc) - at, " (%ld,%ld:%ld,%ld)", (long)(int32_t)get32(gx_p(GX_POINTS + 8u * i)), (long)(int32_t)get32(gx_p(GX_POINTS + 8u * i + 4u)),
                   (long)(int32_t)get32(gx_p(TP_CORNERS + 8u * i)), (long)(int32_t)get32(gx_p(TP_CORNERS + 8u * i + 4u)));
      snprintf(gx_desc + at, sizeof(gx_desc) - at, " clip %ld %ld %ld %ld", (long)(int32_t)get32(gx_p(c + 0x20cu)), (long)(int32_t)get32(gx_p(c + 0x210u)), (long)(int32_t)get32(gx_p(c + 0x214u)), (long)(int32_t)get32(gx_p(c + 0x218u))); }
    /* coverage from the input (the paths the original takes) */
    if ((int32_t)n < 3) tp_cv[TPC_FEW]++;
    else {
        int32_t ymin = 0x7fff, ymax = -0x8000;
        int rising = 0;
        for (uint32_t i = 0; i < n; ++i) {
            int32_t y = (int32_t)get32(gx_p(GX_POINTS + 8u * i + 4u));
            ymin = y < ymin ? y : ymin;
            ymax = y > ymax ? y : ymax;
        }
        int32_t top = tp_ymin_index(n);
        for (uint32_t i = 1; top >= 0 && i + 1u < n; ++i) {   /* a corner other than the first lowest lower than its successor */
            int32_t y0 = (int32_t)get32(gx_p(GX_POINTS + 8u * ((top + i) % n) + 4u)), y1 = (int32_t)get32(gx_p(GX_POINTS + 8u * ((top + i + 1u) % n) + 4u));
            if (y1 < y0 && y0 != ymax) rising = 1;
        }
        if (ymax <= ymin) tp_cv[TPC_FLAT]++;
        else {
            if (rising) tp_cv[TPC_RISING_EDGE]++;
            if (ymin < (int32_t)get32(gx_p(c + 0x210u))) tp_cv[TPC_ABOVE_TOP]++;
            if (ymax > (int32_t)get32(gx_p(c + 0x218u))) tp_cv[TPC_BOTTOM]++;
            else tp_cv[TPC_EDGE_END]++;
            if (kind == 7u && shade) tp_cv[TPC_KIND7_SHADE]++; else tp_cv[TPC_TRANSPARENT]++;
            if (mode == 0) tp_cv[TPC_MODE0]++;
            else if (mode == 1u) tp_cv[TPC_MODE1]++;
            else if (!get32(gx_p(c))) tp_cv[TPC_MODE2_EMPTY]++;
            else {
                for (uint32_t i = 0; i < get32(gx_p(c)); ++i) {
                    uint32_t rk = get32(gx_p(GX_ENTRIES + 0x20u * i));
                    tp_cv[rk == 0 ? TPC_MODE2_RECT : rk == 2u ? TPC_MODE2_SPANS : TPC_MODE2_OTHER]++;
                }
            }
            int32_t xmin = 0x7fffffff;
            for (uint32_t i = 0; i < n; ++i) { int32_t x = (int32_t)get32(gx_p(GX_POINTS + 8u * i)); xmin = x < xmin ? x : xmin; }
            if (xmin < (int32_t)get32(gx_p(c + 0x20cu))) tp_cv[TPC_LEFT_CLIP]++;
            if (shape == 2u) tp_cv[TPC_TRIANGLE]++;
            for (uint32_t i = 0; i + 1u < n; ++i) {
                if ((int32_t)get32(gx_p(TP_CORNERS + 8u * i)) > (int32_t)get32(gx_p(TP_CORNERS + 8u * i + 8u))) tp_cv[TPC_DU_NEG]++;
                if ((int32_t)get32(gx_p(TP_CORNERS + 8u * i + 4u)) > (int32_t)get32(gx_p(TP_CORNERS + 8u * i + 12u))) tp_cv[TPC_DV_NEG]++;
            }
            if (how == 1u) tp_cv[TPC_VIA_SLOT]++;
            if (how == 2u) tp_cv[TPC_VIA_ROW]++;
        }
    }
    uint32_t eax[2] = {0, 0};
    for (unsigned side = 0; side < 2u; ++side) {
        gx_side(side);
        uint32_t args[2] = {n, gx_va(GX_POINTS)};
        if (!side) {
            lc_machine(0x0048a924u, args, 2, NULL);
        } else if (how == 0) {
            if (es2_sim_gfx_polygon(&lc_loop, n, args[1])) gx_bad = 1;
        } else if (how == 1u) {
            if (es2_sim_gfx_slot(&lc_loop, 33u, args, 2u, NULL)) gx_bad = 1;
        } else {
            es2_sim_result r;
            if (es2_sim_loop_call(&lc_loop, 0x0048a924u, args, 2, &r)) gx_bad = 1;
        }
        tp_mask();
        gx_end(side);
    }
    return gx_compare("polygon", k, eax[0], eax[1], 0);
}

static int gfx_polygon_cases(void) {
    current_function = "gfx polygon";
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
        return fail("gfx polygon: memory");
    if (get32(mapped + (SU_COUNTER - 0x400000u))) return fail("gfx polygon: the counter word is not free");
    if (get32(mapped + (0x4a586cu - 0x400000u) + 4u * 33u) != 0x0048a924u || get32(mapped + (0x4a586cu - 0x400000u) + 4u * 31u) != 0x004898dfu)
        return fail("gfx polygon: the BMP_8 driver table is not at 0x4a586c");
    su_pristine_take();
    static const struct { const char *dll, *name; void *stub; } stubs[] = {
        {"cw3220.dll", "_calloc", (void *)su_calloc}, {"cw3220.dll", "_free", (void *)su_free},
        {"cw3220.dll", "_memset", (void *)ic_memset}, {"cw3220.dll", "_sprintf", (void *)gx_sprintf},
        {"KERNEL32.dll", "GetTickCount", (void *)lc_tick},
    };
    for (unsigned i = 0; i < sizeof(stubs) / sizeof(stubs[0]); ++i)
        if (!ir_patch_import(stubs[i].dll, stubs[i].name, stubs[i].stub)) return fail("gfx polygon: import slot not found");
    if (tp_dispatch_setup()) return 1;
    gx_active_dispatch = &tp_dispatch;
    int bad = 0;
    static const struct { const char *name; int (*run)(unsigned); unsigned cases; } parts[] = {
        {"cursor", tp_cursor_case, 1000}, {"polygon", tp_polygon_case, 6000},
    };
    for (unsigned p = 0; !bad && p < sizeof(parts) / sizeof(parts[0]); ++p) {
        for (unsigned k = 0; !bad && k < parts[p].cases; ++k) {
            current_case = k; current_case_valid = 1;
            gx_bad = 0;
            bad = parts[p].run(k);
        }
        if (!bad) printf("PASS: gfx polygon %s: %u cases identical\n", parts[p].name, parts[p].cases);
    }
    if (!bad) {
        printf("GFX POLYGON coverage:");
        for (unsigned i = 0; i < TPC_COUNT; ++i) printf(" %s %u%s", tp_cv_names[i], tp_cv[i], i + 1u < TPC_COUNT ? "," : "\n");
        for (unsigned i = 0; i < TPC_COUNT; ++i)
            if (!tp_cv[i]) { fprintf(stderr, "FAIL: gfx polygon: path '%s' never taken\n", tp_cv_names[i]); bad = 1; }
    }
    current_case_valid = 0;
    gx_active_dispatch = &gx_dispatch;
    put32(mapped + 0x2cbee4u, 0);
    memcpy(mapped + SU_DATA_RVA, su_data_pristine, SU_DATA_SIZE);
    return bad;
}
