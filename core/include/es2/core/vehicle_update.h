#ifndef ES2_CORE_VEHICLE_UPDATE_H
#define ES2_CORE_VEHICLE_UPDATE_H
#include "es2/core/weapons.h"
/* Per-vehicle update 41aa5c and its leaves; exact contracts in
 * docs/reverse-engineering/vehicle.md ("Per-vehicle update"). Game calls go
 * through the graph's guest bridge with the entry registers the original
 * holds at each call, so hooked callees are entered as hooks. Every
 * function returns the original's full EDX:EAX. Frame words that the
 * original passes by address live in the graph scratch (128 bytes, a
 * synthetic EBP at scratch+80h).
 *
 * combat_rating 41edd8 (vehicle): word A+29Eh from the condition record;
 * the weapon count must be <= 10 (precondition).
 * condition_record 41b4ec (vehicle, out): config word +6Ch and
 * condition_table. No scratch.
 * device_state 41aa10, device_word 41aa44 (vehicle): pure readers.
 * ready_tone 41b0bc (vehicle): tone flags 49a1d0/49a1d1, clock and sound
 * stay original. No scratch.
 * weapon_tally 40fbdc (group, out): memset and V60 through the bridge; the
 * V60 local word lives in the scratch.
 * vehicle_update 41aa5c (vehicle): the tally buffer lives in the scratch;
 * V60 indices 0..5 (precondition of weapon_tally's frame). */
uint64_t es2_combat_rating_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t vehicle,const uint32_t entry[3]);
uint64_t es2_condition_record_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t vehicle,uint32_t out,
    const uint32_t entry[3]);
uint64_t es2_device_state_registers(const es2_guest_memory *,uint32_t vehicle);
uint64_t es2_device_word_registers(const es2_guest_memory *,uint32_t vehicle);
uint64_t es2_ready_tone_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t vehicle,const uint32_t entry[3]);
uint64_t es2_weapon_tally_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t group,uint32_t out,
    const uint32_t entry[3]);
uint64_t es2_vehicle_update_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t vehicle,const uint32_t entry[3]);
/* The weapon group's slot-0 bodies 411a1d (table 499338, index -1)
 * and 41194a (table 499238, index = s8 byte G+1Dh), cdecl (group, value
 * word, owner): group_fire 4107e4 through the bridge, its EDX:EAX. No
 * scratch. */
uint64_t es2_group_fire_any_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t group,uint32_t value,
    uint32_t owner,const uint32_t entry[3]);
uint64_t es2_group_fire_sel_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t group,uint32_t value,
    uint32_t owner);
#endif
