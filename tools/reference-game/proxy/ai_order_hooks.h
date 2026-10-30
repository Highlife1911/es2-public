#ifndef ES2_AI_ORDER_HOOKS_H
#define ES2_AI_ORDER_HOOKS_H
/* AI order helpers (docs/reverse-engineering/mech_order.md, section
 * "AI order helpers"): target_class 411cb4, script_start 413e50, attack_target 41c0f4
 * and order_resume 41eb34. Included after mech_order_hooks.h. Every game
 * callee (the memset thunk 49681a, the AI function 41d5ec, and 413e50,
 * 41c0f4, 41eb34 themselves) goes through the weapons guest bridge and
 * callee observer (weapons_graph) with the original's registers, so the
 * three refuse compare mode; target_class, a pure leaf (reads A+4Dh and
 * the two flag bytes, writes nothing; EAX keeps A's high word, EDX is S or
 * the entry EDX), accepts it with the default comparison. order_resume reads the entry EDX (its table
 * for group order types above 6) from the framework's register slots; no
 * frame local is read before it is written, so no entry shim. ECX at
 * return is outside the contract. No game instruction is embedded. */
#include "es2/core/ai_order.h"
static uint64_t __cdecl ai_order_target_class(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;return es2_target_class_registers(&math_native_memory,a[0],r);
}
static uint64_t __cdecl ai_order_script_start(const uint32_t *a,void *u,const uint32_t *r) {
    (void)r;es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_script_start);
    return es2_script_start_registers(&math_native_memory,&g,a[0],a[1]);
}
static uint64_t __cdecl ai_order_attack_target(const uint32_t *a,void *u,const uint32_t *r) {
    (void)r;es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_attack_target);
    return es2_attack_target_registers(&math_native_memory,&g,a[0],a[1]);
}
static uint64_t __cdecl ai_order_order_resume(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_order_resume);
    return es2_order_resume_registers(&math_native_memory,&g,a[0],r);
}
static const BYTE ai_order_target_class_sig[]={0x55,0x8b,0xec,0x56,0x8b,0x45,0x08,0x8b,0x48,0x4d};
static const BYTE ai_order_script_start_sig[]={0x55,0x8b,0xec,0x53,0x56,0x8b,0x5d,0x08,0x8b,0x45,0x0c};
static const BYTE ai_order_attack_target_sig[]={0x55,0x8b,0xec,0x53,0x56,0x8b,0x4d,0x0c,0x8b,0x45,0x08};
static const BYTE ai_order_order_resume_sig[]={0x55,0x8b,0xec,0x53,0x8b,0x5d,0x08,0x8d,0x83,0x92,0x00,0x00,0x00};
static const BYTE ai_order_load_lens[]={1,2,1,3},ai_order_saved_lens[]={1,2,1,1};
#define AI_ORDER_EXT(name,refuse) static const es2_hook_extension ai_order_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,ai_order_##name,refuse};
#define AI_ORDER_LEGACY(name) static uint32_t __cdecl ai_order_##name##32(const uint32_t *a,void *u) {return (uint32_t)ai_order_##name(a,u,weapons_no_registers);}
AI_ORDER_EXT(target_class,FALSE) AI_ORDER_LEGACY(target_class)
AI_ORDER_EXT(script_start,TRUE) AI_ORDER_LEGACY(script_start)
AI_ORDER_EXT(attack_target,TRUE) AI_ORDER_LEGACY(attack_target)
AI_ORDER_EXT(order_resume,TRUE) AI_ORDER_LEGACY(order_resume)
#undef AI_ORDER_EXT
#undef AI_ORDER_LEGACY
static es2_hook_descriptor ai_order_hook_descriptors[]={
    {ES2_HOOK_ID(target_class),ai_order_target_class_sig,10,7,ai_order_load_lens,4,1,ai_order_target_class32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(script_start),ai_order_script_start_sig,11,5,ai_order_saved_lens,4,2,ai_order_script_start32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(attack_target),ai_order_attack_target_sig,11,5,ai_order_saved_lens,4,2,ai_order_attack_target32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(order_resume),ai_order_order_resume_sig,13,7,ai_order_load_lens,4,1,ai_order_order_resume32,NULL,NULL,NULL,0},
};
/* Callee counters (hk_<hook>_<suffix>, only while the hook is non-original);
 * target_class has no game calls. */
static callee_counter script_start_counters[]={
    {"call_49681a",ES2_COLLIDE_CALL,0x49681a,0},
};
static callee_counter attack_target_counters[]={
    {"call_413e50",ES2_COLLIDE_CALL,0x413e50,0},
    {"call_41d5ec",ES2_COLLIDE_CALL,0x41d5ec,0},
};
static callee_counter order_resume_counters[]={
    {"call_413e50",ES2_COLLIDE_CALL,0x413e50,0},
    {"call_41c0f4",ES2_COLLIDE_CALL,0x41c0f4,0},
    {"call_41eb34",ES2_COLLIDE_CALL,0x41eb34,0},
};
static callee_counter *ai_order_hook_counters(unsigned hook,unsigned *count) {
#define AI_ORDER_COUNTERS(name) if(hook==ES2_HOOK_INDEX_##name) { *count=sizeof(name##_counters)/sizeof(name##_counters[0]);return name##_counters; }
    AI_ORDER_COUNTERS(script_start)
    AI_ORDER_COUNTERS(attack_target)
    AI_ORDER_COUNTERS(order_resume)
#undef AI_ORDER_COUNTERS
    *count=0;return NULL;
}
#endif
