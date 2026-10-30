#ifndef ES2_CORE_EFFECT_POOLS_H
#define ES2_CORE_EFFECT_POOLS_H
#include "es2/core/weapons.h"
/* The effect pools with resources (docs/reverse-engineering/
 * mission_load.md, section "effect pools with resources"): the
 * stage-2 callbacks of the explosions, debris, meteors and fires, the
 * smoke-ball set-up of smoke_pools, the flat-object pool of the world
 * loader, and the debris set and debris record readers that the type loads
 * call.
 *
 * The functions with a runtime record set it up through 0x49630c with the
 * synthetic EBP f = g->scratch +._FRAME (the bridge passes f as EBP for that
 * call; g->scratch needs._SCRATCH zeroed bytes, the locals lie at the
 * original's offsets below f) and return the record's saved FS:0 head in EAX,
 * or in ECX for debris_pools and debris_set (their epilogues load it into ECX);
 * the caller writes it back to FS:0 as the original's epilogue does. Every game
 * call goes through g->call with the original's EAX/EDX/ECX and is reported to
 * g->observe first (reference VA, slot 0; the stream's read method by its slot
 * 18h), so callees that are hooks keep their own modes. Each returns the
 * original's EDX:EAX and, when `ecx` is not NULL, stores the original's ECX
 * there. entry[] = EAX/EDX/ECX at entry. */
#define ES2_EXPLOSION_POOLS_SCRATCH 0x70u
#define ES2_EXPLOSION_POOLS_FRAME 0x6cu
#define ES2_DEBRIS_POOLS_SCRATCH 0x2cu
#define ES2_DEBRIS_POOLS_FRAME 0x28u
#define ES2_DEBRIS_SET_SCRATCH 0x54u
#define ES2_DEBRIS_SET_FRAME 0x50u
#define ES2_EFFECT_POOL_SCRATCH 0x2cu   /* flat_pools, meteor_pools, smoke_balls */
#define ES2_EFFECT_POOL_FRAME 0x28u
#define ES2_FIRE_POOLS_SCRATCH 0x78u
#define ES2_FIRE_POOLS_FRAME 0x74u

/* explosion_pools 0x407b54 (stage-2 record 5; record f-58h, context word
 * f-48h, stream f-38h, count word f-5Ah, name buffer f-6Ch): the pool
 * (40 x 5Bh) at 4a961eh, the list (30) at 4a9622h, 15 bitmap pairs at
 * 4a9638h, 13 or 15 explosion bitmaps (low-memory word 4d2680h), the shapes at
 * 4a9630h, the shape map at 4a963ch, the records (28h bytes, count word
 * 4a962ch) at 4a9628h, the count word 4a9634h and +26h of every shape. */
uint64_t es2_explosion_pools_registers(const es2_guest_memory *,const es2_weapon_graph *,const uint32_t entry[3],
    uint32_t *ecx);
/* debris_pools 0x408860 (stage-2 record 6; record f-28h): the pool
 * (150 x 61h) at 4a9642h, the list (40) at 4a9646h, the default debris set
 * at 4a9650h, 4a964ch = 0 and the words 4a9664h..4a9668h. ECX = the head. */
uint64_t es2_debris_pools_registers(const es2_guest_memory *,const es2_weapon_graph *,const uint32_t entry[3],
    uint32_t *ecx);
/* debris_set 0x40874c (set, name, tag): the shapes into set+6 (count word
 * set+0Ah), the record count word set+4, the 10-byte records at [set] (one
 * debris_record each), +26h of every shape = tag when tag is nonzero;
 * record f-50h, context word f-40h, stream f-30h. ECX = the head. */
uint64_t es2_debris_set_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t set,uint32_t name,
    uint32_t tag,const uint32_t entry[3],uint32_t *ecx);
/* debris_record 0x4083f8 (record, stream): words +0 and +2 (the piece
 * count), the 0Eh-byte pieces at +4 (seven words each, +8 and +0Ah scaled
 * by 182 unless FFFFh), the sum of their words +2 at +8; AX = the largest
 * word +0 (at least 0); ECX = the two local words (maximum:counter). */
uint64_t es2_debris_record_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t record,uint32_t stream,
    const uint32_t entry[3],uint32_t *ecx);
/* flat_pools 0x4097a8 (flag word): the flat shapes (flat or flat2) at
 * 4a9698h (count word 4a969ch), their bitmap pair at 4a969eh, the pool
 * (150 x 41h) at 4a968dh, the list (25) at 4a9691h; the guard byte
 * 498098h. */
uint64_t es2_flat_pools_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t flag,
    const uint32_t entry[3],uint32_t *ecx);
/* meteor_pools 0x409a34 (stage-2 record 9): the pool (10 x 59h) at
 * 4a96aah, the list (25) at 4a96aeh, the shapes at 4a96b4h, the bitmap at
 * 4a96b8h. */
uint64_t es2_meteor_pools_registers(const es2_guest_memory *,const es2_weapon_graph *,const uint32_t entry[3],
    uint32_t *ecx);
/* smoke_balls 0x408fb8 (called by smoke_pools): the pool (200 x 4Ch) at
 * 4a966eh, the list (25) at 4a9672h, the shapes at 4a9678h, the bitmap at
 * 4a967ch. */
uint64_t es2_smoke_balls_registers(const es2_guest_memory *,const es2_weapon_graph *,const uint32_t entry[3],
    uint32_t *ecx);
/* fire_pools 0x46b1a8 (stage-2 record 55; record f-58h, context word
 * f-48h, stream f-38h, names f-60h and f-74h): the pool (10 x 5Bh) at
 * 6b4f30h, the list (10) at 6b4f34h, the shapes at 6b4f28h, the bitmap
 * pairs at 6b4f2ch, the per-shape bytes, 6b4f38h = 0. */
uint64_t es2_fire_pools_registers(const es2_guest_memory *,const es2_weapon_graph *,const uint32_t entry[3],
    uint32_t *ecx);
#endif
