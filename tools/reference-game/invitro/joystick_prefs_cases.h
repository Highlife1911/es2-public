/* The input rest's two rows, original machine code against sim/src/input.c
 * (joystick_prefs.h): the joystick button map 0x45a274 (row joystick_map) and the
 * one-byte prefs store 0x459b80 (row prefs_write). It reuses startup_cases.h's
 * snapshots and loop_cases.h's loop, as hdd_map_leaves_cases.h does.
 *
 * Both sides start from the pristine DATA and fill the same random state:
 * the joystick results 0x6bb6a0/0x6bb6a4 (zero or not), the two button
 * counts 0x6bb3b4/0x6bb548 (small or random), the capability byte
 * 0x6bb3d8, the summary 0x6bb6a8 and the map 0x4d20b5 (random bytes before
 * the call). Then 1..8 calls of the two rows with random entry EAX, EDX
 * and ECX; the map's set is 0, 1, 2 or a random dword, the store's index a
 * random dword. The original side calls the rows through an own thunk that
 * loads and stores the three registers; the joystick summary 0x477904 runs
 * as the original's machine code there (the own side: input_layer.h's row). The
 * callees are recording stubs on both sides: prefs_store 0x459b94 (a hook;
 * it logs its entry registers, the count, whether EAX is the list pointer
 * and the list's first dword, and leaves seeded EAX, EDX and ECX) and
 * _memset (the original's import slot; the own side's RTL row wrapped),
 * both over core/src/rtl.c's model with the registers CW3220 leaves.
 * Compared: EAX, EDX and ECX of every call, DATA 0x497000..0x6cc000 and the
 * heap, the stub log. Coverage: per-path counters; every one must be
 * non-zero. The map's clearing path (0x45a288..0x45a295) is unreachable:
 * the summary's word is 1 or 2 (0x477917..0x477925), never 0. */
#pragma once
/* The headers this suite builds on (sim_main.c). */
#include "init_records_cases.h"
#include "startup_cases.h"
#include "loop_cases.h"
#include "es2/sim/input.h"

static es2_sim_entry hf_entries[4096];
static es2_sim_dispatch hf_dispatch;
static es2_sim_input hf_input;
static int hf_bad;
static unsigned hf_side;

/* ---- the original side's register thunk: pushes the arguments, enters
 * with EAX/EDX/ECX from in[3], stores EAX/EDX/ECX into out[3]. */
extern void hf_call(uint32_t target, const uint32_t *args, uint32_t count, const uint32_t *in, uint32_t *out);
__asm__(".text\n.globl _hf_call\n_hf_call:\n\tpushl %ebp\n\tmovl %esp, %ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n"
        "\tmovl 16(%ebp), %ecx\n\tmovl 12(%ebp), %esi\n"
        "1:\n\ttestl %ecx, %ecx\n\tjz 2f\n\tdecl %ecx\n\tpushl (%esi,%ecx,4)\n\tjmp 1b\n"
        "2:\n\tmovl 20(%ebp), %ebx\n\tmovl (%ebx), %eax\n\tmovl 4(%ebx), %edx\n\tmovl 8(%ebx), %ecx\n"
        "\tcall *8(%ebp)\n"
        "\tmovl 24(%ebp), %ebx\n\tmovl %eax, (%ebx)\n\tmovl %edx, 4(%ebx)\n\tmovl %ecx, 8(%ebx)\n"
        "\tleal -12(%ebp), %esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n");

/* ---- the stub log of a side */
static uint64_t hf_hash[2];
static unsigned hf_count[2], hf_letters[2][128];
static char hf_text[2][4096];
static size_t hf_len[2];
static void hf_note(char letter, const uint32_t in[3], const uint32_t v4[4]) {
    uint64_t h = hf_hash[hf_side];
    uint32_t v[8] = {(uint32_t)letter, in[0], in[1], in[2], v4[0], v4[1], v4[2], v4[3]};
    for (unsigned i = 0; i < 8u; ++i) { h ^= v[i]; h *= 0x100000001b3ull; }
    hf_hash[hf_side] = h;
    ++hf_count[hf_side];
    ++hf_letters[hf_side][(unsigned char)letter & 127u];
    if (hf_len[hf_side] + 100u < sizeof(hf_text[0]))
        hf_len[hf_side] += (size_t)sprintf(hf_text[hf_side] + hf_len[hf_side], "%c[%lx %lx %lx](%lx,%lx,%lx,%lx) ", letter,
                                           (unsigned long)v[1], (unsigned long)v[2], (unsigned long)v[3], (unsigned long)v[4],
                                           (unsigned long)v[5], (unsigned long)v[6], (unsigned long)v[7]);
}
static uint8_t hf_get8(uint32_t a) { return *(BYTE *)(uintptr_t)a; }
static void hf_put8(uint32_t a, uint8_t v) { *(BYTE *)(uintptr_t)a = v; }
static void hf_put32(uint32_t a, uint32_t v) { put32((BYTE *)(uintptr_t)a, v); }

/* ---- the stubs' bodies, one for both sides */
static uint32_t hf_answer_seed;
static uint32_t hf_answer(void) {
    hf_answer_seed = hf_answer_seed * 1103515245u + 12345u;
    return hf_answer_seed ^ (hf_answer_seed >> 13);
}
/* prefs_store(count, list): the EAX at entry is logged as "is the list"
 * (the list is the caller's argument slot, a different address per side). */
static void hf_store(const uint32_t in[3], const uint32_t *s, uint32_t out[3]) {
    const uint32_t entry[3] = {in[0] == s[1] ? 1u : 0u, in[1], in[2]};
    const uint32_t v[4] = {s[0], get32((BYTE *)(uintptr_t)s[1]), 0, 0};
    hf_note('p', entry, v);
    out[0] = hf_answer(); out[1] = hf_answer(); out[2] = hf_answer();
}
/* _memset(d, v, n): rtl.c's model (the registers CW3220 leaves). */
static void hf_memset(const uint32_t in[3], const uint32_t *s, uint32_t out[3]) {
    /* Its entry registers are not observable (rtl.c: EAX = d, ECX = 0, EDX = n)
     * and are not logged. */
    const uint32_t v[4] = {s[0], s[1], s[2], 0}, none[3] = {0, 0, 0};
    hf_note('S', none, v);
    es2_rtl_regs r = {in[0], in[2], in[1]};
    if (es2_rtl_call(ES2_RTL_MEMSET, &su_rtl, (uint32_t)(uintptr_t)s, &r) != ES2_RTL_DONE) hf_bad = 1;
    out[0] = r.eax; out[1] = r.edx; out[2] = r.ecx;
}

/* ---- the original side's stubs: naked entries that keep the registers */
uint32_t hf_reg_in[3], hf_reg_out[3];
void __cdecl hf_stub_c(uint32_t letter, const uint32_t *args) {
    if (letter == 'p') hf_store(hf_reg_in, args, hf_reg_out);
    else hf_memset(hf_reg_in, args, hf_reg_out);
}
#define HF_STUB(name, letter)                                                                                                      \
    extern void name(void);                                                                                                        \
    __asm__(".text\n.globl _" #name "\n_" #name ":\n\tmovl %eax, _hf_reg_in\n\tmovl %edx, _hf_reg_in+4\n\tmovl %ecx, _hf_reg_in+8\n" \
            "\tleal 4(%esp), %eax\n\tpushl %eax\n\tpushl $" #letter "\n\tcall _hf_stub_c\n\taddl $8, %esp\n"                     \
            "\tmovl _hf_reg_out, %eax\n\tmovl _hf_reg_out+4, %edx\n\tmovl _hf_reg_out+8, %ecx\n\tret\n");
HF_STUB(hf_o_store, 112)   /* 'p' */
HF_STUB(hf_o_memset, 83)   /* 'S' */

static BYTE hf_saved[5];
static int hf_patch(uint32_t va, void *target, int install) {
    BYTE *site = mapped + (va - 0x400000u);
    DWORD previous;
    if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &previous)) return fail("joystick prefs: stub protection");
    if (install) { memcpy(hf_saved, site, 5); site[0] = 0xe9; put32(site + 1, (uint32_t)(uintptr_t)target - (uint32_t)(uintptr_t)(site + 5)); }
    else memcpy(site, hf_saved, 5);
    if (!VirtualProtect(site, 5, previous, &previous)) return fail("joystick prefs: stub restore protection");
    FlushInstructionCache(GetCurrentProcess(), site, 5);
    return 0;
}

/* ---- the own side */
static void hf_s_store(es2_sim_service_call *call, void *user) {
    (void)user;
    const uint32_t in[3] = {call->eax, call->edx, call->ecx};
    uint32_t out[3];
    hf_store(in, call->slots, out);
    call->eax = out[0]; call->edx = out[1]; call->ecx = out[2];
}
static void hf_s_memset(es2_sim_service_call *call, void *user) {
    (void)user;
    const uint32_t in[3] = {call->eax, call->edx, call->ecx};
    uint32_t out[3];
    hf_memset(in, call->slots, out);
    call->eax = out[0]; call->edx = out[1]; call->ecx = out[2];
}
static void hf_refuse(const es2_sim_refusal *r, void *user) {
    (void)user;
    fprintf(stderr, "FAIL: own joystick prefs: refused call to 0x%08lx (%s): %s\n", (unsigned long)r->va, r->name ? r->name : "?",
            r->reason ? r->reason : "");
    hf_bad = 1;
}
static int hf_dispatch_setup(void) {
    es2_sim_dispatch_init(&hf_dispatch, hf_entries, 4096, hf_refuse, NULL);
    static es2_sim_services s;
    s.memory = &ir_memory;
    s.heap = &ir_heap;
    s.vt = &lc_vt;
    s.fs0 = NULL;
    if (es2_sim_register_services(&hf_dispatch, &s)) return fail("joystick prefs: dispatch");
    if (es2_sim_dispatch_add_service(&hf_dispatch, 0x00459b94u, "joystick prefs prefs_store stub", hf_s_store, NULL, 0, 2))
        return fail("joystick prefs: dispatch (stub)");
    const char *error = NULL;
    uint32_t bad = 0;
    if (es2_sim_dispatch_seal(&hf_dispatch, &error, &bad)) return fail("joystick prefs: dispatch seal");
    /* The RTL row: replaced by the recording model after the seal. */
    es2_sim_entry *e = (es2_sim_entry *)es2_sim_dispatch_find(&hf_dispatch, 0x0049681au);
    if (!e || e->kind != ES2_SIM_SERVICE) return fail("joystick prefs: _memset row missing");
    e->service = hf_s_memset;
    e->service_user = NULL;
    return 0;
}

/* ---- the random state (DATA) */
static uint32_t hf_seed;
static uint32_t hf_rand(uint32_t n) {
    hf_seed = hf_seed * 1103515245u + 12345u;
    return (hf_seed >> 8) % n;
}
static uint32_t hf_count_value(void) { uint32_t r = hf_rand(4u); return r < 3u ? hf_rand(10u) : hf_rand(0x10000u) << 16 | hf_rand(0x10000u); }
static void hf_build(void) {
    hf_put32(0x006bb6a0u, hf_rand(3u) ? 0u : hf_rand(200u));
    hf_put32(0x006bb6a4u, hf_rand(2u) ? 0u : 1u + hf_rand(200u));
    hf_put32(0x006bb3b4u, hf_count_value());
    hf_put32(0x006bb548u, hf_count_value());
    hf_put8(0x006bb3d8u, (uint8_t)hf_rand(256u));
    for (uint32_t k = 0; k < 8u; ++k) hf_put8(0x006bb6a8u + k, (uint8_t)hf_rand(256u));
    for (uint32_t k = 0; k < 12u; ++k) hf_put8(0x004d20b5u + k, (uint8_t)hf_rand(256u));
}

/* ---- the rows */
enum { HF_MAP, HF_WRITE, HF_OPS };
static const struct { uint32_t va; const char *name; } hf_ops[HF_OPS] = {
    {0x0045a274u, "joystick map"}, {0x00459b80u, "prefs write"},
};
typedef struct hf_op { unsigned op; uint32_t a[1], in[3]; } hf_op;
static void hf_args(hf_op *o) {
    if (o->op == HF_MAP) {
        uint32_t r = random32() % 8u;
        o->a[0] = r < 3u ? 0u : r < 6u ? 1u : r < 7u ? 2u : random32();
    } else {
        o->a[0] = random32() % 2u ? random32() % 0x36u : random32();
    }
    for (unsigned k = 0; k < 3u; ++k) o->in[k] = random32();
}

/* ---- coverage */
enum { HFC_SET0, HFC_SET1, HFC_SET_OTHER, HFC_WORD1, HFC_WORD2, HFC_WRITE, HFC_COUNT };
static const char *const hf_cv_names[HFC_COUNT] = {"set 0", "set 1", "set other", "summary word 1", "summary word 2", "write"};
static unsigned hf_cv[HFC_COUNT];
static void hf_cover(const hf_op *o) {
    if (o->op == HF_WRITE) { ++hf_cv[HFC_WRITE]; return; }
    ++hf_cv[o->a[0] == 0 ? HFC_SET0 : o->a[0] == 1u ? HFC_SET1 : HFC_SET_OTHER];
    ++hf_cv[hf_get8(0x006bb6a8u) == 1u ? HFC_WORD1 : HFC_WORD2];
}

static void hf_side_begin(unsigned side, uint32_t seed, uint32_t answers) {
    lc_reset(side, 47, 0x01000000u, 1);
    lc_loop.dispatch = &hf_dispatch;
    hf_side = side;
    hf_hash[side] = 0xcbf29ce484222325ull;
    hf_count[side] = 0;
    memset(hf_letters[side], 0, sizeof(hf_letters[side]));
    hf_len[side] = 0;
    hf_text[side][0] = 0;
    hf_answer_seed = answers;
    hf_seed = seed;
    hf_build();
    if (side) {
        es2_sim_input_headless(&hf_input, &lc_loop, NULL, 640, 480);
        es2_sim_input_bind(&hf_input);
    }
}
static unsigned hf_calls[HF_OPS];
static int hf_case(unsigned k) {
    unsigned ops = 1u + random32() % 8u;
    static hf_op list[8];
    uint32_t seed = random32(), answers = random32();
    static uint32_t reg[2][8][3];
    for (unsigned i = 0; i < ops; ++i) {
        list[i].op = random32() % HF_OPS;
        hf_args(&list[i]);
    }
    for (unsigned side = 0; side < 2u; ++side) {
        hf_side_begin(side, seed, answers);
        for (unsigned i = 0; i < ops && !hf_bad && !lc_bad; ++i) {
            hf_op *o = &list[i];
            if (side == 0) {
                hf_call((uint32_t)(uintptr_t)(mapped + (hf_ops[o->op].va - 0x400000u)), o->a, 1, o->in, reg[0][i]);
                hf_cover(o);
                ++hf_calls[o->op];
            } else {
                es2_sim_result r;
                memset(&r, 0, sizeof(r));
                if (es2_sim_call(&hf_dispatch, hf_ops[o->op].va, o->a, 1, o->in, &r)) hf_bad = 1;
                reg[1][i][0] = r.eax; reg[1][i][1] = r.edx; reg[1][i][2] = r.ecx;
            }
        }
        if (hf_bad || lc_bad) { fprintf(stderr, "FAIL: joystick prefs case %u: a call failed (side %u)\n", k, side); return 1; }
        su_take(side);
    }
    for (unsigned i = 0; i < ops; ++i)
        if (memcmp(reg[0][i], reg[1][i], sizeof(reg[0][i]))) {
            fprintf(stderr, "FAIL: joystick prefs case %u: %s (call %u) EAX %08lx / %08lx, EDX %08lx / %08lx, ECX %08lx / %08lx\n", k,
                    hf_ops[list[i].op].name, i, (unsigned long)reg[0][i][0], (unsigned long)reg[1][i][0], (unsigned long)reg[0][i][1],
                    (unsigned long)reg[1][i][1], (unsigned long)reg[0][i][2], (unsigned long)reg[1][i][2]);
            return 1;
        }
    if (su_compare("joystick prefs", k)) {
        fprintf(stderr, "  rows:");
        for (unsigned i = 0; i < ops; ++i) fprintf(stderr, " %s", hf_ops[list[i].op].name);
        fprintf(stderr, "\n");
        return 1;
    }
    if (hf_hash[0] != hf_hash[1] || hf_count[0] != hf_count[1]) {
        fprintf(stderr, "FAIL: joystick prefs case %u: stub calls differ (%u / %u)\n  original: %s\n  own:      %s\n", k, hf_count[0], hf_count[1],
                hf_text[0], hf_text[1]);
        return 1;
    }
    return 0;
}

static int joystick_prefs_cases(void) {
    current_function = "joystick prefs";
    es2_sim_identity_memory(&ir_memory);
    es2_sim_rtl_memory(&ir_memory, &su_rtl);
    su_data_pristine = malloc(SU_DATA_SIZE);
    su_snap[0] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    su_snap[1] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    ir_region = ww_alloc(IR_REGION_SIZE);
    lc_stack = VirtualAlloc(NULL, 0x10000u, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    lc_bits = ww_alloc(LC_BITS_SIZE);
    if (!su_data_pristine || !su_snap[0] || !su_snap[1] || !ir_region || !lc_stack || !lc_bits) return fail("joystick prefs: memory");
    su_pristine_take();
    if (!ir_patch_import("cw3220.dll", "_memset", (void *)hf_o_memset)) return fail("joystick prefs: import slot not found");
    if (hf_patch(0x00459b94u, (void *)hf_o_store, 1)) return 1;
    if (hf_dispatch_setup()) return 1;
    int bad = 0;
    const unsigned cases = 3000;
    for (unsigned k = 0; !bad && k < cases; ++k) {
        current_case = k; current_case_valid = 1;
        bad = hf_case(k);
    }
    if (!bad) {
        printf("PASS: joystick prefs: %u cases identical\n", cases);
        printf("joystick prefs rows called:");
        for (unsigned i = 0; i < HF_OPS; ++i) printf(" %s %u%s", hf_ops[i].name, hf_calls[i], i + 1u < HF_OPS ? "," : "\n");
        printf("joystick prefs coverage:");
        for (unsigned i = 0; i < HFC_COUNT; ++i) printf(" %s %u%s", hf_cv_names[i], hf_cv[i], i + 1u < HFC_COUNT ? "," : "\n");
        for (unsigned i = 0; i < HFC_COUNT; ++i)
            if (!hf_cv[i]) { fprintf(stderr, "FAIL: joystick prefs: path '%s' never taken\n", hf_cv_names[i]); bad = 1; }
    }
    current_case_valid = 0;
    hf_patch(0x00459b94u, NULL, 0);
    put32(mapped + 0x2cbee4u, 0);
    memcpy(mapped + SU_DATA_RVA, su_data_pristine, SU_DATA_SIZE);
    return bad;
}
