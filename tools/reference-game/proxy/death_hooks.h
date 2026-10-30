#ifndef ES2_DEATH_HOOKS_H
#define ES2_DEATH_HOOKS_H
/* death group (docs/reverse-engineering/main_loop.md, section "death
 * sequence 45fa84"): death_sequence 45fa84, step 18 of the logic
 * frame 45f570, entered every frame while the player's byte +99h is set.
 * Every game callee (the display request 42a3f4, the view subject setter
 * 45df34, the display start 42a4c4, the view update 45de30, the outside
 * camera control 401c74 and timer_dword_step 467aa8) goes through the
 * weapons guest bridge and callee observer with the original's registers,
 * so the nested hook keeps its mode and the presentation callees run as
 * original code. The entry ECX reaches 401c74's argument high words, so
 * the adapter passes the entry registers. The original's 8-byte frame
 * holds only the two attachment words; the core keeps them in registers.
 * EDX and ECX after the hook are dead at the only caller (main_loop.md).
 * Refuses compare mode. No game instruction is embedded. */
#include "es2/core/death.h"
static uint64_t __cdecl death_death_sequence(const uint32_t *a,void *u,const uint32_t *r) {
    (void)a;es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_death_sequence);
    return es2_death_sequence_registers(&math_native_memory,&g,r);
}
/* Callee counters (hk_death_sequence_<suffix>), incremented only while the
 * hook is non-original. */
static callee_counter death_sequence_counters[]={
    {"call_42a3f4",ES2_COLLIDE_CALL,0x42a3f4,0},
    {"call_45df34",ES2_COLLIDE_CALL,0x45df34,0},
    {"call_42a4c4",ES2_COLLIDE_CALL,0x42a4c4,0},
    {"call_45de30",ES2_COLLIDE_CALL,0x45de30,0},
    {"call_401c74",ES2_COLLIDE_CALL,0x401c74,0},
    {"call_467aa8",ES2_COLLIDE_CALL,0x467aa8,0},
};
static callee_counter *death_hook_counters(unsigned hook,unsigned *count) {
    if(hook==ES2_HOOK_INDEX_death_sequence) { *count=sizeof(death_sequence_counters)/sizeof(death_sequence_counters[0]);return death_sequence_counters; }
    *count=0;return NULL;
}
/* push ebx; push esi; add esp,-8; mov esi,4d24bch */
static const BYTE death_sequence_sig[]={0x53,0x56,0x83,0xc4,0xf8,0xbe,0xbc,0x24,0x4d,0x00};
static const BYTE death_frame_lens[]={1,1,3};
static const es2_hook_extension death_death_sequence_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,death_death_sequence,TRUE};
static uint32_t __cdecl death_death_sequence32(const uint32_t *a,void *u) {return (uint32_t)death_death_sequence(a,u,weapons_no_registers);}
static es2_hook_descriptor death_hook_descriptors[]={
    {ES2_HOOK_ID(death_sequence),death_sequence_sig,10,5,death_frame_lens,3,0,death_death_sequence32,NULL,NULL,NULL,0},
};
#endif
