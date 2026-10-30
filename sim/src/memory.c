/* Guest memory views; see es2/sim/memory.h. */
#include "es2/sim/memory.h"
#include <string.h>
#include "es2/sim/guest_memory.h"

void es2_sim_identity_memory(es2_guest_memory *m) { es2_sim_guest_memory(m); }

int es2_sim_arena_add(es2_sim_arena *arena, uint32_t base, uint8_t *host, uint32_t size) {
    if (arena->count >= ES2_SIM_ARENA_RANGES || !size || (uint64_t)base + size > 0x100000000ull) return 0;
    for (unsigned i = 0; i < arena->count; ++i)
        if (base < arena->base[i] + arena->size[i] && arena->base[i] < base + size) return 0;
    arena->base[arena->count] = base; arena->host[arena->count] = host; arena->size[arena->count] = size;
    ++arena->count;
    return 1;
}

/* Host address of `count` bytes at guest `a` inside one range, else NULL. */
static uint8_t *arena_at(es2_sim_arena *arena, uint32_t a, uint32_t count) {
    for (unsigned i = 0; i < arena->count; ++i)
        if (a - arena->base[i] < arena->size[i] && count <= arena->size[i] - (a - arena->base[i]))
            return arena->host[i] + (a - arena->base[i]);
    return NULL;
}
static uint32_t ar_read32(void *c, uint32_t a) {
    uint8_t *p = arena_at(c, a, 4);
    if (!p) { ++((es2_sim_arena *)c)->faults; return 0; }
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static uint16_t ar_read16(void *c, uint32_t a) {
    uint8_t *p = arena_at(c, a, 2);
    if (!p) { ++((es2_sim_arena *)c)->faults; return 0; }
    return (uint16_t)((unsigned)p[0] | (unsigned)p[1] << 8);
}
static void ar_write32(void *c, uint32_t a, uint32_t v) {
    uint8_t *p = arena_at(c, a, 4);
    if (!p) { ++((es2_sim_arena *)c)->faults; return; }
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static void ar_write16(void *c, uint32_t a, uint16_t v) {
    uint8_t *p = arena_at(c, a, 2);
    if (!p) { ++((es2_sim_arena *)c)->faults; return; }
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
}
static void *ar_host(void *c, uint32_t a) { return arena_at(c, a, 1); }
/* A zero-length span is checked as one byte (the checked accessor does not
 * check it, es2/sim/guest_memory.h). */
static void *ar_span(void *c, uint32_t a, uint32_t n) { return arena_at(c, a, n ? n : 1u); }

void es2_sim_arena_memory(es2_sim_arena *arena, es2_guest_memory *m) {
    m->context = arena;
    m->read32 = ar_read32; m->read16 = ar_read16;
    m->write32 = ar_write32; m->write16 = ar_write16;
    m->host = ar_host;
    m->span = ar_span;
}

uint8_t es2_sim_read8(const es2_guest_memory *memory, uint32_t address) {
    const uint8_t *p = memory->host(memory->context, address);
    return p ? *p : 0;
}
void es2_sim_write8(const es2_guest_memory *memory, uint32_t address, uint8_t value) {
    uint8_t *p = memory->host(memory->context, address);
    if (p) *p = value;
}
static uint8_t rtl_read8(void *c, uint32_t a) { return es2_sim_read8(c, a); }
static void rtl_write8(void *c, uint32_t a, uint8_t v) { es2_sim_write8(c, a, v); }
void es2_sim_rtl_memory(const es2_guest_memory *guest, es2_rtl_memory *memory) {
    memory->context = (void *)guest;
    memory->read8 = rtl_read8;
    memory->write8 = rtl_write8;
}
