#ifndef ES2_AI_MOTION_HOOKS_H
#define ES2_AI_MOTION_HOOKS_H
/* AI flyer move step flyer_move 4218c4 (docs/reverse-engineering/
 * flyer.md, "the AI flyer move step") and formation steering formation_steer 41fbb8 (ai.md;
 * dist2d_point, heading_point, euler_matrix, vehicle_control and the
 * virtual +78h through the same bridge). Included after weapons_hooks.h: euler_matrix,
 * transform_point, terrain_height, multiply_q10, dist3d_delta and the sound
 * 462830 go through its guest bridge and callee observer (weapons_graph),
 * so the hooked callees keep their own modes and the sound stays original
 * code in its place. Compare is refused (game calls). No local is read
 * before it is written and the entry EDX/ECX that reach euler_matrix are
 * the framework's entry registers, so no entry shim. No game instruction
 * is embedded. */
#include "es2/core/ai_motion.h"
static uint64_t __cdecl ai_motion_flyer_move(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[128]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_flyer_move);
    return es2_flyer_move_registers(&math_native_memory,&g,a[0],r);
}
static uint64_t __cdecl ai_motion_formation_steer(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[128]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_formation_steer);
    return es2_formation_steer_registers(&math_native_memory,&g,a[0],r);
}
static const BYTE ai_motion_formation_steer_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xcc,0x53,0x56,0x57,0x8b,0x5d,0x08};
static const BYTE ai_motion_flyer_move_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xbc,0x53,0x56,0x57,0x8b,0x5d,0x08};
static const BYTE ai_motion_frame_lens[]={1,2,3};
static const es2_hook_extension ai_motion_flyer_move_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,ai_motion_flyer_move,TRUE};
static const es2_hook_extension ai_motion_formation_steer_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,ai_motion_formation_steer,TRUE};
static uint32_t __cdecl ai_motion_formation_steer32(const uint32_t *a,void *u) {return (uint32_t)ai_motion_formation_steer(a,u,weapons_no_registers);}
static uint32_t __cdecl ai_motion_flyer_move32(const uint32_t *a,void *u) {return (uint32_t)ai_motion_flyer_move(a,u,weapons_no_registers);}
static es2_hook_descriptor ai_motion_hook_descriptors[]={
    {ES2_HOOK_ID(flyer_move),ai_motion_flyer_move_sig,12,6,ai_motion_frame_lens,3,1,ai_motion_flyer_move32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(formation_steer),ai_motion_formation_steer_sig,12,6,ai_motion_frame_lens,3,1,ai_motion_formation_steer32,NULL,NULL,NULL,0},
};
#endif
