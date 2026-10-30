#ifndef ES2_SHIELD_HOOKS_H
#define ES2_SHIELD_HOOKS_H
/* shield group (docs/reverse-engineering/cockpit.md, sections
 * "0x413bc8(S) contract (simulation)" and "shield balance as own
 * code"): shield_balance 413bc8, called once per rendered frame from the
 * cockpit/player synchronisation 41b130 with S = player+222h. The getter
 * 443908 and the balance step 413af8 are computed in core (the step is
 * counted as call_413af8 without a game call); the gauge setter 443880
 * goes through the weapons guest bridge and callee observer with the
 * original's registers, so it stays original code. The 15-byte record it
 * receives by pointer lives in the adapter's scratch. No local is read
 * before it is written and no entry register is read, so no entry shim.
 * EDX and ECX after the hook are dead at the only caller (cockpit.md).
 * Refuses compare mode. No game instruction is embedded. */
#include "es2/core/shield.h"
static uint64_t __cdecl shield_shield_balance(const uint32_t *a,void *u,const uint32_t *r) {
    (void)r;BYTE scratch[16]={0};
    es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_shield_balance);
    return es2_shield_balance_registers(&math_native_memory,&g,a[0]);
}
/* Callee counters (hk_shield_balance_<suffix>), incremented only while the
 * hook is non-original. */
static callee_counter shield_balance_counters[]={
    {"call_443880",ES2_COLLIDE_CALL,0x443880,0},
    {"call_413af8",ES2_COLLIDE_CALL,0x413af8,0},
};
static callee_counter *shield_hook_counters(unsigned hook,unsigned *count) {
    if(hook==ES2_HOOK_INDEX_shield_balance) { *count=sizeof(shield_balance_counters)/sizeof(shield_balance_counters[0]);return shield_balance_counters; }
    *count=0;return NULL;
}
/* push ebp; mov ebp,esp; add esp,-10h; mov eax,[49b088h] */
static const BYTE shield_balance_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xf0,0xa1,0x88,0xb0,0x49,0x00};
static const BYTE shield_frame_lens[]={1,2,3};
static const es2_hook_extension shield_shield_balance_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,shield_shield_balance,TRUE};
static uint32_t __cdecl shield_shield_balance32(const uint32_t *a,void *u) {return (uint32_t)shield_shield_balance(a,u,weapons_no_registers);}
static es2_hook_descriptor shield_hook_descriptors[]={
    {ES2_HOOK_ID(shield_balance),shield_balance_sig,11,6,shield_frame_lens,3,1,shield_shield_balance32,NULL,NULL,NULL,0},
};
#endif
