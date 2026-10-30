/* The stream slots and the class lookup (sim/dispatch/services/file_streams.h;
 * sim/src/services_files.c): original machine code against the own rows. They
 * are the leaf slots of the volume stream (vtable 0x4a0e10: 0x473510,
 * 0x473578, 0x4735a4), the write stream (0x4a1760: 0x47b0d8, 0x47b1e4,
 * 0x47b200, 0x47b28c, 0x47b2f8 and the error leaf 0x47b294), the memory
 * stream (0x4a1788: 0x47b3d4) and the class lookup 0x47a4a0. It reuses
 * startup_cases.h's snapshots and loop_cases.h's loop, as runtime_leaves_cases.h does.
 *
 * Both sides start from the pristine DATA and an empty heap and build the
 * same seeded world: four stream objects with a random status (0..5, 2
 * weighted), a handle word (0xffff weighted) and a dword +0x1e (2
 * weighted), and the class registry: 0..3 pairs (table, count) at 0x6c5834
 * with [0x4a1690] = the pair count, tables of 0..4 entries of 12 bytes
 * whose tags are random or repeated. Then 1..8 of the rows run on it, each
 * with random entry EAX, EDX and ECX; k is 0..5 or a random dword, the
 * lookup's tag one of the registry's or a random one. The original side
 * calls the rows through an own thunk that loads and stores the three
 * registers. The only callee, _lseek 0x496826, is a recording stub on both
 * sides (patched on the original side, the RTL row's service replaced after
 * the seal on the own side): it logs its arguments and leaves seeded EAX
 * (-1 in a fifth of the calls), EDX and ECX. Compared: EAX, EDX and ECX of
 * every call, DATA 0x497000..0x6cc000 and the heap, the stub log. Coverage:
 * counters from the original side; every one must be non-zero. The error
 * status 3 of 0x47b294 is unreachable: its probe 0x4967ae is the code of an
 * import thunk, never 0. Identifiers use the prefix fs_/FS_. */
#pragma once
#include "init_records_cases.h"
#include "startup_cases.h"
#include "loop_cases.h"

static es2_sim_entry fs_entries[4096];
static es2_sim_dispatch fs_dispatch;
static int fs_bad;
static unsigned fs_side;

/* ---- the original side's register thunk: pushes the arguments, enters
 * with EAX/EDX/ECX from in[3], stores EAX/EDX/ECX into out[3]. */
extern void fs_call(uint32_t target, const uint32_t *args, uint32_t count, const uint32_t *in, uint32_t *out);
__asm__(".text\n.globl _fs_call\n_fs_call:\n\tpushl %ebp\n\tmovl %esp, %ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n"
        "\tmovl 16(%ebp), %ecx\n\tmovl 12(%ebp), %esi\n"
        "1:\n\ttestl %ecx, %ecx\n\tjz 2f\n\tdecl %ecx\n\tpushl (%esi,%ecx,4)\n\tjmp 1b\n"
        "2:\n\tmovl 20(%ebp), %ebx\n\tmovl (%ebx), %eax\n\tmovl 4(%ebx), %edx\n\tmovl 8(%ebx), %ecx\n"
        "\tcall *8(%ebp)\n"
        "\tmovl 24(%ebp), %ebx\n\tmovl %eax, (%ebx)\n\tmovl %edx, 4(%ebx)\n\tmovl %ecx, 8(%ebx)\n"
        "\tleal -12(%ebp), %esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n");

static uint32_t fs_get(uint32_t a) { return get32((BYTE *)(uintptr_t)a); }
static void fs_put8(uint32_t a, uint8_t v) { *(BYTE *)(uintptr_t)a = v; }
static void fs_put16(uint32_t a, uint16_t v) { put16((BYTE *)(uintptr_t)a, v); }
static void fs_put32(uint32_t a, uint32_t v) { put32((BYTE *)(uintptr_t)a, v); }

/* ---- the rows */
enum { FS_LOOKUP, FS_VOL_MAP, FS_VOL_TRUE, FS_VOL_FAIL, FS_W_MAP, FS_W_TELL, FS_W_SEEK, FS_W_TRUE, FS_W_ERROR, FS_W_FAIL, FS_M_MAP, FS_OPS };
static const struct { uint32_t va; unsigned n; const char *name; } fs_ops[FS_OPS] = {
    {0x0047a4a0u, 1, "class lookup"}, {0x00473510u, 2, "volume map"}, {0x00473578u, 1, "volume true"},
    {0x004735a4u, 1, "volume fail"}, {0x0047b0d8u, 2, "write map"}, {0x0047b1e4u, 1, "write tell"},
    {0x0047b200u, 2, "write seek"}, {0x0047b28cu, 1, "write true"}, {0x0047b294u, 1, "write error"},
    {0x0047b2f8u, 1, "write fail"}, {0x0047b3d4u, 2, "memory map"},
};

/* ---- coverage */
enum { FSC_FOUND, FSC_MISSING, FSC_NO_PAIRS, FSC_EMPTY_TABLE, FSC_VOL_K0, FSC_VOL_K1, FSC_VOL_K23, FSC_VOL_K4, FSC_VOL_KX,
       FSC_VOL_NONE_K0, FSC_VOL_NONE_K1, FSC_VOL_NONE_K23, FSC_VOL_NONE_K4_2, FSC_VOL_NONE_K4_X, FSC_VOL_NONE_KX, FSC_W_MAP_IN,
       FSC_W_MAP_OUT, FSC_M_MAP_0, FSC_M_MAP_13, FSC_M_MAP_4, FSC_M_MAP_X, FSC_SEEK_STATUS2, FSC_SEEK_STATUS_OTHER, FSC_SEEK_FAIL,
       FSC_SEEK_OK, FSC_COUNT };
static const char *const fs_cv_names[FSC_COUNT] = {
    "lookup found", "lookup missing", "no pairs", "empty table", "vol k0", "vol k1", "vol k2/3", "vol k4", "vol k>4",
    "vol none k0", "vol none k1", "vol none k2/3", "vol none k4 =2", "vol none k4 !=2", "vol none k>4", "write map 0..4",
    "write map >4", "memory map 0", "memory map 1..3", "memory map 4", "memory map >4", "seek status 2", "seek status other",
    "seek lseek -1", "seek ok",
};
static unsigned fs_cv[FSC_COUNT];

/* ---- the stub log of a side */
static uint64_t fs_hash[2];
static unsigned fs_count[2];
static char fs_text[2][4096];
static size_t fs_len[2];
static uint32_t fs_answer_seed;
static int fs_lseek_failed;
static uint32_t fs_answer(void) {
    fs_answer_seed = fs_answer_seed * 1103515245u + 12345u;
    return fs_answer_seed ^ (fs_answer_seed >> 13);
}
/* _lseek(handle, offset, whence) on both sides. */
static void fs_stub(const uint32_t *s, uint32_t out[3]) {
    uint64_t h = fs_hash[fs_side];
    for (unsigned i = 0; i < 3u; ++i) { h ^= s[i]; h *= 0x100000001b3ull; }
    fs_hash[fs_side] = h;
    ++fs_count[fs_side];
    if (fs_len[fs_side] + 40u < sizeof(fs_text[0]))
        fs_len[fs_side] += (size_t)sprintf(fs_text[fs_side] + fs_len[fs_side], "l(%lx,%lx,%lx) ", (unsigned long)s[0],
                                           (unsigned long)s[1], (unsigned long)s[2]);
    int fail_now = fs_answer() % 5u == 0;
    out[0] = fail_now ? 0xffffffffu : fs_answer() % 1000u;
    out[1] = fs_answer();
    out[2] = fs_answer();
    if (!fs_side) fs_lseek_failed = fail_now;
}
uint32_t fs_reg_out[3];
void __cdecl fs_stub_c(const uint32_t *args) { fs_stub(args, fs_reg_out); }
extern void fs_o_lseek(void);
__asm__(".text\n.globl _fs_o_lseek\n_fs_o_lseek:\n\tleal 4(%esp), %eax\n\tpushl %eax\n\tcall _fs_stub_c\n"
        "\taddl $4, %esp\n\tmovl _fs_reg_out, %eax\n\tmovl _fs_reg_out+4, %edx\n\tmovl _fs_reg_out+8, %ecx\n\tret\n");

static BYTE fs_saved[5];
static int fs_patch(uint32_t va, void *target, int install) {
    BYTE *site = mapped + (va - 0x400000u);
    DWORD previous;
    if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &previous)) return fail("file streams: stub protection");
    if (install) { memcpy(fs_saved, site, 5); site[0] = 0xe9; put32(site + 1, (uint32_t)(uintptr_t)target - (uint32_t)(uintptr_t)(site + 5)); }
    else memcpy(site, fs_saved, 5);
    if (!VirtualProtect(site, 5, previous, &previous)) return fail("file streams: stub restore protection");
    FlushInstructionCache(GetCurrentProcess(), site, 5);
    return 0;
}

/* ---- the own side */
static void fs_s_stub(es2_sim_service_call *call, void *user) {
    (void)user;
    uint32_t out[3];
    fs_stub(call->slots, out);
    call->eax = out[0]; call->edx = out[1]; call->ecx = out[2];
}
static void fs_refuse(const es2_sim_refusal *r, void *user) {
    (void)user;
    fprintf(stderr, "FAIL: own file streams: refused call to 0x%08lx (%s): %s\n", (unsigned long)r->va, r->name ? r->name : "?",
            r->reason ? r->reason : "");
    fs_bad = 1;
}
static int fs_dispatch_setup(void) {
    es2_sim_dispatch_init(&fs_dispatch, fs_entries, 4096, fs_refuse, NULL);
    static es2_sim_services s;
    s.memory = &ir_memory;
    s.heap = &ir_heap;
    s.vt = &lc_vt;
    s.fs0 = NULL;
    if (es2_sim_register_services(&fs_dispatch, &s)) return fail("file streams: dispatch");
    const char *error = NULL;
    uint32_t bad = 0;
    if (es2_sim_dispatch_seal(&fs_dispatch, &error, &bad)) return fail("file streams: dispatch seal");
    es2_sim_entry *e = (es2_sim_entry *)es2_sim_dispatch_find(&fs_dispatch, 0x00496826u);
    if (!e || e->kind != ES2_SIM_SERVICE) return fail("file streams: lseek service missing");
    e->service = fs_s_stub;
    e->service_user = NULL;
    return 0;
}

/* ---- the synthetic world */
static uint32_t fs_seed;
static uint32_t fs_rand(uint32_t n) {
    fs_seed = fs_seed * 1103515245u + 12345u;
    return (fs_seed >> 8) % n;
}
typedef struct fs_world {
    uint32_t stream[4], pairs, table[3], count[3], tags[12];
    unsigned tag_count;
} fs_world;
static fs_world fs_w, fs_w0;
static uint32_t fs_alloc(uint32_t n) { return (uint32_t)(uintptr_t)ir_calloc(1, n); }
static void fs_build(void) {
    fs_world *w = &fs_w;
    memset(w, 0, sizeof(*w));
    static const uint32_t vtables[4] = {0x004a0e10u, 0x004a1760u, 0x004a1788u, 0x004a1760u};
    static const uint32_t statuses[] = {0, 0, 0, 1, 2, 2, 3, 4, 5};
    static const uint16_t handles[] = {0xffffu, 0xffffu, 0, 3, 5, 0x8001u};
    for (unsigned k = 0; k < 4u; ++k) {
        uint32_t o = w->stream[k] = fs_alloc(0x30u);
        for (uint32_t i = 0; i < 0x30u; ++i) fs_put8(o + i, (uint8_t)fs_rand(256u));
        fs_put32(o, vtables[k]);
        fs_put32(o + 4u, statuses[fs_rand(9u)]);
        fs_put16(o + 8u, handles[fs_rand(6u)]);
        fs_put32(o + 0x1eu, fs_rand(3u) ? 2u : fs_rand(2u) ? fs_rand(5u) : (fs_rand(65536u) << 16 | fs_rand(65536u)));
    }
    w->pairs = fs_rand(4u);
    fs_put32(0x004a1690u, w->pairs);
    for (unsigned i = 0; i < w->pairs; ++i) {
        w->count[i] = fs_rand(5u);
        w->table[i] = fs_alloc(12u * (w->count[i] + 1u));
        for (uint32_t k = 0; k < 12u * (w->count[i] + 1u); ++k) fs_put8(w->table[i] + k, (uint8_t)fs_rand(256u));
        for (uint32_t k = 0; k < w->count[i]; ++k) {
            uint32_t tag = w->tag_count && fs_rand(4u) == 0 ? w->tags[fs_rand(w->tag_count)] : 0x41000000u + fs_rand(0x1000u);
            fs_put32(w->table[i] + 12u * k, tag);
            if (w->tag_count < 12u) w->tags[w->tag_count++] = tag;
        }
        fs_put32(0x006c5834u + 8u * i, w->table[i]);
        fs_put32(0x006c5838u + 8u * i, w->count[i]);
    }
}

typedef struct fs_op { unsigned op; uint32_t a[2], in[3]; } fs_op;
static unsigned fs_calls[FS_OPS];
static uint32_t fs_k(void) { return random32() % 6u ? random32() % 7u : random32(); }
static void fs_args(fs_op *o) {
    const fs_world *w = &fs_w;
    o->a[0] = w->stream[random32() % 4u];
    o->a[1] = 0;
    switch (o->op) {
    case FS_LOOKUP: o->a[0] = w->tag_count && random32() % 3u ? w->tags[random32() % w->tag_count] : 0x41000000u + random32() % 0x1000u; break;
    case FS_VOL_MAP: case FS_W_MAP: case FS_M_MAP: o->a[1] = fs_k(); break;
    case FS_W_SEEK: o->a[1] = random32() % 4u ? random32() % 500u : random32(); break;
    default: break;
    }
    for (unsigned k = 0; k < 3u; ++k) o->in[k] = random32();
}
static void fs_cover(const fs_op *o, const uint32_t before[2]) {
    const fs_world *w = &fs_w;
    uint32_t k = o->a[1];
    switch (o->op) {
    case FS_LOOKUP: {
        int found = 0, empty = 0;
        for (unsigned i = 0; i < w->pairs; ++i) {
            if (!w->count[i]) empty = 1;
            for (uint32_t j = 0; j < w->count[i]; ++j) if (fs_get(w->table[i] + 12u * j) == o->a[0]) found = 1;
        }
        ++fs_cv[found ? FSC_FOUND : FSC_MISSING];
        if (!w->pairs) ++fs_cv[FSC_NO_PAIRS];
        if (empty) ++fs_cv[FSC_EMPTY_TABLE];
        break;
    }
    case FS_VOL_MAP: {
        int none = (before[0] & 0xffffu) == 0xffffu;
        unsigned c = k == 0 ? 0 : k == 1u ? 1 : k <= 3u ? 2 : k == 4u ? 3 : 4;
        if (!none) ++fs_cv[FSC_VOL_K0 + c];
        else if (c < 3u) ++fs_cv[FSC_VOL_NONE_K0 + c];
        else if (c == 3u) ++fs_cv[before[1] == 2u ? FSC_VOL_NONE_K4_2 : FSC_VOL_NONE_K4_X];
        else ++fs_cv[FSC_VOL_NONE_KX];
        break;
    }
    case FS_W_MAP: ++fs_cv[k <= 4u ? FSC_W_MAP_IN : FSC_W_MAP_OUT]; break;
    case FS_M_MAP: ++fs_cv[k == 0 ? FSC_M_MAP_0 : k <= 3u ? FSC_M_MAP_13 : k == 4u ? FSC_M_MAP_4 : FSC_M_MAP_X]; break;
    case FS_W_SEEK:
        if (before[0] == 2u) ++fs_cv[FSC_SEEK_STATUS2];
        else if (before[0]) { ++fs_cv[FSC_SEEK_STATUS_OTHER]; break; }
        ++fs_cv[fs_lseek_failed ? FSC_SEEK_FAIL : FSC_SEEK_OK];
        break;
    default: break;
    }
}

static int fs_side_begin(unsigned side, uint32_t seed, uint32_t answers) {
    lc_reset(side, 47, 0x01000000u, 1);
    lc_loop.dispatch = &fs_dispatch;
    fs_side = side;
    fs_hash[side] = 0xcbf29ce484222325ull;
    fs_count[side] = 0;
    fs_len[side] = 0;
    fs_text[side][0] = 0;
    fs_answer_seed = answers;
    fs_seed = seed;
    fs_build();
    if (side) {
        if (memcmp(&fs_w, &fs_w0, sizeof(fs_w))) return fail("file streams: the sides' worlds differ (heap order)");
    } else {
        fs_w0 = fs_w;
    }
    return 0;
}
static int fs_case(unsigned k) {
    unsigned ops = 1u + random32() % 8u;
    static fs_op list[8];
    uint32_t seed = random32(), answers = random32();
    static uint32_t reg[2][8][3];
    for (unsigned i = 0; i < ops; ++i) list[i].op = random32() % FS_OPS;
    for (unsigned side = 0; side < 2u; ++side) {
        if (fs_side_begin(side, seed, answers)) return 1;
        for (unsigned i = 0; i < ops && !fs_bad && !lc_bad; ++i) {
            fs_op *o = &list[i];
            if (side == 0) {
                fs_args(o);
                uint32_t before[2] = {0, 0};
                if (o->op != FS_LOOKUP) {
                    before[0] = o->op == FS_W_SEEK ? fs_get(o->a[0] + 4u) : fs_get(o->a[0] + 8u);
                    before[1] = fs_get(o->a[0] + 0x1eu);
                }
                fs_lseek_failed = 0;
                fs_call((uint32_t)(uintptr_t)(mapped + (fs_ops[o->op].va - 0x400000u)), o->a, fs_ops[o->op].n, o->in, reg[0][i]);
                ++fs_calls[o->op];
                fs_cover(o, before);
            } else {
                es2_sim_result r;
                memset(&r, 0, sizeof(r));
                if (es2_sim_call(&fs_dispatch, fs_ops[o->op].va, o->a, fs_ops[o->op].n, o->in, &r)) fs_bad = 1;
                reg[1][i][0] = r.eax; reg[1][i][1] = r.edx; reg[1][i][2] = r.ecx;
            }
        }
        if (fs_bad || lc_bad) { fprintf(stderr, "FAIL: file streams case %u: a call failed (side %u)\n", k, side); return 1; }
        su_take(side);
    }
    for (unsigned i = 0; i < ops; ++i)
        if (memcmp(reg[0][i], reg[1][i], sizeof(reg[0][i]))) {
            fprintf(stderr, "FAIL: file streams case %u: %s (call %u) EAX %08lx / %08lx, EDX %08lx / %08lx, ECX %08lx / %08lx\n", k,
                    fs_ops[list[i].op].name, i, (unsigned long)reg[0][i][0], (unsigned long)reg[1][i][0], (unsigned long)reg[0][i][1],
                    (unsigned long)reg[1][i][1], (unsigned long)reg[0][i][2], (unsigned long)reg[1][i][2]);
            return 1;
        }
    if (su_compare("file streams", k)) {
        fprintf(stderr, "  rows:");
        for (unsigned i = 0; i < ops; ++i) fprintf(stderr, " %s", fs_ops[list[i].op].name);
        fprintf(stderr, "\n");
        return 1;
    }
    if (fs_hash[0] != fs_hash[1] || fs_count[0] != fs_count[1]) {
        fprintf(stderr, "FAIL: file streams case %u: stub calls differ (%u / %u)\n  original: %s\n  own:      %s\n", k, fs_count[0],
                fs_count[1], fs_text[0], fs_text[1]);
        return 1;
    }
    return 0;
}

static int file_streams_cases(void) {
    current_function = "file streams";
    es2_sim_identity_memory(&ir_memory);
    es2_sim_rtl_memory(&ir_memory, &su_rtl);
    su_data_pristine = malloc(SU_DATA_SIZE);
    su_snap[0] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    su_snap[1] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    ir_region = ww_alloc(IR_REGION_SIZE);
    lc_stack = VirtualAlloc(NULL, 0x10000u, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    lc_bits = ww_alloc(LC_BITS_SIZE);
    if (!su_data_pristine || !su_snap[0] || !su_snap[1] || !ir_region || !lc_stack || !lc_bits) return fail("file streams: memory");
    su_pristine_take();
    if (fs_patch(0x00496826u, (void *)fs_o_lseek, 1)) return 1;
    if (fs_dispatch_setup()) return 1;
    int bad = 0;
    const unsigned cases = 3000;
    for (unsigned k = 0; !bad && k < cases; ++k) {
        current_case = k; current_case_valid = 1;
        bad = fs_case(k);
    }
    if (!bad) {
        printf("PASS: file streams: %u cases identical\n", cases);
        printf("File streams rows called:");
        for (unsigned i = 0; i < FS_OPS; ++i) printf(" %s %u%s", fs_ops[i].name, fs_calls[i], i + 1u < FS_OPS ? "," : "\n");
        printf("File streams coverage:");
        for (unsigned i = 0; i < FSC_COUNT; ++i) printf(" %s %u%s", fs_cv_names[i], fs_cv[i], i + 1u < FSC_COUNT ? "," : "\n");
        for (unsigned i = 0; i < FS_OPS; ++i)
            if (!fs_calls[i]) { fprintf(stderr, "FAIL: file streams: row '%s' never called\n", fs_ops[i].name); bad = 1; }
        for (unsigned i = 0; i < FSC_COUNT; ++i)
            if (!fs_cv[i]) { fprintf(stderr, "FAIL: file streams: path '%s' never taken\n", fs_cv_names[i]); bad = 1; }
    }
    current_case_valid = 0;
    fs_patch(0x00496826u, NULL, 0);
    memcpy(mapped + SU_DATA_RVA, su_data_pristine, SU_DATA_SIZE);
    return bad;
}
