#ifndef ES2_CORE_GROUPS_H
#define ES2_CORE_GROUPS_H
#include "es2/core/weapons.h"
/* Group activation, the current-order update and the order leaves.
 * Exact contracts: docs/reverse-engineering/mission_runtime.md, section
 * "Group activation and current-order update". Every game call,
 * including the hooked functions (rng_next, pool_alloc, dist_gate,
 * face_test, order_done and this group's own), goes through g->call with
 * the registers the original sets up, so nested hooks keep their own
 * modes; g->image locates the globals. All return EDX:EAX exactly; ECX is
 * outside the contract (every caller pops it). */

/* order_actor 4238a0 (group): the current order's subject actor or 0.
 * order_point 4238d4 (group): the current order's destination point.
 * Leaves. */
uint64_t es2_order_actor_registers(const es2_guest_memory *,uint32_t group);
uint64_t es2_order_point_registers(const es2_guest_memory *,uint32_t group);
/* actor_destination 41dbcc (actor): the actor's destination point by its
 * state word, else order_point of its group (a game call). */
uint64_t es2_actor_destination_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t actor,const uint32_t entry[3]);
/* group_assess 423f08 (group): 41b804 for every member. */
uint64_t es2_group_assess_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t group,const uint32_t entry[3]);
/* member_assess 41b804 (member) (mission_runtime.md, "member assessment
 * 41b804"): actor_safe, the ratings of the
 * live objects of the MECH, FLYER and building pools around the member by
 * side, random_damage while the hostiles outweigh the friends. Game calls
 * (4137b4, 471c70, 49288c, 417a04) through g->call. */
uint64_t es2_member_assess_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t member,const uint32_t entry[3]);
/* order_update 423a74 (group): completion of the current order, advance to
 * the next order, 411cec for every member. */
uint64_t es2_order_update_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t group,const uint32_t entry[3]);
/* spawn_point 42354c (out, distance, angle, check): the first acceptable
 * placement around the player. g->scratch needs 48 accessible guest bytes
 * (the frame: v, m and w at their original EBP offsets). */
uint64_t es2_spawn_point_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t out,uint32_t distance,uint32_t angle,uint32_t check,const uint32_t entry[3]);
/* group_activate 4236c4 (group): the pending activation. g->scratch needs
 * 208 accessible guest bytes below the synthetic frame at g->scratch+208,
 * which is also the EBP that g->call must give the runtime frame setup
 * 49630c (record at frame-24h). EAX = the record's saved FS:0 head, which
 * the caller restores. esi_high is the caller's ESI high word (the angle
 * of modes 4/5 is built in SI over it); it is unobservable except in EDX
 * after mode 4/5 with fewer than two members. */
uint64_t es2_group_activate_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t group,uint32_t esi_high,const uint32_t entry[3]);
#endif
