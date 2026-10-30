#ifndef ES2_CORE_WEAPON_HELPERS_H
#define ES2_CORE_WEAPON_HELPERS_H
#include "es2/core/weapons.h"
/* Weapon group and collision-sphere helpers. Contracts:
 * docs/reverse-engineering/weapons.md, "Weapon group and sphere helpers".
 * Game callees (virtual slots, 40ede8, 4801d4, 40c428, 47de4c) go through
 * g->call with the original entry registers. g->scratch is the original's
 * local frame: at least 20 accessible guest bytes, disjoint from game state;
 * its address shows up in residue registers as in the original.
 * entry[] holds EAX/EDX/ECX at entry; results are EDX:EAX. */
uint64_t es2_group_fire_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t group,uint32_t value,uint32_t index,uint32_t owner,const uint32_t entry[3]);
uint64_t es2_weapon_ready_registers(const es2_guest_memory *,uint32_t weapon,const uint32_t entry[3]);
/* The second sum must be nonzero and the quotient must fit, as for IDIV. */
uint64_t es2_part_condition_registers(const es2_guest_memory *,uint32_t parts);
uint64_t es2_sphere_hits_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t region,uint32_t offset,const uint32_t entry[3]);
uint64_t es2_sphere_test_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t centre,uint32_t radius,uint32_t mode,const uint32_t entry[3]);
#endif
