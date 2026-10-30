#ifndef ES2_AI_BEHAVIOUR2_HOOKS_H
#define ES2_AI_BEHAVIOUR2_HOOKS_H
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
 * the plain bridge, like hunt and group_move (ai_behaviour_hooks.h).
 * The shims are chained from ai_behaviour_capture_entry (end of the
 * chain: ai_behaviour2_capture_entry). */
#include "es2/core/ai_behaviour2.h"
__attribute__((used)) static ai_behaviour_stack *ai_behaviour2_aim_select_pending,*ai_behaviour2_retreat_fire_pending;
__attribute__((used)) static void *ai_behaviour2_aim_select_bridge,*ai_behaviour2_retreat_fire_bridge;
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
 * outgoing ESP EBP-28h; three slots). EAX (dead at entry: 41ea85 writes
 * it) counts the probed pages; EDX and ECX pass through. */
__attribute__((naked)) static void ai_behaviour2_aim_select_entry(void) {
    __asm__ volatile(
        "pushl %ebp\n\tmovl %esp,%ebp\n\tmovl %ebx,-0x20(%ebp)\n\tmovl %esi,-0x24(%ebp)\n\tmovl %edi,-0x28(%ebp)\n\t"
        "movl $68,%eax\n\t1: subl $4096,%esp\n\torl $0,(%esp)\n\tdecl %eax\n\tjnz 1b\n\t" ES2_SHIM_FLOOR
        "subl $8,%esp\n\tmovl %ebp,(%esp)\n\tleal -0x28(%ebp),%eax\n\tmovl %eax,4(%esp)\n\t"
        "movl %esp,_ai_behaviour2_aim_select_pending\n\txorl %eax,%eax\n\t"
        "pushl 16(%ebp)\n\tpushl 12(%ebp)\n\tpushl 8(%ebp)\n\tcall *_ai_behaviour2_aim_select_bridge\n\t"
        "movl $0,_ai_behaviour2_aim_select_pending\n\tmovl %ebp,%esp\n\tpopl %ebp\n\tret\n\t");
}
/* retreat_fire's entry shim (frame 18h: EBX/ESI/EDI at EBP-1Ch/-20h/-24h,
 * outgoing ESP EBP-24h; one slot). EAX is dead at entry (41d2d3). */
__attribute__((naked)) static void ai_behaviour2_retreat_fire_entry(void) {
    __asm__ volatile(
        "pushl %ebp\n\tmovl %esp,%ebp\n\tmovl %ebx,-0x1c(%ebp)\n\tmovl %esi,-0x20(%ebp)\n\tmovl %edi,-0x24(%ebp)\n\t"
        "movl $72,%eax\n\t1: subl $4096,%esp\n\torl $0,(%esp)\n\tdecl %eax\n\tjnz 1b\n\t" ES2_SHIM_FLOOR
        "subl $8,%esp\n\tmovl %ebp,(%esp)\n\tleal -0x24(%ebp),%eax\n\tmovl %eax,4(%esp)\n\t"
        "movl %esp,_ai_behaviour2_retreat_fire_pending\n\txorl %eax,%eax\n\t"
        "pushl 8(%ebp)\n\tcall *_ai_behaviour2_retreat_fire_bridge\n\t"
        "movl $0,_ai_behaviour2_retreat_fire_pending\n\tmovl %ebp,%esp\n\tpopl %ebp\n\tret\n\t");
}
/* Called by ai_behaviour_capture_entry after the framework installed its
 * jump: redirect it to the shim and keep h->patch consistent, so uninstall
 * still validates the exact patch. The chain continues in
 * control_record_capture_entry (control_record_hooks.h). */
static BOOL control_record_capture_entry(es2_hook *h);
static BOOL ai_behaviour2_capture_entry(es2_hook *h) {
    void *target;void **bridge;
    if(h->descriptor->rva==0x1ea7cu) { target=(void *)ai_behaviour2_aim_select_entry;bridge=&ai_behaviour2_aim_select_bridge; }
    else if(h->descriptor->rva==0x1d2c4u) { target=(void *)ai_behaviour2_retreat_fire_entry;bridge=&ai_behaviour2_retreat_fire_bridge; }
    else return control_record_capture_entry(h);
    DWORD old,ignored;
    if(memcmp(h->site,h->patch,h->descriptor->relocate_size))return FALSE;
    if(!VirtualProtect(h->site,h->descriptor->relocate_size,PAGE_EXECUTE_READWRITE,&old))return FALSE;
    *bridge=(void *)(uintptr_t)((uint32_t)(uintptr_t)(h->site+5)+math_native_read32(NULL,(uint32_t)(uintptr_t)(h->patch+1)));
    math_native_write32(NULL,(uint32_t)(uintptr_t)(h->patch+1),(uint32_t)(uintptr_t)target-(uint32_t)(uintptr_t)(h->site+5));
    memcpy(h->site,h->patch,h->descriptor->relocate_size);
    BOOL flushed=FlushInstructionCache(GetCurrentProcess(),h->site,h->descriptor->relocate_size);
    BOOL protected=VirtualProtect(h->site,h->descriptor->relocate_size,old,&ignored);
    return flushed && protected;
}
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
