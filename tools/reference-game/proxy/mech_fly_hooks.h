#ifndef ES2_MECH_FLY_HOOKS_H
#define ES2_MECH_FLY_HOOKS_H
/* MECH "player fly" movement 4198f4 (docs/reverse-engineering/
 * hit_tests.md, the section "MECH player fly
 * movement"), the +24h member of the MECH state "player fly", called only
 * by the trampoline mech_move 415afc. Every game callee goes through the
 * weapons guest bridge and callee observer (weapons_graph) with the
 * original's registers, so the nested hooks (euler_matrix,
 * transform_point, movement_step, terrain_height, sweep, multiply_q10,
 * mech_part_hit, debris_table, debris_burst, dist3d_delta, dist_gate,
 * weapon_converge) keep their modes and the SOS calls 462ac4, 463114 and
 * 46297c run as original code. The frame (120h bytes) lives in the
 * adapter's scratch at the original EBP offsets; [0x49a170] (the static
 * sweep packet's segment pointer) gets the scratch address of the
 * segment, which only the sweeps of this call read. No entry shim: no
 * frame local is read before it is written, the entry EAX/EDX are dead,
 * the entry ECX reaches euler_matrix as the core's entry register, and no
 * callee in the subtree reads stack residue that the original leaves
 * (hit_tests.md, "Mech-fly frame, packet and residue"). ECX after the hook is dead at the
 * trampoline (415b2d pops it). Compare mode refused (game calls). No game
 * instruction is embedded. */
#include "es2/core/mech_fly.h"
static uint64_t __cdecl mech_fly_mech_fly(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[ES2_MECH_FLY_SCRATCH]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_mech_fly);
    return es2_mech_fly_registers(&math_native_memory,&g,a[0],r);
}
/* Callee counters (hk_mech_fly_<suffix>), incremented only while the hook
 * is non-original. Keys: the callee VA. */
static callee_counter mech_fly_counters[]={
    {"call_47ebb8",ES2_COLLIDE_CALL,0x47ebb8,0},
    {"call_48043c",ES2_COLLIDE_CALL,0x48043c,0},
    {"call_467924",ES2_COLLIDE_CALL,0x467924,0},
    {"call_46e180",ES2_COLLIDE_CALL,0x46e180,0},
    {"call_426528",ES2_COLLIDE_CALL,0x426528,0},
    {"call_47e0b0",ES2_COLLIDE_CALL,0x47e0b0,0},
    {"call_4188c8",ES2_COLLIDE_CALL,0x4188c8,0},
    {"call_4089bc",ES2_COLLIDE_CALL,0x4089bc,0},
    {"call_408530",ES2_COLLIDE_CALL,0x408530,0},
    {"call_462ac4",ES2_COLLIDE_CALL,0x462ac4,0},
    {"call_47de72",ES2_COLLIDE_CALL,0x47de72,0},
    {"call_463114",ES2_COLLIDE_CALL,0x463114,0},
    {"call_46297c",ES2_COLLIDE_CALL,0x46297c,0},
    {"call_49288c",ES2_COLLIDE_CALL,0x49288c,0},
    {"call_41a74c",ES2_COLLIDE_CALL,0x41a74c,0},
};
static callee_counter *mech_fly_hook_counters(unsigned hook,unsigned *count) {
    if(hook==ES2_HOOK_INDEX_mech_fly) {
        *count=sizeof(mech_fly_counters)/sizeof(mech_fly_counters[0]);return mech_fly_counters;
    }
    *count=0;return NULL;
}
/* push ebp; mov ebp,esp; add esp,-120h; cmp byte [49a194h],0 */
static const BYTE mech_fly_sig[]={0x55,0x8b,0xec,0x81,0xc4,0xe0,0xfe,0xff,0xff,0x80,0x3d,0x94,0xa1,0x49,0x00,0x00};
static const BYTE mech_fly_frame_lens[]={1,2,6};
static const es2_hook_extension mech_fly_mech_fly_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,
    mech_fly_mech_fly,TRUE};
static uint32_t __cdecl mech_fly_mech_fly32(const uint32_t *a,void *u) {
    return (uint32_t)mech_fly_mech_fly(a,u,weapons_no_registers);
}
static es2_hook_descriptor mech_fly_hook_descriptors[]={
    {ES2_HOOK_ID(mech_fly),mech_fly_sig,16,9,mech_fly_frame_lens,3,1,mech_fly_mech_fly32,NULL,NULL,NULL,0},
};
#endif
