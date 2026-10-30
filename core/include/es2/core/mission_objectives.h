#ifndef ES2_CORE_MISSION_OBJECTIVES_H
#define ES2_CORE_MISSION_OBJECTIVES_H
#include "es2/core/weapons.h"
/* Objectives, route completion and the periodic outcome policy.
 * Exact contracts: docs/reverse-engineering/mission_runtime.md (section
 * "Objectives, route completion and outcome policy"). Every game call,
 * including the hooked functions of this group, goes through g->call with
 * the entry registers the original sets up, so nested hooks keep their own
 * modes; g->image locates the globals. All return EDX:EAX exactly; ECX is
 * outside the contract (reproduced where it enters a game call). */

/* Leaves: route_point 423b0c (holder, index), order_target 423918 (group,
 * actor), bounds_outside 41373c (position, margin). */
uint64_t es2_route_point_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t holder,uint32_t index,const uint32_t entry[3]);
uint64_t es2_order_target_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t group,uint32_t actor,const uint32_t entry[3]);
uint64_t es2_bounds_outside_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t position,uint32_t margin,const uint32_t entry[3]);
/* bounds_check 41379c (actor): 41373c(actor+26, 0). */
uint64_t es2_bounds_check_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t actor,const uint32_t entry[3]);
/* target_known 411c58 (observer, target), actor_safe 4137b4 (actor). */
uint64_t es2_target_known_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t observer,uint32_t target,const uint32_t entry[3]);
uint64_t es2_actor_safe_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t actor,const uint32_t entry[3]);
/* subject_done 412e58 (group), rivals_done 412e74 (group), order_done
 * 4239fc (group, order). */
uint64_t es2_subject_done_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t group,const uint32_t entry[3]);
uint64_t es2_rivals_done_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t group,const uint32_t entry[3]);
uint64_t es2_order_done_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t group,uint32_t order,const uint32_t entry[3]);
/* status_eval 413180 (player, force), outcome_poll 4131ac (player). */
uint64_t es2_status_eval_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t player,uint32_t force,const uint32_t entry[3]);
uint64_t es2_outcome_poll_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t player,const uint32_t entry[3]);
/* objective_eval 413280 (player). esi is the caller's ESI, which the
 * original reads for objectives whose scope or type assigns no predicate
 * (only esi != 0 matters). */
uint64_t es2_objective_eval_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t player,uint32_t esi,const uint32_t entry[3]);
/* Objective predicates (mission_runtime.md, section "objective predicates";
 * adapter group objective_preds). Leaves without game calls: subject_group
 * 412bd0 and its byte-identical twin subject_actor 412bbc (group), group_down
 * 412be4, group_flagged 412c58, group_marked 412d90 (group), route_flag
 * 41324c (group, route). With game calls through g->call (entry registers
 * as the original passes them): subject_status 412dc4 (412bd0, 412bbc,
 * group_state 412c8c, the actor virtual +40h), state_reached 413920
 * (412c8c), group_safe 413950 (413920, actor_safe 4137b4), bounds_margin
 * 413780 (bounds_outside 41373c). */
uint64_t es2_subject_group_registers(const es2_guest_memory *,uint32_t group);
uint64_t es2_subject_actor_registers(const es2_guest_memory *,uint32_t group);
uint64_t es2_group_down_registers(const es2_guest_memory *,uint32_t group);
uint64_t es2_group_flagged_registers(const es2_guest_memory *,uint32_t group);
uint64_t es2_group_marked_registers(const es2_guest_memory *,uint32_t group);
uint64_t es2_route_flag_registers(const es2_guest_memory *,uint32_t group,uint32_t route);
uint64_t es2_subject_status_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t group,const uint32_t entry[3]);
uint64_t es2_state_reached_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t group,const uint32_t entry[3]);
uint64_t es2_group_safe_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t group,const uint32_t entry[3]);
uint64_t es2_bounds_margin_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t actor,const uint32_t entry[3]);
#endif
