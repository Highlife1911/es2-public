#ifndef ES2_CORE_TRANSFORM_HELPERS_H
#define ES2_CORE_TRANSFORM_HELPERS_H
#include "es2/core/weapons.h"
/* Enter-prologue helpers. Contract in
 * docs/reverse-engineering/player_movement.md, section "enter-prologue helpers
 * 47ff8c and 4801d4". Addresses are guest addresses; all arithmetic is 32-bit
 * and wraps as the original.
 *
 * rotate_words 47ff8c (v, m, out): the word pair v rotated by the 2 x 2
 * Q14 word matrix m into the word pair out: out0 = (v0 m0 + v1 m2 + 2000h)
 * SAR 14, out1 = (v0 m1 + v1 m3 + 2000h) SAR 14, in the original's
 * read/write order (out may overlap v or m). EDX:EAX = low dword of
 * v1 m3 : out; *ecx = s16 m3.
 *
 * matrix_point 4801d4 (v, M, out): the word triple v times the transform M
 * (nine Q14 words, mode byte M+12h, translation dwords M+14h..1Fh) into the
 * dword triple out. Mode < 1 (signed byte): translation only; mode 1:
 * planar (rows 0 and 1 use two products, row 2 none); mode > 1: three
 * products per row, words (0,2,6), (1,3,7), (4,5,8). Read/write order as
 * the original. EAX = out, EDX = the low dword of the last product, *ecx =
 * its sign-extended matrix word; on the mode < 1 path EAX = the stored
 * out2 and EDX/ECX the entry values (entry = EAX, EDX, ECX). */
uint64_t es2_rotate_words_registers(const es2_guest_memory *,uint32_t vector,uint32_t matrix,uint32_t out,uint32_t *ecx);
uint64_t es2_matrix_point_registers(const es2_guest_memory *,uint32_t vector,uint32_t matrix,uint32_t out,
    const uint32_t entry[3],uint32_t *ecx);
#endif
