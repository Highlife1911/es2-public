/* The actor draw slots 0x40bc14 (beam), 0x4174c8 (mech), 0x4215cc (flyer),
 * 0x408e6c (debris), 0x409cd0 (meteor), the configuration part 0x40316c with
 * its offset slot 0x40ded8, and the explosion binning callees
 * 0x408228/0x408240, original machine code against sim/src/shape_draw.c (target
 * :differential_actor_draw_test). Builds on shape_draw_cases.h: its world (vtables,
 * context, camera, part trees, instances), its recording stand-ins of the fill
 * layer, its presets and DATA snapshots (su_take, su_compare).
 *
 * The mech's part callee 0x4030d0 (part_shapes_bind) runs for real: the
 * original's machine code on side 0, on side 1 the core body
 * es2_part_shapes_bind_registers that es2sim's dispatch entry for the
 * registry hook runs (EAX/EDX from it, ECX the entry ECX as on a hook
 * entry; the original's ECX is the pool cursor, dead at 0x417510).
 *
 * Added stand-ins, on both sides (side 0: a jump patched over the
 * original's entry; side 1: a service row at the same VA, replacing
 * poly_fills.h's row at 0x468414 after the seal):
 * - 0x468414, the 2D fill: logs the count, the four corners' x, y, +0xc
 *   and +0x10 (the beam leaves +8, +0x14 and +0x18 uninitialised), the
 *   other arguments, and the gc's +0x22c record (whether it lies in that
 *   side's stack, and its two dwords).
 * - the leaf parts' draw slot: logs the part, the part offset 0x4a1020,
 *   the cell words 0x6b7b6c, the shape record 0x6c6064, the current
 *   transform 0x6bb2e4 and the view matrix's first dword.
 * The hooks dist_octagonal 0x47de4c and camera_attached 0x401078 run as
 * the original's machine code on side 1 too.
 *
 * Every op runs with random entry registers on both sides; compared: the world
 * (with the log), DATA, and EAX/EDX/ECX where the listing gives them a value
 * (rasterizer.md, "Actor draw slots and explosion binning").
 *
 * - beam: kinds 0..7 (1 and 7 draw the polygon first), count bytes
 *   negative to 4, shift bytes -1..4, point counts 2..8, segments in
 *   front of, crossing and behind the near plane, equal ends (length 0),
 *   a perspective shift or the divisor.
 * - actors: the mech (the word 0x4a9b82 on or off, part counts negative to
 *   6; per mount a list A of 0..4 slots, the instance's list B of 0..4
 *   entries, each possibly without its pointer array, zero slots and zero
 *   entries, records that overlap each other and the pool, so that the
 *   order of 0x4030d0's reads and writes shows), 0x4030d0 called directly
 *   (EAX/EDX compared), the flyer, debris, the meteor (byte +0x4b set or clear), each with
 *   or without its matrix built and detail tables.
 * - config: 0x40316c and 0x40ded8 with or without a shape.
 * - explosion: 0x408228 and 0x408240 with type bytes 0..0x10, with or
 *   without a parent, the camera attached to it or not. */
#pragma once
/* The headers this suite builds on (sim_main.c). */
#include "init_records_cases.h"
#include "startup_cases.h"
#include "loop_cases.h"
#include "shape_draw_cases.h"
#include "es2/core/sim_leaves.h"

/* World layout (offsets) beside shape_draw_cases.h's (free in these cases). */
#define R8_CAM SD_FRAME      /* the camera object (+0x32 the attached object, +0x36 the mode word) */
#define R8_EXPL 0x2840u      /* 2 explosion objects of 0x60 (SD_SAVE's room, unused here) */
#define R8_RECS 0x2900u      /* 3 beam records of 20 bytes */
#define R8_CFGPART 0x2940u   /* the configuration part (0x18) */
#define R8_LEAFP 0x2960u     /* a leaf part for the meteor's +0x41 */
#define R8_MECH 0x2b80u      /* the mech (0x206; after SD_BUF's clip buffers) */
#define R8_CFG 0x2bd0u       /* its configuration (+0xe6..+0xed), inside the mech's unused middle */
#define R8_POOL 0x2cbcu      /* [cfg+0xec]: 0x4030d0's pool, the record arena (0x70 bytes, words only) */
#define R8_POOL_SIZE 0x70u
#define R8_OBJ 0x2d88u       /* the actor of the flyer, debris and meteor ops (0x58) */
#define R8_MET 0x2de0u       /* the meteor with byte +0x4b set (0x58) */
#define R8_BEAM 0x2e38u      /* the beam (0x5c) */
#define R8_BPTS 0x2e94u      /* 8 dword points */
#define R8_KINDS 0x2ef4u     /* 8 beam kinds of 6 bytes (to 0x2f24) */
#define R8_ENTRIES 0x2f24u   /* 7 mount entries of 6 bytes: list A {dword pointer array, word count} */
#define R8_BHDR 0x2f50u      /* [mech+0x202]: +4 list B {dword pointer array, word count} */
#define R8_ACELLS 0x2f5cu    /* 6 cells that list A's slots point at, each a record pointer (never 0) */
#define R8_AARR 0x2f74u      /* 8 dwords: list A's slots, 0 or a cell */
#define R8_BARR 0x2f94u      /* 8 dwords: list B's entries, 0 or a record (to 0x2fb4, below SD_LOG) */

static uint32_t r8_in[64][3];
static unsigned r8_cov_beam[8], r8_cov_actor[9], r8_cov_cfg[3], r8_cov_expl[6];
static unsigned r8_dist_calls, r8_dist_zero, r8_fills2d, r8_rounds, r8_mech_parts;
/* 0x4030d0 (side 1): slots bound to a B record, to a pool record, zero A
 * slots, A slots left when B ran out, lists without a pointer array. */
static unsigned r8_bind[6];

/* ---- the stand-ins */
static uint32_t __cdecl r8_poly_fill(uint32_t n, uint32_t pts, uint32_t mode, uint32_t page, uint32_t shade, uint32_t flags) {
    sd_log(0x4f1u);
    sd_log(n);
    sd_log((uint32_t)sd_is_stack(sd_side, pts));
    for (uint32_t i = 0; i < n && i < 8u; ++i) {
        uint32_t p = pts + 0x1cu * i;
        sd_log(sd_g32(p));
        sd_log(sd_g32(p + 4u));
        sd_log(sd_g32(p + 0xcu));
        sd_log(sd_g32(p + 0x10u));
    }
    sd_log(mode);
    sd_log(page);
    sd_log(shade);
    sd_log(flags);
    uint32_t rec = sd_g32(get32(mapped + 0x2c5f70u) + 0x22cu);
    int live = sd_is_stack(sd_side, rec);
    sd_log((uint32_t)live);
    if (live) { sd_log(sd_g32(rec)); sd_log(sd_g32(rec + 4u)); }
    if (sd_side) ++r8_fills2d;
    return 0x2d00u + n;
}
static uint32_t __cdecl r8_leaf(uint32_t part) {
    sd_log(0x4c8u);
    sd_log(part);
    sd_log(get32(mapped + 0x0a1020u));
    sd_log(get32(mapped + 0x2b7b6cu));
    sd_log(get32(mapped + 0x2c6064u));
    sd_log(get32(mapped + 0x2bb2e4u));
    sd_log(get32(mapped + 0x2c5fb4u));
    return part + 0x200u;
}
/* 0x4030d0 as es2sim's hook entry runs it: the core body on the identity
 * memory, EAX/EDX from it, ECX the entry ECX; the coverage first, from the
 * lists as the call finds them. */
static void r8_s_mech_part(es2_sim_service_call *c, void *u) {
    (void)u;
    uint32_t a = c->slots[0], b = c->slots[1], pa = sd_g32(a), pb = sd_g32(b);
    uint16_t ca = get16((const BYTE *)(uintptr_t)(a + 4u)), cb = get16((const BYTE *)(uintptr_t)(b + 4u));
    r8_bind[4] += !pa && ca;
    r8_bind[5] += !pb && cb;
    /* The arrays are never written, so their walk gives the cases. */
    for (; ca && pa; --ca, pa += 4u) {
        uint32_t cursor = cb ? pb : 0u;
        if (cb) pb += 4u;
        --cb;
        if (!cursor) ++r8_bind[3];
        else if (!sd_g32(pa)) ++r8_bind[2];
        else ++r8_bind[sd_g32(cursor) ? 0 : 1];
    }
    ++r8_mech_parts;
    const uint32_t entry[3] = {c->eax, c->edx, c->ecx};
    uint64_t r = es2_part_shapes_bind_registers(&ir_memory, a, b, c->slots[2], entry, NULL);
    c->eax = (uint32_t)r;
    c->edx = (uint32_t)(r >> 32);
}
static void r8_s_poly_fill(es2_sim_service_call *c, void *u) {
    (void)u;
    c->eax = r8_poly_fill(c->slots[0], c->slots[1], c->slots[2], c->slots[3], c->slots[4], c->slots[5]);
}
static void r8_s_leaf(es2_sim_service_call *c, void *u) { (void)u; c->eax = r8_leaf(c->slots[0]); }
/* dist_octagonal as the original, counted (side 1). */
static void r8_s_dist(es2_sim_service_call *c, void *u) {
    au_original(c, u);
    ++r8_dist_calls;
    r8_dist_zero += c->eax == 0;
}
static const struct { uint32_t va; void *stub; es2_sim_service_fn service; unsigned slots; } r8_stubs[] = {
    {0x00468414u, (void *)r8_poly_fill, r8_s_poly_fill, 6},
};
static BYTE r8_saved[sizeof(r8_stubs) / sizeof(r8_stubs[0])][5];

/* shape_draw_cases.h's dispatch (sd_dispatch_setup) with the actor_draw.h
 * stand-ins and pass-throughs added before the seal. */
static int r8_dispatch_setup(void) {
    es2_sim_dispatch_init(&sd_dispatch, sd_entries, 4096, sd_refuse, NULL);
    static es2_sim_services s;
    s.memory = &ir_memory;
    s.heap = &ir_heap;
    s.vt = &lc_vt;
    s.fs0 = NULL;
    static const struct { uint32_t va; unsigned slots; const char *name; } passed[] = {
        {0x00495132u, 1, "matrix_ctor (original)"}, {0x00492a30u, 2, "vector_subtract (original)"},
        {0x0047ebb8u, 2, "euler_matrix (original)"}, {0x0047fa20u, 3, "matrix_compose (original)"},
        {0x0047df19u, 1, "matrix_transpose (original)"}, {0x00480304u, 3, "rotate_point (original)"},
        {0x0048043cu, 3, "transform_point (original)"}, {0x004801d4u, 3, "matrix_point (original)"},
        {0x00492aafu, 3, "dist3d_forward (original)"}, {0x0049290cu, 2, "vector_angle (original)"},
        {0x0047d32cu, 2, "angle_ratio (original)"}, {0x0047dff0u, 1, "heading_cos (original)"},
        {0x0047e0b0u, 2, "multiply_q10 (original)"}, {0x0047de72u, 3, "dist3d_delta (original)"},
        {0x0047d694u, 2, "distance_display (original)"}, {0x0047e0a0u, 2, "multiply_q8 (original)"},
        {0x0047eb30u, 2, "sincos_pair (original)"}, {0x0047ffe8u, 3, "rotate_2d (original)"},
        {0x00401078u, 2, "camera_attached (original)"},
    };
    if (es2_sim_register_services(&sd_dispatch, &s)) return fail("actor draw: dispatch");
    for (unsigned i = 0; i < sizeof(passed) / sizeof(passed[0]); ++i)
        if (es2_sim_dispatch_add_service(&sd_dispatch, passed[i].va, passed[i].name, au_original, (void *)(uintptr_t)passed[i].slots, 0,
                                         (uint8_t)passed[i].slots))
            return fail("actor draw: dispatch");
    if (es2_sim_dispatch_add_service(&sd_dispatch, 0x0047de4cu, "dist_octagonal (original)", r8_s_dist, (void *)(uintptr_t)2u, 0, 2))
        return fail("actor draw: dispatch");
    if (sd_has_row(0x004030d0u)) return fail("actor draw: 0x4030d0 has a service row");
    if (es2_sim_dispatch_add_service(&sd_dispatch, 0x004030d0u, "part_shapes_bind (core)", r8_s_mech_part, NULL, 0, 3))
        return fail("actor draw: dispatch");
    int deferred[sizeof(sd_stubs) / sizeof(sd_stubs[0])] = {0}, deferred8[sizeof(r8_stubs) / sizeof(r8_stubs[0])] = {0};
    for (unsigned i = 0; i < sizeof(sd_stubs) / sizeof(sd_stubs[0]); ++i) {
        if (sd_has_row(sd_stubs[i].va)) { deferred[i] = 1; continue; }
        if (es2_sim_dispatch_add_service(&sd_dispatch, sd_stubs[i].va, "stand-in", sd_stubs[i].service, NULL, 0, (uint8_t)sd_stubs[i].slots))
            return fail("actor draw: dispatch");
    }
    for (unsigned i = 0; i < sizeof(r8_stubs) / sizeof(r8_stubs[0]); ++i) {
        if (sd_has_row(r8_stubs[i].va)) { deferred8[i] = 1; continue; }
        if (es2_sim_dispatch_add_service(&sd_dispatch, r8_stubs[i].va, "stand-in", r8_stubs[i].service, NULL, 0, (uint8_t)r8_stubs[i].slots))
            return fail("actor draw: dispatch");
    }
    uint32_t leaf = (uint32_t)(uintptr_t)sd_leaf, leaf8 = (uint32_t)(uintptr_t)r8_leaf;
    if (sd_has_row(leaf) || sd_has_row(leaf8)) return fail("actor draw: a leaf stand-in's address has a row");
    if (es2_sim_dispatch_add_service(&sd_dispatch, leaf, "leaf part (stand-in)", sd_s_leaf, NULL, 0, 1) ||
        es2_sim_dispatch_add_service(&sd_dispatch, leaf8, "leaf part (actor draw stand-in)", r8_s_leaf, NULL, 0, 1))
        return fail("actor draw: dispatch");
    const char *error = NULL;
    uint32_t bad = 0;
    if (es2_sim_dispatch_seal(&sd_dispatch, &error, &bad)) {
        fprintf(stderr, "seal: %s 0x%08lx\n", error ? error : "?", (unsigned long)bad);
        return fail("actor draw: dispatch seal");
    }
    for (unsigned i = 0; i < sizeof(sd_stubs) / sizeof(sd_stubs[0]); ++i) {
        if (!deferred[i]) continue;
        es2_sim_entry *e = (es2_sim_entry *)es2_sim_dispatch_find(&sd_dispatch, sd_stubs[i].va);
        e->service = sd_stubs[i].service;
        e->service_user = NULL;
    }
    for (unsigned i = 0; i < sizeof(r8_stubs) / sizeof(r8_stubs[0]); ++i) {
        if (!deferred8[i]) continue;
        es2_sim_entry *e = (es2_sim_entry *)es2_sim_dispatch_find(&sd_dispatch, r8_stubs[i].va);
        e->service = r8_stubs[i].service;
        e->service_user = NULL;
    }
    es2_sim_entry *vn = (es2_sim_entry *)es2_sim_dispatch_find(&sd_dispatch, 0x0049667cu);
    if (!vn || vn->kind != ES2_SIM_SERVICE) return fail("actor draw: vector_new missing");
    vn->service = sc_original;
    vn->service_user = (void *)(uintptr_t)5u;
    static const uint32_t rows[] = {0x0040bc14u, 0x004174c8u, 0x004215ccu, 0x00408e6cu, 0x00409cd0u,
                                    0x0040316cu, 0x0040ded8u, 0x00408228u, 0x00408240u};
    for (unsigned i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i) {
        const es2_sim_entry *e = es2_sim_dispatch_find(&sd_dispatch, rows[i]);
        if (!e || e->kind != ES2_SIM_SERVICE) {
            fprintf(stderr, "no service row at 0x%08lx\n", (unsigned long)rows[i]);
            return fail("actor draw: a row is missing");
        }
    }
    return 0;
}

/* ---- the runner: sd_run with random entry registers per op */
static unsigned r8_op(uint32_t va, unsigned mask, uint32_t a) {
    unsigned i = SDOP1(va, mask, a);
    for (unsigned j = 0; j < 3u; ++j) r8_in[i][j] = sd_rand(4u) ? random32() : 0u;
    return i;
}
static int r8_run(const char *what, unsigned k) {
    for (unsigned side = 0; side < 2u; ++side) {
        sd_side = side;
        sd_side_reset(side);
        for (unsigned i = 0; i < sd_op_count; ++i) {
            sd_op *o = &sd_ops[i];
            uint32_t *out = sd_res[side][i];
            /* As in sd_run: a dead stack local left in the object matrix
             * pointer by an earlier op is replaced by the case's preset. */
            uint32_t object = 0;
            for (unsigned p = 0; p < sd_preset_count; ++p) if (sd_presets[p].va == 0x006c5ff8u) object = sd_presets[p].value;
            if (sd_is_stack(side, get32(mapped + 0x2c5ff8u))) put32(mapped + 0x2c5ff8u, object);
            if (sd_is_stack(side, get32(mapped + 0x2c6060u))) put32(mapped + 0x2c6060u, sd_at(SD_BUF));
            if (!side) au_call((uint32_t)(uintptr_t)(mapped + (o->va - 0x400000u)), o->a, o->n, r8_in[i], out);
            else {
                es2_sim_result r;
                memset(&r, 0, sizeof(r));
                if (es2_sim_call(&sd_dispatch, o->va, o->a, o->n, r8_in[i], &r)) sd_bad = 1;
                out[0] = r.eax; out[1] = r.edx; out[2] = r.ecx;
            }
            sd_res_stack[side][i] = (unsigned char)sd_is_stack(side, out[0]);
            if (sd_bad) { fprintf(stderr, "FAIL: actor draw %s case %u op %u (0x%08lx): a call failed\n", what, k, i, (unsigned long)o->va); return 1; }
        }
        sd_norm_data(side);
        if (!side) memcpy(sd_world_start + SD_WORLD_SIZE, sd_world, SD_WORLD_SIZE);
        su_take(side);
    }
    char label[64];
    snprintf(label, sizeof(label), "actor draw %s", what);
    if (sd_alias_report(label, k)) return 1;
    if (su_compare(label, k)) { sd_dump(what, k); return 1; }
    if (memcmp(sd_world, sd_world_start + SD_WORLD_SIZE, SD_WORLD_SIZE)) {
        for (unsigned i = 0, shown = 0; i < SD_WORLD_SIZE && shown < 24u; ++i)
            if (sd_world[i] != sd_world_start[SD_WORLD_SIZE + i]) {
                fprintf(stderr, "  world +0x%04x original %02x own %02x\n", i, sd_world_start[SD_WORLD_SIZE + i], sd_world[i]);
                ++shown;
            }
        sd_dump(what, k);
        fprintf(stderr, "  the world differs (log count original %lu own %lu)\n", (unsigned long)get32(sd_world_start + SD_WORLD_SIZE + SD_LOG),
                (unsigned long)get32(sd_world + SD_LOG));
        return 1;
    }
    if (get32(sd_world + SD_LOG) > SD_LOG_CAP) { fprintf(stderr, "FAIL: actor draw %s case %u: the log overflows\n", what, k); return 1; }
    static const char *const names[3] = {"EAX", "EDX", "ECX"};
    for (unsigned i = 0; i < sd_op_count; ++i)
        for (unsigned j = 0; j < 3u; ++j) {
            if (!(sd_ops[i].mask & (1u << j)) || sd_res[0][i][j] == sd_res[1][i][j]) continue;
            /* EAX a stack local's address on both sides (sd_run's rule). */
            if (j == 0u && sd_res_stack[0][i] && sd_res_stack[1][i]) continue;
            fprintf(stderr, "FAIL: actor draw %s case %u op %u (0x%08lx): %s original 0x%08lx own 0x%08lx (entry 0x%08lx)\n", what, k, i,
                    (unsigned long)sd_ops[i].va, names[j], (unsigned long)sd_res[0][i][j], (unsigned long)sd_res[1][i][j],
                    (unsigned long)r8_in[i][j]);
            sd_dump(what, k);
            return 1;
        }
    return 0;
}

/* ---- the world */
static void r8_world(void) {
    sd_begin();
    sd_world_base();
    sd_tree_reset();
    /* Leaf parts log the part offset and the cell words (r8_leaf). */
    sd_w32(SD_VT + 0x20u * SD_K_LEAF + 0x1cu, (uint32_t)(uintptr_t)r8_leaf);
    /* A near plane of at least 1: 0x48c5fc and 0x48c5cc divide by depths
     * at or beyond it. */
    sd_set32(0x006c602cu, 1u + sd_rand(0x100u));
    sd_set32(0x0049736cu, sd_rand(4u));
    sd_set32(0x00497368u, 0x100u + sd_rand(0x800u));
    sd_set32(0x004a1020u, sd_rand(3u));
    sd_set32(0x004a9868u, sd_rand(4u));
    sd_set32(0x004a9dc4u, sd_rand(4u));
    /* For direct part transforms (0x476120 inside 0x40316c). */
    sd_set32(0x006b7b68u, sd_at(SD_MATS));
    sd_set32(0x006bb2e4u, sd_rand(3u) ? 0u : sd_rand(5u));
    for (unsigned i = 0; i < 6u; ++i) sd_set8(0x006bb2b0u + i, sd_rand(2u));
    sd_set32(0x006b7b90u, sd_rand(2u) ? 0x006c5ffcu : sd_at(SD_M1));
    sd_set32(0x006b7b6cu, sd_at(SD_CELLS));
    for (unsigned i = 0; i < 6u; ++i) {
        sd_set32(0x006b7b70u + 0x110u * i + 0x20u, sd_rand(2u) ? 0x006c5ffcu : sd_at(SD_M1));
        sd_set32(0x006b7b70u + 0x110u * i + 0x24u, sd_rand(2u) ? 0x006c5ffcu : sd_at(SD_M2));
    }
}
/* An actor at a (sd_actor's fields at any offset; table i of 2). */
static uint32_t r8_actor(uint32_t a, unsigned i, uint32_t inst, uint32_t s0, uint32_t s1) {
    uint32_t t = SD_DTAB + 0x10u * i;
    sd_wvec(a + 0xcu, 0x8000u);
    sd_matrix(a + 0x12u);
    sd_w16(a + 0x32u, sd_rand(2u));
    sd_w32(a + 0x34u, inst);
    sd_w32(a + 0x41u, sd_rand(4u) ? sd_at(t) : 0u);
    sd_w32(t, sd_at(SD_LSHAPES + 0x10u * i));
    sd_w32(t + 4u, sd_at(SD_LTHR + 0x20u * i));
    sd_w16(t + 0xcu, sd_rand(4u));
    for (unsigned j = 0; j < 4u; ++j) sd_w32(SD_LSHAPES + 0x10u * i + 4u * j, sd_rand(2u) ? s0 : s1);
    uint32_t v = 0x10000u;
    for (unsigned j = 0; j < 8u; ++j) { v -= sd_rand(0x2000u); sd_w32(SD_LTHR + 0x20u * i + 4u * j, sd_rand(8u) ? v : random32()); }
    return sd_at(a);
}

/* ---- beam */
static int r8_beam_case(unsigned k) {
    r8_world();
    uint32_t kind = sd_rand(8u), count = sd_rand(8u) == 0 ? 0x80u + sd_rand(0x80u) : sd_rand(5u);
    uint32_t shift = sd_rand(6u) - 1u, points = 2u + sd_rand(7u);
    sd_w16(R8_BEAM + 0x52u, kind);
    sd_w8(R8_BEAM + 0x54u, count);
    sd_w8(R8_BEAM + 0x55u, shift);
    sd_w16(R8_BEAM + 0x56u, points);
    sd_w32(R8_BEAM + 0x58u, sd_at(R8_BPTS));
    /* The segment: two dword points; sometimes equal (length 0), sometimes
     * both near the eye. */
    unsigned shape = sd_rand(8u);
    for (unsigned i = 0; i < 8u; ++i) sd_vec(R8_BPTS + 12u * i, shape < 2u ? 0x200u : 0x2000u);
    if (shape == 2u) for (unsigned i = 0; i < 12u; i += 4u) sd_w32(R8_BPTS + 12u + i, sd_r32(R8_BPTS + i));
    for (unsigned i = 0; i < 8u; ++i) {
        sd_w16(R8_KINDS + 6u * i, sd_rand(8u) ? sd_rand(0x400u) : sd_signed(0x8000u));
        sd_w16(R8_KINDS + 6u * i + 2u, random32());
        sd_w16(R8_KINDS + 6u * i + 4u, sd_rand(3u));
    }
    for (unsigned i = 0; i < 3u; ++i) {
        for (unsigned j = 0; j < 4u; ++j) sd_w32(R8_RECS + 20u * i + 4u * j, sd_rand(4u) ? sd_rand(0x100u) : random32());
        sd_w16(R8_RECS + 20u * i + 0x10u, random32());
    }
    sd_set32(0x004a9804u, sd_at(R8_KINDS));
    sd_set32(0x004a9808u, sd_at(R8_RECS));
    /* The beam's index list 0x4a9712 (DATA, restored after the suite). */
    for (unsigned i = 0; i < 64u; ++i) sd_set16(0x004a9712u + 2u * i, sd_rand(points));
    /* Point states and cache from an earlier draw (the beam clears count). */
    for (unsigned i = 0; i < 8u; ++i) {
        sd_set8(0x006c68fau + i, sd_rand(3u));
        sd_set32(0x006c9bc2u + 8u * i, sd_signed(0x400u));
        sd_set32(0x006c9bc2u + 8u * i + 4u, sd_signed(0x400u));
    }
    unsigned ops = 1u + sd_rand(2u);
    for (unsigned j = 0; j < ops; ++j) r8_op(0x0040bc14u, SD_EAX | SD_EDX | SD_ECX, sd_at(R8_BEAM));
    unsigned rounds = (int8_t)count > 0 ? count : 0u;
    ++r8_cov_beam[kind == 1u || kind == 7u ? 0 : 1];
    r8_cov_beam[2] += rounds == 0;
    r8_cov_beam[3] += shape == 2u;
    if (r8_run("beam", k)) return 1;
    r8_rounds += ops * rounds;
    if ((kind == 1u || kind == 7u) && rounds) {
        int clipped = 0;
        for (unsigned i = 0; i < points; ++i) clipped |= mapped[0x006c68fau + i - 0x400000u] == 1u;
        ++r8_cov_beam[clipped ? 4 : 5];
    }
    r8_cov_beam[6] += (int8_t)shift < 0 || shift > 3u;
    r8_cov_beam[7] += !sd_shift;
    return 0;
}

/* sd_part shares the 16 BSP node records between the BSP parts of a world
 * (a part whose nodes would pass the 16th starts again at 0), so a node
 * written for a part with more children can link a child index beyond
 * another part's array; the original then calls through the zero dword
 * behind it (0x476bd5). Every child link of a node a BSP part can reach
 * is reduced below that part's child count. */
static void r8_bsp_links(void) {
    for (unsigned i = 0; i < sd_next_part && i < 40u; ++i) {
        if (sd_kinds[i] != SD_K_BSP) continue;
        uint32_t part = SD_PARTS + 0x30u * i, n = get16(sd_world_start + part + 0x10u);
        uint32_t base = (sd_r32(part + 0x18u) - sd_at(SD_BSP)) / 14u;
        for (uint32_t j = base; n && j < 16u; ++j)
            for (unsigned l = 0; l < 2u; ++l) {
                uint32_t at = SD_BSP + 14u * j + 0xau + 2u * l, link = get16(sd_world_start + at);
                if ((link & 0xc000u) == 0x4000u && (link & 0x3fffu) >= n) sd_w16(at, 0x4000u | (link & 0x3fffu) % n);
            }
    }
}
/* ---- actors */
/* A record of the arena: words only, so that 0x4030d0's word stores
 * never reach a pointer. */
static uint32_t r8_record(void) { return sd_at(R8_POOL + 2u * sd_rand((R8_POOL_SIZE - 8u) / 2u)); }
/* The mounts' lists A, the instance's list B and the cells, arrays and
 * records 0x4030d0 walks. A list without a pointer array has a count all
 * the same; B without one is entered by at most one A slot (the
 * original's second step reads address 4). */
static void r8_part_lists(void) {
    for (uint32_t i = 0; i < R8_POOL_SIZE; i += 2u) sd_w16(R8_POOL + i, random32());
    for (unsigned i = 0; i < 6u; ++i) sd_w32(R8_ACELLS + 4u * i, r8_record());
    for (unsigned i = 0; i < 8u; ++i) {
        sd_w32(R8_AARR + 4u * i, sd_rand(4u) ? sd_at(R8_ACELLS + 4u * sd_rand(6u)) : 0u);
        sd_w32(R8_BARR + 4u * i, sd_rand(3u) ? r8_record() : 0u);
    }
    int b_none = sd_rand(8u) == 0;
    sd_w32(R8_BHDR + 4u, b_none ? 0u : sd_at(R8_BARR + 4u * sd_rand(5u)));
    sd_w16(R8_BHDR + 8u, b_none ? sd_rand(2u) : sd_rand(5u));
    for (unsigned i = 0; i < 7u; ++i) {
        int a_none = sd_rand(10u) == 0;
        sd_w32(R8_ENTRIES + 6u * i, a_none ? 0u : sd_at(R8_AARR + 4u * sd_rand(5u)));
        sd_w16(R8_ENTRIES + 6u * i + 4u, b_none && !a_none ? sd_rand(2u) : sd_rand(5u));
    }
}
static int r8_actors_case(unsigned k) {
    r8_world();
    uint32_t s0 = sd_shape(), s1 = sd_shape();
    r8_bsp_links();
    uint32_t mech = r8_actor(R8_MECH, 0, sd_instance(0, s0), s0, s1), obj = r8_actor(R8_OBJ, 1, sd_instance(1, s1), s0, s1);
    uint32_t met = r8_actor(R8_MET, 1, sd_instance(2, s0), s0, s1);
    sd_w8(R8_OBJ + 0x4bu, 0);
    sd_w32(R8_OBJ + 0x50u, sd_rand(4u));
    sd_w8(R8_MET + 0x4bu, 1u + sd_rand(0xffu));
    sd_w32(R8_MET + 0x41u, sd_at(sd_rand(2u) ? R8_LEAFP : SD_INST + 0x20u * 3u));
    sd_instance(3, s1);
    sd_w32(R8_LEAFP, sd_vt(SD_K_LEAF));
    uint32_t parts = sd_rand(6u) == 0 ? 0x8000u + sd_rand(0x8000u) : sd_rand(7u);
    sd_w32(R8_MECH + 0x1f2u, sd_at(R8_CFG));
    sd_w32(R8_MECH + 0x202u, sd_at(R8_BHDR));
    sd_w32(R8_CFG + 0xe6u, sd_at(R8_ENTRIES));
    sd_w16(R8_CFG + 0xeau, parts);
    r8_part_lists();
    int on = sd_rand(4u) != 0;
    sd_set16(0x004a9b82u, on ? 1u + sd_rand(0xffffu) : 0u);
    unsigned ops = 1u + sd_rand(4u);
    for (unsigned j = 0; j < ops; ++j) {
        unsigned pick = sd_rand(6u);
        ++r8_cov_actor[pick < 5u ? pick : 8u];
        switch (pick) {
        case 0: r8_op(0x004174c8u, SD_EAX | SD_ECX, mech); r8_cov_actor[5 + on] += 1; r8_cov_actor[7] += on && (int16_t)parts > 0; break;
        case 1: r8_op(0x004215ccu, SD_EAX | SD_ECX, obj); break;
        case 2: r8_op(0x00408e6cu, SD_EAX | SD_ECX, obj); break;
        case 3: r8_op(0x00409cd0u, SD_EAX | SD_ECX, met); break;
        case 4: r8_op(0x00409cd0u, SD_EAX | SD_ECX, obj); break;
        default: {
            unsigned i = SDOP3(0x004030d0u, SD_EAX | SD_EDX, sd_at(R8_ENTRIES + 6u * sd_rand(7u)), sd_at(R8_BHDR + 4u),
                               sd_at(R8_POOL + 2u * sd_rand(4u)));
            for (unsigned m = 0; m < 3u; ++m) r8_in[i][m] = sd_rand(4u) ? random32() : 0u;
            break;
        }
        }
    }
    return r8_run("actors", k);
}

/* ---- configuration parts */
static int r8_config_case(unsigned k) {
    r8_world();
    uint32_t s0 = sd_shape();
    r8_bsp_links();
    int shape = sd_rand(5u) != 0;
    sd_w32(R8_CFGPART, sd_vt(SD_K_LEAF));
    sd_w16(R8_CFGPART + 4u, sd_transform_word());
    sd_w32(R8_CFGPART + 0x10u, shape ? s0 : 0u);
    sd_w32(R8_CFGPART + 0x14u, sd_at(SD_CELLS + 2u * sd_rand(8u)));
    sd_set32(0x006c6064u, random32());
    unsigned ops = 1u + sd_rand(3u);
    for (unsigned j = 0; j < ops; ++j) {
        if (sd_rand(2u)) r8_op(0x0040316cu, SD_EAX | SD_ECX, sd_at(R8_CFGPART));
        else r8_op(0x0040ded8u, SD_EAX | SD_ECX, sd_at(R8_CFGPART));
    }
    ++r8_cov_cfg[shape ? 0 : 1];
    r8_cov_cfg[2] += shape && get16(sd_world_start + (s0 - sd_at(0)) + 4u) != 0xffffu;
    return r8_run("config", k);
}

/* ---- explosion binning */
static int r8_explosion_case(unsigned k) {
    r8_world();
    uint32_t parents[2];
    for (unsigned i = 0; i < 2u; ++i) {
        uint32_t e = R8_EXPL + 0x60u * i, type = sd_rand(3u) ? sd_rand(0x11u) : random32() & 0xffu;
        parents[i] = sd_rand(5u) ? sd_at(SD_PARTS + 0x30u * sd_rand(40u)) : 0u;
        sd_w8(e + 0x41u, type);
        sd_w32(e + 0x57u, parents[i]);
    }
    unsigned pick = sd_rand(2u);
    sd_w32(R8_CAM + 0x32u, sd_rand(3u) ? parents[pick] : random32());
    sd_w16(R8_CAM + 0x36u, sd_rand(3u) ? 2u : sd_rand(5u));
    sd_set32(0x004d24eau, sd_at(R8_CAM));
    unsigned ops = 1u + sd_rand(4u);
    for (unsigned j = 0; j < ops; ++j) {
        unsigned i = sd_rand(2u);
        uint32_t e = R8_EXPL + 0x60u * i, type = sd_world_start[e + 0x41u];
        if (sd_rand(2u)) { r8_op(0x00408228u, SD_EAX | SD_EDX | SD_ECX, sd_at(e)); ++r8_cov_expl[parents[i] ? 0 : 1]; continue; }
        r8_op(0x00408240u, SD_EAX | SD_EDX | SD_ECX, sd_at(e));
        if (type == 2u || (type >= 0xbu && type <= 0xeu)) ++r8_cov_expl[2];
        else if (!parents[i]) ++r8_cov_expl[3];
        else if (get16(sd_world_start + R8_CAM + 0x36u) == 2u && sd_r32(R8_CAM + 0x32u) == parents[i]) ++r8_cov_expl[4];
        else ++r8_cov_expl[5];
    }
    return r8_run("explosion", k);
}

static int actor_draw_cases(void) {
    current_function = "actor draw";
    es2_sim_identity_memory(&ir_memory);
    es2_sim_rtl_memory(&ir_memory, &su_rtl);
    su_data_pristine = malloc(SU_DATA_SIZE);
    su_snap[0] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    su_snap[1] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    ir_region = ww_alloc(IR_REGION_SIZE);
    lc_stack = VirtualAlloc(NULL, 0x10000u, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    lc_bits = ww_alloc(LC_BITS_SIZE);
    sd_world = VirtualAlloc(NULL, SD_WORLD_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    sd_world_start = malloc(2u * SD_WORLD_SIZE);
    if (!su_data_pristine || !su_snap[0] || !su_snap[1] || !ir_region || !lc_stack || !lc_bits || !sd_world || !sd_world_start)
        return fail("actor draw: memory");
    if (get32(mapped + (SU_COUNTER - 0x400000u))) return fail("actor draw: the counter word is not free");
    su_pristine_take();
    static const struct { const char *dll, *name; void *stub; } imports[] = {
        {"cw3220.dll", "_calloc", (void *)ir_calloc}, {"cw3220.dll", "_free", (void *)ir_free},
        {"cw3220.dll", "_memset", (void *)ir_memset}, {"cw3220.dll", "_memmove", (void *)ir_memmove},
    };
    for (unsigned i = 0; i < sizeof(imports) / sizeof(imports[0]); ++i)
        if (!ir_patch_import(imports[i].dll, imports[i].name, imports[i].stub)) {
            fprintf(stderr, "import %s!%s\n", imports[i].dll, imports[i].name);
            return fail("actor draw: import slot not found");
        }
    if (r8_dispatch_setup()) return 1;
    for (unsigned i = 0; i < sizeof(sd_stubs) / sizeof(sd_stubs[0]); ++i)
        if (sd_patch(sd_stubs[i].va, sd_stubs[i].stub, sd_saved[i], 1)) return 1;
    for (unsigned i = 0; i < sizeof(r8_stubs) / sizeof(r8_stubs[0]); ++i)
        if (sd_patch(r8_stubs[i].va, r8_stubs[i].stub, r8_saved[i], 1)) return 1;
    int bad = 0;
    static const struct { const char *name; int (*run)(unsigned); unsigned cases; } parts[] = {
        {"beam", r8_beam_case, 3000}, {"actors", r8_actors_case, 2000}, {"config", r8_config_case, 1500},
        {"explosion", r8_explosion_case, 1500},
    };
    for (unsigned p = 0; !bad && p < sizeof(parts) / sizeof(parts[0]); ++p) {
        for (unsigned k = 0; !bad && k < parts[p].cases; ++k) {
            current_case = k; current_case_valid = 1;
            bad = parts[p].run(k);
        }
        if (!bad) printf("PASS: actor draw %s: %u cases identical\n", parts[p].name, parts[p].cases);
    }
    if (!bad) {
        printf("actor draw coverage: beam polygon kinds %u other kinds %u, no rounds %u, equal ends %u, polygon clipped %u not %u, shift outside 0..3 %u, "
               "divisor %u; %u rounds, %u lengths (%u zero), %u quads\n",
               r8_cov_beam[0], r8_cov_beam[1], r8_cov_beam[2], r8_cov_beam[3], r8_cov_beam[4], r8_cov_beam[5], r8_cov_beam[6], r8_cov_beam[7],
               r8_rounds, r8_dist_calls, r8_dist_zero, r8_fills2d);
        printf("actor draw coverage: actors mech %u (off %u on %u, with parts %u), flyer %u, debris %u, meteor byte set %u clear %u\n",
               r8_cov_actor[0], r8_cov_actor[5], r8_cov_actor[6], r8_cov_actor[7], r8_cov_actor[1], r8_cov_actor[2], r8_cov_actor[3],
               r8_cov_actor[4]);
        printf("actor draw coverage: 0x4030d0 %u calls (%u direct); slots bound to a B record %u, to a pool record %u, zero %u, "
               "B out %u; A without array %u, B without array %u\n",
               r8_mech_parts, r8_cov_actor[8], r8_bind[0], r8_bind[1], r8_bind[2], r8_bind[3], r8_bind[4], r8_bind[5]);
        printf("actor draw coverage: config with shape %u without %u (own transform %u); explosion cells parent %u none %u, hidden type %u "
               "no parent %u attached %u not %u\n",
               r8_cov_cfg[0], r8_cov_cfg[1], r8_cov_cfg[2], r8_cov_expl[0], r8_cov_expl[1], r8_cov_expl[2], r8_cov_expl[3], r8_cov_expl[4],
               r8_cov_expl[5]);
    }
    current_case_valid = 0;
    for (unsigned i = 0; i < sizeof(sd_stubs) / sizeof(sd_stubs[0]); ++i)
        if (sd_patch(sd_stubs[i].va, sd_stubs[i].stub, sd_saved[i], 0)) bad = 1;
    for (unsigned i = 0; i < sizeof(r8_stubs) / sizeof(r8_stubs[0]); ++i)
        if (sd_patch(r8_stubs[i].va, r8_stubs[i].stub, r8_saved[i], 0)) bad = 1;
    put32(mapped + 0x2cbee4u, 0);
    memcpy(mapped + SU_DATA_RVA, su_data_pristine, SU_DATA_SIZE);
    return bad;
}
