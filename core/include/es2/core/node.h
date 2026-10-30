#ifndef ES2_CORE_NODE_H
#define ES2_CORE_NODE_H
#include "es2/core/weapons.h"
/* Three small graph nodes. Every game callee, including hooked leaves
 * and the virtual size query, is called through g->call; the pure helpers
 * 47ffe8 (2 x 2 Q14 rotation) and 47e0c0 (Q14 word product) are computed
 * here. scratch: 128 accessible guest bytes, disjoint from game state.
 * Contracts: player_movement.md (terrain_pose), weapons.md
 * (shield_geometry), rng.md (debris_heading). */
uint64_t es2_terrain_pose_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t object,const uint32_t entry[3]);
uint64_t es2_shield_geometry_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t shields,uint32_t vehicle,uint32_t matrix,uint32_t range,uint32_t damage,const uint32_t entry[3]);
/* The divisor must be nonzero, as required by the original IDIV. */
uint64_t es2_debris_heading_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t object,uint32_t divisor,uint32_t pitch,uint32_t yaw,const uint32_t entry[3]);
#endif
