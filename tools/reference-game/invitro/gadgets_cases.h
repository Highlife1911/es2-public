/* The HUD gadgets, original machine code against sim/src/gadgets.c (--gadgets;
 * target :differential_gadgets_test). Included after terrain_draw_cases.h;
 * reuses startup_cases.h's DATA snapshots (su_take, su_compare), loop_cases.h's
 * loop set-up (lc_reset) and audio_cases.h's register-capturing machine call
 * (au_call). Identifiers: gd_/GD_.
 *
 * Every case builds a synthetic gadget object D (its rectangle, centre,
 * target point and flags), the panel H [0x49b088] with its player object
 * P (heading, position, route, target), the canvas and current graphics
 * contexts, the view object [0x4d24ea], the camera [0x6c5ff8], the HUD
 * bitmap table [0x49be78]+8 (bitmap headers only), the gauge step table
 * [0x49be70], a route with its point list and the cursor entries
 * 0x49b08c in an identity-mapped buffer, presets the DATA words the
 * gadgets read (the shifts 0x4d253a/c, the colour words, the nav gauge
 * sizes 0x4d1808..0x4d1824, the mouse point 0x4a3620, the cursor state),
 * then runs one gadget call on both sides: side 0 the original's code,
 * side 1 the sim's rows (hud_load.h's draw rows, which delegate to gadgets.c,
 * and the hud_gadgets.h rows) through a dispatch of the sim's real services.
 *
 * Recorders on both sides (side 0: a jump patched over the entry; side 1:
 * a row at the same VA): the view rows 0x48c444 (the camera, its 0x20
 * bytes), 0x48c57c, 0x48c5fc, 0x48c6d0 (the input bytes hashed, the
 * output written from the case's script, so that every screen-position
 * branch is steered), the blit 0x4816a8 (bitmap, point bytes, flags), the
 * label 0x438920 (its argument and text), the polygon fills 0x483eb8 and
 * 0x4842bb (the count, the points and the fill record put into the current
 * context's +0x22c), the empty cursor sets 0x486e70/0x48992e (their
 * arguments) and the gadget's draw slot [[D]] (a host function, a row at
 * its address on side 1). Stack addresses are never hashed, only the bytes
 * behind them. The canvas push/pop (hud_state.c), the vector constructor,
 * itoa/strcat, radar_units and the 16.16 multiply run as the sim's rows;
 * the registry hooks the gadgets call (dist2d, dist3d, angle_ratio,
 * sincos, rotate_2d, distance_display, vector_angle, route_point) run as
 * the original's machine code on the own side. Compared: the recorder
 * hash and count, DATA, the world (masked: the two contexts' +0x22c, a
 * stack address on both sides) and EAX where the contract defines it
 * (0x486ece, 0x43495c, 0x43b67c, 0x43b7cc). */
#pragma once
/* The headers this suite builds on (sim_main.c). */
#include "init_records_cases.h"
#include "startup_cases.h"
#include "loop_cases.h"
#include "audio_cases.h"
#include "es2/sim/load.h"
#include "es2/sim/view.h"
#include "es2/sim/input.h"
#include "es2/sim/hud.h"

#define GD_WORLD_SIZE 0x2000u
#define GD_D 0x0000u        /* the gadget, 0x100 */
#define GD_VT 0x0100u       /* its vtable */
#define GD_H 0x0140u        /* the panel, 0x280 */
#define GD_P 0x0400u        /* the player object, 0x1b0 */
#define GD_GC0 0x0600u      /* the current context [0x6c5f70] at entry */
#define GD_GC1 0x0840u      /* the canvas [H+4] */
#define GD_SURF 0x0a80u     /* the surface [0x6c5ed0] (+0x44) */
#define GD_EYE 0x0b00u      /* the view object [0x4d24ea] */
#define GD_CAM 0x0b40u      /* the camera [0x6c5ff8], 0x20 */
#define GD_TBL 0x0b80u      /* [0x49be78]: +8 the bitmap table */
#define GD_BMPS 0x0b90u     /* 16 pointers */
#define GD_BMP 0x0c00u      /* 16 bitmap headers of 0x10 */
#define GD_STEPS 0x0d00u    /* [0x49be70]: +4 count, +8 table */
#define GD_STEPTBL 0x0d10u  /* 16 dwords */
#define GD_ROUTE 0x0d80u    /* route+4 word index, route+6 the list */
#define GD_LIST 0x0da0u     /* word count, +2 the point pointers */
#define GD_PPTR 0x0dc0u     /* 16 pointers */
#define GD_PTS 0x0e00u      /* 16 points of 8 */
#define GD_TARGET 0x0e80u   /* P+0x1a4's object (+0x26 its point) */
#define GD_STR 0x0f00u      /* the string [0x4d1654] */
#define GD_CURS 0x0f40u     /* 4 cursor entries of 0x10 */
#define GD_MOUSE 0x0f80u    /* the mouse position op's output */
static BYTE *gd_world, *gd_world_start;
static es2_sim_entry gd_entries[4096];
static es2_sim_dispatch gd_dispatch;
static es2_sim_load gd_load;
static es2_sim_view gd_view;
static es2_sim_input gd_input;
static es2_sim_hud gd_hud;
static int gd_bad;
static void gd_refuse(const es2_sim_refusal *r, void *user) {
    (void)user;
    fprintf(stderr, "FAIL: own gadgets: refused call to 0x%08lx (%s): %s\n", (unsigned long)r->va, r->name ? r->name : "?",
            r->reason ? r->reason : "");
    gd_bad = 1;
}

/* ---- the recorders */
static uint64_t gd_hash[2];
static unsigned gd_count[2], gd_side_now, gd_calls[2][12];
static char gd_text[2][512];
static uint32_t gd_xf[3], gd_proj[2][2], gd_scr[2][2];
static unsigned gd_kinds[12], gd_bmp_hits[16], gd_flags_hits[4], gd_poly_n[2][5];
static void gd_mix(uint32_t v) {
    for (unsigned i = 0; i < 4u; ++i) { gd_hash[gd_side_now] ^= (BYTE)(v >> (8u * i)); gd_hash[gd_side_now] *= 0x100000001b3ull; }
}
static void gd_bytes(uint32_t va, uint32_t n) { for (uint32_t i = 0; i < n; ++i) gd_mix(*(BYTE *)(uintptr_t)(va + i)); }
static void gd_note(unsigned kind, const uint32_t *a, unsigned n) {
    unsigned side = gd_side_now;
    gd_mix(0xabc00000u + kind);
    for (unsigned i = 0; i < n; ++i) gd_mix(a[i]);
    size_t used = strlen(gd_text[side]);
    if (gd_count[side] < 14u && used + 40u < sizeof(gd_text[side]))
        snprintf(gd_text[side] + used, sizeof(gd_text[side]) - used, " %u(%lx,%lx)", kind, (unsigned long)(n ? a[0] : 0),
                 (unsigned long)(n > 1u ? a[1] : 0));
    ++gd_count[side];
    if (side) ++gd_kinds[kind];
}
static void gd_put_pair(uint32_t va, const uint32_t *v, unsigned n) { for (unsigned i = 0; i < n; ++i) put32((BYTE *)(uintptr_t)(va + 4u * i), v[i]); }
/* One recorded call (the same on both sides); returns its EAX. */
static uint32_t gd_record(unsigned kind, const uint32_t *a) {
    unsigned side = gd_side_now, k = gd_calls[side][kind]++;
    switch (kind) {
    case 1: {   /* 0x48c444(camera or 0) */
        uint32_t v = a[0] != 0;
        gd_note(1, &v, 1);
        if (a[0]) gd_bytes(a[0], 0x20u);
        return 0;
    }
    case 2: gd_note(2, NULL, 0); gd_bytes(a[0], 12u); gd_put_pair(a[1], gd_xf, 3); return 0;          /* 0x48c57c(in, out) */
    case 3: gd_note(3, NULL, 0); gd_bytes(a[0], 12u); gd_put_pair(a[1], gd_proj[k & 1u], 2); return 0; /* 0x48c5fc(in, out) */
    case 4: gd_note(4, NULL, 0); gd_bytes(a[0], 8u); gd_put_pair(a[1], gd_scr[k & 1u], 2); return 0;   /* 0x48c6d0(in, out) */
    case 5: {   /* 0x4816a8(bitmap, point, flags) */
        uint32_t v[2] = {a[0], a[2]};
        gd_note(5, v, 2);
        gd_bytes(a[1], 8u);
        if (side) {
            for (unsigned i = 0; i < 16u; ++i)
                if (a[0] == (uint32_t)(uintptr_t)gd_world + GD_BMP + 0x10u * i) ++gd_bmp_hits[i];
            ++gd_flags_hits[a[2] & 3u];
        }
        return 0;
    }
    case 6: {   /* 0x438920(value, text) */
        gd_note(6, a, 1);
        uint32_t n = 0;
        while (n < 80u && *(BYTE *)(uintptr_t)(a[1] + n)) ++n;
        gd_mix(n);
        gd_bytes(a[1], n);
        return 0;
    }
    case 7: case 8: {   /* 0x483eb8 / 0x4842bb(count, points) */
        gd_note(kind, a, 1);
        if (a[0] <= 8u) gd_bytes(a[1], 8u * a[0]);
        uint32_t rec = get32((BYTE *)(uintptr_t)(get32(mapped + (0x006c5f70u - 0x400000u)) + 0x22cu));
        gd_bytes(rec, 8u);
        if (side && a[0] <= 4u) ++gd_poly_n[kind - 7u][a[0]];
        return 0;
    }
    case 9: case 10: gd_note(kind, a, 2); return 0;   /* 0x486e70 / 0x48992e(first, second) */
    default: gd_note(11, a, 1); return 0x5a000000u ^ a[0];   /* the draw slot [[D]](D) */
    }
}
#define GD_HOST1(k) static uint32_t __cdecl gd_o##k(uint32_t a) { const uint32_t s[1] = {a}; return gd_record(k, s); }
#define GD_HOST2(k) static uint32_t __cdecl gd_o##k(uint32_t a, uint32_t b) { const uint32_t s[2] = {a, b}; return gd_record(k, s); }
#define GD_HOST3(k) static uint32_t __cdecl gd_o##k(uint32_t a, uint32_t b, uint32_t c) { const uint32_t s[3] = {a, b, c}; return gd_record(k, s); }
GD_HOST1(1)
GD_HOST2(2)
GD_HOST2(3)
GD_HOST2(4)
GD_HOST3(5)
GD_HOST2(6)
GD_HOST2(7)
GD_HOST2(8)
GD_HOST2(9)
GD_HOST2(10)
GD_HOST1(11)
static void gd_s_stub(es2_sim_service_call *sc, void *user) { sc->eax = gd_record((unsigned)(uintptr_t)user, sc->slots); }
static const struct { uint32_t va; void *stub; unsigned kind, slots; } gd_stubs[] = {
    {0x0048c444u, (void *)gd_o1, 1, 1}, {0x0048c57cu, (void *)gd_o2, 2, 2}, {0x0048c5fcu, (void *)gd_o3, 3, 2},
    {0x0048c6d0u, (void *)gd_o4, 4, 2}, {0x004816a8u, (void *)gd_o5, 5, 3}, {0x00438920u, (void *)gd_o6, 6, 2},
    {0x00483eb8u, (void *)gd_o7, 7, 2}, {0x004842bbu, (void *)gd_o8, 8, 2}, {0x00486e70u, (void *)gd_o9, 9, 2},
    {0x0048992eu, (void *)gd_o10, 10, 2},
};
#define GD_STUBS (sizeof(gd_stubs) / sizeof(gd_stubs[0]))
static BYTE gd_saved[GD_STUBS][5];
static int gd_patch(uint32_t va, void *target, BYTE saved[5], int install) {
    BYTE *site = mapped + (va - 0x400000u);
    DWORD previous;
    if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &previous)) return fail("gadgets: stub protection");
    if (install) { memcpy(saved, site, 5); site[0] = 0xe9; put32(site + 1, (uint32_t)(uintptr_t)target - (uint32_t)(uintptr_t)(site + 5)); }
    else memcpy(site, saved, 5);
    if (!VirtualProtect(site, 5, previous, &previous)) return fail("gadgets: stub restore protection");
    FlushInstructionCache(GetCurrentProcess(), site, 5);
    return 0;
}
static void gd_original(es2_sim_service_call *sc, void *user) {
    unsigned n = (unsigned)(uintptr_t)user;
    uint32_t a[6] = {0, 0, 0, 0, 0, 0}, in[3] = {sc->eax, sc->edx, sc->ecx}, out[3];
    for (unsigned i = 0; i < n; ++i) a[i] = sc->slots[i];
    au_call((uint32_t)(uintptr_t)(mapped + (sc->va - 0x400000u)), a, n, in, out);
    sc->eax = out[0]; sc->edx = out[1]; sc->ecx = out[2];
}
static int gd_put(uint32_t va, const char *name, es2_sim_service_fn fn, void *user, unsigned slots) {
    for (unsigned i = 0; i < gd_dispatch.count; ++i) {
        es2_sim_entry *e = &gd_dispatch.entries[i];
        if (e->va != va) continue;
        e->kind = ES2_SIM_SERVICE; e->name = name; e->service = fn; e->service_user = user; e->pop_bytes = 0; e->stack_slots = (uint8_t)slots;
        return 0;
    }
    return es2_sim_dispatch_add_service(&gd_dispatch, va, name, fn, user, 0, (uint8_t)slots);
}
static int gd_dispatch_setup(void) {
    es2_sim_dispatch_init(&gd_dispatch, gd_entries, 4096, gd_refuse, NULL);
    static es2_sim_services s;
    s.memory = &ir_memory;
    s.heap = &ir_heap;
    s.vt = &lc_vt;
    s.fs0 = NULL;
    static const struct { uint32_t va; unsigned slots; const char *name; } passed[] = {
        {0x004928d0u, 2, "dist2d (original)"}, {0x0047de72u, 3, "dist3d (original)"}, {0x0047d32cu, 2, "angle_ratio (original)"},
        {0x0047eb30u, 2, "sincos (original)"}, {0x0047ffe8u, 3, "rotate_2d (original)"}, {0x0047d694u, 2, "distance_display (original)"},
        {0x0049290cu, 2, "vector_angle (original)"}, {0x00423b0cu, 2, "route_point (original)"},
    };
    if (es2_sim_register_services(&gd_dispatch, &s)) return fail("gadgets: dispatch");
    for (unsigned i = 0; i < sizeof(passed) / sizeof(passed[0]); ++i)
        if (gd_put(passed[i].va, passed[i].name, gd_original, (void *)(uintptr_t)passed[i].slots, passed[i].slots)) return fail("gadgets: dispatch");
    for (unsigned i = 0; i < GD_STUBS; ++i)
        if (gd_put(gd_stubs[i].va, "gadgets recorder", gd_s_stub, (void *)(uintptr_t)gd_stubs[i].kind, gd_stubs[i].slots))
            return fail("gadgets: dispatch");
    if (gd_put((uint32_t)(uintptr_t)gd_o11, "gadgets draw slot", gd_s_stub, (void *)(uintptr_t)11u, 1)) return fail("gadgets: dispatch");
    const char *error = NULL;
    uint32_t bad = 0;
    if (es2_sim_dispatch_seal(&gd_dispatch, &error, &bad)) return fail("gadgets: dispatch seal");
    return 0;
}

/* ---- the case script */
typedef struct gd_preset { uint32_t va, value; unsigned width; } gd_preset;
static gd_preset gd_presets[96];
static unsigned gd_preset_count;
static void gd_set32(uint32_t va, uint32_t v) { gd_presets[gd_preset_count++] = (gd_preset){va, v, 4}; }
static void gd_set16(uint32_t va, uint32_t v) { gd_presets[gd_preset_count++] = (gd_preset){va, v, 2}; }
static void gd_set8(uint32_t va, uint32_t v) { gd_presets[gd_preset_count++] = (gd_preset){va, v, 1}; }
typedef struct gd_op { uint32_t va, a[2]; unsigned n, eax; } gd_op;
static gd_op gd_ops[4];
static unsigned gd_op_count;
static uint32_t gd_res[2][4];
static uint32_t gd_rand(uint32_t n) { return random32() % n; }
static uint32_t gd_signed(uint32_t range) { return (uint32_t)((int32_t)gd_rand(2u * range + 1u) - (int32_t)range); }
static uint32_t gd_at(uint32_t offset) { return (uint32_t)(uintptr_t)gd_world + offset; }
static void gd_w32(uint32_t offset, uint32_t v) { put32(gd_world_start + offset, v); }
static void gd_w16(uint32_t offset, uint32_t v) { put16(gd_world_start + offset, (uint16_t)v); }
static void gd_w8(uint32_t offset, uint32_t v) { gd_world_start[offset] = (BYTE)v; }
static uint32_t gd_r32(uint32_t offset) { return get32(gd_world_start + offset); }
static void gd_op_add(uint32_t va, unsigned eax, unsigned n, uint32_t a0, uint32_t a1) { gd_ops[gd_op_count++] = (gd_op){va, {a0, a1}, n, eax}; }
static unsigned gd_shx, gd_shy;
static int32_t gd_near;
/* A value on either side of the bound b (or anywhere within 2b). */
static uint32_t gd_edge(uint32_t b) {
    switch (gd_rand(6u)) {
    case 0: return 0u - b - 1u;
    case 1: return 0u - b;
    case 2: return b;
    case 3: return b + 1u;
    case 4: return gd_signed(b);
    default: return gd_signed(3u * b);
    }
}
/* The world every case shares and the DATA words. */
static void gd_common(void) {
    memset(gd_world_start, 0, GD_WORLD_SIZE);
    gd_preset_count = 0;
    gd_op_count = 0;
    random_bytes(gd_world_start + GD_D, 0x100);
    gd_w32(GD_D, gd_at(GD_VT));
    gd_w32(GD_VT, (uint32_t)(uintptr_t)gd_o11);
    random_bytes(gd_world_start + GD_H, 0x280);
    gd_w32(GD_H + 4u, gd_at(GD_GC1));
    put32(gd_world_start + GD_H + 0x203u, gd_at(GD_P));
    put32(gd_world_start + GD_H + 0x226u, gd_rand(3u) ? gd_at(GD_H + 0x232u) : random32());
    gd_w32(GD_H + 0x25eu, gd_signed(0x8000u));
    gd_w32(GD_H + 0x262u, gd_signed(0x8000u));
    gd_w8(GD_H + 0x26au, gd_rand(3u) ? gd_rand(2u) : random32());
    random_bytes(gd_world_start + GD_P, 0x1b0);
    gd_w32(GD_P + 0x26u, gd_signed(0x8000u));
    gd_w32(GD_P + 0x2au, gd_signed(0x8000u));
    put32(gd_world_start + GD_P + 0x45u, 0);
    gd_w32(GD_P + 0x1a4u, 0);
    gd_w32(GD_GC0 + 0x22cu, random32());
    gd_w32(GD_GC1 + 0x22cu, random32());
    for (unsigned i = 0; i < 3u; ++i) gd_w32(GD_EYE + 4u + 4u * i, gd_signed(0x10000u));
    put32(gd_world_start + GD_EYE + 0x1au, gd_rand(9u));
    gd_near = (int32_t)gd_signed(0x100u) + 0x100;
    put32(gd_world_start + GD_EYE + 0x1eu, (uint32_t)gd_near);
    random_bytes(gd_world_start + GD_CAM, 0x20);
    gd_w32(GD_TBL + 8u, gd_at(GD_BMPS));
    for (unsigned i = 0; i < 16u; ++i) {
        gd_w32(GD_BMPS + 4u * i, gd_at(GD_BMP + 0x10u * i));
        gd_w16(GD_BMP + 0x10u * i + 4u, gd_rand(8u) ? 1u + gd_rand(48u) : random32());
        gd_w16(GD_BMP + 0x10u * i + 6u, gd_rand(8u) ? 1u + gd_rand(48u) : random32());
    }
    gd_w32(GD_STEPS + 4u, 2u + gd_rand(7u));
    gd_w32(GD_STEPS + 8u, gd_at(GD_STEPTBL));
    for (unsigned i = 0; i < 16u; ++i) gd_w32(GD_STEPTBL + 4u * i, random32());
    static const char *const words[] = {"WP ", "NAV ", "", "TGT-"};
    strcpy((char *)gd_world_start + GD_STR, words[gd_rand(4u)]);
    for (unsigned i = 0; i < 4u; ++i) {
        gd_w32(GD_CURS + 0x10u * i + 4u, gd_rand(2u) ? 0u : random32());
        gd_w32(GD_CURS + 0x10u * i + 8u, random32());
        gd_set32(0x0049b08cu + 4u * i, gd_at(GD_CURS + 0x10u * i));
    }
    gd_set32(0x0049b088u, gd_at(GD_H));
    gd_set32(0x004d24eau, gd_at(GD_EYE));
    gd_set32(0x0049be78u, gd_at(GD_TBL));
    gd_set32(0x0049be70u, gd_at(GD_STEPS));
    gd_set32(0x006c5ff8u, gd_at(GD_CAM));
    gd_set32(0x006c5f70u, gd_at(GD_GC0));
    gd_set32(0x006c5ed0u, gd_at(GD_SURF));
    gd_shx = gd_rand(3u);
    gd_shy = gd_rand(3u);
    gd_set16(0x004d253au, gd_shx);
    gd_set16(0x004d253cu, gd_shy);
    gd_set8(0x0049b106u, gd_rand(4u));
    gd_set16(0x004d3b8eu, random32());
    gd_set16(0x004d3b94u, random32());
    gd_set16(0x004d3b9au, random32());
    gd_set16(0x004d3b7cu, random32());
    for (uint32_t va = 0x004d1808u; va <= 0x004d1824u; va += 4u) gd_set32(va, gd_rand(40u));
    gd_set32(0x004d2a6cu, gd_rand(3u) == 0);
    gd_set32(0x004d1654u, gd_at(GD_STR));
    gd_set8(0x0049b105u, gd_rand(4u) != 0);
    gd_set32(0x0049b62cu, gd_rand(4u));
    gd_set16(0x004d2566u, gd_rand(2u));
    gd_set32(0x004a3620u, gd_signed(0x400u));
    gd_set32(0x004a3624u, gd_signed(0x400u));
    for (unsigned i = 0; i < 3u; ++i) gd_xf[i] = gd_signed(0x8000u);
    for (unsigned i = 0; i < 2u; ++i) { gd_proj[i][0] = random32(); gd_proj[i][1] = random32(); gd_scr[i][0] = gd_signed(0x400u); gd_scr[i][1] = gd_signed(0x400u); }
}
/* The sight's rectangle D+4..D+0x10 and its centre D+0x42/D+0x46. */
static void gd_rect(void) {
    uint32_t x0 = 40u + gd_rand(100u), y0 = 40u + gd_rand(100u), x1 = x0 + 100u + gd_rand(300u), y1 = y0 + 80u + gd_rand(200u);
    gd_w32(GD_D + 4u, x0);
    gd_w32(GD_D + 8u, y0);
    gd_w32(GD_D + 0xcu, x1);
    gd_w32(GD_D + 0x10u, y1);
    gd_w32(GD_D + 0x42u, (x0 + x1) / 2u + gd_signed(4u));
    gd_w32(GD_D + 0x46u, (y0 + y1) / 2u + gd_signed(4u));
}
/* A screen point: near the centre box, inside the rectangle or anywhere. */
static void gd_screen(unsigned k) {
    uint32_t cx = gd_r32(GD_D + 0x42u), cy = gd_r32(GD_D + 0x46u);
    switch (gd_rand(4u)) {
    case 0: case 1: gd_scr[k][0] = cx + gd_edge(5u << gd_shx); gd_scr[k][1] = cy + gd_edge(5u << gd_shy); break;
    case 2:
        gd_scr[k][0] = gd_r32(GD_D + 4u) + gd_rand(gd_r32(GD_D + 0xcu) - gd_r32(GD_D + 4u) + 1u) + (gd_rand(4u) ? 0u : gd_signed(2u));
        gd_scr[k][1] = gd_r32(GD_D + 8u) + gd_rand(gd_r32(GD_D + 0x10u) - gd_r32(GD_D + 8u) + 1u) + (gd_rand(4u) ? 0u : gd_signed(2u));
        break;
    default: break;   /* the random point of gd_common */
    }
}
static void gd_side(unsigned side) {
    lc_reset(side, 47, 0x01000000u, 1);
    lc_loop.dispatch = &gd_dispatch;
    es2_sim_load_headless(&gd_load, &lc_loop);
    es2_sim_load_bind(&gd_load);
    memset(&gd_view, 0, sizeof(gd_view));
    gd_view.loop = &lc_loop;
    es2_sim_view_bind(&gd_view);
    es2_sim_input_headless(&gd_input, &lc_loop, &lc_journal[side], 640, 480);
    es2_sim_input_bind(&gd_input);
    unsigned headless = gd_hud.headless;
    es2_sim_hud_init(&gd_hud, &lc_loop);
    gd_hud.headless = headless;
    es2_sim_hud_bind(&gd_hud);
    memcpy(gd_world, gd_world_start, GD_WORLD_SIZE);
    for (unsigned i = 0; i < gd_preset_count; ++i) {
        BYTE *p = mapped + (gd_presets[i].va - 0x400000u);
        if (gd_presets[i].width == 4) put32(p, gd_presets[i].value);
        else if (gd_presets[i].width == 2) put16(p, (uint16_t)gd_presets[i].value);
        else *p = (BYTE)gd_presets[i].value;
    }
    gd_side_now = side;
    gd_hash[side] = 0xcbf29ce484222325ull;
    gd_count[side] = 0;
    gd_text[side][0] = 0;
    memset(gd_calls[side], 0, sizeof(gd_calls[side]));
}
static int gd_run(const char *what, unsigned k) {
    for (unsigned side = 0; side < 2u; ++side) {
        gd_side(side);
        for (unsigned i = 0; i < gd_op_count; ++i) {
            gd_op *o = &gd_ops[i];
            uint32_t in[3] = {0, 0, 0}, out[3] = {0, 0, 0};
            static char where[200];
            snprintf(where, sizeof(where), "gadgets (%s side %u op %u: 0x%08lx(0x%lx, 0x%lx))", what, side, i, (unsigned long)o->va,
                     (unsigned long)o->a[0], (unsigned long)o->a[1]);
            current_function = where;
            if (!side) au_call((uint32_t)(uintptr_t)(mapped + (o->va - 0x400000u)), o->a, o->n, in, out);
            else {
                es2_sim_result r;
                memset(&r, 0, sizeof(r));
                if (es2_sim_call(&gd_dispatch, o->va, o->a, o->n, in, &r)) gd_bad = 1;
                out[0] = r.eax;
            }
            gd_res[side][i] = out[0];
            if (gd_bad) { fprintf(stderr, "FAIL: gadgets %s case %u op %u (0x%08lx): a call failed\n", what, k, i, (unsigned long)o->va); return 1; }
        }
        put32(gd_world + GD_GC0 + 0x22cu, 0);   /* a stack address on both sides (the fill record) */
        put32(gd_world + GD_GC1 + 0x22cu, 0);
        if (!side) memcpy(gd_world_start + GD_WORLD_SIZE, gd_world, GD_WORLD_SIZE);
        su_take(side);
    }
    if (gd_hash[0] != gd_hash[1] || gd_count[0] != gd_count[1]) {
        fprintf(stderr, "FAIL: gadgets %s case %u: recorded calls differ (%u / %u)\n  original:%s\n  own:     %s\n", what, k, gd_count[0],
                gd_count[1], gd_text[0], gd_text[1]);
        return 1;
    }
    if (su_compare(what, k)) return 1;
    if (memcmp(gd_world, gd_world_start + GD_WORLD_SIZE, GD_WORLD_SIZE)) {
        for (unsigned i = 0, shown = 0; i < GD_WORLD_SIZE && shown < 16u; ++i)
            if (gd_world[i] != gd_world_start[GD_WORLD_SIZE + i]) {
                fprintf(stderr, "  world +0x%04x original %02x own %02x\n", i, gd_world_start[GD_WORLD_SIZE + i], gd_world[i]);
                ++shown;
            }
        fprintf(stderr, "FAIL: gadgets %s case %u: the world differs (world at 0x%08lx)\n", what, k, (unsigned long)(uintptr_t)gd_world);
        return 1;
    }
    for (unsigned i = 0; i < gd_op_count; ++i)
        if (gd_ops[i].eax && gd_res[0][i] != gd_res[1][i]) {
            fprintf(stderr, "FAIL: gadgets %s case %u op %u (0x%08lx): EAX original 0x%08lx own 0x%08lx\n", what, k, i, (unsigned long)gd_ops[i].va,
                    (unsigned long)gd_res[0][i], (unsigned long)gd_res[1][i]);
            return 1;
        }
    return 0;
}

/* ---- the gunsights */
static unsigned gd_cov_fixed[3], gd_cov_target_hidden, gd_cov_corners, gd_cov_arrow, gd_cov_bracket;
static int gd_fixed_case(unsigned k) {
    gd_common();
    gd_rect();
    gd_w32(GD_D + 0x1au, gd_rand(4u) ? random32() | 1u : 0u);
    for (unsigned i = 0; i < 3u; ++i) gd_w32(GD_D + 0x1eu + 4u * i, gd_signed(0x8000u));
    gd_w8(GD_D + 0x39u, gd_rand(2u));
    gd_xf[1] = gd_rand(3u) ? (uint32_t)gd_near + gd_edge(1u) : gd_signed(0x8000u);
    gd_screen(0);
    gd_op_add(0x0043b808u, 0, 1, gd_at(GD_D), 0);
    int bad = gd_run("fixed", k);
    if (!bad)
        for (unsigned i = 0; i < 3u; ++i) gd_cov_fixed[i] = gd_bmp_hits[i];
    return bad;
}
static int gd_target_case(unsigned k) {
    gd_common();
    gd_rect();
    gd_w32(GD_D + 0x1au, gd_rand(8u) ? random32() | 1u : 0u);
    gd_w8(GD_D + 0x38u, gd_rand(8u) != 0);
    gd_w8(GD_D + 0x39u, gd_rand(3u) == 0);
    gd_w16(GD_D + 0x2cu, gd_rand(2u) ? 0u : random32());
    /* The target point: x at least 64 from the player's, y the same (the
     * distance 0x4928d0 is then |dx| and never 0: the bracket divides by it). */
    int32_t dx = 64 + (int32_t)gd_rand(0x4000u);
    if (gd_rand(2u)) dx = -dx;
    gd_w32(GD_D + 0x1eu, gd_r32(GD_P + 0x26u) + (uint32_t)dx);
    gd_w32(GD_D + 0x22u, gd_r32(GD_P + 0x2au));
    gd_w32(GD_D + 0x26u, gd_signed(0x8000u));
    unsigned shift = gd_r32(GD_EYE + 0x1au) & 31u;
    uint32_t span = (uint32_t)(dx < 0 ? -dx : dx), e = gd_rand(60u);
    gd_w32(GD_D + 0x30u, 2u * ((e * span) >> shift) + gd_rand(2u));
    gd_xf[1] = gd_rand(3u) ? (uint32_t)gd_near + gd_edge(1u) : gd_rand(4u) ? gd_signed(0x8000u) : 0u;
    if (gd_rand(4u) == 0) gd_xf[0] = 0;
    gd_xf[2] = gd_rand(2u) ? gd_signed(0x400u) : gd_signed(0x40000u);
    gd_screen(0);
    gd_screen(1);
    gd_op_add(0x0043b978u, 0, 1, gd_at(GD_D), 0);
    unsigned before = gd_kinds[8], blits = gd_kinds[5], pops = gd_flags_hits[3];
    int bad = gd_run("target", k);
    if (!bad) {
        if (!gd_r32(GD_D + 0x1au) || !gd_world_start[GD_D + 0x38u]) ++gd_cov_target_hidden;
        if (gd_kinds[8] != before) ++gd_cov_arrow;
        if (gd_kinds[5] != blits) ++gd_cov_bracket;
        if (gd_flags_hits[3] != pops) ++gd_cov_corners;
    }
    return bad;
}
static unsigned gd_cov_roving[2];
static int gd_roving_case(unsigned k) {
    gd_common();
    gd_rect();
    uint32_t x0 = gd_r32(GD_D + 4u), y0 = gd_r32(GD_D + 8u), x1 = gd_r32(GD_D + 0xcu), y1 = gd_r32(GD_D + 0x10u);
    static const uint32_t pick[4] = {0, 1, 2, 3};
    uint32_t xs[4] = {x0 - 1u, x0, x1, x1 + 1u}, ys[4] = {y0 - 1u, y0, y1, y1 + 1u};
    if (gd_rand(2u)) {
        gd_set32(0x004a3620u, xs[pick[gd_rand(4u)]]);
        gd_set32(0x004a3624u, ys[pick[gd_rand(4u)]]);
    } else if (gd_rand(2u)) {
        gd_set32(0x004a3620u, x0 + gd_rand(x1 - x0 + 1u));
        gd_set32(0x004a3624u, y0 + gd_rand(y1 - y0 + 1u));
    }
    gd_set32(0x0049b62cu, gd_rand(3u));
    gd_op_add(0x0043c204u, 0, 1, gd_at(GD_D), 0);
    int bad = gd_run("roving", k);
    if (!bad) ++gd_cov_roving[get32(mapped + (0x0049b62cu - 0x400000u)) == 1u];
    return bad;
}
static int gd_cursor_case(unsigned k) {
    gd_common();
    switch (gd_rand(4u)) {
    case 0: gd_op_add(0x00433d90u, 0, 1, gd_rand(4u), 0); break;
    case 1: gd_op_add(0x00486eceu, 1, 1, gd_at(GD_MOUSE), 0); break;
    case 2: gd_op_add(0x0043495cu, 1, 0, 0, 0); break;
    default: gd_op_add(0x0043b7ccu, 1, 1, gd_at(GD_D), 0); break;
    }
    return gd_run("cursor", k);
}

/* ---- the gauges */
static unsigned gd_cov_needle[2];
static int gd_rotation_case(unsigned k) {
    gd_common();
    static const uint16_t values[] = {299u, 300u, 301u, 0xfed5u, 0xfed4u, 0xfed3u, 0u, 0xffffu};
    uint16_t si = gd_rand(3u) ? values[gd_rand(8u)] : (uint16_t)random32();
    gd_w16(GD_D + 0x3au, si);
    gd_w16(GD_D + 0x24u, gd_rand(2u) ? 0u : random32());
    gd_w32(GD_D + 0x28u, gd_signed(0x4000u));
    gd_w16(GD_D + 0x2cu, 1u + gd_rand(0xffffu));
    gd_w32(GD_D + 4u, gd_signed(0x400u));
    gd_w32(GD_D + 8u, gd_signed(0x400u));
    gd_op_add(0x0043b4ccu, 0, 1, gd_at(GD_D), 0);
    int bad = gd_run("rotation", k);
    if (!bad) ++gd_cov_needle[(int16_t)si >= 300 || (int16_t)si <= -300];
    return bad;
}
static unsigned gd_cov_step_back, gd_cov_step_wrap, gd_cov_step_plain;
static int gd_step_case(unsigned k) {
    gd_common();
    int32_t count = (int32_t)gd_r32(GD_STEPS + 4u);
    for (unsigned tries = 0; tries < 64u; ++tries) {
        int32_t step = 1 + (int32_t)gd_rand(0x1000u);
        if (gd_rand(2u)) step = -step;
        int32_t q_t = (int32_t)gd_rand((uint32_t)count), r_t = (int32_t)gd_rand((uint32_t)(step < 0 ? -step : step));
        int64_t p_t = (int64_t)q_t * step + (step < 0 ? -r_t : r_t);
        uint32_t v = 1u + gd_rand(0xffffu);
        int32_t m = (int32_t)((p_t * 65536) / (int64_t)v);
        int32_t p = (int32_t)(uint32_t)(((int64_t)v * m) >> 16), q = p / step, r = p % step;
        if (!(step > r)) ++q;
        if (q < 0 || q > count) continue;
        gd_w32(GD_D + 0x28u, (uint32_t)step);
        gd_w32(GD_D + 0x40u, (uint32_t)m);
        gd_w32(GD_D + 4u, gd_signed(0x400u));
        gd_op_add(0x0043b67cu, 1, 2, gd_at(GD_D), v | (gd_rand(2u) ? random32() & 0xffff0000u : 0u));
        int bad = gd_run("step", k);
        if (!bad) {
            if (step < 0 && q == count) ++gd_cov_step_wrap;
            else if (step < 0) ++gd_cov_step_back;
            else ++gd_cov_step_plain;
        }
        return bad;
    }
    return 0;
}
static unsigned gd_cov_nav[4];
static int gd_nav_case(unsigned k) {
    gd_common();
    gd_rect();
    gd_w8(GD_D + 0x45u, gd_rand(3u) == 0);
    gd_w32(GD_D + 0x28u, gd_signed(0x40u));
    gd_w16(GD_D + 0x2cu, 1u + gd_rand(0x100u));
    if (gd_rand(5u)) put32(gd_world_start + GD_P + 0x45u, gd_at(GD_ROUTE));
    uint32_t count = gd_rand(7u);
    gd_w16(GD_ROUTE + 4u, gd_rand(count + 3u) - 1u);
    put32(gd_world_start + GD_ROUTE + 6u, gd_rand(6u) ? gd_at(GD_LIST) : 0u);
    gd_w16(GD_LIST, count);
    put32(gd_world_start + GD_LIST + 2u, gd_at(GD_PPTR));
    for (unsigned i = 0; i < 16u; ++i) {
        gd_w32(GD_PPTR + 4u * i, gd_rand(5u) ? gd_at(GD_PTS + 8u * i) : 0u);
        gd_w32(GD_PTS + 8u * i, gd_signed(0x8000u));
        gd_w32(GD_PTS + 8u * i + 4u, gd_signed(0x8000u));
    }
    if (gd_rand(3u)) gd_w32(GD_P + 0x1a4u, gd_at(GD_TARGET));
    gd_w32(GD_TARGET + 0x26u, gd_signed(0x8000u));
    gd_w32(GD_TARGET + 0x2au, gd_signed(0x8000u));
    gd_op_add(0x0043c40cu, 0, 1, gd_at(GD_D), 0);
    unsigned n3 = gd_poly_n[0][3], n4 = gd_poly_n[0][4], labels = gd_kinds[6];
    int bad = gd_run("nav", k);
    if (!bad) {
        if (gd_poly_n[0][3] != n3) ++gd_cov_nav[0];
        else if (gd_poly_n[0][4] != n4) ++gd_cov_nav[1];
        else ++gd_cov_nav[3];
        if (gd_kinds[6] != labels) ++gd_cov_nav[2];
    }
    return bad;
}

/* The nav gauge's text on the original side: the runtime's itoa (the
 * strcat stub is startup_cases.h's). */
static uint32_t __cdecl gd_itoa(uint32_t v, uint32_t buffer, uint32_t radix) { uint32_t a[3] = {v, buffer, radix}; return su_rtl_call(ES2_RTL_ITOA, a); }
static int gadgets_cases(void) {
    current_function = "gadgets";
    es2_sim_identity_memory(&ir_memory);
    es2_sim_rtl_memory(&ir_memory, &su_rtl);
    su_data_pristine = malloc(SU_DATA_SIZE);
    su_snap[0] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    su_snap[1] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    ir_region = ww_alloc(IR_REGION_SIZE);
    lc_stack = VirtualAlloc(NULL, 0x10000u, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    lc_bits = ww_alloc(LC_BITS_SIZE);
    gd_world = VirtualAlloc(NULL, GD_WORLD_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    gd_world_start = malloc(2u * GD_WORLD_SIZE);
    if (!su_data_pristine || !su_snap[0] || !su_snap[1] || !ir_region || !lc_stack || !lc_bits || !gd_world || !gd_world_start)
        return fail("gadgets: memory");
    if (get32(mapped + (SU_COUNTER - 0x400000u))) return fail("gadgets: the counter word is not free");
    su_pristine_take();
    static const struct { const char *dll, *name; void *stub; } stubs[] = {
        {"cw3220.dll", "_calloc", (void *)ir_calloc}, {"cw3220.dll", "_free", (void *)ir_free},
        {"cw3220.dll", "_memset", (void *)ir_memset}, {"cw3220.dll", "_memmove", (void *)ir_memmove},
        {"cw3220.dll", "_itoa", (void *)gd_itoa}, {"cw3220.dll", "_strcat", (void *)su_strcat},
    };
    for (unsigned i = 0; i < sizeof(stubs) / sizeof(stubs[0]); ++i)
        if (!ir_patch_import(stubs[i].dll, stubs[i].name, stubs[i].stub)) {
            fprintf(stderr, "import %s!%s\n", stubs[i].dll, stubs[i].name);
            return fail("gadgets: import slot not found");
        }
    if (gd_dispatch_setup()) return 1;
    for (unsigned i = 0; i < GD_STUBS; ++i) if (gd_patch(gd_stubs[i].va, gd_stubs[i].stub, gd_saved[i], 1)) return 1;
    int bad = 0;
    static const struct { const char *name; int (*run)(unsigned); unsigned cases; } parts[] = {
        {"fixed", gd_fixed_case, 400}, {"target", gd_target_case, 1200}, {"roving", gd_roving_case, 300}, {"cursor", gd_cursor_case, 400},
        {"rotation", gd_rotation_case, 300}, {"step", gd_step_case, 300}, {"nav", gd_nav_case, 1200},
    };
    for (unsigned p = 0; !bad && p < sizeof(parts) / sizeof(parts[0]); ++p) {
        for (unsigned k = 0; !bad && k < parts[p].cases; ++k) {
            current_case = k; current_case_valid = 1;
            bad = parts[p].run(k);
        }
        if (!bad) printf("PASS: gadgets %s: %u cases identical\n", parts[p].name, parts[p].cases);
    }
    for (unsigned i = 0; i < GD_STUBS; ++i) if (gd_patch(gd_stubs[i].va, gd_stubs[i].stub, gd_saved[i], 0)) bad = 1;
    if (!bad)
        printf("GADGETS coverage: fixed bitmaps 0/1/2 %u/%u/%u; target hidden %u, bracket %u, corners %u, arrow %u; roving in/out %u/%u; "
               "needle 13/12 %u/%u; step plain %u, back %u, wrap %u; nav arrow %u, diamond %u, none %u, text %u; calls: select %u, transform %u, "
               "project %u, screen %u, blit %u, label %u, fill %u, any %u, cursor sets %u/%u, draw slot %u; blit flags 0/1/2/3 %u/%u/%u/%u; "
               "bitmaps 3/7/11/12/13 %u/%u/%u/%u/%u; headless %u\n",
               gd_cov_fixed[0], gd_cov_fixed[1], gd_cov_fixed[2], gd_cov_target_hidden, gd_cov_bracket, gd_cov_corners, gd_cov_arrow,
               gd_cov_roving[0], gd_cov_roving[1], gd_cov_needle[0], gd_cov_needle[1], gd_cov_step_plain, gd_cov_step_back, gd_cov_step_wrap,
               gd_cov_nav[0], gd_cov_nav[1], gd_cov_nav[3], gd_cov_nav[2], gd_kinds[1], gd_kinds[2], gd_kinds[3], gd_kinds[4], gd_kinds[5],
               gd_kinds[6], gd_kinds[7], gd_kinds[8], gd_kinds[9], gd_kinds[10], gd_kinds[11], gd_flags_hits[0], gd_flags_hits[1],
               gd_flags_hits[2], gd_flags_hits[3], gd_bmp_hits[3], gd_bmp_hits[7], gd_bmp_hits[11], gd_bmp_hits[12], gd_bmp_hits[13],
               gd_hud.headless);
    current_case_valid = 0;
    put32(mapped + 0x2cbee4u, 0);
    memcpy(mapped + SU_DATA_RVA, su_data_pristine, SU_DATA_SIZE);
    return bad;
}
