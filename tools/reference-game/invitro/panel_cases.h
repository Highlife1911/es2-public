/* The cockpit panel pieces, original machine code against sim/src/panel.c
 * (--panel; target :differential_panel_test). Included after gadgets_cases.h;
 * reuses startup_cases.h's DATA and heap snapshots (su_take, su_compare),
 * loop_cases.h's loop set-up (lc_reset) and audio_cases.h's register-capturing
 * machine call (au_call). Identifiers: pn_/PN_.
 *
 * Every case builds a synthetic panel H [0x49b088] with its widget root
 * [H] in an identity-mapped world, the surface [0x6c5ed0] with its width
 * and height, presets the DATA words the code reads (the shifts
 * 0x4d253a/c), then runs one call on both sides: side 0 the original's
 * code, side 1 the sim's rows through a dispatch of the sim's real
 * services. Operator new 0x47749c is a recorder on both sides (side 0: a
 * jump patched over its entry; side 1: a row at the same VA) that either
 * fails (NULL) or allocates from the shared heap, so that both branches of
 * every allocation test are taken; its argument is hashed. Compared: the
 * recorder hash and count, DATA and the used heap, the world, EAX, ECX and
 * EDX where the contract defines them. */
#pragma once
/* The headers this suite builds on (sim_main.c). */
#include "init_records_cases.h"
#include "startup_cases.h"
#include "loop_cases.h"
#include "gadgets_cases.h"
#include "audio_cases.h"
#include "es2/sim/load.h"
#include "es2/sim/view.h"
#include "es2/sim/input.h"
#include "es2/sim/hud.h"

#define PN_WORLD_SIZE 0x2000u
#define PN_H 0x0000u        /* the panel, 0x280 */
#define PN_ROOT 0x0300u     /* the widget root [H], 0x300 */
#define PN_SURF 0x0700u     /* the surface [0x6c5ed0] */
static BYTE *pn_world, *pn_world_start;
static es2_sim_entry pn_entries[4096];
static es2_sim_dispatch pn_dispatch;
static es2_sim_load pn_load;
static es2_sim_view pn_view;
static es2_sim_input pn_input;
static es2_sim_hud pn_hud;
static int pn_bad;
static void pn_refuse(const es2_sim_refusal *r, void *user) {
    (void)user;
    fprintf(stderr, "FAIL: own panel: refused call to 0x%08lx (%s): %s\n", (unsigned long)r->va, r->name ? r->name : "?",
            r->reason ? r->reason : "");
    pn_bad = 1;
}

/* ---- the recorders */
static uint64_t pn_hash[2];
static unsigned pn_count[2], pn_side_now, pn_calls[2][16];
static char pn_text[2][512];
static unsigned pn_kinds[16], pn_new_fail_mask, pn_new_nulls;
static uint32_t pn_heading, pn_sincos[2][2], pn_rot[2][2];
static void pn_mix(uint32_t v) {
    for (unsigned i = 0; i < 4u; ++i) { pn_hash[pn_side_now] ^= (BYTE)(v >> (8u * i)); pn_hash[pn_side_now] *= 0x100000001b3ull; }
}
static void pn_bytes(uint32_t va, uint32_t n) { for (uint32_t i = 0; i < n; ++i) pn_mix(*(BYTE *)(uintptr_t)(va + i)); }
static void pn_note(unsigned kind, const uint32_t *a, unsigned n) {
    unsigned side = pn_side_now;
    pn_mix(0xabc00000u + kind);
    for (unsigned i = 0; i < n; ++i) pn_mix(a[i]);
    size_t used = strlen(pn_text[side]);
    if (pn_count[side] < 14u && used + 40u < sizeof(pn_text[side]))
        snprintf(pn_text[side] + used, sizeof(pn_text[side]) - used, " %u(%lx,%lx)", kind, (unsigned long)(n ? a[0] : 0),
                 (unsigned long)(n > 1u ? a[1] : 0));
    ++pn_count[side];
    if (side) ++pn_kinds[kind];
}
/* The colour record behind the current context's +0x228 or +0x22c. */
static void pn_record_of(uint32_t field) {
    uint32_t g = get32(mapped + (0x006c5f70u - 0x400000u));
    pn_bytes(get32((BYTE *)(uintptr_t)(g + field)), 8u);
}
static void pn_put_pair(uint32_t va, const uint32_t v[2]) {
    put32((BYTE *)(uintptr_t)va, v[0]);
    put32((BYTE *)(uintptr_t)(va + 4u), v[1]);
}
/* One recorded call (the same on both sides); returns its EAX. Stack
 * addresses are never hashed, only the bytes behind them. */
static uint32_t pn_record(unsigned kind, const uint32_t *a) {
    unsigned side = pn_side_now, k = pn_calls[side][kind]++;
    switch (kind) {
    case 1: {   /* operator new(n): NULL for the case's failing calls, else the shared heap */
        pn_note(1, a, 1);
        if (k < 32u && (pn_new_fail_mask >> k) & 1u) {
            if (side) ++pn_new_nulls;
            return 0;
        }
        return (uint32_t)(uintptr_t)ir_calloc(1, a[0]);
    }
    case 2: case 3:   /* 0x486704 / 0x48817c(rect): the rectangle and the fill record +0x22c */
        pn_note(kind, NULL, 0);
        pn_bytes(a[0], 16u);
        pn_record_of(0x22cu);
        return 0;
    case 4:   /* 0x4895ec(pt): the point and the pen record +0x228 */
        pn_note(4, NULL, 0);
        pn_bytes(a[0], 8u);
        pn_record_of(0x228u);
        return 0;
    case 5:   /* 0x483a04(a, b): both points and the pen record +0x228 */
        pn_note(5, NULL, 0);
        pn_bytes(a[0], 8u);
        pn_bytes(a[1], 8u);
        pn_record_of(0x228u);
        return 0;
    case 6: {   /* 0x45caa4(desc, B, pt, flags, size, angle) */
        uint32_t v[4] = {a[0], a[1], a[3], a[5]};
        pn_note(6, v, 4);
        pn_bytes(a[2], 8u);
        pn_bytes(a[4], 8u);
        return 0;
    }
    case 7:   /* 0x45c8bc(desc, B, pt) */
        pn_note(7, a, 2);
        pn_bytes(a[2], 8u);
        return 0;
    case 8: {   /* 0x438920(L, text) */
        pn_note(8, a, 1);
        uint32_t n = 0;
        while (n < 80u && *(BYTE *)(uintptr_t)(a[1] + n)) ++n;
        pn_mix(n);
        pn_bytes(a[1], n);
        return 0;
    }
    case 9:   /* 0x47eb30(angle, out): the scripted pair */
        pn_note(9, a, 1);
        pn_put_pair(a[1], pn_sincos[k & 1u]);
        return 0;
    case 10:   /* 0x47ffe8(in, sc, out): the scripted pair */
        pn_note(10, NULL, 0);
        pn_bytes(a[0], 8u);
        pn_bytes(a[1], 8u);
        pn_put_pair(a[2], pn_rot[k & 1u]);
        return 0;
    case 11: pn_note(11, a, 1); return 0;                 /* 0x43ec08(R), the blip scan */
    case 12: pn_note(12, a, 1); return pn_heading;        /* the player's slot +0x3c(P) */
    default: pn_note(13, a, 1); return 0x5a000000u ^ a[0];   /* R's slot 0 (R) */
    }
}
#define PN_HOST1(k) static uint32_t __cdecl pn_o##k(uint32_t a) { const uint32_t s[1] = {a}; return pn_record(k, s); }
#define PN_HOST2(k) static uint32_t __cdecl pn_o##k(uint32_t a, uint32_t b) { const uint32_t s[2] = {a, b}; return pn_record(k, s); }
#define PN_HOST3(k) static uint32_t __cdecl pn_o##k(uint32_t a, uint32_t b, uint32_t c) { const uint32_t s[3] = {a, b, c}; return pn_record(k, s); }
PN_HOST1(1)
PN_HOST1(2)
PN_HOST1(3)
PN_HOST1(4)
PN_HOST2(5)
static uint32_t __cdecl pn_o6(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f) {
    const uint32_t s[6] = {a, b, c, d, e, f};
    return pn_record(6, s);
}
PN_HOST3(7)
PN_HOST2(8)
PN_HOST2(9)
PN_HOST3(10)
PN_HOST1(11)
PN_HOST1(12)
PN_HOST1(13)
static void pn_s_stub(es2_sim_service_call *sc, void *user) { sc->eax = pn_record((unsigned)(uintptr_t)user, sc->slots); }
static const struct { uint32_t va; void *stub; unsigned kind, slots; } pn_stubs[] = {
    {0x0047749cu, (void *)pn_o1, 1, 1}, {0x00486704u, (void *)pn_o2, 2, 1}, {0x0048817cu, (void *)pn_o3, 3, 1},
    {0x004895ecu, (void *)pn_o4, 4, 1}, {0x00483a04u, (void *)pn_o5, 5, 2}, {0x0045caa4u, (void *)pn_o6, 6, 6},
    {0x0045c8bcu, (void *)pn_o7, 7, 3}, {0x00438920u, (void *)pn_o8, 8, 2}, {0x0047eb30u, (void *)pn_o9, 9, 2},
    {0x0047ffe8u, (void *)pn_o10, 10, 3}, {0x0043ec08u, (void *)pn_o11, 11, 1},
};
#define PN_STUBS (sizeof(pn_stubs) / sizeof(pn_stubs[0]))
static BYTE pn_saved[PN_STUBS][5];
static int pn_patch(uint32_t va, void *target, BYTE saved[5], int install) {
    BYTE *site = mapped + (va - 0x400000u);
    DWORD previous;
    if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &previous)) return fail("panel: stub protection");
    if (install) { memcpy(saved, site, 5); site[0] = 0xe9; put32(site + 1, (uint32_t)(uintptr_t)target - (uint32_t)(uintptr_t)(site + 5)); }
    else memcpy(site, saved, 5);
    if (!VirtualProtect(site, 5, previous, &previous)) return fail("panel: stub restore protection");
    FlushInstructionCache(GetCurrentProcess(), site, 5);
    return 0;
}
static int pn_registered(uint32_t va) {
    for (unsigned i = 0; i < pn_dispatch.count; ++i) if (pn_dispatch.entries[i].va == va) return 1;
    return 0;
}
/* The recorders: VAs without a row are added before the seal, rows are
 * replaced after it (es2_sim_dispatch_find works only then). The host
 * slots (the player's +0x3c, R's slot 0) are rows at their addresses. */
static int pn_dispatch_setup(void) {
    es2_sim_dispatch_init(&pn_dispatch, pn_entries, 4096, pn_refuse, NULL);
    static es2_sim_services s;
    s.memory = &ir_memory;
    s.heap = &ir_heap;
    s.vt = &lc_vt;
    s.fs0 = NULL;
    if (es2_sim_register_services(&pn_dispatch, &s)) return fail("panel: dispatch");
    static int replace[PN_STUBS];
    for (unsigned i = 0; i < PN_STUBS; ++i) {
        replace[i] = pn_registered(pn_stubs[i].va);
        if (!replace[i] && es2_sim_dispatch_add_service(&pn_dispatch, pn_stubs[i].va, "panel recorder", pn_s_stub,
                                                        (void *)(uintptr_t)pn_stubs[i].kind, 0, (uint8_t)pn_stubs[i].slots))
            return fail("panel: dispatch");
    }
    if (es2_sim_dispatch_add_service(&pn_dispatch, (uint32_t)(uintptr_t)pn_o12, "panel heading slot", pn_s_stub, (void *)(uintptr_t)12u, 0, 1) ||
        es2_sim_dispatch_add_service(&pn_dispatch, (uint32_t)(uintptr_t)pn_o13, "panel draw slot", pn_s_stub, (void *)(uintptr_t)13u, 0, 1))
        return fail("panel: dispatch");
    const char *error = NULL;
    uint32_t bad = 0;
    if (es2_sim_dispatch_seal(&pn_dispatch, &error, &bad)) return fail("panel: dispatch seal");
    for (unsigned i = 0; i < PN_STUBS; ++i) {
        if (!replace[i]) continue;
        es2_sim_entry *e = (es2_sim_entry *)es2_sim_dispatch_find(&pn_dispatch, pn_stubs[i].va);
        if (!e) return fail("panel: a replaced row is missing");
        e->kind = ES2_SIM_SERVICE; e->name = "panel recorder"; e->service = pn_s_stub;
        e->service_user = (void *)(uintptr_t)pn_stubs[i].kind; e->pop_bytes = 0; e->stack_slots = (uint8_t)pn_stubs[i].slots;
    }
    return 0;
}

/* ---- the case script */
typedef struct pn_preset { uint32_t va, value; unsigned width; } pn_preset;
static pn_preset pn_presets[64];
static unsigned pn_preset_count;
static void pn_set32(uint32_t va, uint32_t v) { pn_presets[pn_preset_count++] = (pn_preset){va, v, 4}; }
static void pn_set16(uint32_t va, uint32_t v) { pn_presets[pn_preset_count++] = (pn_preset){va, v, 2}; }
/* World dwords that hold stack addresses after the call (compared only as
 * changed or not). */
static uint32_t pn_mask[8];
static unsigned pn_mask_count;
/* An op: the VA, its arguments, which of EAX/ECX/EDX (bits 0/1/2) the
 * contract defines. */
typedef struct pn_op { uint32_t va, a[3]; unsigned n, regs; } pn_op;
static pn_op pn_ops[4];
static unsigned pn_op_count;
static uint32_t pn_res[2][4][3];
static uint32_t pn_rand(uint32_t n) { return random32() % n; }
static uint32_t pn_at(uint32_t offset) { return (uint32_t)(uintptr_t)pn_world + offset; }
static void pn_w32(uint32_t offset, uint32_t v) { put32(pn_world_start + offset, v); }
static void pn_w16(uint32_t offset, uint32_t v) { put16(pn_world_start + offset, (uint16_t)v); }
static void pn_op_add(uint32_t va, unsigned regs, unsigned n, uint32_t a0, uint32_t a1, uint32_t a2) {
    pn_ops[pn_op_count++] = (pn_op){va, {a0, a1, a2}, n, regs};
}
static void pn_common(void) {
    memset(pn_world_start, 0, PN_WORLD_SIZE);
    pn_preset_count = 0;
    pn_mask_count = 0;
    pn_op_count = 0;
    pn_new_fail_mask = 0;
    random_bytes(pn_world_start + PN_H, 0x280);
    pn_w32(PN_H, pn_at(PN_ROOT));
    random_bytes(pn_world_start + PN_ROOT, 0x300);
    random_bytes(pn_world_start + PN_SURF, 0x40);
    pn_set32(0x0049b088u, pn_at(PN_H));
    pn_set32(0x006c5ed0u, pn_at(PN_SURF));
    pn_set16(0x004d253au, pn_rand(4u) ? pn_rand(3u) : random32());
    pn_set16(0x004d253cu, pn_rand(4u) ? pn_rand(3u) : random32());
}
static void pn_side(unsigned side) {
    lc_reset(side, 47, 0x01000000u, 1);
    lc_loop.dispatch = &pn_dispatch;
    es2_sim_load_headless(&pn_load, &lc_loop);
    es2_sim_load_bind(&pn_load);
    memset(&pn_view, 0, sizeof(pn_view));
    pn_view.loop = &lc_loop;
    es2_sim_view_bind(&pn_view);
    es2_sim_input_headless(&pn_input, &lc_loop, &lc_journal[side], 640, 480);
    es2_sim_input_bind(&pn_input);
    unsigned headless = pn_hud.headless;
    es2_sim_hud_init(&pn_hud, &lc_loop);
    pn_hud.headless = headless;
    es2_sim_hud_bind(&pn_hud);
    memcpy(pn_world, pn_world_start, PN_WORLD_SIZE);
    for (unsigned i = 0; i < pn_preset_count; ++i) {
        BYTE *p = mapped + (pn_presets[i].va - 0x400000u);
        if (pn_presets[i].width == 4) put32(p, pn_presets[i].value);
        else if (pn_presets[i].width == 2) put16(p, (uint16_t)pn_presets[i].value);
        else *p = (BYTE)pn_presets[i].value;
    }
    pn_side_now = side;
    pn_hash[side] = 0xcbf29ce484222325ull;
    pn_count[side] = 0;
    pn_text[side][0] = 0;
    memset(pn_calls[side], 0, sizeof(pn_calls[side]));
}
static int pn_run(const char *what, unsigned k) {
    for (unsigned side = 0; side < 2u; ++side) {
        pn_side(side);
        for (unsigned i = 0; i < pn_op_count; ++i) {
            pn_op *o = &pn_ops[i];
            uint32_t in[3] = {0, 0, 0}, out[3] = {0, 0, 0};
            static char where[200];
            snprintf(where, sizeof(where), "panel (%s side %u op %u: 0x%08lx(0x%lx, 0x%lx))", what, side, i, (unsigned long)o->va,
                     (unsigned long)o->a[0], (unsigned long)o->a[1]);
            current_function = where;
            if (!side) au_call((uint32_t)(uintptr_t)(mapped + (o->va - 0x400000u)), o->a, o->n, in, out);
            else {
                es2_sim_result r;
                memset(&r, 0, sizeof(r));
                if (es2_sim_call(&pn_dispatch, o->va, o->a, o->n, in, &r)) pn_bad = 1;
                out[0] = r.eax; out[1] = r.ecx; out[2] = r.edx;
            }
            if (!side) { uint32_t edx = out[1]; out[1] = out[2]; out[2] = edx; }   /* au_call: EAX, EDX, ECX */
            memcpy(pn_res[side][i], out, sizeof(out));
            if (pn_bad) { fprintf(stderr, "FAIL: panel %s case %u op %u (0x%08lx): a call failed\n", what, k, i, (unsigned long)o->va); return 1; }
        }
        if (!side) memcpy(pn_world_start + PN_WORLD_SIZE, pn_world, PN_WORLD_SIZE);
        su_take(side);
    }
    if (pn_hash[0] != pn_hash[1] || pn_count[0] != pn_count[1]) {
        fprintf(stderr, "FAIL: panel %s case %u: recorded calls differ (%u / %u)\n  original:%s\n  own:     %s\n", what, k, pn_count[0],
                pn_count[1], pn_text[0], pn_text[1]);
        return 1;
    }
    if (su_compare(what, k)) return 1;
    for (unsigned i = 0; i < pn_mask_count; ++i) {
        uint32_t o = pn_mask[i], start = get32(pn_world_start + o);
        BYTE *w[2] = {pn_world_start + PN_WORLD_SIZE + o, pn_world + o};
        for (unsigned side = 0; side < 2u; ++side) put32(w[side], get32(w[side]) == start ? 0x11111111u : 0x22222222u);
    }
    if (memcmp(pn_world, pn_world_start + PN_WORLD_SIZE, PN_WORLD_SIZE)) {
        for (unsigned i = 0, shown = 0; i < PN_WORLD_SIZE && shown < 16u; ++i)
            if (pn_world[i] != pn_world_start[PN_WORLD_SIZE + i]) {
                fprintf(stderr, "  world +0x%04x original %02x own %02x\n", i, pn_world_start[PN_WORLD_SIZE + i], pn_world[i]);
                ++shown;
            }
        fprintf(stderr, "FAIL: panel %s case %u: the world differs (world at 0x%08lx)\n", what, k, (unsigned long)(uintptr_t)pn_world);
        return 1;
    }
    static const char *const names[3] = {"EAX", "ECX", "EDX"};
    for (unsigned i = 0; i < pn_op_count; ++i)
        for (unsigned r = 0; r < 3u; ++r)
            if ((pn_ops[i].regs >> r) & 1u && pn_res[0][i][r] != pn_res[1][i][r]) {
                fprintf(stderr, "FAIL: panel %s case %u op %u (0x%08lx): %s original 0x%08lx own 0x%08lx\n", what, k, i,
                        (unsigned long)pn_ops[i].va, names[r], (unsigned long)pn_res[0][i][r], (unsigned long)pn_res[1][i][r]);
                return 1;
            }
    return 0;
}

/* ---- 0x433770, the scroll zones */
static unsigned pn_cov_zones[2];
static int pn_zones_case(unsigned k) {
    pn_common();
    static const uint32_t sizes[] = {0, 1, 2, 320, 640, 0x8000u, 0x8001u, 0x10000u, 0x10001u};
    pn_w32(PN_SURF + 8u, pn_rand(3u) ? sizes[pn_rand(9u)] : random32());
    pn_w32(PN_SURF + 0xcu, pn_rand(3u) ? sizes[pn_rand(9u)] : random32());
    pn_w16(PN_ROOT + 0x254u, pn_rand(40u));   /* a negative count makes the original overwrite the count itself */
    pn_new_fail_mask = pn_rand(2u) ? 0u : pn_rand(8u);
    pn_op_add(0x00433770u, 7u, 1, pn_at(PN_H), 0, 0);
    int bad = pn_run("zones", k);
    if (!bad) ++pn_cov_zones[pn_new_fail_mask != 0];
    return bad;
}

/* ---- the radar page: 0x43eef4 (draw), 0x43f2d8 (HUD radar), 0x43eedc
 * (slot +4). The MFDRadar R with its blips, the range object [R+8], the
 * sweep bitmap header [R+0x14], R's vtable (slot 0 a host recorder), the
 * player P [H+0x203] (slot +0x3c a host recorder returning the heading),
 * the MFD M [H+0x1ed] with its page table, the current context G
 * [0x6c5f70] (its +0x228/+0x22c masked: stack addresses on both sides),
 * the MFD bitmap table [[0x49cbcc]+8] and the two label strings. */
#define PN_R 0x1000u       /* the radar, 0x530 */
#define PN_OBJ 0x1540u     /* [R+8]: +0xc the range */
#define PN_BMP 0x1560u     /* [R+0x14]: words +4/+6 */
#define PN_TPT 0x1570u     /* a target point outside the blips */
#define PN_P 0x1600u       /* the player */
#define PN_PVT 0x1640u     /* its vtable */
#define PN_MFD 0x1680u     /* the MFD, 0x100 */
#define PN_GC 0x1780u      /* the current context, 0x240 */
#define PN_T 0x19c0u       /* [0x49cbcc] */
#define PN_TBL 0x19d0u     /* [PN_T+8], 0x50 */
#define PN_STR 0x1a40u     /* two strings of 0x20 */
#define PN_RVT 0x1a80u     /* R's vtable */
#define PN_PT 0x1a90u      /* the HUD radar's point */
static unsigned pn_cov_radar[16];
static uint32_t pn_signed(uint32_t range) { return (uint32_t)((int32_t)pn_rand(2u * range + 1u) - (int32_t)range); }
static uint32_t pn_shift_word(void) {
    static const uint32_t words[] = {0, 1, 2, 0x100u, 0x101u, 0x120u, 0x21u, 0x122u};
    return words[pn_rand(8u)];
}
static void pn_set8(uint32_t va, uint32_t v) { pn_presets[pn_preset_count++] = (pn_preset){va, v, 1}; }
/* The radar world; returns the radar draw's scale (for the case's
 * divisor guard). */
static void pn_radar_world(void) {
    random_bytes(pn_world_start + PN_R, 0x530);
    pn_w32(PN_R, pn_at(PN_RVT));
    pn_w32(PN_RVT, (uint32_t)(uintptr_t)pn_o13);
    pn_w32(PN_R + 8u, pn_at(PN_OBJ));
    static const uint32_t ranges[] = {1000u, 6000u, 50000u, 0x222e0u, 0x222e1u, 200000u};
    pn_w32(PN_OBJ + 0xcu, pn_rand(2u) ? ranges[pn_rand(6u)] : 1u + pn_rand(300000u));
    pn_w32(PN_R + 0x28u, pn_rand(4u) ? 1u + pn_rand(8u) : 0u - (1u + pn_rand(4u)));
    pn_w32(PN_R + 0x10u, random32());
    pn_w32(PN_R + 0x14u, pn_at(PN_BMP));
    pn_w16(PN_BMP + 4u, pn_rand(4u) ? 1u + pn_rand(64u) : random32());
    pn_w16(PN_BMP + 6u, pn_rand(4u) ? 1u + pn_rand(64u) : random32());
    for (uint32_t o = 0x18u; o < 0x28u; o += 4u) pn_w32(PN_R + o, pn_signed(400u));
    for (uint32_t o = 0x58u; o < 0x68u; o += 4u) pn_w32(PN_R + o, pn_signed(400u));
    pn_w32(PN_R + 0x30u, pn_rand(2u) ? ranges[pn_rand(6u)] : pn_signed(300000u));
    pn_w32(PN_R + 0x34u, pn_rand(4u) ? pn_rand(2u) : random32());
    uint32_t n = pn_rand(8u) ? pn_rand(7u) : 0u - pn_rand(3u);
    pn_w32(PN_R + 0x518u, n);
    for (uint32_t i = 0; i < 8u; ++i) {
        pn_w32(PN_R + 0x68u + 0xcu * i, pn_signed(0x8000u));
        pn_w32(PN_R + 0x6cu + 0xcu * i, pn_signed(0x8000u));
        pn_w32(PN_R + 0x70u + 0xcu * i, random32());
    }
    pn_w32(PN_TPT, pn_signed(0x8000u));
    pn_w32(PN_TPT + 4u, pn_signed(0x8000u));
    switch (pn_rand(3u)) {
    case 0: pn_w32(PN_R + 0x51cu, 0); break;
    case 1: pn_w32(PN_R + 0x51cu, pn_at(PN_R + 0x68u + 0xcu * pn_rand(8u))); break;
    default: pn_w32(PN_R + 0x51cu, pn_at(PN_TPT)); break;
    }
    pn_w32(PN_R + 0x520u, pn_rand(3u) ? pn_rand(10000u) : random32());
    pn_w32(PN_PT, pn_signed(400u));
    pn_w32(PN_PT + 4u, pn_signed(400u));
    pn_w32(PN_H + 0x203u, pn_at(PN_P));
    random_bytes(pn_world_start + PN_P, 0x40);
    pn_w32(PN_P, pn_at(PN_PVT));
    pn_w32(PN_PVT + 0x3cu, (uint32_t)(uintptr_t)pn_o12);
    pn_w32(PN_H + 0x1edu, pn_at(PN_MFD));
    random_bytes(pn_world_start + PN_MFD, 0x100);
    pn_w32(PN_MFD + 0xc1u, pn_rand(4u));
    for (uint32_t i = 0; i < 4u; ++i) pn_w32(PN_MFD + 0xcdu + 4u * i, pn_rand(6u) ? random32() : pn_at(PN_R));
    random_bytes(pn_world_start + PN_GC, 0x240);
    pn_w32(PN_T + 8u, pn_at(PN_TBL));
    random_bytes(pn_world_start + PN_TBL, 0x50);
    static const char *const words[] = {"RNG", "SCALE ", "", "TGT-RANGE"};
    strcpy((char *)pn_world_start + PN_STR, words[pn_rand(4u)]);
    strcpy((char *)pn_world_start + PN_STR + 0x20u, words[pn_rand(4u)]);
    pn_set32(0x006c5f70u, pn_at(PN_GC));
    pn_set32(0x0049cbccu, pn_at(PN_T));
    pn_set32(0x004d1630u, pn_at(PN_STR));
    pn_set32(0x004d1634u, pn_at(PN_STR + 0x20u));
    pn_set8(0x004d2537u, pn_rand(3u) ? 3u : pn_rand(6u));
    pn_set8(0x004d256cu, pn_rand(3u) ? 0u : 1u + pn_rand(4u));
    pn_set16(0x004d3b92u, random32());
    pn_set16(0x004d3b8eu, random32());
    pn_set16(0x004d3ba2u, random32());
    pn_set16(0x004d253au, pn_shift_word());
    pn_set16(0x004d253cu, pn_shift_word());
    pn_heading = random32();
    for (unsigned i = 0; i < 2u; ++i)
        for (unsigned j = 0; j < 2u; ++j) { pn_sincos[i][j] = pn_signed(0x10000u); pn_rot[i][j] = pn_signed(200u); }
    pn_mask[pn_mask_count++] = PN_GC + 0x228u;
    pn_mask[pn_mask_count++] = PN_GC + 0x22cu;
}
static uint32_t pn_preset_value(uint32_t va) {
    uint32_t v = 0;
    for (unsigned i = 0; i < pn_preset_count; ++i) if (pn_presets[i].va == va) v = pn_presets[i].value;
    return v;
}
/* No blips, no target and no ring when a divisor is zero (the original's
 * #DE). */
static void pn_no_divisions(void) {
    pn_w32(PN_R + 0x518u, 0);
    pn_w32(PN_R + 0x51cu, 0);
    pn_w32(PN_R + 0x34u, 0);
}
static int pn_radar_draw_case(unsigned k) {
    pn_common();
    pn_radar_world();
    int32_t range = (int32_t)get32(pn_world_start + PN_OBJ + 0xcu), div = (int32_t)get32(pn_world_start + PN_R + 0x28u);
    int32_t scale = (range / div) >> (pn_preset_value(0x004d253au) & 31u);
    if (!scale) pn_no_divisions();
    pn_op_add(0x0043eef4u, 0u, 1, pn_at(PN_R), 0, 0);
    int bad = pn_run("radar draw", k);
    if (!bad) {
        int32_t n = (int32_t)get32(pn_world_start + PN_R + 0x518u);
        int dots = (BYTE)pn_preset_value(0x004d253au) != 0;
        int scaled = pn_preset_value(0x004d2537u) == 3u && pn_preset_value(0x004d256cu) == 0;
        ++pn_cov_radar[0];
        if (n > 0) ++pn_cov_radar[dots ? 1 : 2];
        if (get32(pn_world_start + PN_R + 0x51cu)) ++pn_cov_radar[3];
        if (get32(pn_world_start + PN_R + 0x34u) == 1u && range > 0x222e0) ++pn_cov_radar[4];
        ++pn_cov_radar[scaled ? 5 : 6];
    }
    return bad;
}
static int pn_hud_radar_case(unsigned k) {
    pn_common();
    pn_radar_world();
    int32_t half = (int32_t)(0x2eu << (pn_preset_value(0x004d253cu) & 31u)) >> 1;
    int32_t scale = (int32_t)get32(pn_world_start + PN_R + 0x30u) / half;
    if (!scale) pn_no_divisions();
    pn_op_add(0x0043f2d8u, 0u, 2, pn_at(PN_R), pn_at(PN_PT), 0);
    int bad = pn_run("HUD radar", k);
    if (!bad) {
        uint32_t m = PN_MFD, shown = get32(pn_world_start + m + 0xcdu + 4u * get32(pn_world_start + m + 0xc1u));
        int32_t n = (int32_t)get32(pn_world_start + PN_R + 0x518u);
        int dots = (BYTE)pn_preset_value(0x004d253au) != 0;
        if (shown == pn_at(PN_R)) ++pn_cov_radar[7];
        else {
            ++pn_cov_radar[8];
            if (n > 0) ++pn_cov_radar[dots ? 9 : 10];
            if (get32(pn_world_start + PN_R + 0x51cu)) ++pn_cov_radar[11];
        }
    }
    return bad;
}
static int pn_radar_update_case(unsigned k) {
    pn_common();
    pn_radar_world();
    pn_op_add(0x0043eedcu, 3u, 1, pn_at(PN_R), 0, 0);
    int bad = pn_run("radar update", k);
    if (!bad) ++pn_cov_radar[12];
    return bad;
}

static int panel_cases(void) {
    current_function = "panel";
    es2_sim_identity_memory(&ir_memory);
    es2_sim_rtl_memory(&ir_memory, &su_rtl);
    su_data_pristine = malloc(SU_DATA_SIZE);
    su_snap[0] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    su_snap[1] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    ir_region = ww_alloc(IR_REGION_SIZE);
    lc_stack = VirtualAlloc(NULL, 0x10000u, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    lc_bits = ww_alloc(LC_BITS_SIZE);
    pn_world = VirtualAlloc(NULL, PN_WORLD_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    pn_world_start = malloc(2u * PN_WORLD_SIZE);
    if (!su_data_pristine || !su_snap[0] || !su_snap[1] || !ir_region || !lc_stack || !lc_bits || !pn_world || !pn_world_start)
        return fail("panel: memory");
    if (get32(mapped + (SU_COUNTER - 0x400000u))) return fail("panel: the counter word is not free");
    su_pristine_take();
    static const struct { const char *dll, *name; void *stub; } stubs[] = {
        {"cw3220.dll", "_calloc", (void *)ir_calloc}, {"cw3220.dll", "_free", (void *)ir_free},
        {"cw3220.dll", "_memset", (void *)ir_memset}, {"cw3220.dll", "_memmove", (void *)ir_memmove},
        {"cw3220.dll", "_itoa", (void *)gd_itoa}, {"cw3220.dll", "_strlen", (void *)su_strlen},   /* gadgets_cases.h, startup_cases.h */
    };
    for (unsigned i = 0; i < sizeof(stubs) / sizeof(stubs[0]); ++i)
        if (!ir_patch_import(stubs[i].dll, stubs[i].name, stubs[i].stub)) {
            fprintf(stderr, "import %s!%s\n", stubs[i].dll, stubs[i].name);
            return fail("panel: import slot not found");
        }
    if (pn_dispatch_setup()) return 1;
    for (unsigned i = 0; i < PN_STUBS; ++i) if (pn_patch(pn_stubs[i].va, pn_stubs[i].stub, pn_saved[i], 1)) return 1;
    int bad = 0;
    static const struct { const char *name; int (*run)(unsigned); unsigned cases; } parts[] = {
        {"zones", pn_zones_case, 600},
        {"radar draw", pn_radar_draw_case, 1500},
        {"HUD radar", pn_hud_radar_case, 1500},
        {"radar update", pn_radar_update_case, 200},
    };
    for (unsigned p = 0; !bad && p < sizeof(parts) / sizeof(parts[0]); ++p) {
        for (unsigned k = 0; !bad && k < parts[p].cases; ++k) {
            current_case = k; current_case_valid = 1;
            bad = parts[p].run(k);
        }
        if (!bad) printf("PASS: panel %s: %u cases identical\n", parts[p].name, parts[p].cases);
    }
    for (unsigned i = 0; i < PN_STUBS; ++i) if (pn_patch(pn_stubs[i].va, pn_stubs[i].stub, pn_saved[i], 0)) bad = 1;
    if (!bad)
        printf("PANEL coverage: zones allocated/with NULL %u/%u (NULL allocations %u); calls: new %u; headless %u\n", pn_cov_zones[0],
               pn_cov_zones[1], pn_new_nulls, pn_kinds[1], pn_hud.headless);
    if (!bad)
        printf("PANEL coverage: radar draw %u (blips dots/points %u/%u, target %u, ring %u, scaled/not %u/%u); HUD radar shown/drawn %u/%u "
               "(blips dots/points %u/%u, target %u); update %u; calls: fill %u ellipse %u point %u line %u rotated %u blit %u label %u\n",
               pn_cov_radar[0], pn_cov_radar[1], pn_cov_radar[2], pn_cov_radar[3], pn_cov_radar[4], pn_cov_radar[5], pn_cov_radar[6],
               pn_cov_radar[7], pn_cov_radar[8], pn_cov_radar[9], pn_cov_radar[10], pn_cov_radar[11], pn_cov_radar[12], pn_kinds[2],
               pn_kinds[3], pn_kinds[4], pn_kinds[5], pn_kinds[6], pn_kinds[7], pn_kinds[8]);
    if (!bad && (!pn_cov_zones[0] || !pn_cov_zones[1] || !pn_new_nulls)) bad = fail("panel: a path was not taken");
    for (unsigned i = 0; !bad && i < 13u; ++i) if (!pn_cov_radar[i]) bad = fail("panel: a radar path was not taken");
    current_case_valid = 0;
    put32(mapped + 0x2cbee4u, 0);
    memcpy(mapped + SU_DATA_RVA, su_data_pristine, SU_DATA_SIZE);
    return bad;
}
