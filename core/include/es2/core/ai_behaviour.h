#ifndef ES2_CORE_AI_BEHAVIOUR_H
#define ES2_CORE_AI_BEHAVIOUR_H
#include "es2/core/weapons.h"
/* The AI behaviours around the torso servos
 * (docs/reverse-engineering/ai.md, section "AI behaviours around the torso
 * servos"): weapon_range 0x40e5f8,
 * weapon_choose 0x41f358, aim_fire 0x41f5a0, aim_target 0x41e984,
 * target_search 0x41e0e0, hunt 0x41e224, line_blocked 0x41dc24,
 * clear_shot 0x41dd64 and group_move 0x41d598.
 *
 * Every game callee (the hooks named in ai.md, the original helpers
 * 41c3c8/41fac4/41fb60/471c70 and the actor/weapon virtuals) is called
 * through g->call with the original's entry EAX/EDX/ECX, so nested hooks
 * keep their own modes. g->image is the image base (globals, tables,
 * pushed script tables and the RNG state); g->scratch holds the frame of
 * the hooks that pass frame addresses (ES2_*_SCRATCH guest bytes, the
 * original's frame layout from its lowest local). Each returns the
 * original's EDX:EAX; ECX after return is outside the contract (except
 * weapon_range, whose adapter loads it in an entry shim).
 * Preconditions (the original faults): the actor has a target where the
 * function reads it; target_search's candidates have attacker counts
 * other than FFFFh. */
#define ES2_LINE_BLOCKED_SCRATCH 0x2cu
#define ES2_CLEAR_SHOT_SCRATCH 0x0cu
#define ES2_AIM_TARGET_SCRATCH 0x1cu
#define ES2_AIM_FIRE_SCRATCH 0x10u
/* weapon_range 40e5f8 (W, d): EAX = 1 when [[W+1Ch]+2Ch] < d < [[W+1Ch]+30h]
 * (signed), EDX = d. Pure. */
uint64_t es2_weapon_range_registers(const es2_guest_memory *,uint32_t weapon,uint32_t distance);
/* weapon_choose 41f358 (A, angle, d, T): the ready weapon with the best
 * random score, or 0 (then possibly trigger_fire). */
uint64_t es2_weapon_choose_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t actor,
    uint32_t angle,uint32_t distance,uint32_t target,const uint32_t entry[3]);
/* What the original holds at one game call besides EAX/EDX/ECX: its EBX,
 * ESI and EDI, and the return address its CALL leaves on the stack (an
 * image VA relative to g->image). A bridge that runs the call at the
 * original's outgoing stack coordinate loads the registers and leaves the
 * return address behind (ai.md, "stack residue under aim_fire"). */
typedef struct es2_ai_call_frame { uint32_t ebx, esi, edi, ret; } es2_ai_call_frame;
/* weapon_choose at the original stack coordinate. Besides the
 * registers and the return address, whether the original has a temporary
 * pushed below its outgoing ESP at this call (pushed = 1 at the second
 * multiply_q10 and both rng_bounded calls, 41f4e3/41f502) and its value.
 * With frame non-NULL the core also writes the original's frame locals at
 * g->scratch (EBP-18h, ES2_WEAPON_CHOOSE_SCRATCH bytes): best -2, t34 -4,
 * chosen -8, count -0Ah, none -0Ch, bound -0Eh, i -10h, rank -14h, cost
 * -18h, each when the original writes it. */
typedef struct es2_ai_push_frame { es2_ai_call_frame call; uint32_t pushed, temp; } es2_ai_push_frame;
#define ES2_WEAPON_CHOOSE_SCRATCH 0x18u
uint64_t es2_weapon_choose_frame_registers(const es2_guest_memory *,const es2_weapon_graph *,es2_ai_push_frame *frame,
    uint32_t actor,uint32_t angle,uint32_t distance,uint32_t target,const uint32_t entry[3]);
/* aim_fire 41f5a0 (A, P, w, T): aim the torso at P (lead and spread) and
 * fire the kept or chosen weapon. Frame at scratch: the point L at +0 and
 * the speed word k at +0Eh (the original's EBP-10h and EBP-2). Before
 * every game call it stores the original's EBX/ESI/EDI and return address
 * in *frame unless frame is NULL. */
uint64_t es2_aim_fire_registers(const es2_guest_memory *,const es2_weapon_graph *,es2_ai_call_frame *frame,
    uint32_t actor,uint32_t point,uint32_t word,uint32_t target,const uint32_t entry[3]);
/* aim_target 41e984 (A, w, T): T inside the turret cone gives a part or
 * centre aim point (scratch) and aim_fire; otherwise torso_center. */
uint64_t es2_aim_target_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t actor,
    uint32_t word,uint32_t target);
/* target_search 41e0e0 (A, P): the best MECH-list target near P, or 0. */
uint64_t es2_target_search_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t actor,
    uint32_t point);
/* hunt 41e224 (A): script step; target choice, attack/retreat script or
 * movement. EAX = 0. */
uint64_t es2_hunt_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t actor,
    const uint32_t entry[3]);
/* line_blocked 41dc24 (A): AX = 0 clear, 1 an object or terrain with a
 * segment contact, 2 terrain only. Frame at scratch. */
uint64_t es2_line_blocked_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t actor);
/* clear_shot 41dd64 (A): script step; circle the point A+31Eh until the
 * line of fire is clear. EAX = 0. Frame point at scratch. */
uint64_t es2_clear_shot_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t actor);
/* group_move 41d598 (A): own point, leader route or formation slot, then
 * 41c3c8 and torso_center; torso_center's EDX:EAX. */
uint64_t es2_group_move_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t actor,
    const uint32_t entry[3]);
#endif
