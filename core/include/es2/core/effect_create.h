#ifndef ES2_CORE_EFFECT_CREATE_H
#define ES2_CORE_EFFECT_CREATE_H
#include "es2/core/weapons.h"
/* Effect creation: explosion_create 0x407f1c, its base
 * constructor object_init 0x402188, light_create 0x407604 and the pool
 * allocation pool_alloc 0x471b30. Contracts:
 * docs/reverse-engineering/weapons.md, section "effect creation". Every game
 * callee (the runtime frame setup 0x49630c, the heap 0x47749c, 0x490b64,
 * memset, the light module 0x406f38/0x407048, rng_bounded, the sound wrapper
 * 0x4628e0 and the hooked constructors themselves) is called through g->call
 * with the original's entry registers; the pure leaf 0x407b20 is computed
 * here. g->image is the image base (addresses are image + VA - 0x400000).
 * object_init and explosion_create need 256 scratch bytes: the synthetic EBP
 * is scratch+208, so the runtime record lies at scratch+208-24h and
 * scratch+208-28h respectively. 0x49630c links that record into FS:0; the
 * caller restores FS:0 to its entry head after the return (the record's
 * first dword holds the same value), as the original's epilogue does. */
uint64_t es2_pool_alloc_registers(const es2_guest_memory *,uint32_t pool,const uint32_t entry[3]);
uint64_t es2_object_init_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t object,uint32_t shape,const uint32_t entry[3]);
uint64_t es2_light_create_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t light,uint32_t position,uint32_t level);
/* The linked-object allocation must succeed when the type asks for one
 * (the original faults on a null object, see the contract). */
uint64_t es2_explosion_create_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t object,uint32_t type,uint32_t position,uint32_t value,uint32_t sound,const uint32_t entry[3]);
#endif
