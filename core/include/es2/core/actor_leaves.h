#ifndef ES2_CORE_ACTOR_LEAVES_H
#define ES2_CORE_ACTOR_LEAVES_H
#include "es2/core/weapons.h"
/* The actor slot leaves (docs/reverse-engineering/vtables.md,
 * section "actor slot leaves"): the one- to thirty-instruction
 * virtual methods of the ACTOR, BASE, RADAR_BASE, GUN_BASE, VEHICLE,
 * LC_BASE, MECH and FLYER tables. Each
 * function is reached only through its vtable slots; a caller cannot
 * compute it without knowing the receiver's class.
 *
 * Every function returns EDX:EAX exactly as the original leaves them;
 * entry[] = EAX/EDX/ECX at entry; *ecx receives the original's ECX at
 * return. O is the object (first stack argument), C its configuration
 * [O+1F2h], lo(R, w) a 16-bit load into R that keeps R's high word, s16
 * sign extension of a word. */

/* Constant leaves. EAX = 0 (XOR EAX,EAX): base_decide 0x406cfe (BASE +1C),
 * actor_armour 0x411a4c (+34), actor_target_scale 0x411a54 (+38),
 * actor_angle 0x411a5c (+3C), actor_order 0x411a64 (+28), actor_keys
 * 0x411a6c (+2C), actor_aim_node 0x411a9c (+24), actor_radius 0x411aa4
 * (+5C), actor_sight_radius 0x411aac (+7C), object_zero 0x4786cb (+0C of
 * DRAWABLE and every derived table) and flyer_keys 0x4215c0 (FLYER +2C;
 * its 16-bit load of the second argument is overwritten by the XOR).
 * EAX = lo(entry EAX, 1) (MOV AX,1): base_in_front 0x405308 (BASE +44),
 * actor_move 0x411ab4 (+14), actor_engage_roll 0x411ac0 (+4C),
 * actor_mount_word 0x411b04 (+6C). EAX = 1 (MOV EAX,1): actor_part_word
 * 0x411b10 (+84). EDX and ECX are the entry values in all of them. */
uint64_t es2_base_decide_registers(const uint32_t entry[3],uint32_t *ecx);
uint64_t es2_actor_armour_registers(const uint32_t entry[3],uint32_t *ecx);
uint64_t es2_actor_target_scale_registers(const uint32_t entry[3],uint32_t *ecx);
uint64_t es2_actor_angle_registers(const uint32_t entry[3],uint32_t *ecx);
uint64_t es2_actor_order_registers(const uint32_t entry[3],uint32_t *ecx);
uint64_t es2_actor_keys_registers(const uint32_t entry[3],uint32_t *ecx);
uint64_t es2_actor_aim_node_registers(const uint32_t entry[3],uint32_t *ecx);
uint64_t es2_actor_radius_registers(const uint32_t entry[3],uint32_t *ecx);
uint64_t es2_actor_sight_radius_registers(const uint32_t entry[3],uint32_t *ecx);
uint64_t es2_object_zero_registers(const uint32_t entry[3],uint32_t *ecx);
uint64_t es2_flyer_keys_registers(const uint32_t entry[3],uint32_t *ecx);
uint64_t es2_base_in_front_registers(const uint32_t entry[3],uint32_t *ecx);
uint64_t es2_actor_move_registers(const uint32_t entry[3],uint32_t *ecx);
uint64_t es2_actor_engage_roll_registers(const uint32_t entry[3],uint32_t *ecx);
uint64_t es2_actor_mount_word_registers(const uint32_t entry[3],uint32_t *ecx);
uint64_t es2_actor_part_word_registers(const uint32_t entry[3],uint32_t *ecx);

/* Word getters. gun_angle 0x403594 (GUN_BASE, VEHICLE +3C): EAX =
 * lo(O, [O+20Fh]). mech_angle 0x415488 (MECH +3C): lo(O, [O+298h]).
 * base_radius 0x4035a4 (BASE family +5C): lo(C, [C+2Ah]). mech_radius
 * 0x415518 (MECH +5C) and mech_sight_radius 0x41552c (MECH +7C):
 * lo(C, [C+70h]). EDX, ECX = entry. */
uint64_t es2_gun_angle_registers(const es2_guest_memory *,uint32_t object,const uint32_t entry[3],uint32_t *ecx);
uint64_t es2_mech_angle_registers(const es2_guest_memory *,uint32_t object,const uint32_t entry[3],uint32_t *ecx);
uint64_t es2_base_radius_registers(const es2_guest_memory *,uint32_t object,const uint32_t entry[3],uint32_t *ecx);
uint64_t es2_mech_radius_registers(const es2_guest_memory *,uint32_t object,const uint32_t entry[3],uint32_t *ecx);
uint64_t es2_mech_sight_radius_registers(const es2_guest_memory *,uint32_t object,const uint32_t entry[3],uint32_t *ecx);

/* base_sight_radius 0x4035b8 (BASE family +7C): [C+6] = 0 gives EAX 0,
 * EDX C, ECX entry. Otherwise ECX = C and, when [C+4] = FFFFh, or s16
 * [[O+205h]+5] >= 2, or the byte [O+99h] is 0, EAX = lo(O, [C+2Ah]) with
 * EDX 0; else EAX 0 with EDX 1. */
uint64_t es2_base_sight_radius_registers(const es2_guest_memory *,uint32_t object,const uint32_t entry[3],uint32_t *ecx);

/* Part-table words. base_part_word 0x40360c (BASE family +84, O, i):
 * EAX = ECX = s16 [[O+201h] + 2i], EDX = i. mech_part_word 0x415540 (MECH
 * +84, O, i): EAX = s16 [[O+20Eh] + 2i], EDX = i, ECX entry. The index is
 * the full dword (32-bit address arithmetic). mech_mount_word 0x4155ac
 * (MECH +6C, O, w): EDX = s16(w), EAX = lo(P, [P + 2 s16(w) + 0Ah]) with
 * P = [O+202h], ECX entry. */
uint64_t es2_base_part_word_registers(const es2_guest_memory *,uint32_t object,uint32_t index,const uint32_t entry[3],uint32_t *ecx);
uint64_t es2_mech_part_word_registers(const es2_guest_memory *,uint32_t object,uint32_t index,const uint32_t entry[3],uint32_t *ecx);
uint64_t es2_mech_mount_word_registers(const es2_guest_memory *,uint32_t object,uint32_t word,const uint32_t entry[3],uint32_t *ecx);

/* Next living part (O, i, out). base_next_part 0x403624 (BASE family
 * +80): from s = max(i, 0), k runs s+1, s+2, ... wrapping to 0 when it
 * reaches n = s16 [C+12h] (C reread every step), and stops at the first k
 * with a nonzero word [[O+201h] + 2k] or when k returns to s (then -1).
 * [out] = EAX = k or -1; EDX = out, ECX = s. mech_next_part 0x415558 (MECH
 * +80): the same walk over seven slots (wrap at 7) whose part words are
 * [[O+20Eh] + 2 s16 T[k]], T the word table 0x49a060; EAX = k or -1,
 * [out] = EDX = s16 T[k] or -1, ECX = out. actor_next_part 0x411b1c
 * (ACTOR +80): [out] = EAX = EDX = -1, ECX entry. The walks are not
 * bounded: a start at or beyond the wrap value scans on until a nonzero
 * word, as the original does. */
uint64_t es2_base_next_part_registers(const es2_guest_memory *,uint32_t object,uint32_t index,uint32_t out,
    const uint32_t entry[3],uint32_t *ecx);
uint64_t es2_mech_next_part_registers(const es2_guest_memory *,uint32_t image,uint32_t object,uint32_t index,uint32_t out,
    const uint32_t entry[3],uint32_t *ecx);
uint64_t es2_actor_next_part_registers(const es2_guest_memory *,uint32_t out,const uint32_t entry[3],uint32_t *ecx);

/* actor_in_front 0x411acc (ACTOR, MECH, FLYER +44, O, a): DX = a - [O+10h]
 * + 3800h (16-bit, EDX keeps the entry high word); EAX = 1 when DX < 7000h
 * (unsigned), else 0. ECX entry. */
uint64_t es2_actor_in_front_registers(const es2_guest_memory *,uint32_t object,uint32_t angle,const uint32_t entry[3],uint32_t *ecx);

/* actor_mark 0x42200c (+68 of every actor table): byte [O+B1h] = 1; EAX =
 * O+92h; EDX, ECX entry. */
uint64_t es2_actor_mark_registers(const es2_guest_memory *,uint32_t object,const uint32_t entry[3],uint32_t *ecx);

/* Leaves with one game call (g->call with the original's arguments and
 * entry registers; image = the loaded image's base). mech_condition
 * 0x415504 (MECH +40) and flyer_condition 0x421e84 (FLYER +40):
 * part_condition 0x40db2c(O+206h resp. O+200h) with EAX = that pointer and
 * the entry EDX/ECX; its EDX:EAX is the result, ECX = the pointer (POP
 * ECX). flyer_target_scale 0x421584 (FLYER +38): multiply_q10
 * 0x47e0b0(1000, s16 [O+233h]) with the entry EAX/EDX/ECX; its EDX:EAX and
 * ECX are the result. */
uint64_t es2_mech_condition_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t object,
    const uint32_t entry[3],uint32_t *ecx);
uint64_t es2_flyer_condition_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t object,
    const uint32_t entry[3],uint32_t *ecx);
uint64_t es2_flyer_target_scale_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t object,
    const uint32_t entry[3],uint32_t *ecx);

/* Five bodies that need the prologue whitelist's final RET and 83 /1
 * (hook/README.md; vtables.md, "Actor slot leaves"):
 * actor_contact 0x4127f8, actor_part_position 0x411a3c, actor_kill 0x411b2c
 * and actor_link_position 0x411b34 are es2_actor_empty_registers,
 * actor_part_facing 0x411a44 is es2_actor_no_part_registers. The empty
 * bodies 0x411a3c (ACTOR/FLYER +58), 0x411b2c (+60), 0x411b34 (+64) and
 * 0x4127f8 (+48) change no register: es2_actor_empty_registers. 0x411a44
 * (ACTOR/FLYER +54: OR EAX,-1) returns EAX = -1: es2_actor_no_part_registers. */
uint64_t es2_actor_empty_registers(const uint32_t entry[3],uint32_t *ecx);
uint64_t es2_actor_no_part_registers(const uint32_t entry[3],uint32_t *ecx);

/* The default radial hit actor_radial_hit 0x411b3c(O, damage, point,
 * reach, source), slot +70h of the ACTOR table and of FLYER (the only class
 * that instantiates it; hit_tests.md, "FLYER part damage", table of slot
 * +74h callers).
 * Four game calls through g->call with the registers the original holds:
 * the virtual radius slot +5Ch(O); dist_gate 0x49288c(point, O+26h) (EAX =
 * point, EDX = s16(radius), ECX = O+26h); and, as the tail call that
 * returns its EDX:EAX and ECX, slot +74h(O, 0, q, source) with EAX = the
 * vtable (reread), EDX = the IDIV remainder, ECX = source.
 * q = (s16(reach) - (dist - s16(radius))) * s16(damage) / s16(reach):
 * the product keeps its low dword (IMUL, then CDQ), the division is
 * signed with the remainder. Precondition as in the original: s16(reach)
 * is nonzero and the quotient fits (the original faults otherwise).
 * *ecx = the tail callee's ECX; the adapter's entry shim zeroes the entry
 * ECX because FLYER's slot +74h (flyer_part) returns ECX = 0 on every path. */
uint64_t es2_actor_radial_hit_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t object,
    uint32_t damage,uint32_t point,uint32_t reach,uint32_t source,const uint32_t entry[3],uint32_t *ecx);
#endif
