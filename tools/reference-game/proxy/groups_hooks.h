#ifndef ES2_GROUPS_HOOKS_H
#define ES2_GROUPS_HOOKS_H
/* Group activation and current-order update
 * (docs/reverse-engineering/mission_runtime.md, section "Group activation
 * and current-order update"). Core in core/src/groups.c. Every game
 * callee goes through the weapons guest bridge and callee observer
 * (weapons_graph), so these hooks refuse compare mode; group_activate's
 * runtime frame setup 49630c gets the synthetic EBP through
 * weapons_impact_guest_call, and the adapter restores FS:0 from the
 * record's saved head (the returned EAX), as the original's epilogue does.
 * No entry shim; no game instruction is embedded. */
#include "es2/core/groups.h"

static callee_counter group_activate_counters[]={
    {"call_49630c",ES2_COLLIDE_CALL,0x49630c,0},
    {"call_492ee0",ES2_COLLIDE_CALL,0x492ee0,0},
    {"call_42354c",ES2_COLLIDE_CALL,0x42354c,0},
    {"call_471b30",ES2_COLLIDE_CALL,0x471b30,0},
    {"call_409b44",ES2_COLLIDE_CALL,0x409b44,0},
    {"slot_78",ES2_COLLIDE_SLOT,0x78,0},
};
static callee_counter spawn_point_counters[]={
    {"call_47eb30",ES2_COLLIDE_CALL,0x47eb30,0},
    {"call_47ffe8",ES2_COLLIDE_CALL,0x47ffe8,0},
    {"slot_7c",ES2_COLLIDE_SLOT,0x7c,0},
    {"call_49288c",ES2_COLLIDE_CALL,0x49288c,0},
    {"call_404ae4",ES2_COLLIDE_CALL,0x404ae4,0},
    {"call_46ff90",ES2_COLLIDE_CALL,0x46ff90,0},
};
static callee_counter order_update_counters[]={
    {"call_4239fc",ES2_COLLIDE_CALL,0x4239fc,0},
    {"call_411cec",ES2_COLLIDE_CALL,0x411cec,0},
};
static callee_counter group_assess_counters[]={
    {"call_41b804",ES2_COLLIDE_CALL,0x41b804,0},
};
static callee_counter actor_destination_counters[]={
    {"call_4238d4",ES2_COLLIDE_CALL,0x4238d4,0},
};
/* order_actor and order_point are leaves without game calls. */
static callee_counter *groups_hook_counters(unsigned hook,unsigned *count) {
#define GROUPS_COUNTERS(name) if(hook==ES2_HOOK_INDEX_##name) { *count=sizeof(name##_counters)/sizeof(name##_counters[0]);return name##_counters; }
    GROUPS_COUNTERS(group_activate)
    GROUPS_COUNTERS(spawn_point)
    GROUPS_COUNTERS(order_update)
    GROUPS_COUNTERS(group_assess)
    GROUPS_COUNTERS(actor_destination)
#undef GROUPS_COUNTERS
    *count=0;return NULL;
}

static uint64_t __cdecl groups_order_actor(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;(void)r;return es2_order_actor_registers(&math_native_memory,a[0]);
}
static uint64_t __cdecl groups_order_point(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;(void)r;return es2_order_point_registers(&math_native_memory,a[0]);
}
#define GROUPS_ADAPTER1(name) \
static uint64_t __cdecl groups_##name(const uint32_t *a,void *u,const uint32_t *r) { \
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_##name); \
    return es2_##name##_registers(&math_native_memory,&g,a[0],r); }
GROUPS_ADAPTER1(actor_destination) GROUPS_ADAPTER1(group_assess) GROUPS_ADAPTER1(order_update)
#undef GROUPS_ADAPTER1
static uint64_t __cdecl groups_spawn_point(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[64]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_spawn_point);
    return es2_spawn_point_registers(&math_native_memory,&g,a[0],a[1],a[2],a[3],r);
}
/* The caller's ESI high word only reaches 42354c's angle slot (never read
 * above the low word) and EDX after mode 4/5 with one member (dead at the
 * only caller 45f7ec); the core gets 0. */
static uint64_t __cdecl groups_group_activate(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[256]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_group_activate);
    weapons_impact_context c={(uint32_t)(uintptr_t)u,(uint32_t)(uintptr_t)(scratch+208)};
    g.call=weapons_impact_guest_call;g.context=&c;
    uint64_t result=es2_group_activate_registers(&math_native_memory,&g,a[0],0,r);
    uint32_t restored=(uint32_t)result;es2_machine_set_fs0(restored);return result;
}
static const BYTE groups_group_activate_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xb8,0xb8,0xbc,0xa6,0x49,0x00};
static const BYTE groups_spawn_point_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xd4,0x33,0xc9};
static const BYTE groups_order_update_sig[]={0x55,0x8b,0xec,0x53,0x56,0x57,0x8b,0x5d,0x08};
static const BYTE groups_group_assess_sig[]={0x55,0x8b,0xec,0x53,0x56,0x8b,0x75,0x08};
static const BYTE groups_order_actor_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x8b,0x50,0x6c,0x8b,0x44,0x90,0x44};
static const BYTE groups_order_point_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x8b,0x50,0x6c,0x8b,0x54,0x90,0x44};
static const BYTE groups_actor_destination_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x0f,0xbf,0x90,0x3e,0x02,0x00,0x00};
static const BYTE groups_frame_lens[]={1,2,3},groups_saved_lens[]={1,2,1,1};
#define GROUPS_EXT(name) static const es2_hook_extension groups_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,groups_##name,TRUE};
#define GROUPS_LEGACY(name) static uint32_t __cdecl groups_##name##32(const uint32_t *a,void *u) {return (uint32_t)groups_##name(a,u,weapons_no_registers);}
#define GROUPS_BOTH(name) GROUPS_EXT(name) GROUPS_LEGACY(name)
GROUPS_BOTH(group_activate) GROUPS_BOTH(spawn_point) GROUPS_BOTH(order_update) GROUPS_BOTH(group_assess)
GROUPS_BOTH(order_actor) GROUPS_BOTH(order_point) GROUPS_BOTH(actor_destination)
#undef GROUPS_EXT
#undef GROUPS_LEGACY
#undef GROUPS_BOTH
static es2_hook_descriptor groups_hook_descriptors[]={
    {ES2_HOOK_ID(group_activate),groups_group_activate_sig,11,6,groups_frame_lens,3,1,groups_group_activate32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(spawn_point),groups_spawn_point_sig,8,6,groups_frame_lens,3,4,groups_spawn_point32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(order_update),groups_order_update_sig,9,5,groups_saved_lens,4,1,groups_order_update32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(group_assess),groups_group_assess_sig,8,5,groups_saved_lens,4,1,groups_group_assess32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(order_actor),groups_order_actor_sig,13,6,groups_frame_lens,3,1,groups_order_actor32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(order_point),groups_order_point_sig,13,6,groups_frame_lens,3,1,groups_order_point32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(actor_destination),groups_actor_destination_sig,13,6,groups_frame_lens,3,1,groups_actor_destination32,NULL,NULL,NULL,0},
};
#endif
