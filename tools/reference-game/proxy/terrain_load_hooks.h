#ifndef ES2_TERRAIN_LOAD_HOOKS_H
#define ES2_TERRAIN_LOAD_HOOKS_H
/* terrain_load group (docs/reverse-engineering/terrain.md, section
 * "the terrain zone load"; mission_load.md section 24): the terrain
 * zone load of terrain.cpp. zone_load 42789c (from the world load 424308 at
 * 4252f7), grid_ctor 46befc (from zone_load, 4279ce), height_cache_ctor
 * 495268 (from grid_ctor, 46bf3f), terrain_materials 46c754 (from
 * zone_load, 427a8a), terrain_cells 46c4c4 (from terrain_materials,
 * 46c86f) and cell_mark_set 470d74 (from terrain_cells, 46c5e5, and the
 * base marks 47136c, 47146c; 470e20's call has no caller). All load-time
 * code: live only with the early arming (the default). Core in
 * core/src/terrain_load.c.
 *
 * Every game call goes through the weapons guest bridge and callee observer
 * (weapons_graph) with the original's EAX/EDX/ECX: the runtime record
 * 49630c (zone_load's and terrain_materials' synthetic EBP in a zeroed
 * scratch, through weapons_impact_guest_call; the adapters write the
 * record's saved head back to FS:0 as the epilogues do: zone_load's from
 * ECX, terrain_materials' from EDX), _itoa 496820, the path builder 492bec,
 * the stream constructor 472f14 and stream_destroy 472f64, the assertion
 * 479c78, the stream reads (slot +18h, stream_read 4735b8), new 47749c and
 * new[] 4774f0, 49667c with the element constructors 49545b/4953c4/4715e1,
 * euler_matrix 47ebb8, _strchr 4967a2, _stricmp 496868, dts_load 47a144,
 * 496406, the bitmap's slot +18h, _fopen 496802, _fscanf 4967b4, _fclose
 * 496838, rng_next 492ee0 on stream A 4d2599 (its draws keep their number
 * and order), the height-cache setter 4952e8 and reset 4952fc (H3), and the
 * six among themselves, so nested hooks keep their modes.
 *
 * EDX:EAX exact; ECX is dead after every call site (terrain.md), so it is
 * not returned (the framework returns the entry ECX). No entry or exit
 * shim. All six refuse compare (allocation, file reads, the runtime
 * record, the RNG stream, game calls; cell_mark_set writes a
 * runtime-indexed byte). No game instruction is embedded. */
#include "es2/core/terrain_load.h"
static uint64_t __cdecl terrain_load_zone_load(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[ES2_ZONE_LOAD_SCRATCH]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_zone_load);
    weapons_impact_context c={(uint32_t)(uintptr_t)u,(uint32_t)(uintptr_t)(scratch+ES2_ZONE_LOAD_FRAME)};
    g.call=weapons_impact_guest_call;g.context=&c;uint32_t head=0;
    const uint64_t result=es2_zone_load_registers(&math_native_memory,&g,a[0],r,&head);
    __asm__ volatile("movl %0,%%fs:0"::"r"(head):"memory");return result;
}
static uint64_t __cdecl terrain_load_grid_ctor(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_grid_ctor);
    return es2_grid_ctor_registers(&math_native_memory,&g,a[0],r,NULL);
}
static uint64_t __cdecl terrain_load_height_cache_ctor(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_height_cache_ctor);
    return es2_height_cache_ctor_registers(&math_native_memory,&g,a[0],a[1],r,NULL);
}
static uint64_t __cdecl terrain_load_terrain_materials(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[ES2_TERRAIN_MATERIALS_SCRATCH]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_terrain_materials);
    weapons_impact_context c={(uint32_t)(uintptr_t)u,(uint32_t)(uintptr_t)(scratch+ES2_TERRAIN_MATERIALS_FRAME)};
    g.call=weapons_impact_guest_call;g.context=&c;
    const uint64_t result=es2_terrain_materials_registers(&math_native_memory,&g,a[0],a[1],a[2],r,NULL);
    const uint32_t head=(uint32_t)(result>>32);__asm__ volatile("movl %0,%%fs:0"::"r"(head):"memory");return result;
}
static uint64_t __cdecl terrain_load_terrain_cells(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_terrain_cells);
    return es2_terrain_cells_registers(&math_native_memory,&g,a[0],a[1],a[2],r,NULL);
}
static uint64_t __cdecl terrain_load_cell_mark_set(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;return es2_cell_mark_set_registers(&math_native_memory,a[0],a[1],a[2],r,NULL);
}
/* Callee counters (hk_<hook>_<suffix>), incremented only while the hook is
 * non-original. Keys: the callee VA, or the slot +18h (terrain_materials:
 * the stream read and the bitmap's deleting destructor share it). */
static callee_counter zone_load_counters[]={
    {"call_49630c",ES2_COLLIDE_CALL,0x49630c,0},
    {"call_496820",ES2_COLLIDE_CALL,0x496820,0},
    {"call_492bec",ES2_COLLIDE_CALL,0x492bec,0},
    {"call_472f14",ES2_COLLIDE_CALL,0x472f14,0},
    {"call_479c78",ES2_COLLIDE_CALL,0x479c78,0},
    {"slot_18",ES2_COLLIDE_SLOT,0x18,0},
    {"call_47749c",ES2_COLLIDE_CALL,0x47749c,0},
    {"call_46befc",ES2_COLLIDE_CALL,0x46befc,0},
    {"call_46c754",ES2_COLLIDE_CALL,0x46c754,0},
    {"call_47ebb8",ES2_COLLIDE_CALL,0x47ebb8,0},
    {"call_472f64",ES2_COLLIDE_CALL,0x472f64,0},
};
static callee_counter grid_ctor_counters[]={
    {"call_49667c",ES2_COLLIDE_CALL,0x49667c,0},
    {"call_495268",ES2_COLLIDE_CALL,0x495268,0},
};
static callee_counter height_cache_ctor_counters[]={
    {"call_4774f0",ES2_COLLIDE_CALL,0x4774f0,0},
    {"call_49667c",ES2_COLLIDE_CALL,0x49667c,0},
};
static callee_counter terrain_materials_counters[]={
    {"call_49630c",ES2_COLLIDE_CALL,0x49630c,0},
    {"call_492bec",ES2_COLLIDE_CALL,0x492bec,0},
    {"call_472f14",ES2_COLLIDE_CALL,0x472f14,0},
    {"slot_18",ES2_COLLIDE_SLOT,0x18,0},
    {"call_4774f0",ES2_COLLIDE_CALL,0x4774f0,0},
    {"call_4735b8",ES2_COLLIDE_CALL,0x4735b8,0},
    {"call_4967a2",ES2_COLLIDE_CALL,0x4967a2,0},
    {"call_496868",ES2_COLLIDE_CALL,0x496868,0},
    {"call_47a144",ES2_COLLIDE_CALL,0x47a144,0},
    {"call_46c4c4",ES2_COLLIDE_CALL,0x46c4c4,0},
    {"call_496406",ES2_COLLIDE_CALL,0x496406,0},
    {"call_472f64",ES2_COLLIDE_CALL,0x472f64,0},
    {"call_496802",ES2_COLLIDE_CALL,0x496802,0},
    {"call_4967b4",ES2_COLLIDE_CALL,0x4967b4,0},
    {"call_49667c",ES2_COLLIDE_CALL,0x49667c,0},
    {"call_492ee0",ES2_COLLIDE_CALL,0x492ee0,0},
    {"call_496838",ES2_COLLIDE_CALL,0x496838,0},
    {"call_4952e8",ES2_COLLIDE_CALL,0x4952e8,0},
    {"call_4952fc",ES2_COLLIDE_CALL,0x4952fc,0},
};
static callee_counter terrain_cells_counters[]={
    {"call_49667c",ES2_COLLIDE_CALL,0x49667c,0},
    {"call_4774f0",ES2_COLLIDE_CALL,0x4774f0,0},
    {"call_470d74",ES2_COLLIDE_CALL,0x470d74,0},
    {"call_492ee0",ES2_COLLIDE_CALL,0x492ee0,0},
    {"call_4952e8",ES2_COLLIDE_CALL,0x4952e8,0},
    {"call_4952fc",ES2_COLLIDE_CALL,0x4952fc,0},
};
#define TERRAIN_LOAD_COUNTERS(name) \
    if(hook==ES2_HOOK_INDEX_##name){*count=sizeof(name##_counters)/sizeof(name##_counters[0]);return name##_counters;}
static callee_counter *terrain_load_hook_counters(unsigned hook,unsigned *count) {
    TERRAIN_LOAD_COUNTERS(zone_load)
    TERRAIN_LOAD_COUNTERS(grid_ctor)
    TERRAIN_LOAD_COUNTERS(height_cache_ctor)
    TERRAIN_LOAD_COUNTERS(terrain_materials)
    TERRAIN_LOAD_COUNTERS(terrain_cells)
    *count=0;return NULL;
}
#undef TERRAIN_LOAD_COUNTERS
/* Verified entry bytes (reference DBSIM.EXE).
 * 42789c: push ebp; mov ebp,esp; add esp,-94h; push ebx; push esi; mov
 * ebx,[ebp+8]. Relocated 9 bytes (1,2,6).
 * 46c754: push ebp; mov ebp,esp; add esp,-0C8h; mov eax,4A0C14h; push ebx.
 * Relocated 9 bytes (1,2,6); the descriptor load and the call of 49630c
 * that follow stay in place.
 * 46befc and 495268: push ebp; mov ebp,esp; push ebx; mov ebx,[ebp+8]; mov
 * dword [ebx],4A69F0h. Relocated 7 bytes (1,2,1,3).
 * 46c4c4: push ebp; mov ebp,esp; add esp,-10h; xor edx,edx; mov eax,1.
 * Relocated 6 bytes (1,2,3).
 * 470d74: push ebp; mov ebp,esp; mov eax,[ebp+8]; mov edx,[ebp+0Ch]; mov
 * cl,[ebp+10h]. Relocated 6 bytes (1,2,3). */
static const BYTE zone_load_sig[]={0x55,0x8b,0xec,0x81,0xc4,0x6c,0xff,0xff,0xff,0x53,0x56,0x8b,0x5d,0x08};
static const BYTE grid_ctor_sig[]={0x55,0x8b,0xec,0x53,0x8b,0x5d,0x08,0xc7,0x03,0xf0,0x69,0x4a,0x00};
static const BYTE height_cache_ctor_sig[]={0x55,0x8b,0xec,0x53,0x8b,0x5d,0x08,0xc7,0x03,0xf0,0x69,0x4a,0x00};
static const BYTE terrain_materials_sig[]={0x55,0x8b,0xec,0x81,0xc4,0x38,0xff,0xff,0xff,0xb8,0x14,0x0c,0x4a,0x00,0x53};
static const BYTE terrain_cells_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xf0,0x33,0xd2,0xb8,0x01,0x00,0x00,0x00};
static const BYTE cell_mark_set_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x8b,0x55,0x0c,0x8a,0x4d,0x10};
static const BYTE terrain_load_frame32_lens[]={1,2,6},terrain_load_ctor_lens[]={1,2,1,3},terrain_load_frame8_lens[]={1,2,3};
#define TERRAIN_LOAD_EXT(name) \
    static const es2_hook_extension terrain_load_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,terrain_load_##name,TRUE}; \
    static uint32_t __cdecl terrain_load_##name##32(const uint32_t *a,void *u) {return (uint32_t)terrain_load_##name(a,u,weapons_no_registers);}
TERRAIN_LOAD_EXT(zone_load)
TERRAIN_LOAD_EXT(grid_ctor)
TERRAIN_LOAD_EXT(height_cache_ctor)
TERRAIN_LOAD_EXT(terrain_materials)
TERRAIN_LOAD_EXT(terrain_cells)
TERRAIN_LOAD_EXT(cell_mark_set)
#undef TERRAIN_LOAD_EXT
static es2_hook_descriptor terrain_load_hook_descriptors[]={
    {ES2_HOOK_ID(zone_load),zone_load_sig,14,9,terrain_load_frame32_lens,3,1,terrain_load_zone_load32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(grid_ctor),grid_ctor_sig,13,7,terrain_load_ctor_lens,4,1,terrain_load_grid_ctor32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(height_cache_ctor),height_cache_ctor_sig,13,7,terrain_load_ctor_lens,4,2,terrain_load_height_cache_ctor32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(terrain_materials),terrain_materials_sig,15,9,terrain_load_frame32_lens,3,3,terrain_load_terrain_materials32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(terrain_cells),terrain_cells_sig,13,6,terrain_load_frame8_lens,3,3,terrain_load_terrain_cells32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(cell_mark_set),cell_mark_set_sig,12,6,terrain_load_frame8_lens,3,3,terrain_load_cell_mark_set32,NULL,NULL,NULL,0},
};
#endif
