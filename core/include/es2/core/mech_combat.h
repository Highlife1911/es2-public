#ifndef ES2_CORE_MECH_COMBAT_H
#define ES2_CORE_MECH_COMBAT_H
#include "es2/core/ai_behaviour.h"
/* The combat states of the MECH AI (docs/reverse-engineering/ai.md,
 * section "MECH combat states"): the step members (+18h, called by the
 * trampoline 415b38) of the state records "attacking" 49945e (attack_step
 * 0x41c594), "flanking" 49949c (flank_attack 0x41d4e4), "facing off" 4994da
 * (face_off 0x41d41c), "attacking base" 499518 (base_attack 0x41c86c),
 * "attacking flyer" 499556 (flyer_attack 0x41c9cc) and "driving off" 499784
 * (drive_off 0x41def0), with their shared helpers engage_end 0x41c478 and
 * skirt_check 0x41de9c.
 *
 * Every game callee (the hooks named in ai.md and this group's own helpers)
 * is called through g->call with the original's EAX/EDX/ECX, so nested hooks
 * keep their own modes. g->image is the image base (the pushed state record
 * 499708h). The six steps pass frame addresses: g->scratch is their frame
 * (ES2_*_SCRATCH guest bytes from the lowest local, the original's layout).
 * Unless frame is NULL they also store, before every game call, the
 * EBX/ESI/EDI the original holds there and the address after its CALL in
 * *frame (an adapter that runs the call at the original's stack coordinate
 * loads them; ai.md, "stack residue under aim_fire"); esi and edi are the
 * caller's, which the originals keep in ESI/EDI up to their first own use. Each
 * returns the original's EDX:EAX; ECX after return is outside the contract
 * (every caller pops it).
 * Preconditions (the original faults otherwise): the actor has a target
 * (an actor) wherever a step reaches target_bearing, engage_done or a
 * target field. */
#define ES2_ATTACK_STEP_SCRATCH 0x30u
#define ES2_COMBAT_STEP_SCRATCH 0x18u
/* engage_end 41c478 (A): in order state 4 with A's target the order target,
 * the order state word becomes 0. EAX = 1; EDX = the target in state 4,
 * else the entry EDX. Leaf. */
uint64_t es2_engage_end_registers(const es2_guest_memory *,uint32_t actor,const uint32_t entry[3]);
/* skirt_check 41de9c (A): with byte A+0ADh set (the line of fire was
 * blocked), keep the current state record in B+0, start "skirting" 499708h
 * and store the distance to the point A+31Eh in B+0Dh; AX = 1. Else EAX = 0
 * with the entry EDX. */
uint64_t es2_skirt_check_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t actor,
    const uint32_t entry[3]);
/* attack_step 41c594 (A): "attacking". Frame at scratch (30h bytes): the
 * mirror point +0, the offset point +0Ch, the steering record +18h. */
uint64_t es2_attack_step_registers(const es2_guest_memory *,const es2_weapon_graph *,es2_ai_call_frame *frame,
    uint32_t esi,uint32_t edi,uint32_t actor,const uint32_t entry[3]);
/* base_attack 41c86c (A): "attacking base". Steering record at scratch. */
uint64_t es2_base_attack_registers(const es2_guest_memory *,const es2_weapon_graph *,es2_ai_call_frame *frame,
    uint32_t esi,uint32_t edi,uint32_t actor,const uint32_t entry[3]);
/* flyer_attack 41c9cc (A): "attacking flyer". Steering record at scratch. */
uint64_t es2_flyer_attack_registers(const es2_guest_memory *,const es2_weapon_graph *,es2_ai_call_frame *frame,
    uint32_t esi,uint32_t edi,uint32_t actor,const uint32_t entry[3]);
/* face_off 41d41c (A): "facing off". Steering record at scratch. */
uint64_t es2_face_off_registers(const es2_guest_memory *,const es2_weapon_graph *,es2_ai_call_frame *frame,
    uint32_t esi,uint32_t edi,uint32_t actor,const uint32_t entry[3]);
/* flank_attack 41d4e4 (A): "flanking". Steering record at scratch. */
uint64_t es2_flank_attack_registers(const es2_guest_memory *,const es2_weapon_graph *,es2_ai_call_frame *frame,
    uint32_t esi,uint32_t edi,uint32_t actor,const uint32_t entry[3]);
/* drive_off 41def0 (A): "driving off": skirt_check, else face_off; the rest
 * of the original body is unreachable (ai.md). No frame use. */
uint64_t es2_drive_off_registers(const es2_guest_memory *,const es2_weapon_graph *,es2_ai_call_frame *frame,
    uint32_t esi,uint32_t edi,uint32_t actor,const uint32_t entry[3]);
#endif
