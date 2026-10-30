#ifndef ES2_COL_HOOKS_H
#define ES2_COL_HOOKS_H
/* COL traversal of the actor hit tests
 * (docs/reverse-engineering/hit_tests.md, section "COL
 * traversal"): col_query 40c9d4, col_group
 * 40c8fc, col_shape 40c8c8 and col_bound 40c4c4. Core in core/src/col.c.
 * Every game callee goes through the weapons guest bridge and callee
 * observer (weapons_graph) with the original's registers, so the group's
 * own nested functions and the hooks matrix_compose, dist_octagonal and
 * sphere_hits keep their modes; the shape node lookup (virtual +20h of
 * [[actor+34h]+4]) and the point transform 4801d4 run as original code.
 * col_group's composed matrix and col_bound's transformed center live in
 * the adapter's zeroed scratch; col_group stores the scratch address in
 * 4a980c as the original stores its frame address (read only by col_bound
 * and sphere_hits inside col_group's shape loop). ECX after the hooks is
 * dead at every caller. All four refuse compare mode. No entry shim, no
 * game instruction embedded. */
#include "es2/core/col.h"
static uint64_t __cdecl col_col_query(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_col_query);
    return es2_col_query_registers(&math_native_memory,&g,a[0],a[1],a[2],a[3],a[4],r);
}
static uint64_t __cdecl col_col_group(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[ES2_COL_SCRATCH]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_col_group);
    return es2_col_group_registers(&math_native_memory,&g,a[0],a[1],a[2],a[3],r);
}
static uint64_t __cdecl col_col_shape(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_col_shape);
    return es2_col_shape_registers(&math_native_memory,&g,a[0],r);
}
static uint64_t __cdecl col_col_bound(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[ES2_COL_SCRATCH]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_col_bound);
    return es2_col_bound_registers(&math_native_memory,&g,a[0],a[1],r);
}
/* Callee counters (hk_<hook>_<suffix>), incremented only while the hook is
 * non-original. Keys: the callee VA, or the vtable offset for slots. */
static callee_counter col_query_counters[]={
    {"call_40c8fc",ES2_COLLIDE_CALL,0x40c8fc,0},
};
static callee_counter col_group_counters[]={
    {"slot_20",ES2_COLLIDE_SLOT,0x20,0},
    {"call_47fa20",ES2_COLLIDE_CALL,0x47fa20,0},
    {"call_40c8c8",ES2_COLLIDE_CALL,0x40c8c8,0},
};
static callee_counter col_shape_counters[]={
    {"call_40c4c4",ES2_COLLIDE_CALL,0x40c4c4,0},
    {"call_40c524",ES2_COLLIDE_CALL,0x40c524,0},
};
static callee_counter col_bound_counters[]={
    {"call_4801d4",ES2_COLLIDE_CALL,0x4801d4,0},
    {"call_47de4c",ES2_COLLIDE_CALL,0x47de4c,0},
};
static callee_counter *col_hook_counters(unsigned hook,unsigned *count) {
#define COL_COUNTERS(name) if(hook==ES2_HOOK_INDEX_##name) { \
        *count=sizeof(name##_counters)/sizeof(name##_counters[0]);return name##_counters; }
    COL_COUNTERS(col_query)
    COL_COUNTERS(col_group)
    COL_COUNTERS(col_shape)
    COL_COUNTERS(col_bound)
#undef COL_COUNTERS
    *count=0;return NULL;
}
/* push ebp; mov ebp,esp; push ebx; push esi; push edi; xor edx,edx; mov edi,[ebp+8] */
static const BYTE col_query_sig[]={0x55,0x8b,0xec,0x53,0x56,0x57,0x33,0xd2,0x8b,0x7d,0x08};
/* push ebp; mov ebp,esp; add esp,-24h; push ebx; push esi; push edi; mov edx,[ebp+10h] */
static const BYTE col_group_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xdc,0x53,0x56,0x57,0x8b,0x55,0x10};
/* push ebp; mov ebp,esp; push ebx; push esi; mov ebx,[ebp+8]; xor esi,esi */
static const BYTE col_shape_sig[]={0x55,0x8b,0xec,0x53,0x56,0x8b,0x5d,0x08,0x33,0xf6};
/* push ebp; mov ebp,esp; add esp,-0Ch; mov eax,[4a980ch] */
static const BYTE col_bound_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xf4,0xa1,0x0c,0x98,0x4a,0x00};
static const BYTE col_frame_lens[]={1,2,3},col_push_lens[]={1,2,1,1};
#define COL_EXT(name) \
    static const es2_hook_extension col_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,col_##name,TRUE}; \
    static uint32_t __cdecl col_##name##32(const uint32_t *a,void *u) {return (uint32_t)col_##name(a,u,weapons_no_registers);}
COL_EXT(col_query)
COL_EXT(col_group)
COL_EXT(col_shape)
COL_EXT(col_bound)
#undef COL_EXT
static es2_hook_descriptor col_hook_descriptors[]={
    {ES2_HOOK_ID(col_query),col_query_sig,11,5,col_push_lens,4,5,col_col_query32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(col_group),col_group_sig,12,6,col_frame_lens,3,4,col_col_group32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(col_shape),col_shape_sig,10,5,col_push_lens,4,1,col_col_shape32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(col_bound),col_bound_sig,11,6,col_frame_lens,3,2,col_col_bound32,NULL,NULL,NULL,0},
};
#endif
