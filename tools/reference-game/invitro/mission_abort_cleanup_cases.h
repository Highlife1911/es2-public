/* The rows of sim/dispatch/services/mission_abort_cleanup.h, original machine code against
 * sim/src/loop.c (target :differential_mission_abort_cleanup_test): the mission
 * start's abort-notify clean-up 0x461304 (row abort_cleanup) and the remap
 * table's release 0x46a4ac (row remap_release). It reuses startup_cases.h's
 * snapshots and loop_cases.h's loop, as joystick_prefs_cases.h does. Identifiers use
 * hn_/HN_.
 *
 * Both sides start from the pristine DATA and fill the same random state:
 * the tape and dump streams 0x4d24e2/0x4d24de (zero in a third each), the
 * remap flag word 0x4a0918 (zero in half), the block 0x6b4f20, the dword
 * 0x6b4f12 and the exit code 0x4d27b8 (random). Then 1..4 calls of the two
 * rows with random entry EAX, EDX and ECX. The original side calls them
 * through an own thunk that loads and stores the three registers; 0x461304
 * reaches 0x46a4ac as the original's machine code there (the own side: the
 * row). The callees are recording stubs on both sides: the sound close
 * 0x462734 and operator delete 0x477548 (code patches on the original side;
 * the own side's rows replaced after the seal) and _fclose (the original's
 * import slot; the own side's row file_fclose replaced). A stub logs its
 * letter, its entry EAX, EDX and ECX and its argument, leaves seeded EAX,
 * EDX and ECX, and (fclose and delete, in half of the calls) writes a
 * seeded value into its argument slot, so the caller's `pop ecx` shows.
 * Compared: EAX, EDX and ECX of every call, DATA 0x497000..0x6cc000 and the
 * heap, the stub log. Coverage: per-path counters; every one must be
 * non-zero. */
#pragma once
/* The headers this suite builds on (sim_main.c). */
#include "init_records_cases.h"
#include "startup_cases.h"
#include "loop_cases.h"

static es2_sim_entry hn_entries[4096];
static es2_sim_dispatch hn_dispatch;
static int hn_bad;
static unsigned hn_side;

/* ---- the original side's register thunk: pushes the arguments, enters
 * with EAX/EDX/ECX from in[3], stores EAX/EDX/ECX into out[3]. */
extern void hn_call(uint32_t target, const uint32_t *args, uint32_t count, const uint32_t *in, uint32_t *out);
__asm__(".text\n.globl _hn_call\n_hn_call:\n\tpushl %ebp\n\tmovl %esp, %ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n"
        "\tmovl 16(%ebp), %ecx\n\tmovl 12(%ebp), %esi\n"
        "1:\n\ttestl %ecx, %ecx\n\tjz 2f\n\tdecl %ecx\n\tpushl (%esi,%ecx,4)\n\tjmp 1b\n"
        "2:\n\tmovl 20(%ebp), %ebx\n\tmovl (%ebx), %eax\n\tmovl 4(%ebx), %edx\n\tmovl 8(%ebx), %ecx\n"
        "\tcall *8(%ebp)\n"
        "\tmovl 24(%ebp), %ebx\n\tmovl %eax, (%ebx)\n\tmovl %edx, 4(%ebx)\n\tmovl %ecx, 8(%ebx)\n"
        "\tleal -12(%ebp), %esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n");

/* ---- the stub log of a side */
static uint64_t hn_hash[2];
static unsigned hn_count[2];
static char hn_text[2][4096];
static size_t hn_len[2];
static void hn_note(char letter, const uint32_t in[3], uint32_t arg) {
    uint64_t h = hn_hash[hn_side];
    uint32_t v[5] = {(uint32_t)letter, in[0], in[1], in[2], arg};
    for (unsigned i = 0; i < 5u; ++i) { h ^= v[i]; h *= 0x100000001b3ull; }
    hn_hash[hn_side] = h;
    ++hn_count[hn_side];
    if (hn_len[hn_side] + 80u < sizeof(hn_text[0]))
        hn_len[hn_side] += (size_t)sprintf(hn_text[hn_side] + hn_len[hn_side], "%c[%lx %lx %lx](%lx) ", letter, (unsigned long)v[1],
                                           (unsigned long)v[2], (unsigned long)v[3], (unsigned long)v[4]);
}
static void hn_put32(uint32_t a, uint32_t v) { put32((BYTE *)(uintptr_t)a, v); }
static void hn_put16(uint32_t a, uint16_t v) { put16((BYTE *)(uintptr_t)a, v); }
static uint32_t hn_get32(uint32_t a) { return get32((BYTE *)(uintptr_t)a); }
static uint16_t hn_get16(uint32_t a) { return (uint16_t)(*(BYTE *)(uintptr_t)a | *(BYTE *)(uintptr_t)(a + 1u) << 8); }

/* ---- coverage */
enum { HNC_NO_TAPE, HNC_TAPE_ONLY, HNC_BOTH, HNC_HELD, HNC_NOT_HELD, HNC_SLOT, HNC_REMAP_ALONE, HNC_COUNT };
static const char *const hn_cv_names[HNC_COUNT] = {"no tape", "tape only", "tape and dump", "remap held", "remap not held",
                                                   "slot rewritten", "0x46a4ac called alone"};
static unsigned hn_cv[HNC_COUNT];
static void hn_cover(unsigned c) { if (!hn_side) ++hn_cv[c]; }

/* ---- the stubs' bodies, one for both sides */
static uint32_t hn_answer_seed;
static uint32_t hn_answer(void) {
    hn_answer_seed = hn_answer_seed * 1103515245u + 12345u;
    return hn_answer_seed ^ (hn_answer_seed >> 13);
}
/* slot: the argument slot's address (NULL: no argument). */
static void hn_stub(char letter, const uint32_t in[3], uint32_t slot, uint32_t out[3]) {
    hn_note(letter, in, slot ? hn_get32(slot) : 0u);
    out[0] = hn_answer(); out[1] = hn_answer(); out[2] = hn_answer();
    if (slot && hn_answer() % 2u) {
        hn_put32(slot, hn_answer());
        hn_cover(HNC_SLOT);
    }
}

/* ---- the original side's stubs: naked entries that keep the registers */
uint32_t hn_reg_in[3], hn_reg_out[3];
void __cdecl hn_stub_c(uint32_t letter, uint32_t args) {
    hn_stub((char)letter, hn_reg_in, letter == 's' ? 0u : args, hn_reg_out);
}
#define HN_STUB(name, letter)                                                                                                      \
    extern void name(void);                                                                                                        \
    __asm__(".text\n.globl _" #name "\n_" #name ":\n\tmovl %eax, _hn_reg_in\n\tmovl %edx, _hn_reg_in+4\n\tmovl %ecx, _hn_reg_in+8\n" \
            "\tleal 4(%esp), %eax\n\tpushl %eax\n\tpushl $" #letter "\n\tcall _hn_stub_c\n\taddl $8, %esp\n"                     \
            "\tmovl _hn_reg_out, %eax\n\tmovl _hn_reg_out+4, %edx\n\tmovl _hn_reg_out+8, %ecx\n\tret\n");
HN_STUB(hn_o_sound, 115)    /* 's' the sound close 0x462734 (no argument) */
HN_STUB(hn_o_fclose, 99)    /* 'c' _fclose */
HN_STUB(hn_o_delete, 100)   /* 'd' operator delete 0x477548 */

static BYTE hn_saved[2][5];
static int hn_patch(unsigned k, uint32_t va, void *target, int install) {
    BYTE *site = mapped + (va - 0x400000u);
    DWORD previous;
    if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &previous)) return fail("mission abort cleanup: stub protection");
    if (install) { memcpy(hn_saved[k], site, 5); site[0] = 0xe9; put32(site + 1, (uint32_t)(uintptr_t)target - (uint32_t)(uintptr_t)(site + 5)); }
    else memcpy(site, hn_saved[k], 5);
    if (!VirtualProtect(site, 5, previous, &previous)) return fail("mission abort cleanup: stub restore protection");
    FlushInstructionCache(GetCurrentProcess(), site, 5);
    return 0;
}

/* ---- the own side */
static void hn_s_any(es2_sim_service_call *call, void *user) {
    const char letter = (char)(uintptr_t)user;
    const uint32_t in[3] = {call->eax, call->edx, call->ecx};
    uint32_t out[3];
    hn_stub(letter, in, letter == 's' ? 0u : (uint32_t)(uintptr_t)call->slots, out);   /* identity memory: host = guest */
    call->eax = out[0]; call->edx = out[1]; call->ecx = out[2];
}
static void hn_refuse(const es2_sim_refusal *r, void *user) {
    (void)user;
    fprintf(stderr, "FAIL: own mission abort cleanup: refused call to 0x%08lx (%s): %s\n", (unsigned long)r->va, r->name ? r->name : "?",
            r->reason ? r->reason : "");
    hn_bad = 1;
}
static int hn_dispatch_setup(void) {
    es2_sim_dispatch_init(&hn_dispatch, hn_entries, 4096, hn_refuse, NULL);
    static es2_sim_services s;
    s.memory = &ir_memory;
    s.heap = &ir_heap;
    s.vt = &lc_vt;
    s.fs0 = NULL;
    if (es2_sim_register_services(&hn_dispatch, &s)) return fail("mission abort cleanup: dispatch");
    const char *error = NULL;
    uint32_t bad = 0;
    if (es2_sim_dispatch_seal(&hn_dispatch, &error, &bad)) return fail("mission abort cleanup: dispatch seal");
    /* The callees' rows: replaced by the recording stubs after the seal. */
    static const struct { uint32_t va; char letter; } rows[3] = {{0x00462734u, 's'}, {0x00496838u, 'c'}, {0x00477548u, 'd'}};
    for (unsigned i = 0; i < 3u; ++i) {
        es2_sim_entry *e = (es2_sim_entry *)es2_sim_dispatch_find(&hn_dispatch, rows[i].va);
        if (!e || e->kind != ES2_SIM_SERVICE) return fail("mission abort cleanup: a callee's row is missing");
        e->service = hn_s_any;
        e->service_user = (void *)(uintptr_t)rows[i].letter;
    }
    return 0;
}

/* ---- the random state (DATA) */
static uint32_t hn_seed;
static uint32_t hn_rand(uint32_t n) {
    hn_seed = hn_seed * 1103515245u + 12345u;
    return (hn_seed >> 8) % n;
}
static uint32_t hn_dword(void) { return hn_rand(0x10000u) << 16 | hn_rand(0x10000u); }
static void hn_build(void) {
    hn_put32(0x004d24e2u, hn_rand(3u) ? hn_dword() | 1u : 0u);
    hn_put32(0x004d24deu, hn_rand(3u) ? hn_dword() | 1u : 0u);
    hn_put16(0x004a0918u, hn_rand(2u) ? (uint16_t)(1u + hn_rand(0xffffu)) : 0u);
    hn_put32(0x006b4f20u, hn_dword());
    hn_put32(0x006b4f12u, hn_dword());
    hn_put32(0x004d27b8u, hn_dword());
}

/* ---- the rows */
enum { HN_CLEANUP, HN_REMAP, HN_OPS };
static const struct { uint32_t va; const char *name; } hn_ops[HN_OPS] = {
    {0x00461304u, "abort cleanup"}, {0x0046a4acu, "remap release"},
};
typedef struct hn_op { unsigned op; uint32_t in[3]; } hn_op;
static void hn_cover_op(const hn_op *o) {
    if (o->op == HN_REMAP) hn_cover(HNC_REMAP_ALONE);
    else if (!hn_get32(0x004d24e2u)) hn_cover(HNC_NO_TAPE);
    else hn_cover(hn_get32(0x004d24deu) ? HNC_BOTH : HNC_TAPE_ONLY);
    hn_cover(hn_get16(0x004a0918u) ? HNC_HELD : HNC_NOT_HELD);
}

static void hn_side_begin(unsigned side, uint32_t seed, uint32_t answers) {
    lc_reset(side, 47, 0x01000000u, 1);
    lc_loop.dispatch = &hn_dispatch;
    hn_side = side;
    hn_hash[side] = 0xcbf29ce484222325ull;
    hn_count[side] = 0;
    hn_len[side] = 0;
    hn_text[side][0] = 0;
    hn_answer_seed = answers;
    hn_seed = seed;
}
static unsigned hn_calls[HN_OPS];
static int hn_case(unsigned k) {
    unsigned ops = 1u + random32() % 4u;
    static hn_op list[4];
    uint32_t seed = random32(), answers = random32();
    static uint32_t reg[2][4][3];
    for (unsigned i = 0; i < ops; ++i) {
        list[i].op = random32() % 4u ? HN_CLEANUP : HN_REMAP;
        for (unsigned j = 0; j < 3u; ++j) list[i].in[j] = random32();
    }
    for (unsigned side = 0; side < 2u; ++side) {
        hn_side_begin(side, seed, answers);
        for (unsigned i = 0; i < ops && !hn_bad && !lc_bad; ++i) {
            hn_op *o = &list[i];
            hn_build();   /* fresh streams and flag before each call (the same draws on both sides) */
            if (side == 0) {
                hn_cover_op(o);
                hn_call((uint32_t)(uintptr_t)(mapped + (hn_ops[o->op].va - 0x400000u)), NULL, 0, o->in, reg[0][i]);
                ++hn_calls[o->op];
            } else {
                es2_sim_result r;
                memset(&r, 0, sizeof(r));
                if (es2_sim_call(&hn_dispatch, hn_ops[o->op].va, NULL, 0, o->in, &r)) hn_bad = 1;
                reg[1][i][0] = r.eax; reg[1][i][1] = r.edx; reg[1][i][2] = r.ecx;
            }
        }
        if (hn_bad || lc_bad) { fprintf(stderr, "FAIL: mission abort cleanup case %u: a call failed (side %u)\n", k, side); return 1; }
        su_take(side);
    }
    for (unsigned i = 0; i < ops; ++i)
        if (memcmp(reg[0][i], reg[1][i], sizeof(reg[0][i]))) {
            fprintf(stderr, "FAIL: mission abort cleanup case %u: %s (call %u) EAX %08lx / %08lx, EDX %08lx / %08lx, ECX %08lx / %08lx\n", k,
                    hn_ops[list[i].op].name, i, (unsigned long)reg[0][i][0], (unsigned long)reg[1][i][0], (unsigned long)reg[0][i][1],
                    (unsigned long)reg[1][i][1], (unsigned long)reg[0][i][2], (unsigned long)reg[1][i][2]);
            fprintf(stderr, "  original: %s\n  own:      %s\n", hn_text[0], hn_text[1]);
            return 1;
        }
    if (su_compare("mission abort cleanup", k)) {
        fprintf(stderr, "  rows:");
        for (unsigned i = 0; i < ops; ++i) fprintf(stderr, " %s", hn_ops[list[i].op].name);
        fprintf(stderr, "\n");
        return 1;
    }
    if (hn_hash[0] != hn_hash[1] || hn_count[0] != hn_count[1]) {
        fprintf(stderr, "FAIL: mission abort cleanup case %u: stub calls differ (%u / %u)\n  original: %s\n  own:      %s\n", k, hn_count[0], hn_count[1],
                hn_text[0], hn_text[1]);
        return 1;
    }
    return 0;
}

static int mission_abort_cleanup_cases(void) {
    current_function = "mission abort cleanup";
    es2_sim_identity_memory(&ir_memory);
    es2_sim_rtl_memory(&ir_memory, &su_rtl);
    su_data_pristine = malloc(SU_DATA_SIZE);
    su_snap[0] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    su_snap[1] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    ir_region = ww_alloc(IR_REGION_SIZE);
    lc_stack = VirtualAlloc(NULL, 0x10000u, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    lc_bits = ww_alloc(LC_BITS_SIZE);
    if (!su_data_pristine || !su_snap[0] || !su_snap[1] || !ir_region || !lc_stack || !lc_bits) return fail("mission abort cleanup: memory");
    su_pristine_take();
    if (!ir_patch_import("cw3220.dll", "_fclose", (void *)hn_o_fclose)) return fail("mission abort cleanup: import slot not found");
    if (hn_patch(0, 0x00462734u, (void *)hn_o_sound, 1) || hn_patch(1, 0x00477548u, (void *)hn_o_delete, 1)) return 1;
    if (hn_dispatch_setup()) return 1;
    int bad = 0;
    const unsigned cases = 3000;
    for (unsigned k = 0; !bad && k < cases; ++k) {
        current_case = k; current_case_valid = 1;
        bad = hn_case(k);
    }
    if (!bad) {
        printf("PASS: mission abort cleanup: %u cases identical\n", cases);
        printf("mission abort cleanup rows called:");
        for (unsigned i = 0; i < HN_OPS; ++i) printf(" %s %u%s", hn_ops[i].name, hn_calls[i], i + 1u < HN_OPS ? "," : "\n");
        printf("mission abort cleanup coverage:");
        for (unsigned i = 0; i < HNC_COUNT; ++i) printf(" %s %u%s", hn_cv_names[i], hn_cv[i], i + 1u < HNC_COUNT ? "," : "\n");
        for (unsigned i = 0; i < HNC_COUNT; ++i)
            if (!hn_cv[i]) { fprintf(stderr, "FAIL: mission abort cleanup: path '%s' never taken\n", hn_cv_names[i]); bad = 1; }
    }
    current_case_valid = 0;
    hn_patch(1, 0x00477548u, NULL, 0);
    hn_patch(0, 0x00462734u, NULL, 0);
    put32(mapped + 0x2cbee4u, 0);
    memcpy(mapped + SU_DATA_RVA, su_data_pristine, SU_DATA_SIZE);
    return bad;
}
