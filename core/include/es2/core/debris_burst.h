#ifndef ES2_CORE_DEBRIS_BURST_H
#define ES2_CORE_DEBRIS_BURST_H
#include "es2/core/weapons.h"
/* The shared debris spawn (docs/reverse-engineering/hit_tests.md,
 * section "debris burst").
 *
 *   debris_table    0x4089bc  pure leaf: the debris table record for an
 *                             index word, which it rebases into the second
 *                             list [0x4a964c] when the index is not below
 *                             the default list's count s16([0x4a9654])
 *   debris_burst    0x408530  identity matrix at the given position, then
 *                             debris_pick
 *   debris_pick     0x4086bc  weighted pick (count times) or every piece
 *                             (count 0); the weight walk has no end check
 *   debris_piece    0x408588  one piece: allocation from [0x4a9642],
 *                             debris_init, optional orientation, spin
 *   debris_init     0x408940  the debris constructor body
 *   debris_spin     0x408b74  random speed from [0x4a9664]..[0x4a9666],
 *                             then debris_heading 0x4089e4
 *   debris_spin_any 0x408bb4  debris_spin with a random heading
 *
 * T is the 10-byte table record {word count, word count2, dword pieces,
 * word bound}; a piece is 14 bytes {word shape index, word weight, word
 * byte b, word w, word angle or -1, word heading or -1, word speed}.
 *
 * Every game call goes through g->call with the EAX/EDX/ECX the original
 * holds there: memset 0x49681a, rng_bounded, rng_next, the runtime frame
 * setup 0x49630c (synthetic EBP f = g->scratch + ES2_DEBRIS_FRAME, record
 * at f-24h, context word f-14h), pool_alloc, object_init, euler_matrix,
 * the rotation compose 0x47f058, matrix_angles, debris_heading and this
 * group's own functions (nested hooks keep their modes). g->image is the
 * image base. debris_burst and debris_piece need ES2_DEBRIS_SCRATCH bytes
 * of scratch (their frame locals live below f), the others none.
 *
 * Results: EDX:EAX exactly as the originals leave them (ECX is dead at
 * every caller; hit_tests.md). debris_piece leaves FS:0 linked to its
 * record; the caller restores the entry head, as the epilogue does.
 * Preconditions (the original faults or never ends): the weights of a
 * picked table cover its bound; [0x4a9666] != [0x4a9664] and the table
 * bound != 0 (IDIV in rng_bounded). */
#define ES2_DEBRIS_SCRATCH 256u
#define ES2_DEBRIS_FRAME 208u
/* 0x4089bc: EAX = the record, EDX = s16([0x4a9654]); the index word at
 * `word` is lowered by that count when the second list is chosen. ECX
 * after the original is `word` (dead at all twelve call sites). */
uint64_t es2_debris_table_registers(const es2_guest_memory *,uint32_t image,uint32_t word);
uint64_t es2_debris_burst_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t table,uint32_t shapes,uint32_t position,uint32_t flag,const uint32_t entry[3]);
uint64_t es2_debris_pick_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t table,uint32_t shapes,uint32_t matrix,uint32_t flag,const uint32_t entry[3]);
uint64_t es2_debris_piece_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t shapes,uint32_t piece,uint32_t matrix,uint32_t flag,const uint32_t entry[3]);
uint64_t es2_debris_init_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t object,uint32_t shape,uint32_t position,uint32_t b,uint32_t w,const uint32_t entry[3]);
uint64_t es2_debris_spin_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t object,uint32_t speed,uint32_t heading,const uint32_t entry[3]);
uint64_t es2_debris_spin_any_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t object,uint32_t speed,const uint32_t entry[3]);
#endif
