#ifndef ES2_REGION_DAMAGE_HOOKS_H
#define ES2_REGION_DAMAGE_HOOKS_H
/* Region-damage helpers (docs/reverse-engineering/hit_tests.md,
 * section "region-damage helpers"): region_absorb 40d3ec,
 * region_spread 40cf44, region_entry 40ca58 and region_find 40ca8c.
 * region_find calls 40ca58 and region_spread calls rng_bounded 492f24 and
 * 40d3ec through the weapons guest bridge and callee observer
 * (weapons_graph) with the original's registers, so those nested hooks
 * keep their modes; region_absorb and region_entry are leaves on guest
 * memory. region_spread keeps the damage word of its third argument slot
 * in 4 bytes of adapter scratch (only 40d3ec reads and writes it, through
 * the pointer; both callers pop the slot unread). EDX:EAX are exact; ECX
 * after the hooks is the entry ECX (the dispatch restores it) and dead at
 * every caller (walks in the document); no entry register or
 * uninitialized local is read, so no entry shim. Three refuse compare
 * mode (two call game code; region_absorb's stores are not declared);
 * region_entry accepts it (below). No game
 * instruction is embedded. */
#include "es2/core/region_damage.h"
static uint64_t __cdecl region_damage_region_absorb(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;return es2_region_absorb_registers(&math_native_memory,a[0],a[1],a[2],r[2],NULL);
}
static uint64_t __cdecl region_damage_region_spread(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[ES2_REGION_SPREAD_SCRATCH]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_region_spread);
    return es2_region_spread_registers(&math_native_memory,&g,a[0],a[1],a[2],a[3],r,NULL);
}
static uint64_t __cdecl region_damage_region_entry(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;(void)r;return es2_region_entry_registers(&math_native_memory,a[0],a[1],a[2],NULL);
}
static uint64_t __cdecl region_damage_region_find(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_region_find);
    return es2_region_find_registers(&math_native_memory,&g,a[0],a[1],a[2],r,NULL);
}
/* Callee counters (hk_<hook>_<suffix>), incremented only while the hook is
 * non-original. Keys: the callee VA. The leaves have no game calls. */
static callee_counter region_spread_counters[]={
    {"call_492f24",ES2_COLLIDE_CALL,0x492f24,0},
    {"call_40d3ec",ES2_COLLIDE_CALL,0x40d3ec,0},
};
static callee_counter region_find_counters[]={
    {"call_40ca58",ES2_COLLIDE_CALL,0x40ca58,0},
};
static callee_counter *region_damage_hook_counters(unsigned hook,unsigned *count) {
    if(hook==ES2_HOOK_INDEX_region_spread) { *count=2;return region_spread_counters; }
    if(hook==ES2_HOOK_INDEX_region_find) { *count=1;return region_find_counters; }
    *count=0;return NULL;
}
/* push ebp; mov ebp,esp; push ebx; push esi; mov eax,[ebp+8]; mov edx,[ebp+10h] */
static const BYTE region_absorb_sig[]={0x55,0x8b,0xec,0x53,0x56,0x8b,0x45,0x08,0x8b,0x55,0x10};
/* push ebp; mov ebp,esp; push ecx; push ebx; push esi; push edi; mov esi,[ebp+8] */
static const BYTE region_spread_sig[]={0x55,0x8b,0xec,0x51,0x53,0x56,0x57,0x8b,0x75,0x08};
/* push ebp; mov ebp,esp; push ebx; push esi; mov ecx,[ebp+8]; mov esi,[ebp+0Ch] */
static const BYTE region_entry_sig[]={0x55,0x8b,0xec,0x53,0x56,0x8b,0x4d,0x08,0x8b,0x75,0x0c};
/* push ebp; mov ebp,esp; push ebx; push esi; push edi; mov eax,[ebp+8]; mov ebx,[eax+2] */
static const BYTE region_find_sig[]={0x55,0x8b,0xec,0x53,0x56,0x57,0x8b,0x45,0x08,0x8b,0x58,0x02};
static const BYTE region_damage_push_lens[]={1,2,1,1};
/* region_entry in compare mode: its only store, the record's word
 * into the third argument when the key is found; EAX the entry pointer
 * from the record (or 0), EDX the index; default comparison. The other
 * three refuse compare mode (game calls, undeclared region words). */
static const es2_hook_span region_entry_spans[]={{2,0,2}};
#define REGION_DAMAGE_EXT(name,refuse) \
    static const es2_hook_extension region_damage_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,region_damage_##name,refuse}; \
    static uint32_t __cdecl region_damage_##name##32(const uint32_t *a,void *u) {return (uint32_t)region_damage_##name(a,u,weapons_no_registers);}
REGION_DAMAGE_EXT(region_absorb,TRUE)
REGION_DAMAGE_EXT(region_spread,TRUE)
REGION_DAMAGE_EXT(region_entry,FALSE)
REGION_DAMAGE_EXT(region_find,TRUE)
#undef REGION_DAMAGE_EXT
static es2_hook_descriptor region_damage_hook_descriptors[]={
    {ES2_HOOK_ID(region_absorb),region_absorb_sig,11,5,region_damage_push_lens,4,3,region_damage_region_absorb32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(region_spread),region_spread_sig,10,5,region_damage_push_lens,4,4,region_damage_region_spread32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(region_entry),region_entry_sig,11,5,region_damage_push_lens,4,3,region_damage_region_entry32,NULL,NULL,region_entry_spans,1},
    {ES2_HOOK_ID(region_find),region_find_sig,12,5,region_damage_push_lens,4,3,region_damage_region_find32,NULL,NULL,NULL,0},
};
#endif
