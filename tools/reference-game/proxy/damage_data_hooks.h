#ifndef ES2_DAMAGE_DATA_HOOKS_H
#define ES2_DAMAGE_DATA_HOOKS_H
/* damage_data group (docs/reverse-engineering/mission_load.md,
 * section "damage data and part tables"): the damage data of
 * damage.cpp and the part tables of the mission load, dmg_load 40d160,
 * dmg_read 40d09c, dmg_region_read 40cff8, dmg_region_copy 40d050,
 * dmg_capacity_set 40d128, dmg_region_set 40d240, dmg_part_store 40d284,
 * col_part_collect 40cb84, col_part_gather 40cafc, damage_state_ctor
 * 40d2cc and damage_state_bind 40d354 (core/src/damage.c). They run during
 * the load only (the constructors, weapon_fit, the FLYER type load and the
 * weapon list), so they are live only with the early arming (the
 * default).
 *
 * Every game call goes through the weapons guest bridge and callee observer
 * (weapons_graph) with the original's EAX/EDX/ECX. dmg_load and
 * damage_state_bind set up the runtime record through 49630c with the
 * synthetic EBP (the frame in a zeroed scratch, the locals at the
 * original's offsets: dmg_load's stream object at f-30h is passed to its
 * callees) through weapons_impact_guest_call, and the adapter writes the
 * record's saved head (EDX) back to FS:0 as the original's epilogue does.
 * The callees stay game code with their own modes: the runtime (49630c,
 * new 47749c, new[] 4774f0, _memcpy 49680e, _memset 49681a), the stream
 * constructor 472f14, the error report 479c78, the stream's read method
 * (slot +18h), the hook stream_destroy and the hooks of this group where
 * the original calls them (dmg_read, dmg_region_read, dmg_region_copy,
 * col_part_gather).
 *
 * EDX:EAX exact; ECX is dead at every call site (mission_load.md lists
 * them), so it is not returned (the framework returns the entry ECX). No
 * entry or exit shim: no read of an uninitialized local or of stack
 * residue; col_part_gather and dmg_part_store advance their own argument
 * slots, which no caller reads back. All eleven refuse compare mode
 * (allocation, streams, the runtime record, writes through runtime-indexed
 * pointers and count-dependent arrays); the load is accepted by the
 * after-load snapshot. No game instruction is embedded. */
#include "es2/core/damage.h"
static uint64_t __cdecl damage_data_dmg_load(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[ES2_DMG_LOAD_SCRATCH]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_dmg_load);
    weapons_impact_context c={(uint32_t)(uintptr_t)u,(uint32_t)(uintptr_t)(scratch+ES2_DMG_LOAD_FRAME)};
    g.call=weapons_impact_guest_call;g.context=&c;
    uint64_t result=es2_dmg_load_registers(&math_native_memory,&g,a[0],r,NULL);
    uint32_t head=(uint32_t)(result>>32);es2_machine_set_fs0(head);return result;
}
static uint64_t __cdecl damage_data_damage_state_bind(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[ES2_DAMAGE_BIND_SCRATCH]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_damage_state_bind);
    weapons_impact_context c={(uint32_t)(uintptr_t)u,(uint32_t)(uintptr_t)(scratch+ES2_DAMAGE_BIND_FRAME)};
    g.call=weapons_impact_guest_call;g.context=&c;
    uint64_t result=es2_damage_state_bind_registers(&math_native_memory,&g,a[0],a[1],a[2],a[3],r,NULL);
    uint32_t head=(uint32_t)(result>>32);es2_machine_set_fs0(head);return result;
}
static uint64_t __cdecl damage_data_dmg_read(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_dmg_read);
    return es2_dmg_read_registers(&math_native_memory,&g,a[0],a[1],r,NULL);
}
static uint64_t __cdecl damage_data_dmg_region_read(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_dmg_region_read);
    return es2_dmg_region_read_registers(&math_native_memory,&g,a[0],a[1],r,NULL);
}
static uint64_t __cdecl damage_data_dmg_region_copy(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_dmg_region_copy);
    return es2_dmg_region_copy_registers(&math_native_memory,&g,a[0],a[1],r,NULL);
}
static uint64_t __cdecl damage_data_dmg_region_set(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_dmg_region_set);
    return es2_dmg_region_set_registers(&math_native_memory,&g,a[0],a[1],a[2],a[3],r,NULL);
}
static uint64_t __cdecl damage_data_col_part_collect(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_col_part_collect);
    return es2_col_part_collect_registers(&math_native_memory,&g,a[0],a[1],a[2],a[3],r,NULL);
}
static uint64_t __cdecl damage_data_damage_state_ctor(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_damage_state_ctor);
    return es2_damage_state_ctor_registers(&math_native_memory,&g,a[0],a[1],a[2],r,NULL);
}
static uint64_t __cdecl damage_data_dmg_capacity_set(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;return es2_dmg_capacity_set_registers(&math_native_memory,a[0],a[1],a[2],a[3],r,NULL);
}
static uint64_t __cdecl damage_data_dmg_part_store(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;return es2_dmg_part_store_registers(&math_native_memory,a[0],a[1],a[2],a[3],a[4],r,NULL);
}
static uint64_t __cdecl damage_data_col_part_gather(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;return es2_col_part_gather_registers(&math_native_memory,a[0],a[1],a[2],a[3],r,NULL);
}
/* Callee counters (hk_<hook>_<suffix>), incremented only while the hook is
 * non-original. Keys: the callee VA, or the virtual slot. */
static callee_counter dmg_load_counters[]={
    {"call_49630c",ES2_COLLIDE_CALL,0x49630c,0},
    {"call_472f14",ES2_COLLIDE_CALL,0x472f14,0},
    {"call_479c78",ES2_COLLIDE_CALL,0x479c78,0},
    {"call_40d09c",ES2_COLLIDE_CALL,0x40d09c,0},
    {"call_472f64",ES2_COLLIDE_CALL,0x472f64,0},
};
static callee_counter dmg_read_counters[]={
    {"slot_18",ES2_COLLIDE_SLOT,0x18,0},
    {"call_4774f0",ES2_COLLIDE_CALL,0x4774f0,0},
    {"call_40cff8",ES2_COLLIDE_CALL,0x40cff8,0},
};
static callee_counter dmg_region_read_counters[]={
    {"slot_18",ES2_COLLIDE_SLOT,0x18,0},
    {"call_4774f0",ES2_COLLIDE_CALL,0x4774f0,0},
};
static callee_counter dmg_region_copy_counters[]={
    {"call_49680e",ES2_COLLIDE_CALL,0x49680e,0},
    {"call_4774f0",ES2_COLLIDE_CALL,0x4774f0,0},
};
static callee_counter dmg_region_set_counters[]={
    {"call_40d050",ES2_COLLIDE_CALL,0x40d050,0},
};
static callee_counter col_part_collect_counters[]={
    {"call_40cafc",ES2_COLLIDE_CALL,0x40cafc,0},
};
static callee_counter damage_state_ctor_counters[]={
    {"call_4774f0",ES2_COLLIDE_CALL,0x4774f0,0},
    {"call_49681a",ES2_COLLIDE_CALL,0x49681a,0},
};
static callee_counter damage_state_bind_counters[]={
    {"call_49630c",ES2_COLLIDE_CALL,0x49630c,0},
    {"call_47749c",ES2_COLLIDE_CALL,0x47749c,0},
    {"call_4774f0",ES2_COLLIDE_CALL,0x4774f0,0},
};
#define DAMAGE_DATA_COUNTERS(name) \
    if(hook==ES2_HOOK_INDEX_##name){*count=sizeof(name##_counters)/sizeof(name##_counters[0]);return name##_counters;}
static callee_counter *damage_data_hook_counters(unsigned hook,unsigned *count) {
    DAMAGE_DATA_COUNTERS(dmg_load)
    DAMAGE_DATA_COUNTERS(dmg_read)
    DAMAGE_DATA_COUNTERS(dmg_region_read)
    DAMAGE_DATA_COUNTERS(dmg_region_copy)
    DAMAGE_DATA_COUNTERS(dmg_region_set)
    DAMAGE_DATA_COUNTERS(col_part_collect)
    DAMAGE_DATA_COUNTERS(damage_state_ctor)
    DAMAGE_DATA_COUNTERS(damage_state_bind)
    *count=0;return NULL;
}
#undef DAMAGE_DATA_COUNTERS
/* 40d160: push ebp; mov ebp,esp; add esp,-50h; mov eax,498840h; push ebx.
 * 40d354: push ebp; mov ebp,esp; add esp,-24h; mov eax,498878h; push ebx.
 * Relocated 6 bytes (1,2,3); the descriptor load and the call of 49630c
 * that follow stay in place.
 * 40d09c, 40cff8, 40d050, 40d128, 40d240, 40d2cc, 40cb84, 40cafc: push
 * ebp; mov ebp,esp; push ebx; push esi; push edi. 40d284: push ebp; mov
 * ebp,esp; push ebx; push esi; xor edx,edx. Relocated 5 bytes (1,2,1,1). */
static const BYTE dmg_load_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xb0,0xb8,0x40,0x88,0x49,0x00,0x53};
static const BYTE damage_state_bind_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xdc,0xb8,0x78,0x88,0x49,0x00,0x53};
static const BYTE dmg_read_sig[]={0x55,0x8b,0xec,0x53,0x56,0x57,0x8b,0x5d,0x08};
static const BYTE dmg_region_read_sig[]={0x55,0x8b,0xec,0x53,0x56,0x57,0x8b,0x75,0x0c};
static const BYTE dmg_region_copy_sig[]={0x55,0x8b,0xec,0x53,0x56,0x57,0x8b,0x75,0x0c};
static const BYTE dmg_capacity_set_sig[]={0x55,0x8b,0xec,0x53,0x56,0x57,0x8b,0x55,0x10};
static const BYTE dmg_region_set_sig[]={0x55,0x8b,0xec,0x53,0x56,0x57,0x8b,0x75,0x10};
static const BYTE dmg_part_store_sig[]={0x55,0x8b,0xec,0x53,0x56,0x33,0xd2};
static const BYTE col_part_collect_sig[]={0x55,0x8b,0xec,0x53,0x56,0x57,0x33,0xdb};
static const BYTE col_part_gather_sig[]={0x55,0x8b,0xec,0x53,0x56,0x57,0x8b,0x5d,0x10};
static const BYTE damage_state_ctor_sig[]={0x55,0x8b,0xec,0x53,0x56,0x57,0x8b,0x7d,0x10};
static const BYTE damage_data_frame_lens[]={1,2,3},damage_data_push_lens[]={1,2,1,1};
#define DAMAGE_DATA_EXT(name) \
    static const es2_hook_extension damage_data_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,damage_data_##name,TRUE}; \
    static uint32_t __cdecl damage_data_##name##32(const uint32_t *a,void *u) {return (uint32_t)damage_data_##name(a,u,weapons_no_registers);}
DAMAGE_DATA_EXT(dmg_load)
DAMAGE_DATA_EXT(dmg_read)
DAMAGE_DATA_EXT(dmg_region_read)
DAMAGE_DATA_EXT(dmg_region_copy)
DAMAGE_DATA_EXT(dmg_capacity_set)
DAMAGE_DATA_EXT(dmg_region_set)
DAMAGE_DATA_EXT(dmg_part_store)
DAMAGE_DATA_EXT(col_part_collect)
DAMAGE_DATA_EXT(col_part_gather)
DAMAGE_DATA_EXT(damage_state_ctor)
DAMAGE_DATA_EXT(damage_state_bind)
#undef DAMAGE_DATA_EXT
static es2_hook_descriptor damage_data_hook_descriptors[]={
    {ES2_HOOK_ID(dmg_load),dmg_load_sig,12,6,damage_data_frame_lens,3,1,damage_data_dmg_load32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(dmg_read),dmg_read_sig,9,5,damage_data_push_lens,4,2,damage_data_dmg_read32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(dmg_region_read),dmg_region_read_sig,9,5,damage_data_push_lens,4,2,damage_data_dmg_region_read32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(dmg_region_copy),dmg_region_copy_sig,9,5,damage_data_push_lens,4,2,damage_data_dmg_region_copy32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(dmg_capacity_set),dmg_capacity_set_sig,9,5,damage_data_push_lens,4,4,damage_data_dmg_capacity_set32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(dmg_region_set),dmg_region_set_sig,9,5,damage_data_push_lens,4,4,damage_data_dmg_region_set32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(dmg_part_store),dmg_part_store_sig,7,5,damage_data_push_lens,4,5,damage_data_dmg_part_store32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(col_part_collect),col_part_collect_sig,8,5,damage_data_push_lens,4,4,damage_data_col_part_collect32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(col_part_gather),col_part_gather_sig,9,5,damage_data_push_lens,4,4,damage_data_col_part_gather32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(damage_state_ctor),damage_state_ctor_sig,9,5,damage_data_push_lens,4,3,damage_data_damage_state_ctor32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(damage_state_bind),damage_state_bind_sig,12,6,damage_data_frame_lens,3,4,damage_data_damage_state_bind32,NULL,NULL,NULL,0},
};
#endif
