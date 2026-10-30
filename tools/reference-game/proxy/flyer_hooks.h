#ifndef ES2_FLYER_HOOKS_H
#define ES2_FLYER_HOOKS_H
/* flyer group (docs/reverse-engineering/flyer.md): flight_model
 * 466b58, the flight model of the player's Razor and the AI flyers.
 * Included after weapons_hooks.h: the platform function 477904 and the
 * hooked leaves go through its guest bridge and callee observer
 * (weapons_graph); the private leaves 466a88/466ae0/466b20/467b28 and the
 * pure helpers 47e08d/47e0c0/47f058 run in core. Compare mode is refused
 * (game calls). The original's 1C0h-byte frame is scratch here. An entry
 * shim hands the two stack residue bytes at entry ESP-191h and ESP-1A5h
 * (bytes 19 of the local matrices N and D, one of which the original
 * copies into the pose byte actor+25h) to the replacement in CL and CH;
 * 466b58 never reads its entry ECX (477904 receives it and ignores it).
 * No game instruction is embedded. */
#include "es2/core/flyer.h"
static uint64_t __cdecl flyer_flight_model(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE frame[0x1c0]={0};es2_weapon_graph g=weapons_graph(u,frame,ES2_HOOK_INDEX_flight_model);
    g.padding=(uint8_t)r[2];g.composed_padding=(uint8_t)(r[2]>>8);
    return es2_flight_model_registers(&math_native_memory,&g,a[0],a[1],a[2],a[3],r);
}
/* 466b58: copy the two residue bytes into CL/CH and continue into the
 * framework's dispatch (installed by flyer_capture_entry). */
__attribute__((used)) static void *flyer_entry_bridge;
__attribute__((naked)) static void flyer_entry(void) {
    __asm__ volatile("movb -401(%esp),%cl\n\tmovb -421(%esp),%ch\n\tjmp *_flyer_entry_bridge\n\t");
}
/* Called by vehicle_leaf_capture_entry (end of the shim chain) after the
 * framework installed its jump: redirect it to the shim and keep h->patch
 * consistent, so uninstall still validates the exact patch. */
/* The flyer control shims (flyer_control_hooks.h) continue the chain. */
static BOOL flyer_control_capture_entry(es2_hook *h);
static BOOL flyer_capture_entry(es2_hook *h) {
    if(h->descriptor->rva!=0x66b58u)return flyer_control_capture_entry(h);
    DWORD old,ignored;
    if(memcmp(h->site,h->patch,h->descriptor->relocate_size))return FALSE;
    if(!VirtualProtect(h->site,h->descriptor->relocate_size,PAGE_EXECUTE_READWRITE,&old))return FALSE;
    flyer_entry_bridge=(void *)(uintptr_t)((uint32_t)(uintptr_t)(h->site+5)+math_native_read32(NULL,(uint32_t)(uintptr_t)(h->patch+1)));
    math_native_write32(NULL,(uint32_t)(uintptr_t)(h->patch+1),(uint32_t)(uintptr_t)flyer_entry-(uint32_t)(uintptr_t)(h->site+5));
    memcpy(h->site,h->patch,h->descriptor->relocate_size);
    BOOL flushed=FlushInstructionCache(GetCurrentProcess(),h->site,h->descriptor->relocate_size);
    BOOL protected=VirtualProtect(h->site,h->descriptor->relocate_size,old,&ignored);
    return flushed && protected;
}
static const BYTE flyer_flight_model_sig[]={0x55,0x8b,0xec,0x81,0xc4,0x40,0xfe,0xff,0xff};
static const BYTE flyer_frame_lens[]={1,2,6};
static const es2_hook_extension flyer_flight_model_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,flyer_flight_model,TRUE};
static uint32_t __cdecl flyer_flight_model32(const uint32_t *a,void *u) {return (uint32_t)flyer_flight_model(a,u,weapons_no_registers);}
static es2_hook_descriptor flyer_hook_descriptors[]={
    {ES2_HOOK_ID(flight_model),flyer_flight_model_sig,9,9,flyer_frame_lens,3,4,flyer_flight_model32,NULL,NULL,NULL,0},
};
#endif
