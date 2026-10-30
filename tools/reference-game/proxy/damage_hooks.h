#ifndef ES2_DAMAGE_HOOKS_H
#define ES2_DAMAGE_HOOKS_H
#include "es2/core/damage.h"
/* Native mapping/cdecl adapter supplied by math_hooks.h. */
static uint64_t __cdecl damage_dist3d_delta(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;(void)r;return es2_distance3_registers(a[0],a[1],a[2]);
}
static uint64_t __cdecl damage_dist3d_forward(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;(void)r;return es2_distance3_forward_registers(a[0],a[1],a[2]);
}
static uint64_t __cdecl damage_heading_point(const uint32_t *a,void *u,const uint32_t *r) {
    (void)r;return es2_heading_point_registers((void *)(uintptr_t)a[0],(void *)(uintptr_t)a[1],(BYTE *)u+0xa1c34);
}
static uint64_t __cdecl damage_shield_damage(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;(void)r;return es2_shield_damage_registers((void *)(uintptr_t)a[0],a[1],a[2]);
}
static es2_damage_context damage_context(void *u,uint32_t point,BOOL building) {
    uint32_t base=(uint32_t)(uintptr_t)u;
    es2_damage_context g={base+0xd2599u,base+0x92934u,base+0x13c68u,base+0x92ee0u,base+0x9288cu,
        point,math_guest_call,NULL,building?building_hit_observe:vehicle_hit_observe,u};return g;
}
static uint64_t __cdecl damage_vehicle_hit(const uint32_t *a,void *u,const uint32_t *r) {
    (void)r;BYTE point[12];es2_damage_context g=damage_context(u,(uint32_t)(uintptr_t)point,FALSE);
    return es2_vehicle_hit_registers(&math_native_memory,&g,a[0],a[1],a[2],a[3],a[4]);
}
static uint64_t __cdecl damage_building_hit(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE point[12];es2_damage_context g=damage_context(u,(uint32_t)(uintptr_t)point,TRUE);
    return es2_building_hit_registers(&math_native_memory,&g,a[0],a[1],a[2],a[3],a[4],r[1]);
}
static es2_building_part_context building_part_context(void *u) {
    uint32_t b=(uint32_t)(uintptr_t)u;
    es2_building_part_context g={b+0xd24e6u,b+0x9741cu,b+0x7e068u,b+0x7e08du,b+0x92ee0u,
        b+0xd2599u,b+0x20a68u,b+0x11bc8u,b+0x23430u,math_guest_call,NULL,math_effects_call};return g;
}
static uint64_t __cdecl damage_building_part(const uint32_t *a,void *u,const uint32_t *r) {
    es2_building_part_context g=building_part_context(u);
    return es2_building_part_registers(&math_native_memory,&g,a[0],a[1],a[2],a[3],r[0]);
}
static const BYTE damage_dist3d_delta_sig[]={0xc8,0,0,0,0x53};
static const BYTE damage_dist3d_delta_lens[]={4,1};
static const BYTE damage_dist3d_forward_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x10};
static const BYTE damage_building_part_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xec,0x53,0x56,0x57};
static const BYTE damage_frame_lens[]={1,2,3};
static const BYTE damage_heading_point_sig[]={0x55,0x8b,0xec,0x53,0x56};
static const BYTE damage_saved_lens[]={1,2,1,1};
static const BYTE damage_shield_damage_sig[]={0x55,0x8b,0xec,0x53,0x56,0x57};
static const BYTE damage_vehicle_hit_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xf4,0x53,0x56,0x57};
static const BYTE damage_building_hit_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xf4,0x53,0x56,0x57};
static const es2_hook_span damage_heading_spans[]={{0,0,8},{1,0,8}};
static const es2_hook_span damage_shield_spans[]={{0,0,4}};
static const es2_hook_extension damage_dist3d_delta_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,damage_dist3d_delta,FALSE};
static const es2_hook_extension damage_dist3d_forward_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,damage_dist3d_forward,FALSE};
static const es2_hook_extension damage_heading_point_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,math_ratio_region,1,damage_heading_point,FALSE};
static const es2_hook_extension damage_shield_damage_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,damage_shield_damage,FALSE};
/* All writes are delegated: arbitrary V58/V74 graphs and RNG. Refuse compare
 * rather than describe an incomplete writable state region. */
static const es2_hook_extension damage_vehicle_hit_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,damage_vehicle_hit,TRUE};
static const es2_hook_extension damage_building_hit_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,damage_building_hit,TRUE};
/* Indexed 11-byte state/word flags cannot be expressed as fixed regions;
 * arbitrary V40/V60/lifecycle effects also preclude compare. Actor bytes are
 * the fixed direct-write span; full graph writes are covered in vitro. */
static const es2_hook_span damage_part_spans[]={{0,0x96,4}};
static const es2_hook_extension damage_building_part_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,damage_building_part,TRUE};
static const uint32_t damage_no_registers[3]={0,0,0};
#define DAMAGE_LEGACY(name) static uint32_t __cdecl damage_##name##32(const uint32_t *a,void *u) { return (uint32_t)damage_##name(a,u,damage_no_registers); }
DAMAGE_LEGACY(dist3d_delta)
DAMAGE_LEGACY(dist3d_forward)
DAMAGE_LEGACY(heading_point)
DAMAGE_LEGACY(shield_damage)
DAMAGE_LEGACY(vehicle_hit)
DAMAGE_LEGACY(building_hit)
DAMAGE_LEGACY(building_part)
#undef DAMAGE_LEGACY
static es2_hook_descriptor damage_hook_descriptors[]={
    {ES2_HOOK_ID(dist3d_delta),damage_dist3d_delta_sig,5,5,damage_dist3d_delta_lens,2,3,damage_dist3d_delta32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(dist3d_forward),damage_dist3d_forward_sig,6,6,damage_frame_lens,3,3,damage_dist3d_forward32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(heading_point),damage_heading_point_sig,5,5,damage_saved_lens,4,2,damage_heading_point32,NULL,NULL,damage_heading_spans,2},
    {ES2_HOOK_ID(shield_damage),damage_shield_damage_sig,6,5,damage_saved_lens,4,3,damage_shield_damage32,NULL,NULL,damage_shield_spans,1},
    {ES2_HOOK_ID(vehicle_hit),damage_vehicle_hit_sig,9,6,damage_frame_lens,3,5,damage_vehicle_hit32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(building_hit),damage_building_hit_sig,9,6,damage_frame_lens,3,5,damage_building_hit32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(building_part),damage_building_part_sig,9,6,damage_frame_lens,3,4,damage_building_part32,NULL,NULL,damage_part_spans,1},
};
#endif
