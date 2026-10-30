#ifndef ES2_CORE_LOGIC_H
#define ES2_CORE_LOGIC_H
#include "es2/core/weapons.h"
/* Pool updates called by the logic frame 0x45f570:
 * explosion_update 0x40813c, its light setter light_level 0x4076a0 and
 * fire_update 0x46b678. Every game callee (timer_word_step, light_level,
 * euler_matrix, transform_point, 0x4801d4, the node virtual +20 and the
 * sound wrapper 0x46297c) is called through g->call with the original's
 * entry registers; the pure leaves 0x407b20 (explosion type entry),
 * 0x407048/0x40735c (light entry) and 0x47df9c (square root) are computed
 * here. g->image is the image base (addresses are image + VA - 0x400000).
 * Contracts: docs/reverse-engineering/main_loop.md, section "explosion and fire
 * pool updates". The frame counts divided by must be nonzero, as the original
 * IDIV requires. fire_update needs 32 scratch bytes. */
uint64_t es2_explosion_update_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t object,const uint32_t entry[3]);
uint64_t es2_light_level_registers(const es2_guest_memory *,uint32_t image,uint32_t light,uint32_t value);
uint64_t es2_fire_update_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t object,const uint32_t entry[3]);
/* 0x47df9c: EAX of the original's bit-pair square root (floor). */
uint32_t es2_logic_root(uint32_t value);
#endif
