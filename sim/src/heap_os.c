/* The real OS calls of the own heap (es2/sim/heap.h), through the host OS
 * layer. */
#include "es2/platform/os.h"
#include "es2/sim/heap.h"

static uint32_t os_alloc(void *c, uint32_t address, uint32_t size, uint32_t type, uint32_t protect) {
    (void)c;
    return (uint32_t)es2_os_virtual_alloc(address, size, type, protect);
}
static uint32_t os_free(void *c, uint32_t address, uint32_t size, uint32_t type) {
    (void)c;
    return (uint32_t)es2_os_virtual_free(address, size, type);
}

void es2_sim_heap_os_system(es2_sim_heap_os *os) {
    os->context = NULL;
    os->virtual_alloc = os_alloc;
    os->virtual_free = os_free;
}

uint32_t es2_sim_heap_platform(void) { return es2_os_heap_platform(); }
