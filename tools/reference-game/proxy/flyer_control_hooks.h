#ifndef ES2_FLYER_CONTROL_HOOKS_H
#define ES2_FLYER_CONTROL_HOOKS_H
/* Flyer control group (docs/reverse-engineering/flyer.md, section "flyer
 * control"): flyer_input 41bb9c (the player's Razor), flyer_command 4221a8,
 * flyer_bank 42215c and flyer_steer 4222fc (the AI flyers). Included after
 * flyer_hooks.h: every game call goes through the weapons guest bridge and
 * callee observer; 47e08d/47e0c0 run in core. Compare mode is refused
 * (game calls). The original frames are scratch here.
 *
 * Stack residue: the flight model copies a byte from its entry ESP-191h or
 * ESP-1A5h into the pose byte actor+25h. Below an all-original caller
 * those bytes sit at fixed offsets under the caller's entry ESP that no
 * intermediate callee reaches, so each entry shim reads them (CL/CH; for
 * 4222fc, whose entry ECX high word is live, AL/AH and DL/DH) and the
 * guest call writes them below ESP at the callee's original offsets before
 * the CALL, as weapons_prepare_stack_call does. No game instruction is
 * embedded; the shim only marshals the ABI. */
#include "es2/core/flyer.h"
/* Byte 0 of residue at the callee's entry ESP-first and byte 1 at
 * entry ESP-second. */
static void __cdecl flyer_control_stack_call(uint32_t target,const uint32_t *args,unsigned count,
    es2_weapon_registers *registers,uint32_t residue,uint32_t first,uint32_t second) {
    const es2_machine_plant plants[2]={{first,residue&0xffu},{second,residue>>8&0xffu}};
    weapons_machine_call_at(target,args,count,registers,NULL,0,plants,2,NULL);
}
/* Up to two residue-reading callees per hook: target, two bytes, offsets. */
typedef struct flyer_control_residue { uint32_t target,bytes,first,second; } flyer_control_residue;
typedef struct flyer_control_stack { flyer_control_residue site[2]; } flyer_control_stack;
static void flyer_control_guest_call(void *context,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    const flyer_control_stack *s=context;
    for(unsigned i=0;i<2;++i)if(s->site[i].target==f) {
        flyer_control_stack_call(f,a,n,r,s->site[i].bytes,s->site[i].first,s->site[i].second);return;
    }
    weapons_machine_call(f,a,n,r);
}
#define FLYER_CONTROL_GRAPH(frame_size,index) \
    BYTE frame[(frame_size)+4]={0};es2_weapon_graph g=weapons_graph(u,frame,ES2_HOOK_INDEX_##index); \
    g.call=flyer_control_guest_call;g.context=&stack
static uint64_t __cdecl flyer_control_flyer_input(const uint32_t *a,void *u,const uint32_t *r) {
    flyer_control_stack stack={{{(uint32_t)(uintptr_t)u+0x66b58u,r[2]&0xffffu,0x191u,0x1a5u},{0,0,0,0}}};
    FLYER_CONTROL_GRAPH(0x24,flyer_input);
    return es2_flyer_input_registers(&math_native_memory,&g,a[0],a[1],a[2],a[3],a[4],r);
}
static uint64_t __cdecl flyer_control_flyer_command(const uint32_t *a,void *u,const uint32_t *r) {
    flyer_control_stack stack={{{(uint32_t)(uintptr_t)u+0x66b58u,r[2]&0xffffu,0x191u,0x1a5u},{0,0,0,0}}};
    FLYER_CONTROL_GRAPH(0x14,flyer_command);
    return es2_flyer_command_registers(&math_native_memory,&g,a[0],a[1],r);
}
static uint64_t __cdecl flyer_control_flyer_bank(const uint32_t *a,void *u,const uint32_t *r) {
    flyer_control_stack stack={{{(uint32_t)(uintptr_t)u+0x221a8u,r[2]&0xffffu,0x1c9u,0x1ddu},{0,0,0,0}}};
    FLYER_CONTROL_GRAPH(0,flyer_bank);
    return es2_flyer_bank_registers(&math_native_memory,&g,a[0],a[1],a[2],r);
}
static uint64_t __cdecl flyer_control_flyer_steer(const uint32_t *a,void *u,const uint32_t *r) {
    flyer_control_stack stack={{{(uint32_t)(uintptr_t)u+0x2215cu,r[0]&0xffffu,0x1e1u,0x1f5u},
        {(uint32_t)(uintptr_t)u+0x221a8u,r[1]&0xffffu,0x1c9u,0x1ddu}}};
    FLYER_CONTROL_GRAPH(0x28,flyer_steer);
    return es2_flyer_steer_registers(&math_native_memory,&g,a[0],a[1],a[2],a[3],r);
}
#undef FLYER_CONTROL_GRAPH
/* Entry shims: the residue bytes into CL/CH (steer: AL/AH and DL/DH)
 * before the framework's dispatch. */
static int flyer_control_input_entry(es2_machine *m,uint32_t bridge) {(void)bridge;m->ecx=shim_cx(m,m->ecx,477u,497u);return 0;}
static int flyer_control_command_entry(es2_machine *m,uint32_t bridge) {(void)bridge;m->ecx=shim_cx(m,m->ecx,457u,477u);return 0;}
static int flyer_control_bank_entry(es2_machine *m,uint32_t bridge) {(void)bridge;m->ecx=shim_cx(m,m->ecx,481u,501u);return 0;}
static int flyer_control_steer_entry(es2_machine *m,uint32_t bridge) {
    (void)bridge;m->eax=shim_cx(m,m->eax,553u,573u);m->edx=shim_cx(m,m->edx,525u,545u);return 0;
}
ES2_SHIM(flyer_control_input_shim,0x1bb9cu,flyer_control_input_entry,0);
ES2_SHIM(flyer_control_command_shim,0x221a8u,flyer_control_command_entry,0);
ES2_SHIM(flyer_control_bank_shim,0x2215cu,flyer_control_bank_entry,0);
ES2_SHIM(flyer_control_steer_shim,0x222fcu,flyer_control_steer_entry,0);
static const BYTE flyer_control_input_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xdc};
static const BYTE flyer_control_command_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xec};
static const BYTE flyer_control_bank_sig[]={0x55,0x8b,0xec,0x53,0x56};
static const BYTE flyer_control_steer_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xd8};
static const BYTE flyer_control_frame_lens[]={1,2,3},flyer_control_push_lens[]={1,2,1,1};
#define FLYER_CONTROL_EXT(name) \
    static const es2_hook_extension flyer_control_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,flyer_control_##name,TRUE}; \
    static uint32_t __cdecl flyer_control_##name##32(const uint32_t *a,void *u) {return (uint32_t)flyer_control_##name(a,u,weapons_no_registers);}
FLYER_CONTROL_EXT(flyer_input)
FLYER_CONTROL_EXT(flyer_command)
FLYER_CONTROL_EXT(flyer_bank)
FLYER_CONTROL_EXT(flyer_steer)
#undef FLYER_CONTROL_EXT
static es2_hook_descriptor flyer_control_hook_descriptors[]={
    {ES2_HOOK_ID(flyer_input),flyer_control_input_sig,6,6,flyer_control_frame_lens,3,5,flyer_control_flyer_input32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(flyer_command),flyer_control_command_sig,6,6,flyer_control_frame_lens,3,2,flyer_control_flyer_command32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(flyer_bank),flyer_control_bank_sig,5,5,flyer_control_push_lens,4,3,flyer_control_flyer_bank32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(flyer_steer),flyer_control_steer_sig,6,6,flyer_control_frame_lens,3,4,flyer_control_flyer_steer32,NULL,NULL,NULL,0},
};
#endif
