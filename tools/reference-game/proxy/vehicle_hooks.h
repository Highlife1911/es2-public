#ifndef ES2_VEHICLE_HOOKS_H
#define ES2_VEHICLE_HOOKS_H
/* Vehicle leaves (docs/reverse-engineering/vehicle.md):
 * condition_table 4151a4, condition_ratio 4152ec, sight_check 412608,
 * group_state 412c8c, target_bearing 41e758, part_position 41b60c.
 * Included after weapons_hooks.h: game callees go through its guest bridge
 * and callee observer (weapons_graph). All six refuse compare mode: the two
 * condition functions write a table or need the caller's EBX, the others
 * call game code. condition_ratio has an entry shim that hands the
 * caller's EBX to the replacement in EAX (4152ec never reads its entry
 * EAX). No game instruction is embedded. */
#include "es2/core/vehicle.h"
static uint64_t __cdecl vehicle_leaf_condition_table(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;(void)r;return es2_condition_table_registers(&math_native_memory,a[0],a[1]);
}
static uint64_t __cdecl vehicle_leaf_condition_ratio(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;return es2_condition_ratio_registers(&math_native_memory,a[0],a[1],a[2],r);
}
static uint64_t __cdecl vehicle_leaf_sight_check(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[48]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_sight_check);
    return es2_sight_check_registers(&math_native_memory,&g,a[0],a[1],r);
}
static uint64_t __cdecl vehicle_leaf_group_state(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_group_state);
    return es2_group_state_registers(&math_native_memory,&g,a[0],r);
}
static uint64_t __cdecl vehicle_leaf_target_bearing(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_target_bearing);
    return es2_target_bearing_registers(&math_native_memory,&g,a[0],a[1],a[2],r);
}
static uint64_t __cdecl vehicle_leaf_part_position(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[32]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_part_position);
    return es2_part_position_registers(&math_native_memory,&g,a[0],a[1],a[2],r);
}
/* 4152ec: EBX copied into EAX before the framework's dispatch. */
static int vehicle_leaf_ratio_entry(es2_machine *m,uint32_t bridge) {(void)bridge;m->eax=m->ebx;return 0;}
ES2_SHIM(vehicle_leaf_ratio_shim,0x152ecu,vehicle_leaf_ratio_entry,0);
static const BYTE vehicle_leaf_condition_table_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xec};
static const BYTE vehicle_leaf_condition_ratio_sig[]={0x55,0x8b,0xec,0x53,0x56};
static const BYTE vehicle_leaf_sight_check_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xdc};
static const BYTE vehicle_leaf_group_state_sig[]={0x55,0x8b,0xec,0x51,0x53};
static const BYTE vehicle_leaf_target_bearing_sig[]={0x55,0x8b,0xec,0x51,0x53};
static const BYTE vehicle_leaf_part_position_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xe4};
static const BYTE vehicle_leaf_frame_lens[]={1,2,3},vehicle_leaf_saved_lens[]={1,2,1,1};
#define VEHICLE_LEAF_EXT(name) static const es2_hook_extension vehicle_leaf_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,vehicle_leaf_##name,TRUE};
#define VEHICLE_LEAF_LEGACY(name) static uint32_t __cdecl vehicle_leaf_##name##32(const uint32_t *a,void *u) {return (uint32_t)vehicle_leaf_##name(a,u,weapons_no_registers);}
VEHICLE_LEAF_EXT(condition_table) VEHICLE_LEAF_EXT(condition_ratio) VEHICLE_LEAF_EXT(sight_check)
VEHICLE_LEAF_EXT(group_state) VEHICLE_LEAF_EXT(target_bearing) VEHICLE_LEAF_EXT(part_position)
VEHICLE_LEAF_LEGACY(condition_table) VEHICLE_LEAF_LEGACY(condition_ratio) VEHICLE_LEAF_LEGACY(sight_check)
VEHICLE_LEAF_LEGACY(group_state) VEHICLE_LEAF_LEGACY(target_bearing) VEHICLE_LEAF_LEGACY(part_position)
#undef VEHICLE_LEAF_EXT
#undef VEHICLE_LEAF_LEGACY
static es2_hook_descriptor vehicle_leaf_hook_descriptors[]={
    {ES2_HOOK_ID(condition_table),vehicle_leaf_condition_table_sig,6,6,vehicle_leaf_frame_lens,3,2,vehicle_leaf_condition_table32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(condition_ratio),vehicle_leaf_condition_ratio_sig,5,5,vehicle_leaf_saved_lens,4,3,vehicle_leaf_condition_ratio32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(sight_check),vehicle_leaf_sight_check_sig,6,6,vehicle_leaf_frame_lens,3,2,vehicle_leaf_sight_check32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(group_state),vehicle_leaf_group_state_sig,5,5,vehicle_leaf_saved_lens,4,1,vehicle_leaf_group_state32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(target_bearing),vehicle_leaf_target_bearing_sig,5,5,vehicle_leaf_saved_lens,4,3,vehicle_leaf_target_bearing32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(part_position),vehicle_leaf_part_position_sig,6,6,vehicle_leaf_frame_lens,3,3,vehicle_leaf_part_position32,NULL,NULL,NULL,0},
};
#endif
