#ifndef ES2_HIT_TEST_HOOKS_H
#define ES2_HIT_TEST_HOOKS_H
/* Actor hit tests of vtable slot +20h
 * (docs/reverse-engineering/hit_tests.md, the
 * section "hit tests"): base_hit_test 405038, mech_hit_test 418ba8,
 * flyer_hit_test 421c8c. Core in core/src/hit_test.c. Every game callee
 * goes through the weapons guest bridge and callee observer
 * (weapons_graph) with the original's registers, so the nested hooks
 * (object_query, dist_gate, euler_matrix, matrix_compose, col_query,
 * transform_point, shield_geometry, pool_alloc, rng_next,
 * explosion_create, debris_table, debris_burst and the slot +74h hooks
 * building_part/vehicle_part) keep their modes; the slot +10h leaf 46b910,
 * the FLYER part damage 421bb4, the message post 420a68, the MECH part hit
 * 4188c8 and the runtime frame setup 49630c run as original code (4188c8
 * until its own hook exists). The record of 49630c lies in the adapter's
 * zeroed scratch (synthetic EBP = scratch + ES2_HIT_TEST_FRAME, through
 * weapons_impact_guest_call); the adapters restore FS:0 to their entry
 * head, which is the record's saved head, as the originals' epilogues do.
 * ECX after the hooks is dead at the sweep (hit_tests.md, "Dispatch site and packet"). All
 * three refuse compare mode. No entry shim, no game instruction
 * embedded. */
#include "es2/core/hit_test.h"
#define HIT_TEST_FRAME_GRAPH(index) \
    BYTE scratch[ES2_HIT_TEST_SCRATCH]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_##index); \
    weapons_impact_context c={(uint32_t)(uintptr_t)u,(uint32_t)(uintptr_t)(scratch+ES2_HIT_TEST_FRAME)}; \
    g.call=weapons_impact_guest_call;g.context=&c; \
    uint32_t head;__asm__ volatile("movl %%fs:0,%0":"=r"(head))
#define HIT_TEST_FRAME_RESTORE() __asm__ volatile("movl %0,%%fs:0"::"r"(head):"memory")
static uint64_t __cdecl hit_test_base_hit_test(const uint32_t *a,void *u,const uint32_t *r) {
    HIT_TEST_FRAME_GRAPH(base_hit_test);
    uint64_t result=es2_base_hit_test_registers(&math_native_memory,&g,a[0],a[1],r);
    HIT_TEST_FRAME_RESTORE();return result;
}
static uint64_t __cdecl hit_test_mech_hit_test(const uint32_t *a,void *u,const uint32_t *r) {
    HIT_TEST_FRAME_GRAPH(mech_hit_test);
    uint64_t result=es2_mech_hit_test_registers(&math_native_memory,&g,a[0],a[1],r);
    HIT_TEST_FRAME_RESTORE();return result;
}
static uint64_t __cdecl hit_test_flyer_hit_test(const uint32_t *a,void *u,const uint32_t *r) {
    HIT_TEST_FRAME_GRAPH(flyer_hit_test);
    uint64_t result=es2_flyer_hit_test_registers(&math_native_memory,&g,a[0],a[1],r);
    HIT_TEST_FRAME_RESTORE();return result;
}
#undef HIT_TEST_FRAME_GRAPH
#undef HIT_TEST_FRAME_RESTORE
/* Callee counters (hk_<hook>_<suffix>), incremented only while the hook is
 * non-original. Keys: the callee VA, or the vtable offset for slots. */
static callee_counter base_hit_test_counters[]={
    {"call_49630c",ES2_COLLIDE_CALL,0x49630c,0},
    {"call_427da8",ES2_COLLIDE_CALL,0x427da8,0},
    {"slot_10",ES2_COLLIDE_SLOT,0x10,0},
    {"call_49288c",ES2_COLLIDE_CALL,0x49288c,0},
    {"call_47ebb8",ES2_COLLIDE_CALL,0x47ebb8,0},
    {"call_47fa20",ES2_COLLIDE_CALL,0x47fa20,0},
    {"call_40c9d4",ES2_COLLIDE_CALL,0x40c9d4,0},
    {"call_48043c",ES2_COLLIDE_CALL,0x48043c,0},
    {"slot_74",ES2_COLLIDE_SLOT,0x74,0},
    {"call_471b30",ES2_COLLIDE_CALL,0x471b30,0},
    {"call_492ee0",ES2_COLLIDE_CALL,0x492ee0,0},
    {"call_407f1c",ES2_COLLIDE_CALL,0x407f1c,0},
    {"call_4089bc",ES2_COLLIDE_CALL,0x4089bc,0},
    {"call_408530",ES2_COLLIDE_CALL,0x408530,0},
};
static callee_counter mech_hit_test_counters[]={
    {"call_49630c",ES2_COLLIDE_CALL,0x49630c,0},
    {"call_49288c",ES2_COLLIDE_CALL,0x49288c,0},
    {"call_47ebb8",ES2_COLLIDE_CALL,0x47ebb8,0},
    {"call_47fa20",ES2_COLLIDE_CALL,0x47fa20,0},
    {"call_413cc4",ES2_COLLIDE_CALL,0x413cc4,0},
    {"call_48043c",ES2_COLLIDE_CALL,0x48043c,0},
    {"call_471b30",ES2_COLLIDE_CALL,0x471b30,0},
    {"call_492ee0",ES2_COLLIDE_CALL,0x492ee0,0},
    {"call_407f1c",ES2_COLLIDE_CALL,0x407f1c,0},
    {"call_420a68",ES2_COLLIDE_CALL,0x420a68,0},
    {"call_40c9d4",ES2_COLLIDE_CALL,0x40c9d4,0},
    {"call_4188c8",ES2_COLLIDE_CALL,0x4188c8,0},
};
static callee_counter flyer_hit_test_counters[]={
    {"call_49630c",ES2_COLLIDE_CALL,0x49630c,0},
    {"slot_10",ES2_COLLIDE_SLOT,0x10,0},
    {"call_49288c",ES2_COLLIDE_CALL,0x49288c,0},
    {"call_47ebb8",ES2_COLLIDE_CALL,0x47ebb8,0},
    {"call_47fa20",ES2_COLLIDE_CALL,0x47fa20,0},
    {"call_40c9d4",ES2_COLLIDE_CALL,0x40c9d4,0},
    {"call_492ee0",ES2_COLLIDE_CALL,0x492ee0,0},
    {"slot_74",ES2_COLLIDE_SLOT,0x74,0},
    {"call_48043c",ES2_COLLIDE_CALL,0x48043c,0},
    {"call_471b30",ES2_COLLIDE_CALL,0x471b30,0},
    {"call_407f1c",ES2_COLLIDE_CALL,0x407f1c,0},
    {"call_4089bc",ES2_COLLIDE_CALL,0x4089bc,0},
    {"call_408530",ES2_COLLIDE_CALL,0x408530,0},
};
static callee_counter *hit_test_hook_counters(unsigned hook,unsigned *count) {
#define HIT_TEST_COUNTERS(name) if(hook==ES2_HOOK_INDEX_##name) { \
        *count=sizeof(name##_counters)/sizeof(name##_counters[0]);return name##_counters; }
    HIT_TEST_COUNTERS(base_hit_test)
    HIT_TEST_COUNTERS(mech_hit_test)
    HIT_TEST_COUNTERS(flyer_hit_test)
#undef HIT_TEST_COUNTERS
    *count=0;return NULL;
}
/* push ebp; mov ebp,esp; add esp,-80h; mov eax,4974e6h */
static const BYTE base_hit_test_sig[]={0x55,0x8b,0xec,0x83,0xc4,0x80,0xb8,0xe6,0x74,0x49,0x00};
/* push ebp; mov ebp,esp; add esp,-88h; push ebx; push esi; push edi; mov edi,[ebp+0Ch] */
static const BYTE mech_hit_test_sig[]={0x55,0x8b,0xec,0x81,0xc4,0x78,0xff,0xff,0xff,0x53,0x56,0x57,0x8b,0x7d,0x0c};
/* push ebp; mov ebp,esp; add esp,-74h; mov eax,49a4cch */
static const BYTE flyer_hit_test_sig[]={0x55,0x8b,0xec,0x83,0xc4,0x8c,0xb8,0xcc,0xa4,0x49,0x00};
static const BYTE hit_test_frame_lens[]={1,2,3},hit_test_wide_frame_lens[]={1,2,6};
#define HIT_TEST_EXT(name) \
    static const es2_hook_extension hit_test_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,hit_test_##name,TRUE}; \
    static uint32_t __cdecl hit_test_##name##32(const uint32_t *a,void *u) {return (uint32_t)hit_test_##name(a,u,weapons_no_registers);}
HIT_TEST_EXT(base_hit_test)
HIT_TEST_EXT(mech_hit_test)
HIT_TEST_EXT(flyer_hit_test)
#undef HIT_TEST_EXT
static es2_hook_descriptor hit_test_hook_descriptors[]={
    {ES2_HOOK_ID(base_hit_test),base_hit_test_sig,11,6,hit_test_frame_lens,3,2,hit_test_base_hit_test32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(mech_hit_test),mech_hit_test_sig,15,9,hit_test_wide_frame_lens,3,2,hit_test_mech_hit_test32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(flyer_hit_test),flyer_hit_test_sig,11,6,hit_test_frame_lens,3,2,hit_test_flyer_hit_test32,NULL,NULL,NULL,0},
};
#endif
