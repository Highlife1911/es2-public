#ifndef ES2_CORE_FIXED_INTERNAL_H
#define ES2_CORE_FIXED_INTERNAL_H
#include <stdint.h>
static inline uint16_t fx_r16(const void *v) { const uint8_t *p=v; return (uint16_t)((uint16_t)p[0]|((uint16_t)p[1]<<8)); }
static inline uint32_t fx_r32(const void *v) { const uint8_t *p=v; return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static inline void fx_w16(void *v,uint32_t x) { uint8_t *p=v; p[0]=(uint8_t)x; p[1]=(uint8_t)(x>>8); }
static inline void fx_w32(void *v,uint32_t x) { uint8_t *p=v; for(unsigned i=0;i<4;++i) p[i]=(uint8_t)(x>>(8*i)); }
static inline int32_t fx_s16(uint32_t x) { x&=65535; return x<32768 ? (int32_t)x : (int32_t)x-65536; }
static inline int64_t fx_s32(uint32_t x) { return x<UINT32_C(0x80000000) ? (int64_t)x : (int64_t)x-INT64_C(4294967296); }
/* SAR r32 by n: like the instruction, only the count's low five bits are
 * used, so a zero count (or 32) leaves the value. */
static inline uint32_t fx_sar(uint32_t x,unsigned n) { n&=31u; return (x>>n)|((x&UINT32_C(0x80000000)) ? ~(UINT32_MAX>>n) : 0); }
static inline uint32_t fx_q14(uint32_t a,uint32_t b) { return fx_sar(a*b+8192u,14); }
#endif
