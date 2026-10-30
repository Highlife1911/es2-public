#ifndef ES2_HDD_ORDERS_HOOKS_H
#define ES2_HDD_ORDERS_HOOKS_H
/* hdd_orders group (docs/reverse-engineering/cockpit.md, section
 * "HDD map order controls"): the HDD map's event handler 44d450
 * (map_areas), the map mode 44d9f4 (map_mode), the wingman selection
 * 44da98 (wingman_select) and the order cancel 44dc10 (order_cancel), all
 * on the HDDMap object. Every game call goes through the weapons guest
 * bridge and callee observer (weapons_graph) with the original's entry
 * EAX/EDX/ECX, so the nested hooks (map_mode and wingman_select within the
 * group, the HDD button press 438d9c = button_press of hud_keys) keep their
 * own modes; the label/lamp/cursor helpers 44dc6c, 44dd14, 44ede0, 44ee00,
 * 44ee24, 433e3c and the mouse position 486e7c stay guest calls (H3).
 * map_areas keeps its 8-byte local (the mouse point for 486e7c) in the
 * adapter's scratch. EDX:EAX exact; ECX is dead after every call site of
 * the four (POP ECX, an overwrite, or a callee that writes ECX before
 * reading it), so ECX is not reproduced and there is NO entry or exit
 * shim. Entry EDX/ECX are only passed on to the first callee (and
 * returned by map_areas when nothing else writes EDX); no uninitialized
 * local is read. All four refuse compare mode (game calls). No game
 * instruction is embedded. */
#include "es2/core/hdd_orders.h"
static uint64_t __cdecl hdd_orders_map_areas(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[16]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_map_areas);
    return es2_map_areas_registers(&math_native_memory,&g,a[0],r);
}
static uint64_t __cdecl hdd_orders_map_mode(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_map_mode);
    return es2_map_mode_registers(&math_native_memory,&g,a[0],a[1],r);
}
static uint64_t __cdecl hdd_orders_wingman_select(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_wingman_select);
    return es2_wingman_select_registers(&math_native_memory,&g,a[0],a[1],r);
}
static uint64_t __cdecl hdd_orders_order_cancel(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_order_cancel);
    return es2_order_cancel_registers(&math_native_memory,&g,a[0],r);
}
/* Callee counters (hk_<hook>_<suffix>), incremented only while the hook is
 * non-original. */
static callee_counter map_areas_counters[]={
    {"call_438d9c",ES2_COLLIDE_CALL,0x438d9c,0},
    {"call_44d9f4",ES2_COLLIDE_CALL,0x44d9f4,0},
    {"call_486e7c",ES2_COLLIDE_CALL,0x486e7c,0},
    {"call_44dc6c",ES2_COLLIDE_CALL,0x44dc6c,0},
};
static callee_counter map_mode_counters[]={
    {"call_433e3c",ES2_COLLIDE_CALL,0x433e3c,0},
    {"call_44dc6c",ES2_COLLIDE_CALL,0x44dc6c,0},
    {"call_44dd14",ES2_COLLIDE_CALL,0x44dd14,0},
};
static callee_counter wingman_select_counters[]={
    {"call_44ede0",ES2_COLLIDE_CALL,0x44ede0,0},
    {"call_44d9f4",ES2_COLLIDE_CALL,0x44d9f4,0},
    {"call_44ee00",ES2_COLLIDE_CALL,0x44ee00,0},
    {"call_44ee24",ES2_COLLIDE_CALL,0x44ee24,0},
};
static callee_counter order_cancel_counters[]={
    {"call_44d9f4",ES2_COLLIDE_CALL,0x44d9f4,0},
    {"call_44da98",ES2_COLLIDE_CALL,0x44da98,0},
};
#define HDD_ORDERS_COUNTERS(name) \
    if(hook==ES2_HOOK_INDEX_##name){*count=sizeof(name##_counters)/sizeof(name##_counters[0]);return name##_counters;}
static callee_counter *hdd_orders_hook_counters(unsigned hook,unsigned *count) {
    HDD_ORDERS_COUNTERS(map_areas)
    HDD_ORDERS_COUNTERS(map_mode)
    HDD_ORDERS_COUNTERS(wingman_select)
    HDD_ORDERS_COUNTERS(order_cancel)
    *count=0;return NULL;
}
#undef HDD_ORDERS_COUNTERS
/* 44d450: push ebp; mov ebp,esp; add esp,-0Ch; push ebx; push esi; push
 * edi; mov ebx,[ebp+8]. Relocated 6 bytes (1,2,3). */
static const BYTE map_areas_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xf4,0x53,0x56,0x57,0x8b,0x5d,0x08};
static const BYTE map_areas_lens[]={1,2,3};
/* 44d9f4: push ebp; mov ebp,esp; push ebx; mov eax,[49d6b0h]; push esi;
 * push edi; mov edi,[eax+8]. Relocated 9 bytes (1,2,1,5). */
static const BYTE map_mode_sig[]={0x55,0x8b,0xec,0x53,0xa1,0xb0,0xd6,0x49,0x00,0x56,0x57,0x8b,0x78,0x08};
static const BYTE map_mode_lens[]={1,2,1,5};
/* 44da98: push ebp; mov ebp,esp; push ebx; mov ebx,[ebp+8]; mov
 * eax,[ebx+105h]. Relocated 7 bytes (1,2,1,3). */
static const BYTE wingman_select_sig[]={0x55,0x8b,0xec,0x53,0x8b,0x5d,0x08,0x8b,0x83,0x05,0x01,0x00,0x00};
static const BYTE wingman_select_lens[]={1,2,1,3};
/* 44dc10: push ebp; mov ebp,esp; push ebx; mov ebx,[ebp+8]; push -1;
 * push ebx. Relocated 7 bytes (1,2,1,3). */
static const BYTE order_cancel_sig[]={0x55,0x8b,0xec,0x53,0x8b,0x5d,0x08,0x6a,0xff,0x53};
static const BYTE order_cancel_lens[]={1,2,1,3};
#define HDD_ORDERS_EXT(name) \
    static const es2_hook_extension hdd_orders_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,hdd_orders_##name,TRUE}; \
    static uint32_t __cdecl hdd_orders_##name##32(const uint32_t *a,void *u) {return (uint32_t)hdd_orders_##name(a,u,weapons_no_registers);}
HDD_ORDERS_EXT(map_areas)
HDD_ORDERS_EXT(map_mode)
HDD_ORDERS_EXT(wingman_select)
HDD_ORDERS_EXT(order_cancel)
#undef HDD_ORDERS_EXT
static es2_hook_descriptor hdd_orders_hook_descriptors[]={
    {ES2_HOOK_ID(map_areas),map_areas_sig,12,6,map_areas_lens,3,1,hdd_orders_map_areas32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(map_mode),map_mode_sig,14,9,map_mode_lens,4,2,hdd_orders_map_mode32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(wingman_select),wingman_select_sig,13,7,wingman_select_lens,4,2,hdd_orders_wingman_select32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(order_cancel),order_cancel_sig,10,7,order_cancel_lens,4,1,hdd_orders_order_cancel32,NULL,NULL,NULL,0},
};
#endif
