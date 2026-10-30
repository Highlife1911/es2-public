#ifndef ES2_MECH_ORDER_HOOKS_H
#define ES2_MECH_ORDER_HOOKS_H
/* MECH orders (docs/reverse-engineering/mech_order.md): mech_order
 * 420ad4 (the MECH vehicle virtual +28h) and wingman_order 431610 (the
 * cockpit's order to a wingman). Included after weapons_hooks.h: every game
 * callee (target_select, dist_gate, dist2d_point, the AI helpers 411cb4,
 * 41c0f4, 41eb34, 4238a0, 413e50, the list step 471c70, the radio message
 * 420a98, the HUD message cancel 435ac8 and the wingman's virtual +28h) goes
 * through its guest bridge and callee observer (weapons_graph), so both
 * refuse compare mode. No local is read before it is written and no entry
 * register beyond the framework's EAX/EDX/ECX, so no entry shim. ECX at
 * return is outside the contract (mech_order.md). No game instruction is
 * embedded. */
#include "es2/core/mech_order.h"
static uint64_t __cdecl mech_order_mech_order(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_mech_order);
    return es2_mech_order_registers(&math_native_memory,&g,a[0],a[1],a[2],r);
}
static uint64_t __cdecl mech_order_wingman_order(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_wingman_order);
    return es2_wingman_order_registers(&math_native_memory,&g,a[0],a[1],a[2],r);
}
static const BYTE mech_order_mech_order_sig[]={0x55,0x8b,0xec,0x81,0xc4,0x28,0xff,0xff,0xff};
static const BYTE mech_order_wingman_order_sig[]={0x55,0x8b,0xec,0x53,0x56,0x0f};
static const BYTE mech_order_frame_lens[]={1,2,6},mech_order_saved_lens[]={1,2,1,1};
#define MECH_ORDER_EXT(name) static const es2_hook_extension mech_order_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,mech_order_##name,TRUE};
#define MECH_ORDER_LEGACY(name) static uint32_t __cdecl mech_order_##name##32(const uint32_t *a,void *u) {return (uint32_t)mech_order_##name(a,u,weapons_no_registers);}
MECH_ORDER_EXT(mech_order) MECH_ORDER_LEGACY(mech_order)
MECH_ORDER_EXT(wingman_order) MECH_ORDER_LEGACY(wingman_order)
#undef MECH_ORDER_EXT
#undef MECH_ORDER_LEGACY
static es2_hook_descriptor mech_order_hook_descriptors[]={
    {ES2_HOOK_ID(mech_order),mech_order_mech_order_sig,9,9,mech_order_frame_lens,3,3,mech_order_mech_order32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(wingman_order),mech_order_wingman_order_sig,6,5,mech_order_saved_lens,4,3,mech_order_wingman_order32,NULL,NULL,NULL,0},
};
/* Callee counters (hk_<hook>_<suffix>, only while the hook is non-original). */
static callee_counter mech_order_counters[]={
    {"call_411cb4",ES2_COLLIDE_CALL,0x411cb4,0},
    {"call_41c0f4",ES2_COLLIDE_CALL,0x41c0f4,0},
    {"call_411fa0",ES2_COLLIDE_CALL,0x411fa0,0},
    {"call_41eb34",ES2_COLLIDE_CALL,0x41eb34,0},
    {"call_471c70",ES2_COLLIDE_CALL,0x471c70,0},
    {"call_49288c",ES2_COLLIDE_CALL,0x49288c,0},
    {"call_4238a0",ES2_COLLIDE_CALL,0x4238a0,0},
    {"call_4928d0",ES2_COLLIDE_CALL,0x4928d0,0},
    {"call_413e50",ES2_COLLIDE_CALL,0x413e50,0},
    {"call_420a98",ES2_COLLIDE_CALL,0x420a98,0},
};
static callee_counter wingman_order_counters[]={
    {"slot_28",ES2_COLLIDE_SLOT,0x28,0},
    {"call_435ac8",ES2_COLLIDE_CALL,0x435ac8,0},
};
static callee_counter *mech_order_hook_counters(unsigned hook,unsigned *count) {
    if(hook==ES2_HOOK_INDEX_mech_order) { *count=sizeof(mech_order_counters)/sizeof(mech_order_counters[0]);return mech_order_counters; }
    if(hook==ES2_HOOK_INDEX_wingman_order) { *count=sizeof(wingman_order_counters)/sizeof(wingman_order_counters[0]);return wingman_order_counters; }
    *count=0;return NULL;
}
#endif
