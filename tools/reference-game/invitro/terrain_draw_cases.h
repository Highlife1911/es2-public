/* The terrain renderer, original machine code against sim/src/terrain_draw.c
 * (--terrain-draw; target :differential_terrain_draw_test). Included after
 * gfx_cases.h; reuses startup_cases.h's DATA snapshots (su_take, su_compare),
 * loop_cases.h's loop set-up (lc_reset) and audio_cases.h's register-capturing
 * machine call (au_call).
 *
 * Every case builds a synthetic terrain grid (2..4 bits per side, random cell
 * heights from a small set so that splits tie, random normals, shades, texture
 * bytes and marks), its height cache, the texture tables, a camera and a
 * graphics context in an identity-mapped buffer and presets the module's DATA
 * words (the fill and shade modes, the near distance, the walk order flags, the
 * eye, the view matrices), then runs the same calls on both sides: side 0 the
 * original's code, side 1 the own rows through a dispatch of the sim's real
 * services. The fills (0x4680e0, 0x4680f0, 0x46bc44, 0x468760, 0x46817c), the
 * scene's 0x428bc0 and the grid's cell object callback are recorders on both
 * sides (side 0: a jump patched over the entry; side 1: a row at the same VA):
 * every call is logged with its arguments and the buffers it receives (the
 * polygon points, the coordinates, the shade word or dword where the original
 * wrote it) into a hash, so the walk order, the cell selection, the split, the
 * texture choice and the fill calls are compared call by call. The registry
 * hooks the module calls (poly_clip, poly_spans, view_rect, cell_uv, the cell
 * flags, rotate_point, transform_point, line_plane) run as the original's
 * machine code on the own side; the frustum library (0x4949a4, 0x4949e4,
 * 0x494e58) is the own rows of sim/src/frustum.c. Compared after the sequence:
 * EAX (EDX/ECX where the contract names them), the recorder logs, the world and
 * DATA. Masked: the four shade dwords 0x6b4f6c (the original fills them from
 * uninitialised stack words; the one it writes is logged by the recorder) and
 * EAX where the original leaves it undefined.
 *
 * - leaves: 8 random calls of the small rows and the cell level
 *   (0x49534c, 0x470d94, 0x46be9c, 0x46e5a4, 0x4724b8, 0x4724e0, 0x471f30,
 *   0x46d448 with every fill and shade mode, 0x46dddc and 0x46dfa8 in
 *   both step directions with the eye flag).
 * - walk: 0x46d1a8 over a random convex view polygon on the grid with the
 *   eye inside or outside it, both walkers (the heading word) and both
 *   row orders (0x49f248, 0x49aad4), the visible byte on or off.
 * - draw: 0x46cb9c with a given polygon (clamped to the grid, visible or
 *   not, the shift above 14 or not) or the frustum's polygon, then the
 *   walk 0x46d1a8. */
#pragma once
/* The headers this suite builds on (sim_main.c). */
#include "init_records_cases.h"
#include "startup_cases.h"
#include "loop_cases.h"
#include "audio_cases.h"
#include "es2/sim/load.h"
#include "es2/sim/view.h"
#include "es2/sim/input.h"
#include "es2/sim/terrain_draw.h"
#include "es2/sim/frustum.h"

#define TD_WORLD_SIZE 0x4000u
/* World layout (offsets). */
#define TD_G 0x0000u        /* the grid, 0x200 */
#define TD_CAM 0x0200u      /* the camera, 0x40 */
#define TD_CTX 0x0240u      /* the graphics context (+0x22c) */
#define TD_Q 0x0480u        /* the polygon {count, points} given to 0x46cb9c */
#define TD_QPTS 0x0490u     /* its points, 8 x 8 */
#define TD_HV 0x04d0u       /* the height vector */
#define TD_M 0x04e0u        /* the matrix [0x6c5ff4] */
#define TD_PT 0x0520u       /* a point pair and an eye for the turns */
#define TD_QUAD 0x0540u     /* a corner quad for the direct cell draw */
#define TD_N 0x0580u        /* a normal and a point for the facing test */
#define TD_UVT 0x0600u      /* the coordinate records [0x6b4f40], 8 x 0x14 */
#define TD_TEX 0x0700u      /* the texture entries [G+0x121], 64 x 8 */
#define TD_FLAGS 0x0900u    /* the height cache's flags, 0x100 */
#define TD_ENTRIES 0x0a00u  /* its entries, 0x100 x 12 */
#define TD_MARKS 0x1600u    /* the marks, 0x100 */
#define TD_WPTS 0x1700u     /* the walk polygon's points */
#define TD_LINES 0x1800u    /* the line callbacks' records, 8 x 0x20 */
#define TD_CELLS 0x2000u    /* the cells, 0x100 x 16 */
static BYTE *td_world, *td_world_start;
static es2_sim_entry td_entries[4096];
static es2_sim_dispatch td_dispatch;
static es2_sim_load td_load;
static es2_sim_view td_view;
static es2_sim_input td_input;   /* the vector constructor 0x49667c is the input layer's row */
static es2_sim_terrain_draw td_terrain;
static es2_sim_frustum td_frustum;   /* the frustum library's rows */
static int td_bad;
static void td_refuse(const es2_sim_refusal *r, void *user) {
    (void)user;
    fprintf(stderr, "FAIL: own terrain: refused call to 0x%08lx (%s): %s\n", (unsigned long)r->va, r->name ? r->name : "?",
            r->reason ? r->reason : "");
    td_bad = 1;
}

/* ---- the recorders */
static uint64_t td_hash[2];
static unsigned td_count[2], td_side_now;
static char td_text[2][512];
static unsigned td_kinds[8], td_n4, td_shade_words;
static void td_mix(uint32_t v) {
    for (unsigned i = 0; i < 4u; ++i) { td_hash[td_side_now] ^= (BYTE)(v >> (8u * i)); td_hash[td_side_now] *= 0x100000001b3ull; }
}
static void td_bytes(uint32_t va, uint32_t n) { for (uint32_t i = 0; i < n; i += 4u) td_mix(get32((BYTE *)(uintptr_t)(va + i))); }
static void td_note(unsigned kind, const uint32_t *a, unsigned n) {
    unsigned side = td_side_now;
    td_mix(0xabc00000u + kind);
    for (unsigned i = 0; i < n; ++i) td_mix(a[i]);
    size_t used = strlen(td_text[side]);
    if (td_count[side] < 12u && used + 40u < sizeof(td_text[side]))
        snprintf(td_text[side] + used, sizeof(td_text[side]) - used, " %u(%lx,%lx)", kind, (unsigned long)(n ? a[0] : 0),
                 (unsigned long)(n > 1u ? a[1] : 0));
    ++td_count[side];
    if (side) ++td_kinds[kind];
}
/* 0x46bc44(points, n, &shade): the shade word only where the original
 * wrote it (the fill mode 0, the shade mode below 2). */
static void td_flat(uint32_t pts, uint32_t n, uint32_t shade) {
    uint32_t a[3] = {pts, n, get32(mapped + 0x0aaaacu) < 2u ? get16((BYTE *)(uintptr_t)shade) : 0u};
    td_note(3, a, 3);
    td_bytes(pts, 12u * n);
    if (td_side_now && n == 4u) ++td_n4;
}
/* 0x468760/0x46817c(points, coords, n, texture, shades, mode, 1, 0): the
 * first shade dword only where it is written (the shade mode 1). */
static void td_textured(unsigned kind, const uint32_t *a) {
    uint32_t b[9];
    memcpy(b, a, 8u * sizeof(uint32_t));
    b[8] = a[5] == 1u ? get32((BYTE *)(uintptr_t)a[4]) : 0u;
    if (td_side_now && a[5] == 1u) ++td_shade_words;
    td_note(kind, b, 9);
    td_bytes(a[0], 12u * a[2]);
    td_bytes(a[1], 8u * a[2]);
    if (td_side_now && a[2] == 4u) ++td_n4;
}
/* Side 0: the original's calls land here (cdecl, a jump over the entry). */
static uint32_t __cdecl td_o_far(uint32_t a) { td_note(1, &a, 1); return 0; }
static uint32_t __cdecl td_o_haze(uint32_t a) { td_note(2, &a, 1); return 0; }
static uint32_t __cdecl td_o_flat(uint32_t p, uint32_t n, uint32_t s) { td_flat(p, n, s); return 0; }
static uint32_t __cdecl td_o_near(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7) {
    const uint32_t a[8] = {a0, a1, a2, a3, a4, a5, a6, a7};
    td_textured(4, a);
    return 0;
}
static uint32_t __cdecl td_o_textured(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7) {
    const uint32_t a[8] = {a0, a1, a2, a3, a4, a5, a6, a7};
    td_textured(5, a);
    return 0;
}
static uint32_t __cdecl td_o_distances(uint32_t a) { td_note(6, &a, 1); return 0; }
/* The grid's cell object callback [G+0x125](x, y, r): a host function on
 * both sides (side 1: a row at its address). */
static uint32_t __cdecl td_o_event(uint32_t x, uint32_t y, uint32_t r) {
    const uint32_t a[3] = {x, y, r};
    td_note(7, a, 3);
    return 0;
}
/* Side 1: the same recorders as rows. */
static void td_s_stub(es2_sim_service_call *sc, void *user) {
    const uint32_t *s = sc->slots;
    switch ((unsigned)(uintptr_t)user) {
    case 1: td_o_far(s[0]); break;
    case 2: td_o_haze(s[0]); break;
    case 3: td_flat(s[0], s[1], s[2]); break;
    case 4: td_textured(4, s); break;
    case 5: td_textured(5, s); break;
    case 6: td_o_distances(s[0]); break;
    default: td_o_event(s[0], s[1], s[2]); break;
    }
    sc->eax = 0;
}
static const struct { uint32_t va; void *stub; unsigned kind, slots; } td_stubs[] = {
    {0x004680e0u, (void *)td_o_far, 1, 1}, {0x004680f0u, (void *)td_o_haze, 2, 1}, {0x0046bc44u, (void *)td_o_flat, 3, 3},
    {0x00468760u, (void *)td_o_near, 4, 8}, {0x0046817cu, (void *)td_o_textured, 5, 8}, {0x00428bc0u, (void *)td_o_distances, 6, 1},
};
#define TD_STUBS (sizeof(td_stubs) / sizeof(td_stubs[0]))
static BYTE td_saved[TD_STUBS][5];
static int td_patch(uint32_t va, void *target, BYTE saved[5], int install) {
    BYTE *site = mapped + (va - 0x400000u);
    DWORD previous;
    if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &previous)) return fail("terrain: stub protection");
    if (install) { memcpy(saved, site, 5); site[0] = 0xe9; put32(site + 1, (uint32_t)(uintptr_t)target - (uint32_t)(uintptr_t)(site + 5)); }
    else memcpy(site, saved, 5);
    if (!VirtualProtect(site, 5, previous, &previous)) return fail("terrain: stub restore protection");
    FlushInstructionCache(GetCurrentProcess(), site, 5);
    return 0;
}
/* The original's machine code as a row (up to six arguments). */
static void td_original(es2_sim_service_call *sc, void *user) {
    unsigned n = (unsigned)(uintptr_t)user;
    uint32_t a[6] = {0, 0, 0, 0, 0, 0}, in[3] = {sc->eax, sc->edx, sc->ecx}, out[3];
    for (unsigned i = 0; i < n; ++i) a[i] = sc->slots[i];
    au_call((uint32_t)(uintptr_t)(mapped + (sc->va - 0x400000u)), a, n, in, out);
    sc->eax = out[0]; sc->edx = out[1]; sc->ecx = out[2];
}
/* A row at va: replaces an entry already registered (a later package's
 * row for a stubbed VA) or adds one; before the seal. */
static int td_put(uint32_t va, const char *name, es2_sim_service_fn fn, void *user, unsigned slots) {
    for (unsigned i = 0; i < td_dispatch.count; ++i) {
        es2_sim_entry *e = &td_dispatch.entries[i];
        if (e->va != va) continue;
        e->kind = ES2_SIM_SERVICE; e->name = name; e->service = fn; e->service_user = user; e->pop_bytes = 0; e->stack_slots = (uint8_t)slots;
        return 0;
    }
    return es2_sim_dispatch_add_service(&td_dispatch, va, name, fn, user, 0, (uint8_t)slots);
}
static int td_dispatch_setup(void) {
    es2_sim_dispatch_init(&td_dispatch, td_entries, 4096, td_refuse, NULL);
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
        {0x00472bccu, 5, "poly_clip (original)"}, {0x00472b14u, 3, "poly_spans (original)"},
        {0x0046d324u, 2, "view_rect (original)"}, {0x0046bdf8u, 4, "cell_uv (original)"},
        {0x00470ddcu, 4, "cell_flag_set (original)"}, {0x00470e00u, 3, "cell_flag_get (original)"},
        {0x0047e610u, 3, "line_plane (original)"},   /* the frustum clip's (the frustum library is own code here) */
    };
    if (es2_sim_register_services(&td_dispatch, &s)) return fail("terrain: dispatch");
    for (unsigned i = 0; i < sizeof(passed) / sizeof(passed[0]); ++i)
        if (td_put(passed[i].va, passed[i].name, td_original, (void *)(uintptr_t)passed[i].slots, passed[i].slots))
            return fail("terrain: dispatch");
    for (unsigned i = 0; i < TD_STUBS; ++i)
        if (td_put(td_stubs[i].va, "terrain recorder", td_s_stub, (void *)(uintptr_t)td_stubs[i].kind, td_stubs[i].slots))
            return fail("terrain: dispatch");
    if (td_put((uint32_t)(uintptr_t)td_o_event, "terrain cell object callback", td_s_stub, (void *)(uintptr_t)7u, 3))
        return fail("terrain: dispatch");
    const char *error = NULL;
    uint32_t bad = 0;
    if (es2_sim_dispatch_seal(&td_dispatch, &error, &bad)) return fail("terrain: dispatch seal");
    return 0;
}

/* ---- the case script */
typedef struct td_preset { uint32_t va, value; unsigned width; } td_preset;
static td_preset td_presets[96];
static unsigned td_preset_count;
static void td_set32(uint32_t va, uint32_t v) { td_presets[td_preset_count++] = (td_preset){va, v, 4}; }
static void td_set16(uint32_t va, uint32_t v) { td_presets[td_preset_count++] = (td_preset){va, v, 2}; }
static void td_set8(uint32_t va, uint32_t v) { td_presets[td_preset_count++] = (td_preset){va, v, 1}; }
typedef struct td_op { uint32_t va, a[4]; unsigned n, mask; } td_op;
static td_op td_ops[32];
static unsigned td_op_count;
static uint32_t td_res[2][32][3];
#define TD_EAX 1u
#define TD_EDX 2u
#define TD_ECX 4u
static void td_op_add(uint32_t va, unsigned mask, unsigned n, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3) {
    td_ops[td_op_count++] = (td_op){va, {a0, a1, a2, a3}, n, mask};
}
static uint32_t td_rand(uint32_t n) { return random32() % n; }
static uint32_t td_signed(uint32_t range) { return (uint32_t)((int32_t)td_rand(2u * range + 1u) - (int32_t)range); }
static uint32_t td_at(uint32_t offset) { return (uint32_t)(uintptr_t)td_world + offset; }
static void td_w32(uint32_t offset, uint32_t v) { put32(td_world_start + offset, v); }
static void td_w16(uint32_t offset, uint32_t v) { put16(td_world_start + offset, (uint16_t)v); }
static void td_w8(uint32_t offset, uint32_t v) { td_world_start[offset] = (BYTE)v; }
static uint32_t td_r32(uint32_t offset) { return get32(td_world_start + offset); }
static void td_vec(uint32_t offset, uint32_t range) { for (unsigned i = 0; i < 3u; ++i) td_w32(offset + 4u * i, td_signed(range)); }

static unsigned td_w, td_h, td_sh;
static void td_begin(void) {
    memset(td_world_start, 0, TD_WORLD_SIZE);
    td_preset_count = 0;
    td_op_count = 0;
}
/* The grid, its cells, marks, height cache and texture tables, the DATA
 * words of the module. */
static void td_grid(void) {
    td_w = 2u + td_rand(3u);
    td_h = 2u + td_rand(3u);
    td_sh = td_rand(4u) ? td_rand(15u) : 15u + td_rand(2u);
    uint32_t g = TD_G, cells = 1u << (td_w + td_h);
    td_vec(g + 4u, 0x4000u);
    td_vec(g + 0x10u, 0x4000u);
    td_vec(g + 0x1cu, 0x40000u);
    td_w32(g + 0xd4u, td_signed(0x400u));
    td_w32(g + 0xd8u, td_signed(0x10000u));
    td_w32(g + 0xdcu, 0x100u);
    td_w32(g + 0xe0u, td_at(TD_FLAGS));
    td_w32(g + 0xe4u, td_at(TD_ENTRIES));
    td_w32(g + 0xe8u, td_at(TD_HV));
    td_w32(g + 0xecu, td_at(TD_CELLS));
    td_w32(g + 0xf0u, td_at(TD_MARKS));
    td_vec(g + 0xf4u, 0x10000u);
    td_w32(g + 0x100u, td_w);
    td_w32(g + 0x104u, td_h);
    td_w32(g + 0x108u, td_sh);
    td_w32(g + 0x10cu, td_rand(0x100u));
    td_w32(g + 0x110u, td_signed(0x1000u));
    td_w32(g + 0x114u, td_signed(0x1000u));
    td_w32(g + 0x118u, td_signed(0x200u));
    td_w8(g + 0x11cu, 1);
    put32(td_world_start + g + 0x121u, td_at(TD_TEX));
    put32(td_world_start + g + 0x125u, (uint32_t)(uintptr_t)td_o_event);
    if (td_rand(4u)) td_vec(TD_HV, 0x20000u);
    for (unsigned i = 0; i < 0x100u; ++i) {
        td_w8(TD_FLAGS + i, td_rand(4u) == 0);
        td_vec(TD_ENTRIES + 12u * i, 0x1000u);
    }
    unsigned levels = 1u + td_rand(4u);
    for (unsigned i = 0; i < cells; ++i) {
        uint32_t c = TD_CELLS + 16u * i;
        random_bytes(td_world_start + c, 16);
        td_w8(c, td_rand(3u) ? td_rand(levels) * 37u : td_rand(0x100u));
        td_w8(TD_MARKS + i, td_rand(6u) == 0 ? 1u + td_rand(3u) : 0u);
    }
    for (unsigned i = 0; i < 64u; ++i) { td_w32(TD_TEX + 8u * i, td_rand(8u)); td_w32(TD_TEX + 8u * i + 4u, td_rand(3u) ? 0u : 1u + td_rand(4u)); }
    random_bytes(td_world_start + TD_UVT, 8u * 0x14u);
    td_set32(0x006b4f40u, td_at(TD_UVT));
    td_set32(0x004a0be0u, td_at(TD_G));
    td_set32(0x004aaaa8u, td_rand(5u) ? td_rand(2u) : 2u);
    td_set32(0x004aaaacu, td_rand(4u));
    td_set32(0x0049aad0u, td_rand(0x8000u));
    td_set32(0x0049f248u, td_rand(3u) == 0);
    td_set32(0x0049aad4u, td_rand(3u) != 0);
    td_set32(0x006c5f70u, td_at(TD_CTX));
    td_w32(TD_CTX + 0x22cu, random32());
    td_set32(0x006c60a8u, 0);
    td_set8(0x006cbc04u, 0);
    td_set8(0x004d1f3fu, td_rand(4u));
    /* The view: 0x48c444 composes [0x6c5ff4] with the matrix 0x6c5ffc. */
    for (unsigned i = 0; i < 9u; ++i) { td_w16(TD_M + 2u * i, td_signed(0x4000u)); td_set16(0x006c5ffcu + 2u * i, td_signed(0x4000u)); }
    td_vec(TD_M + 0x14u, 0x10000u);
    for (unsigned i = 0; i < 3u; ++i) td_set32(0x006c6010u + 4u * i, td_signed(0x10000u));
    td_set32(0x006c5ff4u, td_at(TD_M));
}
/* The eye in world units: the cell (cx, cy) plus random low bits. */
static void td_eye_at(uint32_t cam, int32_t cx, int32_t cy) {
    uint32_t low = td_sh ? (1u << td_sh) - 1u : 0u;
    td_w32(cam + 4u, ((uint32_t)cx << td_sh) + (random32() & low));
    td_w32(cam + 8u, ((uint32_t)cy << td_sh) + (random32() & low));
    td_w32(cam + 0xcu, td_signed(0x40000u));
}
static void td_eye_data(void) {
    for (unsigned i = 0; i < 3u; ++i) td_set32(0x006b4f44u + 4u * i, td_signed(0x80000u));
    td_set32(0x006b4f50u, td_rand((1u << td_w) + 2u) - 1u);
    td_set32(0x006b4f54u, td_rand((1u << td_h) + 2u) - 1u);
    td_set8(0x006b4f5cu, td_rand(2u));
}
static void td_side(unsigned side) {
    lc_reset(side, 47, 0x01000000u, 1);
    lc_loop.dispatch = &td_dispatch;
    es2_sim_load_headless(&td_load, &lc_loop);
    es2_sim_load_bind(&td_load);
    memset(&td_view, 0, sizeof(td_view));
    td_view.loop = &lc_loop;
    es2_sim_view_bind(&td_view);
    es2_sim_input_headless(&td_input, &lc_loop, &lc_journal[side], 640, 480);
    es2_sim_input_bind(&td_input);
    uint64_t cells = td_terrain.cells;
    memset(&td_terrain, 0, sizeof(td_terrain));
    td_terrain.loop = &lc_loop;
    td_terrain.cells = cells;
    es2_sim_terrain_draw_bind(&td_terrain);
    memset(&td_frustum, 0, sizeof(td_frustum));
    td_frustum.loop = &lc_loop;
    es2_sim_frustum_bind(&td_frustum);
    memcpy(td_world, td_world_start, TD_WORLD_SIZE);
    for (unsigned i = 0; i < td_preset_count; ++i) {
        BYTE *p = mapped + (td_presets[i].va - 0x400000u);
        if (td_presets[i].width == 4) put32(p, td_presets[i].value);
        else if (td_presets[i].width == 2) put16(p, (uint16_t)td_presets[i].value);
        else *p = (BYTE)td_presets[i].value;
    }
    td_side_now = side;
    td_hash[side] = 0xcbf29ce484222325ull;
    td_count[side] = 0;
    td_text[side][0] = 0;
}
/* The walk 0x46d1a8 after the set-up reads the cells under the view
 * polygon; a polygon point outside [0, (1 << w) - 2] x [0, (1 << h) - 2]
 * makes the original read a corner cell at NULL. Decided on side 0 after
 * the set-up, the walk is then left out on both sides and counted. */
static unsigned td_walk_skipped, td_skip_shown;
static int td_outside(void) {
    if (!td_world[TD_G + 0x11cu]) return 0;
    uint32_t n = get32(td_world + TD_G + 0xc8u), pts = get32(td_world + TD_G + 0xccu);
    for (uint32_t i = 0; i < n && i < 20u; ++i) {
        int32_t x = (int32_t)get32((BYTE *)(uintptr_t)(pts + 8u * i)), y = (int32_t)get32((BYTE *)(uintptr_t)(pts + 8u * i + 4u));
        if (x < 0 || y < 0 || x > (int32_t)(1u << td_w) - 2 || y > (int32_t)(1u << td_h) - 2) {
            if (td_skip_shown++ < 4u)
                fprintf(stderr, "NOTE: terrain draw: polygon point %u (%ld, %ld) outside the grid (%u x %u bits, %s); walk left out\n", i,
                        (long)x, (long)y, td_w, td_h, pts == (uint32_t)(uintptr_t)(td_world + TD_G + 0x28u) ? "frustum" : "given");
            return 1;
        }
    }
    return 0;
}
static int td_run(const char *what, unsigned k) {
    unsigned stop = td_op_count;
    for (unsigned side = 0; side < 2u; ++side) {
        td_side(side);
        for (unsigned i = 0; i < stop; ++i) {
            td_op *o = &td_ops[i];
            if (!side && o->va == 0x0046d1a8u && i && td_outside()) { stop = i; ++td_walk_skipped; break; }
            uint32_t in[3] = {0, 0, 0}, *out = td_res[side][i];
            static char where[400];   /* named in a fault report */
            snprintf(where, sizeof(where), "terrain_draw (%s side %u op %u: 0x%08lx(0x%lx, 0x%lx, 0x%lx, 0x%lx), grid %u x %u bits, shift %u)",
                     what, side, i, (unsigned long)o->va, (unsigned long)o->a[0], (unsigned long)o->a[1], (unsigned long)o->a[2],
                     (unsigned long)o->a[3], td_w, td_h, td_sh);
            if (o->va == 0x0046d1a8u && td_world[TD_G + 0x11cu]) {   /* the polygon walked, for a fault report */
                uint32_t n = get32(td_world + TD_G + 0xc8u), pts = get32(td_world + TD_G + 0xccu);
                size_t used = strlen(where);
                for (uint32_t j = 0; j < n && j < 12u && used + 24u < sizeof(where); ++j, used = strlen(where))
                    snprintf(where + used, sizeof(where) - used, " (%ld,%ld)", (long)(int32_t)get32((BYTE *)(uintptr_t)(pts + 8u * j)),
                             (long)(int32_t)get32((BYTE *)(uintptr_t)(pts + 8u * j + 4u)));
            }
            current_function = where;
            if (!side) au_call((uint32_t)(uintptr_t)(mapped + (o->va - 0x400000u)), o->a, o->n, in, out);
            else {
                es2_sim_result r;
                memset(&r, 0, sizeof(r));
                if (es2_sim_call(&td_dispatch, o->va, o->a, o->n, in, &r)) td_bad = 1;
                out[0] = r.eax; out[1] = r.edx; out[2] = r.ecx;
            }
            if (td_bad) { fprintf(stderr, "FAIL: terrain %s case %u op %u (0x%08lx): a call failed\n", what, k, i, (unsigned long)o->va); return 1; }
        }
        if (!side) memcpy(td_world_start + TD_WORLD_SIZE, td_world, TD_WORLD_SIZE);
        memset(mapped + (0x006b4f6cu - 0x400000u), 0, 0x10u);   /* the shade dwords: stack residue (the recorder logs the written one) */
        su_take(side);
    }
    if (td_hash[0] != td_hash[1] || td_count[0] != td_count[1]) {
        fprintf(stderr, "FAIL: terrain %s case %u: recorded calls differ (%u / %u)\n  original:%s\n  own:     %s\n", what, k, td_count[0],
                td_count[1], td_text[0], td_text[1]);
        return 1;
    }
    if (su_compare(what, k)) return 1;
    if (memcmp(td_world, td_world_start + TD_WORLD_SIZE, TD_WORLD_SIZE)) {
        for (unsigned i = 0, shown = 0; i < TD_WORLD_SIZE && shown < 16u; ++i)
            if (td_world[i] != td_world_start[TD_WORLD_SIZE + i]) {
                fprintf(stderr, "  world +0x%04x original %02x own %02x\n", i, td_world_start[TD_WORLD_SIZE + i], td_world[i]);
                ++shown;
            }
        fprintf(stderr, "FAIL: terrain %s case %u: the world differs (world at 0x%08lx)\n", what, k, (unsigned long)(uintptr_t)td_world);
        return 1;
    }
    for (unsigned i = 0; i < stop; ++i) {
        static const char *const names[3] = {"EAX", "EDX", "ECX"};
        for (unsigned j = 0; j < 3u; ++j) {
            if (!(td_ops[i].mask & (1u << j)) || td_res[0][i][j] == td_res[1][i][j]) continue;
            fprintf(stderr, "FAIL: terrain %s case %u op %u (0x%08lx): %s original 0x%08lx own 0x%08lx\n", what, k, i, (unsigned long)td_ops[i].va,
                    names[j], (unsigned long)td_res[0][i][j], (unsigned long)td_res[1][i][j]);
            return 1;
        }
    }
    return 0;
}

/* ---- leaves: the small rows and the cell level */
static unsigned td_cov_flag_cleared, td_cov_flag_kept, td_cov_columns, td_cov_rows, td_cov_simple;
/* A cell (x, y) whose four corners lie inside the grid. */
static uint32_t td_cx(void) { return td_rand((1u << td_w) - 1u); }
static uint32_t td_cy(void) { return td_rand((1u << td_h) - 1u); }
static int td_leaves_case(unsigned k) {
    td_begin();
    td_grid();
    td_eye_data();
    uint32_t g = td_at(TD_G);
    for (unsigned i = 0; i < 8u; ++i) {
        switch (td_rand(11u)) {
        case 0: td_op_add(0x0049534cu, TD_EAX, 2, g + 0xd0u, td_rand(0x100u), 0, 0); break;
        case 1: td_op_add(0x00470d94u, TD_EAX, 2, g, td_at(TD_CELLS) + 16u * td_rand(1u << (td_w + td_h)), 0, 0); break;
        case 2:
            for (unsigned j = 0; j < 3u; ++j) td_w16(TD_N + 2u * j, td_signed(0x4000u));
            td_vec(TD_N + 8u, 0x80000u);
            td_op_add(0x0046be9cu, TD_EAX, 2, td_at(TD_N), td_at(TD_N + 8u), 0, 0);
            break;
        case 3: td_op_add(0x0046e5a4u, TD_EAX, 3, g, td_rand((1u << td_w) + 2u) - 1u, td_rand((1u << td_h) + 2u) - 1u, 0); break;
        case 4: case 5:
            td_vec(TD_PT, 0x400u);
            td_vec(TD_PT + 0xcu, 0x400u);
            td_op_add(td_rand(2u) ? 0x004724b8u : 0x004724e0u, TD_EAX | TD_EDX | TD_ECX, 3, td_at(TD_PT), td_at(TD_PT + 4u), td_at(TD_PT + 0xcu), 0);
            break;
        case 6: td_op_add(0x00471f30u, TD_EAX | TD_EDX | TD_ECX, 3, td_at(0x05c0u), random32(), random32(), 0); break;   /* its own record: R keeps G */
        case 7: case 8:
            td_vec(TD_QUAD, 0x8000u); td_vec(TD_QUAD + 12u, 0x8000u); td_vec(TD_QUAD + 24u, 0x8000u); td_vec(TD_QUAD + 36u, 0x8000u);
            if (td_rand(3u) == 0) td_w32(TD_QUAD + 0x2cu, td_r32(TD_QUAD + 8u) + td_r32(TD_QUAD + 0x20u) - td_r32(TD_QUAD + 0x14u));
            td_op_add(0x0046d448u, TD_EAX, 4, td_cx(), td_cy(), g, td_at(TD_QUAD));
            break;
        default: {
            int column = td_rand(2u) != 0;
            uint32_t step = td_rand(2u) ? 1u : 0xffffffffu, line = column ? td_cx() : td_cy(), n = column ? (1u << td_h) - 1u : (1u << td_w) - 1u;
            uint32_t a = td_rand(n), b = td_rand(n);
            if (((int32_t)step > 0) != (a <= b)) { uint32_t t = a; a = b; b = t; }
            uint32_t rec = TD_LINES + 0x20u * i;   /* a record per op: the steps stay those of its own range */
            td_w32(rec + 0x18u, g);
            td_w32(rec + (column ? 0x10u : 0xcu), step);
            td_w32(rec + (column ? 0xcu : 0x10u), random32());
            if (td_rand(2u)) {   /* the eye on this line: the flag test's ranges */
                td_set32(column ? 0x006b4f50u : 0x006b4f54u, line);
                td_set32(column ? 0x006b4f54u : 0x006b4f50u, td_rand(n + 3u) - 1u);
                if (column && td_rand(2u)) td_set32(0x006b4f50u, line);
                td_set8(0x006b4f5cu, 1);
            }
            td_op_add(column ? 0x0046dfa8u : 0x0046dddcu, 0, 4, td_at(rec), line, a, b);
            break;
        }
        }
    }
    int bad = td_run("leaves", k);
    if (!bad) { if (mapped[0x2b4f5cu]) ++td_cov_flag_kept; else ++td_cov_flag_cleared; }
    return bad;
}

/* ---- walk: the walk over a view polygon */
/* A convex polygon on the grid: a trapezoid (x0, y0) (x1, y1) (x2, y1)
 * (x3, y0) with x0 < x3, x1 < x2, y0 < y1, or a triangle; in cells, or in
 * world units with stray points for the clamps. */
static unsigned td_polygon(uint32_t pts, int world) {
    uint32_t mx = (1u << td_w) - 2u, my = (1u << td_h) - 2u;
    uint32_t y0 = td_rand(my + 1u), y1 = y0 + td_rand(my + 1u - y0), x0 = td_rand(mx + 1u), x3 = x0 + td_rand(mx + 1u - x0),
             x1 = td_rand(mx + 1u), x2 = x1 + td_rand(mx + 1u - x1);
    uint32_t p[4][2] = {{x0, y0}, {x1, y1}, {x2, y1}, {x3, y0}};
    unsigned n = td_rand(4u) ? 4u : 3u;
    if (n == 3u) { p[2][0] = x2 > x0 || x0 == mx ? x2 : x0 + 1u; p[2][1] = y1; p[1][0] = x1; }   /* inside [0, mx]: the set-up clamps there */
    int32_t q[4][2];
    for (unsigned i = 0; i < n; ++i) {
        uint32_t low = world && td_sh ? random32() & ((1u << td_sh) - 1u) : 0u;
        q[i][0] = (int32_t)((p[i][0] << (world ? td_sh : 0u)) + low);
        q[i][1] = (int32_t)((p[i][1] << (world ? td_sh : 0u)) + low);
    }
    if (world) {
        /* Points beyond the grid for the clamps, moved so that the order of
         * the edges stays (the clamped polygon stays convex): the left
         * points further left, the right ones further right, a whole
         * horizontal edge up or down. */
        int32_t beyond = (int32_t)((mx + 2u) << td_sh);
        unsigned right = 2u, last = n - 1u;
        if (td_rand(5u) == 0) q[0][0] = -(int32_t)td_rand(0x40000u);
        if (td_rand(5u) == 0) q[1][0] = -(int32_t)td_rand(0x40000u);
        if (td_rand(5u) == 0) q[right][0] = beyond + (int32_t)td_rand(0x40000u);
        if (n == 4u && td_rand(5u) == 0) q[last][0] = beyond + (int32_t)td_rand(0x40000u);
        if (td_rand(5u) == 0) { int32_t y = -(int32_t)td_rand(0x40000u); q[0][1] = y; if (n == 4u) q[last][1] = y; }
        if (td_rand(5u) == 0) { int32_t y = (int32_t)((my + 2u + td_rand(4u)) << td_sh); q[1][1] = y; q[2][1] = y; }
    }
    for (unsigned i = 0; i < n; ++i) {
        td_w32(pts + 8u * i, (uint32_t)q[i][0]);
        td_w32(pts + 8u * i + 4u, (uint32_t)q[i][1]);
    }
    return n;
}
static void td_heading(void) {
    static const uint16_t edges[] = {0x2000u, 0x2001u, 0x5fffu, 0x6000u, 0x8000u, 0xa000u, 0xe000u, 0xdfffu};
    td_w16(TD_CAM + 0x14u, td_rand(3u) ? random32() : edges[td_rand(8u)]);
}
static void td_cov_walk(void) {
    uint16_t h = get16(td_world + TD_CAM + 0x14u);
    if (h == 0x8000u) h = 0x7fffu; else if ((int16_t)h < 0) h = (uint16_t)(0u - h);
    if ((int16_t)h > 0x2000 && (int16_t)h < 0x6000) ++td_cov_columns; else ++td_cov_rows;
    if (get32(mapped + 0x09f248u) || !get32(mapped + 0x09aad4u)) ++td_cov_simple;
}
static int td_walk_case(unsigned k) {
    td_begin();
    td_grid();
    uint32_t g = td_at(TD_G), cam = td_at(TD_CAM);
    td_w32(TD_G + 0xc8u, td_polygon(TD_WPTS, 0));
    td_w32(TD_G + 0xccu, td_at(TD_WPTS));
    td_eye_at(TD_CAM, (int32_t)td_rand((1u << td_w) + 4u) - 2, (int32_t)td_rand((1u << td_h) + 4u) - 2);
    for (unsigned i = 0; i < 3u; ++i) td_set32(0x006b4f44u + 4u * i, td_r32(TD_CAM + 4u + 4u * i));
    td_heading();
    int visible = td_rand(8u) != 0;
    td_w8(TD_G + 0x11cu, (uint32_t)visible);
    td_op_add(0x0046d1a8u, visible ? TD_EAX : 0u, 2, g, cam, 0, 0);
    int bad = td_run("walk", k);
    if (!bad && visible) td_cov_walk();
    return bad;
}

/* ---- draw: the frame's set-up, then the walk */
static unsigned td_cov_given, td_cov_frustum, td_cov_hidden;
static int td_draw_case(unsigned k) {
    td_begin();
    td_grid();
    uint32_t g = td_at(TD_G), cam = td_at(TD_CAM), q = 0;
    td_eye_at(TD_CAM, (int32_t)td_rand((1u << td_w) + 4u) - 2, (int32_t)td_rand((1u << td_h) + 4u) - 2);
    /* The camera as the frustum library reads it (0x4949e4 through
     * 0x495157/0x49516d/0x495162/0x495178): +4 the position, +0x10 the
     * three angle words (+0x14 the heading), +0x16 the graphics context
     * (+0x210..+0x21c the view rectangle, +0x220/+0x224 its origin), +0x1a
     * the projection shift. */
    random_bytes(td_world_start + TD_CAM + 0x10u, 4);
    td_heading();
    td_w32(TD_CAM + 0x16u, td_at(TD_CTX));
    td_w32(TD_CAM + 0x1au, 7u + td_rand(4u));
    uint32_t vw = 160u + td_rand(481u), vh = 100u + td_rand(381u);
    td_w32(TD_CTX + 0x210u, 0);
    td_w32(TD_CTX + 0x214u, 0);
    td_w32(TD_CTX + 0x218u, vw - 1u);
    td_w32(TD_CTX + 0x21cu, vh - 1u);
    td_w32(TD_CTX + 0x220u, 0u - vw / 2u - td_rand(9u));
    td_w32(TD_CTX + 0x224u, 0u - vh / 2u - td_rand(9u));
    if (td_rand(4u)) {
        q = td_at(TD_Q);
        td_w32(TD_Q, td_polygon(TD_QPTS, 1));
        td_w32(TD_Q + 4u, td_at(TD_QPTS));
    } else {
        /* The frustum's polygon lies in the rectangle 0x46d324 builds from the
         * grid origin, and the set-up shifts its points without subtracting the
         * origin: on a grid whose origin x, y are not 0 the walk reaches cells
         * outside the grid (the original faults), so the origin x, y is 0 here,
         * as the zone load sets it. */
        td_w32(TD_G + 0xf4u, 0);
        td_w32(TD_G + 0xf8u, 0);
    }
    td_op_add(0x0046cb9cu, TD_EAX, 3, g, cam, q, 0);
    td_op_add(0x0046d1a8u, 0, 2, g, cam, 0, 0);
    int bad = td_run("draw", k);
    if (!bad) {
        if (!td_world[TD_G + 0x11cu]) ++td_cov_hidden;
        else if (q) ++td_cov_given;
        else ++td_cov_frustum;
        if (td_world[TD_G + 0x11cu]) td_cov_walk();
    }
    return bad;
}

static int terrain_draw_cases(void) {
    current_function = "terrain_draw";
    es2_sim_identity_memory(&ir_memory);
    es2_sim_rtl_memory(&ir_memory, &su_rtl);
    su_data_pristine = malloc(SU_DATA_SIZE);
    su_snap[0] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    su_snap[1] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    ir_region = ww_alloc(IR_REGION_SIZE);
    lc_stack = VirtualAlloc(NULL, 0x10000u, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    lc_bits = ww_alloc(LC_BITS_SIZE);
    td_world = VirtualAlloc(NULL, TD_WORLD_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    td_world_start = malloc(2u * TD_WORLD_SIZE);
    if (!su_data_pristine || !su_snap[0] || !su_snap[1] || !ir_region || !lc_stack || !lc_bits || !td_world || !td_world_start)
        return fail("terrain: memory");
    if (get32(mapped + (SU_COUNTER - 0x400000u))) return fail("terrain: the counter word is not free");
    su_pristine_take();
    static const struct { const char *dll, *name; void *stub; } stubs[] = {
        {"cw3220.dll", "_calloc", (void *)ir_calloc}, {"cw3220.dll", "_free", (void *)ir_free},
        {"cw3220.dll", "_memset", (void *)ir_memset}, {"cw3220.dll", "_memmove", (void *)ir_memmove},
    };
    for (unsigned i = 0; i < sizeof(stubs) / sizeof(stubs[0]); ++i)
        if (!ir_patch_import(stubs[i].dll, stubs[i].name, stubs[i].stub)) {
            fprintf(stderr, "import %s!%s\n", stubs[i].dll, stubs[i].name);
            return fail("terrain: import slot not found");
        }
    if (td_dispatch_setup()) return 1;
    for (unsigned i = 0; i < TD_STUBS; ++i) if (td_patch(td_stubs[i].va, td_stubs[i].stub, td_saved[i], 1)) return 1;
    int bad = 0;
    static const struct { const char *name; int (*run)(unsigned); unsigned cases; } parts[] = {
        {"leaves", td_leaves_case, 3000}, {"walk", td_walk_case, 1500}, {"draw", td_draw_case, 1500},
    };
    for (unsigned p = 0; !bad && p < sizeof(parts) / sizeof(parts[0]); ++p) {
        for (unsigned k = 0; !bad && k < parts[p].cases; ++k) {
            current_case = k; current_case_valid = 1;
            bad = parts[p].run(k);
        }
        if (!bad) printf("PASS: terrain %s: %u cases identical\n", parts[p].name, parts[p].cases);
    }
    for (unsigned i = 0; i < TD_STUBS; ++i) if (td_patch(td_stubs[i].va, td_stubs[i].stub, td_saved[i], 0)) bad = 1;
    if (!bad)
        printf("TERRAIN coverage: %llu own cell draws; fills: far %u, haze %u, flat %u, near %u, textured %u (4 corners %u, shade dwords %u); "
               "scene %u, cell objects %u; eye flag cleared/kept %u/%u; walks rows %u, columns %u, simple order %u; draws given %u, frustum %u, "
               "hidden %u, walks left out %u\n",
               (unsigned long long)td_terrain.cells, td_kinds[1], td_kinds[2], td_kinds[3], td_kinds[4], td_kinds[5], td_n4, td_shade_words,
               td_kinds[6], td_kinds[7], td_cov_flag_cleared, td_cov_flag_kept, td_cov_rows, td_cov_columns, td_cov_simple, td_cov_given,
               td_cov_frustum, td_cov_hidden, td_walk_skipped);
    current_case_valid = 0;
    put32(mapped + 0x2cbee4u, 0);
    memcpy(mapped + SU_DATA_RVA, su_data_pristine, SU_DATA_SIZE);
    return bad;
}
