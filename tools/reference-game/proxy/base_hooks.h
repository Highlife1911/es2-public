#ifndef ES2_BASE_HOOKS_H
#define ES2_BASE_HOOKS_H
/* Building update (docs/reverse-engineering/vtables.md, section
 * "base_update: contract"): base_update 403ca8 (slot +18h of BASE
 * and RADAR_BASE, row 10 of the logic frame), base_parts 403914 (with the
 * pure 403668 computed inside), base_part_chain 403890 (recursive),
 * base_part_blast 4036c0 and base_part_debris 40379c. Every game callee
 * goes through the weapons guest bridge and callee observer
 * (weapons_graph) with the original's registers, so the nested hooks
 * (timer_word_step, actor_animate, rng_bounded, euler_matrix,
 * transform_point, pool_alloc, explosion_create, slot +74h building_part
 * and the group's own functions) keep their modes and the FIRE creation
 * 46b430/46b48c, the debris calls 4089bc/408530 and the point transform
 * 4801d4 run as original code. The runtime frame setup 49630c of
 * base_parts and base_part_blast gets the synthetic EBP (scratch +
 * ES2_BASE_FRAME) through weapons_impact_guest_call; the adapters restore
 * FS:0 to the record's saved head, as the originals' epilogues do. ECX
 * after the hooks is dead at every caller (vtables.md). All five refuse
 * compare mode. No entry shim, no game instruction embedded. */
#include "es2/core/base_update.h"
#define BASE_FRAME_GRAPH(index) \
    BYTE scratch[ES2_BASE_SCRATCH]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_##index); \
    weapons_impact_context c={(uint32_t)(uintptr_t)u,(uint32_t)(uintptr_t)(scratch+ES2_BASE_FRAME)}; \
    g.call=weapons_impact_guest_call;g.context=&c; \
    uint32_t head;__asm__ volatile("movl %%fs:0,%0":"=r"(head))
#define BASE_FRAME_RESTORE() __asm__ volatile("movl %0,%%fs:0"::"r"(head):"memory")
static uint64_t __cdecl base_base_update(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_base_update);
    return es2_base_update_registers(&math_native_memory,&g,a[0],r);
}
static uint64_t __cdecl base_base_parts(const uint32_t *a,void *u,const uint32_t *r) {
    BASE_FRAME_GRAPH(base_parts);
    uint64_t result=es2_base_parts_registers(&math_native_memory,&g,a[0],r);
    BASE_FRAME_RESTORE();return result;
}
static uint64_t __cdecl base_base_part_chain(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_base_part_chain);
    return es2_base_part_chain_registers(&math_native_memory,&g,a[0],a[1],r);
}
static uint64_t __cdecl base_base_part_blast(const uint32_t *a,void *u,const uint32_t *r) {
    BASE_FRAME_GRAPH(base_part_blast);
    uint64_t result=es2_base_part_blast_registers(&math_native_memory,&g,a[0],a[1],a[2],a[3],r);
    BASE_FRAME_RESTORE();return result;
}
static uint64_t __cdecl base_base_part_debris(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[ES2_BASE_SCRATCH]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_base_part_debris);
    return es2_base_part_debris_registers(&math_native_memory,&g,a[0],a[1],r);
}
#undef BASE_FRAME_GRAPH
#undef BASE_FRAME_RESTORE
/* Callee counters (hk_<hook>_<suffix>), incremented only while the hook is
 * non-original. Keys: the callee VA, or the vtable offset for slots. */
static callee_counter base_update_counters[]={
    {"call_403914",ES2_COLLIDE_CALL,0x403914,0},
    {"call_467a48",ES2_COLLIDE_CALL,0x467a48,0},
    {"call_402604",ES2_COLLIDE_CALL,0x402604,0},
};
static callee_counter base_parts_counters[]={
    {"call_49630c",ES2_COLLIDE_CALL,0x49630c,0},
    {"call_467a48",ES2_COLLIDE_CALL,0x467a48,0},
    {"call_46b430",ES2_COLLIDE_CALL,0x46b430,0},
    {"call_46b48c",ES2_COLLIDE_CALL,0x46b48c,0},
    {"call_4036c0",ES2_COLLIDE_CALL,0x4036c0,0},
    {"call_40379c",ES2_COLLIDE_CALL,0x40379c,0},
    {"call_403890",ES2_COLLIDE_CALL,0x403890,0},
    {"call_492f24",ES2_COLLIDE_CALL,0x492f24,0},
    {"call_47ebb8",ES2_COLLIDE_CALL,0x47ebb8,0},
    {"call_48043c",ES2_COLLIDE_CALL,0x48043c,0},
    {"call_471b30",ES2_COLLIDE_CALL,0x471b30,0},
    {"call_407f1c",ES2_COLLIDE_CALL,0x407f1c,0},
};
static callee_counter base_part_chain_counters[]={
    {"slot_74",ES2_COLLIDE_SLOT,0x74,0},
    {"call_403890",ES2_COLLIDE_CALL,0x403890,0},
};
static callee_counter base_part_blast_counters[]={
    {"call_49630c",ES2_COLLIDE_CALL,0x49630c,0},
    {"call_47ebb8",ES2_COLLIDE_CALL,0x47ebb8,0},
    {"call_4801d4",ES2_COLLIDE_CALL,0x4801d4,0},
    {"call_471b30",ES2_COLLIDE_CALL,0x471b30,0},
    {"call_407f1c",ES2_COLLIDE_CALL,0x407f1c,0},
};
static callee_counter base_part_debris_counters[]={
    {"call_47ebb8",ES2_COLLIDE_CALL,0x47ebb8,0},
    {"call_4801d4",ES2_COLLIDE_CALL,0x4801d4,0},
    {"call_4089bc",ES2_COLLIDE_CALL,0x4089bc,0},
    {"call_408530",ES2_COLLIDE_CALL,0x408530,0},
};
static callee_counter *base_hook_counters(unsigned hook,unsigned *count) {
#define BASE_COUNTERS(name) if(hook==ES2_HOOK_INDEX_##name) { \
        *count=sizeof(name##_counters)/sizeof(name##_counters[0]);return name##_counters; }
    BASE_COUNTERS(base_update)
    BASE_COUNTERS(base_parts)
    BASE_COUNTERS(base_part_chain)
    BASE_COUNTERS(base_part_blast)
    BASE_COUNTERS(base_part_debris)
#undef BASE_COUNTERS
    *count=0;return NULL;
}
/* push ebp; mov ebp,esp; push ebx; push esi; push edi; mov ebx,[ebp+8] */
static const BYTE base_update_sig[]={0x55,0x8b,0xec,0x53,0x56,0x57,0x8b,0x5d,0x08};
/* push ebp; mov ebp,esp; add esp,-64h; mov eax,497485h */
static const BYTE base_parts_sig[]={0x55,0x8b,0xec,0x83,0xc4,0x9c,0xb8,0x85,0x74,0x49,0x00};
static const BYTE base_part_chain_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xf8,0x53,0x56,0x57};
static const BYTE base_part_blast_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xc8,0x53,0x56,0x57};
static const BYTE base_part_debris_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xec,0x53,0x56,0x57};
static const BYTE base_frame_lens[]={1,2,3},base_push_lens[]={1,2,1,1};
#define BASE_EXT(name) \
    static const es2_hook_extension base_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,base_##name,TRUE}; \
    static uint32_t __cdecl base_##name##32(const uint32_t *a,void *u) {return (uint32_t)base_##name(a,u,weapons_no_registers);}
BASE_EXT(base_update)
BASE_EXT(base_parts)
BASE_EXT(base_part_chain)
BASE_EXT(base_part_blast)
BASE_EXT(base_part_debris)
#undef BASE_EXT
static es2_hook_descriptor base_hook_descriptors[]={
    {ES2_HOOK_ID(base_update),base_update_sig,9,5,base_push_lens,4,1,base_base_update32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(base_parts),base_parts_sig,11,6,base_frame_lens,3,1,base_base_parts32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(base_part_chain),base_part_chain_sig,9,6,base_frame_lens,3,2,base_base_part_chain32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(base_part_blast),base_part_blast_sig,9,6,base_frame_lens,3,4,base_base_part_blast32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(base_part_debris),base_part_debris_sig,9,6,base_frame_lens,3,2,base_base_part_debris32,NULL,NULL,NULL,0},
};
#endif
