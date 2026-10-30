/* The five rows of sim/dispatch/services/gap_roots_sim.h (sim/src/gfx.c),
 * original machine code against the own rows: the stream helpers 0x492447 (two
 * bytes) and 0x49085d (one byte), the next MFD page 0x446e3c and the shield
 * arrow button's slots 0x444b84 (draw) and 0x444b98 (frame). It reuses
 * startup_cases.h's snapshots and loop_cases.h's loop, as runtime_leaves_cases.h does.
 *
 * Both sides start from the pristine DATA and an empty heap and build the
 * same seeded world: two streams whose vtable holds a recording host stub
 * in the write slot +0x14, three MFD objects whose dword +0xc1 is drawn
 * from -1, 0..7 and random values, and four buttons whose byte +0x1f is
 * drawn from 0..3, 0xff and random values. Then 1..8 of the rows run on
 * it, each with random entry EAX, EDX and ECX. The original side calls the
 * rows through an own thunk that loads and stores the three registers.
 * The callees outside the rows are recording stubs on both sides: the
 * page switch 0x446e60 and the copy 0x43122c (rows of other packages,
 * replaced after the seal), panel_visible 0x431410 (a hook, added as a
 * service) and the write slot. A stub logs its letter and arguments; the
 * write slot logs the n bytes at its pointer instead of the pointer (the
 * sides' stacks differ); the copy stub may rewrite the button's byte +0x1f
 * (the frame reads it again after the call). Each stub leaves seeded EAX,
 * EDX and ECX. Compared: EAX, EDX and ECX of every call, DATA
 * 0x497000..0x6cc000 and the heap, the stub log. Coverage: counters from
 * the original side; every one must be non-zero. Identifiers use the
 * prefix hq_/HQ_. */
#pragma once
#include "init_records_cases.h"
#include "startup_cases.h"
#include "loop_cases.h"

static es2_sim_entry hq_entries[4096];
static es2_sim_dispatch hq_dispatch;
static int hq_bad;
static unsigned hq_side;

/* ---- the original side's register thunk: pushes the arguments, enters
 * with EAX/EDX/ECX from in[3], stores EAX/EDX/ECX into out[3]. */
extern void hq_call(uint32_t target, const uint32_t *args, uint32_t count, const uint32_t *in, uint32_t *out);
__asm__(".text\n.globl _hq_call\n_hq_call:\n\tpushl %ebp\n\tmovl %esp, %ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n"
        "\tmovl 16(%ebp), %ecx\n\tmovl 12(%ebp), %esi\n"
        "1:\n\ttestl %ecx, %ecx\n\tjz 2f\n\tdecl %ecx\n\tpushl (%esi,%ecx,4)\n\tjmp 1b\n"
        "2:\n\tmovl 20(%ebp), %ebx\n\tmovl (%ebx), %eax\n\tmovl 4(%ebx), %edx\n\tmovl 8(%ebx), %ecx\n"
        "\tcall *8(%ebp)\n"
        "\tmovl 24(%ebp), %ebx\n\tmovl %eax, (%ebx)\n\tmovl %edx, 4(%ebx)\n\tmovl %ecx, 8(%ebx)\n"
        "\tleal -12(%ebp), %esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n");

static uint32_t hq_get(uint32_t a) { return get32((BYTE *)(uintptr_t)a); }
static uint8_t hq_get8(uint32_t a) { return *(BYTE *)(uintptr_t)a; }
static void hq_put8(uint32_t a, uint8_t v) { *(BYTE *)(uintptr_t)a = v; }
static void hq_put32(uint32_t a, uint32_t v) { put32((BYTE *)(uintptr_t)a, v); }

/* ---- the stub log of a side, the coverage and the answers */
enum { HQC_WORD, HQC_BYTE, HQC_WRAP, HQC_NO_WRAP, HQC_MINUS_ONE, HQC_FRAME_COPY, HQC_FRAME_COUNT, HQC_FRAME_NONE, HQC_REWRITE,
       HQC_VISIBLE, HQC_HIDDEN, HQC_COUNT };
static const char *const hq_cv_names[HQC_COUNT] = {
    "write word", "write byte", "page wraps", "page steps", "page from -1", "frame copy", "frame count", "frame none",
    "copy rewrites the byte", "visible", "hidden",
};
static unsigned hq_cv[HQC_COUNT];
static uint64_t hq_hash[2];
static unsigned hq_count[2];
static char hq_text[2][4096];
static size_t hq_len[2];
static uint32_t hq_answer_seed;
static uint32_t hq_answer(void) {
    hq_answer_seed = hq_answer_seed * 1103515245u + 12345u;
    return hq_answer_seed ^ (hq_answer_seed >> 13);
}
static void hq_cover(unsigned c) { if (!hq_side) ++hq_cv[c]; }
static unsigned hq_nargs(char letter) {
    switch (letter) {
    case 'W': case 'C': return 3;
    case 'P': return 2;
    default: return 1;
    }
}
static void hq_note(char letter, const uint32_t *v, unsigned n) {
    uint64_t h = hq_hash[hq_side];
    uint32_t w[4] = {(uint32_t)letter, 0, 0, 0};
    for (unsigned i = 0; i < n; ++i) w[1 + i] = v[i];
    for (unsigned i = 0; i < 4u; ++i) { h ^= w[i]; h *= 0x100000001b3ull; }
    hq_hash[hq_side] = h;
    ++hq_count[hq_side];
    if (hq_len[hq_side] + 60u < sizeof(hq_text[0]))
        hq_len[hq_side] += (size_t)sprintf(hq_text[hq_side] + hq_len[hq_side], "%c(%lx,%lx,%lx) ", letter, (unsigned long)w[1],
                                           (unsigned long)w[2], (unsigned long)w[3]);
}
/* One stub body for both sides. */
static void hq_stub(char letter, const uint32_t *s, uint32_t out[3]) {
    if (letter == 'W') {   /* the write slot (st, n, p): the bytes at p, not p */
        uint32_t v = 0, n = s[1] < 4u ? s[1] : 4u;
        for (uint32_t k = 0; k < n; ++k) v |= (uint32_t)hq_get8(s[2] + k) << (8u * k);
        const uint32_t logged[3] = {s[0], s[1], v};
        hq_note(letter, logged, 3);
        hq_cover(s[1] == 2u ? HQC_WORD : HQC_BYTE);
    } else {
        hq_note(letter, s, hq_nargs(letter));
    }
    out[0] = hq_answer();
    out[1] = hq_answer();
    out[2] = hq_answer();
    if (letter == 'V') hq_cover(out[0] & 0xffu ? HQC_VISIBLE : HQC_HIDDEN);
    if (letter == 'C' && hq_answer() % 3u == 0) {   /* the copy (0, B, 0) rewrites B+0x1f */
        hq_put8(s[1] + 0x1fu, (uint8_t)hq_answer());
        hq_cover(HQC_REWRITE);
    }
}

/* ---- the original side's stubs: naked entries that pass the stack arguments */
uint32_t hq_reg_out[3];
void __cdecl hq_stub_c(uint32_t letter, const uint32_t *args) { hq_stub((char)letter, args, hq_reg_out); }
#define HQ_STUB(name, letter)                                                                                                      \
    extern void name(void);                                                                                                        \
    __asm__(".text\n.globl _" #name "\n_" #name ":\n\tleal 4(%esp), %eax\n\tpushl %eax\n\tpushl $" #letter "\n\tcall _hq_stub_c\n"  \
            "\taddl $8, %esp\n\tmovl _hq_reg_out, %eax\n\tmovl _hq_reg_out+4, %edx\n\tmovl _hq_reg_out+8, %ecx\n\tret\n");
HQ_STUB(hq_o_page, 80)      /* 'P' 0x446e60 */
HQ_STUB(hq_o_copy, 67)      /* 'C' 0x43122c */
HQ_STUB(hq_o_visible, 86)   /* 'V' 0x431410 */
HQ_STUB(hq_slot_write, 87)  /* 'W' the stream's slot +0x14 */

static int hq_patch(uint32_t va, void *target, BYTE saved[5], int install) {
    BYTE *site = mapped + (va - 0x400000u);
    DWORD previous;
    if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &previous)) return fail("h5-24: stub protection");
    if (install) { memcpy(saved, site, 5); site[0] = 0xe9; put32(site + 1, (uint32_t)(uintptr_t)target - (uint32_t)(uintptr_t)(site + 5)); }
    else memcpy(site, saved, 5);
    if (!VirtualProtect(site, 5, previous, &previous)) return fail("h5-24: stub restore protection");
    FlushInstructionCache(GetCurrentProcess(), site, 5);
    return 0;
}
/* The callees: rows of other packages (replaced after the seal on the own
 * side) and the hook panel_visible (added as a service). */
static const struct { uint32_t va; void *stub; char letter; int row; } hq_patched[] = {
    {0x00446e60u, (void *)hq_o_page, 'P', 1}, {0x0043122cu, (void *)hq_o_copy, 'C', 1}, {0x00431410u, (void *)hq_o_visible, 'V', 0},
};
#define HQ_PATCHED (sizeof(hq_patched) / sizeof(hq_patched[0]))
static BYTE hq_saved[HQ_PATCHED][5];

/* ---- the own side */
static void hq_s_stub(es2_sim_service_call *call, void *user) {
    uint32_t out[3];
    hq_stub((char)(uintptr_t)user, call->slots, out);
    call->eax = out[0]; call->edx = out[1]; call->ecx = out[2];
}
static void hq_refuse(const es2_sim_refusal *r, void *user) {
    (void)user;
    fprintf(stderr, "FAIL: own h5-24: refused call to 0x%08lx (%s): %s\n", (unsigned long)r->va, r->name ? r->name : "?",
            r->reason ? r->reason : "");
    hq_bad = 1;
}
static int hq_dispatch_setup(void) {
    es2_sim_dispatch_init(&hq_dispatch, hq_entries, 4096, hq_refuse, NULL);
    static es2_sim_services s;
    s.memory = &ir_memory;
    s.heap = &ir_heap;
    s.vt = &lc_vt;
    s.fs0 = NULL;
    if (es2_sim_register_services(&hq_dispatch, &s)) return fail("h5-24: dispatch");
    if (es2_sim_dispatch_add_service(&hq_dispatch, (uint32_t)(uintptr_t)hq_slot_write, "h5-24 write slot", hq_s_stub,
                                     (void *)(uintptr_t)'W', 0, 3))
        return fail("h5-24: dispatch (slot)");
    for (unsigned i = 0; i < HQ_PATCHED; ++i)
        if (!hq_patched[i].row && es2_sim_dispatch_add_service(&hq_dispatch, hq_patched[i].va, "h5-24 hook stub", hq_s_stub,
                                                               (void *)(uintptr_t)hq_patched[i].letter, 0,
                                                               (uint8_t)hq_nargs(hq_patched[i].letter)))
            return fail("h5-24: dispatch (hook stub)");
    const char *error = NULL;
    uint32_t bad = 0;
    if (es2_sim_dispatch_seal(&hq_dispatch, &error, &bad)) return fail("h5-24: dispatch seal");
    for (unsigned i = 0; i < HQ_PATCHED; ++i) {
        if (!hq_patched[i].row) continue;
        es2_sim_entry *e = (es2_sim_entry *)es2_sim_dispatch_find(&hq_dispatch, hq_patched[i].va);
        if (!e || e->kind != ES2_SIM_SERVICE) return fail("h5-24: service missing");
        e->service = hq_s_stub;
        e->service_user = (void *)(uintptr_t)hq_patched[i].letter;
    }
    return 0;
}

/* ---- the synthetic world */
static uint32_t hq_seed;
static uint32_t hq_rand(uint32_t n) {
    hq_seed = hq_seed * 1103515245u + 12345u;
    return (hq_seed >> 8) % n;
}
typedef struct hq_world { uint32_t vt, st[2], g[3], b[4]; } hq_world;
static hq_world hq_w, hq_w0;
static uint32_t hq_alloc(uint32_t n) { return (uint32_t)(uintptr_t)ir_calloc(1, n); }
static void hq_randomize(uint32_t a, uint32_t n) { for (uint32_t k = 0; k < n; ++k) hq_put8(a + k, (uint8_t)hq_rand(256u)); }
static void hq_build(void) {
    hq_world *w = &hq_w;
    memset(w, 0, sizeof(*w));
    w->vt = hq_alloc(0x20u);
    hq_put32(w->vt + 0x14u, (uint32_t)(uintptr_t)hq_slot_write);
    for (unsigned k = 0; k < 2u; ++k) {
        w->st[k] = hq_alloc(0x10u);
        hq_randomize(w->st[k], 0x10u);
        hq_put32(w->st[k], w->vt);
    }
    for (unsigned k = 0; k < 3u; ++k) {
        w->g[k] = hq_alloc(0xd0u);
        hq_randomize(w->g[k], 0xd0u);
    }
    for (unsigned k = 0; k < 4u; ++k) {
        w->b[k] = hq_alloc(0x40u);
        hq_randomize(w->b[k], 0x40u);
    }
}
/* Before a row: the MFD's page dword and the button's byte drawn again. */
static void hq_state(uint32_t g, uint32_t b) {
    static const uint32_t pages[] = {0xffffffffu, 0, 1, 2, 3, 4, 5, 5, 5, 6, 7};
    uint32_t r = hq_rand(12u);
    hq_put32(g + 0xc1u, r < 11u ? pages[r] : hq_rand(65536u) << 16 | hq_rand(65536u));
    static const uint8_t bytes[] = {0, 1, 1, 2, 2, 3, 0xff};
    uint32_t q = hq_rand(8u);
    hq_put8(b + 0x1fu, q < 7u ? bytes[q] : (uint8_t)hq_rand(256u));
}

/* ---- the rows */
enum { HQ_PUT_WORD, HQ_PUT_BYTE, HQ_MFD_NEXT, HQ_SB_DRAW, HQ_SB_FRAME, HQ_OPS };
static const struct { uint32_t va; unsigned n; const char *name; } hq_ops[HQ_OPS] = {
    {0x00492447u, 2, "stream put word"}, {0x0049085du, 2, "stream put byte"}, {0x00446e3cu, 1, "mfd next"},
    {0x00444b84u, 1, "shield button draw"}, {0x00444b98u, 1, "shield button frame"},
};
typedef struct hq_op { unsigned op; uint32_t a[2], in[3]; } hq_op;
static unsigned hq_calls[HQ_OPS];
static void hq_args(hq_op *o) {
    const hq_world *w = &hq_w;
    memset(o->a, 0, sizeof(o->a));
    switch (o->op) {
    case HQ_PUT_WORD: case HQ_PUT_BYTE: o->a[0] = w->st[random32() % 2u]; o->a[1] = random32(); break;
    case HQ_MFD_NEXT: o->a[0] = w->g[random32() % 3u]; break;
    default: o->a[0] = w->b[random32() % 4u]; break;
    }
    for (unsigned k = 0; k < 3u; ++k) o->in[k] = random32();
}

static int hq_side_begin(unsigned side, uint32_t seed, uint32_t answers) {
    lc_reset(side, 47, 0x01000000u, 1);
    lc_loop.dispatch = &hq_dispatch;
    hq_side = side;
    hq_hash[side] = 0xcbf29ce484222325ull;
    hq_count[side] = 0;
    hq_len[side] = 0;
    hq_text[side][0] = 0;
    hq_answer_seed = answers;
    hq_seed = seed;
    hq_build();
    if (side) {
        if (memcmp(&hq_w, &hq_w0, sizeof(hq_w))) return fail("h5-24: the sides' worlds differ (heap order)");
    } else {
        hq_w0 = hq_w;
    }
    return 0;
}
static int hq_case(unsigned k) {
    unsigned ops = 1u + random32() % 8u;
    static hq_op list[8];
    uint32_t seed = random32(), answers = random32();
    static uint32_t reg[2][8][3];
    for (unsigned i = 0; i < ops; ++i) list[i].op = random32() % HQ_OPS;
    for (unsigned side = 0; side < 2u; ++side) {
        if (hq_side_begin(side, seed, answers)) return 1;
        for (unsigned i = 0; i < ops && !hq_bad && !lc_bad; ++i) {
            hq_op *o = &list[i];
            if (side == 0) hq_args(o);
            /* the state draw comes from the world's generator: the same on both sides */
            hq_state(o->op == HQ_MFD_NEXT ? o->a[0] : hq_w.g[0], o->op >= HQ_SB_DRAW ? o->a[0] : hq_w.b[0]);
            if (side == 0) {
                uint32_t page = hq_get(o->a[0] + 0xc1u);
                uint8_t byte = hq_get8(o->a[0] + 0x1fu);
                hq_call((uint32_t)(uintptr_t)(mapped + (hq_ops[o->op].va - 0x400000u)), o->a, hq_ops[o->op].n, o->in, reg[0][i]);
                ++hq_calls[o->op];
                if (o->op == HQ_MFD_NEXT) {
                    if (page == 5u) ++hq_cv[HQC_WRAP];
                    else ++hq_cv[HQC_NO_WRAP];
                    if (page == 0xffffffffu) ++hq_cv[HQC_MINUS_ONE];
                }
                if (o->op == HQ_SB_FRAME) ++hq_cv[byte == 1u ? HQC_FRAME_COPY : byte == 2u ? HQC_FRAME_COUNT : HQC_FRAME_NONE];
            } else {
                es2_sim_result r;
                memset(&r, 0, sizeof(r));
                if (es2_sim_call(&hq_dispatch, hq_ops[o->op].va, o->a, hq_ops[o->op].n, o->in, &r)) hq_bad = 1;
                reg[1][i][0] = r.eax; reg[1][i][1] = r.edx; reg[1][i][2] = r.ecx;
            }
        }
        if (hq_bad || lc_bad) { fprintf(stderr, "FAIL: h5-24 case %u: a call failed (side %u)\n", k, side); return 1; }
        su_take(side);
    }
    for (unsigned i = 0; i < ops; ++i)
        if (memcmp(reg[0][i], reg[1][i], sizeof(reg[0][i]))) {
            fprintf(stderr, "FAIL: h5-24 case %u: %s (call %u) EAX %08lx / %08lx, EDX %08lx / %08lx, ECX %08lx / %08lx\n", k,
                    hq_ops[list[i].op].name, i, (unsigned long)reg[0][i][0], (unsigned long)reg[1][i][0], (unsigned long)reg[0][i][1],
                    (unsigned long)reg[1][i][1], (unsigned long)reg[0][i][2], (unsigned long)reg[1][i][2]);
            return 1;
        }
    if (su_compare("h5-24", k)) {
        fprintf(stderr, "  rows:");
        for (unsigned i = 0; i < ops; ++i) fprintf(stderr, " %s", hq_ops[list[i].op].name);
        fprintf(stderr, "\n");
        return 1;
    }
    if (hq_hash[0] != hq_hash[1] || hq_count[0] != hq_count[1]) {
        fprintf(stderr, "FAIL: h5-24 case %u: stub calls differ (%u / %u)\n  original: %s\n  own:      %s\n", k, hq_count[0], hq_count[1],
                hq_text[0], hq_text[1]);
        return 1;
    }
    return 0;
}

static int gap_roots_sim_cases(void) {
    current_function = "h5-24";
    es2_sim_identity_memory(&ir_memory);
    es2_sim_rtl_memory(&ir_memory, &su_rtl);
    su_data_pristine = malloc(SU_DATA_SIZE);
    su_snap[0] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    su_snap[1] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    ir_region = ww_alloc(IR_REGION_SIZE);
    lc_stack = VirtualAlloc(NULL, 0x10000u, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    lc_bits = ww_alloc(LC_BITS_SIZE);
    if (!su_data_pristine || !su_snap[0] || !su_snap[1] || !ir_region || !lc_stack || !lc_bits) return fail("h5-24: memory");
    su_pristine_take();
    for (unsigned i = 0; i < HQ_PATCHED; ++i)
        if (hq_patch(hq_patched[i].va, hq_patched[i].stub, hq_saved[i], 1)) return 1;
    if (hq_dispatch_setup()) return 1;
    int bad = 0;
    const unsigned cases = 2000;
    for (unsigned k = 0; !bad && k < cases; ++k) {
        current_case = k; current_case_valid = 1;
        bad = hq_case(k);
    }
    if (!bad) {
        printf("PASS: h5-24: %u cases identical\n", cases);
        printf("H5-24 rows called:");
        for (unsigned i = 0; i < HQ_OPS; ++i) printf(" %s %u%s", hq_ops[i].name, hq_calls[i], i + 1u < HQ_OPS ? "," : "\n");
        printf("H5-24 coverage:");
        for (unsigned i = 0; i < HQC_COUNT; ++i) printf(" %s %u%s", hq_cv_names[i], hq_cv[i], i + 1u < HQC_COUNT ? "," : "\n");
        for (unsigned i = 0; i < HQ_OPS; ++i)
            if (!hq_calls[i]) { fprintf(stderr, "FAIL: h5-24: row '%s' never called\n", hq_ops[i].name); bad = 1; }
        for (unsigned i = 0; i < HQC_COUNT; ++i)
            if (!hq_cv[i]) { fprintf(stderr, "FAIL: h5-24: path '%s' never taken\n", hq_cv_names[i]); bad = 1; }
    }
    current_case_valid = 0;
    for (unsigned i = 0; i < HQ_PATCHED; ++i) hq_patch(hq_patched[i].va, NULL, hq_saved[i], 0);
    memcpy(mapped + SU_DATA_RVA, su_data_pristine, SU_DATA_SIZE);
    return bad;
}
