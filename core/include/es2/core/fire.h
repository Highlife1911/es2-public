#ifndef ES2_CORE_FIRE_H
#define ES2_CORE_FIRE_H
#include "es2/core/weapons.h"
/* The FIRE pool helpers of the damage code
 * (docs/reverse-engineering/main_loop.md, section "fire allocation
 * 46b430, fire constructor 46b48c, fire release 46b62c").
 *
 * Every game call goes through g->call with the original's arguments and
 * entry EAX/EDX/ECX (observer key: the callee's original VA): pool_alloc
 * 0x471b30, pool_prev 0x471c70, object_init 0x402188, the memset thunk
 * 0x49681a, sfx_play 0x462830, fire_update 0x46b678 and removal_append
 * 0x478628. Results are EDX:EAX exactly; *ecx receives the original's ECX
 * (fire_alloc after a successful allocation: the entry ECX, where the
 * original returns an uninitialized local).
 *
 * fire_alloc 0x46b430(): a new object of the pool [0x6b4f30], or on a
 * full pool the fire with the smallest signed lifetime word +59h below
 * 10000 (iteration order, first of equal minima), or 0.
 * fire_create 0x46b48c(F, O, w, P, k): the constructor (object_init with
 * the kind's shape, vtable, owner, node word, offset, lifetime, sound 33h
 * for the first burning fire, one fire_update), or in world 4 for a kind
 * other than 1 and 3 the immediate removal append.
 * fire_release 0x46b62c(O): appends every fire whose owner dword +4Ah is
 * O to the removal list [0x6b4f34]. */
uint64_t es2_fire_alloc_registers(const es2_guest_memory *,const es2_weapon_graph *,
    const uint32_t entry[3],uint32_t *ecx);
uint64_t es2_fire_create_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t fire,uint32_t owner,uint32_t node,uint32_t offset,uint32_t kind,const uint32_t entry[3],uint32_t *ecx);
uint64_t es2_fire_release_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t owner,const uint32_t entry[3],uint32_t *ecx);
/* Two object base methods next to the FIRE code
 * (main_loop.md, section "the stage-5 flushes and the manager slot
 * methods", "The object base methods"), slots +04
 * and +10 of 22 class tables; entry[] = EAX/EDX/ECX at entry.
 * object_position 0x46b905(O): EAX = O+26h; EDX, ECX = entry.
 * shape_word 0x46b910(O): S = [O+34h]; 0 → EAX 0, ECX 0, EDX entry;
 * [S+4] = 0 → EAX 0, ECX 0, EDX S; else [O+34h] and its +4 read again:
 * EAX = high word of S' : word [[S'+4]+8], EDX = [S'+4], ECX = [S+4]. */
uint64_t es2_object_position_registers(uint32_t object,const uint32_t entry[3],uint32_t *ecx);
uint64_t es2_shape_word_registers(const es2_guest_memory *,uint32_t object,const uint32_t entry[3],uint32_t *ecx);
#endif
