#ifndef ES2_CORE_OBSTACLE_H
#define ES2_CORE_OBSTACLE_H
#include "es2/core/weapons.h"
/* The static-obstacle test and the convoy collision test. Exact
 * contracts: docs/reverse-engineering/mission_runtime.md, section
 * "Static-obstacle list 404ae4", and docs/reverse-engineering/
 * convoy.md, section "collision test 0x46a614". Every game call (the pool step
 * 471c70, the collision walk 427c68, dist_gate, the actor virtuals
 * +5Ch/+68h/+7Ch and 404ae4 itself under 46a614) goes through g->call with the
 * registers the original sets up, so nested hooks keep their own modes;
 * g->image locates the globals. Both return EDX:EAX exactly; ECX is outside the
 * contract (dead at every caller). */

/* Bytes of the frame image obstacle_test keeps at g->scratch: the list
 * (140 dwords), the cursor slot (+230h), the never-written word EBP-8
 * (+234h), the count word (+236h) and the iterator (+238h). */
#define ES2_OBSTACLE_FRAME 0x23cu

/* obstacle_test 404ae4 (P, radius, exclude): collects the objects of the
 * pool [4a95a0] that pass the filter into the frame image and returns
 * 427c68(P, radius word, list, count). Appends go through the cursor slot,
 * so the 141st entry overwrites the cursor and later ones land in the
 * 141st object, as in the original. */
uint64_t es2_obstacle_test_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t point,uint32_t radius,uint32_t exclude,const uint32_t entry[3]);
/* convoy_collide 46a614 (B): the first actor of the actor list within the
 * combined radius (flagged through virtual +68h; AX = 1), else 404ae4. */
uint64_t es2_convoy_collide_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t object,const uint32_t entry[3]);
/* pool_query 404bc0 (out, record, filter): the objects of the pool
 * [4a95a0] that pass the team filter (only for a non-negative filter word)
 * and the first test of obstacle_test into a 140-dword frame list, then
 * object_query 427da8(out, record, list, count) when the list is not
 * empty. AX = 1 when 427da8 returned AX != 0, else 0; EDX is the last
 * callee's. Contract: docs/reverse-engineering/ai.md, section "pool object
 * query 404bc0". ES2_POOL_QUERY_FRAME bytes at g->scratch: the list (140
 * dwords), the cursor slot (+230h), the result dword (+234h), the never-written
 * word EBP-8 (+238h), the count word (+23Ah) and the iterator (+23Ch). The
 * 141st append overwrites the cursor slot, as in the original. */
#define ES2_POOL_QUERY_FRAME 0x240u
uint64_t es2_pool_query_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t out,uint32_t record,uint32_t filter,const uint32_t entry[3]);
/* collision_walk 427c68 (P, radius, list, count) and shape_cell
 * 427360 (S, R, word). Contract: docs/reverse-engineering/
 * mission_runtime.md, section "collision walk 427c68 and shape cell
 * 427360". collision_walk tests the first count (signed low word) list
 * entries against P: dist2d_point, then inside the shape radius the lazy
 * euler_matrix, the swap 47df04, rotate_2d and shape_cell 427360; it
 * returns the first nonzero 427360 EAX. The radius slot is never read.
 * ES2_COLLISION_WALK_FRAME bytes at g->scratch hold the original's frame
 * image (synthetic EBP = scratch + 24h): D at -24h, R at -1Ch, M at -0Ch.
 * shape_cell returns 0 when word [S+2Ah] is 0, else cell 427238(S, Q,
 * EDX) with Q = R + the shape's origin words +2Eh/+30h shifted left by
 * word +32h; ES2_SHAPE_CELL_FRAME bytes at g->scratch (EBP = scratch +
 * 0Ch: Q at -0Ch, the two shifted words at -4/-2). Both return EDX:EAX
 * exactly; ECX is outside the contract (dead at every caller). */
#define ES2_COLLISION_WALK_FRAME 0x24u
#define ES2_SHAPE_CELL_FRAME 0x0cu
uint64_t es2_collision_walk_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t point,uint32_t radius,uint32_t list,uint32_t count,const uint32_t entry[3]);
uint64_t es2_shape_cell_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t shape,uint32_t point,uint32_t word,const uint32_t entry[3]);
#endif
