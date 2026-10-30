/* The pod gauge creators (weapon slot +0x64 0x40e3bc, 0x40e3e4, 0x40e40c,
 * 0x40e434, 0x40e45c) with the factory 0x4321d4 and the pod gauge
 * constructors 0x44154c, 0x4419c0, 0x441a5c, original machine code against
 * sim/src/hud_gauges.c (target :differential_pod_gauges_test).
 * Built on gauges_cases.h: its recorders, world, side set-up and comparison
 * (gg_record, gg_side, gg_run). Identifiers: pg_/PG_.
 *
 * Every case builds a weapon W (kind word W+0x77, slot byte [[W+0x18]+7], the
 * name through W+0x20 or the type W+0x1c), the panel H [0x49b088] with its
 * root [H], the layout [0x49b0a8] and the shifts in an identity-mapped world,
 * then calls one of the five creators on both sides: side 0 the original's
 * code (factory, constructors, the gauge base 0x440834, the toggle 0x442204
 * and the root add 0x452c6c all original), side 1 the row through the
 * dispatch of the sim's real services. Recorders on both sides: gauges_cases.h's
 * (new, new[], path, load) and the label init 0x4387ac, the label placement
 * 0x438884 and the bar 0x4395c4. The RTL imports run the sim's RTL on both
 * sides. The allocations of G, of the damage animation and of the toggle
 * fail in some cases; the labels and the bar never do (the original writes
 * through them, the own code refuses). Kinds other than 0x12, 0x1d..0x20
 * are not compared: the original files an unset local, the own code refuses
 * (hud.md). Compared as in gauges_cases.h, EAX included. */
#pragma once
/* The headers this suite builds on (sim_main.c). */
#include "gauges_cases.h"

#define PG_W 0x0000u      /* the weapon, 0x100 */
#define PG_S 0x0100u      /* [W+0x18]: byte +7 the slot */
#define PG_T 0x0120u      /* the type [W+0x1c]: +0x52 the name */
#define PG_A 0x0180u      /* [W+0x20]: word +0 0 takes the table name, word +2 its index */
#define PG_N1 0x01a0u     /* the type's name */
#define PG_N2 0x01c0u     /* the table's name */
#define PG_SUF 0x01e0u    /* the suffix [0x4d1348] */
#define PG_H 0x0200u      /* the panel, 0x220 */
#define PG_L 0x0440u      /* the layout [0x49b0a8], 0x180 */
#define PG_ROOT 0x0600u   /* the root [H], 0x2a0 */

static uint32_t pg_record(unsigned kind, const uint32_t *a) {
    unsigned side = gg_side_now, k = gg_calls[side][kind]++;
    switch (kind) {
    case 17:   /* 0x4387ac(L, font, n) */
        gg_note(17, a, 3);
        return 0x17000000u ^ k;
    case 18:   /* 0x438884(L, offset, rect, flags) */
        gg_note(18, a, 1);
        gg_mix(a[3]);
        gg_bytes(a[1], 8u);
        gg_bytes(a[2], 16u);
        return 0x18000000u ^ k;
    case 19:   /* 0x4395c4(bar, rect, n) */
        gg_note(19, a, 1);
        gg_bytes(a[1], 16u);
        gg_mix(a[2]);
        return 0x19000000u ^ k;
    default:
        --gg_calls[side][kind];
        return gg_record(kind, a);
    }
}
static uint32_t __cdecl pg_o17(uint32_t a, uint32_t b, uint32_t c) { const uint32_t s[3] = {a, b, c}; return pg_record(17, s); }
static uint32_t __cdecl pg_o18(uint32_t a, uint32_t b, uint32_t c, uint32_t d) { const uint32_t s[4] = {a, b, c, d}; return pg_record(18, s); }
static uint32_t __cdecl pg_o19(uint32_t a, uint32_t b, uint32_t c) { const uint32_t s[3] = {a, b, c}; return pg_record(19, s); }
static void pg_s_stub(es2_sim_service_call *sc, void *user) { sc->eax = pg_record((unsigned)(uintptr_t)user, sc->slots); }
static const struct { uint32_t va; void *stub; unsigned kind, slots; } pg_stubs[] = {
    {0x0047749cu, (void *)gg_o1, 1, 1}, {0x004774f0u, (void *)gg_o2, 2, 1}, {0x00492becu, (void *)gg_o10, 10, 2},
    {0x0045cdf4u, (void *)gg_o11, 11, 1}, {0x004387acu, (void *)pg_o17, 17, 3}, {0x00438884u, (void *)pg_o18, 18, 4},
    {0x004395c4u, (void *)pg_o19, 19, 3},
};
#define PG_STUBS (sizeof(pg_stubs) / sizeof(pg_stubs[0]))
static BYTE pg_saved[PG_STUBS][5];
static uint32_t __cdecl pg_strncat(uint32_t d, uint32_t s, uint32_t n) { uint32_t a[3] = {d, s, n}; return su_rtl_call(ES2_RTL_STRNCAT, a); }
static uint32_t __cdecl pg_strncpy(uint32_t d, uint32_t s, uint32_t n) { uint32_t a[3] = {d, s, n}; return su_rtl_call(ES2_RTL_STRNCPY, a); }
/* As gg_dispatch_setup, with this suite's recorders. */
static int pg_dispatch_setup(void) {
    es2_sim_dispatch_init(&gg_dispatch, gg_entries, 4096, gg_refuse, NULL);
    static es2_sim_services s;
    s.memory = &ir_memory;
    s.heap = &ir_heap;
    s.vt = &lc_vt;
    s.fs0 = NULL;
    if (es2_sim_register_services(&gg_dispatch, &s)) return fail("pod gauges: dispatch");
    static int replace[PG_STUBS];
    for (unsigned i = 0; i < PG_STUBS; ++i) {
        replace[i] = gg_registered(pg_stubs[i].va);
        if (!replace[i] && es2_sim_dispatch_add_service(&gg_dispatch, pg_stubs[i].va, "pod gauges recorder", pg_s_stub,
                                                        (void *)(uintptr_t)pg_stubs[i].kind, 0, (uint8_t)pg_stubs[i].slots))
            return fail("pod gauges: dispatch");
    }
    const char *error = NULL;
    uint32_t bad = 0;
    if (es2_sim_dispatch_seal(&gg_dispatch, &error, &bad)) return fail("pod gauges: dispatch seal");
    for (unsigned i = 0; i < PG_STUBS; ++i) {
        if (!replace[i]) continue;
        es2_sim_entry *e = (es2_sim_entry *)es2_sim_dispatch_find(&gg_dispatch, pg_stubs[i].va);
        if (!e) return fail("pod gauges: a replaced row is missing");
        e->kind = ES2_SIM_SERVICE; e->name = "pod gauges recorder"; e->service = pg_s_stub;
        e->service_user = (void *)(uintptr_t)pg_stubs[i].kind; e->pop_bytes = 0; e->stack_slots = (uint8_t)pg_stubs[i].slots;
    }
    return 0;
}

static unsigned pg_cov[22];
static void pg_text(uint32_t offset, unsigned max) {
    unsigned n = gg_rand(max + 1u);
    for (unsigned i = 0; i < n; ++i) gg_w8(offset + i, 'A' + gg_rand(26u));
    gg_w8(offset + n, 0);
}
static int pg_case(unsigned k) {
    static const uint32_t vas[] = {0x0040e3bcu, 0x0040e3e4u, 0x0040e40cu, 0x0040e434u, 0x0040e45cu};
    static const uint16_t kinds[] = {0x12u, 0x1du, 0x1eu, 0x1fu, 0x20u};
    memset(gg_world_start, 0, GG_WORLD_SIZE);
    gg_preset_count = 0;
    random_bytes(gg_world_start, GG_WORLD_SIZE);
    unsigned v = gg_rand(5u), kind = gg_rand(5u);
    gg_w16(PG_W + 0x77u, kinds[kind]);
    gg_w32(PG_W + 0x18u, gg_at(PG_S));
    uint32_t slot = gg_rand(8u) ? gg_rand(10u) : 0xffu;
    gg_w8(PG_S + 7u, slot);
    gg_w32(PG_W + 0x1cu, gg_at(PG_T));
    gg_w32(PG_T + 0x52u, gg_at(PG_N1));
    pg_text(PG_N1, 14u);
    pg_text(PG_N2, 14u);
    pg_text(PG_SUF, 6u);
    int table = 0;
    if (gg_rand(2u)) {
        gg_w32(PG_W + 0x20u, gg_at(PG_A));
        table = gg_rand(2u);
        uint32_t index = gg_rand(4u);
        gg_w16(PG_A, table ? 0u : 1u + gg_rand(0xffffu));
        gg_w16(PG_A + 2u, index);
        gg_set32(0x004989c8u + 4u * index, gg_at(PG_N2));
    } else gg_w32(PG_W + 0x20u, 0);
    /* The panel: the element count, the slots (some free), the first element. */
    gg_w32(PG_H, gg_at(PG_ROOT));
    gg_w32(PG_H + 0x6cu, gg_rand(18u));
    int full = gg_rand(3u) == 0;
    for (uint32_t i = 0; i < 10u; ++i) gg_w32(PG_H + 0x70u + 4u * i, full || gg_rand(2u) ? 1u + random32() : 0u);
    gg_w32(PG_H + 0x98u, random32());
    gg_w32(PG_H + 0x1d8u, gg_rand(2u) ? 0u : random32());
    gg_w16(PG_ROOT + 0x254u, gg_rand(12u));
    gg_set32(0x0049b088u, gg_at(PG_H));
    gg_set32(0x0049b0a8u, gg_at(PG_L));
    gg_set32(0x004d1348u, gg_at(PG_SUF));
    gg_set32(0x0049b0d8u, random32());
    gg_set32(0x0049b0d4u, random32());
    static const unsigned shifts[] = {0, 0, 1, 1, 2, 3, 31};
    for (uint32_t i = 0; i < 2u; ++i) {   /* the words 0x4d253a, 0x4d253c: the shift in bits 0..4 */
        gg_set8(0x004d253au + 2u * i, (gg_rand(2u) ? random32() & 0xe0u : 0u) | shifts[gg_rand(7u)]);
        gg_set8(0x004d253bu + 2u * i, gg_rand(2u) ? random32() : 0u);
    }
    int fresh_p = gg_rand(4u) == 0, fresh_d = gg_rand(4u) == 0;
    gg_set32(0x0049c4d0u, fresh_p ? 0u : 1u + random32() % 0xfffffu);
    gg_set32(0x0049c4d4u, fresh_d ? 0u : 1u + random32() % 0xfffffu);
    gg_set8(0x004d256cu, gg_rand(2u));
    gg_load_value = random32() | 1u;
    gg_new_fail_mask = gg_rand(3u) ? 0u : gg_rand(8u);
    gg_op_set(vas[v], 1u, 1, gg_at(PG_W), 0, 0, 0);
    /* The flag byte the constructor reads: the layout rect's +0x50 (L+0x164+0x50). */
    uint32_t flag_src = PG_L + 0x164u + 0x50u;
    gg_w8(flag_src, gg_rand(2u) ? 0u : (BYTE)random32());
    uint32_t free_before = 0;
    for (uint32_t i = 0; i < 10u; ++i) if (!gg_r32(PG_H + 0x70u + 4u * i)) { free_before = 1; break; }
    int bad = gg_run("pod gauge creator", k);
    if (bad) return 1;
    ++pg_cov[v];
    if (gg_new_fail_mask & 1u) ++pg_cov[5];
    else {
        ++pg_cov[6 + kind];
        if (gg_new_fail_mask & 2u) ++pg_cov[11];
        if (gg_new_fail_mask & 4u) ++pg_cov[12];
    }
    if (slot == 0xffu) ++pg_cov[free_before ? 13 : 14];
    if (slot < 8u && (gg_r8(flag_src) >> slot) & 1u) ++pg_cov[15];
    if (gg_r32(PG_W + 0x20u)) ++pg_cov[table ? 16 : 17];
    else ++pg_cov[18];
    if (fresh_p) ++pg_cov[19];
    if (fresh_d) ++pg_cov[20];
    if (!gg_r32(PG_H + 0x1d8u)) ++pg_cov[21];
    return 0;
}

static int pod_gauges_cases(void) {
    current_function = "pod gauges";
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
        return fail("pod gauges: memory");
    if (get32(mapped + (SU_COUNTER - 0x400000u))) return fail("pod gauges: the counter word is not free");
    su_pristine_take();
    static const struct { const char *dll, *name; void *stub; } stubs[] = {
        {"cw3220.dll", "_calloc", (void *)ir_calloc}, {"cw3220.dll", "_free", (void *)ir_free},
        {"cw3220.dll", "_memset", (void *)ir_memset}, {"cw3220.dll", "_memmove", (void *)ir_memmove},
        {"cw3220.dll", "_strlen", (void *)su_strlen}, {"cw3220.dll", "_strncat", (void *)pg_strncat},
        {"cw3220.dll", "_strncpy", (void *)pg_strncpy},
    };
    for (unsigned i = 0; i < sizeof(stubs) / sizeof(stubs[0]); ++i)
        if (!ir_patch_import(stubs[i].dll, stubs[i].name, stubs[i].stub)) {
            fprintf(stderr, "import %s!%s\n", stubs[i].dll, stubs[i].name);
            return fail("pod gauges: import slot not found");
        }
    if (pg_dispatch_setup()) return 1;
    for (unsigned i = 0; i < PG_STUBS; ++i) if (gg_patch(pg_stubs[i].va, pg_stubs[i].stub, pg_saved[i], 1)) return 1;
    int bad = 0;
    const unsigned cases = 3000;
    for (unsigned k = 0; !bad && k < cases; ++k) {
        current_case = k; current_case_valid = 1;
        bad = pg_case(k);
    }
    if (!bad) printf("PASS: pod gauges creators: %u cases identical\n", cases);
    for (unsigned i = 0; i < PG_STUBS; ++i) if (gg_patch(pg_stubs[i].va, pg_stubs[i].stub, pg_saved[i], 0)) bad = 1;
    if (!bad) {
        printf("POD GAUGES coverage:");
        for (unsigned i = 0; i < sizeof(pg_cov) / sizeof(pg_cov[0]); ++i) printf(" %u", pg_cov[i]);
        printf("; NULL allocations %u; calls: new %u label init %u label place %u bar %u path %u load %u\n", gg_new_nulls, gg_kinds[1],
               gg_kinds[17], gg_kinds[18], gg_kinds[19], gg_kinds[10], gg_kinds[11]);
    }
    for (unsigned i = 0; !bad && i < sizeof(pg_cov) / sizeof(pg_cov[0]); ++i)
        if (!pg_cov[i]) {
            fprintf(stderr, "pod gauges coverage slot %u\n", i);
            bad = fail("pod gauges: a path was not taken");
        }
    current_case_valid = 0;
    put32(mapped + 0x2cbee4u, 0);
    memcpy(mapped + SU_DATA_RVA, su_data_pristine, SU_DATA_SIZE);
    return bad;
}
