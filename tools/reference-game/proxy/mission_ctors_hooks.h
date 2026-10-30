#ifndef ES2_MISSION_CTORS_HOOKS_H
#define ES2_MISSION_CTORS_HOOKS_H
/* mission_ctors group (docs/reverse-engineering/mission_load.md,
 * section "L5 constructors"): the constructors of the actor load
 * 4253d8, each called from one site only: mech_ctor 415bb0 (4256a4, once
 * per constructed mech), flyer_ctor 4215f4 (425a71, once per selected
 * flyer), building_factory 405314 (425c90, once per selected building),
 * group_ctor 423b34 (4261e5, once per script group) and base_marks 405db0
 * (426231, once per base group). They run during the load only, so they are
 * live only with the early arming (the default).
 *
 * Every game call goes through the weapons guest bridge and callee observer
 * (weapons_graph) with the original's EAX/EDX/ECX; mech_ctor and
 * building_factory set up the runtime record through 49630c with the
 * synthetic EBP (the frame in a zeroed scratch, the locals at the original's
 * offsets) through weapons_impact_guest_call, and their adapters write the
 * record's saved head back to FS:0 as the originals' epilogues do (mech_ctor:
 * ECX, building_factory: EDX). flyer_ctor's frame holds the vector that
 * rotate_point reads. The callees stay game code with their own modes: the
 * shape set-ups 4033a4/40332c/403368, the part tables 40d2cc/40d354/40d160/
 * 40cd88, the animation tracks 402374, the name builder 492bec, the group
 * join methods 417aa8/421ee8/405c3c, the terrain marks 47136c, the runtime
 * (new[] 4774f0, 49667c, memset 49681a) and the hooks pool_alloc,
 * object_init, script_start, euler_matrix, matrix_transpose, rotate_point,
 * track_rate, track_seek and pool_nth. The leaves 41ff00, 422e80, 411dd4 and
 * 413a90 are computed in the core.
 *
 * EDX:EAX exact; ECX is dead after every call site (mission_load.md), so it
 * is not returned (the framework returns the entry ECX). Two values of the
 * callers' callee-saved registers cannot be seen by an adapter: mech_ctor
 * pushes the type word with the high word of the caller's ESI (41ff00 reads
 * the low word only; POP ECX hands the dword to 4033a4 as its entry ECX,
 * which 4033a4 and 402278 overwrite before any read), and the adapter
 * passes ESI = the actor (the caller's ESI is the previously constructed
 * mech of the same pool block, or the actor load's entry ESI for the first);
 * building_factory's case 0 (types 10, 53, 54 and above 3Dh, which no
 * launch set and no shipped mission contains) builds no object and works on
 * the caller's EDI (the actor load's selection-flag pointer), and the
 * adapter passes EDI = 0, so a replaced case 0 faults at its first object
 * access. No entry or exit shim. All five refuse compare mode (allocation,
 * constructors, the construction count, game calls); the load is accepted
 * by the after-load snapshot. No game instruction is embedded. */
#include "es2/core/mission_load.h"
static uint64_t __cdecl mission_ctors_mech_ctor(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[ES2_MECH_CTOR_SCRATCH]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_mech_ctor);
    weapons_impact_context c={(uint32_t)(uintptr_t)u,(uint32_t)(uintptr_t)(scratch+ES2_MECH_CTOR_FRAME)};
    g.call=weapons_impact_guest_call;g.context=&c;
    uint32_t head=0;
    const uint64_t result=es2_mech_ctor_registers(&math_native_memory,&g,a[0],a[1],a[0],r,&head);
    __asm__ volatile("movl %0,%%fs:0"::"r"(head):"memory");return result;
}
static uint64_t __cdecl mission_ctors_flyer_ctor(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[ES2_FLYER_CTOR_SCRATCH]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_flyer_ctor);
    return es2_flyer_ctor_registers(&math_native_memory,&g,a[0],a[1],r,NULL);
}
static uint64_t __cdecl mission_ctors_building_factory(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[ES2_BUILDING_FACTORY_SCRATCH]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_building_factory);
    weapons_impact_context c={(uint32_t)(uintptr_t)u,(uint32_t)(uintptr_t)(scratch+ES2_BUILDING_FACTORY_FRAME)};
    g.call=weapons_impact_guest_call;g.context=&c;
    const uint64_t result=es2_building_factory_registers(&math_native_memory,&g,a[0],0,r,NULL);
    const uint32_t head=(uint32_t)(result>>32);__asm__ volatile("movl %0,%%fs:0"::"r"(head):"memory");return result;
}
static uint64_t __cdecl mission_ctors_group_ctor(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_group_ctor);
    return es2_group_ctor_registers(&math_native_memory,&g,a[0],a[1],a[2],a[3],a[4],a[5],r,NULL);
}
static uint64_t __cdecl mission_ctors_base_marks(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_base_marks);
    return es2_base_marks_registers(&math_native_memory,&g,a[0],r,NULL);
}
/* Callee counters (hk_<hook>_<suffix>), incremented only while the hook is
 * non-original. Keys: the callee VA. */
static callee_counter mech_ctor_counters[]={
    {"call_49630c",ES2_COLLIDE_CALL,0x49630c,0},
    {"call_4033a4",ES2_COLLIDE_CALL,0x4033a4,0},
    {"call_40d2cc",ES2_COLLIDE_CALL,0x40d2cc,0},
    {"call_49667c",ES2_COLLIDE_CALL,0x49667c,0},
    {"call_402374",ES2_COLLIDE_CALL,0x402374,0},
    {"call_4774f0",ES2_COLLIDE_CALL,0x4774f0,0},
    {"call_471b30",ES2_COLLIDE_CALL,0x471b30,0},
    {"call_402188",ES2_COLLIDE_CALL,0x402188,0},
    {"call_413e50",ES2_COLLIDE_CALL,0x413e50,0},
    {"call_492bec",ES2_COLLIDE_CALL,0x492bec,0},
    {"call_40d160",ES2_COLLIDE_CALL,0x40d160,0},
    {"call_40cd88",ES2_COLLIDE_CALL,0x40cd88,0},
    {"call_40d354",ES2_COLLIDE_CALL,0x40d354,0},
    {"call_49681a",ES2_COLLIDE_CALL,0x49681a,0},
    {"call_47ebb8",ES2_COLLIDE_CALL,0x47ebb8,0},
    {"call_47df19",ES2_COLLIDE_CALL,0x47df19,0},
    {"call_480304",ES2_COLLIDE_CALL,0x480304,0},
};
static callee_counter flyer_ctor_counters[]={
    {"call_403368",ES2_COLLIDE_CALL,0x403368,0},
    {"call_40d2cc",ES2_COLLIDE_CALL,0x40d2cc,0},
    {"call_40d354",ES2_COLLIDE_CALL,0x40d354,0},
    {"call_413e50",ES2_COLLIDE_CALL,0x413e50,0},
    {"call_49681a",ES2_COLLIDE_CALL,0x49681a,0},
    {"call_47ebb8",ES2_COLLIDE_CALL,0x47ebb8,0},
    {"call_47df19",ES2_COLLIDE_CALL,0x47df19,0},
    {"call_480304",ES2_COLLIDE_CALL,0x480304,0},
};
static callee_counter building_factory_counters[]={
    {"call_49630c",ES2_COLLIDE_CALL,0x49630c,0},
    {"call_471b30",ES2_COLLIDE_CALL,0x471b30,0},
    {"call_40332c",ES2_COLLIDE_CALL,0x40332c,0},
    {"call_403368",ES2_COLLIDE_CALL,0x403368,0},
    {"call_49667c",ES2_COLLIDE_CALL,0x49667c,0},
    {"call_402374",ES2_COLLIDE_CALL,0x402374,0},
    {"call_4796d8",ES2_COLLIDE_CALL,0x4796d8,0},
    {"call_479344",ES2_COLLIDE_CALL,0x479344,0},
    {"call_4774f0",ES2_COLLIDE_CALL,0x4774f0,0},
};
static callee_counter group_ctor_counters[]={
    {"call_49681a",ES2_COLLIDE_CALL,0x49681a,0},
    {"call_4774f0",ES2_COLLIDE_CALL,0x4774f0,0},
    {"call_471cb0",ES2_COLLIDE_CALL,0x471cb0,0},
    {"call_417aa8",ES2_COLLIDE_CALL,0x417aa8,0},
    {"call_421ee8",ES2_COLLIDE_CALL,0x421ee8,0},
    {"call_405c3c",ES2_COLLIDE_CALL,0x405c3c,0},
};
static callee_counter base_marks_counters[]={
    {"call_47136c",ES2_COLLIDE_CALL,0x47136c,0},
};
#define MISSION_CTORS_COUNTERS(name) \
    if(hook==ES2_HOOK_INDEX_##name){*count=sizeof(name##_counters)/sizeof(name##_counters[0]);return name##_counters;}
static callee_counter *mission_ctors_hook_counters(unsigned hook,unsigned *count) {
    MISSION_CTORS_COUNTERS(mech_ctor)
    MISSION_CTORS_COUNTERS(flyer_ctor)
    MISSION_CTORS_COUNTERS(building_factory)
    MISSION_CTORS_COUNTERS(group_ctor)
    MISSION_CTORS_COUNTERS(base_marks)
    *count=0;return NULL;
}
#undef MISSION_CTORS_COUNTERS
/* 415bb0: push ebp; mov ebp,esp; add esp,-58h; mov eax,49A080h; push ebx.
 * Relocated 6 bytes (1,2,3); the descriptor load and the call of 49630c
 * that follow stay in place.
 * 4215f4: push ebp; mov ebp,esp; add esp,-0Ch; push ebx; push esi; push
 * edi; mov esi,[ebp+0Ch]. Relocated 6 bytes (1,2,3).
 * 405314: push ebp; mov ebp,esp; add esp,-0A4h; mov eax,497556h; push ebx.
 * Relocated 9 bytes (1,2,6).
 * 423b34: push ebp; mov ebp,esp; add esp,-10h; push ebx; push esi; push
 * edi; mov eax,[ebp+0Ch]. Relocated 6 bytes (1,2,3).
 * 405db0: push ebp; mov ebp,esp; push ebx; mov edx,[ebp+8]; mov eax,[edx+45h].
 * Relocated 7 bytes (1,2,1,3). */
static const BYTE mech_ctor_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xa8,0xb8,0x80,0xa0,0x49,0x00,0x53};
static const BYTE flyer_ctor_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xf4,0x53,0x56,0x57,0x8b,0x75,0x0c};
static const BYTE building_factory_sig[]={0x55,0x8b,0xec,0x81,0xc4,0x5c,0xff,0xff,0xff,0xb8,0x56,0x75,0x49,0x00,0x53};
static const BYTE group_ctor_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xf0,0x53,0x56,0x57,0x8b,0x45,0x0c};
static const BYTE base_marks_sig[]={0x55,0x8b,0xec,0x53,0x8b,0x55,0x08,0x8b,0x42,0x45};
static const BYTE mech_ctor_lens[]={1,2,3},flyer_ctor_lens[]={1,2,3},building_factory_lens[]={1,2,6},
    group_ctor_lens[]={1,2,3},base_marks_lens[]={1,2,1,3};
#define MISSION_CTORS_EXT(name) \
    static const es2_hook_extension mission_ctors_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,mission_ctors_##name,TRUE}; \
    static uint32_t __cdecl mission_ctors_##name##32(const uint32_t *a,void *u) {return (uint32_t)mission_ctors_##name(a,u,weapons_no_registers);}
MISSION_CTORS_EXT(mech_ctor)
MISSION_CTORS_EXT(flyer_ctor)
MISSION_CTORS_EXT(building_factory)
MISSION_CTORS_EXT(group_ctor)
MISSION_CTORS_EXT(base_marks)
#undef MISSION_CTORS_EXT
static es2_hook_descriptor mission_ctors_hook_descriptors[]={
    {ES2_HOOK_ID(mech_ctor),mech_ctor_sig,12,6,mech_ctor_lens,3,2,mission_ctors_mech_ctor32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(flyer_ctor),flyer_ctor_sig,12,6,flyer_ctor_lens,3,2,mission_ctors_flyer_ctor32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(building_factory),building_factory_sig,15,9,building_factory_lens,3,2,mission_ctors_building_factory32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(group_ctor),group_ctor_sig,12,6,group_ctor_lens,3,6,mission_ctors_group_ctor32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(base_marks),base_marks_sig,10,7,base_marks_lens,4,1,mission_ctors_base_marks32,NULL,NULL,NULL,0},
};
#endif
