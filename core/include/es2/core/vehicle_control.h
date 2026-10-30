#ifndef ES2_CORE_VEHICLE_CONTROL_H
#define ES2_CORE_VEHICLE_CONTROL_H
#include "es2/core/weapons.h"
/* Vehicle control 416a04 and its helpers; exact contracts in
 * docs/reverse-engineering/ai.md. Game calls go through the graph's
 * guest bridge with the entry registers the original holds at each call, so
 * hooked callees are entered as hooks.
 *
 * part_ratio 40dbc0 (obj, group word): Q8 condition of a part group. Pure;
 * EDX:EAX = remainder:quotient. A zero divisor or a quotient overflow
 * faults like the original (precondition).
 *
 * slope_speed 41693c (actor, speed word pointer): terrain face and matrix,
 * speed corrected along the slope. Needs 16 scratch bytes (frame vector).
 *
 * random_damage 417a04 (vehicle, amount word, chance word): RNG-driven part
 * damage through the vehicle virtual +74h. No scratch.
 *
 * vehicle_control 416a04 (actor, turn, speed, flags): speed command, clip
 * selection and turn rate. Needs 32 scratch bytes (its argument words). A
 * zero divisor in the tail faults like the original (precondition). */
uint64_t es2_part_ratio_registers(const es2_guest_memory *,uint32_t obj,uint32_t group);
uint64_t es2_slope_speed_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t actor,uint32_t speed,
    const uint32_t entry[3]);
uint64_t es2_random_damage_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t vehicle,uint32_t amount,
    uint32_t chance,const uint32_t entry[3]);
uint64_t es2_vehicle_control_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t actor,uint32_t turn,
    uint32_t speed,uint32_t flags,const uint32_t entry[3]);
#endif
