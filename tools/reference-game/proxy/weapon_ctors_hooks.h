#ifndef ES2_WEAPON_CTORS_HOOKS_H
#define ES2_WEAPON_CTORS_HOOKS_H
/* weapon_ctors group (docs/reverse-engineering/weapon_classes.md,
 * section "the weapon constructors"): the constructors that the
 * weapon factory weapon_factory 40fff8 (mission_weapons) calls: the WEAPON
 * base weapon_ctor 40df30, BEAM beam_ctor 40e074 (also ELF), GUN gun_ctor
 * 40e140 (also LAEW), the EQUIP base equip_ctor 40e234 and the five
 * equipment classes ecm_ctor 40e274, turbo_ctor 40e2bc, targ_ctor 40e308,
 * shield_ctor 40e344, energy_ctor 40e380. They run during the load only, so
 * they are live only with the early arming (the default).
 *
 * Every game call goes through the weapons guest bridge and callee observer
 * (weapons_graph) with the original's EAX/EDX/ECX, so the group's own hooks
 * call each other through the bridge and keep their modes, and so do
 * shape_word_block, object_track, mount_offset and multiply_q10; the weapon
 * shape 40fab0 (platform), the mesh offset 40dd4c (H3) and the CW3220
 * memset thunk 49681a stay guest calls. No frame record, no local.
 *
 * EDX:EAX exact; ECX is dead after every call site (weapon_classes.md), so
 * it is not returned (the framework returns the entry ECX). No function
 * reads a caller register other than EAX/EDX/ECX, an uninitialized local or
 * stack residue: no entry or exit shim. All nine refuse compare mode
 * (vtables, the construction count, game calls that allocate); the load is
 * accepted by the after-load snapshot. No game instruction is embedded. */
#include "es2/core/weapon_classes.h"
#define WEAPON_CTORS_FIVE(name) \
    static uint64_t __cdecl weapon_ctors_##name(const uint32_t *a,void *u,const uint32_t *r) { \
        es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_##name);uint32_t ecx; \
        return es2_##name##_registers(&math_native_memory,&g,a[0],a[1],a[2],a[3],a[4],r,&ecx); }
#define WEAPON_CTORS_FOUR(name) \
    static uint64_t __cdecl weapon_ctors_##name(const uint32_t *a,void *u,const uint32_t *r) { \
        es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_##name);uint32_t ecx; \
        return es2_##name##_registers(&math_native_memory,&g,a[0],a[1],a[2],a[3],r,&ecx); }
WEAPON_CTORS_FIVE(weapon_ctor)
WEAPON_CTORS_FIVE(beam_ctor)
WEAPON_CTORS_FIVE(gun_ctor)
WEAPON_CTORS_FOUR(equip_ctor)
WEAPON_CTORS_FOUR(ecm_ctor)
WEAPON_CTORS_FOUR(turbo_ctor)
WEAPON_CTORS_FOUR(targ_ctor)
WEAPON_CTORS_FOUR(shield_ctor)
WEAPON_CTORS_FOUR(energy_ctor)
#undef WEAPON_CTORS_FIVE
#undef WEAPON_CTORS_FOUR
/* Callee counters (hk_<hook>_<suffix>), incremented only while the hook is
 * non-original. Keys: the callee VA. */
static callee_counter weapon_ctor_counters[]={
    {"call_40fab0",ES2_COLLIDE_CALL,0x40fab0,0},
    {"call_402fc0",ES2_COLLIDE_CALL,0x402fc0,0},
    {"call_402374",ES2_COLLIDE_CALL,0x402374,0},
    {"call_40f540",ES2_COLLIDE_CALL,0x40f540,0},
    {"call_40dd4c",ES2_COLLIDE_CALL,0x40dd4c,0},
    {"call_49681a",ES2_COLLIDE_CALL,0x49681a,0},
};
static callee_counter beam_ctor_counters[]={
    {"call_40df30",ES2_COLLIDE_CALL,0x40df30,0},
    {"call_47e0b0",ES2_COLLIDE_CALL,0x47e0b0,0},
};
static callee_counter gun_ctor_counters[]={
    {"call_40df30",ES2_COLLIDE_CALL,0x40df30,0},
};
static callee_counter equip_ctor_counters[]={
    {"call_40df30",ES2_COLLIDE_CALL,0x40df30,0},
};
static callee_counter ecm_ctor_counters[]={
    {"call_40e234",ES2_COLLIDE_CALL,0x40e234,0},
};
static callee_counter turbo_ctor_counters[]={
    {"call_40e234",ES2_COLLIDE_CALL,0x40e234,0},
};
static callee_counter targ_ctor_counters[]={
    {"call_40e234",ES2_COLLIDE_CALL,0x40e234,0},
};
static callee_counter shield_ctor_counters[]={
    {"call_40e234",ES2_COLLIDE_CALL,0x40e234,0},
};
static callee_counter energy_ctor_counters[]={
    {"call_40e234",ES2_COLLIDE_CALL,0x40e234,0},
};
#define WEAPON_CTORS_COUNTERS(name) \
    if(hook==ES2_HOOK_INDEX_##name){*count=sizeof(name##_counters)/sizeof(name##_counters[0]);return name##_counters;}
static callee_counter *weapon_ctors_hook_counters(unsigned hook,unsigned *count) {
    WEAPON_CTORS_COUNTERS(weapon_ctor)
    WEAPON_CTORS_COUNTERS(beam_ctor)
    WEAPON_CTORS_COUNTERS(gun_ctor)
    WEAPON_CTORS_COUNTERS(equip_ctor)
    WEAPON_CTORS_COUNTERS(ecm_ctor)
    WEAPON_CTORS_COUNTERS(turbo_ctor)
    WEAPON_CTORS_COUNTERS(targ_ctor)
    WEAPON_CTORS_COUNTERS(shield_ctor)
    WEAPON_CTORS_COUNTERS(energy_ctor)
    *count=0;return NULL;
}
#undef WEAPON_CTORS_COUNTERS
/* 40df30: push ebp; mov ebp,esp; push ebx; push esi; push edi; mov
 * esi,[ebp+10h]; mov edi,[ebp+0Ch]. Relocated 5 bytes (1,2,1,1).
 * 40e074: push ebp; mov ebp,esp; push ebx; push esi; mov ebx,[ebp+8]; mov
 * eax,[ebp+18h]. Relocated 5 bytes (1,2,1,1).
 * 40e140: push ebp; mov ebp,esp; push ebx; mov ebx,[ebp+8]; mov
 * eax,[ebp+18h]; push eax. Relocated 7 bytes (1,2,1,3).
 * 40e234, 40e274, 40e2bc, 40e308, 40e344, 40e380: push ebp; mov ebp,esp;
 * push ebx; mov ebx,[ebp+8]; push 0; mov eax,[ebp+14h]. Relocated 7 bytes
 * (1,2,1,3). */
static const BYTE weapon_ctor_sig[]={0x55,0x8b,0xec,0x53,0x56,0x57,0x8b,0x75,0x10,0x8b,0x7d,0x0c};
static const BYTE beam_ctor_sig[]={0x55,0x8b,0xec,0x53,0x56,0x8b,0x5d,0x08,0x8b,0x45,0x18,0x50};
static const BYTE gun_ctor_sig[]={0x55,0x8b,0xec,0x53,0x8b,0x5d,0x08,0x8b,0x45,0x18,0x50};
static const BYTE equip_class_sig[]={0x55,0x8b,0xec,0x53,0x8b,0x5d,0x08,0x6a,0x00,0x8b,0x45,0x14};
static const BYTE weapon_ctors_push_lens[]={1,2,1,1},weapon_ctors_load_lens[]={1,2,1,3};
#define WEAPON_CTORS_EXT(name) \
    static const es2_hook_extension weapon_ctors_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,weapon_ctors_##name,TRUE}; \
    static uint32_t __cdecl weapon_ctors_##name##32(const uint32_t *a,void *u) {return (uint32_t)weapon_ctors_##name(a,u,weapons_no_registers);}
WEAPON_CTORS_EXT(weapon_ctor)
WEAPON_CTORS_EXT(beam_ctor)
WEAPON_CTORS_EXT(gun_ctor)
WEAPON_CTORS_EXT(equip_ctor)
WEAPON_CTORS_EXT(ecm_ctor)
WEAPON_CTORS_EXT(turbo_ctor)
WEAPON_CTORS_EXT(targ_ctor)
WEAPON_CTORS_EXT(shield_ctor)
WEAPON_CTORS_EXT(energy_ctor)
#undef WEAPON_CTORS_EXT
static es2_hook_descriptor weapon_ctors_hook_descriptors[]={
    {ES2_HOOK_ID(weapon_ctor),weapon_ctor_sig,12,5,weapon_ctors_push_lens,4,5,weapon_ctors_weapon_ctor32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(beam_ctor),beam_ctor_sig,12,5,weapon_ctors_push_lens,4,5,weapon_ctors_beam_ctor32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(gun_ctor),gun_ctor_sig,11,7,weapon_ctors_load_lens,4,5,weapon_ctors_gun_ctor32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(equip_ctor),equip_class_sig,12,7,weapon_ctors_load_lens,4,4,weapon_ctors_equip_ctor32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(ecm_ctor),equip_class_sig,12,7,weapon_ctors_load_lens,4,4,weapon_ctors_ecm_ctor32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(turbo_ctor),equip_class_sig,12,7,weapon_ctors_load_lens,4,4,weapon_ctors_turbo_ctor32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(targ_ctor),equip_class_sig,12,7,weapon_ctors_load_lens,4,4,weapon_ctors_targ_ctor32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(shield_ctor),equip_class_sig,12,7,weapon_ctors_load_lens,4,4,weapon_ctors_shield_ctor32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(energy_ctor),equip_class_sig,12,7,weapon_ctors_load_lens,4,4,weapon_ctors_energy_ctor32,NULL,NULL,NULL,0},
};
#endif
