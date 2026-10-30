#ifndef ES2_SLOT_LEAVES_HOOKS_H
#define ES2_SLOT_LEAVES_HOOKS_H
/* slot_leaves group (docs/reverse-engineering/vtables.md, section
 * "Actor slot leaves"): the six actor-table methods
 * outside actor_leaves, one hook per function body, reached only
 * through their vtable slots. Five are empty or constant bodies that the
 * hook framework could not take before it accepted a final RET and 83 /1
 * (hook/README.md): actor_contact 4127f8 (+48), actor_part_position 411a3c
 * (+58), actor_part_facing 411a44 (+54), actor_kill 411b2c (+60) and
 * actor_link_position 411b34 (+64); the sixth, actor_radial_hit 411b3c
 * (+70, 38 instructions), is the default radial hit with three game calls.
 * Core in core/src/actor_leaves.c.
 *
 * EDX:EAX exact. The empty bodies leave all three registers as they are
 * (ECX is the framework's entry value); actor_part_facing sets EAX = -1.
 * No local, no shim for them. Compare: the five accept it (no state);
 * actor_radial_hit refuses it (its replacement calls the radius slot, the
 * distance gate and the part-damage slot, which change game state).
 *
 * actor_radial_hit goes through the weapons guest bridge with the original's
 * registers at each call, so the nested hooks (the radius leaves, dist_gate,
 * flyer_part) keep their modes. The original returns the ECX of its tail
 * callee, FLYER's slot +74h (flyer_part), which is 0 on every path, and
 * hands its entry ECX to the radius slot; an entry shim (a wrapper around
 * the framework's bridge) clears ECX after the return, so the entry ECX
 * reaches the replacement unchanged and the caller gets 0, as from the
 * original. No game instruction is
 * embedded. */
#include "es2/core/actor_leaves.h"
static uint64_t __cdecl slot_leaves_actor_contact(const uint32_t *a,void *u,const uint32_t *r) {
    (void)a;(void)u;uint32_t ecx;return es2_actor_empty_registers(r,&ecx);
}
static uint64_t __cdecl slot_leaves_actor_part_position(const uint32_t *a,void *u,const uint32_t *r) {
    (void)a;(void)u;uint32_t ecx;return es2_actor_empty_registers(r,&ecx);
}
static uint64_t __cdecl slot_leaves_actor_kill(const uint32_t *a,void *u,const uint32_t *r) {
    (void)a;(void)u;uint32_t ecx;return es2_actor_empty_registers(r,&ecx);
}
static uint64_t __cdecl slot_leaves_actor_link_position(const uint32_t *a,void *u,const uint32_t *r) {
    (void)a;(void)u;uint32_t ecx;return es2_actor_empty_registers(r,&ecx);
}
static uint64_t __cdecl slot_leaves_actor_part_facing(const uint32_t *a,void *u,const uint32_t *r) {
    (void)a;(void)u;uint32_t ecx;return es2_actor_no_part_registers(r,&ecx);
}
static uint64_t __cdecl slot_leaves_actor_radial_hit(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[16]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_actor_radial_hit);uint32_t ecx;
    return es2_actor_radial_hit_registers(&math_native_memory,&g,a[0],a[1],a[2],a[3],a[4],r,&ecx);
}
/* Callee counters (hk_actor_radial_hit_<suffix>), incremented only while the
 * hook is non-original. Keys: the callee VA, or the vtable offset for slots. */
static callee_counter actor_radial_hit_counters[]={
    {"slot_5c",ES2_COLLIDE_SLOT,0x5c,0},
    {"call_49288c",ES2_COLLIDE_CALL,0x49288c,0},
    {"slot_74",ES2_COLLIDE_SLOT,0x74,0},
};
static callee_counter *slot_leaves_hook_counters(unsigned hook,unsigned *count) {
    if(hook==ES2_HOOK_INDEX_actor_radial_hit) {
        *count=sizeof(actor_radial_hit_counters)/sizeof(actor_radial_hit_counters[0]);return actor_radial_hit_counters;
    }
    *count=0;return NULL;
}
/* Verified entry bytes (reference DBSIM.EXE), through the RET where short. */
/* 4127f8, 411a3c, 411b2c, 411b34: push ebp; mov ebp,esp; pop ebp; ret */
static const BYTE slot_leaves_empty_sig[]={0x55,0x8b,0xec,0x5d,0xc3};
/* 411a44: push ebp; mov ebp,esp; or eax,-1; pop ebp; ret */
static const BYTE slot_leaves_part_facing_sig[]={0x55,0x8b,0xec,0x83,0xc8,0xff,0x5d,0xc3};
/* 411b3c: push ebp; mov ebp,esp; push ebx; push esi; push edi; mov esi,[ebp+14h]; mov ebx,[ebp+8] */
static const BYTE slot_leaves_radial_hit_sig[]={0x55,0x8b,0xec,0x53,0x56,0x57,0x8b,0x75,0x14,0x8b,0x5d,0x08};
/* Relocated: push ebp; mov ebp,esp; pop ebp; ret (the whole body) / ... or
 * eax,-1; pop ebp (the RET stays behind) / ... push ebx; push esi. */
static const BYTE slot_leaves_empty_lens[]={1,2,1,1},slot_leaves_facing_lens[]={1,2,3,1},slot_leaves_saved_lens[]={1,2,1,1};
#define SLOT_LEAVES_EXT(name,unsupported) \
    static const es2_hook_extension slot_leaves_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,slot_leaves_##name,unsupported}; \
    static uint32_t __cdecl slot_leaves_##name##32(const uint32_t *a,void *u) {return (uint32_t)slot_leaves_##name(a,u,weapons_no_registers);}
SLOT_LEAVES_EXT(actor_contact,FALSE)
SLOT_LEAVES_EXT(actor_part_position,FALSE)
SLOT_LEAVES_EXT(actor_part_facing,FALSE)
SLOT_LEAVES_EXT(actor_kill,FALSE)
SLOT_LEAVES_EXT(actor_link_position,FALSE)
SLOT_LEAVES_EXT(actor_radial_hit,TRUE)
#undef SLOT_LEAVES_EXT
static es2_hook_descriptor slot_leaves_hook_descriptors[]={
    {ES2_HOOK_ID(actor_contact),slot_leaves_empty_sig,5,5,slot_leaves_empty_lens,4,1,slot_leaves_actor_contact32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(actor_part_position),slot_leaves_empty_sig,5,5,slot_leaves_empty_lens,4,1,slot_leaves_actor_part_position32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(actor_part_facing),slot_leaves_part_facing_sig,8,7,slot_leaves_facing_lens,4,1,slot_leaves_actor_part_facing32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(actor_kill),slot_leaves_empty_sig,5,5,slot_leaves_empty_lens,4,1,slot_leaves_actor_kill32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(actor_link_position),slot_leaves_empty_sig,5,5,slot_leaves_empty_lens,4,1,slot_leaves_actor_link_position32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(actor_radial_hit),slot_leaves_radial_hit_sig,12,5,slot_leaves_saved_lens,4,5,slot_leaves_actor_radial_hit32,NULL,NULL,NULL,0},
};
/* The shim of actor_radial_hit: the original passes its entry ECX on to the
 * radius slot and returns the ECX of its tail callee, FLYER's slot +74h
 * (flyer_part), which is 0 on every path. The shim therefore calls the
 * framework's bridge with the caller's five argument slots copied and the
 * entry registers untouched, and clears ECX after the return (the dispatch
 * restores the entry ECX in replaced mode). */
static int slot_leaves_radial_hit_entry(es2_machine *m,uint32_t bridge) {
    uint32_t args[5];
    for(unsigned i=0;i<5u;++i)args[i]=shim_r32(m->esp+4u+4u*i);
    es2_machine c=*m;
    (void)es2_machine_call(&c,bridge,ES2_MACHINE_CDECL,args,5);
    c.ecx=0;return shim_return(m,&c,m->esp,0);
}
ES2_SHIM(slot_leaves_radial_hit_shim,0x11b3cu,slot_leaves_radial_hit_entry,0);
#endif
