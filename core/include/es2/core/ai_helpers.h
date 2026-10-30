#ifndef ES2_CORE_AI_HELPERS_H
#define ES2_CORE_AI_HELPERS_H
#include "es2/core/weapons.h"
/* The route and behaviour helpers below the AI behaviours
 * (docs/reverse-engineering/mission_runtime.md, section "Route and behaviour
 * helpers"):
 * point_offset 0x4929fc, threat_sum 0x41cb44, engage_done 0x41c4a8,
 * flank_step 0x41c72c, retreat_check 0x41cb94, part_pick 0x41ce08 and
 * drive_apply 0x41e828.
 *
 * Every game callee (target_class, order_target, timer_word_step,
 * heading_point, rng_next, part_ratio, vehicle_control, script_start,
 * order_resume, the list step 0x471c70, the actor virtuals +3Ch/+40h and
 * this group's point_offset/threat_sum) is called through g->call with
 * the original's entry EAX/EDX/ECX, so nested hooks keep their own modes.
 * g->image is the image base (globals, image tables, pushed script table);
 * g->scratch holds ES2_AI_HELPERS_SCRATCH guest bytes for flank_step's
 * frame point (passed by pointer) and retreat_check's frame address (only
 * its value reaches a call, as ECX). Each returns the original's EDX:EAX;
 * ECX after return is outside the contract.
 * Preconditions (the original faults): the actor has a target where the
 * function reads it; retreat_check's part maxima are nonzero. */
#define ES2_AI_HELPERS_SCRATCH 0x48u
/* point_offset 4929fc (P, angle, distance): [P] and [P+4] move by the
 * distance word along angle + 4000h through the quarter sine table
 * (host pointer to 0x4a25c4). EAX = P, EDX = the sign of the second
 * product. Pure apart from the two writes. */
uint64_t es2_point_offset_registers(const es2_guest_memory *,uint32_t point,uint32_t angle,uint32_t distance,
    const void *quarter);
/* threat_sum 41cb44 (X): sum of the strength words +29Eh of every entry
 * of the MECH list [4a9b7a] whose target +1A4h is X. */
uint64_t es2_threat_sum_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t target,
    const uint32_t entry[3]);
/* engage_done 41c4a8 (A): AX = 1 when A should stop engaging its target
 * (class above A's limit in order state 4, or the target is out). */
uint64_t es2_engage_done_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t actor,
    const uint32_t entry[3]);
/* flank_step 41c72c (A, S): the two behaviour timers, then either the
 * close test through virtual +3Ch or a point 15000 units off the target
 * whose heading goes into the steering record S. */
uint64_t es2_flank_step_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t actor,
    uint32_t record,const uint32_t entry[3]);
/* retreat_check 41cb94 (A): AX = 1 when A starts the script at 499800
 * (or, when out, resumes its order). */
uint64_t es2_retreat_check_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t actor,
    const uint32_t entry[3]);
/* part_pick 41ce08 (A): word A+2A2h = the part with the best random
 * weighted ratio (FFFFh for none). */
uint64_t es2_part_pick_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t actor,
    const uint32_t entry[3]);
/* drive_apply 41e828 (A, S): drive word and magnitude of S, then
 * vehicle_control(A, steer, speed, 0); its EDX:EAX. */
uint64_t es2_drive_apply_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t actor,
    uint32_t record);
#endif
