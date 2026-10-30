#ifndef ES2_CORE_FIT_LEAVES_H
#define ES2_CORE_FIT_LEAVES_H
#include "es2/core/weapons.h"
/* The remaining callees of the weapon fit weapon_fit 4175dc
 * (docs/reverse-engineering/mission_load.md, section "the remaining
 * weapon_fit callees"): the mount table of an actor and its entry lookup,
 * the weapon's shape word, the recursive state fill over a shape-node tree,
 * the equipment sort, the shield share split and the derived actor word
 * A+256h. Addresses are guest addresses; g->image is the loaded image base
 * (0x400000 in the original). Every game call goes through g->call with
 * the original's entry EAX/EDX/ECX, so a callee that is itself a hook keeps
 * its own mode; `entry` holds the entry EAX/EDX/ECX. Each function returns
 * the original's EDX:EAX and, when `ecx` is not NULL, stores the original's
 * ECX there (the adapters pass NULL: ECX is dead at every caller). */

/* weapon_shape_word 40e61c (weapon): AX = the first word of the weapon's
 * mount definition [W+18h]; EAX keeps the pointer's high word. EDX and ECX
 * unchanged. */
uint64_t es2_weapon_shape_word_registers(const es2_guest_memory *, uint32_t weapon, const uint32_t entry[3],
    uint32_t *ecx);
/* mount_entry 40fea0 (type): EAX = the 6-byte entry of the table [4a98e4]
 * for the type's low word (signed). EDX and ECX unchanged. */
uint64_t es2_mount_entry_registers(const es2_guest_memory *, const es2_weapon_graph *, uint32_t type,
    const uint32_t entry[3], uint32_t *ecx);
/* mount_table 420634 (actor): mount_entry(word [[A+1F2h]] + word [4a9d70]),
 * the sum in the low word of the entry ECX. EDX = [A+1F2h]; ECX = the
 * pushed dword. */
uint64_t es2_mount_table_registers(const es2_guest_memory *, const es2_weapon_graph *, uint32_t actor,
    const uint32_t entry[3], uint32_t *ecx);
/* shield_split 413ac8 (record, A+222h): word +0 = multiply_q10(sx +4,
 * sx +6), word +2 = word +6 minus the new word +0; EAX keeps multiply_q10's
 * high word, EDX and ECX are multiply_q10's. */
uint64_t es2_shield_split_registers(const es2_guest_memory *, const es2_weapon_graph *, uint32_t record,
    const uint32_t entry[3], uint32_t *ecx);
/* energy_rate 417d08 (actor): word A+256h = 20, or multiply_q10(200, 20)
 * when byte A+0ABh, else multiply_q10(600, 20) when byte A+0AAh, plus
 * multiply_q10(1024 - 204 x (c / 51), 20) when the weapon [A+313h]
 * exists and c = part_ratio(A+206h, sx([[W+18h]+17h]) + 13h) is below 225.
 * Registers as left by the last call (EAX 0 without a weapon). */
uint64_t es2_energy_rate_registers(const es2_guest_memory *, const es2_weapon_graph *, uint32_t actor,
    const uint32_t entry[3], uint32_t *ecx);
/* equip_slots 40fb2c (set, record): memset(record, 0, 20) through 49681a,
 * then each of the set's items [[set+4] + 4i] (word count [set+8], null
 * skipped) is filed by the weapon ID sx[[W+1Ch]+56h] into the record: 18
 * (ECM) -> +0, 29 (TARG) -> +4, 30 (SHIELD) -> +8, 32 (ENERGY) -> +0Ch, 31
 * (TURBO) -> +10h, any other ID not at all (the last item of an ID wins;
 * the equipment names are those of weapon_classes.md). The caller passes
 * record = actor+307h, so +0Ch is the dword A+313h that energy_rate reads.
 * EDX = 0, EAX = the last counter word over the last EAX; ECX = the final
 * counter word (the dword's high word is stack residue, reported as 0). */
uint64_t es2_equip_slots_registers(const es2_guest_memory *, const es2_weapon_graph *, uint32_t set,
    uint32_t record, const uint32_t entry[3], uint32_t *ecx);
/* node_state_fill 417530 (node, value): recursive over a shape-node tree.
 * The class id comes from the node's virtual slot 0. 140014h and 14000Bh
 * store the low word of value at node+4; 140015h fills the words
 * [node+1Ch][0 .. word [node+16h]) with it; 140007h, 140008h, 14000Bh,
 * 14000Ch and 140015h recurse into the children [node+12h] (word count
 * [node+10h]) in order with the full value dword, 140005h and 140006h and
 * every other class do nothing. The core recurses on the host stack in the
 * original's order. ECX = the node for class 140015h (else stack residue,
 * reported as 0). */
uint64_t es2_node_state_fill_registers(const es2_guest_memory *, const es2_weapon_graph *, uint32_t node,
    uint32_t value, const uint32_t entry[3], uint32_t *ecx);
#endif
