/* The Gouraud fill 0x487acc, its triangle 0x486ee8 and the terrain's flat fill
 * 0x46bc44, original machine code against sim/src/gouraud.c (--gl-gouraud;
 * target :differential_gl_gouraud_test). Included after gl_cases.h and
 * shape_draw_cases.h; identifiers gg_/GG_.
 *
 * Part A on gfx_cases.h's world with gl_cases.h's dispatch (the fill layer
 * and the Gouraud rows as own code, slot 33 as the original or a
 * recorder); compared: EAX (EDX and ECX for the Gouraud fill), DATA and
 * the world.
 * - triangle: 0x486ee8 directly; fill kinds 0, 4, 5/7 (recorder), 2/3/6/8;
 *   clip modes 0..2; shade spreads 0..256 (both short edges, one of them
 *   empty, ties), equal points.
 * - gouraud: 0x487acc directly through the register-capturing call
 *   (au_call); n 0..20, 21..36 (the shade colours run into the frame:
 *   later triangles degenerate, the table pointer's words kept by the
 *   ramp), n = 0xffffffff and 0x80000002.
 * - polygon: fill kind 1 through 0x483eb8, 0x484222 and 0x4842bb.
 *
 * Part B on shape_draw_cases.h's world: the flat fill 0x46bc44 with the
 * projection 0x48cce4 and 0x42e8e8 (no rows) as the original on the own
 * side too, the clips 0x48cf20/0x48d38c and the shape fill 0x48d5c0 as
 * their rows, the haze/shade lookups as shape_draw's stand-ins and
 * 0x4842bb as a recorder that also logs the fill record at GC+0x22c.
 * 0x42e8e8 takes its early return ([0x49aef2] = 0, an empty mark entry);
 * its other path is not covered. Compared: EAX, EDX, ECX, DATA (the stack
 * pointers normalised) and the world. */
#pragma once
/* The headers this suite builds on (sim_main.c). */
#include "init_records_cases.h"
#include "startup_cases.h"
#include "loop_cases.h"
#include "input_layer_cases.h"
#include "audio_cases.h"
#include "gfx_cases.h"
#include "shape_draw_cases.h"
#include "gl_cases.h"
#include "es2/sim/gouraud.h"

#define GG_P 0x7000u        /* up to 40 points of two dwords */
#define GG_S 0x7200u        /* the triangle's three shade words */

static unsigned gg_cov_tri[8], gg_cov_tri_kind[9], gg_cov_d256, gg_cov_g[5], gg_cov_poly[3], gg_cov_ff[8];

/* ---- part A: the triangle 0x486ee8 */
static void gg_points(uint32_t off, uint32_t n) {
    if (gx_mode == 0) gl_points_random(off, n, 3, GX_W - 4, 3, GX_H - 4);
    else gl_points_random(off, n, -30, GX_W + 30, -30, GX_H + 30);
}
static uint32_t gg_fill_kind(void) {
    uint32_t r = gx_r(20u), kind = r < 12u ? 0 : r < 14u ? 4u : r < 16u ? 5u : r < 17u ? 7u : r == 17u ? 6u : r == 18u ? 2u : (gx_r(2u) ? 3u : 8u);
    gx_put32(GX_FILL, kind);
    gx_put32(GX_FILL + 4u, gx_colour());
    if (kind == 5u) {
        gx_put32(GX_FILL + 8u, gx_va(GL_TABLE));
        gx_fill_random(GL_TABLE, 0x100u);
    }
    return kind;
}
static int gg_tri_case(unsigned k) {
    gx_world_reset();
    gx_context(1);
    uint32_t kind = gg_fill_kind();
    ++gg_cov_tri_kind[kind];
    gg_points(GG_P, 3u);
    /* Equal points (each pair). */
    if (gx_r(12u) == 0) {
        unsigned a = gx_r(3u), b = (a + 1u + gx_r(2u)) % 3u;
        gx_point(GG_P + 8u * b, (int32_t)get32(gx_p(GG_P + 8u * a)), (int32_t)get32(gx_p(GG_P + 8u * a + 4u)));
    }
    /* Shades: a spread of 0..256 from a base; ties (one short edge empty) and a single colour. */
    uint32_t r = gx_r(10u), d = r == 0 ? 0 : r == 1u ? 256u : r < 5u ? 1u + gx_r(12u) : 1u + gx_r(256u);
    uint32_t base = gx_r(4u) ? gx_r(256u) : gx_r(0x10000u - d);
    uint32_t s[3];
    unsigned ihi = gx_r(3u), ilo = (ihi + 1u + gx_r(2u)) % 3u, imid = 3u - ihi - ilo;
    s[ihi] = base + d;
    s[ilo] = base;
    uint32_t m = gx_r(4u);
    s[imid] = m == 0 ? base : m == 1u ? base + d : base + gx_r(d + 1u);
    for (unsigned i = 0; i < 3u; ++i) gx_put16(GG_S + 2u * i, s[i]);
    gg_cov_d256 += d == 256u;
    int stub33 = kind == 5u || kind == 7u;
    snprintf(gx_desc, sizeof(gx_desc), "triangle kind %lu mode %d shades %lu %lu %lu points (%ld,%ld) (%ld,%ld) (%ld,%ld)", (unsigned long)kind, gx_mode,
             (unsigned long)s[0], (unsigned long)s[1], (unsigned long)s[2], (long)get32(gx_p(GG_P)), (long)get32(gx_p(GG_P + 4u)),
             (long)get32(gx_p(GG_P + 8u)), (long)get32(gx_p(GG_P + 12u)), (long)get32(gx_p(GG_P + 16u)), (long)get32(gx_p(GG_P + 20u)));
    uint32_t eax[2] = {0, 0};
    for (unsigned side = 0; side < 2u; ++side) {
        gl_side(side, stub33, 0);
        uint32_t args[2] = {gx_va(GG_P), gx_va(GG_S)};
        if (!side) eax[0] = lc_machine(ES2_SIM_VA_GOURAUD_TRIANGLE, args, 2, NULL);
        else if (es2_sim_fill_gouraud_triangle(&lc_loop, args[0], args[1], &eax[1])) gx_bad = 1;
        gx_end(side);
    }
    /* Which path: equal points, one colour, the long and one short edge, both short edges. */
    int same = 0;
    for (unsigned a = 0; a < 3u; ++a)
        if (get32(gx_p(GG_P + 8u * a)) == get32(gx_p(GG_P + 8u * ((a + 1u) % 3u))) &&
            get32(gx_p(GG_P + 8u * a + 4u)) == get32(gx_p(GG_P + 8u * ((a + 1u) % 3u) + 4u)))
            same = 1;
    ++gg_cov_tri[same ? 0 : d == 0 ? 1 : s[imid] == base + d ? 2 : s[imid] == base ? 3 : 4];
    ++gg_cov_tri[5u + (unsigned)((ihi + 1u) % 3u != ilo)];
    return gl_compare("triangle", k, eax[0], eax[1], 1);
}

/* ---- part A: the Gouraud fill 0x487acc */
static void gg_gouraud_record(void) {
    gx_put32(GX_FILL, 1u);
    gx_put32(GX_FILL + 4u, gx_r(4u) | (gx_r(0x10000u) << 16));
    gx_put32(GX_FILL + 0xcu, gx_va(GL_SHADES));
    for (uint32_t i = 0; i < 40u; ++i) gx_put16(GL_SHADES + 2u * i, gx_r(256u));
    /* Ramp entries are palette indices: a triangle's spread stays below 256. */
    for (uint32_t i = 0; i < 0x200u; ++i) gx_put16(GX_RAMP + 2u * i, gx_r(256u));
}
static int gg_gouraud_case(unsigned k) {
    gx_world_reset();
    gx_context(1);
    gg_gouraud_record();
    uint32_t r = gx_r(40u), n;
    if (r == 0) n = gx_r(2u) ? 0xffffffffu : 0x80000002u;
    else if (r < 5u) n = 21u + gx_r(16u);
    else if (r < 8u) n = gx_r(3u);
    else n = 3u + gx_r(18u);
    gg_points(GG_P, 40u);
    if (n >= 21u && n <= 36u) {
        /* Past 20 points the shade colours run into the pointers and the
         * colour words of the frame: the triangles from 18 on repeat one
         * point (degenerate) so that those colours are never drawn. From 33
         * points L[32], L[33] overwrite the table pointer that the colour
         * loop reads again: the ramp words they pick are its own halves. */
        for (uint32_t i = 19u; i < 40u; ++i) gx_point(GG_P + 8u * i, (int32_t)get32(gx_p(GG_P + 8u * 18u)), (int32_t)get32(gx_p(GG_P + 8u * 18u + 4u)));
        uint32_t entry = get32(gx_p(GX_FILL + 4u)) & 0xffffu, count = 0x20u + 0x30u * entry, table = GX_RAMP + 0x200u * (entry & 1u);
        /* The other shades pick ramp words from 2 on (count >= 0x20). */
        for (uint32_t i = 0; i < 40u; ++i) gx_put16(GL_SHADES + 2u * i, 16u + gx_r(240u));
        gx_put16(GL_SHADES + 2u * 32u, 0);
        gx_put16(GL_SHADES + 2u * 33u, (256u + count - 1u) / count);
        gx_put16(table, gx_va(table) & 0xffffu);
        gx_put16(table + 2u, gx_va(table) >> 16);
    }
    ++gg_cov_g[(int32_t)n < 0 ? 4 : n < 3u ? 0 : n <= 20u ? 1 : n <= 32u ? 2 : 3];
    snprintf(gx_desc, sizeof(gx_desc), "gouraud n %lu colour %08lx mode %d regions %lu origin %lu", (unsigned long)n,
             (unsigned long)get32(gx_p(GX_FILL + 4u)), gx_mode, (unsigned long)get32(gx_p(GX_CONTEXT)), (unsigned long)get32(gx_p(GX_CONTEXT + 0x208u)));
    uint32_t out[2][3] = {{0, 0, 0}, {0, 0, 0}};
    for (unsigned side = 0; side < 2u; ++side) {
        gl_side(side, 0, 0);
        uint32_t args[2] = {n, gx_va(GG_P)}, in[3] = {0, 0, 0};
        if (!side) au_call((uint32_t)(uintptr_t)(mapped + (ES2_SIM_VA_GOURAUD - 0x400000u)), args, 2, in, out[0]);
        else if (es2_sim_fill_gouraud(&lc_loop, args[0], args[1], &out[1][0], &out[1][1], &out[1][2])) gx_bad = 1;
        gx_end(side);
    }
    if (!gx_bad && (out[0][1] != out[1][1] || out[0][2] != out[1][2])) {
        fprintf(stderr, "  case %u: %s\nFAIL: gouraud case %u: EDX %08lx/%08lx ECX %08lx/%08lx (original/own)\n", k, gx_desc, k, (unsigned long)out[0][1],
                (unsigned long)out[1][1], (unsigned long)out[0][2], (unsigned long)out[1][2]);
        return 1;
    }
    return gl_compare("gouraud", k, out[0][0], out[1][0], 1);
}

/* ---- part A: fill kind 1 through the polygon entries */
static int gg_poly_case(unsigned k) {
    gx_world_reset();
    gx_context(1);
    gg_gouraud_record();
    unsigned which = gx_r(3u);
    uint32_t n = gx_r(8u) ? 3u + gx_r(6u) : gx_r(21u);
    int origin = get32(gx_p(GX_CONTEXT + 0x208u)) != 0;
    if (origin && n == 0 && which == 0) n = 1u;
    ++gg_cov_poly[which];
    gg_points(GX_POINTS, 22u);
    snprintf(gx_desc, sizeof(gx_desc), "polygon %lx kind 1 n %lu colour %08lx mode %d regions %lu origin %d", (unsigned long)gl_poly_va[which], (unsigned long)n,
             (unsigned long)get32(gx_p(GX_FILL + 4u)), gx_mode, (unsigned long)get32(gx_p(GX_CONTEXT)), origin);
    uint32_t eax[2] = {0, 0};
    for (unsigned side = 0; side < 2u; ++side) {
        gl_side(side, 0, 0);
        uint32_t args[2] = {n, gx_va(GX_POINTS)};
        if (!side) eax[0] = lc_machine(gl_poly_va[which], args, 2, NULL);
        else if (gl_poly_run(which, n, args[1], &eax[1])) gx_bad = 1;
        gx_end(side);
    }
    return gl_compare("gouraud polygon", k, eax[0], eax[1], which == 0 || n != 0);
}

/* ---- part B: the flat fill 0x46bc44 on the shape draw world */
#define GG_FP 0x2c00u       /* 32 points of three dwords */
#define GG_FS 0x2d80u       /* 32 shade words */
#define GG_MARKS 0x2e00u    /* the mark table stand-in [0x4cfce0] (zero) */
/* 0x4842bb: the shape draw recorder, then the fill record GC+0x22c: kind,
 * colour and for a Gouraud record its shade words (the pointer as a stack
 * mark or the world address). */
static uint32_t gg_fill_record(uint32_t eax, uint32_t n) {
    uint32_t rec = sd_g32(get32(mapped + 0x2c5f70u) + 0x22cu);
    sd_log(0xfdu);
    sd_log(sd_is_stack(sd_side, rec));
    uint32_t kind = sd_g32(rec);
    sd_log(kind);
    sd_log(sd_g32(rec + 4u));
    if (kind == 1u) {
        uint32_t s = sd_g32(rec + 0xcu);
        sd_log(sd_g32(rec + 8u));
        sd_log(sd_norm(sd_side, s));
        for (uint32_t i = 0; i < n && i < 32u; ++i) sd_log(get16((const BYTE *)(uintptr_t)(s + 2u * i)));
    }
    return eax;
}
static uint32_t __cdecl gg_fill_plain(uint32_t n, uint32_t pts) { return gg_fill_record(sd_fill(0xf3u, n, pts), n); }
static void gg_s_fill_plain(es2_sim_service_call *c, void *u) { (void)u; c->eax = gg_fill_record(sd_fill(0xf3u, c->slots[0], c->slots[1]), c->slots[0]); }
static void *gg_fd_stub(unsigned i) { return sd_stubs[i].va == 0x004842bbu ? (void *)gg_fill_plain : sd_stubs[i].stub; }
static es2_sim_service_fn gg_fd_service(unsigned i) { return sd_stubs[i].va == 0x004842bbu ? gg_s_fill_plain : sd_stubs[i].service; }
/* sd_dispatch (sd_side_reset installs it) with the flat fill's callees
 * that have no row as the original. */
static int gg_fd_setup(void) {
    es2_sim_dispatch_init(&sd_dispatch, sd_entries, (unsigned)(sizeof(sd_entries) / sizeof(sd_entries[0])), sd_refuse, NULL);
    static es2_sim_services s;
    s.memory = &ir_memory;
    s.heap = &ir_heap;
    s.vt = &lc_vt;
    s.fs0 = NULL;
    if (es2_sim_register_services(&sd_dispatch, &s)) return fail("gouraud flat: dispatch");
    static const struct { uint32_t va; unsigned slots; const char *name; } passed[] = {
        {0x0048cce4u, 0, "project (original)"}, {0x0042e8e8u, 3, "marks (original)"},
    };
    for (unsigned i = 0; i < sizeof(passed) / sizeof(passed[0]); ++i) {
        if (sd_has_row(passed[i].va)) continue;
        if (es2_sim_dispatch_add_service(&sd_dispatch, passed[i].va, passed[i].name, au_original, (void *)(uintptr_t)passed[i].slots, 0,
                                         (uint8_t)passed[i].slots))
            return fail("gouraud flat: dispatch");
    }
    int deferred[sizeof(sd_stubs) / sizeof(sd_stubs[0])] = {0};
    for (unsigned i = 0; i < sizeof(sd_stubs) / sizeof(sd_stubs[0]); ++i) {
        if (sd_has_row(sd_stubs[i].va)) { deferred[i] = 1; continue; }
        if (es2_sim_dispatch_add_service(&sd_dispatch, sd_stubs[i].va, "stand-in", gg_fd_service(i), NULL, 0, (uint8_t)sd_stubs[i].slots))
            return fail("gouraud flat: dispatch");
    }
    const char *error = NULL;
    uint32_t bad = 0;
    if (es2_sim_dispatch_seal(&sd_dispatch, &error, &bad)) {
        fprintf(stderr, "seal: %s 0x%08lx\n", error ? error : "?", (unsigned long)bad);
        return fail("gouraud flat: dispatch seal");
    }
    for (unsigned i = 0; i < sizeof(sd_stubs) / sizeof(sd_stubs[0]); ++i) {
        if (!deferred[i]) continue;
        es2_sim_entry *e = (es2_sim_entry *)es2_sim_dispatch_find(&sd_dispatch, sd_stubs[i].va);
        e->service = gg_fd_service(i);
        e->service_user = NULL;
    }
    return 0;
}
static int gg_flat_case(unsigned k) {
    sd_begin();
    sd_world_base();
    uint32_t shift = sd_shift, near_y = 1u + sd_rand(0xffu), mode = sd_rand(3u);
    sd_set32(0x006c602cu, near_y);
    sd_set32(0x004aaaacu, mode);
    sd_set8(0x0049aef2u, 0);
    sd_set32(0x004cfce0u, sd_at(GG_MARKS));
    /* The default index list 0x6c6068 (32 words): identity or shuffled. */
    uint32_t list[32];
    for (uint32_t i = 0; i < 32u; ++i) list[i] = i;
    if (sd_rand(3u) == 0)
        for (uint32_t i = 31u; i > 0; --i) { uint32_t j = sd_rand(i + 1u), t = list[i]; list[i] = list[j]; list[j] = t; }
    for (uint32_t i = 0; i < 32u; ++i) sd_set16(0x006c6068u + 2u * i, list[i]);
    /* The polygon: up to 16 points (a clip's output fits 0x6cbb02); with
     * the shaded terrain up to 6 (the clipped shade words fit 10). */
    uint32_t r = sd_rand(10u), n = r == 0 ? sd_rand(3u) : r == 1u ? 3u + sd_rand(14u) : 3u + sd_rand(6u);
    if (mode == 2u && n > 6u) n = 3u + sd_rand(4u);
    int behind_some = shift && sd_rand(2u);
    for (uint32_t i = 0; i < 32u; ++i) {
        uint32_t p = GG_FP + 12u * i;
        sd_w32(p, sd_signed(0x2000u));
        uint32_t y = behind_some && sd_rand(3u) == 0 ? near_y - sd_rand(near_y + 0x40u) : near_y + sd_rand(0x2000u);
        sd_w32(p + 4u, y);
        sd_w32(p + 8u, sd_signed(0x2000u));
        sd_w16(GG_FS + 2u * i, sd_rand(4u) ? sd_rand(256u) : sd_rand(0x10000u));
    }
    sd_op_count = 0;
    SDOP3(ES2_SIM_VA_FLAT_FILL, SD_EAX | SD_EDX | SD_ECX, sd_at(GG_FP), n, sd_at(GG_FS));
    ++gg_cov_ff[mode == 2u ? 0 : 1];
    ++gg_cov_ff[shift ? 2 : 3];
    for (unsigned side = 0; side < 2u; ++side) {
        sd_side = side;
        sd_side_reset(side);
        sd_op *o = &sd_ops[0];
        uint32_t in[3] = {0, 0, 0}, *out = sd_res[side][0];
        if (!side) au_call((uint32_t)(uintptr_t)(mapped + (o->va - 0x400000u)), o->a, o->n, in, out);
        else {
            es2_sim_result res;
            memset(&res, 0, sizeof(res));
            if (es2_sim_call(&sd_dispatch, o->va, o->a, o->n, in, &res)) sd_bad = 1;
            out[0] = res.eax; out[1] = res.edx; out[2] = res.ecx;
        }
        if (sd_bad) { fprintf(stderr, "FAIL: gouraud flat case %u (n %lu mode %lu shift %lu): a call failed\n", k, (unsigned long)n, (unsigned long)mode, (unsigned long)shift); return 1; }
        /* GC+0x22c is left on the dead record local. */
        uint32_t *rec = (uint32_t *)(sd_world + SD_CTX + 0x22cu);
        put32((BYTE *)rec, sd_norm(side, get32((BYTE *)rec)));
        sd_norm_data(side);
        if (!side) memcpy(sd_world_start + SD_WORLD_SIZE, sd_world, SD_WORLD_SIZE);
        su_take(side);
    }
    char what[96];
    snprintf(what, sizeof(what), "gouraud flat (n %lu mode %lu shift %lu near_y %lu)", (unsigned long)n, (unsigned long)mode, (unsigned long)shift,
             (unsigned long)near_y);
    if (sd_alias_report(what, k)) return 1;
    if (su_compare(what, k)) return 1;
    if (memcmp(sd_world, sd_world_start + SD_WORLD_SIZE, SD_WORLD_SIZE)) {
        for (unsigned i = 0, shown = 0; i < SD_WORLD_SIZE && shown < 24u; ++i)
            if (sd_world[i] != sd_world_start[SD_WORLD_SIZE + i]) {
                fprintf(stderr, "  world +0x%04x original %02x own %02x\n", i, sd_world_start[SD_WORLD_SIZE + i], sd_world[i]);
                ++shown;
            }
        fprintf(stderr, "FAIL: %s case %u: the world differs\n", what, k);
        return 1;
    }
    static const char *const names[3] = {"EAX", "EDX", "ECX"};
    for (unsigned j = 0; j < 3u; ++j)
        if (sd_res[0][0][j] != sd_res[1][0][j]) {
            fprintf(stderr, "FAIL: %s case %u: %s original 0x%08lx own 0x%08lx\n", what, k, names[j], (unsigned long)sd_res[0][0][j],
                    (unsigned long)sd_res[1][0][j]);
            return 1;
        }
    /* Coverage over the log: a fill reached, a point behind (the clip ran). */
    uint32_t count = get32(sd_world + SD_LOG);
    for (uint32_t i = 0; i < count && i < SD_LOG_CAP; ++i) if (get32(sd_world + SD_LOG + 4u + 4u * i) == 0xfdu) { ++gg_cov_ff[4]; break; }
    for (uint32_t i = 0; i < n; ++i) if (mapped[0x006c68fau + list[i] - 0x400000u] == 1u) { ++gg_cov_ff[5]; break; }
    gg_cov_ff[6] += n < 3u;
    gg_cov_ff[7] += n > 8u;
    return 0;
}

static int gl_gouraud_cases(void) {
    current_function = "gl gouraud";
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
    sd_world = VirtualAlloc(NULL, SD_WORLD_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    sd_world_start = malloc(2u * SD_WORLD_SIZE);
    if (!su_data_pristine || !su_snap[0] || !su_snap[1] || !ir_region || !lc_stack || !lc_bits || !gx_world || !gx_world_start || !gx_snap[0] ||
        !gx_snap[1] || !sd_world || !sd_world_start)
        return fail("gl gouraud: memory");
    if (get32(mapped + (SU_COUNTER - 0x400000u))) return fail("gl gouraud: the counter word is not free");
    if (get32(mapped + (0x4a586cu - 0x400000u) + 4u * 34u) != 0x0048a3b0u || get32(mapped + (0x4a586cu - 0x400000u) + 4u * 33u) != 0x0048a924u)
        return fail("gl gouraud: the BMP_8 driver table is not at 0x4a586c");
    su_pristine_take();
    static const struct { const char *dll, *name; void *stub; } stubs[] = {
        {"cw3220.dll", "_calloc", (void *)su_calloc}, {"cw3220.dll", "_free", (void *)su_free},
        {"cw3220.dll", "_memset", (void *)ic_memset}, {"cw3220.dll", "_sprintf", (void *)gx_sprintf},
        {"KERNEL32.dll", "GetTickCount", (void *)lc_tick},
    };
    for (unsigned i = 0; i < sizeof(stubs) / sizeof(stubs[0]); ++i)
        if (!ir_patch_import(stubs[i].dll, stubs[i].name, stubs[i].stub)) {
            fprintf(stderr, "import %s!%s\n", stubs[i].dll, stubs[i].name);
            return fail("gl gouraud: import slot not found");
        }
    if (gl_dispatch_setup()) return 1;
    gx_active_dispatch = &gl_dispatch;
    int bad = 0;
    static const struct { const char *name; int (*run)(unsigned); unsigned cases; } parts[] = {
        {"triangle", gg_tri_case, 6000}, {"gouraud", gg_gouraud_case, 4000}, {"polygon", gg_poly_case, 2000},
    };
    /* Each part's cases run in chunks of GG_CHUNK, part B's flat fill included;
     * chunk c of the whole run belongs to test shard c % count
     * (suite_part_in_shard) and starts from its own seed, so a chunk's cases do
     * not depend on the shard layout. The coverage lines count the shard's own
     * chunks. */
    enum { GG_CHUNK = 500 };
    unsigned chunk = 0;
    for (unsigned p = 0; !bad && p < sizeof(parts) / sizeof(parts[0]); ++p) {
        unsigned owned = 0;
        for (unsigned first = 0; !bad && first < parts[p].cases; first += GG_CHUNK, ++chunk) {
            if (!suite_part_in_shard(chunk)) continue;
            unsigned end = parts[p].cases - first < GG_CHUNK ? parts[p].cases : first + GG_CHUNK;
            random_state = 0x48c06e21u ^ (chunk * 0x9e3779b9u);
            for (unsigned k = first; !bad && k < end; ++k) {
                current_case = k; current_case_valid = 1;
                gx_bad = 0;
                gx_desc[0] = 0;
                bad = parts[p].run(k);
            }
            owned += end - first;
        }
        if (!bad && owned) printf("PASS: gl gouraud %s: %u of %u cases identical\n", parts[p].name, owned, parts[p].cases);
    }
    gx_desc[0] = 0;
    /* Part B: the shape draw world, its dispatch and side-0 stand-ins. */
    BYTE saved[sizeof(sd_stubs) / sizeof(sd_stubs[0])][5];
    unsigned patched = 0;
    if (!bad && gg_fd_setup()) bad = 1;
    for (; !bad && patched < sizeof(sd_stubs) / sizeof(sd_stubs[0]); ++patched)
        if (sd_patch(sd_stubs[patched].va, gg_fd_stub(patched), saved[patched], 1)) bad = 1;
    const unsigned flat_cases = 6000;
    unsigned flat_owned = 0;
    for (unsigned first = 0; !bad && first < flat_cases; first += GG_CHUNK, ++chunk) {
        if (!suite_part_in_shard(chunk)) continue;
        unsigned end = flat_cases - first < GG_CHUNK ? flat_cases : first + GG_CHUNK;
        random_state = 0x48c06e21u ^ (chunk * 0x9e3779b9u);
        for (unsigned k = first; !bad && k < end; ++k) {
            current_case = k; current_case_valid = 1;
            sd_bad = 0;
            bad = gg_flat_case(k);
        }
        flat_owned += end - first;
    }
    if (!bad && flat_owned) printf("PASS: gl gouraud flat fill: %u of %u cases identical\n", flat_owned, flat_cases);
    while (patched > 0) {
        --patched;
        if (sd_patch(sd_stubs[patched].va, gg_fd_stub(patched), saved[patched], 0)) bad = 1;
    }
    if (!bad) {
        printf("GL GOURAUD coverage: triangles equal points %u one colour %u upper edge empty %u lower edge empty %u both edges %u; winding %u/%u;"
               " fill kinds 0:%u 2:%u 3:%u 4:%u 5:%u 6:%u 7:%u 8:%u; %u with 256 steps\n",
               gg_cov_tri[0], gg_cov_tri[1], gg_cov_tri[2], gg_cov_tri[3], gg_cov_tri[4], gg_cov_tri[5], gg_cov_tri[6], gg_cov_tri_kind[0],
               gg_cov_tri_kind[2], gg_cov_tri_kind[3], gg_cov_tri_kind[4], gg_cov_tri_kind[5], gg_cov_tri_kind[6], gg_cov_tri_kind[7], gg_cov_tri_kind[8],
               gg_cov_d256);
        printf("GL GOURAUD coverage: gouraud n<3 %u, 3..20 %u, 21..32 %u, 33..36 %u, negative %u; polygon kind 1 via 0x483eb8 %u 0x484222 %u 0x4842bb %u\n",
               gg_cov_g[0], gg_cov_g[1], gg_cov_g[2], gg_cov_g[3], gg_cov_g[4], gg_cov_poly[0], gg_cov_poly[1], gg_cov_poly[2]);
        printf("GL GOURAUD coverage: flat fill shaded %u plain %u; shift %u divisor %u; filled %u; a point behind %u; n<3 %u n>8 %u\n", gg_cov_ff[0],
               gg_cov_ff[1], gg_cov_ff[2], gg_cov_ff[3], gg_cov_ff[4], gg_cov_ff[5], gg_cov_ff[6], gg_cov_ff[7]);
    }
    current_case_valid = 0;
    put32(mapped + 0x2cbee4u, 0);
    memcpy(mapped + SU_DATA_RVA, su_data_pristine, SU_DATA_SIZE);
    return bad;
}
