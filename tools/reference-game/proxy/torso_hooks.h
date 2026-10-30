#ifndef ES2_TORSO_HOOKS_H
#define ES2_TORSO_HOOKS_H
/* torso group (docs/reverse-engineering/input.md, section "torso servos
 * and weapon keys"):
 * torso_twist 41a550, torso_pitch 41a808, weapon_converge 41a74c and
 * weapon_keys 410f78. Included after projectile_hooks.h: every game callee
 * goes through the weapons guest bridge and callee observer (weapons_graph),
 * so all four refuse compare mode. The sound latches 41a6d0/41a994
 * (sim_leaves_hooks.h), the weapon mount helper 40f540 (mount_hooks.h) and
 * the weapon-system routines are hooks of their own. weapon_converge's 24h-byte frame is the adapter's scratch.
 * weapon_keys returns the caller's ESI high word in EAX; its entry shim
 * passes ESI in EAX (dead at 410f78's entry). Its calls load that ESI with
 * SI = 1, as the original holds it (input.md, "weapon_keys calls with the
 * original's ESI"). No game instruction is
 * embedded; the shim only moves a register. */
#include "es2/core/torso.h"
static uint64_t __cdecl torso_torso_twist(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_torso_twist);
    return es2_torso_twist_registers(&math_native_memory,&g,a[0],a[1],a[2],a[3],r);
}
static uint64_t __cdecl torso_torso_pitch(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_torso_pitch);
    return es2_torso_pitch_registers(&math_native_memory,&g,a[0],a[1],a[2],a[3],a[4],r);
}
static uint64_t __cdecl torso_weapon_converge(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE frame[ES2_WEAPON_CONVERGE_SCRATCH+4]={0};
    es2_weapon_graph g=weapons_graph(u,frame,ES2_HOOK_INDEX_weapon_converge);
    return es2_weapon_converge_registers(&math_native_memory,&g,a[0],a[1],r);
}
/* Every game call of 410f78 runs with ESI = the caller's ESI with
 * SI = 1 (MOV SI,1 at 410f83, before any call); 41074c reads its high word
 * (LEA EAX,[ESI-1] at 410775). The calls go through weapon_system's
 * ESI-loading bridge (weapon_system_hooks.h) with that value. */
static void weapon_system_guest_call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r);
/* r[0] is the caller's ESI (entry shim below; 0 on the legacy path). */
static uint64_t __cdecl torso_weapon_keys(const uint32_t *a,void *u,const uint32_t *r) {
    uint32_t esi=(r[0]&0xffff0000u)|1u;
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_weapon_keys);
    g.call=weapon_system_guest_call;g.context=&esi;
    return es2_weapon_keys_registers(&math_native_memory,&g,a[0],a[1],r[0],r);
}
__attribute__((used)) static void *torso_keys_bridge;
__attribute__((naked)) static void torso_keys_entry(void) {
    __asm__ volatile("movl %esi,%eax\n\tjmp *_torso_keys_bridge\n\t");
}
/* Called by projectile_capture_entry (end of the shim chain) after the
 * framework installed its jump: redirect it to the shim and keep h->patch
 * consistent, so uninstall still validates the exact patch. */
/* The weapon_system shims (weapon_system_hooks.h) end the chain. */
static BOOL weapon_system_capture_entry(es2_hook *h);
static BOOL torso_capture_entry(es2_hook *h) {
    if(h->descriptor->rva!=0x10f78u)return weapon_system_capture_entry(h);
    DWORD old,ignored;
    if(memcmp(h->site,h->patch,h->descriptor->relocate_size))return FALSE;
    if(!VirtualProtect(h->site,h->descriptor->relocate_size,PAGE_EXECUTE_READWRITE,&old))return FALSE;
    torso_keys_bridge=(void *)(uintptr_t)((uint32_t)(uintptr_t)(h->site+5)+math_native_read32(NULL,(uint32_t)(uintptr_t)(h->patch+1)));
    math_native_write32(NULL,(uint32_t)(uintptr_t)(h->patch+1),(uint32_t)(uintptr_t)torso_keys_entry-(uint32_t)(uintptr_t)(h->site+5));
    memcpy(h->site,h->patch,h->descriptor->relocate_size);
    BOOL flushed=FlushInstructionCache(GetCurrentProcess(),h->site,h->descriptor->relocate_size);
    BOOL protected=VirtualProtect(h->site,h->descriptor->relocate_size,old,&ignored);
    return flushed && protected;
}
static callee_counter torso_twist_counters[]={
    {"call_47e0a0",ES2_COLLIDE_CALL,0x47e0a0,0},
    {"call_467924",ES2_COLLIDE_CALL,0x467924,0},
    {"call_467adc",ES2_COLLIDE_CALL,0x467adc,0},
    {"call_479344",ES2_COLLIDE_CALL,0x479344,0},
    {"call_41a6d0",ES2_COLLIDE_CALL,0x41a6d0,0},
};
static callee_counter torso_pitch_counters[]={
    {"call_47e0a0",ES2_COLLIDE_CALL,0x47e0a0,0},
    {"call_467924",ES2_COLLIDE_CALL,0x467924,0},
    {"call_467adc",ES2_COLLIDE_CALL,0x467adc,0},
    {"call_479344",ES2_COLLIDE_CALL,0x479344,0},
    {"call_41a74c",ES2_COLLIDE_CALL,0x41a74c,0},
    {"call_41a994",ES2_COLLIDE_CALL,0x41a994,0},
};
static callee_counter weapon_converge_counters[]={
    {"call_40f540",ES2_COLLIDE_CALL,0x40f540,0},
    {"call_492990",ES2_COLLIDE_CALL,0x492990,0},
};
static callee_counter weapon_keys_counters[]={
    {"call_410d04",ES2_COLLIDE_CALL,0x410d04,0},
    {"call_410ae4",ES2_COLLIDE_CALL,0x410ae4,0},
    {"call_41074c",ES2_COLLIDE_CALL,0x41074c,0},
    {"call_4110ac",ES2_COLLIDE_CALL,0x4110ac,0},
    {"slot_38",ES2_COLLIDE_SLOT,0x38,0},
};
static callee_counter *torso_hook_counters(unsigned hook,unsigned *count) {
#define TORSO_COUNTERS(name) if(hook==ES2_HOOK_INDEX_##name) { *count=sizeof(name##_counters)/sizeof(name##_counters[0]);return name##_counters; }
    TORSO_COUNTERS(torso_twist)
    TORSO_COUNTERS(torso_pitch)
    TORSO_COUNTERS(weapon_converge)
    TORSO_COUNTERS(weapon_keys)
#undef TORSO_COUNTERS
    *count=0;return NULL;
}
static const BYTE torso_servo_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xf4,0x53,0x56,0x57};
static const BYTE torso_converge_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xdc,0x53,0x56,0x57};
static const BYTE torso_keys_sig[]={0x55,0x8b,0xec,0x53,0x56,0x8b,0x45,0x0c};
static const BYTE torso_frame_lens[]={1,2,3},torso_push_lens[]={1,2,1,1};
#define TORSO_EXT(name) \
    static const es2_hook_extension torso_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,torso_##name,TRUE}; \
    static uint32_t __cdecl torso_##name##32(const uint32_t *a,void *u) {return (uint32_t)torso_##name(a,u,weapons_no_registers);}
TORSO_EXT(torso_twist)
TORSO_EXT(torso_pitch)
TORSO_EXT(weapon_converge)
TORSO_EXT(weapon_keys)
#undef TORSO_EXT
static es2_hook_descriptor torso_hook_descriptors[]={
    {ES2_HOOK_ID(torso_twist),torso_servo_sig,9,6,torso_frame_lens,3,4,torso_torso_twist32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(torso_pitch),torso_servo_sig,9,6,torso_frame_lens,3,5,torso_torso_pitch32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(weapon_converge),torso_converge_sig,9,6,torso_frame_lens,3,2,torso_weapon_converge32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(weapon_keys),torso_keys_sig,8,5,torso_push_lens,4,2,torso_weapon_keys32,NULL,NULL,NULL,0},
};
#endif
