#ifndef ES2_CORE_REGION_DAMAGE_H
#define ES2_CORE_REGION_DAMAGE_H
#include "es2/core/weapons.h"
/* The region-damage helpers (docs/reverse-engineering/hit_tests.md,
 * section "region-damage helpers"). The region damage 0x40d434 / 0x40da38
 * in this module calls these entry points.
 *
 *   region_absorb 0x40d3ec  a region word takes damage up to its limit;
 *                           the overflow goes back through the damage
 *                           pointer and the region becomes -1
 *   region_spread 0x40cf44  the damage goes to weighted random live
 *                           regions (stream A) until one does not break
 *   region_entry  0x40ca58  the entry of a record whose key word matches
 *   region_find   0x40ca8c  region_entry over a list of records
 *
 * All four return EDX:EAX as the original leaves them and store the
 * original's ECX at *ecx (ecx may be NULL). The hook dispatch returns only
 * EDX:EAX; ECX is dead at every caller (see the document).
 *
 * region_find and region_spread call game code through g->call with the
 * EAX/EDX/ECX the original holds there (region_entry 0x40ca58,
 * rng_bounded 0x492f24 on stream A at g->image + 0xd2599, region_absorb
 * 0x40d3ec), so nested hooks keep their modes; g->image is the image
 * base. region_spread keeps the damage word, which the original holds in
 * its own third argument slot, in the first ES2_REGION_SPREAD_SCRATCH
 * bytes of g->scratch (initialized with the damage dword); on return its
 * low word is the final damage. The leaves only read and write guest
 * memory.
 *
 * Precondition of region_spread (the original never ends otherwise): a
 * region that region_absorb reports broken is not chosen again, which
 * holds for non-negative weights whose live total stays below 8000h. */
#define ES2_REGION_SPREAD_SCRATCH 4u
/* 0x40d3ec (v, limit, d): v and d point to words; the limit is the low
 * word of the dword. entry_ecx is the entry ECX (its high word survives
 * when the region is already negative). EAX = 1 if the region is
 * negative afterwards. */
uint64_t es2_region_absorb_registers(const es2_guest_memory *,uint32_t region,uint32_t limit,uint32_t damage,
    uint32_t entry_ecx,uint32_t *ecx);
/* 0x40cf44 (R, V, d, T): R the per-part record (count word +6, entries
 * {weight, region index} at [+8]), V the region words, d the damage
 * dword, T the limit words. EAX = 1 if no weighted live region is left;
 * ECX = the last total word above the entry ECX's low word. */
uint64_t es2_region_spread_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t record,uint32_t regions,uint32_t damage,uint32_t limits,const uint32_t entry[3],uint32_t *ecx);
/* 0x40ca58 (N, k, o): the 16-byte entry at [N+6] whose word +6 equals
 * k's low word, among the first s16(word N+4); on a match word [o] <-
 * word [N]. EAX = the entry or 0, EDX = the index or the step count. */
uint64_t es2_region_entry_registers(const es2_guest_memory *,uint32_t record,uint32_t key,uint32_t out,uint32_t *ecx);
/* 0x40ca8c (S, k, o): region_entry over the s16(word S) 10-byte records
 * at [S+2]; the first nonzero result with its EDX/ECX, else EAX = 0, EDX
 * = S. */
uint64_t es2_region_find_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t list,uint32_t key,uint32_t out,const uint32_t entry[3],uint32_t *ecx);

/* The region damage 0x40da38 and the part break 0x40d434
 * (docs/reverse-engineering/hit_tests.md, section "region damage
 * 40da38 and part break 40d434"). D is the damage state (A+206h, F+200h).
 *
 *   region_hit   0x40da38 (D, part, damage)  the part's word absorbs the
 *                damage up to its limit; the overflow spreads over its
 *                regions; a part that breaks with flag bit 0 breaks, and so
 *                do the children it pushes on the stack [0x498864]
 *   region_break 0x40d434 (D, part)  the part is marked broken, 7D00h
 *                damage spreads over its regions, fires, the state word,
 *                an explosion and a debris burst at the part's point, and
 *                the intact children are pushed on the stack
 *
 * Both return EDX:EAX as the original leaves them and store the original's
 * ECX at *ecx (ecx may be NULL); every game call goes through g->call with
 * the original's EAX/EDX/ECX (nested hooks keep their modes; the node
 * lookup is the virtual +20h of the node tree, its target read from the
 * vtable). region_hit keeps the damage word, which the original holds in
 * its own third argument slot and hands to 0x40d3ec by address, in the
 * first ES2_REGION_HIT_SCRATCH bytes of g->scratch (initialized with the
 * damage dword). region_break keeps its frame in g->scratch
 * (ES2_REGION_BREAK_SCRATCH bytes, synthetic EBP f = g->scratch +
 * ES2_REGION_BREAK_FRAME, every local at the original's offset): the
 * runtime frame setup 0x49630c, called first through g->call, links the
 * record at f-34h (context word f-24h); the caller restores FS:0 from the
 * saved head, the dword at f-34h, as the original's epilogues do.
 *
 * Preconditions (the original faults otherwise): region_find finds an
 * entry for an intact part; the stack [0x498864] exists; the cascade ends. */
#define ES2_REGION_HIT_SCRATCH 4u
#define ES2_REGION_BREAK_SCRATCH 256u
#define ES2_REGION_BREAK_FRAME 0xf0u
uint64_t es2_region_hit_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t state,uint32_t part,uint32_t damage,const uint32_t entry[3],uint32_t *ecx);
uint64_t es2_region_break_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t state,uint32_t part,const uint32_t entry[3],uint32_t *ecx);
#endif
