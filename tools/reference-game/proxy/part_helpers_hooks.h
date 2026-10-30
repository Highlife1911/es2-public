#ifndef ES2_PART_HELPERS_HOOKS_H
#define ES2_PART_HELPERS_HOOKS_H
/* part_helpers group (docs/reverse-engineering/vehicle.md, section
 * "Vehicle part and weapon-group helpers"): the weapon group's
 * slot-0 bodies group_fire_any 411a1d (table 499338) and group_fire_sel
 * 41194a (table 499238), reached from vehicle_update through the group's
 * vtable; part_group_dead 40d9f8 and its callee part_list_dead 40cf10
 * (under vehicle_part); part_group_max 40dc58 (under random_damage);
 * mission_guard 4240f4 (under vehicle_part, no arguments); actor_offsets
 * 411a74 (ACTOR/FLYER slot +30h, also under base_offsets). The hooks sit at
 * the function entries, so the vtable and direct callers all go through
 * them. Game calls (group_fire 4107e4, 40cf10, the memset thunk 49681a)
 * go through the weapons guest bridge and callee observer (weapons_graph)
 * with the original's registers, so nested hooks keep their modes. No
 * entry shim: no uninitialized local and no caller register other than the
 * documented ones is read (the entry ECX of 411a1d becomes the high word
 * of the value dword it pushes, the entry EDX of 4240f4 is its result EDX;
 * both come from the dispatch context). ECX after each hook is the entry
 * ECX (the framework restores it); every caller writes ECX before reading
 * it (vehicle.md). part_list_dead, part_group_dead (its only callee is the
 * pure 40cf10), part_group_max and mission_guard accept compare mode; the
 * two slot-0 bodies refuse it (group_fire's weapon virtuals, as group_fire
 * itself), actor_offsets refuses it (its two records are written through
 * game code behind argument pointers). mission_guard's prologue `cmp word
 * [4a9e52h],0` (66 83 3D) is relocatable (hook.c). No
 * game instruction is embedded. */
#include "es2/core/vehicle.h"
#include "es2/core/vehicle_update.h"
static uint64_t __cdecl part_helpers_group_fire_any(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_group_fire_any);
    return es2_group_fire_any_registers(&math_native_memory,&g,a[0],a[1],a[2],r);
}
static uint64_t __cdecl part_helpers_group_fire_sel(const uint32_t *a,void *u,const uint32_t *r) {
    (void)r;es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_group_fire_sel);
    return es2_group_fire_sel_registers(&math_native_memory,&g,a[0],a[1],a[2]);
}
static uint64_t __cdecl part_helpers_part_list_dead(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;(void)r;return es2_part_list_dead_registers(&math_native_memory,a[0],a[1]);
}
static uint64_t __cdecl part_helpers_part_group_dead(const uint32_t *a,void *u,const uint32_t *r) {
    (void)r;es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_part_group_dead);
    return es2_part_group_dead_registers(&math_native_memory,&g,a[0],a[1]);
}
static uint64_t __cdecl part_helpers_part_group_max(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;(void)r;return es2_part_group_max_registers(&math_native_memory,a[0],a[1]);
}
static uint64_t __cdecl part_helpers_mission_guard(const uint32_t *a,void *u,const uint32_t *r) {
    (void)a;return es2_mission_guard_registers(&math_native_memory,(uint32_t)(uintptr_t)u,r);
}
static uint64_t __cdecl part_helpers_actor_offsets(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_actor_offsets);
    return es2_actor_offsets_registers(&math_native_memory,&g,a[1],a[2],r);
}
/* Callee counters (hk_<hook>_<suffix>), incremented only while the hook is
 * non-original. */
static callee_counter group_fire_any_counters[]={{"call_4107e4",ES2_COLLIDE_CALL,0x4107e4,0}};
static callee_counter group_fire_sel_counters[]={{"call_4107e4",ES2_COLLIDE_CALL,0x4107e4,0}};
static callee_counter part_group_dead_counters[]={{"call_40cf10",ES2_COLLIDE_CALL,0x40cf10,0}};
static callee_counter actor_offsets_counters[]={{"call_49681a",ES2_COLLIDE_CALL,0x49681a,0}};
static callee_counter *part_helpers_hook_counters(unsigned hook,unsigned *count) {
    *count=1;
    if(hook==ES2_HOOK_INDEX_group_fire_any)return group_fire_any_counters;
    if(hook==ES2_HOOK_INDEX_group_fire_sel)return group_fire_sel_counters;
    if(hook==ES2_HOOK_INDEX_part_group_dead)return part_group_dead_counters;
    if(hook==ES2_HOOK_INDEX_actor_offsets)return actor_offsets_counters;
    *count=0;return NULL;
}
/* push ebp; mov ebp,esp; mov eax,[ebp+8]; mov edx,[ebp+10h]; push edx; push -1 */
static const BYTE part_helpers_group_fire_any_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x8b,0x55,0x10,0x52,0x6a,0xff};
/* push ebp; mov ebp,esp; mov eax,[ebp+8]; mov edx,[ebp+10h]; push edx; movsx ecx,byte [eax+1Dh] */
static const BYTE part_helpers_group_fire_sel_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x8b,0x55,0x10,0x52,0x0f,0xbe,0x48,0x1d};
/* push ebp; mov ebp,esp; push ebx; push esi; mov ebx,[ebp+0Ch] */
static const BYTE part_helpers_part_list_dead_sig[]={0x55,0x8b,0xec,0x53,0x56,0x8b,0x5d,0x0c};
/* push ebp; mov ebp,esp; push ebx; mov edx,[ebp+0Ch]; mov eax,[ebp+8] */
static const BYTE part_helpers_part_group_dead_sig[]={0x55,0x8b,0xec,0x53,0x8b,0x55,0x0c,0x8b,0x45,0x08};
/* push ebp; mov ebp,esp; push ecx; push ebx; push esi; push edi; mov ax,[ebp+0Ch] */
static const BYTE part_helpers_part_group_max_sig[]={0x55,0x8b,0xec,0x51,0x53,0x56,0x57,0x66,0x8b,0x45,0x0c};
/* cmp word [4a9e52h],0; sete al */
static const BYTE part_helpers_mission_guard_sig[]={0x66,0x83,0x3d,0x52,0x9e,0x4a,0x00,0x00,0x0f,0x94,0xc0};
/* push ebp; mov ebp,esp; push 6; push 0; mov eax,[ebp+0Ch] */
static const BYTE part_helpers_actor_offsets_sig[]={0x55,0x8b,0xec,0x6a,0x06,0x6a,0x00,0x8b,0x45,0x0c};
static const BYTE part_helpers_frame_lens[]={1,2,3},part_helpers_saved_lens[]={1,2,1,1},part_helpers_load_lens[]={1,2,1,3},
    part_helpers_compare_lens[]={8},part_helpers_push_lens[]={1,2,2};
#define PART_HELPERS_EXT(name,unsupported) \
    static const es2_hook_extension part_helpers_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,part_helpers_##name,unsupported}; \
    static uint32_t __cdecl part_helpers_##name##32(const uint32_t *a,void *u) {return (uint32_t)part_helpers_##name(a,u,weapons_no_registers);}
PART_HELPERS_EXT(group_fire_any,TRUE)
PART_HELPERS_EXT(group_fire_sel,TRUE)
PART_HELPERS_EXT(part_list_dead,FALSE)
PART_HELPERS_EXT(part_group_dead,FALSE)
PART_HELPERS_EXT(part_group_max,FALSE)
PART_HELPERS_EXT(mission_guard,FALSE)
PART_HELPERS_EXT(actor_offsets,TRUE)
#undef PART_HELPERS_EXT
static es2_hook_descriptor part_helpers_hook_descriptors[]={
    {ES2_HOOK_ID(group_fire_any),part_helpers_group_fire_any_sig,12,6,part_helpers_frame_lens,3,3,part_helpers_group_fire_any32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(group_fire_sel),part_helpers_group_fire_sel_sig,14,6,part_helpers_frame_lens,3,3,part_helpers_group_fire_sel32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(part_list_dead),part_helpers_part_list_dead_sig,8,5,part_helpers_saved_lens,4,2,part_helpers_part_list_dead32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(part_group_dead),part_helpers_part_group_dead_sig,10,7,part_helpers_load_lens,4,2,part_helpers_part_group_dead32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(part_group_max),part_helpers_part_group_max_sig,11,5,part_helpers_saved_lens,4,2,part_helpers_part_group_max32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(mission_guard),part_helpers_mission_guard_sig,11,8,part_helpers_compare_lens,1,0,part_helpers_mission_guard32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(actor_offsets),part_helpers_actor_offsets_sig,10,5,part_helpers_push_lens,3,3,part_helpers_actor_offsets32,NULL,NULL,NULL,0},
};
#endif
