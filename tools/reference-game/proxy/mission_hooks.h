#ifndef ES2_MISSION_HOOKS_H
#define ES2_MISSION_HOOKS_H
/* Mission runtime group (docs/reverse-engineering/mission_runtime.md).
 * Included after weapons_hooks.h: every game callee goes through its guest
 * bridge and callee observer (weapons_graph), so these hooks refuse compare
 * mode, except region_test (see the extensions). No game instruction
 * is embedded. Other groups append their
 * adapters before the descriptor table and their rows at its end. */
#include "es2/core/mission.h"

/* ---- trigger dispatch and region predicates ------------------------- */
static uint64_t __cdecl mission_region_test(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_region_test);
    return es2_region_test_registers(&math_native_memory,&g,a[0],a[1],r);
}
static uint64_t __cdecl mission_trigger_regions(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_trigger_regions);
    return es2_trigger_regions_registers(&math_native_memory,&g,a[0],a[1],r);
}
/* The framework supplies the entry ECX; its high word is 423508's
 * uninitialized result for an empty group. */
static uint64_t __cdecl mission_trigger_group(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_trigger_group);
    return es2_trigger_group_registers(&math_native_memory,&g,a[0],a[1],r);
}
/* ECX carries the address of the 16 residue bytes captured by the entry
 * shim below (0 on the legacy path: zeros). 423430 never reads its entry
 * ECX, so the core gets 0 there. */
static uint64_t __cdecl mission_trigger_fire(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[32]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_trigger_fire);
    const uint32_t entry[3]={r[0],r[1],0};
    return es2_trigger_fire_registers(&math_native_memory,&g,a[0],r[2],entry);
}
static uint64_t __cdecl mission_trigger_scan(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_trigger_scan);
    return es2_trigger_scan_registers(&math_native_memory,&g,a[0],r);
}
__attribute__((used)) static void *mission_fire_entry_bridge;
/* 423430's event record (EBP-18h) keeps entry ESP-16h..-7 uninitialized and
 * the cockpit handler copies them. Copy the 16 bytes before the framework
 * uses the stack (PUSH EBP gives the original's own EBP, so they are
 * EBP-12h..-3 here), pass their address in ECX and call the framework
 * bridge with the one argument; EAX and EDX pass through. */
__attribute__((naked)) static void mission_fire_entry(void) {
    __asm__ volatile(
        "pushl %ebp\n\tmovl %esp,%ebp\n\tsubl $48,%esp\n\t"
        "movl -18(%ebp),%ecx\n\tmovl %ecx,(%esp)\n\tmovl -14(%ebp),%ecx\n\tmovl %ecx,4(%esp)\n\t"
        "movl -10(%ebp),%ecx\n\tmovl %ecx,8(%esp)\n\tmovl -6(%ebp),%ecx\n\tmovl %ecx,12(%esp)\n\t"
        "movl %esp,%ecx\n\tpushl 8(%ebp)\n\tcall *_mission_fire_entry_bridge\n\t"
        "movl %ebp,%esp\n\tpopl %ebp\n\tret\n\t");
}
/* Shim of a later adapter group (vehicle_hooks.h), defined after this file. */
static BOOL vehicle_leaf_capture_entry(es2_hook *h);
/* Called by weapons_capture_entry after the framework installed its jump:
 * redirect it to the shim and keep h->patch consistent, so uninstall still
 * validates the exact patch (same procedure as the weapons shims). */
/* Shims of later adapter groups (mission_objectives_hooks.h). */
static BOOL objectives_capture_entry(es2_hook *h);
static BOOL mission_capture_entry(es2_hook *h) {
    if(h->descriptor->rva!=0x23430u)return objectives_capture_entry(h);
    DWORD old,ignored;
    if(memcmp(h->site,h->patch,h->descriptor->relocate_size))return FALSE;
    if(!VirtualProtect(h->site,h->descriptor->relocate_size,PAGE_EXECUTE_READWRITE,&old))return FALSE;
    mission_fire_entry_bridge=(void *)(uintptr_t)((uint32_t)(uintptr_t)(h->site+5)+math_native_read32(NULL,(uint32_t)(uintptr_t)(h->patch+1)));
    math_native_write32(NULL,(uint32_t)(uintptr_t)(h->patch+1),(uint32_t)(uintptr_t)mission_fire_entry-(uint32_t)(uintptr_t)(h->site+5));
    memcpy(h->site,h->patch,h->descriptor->relocate_size);
    BOOL flushed=FlushInstructionCache(GetCurrentProcess(),h->site,h->descriptor->relocate_size);
    BOOL protected=VirtualProtect(h->site,h->descriptor->relocate_size,old,&ignored);
    return flushed && protected;
}
static const BYTE mission_trigger_scan_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xc4};
static const BYTE mission_trigger_regions_sig[]={0x55,0x8b,0xec,0x53,0x56,0x57};
static const BYTE mission_region_test_sig[]={0x55,0x8b,0xec,0x53,0x56};
static const BYTE mission_trigger_group_sig[]={0x55,0x8b,0xec,0x51,0x53};
static const BYTE mission_trigger_fire_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xe8};
static const BYTE mission_frame_lens[]={1,2,3},mission_saved_lens[]={1,2,1,1};
#define MISSION_EXT_FLAG(name,unsupported) static const es2_hook_extension mission_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,mission_##name,unsupported};
#define MISSION_EXT(name) MISSION_EXT_FLAG(name,TRUE)
#define MISSION_LEGACY(name) static uint32_t __cdecl mission_##name##32(const uint32_t *a,void *u) {return (uint32_t)mission_##name(a,u,weapons_no_registers);}
/* region_test (a reader; its only callee dist2d_point is pure) accepts compare. */
MISSION_EXT(trigger_scan) MISSION_EXT(trigger_regions) MISSION_EXT_FLAG(region_test,FALSE) MISSION_EXT(trigger_group) MISSION_EXT(trigger_fire)
MISSION_LEGACY(trigger_scan) MISSION_LEGACY(trigger_regions) MISSION_LEGACY(region_test) MISSION_LEGACY(trigger_group) MISSION_LEGACY(trigger_fire)
#undef MISSION_EXT
#undef MISSION_EXT_FLAG
#undef MISSION_LEGACY
/* region_test's compare checks EAX only: on the distance path its EDX is
 * dist2d_point's, which the legacy dist2d_point hook (EAX contract) does
 * not reproduce when that hook is replaced. */
static BOOL __cdecl mission_region_test_equal(const es2_hook_comparison *v,void *u) {
    (void)u;return v->original_result==v->replacement_result;
}
static es2_hook_descriptor mission_hook_descriptors[]={
    {ES2_HOOK_ID(trigger_scan),mission_trigger_scan_sig,6,6,mission_frame_lens,3,1,mission_trigger_scan32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(trigger_regions),mission_trigger_regions_sig,6,5,mission_saved_lens,4,2,mission_trigger_regions32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(region_test),mission_region_test_sig,5,5,mission_saved_lens,4,2,mission_region_test32,mission_region_test_equal,NULL,NULL,0},
    {ES2_HOOK_ID(trigger_group),mission_trigger_group_sig,5,5,mission_saved_lens,4,2,mission_trigger_group32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(trigger_fire),mission_trigger_fire_sig,6,6,mission_frame_lens,3,1,mission_trigger_fire32,NULL,NULL,NULL,0},
};
#endif
