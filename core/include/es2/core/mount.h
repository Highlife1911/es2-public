#ifndef ES2_CORE_MOUNT_H
#define ES2_CORE_MOUNT_H
#include "es2/core/weapons.h"
/* Trig leaves and weapon mount helper. Contracts in
 * docs/reverse-engineering/player_movement.md (section "trig leaves")
 * and weapons.md (section "weapon mount helper"). Addresses are guest
 * addresses; `image` is the loaded image base (0x400000 in the original).
 *
 * sincos_pair 47eb30 (angle, m): words m+0 = m+6 = cos, m+2 = sin,
 * m+4 = -sin of the low word of `angle` (quarter sine table at `quarter`,
 * a host pointer). EDX:EAX as the original.
 *
 * rotate_2d 47ffe8 (v, m, out): out = the two dwords v rotated by the
 * 2 x 2 Q14 word matrix m, in the original's read/write order. EDX:EAX =
 * EDX : out.
 *
 * mount_base 40f904 (M, side): fills the static vector at 4a9af0 from M's
 * words +46h/+4Ah, mirrored or cleared by the side word, and sets the
 * guard byte 498f34. EDX:EAX as the original.
 *
 * mount_offset 40f540 (W): calls 40f904 through the graph with the
 * original's registers and adds the definition's offset words to the
 * vector it returns. EDX:EAX as the original. */
uint64_t es2_sincos_pair_registers(const es2_guest_memory *,uint32_t angle,uint32_t matrix,const void *quarter);
uint64_t es2_rotate_2d_registers(const es2_guest_memory *,uint32_t vector,uint32_t matrix,uint32_t out);
uint64_t es2_mount_base_registers(const es2_guest_memory *,uint32_t image,uint32_t mount,uint32_t side);
uint64_t es2_mount_offset_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t weapon,
    const uint32_t entry[3]);
#endif
