/* Sky, horizon, object binning and the sort tree, original machine code against
 * sim/src/scene.c (--scene; target :differential_scene_test). Included after
 * view_cases.h; reuses startup_cases.h's DATA reset and snapshots (su_reset,
 * su_take, su_compare), loop_cases.h's loop set-up (lc_reset), audio_cases.h's
 * register-capturing machine call (au_call) and the world helpers' pattern of
 * view_cases.h.
 *
 * Every callee of the module that is not its own is a recording stand-in:
 * on the original's side a __cdecl function of this file entered through a
 * `jmp rel32` written over the callee's first bytes (restored at the end),
 * on the own side the same function body as a dispatch service at the
 * callee's VA (the other packages' rows 0x407098, 0x407878, 0x48c444,
 * 0x486704, 0x4303b0, 0x477548, 0x4680f0, 0x46e5a4, 0x46d1a8 replaced after the seal). The objects'
 * vtable slots and the graphics driver's slots 0x4a57bc/0x4a57c0/0x4a57ec
 * point at stand-ins too (services at the functions' addresses on the own
 * side). Each stand-in appends a letter with its arguments (and the
 * contents of the stack records it is handed: the points of a polygon, the
 * rectangle, the colour record, the light position) to the side's log,
 * hashed. Run as the original's machine code on both sides: the hooks
 * heading_cos 0x47dff0, multiply_q10 0x47e0b0 and dist_gate 0x49288c, and
 * vector_new 0x49667c (input_layer.h's row needs the input layer).
 *
 * Compared after each case: the stand-in logs (the call order and every
 * argument), DATA and the heap, the world and the registers the contract
 * names.
 *
 * - binning: 0x42841c over ten random pools of random objects (skip flags,
 *   links, cells found or not, the flats' distance gate), then the stores
 *   0x4282f8/0x42837c/0x4283b4/0x4282d8 and the cell lookup 0x4281b0 (the
 *   assertion from the 0x96th entry in an eighth of the cases, as the last
 *   call).
 * - cells: 0x428bc0, then the cell callback 0x428c60 over a bucket of
 *   random objects (kind 9, attached, culled by kind, the shape slot), the
 *   draw records' slot 0 being the per-object draw 0x42876c or a stand-in;
 *   the cull 0x428c08.
 * - sort: the sort tree on a random list of records of random kinds
 *   (0x429620, or the passes 0x429524/0x42958c and the walk 0x4295f0), the
 *   comparison 0x429460 and the insert 0x4294d4.
 * - pick: 0x42876c, 0x42883c, 0x428ae0, 0x428b38, 0x4282c0, 0x4282c8 over
 *   random pick states, loose lists and players.
 * - horizon: 0x42ec68, 0x42ee88, 0x42f0b0, 0x42ec08, 0x42ebe8, 0x42e700,
 *   0x428140 over random horizons (pitch, roll, band steps and counts,
 *   flat ground), jolts, shifts and viewports.
 * - leaves: the jolt palette 0x42ea44/0x42ea70/0x42ea94, the destructor
 *   0x42f6d2, 0x428eec. */
#pragma once
/* The headers this suite builds on (sim_main.c). */
#include "init_records_cases.h"
#include "startup_cases.h"
#include "loop_cases.h"
#include "audio_cases.h"
#include "es2/sim/load.h"
#include "es2/sim/scene.h"

#define SC_WORLD_SIZE 0x8000u
/* World layout (offsets). */
#define SC_OBJ 0x0000u       /* 16 objects of 0x300 */
#define SC_VT 0x3000u        /* the objects' vtable */
#define SC_RVT 0x3080u       /* the draw records' vtable */
#define SC_REC 0x3100u       /* 16 cell records of 4 (column, row, slot) */
#define SC_CW 0x3200u        /* 8 cell-word blocks of 8: +4 the cell record for 0x46e8c4 */
#define SC_SKIP 0x3280u      /* 4 skip records of 0x20 */
#define SC_SHAPE 0x3300u     /* 4 shapes of 0x20 (+0x1c) */
#define SC_RECORDS 0x3400u   /* 16 draw records of 0x36 */
#define SC_LIST 0x3800u      /* the draw list, 32 entries */
#define SC_NODES 0x3900u     /* 24 sort nodes of 0x1d */
#define SC_H 0x3c00u         /* the horizon object */
#define SC_GC 0x3d00u        /* the graphics context (0x240) */
#define SC_PLANE 0x3f80u     /* the context's first plane word */
#define SC_CAM 0x4000u       /* the camera */
#define SC_DEV 0x4100u       /* the device */
#define SC_PX 0x4200u        /* the player's +0x1f2 record */
#define SC_SKY 0x4300u       /* the sky object */
#define SC_DTOR 0x4400u      /* an object for 0x42f6d2 */
#define SC_NODE_X 0x4500u    /* a fresh node for 0x4294d4 */

#define SC_VA_GRID 0x004a0be0u
#define SC_VA_CELL_TABLE 0x004cf4f0u
#define SC_VA_CELL_COUNT 0x004cf874u
#define SC_VA_BUCKETS 0x004aaafcu
#define SC_VA_LOOSE 0x004cf88cu
#define SC_VA_LOOSE_COUNT 0x004cf92cu
#define SC_VA_GC 0x006c5f70u

static BYTE *sc_world, *sc_world_start;   /* start + the kept side-0 result */
static es2_sim_entry sc_entries[4096];
static es2_sim_dispatch sc_dispatch;
static es2_sim_load sc_load;
static es2_sim_scene sc_scene;
static int sc_bad;
static unsigned sc_side;

/* ---- the stand-in log of a side */
static uint64_t sc_hash[2];
static unsigned sc_count[2];
static char sc_text[2][4096];
static size_t sc_len[2];
static unsigned sc_letters[128];   /* side-0 calls per letter over all cases (coverage) */
static void sc_note(char letter, uint32_t a, uint32_t b, uint32_t c) {
    uint64_t h = sc_hash[sc_side];
    const uint32_t v[4] = {(uint32_t)letter, a, b, c};
    for (unsigned i = 0; i < 4u; ++i) { h ^= v[i]; h *= 0x100000001b3ull; }
    sc_hash[sc_side] = h;
    ++sc_count[sc_side];
    if (!sc_side) ++sc_letters[(unsigned char)letter & 127u];
    if (sc_len[sc_side] + 48u < sizeof(sc_text[0]))
        sc_len[sc_side] += (size_t)sprintf(sc_text[sc_side] + sc_len[sc_side], "%c(%lx,%lx,%lx) ", letter, (unsigned long)a,
                                           (unsigned long)b, (unsigned long)c);
}
static uint32_t sc_get(uint32_t a) { return get32((const BYTE *)(uintptr_t)a); }
static void sc_put(uint32_t a, uint32_t v) { put32((BYTE *)(uintptr_t)a, v); }
/* The colour record the context points at during a fill. */
static void sc_colour(char letter) {
    uint32_t rec = sc_get(sc_get(SC_VA_GC) + 0x22cu);
    sc_note(letter, sc_get(rec), sc_get(rec + 4u), 0);
}
static uint32_t sc_points(uint32_t n, uint32_t p) {
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < 2u * n && i < 16u; ++i) { h ^= sc_get(p + 4u * i); h *= 16777619u; }
    return h;
}

/* ---- the scripted answers */
static uint32_t sc_pool[10][6];   /* per pool: the objects in walk order, 0-terminated */
static uint32_t sc_edge[2], sc_terrain_ret;
static uint32_t sc_compare(uint32_t a, uint32_t b) {
    uint32_t ra = sc_get(a + 8u), rb = sc_get(b + 8u);
    return (sc_get(ra + 0x1au) ^ sc_get(rb + 0x1au)) & 0x00010001u;
}
static uint32_t sc_act(char l, const uint32_t *a) {
    switch (l) {
    case 'A': sc_note('A', a[0], a[1], a[2]); sc_note('a', a[3], a[4], a[5]); return 0;
    case 'B': sc_note('B', a[0], 0, 0); return 0;
    case 'P': sc_note('P', a[0], a[1], a[2]); return sc_get(a[1] + 0xcu);
    case 'C': sc_note('C', a[0], a[1], 0); return sc_get(a[1] + 4u);
    case 'M': sc_note('M', a[0], a[1], a[2]); return 0;
    case 'E': sc_note('E', a[0], a[1], a[2]); sc_put(a[2], sc_edge[0]); sc_put(a[2] + 4u, sc_edge[1]); return 0;
    case 'T': sc_note('T', a[0], a[1], 0); return sc_terrain_ret;
    case 'h': sc_note('h', a[0], 0, 0); return sc_get(a[0] + 0x2e0u);
    case 'c': sc_note('c', a[0], 0, 0); return sc_get(a[0] + 0x2e4u);
    case 'n': {   /* never 0: the original reads the cell words unchecked */
        uint32_t cw = sc_get(a[0] + 0x2e4u);
        sc_note('n', a[0], 0, 0);
        return cw ? cw : (uint32_t)(uintptr_t)sc_world + SC_CW;
    }
    case 'L': sc_note('L', a[0], sc_get(a[1]), sc_get(a[1] + 4u)); sc_note('l', sc_get(a[1] + 8u), sc_get(a[1] + 12u), 0); return 0;
    case 'S': sc_note('S', a[0], a[1], a[2] & 0xffu); return 0;
    case 'D': sc_note('D', a[0], 0, 0); return 0;
    case 'G': sc_note('G', 0, 0, 0); return 0;
    case 'O': sc_note('O', a[0], 0, 0); return 0;
    case 'Y': case 'Z': sc_note(l, a[0], sc_points(a[0], a[1]), 0); sc_colour((char)(l + 32)); return 0;
    case 'R':
        sc_note('R', sc_get(a[0]), sc_get(a[0] + 4u), sc_get(a[0] + 8u));
        sc_note('r', sc_get(a[0] + 12u), 0, 0);
        sc_colour('o');
        return 0;
    case 'W': sc_note('W', a[0], 0, 0); return a[0] * 3u + 1u;
    case 'X': sc_note('X', a[0], a[1], 0); return 0x77u;
    case 'd': sc_note('d', a[0], 0, 0); return 0x88u;
    case 'K': sc_note('K', a[0], a[1], 0); return sc_compare(a[0], a[1]);
    case 'k': sc_note('k', a[0], a[1], a[2]); return sc_compare(a[0], a[1]);
    case 'Q': {
        sc_note('Q', a[0], a[1], 0);
        uint32_t p = a[0] - 0x7000u;
        if (p >= 10u) return 0;
        unsigned i = 0;
        if (a[1]) { while (sc_pool[p][i] && sc_pool[p][i] != a[1]) ++i; if (sc_pool[p][i]) ++i; }
        return sc_pool[p][i];
    }
    case 'V': sc_note('V', a[0], a[1], 0); return sc_get(a[1] + 0x2dcu);
    case '0': sc_note('0', a[0], 0, 0); return 0x99u;
    case '4': sc_note('4', a[0], 0, 0); return a[0] + 0x2c0u;
    case 'z': sc_note('z', a[0], 0, 0); return sc_get(a[0] + 0x2d0u);
    case 's': sc_note('s', a[0], 0, 0); return sc_get(a[0] + 0x2d4u);
    case 'j': sc_note('j', a[0], a[1], 0); return 0;
    case 'q': sc_note('q', a[0], 0, 0); return sc_get(a[0] + 0x2d8u);
    case 'g': sc_note('g', a[0], 0, 0); return sc_get(a[0]);
    case 'e': sc_note('e', a[0], a[1], 0); sc_put(a[0], a[1]); return 0;
    case 'p': sc_note('p', a[0], 0, 0); return 0;
    case 'w': sc_note('w', a[0], 0, 0); return 0x55u;
    default: sc_note('?', 0, 0, 0); return 0;
    }
}
/* The original's side: one __cdecl function per stand-in. */
#define SC_O(name, letter, n)                                                                                   \
    static uint32_t __cdecl sc_o_##name(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5) { \
        uint32_t a[6] = {a0, a1, a2, a3, a4, a5};                                                               \
        for (unsigned i = (n); i < 6u; ++i) a[i] = 0;                                                           \
        return sc_act(letter, a);                                                                               \
    }
SC_O(assert, 'A', 6) SC_O(grid_begin, 'B', 1) SC_O(grid_at, 'P', 3) SC_O(grid_cell, 'C', 2) SC_O(grid_mark, 'M', 3)
SC_O(edge, 'E', 3) SC_O(terrain, 'T', 2) SC_O(hidden, 'h', 1) SC_O(cell6, 'c', 1) SC_O(cell9, 'n', 1) SC_O(light, 'L', 2)
SC_O(shape_light, 'S', 3) SC_O(depth, 'D', 1) SC_O(lights, 'G', 0) SC_O(select, 'O', 1) SC_O(poly, 'Y', 2) SC_O(poly_clip, 'Z', 2)
SC_O(rect, 'R', 1) SC_O(swap, 'W', 1) SC_O(line_dtor, 'X', 2) SC_O(del, 'd', 1) SC_O(cmp_kind, 'K', 2) SC_O(cmp_shapes, 'k', 3)
SC_O(pool_prev, 'Q', 2) SC_O(attached, 'V', 2) SC_O(draw, '0', 1) SC_O(pos, '4', 1) SC_O(size, 'z', 1) SC_O(shape, 's', 1)
SC_O(sky, 'j', 2) SC_O(size2, 'q', 1) SC_O(drv_get, 'g', 1) SC_O(drv_set, 'e', 2) SC_O(drv_palette, 'p', 1) SC_O(rdraw, 'w', 1)
static const struct { uint32_t va; void *fn; char letter; unsigned slots; } sc_stubs[] = {
    {0x00479c78u, (void *)sc_o_assert, 'A', 6}, {0x0046e944u, (void *)sc_o_grid_begin, 'B', 1},
    {0x0046e62cu, (void *)sc_o_grid_at, 'P', 3}, {0x0046e8c4u, (void *)sc_o_grid_cell, 'C', 2},
    {0x0046e5a4u, (void *)sc_o_grid_mark, 'M', 3}, {0x00470a1cu, (void *)sc_o_edge, 'E', 3},
    {0x0046d1a8u, (void *)sc_o_terrain, 'T', 2}, {0x00408240u, (void *)sc_o_hidden, 'h', 1},
    {0x00408228u, (void *)sc_o_cell6, 'c', 1}, {0x0046b850u, (void *)sc_o_cell9, 'n', 1},
    {0x00407098u, (void *)sc_o_light, 'L', 2}, {0x0042e8e8u, (void *)sc_o_shape_light, 'S', 3},
    {0x004680f0u, (void *)sc_o_depth, 'D', 1}, {0x00407878u, (void *)sc_o_lights, 'G', 0},
    {0x0048c444u, (void *)sc_o_select, 'O', 1}, {0x00484222u, (void *)sc_o_poly, 'Y', 2},
    {0x00483eb8u, (void *)sc_o_poly_clip, 'Z', 2}, {0x00486704u, (void *)sc_o_rect, 'R', 1},
    {0x004303b0u, (void *)sc_o_swap, 'W', 1}, {0x00475900u, (void *)sc_o_line_dtor, 'X', 2},
    {0x00477548u, (void *)sc_o_del, 'd', 1}, {0x0042937cu, (void *)sc_o_cmp_kind, 'K', 2},
    {0x0042900cu, (void *)sc_o_cmp_shapes, 'k', 3}, {0x00471c70u, (void *)sc_o_pool_prev, 'Q', 2},
    {0x00401078u, (void *)sc_o_attached, 'V', 2},
    /* reached through pointers: no VA of the original */
    {0, (void *)sc_o_draw, '0', 1}, {0, (void *)sc_o_pos, '4', 1}, {0, (void *)sc_o_size, 'z', 1}, {0, (void *)sc_o_shape, 's', 1},
    {0, (void *)sc_o_sky, 'j', 2}, {0, (void *)sc_o_size2, 'q', 1}, {0, (void *)sc_o_drv_get, 'g', 1}, {0, (void *)sc_o_drv_set, 'e', 2},
    {0, (void *)sc_o_drv_palette, 'p', 1}, {0, (void *)sc_o_rdraw, 'w', 1},
};
#define SC_STUBS (sizeof(sc_stubs) / sizeof(sc_stubs[0]))
static BYTE sc_saved[SC_STUBS][5];
static uint32_t sc_fn(void *f) { return (uint32_t)(uintptr_t)f; }
static int sc_patch(uint32_t va, void *target, BYTE saved[5], int install) {
    BYTE *site = mapped + (va - 0x400000u);
    DWORD previous;
    if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &previous)) return fail("scene: stub protection");
    if (install) { memcpy(saved, site, 5); site[0] = 0xe9; put32(site + 1, (uint32_t)(uintptr_t)target - (uint32_t)(uintptr_t)(site + 5)); }
    else memcpy(site, saved, 5);
    if (!VirtualProtect(site, 5, previous, &previous)) return fail("scene: stub restore protection");
    FlushInstructionCache(GetCurrentProcess(), site, 5);
    return 0;
}
static int sc_patch_all(int install) {
    for (unsigned i = 0; i < SC_STUBS; ++i)
        if (sc_stubs[i].va && sc_patch(sc_stubs[i].va, sc_stubs[i].fn, sc_saved[i], install)) return 1;
    return 0;
}

/* ---- the own side: the stand-ins as services, the passed originals */
static void sc_s_stub(es2_sim_service_call *call, void *user) {
    unsigned i = (unsigned)(uintptr_t)user;
    uint32_t a[6] = {0, 0, 0, 0, 0, 0};
    for (unsigned k = 0; k < sc_stubs[i].slots; ++k) a[k] = call->slots[k];
    call->eax = sc_act(sc_stubs[i].letter, a);
}
static void sc_original(es2_sim_service_call *call, void *user) {
    unsigned n = (unsigned)(uintptr_t)user;
    es2_hook_result r;
    uint32_t a[ES2_HOOK_MAX_ARGS] = {0};
    for (unsigned i = 0; i < n; ++i) a[i] = call->slots[i];
    uint32_t regs[3] = {call->eax, call->edx, call->ecx};
    es2_hook_call c = {mapped + (call->va - 0x400000u), ES2_ABI_CDECL, n, a, initial_fpu, &r, regs};
    es2_hook_machine_call(&c);
    call->eax = r.eax; call->edx = r.edx;
}
static void sc_refuse(const es2_sim_refusal *r, void *user) {
    (void)user;
    fprintf(stderr, "FAIL: own scene: refused call to 0x%08lx (%s): %s\n", (unsigned long)r->va, r->name ? r->name : "?",
            r->reason ? r->reason : "");
    sc_bad = 1;
}
static int sc_dispatch_setup(void) {
    es2_sim_dispatch_init(&sc_dispatch, sc_entries, 4096, sc_refuse, NULL);
    static es2_sim_services s;
    s.memory = &ir_memory;
    s.heap = &ir_heap;
    s.vt = &lc_vt;
    s.fs0 = NULL;
    if (es2_sim_register_services(&sc_dispatch, &s)) return fail("scene: dispatch");
    /* The other packages' rows among the callees: replaced after the seal. */
    static const uint32_t replaced[] = {0x00407098u, 0x00407878u, 0x0048c444u, 0x00486704u, 0x004303b0u, 0x00477548u, 0x004680f0u, 0x0046e5a4u, 0x0046d1a8u,
                                        0x00484222u, 0x00483eb8u,   /* the fill layer has rows */
                                        0x0042e8e8u,   /* the shade ranges have a row */
                                        0x00408240u, 0x00408228u,   /* the explosion binning callees have rows */
                                        0x0046e944u, 0x0046e62cu, 0x0046e8c4u, 0x00470a1cu,   /* the grid leaves have rows */
                                        0x0046b850u,   /* the object field has a row */
                                        0x00479c78u};   /* the error report has a row */
    for (unsigned i = 0; i < SC_STUBS; ++i) {
        uint32_t va = sc_stubs[i].va ? sc_stubs[i].va : sc_fn(sc_stubs[i].fn);
        int row = 0;
        for (unsigned k = 0; k < sizeof(replaced) / sizeof(replaced[0]); ++k) row |= replaced[k] == va;
        if (!row && es2_sim_dispatch_add_service(&sc_dispatch, va, "scene stub", sc_s_stub, (void *)(uintptr_t)i, 0, (uint8_t)sc_stubs[i].slots))
            return fail("scene: dispatch (stub)");
    }
    static const struct { uint32_t va; unsigned slots; const char *name; } passed[] = {
        {0x0047dff0u, 1, "heading_cos (original)"}, {0x0047e0b0u, 2, "multiply_q10 (original)"}, {0x0049288cu, 2, "dist_gate (original)"},
    };
    for (unsigned i = 0; i < sizeof(passed) / sizeof(passed[0]); ++i)
        if (es2_sim_dispatch_add_service(&sc_dispatch, passed[i].va, passed[i].name, sc_original, (void *)(uintptr_t)passed[i].slots, 0,
                                         (uint8_t)passed[i].slots))
            return fail("scene: dispatch (pass-through)");
    const char *error = NULL;
    uint32_t bad = 0;
    if (es2_sim_dispatch_seal(&sc_dispatch, &error, &bad)) return fail("scene: dispatch seal");
    for (unsigned i = 0; i < SC_STUBS; ++i) {
        uint32_t va = sc_stubs[i].va;
        int row = 0;
        for (unsigned k = 0; k < sizeof(replaced) / sizeof(replaced[0]); ++k) row |= replaced[k] == va;
        if (!row) continue;
        es2_sim_entry *e = (es2_sim_entry *)es2_sim_dispatch_find(&sc_dispatch, va);
        if (!e || e->kind != ES2_SIM_SERVICE) return fail("scene: service missing");
        e->service = sc_s_stub;
        e->service_user = (void *)(uintptr_t)i;
    }
    es2_sim_entry *e = (es2_sim_entry *)es2_sim_dispatch_find(&sc_dispatch, 0x0049667cu);
    if (!e || e->kind != ES2_SIM_SERVICE) return fail("scene: vector_new missing");
    e->service = sc_original;
    e->service_user = (void *)(uintptr_t)5u;
    return 0;
}

/* ---- the case script: DATA presets, the world, the calls */
typedef struct sc_preset { uint32_t va, value; unsigned width; } sc_preset;
static sc_preset sc_presets[512];
static unsigned sc_preset_count;
static void sc_set32(uint32_t va, uint32_t v) { sc_presets[sc_preset_count++] = (sc_preset){va, v, 4}; }
static void sc_set16(uint32_t va, uint32_t v) { sc_presets[sc_preset_count++] = (sc_preset){va, v, 2}; }
static void sc_set8(uint32_t va, uint32_t v) { sc_presets[sc_preset_count++] = (sc_preset){va, v, 1}; }
typedef struct sc_op { uint32_t va, a[4]; unsigned n, mask; } sc_op;
static sc_op sc_ops[48];
static unsigned sc_op_count;
static uint32_t sc_res[2][48][3];
#define SC_EAX 1u
#define SC_EDX 2u
#define SC_ECX 4u
static void sc_op_add(uint32_t va, unsigned mask, unsigned n, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3) {
    sc_op *o = &sc_ops[sc_op_count++];
    o->va = va; o->mask = mask; o->n = n;
    o->a[0] = a0; o->a[1] = a1; o->a[2] = a2; o->a[3] = a3;
}
#define SOP0(va, mask) sc_op_add((va), (mask), 0, 0, 0, 0, 0)
#define SOP1(va, mask, a) sc_op_add((va), (mask), 1, (a), 0, 0, 0)
#define SOP2(va, mask, a, b) sc_op_add((va), (mask), 2, (a), (b), 0, 0)
#define SOP3(va, mask, a, b, c) sc_op_add((va), (mask), 3, (a), (b), (c), 0)
#define SOP4(va, mask, a, b, c, d) sc_op_add((va), (mask), 4, (a), (b), (c), (d))
static uint32_t sc_rand(uint32_t n) { return random32() % n; }
static uint32_t sc_signed(uint32_t range) { return (uint32_t)((int32_t)sc_rand(2u * range + 1u) - (int32_t)range); }
static uint32_t sc_at(uint32_t offset) { return (uint32_t)(uintptr_t)sc_world + offset; }
static void sc_w32(uint32_t offset, uint32_t v) { put32(sc_world_start + offset, v); }
static void sc_w16(uint32_t offset, uint32_t v) { put16(sc_world_start + offset, (uint16_t)v); }
static void sc_w8(uint32_t offset, uint32_t v) { sc_world_start[offset] = (BYTE)v; }
static uint32_t sc_obj(unsigned i) { return sc_at(SC_OBJ + 0x300u * i); }

static void sc_begin(void) {
    memset(sc_world_start, 0, SC_WORLD_SIZE);
    sc_preset_count = 0;
    sc_op_count = 0;
    memset(sc_pool, 0, sizeof(sc_pool));
    sc_edge[0] = sc_signed(400u);
    sc_edge[1] = sc_signed(400u);
    sc_terrain_ret = random32();
    /* The vtables. */
    static const struct { uint32_t slot; void *fn; } slots[] = {
        {0, (void *)sc_o_draw}, {4, (void *)sc_o_pos}, {0x10, (void *)sc_o_size}, {0x24, (void *)sc_o_shape},
        {0x34, (void *)sc_o_sky}, {0x5c, (void *)sc_o_size2},
    };
    for (unsigned i = 0; i < sizeof(slots) / sizeof(slots[0]); ++i) sc_w32(SC_VT + slots[i].slot, sc_fn(slots[i].fn));
    sc_w32(SC_RVT, sc_rand(2u) ? 0x0042876cu : sc_fn((void *)sc_o_rdraw));
    sc_set32(0x004a57bcu, sc_fn((void *)sc_o_drv_get));
    sc_set32(0x004a57c0u, sc_fn((void *)sc_o_drv_set));
    sc_set32(0x004a57ecu, sc_fn((void *)sc_o_drv_palette));
    sc_set32(SC_VA_GRID, 0x00a0be00u + sc_rand(16u));
    sc_set32(0x0049aac8u, 0x00a0ac00u + sc_rand(16u));
    sc_set32(0x004d24eau, sc_at(SC_CAM));
    sc_set32(SC_VA_GC, sc_at(SC_GC));
    sc_set32(0x004a9608u, 0x00a96080u + sc_rand(16u));
    /* The camera: a position for the distance gate, pitch and roll. */
    for (unsigned i = 0; i < 3u; ++i) sc_w32(SC_CAM + 4u + 4u * i, sc_signed(0x8000u));
    sc_w16(SC_CAM + 0x10u, sc_rand(3u) ? random32() : 0u);
    sc_w16(SC_CAM + 0x12u, sc_rand(3u) ? random32() : 0u);
    /* The objects. */
    for (unsigned i = 0; i < 16u; ++i) {
        uint32_t o = SC_OBJ + 0x300u * i;
        static const uint16_t kinds[8] = {0, 1, 2, 5, 7, 8, 9, 3};
        random_bytes(sc_world_start + o + 4u, 0x2fcu);
        sc_w32(o, sc_at(SC_VT));
        sc_w16(o + 4u, kinds[sc_rand(8u)]);
        for (unsigned k = 0; k < 3u; ++k) sc_w32(o + 0x26u + 4u * k, sc_signed(0x8000u));
        sc_w32(o + 0x45u, sc_at(SC_SKIP + 0x20u * sc_rand(4u)));
        sc_w32(o + 0x1ecu, sc_rand(3u) ? sc_at(SC_REC + 4u * sc_rand(16u)) : 0u);
        sc_w32(o + 0x2b0u, sc_rand(3u) ? 0u : sc_obj(sc_rand(16u)));
        for (unsigned k = 0; k < 3u; ++k) sc_w32(o + 0x2c0u + 4u * k, sc_signed(0x8000u));
        sc_w32(o + 0x2ccu, sc_rand(4u) ? sc_at(SC_REC + 4u * sc_rand(16u)) : 0u);
        sc_w32(o + 0x2d0u, sc_rand(2u) ? sc_signed(0x2000u) : random32());
        sc_w32(o + 0x2d4u, sc_rand(2u) ? sc_at(SC_SHAPE + 0x20u * sc_rand(4u)) : 0u);
        sc_w32(o + 0x2d8u, random32());
        static const uint32_t attached[4] = {0, 0, 1, 0x00010000u};
        sc_w32(o + 0x2dcu, attached[sc_rand(4u)]);
        static const uint32_t hidden[4] = {0, 0, 1, 0x100u};
        sc_w32(o + 0x2e0u, hidden[sc_rand(4u)]);
        sc_w32(o + 0x2e4u, sc_rand(3u) ? sc_at(SC_CW + 8u * sc_rand(8u)) : 0u);
        sc_w32(o + 0x1f2u, sc_at(SC_PX));
    }
    for (unsigned j = 0; j < 4u; ++j) sc_w32(SC_SKIP + 0x20u * j + 0x14u, sc_rand(3u) ? 0u : random32());
    for (unsigned j = 0; j < 4u; ++j) sc_w32(SC_SHAPE + 0x20u * j + 0x1cu, sc_signed(0x400u));
    for (unsigned j = 0; j < 16u; ++j) {
        sc_w8(SC_REC + 4u * j, sc_rand(4u));
        sc_w8(SC_REC + 4u * j + 1u, sc_rand(4u));
        sc_w8(SC_REC + 4u * j + 2u, sc_rand(16u) ? sc_rand(0xfau) : 0xfau + sc_rand(6u));
    }
    for (unsigned j = 0; j < 8u; ++j) {
        random_bytes(sc_world_start + SC_CW + 8u * j, 4u);
        sc_w32(SC_CW + 8u * j + 4u, sc_rand(3u) ? sc_at(SC_REC + 4u * sc_rand(16u)) : 0u);
    }
    /* The cell table: a few entries; the buckets of the first eight cells. */
    unsigned cells = sc_rand(6u);
    sc_set32(SC_VA_CELL_COUNT, cells);
    for (unsigned j = 0; j < cells; ++j) {
        sc_set16(SC_VA_CELL_TABLE + 6u * j, sc_rand(4u));
        sc_set16(SC_VA_CELL_TABLE + 6u * j + 2u, sc_rand(4u));
        sc_set16(SC_VA_CELL_TABLE + 6u * j + 4u, j);
    }
}
static void sc_side_begin(unsigned side) {
    lc_reset(side, 47, 0x01000000u, 1);
    lc_loop.dispatch = &sc_dispatch;
    es2_sim_load_headless(&sc_load, &lc_loop);
    es2_sim_load_bind(&sc_load);
    memset(&sc_scene, 0, sizeof(sc_scene));
    sc_scene.loop = &lc_loop;
    es2_sim_scene_bind(&sc_scene);
    memcpy(sc_world, sc_world_start, SC_WORLD_SIZE);
    for (unsigned i = 0; i < sc_preset_count; ++i) {
        BYTE *p = mapped + (sc_presets[i].va - 0x400000u);
        if (sc_presets[i].width == 4) put32(p, sc_presets[i].value);
        else if (sc_presets[i].width == 2) put16(p, (uint16_t)sc_presets[i].value);
        else *p = (BYTE)sc_presets[i].value;
    }
    sc_side = side;
    sc_hash[side] = 0xcbf29ce484222325ull;
    sc_count[side] = 0;
    sc_len[side] = 0;
    sc_text[side][0] = 0;
}
static void sc_show(const char *what, unsigned k) {
    for (unsigned i = 0; i < sc_op_count; ++i)
        fprintf(stderr, "  op %u: 0x%08lx(0x%08lx, 0x%08lx, 0x%08lx, 0x%08lx)\n", i, (unsigned long)sc_ops[i].va, (unsigned long)sc_ops[i].a[0],
                (unsigned long)sc_ops[i].a[1], (unsigned long)sc_ops[i].a[2], (unsigned long)sc_ops[i].a[3]);
    fprintf(stderr, "  world at 0x%08lx; %s case %u\n", (unsigned long)(uintptr_t)sc_world, what, k);
}
static int sc_run(const char *what, unsigned k) {
    for (unsigned side = 0; side < 2u; ++side) {
        sc_side_begin(side);
        for (unsigned i = 0; i < sc_op_count; ++i) {
            sc_op *o = &sc_ops[i];
            uint32_t in[3] = {0, 0, 0}, *out = sc_res[side][i];
            sc_note('#', i, 0, 0);
            if (!side) au_call((uint32_t)(uintptr_t)(mapped + (o->va - 0x400000u)), o->a, o->n, in, out);
            else {
                es2_sim_result r;
                memset(&r, 0, sizeof(r));
                if (es2_sim_call(&sc_dispatch, o->va, o->a, o->n, in, &r)) sc_bad = 1;
                out[0] = r.eax; out[1] = r.edx; out[2] = r.ecx;
            }
            if (sc_bad) {
                fprintf(stderr, "FAIL: scene %s case %u op %u (0x%08lx): a call failed\n", what, k, i, (unsigned long)o->va);
                sc_show(what, k);
                return 1;
            }
        }
        if (!side) memcpy(sc_world_start + SC_WORLD_SIZE, sc_world, SC_WORLD_SIZE);
        su_take(side);
    }
    if (sc_hash[0] != sc_hash[1] || sc_count[0] != sc_count[1]) {
        fprintf(stderr, "  original (%u): %s\n  own      (%u): %s\n", sc_count[0], sc_text[0], sc_count[1], sc_text[1]);
        sc_show(what, k);
        fprintf(stderr, "FAIL: scene %s case %u: the stand-in calls differ\n", what, k);
        return 1;
    }
    if (su_compare(what, k)) { sc_show(what, k); return 1; }
    if (memcmp(sc_world, sc_world_start + SC_WORLD_SIZE, SC_WORLD_SIZE)) {
        for (unsigned i = 0, shown = 0; i < SC_WORLD_SIZE && shown < 16u; ++i)
            if (sc_world[i] != sc_world_start[SC_WORLD_SIZE + i]) {
                fprintf(stderr, "  world +0x%04x original %02x own %02x\n", i, sc_world_start[SC_WORLD_SIZE + i], sc_world[i]);
                ++shown;
            }
        sc_show(what, k);
        fprintf(stderr, "FAIL: scene %s case %u: the world differs\n", what, k);
        return 1;
    }
    for (unsigned i = 0; i < sc_op_count; ++i) {
        static const char *const names[3] = {"EAX", "EDX", "ECX"};
        for (unsigned j = 0; j < 3u; ++j) {
            if (!(sc_ops[i].mask & (1u << j)) || sc_res[0][i][j] == sc_res[1][i][j]) continue;
            fprintf(stderr, "FAIL: scene %s case %u op %u (0x%08lx): %s original 0x%08lx own 0x%08lx\n", what, k, i, (unsigned long)sc_ops[i].va,
                    names[j], (unsigned long)sc_res[0][i][j], (unsigned long)sc_res[1][i][j]);
            sc_show(what, k);
            return 1;
        }
    }
    return 0;
}

/* ---- binning */
static int sc_binning_case(unsigned k) {
    sc_begin();
    if (sc_rand(8u) == 0) {
        /* The assertion: the table full, a new cell (alone: the original
         * goes on writing past the table into the count). */
        sc_set32(SC_VA_CELL_COUNT, 0x96u);
        SOP2(0x004281b0u, SC_EAX, 7u, 9u);
        return sc_run("binning", k);
    }
    static const uint32_t pools[10] = {0x004a968du, 0x004a95a0u, 0x004a9b7au, 0x004a9db9u, 0x004a96c2u,
                                       0x004a961eu, 0x004a9642u, 0x004a966eu, 0x006b4f30u, 0x004a96aau};
    for (unsigned p = 0; p < 10u; ++p) {
        sc_set32(pools[p], 0x7000u + p);
        unsigned n = sc_rand(4u), used = 0;
        for (unsigned i = 0; i < n; ++i) {
            unsigned o = sc_rand(16u);
            if (used & (1u << o)) continue;
            used |= 1u << o;
            unsigned j = 0;
            while (sc_pool[p][j]) ++j;
            sc_pool[p][j] = sc_obj(o);
        }
    }
    sc_set32(SC_VA_LOOSE_COUNT, sc_rand(4u));
    SOP0(0x0042841cu, SC_EAX);
    uint32_t o = sc_obj(sc_rand(16u)), rec = sc_rand(5u) ? sc_at(SC_REC + 4u * sc_rand(16u)) : 0u;
    SOP2(0x004282f8u, SC_EAX | SC_EDX | SC_ECX, o, rec);
    SOP2(0x0042837cu, SC_EAX, sc_obj(sc_rand(16u)), sc_signed(0x4000u));
    SOP2(0x004283b4u, SC_EAX, sc_obj(sc_rand(16u)), sc_at(SC_CW + 8u * sc_rand(8u)));
    SOP1(0x004282d8u, SC_EAX, sc_obj(sc_rand(16u)));
    SOP2(0x004281b0u, SC_EAX, sc_rand(4u), sc_rand(4u));
    return sc_run("binning", k);
}

/* ---- cells */
static int sc_cells_case(unsigned k) {
    sc_begin();
    sc_set32(0x004cf884u, sc_at(SC_RECORDS));
    sc_set32(0x004cf888u, sc_at(SC_LIST));
    sc_set32(0x0049ac40u, sc_at(SC_NODES));
    sc_set32(0x004a5b08u, sc_rand(4u));
    sc_set8(0x004d24c5u, sc_rand(3u));
    for (unsigned j = 0; j < 5u; ++j) sc_set16(0x0049abb0u + 2u * j, sc_rand(0x800u));
    for (unsigned j = 0; j < 16u; ++j) {
        random_bytes(sc_world_start + SC_RECORDS + 0x36u * j, 0x36u);
        sc_w32(SC_RECORDS + 0x36u * j, sc_at(SC_RVT));
    }
    /* The per-object draw's state when the records draw through 0x42876c. */
    sc_set8(0x004cf930u, sc_rand(2u));
    sc_set32(0x004cf931u, sc_rand(3u));
    sc_set8(0x0049abbcu, sc_rand(4u) == 0);
    sc_set32(0x004cf974u, sc_obj(sc_rand(16u)));
    sc_set32(0x006c5ed0u, sc_at(SC_DEV));
    /* The buckets of the cells 0..7: up to eight objects. */
    for (unsigned c = 0; c < 8u; ++c)
        for (unsigned i = 0; i < 8u; ++i) sc_set32(SC_VA_BUCKETS + 1000u * c + 4u + 4u * i, sc_obj(sc_rand(16u)));
    SOP1(0x00428bc0u, SC_EAX, 0x1000u + sc_rand(0x10000u));
    SOP3(0x00428c60u, 0, sc_rand(4u), sc_rand(4u), sc_rand(9u) | (sc_rand(4u) ? 0u : 0xffff0000u));
    SOP2(0x00428c08u, SC_EAX, sc_obj(sc_rand(16u)), sc_rand(0x30000u));
    SOP3(0x00428c60u, 0, sc_rand(4u), sc_rand(4u), sc_rand(9u));
    return sc_run("cells", k);
}

/* ---- sort */
static int sc_sort_case(unsigned k) {
    sc_begin();
    sc_set32(0x0049ac40u, sc_at(SC_NODES));
    unsigned n = sc_rand(14u);
    for (unsigned j = 0; j < 16u; ++j) {
        uint32_t r = SC_RECORDS + 0x36u * j;
        random_bytes(sc_world_start + r, 0x36u);
        sc_w32(r, sc_at(SC_RVT));
        static const uint16_t kinds[6] = {0, 0, 1, 2, 3, 5};
        sc_w16(r + 0x16u, kinds[sc_rand(6u)]);
        sc_w32(r + 0x32u, sc_obj(sc_rand(16u)));
    }
    for (unsigned i = 0; i < n; ++i) sc_w32(SC_LIST + 4u * i, sc_at(SC_RECORDS + 0x36u * sc_rand(16u)));
    /* The draw records draw through the stand-in here (the per-object draw is the cells part's). */
    sc_w32(SC_RVT, sc_fn((void *)sc_o_rdraw));
    sc_set32(0x004a5b08u, 0);
    uint32_t list = sc_at(SC_LIST), root = sc_at(SC_NODES);
    /* A root node with a record (the second pass inserts into it even when
     * the first pass found nothing). */
    sc_w32(SC_NODES + 8u, sc_at(SC_RECORDS));
    switch (sc_rand(3u)) {
    case 0: SOP2(0x00429620u, SC_EAX, list, n | (sc_rand(4u) ? 0u : 0x00010000u)); break;
    case 1:
        SOP2(0x00429524u, SC_EAX, list, n);
        SOP3(0x0042958cu, 0, list, n, root + 0x1du * n);   /* a node past every first-pass node */
        if (n) SOP1(0x004295f0u, SC_EAX, root);
        break;
    default:
        SOP2(0x00429620u, SC_EAX, list, n);
        break;
    }
    /* A fresh node inserted into the tree, the comparison on two nodes. */
    if (n) {
        random_bytes(sc_world_start + SC_NODE_X, 0x1du);
        sc_w32(SC_NODE_X, 0);
        sc_w32(SC_NODE_X + 4u, 0);
        sc_w32(SC_NODE_X + 8u, sc_at(SC_RECORDS + 0x36u * sc_rand(16u)));
        SOP2(0x004294d4u, SC_EAX, root, sc_at(SC_NODE_X));
        SOP2(0x00429460u, SC_EAX, root, sc_at(SC_NODE_X));
    }
    return sc_run("sort", k);
}

/* ---- pick */
static int sc_pick_case(unsigned k) {
    sc_begin();
    uint32_t rec = SC_RECORDS;
    random_bytes(sc_world_start + rec, 0x36u);
    sc_w32(rec + 0x32u, sc_obj(sc_rand(16u)));
    sc_set16(0x004cf984u, sc_rand(3u) ? 0u : 1u + sc_rand(0x100u));
    sc_set8(0x004cf930u, sc_rand(2u));
    sc_set32(0x004cf931u, sc_rand(3u));
    sc_set32(0x004cf93cu, random32());
    for (unsigned j = 0; j < 4u; ++j) {
        sc_set32(0x004cf940u + 8u * j, sc_rand(4u));
        sc_set32(0x004cf960u + 4u * j, sc_rand(4u));
        sc_set8(0x004cf970u + j, sc_rand(3u) == 0);
    }
    sc_set32(0x004cf974u, sc_rand(2u) ? get32(sc_world_start + rec + 0x32u) : sc_obj(sc_rand(16u)));
    sc_set32(0x004cf97cu, random32());
    sc_set8(0x0049abbcu, sc_rand(2u));
    sc_set32(0x006c5ed0u, sc_at(SC_DEV));
    sc_w32(SC_DEV + 0x8cu, random32());
    unsigned loose = sc_rand(4u);
    sc_set32(SC_VA_LOOSE_COUNT, loose);
    for (unsigned i = 0; i < loose; ++i) sc_set32(SC_VA_LOOSE + 4u * i, sc_obj(sc_rand(16u)));
    uint32_t player = sc_obj(sc_rand(16u));
    sc_set32(0x004d24e6u, player);
    sc_w16(SC_PX + 0x50u, sc_rand(3u) ? 1u + sc_rand(0xffffu) : 0u);
    uint32_t po = player - (uint32_t)(uintptr_t)sc_world;
    sc_w16(po + 0x1e8u, sc_rand(4u) ? sc_rand(8u) : 0xffffu);
    sc_w16(po + 0x1eau, sc_rand(4u) ? sc_rand(8u) : 0xffffu);
    for (unsigned i = 0; i < 1u + sc_rand(3u); ++i) {
        switch (sc_rand(6u)) {
        case 0: SOP1(0x0042876cu, 0, sc_at(rec)); break;
        case 1: SOP2(0x0042883cu, 0, sc_at(SC_CAM), random32()); break;
        case 2: SOP0(0x00428ae0u, 0); break;
        case 3: SOP0(0x00428b38u, 0); break;
        case 4: SOP0(0x004282c0u, SC_EAX); break;
        default: SOP0(0x004282c8u, SC_EAX); break;
        }
    }
    SOP1(0x0042876cu, 0, sc_at(rec));
    SOP2(0x0042883cu, 0, sc_at(SC_CAM), 0);
    return sc_run("pick", k);
}

/* ---- horizon */
static void sc_horizon_world(void) {
    uint32_t h = SC_H;
    random_bytes(sc_world_start + h, 0x80u);
    sc_w32(h + 0x30u, sc_signed(60u));
    sc_w16(h + 0x34u, sc_rand(3u) ? random32() : 0u);
    sc_w16(h + 0x36u, sc_rand(3u) ? random32() : 0u);
    for (unsigned i = 0; i < 4u; ++i) sc_w32(h + 0x38u + 4u * i, sc_signed(400u));
    sc_w32(h + 0x48u, sc_signed(0x4000u));
    sc_w32(h + 0x4cu, sc_signed(0x4000u));
    sc_w32(h + 0x54u, sc_rand(24u));
    sc_w32(h + 0x58u, sc_rand(8u));
    sc_w32(h + 0x5cu, sc_rand(24u));
    sc_w32(h + 0x60u, sc_rand(8u));
    sc_w32(h + 0x6cu, sc_signed(40u));
    sc_w8(h + 0x71u, sc_rand(2u));
    sc_w32(h + 0x72u, sc_signed(400u));
    sc_w32(h + 0x76u, sc_signed(400u));
    random_bytes(sc_world_start + SC_GC, 0x240u);
    sc_w32(SC_GC + 4u, sc_rand(3u));
    sc_w32(SC_GC + 8u, sc_at(SC_PLANE));
    sc_w32(SC_PLANE, sc_rand(2u) ? 0u : random32());
    sc_w32(SC_GC + 0x210u, sc_rand(100u));
    sc_w32(SC_GC + 0x214u, sc_rand(100u));
    sc_w32(SC_GC + 0x218u, 100u + sc_rand(540u));
    sc_w32(SC_GC + 0x21cu, 100u + sc_rand(380u));
    sc_set32(0x004cf9a0u, sc_rand(3u) ? 0u : random32());
    sc_set32(0x004cf9a4u, sc_rand(4u) ? 0u : 1u);
    sc_set16(0x004d253au, sc_signed(2u));
    sc_set16(0x004d253cu, sc_signed(2u));
    sc_set32(0x006c6028u, sc_rand(13u));
    sc_set32(0x006c6034u, sc_rand(640u));
    sc_set32(0x006c6038u, sc_rand(480u));
    sc_set32(0x0049aee0u, sc_at(SC_H));
    sc_set32(0x0049aee4u, sc_rand(2u) ? sc_at(SC_SKY) : 0u);
    sc_w32(SC_SKY, sc_at(SC_VT));
    sc_set32(0x006c5ed0u, random32());
}
static int sc_horizon_case(unsigned k) {
    sc_begin();
    sc_horizon_world();
    uint32_t h = sc_at(SC_H), cam = sc_at(SC_CAM), j = sc_rand(3u) ? 0x004cf9a0u : 0u;
    switch (sc_rand(7u)) {
    case 0: SOP2(0x0042ec68u, SC_EAX, h, j); break;
    case 1: SOP1(0x0042ee88u, SC_EAX, h); break;
    case 2: SOP3(0x0042f0b0u, SC_EAX, h, cam, j); break;
    case 3: SOP4(0x0042ec08u, SC_EAX, h, cam, sc_signed(60u), j); break;
    case 4: SOP3(0x0042ebe8u, SC_EAX, h, cam, j); break;
    case 5: SOP1(0x00428140u, SC_EAX, cam); break;
    default: SOP1(0x0042e700u, SC_EAX, cam); break;
    }
    return sc_run("horizon", k);
}

/* ---- leaves */
static int sc_leaves_case(unsigned k) {
    sc_begin();
    sc_set32(0x0049aef8u, sc_rand(3u) ? random32() : 0u);
    sc_set32(0x004cfd0cu, random32());
    sc_set32(0x004cfd08u, random32());
    sc_set8(0x0049aef3u, sc_rand(2u));
    sc_set32(0x004cf93cu, random32());
    sc_set8(0x004cf930u, sc_rand(2u));
    sc_set32(0x006cbee4u, SU_COUNTER);
    sc_set32(SU_COUNTER, random32());
    for (unsigned i = 0; i < 4u; ++i) {
        switch (sc_rand(7u)) {
        case 0: SOP0(0x0042ea44u, SC_EAX); break;
        case 1: SOP0(0x0042ea70u, SC_EAX); break;
        case 2: SOP0(0x0042ea94u, SC_EAX); break;
        case 3: SOP2(0x0042f6d2u, SC_EAX, sc_rand(4u) ? sc_at(SC_DTOR) : 0u, sc_rand(4u)); break;
        case 4: SOP1(0x00428eecu, SC_EAX, random32()); break;
        case 5: SOP0(0x004282c0u, SC_EAX); break;
        default: SOP0(0x004282c8u, SC_EAX); break;
        }
    }
    return sc_run("leaves", k);
}

static int scene_cases(void) {
    current_function = "scene";
    es2_sim_identity_memory(&ir_memory);
    es2_sim_rtl_memory(&ir_memory, &su_rtl);
    su_data_pristine = malloc(SU_DATA_SIZE);
    su_snap[0] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    su_snap[1] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    ir_region = ww_alloc(IR_REGION_SIZE);
    lc_stack = VirtualAlloc(NULL, 0x10000u, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    lc_bits = ww_alloc(LC_BITS_SIZE);
    sc_world = VirtualAlloc(NULL, SC_WORLD_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    sc_world_start = malloc(2u * SC_WORLD_SIZE);
    if (!su_data_pristine || !su_snap[0] || !su_snap[1] || !ir_region || !lc_stack || !lc_bits || !sc_world || !sc_world_start)
        return fail("scene: memory");
    if (get32(mapped + (SU_COUNTER - 0x400000u))) return fail("scene: the counter word is not free");
    su_pristine_take();
    static const struct { const char *dll, *name; void *stub; } stubs[] = {
        {"cw3220.dll", "_calloc", (void *)ir_calloc}, {"cw3220.dll", "_free", (void *)ir_free},
        {"cw3220.dll", "_memset", (void *)ir_memset}, {"cw3220.dll", "_memmove", (void *)ir_memmove},
    };
    for (unsigned i = 0; i < sizeof(stubs) / sizeof(stubs[0]); ++i)
        if (!ir_patch_import(stubs[i].dll, stubs[i].name, stubs[i].stub)) {
            fprintf(stderr, "import %s!%s\n", stubs[i].dll, stubs[i].name);
            return fail("scene: import slot not found");
        }
    if (sc_dispatch_setup() || sc_patch_all(1)) return 1;
    int bad = 0;
    static const struct { const char *name; int (*run)(unsigned); unsigned cases; } parts[] = {
        {"binning", sc_binning_case, 1000}, {"cells", sc_cells_case, 1000}, {"sort", sc_sort_case, 1000},
        {"pick", sc_pick_case, 1000}, {"horizon", sc_horizon_case, 1500}, {"leaves", sc_leaves_case, 500},
    };
    for (unsigned p = 0; !bad && p < sizeof(parts) / sizeof(parts[0]); ++p) {
        for (unsigned k = 0; !bad && k < parts[p].cases; ++k) {
            current_case = k; current_case_valid = 1;
            bad = parts[p].run(k);
        }
        if (!bad) printf("PASS: scene %s: %u cases identical\n", parts[p].name, parts[p].cases);
    }
    if (!bad) {
        printf("SCENE coverage (original's stand-in calls):");
        static const char letters[] = "ABPCMETchnLSDGOYZRWXdKkQV04zsjqgepw";
        for (const char *l = letters; *l; ++l) printf(" %c=%u", *l, sc_letters[(unsigned char)*l]);
        printf("\n");
    }
    current_case_valid = 0;
    if (sc_patch_all(0)) bad = 1;
    put32(mapped + 0x2cbee4u, 0);
    memcpy(mapped + SU_DATA_RVA, su_data_pristine, SU_DATA_SIZE);
    return bad;
}
