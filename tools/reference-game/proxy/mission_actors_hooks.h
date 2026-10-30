#ifndef ES2_MISSION_ACTORS_HOOKS_H
#define ES2_MISSION_ACTORS_HOOKS_H
/* mission_actors group (docs/reverse-engineering/mission_load.md,
 * section "L6 actor load"): the actor load 4253d8, script pass 2 of
 * the mission load, called once per process from 46182b. It runs during
 * the load only, so it is live only with the early arming (the
 * default). An own group because its in-vitro harness
 * (a synthetic script stream, every callee a logging stub) shares nothing
 * with the constructors of mission_ctors.
 *
 * Every game call goes through the weapons guest bridge and callee observer
 * (weapons_graph) with the original's EAX/EDX/ECX; 49630c gets the synthetic
 * EBP (the frame in a zeroed scratch, the locals and record buffers at the
 * original's offsets) through weapons_impact_guest_call, and the adapter
 * writes the record's saved head back to FS:0 as the original's epilogue
 * does (ECX). The callees stay game code with their own modes: the script
 * name 45ee00/492b90, the stream 47ad40/47af98/the virtual read +18h/47ad78
 * (file I/O), the weapon shape cache 40f998/40fa88, the terrain marks
 * 470ed4/47129c, the runtime (new[] 4774f0, memcpy 49680e) and the hooks
 * pool_alloc, mech_ctor, terrain_height, weapon_fit, condition_init,
 * action_copy, initial_damage, flyer_ctor, building_factory, shape_word (the
 * building virtual +10h), subject_resolve, heading_point, group_ctor,
 * base_marks and pool_prev.
 *
 * EDX:EAX exact (the stream close's); ECX (the saved head) is dead at the
 * only call site: 44f6f4 loads EAX before 49630c, which keeps EDX and ECX,
 * and writes EDX and ECX before reading them. No entry or exit shim: the
 * function has no arguments and reads no local it did not write (the record
 * buffers are filled by the stream reads). Refuses compare mode
 * (allocation, file I/O, constructors); the load is accepted by the
 * after-load snapshot. No game instruction is embedded. */
#include "es2/core/mission_load.h"
static uint64_t __cdecl mission_actors_actor_load(const uint32_t *a,void *u,const uint32_t *r) {
    (void)a;
    BYTE scratch[ES2_ACTOR_LOAD_SCRATCH]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_actor_load);
    weapons_impact_context c={(uint32_t)(uintptr_t)u,(uint32_t)(uintptr_t)(scratch+ES2_ACTOR_LOAD_FRAME)};
    g.call=weapons_impact_guest_call;g.context=&c;
    uint32_t head=0;
    const uint64_t result=es2_actor_load_registers(&math_native_memory,&g,r,&head);
    __asm__ volatile("movl %0,%%fs:0"::"r"(head):"memory");return result;
}
/* Callee counters (hk_actor_load_<suffix>), incremented only while the hook
 * is non-original. Keys: the callee VA, or the virtual slot (the stream's
 * read +18h, the building's shape word +10h). */
static callee_counter actor_load_counters[]={
    {"call_49630c",ES2_COLLIDE_CALL,0x49630c,0},
    {"call_45ee00",ES2_COLLIDE_CALL,0x45ee00,0},
    {"call_492b90",ES2_COLLIDE_CALL,0x492b90,0},
    {"call_47ad40",ES2_COLLIDE_CALL,0x47ad40,0},
    {"call_47af98",ES2_COLLIDE_CALL,0x47af98,0},
    {"slot_18",ES2_COLLIDE_SLOT,0x18,0},
    {"call_40f998",ES2_COLLIDE_CALL,0x40f998,0},
    {"call_471b30",ES2_COLLIDE_CALL,0x471b30,0},
    {"call_415bb0",ES2_COLLIDE_CALL,0x415bb0,0},
    {"call_46e180",ES2_COLLIDE_CALL,0x46e180,0},
    {"call_4175dc",ES2_COLLIDE_CALL,0x4175dc,0},
    {"call_415068",ES2_COLLIDE_CALL,0x415068,0},
    {"call_411b90",ES2_COLLIDE_CALL,0x411b90,0},
    {"call_4178e8",ES2_COLLIDE_CALL,0x4178e8,0},
    {"call_40fa88",ES2_COLLIDE_CALL,0x40fa88,0},
    {"call_4215f4",ES2_COLLIDE_CALL,0x4215f4,0},
    {"call_405314",ES2_COLLIDE_CALL,0x405314,0},
    {"slot_10",ES2_COLLIDE_SLOT,0x10,0},
    {"call_470ed4",ES2_COLLIDE_CALL,0x470ed4,0},
    {"call_4774f0",ES2_COLLIDE_CALL,0x4774f0,0},
    {"call_425348",ES2_COLLIDE_CALL,0x425348,0},
    {"call_492934",ES2_COLLIDE_CALL,0x492934,0},
    {"call_423b34",ES2_COLLIDE_CALL,0x423b34,0},
    {"call_405db0",ES2_COLLIDE_CALL,0x405db0,0},
    {"call_49680e",ES2_COLLIDE_CALL,0x49680e,0},
    {"call_47129c",ES2_COLLIDE_CALL,0x47129c,0},
    {"call_471c70",ES2_COLLIDE_CALL,0x471c70,0},
    {"call_47ad78",ES2_COLLIDE_CALL,0x47ad78,0},
};
static callee_counter *mission_actors_hook_counters(unsigned hook,unsigned *count) {
    if(hook==ES2_HOOK_INDEX_actor_load){*count=sizeof(actor_load_counters)/sizeof(actor_load_counters[0]);return actor_load_counters;}
    *count=0;return NULL;
}
/* 4253d8: push ebp; mov ebp,esp; add esp,-0B30h; mov eax,49A8B5h; push ebx.
 * Relocated 9 bytes (1,2,6); the descriptor load and the call of 49630c
 * that follow stay in place. */
static const BYTE actor_load_sig[]={0x55,0x8b,0xec,0x81,0xc4,0xd0,0xf4,0xff,0xff,0xb8,0xb5,0xa8,0x49,0x00,0x53};
static const BYTE actor_load_lens[]={1,2,6};
static const es2_hook_extension mission_actors_actor_load_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,
    mission_actors_actor_load,TRUE};
static uint32_t __cdecl mission_actors_actor_load32(const uint32_t *a,void *u) {
    return (uint32_t)mission_actors_actor_load(a,u,weapons_no_registers);
}
static es2_hook_descriptor mission_actors_hook_descriptors[]={
    {ES2_HOOK_ID(actor_load),actor_load_sig,15,9,actor_load_lens,3,0,mission_actors_actor_load32,NULL,NULL,NULL,0},
};
#endif
