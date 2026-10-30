#ifndef ES2_LOGIC_HOOKS_H
#define ES2_LOGIC_HOOKS_H
/* Pool updates of the logic frame: explosion_update 40813c,
 * light_level 4076a0, fire_update 46b678. Included after weapons_hooks.h:
 * every game callee goes through its guest bridge and callee observer
 * (weapons_graph), so the three hooks refuse compare mode (light_level
 * writes a runtime-indexed light entry). No game instruction is embedded. */
#include "es2/core/logic.h"
static uint64_t __cdecl logic_explosion_update(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_explosion_update);
    return es2_explosion_update_registers(&math_native_memory,&g,a[0],r);
}
static uint64_t __cdecl logic_light_level(const uint32_t *a,void *u,const uint32_t *r) {
    (void)r;return es2_light_level_registers(&math_native_memory,(uint32_t)(uintptr_t)u,a[0],a[1]);
}
static uint64_t __cdecl logic_fire_update(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[32]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_fire_update);
    return es2_fire_update_registers(&math_native_memory,&g,a[0],r);
}
static const BYTE logic_explosion_update_sig[]={0x55,0x8b,0xec,0x51,0x53,0x56,0x57,0x8b,0x5d,0x08};
static const BYTE logic_light_level_sig[]={0x55,0x8b,0xec,0x33,0xd2,0x8b,0x0d,0x08,0x96,0x4a,0x00};
static const BYTE logic_fire_update_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xd4};
static const BYTE logic_push_lens[]={1,2,1,1};
static const BYTE logic_xor_lens[]={1,2,2};
static const BYTE logic_frame_lens[]={1,2,3};
#define LOGIC_EXT(name) static const es2_hook_extension logic_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,logic_##name,TRUE};
LOGIC_EXT(explosion_update)
LOGIC_EXT(light_level)
LOGIC_EXT(fire_update)
#undef LOGIC_EXT
#define LOGIC_LEGACY(name) static uint32_t __cdecl logic_##name##32(const uint32_t *a,void *u) {return (uint32_t)logic_##name(a,u,weapons_no_registers);}
LOGIC_LEGACY(explosion_update)
LOGIC_LEGACY(light_level)
LOGIC_LEGACY(fire_update)
#undef LOGIC_LEGACY
static es2_hook_descriptor logic_hook_descriptors[]={
    {ES2_HOOK_ID(explosion_update),logic_explosion_update_sig,10,5,logic_push_lens,4,1,logic_explosion_update32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(light_level),logic_light_level_sig,11,5,logic_xor_lens,3,2,logic_light_level32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(fire_update),logic_fire_update_sig,6,6,logic_frame_lens,3,1,logic_fire_update32,NULL,NULL,NULL,0},
};
#endif
