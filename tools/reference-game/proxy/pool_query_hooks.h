#ifndef ES2_POOL_QUERY_HOOKS_H
#define ES2_POOL_QUERY_HOOKS_H
/* pool_query group (docs/reverse-engineering/ai.md, section "pool object
 * query 404bc0"): pool_query 404bc0, the only callee of
 * object_line 404ca0 that was still original. Its game callees (the pool
 * step 471c70 = pool_prev and the object query 427da8 = object_query) go
 * through the weapons guest bridge and callee observer (weapons_graph)
 * with the original's registers, so the nested hooks keep their modes; the
 * hook refuses compare mode. The 240h-byte frame image (list, cursor,
 * result dword, EBP-8, count, iterator) lives in the adapter's zeroed
 * buffer, so a list past 140 entries overwrites the cursor and the 141st
 * object exactly as the original does; the never-written word EBP-8 reads
 * 0 here (stack residue in the original, reachable only past 141 pool
 * objects; no entry shim). No entry register is read; ECX after the hook
 * is dead at object_line's callers; EDX can be a frame address of 427da8
 * (the bridge run's instead of the original's), also dead there. No game
 * instruction is embedded. */
#include "es2/core/obstacle.h"
static uint64_t __cdecl pool_query_pool_query(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE frame[ES2_POOL_QUERY_FRAME]={0};es2_weapon_graph g=weapons_graph(u,frame,ES2_HOOK_INDEX_pool_query);
    return es2_pool_query_registers(&math_native_memory,&g,a[0],a[1],a[2],r);
}
/* Callee counters (hk_<hook>_<suffix>), incremented only while the hook is
 * non-original. */
static callee_counter pool_query_counters[]={
    {"call_471c70",ES2_COLLIDE_CALL,0x471c70,0},
    {"call_427da8",ES2_COLLIDE_CALL,0x427da8,0},
};
static callee_counter *pool_query_hook_counters(unsigned hook,unsigned *count) {
    if(hook==ES2_HOOK_INDEX_pool_query) { *count=sizeof(pool_query_counters)/sizeof(pool_query_counters[0]);return pool_query_counters; }
    *count=0;return NULL;
}
/* push ebp; mov ebp,esp; add esp,-240h; xor eax,eax */
static const BYTE pool_query_sig[]={0x55,0x8b,0xec,0x81,0xc4,0xc0,0xfd,0xff,0xff,0x33,0xc0};
static const BYTE pool_query_lens[]={1,2,6};
static const es2_hook_extension pool_query_pool_query_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,pool_query_pool_query,TRUE};
static uint32_t __cdecl pool_query_pool_query32(const uint32_t *a,void *u) {return (uint32_t)pool_query_pool_query(a,u,weapons_no_registers);}
static es2_hook_descriptor pool_query_hook_descriptors[]={
    {ES2_HOOK_ID(pool_query),pool_query_sig,11,9,pool_query_lens,3,3,pool_query_pool_query32,NULL,NULL,NULL,0},
};
#endif
