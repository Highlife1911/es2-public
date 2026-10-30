/* The display transitions 0x42a4c4 (leave) and 0x42a9c0 (enter), their small
 * callees and the shield palette 0x443918, original machine code against
 * sim/src/display.c (--display; target :differential_display_test). Included
 * after present_cases.h; reuses startup_cases.h's DATA reset and snapshots
 * (su_reset, su_take, su_compare), loop_cases.h's loop reset (lc_reset,
 * lc_machine) and input_layer_cases.h's pass-through (ic_original).
 *
 * Both sides start from one synthetic world built at random (the display
 * D with its eight 32-byte records, the view V with its graphics context,
 * a second view, the cockpit root H with up to five buttons, the cockpit
 * port and the port 0x433158 answers, the camera, the palette, the shield
 * gauge G) and the same DATA globals. The other packages' callees are
 * stubs that record letters and values: on the original side host
 * functions jumped to from the original entries (the scroll 0x487e60 with
 * its region, point, the GC's +0x208/+0x20c and the construction counter;
 * the present 0x464a14 with its return address and S; 0x45c638, 0x42a1f0,
 * 0x429e60, 0x431518, 0x433158, 0x42da08, 0x462fe0, 0x463014, 0x45b68c,
 * 0x437164, 0x433f7c, 0x433ee8, 0x42d2f8); on the own side the same
 * letters from service stubs at those VAs and, for the present, the
 * present instance's on_present (es2_sim_present_frame with the site). The
 * driver slots and vtables hold host stubs on both sides. The q10 multiply
 * 0x47e0b0, 0x45edf8, the palette writes 0x4303c4 (hud_load.h's row on the own
 * side) and the button rows run as the original's code / the own rows.
 * The scroll's pixels are gfx_primitives.h's (its row is tested by --gfx); here only
 * its arguments are compared.
 *
 * Parts: rows (the small callees 0x42d9e8, 0x434604, 0x434634, 0x434928,
 * 0x434944), shield (0x443918), leave (0x42a4c4), enter (0x42a9c0) and frame
 * (presentation_frame.h's 0x45fba4 around them, with [0x4d2537] = 1 through 0x481224),
 * capture (the rows 0x45b68c and 0x433ee8, stubbed in the other
 * parts). Compared: EAX (not for the frame), DATA 0x497000..0x6cc000, the world
 * and the stub log. */
#pragma once
/* The headers this suite builds on (sim_main.c). */
#include "init_records_cases.h"
#include "startup_cases.h"
#include "loop_cases.h"
#include "input_layer_cases.h"
#include "es2/sim/present.h"
#include "es2/sim/load.h"
#define DP_WORLD_SIZE 0x4000u
#define DP_D 0x0000u
#define DP_R 0x0100u            /* eight 32-byte records */
#define DP_V 0x0300u
#define DP_V2 0x0380u           /* the other view a stub makes current */
#define DP_GC 0x0400u           /* the GC block; V+0x44 = its address + 4 */
#define DP_H 0x0700u
#define DP_HPORT 0x0980u
#define DP_PORT 0x09a0u         /* +0x49e: the port's flag */
#define DP_HPORT_VT 0x0e80u
#define DP_PORT_VT 0x0ec0u
#define DP_BUTTONS 0x0f00u      /* five buttons 0x80 apart */
#define DP_BUTTON_VT 0x1180u
#define DP_CAMERA 0x1200u
#define DP_P 0x1300u            /* the palette: +8 the entries */
#define DP_P_ENTRIES 0x1400u
#define DP_G 0x2000u            /* the shield gauge */
#define DP_CLIP 0x2400u         /* the clip records of V and V2 (0x400 apart) */
#define DP_DRVTAB 0x2c00u       /* a driver table (36 dwords) */
#define DP_P_REC 0x2d40u        /* the record P of 0x433ee8 */

static BYTE *dp_world, *dp_world_start, *dp_snap[2];
static int dp_bad, dp_eax_free, dp_edx_check;
static uint32_t dp_eax_mask = 0xffffffffu, dp_edx_out[2];   /* the capture part */   /* the frame: EAX is the last callee's, read by no caller (present.c) */
static unsigned dp_side;
static es2_sim_present dp_present;
static char dp_desc[256];
static uint32_t dp_va(uint32_t off) { return (uint32_t)(uintptr_t)(dp_world + off); }
static void dp_put32(uint32_t off, uint32_t v) { put32(dp_world_start + off, v); }
static void dp_put16(uint32_t off, uint32_t v) { put16(dp_world_start + off, (uint16_t)v); }
static uint32_t dp_r(uint32_t n) { return random32() % n; }
static int32_t dp_ri(int32_t lo, int32_t hi) { return lo + (int32_t)dp_r((uint32_t)(hi - lo + 1)); }
static uint32_t dp_get(uint32_t a) { return get32((BYTE *)(uintptr_t)a); }

/* ---- the stub log */
static unsigned dp_letters[6][128], dp_part;
static BYTE dp_seen[128];
static uint64_t dp_hash[2];
static unsigned dp_count[2];
static char dp_text[2][4096];
static size_t dp_len[2];
static void dp_note(char letter, uint32_t a, uint32_t b, uint32_t c) {
    uint64_t h = dp_hash[dp_side];
    const uint32_t v[4] = {(uint32_t)letter, a, b, c};
    for (unsigned i = 0; i < 4u; ++i) { h ^= v[i]; h *= 0x100000001b3ull; }
    dp_hash[dp_side] = h;
    ++dp_count[dp_side];
    if (!dp_side && (unsigned char)letter < 128u) dp_seen[(unsigned char)letter] = 1;
    if (dp_len[dp_side] + 48u < sizeof(dp_text[0]))
        dp_len[dp_side] += (size_t)sprintf(dp_text[dp_side] + dp_len[dp_side], "%c(%lx,%lx,%lx) ", letter, (unsigned long)a,
                                           (unsigned long)b, (unsigned long)c);
}
/* The scroll's arguments: the region (type, four corners), the point, and
 * the GC fields and the counter at the call. */
static void dp_scroll(uint32_t region, uint32_t pt) {
    uint32_t h = 2166136261u;
    for (unsigned i = 0; i < 5u; ++i) { h ^= dp_get(region + 4u * i); h *= 16777619u; }
    dp_note('S', h, dp_get(pt), dp_get(pt + 4u));
    uint32_t gc = dp_va(DP_GC);
    dp_note('s', dp_get(gc + 0x208u), dp_get(gc + 0x20cu), get32(mapped + (SU_COUNTER - 0x400000u)));
}
static uint32_t dp_level, dp_swap_view;
static uint32_t dp_answer(char letter, const uint32_t *s) {
    switch (letter) {
    case 'S': dp_scroll(s[0], s[1]); return 0;
    case 'a': dp_note('a', s[0], s[1], 0); put32((BYTE *)(uintptr_t)(s[0] + 0x14u), s[1] + 0x10u); return 0x61;
    case 'b': dp_note('b', s[0], s[1], 0); put32((BYTE *)(uintptr_t)(s[0] + 0x14u), s[1]); return 0x62;
    case 'W':
        dp_note('W', 0, 0, 0);
        if (dp_swap_view) put32(mapped + (0x006c5ed0u - 0x400000u), dp_va(DP_V2));
        return dp_va(DP_PORT);
    case 'l': dp_note('l', s[0], 0, 0); return dp_level;
    case 'B': dp_note('B', s[0], 0, 0); return 0x77;
    case 'L': case 'P': dp_note(letter, s[0], s[1], 0); return 0;
    case 'o': dp_note('o', s[0], dp_get(s[1]), dp_get(s[1] + 4u)); return 0;
    case 'x': dp_note('x', s[0], get16((BYTE *)(uintptr_t)(s[0] + 0x1bu)), dp_get(s[0] + 0x5cu)); return 0x1200u + (s[0] & 0xffu);
    case 'Y': case 'f': case 'g': case 'u': case 'v': case 'C': dp_note(letter, 0, 0, 0); return 0;
    default: dp_note(letter, s[0], 0, 0); return 0;   /* c n j k m q r s h */
    }
}

/* ---- the original side's stubs (jumped to from the original entries, or
 * held in the driver slots and vtables) */
#define DP_STUB0(name, letter) static uint32_t __cdecl name(void) { return dp_answer(letter, NULL); }
#define DP_STUB1(name, letter) static uint32_t __cdecl name(uint32_t a) { const uint32_t s[1] = {a}; return dp_answer(letter, s); }
#define DP_STUB2(name, letter) \
    static uint32_t __cdecl name(uint32_t a, uint32_t b) { const uint32_t s[2] = {a, b}; return dp_answer(letter, s); }
DP_STUB2(dp_o_scroll, 'S')
DP_STUB0(dp_o_retrace, 'Y')
DP_STUB2(dp_o_set, 'a')
DP_STUB2(dp_o_switch, 'b')
DP_STUB1(dp_o_panel, 'c')
DP_STUB0(dp_o_port_a, 'W')
DP_STUB1(dp_o_border, 'B')
DP_STUB1(dp_o_level, 'l')
DP_STUB2(dp_o_set_level, 'L')
DP_STUB1(dp_o_slide_sound, 'n')
DP_STUB2(dp_o_redraw, 'P')
DP_STUB0(dp_o_up_done, 'f')
DP_STUB0(dp_o_done, 'g')
DP_STUB1(dp_o_jolt, 'j')
DP_STUB0(dp_o_cycle, 'C')
DP_STUB1(dp_o_upload, 'U')
DP_STUB1(dp_o_palette_set, 'X')
static uint32_t __cdecl dp_o_present(void) {
    dp_note('V', (uint32_t)(uintptr_t)__builtin_return_address(0), get32(mapped + 0xd3018u), get32(mapped + 0xd301cu));
    return 1;
}
DP_STUB2(dp_drv_origin, 'o')
DP_STUB1(dp_drv_draw, 'k')
DP_STUB1(dp_drv_show, 'm')
DP_STUB1(dp_drv_view, 'q')
DP_STUB0(dp_drv_lock, 'u')
DP_STUB0(dp_drv_unlock, 'v')
DP_STUB1(dp_port14, 'r')
DP_STUB1(dp_port18, 's')
DP_STUB1(dp_hport18, 'h')
DP_STUB1(dp_button_fn, 'x')
DP_STUB0(dp_drv_other, 'O')   /* the other entries of a copied driver table */
static uint32_t __cdecl dp_o_memset(uint32_t d, uint32_t v, uint32_t n) { dp_note('z', d, v, n); uint32_t a[3] = {d, v, n}; return su_rtl_call(ES2_RTL_MEMSET, a); }

static const struct { uint32_t va; void *stub; char letter; unsigned slots; } dp_patched[] = {
    {0x00487e60u, (void *)dp_o_scroll, 'S', 2}, {0x00464a14u, (void *)dp_o_present, 'V', 0},
    {0x0045c638u, (void *)dp_o_retrace, 'Y', 0}, {0x0042a1f0u, (void *)dp_o_set, 'a', 2},
    {0x00429e60u, (void *)dp_o_switch, 'b', 2}, {0x00431518u, (void *)dp_o_panel, 'c', 1},
    {0x00433158u, (void *)dp_o_port_a, 'W', 0}, {0x0042da08u, (void *)dp_o_border, 'B', 1},
    {0x00462fe0u, (void *)dp_o_level, 'l', 1}, {0x00463014u, (void *)dp_o_set_level, 'L', 2},
    {0x0045b68cu, (void *)dp_o_slide_sound, 'n', 1}, {0x00437164u, (void *)dp_o_redraw, 'P', 2},
    {0x00433f7cu, (void *)dp_o_up_done, 'f', 0}, {0x00433ee8u, (void *)dp_o_done, 'g', 0},
    {0x0042d2f8u, (void *)dp_o_jolt, 'j', 1},
    {0x004306acu, (void *)dp_o_cycle, 'C', 0}, {0x00465cc4u, (void *)dp_o_upload, 'U', 1},
    {0x00481fe0u, (void *)dp_o_palette_set, 'X', 1},
};
static BYTE dp_saved[sizeof(dp_patched) / sizeof(dp_patched[0])][5];
static const struct { void *stub; char letter; unsigned slots; } dp_hosts[] = {
    {(void *)dp_drv_origin, 'o', 2}, {(void *)dp_drv_draw, 'k', 1}, {(void *)dp_drv_show, 'm', 1}, {(void *)dp_drv_view, 'q', 1},
    {(void *)dp_drv_lock, 'u', 0}, {(void *)dp_drv_unlock, 'v', 0}, {(void *)dp_port14, 'r', 1}, {(void *)dp_port18, 's', 1},
    {(void *)dp_hport18, 'h', 1}, {(void *)dp_button_fn, 'x', 1}, {(void *)dp_drv_other, 'O', 0},
};
static int dp_patch(uint32_t va, void *target, BYTE saved[5], int install) {
    BYTE *site = mapped + (va - 0x400000u);
    DWORD previous;
    if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &previous)) return fail("display: stub protection");
    if (install) { memcpy(saved, site, 5); site[0] = 0xe9; put32(site + 1, (uint32_t)(uintptr_t)target - (uint32_t)(uintptr_t)(site + 5)); }
    else memcpy(site, saved, 5);
    if (!VirtualProtect(site, 5, previous, &previous)) return fail("display: stub restore protection");
    FlushInstructionCache(GetCurrentProcess(), site, 5);
    return 0;
}

/* ---- the own side: the sim services, the stubs as services */
void es2_sim_service_shield_palette(es2_sim_service_call *call, void *user);   /* sim/src/display.c, no row yet */
static es2_sim_entry dp_entries[4096];
static es2_sim_dispatch dp_dispatch;
static void dp_s_stub(es2_sim_service_call *call, void *user) { call->eax = dp_answer((char)(uintptr_t)user, call->slots); }
static es2_sim_service_fn dp_retrace_real, dp_capture_real, dp_done_real, dp_memset_real;
static void dp_s_memset(es2_sim_service_call *call, void *user) {
    dp_note('z', call->slots[0], call->slots[1], call->slots[2]);
    dp_memset_real(call, user);
}
static void dp_s_retrace(es2_sim_service_call *call, void *user) {
    (void)user;
    dp_note('Y', 0, 0, 0);
    dp_retrace_real(call, NULL);
}
static void dp_on_present(void *user, const es2_sim_present_record *r) {
    (void)user;
    dp_note('V', r->site, (uint32_t)r->page[0], (uint32_t)r->page[1]);
}
static void dp_refuse(const es2_sim_refusal *r, void *user) {
    (void)user;
    fprintf(stderr, "FAIL: own display: refused call to 0x%08lx (%s): %s\n", (unsigned long)r->va, r->name ? r->name : "?",
            r->reason ? r->reason : "");
    dp_bad = 1;
}
static int dp_has_row(uint32_t va) {
    for (unsigned i = 0; i < es2_sim_service_count(); ++i) {
        uint32_t row = 0;
        es2_sim_service_row(i, &row);
        if (row == va) return 1;
    }
    return 0;
}
static int dp_dispatch_setup(void) {
    es2_sim_dispatch_init(&dp_dispatch, dp_entries, 4096, dp_refuse, NULL);
    static es2_sim_services s;
    s.memory = &ir_memory;
    s.heap = &ir_heap;
    s.vt = &lc_vt;
    s.fs0 = NULL;
    if (es2_sim_register_services(&dp_dispatch, &s)) return fail("display: dispatch");
    /* the callees without a row (open), the host stubs, the multiply hook */
    static const struct { uint32_t va; char letter; unsigned slots; } added[] = {
        {0x0045b68cu, 'n', 1}, {0x00437164u, 'P', 2}, {0x00433f7cu, 'f', 0}, {0x00433ee8u, 'g', 0}, {0x0042d2f8u, 'j', 1},
    };
    /* a VA that has a row is replaced after the seal instead
     * (es2_sim_dispatch_find works only after it) */
    for (unsigned i = 0; i < sizeof(added) / sizeof(added[0]); ++i)
        if (!dp_has_row(added[i].va) && es2_sim_dispatch_add_service(&dp_dispatch, added[i].va, "display stub", dp_s_stub, (void *)(uintptr_t)added[i].letter, 0,
                                         (uint8_t)added[i].slots))
            return fail("display: dispatch (stub)");
    for (unsigned i = 0; i < sizeof(dp_hosts) / sizeof(dp_hosts[0]); ++i)
        if (es2_sim_dispatch_add_service(&dp_dispatch, (uint32_t)(uintptr_t)dp_hosts[i].stub, "display host stub", dp_s_stub,
                                         (void *)(uintptr_t)dp_hosts[i].letter, 0, (uint8_t)dp_hosts[i].slots))
            return fail("display: dispatch (host stub)");
    if (!dp_has_row(0x0047e0b0u) && es2_sim_dispatch_add_service(&dp_dispatch, 0x0047e0b0u, "q10 multiply (original)", ic_original, (void *)(uintptr_t)2u, 0, 2))
        return fail("display: dispatch (pass-through)");
    if (!dp_has_row(0x0045edf8u) && es2_sim_dispatch_add_service(&dp_dispatch, 0x0045edf8u, "camera_current (original hook)", ic_original, (void *)(uintptr_t)0u, 0, 0))
        return fail("display: dispatch (camera pass-through)");
    const char *error = NULL;
    uint32_t bad = 0;
    if (es2_sim_dispatch_seal(&dp_dispatch, &error, &bad)) return fail("display: dispatch seal");
    static const uint32_t replaced[] = {0x00487e60u, 0x0042a1f0u, 0x00429e60u, 0x00431518u, 0x00433158u,
                                        0x0042da08u, 0x00462fe0u, 0x00463014u, 0x004306acu, 0x00465cc4u, 0x00481fe0u};
    for (unsigned i = 0; i < sizeof(replaced) / sizeof(replaced[0]); ++i) {
        es2_sim_entry *e = (es2_sim_entry *)es2_sim_dispatch_find(&dp_dispatch, replaced[i]);
        if (!e || e->kind != ES2_SIM_SERVICE) return fail("display: service missing");
        char letter = 0;
        for (unsigned k = 0; k < sizeof(dp_patched) / sizeof(dp_patched[0]); ++k)
            if (dp_patched[k].va == replaced[i]) letter = dp_patched[k].letter;
        e->service = dp_s_stub;
        e->service_user = (void *)(uintptr_t)letter;
    }
    /* Row shield_palette (0x443918) points at the own shield palette. */
    for (unsigned i = 0; i < sizeof(added) / sizeof(added[0]); ++i) {
        if (!dp_has_row(added[i].va)) continue;
        es2_sim_entry *e = (es2_sim_entry *)es2_sim_dispatch_find(&dp_dispatch, added[i].va);
        if (!e || e->kind != ES2_SIM_SERVICE) return fail("display: stub row missing");
        if (added[i].va == 0x0045b68cu) dp_capture_real = e->service;   /* run by the capture part */
        if (added[i].va == 0x00433ee8u) dp_done_real = e->service;
        e->service = dp_s_stub;
        e->service_user = (void *)(uintptr_t)added[i].letter;
    }
    es2_sim_entry *sp = (es2_sim_entry *)es2_sim_dispatch_find(&dp_dispatch, 0x00443918u);
    if (!sp || sp->kind != ES2_SIM_SERVICE) return fail("display: shield palette row missing");
    sp->service = es2_sim_service_shield_palette;
    sp->service_user = NULL;
    es2_sim_entry *e = (es2_sim_entry *)es2_sim_dispatch_find(&dp_dispatch, 0x0045c638u);
    if (!e || e->kind != ES2_SIM_SERVICE) return fail("display: retrace row missing");
    dp_retrace_real = e->service;
    e->service = dp_s_retrace;
    return 0;
}
static uint32_t dp_own(uint32_t va, const uint32_t *slots, unsigned n, uint32_t eax_in) {
    uint32_t regs[3] = {eax_in, 0, 0};
    es2_sim_result r;
    memset(&r, 0, sizeof(r));
    if (es2_sim_call(&dp_dispatch, va, slots, n, regs, &r)) dp_bad = 1;
    dp_edx_out[1] = r.edx;
    return r.eax;
}
static uint32_t dp_machine(uint32_t va, const uint32_t *a, unsigned n, uint32_t eax_in) {
    es2_hook_result r;
    uint32_t regs[3] = {eax_in, 0, 0};
    es2_hook_call c = {mapped + (va - 0x400000u), ES2_ABI_CDECL, n, a, initial_fpu, &r, regs};
    es2_hook_machine_call(&c);
    dp_edx_out[0] = r.edx;
    return r.eax;
}

/* ---- sides */
static struct { uint32_t va, v; unsigned size; } dp_data[128];
static unsigned dp_data_count;
static void dp_set(uint32_t va, uint32_t v, unsigned size) {
    dp_data[dp_data_count].va = va; dp_data[dp_data_count].v = v; dp_data[dp_data_count].size = size; ++dp_data_count;
}
static void dp_begin(unsigned side) {
    lc_reset(side, 47, 0x01000000u, 1);
    lc_loop.dispatch = &dp_dispatch;
    dp_side = side;
    dp_hash[side] = 0xcbf29ce484222325ull;
    dp_count[side] = 0;
    dp_len[side] = 0;
    dp_text[side][0] = 0;
    memcpy(dp_world, dp_world_start, DP_WORLD_SIZE);
    BYTE *d = mapped - 0x400000u;
    for (unsigned i = 0; i < dp_data_count; ++i) {
        if (dp_data[i].size == 4u) put32(d + dp_data[i].va, dp_data[i].v);
        else if (dp_data[i].size == 2u) put16(d + dp_data[i].va, (uint16_t)dp_data[i].v);
        else d[dp_data[i].va] = (BYTE)dp_data[i].v;
    }
    es2_sim_present_headless(&dp_present, &lc_loop);
    dp_present.on_present = dp_on_present;
    dp_present.user = NULL;
    es2_sim_present_bind(&dp_present);
}
static void dp_end(unsigned side) {
    su_take(side);
    memcpy(dp_snap[side], dp_world, DP_WORLD_SIZE);
}
static int dp_compare(const char *what, unsigned k, const uint32_t eax[2]) {
    int shown = 0;
#define DP_FAIL(...) do { if (!shown++) fprintf(stderr, "  case %u: %s\n", k, dp_desc); fprintf(stderr, __VA_ARGS__); return 1; } while (0)
    if (dp_bad) DP_FAIL("FAIL: display %s case %u: a call was refused\n", what, k);
    if (dp_hash[0] != dp_hash[1] || dp_count[0] != dp_count[1])
        DP_FAIL("FAIL: display %s case %u: the stub logs differ\n  original: %s\n  own:      %s\n", what, k, dp_text[0], dp_text[1]);
    if (dp_edx_check && dp_edx_out[0] != dp_edx_out[1])
        DP_FAIL("FAIL: display %s case %u: EDX %08lx original, %08lx own\n", what, k, (unsigned long)dp_edx_out[0], (unsigned long)dp_edx_out[1]);
    if (!dp_eax_free && ((eax[0] ^ eax[1]) & dp_eax_mask)) DP_FAIL("FAIL: display %s case %u: EAX %08lx original, %08lx own\n", what, k, (unsigned long)eax[0], (unsigned long)eax[1]);
    if (su_compare(what, k)) DP_FAIL("  (DATA)\n");
    if (memcmp(dp_snap[0], dp_snap[1], DP_WORLD_SIZE)) {
        unsigned count = 0;
        for (uint32_t i = 0; i < DP_WORLD_SIZE; ++i)
            if (dp_snap[0][i] != dp_snap[1][i] && count++ < 16)
                fprintf(stderr, "  world +0x%04lx original %02x own %02x\n", (unsigned long)i, dp_snap[0][i], dp_snap[1][i]);
        DP_FAIL("FAIL: display %s case %u: %u world bytes differ\n", what, k, count);
    }
#undef DP_FAIL
    return 0;
}

/* ---- the world a case starts from */
static void dp_world_reset(void) {
    memset(dp_world_start, 0, DP_WORLD_SIZE);
    random_bytes(dp_world_start, DP_WORLD_SIZE);
    dp_data_count = 0;
    dp_swap_view = 0;
    dp_level = random32();
    /* D: the records at +4 */
    dp_put32(DP_D + 4u, dp_va(DP_R));
    for (uint32_t i = 0; i < 8u; ++i) {
        dp_put32(DP_R + 32u * i + 0x18u, (uint32_t)dp_ri(-160, 160));
        dp_put32(DP_R + 32u * i + 0x1cu, (uint32_t)dp_ri(-120, 120));
    }
    /* V, V2: the GC, the pages, the origins */
    for (uint32_t v = DP_V; v <= DP_V2; v += DP_V2 - DP_V) {
        dp_put32(v + 0x44u, dp_va(DP_GC) + 4u);
        dp_put32(v + 0x88u, dp_r(3u));
        dp_put32(v + 0x8cu, dp_r(3u));
    }
    /* H: the cockpit port, the buttons */
    dp_put32(DP_H + 0x20bu, dp_va(DP_HPORT));
    dp_put32(DP_HPORT, dp_va(DP_HPORT_VT));
    dp_put32(DP_HPORT_VT + 0x18u, (uint32_t)(uintptr_t)dp_hport18);
    dp_put32(DP_PORT, dp_va(DP_PORT_VT));
    dp_put32(DP_PORT_VT + 0x14u, (uint32_t)(uintptr_t)dp_port14);
    dp_put32(DP_PORT_VT + 0x18u, (uint32_t)(uintptr_t)dp_port18);
    dp_world_start[DP_PORT + 0x49eu] = (BYTE)(dp_r(2u));
    uint32_t n = dp_r(4u) ? dp_r(6u) : (uint32_t)dp_ri(-3, -1);
    dp_put32(DP_H + 0x25au, n);
    for (uint32_t i = 0; i < 5u; ++i) {
        dp_put32(DP_H + 0x246u + 4u * i, dp_va(DP_BUTTONS + 0x80u * i));
        dp_put32(DP_BUTTONS + 0x80u * i + 0x17u, dp_va(DP_BUTTON_VT));
    }
    dp_put32(DP_BUTTON_VT, (uint32_t)(uintptr_t)dp_button_fn);
    dp_put32(DP_CAMERA + 0x4au, dp_r(4u) ? 0 : 1u);
    /* the palette */
    dp_put32(DP_P + 8u, dp_va(DP_P_ENTRIES));
    /* DATA */
    dp_set(0x006c5ed0u, dp_va(DP_V), 4);
    dp_set(0x0049b088u, dp_va(DP_H), 4);
    dp_set(0x004a57e8u, (uint32_t)(uintptr_t)dp_drv_origin, 4);
    dp_set(0x004a57ecu, (uint32_t)(uintptr_t)dp_drv_draw, 4);
    dp_set(0x004a57f0u, (uint32_t)(uintptr_t)dp_drv_show, 4);
    dp_set(0x004a57f4u, (uint32_t)(uintptr_t)dp_drv_view, 4);
    dp_set(0x004a5828u, (uint32_t)(uintptr_t)dp_drv_lock, 4);
    dp_set(0x004a5824u, (uint32_t)(uintptr_t)dp_drv_unlock, 4);
    dp_set(0x004d24eau, dp_r(5u) ? dp_va(DP_CAMERA) : 0, 4);
    dp_set(0x004d3034u, 0x00020007u, 4);   /* the DIB DC: the own present records */
    dp_set(0x0049b020u, dp_va(DP_P), 4);
}
/* The display's globals for leave and enter. */
static unsigned dp_cov_res[3], dp_cov_kind[2], dp_cov_paged[2], dp_cov_port[2];
static void dp_display_globals(void) {
    int big = dp_r(2u);
    uint32_t w = big ? 640u : 320u, h = big ? 480u : 240u;
    uint32_t res = dp_r(3u), kind = dp_r(3u), paged = dp_r(2u);
    uint32_t sx = dp_r(3u) == 0 ? 0 : dp_r(2u) ? w : (uint32_t)dp_ri(-5, 700);
    dp_set(0x004d253eu, w, 4); dp_set(0x004d2542u, h, 4);
    dp_set(0x004d2538u, res, 1); dp_set(0x004d2537u, kind, 1); dp_set(0x004d2566u, paged, 2);
    dp_set(0x004d3018u, sx, 4); dp_set(0x004d301cu, (uint32_t)dp_ri(-5, 500), 4);
    dp_set(0x004d2ff8u, 0, 4); dp_set(0x004d2ffcu, 0, 4); dp_set(0x004d3000u, w - 1u, 4); dp_set(0x004d3004u, h - 1u, 4);
    dp_set(0x004d267eu, random32(), 2);
    dp_set(0x004a3646u, random32(), 4);
    dp_set(0x0049dbdfu, random32(), 1);
    dp_set(0x0049f8f8u, dp_r(2u), 1);
    dp_set(0x004cfcb4u, random32(), 4);
    dp_set(0x0049ac45u, dp_r(2u), 1);
    dp_swap_view = dp_r(5u) == 0;
    ++dp_cov_res[res]; ++dp_cov_kind[kind == 1u]; ++dp_cov_paged[paged]; ++dp_cov_port[dp_world_start[DP_PORT + 0x49eu] != 0];
    snprintf(dp_desc, sizeof(dp_desc), "w %lu res %lu kind %lu paged %lu S.x %ld swap %lu port %u", (unsigned long)w, (unsigned long)res,
             (unsigned long)kind, (unsigned long)paged, (long)(int32_t)sx, (unsigned long)dp_swap_view, dp_world_start[DP_PORT + 0x49eu]);
}
static int dp_run(const char *what, unsigned k, uint32_t va, const uint32_t *args, unsigned n, uint32_t eax_in) {
    uint32_t eax[2];
    for (unsigned side = 0; side < 2u; ++side) {
        dp_begin(side);
        eax[side] = side ? dp_own(va, args, n, eax_in) : dp_machine(va, args, n, eax_in);
        dp_end(side);
    }
    for (unsigned i = 0; i < 128u; ++i) if (dp_seen[i]) { ++dp_letters[dp_part][i]; dp_seen[i] = 0; }
    return dp_compare(what, k, eax);
}

/* ---- leave */
static unsigned dp_cov_leave_req[9], dp_cov_cool, dp_cov_border, dp_cov_started[2];
static int dp_leave_case(unsigned k) {
    dp_world_reset();
    dp_display_globals();
    int32_t req = dp_ri(-1, 7);
    dp_put32(DP_D + 0x18u, (uint32_t)req);
    dp_put32(DP_D + 0x14u, dp_r(5u));
    dp_world_start[DP_D + 0x36u] = (BYTE)(dp_r(4u) == 0);
    dp_put16(DP_D + 0x1du, dp_r(5u) == 0 ? (uint32_t)dp_ri(1, 3) : 0);
    dp_world_start[DP_D + 0x1cu] = (BYTE)(dp_r(3u) == 0);
    dp_put32(DP_D + 0x23u, dp_r(4u));
    if (dp_r(4u) == 0) dp_set(0x004d3018u, dp_r(2u) ? 640u : 320u, 4);   /* S.x equal to the width */
    ++dp_cov_leave_req[req + 1]; dp_cov_cool += get16(dp_world_start + DP_D + 0x1du) != 0; dp_cov_border += dp_world_start[DP_D + 0x36u];
    ++dp_cov_started[dp_world_start[DP_D + 0x1cu]];
    uint32_t a[1] = {dp_va(DP_D)};
    return dp_run("leave", k, 0x0042a4c4u, a, 1, 0);
}

/* ---- enter */
static unsigned dp_cov_enter_req[9], dp_cov_sound[2], dp_cov_jolt[2];
static int dp_enter_case(unsigned k) {
    dp_world_reset();
    dp_display_globals();
    int32_t req = dp_r(8u) == 0 ? dp_ri(-1, 7) : dp_ri(0, 6);
    if (req == 7) req = 6;   /* above 6 is refused by the own code (the original scrolls with an undefined area) */
    dp_put32(DP_D + 0x18u, (uint32_t)req);
    uint32_t mode = dp_r(4u);
    if (req == 6 && dp_r(2u)) mode = 2;
    dp_put32(DP_D + 0x14u, mode);
    dp_world_start[DP_D + 0x1cu] = (BYTE)(dp_r(8u) != 0);
    dp_put32(DP_D + 0x23u, dp_r(4u));
    /* deltas that end the slides in a few steps */
    for (uint32_t i = 0; i < 8u; ++i) {
        dp_put32(DP_R + 32u * i + 0x18u, (uint32_t)dp_ri(-60, 60));
        dp_put32(DP_R + 32u * i + 0x1cu, (uint32_t)dp_ri(-60, 60));
    }
    ++dp_cov_enter_req[req + 1];
    uint32_t a[1] = {dp_va(DP_D)};
    for (unsigned i = 0; i < dp_data_count; ++i) {
        if (dp_data[i].va == 0x0049f8f8u) ++dp_cov_sound[dp_data[i].v != 0];
        if (dp_data[i].va == 0x0049ac45u) ++dp_cov_jolt[dp_data[i].v != 0];
    }
    char more[64];
    snprintf(more, sizeof(more), " req %ld mode %lu", (long)req, (unsigned long)mode);
    strncat(dp_desc, more, sizeof(dp_desc) - strlen(dp_desc) - 1u);
    return dp_run("enter", k, 0x0042a9c0u, a, 1, 0);
}

/* ---- shield */
static unsigned dp_cov_high[2], dp_cov_t2[3], dp_cov_t3[3];
static int dp_shield_case(unsigned k) {
    dp_world_reset();
    for (unsigned i = 0; i < 2u; ++i) {
        int32_t v = dp_r(4u) ? dp_ri(-0x80, 0x880) : (int32_t)random32();
        dp_put32(DP_G + (i ? 0xb9u : 0xb5u), (uint32_t)v);
        int32_t e = v > 0x400 ? v - 0x400 : v;
        ++dp_cov_high[v > 0x400];
        ++dp_cov_t2[e > 0x300 ? 2 : e < 0x100 ? 0 : 1];
        ++dp_cov_t3[e > 0x280 ? 2 : e < 0x180 ? 0 : 1];
    }
    for (uint32_t va = 0x0049c9cbu; va <= 0x0049c9d0u; ++va) dp_set(va, dp_r(3u) ? dp_r(256u) : (dp_r(2u) ? 0 : 255u), 1);
    dp_set(0x0049b030u, dp_r(2u), 1);
    dp_set(0x0049b028u, dp_r(0x60u), 4);
    dp_set(0x0049b02cu, dp_r(0x60u), 4);
    snprintf(dp_desc, sizeof(dp_desc), "shield b5 %ld b9 %ld", (long)(int32_t)get32(dp_world_start + DP_G + 0xb5u),
             (long)(int32_t)get32(dp_world_start + DP_G + 0xb9u));
    uint32_t a[1] = {dp_va(DP_G)};
    return dp_run("shield", k, 0x00443918u, a, 1, 0);
}

/* ---- rows */
/* ---- the presentation frame 0x45fba4 (presentation_frame.h's row): leave and enter through
 * the frame, the palette flush 0x43048c run as code (hud_load.h's row on the own
 * side), the cycler 0x4306ac, the upload 0x465cc4 and the palette switch
 * 0x481fe0 stubbed. Byte 0x4d2537 = 1 with the present word set: the frame
 * selects the surface [0x4d2568] before and [0x4d24bc] after through 0x481224
 * (the original's code against frame_buffer.c's surface_select, which present.c
 * calls directly). V and V2 are surfaces: +0x30 a format, +0x40 a clip record
 * (DP_CLIP), +0x44 the GC; format 0 finds the pair 0x4a5840 pointed at
 * DP_DRVTAB (36 dwords: the six driver slots permuted, so a missing copy
 * changes the stub log, the rest stub 'O'), format 9 none (the walk ends at
 * -1). */
static unsigned dp_cov_frame[2], dp_cov_select[2];
static int dp_frame_case(unsigned k) {
    dp_world_reset();
    dp_display_globals();
    for (uint32_t v = DP_V, n = 0; v <= DP_V2; v += DP_V2 - DP_V, ++n) {
        dp_put32(v + 0x30u, dp_r(2u) ? 0u : 9u);
        dp_put32(v + 0x40u, dp_va(DP_CLIP + 0x400u * n));
    }
    for (uint32_t i = 0; i < 36u; ++i) dp_put32(DP_DRVTAB + 4u * i, (uint32_t)(uintptr_t)dp_drv_other);
    dp_put32(DP_DRVTAB + 4u * 15u, (uint32_t)(uintptr_t)dp_drv_origin);
    dp_put32(DP_DRVTAB + 4u * 16u, (uint32_t)(uintptr_t)dp_drv_view);    /* 0x4a57ec */
    dp_put32(DP_DRVTAB + 4u * 17u, (uint32_t)(uintptr_t)dp_drv_draw);    /* 0x4a57f0 */
    dp_put32(DP_DRVTAB + 4u * 18u, (uint32_t)(uintptr_t)dp_drv_show);    /* 0x4a57f4 */
    dp_put32(DP_DRVTAB + 4u * 30u, (uint32_t)(uintptr_t)dp_drv_lock);    /* 0x4a5824 */
    dp_put32(DP_DRVTAB + 4u * 31u, (uint32_t)(uintptr_t)dp_drv_unlock);  /* 0x4a5828 */
    dp_set(0x004a5844u, dp_va(DP_DRVTAB), 4);
    dp_set(0x004d2568u, dp_r(2u) ? dp_va(DP_V) : dp_va(DP_V2), 4);
    int32_t req = dp_ri(-1, 6);
    dp_put32(DP_D + 0x18u, (uint32_t)req);
    dp_put32(DP_D + 0x14u, dp_r(4u));
    dp_world_start[DP_D + 0x1cu] = (BYTE)dp_r(2u);
    dp_put16(DP_D + 0x1du, dp_r(4u) == 0 ? 1u : 0u);
    dp_put32(DP_D + 0x23u, dp_r(4u));
    for (uint32_t i = 0; i < 8u; ++i) {
        dp_put32(DP_R + 32u * i + 0x18u, (uint32_t)dp_ri(-60, 60));
        dp_put32(DP_R + 32u * i + 0x1cu, (uint32_t)dp_ri(-60, 60));
    }
    dp_set(0x004d24c0u, dp_va(DP_D), 4);
    dp_set(0x004d24bcu, dp_r(4u) ? dp_va(DP_V) : dp_va(DP_V2), 4);
    dp_set(0x004d24f2u, dp_r(2u), 2);
    dp_set(0x0049b024u, dp_r(2u) ? dp_va(DP_P) : 0, 4);
    dp_set(0x0049b030u, dp_r(2u), 1);
    unsigned paged = 0, kind = 0;
    uint32_t present_s = 0;
    for (unsigned i = 0; i < dp_data_count; ++i) {
        if (dp_data[i].va == 0x004d2566u) paged = dp_data[i].v != 0;
        if (dp_data[i].va == 0x004d2537u) kind = dp_data[i].v;
        if (dp_data[i].va == 0x004d2568u) present_s = dp_data[i].v - dp_va(0);
    }
    ++dp_cov_frame[paged];
    if (paged && kind == 1u) ++dp_cov_select[get32(dp_world_start + present_s + 0x30u) == 0u];
    char more[48];
    snprintf(more, sizeof(more), " frame req %ld", (long)req);
    strncat(dp_desc, more, sizeof(dp_desc) - strlen(dp_desc) - 1u);
    dp_eax_free = 1;
    int r = dp_run("frame", k, 0x0045fba4u, NULL, 0, 0);
    dp_eax_free = 0;
    return r;
}

static unsigned dp_cov_row[6];
static int dp_row_case(unsigned k) {
    dp_world_reset();
    dp_display_globals();
    unsigned which = 1u + dp_r(5u);   /* 0x45edf8 is the hook camera_current, run as the original on both sides */
    static const uint32_t vas[6] = {0x0045edf8u, 0x0042d9e8u, 0x00434604u, 0x00434634u, 0x00434928u, 0x00434944u};
    uint32_t a[1] = {0};
    if (which == 1u) { a[0] = dp_va(DP_D); dp_put32(DP_D + 0x14u, dp_r(2u) ? 4u : dp_r(6u)); }
    else if (which == 2u || which == 3u) a[0] = dp_va(DP_H);
    else if (which >= 4u) a[0] = dp_va(DP_BUTTONS + 0x80u * dp_r(5u));
    ++dp_cov_row[which];
    snprintf(dp_desc, sizeof(dp_desc), "row 0x%06lx count %ld", (unsigned long)vas[which], (long)(int32_t)get32(dp_world_start + DP_H + 0x25au));
    return dp_run("rows", k, vas[which], a, which ? 1u : 0u, 0x5a5a0000u + k);
}

/* ---- the flight-key capture 0x45b68c and the cockpit done
 * step 0x433ee8 as the original's code against the rows flight_capture and
 * cockpit_done (the part unpatches the two entries and points the own
 * entries at the rows; memset 0x49681a logs 'z' on both sides). The
 * keyboard id [0x4d249c] (0: none) and its record slot (0: none), the
 * capture byte [0x49eaa5], the skip byte [0x49eb61], the hot-key table's
 * used words (all ten used, one free, random) and the key arrays at
 * random. 0x45b68c's EAX is the caller's EBX with BL = 1 where no
 * registration ran: there only AL is compared. 0x433ee8: [H+0x226] equal
 * to H+0x232 or not, the link [P+4] zero or not, both page modes; EAX and
 * EDX compared (ECX is not returned by the machine call). */
static BYTE dp_capture_saved[3][5];
static es2_sim_entry *dp_capture_entry[3];
static es2_sim_service_fn dp_capture_stub_fn[3];
static void *dp_capture_stub_user[3];
static int dp_capture_swap(int on) {
    static const uint32_t vas[3] = {0x0045b68cu, 0x00433ee8u, 0x0049681au};
    for (unsigned i = 0; i < 3u; ++i) {
        if (on) {
            es2_sim_entry *e = (es2_sim_entry *)es2_sim_dispatch_find(&dp_dispatch, vas[i]);
            if (!e || e->kind != ES2_SIM_SERVICE) return fail("display: capture row missing");
            dp_capture_entry[i] = e;
            dp_capture_stub_fn[i] = e->service;
            dp_capture_stub_user[i] = e->service_user;
            if (i == 2u) { dp_memset_real = e->service; e->service = dp_s_memset; }
            else { e->service = i ? dp_done_real : dp_capture_real; e->service_user = NULL; }
        } else {
            dp_capture_entry[i]->service = dp_capture_stub_fn[i];
            dp_capture_entry[i]->service_user = dp_capture_stub_user[i];
        }
    }
    for (unsigned k = 0; k < sizeof(dp_patched) / sizeof(dp_patched[0]); ++k) {
        if (dp_patched[k].va != vas[0] && dp_patched[k].va != vas[1]) continue;
        if (dp_patch(dp_patched[k].va, NULL, dp_saved[k], 0)) return 1;   /* the original's bytes back */
        if (!on && dp_patch(dp_patched[k].va, dp_patched[k].stub, dp_saved[k], 1)) return 1;
    }
    if (on) return dp_patch(vas[2], (void *)dp_o_memset, dp_capture_saved[2], 1);
    return dp_patch(vas[2], NULL, dp_capture_saved[2], 0);
}
static unsigned dp_cov_cap[6], dp_cov_done[2][3];
static int dp_capture_case(unsigned k) {
    dp_world_reset();
    dp_display_globals();
    dp_edx_check = 1;
    int r;
    if (k & 1u) {
        uint32_t h = DP_H, equal = dp_r(3u) == 0, link = dp_r(2u) ? 0u : (dp_r(2u) ? dp_va(0x2e00u) : random32());
        dp_put32(h + 0x226u, equal ? dp_va(h + 0x232u) : (dp_r(2u) ? dp_va(h + 0x23au) : random32()));
        dp_put32(h + 0x23au, dp_va(DP_P_REC));
        dp_put32(DP_P_REC + 4u, link);
        unsigned paged = 0;
        for (unsigned i = 0; i < dp_data_count; ++i) if (dp_data[i].va == 0x004d2566u) paged = dp_data[i].v != 0;
        ++dp_cov_done[paged][equal ? 0 : link ? 2 : 1];
        snprintf(dp_desc, sizeof(dp_desc), "done equal %lu link %08lx paged %u", (unsigned long)equal, (unsigned long)link, paged);
        r = dp_run("capture", k, 0x00433ee8u, NULL, 0, 0x5a5a0000u + k);
    } else {
        uint32_t id = dp_r(6u) ? 1u + dp_r(12u) : 0u;
        uint32_t record = dp_r(5u) ? dp_va(0x2e40u) + 4u * dp_r(8u) : 0u;
        uint32_t cap = dp_r(8u) ? dp_r(2u) : dp_r(256u), skip = dp_r(3u) ? 0u : 1u + dp_r(255u);
        uint32_t on = dp_r(4u) ? dp_r(2u) : random32();
        if (dp_r(4u) == 0) on = (on & ~0xffu) | cap;   /* equal to the capture byte */
        dp_set(0x004d249cu, id, 4);
        if (id) dp_set(0x004d2468u + 4u * id, record, 4);
        dp_set(0x0049eaa5u, cap, 1);
        dp_set(0x0049eb61u, skip, 1);
        unsigned table = dp_r(4u);
        for (uint32_t i = 0; i < 10u; ++i) {
            uint32_t used = table == 0 ? 1u : table == 1 ? (i != 9u) : (dp_r(3u) == 0 ? 0u : random32() | 1u);
            dp_set(0x006bb8b8u + 12u * i, used, 4);
        }
        for (uint32_t a = 0x004d2394u; a < 0x004d23c4u; a += 4u) dp_set(a, random32() | 0x01010101u, 4);
        int same = (uint8_t)on == (uint8_t)cap, keyboard = id && record, reg = keyboard && (uint8_t)on && !skip;
        dp_eax_mask = (!same && (!keyboard || reg)) ? 0xffffffffu : 0xffu;
        dp_edx_check = 0;   /* 0x45b68c leaves EDX to its callees (0x45bf04's index, 0x477b18's count, memset's) */
        ++dp_cov_cap[same ? 0 : !keyboard ? 1 : reg ? 2 : (uint8_t)on ? 3 : 4];
        if (reg && table == 1u) ++dp_cov_cap[5];
        snprintf(dp_desc, sizeof(dp_desc), "capture on %08lx cap %02lx skip %02lx id %lu record %08lx table %u", (unsigned long)on,
                 (unsigned long)cap, (unsigned long)skip, (unsigned long)id, (unsigned long)record, table);
        uint32_t a[1] = {on};
        r = dp_run("capture", k, 0x0045b68cu, a, 1, 0x5a5a0000u + k);
    }
    dp_eax_mask = 0xffffffffu;
    dp_edx_check = 0;
    return r;
}

static LONG CALLBACK dp_fault(EXCEPTION_POINTERS *fault) {
    DWORD code = fault->ExceptionRecord->ExceptionCode;
    if ((code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_INT_DIVIDE_BY_ZERO) && dp_desc[0])
        fprintf(stderr, "  case parameters: %s\n", dp_desc);
    return EXCEPTION_CONTINUE_SEARCH;
}
static int display_cases(void) {
    current_function = "display";
    AddVectoredExceptionHandler(1, dp_fault);
    es2_sim_identity_memory(&ir_memory);
    es2_sim_rtl_memory(&ir_memory, &su_rtl);
    su_data_pristine = malloc(SU_DATA_SIZE);
    su_snap[0] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    su_snap[1] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    ir_region = ww_alloc(IR_REGION_SIZE);
    lc_stack = VirtualAlloc(NULL, 0x10000u, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    lc_bits = ww_alloc(LC_BITS_SIZE);
    dp_world = VirtualAlloc(NULL, DP_WORLD_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    dp_world_start = malloc(DP_WORLD_SIZE);
    dp_snap[0] = malloc(DP_WORLD_SIZE);
    dp_snap[1] = malloc(DP_WORLD_SIZE);
    if (!su_data_pristine || !su_snap[0] || !su_snap[1] || !ir_region || !lc_stack || !lc_bits || !dp_world || !dp_world_start || !dp_snap[0] ||
        !dp_snap[1])
        return fail("display: memory");
    if (get32(mapped + (SU_COUNTER - 0x400000u))) return fail("display: the counter word is not free");
    su_pristine_take();
    if (dp_dispatch_setup()) return 1;
    es2_sim_load_bind(NULL);
    for (unsigned i = 0; i < sizeof(dp_patched) / sizeof(dp_patched[0]); ++i)
        if (dp_patch(dp_patched[i].va, dp_patched[i].stub, dp_saved[i], 1)) return 1;
    int bad = 0;
    static const struct { const char *name; int (*run)(unsigned); unsigned cases; } parts[] = {
        {"rows", dp_row_case, 600}, {"shield", dp_shield_case, 1500}, {"leave", dp_leave_case, 3000}, {"enter", dp_enter_case, 4000},
        {"frame", dp_frame_case, 2000}, {"capture", dp_capture_case, 2000},
    };
    /* Each part is one test shard's (suite_part_in_shard) and starts from its
     * own seed, so its cases do not depend on the shard layout. The coverage
     * line counts the shard's own parts, and the coverage checks below hold for
     * the parts the shard ran. */
    unsigned owned = 0;
    for (unsigned p = 0; !bad && p < sizeof(parts) / sizeof(parts[0]); ++p) {
        if (!suite_part_in_shard(p)) continue;
        owned |= 1u << p;
        random_state = 0x48c06e21u ^ (p * 0x9e3779b9u);
        if (parts[p].run == dp_capture_case && dp_capture_swap(1)) { bad = 1; break; }
        for (unsigned k = 0; !bad && k < parts[p].cases; ++k) {
            current_case = k; current_case_valid = 1;
            dp_bad = 0;
            dp_desc[0] = 0;
            dp_part = p;
            bad = parts[p].run(k);
        }
        if (parts[p].run == dp_capture_case && dp_capture_swap(0)) bad = 1;
        if (!bad) printf("PASS: display %s: %u cases identical\n", parts[p].name, parts[p].cases);
        if (!bad) {
            printf("DISPLAY %s stub letters (cases):", parts[p].name);
            for (unsigned i = 0; i < 128u; ++i)
                if (dp_letters[p][i]) printf(" %c %u", (char)i, dp_letters[p][i]);
            printf("\n");
        }
    }
    if (!bad) {
        printf("DISPLAY coverage: res 0/1/2 %u/%u/%u, kind 1 %u/%u, paged %u/%u, port flag %u/%u; leave requests -1..7", dp_cov_res[0],
               dp_cov_res[1], dp_cov_res[2], dp_cov_kind[0], dp_cov_kind[1], dp_cov_paged[0], dp_cov_paged[1], dp_cov_port[0], dp_cov_port[1]);
        for (unsigned i = 0; i < 9u; ++i) printf(" %u", dp_cov_leave_req[i]);
        printf(", cool-down %u, border %u, started %u/%u; enter requests -1..6", dp_cov_cool, dp_cov_border, dp_cov_started[0], dp_cov_started[1]);
        for (unsigned i = 0; i < 8u; ++i) printf(" %u", dp_cov_enter_req[i]);
        printf(", sound %u/%u, jolt %u/%u; shield high %u/%u, t2 %u/%u/%u, t3 %u/%u/%u; rows %u %u %u %u %u %u\n", dp_cov_sound[0],
               dp_cov_sound[1], dp_cov_jolt[0], dp_cov_jolt[1], dp_cov_high[0], dp_cov_high[1], dp_cov_t2[0], dp_cov_t2[1], dp_cov_t2[2],
               dp_cov_t3[0], dp_cov_t3[1], dp_cov_t3[2], dp_cov_row[0], dp_cov_row[1], dp_cov_row[2], dp_cov_row[3], dp_cov_row[4],
               dp_cov_row[5]);
        unsigned need[] = {dp_cov_res[0], dp_cov_res[1], dp_cov_res[2], dp_cov_kind[0], dp_cov_kind[1], dp_cov_paged[0], dp_cov_paged[1],
                           dp_cov_port[0], dp_cov_port[1], dp_cov_cool, dp_cov_border, dp_cov_started[0], dp_cov_started[1], dp_cov_sound[0],
                           dp_cov_sound[1], dp_cov_jolt[0], dp_cov_jolt[1], dp_cov_high[0], dp_cov_high[1], dp_cov_t2[0], dp_cov_t2[1],
                           dp_cov_t2[2], dp_cov_t3[0], dp_cov_t3[1], dp_cov_t3[2], dp_cov_frame[0], dp_cov_frame[1],
                           dp_cov_select[0], dp_cov_select[1], dp_cov_cap[0], dp_cov_cap[1], dp_cov_cap[2], dp_cov_cap[3], dp_cov_cap[4],
                           dp_cov_cap[5], dp_cov_done[0][0], dp_cov_done[0][1], dp_cov_done[0][2], dp_cov_done[1][0], dp_cov_done[1][1],
                           dp_cov_done[1][2]};
        printf("DISPLAY HD-03 rest coverage: frame select table %u/absent %u; capture equal %u, no keyboard %u, registered %u, on skipped %u, "
               "off %u, one free entry %u; done equal/no link/link unpaged %u/%u/%u paged %u/%u/%u\n", dp_cov_select[1], dp_cov_select[0],
               dp_cov_cap[0], dp_cov_cap[1], dp_cov_cap[2], dp_cov_cap[3], dp_cov_cap[4], dp_cov_cap[5], dp_cov_done[0][0],
               dp_cov_done[0][1], dp_cov_done[0][2], dp_cov_done[1][0], dp_cov_done[1][1], dp_cov_done[1][2]);
        /* the parts (bit p of the part list) whose cases count each need[] item:
         * the display globals in every part but shield, then leave, enter,
         * shield, frame and capture */
        static const unsigned char from[] = {0x3d, 0x3d, 0x3d, 0x3d, 0x3d, 0x3d, 0x3d, 0x3d, 0x3d, 0x04, 0x04, 0x04, 0x04, 0x08,
                                             0x08, 0x08, 0x08, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x10, 0x10, 0x10,
                                             0x10, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20};
        _Static_assert(sizeof(from) == sizeof(need) / sizeof(need[0]), "one part mask per coverage item");
        for (unsigned i = 0; i < sizeof(need) / sizeof(need[0]); ++i)
            if ((from[i] & owned) && !need[i]) { fprintf(stderr, "FAIL: display coverage item %u never taken\n", i); bad = 1; }
        for (unsigned i = 0; (owned & 0x04u) && i < 9u; ++i) if (!dp_cov_leave_req[i]) { fprintf(stderr, "FAIL: display leave request %d never taken\n", (int)i - 1); bad = 1; }
        for (unsigned i = 0; (owned & 0x08u) && i < 8u; ++i) if (!dp_cov_enter_req[i]) { fprintf(stderr, "FAIL: display enter request %d never taken\n", (int)i - 1); bad = 1; }
        static const char *const letters[6] = {"Bx", "", "BSWYabcosx", "LPSVWfghjklmnoqrsuvx", "CSVUXmq", "z"};
        for (unsigned p = 0; p < 6u; ++p)
            for (const char *l = letters[p]; (owned & 1u << p) && *l; ++l)
                if (!dp_letters[p][(unsigned char)*l]) { fprintf(stderr, "FAIL: display part %u never reached stub %c\n", p, *l); bad = 1; }
        for (unsigned i = 1; (owned & 0x01u) && i < 6u; ++i) if (!dp_cov_row[i]) { fprintf(stderr, "FAIL: display row %u never taken\n", i); bad = 1; }
    }
    current_case_valid = 0;
    for (unsigned i = 0; i < sizeof(dp_patched) / sizeof(dp_patched[0]); ++i) dp_patch(dp_patched[i].va, NULL, dp_saved[i], 0);
    put32(mapped + 0x2cbee4u, 0);
    memcpy(mapped + SU_DATA_RVA, su_data_pristine, SU_DATA_SIZE);
    return bad;
}
