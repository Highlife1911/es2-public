#ifndef ES2_FLIGHT_KEYS_HOOKS_H
#define ES2_FLIGHT_KEYS_HOOKS_H
/* flight_keys group (docs/reverse-engineering/input.md, section
 * "flight-key callback 45a324"): flight_key 45a324, the hot-key
 * callback that the flight-key capture 45b68c registers for the list
 * 49eb0b and that the keyboard handler 477bec calls (cdecl, one slot: the
 * whole key code; plain RET). It updates the key-state arrays 4d2394/
 * 4d23a3/4d23b2, the Alt word 4d23d6 and, in a demo, the quit word 4d2532,
 * which it reaches through the image base in the descriptor's user
 * pointer. No game call and no Win32 function (no graph, no counters,
 * nothing for the input journal). EAX (1 consumed, 2 pass) and EDX are
 * exact; ECX after the hook is the entry ECX (the original returns it
 * with the down flag in its top byte), dead at both callers: the
 * original 477bec pops it at 477ccb, the replaced key_message sets ECX
 * itself after the call. No uninitialized local is read, so no entry
 * shim. Refuses compare mode like the other input hooks. No game
 * instruction is embedded. */
#include "es2/core/control_record.h"
static uint64_t __cdecl flight_keys_flight_key(const uint32_t *a,void *u,const uint32_t *r) {
    (void)r;return es2_flight_key_registers(&math_native_memory,(uint32_t)(uintptr_t)u,a[0]);
}
static callee_counter *flight_keys_hook_counters(unsigned hook,unsigned *count) {
    (void)hook;*count=0;return NULL;
}
/* push ebp; mov ebp,esp; push ecx; push ebx; push esi; push edi; mov edx,[ebp+8] */
static const BYTE flight_keys_flight_key_sig[]={0x55,0x8b,0xec,0x51,0x53,0x56,0x57,0x8b,0x55,0x08};
static const BYTE flight_keys_frame_lens[]={1,2,1,1};
static const es2_hook_extension flight_keys_flight_key_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,flight_keys_flight_key,TRUE};
static uint32_t __cdecl flight_keys_flight_key32(const uint32_t *a,void *u) {return (uint32_t)flight_keys_flight_key(a,u,weapons_no_registers);}
static es2_hook_descriptor flight_keys_hook_descriptors[]={
    {ES2_HOOK_ID(flight_key),flight_keys_flight_key_sig,10,5,flight_keys_frame_lens,4,1,flight_keys_flight_key32,NULL,NULL,NULL,0},
};
#endif
