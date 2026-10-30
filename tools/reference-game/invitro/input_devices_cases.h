/* The joystick virtuals (sim/dispatch/services/input_devices.h;
 * sim/src/input.c): original machine code against the own rows. They are
 * the joystick vtable 0x49ec74's destructor 0x45c2e0 (with 0x477630 and
 * 0x477714 as the original's code) and its poll 0x45c330 (with 0x47785c,
 * 0x4778a8, 0x47e068 and 0x47e08d as the original's code). It reuses
 * startup_cases.h's snapshots and loop_cases.h's loop, as joystick_prefs_cases.h does.
 *
 * Both sides start from the pristine DATA and an empty heap and build the
 * same seeded world: a construction counter behind 0x6cbee4, the slot table
 * 0x6c5eb4 (zero or not), the current joystick id 0x4d24a4, the last poll
 * result 0x4d2449, and three joystick objects of 0x30 random bytes with an
 * id 0..7 at +0x20 (often the current one), a slot 0..7 at +0x18, the
 * flag dword +0 (bit 0 either way), axis dwords at +4..+0x13 (raw 0..0xffff
 * or edge values), button and hat bytes +0x14/+0x15, the axis bits word
 * +0x16 (any bits whose dead-zone square is 0 are left out: the original
 * faults in 0x47e068) and the mode +0x28 (0, 1, 2 weighted, or random).
 * Then 1..8 of the rows run on it, each with random entry EAX, EDX and
 * ECX; the destructor gets one of the objects or 0 and random flags. The
 * original side calls the rows through an own thunk that zeroes 1 KiB
 * below its stack pointer first (the poll's local is unwritten for a mode
 * above 2 or a clear flag bit 0; the own model is 0) and then loads and
 * stores the three registers. The callees outside the rows are recording
 * stubs on both sides (patched on the original side, the RTL rows' services
 * replaced after the seal on the own side): _memcmp (AL zero in a third of
 * the calls), _memcpy (does the copy) and operator delete 0x477548; each
 * logs its letter and arguments and leaves seeded EAX, EDX and ECX.
 * Compared: EAX, EDX and ECX of every call, DATA 0x497000..0x6cc000 and the
 * heap, the stub log. Coverage: counters from the original side; every one
 * must be non-zero. Identifiers use the prefix jd_/JD_. */
#pragma once
#include "init_records_cases.h"
#include "startup_cases.h"
#include "loop_cases.h"
#include "es2/sim/input.h"

static es2_sim_entry jd_entries[4096];
static es2_sim_dispatch jd_dispatch;
static es2_sim_input jd_input;
static int jd_bad;
static unsigned jd_side;

/* ---- the original side's register thunk: zeroes 1 KiB below ESP, pushes
 * the arguments, enters with EAX/EDX/ECX from in[3], stores EAX/EDX/ECX
 * into out[3]. */
extern void jd_call(uint32_t target, const uint32_t *args, uint32_t count, const uint32_t *in, uint32_t *out);
__asm__(".text\n.globl _jd_call\n_jd_call:\n\tpushl %ebp\n\tmovl %esp, %ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n"
        "\tsubl $1024, %esp\n\tmovl %esp, %edi\n\txorl %eax, %eax\n\tmovl $256, %ecx\n\tcld\n\trep stosl\n\taddl $1024, %esp\n"
        "\tmovl 16(%ebp), %ecx\n\tmovl 12(%ebp), %esi\n"
        "1:\n\ttestl %ecx, %ecx\n\tjz 2f\n\tdecl %ecx\n\tpushl (%esi,%ecx,4)\n\tjmp 1b\n"
        "2:\n\tmovl 20(%ebp), %ebx\n\tmovl (%ebx), %eax\n\tmovl 4(%ebx), %edx\n\tmovl 8(%ebx), %ecx\n"
        "\tcall *8(%ebp)\n"
        "\tmovl 24(%ebp), %ebx\n\tmovl %eax, (%ebx)\n\tmovl %edx, 4(%ebx)\n\tmovl %ecx, 8(%ebx)\n"
        "\tleal -12(%ebp), %esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n");

static uint32_t jd_get(uint32_t a) { return get32((BYTE *)(uintptr_t)a); }
static void jd_put8(uint32_t a, uint8_t v) { *(BYTE *)(uintptr_t)a = v; }
static void jd_put16(uint32_t a, uint16_t v) { put16((BYTE *)(uintptr_t)a, v); }
static void jd_put32(uint32_t a, uint32_t v) { put32((BYTE *)(uintptr_t)a, v); }

/* ---- coverage */
enum { JDC_DTOR_NULL, JDC_DTOR_CURRENT, JDC_DTOR_OTHER, JDC_SLOT_SET, JDC_SLOT_CLEAR, JDC_DELETE, JDC_KEEP, JDC_MODE0, JDC_MODE1,
       JDC_MODE2, JDC_MODE_OTHER, JDC_UNSCALED, JDC_CURVE_ZERO, JDC_CURVE_POS, JDC_CURVE_NEG, JDC_DEAD, JDC_LIVE_POS, JDC_LIVE_NEG,
       JDC_CHANGED, JDC_SAME, JDC_COUNT };
static const char *const jd_cv_names[JDC_COUNT] = {
    "dtor null", "dtor current id", "dtor other id", "slot set", "slot clear", "delete", "no delete", "mode 0", "mode 1",
    "mode 2", "mode other", "flag bit 0 clear", "curve dead zone", "curve positive", "curve negative", "dead zone", "live positive",
    "live negative", "changed", "unchanged",
};
static unsigned jd_cv[JDC_COUNT];

/* ---- the stub log of a side */
static uint64_t jd_hash[2];
static unsigned jd_count[2];
static char jd_text[2][4096];
static size_t jd_len[2];
static uint32_t jd_answer_seed;
static int jd_changed;
static uint32_t jd_answer(void) {
    jd_answer_seed = jd_answer_seed * 1103515245u + 12345u;
    return jd_answer_seed ^ (jd_answer_seed >> 13);
}
static void jd_stub(char letter, const uint32_t *s, uint32_t out[3]) {
    unsigned n = letter == 'd' ? 1u : 3u;
    uint64_t h = jd_hash[jd_side];
    h ^= (uint32_t)letter; h *= 0x100000001b3ull;
    for (unsigned i = 0; i < n; ++i) { h ^= s[i]; h *= 0x100000001b3ull; }
    jd_hash[jd_side] = h;
    ++jd_count[jd_side];
    if (jd_len[jd_side] + 40u < sizeof(jd_text[0]))
        jd_len[jd_side] += (size_t)sprintf(jd_text[jd_side] + jd_len[jd_side], "%c(%lx,%lx,%lx) ", letter, (unsigned long)s[0],
                                           (unsigned long)(n > 1u ? s[1] : 0), (unsigned long)(n > 2u ? s[2] : 0));
    out[0] = jd_answer();
    out[1] = jd_answer();
    out[2] = jd_answer();
    if (letter == 'c') {
        int changed = jd_answer() % 3u != 0;
        out[0] = (out[0] & 0xffffff00u) | (changed ? 1u + jd_answer() % 255u : 0u);
        if (!jd_side) jd_changed = changed;
    } else if (letter == 'm' && s[2] <= 0x100u) {
        memmove((void *)(uintptr_t)s[0], (void *)(uintptr_t)s[1], s[2]);
    }
}
uint32_t jd_reg_out[3];
void __cdecl jd_stub_c(uint32_t letter, const uint32_t *args) { jd_stub((char)letter, args, jd_reg_out); }
#define JD_STUB(name, letter)                                                                                                      \
    extern void name(void);                                                                                                        \
    __asm__(".text\n.globl _" #name "\n_" #name ":\n\tleal 4(%esp), %eax\n\tpushl %eax\n\tpushl $" #letter "\n\tcall _jd_stub_c\n"  \
            "\taddl $8, %esp\n\tmovl _jd_reg_out, %eax\n\tmovl _jd_reg_out+4, %edx\n\tmovl _jd_reg_out+8, %ecx\n\tret\n");
JD_STUB(jd_o_memcmp, 99)   /* 'c' */
JD_STUB(jd_o_memcpy, 109)  /* 'm' */
JD_STUB(jd_o_delete, 100)  /* 'd' */

static int jd_patch(uint32_t va, void *target, BYTE saved[5], int install) {
    BYTE *site = mapped + (va - 0x400000u);
    DWORD previous;
    if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &previous)) return fail("input devices: stub protection");
    if (install) { memcpy(saved, site, 5); site[0] = 0xe9; put32(site + 1, (uint32_t)(uintptr_t)target - (uint32_t)(uintptr_t)(site + 5)); }
    else memcpy(site, saved, 5);
    if (!VirtualProtect(site, 5, previous, &previous)) return fail("input devices: stub restore protection");
    FlushInstructionCache(GetCurrentProcess(), site, 5);
    return 0;
}
static const struct { uint32_t va; void *stub; char letter; } jd_patched[] = {
    {0x004967bau, (void *)jd_o_memcmp, 'c'}, {0x0049680eu, (void *)jd_o_memcpy, 'm'}, {0x00477548u, (void *)jd_o_delete, 'd'},
};
#define JD_PATCHED (sizeof(jd_patched) / sizeof(jd_patched[0]))
static BYTE jd_saved[JD_PATCHED][5];

/* ---- the own side */
static void jd_s_stub(es2_sim_service_call *call, void *user) {
    uint32_t out[3];
    jd_stub((char)(uintptr_t)user, call->slots, out);
    call->eax = out[0]; call->edx = out[1]; call->ecx = out[2];
}
static void jd_refuse(const es2_sim_refusal *r, void *user) {
    (void)user;
    fprintf(stderr, "FAIL: own input devices: refused call to 0x%08lx (%s): %s\n", (unsigned long)r->va, r->name ? r->name : "?",
            r->reason ? r->reason : "");
    jd_bad = 1;
}
static int jd_dispatch_setup(void) {
    es2_sim_dispatch_init(&jd_dispatch, jd_entries, 4096, jd_refuse, NULL);
    static es2_sim_services s;
    s.memory = &ir_memory;
    s.heap = &ir_heap;
    s.vt = &lc_vt;
    s.fs0 = NULL;
    if (es2_sim_register_services(&jd_dispatch, &s)) return fail("input devices: dispatch");
    const char *error = NULL;
    uint32_t bad = 0;
    if (es2_sim_dispatch_seal(&jd_dispatch, &error, &bad)) return fail("input devices: dispatch seal");
    for (unsigned i = 0; i < JD_PATCHED; ++i) {
        es2_sim_entry *e = (es2_sim_entry *)es2_sim_dispatch_find(&jd_dispatch, jd_patched[i].va);
        if (!e || e->kind != ES2_SIM_SERVICE) return fail("input devices: service missing");
        e->service = jd_s_stub;
        e->service_user = (void *)(uintptr_t)jd_patched[i].letter;
    }
    return 0;
}

/* ---- the synthetic world */
static uint32_t jd_seed;
static uint32_t jd_rand(uint32_t n) {
    jd_seed = jd_seed * 1103515245u + 12345u;
    return (jd_seed >> 8) % n;
}
static uint32_t jd_axis(void) {
    static const uint32_t edge[] = {0, 1, 0x7fffu, 0x8000u, 0xffffu, 0x10000u, 0xffffffffu, 0x80000000u, 0x7fffffffu, 0x3000u, 0xd000u};
    uint32_t r = jd_rand(6u);
    if (r < 4u) return jd_rand(0x10000u);
    if (r < 5u) return edge[jd_rand(sizeof(edge) / sizeof(edge[0]))];
    return jd_rand(65536u) << 16 | jd_rand(65536u);
}
/* The poll's divisor 0x47e068(1, square) faults for a zero square. */
static int jd_bits_fault(uint32_t bits) {
    uint32_t e = (1u << (bits & 0x1fu)) - 0x19u;
    return ((int32_t)(e * e) >> ((bits + 1u) & 0x1fu)) == 0;
}
typedef struct jd_world { uint32_t counter, j[3]; } jd_world;
static jd_world jd_w, jd_w0;
static uint32_t jd_alloc(uint32_t n) { return (uint32_t)(uintptr_t)ir_calloc(1, n); }
static void jd_build(void) {
    jd_world *w = &jd_w;
    memset(w, 0, sizeof(*w));
    w->counter = jd_alloc(8u);
    jd_put32(w->counter, 1000u + jd_rand(1000u));
    put32(mapped + (0x006cbee4u - 0x400000u), w->counter);
    for (unsigned i = 0; i < 8u; ++i) jd_put32(0x006c5eb4u + 4u * i, jd_rand(2u) ? jd_rand(65536u) << 16 | jd_rand(65536u) : 0u);
    uint32_t current = jd_rand(8u);
    jd_put32(0x004d24a4u, current);
    for (unsigned i = 0; i < 0x1du; ++i) jd_put8(0x004d2449u + i, (uint8_t)jd_rand(256u));
    static const uint32_t modes[] = {0, 0, 1, 1, 2, 2, 3, 7};
    static const uint16_t bit_set[] = {16, 16, 16, 15, 12, 10, 8, 7, 6, 4, 0, 20, 23, 24, 31};
    for (unsigned k = 0; k < 3u; ++k) {
        uint32_t j = w->j[k] = jd_alloc(0x30u);
        for (uint32_t i = 0; i < 0x30u; ++i) jd_put8(j + i, (uint8_t)jd_rand(256u));
        jd_put32(j, jd_rand(4u) ? jd_get(j) | 1u : jd_get(j) & ~1u);
        for (uint32_t a = 0; a < 4u; ++a) jd_put32(j + 4u + 4u * a, jd_axis());
        uint16_t bits;
        do bits = jd_rand(5u) ? bit_set[jd_rand(sizeof(bit_set) / sizeof(bit_set[0]))] : (uint16_t)jd_rand(65536u);
        while (jd_bits_fault(bits));
        jd_put16(j + 0x16u, bits);
        jd_put32(j + 0x18u, jd_rand(8u));
        jd_put32(j + 0x20u, jd_rand(3u) ? current : jd_rand(8u));
        jd_put32(j + 0x24u, 0x0049ec74u);
        jd_put32(j + 0x28u, jd_rand(8u) ? modes[jd_rand(8u)] : jd_rand(65536u) << 16 | jd_rand(65536u));
    }
}

enum { JD_DTOR, JD_POLL, JD_OPS };
static const struct { uint32_t va; unsigned n; const char *name; } jd_ops[JD_OPS] = {
    {0x0045c2e0u, 2, "joystick dtor"}, {0x0045c330u, 1, "joystick poll"},
};
typedef struct jd_op { unsigned op; uint32_t a[2], in[3]; } jd_op;
static unsigned jd_calls[JD_OPS];
static void jd_args(jd_op *o) {
    o->a[0] = o->op == JD_DTOR && random32() % 8u == 0 ? 0u : jd_w.j[random32() % 3u];
    o->a[1] = random32() % 3u ? random32() % 4u : random32();
    for (unsigned k = 0; k < 3u; ++k) o->in[k] = random32();
}
/* The poll's per-axis paths from the object before the call. */
static void jd_cover_poll(uint32_t j) {
    uint32_t mode = jd_get(j + 0x28u);
    ++jd_cv[mode == 0 ? JDC_MODE0 : mode == 1u ? JDC_MODE1 : mode == 2u ? JDC_MODE2 : JDC_MODE_OTHER];
    if (mode > 2u) return;
    int scaled = jd_get(j) & 1u;
    if (!scaled) ++jd_cv[JDC_UNSCALED];
    for (uint32_t a = 0; a < 4u; ++a) {
        int32_t v = scaled ? (int32_t)(jd_get(j + 4u + 4u * a) << 8) / 0xffff - 0x80 : 0;
        if (mode == 2u && scaled) v = v > 0 ? 1 : v < 0 ? -1 : 0;
        if (mode == 1u) {
            int32_t w = (int16_t)(uint16_t)v;
            ++jd_cv[w < 0x19 && w > -0x19 ? JDC_CURVE_ZERO : w < 0 ? JDC_CURVE_NEG : JDC_CURVE_POS];
        } else {
            ++jd_cv[v > 0x19 ? JDC_LIVE_POS : v < -0x19 ? JDC_LIVE_NEG : JDC_DEAD];
        }
    }
}

static int jd_side_begin(unsigned side, uint32_t seed, uint32_t answers) {
    lc_reset(side, 47, 0x01000000u, 1);
    lc_loop.dispatch = &jd_dispatch;
    jd_side = side;
    jd_hash[side] = 0xcbf29ce484222325ull;
    jd_count[side] = 0;
    jd_len[side] = 0;
    jd_text[side][0] = 0;
    jd_answer_seed = answers;
    jd_seed = seed;
    jd_build();
    if (side) {
        if (memcmp(&jd_w, &jd_w0, sizeof(jd_w))) return fail("input devices: the sides' worlds differ (heap order)");
        es2_sim_input_headless(&jd_input, &lc_loop, NULL, 640, 480);
        es2_sim_input_bind(&jd_input);
    } else {
        jd_w0 = jd_w;
    }
    return 0;
}
static int jd_case(unsigned k) {
    unsigned ops = 1u + random32() % 8u;
    static jd_op list[8];
    uint32_t seed = random32(), answers = random32();
    static uint32_t reg[2][8][3];
    for (unsigned i = 0; i < ops; ++i) list[i].op = random32() % JD_OPS;
    for (unsigned side = 0; side < 2u; ++side) {
        if (jd_side_begin(side, seed, answers)) return 1;
        for (unsigned i = 0; i < ops && !jd_bad && !lc_bad; ++i) {
            jd_op *o = &list[i];
            if (side == 0) {
                jd_args(o);
                uint32_t j = o->a[0];
                if (o->op == JD_DTOR) {
                    if (!j) ++jd_cv[JDC_DTOR_NULL];
                    else {
                        ++jd_cv[jd_get(j + 0x20u) == jd_get(0x004d24a4u) ? JDC_DTOR_CURRENT : JDC_DTOR_OTHER];
                        ++jd_cv[jd_get(0x006c5eb4u + 4u * jd_get(j + 0x18u)) ? JDC_SLOT_SET : JDC_SLOT_CLEAR];
                        ++jd_cv[o->a[1] & 1u ? JDC_DELETE : JDC_KEEP];
                    }
                } else {
                    jd_cover_poll(j);
                }
                jd_changed = -1;
                jd_call((uint32_t)(uintptr_t)(mapped + (jd_ops[o->op].va - 0x400000u)), o->a, jd_ops[o->op].n, o->in, reg[0][i]);
                ++jd_calls[o->op];
                if (jd_changed >= 0) ++jd_cv[jd_changed ? JDC_CHANGED : JDC_SAME];
            } else {
                es2_sim_result r;
                memset(&r, 0, sizeof(r));
                if (es2_sim_call(&jd_dispatch, jd_ops[o->op].va, o->a, jd_ops[o->op].n, o->in, &r)) jd_bad = 1;
                reg[1][i][0] = r.eax; reg[1][i][1] = r.edx; reg[1][i][2] = r.ecx;
            }
        }
        if (jd_bad || lc_bad) { fprintf(stderr, "FAIL: input devices case %u: a call failed (side %u)\n", k, side); return 1; }
        su_take(side);
    }
    for (unsigned i = 0; i < ops; ++i)
        if (memcmp(reg[0][i], reg[1][i], sizeof(reg[0][i]))) {
            fprintf(stderr, "FAIL: input devices case %u: %s (call %u) EAX %08lx / %08lx, EDX %08lx / %08lx, ECX %08lx / %08lx\n", k,
                    jd_ops[list[i].op].name, i, (unsigned long)reg[0][i][0], (unsigned long)reg[1][i][0], (unsigned long)reg[0][i][1],
                    (unsigned long)reg[1][i][1], (unsigned long)reg[0][i][2], (unsigned long)reg[1][i][2]);
            return 1;
        }
    if (su_compare("input devices", k)) {
        fprintf(stderr, "  rows:");
        for (unsigned i = 0; i < ops; ++i) fprintf(stderr, " %s", jd_ops[list[i].op].name);
        fprintf(stderr, "\n");
        return 1;
    }
    if (jd_hash[0] != jd_hash[1] || jd_count[0] != jd_count[1]) {
        fprintf(stderr, "FAIL: input devices case %u: stub calls differ (%u / %u)\n  original: %s\n  own:      %s\n", k, jd_count[0],
                jd_count[1], jd_text[0], jd_text[1]);
        return 1;
    }
    return 0;
}

static int input_devices_cases(void) {
    current_function = "input devices";
    es2_sim_identity_memory(&ir_memory);
    es2_sim_rtl_memory(&ir_memory, &su_rtl);
    su_data_pristine = malloc(SU_DATA_SIZE);
    su_snap[0] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    su_snap[1] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    ir_region = ww_alloc(IR_REGION_SIZE);
    lc_stack = VirtualAlloc(NULL, 0x10000u, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    lc_bits = ww_alloc(LC_BITS_SIZE);
    if (!su_data_pristine || !su_snap[0] || !su_snap[1] || !ir_region || !lc_stack || !lc_bits) return fail("input devices: memory");
    su_pristine_take();
    for (unsigned i = 0; i < JD_PATCHED; ++i)
        if (jd_patch(jd_patched[i].va, jd_patched[i].stub, jd_saved[i], 1)) return 1;
    if (jd_dispatch_setup()) return 1;
    int bad = 0;
    const unsigned cases = 3000;
    for (unsigned k = 0; !bad && k < cases; ++k) {
        current_case = k; current_case_valid = 1;
        bad = jd_case(k);
    }
    if (!bad) {
        printf("PASS: input devices: %u cases identical\n", cases);
        printf("Input devices rows called:");
        for (unsigned i = 0; i < JD_OPS; ++i) printf(" %s %u%s", jd_ops[i].name, jd_calls[i], i + 1u < JD_OPS ? "," : "\n");
        printf("Input devices coverage:");
        for (unsigned i = 0; i < JDC_COUNT; ++i) printf(" %s %u%s", jd_cv_names[i], jd_cv[i], i + 1u < JDC_COUNT ? "," : "\n");
        for (unsigned i = 0; i < JDC_COUNT; ++i)
            if (!jd_cv[i]) { fprintf(stderr, "FAIL: input devices: path '%s' never taken\n", jd_cv_names[i]); bad = 1; }
    }
    current_case_valid = 0;
    for (unsigned i = 0; i < JD_PATCHED; ++i) jd_patch(jd_patched[i].va, NULL, jd_saved[i], 0);
    put32(mapped + 0x2cbee4u, 0);
    memcpy(mapped + SU_DATA_RVA, su_data_pristine, SU_DATA_SIZE);
    return bad;
}
