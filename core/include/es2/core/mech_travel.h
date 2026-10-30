#ifndef ES2_CORE_MECH_TRAVEL_H
#define ES2_CORE_MECH_TRAVEL_H
#include "es2/core/ai_behaviour.h"
/* The MECH AI state methods for travel, follow, sleep, ram and dead
 * and their helpers (docs/reverse-engineering/ai.md, section "MECH travel,
 * follow, sleep, ram and dead states"):
 * travel_step 0x41d9cc ("travelling", "bulldog travel"), follow_step
 * 0x41daac ("following"), sleep_step 0x41c418 ("sleeping"), ram_step
 * 0x41e570 and ram_move 0x41e488 ("ramming", behaviour and movement),
 * dead_step 0x41e554 ("dead", "disabled"), leader_route 0x41fb60,
 * point_move 0x41fac4 and fire_permit 0x41c3c8.
 *
 * Every game callee (the hooks named in ai.md, the original sound trigger
 * 40f09c and the actor virtuals +3Ch and +74h) is called through g->call
 * with the original's EAX/EDX/ECX, so nested hooks keep their own modes.
 * g->image is the image base (direct callees, the terrain map pointer
 * 4a0be0, the RNG state 4d2599). Each returns the original's EDX:EAX; ECX
 * after return is outside the contract (every caller pops or overwrites
 * it, ai.md).
 *
 * travel_step and follow_step lie on the fire path (both call aim_select).
 * Unless frame is NULL they store, before every game call, the EBX/ESI/EDI
 * the original holds there and the address after its CALL in *frame (an
 * adapter that runs the call at the original's stack coordinate loads
 * them; ai.md, "stack residue under aim_fire"); edi is the caller's EDI, which
 * the original keeps in EDI up to its first change. */
#define ES2_FOLLOW_STEP_SCRATCH 0x4u
#define ES2_RAM_MOVE_SCRATCH 0xcu
/* fire_permit 41c3c8 (A): byte A+96h = (byte A+0B2h set) when the group
 * leader's byte +0A3h is set, else (byte A+97h set). EAX = EDX = A+92h.
 * Leaf. */
uint64_t es2_fire_permit_registers(const es2_guest_memory *,uint32_t actor);
/* dead_step 41e554 (A): vehicle_control(A, 0, 0, 0). EAX = 0. */
uint64_t es2_dead_step_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t actor,
    const uint32_t entry[3]);
/* sleep_step 41c418 (A): drop the target, vehicle_control(A, 0, 0, 0),
 * fire_permit(A). EAX = 0. */
uint64_t es2_sleep_step_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t actor,
    const uint32_t entry[3]);
/* point_move 41fac4 (A, P): steer towards the point P (turn from the
 * heading, speed word A+252h or AAh, 100h with a one-shot sound when A has
 * an order state and P is beyond 30000). EAX = 1 when P is closer than
 * 10000. */
uint64_t es2_point_move_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t actor,
    uint32_t point,const uint32_t entry[3]);
/* leader_route 41fb60 (A): the next point of the group's route, point_move
 * towards it and the route advance when it is reached; without a point
 * vehicle_control(A, 0, 0, 0). */
uint64_t es2_leader_route_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t actor,
    const uint32_t entry[3]);
/* ram_move 41e488 (A): ground step at the terrain height (frame point at
 * scratch, the original's EBP-0Ch), and on a collision (or byte A+0B1h)
 * radius damage round A and the part damage virtual +74h on A's own
 * parts. AX = 1. */
uint64_t es2_ram_move_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t actor,
    const uint32_t entry[3]);
/* ram_step 41e570 (A): the target scan on the expired timer, then up to
 * three steering steps towards the target with two ram_move steps between
 * them while the charge is on, and the charge/rest timer. EAX = 0. */
uint64_t es2_ram_step_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t actor,
    const uint32_t entry[3]);
/* travel_step 41d9cc (A): leader_route, drop the target, the scan timer
 * (target_select into the remembered target A+5Fh, fire permission) and,
 * with a remembered target, aim_select at it. EAX = 0. */
uint64_t es2_travel_step_registers(const es2_guest_memory *,const es2_weapon_graph *,es2_ai_call_frame *frame,
    uint32_t edi,uint32_t actor,const uint32_t entry[3]);
/* follow_step 41daac (A): follow the group's order actor (point_move
 * beyond 25000, else stand), then as travel_step. Frame at scratch: the
 * order actor (the original's local EBP-4). EAX = 0. */
uint64_t es2_follow_step_registers(const es2_guest_memory *,const es2_weapon_graph *,es2_ai_call_frame *frame,
    uint32_t edi,uint32_t actor,const uint32_t entry[3]);
/* turbo_engage 40f09c (T): the TURBO latch T+81h and its sound
 * (sfx_play 462830 through g->call). EAX = T or sfx_play's. */
uint64_t es2_turbo_engage_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t turbo,
    const uint32_t entry[3]);
#endif
