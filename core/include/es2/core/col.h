#ifndef ES2_CORE_COL_H
#define ES2_CORE_COL_H
#include "es2/core/weapons.h"
/* The COL traversal of the actor hit tests. Exact contract:
 * docs/reverse-engineering/hit_tests.md, section "COL
 * traversal".
 *
 *   col_query  0x40c9d4 (COL, actor, S, M, flags)  every group of a COL
 *   col_group  0x40c8fc (G, actor, M, flags)       one group: its frame
 *                                                  matrix, then its shapes
 *   col_shape  0x40c8c8 (shape)                    bound test, then spheres
 *   col_bound  0x40c4c4 (center, radius)           bounding-sphere test
 *
 * State: the statics 0x4a980c (matrix pointer), 0x4a9810 (remaining
 * length), 0x4a9814 (probe radius word), 0x4a9818/0x4a981c (best hit and
 * its region word) and the result pair 0x4a981e/0x4a9822, all in the image
 * (g->image is its base). Every game call goes through g->call with the
 * EAX/EDX/ECX the original holds there: the shape node lookup (virtual
 * +20h of [[actor+34h]+4]), matrix_compose 0x47fa20, the point transform
 * 0x4801d4, dist_octagonal 0x47de4c, sphere_hits 0x40c524 and the group's
 * own nested functions, so nested hooks keep their modes.
 *
 * Scratch (g->scratch, guest-addressable, ES2_COL_SCRATCH bytes): col_group
 * keeps its composed 32-byte matrix (the original's EBP-24h) at +0 and
 * stores that address in 0x4a980c, as the original stores its frame
 * address; col_bound keeps the transformed center (EBP-0Ch) at +0.
 * col_query and col_shape need none. Nothing reads 0x4a980c after
 * col_group returns (readers: col_bound and sphere_hits, both only inside
 * col_group's shape loop).
 *
 * Results: the full EDX:EAX pair of the original. ECX after each hook is
 * dead at every caller (hit_tests.md). */
#define ES2_COL_SCRATCH 32u
uint64_t es2_col_query_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t col,uint32_t actor,
    uint32_t segment,uint32_t matrix,uint32_t flags,const uint32_t entry[3]);
uint64_t es2_col_group_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t group,uint32_t actor,
    uint32_t matrix,uint32_t flags,const uint32_t entry[3]);
uint64_t es2_col_shape_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t shape,
    const uint32_t entry[3]);
uint64_t es2_col_bound_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t center,uint32_t radius,
    const uint32_t entry[3]);
#endif
