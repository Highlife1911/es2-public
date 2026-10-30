/* The R3D view transform, the projection and the lights, original machine code
 * against sim/src/view.c (--view; target :differential_view_test). Included
 * after audio_cases.h; reuses startup_cases.h's DATA reset and snapshots
 * (su_reset, su_take, su_compare), loop_cases.h's loop set-up (lc_reset) and
 * audio_cases.h's register-capturing machine call (au_call) and passed-row
 * function (au_original: the ten registry hooks the module calls run as the
 * original's machine code on the own side).
 *
 * Every case builds a random world (camera, context, matrices, vectors,
 * light objects, the light table) in an identity-mapped buffer and presets
 * the module's DATA words (pointers, the light list, the flags), then runs
 * the same sequence of calls on both sides: side 0 the original's code,
 * side 1 the own rows through a dispatch of the sim's real services plus
 * the passed hook rows. Compared after the sequence: EAX of every call
 * (EDX/ECX where the contract names them), the world, DATA and the heap
 * (the light objects the table allocates).
 *
 * - view: 0x48c2e4 over random cameras (shift 0..15, flag, divisor,
 *   viewport, centre) with a random active light list (0..5 lights of the
 *   three kinds) and lights on or off, then the projection leaves 0x48c57c,
 *   0x48c5fc, 0x48c6d0, 0x48c5cc, the object selection 0x48c444 (NULL or a
 *   random matrix), the lighting sums 0x48bfe8/0x48c16c, 0x48c3b0 and the
 *   saved view 0x48d7ec.
 * - table: the light table's entry life cycle (0x406ee4, 0x406f38,
 *   0x407048, 0x406fbc) and 2..6 per-object updates 0x407098 with the
 *   object near an entry or far away, the spot and point lights from the
 *   heap and the pools, then 0x407388/0x407444 and the destructors
 *   0x4075c1/0x48bcc4 with delete.
 * - leaves: 12 random calls of the small rows (dot products, vector and
 *   matrix leaves, accessors, 0x48bc70, 0x48bcfc, 0x48be98, 0x48bd6c, the
 *   MATH leaves, 0x48db12 and the forwarding rows, the table leaves,
 *   0x407878, 0x40765c, 0x407500, 0x4075c1). */
#pragma once
/* The headers this suite builds on (sim_main.c). */
#include "init_records_cases.h"
#include "startup_cases.h"
#include "loop_cases.h"
#include "audio_cases.h"
#include "es2/sim/load.h"
#include "es2/sim/view.h"

#define VW_WORLD_SIZE 0x4000u
/* World layout (offsets). */
#define VW_CAM 0x0000u
#define VW_CONTEXT 0x0100u
#define VW_M1 0x0400u
#define VW_M2 0x0440u
#define VW_M3 0x0480u
#define VW_VEC 0x0600u       /* 16 vectors of 0x10 */
#define VW_P 0x0700u         /* the saved view record */
#define VW_LIGHTS 0x0800u    /* 6 lights of 0x40 */
#define VW_SCRATCH 0x0a00u   /* a light built by 0x48bc70 */
#define VW_T 0x1000u         /* the light table, 0x800 */
#define VW_OBJ 0x1800u       /* 8 objects of 0x10 for 0x407098 */
#define VW_POS 0x1900u       /* 8 positions of 0x10 */
#define VW_LOBJ 0x1a00u      /* a light-carrying object (word +0xc) */
static BYTE *vw_world, *vw_world_start;   /* start + the kept side-0 result */
static es2_sim_entry vw_entries[4096];
static es2_sim_dispatch vw_dispatch;
static es2_sim_load vw_load;
static es2_sim_view vw_view;
static int vw_bad;
static void vw_refuse(const es2_sim_refusal *r, void *user) {
    (void)user;
    fprintf(stderr, "FAIL: own view: refused call to 0x%08lx (%s): %s\n", (unsigned long)r->va, r->name ? r->name : "?",
            r->reason ? r->reason : "");
    vw_bad = 1;
}
static int vw_dispatch_setup(void) {
    es2_sim_dispatch_init(&vw_dispatch, vw_entries, 4096, vw_refuse, NULL);
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
    };
    if (es2_sim_register_services(&vw_dispatch, &s)) return fail("view: dispatch");
    for (unsigned i = 0; i < sizeof(passed) / sizeof(passed[0]); ++i)
        if (es2_sim_dispatch_add_service(&vw_dispatch, passed[i].va, passed[i].name, au_original, (void *)(uintptr_t)passed[i].slots, 0,
                                         (uint8_t)passed[i].slots))
            return fail("view: dispatch");
    const char *error = NULL;
    uint32_t bad = 0;
    if (es2_sim_dispatch_seal(&vw_dispatch, &error, &bad)) return fail("view: dispatch seal");
    return 0;
}

/* ---- the case script: DATA presets, the world, the calls */
typedef struct vw_preset { uint32_t va, value; unsigned width; } vw_preset;
static vw_preset vw_presets[48];
static unsigned vw_preset_count;
static void vw_set32(uint32_t va, uint32_t v) { vw_presets[vw_preset_count++] = (vw_preset){va, v, 4}; }
static void vw_set16(uint32_t va, uint32_t v) { vw_presets[vw_preset_count++] = (vw_preset){va, v, 2}; }
static void vw_set8(uint32_t va, uint32_t v) { vw_presets[vw_preset_count++] = (vw_preset){va, v, 1}; }
typedef struct vw_op { uint32_t va, a[4]; uint8_t from[4]; unsigned n, mask; } vw_op;   /* from[i]: 1 + the op whose EAX is a[i] */
static vw_op vw_ops[64];
static unsigned vw_op_count;
static uint32_t vw_res[2][64][3];
#define VW_EAX 1u
#define VW_EDX 2u
#define VW_ECX 4u
static unsigned vw_op_add(uint32_t va, unsigned mask, unsigned n, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3) {
    vw_op *o = &vw_ops[vw_op_count];
    memset(o, 0, sizeof(*o));
    o->va = va; o->mask = mask; o->n = n;
    o->a[0] = a0; o->a[1] = a1; o->a[2] = a2; o->a[3] = a3;
    return vw_op_count++;
}
#define OP0(va, mask) vw_op_add((va), (mask), 0, 0, 0, 0, 0)
#define OP1(va, mask, a) vw_op_add((va), (mask), 1, (a), 0, 0, 0)
#define OP2(va, mask, a, b) vw_op_add((va), (mask), 2, (a), (b), 0, 0)
#define OP3(va, mask, a, b, c) vw_op_add((va), (mask), 3, (a), (b), (c), 0)
#define OP4(va, mask, a, b, c, d) vw_op_add((va), (mask), 4, (a), (b), (c), (d))
static uint32_t vw_rand(uint32_t n) { return random32() % n; }
static uint32_t vw_signed(uint32_t range) { return (uint32_t)((int32_t)vw_rand(2u * range + 1u) - (int32_t)range); }
static uint32_t vw_at(uint32_t offset) { return (uint32_t)(uintptr_t)vw_world + offset; }   /* a guest address in the world */
static void vw_w32(uint32_t offset, uint32_t v) { put32(vw_world_start + offset, v); }
static void vw_w16(uint32_t offset, uint32_t v) { put16(vw_world_start + offset, (uint16_t)v); }
static void vw_vec(uint32_t offset, uint32_t range) { for (unsigned i = 0; i < 3u; ++i) vw_w32(offset + 4u * i, vw_signed(range)); }
static void vw_wvec(uint32_t offset, uint32_t range) { for (unsigned i = 0; i < 3u; ++i) vw_w16(offset + 2u * i, vw_signed(range)); }
/* A random 0x20-byte matrix: nine words, two bytes, the translation. */
static void vw_matrix(uint32_t offset) {
    for (unsigned i = 0; i < 9u; ++i) vw_w16(offset + 2u * i, vw_signed(0x4000u));
    vw_w16(offset + 0x12u, vw_rand(0x10000u));
    vw_vec(offset + 0x14u, 0x10000u);
}
/* A random light object of a random kind (the three vtables). */
static uint32_t vw_light(unsigned i, unsigned kind) {
    uint32_t l = VW_LIGHTS + 0x40u * i;
    static const uint32_t vtables[3] = {0x0049f8d4u, 0x0049f8c0u, 0x00497ac8u};
    vw_w32(l, vtables[kind]);
    vw_vec(l + 4u, 0x8000u);
    vw_w32(l + 0x10u, random32());
    vw_w16(l + 0x14u, random32());
    vw_w32(l + 0x16u, kind);
    vw_w32(l + 0x1au, vw_rand(0x201u));
    vw_w32(l + 0x1eu, vtables[0]);
    vw_vec(l + 0x22u, 0x8000u);
    vw_wvec(l + 0x2eu, 0x4000u);
    vw_w32(l + 0x34u, 1u + vw_rand(0x2000u));
    vw_w32(l + 0x38u, vw_rand(0x2001u));
    vw_w32(l + 0x3cu, random32());
    return l;
}
static void vw_begin(void) {
    memset(vw_world_start, 0, VW_WORLD_SIZE);
    vw_preset_count = 0;
    vw_op_count = 0;
    vw_set32(0x006c60a8u, 0);
}
static void vw_side(unsigned side) {
    lc_reset(side, 47, 0x01000000u, 1);
    lc_loop.dispatch = &vw_dispatch;
    es2_sim_load_headless(&vw_load, &lc_loop);
    es2_sim_load_bind(&vw_load);
    memset(&vw_view, 0, sizeof(vw_view));
    vw_view.loop = &lc_loop;
    es2_sim_view_bind(&vw_view);
    memcpy(vw_world, vw_world_start, VW_WORLD_SIZE);
    for (unsigned i = 0; i < vw_preset_count; ++i) {
        BYTE *p = mapped + (vw_presets[i].va - 0x400000u);
        if (vw_presets[i].width == 4) put32(p, vw_presets[i].value);
        else if (vw_presets[i].width == 2) put16(p, (uint16_t)vw_presets[i].value);
        else *p = (BYTE)vw_presets[i].value;
    }
}
static int vw_run(const char *what, unsigned k) {
    for (unsigned side = 0; side < 2u; ++side) {
        vw_side(side);
        for (unsigned i = 0; i < vw_op_count; ++i) {
            vw_op *o = &vw_ops[i];
            uint32_t a[4], in[3] = {0, 0, 0}, *out = vw_res[side][i];
            for (unsigned j = 0; j < 4u; ++j) a[j] = o->from[j] ? vw_res[side][o->from[j] - 1u][0] : o->a[j];
            if (!side) au_call((uint32_t)(uintptr_t)(mapped + (o->va - 0x400000u)), a, o->n, in, out);
            else {
                es2_sim_result r;
                if (es2_sim_call(&vw_dispatch, o->va, a, o->n, in, &r)) vw_bad = 1;
                out[0] = r.eax; out[1] = r.edx; out[2] = r.ecx;
            }
            if (vw_bad) { fprintf(stderr, "FAIL: view %s case %u op %u (0x%08lx): a call failed\n", what, k, i, (unsigned long)o->va); return 1; }
        }
        if (!side) memcpy(vw_world_start + VW_WORLD_SIZE, vw_world, VW_WORLD_SIZE);
        su_take(side);
    }
    if (su_compare(what, k)) return 1;
    if (memcmp(vw_world, vw_world_start + VW_WORLD_SIZE, VW_WORLD_SIZE)) {
        for (unsigned i = 0, shown = 0; i < VW_WORLD_SIZE && shown < 16u; ++i)
            if (vw_world[i] != vw_world_start[VW_WORLD_SIZE + i]) {
                fprintf(stderr, "  world +0x%04x original %02x own %02x\n", i, vw_world_start[VW_WORLD_SIZE + i], vw_world[i]);
                ++shown;
            }
        for (unsigned i = 0; i < vw_op_count; ++i)
            fprintf(stderr, "  op %u: 0x%08lx(0x%08lx, 0x%08lx, 0x%08lx, 0x%08lx) world at 0x%08lx\n", i, (unsigned long)vw_ops[i].va,
                    (unsigned long)vw_ops[i].a[0], (unsigned long)vw_ops[i].a[1], (unsigned long)vw_ops[i].a[2], (unsigned long)vw_ops[i].a[3],
                    (unsigned long)(uintptr_t)vw_world);
        fprintf(stderr, "FAIL: view %s case %u: the world differs\n", what, k);
        return 1;
    }
    for (unsigned i = 0; i < vw_op_count; ++i) {
        static const char *const names[3] = {"EAX", "EDX", "ECX"};
        for (unsigned j = 0; j < 3u; ++j) {
            if (!(vw_ops[i].mask & (1u << j)) || vw_res[0][i][j] == vw_res[1][i][j]) continue;
            fprintf(stderr, "FAIL: view %s case %u op %u (0x%08lx): %s original 0x%08lx own 0x%08lx\n", what, k, i, (unsigned long)vw_ops[i].va,
                    names[j], (unsigned long)vw_res[0][i][j], (unsigned long)vw_res[1][i][j]);
            return 1;
        }
    }
    return 0;
}

/* ---- the common world pieces */
static unsigned vw_lights_on, vw_light_count;
static void vw_world_lights(unsigned max) {
    vw_light_count = vw_rand(max + 1u);
    vw_lights_on = vw_rand(2u);
    vw_set8(0x006cbc04u, vw_lights_on);
    vw_set32(0x006c60a8u, vw_light_count);
    for (unsigned i = 0; i < 6u; ++i) vw_light(i, vw_rand(3u));
    for (unsigned i = 0; i < vw_light_count; ++i) vw_set32(0x006c60acu + 4u * i, vw_at(VW_LIGHTS + 0x40u * i));
}
static void vw_world_camera(void) {
    vw_vec(VW_CAM + 4u, 0x100000u);
    vw_wvec(VW_CAM + 0x10u, 0x8000u);
    vw_w32(VW_CAM + 0x16u, vw_at(VW_CONTEXT));
    vw_w32(VW_CAM + 0x1au, vw_rand(4u) ? vw_rand(16u) : 0u);
    vw_w32(VW_CAM + 0x1eu, vw_rand(2u));
    vw_w32(VW_CAM + 0x22u, 1u + vw_rand(0x1000u));
    for (unsigned i = 0; i < 4u; ++i) vw_w32(VW_CONTEXT + 0x210u + 4u * i, vw_rand(0x401u));
    vw_w32(VW_CONTEXT + 0x220u, vw_rand(0x201u));
    vw_w32(VW_CONTEXT + 0x224u, vw_rand(0x201u));
    vw_matrix(VW_M1);
    vw_matrix(VW_M2);
    vw_matrix(VW_M3);
    vw_set8(0x004a5b0cu, vw_rand(2u));
    vw_set32(0x006c5ff8u, vw_rand(2u) ? 0x006c5ffcu : vw_at(VW_M1));
    vw_set32(0x006c5ff4u, vw_rand(2u) ? 0x006c5ffcu : vw_at(VW_M2));
    vw_set32(0x006c6028u, vw_rand(16u));
    vw_set32(0x006c6030u, 1u + vw_rand(0x1000u));
    vw_set32(0x006c6034u, vw_signed(0x200u));
    vw_set32(0x006c6038u, vw_signed(0x200u));
}
static uint32_t vw_vector(unsigned i, uint32_t range) { vw_vec(VW_VEC + 0x10u * i, range); return vw_at(VW_VEC + 0x10u * i); }
static uint32_t vw_wvector(unsigned i, uint32_t range) { vw_wvec(VW_VEC + 0x10u * i, range); return vw_at(VW_VEC + 0x10u * i); }
/* A point for the projection: x and z random, the depth y nonzero. */
static uint32_t vw_depth_point(unsigned i) {
    uint32_t v = vw_vector(i, 0x8000u);
    vw_w32(VW_VEC + 0x10u * i + 4u, 1u + vw_rand(0x8000u));
    return v;
}
static void vw_world_table(void) {
    for (unsigned i = 0; i < 20u; ++i) vw_world_start[VW_T + 0x6cu + 0x23u * i] = 1;
    vw_w32(VW_T + 0x10u, 1u + vw_rand(0x20u));   /* the inner range stays >= 1: the level division */
    vw_w32(VW_T + 0x14u, 1u + vw_rand(0x20u));
    vw_set32(0x004a9608u, vw_at(VW_T));
}

/* ---- view */
static int vw_view_case(unsigned k) {
    vw_begin();
    vw_world_camera();
    vw_world_lights(5);
    uint32_t cam = vw_at(VW_CAM), p = vw_vector(0, 0x8000u), out = vw_at(VW_VEC + 0x10u), v = vw_depth_point(2), s = vw_at(VW_VEC + 0x30u),
             t = vw_at(VW_VEC + 0x40u), n = vw_wvector(5, 0x4000u), pp = vw_wvector(6, 0x4000u), pd = vw_vector(7, 0x8000u);
    unsigned light_eax = vw_lights_on && vw_light_count ? VW_EAX : 0u;
    random_bytes(vw_world_start + VW_P, 0x34u + 0x16u * 6u);
    vw_w32(VW_P + 0x20u, vw_rand(2u) ? 0x006c5ffcu : vw_at(VW_M1));   /* the saved pointers must point at matrices */
    vw_w32(VW_P + 0x24u, vw_rand(2u) ? 0x006c5ffcu : vw_at(VW_M2));
    OP1(0x0048c2e4u, VW_EAX, cam);
    OP2(0x0048c57cu, VW_EAX, p, out);
    OP2(0x0048c5fcu, VW_EAX, v, s);
    OP2(0x0048c6d0u, VW_EAX | VW_EDX, s, t);
    OP2(0x0048c5ccu, VW_EAX | VW_EDX, vw_signed(0x1000u), 1u + vw_rand(0x1000u));
    OP1(0x0048c444u, light_eax, vw_rand(3u) ? vw_at(VW_M1) : 0u);
    OP2(0x0048bfe8u, VW_EAX, n, pp);
    OP2(0x0048c16cu, VW_EAX, n, pd);
    OP1(0x0048c3b0u, VW_EAX, vw_rand(3u) ? vw_at(VW_M3) : 0u);
    OP2(0x0048c5fcu, VW_EAX, v, s);
    OP1(0x0048d7ecu, VW_EAX, vw_at(VW_P));
    OP2(0x0048c57cu, VW_EAX, p, out);
    OP1(0x0048c444u, light_eax, 0u);
    OP2(0x0048bfe8u, VW_EAX, n, pp);
    OP2(0x0048c16cu, VW_EAX, n, pd);
    return vw_run("view", k);
}

/* ---- table */
static unsigned vw_table_lights;
static int vw_table_case(unsigned k) {
    vw_begin();
    vw_world_table();
    uint32_t t = vw_at(VW_T);
    unsigned entries = 1u + vw_rand(5u), objects = 0;
    OP3(0x00406ee4u, VW_EAX | VW_EDX | VW_ECX, t, 0x40u + vw_rand(0x7c0u), 0x40u + vw_rand(0x7c0u));   /* ranges >= 2 after >> 5 */
    for (unsigned j = 0; j < entries; ++j) {
        vw_vec(VW_POS + 0x10u * j, 0x4000u);
        OP2(0x00406f38u, VW_EAX, t, vw_at(VW_POS + 0x10u * j));
        OP3(0x00407048u, VW_EAX | VW_EDX, t, j, vw_rand(0x200u));
    }
    unsigned rounds = 2u + vw_rand(5u);
    for (unsigned r = 0; r < rounds; ++r) {
        uint32_t o = VW_OBJ + 0x10u * objects++, j = vw_rand(entries);
        if (vw_rand(4u)) for (unsigned i = 0; i < 3u; ++i) vw_w32(o + 4u * i, get32(vw_world_start + VW_POS + 0x10u * j + 4u * i) + vw_signed(0x400u));
        else vw_vec(o, 0x20000u);
        vw_w32(o + 0xcu, vw_signed(0x10000u));
        OP2(0x00407098u, 0, t, vw_at(o));
        if (vw_rand(3u) == 0) {
            OP2(0x00406fbcu, VW_EAX, t, j);
            vw_vec(VW_POS + 0x10u * (entries + r), 0x4000u);
            OP2(0x00406f38u, VW_EAX, t, vw_at(VW_POS + 0x10u * (entries + r)));
        }
    }
    unsigned spot = OP1(0x00407388u, VW_EAX, t), point = OP1(0x00407444u, VW_EAX, t);
    /* The deleting destructors: EAX is `delete`'s (the C run-time's free,
     * no contract), so only the memory is compared. */
    unsigned d1 = OP2(0x004075c1u, 0, 0u, 1u);
    vw_ops[d1].from[0] = (uint8_t)(point + 1u);
    unsigned d2 = OP2(0x0048bcc4u, 0, 0u, 1u);
    vw_ops[d2].from[0] = (uint8_t)(spot + 1u);
    int bad = vw_run("table", k);
    if (!bad) for (unsigned i = 0; i < vw_op_count; ++i) if (vw_ops[i].va == 0x00407098u) vw_table_lights += get32(mapped + 0x2c60a8u) != 0;
    return bad;
}

/* ---- leaves */
static int vw_leaves_case(unsigned k) {
    vw_begin();
    vw_world_camera();
    vw_world_lights(4);
    vw_world_table();
    uint32_t t = vw_at(VW_T), cam = vw_at(VW_CAM);
    uint32_t scene = vw_rand(4u);
    vw_set16(0x00497ae4u, scene);
    for (unsigned i = 0; i < scene; ++i) vw_set32(0x004a960cu + 4u * i, vw_at(VW_LIGHTS + 0x40u * (2u + i)));
    for (unsigned i = 0; i < 16u; ++i) { vw_vec(VW_VEC + 0x10u * i, 0x8000u); if (vw_rand(2u)) vw_wvec(VW_VEC + 0x10u * i, 0x4000u); }
    vw_vec(VW_POS, 0x4000u);
    vw_w16(VW_LOBJ + 0xcu, vw_rand(3u));   /* the light-carrying object's entry index */
    OP2(0x00406f38u, VW_EAX, t, vw_at(VW_POS));
    OP3(0x00407048u, VW_EAX | VW_EDX, t, 0u, vw_rand(0x200u));
    unsigned point = OP1(0x00407444u, VW_EAX, t);
    for (unsigned i = 0; i < 12u; ++i) {
        uint32_t ia = vw_rand(16u), io = vw_rand(4u) ? (ia + 1u + vw_rand(15u)) % 16u : ia;   /* the forwarding rows' output aliases the input in a quarter */
        uint32_t l = vw_at(VW_LIGHTS + 0x40u * vw_rand(6u)), a = vw_at(VW_VEC + 0x10u * ia), b = vw_at(VW_VEC + 0x10u * vw_rand(16u)),
                 o = vw_at(VW_VEC + 0x10u * io), m = vw_at(VW_M1 + 0x40u * vw_rand(3u));
        unsigned op;
        switch (vw_rand(40u)) {
        case 0: OP2(0x0048d97au, VW_EAX, a, b); break;
        case 1: OP2(0x0047dd1au, VW_EAX, a, b); break;
        case 2: OP2(0x0048daa7u, VW_EAX, a, b); break;
        case 3: OP2(0x0047dce7u, VW_EAX, a, b); break;
        case 4: OP1(0x0048d98fu, VW_EAX, vw_rand(16u)); break;
        case 5: OP0(0x0048d99eu, VW_EAX); break;
        case 6: OP3(0x0048d9a7u, VW_EAX | VW_EDX | VW_ECX, a, random32(), random32()); break;
        case 7: OP4(0x0048db70u, VW_EAX, a, random32(), random32(), random32()); break;
        case 8: OP2(0x0048d9cfu, VW_EAX, a, b); break;
        case 9: OP2(0x0048dabcu, VW_EAX, a, b); break;
        case 10: OP2(0x0048dadcu, VW_EAX, a, b); break;
        case 11: OP2(0x0048dc15u, VW_EAX, a, b); break;
        case 12: OP2(0x0048dc2cu, VW_EAX, a, b); break;
        case 13: {
            static const uint32_t acc[] = {0x0048d9fdu, 0x0048da57u, 0x0048da62u, 0x0048da6du, 0x0048da7fu, 0x0048da8au, 0x0048da95u,
                                           0x0048dafcu, 0x0048db07u, 0x0048dbd6u, 0x00495157u, 0x00495162u, 0x0049516du};
            uint32_t va = acc[vw_rand(13u)];
            OP1(va, VW_EAX, va >= 0x00495162u ? cam : l);
            break;
        }
        case 14: OP2(0x0048da21u, VW_EAX, l, a); break;
        case 15: OP2(0x0048da3fu, VW_EAX, l, a); break;
        case 16: OP2(0x0048dbe1u, VW_EAX, l, a); break;
        case 17: OP3(0x0048bc70u, VW_EAX, vw_at(VW_SCRATCH), a, b); break;
        case 18: OP2(0x0048bcc4u, VW_EAX, vw_at(VW_SCRATCH), 0u); break;
        case 19: OP2(0x0048bd6cu, VW_EAX | VW_EDX | VW_ECX, l, vw_rand(0x400u)); break;
        case 20: OP2(0x0048bcfcu, VW_EAX, l, m); break;
        case 21: OP1(0x0048be98u, VW_EAX | VW_EDX | VW_ECX, vw_rand(4u) ? l : a); break;
        case 22: OP1(vw_rand(2u) ? 0x004951d6u : 0x00495201u, VW_EAX, a); break;
        case 23: OP2(0x00492a5eu, VW_EAX, a, b); break;
        case 24: OP1(0x00492a7du, VW_EAX, a); break;
        case 25: OP2(0x0047d734u, VW_EAX, a, b); break;
        case 26: OP2(0x00495178u, VW_EAX, cam, m); break;
        case 27: OP3(0x004951a7u, VW_EAX, m, a, b); break;
        case 28: OP2(0x004951ecu, VW_EAX, m, a); break;
        case 29: OP1(0x0048db12u, VW_EAX, m); break;
        case 30: OP3(0x0048db57u, VW_EAX, m, vw_at(VW_M1 + 0x40u * vw_rand(3u)), vw_at(VW_M3)); break;
        case 31: OP1(0x0048dc43u, VW_EAX, m); break;
        case 32: OP3(vw_rand(2u) ? 0x0048d9e4u : 0x0048db93u, VW_EAX, a, m, o); break;
        case 33: OP3(0x0048da08u, VW_EAX, a, m, o); break;
        case 34: OP3(0x00406ee4u, VW_EAX | VW_EDX | VW_ECX, t, vw_signed(0x800u), vw_signed(0x800u)); break;
        case 35: OP3(0x00407018u, VW_EAX, t, 0u, a); break;
        case 36: OP2(0x0040735cu, VW_EAX | VW_EDX | VW_ECX, t, t + 0x6cu); break;
        case 37: OP0(0x00407878u, 0); break;
        case 38: OP2(0x0040765cu, 0, vw_at(VW_LOBJ), 0u); break;   /* EAX: the counter's address (relocated here) */
        default:
            op = OP2(vw_rand(2u) ? 0x00407500u : 0x004075c1u, 0, t, 0u);
            if (vw_ops[op].va == 0x00407500u) vw_ops[op].from[1] = (uint8_t)(point + 1u);
            else { vw_ops[op].a[0] = 0u; vw_ops[op].from[0] = (uint8_t)(point + 1u); vw_ops[op].mask = VW_EAX; }
            break;
        }
    }
    OP2(0x0048c5ccu, VW_EAX | VW_EDX, vw_signed(0x1000u), 1u + vw_rand(0x1000u));
    OP2(0x0048c6d0u, VW_EAX | VW_EDX, vw_at(VW_VEC), vw_at(VW_VEC + 0x10u));
    return vw_run("leaves", k);
}

static int view_cases(void) {
    current_function = "view";
    es2_sim_identity_memory(&ir_memory);
    es2_sim_rtl_memory(&ir_memory, &su_rtl);
    su_data_pristine = malloc(SU_DATA_SIZE);
    su_snap[0] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    su_snap[1] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    ir_region = ww_alloc(IR_REGION_SIZE);
    lc_stack = VirtualAlloc(NULL, 0x10000u, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    lc_bits = ww_alloc(LC_BITS_SIZE);
    vw_world = VirtualAlloc(NULL, VW_WORLD_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    vw_world_start = malloc(2u * VW_WORLD_SIZE);
    if (!su_data_pristine || !su_snap[0] || !su_snap[1] || !ir_region || !lc_stack || !lc_bits || !vw_world || !vw_world_start)
        return fail("view: memory");
    if (get32(mapped + (SU_COUNTER - 0x400000u))) return fail("view: the counter word is not free");
    su_pristine_take();
    static const struct { const char *dll, *name; void *stub; } stubs[] = {
        {"cw3220.dll", "_calloc", (void *)ir_calloc}, {"cw3220.dll", "_free", (void *)ir_free},
        {"cw3220.dll", "_memset", (void *)ir_memset}, {"cw3220.dll", "_memmove", (void *)ir_memmove},
    };
    for (unsigned i = 0; i < sizeof(stubs) / sizeof(stubs[0]); ++i)
        if (!ir_patch_import(stubs[i].dll, stubs[i].name, stubs[i].stub)) {
            fprintf(stderr, "import %s!%s\n", stubs[i].dll, stubs[i].name);
            return fail("view: import slot not found");
        }
    if (vw_dispatch_setup()) return 1;
    int bad = 0;
    static const struct { const char *name; int (*run)(unsigned); unsigned cases; } parts[] = {
        {"view", vw_view_case, 1500}, {"table", vw_table_case, 1500}, {"leaves", vw_leaves_case, 2000},
    };
    for (unsigned p = 0; !bad && p < sizeof(parts) / sizeof(parts[0]); ++p) {
        for (unsigned k = 0; !bad && k < parts[p].cases; ++k) {
            current_case = k; current_case_valid = 1;
            bad = parts[p].run(k);
        }
        if (!bad) printf("PASS: view %s: %u cases identical\n", parts[p].name, parts[p].cases);
    }
    if (!bad) printf("VIEW coverage: %u per-object updates left a light active\n", vw_table_lights);
    current_case_valid = 0;
    put32(mapped + 0x2cbee4u, 0);
    memcpy(mapped + SU_DATA_RVA, su_data_pristine, SU_DATA_SIZE);
    return bad;
}
