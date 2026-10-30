#ifndef ES2_MOUSE_EVENTS_HOOKS_H
#define ES2_MOUSE_EVENTS_HOOKS_H
/* mouse_events group (docs/reverse-engineering/input.md, section
 * "mouse event queue 452d40 / 453078"): mouse_dispatch 452d40 (the
 * per-call dispatch of the mouse event list to the active GUI screen,
 * called by control_record 45a810) and mouse_queue 453078 (the append,
 * called by the mouse callback 452cdc and by 45a810's tape playback).
 * Core in core/src/mouse_events.c. Every game callee of 452d40 (486e7c,
 * 4523b0, 45297c, 4527c8, 452898, 452ab8 and the gadget virtuals +8,
 * +10h, +18h) goes through the weapons guest bridge and callee observer
 * with the original's EAX/EDX/ECX; 453078 has no callee. Both refuse
 * compare mode (452d40 calls game code, 453078 appends to a list).
 *
 * 452d40's ECX at RET can reach the modal loops' screen input virtual
 * through 45a810, so an entry shim (chained from
 * lc_base_capture_entry) calls the
 * framework's dispatch and, if the replacement ran, loads the ECX the core
 * computed, as control_record does. 453078's ECX is dead at both callers
 * (input.md); the adapter keeps the core's ECX for the in-vitro check
 * only. No game instruction is embedded. */
#include "es2/core/mouse_events.h"
/* Set by the dispatch adapter, consumed by the shim; a nested invocation
 * sets and consumes its own value first. */
__attribute__((used)) static uint32_t mouse_events_ecx,mouse_events_ecx_valid;
static uint32_t mouse_events_queue_ecx;
static uint64_t __cdecl mouse_events_mouse_dispatch(const uint32_t *a,void *u,const uint32_t *r) {
    (void)a;BYTE frame[ES2_MOUSE_DISPATCH_SCRATCH]={0};uint32_t ecx;
    es2_weapon_graph g=weapons_graph(u,frame,ES2_HOOK_INDEX_mouse_dispatch);
    uint64_t v=es2_mouse_dispatch_registers(&math_native_memory,&g,&ecx,r);
    mouse_events_ecx=ecx;mouse_events_ecx_valid=1;
    return v;
}
static uint64_t __cdecl mouse_events_mouse_queue(const uint32_t *a,void *u,const uint32_t *r) {
    uint32_t ecx;
    uint64_t v=es2_mouse_queue_registers(&math_native_memory,(uint32_t)(uintptr_t)u,a[0],a[1],&ecx,r);
    mouse_events_queue_ecx=ecx;
    return v;
}
/* Callee counters (hk_mouse_dispatch_<suffix>), incremented only while the
 * hook is non-original. Keys: the callee VA, or the vtable slot. */
static callee_counter mouse_dispatch_counters[]={
    {"call_486e7c",ES2_COLLIDE_CALL,0x486e7c,0},
    {"call_4523b0",ES2_COLLIDE_CALL,0x4523b0,0},
    {"call_45297c",ES2_COLLIDE_CALL,0x45297c,0},
    {"call_4527c8",ES2_COLLIDE_CALL,0x4527c8,0},
    {"call_452898",ES2_COLLIDE_CALL,0x452898,0},
    {"call_452ab8",ES2_COLLIDE_CALL,0x452ab8,0},
    {"slot_18",ES2_COLLIDE_SLOT,0x18,0},
    {"slot_10",ES2_COLLIDE_SLOT,0x10,0},
    {"slot_08",ES2_COLLIDE_SLOT,0x08,0},
};
static callee_counter *mouse_events_hook_counters(unsigned hook,unsigned *count) {
    if(hook==ES2_HOOK_INDEX_mouse_dispatch) { *count=sizeof(mouse_dispatch_counters)/sizeof(mouse_dispatch_counters[0]);return mouse_dispatch_counters; }
    *count=0;return NULL;
}
/* 452d40: push ebx; push esi; push edi; push ebp; add esp,-18h; mov esi,4d1de3h
 * 453078: push ebp; mov ebp,esp; mov eax,[4d1e04h]; cmp dword [eax],63h */
static const BYTE mouse_events_dispatch_sig[]={0x53,0x56,0x57,0x55,0x83,0xc4,0xe8,0xbe,0xe3,0x1d,0x4d,0x00};
static const BYTE mouse_events_dispatch_lens[]={1,1,1,1,3};
static const BYTE mouse_events_queue_sig[]={0x55,0x8b,0xec,0xa1,0x04,0x1e,0x4d,0x00,0x83,0x38,0x63};
static const BYTE mouse_events_queue_lens[]={1,2,5};
#define MOUSE_EVENTS_EXT(name) \
    static const es2_hook_extension mouse_events_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,mouse_events_##name,TRUE}; \
    static uint32_t __cdecl mouse_events_##name##32(const uint32_t *a,void *u) {return (uint32_t)mouse_events_##name(a,u,weapons_no_registers);}
MOUSE_EVENTS_EXT(mouse_dispatch)
MOUSE_EVENTS_EXT(mouse_queue)
#undef MOUSE_EVENTS_EXT
static es2_hook_descriptor mouse_events_hook_descriptors[]={
    {ES2_HOOK_ID(mouse_dispatch),mouse_events_dispatch_sig,12,7,mouse_events_dispatch_lens,5,0,mouse_events_mouse_dispatch32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(mouse_queue),mouse_events_queue_sig,11,8,mouse_events_queue_lens,3,2,mouse_events_mouse_queue32,NULL,NULL,NULL,0},
};
/* The entry shim of mouse_dispatch: the framework's dispatch as a call (no
 * arguments), then the core's ECX if the replacement ran; in original mode
 * the original's own ECX stays. EAX and EDX pass through. */
__attribute__((used)) static void *mouse_events_bridge;
__attribute__((naked)) static void mouse_events_entry(void) {
    __asm__ volatile(
        "movl $0,_mouse_events_ecx_valid\n\tcall *_mouse_events_bridge\n\t"
        "cmpl $0,_mouse_events_ecx_valid\n\tje 1f\n\t"
        "movl _mouse_events_ecx,%ecx\n\tmovl $0,_mouse_events_ecx_valid\n\t"
        "1: ret\n\t");
}
/* Called by lc_base_capture_entry after the framework installed its
 * jump: redirect mouse_dispatch's jump to the shim and keep h->patch
 * consistent, so uninstall still validates the exact patch. The chain
 * continues in flyer_part_capture_entry (flyer_part_hooks.h). */
static BOOL flyer_part_capture_entry(es2_hook *h);
static BOOL mouse_events_capture_entry(es2_hook *h) {
    if(h->descriptor->rva!=0x52d40u)return flyer_part_capture_entry(h);
    DWORD old,ignored;
    if(memcmp(h->site,h->patch,h->descriptor->relocate_size))return FALSE;
    if(!VirtualProtect(h->site,h->descriptor->relocate_size,PAGE_EXECUTE_READWRITE,&old))return FALSE;
    mouse_events_bridge=(void *)(uintptr_t)((uint32_t)(uintptr_t)(h->site+5)+math_native_read32(NULL,(uint32_t)(uintptr_t)(h->patch+1)));
    math_native_write32(NULL,(uint32_t)(uintptr_t)(h->patch+1),(uint32_t)(uintptr_t)mouse_events_entry-(uint32_t)(uintptr_t)(h->site+5));
    memcpy(h->site,h->patch,h->descriptor->relocate_size);
    BOOL flushed=FlushInstructionCache(GetCurrentProcess(),h->site,h->descriptor->relocate_size);
    BOOL protected=VirtualProtect(h->site,h->descriptor->relocate_size,old,&ignored);
    return flushed && protected;
}
#endif
