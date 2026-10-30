#ifndef ES2_OBJECTIVE_PREDS_HOOKS_H
#define ES2_OBJECTIVE_PREDS_HOOKS_H
/* objective_preds group (docs/reverse-engineering/mission_runtime.md,
 * section "objective predicates"): the status helpers under
 * subject_done, rivals_done, objective_eval and status_check, one hook per
 * function. Leaves without game calls: subject_group 412bd0, its twin
 * subject_actor 412bbc, group_down 412be4, group_flagged 412c58,
 * group_marked 412d90, route_flag 41324c. With game calls through the
 * weapons guest bridge and callee observer (weapons_graph), so nested hooks
 * keep their modes: subject_status 412dc4 (412bd0, 412bbc, group_state
 * 412c8c, the actor's virtual +40h), state_reached 413920 (412c8c),
 * group_safe 413950 (413920, actor_safe 4137b4), bounds_margin 413780
 * (bounds_outside 41373c). No entry shim: no uninitialized local and no
 * caller register other than the documented ones is read (the entry
 * EAX/EDX/ECX only reach callees that never read them). ECX after each
 * hook is dead at every caller (POP ECX or ADD ESP followed by a write).
 * The six leaves and bounds_margin (whose only callee bounds_outside is a
 * pure reader, as for bounds_check) accept compare mode; subject_status,
 * state_reached and group_safe refuse it (group_state's actor virtual,
 * actor_safe). No game instruction is embedded. */
#include "es2/core/mission_objectives.h"
#define OBJECTIVE_PREDS_LEAF(name) \
static uint64_t __cdecl objective_preds_##name(const uint32_t *a,void *u,const uint32_t *r) { \
    (void)u;(void)r;return es2_##name##_registers(&math_native_memory,a[0]); }
#define OBJECTIVE_PREDS_GRAPH(name) \
static uint64_t __cdecl objective_preds_##name(const uint32_t *a,void *u,const uint32_t *r) { \
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_##name); \
    return es2_##name##_registers(&math_native_memory,&g,a[0],r); }
OBJECTIVE_PREDS_LEAF(subject_group) OBJECTIVE_PREDS_LEAF(subject_actor) OBJECTIVE_PREDS_LEAF(group_down)
OBJECTIVE_PREDS_LEAF(group_flagged) OBJECTIVE_PREDS_LEAF(group_marked)
OBJECTIVE_PREDS_GRAPH(subject_status) OBJECTIVE_PREDS_GRAPH(state_reached) OBJECTIVE_PREDS_GRAPH(group_safe)
OBJECTIVE_PREDS_GRAPH(bounds_margin)
#undef OBJECTIVE_PREDS_LEAF
#undef OBJECTIVE_PREDS_GRAPH
static uint64_t __cdecl objective_preds_route_flag(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;(void)r;return es2_route_flag_registers(&math_native_memory,a[0],a[1]);
}
/* Callee counters (hk_<hook>_<suffix>), incremented only while the hook is
 * non-original. */
static callee_counter subject_status_counters[]={
    {"call_412bd0",ES2_COLLIDE_CALL,0x412bd0,0},
    {"call_412bbc",ES2_COLLIDE_CALL,0x412bbc,0},
    {"call_412c8c",ES2_COLLIDE_CALL,0x412c8c,0},
    {"slot_40",ES2_COLLIDE_SLOT,0x40,0},
};
static callee_counter state_reached_counters[]={
    {"call_412c8c",ES2_COLLIDE_CALL,0x412c8c,0},
};
static callee_counter group_safe_counters[]={
    {"call_413920",ES2_COLLIDE_CALL,0x413920,0},
    {"call_4137b4",ES2_COLLIDE_CALL,0x4137b4,0},
};
static callee_counter bounds_margin_counters[]={
    {"call_41373c",ES2_COLLIDE_CALL,0x41373c,0},
};
static callee_counter *objective_preds_hook_counters(unsigned hook,unsigned *count) {
    if(hook==ES2_HOOK_INDEX_subject_status) { *count=4;return subject_status_counters; }
    if(hook==ES2_HOOK_INDEX_state_reached) { *count=1;return state_reached_counters; }
    if(hook==ES2_HOOK_INDEX_group_safe) { *count=2;return group_safe_counters; }
    if(hook==ES2_HOOK_INDEX_bounds_margin) { *count=1;return bounds_margin_counters; }
    *count=0;return NULL;
}
/* push ebp; mov ebp,esp; push ebx; push esi; push edi; mov ebx,[ebp+8] */
static const BYTE objective_preds_subject_status_sig[]={0x55,0x8b,0xec,0x53,0x56,0x57,0x8b,0x5d,0x08};
/* push ebp; mov ebp,esp; mov eax,[ebp+8]; mov edx,[eax+6Ch] (412bd0 and 412bbc) */
static const BYTE objective_preds_subject_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x8b,0x50,0x6c};
/* push ebp; mov ebp,esp; push ecx; push esi; push edi; xor ecx,ecx */
static const BYTE objective_preds_group_down_sig[]={0x55,0x8b,0xec,0x51,0x56,0x57,0x33,0xc9};
/* push ebp; mov ebp,esp; push esi; mov esi,[ebp+8]; xor edx,edx (412c58 and 412d90) */
static const BYTE objective_preds_scan_sig[]={0x55,0x8b,0xec,0x56,0x8b,0x75,0x08,0x33,0xd2};
/* push ebp; mov ebp,esp; push ebx; push esi; mov esi,[ebp+0Ch] */
static const BYTE objective_preds_route_flag_sig[]={0x55,0x8b,0xec,0x53,0x56,0x8b,0x75,0x0c};
/* push ebp; mov ebp,esp; push ebx; push esi; mov ebx,[ebp+8]; mov esi,4 */
static const BYTE objective_preds_state_reached_sig[]={0x55,0x8b,0xec,0x53,0x56,0x8b,0x5d,0x08,0xbe,0x04,0x00,0x00,0x00};
/* push ebp; mov ebp,esp; push ebx; push esi; mov esi,[ebp+8]; push esi */
static const BYTE objective_preds_group_safe_sig[]={0x55,0x8b,0xec,0x53,0x56,0x8b,0x75,0x08,0x56};
/* push ebp; mov ebp,esp; mov eax,[ebp+8]; push 1ADB0h */
static const BYTE objective_preds_bounds_margin_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x68,0xb0,0xad,0x01,0x00};
static const BYTE objective_preds_saved_lens[]={1,2,1,1},objective_preds_frame_lens[]={1,2,3},objective_preds_load_lens[]={1,2,1,3};
#define OBJECTIVE_PREDS_EXT(name,unsupported) \
    static const es2_hook_extension objective_preds_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,objective_preds_##name,unsupported}; \
    static uint32_t __cdecl objective_preds_##name##32(const uint32_t *a,void *u) {return (uint32_t)objective_preds_##name(a,u,weapons_no_registers);}
OBJECTIVE_PREDS_EXT(subject_status,TRUE)
OBJECTIVE_PREDS_EXT(subject_group,FALSE)
OBJECTIVE_PREDS_EXT(subject_actor,FALSE)
OBJECTIVE_PREDS_EXT(group_down,FALSE)
OBJECTIVE_PREDS_EXT(route_flag,FALSE)
OBJECTIVE_PREDS_EXT(state_reached,TRUE)
OBJECTIVE_PREDS_EXT(group_safe,TRUE)
OBJECTIVE_PREDS_EXT(group_marked,FALSE)
OBJECTIVE_PREDS_EXT(group_flagged,FALSE)
OBJECTIVE_PREDS_EXT(bounds_margin,FALSE)
#undef OBJECTIVE_PREDS_EXT
static es2_hook_descriptor objective_preds_hook_descriptors[]={
    {ES2_HOOK_ID(subject_status),objective_preds_subject_status_sig,9,5,objective_preds_saved_lens,4,1,objective_preds_subject_status32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(subject_group),objective_preds_subject_sig,9,6,objective_preds_frame_lens,3,1,objective_preds_subject_group32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(subject_actor),objective_preds_subject_sig,9,6,objective_preds_frame_lens,3,1,objective_preds_subject_actor32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(group_down),objective_preds_group_down_sig,8,5,objective_preds_saved_lens,4,1,objective_preds_group_down32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(route_flag),objective_preds_route_flag_sig,8,5,objective_preds_saved_lens,4,2,objective_preds_route_flag32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(state_reached),objective_preds_state_reached_sig,13,5,objective_preds_saved_lens,4,1,objective_preds_state_reached32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(group_safe),objective_preds_group_safe_sig,9,5,objective_preds_saved_lens,4,1,objective_preds_group_safe32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(group_marked),objective_preds_scan_sig,9,7,objective_preds_load_lens,4,1,objective_preds_group_marked32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(group_flagged),objective_preds_scan_sig,9,7,objective_preds_load_lens,4,1,objective_preds_group_flagged32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(bounds_margin),objective_preds_bounds_margin_sig,11,6,objective_preds_frame_lens,3,1,objective_preds_bounds_margin32,NULL,NULL,NULL,0},
};
#endif
