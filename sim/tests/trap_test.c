/* The stage-1 trap (es2/sim/trap.h) on a synthetic non-executable code page:
 * every entry kind and ABI through a machine call, nesting, refusals (also
 * through an import-slot trap), FS:0 records around trapped calls, and the
 * cost per trapped call. No game data. Modes: calls, refusal, seh, benchmark. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <setjmp.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "es2/sim/trap.h"
/* The adapter headers' seam macros as es2sim compiles them. */
#define ES2_SIM_HOST 1
#include "tools/reference-game/proxy/sim_bridge.h"

typedef struct sim_probe {
    uint32_t target, slot_count;
    const uint32_t *slots;
    uint32_t eax, edx, ecx, eax_out, edx_out, ecx_out, popped;
    uint8_t st0[12];
    uint32_t want_st0, ebx_out, seh_handler, fs0_during;
} sim_probe;
_Static_assert(offsetof(sim_probe, eax_out) == 24 && offsetof(sim_probe, st0) == 40 && offsetof(sim_probe, want_st0) == 52 &&
               offsetof(sim_probe, seh_handler) == 60 && offsetof(sim_probe, fs0_during) == 64, "probe layout");
extern void sim_probe_call(sim_probe *p);

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

static uint8_t *code;
static uint32_t A(uint32_t offset) { return (uint32_t)(uintptr_t)code + offset; }

/* ---- entries */
static void svc_sum(es2_sim_service_call *c, void *user) {
    (void)user;
    c->eax = c->slots[0] + c->slots[1] + c->slots[2];
    c->edx ^= 1u;
    c->ecx = 0x77u;
}
static void svc_mul(es2_sim_service_call *c, void *user) { (void)user; c->eax = c->slots[0] * c->slots[1]; }
typedef uint32_t (__cdecl *sum_fn)(uint32_t, uint32_t, uint32_t);
static void svc_nested(es2_sim_service_call *c, void *user) {
    (void)user;
    sum_fn inner = (sum_fn)(uintptr_t)A(0x10);
    c->eax = inner(c->slots[0], 10, 20) + 1u;   /* a trapped call from inside a trapped call */
}
/* Like "nested", but the inner call goes through the seam (0x20 is bridged in
 * the bridge cases; stdcall, the callee pops). */
typedef uint32_t (__stdcall *mul_fn)(uint32_t, uint32_t);
static void svc_nested_bridged(es2_sim_service_call *c, void *user) {
    (void)user;
    mul_fn inner = (mul_fn)(uintptr_t)es2_sim_bridge_target(A(0x20));
    c->eax = inner(c->slots[0], 3) + 1u;
}
/* The register-block bridges' callee (0xc0, 0xd0): it sees
 * the caller's EBP (the synthetic one the bridge loads) and the entry
 * registers, and leaves results in EAX, EDX and ECX. */
static uint32_t regs_seen[4];
static void svc_regs(es2_sim_service_call *c, void *user) {
    (void)user;
    regs_seen[0] = c->eax; regs_seen[1] = c->edx; regs_seen[2] = c->ecx; regs_seen[3] = c->ebp;
    c->eax = c->slots[0] + c->eax;
    c->edx ^= c->ebp;
    c->ecx += 1u;
}
static unsigned import_calls;
static void svc_import(es2_sim_service_call *c, void *user) { (void)user; ++import_calls; c->eax = 0xabcdu; }

static uint64_t __cdecl hook_borland(const uint32_t *a, void *user) {
    (void)user;
    return (uint64_t)a[3] | (uint64_t)a[4] << 32 | (a[0] == 1 && a[1] == 2 && a[2] == 4 ? 0 : 0x8000000000000000ull);
}
static long double __cdecl hook_st0(const uint32_t *a, void *user) { (void)user; return (long double)a[0] / 4.0L; }
static uint64_t __cdecl hook_context(const uint32_t *a, void *user, const uint32_t regs[3]) {
    (void)user;
    return (uint64_t)(a[0] + a[1]) | (uint64_t)regs[2] << 32;
}
static uint32_t __cdecl hook_legacy(const uint32_t a[ES2_HOOK_MAX_ARGS], void *user) { (void)user; return a[0] - a[1]; }

static es2_hook_descriptor d_borland = {"t_borland", 0, NULL, 0, 0, NULL, 0, 5, NULL, NULL, NULL, NULL, 0};
static es2_hook_extension x_borland = {ES2_ABI_BORLAND, ES2_RESULT_PAIR, hook_borland, NULL, NULL, 0, NULL, FALSE};
static es2_hook_descriptor d_st0 = {"t_st0", 0, NULL, 0, 0, NULL, 0, 1, NULL, NULL, NULL, NULL, 0};
static es2_hook_extension x_st0 = {ES2_ABI_CDECL, ES2_RESULT_ST0, NULL, hook_st0, NULL, 0, NULL, FALSE};
static es2_hook_descriptor d_context = {"t_context", 0, NULL, 0, 0, NULL, 0, 2, NULL, NULL, NULL, NULL, 0};
static es2_hook_extension x_context = {ES2_ABI_STDCALL, ES2_RESULT_PAIR, NULL, NULL, NULL, 0, hook_context, FALSE};
static es2_hook_descriptor d_legacy = {"t_legacy", 0, NULL, 0, 0, NULL, 0, 2, hook_legacy, NULL, NULL, NULL, 0};

static es2_sim_entry storage[32];
static es2_sim_dispatch table;
static jmp_buf refused_jump;
static es2_sim_refusal last_refusal;
static unsigned refusal_count;
static void on_refuse(const es2_sim_refusal *r, void *user) {
    (void)user;
    last_refusal = *r;
    ++refusal_count;
    es2_sim_trap_reset_depth();
    longjmp(refused_jump, 1);
}
static const uint32_t import_keys[2] = {0x006cc3e8u, 0x006cc2d4u};
static const uint32_t import_slots[2] = {0x006cc3e8u, 0x006cc2d4u};
static const char *const import_names[2] = {"KERNEL32.dll!GetTickCount", "cw3220.DLL!_strupr"};

static int setup(void) {
    unsigned long flags = 0; int permanent = 0;
    CHECK(!es2_sim_enable_dep(&flags, &permanent));
    code = VirtualAlloc(NULL, 0x2000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    CHECK(code);
    memset(code, 0xcc, 0x2000);
    DWORD old;
    CHECK(VirtualProtect(code, 0x2000, PAGE_READONLY, &old));
    es2_sim_dispatch_init(&table, storage, 32, on_refuse, NULL);
    table.import_slots = import_slots; table.import_names = import_names; table.import_count = 2;
    CHECK(!es2_sim_dispatch_add_service(&table, A(0x10), "sum", svc_sum, NULL, 0, 3));
    CHECK(!es2_sim_dispatch_add_service(&table, A(0x20), "mul_stdcall", svc_mul, NULL, 8, 2));
    CHECK(!es2_sim_dispatch_add_hook(&table, A(0x30), "borland", &d_borland, &x_borland));
    CHECK(!es2_sim_dispatch_add_hook(&table, A(0x40), "st0", &d_st0, &x_st0));
    CHECK(!es2_sim_dispatch_add_hook(&table, A(0x50), "context", &d_context, &x_context));
    CHECK(!es2_sim_dispatch_add_hook(&table, A(0x70), "legacy", &d_legacy, NULL));
    CHECK(!es2_sim_dispatch_add_service(&table, A(0x80), "nested", svc_nested, NULL, 0, 1));
    CHECK(!es2_sim_dispatch_add_service(&table, A(0xa0), "shimmed", svc_sum, NULL, 0, 3));
    CHECK(!es2_sim_dispatch_add_service(&table, A(0xb0), "nested_bridged", svc_nested_bridged, NULL, 0, 1));
    CHECK(!es2_sim_dispatch_add_service(&table, A(0xc0), "regs", svc_regs, NULL, 0, 1));
    CHECK(!es2_sim_dispatch_add_service(&table, A(0xd0), "regs_unlisted", svc_regs, NULL, 0, 1));
    CHECK(!es2_sim_dispatch_add_service(&table, 0x006cc3e8u, "import_service", svc_import, NULL, 0, 0));
    const char *error = NULL; uint32_t bad = 0;
    CHECK(!es2_sim_dispatch_seal(&table, &error, &bad));
    es2_sim_trap_range ranges[2] = {{A(0), A(0x1000), NULL, 0}, {A(0x1000), A(0x1008), import_keys, 4}};
    CHECK(!es2_sim_trap_install(&table, ranges, 2));
    return 0;
}

static sim_probe probe(uint32_t target, unsigned count, const uint32_t *slots, uint32_t eax, uint32_t edx, uint32_t ecx) {
    sim_probe p;
    memset(&p, 0, sizeof(p));
    p.target = target; p.slot_count = count; p.slots = slots; p.eax = eax; p.edx = edx; p.ecx = ecx;
    return p;
}

static int calls(void) {
    static const uint32_t three[3] = {1, 2, 3}, two[2] = {6, 7}, b[2] = {0x100, 0x200}, five[1] = {5};
    sim_probe p = probe(A(0x10), 3, three, 0xe0, 0xd0, 0xc0);
    sim_probe_call(&p);
    CHECK(p.eax_out == 6 && p.edx_out == 0xd1 && p.ecx_out == 0x77 && p.popped == 0 && p.ebx_out == 0x5a5a1234u);
    p = probe(A(0x20), 2, two, 0, 0x44, 0x55);
    sim_probe_call(&p);
    CHECK(p.eax_out == 42 && p.edx_out == 0x44 && p.ecx_out == 0x55 && p.popped == 8);     /* stdcall: callee pops */
    p = probe(A(0x30), 2, b, 1, 2, 4);
    sim_probe_call(&p);
    CHECK(p.eax_out == 0x200 && p.edx_out == 0x100 && p.ecx_out == 4 && p.popped == 8);    /* Borland: EAX EDX ECX, slots reversed */
    p = probe(A(0x40), 1, five, 0, 0, 9);
    p.want_st0 = 1;
    sim_probe_call(&p);
    long double st0; memcpy(&st0, p.st0, 10);
    CHECK(st0 == 1.25L && p.popped == 0 && p.ecx_out == 9);
    p = probe(A(0x50), 2, two, 0, 0, 0x99);
    sim_probe_call(&p);
    CHECK(p.eax_out == 13 && p.edx_out == 0x99 && p.ecx_out == 0x99 && p.popped == 8);
    p = probe(A(0x70), 2, two, 0, 0, 3);
    sim_probe_call(&p);
    CHECK(p.eax_out == (uint32_t)-1 && p.popped == 0 && p.ecx_out == 3);
    p = probe(A(0x80), 1, five, 0, 0, 0);
    sim_probe_call(&p);
    CHECK(p.eax_out == 36 && es2_sim_trap_depth() == 0);                                  /* nested: 5 + 10 + 20 + 1 */
    p = probe(A(0x1000), 0, NULL, 0, 0, 0);                                               /* the first import-slot trap */
    sim_probe_call(&p);
    CHECK(p.eax_out == 0xabcd && import_calls == 1);
    /* The C path gives the same results. */
    es2_sim_result r;
    const uint32_t regs[3] = {1, 2, 4};
    CHECK(!es2_sim_call(&table, A(0x30), b, 2, regs, &r) && r.eax == 0x200 && r.edx == 0x100 && r.pop_bytes == 8);
    CHECK(!es2_sim_call(&table, A(0x40), five, 1, regs, &r) && r.has_st0 && r.st0 == 1.25L);
    CHECK(!es2_sim_call(&table, A(0x10), three, 3, regs, &r) && r.eax == 6 && r.ecx == 0x77 && r.edx == 3);
    CHECK(es2_sim_trap_calls() >= 9 && refusal_count == 0);
    const es2_sim_entry *e = es2_sim_dispatch_find(&table, A(0x10));
    CHECK(e && e->calls == 3);                                       /* direct, nested, C path */
    puts("PASS: trapped calls: service, stdcall, Borland, ST0, context, legacy, nested, import slot; C path");
    return 0;
}

/* ---- the bridge seam against the trap */
typedef struct sim_probe_x {
    sim_probe p;
    uint32_t flags_in, flags_out, flags_seam;
} sim_probe_x;
_Static_assert(offsetof(sim_probe_x, flags_in) == 68 && offsetof(sim_probe_x, flags_out) == 72 &&
               offsetof(sim_probe_x, flags_seam) == 76, "flags probe layout");
extern void sim_probe_call_flags(sim_probe_x *p);
extern volatile uint32_t sim_test_shim_runs;
extern void sim_test_shim(void);
/* As a naked adapter bridge uses ES2_SIM_BRIDGE_ARG8: the target at 8(%ebp)
 * through the seam right after the prologue, then the call. The flags are
 * set to flags_in before the seam and stored after it in flags_seam (the
 * seam keeps them); the probe then sets them again for its CALL. */
__attribute__((naked, noinline)) static void __cdecl bridge_probe_call(uint32_t target, sim_probe_x *p) {
    __asm__ volatile(
        "pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "movl 12(%ebp),%edi\n\tpushl 68(%edi)\n\tpopfl\n\t"
        ES2_SIM_BRIDGE_ARG8
        "pushfl\n\tpopl 76(%edi)\n\tcld\n\t"
        "movl 8(%ebp),%eax\n\tmovl %eax,0(%edi)\n\tpushl %edi\n\tcall _sim_probe_call_flags\n\taddl $4,%esp\n\t"
        "popl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
#define PROBE_FLAGS 0x00000ad7u   /* CF PF AF ZF SF OF set, IF, bit 1; DF clear */
#define FLAGS_MASK 0x00000cd5u    /* CF PF AF ZF SF DF OF */

static const es2_sim_entry *entry(uint32_t va) { return es2_sim_dispatch_find(&table, va); }

/* One call of `target` through the trap and one through the seam with the
 * same inputs: every output equal; the flags as at the CALL (unless the
 * entry has a shim: the shims do not keep them); `calls` +1 each way,
 * `traps` +1 only through the trap, `bridges` +1 only through the seam.
 * `inner`: the entry makes one call of its own, 1 a plain trapped one, 2 one
 * through the seam. */
static int same_both_ways(uint32_t target, unsigned count, const uint32_t *slots, uint32_t eax, uint32_t edx, uint32_t ecx,
                          int want_st0, int shim, int inner) {
    const es2_sim_entry *e = entry(target);
    CHECK(e);
    uint64_t c0 = e->calls, t0 = e->traps, b0 = e->bridges;
    uint64_t faults0 = es2_sim_trap_calls(), bridged0 = es2_sim_bridge_calls();
    sim_probe_x t, b;
    memset(&t, 0, sizeof(t));
    t.p = probe(target, count, slots, eax, edx, ecx);
    t.p.want_st0 = (uint32_t)want_st0;
    t.flags_in = PROBE_FLAGS;
    b = t;
    sim_probe_call_flags(&t);
    CHECK(es2_sim_trap_calls() == faults0 + 1u + (inner == 1));
    CHECK(es2_sim_bridge_calls() == bridged0 + (inner == 2));
    CHECK(e->calls == c0 + 1u && e->traps == t0 + 1u && e->bridges == b0);
    uint64_t faults1 = es2_sim_trap_calls(), bridged1 = es2_sim_bridge_calls();
    bridge_probe_call(target, &b);
    CHECK(b.p.target != target);                                     /* the seam replaced it */
    if ((b.flags_seam & FLAGS_MASK) != (PROBE_FLAGS & FLAGS_MASK) || (!shim && (t.flags_out & FLAGS_MASK) != (b.flags_out & FLAGS_MASK)))
        fprintf(stderr, "flags: seam %08x, after the trapped call %08x, after the bridged call %08x\n",
                (unsigned)b.flags_seam, (unsigned)t.flags_out, (unsigned)b.flags_out);
    CHECK((b.flags_seam & FLAGS_MASK) == (PROBE_FLAGS & FLAGS_MASK));  /* ES2_SIM_BRIDGE_ARG8 keeps the flags */
    CHECK(es2_sim_trap_calls() == faults1 + (inner == 1));           /* no fault (an inner plain call still faults) */
    CHECK(es2_sim_bridge_calls() == bridged1 + 1u + (inner == 2));
    CHECK(e->calls == c0 + 2u && e->traps == t0 + 1u && e->bridges == b0 + 1u);
    CHECK(t.p.eax_out == b.p.eax_out && t.p.edx_out == b.p.edx_out && t.p.ecx_out == b.p.ecx_out);
    CHECK(t.p.popped == b.p.popped && t.p.ebx_out == b.p.ebx_out && b.p.ebx_out == 0x5a5a1234u);
    if (want_st0) CHECK(!memcmp(t.p.st0, b.p.st0, 10));
    if (!shim) CHECK((t.flags_out & FLAGS_MASK) == (PROBE_FLAGS & FLAGS_MASK) && (b.flags_out & FLAGS_MASK) == (t.flags_out & FLAGS_MASK));
    CHECK(es2_sim_trap_depth() == 0);
    return 0;
}

/* A bridge of the weapon_slots / logic_frame / weapon_system /
 * route kind: ES2_SIM_BRIDGE_ARG8 right after the prologue, the target then
 * copied to a global, the host ESP saved in a global, one argument slot
 * pushed, EAX EDX ECX EBX ESI EDI and a synthetic EBP loaded from a block,
 * the call, the outputs stored, ESP and EBP restored. Block: [0] slot,
 * [1] EAX, [2] EDX, [3] ECX, [4] EBX, [5] ESI, [6] EDI, [7] EBP; regs_out:
 * EAX EDX ECX EBX ESI EDI after the call. The same bridge without the seam
 * is the trap path. */
static uint32_t __attribute__((used)) regs_target, regs_host_esp, regs_out[6];
#define REGS_BRIDGE(name, seam)                                                                                 \
    __attribute__((naked, noinline)) static void __cdecl name(uint32_t target, const uint32_t *block) {         \
        __asm__ volatile(                                                                                       \
            "pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"                         \
            seam                                                                                                \
            "movl 8(%ebp),%eax\n\tmovl %eax,_regs_target\n\t"                                                   \
            "pushl _regs_host_esp\n\tmovl %esp,_regs_host_esp\n\t"                                              \
            "movl 12(%ebp),%ebx\n\tpushl (%ebx)\n\t"                                                            \
            "movl 4(%ebx),%eax\n\tmovl 8(%ebx),%edx\n\tmovl 12(%ebx),%ecx\n\t"                                  \
            "movl 20(%ebx),%esi\n\tmovl 24(%ebx),%edi\n\tmovl 28(%ebx),%ebp\n\tmovl 16(%ebx),%ebx\n\t"         \
            "call *_regs_target\n\t"                                                                            \
            "movl %eax,_regs_out\n\tmovl %edx,_regs_out+4\n\tmovl %ecx,_regs_out+8\n\t"                         \
            "movl %ebx,_regs_out+12\n\tmovl %esi,_regs_out+16\n\tmovl %edi,_regs_out+20\n\t"                    \
            "movl _regs_host_esp,%esp\n\tpopl _regs_host_esp\n\tleal 12(%esp),%ebp\n\t"                         \
            "popl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");                                    \
    }
REGS_BRIDGE(regs_bridge_trapped, "")
REGS_BRIDGE(regs_bridge_seamed, ES2_SIM_BRIDGE_ARG8)

typedef void (__cdecl *regs_bridge_fn)(uint32_t, const uint32_t *);
typedef struct regs_run { uint32_t out[6], seen[4]; } regs_run;
static regs_run regs_call(regs_bridge_fn f, uint32_t target, const uint32_t block[8]) {
    regs_run r;
    memset(regs_out, 0, sizeof(regs_out));
    memset(regs_seen, 0, sizeof(regs_seen));
    f(target, block);
    memcpy(r.out, regs_out, sizeof(r.out));
    memcpy(r.seen, regs_seen, sizeof(r.seen));
    return r;
}
/* 0xc0 is bridged, 0xd0 is not: through the seamed bridge 0xc0 is bridged
 * and 0xd0 still traps; every output, the registers the callee saw (with the
 * caller's EBP) and EBX ESI EDI after the call equal the stage-1 path. */
static int regs_both_ways(void) {
    const uint32_t block[8] = {0x1000u, 0x22u, 0x3300u, 0x44u, 0xb0b0b0b0u, 0x5e5e5e5eu, 0xd1d1d1d1u, 0x0012ff40u};
    const es2_sim_entry *listed = entry(A(0xc0)), *unlisted = entry(A(0xd0));
    CHECK(listed && unlisted);
    uint64_t lc = listed->calls, lt = listed->traps, lb = listed->bridges;
    uint64_t uc = unlisted->calls, ut = unlisted->traps, ub = unlisted->bridges;
    uint64_t faults = es2_sim_trap_calls(), bridged = es2_sim_bridge_calls();
    regs_run t = regs_call(regs_bridge_trapped, A(0xc0), block);
    CHECK(es2_sim_trap_calls() == faults + 1u && es2_sim_bridge_calls() == bridged);
    CHECK(listed->calls == lc + 1u && listed->traps == lt + 1u && listed->bridges == lb);
    regs_run s = regs_call(regs_bridge_seamed, A(0xc0), block);
    CHECK(es2_sim_trap_calls() == faults + 1u && es2_sim_bridge_calls() == bridged + 1u);
    CHECK(listed->calls == lc + 2u && listed->traps == lt + 1u && listed->bridges == lb + 1u);
    regs_run u = regs_call(regs_bridge_seamed, A(0xd0), block);
    CHECK(es2_sim_trap_calls() == faults + 2u && es2_sim_bridge_calls() == bridged + 1u);
    CHECK(unlisted->calls == uc + 1u && unlisted->traps == ut + 1u && unlisted->bridges == ub);
    CHECK(t.seen[0] == block[1] && t.seen[1] == block[2] && t.seen[2] == block[3] && t.seen[3] == block[7]);
    CHECK(t.out[0] == block[0] + block[1] && t.out[1] == (block[2] ^ block[7]) && t.out[2] == block[3] + 1u);
    CHECK(t.out[3] == block[4] && t.out[4] == block[5] && t.out[5] == block[6]);
    CHECK(!memcmp(&t, &s, sizeof(t)) && !memcmp(&t, &u, sizeof(t)));
    CHECK(es2_sim_trap_depth() == 0);
    return 0;
}

static int bridge(void) {
    static const uint32_t three[3] = {1, 2, 3}, two[2] = {6, 7}, b[2] = {0x100, 0x200}, five[1] = {5};
    /* The set: entries only, sorted, in CODE (not an import-slot trap). */
    const uint32_t unsorted[2] = {A(0x20), A(0x10)}, unknown[1] = {A(0x90)}, slot[1] = {0x006cc3e8u};
    CHECK(es2_sim_bridge_set(unsorted, 2) && es2_sim_bridge_count() == 0);
    CHECK(es2_sim_bridge_set(unknown, 1) && es2_sim_bridge_count() == 0);
    CHECK(es2_sim_bridge_set(slot, 1) && es2_sim_bridge_count() == 0);
    CHECK(es2_sim_bridge_target(A(0x10)) == A(0x10));               /* nothing bridged: unchanged */
    /* A shim on the "shimmed" entry, as es2_sim_register_adapters sets one. */
    es2_sim_entry *shimmed = (es2_sim_entry *)entry(A(0xa0));
    CHECK(shimmed);
    shimmed->entry_shim = (void *)sim_test_shim;
    /* 0x10 stays unbridged: the "nested" services' inner calls of it fault. */
    const uint32_t set[9] = {A(0x20), A(0x30), A(0x40), A(0x50), A(0x70), A(0x80), A(0xa0), A(0xb0), A(0xc0)};
    CHECK(!es2_sim_bridge_set(set, 9) && es2_sim_bridge_count() == 9);
    CHECK(es2_sim_bridge_target(A(0x10)) == A(0x10) && es2_sim_bridge_target(A(0x90)) == A(0x90)); /* unlisted: unchanged */
    if (same_both_ways(A(0x20), 2, two, 0, 0x44, 0x55, 0, 0, 0)) return 1;        /* stdcall service, callee pops */
    if (same_both_ways(A(0x30), 2, b, 1, 2, 4, 0, 0, 0)) return 1;                /* Borland hook */
    if (same_both_ways(A(0x40), 1, five, 0, 0, 9, 1, 0, 0)) return 1;             /* ST0 hook */
    if (same_both_ways(A(0x50), 2, two, 0, 0, 0x99, 0, 0, 0)) return 1;           /* context hook (stdcall) */
    if (same_both_ways(A(0x70), 2, two, 0, 0, 3, 0, 0, 0)) return 1;              /* legacy hook */
    if (same_both_ways(A(0x80), 1, five, 0, 0, 0, 0, 0, 1)) return 1;             /* nested: the inner call faults */
    if (same_both_ways(A(0xb0), 1, five, 0, 0, 0, 0, 0, 2)) return 1;             /* nested, inner call bridged too */
    uint32_t runs = sim_test_shim_runs;
    if (same_both_ways(A(0xa0), 3, three, 0xe0, 0xd0, 0xc0, 0, 1, 0)) return 1;   /* entry with a shim */
    CHECK(sim_test_shim_runs == runs + 2u);                          /* the shim ran both ways */
    if (regs_both_ways()) return 1;                                  /* register-block bridge (stage 3) */
    /* es2_sim_call of an entry with a shim makes a machine call (es2_sim_trap_call), now through the seam. */
    uint64_t faults = es2_sim_trap_calls(), bridged = es2_sim_bridge_calls();
    es2_sim_result r;
    const uint32_t regs[3] = {0, 0, 0};
    CHECK(!es2_sim_call(&table, A(0xa0), three, 3, regs, &r) && r.eax == 6 && r.ecx == 0x77);
    CHECK(es2_sim_trap_calls() == faults && es2_sim_bridge_calls() == bridged + 1u && sim_test_shim_runs == runs + 3u);
    /* Removing the trap clears the set. */
    es2_sim_trap_remove();
    CHECK(es2_sim_bridge_count() == 0 && es2_sim_bridge_target(A(0x20)) == A(0x20));
    puts("PASS: bridge seam: stdcall, Borland, ST0, context, legacy, nested (inner trapped and bridged), shim entry, "
         "es2_sim_call; results, ECX, popped bytes, EBX, ST0 and flags as through the trap; calls/traps/bridges; set checks");
    puts("PASS: register-block bridge seam (stage 3): ARG8 before the target global and the host ESP, synthetic EBP; "
         "results, the callee's entry registers and EBP, EBX ESI EDI as through the trap; an unlisted VA still traps");
    return 0;
}

static int refusal(void) {
    /* An unmapped VA in the trapped range. */
    if (!setjmp(refused_jump)) {
        sim_probe p = probe(A(0x90), 0, NULL, 0, 0, 0);
        sim_probe_call(&p);
        fprintf(stderr, "FAIL: unmapped VA not refused\n"); return 1;
    }
    CHECK(refusal_count == 1 && last_refusal.va == A(0x90) && !last_refusal.name && !strcmp(last_refusal.reason, "unmapped"));
    CHECK(last_refusal.return_address && last_refusal.frame);
    /* An import slot without a service: named by its import. */
    if (!setjmp(refused_jump)) {
        sim_probe p = probe(A(0x1004), 0, NULL, 0, 0, 0);
        sim_probe_call(&p);
        fprintf(stderr, "FAIL: import slot not refused\n"); return 1;
    }
    CHECK(refusal_count == 2 && last_refusal.va == 0x006cc2d4u && last_refusal.name && !strcmp(last_refusal.name, "cw3220.DLL!_strupr"));
    /* An address between two slot traps. */
    if (!setjmp(refused_jump)) {
        sim_probe p = probe(A(0x1002), 0, NULL, 0, 0, 0);
        sim_probe_call(&p);
        fprintf(stderr, "FAIL: unaligned slot trap not refused\n"); return 1;
    }
    CHECK(refusal_count == 3 && last_refusal.va == A(0x1002));
    /* The trap still works after the refusals. */
    static const uint32_t three[3] = {1, 2, 3};
    sim_probe p = probe(A(0x10), 3, three, 0, 0, 0);
    sim_probe_call(&p);
    CHECK(p.eax_out == 6 && es2_sim_trap_depth() == 0);
    /* The C path returns 1 instead (this callback longjmps, so catch it). */
    es2_sim_result r;
    const uint32_t regs[3] = {0, 0, 0};
    if (!setjmp(refused_jump)) { es2_sim_call(&table, A(0x94), NULL, 0, regs, &r); return 1; }
    CHECK(refusal_count == 4 && table.refusals == 4);
    puts("PASS: refusals named (unmapped VA, import slot, between slots), trap usable afterwards");
    return 0;
}

static volatile LONG seh_called;
static EXCEPTION_DISPOSITION __cdecl seh_handler(EXCEPTION_RECORD *r, void *frame, CONTEXT *c, void *dispatcher) {
    (void)r; (void)frame; (void)c; (void)dispatcher;
    InterlockedIncrement(&seh_called);
    return ExceptionContinueSearch;
}

static int seh(void) {
    uint32_t before;
    __asm__ volatile("movl %%fs:0, %0" : "=r"(before));
    static const uint32_t three[3] = {1, 2, 3}, five[1] = {5};
    /* A record with a host handler, as the 0x49630c service links them. */
    sim_probe p = probe(A(0x10), 3, three, 0, 0, 0);
    p.seh_handler = (uint32_t)(uintptr_t)seh_handler;
    sim_probe_call(&p);
    CHECK(p.eax_out == 6 && p.fs0_during != before && seh_called == 0);
    /* A record whose handler lies in the non-executable page, like the
     * guest's 0x4967f0: never called, the trap runs first. Nested too. */
    p = probe(A(0x80), 1, five, 0, 0, 0);
    p.seh_handler = A(0x08);
    sim_probe_call(&p);
    CHECK(p.eax_out == 36 && seh_called == 0);
    uint32_t after;
    __asm__ volatile("movl %%fs:0, %0" : "=r"(after));
    CHECK(after == before);
    puts("PASS: FS:0 records (host handler, handler in non-executable code) untouched by trapped calls");
    return 0;
}

static uint32_t __attribute__((noinline)) __cdecl direct_sum(uint32_t a, uint32_t b, uint32_t c) { return a + b + c; }

static int benchmark(void) {
    const unsigned n = 200000;
    sum_fn trapped = (sum_fn)(uintptr_t)A(0x10);
    volatile sum_fn direct = direct_sum;
    LARGE_INTEGER f, t0, t1, t2, t3;
    QueryPerformanceFrequency(&f);
    uint32_t acc = 0;
    QueryPerformanceCounter(&t0);
    for (unsigned i = 0; i < n; ++i) acc += trapped(i, 1, 2);
    QueryPerformanceCounter(&t1);
    for (unsigned i = 0; i < n; ++i) acc += direct(i, 1, 2);
    QueryPerformanceCounter(&t2);
    es2_sim_result r;
    const uint32_t regs[3] = {0, 0, 0};
    uint32_t s[3] = {0, 1, 2};
    for (unsigned i = 0; i < n; ++i) { s[0] = i; es2_sim_call(&table, A(0x10), s, 3, regs, &r); acc += r.eax; }
    QueryPerformanceCounter(&t3);
    double trap_ns = (double)(t1.QuadPart - t0.QuadPart) * 1e9 / (double)f.QuadPart / n;
    double direct_ns = (double)(t2.QuadPart - t1.QuadPart) * 1e9 / (double)f.QuadPart / n;
    double call_ns = (double)(t3.QuadPart - t2.QuadPart) * 1e9 / (double)f.QuadPart / n;
    printf("TRAP COST: %.0f ns per trapped call, %.1f ns per direct call, %.1f ns per es2_sim_call (%u calls each; acc %u)\n",
           trap_ns, direct_ns, call_ns, n, acc);
    CHECK(es2_sim_trap_depth() == 0);
    puts("PASS: benchmark");
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (setup()) return 1;
    if (!strcmp(argv[1], "calls")) return calls() || bridge();
    if (!strcmp(argv[1], "refusal")) return refusal();
    if (!strcmp(argv[1], "seh")) return seh();
    if (!strcmp(argv[1], "benchmark")) return benchmark();
    return 2;
}
