#ifndef ES2_CORE_BUILDING_TYPES_H
#define ES2_CORE_BUILDING_TYPES_H
#include "es2/core/weapons.h"
/* The building module's (base.cpp) load-time functions
 * (docs/reverse-engineering/mission_load.md, section "building types and
 * pools"): its stage-2 callback and the building pools of the
 * world load.
 *
 * Both set up the runtime exception record through 0x49630c with the synthetic
 * EBP f = g->scratch +._FRAME (the bridge passes f as EBP for that call;
 * g->scratch needs._SCRATCH zeroed bytes) and return the record's saved FS:0
 * head in EAX, which the caller writes back to FS:0 as the original's epilogue
 * does. Every game call goes through g->call with the original's EAX/EDX/ECX
 * and is reported to g->observe first (reference VA, slot 0; the stream's read
 * method by its slot 18h), so callees that are hooks keep their own modes. Each
 * returns the original's EDX:EAX and, when `ecx` is not NULL, stores the
 * original's ECX there. entry[] = EAX/EDX/ECX at entry. */
#define ES2_BASE_TYPES_SCRATCH 0x124u
#define ES2_BASE_TYPES_FRAME 0x120u
#define ES2_BASE_POOLS_SCRATCH 0x2cu
#define ES2_BASE_POOLS_FRAME 0x28u

/* base_types 0x405fac (stage-2 record 3; record f-E0h, context word
 * f-D0h, streams at f-60h, f-90h, f-30h, f-C0h): `bforms` (dword count
 * 4a95a8h, 1Ch-byte forms from 0x49667c at 4a95ach: element array +4,
 * 8 bytes +0Ch, byte lists +18h), `lc_wpns` (count word 4a95b8h, 16h-byte
 * records at 4a95bch), `bases` (count word 4a9598h, 3Ch-byte types at
 * 4a959ch with 1Eh-byte children +14h and +38h = the type's 6-byte slot
 * in the new[] block 4a95b0h when word +30h is nonzero), `basecol`
 * (collision_read 0x40ccf8 per type into 4a95b0h + 6k), the count words
 * 4a9572h/4a9578h/4a9580h (maxima of the type words +2 and +4, plus one),
 * the hulk shapes `bhulks.dgs` (0x474cd8 into 4a9584h), the zeroed tables
 * 4a957ch/4a9574h, the textures `basetex` (4a958ch) and `vehtex`
 * (4a9594h) with their bitmaps released, +26h of each hulk = 4a9588h, the
 * debris set `base_deb` (0x40874c at 4a95c0h). Callees: 49630c, the path
 * builder 492bec, the stream (472f14, the read method at slot +18h,
 * stream_read 4735b8, stream_destroy 472f64), the error report 479c78,
 * 49667c, new[] 4774f0, collision_read 40ccf8, the resources (474cd8,
 * 45cdf4, 46a03c, 48349c) and 40874c. */
uint64_t es2_base_types_registers(const es2_guest_memory *,const es2_weapon_graph *,const uint32_t entry[3],
    uint32_t *ecx);
/* base_pools 0x405df4 (count): the building pool (new(12h), pool_create(p,
 * count, 26Dh)) at 4a95a0h and its removal list (new(8), new[](64h),
 * capacity 25) at 4a95a4h; record f-28h, context word f-18h, slots f-4,
 * f-8. `count` is the argument slot (its word is used); `ebx` is the
 * caller's EBX, whose high word the original pushes with the count word as
 * pool_create's capacity slot (pool_create reads only the word). */
uint64_t es2_base_pools_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t count,uint32_t ebx,
    const uint32_t entry[3],uint32_t *ecx);
#endif
