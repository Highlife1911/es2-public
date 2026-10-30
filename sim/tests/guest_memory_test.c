/* The checked guest accessor (es2/sim/guest_memory.h) over a table like
 * es2sim's: the classes, the fault callback and what a returning callback
 * leaves, the probe, the zero-length span, the canary and the heap ceiling. */
#ifdef __EMSCRIPTEN__
#define _DEFAULT_SOURCE /* sbrk */
#endif
#include <stdio.h>
#include <string.h>
#include "es2/sim/guest_memory.h"
#ifdef __EMSCRIPTEN__
#include <unistd.h>
#endif

static int failures;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); ++failures; } } while (0)

static unsigned faults;
static es2_sim_guest_fault last;
static void on_fault(const es2_sim_guest_fault *f, void *user) { (void)user; ++faults; last = *f; }

static _Alignas(16) uint8_t buffer[64];

int main(void) {
    const uint32_t va = (uint32_t)(uintptr_t)buffer;
    /* Without a table: the unchecked identity view. */
    CHECK(es2_sim_guest_span(0x10u, 4u) == (void *)(uintptr_t)0x10u, "no table");
    CHECK(es2_sim_guest_classify(0x10u, 4u, 0) == ES2_SIM_GUEST_OK, "no table: no class");

    /* es2sim's layout: the NULL zone, read-only headers, CODE pages, the barrier. */
    CHECK(es2_sim_guest_range(0, ES2_SIM_GUEST_NULL_ZONE_END, ES2_SIM_GUEST_NULL_ZONE) == 0, "NULL zone");
    CHECK(es2_sim_guest_range(0x00400000u, 0x00401000u, ES2_SIM_GUEST_READ_ONLY) == 0, "headers");
    CHECK(es2_sim_guest_range(0x00401000u, 0x00405000u, ES2_SIM_GUEST_NO_ACCESS) == 0, "CODE below the table");
    CHECK(es2_sim_guest_range(0x00406000u, 0x00497000u, ES2_SIM_GUEST_NO_ACCESS) == 0, "CODE above the table");
    CHECK(es2_sim_guest_range(0x00405000u, 0x00406000u, ES2_SIM_GUEST_READ_ONLY) == 0, "a table page");
    CHECK(es2_sim_guest_range(0x006d8000u, 0x006d9000u, ES2_SIM_GUEST_NO_ACCESS) == 0, "barrier");
    CHECK(es2_sim_guest_range(0x00404000u, 0x00405800u, ES2_SIM_GUEST_NO_ACCESS) == 1, "overlap refused");
    CHECK(es2_sim_guest_range(0x00500000u, 0x00500000u, ES2_SIM_GUEST_NO_ACCESS) == 1, "empty refused");
    CHECK(es2_sim_guest_range(0x00500000u, 0x00501000u, ES2_SIM_GUEST_OK) == 1, "kind OK refused");

    CHECK(es2_sim_guest_classify(0x0000000fu, 1u, 0) == ES2_SIM_GUEST_NULL_ZONE, "NULL read");
    CHECK(es2_sim_guest_classify(0x0000fffeu, 4u, 0) == ES2_SIM_GUEST_NULL_ZONE, "a read leaving the NULL zone");
    CHECK(es2_sim_guest_classify(0x00010000u, 4u, 1) == ES2_SIM_GUEST_OK, "above the NULL zone");
    CHECK(es2_sim_guest_classify(0x00400010u, 4u, 0) == ES2_SIM_GUEST_OK, "header read");
    CHECK(es2_sim_guest_classify(0x00400010u, 4u, 1) == ES2_SIM_GUEST_READ_ONLY, "header write");
    CHECK(es2_sim_guest_classify(0x00405366u, 62u, 0) == ES2_SIM_GUEST_OK, "table run read");
    CHECK(es2_sim_guest_classify(0x00405366u, 1u, 1) == ES2_SIM_GUEST_READ_ONLY, "table run write");
    CHECK(es2_sim_guest_classify(0x00404ffcu, 8u, 0) == ES2_SIM_GUEST_NO_ACCESS, "a read into the table page from CODE");
    CHECK(es2_sim_guest_classify(0x00405ffeu, 4u, 0) == ES2_SIM_GUEST_NO_ACCESS, "a read out of the table page");
    CHECK(es2_sim_guest_classify(0x00496ffcu, 4u, 0) == ES2_SIM_GUEST_NO_ACCESS, "CODE's last dword");
    CHECK(es2_sim_guest_classify(0x00497000u, 4u, 1) == ES2_SIM_GUEST_OK, "DATA");
    CHECK(es2_sim_guest_classify(0x006d7ffcu, 8u, 0) == ES2_SIM_GUEST_NO_ACCESS, "into the barrier");
    CHECK(es2_sim_guest_classify(0x006d9000u, 4u, 1) == ES2_SIM_GUEST_OK, "above the barrier");
    CHECK(es2_sim_guest_classify(0xfffffffeu, 4u, 0) == ES2_SIM_GUEST_WRAP, "wrap");
    CHECK(es2_sim_guest_classify(0x0000000fu, 0u, 0) == ES2_SIM_GUEST_OK, "zero length");

    /* The in-place test of the accessors: the page map. */
    CHECK(ES2_SIM_GUEST_UNCHECKED(0x00497000u, 4u) && ES2_SIM_GUEST_UNCHECKED_WRITE(0x00497ffeu, 4u), "DATA in place");
    CHECK(ES2_SIM_GUEST_UNCHECKED(0x00405366u, 4u) && !ES2_SIM_GUEST_UNCHECKED_WRITE(0x00405366u, 4u),
          "a table page: reads in place, writes looked up");
    CHECK(!ES2_SIM_GUEST_UNCHECKED(0x00404ffcu, 8u) && !ES2_SIM_GUEST_UNCHECKED(0x00401000u, 1u), "CODE");
    CHECK(!ES2_SIM_GUEST_UNCHECKED(0x0000000fu, 4u) && !ES2_SIM_GUEST_UNCHECKED(0x0000fffeu, 4u), "NULL zone");
    CHECK(!ES2_SIM_GUEST_UNCHECKED(0x00497000u, 0x2001u), "a span over 4096 bytes is looked up");
    CHECK(!ES2_SIM_GUEST_UNCHECKED(0x006d7ffcu, 8u), "into the barrier");
    CHECK(ES2_SIM_GUEST_UNCHECKED_WRITE(0x006d9000u, 4u) && !ES2_SIM_GUEST_UNCHECKED(0xfffffffeu, 4u), "above the barrier; the wrap meets the last entry");

    /* Through the accessor: a hit goes to the callback; when it returns, a
     * read gives 0, a write is dropped, a span is NULL. */
    es2_sim_guest_on_fault(on_fault, NULL);
    CHECK(es2_sim_guest_read32(0x0000000fu) == 0 && faults == 1 && last.kind == ES2_SIM_GUEST_NULL_ZONE &&
              last.va == 0x0000000fu && last.length == 4u && !last.write, "NULL read through the accessor");
    es2_sim_guest_write8(0x00400010u, 1);
    CHECK(faults == 2 && last.kind == ES2_SIM_GUEST_READ_ONLY && last.write, "header write through the accessor");
    CHECK(es2_sim_guest_span(0x00401000u, 16u) == NULL && faults == 3 && last.kind == ES2_SIM_GUEST_NO_ACCESS,
          "CODE span");
    CHECK(es2_sim_guest_writable(0x00405400u, 4u) == NULL && faults == 4 && last.kind == ES2_SIM_GUEST_READ_ONLY,
          "table page written");
    CHECK(es2_sim_guest_span(0x0000000fu, 0u) == (void *)(uintptr_t)0x0000000fu && faults == 4,
          "a zero-length span is not checked");
    /* The probe: NULL without the callback. */
    CHECK(es2_sim_guest_probe(0x0000000fu, 4u) == NULL && es2_sim_guest_probe(0x00401000u, 4u) == NULL && faults == 4,
          "probe");
    CHECK(es2_sim_guest_probe(0x00400000u, 4u) == (void *)(uintptr_t)0x00400000u, "probe of a readable range");

    /* Host memory outside the table, through the interface: the test's own
     * image may lie at 0x400000, so the table holds only the NULL zone here. */
    es2_sim_guest_reset();
    CHECK(es2_sim_guest_range(0, ES2_SIM_GUEST_NULL_ZONE_END, ES2_SIM_GUEST_NULL_ZONE) == 0, "NULL zone again");
    es2_guest_memory m;
    es2_sim_guest_memory(&m);
    m.write32(m.context, va, 0x11223344u);
    m.write16(m.context, va + 4u, 0x5566u);
    es2_sim_guest_write8(va + 6u, 0x77u);
    CHECK(m.read32(m.context, va) == 0x11223344u && m.read16(m.context, va + 4u) == 0x5566u &&
              es2_sim_guest_read8(va + 6u) == 0x77u && buffer[0] == 0x44u, "host memory");
    CHECK(es2_guest_memory_span(&m, va, 7u) == buffer && m.host(m.context, va) == buffer && faults == 4,
          "host memory spans");
    CHECK(m.read32(m.context, 0x0000fffcu) == 0 && faults == 5, "interface read of the NULL zone");

    /* The canary and the host-heap ceiling. */
    memset(buffer, 0, sizeof buffer);
    CHECK(es2_sim_guest_canary_check(buffer, sizeof buffer) == 0 && faults == 5, "clean zone");
    buffer[0x21] = 1;
    CHECK(es2_sim_guest_canary_check(buffer, sizeof buffer) == 1 && faults == 6 && last.kind == ES2_SIM_GUEST_CANARY &&
              last.va == 0x21u, "dirty zone");
    CHECK(es2_sim_guest_heap_check(ES2_SIM_GUEST_HOST_HEAP_CEILING) == 0 && faults == 6, "heap at the ceiling");
    CHECK(es2_sim_guest_heap_check(ES2_SIM_GUEST_HOST_HEAP_CEILING + 1u) == 1 && faults == 7 &&
              last.kind == ES2_SIM_GUEST_HOST_HEAP, "heap above the ceiling");
#ifdef __EMSCRIPTEN__
    /* The instance's own NULL zone and sbrk top. */
    CHECK(es2_sim_guest_check_frame() == 0 && es2_sim_guest_check_heap() == 0 && faults == 7, "a clean instance");
    volatile uint8_t *zone = (volatile uint8_t *)(uintptr_t)0x21u;
    *zone = 1;
    CHECK(es2_sim_guest_check_frame() == 1 && faults == 8 && last.kind == ES2_SIM_GUEST_CANARY && last.va == 0x21u,
          "a byte written into the instance's NULL zone");
    *zone = 0;
    CHECK(es2_sim_guest_check_frame() == 0 && faults == 8, "the zone clean again");
    /* Last: Emscripten's 32-bit sbrk takes its increment as unsigned and
     * cannot shrink the heap again. */
    const uintptr_t top = (uintptr_t)sbrk(0);
    const intptr_t grow = (intptr_t)(ES2_SIM_GUEST_HOST_HEAP_CEILING + 0x1000u - top);
    CHECK(sbrk(grow) != (void *)-1, "sbrk past the ceiling");
    CHECK(es2_sim_guest_check_heap() == 1 && faults == 9 && last.kind == ES2_SIM_GUEST_HOST_HEAP &&
              last.va == ES2_SIM_GUEST_HOST_HEAP_CEILING + 0x1000u, "the host heap above the ceiling");
    CHECK(es2_sim_guest_check_frame() == 1 && faults == 10 && last.kind == ES2_SIM_GUEST_HOST_HEAP,
          "the frame check includes the heap");
#else
    CHECK(es2_sim_guest_check_frame() == 0 && es2_sim_guest_check_heap() == 0 && faults == 7,
          "no probes outside the web build");
#endif

    es2_sim_guest_reset();
    es2_sim_guest_on_fault(NULL, NULL);
    CHECK(es2_sim_guest_span(0x0000000fu, 4u) == (void *)(uintptr_t)0x0000000fu, "reset: unchecked again");
    if (failures) { printf("%d failure(s)\n", failures); return 1; }
    printf("guest_memory_test: ok\n");
    return 0;
}
