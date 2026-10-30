#ifndef ES2_POOL_ITER_HOOKS_H
#define ES2_POOL_ITER_HOOKS_H
/* Pool iterator (docs/reverse-engineering/main_loop.md, section
 * "pool iterator 471c70 / 471c18"): pool_prev 471c70 and pool_next
 * 471c18. pool_prev's steps to 471c18 (O = 0) and pool_next's debug report
 * (the CW3220 sprintf thunk 496796, only for a node below 1 MiB) go through
 * the weapons guest bridge and callee observer (weapons_graph) with the
 * original's registers, so a nested pool_next keeps its own mode; both
 * refuse compare mode. The report's buffer is 128 bytes of adapter
 * scratch (the original's 32-byte buffer overflows, see the doc). EDX:EAX
 * are exact on every path; ECX is the entry ECX, as in the original
 * outside the report; flags are dead after all 42 call sites (add esp,8).
 * No entry register or uninitialized local is read, so no entry shim. No
 * game instruction is embedded. */
#include "es2/core/pool_iter.h"
static uint64_t __cdecl pool_iter_pool_prev(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_pool_prev);
    return es2_pool_prev_registers(&math_native_memory,&g,a[0],a[1],r);
}
static uint64_t __cdecl pool_iter_pool_next(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[ES2_POOL_ITER_SCRATCH];es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_pool_next);
    return es2_pool_next_registers(&math_native_memory,&g,a[0],a[1],r);
}
/* Callee counters (hk_<hook>_<suffix>), incremented only while the hook is
 * non-original. */
static callee_counter pool_prev_counters[]={
    {"call_471c18",ES2_COLLIDE_CALL,0x471c18,0},
};
static callee_counter pool_next_counters[]={
    {"call_496796",ES2_COLLIDE_CALL,0x496796,0},
};
static callee_counter *pool_iter_hook_counters(unsigned hook,unsigned *count) {
    if(hook==ES2_HOOK_INDEX_pool_prev) { *count=1;return pool_prev_counters; }
    if(hook==ES2_HOOK_INDEX_pool_next) { *count=1;return pool_next_counters; }
    *count=0;return NULL;
}
/* push ebp; mov ebp,esp; push ebx; push esi; push edi; xor esi,esi */
static const BYTE pool_iter_pool_prev_sig[]={0x55,0x8b,0xec,0x53,0x56,0x57,0x33,0xf6};
/* push ebp; mov ebp,esp; add esp,-20h; push ebx; mov edx,[ebp+0Ch] */
static const BYTE pool_iter_pool_next_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xe0,0x53,0x8b,0x55,0x0c};
static const BYTE pool_iter_push_lens[]={1,2,1,1},pool_iter_frame_lens[]={1,2,3};
#define POOL_ITER_EXT(name) \
    static const es2_hook_extension pool_iter_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,pool_iter_##name,TRUE}; \
    static uint32_t __cdecl pool_iter_##name##32(const uint32_t *a,void *u) {return (uint32_t)pool_iter_##name(a,u,weapons_no_registers);}
POOL_ITER_EXT(pool_prev)
POOL_ITER_EXT(pool_next)
#undef POOL_ITER_EXT
static es2_hook_descriptor pool_iter_hook_descriptors[]={
    {ES2_HOOK_ID(pool_prev),pool_iter_pool_prev_sig,8,5,pool_iter_push_lens,4,2,pool_iter_pool_prev32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(pool_next),pool_iter_pool_next_sig,10,6,pool_iter_frame_lens,3,2,pool_iter_pool_next32,NULL,NULL,NULL,0},
};
#endif
