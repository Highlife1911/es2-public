#ifndef ES2_THROTTLE_HOOKS_H
#define ES2_THROTTLE_HOOKS_H
/* throttle group (docs/reverse-engineering/cockpit.md, section
 * "throttle slider event and shield step"): slider_event 4483a0
 * (slot +8 of the ThrottleVSliderGadget table: the base gadget event, then
 * with a joystick throttle lever the lever direction word 49a06e),
 * slider_base 43904c (that base event: three virtual calls on the gadget)
 * and shield_step 413af8 (the balance step of 413bc8, only reached when the
 * shield_balance hook is original: its replacement computes the step in
 * core). Every game call (43904c under slider_event, the joystick summary
 * 477904, the three gadget virtuals under slider_base) goes through the
 * weapons guest bridge and callee observer (weapons_graph) with the
 * original's registers, so the nested hook keeps its mode; the virtuals are
 * counted by the slot ids 42h (the object at G+42h), 17h (the class table)
 * and 3Eh (the sub-object). No local exists and no entry register is read,
 * so there is NO entry or exit shim. EDX:EAX exact; ECX after each hook is
 * not reproduced (dead at the known callers, cockpit.md). slider_event and
 * slider_base refuse compare mode (game calls); shield_step accepts it:
 * span S[0..5], EAX relative to S and the low word of EDX (the high word
 * is entry residue). No game instruction is embedded. */
#include "es2/core/throttle.h"
static uint64_t __cdecl throttle_slider_event(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_slider_event);
    return es2_slider_event_registers(&math_native_memory,&g,a[0],a[1],r);
}
static uint64_t __cdecl throttle_slider_base(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_slider_base);
    return es2_slider_base_registers(&math_native_memory,&g,a[0],a[1],r);
}
static uint64_t __cdecl throttle_shield_step(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;return es2_shield_step_registers(&math_native_memory,a[0],a[1],r);
}
/* Callee counters (hk_<hook>_<suffix>), incremented only while the hook is
 * non-original. */
static callee_counter slider_event_counters[]={
    {"call_43904c",ES2_COLLIDE_CALL,0x43904c,0},
    {"call_477904",ES2_COLLIDE_CALL,0x477904,0},
};
static callee_counter slider_base_counters[]={
    {"slot_42",ES2_COLLIDE_SLOT,0x42,0},
    {"slot_17",ES2_COLLIDE_SLOT,0x17,0},
    {"slot_3e",ES2_COLLIDE_SLOT,0x3e,0},
};
static callee_counter *throttle_hook_counters(unsigned hook,unsigned *count) {
    if(hook==ES2_HOOK_INDEX_slider_event) { *count=sizeof(slider_event_counters)/sizeof(slider_event_counters[0]);return slider_event_counters; }
    if(hook==ES2_HOOK_INDEX_slider_base) { *count=sizeof(slider_base_counters)/sizeof(slider_base_counters[0]);return slider_base_counters; }
    *count=0;return NULL;
}
/* 4483a0: push ebp; mov ebp,esp; push ebx; mov ebx,[ebp+0Ch]; mov
 * eax,[ebp+8]. Relocated 7 bytes (1,2,1,3). */
static const BYTE throttle_slider_event_sig[]={0x55,0x8b,0xec,0x53,0x8b,0x5d,0x0c,0x8b,0x45,0x08};
/* 43904c: push ebp; mov ebp,esp; push ebx; mov ebx,[ebp+8]; mov
 * eax,[ebp+0Ch]. Relocated 7 bytes (1,2,1,3). */
static const BYTE throttle_slider_base_sig[]={0x55,0x8b,0xec,0x53,0x8b,0x5d,0x08,0x8b,0x45,0x0c};
/* 413af8: push ebp; mov ebp,esp; mov dx,66h; cmp word [ebp+0Ch],0; mov
 * eax,[ebp+8]. Relocated 7 bytes (1,2,4). */
static const BYTE throttle_shield_step_sig[]={0x55,0x8b,0xec,0x66,0xba,0x66,0x00,0x66,0x83,0x7d,0x0c,0x00,0x8b,0x45,0x08};
static const BYTE throttle_push_lens[]={1,2,1,3},throttle_step_lens[]={1,2,4};
#define THROTTLE_EXT(name,unsupported) \
    static const es2_hook_extension throttle_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,throttle_##name,unsupported}; \
    static uint32_t __cdecl throttle_##name##32(const uint32_t *a,void *u) {return (uint32_t)throttle_##name(a,u,weapons_no_registers);}
THROTTLE_EXT(slider_event,TRUE)
THROTTLE_EXT(slider_base,TRUE)
THROTTLE_EXT(shield_step,FALSE)
#undef THROTTLE_EXT
/* Compare of shield_step: the block S[0..5] (front, rear, balance); EAX is S
 * (the clone in the replacement phase); only the low word of EDX is
 * compared, its high word is the entry residue. */
static const es2_hook_span throttle_shield_step_spans[]={{0,0,6}};
static BOOL __cdecl throttle_shield_step_equal(const es2_hook_comparison *v,void *u) {
    (void)u;const uint32_t o=(uint32_t)(uintptr_t)v->original[0],r=(uint32_t)(uintptr_t)v->replacement[0];
    return !memcmp(v->original[0],v->replacement[0],6)&&v->original_result==o&&v->replacement_result==r&&
        (uint16_t)v->original_edx==(uint16_t)v->replacement_edx;
}
static es2_hook_descriptor throttle_hook_descriptors[]={
    {ES2_HOOK_ID(slider_event),throttle_slider_event_sig,10,7,throttle_push_lens,4,2,throttle_slider_event32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(slider_base),throttle_slider_base_sig,10,7,throttle_push_lens,4,2,throttle_slider_base32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(shield_step),throttle_shield_step_sig,15,7,throttle_step_lens,3,2,throttle_shield_step32,throttle_shield_step_equal,NULL,throttle_shield_step_spans,1},
};
#endif
