#ifndef ES2_KEYBOARD_HOOKS_H
#define ES2_KEYBOARD_HOOKS_H
/* keyboard group (docs/reverse-engineering/input.md, section "keyboard
 * message handler 477bec"): key_message 477bec, the handler that
 * the window procedure 466034 offers every message (stdcall, four slots,
 * RET 10h). Its game calls go through the weapons guest bridge and callee
 * observer with the original's registers: GetKeyState through the import
 * thunk 49695e, so the proxy's input journal records and replays the same
 * calls in the same order; the focus helper 4669b4 (display-mode switch
 * on Alt+Tab, Ctrl+Esc, Alt+Esc) and the hot-key callbacks of the table
 * 6bb8b8 (the flight keys 45a324, the command-key queue 45a498 and the
 * empty 477a7c) run as original code. The bridge restores ESP after the
 * stdcall thunk. EAX and EDX are exact; ECX after the hook is the entry
 * ECX (the original returns its loop counter there on the hot-key path),
 * dead at the only caller 466056. No entry shim: no frame local is read
 * before it is written. Refuses compare mode (writes the ring and calls
 * game code). No game instruction is embedded. */
#include "es2/core/keyboard.h"
static uint64_t __cdecl keyboard_key_message(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_key_message);
    return es2_key_message_registers(&math_native_memory,&g,a[0],a[1],a[2],a[3],r);
}
/* Callee counters (hk_key_message_<suffix>), incremented only while the
 * hook is non-original; callbacks are keyed by the target read from the
 * entry. */
static callee_counter key_message_counters[]={
    {"call_49695e",ES2_COLLIDE_CALL,0x49695e,0},
    {"call_4669b4",ES2_COLLIDE_CALL,0x4669b4,0},
    {"call_45a324",ES2_COLLIDE_CALL,0x45a324,0},
    {"call_45a498",ES2_COLLIDE_CALL,0x45a498,0},
    {"call_477a7c",ES2_COLLIDE_CALL,0x477a7c,0},
};
static callee_counter *keyboard_hook_counters(unsigned hook,unsigned *count) {
    if(hook==ES2_HOOK_INDEX_key_message) { *count=sizeof(key_message_counters)/sizeof(key_message_counters[0]);return key_message_counters; }
    *count=0;return NULL;
}
/* push ebp; mov ebp,esp; push ecx; push ebx; push esi; push edi; mov edx,[ebp+10h] */
static const BYTE keyboard_key_message_sig[]={0x55,0x8b,0xec,0x51,0x53,0x56,0x57,0x8b,0x55,0x10};
static const BYTE keyboard_frame_lens[]={1,2,1,1};
static const es2_hook_extension keyboard_key_message_extension={ES2_ABI_STDCALL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,keyboard_key_message,TRUE};
static uint32_t __cdecl keyboard_key_message32(const uint32_t *a,void *u) {return (uint32_t)keyboard_key_message(a,u,weapons_no_registers);}
static es2_hook_descriptor keyboard_hook_descriptors[]={
    {ES2_HOOK_ID(key_message),keyboard_key_message_sig,10,5,keyboard_frame_lens,4,4,keyboard_key_message32,NULL,NULL,NULL,0},
};
#endif
