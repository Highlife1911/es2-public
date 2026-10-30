#ifndef ES2_BUILDING_HOOKS_H
#define ES2_BUILDING_HOOKS_H
/* Building logic group (docs/reverse-engineering/weapons.md, section
 * "Building logic: target selection and destruction actions"). Included
 * after mission_objectives_hooks.h: every game callee goes through the
 * weapons guest bridge and callee observer (weapons_graph), so these hooks
 * refuse compare mode. No game instruction is embedded. */
#include "es2/core/building.h"

static uint64_t __cdecl building_target_select(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_target_select);
    return es2_target_select_registers(&math_native_memory,&g,a[0],a[1],a[2],r);
}
static uint64_t __cdecl building_target_valid(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_target_valid);
    return es2_target_valid_registers(&math_native_memory,&g,a[0],a[1],a[2],r);
}
static uint64_t __cdecl building_death_actions(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_death_actions);
    return es2_death_actions_registers(&math_native_memory,&g,a[0],r);
}
static uint64_t __cdecl building_group_death(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_group_death);
    return es2_group_death_registers(&math_native_memory,&g,a[0],a[1],r);
}
static const BYTE building_target_select_sig[]={0x55,0x8b,0xec,0x83,0xc4,0x90};
static const BYTE building_target_valid_sig[]={0x55,0x8b,0xec,0x53,0x56,0x57};
static const BYTE building_death_actions_sig[]={0x55,0x8b,0xec,0x51,0x53,0x56};
static const BYTE building_group_death_sig[]={0x55,0x8b,0xec,0x53,0x56,0x57};
static const BYTE building_frame_lens[]={1,2,3},building_saved_lens[]={1,2,1,1};
#define BUILDING_EXT(name) static const es2_hook_extension building_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,building_##name,TRUE};
#define BUILDING_LEGACY(name) static uint32_t __cdecl building_##name##32(const uint32_t *a,void *u) {return (uint32_t)building_##name(a,u,weapons_no_registers);}
#define BUILDING_BOTH(name) BUILDING_EXT(name) BUILDING_LEGACY(name)
BUILDING_BOTH(target_select) BUILDING_BOTH(target_valid) BUILDING_BOTH(death_actions) BUILDING_BOTH(group_death)
#undef BUILDING_EXT
#undef BUILDING_LEGACY
#undef BUILDING_BOTH
static es2_hook_descriptor building_hook_descriptors[]={
    {ES2_HOOK_ID(target_select),building_target_select_sig,6,6,building_frame_lens,3,3,building_target_select32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(target_valid),building_target_valid_sig,6,5,building_saved_lens,4,3,building_target_valid32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(death_actions),building_death_actions_sig,6,5,building_saved_lens,4,1,building_death_actions32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(group_death),building_group_death_sig,6,5,building_saved_lens,4,2,building_group_death32,NULL,NULL,NULL,0},
};
#endif
