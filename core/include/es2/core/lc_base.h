#ifndef ES2_CORE_LC_BASE_H
#define ES2_CORE_LC_BASE_H
#include "es2/core/ai_behaviour.h"
/* The row-10 update of the building-pool class LC_BASE (vtable
 * 497784h slot +18h) with the projectile constructor and the weapon record
 * lookup it calls. Exact contracts: docs/reverse-engineering/vtables.md,
 * section "lc_base_update: contract".
 *
 *   lc_base_update     0x4045c8  three banks of a cannon (projectile) and two
 *                                lasers (immediate impact) per target
 *   projectile_create  0x40a9c4  a projectile object from the pool 4a96c2
 *                                (also called by turret, weapon_fire and
 *                                target_emit)
 *   weapon_record      0x40fe84  [4a98ech] + 58h * s16(i)
 *
 * Every game call goes through g->call with the EAX/EDX/ECX the original
 * holds there (the nested hooks keep their own modes; the constructor
 * 40a948, the runtime calls 49630c/49681a and the sound 4628e0 stay
 * original code). g->image is the image base. g->scratch is the frame:
 * ES2_*_SCRATCH guest bytes from the original's lowest local, so f =
 * g->scratch + ES2_*_SCRATCH is the original's EBP; the cores write every
 * frame local the originals write. Unless frame is NULL they also store,
 * before every game call, the EBX/ESI/EDI the original holds there and the
 * address after its CALL in *frame (an adapter that runs the call at the
 * original's stack coordinate loads them); the esi/edi or ebx/edi
 * arguments are the caller's registers, which the originals keep up to
 * their first own use.
 *
 * Results: the full EDX:EAX pair. projectile_create returns EDX = the
 * runtime record's saved FS:0 head (f-24h, written by 49630c), which the
 * caller's epilogue (the adapter) stores back into FS:0. ECX after the
 * hooks is outside the contract (vtables.md). Preconditions (the original
 * faults): pool_alloc returns an object in projectile_create. */
#define ES2_LC_BASE_UPDATE_SCRATCH 0xc0u
#define ES2_PROJECTILE_CREATE_SCRATCH 0x28u
/* weapon_record 40fe84 (i): EAX = table + 58h * s16(i), EDX = s16(i);
 * table = the dword [4a98ech]. */
uint64_t es2_weapon_record_registers(uint32_t table,uint32_t index);
/* projectile_create 40a9c4 (type, position, angles, owner, word). */
uint64_t es2_projectile_create_registers(const es2_guest_memory *,const es2_weapon_graph *,es2_ai_call_frame *frame,
    uint32_t ebx,uint32_t edi,uint32_t type,uint32_t position,uint32_t angles,uint32_t owner,uint32_t word,
    const uint32_t entry[3]);
/* lc_base_update 4045c8 (B). */
uint64_t es2_lc_base_update_registers(const es2_guest_memory *,const es2_weapon_graph *,es2_ai_call_frame *frame,
    uint32_t esi,uint32_t edi,uint32_t base,const uint32_t entry[3]);
#endif
