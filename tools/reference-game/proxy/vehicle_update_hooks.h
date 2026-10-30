#ifndef ES2_VEHICLE_UPDATE_HOOKS_H
#define ES2_VEHICLE_UPDATE_HOOKS_H
/* Per-vehicle update (docs/reverse-engineering/vehicle.md, "Per-vehicle
 * update"): combat_rating 41edd8, condition_record 41b4ec, device_state
 * 41aa10, device_word 41aa44, ready_tone 41b0bc, weapon_tally 40fbdc and
 * vehicle_update 41aa5c. Included after weapons_hooks.h: game callees go
 * through its guest bridge and callee observer (weapons_graph). The two
 * device readers are pure and allow compare mode; the others call game code
 * and refuse it. No local is read before it is written and the entry EAX
 * that 41aa5c forwards is part of the framework's entry registers, so no
 * entry shim. No game instruction is embedded. */
#include "es2/core/vehicle_update.h"
static uint64_t __cdecl vehicle_update_combat_rating(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[128]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_combat_rating);
    return es2_combat_rating_registers(&math_native_memory,&g,a[0],r);
}
static uint64_t __cdecl vehicle_update_condition_record(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_condition_record);
    return es2_condition_record_registers(&math_native_memory,&g,a[0],a[1],r);
}
static uint64_t __cdecl vehicle_update_device_state(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;(void)r;return es2_device_state_registers(&math_native_memory,a[0]);
}
static uint64_t __cdecl vehicle_update_device_word(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;(void)r;return es2_device_word_registers(&math_native_memory,a[0]);
}
static uint64_t __cdecl vehicle_update_ready_tone(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_ready_tone);
    return es2_ready_tone_registers(&math_native_memory,&g,a[0],r);
}
static uint64_t __cdecl vehicle_update_weapon_tally(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[128]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_weapon_tally);
    return es2_weapon_tally_registers(&math_native_memory,&g,a[0],a[1],r);
}
static uint64_t __cdecl vehicle_update_vehicle_update(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[128]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_vehicle_update);
    return es2_vehicle_update_registers(&math_native_memory,&g,a[0],r);
}
static const BYTE vehicle_update_combat_rating_sig[]={0x55,0x8b,0xec,0x83,0xc4,0x8c};
static const BYTE vehicle_update_condition_record_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08};
static const BYTE vehicle_update_device_state_sig[]={0x55,0x8b,0xec,0x56,0x8b,0x45,0x08};
static const BYTE vehicle_update_device_word_sig[]={0x55,0x8b,0xec,0x8b,0x55,0x08};
static const BYTE vehicle_update_ready_tone_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08};
static const BYTE vehicle_update_weapon_tally_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xf4};
static const BYTE vehicle_update_vehicle_update_sig[]={0x55,0x8b,0xec,0x81,0xc4,0x74,0xff,0xff,0xff};
static const BYTE vehicle_update_frame_lens[]={1,2,3},vehicle_update_esi_lens[]={1,2,1,3},vehicle_update_long_lens[]={1,2,6};
#define VEHICLE_UPDATE_EXT(name,refuse) static const es2_hook_extension vehicle_update_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,vehicle_update_##name,refuse};
#define VEHICLE_UPDATE_LEGACY(name) static uint32_t __cdecl vehicle_update_##name##32(const uint32_t *a,void *u) {return (uint32_t)vehicle_update_##name(a,u,weapons_no_registers);}
VEHICLE_UPDATE_EXT(combat_rating,TRUE) VEHICLE_UPDATE_EXT(condition_record,TRUE) VEHICLE_UPDATE_EXT(device_state,FALSE)
VEHICLE_UPDATE_EXT(device_word,FALSE) VEHICLE_UPDATE_EXT(ready_tone,TRUE) VEHICLE_UPDATE_EXT(weapon_tally,TRUE)
VEHICLE_UPDATE_EXT(vehicle_update,TRUE)
VEHICLE_UPDATE_LEGACY(combat_rating) VEHICLE_UPDATE_LEGACY(condition_record) VEHICLE_UPDATE_LEGACY(device_state)
VEHICLE_UPDATE_LEGACY(device_word) VEHICLE_UPDATE_LEGACY(ready_tone) VEHICLE_UPDATE_LEGACY(weapon_tally)
VEHICLE_UPDATE_LEGACY(vehicle_update)
#undef VEHICLE_UPDATE_EXT
#undef VEHICLE_UPDATE_LEGACY
static es2_hook_descriptor vehicle_update_hook_descriptors[]={
    {ES2_HOOK_ID(combat_rating),vehicle_update_combat_rating_sig,6,6,vehicle_update_frame_lens,3,1,vehicle_update_combat_rating32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(condition_record),vehicle_update_condition_record_sig,6,6,vehicle_update_frame_lens,3,2,vehicle_update_condition_record32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(device_state),vehicle_update_device_state_sig,7,7,vehicle_update_esi_lens,4,1,vehicle_update_device_state32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(device_word),vehicle_update_device_word_sig,6,6,vehicle_update_frame_lens,3,1,vehicle_update_device_word32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(ready_tone),vehicle_update_ready_tone_sig,6,6,vehicle_update_frame_lens,3,1,vehicle_update_ready_tone32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(weapon_tally),vehicle_update_weapon_tally_sig,6,6,vehicle_update_frame_lens,3,2,vehicle_update_weapon_tally32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(vehicle_update),vehicle_update_vehicle_update_sig,9,9,vehicle_update_long_lens,3,1,vehicle_update_vehicle_update32,NULL,NULL,NULL,0},
};
#endif
