/* The perspective fill 0x468760 and the shade lookup 0x430e34 of DBSIM, original machine code
 * against sim/src/poly.c (--poly-rest; target :differential_poly_rest_test). Included after
 * poly_cases.h; reuses its world (py_world, py_world_reset, py_side, py_end, py_compare) and the
 * DATA reset and snapshots of startup_cases.h and loop_cases.h.
 *
 * A case builds one synthetic world as poly_cases.h does, then either keeps
 * its 64x48 surface or switches to a wide one (pitch 1024, 12 rows, the
 * clip rect up to 1024 pixels wide, a depth buffer of its own) so that rows
 * of 640 pixels and more occur: the row recip then comes from 0x47e068 and
 * a remainder of 640 or more reads past the recip table. Points are placed
 * in view space so that they project near the surface (screen x = x << f /
 * depth + centre), some behind the near distance (the near clip), some at
 * one shared depth (equal w at both row ends: steps of 1024); the focal
 * shift [0x6c6028] 4..10, the far flag [0x49f250] on or off, modes 0..2,
 * transformed and untransformed points, depth on/off, region mode 2 with
 * and without run lists.
 *
 * The own side runs first: inputs on which the own code refuses with a
 * divide fault (#DE: a 64-bit u/v quotient that does not fit, which would
 * fault the original too) are counted and skipped without running the
 * original. Every other case is compared as poly_cases.h does (EAX, DATA,
 * the world byte for byte). */
#pragma once
/* The headers this suite builds on (sim_main.c). */
#include "init_records_cases.h"
#include "startup_cases.h"
#include "loop_cases.h"
#include "input_layer_cases.h"
#include "gfx_cases.h"
#include "poly_cases.h"

static es2_sim_entry pf_entries[4096];
static es2_sim_dispatch pf_dispatch;
static const char *pf_reason;
static void pf_refuse(const es2_sim_refusal *r, void *user) {
    (void)user;
    if (!pf_reason) pf_reason = r->reason ? r->reason : "";
    if (!r->reason || !strstr(r->reason, "#DE"))
        fprintf(stderr, "FAIL: own perspective fill: refused call to 0x%08lx (%s): %s\n", (unsigned long)r->va, r->name ? r->name : "?",
                r->reason ? r->reason : "");
    py_bad = 1;
}
static int pf_dispatch_setup(void) {
    es2_sim_dispatch_init(&pf_dispatch, pf_entries, 4096, pf_refuse, NULL);
    static es2_sim_services s;
    s.memory = &ir_memory;
    s.heap = &ir_heap;
    s.vt = &lc_vt;
    s.fs0 = NULL;
    if (es2_sim_register_services(&pf_dispatch, &s)) return fail("poly rest: dispatch");
    if (es2_sim_dispatch_add_service(&pf_dispatch, 0x0048043cu, "transform_point (original)", gx_original, (void *)(uintptr_t)3u, 0, 3))
        return fail("poly rest: dispatch");
    const char *error = NULL;
    uint32_t bad = 0;
    if (es2_sim_dispatch_seal(&pf_dispatch, &error, &bad)) return fail("poly rest: dispatch seal");
    return 0;
}

#define PF_WIDE_W 1024
#define PF_WIDE_H 12
#define PF_WIDE_DEPTH 0x20200u   /* the wide depth buffer (1024 x 12 dwords and slack) */
static uint32_t pf_far_flag, pf_wide, pf_width, pf_height;
static unsigned pf_cov_shift[11], pf_cov_far, pf_cov_wide_rows, pf_cov_rest_high, pf_cov_steps, pf_cov_rest0, pf_cov_skipped, pf_cov_near_cut,
    pf_cov_empty, pf_cov_rows_case;

static void pf_side(unsigned side) {
    py_side(side);
    lc_loop.dispatch = &pf_dispatch;
    BYTE *d = mapped - 0x400000u;
    put32(d + 0x4a3676u, pf_width);
    put32(d + 0x6c6034u, pf_width / 2u);
    put32(d + 0x6c6038u, pf_height / 2u);
    put32(d + 0x49f250u, pf_far_flag);
    if (pf_wide) put32(d + 0x6c5f90u, py_va(PF_WIDE_DEPTH));
}

/* The world: poly_cases.h's, made wide on request. */
static void pf_world_reset(void) {
    py_world_reset();
    pf_wide = py_r(3u) == 0;
    pf_width = pf_wide ? PF_WIDE_W : PY_W;
    pf_height = pf_wide ? PF_WIDE_H : PY_H;
    pf_far_flag = py_r(3u) == 0 ? 1u + py_r(0x100u) : 0;
    py_shift = 4u + py_r(7u);
    if (!pf_wide) return;
    for (int32_t y = 0; y < 64; ++y) py_put32(PY_ROWS + 4u + 4u * (uint32_t)y, (uint32_t)(y * PF_WIDE_W));
    py_fill_random(PY_BITS, 0x4000u);
    py_fill_random(PF_WIDE_DEPTH, 0xc100u);
    int32_t l = py_ri(0, 120), t = py_ri(0, 4);
    py_put32(PY_GC + 0x210u, (uint32_t)l);
    py_put32(PY_GC + 0x214u, (uint32_t)t);
    py_put32(PY_GC + 0x218u, (uint32_t)(py_r(2u) ? PF_WIDE_W - 1 : py_ri(l, PF_WIDE_W - 1)));
    py_put32(PY_GC + 0x21cu, (uint32_t)py_ri(t, PF_WIDE_H - 1));
    for (uint32_t y = 0; y < 64u; ++y) {
        uint32_t runs = PY_RUNS + 0x20u * y;
        int32_t x = py_ri(-6, 200);
        for (uint32_t i = 0; i < 4u; ++i) {
            int32_t len = py_ri(1, 300);
            py_put32(runs + 8u * i, (uint32_t)x);
            py_put32(runs + 8u * i + 4u, (uint32_t)len);
            x += len + py_ri(0, 100);
        }
    }
}

static void pf_row_coverage(void);

/* One case: n points in view space {x, depth, z}, their u/v, shades. */
static int pf_case(unsigned k) {
    pf_world_reset();
    uint32_t n = 1u + py_r(6u), mode = py_r(3u), page = py_r(4u), flags = py_r(2u) ? 0 : 1u, transformed = py_r(5u) != 0, eax[2] = {0, 0};
    int same = py_r(3u) == 0, cut = py_r(4u) == 0;
    int32_t near_d = (int32_t)py_near, spread = py_r(2u) ? 4000 : py_r(2u) ? 400 : 60, common = py_ri(near_d, near_d + spread);
    int32_t cx = (int32_t)pf_width / 2, cy = (int32_t)pf_height / 2;
    for (uint32_t i = 0; i < n; ++i) {
        int32_t depth = same ? common : py_ri(near_d, near_d + spread);
        if (cut && py_r(2u)) { depth = py_ri(near_d - 60, near_d - 1); ++pf_cov_near_cut; }
        int32_t xs = py_ri(-(int32_t)pf_width / 4, (int32_t)pf_width * 5 / 4), ys = py_ri(-(int32_t)pf_height / 4, (int32_t)pf_height * 5 / 4);
        int64_t d = depth > 0 ? depth : 1;
        py_put32(PY_POINTS + 12u * i, (uint32_t)(int32_t)(((int64_t)(xs - cx) * d) >> py_shift));
        py_put32(PY_POINTS + 12u * i + 4u, (uint32_t)depth);
        py_put32(PY_POINTS + 12u * i + 8u, (uint32_t)(int32_t)(((int64_t)(ys - cy) * d) >> py_shift));
        py_put32(PY_UV + 8u * i, py_r(0x400u));
        py_put32(PY_UV + 8u * i + 4u, py_r(0x400u));
        py_put32(PY_SHADES + 4u * i, py_r(16u));
    }
    if (!transformed) for (unsigned i = 0; i < 8u; ++i) py_matrix[i] = py_ri(-0x40, 0x40);
    snprintf(py_desc, sizeof(py_desc), "persp n %u mode %u transformed %u near %lu shift %lu depth %u wide %u far %lu same %d region %u runs %u", n,
             mode, transformed, (unsigned long)py_near, (unsigned long)py_shift, py_depth_on, pf_wide, (unsigned long)pf_far_flag, same,
             get32(py_world_start + PY_GC + 0x208u), py_runs_on);
    int skip = 0;
    for (unsigned order = 0; order < 2u && !skip; ++order) {
        unsigned side = 1u - order;   /* the own side first */
        pf_side(side);
        uint32_t args[8] = {py_va(PY_POINTS), py_va(PY_UV), n, page, py_va(PY_SHADES), mode, transformed, flags};
        if (!side) eax[0] = lc_machine(0x00468760u, args, 8, NULL);
        else {
            pf_reason = NULL;
            if (es2_sim_poly_fill_persp(&lc_loop, args[0], args[1], (int32_t)n, page, args[4], (int32_t)mode, transformed, flags, &eax[1])) py_bad = 1;
            if (py_bad && pf_reason && strstr(pf_reason, "#DE")) { skip = 1; ++pf_cov_skipped; py_bad = 0; }
            if (!eax[1]) ++pf_cov_empty;
            else if (!py_bad) pf_row_coverage();
        }
        py_end(side);
    }
    if (skip) return 0;
    return py_compare("persp", k, eax[0], eax[1], 1);
}

/* Coverage of the row loop, read from the own side's edge tables: the
 * step shift per row, rows of 640 pixels and more, several steps in one
 * row, remainders of 0 and of 640 and more (past the recip table). */
static void pf_row_coverage(void) {
    BYTE *d = mapped - 0x400000u;
    uint32_t e = ES2_SIM_VA_POLY_EDGES;
    int32_t rows = (int32_t)get32(d + e + 4u);
    uint32_t left = get32(d + e + 8u), right = get32(d + e + 0xcu);
    if (rows > 0) ++pf_cov_rows_case;
    for (int32_t r = 0; r < rows; ++r, left += 0x28u, right += 0x28u) {
        int32_t w = (int32_t)(get32(d + right) - get32(d + left));
        if (w <= 0) continue;
        int32_t lw = (int32_t)get32(d + left + 0x10u), rw = (int32_t)get32(d + right + 0x10u);
        unsigned s = 10u;
        if (lw != rw) {
            int32_t dd = (int32_t)((lw < rw ? (uint32_t)rw - (uint32_t)lw : (uint32_t)lw - (uint32_t)rw)) >> 12;
            s = dd > 3000 ? 4u : dd > 1000 ? 5u : dd > 300 ? 6u : 7u;
        }
        ++pf_cov_shift[s];
        if (w >= 0x280) ++pf_cov_wide_rows;
        if (w >= 2 * (1 << s)) ++pf_cov_steps;
        int32_t rest = w % (1 << s);
        if (rest == 0) ++pf_cov_rest0;
        if (rest >= 0x280) ++pf_cov_rest_high;
    }
    if (pf_far_flag && rows > 0) ++pf_cov_far;
}

/* The shade lookup 0x430e34(colour, level): a palette {+0xc int16 entry
 * count, +0x10 entries} at PY_OUT, [0x49b020] pointing to it, entries
 * {pointer, int16 count} at PY_OUT + 0x40 whose tables lie in
 * PY_SHADE_TABLE with room on both sides (counts 0, negative and up to 64,
 * levels -0x100..0x1ff and exactly 0x100: the "i == count" branch). EAX and
 * EDX are compared; ECX is not visible to lc_machine. */
#define PF_PALETTE 0x2000u
#define PF_PALETTE_ENTRIES 0x2040u
static unsigned pf_cov_shade_zero, pf_cov_shade_clamp, pf_cov_shade_top;
static int pf_shade_case(unsigned k) {
    pf_world_reset();
    int32_t entries = py_r(8u) == 0 ? 0 : py_r(10u) == 0 ? -py_ri(1, 5) : py_ri(1, 40);
    py_put32(PF_PALETTE + 0xcu, (uint32_t)entries & 0xffffu);
    py_put32(PF_PALETTE + 0x10u, py_va(PF_PALETTE_ENTRIES));
    for (uint32_t i = 0; i < 40u; ++i) {
        int32_t count = py_r(6u) == 0 ? 0 : py_r(8u) == 0 ? -py_ri(1, 8) : py_ri(1, 64);
        py_put32(PF_PALETTE_ENTRIES + 8u * i, py_va(PY_SHADE_TABLE + 0x200u * i + 0x100u));
        py_put32(PF_PALETTE_ENTRIES + 8u * i + 4u, (uint32_t)count & 0xffffu);
    }
    uint32_t colour = py_r(2u) ? py_r(0x30u) : py_r(0x10000u) * 0x10000u + py_r(0x10000u);
    int32_t level = py_r(4u) == 0 ? 0x100 : py_ri(-0x100, 0x1ff);
    uint32_t idx = colour & 0xffu;
    if ((int32_t)idx >= (int16_t)entries) { idx = 0; ++pf_cov_shade_clamp; }
    if ((int16_t)entries == 0) ++pf_cov_shade_zero;
    else {
        int32_t count = (int16_t)get32(py_world_start + PF_PALETTE_ENTRIES + 8u * idx + 4u);
        if ((int32_t)(((int64_t)level * count) >> 8) == count) ++pf_cov_shade_top;
    }
    snprintf(py_desc, sizeof(py_desc), "shade colour %08lx level %ld entries %ld", (unsigned long)colour, (long)level, (long)entries);
    uint32_t eax[2] = {0, 0}, edx[2] = {0, 0}, ecx = 0;
    for (unsigned side = 0; side < 2u; ++side) {
        pf_side(side);
        put32(mapped - 0x400000u + 0x49b020u, py_va(PF_PALETTE));
        uint32_t args[2] = {colour, (uint32_t)level};
        if (!side) eax[0] = lc_machine(0x00430e34u, args, 2, &edx[0]);
        else es2_sim_poly_shade(&lc_loop, colour, (uint32_t)level, &eax[1], &edx[1], &ecx);
        py_end(side);
    }
    if (edx[0] != edx[1]) {
        fprintf(stderr, "  case %u: %s\nFAIL: poly shade case %u: EDX %08lx original, %08lx own\n", k, py_desc, k, (unsigned long)edx[0],
                (unsigned long)edx[1]);
        return 1;
    }
    return py_compare("shade", k, eax[0], eax[1], 1);
}

static int poly_rest_cases(void) {
    current_function = "poly rest";
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
        return fail("poly rest: memory");
    if (get32(mapped + (SU_COUNTER - 0x400000u))) return fail("poly rest: the counter word is not free");
    su_pristine_take();
    static const struct { const char *dll, *name; void *stub; } stubs[] = {
        {"cw3220.dll", "_calloc", (void *)su_calloc}, {"cw3220.dll", "_free", (void *)su_free},
        {"cw3220.dll", "_memset", (void *)ic_memset}, {"KERNEL32.dll", "GetTickCount", (void *)lc_tick},
    };
    for (unsigned i = 0; i < sizeof(stubs) / sizeof(stubs[0]); ++i)
        if (!ir_patch_import(stubs[i].dll, stubs[i].name, stubs[i].stub)) {
            fprintf(stderr, "import %s!%s\n", stubs[i].dll, stubs[i].name);
            return fail("poly rest: import slot not found");
        }
    if (pf_dispatch_setup()) return 1;
    int bad = 0;
    static const struct { const char *name; int (*run)(unsigned); unsigned cases; } parts[] = {
        {"persp", pf_case, 3000},
        {"shade", pf_shade_case, 2000},
    };
    for (unsigned p = 0; !bad && p < sizeof(parts) / sizeof(parts[0]); ++p) {
        for (unsigned k = 0; !bad && k < parts[p].cases; ++k) {
            current_case = k; current_case_valid = 1;
            py_bad = 0;
            py_desc[0] = 0;
            bad = parts[p].run(k);
        }
        if (!bad) printf("PASS: poly rest %s: %u cases identical\n", parts[p].name, parts[p].cases);
    }
    if (!bad)
        printf("POLY REST coverage: %u skipped (#DE), %u empty, %u near cuts, %u with rows, %u far-flag fills; rows by step shift"
               " 4:%u 5:%u 6:%u 7:%u 10:%u; %u rows of 640+, %u with several steps, %u remainders 0, %u remainders 640+\n",
               pf_cov_skipped, pf_cov_empty, pf_cov_near_cut, pf_cov_rows_case, pf_cov_far, pf_cov_shift[4], pf_cov_shift[5], pf_cov_shift[6],
               pf_cov_shift[7], pf_cov_shift[10], pf_cov_wide_rows, pf_cov_steps, pf_cov_rest0, pf_cov_rest_high);
    if (!bad)
        printf("POLY REST shade coverage: %u no entries, %u indices clamped to 0, %u levels at the top (i == count)\n", pf_cov_shade_zero,
               pf_cov_shade_clamp, pf_cov_shade_top);
    current_case_valid = 0;
    put32(mapped + 0x2cbee4u, 0);
    memcpy(mapped + SU_DATA_RVA, su_data_pristine, SU_DATA_SIZE);
    return bad;
}
