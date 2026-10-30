/* Guest VAs turned into host pointers; the reverse direction is not a hit. */
#include <stdint.h>
struct rec { uint32_t a; };
uint8_t *span(uint32_t va) { return (uint8_t *)(uintptr_t)va; }             /* EXPECT: guest-host-cast */
struct rec *record(uint32_t va) { return (struct rec *)(uintptr_t)va; }     /* EXPECT: guest-host-cast */
const char *text(uint32_t va) { return (const char *) (uintptr_t) va; }     /* EXPECT: guest-host-cast */
uint32_t address(const void *p) { return (uint32_t)(uintptr_t)p; }
uintptr_t widen(uint32_t va) { return (uintptr_t)va; }
