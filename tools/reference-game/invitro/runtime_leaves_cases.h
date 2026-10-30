/* The files, streams, math and runtime leftovers
 * (sim/dispatch/services/runtime_leaves.h; sim/src/services_rtl.c and services_files.c,
 * appended): original machine code against the own rows. They are the arena
 * set-up and block test 0x476df4/0x476e2c, the grid leaf 0x470db8 and the field
 * leaf 0x46b850, the pure math leaves of 0x47db4d..0x480590 and 0x492b04, and
 * the leaf methods of the stream classes: the base class (vtable 0x4a16e8), the
 * read-from-handle class (0x4a1738), the buffered class (0x4a1ad8) and the two
 * detach thunks 0x47b5cc/0x47b9cc. It reuses startup_cases.h's snapshots and
 * loop_cases.h's loop, as hdd_map_leaves_cases.h does.
 *
 * Both sides start from the pristine DATA and an empty heap and build the
 * same seeded world: a construction counter, one inner stream (a vtable of
 * host stubs) with a random status, five buffered streams (three live ones
 * over the inner stream and two that detach or are destroyed, one of them
 * without an inner stream), three handle streams, two base-class objects,
 * two detach objects, the arena header with its block tags (and a header
 * whose end passes 2^31), a grid object with its cell counters, six
 * vector blocks of edge-heavy dwords, output blocks, division operands that
 * do not fault (the rows refuse a fault), and the slot table 0x6c5eb4. Then
 * 1..8 of the rows run on it, each with random entry EAX, EDX and ECX.
 * The original side calls the rows through an own thunk that loads and
 * stores the three registers. Every callee outside the rows is a recording
 * stub on both sides: the imports lseek/close/memcpy/free, operator
 * delete, the base destructor 0x47a8a4 (rows of other packages, replaced by
 * the stubs after the seal) and the inner stream's four methods (host
 * stubs in its vtable). A stub logs its letter and arguments (not the entry
 * registers: the own side enters with zero registers), does the memory
 * effect of a memcpy or an inner read (seeded bytes), and leaves seeded EAX, EDX and
 * ECX; the answers of the stream stubs are weighted so that every outcome
 * of the buffered fill (seek false, read true, read false with status 2 and
 * a tell equal to, beyond or short of the position, other statuses) and of
 * the handle stream (a failed lseek, a failed close) occurs. The handle
 * read 0x47af98 is input_layer.h's row and not in this suite.
 * Compared: EAX, EDX and ECX of every call, DATA 0x497000..0x6cc000 and the
 * heap, the stub log. Coverage: counters from the original side; every one
 * must be non-zero. Identifiers use the prefix hf_/HF_. */
#pragma once
#include "init_records_cases.h"
#include "startup_cases.h"
#include "loop_cases.h"

static es2_sim_entry hf_entries[4096];
static es2_sim_dispatch hf_dispatch;
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

/* ---- guest memory helpers (the process is the guest: addresses are pointers) */
static uint32_t hf_get(uint32_t a) { return get32((BYTE *)(uintptr_t)a); }
static uint8_t hf_get8(uint32_t a) { return *(BYTE *)(uintptr_t)a; }
static void hf_put8(uint32_t a, uint8_t v) { *(BYTE *)(uintptr_t)a = v; }
static void hf_put16(uint32_t a, uint16_t v) { put16((BYTE *)(uintptr_t)a, v); }
static void hf_put32(uint32_t a, uint32_t v) { put32((BYTE *)(uintptr_t)a, v); }
static int hf_in_region(uint32_t a, uint32_t n) {
    uint32_t base = (uint32_t)(uintptr_t)ir_region;
    return a - base < IR_REGION_SIZE && n <= IR_REGION_SIZE - (a - base);
}

/* ---- the stub log of a side, the coverage and the answers */
enum { HFC_SEEK_FALSE, HFC_READ_TRUE, HFC_READ_FALSE_END, HFC_READ_FALSE_OTHER, HFC_TELL_EQUAL, HFC_TELL_AHEAD, HFC_LSEEK_FAIL,
       HFC_CLOSE_FAIL, HFC_CLOSE_OK, HFC_ALIAS_CROSS, HFC_ALIAS_AFFINE,
       HFC_ALIAS_DIVIDE, HFC_READ_LOOP, HFC_ARENA_IN, HFC_ARENA_WILD, HFC_FREE, HFC_DELETE, HFC_BASE_DTOR, HFC_COUNT };
static const char *const hf_cv_names[HFC_COUNT] = {
    "seek false", "read true", "read false at end", "read false other", "tell equal", "tell ahead", "lseek -1", "close -1", "close ok", "cross alias", "affine alias", "divide alias", "read loop", "arena in",
    "arena wild", "free", "operator delete", "base destructor",
};
static unsigned hf_cv[HFC_COUNT];
static uint64_t hf_hash[2];
static unsigned hf_count[2];
static char hf_text[2][8192];
static size_t hf_len[2];
static uint32_t hf_answer_seed, hf_last_seek;
static unsigned hf_op_copies;
static uint32_t hf_answer(void) {
    hf_answer_seed = hf_answer_seed * 1103515245u + 12345u;
    return hf_answer_seed ^ (hf_answer_seed >> 13);
}
static void hf_cover(unsigned c) { if (!hf_side) ++hf_cv[c]; }
static unsigned hf_nargs(char letter) {
    switch (letter) {
    case 'm': case 'l': case 'R': return 3;
    case 'b': case 's': return 2;
    default: return 1;
    }
}
static void hf_note(char letter, const uint32_t *s, unsigned n) {
    uint64_t h = hf_hash[hf_side];
    uint32_t v[4] = {(uint32_t)letter, 0, 0, 0};
    for (unsigned i = 0; i < n; ++i) v[1 + i] = s[i];
    for (unsigned i = 0; i < 4u; ++i) { h ^= v[i]; h *= 0x100000001b3ull; }
    hf_hash[hf_side] = h;
    ++hf_count[hf_side];
    if (hf_len[hf_side] + 60u < sizeof(hf_text[0]))
        hf_len[hf_side] += (size_t)sprintf(hf_text[hf_side] + hf_len[hf_side], "%c(%lx,%lx,%lx) ", letter, (unsigned long)v[1],
                                           (unsigned long)v[2], (unsigned long)v[3]);
}
static void hf_fill(uint32_t a, uint32_t n) {
    if (n <= 0x1000u && hf_in_region(a, n))
        for (uint32_t k = 0; k < n; ++k) hf_put8(a + k, (uint8_t)hf_answer());
}
/* One stub body for both sides. */
static void hf_stub(char letter, const uint32_t *s, uint32_t out[3]) {
    hf_note(letter, s, hf_nargs(letter));
    out[0] = hf_answer();
    out[1] = hf_answer();
    out[2] = hf_answer();
    switch (letter) {
    case 'm':   /* memcpy(dst, src, n): the copy is done */
        if (s[2] <= 0x1000u && hf_in_region(s[0], s[2]) && hf_in_region(s[1], s[2])) memmove((void *)(uintptr_t)s[0], (void *)(uintptr_t)s[1], s[2]);
        if (!hf_side) ++hf_op_copies;
        break;
    case 'R': { /* the inner stream's read(inner, n, buf): bytes, then AL */
        hf_fill(s[2], s[1]);
        int ok = hf_answer() % 4u != 0;
        out[0] = (out[0] & 0xffffff00u) | (ok ? 1u : 0u);
        if (ok) hf_cover(HFC_READ_TRUE);
        else hf_cover(hf_get(s[0] + 4u) == 2u ? HFC_READ_FALSE_END : HFC_READ_FALSE_OTHER);
        break;
    }
    case 's': { /* seek(inner, pos): AL */
        int ok = hf_answer() % 4u != 0;
        hf_last_seek = s[1];
        out[0] = (out[0] & 0xffffff00u) | (ok ? 1u : 0u);
        if (!ok) hf_cover(HFC_SEEK_FALSE);
        break;
    }
    case 't': { /* tell(inner): the last seek plus nothing, a little, or a lot */
        uint32_t r = hf_answer() % 20u, delta = r < 7u ? 0u : r < 14u ? 1u + hf_answer() % 8u : hf_answer() % 64u;
        out[0] = hf_last_seek + delta;
        hf_cover(delta ? HFC_TELL_AHEAD : HFC_TELL_EQUAL);
        break;
    }
    case 'l': { /* _lseek */
        uint32_t r = hf_answer() % 5u;
        out[0] = r == 0 ? 0xffffffffu : hf_answer() % 1000u;
        if (r == 0) hf_cover(HFC_LSEEK_FAIL);
        break;
    }
    case 'c': { /* _close */
        int fail = hf_answer() % 10u < 3u;
        out[0] = fail ? 0xffffffffu : hf_answer() % 2u;
        hf_cover(fail ? HFC_CLOSE_FAIL : HFC_CLOSE_OK);
        break;
    }
    case 'f': hf_cover(HFC_FREE); break;
    case 'd': hf_cover(HFC_DELETE); break;
    case 'b': hf_cover(HFC_BASE_DTOR); break;
    default: break;
    }
}

/* ---- the original side's stubs: naked entries that pass the stack arguments */
uint32_t hf_reg_out[3];
void __cdecl hf_stub_c(uint32_t letter, const uint32_t *args) { hf_stub((char)letter, args, hf_reg_out); }
#define HF_STUB(name, letter)                                                                                                      \
    extern void name(void);                                                                                                        \
    __asm__(".text\n.globl _" #name "\n_" #name ":\n\tleal 4(%esp), %eax\n\tpushl %eax\n\tpushl $" #letter "\n\tcall _hf_stub_c\n"  \
            "\taddl $8, %esp\n\tmovl _hf_reg_out, %eax\n\tmovl _hf_reg_out+4, %edx\n\tmovl _hf_reg_out+8, %ecx\n\tret\n");
HF_STUB(hf_o_memcpy, 109)   /* 'm' */
HF_STUB(hf_o_lseek, 108)    /* 'l' */
HF_STUB(hf_o_close, 99)     /* 'c' */
HF_STUB(hf_o_free, 102)     /* 'f' */
HF_STUB(hf_o_delete, 100)   /* 'd' */
HF_STUB(hf_o_base, 98)      /* 'b' */
HF_STUB(hf_slot_tell, 116)  /* 't': inner slot 1 */
HF_STUB(hf_slot_seek, 115)  /* 's': inner slot 2 */
HF_STUB(hf_slot_flag, 70)   /* 'F': slot 4 */
HF_STUB(hf_slot_read, 82)   /* 'R': inner slot 6 */

static int hf_patch(uint32_t va, void *target, BYTE saved[5], int install) {
    BYTE *site = mapped + (va - 0x400000u);
    DWORD previous;
    if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &previous)) return fail("runtime leaves: stub protection");
    if (install) { memcpy(saved, site, 5); site[0] = 0xe9; put32(site + 1, (uint32_t)(uintptr_t)target - (uint32_t)(uintptr_t)(site + 5)); }
    else memcpy(site, saved, 5);
    if (!VirtualProtect(site, 5, previous, &previous)) return fail("runtime leaves: stub restore protection");
    FlushInstructionCache(GetCurrentProcess(), site, 5);
    return 0;
}
/* The callees that are rows of other packages: patched on the original
 * side, replaced after the seal on the own side. */
static const struct { uint32_t va; void *stub; char letter; } hf_patched[] = {
    {0x0049680eu, (void *)hf_o_memcpy, 'm'}, {0x00496826u, (void *)hf_o_lseek, 'l'},
    {0x00496874u, (void *)hf_o_close, 'c'}, {0x004967a8u, (void *)hf_o_free, 'f'}, {0x00477548u, (void *)hf_o_delete, 'd'},
    {0x0047a8a4u, (void *)hf_o_base, 'b'},
};
#define HF_PATCHED (sizeof(hf_patched) / sizeof(hf_patched[0]))
static BYTE hf_saved[HF_PATCHED][5];

/* ---- the own side */
static void hf_s_stub(es2_sim_service_call *call, void *user) {
    uint32_t out[3];
    hf_stub((char)(uintptr_t)user, call->slots, out);
    call->eax = out[0]; call->edx = out[1]; call->ecx = out[2];
}
static void hf_refuse(const es2_sim_refusal *r, void *user) {
    (void)user;
    fprintf(stderr, "FAIL: own runtime leaves: refused call to 0x%08lx (%s): %s\n", (unsigned long)r->va, r->name ? r->name : "?",
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
    if (es2_sim_register_services(&hf_dispatch, &s)) return fail("runtime leaves: dispatch");
    const struct { void *host; char letter; } hosts[] = {{(void *)hf_slot_tell, 't'}, {(void *)hf_slot_seek, 's'},
                                                        {(void *)hf_slot_flag, 'F'}, {(void *)hf_slot_read, 'R'}};
    for (unsigned i = 0; i < sizeof(hosts) / sizeof(hosts[0]); ++i)
        if (es2_sim_dispatch_add_service(&hf_dispatch, (uint32_t)(uintptr_t)hosts[i].host, "runtime leaves slot", hf_s_stub,
                                         (void *)(uintptr_t)hosts[i].letter, 0, (uint8_t)hf_nargs(hosts[i].letter)))
            return fail("runtime leaves: dispatch (slot)");
    const char *error = NULL;
    uint32_t bad = 0;
    if (es2_sim_dispatch_seal(&hf_dispatch, &error, &bad)) return fail("runtime leaves: dispatch seal");
    for (unsigned i = 0; i < HF_PATCHED; ++i) {
        es2_sim_entry *e = (es2_sim_entry *)es2_sim_dispatch_find(&hf_dispatch, hf_patched[i].va);
        if (!e || e->kind != ES2_SIM_SERVICE) return fail("runtime leaves: service missing");
        e->service = hf_s_stub;
        e->service_user = (void *)(uintptr_t)hf_patched[i].letter;
    }
    return 0;
}

/* ---- the synthetic world */
static uint32_t hf_seed;
static uint32_t hf_rand(uint32_t n) {
    hf_seed = hf_seed * 1103515245u + 12345u;
    return (hf_seed >> 8) % n;
}
/* A dword that likes the edges of 16- and 32-bit arithmetic. */
static uint32_t hf_val(void) {
    static const uint32_t edge[] = {0, 1, 2, 0xffffffffu, 0xfffffffeu, 0x7fffffffu, 0x80000000u, 0x80000001u, 0xffffu, 0x8000u,
                                    0x7fffu, 0xffff8000u, 0x10000u, 0xffu, 0x80u, 0xffffff80u, 0x2000u, 0x4000u, 0x3fffu};
    uint32_t r = hf_rand(8u);
    if (r < 3u) return edge[hf_rand(sizeof(edge) / sizeof(edge[0]))];
    if (r < 5u) return hf_rand(0x1000u) - 0x800u;
    return hf_rand(65536u) << 16 | hf_rand(65536u);
}
typedef struct hf_world {
    uint32_t counter, inner, ivt, dvt, b[5], buf[5], r[3], base[2], det[2], hdr, arena, wild, grid, gridbase, cells, v[6], o[4], num[3],
        dst;
    int32_t den[3];
} hf_world;
static hf_world hf_w, hf_w0;
static uint32_t hf_alloc(uint32_t n) { return (uint32_t)(uintptr_t)ir_calloc(1, n); }
static void hf_randomize(uint32_t a, uint32_t n) { for (uint32_t k = 0; k < n; ++k) hf_put8(a + k, (uint8_t)hf_rand(256u)); }
/* A 64-bit numerator and a divisor whose quotient fits 32 bits (a divide
 * fault is outside the rows). */
static void hf_division(uint32_t block, int32_t *den) {
    int32_t q = (int32_t)hf_val(), d = (int32_t)hf_val();
    if (d == 0) d = 1 + (int32_t)hf_rand(5u);
    if (q == INT32_MIN && d == -1) q = 7;
    uint64_t magnitude = d < 0 ? (uint64_t)-(int64_t)d : (uint64_t)d;
    uint64_t r = (uint64_t)hf_val() % magnitude;
    int64_t n = (int64_t)q * d;
    n = n < 0 ? n - (int64_t)r : n + (int64_t)r;
    hf_put32(block, (uint32_t)(uint64_t)n);
    hf_put32(block + 4u, (uint32_t)((uint64_t)n >> 32));
    *den = d;
}
static void hf_build(void) {
    hf_world *w = &hf_w;
    memset(w, 0, sizeof(*w));
    w->counter = hf_alloc(8u);
    hf_put32(w->counter, 1000u + hf_rand(1000u));
    put32(mapped + (0x006cbee4u - 0x400000u), w->counter);
    /* the inner stream and the vtables of host stubs */
    w->ivt = hf_alloc(0x20u);
    hf_put32(w->ivt + 4u, (uint32_t)(uintptr_t)hf_slot_tell);
    hf_put32(w->ivt + 8u, (uint32_t)(uintptr_t)hf_slot_seek);
    hf_put32(w->ivt + 0x10u, (uint32_t)(uintptr_t)hf_slot_flag);
    hf_put32(w->ivt + 0x18u, (uint32_t)(uintptr_t)hf_slot_read);
    w->inner = hf_alloc(0x20u);
    hf_randomize(w->inner, 0x20u);
    hf_put32(w->inner, w->ivt);
    static const uint32_t statuses[] = {0, 1, 2, 2, 3, 4, 5, 6};
    hf_put32(w->inner + 4u, statuses[hf_rand(8u)]);
    w->dvt = hf_alloc(0x20u);
    hf_put32(w->dvt + 0x10u, (uint32_t)(uintptr_t)hf_slot_flag);
    for (unsigned k = 0; k < 2u; ++k) {
        uint32_t d = w->det[k] = hf_alloc(0x20u);
        hf_randomize(d, 0x20u);
        hf_put32(d, w->dvt);
        hf_put32(d + 8u, hf_rand(3u) ? w->inner : 0u);
    }
    /* the buffered streams: 0..2 live, 3 and 4 destroyed or detached (4 has no inner stream) */
    for (unsigned k = 0; k < 5u; ++k) {
        uint32_t b = w->b[k] = hf_alloc(0x30u), size = 8u + 8u * hf_rand(3u), valid = hf_rand(size + 1u);
        w->buf[k] = hf_alloc(0x80u);
        hf_randomize(w->buf[k], 0x80u);
        hf_randomize(b, 0x30u);
        hf_put32(b, 0x004a1ad8u);
        static const uint32_t bs[] = {0, 0, 0, 0, 0, 1, 2, 4, 6, 3};
        hf_put32(b + 4u, bs[hf_rand(10u)]);
        hf_put32(b + 8u, k == 4u ? 0u : w->inner);
        hf_put32(b + 0xcu, size);
        hf_put32(b + 0x10u, hf_rand(200u));
        hf_put32(b + 0x14u, w->buf[k]);
        hf_put8(b + 0x18u, (uint8_t)hf_rand(2u));
        hf_put32(b + 0x19u, valid);
        hf_put32(b + 0x1du, hf_rand(8u) ? hf_rand(valid + 1u) : valid + hf_rand(3u));
    }
    for (unsigned k = 0; k < 3u; ++k) {
        uint32_t r = w->r[k] = hf_alloc(0x10u);
        static const uint16_t handles[] = {0, 3, 5, 0xffffu, 0x8001u, 7u};
        hf_randomize(r, 0x10u);
        hf_put32(r, 0x004a1738u);
        hf_put32(r + 4u, statuses[hf_rand(8u)] % 6u);
        hf_put16(r + 8u, handles[hf_rand(6u)]);
        hf_put8(r + 0xau, (uint8_t)hf_rand(2u));
    }
    for (unsigned k = 0; k < 2u; ++k) {
        uint32_t a = w->base[k] = hf_alloc(0x10u);
        hf_randomize(a, 0x10u);
        hf_put32(a, 0x004a16e8u);
    }
    /* the arena: header, region, tags at 0x10 and 0x28; a header whose end passes 2^31 */
    w->hdr = hf_alloc(0x20u);
    hf_randomize(w->hdr, 0x20u);
    w->arena = hf_alloc(0x120u);
    hf_randomize(w->arena, 0x120u);
    hf_put32(w->arena + 0x10u, 0x41424c4bu);
    hf_put32(w->arena + 0x28u, hf_rand(2u) ? 0x41424c4bu : hf_val());
    w->wild = hf_rand(4u) == 0;
    hf_put32(w->hdr, w->wild ? 0x7fffff00u : w->arena);
    hf_put32(w->hdr + 4u, w->wild ? 0x200u : 0x100u);
    w->grid = hf_alloc(0x100u);
    hf_randomize(w->grid, 0x100u);
    w->gridbase = 0x1000u + hf_rand(0x100000u);
    w->cells = hf_alloc(0x40u);
    for (unsigned k = 0; k < 0x40u; ++k) hf_put8(w->cells + k, hf_rand(4u) ? (uint8_t)hf_rand(256u) : 0xffu);
    hf_put32(w->grid + 0xecu, w->gridbase);
    hf_put32(w->grid + 0xf0u, w->cells);
    for (unsigned k = 0; k < 6u; ++k) {
        w->v[k] = hf_alloc(0x20u);
        for (unsigned j = 0; j < 8u; ++j) hf_put32(w->v[k] + 4u * j, hf_val());
    }
    for (unsigned k = 0; k < 4u; ++k) {
        w->o[k] = hf_alloc(0x20u);
        hf_randomize(w->o[k], 0x20u);
    }
    for (unsigned k = 0; k < 3u; ++k) {
        w->num[k] = hf_alloc(8u);
        hf_division(w->num[k], &w->den[k]);
    }
    w->dst = hf_alloc(0x80u);
    for (unsigned i = 0; i < 8u; ++i) hf_put32(0x006c5eb4u + 4u * i, hf_rand(2u) ? hf_val() : 0u);
}

/* ---- the rows */
enum {
    HF_ARENA_INIT, HF_ARENA_TEST, HF_FIELD, HF_CELL, HF_DOT16, HF_DOT8, HF_CROSS, HF_DOT_SIGN, HF_IDIV64, HF_IDIV64_WRAP, HF_SWAP,
    HF_IMUL64, HF_IMUL64_WRAP, HF_ISQRT, HF_ISQRT_WRAP, HF_LENGTH, HF_MUL_SHIFT, HF_AFFINE, HF_SLOT_INIT, HF_SLOT_SET, HF_CTOR_PAIR,
    HF_IDIV, HF_S_STATUS, HF_S_ZERO_X, HF_S_TRUE_8FC, HF_S_TRUE_904, HF_S_TRUE_90C, HF_S_TRUE_914, HF_DETACH_W, HF_DETACH_M,
    HF_R_MAP, HF_R_CLOSE, HF_R_TELL, HF_R_SEEK, HF_R_TRUE, HF_R_ERROR, HF_R_FAIL, HF_B_DTOR, HF_B_DETACH, HF_B_MAP, HF_B_TELL,
    HF_B_SEEK, HF_B_LIVE, HF_B_READ, HF_B_FAIL, HF_B_REFILL, HF_B_STATUS, HF_B_READ_INNER, HF_OPS
};
static const struct { uint32_t va; unsigned n; const char *name; } hf_ops[HF_OPS] = {
    {0x00476df4u, 3, "arena init"}, {0x00476e2cu, 2, "arena test"}, {0x0046b850u, 1, "field"}, {0x00470db8u, 2, "cell count"},
    {0x0047dbcau, 2, "dot words"}, {0x0047dba4u, 2, "dot bytes"}, {0x0047db4du, 3, "cross"}, {0x0047ddcdu, 2, "dot sign"},
    {0x0047de2du, 3, "idiv64"}, {0x0047eabeu, 3, "idiv64 wrap"}, {0x0047df04u, 1, "swap words"}, {0x0047df84u, 3, "imul64"},
    {0x0047eaa1u, 3, "imul64 wrap"}, {0x0047df9cu, 1, "isqrt"}, {0x0047ea92u, 1, "isqrt wrap"}, {0x0047ea64u, 1, "length"},
    {0x0047e0c0u, 2, "mul shift"}, {0x00480050u, 3, "affine"}, {0x00480590u, 3, "slot init"}, {0x004805b4u, 2, "slot set"},
    {0x0047eadbu, 1, "ctor pair"}, {0x00492b04u, 2, "idiv"}, {0x0047a874u, 1, "stream status"}, {0x0047a8d4u, 2, "stream zero x"},
    {0x0047a8fcu, 1, "true 8fc"}, {0x0047a904u, 1, "true 904"}, {0x0047a90cu, 1, "true 90c"}, {0x0047a914u, 1, "true 914"},
    {0x0047b5ccu, 1, "detach write"}, {0x0047b9ccu, 1, "detach memory"}, {0x0047adb4u, 2, "read map"}, {0x0047ae28u, 1, "read close"},
    {0x0047aeb0u, 1, "read tell"}, {0x0047aeccu, 2, "read seek"}, {0x0047af58u, 1, "read true"}, {0x0047af60u, 1, "read error"},
    {0x0047af84u, 1, "read fail"}, {0x0047c684u, 2, "buf dtor"}, {0x0047c754u, 1, "buf detach"}, {0x0047c78cu, 2, "buf map"},
    {0x0047c7bcu, 1, "buf tell"}, {0x0047c7d8u, 2, "buf seek"}, {0x0047c818u, 1, "buf live"}, {0x0047c82cu, 3, "buf read"},
    {0x0047c8b0u, 1, "buf fail"}, {0x0047c8c4u, 2, "buf refill"}, {0x0047c95cu, 1, "buf status"}, {0x0047c984u, 3, "buf read inner"},
};
typedef struct hf_op { unsigned op; uint32_t a[3], in[3]; } hf_op;
static unsigned hf_calls[HF_OPS];
static uint32_t hf_pick_v(void) { return hf_w.v[random32() % 6u]; }
static uint32_t hf_pick_b(void) { return hf_w.b[random32() % 5u]; }
static uint32_t hf_pick_live(void) { return hf_w.b[random32() % 3u]; }
static uint32_t hf_pick_dying(void) { return hf_w.b[3u + random32() % 2u]; }
static uint32_t hf_pick_r(void) { return hf_w.r[random32() % 3u]; }
static void hf_args(hf_op *o) {
    const hf_world *w = &hf_w;
    memset(o->a, 0, sizeof(o->a));
    switch (o->op) {
    case HF_ARENA_INIT: o->a[0] = w->hdr; o->a[1] = random32() % 3u ? random32() % 0x100u : random32(); o->a[2] = w->arena + 8u * (random32() % 2u); break;
    case HF_ARENA_TEST: {
        static const uint32_t offsets[] = {0x18u, 0x20u, 0x30u, 0xfffffffcu, 0x108u, 0x110u, 0x11cu};
        o->a[0] = w->hdr;
        o->a[1] = w->wild && random32() % 2u ? 0x7fffff80u : w->arena + offsets[random32() % 7u];
        break;
    }
    case HF_FIELD: o->a[0] = w->grid; break;
    case HF_CELL: o->a[0] = w->grid; o->a[1] = w->gridbase + 16u * (random32() % 0x40u) + random32() % 16u; break;
    case HF_DOT16: case HF_DOT8: case HF_DOT_SIGN: o->a[0] = hf_pick_v(); o->a[1] = hf_pick_v(); break;
    case HF_CROSS: {
        o->a[0] = hf_pick_v(); o->a[1] = hf_pick_v();
        unsigned alias = random32() % 6u;
        o->a[2] = alias == 0 ? o->a[0] : alias == 1 ? o->a[1] : w->o[random32() % 4u];
        break;
    }
    case HF_IDIV64: case HF_IDIV64_WRAP: {
        unsigned k = random32() % 3u;
        o->a[0] = random32() % 5u ? w->o[random32() % 4u] : w->num[k];
        o->a[1] = w->num[k];
        o->a[2] = (uint32_t)w->den[k];
        break;
    }
    case HF_SWAP: o->a[0] = hf_pick_v(); break;
    case HF_IMUL64: case HF_IMUL64_WRAP: o->a[0] = w->o[random32() % 4u]; o->a[1] = hf_val(); o->a[2] = hf_val(); break;
    case HF_ISQRT: case HF_ISQRT_WRAP: {
        unsigned r = random32() % 6u;
        uint32_t m = random32() % 46341u;
        o->a[0] = r == 0 ? m * m : r == 1 ? m * m - 1u : r == 2 ? random32() >> (random32() % 32u) : hf_val();
        break;
    }
    case HF_LENGTH: o->a[0] = hf_pick_v(); break;
    case HF_MUL_SHIFT: o->a[0] = hf_val(); o->a[1] = hf_val(); break;
    case HF_AFFINE: {
        o->a[0] = hf_pick_v(); o->a[1] = hf_pick_v();
        unsigned alias = random32() % 7u;
        o->a[2] = alias == 0 ? o->a[0] : alias == 1 ? o->a[1] : w->o[random32() % 4u];
        break;
    }
    case HF_SLOT_INIT: o->a[0] = w->o[random32() % 4u]; o->a[1] = random32() % 4u ? random32() % 8u : random32() % 0x300u; o->a[2] = random32() % 4u ? hf_val() : 0u; break;
    case HF_SLOT_SET: o->a[0] = random32() % 4u ? random32() % 8u : random32() % 0x300u; o->a[1] = random32() % 4u ? hf_val() : 0u; break;
    case HF_CTOR_PAIR: o->a[0] = random32(); break;
    case HF_IDIV: {
        int32_t a, b;
        do { a = (int32_t)hf_val(); b = (int32_t)hf_val(); } while (b == 0 || (a == INT32_MIN && b == -1));
        o->a[0] = (uint32_t)a; o->a[1] = (uint32_t)b;
        break;
    }
    case HF_S_STATUS: case HF_S_TRUE_8FC: case HF_S_TRUE_904: case HF_S_TRUE_90C: case HF_S_TRUE_914:
        o->a[0] = w->base[random32() % 2u]; break;
    case HF_S_ZERO_X: o->a[0] = w->base[random32() % 2u]; o->a[1] = random32() % 3u ? random32() % 8u : random32(); break;
    case HF_DETACH_W: case HF_DETACH_M: o->a[0] = w->det[random32() % 2u]; break;
    case HF_R_MAP: o->a[0] = hf_pick_r(); o->a[1] = random32() % 4u ? random32() % 6u : random32(); break;
    case HF_R_CLOSE: case HF_R_TELL: case HF_R_TRUE: case HF_R_ERROR: case HF_R_FAIL: o->a[0] = hf_pick_r(); break;
    case HF_R_SEEK: o->a[0] = hf_pick_r(); o->a[1] = random32() % 500u; break;
    case HF_B_DTOR: o->a[0] = random32() % 8u ? hf_pick_dying() : 0u; o->a[1] = random32() % 3u ? random32() % 2u : random32(); break;
    case HF_B_DETACH: o->a[0] = hf_pick_dying(); break;
    case HF_B_MAP: o->a[0] = hf_pick_b(); o->a[1] = random32() % 4u ? random32() % 6u : random32(); break;
    case HF_B_TELL: case HF_B_LIVE: case HF_B_FAIL: case HF_B_STATUS: o->a[0] = hf_pick_b(); break;
    case HF_B_SEEK: {
        o->a[0] = hf_pick_live();
        uint32_t base = hf_get(o->a[0] + 0x10u);
        o->a[1] = random32() % 5u ? base + random32() % 24u : random32() % 200u;
        break;
    }
    case HF_B_READ: o->a[0] = hf_pick_live(); o->a[1] = random32() % 40u; o->a[2] = w->dst; break;
    case HF_B_REFILL: o->a[0] = hf_pick_live(); o->a[1] = random32() % 200u; break;
    case HF_B_READ_INNER: o->a[0] = w->inner; o->a[1] = random32() % 40u; o->a[2] = w->dst; break;
    default: break;
    }
    for (unsigned k = 0; k < 3u; ++k) o->in[k] = random32();
}

static int hf_side_begin(unsigned side, uint32_t seed, uint32_t answers) {
    lc_reset(side, 47, 0x01000000u, 1);
    lc_loop.dispatch = &hf_dispatch;
    hf_side = side;
    hf_hash[side] = 0xcbf29ce484222325ull;
    hf_count[side] = 0;
    hf_len[side] = 0;
    hf_text[side][0] = 0;
    hf_answer_seed = answers;
    hf_last_seek = 0;
    hf_seed = seed;
    hf_build();
    if (side) {
        if (memcmp(&hf_w, &hf_w0, sizeof(hf_w))) return fail("runtime leaves: the sides' worlds differ (heap order)");
    } else {
        hf_w0 = hf_w;
    }
    return 0;
}
static int hf_case(unsigned k) {
    unsigned ops = 1u + random32() % 8u;
    static hf_op list[8];
    uint32_t seed = random32(), answers = random32();
    static uint32_t reg[2][8][3];
    for (unsigned i = 0; i < ops; ++i) list[i].op = random32() % HF_OPS;
    for (unsigned side = 0; side < 2u; ++side) {
        if (hf_side_begin(side, seed, answers)) return 1;
        for (unsigned i = 0; i < ops && !hf_bad && !lc_bad; ++i) {
            hf_op *o = &list[i];
            if (side == 0) {
                hf_args(o);
                hf_op_copies = 0;
                hf_call((uint32_t)(uintptr_t)(mapped + (hf_ops[o->op].va - 0x400000u)), o->a, hf_ops[o->op].n, o->in, reg[0][i]);
                ++hf_calls[o->op];
                if (o->op == HF_CROSS && (o->a[2] == o->a[0] || o->a[2] == o->a[1])) ++hf_cv[HFC_ALIAS_CROSS];
                if (o->op == HF_AFFINE && (o->a[2] == o->a[0] || o->a[2] == o->a[1])) ++hf_cv[HFC_ALIAS_AFFINE];
                if ((o->op == HF_IDIV64 || o->op == HF_IDIV64_WRAP) && o->a[0] == o->a[1]) ++hf_cv[HFC_ALIAS_DIVIDE];
                if (o->op == HF_B_READ && hf_op_copies >= 2u) ++hf_cv[HFC_READ_LOOP];
                if (o->op == HF_ARENA_TEST && reg[0][i][0] == 1u) ++hf_cv[HFC_ARENA_IN];
                if (o->op == HF_ARENA_TEST && o->a[1] == 0x7fffff80u) ++hf_cv[HFC_ARENA_WILD];
            } else {
                es2_sim_result r;
                memset(&r, 0, sizeof(r));
                if (es2_sim_call(&hf_dispatch, hf_ops[o->op].va, o->a, hf_ops[o->op].n, o->in, &r)) hf_bad = 1;
                reg[1][i][0] = r.eax; reg[1][i][1] = r.edx; reg[1][i][2] = r.ecx;
            }
        }
        if (hf_bad || lc_bad) { fprintf(stderr, "FAIL: runtime leaves case %u: a call failed (side %u)\n", k, side); return 1; }
        su_take(side);
    }
    for (unsigned i = 0; i < ops; ++i)
        if (memcmp(reg[0][i], reg[1][i], sizeof(reg[0][i]))) {
            fprintf(stderr, "FAIL: runtime leaves case %u: %s (call %u) EAX %08lx / %08lx, EDX %08lx / %08lx, ECX %08lx / %08lx\n", k,
                    hf_ops[list[i].op].name, i, (unsigned long)reg[0][i][0], (unsigned long)reg[1][i][0], (unsigned long)reg[0][i][1],
                    (unsigned long)reg[1][i][1], (unsigned long)reg[0][i][2], (unsigned long)reg[1][i][2]);
            return 1;
        }
    if (su_compare("runtime leaves", k)) {
        fprintf(stderr, "  rows:");
        for (unsigned i = 0; i < ops; ++i) fprintf(stderr, " %s", hf_ops[list[i].op].name);
        fprintf(stderr, "\n");
        return 1;
    }
    if (hf_hash[0] != hf_hash[1] || hf_count[0] != hf_count[1]) {
        fprintf(stderr, "FAIL: runtime leaves case %u: stub calls differ (%u / %u)\n  original: %s\n  own:      %s\n", k, hf_count[0], hf_count[1],
                hf_text[0], hf_text[1]);
        return 1;
    }
    return 0;
}

static int runtime_leaves_cases(void) {
    current_function = "runtime leaves";
    es2_sim_identity_memory(&ir_memory);
    es2_sim_rtl_memory(&ir_memory, &su_rtl);
    su_data_pristine = malloc(SU_DATA_SIZE);
    su_snap[0] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    su_snap[1] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    ir_region = ww_alloc(IR_REGION_SIZE);
    lc_stack = VirtualAlloc(NULL, 0x10000u, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    lc_bits = ww_alloc(LC_BITS_SIZE);
    if (!su_data_pristine || !su_snap[0] || !su_snap[1] || !ir_region || !lc_stack || !lc_bits) return fail("runtime leaves: memory");
    su_pristine_take();
    for (unsigned i = 0; i < HF_PATCHED; ++i)
        if (hf_patch(hf_patched[i].va, hf_patched[i].stub, hf_saved[i], 1)) return 1;
    if (hf_dispatch_setup()) return 1;
    int bad = 0;
    const unsigned cases = 2000;
    for (unsigned k = 0; !bad && k < cases; ++k) {
        current_case = k; current_case_valid = 1;
        bad = hf_case(k);
    }
    if (!bad) {
        printf("PASS: runtime leaves: %u cases identical\n", cases);
        printf("runtime leaves rows called:");
        for (unsigned i = 0; i < HF_OPS; ++i) printf(" %s %u%s", hf_ops[i].name, hf_calls[i], i + 1u < HF_OPS ? "," : "\n");
        printf("runtime leaves coverage:");
        for (unsigned i = 0; i < HFC_COUNT; ++i) printf(" %s %u%s", hf_cv_names[i], hf_cv[i], i + 1u < HFC_COUNT ? "," : "\n");
        for (unsigned i = 0; i < HF_OPS; ++i)
            if (!hf_calls[i]) { fprintf(stderr, "FAIL: runtime leaves: row '%s' never called\n", hf_ops[i].name); bad = 1; }
        for (unsigned i = 0; i < HFC_COUNT; ++i)
            if (!hf_cv[i]) { fprintf(stderr, "FAIL: runtime leaves: path '%s' never taken\n", hf_cv_names[i]); bad = 1; }
    }
    current_case_valid = 0;
    for (unsigned i = 0; i < HF_PATCHED; ++i) hf_patch(hf_patched[i].va, NULL, hf_saved[i], 0);
    put32(mapped + 0x2cbee4u, 0);
    memcpy(mapped + SU_DATA_RVA, su_data_pristine, SU_DATA_SIZE);
    return bad;
}
