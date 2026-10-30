/* The DTS shape draw slots, original machine code against sim/src/shape_draw.c
 * (--shape-draw; target :differential_shape_draw_test). Included after
 * gfx_cases.h; reuses startup_cases.h's DATA reset and snapshots (su_reset,
 * su_take, su_compare), loop_cases.h's loop set-up (lc_reset) and
 * audio_cases.h's register-capturing machine call (au_call) and passed-row
 * function (au_original: the registry hooks the module and view_transform.h's rows call
 * run as the original's machine code on the own side).
 *
 * Every case builds a random synthetic shape world in an identity-mapped
 * buffer (part trees of part lists, polygon groups, cell animations,
 * detail parts, BSP parts and leaf parts; solid, shaded and Gouraud
 * polygons; transform tables, instances, actors with detail tables, lights)
 * and presets the module's DATA words, then runs the same sequence of calls
 * on both sides: side 0 the original's code, side 1 the own rows through a
 * dispatch of the sim's real services plus the passed hook rows.
 *
 * The layers below the module are recording stand-ins of this harness on
 * both sides (side 0: a jump patched over the original's entry; side 1: a
 * service row at the same VA): the fill layer 0x484222/0x483eb8/0x4842bb
 * (each call logs the kind, the count, the colours 0x6c6050/0x6c6058, the
 * mode 0x6c6054, the points and, in mode 1, the intensity words), the haze
 * table 0x468158 and the shade lookup 0x430e34, and the leaf parts' draw
 * slot (logs the part, the current transform 0x6bb2e4, the view matrix's
 * first words, the shape record 0x6c6064 and the clip flag). The log lives
 * in the world, so the slot order, the detail choices and every fill call
 * are compared with the world.
 *
 * Compared after the sequence: the world (with the log), DATA and EAX of
 * every call (except the Gouraud polygon's and where the original leaves
 * no contract). Stack addresses (the original's locals on the host stack,
 * the own code's in the call area) are replaced by one marker before the
 * comparison: the object matrix pointer 0x6c5ff8 and the saved views'
 * pointers (a part's or instance's matrix is a stack local), the Gouraud
 * intensity pointer 0x6c6060, and EAX values (0x48c444 returns a local).
 *
 * - draw: instance draws 0x478da4, shape draws 0x490c1c/0x490ab8, the actor
 *   draws 0x46b8ea/0x4033e4/0x4034f4/0x401fe4 over random part trees, with
 *   the detail test on or off, a perspective shift or the divisor, lights.
 * - polygons: the polygon slots 0x474ec0/0x475538/0x4756d4 on a preset
 *   render context (point states 0, 1, 2 with consistent depths), the
 *   back-face tests, the projections, the clips 0x48cf20/0x48d38c, the fill
 *   dispatch and its forwards.
 * - leaves: the part transforms 0x4760e4/0x476120/0x47613c, the saved view
 *   0x48d718, the detail test 0x476248, the part slots called directly,
 *   0x476b28/0x4911c0, the setters and the detail level tables. */
#pragma once
/* The headers this suite builds on (sim_main.c). */
#include "init_records_cases.h"
#include "startup_cases.h"
#include "loop_cases.h"
#include "audio_cases.h"
#include "scene_cases.h"
#include "es2/sim/load.h"
#include "es2/sim/view.h"
#include "es2/sim/shape_draw.h"

#define SD_WORLD_SIZE 0x10000u
/* World layout (offsets). */
#define SD_CTX 0x0000u      /* the graphics context (0x6c5f70) */
#define SD_VT 0x0300u       /* vtables of 0x20 bytes (slot +0x1c) */
#define SD_MATS 0x0500u     /* 8 matrices of 0x20: the transform table / object matrices */
#define SD_M1 0x0600u       /* an object matrix preset into 0x6c5ff8 */
#define SD_M2 0x0620u       /* a view matrix preset into 0x6c5ff4 */
#define SD_POINTS 0x0700u   /* 3 point sets of 0x120 (48 points of three words) */
#define SD_INDEX 0x0a80u    /* 3 index lists of 0x100 (128 words) */
#define SD_COLOURS 0x0d80u  /* 3 colour tables of 0x80 (8 entries of four dwords) */
#define SD_POLYS 0x1000u    /* 64 polygons of 0x10 */
#define SD_PTRS 0x1400u     /* pointer arrays (0x400) */
#define SD_PARTS 0x1800u    /* 40 parts of 0x30 */
#define SD_INST 0x1f80u     /* 4 instances of 0x20 */
#define SD_CELLS 0x2000u    /* cell words */
#define SD_THR 0x2040u      /* threshold words of the detail parts */
#define SD_BSP 0x2100u      /* BSP node records (14 bytes) */
#define SD_BSPT 0x2200u     /* BSP transform words */
#define SD_LIGHTS 0x2240u   /* 4 lights of 0x40 */
#define SD_ACTORS 0x2340u   /* 2 actors of 0x50 */
#define SD_DTAB 0x23e0u     /* 2 detail tables of 0x10 */
#define SD_LSHAPES 0x2400u  /* level shape pointer arrays */
#define SD_LTHR 0x2440u     /* level threshold dwords */
#define SD_HAZE 0x2500u     /* the haze table stand-in (0x300 bytes) */
#define SD_FRAME 0x2800u    /* a frame for direct 0x476120/0x47613c calls */
#define SD_SAVE 0x2840u     /* a record for direct 0x48d718 calls (0x110) */
#define SD_BUF 0x2980u      /* clip buffers: intensities in (0x80), out (0x100) */
#define SD_VEC 0x2b00u      /* vectors */
#define SD_LOG 0x3000u      /* the log: count dword, then the entries */
#define SD_LOG_CAP ((SD_WORLD_SIZE - SD_LOG - 4u) / 4u)
#define SD_STACK_MARK 0x57ac57acu

static BYTE *sd_world, *sd_world_start;   /* start + the kept side-0 result */
static es2_sim_entry sd_entries[4096];
static es2_sim_dispatch sd_dispatch;
static es2_sim_load sd_load;
static es2_sim_view sd_view;
static es2_sim_shape_draw sd_draw;
static int sd_bad;
static unsigned sd_side;
static uint32_t sd_shift;   /* the case's perspective shift (0: the divisor) */
static void sd_refuse(const es2_sim_refusal *r, void *user) {
    (void)user;
    fprintf(stderr, "FAIL: own shape draw: refused call to 0x%08lx (%s): %s\n", (unsigned long)r->va, r->name ? r->name : "?",
            r->reason ? r->reason : "");
    sd_bad = 1;
}
static uint32_t sd_at(uint32_t offset) { return (uint32_t)(uintptr_t)sd_world + offset; }
static uint32_t sd_g32(uint32_t a) { return get32((const BYTE *)(uintptr_t)a); }

/* ---- the recording stand-ins (both sides) */
static void sd_log(uint32_t v) {
    BYTE *log = sd_world + SD_LOG;
    uint32_t n = get32(log);
    if (n < SD_LOG_CAP) put32(log + 4u + 4u * n, v);
    put32(log, n + 1u);
}
static int sd_is_stack(unsigned side, uint32_t v);
/* The textures part logs the mode record; coverage counters (side 1). */
static int sd_tex_log;
static unsigned sd_tex_fills, sd_fills3d, sd_blits;
static uint32_t sd_fill(uint32_t kind, uint32_t n, uint32_t pts) {
    sd_log(kind);
    sd_log(n);
    uint32_t mode = get32(mapped + 0x2c6054u);
    sd_log(get32(mapped + 0x2c6050u));
    sd_log(mode);
    sd_log(get32(mapped + 0x2c6058u));
    for (uint32_t i = 0; i < 2u * n && i < 256u; ++i) sd_log(sd_g32(pts + 4u * i));
    if (mode == 1u) {
        /* The intensities only when 0x6c6060 points at a live array (a
         * Gouraud polygon's locals or the world); a solid or shaded fill
         * under a preset mode 1 leaves it on an earlier case's value. */
        uint32_t in = get32(mapped + 0x2c6060u);
        int live = sd_is_stack(sd_side, in) || (in >= sd_at(0) && in < sd_at(SD_LOG));
        /* a textured polygon's fill reads its mode from the stack record at the
         * context's +0x22c (7), not 0x6c6054; a 1 left there by an earlier
         * Gouraud polygon points at its dead locals. */
        if (sd_tex_log && sd_is_stack(sd_side, sd_g32(get32(mapped + 0x2c5f70u) + 0x22cu))) live = 2;
        sd_log(live);
        if (live == 1)
            for (uint32_t i = 0; i < n && i < 128u; ++i) sd_log(get16((const BYTE *)(uintptr_t)(in + 2u * i)));
    }
    if (sd_tex_log) {
        /* The context's mode record +0x22c, a stack address while a textured
         * polygon draws through the fill layer: {7, count, UV, texture, shade}
         * and the UV pairs. */
        uint32_t rec = sd_g32(get32(mapped + 0x2c5f70u) + 0x22cu);
        int live = sd_is_stack(sd_side, rec);
        sd_log(0x7eu);
        sd_log(live);
        if (live) {
            for (uint32_t i = 0; i < 5u; ++i) sd_log(sd_g32(rec + 4u * i));
            uint32_t uv = sd_g32(rec + 8u), count = sd_g32(rec + 4u);
            for (uint32_t i = 0; i < 2u * count && i < 64u; ++i) sd_log(sd_g32(uv + 4u * i));
            if (sd_side) ++sd_tex_fills;
        }
    }
    return 0x7000u + (kind << 8) + (n & 0xffu);
}
/* The 3D texture fills 0x46817c/0x468760 (records, UV, count, a3, the light
 * levels' address, mode, a6, a7) and the rotated blit 0x48185c (bitmap, point,
 * a2, the two sizes, angle). */
static uint32_t sd_fill3d(uint32_t kind, const uint32_t *a) {
    sd_log(kind);
    uint32_t n = a[2];
    for (unsigned i = 0; i < 8u; ++i) if (i != 4u) sd_log(a[i]);
    for (uint32_t i = 0; i < 3u * n && i < 48u; ++i) sd_log(sd_g32(a[0] + 4u * i));
    for (uint32_t i = 0; i < 2u * n && i < 32u; ++i) sd_log(sd_g32(a[1] + 4u * i));
    /* The levels only where the original wrote them (with lights; mode 2: four). */
    if (get32(mapped + 0x0a5b04u)) for (uint32_t i = 0; i < (a[5] == 2u ? 4u : 1u); ++i) sd_log(sd_g32(a[4] + 4u * i));
    if (sd_side) ++sd_fills3d;
    return 0x7400u + (kind << 8) + (n & 0xffu);
}
static uint32_t __cdecl sd_fill_3d(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7) {
    const uint32_t a[8] = {a0, a1, a2, a3, a4, a5, a6, a7};
    return sd_fill3d(0xf4u, a);
}
static uint32_t __cdecl sd_fill_persp(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7) {
    const uint32_t a[8] = {a0, a1, a2, a3, a4, a5, a6, a7};
    return sd_fill3d(0xf5u, a);
}
static uint32_t __cdecl sd_blit(uint32_t bitmap, uint32_t point, uint32_t a2, uint32_t sizes, uint32_t angle) {
    sd_log(0xb1u);
    sd_log(bitmap);
    sd_log(sd_g32(point));
    sd_log(sd_g32(point + 4u));
    sd_log(a2);
    sd_log(sd_g32(sizes));
    sd_log(sd_g32(sizes + 4u));
    sd_log(angle & 0xffffu);
    if (sd_side) ++sd_blits;
    return 0x7b00u + (bitmap & 0xffu);
}
static uint32_t __cdecl sd_fill_back(uint32_t n, uint32_t pts) { return sd_fill(0xf1u, n, pts); }
static uint32_t __cdecl sd_fill_flat(uint32_t n, uint32_t pts) { return sd_fill(0xf2u, n, pts); }
static uint32_t __cdecl sd_fill_plain(uint32_t n, uint32_t pts) { return sd_fill(0xf3u, n, pts); }
static uint32_t __cdecl sd_haze(uint32_t level) { sd_log(0x4eu); sd_log(level); return sd_at(SD_HAZE); }
static uint32_t __cdecl sd_shade(uint32_t colour, uint32_t level) {
    sd_log(0x53u);
    sd_log(colour);
    sd_log(level);
    return (colour + 7u * level) & 0x1ffu;
}
static uint32_t __cdecl sd_leaf(uint32_t part) {
    sd_log(0x4cu);
    sd_log(part);
    sd_log(get32(mapped + 0x2bb2e4u));
    sd_log(get32(mapped + 0x2c5fb4u));
    sd_log(get32(mapped + 0x2c5fb8u));
    sd_log(get32(mapped + 0x2c6064u));
    sd_log(sd_g32(get32(mapped + 0x2c5f70u) + 0x208u));
    return part + 0x100u;
}
static void sd_s_fill(es2_sim_service_call *c, void *u) {
    (void)u;
    uint32_t kind = c->va == 0x00484222u ? 0xf1u : c->va == 0x00483eb8u ? 0xf2u : 0xf3u;
    c->eax = sd_fill(kind, c->slots[0], c->slots[1]);
}
static void sd_s_haze(es2_sim_service_call *c, void *u) { (void)u; c->eax = sd_haze(c->slots[0]); }
static void sd_s_shade(es2_sim_service_call *c, void *u) { (void)u; c->eax = sd_shade(c->slots[0], c->slots[1]); }
static void sd_s_leaf(es2_sim_service_call *c, void *u) { (void)u; c->eax = sd_leaf(c->slots[0]); }
static void sd_s_fill3d(es2_sim_service_call *c, void *u) { (void)u; c->eax = sd_fill3d(c->va == 0x0046817cu ? 0xf4u : 0xf5u, c->slots); }
static void sd_s_blit(es2_sim_service_call *c, void *u) { (void)u; c->eax = sd_blit(c->slots[0], c->slots[1], c->slots[2], c->slots[3], c->slots[4]); }
static const struct { uint32_t va; void *stub; es2_sim_service_fn service; unsigned slots; } sd_stubs[] = {
    {0x00484222u, (void *)sd_fill_back, sd_s_fill, 2}, {0x00483eb8u, (void *)sd_fill_flat, sd_s_fill, 2},
    {0x004842bbu, (void *)sd_fill_plain, sd_s_fill, 2}, {0x00468158u, (void *)sd_haze, sd_s_haze, 1},
    {0x00430e34u, (void *)sd_shade, sd_s_shade, 2},
    {0x0046817cu, (void *)sd_fill_3d, sd_s_fill3d, 8}, {0x00468760u, (void *)sd_fill_persp, sd_s_fill3d, 8},
    {0x0048185cu, (void *)sd_blit, sd_s_blit, 5},
};
static BYTE sd_saved[sizeof(sd_stubs) / sizeof(sd_stubs[0])][5];
static int sd_patch(uint32_t va, void *target, BYTE saved[5], int install) {
    BYTE *site = mapped + (va - 0x400000u);
    DWORD previous;
    if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &previous)) return fail("shape draw: stub protection");
    if (install) { memcpy(saved, site, 5); site[0] = 0xe9; put32(site + 1, (uint32_t)(uintptr_t)target - (uint32_t)(uintptr_t)(site + 5)); }
    else memcpy(site, saved, 5);
    if (!VirtualProtect(site, 5, previous, &previous)) return fail("shape draw: stub restore protection");
    FlushInstructionCache(GetCurrentProcess(), site, 5);
    return 0;
}

/* Whether a services.h row exists at va (the dispatch's lookup works only after the seal). */
static int sd_has_row(uint32_t va) {
    for (unsigned i = 0; i < es2_sim_service_count(); ++i) {
        uint32_t row = 0;
        es2_sim_service_row(i, &row);
        if (row == va) return 1;
    }
    return 0;
}
static int sd_dispatch_setup(void) {
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
        /* The sprite part's hooks */
        {0x0047d694u, 2, "distance_display (original)"}, {0x0047e0a0u, 2, "multiply_q8 (original)"},
        {0x0047eb30u, 2, "sincos_pair (original)"}, {0x0047ffe8u, 3, "rotate_2d (original)"},
    };
    if (es2_sim_register_services(&sd_dispatch, &s)) return fail("shape draw: dispatch");
    for (unsigned i = 0; i < sizeof(passed) / sizeof(passed[0]); ++i)
        if (es2_sim_dispatch_add_service(&sd_dispatch, passed[i].va, passed[i].name, au_original, (void *)(uintptr_t)passed[i].slots, 0,
                                         (uint8_t)passed[i].slots))
            return fail("shape draw: dispatch");
    /* A stand-in whose VA another row already serves (poly_fills.h's
     * 0x468158) replaces that row after the seal instead. */
    int deferred[sizeof(sd_stubs) / sizeof(sd_stubs[0])] = {0};
    for (unsigned i = 0; i < sizeof(sd_stubs) / sizeof(sd_stubs[0]); ++i) {
        if (sd_has_row(sd_stubs[i].va)) { deferred[i] = 1; continue; }
        if (es2_sim_dispatch_add_service(&sd_dispatch, sd_stubs[i].va, "stand-in", sd_stubs[i].service, NULL, 0, (uint8_t)sd_stubs[i].slots))
            return fail("shape draw: dispatch");
    }
    uint32_t leaf = (uint32_t)(uintptr_t)sd_leaf;
    if (sd_has_row(leaf)) return fail("shape draw: the leaf stand-in's address has a row");
    if (es2_sim_dispatch_add_service(&sd_dispatch, leaf, "leaf part (stand-in)", sd_s_leaf, NULL, 0, 1)) return fail("shape draw: dispatch");
    const char *error = NULL;
    uint32_t bad = 0;
    if (es2_sim_dispatch_seal(&sd_dispatch, &error, &bad)) {
        fprintf(stderr, "seal: %s 0x%08lx\n", error ? error : "?", (unsigned long)bad);
        return fail("shape draw: dispatch seal");
    }
    for (unsigned i = 0; i < sizeof(sd_stubs) / sizeof(sd_stubs[0]); ++i) {
        if (!deferred[i]) continue;
        es2_sim_entry *e = (es2_sim_entry *)es2_sim_dispatch_find(&sd_dispatch, sd_stubs[i].va);
        e->service = sd_stubs[i].service;
        e->service_user = NULL;
    }
    /* vector_new 0x49667c (input_layer.h's row needs the input layer) runs as the
     * original's machine code through scene_cases.h's sc_original (five
     * arguments; au_original takes four). */
    es2_sim_entry *vn = (es2_sim_entry *)es2_sim_dispatch_find(&sd_dispatch, 0x0049667cu);
    if (!vn || vn->kind != ES2_SIM_SERVICE) return fail("shape draw: vector_new missing");
    vn->service = sc_original;
    vn->service_user = (void *)(uintptr_t)5u;
    return 0;
}

/* ---- the case script: DATA presets, the world, the calls */
typedef struct sd_preset { uint32_t va, value; unsigned width; } sd_preset;
static sd_preset sd_presets[640];
static unsigned sd_preset_count;
static void sd_preset_room(void) { if (sd_preset_count >= sizeof(sd_presets) / sizeof(sd_presets[0])) { fprintf(stderr, "FAIL: shape draw: too many presets\n"); exit(1); } }
static void sd_set32(uint32_t va, uint32_t v) { sd_preset_room(); sd_presets[sd_preset_count++] = (sd_preset){va, v, 4}; }
static void sd_set16(uint32_t va, uint32_t v) { sd_preset_room(); sd_presets[sd_preset_count++] = (sd_preset){va, v, 2}; }
static void sd_set8(uint32_t va, uint32_t v) { sd_preset_room(); sd_presets[sd_preset_count++] = (sd_preset){va, v, 1}; }
typedef struct sd_op { uint32_t va, a[4]; unsigned n, mask; } sd_op;
static sd_op sd_ops[64];
static unsigned sd_op_count;
static uint32_t sd_res[2][64][3];
/* EAX lay in that side's stack (a local's address): such values compare
 * equal to each other; the raw values are kept, since a number may fall
 * into the other side's stack range by chance. */
static unsigned char sd_res_stack[2][64];
#define SD_EAX 1u
#define SD_EDX 2u
#define SD_ECX 4u
#define SD_AX 8u   /* only the low word of EAX */
static unsigned sd_op_add(uint32_t va, unsigned mask, unsigned n, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3) {
    sd_op *o = &sd_ops[sd_op_count];
    memset(o, 0, sizeof(*o));
    o->va = va; o->mask = mask; o->n = n;
    o->a[0] = a0; o->a[1] = a1; o->a[2] = a2; o->a[3] = a3;
    return sd_op_count++;
}
#define SDOP0(va, mask) sd_op_add((va), (mask), 0, 0, 0, 0, 0)
#define SDOP1(va, mask, a) sd_op_add((va), (mask), 1, (a), 0, 0, 0)
#define SDOP2(va, mask, a, b) sd_op_add((va), (mask), 2, (a), (b), 0, 0)
#define SDOP3(va, mask, a, b, c) sd_op_add((va), (mask), 3, (a), (b), (c), 0)
static uint32_t sd_rand(uint32_t n) { return random32() % n; }
static uint32_t sd_signed(uint32_t range) { return (uint32_t)((int32_t)sd_rand(2u * range + 1u) - (int32_t)range); }
static void sd_w32(uint32_t offset, uint32_t v) { put32(sd_world_start + offset, v); }
static void sd_w16(uint32_t offset, uint32_t v) { put16(sd_world_start + offset, (uint16_t)v); }
static void sd_w8(uint32_t offset, uint32_t v) { sd_world_start[offset] = (BYTE)v; }
static uint32_t sd_r32(uint32_t offset) { return get32(sd_world_start + offset); }
static void sd_vec(uint32_t offset, uint32_t range) { for (unsigned i = 0; i < 3u; ++i) sd_w32(offset + 4u * i, sd_signed(range)); }
static void sd_wvec(uint32_t offset, uint32_t range) { for (unsigned i = 0; i < 3u; ++i) sd_w16(offset + 2u * i, sd_signed(range)); }
static void sd_matrix(uint32_t offset) {
    for (unsigned i = 0; i < 9u; ++i) sd_w16(offset + 2u * i, sd_signed(0x4000u));
    sd_w16(offset + 0x12u, sd_rand(0x10000u));
    sd_vec(offset + 0x14u, 0x4000u);
}

/* sd_is_stack classifies by value against a different range on each side,
 * so a generated data word that reaches it must lie in neither range: the
 * context word +0x22c, which the fill stubs classify while no polygon has
 * replaced it, keeps bit 31 set (sd_world_base). On the original's side a
 * classification of that word inside the worker stack's reservation
 * [sd_stack_lo, sd_stack_hi) or the call area fails the case. */
static uint32_t sd_stack_lo, sd_stack_hi, sd_alias_value;
static int sd_alias_hit;
static void sd_alias(uint32_t v) {
    if (sd_alias_hit || !sd_world_start || v != get32(sd_world_start + SD_CTX + 0x22cu)) return;
    if (v - sd_stack_lo < sd_stack_hi - sd_stack_lo || v - (uint32_t)(uintptr_t)lc_stack < 0x10000u) { sd_alias_hit = 1; sd_alias_value = v; }
}
static int sd_alias_report(const char *what, unsigned k) {
    if (!sd_alias_hit) return 0;
    fprintf(stderr, "FAIL: %s case %u: the generated context word %08lx lies in a stack range (worker %08lx..%08lx, call area %08lx)\n",
            what, k, (unsigned long)sd_alias_value, (unsigned long)sd_stack_lo, (unsigned long)sd_stack_hi, (unsigned long)(uintptr_t)lc_stack);
    return 1;
}
/* The stack ranges: side 0 the host thread's stack, side 1 the call area. */
static int sd_is_stack(unsigned side, uint32_t v) {
    if (side) return v >= (uint32_t)(uintptr_t)lc_stack && v < (uint32_t)(uintptr_t)lc_stack + 0x10000u;
    sd_alias(v);
    NT_TIB *tib = (NT_TIB *)NtCurrentTeb();
    return v >= (uint32_t)(uintptr_t)tib->StackLimit && v < (uint32_t)(uintptr_t)tib->StackBase;
}
static uint32_t sd_norm(unsigned side, uint32_t v) { return sd_is_stack(side, v) ? SD_STACK_MARK : v; }
static void sd_norm_data(unsigned side) {
    static const uint32_t words[] = {0x006c6060u, 0x006c5ff8u, 0x006c5ff4u};
    for (unsigned i = 0; i < sizeof(words) / sizeof(words[0]); ++i)
        put32(mapped + (words[i] - 0x400000u), sd_norm(side, get32(mapped + (words[i] - 0x400000u))));
    for (uint32_t k = 0; k < 0x32u; ++k) {
        for (uint32_t off = 0x20u; off <= 0x24u; off += 4u) {
            BYTE *p = mapped + (0x006b7b70u + 0x110u * k + off - 0x400000u);
            put32(p, sd_norm(side, get32(p)));
        }
        /* A matrix's byte +0x13 is padding: matrix_compose writes only the
         * flag byte +0x12, so a matrix composed into a stack local (0x478d60
         * from 0x478da4, 0x47613c into its caller's frame) keeps the
         * original's uninitialised stack byte there, and 0x48c444 copies it
         * into the view matrix and 0x48d718 into the saved views. The own
         * code's locals start zeroed. */
        mapped[0x006b7b70u + 0x110u * k + 0x13u - 0x400000u] = 0;
    }
    mapped[0x006c5fc7u - 0x400000u] = 0;
    /* 0x48d7ec restores a saved view into 0x6c5fb4 and copies it on into
     * the inverse 0x6c5fd4 with the same padding byte. */
    mapped[0x006c5fe7u - 0x400000u] = 0;
    /* 0x48d718's record in the world: the object and view pointers. */
    for (uint32_t off = 0x20u; off <= 0x24u; off += 4u) put32(sd_world + SD_SAVE + off, sd_norm(side, get32(sd_world + SD_SAVE + off)));
    sd_world[SD_SAVE + 0x13u] = 0;
}

static void sd_begin(void) {
    memset(sd_world_start, 0, SD_WORLD_SIZE);
    sd_preset_count = 0;
    sd_op_count = 0;
}
static void sd_side_reset(unsigned side) {
    if (!sd_stack_hi) {
        const stack_layout s = current_stack();
        sd_stack_lo = (uint32_t)(uintptr_t)s.allocation;
        sd_stack_hi = (uint32_t)(uintptr_t)s.base;
    }
    if (!side) sd_alias_hit = 0;
    lc_reset(side, 47, 0x01000000u, 1);
    lc_loop.dispatch = &sd_dispatch;
    es2_sim_load_headless(&sd_load, &lc_loop);
    es2_sim_load_bind(&sd_load);
    memset(&sd_view, 0, sizeof(sd_view));
    sd_view.loop = &lc_loop;
    es2_sim_view_bind(&sd_view);
    memset(&sd_draw, 0, sizeof(sd_draw));
    sd_draw.loop = &lc_loop;
    es2_sim_shape_draw_bind(&sd_draw);
    memcpy(sd_world, sd_world_start, SD_WORLD_SIZE);
    for (unsigned i = 0; i < sd_preset_count; ++i) {
        BYTE *p = mapped + (sd_presets[i].va - 0x400000u);
        if (sd_presets[i].width == 4) put32(p, sd_presets[i].value);
        else if (sd_presets[i].width == 2) put16(p, (uint16_t)sd_presets[i].value);
        else *p = (BYTE)sd_presets[i].value;
    }
}
static void sd_dump(const char *what, unsigned k) {
    for (unsigned i = 0; i < sd_op_count; ++i)
        fprintf(stderr, "  op %u: 0x%08lx(0x%08lx, 0x%08lx, 0x%08lx) world at 0x%08lx\n", i, (unsigned long)sd_ops[i].va,
                (unsigned long)sd_ops[i].a[0], (unsigned long)sd_ops[i].a[1], (unsigned long)sd_ops[i].a[2], (unsigned long)(uintptr_t)sd_world);
    fprintf(stderr, "FAIL: shape draw %s case %u\n", what, k);
}
static unsigned sd_fills, sd_leaves_logged, sd_max_log;
static int sd_run(const char *what, unsigned k) {
    for (unsigned side = 0; side < 2u; ++side) {
        sd_side = side;
        sd_side_reset(side);
        uint32_t object = 0;
        for (unsigned i = 0; i < sd_preset_count; ++i) if (sd_presets[i].va == 0x006c5ff8u) object = sd_presets[i].value;
        for (unsigned i = 0; i < sd_op_count; ++i) {
            sd_op *o = &sd_ops[i];
            uint32_t in[3] = {0, 0, 0}, *out = sd_res[side][i];
            /* A shape draw called directly (not through 0x478da4, whose pop
             * selects the previous matrix again) whose last part selected a
             * transform leaves the object matrix pointer on that part's dead
             * stack local; the next call would read the dead stack (garbage
             * in the original, different from the own call area). The case
             * selects the preset matrix again between calls, on both sides. */
            if (sd_is_stack(side, get32(mapped + 0x2c5ff8u))) put32(mapped + 0x2c5ff8u, object);
            /* The same for the fill layer's intensity pointer 0x6c6060: a
             * Gouraud polygon leaves it on its dead locals; a direct fill
             * after it reads the world's intensity buffer instead. */
            if (sd_is_stack(side, get32(mapped + 0x2c6060u))) put32(mapped + 0x2c6060u, sd_at(SD_BUF));
            if (!side) au_call((uint32_t)(uintptr_t)(mapped + (o->va - 0x400000u)), o->a, o->n, in, out);
            else {
                es2_sim_result r;
                memset(&r, 0, sizeof(r));
                if (es2_sim_call(&sd_dispatch, o->va, o->a, o->n, in, &r)) sd_bad = 1;
                out[0] = r.eax; out[1] = r.edx; out[2] = r.ecx;
            }
            sd_res_stack[side][i] = (unsigned char)sd_is_stack(side, out[0]);
            if (sd_bad) { fprintf(stderr, "FAIL: shape draw %s case %u op %u (0x%08lx): a call failed\n", what, k, i, (unsigned long)o->va); return 1; }
        }
        sd_norm_data(side);
        if (!side) memcpy(sd_world_start + SD_WORLD_SIZE, sd_world, SD_WORLD_SIZE);
        su_take(side);
    }
    if (sd_alias_report(what, k)) return 1;
    if (su_compare(what, k)) { sd_dump(what, k); return 1; }
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
    if (get32(sd_world + SD_LOG) > SD_LOG_CAP) { fprintf(stderr, "FAIL: shape draw %s case %u: the log overflows\n", what, k); return 1; }
    for (unsigned i = 0; i < sd_op_count; ++i) {
        static const char *const names[3] = {"EAX", "EDX", "ECX"};
        for (unsigned j = 0; j < 4u; ++j) {
            if (!(sd_ops[i].mask & (1u << j))) continue;
            unsigned r = j == 3u ? 0u : j;
            uint32_t m = j == 3u ? 0xffffu : 0xffffffffu;
            if (((sd_res[0][i][r] ^ sd_res[1][i][r]) & m) == 0) continue;
            if (r == 0u && sd_res_stack[0][i] && sd_res_stack[1][i]) continue;
            fprintf(stderr, "FAIL: shape draw %s case %u op %u (0x%08lx): %s original 0x%08lx own 0x%08lx\n", what, k, i, (unsigned long)sd_ops[i].va,
                    j == 3u ? "AX" : names[j], (unsigned long)sd_res[0][i][r], (unsigned long)sd_res[1][i][r]);
            sd_dump(what, k);
            return 1;
        }
    }
    /* Coverage counters over the log. */
    uint32_t n = get32(sd_world + SD_LOG);
    if (n > sd_max_log) sd_max_log = n;
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t v = get32(sd_world + SD_LOG + 4u + 4u * i);
        if (v == 0xf1u || v == 0xf2u || v == 0xf3u) ++sd_fills;
        if (v == 0x4cu) ++sd_leaves_logged;
    }
    return 0;
}

/* ---- the world: vtables, context, camera, lights */
/* Six more kinds (16 vtables fill 0x300..0x4ff). */
enum { SD_K_SOLID, SD_K_SHADED, SD_K_GOURAUD, SD_K_LIST, SD_K_GROUP, SD_K_CELLS, SD_K_DETAIL, SD_K_BSP, SD_K_LEAF, SD_K_INSTANCE,
       SD_K_PLAIN, SD_K_LIT, SD_K_GPLAIN, SD_K_TEXTURED, SD_K_GROUP_BSP, SD_K_SPRITE, SD_KINDS };
static uint32_t sd_vt(unsigned kind) { return sd_at(SD_VT + 0x20u * kind); }
static void sd_world_base(void) {
    static const uint32_t slots[SD_KINDS] = {0x00474ec0u, 0x00475538u, 0x004756d4u, 0x00476808u, 0x004759d4u,
                                             0x004768f0u, 0x004769c8u, 0x00476c18u, 0u, 0x00478da4u,
                                             0x00475cd0u, 0x00475d8cu, 0x00475eecu, 0x00474fa8u, 0x00475c04u, 0x004763f4u};
    for (unsigned i = 0; i < SD_KINDS; ++i) {
        for (uint32_t j = 0; j < 0x20u; j += 4u) sd_w32(SD_VT + 0x20u * i + j, random32());
        sd_w32(SD_VT + 0x20u * i + 0x1cu, slots[i] ? slots[i] : (uint32_t)(uintptr_t)sd_leaf);
    }
    /* The graphics context: a viewport, the clip flag, the fill fields. */
    /* The detail test's width [+0x210] - [+0x220] (or [+0x218] + [+0x220]
     * when negative) stays in 1..0x100: angle_ratio divides by it. */
    uint32_t x0 = 1u + sd_rand(0x40u), y0 = sd_rand(0x40u), w = 1u + sd_rand(0x80u), h = 1u + sd_rand(0x100u);
    uint32_t off = sd_rand(4u) ? 0u : sd_rand(0x40u);
    if (off == x0) off = 0;
    sd_w32(SD_CTX + 0x208u, sd_rand(3u) ? 1u : sd_rand(3u));
    sd_w32(SD_CTX + 0x20cu, random32());
    sd_w32(SD_CTX + 0x210u, x0);
    sd_w32(SD_CTX + 0x214u, y0);
    sd_w32(SD_CTX + 0x218u, x0 + w);
    sd_w32(SD_CTX + 0x21cu, y0 + h);
    sd_w32(SD_CTX + 0x220u, off);
    sd_w32(SD_CTX + 0x228u, random32());
    sd_w32(SD_CTX + 0x22cu, random32() | 0x80000000u);
    sd_set32(0x006c5f70u, sd_at(SD_CTX));
    for (unsigned i = 0; i < 8u; ++i) sd_matrix(SD_MATS + 0x20u * i);
    sd_matrix(SD_M1);
    sd_matrix(SD_M2);
    for (uint32_t i = 0; i < 0x300u; ++i) sd_w8(SD_HAZE + i, random32());
    /* The camera: the shift (or the divisor), near plane, centre, eye, the
     * view and object pointers. */
    uint32_t shift = sd_rand(4u) ? 8u + sd_rand(5u) : 0u;
    sd_shift = shift;
    sd_set32(0x006c6028u, shift);
    sd_set32(0x006c602cu, sd_rand(0x100u));
    sd_set32(0x006c6030u, 1u + sd_rand(0x1000u));
    sd_set32(0x006c6034u, sd_signed(0x200u));
    sd_set32(0x006c6038u, sd_signed(0x200u));
    for (unsigned i = 0; i < 3u; ++i) sd_set32(0x006c601cu + 4u * i, sd_signed(0x4000u));
    sd_set32(0x006c5ff8u, sd_rand(2u) ? 0x006c5ffcu : sd_at(SD_M1));
    sd_set32(0x006c5ff4u, sd_rand(2u) ? 0x006c5ffcu : sd_at(SD_M2));
    for (unsigned i = 0; i < 0x14u; i += 2u) sd_set16(0x006c5fb4u + i, sd_signed(0x4000u));
    for (unsigned i = 0; i < 3u; ++i) sd_set32(0x006c5fc8u + 4u * i, sd_signed(0x4000u));
    /* The detail test, its cached width and scale; the part detail words. */
    sd_set8(0x004a62e0u, shift && sd_rand(2u));
    sd_set32(0x004a0fe8u, sd_rand(3u) ? 0u : w);
    sd_set32(0x006bb2f4u, 0x1000u + sd_rand(0x4000u));
    sd_set32(0x004a101cu, 0x100u + sd_rand(0x800u));
    sd_set32(0x004a1020u, sd_rand(3u) ? 0u : sd_rand(3u));
    sd_set32(0x0049f248u, sd_rand(2u));
    sd_set32(0x006c6064u, random32());
    sd_set32(0x006c6050u, random32());
    sd_set32(0x006c6058u, random32());
    sd_set32(0x006c6054u, sd_rand(6u));
    /* Lights: 0..3 active of four (the three kinds), lights on or off. */
    static const uint32_t vtables[3] = {0x0049f8d4u, 0x0049f8c0u, 0x00497ac8u};
    for (unsigned i = 0; i < 4u; ++i) {
        uint32_t l = SD_LIGHTS + 0x40u * i, kind = sd_rand(3u);
        sd_w32(l, vtables[kind]);
        sd_vec(l + 4u, 0x8000u);
        sd_w32(l + 0x10u, random32());
        sd_w16(l + 0x14u, random32());
        sd_w32(l + 0x16u, kind);
        sd_w32(l + 0x1au, sd_rand(0x201u));
        sd_w32(l + 0x1eu, vtables[0]);
        sd_vec(l + 0x22u, 0x8000u);
        sd_wvec(l + 0x2eu, 0x4000u);
        sd_w32(l + 0x34u, 1u + sd_rand(0x2000u));
        sd_w32(l + 0x38u, sd_rand(0x2001u));
        sd_w32(l + 0x3cu, random32());
    }
    uint32_t lights = sd_rand(4u);
    sd_set8(0x006cbc04u, sd_rand(2u));
    sd_set32(0x006c60a8u, lights);
    for (unsigned i = 0; i < lights; ++i) sd_set32(0x006c60acu + 4u * i, sd_at(SD_LIGHTS + 0x40u * i));
}

/* ---- the part trees */
static uint32_t sd_next_part, sd_next_poly, sd_next_ptr, sd_next_node, sd_next_thr, sd_groups;
static uint32_t sd_ptrs(unsigned n) { uint32_t p = SD_PTRS + 4u * sd_next_ptr; sd_next_ptr += n; return p; }
/* Colours: a pair is either both "not drawn" (top byte 0x14) or both small. */
static void sd_colour_pair(uint32_t offset) {
    if (sd_rand(8u) == 0) { sd_w32(offset, 0x14000000u | sd_rand(0x1000000u)); sd_w32(offset + 4u, 0x14000000u | sd_rand(0x1000000u)); }
    else { sd_w32(offset, sd_rand(0x100u)); sd_w32(offset + 4u, sd_rand(4u) ? sd_rand(0x100u) : sd_r32(offset)); }
}
/* A point set, an index list and a colour table (set g of three). */
static void sd_geometry(unsigned g, unsigned points) {
    for (unsigned i = 0; i < points; ++i) sd_wvec(SD_POINTS + 0x120u * g + 6u * i, 0x2000u);
    for (unsigned i = 0; i < 128u; ++i) sd_w16(SD_INDEX + 0x100u * g + 2u * i, sd_rand(points));
    for (unsigned i = 0; i < 8u; ++i) { sd_colour_pair(SD_COLOURS + 0x80u * g + 16u * i); sd_colour_pair(SD_COLOURS + 0x80u * g + 16u * i + 8u); }
}
static uint32_t sd_polygon(unsigned max_count) {
    uint32_t p = SD_POLYS + 0x10u * (sd_next_poly++ % 64u);
    /* Vertex ranges inside the first 32 index words (the default list
     * 0x6c6068 holds 32), the colour index even (a front/back pair of
     * colour dwords, both drawn or both not). */
    unsigned kind = sd_rand(3u), count = 1u + sd_rand(max_count);
    sd_w32(p, sd_vt(kind));
    sd_w16(p + 4u, sd_rand(40u));          /* normal (a point of the set) */
    sd_w16(p + 6u, sd_rand(40u));          /* point */
    sd_w16(p + 8u, count);
    sd_w16(p + 0xau, sd_rand(33u - count));
    sd_w16(p + 0xcu, 2u * sd_rand(8u));
    sd_w16(p + 0xeu, sd_rand(33u - count));
    return sd_at(p);
}
static uint32_t sd_transform_word(void) { return sd_rand(3u) ? 0xffffu : sd_rand(4u); }
static uint32_t sd_part(unsigned depth);
static unsigned char sd_kinds[40];
static uint32_t sd_children(uint32_t part, unsigned depth, unsigned min, unsigned max) {
    unsigned n = min + sd_rand(max - min + 1u);
    uint32_t ptrs = sd_ptrs(n ? n : 1u);
    sd_w16(part + 0x10u, n);
    sd_w32(part + 0x12u, sd_at(ptrs));
    for (unsigned i = 0; i < n; ++i) sd_w32(ptrs + 4u * i, sd_part(depth + 1u));
    return n;
}
static uint32_t sd_part(unsigned depth) {
    if (sd_next_part >= 40u || sd_next_ptr > 0xe0u) {
        uint32_t leaf = SD_PARTS + 0x30u * 39u;
        sd_w32(leaf, sd_vt(SD_K_LEAF));
        sd_kinds[39] = SD_K_LEAF;
        if (sd_next_part < 40u) sd_next_part = 40u;
        return sd_at(leaf);
    }
    sd_kinds[sd_next_part] = 0;
    uint32_t p = SD_PARTS + 0x30u * sd_next_part++;
    unsigned kind = depth >= 3u ? (sd_rand(2u) ? SD_K_LEAF : SD_K_GROUP) : SD_K_LIST + sd_rand(6u);
    sd_kinds[(p - SD_PARTS) / 0x30u] = (unsigned char)kind;
    sd_w32(p, sd_vt(kind));
    sd_w16(p + 4u, sd_transform_word());
    sd_w16(p + 8u, 1u + sd_rand(0x800u));
    switch (kind) {
    case SD_K_LIST: sd_children(p, depth, 0, 3); break;
    case SD_K_GROUP: {
        unsigned g = sd_groups++ % 3u, n = sd_rand(5u);
        uint32_t ptrs = sd_ptrs(n ? n : 1u);
        sd_w32(p + 0x10u, sd_rand(6u) ? sd_at(SD_INDEX + 0x100u * g) : 0u);
        sd_w32(p + 0x14u, sd_at(SD_POINTS + 0x120u * g));
        sd_w32(p + 0x18u, sd_at(SD_COLOURS + 0x80u * g));
        sd_w32(p + 0x1cu, sd_at(ptrs));
        sd_w16(p + 0x22u, 40u);
        sd_w16(p + 0x26u, n);
        for (unsigned i = 0; i < n; ++i) sd_w32(ptrs + 4u * i, sd_polygon(sd_rand(4u) ? 4u : 10u));
        break;
    }
    case SD_K_CELLS: sd_children(p, depth, 1, 3); sd_w16(p + 0x16u, sd_rand(8u)); break;
    case SD_K_DETAIL: {
        unsigned n = sd_children(p, depth, 1, 4);
        uint32_t t = SD_THR + 2u * sd_next_thr;
        sd_next_thr = (sd_next_thr + 8u) % 0x58u;
        uint32_t v = 0;
        for (unsigned i = 0; i < n + 3u; ++i) { v += sd_rand(0x200u); sd_w16(t + 2u * i, v); }
        sd_w32(p + 0x16u, sd_at(t));
        break;
    }
    case SD_K_BSP: {
        unsigned n = sd_children(p, depth, 1, 3), nodes = 1u + sd_rand(4u);
        uint32_t base = sd_next_node;
        if (base + nodes > 16u) base = 0;
        sd_next_node = base + nodes;
        for (unsigned j = 0; j < nodes; ++j) {
            uint32_t r = SD_BSP + 14u * (base + j);
            sd_wvec(r, 0x4000u);
            sd_w32(r + 6u, sd_signed(0x10000u));
            /* The eye on the plane (d == 0, the side test's boundary): a
             * zero normal and distance give 0 with or without a transform. */
            if (!sd_rand(8u)) { for (unsigned i = 0; i < 6u; i += 2u) sd_w16(r + i, 0); sd_w32(r + 6u, 0); }
            /* A link: a child (0x4000 | index), nothing (negative) or a
             * later node of the part's array (no cycles). */
            for (unsigned s = 0; s < 2u; ++s) {
                unsigned pick = sd_rand(4u);
                uint32_t link = pick == 0 ? 0xffffu : 0x4000u | sd_rand(n);
                if (pick == 3 && j + 1u < nodes) link = j + 1u + sd_rand(nodes - 1u - j);
                sd_w16(r + 0xau + 2u * s, link);
            }
            sd_w16(SD_BSPT + 2u * (base + j), sd_rand(3u) ? 0xffffu : sd_rand(4u));
        }
        sd_w32(p + 0x18u, sd_at(SD_BSP + 14u * base));
        sd_w32(p + 0x1cu, sd_at(SD_BSPT + 2u * base));
        break;
    }
    default: break;
    }
    return sd_at(p);
}
/* A shape: a part list with a bounding sphere, transforms, cells, record. */
static uint32_t sd_shape(void) {
    if (sd_next_part >= 38u) sd_next_part = 38u;
    sd_kinds[sd_next_part] = SD_K_LIST;
    uint32_t p = SD_PARTS + 0x30u * sd_next_part++;
    sd_w32(p, sd_vt(SD_K_LIST));
    sd_w16(p + 4u, sd_transform_word());
    sd_w16(p + 8u, 1u + sd_rand(0x2000u));
    sd_wvec(p + 0xau, 0x2000u);
    sd_children(p, 0, 1, 3);
    sd_w32(p + 0x18u, sd_at(SD_MATS));
    sd_w32(p + 0x1cu, sd_at(SD_CELLS));
    sd_w32(p + 0x26u, random32());
    return sd_at(p);
}
static void sd_tree_reset(void) {
    sd_next_part = sd_next_poly = sd_next_ptr = sd_next_node = sd_next_thr = sd_groups = 0;
    for (unsigned g = 0; g < 3u; ++g) sd_geometry(g, 40u);
    for (unsigned i = 0; i < 0x20u; ++i) sd_w16(SD_CELLS + 2u * i, random32());
    /* The default index list 0x6c6068: indices below the point count. */
    for (unsigned i = 0; i < 32u; ++i) sd_set16(0x006c6068u + 2u * i, sd_rand(40u));
}
static uint32_t sd_instance(unsigned i, uint32_t shape) {
    uint32_t inst = SD_INST + 0x20u * i;
    sd_w32(inst, sd_vt(SD_K_INSTANCE));
    sd_w32(inst + 4u, shape);
    sd_w32(inst + 8u, sd_at(SD_CELLS + 2u * sd_rand(8u)));
    sd_w32(inst + 0x16u, sd_at(SD_MATS));
    return sd_at(inst);
}
static uint32_t sd_actor(unsigned i, uint32_t inst, uint32_t s0, uint32_t s1) {
    uint32_t a = SD_ACTORS + 0x50u * i, t = SD_DTAB + 0x10u * i;
    sd_wvec(a + 0xcu, 0x8000u);
    sd_matrix(a + 0x12u);
    sd_w16(a + 0x32u, sd_rand(2u));
    sd_w32(a + 0x34u, inst);
    unsigned levels = sd_rand(4u);
    sd_w32(a + 0x41u, sd_rand(4u) ? sd_at(t) : 0u);
    sd_w32(t, sd_at(SD_LSHAPES + 0x10u * i));
    sd_w32(t + 4u, sd_at(SD_LTHR + 0x20u * i));
    sd_w16(t + 0xcu, levels);
    for (unsigned j = 0; j < 4u; ++j) sd_w32(SD_LSHAPES + 0x10u * i + 4u * j, sd_rand(2u) ? s0 : s1);
    uint32_t v = 0x10000u;
    for (unsigned j = 0; j < 8u; ++j) { v -= sd_rand(0x2000u); sd_w32(SD_LTHR + 0x20u * i + 4u * j, sd_rand(8u) ? v : random32()); }
    return sd_at(a);
}

/* ---- draw */
static int sd_draw_case(unsigned k) {
    sd_begin();
    sd_world_base();
    sd_tree_reset();
    uint32_t s0 = sd_shape(), s1 = sd_shape();
    uint32_t i0 = sd_instance(0, s0), i1 = sd_instance(1, s1);
    uint32_t a0 = sd_actor(0, sd_instance(2, s0), s0, s1), a1 = sd_actor(1, sd_instance(3, s1), s0, s1);
    sd_set32(0x0049736cu, sd_rand(4u));
    sd_set32(0x00497368u, 0x100u + sd_rand(0x800u));
    sd_set32(0x004a95b4u, sd_rand(3u));
    unsigned ops = 2u + sd_rand(4u);
    for (unsigned j = 0; j < ops; ++j) {
        switch (sd_rand(8u)) {
        case 0: SDOP1(0x00478da4u, SD_EAX, i0); break;
        case 1: SDOP1(0x00478da4u, SD_EAX, i1); break;
        case 2: SDOP1(0x00490c1cu, SD_EAX, sd_rand(2u) ? i0 : i1); break;
        case 3: SDOP1(0x00490ab8u, SD_EAX, sd_rand(2u) ? s0 : s1); break;
        case 4: SDOP1(0x0046b8eau, SD_EAX, sd_rand(2u) ? a0 : a1); break;
        case 5: SDOP1(0x004033e4u, SD_EAX, sd_rand(2u) ? a0 : a1); break;
        case 6: SDOP1(0x004034f4u, SD_EAX, sd_rand(2u) ? a0 : a1); break;
        default: SDOP1(0x00401fe4u, SD_EAX, sd_rand(2u) ? a0 : a1); break;
        }
    }
    return sd_run("draw", k);
}

/* ---- polygons: a preset render context */
static void sd_context(unsigned g) {
    sd_set32(0x006c68e8u, sd_at(SD_POINTS + 0x120u * g));
    sd_set32(0x006c68f2u, sd_rand(6u) ? sd_at(SD_INDEX + 0x100u * g) : 0x006c6068u);
    sd_set32(0x006c68f6u, sd_at(SD_COLOURS + 0x80u * g));
    sd_set16(0x006c68f0u, 40u);
}
/* The point states with depths consistent with them (state 2: in front of
 * the near plane, states 0 and 1: at or behind it), the cache. */
static void sd_states(int all_zero) {
    for (unsigned i = 0; i < 40u; ++i) {
        unsigned s = all_zero ? 0u : sd_rand(3u);
        sd_set8(0x006c68fau + i, s);
        uint32_t v = 0x006c6ce2u + 12u * i;
        sd_set32(v, sd_signed(0x4000u));
        sd_set32(v + 8u, sd_signed(0x4000u));
        sd_set32(v + 4u, s == 2u ? 0x101u + sd_rand(0x4000u) : (uint32_t)-(int32_t)sd_rand(0x4000u));
        sd_set32(0x006c9bc2u + 8u * i, sd_signed(0x400u));
        sd_set32(0x006c9bc2u + 8u * i + 4u, sd_signed(0x400u));
    }
}
static int sd_polygons_case(unsigned k) {
    sd_begin();
    sd_world_base();
    sd_tree_reset();
    unsigned g = sd_rand(3u);
    sd_context(g);
    sd_states(sd_rand(3u) == 0);
    uint32_t in = SD_BUF, out = SD_BUF + 0x80u;
    for (unsigned i = 0; i < 64u; ++i) sd_w16(in + 2u * i, random32());
    /* One vertex range (inside the 32 words of the default list; at most 16
     * vertices so that a clip's up to 32 output points fit 0x6cbb02) and
     * one output polygon per case (presets apply before the first call). */
    unsigned count = 1u + sd_rand(sd_rand(4u) ? 6u : 16u), first = sd_rand(33u - count), n_out = sd_rand(8u);
    sd_set16(0x006c68e4u, sd_rand(16u) ? count : 0u);
    sd_set16(0x006c68e6u, first);
    sd_set16(0x006cbc02u, n_out);
    for (unsigned i = 0; i < 2u * n_out; ++i) sd_set32(0x006cbb02u + 4u * i, sd_signed(0x400u));
    /* Without a shift a projection marks points 2 at any depth, so a later
     * clip could divide by a zero depth difference (#DE in the original):
     * no direct clip after one. */
    int unclippable = 0;
    unsigned ops = 1u + sd_rand(5u);
    for (unsigned j = 0; j < ops; ++j) {
        unsigned pick = sd_rand(16u);
        if ((pick == 8 || pick == 9) && unclippable) pick = 7;
        if (!sd_shift && (pick <= 3 || pick == 6)) unclippable = 1;
        switch (pick) {
        case 0: case 1: case 2: case 3: {
            uint32_t p = sd_polygon(sd_rand(4u) ? 6u : 16u);
            unsigned kind = sd_rand(3u);
            put32(sd_world_start + (p - sd_at(0)), sd_vt(kind));
            SDOP1(kind == 0 ? 0x00474ec0u : kind == 1 ? 0x00475538u : 0x004756d4u, kind == 2 ? 0u : SD_EAX, p);
            break;
        }
        case 4: SDOP2(0x0048c72cu, SD_EAX, sd_at(SD_POINTS + 0x120u * g + 6u * sd_rand(40u)), sd_at(SD_POINTS + 0x120u * g + 6u * sd_rand(40u))); break;
        case 5:
            sd_vec(SD_VEC, 0x8000u);
            SDOP2(0x0048c844u, SD_EAX, sd_at(SD_POINTS + 0x120u * g + 6u * sd_rand(40u)), sd_at(SD_VEC));
            break;
        case 6: SDOP0(0x0048c954u, SD_EAX); break;
        case 7: SDOP0(0x004912a0u, SD_EAX); break;
        case 8: SDOP0(0x0048cf20u, SD_EAX); break;
        case 9: SDOP2(0x0048d38cu, SD_AX, sd_at(in), sd_at(out)); break;
        case 10: case 11: {
            /* EAX of an empty polygon is the entry EAX (not a contract); the
             * thunk 0x48d708 loads its argument first. */
            static const uint32_t fills[4] = {0x0048d5c0u, 0x0048d624u, 0x0048d5c0u, 0x0048d708u};
            uint32_t va = fills[sd_rand(4u)];
            if (va == 0x0048d5c0u) SDOP0(va, 0u);
            else SDOP1(va, va == 0x0048d708u ? SD_EAX : 0u, sd_rand(2u));
            break;
        }
        default: {
            static const uint32_t forwards[3] = {0x0048d9bau, 0x0048dbacu, 0x0048dbc1u};
            sd_vec(SD_VEC, 0x400u);
            sd_vec(SD_VEC + 12u, 0x400u);
            SDOP3(forwards[sd_rand(3u)], SD_EAX, random32(), 3u, sd_at(SD_VEC));
            break;
        }
        }
    }
    return sd_run("polygons", k);
}

/* ---- leaves */
static int sd_leaves_case(unsigned k) {
    sd_begin();
    sd_world_base();
    sd_tree_reset();
    uint32_t s0 = sd_shape();
    sd_set32(0x006b7b68u, sd_at(SD_MATS));
    sd_set32(0x006bb2e4u, sd_rand(3u) ? 0u : sd_rand(5u));
    for (unsigned i = 0; i < 6u; ++i) sd_set8(0x006bb2b0u + i, sd_rand(2u));
    sd_set32(0x006b7b90u, sd_rand(2u) ? 0x006c5ffcu : sd_at(SD_M1));
    for (unsigned i = 0; i < 3u; ++i) sd_set32(0x006bb2e8u + 4u * i, sd_signed(0x4000u));
    sd_set32(0x006b7b6cu, sd_at(SD_CELLS));
    for (unsigned i = 0; i < 6u; ++i) {
        /* the saved views' pointers must point at matrices */
        sd_set32(0x006b7b70u + 0x110u * i + 0x20u, sd_rand(2u) ? 0x006c5ffcu : sd_at(SD_M1));
        sd_set32(0x006b7b70u + 0x110u * i + 0x24u, sd_rand(2u) ? 0x006c5ffcu : sd_at(SD_M2));
    }
    unsigned ops = 2u + sd_rand(6u), parts = sd_next_part < 40u ? sd_next_part : 40u, bsp = 40u;
    for (unsigned i = 0; i < parts; ++i) if (sd_kinds[i] == SD_K_BSP) bsp = i;
    for (unsigned j = 0; j < ops; ++j) {
        unsigned index = sd_rand(parts ? parts : 1u), pick = sd_rand(20u);
        uint32_t part = SD_PARTS + 0x30u * index;
        if (pick == 4 && !sd_shift) pick = 5;            /* the detail test only with a shift (angle_ratio) */
        if (pick == 8 && bsp == 40u) pick = 5;
        switch (pick) {
        /* Without a table 0x4760e4 stores 0 into 0x6b7b68; a later BSP
         * node with a transform word would read at (word << 5) + 0 (a fault
         * in the original), so that variant only as the case's last op. */
        case 0: SDOP2(0x004760e4u, SD_EAX, s0, sd_rand(4u) || j + 1u < ops ? sd_at(SD_MATS) : 0u); break;
        case 1: SDOP2(0x00476120u, SD_EAX, sd_at(part), sd_at(SD_FRAME)); break;
        case 2: SDOP3(0x0047613cu, SD_EAX, sd_at(part), sd_at(SD_FRAME), sd_rand(3u) ? sd_rand(4u) : 0xffffffffu); break;
        case 3: SDOP1(0x0048d718u, SD_EAX, sd_at(SD_SAVE)); break;
        case 4: SDOP1(0x00476248u, SD_EAX, s0); break;
        case 5: SDOP1(0x00476808u, SD_EAX, s0); break;
        case 6: case 7: {
            unsigned kind = sd_kinds[index];
            if (kind >= SD_K_LIST && kind <= SD_K_BSP) SDOP1(sd_r32(SD_VT + 0x20u * kind + 0x1cu), SD_EAX, sd_at(part));
            else SDOP1(0x00476808u, SD_EAX, s0);
            break;
        }
        case 8: SDOP2(0x00476b28u, SD_EAX, sd_at(SD_PARTS + 0x30u * bsp), 0u); break;
        case 9: {
            sd_vec(SD_VEC, 0x8000u);
            sd_wvec(SD_VEC + 0x10u, 0x4000u);
            SDOP3(0x004911c0u, SD_EAX, sd_at(SD_VEC), sd_at(SD_MATS + 0x20u * sd_rand(8u)), sd_at(SD_VEC + 0x10u));
            break;
        }
        case 10: SDOP1(0x004769b8u, SD_EAX, sd_rand(3u)); break;
        /* 0x45d490/0x45d50c are hud_load.h's rows (hud_load.c), not this
         * package's: their table words are preset above instead. */
        case 11: case 12: SDOP1(0x00476808u, SD_EAX, s0); break;
        case 13: SDOP2(0x00490d3au, SD_EAX, s0, sd_at(SD_CELLS)); break;
        case 14: SDOP0(0x00490d47u, SD_EAX); break;
        case 15: SDOP1(0x00490d4du, SD_EAX, s0); break;
        case 16: SDOP1(0x00490d58u, SD_EAX, random32()); break;
        case 17: SDOP3(0x00478d60u, SD_EAX, 0u, sd_at(SD_FRAME), sd_at(SD_MATS)); break;
        case 18: SDOP2(0x00478d90u, SD_EAX, 0u, sd_rand(3u) ? sd_at(SD_M1) : 0u); break;
        default: SDOP1(0x00490ab8u, SD_EAX, s0); break;
        }
    }
    return sd_run("leaves", k);
}

/* ---- textures: the textured polygon 0x474fa8, the plain, lit and Gouraud
 * polygons without the haze table 0x475cd0/0x475d8c/ 0x475eec, BSP polygon
 * groups 0x475c04/0x475b2c, sprite parts 0x4763f4, the dword projection
 * 0x48ca70, the UV clip 0x48d0f8 and the segment clip 0x40bb4c. The 3D texture
 * fills and the rotated blit are recording stand-ins; the fill layer also logs
 * the context's mode record. */
#define SD_TS 0x2c00u    /* [0x6c6064]: +0 the record (at +0x10, its +8 the bitmap table), +4 the texture records */
#define SD_TBMT 0x2c20u  /* 8 bitmap pointers */
#define SD_TBM 0x2c40u   /* 4 bitmaps of 0x20 */
#define SD_TREC 0x2cc0u  /* 8 texture records of 0x14 */
#define SD_TCOL 0x2d60u  /* the colour table: dwords 0..15 colours, 16..31 texture indices */
#define SD_TPTS 0x2de0u  /* 40 dword points (to 0x2fc0) */
static int sd_tex_on;
static unsigned sd_tx_project1, sd_tx_uv, sd_tx_seg[2], sd_tx_ops[12];
static const unsigned sd_tx_kinds[7] = {SD_K_SOLID, SD_K_SHADED, SD_K_GOURAUD, SD_K_PLAIN, SD_K_LIT, SD_K_GPLAIN, SD_K_TEXTURED};
static void sd_tex_world(void) {
    sd_w32(SD_TS, sd_at(SD_TS + 0x10u));
    sd_w32(SD_TS + 4u, sd_at(SD_TREC));
    sd_w32(SD_TS + 0x18u, sd_at(SD_TBMT));
    for (unsigned i = 0; i < 8u; ++i) sd_w32(SD_TBMT + 4u * i, sd_rand(6u) ? sd_at(SD_TBM + 0x20u * sd_rand(4u)) : 0u);
    for (unsigned i = 0; i < 4u; ++i) {
        uint32_t b = SD_TBM + 0x20u * i;
        for (uint32_t j = 0; j < 0x20u; j += 4u) sd_w32(b + j, random32());
        sd_w16(b + 4u, 1u + sd_rand(0x100u));   /* the sprite's divisor (#DE at 0); the texture's v extent */
        sd_w16(b + 6u, sd_rand(0x101u));
    }
    for (unsigned i = 0; i < 8u * 5u; ++i) sd_w32(SD_TREC + 4u * i, sd_rand(4u) ? sd_rand(0x100u) : random32());
    for (unsigned i = 0; i < 8u; ++i) sd_colour_pair(SD_TCOL + 8u * i);
    for (unsigned i = 16u; i < 32u; ++i) sd_w32(SD_TCOL + 4u * i, sd_rand(6u) ? sd_rand(8u) : 0xffffffffu);
    for (unsigned i = 0; i < 40u; ++i) sd_vec(SD_TPTS + 12u * i, 0x2000u);
}
/* A polygon of the kind; a textured one takes its colour index in the
 * texture half and at most 4 vertices for the 3D fills' static arrays. */
static uint32_t sd_tex_polygon(unsigned kind) {
    uint32_t p = sd_polygon(kind == SD_K_TEXTURED && !sd_tex_on ? 4u : sd_rand(4u) ? 6u : 16u) - sd_at(0);
    sd_w32(p, sd_vt(kind));
    sd_w16(p + 0xcu, kind == SD_K_TEXTURED ? 16u + 2u * sd_rand(7u) : 2u * sd_rand(7u));
    return sd_at(p);
}
/* A BSP polygon group: nodes of 10 bytes (distance, polygon, two links:
 * none, a polygon 0x4000 | i or a later node, no cycles) at +0x2a. */
static uint32_t sd_tex_group(unsigned index, unsigned g, int *gouraud, unsigned *nodes_out) {
    uint32_t p = SD_PARTS + 0x30u * index;
    unsigned n = 1u + sd_rand(6u), nodes = 1u + sd_rand(6u);
    uint32_t ptrs = sd_ptrs(n), base = sd_next_node;
    sd_next_node += nodes;
    sd_kinds[index] = SD_K_GROUP_BSP;
    sd_w32(p, sd_vt(SD_K_GROUP_BSP));
    sd_w16(p + 4u, sd_transform_word());
    sd_w16(p + 8u, 1u + sd_rand(0x800u));
    sd_w32(p + 0x10u, sd_rand(6u) ? sd_at(SD_INDEX + 0x100u * g) : 0u);
    sd_w32(p + 0x14u, sd_at(SD_POINTS + 0x120u * g));
    sd_w32(p + 0x18u, sd_at(SD_TCOL));
    sd_w32(p + 0x1cu, sd_at(ptrs));
    sd_w16(p + 0x22u, 40u);
    sd_w16(p + 0x26u, n);
    *gouraud = 0;
    for (unsigned i = 0; i < n; ++i) {
        unsigned kind = sd_tx_kinds[sd_rand(7u)];
        if (kind == SD_K_GOURAUD || kind == SD_K_GPLAIN) *gouraud = 1;
        sd_w32(ptrs + 4u * i, sd_tex_polygon(kind));
    }
    for (unsigned j = 0; j < nodes; ++j) {
        uint32_t r = SD_BSP + 10u * (base + j);
        sd_w32(r, sd_rand(8u) ? sd_signed(0x4000000u) : 0u);
        sd_w16(r + 4u, sd_rand(n));
        for (unsigned s = 0; s < 2u; ++s) {
            unsigned pick = sd_rand(4u);
            uint32_t link = pick == 0 ? 0xffffu : 0x4000u | sd_rand(n);
            if (pick == 3 && j + 1u < nodes) link = j + 1u + sd_rand(nodes - 1u - j);
            sd_w16(r + 6u + 2u * s, link);
        }
    }
    sd_w32(p + 0x2au, sd_at(SD_BSP + 10u * base));
    *nodes_out = nodes;
    return sd_at(p);
}
/* A sprite part: centre words +0xa, bitmap index +0x10, offset bytes +0x12/+0x13. */
static uint32_t sd_tex_sprite(unsigned index) {
    uint32_t p = SD_PARTS + 0x30u * index;
    sd_kinds[index] = SD_K_SPRITE;
    sd_w32(p, sd_vt(SD_K_SPRITE));
    sd_w16(p + 4u, sd_transform_word());
    sd_w16(p + 8u, sd_rand(0x800u));
    sd_wvec(p + 0xau, 0x2000u);
    sd_w16(p + 0x10u, sd_rand(8u));
    sd_w8(p + 0x12u, random32());
    sd_w8(p + 0x13u, random32());
    return sd_at(p);
}
static int sd_textures_case(unsigned k) {
    sd_begin();
    sd_world_base();
    sd_tree_reset();
    sd_tex_log = 1;
    sd_tex_on = (int)sd_rand(2u);
    sd_tex_world();
    sd_set32(0x006c6064u, sd_at(SD_TS));
    sd_set32(0x006c68ecu, sd_at(SD_TPTS));
    sd_set32(0x0049f244u, (uint32_t)sd_tex_on);
    sd_set32(0x0049f24cu, sd_rand(2u));
    for (unsigned i = 0; i < 3u; ++i) sd_set8(0x004a0f4cu + i, sd_rand(3u) ? 0u : 1u);
    sd_set32(0x004a5b04u, sd_rand(3u) ? 0u : 1u + sd_rand(16u));
    sd_set32(0x004a5b00u, random32());
    sd_set32(0x004aaaacu, sd_rand(3u));
    /* A near plane of at least 1: 0x48ca70 divides by a depth at or past it. */
    sd_set32(0x006c602cu, 1u + sd_rand(0xffu));
    /* The part transforms as in the leaves part. */
    sd_set32(0x006b7b68u, sd_at(SD_MATS));
    sd_set32(0x006bb2e4u, sd_rand(3u) ? 0u : sd_rand(5u));
    for (unsigned i = 0; i < 6u; ++i) sd_set8(0x006bb2b0u + i, sd_rand(2u));
    sd_set32(0x006b7b90u, sd_rand(2u) ? 0x006c5ffcu : sd_at(SD_M1));
    for (unsigned i = 0; i < 3u; ++i) sd_set32(0x006bb2e8u + 4u * i, sd_signed(0x4000u));
    sd_set32(0x006b7b6cu, sd_at(SD_CELLS));
    for (unsigned i = 0; i < 6u; ++i) {
        sd_set32(0x006b7b70u + 0x110u * i + 0x20u, sd_rand(2u) ? 0x006c5ffcu : sd_at(SD_M1));
        sd_set32(0x006b7b70u + 0x110u * i + 0x24u, sd_rand(2u) ? 0x006c5ffcu : sd_at(SD_M2));
    }
    int gouraud[4];
    unsigned nodes[4];
    uint32_t groups[4], sprites[4];
    for (unsigned i = 0; i < 4u; ++i) groups[i] = sd_tex_group(i, sd_rand(3u), &gouraud[i], &nodes[i]);
    for (unsigned i = 0; i < 4u; ++i) sprites[i] = sd_tex_sprite(4u + i);
    /* A part list over them (their draw slots). */
    uint32_t list = SD_PARTS + 0x30u * 8u, children = sd_ptrs(8u);
    sd_kinds[8] = SD_K_LIST;
    sd_w32(list, sd_vt(SD_K_LIST));
    sd_w16(list + 4u, sd_transform_word());
    sd_w16(list + 8u, 1u + sd_rand(0x800u));
    sd_w16(list + 0x10u, 1u + sd_rand(8u));
    sd_w32(list + 0x12u, sd_at(children));
    for (unsigned i = 0; i < 8u; ++i) sd_w32(children + 4u * i, sd_rand(2u) ? groups[sd_rand(4u)] : sprites[sd_rand(4u)]);
    /* The render context for direct calls, as in the polygons part. */
    unsigned g = sd_rand(3u);
    sd_context(g);
    sd_set32(0x006c68f6u, sd_at(SD_TCOL));
    sd_states(sd_rand(3u) == 0);
    unsigned count = 1u + sd_rand(sd_rand(4u) ? 6u : 16u), first = sd_rand(33u - count), n_out = sd_rand(8u);
    sd_set16(0x006c68e4u, sd_rand(16u) ? count : 0u);
    sd_set16(0x006c68e6u, first);
    sd_set16(0x006cbc02u, n_out);
    for (unsigned i = 0; i < 2u * n_out; ++i) sd_set32(0x006cbb02u + 4u * i, sd_signed(0x400u));
    for (unsigned i = 0; i < 32u; ++i) sd_w32(SD_BUF + 4u * i, sd_signed(0x400u));
    /* Without a shift a projection marks points 2 at any depth: no UV clip
     * after one (a zero depth difference is #DE in the original). */
    int unclippable = 0;
    unsigned ops = 1u + sd_rand(6u);
    for (unsigned j = 0; j < ops; ++j) {
        unsigned pick = sd_rand(12u);
        if (pick == 1 && unclippable) pick = 10;
        if (!sd_shift && pick != 1 && pick != 7 && pick != 8 && pick != 10) unclippable = 1;
        ++sd_tx_ops[pick];
        switch (pick) {
        case 0: SDOP0(0x0048ca70u, SD_EAX); break;
        case 1: SDOP2(0x0048d0f8u, SD_EAX, sd_at(SD_BUF), sd_at(SD_BUF + 0x80u)); break;
        case 2: case 3: case 11: {
            unsigned kind = pick == 11 ? SD_K_TEXTURED : sd_tx_kinds[3u + sd_rand(4u)];
            uint32_t p = sd_tex_polygon(kind);
            SDOP1(sd_r32(SD_VT + 0x20u * kind + 0x1cu), kind == SD_K_GPLAIN ? 0u : SD_EAX, p);
            break;
        }
        case 4: { unsigned i = sd_rand(4u); SDOP2(0x00475b2cu, gouraud[i] ? 0u : SD_EAX, groups[i], sd_rand(nodes[i])); break; }
        case 5: case 6: SDOP1(0x00475c04u, SD_EAX, groups[sd_rand(4u)]); break;
        case 7: case 8: SDOP1(0x004763f4u, SD_EAX, sprites[sd_rand(4u)]); break;
        case 9: SDOP1(0x00476808u, SD_EAX, sd_at(list)); break;
        default: {
            uint32_t a = SD_VEC + 0x18u * j;
            for (unsigned i = 0; i < 6u; ++i) sd_w32(a + 4u * i, i % 3u == 1u ? sd_signed(0x200u) : sd_signed(0x8000u));
            SDOP2(0x0040bb4cu, SD_EAX, sd_at(a), sd_at(a + 12u));
            break;
        }
        }
    }
    if (sd_run("textures", k)) return 1;
    for (unsigned i = 0; i < sd_op_count; ++i) {
        uint32_t eax = sd_res[1][i][0];
        if (sd_ops[i].va == 0x0048ca70u && eax == 1u) ++sd_tx_project1;
        if (sd_ops[i].va == 0x0048d0f8u && (eax & 0xffffu)) ++sd_tx_uv;
        if (sd_ops[i].va == 0x0040bb4cu) ++sd_tx_seg[eax & 1u];
    }
    return 0;
}

static int shape_draw_cases(void) {
    current_function = "shape-draw";
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
        return fail("shape draw: memory");
    if (get32(mapped + (SU_COUNTER - 0x400000u))) return fail("shape draw: the counter word is not free");
    su_pristine_take();
    static const struct { const char *dll, *name; void *stub; } imports[] = {
        {"cw3220.dll", "_calloc", (void *)ir_calloc}, {"cw3220.dll", "_free", (void *)ir_free},
        {"cw3220.dll", "_memset", (void *)ir_memset}, {"cw3220.dll", "_memmove", (void *)ir_memmove},
    };
    for (unsigned i = 0; i < sizeof(imports) / sizeof(imports[0]); ++i)
        if (!ir_patch_import(imports[i].dll, imports[i].name, imports[i].stub)) {
            fprintf(stderr, "import %s!%s\n", imports[i].dll, imports[i].name);
            return fail("shape draw: import slot not found");
        }
    if (sd_dispatch_setup()) return 1;
    for (unsigned i = 0; i < sizeof(sd_stubs) / sizeof(sd_stubs[0]); ++i)
        if (sd_patch(sd_stubs[i].va, sd_stubs[i].stub, sd_saved[i], 1)) return 1;
    int bad = 0;
    static const struct { const char *name; int (*run)(unsigned); unsigned cases; } parts[] = {
        {"draw", sd_draw_case, 1500}, {"polygons", sd_polygons_case, 3000}, {"leaves", sd_leaves_case, 2000},
        {"textures", sd_textures_case, 3000},
    };
    for (unsigned p = 0; !bad && p < sizeof(parts) / sizeof(parts[0]); ++p) {
        for (unsigned k = 0; !bad && k < parts[p].cases; ++k) {
            current_case = k; current_case_valid = 1;
            bad = parts[p].run(k);
        }
        if (!bad) printf("PASS: shape draw %s: %u cases identical\n", parts[p].name, parts[p].cases);
    }
    if (!bad) printf("SHAPE DRAW coverage: %u fill calls, %u leaf draws, longest log %u dwords\n", sd_fills, sd_leaves_logged, sd_max_log);
    if (!bad)
        printf("SHAPE DRAW textures coverage: %u textured fills, %u 3D fills, %u blits, 0x48ca70 result 1 x%u, 0x48d0f8 output x%u, "
               "0x40bb4c 0/1 %u/%u; ops %u %u %u %u %u %u %u %u %u %u %u %u\n",
               sd_tex_fills, sd_fills3d, sd_blits, sd_tx_project1, sd_tx_uv, sd_tx_seg[0], sd_tx_seg[1], sd_tx_ops[0], sd_tx_ops[1],
               sd_tx_ops[2], sd_tx_ops[3], sd_tx_ops[4], sd_tx_ops[5], sd_tx_ops[6], sd_tx_ops[7], sd_tx_ops[8], sd_tx_ops[9], sd_tx_ops[10],
               sd_tx_ops[11]);
    sd_tex_log = 0;
    current_case_valid = 0;
    for (unsigned i = 0; i < sizeof(sd_stubs) / sizeof(sd_stubs[0]); ++i)
        if (sd_patch(sd_stubs[i].va, sd_stubs[i].stub, sd_saved[i], 0)) bad = 1;
    put32(mapped + 0x2cbee4u, 0);
    memcpy(mapped + SU_DATA_RVA, su_data_pristine, SU_DATA_SIZE);
    return bad;
}
