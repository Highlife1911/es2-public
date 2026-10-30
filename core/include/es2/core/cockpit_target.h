#ifndef ES2_CORE_COCKPIT_TARGET_H
#define ES2_CORE_COCKPIT_TARGET_H
#include "es2/core/weapons.h"
/* The cockpit target selection (docs/reverse-engineering/hud.md,
 * "cockpit target cycling as own code"). H is the cockpit object
 * [49B088h], T an actor. Every game call goes through g->call at the
 * original address with the registers the original holds there, so the
 * hooked callees (dist_gate, heading_point, angle_ratio, multiply_q10 and
 * these five among themselves) keep their own modes; the vehicle virtuals
 * +3Ch/+10h and the HUD record update 43D9B4h stay game code. g->image
 * locates the globals; no scratch is used. Each returns the original's
 * EDX:EAX; ECX is outside the contract. */

/* cockpit_valid 433174 (H, T): 1 if T may be the cockpit target. */
uint64_t es2_cockpit_valid_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t cockpit,uint32_t target,const uint32_t entry[3]);
/* cockpit_arc 433250 (H, T): 1 if T lies within +-9000 of the heading. */
uint64_t es2_cockpit_arc_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t cockpit,uint32_t target,const uint32_t entry[3]);
/* cockpit_next 43349C (H): rebuilds the four-entry target list and steps
 * the cockpit target [H+210h]. */
uint64_t es2_cockpit_next_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t cockpit,const uint32_t entry[3]);
/* cockpit_nearest 4333C8 (H): the nearest valid actor of type 0 or 2. */
uint64_t es2_cockpit_nearest_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t cockpit,const uint32_t entry[3]);
/* cockpit_select 4332DC (H, T): clears (T = 0) or cycles to T. The cycle
 * must reach its starting target (precondition, see hud.md). */
uint64_t es2_cockpit_select_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t cockpit,uint32_t target,const uint32_t entry[3]);
#endif
