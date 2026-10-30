#ifndef ES2_CORE_AI_H
#define ES2_CORE_AI_H
#include "es2/core/weapons.h"
/* Vehicle AI helpers; exact contracts: docs/reverse-engineering/ai.md.
 * Every game call goes through the graph's guest bridge (hooked callees are
 * entered as hooks) with the entry registers the original sets up.
 *
 * object_line 404ca0 (out, start, end, radius word, filter word): builds the
 * swept segment record start -> end and runs the object query 404bc0 on it.
 * Needs 128 scratch bytes. EDX:EAX is the query's pair.
 *
 * obstacle_steer 416274 (actor, turn word pointer, speed word pointer,
 * flags): two feeler segments, nearby vehicles and the avoidance point table
 * adjust the turn word and may set the speed word. Needs 256 scratch bytes.
 * EDX:EAX is the path's register residue (dead at the only caller). */
uint64_t es2_object_line_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t out,uint32_t start,
    uint32_t end,uint32_t radius,uint32_t filter,const uint32_t entry[3]);
uint64_t es2_obstacle_steer_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t actor,uint32_t turn,
    uint32_t speed,uint32_t flags,const uint32_t entry[3]);
#endif
