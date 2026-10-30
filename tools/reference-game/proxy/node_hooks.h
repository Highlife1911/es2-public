#ifndef ES2_NODE_HOOKS_H
#define ES2_NODE_HOOKS_H
/* WP3 graph nodes: terrain_pose 4029d8, shield_geometry 413cc4,
 * debris_heading 4089e4. Included after weapons_hooks.h: every game callee
 * goes through its guest bridge and callee observer (weapons_graph), so the
 * three hooks refuse compare mode. No game instruction is embedded. */
#include "es2/core/node.h"
static uint64_t __cdecl node_terrain_pose(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[128]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_terrain_pose);
    return es2_terrain_pose_registers(&math_native_memory,&g,a[0],r);
}
static uint64_t __cdecl node_shield_geometry(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[128]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_shield_geometry);
    return es2_shield_geometry_registers(&math_native_memory,&g,a[0],a[1],a[2],a[3],a[4],r);
}
static uint64_t __cdecl node_debris_heading(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_debris_heading);
    return es2_debris_heading_registers(&math_native_memory,&g,a[0],a[1],a[2],a[3],r);
}
static const BYTE node_terrain_pose_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xbc};
static const BYTE node_shield_geometry_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xb8};
static const BYTE node_debris_heading_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xcc};
static const BYTE node_frame_lens[]={1,2,3};
#define NODE_EXT(name) static const es2_hook_extension node_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,node_##name,TRUE};
NODE_EXT(terrain_pose)
NODE_EXT(shield_geometry)
NODE_EXT(debris_heading)
#undef NODE_EXT
#define NODE_LEGACY(name) static uint32_t __cdecl node_##name##32(const uint32_t *a,void *u) {return (uint32_t)node_##name(a,u,weapons_no_registers);}
NODE_LEGACY(terrain_pose)
NODE_LEGACY(shield_geometry)
NODE_LEGACY(debris_heading)
#undef NODE_LEGACY
static es2_hook_descriptor node_hook_descriptors[]={
    {ES2_HOOK_ID(terrain_pose),node_terrain_pose_sig,6,6,node_frame_lens,3,1,node_terrain_pose32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(shield_geometry),node_shield_geometry_sig,6,6,node_frame_lens,3,5,node_shield_geometry32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(debris_heading),node_debris_heading_sig,6,6,node_frame_lens,3,4,node_debris_heading32,NULL,NULL,NULL,0},
};
#endif
