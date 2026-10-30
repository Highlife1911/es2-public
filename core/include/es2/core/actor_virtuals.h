#ifndef ES2_CORE_ACTOR_VIRTUALS_H
#define ES2_CORE_ACTOR_VIRTUALS_H
#include "es2/core/weapons.h"
/* Small actor virtuals with simulation reads (slot matrix:
 * docs/reverse-engineering/vtables.md section "actor virtuals"):
 *   base_aim_node      403548  BASE  +24h (static node record 4a95cc)
 *   base_offsets       40351c  BASE  +30h (through 411a74)
 *   base_condition     4052b4  BASE  +40h (IDIV by the summed maxima)
 *   actor_attacked     411aec  ACTOR +50h (BASE classes, FLYER; 41f7b8)
 *   base_part_position 406808  BASE  +58h (matrix cache, 4801d4)
 *   mech_aim_node      417b98  MECH  +24h (shape slot +20h)
 *   mech_offsets       4155c4  MECH  +30h
 *   mech_armour        4154d0  MECH  +34h
 *   mech_target_scale  415498  MECH  +38h (multiply_q10)
 *   mech_engage_roll   41cabc  MECH  +4Ch (two rng_next draws, stream A)
 *   mech_link_position 41dd2c  MECH  +64h
 * Every game callee runs through g->call with the original's entry
 * registers (nested hooks keep their modes; memset 49681a, 411a74, 4801d4
 * and the shape slot stay original code) and is reported to g->observe
 * (slot calls with their vtable offset). Image addresses (the static node
 * record, its first-call byte, the default node record 6c56a8) are reached
 * through g->image. entry[] is EAX/EDX/ECX at entry. Each returns EDX:EAX
 * exactly as the original; ECX and the flags at return are not part of the
 * contract (dead at every listed call site, see vtables.md). No scratch. */
uint64_t es2_base_aim_node_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t actor,const uint32_t entry[3]);
uint64_t es2_base_offsets_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t actor,uint32_t first,uint32_t second,const uint32_t entry[3]);
/* The original divides (256 * sum of the current words) by the sum of the
 * maxima with IDIV, which faults on a zero sum and on 80000000h / -1. The
 * core does not divide then: it returns 0 with *fault = 1 and leaves the
 * dividend and divisor in operands[0..1] (always set), and the caller raises
 * the same exception (the proxy adapter executes the IDIV). */
uint64_t es2_base_condition_registers(const es2_guest_memory *,uint32_t actor,uint32_t operands[2],int *fault);
uint64_t es2_actor_attacked_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t actor,uint32_t attacker,const uint32_t entry[3]);
uint64_t es2_base_part_position_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t actor,uint32_t part,uint32_t out,const uint32_t entry[3]);
uint64_t es2_mech_aim_node_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t actor,const uint32_t entry[3]);
uint64_t es2_mech_offsets_registers(const es2_guest_memory *,uint32_t actor,uint32_t first,uint32_t second);
uint64_t es2_mech_armour_registers(const es2_guest_memory *,uint32_t actor,uint32_t angle);
uint64_t es2_mech_target_scale_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t actor,const uint32_t entry[3]);
uint64_t es2_mech_engage_roll_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t actor,uint32_t target,const uint32_t entry[3]);
uint64_t es2_mech_link_position_registers(const es2_guest_memory *,uint32_t actor,const uint32_t entry[3]);
#endif
