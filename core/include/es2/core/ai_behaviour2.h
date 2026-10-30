#ifndef ES2_CORE_AI_BEHAVIOUR2_H
#define ES2_CORE_AI_BEHAVIOUR2_H
#include "es2/core/ai_behaviour.h"
/* AI behaviours (docs/reverse-engineering/ai.md, section "Aim selection,
 * attack planning, group scans and retreat"): aim_select 0x41ea7c, attack_plan 0x41cf18, attack_replan 0x41d5ec,
 * group_scan 0x41d60c, order_scan 0x41d7d0 and retreat_fire 0x41d2c4.
 *
 * Every game callee (the hooks named in ai.md, the original helper 40f09c
 * and the actor virtuals +30h, +4Ch, +54h and +58h) is called through
 * g->call with the original's EAX/EDX/ECX, so nested hooks keep their own
 * modes. g->image is the image base (pushed script tables, the RNG state).
 * aim_select and retreat_fire pass frame addresses: g->scratch is their frame
 * (ES2_*_SCRATCH guest bytes from the lowest local, the original's layout).
 * Unless frame is NULL they also store, before every game call, the
 * EBX/ESI/EDI the original holds there and the address after its CALL in
 * *frame (an adapter that runs the call at the original's stack coordinate
 * loads them; ai.md, "stack residue under aim_fire"); edi is the caller's EDI,
 * which the original keeps in EDI up to those calls. Each returns the
 * original's EDX:EAX; ECX after return is outside the contract (every caller
 * pops or overwrites it, ai.md).
 * Preconditions (the original faults otherwise): attack_plan's actor has a
 * target where it reads its type word; retreat_fire's remembered target [A+61h]
 * (or the current one) is an actor. */
#define ES2_AIM_SELECT_SCRATCH 0x1cu
#define ES2_RETREAT_FIRE_SCRATCH 0x18u
/* aim_select 41ea7c (A, w, T): the aim point on T (or A's target) by T's
 * type word: aim_target (0), the part of virtuals +54h/+58h (1) or the
 * position plus the offset of virtual +30h (other), then aim_fire. Frame at
 * scratch: Q +0, the two 6-byte records of +30h at +0Ch and +14h. */
uint64_t es2_aim_select_registers(const es2_guest_memory *,const es2_weapon_graph *,es2_ai_call_frame *frame,
    uint32_t edi,uint32_t actor,uint32_t word,uint32_t target,const uint32_t entry[3]);
/* attack_plan 41cf18 (A): the script member +30h of the attack scripts:
 * fire permission, target choice by order state, the idle group members,
 * retreat check and the next attack script. EAX = 0. */
uint64_t es2_attack_plan_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t actor,
    const uint32_t entry[3]);
/* attack_replan 41d5ec (A): attack_plan(A) with EAX = A. */
uint64_t es2_attack_replan_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t actor,
    const uint32_t entry[3]);
/* group_scan 41d60c (A): script step; group_move, drop the target, and on
 * the expired scan timer the retreat or an attack on a target the group's
 * order allows. EAX = 0. */
uint64_t es2_group_scan_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t actor,
    const uint32_t entry[3]);
/* order_scan 41d7d0 (A): the same with the order state word read before
 * group_move; attacks only in order state 2 or under a type-3 order. */
uint64_t es2_order_scan_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t actor,
    const uint32_t entry[3]);
/* retreat_fire 41d2c4 (A): step of the retreat script 499800h; remember and
 * drop the target, back away from it or turn aside (target_bearing, timer,
 * side) and fire at it with aim_select. AX = 1 when the remembered target
 * is out. Frame (the steering record) at scratch. */
uint64_t es2_retreat_fire_registers(const es2_guest_memory *,const es2_weapon_graph *,es2_ai_call_frame *frame,
    uint32_t edi,uint32_t actor,const uint32_t entry[3]);
#endif
