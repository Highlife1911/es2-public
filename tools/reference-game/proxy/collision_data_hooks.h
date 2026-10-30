#ifndef ES2_COLLISION_DATA_HOOKS_H
#define ES2_COLLISION_DATA_HOOKS_H
/* collision_data group (docs/reverse-engineering/mission_load.md,
 * section "collision data"): the collision data of collide.cpp.
 * The reader chain of a type's COL resource collision_load 40cd88 (from
 * mech_ctor 415fb5 and the FLYER type load 422ed0 at 422f69),
 * collision_read 40ccf8 (also the building module's stage-2 callback
 * 405fac at 40659a, BASECOL), collision_entry 40cc50, collision_volume
 * 40cc14 (also the weapon-list entry 40f8bc at 40f8d7) and
 * collision_points 40c7c4; the bounding spheres collision_bounds 40ccc8 and
 * collision_radius 40c5d0; the part-list scan of weapon_fit (41778f)
 * collision_scan 40cec0 with collision_match 40ce78. All load-time code:
 * live only with the early arming (the default).
 * Core in core/src/collision_data.c.
 *
 * Every game call goes through the weapons guest bridge and callee
 * observer (weapons_graph) with the original's EAX/EDX/ECX: the stream
 * reads (the stream object's slot +18h, stream_read 4735b8 for the game's
 * streams), new 4774f0, new[] 49667c with the element constructors
 * 40c8be/40cf04, the runtime record 49630c (collision_load's synthetic EBP
 * in a zeroed scratch, through weapons_impact_guest_call; the adapter
 * writes the record's saved head, EDX, back to FS:0 as the epilogue does),
 * the stream constructor 472f14 and stream_destroy 472f64, the assertion
 * 479c78, dist3d_delta 47de72, the original volume match 40ce28 and each
 * other of the nine, so nested hooks keep their modes.
 *
 * EDX:EAX exact; ECX is dead after every call site (mission_load.md), so it
 * is not returned (the framework returns the entry ECX). No entry or exit
 * shim. collision_radius accepts compare: it writes only the six centre
 * bytes of its second argument (one span) and calls the pure dist3d_delta;
 * the other eight refuse it (streams, allocation, the slot counter, the
 * runtime record, game calls that write the volumes). No game instruction
 * is embedded. */
#include "es2/core/collision_data.h"
static uint64_t __cdecl collision_data_collision_load(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[ES2_COLLISION_LOAD_SCRATCH]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_collision_load);
    weapons_impact_context c={(uint32_t)(uintptr_t)u,(uint32_t)(uintptr_t)(scratch+ES2_COLLISION_LOAD_FRAME)};
    g.call=weapons_impact_guest_call;g.context=&c;
    const uint64_t result=es2_collision_load_registers(&math_native_memory,&g,a[0],r,NULL);
    const uint32_t head=(uint32_t)(result>>32);es2_machine_set_fs0(head);return result;
}
#define COLLISION_DATA_TWO(name) \
static uint64_t __cdecl collision_data_##name(const uint32_t *a,void *u,const uint32_t *r) { \
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_##name); \
    return es2_##name##_registers(&math_native_memory,&g,a[0],a[1],r,NULL); }
COLLISION_DATA_TWO(collision_read)
COLLISION_DATA_TWO(collision_entry)
COLLISION_DATA_TWO(collision_volume)
COLLISION_DATA_TWO(collision_points)
COLLISION_DATA_TWO(collision_radius)
#undef COLLISION_DATA_TWO
static uint64_t __cdecl collision_data_collision_bounds(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_collision_bounds);
    return es2_collision_bounds_registers(&math_native_memory,&g,a[0],r,NULL);
}
static uint64_t __cdecl collision_data_collision_scan(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_collision_scan);
    return es2_collision_scan_registers(&math_native_memory,&g,a[0],a[1],a[2],a[3],r,NULL);
}
static uint64_t __cdecl collision_data_collision_match(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_collision_match);
    return es2_collision_match_registers(&math_native_memory,&g,a[0],a[1],a[2],a[3],r,NULL);
}
/* Callee counters (hk_<hook>_<suffix>), incremented only while the hook is
 * non-original. Keys: the callee VA, or the stream's slot +18h. */
static callee_counter collision_load_counters[]={
    {"call_49630c",ES2_COLLIDE_CALL,0x49630c,0},
    {"call_472f14",ES2_COLLIDE_CALL,0x472f14,0},
    {"call_479c78",ES2_COLLIDE_CALL,0x479c78,0},
    {"call_40ccf8",ES2_COLLIDE_CALL,0x40ccf8,0},
    {"call_472f64",ES2_COLLIDE_CALL,0x472f64,0},
};
static callee_counter collision_read_counters[]={
    {"call_479c78",ES2_COLLIDE_CALL,0x479c78,0},
    {"slot_18",ES2_COLLIDE_SLOT,0x18,0},
    {"call_4774f0",ES2_COLLIDE_CALL,0x4774f0,0},
    {"call_40cc50",ES2_COLLIDE_CALL,0x40cc50,0},
    {"call_40ccc8",ES2_COLLIDE_CALL,0x40ccc8,0},
};
static callee_counter collision_entry_counters[]={
    {"slot_18",ES2_COLLIDE_SLOT,0x18,0},
    {"call_49667c",ES2_COLLIDE_CALL,0x49667c,0},
    {"call_40cc14",ES2_COLLIDE_CALL,0x40cc14,0},
};
static callee_counter collision_volume_counters[]={
    {"slot_18",ES2_COLLIDE_SLOT,0x18,0},
    {"call_40c7c4",ES2_COLLIDE_CALL,0x40c7c4,0},
};
static callee_counter collision_points_counters[]={
    {"slot_18",ES2_COLLIDE_SLOT,0x18,0},
    {"call_49667c",ES2_COLLIDE_CALL,0x49667c,0},
};
static callee_counter collision_bounds_counters[]={{"call_40c5d0",ES2_COLLIDE_CALL,0x40c5d0,0}};
static callee_counter collision_radius_counters[]={{"call_47de72",ES2_COLLIDE_CALL,0x47de72,0}};
static callee_counter collision_scan_counters[]={
    {"call_40ce78",ES2_COLLIDE_CALL,0x40ce78,0},
    {"call_40ccc8",ES2_COLLIDE_CALL,0x40ccc8,0},
};
static callee_counter collision_match_counters[]={{"call_40ce28",ES2_COLLIDE_CALL,0x40ce28,0}};
#define COLLISION_DATA_COUNTERS(name) \
    if(hook==ES2_HOOK_INDEX_##name){*count=sizeof(name##_counters)/sizeof(name##_counters[0]);return name##_counters;}
static callee_counter *collision_data_hook_counters(unsigned hook,unsigned *count) {
    COLLISION_DATA_COUNTERS(collision_load)
    COLLISION_DATA_COUNTERS(collision_read)
    COLLISION_DATA_COUNTERS(collision_entry)
    COLLISION_DATA_COUNTERS(collision_volume)
    COLLISION_DATA_COUNTERS(collision_points)
    COLLISION_DATA_COUNTERS(collision_bounds)
    COLLISION_DATA_COUNTERS(collision_radius)
    COLLISION_DATA_COUNTERS(collision_scan)
    COLLISION_DATA_COUNTERS(collision_match)
    *count=0;return NULL;
}
#undef COLLISION_DATA_COUNTERS
/* Verified entry bytes (reference DBSIM.EXE).
 * 40cd88: push ebp; mov ebp,esp; add esp,-50h; mov eax,4987F0h; push ebx.
 * Relocated 6 bytes (1,2,3); the descriptor load and the call of 49630c
 * that follow stay in place.
 * 40ccf8: push ebp; mov ebp,esp; push ebx; push esi; push edi; mov esi,[ebp+8]
 * 40cc50: push ebp; mov ebp,esp; push ebx; push esi; push edi; mov ebx,[ebp+8]; mov eax,ebx
 * 40cc14: push ebp; mov ebp,esp; push ebx; push esi; push edi; mov ebx,[ebp+8]; lea edi,[ebx+6]
 * 40c7c4: push ebp; mov ebp,esp; push ebx; push esi; push edi; mov ebx,[ebp+8]; mov edi,ebx
 * 40ccc8: push ebp; mov ebp,esp; push ebx; push esi; push edi; mov edi,[ebp+8]; mov ebx,[edi+6]
 * 40cec0: push ebp; mov ebp,esp; push ebx; push esi; push edi; xor esi,esi; mov edi,[ebp+8]
 * 40ce78: push ebp; mov ebp,esp; push ebx; push esi; push edi; xor ebx,ebx; mov edi,[ebp+8]
 * Relocated 5 bytes (1,2,1,1).
 * 40c5d0: push ebp; mov ebp,esp; add esp,-10h; push ebx; push esi; push
 * edi; mov esi,[ebp+8]. Relocated 6 bytes (1,2,3). */
static const BYTE collision_load_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xb0,0xb8,0xf0,0x87,0x49,0x00,0x53};
static const BYTE collision_read_sig[]={0x55,0x8b,0xec,0x53,0x56,0x57,0x8b,0x75,0x08,0x8b,0x45,0x0c};
static const BYTE collision_entry_sig[]={0x55,0x8b,0xec,0x53,0x56,0x57,0x8b,0x5d,0x08,0x8b,0xc3};
static const BYTE collision_volume_sig[]={0x55,0x8b,0xec,0x53,0x56,0x57,0x8b,0x5d,0x08,0x8d,0x7b,0x06};
static const BYTE collision_points_sig[]={0x55,0x8b,0xec,0x53,0x56,0x57,0x8b,0x5d,0x08,0x8b,0xfb};
static const BYTE collision_bounds_sig[]={0x55,0x8b,0xec,0x53,0x56,0x57,0x8b,0x7d,0x08,0x8b,0x5f,0x06};
static const BYTE collision_radius_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xf0,0x53,0x56,0x57,0x8b,0x75,0x08};
static const BYTE collision_scan_sig[]={0x55,0x8b,0xec,0x53,0x56,0x57,0x33,0xf6,0x8b,0x7d,0x08};
static const BYTE collision_match_sig[]={0x55,0x8b,0xec,0x53,0x56,0x57,0x33,0xdb,0x8b,0x7d,0x08};
static const BYTE collision_data_frame_lens[]={1,2,3},collision_data_saved_lens[]={1,2,1,1};
/* Compare: collision_radius declares the six centre bytes of argument 1. */
static const es2_hook_span collision_data_radius_span[]={{1,0,6}};
#define COLLISION_DATA_EXT(name,unsupported) \
    static const es2_hook_extension collision_data_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,collision_data_##name,unsupported}; \
    static uint32_t __cdecl collision_data_##name##32(const uint32_t *a,void *u) {return (uint32_t)collision_data_##name(a,u,weapons_no_registers);}
COLLISION_DATA_EXT(collision_load,TRUE)
COLLISION_DATA_EXT(collision_read,TRUE)
COLLISION_DATA_EXT(collision_entry,TRUE)
COLLISION_DATA_EXT(collision_volume,TRUE)
COLLISION_DATA_EXT(collision_points,TRUE)
COLLISION_DATA_EXT(collision_bounds,TRUE)
COLLISION_DATA_EXT(collision_radius,FALSE)
COLLISION_DATA_EXT(collision_scan,TRUE)
COLLISION_DATA_EXT(collision_match,TRUE)
#undef COLLISION_DATA_EXT
static es2_hook_descriptor collision_data_hook_descriptors[]={
    {ES2_HOOK_ID(collision_load),collision_load_sig,12,6,collision_data_frame_lens,3,1,collision_data_collision_load32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(collision_read),collision_read_sig,12,5,collision_data_saved_lens,4,2,collision_data_collision_read32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(collision_entry),collision_entry_sig,11,5,collision_data_saved_lens,4,2,collision_data_collision_entry32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(collision_volume),collision_volume_sig,12,5,collision_data_saved_lens,4,2,collision_data_collision_volume32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(collision_points),collision_points_sig,11,5,collision_data_saved_lens,4,2,collision_data_collision_points32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(collision_bounds),collision_bounds_sig,12,5,collision_data_saved_lens,4,1,collision_data_collision_bounds32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(collision_radius),collision_radius_sig,12,6,collision_data_frame_lens,3,2,collision_data_collision_radius32,
        NULL,NULL,collision_data_radius_span,1},
    {ES2_HOOK_ID(collision_scan),collision_scan_sig,11,5,collision_data_saved_lens,4,4,collision_data_collision_scan32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(collision_match),collision_match_sig,11,5,collision_data_saved_lens,4,4,collision_data_collision_match32,NULL,NULL,NULL,0},
};
#endif
