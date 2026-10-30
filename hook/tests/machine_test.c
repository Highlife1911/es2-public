/* Both backends of es2/hook/machine.h on the same calls: the assembly guest
 * functions of machine_guest.S through the x86 backend, C models of the same
 * behaviour as dispatch rows (entries, hooks, a service) at the same VAs
 * through the sim backend. Registers, stack bytes and results must agree for
 * every ABI and result kind; then the sim backend's own rules (refusals,
 * the guest stack, FS:0, nesting) and the x86 backend's FS:0 and x87. */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "es2/hook/machine.h"
#include "es2/hook/machine_x86.h"
#include "es2/sim/dispatch.h"
#include "es2/sim/machine_sim.h"

void guest_cdecl3(void), guest_cdecl_pair2(void), guest_stdcall2(void), guest_stdcall_pair4(void);
void guest_borland5(void), guest_borland_pair6(void), guest_borland2(void), guest_cdecl0(void);
void guest_hook_cdecl3(void), guest_hook_stdcall_pair2(void), guest_hook_borland_pair5(void);
void guest_service_stdcall2(void), guest_fldcw(void), guest_floor(void);

#define VA(f) ((uint32_t)(uintptr_t)(f))

static int failures;
#define CHECK(c, ...) do { if (!(c)) { ++failures; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint32_t rd(uint32_t va) {
    const uint8_t *p = (const uint8_t *)(uintptr_t)va;
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void wr(uint32_t va, uint32_t v) {
    uint8_t *p = (uint8_t *)(uintptr_t)va;
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

/* The models: m->esp at the return address, as the asm sees ESP. */
static void model_cdecl3(es2_machine *m, void *u) {
    (void)u;
    uint32_t s = m->esp, a = rd(s + 4), b = rd(s + 8), c = rd(s + 12);
    m->eax = m->eax + m->ecx + a + 2u * b + 3u * c;
    wr(s - 4, m->eax);
    wr(s - 12, m->edi);
    m->edx ^= a;
    m->ecx = b - m->ebx;
    m->ebx += c;
    m->esi ^= m->ebp;
    m->edi += a;
    m->ebp -= 1;
    wr(s + 12, c + 1);
}
static void model_cdecl_pair2(es2_machine *m, void *u) {
    (void)u;
    uint32_t s = m->esp;
    uint64_t r = (uint64_t)rd(s + 4) * rd(s + 8) + m->ecx;
    m->eax = (uint32_t)r; m->edx = (uint32_t)(r >> 32);
    wr(s - 8, m->esi);
    m->esi = ~m->esi;
}
static void model_stdcall2(es2_machine *m, void *u) {
    (void)u;
    uint32_t s = m->esp, a = rd(s + 4), b = rd(s + 8);
    wr(s - 4, m->ebx);
    m->eax = a - b; m->ecx = a; m->ebx ^= b;
    wr(s + 4, 0);
}
static void model_stdcall_pair4(es2_machine *m, void *u) {
    (void)u;
    uint32_t s = m->esp, a = rd(s + 4), b = rd(s + 8), c = rd(s + 12), d = rd(s + 16);
    wr(s - 16, m->edx);
    m->eax = a + c; m->edx = b - d; m->ebp += d; m->edi ^= c;
}
static void model_borland5(es2_machine *m, void *u) {
    (void)u;
    uint32_t s = m->esp, x0 = m->eax, x1 = m->edx, x2 = m->ecx, a3 = rd(s + 8), a4 = rd(s + 4);
    wr(s - 4, x2);
    m->eax = ((x0 - x1) ^ x2) + 2u * a3 - a4;
    m->ecx = a4 - a3;
    m->edx = x1 + m->eax;
    wr(s + 8, m->eax);
}
static void model_borland_pair6(es2_machine *m, void *u) {
    (void)u;
    uint32_t s = m->esp, a3 = rd(s + 12), a4 = rd(s + 8), a5 = rd(s + 4);
    m->eax = m->eax + a3 - a5;
    m->edx = m->edx ^ a4 ^ m->ecx;
    m->esi += a4;
    wr(s - 20, m->edi);
}
static void model_borland2(es2_machine *m, void *u) { (void)u; m->eax *= m->edx; }
static void model_cdecl0(es2_machine *m, void *u) {
    (void)u;
    m->eax = m->ebx ^ m->esi; m->ebx = m->edi;
    wr(m->esp - 4, m->ebp);
}

static uint32_t __cdecl hook_cdecl3(const uint32_t a[ES2_HOOK_MAX_ARGS], void *u) { (void)u; return a[0] - a[1] * a[2]; }
static uint64_t __cdecl hook_stdcall_pair2(const uint32_t *a, void *u) { (void)u; return (uint64_t)a[0] * a[1]; }
static uint64_t __cdecl hook_borland_pair5(const uint32_t *a, void *u) {
    (void)u;
    return (uint64_t)(a[1] - a[4]) << 32 | (a[0] ^ a[3] ^ a[2]);
}
static void service_stdcall2(es2_sim_service_call *c, void *u) {
    (void)u;
    c->eax = c->slots[0] + c->slots[1];
    c->ecx = c->slots[0] ^ c->slots[1];
}

static const es2_hook_descriptor dsc_cdecl3 = {.name = "hook_cdecl3", .arg_count = 3, .replacement = hook_cdecl3};
static const es2_hook_descriptor dsc_stdcall_pair2 = {.name = "hook_stdcall_pair2", .arg_count = 2};
static const es2_hook_extension ext_stdcall_pair2 = {.abi = ES2_ABI_STDCALL, .result_kind = ES2_RESULT_PAIR,
                                                    .replacement64 = hook_stdcall_pair2};
static const es2_hook_descriptor dsc_borland_pair5 = {.name = "hook_borland_pair5", .arg_count = 5};
static const es2_hook_extension ext_borland_pair5 = {.abi = ES2_ABI_BORLAND, .result_kind = ES2_RESULT_PAIR,
                                                    .replacement64 = hook_borland_pair5};

/* Nesting: an entry that makes a machine call with ESP 0 from inside. */
static unsigned nested_depth, nested_inner_depth;
static uint32_t nested_outer_esp, nested_slot0, nested_eax;
static es2_sim_dispatch dispatch;
static void model_nest(es2_machine *m, void *u) {
    (void)u;
    nested_depth = es2_machine_sim_depth();
    nested_outer_esp = m->esp;
    es2_machine inner = {.eax = 1, .ecx = 2};
    uint32_t args[3] = {10, 20, 30};
    if (es2_machine_call(&inner, VA(guest_cdecl3), ES2_MACHINE_CDECL, args, 3)) return;
    /* The inner call pushed below the outer ESP: return slot, then 10, 20, 31. */
    nested_slot0 = rd(nested_outer_esp - 12);
    nested_eax = inner.eax;
    nested_inner_depth = es2_machine_sim_depth();
    m->eax = inner.eax;
}
static void model_depth(es2_machine *m, void *u) { (void)u; m->eax = es2_machine_sim_depth(); }

/* The guest entries the backend runs: seen inside a nested machine call and
 * in the outer entry after it returned. */
static uint32_t seen_entries[2][4];
static void model_entries_inner(es2_machine *m, void *u) {
    (void)m; (void)u;
    for (unsigned i = 0; i < 4; ++i) seen_entries[0][i] = es2_sim_guest_entry_va(i);
}
static void model_entries(es2_machine *m, void *u) {
    (void)u;
    es2_machine inner = {0};
    es2_machine_call(&inner, 0x7ff00040u, ES2_MACHINE_CDECL, NULL, 0);
    for (unsigned i = 0; i < 4; ++i) seen_entries[1][i] = es2_sim_guest_entry_va(i);
    m->eax = 0;
}

static unsigned refusals;
static char last_reason[96];
static void on_refuse(const es2_sim_refusal *r, void *u) {
    (void)u;
    ++refusals;
    snprintf(last_reason, sizeof last_reason, "%s", r->reason);
}
static unsigned model_errors;
static char last_model_error[96];
static void on_model_error(const es2_sim_refusal *r, void *u) {
    (void)u;
    ++model_errors;
    snprintf(last_model_error, sizeof last_model_error, "%s", r->reason);
}

typedef struct call_case {
    const char *name;
    void (*fn)(void);
    es2_machine_abi abi;
    unsigned count;
} call_case;

static const call_case cases[] = {
    {"cdecl EAX", guest_cdecl3, ES2_MACHINE_CDECL, 3},
    {"cdecl EDX:EAX", guest_cdecl_pair2, ES2_MACHINE_CDECL, 2},
    {"cdecl no arguments", guest_cdecl0, ES2_MACHINE_CDECL, 0},
    {"stdcall EAX", guest_stdcall2, ES2_MACHINE_STDCALL, 2},
    {"stdcall EDX:EAX", guest_stdcall_pair4, ES2_MACHINE_STDCALL, 4},
    {"borland EAX", guest_borland5, ES2_MACHINE_BORLAND, 5},
    {"borland EDX:EAX", guest_borland_pair6, ES2_MACHINE_BORLAND, 6},
    {"borland registers only", guest_borland2, ES2_MACHINE_BORLAND, 2},
    {"hook cdecl EAX", guest_hook_cdecl3, ES2_MACHINE_CDECL, 3},
    {"hook stdcall EDX:EAX", guest_hook_stdcall_pair2, ES2_MACHINE_STDCALL, 2},
    {"hook borland EDX:EAX", guest_hook_borland_pair5, ES2_MACHINE_BORLAND, 5},
    {"service stdcall", guest_service_stdcall2, ES2_MACHINE_STDCALL, 2},
};

#define REGION 4096u
#define TOP 2048u
static _Alignas(16) uint8_t region_x86[REGION], region_sim[REGION];
static _Alignas(16) uint8_t guest_stack[65536];
static es2_sim_entry storage[32];

static void fill(uint8_t *r) { for (unsigned i = 0; i < REGION; ++i) r[i] = (uint8_t)(i * 7u + 0x5a); }

static es2_machine pattern(unsigned k) {
    es2_machine m = {0x10000001u * (k + 1), 0x0f0f0f0fu + k, 0x80000000u | k, 0x13579bdfu ^ k, 0,
                     0x2468ace0u + 3 * k, 0xdeadbeefu - k, 0x01234567u * (k + 3)};
    return m;
}

/* Planted residue: bytes below the entry ESP, past every byte the guest
 * functions write there (at most 20 below their ESP); a later plant of the
 * same byte wins. */
static const es2_machine_plant plants[] = {{24, 0xa5}, {33, 0x6d}, {65, 0x1e7}, {144, 0x11}, {145, 0x22}, {146, 0x33},
                                           {147, 0x44}, {33, 0x5c}};

static void compare_case(unsigned k, const call_case *c, int host_stack, int planted) {
    uint32_t args[ES2_MACHINE_MAX_ARGS];
    for (unsigned i = 0; i < c->count; ++i) args[i] = 0x9e3779b9u * (i + 1) + k * 0x111u + (i == 1 ? 0xfffff000u : 0);
    es2_machine mx = pattern(k), ms = pattern(k);
    fill(region_x86); fill(region_sim);
    if (!host_stack) {
        mx.esp = VA(region_x86 + TOP);
        ms.esp = VA(region_sim + TOP);
    }
    uint32_t esp_x = mx.esp, esp_s = ms.esp, entry_x = 0, entry_s = 0;
    const es2_machine_plant *p = planted ? plants : NULL;
    unsigned np = planted ? (unsigned)(sizeof plants / sizeof plants[0]) : 0u;
    CHECK(es2_machine_x86_call_planted(&mx, VA(c->fn), c->abi, args, c->count, p, np, planted ? &entry_x : NULL) == 0,
          "%s: x86 refused", c->name);
    uint64_t before = refusals;
    CHECK(es2_machine_sim_call_planted(&dispatch, &ms, VA(c->fn), c->abi, args, c->count, p, np,
                                       planted ? &entry_s : NULL) == 0,
          "%s: sim refused (%s)", c->name, last_reason);
    CHECK(refusals == before, "%s: refusal", c->name);
    CHECK(mx.esp == esp_x && ms.esp == esp_s, "%s: ESP moved", c->name);
    mx.esp = ms.esp = 0;
    CHECK(memcmp(&mx, &ms, sizeof mx) == 0,
          "%s%s: registers x86 %08x %08x %08x %08x %08x %08x %08x, sim %08x %08x %08x %08x %08x %08x %08x", c->name,
          host_stack ? " (ESP 0)" : "", mx.eax, mx.ecx, mx.edx, mx.ebx, mx.ebp, mx.esi, mx.edi, ms.eax, ms.ecx, ms.edx,
          ms.ebx, ms.ebp, ms.esi, ms.edi);
    if (host_stack) {
        CHECK(!planted || entry_x != 0, "%s: no x86 entry ESP", c->name);
        return;
    }
    unsigned slots = c->abi == ES2_MACHINE_BORLAND ? (c->count > 3 ? c->count - 3 : 0) : c->count;
    unsigned ret = TOP - 4u * (slots + 1u);
    CHECK(rd(VA(region_sim + ret)) == ES2_MACHINE_SIM_SENTINEL, "%s: sentinel", c->name);
    if (planted) {
        CHECK(entry_x == VA(region_x86 + ret) && entry_s == VA(region_sim + ret), "%s: entry ESP x86 top%+d sim top%+d",
              c->name, (int)(entry_x - VA(region_x86 + TOP)), (int)(entry_s - VA(region_sim + TOP)));
        CHECK(region_sim[ret - 33] == 0x5c && region_sim[ret - 65] == 0xe7 && rd(VA(region_sim + ret - 147)) == 0x11223344u,
              "%s: planted bytes", c->name);
    }
    memset(region_x86 + ret, 0, 4); memset(region_sim + ret, 0, 4);
    for (unsigned i = 0; i < REGION; ++i)
        if (region_x86[i] != region_sim[i]) {
            CHECK(0, "%s: stack byte at top%+d x86 %02x sim %02x", c->name, (int)i - (int)TOP, region_x86[i],
                  region_sim[i]);
            break;
        }
}

static int setup(void) {
    es2_sim_dispatch_init(&dispatch, storage, 32, on_refuse, NULL);
    dispatch.model_error = on_model_error;
    int r = 0;
    r |= es2_sim_dispatch_add_entry(&dispatch, VA(guest_cdecl3), "cdecl3", model_cdecl3, NULL, 0, 3);
    r |= es2_sim_dispatch_add_entry(&dispatch, VA(guest_cdecl_pair2), "cdecl_pair2", model_cdecl_pair2, NULL, 0, 2);
    r |= es2_sim_dispatch_add_entry(&dispatch, VA(guest_cdecl0), "cdecl0", model_cdecl0, NULL, 0, 0);
    r |= es2_sim_dispatch_add_entry(&dispatch, VA(guest_stdcall2), "stdcall2", model_stdcall2, NULL, 8, 2);
    r |= es2_sim_dispatch_add_entry(&dispatch, VA(guest_stdcall_pair4), "stdcall_pair4", model_stdcall_pair4, NULL, 16, 4);
    r |= es2_sim_dispatch_add_entry(&dispatch, VA(guest_borland5), "borland5", model_borland5, NULL, 8, 2);
    r |= es2_sim_dispatch_add_entry(&dispatch, VA(guest_borland_pair6), "borland_pair6", model_borland_pair6, NULL, 12, 3);
    r |= es2_sim_dispatch_add_entry(&dispatch, VA(guest_borland2), "borland2", model_borland2, NULL, 0, 0);
    r |= es2_sim_dispatch_add_hook(&dispatch, VA(guest_hook_cdecl3), "hook_cdecl3", &dsc_cdecl3, NULL);
    r |= es2_sim_dispatch_add_hook(&dispatch, VA(guest_hook_stdcall_pair2), "hook_stdcall_pair2", &dsc_stdcall_pair2,
                                   &ext_stdcall_pair2);
    r |= es2_sim_dispatch_add_hook(&dispatch, VA(guest_hook_borland_pair5), "hook_borland_pair5", &dsc_borland_pair5,
                                   &ext_borland_pair5);
    r |= es2_sim_dispatch_add_service(&dispatch, VA(guest_service_stdcall2), "service_stdcall2", service_stdcall2, NULL,
                                      8, 2);
    r |= es2_sim_dispatch_add_entry(&dispatch, 0x7ff00000u, "nest", model_nest, NULL, 0, 0);
    r |= es2_sim_dispatch_add_entry(&dispatch, 0x7ff00010u, "depth", model_depth, NULL, 0, 0);
    r |= es2_sim_dispatch_add_entry(&dispatch, 0x7ff00030u, "entries", model_entries, NULL, 0, 0);
    r |= es2_sim_dispatch_add_entry(&dispatch, 0x7ff00040u, "entries_inner", model_entries_inner, NULL, 0, 0);
    const char *error = NULL;
    uint32_t bad = 0;
    if (r || es2_sim_dispatch_seal(&dispatch, &error, &bad)) {
        printf("FAIL dispatch set-up: %s %08x\n", error ? error : "add", bad);
        return 1;
    }
    es2_machine_sim_bind(&dispatch, VA(guest_stack), VA(guest_stack) + sizeof guest_stack);
    return 0;
}

static void sim_rules(void) {
    const uint32_t base = VA(guest_stack) + sizeof guest_stack, limit = VA(guest_stack);
    const es2_machine_stack *st = es2_machine_sim_stack();
    CHECK(st->base == base && st->limit == limit, "stack bounds");
    CHECK(es2_machine_fs0() == base - 8 && rd(base - 8) == ES2_MACHINE_FS0_END && rd(base - 4) == 0,
          "FS:0 sentinel record");
    es2_machine_set_fs0(0x12345678u);
    CHECK(es2_machine_fs0() == 0x12345678u, "FS:0 set");
    es2_machine_set_fs0(base - 8);

    /* ESP 0 at the outermost level: the guest stack below the FS:0 record. */
    es2_machine m = {.eax = 5, .ecx = 6};
    uint32_t args[3] = {1, 2, 3};
    CHECK(es2_machine_call(&m, VA(guest_cdecl3), ES2_MACHINE_CDECL, args, 3) == 0 && m.esp == 0, "ESP 0 call");
    CHECK(m.eax == 5 + 6 + 1 + 4 + 9, "ESP 0 result %u", m.eax);
    CHECK(rd(base - 8 - 16) == ES2_MACHINE_SIM_SENTINEL && rd(base - 8 - 12) == 1 && rd(base - 8 - 4) == 4,
          "ESP 0 pushes at the stack top");

    es2_machine n = {0};
    CHECK(es2_machine_call(&n, 0x7ff00000u, ES2_MACHINE_CDECL, NULL, 0) == 0, "nested call");
    CHECK(nested_depth == 1 && nested_inner_depth == 1, "depth %u %u", nested_depth, nested_inner_depth);
    CHECK(nested_outer_esp == base - 8 - 4, "outer ESP %08x", nested_outer_esp);
    CHECK(nested_slot0 == 10 && nested_eax == 1 + 2 + 10 + 40 + 90, "inner call slot %u eax %u", nested_slot0,
          nested_eax);
    CHECK(es2_machine_sim_depth() == 0, "depth after");
    es2_machine dm = {0};
    CHECK(es2_machine_call(&dm, 0x7ff00010u, ES2_MACHINE_CDECL, NULL, 0) == 0 && dm.eax == 1, "depth inside");

    /* The guest entries, innermost first. */
    CHECK(es2_sim_guest_entry_va(0) == 0, "no entry outside");
    es2_machine em = {0};
    CHECK(es2_machine_call(&em, 0x7ff00030u, ES2_MACHINE_CDECL, NULL, 0) == 0, "entries call");
    CHECK(seen_entries[0][0] == 0x7ff00040u && seen_entries[0][1] == 0x7ff00030u && seen_entries[0][2] == 0 &&
              seen_entries[0][3] == 0,
          "entries inside the machine calls %08x %08x %08x %08x", seen_entries[0][0], seen_entries[0][1],
          seen_entries[0][2], seen_entries[0][3]);
    CHECK(seen_entries[1][0] == 0x7ff00030u && seen_entries[1][1] == 0 && seen_entries[1][2] == 0,
          "the outer entry after the inner call %08x %08x %08x", seen_entries[1][0], seen_entries[1][1],
          seen_entries[1][2]);
    CHECK(es2_sim_guest_entry_va(0) == 0, "no entry after");
    /* The wasm trap's exit code: the divide codes inside an entry only. */
    CHECK(es2_machine_sim_trap_exit_code(ES2_MACHINE_SIM_TRAP_DIVIDE_BY_ZERO, 0x401000u) == 0xc0000094u &&
              es2_machine_sim_trap_exit_code(ES2_MACHINE_SIM_TRAP_INTEGER_OVERFLOW, 0x401000u) == 0xc0000095u &&
              es2_machine_sim_trap_exit_code(ES2_MACHINE_SIM_TRAP_OTHER, 0x401000u) == 71u &&
              es2_machine_sim_trap_exit_code(ES2_MACHINE_SIM_TRAP_DIVIDE_BY_ZERO, 0) == 71u &&
              es2_machine_sim_trap_exit_code(ES2_MACHINE_SIM_TRAP_INTEGER_OVERFLOW, 0) == 71u &&
              es2_machine_sim_trap_exit_code(ES2_MACHINE_SIM_TRAP_OTHER, 0) == 71u,
          "wasm trap exit codes");

    unsigned before = refusals;
    es2_machine r = {0};
    CHECK(es2_machine_call(&r, 0x7ff00020u, ES2_MACHINE_CDECL, NULL, 0) == 1 && refusals == before + 1 &&
              strcmp(last_reason, "unmapped") == 0, "unlisted VA refused (%s)", last_reason);
    CHECK(es2_machine_call(&r, VA(guest_cdecl3), ES2_MACHINE_STDCALL, args, 3) == 1 &&
              strcmp(last_reason, "stack cleanup differs from the call's ABI") == 0, "ABI mismatch (%s)", last_reason);
    CHECK(es2_machine_call(&r, VA(guest_cdecl3), ES2_MACHINE_CDECL, args, 2) == 1 &&
              strcmp(last_reason, "fewer argument slots than the entry reads") == 0, "too few slots (%s)", last_reason);
    /* Passing the limit is a model error (exit 71), not a refusal (exit 70). */
    unsigned refused = refusals;
    r.esp = limit + 12;
    CHECK(es2_machine_call(&r, VA(guest_cdecl3), ES2_MACHINE_CDECL, args, 3) == 1 && model_errors == 1 &&
              refusals == refused && strcmp(last_model_error, "the guest stack is exhausted") == 0,
          "exhaustion (%s)", last_model_error);
    r.esp = limit + 16;
    CHECK(es2_machine_call(&r, VA(guest_cdecl3), ES2_MACHINE_CDECL, args, 3) == 0, "last fitting push");
    uint32_t many[ES2_MACHINE_MAX_ARGS + 1] = {0};
    r.esp = 0;
    CHECK(es2_machine_call(&r, VA(guest_cdecl3), ES2_MACHINE_CDECL, many, ES2_MACHINE_MAX_ARGS + 1) == 1,
          "too many arguments (sim)");
    CHECK(es2_machine_x86_call(&r, VA(guest_cdecl3), ES2_MACHINE_CDECL, many, ES2_MACHINE_MAX_ARGS + 1) == 1,
          "too many arguments (x86)");
    es2_machine_plant lots[ES2_MACHINE_MAX_PLANTS + 1] = {{0}};
    CHECK(es2_machine_call_planted(&r, VA(guest_cdecl3), ES2_MACHINE_CDECL, args, 3, lots, ES2_MACHINE_MAX_PLANTS + 1,
                                   NULL) == 1 && strcmp(last_reason, "more plants than a machine call takes") == 0,
          "too many plants (sim: %s)", last_reason);
    CHECK(es2_machine_x86_call_planted(&r, VA(guest_cdecl3), ES2_MACHINE_CDECL, args, 3, lots,
                                       ES2_MACHINE_MAX_PLANTS + 1, NULL) == 1, "too many plants (x86)");
    /* The pushes end at limit + 4 (ESP limit + 20, 16 bytes); a plant 8 below the entry needs 9 more. */
    es2_machine_plant deep = {8, 1};
    r.esp = limit + 16 + 8;
    CHECK(es2_machine_call_planted(&r, VA(guest_cdecl3), ES2_MACHINE_CDECL, args, 3, &deep, 1, NULL) == 1 &&
              model_errors == 2 && strcmp(last_model_error, "the guest stack is exhausted") == 0,
          "plant below the limit (%s)", last_model_error);
    r.esp = limit + 16 + 9;
    CHECK(es2_machine_call_planted(&r, VA(guest_cdecl3), ES2_MACHINE_CDECL, args, 3, &deep, 1, NULL) == 0,
          "last fitting plant");
    CHECK(refusals == refused + 2, "the plant refusal and nothing else refused");
    /* A shim's gap: fits down to the limit, the next byte is a model error. */
    CHECK(es2_machine_sim_below_limit(0x7ff00000u, limit + 0x1000u, 0x1000u, "gap") == 0, "gap to the limit");
    CHECK(es2_machine_sim_below_limit(0x7ff00000u, limit + 0x1000u, 0x1001u, "gap") == 1 && model_errors == 3 &&
              strcmp(last_model_error, "gap") == 0, "gap below the limit");
    CHECK(es2_machine_sim_below_limit(0x7ff00000u, 0x10000u, 0x20000u, "gap") == 0, "ESP outside the guest stack");
    /* The C entries' primitives: a probe is that check; the floor is ESP. */
    CHECK(es2_machine_probe(0x7ff00000u, limit + 0x1000u, 0x1000u) == 0 && model_errors == 3, "probe to the limit");
    CHECK(es2_machine_probe(0x7ff00000u, limit + 0x1000u, 0x1001u) == 1 && model_errors == 4 &&
              strcmp(last_model_error, "a shim's stack gap passes the guest stack's limit") == 0,
          "probe below the limit (%s)", last_model_error);
    CHECK(es2_machine_floor(limit + 0x1000u) == limit + 0x1000u, "sim floor is ESP");
}

static void x86_rules(void) {
    CHECK(es2_machine_x86_fs0() == (uint32_t)(uintptr_t)((NT_TIB *)NtCurrentTeb())->ExceptionList, "x86 FS:0");
    /* The callee starts with the caller's x87 state; the caller continues with the callee's. */
    uint16_t saved, cw;
    __asm__ volatile("fnstcw %0" : "=m"(saved));
    es2_machine m = {0};
    uint32_t arg = 0x0c7fu;
    es2_machine_x86_call(&m, VA(guest_fldcw), ES2_MACHINE_CDECL, &arg, 1);
    __asm__ volatile("fnstcw %0" : "=m"(cw));
    __asm__ volatile("fldcw %0" : : "m"(saved));
    CHECK(cw == 0x0c7fu, "x87 control word after the call %04x", cw);

    /* The floor: 0 outside a call and under a call on the host stack; under a
     * call that moves ESP, the host ESP it left, below this frame. */
    uint32_t here = VA(&m);
    es2_machine f = {0};
    CHECK(es2_machine_x86_floor == 0, "floor before");
    es2_machine_x86_call(&f, VA(guest_floor), ES2_MACHINE_CDECL, NULL, 0);
    CHECK(f.eax == 0, "floor under a host-stack call %08x", f.eax);
    f.esp = VA(region_x86 + TOP);
    es2_machine_x86_call(&f, VA(guest_floor), ES2_MACHINE_CDECL, NULL, 0);
    CHECK(f.eax != 0 && f.eax < here && here - f.eax < 4096u, "floor under a switched call %08x (frame %08x)", f.eax,
          here);
    CHECK(es2_machine_x86_floor == 0, "floor after %08x", es2_machine_x86_floor);
}

/* The sim backend's IDIV against the instruction (x86 backend's body), and
 * its fault codes through the callback. */
static uint32_t divide_code;
static void on_divide_fault(uint32_t code, void *u) { (void)u; divide_code = code; }
static void divide_rules(void) {
    static const uint32_t pairs[][2] = {{7, 2}, {0xfffffff9u, 2}, {7, 0xfffffffeu}, {0x80000000u, 1},
                                        {0x7fffffffu, 0xffffffffu}, {0x80000000u, 2}, {0, 5}, {5, 7}};
    for (unsigned i = 0; i < sizeof pairs / sizeof pairs[0]; ++i) {
        uint32_t eax = pairs[i][0], edx;
        __asm__ volatile("cltd\n\tidivl %2" : "+a"(eax), "=&d"(edx) : "r"(pairs[i][1]) : "cc");
        uint64_t got = es2_machine_idiv(pairs[i][0], pairs[i][1]);
        CHECK(got == ((uint64_t)edx << 32 | eax), "idiv %08x / %08x: %016llx", pairs[i][0], pairs[i][1],
              (unsigned long long)got);
    }
    es2_machine_sim_on_fault(on_divide_fault, NULL);
    CHECK(es2_machine_idiv(5, 0) == 0 && divide_code == 0xc0000094u, "zero divisor %08x", divide_code);
    divide_code = 0;
    CHECK(es2_machine_idiv(0x80000000u, 0xffffffffu) == 0 && divide_code == 0xc0000095u, "overflow %08x", divide_code);
    divide_code = 0;
    CHECK(es2_machine_idiv(0x80000000u, 0xfffffffeu) == 0x40000000u && divide_code == 0, "no fault");
    es2_machine_stack_overflow();
    CHECK(divide_code == 0xc00000fdu, "stack overflow %08x", divide_code);
    es2_machine_sim_on_fault(NULL, NULL);
}

int main(void) {
    if (setup()) return 1;
    for (unsigned k = 0; k < sizeof cases / sizeof cases[0]; ++k) {
        compare_case(k, &cases[k], 0, 0);
        compare_case(k, &cases[k], 1, 0);
        compare_case(k, &cases[k], 0, 1);
        compare_case(k, &cases[k], 1, 1);
    }
    sim_rules();
    x86_rules();
    divide_rules();
    if (failures) { printf("%d failure(s)\n", failures); return 1; }
    printf("machine_test: %u calls equal through both backends\n", (unsigned)(sizeof cases / sizeof cases[0]) * 4u);
    return 0;
}
