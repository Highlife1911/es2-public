#ifndef ES2_FLYER_PART_HOOKS_H
#define ES2_FLYER_PART_HOOKS_H
/* FLYER part damage 421bb4, slot +74h of the FLYER vtable
 * (docs/reverse-engineering/hit_tests.md, the section
 * "FLYER part damage"), called through the slot by the FLYER hit
 * test 421c8c, by the ACTOR radial-hit default 411b3c (FLYER slot +70h,
 * whose caller gets these registers back by a tail return) and by the
 * cheat dispatcher 45feb8. Every game callee goes through the weapons
 * guest bridge and callee observer (weapons_graph) with the original's
 * registers, so the nested hooks (death_actions, trigger_fire,
 * script_start and the source's slot +60h, e.g. mech_kill) keep their
 * modes and the region damage 40da38, the message post 420a68 and the
 * sound 462ac4 run as original code. Compare mode refused (game calls).
 *
 * The original returns ECX = 0 on every path. That ECX reaches 411b3c's
 * callers (radius_hit, ground_collide) and the cheat dispatcher's callers,
 * so an entry shim zeroes ECX before the framework's dispatch: the
 * dispatch restores the entry ECX on return (replaced mode), and the
 * original never reads its entry ECX (original mode). No game instruction is embedded. */
#include "es2/core/flyer_part.h"
static uint64_t __cdecl flyer_part_flyer_part(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[16]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_flyer_part);
    return es2_flyer_part_registers(&math_native_memory,&g,a[0],a[1],a[2],a[3],r);
}
/* Callee counters (hk_flyer_part_<suffix>), incremented only while the
 * hook is non-original. Keys: the callee VA, or the vtable offset for
 * slots. */
static callee_counter flyer_part_counters[]={
    {"call_40da38",ES2_COLLIDE_CALL,0x40da38,0},
    {"call_420a68",ES2_COLLIDE_CALL,0x420a68,0},
    {"slot_60",ES2_COLLIDE_SLOT,0x60,0},
    {"call_411bc8",ES2_COLLIDE_CALL,0x411bc8,0},
    {"call_423430",ES2_COLLIDE_CALL,0x423430,0},
    {"call_413e50",ES2_COLLIDE_CALL,0x413e50,0},
    {"call_462ac4",ES2_COLLIDE_CALL,0x462ac4,0},
};
static callee_counter *flyer_part_hook_counters(unsigned hook,unsigned *count) {
    if(hook==ES2_HOOK_INDEX_flyer_part) {
        *count=sizeof(flyer_part_counters)/sizeof(flyer_part_counters[0]);return flyer_part_counters;
    }
    *count=0;return NULL;
}
/* push ebp; mov ebp,esp; push ebx; push esi; push edi; mov dx,[ebp+10h] */
static const BYTE flyer_part_sig[]={0x55,0x8b,0xec,0x53,0x56,0x57,0x66,0x8b,0x55,0x10};
static const BYTE flyer_part_frame_lens[]={1,2,1,1};
static const es2_hook_extension flyer_part_flyer_part_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,
    flyer_part_flyer_part,TRUE};
static uint32_t __cdecl flyer_part_flyer_part32(const uint32_t *a,void *u) {
    return (uint32_t)flyer_part_flyer_part(a,u,weapons_no_registers);
}
static es2_hook_descriptor flyer_part_hook_descriptors[]={
    {ES2_HOOK_ID(flyer_part),flyer_part_sig,10,5,flyer_part_frame_lens,4,4,flyer_part_flyer_part32,NULL,NULL,NULL,0},
};
/* The entry shim: ECX = 0, then the framework's bridge with the caller's
 * stack unchanged (return address on top, four argument slots). */
static int flyer_part_entry(es2_machine *m,uint32_t bridge) {(void)bridge;m->ecx=0;return 0;}
ES2_SHIM(flyer_part_shim,0x21bb4u,flyer_part_entry,0);
#endif
