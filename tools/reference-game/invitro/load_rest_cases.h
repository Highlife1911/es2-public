/* The load root's last guest callees, original machine code against
 * sim/src/load.c (--load-rest; target :differential_load_rest_test). Included
 * after view_cases.h; reuses startup_cases.h's DATA reset and snapshots
 * (su_take, su_compare), loop_cases.h's loop set-up (lc_reset) and
 * audio_cases.h's register-capturing machine call (au_call) and passed row
 * (au_original). Identifiers use the prefix ldr_/LDR_.
 *
 * Every case builds a random world in an identity-mapped buffer and presets
 * DATA words, then runs the same calls on both sides with the same random
 * entry registers: side 0 the original's code, side 1 the own rows through
 * a dispatch of the sim's real services (view_transform.h's object select 0x48c444
 * and terrain lighting 0x48c16c among them) plus the registry hooks the
 * calls reach as passed rows running the original's code (normalize
 * 0x46c23c and view_cases.h's ten matrix and vector hooks). Compared after
 * the calls: EAX, EDX and ECX of every call, the world, DATA and the heap.
 *
 * - shading: the terrain shading pass 0x46c2e0 over a random grid G
 *   (shifts [G+0x100]/[G+0x104] 0..3, now and then with high bits, a shift
 *   of 31 that leaves the loops empty or the rows outside the grid; the z
 *   shift [G+0x108] and the signed divisor [G+0x118]; corner heights fully
 *   random, 0/1 or level, so the split d is zero, positive and negative;
 *   random split bits and cell bytes) with 0..3 random lights of the three
 *   kinds.
 * - small: the cockpit step 0x433f7c (H+0x232 == [H+0x226] or not, word
 *   0x4d2566 zero or not, [P+4] zero or not), the cursor sprite store
 *   0x465618 and the mono output 0x4955c4 with the handle -1.
 * - renderer reset: 0x48dd08 with random words in the default
 *   index list 0x6c6068 (the original writes 0, 1, .., 31 there), a
 *   random class count [0x4a6488] (zero or not) and registry index
 *   [0x4a1690] 0..3; only EAX is compared for it (its row's contract).
 * - refusals (own side only): 0x4955c4 with a handle other than -1 and
 *   0x46c2e0 with a zero divisor are refused (the original calls
 *   DeviceIoControl, or faults). */
#pragma once
/* The headers this suite builds on (sim_main.c). */
#include "init_records_cases.h"
#include "startup_cases.h"
#include "loop_cases.h"
#include "audio_cases.h"
#include "es2/sim/load.h"
#include "es2/sim/view.h"

#define LDR_WORLD_SIZE 0x2000u
#define LDR_G 0x0000u        /* the terrain object (0x120) */
#define LDR_VIEW 0x01c0u     /* a view matrix (0x20) */
#define LDR_LIGHTS 0x0200u   /* 3 lights of 0x40 */
#define LDR_H 0x0400u        /* the cockpit object (0x240) */
#define LDR_P 0x0700u        /* its +0x236 record */
#define LDR_CELLS 0x0800u    /* up to 8 x 8 cells of 16 */
static BYTE *ldr_world, *ldr_world_start;   /* start + the kept side-0 result */
static es2_sim_entry ldr_entries[4096];
static es2_sim_dispatch ldr_dispatch;
static es2_sim_load ldr_load;
static es2_sim_view ldr_view;
static int ldr_bad, ldr_expect_refusal;
static unsigned ldr_refusals;
static void ldr_refuse(const es2_sim_refusal *r, void *user) {
    (void)user;
    ++ldr_refusals;
    if (ldr_expect_refusal) return;
    fprintf(stderr, "FAIL: own load rest: refused call to 0x%08lx (%s): %s\n", (unsigned long)r->va, r->name ? r->name : "?",
            r->reason ? r->reason : "");
    ldr_bad = 1;
}
static int ldr_dispatch_setup(void) {
    es2_sim_dispatch_init(&ldr_dispatch, ldr_entries, 4096, ldr_refuse, NULL);
    static es2_sim_services s;
    s.memory = &ir_memory;
    s.heap = &ir_heap;
    s.vt = &lc_vt;
    s.fs0 = NULL;
    static const struct { uint32_t va; unsigned slots; const char *name; } passed[] = {
        {0x0046c23cu, 2, "normalize (original)"},
        {0x00495132u, 1, "matrix_ctor (original)"}, {0x00492a30u, 2, "vector_subtract (original)"},
        {0x0047ebb8u, 2, "euler_matrix (original)"}, {0x0047fa20u, 3, "matrix_compose (original)"},
        {0x0047df19u, 1, "matrix_transpose (original)"}, {0x00480304u, 3, "rotate_point (original)"},
        {0x0048043cu, 3, "transform_point (original)"}, {0x004801d4u, 3, "matrix_point (original)"},
        {0x00492aafu, 3, "dist3d_forward (original)"}, {0x0049290cu, 2, "vector_angle (original)"},
    };
    if (es2_sim_register_services(&ldr_dispatch, &s)) return fail("load rest: dispatch");
    for (unsigned i = 0; i < sizeof(passed) / sizeof(passed[0]); ++i)
        if (es2_sim_dispatch_add_service(&ldr_dispatch, passed[i].va, passed[i].name, au_original, (void *)(uintptr_t)passed[i].slots, 0,
                                         (uint8_t)passed[i].slots))
            return fail("load rest: dispatch");
    const char *error = NULL;
    uint32_t bad = 0;
    if (es2_sim_dispatch_seal(&ldr_dispatch, &error, &bad)) return fail("load rest: dispatch seal");
    return 0;
}

/* ---- the case script: DATA presets, the world, the calls */
typedef struct ldr_preset { uint32_t va, value; unsigned width; } ldr_preset;
static ldr_preset ldr_presets[32];
static unsigned ldr_preset_count;
static void ldr_set32(uint32_t va, uint32_t v) { ldr_presets[ldr_preset_count++] = (ldr_preset){va, v, 4}; }
static void ldr_set16(uint32_t va, uint32_t v) { ldr_presets[ldr_preset_count++] = (ldr_preset){va, v, 2}; }
static void ldr_set8(uint32_t va, uint32_t v) { ldr_presets[ldr_preset_count++] = (ldr_preset){va, v, 1}; }
typedef struct ldr_op { uint32_t va, a[2], in[3]; unsigned n, regs; } ldr_op;
static ldr_op ldr_ops[8];
static unsigned ldr_op_count;
static uint32_t ldr_res[2][8][3];
static void ldr_op_add(uint32_t va, unsigned n, uint32_t a0) {
    ldr_op *o = &ldr_ops[ldr_op_count++];
    o->va = va; o->n = n; o->a[0] = a0; o->a[1] = 0; o->regs = 3u;
    for (unsigned i = 0; i < 3u; ++i) o->in[i] = random32();
}
static uint32_t ldr_rand(uint32_t n) { return random32() % n; }
static uint32_t ldr_signed(uint32_t range) { return (uint32_t)((int32_t)ldr_rand(2u * range + 1u) - (int32_t)range); }
static uint32_t ldr_at(uint32_t offset) { return (uint32_t)(uintptr_t)ldr_world + offset; }
static void ldr_w32(uint32_t offset, uint32_t v) { put32(ldr_world_start + offset, v); }
static void ldr_w16(uint32_t offset, uint32_t v) { put16(ldr_world_start + offset, (uint16_t)v); }
static void ldr_begin(void) {
    memset(ldr_world_start, 0, LDR_WORLD_SIZE);
    for (uint32_t i = 0; i < LDR_WORLD_SIZE; i += 4u) put32(ldr_world_start + i, random32());
    ldr_preset_count = 0;
    ldr_op_count = 0;
    ldr_set32(0x006c60a8u, 0);
}
static void ldr_side(unsigned side) {
    lc_reset(side, 47, 0x01000000u, 1);
    lc_loop.dispatch = &ldr_dispatch;
    es2_sim_load_headless(&ldr_load, &lc_loop);
    es2_sim_load_bind(&ldr_load);
    memset(&ldr_view, 0, sizeof(ldr_view));
    ldr_view.loop = &lc_loop;
    es2_sim_view_bind(&ldr_view);
    memcpy(ldr_world, ldr_world_start, LDR_WORLD_SIZE);
    for (unsigned i = 0; i < ldr_preset_count; ++i) {
        BYTE *p = mapped + (ldr_presets[i].va - 0x400000u);
        if (ldr_presets[i].width == 4) put32(p, ldr_presets[i].value);
        else if (ldr_presets[i].width == 2) put16(p, (uint16_t)ldr_presets[i].value);
        else *p = (BYTE)ldr_presets[i].value;
    }
}
static int ldr_run(const char *what, unsigned k) {
    for (unsigned side = 0; side < 2u; ++side) {
        ldr_side(side);
        for (unsigned i = 0; i < ldr_op_count; ++i) {
            ldr_op *o = &ldr_ops[i];
            uint32_t *out = ldr_res[side][i];
            if (!side) au_call((uint32_t)(uintptr_t)(mapped + (o->va - 0x400000u)), o->a, o->n, o->in, out);
            else {
                es2_sim_result r;
                memset(&r, 0, sizeof(r));
                if (es2_sim_call(&ldr_dispatch, o->va, o->a, o->n, o->in, &r)) ldr_bad = 1;
                out[0] = r.eax; out[1] = r.edx; out[2] = r.ecx;
            }
            if (ldr_bad) { fprintf(stderr, "FAIL: load rest %s case %u op %u (0x%08lx): a call failed\n", what, k, i, (unsigned long)o->va); return 1; }
        }
        if (!side) memcpy(ldr_world_start + LDR_WORLD_SIZE, ldr_world, LDR_WORLD_SIZE);
        su_take(side);
    }
    if (su_compare(what, k)) return 1;
    if (memcmp(ldr_world, ldr_world_start + LDR_WORLD_SIZE, LDR_WORLD_SIZE)) {
        for (unsigned i = 0, shown = 0; i < LDR_WORLD_SIZE && shown < 16u; ++i)
            if (ldr_world[i] != ldr_world_start[LDR_WORLD_SIZE + i]) {
                fprintf(stderr, "  world +0x%04x original %02x own %02x\n", i, ldr_world_start[LDR_WORLD_SIZE + i], ldr_world[i]);
                ++shown;
            }
        fprintf(stderr, "FAIL: load rest %s case %u: the world differs (world at 0x%08lx)\n", what, k, (unsigned long)(uintptr_t)ldr_world);
        return 1;
    }
    for (unsigned i = 0; i < ldr_op_count; ++i) {
        static const char *const names[3] = {"EAX", "EDX", "ECX"};
        for (unsigned j = 0; j < ldr_ops[i].regs; ++j) {
            if (ldr_res[0][i][j] == ldr_res[1][i][j]) continue;
            fprintf(stderr, "FAIL: load rest %s case %u op %u (0x%08lx): %s original 0x%08lx own 0x%08lx\n", what, k, i, (unsigned long)ldr_ops[i].va,
                    names[j], (unsigned long)ldr_res[0][i][j], (unsigned long)ldr_res[1][i][j]);
            return 1;
        }
    }
    return 0;
}

/* ---- the parts */
static unsigned ldr_split[3], ldr_empty, ldr_null_rows, ldr_interior, ldr_edge;
/* A light of a kind of the terrain lighting (0 ambient, 1 directional, 2 point). */
static void ldr_light(unsigned i) {
    uint32_t l = LDR_LIGHTS + 0x40u * i, kind = ldr_rand(3u);
    ldr_w32(l + 0x16u, kind);
    ldr_w32(l + 0x1au, ldr_rand(0x201u));
    for (unsigned j = 0; j < 3u; ++j) ldr_w32(l + 0x22u + 4u * j, ldr_signed(0x4000u));
    for (unsigned j = 0; j < 3u; ++j) ldr_w16(l + 0x2eu + 2u * j, ldr_signed(0x4000u));
    ldr_w32(l + 0x34u, 1u + ldr_rand(0x2000u));
    ldr_w32(l + 0x38u, ldr_rand(0x2001u));
    ldr_set32(0x006c60acu + 4u * i, ldr_at(l));
}
static int ldr_shading_case(unsigned k) {
    ldr_begin();
    uint32_t cols = ldr_rand(4u), rows = ldr_rand(4u);
    unsigned shape = ldr_rand(16u);
    if (shape == 0) rows = 31u;                                 /* 1 << 31 < 0: no row */
    else if (shape == 1) { cols = 31u; rows = ldr_rand(3u); }   /* rows outside the grid or empty */
    else if (shape == 2) { cols |= 0x20u * (1u + ldr_rand(7u)); rows |= 0x20u * ldr_rand(8u); }   /* high bits: masked shifts */
    ldr_w32(LDR_G + 0x100u, cols);
    ldr_w32(LDR_G + 0x104u, rows);
    ldr_w32(LDR_G + 0xecu, ldr_at(LDR_CELLS));
    /* The z word must stay nonzero: normalize 0x46c23c divides by the
     * normal's length (a zero normal faults in the original). */
    uint32_t zshift = ldr_rand(15u), den = 1u + ldr_rand(1u << zshift);
    ldr_w32(LDR_G + 0x108u, zshift);
    ldr_w32(LDR_G + 0x118u, ldr_rand(4u) ? den : (uint32_t)-(int32_t)den);
    unsigned heights = ldr_rand(3u), level = ldr_rand(256u);
    for (uint32_t i = 0; i < 64u; ++i) {
        BYTE *cell = ldr_world_start + LDR_CELLS + 16u * i;
        cell[0] = heights == 0 ? (BYTE)ldr_rand(256u) : heights == 1 ? (BYTE)(level + ldr_rand(2u)) : (BYTE)level;
    }
    unsigned lights = ldr_rand(4u);
    for (unsigned i = 0; i < lights; ++i) ldr_light(i);
    ldr_set32(0x006c60a8u, lights);
    /* The view matrix in use (identity or random) for 0x48c444's compose, lights on or off. */
    for (unsigned i = 0; i < 9u; ++i) ldr_w16(LDR_VIEW + 2u * i, ldr_signed(0x4000u));
    for (unsigned i = 0; i < 3u; ++i) ldr_w32(LDR_VIEW + 0x14u + 4u * i, ldr_signed(0x10000u));
    ldr_set32(0x006c5ff4u, ldr_rand(2u) ? 0x006c5ffcu : ldr_at(LDR_VIEW));
    ldr_set8(0x006cbc04u, ldr_rand(2u));
    ldr_op_add(0x0046c2e0u, 1, ldr_at(LDR_G));
    /* Coverage: the branches the case reaches (from the inputs). */
    int32_t nr = (int32_t)(1u << (rows & 31u)), nc = (int32_t)(1u << (cols & 31u));
    if (nr <= 0) ++ldr_empty;
    else if (nc <= 0 || (cols & 31u) + (rows & 31u) >= 31u) ++ldr_null_rows;
    else
        for (int32_t j = 0; j < nr; ++j)
            for (int32_t i = 0; i < nc; ++i) {
                if (j >= nr - 1 || i >= nc - 1) { ++ldr_edge; continue; }
                ++ldr_interior;
                const BYTE *c0 = ldr_world_start + LDR_CELLS;
                int32_t a = c0[16 * (j * nc + i)], h01 = c0[16 * ((j + 1) * nc + i)], h11 = c0[16 * ((j + 1) * nc + i + 1)],
                        h10 = c0[16 * (j * nc + i + 1)], d = a + h11 - h01 - h10;
                ++ldr_split[d == 0 ? 0 : d > 0 ? 1 : 2];
            }
    return ldr_run("shading", k);
}
static unsigned ldr_cockpit_paths[4];   /* level, word and link nonzero, link zero, word zero */
static int ldr_small_case(unsigned k) {
    ldr_begin();
    unsigned path = ldr_rand(4u);
    ldr_set32(0x0049b088u, ldr_at(LDR_H));
    ldr_w32(LDR_H + 0x226u, path == 0 ? ldr_at(LDR_H + 0x232u) : random32());
    ldr_w32(LDR_H + 0x236u, ldr_at(LDR_P));
    ldr_w32(LDR_P + 4u, path == 2 ? 0u : random32());
    ldr_set16(0x004d2566u, path == 3 ? 0u : 1u + ldr_rand(0xffffu));
    ++ldr_cockpit_paths[path];
    ldr_set32(0x006cbc50u, 0xffffffffu);
    ldr_op_add(0x00433f7cu, 0, 0);
    ldr_op_add(0x00465618u, 1, random32());
    ldr_op_add(0x004955c4u, 0, 0);
    return ldr_run("small", k);
}
static unsigned ldr_reset_counts[2];   /* class count zero, nonzero */
static int ldr_reset_case(unsigned k) {
    ldr_begin();
    for (unsigned i = 0; i < 8u; ++i) ldr_set16(0x006c6068u + 2u * ldr_rand(32u), random32());
    uint32_t n = ldr_rand(3u) ? 1u + ldr_rand(0xffu) : 0u;
    ldr_set32(0x004a6488u, n);
    ldr_set32(0x004a1690u, ldr_rand(4u));
    ++ldr_reset_counts[n != 0];
    ldr_op_add(0x0048dd08u, 0, 0);
    ldr_ops[ldr_op_count - 1u].regs = 1u;
    return ldr_run("renderer reset", k);
}
/* Own side only: the refused paths. */
static int ldr_refusal_cases(void) {
    ldr_begin();
    ldr_side(1);
    es2_sim_result r;
    uint32_t in[3] = {0, 0, 0}, a[1] = {0};
    ldr_expect_refusal = 1;
    ldr_refusals = 0;
    put32(mapped + (0x006cbc50u - 0x400000u), 0x00000044u);
    int refused = es2_sim_call(&ldr_dispatch, 0x004955c4u, a, 0, in, &r);
    if (!refused || ldr_load.refused_va != 0x00495741u) { ldr_expect_refusal = 0; return fail("load rest: 0x4955c4 with a VxD handle not refused"); }
    put32(ldr_world + LDR_G + 0x100u, 1u);
    put32(ldr_world + LDR_G + 0x104u, 1u);
    put32(ldr_world + LDR_G + 0xecu, ldr_at(LDR_CELLS));
    put32(ldr_world + LDR_G + 0x118u, 0u);
    put32(mapped + (0x006c60a8u - 0x400000u), 0u);
    put32(mapped + (0x006c5ff4u - 0x400000u), 0x006c5ffcu);
    a[0] = ldr_at(LDR_G);
    ldr_refusals = 0;
    refused = es2_sim_call(&ldr_dispatch, 0x0046c2e0u, a, 1, in, &r);
    ldr_expect_refusal = 0;
    if (!refused || ldr_load.refused_va != 0x0046c208u) return fail("load rest: 0x46c2e0 with a zero divisor not refused");
    printf("PASS: load rest refusals: 0x4955c4 with a VxD handle, 0x46c2e0 with a zero divisor\n");
    return 0;
}

static int load_rest_cases(void) {
    current_function = "load rest";
    es2_sim_identity_memory(&ir_memory);
    es2_sim_rtl_memory(&ir_memory, &su_rtl);
    su_data_pristine = malloc(SU_DATA_SIZE);
    su_snap[0] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    su_snap[1] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    ir_region = ww_alloc(IR_REGION_SIZE);
    lc_stack = VirtualAlloc(NULL, 0x10000u, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    lc_bits = ww_alloc(LC_BITS_SIZE);
    ldr_world = VirtualAlloc(NULL, LDR_WORLD_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    ldr_world_start = malloc(2u * LDR_WORLD_SIZE);
    if (!su_data_pristine || !su_snap[0] || !su_snap[1] || !ir_region || !lc_stack || !lc_bits || !ldr_world || !ldr_world_start)
        return fail("load rest: memory");
    if (get32(mapped + (SU_COUNTER - 0x400000u))) return fail("load rest: the counter word is not free");
    su_pristine_take();
    static const struct { const char *dll, *name; void *stub; } stubs[] = {
        {"cw3220.dll", "_calloc", (void *)ir_calloc}, {"cw3220.dll", "_free", (void *)ir_free},
        {"cw3220.dll", "_memset", (void *)ir_memset}, {"cw3220.dll", "_memmove", (void *)ir_memmove},
    };
    for (unsigned i = 0; i < sizeof(stubs) / sizeof(stubs[0]); ++i)
        if (!ir_patch_import(stubs[i].dll, stubs[i].name, stubs[i].stub)) {
            fprintf(stderr, "import %s!%s\n", stubs[i].dll, stubs[i].name);
            return fail("load rest: import slot not found");
        }
    if (ldr_dispatch_setup()) return 1;
    int bad = 0;
    static const struct { const char *name; int (*run)(unsigned); unsigned cases; } parts[] = {
        {"shading", ldr_shading_case, 1500}, {"small", ldr_small_case, 1000}, {"renderer reset", ldr_reset_case, 200},
    };
    for (unsigned p = 0; !bad && p < sizeof(parts) / sizeof(parts[0]); ++p) {
        for (unsigned k = 0; !bad && k < parts[p].cases; ++k) {
            current_case = k; current_case_valid = 1;
            bad = parts[p].run(k);
        }
        if (!bad) printf("PASS: load rest %s: %u cases identical\n", parts[p].name, parts[p].cases);
    }
    current_case_valid = 0;
    if (!bad) bad = ldr_refusal_cases();
    if (!bad) {
        printf("LOAD REST coverage: shading %u empty, %u without cells (rows outside the grid), %u edge cells, %u interior cells "
               "(split level %u, positive %u, negative %u); cockpit step level %u, changed %u, link zero %u, word zero %u; "
               "renderer reset class count zero %u, nonzero %u\n",
               ldr_empty, ldr_null_rows, ldr_edge, ldr_interior, ldr_split[0], ldr_split[1], ldr_split[2], ldr_cockpit_paths[0],
               ldr_cockpit_paths[1], ldr_cockpit_paths[2], ldr_cockpit_paths[3], ldr_reset_counts[0], ldr_reset_counts[1]);
        if (!ldr_empty || !ldr_null_rows || !ldr_edge || !ldr_split[0] || !ldr_split[1] || !ldr_split[2] || !ldr_cockpit_paths[0] ||
            !ldr_cockpit_paths[1] || !ldr_cockpit_paths[2] || !ldr_cockpit_paths[3] || !ldr_reset_counts[0] || !ldr_reset_counts[1])
            bad = fail("load rest: a branch was not reached");
    }
    put32(mapped + 0x2cbee4u, 0);
    memcpy(mapped + SU_DATA_RVA, su_data_pristine, SU_DATA_SIZE);
    return bad;
}
