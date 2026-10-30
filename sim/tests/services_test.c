/* The runtime services (sim/dispatch/services.h) on a synthetic guest: the
 * image range 0x400000..0x6d7fff as zeroed host memory, a heap arena and a
 * stack arena for argument slots, called through es2_sim_call and directly.
 * No game data. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "es2/sim/services.h"
#include "es2/sim/memory.h"
#include "es2/sim/residue.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

#define IMAGE_BASE 0x00400000u
#define IMAGE_SIZE 0x002d8000u
#define HEAP_BASE 0x20000000u
#define HEAP_SIZE 0x00800000u
#define STACK_SIZE 0x00010000u

static uint8_t *image, *heap_bytes, *stack_bytes;
static es2_sim_arena arena;
static es2_guest_memory memory;
static es2_sim_heap heap;
static es2_vt vt;
static es2_sim_services services;
static es2_sim_entry storage[4096];
static es2_sim_dispatch table;
static es2_sim_refusal last;
static unsigned refusals;
static void on_refuse(const es2_sim_refusal *r, void *user) { (void)user; last = *r; ++refusals; }

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

static uint32_t rd(uint32_t a) { return memory.read32(memory.context, a); }
static void wr(uint32_t a, uint32_t v) { memory.write32(memory.context, a, v); }
/* Argument slots live in the stack arena (the rtl models read them by guest address). */
static uint32_t *slots_at(unsigned i) { return (uint32_t *)(stack_bytes + 0x100 + 0x40 * i); }
static int call(uint32_t va, const uint32_t *values, unsigned count, uint32_t eax, uint32_t edx, uint32_t ecx, es2_sim_result *r) {
    static unsigned which;
    uint32_t *s = slots_at(which++ & 7u);
    memcpy(s, values, 4u * count);
    const uint32_t regs[3] = {eax, edx, ecx};
    return es2_sim_call(&table, va, s, count, regs, r);
}
/* es2_sim_call passes the host address of the slots as their guest address;
 * map the stack arena at that host address so both views agree. */
static uint32_t stack_guest(void) { return (uint32_t)(uintptr_t)stack_bytes; }
/* The stack arena must not overlap the synthetic image and heap ranges, which
 * a calloc'ed block can (its host address is the CRT heap's choice): reserve
 * it at a fixed address outside both, trying a few in case one is taken. */
static uint8_t *stack_alloc(void) {
    for (uint32_t a = 0x30000000u; a < 0x38000000u; a += 0x01000000u) {
        void *p = VirtualAlloc((void *)(uintptr_t)a, STACK_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (p) return p;
    }
    return NULL;
}

int main(void) {
    image = calloc(1, IMAGE_SIZE); heap_bytes = calloc(1, HEAP_SIZE); stack_bytes = stack_alloc();
    CHECK(image && heap_bytes && stack_bytes);
    CHECK(es2_sim_arena_add(&arena, IMAGE_BASE, image, IMAGE_SIZE));
    CHECK(es2_sim_arena_add(&arena, HEAP_BASE, heap_bytes, HEAP_SIZE));
    CHECK(es2_sim_arena_add(&arena, stack_guest(), stack_bytes, STACK_SIZE));
    es2_sim_arena_memory(&arena, &memory);
    es2_sim_heap_os os = {NULL, os_alloc, os_free};
    es2_sim_heap_init(&heap, &memory, &os, stack_guest() + 0x10u, 0, 0, 0);
    es2_vt_enable(&vt, 1, 47, 0x01000000u, 0, 1);
    uint32_t fs0 = 0xffffffffu;
    services.memory = &memory; services.heap = &heap; services.vt = &vt; services.fs0 = &fs0;
    es2_sim_dispatch_init(&table, storage, 4096, on_refuse, NULL);
    CHECK(!es2_sim_register_services(&table, &services));
    const char *error = NULL; uint32_t bad = 0;
    CHECK(!es2_sim_dispatch_seal(&table, &error, &bad));
    /* Every row of services.h registers and seals. No fixed total: the table
     * grows with every package, and a literal here made each two parallel
     * packages conflict; the rows themselves are checked below and by the
     * in-vitro suites. */
    CHECK(es2_sim_service_count() > 0 && table.count == es2_sim_service_count());
    es2_sim_result r;

    /* operator new (0x47749c): EAX = p, ECX = n, EDX = [p-4]; [0x6bb30c] += [p-4].
     * The numbers of init record 53: new(0x808) -> header 0x811. */
    uint32_t n = 0x808;
    CHECK(!call(0x0047749cu, &n, 1, 0xa, 0xb, 0xc, &r));
    uint32_t p = r.eax;
    CHECK(p && r.ecx == 0x808 && r.edx == 0x811 && rd(p - 4) == 0x811 && rd(ES2_SIM_VA_HEAP_BYTES) == 0x811);
    /* new[] (0x4774f0): n + 8. */
    n = 8;
    CHECK(!call(0x004774f0u, &n, 1, 0, 0, 0, &r) && r.eax && r.ecx == 16 && rd(r.eax - 4) == 25u);
    uint32_t q = r.eax;
    CHECK(rd(ES2_SIM_VA_HEAP_BYTES) == 0x811u + 25u);
    /* delete (0x477548): [0x6bb30c] -= [p-4], ECX = p. */
    CHECK(!call(0x00477548u, &q, 1, 0, 0, 0, &r) && r.ecx == q && rd(ES2_SIM_VA_HEAP_BYTES) == 0x811u);
    CHECK(!call(0x0047758cu, &p, 1, 0, 0, 0, &r) && r.ecx == p && rd(ES2_SIM_VA_HEAP_BYTES) == 0);
    /* delete(0): EAX = ECX = EDX = 0. */
    uint32_t zero = 0;
    CHECK(!call(0x00477548u, &zero, 1, 5, 6, 7, &r) && r.eax == 0 && r.ecx == 0 && r.edx == 0);
    /* 0x47747c(0): calloc(1, 0) = NULL: ECX = 0 (count*n), EDX unchanged. */
    CHECK(!call(0x0047747cu, &zero, 1, 5, 6, 7, &r) && r.eax == 0 && r.ecx == 0 && r.edx == 6);
    /* The arena (-l) is refused: threshold set, or a header present. */
    wr(ES2_SIM_VA_ARENA_LIMIT, 0x100);
    n = 0x10;
    unsigned before = refusals;
    CHECK(call(0x0047749cu, &n, 1, 0, 0, 0, &r) == 1 && refusals == before + 1 && !strcmp(last.name, "operator_new"));
    n = 0x100;                                     /* n >= threshold: jae taken, calloc path */
    CHECK(!call(0x0047749cu, &n, 1, 0, 0, 0, &r) && r.eax);
    wr(ES2_SIM_VA_ARENA_LIMIT, 0);
    wr(ES2_SIM_VA_ARENA, 0x604f04u);
    CHECK(call(0x00477548u, &r.eax, 1, 0, 0, 0, &r) == 1);
    wr(ES2_SIM_VA_ARENA, 0);
    /* calloc/free/realloc imports. */
    uint32_t two[2] = {3, 4};
    CHECK(!call(0x0049686eu, two, 2, 0, 0, 0, &r) && r.eax && r.ecx == 0 && r.edx == 12);
    uint32_t c = r.eax;
    uint32_t re[2] = {c, 64};
    CHECK(!call(0x0049687au, re, 2, 0, 0x5, 0x6, &r) && r.eax && r.ecx == 6 && r.edx == 5);
    CHECK(!call(0x004967a8u, &r.eax, 1, 0, 0, 0, &r));
    uint32_t foreign = IMAGE_BASE + 0x100;
    CHECK(call(0x004967a8u, &foreign, 1, 0, 0, 0, &r) == 1 && !strcmp(last.reason, "free of a pointer outside the own heap"));

    /* Pure imports through the core models: memset (EAX = dest, ECX = 0, EDX = n), strlen. */
    uint32_t dest = IMAGE_BASE + 0x97000u;
    uint32_t ms[3] = {dest, 0x41, 5};
    CHECK(!call(0x0049681au, ms, 3, 0, 0, 0, &r) && r.eax == dest && r.ecx == 0 && r.edx == 5);
    CHECK(image[0x97000] == 0x41 && image[0x97004] == 0x41 && image[0x97005] == 0);
    CHECK(!call(0x00496766u, &dest, 1, 0, 0, 0, &r) && r.eax == 5);
    /* sprintf with %f is not modelled: refused, never approximated. */
    uint32_t fmt = IMAGE_BASE + 0x97100u;
    memcpy(image + 0x97100, "%f", 3);
    uint32_t sp[2] = {IMAGE_BASE + 0x97200u, fmt};
    CHECK(call(0x00496796u, sp, 2, 0, 0, 0, &r) == 1 && strstr(last.reason, "not modelled"));

    /* GetTickCount: a read (any caller, here the C path) and the limiter's poll. */
    CHECK(!call(0x004968ecu, NULL, 0, 0, 0x12, 0x34, &r) && r.eax == 0x01000000u && r.edx == 0x12 && r.ecx == 0x34);
    CHECK(!call(0x004968ecu, NULL, 0, 0, 0, 0, &r) && r.eax == 0x01000001u);      /* intra-frame advance */
    wr(ES2_SIM_VA_LIMITER_PREVIOUS, 0x02000000u);
    es2_sim_service_call tick = {0x004968ecu, ES2_SIM_VA_LIMITER_RETURN, 0, NULL, 0, 0, 0, 0, 0, NULL};
    const es2_sim_entry *e = es2_sim_dispatch_find(&table, 0x004968ecu);
    CHECK(e);
    e->service(&tick, e->service_user);
    CHECK(!tick.refused && tick.eax == 0x02000000u + 47u && vt.limiter_polls == 1);
    vt.enabled = 0;
    CHECK(call(0x004968ecu, NULL, 0, 0, 0, 0, &r) == 1);
    vt.enabled = 1;

    /* 0x49630c: refused without [0x6cbee4]; then the record as the listing writes it. */
    e = es2_sim_dispatch_find(&table, 0x0049630cu);
    CHECK(e);
    uint32_t ebp = IMAGE_BASE + 0x98000u, descriptor = IMAGE_BASE + 0x99000u;
    wr(descriptor + 4, (uint32_t)-0x24);
    es2_sim_service_call rec = {0x0049630cu, 0x00401234u, ebp - 0x40u, NULL, descriptor, 0x55, 0x66, ebp, 0, NULL};
    e->service(&rec, e->service_user);
    CHECK(rec.refused);
    wr(ES2_SIM_VA_COUNTER_POINTER, IMAGE_BASE + 0x9a000u);
    wr(IMAGE_BASE + 0x9a000u, 7);
    memset(image + 0x98000 - 0x24, 0xab, 0x24);
    rec.refused = 0;
    e->service(&rec, e->service_user);
    uint32_t record = ebp - 0x24u;
    CHECK(!rec.refused && fs0 == record && rec.eax == 0xffffffffu && rec.ecx == 0x66 && rec.edx == 0x55);
    CHECK(rd(record) == 0xffffffffu && rd(record + 4) == 0x004967f0u && rd(record + 8) == descriptor && rd(record + 0xc) == ebp - 0x40u);
    CHECK(rd(record + 0x10) == 0 && memory.read16(memory.context, record + 0x14) == 0 && rd(record + 0x1c) == 7);
    CHECK(image[0x98000 - 0x24 + 0x16] == 0xab && image[0x98000 - 0x24 + 0x1b] == 0xab);   /* +0x16..+0x1b untouched */

    /* The residue table: the known rows of own_process.md section 5. */
    CHECK(es2_sim_residue_count() >= 5);
    const es2_sim_residue *g = es2_sim_residue_find(0x004231a4u);
    CHECK(g && g->place == ES2_RESIDUE_FRAME && g->offset == -0x1c && g->width == 2 && g->rule == ES2_RESIDUE_ZERO);
    /* C: the rows checked against their writers; every reader once. */
    const es2_sim_residue *t = es2_sim_residue_find(0x0046254cu), *s = es2_sim_residue_find(0x0042e8e8u);
    CHECK(t && t->width == 0x14 && t->rule == ES2_RESIDUE_ZERO && s && s->rule == ES2_RESIDUE_WRITER);
    for (unsigned i = 0; i < es2_sim_residue_count(); ++i) CHECK(es2_sim_residue_find(es2_sim_residue_row(i)->reader) == es2_sim_residue_row(i));
    CHECK(arena.faults == 0);
    puts("PASS: services: new/new[]/delete/delete[] and 0x47747c with [0x6bb30c], arena refused, calloc/realloc/free, "
         "memset/strlen, sprintf %f refused, GetTickCount read and poll, frame record 0x49630c; residue rows");
    free(image); free(heap_bytes); VirtualFree(stack_bytes, 0, MEM_RELEASE);
    return 0;
}
