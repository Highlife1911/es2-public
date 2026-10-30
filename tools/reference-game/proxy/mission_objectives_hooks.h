#ifndef ES2_MISSION_OBJECTIVES_HOOKS_H
#define ES2_MISSION_OBJECTIVES_HOOKS_H
/* Objectives group (docs/reverse-engineering/mission_runtime.md,
 * section "Objectives, route completion and outcome policy"). Included
 * after mission_hooks.h: every game callee goes through the weapons guest
 * bridge and callee observer (weapons_graph), so these hooks refuse compare
 * mode, except the five readers marked OBJECTIVES_COMPARE. No game
 * instruction is embedded. */
#include "es2/core/mission_objectives.h"

#define OBJECTIVES_ADAPTER1(name) \
static uint64_t __cdecl objectives_##name(const uint32_t *a,void *u,const uint32_t *r) { \
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_##name); \
    return es2_##name##_registers(&math_native_memory,&g,a[0],r); }
#define OBJECTIVES_ADAPTER2(name) \
static uint64_t __cdecl objectives_##name(const uint32_t *a,void *u,const uint32_t *r) { \
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_##name); \
    return es2_##name##_registers(&math_native_memory,&g,a[0],a[1],r); }
OBJECTIVES_ADAPTER1(actor_safe) OBJECTIVES_ADAPTER2(target_known) OBJECTIVES_ADAPTER2(order_target)
OBJECTIVES_ADAPTER2(order_done) OBJECTIVES_ADAPTER1(subject_done) OBJECTIVES_ADAPTER1(rivals_done)
OBJECTIVES_ADAPTER2(route_point) OBJECTIVES_ADAPTER1(outcome_poll) OBJECTIVES_ADAPTER2(status_eval)
OBJECTIVES_ADAPTER1(bounds_check) OBJECTIVES_ADAPTER2(bounds_outside)
#undef OBJECTIVES_ADAPTER1
#undef OBJECTIVES_ADAPTER2
/* ECX carries the caller's ESI, captured by the entry shim below (0 on the
 * legacy path). 413280's own entry ECX is then unknown; it only reaches
 * 4137b4 (which never reads it) when the objective table is empty. */
static uint64_t __cdecl objectives_objective_eval(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_objective_eval);
    const uint32_t entry[3]={r[0],r[1],0};
    return es2_objective_eval_registers(&math_native_memory,&g,a[0],r[2],entry);
}
__attribute__((used)) static void *objectives_eval_entry_bridge;
/* 413280 reads the caller's ESI for objectives without a predicate. Pass it
 * in ECX to the framework bridge; EAX, EDX and the stack pass through. */
__attribute__((naked)) static void objectives_eval_entry(void) {
    __asm__ volatile("movl %esi,%ecx\n\tjmp *_objectives_eval_entry_bridge\n\t");
}
/* Called by mission_capture_entry for RVAs it does not own, after the
 * framework installed its jump: redirect it to the shim and keep h->patch
 * consistent, so uninstall still validates the exact patch. */
static BOOL objectives_capture_entry(es2_hook *h) {
    if(h->descriptor->rva!=0x13280u)return vehicle_leaf_capture_entry(h);
    DWORD old,ignored;
    if(memcmp(h->site,h->patch,h->descriptor->relocate_size))return FALSE;
    if(!VirtualProtect(h->site,h->descriptor->relocate_size,PAGE_EXECUTE_READWRITE,&old))return FALSE;
    objectives_eval_entry_bridge=(void *)(uintptr_t)((uint32_t)(uintptr_t)(h->site+5)+math_native_read32(NULL,(uint32_t)(uintptr_t)(h->patch+1)));
    math_native_write32(NULL,(uint32_t)(uintptr_t)(h->patch+1),(uint32_t)(uintptr_t)objectives_eval_entry-(uint32_t)(uintptr_t)(h->site+5));
    memcpy(h->site,h->patch,h->descriptor->relocate_size);
    BOOL flushed=FlushInstructionCache(GetCurrentProcess(),h->site,h->descriptor->relocate_size);
    BOOL protected=VirtualProtect(h->site,h->descriptor->relocate_size,old,&ignored);
    return flushed && protected;
}
static const BYTE objectives_objective_eval_sig[]={0x55,0x8b,0xec,0x81,0xc4,0xf4,0xfe,0xff,0xff};
static const BYTE objectives_actor_safe_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xe8};
static const BYTE objectives_target_known_sig[]={0x55,0x8b,0xec,0x51,0x53,0x56};
static const BYTE objectives_order_target_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08};
static const BYTE objectives_order_done_sig[]={0x55,0x8b,0xec,0x53,0x56,0x8b};
static const BYTE objectives_subject_done_sig[]={0x55,0x8b,0xec,0x53,0x8b,0x5d,0x08};
static const BYTE objectives_rivals_done_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xf8};
static const BYTE objectives_route_point_sig[]={0x55,0x8b,0xec,0x53,0x8b,0x5d,0x0c};
static const BYTE objectives_outcome_poll_sig[]={0x55,0x8b,0xec,0x53,0x8b,0x5d,0x08};
static const BYTE objectives_status_eval_sig[]={0x55,0x8b,0xec,0x66,0xa1,0x54,0x9e,0x4a,0x00};
static const BYTE objectives_bounds_check_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08};
static const BYTE objectives_bounds_outside_sig[]={0x55,0x8b,0xec,0x53,0x56,0xb8};
static const BYTE objectives_long_lens[]={1,2,6},objectives_frame_lens[]={1,2,3},
    objectives_saved_lens[]={1,2,1,1},objectives_load_lens[]={1,2,1,3};
#define OBJECTIVES_EXT_FLAG(name,unsupported) static const es2_hook_extension objectives_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,objectives_##name,unsupported};
#define OBJECTIVES_EXT(name) OBJECTIVES_EXT_FLAG(name,TRUE)
#define OBJECTIVES_LEGACY(name) static uint32_t __cdecl objectives_##name##32(const uint32_t *a,void *u) {return (uint32_t)objectives_##name(a,u,weapons_no_registers);}
#define OBJECTIVES_BOTH(name) OBJECTIVES_EXT(name) OBJECTIVES_LEGACY(name)
/* The five readers whose callees (dist_gate, bounds_outside) are pure
 * accept compare mode. */
#define OBJECTIVES_COMPARE(name) OBJECTIVES_EXT_FLAG(name,FALSE) OBJECTIVES_LEGACY(name)
OBJECTIVES_BOTH(objective_eval) OBJECTIVES_BOTH(actor_safe) OBJECTIVES_COMPARE(target_known) OBJECTIVES_COMPARE(order_target)
OBJECTIVES_BOTH(order_done) OBJECTIVES_BOTH(subject_done) OBJECTIVES_BOTH(rivals_done) OBJECTIVES_COMPARE(route_point)
OBJECTIVES_BOTH(outcome_poll) OBJECTIVES_BOTH(status_eval) OBJECTIVES_COMPARE(bounds_check) OBJECTIVES_COMPARE(bounds_outside)
#undef OBJECTIVES_EXT
#undef OBJECTIVES_EXT_FLAG
#undef OBJECTIVES_LEGACY
#undef OBJECTIVES_BOTH
#undef OBJECTIVES_COMPARE
static es2_hook_descriptor objectives_hook_descriptors[]={
    {ES2_HOOK_ID(objective_eval),objectives_objective_eval_sig,9,9,objectives_long_lens,3,1,objectives_objective_eval32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(actor_safe),objectives_actor_safe_sig,6,6,objectives_frame_lens,3,1,objectives_actor_safe32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(target_known),objectives_target_known_sig,6,5,objectives_saved_lens,4,2,objectives_target_known32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(order_target),objectives_order_target_sig,6,6,objectives_frame_lens,3,2,objectives_order_target32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(order_done),objectives_order_done_sig,6,5,objectives_saved_lens,4,2,objectives_order_done32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(subject_done),objectives_subject_done_sig,7,7,objectives_load_lens,4,1,objectives_subject_done32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(rivals_done),objectives_rivals_done_sig,6,6,objectives_frame_lens,3,1,objectives_rivals_done32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(route_point),objectives_route_point_sig,7,7,objectives_load_lens,4,2,objectives_route_point32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(outcome_poll),objectives_outcome_poll_sig,7,7,objectives_load_lens,4,1,objectives_outcome_poll32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(status_eval),objectives_status_eval_sig,9,9,objectives_long_lens,3,2,objectives_status_eval32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(bounds_check),objectives_bounds_check_sig,6,6,objectives_frame_lens,3,1,objectives_bounds_check32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(bounds_outside),objectives_bounds_outside_sig,6,5,objectives_saved_lens,4,2,objectives_bounds_outside32,NULL,NULL,NULL,0},
};
#endif
