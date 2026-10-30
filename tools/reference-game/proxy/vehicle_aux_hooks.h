#ifndef ES2_VEHICLE_AUX_HOOKS_H
#define ES2_VEHICLE_AUX_HOOKS_H
/* More vehicle leaves (docs/reverse-engineering/vehicle.md,
 * section "Shield, damage, facing and keys"): shield_max_set 413ab8, shield_capacity 417bec,
 * damage_score 41537c, facing_part 41b534 (vehicle virtual +54h) and
 * vehicle_keys 4157c8 (vehicle virtual +2Ch). Every game callee
 * (multiply_q10, part_ratio, shield_max_set, the vehicle's slot +58h,
 * aim_angles, weapon_keys, euler_matrix, 4801d4 and the widget call 438d9c)
 * goes through the weapons guest bridge and callee observer
 * (weapons_graph) with the original's registers, so nested hooks keep their
 * modes; vehicle_keys' calls go through weapon_system's ESI-loading bridge
 * with the ESI the original holds there. All five refuse compare mode.
 * vehicle_keys has an entry shim: 4157c8 returns the caller's ESI high word
 * and passes the caller's ESI to weapon_keys, and its entry EAX is dead, so
 * the shim copies ESI into EAX before the framework's dispatch. The frames of facing_part and
 * vehicle_keys are the adapters' scratch. No game instruction is embedded;
 * the shim only moves a register. */
#include "es2/core/vehicle.h"
static uint64_t __cdecl vehicle_aux_shield_max_set(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;return es2_shield_max_set_registers(&math_native_memory,a[0],a[1],r);
}
static uint64_t __cdecl vehicle_aux_shield_capacity(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_shield_capacity);
    return es2_shield_capacity_registers(&math_native_memory,&g,a[0],r);
}
static uint64_t __cdecl vehicle_aux_damage_score(const uint32_t *a,void *u,const uint32_t *r) {
    return es2_damage_score_registers(&math_native_memory,(uint32_t)(uintptr_t)u,a[0],r);
}
static uint64_t __cdecl vehicle_aux_facing_part(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE frame[ES2_FACING_PART_SCRATCH]={0};es2_weapon_graph g=weapons_graph(u,frame,ES2_HOOK_INDEX_facing_part);
    return es2_facing_part_registers(&math_native_memory,&g,a[0],a[1],r);
}
/* r[0] is the caller's ESI (entry shim below; 0 on the legacy path). */
static uint64_t __cdecl vehicle_aux_vehicle_keys(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE frame[ES2_VEHICLE_KEYS_SCRATCH]={0};uint32_t esi=r[0];
    es2_weapon_graph g=weapons_graph(u,frame,ES2_HOOK_INDEX_vehicle_keys);
    g.call=weapon_system_guest_call;g.context=&esi;
    return es2_vehicle_keys_registers(&math_native_memory,&g,a[0],a[1],r,&esi);
}
/* 4157c8: the caller's ESI in EAX. */
static int vehicle_aux_keys_entry(es2_machine *m,uint32_t bridge) {(void)bridge;m->eax=m->esi;return 0;}
ES2_SHIM(vehicle_aux_keys_shim,0x157c8u,vehicle_aux_keys_entry,0);
/* Callee counters (hk_<hook>_<suffix>), incremented only while the hook is
 * non-original; shield_max_set and damage_score make no game calls. */
static callee_counter shield_capacity_counters[]={
    {"call_47e0b0",ES2_COLLIDE_CALL,0x47e0b0,0},
    {"call_40dbc0",ES2_COLLIDE_CALL,0x40dbc0,0},
    {"call_413ab8",ES2_COLLIDE_CALL,0x413ab8,0},
};
static callee_counter facing_part_counters[]={
    {"slot_58",ES2_COLLIDE_SLOT,0x58,0},
    {"call_492990",ES2_COLLIDE_CALL,0x492990,0},
};
static callee_counter vehicle_keys_counters[]={
    {"call_410f78",ES2_COLLIDE_CALL,0x410f78,0},
    {"call_47ebb8",ES2_COLLIDE_CALL,0x47ebb8,0},
    {"call_4801d4",ES2_COLLIDE_CALL,0x4801d4,0},
    {"call_438d9c",ES2_COLLIDE_CALL,0x438d9c,0},
};
static callee_counter *vehicle_aux_hook_counters(unsigned hook,unsigned *count) {
#define VEHICLE_AUX_COUNTERS(name) if(hook==ES2_HOOK_INDEX_##name) { \
        *count=sizeof(name##_counters)/sizeof(name##_counters[0]);return name##_counters; }
    VEHICLE_AUX_COUNTERS(shield_capacity)
    VEHICLE_AUX_COUNTERS(facing_part)
    VEHICLE_AUX_COUNTERS(vehicle_keys)
#undef VEHICLE_AUX_COUNTERS
    *count=0;return NULL;
}
static const BYTE vehicle_aux_max_set_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x66,0x8b,0x55,0x0c};
static const BYTE vehicle_aux_capacity_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xec,0x53,0x56,0x57};
static const BYTE vehicle_aux_score_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xf4,0x53,0x56,0x57};
static const BYTE vehicle_aux_facing_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xe8,0x53,0x56};
static const BYTE vehicle_aux_keys_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xc4,0xa1,0x84,0x9b,0x4a,0x00};
static const BYTE vehicle_aux_frame_lens[]={1,2,3};
#define VEHICLE_AUX_EXT(name) \
    static const es2_hook_extension vehicle_aux_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,vehicle_aux_##name,TRUE}; \
    static uint32_t __cdecl vehicle_aux_##name##32(const uint32_t *a,void *u) {return (uint32_t)vehicle_aux_##name(a,u,weapons_no_registers);}
VEHICLE_AUX_EXT(shield_max_set)
VEHICLE_AUX_EXT(shield_capacity)
VEHICLE_AUX_EXT(damage_score)
VEHICLE_AUX_EXT(facing_part)
VEHICLE_AUX_EXT(vehicle_keys)
#undef VEHICLE_AUX_EXT
static es2_hook_descriptor vehicle_aux_hook_descriptors[]={
    {ES2_HOOK_ID(shield_max_set),vehicle_aux_max_set_sig,10,6,vehicle_aux_frame_lens,3,2,vehicle_aux_shield_max_set32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(shield_capacity),vehicle_aux_capacity_sig,9,6,vehicle_aux_frame_lens,3,1,vehicle_aux_shield_capacity32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(damage_score),vehicle_aux_score_sig,9,6,vehicle_aux_frame_lens,3,1,vehicle_aux_damage_score32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(facing_part),vehicle_aux_facing_sig,8,6,vehicle_aux_frame_lens,3,2,vehicle_aux_facing_part32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(vehicle_keys),vehicle_aux_keys_sig,11,6,vehicle_aux_frame_lens,3,2,vehicle_aux_vehicle_keys32,NULL,NULL,NULL,0},
};
#endif
