/* Guest memory views for the own process (own_process.md section 2.4). New
 * code in sim/ reads and writes guest state only through es2_guest_memory:
 * in es2sim.exe the identity view (guest address = host address, the
 * original's layout at its original VAs), in unit tests an arena of host
 * buffers at synthetic guest addresses. In es2sim the adapters' accessor
 * (guest_native.h) is the identity view as well. */
#ifndef ES2_SIM_MEMORY_H
#define ES2_SIM_MEMORY_H
#include <stddef.h>
#include <stdint.h>
#include "es2/core/guest.h"
#include "es2/core/rtl.h"

/* Identity view: valid only in a 32-bit process where the guest ranges are
 * mapped at their own addresses. It is the checked accessor
 * (es2/sim/guest_memory.h), unchecked while its range table is empty. */
void es2_sim_identity_memory(es2_guest_memory *memory);

/* Arena view over up to four host buffers, each at a guest base. A read
 * outside every range returns 0 and counts a fault; a write outside is
 * dropped and counted. host(va) and span(va, 0) are checked as one byte:
 * NULL outside every range, without a count. */
#define ES2_SIM_ARENA_RANGES 4u
typedef struct es2_sim_arena {
    unsigned count;
    uint32_t base[ES2_SIM_ARENA_RANGES], size[ES2_SIM_ARENA_RANGES];
    uint8_t *host[ES2_SIM_ARENA_RANGES];
    unsigned faults;
} es2_sim_arena;
int es2_sim_arena_add(es2_sim_arena *arena, uint32_t base, uint8_t *host, uint32_t size);
void es2_sim_arena_memory(es2_sim_arena *arena, es2_guest_memory *memory);

/* Byte access for the runtime models (core/src/rtl.c) over a guest view. */
void es2_sim_rtl_memory(const es2_guest_memory *guest, es2_rtl_memory *memory);
uint8_t es2_sim_read8(const es2_guest_memory *memory, uint32_t address);
void es2_sim_write8(const es2_guest_memory *memory, uint32_t address, uint8_t value);

#endif
