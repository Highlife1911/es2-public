#ifndef ES2_CORE_BUILDING_H
#define ES2_CORE_BUILDING_H
#include "es2/core/weapons.h"
/* Building logic below the turret, target selection and the
 * destruction actions. Exact contracts: docs/reverse-engineering/weapons.md
 * (section "Building logic: target selection and destruction actions").
 * Every game call goes through g->call with the entry registers the
 * original sets up, so nested hooks keep their own modes; g->image locates
 * the globals and the factor tables. All return EDX:EAX exactly; ECX is
 * outside the contract (reproduced where it enters a game call). */

/* target_valid 411e80 (actor, candidate, flags): may the actor attack the
 * candidate? AX = 1 or EAX = 0. */
uint64_t es2_target_valid_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t actor,uint32_t candidate,uint32_t flags,const uint32_t entry[3]);
/* target_select 411fa0 (actor, flags, arc): the best candidate of the
 * actor list, or 0. */
uint64_t es2_target_select_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t actor,uint32_t flags,uint32_t arc,const uint32_t entry[3]);
/* group_death 423f30 (group, actor): the group's variable actions when no
 * other member is alive. Leaf. */
uint64_t es2_group_death_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t group,uint32_t actor,const uint32_t entry[3]);
/* death_actions 411bc8 (actor): group_death for its group, then the
 * actor's own variable actions. */
uint64_t es2_death_actions_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t actor,const uint32_t entry[3]);
/* Building turret target 403eec and part facing 406868 (weapons.md,
 * same heading). g->scratch is the original's frame: 128 bytes for
 * turret_target (frame base - 80h), 24 bytes for part_facing (base - 18h). */
/* turret_target 403eec (B, P): the turret's aim angles toward the world
 * point P; EAX = 4a95ec, EDX = turret_aim's EDX. */
uint64_t es2_turret_target_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t building,uint32_t point,const uint32_t entry[3]);
/* part_facing 406868 (B, P), building virtual +54h: AX = the living part
 * facing the pose P best, or the first living part when P = 0. */
uint64_t es2_part_facing_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t building,uint32_t pose,const uint32_t entry[3]);
#endif
