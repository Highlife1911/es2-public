#ifndef ES2_HDD_KEYS_HOOKS_H
#define ES2_HDD_KEYS_HOOKS_H
/* hdd_keys group (docs/reverse-engineering/cockpit.md, section
 * "HDD key method 449ff4"): the HDD gauge's key method 449ff4
 * (hdd_key; vtable 49d64c slot +0Ch, offered by hud_key 432bc8 in display
 * mode 1). The core is appended to core/src/hdd_orders.c. Every game call
 * goes through the weapons guest bridge and callee observer
 * (weapons_graph) with the original's registers, so the nested hook
 * button_press 438d9c (hud_keys) keeps its own mode; the page switch
 * 44a60c, the status view 450b88 and the active page's key slot +10h stay
 * guest calls (the map page's slot 44cc68 is the hook map_key,
 * reached through the bridge). EDX:EAX exact; ECX is dead at both
 * call sites (hud_key 4330d9/4330fb test AL, then return through its
 * epilogue; hud_key's callers read only AL), so the framework returns the
 * entry ECX. No local, no uninitialized read: NO entry or exit shim.
 * Refuses compare mode (game calls). No game instruction is embedded. */
#include "es2/core/hdd_orders.h"
static uint64_t __cdecl hdd_keys_hdd_key(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_hdd_key);
    return es2_hdd_key_registers(&math_native_memory,&g,a[0],a[1],r);
}
/* Callee counters (hk_hdd_key_<suffix>), incremented only while the hook
 * is non-original. */
static callee_counter hdd_key_counters[]={
    {"call_438d9c",ES2_COLLIDE_CALL,0x438d9c,0},
    {"call_44a60c",ES2_COLLIDE_CALL,0x44a60c,0},
    {"call_450b88",ES2_COLLIDE_CALL,0x450b88,0},
    {"slot_10",ES2_COLLIDE_SLOT,0x10,0},
};
static callee_counter *hdd_keys_hook_counters(unsigned hook,unsigned *count) {
    if(hook==ES2_HOOK_INDEX_hdd_key){*count=sizeof(hdd_key_counters)/sizeof(hdd_key_counters[0]);return hdd_key_counters;}
    *count=0;return NULL;
}
/* 449ff4: push ebp; mov ebp,esp; push ebx; mov eax,[ebp+0Ch]; mov
 * ebx,[ebp+8]. Relocated 7 bytes (1,2,1,3). */
static const BYTE hdd_key_sig[]={0x55,0x8b,0xec,0x53,0x8b,0x45,0x0c,0x8b,0x5d,0x08};
static const BYTE hdd_key_lens[]={1,2,1,3};
static const es2_hook_extension hdd_keys_hdd_key_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,hdd_keys_hdd_key,TRUE};
static uint32_t __cdecl hdd_keys_hdd_key32(const uint32_t *a,void *u) {return (uint32_t)hdd_keys_hdd_key(a,u,weapons_no_registers);}
static es2_hook_descriptor hdd_keys_hook_descriptors[]={
    {ES2_HOOK_ID(hdd_key),hdd_key_sig,10,7,hdd_key_lens,4,2,hdd_keys_hdd_key32,NULL,NULL,NULL,0},
};
#endif
