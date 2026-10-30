#ifndef ES2_MAP_CLICK_HOOKS_H
#define ES2_MAP_CLICK_HOOKS_H
/* map_click group (docs/reverse-engineering/cockpit.md, section
 * "map click 44d6e0"): map_click 44d6e0, the HDD map's handler of a
 * pending click, called only from the map's input slot 44c988 (44ca28).
 * Its three game calls go through the weapons guest bridge and callee
 * observer (weapons_graph) with the original's entry EAX/EDX/ECX: the map
 * pick 44d888 (a hook of the map_pick group, which keeps its mode), the
 * sound 462830 (the sfx_play hook) and the label update 44dd14 (H3). It
 * writes only the HDDMap object and the redraw byte 49d6ae. EDX:EAX are
 * exact; ECX is dead (44c988 pops it at 44ca2d). Entry EDX/ECX are passed
 * on to 44d888 as the original does; no other entry register and no
 * uninitialized local is read, so no entry shim. Refuses compare mode
 * (game calls). No game instruction is embedded. */
#include "es2/core/map_pick.h"
static uint64_t __cdecl map_click_map_click(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_map_click);
    return es2_map_click_registers(&math_native_memory,&g,a[0],r);
}
/* Callee counters (hk_map_click_<suffix>), incremented only while the hook
 * is non-original. */
static callee_counter map_click_counters[]={
    {"call_44d888",ES2_COLLIDE_CALL,0x44d888,0},
    {"call_462830",ES2_COLLIDE_CALL,0x462830,0},
    {"call_44dd14",ES2_COLLIDE_CALL,0x44dd14,0},
};
static callee_counter *map_click_hook_counters(unsigned hook,unsigned *count) {
    if(hook==ES2_HOOK_INDEX_map_click) { *count=sizeof(map_click_counters)/sizeof(map_click_counters[0]);return map_click_counters; }
    *count=0;return NULL;
}
/* push ebp; mov ebp,esp; push ebx; mov ebx,[ebp+8]; lea eax,[ebx+11Dh] */
static const BYTE map_click_sig[]={0x55,0x8b,0xec,0x53,0x8b,0x5d,0x08,0x8d,0x83,0x1d,0x01,0x00,0x00};
static const BYTE map_click_lens[]={1,2,1,3};
static const es2_hook_extension map_click_map_click_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,map_click_map_click,TRUE};
static uint32_t __cdecl map_click_map_click32(const uint32_t *a,void *u) {return (uint32_t)map_click_map_click(a,u,weapons_no_registers);}
static es2_hook_descriptor map_click_hook_descriptors[]={
    {ES2_HOOK_ID(map_click),map_click_sig,13,7,map_click_lens,4,1,map_click_map_click32,NULL,NULL,NULL,0},
};
#endif
