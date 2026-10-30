/* The stage-1 trap as a guard (es2/sim/trap.h) on a synthetic non-executable
 * code page, no game data. Mode `calls`: with the trap installed, every entry
 * kind and ABI through the C path (es2_sim_call), nesting, and a C call's
 * refusal; no call traps. Mode `refusal`: one child process per jump into the
 * trapped ranges (an unmapped VA, a listed VA, an import-slot trap, an address
 * between two slot traps) ends with ES2_SIM_EXIT_REFUSED after one refusal
 * "unconverted call" with the VA, its name, the return address and the call's
 * ESP; an execute fault outside the ranges is not the trap's. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "es2/sim/trap.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

static uint8_t *code;
static uint32_t A(uint32_t offset) { return (uint32_t)(uintptr_t)code + offset; }

/* ---- entries */
static es2_sim_dispatch table;
static void svc_sum(es2_sim_service_call *c, void *user) {
    (void)user;
    c->eax = c->slots[0] + c->slots[1] + c->slots[2];
    c->edx ^= 1u;
    c->ecx = 0x77u;
}
static void svc_mul(es2_sim_service_call *c, void *user) { (void)user; c->eax = c->slots[0] * c->slots[1]; }
static void svc_nested(es2_sim_service_call *c, void *user) {
    (void)user;
    const uint32_t inner[3] = {c->slots[0], 10, 20}, regs[3] = {0, 0, 0};
    es2_sim_result r;
    if (es2_sim_call(&table, A(0x10), inner, 3, regs, &r)) { c->refused = 1; c->reason = "inner call refused"; return; }
    c->eax = r.eax + 1u;
}
static void svc_import(es2_sim_service_call *c, void *user) { (void)user; c->eax = 0xabcdu; }

static uint64_t __cdecl hook_borland(const uint32_t *a, void *user) {
    (void)user;
    return (uint64_t)a[3] | (uint64_t)a[4] << 32 | (a[0] == 1 && a[1] == 2 && a[2] == 4 ? 0 : 0x8000000000000000ull);
}
static uint64_t __cdecl hook_context(const uint32_t *a, void *user, const uint32_t regs[3]) {
    (void)user;
    return (uint64_t)(a[0] + a[1]) | (uint64_t)regs[2] << 32;
}
static uint32_t __cdecl hook_legacy(const uint32_t a[ES2_HOOK_MAX_ARGS], void *user) { (void)user; return a[0] - a[1]; }

static es2_hook_descriptor d_borland = {"t_borland", 0, NULL, 0, 0, NULL, 0, 5, NULL, NULL, NULL, NULL, 0};
static es2_hook_extension x_borland = {ES2_ABI_BORLAND, ES2_RESULT_PAIR, hook_borland, NULL, NULL, 0, NULL, FALSE};
static es2_hook_descriptor d_context = {"t_context", 0, NULL, 0, 0, NULL, 0, 2, NULL, NULL, NULL, NULL, 0};
static es2_hook_extension x_context = {ES2_ABI_STDCALL, ES2_RESULT_PAIR, NULL, NULL, NULL, 0, hook_context, FALSE};
static es2_hook_descriptor d_legacy = {"t_legacy", 0, NULL, 0, 0, NULL, 0, 2, hook_legacy, NULL, NULL, NULL, 0};

static es2_sim_entry storage[32];
static es2_sim_refusal last_refusal;
static unsigned refusal_count;
/* The refusal a child expects; a mismatch exits with 1 instead of 70. */
static struct { uint32_t va; const char *name; } want;
static int child;
static void on_refuse(const es2_sim_refusal *r, void *user) {
    (void)user;
    last_refusal = *r;
    ++refusal_count;
    if (!child) return;
    MEMORY_BASIC_INFORMATION m;
    int ok = r->va == want.va && !strcmp(r->reason, "unconverted call") && refusal_count == 1 && es2_sim_trap_calls() == 1u &&
             (want.name ? r->name && !strcmp(r->name, want.name) : !r->name) && r->frame &&
             *(const uint32_t *)(uintptr_t)r->frame == r->return_address &&
             VirtualQuery((void *)(uintptr_t)r->return_address, &m, sizeof(m)) && m.AllocationBase == GetModuleHandleW(NULL);
    const es2_sim_entry *e = es2_sim_dispatch_find(&table, r->va);
    if (e && e->traps != 1u) ok = 0;
    printf("refused 0x%08lx (%s): %s, return 0x%08lx, frame 0x%08lx%s\n", (unsigned long)r->va, r->name ? r->name : "-",
           r->reason, (unsigned long)r->return_address, (unsigned long)r->frame, ok ? "" : " MISMATCH");
    fflush(stdout);
    if (!ok) ExitProcess(1);
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
    CHECK(!es2_sim_dispatch_add_hook(&table, A(0x50), "context", &d_context, &x_context));
    CHECK(!es2_sim_dispatch_add_hook(&table, A(0x70), "legacy", &d_legacy, NULL));
    CHECK(!es2_sim_dispatch_add_service(&table, A(0x80), "nested", svc_nested, NULL, 0, 1));
    CHECK(!es2_sim_dispatch_add_service(&table, 0x006cc3e8u, "import_service", svc_import, NULL, 0, 0));
    const char *error = NULL; uint32_t bad = 0;
    CHECK(!es2_sim_dispatch_seal(&table, &error, &bad));
    es2_sim_trap_range ranges[2] = {{A(0), A(0x1000), NULL, 0}, {A(0x1000), A(0x1008), import_keys, 4}};
    CHECK(!es2_sim_trap_install(&table, ranges, 2));
    return 0;
}

static int calls(void) {
    static const uint32_t three[3] = {1, 2, 3}, two[2] = {6, 7}, b[2] = {0x100, 0x200}, five[1] = {5};
    es2_sim_result r;
    uint32_t regs[3] = {0xe0, 0xd0, 0xc0};
    CHECK(!es2_sim_call(&table, A(0x10), three, 3, regs, &r) && r.eax == 6 && r.edx == 0xd1 && r.ecx == 0x77 && r.pop_bytes == 0);
    regs[0] = 0; regs[1] = 0x44; regs[2] = 0x55;
    CHECK(!es2_sim_call(&table, A(0x20), two, 2, regs, &r) && r.eax == 42 && r.edx == 0x44 && r.ecx == 0x55 && r.pop_bytes == 8);
    regs[0] = 1; regs[1] = 2; regs[2] = 4;
    CHECK(!es2_sim_call(&table, A(0x30), b, 2, regs, &r) && r.eax == 0x200 && r.edx == 0x100 && r.ecx == 4 && r.pop_bytes == 8);
    regs[0] = 0; regs[1] = 0; regs[2] = 0x99;
    CHECK(!es2_sim_call(&table, A(0x50), two, 2, regs, &r) && r.eax == 13 && r.edx == 0x99 && r.ecx == 0x99 && r.pop_bytes == 8);
    regs[2] = 3;
    CHECK(!es2_sim_call(&table, A(0x70), two, 2, regs, &r) && r.eax == (uint32_t)-1 && r.pop_bytes == 0 && r.ecx == 3);
    CHECK(!es2_sim_call(&table, A(0x80), five, 1, regs, &r) && r.eax == 36);              /* nested: 5 + 10 + 20 + 1 */
    CHECK(!es2_sim_call(&table, 0x006cc3e8u, NULL, 0, regs, &r) && r.eax == 0xabcd);     /* the import slot's service */
    const es2_sim_entry *e = es2_sim_dispatch_find(&table, A(0x10));
    CHECK(e && e->calls == 2 && e->traps == 0);                                          /* direct, nested */
    /* A C call's refusal returns 1. */
    CHECK(es2_sim_call(&table, A(0x94), NULL, 0, regs, &r) == 1);
    CHECK(refusal_count == 1 && table.refusals == 1 && last_refusal.va == A(0x94) && !strcmp(last_refusal.reason, "unmapped") &&
          !last_refusal.return_address);
    CHECK(es2_sim_trap_calls() == 0);
    puts("PASS: C path with the trap installed: service, stdcall, Borland, context, legacy, nested, import slot, refusal; "
         "no trapped call");
    return 0;
}

/* ---- the guard: one child per jump */
static const struct { uint32_t offset; uint32_t key; const char *name; } cases[] = {
    {0x90, 0, NULL},                                      /* unmapped VA: keyed by itself */
    {0x10, 0, "sum"},                                     /* a listed VA: still an unconverted call */
    {0x1000, 0x006cc3e8u, "import_service"},              /* the first import-slot trap: keyed by its slot */
    {0x1004, 0x006cc2d4u, "cw3220.DLL!_strupr"},          /* a slot without a service: named by its import */
    {0x1002, 0, NULL},                                    /* between two slot traps: keyed by itself */
    {0x1010, 0, NULL},                                    /* outside the ranges: not the trap's fault */
};
#define CASES (sizeof(cases) / sizeof(cases[0]))
#define OUTSIDE (CASES - 1u)

static int run_child(unsigned i) {
    child = 1;
    want.va = cases[i].key ? cases[i].key : A(cases[i].offset);
    want.name = cases[i].name;
    typedef uint32_t (__cdecl *fn)(uint32_t, uint32_t, uint32_t);
    volatile fn target = (fn)(uintptr_t)A(cases[i].offset);
    uint32_t got = target(1, 2, 3);
    fprintf(stderr, "FAIL: the jump to +0x%lx returned (%lu)\n", (unsigned long)cases[i].offset, (unsigned long)got);
    return 1;
}

static int refusal(const char *self) {
    for (unsigned i = 0; i < CASES; ++i) {
        char line[MAX_PATH + 32];
        snprintf(line, sizeof(line), "\"%s\" child %u", self, i);
        STARTUPINFOA si; PROCESS_INFORMATION pi;
        memset(&si, 0, sizeof(si)); si.cb = sizeof(si);
        /* window-lint: allow inherits console: the child is this test, sharing its Bazel console */
        CHECK(CreateProcessA(NULL, line, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi));
        CHECK(WaitForSingleObject(pi.hProcess, 60000) == WAIT_OBJECT_0);
        DWORD exit_code = 0;
        CHECK(GetExitCodeProcess(pi.hProcess, &exit_code));
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        printf("case %u (+0x%lx): exit 0x%lx\n", i, (unsigned long)cases[i].offset, (unsigned long)exit_code);
        if (i == OUTSIDE) CHECK(exit_code == (DWORD)EXCEPTION_ACCESS_VIOLATION);
        else CHECK(exit_code == ES2_SIM_EXIT_REFUSED);
    }
    puts("PASS: unconverted calls (unmapped VA, listed VA, import slot, slot without a service, between slots) refused with 70; "
         "a fault outside the ranges not handled");
    return 0;
}

int main(int argc, char **argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (argc == 3 && !strcmp(argv[1], "child")) {
        unsigned i = (unsigned)strtoul(argv[2], NULL, 10);
        if (i >= CASES || setup()) return 1;
        return run_child(i);
    }
    if (argc != 2) return 2;
    if (setup()) return 1;
    if (!strcmp(argv[1], "calls")) return calls();
    if (!strcmp(argv[1], "refusal")) {
        char self[MAX_PATH];
        DWORD n = GetModuleFileNameA(NULL, self, sizeof(self));
        CHECK(n && n < sizeof(self));
        return refusal(self);
    }
    return 2;
}
