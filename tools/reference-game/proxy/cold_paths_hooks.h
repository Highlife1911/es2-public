#ifndef ES2_COLD_PATHS_HOOKS_H
#define ES2_COLD_PATHS_HOOKS_H
/* cold_paths group: six functions that no suite enters (0 calls in all
 * suites), bridged as guest calls by replaced hooks.
 * Contracts: input.md "the targeting computer 40e4dc and 40e4ac"
 * (targ_aim 40e4dc under aim_point, targ_next 40e4ac under targ_aim and
 * hud_key), weapons.md "missile steering 40a488 and ally selection
 * 423974" (missile_steer under projectile_flight, ally_nearest under
 * sweep), ai.md "TURBO engage 40f09c" (turbo_engage under
 * point_move and retreat_fire), mission_runtime.md "member
 * assessment 41b804" (member_assess under group_assess).
 *
 * Every game call (the target's virtuals +24h/+58h/+80h/+84h, 40e4ac,
 * timer_dword_step, euler_matrix, transform_point, multiply_q8,
 * movement_step, dist_gate, sfx_play, actor_safe, pool_prev,
 * random_damage) goes through the weapons guest bridge and callee observer
 * (weapons_graph) with the original's EAX/EDX/ECX, so nested hooks keep
 * their modes. No entry shim: none of the six reads an uninitialized local
 * or a caller register beyond the dispatch context (40e4dc's PUSH ECX
 * local holds the entry ECX and is only popped). EDX:EAX exact. ECX after
 * each hook is the entry ECX (the framework restores it); the originals'
 * ECX is dead at every caller (the contracts list the sites: popped,
 * overwritten, or only the high word of a slot the callees read as a
 * word). ally_nearest accepts compare mode (a read-only query whose only
 * callee dist_gate is pure); the other five refuse it (virtual calls,
 * state writes, sound, RNG and damage). No game instruction is embedded. */
#include "es2/core/input.h"
#include "es2/core/weapons.h"
#include "es2/core/mech_travel.h"
#include "es2/core/groups.h"
static uint64_t __cdecl cold_paths_targ_aim(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_targ_aim);
    return es2_targ_aim_registers(&math_native_memory,&g,a[0],a[1],a[2],a[4],a[5],r);
}
static uint64_t __cdecl cold_paths_targ_next(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_targ_next);
    return es2_targ_next_registers(&math_native_memory,&g,a[0],a[1],r);
}
static uint64_t __cdecl cold_paths_missile_steer(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_missile_steer);
    return es2_missile_steer_registers(&math_native_memory,&g,a[0],a[1],r);
}
static uint64_t __cdecl cold_paths_ally_nearest(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_ally_nearest);
    return es2_ally_nearest_registers(&math_native_memory,&g,a[0],a[1],r);
}
static uint64_t __cdecl cold_paths_turbo_engage(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_turbo_engage);
    return es2_turbo_engage_registers(&math_native_memory,&g,a[0],r);
}
static uint64_t __cdecl cold_paths_member_assess(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_member_assess);
    return es2_member_assess_registers(&math_native_memory,&g,a[0],r);
}
/* Callee counters (hk_<hook>_<suffix>), incremented only while the hook is
 * non-original. Keys: the callee VA for calls, the vtable offset for
 * slots. */
static callee_counter targ_aim_counters[]={
    {"slot_84",ES2_COLLIDE_SLOT,0x84,0},
    {"call_40e4ac",ES2_COLLIDE_CALL,0x40e4ac,0},
    {"slot_58",ES2_COLLIDE_SLOT,0x58,0},
    {"call_467aa8",ES2_COLLIDE_CALL,0x467aa8,0},
    {"slot_24",ES2_COLLIDE_SLOT,0x24,0},
    {"call_47ebb8",ES2_COLLIDE_CALL,0x47ebb8,0},
    {"call_48043c",ES2_COLLIDE_CALL,0x48043c,0},
};
static callee_counter targ_next_counters[]={{"slot_80",ES2_COLLIDE_SLOT,0x80,0}};
static callee_counter missile_steer_counters[]={
    {"call_47e0a0",ES2_COLLIDE_CALL,0x47e0a0,0},
    {"call_467924",ES2_COLLIDE_CALL,0x467924,0},
};
static callee_counter ally_nearest_counters[]={{"call_49288c",ES2_COLLIDE_CALL,0x49288c,0}};
static callee_counter turbo_engage_counters[]={{"call_462830",ES2_COLLIDE_CALL,0x462830,0}};
static callee_counter member_assess_counters[]={
    {"call_4137b4",ES2_COLLIDE_CALL,0x4137b4,0},
    {"call_471c70",ES2_COLLIDE_CALL,0x471c70,0},
    {"call_49288c",ES2_COLLIDE_CALL,0x49288c,0},
    {"call_417a04",ES2_COLLIDE_CALL,0x417a04,0},
};
#define COLD_PATHS_COUNTERS(name) if(hook==ES2_HOOK_INDEX_##name) { \
    *count=sizeof(name##_counters)/sizeof(name##_counters[0]);return name##_counters; }
static callee_counter *cold_paths_hook_counters(unsigned hook,unsigned *count) {
    COLD_PATHS_COUNTERS(targ_aim) COLD_PATHS_COUNTERS(targ_next) COLD_PATHS_COUNTERS(missile_steer)
    COLD_PATHS_COUNTERS(ally_nearest) COLD_PATHS_COUNTERS(turbo_engage) COLD_PATHS_COUNTERS(member_assess)
    *count=0;return NULL;
}
#undef COLD_PATHS_COUNTERS
/* 40e4dc: push ebp; mov ebp,esp; push ecx; push ebx; push esi; push edi;
 * mov ebx,[ebp+8]; mov edi,[ebp+0Ch]. Relocated 5 bytes (1,2,1,1). */
static const BYTE cold_paths_targ_aim_sig[]={0x55,0x8b,0xec,0x51,0x53,0x56,0x57,0x8b,0x5d,0x08,0x8b,0x7d,0x0c};
/* 40e4ac: push ebp; mov ebp,esp; push ebx; push esi; mov esi,[ebp+0Ch];
 * mov ebx,[ebp+8]. Relocated 5 bytes (1,2,1,1). */
static const BYTE cold_paths_targ_next_sig[]={0x55,0x8b,0xec,0x53,0x56,0x8b,0x75,0x0c,0x8b,0x5d,0x08};
/* 40a488: push ebp; mov ebp,esp; add esp,-8; push ebx; push esi; push edi;
 * mov esi,[ebp+0Ch]. Relocated 6 bytes (1,2,3). */
static const BYTE cold_paths_missile_steer_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xf8,0x53,0x56,0x57,0x8b,0x75,0x0c};
/* 423974: push ebp; mov ebp,esp; add esp,-0Ch; xor eax,eax; push ebx;
 * push esi; push edi. Relocated 6 bytes (1,2,3). */
static const BYTE cold_paths_ally_nearest_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xf4,0x33,0xc0,0x53,0x56,0x57};
/* 40f09c: push ebp; mov ebp,esp; mov eax,[ebp+8]; cmp dword [eax+81h],0.
 * Relocated 6 bytes (1,2,3). */
static const BYTE cold_paths_turbo_engage_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x83,0xb8,0x81,0x00,0x00,0x00,0x00};
/* 41b804: push ebp; mov ebp,esp; add esp,-34h; push ebx; push esi; push
 * edi; mov esi,[ebp+8]. Relocated 6 bytes (1,2,3). */
static const BYTE cold_paths_member_assess_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xcc,0x53,0x56,0x57,0x8b,0x75,0x08};
static const BYTE cold_paths_saved_lens[]={1,2,1,1},cold_paths_frame_lens[]={1,2,3};
/* ally_nearest: EAX (the member) and EDX (the last pass's register). */
static BOOL __cdecl cold_paths_ally_equal(const es2_hook_comparison *v,void *u) {
    (void)u;return v->original_result==v->replacement_result&&v->original_edx==v->replacement_edx;
}
#define COLD_PATHS_EXT(name,unsupported) \
    static const es2_hook_extension cold_paths_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,cold_paths_##name,unsupported}; \
    static uint32_t __cdecl cold_paths_##name##32(const uint32_t *a,void *u) {return (uint32_t)cold_paths_##name(a,u,weapons_no_registers);}
COLD_PATHS_EXT(targ_aim,TRUE)
COLD_PATHS_EXT(targ_next,TRUE)
COLD_PATHS_EXT(missile_steer,TRUE)
COLD_PATHS_EXT(ally_nearest,FALSE)
COLD_PATHS_EXT(turbo_engage,TRUE)
COLD_PATHS_EXT(member_assess,TRUE)
#undef COLD_PATHS_EXT
static es2_hook_descriptor cold_paths_hook_descriptors[]={
    {ES2_HOOK_ID(targ_aim),cold_paths_targ_aim_sig,13,5,cold_paths_saved_lens,4,6,cold_paths_targ_aim32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(targ_next),cold_paths_targ_next_sig,11,5,cold_paths_saved_lens,4,2,cold_paths_targ_next32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(missile_steer),cold_paths_missile_steer_sig,12,6,cold_paths_frame_lens,3,2,cold_paths_missile_steer32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(ally_nearest),cold_paths_ally_nearest_sig,11,6,cold_paths_frame_lens,3,2,cold_paths_ally_nearest32,cold_paths_ally_equal,NULL,NULL,0},
    {ES2_HOOK_ID(turbo_engage),cold_paths_turbo_engage_sig,13,6,cold_paths_frame_lens,3,1,cold_paths_turbo_engage32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(member_assess),cold_paths_member_assess_sig,12,6,cold_paths_frame_lens,3,1,cold_paths_member_assess32,NULL,NULL,NULL,0},
};
#endif
