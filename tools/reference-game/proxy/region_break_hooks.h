#ifndef ES2_REGION_BREAK_HOOKS_H
#define ES2_REGION_BREAK_HOOKS_H
/* Region damage (docs/reverse-engineering/hit_tests.md, section
 * "region damage 40da38 and part break 40d434"): region_hit 40da38
 * (called by vehicle_part, mech_part_hit and flyer_part) and region_break
 * 40d434 (called only by 40da38). Every game callee goes through the
 * weapons guest bridge and callee observer (weapons_graph) with the
 * original's registers, so the nested hooks (region_absorb,
 * region_spread, region_find, fire_release, fire_alloc, fire_create,
 * euler_matrix, matrix_compose, pool_alloc, explosion_create,
 * debris_table, debris_pick, and region_break under region_hit) keep their
 * modes; the runtime frame setup 49630c and the node lookup (virtual +20h
 * of the node tree) run as original code, the transform 4801d4 as the
 * matrix_point hook (transform_helpers_hooks.h) in its own mode. region_hit keeps the damage word of its third argument
 * slot in 4 bytes of adapter scratch (40d3ec reads and writes it through
 * the pointer; the three callers pop the slot unread). region_break keeps
 * its frame in a 256-byte scratch (synthetic EBP scratch+F0h) and gets
 * 49630c through weapons_impact_guest_call; the adapter restores FS:0 from
 * the record's saved head at f-34h, as both epilogues do. EDX:EAX are
 * exact; ECX after the hooks is the entry ECX (the dispatch restores it)
 * and dead at every caller. No entry shim: neither the subtree nor the
 * callers' later callees read the stack the original leaves below a
 * caller's ESP except the presentation event records (residue audit in
 * the document). Both refuse compare mode (game calls). No game
 * instruction is embedded. */
#include "es2/core/region_damage.h"
static uint64_t __cdecl region_break_region_hit(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[ES2_REGION_HIT_SCRATCH]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_region_hit);
    return es2_region_hit_registers(&math_native_memory,&g,a[0],a[1],a[2],r,NULL);
}
static uint64_t __cdecl region_break_region_break(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[ES2_REGION_BREAK_SCRATCH]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_region_break);
    weapons_impact_context c={(uint32_t)(uintptr_t)u,(uint32_t)(uintptr_t)(scratch+ES2_REGION_BREAK_FRAME)};
    g.call=weapons_impact_guest_call;g.context=&c;
    uint64_t result=es2_region_break_registers(&math_native_memory,&g,a[0],a[1],r,NULL);
    uint32_t head=math_native_read32(NULL,(uint32_t)(uintptr_t)(scratch+ES2_REGION_BREAK_FRAME-0x34u));
    es2_machine_set_fs0(head);return result;
}
/* Callee counters (hk_<hook>_<suffix>), incremented only while the hook is
 * non-original. Keys: the callee VA, or the vtable offset for the node
 * lookup. */
static callee_counter region_hit_counters[]={
    {"call_40d3ec",ES2_COLLIDE_CALL,0x40d3ec,0},
    {"call_40cf44",ES2_COLLIDE_CALL,0x40cf44,0},
    {"call_40d434",ES2_COLLIDE_CALL,0x40d434,0},
};
static callee_counter region_break_counters[]={
    {"call_49630c",ES2_COLLIDE_CALL,0x49630c,0},
    {"call_40ca8c",ES2_COLLIDE_CALL,0x40ca8c,0},
    {"call_40cf44",ES2_COLLIDE_CALL,0x40cf44,0},
    {"call_46b62c",ES2_COLLIDE_CALL,0x46b62c,0},
    {"call_46b430",ES2_COLLIDE_CALL,0x46b430,0},
    {"call_46b48c",ES2_COLLIDE_CALL,0x46b48c,0},
    {"call_47ebb8",ES2_COLLIDE_CALL,0x47ebb8,0},
    {"slot_20",ES2_COLLIDE_SLOT,0x20,0},
    {"call_47fa20",ES2_COLLIDE_CALL,0x47fa20,0},
    {"call_4801d4",ES2_COLLIDE_CALL,0x4801d4,0},
    {"call_471b30",ES2_COLLIDE_CALL,0x471b30,0},
    {"call_407f1c",ES2_COLLIDE_CALL,0x407f1c,0},
    {"call_4089bc",ES2_COLLIDE_CALL,0x4089bc,0},
    {"call_4086bc",ES2_COLLIDE_CALL,0x4086bc,0},
};
static callee_counter *region_break_hook_counters(unsigned hook,unsigned *count) {
    if(hook==ES2_HOOK_INDEX_region_hit) {
        *count=sizeof(region_hit_counters)/sizeof(region_hit_counters[0]);return region_hit_counters; }
    if(hook==ES2_HOOK_INDEX_region_break) {
        *count=sizeof(region_break_counters)/sizeof(region_break_counters[0]);return region_break_counters; }
    *count=0;return NULL;
}
/* push ebp; mov ebp,esp; add esp,-10h; lea ecx,[ebp+10h]; push ebx; push
 * esi; push edi (40da38) / push ebp; mov ebp,esp; add esp,-0C4h; mov
 * eax,4988e8h (40d434). */
static const BYTE region_hit_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xf0,0x8d,0x4d,0x10,0x53,0x56,0x57};
static const BYTE region_break_sig[]={0x55,0x8b,0xec,0x81,0xc4,0x3c,0xff,0xff,0xff,0xb8,0xe8,0x88,0x49,0x00};
static const BYTE region_hit_lens[]={1,2,3},region_break_lens[]={1,2,6};
#define REGION_BREAK_EXT(name) \
    static const es2_hook_extension region_break_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,region_break_##name,TRUE}; \
    static uint32_t __cdecl region_break_##name##32(const uint32_t *a,void *u) {return (uint32_t)region_break_##name(a,u,weapons_no_registers);}
REGION_BREAK_EXT(region_hit)
REGION_BREAK_EXT(region_break)
#undef REGION_BREAK_EXT
static es2_hook_descriptor region_break_hook_descriptors[]={
    {ES2_HOOK_ID(region_hit),region_hit_sig,12,6,region_hit_lens,3,3,region_break_region_hit32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(region_break),region_break_sig,14,9,region_break_lens,3,2,region_break_region_break32,NULL,NULL,NULL,0},
};
#endif
