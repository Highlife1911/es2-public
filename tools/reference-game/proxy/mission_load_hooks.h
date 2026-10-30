#ifndef ES2_MISSION_LOAD_HOOKS_H
#define ES2_MISSION_LOAD_HOOKS_H
/* mission_load group (docs/reverse-engineering/mission_load.md,
 * section "pieces L1 to L3 and the snapshot noise model"): the
 * mission load's pool construction pool_create 471ad8 (with its
 * initializer 471a50), the pool walk pool_nth 471cb0, the load leaves
 * subject_resolve 425348, action_copy 411b90, condition_init 415068,
 * region_init 423358, timer_init 423104 and group_array 423d38, and the
 * initial damage initial_damage 4178e8. All run during the load only, so
 * they are live only with the early arming (the default). Every game call goes through the weapons guest bridge and
 * callee observer (weapons_graph) with the original's entry EAX/EDX/ECX:
 * new[] 4774f0, memset 49681a and the array construction 49667c
 * (runtime), the element constructor 4240e4 (passed as an argument), pool_prev
 * (a hook), pool_nth under subject_resolve (in-group), timer_arm,
 * rng_next, multiply_q8, random_damage (hooks) and the actor's virtual +74h
 * (vehicle_part for a MECH) keep their own modes. EDX:EAX exact; ECX is
 * dead after every call site (mission_load.md lists the sites), so it is
 * not reproduced (the framework returns the entry ECX). No entry or exit
 * shim: no function reads an uninitialized local or stack residue; the
 * argument words that pool_nth and condition_init advance in place are
 * released by every caller (ADD ESP). Compare mode (compare_mode.md,
 * mission_load.md "compare mode for six L2 leaves"): pool_nth and subject_resolve accept it without a
 * declaration (readers; their callees pool_prev/pool_next and pool_nth
 * store nothing outside the frames on the live heap-node path);
 * action_copy (span A+1BAh..1E1h), region_init (R+0..9) and timer_init
 * (T+0..30h, timer_arm's dword T+2Dh included; timer_arm acts on the
 * clone) declare a span and compare EAX or EDX relative to it;
 * condition_init declares the two tables behind [C+4] (29 words) and [C]
 * (22 words) as one-hop regions. pool_create and group_array (allocation)
 * and initial_damage (RNG stream A, the damage virtual and random_damage)
 * refuse it. No game instruction is embedded. */
#include "es2/core/mission_load.h"
#define ML_GRAPH(name) es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_##name)
static uint64_t __cdecl mission_load_pool_create(const uint32_t *a,void *u,const uint32_t *r) {
    ML_GRAPH(pool_create);return es2_pool_create_registers(&math_native_memory,&g,a[0],a[1],a[2],r,NULL);
}
static uint64_t __cdecl mission_load_pool_nth(const uint32_t *a,void *u,const uint32_t *r) {
    ML_GRAPH(pool_nth);return es2_pool_nth_registers(&math_native_memory,&g,a[0],a[1],r,NULL);
}
static uint64_t __cdecl mission_load_subject_resolve(const uint32_t *a,void *u,const uint32_t *r) {
    ML_GRAPH(subject_resolve);return es2_subject_resolve_registers(&math_native_memory,&g,a[0],a[1],r,NULL);
}
static uint64_t __cdecl mission_load_action_copy(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;(void)r;return es2_action_copy_registers(&math_native_memory,a[0],a[1],a[2],NULL);
}
static uint64_t __cdecl mission_load_condition_init(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;(void)r;return es2_condition_init_registers(&math_native_memory,a[0],a[1],a[2],a[3],NULL);
}
static uint64_t __cdecl mission_load_region_init(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;(void)r;return es2_region_init_registers(&math_native_memory,a[0],a[1],a[2],NULL);
}
static uint64_t __cdecl mission_load_timer_init(const uint32_t *a,void *u,const uint32_t *r) {
    ML_GRAPH(timer_init);return es2_timer_init_registers(&math_native_memory,&g,a[0],a[1],a[2],a[3],r,NULL);
}
static uint64_t __cdecl mission_load_group_array(const uint32_t *a,void *u,const uint32_t *r) {
    ML_GRAPH(group_array);return es2_group_array_registers(&math_native_memory,&g,a[0],a[1],r,NULL);
}
static uint64_t __cdecl mission_load_initial_damage(const uint32_t *a,void *u,const uint32_t *r) {
    ML_GRAPH(initial_damage);return es2_initial_damage_registers(&math_native_memory,&g,a[0],a[1],r,NULL);
}
#undef ML_GRAPH
/* Callee counters (hk_<hook>_<suffix>), incremented only while the hook is
 * non-original. action_copy, condition_init and region_init make no game
 * call. */
static callee_counter pool_create_counters[]={
    {"call_4774f0",ES2_COLLIDE_CALL,0x4774f0,0},
    {"call_49681a",ES2_COLLIDE_CALL,0x49681a,0},
};
static callee_counter pool_nth_counters[]={
    {"call_471c70",ES2_COLLIDE_CALL,0x471c70,0},
};
static callee_counter subject_resolve_counters[]={
    {"call_471cb0",ES2_COLLIDE_CALL,0x471cb0,0},
};
static callee_counter timer_init_counters[]={
    {"call_467ac4",ES2_COLLIDE_CALL,0x467ac4,0},
};
static callee_counter group_array_counters[]={
    {"call_49667c",ES2_COLLIDE_CALL,0x49667c,0},
};
static callee_counter initial_damage_counters[]={
    {"call_47e0a0",ES2_COLLIDE_CALL,0x47e0a0,0},
    {"call_492ee0",ES2_COLLIDE_CALL,0x492ee0,0},
    {"slot_74",ES2_COLLIDE_SLOT,0x74,0},
    {"call_417a04",ES2_COLLIDE_CALL,0x417a04,0},
};
#define MISSION_LOAD_COUNTERS(name) \
    if(hook==ES2_HOOK_INDEX_##name){*count=sizeof(name##_counters)/sizeof(name##_counters[0]);return name##_counters;}
static callee_counter *mission_load_hook_counters(unsigned hook,unsigned *count) {
    MISSION_LOAD_COUNTERS(pool_create)
    MISSION_LOAD_COUNTERS(pool_nth)
    MISSION_LOAD_COUNTERS(subject_resolve)
    MISSION_LOAD_COUNTERS(timer_init)
    MISSION_LOAD_COUNTERS(group_array)
    MISSION_LOAD_COUNTERS(initial_damage)
    *count=0;return NULL;
}
#undef MISSION_LOAD_COUNTERS
/* 471ad8: push ebp; mov ebp,esp; push ecx; push ebx; push esi; push edi;
 * mov esi,[ebp+0Ch]; mov ebx,[ebp+8]. Relocated 5 bytes (1,2,1,1). */
static const BYTE pool_create_sig[]={0x55,0x8b,0xec,0x51,0x53,0x56,0x57,0x8b,0x75,0x0c,0x8b,0x5d,0x08};
/* 471cb0: push ebp; mov ebp,esp; push ebx; xor eax,eax; mov ebx,[ebp+8];
 * push eax; push ebx. Relocated 6 bytes (1,2,1,2). */
static const BYTE pool_nth_sig[]={0x55,0x8b,0xec,0x53,0x33,0xc0,0x8b,0x5d,0x08,0x50,0x53};
static const BYTE pool_nth_lens[]={1,2,1,2};
/* 425348: push ebp; mov ebp,esp; push ebx; push esi; mov ebx,[ebp+8];
 * mov eax,[ebp+0Ch]; sub eax,1. Relocated 5 bytes (1,2,1,1). */
static const BYTE subject_resolve_sig[]={0x55,0x8b,0xec,0x53,0x56,0x8b,0x5d,0x08,0x8b,0x45,0x0c,0x83,0xe8,0x01};
/* 411b90: push ebp; mov ebp,esp; push ebx; push esi; mov eax,[ebp+8];
 * xor ebx,ebx. Relocated 5 bytes (1,2,1,1). */
static const BYTE action_copy_sig[]={0x55,0x8b,0xec,0x53,0x56,0x8b,0x45,0x08,0x33,0xdb};
/* 415068: push ebp; mov ebp,esp; add esp,-8; push ebx; push esi; push edi;
 * mov eax,[ebp+8]. Relocated 6 bytes (1,2,3). */
static const BYTE condition_init_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xf8,0x53,0x56,0x57,0x8b,0x45,0x08};
/* 423358: push ebp; mov ebp,esp; push ebx; mov ebx,[ebp+10h];
 * mov edx,[ebp+0Ch]; mov eax,[ebp+8]. Relocated 7 bytes (1,2,1,3). */
static const BYTE region_init_sig[]={0x55,0x8b,0xec,0x53,0x8b,0x5d,0x10,0x8b,0x55,0x0c,0x8b,0x45,0x08};
/* 423104: push ebp; mov ebp,esp; push ebx; mov ebx,[ebp+8];
 * mov eax,[ebp+0Ch]; mov [ebx],eax. Relocated 7 bytes (1,2,1,3). */
static const BYTE timer_init_sig[]={0x55,0x8b,0xec,0x53,0x8b,0x5d,0x08,0x8b,0x45,0x0c,0x89,0x03};
/* 423d38: push ebp; mov ebp,esp; push ebx; push esi; mov si,[ebp+0Ch];
 * mov ebx,[ebp+8]. Relocated 5 bytes (1,2,1,1). */
static const BYTE group_array_sig[]={0x55,0x8b,0xec,0x53,0x56,0x66,0x8b,0x75,0x0c,0x8b,0x5d,0x08};
/* 4178e8: push ebp; mov ebp,esp; add esp,-8; push ebx; push esi; push
 * edi; xor esi,esi. Relocated 6 bytes (1,2,3). */
static const BYTE initial_damage_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xf8,0x53,0x56,0x57,0x33,0xf6};
static const BYTE mission_load_push_lens[]={1,2,1,1},mission_load_frame_lens[]={1,2,3},mission_load_load_lens[]={1,2,1,3};
/* Compare declarations. Every declared byte is compared; a register
 * that holds an address inside the span is compared relative to it. */
static BOOL mission_load_bytes_equal(const es2_hook_comparison *v) {
    for(unsigned i=0;i<v->span_count;++i)if(memcmp(v->original[i],v->replacement[i],v->sizes[i]))return FALSE;
    return TRUE;
}
static uint32_t mission_load_offset(uint32_t value,const BYTE *base) {return value-(uint32_t)(uintptr_t)base;}
/* action_copy: EAX = A+1CEh, EDX = ids+20; region_init: EAX = R, EDX a value. */
static BOOL __cdecl mission_load_eax_span_equal(const es2_hook_comparison *v,void *u) {
    (void)u;
    return mission_load_bytes_equal(v)&&v->original_edx==v->replacement_edx&&
        mission_load_offset(v->original_result,v->original[0])==mission_load_offset(v->replacement_result,v->replacement[0]);
}
/* timer_init: EAX = words+40 (outside the span), EDX = T+2Ch. */
static BOOL __cdecl mission_load_edx_span_equal(const es2_hook_comparison *v,void *u) {
    (void)u;
    return mission_load_bytes_equal(v)&&v->original_result==v->replacement_result&&
        mission_load_offset(v->original_edx,v->original[0])==mission_load_offset(v->replacement_edx,v->replacement[0]);
}
static const es2_hook_span mission_load_action_span[]={{0,0x1ba,0x28}},mission_load_region_span[]={{0,0,10}},
    mission_load_timer_span[]={{0,0,0x31}};
/* condition_init: table 1 behind [C+4] (rows 0..28, one word each) and
 * table 2 behind [C] (words 0..21), isolated in place; both pointers are
 * read once at entry and never written. */
static const es2_hook_region mission_load_condition_regions[]={
    {ES2_REGION_ARG_INDIRECT,0,0,4,0,0x3a,FALSE,0,{0,0}},
    {ES2_REGION_ARG_INDIRECT,0,0,0,0,0x2c,FALSE,0,{0,0}}};
#define MISSION_LOAD_EXT(name,regions,count,unsupported) \
    static const es2_hook_extension mission_load_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,regions,count,mission_load_##name,unsupported}; \
    static uint32_t __cdecl mission_load_##name##32(const uint32_t *a,void *u) {return (uint32_t)mission_load_##name(a,u,weapons_no_registers);}
MISSION_LOAD_EXT(pool_create,NULL,0,TRUE)
MISSION_LOAD_EXT(pool_nth,NULL,0,FALSE)
MISSION_LOAD_EXT(subject_resolve,NULL,0,FALSE)
MISSION_LOAD_EXT(action_copy,NULL,0,FALSE)
MISSION_LOAD_EXT(condition_init,mission_load_condition_regions,2,FALSE)
MISSION_LOAD_EXT(region_init,NULL,0,FALSE)
MISSION_LOAD_EXT(timer_init,NULL,0,FALSE)
MISSION_LOAD_EXT(group_array,NULL,0,TRUE)
MISSION_LOAD_EXT(initial_damage,NULL,0,TRUE)
#undef MISSION_LOAD_EXT
static es2_hook_descriptor mission_load_hook_descriptors[]={
    {ES2_HOOK_ID(pool_create),pool_create_sig,13,5,mission_load_push_lens,4,3,mission_load_pool_create32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(pool_nth),pool_nth_sig,11,6,pool_nth_lens,4,2,mission_load_pool_nth32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(subject_resolve),subject_resolve_sig,14,5,mission_load_push_lens,4,2,mission_load_subject_resolve32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(action_copy),action_copy_sig,10,5,mission_load_push_lens,4,3,mission_load_action_copy32,
        mission_load_eax_span_equal,NULL,mission_load_action_span,1},
    {ES2_HOOK_ID(condition_init),condition_init_sig,12,6,mission_load_frame_lens,3,4,mission_load_condition_init32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(region_init),region_init_sig,13,7,mission_load_load_lens,4,3,mission_load_region_init32,
        mission_load_eax_span_equal,NULL,mission_load_region_span,1},
    {ES2_HOOK_ID(timer_init),timer_init_sig,12,7,mission_load_load_lens,4,4,mission_load_timer_init32,
        mission_load_edx_span_equal,NULL,mission_load_timer_span,1},
    {ES2_HOOK_ID(group_array),group_array_sig,12,5,mission_load_push_lens,4,2,mission_load_group_array32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(initial_damage),initial_damage_sig,11,6,mission_load_frame_lens,3,2,mission_load_initial_damage32,NULL,NULL,NULL,0},
};
#endif
