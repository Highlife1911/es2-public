#ifndef ES2_AI_BEHAVIOUR_HOOKS_H
#define ES2_AI_BEHAVIOUR_HOOKS_H
/* AI behaviours around the torso servos (docs/reverse-engineering/ai.md,
 * section "AI behaviours around the torso servos"): weapon_range 40e5f8, weapon_choose 41f358,
 * aim_fire 41f5a0, aim_target 41e984, target_search 41e0e0, hunt 41e224,
 * line_blocked 41dc24, clear_shot 41dd64 and group_move 41d598. Every game
 * callee (the nested hooks, the original helpers 41c3c8/41fac4/41fb60 and
 * the list step 471c70, the actor and weapon virtuals) goes through the
 * weapons guest bridge and callee observer (weapons_graph) with the
 * original's registers, so eight refuse compare mode; weapon_range is a
 * pure leaf (no game call, no write) and accepts it: its shim
 * below hands both phases the same entry ECX, which the original ignores,
 * and the framework returns that ECX in every mode. The frames whose
 * addresses reach a callee
 * (line_blocked, clear_shot, aim_target, aim_fire) live in the adapters'
 * scratch. weapon_range has an entry shim: its ECX at return is
 * [W+1Ch], which reaches the weapon virtual +2Ch in 41f358, so the shim
 * loads ECX from its first slot before the framework's dispatch (the
 * framework returns the entry ECX); it is chained from
 * vehicle_aux_capture_entry. No other entry register is read before it is
 * written beyond what the adapters forward; no game instruction is
 * embedded (the shims only load and store registers and move ESP).
 * aim_fire has a second entry shim (ai.md, "stack residue under
 * aim_fire"): its game calls run at the original's outgoing stack
 * coordinate with the original's EBX/ESI/EDI/EBP, so every callee
 * (torso_aim, the weapon virtuals +60h/+28h and below them preparation and
 * the emitters, which read uninitialized stack) finds the stack bytes the
 * original aim_fire would leave there.
 * weapon_choose has the same kind of shim (trigger_fire reads its record
 * bytes from what weapon_choose's earlier callees leave below its entry),
 * and ai_behaviour_capture_entry also installs torso_aim's shim
 * (input_dispatch_hooks.h, whose group is registered earlier) and
 * player_input's. */
#include "es2/core/ai_behaviour.h"
#include "sim_bridge.h"
static uint64_t __cdecl ai_behaviour_weapon_range(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;(void)r;return es2_weapon_range_registers(&math_native_memory,a[0],a[1]);
}
/* aim_fire at the original stack coordinate. The entry shim
 * ai_behaviour_aim_fire_entry (below) keeps the original's EBP (S-4 for
 * entry ESP S), stores the caller's EBX/ESI/EDI where the original prologue
 * saves them (EBP-14h/-18h/-1Ch), reserves a guard-probed gap and leaves
 * {EBP, outgoing ESP = EBP-1Ch} in ai_behaviour_aim_fire_pending for the
 * adapter. The adapter's frame is then the original one (L at EBP-10h, k
 * at EBP-2) and every game call goes through ai_behaviour_stack_call,
 * which switches ESP to the outgoing coordinate, loads the EBX/ESI/EDI
 * that the core names for that call and EBP, calls, returns to the host
 * stack and leaves the original's return address in the slot below the
 * arguments. Without the shim (direct calls, legacy path) the adapter uses
 * the plain bridge and its own frame. */
typedef struct ai_behaviour_stack { uint32_t ebp,top; } ai_behaviour_stack;
typedef struct ai_behaviour_guest { const ai_behaviour_stack *stack; es2_ai_call_frame frame; } ai_behaviour_guest;
__attribute__((used)) static ai_behaviour_stack *ai_behaviour_aim_fire_pending;
/* The innermost active stack bridge's host ESP and the callee of the
 * current switch (read only by its CALL instruction). The bridge also
 * keeps es2_shim_floor (weapons_hooks.h): saved, lowered to its host
 * ESP for the call, restored on return. */
__attribute__((used)) static uint32_t ai_behaviour_guest_esp,ai_behaviour_guest_target;
/* call[] = {EBX, ESI, EDI, EBP, outgoing ESP, return address}. */
__attribute__((naked,noinline)) static void __cdecl ai_behaviour_stack_call(
    uint32_t target,const uint32_t *args,unsigned count,es2_weapon_registers *registers,const uint32_t *call) {
    __asm__ volatile(
        "pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        ES2_SIM_BRIDGE_ARG8
        "pushl _es2_shim_floor\n\t"
        "pushl _ai_behaviour_guest_esp\n\tmovl 8(%ebp),%eax\n\tmovl %eax,_ai_behaviour_guest_target\n\t"
        "movl %esp,_ai_behaviour_guest_esp\n\t"
        "movl _es2_shim_floor,%eax\n\ttestl %eax,%eax\n\tje 6f\n\tcmpl %eax,%esp\n\tjae 7f\n\t6: movl %esp,_es2_shim_floor\n\t7:\n\t"
        "movl 12(%ebp),%esi\n\tmovl 16(%ebp),%ecx\n\tmovl 20(%ebp),%edi\n\tmovl 24(%ebp),%ebx\n\tmovl 16(%ebx),%esp\n\t"
        "testl %ecx,%ecx\n\tjz 2f\n\t1: decl %ecx\n\tpushl (%esi,%ecx,4)\n\ttestl %ecx,%ecx\n\tjnz 1b\n\t"
        "2: movl (%edi),%eax\n\tmovl 4(%edi),%edx\n\tmovl 8(%edi),%ecx\n\t"
        "movl 12(%ebx),%ebp\n\tmovl 4(%ebx),%esi\n\tmovl 8(%ebx),%edi\n\tmovl (%ebx),%ebx\n\t"
        "call *_ai_behaviour_guest_target\n\t"
        "movl _ai_behaviour_guest_esp,%esp\n\tpopl _ai_behaviour_guest_esp\n\tpopl _es2_shim_floor\n\tleal 12(%esp),%ebp\n\t"
        "movl 20(%ebp),%ebx\n\tmovl %eax,(%ebx)\n\tmovl %edx,4(%ebx)\n\tmovl %ecx,8(%ebx)\n\t"
        "movl 24(%ebp),%ebx\n\tmovl 16(%ebx),%edx\n\tmovl 16(%ebp),%ecx\n\tshll $2,%ecx\n\tsubl %ecx,%edx\n\t"
        "movl 20(%ebx),%ecx\n\tmovl %ecx,-4(%edx)\n\t"
        "popl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
static void ai_behaviour_stack_guest_call(void *context,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    const ai_behaviour_guest *c=context;
    const uint32_t call[6]={c->frame.ebx,c->frame.esi,c->frame.edi,c->stack->ebp,c->stack->top,c->frame.ret};
    ai_behaviour_stack_call(f,a,n,r,call);
}
static uint64_t __cdecl ai_behaviour_aim_fire(const uint32_t *a,void *u,const uint32_t *r) {
    const ai_behaviour_stack *s=ai_behaviour_aim_fire_pending;ai_behaviour_aim_fire_pending=NULL;
    /* The context belongs to this invocation only if the arguments above
     * the shim's EBP are this call's. */
    for(unsigned i=0;s&&i<4;++i)if(math_native_read32(NULL,s->ebp+8u+4u*i)!=a[i])s=NULL;
    BYTE frame[ES2_AIM_FIRE_SCRATCH]={0};es2_weapon_graph g=weapons_graph(u,frame,ES2_HOOK_INDEX_aim_fire);
    ai_behaviour_guest c={s,{0,0,0,0}};
    if(s) { g.scratch=s->ebp-0x10u;g.call=ai_behaviour_stack_guest_call;g.context=&c; }
    return es2_aim_fire_registers(&math_native_memory,&g,&c.frame,a[0],a[1],a[2],a[3],r);
}
/* weapon_choose at the original stack coordinate, as aim_fire: its
 * entry shim (below) leaves {EBP, outgoing ESP = EBP-24h}; the frame locals
 * go to EBP-18h; every game call runs through ai_behaviour_stack_call with
 * the core's EBX/ESI/EDI and return address. Where the original has pushed
 * a temporary (41f4e3 before the second multiply_q10, 41f502 before both
 * rng_bounded calls) the call runs 4 bytes lower and the temporary is
 * stored in the slot above its arguments first. So trigger_fire (41f57c)
 * finds the record bytes the original's earlier callees leave below its
 * entry (ai.md, "residue shims for torso_aim and weapon_choose"). */
typedef struct ai_behaviour_push_guest { const ai_behaviour_stack *stack; es2_ai_push_frame frame; } ai_behaviour_push_guest;
__attribute__((used)) static ai_behaviour_stack *ai_behaviour_weapon_choose_pending;
static void ai_behaviour_push_guest_call(void *context,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    const ai_behaviour_push_guest *c=context;uint32_t top=c->stack->top;
    if(c->frame.pushed) { top-=4u;math_native_write32(NULL,top,c->frame.temp); }
    const uint32_t call[6]={c->frame.call.ebx,c->frame.call.esi,c->frame.call.edi,c->stack->ebp,top,c->frame.call.ret};
    ai_behaviour_stack_call(f,a,n,r,call);
}
static uint64_t __cdecl ai_behaviour_weapon_choose(const uint32_t *a,void *u,const uint32_t *r) {
    const ai_behaviour_stack *s=ai_behaviour_weapon_choose_pending;ai_behaviour_weapon_choose_pending=NULL;
    for(unsigned i=0;s&&i<4;++i)if(math_native_read32(NULL,s->ebp+8u+4u*i)!=a[i])s=NULL;
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_weapon_choose);
    ai_behaviour_push_guest c={s,{{0,0,0,0},0,0}};
    if(s) { g.scratch=s->ebp-ES2_WEAPON_CHOOSE_SCRATCH;g.call=ai_behaviour_push_guest_call;g.context=&c; }
    return es2_weapon_choose_frame_registers(&math_native_memory,&g,s?&c.frame:NULL,a[0],a[1],a[2],a[3],r);
}
static uint64_t __cdecl ai_behaviour_aim_target(const uint32_t *a,void *u,const uint32_t *r) {
    (void)r;BYTE frame[ES2_AIM_TARGET_SCRATCH]={0};es2_weapon_graph g=weapons_graph(u,frame,ES2_HOOK_INDEX_aim_target);
    return es2_aim_target_registers(&math_native_memory,&g,a[0],a[1],a[2]);
}
static uint64_t __cdecl ai_behaviour_target_search(const uint32_t *a,void *u,const uint32_t *r) {
    (void)r;es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_target_search);
    return es2_target_search_registers(&math_native_memory,&g,a[0],a[1]);
}
static uint64_t __cdecl ai_behaviour_hunt(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_hunt);
    return es2_hunt_registers(&math_native_memory,&g,a[0],r);
}
static uint64_t __cdecl ai_behaviour_line_blocked(const uint32_t *a,void *u,const uint32_t *r) {
    (void)r;BYTE frame[ES2_LINE_BLOCKED_SCRATCH]={0};es2_weapon_graph g=weapons_graph(u,frame,ES2_HOOK_INDEX_line_blocked);
    return es2_line_blocked_registers(&math_native_memory,&g,a[0]);
}
static uint64_t __cdecl ai_behaviour_clear_shot(const uint32_t *a,void *u,const uint32_t *r) {
    (void)r;BYTE frame[ES2_CLEAR_SHOT_SCRATCH]={0};es2_weapon_graph g=weapons_graph(u,frame,ES2_HOOK_INDEX_clear_shot);
    return es2_clear_shot_registers(&math_native_memory,&g,a[0]);
}
static uint64_t __cdecl ai_behaviour_group_move(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_group_move);
    return es2_group_move_registers(&math_native_memory,&g,a[0],r);
}
/* weapon_range's entry shim: ECX = [W+1Ch] (W = the first slot), then the
 * framework's dispatch. The original ignores its entry ECX. */
__attribute__((used)) static void *ai_behaviour_range_bridge;
__attribute__((naked)) static void ai_behaviour_range_entry(void) {
    __asm__ volatile("movl 4(%esp),%ecx\n\tmovl 0x1c(%ecx),%ecx\n\tjmp *_ai_behaviour_range_bridge\n\t");
}
/* aim_fire's entry shim. PUSH EBP gives the original's EBP; the caller's
 * EBX/ESI/EDI go to the original's save slots; EAX (dead at entry: 41f5b2
 * writes it before the first call) counts the probed pages: 64 pages = 256
 * KiB, so that the emitters' own 128-KiB gap (weapons_hooks.h) and their
 * host frames fit between aim_fire's outgoing ESP and its host frames.
 * The context {EBP, EBP-1Ch} sits right below the gap; the four arguments
 * are copied for the framework's dispatch. EDX and ECX pass through. */
__attribute__((used)) static void *ai_behaviour_aim_fire_bridge;
__attribute__((naked)) static void ai_behaviour_aim_fire_entry(void) {
    __asm__ volatile(
        "pushl %ebp\n\tmovl %esp,%ebp\n\tmovl %ebx,-0x14(%ebp)\n\tmovl %esi,-0x18(%ebp)\n\tmovl %edi,-0x1c(%ebp)\n\t"
        "movl $64,%eax\n\t1: subl $4096,%esp\n\torl $0,(%esp)\n\tdecl %eax\n\tjnz 1b\n\t" ES2_SHIM_FLOOR
        "subl $8,%esp\n\tmovl %ebp,(%esp)\n\tleal -0x1c(%ebp),%eax\n\tmovl %eax,4(%esp)\n\t"
        "movl %esp,_ai_behaviour_aim_fire_pending\n\txorl %eax,%eax\n\t"
        "pushl 20(%ebp)\n\tpushl 16(%ebp)\n\tpushl 12(%ebp)\n\tpushl 8(%ebp)\n\tcall *_ai_behaviour_aim_fire_bridge\n\t"
        "movl $0,_ai_behaviour_aim_fire_pending\n\tmovl %ebp,%esp\n\tpopl %ebp\n\tret\n\t");
}
/* weapon_choose's entry shim (frame 18h: EBX/ESI/EDI at EBP-1Ch/
 * -20h/-24h, outgoing ESP EBP-24h; four slots). The entry EAX/EDX/ECX reach
 * 47df48 unchanged in the original, so they pass through: EBX counts the
 * probed pages (32 pages = 128 KiB: the callees are leaves, the weapon
 * virtuals, weapon_range and trigger_fire, no emitter, but one callee hook
 * in compare mode reserves 64 KiB of comparison storage; aim_fire's 64-page
 * gap encloses this gap and weapon_choose's host frames) and is reloaded
 * from its slot. */
__attribute__((used)) static void *ai_behaviour_weapon_choose_bridge;
__attribute__((naked)) static void ai_behaviour_weapon_choose_entry(void) {
    __asm__ volatile(
        "pushl %ebp\n\tmovl %esp,%ebp\n\tmovl %ebx,-0x1c(%ebp)\n\tmovl %esi,-0x20(%ebp)\n\tmovl %edi,-0x24(%ebp)\n\t"
        "movl $32,%ebx\n\t1: subl $4096,%esp\n\torl $0,(%esp)\n\tdecl %ebx\n\tjnz 1b\n\t" ES2_SHIM_FLOOR
        "subl $8,%esp\n\tmovl %ebp,(%esp)\n\tleal -0x24(%ebp),%ebx\n\tmovl %ebx,4(%esp)\n\t"
        "movl %esp,_ai_behaviour_weapon_choose_pending\n\tmovl -0x1c(%ebp),%ebx\n\t"
        "pushl 20(%ebp)\n\tpushl 16(%ebp)\n\tpushl 12(%ebp)\n\tpushl 8(%ebp)\n\tcall *_ai_behaviour_weapon_choose_bridge\n\t"
        "movl $0,_ai_behaviour_weapon_choose_pending\n\tmovl %ebp,%esp\n\tpopl %ebp\n\tret\n\t");
}
/* Called by vehicle_aux_capture_entry after the framework installed its
 * jump: redirect it to the shim and keep h->patch consistent, so uninstall
 * still validates the exact patch. The chain continues in
 * ai_behaviour2_capture_entry (ai_behaviour2_hooks.h). */
static BOOL ai_behaviour2_capture_entry(es2_hook *h);
static BOOL ai_behaviour_capture_entry(es2_hook *h) {
    void *target;void **bridge;
    if(h->descriptor->rva==0x0e5f8u) { target=(void *)ai_behaviour_range_entry;bridge=&ai_behaviour_range_bridge; }
    else if(h->descriptor->rva==0x1f5a0u) { target=(void *)ai_behaviour_aim_fire_entry;bridge=&ai_behaviour_aim_fire_bridge; }
    /* weapon_choose's shim and torso_aim's (input_dispatch_hooks.h). */
    else if(h->descriptor->rva==0x1f358u) { target=(void *)ai_behaviour_weapon_choose_entry;bridge=&ai_behaviour_weapon_choose_bridge; }
    else if(h->descriptor->rva==0x1ef14u) { target=(void *)input_dispatch_torso_aim_entry;bridge=&input_dispatch_torso_aim_bridge; }
    /* player_input's shim (input_dispatch_hooks.h). */
    else if(h->descriptor->rva==0x60870u) { target=(void *)input_dispatch_player_input_entry;bridge=&input_dispatch_player_input_bridge; }
    else return ai_behaviour2_capture_entry(h);
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
 * weapon_range has no game calls. */
static callee_counter weapon_choose_counters[]={
    {"call_47df48",ES2_COLLIDE_CALL,0x47df48,0},
    {"slot_34",ES2_COLLIDE_SLOT,0x34,0},
    {"slot_5c",ES2_COLLIDE_SLOT,0x5c,0},
    {"slot_60",ES2_COLLIDE_SLOT,0x60,0},
    {"call_40e5f8",ES2_COLLIDE_CALL,0x40e5f8,0},
    {"slot_2c",ES2_COLLIDE_SLOT,0x2c,0},
    {"call_47e0b0",ES2_COLLIDE_CALL,0x47e0b0,0},
    {"call_492f24",ES2_COLLIDE_CALL,0x492f24,0},
    {"call_423430",ES2_COLLIDE_CALL,0x423430,0},
};
static callee_counter aim_fire_counters[]={
    {"call_4928d0",ES2_COLLIDE_CALL,0x4928d0,0},
    {"slot_2c",ES2_COLLIDE_SLOT,0x2c,0},
    {"call_40e5f8",ES2_COLLIDE_CALL,0x40e5f8,0},
    {"call_41f358",ES2_COLLIDE_CALL,0x41f358,0},
    {"slot_38",ES2_COLLIDE_SLOT,0x38,0},
    {"call_4929fc",ES2_COLLIDE_CALL,0x4929fc,0},
    {"call_492f24",ES2_COLLIDE_CALL,0x492f24,0},
    {"call_41ef14",ES2_COLLIDE_CALL,0x41ef14,0},
    {"slot_60",ES2_COLLIDE_SLOT,0x60,0},
    {"slot_28",ES2_COLLIDE_SLOT,0x28,0},
};
static callee_counter aim_target_counters[]={
    {"call_492934",ES2_COLLIDE_CALL,0x492934,0},
    {"call_41e8d4",ES2_COLLIDE_CALL,0x41e8d4,0},
    {"call_49288c",ES2_COLLIDE_CALL,0x49288c,0},
    {"slot_58",ES2_COLLIDE_SLOT,0x58,0},
    {"call_47ebb8",ES2_COLLIDE_CALL,0x47ebb8,0},
    {"slot_24",ES2_COLLIDE_SLOT,0x24,0},
    {"call_48043c",ES2_COLLIDE_CALL,0x48043c,0},
    {"call_41f5a0",ES2_COLLIDE_CALL,0x41f5a0,0},
};
static callee_counter target_search_counters[]={
    {"call_471c70",ES2_COLLIDE_CALL,0x471c70,0},
    {"call_411e80",ES2_COLLIDE_CALL,0x411e80,0},
    {"call_4928d0",ES2_COLLIDE_CALL,0x4928d0,0},
};
static callee_counter hunt_counters[]={
    {"call_41dbcc",ES2_COLLIDE_CALL,0x41dbcc,0},
    {"call_467a48",ES2_COLLIDE_CALL,0x467a48,0},
    {"call_41e0e0",ES2_COLLIDE_CALL,0x41e0e0,0},
    {"call_413e50",ES2_COLLIDE_CALL,0x413e50,0},
    {"call_41ce08",ES2_COLLIDE_CALL,0x41ce08,0},
    {"call_4928d0",ES2_COLLIDE_CALL,0x4928d0,0},
    {"call_41e8d4",ES2_COLLIDE_CALL,0x41e8d4,0},
    {"call_41c3c8",ES2_COLLIDE_CALL,0x41c3c8,0},
    {"call_41fac4",ES2_COLLIDE_CALL,0x41fac4,0},
    {"call_492934",ES2_COLLIDE_CALL,0x492934,0},
    {"call_416a04",ES2_COLLIDE_CALL,0x416a04,0},
    {"call_41fbb8",ES2_COLLIDE_CALL,0x41fbb8,0},
};
static callee_counter line_blocked_counters[]={
    {"slot_24",ES2_COLLIDE_SLOT,0x24,0},
    {"call_46e980",ES2_COLLIDE_CALL,0x46e980,0},
    {"call_404ca0",ES2_COLLIDE_CALL,0x404ca0,0},
    {"call_4280f4",ES2_COLLIDE_CALL,0x4280f4,0},
};
static callee_counter clear_shot_counters[]={
    {"call_492934",ES2_COLLIDE_CALL,0x492934,0},
    {"call_467aa8",ES2_COLLIDE_CALL,0x467aa8,0},
    {"call_41dc24",ES2_COLLIDE_CALL,0x41dc24,0},
    {"call_413e50",ES2_COLLIDE_CALL,0x413e50,0},
    {"call_4928d0",ES2_COLLIDE_CALL,0x4928d0,0},
    {"call_4929fc",ES2_COLLIDE_CALL,0x4929fc,0},
    {"call_416a04",ES2_COLLIDE_CALL,0x416a04,0},
    {"call_41e8d4",ES2_COLLIDE_CALL,0x41e8d4,0},
};
static callee_counter group_move_counters[]={
    {"call_41fac4",ES2_COLLIDE_CALL,0x41fac4,0},
    {"call_41fb60",ES2_COLLIDE_CALL,0x41fb60,0},
    {"call_41fbb8",ES2_COLLIDE_CALL,0x41fbb8,0},
    {"call_41c3c8",ES2_COLLIDE_CALL,0x41c3c8,0},
    {"call_41e8d4",ES2_COLLIDE_CALL,0x41e8d4,0},
};
static callee_counter *ai_behaviour_hook_counters(unsigned hook,unsigned *count) {
#define AI_BEHAVIOUR_COUNTERS(name) if(hook==ES2_HOOK_INDEX_##name) { \
        *count=sizeof(name##_counters)/sizeof(name##_counters[0]);return name##_counters; }
    AI_BEHAVIOUR_COUNTERS(weapon_choose)
    AI_BEHAVIOUR_COUNTERS(aim_fire)
    AI_BEHAVIOUR_COUNTERS(aim_target)
    AI_BEHAVIOUR_COUNTERS(target_search)
    AI_BEHAVIOUR_COUNTERS(hunt)
    AI_BEHAVIOUR_COUNTERS(line_blocked)
    AI_BEHAVIOUR_COUNTERS(clear_shot)
    AI_BEHAVIOUR_COUNTERS(group_move)
#undef AI_BEHAVIOUR_COUNTERS
    *count=0;return NULL;
}
static const BYTE ai_behaviour_weapon_range_sig[]={0x55,0x8b,0xec,0x8b,0x55,0x0c,0x8b,0x45,0x08};
static const BYTE ai_behaviour_weapon_choose_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xe8,0x53,0x56,0x57};
static const BYTE ai_behaviour_aim_fire_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xf0,0x53,0x56,0x57};
static const BYTE ai_behaviour_aim_target_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xe4,0x53,0x56,0x57};
static const BYTE ai_behaviour_target_search_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xe8,0x33,0xc0};
static const BYTE ai_behaviour_hunt_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xf4,0x53,0x56,0x57};
static const BYTE ai_behaviour_line_blocked_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xd4,0x53,0x56};
static const BYTE ai_behaviour_clear_shot_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xf4,0x53,0x56,0x57};
static const BYTE ai_behaviour_group_move_sig[]={0x55,0x8b,0xec,0x53,0x8b,0x5d,0x08};
static const BYTE ai_behaviour_frame_lens[]={1,2,3},ai_behaviour_move_lens[]={1,2,1,3};
#define AI_BEHAVIOUR_EXT_FLAG(name,unsupported) \
    static const es2_hook_extension ai_behaviour_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,ai_behaviour_##name,unsupported}; \
    static uint32_t __cdecl ai_behaviour_##name##32(const uint32_t *a,void *u) {return (uint32_t)ai_behaviour_##name(a,u,weapons_no_registers);}
#define AI_BEHAVIOUR_EXT(name) AI_BEHAVIOUR_EXT_FLAG(name,TRUE)
AI_BEHAVIOUR_EXT_FLAG(weapon_range,FALSE)
AI_BEHAVIOUR_EXT(weapon_choose)
AI_BEHAVIOUR_EXT(aim_fire)
AI_BEHAVIOUR_EXT(aim_target)
AI_BEHAVIOUR_EXT(target_search)
AI_BEHAVIOUR_EXT(hunt)
AI_BEHAVIOUR_EXT(line_blocked)
AI_BEHAVIOUR_EXT(clear_shot)
AI_BEHAVIOUR_EXT(group_move)
#undef AI_BEHAVIOUR_EXT
#undef AI_BEHAVIOUR_EXT_FLAG
static es2_hook_descriptor ai_behaviour_hook_descriptors[]={
    {ES2_HOOK_ID(weapon_range),ai_behaviour_weapon_range_sig,9,6,ai_behaviour_frame_lens,3,2,ai_behaviour_weapon_range32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(weapon_choose),ai_behaviour_weapon_choose_sig,9,6,ai_behaviour_frame_lens,3,4,ai_behaviour_weapon_choose32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(aim_fire),ai_behaviour_aim_fire_sig,9,6,ai_behaviour_frame_lens,3,4,ai_behaviour_aim_fire32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(aim_target),ai_behaviour_aim_target_sig,9,6,ai_behaviour_frame_lens,3,3,ai_behaviour_aim_target32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(target_search),ai_behaviour_target_search_sig,8,6,ai_behaviour_frame_lens,3,2,ai_behaviour_target_search32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(hunt),ai_behaviour_hunt_sig,9,6,ai_behaviour_frame_lens,3,1,ai_behaviour_hunt32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(line_blocked),ai_behaviour_line_blocked_sig,8,6,ai_behaviour_frame_lens,3,1,ai_behaviour_line_blocked32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(clear_shot),ai_behaviour_clear_shot_sig,9,6,ai_behaviour_frame_lens,3,1,ai_behaviour_clear_shot32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(group_move),ai_behaviour_group_move_sig,7,7,ai_behaviour_move_lens,4,1,ai_behaviour_group_move32,NULL,NULL,NULL,0},
};
#endif
