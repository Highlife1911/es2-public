#ifndef ES2_CORE_WEAPON_EJECT_H
#define ES2_CORE_WEAPON_EJECT_H
#include "es2/core/weapons.h"
/* The weapon eject 0x40f57c(W, A, flag) and the weapon damage slot
 * +68h of the equipment classes: the common body 0x40ee0c and the class
 * handlers 0x40ee38, 0x40ee90 and 0x40ef6c, each (W, A, x, d) with x the
 * weapon part's condition before and d after the damage
 * (docs/reverse-engineering/weapons.md, section "weapon eject").
 *
 * Every game call goes through g->call with the EAX/EDX/ECX the original
 * holds there (nested hooks keep their modes): for the eject the runtime
 * frame setup 0x49630c (synthetic EBP f = g->scratch +
 * ES2_WEAPON_EJECT_FRAME, record at f-24h, context word f-14h),
 * euler_matrix, the node lookup [[A+34h]+4].V20, matrix_compose,
 * mount_offset, 0x4801d4, A.V24, transform_point, aim_angles, pool_alloc,
 * debris_init, debris_heading and matrix_angles; for the slot bodies
 * 0x40ee0c, 0x40f57c, multiply_q10 and rng_next. g->image is the image
 * base; only the eject needs g->scratch (ES2_WEAPON_EJECT_SCRATCH bytes).
 *
 * Results: EDX:EAX exactly as the original leaves them. The eject also
 * stores the original's ECX at return (the record's saved FS:0 head,
 * dword f-24h) in *ecx; the caller restores FS:0 to it, as the epilogue
 * does. The slot bodies' ECX is not modelled (dead at their only caller
 * 0x417fdf). Precondition (the original faults): W is a valid weapon
 * (byte W+49h is read first). */
#define ES2_WEAPON_EJECT_SCRATCH 256u
#define ES2_WEAPON_EJECT_FRAME 208u
uint64_t es2_weapon_eject_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t weapon,uint32_t actor,uint32_t flag,const uint32_t entry[3],uint32_t *ecx);
/* 0x40ee0c: d = 100h and byte W+49h = 0 -> 0x40f57c(W, A, 0). */
uint64_t es2_weapon_damage_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t weapon,uint32_t actor,uint32_t before,uint32_t after,const uint32_t entry[3]);
/* 0x40ee38: 0x40ee0c, dword W+7Fh <- 14h - multiply_q10(14h, 100*((d-80h)/25)). */
uint64_t es2_beam_damage_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t weapon,uint32_t actor,uint32_t before,uint32_t after,const uint32_t entry[3]);
/* 0x40ee90: 0x40ee0c; with word W+7Bh set: type 0 draws the eject chance
 * (rng_next, stream A) once per 25-point step from x to d; type 2 sets
 * word W+63h <- 400h - 102*((d-80h)/25). */
uint64_t es2_gun_damage_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t weapon,uint32_t actor,uint32_t before,uint32_t after,const uint32_t entry[3]);
/* 0x40ef6c: 0x40ee0c, word W+7Fh <- d. */
uint64_t es2_targ_damage_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t weapon,uint32_t actor,uint32_t before,uint32_t after,const uint32_t entry[3]);
#endif
