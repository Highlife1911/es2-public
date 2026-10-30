#ifndef ES2_ACTOR_PASS_HOOKS_H
#define ES2_ACTOR_PASS_HOOKS_H
/* Actor-list pass (docs/reverse-engineering/vehicle.md, section
 * "Actor-list pass"): actor_pass 4123ac, contact_expire 41251c,
 * contact_scan 4128f8, contact_share 412704. Included after
 * weapons_hooks.h: every game callee goes through the weapons guest bridge
 * and callee observer (weapons_graph) with the original's entry registers,
 * so nested hooks (timer_word_step, rng_bounded, dist2d_point, dist_gate,
 * heading_point, sight_check, trigger_fire and the group's own helpers)
 * keep their modes, and so do the targets of the actors' virtuals
 * +3Ch/+44h/+48h (counted as slot_<off>).
 * actor_pass, contact_expire and contact_share refuse compare mode (they
 * write actors and call game code); contact_scan accepts it with snapshot
 * regions and guards (below). No entry shim: no entry register or uninitialized local reaches a result
 * except actor_pass's EDX high word (uninitialized stack in the original,
 * dead at its only caller 45f881, returned as 0). No game instruction is
 * embedded. */
#include "es2/core/actor_pass.h"
static uint64_t __cdecl actor_pass_actor_pass(const uint32_t *a,void *u,const uint32_t *r) {
    (void)a;es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_actor_pass);
    return es2_actor_pass_registers(&math_native_memory,&g,r);
}
static uint64_t __cdecl actor_pass_contact_expire(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_contact_expire);
    return es2_contact_expire_registers(&math_native_memory,&g,a[0],r);
}
static uint64_t __cdecl actor_pass_contact_scan(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_contact_scan);
    return es2_contact_scan_registers(&math_native_memory,&g,a[0],r);
}
static uint64_t __cdecl actor_pass_contact_share(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_contact_share);
    return es2_contact_share_registers(&math_native_memory,&g,a[0],a[1],r);
}
/* Callee counters (hk_<hook>_<suffix>), keyed by VA or virtual slot. */
static callee_counter actor_pass_counters[]={
    {"call_467a48",ES2_COLLIDE_CALL,0x467a48,0},
    {"call_492f24",ES2_COLLIDE_CALL,0x492f24,0},
    {"call_41251c",ES2_COLLIDE_CALL,0x41251c,0},
    {"call_4128f8",ES2_COLLIDE_CALL,0x4128f8,0},
};
static callee_counter contact_expire_counters[]={
    {"call_4928d0",ES2_COLLIDE_CALL,0x4928d0,0},
    {"call_412608",ES2_COLLIDE_CALL,0x412608,0},
};
static callee_counter contact_scan_counters[]={
    {"call_49288c",ES2_COLLIDE_CALL,0x49288c,0},
    {"call_412608",ES2_COLLIDE_CALL,0x412608,0},
    {"slot_48",ES2_COLLIDE_SLOT,0x48,0},
    {"call_423430",ES2_COLLIDE_CALL,0x423430,0},
    {"call_492934",ES2_COLLIDE_CALL,0x492934,0},
    {"slot_3c",ES2_COLLIDE_SLOT,0x3c,0},
    {"slot_44",ES2_COLLIDE_SLOT,0x44,0},
    {"call_412704",ES2_COLLIDE_CALL,0x412704,0},
};
static callee_counter contact_share_counters[]={
    {"call_49288c",ES2_COLLIDE_CALL,0x49288c,0},
    {"call_423430",ES2_COLLIDE_CALL,0x423430,0},
};
static callee_counter *actor_pass_hook_counters(unsigned hook,unsigned *count) {
#define ACTOR_PASS_COUNTERS(name) if(hook==ES2_HOOK_INDEX_##name) { *count=sizeof(name##_counters)/sizeof(name##_counters[0]);return name##_counters; }
    ACTOR_PASS_COUNTERS(actor_pass)
    ACTOR_PASS_COUNTERS(contact_expire)
    ACTOR_PASS_COUNTERS(contact_scan)
    ACTOR_PASS_COUNTERS(contact_share)
#undef ACTOR_PASS_COUNTERS
    *count=0;return NULL;
}
static const BYTE actor_pass_actor_pass_sig[]={0x53,0x56,0x57,0x55,0x83,0xc4,0xf8,0x8b,0xec};
static const BYTE actor_pass_contact_expire_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xec,0x53,0x56,0x57};
static const BYTE actor_pass_contact_scan_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xdc,0xa1,0xf8,0x9a,0x4a,0x00};
static const BYTE actor_pass_contact_share_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xec,0x53,0x56,0x57};
static const BYTE actor_pass_push_lens[]={1,1,1,1,3},actor_pass_frame_lens[]={1,2,3};
/* Snapshot compare (docs/reference-game/compare_mode.md, section
 * "snapshot regions"): contact_scan accepts compare. Its writes outside the frame
 * are actor bytes (its own, contact_share's and sight_check's: +95h, +9Eh,
 * the contact tables +C2h, +132h, the timer +1E3h), all inside the blocks
 * of the three actor pools (buildings [4a95a0], MECHs [4a9b7a], FLYERs
 * [4a9db9]; header +0 free and +2 used count, +4 object size, +6 block of
 * (free + used) * (size + 8) bytes, 471a50/471ad8), and RNG stream A
 * 4d2599h (rng_bounded in sight_check). Snapshot regions: the three blocks
 * (absent when the header pointer or the block is 0) and the stream. The
 * other stores of the reach overwrite scratch with values independent of
 * its pre-state (terrain contact 6b505c..67h and byte 4a0c4c in 470798, the
 * BASE V24 buffer 4a95cc and its first-call byte 497424), so a correct
 * replacement phase leaves them as the original did. mech_contact (V48 of
 * the MECH, 412800: radio message 420a98, the message timer) and
 * trigger_fire (423430: trigger latch, variables, message port, clock) are
 * guards: a call whose original phase enters one is not compared. */
static BOOL actor_pass_readable(uint32_t address,size_t size) {
    MEMORY_BASIC_INFORMATION region;
    if(!address||!VirtualQuery((const void *)(uintptr_t)address,&region,sizeof(region))||region.State!=MEM_COMMIT||
       (region.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return FALSE;
    return address+size>=address&&address+size<=(uint32_t)(uintptr_t)region.BaseAddress+region.RegionSize;
}
static BOOL __cdecl actor_pass_pool_block(void *user,BYTE *image,const uint32_t *args,BYTE **base,size_t *size) {
    (void)args;uint32_t header,block;uint16_t free_count,used,object;
    memcpy(&header,image+(uint32_t)(uintptr_t)user,4);
    if(!header)return TRUE;
    if(!actor_pass_readable(header,10))return FALSE;
    const BYTE *h=(const BYTE *)(uintptr_t)header;
    memcpy(&free_count,h,2);memcpy(&used,h+2,2);memcpy(&object,h+4,2);memcpy(&block,h+6,4);
    *base=(BYTE *)(uintptr_t)block;*size=block?((size_t)free_count+used)*((size_t)object+8u):0;
    return TRUE;
}
static const es2_hook_snapshot actor_pass_contact_scan_regions[]={
    {0,0x40000,actor_pass_pool_block,(void *)(uintptr_t)0xa95a0u},
    {0,0x40000,actor_pass_pool_block,(void *)(uintptr_t)0xa9b7au},
    {0,0x40000,actor_pass_pool_block,(void *)(uintptr_t)0xa9db9u},
    {0xd2599,0x72,NULL,NULL},
};
static const uint32_t actor_pass_contact_scan_guards[]={0x12800,0x23430};
static const es2_hook_snapshot_set actor_pass_contact_scan_snapshot={actor_pass_contact_scan_regions,4,actor_pass_contact_scan_guards,2,1,1};
#define ACTOR_PASS_EXT(name) static const es2_hook_extension actor_pass_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,actor_pass_##name,TRUE};
#define ACTOR_PASS_LEGACY(name) static uint32_t __cdecl actor_pass_##name##32(const uint32_t *a,void *u) {return (uint32_t)actor_pass_##name(a,u,weapons_no_registers);}
#define ACTOR_PASS_BOTH(name) ACTOR_PASS_EXT(name) ACTOR_PASS_LEGACY(name)
ACTOR_PASS_BOTH(actor_pass) ACTOR_PASS_BOTH(contact_expire) ACTOR_PASS_BOTH(contact_share)
static const es2_hook_extension actor_pass_contact_scan_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,actor_pass_contact_scan,FALSE};
ACTOR_PASS_LEGACY(contact_scan)
/* Bound at load, before any installation (hook.h: es2_hook_bind_snapshot). */
__attribute__((constructor)) static void actor_pass_bind_snapshot(void) {
    (void)es2_hook_bind_snapshot(&actor_pass_contact_scan_extension,&actor_pass_contact_scan_snapshot);
}
#undef ACTOR_PASS_EXT
#undef ACTOR_PASS_LEGACY
#undef ACTOR_PASS_BOTH
static es2_hook_descriptor actor_pass_hook_descriptors[]={
    {ES2_HOOK_ID(actor_pass),actor_pass_actor_pass_sig,9,7,actor_pass_push_lens,5,0,actor_pass_actor_pass32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(contact_expire),actor_pass_contact_expire_sig,9,6,actor_pass_frame_lens,3,1,actor_pass_contact_expire32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(contact_scan),actor_pass_contact_scan_sig,11,6,actor_pass_frame_lens,3,1,actor_pass_contact_scan32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(contact_share),actor_pass_contact_share_sig,9,6,actor_pass_frame_lens,3,2,actor_pass_contact_share32,NULL,NULL,NULL,0},
};
#endif
