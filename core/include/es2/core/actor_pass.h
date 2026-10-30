#ifndef ES2_CORE_ACTOR_PASS_H
#define ES2_CORE_ACTOR_PASS_H
#include "es2/core/weapons.h"
/* The per-frame actor-list pass of the logic frame and its contact
 * helpers. Exact contracts: docs/reverse-engineering/vehicle.md, section
 * "Actor-list pass". Every game call goes through g->call with the
 * entry registers the original holds at that call, so nested hooks keep
 * their own modes; g->image locates the actor list (4a9af8h, count word
 * 4a9afeh) and the RNG state. All four return EDX:EAX as the original
 * leaves it, except that actor_pass returns EDX's high word as 0 (the
 * original leaves uninitialized stack there; dead at its only caller).
 * ECX is outside the contract. No scratch memory is needed. */

/* actor_pass 4123ac (): contact expiry timers, contact scans, +A2h clear. */
uint64_t es2_actor_pass_registers(const es2_guest_memory *,const es2_weapon_graph *,const uint32_t entry[3]);
/* contact_expire 41251c (actor): drops mutual contacts out of range/sight. */
uint64_t es2_contact_expire_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t actor,
    const uint32_t entry[3]);
/* contact_scan 4128f8 (actor): one side-0 actor against all side-1 actors. */
uint64_t es2_contact_scan_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t actor,
    const uint32_t entry[3]);
/* contact_share 412704 (actor, other): the actor's allies near the other. */
uint64_t es2_contact_share_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t actor,
    uint32_t other,const uint32_t entry[3]);
#endif
