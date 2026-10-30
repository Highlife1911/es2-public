#ifndef ES2_OBSTACLE_HOOKS_H
#define ES2_OBSTACLE_HOOKS_H
/* obstacle group (docs/reverse-engineering/mission_runtime.md,
 * section "Static-obstacle list 404ae4", and convoy.md, section
 * "collision test 0x46a614"): obstacle_test 404ae4 and convoy_collide 46a614. Every game callee
 * (the pool step 471c70, the collision walk 427c68, dist_gate, the actor
 * virtuals +5Ch/+68h/+7Ch, and 404ae4 under 46a614) goes through the
 * weapons guest bridge and callee observer (weapons_graph) with the
 * original's registers, so nested hooks keep their modes; both refuse
 * compare mode. obstacle_test keeps the original's 23Ch-byte frame image
 * (list, cursor, EBP-8, count, iterator) in the adapter's zeroed frame
 * buffer, so a list past 140 entries overwrites the cursor and the 141st
 * object exactly as the original does; the never-written word EBP-8 reads
 * 0 here (the original's is stack residue, reachable only with 142 or
 * more base objects; no entry shim). No entry register is read, ECX after
 * either hook is dead at every caller. No game instruction is embedded. */
#include "es2/core/obstacle.h"
static uint64_t __cdecl obstacle_obstacle_test(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE frame[ES2_OBSTACLE_FRAME]={0};es2_weapon_graph g=weapons_graph(u,frame,ES2_HOOK_INDEX_obstacle_test);
    return es2_obstacle_test_registers(&math_native_memory,&g,a[0],a[1],a[2],r);
}
static uint64_t __cdecl obstacle_convoy_collide(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_convoy_collide);
    return es2_convoy_collide_registers(&math_native_memory,&g,a[0],r);
}
/* Callee counters (hk_<hook>_<suffix>), incremented only while the hook is
 * non-original. Keys: the callee VA, or the vtable offset for slots. */
static callee_counter obstacle_test_counters[]={
    {"call_471c70",ES2_COLLIDE_CALL,0x471c70,0},
    {"call_427c68",ES2_COLLIDE_CALL,0x427c68,0},
};
static callee_counter convoy_collide_counters[]={
    {"slot_5c",ES2_COLLIDE_SLOT,0x5c,0},
    {"slot_7c",ES2_COLLIDE_SLOT,0x7c,0},
    {"call_49288c",ES2_COLLIDE_CALL,0x49288c,0},
    {"slot_68",ES2_COLLIDE_SLOT,0x68,0},
    {"call_404ae4",ES2_COLLIDE_CALL,0x404ae4,0},
};
static callee_counter *obstacle_hook_counters(unsigned hook,unsigned *count) {
    if(hook==ES2_HOOK_INDEX_obstacle_test) { *count=sizeof(obstacle_test_counters)/sizeof(obstacle_test_counters[0]);return obstacle_test_counters; }
    if(hook==ES2_HOOK_INDEX_convoy_collide) { *count=sizeof(convoy_collide_counters)/sizeof(convoy_collide_counters[0]);return convoy_collide_counters; }
    *count=0;return NULL;
}
static const BYTE obstacle_test_sig[]={0x55,0x8b,0xec,0x81,0xc4,0xc4,0xfd,0xff,0xff,0x33,0xc0};
static const BYTE obstacle_collide_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xf0,0xa1,0xf8,0x9a,0x4a,0x00};
static const BYTE obstacle_test_lens[]={1,2,6},obstacle_frame_lens[]={1,2,3};
#define OBSTACLE_EXT(name) \
    static const es2_hook_extension obstacle_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,obstacle_##name,TRUE}; \
    static uint32_t __cdecl obstacle_##name##32(const uint32_t *a,void *u) {return (uint32_t)obstacle_##name(a,u,weapons_no_registers);}
OBSTACLE_EXT(obstacle_test)
OBSTACLE_EXT(convoy_collide)
#undef OBSTACLE_EXT
static es2_hook_descriptor obstacle_hook_descriptors[]={
    {ES2_HOOK_ID(obstacle_test),obstacle_test_sig,11,9,obstacle_test_lens,3,3,obstacle_obstacle_test32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(convoy_collide),obstacle_collide_sig,11,6,obstacle_frame_lens,3,1,obstacle_convoy_collide32,NULL,NULL,NULL,0},
};
#endif
