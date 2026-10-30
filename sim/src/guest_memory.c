/* The checked guest memory accessor; see es2/sim/guest_memory.h. */
#ifdef __EMSCRIPTEN__
#define _DEFAULT_SOURCE /* sbrk */
#endif
#include "es2/sim/guest_memory.h"
#include <stddef.h>
#include <string.h>
#ifdef __EMSCRIPTEN__
#include <unistd.h>
#endif

static struct { uint32_t lo, hi; es2_sim_guest_class kind; } ranges[ES2_SIM_GUEST_RANGES];
static unsigned range_count;
/* Every access at or above `checked_end` lies outside the table. */
static uint32_t checked_end;
uint8_t es2_sim_guest_page_bits[0x100001];
#define WRAP_PAGE 0x100000u

static void mark_pages(uint32_t lo, uint32_t hi, es2_sim_guest_class kind) {
    const uint8_t bits = kind == ES2_SIM_GUEST_READ_ONLY ? ES2_SIM_GUEST_PAGE_NO_WRITE
                                                         : ES2_SIM_GUEST_PAGE_NO_READ | ES2_SIM_GUEST_PAGE_NO_WRITE;
    for (uint32_t page = lo >> 12; page <= (hi - 1u) >> 12; ++page) es2_sim_guest_page_bits[page] |= bits;
}
static es2_sim_guest_fault_fn fault_fn;
static void *fault_user;

int es2_sim_guest_range(uint32_t lo, uint32_t hi, es2_sim_guest_class kind) {
    if (lo >= hi || kind < ES2_SIM_GUEST_NULL_ZONE || kind > ES2_SIM_GUEST_READ_ONLY) return 1;
    unsigned at = 0;
    while (at < range_count && ranges[at].hi <= lo) ++at;
    if (at < range_count && ranges[at].lo < hi) return 1;
    if (at > 0 && ranges[at - 1].hi == lo && ranges[at - 1].kind == kind) {
        ranges[at - 1].hi = hi;
        if (at < range_count && ranges[at].lo == hi && ranges[at].kind == kind) {
            ranges[at - 1].hi = ranges[at].hi;
            memmove(&ranges[at], &ranges[at + 1], (range_count - at - 1u) * sizeof(ranges[0]));
            --range_count;
        }
    } else if (at < range_count && ranges[at].lo == hi && ranges[at].kind == kind) {
        ranges[at].lo = lo;
    } else {
        if (range_count >= ES2_SIM_GUEST_RANGES) return 1;
        memmove(&ranges[at + 1], &ranges[at], (range_count - at) * sizeof(ranges[0]));
        ranges[at].lo = lo; ranges[at].hi = hi; ranges[at].kind = kind;
        ++range_count;
    }
    mark_pages(lo, hi, kind);
    es2_sim_guest_page_bits[WRAP_PAGE] = ES2_SIM_GUEST_PAGE_NO_READ | ES2_SIM_GUEST_PAGE_NO_WRITE;
    checked_end = ranges[range_count - 1u].hi;
    return 0;
}

void es2_sim_guest_reset(void) {
    range_count = 0;
    checked_end = 0;
    memset(es2_sim_guest_page_bits, 0, sizeof es2_sim_guest_page_bits);
}

void es2_sim_guest_on_fault(es2_sim_guest_fault_fn fn, void *user) { fault_fn = fn; fault_user = user; }

const char *es2_sim_guest_class_name(es2_sim_guest_class kind) {
    switch (kind) {
    case ES2_SIM_GUEST_OK: return "ok";
    case ES2_SIM_GUEST_NULL_ZONE: return "the NULL zone";
    case ES2_SIM_GUEST_UNUSED: return "the unused range";
    case ES2_SIM_GUEST_NO_ACCESS: return "a no-access range";
    case ES2_SIM_GUEST_READ_ONLY: return "a read-only range";
    case ES2_SIM_GUEST_WRAP: return "the end of the address space";
    case ES2_SIM_GUEST_CANARY: return "the NULL-zone canary";
    case ES2_SIM_GUEST_HOST_HEAP: return "the host-heap ceiling";
    }
    return "?";
}

es2_sim_guest_class es2_sim_guest_classify(uint32_t va, uint32_t length, int write) {
    if (!length) return ES2_SIM_GUEST_OK;
    if (length - 1u > 0xffffffffu - va) return ES2_SIM_GUEST_WRAP;
    if (va >= checked_end) return ES2_SIM_GUEST_OK;
    uint32_t last = va + (length - 1u);
    unsigned lo = 0, hi = range_count;
    while (lo < hi) {
        unsigned mid = (lo + hi) / 2u;
        if (ranges[mid].hi <= va) lo = mid + 1u;
        else hi = mid;
    }
    for (unsigned i = lo; i < range_count && ranges[i].lo <= last; ++i)
        if (ranges[i].kind != ES2_SIM_GUEST_READ_ONLY || write) return ranges[i].kind;
    return ES2_SIM_GUEST_OK;
}

static int report(es2_sim_guest_class kind, uint32_t va, uint32_t length, int write) {
    es2_sim_guest_fault f = {kind, va, length, write};
    if (fault_fn) fault_fn(&f, fault_user);
    return 1;
}

/* An access ES2_SIM_GUEST_IN_PLACE does not pass: NULL after a reported hit. */
static void *checked(uint32_t va, uint32_t length, int write) {
    if (range_count) {
        es2_sim_guest_class kind = es2_sim_guest_classify(va, length, write);
        if (kind != ES2_SIM_GUEST_OK && report(kind, va, length, write)) return NULL;
    }
    return (void *)(uintptr_t)va;
}
#define ACCESS(va, length, write)                                                                  \
    (ES2_SIM_GUEST_IN_PLACE(va, length, (write) ? ES2_SIM_GUEST_PAGE_NO_WRITE : ES2_SIM_GUEST_PAGE_NO_READ) \
         ? (void *)(uintptr_t)(va)                                                                  \
         : checked(va, length, write))

void *es2_sim_guest_span(uint32_t va, uint32_t length) {
    return length ? ACCESS(va, length, 0) : (void *)(uintptr_t)va;
}
void *es2_sim_guest_writable(uint32_t va, uint32_t length) {
    return length ? ACCESS(va, length, 1) : (void *)(uintptr_t)va;
}
void *es2_sim_guest_probe(uint32_t va, uint32_t length) {
    return es2_sim_guest_classify(va, length, 0) == ES2_SIM_GUEST_OK ? (void *)(uintptr_t)va : NULL;
}

uint32_t es2_sim_guest_read32(uint32_t va) {
    const void *p = ACCESS(va, 4u, 0);
    uint32_t v = 0;
    if (p) memcpy(&v, p, 4);
    return v;
}
uint16_t es2_sim_guest_read16(uint32_t va) {
    const void *p = ACCESS(va, 2u, 0);
    uint16_t v = 0;
    if (p) memcpy(&v, p, 2);
    return v;
}
uint8_t es2_sim_guest_read8(uint32_t va) {
    const uint8_t *p = ACCESS(va, 1u, 0);
    return p ? *p : 0u;
}
void es2_sim_guest_write32(uint32_t va, uint32_t value) {
    void *p = ACCESS(va, 4u, 1);
    if (p) memcpy(p, &value, 4);
}
void es2_sim_guest_write16(uint32_t va, uint16_t value) {
    void *p = ACCESS(va, 2u, 1);
    if (p) memcpy(p, &value, 2);
}
void es2_sim_guest_write8(uint32_t va, uint8_t value) {
    uint8_t *p = ACCESS(va, 1u, 1);
    if (p) *p = value;
}

static uint32_t view_read32(void *c, uint32_t a) {
    (void)c;
    uint32_t v;
    if (!ES2_SIM_GUEST_UNCHECKED(a, 4u)) return es2_sim_guest_read32(a);
    memcpy(&v, (const void *)(uintptr_t)a, 4);
    return v;
}
static uint16_t view_read16(void *c, uint32_t a) {
    (void)c;
    uint16_t v;
    if (!ES2_SIM_GUEST_UNCHECKED(a, 2u)) return es2_sim_guest_read16(a);
    memcpy(&v, (const void *)(uintptr_t)a, 2);
    return v;
}
static void view_write32(void *c, uint32_t a, uint32_t v) {
    (void)c;
    if (ES2_SIM_GUEST_UNCHECKED_WRITE(a, 4u)) memcpy((void *)(uintptr_t)a, &v, 4);
    else es2_sim_guest_write32(a, v);
}
static void view_write16(void *c, uint32_t a, uint16_t v) {
    (void)c;
    if (ES2_SIM_GUEST_UNCHECKED_WRITE(a, 2u)) memcpy((void *)(uintptr_t)a, &v, 2);
    else es2_sim_guest_write16(a, v);
}
static void *view_host(void *c, uint32_t a) { (void)c; return ACCESS(a, 1u, 0); }
static void *view_span(void *c, uint32_t a, uint32_t n) { (void)c; return n ? ACCESS(a, n, 0) : (void *)(uintptr_t)a; }

const es2_guest_memory es2_sim_guest_memory_view = {NULL, view_read32, view_read16, view_write32, view_write16,
                                                    view_host, view_span};
void es2_sim_guest_memory(es2_guest_memory *memory) { *memory = es2_sim_guest_memory_view; }

int es2_sim_guest_canary_check(const uint8_t *zone, uint32_t size) {
    for (uint32_t i = 0; i < size; ++i)
        if (zone[i]) return report(ES2_SIM_GUEST_CANARY, i, 1u, 1);
    return 0;
}
int es2_sim_guest_heap_check(uint32_t top) {
    return top > ES2_SIM_GUEST_HOST_HEAP_CEILING ? report(ES2_SIM_GUEST_HOST_HEAP, top, 0u, 0) : 0;
}

int es2_sim_guest_check_frame(void) {
#ifdef __EMSCRIPTEN__
    /* Linear memory from address 0: the zone is plain memory in wasm. */
    const volatile uint8_t *zone = (const volatile uint8_t *)(uintptr_t)0u;
    for (uint32_t i = 0; i < ES2_SIM_GUEST_NULL_ZONE_END; ++i)
        if (zone[i]) return report(ES2_SIM_GUEST_CANARY, i, 1u, 1);
    return es2_sim_guest_check_heap();
#else
    return 0;
#endif
}
int es2_sim_guest_check_heap(void) {
#ifdef __EMSCRIPTEN__
    return es2_sim_guest_heap_check((uint32_t)(uintptr_t)sbrk(0));
#else
    return 0;
#endif
}
