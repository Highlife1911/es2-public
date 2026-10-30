#ifndef ES2_CORE_FLYER_STATES_H
#define ES2_CORE_FLYER_STATES_H
#include "es2/core/weapons.h"
/* The FLYER AI state methods and their shared helpers
 * (docs/reverse-engineering/flyer.md, section "the FLYER AI state
 * methods"): flyer_attacking
 * 0x422ca8 (behaviour of the "attacking" record), flyer_patrol 0x422b34,
 * flyer_search 0x422a80, flyer_scout 0x422bdc (behaviours of "patrolling",
 * "search and destroy", "scouting"), flyer_decision 0x422d00 (the decision
 * method of the four active records and "deciding"), and the helpers
 * flyer_formation 0x422a50 (leader route step or wingman follow),
 * flyer_follow 0x422598 (wingman: formation point beside the leader and
 * steering) and flyer_engage 0x422bf0 (switch to "attacking", hand the
 * target to the group).
 *
 * Every game callee (the hooks point_offset, dist_gate, heading_point,
 * flyer_steer, target_emit, timer_word_step, target_select, order_target,
 * script_start, this group's own hooks, the original helpers 0x4224c4,
 * 0x422108, 0x422260 and the actor virtual +78h) is called through g->call
 * with the EAX/EDX/ECX the original holds there, so nested hooks keep their
 * own modes. g->image is the image base (the state records pushed by
 * flyer_decision and flyer_engage). flyer_follow's frame (the point P at
 * +0, the zero dword at +0Ch, the distance at +10h; the original's EBP-14h
 * .. EBP-1) is at g->scratch. Each returns the original's EDX:EAX; ECX
 * after return is outside the contract (every caller pops it). */
#define ES2_FLYER_FOLLOW_SCRATCH 0x14u
/* flyer_attacking 0x422ca8 (A): target T = [A+1A4h] out (byte T+0A4h or
 * T+99h set) returns AX = 1; else the leader runs target_emit, a wingman
 * flyer_follow, and EAX = 0. T must not be 0 (the original faults). */
uint64_t es2_flyer_attacking_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t actor,
    const uint32_t entry[3]);
/* flyer_follow 0x422598 (A): wingman of the leader L = [[[A+45h]+0Ch]]. */
uint64_t es2_flyer_follow_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t actor,
    const uint32_t entry[3]);
/* flyer_formation 0x422a50 (A): 0x4224c4 for the leader, flyer_follow for
 * a wingman, then byte A+96h = 0. */
uint64_t es2_flyer_formation_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t actor,
    const uint32_t entry[3]);
/* flyer_search 0x422a80 / flyer_patrol 0x422b34 (A): flyer_formation;
 * the leader picks a target (class 11h / 10h) when its timer A+5Ah runs
 * out and engages it. EAX = 0. */
uint64_t es2_flyer_search_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t actor,
    const uint32_t entry[3]);
uint64_t es2_flyer_patrol_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t actor,
    const uint32_t entry[3]);
/* flyer_scout 0x422bdc (A): flyer_formation, EAX = 0. */
uint64_t es2_flyer_scout_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t actor,
    const uint32_t entry[3]);
/* flyer_decision 0x422d00 (A): the next state record from the group's
 * current order type; types 1, 2 and others start the record in the entry
 * ECX (a defect of the original, see flyer.md). AX = 1. */
uint64_t es2_flyer_decision_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t actor,
    const uint32_t entry[3]);
/* flyer_engage 0x422bf0 (A): "attacking" for A; a leader hands its target
 * to every wingman not out (byte +99h) and engages them (recursive). */
uint64_t es2_flyer_engage_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t actor,
    const uint32_t entry[3]);
/* The FLYER AI helpers below the state methods (flyer.md, section "the
 * FLYER AI helpers"). Every game callee (route_point, dist2d_point,
 * route_advance, heading_point, flyer_steer, multiply_q8, vector_angle,
 * heading_cos and 0x422108/0x422098 themselves) goes through g->call with the
 * original's EAX/EDX/ECX; the pure 0x47e08d (q16) and 0x47e0c0 (q14) are
 * computed. Each returns the original's EDX:EAX; ECX after return is dead at
 * every caller (not delivered). No scratch frame is needed. */
/* flyer_route 0x4224c4 (A): the leader's route step: next waypoint of the
 * group's route record [A+45h]+4 (advanced within 15000), pitch toward
 * height 30000 (0x422108), flyer_steer(A, yaw, pitch, 0), word A+21Ch =
 * 0B4h. Entry ECX's high word reaches route_point's second slot. */
uint64_t es2_flyer_route_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t actor,
    const uint32_t entry[3]);
/* flyer_altitude 0x422108 (A, height): goal pitch from word A+23Ch
 * (multiply_q8, plus the pitch A+0Ch) when byte A+0AEh is set, else
 * vector_angle(10000, height - [A+2Eh]); then 0x422098(A, goal). */
uint64_t es2_flyer_altitude_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t actor,
    uint32_t height,const uint32_t entry[3]);
/* flyer_pitch 0x422098 (A, goal): q14(cos(roll), q16(goal - pitch, 5DCh))
 * - q16(pitch rate A+25Bh, 1F40h). No memory write. */
uint64_t es2_flyer_pitch_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t actor,
    uint32_t goal,const uint32_t entry[3]);
/* flyer_throttle 0x422260 (A, L, d, h, spacing): word A+21Ch += the
 * along-track error (q14(d, cos(h + A.heading - L.heading)) - spacing) / 2
 * + 8 (L+233h - A+233h), clamped to [8Ch, 100h]. */
uint64_t es2_flyer_throttle_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t actor,
    uint32_t leader,uint32_t distance,uint32_t heading,uint32_t spacing,const uint32_t entry[3]);
#endif
