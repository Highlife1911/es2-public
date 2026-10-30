#ifndef ES2_CORE_ACTOR_STEP_H
#define ES2_CORE_ACTOR_STEP_H
#include "es2/core/weapons.h"
/* The row-11 per-member step of MECH and FLYER actors
 * (docs/reverse-engineering/vtables.md, section "actor_step"):
 * actor_step 0x411cec, state_frame 0x413eb0 and the six update-slot
 * trampolines of the MECH table 0x49a282 (+14h 0x415afc, +18h 0x415b38,
 * +1Ch 0x415b74) and the FLYER table 0x49a5e0 (+14h 0x4217fc, +18h
 * 0x42184c, +1Ch 0x421888), which call a member pointer of the actor's AI
 * state record [A+4Dh].
 *
 * Every game call (the update slots through the actor's vtable, the state
 * methods through the member pointers, timer_dword_step) goes through
 * g->call with the original's entry EAX/EDX/ECX, so nested hooks keep
 * their own modes. g->image is the image base (direct callees and the
 * return addresses below). g->scratch is the original's frame: actor_step
 * takes its EBP (the core writes the stack dwords 413eb0 leaves there),
 * the trampolines the address of their lowest local (the core writes the
 * locals there). Before every game call the core stores in *frame the EBX
 * the original holds there and the address after its CALL instruction (an
 * address in the image at g->image); ESI and EDI are the caller's in the
 * original and are left to the caller of the core. A bridge that runs the
 * call at the original's outgoing stack coordinate loads them and leaves
 * the return address behind (vtables.md, "Stack coordinate").
 *
 * All return the original's EDX:EAX and store its ECX in *ecx. */
typedef struct es2_actor_call_frame { uint32_t ebx, esi, edi, ret; } es2_actor_call_frame;

/* actor_step 0x411cec (A): nothing for a null state; else the decision
 * slot +1Ch when the state timer A+52h has run out, the state timer step
 * unless the state's byte +8 is set, the movement slot +14h, the behaviour
 * slot +18h unless the skip byte A+0AFh is set (then cleared), a decision
 * request when the behaviour's AX is nonzero, and 413eb0(A+4Dh) computed
 * in core. g->scratch = the original's EBP; the core writes EBP-8 ..
 * EBP-10h (413eb0's argument, return address and saved EBP). */
uint64_t es2_actor_step_registers(const es2_guest_memory *,const es2_weapon_graph *,
    es2_actor_call_frame *frame,uint32_t actor,const uint32_t entry[3],uint32_t *ecx);
/* state_frame 0x413eb0 (P = A+4Dh): clear the once-flag A+83h, or set
 * A+82h and count A+56h up. Leaf. */
uint64_t es2_state_frame_registers(const es2_guest_memory *,uint32_t state_slot,uint32_t *ecx);
/* The trampolines 0x415afc/0x415b38/0x415b74/0x42184c/0x421888 (A): copy
 * the member pointer at state+member (member = 24h, 18h or 30h) to the
 * locals at g->scratch (the original's EBP-0Ch) and call its function with
 * A + delta, entry EAX = the state, EDX = the third dword, ECX = the OR of
 * the three dwords; EAX = 0 when all three are 0. ret is the address after
 * the original's CALL (0x415b2d, 0x415b69, 0x415ba5, 0x42187d, 0x4218b9). */
uint64_t es2_state_member_registers(const es2_guest_memory *,const es2_weapon_graph *,
    es2_actor_call_frame *frame,uint32_t actor,unsigned member,uint32_t ret,uint32_t *ecx);
/* The FLYER movement trampoline 0x4217fc (A): as above for member 24h, but
 * it first copies the position A+26h..A+31h to its locals EBP-0Ch..-4 and
 * keeps the member pointer at EBP-18h (g->scratch = EBP-18h); entry EAX =
 * the OR, EDX = the state, ECX = the third dword. */
uint64_t es2_flyer_member_registers(const es2_guest_memory *,const es2_weapon_graph *,
    es2_actor_call_frame *frame,uint32_t actor,uint32_t *ecx);
#endif
