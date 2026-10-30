#ifndef ES2_MISSION_POOLS_HOOKS_H
#define ES2_MISSION_POOLS_HOOKS_H
/* mission_pools group (docs/reverse-engineering/mission_load.md,
 * section "L8 stage-2 pool callbacks"): the pool-only callbacks of
 * stage_dispatch(2), light_pools 4076e4, smoke_pools 4090cc and
 * projectile_pools 40a060 (core/src/mission_load.c). They run once per
 * process, from 401d94(2) at 4617b8 inside the mission load, so they are
 * live only with the early arming (the default).
 *
 * Every game call goes through the weapons guest bridge and callee observer
 * (weapons_graph) with the original's EAX/EDX/ECX: the runtime frame setup
 * 49630c gets the synthetic EBP (ES2_STAGE_POOLS_FRAME in a zeroed scratch)
 * through weapons_impact_guest_call, and the adapter restores FS:0 to the
 * record's saved head (the returned EAX) as the original's epilogue does;
 * new 47749c and new[] 4774f0 (runtime), pool_create 471ad8 (a hook of
 * group mission_load, keeps its own mode), the light table constructor
 * 406e44 (graphics-library lights) and the smoke-ball set-up 408fb8 (shape
 * and bitmap loading, assets_hooks.h) stay game code. The leaf 406ee4 is computed by
 * the core. EDX:EAX exact; the dispatcher reads no register after its
 * indirect call (401dac), so ECX is not returned (the framework returns
 * the entry ECX). No entry or exit shim: no read of an uninitialized local
 * or of stack residue. All three refuse compare mode (allocation through
 * new/new[] and pool_create, construction count, resource loading); the
 * load is accepted by the after-load snapshot. No game instruction is
 * embedded. */
#include "es2/core/mission_load.h"
#define MISSION_POOLS_ADAPTER(name) \
static uint64_t __cdecl mission_pools_##name(const uint32_t *a,void *u,const uint32_t *r) { \
    (void)a;BYTE scratch[ES2_STAGE_POOLS_SCRATCH]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_##name); \
    weapons_impact_context c={(uint32_t)(uintptr_t)u,(uint32_t)(uintptr_t)(scratch+ES2_STAGE_POOLS_FRAME)}; \
    g.call=weapons_impact_guest_call;g.context=&c; \
    uint64_t result=es2_##name##_registers(&math_native_memory,&g,r,NULL); \
    uint32_t head=(uint32_t)result;__asm__ volatile("movl %0,%%fs:0"::"r"(head):"memory");return result; }
MISSION_POOLS_ADAPTER(light_pools)
MISSION_POOLS_ADAPTER(smoke_pools)
MISSION_POOLS_ADAPTER(projectile_pools)
#undef MISSION_POOLS_ADAPTER
/* Callee counters (hk_<hook>_<suffix>), incremented only while the hook is
 * non-original. Keys: the callee VA. */
static callee_counter light_pools_counters[]={
    {"call_49630c",ES2_COLLIDE_CALL,0x49630c,0},
    {"call_47749c",ES2_COLLIDE_CALL,0x47749c,0},
    {"call_406e44",ES2_COLLIDE_CALL,0x406e44,0},
    {"call_471ad8",ES2_COLLIDE_CALL,0x471ad8,0},
    {"call_4774f0",ES2_COLLIDE_CALL,0x4774f0,0},
};
static callee_counter smoke_pools_counters[]={
    {"call_49630c",ES2_COLLIDE_CALL,0x49630c,0},
    {"call_47749c",ES2_COLLIDE_CALL,0x47749c,0},
    {"call_471ad8",ES2_COLLIDE_CALL,0x471ad8,0},
    {"call_4774f0",ES2_COLLIDE_CALL,0x4774f0,0},
    {"call_408fb8",ES2_COLLIDE_CALL,0x408fb8,0},
};
static callee_counter projectile_pools_counters[]={
    {"call_49630c",ES2_COLLIDE_CALL,0x49630c,0},
    {"call_47749c",ES2_COLLIDE_CALL,0x47749c,0},
    {"call_471ad8",ES2_COLLIDE_CALL,0x471ad8,0},
    {"call_4774f0",ES2_COLLIDE_CALL,0x4774f0,0},
};
#define MISSION_POOLS_COUNTERS(name) \
    if(hook==ES2_HOOK_INDEX_##name){*count=sizeof(name##_counters)/sizeof(name##_counters[0]);return name##_counters;}
static callee_counter *mission_pools_hook_counters(unsigned hook,unsigned *count) {
    MISSION_POOLS_COUNTERS(light_pools)
    MISSION_POOLS_COUNTERS(smoke_pools)
    MISSION_POOLS_COUNTERS(projectile_pools)
    *count=0;return NULL;
}
#undef MISSION_POOLS_COUNTERS
/* 4076e4: push ebp; mov ebp,esp; add esp,-2Ch; mov eax,497b16h; push ebx.
 * 4090cc: push ebp; mov ebp,esp; add esp,-28h; mov eax,497f5eh.
 * 40a060: push ebp; mov ebp,esp; add esp,-28h; mov eax,4982ech.
 * Relocated 6 bytes each (1,2,3); the descriptor load and the call of
 * 49630c that follow stay in place. */
static const BYTE light_pools_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xd4,0xb8,0x16,0x7b,0x49,0x00,0x53};
static const BYTE smoke_pools_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xd8,0xb8,0x5e,0x7f,0x49,0x00};
static const BYTE projectile_pools_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xd8,0xb8,0xec,0x82,0x49,0x00};
static const BYTE mission_pools_frame_lens[]={1,2,3};
#define MISSION_POOLS_EXT(name) \
    static const es2_hook_extension mission_pools_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,mission_pools_##name,TRUE}; \
    static uint32_t __cdecl mission_pools_##name##32(const uint32_t *a,void *u) {return (uint32_t)mission_pools_##name(a,u,weapons_no_registers);}
MISSION_POOLS_EXT(light_pools)
MISSION_POOLS_EXT(smoke_pools)
MISSION_POOLS_EXT(projectile_pools)
#undef MISSION_POOLS_EXT
static es2_hook_descriptor mission_pools_hook_descriptors[]={
    {ES2_HOOK_ID(light_pools),light_pools_sig,12,6,mission_pools_frame_lens,3,0,mission_pools_light_pools32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(smoke_pools),smoke_pools_sig,11,6,mission_pools_frame_lens,3,0,mission_pools_smoke_pools32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(projectile_pools),projectile_pools_sig,11,6,mission_pools_frame_lens,3,0,mission_pools_projectile_pools32,NULL,NULL,NULL,0},
};
#endif
