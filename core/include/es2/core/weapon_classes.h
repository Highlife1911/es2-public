#ifndef ES2_CORE_WEAPON_CLASSES_H
#define ES2_CORE_WEAPON_CLASSES_H
#include "es2/core/weapons.h"
/* The weapon-class methods of the slots +2Ch ... +60h
 * (docs/reverse-engineering/weapon_classes.md, section "the
 * weapon-class methods"). W is the weapon (the cdecl `this`), entry[] the
 * EAX/EDX/ECX at entry. Every function returns the original's EDX:EAX and
 * stores the original's ECX in *ecx.
 *
 * Bodies with identical bytes share one function: weapon_zero for 40ecd4
 * (equip_ready), 40e678 (equip_trigger), 4111e2 (weapon_priority) and
 * 40df00 (weapon_gauge_match); weapon_one for 40f8a4 (weapon_spent) and
 * 4111e9 (weapon_stock); gauge_match for 40df08 and 40df1c.
 *
 * beam_ready's EDX high word is the caller's EBX high word on one path
 * (ebx_high, the high word of that EBX; the adapter passes 0).
 *
 * The step bodies and laew_trigger make game calls through g->call with the
 * original's arguments and entry EAX/EDX/ECX (observer key: the callee's
 * original VA): weapon_step 0x40ef94 (beam_step, turbo_step),
 * timer_word_step 0x467a48 and beam_step 0x40f00c (elf_step), the button
 * latch 0x45b734 (laew_trigger). gun_ammo computes its direct call of
 * 0x40e62c itself (no observer call). */
uint64_t es2_weapon_zero_registers(const uint32_t entry[3],uint32_t *ecx);
uint64_t es2_weapon_one_registers(const uint32_t entry[3],uint32_t *ecx);
uint64_t es2_weapon_trigger_registers(const es2_guest_memory *,uint32_t weapon,uint32_t record,
    const uint32_t entry[3],uint32_t *ecx);
uint64_t es2_gauge_match_registers(const es2_guest_memory *,uint32_t weapon,uint32_t widget,
    const uint32_t entry[3],uint32_t *ecx);
uint64_t es2_beam_spent_registers(const es2_guest_memory *,uint32_t weapon,const uint32_t entry[3],uint32_t *ecx);
uint64_t es2_gun_spent_registers(const es2_guest_memory *,uint32_t weapon,const uint32_t entry[3],uint32_t *ecx);
uint64_t es2_gun_stock_registers(const es2_guest_memory *,uint32_t weapon,const uint32_t entry[3],uint32_t *ecx);
uint64_t es2_beam_priority_registers(const es2_guest_memory *,uint32_t weapon,const uint32_t entry[3],uint32_t *ecx);
uint64_t es2_gun_clear_registers(const es2_guest_memory *,uint32_t weapon,const uint32_t entry[3],uint32_t *ecx);
uint64_t es2_beam_fill_registers(const es2_guest_memory *,uint32_t weapon,const uint32_t entry[3],uint32_t *ecx);
uint64_t es2_beam_preset_registers(const es2_guest_memory *,uint32_t weapon,const uint32_t entry[3],uint32_t *ecx);
uint64_t es2_beam_drain_registers(const es2_guest_memory *,uint32_t weapon,const uint32_t entry[3],uint32_t *ecx);
uint64_t es2_beam_clear_registers(const es2_guest_memory *,uint32_t weapon,const uint32_t entry[3],uint32_t *ecx);
uint64_t es2_weapon_ammo_registers(const es2_guest_memory *,uint32_t weapon,uint32_t out,
    const uint32_t entry[3],uint32_t *ecx);
uint64_t es2_gun_ammo_registers(const es2_guest_memory *,uint32_t weapon,uint32_t out,
    const uint32_t entry[3],uint32_t *ecx);
uint64_t es2_gun_ready_registers(const es2_guest_memory *,uint32_t weapon,const uint32_t entry[3],uint32_t *ecx);
uint64_t es2_beam_ready_registers(const es2_guest_memory *,uint32_t weapon,const uint32_t entry[3],
    uint32_t ebx_high,uint32_t *ecx);
uint64_t es2_elf_ready_registers(const es2_guest_memory *,uint32_t weapon,const uint32_t entry[3],uint32_t *ecx);
uint64_t es2_elf_trigger_registers(const es2_guest_memory *,uint32_t weapon,uint32_t record,
    const uint32_t entry[3],uint32_t *ecx);
uint64_t es2_beam_adjust_registers(const es2_guest_memory *,uint32_t weapon,uint32_t delta,
    const uint32_t entry[3],uint32_t *ecx);
uint64_t es2_laew_trigger_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t weapon,
    uint32_t record,const uint32_t entry[3],uint32_t *ecx);
uint64_t es2_beam_step_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t weapon,
    uint32_t amount,uint32_t flag,const uint32_t entry[3],uint32_t *ecx);
uint64_t es2_turbo_step_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t weapon,
    uint32_t amount,uint32_t flag,const uint32_t entry[3],uint32_t *ecx);
uint64_t es2_elf_step_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t weapon,
    uint32_t amount,uint32_t flag,const uint32_t entry[3],uint32_t *ecx);

/* The weapon constructors that weapon_factory 0x40fff8 calls
 * (weapon_classes.md, section "the weapon constructors"). W the
 * object, A the vehicle, M the mount record, C the definition record, P the
 * projectile record. Game calls go through g->call with the original's
 * arguments and entry EAX/EDX/ECX (observer key: the callee's original VA):
 * weapon_ctor calls 0x40fab0, shape_word_block 0x402fc0, object_track
 * 0x402374 (at most twice), mount_offset 0x40f540, 0x40dd4c and memset
 * 0x49681a; beam_ctor calls weapon_ctor 0x40df30 and multiply_q10 0x47e0b0;
 * gun_ctor and equip_ctor call weapon_ctor; the five equipment classes call
 * equip_ctor 0x40e234. "count +1" is [[6CBEE4h]] += 1. Each returns the
 * original's EDX:EAX (EAX = W) and stores its ECX in *ecx. */
/* weapon_ctor 40df30: the WEAPON base. EDX = the counter pointer, ECX =
 * the last memset's. */
uint64_t es2_weapon_ctor_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t W,
    uint32_t A,uint32_t M,uint32_t C,uint32_t P,const uint32_t entry[3],uint32_t *ecx);
/* beam_ctor 40e074 (BEAM 498DCCh): EDX = 0, ECX = the counter pointer.
 * gun_ctor 40e140 (GUN 498D54h): EDX = sx word [[W+1Ch]+3Ah] << 8, ECX = 0. */
uint64_t es2_beam_ctor_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t W,
    uint32_t A,uint32_t M,uint32_t C,uint32_t P,const uint32_t entry[3],uint32_t *ecx);
uint64_t es2_gun_ctor_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t W,
    uint32_t A,uint32_t M,uint32_t C,uint32_t P,const uint32_t entry[3],uint32_t *ecx);
/* equip_ctor 40e234 (EQUIP 498CDCh): weapon_ctor(W, A, M, C, 0), byte W+4Ch
 * = 0, word W+77h = FFFFh. EDX = the counter pointer, ECX = weapon_ctor's. */
uint64_t es2_equip_ctor_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t W,
    uint32_t A,uint32_t M,uint32_t C,const uint32_t entry[3],uint32_t *ecx);
/* ecm_ctor 40e274, turbo_ctor 40e2bc: EDX = 0, ECX = the counter pointer;
 * targ_ctor 40e308, shield_ctor 40e344, energy_ctor 40e380: EDX = the
 * counter pointer, ECX = equip_ctor's. */
uint64_t es2_ecm_ctor_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t W,
    uint32_t A,uint32_t M,uint32_t C,const uint32_t entry[3],uint32_t *ecx);
uint64_t es2_turbo_ctor_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t W,
    uint32_t A,uint32_t M,uint32_t C,const uint32_t entry[3],uint32_t *ecx);
uint64_t es2_targ_ctor_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t W,
    uint32_t A,uint32_t M,uint32_t C,const uint32_t entry[3],uint32_t *ecx);
uint64_t es2_shield_ctor_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t W,
    uint32_t A,uint32_t M,uint32_t C,const uint32_t entry[3],uint32_t *ecx);
uint64_t es2_energy_ctor_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t W,
    uint32_t A,uint32_t M,uint32_t C,const uint32_t entry[3],uint32_t *ecx);

/* The gauge syncs of the slots +50h and the LAEW emission of slot
 * +28h (weapon_classes.md, section "the weapon gauge syncs, the LAEW emission
 * and the empty bodies"). W the weapon; b1, b2, b3 the low bytes of the three
 * arguments after W (laew_sync: the low words, its high-word residue reaches
 * the getter call's EDX and ECX). The graph's scratch is the original's local
 * record (guest addressable, 24 bytes); the widget slots +18h/+14h/+10h are
 * called through g->call at the addresses read from the widget's table. Game
 * calls (observer key: the callee's VA): cockpit message 0x420a68 (ecm_sync),
 * sfx_play 0x462830 (turbo_sync), the gauge record getters 0x440e9c (beam_sync)
 * and 0x4414cc (gun_sync), movement_step 0x467924 (gun_sync), weapon_prepare
 * 0x40e788 and the emission 0x40acac (laew_fire). laew_sync computes its call
 * of gun_sync in core. Each returns the original's EDX:EAX and stores its ECX
 * in *ecx. */
uint64_t es2_equip_sync_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t weapon,
    const uint32_t entry[3],uint32_t *ecx);
uint64_t es2_ecm_sync_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t weapon,
    const uint32_t entry[3],uint32_t *ecx);
uint64_t es2_turbo_sync_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t weapon,
    const uint32_t entry[3],uint32_t *ecx);
uint64_t es2_beam_sync_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t weapon,
    uint32_t b1,uint32_t b2,uint32_t b3,const uint32_t entry[3],uint32_t *ecx);
uint64_t es2_gun_sync_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t weapon,
    uint32_t b1,uint32_t b2,uint32_t b3,const uint32_t entry[3],uint32_t *ecx);
uint64_t es2_laew_sync_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t weapon,
    uint32_t w1,uint32_t w2,uint32_t w3,const uint32_t entry[3],uint32_t *ecx);
uint64_t es2_laew_fire_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t weapon,
    uint32_t owner,uint32_t free_word,const uint32_t entry[3],uint32_t *ecx);
#endif
