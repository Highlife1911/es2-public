/* The adapters' guest memory (docs/web_build.md section 5), math_native_memory,
 * and es2_guest_span over it. Inside the original 32-bit process guest
 * addresses are host addresses: the proxy's accessor is the unchecked
 * identity one. es2sim's adapters (ES2_SIM_HOST) take its checked accessor
 * (es2/sim/guest_memory.h). */
#ifndef ES2_GUEST_NATIVE_H
#define ES2_GUEST_NATIVE_H
#include <stdint.h>
#include "es2/core/guest.h"
#ifdef ES2_SIM_HOST
#include "es2/sim/guest_memory.h"
/* In place where the page map allows it, else through the checks. */
static uint32_t math_native_read32(void *c,uint32_t x) { (void)c; if(!ES2_SIM_GUEST_UNCHECKED(x,4u)) return es2_sim_guest_read32(x); const uint8_t *p=(const uint8_t *)(uintptr_t)x; return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static uint16_t math_native_read16(void *c,uint32_t x) { (void)c; if(!ES2_SIM_GUEST_UNCHECKED(x,2u)) return es2_sim_guest_read16(x); const uint8_t *p=(const uint8_t *)(uintptr_t)x; return (uint16_t)(p[0]|(p[1]<<8)); }
static void math_native_write32(void *c,uint32_t x,uint32_t v) { (void)c; if(!ES2_SIM_GUEST_UNCHECKED_WRITE(x,4u)) { es2_sim_guest_write32(x,v); return; } uint8_t *p=(uint8_t *)(uintptr_t)x; for(unsigned i=0;i<4;++i) p[i]=(uint8_t)(v>>(8*i)); }
static void math_native_write16(void *c,uint32_t x,uint16_t v) { (void)c; if(!ES2_SIM_GUEST_UNCHECKED_WRITE(x,2u)) { es2_sim_guest_write16(x,v); return; } uint8_t *p=(uint8_t *)(uintptr_t)x; p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); }
static void *math_native_host(void *c,uint32_t x) { (void)c; return ES2_SIM_GUEST_UNCHECKED(x,1u)?(void *)(uintptr_t)x:es2_sim_guest_span(x,1u); }
static void *math_native_span(void *c,uint32_t x,uint32_t n) { (void)c; return !n||ES2_SIM_GUEST_UNCHECKED(x,n)?(void *)(uintptr_t)x:es2_sim_guest_span(x,n); }
#else
static uint32_t math_native_read32(void *c,uint32_t x) { (void)c; const uint8_t *p=(const uint8_t *)(uintptr_t)x; return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static uint16_t math_native_read16(void *c,uint32_t x) { (void)c; const uint8_t *p=(const uint8_t *)(uintptr_t)x; return (uint16_t)(p[0]|(p[1]<<8)); }
static void math_native_write32(void *c,uint32_t x,uint32_t v) { (void)c; uint8_t *p=(uint8_t *)(uintptr_t)x; for(unsigned i=0;i<4;++i) p[i]=(uint8_t)(v>>(8*i)); }
static void math_native_write16(void *c,uint32_t x,uint16_t v) { (void)c; uint8_t *p=(uint8_t *)(uintptr_t)x; p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); }
static void *math_native_host(void *c,uint32_t x) { (void)c; return (void *)(uintptr_t)x; }
static void *math_native_span(void *c,uint32_t x,uint32_t n) { (void)c; (void)n; return (void *)(uintptr_t)x; }
#endif
static const es2_guest_memory math_native_memory={NULL,math_native_read32,math_native_read16,math_native_write32,math_native_write16,
    math_native_host,math_native_span};
/* Host pointer for the `length` bytes at guest `va` that the adapter touches
 * through it. */
static inline void *es2_guest_span(uint32_t va,uint32_t length) { return es2_guest_memory_span(&math_native_memory,va,length); }
/* es2_guest_span for a readability probe: NULL instead of the checked
 * accessor's fault where its table forbids the read (the proxy: no table). */
static inline void *es2_guest_probe(uint32_t va,uint32_t length) {
#ifdef ES2_SIM_HOST
    return es2_sim_guest_probe(va,length);
#else
    return es2_guest_span(va,length);
#endif
}
/* The NUL-terminated string at `va` with its NUL, or its first `limit`
 * bytes when they hold no NUL. */
static inline const char *es2_guest_string(uint32_t va,uint32_t limit) {
    uint32_t n=0;
    while(n<limit && *(const char *)es2_guest_span(va+n,1u)) ++n;
    return es2_guest_span(va,n<limit?n+1u:limit);
}
#endif
