#ifndef ES2_CORE_POOLS_H
#define ES2_CORE_POOLS_H
#include "es2/core/weapons.h"
/* Remaining pool updates of the logic frame 0x45f570:
 * smoke_ball_update 0x4091cc, smoke_update 0x4092dc with its smoke-ball
 * constructor body smoke_ball_init 0x409178, debris_update 0x408bd8 with
 * its ground wrapper ground_height 0x427d80, and meteor_update 0x409d2c.
 * Contracts: docs/reverse-engineering/main_loop.md, section "remaining
 * logic-frame pool updates".
 *
 * Every game callee (the runtime frame setup 0x49630c, the timers,
 * movement_step, multiply_q10, euler_matrix, transform_point, 0x4801d4,
 * pool_alloc, object_init, explosion_create, 0x408530, terrain_height,
 * angle_ratio, radius_hit, the runtime 0x496406, the sound 0x4628e0 and
 * the virtual +10h/+18h/+20h) is called through g->call with the
 * original's entry registers, including this group's own nested hooks;
 * the pure leaf 0x4089bc is computed here. g->image is the image base
 * (addresses are image + VA - 0x400000).
 *
 * smoke_update, debris_update and meteor_update need 256 scratch bytes:
 * the synthetic EBP is scratch+208 and the runtime record lies at
 * EBP-24h (smoke, debris) or EBP-28h (meteor). 0x49630c links that record
 * into FS:0; the caller restores FS:0 to its entry head after the return
 * (the record's first dword, which the originals also return in EDX).
 * smoke_ball_update's frame count must be nonzero (the original IDIV
 * faults on 0); meteor_update's flat-object allocation must succeed when
 * the arrival animation ends (the original faults on a null object). */
uint64_t es2_smoke_ball_update_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t object,const uint32_t entry[3]);
uint64_t es2_smoke_update_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t object,const uint32_t entry[3]);
uint64_t es2_smoke_ball_init_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t ball,uint32_t point,uint32_t frame,const uint32_t entry[3]);
uint64_t es2_debris_update_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t object,const uint32_t entry[3]);
uint64_t es2_ground_height_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t point,const uint32_t entry[3]);
uint64_t es2_meteor_update_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t object,const uint32_t entry[3]);
#endif
