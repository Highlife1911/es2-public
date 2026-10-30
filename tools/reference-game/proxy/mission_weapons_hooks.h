#ifndef ES2_MISSION_WEAPONS_HOOKS_H
#define ES2_MISSION_WEAPONS_HOOKS_H
#include "guest_native.h"
/* mission_weapons group (docs/reverse-engineering/mission_load.md,
 * section "L4 weapon fit"): the weapon fit of the actor load,
 * weapon_fit 4175dc (once per constructed mech, from 4258a2), the player
 * weapon system player_weapons 4104ec (once, for the player's mech) and the
 * weapon factory weapon_factory 40fff8 (from both). They run during the
 * load only, so they are live only with the early arming (the
 * default).
 *
 * Every game call goes through the weapons guest bridge and callee observer
 * (weapons_graph) with the original's EAX/EDX/ECX. weapon_fit and
 * weapon_factory set up the runtime record through 49630c with the
 * synthetic EBP (the frame in a zeroed scratch, the locals at the
 * original's offsets: guest callees receive their addresses) through
 * weapons_impact_guest_call, and the adapter writes the record's saved head
 * back to FS:0 as the original's epilogue does (weapon_fit: EAX,
 * weapon_factory: ECX). The callees stay game code with their own modes:
 * new 47749c, new[] 4774f0, the array construction 49667c and the arena
 * calls 474bbc/474be4 (runtime), the allocator 47a2c8, the mount table
 * 420634, the weapon constructors 40e074 ... 40e380, the weapon helpers
 * 40cec0 ... 40fb2c, 40e61c, 417530, 413ac8, 417d08, the shape virtual +20h,
 * and the hooks actor_animate, mount_offset, fire_group, shield_capacity,
 * weapon_record, def_lookup and proj_record; player_weapons and
 * weapon_factory nest inside weapon_fit.
 *
 * EDX:EAX exact; ECX is dead after every call site (mission_load.md lists
 * them), so it is not returned (the framework returns the entry ECX). The
 * weapon factory has seven argument slots, the framework passes six: the
 * seventh (0 from 4175dc, 1 from 4104ec) and the high word of the caller's
 * EBX only reach argument slots that no callee reads; the adapter
 * reconstructs both as the original callers have them (the word from the
 * player byte actor+0A3h that selected the caller, EBX = the actor). The
 * factory's projectile local [EBP-58h] is stack residue when the first
 * weapon's record has the selector 21h; with the shipped WEAPONS.DAT only
 * ID 18 (ECM) has it, and its case does not pass the local, so the zeroed
 * scratch stands in. No entry or exit shim. All three refuse compare mode
 * (allocation, constructors, the construction count); the load is accepted
 * by the after-load snapshot. No game instruction is embedded. */
#include "es2/core/mission_load.h"
static uint64_t __cdecl mission_weapons_weapon_fit(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[ES2_WEAPON_FIT_SCRATCH]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_weapon_fit);
    weapons_impact_context c={(uint32_t)(uintptr_t)u,(uint32_t)(uintptr_t)(scratch+ES2_WEAPON_FIT_FRAME)};
    g.call=weapons_impact_guest_call;g.context=&c;
    uint64_t result=es2_weapon_fit_registers(&math_native_memory,&g,a[0],a[1],a[2],r,NULL);
    uint32_t head=(uint32_t)result;es2_machine_set_fs0(head);return result;
}
static uint64_t __cdecl mission_weapons_player_weapons(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_player_weapons);
    return es2_player_weapons_registers(&math_native_memory,&g,a[0],a[1],a[2],a[3],a[4],a[5],r,NULL);
}
static uint64_t __cdecl mission_weapons_weapon_factory(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[ES2_WEAPON_FACTORY_SCRATCH]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_weapon_factory);
    weapons_impact_context c={(uint32_t)(uintptr_t)u,(uint32_t)(uintptr_t)(scratch+ES2_WEAPON_FACTORY_FRAME)};
    g.call=weapons_impact_guest_call;g.context=&c;
    const uint32_t word=*(const BYTE *)es2_guest_span(a[1]+0xa3u,1u)?1u:0u;
    uint32_t head=0;
    uint64_t result=es2_weapon_factory_registers(&math_native_memory,&g,a[0],a[1],a[2],a[3],a[4],a[5],word,a[1],r,&head);
    es2_machine_set_fs0(head);return result;
}
/* Callee counters (hk_<hook>_<suffix>), incremented only while the hook is
 * non-original. Keys: the callee VA, or the virtual slot. */
static callee_counter weapon_fit_counters[]={
    {"call_49630c",ES2_COLLIDE_CALL,0x49630c,0},
    {"call_420634",ES2_COLLIDE_CALL,0x420634,0},
    {"call_47749c",ES2_COLLIDE_CALL,0x47749c,0},
    {"call_4104ec",ES2_COLLIDE_CALL,0x4104ec,0},
    {"call_40fff8",ES2_COLLIDE_CALL,0x40fff8,0},
    {"call_402604",ES2_COLLIDE_CALL,0x402604,0},
    {"call_40f540",ES2_COLLIDE_CALL,0x40f540,0},
    {"call_40e61c",ES2_COLLIDE_CALL,0x40e61c,0},
    {"slot_20",ES2_COLLIDE_SLOT,0x20,0},
    {"call_417530",ES2_COLLIDE_CALL,0x417530,0},
    {"call_40cec0",ES2_COLLIDE_CALL,0x40cec0,0},
    {"call_40d240",ES2_COLLIDE_CALL,0x40d240,0},
    {"call_40d128",ES2_COLLIDE_CALL,0x40d128,0},
    {"call_40cb84",ES2_COLLIDE_CALL,0x40cb84,0},
    {"call_40d284",ES2_COLLIDE_CALL,0x40d284,0},
    {"call_40fb2c",ES2_COLLIDE_CALL,0x40fb2c,0},
    {"call_417bec",ES2_COLLIDE_CALL,0x417bec,0},
    {"call_413ac8",ES2_COLLIDE_CALL,0x413ac8,0},
    {"call_417d08",ES2_COLLIDE_CALL,0x417d08,0},
};
static callee_counter player_weapons_counters[]={
    {"call_40fff8",ES2_COLLIDE_CALL,0x40fff8,0},
    {"call_49667c",ES2_COLLIDE_CALL,0x49667c,0},
    {"call_4774f0",ES2_COLLIDE_CALL,0x4774f0,0},
    {"call_410ae4",ES2_COLLIDE_CALL,0x410ae4,0},
};
static callee_counter weapon_factory_counters[]={
    {"call_49630c",ES2_COLLIDE_CALL,0x49630c,0},
    {"call_4774f0",ES2_COLLIDE_CALL,0x4774f0,0},
    {"call_474bbc",ES2_COLLIDE_CALL,0x474bbc,0},
    {"call_40fe84",ES2_COLLIDE_CALL,0x40fe84,0},
    {"call_40ffc8",ES2_COLLIDE_CALL,0x40ffc8,0},
    {"call_40ffb0",ES2_COLLIDE_CALL,0x40ffb0,0},
    {"call_47a2c8",ES2_COLLIDE_CALL,0x47a2c8,0},
    {"call_40e074",ES2_COLLIDE_CALL,0x40e074,0},
    {"call_40e140",ES2_COLLIDE_CALL,0x40e140,0},
    {"call_40e274",ES2_COLLIDE_CALL,0x40e274,0},
    {"call_40e308",ES2_COLLIDE_CALL,0x40e308,0},
    {"call_40e344",ES2_COLLIDE_CALL,0x40e344,0},
    {"call_40e2bc",ES2_COLLIDE_CALL,0x40e2bc,0},
    {"call_40e380",ES2_COLLIDE_CALL,0x40e380,0},
    {"call_474be4",ES2_COLLIDE_CALL,0x474be4,0},
};
#define MISSION_WEAPONS_COUNTERS(name) \
    if(hook==ES2_HOOK_INDEX_##name){*count=sizeof(name##_counters)/sizeof(name##_counters[0]);return name##_counters;}
static callee_counter *mission_weapons_hook_counters(unsigned hook,unsigned *count) {
    MISSION_WEAPONS_COUNTERS(weapon_fit)
    MISSION_WEAPONS_COUNTERS(player_weapons)
    MISSION_WEAPONS_COUNTERS(weapon_factory)
    *count=0;return NULL;
}
#undef MISSION_WEAPONS_COUNTERS
/* 4175dc: push ebp; mov ebp,esp; add esp,-12Ch; push ebx; push esi; push
 * edi. Relocated 9 bytes (1,2,6).
 * 4104ec: push ebp; mov ebp,esp; push ecx; push ebx; push esi; push edi;
 * push 1. Relocated 5 bytes (1,2,1,1).
 * 40fff8: push ebp; mov ebp,esp; add esp,-7Ch; mov eax,499055h; push ebx.
 * Relocated 6 bytes (1,2,3); the descriptor load and the call of 49630c
 * that follow stay in place. */
static const BYTE weapon_fit_sig[]={0x55,0x8b,0xec,0x81,0xc4,0xd4,0xfe,0xff,0xff,0x53,0x56,0x57};
static const BYTE player_weapons_sig[]={0x55,0x8b,0xec,0x51,0x53,0x56,0x57,0x6a,0x01};
static const BYTE weapon_factory_sig[]={0x55,0x8b,0xec,0x83,0xc4,0x84,0xb8,0x55,0x90,0x49,0x00,0x53};
static const BYTE weapon_fit_lens[]={1,2,6},player_weapons_lens[]={1,2,1,1},weapon_factory_lens[]={1,2,3};
#define MISSION_WEAPONS_EXT(name) \
    static const es2_hook_extension mission_weapons_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,mission_weapons_##name,TRUE}; \
    static uint32_t __cdecl mission_weapons_##name##32(const uint32_t *a,void *u) {return (uint32_t)mission_weapons_##name(a,u,weapons_no_registers);}
MISSION_WEAPONS_EXT(weapon_fit)
MISSION_WEAPONS_EXT(player_weapons)
MISSION_WEAPONS_EXT(weapon_factory)
#undef MISSION_WEAPONS_EXT
static es2_hook_descriptor mission_weapons_hook_descriptors[]={
    {ES2_HOOK_ID(weapon_fit),weapon_fit_sig,12,9,weapon_fit_lens,3,3,mission_weapons_weapon_fit32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(player_weapons),player_weapons_sig,9,5,player_weapons_lens,4,6,mission_weapons_player_weapons32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(weapon_factory),weapon_factory_sig,12,6,weapon_factory_lens,3,6,mission_weapons_weapon_factory32,NULL,NULL,NULL,0},
};
#endif
