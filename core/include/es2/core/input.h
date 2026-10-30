#ifndef ES2_CORE_INPUT_H
#define ES2_CORE_INPUT_H
#include "es2/core/weapons.h"
/* Children of the input dispatcher 460870; contracts in
 * docs/reverse-engineering/input.md. Every game call, hooked or not, goes
 * through the graph's guest bridge with the EAX/EDX/ECX the original holds
 * at that call, so nested hooks keep their own modes. g->image is the
 * loaded image base; none of the four needs scratch.
 *
 * fire_selected 410dbc (weapon system W, input record R, vehicle P): fires
 * the selected weapon and its linked partner through the weapon virtuals
 * +2Ch, +30h, +28h, +60h. EDX:EAX = residue : 0 or 1.
 *
 * fire_line 415608 (P, R): 410dbc, then the line of points ahead of the
 * torso at 4a9b88 with its count at 4a9cc8. The 2 x 2 rotation 47eb30 and
 * the vector product 47ffe8 are pure helpers computed here (quarter sine
 * table read from the image at 4a25c4).
 *
 * aim_point 41b728 (P, out, p3, p4): the target's aim point (12 bytes) by
 * the weapon 40e4dc, the target's node through its pose, or its position.
 *
 * leg_input 4160dc (P, turn, throttle): throttle lever, speed set point
 * P+290h and the turn sign, then vehicle_control 416a04. */
uint64_t es2_fire_selected_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t weapons,uint32_t record,
    uint32_t vehicle,const uint32_t entry[3]);
uint64_t es2_fire_line_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t vehicle,uint32_t record,
    const uint32_t entry[3]);
uint64_t es2_aim_point_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t vehicle,uint32_t out,
    uint32_t slot3,uint32_t slot4,const uint32_t entry[3]);
uint64_t es2_leg_input_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t vehicle,uint32_t turn,
    uint32_t throttle,const uint32_t entry[3]);
/* Targeting computer (input.md, "the targeting computer 40e4dc and 40e4ac"):
 * targ_next 40e4ac (W, T) moves the part lock W+7Dh to the target's next living
 * part (virtual +80h); targ_aim 40e4dc (W, T, out, -, p5, p6) is aim_point's
 * path through the TARG equipment [P+30Bh]. The fourth slot of 40e4dc is never
 * read. Game calls (the target's virtuals +24h, +58h, +80h, +84h, 40e4ac,
 * timer_dword_step, euler_matrix, transform_point) go through g->call. */
uint64_t es2_targ_next_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t weapon,uint32_t target,
    const uint32_t entry[3]);
uint64_t es2_targ_aim_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t weapon,uint32_t target,
    uint32_t out,uint32_t slot5,uint32_t slot6,const uint32_t entry[3]);
#endif
