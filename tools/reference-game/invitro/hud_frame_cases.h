/* The HUD's slot 2 draw 0x43d704, the gadgets' slot 1 thunks (0x43e60f,
 * 0x43e61d, 0x43e62b, 0x43e643, 0x43e651, 0x43e69f, 0x43e6ad), the empty slot
 * 0x43b360 and the altitude ladder 0x43dd98, original machine code against
 * sim/src/hud_frame.c (target :differential_hud_frame_test). Builds on
 * startup_cases.h's DATA snapshots (su_take, su_compare), loop_cases.h's loop
 * set-up (lc_reset) and audio_cases.h's register-capturing machine call
 * (au_call), like gadgets_cases.h. Identifiers: hf_/HF_.
 *
 * Every case builds a synthetic HUD object H with its gadget list, the
 * display record, the panel [0x49b088] (target, speed source, throttle
 * gauge), the player [0x4d24e6] with its chassis, the terrain [0x4a0be0],
 * the current context [0x6c5f70] and the HUD bitmap table [0x49be78]+8 in
 * an identity-mapped buffer, presets the DATA words, then runs one call on
 * both sides: side 0 the original's code, side 1 the sim's rows through a
 * dispatch of the sim's real services.
 *
 * Recorders on both sides (side 0: a jump patched over the entry; side 1:
 * a row at the same VA), each returning a scripted EAX and EDX (EDX:EAX of
 * a 64-bit cdecl result): the display record 0x429820, the damage alert
 * 0x4342e0, the blink 0x438bc0, panel_visible 0x431410, the canvas
 * push/pop 0x4311e0/0x431210, the pick calls 0x4282c8/0x4282c0/0x4332dc,
 * the clock 0x467828, the blit 0x4816a8, the label 0x438920, the mission
 * time 0x43dcd4, the speed text 0x43dca0, the radar 0x43e114, the line
 * 0x483a04, the fill 0x486704, terrain_height 0x46e180, and (in the draw
 * part only) the ladder 0x43dd98; the gadgets' slots 0, 1 and 3 are host
 * recorders (slot 1 sometimes the real thunk VA, so the draw also runs the
 * thunks). Stack addresses are never hashed, only the bytes behind them;
 * the line, fill and blit recorders may overwrite the point or rectangle
 * they get (the same values on both sides), so every read-back of the
 * ladder's frame is checked. Compared: the recorder hash and count, DATA,
 * the world (the context's +0x228/+0x22c masked: stack addresses), EAX and
 * EDX always, ECX where the function defines it. */
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

#define HF_WORLD_SIZE 0x1000u
#define HF_H 0x0000u        /* the HUD, 0x140 */
#define HF_DISP 0x0140u     /* the display record (+0x14 the mode) */
#define HF_FLAG 0x0180u     /* [0x4cf99c] (+0x1c) */
#define HF_PANEL 0x0200u    /* [0x49b088], 0x280 */
#define HF_TGT 0x0480u      /* the target object (+0x10, +0x34) */
#define HF_SHAPE 0x04c0u    /* its shape (+4 the record) */
#define HF_REC 0x04d0u      /* the record (+8 a word) */
#define HF_SPD 0x04e0u      /* [[0x49b088]+0x203] (+0x10 the speed word) */
#define HF_THR 0x0500u      /* [[0x49b088]+0x1e1] (+0xb3) */
#define HF_GAD 0x0600u      /* 8 gadgets of 0x20 (+0 the vtable) */
#define HF_GVT 0x0700u      /* 8 vtables of 0x10 (slots 0..3) */
#define HF_TBL 0x0780u      /* [0x49be78] (+8 the bitmap table) */
#define HF_BMPS 0x0790u     /* the table: +0x38..+0x48 used */
#define HF_BMP 0x0800u      /* 8 bitmap headers of 0x10 */
#define HF_LABEL 0x0880u    /* [H+0x103] */
#define HF_P 0x0900u        /* the player [0x4d24e6], 0x200 */
#define HF_CHASSIS 0x0b00u  /* [P+0x1f2] (+0x50) */
#define HF_TERR 0x0b80u     /* [0x4a0be0] (+0x110, +0x114) */
#define HF_GC 0x0c00u       /* [0x6c5f70], 0x240 */
static BYTE *hf_world, *hf_world_start;
static es2_sim_entry hf_entries[4096];
static es2_sim_dispatch hf_dispatch;
static es2_sim_load hf_load;
static es2_sim_view hf_view;
static es2_sim_input hf_input;
static es2_sim_hud hf_hud;
static int hf_bad;
static void hf_refuse(const es2_sim_refusal *r, void *user) {
    (void)user;
    fprintf(stderr, "FAIL: own HUD frame: refused call to 0x%08lx (%s): %s\n", (unsigned long)r->va, r->name ? r->name : "?",
            r->reason ? r->reason : "");
    hf_bad = 1;
}

/* ---- the recorders */
#define HF_KINDS 23u
static uint64_t hf_hash[2];
static unsigned hf_count[2], hf_side_now, hf_calls[2][HF_KINDS], hf_kinds[HF_KINDS];
static char hf_text[2][512];
static uint32_t hf_eax[HF_KINDS][4], hf_edx[HF_KINDS][4];
static int hf_poke_on[HF_KINDS][4];
static uint32_t hf_poke[HF_KINDS][4][4];
static int hf_deep, hf_filled[2];
static void hf_mix(uint32_t v) {
    for (unsigned i = 0; i < 4u; ++i) { hf_hash[hf_side_now] ^= (BYTE)(v >> (8u * i)); hf_hash[hf_side_now] *= 0x100000001b3ull; }
}
static void hf_bytes(uint32_t va, uint32_t n) { for (uint32_t i = 0; i < n; ++i) hf_mix(*(BYTE *)(uintptr_t)(va + i)); }
static void hf_note(unsigned kind, const uint32_t *a, unsigned n) {
    unsigned side = hf_side_now;
    hf_mix(0xabc00000u + kind);
    for (unsigned i = 0; i < n; ++i) hf_mix(a[i]);
    size_t used = strlen(hf_text[side]);
    if (hf_count[side] < 14u && used + 40u < sizeof(hf_text[side]))
        snprintf(hf_text[side] + used, sizeof(hf_text[side]) - used, " %u(%lx,%lx)", kind, (unsigned long)(n ? a[0] : 0),
                 (unsigned long)(n > 1u ? a[1] : 0));
    ++hf_count[side];
    if (side) ++hf_kinds[kind];
}
/* The context's clip and origin fields and (once defined) the colour records. */
static void hf_gc_state(int records) {
    uint32_t gc = get32(mapped + (0x006c5f70u - 0x400000u));
    hf_bytes(gc + 0x208u, 0x20u);
    if (!records) return;
    hf_bytes(get32((BYTE *)(uintptr_t)(gc + 0x228u)), 8u);
    hf_bytes(get32((BYTE *)(uintptr_t)(gc + 0x22cu)), records > 1 ? 8u : 4u);
}
static void hf_poke_at(unsigned kind, unsigned k, uint32_t va, unsigned words) {
    if (!hf_poke_on[kind][k & 3u]) return;
    for (unsigned i = 0; i < words; ++i) put32((BYTE *)(uintptr_t)(va + 4u * i), hf_poke[kind][k & 3u][i]);
}
static uint64_t hf_record(unsigned kind, const uint32_t *a) {
    unsigned side = hf_side_now, k = hf_calls[side][kind]++;
    switch (kind) {
    case 11: {   /* 0x4816a8(bitmap, point, flags) */
        uint32_t v[2] = {a[0], a[2]};
        hf_note(11, v, 2);
        hf_bytes(a[1], 8u);
        if (hf_deep) { hf_gc_state(hf_filled[side] ? 2 : 0); hf_poke_at(11, k, a[1], 2); }
        break;
    }
    case 17:   /* 0x483a04(p0, p1) */
        hf_note(17, NULL, 0);
        hf_bytes(a[0], 8u);
        hf_bytes(a[1], 8u);
        hf_gc_state(1);
        hf_poke_at(17, k, a[0], 2);
        break;
    case 18:   /* 0x486704(rect) */
        hf_note(18, NULL, 0);
        hf_bytes(a[0], 16u);
        hf_gc_state(2);
        hf_filled[side] = 1;
        hf_poke_at(18, k, a[0], 4);
        break;
    case 19:   /* 0x46e180(terrain, point) */
        hf_note(19, a, 1);
        hf_bytes(a[1], 8u);
        break;
    case 1: case 5: case 6: case 7: case 8: case 10: hf_note(kind, NULL, 0); break;   /* no arguments */
    case 9: case 12: case 20: hf_note(kind, a, 2); break;
    default: hf_note(kind, a, 1); break;
    }
    return (uint64_t)hf_edx[kind][k & 3u] << 32 | hf_eax[kind][k & 3u];
}
#define HF_HOST0(k) static uint64_t __cdecl hf_o##k(void) { const uint32_t s[1] = {0}; return hf_record(k, s); }
#define HF_HOST1(k) static uint64_t __cdecl hf_o##k(uint32_t a) { const uint32_t s[1] = {a}; return hf_record(k, s); }
#define HF_HOST2(k) static uint64_t __cdecl hf_o##k(uint32_t a, uint32_t b) { const uint32_t s[2] = {a, b}; return hf_record(k, s); }
#define HF_HOST3(k) static uint64_t __cdecl hf_o##k(uint32_t a, uint32_t b, uint32_t c) { const uint32_t s[3] = {a, b, c}; return hf_record(k, s); }
HF_HOST0(1)
HF_HOST1(2)
HF_HOST1(3)
HF_HOST1(4)
HF_HOST0(5)
HF_HOST0(6)
HF_HOST0(7)
HF_HOST0(8)
HF_HOST2(9)
HF_HOST0(10)
HF_HOST3(11)
HF_HOST2(12)
HF_HOST1(13)
HF_HOST1(14)
HF_HOST1(15)
HF_HOST1(16)
HF_HOST2(17)
HF_HOST1(18)
HF_HOST2(19)
HF_HOST2(20)
HF_HOST1(21)
HF_HOST1(22)
static void hf_s_stub(es2_sim_service_call *sc, void *user) {
    uint64_t v = hf_record((unsigned)(uintptr_t)user, sc->slots);
    sc->eax = (uint32_t)v;
    sc->edx = (uint32_t)(v >> 32);
}
static const struct { uint32_t va; void *stub; unsigned kind, slots; } hf_stubs[] = {
    {0x00429820u, (void *)hf_o1, 1, 0}, {0x004342e0u, (void *)hf_o2, 2, 1}, {0x00438bc0u, (void *)hf_o3, 3, 1},
    {0x00431410u, (void *)hf_o4, 4, 1}, {0x004311e0u, (void *)hf_o5, 5, 0}, {0x00431210u, (void *)hf_o6, 6, 0},
    {0x004282c8u, (void *)hf_o7, 7, 0}, {0x004282c0u, (void *)hf_o8, 8, 0}, {0x004332dcu, (void *)hf_o9, 9, 2},
    {0x00467828u, (void *)hf_o10, 10, 0}, {0x004816a8u, (void *)hf_o11, 11, 3}, {0x00438920u, (void *)hf_o12, 12, 2},
    {0x0043dcd4u, (void *)hf_o13, 13, 1}, {0x0043dca0u, (void *)hf_o14, 14, 1}, {0x0043e114u, (void *)hf_o15, 15, 1},
    {0x00483a04u, (void *)hf_o17, 17, 2}, {0x00486704u, (void *)hf_o18, 18, 1}, {0x0046e180u, (void *)hf_o19, 19, 2},
};
#define HF_STUBS (sizeof(hf_stubs) / sizeof(hf_stubs[0]))
static BYTE hf_saved[HF_STUBS][5], hf_ladder_saved[5];
static int hf_patch(uint32_t va, void *target, BYTE saved[5], int install) {
    BYTE *site = mapped + (va - 0x400000u);
    DWORD previous;
    if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &previous)) return fail("hud frame: stub protection");
    if (install) { memcpy(saved, site, 5); site[0] = 0xe9; put32(site + 1, (uint32_t)(uintptr_t)target - (uint32_t)(uintptr_t)(site + 5)); }
    else memcpy(site, saved, 5);
    if (!VirtualProtect(site, 5, previous, &previous)) return fail("hud frame: stub restore protection");
    FlushInstructionCache(GetCurrentProcess(), site, 5);
    return 0;
}
static int hf_put(uint32_t va, const char *name, es2_sim_service_fn fn, void *user, unsigned slots) {
    for (unsigned i = 0; i < hf_dispatch.count; ++i) {
        es2_sim_entry *e = &hf_dispatch.entries[i];
        if (e->va != va) continue;
        e->kind = ES2_SIM_SERVICE; e->name = name; e->service = fn; e->service_user = user; e->pop_bytes = 0; e->stack_slots = (uint8_t)slots;
        return 0;
    }
    return es2_sim_dispatch_add_service(&hf_dispatch, va, name, fn, user, 0, (uint8_t)slots);
}
/* The ladder row: hud_load.h's (delegating to hud_frame.c) or the recorder. */
static es2_sim_entry hf_ladder_row;
static es2_sim_entry *hf_ladder_entry(void) {
    for (unsigned i = 0; i < hf_dispatch.count; ++i) if (hf_dispatch.entries[i].va == 0x0043dd98u) return &hf_dispatch.entries[i];
    return NULL;
}
static int hf_dispatch_setup(void) {
    es2_sim_dispatch_init(&hf_dispatch, hf_entries, 4096, hf_refuse, NULL);
    static es2_sim_services s;
    s.memory = &ir_memory;
    s.heap = &ir_heap;
    s.vt = &lc_vt;
    s.fs0 = NULL;
    if (es2_sim_register_services(&hf_dispatch, &s)) return fail("hud frame: dispatch");
    for (unsigned i = 0; i < HF_STUBS; ++i)
        if (hf_put(hf_stubs[i].va, "hud frame recorder", hf_s_stub, (void *)(uintptr_t)hf_stubs[i].kind, hf_stubs[i].slots))
            return fail("hud frame: dispatch");
    static void *const hosts[] = {(void *)hf_o20, (void *)hf_o21, (void *)hf_o22};
    for (unsigned i = 0; i < 3u; ++i)
        if (hf_put((uint32_t)(uintptr_t)hosts[i], "hud frame slot", hf_s_stub, (void *)(uintptr_t)(20u + i), i ? 1u : 2u))
            return fail("hud frame: dispatch");
    es2_sim_entry *e = hf_ladder_entry();
    if (!e || e->kind != ES2_SIM_SERVICE) return fail("hud frame: no ladder row");
    hf_ladder_row = *e;
    const char *error = NULL;
    uint32_t bad = 0;
    if (es2_sim_dispatch_seal(&hf_dispatch, &error, &bad)) return fail("hud frame: dispatch seal");
    return 0;
}
static void hf_ladder_recorded(int on) {
    es2_sim_entry *e = hf_ladder_entry();
    if (on) { e->service = hf_s_stub; e->service_user = (void *)(uintptr_t)16u; e->name = "hud frame recorder"; }
    else { e->service = hf_ladder_row.service; e->service_user = hf_ladder_row.service_user; e->name = hf_ladder_row.name; }
}

/* ---- the case script */
typedef struct hf_preset { uint32_t va, value; unsigned width; } hf_preset;
static hf_preset hf_presets[64];
static unsigned hf_preset_count;
static void hf_set32(uint32_t va, uint32_t v) { hf_presets[hf_preset_count++] = (hf_preset){va, v, 4}; }
static void hf_set16(uint32_t va, uint32_t v) { hf_presets[hf_preset_count++] = (hf_preset){va, v, 2}; }
static void hf_set8(uint32_t va, uint32_t v) { hf_presets[hf_preset_count++] = (hf_preset){va, v, 1}; }
static uint32_t hf_rand(uint32_t n) { return random32() % n; }
static uint32_t hf_signed(uint32_t range) { return (uint32_t)((int32_t)hf_rand(2u * range + 1u) - (int32_t)range); }
static uint32_t hf_at(uint32_t offset) { return (uint32_t)(uintptr_t)hf_world + offset; }
static void hf_w32(uint32_t offset, uint32_t v) { put32(hf_world_start + offset, v); }
static void hf_w16(uint32_t offset, uint32_t v) { put16(hf_world_start + offset, (uint16_t)v); }
static void hf_w8(uint32_t offset, uint32_t v) { hf_world_start[offset] = (BYTE)v; }
typedef struct hf_op { uint32_t va, a[2], in[3]; unsigned n, ecx; } hf_op;
static hf_op hf_op_now;
static uint32_t hf_res[2][3];
static void hf_common(void) {
    memset(hf_world_start, 0, HF_WORLD_SIZE);
    random_bytes(hf_world_start, HF_WORLD_SIZE);
    hf_preset_count = 0;
    hf_deep = 0;
    for (unsigned k = 0; k < HF_KINDS; ++k)
        for (unsigned i = 0; i < 4u; ++i) {
            hf_eax[k][i] = random32();
            hf_edx[k][i] = random32();
            hf_poke_on[k][i] = 0;
            for (unsigned j = 0; j < 4u; ++j) hf_poke[k][i][j] = hf_signed(0x200u);
        }
    hf_w32(HF_PANEL + 0x1e1u, hf_at(HF_THR));
    hf_w32(HF_PANEL + 0x203u, hf_at(HF_SPD));
    hf_w32(HF_TGT + 0x34u, hf_at(HF_SHAPE));
    hf_w32(HF_SHAPE + 4u, hf_at(HF_REC));
    hf_w32(HF_TBL + 8u, hf_at(HF_BMPS));
    for (unsigned i = 0; i < 5u; ++i) {
        hf_w32(HF_BMPS + 0x38u + 4u * i, hf_at(HF_BMP + 0x10u * i));
        hf_w16(HF_BMP + 0x10u * i + 4u, hf_rand(8u) ? 1u + hf_rand(40u) : random32());
        hf_w16(HF_BMP + 0x10u * i + 6u, hf_rand(8u) ? 1u + hf_rand(40u) : random32());
    }
    hf_w32(HF_H + 0x103u, hf_at(HF_LABEL));
    hf_w32(HF_P + 0x1f2u, hf_at(HF_CHASSIS));
    hf_set32(0x0049b088u, hf_at(HF_PANEL));
    hf_set32(0x004cf99cu, hf_at(HF_FLAG));
    hf_set32(0x0049be78u, hf_at(HF_TBL));
    hf_set32(0x004d24e6u, hf_at(HF_P));
    hf_set32(0x004a0be0u, hf_at(HF_TERR));
    hf_set32(0x006c5f70u, hf_at(HF_GC));
    hf_set32(0x006c5ed0u, random32());
    static const uint32_t labels[] = {0x004d1650u, 0x004d1658u, 0x004d165cu, 0x004d17ccu, 0x004d17d0u, 0x004d17d4u, 0x004d17d8u};
    for (unsigned i = 0; i < sizeof(labels) / sizeof(labels[0]); ++i) hf_set32(labels[i], random32());
}
static void hf_side(unsigned side) {
    lc_reset(side, 47, 0x01000000u, 1);
    lc_loop.dispatch = &hf_dispatch;
    es2_sim_load_headless(&hf_load, &lc_loop);
    es2_sim_load_bind(&hf_load);
    memset(&hf_view, 0, sizeof(hf_view));
    hf_view.loop = &lc_loop;
    es2_sim_view_bind(&hf_view);
    es2_sim_input_headless(&hf_input, &lc_loop, &lc_journal[side], 640, 480);
    es2_sim_input_bind(&hf_input);
    es2_sim_hud_init(&hf_hud, &lc_loop);
    es2_sim_hud_bind(&hf_hud);
    memcpy(hf_world, hf_world_start, HF_WORLD_SIZE);
    for (unsigned i = 0; i < hf_preset_count; ++i) {
        BYTE *p = mapped + (hf_presets[i].va - 0x400000u);
        if (hf_presets[i].width == 4) put32(p, hf_presets[i].value);
        else if (hf_presets[i].width == 2) put16(p, (uint16_t)hf_presets[i].value);
        else *p = (BYTE)hf_presets[i].value;
    }
    hf_side_now = side;
    hf_hash[side] = 0xcbf29ce484222325ull;
    hf_count[side] = 0;
    hf_text[side][0] = 0;
    hf_filled[side] = 0;
    memset(hf_calls[side], 0, sizeof(hf_calls[side]));
}
static int hf_run(const char *what, unsigned k) {
    hf_op *o = &hf_op_now;
    for (unsigned side = 0; side < 2u; ++side) {
        hf_side(side);
        uint32_t out[3] = {0, 0, 0};
        static char where[200];
        snprintf(where, sizeof(where), "hud frame (%s side %u: 0x%08lx(0x%lx, 0x%lx))", what, side, (unsigned long)o->va,
                 (unsigned long)o->a[0], (unsigned long)o->a[1]);
        current_function = where;
        if (!side) au_call((uint32_t)(uintptr_t)(mapped + (o->va - 0x400000u)), o->a, o->n, o->in, out);
        else {
            es2_sim_result r;
            memset(&r, 0, sizeof(r));
            if (es2_sim_call(&hf_dispatch, o->va, o->a, o->n, o->in, &r)) hf_bad = 1;
            out[0] = r.eax;
            out[1] = r.edx;
            out[2] = r.ecx;
        }
        memcpy(hf_res[side], out, sizeof(out));
        if (hf_bad) { fprintf(stderr, "FAIL: hud frame %s case %u (0x%08lx): a call failed\n", what, k, (unsigned long)o->va); return 1; }
        put32(hf_world + HF_GC + 0x228u, 0);   /* stack addresses on both sides (the ladder's colour records) */
        put32(hf_world + HF_GC + 0x22cu, 0);
        if (!side) memcpy(hf_world_start + HF_WORLD_SIZE, hf_world, HF_WORLD_SIZE);
        su_take(side);
    }
    if (hf_hash[0] != hf_hash[1] || hf_count[0] != hf_count[1]) {
        fprintf(stderr, "FAIL: hud frame %s case %u: recorded calls differ (%u / %u)\n  original:%s\n  own:     %s\n", what, k, hf_count[0],
                hf_count[1], hf_text[0], hf_text[1]);
        return 1;
    }
    if (su_compare(what, k)) return 1;
    if (memcmp(hf_world, hf_world_start + HF_WORLD_SIZE, HF_WORLD_SIZE)) {
        for (unsigned i = 0, shown = 0; i < HF_WORLD_SIZE && shown < 16u; ++i)
            if (hf_world[i] != hf_world_start[HF_WORLD_SIZE + i]) {
                fprintf(stderr, "  world +0x%04x original %02x own %02x\n", i, hf_world_start[HF_WORLD_SIZE + i], hf_world[i]);
                ++shown;
            }
        fprintf(stderr, "FAIL: hud frame %s case %u: the world differs (world at 0x%08lx)\n", what, k, (unsigned long)(uintptr_t)hf_world);
        return 1;
    }
    unsigned regs = o->ecx ? 3u : 2u;
    for (unsigned i = 0; i < regs; ++i)
        if (hf_res[0][i] != hf_res[1][i]) {
            static const char *const names[] = {"EAX", "EDX", "ECX"};
            fprintf(stderr, "FAIL: hud frame %s case %u (0x%08lx): %s original 0x%08lx own 0x%08lx\n", what, k, (unsigned long)o->va, names[i],
                    (unsigned long)hf_res[0][i], (unsigned long)hf_res[1][i]);
            return 1;
        }
    return 0;
}
static void hf_op_set(uint32_t va, unsigned n, uint32_t a0, uint32_t a1, unsigned ecx) {
    hf_op_now = (hf_op){va, {a0, a1}, {random32(), random32(), random32()}, n, ecx};
}

/* ---- the slot 2 draw */
static const uint32_t hf_thunks[7] = {0x0043e60fu, 0x0043e61du, 0x0043e62bu, 0x0043e643u, 0x0043e651u, 0x0043e69fu, 0x0043e6adu};
static unsigned hf_cov_mode[3], hf_cov_alert[2], hf_cov_blink_off, hf_cov_hidden, hf_cov_pick[4], hf_cov_target[2], hf_cov_ramp[5],
    hf_cov_loop[2], hf_cov_reverse[2], hf_cov_thunk_in_draw;
static int hf_draw_case(unsigned k) {
    hf_common();
    /* the display record and the 0x4cf99c flag */
    unsigned mode = hf_rand(3u);
    hf_eax[1][0] = hf_at(HF_DISP);
    hf_w32(HF_DISP + 0x14u, mode ? 4u : hf_rand(2u) ? 3u : random32());
    hf_w8(HF_FLAG + 0x1cu, mode == 1u ? 0u : 1u + hf_rand(255u));
    /* the damage alert: a level 0..4 in the low word */
    uint32_t level = hf_rand(3u) ? 0u : 1u + hf_rand(4u);
    hf_eax[2][0] = (random32() & 0xffff0000u) | level;
    hf_w8(HF_H + 0x76u, hf_rand(6u) == 0);
    hf_eax[4][0] = (random32() & 0xffffff00u) | (hf_rand(6u) ? 1u + hf_rand(255u) : 0u);
    hf_set8(0x0049be74u, hf_rand(3u) == 0);
    hf_eax[7][0] = (random32() & 0xffffff00u) | (hf_rand(2u) ? 1u + hf_rand(255u) : 0u);
    hf_eax[8][0] = hf_rand(2u) ? random32() | 1u : 0u;
    /* the target */
    hf_w32(HF_PANEL + 0x26cu, hf_rand(3u) ? hf_at(HF_TGT) : 0u);
    /* the gadgets: [H+0xd7 + 4i]; H+0xdb (i = 1) is also the speed tape */
    int32_t count = hf_rand(8u) ? (int32_t)hf_rand(9u) : -(int32_t)hf_rand(0x10000u) - 1;
    hf_w32(HF_H + 0xffu, (uint32_t)count);
    for (unsigned i = 0; i < 8u; ++i) {
        hf_w32(HF_H + 0xd7u + 4u * i, hf_at(HF_GAD + 0x20u * i));
        hf_w32(HF_GAD + 0x20u * i, hf_at(HF_GVT + 0x10u * i));
        hf_w32(HF_GVT + 0x10u * i, (uint32_t)(uintptr_t)hf_o21);
        hf_w32(HF_GVT + 0x10u * i + 4u, hf_rand(2u) ? (uint32_t)(uintptr_t)hf_o22 : hf_thunks[hf_rand(7u)]);
        hf_w32(HF_GVT + 0x10u * i + 0xcu, (uint32_t)(uintptr_t)hf_o20);
    }
    /* the speed and the power-on ramp */
    static const uint16_t speeds[] = {0u, 1u, 0x7fffu, 0x8000u, 0x8001u, 0xffffu, 300u, 0xfed4u};
    uint16_t si = hf_rand(3u) ? speeds[hf_rand(8u)] : (uint16_t)random32();
    hf_w16(HF_SPD + 0x10u, si);
    hf_w8(HF_H + 0x8cu, hf_rand(4u) != 0);
    hf_w8(HF_H + 0x8du, hf_rand(4u) == 0);
    uint32_t base = random32(), target = si > 0x8000u ? 0x10000u - si : si, d = target / 50u;
    switch (hf_rand(4u)) {
    case 0: d += 1u; break;
    case 1: d = d ? d - 1u : 0u; break;
    case 2: break;
    default: d = random32(); break;
    }
    hf_w32(HF_H + 0x90u, base);
    hf_eax[10][0] = base + d;
    hf_w8(HF_THR + 0xb3u, hf_rand(2u));
    hf_op_set(0x0043d704u, 1, hf_at(HF_H), 0, 0);
    unsigned thunks = hf_kinds[21];
    int bad = hf_run("draw", k);
    if (!bad) {
        /* the branch each case took, from its inputs */
        int early = (get32(hf_world_start + HF_DISP + 0x14u) == 4u) && !hf_world_start[HF_FLAG + 0x1cu];
        ++hf_cov_mode[early ? 0 : get32(hf_world_start + HF_DISP + 0x14u) == 4u ? 1 : 2];
        if (early) return 0;
        ++hf_cov_alert[level != 0];
        if (hf_world_start[HF_H + 0x76u]) { ++hf_cov_blink_off; return 0; }
        if (!(hf_eax[4][0] & 0xffu)) { ++hf_cov_hidden; return 0; }
        int pick = 0;
        for (unsigned i = 0; i < hf_preset_count; ++i) if (hf_presets[i].va == 0x0049be74u) pick = (int)hf_presets[i].value;
        ++hf_cov_pick[!pick ? 0 : !(hf_eax[7][0] & 0xffu) ? 1 : !hf_eax[8][0] ? 2 : 3];
        ++hf_cov_target[get32(hf_world_start + HF_PANEL + 0x26cu) != 0];
        if (hf_world_start[HF_H + 0x8du] || !hf_world_start[HF_H + 0x8cu]) ++hf_cov_ramp[0];
        else {
            uint32_t ramp = (hf_eax[10][0] - base) * 50u;
            if (si > 0x8000u) ++hf_cov_ramp[si < (uint16_t)(0u - ramp) ? 1 : 2];
            else ++hf_cov_ramp[si > (uint16_t)ramp ? 3 : 4];
        }
        ++hf_cov_loop[count > 0];
        ++hf_cov_reverse[hf_world_start[HF_THR + 0xb3u] != 0];
        if (hf_kinds[21] != thunks) ++hf_cov_thunk_in_draw;
    }
    return bad;
}

/* ---- the slot 1 thunks and the empty slot 0x43b360 */
static unsigned hf_cov_thunk[8];
static int hf_thunk_case(unsigned k) {
    hf_common();
    unsigned which = hf_rand(8u);
    hf_w32(HF_GAD, hf_at(HF_GVT));
    hf_w32(HF_GVT, (uint32_t)(uintptr_t)hf_o21);
    if (which < 7u) hf_op_set(hf_thunks[which], 1, hf_at(HF_GAD), 0, 1);
    else hf_op_set(0x0043b360u, 2, random32(), random32(), 1);
    int bad = hf_run(which < 7u ? "thunk" : "empty slot", k);
    if (!bad) ++hf_cov_thunk[which];
    return bad;
}

/* ---- the altitude ladder */
static unsigned hf_cov_flyer[2], hf_cov_ground[3], hf_cov_marker[3], hf_cov_pokes;
static int hf_ladder_case(unsigned k) {
    hf_common();
    hf_deep = 1;
    hf_w16(HF_CHASSIS + 0x50u, hf_rand(6u) ? 1u + hf_rand(0xffffu) : 0u);
    unsigned sx = hf_rand(4u) ? hf_rand(3u) : hf_rand(30u), sy = hf_rand(4u) ? hf_rand(3u) : hf_rand(30u);
    hf_set16(0x004d253au, sx | (hf_rand(4u) ? 0u : random32() & 0xff00u));
    hf_set16(0x004d253cu, sy | (hf_rand(4u) ? 0u : random32() & 0xff00u));
    int32_t floor_ = (int32_t)hf_signed(0x8000u), range;
    do range = hf_rand(4u) ? 1 + (int32_t)hf_rand(0x20000u) : (int32_t)random32(); while (range == 0 || range == -1);
    hf_w32(HF_TERR + 0x110u, (uint32_t)floor_);
    hf_w32(HF_TERR + 0x114u, (uint32_t)floor_ + (uint32_t)range - 5000u);
    uint32_t pick[2];
    for (unsigned i = 0; i < 2u; ++i) {
        switch (hf_rand(4u)) {
        case 0: pick[i] = (uint32_t)floor_ - 1u - hf_rand(0x100u); break;            /* below: clamped to the bottom */
        case 1: pick[i] = (uint32_t)floor_ + (uint32_t)range + hf_rand(0x100u); break; /* above: clamped to the top */
        case 2: pick[i] = (uint32_t)floor_ + hf_rand((uint32_t)(range > 0 ? range : 1)); break;
        default: pick[i] = random32(); break;
        }
    }
    hf_eax[19][0] = pick[0];
    hf_w32(HF_P + 0x2eu, pick[1]);
    hf_w32(HF_H + 0x113u, hf_signed(0x200u));
    hf_w32(HF_H + 0x117u, hf_signed(0x200u));
    unsigned pokes = 0;
    if (hf_rand(3u) == 0) {
        static const unsigned kinds[3] = {11, 17, 18};
        unsigned kind = kinds[hf_rand(3u)], i = hf_rand(4u);
        hf_poke_on[kind][i] = 1;
        pokes = 1;
    }
    hf_op_set(0x0043dd98u, 1, hf_at(HF_H), 0, 1);
    int bad = hf_run("ladder", k);
    if (!bad) {
        int flyer = get16(hf_world_start + HF_CHASSIS + 0x50u) != 0;
        ++hf_cov_flyer[flyer];
        if (flyer) {
            hf_cov_pokes += pokes;
            int32_t f0 = floor_, c0 = floor_ + range;
            for (unsigned i = 0; i < 2u; ++i) {
                unsigned *cov = i ? hf_cov_marker : hf_cov_ground;
                int32_t v = (int32_t)pick[i];
                if (range > 0) ++cov[v < f0 ? 0 : v > c0 ? 1 : 2];
            }
        }
    }
    return bad;
}

static int hud_frame_cases(void) {
    current_function = "hud frame";
    es2_sim_identity_memory(&ir_memory);
    es2_sim_rtl_memory(&ir_memory, &su_rtl);
    su_data_pristine = malloc(SU_DATA_SIZE);
    su_snap[0] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    su_snap[1] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    ir_region = ww_alloc(IR_REGION_SIZE);
    lc_stack = VirtualAlloc(NULL, 0x10000u, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    lc_bits = ww_alloc(LC_BITS_SIZE);
    hf_world = VirtualAlloc(NULL, HF_WORLD_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    hf_world_start = malloc(2u * HF_WORLD_SIZE);
    if (!su_data_pristine || !su_snap[0] || !su_snap[1] || !ir_region || !lc_stack || !lc_bits || !hf_world || !hf_world_start)
        return fail("hud frame: memory");
    if (get32(mapped + (SU_COUNTER - 0x400000u))) return fail("hud frame: the counter word is not free");
    su_pristine_take();
    if (hf_dispatch_setup()) return 1;
    for (unsigned i = 0; i < HF_STUBS; ++i) if (hf_patch(hf_stubs[i].va, hf_stubs[i].stub, hf_saved[i], 1)) return 1;
    int bad = 0;
    static const struct { const char *name; int (*run)(unsigned); unsigned cases; int ladder_recorded; } parts[] = {
        {"draw", hf_draw_case, 3000, 1}, {"thunks", hf_thunk_case, 800, 0}, {"ladder", hf_ladder_case, 3000, 0},
    };
    for (unsigned p = 0; !bad && p < sizeof(parts) / sizeof(parts[0]); ++p) {
        if (parts[p].ladder_recorded) { if (hf_patch(0x0043dd98u, (void *)hf_o16, hf_ladder_saved, 1)) return 1; }
        hf_ladder_recorded(parts[p].ladder_recorded);
        for (unsigned k = 0; !bad && k < parts[p].cases; ++k) {
            current_case = k; current_case_valid = 1;
            bad = parts[p].run(k);
        }
        if (parts[p].ladder_recorded && hf_patch(0x0043dd98u, (void *)hf_o16, hf_ladder_saved, 0)) bad = 1;
        hf_ladder_recorded(0);
        if (!bad) printf("PASS: hud frame %s: %u cases identical\n", parts[p].name, parts[p].cases);
    }
    for (unsigned i = 0; i < HF_STUBS; ++i) if (hf_patch(hf_stubs[i].va, hf_stubs[i].stub, hf_saved[i], 0)) bad = 1;
    if (!bad)
        printf("HUD FRAME coverage: mode early/4 on/other %u/%u/%u; alert 0/set %u/%u; blink off %u; hidden %u; pick off/idle no/none/"
               "select %u/%u/%u/%u; target 0/set %u/%u; ramp off %u, high limited/ended %u/%u, low limited/ended %u/%u; gadgets 0/some "
               "%u/%u; reverse off/on %u/%u; thunks in the draw %u; thunks %u/%u/%u/%u/%u/%u/%u, empty slot %u; ladder flyer no/yes %u/%u, "
               "ground low/high/in %u/%u/%u, marker low/high/in %u/%u/%u, pokes %u; calls: blit %u, label %u, line %u, fill %u, terrain %u, "
               "clock %u, slot 0 %u, slot 1 %u, slot 3 %u\n",
               hf_cov_mode[0], hf_cov_mode[1], hf_cov_mode[2], hf_cov_alert[0], hf_cov_alert[1], hf_cov_blink_off, hf_cov_hidden, hf_cov_pick[0],
               hf_cov_pick[1], hf_cov_pick[2], hf_cov_pick[3], hf_cov_target[0], hf_cov_target[1], hf_cov_ramp[0], hf_cov_ramp[1],
               hf_cov_ramp[2], hf_cov_ramp[3], hf_cov_ramp[4], hf_cov_loop[0], hf_cov_loop[1], hf_cov_reverse[0], hf_cov_reverse[1],
               hf_cov_thunk_in_draw, hf_cov_thunk[0], hf_cov_thunk[1], hf_cov_thunk[2], hf_cov_thunk[3], hf_cov_thunk[4], hf_cov_thunk[5],
               hf_cov_thunk[6], hf_cov_thunk[7], hf_cov_flyer[0], hf_cov_flyer[1], hf_cov_ground[0], hf_cov_ground[1], hf_cov_ground[2],
               hf_cov_marker[0], hf_cov_marker[1], hf_cov_marker[2], hf_cov_pokes, hf_kinds[11], hf_kinds[12], hf_kinds[17], hf_kinds[18],
               hf_kinds[19], hf_kinds[10], hf_kinds[21], hf_kinds[22], hf_kinds[20]);
    current_case_valid = 0;
    put32(mapped + 0x2cbee4u, 0);
    memcpy(mapped + SU_DATA_RVA, su_data_pristine, SU_DATA_SIZE);
    return bad;
}
