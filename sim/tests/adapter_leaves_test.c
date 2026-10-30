/* The adapter-leaf rows (src/adapter_leaves.c) through the dispatch on a
 * synthetic guest. The breakpoint 0x467ad8: with a process record that has
 * process_fault (a recorder) the call ends the run EXITED with
 * EXCEPTION_BREAKPOINT at 0x467ad8; without a record, or with one without
 * process_fault, the row refuses and the loop is untouched. The sweep rows
 * 0x427d98/0x427da0 serve over the bound loop and refuse without one. No
 * game data. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "es2/sim/loop.h"
#include "es2/sim/memory.h"
#include "es2/sim/services.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

#define IMAGE_BASE 0x00400000u
#define IMAGE_SIZE 0x002d8000u
#define HEAP_BASE 0x20000000u
#define HEAP_SIZE 0x00100000u
#define STACK_SIZE 0x00010000u
#define BREAKPOINT 0x80000003u   /* EXCEPTION_BREAKPOINT */

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
    if (count) memcpy(s, values, 4u * count);
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

static int breakpoint(void) {
    es2_sim_result r;
    fresh(&recorder);
    CHECK(call(0x00467ad8u, NULL, 0, &r) != 0 && refusals == 1u && first_refused == 0x00467ad8u);
    CHECK(loop.outcome == ES2_SIM_LOOP_EXITED && loop.exit_code == BREAKPOINT);
    CHECK(rec.faults == 1u && rec.code == BREAKPOINT && rec.site == 0x00467ad8u);
    fresh(NULL);
    CHECK(call(0x00467ad8u, NULL, 0, &r) != 0 && refusals == 1u && first_refused == 0x00467ad8u);
    CHECK(loop.outcome == ES2_SIM_LOOP_RETURNED && rec.faults == 0);
    fresh(&no_fault);
    CHECK(call(0x00467ad8u, NULL, 0, &r) != 0 && refusals == 1u);
    CHECK(loop.outcome == ES2_SIM_LOOP_RETURNED && rec.faults == 0);
    return 0;
}

static int leaves(void) {
    es2_sim_result r;
    fresh(NULL);
    wr(0x004aaad0u, 0x12345678u);
    CHECK(call(0x00427da0u, NULL, 0, &r) == 0 && r.eax == 0x12345678u);
    CHECK(call(0x00427d98u, NULL, 0, &r) == 0 && r.eax == 0 && rd(0x004aaad0u) == 0);
    es2_sim_loop_bind(NULL);
    refusals = 0;
    CHECK(call(0x00427da0u, NULL, 0, &r) != 0 && refusals == 1u && first_refused == 0x00427da0u);
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
    if (breakpoint() || leaves()) return 1;
    puts("PASS: the breakpoint 0x467ad8 ends the run EXITED with 0x80000003 at 0x467ad8 under process_fault and refuses "
         "without it; the sweep rows 0x427d98/0x427da0 serve over the bound loop and refuse without one");
    return 0;
}
