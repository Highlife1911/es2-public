#ifndef ES2_HDD_MAP_KEYS_HOOKS_H
#define ES2_HDD_MAP_KEYS_HOOKS_H
/* hdd_map_keys group (docs/reverse-engineering/cockpit.md, section
 * "HDD map key path"): the HDDMap key method 44cc68 (map_key; slot 4
 * of the table 49d9d8, called only by hdd_key 449ff4 at 44a165), the
 * synthetic map click 44d5c0 (map_press), the map frame's wingman pick
 * 44d82c (wingman_pick), the wingman index 433134 (wingman_index), the mode
 * steps 44ee48/44ee88 (mode_next/mode_prev) and the target cycle
 * 44ef08/44ef1c/44ef30 (target_cycle1/target_cycle0/target_cycle). Core in
 * core/src/hdd_map_keys.c. Every game call goes through the weapons guest
 * bridge and callee observer (weapons_graph) with the original's
 * EAX/EDX/ECX, so the nested hooks (the group's own, button_press 438d9c,
 * map_pick 44d888, map_mode 44d9f4, sfx_play 462830) keep their modes; the
 * label 44dd14, the cursor 486ece, the projection 48c2e4/48c444/48c57c/
 * 48c5fc, the map placement 44d24c, the event record 44d3cc, the selection
 * 44a748 and the visibility test 44f03c stay guest calls.
 *
 * EDX:EAX exact. ECX is dead after every call site of the nine: map_key's
 * only caller hdd_key returns at once (44a168..44a16d) and hdd_key's callers
 * test AL (hdd_keys_hooks.h); map_press is called only by map_key, which then sets AL
 * and returns; wingman_pick is followed by POP ECX in map_frame (44ca36);
 * all nine call sites of wingman_index pop ECX at once; mode_next/prev,
 * target_cycle1/0 are followed by POP ECX in map_key (44cddb, 44cde7,
 * 44ce6a, 44ce79); target_cycle returns through target_cycle1/0's epilogues
 * to those pops. So the framework returns the entry ECX: no exit shim.
 * map_press keeps its locals (EBP-24h..EBP-1: the event point, the
 * projected point and its map placement) in the adapter's scratch: every
 * local is written before it is read (by the core or by the callees
 * 486ece, 48c57c, 48c5fc, 44d24c, which write their outputs whole), so no
 * stack residue is read and there is NO entry shim. The others have no
 * local the core needs (target_cycle's iterator words live in the core).
 * All nine refuse compare mode (game calls; wingman_index is a leaf but is
 * offered no compare proof here). No game instruction is embedded. */
#include "es2/core/hdd_map_keys.h"
static uint64_t __cdecl hdd_map_keys_map_key(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_map_key);
    return es2_map_key_registers(&math_native_memory,&g,a[0],a[1],r);
}
static uint64_t __cdecl hdd_map_keys_map_press(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[0x30]={0};
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_map_press);
    const uint32_t f=(uint32_t)(uintptr_t)(scratch+0x28);
    return es2_map_press_registers(&math_native_memory,&g,f,a[0],a[1],a[2],r);
}
static uint64_t __cdecl hdd_map_keys_wingman_pick(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_wingman_pick);
    return es2_wingman_pick_registers(&math_native_memory,&g,a[0],r);
}
static uint64_t __cdecl hdd_map_keys_wingman_index(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_wingman_index);
    return es2_wingman_index_registers(&math_native_memory,&g,a[0],r);
}
static uint64_t __cdecl hdd_map_keys_mode_next(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_mode_next);
    return es2_mode_next_registers(&math_native_memory,&g,a[0],r);
}
static uint64_t __cdecl hdd_map_keys_mode_prev(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_mode_prev);
    return es2_mode_prev_registers(&math_native_memory,&g,a[0],r);
}
static uint64_t __cdecl hdd_map_keys_target_cycle1(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_target_cycle1);
    return es2_target_cycle1_registers(&math_native_memory,&g,a[0],r);
}
static uint64_t __cdecl hdd_map_keys_target_cycle0(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_target_cycle0);
    return es2_target_cycle0_registers(&math_native_memory,&g,a[0],r);
}
static uint64_t __cdecl hdd_map_keys_target_cycle(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_target_cycle);
    return es2_target_cycle_registers(&math_native_memory,&g,a[0],a[1],r);
}
/* Callee counters (hk_<hook>_<suffix>), incremented only while the hook is
 * non-original; keys are the callee VAs. */
#define HDD_MAP_KEYS_CALL(va) {"call_" #va,ES2_COLLIDE_CALL,0x##va,0}
static callee_counter map_key_counters[]={
    HDD_MAP_KEYS_CALL(44d5c0),HDD_MAP_KEYS_CALL(438d9c),HDD_MAP_KEYS_CALL(44ee48),HDD_MAP_KEYS_CALL(44ee88),
    HDD_MAP_KEYS_CALL(44ef08),HDD_MAP_KEYS_CALL(44ef1c),HDD_MAP_KEYS_CALL(44dd14),HDD_MAP_KEYS_CALL(462830),
};
static callee_counter map_press_counters[]={
    HDD_MAP_KEYS_CALL(44d3cc),HDD_MAP_KEYS_CALL(486ece),HDD_MAP_KEYS_CALL(48c2e4),HDD_MAP_KEYS_CALL(48c444),
    HDD_MAP_KEYS_CALL(48c57c),HDD_MAP_KEYS_CALL(48c5fc),HDD_MAP_KEYS_CALL(44d24c),
};
static callee_counter wingman_pick_counters[]={
    HDD_MAP_KEYS_CALL(44d888),HDD_MAP_KEYS_CALL(433134),HDD_MAP_KEYS_CALL(44a748),
};
static callee_counter mode_next_counters[]={HDD_MAP_KEYS_CALL(44d9f4)};
static callee_counter mode_prev_counters[]={HDD_MAP_KEYS_CALL(44d9f4)};
static callee_counter target_cycle1_counters[]={HDD_MAP_KEYS_CALL(44ef30)};
static callee_counter target_cycle0_counters[]={HDD_MAP_KEYS_CALL(44ef30)};
static callee_counter target_cycle_counters[]={HDD_MAP_KEYS_CALL(44f03c)};
#undef HDD_MAP_KEYS_CALL
#define HDD_MAP_KEYS_COUNTERS(name) \
    if(hook==ES2_HOOK_INDEX_##name){*count=sizeof(name##_counters)/sizeof(name##_counters[0]);return name##_counters;}
static callee_counter *hdd_map_keys_hook_counters(unsigned hook,unsigned *count) {
    HDD_MAP_KEYS_COUNTERS(map_key)
    HDD_MAP_KEYS_COUNTERS(map_press)
    HDD_MAP_KEYS_COUNTERS(wingman_pick)
    HDD_MAP_KEYS_COUNTERS(mode_next)
    HDD_MAP_KEYS_COUNTERS(mode_prev)
    HDD_MAP_KEYS_COUNTERS(target_cycle1)
    HDD_MAP_KEYS_COUNTERS(target_cycle0)
    HDD_MAP_KEYS_COUNTERS(target_cycle)
    *count=0;return NULL;
}
#undef HDD_MAP_KEYS_COUNTERS
/* 44cc68: push ebp; mov ebp,esp; push ebx; movzx eax,word [ebp+0Ch]; add
 * eax,-0Eh; mov ebx,[ebp+8]. Relocated 8 bytes (1,2,1,4). */
static const BYTE map_key_sig[]={0x55,0x8b,0xec,0x53,0x0f,0xb7,0x45,0x0c,0x83,0xc0,0xf2,0x8b,0x5d,0x08};
static const BYTE map_key_lens[]={1,2,1,4};
/* 44d5c0: push ebp; mov ebp,esp; add esp,-24h; push ebx; push esi; push
 * edi; mov ebx,[ebp+8]; mov esi,[ebp+0Ch]. Relocated 6 bytes (1,2,3). */
static const BYTE map_press_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xdc,0x53,0x56,0x57,0x8b,0x5d,0x08,0x8b,0x75,0x0c};
/* 44d82c: push ebp; mov ebp,esp; push ebx; push esi; mov ebx,[ebp+8]; lea
 * eax,[ebx+11Dh]. Relocated 5 bytes (1,2,1,1). */
static const BYTE wingman_pick_sig[]={0x55,0x8b,0xec,0x53,0x56,0x8b,0x5d,0x08,0x8d,0x83,0x1d,0x01,0x00,0x00};
static const BYTE wingman_pick_lens[]={1,2,1,1};
/* 433134: push ebp; mov ebp,esp; mov ecx,[ebp+8]; test ecx,ecx; je +14h;
 * xor eax,eax. Relocated 6 bytes (1,2,3). */
static const BYTE wingman_index_sig[]={0x55,0x8b,0xec,0x8b,0x4d,0x08,0x85,0xc9,0x74,0x14,0x33,0xc0};
/* 44ee48/44ee88: push ebp; mov ebp,esp; mov eax,[ebp+8]; mov edx,[eax+5Dh];
 * cmp edx,-1; je; add edx,-9/-0Bh. Relocated 6 bytes (1,2,3). */
static const BYTE mode_next_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x8b,0x50,0x5d,0x83,0xfa,0xff,0x74,0x2d,0x83,0xc2,0xf7};
static const BYTE mode_prev_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x8b,0x50,0x5d,0x83,0xfa,0xff,0x74,0x2f,0x83,0xc2,0xf5};
/* 44ef08/44ef1c: push ebp; mov ebp,esp; mov eax,[ebp+8]; push 1/0; push
 * eax. Relocated 6 bytes (1,2,3). */
static const BYTE target_cycle1_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x6a,0x01,0x50};
static const BYTE target_cycle0_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x6a,0x00,0x50};
/* 44ef30: push ebp; mov ebp,esp; add esp,-0Ch; mov eax,[4a9af8h]; push
 * ebx; push esi; push edi; mov esi,[ebp+8]. Relocated 6 bytes (1,2,3). */
static const BYTE target_cycle_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xf4,0xa1,0xf8,0x9a,0x4a,0x00,0x53,0x56,0x57,0x8b,0x75,0x08};
static const BYTE hdd_map_keys_lens[]={1,2,3};
#define HDD_MAP_KEYS_EXT(name) \
    static const es2_hook_extension hdd_map_keys_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,hdd_map_keys_##name,TRUE}; \
    static uint32_t __cdecl hdd_map_keys_##name##32(const uint32_t *a,void *u) {return (uint32_t)hdd_map_keys_##name(a,u,weapons_no_registers);}
HDD_MAP_KEYS_EXT(map_key)
HDD_MAP_KEYS_EXT(map_press)
HDD_MAP_KEYS_EXT(wingman_pick)
HDD_MAP_KEYS_EXT(wingman_index)
HDD_MAP_KEYS_EXT(mode_next)
HDD_MAP_KEYS_EXT(mode_prev)
HDD_MAP_KEYS_EXT(target_cycle1)
HDD_MAP_KEYS_EXT(target_cycle0)
HDD_MAP_KEYS_EXT(target_cycle)
#undef HDD_MAP_KEYS_EXT
static es2_hook_descriptor hdd_map_keys_hook_descriptors[]={
    {ES2_HOOK_ID(map_key),map_key_sig,14,8,map_key_lens,4,2,hdd_map_keys_map_key32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(map_press),map_press_sig,15,6,hdd_map_keys_lens,3,3,hdd_map_keys_map_press32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(wingman_pick),wingman_pick_sig,14,5,wingman_pick_lens,4,1,hdd_map_keys_wingman_pick32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(wingman_index),wingman_index_sig,12,6,hdd_map_keys_lens,3,1,hdd_map_keys_wingman_index32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(mode_next),mode_next_sig,17,6,hdd_map_keys_lens,3,1,hdd_map_keys_mode_next32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(mode_prev),mode_prev_sig,17,6,hdd_map_keys_lens,3,1,hdd_map_keys_mode_prev32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(target_cycle1),target_cycle1_sig,9,6,hdd_map_keys_lens,3,1,hdd_map_keys_target_cycle132,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(target_cycle0),target_cycle0_sig,9,6,hdd_map_keys_lens,3,1,hdd_map_keys_target_cycle032,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(target_cycle),target_cycle_sig,17,6,hdd_map_keys_lens,3,2,hdd_map_keys_target_cycle32,NULL,NULL,NULL,0},
};
#endif
