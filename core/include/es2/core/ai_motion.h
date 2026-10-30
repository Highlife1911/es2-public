#ifndef ES2_CORE_AI_MOTION_H
#define ES2_CORE_AI_MOTION_H
#include "es2/core/weapons.h"
/* AI flyer move step 4218c4; exact contract in
 * docs/reverse-engineering/flyer.md ("the AI flyer move step").
 * Game calls (euler_matrix, transform_point, terrain_height, multiply_q10,
 * dist3d_delta and the sound 462830) go through the graph's guest bridge
 * with the registers the original holds at each call, so hooked callees
 * are entered as hooks and the sound stays original code in its place.
 * The probe point and the transformed point live in the graph scratch
 * (128 bytes, synthetic EBP at scratch+80h: V at -2Ch, Q at -38h).
 * Returns the original's full EDX:EAX. */
/* Formation steering 41fbb8 (docs/reverse-engineering/ai.md):
 * dist2d_point, heading_point, euler_matrix, vehicle_control and the
 * virtual +78h through the bridge; 47df04 and 47ffe8 are computed. Frame
 * locals T, R, Q, M in the scratch (EBP-1Ch, -28h, -34h, -8). */
uint64_t es2_formation_steer_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t vehicle,const uint32_t entry[3]);
uint64_t es2_flyer_move_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t flyer,const uint32_t entry[3]);
#endif
