#ifndef ES2_RESULTS_HOOKS_H
#define ES2_RESULTS_HOOKS_H
/* results group (docs/reverse-engineering/mission_runtime.md, section
 * "team result records 423d68"): result_conditions 423d68 (team,
 * stream), the team actor records of results.dat, called once per DBSIM
 * run by the result writer 42412c. condition_record 41b4ec (a nested hook
 * of group vehicle_update) and the stream's write virtual +14h (47b2b8,
 * original) go through the weapons guest bridge and callee observer with
 * the original's registers, so the nested hook keeps its mode. The 84-byte
 * condition record and the three frame words the writes read live in the
 * adapter's 128 scratch bytes at the original's EBP-relative offsets; no
 * frame byte is read before it is written (condition_record writes the
 * whole record), so no entry shim. EAX/EDX are exact; they and ECX are dead
 * at the only caller. Refuses compare mode (writes the file). No game
 * instruction is embedded. */
#include "es2/core/results.h"
static uint64_t __cdecl results_result_conditions(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[128]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_result_conditions);
    return es2_result_conditions_registers(&math_native_memory,&g,a[0],a[1],r);
}
/* Callee counters (hk_result_conditions_<suffix>), incremented only while
 * the hook is non-original: slot_14 = 34 x call_41b4ec. */
static callee_counter result_conditions_counters[]={
    {"call_41b4ec",ES2_COLLIDE_CALL,0x41b4ec,0},
    {"slot_14",ES2_COLLIDE_SLOT,0x14,0},
};
static callee_counter *results_hook_counters(unsigned hook,unsigned *count) {
    if(hook==ES2_HOOK_INDEX_result_conditions) { *count=sizeof(result_conditions_counters)/sizeof(result_conditions_counters[0]);return result_conditions_counters; }
    *count=0;return NULL;
}
/* push ebp; mov ebp,esp; add esp,-64h; push ebx; push esi; push edi */
static const BYTE result_conditions_sig[]={0x55,0x8b,0xec,0x83,0xc4,0x9c,0x53,0x56,0x57};
static const BYTE results_frame_lens[]={1,2,3};
static const es2_hook_extension results_result_conditions_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,results_result_conditions,TRUE};
static uint32_t __cdecl results_result_conditions32(const uint32_t *a,void *u) {return (uint32_t)results_result_conditions(a,u,weapons_no_registers);}
static es2_hook_descriptor results_hook_descriptors[]={
    {ES2_HOOK_ID(result_conditions),result_conditions_sig,9,6,results_frame_lens,3,2,results_result_conditions32,NULL,NULL,NULL,0},
};
#endif
