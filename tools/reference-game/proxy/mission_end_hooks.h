#ifndef ES2_MISSION_END_HOOKS_H
#define ES2_MISSION_END_HOOKS_H
/* mission_end group (docs/reverse-engineering/launcher.md, section
 * "the mission end `0x461ff0` as own code"): the mission end of the game main,
 * called once per process by 45f160 after the loop tail. The core
 * (core/src/mission_end.c) clears the byte 4d1dd6, closes the dump and
 * tape streams of an open tape, calls the results writer on the object
 * dword 4d24e6 and writes the exit code 4d27b8 (6 demo, 0 quit, 4 or 3).
 * Both game calls (the runtime's _fclose thunk 496838 and the results
 * writer 42412c, a hook of its own group) stay original and go through
 * the weapons guest bridge and callee observer with the original's entry
 * EAX/EDX/ECX, so the nested hook keeps its mode. The two one-byte exits
 * the original calls last (426514, 425344) are bare RETs and are not
 * called. Refuses compare mode (stream close, file writes).
 *
 * No entry shim: the caller 45f160 reads none of EAX, EDX, ECX after the
 * call (45f550: push 2; lea edx; ... overwrites them) and the original
 * restores EBX itself. */
#include "es2/core/mission_end.h"
static uint64_t __cdecl mission_end_mission_end(const uint32_t *a,void *u,const uint32_t *r) {
    (void)a;es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_mission_end);uint32_t ecx;
    return es2_mission_end_registers(&math_native_memory,&g,r,&ecx);
}
/* Callee counters (hk_mission_end_<suffix>), incremented only while the
 * hook is non-original. Keys: the callee VA. */
static callee_counter mission_end_counters[]={
    {"call_496838",ES2_COLLIDE_CALL,0x496838,0},
    {"call_42412c",ES2_COLLIDE_CALL,0x42412c,0},
};
static callee_counter *mission_end_hook_counters(unsigned hook,unsigned *count) {
    if(hook==ES2_HOOK_INDEX_mission_end) { *count=sizeof(mission_end_counters)/sizeof(mission_end_counters[0]);return mission_end_counters; }
    *count=0;return NULL;
}
/* Verified entry bytes (reference DBSIM.EXE 461ff0): push ebx; mov ebx,4d24bch;
 * mov byte [4d1dd6h],0 */
static const BYTE mission_end_sig[]={0x53,0xbb,0xbc,0x24,0x4d,0x00,0xc6,0x05,0xd6,0x1d,0x4d,0x00,0x00};
static const BYTE mission_end_lens[]={1,5};
static const es2_hook_extension mission_end_mission_end_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,mission_end_mission_end,TRUE};
static uint32_t __cdecl mission_end_mission_end32(const uint32_t *a,void *u) {return (uint32_t)mission_end_mission_end(a,u,weapons_no_registers);}
static es2_hook_descriptor mission_end_hook_descriptors[]={
    {ES2_HOOK_ID(mission_end),mission_end_sig,sizeof(mission_end_sig),6,mission_end_lens,sizeof(mission_end_lens),0,mission_end_mission_end32,NULL,NULL,NULL,0},
};
#endif
