/* The checked guest memory accessor (docs/web_build.md section 5): the one
 * place in es2sim where a guest VA becomes a host pointer. Identity mapping
 * stays (guest VA = host address); a range table names the address ranges a
 * guest access must not touch. Every access is checked against it:
 *
 * - NULL zone: 0x0000..0xffff;
 * - unused: a range the host never maps (the web build's 0x10000..0x3fffff;
 *   on Windows the host's own memory may lie there, so es2sim does not add it);
 * - no access: CODE outside the table pages, the barrier page;
 * - read-only: a write to the headers, the table pages, .edata, .reloc or .rsrc;
 * - wrap: a range past 0xffffffff.
 *
 * Every address outside the table is read-write. Without a table (in vitro,
 * the unit tests) the accessor is the unchecked identity view. A hit goes to
 * the fault callback, which ends the run (es2sim: main.c); when it returns,
 * the read gives 0, the write is dropped and the span is NULL.
 *
 * The NULL-zone canary and the host-heap ceiling are web build checks
 * (es2_sim_guest_check_frame); their breaches go to the same callback. */
#ifndef ES2_SIM_GUEST_MEMORY_H
#define ES2_SIM_GUEST_MEMORY_H
#include <stdint.h>
#include "es2/core/guest.h"

#define ES2_SIM_GUEST_NULL_ZONE_END 0x00010000u
/* The guest stack's limit (es2/sim/machine_sim.h): the host heap stays below. */
#define ES2_SIM_GUEST_HOST_HEAP_CEILING 0x1ff00000u
#define ES2_SIM_GUEST_RANGES 64u

typedef enum es2_sim_guest_class {
    ES2_SIM_GUEST_OK = 0,
    ES2_SIM_GUEST_NULL_ZONE,
    ES2_SIM_GUEST_UNUSED,
    ES2_SIM_GUEST_NO_ACCESS,
    ES2_SIM_GUEST_READ_ONLY,
    ES2_SIM_GUEST_WRAP,
    /* A nonzero byte in the NULL zone (the canary). */
    ES2_SIM_GUEST_CANARY,
    /* The host heap's top above ES2_SIM_GUEST_HOST_HEAP_CEILING. */
    ES2_SIM_GUEST_HOST_HEAP
} es2_sim_guest_class;

typedef struct es2_sim_guest_fault {
    es2_sim_guest_class kind;
    uint32_t va, length;
    int write;
} es2_sim_guest_fault;
typedef void (*es2_sim_guest_fault_fn)(const es2_sim_guest_fault *fault, void *user);

/* Adds [lo, hi) with `kind` (NULL zone ... read-only) to the table and turns
 * the checks on. 0, or 1 (empty or overlapping range, bad kind, table full). */
int es2_sim_guest_range(uint32_t lo, uint32_t hi, es2_sim_guest_class kind);
/* Empties the table: the unchecked identity view again. */
void es2_sim_guest_reset(void);
void es2_sim_guest_on_fault(es2_sim_guest_fault_fn fn, void *user);
const char *es2_sim_guest_class_name(es2_sim_guest_class kind);

/* The page map in front of the table: per 4 KiB page the accesses some range
 * of it forbids (ES2_SIM_GUEST_PAGE_NO_READ, _NO_WRITE; 0: none), and after
 * the last page an entry that forbids both while the table is not empty, so
 * that an access wrapping past 0xffffffff meets it. An access of 1 to 4096
 * bytes whose first and last page forbid nothing of it is OK without a
 * lookup: the accessors test this first and do the access in place, as the
 * identity view did. Longer spans and every other access take the lookup. */
#define ES2_SIM_GUEST_PAGE_NO_READ 1u
#define ES2_SIM_GUEST_PAGE_NO_WRITE 2u
extern uint8_t es2_sim_guest_page_bits[0x100001];
#define ES2_SIM_GUEST_IN_PLACE(va, length, bit)                                                          \
    ((uint32_t)(length) - 1u < 0x1000u &&                                                                \
     !((es2_sim_guest_page_bits[(uint32_t)(va) >> 12] |                                                  \
        es2_sim_guest_page_bits[((uint32_t)(va) >> 12) +                                                 \
                                ((((uint32_t)(va) & 0xfffu) + ((uint32_t)(length) - 1u)) >> 12)]) &      \
       (bit)))
#define ES2_SIM_GUEST_UNCHECKED(va, length) ES2_SIM_GUEST_IN_PLACE(va, length, ES2_SIM_GUEST_PAGE_NO_READ)
#define ES2_SIM_GUEST_UNCHECKED_WRITE(va, length) ES2_SIM_GUEST_IN_PLACE(va, length, ES2_SIM_GUEST_PAGE_NO_WRITE)

/* The class of an access of `length` bytes at `va`; the first range hit in
 * address order. A zero length is no access (ES2_SIM_GUEST_OK). */
es2_sim_guest_class es2_sim_guest_classify(uint32_t va, uint32_t length, int write);

/* Checked as a read; a zero length is not checked and gives the identity
 * pointer (a span of nothing reads nothing; the arena view of es2/sim/memory.h
 * checks it as one byte). */
void *es2_sim_guest_span(uint32_t va, uint32_t length);
/* Checked as a write. */
void *es2_sim_guest_writable(uint32_t va, uint32_t length);
/* The span when the class is OK, else NULL, without the fault callback (the
 * readability probes of the adapters). */
void *es2_sim_guest_probe(uint32_t va, uint32_t length);

uint32_t es2_sim_guest_read32(uint32_t va);
uint16_t es2_sim_guest_read16(uint32_t va);
uint8_t es2_sim_guest_read8(uint32_t va);
void es2_sim_guest_write32(uint32_t va, uint32_t value);
void es2_sim_guest_write16(uint32_t va, uint16_t value);
void es2_sim_guest_write8(uint32_t va, uint8_t value);

/* The es2_guest_memory interface over the accessor; host(va) is span(va, 1). */
void es2_sim_guest_memory(es2_guest_memory *memory);
extern const es2_guest_memory es2_sim_guest_memory_view;

/* The canary over the zone's bytes: 1 and the callback with the first
 * nonzero byte, else 0. */
int es2_sim_guest_canary_check(const uint8_t *zone, uint32_t size);
/* The ceiling over the host heap's top: 1 and the callback above it, else 0. */
int es2_sim_guest_heap_check(uint32_t top);
/* In the web build both checks over the real NULL zone and sbrk(0); at each
 * frame, at exit and at each guest reservation. 0 elsewhere (Windows leaves
 * the NULL zone unmapped and the host heap is not below the guest's). */
int es2_sim_guest_check_frame(void);
int es2_sim_guest_check_heap(void);

#endif
