#ifndef ES2_ACTOR_STEP_HOOKS_H
#define ES2_ACTOR_STEP_HOOKS_H
#include "guest_native.h"
/* actor_step group (docs/reverse-engineering/vtables.md, section
 * "actor_step"): actor_step 411cec, the row-11 per-member step that
 * order_update calls for every group member; state_frame 413eb0; and the
 * update-slot trampolines of MECH (mech_move 415afc, mech_act 415b38,
 * mech_decide 415b74) and FLYER (flyer_move_call 4217fc, flyer_act
 * 42184c, flyer_decide 421888), which call a member pointer of the actor's
 * AI state record. Core in core/src/actor_step.c.
 *
 * Every game call (the update slots through the actor's vtable, the state
 * methods through the member pointers, timer_dword_step) goes through the
 * callee observer (weapons_graph) with the original's registers, so the
 * nested hooks keep their own modes; all eight refuse compare mode.
 *
 * Stack coordinate. These functions sit directly above the AI state
 * methods, whose subtrees read uninitialized stack (the weapon bodies
 * below aim_fire, the emitters, 46ff90; weapons.md, ai.md "stack residue
 * under aim_fire").
 * So the seven hooks with game calls run them where the original does:
 * an entry shim per hook keeps the original's frame (EBP, the saved EBX,
 * the trampolines' locals, which the core writes), reserves a gap below
 * it without writing there (when the gap reaches below the thread's
 * committed stack, TIB StackLimit at FS:8, the pages are probed one by one
 * with OR 0, which keeps their bytes), puts {EBP, ESI, EDI, ECX} below
 * the gap and calls the framework's dispatch there. The adapter takes that context and routes every game
 * call through ai_behaviour_stack_call: ESP = the original's outgoing
 * coordinate, the EBX/ESI/EDI/EBP the original holds at that call, and
 * the original's return address left in the slot below the argument. The
 * callees therefore see the same stack bytes and saved registers whatever
 * mode these hooks are in. The core's ECX goes back through the context:
 * the shims return the original's ECX (dead at 411cec's call sites and at
 * order_update, but exact). Gaps: 80 pages (320 KiB) under a trampoline,
 * enough for a state method's subtree with aim_fire's 256-KiB gap and its
 * host frames; 84 pages under actor_step, enough for a trampoline frame,
 * its gap and its host frames. Without the shim (direct and legacy calls)
 * the adapters use the plain bridge and their own frame.
 *
 * actor_step computes 413eb0 in core (its leaf) and writes the three
 * stack dwords its call leaves, so a replaced actor_step does not enter
 * the state_frame hook. state_frame has no shim (no game call; its ECX at
 * return is dead: 411cec pops it). actor_step's shim keeps the entry
 * EAX/EDX/ECX for the framework (the early exit returns them; ECX reaches
 * slot +1Ch); the trampolines' entry registers are dead (each writes EAX,
 * EDX and ECX before reading them), so their shims use EAX as scratch.
 * The shims only move ESP and load and store registers; no game
 * instruction is embedded. */
#include "es2/core/actor_step.h"
#define ACTOR_STEP_PAGES 84
#define ACTOR_STEP_MEMBER_PAGES 80
typedef struct actor_step_stack { uint32_t ebp,esi,edi,ecx; } actor_step_stack;
typedef struct actor_step_guest { const actor_step_stack *stack; uint32_t top; es2_actor_call_frame frame; } actor_step_guest;
/* The context of the innermost shim, taken by the adapter at its start. */
__attribute__((used)) static actor_step_stack *actor_step_pending;
static void actor_step_stack_guest_call(void *context,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    const actor_step_guest *c=context;
    const uint32_t call[6]={c->frame.ebx,c->frame.esi,c->frame.edi,c->stack->ebp,c->top,c->frame.ret};
    ai_behaviour_stack_call(f,a,n,r,call);
}
/* The context belongs to this invocation only if the argument above the
 * shim's EBP is this call's. */
static actor_step_stack *actor_step_take(uint32_t arg) {
    actor_step_stack *s=actor_step_pending;actor_step_pending=NULL;
    return s&&math_native_read32(NULL,s->ebp+8u)==arg?s:NULL;
}
static void actor_step_bind(es2_weapon_graph *g,actor_step_guest *c,const actor_step_stack *s,uint32_t frame,uint32_t top) {
    c->stack=s;c->top=top;c->frame.esi=s->esi;c->frame.edi=s->edi;
    g->scratch=frame;g->call=actor_step_stack_guest_call;g->context=c;
}
/* The original's frame: EBP = S-4 for entry ESP S, EBX saved at EBP-4,
 * outgoing ESP EBP-4. Without the shim the frame is a local stand-in. */
static uint64_t __cdecl actor_step_actor_step(const uint32_t *a,void *u,const uint32_t *r) {
    actor_step_stack *s=actor_step_take(a[0]);
    BYTE frame[16]={0};es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_actor_step);
    actor_step_guest c={NULL,0,{0,0,0,0}};g.scratch=(uint32_t)(uintptr_t)(frame+16);
    if(s)actor_step_bind(&g,&c,s,s->ebp,s->ebp-4u);
    uint32_t ecx;uint64_t v=es2_actor_step_registers(&math_native_memory,&g,&c.frame,a[0],r,&ecx);
    if(s)s->ecx=ecx;
    return v;
}
static uint64_t __cdecl actor_step_state_frame(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;(void)r;uint32_t ecx;return es2_state_frame_registers(&math_native_memory,a[0],&ecx);
}
/* Trampolines: locals at EBP-0Ch (EBP-18h for 4217fc), EBX saved below
 * them, outgoing ESP EBP-10h (EBP-1Ch). */
#define ACTOR_STEP_MEMBER(name,member,ret) \
static uint64_t __cdecl actor_step_##name(const uint32_t *a,void *u,const uint32_t *r) { \
    (void)r;actor_step_stack *s=actor_step_take(a[0]); \
    BYTE frame[12]={0};es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_##name); \
    actor_step_guest c={NULL,0,{0,0,0,0}};g.scratch=(uint32_t)(uintptr_t)frame; \
    if(s)actor_step_bind(&g,&c,s,s->ebp-0xcu,s->ebp-0x10u); \
    uint32_t ecx;uint64_t v=es2_state_member_registers(&math_native_memory,&g,&c.frame,a[0],member,ret,&ecx); \
    if(s)s->ecx=ecx; \
    return v; }
ACTOR_STEP_MEMBER(mech_move,0x24u,0x415b2du)
ACTOR_STEP_MEMBER(mech_act,0x18u,0x415b69u)
ACTOR_STEP_MEMBER(mech_decide,0x30u,0x415ba5u)
ACTOR_STEP_MEMBER(flyer_act,0x18u,0x42187du)
ACTOR_STEP_MEMBER(flyer_decide,0x30u,0x4218b9u)
#undef ACTOR_STEP_MEMBER
static uint64_t __cdecl actor_step_flyer_move_call(const uint32_t *a,void *u,const uint32_t *r) {
    (void)r;actor_step_stack *s=actor_step_take(a[0]);
    BYTE frame[24]={0};es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_flyer_move_call);
    actor_step_guest c={NULL,0,{0,0,0,0}};g.scratch=(uint32_t)(uintptr_t)frame;
    if(s)actor_step_bind(&g,&c,s,s->ebp-0x18u,s->ebp-0x1cu);
    uint32_t ecx;uint64_t v=es2_flyer_member_registers(&math_native_memory,&g,&c.frame,a[0],&ecx);
    if(s)s->ecx=ecx;
    return v;
}
/* The shims' call: the context {EBP, ESI, EDI, entry ECX} at `context`
 * (below the gap), the argument copied for the dispatch, then ECX from the
 * context (the adapter's). */
static int actor_step_call(es2_machine *m,uint32_t bridge,uint32_t ebp,uint32_t context) {
    shim_w32(context,ebp);shim_w32(context+4u,m->esi);shim_w32(context+8u,m->edi);shim_w32(context+12u,m->ecx);
    actor_step_pending=es2_guest_span(context,sizeof(actor_step_stack));
    const uint32_t args[1]={shim_r32(ebp+8u)};
    es2_machine c=*m;c.ebp=ebp;c.esp=context;
    (void)es2_machine_call(&c,bridge,ES2_MACHINE_CDECL,args,1);
    c.ecx=shim_r32(context+12u);actor_step_pending=NULL;
    return shim_return(m,&c,ebp+4u,ebp);
}
/* Entry shims. actor_step: PUSH EBP and PUSH EBX as the original's
 * prologue, the gap (EAX/EDX/ECX are live and pass through). */
static int actor_step_actor_step_entry(es2_machine *m,uint32_t bridge) {
    const uint32_t ebp=shim_frame(m);shim_w32(ebp-4u,m->ebx);
    return actor_step_call(m,bridge,ebp,shim_gap(bridge,ebp-4u,ACTOR_STEP_PAGES)-16u);
}
/* Trampolines: PUSH EBP, the locals (written by the core) and PUSH EBX as
 * the original's prologue, then the same. */
#define ACTOR_STEP_MEMBER_ENTRY(name,locals) \
static int actor_step_##name##_entry(es2_machine *m,uint32_t bridge) { \
    const uint32_t ebp=shim_frame(m),top=ebp-(locals##u)-4u;shim_w32(top,m->ebx); \
    return actor_step_call(m,bridge,ebp,shim_gap(bridge,top,ACTOR_STEP_MEMBER_PAGES)-16u); }
ACTOR_STEP_MEMBER_ENTRY(mech_move,12)
ACTOR_STEP_MEMBER_ENTRY(mech_act,12)
ACTOR_STEP_MEMBER_ENTRY(mech_decide,12)
ACTOR_STEP_MEMBER_ENTRY(flyer_move_call,24)
ACTOR_STEP_MEMBER_ENTRY(flyer_act,12)
ACTOR_STEP_MEMBER_ENTRY(flyer_decide,12)
#undef ACTOR_STEP_MEMBER_ENTRY
ES2_SHIM(actor_step_actor_step_shim,0x11cecu,actor_step_actor_step_entry,0);
ES2_SHIM(actor_step_mech_move_shim,0x15afcu,actor_step_mech_move_entry,0);
ES2_SHIM(actor_step_mech_act_shim,0x15b38u,actor_step_mech_act_entry,0);
ES2_SHIM(actor_step_mech_decide_shim,0x15b74u,actor_step_mech_decide_entry,0);
ES2_SHIM(actor_step_flyer_move_call_shim,0x217fcu,actor_step_flyer_move_call_entry,0);
ES2_SHIM(actor_step_flyer_act_shim,0x2184cu,actor_step_flyer_act_entry,0);
ES2_SHIM(actor_step_flyer_decide_shim,0x21888u,actor_step_flyer_decide_entry,0);
/* Callee counters (hk_<hook>_<suffix>), incremented only while the hook is
 * non-original: the slots by offset, timer_dword_step by address, the
 * member calls by the member's offset in the state record. state_frame
 * has no game calls. */
static callee_counter actor_step_counters[]={
    {"slot_1c",ES2_COLLIDE_SLOT,0x1c,0},
    {"call_467aa8",ES2_COLLIDE_CALL,0x467aa8,0},
    {"slot_14",ES2_COLLIDE_SLOT,0x14,0},
    {"slot_18",ES2_COLLIDE_SLOT,0x18,0},
};
static callee_counter mech_move_counters[]={{"member_24",ES2_COLLIDE_SLOT,0x24,0}};
static callee_counter mech_act_counters[]={{"member_18",ES2_COLLIDE_SLOT,0x18,0}};
static callee_counter mech_decide_counters[]={{"member_30",ES2_COLLIDE_SLOT,0x30,0}};
static callee_counter flyer_move_call_counters[]={{"member_24",ES2_COLLIDE_SLOT,0x24,0}};
static callee_counter flyer_act_counters[]={{"member_18",ES2_COLLIDE_SLOT,0x18,0}};
static callee_counter flyer_decide_counters[]={{"member_30",ES2_COLLIDE_SLOT,0x30,0}};
static callee_counter *actor_step_hook_counters(unsigned hook,unsigned *count) {
#define ACTOR_STEP_COUNTERS(name) if(hook==ES2_HOOK_INDEX_##name) { \
        *count=sizeof(name##_counters)/sizeof(name##_counters[0]);return name##_counters; }
    ACTOR_STEP_COUNTERS(actor_step)
    ACTOR_STEP_COUNTERS(mech_move)
    ACTOR_STEP_COUNTERS(mech_act)
    ACTOR_STEP_COUNTERS(mech_decide)
    ACTOR_STEP_COUNTERS(flyer_move_call)
    ACTOR_STEP_COUNTERS(flyer_act)
    ACTOR_STEP_COUNTERS(flyer_decide)
#undef ACTOR_STEP_COUNTERS
    *count=0;return NULL;
}
/* push ebp; mov ebp,esp; push ebx; mov ebx,[ebp+8]; cmp dword [ebx+4Dh],0 */
static const BYTE actor_step_actor_step_sig[]={0x55,0x8b,0xec,0x53,0x8b,0x5d,0x08,0x83,0x7b,0x4d,0x00};
/* push ebp; mov ebp,esp; mov eax,[ebp+8]; lea edx,[eax+35h] */
static const BYTE actor_step_state_frame_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x8d,0x50,0x35};
/* push ebp; mov ebp,esp; add esp,-0Ch; push ebx; mov ebx,[ebp+8];
 * mov eax,[ebx+4Dh]; mov edx,[eax+member] */
static const BYTE actor_step_mech_move_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xf4,0x53,0x8b,0x5d,0x08,0x8b,0x43,0x4d,0x8b,0x50,0x24};
static const BYTE actor_step_mech_act_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xf4,0x53,0x8b,0x5d,0x08,0x8b,0x43,0x4d,0x8b,0x50,0x18};
static const BYTE actor_step_mech_decide_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xf4,0x53,0x8b,0x5d,0x08,0x8b,0x43,0x4d,0x8b,0x50,0x30};
/* push ebp; mov ebp,esp; add esp,-18h; push ebx; mov ebx,[ebp+8];
 * lea eax,[ebx+26h] */
static const BYTE actor_step_flyer_move_call_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xe8,0x53,0x8b,0x5d,0x08,0x8d,0x43,0x26};
static const BYTE actor_step_flyer_act_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xf4,0x53,0x8b,0x5d,0x08,0x8b,0x43,0x4d,0x8b,0x50,0x18};
static const BYTE actor_step_flyer_decide_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xf4,0x53,0x8b,0x5d,0x08,0x8b,0x43,0x4d,0x8b,0x50,0x30};
static const BYTE actor_step_saved_lens[]={1,2,1,3},actor_step_frame_lens[]={1,2,3};
#define ACTOR_STEP_EXT(name) \
    static const es2_hook_extension actor_step_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,actor_step_##name,TRUE}; \
    static uint32_t __cdecl actor_step_##name##32(const uint32_t *a,void *u) {return (uint32_t)actor_step_##name(a,u,weapons_no_registers);}
ACTOR_STEP_EXT(actor_step)
ACTOR_STEP_EXT(state_frame)
ACTOR_STEP_EXT(mech_move)
ACTOR_STEP_EXT(mech_act)
ACTOR_STEP_EXT(mech_decide)
ACTOR_STEP_EXT(flyer_move_call)
ACTOR_STEP_EXT(flyer_act)
ACTOR_STEP_EXT(flyer_decide)
#undef ACTOR_STEP_EXT
static es2_hook_descriptor actor_step_hook_descriptors[]={
    {ES2_HOOK_ID(actor_step),actor_step_actor_step_sig,11,7,actor_step_saved_lens,4,1,actor_step_actor_step32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(state_frame),actor_step_state_frame_sig,9,6,actor_step_frame_lens,3,1,actor_step_state_frame32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(mech_move),actor_step_mech_move_sig,16,6,actor_step_frame_lens,3,1,actor_step_mech_move32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(mech_act),actor_step_mech_act_sig,16,6,actor_step_frame_lens,3,1,actor_step_mech_act32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(mech_decide),actor_step_mech_decide_sig,16,6,actor_step_frame_lens,3,1,actor_step_mech_decide32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(flyer_move_call),actor_step_flyer_move_call_sig,13,6,actor_step_frame_lens,3,1,actor_step_flyer_move_call32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(flyer_act),actor_step_flyer_act_sig,16,6,actor_step_frame_lens,3,1,actor_step_flyer_act32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(flyer_decide),actor_step_flyer_decide_sig,16,6,actor_step_frame_lens,3,1,actor_step_flyer_decide32,NULL,NULL,NULL,0},
};
#endif
