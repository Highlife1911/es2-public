#ifndef ES2_CORE_FLYER_H
#define ES2_CORE_FLYER_H
#include "es2/core/weapons.h"
/* Flyer flight model 466b58; contract in
 * docs/reverse-engineering/flyer.md. Every game call (the platform function
 * 477904 and the hooked leaves movement_step, multiply_q8, multiply_q10,
 * heading_cos, euler_matrix, rotate_point, matrix_transpose, matrix_angles)
 * goes through the graph's guest bridge with the EAX/EDX/ECX the original
 * holds at that call, so nested hooks keep their own modes. The private
 * leaves 466a88, 466ae0, 466b20, 467b28 and the pure helpers 47e08d,
 * 47e0c0 and 47f058 are computed here (466ae0/466b20 call movement_step
 * through the bridge).
 *
 * flight_model(C, S, M, F): control record, 78-byte flight state, 54-byte
 * flight-model block, four failure flags. g->scratch is the original's
 * 1C0h-byte frame (EBP = scratch + 1C0h); g->padding and
 * g->composed_padding are the stack bytes the original finds at entry
 * ESP-191h and ESP-1A5h (bytes 19 of its local matrices; the composed pose
 * matrix copies one of them into the actor's pose byte 19). EDX:EAX is
 * what the original leaves (both callers ignore it). */
uint64_t es2_flight_model_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t control,uint32_t state,
    uint32_t model,uint32_t failures,const uint32_t entry[3]);
/* 47f058(a, b, out): the 20 rotation bytes of a x b by the mode bytes
 * (identity operand: 20-byte copy of the other; planar and general cases
 * as matrix_compose), no translation; byte 19 of out is written only by
 * the copies. */
void es2_compose_rotation(const es2_guest_memory *,uint32_t first,uint32_t second,uint32_t output);
/* Flyer control (docs/reverse-engineering/flyer.md, section "flyer control"):
 * 41bb9c flyer_input(P, turn, throttle, pitch, twist) with a 24h-byte
 * scratch frame, 4221a8 flyer_command(A, record) with 14h bytes, 42215c
 * flyer_bank(A, goal, record) without frame, 4222fc flyer_steer(A, yaw,
 * pitch, target) with 28h bytes. terrain_height, heading_cos,
 * flight_model, 42215c and 4221a8 are called through g->call with the
 * original's registers; the adapter plants the flight model's stack
 * residue. EDX:EAX is what the original leaves. */
uint64_t es2_flyer_input_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t player,uint32_t turn,
    uint32_t throttle,uint32_t pitch,uint32_t twist,const uint32_t entry[3]);
uint64_t es2_flyer_command_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t actor,uint32_t record,
    const uint32_t entry[3]);
uint64_t es2_flyer_bank_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t actor,uint32_t goal,
    uint32_t record,const uint32_t entry[3]);
uint64_t es2_flyer_steer_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t actor,uint32_t yaw,
    uint32_t pitch,uint32_t target,const uint32_t entry[3]);
#endif
