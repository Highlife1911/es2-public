#ifndef ES2_OBSTACLE_WALK_HOOKS_H
#define ES2_OBSTACLE_WALK_HOOKS_H
/* obstacle_walk group (docs/reverse-engineering/mission_runtime.md,
 * section "collision walk 427c68 and shape cell 427360", and
 * convoy.md, section "46a5bc convoy_leader(B)"): collision_walk 427c68 (the list walk under
 * obstacle_test), shape_cell 427360 (its per-entry shape test) and
 * convoy_leader 46a5bc (the group-leader search under convoy_drive and
 * convoy_follow). The game callees (dist2d_point, euler_matrix, the
 * unhooked 2 x 2 swap 47df04, rotate_2d, 427360 under 427c68; cell 427238
 * under 427360) go through the weapons guest bridge and callee observer
 * (weapons_graph) with the original's registers, so nested hooks keep
 * their modes; the frame locals passed by pointer (D, R, M; Q) live in a
 * zeroed frame image in the adapter's scratch. convoy_leader is a pure
 * reader without calls. No entry shim: no uninitialized local is read and
 * the only entry registers that matter (EDX on the early returns, ECX at
 * 427c68's first dist2d_point call) come from the dispatch context. ECX
 * after each hook is dead at every caller (mission_runtime.md, convoy.md).
 * All three accept compare mode: convoy_leader and shape_cell write no
 * game state (cell is a read-only compare-capable hook); collision_walk
 * writes only the entries' lazy matrix cache (euler_matrix and word
 * E+32h), which the original phase fills, so the replacement phase finds
 * it set and writes nothing (as object_query). collision_walk's
 * comparator checks EAX and DX: on the no-hit exit EDX's high word is the
 * last callee's (dist2d_point's or 427360's), which nested hooks in other
 * modes leave differently and no caller reads. No game instruction is
 * embedded. */
#include "es2/core/obstacle.h"
#include "es2/core/convoy.h"
static uint64_t __cdecl obstacle_walk_collision_walk(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE frame[ES2_COLLISION_WALK_FRAME]={0};es2_weapon_graph g=weapons_graph(u,frame,ES2_HOOK_INDEX_collision_walk);
    return es2_collision_walk_registers(&math_native_memory,&g,a[0],a[1],a[2],a[3],r);
}
static uint64_t __cdecl obstacle_walk_shape_cell(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE frame[ES2_SHAPE_CELL_FRAME]={0};es2_weapon_graph g=weapons_graph(u,frame,ES2_HOOK_INDEX_shape_cell);
    return es2_shape_cell_registers(&math_native_memory,&g,a[0],a[1],a[2],r);
}
static uint64_t __cdecl obstacle_walk_convoy_leader(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;(void)r;return es2_convoy_leader_registers(&math_native_memory,a[0]);
}
/* Callee counters (hk_<hook>_<suffix>), incremented only while the hook is
 * non-original. */
static callee_counter collision_walk_counters[]={
    {"call_4928d0",ES2_COLLIDE_CALL,0x4928d0,0},
    {"call_47ebb8",ES2_COLLIDE_CALL,0x47ebb8,0},
    {"call_47df04",ES2_COLLIDE_CALL,0x47df04,0},
    {"call_47ffe8",ES2_COLLIDE_CALL,0x47ffe8,0},
    {"call_427360",ES2_COLLIDE_CALL,0x427360,0},
};
static callee_counter shape_cell_counters[]={
    {"call_427238",ES2_COLLIDE_CALL,0x427238,0},
};
static callee_counter *obstacle_walk_hook_counters(unsigned hook,unsigned *count) {
    if(hook==ES2_HOOK_INDEX_collision_walk) { *count=sizeof(collision_walk_counters)/sizeof(collision_walk_counters[0]);return collision_walk_counters; }
    if(hook==ES2_HOOK_INDEX_shape_cell) { *count=1;return shape_cell_counters; }
    *count=0;return NULL;
}
/* push ebp; mov ebp,esp; add esp,-24h; push ebx; push esi; push edi */
static const BYTE obstacle_walk_collision_walk_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xdc,0x53,0x56,0x57};
/* push ebp; mov ebp,esp; add esp,-0Ch; push ebx; mov eax,[ebp+8] */
static const BYTE obstacle_walk_shape_cell_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xf4,0x53,0x8b,0x45,0x08};
/* push ebp; mov ebp,esp; push esi; push edi; xor eax,eax */
static const BYTE obstacle_walk_convoy_leader_sig[]={0x55,0x8b,0xec,0x56,0x57,0x33,0xc0};
static const BYTE obstacle_walk_frame_lens[]={1,2,3},obstacle_walk_push_lens[]={1,2,1,1};
/* EAX and DX: the high word of EDX after a walk without a hit is the last
 * callee's (dead at obstacle_test's callers, which test AX only). */
static BOOL __cdecl obstacle_walk_walk_equal(const es2_hook_comparison *v,void *u) {
    (void)u;return v->original_result==v->replacement_result&&(uint16_t)v->original_edx==(uint16_t)v->replacement_edx;
}
#define OBSTACLE_WALK_EXT(name) \
    static const es2_hook_extension obstacle_walk_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,obstacle_walk_##name,FALSE}; \
    static uint32_t __cdecl obstacle_walk_##name##32(const uint32_t *a,void *u) {return (uint32_t)obstacle_walk_##name(a,u,weapons_no_registers);}
OBSTACLE_WALK_EXT(collision_walk)
OBSTACLE_WALK_EXT(shape_cell)
OBSTACLE_WALK_EXT(convoy_leader)
#undef OBSTACLE_WALK_EXT
static es2_hook_descriptor obstacle_walk_hook_descriptors[]={
    {ES2_HOOK_ID(collision_walk),obstacle_walk_collision_walk_sig,9,6,obstacle_walk_frame_lens,3,4,obstacle_walk_collision_walk32,obstacle_walk_walk_equal,NULL,NULL,0},
    {ES2_HOOK_ID(shape_cell),obstacle_walk_shape_cell_sig,10,6,obstacle_walk_frame_lens,3,3,obstacle_walk_shape_cell32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(convoy_leader),obstacle_walk_convoy_leader_sig,7,5,obstacle_walk_push_lens,4,1,obstacle_walk_convoy_leader32,NULL,NULL,NULL,0},
};
#endif
