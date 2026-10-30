#ifndef ES2_CORE_AI_ORDER_H
#define ES2_CORE_AI_ORDER_H
#include "es2/core/weapons.h"
/* The AI order helpers below the MECH order handler
 * (docs/reverse-engineering/mech_order.md, section "AI order helpers"). Every
 * game call goes through g->call at the original address with the registers the
 * original holds there, so script_start (413E50), attack_target (41C0F4)
 * and order_resume (41EB34, which calls itself) keep their own modes; the
 * memset thunk 49681A and the AI function 41D5EC stay game code. g->image
 * locates the stagger word 4A9B70, the player 4A9B84 and the pushed
 * script tables; no scratch is used. Each returns the original's EDX:EAX;
 * ECX is outside the contract. */

/* target_class 411CB4 (actor T): AX = 2 if the flag byte +5 of T's script
 * table is set, else 1 if its flag +4 is set, else EAX = 0; EDX = the
 * table + 8, or the entry EDX when T has no table. Pure. */
uint64_t es2_target_class_registers(const es2_guest_memory *,uint32_t target,const uint32_t entry[3]);
/* script_start 413E50 (record S = actor + 4Dh, table): stores the table,
 * clears the counter S+9, sets the timer S+5 from the table's +4 plus the
 * stagger word [4A9B70h] (advanced by 13, low four bits), clears S+0Dh..44h
 * with two memset calls and sets S+36h. EDX:EAX = the second memset's. */
uint64_t es2_script_start_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t record,uint32_t table);
/* attack_target 41C0F4 (vehicle A, target T): makes T A's target (the
 * attacker counts +1A2h follow), then starts a script or calls 41D5EC by
 * T's type word +1A8h. */
uint64_t es2_attack_target_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t vehicle,uint32_t target);
/* order_resume 41EB34 (vehicle A): restarts A's behaviour script from its
 * own flag, the player's order state or the group's current order type
 * (the entry EDX is the table for types above 6), and drops A's target.
 * AX = 1. */
uint64_t es2_order_resume_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t vehicle,const uint32_t entry[3]);
#endif
