#ifndef ES2_INPUT_HOOKS_H
#define ES2_INPUT_HOOKS_H
/* input group (docs/reverse-engineering/input.md): fire_selected
 * 410dbc, fire_line 415608, aim_point 41b728, leg_input 4160dc, the
 * children of the input dispatcher 460870. Included after weapons_hooks.h:
 * game callees go through its guest bridge and callee observer
 * (weapons_graph). All four call game code and refuse compare mode. None
 * reads an uninitialized local, so no entry shim and no scratch. No game
 * instruction is embedded. */
#include "es2/core/input.h"
static uint64_t __cdecl input_fire_selected(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_fire_selected);
    return es2_fire_selected_registers(&math_native_memory,&g,a[0],a[1],a[2],r);
}
static uint64_t __cdecl input_fire_line(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_fire_line);
    return es2_fire_line_registers(&math_native_memory,&g,a[0],a[1],r);
}
static uint64_t __cdecl input_aim_point(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_aim_point);
    return es2_aim_point_registers(&math_native_memory,&g,a[0],a[1],a[2],a[3],r);
}
static uint64_t __cdecl input_leg_input(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_leg_input);
    return es2_leg_input_registers(&math_native_memory,&g,a[0],a[1],a[2],r);
}
static const BYTE input_fire_selected_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xf8};
static const BYTE input_fire_line_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xe0};
static const BYTE input_aim_point_sig[]={0x55,0x8b,0xec,0x51,0x53,0x56,0x57};
static const BYTE input_leg_input_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xf4};
static const BYTE input_frame_lens[]={1,2,3};
static const BYTE input_push_lens[]={1,2,1,1};
#define INPUT_EXT(name) static const es2_hook_extension input_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,input_##name,TRUE};
INPUT_EXT(fire_selected)
INPUT_EXT(fire_line)
INPUT_EXT(aim_point)
INPUT_EXT(leg_input)
#undef INPUT_EXT
#define INPUT_LEGACY(name) static uint32_t __cdecl input_##name##32(const uint32_t *a,void *u) {return (uint32_t)input_##name(a,u,weapons_no_registers);}
INPUT_LEGACY(fire_selected)
INPUT_LEGACY(fire_line)
INPUT_LEGACY(aim_point)
INPUT_LEGACY(leg_input)
#undef INPUT_LEGACY
static es2_hook_descriptor input_hook_descriptors[]={
    {ES2_HOOK_ID(fire_selected),input_fire_selected_sig,6,6,input_frame_lens,3,3,input_fire_selected32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(fire_line),input_fire_line_sig,6,6,input_frame_lens,3,2,input_fire_line32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(aim_point),input_aim_point_sig,7,5,input_push_lens,4,4,input_aim_point32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(leg_input),input_leg_input_sig,6,6,input_frame_lens,3,3,input_leg_input32,NULL,NULL,NULL,0},
};
#endif
