#ifndef ES2_MECH_TRAVEL_HOOKS_H
#define ES2_MECH_TRAVEL_HOOKS_H
/* MECH travel, follow, sleep, ram and dead states
 * (docs/reverse-engineering/ai.md, section "MECH travel, follow, sleep,
 * ram and dead states"): travel_step 41d9cc,
 * follow_step 41daac, sleep_step 41c418, ram_step 41e570, ram_move 41e488,
 * dead_step 41e554 (the +18h/+24h members of the MECH state records,
 * called by the trampolines 415b38/415afc) and the helpers leader_route
 * 41fb60, point_move 41fac4 and fire_permit 41c3c8. Core in
 * core/src/mech_travel.c.
 *
 * Every game call (the nested hooks, the original sound trigger 40f09c and
 * the actor virtuals +3Ch and +74h) goes through the weapons guest bridge
 * and callee observer (weapons_graph) with the original's registers, so the
 * eight with game calls refuse compare mode. fire_permit is a leaf whose
 * only write is byte A+96h; it accepts compare with a span
 * A+96h..A+0B2h (the clone's prefix also holds the bytes it reads, A+45h,
 * A+97h and A+0B2h; the leader byte is read through real pointers, which
 * neither phase writes) and a comparator that remaps EAX = EDX = A+92h.
 *
 * travel_step and follow_step lie on the fire path (both call aim_select,
 * whose aim_fire reaches the weapon bodies that read stack residue; ai.md,
 * "stack residue under aim_fire"). Both have entry shims like retreat_fire's: the shim keeps
 * the original's EBP, stores the caller's EBX/ESI/EDI in the original's
 * save slots (follow_step also the entry ECX in its local EBP-4, as PUSH
 * ECX does), reserves 72 guard-probed pages (aim_select's 68 plus its host
 * frames) and leaves {EBP, outgoing ESP} for the adapter, whose game calls
 * then run through ai_behaviour_stack_call at the original stack
 * coordinate with the EBX/ESI/EDI the core names, the original's EBP and
 * return addresses. travel_step passes its entry EAX to leader_route (its
 * high word reaches route_point's index slot), so its shim keeps EAX in
 * memory across the probe; follow_step's EAX is dead at entry (41dab9).
 * EDX and ECX pass through. Without the shim (direct and legacy calls) the
 * plain bridge and the adapter's own frame are used. The other seven reach
 * no fire function and use the plain bridge (the hunt/group_move treatment
 * of ai.md, "AI behaviours around the torso servos"). The shims are chained
 * from flyer_states_capture_entry through mech_travel_capture_entry. No game instruction is
 * embedded (the shims only load and store registers and move ESP). */
#include "es2/core/mech_travel.h"
#define MECH_TRAVEL_PAGES 72
#define MECH_TRAVEL_STRING_(x) #x
#define MECH_TRAVEL_STRING(x) MECH_TRAVEL_STRING_(x)
__attribute__((used)) static ai_behaviour_stack *mech_travel_travel_pending,*mech_travel_follow_pending;
__attribute__((used)) static void *mech_travel_travel_bridge,*mech_travel_follow_bridge;
__attribute__((used)) static uint32_t mech_travel_eax;
/* The shim's context belongs to this invocation only if the argument above
 * its EBP is this call's. */
static const ai_behaviour_stack *mech_travel_take(ai_behaviour_stack **pending,uint32_t arg) {
    const ai_behaviour_stack *s=*pending;*pending=NULL;
    return s&&math_native_read32(NULL,s->ebp+8u)==arg?s:NULL;
}
static uint64_t __cdecl mech_travel_fire_permit(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;(void)r;return es2_fire_permit_registers(&math_native_memory,a[0]);
}
static uint64_t __cdecl mech_travel_dead_step(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_dead_step);
    return es2_dead_step_registers(&math_native_memory,&g,a[0],r);
}
static uint64_t __cdecl mech_travel_sleep_step(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_sleep_step);
    return es2_sleep_step_registers(&math_native_memory,&g,a[0],r);
}
static uint64_t __cdecl mech_travel_point_move(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_point_move);
    return es2_point_move_registers(&math_native_memory,&g,a[0],a[1],r);
}
static uint64_t __cdecl mech_travel_leader_route(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_leader_route);
    return es2_leader_route_registers(&math_native_memory,&g,a[0],r);
}
/* ram_move's point Q (the original's EBP-0Ch) lives in the adapter's frame. */
static uint64_t __cdecl mech_travel_ram_move(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE frame[ES2_RAM_MOVE_SCRATCH]={0};es2_weapon_graph g=weapons_graph(u,frame,ES2_HOOK_INDEX_ram_move);
    return es2_ram_move_registers(&math_native_memory,&g,a[0],r);
}
static uint64_t __cdecl mech_travel_ram_step(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_ram_step);
    return es2_ram_step_registers(&math_native_memory,&g,a[0],r);
}
/* travel_step: EBX/ESI/EDI saved at EBP-4/-8/-0Ch, outgoing ESP EBP-0Ch;
 * the caller's EDI is read back from its save slot. */
static uint64_t __cdecl mech_travel_travel_step(const uint32_t *a,void *u,const uint32_t *r) {
    const ai_behaviour_stack *s=mech_travel_take(&mech_travel_travel_pending,a[0]);
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_travel_step);
    ai_behaviour_guest c={s,{0,0,0,0}};uint32_t edi=0;
    if(s) { g.call=ai_behaviour_stack_guest_call;g.context=&c;edi=math_native_read32(NULL,s->ebp-0xcu); }
    return es2_travel_step_registers(&math_native_memory,&g,s?&c.frame:NULL,edi,a[0],r);
}
/* follow_step: the local EBP-4 (scratch), EBX/ESI/EDI saved at
 * EBP-8/-0Ch/-10h, outgoing ESP EBP-10h. */
static uint64_t __cdecl mech_travel_follow_step(const uint32_t *a,void *u,const uint32_t *r) {
    const ai_behaviour_stack *s=mech_travel_take(&mech_travel_follow_pending,a[0]);
    BYTE frame[ES2_FOLLOW_STEP_SCRATCH]={0};es2_weapon_graph g=weapons_graph(u,frame,ES2_HOOK_INDEX_follow_step);
    ai_behaviour_guest c={s,{0,0,0,0}};uint32_t edi=0;
    if(s) { g.scratch=s->ebp-4u;g.call=ai_behaviour_stack_guest_call;g.context=&c;edi=math_native_read32(NULL,s->ebp-0x10u); }
    return es2_follow_step_registers(&math_native_memory,&g,s?&c.frame:NULL,edi,a[0],r);
}
/* travel_step's entry shim: EAX is live (41fb60 builds route_point's index
 * slot from its high word), so it waits in memory while EAX counts the
 * probed pages. */
__attribute__((naked)) static void mech_travel_travel_entry(void) {
    __asm__ volatile(
        "pushl %ebp\n\tmovl %esp,%ebp\n\tmovl %ebx,-4(%ebp)\n\tmovl %esi,-8(%ebp)\n\tmovl %edi,-0xc(%ebp)\n\t"
        "movl %eax,_mech_travel_eax\n\tmovl $" MECH_TRAVEL_STRING(MECH_TRAVEL_PAGES) ",%eax\n\t"
        "1: subl $4096,%esp\n\torl $0,(%esp)\n\tdecl %eax\n\tjnz 1b\n\t" ES2_SHIM_FLOOR
        "subl $8,%esp\n\tmovl %ebp,(%esp)\n\tleal -0xc(%ebp),%eax\n\tmovl %eax,4(%esp)\n\t"
        "movl %esp,_mech_travel_travel_pending\n\tmovl _mech_travel_eax,%eax\n\t"
        "pushl 8(%ebp)\n\tcall *_mech_travel_travel_bridge\n\t"
        "movl $0,_mech_travel_travel_pending\n\tmovl %ebp,%esp\n\tpopl %ebp\n\tret\n\t");
}
/* follow_step's entry shim: the entry ECX goes to the local EBP-4 (PUSH
 * ECX at 41daaf); EAX is dead at entry. */
__attribute__((naked)) static void mech_travel_follow_entry(void) {
    __asm__ volatile(
        "pushl %ebp\n\tmovl %esp,%ebp\n\tmovl %ecx,-4(%ebp)\n\tmovl %ebx,-8(%ebp)\n\tmovl %esi,-0xc(%ebp)\n\tmovl %edi,-0x10(%ebp)\n\t"
        "movl $" MECH_TRAVEL_STRING(MECH_TRAVEL_PAGES) ",%eax\n\t"
        "1: subl $4096,%esp\n\torl $0,(%esp)\n\tdecl %eax\n\tjnz 1b\n\t" ES2_SHIM_FLOOR
        "subl $8,%esp\n\tmovl %ebp,(%esp)\n\tleal -0x10(%ebp),%eax\n\tmovl %eax,4(%esp)\n\t"
        "movl %esp,_mech_travel_follow_pending\n\txorl %eax,%eax\n\t"
        "pushl 8(%ebp)\n\tcall *_mech_travel_follow_bridge\n\t"
        "movl $0,_mech_travel_follow_pending\n\tmovl %ebp,%esp\n\tpopl %ebp\n\tret\n\t");
}
/* Called by flyer_states_capture_entry after the framework installed its
 * jump: redirect it to the shim and keep h->patch consistent, so uninstall
 * still validates the exact patch. The chain continues in
 * lc_base_capture_entry (lc_base_hooks.h). */
static BOOL lc_base_capture_entry(es2_hook *h);
static BOOL mech_travel_capture_entry(es2_hook *h) {
    void *target;void **bridge;
    switch(h->descriptor->rva) {
    case 0x1d9ccu:target=(void *)mech_travel_travel_entry;bridge=&mech_travel_travel_bridge;break;
    case 0x1daacu:target=(void *)mech_travel_follow_entry;bridge=&mech_travel_follow_bridge;break;
    default:return lc_base_capture_entry(h);
    }
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
 * non-original. Keys: the callee VA, or the vtable offset for slots;
 * fire_permit has no game calls. */
static callee_counter dead_step_counters[]={
    {"call_416a04",ES2_COLLIDE_CALL,0x416a04,0},
};
static callee_counter sleep_step_counters[]={
    {"call_416a04",ES2_COLLIDE_CALL,0x416a04,0},
    {"call_41c3c8",ES2_COLLIDE_CALL,0x41c3c8,0},
};
static callee_counter point_move_counters[]={
    {"call_492934",ES2_COLLIDE_CALL,0x492934,0},
    {"call_4928d0",ES2_COLLIDE_CALL,0x4928d0,0},
    {"call_40f09c",ES2_COLLIDE_CALL,0x40f09c,0},
    {"call_416a04",ES2_COLLIDE_CALL,0x416a04,0},
};
static callee_counter leader_route_counters[]={
    {"call_423b0c",ES2_COLLIDE_CALL,0x423b0c,0},
    {"call_416a04",ES2_COLLIDE_CALL,0x416a04,0},
    {"call_41fac4",ES2_COLLIDE_CALL,0x41fac4,0},
    {"call_42313c",ES2_COLLIDE_CALL,0x42313c,0},
};
static callee_counter ram_move_counters[]={
    {"call_418f40",ES2_COLLIDE_CALL,0x418f40,0},
    {"call_46e180",ES2_COLLIDE_CALL,0x46e180,0},
    {"call_4195c8",ES2_COLLIDE_CALL,0x4195c8,0},
    {"call_418f74",ES2_COLLIDE_CALL,0x418f74,0},
    {"call_426a20",ES2_COLLIDE_CALL,0x426a20,0},
    {"slot_74",ES2_COLLIDE_SLOT,0x74,0},
};
static callee_counter ram_step_counters[]={
    {"call_467aa8",ES2_COLLIDE_CALL,0x467aa8,0},
    {"call_411fa0",ES2_COLLIDE_CALL,0x411fa0,0},
    {"call_416a04",ES2_COLLIDE_CALL,0x416a04,0},
    {"call_492934",ES2_COLLIDE_CALL,0x492934,0},
    {"call_41e488",ES2_COLLIDE_CALL,0x41e488,0},
    {"call_467a48",ES2_COLLIDE_CALL,0x467a48,0},
    {"call_492f24",ES2_COLLIDE_CALL,0x492f24,0},
};
static callee_counter travel_step_counters[]={
    {"call_41fb60",ES2_COLLIDE_CALL,0x41fb60,0},
    {"call_467aa8",ES2_COLLIDE_CALL,0x467aa8,0},
    {"call_411fa0",ES2_COLLIDE_CALL,0x411fa0,0},
    {"call_492934",ES2_COLLIDE_CALL,0x492934,0},
    {"slot_3c",ES2_COLLIDE_SLOT,0x3c,0},
    {"call_41ea7c",ES2_COLLIDE_CALL,0x41ea7c,0},
};
static callee_counter follow_step_counters[]={
    {"call_4238a0",ES2_COLLIDE_CALL,0x4238a0,0},
    {"call_49288c",ES2_COLLIDE_CALL,0x49288c,0},
    {"call_416a04",ES2_COLLIDE_CALL,0x416a04,0},
    {"call_41fac4",ES2_COLLIDE_CALL,0x41fac4,0},
    {"call_467aa8",ES2_COLLIDE_CALL,0x467aa8,0},
    {"call_411fa0",ES2_COLLIDE_CALL,0x411fa0,0},
    {"call_492934",ES2_COLLIDE_CALL,0x492934,0},
    {"slot_3c",ES2_COLLIDE_SLOT,0x3c,0},
    {"call_41ea7c",ES2_COLLIDE_CALL,0x41ea7c,0},
};
static callee_counter *mech_travel_hook_counters(unsigned hook,unsigned *count) {
#define MECH_TRAVEL_COUNTERS(name) if(hook==ES2_HOOK_INDEX_##name) { \
        *count=sizeof(name##_counters)/sizeof(name##_counters[0]);return name##_counters; }
    MECH_TRAVEL_COUNTERS(dead_step)
    MECH_TRAVEL_COUNTERS(sleep_step)
    MECH_TRAVEL_COUNTERS(point_move)
    MECH_TRAVEL_COUNTERS(leader_route)
    MECH_TRAVEL_COUNTERS(ram_move)
    MECH_TRAVEL_COUNTERS(ram_step)
    MECH_TRAVEL_COUNTERS(travel_step)
    MECH_TRAVEL_COUNTERS(follow_step)
#undef MECH_TRAVEL_COUNTERS
    *count=0;return NULL;
}
/* Prologues (push ebp; mov ebp,esp; then the saves or the frame). */
static const BYTE mech_travel_travel_step_sig[]={0x55,0x8b,0xec,0x53,0x56,0x57,0x8b,0x5d,0x08,0x8d,0x73,0x5a,0x53};
static const BYTE mech_travel_follow_step_sig[]={0x55,0x8b,0xec,0x51,0x53,0x56,0x57,0x8b,0x5d,0x08,0x8d,0x73,0x5a};
static const BYTE mech_travel_sleep_step_sig[]={0x55,0x8b,0xec,0x53,0x8b,0x5d,0x08,0x8b,0x93,0xa4,0x01,0x00,0x00};
static const BYTE mech_travel_ram_step_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xf4,0x53,0x56,0x57,0x8b,0x5d,0x08,0x8d,0x73,0x5a};
static const BYTE mech_travel_ram_move_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xcc,0x53,0x56,0x57,0x8b,0x5d,0x08,0x53};
static const BYTE mech_travel_dead_step_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x6a,0x00,0x6a,0x00,0x6a,0x00,0x50};
static const BYTE mech_travel_leader_route_sig[]={0x55,0x8b,0xec,0x53,0x56,0x57,0x8b,0x5d,0x08,0x8b,0x73,0x45};
static const BYTE mech_travel_point_move_sig[]={0x55,0x8b,0xec,0x51,0x53,0x56,0x57,0x8b,0x75,0x0c};
static const BYTE mech_travel_fire_permit_sig[]={0x55,0x8b,0xec,0x51,0x53,0x56,0x8b,0x45,0x08,0x66,0xb9,0x20,0x00};
static const BYTE mech_travel_push_lens[]={1,2,1,1},mech_travel_frame_lens[]={1,2,3},mech_travel_ebx_lens[]={1,2,1,3};
#define MECH_TRAVEL_EXT_FLAG(name,unsupported) \
    static const es2_hook_extension mech_travel_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,mech_travel_##name,unsupported}; \
    static uint32_t __cdecl mech_travel_##name##32(const uint32_t *a,void *u) {return (uint32_t)mech_travel_##name(a,u,weapons_no_registers);}
#define MECH_TRAVEL_EXT(name) MECH_TRAVEL_EXT_FLAG(name,TRUE)
MECH_TRAVEL_EXT(travel_step)
MECH_TRAVEL_EXT(follow_step)
MECH_TRAVEL_EXT(sleep_step)
MECH_TRAVEL_EXT(ram_step)
MECH_TRAVEL_EXT(ram_move)
MECH_TRAVEL_EXT(dead_step)
MECH_TRAVEL_EXT(leader_route)
MECH_TRAVEL_EXT(point_move)
MECH_TRAVEL_EXT_FLAG(fire_permit,FALSE)
#undef MECH_TRAVEL_EXT
#undef MECH_TRAVEL_EXT_FLAG
/* Compare: fire_permit's span A+96h..A+0B2h (written byte first);
 * EAX = EDX = A+92h point into the clone in the replacement phase. */
static const es2_hook_span mech_travel_fire_permit_spans[]={{0,0x96,0x1d}};
static BOOL __cdecl mech_travel_fire_permit_equal(const es2_hook_comparison *v,void *u) {
    (void)u;const uint32_t o=(uint32_t)(uintptr_t)v->original[0],r=(uint32_t)(uintptr_t)v->replacement[0];
    return !memcmp(v->original[0],v->replacement[0],v->sizes[0])&&v->original_result-o==v->replacement_result-r&&
        v->original_edx-o==v->replacement_edx-r;
}
static es2_hook_descriptor mech_travel_hook_descriptors[]={
    {ES2_HOOK_ID(travel_step),mech_travel_travel_step_sig,13,5,mech_travel_push_lens,4,1,mech_travel_travel_step32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(follow_step),mech_travel_follow_step_sig,13,5,mech_travel_push_lens,4,1,mech_travel_follow_step32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(sleep_step),mech_travel_sleep_step_sig,13,7,mech_travel_ebx_lens,4,1,mech_travel_sleep_step32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(ram_step),mech_travel_ram_step_sig,15,6,mech_travel_frame_lens,3,1,mech_travel_ram_step32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(ram_move),mech_travel_ram_move_sig,13,6,mech_travel_frame_lens,3,1,mech_travel_ram_move32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(dead_step),mech_travel_dead_step_sig,13,6,mech_travel_frame_lens,3,1,mech_travel_dead_step32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(leader_route),mech_travel_leader_route_sig,12,5,mech_travel_push_lens,4,1,mech_travel_leader_route32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(point_move),mech_travel_point_move_sig,10,5,mech_travel_push_lens,4,2,mech_travel_point_move32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(fire_permit),mech_travel_fire_permit_sig,13,5,mech_travel_push_lens,4,1,mech_travel_fire_permit32,mech_travel_fire_permit_equal,NULL,mech_travel_fire_permit_spans,1},
};
#endif
