#ifndef ES2_AI_BEHAVIOUR2_HOOKS_H
#define ES2_AI_BEHAVIOUR2_HOOKS_H
#include "guest_native.h"
/* AI behaviours (docs/reverse-engineering/ai.md, section "Aim selection,
 * attack planning, group scans and retreat"): aim_select 41ea7c, attack_plan 41cf18, attack_replan 41d5ec,
 * group_scan 41d60c, order_scan 41d7d0 and retreat_fire 41d2c4. Every game callee
 * (the nested hooks, the original 40f09c and the actor virtuals +30h, +4Ch,
 * +54h, +58h) goes through the weapons guest bridge and callee observer
 * (weapons_graph) with the original's registers, so all six refuse compare
 * mode. No game instruction is embedded (the shims only load and store
 * registers and move ESP).
 *
 * aim_select and retreat_fire lie on the fire path (aim_select calls aim_fire
 * and aim_target; retreat_fire calls aim_select), whose weapon bodies read
 * uninitialized stack (ai.md, "stack residue under aim_fire"). Both have entry shims like
 * aim_fire's: the shim keeps the original's EBP, stores the caller's
 * EBX/ESI/EDI in the original's save slots, reserves a guard-probed gap
 * (68 pages for aim_select: aim_fire's 64 plus its host frames; 72 for
 * retreat_fire: aim_select's plus its host frames) and leaves {EBP, outgoing
 * ESP} for the adapter, whose frame is then the original one and whose
 * game calls run through ai_behaviour_stack_call (ai_behaviour_hooks.h) at
 * the original stack coordinate with the EBX/ESI/EDI the core names and
 * the original's EBP and return addresses. The caller's EDI, which both
 * originals keep up to their calls, is read back from its save slot.
 * Without the shim (direct and legacy calls) the plain bridge and the
 * adapter's own frame are used. The other four have no fire path and use
 * the plain bridge, like hunt and group_move (ai_behaviour_hooks.h). */
#include "es2/core/ai_behaviour2.h"
__attribute__((used)) static ai_behaviour_stack *ai_behaviour2_aim_select_pending,*ai_behaviour2_retreat_fire_pending;
/* The shim's context belongs to this invocation only if the slots above
 * its EBP are this call's. */
static const ai_behaviour_stack *ai_behaviour2_take(ai_behaviour_stack **pending,const uint32_t *a,unsigned n) {
    const ai_behaviour_stack *s=*pending;*pending=NULL;
    for(unsigned i=0;s&&i<n;++i)if(math_native_read32(NULL,s->ebp+8u+4u*i)!=a[i])s=NULL;
    return s;
}
static uint64_t __cdecl ai_behaviour2_aim_select(const uint32_t *a,void *u,const uint32_t *r) {
    const ai_behaviour_stack *s=ai_behaviour2_take(&ai_behaviour2_aim_select_pending,a,3);
    BYTE frame[ES2_AIM_SELECT_SCRATCH]={0};es2_weapon_graph g=weapons_graph(u,frame,ES2_HOOK_INDEX_aim_select);
    ai_behaviour_guest c={s,{0,0,0,0}};uint32_t edi=0;
    if(s) { g.scratch=s->ebp-0x1cu;g.call=ai_behaviour_stack_guest_call;g.context=&c;edi=math_native_read32(NULL,s->ebp-0x28u); }
    return es2_aim_select_registers(&math_native_memory,&g,s?&c.frame:NULL,edi,a[0],a[1],a[2],r);
}
static uint64_t __cdecl ai_behaviour2_attack_plan(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_attack_plan);
    return es2_attack_plan_registers(&math_native_memory,&g,a[0],r);
}
static uint64_t __cdecl ai_behaviour2_attack_replan(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_attack_replan);
    return es2_attack_replan_registers(&math_native_memory,&g,a[0],r);
}
static uint64_t __cdecl ai_behaviour2_group_scan(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_group_scan);
    return es2_group_scan_registers(&math_native_memory,&g,a[0],r);
}
static uint64_t __cdecl ai_behaviour2_order_scan(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_order_scan);
    return es2_order_scan_registers(&math_native_memory,&g,a[0],r);
}
static uint64_t __cdecl ai_behaviour2_retreat_fire(const uint32_t *a,void *u,const uint32_t *r) {
    const ai_behaviour_stack *s=ai_behaviour2_take(&ai_behaviour2_retreat_fire_pending,a,1);
    BYTE frame[ES2_RETREAT_FIRE_SCRATCH]={0};es2_weapon_graph g=weapons_graph(u,frame,ES2_HOOK_INDEX_retreat_fire);
    ai_behaviour_guest c={s,{0,0,0,0}};uint32_t edi=0;
    if(s) { g.scratch=s->ebp-0x18u;g.call=ai_behaviour_stack_guest_call;g.context=&c;edi=math_native_read32(NULL,s->ebp-0x24u); }
    return es2_retreat_fire_registers(&math_native_memory,&g,s?&c.frame:NULL,edi,a[0],r);
}
/* aim_select's entry shim (frame 1Ch: EBX/ESI/EDI at EBP-20h/-24h/-28h,
 * outgoing ESP EBP-28h; three slots, 68 probed pages). EAX (dead at entry:
 * 41ea85 writes it) is 0; EDX and ECX pass through. */
static int ai_behaviour2_aim_select_entry(es2_machine *m,uint32_t bridge) {
    uint32_t ebp;const uint32_t context=shim_saved_gap(m,bridge,&ebp,-0x20,68u);
    ai_behaviour2_aim_select_pending=es2_guest_span(context,sizeof(ai_behaviour_stack));m->eax=0;
    const int ran=shim_gap_call(m,bridge,ebp,context,3);
    ai_behaviour2_aim_select_pending=NULL;return ran;
}
/* retreat_fire's entry shim (frame 18h: EBX/ESI/EDI at EBP-1Ch/-20h/-24h,
 * outgoing ESP EBP-24h; one slot, 72 probed pages). EAX is dead at entry
 * (41d2d3) and 0. */
static int ai_behaviour2_retreat_fire_entry(es2_machine *m,uint32_t bridge) {
    uint32_t ebp;const uint32_t context=shim_saved_gap(m,bridge,&ebp,-0x1c,72u);
    ai_behaviour2_retreat_fire_pending=es2_guest_span(context,sizeof(ai_behaviour_stack));m->eax=0;
    const int ran=shim_gap_call(m,bridge,ebp,context,1);
    ai_behaviour2_retreat_fire_pending=NULL;return ran;
}
ES2_SHIM(ai_behaviour2_aim_select_shim,0x1ea7cu,ai_behaviour2_aim_select_entry,0);
ES2_SHIM(ai_behaviour2_retreat_fire_shim,0x1d2c4u,ai_behaviour2_retreat_fire_entry,0);
/* Callee counters (hk_<hook>_<suffix>), incremented only while the hook is
 * non-original. Keys: the callee VA, or the vtable offset for slots. */
static callee_counter aim_select_counters[]={
    {"call_41e984",ES2_COLLIDE_CALL,0x41e984,0},
    {"slot_54",ES2_COLLIDE_SLOT,0x54,0},
    {"slot_58",ES2_COLLIDE_SLOT,0x58,0},
    {"slot_30",ES2_COLLIDE_SLOT,0x30,0},
    {"call_41f5a0",ES2_COLLIDE_CALL,0x41f5a0,0},
};
static callee_counter attack_plan_counters[]={
    {"call_411fa0",ES2_COLLIDE_CALL,0x411fa0,0},
    {"call_41eb34",ES2_COLLIDE_CALL,0x41eb34,0},
    {"call_41d5ec",ES2_COLLIDE_CALL,0x41d5ec,0},
    {"call_41cb94",ES2_COLLIDE_CALL,0x41cb94,0},
    {"slot_4c",ES2_COLLIDE_SLOT,0x4c,0},
    {"call_41ce08",ES2_COLLIDE_CALL,0x41ce08,0},
    {"call_413e50",ES2_COLLIDE_CALL,0x413e50,0},
};
static callee_counter attack_replan_counters[]={
    {"call_41cf18",ES2_COLLIDE_CALL,0x41cf18,0},
};
static callee_counter group_scan_counters[]={
    {"call_41d598",ES2_COLLIDE_CALL,0x41d598,0},
    {"call_467a48",ES2_COLLIDE_CALL,0x467a48,0},
    {"call_411fa0",ES2_COLLIDE_CALL,0x411fa0,0},
    {"call_413e50",ES2_COLLIDE_CALL,0x413e50,0},
    {"call_423918",ES2_COLLIDE_CALL,0x423918,0},
    {"call_41d5ec",ES2_COLLIDE_CALL,0x41d5ec,0},
};
static callee_counter order_scan_counters[]={
    {"call_41d598",ES2_COLLIDE_CALL,0x41d598,0},
    {"call_467a48",ES2_COLLIDE_CALL,0x467a48,0},
    {"call_411fa0",ES2_COLLIDE_CALL,0x411fa0,0},
    {"call_413e50",ES2_COLLIDE_CALL,0x413e50,0},
    {"call_41d5ec",ES2_COLLIDE_CALL,0x41d5ec,0},
};
static callee_counter retreat_fire_counters[]={
    {"call_41e758",ES2_COLLIDE_CALL,0x41e758,0},
    {"call_467aa8",ES2_COLLIDE_CALL,0x467aa8,0},
    {"call_492ee0",ES2_COLLIDE_CALL,0x492ee0,0},
    {"call_40f09c",ES2_COLLIDE_CALL,0x40f09c,0},
    {"call_41e828",ES2_COLLIDE_CALL,0x41e828,0},
    {"call_41ea7c",ES2_COLLIDE_CALL,0x41ea7c,0},
};
static callee_counter *ai_behaviour2_hook_counters(unsigned hook,unsigned *count) {
#define AI_BEHAVIOUR2_COUNTERS(name) if(hook==ES2_HOOK_INDEX_##name) { \
        *count=sizeof(name##_counters)/sizeof(name##_counters[0]);return name##_counters; }
    AI_BEHAVIOUR2_COUNTERS(aim_select)
    AI_BEHAVIOUR2_COUNTERS(attack_plan)
    AI_BEHAVIOUR2_COUNTERS(attack_replan)
    AI_BEHAVIOUR2_COUNTERS(group_scan)
    AI_BEHAVIOUR2_COUNTERS(order_scan)
    AI_BEHAVIOUR2_COUNTERS(retreat_fire)
#undef AI_BEHAVIOUR2_COUNTERS
    *count=0;return NULL;
}
/* push ebp; mov ebp,esp; add esp,-n (or push ebx; push esi); push ebx;
 * push esi; push edi (41d5ec: mov eax,[ebp+8]; push eax). */
static const BYTE ai_behaviour2_aim_select_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xe4,0x53,0x56,0x57};
static const BYTE ai_behaviour2_attack_plan_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xe0,0x53,0x56,0x57};
static const BYTE ai_behaviour2_attack_replan_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x50};
static const BYTE ai_behaviour2_group_scan_sig[]={0x55,0x8b,0xec,0x53,0x56,0x57,0x8b,0x5d,0x08};
static const BYTE ai_behaviour2_order_scan_sig[]={0x55,0x8b,0xec,0x53,0x56,0x57,0x8b,0x5d,0x08};
static const BYTE ai_behaviour2_retreat_fire_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xe8,0x53,0x56,0x57};
static const BYTE ai_behaviour2_frame_lens[]={1,2,3},ai_behaviour2_push_lens[]={1,2,1,1};
#define AI_BEHAVIOUR2_EXT(name) \
    static const es2_hook_extension ai_behaviour2_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,ai_behaviour2_##name,TRUE}; \
    static uint32_t __cdecl ai_behaviour2_##name##32(const uint32_t *a,void *u) {return (uint32_t)ai_behaviour2_##name(a,u,weapons_no_registers);}
AI_BEHAVIOUR2_EXT(aim_select)
AI_BEHAVIOUR2_EXT(attack_plan)
AI_BEHAVIOUR2_EXT(attack_replan)
AI_BEHAVIOUR2_EXT(group_scan)
AI_BEHAVIOUR2_EXT(order_scan)
AI_BEHAVIOUR2_EXT(retreat_fire)
#undef AI_BEHAVIOUR2_EXT
static es2_hook_descriptor ai_behaviour2_hook_descriptors[]={
    {ES2_HOOK_ID(aim_select),ai_behaviour2_aim_select_sig,9,6,ai_behaviour2_frame_lens,3,3,ai_behaviour2_aim_select32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(attack_plan),ai_behaviour2_attack_plan_sig,9,6,ai_behaviour2_frame_lens,3,1,ai_behaviour2_attack_plan32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(attack_replan),ai_behaviour2_attack_replan_sig,7,6,ai_behaviour2_frame_lens,3,1,ai_behaviour2_attack_replan32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(group_scan),ai_behaviour2_group_scan_sig,9,5,ai_behaviour2_push_lens,4,1,ai_behaviour2_group_scan32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(order_scan),ai_behaviour2_order_scan_sig,9,5,ai_behaviour2_push_lens,4,1,ai_behaviour2_order_scan32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(retreat_fire),ai_behaviour2_retreat_fire_sig,9,6,ai_behaviour2_frame_lens,3,1,ai_behaviour2_retreat_fire32,NULL,NULL,NULL,0},
};
#endif
