#ifndef ES2_PROJECTILE_HOOKS_H
#define ES2_PROJECTILE_HOOKS_H
/* Projectile and bullet steering: vector_angle 49290c, aim_angles
 * 492990, projectile_steer 40a254, bullet_steer 40aff0
 * (docs/reverse-engineering/weapons.md, "Projectile and bullet steering").
 * Included after weapons_hooks.h: the two steering hooks call every game
 * callee through its guest bridge with the original's registers and count
 * them (weapons_graph); their frame buffer stands in for the original's 28h
 * locals. vector_angle and aim_angles compute their helpers in core and get
 * entry shims that load the original's return ECX before the framework's
 * dispatch (the dispatch restores the entry ECX on return), so EAX, EDX and
 * ECX are exact for every caller. No game instruction is embedded; the
 * shims only compute register values from the arguments. */
#include "es2/core/projectile.h"
static uint64_t __cdecl projectile_vector_angle(const uint32_t *a,void *u,const uint32_t *r) {
    (void)r;return es2_vector_angle_registers(a[0],a[1],(BYTE *)u+0xa1c34);
}
static uint64_t __cdecl projectile_aim_angles(const uint32_t *a,void *u,const uint32_t *r) {
    (void)r;return es2_aim_angles_registers(&math_native_memory,a[0],a[1],a[2],(BYTE *)u+0xa1c34);
}
static uint64_t __cdecl projectile_projectile_steer(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE frame[48]={0};es2_weapon_graph g=weapons_graph(u,frame,ES2_HOOK_INDEX_projectile_steer);
    return es2_projectile_steer_registers(&math_native_memory,&g,a[0],r);
}
static uint64_t __cdecl projectile_bullet_steer(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE frame[48]={0};es2_weapon_graph g=weapons_graph(u,frame,ES2_HOOK_INDEX_bullet_steer);
    return es2_bullet_steer_registers(&math_native_memory,&g,a[0],r);
}
/* The magnitude of a signed word, wrapped (|INT_MIN| = INT_MIN). */
static uint32_t projectile_abs(uint32_t v) {const uint32_t s=v>>31?0xffffffffu:0u;return (v^s)-s;}
/* 49290c: ECX = 47d32c's divisor register for (x or 1, y): |y| when y is
 * nonzero and |y| > |x| (signed, wrapped), else |x|. The two pushes the
 * shim saves EAX and EDX with are left below the entry ESP. */
static int projectile_vector_entry(es2_machine *m,uint32_t bridge) {
    (void)bridge;shim_w32(m->esp-4u,m->eax);shim_w32(m->esp-8u,m->edx);
    uint32_t x=shim_r32(m->esp+4u);const uint32_t y=shim_r32(m->esp+8u);
    if(!(x|y))x=1u;
    const uint32_t ax=projectile_abs(x),ay=projectile_abs(y);
    m->ecx=ay&&(int32_t)ay>(int32_t)ax?ay:ax;return 0;
}
/* 492990: ECX = end.x - start.x (its POP ECX reloads that local). */
static int projectile_aim_entry(es2_machine *m,uint32_t bridge) {
    (void)bridge;shim_w32(m->esp-4u,m->edx);
    m->ecx=shim_r32(shim_r32(m->esp+8u))-shim_r32(shim_r32(m->esp+12u));return 0;
}
ES2_SHIM(projectile_vector_shim,0x9290cu,projectile_vector_entry,0);
ES2_SHIM(projectile_aim_shim,0x92990u,projectile_aim_entry,0);
static const BYTE projectile_vector_angle_sig[]={0x55,0x8b,0xec,0x53,0x56,0x8b,0x75,0x0c};
static const BYTE projectile_aim_angles_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xf8,0x53,0x56,0x57};
static const BYTE projectile_projectile_steer_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xd8,0x53,0x56,0x57};
static const BYTE projectile_bullet_steer_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xd8,0x53,0x56,0x57};
static const BYTE projectile_frame_lens[]={1,2,3},projectile_saved_lens[]={1,2,1,1};
/* vector_angle writes nothing and reads only the correction table, so it
 * allows compare mode; the other three refuse it. */
static const es2_hook_extension projectile_vector_angle_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,math_ratio_region,1,projectile_vector_angle,FALSE};
#define PROJECTILE_EXT(name) static const es2_hook_extension projectile_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,projectile_##name,TRUE};
PROJECTILE_EXT(aim_angles) PROJECTILE_EXT(projectile_steer) PROJECTILE_EXT(bullet_steer)
#undef PROJECTILE_EXT
#define PROJECTILE_LEGACY(name) static uint32_t __cdecl projectile_##name##32(const uint32_t *a,void *u) {return (uint32_t)projectile_##name(a,u,weapons_no_registers);}
PROJECTILE_LEGACY(vector_angle) PROJECTILE_LEGACY(aim_angles) PROJECTILE_LEGACY(projectile_steer) PROJECTILE_LEGACY(bullet_steer)
#undef PROJECTILE_LEGACY
static es2_hook_descriptor projectile_hook_descriptors[]={
    {ES2_HOOK_ID(vector_angle),projectile_vector_angle_sig,8,5,projectile_saved_lens,4,2,projectile_vector_angle32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(aim_angles),projectile_aim_angles_sig,9,6,projectile_frame_lens,3,3,projectile_aim_angles32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(projectile_steer),projectile_projectile_steer_sig,9,6,projectile_frame_lens,3,1,projectile_projectile_steer32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(bullet_steer),projectile_bullet_steer_sig,9,6,projectile_frame_lens,3,1,projectile_bullet_steer32,NULL,NULL,NULL,0},
};
#endif
