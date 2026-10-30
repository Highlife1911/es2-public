#ifndef ES2_CORE_INPUT_DISPATCH_H
#define ES2_CORE_INPUT_DISPATCH_H
#include "es2/core/weapons.h"
/* Torso helpers and the player input dispatcher; contracts in
 * docs/reverse-engineering/input.md, section "torso helpers and the input
 * dispatcher". Every game call, hooked or not, goes through the graph's guest
 * bridge with the EAX/EDX/ECX the original holds at that call, so nested hooks
 * keep their own modes. g->image is the loaded image base; globals are read
 * relative to it.
 *
 * torso_center 41e8d4 (P, d): drives twist and pitch back to 0 through
 * multiply_q10 and the two servos 41a550/41a808. EDX:EAX = 41a808's pair.
 *
 * torso_aim 41ef14 (P, point): turns the torso toward a point; the point
 * is moved into the torso frame (euler_matrix, matrix_transpose,
 * rotate_point, transform_point and the node virtual +20h), aim_angles
 * writes the three static angle words 4A9CFCh, the servos follow.
 * EDX:EAX = 4A9CFCh : 41a808's EDX. Needs ES2_TORSO_AIM_SCRATCH bytes of
 * frame scratch at g->scratch (synthetic EBP at its end).
 *
 * player_input 460870 (R): the per-frame dispatcher of the input record R.
 * Needs ES2_PLAYER_INPUT_SCRATCH bytes of frame scratch at g->scratch.
 * Before every game call it stores the EBX, ESI and EDI that the original
 * holds there in *saved; the bridge loads them (410f78 and the key method
 * return the caller's ESI high word). Without the entry shim the
 * caller's EDI is not visible: the first two calls (45feb8, 432b14) get
 * EDI = 0 there. */
enum { ES2_TORSO_AIM_SCRATCH = 0xb0, ES2_PLAYER_INPUT_SCRATCH = 0x90 };
typedef struct es2_input_saved { uint32_t ebx, esi, edi; } es2_input_saved;
uint64_t es2_torso_center_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t vehicle,uint32_t distance,
    const uint32_t entry[3]);
uint64_t es2_torso_aim_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t vehicle,uint32_t point,
    const uint32_t entry[3]);
/* What 41ef14 holds at each game call besides EAX/EDX/ECX (EBX,
 * ESI = P, EDI) and the return address its CALL leaves (an image VA
 * relative to g->image). A bridge that runs the calls at the original's
 * outgoing stack coordinate (EBP-0B8h, frame at EBP-0B0h = g->scratch)
 * loads them, so a replaced torso_aim leaves the original's frame and
 * callee slots behind (ai.md, "residue shims for torso_aim and weapon_choose").
 * Before every game call it stores them in *frame unless frame is NULL
 * (es2_torso_aim_registers). At 47de72 (41f337) the original has already pushed
 * 41a808's last two slots: pushed = 2 there (0 elsewhere), temp[0] = 1
 * (EBP-0BCh), temp[1] = the target word (EBP-0C0h), and its slots lie below
 * them. *ecx (unless NULL) receives the original's ECX at RET: 41a808's. */
typedef struct es2_input_call { uint32_t ebx, esi, edi, ret, pushed, temp[2]; } es2_input_call;
uint64_t es2_torso_aim_frame_registers(const es2_guest_memory *,const es2_weapon_graph *,es2_input_call *frame,
    uint32_t *ecx,uint32_t vehicle,uint32_t point,const uint32_t entry[3]);
uint64_t es2_player_input_registers(const es2_guest_memory *,const es2_weapon_graph *,es2_input_saved *saved,
    uint32_t record,const uint32_t entry[3]);
/* The same at the original stack coordinate. Before every game call
 * it stores in *frame the EBX, ESI and EDI that 460870 holds there and the
 * return address of its CALL (an image VA relative to g->image; pushed = 0:
 * no call of 460870 has slots of another call above it). caller_edi is the
 * caller's EDI, which the original still holds at its first two calls
 * (45feb8, 432b14). A bridge that runs the calls at the original's outgoing
 * ESP (EBP-98h; g->scratch = EBP-ES2_PLAYER_INPUT_SCRATCH, so the frame
 * locals land at the original's EBP offsets) loads them, and the callees
 * find the original's stack bytes below them (input.md, "stack residue under
 * the command handler"). */
uint64_t es2_player_input_frame_registers(const es2_guest_memory *,const es2_weapon_graph *,es2_input_call *frame,
    uint32_t caller_edi,uint32_t record,const uint32_t entry[3]);
#endif
