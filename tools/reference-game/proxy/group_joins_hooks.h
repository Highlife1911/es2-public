#ifndef ES2_GROUP_JOINS_HOOKS_H
#define ES2_GROUP_JOINS_HOOKS_H
/* group_joins group (docs/reverse-engineering/mission_load.md,
 * section "group joins and the building footprint"): the join
 * methods group_ctor 423b34 calls once per member, mech_join 417aa8
 * (423c6c), flyer_join 421ee8 (423cc3) and building_join 405c3c (423d1e),
 * which place members still at the actor load's default angle and height;
 * footprint_marks 470ed4 (the building join's 405d79 and the actor load's
 * 425d08), which marks the terrain cells of a building's footprint; and
 * cell_flag_set 470ddc, the one-byte cell flag writer (footprint_marks, the
 * flattening 470fe8/471128 and the per-frame cell clear 46e5a4). The joins
 * and footprint_marks run during the load only (live with the early
 * arming); cell_flag_set also runs per frame under the render root.
 *
 * Every game call goes through the weapons guest bridge and callee observer
 * (weapons_graph) with the original's EAX/EDX/ECX: route_point,
 * terrain_height, dist_gate, footprint_marks and cell_flag_set (hooks with
 * their own modes) and the virtual slots +78h (the formation slot,
 * formation_mech/flyer/base) and +10h (shape_word). The 12-byte vector of
 * a join and the point footprint_marks hands to dist_gate lie in a zeroed
 * scratch on the adapter's stack; no callee reads beyond them (dist_gate
 * reads 12 bytes, the formation slots and terrain_height the vector).
 *
 * EDX:EAX exact; ECX is dead after every call site (mission_load.md,
 * section "group joins and the building footprint", "Callers' registers":
 * group_ctor reloads ECX before
 * reading it and returns from the last join with ECX dead at its own
 * caller; 405c3c and 4253d8 write ECX or
 * call terrain_height, which writes ECX first, after 470ed4; 46e5a4 pops ECX
 * after 470ddc, 47108c clears it, 4711da loads it), so it is not returned
 * (the framework returns the entry ECX). No original reads an
 * uninitialized local; no entry or exit shim. All five refuse compare mode
 * (the joins and footprint_marks call game code that writes the actor and
 * the terrain cells; cell_flag_set writes one byte at a runtime index). No
 * game instruction is embedded. */
#include "es2/core/group_joins.h"
static uint64_t __cdecl group_joins_mech_join(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[ES2_GROUP_JOINS_SCRATCH]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_mech_join);
    return es2_mech_join_registers(&math_native_memory,&g,a[0],a[1],a[2],r,NULL);
}
static uint64_t __cdecl group_joins_flyer_join(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[ES2_GROUP_JOINS_SCRATCH]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_flyer_join);
    return es2_flyer_join_registers(&math_native_memory,&g,a[0],a[1],a[2],r,NULL);
}
static uint64_t __cdecl group_joins_building_join(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[ES2_GROUP_JOINS_SCRATCH]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_building_join);
    return es2_building_join_registers(&math_native_memory,&g,a[0],a[1],a[2],a[3],r,NULL);
}
static uint64_t __cdecl group_joins_footprint_marks(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[ES2_GROUP_JOINS_SCRATCH]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_footprint_marks);
    return es2_footprint_marks_registers(&math_native_memory,&g,a[0],a[1],a[2],r,NULL);
}
static uint64_t __cdecl group_joins_cell_flag_set(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;return es2_cell_flag_set_registers(&math_native_memory,a[0],a[1],a[2],a[3],r,NULL);
}
/* Callee counters (hk_<hook>_<suffix>), incremented only while the hook is
 * non-original. Keys: the callee VA, or the vtable slot. */
static callee_counter mech_join_counters[]={
    {"call_423b0c",ES2_COLLIDE_CALL,0x423b0c,0},
    {"call_46e180",ES2_COLLIDE_CALL,0x46e180,0},
    {"slot_78",ES2_COLLIDE_SLOT,0x78,0},
};
static callee_counter flyer_join_counters[]={
    {"call_423b0c",ES2_COLLIDE_CALL,0x423b0c,0},
    {"slot_78",ES2_COLLIDE_SLOT,0x78,0},
};
static callee_counter building_join_counters[]={
    {"call_423b0c",ES2_COLLIDE_CALL,0x423b0c,0},
    {"slot_78",ES2_COLLIDE_SLOT,0x78,0},
    {"slot_10",ES2_COLLIDE_SLOT,0x10,0},
    {"call_470ed4",ES2_COLLIDE_CALL,0x470ed4,0},
    {"call_46e180",ES2_COLLIDE_CALL,0x46e180,0},
};
static callee_counter footprint_marks_counters[]={
    {"call_470ddc",ES2_COLLIDE_CALL,0x470ddc,0},
    {"call_49288c",ES2_COLLIDE_CALL,0x49288c,0},
};
#define GROUP_JOINS_COUNTERS(name) \
    if(hook==ES2_HOOK_INDEX_##name){*count=sizeof(name##_counters)/sizeof(name##_counters[0]);return name##_counters;}
static callee_counter *group_joins_hook_counters(unsigned hook,unsigned *count) {
    GROUP_JOINS_COUNTERS(mech_join)
    GROUP_JOINS_COUNTERS(flyer_join)
    GROUP_JOINS_COUNTERS(building_join)
    GROUP_JOINS_COUNTERS(footprint_marks)
    *count=0;return NULL;
}
#undef GROUP_JOINS_COUNTERS
/* 417aa8 and 421ee8: push ebp; mov ebp,esp; add esp,-14h; push ebx; push
 * esi; mov ebx,[ebp+8]; lea esi,[ebp-14h]. Relocated 6 bytes (1,2,3).
 * 405c3c: push ebp; mov ebp,esp; add esp,-18h; push ebx; push esi; push
 * edi; mov ebx,[ebp+8]. Relocated 6 bytes (1,2,3).
 * 470ed4: push ebp; mov ebp,esp; add esp,-1Ch; push ebx; push esi; push
 * edi; mov edx,[ebp+0Ch]. Relocated 6 bytes (1,2,3).
 * 470ddc: push ebp; mov ebp,esp; mov eax,[ebp+8]; mov edx,[ebp+10h].
 * Relocated 6 bytes (1,2,3); the body is 34 bytes long, the patch's five
 * bytes stay inside it. */
static const BYTE mech_join_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xec,0x53,0x56,0x8b,0x5d,0x08,0x8d,0x75,0xec};
static const BYTE flyer_join_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xec,0x53,0x56,0x8b,0x5d,0x08,0x8d,0x75,0xec};
static const BYTE building_join_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xe8,0x53,0x56,0x57,0x8b,0x5d,0x08};
static const BYTE footprint_marks_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xe4,0x53,0x56,0x57,0x8b,0x55,0x0c};
static const BYTE cell_flag_set_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x8b,0x55,0x10};
static const BYTE mech_join_lens[]={1,2,3},flyer_join_lens[]={1,2,3},building_join_lens[]={1,2,3},
    footprint_marks_lens[]={1,2,3},cell_flag_set_lens[]={1,2,3};
#define GROUP_JOINS_EXT(name) \
    static const es2_hook_extension group_joins_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,group_joins_##name,TRUE}; \
    static uint32_t __cdecl group_joins_##name##32(const uint32_t *a,void *u) {return (uint32_t)group_joins_##name(a,u,weapons_no_registers);}
GROUP_JOINS_EXT(mech_join)
GROUP_JOINS_EXT(flyer_join)
GROUP_JOINS_EXT(building_join)
GROUP_JOINS_EXT(footprint_marks)
GROUP_JOINS_EXT(cell_flag_set)
#undef GROUP_JOINS_EXT
static es2_hook_descriptor group_joins_hook_descriptors[]={
    {ES2_HOOK_ID(mech_join),mech_join_sig,14,6,mech_join_lens,3,3,group_joins_mech_join32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(flyer_join),flyer_join_sig,14,6,flyer_join_lens,3,3,group_joins_flyer_join32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(building_join),building_join_sig,12,6,building_join_lens,3,4,group_joins_building_join32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(footprint_marks),footprint_marks_sig,12,6,footprint_marks_lens,3,3,group_joins_footprint_marks32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(cell_flag_set),cell_flag_set_sig,9,6,cell_flag_set_lens,3,4,group_joins_cell_flag_set32,NULL,NULL,NULL,0},
};
#endif
