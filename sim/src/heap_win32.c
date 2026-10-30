/* The real OS calls of the own heap (es2/sim/heap.h). */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "es2/sim/heap.h"

static uint32_t os_alloc(void *c, uint32_t address, uint32_t size, uint32_t type, uint32_t protect) {
    (void)c;
    return (uint32_t)(uintptr_t)VirtualAlloc((LPVOID)(uintptr_t)address, size, type, protect);
}
static uint32_t os_free(void *c, uint32_t address, uint32_t size, uint32_t type) {
    (void)c;
    return (uint32_t)VirtualFree((LPVOID)(uintptr_t)address, size, type);
}

void es2_sim_heap_os_win32(es2_sim_heap_os *os) {
    os->context = NULL;
    os->virtual_alloc = os_alloc;
    os->virtual_free = os_free;
}

uint32_t es2_sim_heap_platform(void) {
    HMODULE kernel = GetModuleHandleA("kernel32.dll");
    if (kernel && GetProcAddress(kernel, "Borland32")) return 2u;
    return (GetVersion() & 0x80000000u) ? 1u : 0u;
}
