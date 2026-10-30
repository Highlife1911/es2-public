#ifndef ES2_BUILDING_AIM_HOOKS_H
#define ES2_BUILDING_AIM_HOOKS_H
/* building_aim group (docs/reverse-engineering/weapons.md, section
 * "building turret target 403eec and part facing 406868"):
 * turret_target 403eec below turret 404100 and part_facing 406868, the
 * building virtual +54h. Every game callee (euler_matrix,
 * matrix_transpose, rotate_point, transform_point, aim_angles, turret_aim,
 * the shape manager's slot +20h and the building's slot +58h) goes through
 * the weapons guest bridge and callee observer, so nested hooks keep their
 * modes; both refuse compare mode. The frame buffers stand for the
 * original's frame (80h and 18h bytes). No entry register is needed beyond
 * part_facing's EAX high word, no entry shim; ECX after either hook is dead
 * at every caller (weapons.md). No game instruction is embedded. */
#include "es2/core/building.h"
static uint64_t __cdecl building_aim_turret_target(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE frame[0x80]={0};es2_weapon_graph g=weapons_graph(u,frame,ES2_HOOK_INDEX_turret_target);
    return es2_turret_target_registers(&math_native_memory,&g,a[0],a[1],r);
}
static uint64_t __cdecl building_aim_part_facing(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE frame[0x18]={0};es2_weapon_graph g=weapons_graph(u,frame,ES2_HOOK_INDEX_part_facing);
    return es2_part_facing_registers(&math_native_memory,&g,a[0],a[1],r);
}
static callee_counter turret_target_counters[]={
    {"call_47ebb8",ES2_COLLIDE_CALL,0x47ebb8,0},
    {"slot_20",ES2_COLLIDE_SLOT,0x20,0},
    {"call_47df19",ES2_COLLIDE_CALL,0x47df19,0},
    {"call_480304",ES2_COLLIDE_CALL,0x480304,0},
    {"call_48043c",ES2_COLLIDE_CALL,0x48043c,0},
    {"call_492990",ES2_COLLIDE_CALL,0x492990,0},
    {"call_403d5c",ES2_COLLIDE_CALL,0x403d5c,0},
};
static callee_counter part_facing_counters[]={
    {"slot_58",ES2_COLLIDE_SLOT,0x58,0},
    {"call_492990",ES2_COLLIDE_CALL,0x492990,0},
};
static callee_counter *building_aim_hook_counters(unsigned hook,unsigned *count) {
    if(hook==ES2_HOOK_INDEX_turret_target) { *count=sizeof(turret_target_counters)/sizeof(turret_target_counters[0]);return turret_target_counters; }
    if(hook==ES2_HOOK_INDEX_part_facing) { *count=sizeof(part_facing_counters)/sizeof(part_facing_counters[0]);return part_facing_counters; }
    *count=0;return NULL;
}
static const BYTE building_aim_target_sig[]={0x55,0x8b,0xec,0x83,0xc4,0x80};
static const BYTE building_aim_facing_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xe8};
static const BYTE building_aim_frame_lens[]={1,2,3};
#define BUILDING_AIM_EXT(name) \
    static const es2_hook_extension building_aim_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,building_aim_##name,TRUE}; \
    static uint32_t __cdecl building_aim_##name##32(const uint32_t *a,void *u) {return (uint32_t)building_aim_##name(a,u,weapons_no_registers);}
BUILDING_AIM_EXT(turret_target)
BUILDING_AIM_EXT(part_facing)
#undef BUILDING_AIM_EXT
static es2_hook_descriptor building_aim_hook_descriptors[]={
    {ES2_HOOK_ID(turret_target),building_aim_target_sig,6,6,building_aim_frame_lens,3,2,building_aim_turret_target32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(part_facing),building_aim_facing_sig,6,6,building_aim_frame_lens,3,2,building_aim_part_facing32,NULL,NULL,NULL,0},
};
#endif
