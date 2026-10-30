/* D: the original's fault paths (own_process.md "The original's fault
 * paths") on a synthetic guest. The divide primitive raises the CPU's fault
 * for the operands and returns the OS's code (a zero divisor, an IDIV and a
 * DIV quotient past 32 bits, no fault); es2_sim_loop_fault and the divide
 * helpers end the run with a process record that has process_fault (a
 * recorder: outcome ES2_SIM_LOOP_EXITED, the code as exit code, no refusal
 * of their own) and refuse without one; the rows 0x47de2d and 0x492b04
 * through the dispatch the same way over the bound loop; an own NULL row
 * (es2_sim_loop_null_refuse) with 0xc0000005. The primitive in C
 * (es2/core/divide.h) equals the CPU's IDIV and DIV (code, quotient and
 * remainder) over every edge pair and a pseudo-random sample, and its raise
 * gives the CPU's codes, also through the HUD speed. No game data. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "es2/core/divide.h"
#include "es2/core/movement.h"
#include "es2/sim/cpu_fault.h"
#include "es2/sim/loop.h"
#include "es2/sim/memory.h"
#include "es2/sim/services.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

#define IMAGE_BASE 0x00400000u
#define IMAGE_SIZE 0x002d8000u
#define HEAP_BASE 0x20000000u
#define HEAP_SIZE 0x00100000u
#define STACK_SIZE 0x00010000u
#define DIVIDE_BY_ZERO 0xc0000094u   /* STATUS_INTEGER_DIVIDE_BY_ZERO */
#define OVERFLOW 0xc0000095u         /* STATUS_INTEGER_OVERFLOW */

static uint8_t *image, *heap_bytes, *stack_bytes;
static es2_sim_arena arena;
static es2_guest_memory memory;
static es2_sim_heap heap;
static es2_vt vt;
static es2_sim_services services;
static es2_sim_entry storage[4096];
static es2_sim_dispatch table;
static es2_sim_loop loop;
static unsigned refusals;
static uint32_t first_refused;
static void on_refuse(const es2_sim_refusal *r, void *user) {
    (void)user;
    if (!refusals) first_refused = r->va;
    ++refusals;
}

static struct { unsigned faults; uint32_t code, site; } rec;
static void rec_fault(void *user, uint32_t code, uint32_t site) {
    (void)user;
    ++rec.faults;
    rec.code = code;
    rec.site = site;
}
static es2_sim_process_platform recorder = {NULL, NULL, NULL, NULL, rec_fault};
static es2_sim_process_platform no_fault = {NULL, NULL, NULL, NULL, NULL};

static uint32_t next_reserve = HEAP_BASE;
static uint32_t os_alloc(void *c, uint32_t address, uint32_t size, uint32_t type, uint32_t protect) {
    (void)c; (void)protect;
    if (type & ES2_HEAP_MEM_RESERVE) {
        if (!address) address = next_reserve;
        if (address < HEAP_BASE || address + size > HEAP_BASE + HEAP_SIZE) return 0;
        next_reserve = address + size;
    }
    return address;
}
static uint32_t os_free(void *c, uint32_t a, uint32_t s, uint32_t t) { (void)c; (void)a; (void)s; (void)t; return 1; }
static void wr(uint32_t a, uint32_t v) { memory.write32(memory.context, a, v); }
static uint32_t rd(uint32_t a) { return memory.read32(memory.context, a); }
static uint8_t *stack_alloc(void) {
    for (uint32_t a = 0x30000000u; a < 0x38000000u; a += 0x01000000u) {
        void *p = VirtualAlloc((void *)(uintptr_t)a, STACK_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (p) return p;
    }
    return NULL;
}
static int call(uint32_t va, const uint32_t *values, unsigned count, es2_sim_result *r) {
    uint32_t *s = (uint32_t *)(stack_bytes + 0x100);
    memcpy(s, values, 4u * count);
    const uint32_t regs[3] = {0, 0, 0};
    memset(r, 0, sizeof(*r));
    return es2_sim_call(&table, va, s, count, regs, r);
}
static void fresh(const es2_sim_process_platform *process) {
    es2_sim_loop_defaults(&loop);
    loop.memory = &memory;
    loop.dispatch = &table;
    loop.process = process;
    es2_sim_loop_bind(&loop);
    memset(&rec, 0, sizeof(rec));
    refusals = 0;
    first_refused = 0;
}

static int primitive(void) {
    CHECK(es2_sim_divide_fault(5u, 0u, 1) == DIVIDE_BY_ZERO);
    CHECK(es2_sim_divide_fault(5u, 0u, 0) == DIVIDE_BY_ZERO);
    CHECK(es2_sim_divide_fault((uint64_t)(int64_t)INT32_MIN, 0xffffffffu, 1) == OVERFLOW);   /* CDQ; IDIV of -2^31 by -1 */
    CHECK(es2_sim_divide_fault((uint64_t)1 << 40, 2u, 1) == OVERFLOW);
    CHECK(es2_sim_divide_fault((uint64_t)60u << 32, 60u, 0) == OVERFLOW);                  /* DIV: the high dword >= the divisor */
    CHECK(es2_sim_divide_fault(100u, 7u, 1) == 0 && es2_sim_divide_fault((uint64_t)59u << 32, 60u, 0) == 0);
    CHECK(es2_sim_divide_fault(5u, 0u, 1) == DIVIDE_BY_ZERO);   /* again: the handler went with the call */
    return 0;
}

/* One operand pair through both: the code, and EDX:EAX (0 after a fault). */
static unsigned compared, faulted;
static int same(uint64_t n, uint32_t d, int is_signed) {
    uint64_t cpu = 0;
    uint32_t q = 1, r = 1;
    const uint32_t code_cpu = es2_sim_divide_cpu(n, d, is_signed, &cpu), code_c = es2_divide(n, d, is_signed, &q, &r);
    ++compared;
    if (code_cpu) ++faulted;
    if (code_cpu == code_c && cpu == ((uint64_t)r << 32 | q)) return 0;
    fprintf(stderr, "FAIL: %s %016llx / %08lx: cpu %08lx %016llx, C %08lx %08lx:%08lx\n", is_signed ? "idiv" : "div",
            (unsigned long long)n, (unsigned long)d, (unsigned long)code_cpu, (unsigned long long)cpu, (unsigned long)code_c,
            (unsigned long)r, (unsigned long)q);
    return 1;
}
static int both(uint64_t n, uint32_t d) { return same(n, d, 1) || same(n, d, 0); }
static uint64_t sext(uint32_t x) { return (uint64_t)(int64_t)(int32_t)x; }
static uint64_t rand_state = 0x9e3779b97f4a7c15ull;
static uint64_t next_rand(void) {
    rand_state ^= rand_state << 13; rand_state ^= rand_state >> 7; rand_state ^= rand_state << 17;
    return rand_state;
}

static int sweep(void) {
    static const uint32_t edges[] = {0u, 1u, 2u, 3u, 7u, 10000u, 0xffffu, 0x10000u, 0x7ffffffeu, 0x7fffffffu, 0x80000000u,
                                     0x80000001u, 0xffff0000u, 0xffffd8f0u, 0xfffffffdu, 0xfffffffeu, 0xffffffffu};
    static const uint32_t highs[] = {0u, 1u, 0x7fffffffu, 0x80000000u, 0xfffffffeu, 0xffffffffu};
    const unsigned ne = sizeof(edges) / sizeof(edges[0]), nh = sizeof(highs) / sizeof(highs[0]);
    for (unsigned j = 0; j < ne; ++j) {
        const uint32_t d = edges[j];
        /* Every edge value as the low dword, CDQ-extended, and under each high dword. */
        for (unsigned i = 0; i < ne; ++i) {
            if (both(edges[i], d) || both(sext(edges[i]), d)) return 1;
            for (unsigned h = 0; h < nh; ++h)
                if (both((uint64_t)highs[h] << 32 | edges[i], d)) return 1;
        }
        if (!d) continue;
        /* IDIV: dividends whose quotient is INT_MAX or INT_MIN (just fits) and
         * one beyond each (just overflows), with no remainder and with the
         * largest one of the dividend's sign. */
        const int64_t sd = (int32_t)d, ad = sd < 0 ? -sd : sd;
        static const int64_t quotients[] = {INT32_MAX, (int64_t)INT32_MAX + 1, INT32_MIN, (int64_t)INT32_MIN - 1};
        for (unsigned k = 0; k < 4; ++k) {
            const int64_t base = quotients[k] * sd, rest = base < 0 ? -(ad - 1) : ad - 1;
            if (same((uint64_t)base, d, 1) || same((uint64_t)(base + rest), d, 1)) return 1;
        }
        /* DIV: quotients 2^32 - 1 (fits) and 2^32 (overflows), remainder 0 and d - 1. */
        for (uint64_t q = 0xffffffffull; q <= 0x100000000ull; ++q)
            if (same(q * d, d, 0) || same(q * d + (d - 1u), d, 0)) return 1;
    }
    const unsigned edge_pairs = compared;
    /* The sample: dividends and divisors of every magnitude and sign. */
    for (unsigned i = 0; i < 20000u; ++i) {
        const uint64_t a = next_rand(), b = next_rand();
        uint64_t n = a >> (b & 63u);
        if (b & 0x40u) n = 0u - n;
        uint32_t d = (uint32_t)(b >> 32) >> ((b >> 8) & 31u);
        if (b & 0x80u) d = 0u - d;
        if (both(n, d)) return 1;
    }
    printf("divide sweep: %u edge pairs and %u sampled ones equal (%u faulting)\n", edge_pairs, compared - edge_pairs,
           faulted);
    return 0;
}

/* es2_divide_raise's divide on the host: a vectored handler of the test
 * steps over the IDIV r/m32 (F7 /7) on this thread and keeps the code. */
static DWORD raise_thread;
static volatile uint32_t raised;
static LONG CALLBACK step_idiv(EXCEPTION_POINTERS *x) {
    const uint8_t *at = x->ExceptionRecord->ExceptionAddress;
    if (GetCurrentThreadId() != raise_thread || at[0] != 0xf7u || ((at[1] >> 3) & 7u) != 7u) return EXCEPTION_CONTINUE_SEARCH;
    const unsigned mod = at[1] >> 6, rm = at[1] & 7u;
    unsigned length = 2;
    if (mod != 3u && rm == 4u) {
        ++length;
        if (mod == 0u && (at[2] & 7u) == 5u) length += 4;
    }
    if (mod == 1u) length += 1;
    else if (mod == 2u || (mod == 0u && rm == 5u)) length += 4;
    raised = (uint32_t)x->ExceptionRecord->ExceptionCode;
    x->ContextRecord->Eip += length;
    return EXCEPTION_CONTINUE_EXECUTION;
}
static int raise_paths(void) {
    PVOID handler = AddVectoredExceptionHandler(1, step_idiv);
    CHECK(handler);
    raise_thread = GetCurrentThreadId();
    int bad = 0;
    raised = 0; es2_divide_raise(DIVIDE_BY_ZERO); bad |= (raised != DIVIDE_BY_ZERO) << 0;
    raised = 0; es2_divide_raise(OVERFLOW); bad |= (raised != OVERFLOW) << 1;
    raised = 0; es2_divide_raise(0); bad |= (raised != 0) << 2;
    uint32_t r = 1;
    raised = 0; bad |= (es2_divide_or_raise(7u, 0u, 1, &r) != 0 || r != 0 || raised != DIVIDE_BY_ZERO) << 3;
    raised = 0; bad |= (es2_divide_or_raise(sext(0x80000000u), 0xffffffffu, 1, &r) != 0 || raised != OVERFLOW) << 4;
    raised = 0; bad |= (es2_divide_or_raise((uint64_t)1 << 32, 1u, 0, &r) != 0 || raised != OVERFLOW) << 5;
    raised = 0; bad |= (es2_divide_or_raise(sext(0xfffffff9u), 2u, 1, &r) != 0xfffffffdu || r != 0xffffffffu || raised != 0) << 6;
    /* The HUD speed (0x41bb3c): a zero C+6, a zero C+0x206 and a quotient past 32 bits. */
    uint8_t a[0x2c1] = {0}, c[0x20a] = {0};
    a[0x28e] = 3; c[0xc2] = 10;
    raised = 0; bad |= (es2_hud_speed(a, c) != 0 || raised != DIVIDE_BY_ZERO) << 7;
    c[0x50] = 1; a[0x2bd + 3] = 0x40;
    raised = 0; bad |= (es2_hud_speed(a, c) != 0 || raised != DIVIDE_BY_ZERO) << 8;
    c[0x206] = 1;
    raised = 0; bad |= (es2_hud_speed(a, c) != 0 || raised != OVERFLOW) << 9;
    raise_thread = 0;
    RemoveVectoredExceptionHandler(handler);
    if (bad) fprintf(stderr, "raise paths: failed checks 0x%x\n", (unsigned)bad);
    CHECK(!bad);
    return 0;
}

static int loop_paths(void) {
    CHECK(es2_sim_loop_fault(NULL, DIVIDE_BY_ZERO, 1u) == 0 && es2_sim_loop_divide_fault(NULL, 1u, 1u, 0u, 1) == 0);
    fresh(NULL);
    CHECK(es2_sim_loop_fault(&loop, DIVIDE_BY_ZERO, 0x00401000u) == 0 && loop.outcome == ES2_SIM_LOOP_RETURNED);
    CHECK(es2_sim_loop_divide_refuse(&loop, 0x00401000u, 1u, 0u, 1, "case", "zero") == 1);
    CHECK(loop.outcome == ES2_SIM_LOOP_REFUSED && loop.refused_va == 0x00401000u && refusals == 1u && first_refused == 0x00401000u);
    fresh(&no_fault);   /* a record without process_fault refuses as before D */
    CHECK(es2_sim_loop_divide_refuse(&loop, 0x00401004u, 1u, 0u, 1, "case", "zero") == 1);
    CHECK(loop.outcome == ES2_SIM_LOOP_REFUSED && refusals == 1u);
    fresh(&recorder);
    CHECK(es2_sim_loop_fault(&loop, 0xc0000005u, 0x00401008u) == 1);
    CHECK(loop.outcome == ES2_SIM_LOOP_EXITED && loop.exit_code == 0xc0000005u && rec.faults == 1u && rec.site == 0x00401008u);
    fresh(&recorder);
    CHECK(es2_sim_loop_divide_refuse(&loop, 0x0040100cu, (uint64_t)1 << 40, 2u, 1, "case", "overflow") == 1);
    CHECK(loop.outcome == ES2_SIM_LOOP_EXITED && loop.exit_code == OVERFLOW && rec.code == OVERFLOW && refusals == 0);
    fresh(&recorder);   /* operands that do not fault: no process end, the refusal names the model's error */
    CHECK(es2_sim_loop_divide_fault(&loop, 0x00401010u, 100u, 7u, 1) == 0 && rec.faults == 0);
    CHECK(es2_sim_loop_divide_refuse(&loop, 0x00401010u, 100u, 7u, 1, "case", "no fault") == 1);
    CHECK(loop.outcome == ES2_SIM_LOOP_REFUSED && refusals == 1u);
    return 0;
}

/* An own row's NULL access: the access violation at the row with the
 * record, the refusal without; a free outside the heap stays a refusal. */
static int null_paths(void) {
    fresh(NULL);
    CHECK(es2_sim_loop_null_refuse(&loop, 0x00402000u, "case", "null") == 1);
    CHECK(loop.outcome == ES2_SIM_LOOP_REFUSED && loop.refused_va == 0x00402000u && refusals == 1u);
    fresh(&no_fault);
    CHECK(es2_sim_loop_null_refuse(&loop, 0x00402004u, "case", "null") == 1 && loop.outcome == ES2_SIM_LOOP_REFUSED);
    fresh(&recorder);
    CHECK(es2_sim_loop_null_refuse(&loop, 0x00402008u, "case", "null") == 1);
    CHECK(loop.outcome == ES2_SIM_LOOP_EXITED && loop.exit_code == ES2_SIM_ACCESS_VIOLATION && refusals == 0);
    CHECK(rec.faults == 1u && rec.code == 0xc0000005u && rec.site == 0x00402008u);
    es2_sim_result r;
    fresh(&recorder);
    CHECK(call(0x004967a8u, (const uint32_t[]){IMAGE_BASE + 0x1000u}, 1, &r) != 0);
    CHECK(refusals == 1u && first_refused == 0x004967a8u && rec.faults == 0 && loop.outcome != ES2_SIM_LOOP_EXITED);
    return 0;
}

static int rows(void) {
    es2_sim_result r;
    uint32_t out = HEAP_BASE + 0x80000u, num = HEAP_BASE + 0x80010u;
    wr(num, 0x10u);
    wr(num + 4u, 0);
    /* 0x47de2d: 16 / 3, then a zero divisor without and with the record */
    fresh(&recorder);
    CHECK(call(0x0047de2du, (const uint32_t[]){out, num, 3u}, 3, &r) == 0 && rd(out) == 5u && rd(out + 4u) == 1u);
    fresh(NULL);
    CHECK(call(0x0047de2du, (const uint32_t[]){out, num, 0u}, 3, &r) != 0 && refusals == 1u && first_refused == 0x0047de2du);
    CHECK(loop.outcome == ES2_SIM_LOOP_RETURNED && rec.faults == 0);
    fresh(&recorder);
    CHECK(call(0x0047de2du, (const uint32_t[]){out, num, 0u}, 3, &r) != 0);
    CHECK(loop.outcome == ES2_SIM_LOOP_EXITED && loop.exit_code == DIVIDE_BY_ZERO);
    CHECK(rec.faults == 1u && rec.code == DIVIDE_BY_ZERO && rec.site == 0x0047de3cu);
    /* 0x47eabe passes to 0x47de2d: 2^40 / 2 does not fit 32 bits */
    wr(num, 0);
    wr(num + 4u, 0x100u);
    fresh(&recorder);
    CHECK(call(0x0047eabeu, (const uint32_t[]){out, num, 2u}, 3, &r) != 0);
    CHECK(loop.exit_code == OVERFLOW && rec.code == OVERFLOW && rec.site == 0x0047de3cu);
    /* 0x492b04: INT_MIN / -1 */
    fresh(&recorder);
    CHECK(call(0x00492b04u, (const uint32_t[]){0x80000000u, 0xffffffffu}, 2, &r) != 0);
    CHECK(loop.exit_code == OVERFLOW && rec.code == OVERFLOW && rec.site == 0x00492b0bu);
    fresh(&recorder);
    CHECK(call(0x00492b04u, (const uint32_t[]){(uint32_t)-7, 2u}, 2, &r) == 0 && r.eax == (uint32_t)-3 && r.edx == (uint32_t)-1);
    CHECK(rec.faults == 0);
    return 0;
}

int main(void) {
    image = calloc(1, IMAGE_SIZE);
    heap_bytes = calloc(1, HEAP_SIZE);
    stack_bytes = stack_alloc();
    CHECK(image && heap_bytes && stack_bytes);
    CHECK(es2_sim_arena_add(&arena, IMAGE_BASE, image, IMAGE_SIZE));
    CHECK(es2_sim_arena_add(&arena, HEAP_BASE, heap_bytes, HEAP_SIZE));
    CHECK(es2_sim_arena_add(&arena, (uint32_t)(uintptr_t)stack_bytes, stack_bytes, STACK_SIZE));
    es2_sim_arena_memory(&arena, &memory);
    es2_sim_heap_os os = {NULL, os_alloc, os_free};
    es2_sim_heap_init(&heap, &memory, &os, (uint32_t)(uintptr_t)stack_bytes + 0x10u, 0, 0, 0);
    es2_vt_enable(&vt, 1, 47, 0x01000000u, 0, 1);
    uint32_t fs0 = 0xffffffffu;
    services.memory = &memory; services.heap = &heap; services.vt = &vt; services.fs0 = &fs0;
    es2_sim_dispatch_init(&table, storage, 4096, on_refuse, NULL);
    CHECK(!es2_sim_register_services(&table, &services));
    const char *error = NULL;
    uint32_t bad = 0;
    CHECK(!es2_sim_dispatch_seal(&table, &error, &bad));
    if (primitive() || sweep() || raise_paths() || loop_paths() || null_paths() || rows()) return 1;
    puts("PASS: D: the divide primitive returns the OS's code (zero divisor 0xc0000094, IDIV/DIV overflow 0xc0000095, none 0); "
         "the primitive in C equals it with quotient and remainder, and its raise gives the same codes; "
         "with process_fault the loop and the rows 0x47de2d/0x47eabe/0x492b04 end EXITED with that code at the IDIV, "
         "without it they refuse as before; a NULL row ends EXITED with 0xc0000005 at the row or refuses without "
         "the record; a free outside the heap stays a refusal");
    return 0;
}
