#ifndef ES2_CORE_LOGIC_FRAME_H
#define ES2_CORE_LOGIC_FRAME_H
#include "es2/core/weapons.h"
/* The logic frame 0x45f570 (docs/reverse-engineering/main_loop.md,
 * section "the logic frame as own code").
 *
 * No stack argument. The runtime record 0x49630c, the control record
 * 0x45a810, the seven pool loops (0x471c70 per round, the update function
 * or the projectiles' virtual +14h, 0x478628 for a removal), the actor
 * pool's virtual +18h, the group step (0x4236c4 or 0x423a74), the step
 * request, the input dispatch 0x460870, the timers 0x426b48 and triggers
 * 0x426b70, the view request 0x45de30, the actor pass 0x4123ac, the MECH
 * loop with 0x41aa5c, and either the death sequence 0x45fa84 or the
 * outcome poll 0x4131ac (with 0x423f08 for status 3), each possibly
 * followed by the alert dialog (0x47749c, 0x455950, the dialog's virtual
 * +8, 0x4543b0). Returns EAX (0/1, or player_input's EAX with AX = 1),
 * EDX = the saved FS:0 head [EBP-30h], which the caller (the adapter)
 * stores back to FS:0; *ecx receives the original's ECX.
 *
 * Every game call goes through g->call (observed first: direct calls by
 * their original VA with slot 0, virtual calls by the resolved target and
 * the slot) with the EAX/EDX/ECX the original holds there, so a nested
 * hook keeps its own mode. g->image is the loaded image base (globals and
 * callees are relocated by it). g->scratch is the frame: the original's
 * EBP minus ES2_LOGIC_FRAME_SIZE (locals 74h bytes and the three saved
 * registers); the core writes every local at the original's offset and
 * 0x49630c (through g->call, with EBP = g->scratch + ES2_LOGIC_FRAME_SIZE)
 * writes the record at EBP-30h.
 *
 * frame (required): before every game call the core stores there the EBX,
 * ESI and EDI that 0x45f570 holds at that call and the return address of
 * its CALL (an image VA relative to g->image); caller_ebx/esi/edi are the
 * caller's registers, which reach the first calls. With frame->top nonzero
 * (the original's outgoing ESP, EBP-80h) ECX after a one-slot call is
 * re-read from the argument slot top-4 (the original's POP ECX); otherwise
 * it is the pushed argument. */
#define ES2_LOGIC_FRAME_SIZE 0x80u
typedef struct es2_logic_frame { es2_weapon_frame call; uint32_t top; } es2_logic_frame;
uint64_t es2_logic_frame_registers(const es2_guest_memory *, const es2_weapon_graph *, es2_logic_frame *frame,
    uint32_t caller_ebx, uint32_t caller_esi, uint32_t caller_edi, const uint32_t entry[3], uint32_t *ecx);
#endif
