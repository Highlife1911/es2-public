#ifndef ES2_TRACK_POSE_HOOKS_H
#define ES2_TRACK_POSE_HOOKS_H
/* Track/pose remainder: track_route 4793d4, track_poses 479ab0,
 * pose_list 478b00, track_seek 479344, turret_aim 403d5c. Included after
 * node_hooks.h. The first three run their callees in core, as inside
 * track_target/track_advance (clip classes other than ANSequence and
 * ANCyclicSequence call their original method); track_seek and turret_aim
 * call every game callee through the weapons guest bridge and its callee
 * observer, so nested hooks keep their modes. No game instruction is
 * embedded. Contracts: player_movement.md, "Track and pose remainder". */
#include "es2/core/track_pose.h"
static uint64_t __cdecl track_pose_track_route(const uint32_t *a,void *u,const uint32_t *r) {
    (void)r;es2_clip_classes k=math_clip_classes(u);return es2_track_route_registers(&math_native_memory,&k,a[0]);
}
static uint64_t __cdecl track_pose_track_poses(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;(void)r;return es2_track_poses_registers(&math_native_memory,a[0],a[1],a[2]);
}
static uint64_t __cdecl track_pose_pose_list(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;(void)r;return es2_pose_list_registers(&math_native_memory,a[0]);
}
static uint64_t __cdecl track_pose_track_seek(const uint32_t *a,void *u,const uint32_t *r) {
    (void)r;es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_track_seek);
    return es2_track_seek_registers(&math_native_memory,&g,a[0],a[1],a[2]);
}
static uint64_t __cdecl track_pose_turret_aim(const uint32_t *a,void *u,const uint32_t *r) {
    (void)r;es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_turret_aim);
    return es2_turret_aim_registers(&math_native_memory,&g,a[0],a[1]);
}
static const BYTE track_pose_frame20_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xe0,0x53,0x56,0x57};
static const BYTE track_pose_frame18_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xe8,0x53,0x56,0x57};
static const BYTE track_pose_pose_list_sig[]={0x55,0x8b,0xec,0x53,0x56,0x57,0x8b,0x5d,0x08};
static const BYTE track_pose_track_seek_sig[]={0x55,0x8b,0xec,0x53,0x56,0x57,0x33,0xf6};
static const BYTE track_pose_turret_aim_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xd4,0x53,0x56,0x57};
static const BYTE track_pose_frame_lens[]={1,2,3};
static const BYTE track_pose_saved_lens[]={1,2,1,1};
/* The route search writes only the track span T+0..0x51 and reads the
 * resource graph through the unchanged T+0, as track_target does. */
static const es2_hook_extension track_pose_track_route_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,track_pose_track_route,FALSE};
/* Runtime-indexed node poses and flags; game callees with their own modes. */
#define TRACK_POSE_EXT(name) static const es2_hook_extension track_pose_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,track_pose_##name,TRUE};
TRACK_POSE_EXT(track_poses)
TRACK_POSE_EXT(pose_list)
TRACK_POSE_EXT(track_seek)
TRACK_POSE_EXT(turret_aim)
#undef TRACK_POSE_EXT
#define TRACK_POSE_LEGACY(name) static uint32_t __cdecl track_pose_##name##32(const uint32_t *a,void *u) {return (uint32_t)track_pose_##name(a,u,weapons_no_registers);}
TRACK_POSE_LEGACY(track_route)
TRACK_POSE_LEGACY(track_poses)
TRACK_POSE_LEGACY(pose_list)
TRACK_POSE_LEGACY(track_seek)
TRACK_POSE_LEGACY(turret_aim)
#undef TRACK_POSE_LEGACY
static es2_hook_descriptor track_pose_hook_descriptors[]={
    {ES2_HOOK_ID(track_route),track_pose_frame20_sig,9,6,track_pose_frame_lens,3,1,track_pose_track_route32,math_track_state_equal,NULL,math_track_state_spans,1},
    {ES2_HOOK_ID(track_poses),track_pose_frame18_sig,9,6,track_pose_frame_lens,3,3,track_pose_track_poses32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(pose_list),track_pose_pose_list_sig,9,5,track_pose_saved_lens,4,1,track_pose_pose_list32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(track_seek),track_pose_track_seek_sig,8,5,track_pose_saved_lens,4,3,track_pose_track_seek32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(turret_aim),track_pose_turret_aim_sig,9,6,track_pose_frame_lens,3,2,track_pose_turret_aim32,NULL,NULL,NULL,0},
};
#endif
