#ifndef ES2_LOGIC_FRAME_HOOKS_H
#define ES2_LOGIC_FRAME_HOOKS_H
/* logic_frame group (docs/reverse-engineering/main_loop.md, section
 * "the logic frame as own code"): the logic frame 45f570
 * (logic_frame). Every game call goes through the callee observer
 * (weapons_graph) with the original's EAX/EDX/ECX, EBX/ESI/EDI, EBP and
 * return address, so the 19 nested hooks (control_record, the pool
 * updates, removal_append, pool_prev, group_activate, order_update,
 * player_input, timer_scan, trigger_scan, actor_pass, vehicle_update,
 * death_sequence, outcome_poll, group_assess) and the hooks behind the
 * virtuals +14h/+18h keep their own modes; the runtime record 49630c, the
 * view request 45de30, the heap 47749c and the alert dialog (455950, its
 * virtual +8, 4543b0) stay guest calls. Refuses compare mode (game calls,
 * dialogs, allocation, the whole simulation).
 *
 * The callees read stack residue (the group order 4231a4 under
 * player_input, the AI state methods under order_update), so logic_frame
 * has an ENTRY SHIM (logic_frame_entry, chained from
 * panel_frame_capture_entry through logic_frame_capture_entry) that runs it at the original stack
 * coordinate: it keeps the original's EBP (T-4 for entry ESP T), stores the
 * caller's EBX/ESI/EDI where 45f570's prologue pushes them (EBP-78h/-7Ch/
 * -80h), reserves a guard-probed gap of LOGIC_FRAME_PAGES pages and leaves
 * {EBP, outgoing ESP = EBP-80h, ECX} below it in logic_frame_pending;
 * every game call (49630c included, so the runtime record lies at the
 * original EBP-30h and FS:0 links it as the original does) then runs
 * through ai_behaviour_stack_call (ai_behaviour_hooks.h) with the
 * registers and return address the core names, and the core re-reads the
 * argument slot for each POP ECX. The adapter writes the core's ECX into
 * the context, which the shim loads after the framework's dispatch, so
 * EAX, EDX and ECX return exactly; FS:0 is set to the record's saved head
 * as both epilogues do. Without the shim (direct and legacy calls) the
 * adapter keeps its frame in a local buffer and calls through
 * logic_frame_machine_call (EBX/ESI/EDI the core names, EBP = the buffer's
 * synthetic EBP). No game instruction is embedded; the assembly only
 * marshals registers and moves ESP. */
#include "es2/core/logic_frame.h"
#include "sim_bridge.h"
/* 448 KiB: the deepest subtree below the frame is order_update's host
 * frames, actor_step's 84-page gap with the trampolines' 80 pages and the
 * host frames (about 352 KiB); player_input's 64 pages, the modal alert
 * loop with command_key's 48 pages and the building updates fit as well. */
#define LOGIC_FRAME_PAGES 112
#define LOGIC_FRAME_STRING_(x) #x
#define LOGIC_FRAME_STRING(x) LOGIC_FRAME_STRING_(x)
typedef struct logic_frame_stack { uint32_t ebp,top,ecx; } logic_frame_stack;
__attribute__((used)) static logic_frame_stack *logic_frame_pending;
__attribute__((used)) static void *logic_frame_bridge;
/* The plain bridge's saved host ESP (nesting-safe: pushed and popped) and
 * the callee of the current call (read only by its CALL instruction). */
__attribute__((used)) static uint32_t logic_frame_host_esp,logic_frame_target;
/* Like weapons_machine_call, plus EBX, ESI, EDI and EBP from call[] =
 * {EBX, ESI, EDI, EBP}; the host frame is found again through the saved
 * ESP. */
__attribute__((naked,noinline)) static void __cdecl logic_frame_machine_call(
    uint32_t target,const uint32_t *args,unsigned count,es2_weapon_registers *registers,const uint32_t *call) {
    __asm__ volatile(
        "pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        ES2_SIM_BRIDGE_ARG8
        "pushl _logic_frame_host_esp\n\tmovl %esp,_logic_frame_host_esp\n\t"
        "movl 8(%ebp),%eax\n\tmovl %eax,_logic_frame_target\n\t"
        "movl 12(%ebp),%esi\n\tmovl 16(%ebp),%ecx\n\tmovl %ecx,_weapons_bridge_count\n\t"
        "testl %ecx,%ecx\n\tjz 2f\n\t1: decl %ecx\n\tpushl (%esi,%ecx,4)\n\ttestl %ecx,%ecx\n\tjnz 1b\n\t"
        "2: movl %esp,_weapons_bridge_args\n\t"
        "movl 20(%ebp),%edi\n\tmovl 24(%ebp),%ebx\n\t"
        "movl (%edi),%eax\n\tmovl 4(%edi),%edx\n\tmovl 8(%edi),%ecx\n\t"
        "movl 4(%ebx),%esi\n\tmovl 8(%ebx),%edi\n\tmovl 12(%ebx),%ebp\n\tmovl (%ebx),%ebx\n\t"
        "call *_logic_frame_target\n\t"
        "movl $0,_weapons_bridge_args\n\t"
        "movl _logic_frame_host_esp,%esp\n\tpopl _logic_frame_host_esp\n\tleal 12(%esp),%ebp\n\t"
        "movl 20(%ebp),%ebx\n\tmovl %eax,(%ebx)\n\tmovl %edx,4(%ebx)\n\tmovl %ecx,8(%ebx)\n\t"
        "popl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
typedef struct logic_frame_guest { const logic_frame_stack *stack; es2_logic_frame frame; uint32_t ebp; } logic_frame_guest;
static void logic_frame_stack_call(void *context,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    const logic_frame_guest *c=context;
    const uint32_t call[6]={c->frame.call.ebx,c->frame.call.esi,c->frame.call.edi,c->stack->ebp,c->stack->top,c->frame.call.ret};
    ai_behaviour_stack_call(f,a,n,r,call);
}
static void logic_frame_plain_call(void *context,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    const logic_frame_guest *c=context;
    const uint32_t call[4]={c->frame.call.ebx,c->frame.call.esi,c->frame.call.edi,c->ebp};
    logic_frame_machine_call(f,a,n,r,call);
}
static uint64_t __cdecl logic_frame_logic_frame(const uint32_t *a,void *u,const uint32_t *r) {
    (void)a;
    /* The shim publishes its context right before the framework's dispatch
     * and nothing can enter 45f570 in between (its only caller is the main
     * loop); without the shim the context is NULL. */
    logic_frame_stack *s=logic_frame_pending;logic_frame_pending=NULL;
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_logic_frame);
    BYTE frame[ES2_LOGIC_FRAME_SIZE]={0};
    logic_frame_guest c={s,{{0,0,0,0},0},(uint32_t)(uintptr_t)(frame+ES2_LOGIC_FRAME_SIZE)};
    uint32_t ecx,ebx=0,esi=0,edi=0;
    if(s) {
        c.frame.top=s->top;c.ebp=s->ebp;
        g.scratch=s->ebp-ES2_LOGIC_FRAME_SIZE;g.call=logic_frame_stack_call;g.context=&c;
        ebx=math_native_read32(NULL,s->ebp-0x78u);esi=math_native_read32(NULL,s->ebp-0x7cu);edi=math_native_read32(NULL,s->ebp-0x80u);
    } else { g.scratch=(uint32_t)(uintptr_t)frame;g.call=logic_frame_plain_call;g.context=&c; }
    const uint64_t v=es2_logic_frame_registers(&math_native_memory,&g,&c.frame,ebx,esi,edi,r,&ecx);
    if(s)s->ecx=ecx;
    const uint32_t head=(uint32_t)(v>>32);
    __asm__ volatile("movl %0,%%fs:0"::"r"(head):"memory");
    return v;
}
/* Callee counters (hk_logic_frame_<suffix>), incremented only while the
 * hook is non-original. Keys: the callee VA for calls, the vtable offset
 * for slots (+14h projectiles, +18h actor-pool objects, +8 the alert). */
static callee_counter logic_frame_counters[]={
    {"call_49630c",ES2_COLLIDE_CALL,0x49630c,0},
    {"call_45a810",ES2_COLLIDE_CALL,0x45a810,0},
    {"call_471c70",ES2_COLLIDE_CALL,0x471c70,0},
    {"call_40813c",ES2_COLLIDE_CALL,0x40813c,0},
    {"call_4091cc",ES2_COLLIDE_CALL,0x4091cc,0},
    {"call_4092dc",ES2_COLLIDE_CALL,0x4092dc,0},
    {"call_46b678",ES2_COLLIDE_CALL,0x46b678,0},
    {"call_408bd8",ES2_COLLIDE_CALL,0x408bd8,0},
    {"call_409d2c",ES2_COLLIDE_CALL,0x409d2c,0},
    {"call_478628",ES2_COLLIDE_CALL,0x478628,0},
    {"slot_14",ES2_COLLIDE_SLOT,0x14,0},
    {"slot_18",ES2_COLLIDE_SLOT,0x18,0},
    {"call_4236c4",ES2_COLLIDE_CALL,0x4236c4,0},
    {"call_423a74",ES2_COLLIDE_CALL,0x423a74,0},
    {"call_460870",ES2_COLLIDE_CALL,0x460870,0},
    {"call_426b48",ES2_COLLIDE_CALL,0x426b48,0},
    {"call_426b70",ES2_COLLIDE_CALL,0x426b70,0},
    {"call_45de30",ES2_COLLIDE_CALL,0x45de30,0},
    {"call_4123ac",ES2_COLLIDE_CALL,0x4123ac,0},
    {"call_41aa5c",ES2_COLLIDE_CALL,0x41aa5c,0},
    {"call_45fa84",ES2_COLLIDE_CALL,0x45fa84,0},
    {"call_47749c",ES2_COLLIDE_CALL,0x47749c,0},
    {"call_455950",ES2_COLLIDE_CALL,0x455950,0},
    {"slot_8",ES2_COLLIDE_SLOT,0x8,0},
    {"call_4543b0",ES2_COLLIDE_CALL,0x4543b0,0},
    {"call_4131ac",ES2_COLLIDE_CALL,0x4131ac,0},
    {"call_423f08",ES2_COLLIDE_CALL,0x423f08,0},
};
static callee_counter *logic_frame_hook_counters(unsigned hook,unsigned *count) {
    if(hook==ES2_HOOK_INDEX_logic_frame){*count=sizeof(logic_frame_counters)/sizeof(logic_frame_counters[0]);return logic_frame_counters;}
    *count=0;return NULL;
}
/* 45f570: push ebp; mov ebp,esp; add esp,-74h; mov eax,49F558h; push ebx;
 * push esi; push edi. Relocated 6 bytes (1,2,3). */
static const BYTE logic_frame_sig[]={0x55,0x8b,0xec,0x83,0xc4,0x8c,0xb8,0x58,0xf5,0x49,0x00,0x53,0x56,0x57};
static const BYTE logic_frame_prologue_lens[]={1,2,3};
static const es2_hook_extension logic_frame_logic_frame_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,logic_frame_logic_frame,TRUE};
static uint32_t __cdecl logic_frame_logic_frame32(const uint32_t *a,void *u) {return (uint32_t)logic_frame_logic_frame(a,u,weapons_no_registers);}
static es2_hook_descriptor logic_frame_hook_descriptors[]={
    {ES2_HOOK_ID(logic_frame),logic_frame_sig,14,6,logic_frame_prologue_lens,3,0,logic_frame_logic_frame32,NULL,NULL,NULL,0},
};
/* The entry shim, as player_input's: PUSH EBP; the caller's EBX/ESI/EDI
 * to EBP-78h/-7Ch/-80h; EBX counts LOGIC_FRAME_PAGES guard-probed pages
 * (OR 0 keeps their bytes) and is reloaded from its slot; the context
 * {EBP, EBP-80h, the entry ECX} below the gap; the framework's dispatch (no
 * argument slot); then ECX from the context (the core's when the
 * replacement ran). EAX/EDX/ECX pass through to the dispatch (EAX is dead
 * at 45f570's entry; EDX and ECX reach 49630c and 45a810). */
__attribute__((naked)) static void logic_frame_entry(void) {
    __asm__ volatile(
        "pushl %ebp\n\tmovl %esp,%ebp\n\tmovl %ebx,-0x78(%ebp)\n\tmovl %esi,-0x7c(%ebp)\n\tmovl %edi,-0x80(%ebp)\n\t"
        "movl $" LOGIC_FRAME_STRING(LOGIC_FRAME_PAGES) ",%ebx\n\t1: subl $4096,%esp\n\torl $0,(%esp)\n\tdecl %ebx\n\tjnz 1b\n\t" ES2_SHIM_FLOOR
        "subl $12,%esp\n\tmovl %ebp,(%esp)\n\tleal -0x80(%ebp),%ebx\n\tmovl %ebx,4(%esp)\n\tmovl %ecx,8(%esp)\n\t"
        "movl %esp,_logic_frame_pending\n\tmovl -0x78(%ebp),%ebx\n\t"
        "call *_logic_frame_bridge\n\t"
        "movl 8(%esp),%ecx\n\tmovl $0,_logic_frame_pending\n\tmovl %ebp,%esp\n\tpopl %ebp\n\tret\n\t");
}
/* Called by panel_frame_capture_entry after the framework installed its
 * jump: redirect the jump of logic_frame to its shim and keep h->patch
 * consistent, so uninstall still validates the exact patch. The chain
 * continues in flyer_ai_capture_entry
 * (flyer_ai_hooks.h). */
static BOOL flyer_ai_capture_entry(es2_hook *h);
static BOOL logic_frame_capture_entry(es2_hook *h) {
    if(h->descriptor->rva!=0x5f570u)return flyer_ai_capture_entry(h);
    DWORD old,ignored;
    if(memcmp(h->site,h->patch,h->descriptor->relocate_size))return FALSE;
    if(!VirtualProtect(h->site,h->descriptor->relocate_size,PAGE_EXECUTE_READWRITE,&old))return FALSE;
    logic_frame_bridge=(void *)(uintptr_t)((uint32_t)(uintptr_t)(h->site+5)+math_native_read32(NULL,(uint32_t)(uintptr_t)(h->patch+1)));
    math_native_write32(NULL,(uint32_t)(uintptr_t)(h->patch+1),(uint32_t)(uintptr_t)logic_frame_entry-(uint32_t)(uintptr_t)(h->site+5));
    memcpy(h->site,h->patch,h->descriptor->relocate_size);
    BOOL flushed=FlushInstructionCache(GetCurrentProcess(),h->site,h->descriptor->relocate_size);
    BOOL protected=VirtualProtect(h->site,h->descriptor->relocate_size,old,&ignored);
    return flushed && protected;
}
#endif
