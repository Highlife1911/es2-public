#ifndef ES2_EFFECT_POOLS_HOOKS_H
#define ES2_EFFECT_POOLS_HOOKS_H
/* effect_pools group (docs/reverse-engineering/mission_load.md,
 * section "effect pools with resources"): the stage-2 callbacks
 * explosion_pools 407b54, debris_pools 408860, meteor_pools 409a34 and
 * fire_pools 46b1a8 (registered by 401d64(fn, 2) at 408296, 408eae, 409f8a
 * and 46b872, called once per process through `call [ebx]` at 401dac from
 * 401d94(2) at 4617b8 inside the mission load), the smoke-ball set-up
 * smoke_balls 408fb8 (called by smoke_pools at 409164), the flat-object
 * pool flat_pools 4097a8 (called by the world loader 42e010 at 42e6dc), the
 * debris set debris_set 40874c (called by base_types at 4067e1,
 * debris_pools at 408907 and mech_type_load at 420483) and its record
 * reader debris_record 4083f8 (called by debris_set at 40880e)
 * (core/src/effect_pools.c). All are load-time functions, live only with
 * the early arming (the default).
 *
 * Every game call goes through the weapons guest bridge and callee observer
 * (weapons_graph) with the original's EAX/EDX/ECX: the runtime record 49630c
 * gets the synthetic EBP (..._FRAME in a zeroed scratch, the stream objects,
 * name buffers and locals at the original's offsets) through
 * weapons_impact_guest_call, and the adapter restores FS:0 to the record's
 * saved head (the returned EAX, or ECX for debris_pools and debris_set) as
 * the original's epilogue does. The callees stay game code with their own
 * modes: the runtime (49630c, new 47749c, new[] 4774f0, 49667c with the
 * element constructor 46bbc1), the RTL thunks 4967ea and 496820, the path
 * builder 492bec, the stream (472f14, the read method at slot +18h, the
 * hooks stream_read 4735b8 and stream_destroy 472f64), the error report
 * 479c78, the resources (474cd8, 45cdf4, 46a03c, 48349c), the hook
 * pool_create 471ad8 and the group's own debris_set and debris_record.
 *
 * EDX:EAX exact. ECX is dead after each hook (mission_load.md, "effect
 * pools with resources"): the dispatcher reads no register after
 * 401dac and the next callbacks load EAX and pass EDX/ECX only through
 * 49630c into new; smoke_pools, the world loader and debris_set pop or
 * overwrite ECX; after debris_set the callers overwrite it, return
 * (mech_type_load, whose own ECX the framework returns) or pass it to
 * stream_destroy, which writes ECX before reading it or hands it only to
 * cdecl callees. So the framework returns the entry ECX; the cores report
 * the original's ECX for the in-vitro comparison. No entry or exit shim:
 * no read of an uninitialized local or of stack residue (debris_record's
 * two words in the pushed ECX slot and every other local are written
 * before they are read; fire_pools' string scan stops at the terminator of
 * its five copied bytes). All refuse compare mode (file I/O, allocation,
 * resource loading, the runtime record); the load is accepted by the
 * after-load snapshot. No game instruction is embedded. */
#include "es2/core/effect_pools.h"
#define EFFECT_POOLS_GRAPH(index,size,frame) \
    BYTE scratch[size]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_##index); \
    weapons_impact_context c={(uint32_t)(uintptr_t)u,(uint32_t)(uintptr_t)(scratch+(frame))}; \
    g.call=weapons_impact_guest_call;g.context=&c
#define EFFECT_POOLS_FS0(head) do{uint32_t h_=(head);es2_machine_set_fs0(h_);}while(0)
static uint64_t __cdecl effect_pools_explosion_pools(const uint32_t *a,void *u,const uint32_t *r) {
    (void)a;EFFECT_POOLS_GRAPH(explosion_pools,ES2_EXPLOSION_POOLS_SCRATCH,ES2_EXPLOSION_POOLS_FRAME);
    const uint64_t result=es2_explosion_pools_registers(&math_native_memory,&g,r,NULL);
    EFFECT_POOLS_FS0((uint32_t)result);return result;
}
static uint64_t __cdecl effect_pools_debris_pools(const uint32_t *a,void *u,const uint32_t *r) {
    (void)a;EFFECT_POOLS_GRAPH(debris_pools,ES2_DEBRIS_POOLS_SCRATCH,ES2_DEBRIS_POOLS_FRAME);uint32_t ecx=0;
    const uint64_t result=es2_debris_pools_registers(&math_native_memory,&g,r,&ecx);
    EFFECT_POOLS_FS0(ecx);return result;
}
static uint64_t __cdecl effect_pools_debris_set(const uint32_t *a,void *u,const uint32_t *r) {
    EFFECT_POOLS_GRAPH(debris_set,ES2_DEBRIS_SET_SCRATCH,ES2_DEBRIS_SET_FRAME);uint32_t ecx=0;
    const uint64_t result=es2_debris_set_registers(&math_native_memory,&g,a[0],a[1],a[2],r,&ecx);
    EFFECT_POOLS_FS0(ecx);return result;
}
static uint64_t __cdecl effect_pools_debris_record(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_debris_record);
    return es2_debris_record_registers(&math_native_memory,&g,a[0],a[1],r,NULL);
}
static uint64_t __cdecl effect_pools_flat_pools(const uint32_t *a,void *u,const uint32_t *r) {
    EFFECT_POOLS_GRAPH(flat_pools,ES2_EFFECT_POOL_SCRATCH,ES2_EFFECT_POOL_FRAME);
    const uint64_t result=es2_flat_pools_registers(&math_native_memory,&g,a[0],r,NULL);
    EFFECT_POOLS_FS0((uint32_t)result);return result;
}
static uint64_t __cdecl effect_pools_meteor_pools(const uint32_t *a,void *u,const uint32_t *r) {
    (void)a;EFFECT_POOLS_GRAPH(meteor_pools,ES2_EFFECT_POOL_SCRATCH,ES2_EFFECT_POOL_FRAME);
    const uint64_t result=es2_meteor_pools_registers(&math_native_memory,&g,r,NULL);
    EFFECT_POOLS_FS0((uint32_t)result);return result;
}
static uint64_t __cdecl effect_pools_fire_pools(const uint32_t *a,void *u,const uint32_t *r) {
    (void)a;EFFECT_POOLS_GRAPH(fire_pools,ES2_FIRE_POOLS_SCRATCH,ES2_FIRE_POOLS_FRAME);
    const uint64_t result=es2_fire_pools_registers(&math_native_memory,&g,r,NULL);
    EFFECT_POOLS_FS0((uint32_t)result);return result;
}
static uint64_t __cdecl effect_pools_smoke_balls(const uint32_t *a,void *u,const uint32_t *r) {
    (void)a;EFFECT_POOLS_GRAPH(smoke_balls,ES2_EFFECT_POOL_SCRATCH,ES2_EFFECT_POOL_FRAME);
    const uint64_t result=es2_smoke_balls_registers(&math_native_memory,&g,r,NULL);
    EFFECT_POOLS_FS0((uint32_t)result);return result;
}
#undef EFFECT_POOLS_GRAPH
#undef EFFECT_POOLS_FS0
/* Callee counters (hk_<hook>_<suffix>), incremented only while the hook is
 * non-original. Keys: the callee VA, or the virtual slot. */
#define EFFECT_POOLS_CALL(va) {"call_" #va,ES2_COLLIDE_CALL,0x##va,0}
static callee_counter explosion_pools_counters[]={
    EFFECT_POOLS_CALL(49630c),EFFECT_POOLS_CALL(47749c),EFFECT_POOLS_CALL(471ad8),EFFECT_POOLS_CALL(4774f0),
    EFFECT_POOLS_CALL(49667c),EFFECT_POOLS_CALL(4967ea),EFFECT_POOLS_CALL(496820),EFFECT_POOLS_CALL(492bec),
    EFFECT_POOLS_CALL(45cdf4),EFFECT_POOLS_CALL(474cd8),EFFECT_POOLS_CALL(472f14),EFFECT_POOLS_CALL(479c78),
    {"slot_18",ES2_COLLIDE_SLOT,0x18,0},EFFECT_POOLS_CALL(4735b8),EFFECT_POOLS_CALL(472f64),
};
static callee_counter debris_pools_counters[]={
    EFFECT_POOLS_CALL(49630c),EFFECT_POOLS_CALL(47749c),EFFECT_POOLS_CALL(471ad8),EFFECT_POOLS_CALL(4774f0),
    EFFECT_POOLS_CALL(40874c),
};
static callee_counter debris_set_counters[]={
    EFFECT_POOLS_CALL(49630c),EFFECT_POOLS_CALL(492bec),EFFECT_POOLS_CALL(474cd8),EFFECT_POOLS_CALL(472f14),
    EFFECT_POOLS_CALL(479c78),{"slot_18",ES2_COLLIDE_SLOT,0x18,0},EFFECT_POOLS_CALL(4774f0),EFFECT_POOLS_CALL(4083f8),
    EFFECT_POOLS_CALL(472f64),
};
static callee_counter debris_record_counters[]={
    {"slot_18",ES2_COLLIDE_SLOT,0x18,0},EFFECT_POOLS_CALL(4774f0),
};
static callee_counter flat_pools_counters[]={
    EFFECT_POOLS_CALL(49630c),EFFECT_POOLS_CALL(492bec),EFFECT_POOLS_CALL(474cd8),EFFECT_POOLS_CALL(45cdf4),
    EFFECT_POOLS_CALL(46a03c),EFFECT_POOLS_CALL(48349c),EFFECT_POOLS_CALL(47749c),EFFECT_POOLS_CALL(471ad8),
    EFFECT_POOLS_CALL(4774f0),
};
static callee_counter meteor_pools_counters[]={
    EFFECT_POOLS_CALL(49630c),EFFECT_POOLS_CALL(47749c),EFFECT_POOLS_CALL(471ad8),EFFECT_POOLS_CALL(4774f0),
    EFFECT_POOLS_CALL(492bec),EFFECT_POOLS_CALL(474cd8),EFFECT_POOLS_CALL(45cdf4),
};
static callee_counter fire_pools_counters[]={
    EFFECT_POOLS_CALL(49630c),EFFECT_POOLS_CALL(47749c),EFFECT_POOLS_CALL(471ad8),EFFECT_POOLS_CALL(4774f0),
    EFFECT_POOLS_CALL(4967ea),EFFECT_POOLS_CALL(492bec),EFFECT_POOLS_CALL(474cd8),EFFECT_POOLS_CALL(472f14),
    EFFECT_POOLS_CALL(479c78),{"slot_18",ES2_COLLIDE_SLOT,0x18,0},EFFECT_POOLS_CALL(472f64),EFFECT_POOLS_CALL(49667c),
    EFFECT_POOLS_CALL(496820),EFFECT_POOLS_CALL(45cdf4),
};
static callee_counter smoke_balls_counters[]={
    EFFECT_POOLS_CALL(49630c),EFFECT_POOLS_CALL(47749c),EFFECT_POOLS_CALL(471ad8),EFFECT_POOLS_CALL(4774f0),
    EFFECT_POOLS_CALL(492bec),EFFECT_POOLS_CALL(474cd8),EFFECT_POOLS_CALL(45cdf4),
};
#undef EFFECT_POOLS_CALL
static callee_counter *effect_pools_hook_counters(unsigned hook,unsigned *count) {
#define EFFECT_POOLS_COUNTERS(name) \
    if(hook==ES2_HOOK_INDEX_##name){*count=sizeof(name##_counters)/sizeof(name##_counters[0]);return name##_counters;}
    EFFECT_POOLS_COUNTERS(explosion_pools) EFFECT_POOLS_COUNTERS(debris_pools) EFFECT_POOLS_COUNTERS(debris_set)
    EFFECT_POOLS_COUNTERS(debris_record) EFFECT_POOLS_COUNTERS(flat_pools) EFFECT_POOLS_COUNTERS(meteor_pools)
    EFFECT_POOLS_COUNTERS(fire_pools) EFFECT_POOLS_COUNTERS(smoke_balls)
#undef EFFECT_POOLS_COUNTERS
    *count=0;return NULL;
}
/* Prologues: push ebp; mov ebp,esp; add esp,-N (83 c4 ib), relocated 6
 * bytes (1,2,3), then mov eax,<descriptor> (b8 id), push ebx/esi/edi or a
 * call, which stay in place; 4083f8: push ebp; mov ebp,esp; push ecx;
 * push ebx, relocated 5 bytes (1,2,1,1); push esi; push edi stay. */
static const BYTE explosion_pools_sig[]={0x55,0x8b,0xec,0x83,0xc4,0x94,0xb8,0xe0,0x7b,0x49,0x00,0x53};
static const BYTE debris_pools_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xd8,0xb8,0xd4,0x7d,0x49,0x00};
static const BYTE debris_set_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xb0,0x53,0x56,0x57,0x8b,0x7d,0x0c};
static const BYTE debris_record_sig[]={0x55,0x8b,0xec,0x51,0x53,0x56,0x57,0x8b,0x75,0x0c};
static const BYTE flat_pools_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xd8,0x53,0x56,0x57,0xbf,0x9e,0x96,0x4a,0x00};
static const BYTE meteor_pools_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xd8,0xb8,0x34,0x81,0x49,0x00,0x53};
static const BYTE fire_pools_sig[]={0x55,0x8b,0xec,0x83,0xc4,0x8c,0xb8,0xa9,0x0a,0x4a,0x00,0x53};
static const BYTE smoke_balls_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xd8,0xb8,0x12,0x7f,0x49,0x00,0x53};
static const BYTE effect_pools_lens[]={1,2,3},debris_record_lens[]={1,2,1,1};
#define EFFECT_POOLS_EXTENSION(name) \
static const es2_hook_extension effect_pools_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0, \
    effect_pools_##name,TRUE}; \
static uint32_t __cdecl effect_pools_##name##32(const uint32_t *a,void *u) { \
    return (uint32_t)effect_pools_##name(a,u,weapons_no_registers); }
EFFECT_POOLS_EXTENSION(explosion_pools)
EFFECT_POOLS_EXTENSION(debris_pools)
EFFECT_POOLS_EXTENSION(debris_set)
EFFECT_POOLS_EXTENSION(debris_record)
EFFECT_POOLS_EXTENSION(flat_pools)
EFFECT_POOLS_EXTENSION(meteor_pools)
EFFECT_POOLS_EXTENSION(fire_pools)
EFFECT_POOLS_EXTENSION(smoke_balls)
#undef EFFECT_POOLS_EXTENSION
static es2_hook_descriptor effect_pools_hook_descriptors[]={
    {ES2_HOOK_ID(explosion_pools),explosion_pools_sig,12,6,effect_pools_lens,3,0,effect_pools_explosion_pools32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(debris_pools),debris_pools_sig,11,6,effect_pools_lens,3,0,effect_pools_debris_pools32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(debris_set),debris_set_sig,12,6,effect_pools_lens,3,3,effect_pools_debris_set32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(debris_record),debris_record_sig,10,5,debris_record_lens,4,2,effect_pools_debris_record32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(flat_pools),flat_pools_sig,14,6,effect_pools_lens,3,1,effect_pools_flat_pools32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(meteor_pools),meteor_pools_sig,12,6,effect_pools_lens,3,0,effect_pools_meteor_pools32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(fire_pools),fire_pools_sig,12,6,effect_pools_lens,3,0,effect_pools_fire_pools32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(smoke_balls),smoke_balls_sig,12,6,effect_pools_lens,3,0,effect_pools_smoke_balls32,NULL,NULL,NULL,0},
};
#endif
