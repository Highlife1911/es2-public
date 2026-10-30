#ifndef ES2_POOLS_HOOKS_H
#define ES2_POOLS_HOOKS_H
/* Remaining pool updates of the logic frame
 * (docs/reverse-engineering/main_loop.md, section "remaining logic-frame
 * pool updates"):
 * smoke_ball_update 4091cc, smoke_update 4092dc, smoke_ball_init 409178,
 * debris_update 408bd8, ground_height 427d80, meteor_update 409d2c.
 * Included after weapons_hooks.h and effect_create_hooks.h: every game
 * callee goes through the weapons guest bridge and callee observer; the
 * runtime frame setup 49630c gets the synthetic EBP (scratch+208) through
 * weapons_impact_guest_call, and the adapter restores FS:0 to its entry
 * head afterwards, as the originals' epilogues do. Five refuse compare
 * mode (game calls; pool links and the runtime count lie outside fixed
 * spans); ground_height, a reader whose only callee terrain_height is
 * pure, accepts it. No entry shim, no game instruction
 * embedded. */
#include "es2/core/pools.h"
#define POOLS_FRAME_GRAPH(index) \
    BYTE scratch[256]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_##index); \
    weapons_impact_context c={(uint32_t)(uintptr_t)u,(uint32_t)(uintptr_t)(scratch+208)}; \
    g.call=weapons_impact_guest_call;g.context=&c; \
    uint32_t head;__asm__ volatile("movl %%fs:0,%0":"=r"(head))
#define POOLS_FRAME_RESTORE() __asm__ volatile("movl %0,%%fs:0"::"r"(head):"memory")
static uint64_t __cdecl pools_smoke_ball_update(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_smoke_ball_update);
    return es2_smoke_ball_update_registers(&math_native_memory,&g,a[0],r);
}
static uint64_t __cdecl pools_smoke_update(const uint32_t *a,void *u,const uint32_t *r) {
    POOLS_FRAME_GRAPH(smoke_update);
    uint64_t result=es2_smoke_update_registers(&math_native_memory,&g,a[0],r);
    POOLS_FRAME_RESTORE();return result;
}
static uint64_t __cdecl pools_smoke_ball_init(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_smoke_ball_init);
    return es2_smoke_ball_init_registers(&math_native_memory,&g,a[0],a[1],a[2],r);
}
static uint64_t __cdecl pools_debris_update(const uint32_t *a,void *u,const uint32_t *r) {
    POOLS_FRAME_GRAPH(debris_update);
    uint64_t result=es2_debris_update_registers(&math_native_memory,&g,a[0],r);
    POOLS_FRAME_RESTORE();return result;
}
static uint64_t __cdecl pools_ground_height(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_ground_height);
    return es2_ground_height_registers(&math_native_memory,&g,a[0],r);
}
static uint64_t __cdecl pools_meteor_update(const uint32_t *a,void *u,const uint32_t *r) {
    POOLS_FRAME_GRAPH(meteor_update);
    uint64_t result=es2_meteor_update_registers(&math_native_memory,&g,a[0],r);
    POOLS_FRAME_RESTORE();return result;
}
#undef POOLS_FRAME_GRAPH
#undef POOLS_FRAME_RESTORE
/* Callee counters (hk_<hook>_<suffix>), incremented only while the hook is
 * non-original. Keys: the callee VA, or the vtable offset for slots. */
static callee_counter pools_smoke_ball_update_counters[]={
    {"call_467aa8",ES2_COLLIDE_CALL,0x467aa8,0},
};
static callee_counter pools_smoke_update_counters[]={
    {"call_49630c",ES2_COLLIDE_CALL,0x49630c,0},
    {"call_467a48",ES2_COLLIDE_CALL,0x467a48,0},
    {"slot_20",ES2_COLLIDE_SLOT,0x20,0},
    {"call_4801d4",ES2_COLLIDE_CALL,0x4801d4,0},
    {"call_47ebb8",ES2_COLLIDE_CALL,0x47ebb8,0},
    {"call_48043c",ES2_COLLIDE_CALL,0x48043c,0},
    {"call_471b30",ES2_COLLIDE_CALL,0x471b30,0},
    {"call_409178",ES2_COLLIDE_CALL,0x409178,0},
};
static callee_counter pools_smoke_ball_init_counters[]={
    {"call_402188",ES2_COLLIDE_CALL,0x402188,0},
};
static callee_counter pools_debris_update_counters[]={
    {"call_49630c",ES2_COLLIDE_CALL,0x49630c,0},
    {"call_467924",ES2_COLLIDE_CALL,0x467924,0},
    {"call_47e0b0",ES2_COLLIDE_CALL,0x47e0b0,0},
    {"slot_10",ES2_COLLIDE_SLOT,0x10,0},
    {"call_427d80",ES2_COLLIDE_CALL,0x427d80,0},
    {"call_467aa8",ES2_COLLIDE_CALL,0x467aa8,0},
    {"call_471b30",ES2_COLLIDE_CALL,0x471b30,0},
    {"call_407f1c",ES2_COLLIDE_CALL,0x407f1c,0},
    {"call_408530",ES2_COLLIDE_CALL,0x408530,0},
};
static callee_counter pools_ground_height_counters[]={
    {"call_46e180",ES2_COLLIDE_CALL,0x46e180,0},
};
static callee_counter pools_meteor_update_counters[]={
    {"call_49630c",ES2_COLLIDE_CALL,0x49630c,0},
    {"call_467924",ES2_COLLIDE_CALL,0x467924,0},
    {"call_471b30",ES2_COLLIDE_CALL,0x471b30,0},
    {"call_402188",ES2_COLLIDE_CALL,0x402188,0},
    {"call_496406",ES2_COLLIDE_CALL,0x496406,0},
    {"slot_18",ES2_COLLIDE_SLOT,0x18,0},
    {"call_47d32c",ES2_COLLIDE_CALL,0x47d32c,0},
    {"call_4628e0",ES2_COLLIDE_CALL,0x4628e0,0},
    {"call_46e180",ES2_COLLIDE_CALL,0x46e180,0},
    {"call_426a20",ES2_COLLIDE_CALL,0x426a20,0},
};
static callee_counter *pools_hook_counters(unsigned hook,unsigned *count) {
#define POOLS_COUNTERS(name) if(hook==ES2_HOOK_INDEX_##name) { \
        *count=sizeof(pools_##name##_counters)/sizeof(pools_##name##_counters[0]);return pools_##name##_counters; }
    POOLS_COUNTERS(smoke_ball_update)
    POOLS_COUNTERS(smoke_update)
    POOLS_COUNTERS(smoke_ball_init)
    POOLS_COUNTERS(debris_update)
    POOLS_COUNTERS(ground_height)
    POOLS_COUNTERS(meteor_update)
#undef POOLS_COUNTERS
    *count=0;return NULL;
}
static const BYTE pools_smoke_ball_update_sig[]={0x55,0x8b,0xec,0x53,0x56,0x57,0x8b,0x5d,0x08};
static const BYTE pools_smoke_update_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xbc,0xb8,0x9a,0x7f,0x49,0x00};
static const BYTE pools_smoke_ball_init_sig[]={0x55,0x8b,0xec,0x53,0x8b,0x15,0x78,0x96,0x4a,0x00};
static const BYTE pools_debris_update_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xbc,0xb8,0x10,0x7e,0x49,0x00};
static const BYTE pools_ground_height_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x50,0x8b,0x15,0xe0,0x0b,0x4a,0x00};
static const BYTE pools_meteor_update_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xcc,0xb8,0xb0,0x81,0x49,0x00};
static const BYTE pools_push_lens[]={1,2,1,1},pools_frame_lens[]={1,2,3},pools_table_lens[]={1,2,1,6};
#define POOLS_EXT_FLAG(name,unsupported) \
    static const es2_hook_extension pools_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,pools_##name,unsupported}; \
    static uint32_t __cdecl pools_##name##32(const uint32_t *a,void *u) {return (uint32_t)pools_##name(a,u,weapons_no_registers);}
#define POOLS_EXT(name) POOLS_EXT_FLAG(name,TRUE)
POOLS_EXT(smoke_ball_update)
POOLS_EXT(smoke_update)
POOLS_EXT(smoke_ball_init)
POOLS_EXT(debris_update)
POOLS_EXT_FLAG(ground_height,FALSE)
POOLS_EXT(meteor_update)
#undef POOLS_EXT
#undef POOLS_EXT_FLAG
static es2_hook_descriptor pools_hook_descriptors[]={
    {ES2_HOOK_ID(smoke_ball_update),pools_smoke_ball_update_sig,9,5,pools_push_lens,4,1,pools_smoke_ball_update32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(smoke_update),pools_smoke_update_sig,11,6,pools_frame_lens,3,1,pools_smoke_update32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(smoke_ball_init),pools_smoke_ball_init_sig,10,10,pools_table_lens,4,3,pools_smoke_ball_init32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(debris_update),pools_debris_update_sig,11,6,pools_frame_lens,3,1,pools_debris_update32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(ground_height),pools_ground_height_sig,13,6,pools_frame_lens,3,1,pools_ground_height32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(meteor_update),pools_meteor_update_sig,11,6,pools_frame_lens,3,1,pools_meteor_update32,NULL,NULL,NULL,0},
};
#endif
