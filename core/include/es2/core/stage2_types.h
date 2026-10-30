#ifndef ES2_CORE_STAGE2_TYPES_H
#define ES2_CORE_STAGE2_TYPES_H
#include "es2/core/weapons.h"
/* The stage-2 callbacks of stage_dispatch(2) that load type data
 * (docs/reverse-engineering/mission_load.md, section "stage-2
 * callbacks with type data"), and the weapon-list entry reader they use.
 *
 * The six callbacks take no argument; each sets up the runtime exception
 * record through 0x49630c (descriptor at reference VA, record at f-50h,
 * context word f-40h) with the synthetic EBP f = g->scratch +
 * ES2_STAGE2_TYPES_FRAME (the bridge passes f as EBP for that call;
 * g->scratch needs ES2_STAGE2_TYPES_SCRATCH zeroed bytes), opens its file
 * into the stream object at f-30h (0x472f14 with the path from 0x492bec;
 * a nonzero status [f-2Ch] reports through 0x479c78), reads the records,
 * and returns the record's saved FS:0 head in EAX (weapon_types,
 * rocket_types, bullet_types) or EDX (beam_types, mech_forms, flyer_forms),
 * which the caller writes back to FS:0 as the original's epilogue does.
 * Every game call goes through g->call with the original's EAX/EDX/ECX and
 * is reported to g->observe first (reference VA, slot 0; the stream's read
 * method by its slot 18h), so callees that are hooks keep their own modes:
 * the runtime (0x49630c, new[] 0x4774f0, the array construction 0x49667c,
 * _memset 0x49681a), the path builder 0x492bec, the stream (0x472f14,
 * stream_read 0x4735b8, the read method, stream_close 0x47307c,
 * stream_open 0x4730e8, stream_destroy 0x472f64), the error report
 * 0x479c78, the resources (shapes 0x474cd8, bitmaps 0x45cdf4, their
 * descriptor 0x46a03c, the bitmap release 0x48349c), weapon_entry
 * 0x40f8bc and its readers dmg_region_read 0x40cff8 and collision_volume
 * 0x40cc14. Each function returns the original's EDX:EAX and, when `ecx`
 * is not NULL, stores the original's ECX there (the adapters pass NULL:
 * nothing reads ECX after the dispatcher's call or after 0x40fdc8).
 * entry[] = EAX/EDX/ECX at entry. */
#define ES2_STAGE2_TYPES_SCRATCH 0x58u
#define ES2_STAGE2_TYPES_FRAME 0x54u

/* weapon_types 0x40fc8c: the weapon texture wpntex (bitmap at 4a9ae8h,
 * descriptor at 4a9aech, then released), the weapon shapes mechwpn2 into
 * the list at 4a98f4h (count word 4a98f8h; +26h of each shape = 4a9ae8h),
 * the weapon list `weapons` (count word 4a98f0h, 58h-byte entries from
 * 0x49667c at 4a98ech, weapon_entry per entry, +52h = the name pointer
 * [498eb0h + 4k], +56h = k), then `proj` reopened on the same stream
 * (count word 4a9900h, 24h-byte records from new[] at 4a98fch). */
uint64_t es2_weapon_types_registers(const es2_guest_memory *,const es2_weapon_graph *,const uint32_t entry[3],
    uint32_t *ecx);
/* weapon_entry 0x40f8bc (E, S): dmg_region_read(E, S), collision_volume(E +
 * 12h, S), S.read(30h, E + 22h) through slot 18h; EAX = the read's AL,
 * word E + 48h = 0. */
uint64_t es2_weapon_entry_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t entry_record,
    uint32_t stream,const uint32_t entry[3],uint32_t *ecx);
/* rocket_types 0x40a818: `rockets` (count word 4a96d4h, 0Eh-byte records
 * from new[] at 4a96d0h), the word 4a96dch = max(0, word +0 of each record
 * + 1), the shapes `rockets` into 4a96d8h. */
uint64_t es2_rocket_types_registers(const es2_guest_memory *,const es2_weapon_graph *,const uint32_t entry[3],
    uint32_t *ecx);
/* bullet_types 0x40ade0: the byte 498514h set once, `bullets` (count word
 * 4a96fch, 0Eh-byte records at 4a96f8h), the word 4a9704h = max(0, word +0
 * + 1), the shapes into 4a9700h, the bitmap at 4a9706h and its descriptor at
 * 4a970ah, +26h of each shape = 4a9706h. */
uint64_t es2_bullet_types_registers(const es2_guest_memory *,const es2_weapon_graph *,const uint32_t entry[3],
    uint32_t *ecx);
/* beam_types 0x40b6e0: 120 words at 4a9712h (word i = (i SAR 1) + the word
 * [498640h + 2 (i AND 3)]), `beam` (the count word read into the local
 * f-52h, 6-byte records from new[] at 4a9804h), the descriptor of the bitmap
 * beamtex at 4a9808h, the bitmap released. */
uint64_t es2_beam_types_registers(const es2_guest_memory *,const es2_weapon_graph *,const uint32_t entry[3],
    uint32_t *ecx);
/* mech_forms 0x41fdb0: _memset(4a9d08h, FFh, 55h), word 4a9d06h = 0,
 * `mforms` (count word 4a9d68h, 1Ch-byte forms from 0x49667c at 4a9d6ch),
 * seven bitmaps named by [49a360h + 4k]: (bitmap, descriptor) at 4a9d72h +
 * 8k, the bitmap released and its slot cleared. */
uint64_t es2_mech_forms_registers(const es2_guest_memory *,const es2_weapon_graph *,const uint32_t entry[3],
    uint32_t *ecx);
/* flyer_forms 0x422d90: _memset(4a9dcch, FFh, 22h), word 4a9dcah = 0,
 * `fforms` (count word 4a9df8h, 12h-byte forms from 0x49667c at 4a9dfch). */
uint64_t es2_flyer_forms_registers(const es2_guest_memory *,const es2_weapon_graph *,const uint32_t entry[3],
    uint32_t *ecx);
#endif
