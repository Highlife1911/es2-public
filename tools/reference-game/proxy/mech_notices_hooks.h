#ifndef ES2_MECH_NOTICES_HOOKS_H
#define ES2_MECH_NOTICES_HOOKS_H
/* mech_notices group (docs/reverse-engineering/ai.md, section
 * "MECH notices"): the MECH virtuals +48h mech_contact 412800,
 * +60h mech_kill 415710, +50h mech_attacked 41f7b8 and its helper
 * mech_alert 41f790 (also called directly by the sweep 426528). Core in
 * core/src/mech_notices.c. Every game callee (timer_word_step, the cockpit
 * message post 420a98, the ACTOR +50h default 411aec, actor_destination,
 * target_search, script_start, part_pick, attack_target, target_select,
 * attack_replan, order_resume, and mech_alert under mech_attacked) goes
 * through the weapons guest bridge and callee observer (weapons_graph) with
 * the original's EAX/EDX/ECX, so nested hooks keep their own modes and all
 * four refuse compare mode. No local is read before it is written, no
 * callee reaches the fire path, so no entry shim. ECX at return is outside
 * the contract (dead at every caller, ai.md). No game instruction is
 * embedded. */
#include "es2/core/mech_notices.h"
static uint64_t __cdecl mech_notices_mech_contact(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_mech_contact);
    return es2_mech_contact_registers(&math_native_memory,&g,a[0],a[1],r);
}
static uint64_t __cdecl mech_notices_mech_kill(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_mech_kill);
    return es2_mech_kill_registers(&math_native_memory,&g,a[0],a[1],a[2],r);
}
static uint64_t __cdecl mech_notices_mech_attacked(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_mech_attacked);
    return es2_mech_attacked_registers(&math_native_memory,&g,a[0],a[1],a[2],r);
}
static uint64_t __cdecl mech_notices_mech_alert(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_mech_alert);
    return es2_mech_alert_registers(&math_native_memory,&g,a[0],r);
}
/* Callee counters (hk_<hook>_<suffix>), incremented only while the hook is
 * non-original. */
static callee_counter mech_contact_counters[]={
    {"call_467a48",ES2_COLLIDE_CALL,0x467a48,0},
    {"call_420a98",ES2_COLLIDE_CALL,0x420a98,0},
};
static callee_counter mech_kill_counters[]={
    {"call_420a98",ES2_COLLIDE_CALL,0x420a98,0},
};
static callee_counter mech_attacked_counters[]={
    {"call_41f790",ES2_COLLIDE_CALL,0x41f790,0},
    {"call_411aec",ES2_COLLIDE_CALL,0x411aec,0},
    {"call_420a98",ES2_COLLIDE_CALL,0x420a98,0},
    {"call_41dbcc",ES2_COLLIDE_CALL,0x41dbcc,0},
    {"call_41e0e0",ES2_COLLIDE_CALL,0x41e0e0,0},
    {"call_413e50",ES2_COLLIDE_CALL,0x413e50,0},
    {"call_41ce08",ES2_COLLIDE_CALL,0x41ce08,0},
    {"call_41c0f4",ES2_COLLIDE_CALL,0x41c0f4,0},
    {"call_411fa0",ES2_COLLIDE_CALL,0x411fa0,0},
    {"call_41d5ec",ES2_COLLIDE_CALL,0x41d5ec,0},
    {"call_41eb34",ES2_COLLIDE_CALL,0x41eb34,0},
};
static callee_counter mech_alert_counters[]={
    {"call_420a98",ES2_COLLIDE_CALL,0x420a98,0},
};
static callee_counter *mech_notices_hook_counters(unsigned hook,unsigned *count) {
#define MECH_NOTICES_COUNTERS(name) if(hook==ES2_HOOK_INDEX_##name) { \
        *count=sizeof(name##_counters)/sizeof(name##_counters[0]);return name##_counters; }
    MECH_NOTICES_COUNTERS(mech_contact)
    MECH_NOTICES_COUNTERS(mech_kill)
    MECH_NOTICES_COUNTERS(mech_attacked)
    MECH_NOTICES_COUNTERS(mech_alert)
#undef MECH_NOTICES_COUNTERS
    *count=0;return NULL;
}
/* 412800: push ebp; mov ebp,esp; add esp,-10h; push ebx; push esi; mov esi,[ebp+0Ch]
 * 415710: push ebp; mov ebp,esp; push ebx; push esi; mov ebx,[ebp+0Ch]; mov esi,[ebp+8]
 * 41f7b8: push ebp; mov ebp,esp; add esp,-0Ch; mov eax,[4a9b84h]
 * 41f790: push ebp; mov ebp,esp; mov eax,[ebp+8]; cmp dword [eax+278h],0 */
static const BYTE mech_notices_contact_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xf0,0x53,0x56,0x8b,0x75,0x0c};
static const BYTE mech_notices_kill_sig[]={0x55,0x8b,0xec,0x53,0x56,0x8b,0x5d,0x0c,0x8b,0x75,0x08};
static const BYTE mech_notices_attacked_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xf4,0xa1,0x84,0x9b,0x4a,0x00};
static const BYTE mech_notices_alert_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x83,0xb8,0x78,0x02,0x00,0x00,0x00};
static const BYTE mech_notices_frame_lens[]={1,2,3},mech_notices_push_lens[]={1,2,1,1};
#define MECH_NOTICES_EXT(name) \
    static const es2_hook_extension mech_notices_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,mech_notices_##name,TRUE}; \
    static uint32_t __cdecl mech_notices_##name##32(const uint32_t *a,void *u) {return (uint32_t)mech_notices_##name(a,u,weapons_no_registers);}
MECH_NOTICES_EXT(mech_contact)
MECH_NOTICES_EXT(mech_kill)
MECH_NOTICES_EXT(mech_attacked)
MECH_NOTICES_EXT(mech_alert)
#undef MECH_NOTICES_EXT
static es2_hook_descriptor mech_notices_hook_descriptors[]={
    {ES2_HOOK_ID(mech_contact),mech_notices_contact_sig,11,6,mech_notices_frame_lens,3,2,mech_notices_mech_contact32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(mech_kill),mech_notices_kill_sig,11,5,mech_notices_push_lens,4,3,mech_notices_mech_kill32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(mech_attacked),mech_notices_attacked_sig,11,6,mech_notices_frame_lens,3,3,mech_notices_mech_attacked32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(mech_alert),mech_notices_alert_sig,13,6,mech_notices_frame_lens,3,1,mech_notices_mech_alert32,NULL,NULL,NULL,0},
};
#endif
