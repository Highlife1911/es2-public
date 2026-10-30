/* The renderer's leftovers, original machine code against the service rows of
 * sim/src/terrain_draw.c and sim/src/view.c (target :differential_renderer_leaves_test).
 * Builds on startup_cases.h's DATA snapshots (su_take, su_compare),
 * loop_cases.h's loop set-up (lc_reset) and audio_cases.h's register-capturing
 * machine call (au_call) and passed rows (au_original), like fill_rest_cases.h.
 * Identifiers: r13_/R13_.
 *
 * Every case builds its objects (the binning grid with its view polygon
 * and marks, a point, a camera, a view matrix, an object, a stream) in an
 * identity-mapped world and presets DATA (the eye 0x6b4f44, the view in
 * use 0x6c5ff4, the projection 0x6c6028/0x6c6030, the screen centre
 * 0x6c6034), then runs one call on both sides: side 0 the original's code
 * through au_call with random entry registers, side 1 the row through
 * es2_sim_call on a dispatch of the sim's real services with the same
 * entry registers.
 *
 * Recorders: memset 0x49681a, vector_new 0x49667c and the terrain face
 * 0x46e498 get a jump to a host trampoline over their entry on side 0 and
 * a row at the same VA on side 1; the stream's slot +0x18 points at a
 * trampoline (a row at its host address on side 1). Each trampoline
 * returns a scripted EAX, EDX and ECX (the terrain face: 0 or a normal in
 * the world), so all three exit registers are compared in every case. The
 * rotation 0x47eb30 and the transform 0x48043c run as the original on
 * side 1 (au_original); 0x476710 and 0x46be9c run as the original on
 * side 0 and as their own code on side 1. A projection whose division
 * faults is refused by the own side (run first) and then not run on the
 * original. Compared: EAX, EDX, ECX, the recorder hash and count, DATA and
 * the world. */
#pragma once
/* The headers this suite builds on (sim_main.c). */
#include "init_records_cases.h"
#include "startup_cases.h"
#include "loop_cases.h"
#include "audio_cases.h"
#include "es2/sim/load.h"
#include "es2/sim/view.h"
#include "es2/sim/terrain_draw.h"

#define R13_WORLD_SIZE 0x1000u
#define R13_G 0x0000u       /* the grid, 0x120 */
#define R13_POLY 0x0140u    /* its view polygon: 8 points of 8 */
#define R13_CELLS 0x0200u   /* the cells (only their addresses are used) */
#define R13_MARKS 0x0600u   /* the marks, 0x40 bytes */
#define R13_P 0x0680u       /* the point (3 dwords) */
#define R13_W 0x0690u       /* the word pair of 0x46e8c4 */
#define R13_FACE 0x06a0u    /* the terrain face's normal (3 words) */
#define R13_CAM 0x06c0u     /* the camera: +4 x, +8 y, +0x14 the heading word */
#define R13_OUT 0x06f0u     /* the screen point */
#define R13_VIEW 0x0700u    /* the view matrix, 0x20 */
#define R13_OBJ 0x0740u     /* the object under test, 0x80 */
#define R13_S 0x07c0u       /* the stream: its vtable, +4 its error state */
#define R13_VT 0x07d0u      /* the stream's vtable */
#define R13_DST 0x0800u     /* stream data */
static BYTE *r13_world, *r13_world_start;
static es2_sim_entry r13_entries[4096];
static es2_sim_dispatch r13_dispatch;
static es2_sim_load r13_load;
static es2_sim_view r13_view;
static es2_sim_terrain_draw r13_terrain;
static int r13_bad, r13_fault;
static void r13_refuse(const es2_sim_refusal *r, void *user) {
    (void)user;
    /* The projection's IDIV sites (0x48c5fc): the original would raise #DE. */
    if (r->va == 0x0048c616u || r->va == 0x0048c625u || r->va == 0x0048c63bu || r->va == 0x0048c64cu) { r13_fault = 1; return; }
    if (r13_fault && r->va == 0x00470a1cu) return;
    fprintf(stderr, "FAIL: own renderer leaves: refused call to 0x%08lx (%s): %s\n", (unsigned long)r->va, r->name ? r->name : "?",
            r->reason ? r->reason : "");
    r13_bad = 1;
}

/* ---- the recorders: kind 1..4 */
#define R13_KINDS 5u
static const unsigned r13_nargs[R13_KINDS] = {0, 3, 5, 2, 3};
static uint64_t r13_hash[2];
static unsigned r13_count[2], r13_side_now, r13_calls[2][R13_KINDS], r13_kinds[R13_KINDS];
static char r13_text[2][512];
static uint32_t r13_eax[R13_KINDS][4], r13_edx[R13_KINDS][4], r13_ecx[R13_KINDS][4];
uint32_t r13_ecx_out;
uint64_t r13_record(unsigned kind, const uint32_t *a);
static void r13_mix(uint32_t v) {
    for (unsigned i = 0; i < 4u; ++i) { r13_hash[r13_side_now] ^= (BYTE)(v >> (8u * i)); r13_hash[r13_side_now] *= 0x100000001b3ull; }
}
uint64_t r13_record(unsigned kind, const uint32_t *a) {
    unsigned side = r13_side_now, k = kind < R13_KINDS ? r13_calls[side][kind]++ : 0;
    if (kind >= R13_KINDS) { r13_ecx_out = 0; return 0; }
    r13_mix(0xabc00000u + kind);
    for (unsigned i = 0; i < r13_nargs[kind]; ++i) r13_mix(a[i]);
    size_t used = strlen(r13_text[side]);
    if (r13_count[side] < 14u && used + 40u < sizeof(r13_text[side]))
        snprintf(r13_text[side] + used, sizeof(r13_text[side]) - used, " %u(%lx,%lx)", kind, (unsigned long)a[0],
                 (unsigned long)(r13_nargs[kind] > 1u ? a[1] : 0));
    ++r13_count[side];
    if (side) ++r13_kinds[kind];
    r13_ecx_out = r13_ecx[kind][k & 3u];
    return (uint64_t)r13_edx[kind][k & 3u] << 32 | r13_eax[kind][k & 3u];
}
/* trampoline N: EAX = N, then r13_record(N, the caller's arguments); ECX
 * the scripted value. */
#define R13_T(n) "\n.globl _r13_t" #n "\n_r13_t" #n ":\n\tmovl $" #n ", %eax\n\tjmp _r13_tcommon\n"
__asm__(".text\n" R13_T(1) R13_T(2) R13_T(3) R13_T(4)
        "_r13_tcommon:\n\tleal 4(%esp), %edx\n\tpushl %edx\n\tpushl %eax\n\tcall _r13_record\n\taddl $8, %esp\n"
        "\tmovl _r13_ecx_out, %ecx\n\tret\n");
#undef R13_T
extern void r13_t1(void), r13_t2(void), r13_t3(void), r13_t4(void);
static void (*const r13_tramp[R13_KINDS])(void) = {NULL, r13_t1, r13_t2, r13_t3, r13_t4};
static uint32_t r13_t(unsigned kind) { return (uint32_t)(uintptr_t)r13_tramp[kind]; }
static void r13_s_stub(es2_sim_service_call *sc, void *user) {
    uint64_t v = r13_record((unsigned)(uintptr_t)user, sc->slots);
    sc->eax = (uint32_t)v;
    sc->edx = (uint32_t)(v >> 32);
    sc->ecx = r13_ecx_out;
}
#define R13_PATCHED 4u
static const uint32_t r13_patched[R13_PATCHED] = {0, 0x0049681au, 0x0049667cu, 0x0046e498u};
static BYTE r13_saved[R13_PATCHED][5];
static int r13_patch(uint32_t va, uint32_t target, BYTE saved[5], int install) {
    BYTE *site = mapped + (va - 0x400000u);
    DWORD previous;
    if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &previous)) return fail("renderer leaves: stub protection");
    if (install) { memcpy(saved, site, 5); site[0] = 0xe9; put32(site + 1, target - (uint32_t)(uintptr_t)(site + 5)); }
    else memcpy(site, saved, 5);
    if (!VirtualProtect(site, 5, previous, &previous)) return fail("renderer leaves: stub restore protection");
    FlushInstructionCache(GetCurrentProcess(), site, 5);
    return 0;
}
static int r13_put(uint32_t va, es2_sim_service_fn fn, void *user, unsigned slots) {
    for (unsigned i = 0; i < r13_dispatch.count; ++i) {
        es2_sim_entry *e = &r13_dispatch.entries[i];
        if (e->va != va) continue;
        e->kind = ES2_SIM_SERVICE; e->name = "renderer leaves recorder"; e->service = fn; e->service_user = user; e->pop_bytes = 0;
        e->stack_slots = (uint8_t)slots;
        return 0;
    }
    return es2_sim_dispatch_add_service(&r13_dispatch, va, "renderer leaves recorder", fn, user, 0, (uint8_t)slots);
}
static int r13_dispatch_setup(void) {
    es2_sim_dispatch_init(&r13_dispatch, r13_entries, 4096, r13_refuse, NULL);
    static es2_sim_services s;
    s.memory = &ir_memory;
    s.heap = &ir_heap;
    s.vt = &lc_vt;
    s.fs0 = NULL;
    static const struct { uint32_t va; unsigned slots; const char *name; } passed[] = {
        {0x0047eb30u, 2, "sincos_pair (original)"}, {0x0048043cu, 3, "transform_point (original)"},
    };
    if (es2_sim_register_services(&r13_dispatch, &s)) return fail("renderer leaves: dispatch");
    for (unsigned i = 0; i < sizeof(passed) / sizeof(passed[0]); ++i)
        if (es2_sim_dispatch_add_service(&r13_dispatch, passed[i].va, passed[i].name, au_original, (void *)(uintptr_t)passed[i].slots, 0,
                                         (uint8_t)passed[i].slots))
            return fail("renderer leaves: dispatch");
    for (unsigned k = 1; k < R13_KINDS; ++k)
        if (r13_put(k < R13_PATCHED ? r13_patched[k] : r13_t(k), r13_s_stub, (void *)(uintptr_t)k, r13_nargs[k])) return fail("renderer leaves: dispatch");
    const char *error = NULL;
    uint32_t bad = 0;
    if (es2_sim_dispatch_seal(&r13_dispatch, &error, &bad)) return fail("renderer leaves: dispatch seal");
    return 0;
}

/* ---- the case script */
static uint32_t r13_rand(uint32_t n) { return random32() % n; }
static int32_t r13_ri(int32_t lo, int32_t hi) { return lo + (int32_t)r13_rand((uint32_t)(hi - lo + 1)); }
static uint32_t r13_at(uint32_t offset) { return (uint32_t)(uintptr_t)r13_world + offset; }
static void r13_w32(uint32_t offset, uint32_t v) { put32(r13_world_start + offset, v); }
static void r13_w16(uint32_t offset, uint32_t v) { put16(r13_world_start + offset, (uint16_t)v); }
typedef struct r13_preset { uint32_t va, value; unsigned width; } r13_preset;
static r13_preset r13_presets[16];
static unsigned r13_preset_count;
static void r13_set(uint32_t va, uint32_t value, unsigned width) {
    if (r13_preset_count < 16u) r13_presets[r13_preset_count++] = (r13_preset){va, value, width};
}
typedef struct r13_op { uint32_t va, a[5], in[3]; unsigned n; } r13_op;
static r13_op r13_op_now;
static uint32_t r13_res[2][3];
static void r13_common(void) {
    random_bytes(r13_world_start, R13_WORLD_SIZE);
    r13_preset_count = 0;
    for (unsigned k = 0; k < R13_KINDS; ++k)
        for (unsigned i = 0; i < 4u; ++i) {
            r13_eax[k][i] = random32();
            r13_edx[k][i] = random32();
            r13_ecx[k][i] = random32();
        }
    for (unsigned i = 0; i < 4u; ++i) r13_eax[3][i] = r13_rand(3u) ? r13_at(R13_FACE) : 0u;   /* 0x46e498: a face or none */
    r13_w32(R13_S, r13_at(R13_VT));
    r13_w32(R13_VT + 0x18u, r13_t(4));
}
static void r13_side(unsigned side) {
    lc_reset(side, 47, 0x01000000u, 1);
    lc_loop.dispatch = &r13_dispatch;
    es2_sim_load_headless(&r13_load, &lc_loop);
    es2_sim_load_bind(&r13_load);
    memset(&r13_view, 0, sizeof(r13_view));
    r13_view.loop = &lc_loop;
    es2_sim_view_bind(&r13_view);
    memset(&r13_terrain, 0, sizeof(r13_terrain));
    r13_terrain.loop = &lc_loop;
    es2_sim_terrain_draw_bind(&r13_terrain);
    memcpy(r13_world, r13_world_start, R13_WORLD_SIZE);
    for (unsigned i = 0; i < r13_preset_count; ++i) {
        BYTE *p = mapped + (r13_presets[i].va - 0x400000u);
        if (r13_presets[i].width == 4) put32(p, r13_presets[i].value);
        else *p = (BYTE)r13_presets[i].value;
    }
    r13_side_now = side;
    r13_hash[side] = 0xcbf29ce484222325ull;
    r13_count[side] = 0;
    r13_text[side][0] = 0;
    memset(r13_calls[side], 0, sizeof(r13_calls[side]));
}
/* The own side first: a refused projection (the original's #DE) skips the
 * original. Returns 0 identical, 1 a difference, 2 skipped. */
static int r13_run(const char *what, unsigned k) {
    r13_op *o = &r13_op_now;
    r13_fault = 0;
    for (unsigned n = 0; n < 2u; ++n) {
        unsigned side = 1u - n;
        r13_side(side);
        uint32_t out[3] = {0, 0, 0};
        static char where[200];
        snprintf(where, sizeof(where), "renderer leaves (%s side %u: 0x%08lx(0x%lx, 0x%lx))", what, side, (unsigned long)o->va,
                 (unsigned long)o->a[0], (unsigned long)o->a[1]);
        current_function = where;
        if (!side) au_call((uint32_t)(uintptr_t)(mapped + (o->va - 0x400000u)), o->a, o->n, o->in, out);
        else {
            es2_sim_result r;
            memset(&r, 0, sizeof(r));
            int refused = es2_sim_call(&r13_dispatch, o->va, o->a, o->n, o->in, &r);
            if (r13_fault) return 2;
            if (refused) r13_bad = 1;
            out[0] = r.eax;
            out[1] = r.edx;
            out[2] = r.ecx;
        }
        memcpy(r13_res[side], out, sizeof(out));
        if (r13_bad) { fprintf(stderr, "FAIL: renderer leaves %s case %u (0x%08lx): a call failed\n", what, k, (unsigned long)o->va); return 1; }
        if (side) memcpy(r13_world_start + R13_WORLD_SIZE, r13_world, R13_WORLD_SIZE);
        su_take(side);
    }
    if (r13_hash[0] != r13_hash[1] || r13_count[0] != r13_count[1]) {
        fprintf(stderr, "FAIL: renderer leaves %s case %u: recorded calls differ (%u / %u)\n  original:%s\n  own:     %s\n", what, k, r13_count[0],
                r13_count[1], r13_text[0], r13_text[1]);
        return 1;
    }
    if (su_compare(what, k)) return 1;
    if (memcmp(r13_world, r13_world_start + R13_WORLD_SIZE, R13_WORLD_SIZE)) {
        for (unsigned i = 0, shown = 0; i < R13_WORLD_SIZE && shown < 16u; ++i)
            if (r13_world[i] != r13_world_start[R13_WORLD_SIZE + i]) {
                fprintf(stderr, "  world +0x%04x original %02x own %02x\n", i, r13_world[i], r13_world_start[R13_WORLD_SIZE + i]);
                ++shown;
            }
        fprintf(stderr, "FAIL: renderer leaves %s case %u: the world differs (world at 0x%08lx)\n", what, k, (unsigned long)(uintptr_t)r13_world);
        return 1;
    }
    for (unsigned i = 0; i < 3u; ++i)
        if (r13_res[0][i] != r13_res[1][i]) {
            static const char *const names[] = {"EAX", "EDX", "ECX"};
            fprintf(stderr, "FAIL: renderer leaves %s case %u (0x%08lx): %s original 0x%08lx own 0x%08lx\n", what, k, (unsigned long)o->va, names[i],
                    (unsigned long)r13_res[0][i], (unsigned long)r13_res[1][i]);
            return 1;
        }
    return 0;
}
static void r13_op_set(uint32_t va, unsigned n, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4) {
    r13_op_now = (r13_op){va, {a0, a1, a2, a3, a4}, {random32(), random32(), random32()}, n};
}

/* ---- the grid: w + h (masked) at most 6, so every mark lies in R13_MARKS */
static uint32_t r13_gw, r13_gh, r13_gs;
static void r13_grid(void) {
    r13_gw = r13_rand(4u);
    r13_gh = r13_rand(4u);
    if (r13_rand(12u) == 0) r13_gw += 32u;              /* SHL's count masked */
    if (r13_rand(12u) == 0) { r13_gw = 28u + r13_rand(4u); r13_gh = 36u - r13_gw + r13_rand(3u); }
    r13_gs = r13_rand(7u);
    if (r13_rand(16u) == 0) r13_gs += 32u;
    r13_w32(R13_G + 0x100u, r13_gw);
    r13_w32(R13_G + 0x104u, r13_gh);
    r13_w32(R13_G + 0x108u, r13_gs);
    r13_w32(R13_G + 0xecu, r13_at(R13_CELLS));
    r13_w32(R13_G + 0xf0u, r13_at(R13_MARKS));
    r13_w32(R13_G + 0xccu, r13_at(R13_POLY));
}
static int32_t r13_dim(uint32_t n) { n &= 31u; return n > 4u ? 16 : (int32_t)(1u << n); }
/* A coordinate in cell c (shift s) with a random offset in the cell. */
static uint32_t r13_coord(int32_t cell) {
    uint32_t s = r13_gs & 31u;
    return ((uint32_t)cell << s) + (s ? r13_rand(1u << s) : 0u);
}

/* 0x46e62c, 0x46e8c4, 0x46e944, 0x470db8 */
static unsigned r13_cov_q[4], r13_cov_moved, r13_cov_face, r13_cov_hit, r13_cov_miss, r13_cov_op[24];
static int r13_grid_case(unsigned k) {
    r13_common();
    r13_grid();
    int32_t gw = r13_dim(r13_gw), gh = r13_dim(r13_gh);
    int32_t cx = r13_ri(-1, gw), cy = r13_ri(-1, gh), ex = cx + r13_ri(-2, 2), ey = cy + r13_ri(-2, 2);
    unsigned which = r13_rand(10u) < 7u ? 0u : 1u + r13_rand(3u);
    if (which == 0) {
        r13_w32(R13_P, r13_coord(cx));
        r13_w32(R13_P + 4u, r13_coord(cy));
        r13_w32(R13_P + 8u, (uint32_t)r13_ri(-0x4000, 0x4000));
        if (r13_rand(20u) == 0) r13_w32(R13_P, random32());
        r13_set(0x006b4f44u, r13_coord(ex), 4);
        r13_set(0x006b4f48u, r13_coord(ey), 4);
        r13_set(0x006b4f4cu, (uint32_t)r13_ri(-0x8000, 0x8000), 4);
        uint32_t s = r13_gs & 31u;
        uint32_t size = r13_rand(4u) ? r13_rand((1u << s) + 2u) : random32();
        int32_t count = r13_rand(10u) ? (int32_t)r13_rand(8u) : -(int32_t)r13_rand(3u);
        r13_w32(R13_G + 0xc8u, (uint32_t)count);
        for (unsigned i = 0; i < 8u; ++i) {
            r13_w32(R13_POLY + 8u * i, (uint32_t)(cx + r13_ri(-2, 2)));
            r13_w32(R13_POLY + 8u * i + 4u, (uint32_t)(cy + r13_ri(-2, 2)));
        }
        for (unsigned i = 0; i < 3u; ++i) r13_w16(R13_FACE + 2u * i, (uint32_t)r13_ri(-0x4000, 0x4000));
        unsigned q = (ex < cx && cy > ey) ? 0u : (ex >= cx && cy > ey) ? 1u : (ex >= cx && cy <= ey) ? 2u : 3u;
        r13_op_set(0x0046e62cu, 3, r13_at(R13_G), r13_at(R13_P), size, 0, 0);
        int bad = r13_run("grid at", k);
        if (!bad) {
            ++r13_cov_q[q];
            if (r13_calls[1][3]) { ++r13_cov_moved; if (r13_eax[3][0]) ++r13_cov_face; }
            if (r13_res[0][0]) ++r13_cov_hit; else ++r13_cov_miss;
            ++r13_cov_op[0];
        }
        return bad;
    }
    if (which == 1) {
        int32_t col = r13_ri(-2, gw + 1), row = r13_ri(-2, gh + 1);
        r13_w16(R13_W, (uint32_t)col);
        r13_w16(R13_W + 2u, (uint32_t)row);
        if (r13_rand(10u) == 0) r13_w16(R13_W, random32());
        r13_op_set(0x0046e8c4u, 2, r13_at(R13_G), r13_at(R13_W), 0, 0, 0);
        int bad = r13_run("grid cell", k);
        if (!bad) { ++r13_cov_op[1]; if (r13_res[0][0]) ++r13_cov_op[20]; }
        return bad;
    }
    if (which == 2) {
        r13_op_set(0x0046e944u, 1, r13_at(R13_G), 0, 0, 0, 0);
        int bad = r13_run("grid begin", k);
        if (!bad) ++r13_cov_op[2];
        return bad;
    }
    uint32_t cell = r13_at(R13_CELLS) + 16u * r13_rand(64u) + r13_rand(16u);
    r13_op_set(0x00470db8u, 2, r13_at(R13_G), cell, 0, 0, 0);
    int bad = r13_run("mark add", k);
    if (!bad) ++r13_cov_op[3];
    return bad;
}

/* 0x470a1c */
static unsigned r13_cov_inside[2], r13_cov_shift0, r13_cov_fault;
static int r13_horizon_case(unsigned k) {
    r13_common();
    r13_grid();
    int32_t gw = r13_dim(r13_gw), gh = r13_dim(r13_gh);
    int32_t edge = r13_rand(8u) ? r13_ri(0, 3) : (int32_t)random32();
    r13_w32(R13_G + 0x10cu, (uint32_t)edge);
    r13_w32(R13_G + 0x110u, (uint32_t)r13_ri(-0x10000, 0x10000));
    int32_t cx = r13_ri(-1, gw), cy = r13_ri(-1, gh);
    r13_w32(R13_CAM + 4u, r13_coord(cx));
    r13_w32(R13_CAM + 8u, r13_coord(cy));
    if (r13_rand(16u) == 0) r13_w32(R13_CAM + 4u, random32());
    for (unsigned i = 0; i < 9u; ++i) r13_w16(R13_VIEW + 2u * i, (uint32_t)r13_ri(-0x4000, 0x4000));
    r13_world_start[R13_VIEW + 0x12u] = (BYTE)r13_rand(3u);
    for (unsigned i = 0; i < 3u; ++i) r13_w32(R13_VIEW + 0x14u + 4u * i, (uint32_t)r13_ri(-0x40000, 0x40000));
    uint32_t shift = r13_rand(3u) ? r13_rand(12u) : 0u;
    r13_set(0x006c5ff4u, r13_at(R13_VIEW), 4);
    r13_set(0x006c6028u, shift, 4);
    r13_set(0x006c6030u, r13_rand(30u) ? (uint32_t)r13_ri(1, 0x400) : 0u, 4);
    r13_set(0x006c6034u, (uint32_t)r13_ri(0, 640), 4);
    r13_set(0x006c6038u, (uint32_t)r13_ri(0, 480), 4);
    r13_op_set(0x00470a1cu, 3, r13_at(R13_G), r13_at(R13_CAM), r13_at(R13_OUT), 0, 0);
    int bad = r13_run("horizon point", k);
    if (bad == 2) { ++r13_cov_fault; return 0; }
    if (!bad) {
        uint32_t s = r13_gs & 31u;
        int32_t ix = (int32_t)get32(r13_world_start + R13_CAM + 4u), iy = (int32_t)get32(r13_world_start + R13_CAM + 8u);
        ix = ix < 0 ? ~(~ix >> s) : ix >> s;
        iy = iy < 0 ? ~(~iy >> s) : iy >> s;
        int inside = ix >= edge && ix < (int32_t)((1u << (r13_gw & 31u)) - (uint32_t)edge) && edge <= iy &&
                     (int32_t)((1u << (r13_gh & 31u)) - (uint32_t)edge) > iy;
        ++r13_cov_inside[inside];
        if (!shift) ++r13_cov_shift0;
        ++r13_cov_op[4];
    }
    return bad;
}

/* 0x428efc, 0x48de70, 0x48de78, 0x4758b4, 0x42eb5c, 0x42ebbc, 0x476940,
 * 0x476aac, the stream thunks 0x48f3ea, 0x490874, 0x491171, 0x49118c,
 * 0x492430, 0x49245e and 0x4930ab */
static int r13_leaf_case(unsigned k) {
    r13_common();
    r13_set(0x006cbc04u, r13_rand(2u), 1);
    unsigned which = r13_rand(15u);
    static const uint32_t vas[15] = {0x00428efcu, 0x0048de70u, 0x0048de78u, 0x004758b4u, 0x0042eb5cu, 0x0042ebbcu, 0x00476940u, 0x00476aacu,
                                     0x0048f3eau, 0x00490874u, 0x00491171u, 0x0049118cu, 0x00492430u, 0x0049245eu, 0x004930abu};
    uint32_t len = r13_rand(3u) ? r13_rand(16u) : random32();
    switch (which) {
    case 0: r13_op_set(vas[0], 1, random32(), 0, 0, 0, 0); break;
    case 1: r13_op_set(vas[1], 0, 0, 0, 0, 0, 0); break;
    case 5: r13_op_set(vas[5], 4, r13_at(R13_OBJ), random32(), random32(), random32(), 0); break;
    case 8: case 10: case 13: r13_op_set(vas[which], 3, r13_at(R13_S), len, r13_at(R13_DST), 0, 0); break;
    case 9: case 11: case 12: r13_op_set(vas[which], 2, r13_at(R13_S), r13_at(R13_DST), 0, 0, 0); break;
    case 14: r13_op_set(vas[14], 1, r13_at(R13_S), 0, 0, 0, 0); break;
    default: r13_op_set(vas[which], 1, r13_at(R13_OBJ), 0, 0, 0, 0); break;
    }
    int bad = r13_run("leaf", k);
    if (!bad) ++r13_cov_op[5u + which];
    return bad;
}

static int renderer_leaves_cases(void) {
    current_function = "renderer leaves";
    es2_sim_identity_memory(&ir_memory);
    es2_sim_rtl_memory(&ir_memory, &su_rtl);
    su_data_pristine = malloc(SU_DATA_SIZE);
    su_snap[0] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    su_snap[1] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    ir_region = ww_alloc(IR_REGION_SIZE);
    lc_stack = VirtualAlloc(NULL, 0x10000u, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    lc_bits = ww_alloc(LC_BITS_SIZE);
    r13_world = VirtualAlloc(NULL, R13_WORLD_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    r13_world_start = malloc(2u * R13_WORLD_SIZE);
    if (!su_data_pristine || !su_snap[0] || !su_snap[1] || !ir_region || !lc_stack || !lc_bits || !r13_world || !r13_world_start)
        return fail("renderer leaves: memory");
    if (get32(mapped + (SU_COUNTER - 0x400000u))) return fail("renderer leaves: the counter word is not free");
    su_pristine_take();
    if (r13_dispatch_setup()) return 1;
    for (unsigned i = 1; i < R13_PATCHED; ++i) if (r13_patch(r13_patched[i], r13_t(i), r13_saved[i], 1)) return 1;
    int bad = 0;
    static const struct { const char *name; int (*run)(unsigned); unsigned cases; } parts[] = {
        {"grid", r13_grid_case, 4000}, {"horizon point", r13_horizon_case, 2000}, {"leaves", r13_leaf_case, 3000},
    };
    /* Each part is one test shard's (suite_part_in_shard) and starts from its
     * own seed, so its cases do not depend on the shard layout. The coverage
     * line counts the shard's own parts. */
    for (unsigned p = 0; !bad && p < sizeof(parts) / sizeof(parts[0]); ++p) {
        if (!suite_part_in_shard(p)) continue;
        random_state = 0x13d0e7a5u ^ (p * 0x9e3779b9u);
        for (unsigned k = 0; !bad && k < parts[p].cases; ++k) {
            current_case = k; current_case_valid = 1;
            bad = parts[p].run(k);
        }
        if (!bad) printf("PASS: renderer leaves %s: %u cases identical\n", parts[p].name, parts[p].cases);
    }
    for (unsigned i = 1; i < R13_PATCHED; ++i) if (r13_patch(r13_patched[i], r13_t(i), r13_saved[i], 0)) bad = 1;
    if (!bad) {
        printf("renderer leaves coverage: grid at quadrants %u/%u/%u/%u, moved %u (face %u), record/none %u/%u; horizon inside/edge %u/%u, "
               "shift 0 %u, projection faults skipped %u\n  per row:",
               r13_cov_q[0], r13_cov_q[1], r13_cov_q[2], r13_cov_q[3], r13_cov_moved, r13_cov_face, r13_cov_hit, r13_cov_miss,
               r13_cov_inside[1], r13_cov_inside[0], r13_cov_shift0, r13_cov_fault);
        for (unsigned i = 0; i < 21u; ++i) printf(" %u", r13_cov_op[i]);
        printf("; calls:");
        for (unsigned i = 1; i < R13_KINDS; ++i) printf(" %u", r13_kinds[i]);
        printf("\n");
    }
    current_case_valid = 0;
    put32(mapped + 0x2cbee4u, 0);
    memcpy(mapped + SU_DATA_RVA, su_data_pristine, SU_DATA_SIZE);
    return bad;
}
