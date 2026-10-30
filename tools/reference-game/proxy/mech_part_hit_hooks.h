#ifndef ES2_MECH_PART_HIT_HOOKS_H
#define ES2_MECH_PART_HIT_HOOKS_H
/* MECH part hit 4188c8 (docs/reverse-engineering/hit_tests.md,
 * the section "MECH part hit"), called by the MECH
 * hit test 418ba8 and by 4198f4. Every game callee goes through the
 * weapons guest bridge and callee observer (weapons_graph) with the
 * original's registers, so the nested hooks (multiply_q10, part_ratio,
 * slot +74h vehicle_part, slot +70h vehicle_hit, rng_next, pool_alloc,
 * explosion_create, debris_table, debris_burst) keep their modes and the
 * panel jolt 434010, the message post 420a68, the weapon eject 40f57c and
 * the region damage 40da38 run as original code. The leaves 410670
 * (weapon by kind) and 426ac8 (results-list append) are computed in core.
 * The runtime frame setup 49630c gets the synthetic EBP (scratch +
 * ES2_MECH_PART_HIT_FRAME) through weapons_impact_guest_call; the adapter
 * restores FS:0 to the record's saved head (EAX), as the original's
 * epilogue does. ECX after the hook is dead at all seven call sites
 * (hit_tests.md). Compare mode refused (game calls). No entry shim, no
 * game instruction embedded. */
#include "es2/core/mech_part_hit.h"
static uint64_t __cdecl mech_part_hit_mech_part_hit(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[ES2_MECH_PART_HIT_SCRATCH]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_mech_part_hit);
    weapons_impact_context c={(uint32_t)(uintptr_t)u,(uint32_t)(uintptr_t)(scratch+ES2_MECH_PART_HIT_FRAME)};
    g.call=weapons_impact_guest_call;g.context=&c;
    uint64_t result=es2_mech_part_hit_registers(&math_native_memory,&g,a[0],a[1],a[2],a[3],r);
    uint32_t restored=(uint32_t)result;es2_machine_set_fs0(restored);return result;
}
/* Callee counters (hk_mech_part_hit_<suffix>), incremented only while the
 * hook is non-original. Keys: the callee VA, or the vtable offset for
 * slots. */
static callee_counter mech_part_hit_counters[]={
    {"call_49630c",ES2_COLLIDE_CALL,0x49630c,0},
    {"call_47e0b0",ES2_COLLIDE_CALL,0x47e0b0,0},
    {"call_40dbc0",ES2_COLLIDE_CALL,0x40dbc0,0},
    {"slot_74",ES2_COLLIDE_SLOT,0x74,0},
    {"slot_70",ES2_COLLIDE_SLOT,0x70,0},
    {"call_434010",ES2_COLLIDE_CALL,0x434010,0},
    {"call_420a68",ES2_COLLIDE_CALL,0x420a68,0},
    {"call_492ee0",ES2_COLLIDE_CALL,0x492ee0,0},
    {"call_40f57c",ES2_COLLIDE_CALL,0x40f57c,0},
    {"call_40da38",ES2_COLLIDE_CALL,0x40da38,0},
    {"call_471b30",ES2_COLLIDE_CALL,0x471b30,0},
    {"call_407f1c",ES2_COLLIDE_CALL,0x407f1c,0},
    {"call_4089bc",ES2_COLLIDE_CALL,0x4089bc,0},
    {"call_408530",ES2_COLLIDE_CALL,0x408530,0},
};
static callee_counter *mech_part_hit_hook_counters(unsigned hook,unsigned *count) {
    if(hook==ES2_HOOK_INDEX_mech_part_hit) {
        *count=sizeof(mech_part_hit_counters)/sizeof(mech_part_hit_counters[0]);return mech_part_hit_counters;
    }
    *count=0;return NULL;
}
/* push ebp; mov ebp,esp; add esp,-4Ch; push ebx; push esi; push edi */
static const BYTE mech_part_hit_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xb4,0x53,0x56,0x57};
static const BYTE mech_part_hit_frame_lens[]={1,2,3};
static const es2_hook_extension mech_part_hit_mech_part_hit_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,
    mech_part_hit_mech_part_hit,TRUE};
static uint32_t __cdecl mech_part_hit_mech_part_hit32(const uint32_t *a,void *u) {
    return (uint32_t)mech_part_hit_mech_part_hit(a,u,weapons_no_registers);
}
static es2_hook_descriptor mech_part_hit_hook_descriptors[]={
    {ES2_HOOK_ID(mech_part_hit),mech_part_hit_sig,9,6,mech_part_hit_frame_lens,3,4,mech_part_hit_mech_part_hit32,NULL,NULL,NULL,0},
};
#endif
