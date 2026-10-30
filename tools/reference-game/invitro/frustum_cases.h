/* The terrain draw's view frustum library, original machine code against
 * sim/src/frustum.c (--frustum; target :differential_frustum_test). Included
 * after terrain_draw_cases.h; reuses its original-code row (td_original),
 * startup_cases.h's DATA snapshots (su_take, su_compare), loop_cases.h's loop
 * set-up (lc_reset) and audio_cases.h's register-capturing machine call
 * (au_call).
 *
 * Every case fills an identity-mapped world (a frustum record, a camera
 * and its graphics context, polygons, planes, points) and runs the same
 * calls on both sides with the same random entry registers: side 0 the
 * original's code, side 1 the own rows through a dispatch of the sim's
 * real services. The registry hooks the library calls (matrix_ctor,
 * rotate_point, line_plane and the camera pose's matrix helpers) run as
 * the original's machine code on the own side. Compared after the
 * sequence: EAX, EDX and ECX of every call (except 0x4949e4's ECX: the
 * address of the caller's own stack local, a dead frame address on both
 * sides), the world and DATA.
 *
 * - leaves: 6 random calls of the small rows (0x4950a4, 0x4950ec,
 *   0x49520e, 0x495221, 0x495236, 0x495254, 0x47eaf8, 0x47e450 with short
 *   and long normals, 0x47e73c on random polygons: planes on the axes with
 *   points on them, coordinates large enough for the 32-bit dot products
 *   to wrap so that line_plane misses, inputs below three points).
 * - frustum: 0x4949a4, 0x4949e4 from a random camera and view rectangle,
 *   then 0x494e58 with every plane (the constructor's mask) on a convex
 *   polygon round the eye; or 0x494e58 alone on a random frustum record
 *   (random planes, vertical ones among them, a random mask).
 * The run fails unless every branch of the own code was taken both ways
 * (the module's counters and the outcomes of side 0). */
#pragma once
/* The headers this suite builds on (sim_main.c). */
#include "init_records_cases.h"
#include "startup_cases.h"
#include "loop_cases.h"
#include "terrain_draw_cases.h"
#include "audio_cases.h"
#include "es2/sim/frustum.h"
#include <math.h>

#define FR_WORLD_SIZE 0x0a00u
/* World layout (offsets). */
#define FR_F 0x0000u        /* the frustum record, 0x78 */
#define FR_CAM 0x0080u      /* the camera, 0x40 */
#define FR_CTX 0x00c0u      /* its graphics context (+0x210..+0x227) */
#define FR_POLY 0x0300u     /* the polygon given to 0x494e58 */
#define FR_PTS 0x0310u      /* its points, 24 x 12 */
#define FR_OUT 0x0430u      /* the clip's output polygon */
#define FR_OPTS 0x0440u
#define FR_P 0x0560u        /* a plane */
#define FR_N 0x0570u        /* a normal */
#define FR_Q 0x0580u        /* a point */
#define FR_SW 0x0590u       /* two dwords for the swap */
#define FR_IN 0x05a0u       /* the split's input, front and back polygons */
#define FR_INPTS 0x05b0u
#define FR_A 0x06d0u
#define FR_APTS 0x06e0u
#define FR_B 0x0800u
#define FR_BPTS 0x0810u
#define FR_PO 0x0930u       /* 0x47e450's output plane */
static BYTE *fr_world, *fr_world_start;
static es2_sim_entry fr_entries[4096];
static es2_sim_dispatch fr_dispatch;
static es2_sim_load fr_load;
static es2_sim_view fr_view;
static es2_sim_input fr_input;
static es2_sim_frustum fr_frustum;
static int fr_bad;
static void fr_refuse(const es2_sim_refusal *r, void *user) {
    (void)user;
    fprintf(stderr, "FAIL: own frustum: refused call to 0x%08lx (%s): %s\n", (unsigned long)r->va, r->name ? r->name : "?",
            r->reason ? r->reason : "");
    fr_bad = 1;
}
static int fr_put(uint32_t va, const char *name, es2_sim_service_fn fn, void *user, unsigned slots) {
    for (unsigned i = 0; i < fr_dispatch.count; ++i) {
        es2_sim_entry *e = &fr_dispatch.entries[i];
        if (e->va != va) continue;
        e->kind = ES2_SIM_SERVICE; e->name = name; e->service = fn; e->service_user = user; e->pop_bytes = 0; e->stack_slots = (uint8_t)slots;
        return 0;
    }
    return es2_sim_dispatch_add_service(&fr_dispatch, va, name, fn, user, 0, (uint8_t)slots);
}
static int fr_dispatch_setup(void) {
    es2_sim_dispatch_init(&fr_dispatch, fr_entries, 4096, fr_refuse, NULL);
    static es2_sim_services s;
    s.memory = &ir_memory;
    s.heap = &ir_heap;
    s.vt = &lc_vt;
    s.fs0 = NULL;
    /* The hooks (not registered by es2_sim_register_services) as the
     * original's machine code. */
    static const struct { uint32_t va; unsigned slots; const char *name; } passed[] = {
        {0x00495132u, 1, "matrix_ctor (original)"}, {0x00492a30u, 2, "vector_subtract (original)"},
        {0x0047ebb8u, 2, "euler_matrix (original)"}, {0x0047fa20u, 3, "matrix_compose (original)"},
        {0x0047df19u, 1, "matrix_transpose (original)"}, {0x00480304u, 3, "rotate_point (original)"},
        {0x0048043cu, 3, "transform_point (original)"}, {0x004801d4u, 3, "matrix_point (original)"},
        {0x00492aafu, 3, "dist3d_forward (original)"}, {0x0049290cu, 2, "vector_angle (original)"},
        {0x0047e610u, 3, "line_plane (original)"},
    };
    if (es2_sim_register_services(&fr_dispatch, &s)) return fail("frustum: dispatch");
    for (unsigned i = 0; i < sizeof(passed) / sizeof(passed[0]); ++i) {
        /* A VA with a service row stays the row: only hooks are passed. */
        for (unsigned j = 0;; ++j) {
            uint32_t va = 0;
            if (!es2_sim_service_row(j, &va)) break;
            if (va == passed[i].va) return fail("frustum: a passed VA has a service row");
        }
        if (fr_put(passed[i].va, passed[i].name, td_original, (void *)(uintptr_t)passed[i].slots, passed[i].slots))
            return fail("frustum: dispatch");
    }
    const char *error = NULL;
    uint32_t bad = 0;
    if (es2_sim_dispatch_seal(&fr_dispatch, &error, &bad)) return fail("frustum: dispatch seal");
    return 0;
}

/* ---- the case script */
typedef struct fr_op { uint32_t va, a[5], in[3]; unsigned n; } fr_op;
static fr_op fr_ops[16];
static unsigned fr_op_count;
static uint32_t fr_res[2][16][3];
static void fr_op_add(uint32_t va, unsigned n, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4) {
    fr_ops[fr_op_count++] = (fr_op){va, {a0, a1, a2, a3, a4}, {random32(), random32(), random32()}, n};
}
static uint32_t fr_rand(uint32_t n) { return random32() % n; }
static uint32_t fr_signed(uint32_t range) { return (uint32_t)((int32_t)fr_rand(2u * range + 1u) - (int32_t)range); }
static uint32_t fr_at(uint32_t offset) { return (uint32_t)(uintptr_t)fr_world + offset; }
static void fr_w32(uint32_t offset, uint32_t v) { put32(fr_world_start + offset, v); }
static void fr_w16(uint32_t offset, uint32_t v) { put16(fr_world_start + offset, (uint16_t)v); }
static uint32_t fr_r32(uint32_t offset) { return get32(fr_world_start + offset); }
static void fr_vec(uint32_t offset, uint32_t range) { for (unsigned i = 0; i < 3u; ++i) fr_w32(offset + 4u * i, fr_signed(range)); }

static void fr_side(unsigned side) {
    lc_reset(side, 47, 0x01000000u, 1);
    lc_loop.dispatch = &fr_dispatch;
    es2_sim_load_headless(&fr_load, &lc_loop);
    es2_sim_load_bind(&fr_load);
    memset(&fr_view, 0, sizeof(fr_view));
    fr_view.loop = &lc_loop;
    es2_sim_view_bind(&fr_view);
    es2_sim_input_headless(&fr_input, &lc_loop, &lc_journal[side], 640, 480);
    es2_sim_input_bind(&fr_input);
    fr_frustum.loop = &lc_loop;   /* the counters run on */
    es2_sim_frustum_bind(&fr_frustum);
    memcpy(fr_world, fr_world_start, FR_WORLD_SIZE);
}
static int fr_run(const char *what, unsigned k) {
    for (unsigned side = 0; side < 2u; ++side) {
        fr_side(side);
        for (unsigned i = 0; i < fr_op_count; ++i) {
            fr_op *o = &fr_ops[i];
            uint32_t *out = fr_res[side][i];
            static char where[200];   /* named in a fault report */
            snprintf(where, sizeof(where), "frustum (%s side %u op %u: 0x%08lx(0x%lx, 0x%lx, 0x%lx, 0x%lx, 0x%lx))", what, side, i,
                     (unsigned long)o->va, (unsigned long)o->a[0], (unsigned long)o->a[1], (unsigned long)o->a[2], (unsigned long)o->a[3],
                     (unsigned long)o->a[4]);
            current_function = where;
            if (!side) au_call((uint32_t)(uintptr_t)(mapped + (o->va - 0x400000u)), o->a, o->n, o->in, out);
            else {
                es2_sim_result r;
                memset(&r, 0, sizeof(r));
                if (es2_sim_call(&fr_dispatch, o->va, o->a, o->n, o->in, &r)) fr_bad = 1;
                out[0] = r.eax; out[1] = r.edx; out[2] = r.ecx;
            }
            if (fr_bad) { fprintf(stderr, "FAIL: frustum %s case %u op %u (0x%08lx): a call failed\n", what, k, i, (unsigned long)o->va); return 1; }
        }
        if (!side) memcpy(fr_world_start + FR_WORLD_SIZE, fr_world, FR_WORLD_SIZE);
        su_take(side);
    }
    if (su_compare(what, k)) return 1;
    if (memcmp(fr_world, fr_world_start + FR_WORLD_SIZE, FR_WORLD_SIZE)) {
        for (unsigned i = 0, shown = 0; i < FR_WORLD_SIZE && shown < 16u; ++i)
            if (fr_world[i] != fr_world_start[FR_WORLD_SIZE + i]) {
                fprintf(stderr, "  world +0x%04x original %02x own %02x\n", i, fr_world_start[FR_WORLD_SIZE + i], fr_world[i]);
                ++shown;
            }
        fprintf(stderr, "FAIL: frustum %s case %u: the world differs (world at 0x%08lx)\n", what, k, (unsigned long)(uintptr_t)fr_world);
        return 1;
    }
    for (unsigned i = 0; i < fr_op_count; ++i) {
        static const char *const names[3] = {"EAX", "EDX", "ECX"};
        for (unsigned j = 0; j < 3u; ++j) {
            if (fr_res[0][i][j] == fr_res[1][i][j] || (fr_ops[i].va == 0x004949e4u && j == 2u)) continue;
            fprintf(stderr, "FAIL: frustum %s case %u op %u (0x%08lx): %s original 0x%08lx own 0x%08lx\n", what, k, i, (unsigned long)fr_ops[i].va,
                    names[j], (unsigned long)fr_res[0][i][j], (unsigned long)fr_res[1][i][j]);
            return 1;
        }
    }
    return 0;
}

/* ---- leaves */
static unsigned fr_cov_offset[2], fr_cov_vertical[2], fr_cov_side[3], fr_cov_normal[2], fr_cov_swap_same;
/* A polygon of n points at `pts`: random in a box of the given range, or
 * on a few values of each axis (so that axis planes meet points). */
static void fr_points(uint32_t pts, unsigned n, uint32_t range, int grid, uint32_t base) {
    for (unsigned i = 0; i < n; ++i)
        for (unsigned j = 0; j < 3u; ++j)
            fr_w32(pts + 12u * i + 4u * j, grid ? base + (fr_rand(3u) - 1u) * (1u + fr_rand(range)) : fr_signed(range));
}
/* A plane: on an axis through the value v (normal +-0x800, term -n.v), or
 * random. */
static void fr_plane(uint32_t p, int axis, uint32_t v) {
    if (axis) {
        unsigned j = fr_rand(3u);
        uint32_t s = fr_rand(2u) ? 0x800u : 0xfffff800u;
        for (unsigned i = 0; i < 3u; ++i) fr_w32(p + 4u * i, i == j ? s : 0u);
        fr_w32(p + 0xcu, 0u - s * v);
    } else {
        fr_vec(p, 0x800u);
        fr_w32(p + 0xcu, random32());
    }
}
static int fr_leaves_case(unsigned k) {
    memset(fr_world_start, 0, FR_WORLD_SIZE);
    fr_op_count = 0;
    random_bytes(fr_world_start, FR_WORLD_SIZE);
    for (unsigned i = 0; i < 6u; ++i) {
        switch (fr_rand(9u)) {
        case 0: {   /* 0x4950a4 / 0x4950ec */
            uint32_t n = fr_rand(5u) ? fr_rand(13u) : fr_signed(3u);
            fr_w32(FR_IN, n);
            fr_w32(FR_IN + 4u, fr_at(FR_INPTS));
            fr_w32(FR_A + 4u, fr_at(FR_APTS));
            fr_op_add(fr_rand(2u) ? 0x004950a4u : 0x004950ecu, 3, fr_at(FR_IN), fr_at(FR_Q), fr_at(FR_A), 0, 0);
            ++fr_cov_offset[(int32_t)n > 0];
            break;
        }
        case 1: fr_op_add(0x0049520eu, 1, fr_at(FR_N), 0, 0, 0, 0); break;
        case 2: {
            int same = fr_rand(6u) == 0;
            fr_op_add(0x00495221u, 2, fr_at(FR_SW), fr_at(same ? FR_SW : FR_SW + 4u), 0, 0, 0);
            fr_cov_swap_same += (unsigned)same;
            break;
        }
        case 3: {   /* 0x495236: zero components often */
            for (unsigned j = 0; j < 3u; ++j) fr_w32(FR_P + 4u * j, fr_rand(2u) ? 0u : random32());
            int32_t x = (int32_t)fr_r32(FR_P), y = (int32_t)fr_r32(FR_P + 4u), z = (int32_t)fr_r32(FR_P + 8u);
            ++fr_cov_vertical[(x || y) && !z];
            fr_op_add(0x00495236u, 1, fr_at(FR_P), 0, 0, 0, 0);
            break;
        }
        case 4: fr_op_add(0x00495254u, 2, fr_at(FR_P), random32(), 0, 0, 0); break;
        case 5: {   /* 0x47eaf8: on, above, below */
            fr_plane(FR_P, fr_rand(2u), fr_signed(0x1000u));
            fr_points(FR_Q, 1, 0x40u, fr_rand(2u), fr_r32(FR_P + 0xcu) ? 0u - fr_r32(FR_P + 0xcu) / 0x800u : 0u);
            if (fr_rand(3u) == 0) {   /* on the plane */
                int32_t n[3] = {(int32_t)fr_r32(FR_P), (int32_t)fr_r32(FR_P + 4u), (int32_t)fr_r32(FR_P + 8u)};
                uint32_t dot = (uint32_t)n[0] * fr_r32(FR_Q) + (uint32_t)n[1] * fr_r32(FR_Q + 4u) + (uint32_t)n[2] * fr_r32(FR_Q + 8u);
                fr_w32(FR_P + 0xcu, 0u - dot);
            }
            fr_op_add(0x0047eaf8u, 2, fr_at(FR_P), fr_at(FR_Q), 0, 0, 0);
            break;
        }
        case 6: {   /* 0x47e450: short and long normals (|x| <= 15000 on each axis or not) */
            static const uint32_t edges[] = {15000u, 15001u, (uint32_t)-15000, (uint32_t)-15001, 0u, 1u};
            uint32_t range = fr_rand(2u) ? 15000u : 0x100000u;
            fr_vec(FR_N, range);
            if (fr_rand(3u) == 0) fr_w32(FR_N + 4u * fr_rand(3u), edges[fr_rand(6u)]);
            if (!fr_r32(FR_N) && !fr_r32(FR_N + 4u) && !fr_r32(FR_N + 8u)) fr_w32(FR_N + 8u, 1u);   /* a zero normal is the original's divide error */
            int big = 0;
            for (unsigned j = 0; j < 3u; ++j) big |= (int32_t)fr_r32(FR_N + 4u * j) > 15000 || (int32_t)fr_r32(FR_N + 4u * j) < -15000;
            ++fr_cov_normal[big];
            fr_vec(FR_Q, fr_rand(2u) ? 0x1000u : 0x1000000u);
            fr_op_add(0x0047e450u, 3, fr_at(FR_PO), fr_at(FR_Q), fr_at(FR_N), 0, 0);
            break;
        }
        default: {   /* 0x47e73c */
            unsigned n = fr_rand(6u) == 0 ? fr_rand(3u) : 3u + fr_rand(8u);
            int axis = fr_rand(2u), large = fr_rand(4u) == 0;
            uint32_t v = fr_signed(0x100u), range = large ? 0x400000u : 0x200u;
            fr_plane(FR_P, axis, v);
            fr_points(FR_INPTS, n, range, axis && !large, v);
            fr_w32(FR_IN, fr_rand(8u) ? n : fr_signed(2u));
            fr_w32(FR_IN + 4u, fr_at(FR_INPTS));
            fr_w32(FR_A + 4u, fr_at(FR_APTS));
            fr_w32(FR_B + 4u, fr_at(FR_BPTS));
            fr_op_add(0x0047e73cu, 4, fr_at(FR_P), fr_at(FR_IN), fr_at(FR_A), fr_at(FR_B), 0);
            break;
        }
        }
    }
    int bad = fr_run("leaves", k);
    for (unsigned i = 0; !bad && i < fr_op_count; ++i)
        if (fr_ops[i].va == 0x0047eaf8u) ++fr_cov_side[fr_res[0][i][0] + 1u < 3u ? fr_res[0][i][0] + 1u : 1u];
    return bad;
}

/* ---- frustum: the record's set-up and the clip */
static unsigned fr_cov_chain, fr_cov_random, fr_cov_clipped_empty, fr_cov_clipped_kept;
/* A convex polygon of n points round the centre c (offset) in the plane of
 * two random axes of length up to `size`. */
static void fr_convex(uint32_t pts, unsigned n, const int32_t c[3], double size) {
    double u[3], w[3], t0 = (double)fr_rand(6283u) / 1000.0;
    for (unsigned j = 0; j < 3u; ++j) { u[j] = ((double)fr_rand(2001u) - 1000.0) / 1000.0 * size; w[j] = ((double)fr_rand(2001u) - 1000.0) / 1000.0 * size; }
    for (unsigned i = 0; i < n; ++i) {
        double t = t0 + 6.283185307179586 * (double)i / (double)n;
        for (unsigned j = 0; j < 3u; ++j) fr_w32(pts + 12u * i + 4u * j, (uint32_t)(c[j] + (int32_t)lrint(cos(t) * u[j] + sin(t) * w[j])));
    }
}
static int fr_frustum_case(unsigned k) {
    memset(fr_world_start, 0, FR_WORLD_SIZE);
    fr_op_count = 0;
    uint32_t f = fr_at(FR_F), cam = fr_at(FR_CAM);
    int32_t eye[3] = {(int32_t)fr_signed(0x100000u), (int32_t)fr_signed(0x100000u), (int32_t)fr_signed(0x10000u)};
    uint32_t d = 0x40u + fr_rand(fr_rand(2u) ? 0x400u : 0x10000u), e = 0x100u + fr_rand(0x40000u);
    unsigned n = fr_rand(8u) == 0 ? fr_rand(3u) : 3u + fr_rand(6u);
    double size = (double)(fr_rand(3u) ? e : 4u * e);
    int32_t centre[3];
    for (unsigned j = 0; j < 3u; ++j) centre[j] = eye[j] + (int32_t)fr_signed(e);
    fr_convex(FR_PTS, n, centre, size);
    fr_w32(FR_POLY, n);
    fr_w32(FR_POLY + 4u, fr_at(FR_PTS));
    fr_w32(FR_OUT + 4u, fr_at(FR_OPTS));
    uint32_t a = fr_signed(0x2000u), b = fr_signed(0x2000u);
    if (fr_rand(2u)) {
        /* The chain as 0x46cb9c runs it. The camera as the library reads it
         * (through 0x495157/0x49516d/0x495162/0x495178): +4 the position,
         * +0x10 the angle words, +0x16 the graphics context (+0x210..+0x21c
         * the view rectangle, +0x220/+0x224 its origin), +0x1a the shift. */
        for (unsigned j = 0; j < 3u; ++j) fr_w32(FR_CAM + 4u + 4u * j, (uint32_t)eye[j]);
        for (unsigned j = 0; j < 3u; ++j) fr_w16(FR_CAM + 0x10u + 2u * j, fr_rand(3u) ? random32() : (j == 2u ? random32() : 0u));
        fr_w32(FR_CAM + 0x16u, fr_at(FR_CTX));
        fr_w32(FR_CAM + 0x1au, fr_rand(4u) ? 7u + fr_rand(4u) : fr_rand(40u));
        uint32_t vw = 160u + fr_rand(481u), vh = 100u + fr_rand(381u), x0 = fr_rand(4u) ? 0u : fr_rand(40u), y0 = fr_rand(4u) ? 0u : fr_rand(40u);
        fr_w32(FR_CTX + 0x210u, x0);
        fr_w32(FR_CTX + 0x214u, y0);
        fr_w32(FR_CTX + 0x218u, x0 + vw - 1u);
        fr_w32(FR_CTX + 0x21cu, y0 + vh - 1u);
        fr_w32(FR_CTX + 0x220u, 0u - vw / 2u - fr_rand(9u));
        fr_w32(FR_CTX + 0x224u, 0u - vh / 2u - fr_rand(9u));
        fr_op_add(0x004949a4u, 3, f, random32(), random32(), 0, 0);
        fr_op_add(0x004949e4u, 4, f, cam, d, e, 0);
        ++fr_cov_chain;
    } else {
        /* A random record: the eye, six planes (vertical ones, z of both
         * signs, zero normals among them) and a random mask. */
        for (unsigned j = 0; j < 3u; ++j) fr_w32(FR_F + 0xcu + 4u * j, (uint32_t)eye[j]);
        for (unsigned i = 0; i < 6u; ++i) {
            uint32_t p = FR_F + 0x18u + 16u * i;
            fr_vec(p, 0x800u);
            switch (fr_rand(5u)) {
            case 0: fr_w32(p + 8u, 0); break;                          /* vertical */
            case 1: fr_w32(p, 0); fr_w32(p + 4u, 0); break;           /* along z */
            case 2: fr_w32(p, 0); fr_w32(p + 4u, 0); fr_w32(p + 8u, 0); break;
            default: break;
            }
            fr_w32(p + 0xcu, fr_signed(e) * 0x100u);
        }
        fr_w32(FR_F, e);
        fr_w32(FR_F + 4u, d);
        fr_w32(FR_F + 8u, random32());
        ++fr_cov_random;
    }
    fr_op_add(0x00494e58u, 5, f, fr_at(FR_POLY), a, b, fr_at(FR_OUT));
    int bad = fr_run("frustum", k);
    if (!bad) { if (!get32(fr_world + FR_OUT)) ++fr_cov_clipped_empty; else ++fr_cov_clipped_kept; }
    return bad;
}

static int frustum_cases(void) {
    current_function = "frustum";
    es2_sim_identity_memory(&ir_memory);
    es2_sim_rtl_memory(&ir_memory, &su_rtl);
    su_data_pristine = malloc(SU_DATA_SIZE);
    su_snap[0] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    su_snap[1] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    ir_region = ww_alloc(IR_REGION_SIZE);
    lc_stack = VirtualAlloc(NULL, 0x10000u, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    lc_bits = ww_alloc(LC_BITS_SIZE);
    fr_world = VirtualAlloc(NULL, FR_WORLD_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    fr_world_start = malloc(2u * FR_WORLD_SIZE);
    if (!su_data_pristine || !su_snap[0] || !su_snap[1] || !ir_region || !lc_stack || !lc_bits || !fr_world || !fr_world_start)
        return fail("frustum: memory");
    if (get32(mapped + (SU_COUNTER - 0x400000u))) return fail("frustum: the counter word is not free");
    su_pristine_take();
    if (fr_dispatch_setup()) return 1;
    memset(&fr_frustum, 0, sizeof(fr_frustum));
    int bad = 0;
    static const struct { const char *name; int (*run)(unsigned); unsigned cases; } parts[] = {
        {"leaves", fr_leaves_case, 3000}, {"frustum", fr_frustum_case, 1500},
    };
    for (unsigned p = 0; !bad && p < sizeof(parts) / sizeof(parts[0]); ++p) {
        for (unsigned k = 0; !bad && k < parts[p].cases; ++k) {
            current_case = k; current_case_valid = 1;
            bad = parts[p].run(k);
        }
        if (!bad) printf("PASS: frustum %s: %u cases identical\n", parts[p].name, parts[p].cases);
    }
    const es2_sim_frustum *c = &fr_frustum;
    if (!bad) {
        printf("FRUSTUM coverage: split cases %llu/%llu/%llu/%llu/%llu/%llu/%llu/%llu/%llu, misses %llu, short inputs %llu, kept %llu, splits %llu; "
               "long normals %llu; clip planes vertical %llu, shifted by -b %llu, by a %llu; offsets empty/not %u/%u; vertical test 0/1 %u/%u; "
               "sides -1/0/1 %u/%u/%u; leaf normals short/long %u/%u; swaps of one dword %u; chains %u, random records %u; outputs empty/not %u/%u\n",
               (unsigned long long)c->cases[0], (unsigned long long)c->cases[1], (unsigned long long)c->cases[2], (unsigned long long)c->cases[3],
               (unsigned long long)c->cases[4], (unsigned long long)c->cases[5], (unsigned long long)c->cases[6], (unsigned long long)c->cases[7],
               (unsigned long long)c->cases[8], (unsigned long long)c->misses, (unsigned long long)c->short_inputs, (unsigned long long)c->kept,
               (unsigned long long)c->splits, (unsigned long long)c->long_normals, (unsigned long long)c->vertical,
               (unsigned long long)c->b_shifts, (unsigned long long)c->a_shifts, fr_cov_offset[0], fr_cov_offset[1], fr_cov_vertical[0],
               fr_cov_vertical[1], fr_cov_side[0], fr_cov_side[1], fr_cov_side[2], fr_cov_normal[0], fr_cov_normal[1], fr_cov_swap_same, fr_cov_chain, fr_cov_random, fr_cov_clipped_empty, fr_cov_clipped_kept);
        int missing = !c->misses || !c->short_inputs || !c->kept || !c->long_normals || !fr_cov_normal[0] || !fr_cov_normal[1] || !c->vertical ||
                      !c->b_shifts || !c->a_shifts || !fr_cov_offset[0] || !fr_cov_offset[1] || !fr_cov_vertical[0] || !fr_cov_vertical[1] ||
                      !fr_cov_clipped_empty || !fr_cov_clipped_kept || !fr_cov_side[0] || !fr_cov_side[1] || !fr_cov_side[2];
        for (unsigned i = 0; i < 9u; ++i) missing |= !c->cases[i];
        if (missing) { fprintf(stderr, "FAIL: frustum: a branch of the own code was not taken both ways (coverage above)\n"); bad = 1; }
    }
    current_case_valid = 0;
    put32(mapped + 0x2cbee4u, 0);
    memcpy(mapped + SU_DATA_RVA, su_data_pristine, SU_DATA_SIZE);
    return bad;
}
