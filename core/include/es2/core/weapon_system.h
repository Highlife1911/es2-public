#ifndef ES2_CORE_WEAPON_SYSTEM_H
#define ES2_CORE_WEAPON_SYSTEM_H
#include "es2/core/weapons.h"
/* Weapon-system routines below the weapon keys; contracts in
 * docs/reverse-engineering/weapons.md, section "weapon-system routines below
 * the weapon keys". W is the weapon system: list [W+4], count word W+8, fire
 * group byte W+1Ch, selected slot s8 W+1Dh, group records (table pointer, count
 * word) at W+1Fh+6g. Every game call (410708, 41074c, 410cd0, 4610ad, 441f90
 * and the weapon virtuals +3Ch/+48h) goes through the graph's guest bridge with
 * the EAX/EDX/ECX the original holds at that call; g->image is the loaded
 * image base.
 *
 * select_step 41074c (W, d): steps the selection by the word d to the next
 * ready member of the current group, then 410708. esi is the caller's ESI
 * (its high word reaches the index on the "below 0" path, entry shim);
 * entry[0] is the entry EAX (its high 24 bits are returned when nothing is
 * selected).
 *
 * fire_group 410ae4 (W, g): sets the fire group, steps the selection when
 * the selected weapon is not a member, and stores g in the HUD (4610ad,
 * 441f90). *esi receives the ESI the original holds at each of its game
 * calls before the call is made (41074c reads its high word); the graph's
 * call loads it.
 *
 * weapons_v3c 410d04 (W): the weapon virtual +3Ch on every listed weapon.
 * entry[2] is the entry ECX (its high word reaches the first call).
 *
 * group_toggle 4110ac (W, n): toggles the first weapon numbered n in the
 * current group (410cd0), then V48 and, when V48's AX is 0, V3C. Returns
 * EAX = 1 (with the last call's high word) or 0. group_toggle_ecx is the
 * original's ECX at return (the found weapon or n), for the entry shim. */
uint64_t es2_select_step_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t weapons,uint32_t step,
    uint32_t esi,const uint32_t entry[3]);
uint64_t es2_fire_group_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t weapons,uint32_t group,
    const uint32_t entry[3],uint32_t *esi);
uint64_t es2_weapons_v3c_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t weapons,
    const uint32_t entry[3]);
uint64_t es2_group_toggle_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t weapons,uint32_t number,
    const uint32_t entry[3]);
uint32_t es2_group_toggle_ecx(const es2_guest_memory *,uint32_t weapons,uint32_t number);
#endif
