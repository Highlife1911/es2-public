#ifndef ES2_ROUTE_HOOKS_H
#define ES2_ROUTE_HOOKS_H
/* Route group (docs/reverse-engineering/mission_runtime.md, section
 * "Player route callback, mission timers and status check"). Core in
 * core/src/mission.c. Included after the other mission groups: every game
 * callee goes through the weapons guest bridge and callee observer
 * (weapons_graph), so these hooks refuse compare mode. No game instruction
 * is embedded. */
#include "es2/core/mission.h"

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
static void __cdecl route_esi_call(uint32_t target,const uint32_t *args,unsigned count,es2_weapon_registers *registers,
    uint32_t esi) {
    const uint32_t frame[4]={0,esi,0,0};
    weapons_machine_call_at(target,args,count,registers,frame,0,NULL,0,NULL);
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
/* 4135e8's event record (EBP-18h) keeps entry ESP-16h..-7 uninitialized
 * and the EventPort copies them. Copy the 16 bytes before the framework
 * uses the stack (after PUSH EBP, EBP-12h..-3), pass their address in ECX
 * and call the framework with the three arguments; EAX and EDX pass
 * through. */
static int route_status_entry(es2_machine *m,uint32_t bridge) {
    const uint32_t ebp=shim_frame(m),esp=ebp-48u;
    for(uint32_t i=0;i<16u;i+=4u)shim_w32(esp+i,shim_r32(ebp-18u+i));
    const uint32_t args[3]={shim_r32(ebp+8u),shim_r32(ebp+12u),shim_r32(ebp+16u)};
    return shim_call(m,bridge,ES2_MACHINE_CDECL,ebp,esp,esp,args,3);
}
ES2_SHIM(route_status_shim,0x135e8u,route_status_entry,0);
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
