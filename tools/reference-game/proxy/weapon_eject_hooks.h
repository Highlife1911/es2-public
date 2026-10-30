#ifndef ES2_WEAPON_EJECT_HOOKS_H
#define ES2_WEAPON_EJECT_HOOKS_H
/* Weapon eject 40f57c and the weapon damage slot +68h: the common
 * body 40ee0c and the class handlers 40ee38, 40ee90, 40ef6c
 * (docs/reverse-engineering/weapons.md, section "weapon eject").
 * Every game callee goes through the weapons guest bridge and callee
 * observer (weapons_graph) with the original's registers, so the nested
 * hooks keep their modes: euler_matrix, matrix_compose, mount_offset,
 * transform_point, aim_angles, pool_alloc, debris_init, debris_heading,
 * matrix_angles, multiply_q10, rng_next, the actor V24 hooks and the
 * group's own weapon_damage/weapon_eject; 4801d4, the node lookup virtual
 * and the runtime frame setup 49630c run as original code. weapon_eject's
 * frame lives in a scratch (synthetic EBP scratch + ES2_WEAPON_EJECT_FRAME,
 * 49630c through weapons_impact_guest_call); its adapter restores FS:0 to
 * the record's saved head, as the original's epilogue does. ECX after the
 * hooks is dead at every caller (weapons.md). Compare mode refused (game
 * calls, allocation). No entry shim, no game instruction embedded. */
#include "es2/core/weapon_eject.h"
static uint64_t __cdecl weapon_eject_weapon_eject(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[ES2_WEAPON_EJECT_SCRATCH]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_weapon_eject);
    weapons_impact_context c={(uint32_t)(uintptr_t)u,(uint32_t)(uintptr_t)(scratch+ES2_WEAPON_EJECT_FRAME)};
    g.call=weapons_impact_guest_call;g.context=&c;uint32_t head=0;
    uint64_t result=es2_weapon_eject_registers(&math_native_memory,&g,a[0],a[1],a[2],r,&head);
    __asm__ volatile("movl %0,%%fs:0"::"r"(head):"memory");return result;
}
#define WEAPON_EJECT_SLOT(name) \
static uint64_t __cdecl weapon_eject_##name(const uint32_t *a,void *u,const uint32_t *r) { \
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_##name); \
    return es2_##name##_registers(&math_native_memory,&g,a[0],a[1],a[2],a[3],r); }
WEAPON_EJECT_SLOT(weapon_damage)
WEAPON_EJECT_SLOT(beam_damage)
WEAPON_EJECT_SLOT(gun_damage)
WEAPON_EJECT_SLOT(targ_damage)
#undef WEAPON_EJECT_SLOT
/* Callee counters (hk_<hook>_<suffix>), incremented only while the hook
 * is non-original. Keys: the callee VA, or the vtable offset for slots. */
static callee_counter weapon_eject_eject_counters[]={
    {"call_49630c",ES2_COLLIDE_CALL,0x49630c,0},
    {"call_47ebb8",ES2_COLLIDE_CALL,0x47ebb8,0},
    {"slot_20",ES2_COLLIDE_SLOT,0x20,0},
    {"call_47fa20",ES2_COLLIDE_CALL,0x47fa20,0},
    {"call_40f540",ES2_COLLIDE_CALL,0x40f540,0},
    {"call_4801d4",ES2_COLLIDE_CALL,0x4801d4,0},
    {"slot_24",ES2_COLLIDE_SLOT,0x24,0},
    {"call_48043c",ES2_COLLIDE_CALL,0x48043c,0},
    {"call_492990",ES2_COLLIDE_CALL,0x492990,0},
    {"call_471b30",ES2_COLLIDE_CALL,0x471b30,0},
    {"call_408940",ES2_COLLIDE_CALL,0x408940,0},
    {"call_4089e4",ES2_COLLIDE_CALL,0x4089e4,0},
    {"call_47f9a0",ES2_COLLIDE_CALL,0x47f9a0,0},
};
static callee_counter weapon_eject_damage_counters[]={
    {"call_40f57c",ES2_COLLIDE_CALL,0x40f57c,0},
};
static callee_counter weapon_eject_beam_counters[]={
    {"call_40ee0c",ES2_COLLIDE_CALL,0x40ee0c,0},
    {"call_47e0b0",ES2_COLLIDE_CALL,0x47e0b0,0},
};
static callee_counter weapon_eject_gun_counters[]={
    {"call_40ee0c",ES2_COLLIDE_CALL,0x40ee0c,0},
    {"call_492ee0",ES2_COLLIDE_CALL,0x492ee0,0},
    {"call_40f57c",ES2_COLLIDE_CALL,0x40f57c,0},
};
static callee_counter weapon_eject_targ_counters[]={
    {"call_40ee0c",ES2_COLLIDE_CALL,0x40ee0c,0},
};
static callee_counter *weapon_eject_hook_counters(unsigned hook,unsigned *count) {
#define WEAPON_EJECT_COUNTERS(name,array) if(hook==ES2_HOOK_INDEX_##name){*count=sizeof(array)/sizeof(array[0]);return array;}
    WEAPON_EJECT_COUNTERS(weapon_eject,weapon_eject_eject_counters)
    WEAPON_EJECT_COUNTERS(weapon_damage,weapon_eject_damage_counters)
    WEAPON_EJECT_COUNTERS(beam_damage,weapon_eject_beam_counters)
    WEAPON_EJECT_COUNTERS(gun_damage,weapon_eject_gun_counters)
    WEAPON_EJECT_COUNTERS(targ_damage,weapon_eject_targ_counters)
#undef WEAPON_EJECT_COUNTERS
    *count=0;return NULL;
}
/* 40f57c: push ebp; mov ebp,esp; add esp,-84h; push ebx; push esi; push edi */
static const BYTE weapon_eject_eject_sig[]={0x55,0x8b,0xec,0x81,0xc4,0x7c,0xff,0xff,0xff,0x53,0x56,0x57};
static const BYTE weapon_eject_eject_lens[]={1,2,6};
/* 40ee0c: push ebp; mov ebp,esp; cmp dword [ebp+14h],100h; mov eax,[ebp+8] */
static const BYTE weapon_eject_damage_sig[]={0x55,0x8b,0xec,0x81,0x7d,0x14,0x00,0x01,0x00,0x00,0x8b,0x45,0x08};
static const BYTE weapon_eject_damage_lens[]={1,2,7};
/* 40ee38: push ebp; mov ebp,esp; push ebx; push esi; push edi; mov esi,[ebp+14h] */
static const BYTE weapon_eject_beam_sig[]={0x55,0x8b,0xec,0x53,0x56,0x57,0x8b,0x75,0x14};
static const BYTE weapon_eject_beam_lens[]={1,2,1,1,1};
/* 40ee90: push ebp; mov ebp,esp; push ecx; push ebx; push esi; push edi; mov esi,[ebp+14h] */
static const BYTE weapon_eject_gun_sig[]={0x55,0x8b,0xec,0x51,0x53,0x56,0x57,0x8b,0x75,0x14};
static const BYTE weapon_eject_gun_lens[]={1,2,1,1};
/* 40ef6c: push ebp; mov ebp,esp; push ebx; push esi; mov esi,[ebp+14h] */
static const BYTE weapon_eject_targ_sig[]={0x55,0x8b,0xec,0x53,0x56,0x8b,0x75,0x14};
static const BYTE weapon_eject_targ_lens[]={1,2,1,1};
#define WEAPON_EJECT_EXTENSION(name) \
static const es2_hook_extension weapon_eject_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0, \
    weapon_eject_##name,TRUE}; \
static uint32_t __cdecl weapon_eject_##name##32(const uint32_t *a,void *u) { \
    return (uint32_t)weapon_eject_##name(a,u,weapons_no_registers); }
WEAPON_EJECT_EXTENSION(weapon_eject)
WEAPON_EJECT_EXTENSION(weapon_damage)
WEAPON_EJECT_EXTENSION(beam_damage)
WEAPON_EJECT_EXTENSION(gun_damage)
WEAPON_EJECT_EXTENSION(targ_damage)
#undef WEAPON_EJECT_EXTENSION
static es2_hook_descriptor weapon_eject_hook_descriptors[]={
    {ES2_HOOK_ID(weapon_eject),weapon_eject_eject_sig,12,9,weapon_eject_eject_lens,3,3,weapon_eject_weapon_eject32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(weapon_damage),weapon_eject_damage_sig,13,10,weapon_eject_damage_lens,3,4,weapon_eject_weapon_damage32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(beam_damage),weapon_eject_beam_sig,9,6,weapon_eject_beam_lens,5,4,weapon_eject_beam_damage32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(gun_damage),weapon_eject_gun_sig,10,5,weapon_eject_gun_lens,4,4,weapon_eject_gun_damage32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(targ_damage),weapon_eject_targ_sig,8,5,weapon_eject_targ_lens,4,4,weapon_eject_targ_damage32,NULL,NULL,NULL,0},
};
#endif
