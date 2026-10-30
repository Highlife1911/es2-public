#ifndef ES2_CORE_GUEST_H
#define ES2_CORE_GUEST_H
#include <stdint.h>

/* Calls a guest cdecl function with dword slots and returns EDX:EAX.
 * Buffer arguments are guest addresses; each host defines their mapping. */
typedef uint64_t (*es2_guest_call)(void *context, uint32_t function, const uint32_t *args, unsigned count);

/* Access to game objects that store 32-bit pointers. Addresses are the
 * original 32-bit values and wrap modulo 2^32; the host maps them to its own
 * memory (identity inside the original process, an arena in tests). Reads are
 * little-endian and may be unaligned. */
typedef struct es2_guest_memory {
    void *context;
    uint32_t (*read32)(void *context, uint32_t address);
    uint16_t (*read16)(void *context, uint32_t address);
    void (*write32)(void *context, uint32_t address, uint32_t value);
    void (*write16)(void *context, uint32_t address, uint16_t value);
    /* Host pointer for a guest object starting at `address`, for core
     * functions that operate on host objects. */
    void *(*host)(void *context, uint32_t address);
} es2_guest_memory;

#endif
