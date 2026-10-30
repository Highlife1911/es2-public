#ifndef ES2_CORE_MECH_PART_HIT_H
#define ES2_CORE_MECH_PART_HIT_H
#include "es2/core/weapons.h"
/* The MECH part hit 0x4188c8(A, part, P, point), called by the
 * MECH hit test 0x418ba8 and by the MECH "player fly" movement 0x4198f4
 * (docs/reverse-engineering/hit_tests.md, the section
 * "MECH part hit"). A is the MECH, part the region index (dword),
 * P the sweep packet (P+4 damage word, P+8 splash factor word, P+0Ah the
 * effect table, P+0Eh the owner), point the hit point (three dwords).
 *
 * It damages the part through slot +74h, splashes through slot +70h,
 * compares the part's condition bracket (part_ratio >> 5) before and
 * after; on a change: the panel jolt 0x434010 and the message 0x420a68(12h)
 * for the player's parts 0/1, and for a weapon-mount part (>= 19) the
 * weapon-destruction chance 41*n of 4096 (n = 10, or 3 when the
 * definition's side byte is 0): weapon eject 0x40f57c, region flag word
 * [[A+20Eh]+2*part] = 0, the results-list append (side byte 1) and the
 * region damage 0x40da38. Then one explosion (pool [0x4a961e]) and, on a
 * bracket change that did not reach 100h, a debris burst
 * (0x4089bc/0x408530).
 *
 * Every game call goes through g->call with the EAX/EDX/ECX the original
 * holds there: the runtime frame setup 0x49630c (synthetic EBP f =
 * g->scratch + ES2_MECH_PART_HIT_FRAME, record at f-24h, context word
 * f-14h), multiply_q10, part_ratio, the slots +74h/+70h of A, 0x434010,
 * 0x420a68, rng_next, 0x40f57c, 0x40da38, pool_alloc, explosion_create,
 * debris_table and debris_burst (nested hooks keep their modes). The two
 * small leaves 0x410670 (weapon by kind) and 0x426ac8 (results-list
 * append) are computed here with their exact registers. g->image is the
 * image base; g->scratch needs ES2_MECH_PART_HIT_SCRATCH guest bytes.
 *
 * Result: EDX:EAX exactly as the original leaves them; EAX is the saved
 * FS:0 head of the record (dword f-24h), and the caller restores FS:0 to
 * it, as the epilogue does. ECX is dead at all seven call sites.
 * Precondition (the original faults): the weapon lookup finds a weapon
 * whenever the weapon path is reached (0x40f57c reads [W+49h] first, the
 * side-byte-1 path [W+1Ch]); the in-vitro stub of 0x40f57c tolerates W = 0
 * as long as the side byte is not 1. */
#define ES2_MECH_PART_HIT_SCRATCH 256u
#define ES2_MECH_PART_HIT_FRAME 208u
uint64_t es2_mech_part_hit_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t mech,uint32_t part,uint32_t packet,uint32_t point,const uint32_t entry[3]);
/* 0x410670(system, word): the first non-null weapon of system ([+4] the
 * pointer array, signed word [+8] the count) whose definition byte
 * [[W+18h]+17h], sign-extended, equals the word. out: EAX = the weapon or
 * 0, EDX = the index where the scan stopped (dword), ECX = the last array
 * entry read (unchanged when the count is <= 0). */
void es2_weapon_by_kind(const es2_guest_memory *,uint32_t system,uint32_t word,es2_weapon_registers *out);
/* 0x426ac8(&item): appends the dword at item to the list [0x4a9e6c] at the
 * signed word index [0x4a9e6a] (no bound check) and increments the word.
 * out: EAX = the old index (sign-extended), EDX = the list, ECX = the
 * dword. */
void es2_results_append(const es2_guest_memory *,uint32_t image,uint32_t item,es2_weapon_registers *out);
#endif
