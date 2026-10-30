#ifndef ES2_CORE_VEHICLE_H
#define ES2_CORE_VEHICLE_H
#include "es2/core/weapons.h"
/* Vehicle leaves; exact contracts in
 * docs/reverse-engineering/vehicle.md. Game calls go through the graph's
 * guest bridge with the entry registers the original holds at each call,
 * so hooked callees are entered as hooks. Every function returns the
 * original's full EDX:EAX.
 *
 * condition_table 4151a4 (parts, out): 41 Q8 condition words of the part
 * groups and parts. Pure. A zero group base or loop-2 divisor faults like
 * the original (precondition).
 *
 * condition_ratio 4152ec (parts, kind word, index word): one Q8 value. Pure.
 * For kinds outside 0..2 the original dereferences its caller's ECX and EBX:
 * entry[2] is ECX, entry[0] carries EBX (the adapter's entry shim moves it into
 * EAX, which 4152ec never reads).
 *
 * sight_check 412608 (viewer, target): terrain line of sight with the
 * re-check timer; byte viewer+132h+slot. Needs 48 scratch bytes.
 *
 * group_state 412c8c (group): condition level 0..4. A zero member count faults
 * like the original (precondition). No scratch.
 *
 * target_bearing 41e758 (actor, out, target or 0): bearing record. No
 * scratch.
 *
 * part_position 41b60c (vehicle, part word, out): vehicle virtual +58h.
 * Needs 32 scratch bytes (the transformed offset vector). */
uint64_t es2_condition_table_registers(const es2_guest_memory *,uint32_t parts,uint32_t out);
uint64_t es2_condition_ratio_registers(const es2_guest_memory *,uint32_t parts,uint32_t kind,uint32_t index,
    const uint32_t entry[3]);
uint64_t es2_sight_check_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t viewer,uint32_t target,
    const uint32_t entry[3]);
uint64_t es2_group_state_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t group,const uint32_t entry[3]);
uint64_t es2_target_bearing_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t actor,uint32_t out,
    uint32_t target,const uint32_t entry[3]);
uint64_t es2_part_position_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t vehicle,uint32_t part,
    uint32_t out,const uint32_t entry[3]);
/* The vehicle aux leaves, same conventions:
 *
 * shield_max_set 413ab8 (shield record, value word): stores word +6. Pure.
 *
 * shield_capacity 417bec (vehicle): shield maximum from the configuration
 * and two part conditions (multiply_q10, part_ratio, shield_max_set). A
 * zero maximum of part 4 with a value >= 0 faults like the original. No
 * scratch.
 *
 * damage_score 41537c (image base, parts): weighted damage sum; reads the
 * weight words in the image. Pure. A zero base of a counted group faults
 * like the original.
 *
 * facing_part 41b534 (vehicle, pose): vehicle virtual +54h. Needs 24
 * scratch bytes (the frame).
 *
 * vehicle_keys 4157c8 (object, code): vehicle virtual +2Ch. entry[0] is the
 * caller's ESI (entry shim); the function stores in *esi the ESI that the
 * guest bridge must load for each game call. Needs 60 scratch bytes. */
#define ES2_FACING_PART_SCRATCH 0x18u
#define ES2_VEHICLE_KEYS_SCRATCH 0x3cu
uint64_t es2_shield_max_set_registers(const es2_guest_memory *,uint32_t shield,uint32_t value,const uint32_t entry[3]);
uint64_t es2_shield_capacity_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t vehicle,
    const uint32_t entry[3]);
uint64_t es2_damage_score_registers(const es2_guest_memory *,uint32_t image,uint32_t parts,const uint32_t entry[3]);
uint64_t es2_facing_part_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t vehicle,uint32_t pose,
    const uint32_t entry[3]);
uint64_t es2_vehicle_keys_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t object,uint32_t code,
    const uint32_t entry[3],uint32_t *esi);
/* Part and actor helpers, same conventions:
 *
 * part_list_dead 40cf10 (group record, part values): 1 in AX if every
 * listed part value is negative. Pure.
 * part_group_dead 40d9f8 (parts P, index word): 0 if the group value is
 * not negative, else 40cf10 through the bridge. No scratch.
 * part_group_max 40dc58 (parts P, index word): the group's base maximum
 * plus its parts' maxima. Pure.
 * mission_guard 4240f4 (no arguments): a predicate over the words
 * 4a9e52/4a9e5a/4a9e5c of the image; EDX is the entry EDX. Pure.
 * actor_offsets 411a74 (object, first, second): ACTOR/FLYER slot +30h,
 * clears the two 6-byte records through memset 49681a (bridge). */
uint64_t es2_part_list_dead_registers(const es2_guest_memory *,uint32_t record,uint32_t values);
uint64_t es2_part_group_dead_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t parts,uint32_t index);
uint64_t es2_part_group_max_registers(const es2_guest_memory *,uint32_t parts,uint32_t index);
uint64_t es2_mission_guard_registers(const es2_guest_memory *,uint32_t image,const uint32_t entry[3]);
uint64_t es2_actor_offsets_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t first,uint32_t second,
    const uint32_t entry[3]);
#endif
