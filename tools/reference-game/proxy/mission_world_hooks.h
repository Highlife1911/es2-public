#ifndef ES2_MISSION_WORLD_HOOKS_H
#define ES2_MISSION_WORLD_HOOKS_H
/* mission_world group (docs/reverse-engineering/mission_load.md,
 * section "L7 world load"): the world load 424308, script pass 1 of
 * the mission load, called once per process from 4617c8, and the four
 * leaves only it calls: actor_list_init 411df8 (from 4252c6), gun_slot_table
 * 40fecc (4252ce), dmg_slot_table 40d218 (4252d3) and col_slot_table 40cbec
 * (4252d8). Load-time code: live only with the early arming (the
 * default). An own group because its in-vitro harness (a
 * synthetic mission.var, player.mec and script.dat behind stub reads, every
 * callee a logging stub) shares nothing with mission_actors'.
 *
 * Every game call goes through the weapons guest bridge and callee observer
 * (weapons_graph) with the original's EAX/EDX/ECX; 49630c gets the synthetic
 * EBP (the frame in a zeroed scratch, the locals and record buffers at the
 * original's offsets) through weapons_impact_guest_call, and the world
 * load's adapter writes the record's saved head back to FS:0 as the
 * original's epilogue does (ECX). The callees stay game code with their own
 * modes: memset 49681a, the path builders 45ee00/492b90, the streams
 * 47ad40/47af98/the read at slot +18h/47ad78 and the fatal report 479c78
 * (file I/O), the STR reader 453878/4539a0 and strlen 496766, the world
 * 42e010 (graphics), the base shapes 405ebc (assets_hooks.h), the runtime (new 47749c,
 * new[] 4774f0, 49667c with the element constructors 4953c4 and 427207) and
 * the hooks region_init, timer_init, group_array, mech_type_select,
 * flyer_type_select, pool_create, base_pools, mech_types, flyer_types,
 * zone_load and the four leaves of this group.
 *
 * EDX:EAX exact; ECX is dead at every call site, so the adapters do not
 * return it (the framework returns the entry ECX): after 4617c8 the mission
 * load writes ECX (mov ecx,19h) and AX before reading them and loads EAX
 * before pushing it; after 4252c6 gun_slot_table writes EAX/EDX and passes
 * ECX only to new[] (which reads no register) before POP ECX; a zero
 * damage or collision slot word returns the entry registers, so ECX flows
 * on into col_slot_table and mech_types, whose 49630c and 492d00 read
 * neither EDX nor ECX first (mission_load.md, "MECH and FLYER type
 * loads"). No entry or exit
 * shim: no function reads a local it did not write, except the world load
 * with a script without groups (the group-0 slots f-0D4Eh are stack residue
 * in the original and zero here; outside the contract, every launch set
 * has groups). All five refuse compare mode (file I/O, allocation, game
 * calls that build the mission tables); the load is accepted by the
 * after-load snapshot. No game instruction is embedded. */
#include "es2/core/mission_load.h"
static uint64_t __cdecl mission_world_world_load(const uint32_t *a,void *u,const uint32_t *r) {
    (void)a;
    BYTE scratch[ES2_WORLD_LOAD_SCRATCH]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_world_load);
    weapons_impact_context c={(uint32_t)(uintptr_t)u,(uint32_t)(uintptr_t)(scratch+ES2_WORLD_LOAD_FRAME)};
    g.call=weapons_impact_guest_call;g.context=&c;
    uint32_t head=0;
    const uint64_t result=es2_world_load_registers(&math_native_memory,&g,r,&head);
    __asm__ volatile("movl %0,%%fs:0"::"r"(head):"memory");return result;
}
static uint64_t __cdecl mission_world_actor_list_init(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_actor_list_init);
    return es2_actor_list_init_registers(&math_native_memory,&g,a[0],a[1],r,NULL);
}
#define MISSION_WORLD_TABLE(name) \
static uint64_t __cdecl mission_world_##name(const uint32_t *a,void *u,const uint32_t *r) { \
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_##name);(void)a; \
    return es2_##name##_registers(&math_native_memory,&g,r,NULL); }
MISSION_WORLD_TABLE(gun_slot_table)
MISSION_WORLD_TABLE(dmg_slot_table)
MISSION_WORLD_TABLE(col_slot_table)
#undef MISSION_WORLD_TABLE
/* Callee counters (hk_<hook>_<suffix>), incremented only while the hook is
 * non-original. Keys: the callee VA, or the virtual slot (the stream's read
 * +18h). */
static callee_counter world_load_counters[]={
    {"call_49630c",ES2_COLLIDE_CALL,0x49630c,0},
    {"call_49681a",ES2_COLLIDE_CALL,0x49681a,0},
    {"call_45ee00",ES2_COLLIDE_CALL,0x45ee00,0},
    {"call_47ad40",ES2_COLLIDE_CALL,0x47ad40,0},
    {"call_479c78",ES2_COLLIDE_CALL,0x479c78,0},
    {"call_47af98",ES2_COLLIDE_CALL,0x47af98,0},
    {"call_47ad78",ES2_COLLIDE_CALL,0x47ad78,0},
    {"slot_18",ES2_COLLIDE_SLOT,0x18,0},
    {"call_4774f0",ES2_COLLIDE_CALL,0x4774f0,0},
    {"call_453878",ES2_COLLIDE_CALL,0x453878,0},
    {"call_4539a0",ES2_COLLIDE_CALL,0x4539a0,0},
    {"call_496766",ES2_COLLIDE_CALL,0x496766,0},
    {"call_492b90",ES2_COLLIDE_CALL,0x492b90,0},
    {"call_42e010",ES2_COLLIDE_CALL,0x42e010,0},
    {"call_49667c",ES2_COLLIDE_CALL,0x49667c,0},
    {"call_423358",ES2_COLLIDE_CALL,0x423358,0},
    {"call_423104",ES2_COLLIDE_CALL,0x423104,0},
    {"call_423d38",ES2_COLLIDE_CALL,0x423d38,0},
    {"call_41ff10",ES2_COLLIDE_CALL,0x41ff10,0},
    {"call_422e90",ES2_COLLIDE_CALL,0x422e90,0},
    {"call_405ebc",ES2_COLLIDE_CALL,0x405ebc,0},
    {"call_47749c",ES2_COLLIDE_CALL,0x47749c,0},
    {"call_471ad8",ES2_COLLIDE_CALL,0x471ad8,0},
    {"call_405df4",ES2_COLLIDE_CALL,0x405df4,0},
    {"call_411df8",ES2_COLLIDE_CALL,0x411df8,0},
    {"call_40fecc",ES2_COLLIDE_CALL,0x40fecc,0},
    {"call_40d218",ES2_COLLIDE_CALL,0x40d218,0},
    {"call_40cbec",ES2_COLLIDE_CALL,0x40cbec,0},
    {"call_42049c",ES2_COLLIDE_CALL,0x42049c,0},
    {"call_422fb4",ES2_COLLIDE_CALL,0x422fb4,0},
    {"call_42789c",ES2_COLLIDE_CALL,0x42789c,0},
};
static callee_counter actor_list_init_counters[]={{"call_4774f0",ES2_COLLIDE_CALL,0x4774f0,0}};
static callee_counter gun_slot_table_counters[]={{"call_4774f0",ES2_COLLIDE_CALL,0x4774f0,0}};
static callee_counter dmg_slot_table_counters[]={{"call_4774f0",ES2_COLLIDE_CALL,0x4774f0,0}};
static callee_counter col_slot_table_counters[]={{"call_4774f0",ES2_COLLIDE_CALL,0x4774f0,0}};
#define MISSION_WORLD_COUNTERS(name) \
    if(hook==ES2_HOOK_INDEX_##name){*count=sizeof(name##_counters)/sizeof(name##_counters[0]);return name##_counters;}
static callee_counter *mission_world_hook_counters(unsigned hook,unsigned *count) {
    MISSION_WORLD_COUNTERS(world_load)
    MISSION_WORLD_COUNTERS(actor_list_init)
    MISSION_WORLD_COUNTERS(gun_slot_table)
    MISSION_WORLD_COUNTERS(dmg_slot_table)
    MISSION_WORLD_COUNTERS(col_slot_table)
    *count=0;return NULL;
}
#undef MISSION_WORLD_COUNTERS
/* Verified entry bytes (reference DBSIM.EXE).
 * 424308: push ebp; mov ebp,esp; add esp,-0E54h (imm32); push ebx; push
 * esi; push edi; lea edi,[ebp-24h]; mov eax,49A7FCh. Relocated 9 bytes
 * (1,2,6); the descriptor load and the call of 49630c stay in place.
 * 411df8: push ebp; mov ebp,esp; push ebx; push esi; mov esi,[ebp+0Ch];
 * mov ebx,[ebp+8]. Relocated 5 bytes (1,2,1,1).
 * 40fecc: movsx eax,word [4A98E8h]; mov edx,eax; add edx,edx; lea
 * edx,[edx+2*edx]. Relocated 7 bytes (0F BF /r with an absolute address).
 * 40d218, 40cbec: push ebx; mov bx,[498856h] / [4987DCh]; test bx,bx; je.
 * Relocated 8 bytes (1,7: 66 8B /r with an absolute address); the JE stays
 * in place. */
static const BYTE world_load_sig[]={0x55,0x8b,0xec,0x81,0xc4,0xac,0xf1,0xff,0xff,0x53,0x56,0x57,0x8d,0x7d,0xdc,
    0xb8,0xfc,0xa7,0x49,0x00};
static const BYTE actor_list_init_sig[]={0x55,0x8b,0xec,0x53,0x56,0x8b,0x75,0x0c,0x8b,0x5d,0x08};
static const BYTE gun_slot_table_sig[]={0x0f,0xbf,0x05,0xe8,0x98,0x4a,0x00,0x8b,0xd0,0x03,0xd2,0x8d,0x14,0x52};
static const BYTE dmg_slot_table_sig[]={0x53,0x66,0x8b,0x1d,0x56,0x88,0x49,0x00,0x66,0x85,0xdb,0x74,0x17};
static const BYTE col_slot_table_sig[]={0x53,0x66,0x8b,0x1d,0xdc,0x87,0x49,0x00,0x66,0x85,0xdb,0x74,0x16};
static const BYTE world_load_lens[]={1,2,6},actor_list_init_lens[]={1,2,1,1},gun_slot_table_lens[]={7},
    slot_table_lens[]={1,7};
#define MISSION_WORLD_EXT(name) \
static const es2_hook_extension mission_world_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0, \
    mission_world_##name,TRUE}; \
static uint32_t __cdecl mission_world_##name##32(const uint32_t *a,void *u) { \
    return (uint32_t)mission_world_##name(a,u,weapons_no_registers); }
MISSION_WORLD_EXT(world_load)
MISSION_WORLD_EXT(actor_list_init)
MISSION_WORLD_EXT(gun_slot_table)
MISSION_WORLD_EXT(dmg_slot_table)
MISSION_WORLD_EXT(col_slot_table)
#undef MISSION_WORLD_EXT
static es2_hook_descriptor mission_world_hook_descriptors[]={
    {ES2_HOOK_ID(world_load),world_load_sig,20,9,world_load_lens,3,0,mission_world_world_load32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(actor_list_init),actor_list_init_sig,11,5,actor_list_init_lens,4,2,mission_world_actor_list_init32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(gun_slot_table),gun_slot_table_sig,14,7,gun_slot_table_lens,1,0,mission_world_gun_slot_table32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(dmg_slot_table),dmg_slot_table_sig,13,8,slot_table_lens,2,0,mission_world_dmg_slot_table32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(col_slot_table),col_slot_table_sig,13,8,slot_table_lens,2,0,mission_world_col_slot_table32,NULL,NULL,NULL,0},
};
#endif
