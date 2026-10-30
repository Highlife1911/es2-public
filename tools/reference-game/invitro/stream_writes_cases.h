/* The four rows of sim/dispatch/services/stream_writes.h (sim/src/view.c),
 * original machine code against the own rows: the stream-write thunks
 * 0x48f3ce (k dwords), 0x491156 and 0x492479 (k words) and 0x4911a3 (one
 * word from the argument slot). It reuses startup_cases.h's snapshots and
 * loop_cases.h's loop, as gap_roots_sim_cases.h does.
 *
 * Both sides start from the pristine DATA and an empty heap and build the
 * same seeded world: two streams whose vtable holds a recording host stub
 * in the write slot +0x14 and a random buffer. Then 1..8 of the rows run
 * on it, each with random entry EAX, EDX and ECX, k drawn from 0..16 and
 * random values, p a pointer into the buffer. The original side calls the
 * rows through an own thunk that loads and stores the three registers. The
 * slot stub logs its arguments (st, n), the first min(n, 32) bytes at p,
 * p itself where it is a world address (not for 0x4911a3, whose p is the
 * caller's argument slot and differs between the sides), whether EDX is p,
 * and the entry EAX and ECX; it leaves seeded EAX, EDX and ECX. Compared:
 * EAX, EDX and ECX of every call, DATA 0x497000..0x6cc000 and the heap,
 * the stub log. Coverage: counters from the original side; every one must
 * be non-zero. Identifiers use the prefix sw_/SW_. */
#pragma once
#include "init_records_cases.h"
#include "startup_cases.h"
#include "loop_cases.h"

static es2_sim_entry sw_entries[4096];
static es2_sim_dispatch sw_dispatch;
static int sw_bad;
static unsigned sw_side;

/* ---- the original side's register thunk: pushes the arguments, enters
 * with EAX/EDX/ECX from in[3], stores EAX/EDX/ECX into out[3]. */
extern void sw_call(uint32_t target, const uint32_t *args, uint32_t count, const uint32_t *in, uint32_t *out);
__asm__(".text\n.globl _sw_call\n_sw_call:\n\tpushl %ebp\n\tmovl %esp, %ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n"
        "\tmovl 16(%ebp), %ecx\n\tmovl 12(%ebp), %esi\n"
        "1:\n\ttestl %ecx, %ecx\n\tjz 2f\n\tdecl %ecx\n\tpushl (%esi,%ecx,4)\n\tjmp 1b\n"
        "2:\n\tmovl 20(%ebp), %ebx\n\tmovl (%ebx), %eax\n\tmovl 4(%ebx), %edx\n\tmovl 8(%ebx), %ecx\n"
        "\tcall *8(%ebp)\n"
        "\tmovl 24(%ebp), %ebx\n\tmovl %eax, (%ebx)\n\tmovl %edx, 4(%ebx)\n\tmovl %ecx, 8(%ebx)\n"
        "\tleal -12(%ebp), %esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n");

static uint8_t sw_get8(uint32_t a) { return *(BYTE *)(uintptr_t)a; }
static void sw_put8(uint32_t a, uint8_t v) { *(BYTE *)(uintptr_t)a = v; }
static void sw_put32(uint32_t a, uint32_t v) { put32((BYTE *)(uintptr_t)a, v); }

/* ---- the stub log of a side, the coverage and the answers */
enum { SWC_K_ZERO, SWC_K_SMALL, SWC_K_LARGE, SWC_COUNT };
static const char *const sw_cv_names[SWC_COUNT] = {"k zero", "k small", "k large"};
static unsigned sw_cv[SWC_COUNT];
static uint64_t sw_hash[2];
static unsigned sw_count[2];
static char sw_text[2][4096];
static size_t sw_len[2];
static int sw_log_p;
static uint32_t sw_answer_seed;
static uint32_t sw_answer(void) {
    sw_answer_seed = sw_answer_seed * 1103515245u + 12345u;
    return sw_answer_seed ^ (sw_answer_seed >> 13);
}
static void sw_note(const uint32_t *w, unsigned n) {
    uint64_t h = sw_hash[sw_side];
    for (unsigned i = 0; i < n; ++i) { h ^= w[i]; h *= 0x100000001b3ull; }
    sw_hash[sw_side] = h;
    ++sw_count[sw_side];
    if (sw_len[sw_side] + 80u < sizeof(sw_text[0]))
        sw_len[sw_side] += (size_t)sprintf(sw_text[sw_side] + sw_len[sw_side], "W(%lx,%lx,%lx,%lx,%lx,%lx) ", (unsigned long)w[0],
                                           (unsigned long)w[1], (unsigned long)w[2], (unsigned long)w[3], (unsigned long)w[4],
                                           (unsigned long)w[5]);
}
/* One stub body for both sides: s the slot's arguments (st, n, p), in the
 * entry EAX, EDX, ECX. */
static void sw_stub(const uint32_t *s, const uint32_t in[3], uint32_t out[3]) {
    uint32_t n = s[1] < 32u ? s[1] : 32u, bytes = 0x811c9dc5u;
    for (uint32_t k = 0; k < n; ++k) { bytes ^= sw_get8(s[2] + k); bytes *= 0x01000193u; }
    const uint32_t w[6] = {s[0], s[1], bytes, sw_log_p ? s[2] : 0u, in[1] == s[2], in[0]};
    sw_note(w, 6);
    const uint32_t e[1] = {in[2]};
    sw_note(e, 1);
    out[0] = sw_answer();
    out[1] = sw_answer();
    out[2] = sw_answer();
}

/* ---- the original side's slot stub: stores the entry registers, passes
 * the stack arguments */
uint32_t sw_reg_in[3], sw_reg_out[3];
void __cdecl sw_stub_c(const uint32_t *args) { sw_stub(args, sw_reg_in, sw_reg_out); }
extern void sw_slot_write(void);
__asm__(".text\n.globl _sw_slot_write\n_sw_slot_write:\n\tmovl %eax, _sw_reg_in\n\tmovl %edx, _sw_reg_in+4\n\tmovl %ecx, _sw_reg_in+8\n"
        "\tleal 4(%esp), %eax\n\tpushl %eax\n\tcall _sw_stub_c\n"
        "\taddl $4, %esp\n\tmovl _sw_reg_out, %eax\n\tmovl _sw_reg_out+4, %edx\n\tmovl _sw_reg_out+8, %ecx\n\tret\n");

/* ---- the own side */
static void sw_s_stub(es2_sim_service_call *call, void *user) {
    uint32_t out[3];
    const uint32_t in[3] = {call->eax, call->edx, call->ecx};
    (void)user;
    sw_stub(call->slots, in, out);
    call->eax = out[0]; call->edx = out[1]; call->ecx = out[2];
}
static void sw_refuse(const es2_sim_refusal *r, void *user) {
    (void)user;
    fprintf(stderr, "FAIL: own stream writes: refused call to 0x%08lx (%s): %s\n", (unsigned long)r->va, r->name ? r->name : "?",
            r->reason ? r->reason : "");
    sw_bad = 1;
}
static int sw_dispatch_setup(void) {
    es2_sim_dispatch_init(&sw_dispatch, sw_entries, 4096, sw_refuse, NULL);
    static es2_sim_services s;
    s.memory = &ir_memory;
    s.heap = &ir_heap;
    s.vt = &lc_vt;
    s.fs0 = NULL;
    if (es2_sim_register_services(&sw_dispatch, &s)) return fail("stream writes: dispatch");
    if (es2_sim_dispatch_add_service(&sw_dispatch, (uint32_t)(uintptr_t)sw_slot_write, "stream writes slot", sw_s_stub, NULL, 0, 3))
        return fail("stream writes: dispatch (slot)");
    const char *error = NULL;
    uint32_t bad = 0;
    if (es2_sim_dispatch_seal(&sw_dispatch, &error, &bad)) return fail("stream writes: dispatch seal");
    return 0;
}

/* ---- the synthetic world */
#define SW_BUF 0x80u
static uint32_t sw_seed;
static uint32_t sw_rand(uint32_t n) {
    sw_seed = sw_seed * 1103515245u + 12345u;
    return (sw_seed >> 8) % n;
}
typedef struct sw_world { uint32_t vt, st[2], buf; } sw_world;
static sw_world sw_w, sw_w0;
static uint32_t sw_alloc(uint32_t n) { return (uint32_t)(uintptr_t)ir_calloc(1, n); }
static void sw_build(void) {
    sw_world *w = &sw_w;
    memset(w, 0, sizeof(*w));
    w->vt = sw_alloc(0x20u);
    sw_put32(w->vt + 0x14u, (uint32_t)(uintptr_t)sw_slot_write);
    for (unsigned k = 0; k < 2u; ++k) {
        w->st[k] = sw_alloc(0x10u);
        for (uint32_t i = 0; i < 0x10u; ++i) sw_put8(w->st[k] + i, (uint8_t)sw_rand(256u));
        sw_put32(w->st[k], w->vt);
    }
    w->buf = sw_alloc(SW_BUF + 32u);
    for (uint32_t i = 0; i < SW_BUF + 32u; ++i) sw_put8(w->buf + i, (uint8_t)sw_rand(256u));
}

/* ---- the rows */
enum { SW_DWORDS, SW_WORDS_A, SW_WORD, SW_WORDS_B, SW_OPS };
static const struct { uint32_t va; unsigned n; const char *name; } sw_ops[SW_OPS] = {
    {0x0048f3ceu, 3, "write dwords"}, {0x00491156u, 3, "write words 491156"}, {0x004911a3u, 2, "write word"},
    {0x00492479u, 3, "write words 492479"},
};
typedef struct sw_op { unsigned op; uint32_t a[3], in[3]; } sw_op;
static unsigned sw_calls[SW_OPS];
static void sw_args(sw_op *o) {
    const sw_world *w = &sw_w;
    o->a[0] = w->st[random32() % 2u];
    if (o->op == SW_WORD) {
        o->a[1] = random32();
        o->a[2] = random32();
    } else {
        uint32_t r = random32() % 8u;
        o->a[1] = r < 6u ? random32() % 17u : random32();
        o->a[2] = w->buf + random32() % SW_BUF;
        sw_cv[o->a[1] == 0 ? SWC_K_ZERO : o->a[1] <= 16u ? SWC_K_SMALL : SWC_K_LARGE]++;
    }
    for (unsigned k = 0; k < 3u; ++k) o->in[k] = random32();
}

static int sw_side_begin(unsigned side, uint32_t seed, uint32_t answers) {
    lc_reset(side, 47, 0x01000000u, 1);
    lc_loop.dispatch = &sw_dispatch;
    sw_side = side;
    sw_hash[side] = 0xcbf29ce484222325ull;
    sw_count[side] = 0;
    sw_len[side] = 0;
    sw_text[side][0] = 0;
    sw_answer_seed = answers;
    sw_seed = seed;
    sw_build();
    if (side) {
        if (memcmp(&sw_w, &sw_w0, sizeof(sw_w))) return fail("stream writes: the sides' worlds differ (heap order)");
    } else {
        sw_w0 = sw_w;
    }
    return 0;
}
static int sw_case(unsigned k) {
    unsigned ops = 1u + random32() % 8u;
    static sw_op list[8];
    uint32_t seed = random32(), answers = random32();
    static uint32_t reg[2][8][3];
    for (unsigned i = 0; i < ops; ++i) list[i].op = random32() % SW_OPS;
    for (unsigned side = 0; side < 2u; ++side) {
        if (sw_side_begin(side, seed, answers)) return 1;
        for (unsigned i = 0; i < ops && !sw_bad && !lc_bad; ++i) {
            sw_op *o = &list[i];
            if (side == 0) sw_args(o);
            sw_log_p = o->op != SW_WORD;
            if (side == 0) {
                sw_call((uint32_t)(uintptr_t)(mapped + (sw_ops[o->op].va - 0x400000u)), o->a, sw_ops[o->op].n, o->in, reg[0][i]);
                ++sw_calls[o->op];
            } else {
                es2_sim_result r;
                memset(&r, 0, sizeof(r));
                if (es2_sim_call(&sw_dispatch, sw_ops[o->op].va, o->a, sw_ops[o->op].n, o->in, &r)) sw_bad = 1;
                reg[1][i][0] = r.eax; reg[1][i][1] = r.edx; reg[1][i][2] = r.ecx;
            }
        }
        if (sw_bad || lc_bad) { fprintf(stderr, "FAIL: stream writes case %u: a call failed (side %u)\n", k, side); return 1; }
        su_take(side);
    }
    for (unsigned i = 0; i < ops; ++i)
        if (memcmp(reg[0][i], reg[1][i], sizeof(reg[0][i]))) {
            fprintf(stderr, "FAIL: stream writes case %u: %s (call %u) EAX %08lx / %08lx, EDX %08lx / %08lx, ECX %08lx / %08lx\n", k,
                    sw_ops[list[i].op].name, i, (unsigned long)reg[0][i][0], (unsigned long)reg[1][i][0], (unsigned long)reg[0][i][1],
                    (unsigned long)reg[1][i][1], (unsigned long)reg[0][i][2], (unsigned long)reg[1][i][2]);
            return 1;
        }
    if (su_compare("stream writes", k)) return 1;
    if (sw_hash[0] != sw_hash[1] || sw_count[0] != sw_count[1]) {
        fprintf(stderr, "FAIL: stream writes case %u: stub calls differ (%u / %u)\n  original: %s\n  own:      %s\n", k, sw_count[0],
                sw_count[1], sw_text[0], sw_text[1]);
        return 1;
    }
    return 0;
}

static int stream_writes_cases(void) {
    current_function = "stream writes";
    es2_sim_identity_memory(&ir_memory);
    es2_sim_rtl_memory(&ir_memory, &su_rtl);
    su_data_pristine = malloc(SU_DATA_SIZE);
    su_snap[0] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    su_snap[1] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    ir_region = ww_alloc(IR_REGION_SIZE);
    lc_stack = VirtualAlloc(NULL, 0x10000u, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    lc_bits = ww_alloc(LC_BITS_SIZE);
    if (!su_data_pristine || !su_snap[0] || !su_snap[1] || !ir_region || !lc_stack || !lc_bits) return fail("stream writes: memory");
    su_pristine_take();
    if (sw_dispatch_setup()) return 1;
    int bad = 0;
    const unsigned cases = 2000;
    for (unsigned k = 0; !bad && k < cases; ++k) {
        current_case = k; current_case_valid = 1;
        bad = sw_case(k);
    }
    if (!bad) {
        printf("PASS: stream writes: %u cases identical\n", cases);
        printf("Stream writes rows called:");
        for (unsigned i = 0; i < SW_OPS; ++i) printf(" %s %u%s", sw_ops[i].name, sw_calls[i], i + 1u < SW_OPS ? "," : "\n");
        printf("Stream writes coverage:");
        for (unsigned i = 0; i < SWC_COUNT; ++i) printf(" %s %u%s", sw_cv_names[i], sw_cv[i], i + 1u < SWC_COUNT ? "," : "\n");
        for (unsigned i = 0; i < SW_OPS; ++i)
            if (!sw_calls[i]) { fprintf(stderr, "FAIL: stream writes: row '%s' never called\n", sw_ops[i].name); bad = 1; }
        for (unsigned i = 0; i < SWC_COUNT; ++i)
            if (!sw_cv[i]) { fprintf(stderr, "FAIL: stream writes: path '%s' never taken\n", sw_cv_names[i]); bad = 1; }
    }
    current_case_valid = 0;
    memcpy(mapped + SU_DATA_RVA, su_data_pristine, SU_DATA_SIZE);
    return bad;
}
