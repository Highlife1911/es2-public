#ifndef ES2_COCKPIT_TARGET_HOOKS_H
#define ES2_COCKPIT_TARGET_HOOKS_H
/* Cockpit target selection (docs/reverse-engineering/hud.md, "cockpit
 * target cycling as own code"): cockpit_valid 433174, cockpit_arc
 * 433250, cockpit_next 43349c, cockpit_nearest 4333c8 and cockpit_select
 * 4332dc. Included after weapons_hooks.h: every game callee (dist_gate,
 * heading_point, angle_ratio, multiply_q10, the vehicle virtuals +3Ch/+10h,
 * the HUD record update 43d9b4 and the five among themselves) goes through
 * its guest bridge and callee observer (weapons_graph), so all five refuse
 * compare mode. No local is read before it is written and no entry register
 * beyond the framework's EAX/EDX/ECX, so no entry shim. ECX at return is
 * outside the contract (hud.md). No game instruction is embedded. */
#include "es2/core/cockpit_target.h"
static uint64_t __cdecl cockpit_target_cockpit_valid(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_cockpit_valid);
    return es2_cockpit_valid_registers(&math_native_memory,&g,a[0],a[1],r);
}
static uint64_t __cdecl cockpit_target_cockpit_arc(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_cockpit_arc);
    return es2_cockpit_arc_registers(&math_native_memory,&g,a[0],a[1],r);
}
static uint64_t __cdecl cockpit_target_cockpit_next(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_cockpit_next);
    return es2_cockpit_next_registers(&math_native_memory,&g,a[0],r);
}
static uint64_t __cdecl cockpit_target_cockpit_nearest(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_cockpit_nearest);
    return es2_cockpit_nearest_registers(&math_native_memory,&g,a[0],r);
}
static uint64_t __cdecl cockpit_target_cockpit_select(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_cockpit_select);
    return es2_cockpit_select_registers(&math_native_memory,&g,a[0],a[1],r);
}
static const BYTE cockpit_target_valid_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xe4};
static const BYTE cockpit_target_arc_sig[]={0x55,0x8b,0xec,0x51,0x53,0x56};
static const BYTE cockpit_target_next_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xe0};
static const BYTE cockpit_target_nearest_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xf0};
static const BYTE cockpit_target_select_sig[]={0x55,0x8b,0xec,0x51,0x53,0x56};
static const BYTE cockpit_target_frame_lens[]={1,2,3},cockpit_target_saved_lens[]={1,2,1,1};
#define COCKPIT_TARGET_EXT(name) static const es2_hook_extension cockpit_target_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,cockpit_target_##name,TRUE};
#define COCKPIT_TARGET_LEGACY(name) static uint32_t __cdecl cockpit_target_##name##32(const uint32_t *a,void *u) {return (uint32_t)cockpit_target_##name(a,u,weapons_no_registers);}
#define COCKPIT_TARGET_BOTH(name) COCKPIT_TARGET_EXT(name) COCKPIT_TARGET_LEGACY(name)
COCKPIT_TARGET_BOTH(cockpit_valid) COCKPIT_TARGET_BOTH(cockpit_arc) COCKPIT_TARGET_BOTH(cockpit_next)
COCKPIT_TARGET_BOTH(cockpit_nearest) COCKPIT_TARGET_BOTH(cockpit_select)
#undef COCKPIT_TARGET_EXT
#undef COCKPIT_TARGET_LEGACY
#undef COCKPIT_TARGET_BOTH
static es2_hook_descriptor cockpit_target_hook_descriptors[]={
    {ES2_HOOK_ID(cockpit_valid),cockpit_target_valid_sig,6,6,cockpit_target_frame_lens,3,2,cockpit_target_cockpit_valid32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(cockpit_arc),cockpit_target_arc_sig,6,5,cockpit_target_saved_lens,4,2,cockpit_target_cockpit_arc32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(cockpit_next),cockpit_target_next_sig,6,6,cockpit_target_frame_lens,3,1,cockpit_target_cockpit_next32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(cockpit_nearest),cockpit_target_nearest_sig,6,6,cockpit_target_frame_lens,3,1,cockpit_target_cockpit_nearest32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(cockpit_select),cockpit_target_select_sig,6,5,cockpit_target_saved_lens,4,2,cockpit_target_cockpit_select32,NULL,NULL,NULL,0},
};
#endif
