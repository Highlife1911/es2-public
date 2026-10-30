#ifndef ES2_WEAPON_SYSTEM_HOOKS_H
#define ES2_WEAPON_SYSTEM_HOOKS_H
/* Weapon-system routines below the weapon keys
 * (docs/reverse-engineering/weapons.md, section "weapon-system routines
 * below the weapon keys"): select_step 41074c,
 * fire_group 410ae4, weapons_v3c 410d04 and group_toggle 4110ac. Every
 * game callee (410708, 41074c, 410cd0, the HUD calls 4610ad/441f90 and the
 * weapon virtuals +3Ch/+48h) goes through the weapons guest bridge and
 * callee observer (weapons_graph), so all four refuse compare mode and
 * select_step stays nested in its own mode under fire_group. fire_group's
 * calls go through an own bridge that also loads the ESI the original
 * holds there (41074c reads its high word). Two entry shims: select_step
 * reads the caller's ESI, which its shim copies into EDX (dead at
 * 41074c's entry); group_toggle's return ECX escapes through MFD gadget
 * methods, so its shim loads the original's return ECX (es2_group_toggle_ecx,
 * reads only) before the framework's dispatch, which restores it on return. No game instruction is embedded; the
 * shim only marshals registers. */
#include "es2/core/weapon_system.h"
/* Like weapons_machine_call, with ESI loaded for the callee. */
static void __cdecl weapon_system_esi_call(uint32_t target,const uint32_t *args,unsigned count,
    es2_weapon_registers *registers,uint32_t esi) {
    const uint32_t frame[4]={0,esi,0,0};
    weapons_machine_call_at(target,args,count,registers,frame,0,NULL,0,NULL);
}
static void weapon_system_guest_call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    weapon_system_esi_call(f,a,n,r,*(const uint32_t *)c);
}
/* r[1] is the caller's ESI (entry shim below; 0 on the legacy path). */
static uint64_t __cdecl weapon_system_select_step(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_select_step);
    return es2_select_step_registers(&math_native_memory,&g,a[0],a[1],r[1],r);
}
static uint64_t __cdecl weapon_system_fire_group(const uint32_t *a,void *u,const uint32_t *r) {
    uint32_t esi=0;es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_fire_group);
    g.call=weapon_system_guest_call;g.context=&esi;
    return es2_fire_group_registers(&math_native_memory,&g,a[0],a[1],r,&esi);
}
static uint64_t __cdecl weapon_system_weapons_v3c(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_weapons_v3c);
    return es2_weapons_v3c_registers(&math_native_memory,&g,a[0],r);
}
static uint64_t __cdecl weapon_system_group_toggle(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_group_toggle);
    return es2_group_toggle_registers(&math_native_memory,&g,a[0],a[1],r);
}
/* 41074c: the caller's ESI in EDX. */
static int weapon_system_select_entry(es2_machine *m,uint32_t bridge) {(void)bridge;m->edx=m->esi;return 0;}
/* 4110ac: ECX = the weapon the search finds, or the n slot. The shim's
 * pushes (EAX, EDX, then n and W for the search) are left below the entry
 * ESP. */
static int weapon_system_toggle_entry(es2_machine *m,uint32_t bridge) {
    (void)bridge;const uint32_t w=shim_r32(m->esp+4u),n=shim_r32(m->esp+8u);
    shim_w32(m->esp-4u,m->eax);shim_w32(m->esp-8u,m->edx);shim_w32(m->esp-12u,n);shim_w32(m->esp-16u,w);
    m->ecx=es2_group_toggle_ecx(&math_native_memory,w,n);return 0;
}
ES2_SHIM(weapon_system_select_shim,0x1074cu,weapon_system_select_entry,0);
ES2_SHIM(weapon_system_toggle_shim,0x110acu,weapon_system_toggle_entry,0);
static callee_counter select_step_counters[]={
    {"call_410708",ES2_COLLIDE_CALL,0x410708,0},
};
static callee_counter fire_group_counters[]={
    {"call_41074c",ES2_COLLIDE_CALL,0x41074c,0},
    {"call_4610ad",ES2_COLLIDE_CALL,0x4610ad,0},
    {"call_441f90",ES2_COLLIDE_CALL,0x441f90,0},
};
static callee_counter weapons_v3c_counters[]={
    {"slot_3c",ES2_COLLIDE_SLOT,0x3c,0},
};
static callee_counter group_toggle_counters[]={
    {"call_410cd0",ES2_COLLIDE_CALL,0x410cd0,0},
    {"slot_48",ES2_COLLIDE_SLOT,0x48,0},
    {"slot_3c",ES2_COLLIDE_SLOT,0x3c,0},
};
static callee_counter *weapon_system_hook_counters(unsigned hook,unsigned *count) {
#define WEAPON_SYSTEM_COUNTERS(name) if(hook==ES2_HOOK_INDEX_##name) { *count=sizeof(name##_counters)/sizeof(name##_counters[0]);return name##_counters; }
    WEAPON_SYSTEM_COUNTERS(select_step)
    WEAPON_SYSTEM_COUNTERS(fire_group)
    WEAPON_SYSTEM_COUNTERS(weapons_v3c)
    WEAPON_SYSTEM_COUNTERS(group_toggle)
#undef WEAPON_SYSTEM_COUNTERS
    *count=0;return NULL;
}
static const BYTE weapon_system_select_sig[]={0x55,0x8b,0xec,0x51,0x53,0x56,0x57,0x8b,0x55,0x08};
static const BYTE weapon_system_fire_sig[]={0x55,0x8b,0xec,0x53,0x56,0x8b,0x5d,0x0c};
static const BYTE weapon_system_v3c_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xf8,0x8b,0x45,0x08};
static const BYTE weapon_system_toggle_sig[]={0x55,0x8b,0xec,0x53,0x56,0x8b,0x4d,0x0c};
static const BYTE weapon_system_push_lens[]={1,2,1,1},weapon_system_frame_lens[]={1,2,3};
#define WEAPON_SYSTEM_EXT(name) \
    static const es2_hook_extension weapon_system_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,weapon_system_##name,TRUE}; \
    static uint32_t __cdecl weapon_system_##name##32(const uint32_t *a,void *u) {return (uint32_t)weapon_system_##name(a,u,weapons_no_registers);}
WEAPON_SYSTEM_EXT(select_step)
WEAPON_SYSTEM_EXT(fire_group)
WEAPON_SYSTEM_EXT(weapons_v3c)
WEAPON_SYSTEM_EXT(group_toggle)
#undef WEAPON_SYSTEM_EXT
static es2_hook_descriptor weapon_system_hook_descriptors[]={
    {ES2_HOOK_ID(select_step),weapon_system_select_sig,10,5,weapon_system_push_lens,4,2,weapon_system_select_step32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(fire_group),weapon_system_fire_sig,8,5,weapon_system_push_lens,4,2,weapon_system_fire_group32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(weapons_v3c),weapon_system_v3c_sig,9,6,weapon_system_frame_lens,3,1,weapon_system_weapons_v3c32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(group_toggle),weapon_system_toggle_sig,8,5,weapon_system_push_lens,4,2,weapon_system_group_toggle32,NULL,NULL,NULL,0},
};
#endif
