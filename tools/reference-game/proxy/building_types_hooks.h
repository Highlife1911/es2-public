#ifndef ES2_BUILDING_TYPES_HOOKS_H
#define ES2_BUILDING_TYPES_HOOKS_H
/* building_types group (docs/reverse-engineering/mission_load.md,
 * section "building types and pools"): the building module's
 * (base.cpp) stage-2 callback base_types 405fac and the building pools
 * base_pools 405df4 (core/src/building_types.c). base_types is registered
 * by 401d64(fn, 2) at 40696e and called once per process through
 * `call [ebx]` at 401dac from 401d94(2) at 4617b8 inside the mission load;
 * base_pools is called once per process by the world load 424308 at
 * 4252a0. Both are live only with the early arming (the default).
 *
 * Every game call goes through the weapons guest bridge and callee observer
 * (weapons_graph) with the original's EAX/EDX/ECX: the runtime record 49630c
 * gets the synthetic EBP (..._FRAME in a zeroed scratch, the four stream
 * objects and the locals at the original's offsets) through
 * weapons_impact_guest_call, and the adapter restores FS:0 to the record's
 * saved head (the returned EAX) as the original's epilogue does. The
 * callees stay game code with their own modes: the runtime (49630c, new
 * 47749c, new[] 4774f0, 49667c with the empty element constructors
 * 406a5c/406a64/406a6c/406a77/406a85), the path builder 492bec, the stream
 * (472f14, the read method at slot +18h, the hooks stream_read 4735b8 and
 * stream_destroy 472f64), the error report 479c78, the resources (474cd8,
 * 45cdf4, 46a03c, 48349c), the debris set 40874c and the hooks
 * collision_read 40ccf8 and pool_create 471ad8.
 *
 * EDX:EAX exact. ECX is dead after the dispatcher's indirect call (401dac
 * reads no register; light_pools, the next callback, loads EAX and keeps
 * EDX/ECX only through 49630c into new, which reads none) and after 4252a0
 * (POP ECX), so it is not returned (the framework returns the entry ECX).
 * base_pools pushes its count word over the caller's EBX high word as
 * pool_create's capacity slot; the adapter cannot see EBX and passes 0
 * (pool_create reads only the word, SI). No entry or exit shim: no read of
 * an uninitialized local or of stack residue (every local is written before
 * it is read; the length byte f-E9h and the stream objects by the callees
 * that receive their addresses). Both refuse compare mode (file I/O,
 * allocation, resource loading, the runtime record); the load is accepted
 * by the after-load snapshot. No game instruction is embedded. */
#include "es2/core/building_types.h"
static uint64_t __cdecl building_types_base_types(const uint32_t *a,void *u,const uint32_t *r) {
    (void)a;BYTE scratch[ES2_BASE_TYPES_SCRATCH]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_base_types);
    weapons_impact_context c={(uint32_t)(uintptr_t)u,(uint32_t)(uintptr_t)(scratch+ES2_BASE_TYPES_FRAME)};
    g.call=weapons_impact_guest_call;g.context=&c;
    uint64_t result=es2_base_types_registers(&math_native_memory,&g,r,NULL);
    uint32_t head=(uint32_t)result;es2_machine_set_fs0(head);return result;
}
static uint64_t __cdecl building_types_base_pools(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[ES2_BASE_POOLS_SCRATCH]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_base_pools);
    weapons_impact_context c={(uint32_t)(uintptr_t)u,(uint32_t)(uintptr_t)(scratch+ES2_BASE_POOLS_FRAME)};
    g.call=weapons_impact_guest_call;g.context=&c;
    uint64_t result=es2_base_pools_registers(&math_native_memory,&g,a[0],0,r,NULL);
    uint32_t head=(uint32_t)result;es2_machine_set_fs0(head);return result;
}
/* Callee counters (hk_<hook>_<suffix>), incremented only while the hook is
 * non-original. Keys: the callee VA, or the virtual slot. */
static callee_counter base_types_counters[]={
    {"call_49630c",ES2_COLLIDE_CALL,0x49630c,0},
    {"call_492bec",ES2_COLLIDE_CALL,0x492bec,0},
    {"call_472f14",ES2_COLLIDE_CALL,0x472f14,0},
    {"call_479c78",ES2_COLLIDE_CALL,0x479c78,0},
    {"slot_18",ES2_COLLIDE_SLOT,0x18,0},
    {"call_49667c",ES2_COLLIDE_CALL,0x49667c,0},
    {"call_4735b8",ES2_COLLIDE_CALL,0x4735b8,0},
    {"call_4774f0",ES2_COLLIDE_CALL,0x4774f0,0},
    {"call_472f64",ES2_COLLIDE_CALL,0x472f64,0},
    {"call_40ccf8",ES2_COLLIDE_CALL,0x40ccf8,0},
    {"call_474cd8",ES2_COLLIDE_CALL,0x474cd8,0},
    {"call_45cdf4",ES2_COLLIDE_CALL,0x45cdf4,0},
    {"call_46a03c",ES2_COLLIDE_CALL,0x46a03c,0},
    {"call_48349c",ES2_COLLIDE_CALL,0x48349c,0},
    {"call_40874c",ES2_COLLIDE_CALL,0x40874c,0},
};
static callee_counter base_pools_counters[]={
    {"call_49630c",ES2_COLLIDE_CALL,0x49630c,0},
    {"call_47749c",ES2_COLLIDE_CALL,0x47749c,0},
    {"call_471ad8",ES2_COLLIDE_CALL,0x471ad8,0},
    {"call_4774f0",ES2_COLLIDE_CALL,0x4774f0,0},
};
static callee_counter *building_types_hook_counters(unsigned hook,unsigned *count) {
    if(hook==ES2_HOOK_INDEX_base_types){*count=sizeof(base_types_counters)/sizeof(base_types_counters[0]);return base_types_counters;}
    if(hook==ES2_HOOK_INDEX_base_pools){*count=sizeof(base_pools_counters)/sizeof(base_pools_counters[0]);return base_pools_counters;}
    *count=0;return NULL;
}
/* 405fac: push ebp; mov ebp,esp; add esp,-11Ch (imm32); push ebx; push
 * esi; push edi; lea edi,[ebp-30h]; mov eax,49766Fh. Relocated 9 bytes
 * (1,2,6). 405df4: push ebp; mov ebp,esp; add esp,-28h; mov eax,497603h;
 * push ebx. Relocated 6 bytes (1,2,3). What follows stays in place. */
static const BYTE base_types_sig[]={0x55,0x8b,0xec,0x81,0xc4,0xe4,0xfe,0xff,0xff,0x53,0x56,0x57,0x8d,0x7d,0xd0,
    0xb8,0x6f,0x76,0x49,0x00};
static const BYTE base_pools_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xd8,0xb8,0x03,0x76,0x49,0x00,0x53};
static const BYTE base_types_lens[]={1,2,6},base_pools_lens[]={1,2,3};
static const es2_hook_extension building_types_base_types_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,
    building_types_base_types,TRUE};
static const es2_hook_extension building_types_base_pools_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,
    building_types_base_pools,TRUE};
static uint32_t __cdecl building_types_base_types32(const uint32_t *a,void *u) {
    return (uint32_t)building_types_base_types(a,u,weapons_no_registers);
}
static uint32_t __cdecl building_types_base_pools32(const uint32_t *a,void *u) {
    return (uint32_t)building_types_base_pools(a,u,weapons_no_registers);
}
static es2_hook_descriptor building_types_hook_descriptors[]={
    {ES2_HOOK_ID(base_types),base_types_sig,20,9,base_types_lens,3,0,building_types_base_types32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(base_pools),base_pools_sig,12,6,base_pools_lens,3,1,building_types_base_pools32,NULL,NULL,NULL,0},
};
#endif
