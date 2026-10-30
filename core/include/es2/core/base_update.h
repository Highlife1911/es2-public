#ifndef ES2_CORE_BASE_UPDATE_H
#define ES2_CORE_BASE_UPDATE_H
#include "es2/core/weapons.h"
/* The row-10 update of the building-pool classes BASE and
 * RADAR_BASE (vtable slot +18h) and its part-destruction subtree. Exact
 * contracts: docs/reverse-engineering/vtables.md, section "base_update:
 * contract".
 *
 *   base_update      0x403ca8  slot +18h of BASE/RADAR_BASE (also called by
 *                              turret, convoy_think and the LC_BASE update)
 *   base_parts       0x403914  part-destruction step (the pure helper
 *                              0x403668 "all fire parts destroyed" is
 *                              computed inside)
 *   base_part_chain  0x403890  recursive: damages the dependent parts
 *                              through slot +74h
 *   base_part_blast  0x4036c0  explosion of a destroyed part
 *   base_part_debris 0x40379c  debris of a destroyed part
 *
 * Every game call goes through g->call with the EAX/EDX/ECX the original
 * holds there, including the nested group functions, the runtime frame
 * setup 0x49630c (synthetic EBP f = g->scratch + ES2_BASE_FRAME), the FIRE
 * creation 0x46b430/0x46b48c, the debris calls 0x4089bc/0x408530, the
 * point transform 0x4801d4 and the hooked leaves (timer_word_step,
 * actor_animate, rng_bounded, euler_matrix, transform_point, pool_alloc,
 * explosion_create, slot +74h). g->image is the image base. Frame locals
 * that the originals pass by pointer live below f; base_parts,
 * base_part_blast and base_part_debris need ES2_BASE_SCRATCH guest bytes
 * of scratch, base_update and base_part_chain none.
 *
 * Results: the full EDX:EAX pair. base_parts returns EAX = the record's
 * saved FS:0 head (dword f-2Ch, written by 0x49630c), base_part_blast
 * keeps it in ECX (f-24h); the adapters restore FS:0 to it. ECX after the
 * hooks is dead at every caller (vtables.md). Preconditions (the original
 * faults): a nonzero animation count on base_update's frame step and
 * nonzero puff ranges (2 * word mod 10000h) for rng_bounded. */
#define ES2_BASE_SCRATCH 256u
#define ES2_BASE_FRAME 208u
uint64_t es2_base_update_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t object,const uint32_t entry[3]);
uint64_t es2_base_parts_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t object,const uint32_t entry[3]);
uint64_t es2_base_part_chain_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t object,uint32_t part,const uint32_t entry[3]);
uint64_t es2_base_part_blast_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t object,uint32_t runtime,uint32_t kind,uint32_t offset,const uint32_t entry[3]);
uint64_t es2_base_part_debris_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t object,uint32_t config,const uint32_t entry[3]);
/* 0x403668 (pure): EAX = 1 when every part whose fire entry (config word
 * +6) is > -1 has its runtime word +0 >= its config word +0, else 0; EDX =
 * the part index where the scan stopped; ECX = the part count (signed
 * word +12h of the configuration). */
void es2_base_parts_done(const es2_guest_memory *,uint32_t object,es2_weapon_registers *out);
#endif
