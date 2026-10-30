/* The weapon gauges' per-frame draws and the methods on their path, original
 * machine code against sim/src/hud_gauges.c (--gauges; target
 * :differential_gauges_test). Built like panel_cases.h: reuses
 * startup_cases.h's DATA and heap snapshots (su_take, su_compare),
 * loop_cases.h's loop set-up (lc_reset) and audio_cases.h's register-capturing
 * machine call (au_call). Identifiers: gg_/GG_.
 *
 * Every case builds a synthetic gauge G (fixture fields, not a constructed
 * gauge: the constructors are gadget_methods.h's) with its widgets, labels, an
 * animation word, the display D [0x4cf99c] and the panel [0x49b088] with
 * its port [+0x20b] in an identity-mapped world, then runs one call on both
 * sides: side 0 the original's code, side 1 the sim's rows through a
 * dispatch of the sim's real services. The callees outside weapon_gauges.h are
 * recorders on both sides (side 0: a jump patched over the entry; side 1: a
 * row at the same VA) that return scripted values; the vtable slots the
 * code calls are host recorders (rows at their host addresses on side 1),
 * except the pods' slot +0x1c, which is the real sweep 0x443444 or 0x441db0
 * in some cases. Compared: the recorder hash and count, DATA and the used
 * heap, the world, and EAX, ECX, EDX where the contract defines them. */
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

#define GG_WORLD_SIZE 0x1000u
#define GG_G 0x0000u       /* the gauge, 0x100 */
#define GG_GVT 0x0100u     /* its vtable, 0x20 */
#define GG_W18 0x0140u     /* the widgets [G+0x18], [G+0x1c], [G+0x20]: 0x80 each */
#define GG_W1C 0x01c0u
#define GG_W20 0x0240u
#define GG_WVT 0x02c0u     /* the widgets' vtable, 0x10 */
#define GG_L 0x0300u       /* the label [G+0xdb] / [G+0xca], 0x30 */
#define GG_ANIM 0x0340u    /* the animation word [G+0x6c] */
#define GG_D 0x0380u       /* the display [0x4cf99c], 0x40 */
#define GG_H 0x0400u       /* the panel [0x49b088], 0x220 */
#define GG_P 0x0640u       /* the port [H+0x20b], 0x10 */
#define GG_PVT 0x0660u     /* its vtable */
#define GG_RECT 0x0680u    /* the base's rectangle */
#define GG_BW 0x06c0u      /* the base's weapon W */
static BYTE *gg_world, *gg_world_start;
static es2_sim_entry gg_entries[4096];
static es2_sim_dispatch gg_dispatch;
static es2_sim_load gg_load;
static es2_sim_view gg_view;
static es2_sim_input gg_input;
static es2_sim_hud gg_hud;
static int gg_bad;
static void gg_refuse(const es2_sim_refusal *r, void *user) {
    (void)user;
    fprintf(stderr, "FAIL: own gauges: refused call to 0x%08lx (%s): %s\n", (unsigned long)r->va, r->name ? r->name : "?",
            r->reason ? r->reason : "");
    gg_bad = 1;
}

/* ---- the recorders */
#define GG_KINDS 20u
static uint64_t gg_hash[2];
static unsigned gg_count[2], gg_side_now, gg_calls[2][GG_KINDS];
static char gg_text[2][512];
static unsigned gg_kinds[GG_KINDS], gg_new_fail_mask, gg_new_nulls;
/* The case's script: AL of 0x431410 per call (bit k), the 0x4342e0
 * values, the byte 0x438bc0 leaves in G+0x76, the clock offset from
 * [G+0x90], the byte the widget frame (slot +0xc) leaves in G+0xb0 (when
 * gg_frame_b0 is set), the load result. */
static uint32_t gg_vis_mask, gg_alert[4], gg_clock_t, gg_load_value;
static int gg_blink_byte, gg_frame_b0;
static unsigned gg_lvis_last;   /* side 1: 0 no label test, 1 hidden, 2 visible */
static void gg_mix(uint32_t v) {
    for (unsigned i = 0; i < 4u; ++i) { gg_hash[gg_side_now] ^= (BYTE)(v >> (8u * i)); gg_hash[gg_side_now] *= 0x100000001b3ull; }
}
static void gg_bytes(uint32_t va, uint32_t n) { for (uint32_t i = 0; i < n; ++i) gg_mix(*(BYTE *)(uintptr_t)(va + i)); }
static void gg_note(unsigned kind, const uint32_t *a, unsigned n) {
    unsigned side = gg_side_now;
    gg_mix(0xabc00000u + kind);
    for (unsigned i = 0; i < n; ++i) gg_mix(a[i]);
    size_t used = strlen(gg_text[side]);
    if (gg_count[side] < 14u && used + 40u < sizeof(gg_text[side]))
        snprintf(gg_text[side] + used, sizeof(gg_text[side]) - used, " %u(%lx,%lx)", kind, (unsigned long)(n ? a[0] : 0),
                 (unsigned long)(n > 1u ? a[1] : 0));
    ++gg_count[side];
    if (side) ++gg_kinds[kind];
}
static uint32_t gg_at(uint32_t offset) { return (uint32_t)(uintptr_t)gg_world + offset; }
/* One recorded call (the same on both sides); returns its EAX. Stack
 * addresses are never hashed, only the bytes behind them. */
static uint32_t gg_record(unsigned kind, const uint32_t *a) {
    unsigned side = gg_side_now, k = gg_calls[side][kind]++;
    switch (kind) {
    case 1:   /* operator new(n): NULL for the case's failing calls, else the shared heap */
        gg_note(1, a, 1);
        if (k < 32u && (gg_new_fail_mask >> k) & 1u) {
            if (side) ++gg_new_nulls;
            return 0;
        }
        return (uint32_t)(uintptr_t)ir_calloc(1, a[0]);
    case 2:   /* operator new[](n) */
        gg_note(2, a, 1);
        return (uint32_t)(uintptr_t)ir_calloc(1, a[0] ? a[0] : 1u);
    case 3:   /* 0x431410(rect): AL scripted, the upper bytes noise */
        gg_note(3, a, 1);
        gg_bytes(a[0], 16u);
        if (side && a[0] == gg_at(GG_L + 4u)) gg_lvis_last = 1u + (k < 32u ? (gg_vis_mask >> k) & 1u : 0u);
        return 0xa5a5a500u ^ (k << 8) ^ (k < 32u ? (gg_vis_mask >> k) & 1u : 0u);
    case 4:   /* 0x4342e0(n) */
        gg_note(4, a, 1);
        return gg_alert[k & 3u];
    case 5:   /* 0x438bc0(G): leaves the scripted byte in G+0x76 */
        gg_note(5, a, 1);
        gg_bytes(a[0] + 0x74u, 4u);
        if (gg_blink_byte >= 0) *(BYTE *)(uintptr_t)(a[0] + 0x76u) = (BYTE)gg_blink_byte;
        return 0x0b0b0000u ^ k;
    case 6: {   /* 0x438920(L, text) */
        gg_note(6, a, 1);
        gg_bytes(a[0], 0x20u);
        uint32_t n = 0;
        while (n < 16u && *(BYTE *)(uintptr_t)(a[1] + n)) ++n;
        gg_mix(n);
        gg_bytes(a[1], n);
        return 0;
    }
    case 7:   /* 0x43122c(a, rect, b) */
        gg_note(7, a, 1);
        gg_mix(a[2]);
        gg_bytes(a[1], 16u);
        return 0x07000000u ^ k;
    case 8:   /* 0x435ac8(P, a, b) */
        gg_note(8, a, 3);
        return 0x08000000u ^ k;
    case 9: {   /* the clock 0x467828: [G+0x90] + the scripted t */
        gg_note(9, NULL, 0);
        return get32(gg_world + GG_G + 0x90u) + gg_clock_t + k;
    }
    case 10:   /* 0x492bec(name, ext): the path */
        gg_note(10, a, 2);
        return 0x10100000u ^ a[0] ^ k;
    case 11:   /* 0x45cdf4(path): the bitmap */
        gg_note(11, a, 1);
        return gg_load_value ^ k;
    case 12:   /* widget slot +4 (W) */
        gg_note(12, a, 1);
        gg_bytes(a[0] + 0x40u, 1u);
        gg_bytes(a[0] + 0x7au, 4u);
        return 0x3c000000u ^ a[0] ^ k;
    case 13:   /* widget slot +0xc (W); may leave a byte in G+0xb0 */
        gg_note(13, a, 1);
        if (gg_frame_b0 >= 0) gg_world[GG_G + 0xb0u] = (BYTE)gg_frame_b0;
        return 0x5a000000u ^ a[0] ^ k;
    case 14:   /* element slot +4 (G) */
        gg_note(14, a, 1);
        gg_bytes(a[0] + 0xb0u, 1u);
        return 0x14000000u ^ k;
    case 15:   /* element slot +0x1c (G) as a recorder */
        gg_note(15, a, 1);
        return 0x15000000u ^ k;
    default:   /* the port's slot 0 (P, event): the 6 event bytes */
        gg_note(16, a, 1);
        gg_bytes(a[1], 6u);
        return 0x77665500u ^ (k << 12) ^ 0x44u;
    }
}
#define GG_HOST1(k) static uint32_t __cdecl gg_o##k(uint32_t a) { const uint32_t s[1] = {a}; return gg_record(k, s); }
#define GG_HOST2(k) static uint32_t __cdecl gg_o##k(uint32_t a, uint32_t b) { const uint32_t s[2] = {a, b}; return gg_record(k, s); }
#define GG_HOST3(k) static uint32_t __cdecl gg_o##k(uint32_t a, uint32_t b, uint32_t c) { const uint32_t s[3] = {a, b, c}; return gg_record(k, s); }
static uint32_t __cdecl gg_o9(void) { return gg_record(9, NULL); }
GG_HOST1(1)
GG_HOST1(2)
GG_HOST1(3)
GG_HOST1(4)
GG_HOST1(5)
GG_HOST2(6)
GG_HOST3(7)
GG_HOST3(8)
GG_HOST2(10)
GG_HOST1(11)
GG_HOST1(12)
GG_HOST1(13)
GG_HOST1(14)
GG_HOST1(15)
GG_HOST2(16)
static void gg_s_stub(es2_sim_service_call *sc, void *user) { sc->eax = gg_record((unsigned)(uintptr_t)user, sc->slots); }
static const struct { uint32_t va; void *stub; unsigned kind, slots; } gg_stubs[] = {
    {0x0047749cu, (void *)gg_o1, 1, 1}, {0x004774f0u, (void *)gg_o2, 2, 1}, {0x00431410u, (void *)gg_o3, 3, 1},
    {0x004342e0u, (void *)gg_o4, 4, 1}, {0x00438bc0u, (void *)gg_o5, 5, 1}, {0x00438920u, (void *)gg_o6, 6, 2},
    {0x0043122cu, (void *)gg_o7, 7, 3}, {0x00435ac8u, (void *)gg_o8, 8, 3}, {0x00467828u, (void *)gg_o9, 9, 0},
    {0x00492becu, (void *)gg_o10, 10, 2}, {0x0045cdf4u, (void *)gg_o11, 11, 1},
};
#define GG_STUBS (sizeof(gg_stubs) / sizeof(gg_stubs[0]))
static const struct { void *stub; unsigned kind, slots; } gg_hosts[] = {
    {(void *)gg_o12, 12, 1}, {(void *)gg_o13, 13, 1}, {(void *)gg_o14, 14, 1}, {(void *)gg_o15, 15, 1}, {(void *)gg_o16, 16, 2},
};
static BYTE gg_saved[GG_STUBS][5];
static int gg_patch(uint32_t va, void *target, BYTE saved[5], int install) {
    BYTE *site = mapped + (va - 0x400000u);
    DWORD previous;
    if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &previous)) return fail("gauges: stub protection");
    if (install) { memcpy(saved, site, 5); site[0] = 0xe9; put32(site + 1, (uint32_t)(uintptr_t)target - (uint32_t)(uintptr_t)(site + 5)); }
    else memcpy(site, saved, 5);
    if (!VirtualProtect(site, 5, previous, &previous)) return fail("gauges: stub restore protection");
    FlushInstructionCache(GetCurrentProcess(), site, 5);
    return 0;
}
static int gg_registered(uint32_t va) {
    for (unsigned i = 0; i < gg_dispatch.count; ++i) if (gg_dispatch.entries[i].va == va) return 1;
    return 0;
}
/* The recorders: VAs without a row are added before the seal, rows are
 * replaced after it. The host slots are rows at their host addresses. */
static int gg_dispatch_setup(void) {
    es2_sim_dispatch_init(&gg_dispatch, gg_entries, 4096, gg_refuse, NULL);
    static es2_sim_services s;
    s.memory = &ir_memory;
    s.heap = &ir_heap;
    s.vt = &lc_vt;
    s.fs0 = NULL;
    if (es2_sim_register_services(&gg_dispatch, &s)) return fail("gauges: dispatch");
    static int replace[GG_STUBS];
    for (unsigned i = 0; i < GG_STUBS; ++i) {
        replace[i] = gg_registered(gg_stubs[i].va);
        if (!replace[i] && es2_sim_dispatch_add_service(&gg_dispatch, gg_stubs[i].va, "gauges recorder", gg_s_stub,
                                                        (void *)(uintptr_t)gg_stubs[i].kind, 0, (uint8_t)gg_stubs[i].slots))
            return fail("gauges: dispatch");
    }
    for (unsigned i = 0; i < sizeof(gg_hosts) / sizeof(gg_hosts[0]); ++i)
        if (es2_sim_dispatch_add_service(&gg_dispatch, (uint32_t)(uintptr_t)gg_hosts[i].stub, "gauges slot", gg_s_stub,
                                         (void *)(uintptr_t)gg_hosts[i].kind, 0, (uint8_t)gg_hosts[i].slots))
            return fail("gauges: dispatch");
    const char *error = NULL;
    uint32_t bad = 0;
    if (es2_sim_dispatch_seal(&gg_dispatch, &error, &bad)) return fail("gauges: dispatch seal");
    for (unsigned i = 0; i < GG_STUBS; ++i) {
        if (!replace[i]) continue;
        es2_sim_entry *e = (es2_sim_entry *)es2_sim_dispatch_find(&gg_dispatch, gg_stubs[i].va);
        if (!e) return fail("gauges: a replaced row is missing");
        e->kind = ES2_SIM_SERVICE; e->name = "gauges recorder"; e->service = gg_s_stub;
        e->service_user = (void *)(uintptr_t)gg_stubs[i].kind; e->pop_bytes = 0; e->stack_slots = (uint8_t)gg_stubs[i].slots;
    }
    return 0;
}

/* ---- the case script */
typedef struct gg_preset { uint32_t va, value; unsigned width; } gg_preset;
static gg_preset gg_presets[32];
static unsigned gg_preset_count;
static void gg_set32(uint32_t va, uint32_t v) { gg_presets[gg_preset_count++] = (gg_preset){va, v, 4}; }
static void gg_set8(uint32_t va, uint32_t v) { gg_presets[gg_preset_count++] = (gg_preset){va, v, 1}; }
/* An op: the VA, its arguments, which of EAX/ECX/EDX (bits 0/1/2) the
 * contract defines. */
typedef struct gg_op { uint32_t va, a[4]; unsigned n, regs; } gg_op;
static gg_op gg_op1;
static uint32_t gg_res[2][3];
static uint32_t gg_rand(uint32_t n) { return random32() % n; }
static void gg_w32(uint32_t offset, uint32_t v) { put32(gg_world_start + offset, v); }
static void gg_w16(uint32_t offset, uint32_t v) { put16(gg_world_start + offset, (uint16_t)v); }
static void gg_w8(uint32_t offset, uint32_t v) { gg_world_start[offset] = (BYTE)v; }
static uint32_t gg_r32(uint32_t offset) { return get32(gg_world_start + offset); }
static BYTE gg_r8(uint32_t offset) { return gg_world_start[offset]; }
static void gg_op_set(uint32_t va, unsigned regs, unsigned n, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3) {
    gg_op1 = (gg_op){va, {a0, a1, a2, a3}, n, regs};
}
static uint32_t gg_flag(void) { return gg_rand(2u) ? 0u : 1u + gg_rand(255u); }
/* The common world: a random G with its vtable (slots +4, +0x1c host
 * recorders), the three widgets (vtable at +0x17: +4, +0xc), the label,
 * the animation word, D, the panel and its port. */
static void gg_common(void) {
    memset(gg_world_start, 0, GG_WORLD_SIZE);
    gg_preset_count = 0;
    gg_new_fail_mask = 0;
    random_bytes(gg_world_start, GG_WORLD_SIZE);
    gg_w32(GG_G, gg_at(GG_GVT));
    gg_w32(GG_GVT + 4u, (uint32_t)(uintptr_t)gg_o14);
    gg_w32(GG_GVT + 0x1cu, (uint32_t)(uintptr_t)gg_o15);
    gg_w32(GG_G + 0x18u, gg_at(GG_W18));
    gg_w32(GG_G + 0x1cu, gg_at(GG_W1C));
    gg_w32(GG_G + 0x20u, gg_at(GG_W20));
    gg_w32(GG_W18 + 0x17u, gg_at(GG_WVT));
    gg_w32(GG_W1C + 0x17u, gg_at(GG_WVT));
    gg_w32(GG_W20 + 0x17u, gg_at(GG_WVT));
    gg_w32(GG_WVT + 4u, (uint32_t)(uintptr_t)gg_o12);
    gg_w32(GG_WVT + 0xcu, (uint32_t)(uintptr_t)gg_o13);
    gg_w8(GG_W18 + 0x1fu, gg_flag());
    gg_w32(GG_G + 0xdbu, gg_at(GG_L));
    gg_w32(GG_G + 0xcau, gg_at(GG_L));
    gg_w32(GG_G + 0x6cu, gg_rand(3u) ? gg_at(GG_ANIM) : 0u);
    gg_w16(GG_ANIM, gg_rand(2u) ? 0u : random32());
    gg_w8(GG_G + 0x76u, gg_flag());
    gg_w8(GG_G + 0x77u, gg_flag());
    gg_w8(GG_G + 0x8cu, gg_rand(6u) ? 1u + gg_rand(255u) : 0u);
    gg_w8(GG_G + 0x8du, gg_flag());
    gg_w8(GG_G + 0x8eu, gg_flag());
    gg_w8(GG_G + 0xd3u, gg_flag());
    gg_w8(GG_G + 0xd6u, gg_flag());
    static const BYTE states[] = {0, 1, 2, 2, 3, 0xff};
    gg_w8(GG_G + 0xb0u, states[gg_rand(6u)]);
    gg_w8(GG_D + 0x1cu, gg_flag());
    gg_w32(GG_H + 0x20bu, gg_at(GG_P));
    gg_w32(GG_P, gg_at(GG_PVT));
    gg_w32(GG_PVT, (uint32_t)(uintptr_t)gg_o16);
    gg_set32(0x004cf99cu, gg_at(GG_D));
    gg_set32(0x0049b088u, gg_at(GG_H));
    gg_set32(0x0049b0acu + 4u * 10u, 0x0a000000u ^ random32());   /* the two label colours (filled at run time) */
    gg_set32(0x0049b0acu + 4u * 11u, 0x0b000000u ^ random32());
    gg_vis_mask = random32();
    if (!gg_rand(4u)) gg_vis_mask = 0;
    else if (!gg_rand(3u)) gg_vis_mask = 0xffffffffu;
    for (unsigned i = 0; i < 4u; ++i) gg_alert[i] = gg_rand(2u) ? random32() & 0xffff0000u : random32();
    gg_blink_byte = gg_rand(3u) ? -1 : (int)(gg_rand(2u) ? 0u : 1u + gg_rand(255u));
    gg_frame_b0 = gg_rand(3u) ? -1 : (int)gg_rand(4u);
    static const uint32_t ts[] = {0, 1, 2, 49, 50, 51, 52, 0xffffffffu, 0x80000000u};
    gg_clock_t = gg_rand(2u) ? ts[gg_rand(9u)] : gg_rand(60u);
    gg_load_value = random32();
}
static void gg_side(unsigned side) {
    lc_reset(side, 47, 0x01000000u, 1);
    lc_loop.dispatch = &gg_dispatch;
    es2_sim_load_headless(&gg_load, &lc_loop);
    es2_sim_load_bind(&gg_load);
    memset(&gg_view, 0, sizeof(gg_view));
    gg_view.loop = &lc_loop;
    es2_sim_view_bind(&gg_view);
    es2_sim_input_headless(&gg_input, &lc_loop, &lc_journal[side], 640, 480);
    es2_sim_input_bind(&gg_input);
    unsigned headless = gg_hud.headless;
    es2_sim_hud_init(&gg_hud, &lc_loop);
    gg_hud.headless = headless;
    es2_sim_hud_bind(&gg_hud);
    memcpy(gg_world, gg_world_start, GG_WORLD_SIZE);
    for (unsigned i = 0; i < gg_preset_count; ++i) {
        BYTE *p = mapped + (gg_presets[i].va - 0x400000u);
        if (gg_presets[i].width == 4) put32(p, gg_presets[i].value);
        else *p = (BYTE)gg_presets[i].value;
    }
    gg_side_now = side;
    gg_hash[side] = 0xcbf29ce484222325ull;
    gg_count[side] = 0;
    gg_text[side][0] = 0;
    memset(gg_calls[side], 0, sizeof(gg_calls[side]));
}
static int gg_run(const char *what, unsigned k) {
    for (unsigned side = 0; side < 2u; ++side) {
        gg_side(side);
        gg_op *o = &gg_op1;
        uint32_t in[3] = {0, 0, 0}, out[3] = {0, 0, 0};
        static char where[200];
        snprintf(where, sizeof(where), "gauges (%s side %u: 0x%08lx(0x%lx, 0x%lx))", what, side, (unsigned long)o->va,
                 (unsigned long)o->a[0], (unsigned long)o->a[1]);
        current_function = where;
        if (!side) {
            au_call((uint32_t)(uintptr_t)(mapped + (o->va - 0x400000u)), o->a, o->n, in, out);
            uint32_t edx = out[1];   /* au_call: EAX, EDX, ECX */
            out[1] = out[2];
            out[2] = edx;
        } else {
            es2_sim_result r;
            memset(&r, 0, sizeof(r));
            if (es2_sim_call(&gg_dispatch, o->va, o->a, o->n, in, &r)) gg_bad = 1;
            out[0] = r.eax; out[1] = r.ecx; out[2] = r.edx;
        }
        memcpy(gg_res[side], out, sizeof(out));
        if (gg_bad) { fprintf(stderr, "FAIL: gauges %s case %u (0x%08lx): a call failed\n", what, k, (unsigned long)o->va); return 1; }
        if (!side) memcpy(gg_world_start + GG_WORLD_SIZE, gg_world, GG_WORLD_SIZE);
        su_take(side);
    }
    if (gg_hash[0] != gg_hash[1] || gg_count[0] != gg_count[1]) {
        fprintf(stderr, "FAIL: gauges %s case %u: recorded calls differ (%u / %u)\n  original:%s\n  own:     %s\n", what, k, gg_count[0],
                gg_count[1], gg_text[0], gg_text[1]);
        return 1;
    }
    if (su_compare(what, k)) return 1;
    if (memcmp(gg_world, gg_world_start + GG_WORLD_SIZE, GG_WORLD_SIZE)) {
        for (unsigned i = 0, shown = 0; i < GG_WORLD_SIZE && shown < 16u; ++i)
            if (gg_world[i] != gg_world_start[GG_WORLD_SIZE + i]) {
                fprintf(stderr, "  world +0x%04x original %02x own %02x\n", i, gg_world_start[GG_WORLD_SIZE + i], gg_world[i]);
                ++shown;
            }
        fprintf(stderr, "FAIL: gauges %s case %u: the world differs (world at 0x%08lx)\n", what, k, (unsigned long)(uintptr_t)gg_world);
        return 1;
    }
    static const char *const names[3] = {"EAX", "ECX", "EDX"};
    for (unsigned r = 0; r < 3u; ++r)
        if ((gg_op1.regs >> r) & 1u && gg_res[0][r] != gg_res[1][r]) {
            fprintf(stderr, "FAIL: gauges %s case %u (0x%08lx): %s original 0x%08lx own 0x%08lx\n", what, k, (unsigned long)gg_op1.va,
                    names[r], (unsigned long)gg_res[0][r], (unsigned long)gg_res[1][r]);
            return 1;
        }
    return 0;
}

/* ---- coverage: the branch outcomes seen on side 1, read from the world
 * before the call and the recorder counts after it. */
static unsigned gg_cov[44];
/* The gate and head outcome of a frame (from the script and the world
 * before the call): 0 the gate closed, 1 the first full draw, 2 on. */
static unsigned gg_head(int has_sweep) {
    int vis0 = (gg_vis_mask & 1u) != 0;
    if (!vis0 && !gg_r8(GG_D + 0x1cu)) { ++gg_cov[0]; return 0; }
    if (!gg_r8(GG_G + 0x8cu)) { ++gg_cov[1]; return 0; }
    ++gg_cov[vis0 ? 2 : 3];
    if (has_sweep && !gg_r8(GG_G + 0x8du)) { ++gg_cov[4]; return 2; }
    if (gg_r8(GG_G + 0x8eu)) { ++gg_cov[5]; return 2; }
    if ((gg_vis_mask >> 1) & 1u) { ++gg_cov[6]; return 1; }
    ++gg_cov[7];
    return 2;
}
static void gg_after(void) {
    if (gg_calls[1][5]) ++gg_cov[8];
    else if (gg_calls[1][4]) ++gg_cov[9];
    if (gg_calls[1][12]) ++gg_cov[10];
    if (gg_calls[1][6]) ++gg_cov[11];
    if (gg_calls[1][7]) ++gg_cov[12];
    if (gg_calls[1][14]) ++gg_cov[13];
}

static int gg_energy_frame_case(unsigned k) {
    gg_common();
    unsigned head = gg_head(1);
    gg_op_set(0x00440cb8u, 0u, 1, gg_at(GG_G), 0, 0, 0);
    int bad = gg_run("energy frame", k);
    if (!bad && head == 2) { ++gg_cov[14]; gg_after(); if (gg_calls[1][9]) ++gg_cov[15]; }
    return bad;
}
static int gg_projectile_frame_case(unsigned k) {
    gg_common();
    /* The ammunition count fits the original's 8-byte _itoa buffer. */
    gg_w32(GG_G + 0xc2u, gg_rand(3u) ? gg_rand(10000000u) : 0u - gg_rand(1000000u));
    unsigned head = gg_head(0);
    gg_op_set(0x00441290u, 0u, 1, gg_at(GG_G), 0, 0, 0);
    gg_lvis_last = 0;
    int bad = gg_run("projectile frame", k);
    if (!bad && head == 2) {
        ++gg_cov[16];
        int d3 = gg_r8(GG_G + 0xd3u) != 0;
        if (gg_lvis_last == 1u) ++gg_cov[40];                /* state 2, the label hidden: no count down */
        if (gg_lvis_last == 2u && d3) ++gg_cov[41];          /* state 2, G+0xd3 set: counted down without a label */
        int busy = gg_world[GG_G + 0x76u] || (get32(gg_world + GG_G + 0x6cu) && get16(gg_world + GG_ANIM));
        if (gg_calls[1][13] && gg_frame_b0 == 1 && !gg_r8(GG_W18 + 0x1fu) && !busy) ++gg_cov[d3 ? 42 : 43];   /* state 1 */
        gg_after();
        if (gg_world[GG_G + 0xb0u] == 1u) ++gg_cov[17];   /* 2 drawn and counted down */
        if (gg_world[GG_G + 0xb0u] == 0u) ++gg_cov[18];
        if (gg_calls[1][13] && !gg_calls[1][6] && !gg_calls[1][7]) ++gg_cov[19];
    }
    return bad;
}
static int gg_pod_frame_case(unsigned k, uint32_t va) {
    gg_common();
    unsigned sweep = gg_rand(3u);
    if (sweep == 1u) gg_w32(GG_GVT + 0x1cu, 0x00443444u);
    else if (sweep == 2u) gg_w32(GG_GVT + 0x1cu, 0x00441db0u);
    unsigned head = gg_head(1);
    gg_op_set(va, 0u, 1, gg_at(GG_G), 0, 0, 0);
    int bad = gg_run(va == 0x00441878u ? "pod frame" : "energy pod frame", k);
    if (!bad && head == 2) {
        ++gg_cov[20];
        gg_after();
        if (gg_calls[1][15]) ++gg_cov[21];
        if (sweep && !gg_r8(GG_G + 0x8du)) ++gg_cov[22];
    }
    return bad;
}
static int gg_pod_case(unsigned k) { return gg_pod_frame_case(k, 0x00441878u); }
static int gg_energy_pod_case(unsigned k) { return gg_pod_frame_case(k, 0x00441da0u); }
static int gg_sweep_case(unsigned k) {
    gg_common();
    static const uint32_t fulls[] = {0, 1, 25, 1249, 1250, 1251, 0x7fffffffu, 0x80000000u, 0xffffffffu};
    gg_w32(GG_G + 0xdbu, gg_rand(2u) ? fulls[gg_rand(9u)] : random32());
    gg_w32(GG_G + 0xe2u, gg_rand(2u) ? fulls[gg_rand(9u)] : random32());
    unsigned which = gg_rand(3u);
    static const uint32_t vas[] = {0x00440eacu, 0x00441db0u, 0x00443444u};
    gg_op_set(vas[which], 1u, 1, gg_at(GG_G), 0, 0, 0);
    int bad = gg_run("sweeps", k);
    if (!bad) ++gg_cov[23 + which];
    if (!bad && which < 2u) ++gg_cov[gg_clock_t + 0u > 50u ? 26 : 27];
    return bad;
}
static int gg_click_case(unsigned k) {
    gg_common();
    int32_t n = gg_rand(5u) ? (int32_t)gg_rand(5u) : -(int32_t)gg_rand(3u);
    gg_w32(GG_G + 0x68u, (uint32_t)n);
    uint32_t w = gg_rand(4u) ? gg_at(GG_W18 + 0x10u * gg_rand(3u)) : random32();
    for (uint32_t i = 0; i < 4u; ++i) if (gg_rand(2u)) gg_w32(GG_G + 0x18u + 4u * i, w);
    gg_op_set(0x00441c04u, 7u, 2, gg_at(GG_G), w, 0, 0);
    int bad = gg_run("energy pod click", k);
    if (!bad) ++gg_cov[n > 0 && gg_r32(GG_G + 0x18u) == w ? 28 : 29];
    return bad;
}
static int gg_lamp_case(unsigned k) {
    gg_common();
    gg_w16(GG_W20 + 0x1bu, gg_rand(4u) ? gg_rand(4u) : random32());
    gg_w8(GG_W20 + 0x40u, gg_flag());
    gg_w8(GG_G + 0xb3u, gg_rand(4u) ? gg_rand(2u) : random32());
    gg_op_set(0x00441fa4u, 1u, 1, gg_at(GG_G), 0, 0, 0);
    int bad = gg_run("range lamp", k);
    if (!bad) {
        if ((uint16_t)get32(gg_world_start + GG_W20 + 0x1bu) == 2u) ++gg_cov[30];
        else ++gg_cov[(gg_r8(GG_G + 0xb3u) ^ 1u) ? (gg_r8(GG_W20 + 0x40u) ? 31 : 32) : (gg_r8(GG_W20 + 0x40u) ? 33 : 34)];
    }
    return bad;
}
static int gg_throttle_case(unsigned k) {
    gg_common();
    gg_op_set(0x00447d94u, 1u, 1, gg_at(GG_G), 0, 0, 0);
    int bad = gg_run("throttle frame", k);
    if (!bad) ++gg_cov[35];
    return bad;
}
/* 0x440834 on a fixture G: the two bitmap pointers preset or 0 (the loads
 * through the path and load recorders), the damage animation's new
 * succeeding or failing. */
static int gg_base_case(unsigned k) {
    gg_common();
    int fresh_p = gg_rand(2u), fresh_d = gg_rand(2u);
    gg_set32(0x0049c4d0u, fresh_p ? 0u : 1u + random32() % 0xfffffu);
    gg_set32(0x0049c4d4u, fresh_d ? 0u : 1u + random32() % 0xfffffu);
    gg_set8(0x004d256cu, gg_rand(2u));
    if (!gg_load_value) gg_load_value = 1u;
    gg_new_fail_mask = gg_rand(2u);
    gg_op_set(0x00440834u, 1u, 4, gg_at(GG_G), gg_at(GG_RECT), gg_at(GG_H), gg_at(GG_BW));
    int bad = gg_run("weapon gauge base", k);
    if (!bad) {
        ++gg_cov[36 + (fresh_d ? 1 : 0)];
        if (gg_new_fail_mask) ++gg_cov[38];
        if (fresh_p) ++gg_cov[39];
    }
    return bad;
}

static int gauges_cases(void) {
    current_function = "gauges";
    es2_sim_identity_memory(&ir_memory);
    es2_sim_rtl_memory(&ir_memory, &su_rtl);
    su_data_pristine = malloc(SU_DATA_SIZE);
    su_snap[0] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    su_snap[1] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    ir_region = ww_alloc(IR_REGION_SIZE);
    lc_stack = VirtualAlloc(NULL, 0x10000u, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    lc_bits = ww_alloc(LC_BITS_SIZE);
    gg_world = VirtualAlloc(NULL, GG_WORLD_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    gg_world_start = malloc(2u * GG_WORLD_SIZE);
    if (!su_data_pristine || !su_snap[0] || !su_snap[1] || !ir_region || !lc_stack || !lc_bits || !gg_world || !gg_world_start)
        return fail("gauges: memory");
    if (get32(mapped + (SU_COUNTER - 0x400000u))) return fail("gauges: the counter word is not free");
    su_pristine_take();
    static const struct { const char *dll, *name; void *stub; } stubs[] = {
        {"cw3220.dll", "_calloc", (void *)ir_calloc}, {"cw3220.dll", "_free", (void *)ir_free},
        {"cw3220.dll", "_memset", (void *)ir_memset}, {"cw3220.dll", "_memmove", (void *)ir_memmove},
        {"cw3220.dll", "_itoa", (void *)gd_itoa}, {"cw3220.dll", "_strlen", (void *)su_strlen},   /* gadgets_cases.h, startup_cases.h */
    };
    for (unsigned i = 0; i < sizeof(stubs) / sizeof(stubs[0]); ++i)
        if (!ir_patch_import(stubs[i].dll, stubs[i].name, stubs[i].stub)) {
            fprintf(stderr, "import %s!%s\n", stubs[i].dll, stubs[i].name);
            return fail("gauges: import slot not found");
        }
    if (gg_dispatch_setup()) return 1;
    for (unsigned i = 0; i < GG_STUBS; ++i) if (gg_patch(gg_stubs[i].va, gg_stubs[i].stub, gg_saved[i], 1)) return 1;
    int bad = 0;
    static const struct { const char *name; int (*run)(unsigned); unsigned cases; } parts[] = {
        {"energy frame", gg_energy_frame_case, 1500},
        {"projectile frame", gg_projectile_frame_case, 2000},
        {"pod frame", gg_pod_case, 1500},
        {"energy pod frame", gg_energy_pod_case, 600},
        {"sweeps", gg_sweep_case, 900},
        {"energy pod click", gg_click_case, 600},
        {"range lamp", gg_lamp_case, 600},
        {"throttle frame", gg_throttle_case, 100},
        {"weapon gauge base", gg_base_case, 400},
    };
    for (unsigned p = 0; !bad && p < sizeof(parts) / sizeof(parts[0]); ++p) {
        for (unsigned k = 0; !bad && k < parts[p].cases; ++k) {
            current_case = k; current_case_valid = 1;
            bad = parts[p].run(k);
        }
        if (!bad) printf("PASS: gauges %s: %u cases identical\n", parts[p].name, parts[p].cases);
    }
    for (unsigned i = 0; i < GG_STUBS; ++i) if (gg_patch(gg_stubs[i].va, gg_stubs[i].stub, gg_saved[i], 0)) bad = 1;
    if (!bad) {
        printf("GAUGES coverage:");
        for (unsigned i = 0; i < sizeof(gg_cov) / sizeof(gg_cov[0]); ++i) printf(" %u", gg_cov[i]);
        printf("; NULL allocations %u; calls: vis %u alert %u blink %u label %u copy %u cancel %u clock %u load %u wredraw %u wframe %u "
               "gredraw %u gsweep %u port %u\n", gg_new_nulls, gg_kinds[3], gg_kinds[4], gg_kinds[5], gg_kinds[6], gg_kinds[7], gg_kinds[8],
               gg_kinds[9], gg_kinds[11], gg_kinds[12], gg_kinds[13], gg_kinds[14], gg_kinds[15], gg_kinds[16]);
    }
    for (unsigned i = 0; !bad && i < sizeof(gg_cov) / sizeof(gg_cov[0]); ++i)
        if (!gg_cov[i]) {
            fprintf(stderr, "gauges coverage slot %u\n", i);
            bad = fail("gauges: a path was not taken");
        }
    current_case_valid = 0;
    put32(mapped + 0x2cbee4u, 0);
    memcpy(mapped + SU_DATA_RVA, su_data_pristine, SU_DATA_SIZE);
    return bad;
}
