#ifndef ES2_ROUTE_HOOKS_H
#define ES2_ROUTE_HOOKS_H
/* Route group (docs/reverse-engineering/mission_runtime.md, section
 * "Player route callback, mission timers and status check"). Core in
 * core/src/mission.c. Included after the other mission groups: every game
 * callee goes through the weapons guest bridge and callee observer
 * (weapons_graph), so these hooks refuse compare mode. No game instruction
 * is embedded. */
#include "es2/core/mission.h"
#include "sim_bridge.h"

#define ROUTE_ADAPTER1(name) \
static uint64_t __cdecl route_##name(const uint32_t *a,void *u,const uint32_t *r) { \
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_##name); \
    return es2_##name##_registers(&math_native_memory,&g,a[0],r); }
ROUTE_ADAPTER1(player_route) ROUTE_ADAPTER1(route_closing) ROUTE_ADAPTER1(route_advance) ROUTE_ADAPTER1(mission_timer)
#undef ROUTE_ADAPTER1
static uint64_t __cdecl route_timer_scan(const uint32_t *a,void *u,const uint32_t *r) {
    (void)a;es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_timer_scan);
    return es2_timer_scan_registers(&math_native_memory,&g,r);
}
/* 4135e8 keeps force in ESI across all its calls, and 413280 reads the
 * caller's ESI. Same marshalling as weapons_machine_call, plus ESI. */
__attribute__((naked,noinline)) static void __cdecl route_esi_call(
    uint32_t target,const uint32_t *args,unsigned count,es2_weapon_registers *registers,uint32_t esi) {
    __asm__ volatile(
        "pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        ES2_SIM_BRIDGE_ARG8
        "movl 12(%ebp),%esi\n\tmovl 16(%ebp),%ebx\n\tmovl 20(%ebp),%edi\n\t"
        "testl %ebx,%ebx\n\tjz 2f\n\t1: decl %ebx\n\tpushl (%esi,%ebx,4)\n\ttestl %ebx,%ebx\n\tjnz 1b\n\t"
        "2: movl 24(%ebp),%esi\n\tmovl 0(%edi),%eax\n\tmovl 4(%edi),%edx\n\tmovl 8(%edi),%ecx\n\tcall *8(%ebp)\n\t"
        "movl %eax,0(%edi)\n\tmovl %edx,4(%edi)\n\tmovl %ecx,8(%edi)\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
static void route_status_guest_call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    route_esi_call(f,a,n,r,*(const uint32_t *)c);
}
/* ECX carries the address of the 16 residue bytes captured by the entry
 * shim below (0 on the legacy path: zeros). 4135e8's entry ECX only reaches
 * 413280 on the forced path, which passes it to 4137b4 for an empty table
 * only; neither reads it, so the core gets 0 there. */
static uint64_t __cdecl route_status_check(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[32]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_status_check);
    uint32_t esi=a[2];g.call=route_status_guest_call;g.context=&esi;
    const uint32_t entry[3]={r[0],r[1],0};
    return es2_status_check_registers(&math_native_memory,&g,a[0],a[2],r[2],entry);
}
__attribute__((used)) static void *route_status_entry_bridge;
/* 4135e8's event record (EBP-18h) keeps entry ESP-16h..-7 uninitialized
 * and the EventPort copies them. Copy the 16 bytes before the framework
 * uses the stack (PUSH EBP gives the original's own EBP, so they are
 * EBP-12h..-3 here), pass their address in ECX and call the framework
 * bridge with the three arguments; EAX and EDX pass through. */
__attribute__((naked)) static void route_status_entry(void) {
    __asm__ volatile(
        "pushl %ebp\n\tmovl %esp,%ebp\n\tsubl $48,%esp\n\t"
        "movl -18(%ebp),%ecx\n\tmovl %ecx,(%esp)\n\tmovl -14(%ebp),%ecx\n\tmovl %ecx,4(%esp)\n\t"
        "movl -10(%ebp),%ecx\n\tmovl %ecx,8(%esp)\n\tmovl -6(%ebp),%ecx\n\tmovl %ecx,12(%esp)\n\t"
        "movl %esp,%ecx\n\tpushl 16(%ebp)\n\tpushl 12(%ebp)\n\tpushl 8(%ebp)\n\tcall *_route_status_entry_bridge\n\t"
        "movl %ebp,%esp\n\tpopl %ebp\n\tret\n\t");
}
/* Called by vehicle_leaf_capture_entry (end of the shim chain) for RVAs it
 * does not own, after the framework installed its jump: redirect it to the
 * shim and keep h->patch consistent, so uninstall still validates the
 * exact patch. */
static BOOL route_capture_entry(es2_hook *h) {
    if(h->descriptor->rva!=0x135e8u)return projectile_capture_entry(h);
    DWORD old,ignored;
    if(memcmp(h->site,h->patch,h->descriptor->relocate_size))return FALSE;
    if(!VirtualProtect(h->site,h->descriptor->relocate_size,PAGE_EXECUTE_READWRITE,&old))return FALSE;
    route_status_entry_bridge=(void *)(uintptr_t)((uint32_t)(uintptr_t)(h->site+5)+math_native_read32(NULL,(uint32_t)(uintptr_t)(h->patch+1)));
    math_native_write32(NULL,(uint32_t)(uintptr_t)(h->patch+1),(uint32_t)(uintptr_t)route_status_entry-(uint32_t)(uintptr_t)(h->site+5));
    memcpy(h->site,h->patch,h->descriptor->relocate_size);
    BOOL flushed=FlushInstructionCache(GetCurrentProcess(),h->site,h->descriptor->relocate_size);
    BOOL protected=VirtualProtect(h->site,h->descriptor->relocate_size,old,&ignored);
    return flushed && protected;
}
static const BYTE route_player_route_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xf4};
static const BYTE route_route_closing_sig[]={0x55,0x8b,0xec,0x53,0x56,0x57};
static const BYTE route_route_advance_sig[]={0x55,0x8b,0xec,0x53,0x8b,0x45,0x08};
static const BYTE route_timer_scan_sig[]={0x53,0x56,0x8b,0x1d,0x38,0x9e,0x4a,0x00};
static const BYTE route_mission_timer_sig[]={0x55,0x8b,0xec,0x53,0x56,0x57};
static const BYTE route_status_check_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xe8};
static const BYTE route_frame_lens[]={1,2,3},route_saved_lens[]={1,2,1,1},route_load_lens[]={1,2,1,3},route_table_lens[]={1,1,6};
#define ROUTE_EXT(name) static const es2_hook_extension route_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,route_##name,TRUE};
#define ROUTE_LEGACY(name) static uint32_t __cdecl route_##name##32(const uint32_t *a,void *u) {return (uint32_t)route_##name(a,u,weapons_no_registers);}
#define ROUTE_BOTH(name) ROUTE_EXT(name) ROUTE_LEGACY(name)
ROUTE_BOTH(player_route) ROUTE_BOTH(route_closing) ROUTE_BOTH(route_advance)
ROUTE_BOTH(timer_scan) ROUTE_BOTH(mission_timer) ROUTE_BOTH(status_check)
#undef ROUTE_EXT
#undef ROUTE_LEGACY
#undef ROUTE_BOTH
static es2_hook_descriptor route_hook_descriptors[]={
    {ES2_HOOK_ID(player_route),route_player_route_sig,6,6,route_frame_lens,3,1,route_player_route32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(route_closing),route_route_closing_sig,6,5,route_saved_lens,4,1,route_route_closing32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(route_advance),route_route_advance_sig,7,7,route_load_lens,4,1,route_route_advance32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(timer_scan),route_timer_scan_sig,8,8,route_table_lens,3,0,route_timer_scan32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(mission_timer),route_mission_timer_sig,6,5,route_saved_lens,4,1,route_mission_timer32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(status_check),route_status_check_sig,6,6,route_frame_lens,3,3,route_status_check32,NULL,NULL,NULL,0},
};
#endif
