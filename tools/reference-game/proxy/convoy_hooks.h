#ifndef ES2_CONVOY_HOOKS_H
#define ES2_CONVOY_HOOKS_H
/* Update chain of the building-pool class VEHICLE (vtable 497818,
 * slot +18h; docs/reverse-engineering/convoy.md): convoy_steer 46a89c,
 * convoy_seek 46a958, convoy_lead 46a9e8, convoy_follow 46aa60,
 * convoy_drive 46a810 and convoy_think 46a6d4. Every game callee goes
 * through the weapons guest bridge and callee observer (weapons_graph), so
 * five refuse compare mode. convoy_steer accepts it: its callees
 * timer_word_step, approach_word and multiply_q8 are pure apart from the
 * words they are given (B+223h, B+220h), and it writes B+0Ch..B+11h and
 * B+32h, so one span B+0Ch..B+227h (the prefix and span also hold the
 * reverse byte B+227h it reads) and a comparator that remaps EAX = B+0Ch
 * cover it (compare_mode.md, "spans and comparators"). The leader search 46a5bc, the base update
 * 403ca8, the collision test 46a614, the projection 4929fc, the 2 x 2 swap
 * 47df04 and the VEHICLE virtual +78h stay original code. Frame locals that
 * the originals pass by pointer live in the adapter's scratch.
 * convoy_follow's entry EDI (callee-saved, not set by 46aa60) is passed as
 * object + 26h, the value of the game's only call chain (46a6d4 sets it at
 * 46a768, 46a810 keeps it); its high word reaches only callee slots that
 * read the low word (convoy.md). No entry shim, no game instruction
 * embedded. */
#include "es2/core/convoy.h"
#define CONVOY_GRAPH(name) \
    BYTE scratch[ES2_CONVOY_SCRATCH]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_##name)
static uint64_t __cdecl convoy_convoy_steer(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_convoy_steer);
    return es2_convoy_steer_registers(&math_native_memory,&g,a[0],a[1],a[2],r);
}
static uint64_t __cdecl convoy_convoy_seek(const uint32_t *a,void *u,const uint32_t *r) {
    CONVOY_GRAPH(convoy_seek);
    return es2_convoy_seek_registers(&math_native_memory,&g,a[0],a[1],a[2],a[3],r);
}
static uint64_t __cdecl convoy_convoy_lead(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_convoy_lead);
    return es2_convoy_lead_registers(&math_native_memory,&g,a[0],r);
}
static uint64_t __cdecl convoy_convoy_follow(const uint32_t *a,void *u,const uint32_t *r) {
    CONVOY_GRAPH(convoy_follow);
    return es2_convoy_follow_registers(&math_native_memory,&g,a[0],a[0]+0x26u,r);
}
static uint64_t __cdecl convoy_convoy_drive(const uint32_t *a,void *u,const uint32_t *r) {
    CONVOY_GRAPH(convoy_drive);
    return es2_convoy_drive_registers(&math_native_memory,&g,a[0],r);
}
static uint64_t __cdecl convoy_convoy_think(const uint32_t *a,void *u,const uint32_t *r) {
    CONVOY_GRAPH(convoy_think);
    return es2_convoy_think_registers(&math_native_memory,&g,a[0],r);
}
#undef CONVOY_GRAPH
/* Callee counters (hk_<hook>_<suffix>), incremented only while the hook is
 * non-original. Keys: the callee VA, or the vtable offset for slots. */
static callee_counter convoy_steer_counters[]={
    {"call_467a48",ES2_COLLIDE_CALL,0x467a48,0},
    {"call_467adc",ES2_COLLIDE_CALL,0x467adc,0},
    {"call_47e0a0",ES2_COLLIDE_CALL,0x47e0a0,0},
};
static callee_counter convoy_seek_counters[]={
    {"call_4928d0",ES2_COLLIDE_CALL,0x4928d0,0},
    {"call_4929fc",ES2_COLLIDE_CALL,0x4929fc,0},
    {"call_492934",ES2_COLLIDE_CALL,0x492934,0},
    {"call_46a89c",ES2_COLLIDE_CALL,0x46a89c,0},
};
static callee_counter convoy_lead_counters[]={
    {"call_423b0c",ES2_COLLIDE_CALL,0x423b0c,0},
    {"call_492934",ES2_COLLIDE_CALL,0x492934,0},
    {"call_46a958",ES2_COLLIDE_CALL,0x46a958,0},
    {"call_42313c",ES2_COLLIDE_CALL,0x42313c,0},
    {"call_46a89c",ES2_COLLIDE_CALL,0x46a89c,0},
};
static callee_counter convoy_follow_counters[]={
    {"call_46a5bc",ES2_COLLIDE_CALL,0x46a5bc,0},
    {"slot_78",ES2_COLLIDE_SLOT,0x78,0},
    {"call_47ebb8",ES2_COLLIDE_CALL,0x47ebb8,0},
    {"call_47df04",ES2_COLLIDE_CALL,0x47df04,0},
    {"call_47ffe8",ES2_COLLIDE_CALL,0x47ffe8,0},
    {"call_4929fc",ES2_COLLIDE_CALL,0x4929fc,0},
    {"call_46a958",ES2_COLLIDE_CALL,0x46a958,0},
    {"call_4928d0",ES2_COLLIDE_CALL,0x4928d0,0},
    {"call_492934",ES2_COLLIDE_CALL,0x492934,0},
    {"call_46a89c",ES2_COLLIDE_CALL,0x46a89c,0},
};
static callee_counter convoy_drive_counters[]={
    {"call_46a5bc",ES2_COLLIDE_CALL,0x46a5bc,0},
    {"call_46a9e8",ES2_COLLIDE_CALL,0x46a9e8,0},
    {"call_46aa60",ES2_COLLIDE_CALL,0x46aa60,0},
    {"call_47ebb8",ES2_COLLIDE_CALL,0x47ebb8,0},
    {"call_48043c",ES2_COLLIDE_CALL,0x48043c,0},
};
static callee_counter convoy_think_counters[]={
    {"call_403ca8",ES2_COLLIDE_CALL,0x403ca8,0},
    {"call_404100",ES2_COLLIDE_CALL,0x404100,0},
    {"call_403d5c",ES2_COLLIDE_CALL,0x403d5c,0},
    {"call_46a810",ES2_COLLIDE_CALL,0x46a810,0},
    {"call_4029d8",ES2_COLLIDE_CALL,0x4029d8,0},
    {"call_46a614",ES2_COLLIDE_CALL,0x46a614,0},
};
static callee_counter *convoy_hook_counters(unsigned hook,unsigned *count) {
#define CONVOY_COUNTERS(name) if(hook==ES2_HOOK_INDEX_convoy_##name) { \
        *count=sizeof(convoy_##name##_counters)/sizeof(convoy_##name##_counters[0]);return convoy_##name##_counters; }
    CONVOY_COUNTERS(steer)
    CONVOY_COUNTERS(seek)
    CONVOY_COUNTERS(lead)
    CONVOY_COUNTERS(follow)
    CONVOY_COUNTERS(drive)
    CONVOY_COUNTERS(think)
#undef CONVOY_COUNTERS
    *count=0;return NULL;
}
static const BYTE convoy_steer_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xf8,0x53,0x56,0x57};
static const BYTE convoy_seek_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xf4,0x53,0x56,0x57};
static const BYTE convoy_lead_sig[]={0x55,0x8b,0xec,0x53,0x56,0x57,0x8b,0x5d,0x08};
static const BYTE convoy_follow_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xcc,0x53,0x56,0x57};
static const BYTE convoy_drive_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xe8,0x53,0x56,0x57};
static const BYTE convoy_think_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xe4,0x53,0x56,0x57};
static const BYTE convoy_frame_lens[]={1,2,3},convoy_push_lens[]={1,2,1,1};
#define CONVOY_EXT_FLAG(name,unsupported) \
    static const es2_hook_extension convoy_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,convoy_##name,unsupported}; \
    static uint32_t __cdecl convoy_##name##32(const uint32_t *a,void *u) {return (uint32_t)convoy_##name(a,u,weapons_no_registers);}
#define CONVOY_EXT(name) CONVOY_EXT_FLAG(name,TRUE)
CONVOY_EXT_FLAG(convoy_steer,FALSE)
CONVOY_EXT(convoy_seek)
CONVOY_EXT(convoy_lead)
CONVOY_EXT(convoy_follow)
CONVOY_EXT(convoy_drive)
CONVOY_EXT(convoy_think)
#undef CONVOY_EXT
#undef CONVOY_EXT_FLAG
/* Compare: convoy_steer's span B+0Ch..B+227h; EAX = B+0Ch (the clone
 * in the replacement phase), EDX the new angle words. */
static const es2_hook_span convoy_steer_spans[]={{0,0x0c,0x21c}};
static BOOL __cdecl convoy_steer_equal(const es2_hook_comparison *v,void *u) {
    (void)u;const uint32_t o=(uint32_t)(uintptr_t)v->original[0],r=(uint32_t)(uintptr_t)v->replacement[0];
    return !memcmp(v->original[0],v->replacement[0],v->sizes[0])&&v->original_result-o==v->replacement_result-r&&
        v->original_edx==v->replacement_edx;
}
static es2_hook_descriptor convoy_hook_descriptors[]={
    {ES2_HOOK_ID(convoy_steer),convoy_steer_sig,9,6,convoy_frame_lens,3,3,convoy_convoy_steer32,convoy_steer_equal,NULL,convoy_steer_spans,1},
    {ES2_HOOK_ID(convoy_seek),convoy_seek_sig,9,6,convoy_frame_lens,3,4,convoy_convoy_seek32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(convoy_lead),convoy_lead_sig,9,5,convoy_push_lens,4,1,convoy_convoy_lead32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(convoy_follow),convoy_follow_sig,9,6,convoy_frame_lens,3,1,convoy_convoy_follow32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(convoy_drive),convoy_drive_sig,9,6,convoy_frame_lens,3,1,convoy_convoy_drive32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(convoy_think),convoy_think_sig,9,6,convoy_frame_lens,3,1,convoy_convoy_think32,NULL,NULL,NULL,0},
};
#endif
