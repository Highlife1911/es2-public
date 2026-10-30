#ifndef ES2_WEAPON_HELPERS_HOOKS_H
#define ES2_WEAPON_HELPERS_HOOKS_H
/* Weapon group and collision-sphere helpers: group_fire 4107e4,
 * weapon_ready 40ede8, part_condition 40db2c, sphere_hits 40c524,
 * sphere_test 40c428. Included after weapons_hooks.h: game callees go
 * through its guest bridge and callee observer (weapons_graph). The frame
 * buffers stand in for the original's locals; their addresses appear in
 * residue registers as the original's frame addresses do (weapons.md).
 * weapon_ready and part_condition call nothing and write nothing, so they
 * allow compare mode. No game instruction is embedded. */
#include "es2/core/weapon_helpers.h"
static uint64_t __cdecl weapon_helpers_group_fire(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE frame[32]={0};es2_weapon_graph g=weapons_graph(u,frame,ES2_HOOK_INDEX_group_fire);
    return es2_group_fire_registers(&math_native_memory,&g,a[0],a[1],a[2],a[3],r);
}
static uint64_t __cdecl weapon_helpers_weapon_ready(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;return es2_weapon_ready_registers(&math_native_memory,a[0],r);
}
static uint64_t __cdecl weapon_helpers_part_condition(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;(void)r;return es2_part_condition_registers(&math_native_memory,a[0]);
}
static uint64_t __cdecl weapon_helpers_sphere_hits(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE frame[32]={0};es2_weapon_graph g=weapons_graph(u,frame,ES2_HOOK_INDEX_sphere_hits);
    return es2_sphere_hits_registers(&math_native_memory,&g,a[0],a[1],r);
}
static uint64_t __cdecl weapon_helpers_sphere_test(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_sphere_test);
    return es2_sphere_test_registers(&math_native_memory,&g,a[0],a[1],a[2],r);
}
static const BYTE weapon_helpers_group_fire_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xd8};
static const BYTE weapon_helpers_weapon_ready_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08};
static const BYTE weapon_helpers_part_condition_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xf8};
static const BYTE weapon_helpers_sphere_hits_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xec};
static const BYTE weapon_helpers_sphere_test_sig[]={0x55,0x8b,0xec,0x53,0x56,0x57};
static const BYTE weapon_helpers_frame_lens[]={1,2,3},weapon_helpers_saved_lens[]={1,2,1,1};
#define WEAPON_HELPERS_EXT(name,unsafe) static const es2_hook_extension weapon_helpers_##name##_extension= \
    {ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,weapon_helpers_##name,unsafe};
WEAPON_HELPERS_EXT(group_fire,TRUE)
WEAPON_HELPERS_EXT(weapon_ready,FALSE)
WEAPON_HELPERS_EXT(part_condition,FALSE)
WEAPON_HELPERS_EXT(sphere_hits,TRUE)
WEAPON_HELPERS_EXT(sphere_test,TRUE)
#undef WEAPON_HELPERS_EXT
#define WEAPON_HELPERS_LEGACY(name) static uint32_t __cdecl weapon_helpers_##name##32(const uint32_t *a,void *u) \
    {return (uint32_t)weapon_helpers_##name(a,u,weapons_no_registers);}
WEAPON_HELPERS_LEGACY(group_fire)
WEAPON_HELPERS_LEGACY(weapon_ready)
WEAPON_HELPERS_LEGACY(part_condition)
WEAPON_HELPERS_LEGACY(sphere_hits)
WEAPON_HELPERS_LEGACY(sphere_test)
#undef WEAPON_HELPERS_LEGACY
static es2_hook_descriptor weapon_helpers_hook_descriptors[]={
    {ES2_HOOK_ID(group_fire),weapon_helpers_group_fire_sig,6,6,weapon_helpers_frame_lens,3,4,weapon_helpers_group_fire32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(weapon_ready),weapon_helpers_weapon_ready_sig,6,6,weapon_helpers_frame_lens,3,1,weapon_helpers_weapon_ready32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(part_condition),weapon_helpers_part_condition_sig,6,6,weapon_helpers_frame_lens,3,1,weapon_helpers_part_condition32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(sphere_hits),weapon_helpers_sphere_hits_sig,6,6,weapon_helpers_frame_lens,3,2,weapon_helpers_sphere_hits32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(sphere_test),weapon_helpers_sphere_test_sig,6,5,weapon_helpers_saved_lens,4,3,weapon_helpers_sphere_test32,NULL,NULL,NULL,0},
};
#endif
