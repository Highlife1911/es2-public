#ifndef ES2_AI_HELPERS_HOOKS_H
#define ES2_AI_HELPERS_HOOKS_H
/* Route and behaviour helpers (docs/reverse-engineering/
 * mission_runtime.md, section "Route and behaviour helpers"): point_offset 4929fc, threat_sum
 * 41cb44, engage_done 41c4a8, flank_step 41c72c, retreat_check 41cb94,
 * part_pick 41ce08 and drive_apply 41e828. Every game callee (target_class,
 * order_target, timer_word_step, heading_point, rng_next, part_ratio,
 * vehicle_control, script_start, order_resume, the list step 471c70, the
 * actor virtuals +3Ch/+40h and the group's own point_offset/threat_sum)
 * goes through the weapons guest bridge and callee observer
 * (weapons_graph) with the original's registers, so six refuse compare
 * mode. point_offset (its cosine lookups are computed in core, as in
 * sincos_pair) makes no game call and writes only P[0..7]; it accepts compare with an 8-byte span on P and a comparator that remaps
 * EAX = P (the clone in the replacement phase). The frame point
 * of flank_step lives in the adapter's scratch. No frame local is read
 * before it is written, so no entry shim; no game instruction embedded. */
#include "es2/core/ai_helpers.h"
#define AI_HELPERS_GRAPH(name) \
    BYTE scratch[ES2_AI_HELPERS_SCRATCH]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_##name)
static uint64_t __cdecl ai_helpers_point_offset(const uint32_t *a,void *u,const uint32_t *r) {
    (void)r;return es2_point_offset_registers(&math_native_memory,a[0],a[1],a[2],(const BYTE *)u+0xa25c4);
}
static uint64_t __cdecl ai_helpers_threat_sum(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_threat_sum);
    return es2_threat_sum_registers(&math_native_memory,&g,a[0],r);
}
static uint64_t __cdecl ai_helpers_engage_done(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_engage_done);
    return es2_engage_done_registers(&math_native_memory,&g,a[0],r);
}
static uint64_t __cdecl ai_helpers_flank_step(const uint32_t *a,void *u,const uint32_t *r) {
    AI_HELPERS_GRAPH(flank_step);
    return es2_flank_step_registers(&math_native_memory,&g,a[0],a[1],r);
}
static uint64_t __cdecl ai_helpers_retreat_check(const uint32_t *a,void *u,const uint32_t *r) {
    AI_HELPERS_GRAPH(retreat_check);
    return es2_retreat_check_registers(&math_native_memory,&g,a[0],r);
}
static uint64_t __cdecl ai_helpers_part_pick(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_part_pick);
    return es2_part_pick_registers(&math_native_memory,&g,a[0],r);
}
static uint64_t __cdecl ai_helpers_drive_apply(const uint32_t *a,void *u,const uint32_t *r) {
    (void)r;es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_drive_apply);
    return es2_drive_apply_registers(&math_native_memory,&g,a[0],a[1]);
}
#undef AI_HELPERS_GRAPH
/* Callee counters (hk_<hook>_<suffix>), incremented only while the hook is
 * non-original. Keys: the callee VA, or the vtable offset for slots;
 * point_offset has no game calls. */
static callee_counter threat_sum_counters[]={
    {"call_471c70",ES2_COLLIDE_CALL,0x471c70,0},
};
static callee_counter engage_done_counters[]={
    {"call_411cb4",ES2_COLLIDE_CALL,0x411cb4,0},
    {"call_423918",ES2_COLLIDE_CALL,0x423918,0},
};
static callee_counter flank_step_counters[]={
    {"call_467a48",ES2_COLLIDE_CALL,0x467a48,0},
    {"slot_3c",ES2_COLLIDE_SLOT,0x3c,0},
    {"call_4929fc",ES2_COLLIDE_CALL,0x4929fc,0},
    {"call_492934",ES2_COLLIDE_CALL,0x492934,0},
};
static callee_counter retreat_check_counters[]={
    {"call_41eb34",ES2_COLLIDE_CALL,0x41eb34,0},
    {"call_413e50",ES2_COLLIDE_CALL,0x413e50,0},
    {"slot_40",ES2_COLLIDE_SLOT,0x40,0},
    {"call_41cb44",ES2_COLLIDE_CALL,0x41cb44,0},
};
static callee_counter part_pick_counters[]={
    {"call_492ee0",ES2_COLLIDE_CALL,0x492ee0,0},
    {"call_40dbc0",ES2_COLLIDE_CALL,0x40dbc0,0},
};
static callee_counter drive_apply_counters[]={
    {"call_416a04",ES2_COLLIDE_CALL,0x416a04,0},
};
static callee_counter *ai_helpers_hook_counters(unsigned hook,unsigned *count) {
#define AI_HELPERS_COUNTERS(name) if(hook==ES2_HOOK_INDEX_##name) { \
        *count=sizeof(name##_counters)/sizeof(name##_counters[0]);return name##_counters; }
    AI_HELPERS_COUNTERS(threat_sum)
    AI_HELPERS_COUNTERS(engage_done)
    AI_HELPERS_COUNTERS(flank_step)
    AI_HELPERS_COUNTERS(retreat_check)
    AI_HELPERS_COUNTERS(part_pick)
    AI_HELPERS_COUNTERS(drive_apply)
#undef AI_HELPERS_COUNTERS
    *count=0;return NULL;
}
static const BYTE ai_helpers_point_offset_sig[]={0x55,0x8b,0xec,0x53,0x56,0x57,0x8b,0x75,0x0c};
static const BYTE ai_helpers_threat_sum_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xf8,0x33,0xc0};
static const BYTE ai_helpers_engage_done_sig[]={0x55,0x8b,0xec,0x53,0x56,0x57,0x8b,0x5d,0x08};
static const BYTE ai_helpers_flank_step_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xf4,0x53,0x56,0x57};
static const BYTE ai_helpers_retreat_check_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xb8,0x53,0x56};
static const BYTE ai_helpers_part_pick_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xf4,0x53,0x56,0x57};
static const BYTE ai_helpers_drive_apply_sig[]={0x55,0x8b,0xec,0x53,0x56,0x57,0x33,0xf6};
static const BYTE ai_helpers_frame_lens[]={1,2,3},ai_helpers_push_lens[]={1,2,1,1};
#define AI_HELPERS_EXT_FLAG(name,unsupported) \
    static const es2_hook_extension ai_helpers_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,ai_helpers_##name,unsupported}; \
    static uint32_t __cdecl ai_helpers_##name##32(const uint32_t *a,void *u) {return (uint32_t)ai_helpers_##name(a,u,weapons_no_registers);}
#define AI_HELPERS_EXT(name) AI_HELPERS_EXT_FLAG(name,TRUE)
AI_HELPERS_EXT_FLAG(point_offset,FALSE)
AI_HELPERS_EXT(threat_sum)
AI_HELPERS_EXT(engage_done)
AI_HELPERS_EXT(flank_step)
AI_HELPERS_EXT(retreat_check)
AI_HELPERS_EXT(part_pick)
AI_HELPERS_EXT(drive_apply)
#undef AI_HELPERS_EXT
#undef AI_HELPERS_EXT_FLAG
/* Compare: point_offset's span P[0..7]; EAX is P (the clone in the
 * replacement phase), EDX the sign of the second product. */
static const es2_hook_span ai_helpers_point_offset_spans[]={{0,0,8}};
static BOOL __cdecl ai_helpers_point_offset_equal(const es2_hook_comparison *v,void *u) {
    (void)u;const uint32_t o=(uint32_t)(uintptr_t)v->original[0],r=(uint32_t)(uintptr_t)v->replacement[0];
    return !memcmp(v->original[0],v->replacement[0],8)&&v->original_result-o==v->replacement_result-r&&
        v->original_edx==v->replacement_edx;
}
static es2_hook_descriptor ai_helpers_hook_descriptors[]={
    {ES2_HOOK_ID(point_offset),ai_helpers_point_offset_sig,9,5,ai_helpers_push_lens,4,3,ai_helpers_point_offset32,ai_helpers_point_offset_equal,NULL,ai_helpers_point_offset_spans,1},
    {ES2_HOOK_ID(threat_sum),ai_helpers_threat_sum_sig,8,6,ai_helpers_frame_lens,3,1,ai_helpers_threat_sum32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(engage_done),ai_helpers_engage_done_sig,9,5,ai_helpers_push_lens,4,1,ai_helpers_engage_done32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(flank_step),ai_helpers_flank_step_sig,9,6,ai_helpers_frame_lens,3,2,ai_helpers_flank_step32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(retreat_check),ai_helpers_retreat_check_sig,8,6,ai_helpers_frame_lens,3,1,ai_helpers_retreat_check32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(part_pick),ai_helpers_part_pick_sig,9,6,ai_helpers_frame_lens,3,1,ai_helpers_part_pick32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(drive_apply),ai_helpers_drive_apply_sig,8,5,ai_helpers_push_lens,4,2,ai_helpers_drive_apply32,NULL,NULL,NULL,0},
};
#endif
