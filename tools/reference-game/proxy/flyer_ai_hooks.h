#ifndef ES2_FLYER_AI_HOOKS_H
#define ES2_FLYER_AI_HOOKS_H
/* flyer_ai group (docs/reverse-engineering/flyer.md, section "the FLYER AI
 * helpers"):
 * the FLYER AI helpers below the state methods, flyer_route 4224c4 (the
 * leader's route step), flyer_altitude 422108 (goal pitch toward a height),
 * flyer_pitch 422098 (pitch command) and flyer_throttle 422260 (the
 * wingman's throttle word); core appended to flyer_states.c. Every game
 * callee (route_point, dist2d_point, route_advance, heading_point,
 * flyer_steer, multiply_q8, vector_angle, heading_cos and this group's own
 * 422108/422098) goes through the weapons guest bridge and callee observer
 * (weapons_graph) with the original's registers, so nested hooks keep their
 * modes; the pure 47e08d/47e0c0 are computed in core. EDX:EAX exact; ECX
 * after each of the four is dead at every caller (flyer.md), so the
 * framework's return of the entry ECX is kept (no exit shim). The bridging
 * hooks flyer_formation, flyer_follow and target_emit reach these addresses
 * through their guest bridges unchanged.
 *
 * Stack residue: flyer_steer reads four bytes at its entry ESP-229h,
 * -23Dh, -20Dh and -221h (its flight model's pose byte, flyer.md "flyer
 * control").
 * 4224c4 calls it at depth 30h after callees that never reach those
 * depths, so in an all-original call they are the bytes at 4224c4's entry
 * ESP-259h, -26Dh, -23Dh, -251h. flyer_route has an ENTRY SHIM
 * (flyer_ai_route_entry, the flyer_control technique)
 * that loads them into AL, AH, DL, DH (entry EAX and EDX are dead) before
 * the framework's dispatch; the adapter plants them at flyer_steer's
 * offsets with flyer_states_stack_call (flyer_states_hooks.h). Without the
 * shim (direct calls) the low words of the entry EAX/EDX are planted.
 * flyer_route refuses compare mode (game calls, the flight model below
 * flyer_steer, the shim). flyer_altitude and flyer_pitch write nothing and
 * call only pure callees, flyer_throttle writes only the word A+21Ch (span
 * on A, 21Ch..234h, so that the clone holds the word A+233h it reads):
 * these three accept compare. No game instruction is embedded; the
 * shim only marshals registers. */
#include "es2/core/flyer_states.h"
/* Callee counters (hk_<hook>_<suffix>), incremented only while the hook is
 * non-original. */
static callee_counter flyer_route_counters[]={
    {"call_423b0c",ES2_COLLIDE_CALL,0x423b0c,0},
    {"call_4928d0",ES2_COLLIDE_CALL,0x4928d0,0},
    {"call_42313c",ES2_COLLIDE_CALL,0x42313c,0},
    {"call_422108",ES2_COLLIDE_CALL,0x422108,0},
    {"call_492934",ES2_COLLIDE_CALL,0x492934,0},
    {"call_4222fc",ES2_COLLIDE_CALL,0x4222fc,0},
};
static callee_counter flyer_altitude_counters[]={
    {"call_47e0a0",ES2_COLLIDE_CALL,0x47e0a0,0},
    {"call_49290c",ES2_COLLIDE_CALL,0x49290c,0},
    {"call_422098",ES2_COLLIDE_CALL,0x422098,0},
};
static callee_counter flyer_pitch_counters[]={
    {"call_47dff0",ES2_COLLIDE_CALL,0x47dff0,0},
};
static callee_counter flyer_throttle_counters[]={
    {"call_47dff0",ES2_COLLIDE_CALL,0x47dff0,0},
};
static callee_counter *flyer_ai_hook_counters(unsigned hook,unsigned *count) {
#define FLYER_AI_COUNTERS(name) if(hook==ES2_HOOK_INDEX_##name) { \
        *count=sizeof(name##_counters)/sizeof(name##_counters[0]);return name##_counters; }
    FLYER_AI_COUNTERS(flyer_route)
    FLYER_AI_COUNTERS(flyer_altitude)
    FLYER_AI_COUNTERS(flyer_pitch)
    FLYER_AI_COUNTERS(flyer_throttle)
#undef FLYER_AI_COUNTERS
    *count=0;return NULL;
}
/* The residue flyer_steer reads: AL, AH, DL, DH of the entry (the shim's
 * bytes from 4224c4's entry ESP-259h, -26Dh, -23Dh, -251h), planted at
 * flyer_steer's entry ESP-229h, -23Dh, -20Dh, -221h. */
static uint64_t __cdecl flyer_ai_flyer_route(const uint32_t *a,void *u,const uint32_t *r) {
    static const uint32_t steer[4]={FS_STEER(0)};
    const BYTE bytes[4]={(BYTE)r[0],(BYTE)(r[0]>>8),(BYTE)r[1],(BYTE)(r[1]>>8)};
    flyer_states_stack stack;memset(&stack,0,sizeof(stack));
    flyer_states_site_set(stack.site,u,0x222fcu,bytes,0,steer,4);
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_flyer_route);
    g.call=flyer_states_guest_call;g.context=&stack;
    return es2_flyer_route_registers(&math_native_memory,&g,a[0],r);
}
static uint64_t __cdecl flyer_ai_flyer_altitude(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_flyer_altitude);
    return es2_flyer_altitude_registers(&math_native_memory,&g,a[0],a[1],r);
}
static uint64_t __cdecl flyer_ai_flyer_pitch(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_flyer_pitch);
    return es2_flyer_pitch_registers(&math_native_memory,&g,a[0],a[1],r);
}
static uint64_t __cdecl flyer_ai_flyer_throttle(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_flyer_throttle);
    return es2_flyer_throttle_registers(&math_native_memory,&g,a[0],a[1],a[2],a[3],a[4],r);
}
/* The entry shim of flyer_route: read the residue before the framework uses
 * the stack (601 = 259h, 621 = 26Dh, 573 = 23Dh, 593 = 251h), then continue
 * into its dispatch. */
static int flyer_ai_route_entry(es2_machine *m,uint32_t bridge) {
    (void)bridge;m->eax=shim_cx(m,m->eax,601u,621u);m->edx=shim_cx(m,m->edx,573u,593u);return 0;
}
ES2_SHIM(flyer_ai_route_shim,0x224c4u,flyer_ai_route_entry,0);
/* 4224c4: push ebp; mov ebp,esp; add esp,-0Ch; push ebx; push esi; push edi; mov ebx,[ebp+8] */
static const BYTE flyer_ai_route_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xf4,0x53,0x56,0x57,0x8b,0x5d,0x08};
/* 422108: push ebp; mov ebp,esp; push ebx; push esi; mov ebx,[ebp+8] */
static const BYTE flyer_ai_altitude_sig[]={0x55,0x8b,0xec,0x53,0x56,0x8b,0x5d,0x08};
/* 422098: push ebp; mov ebp,esp; push ecx; push ebx; push esi; push edi; mov ebx,[ebp+8] */
static const BYTE flyer_ai_pitch_sig[]={0x55,0x8b,0xec,0x51,0x53,0x56,0x57,0x8b,0x5d,0x08};
/* 422260: push ebp; mov ebp,esp; add esp,-0Ch; push ebx; push esi; push edi; mov esi,[ebp+0Ch] */
static const BYTE flyer_ai_throttle_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xf4,0x53,0x56,0x57,0x8b,0x75,0x0c};
static const BYTE flyer_ai_frame_lens[]={1,2,3},flyer_ai_push_lens[]={1,2,1,1};
/* flyer_throttle writes the word A+21Ch and reads A+10h, A+21Ch, A+233h:
 * one span from 21Ch through 234h (the clone covers A..A+234h). */
static const es2_hook_span flyer_ai_throttle_spans[]={{0,0x21c,0x19}};
#define FLYER_AI_EXT(name,refused) \
    static const es2_hook_extension flyer_ai_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,flyer_ai_##name,refused}; \
    static uint32_t __cdecl flyer_ai_##name##32(const uint32_t *a,void *u) {return (uint32_t)flyer_ai_##name(a,u,weapons_no_registers);}
FLYER_AI_EXT(flyer_route,TRUE)
FLYER_AI_EXT(flyer_altitude,FALSE)
FLYER_AI_EXT(flyer_pitch,FALSE)
FLYER_AI_EXT(flyer_throttle,FALSE)
#undef FLYER_AI_EXT
static es2_hook_descriptor flyer_ai_hook_descriptors[]={
    {ES2_HOOK_ID(flyer_route),flyer_ai_route_sig,12,6,flyer_ai_frame_lens,3,1,flyer_ai_flyer_route32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(flyer_altitude),flyer_ai_altitude_sig,8,5,flyer_ai_push_lens,4,2,flyer_ai_flyer_altitude32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(flyer_pitch),flyer_ai_pitch_sig,10,5,flyer_ai_push_lens,4,2,flyer_ai_flyer_pitch32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(flyer_throttle),flyer_ai_throttle_sig,12,6,flyer_ai_frame_lens,3,5,flyer_ai_flyer_throttle32,NULL,NULL,flyer_ai_throttle_spans,1},
};
#endif
