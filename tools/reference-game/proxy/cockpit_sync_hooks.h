#ifndef ES2_COCKPIT_SYNC_HOOKS_H
#define ES2_COCKPIT_SYNC_HOOKS_H
/* cockpit_sync group (docs/reverse-engineering/hud.md, section on the
 * cockpit<->player synchronisation, "player synchronisation as own
 * code"): the
 * cockpit<->player synchronisation 41b130 (cockpit_sync), the throttle
 * record getter 447df8 (throttle_record), the throttle gauge set 447da8
 * (throttle_gauge) and the HUD record getter 43dbb0 (hud_record).
 *
 * cockpit_sync calls every game function through the callee observer
 * (weapons_graph) with the original's EAX/EDX/ECX, EBX/ESI/EDI, EBP and
 * return address, so the nested hooks (shield_balance, dist_gate,
 * weapon_frame, the three helpers of this group, timer_word_step,
 * aim_point) keep their own modes; the EnergyPoolGauge set 4450f8, the
 * weapon word 40e484, the HUD record set 43d9b4 and the HUD aim fields
 * 434a24 stay guest calls. 434a24 copies frame bytes that the original
 * leaves unwritten without a target and the callee trees were not audited
 * for uninitialized reads, so cockpit_sync has an ENTRY SHIM
 * (cockpit_sync_entry, chained from flyer_ai_capture_entry through
 * cockpit_sync_capture_entry) that runs it at the original
 * stack coordinate: it keeps the original's EBP (T-4 for entry ESP T),
 * stores the caller's EBX/ESI where 41b130's prologue pushes them
 * (EBP-44h/-48h), reserves a guard-probed gap of COCKPIT_SYNC_PAGES pages
 * and leaves {EBP, outgoing ESP = EBP-48h, the caller's EDI} below it in
 * cockpit_sync_pending; the core's locals are the original's (EBP-40h ..
 * EBP-1) and every game call runs through ai_behaviour_stack_call
 * (ai_behaviour_hooks.h) with the registers and return address the core
 * names; the core re-reads the argument slot for each POP ECX. Without the
 * shim (direct calls, the own caller in render_root_hooks.h) the adapter
 * keeps the
 * frame in a zeroed local buffer and calls through logic_frame_machine_call
 * (logic_frame_hooks.h: EBX/ESI/EDI as named, EBP = the buffer's synthetic
 * EBP; the caller's ESI/EDI are 0). EDX:EAX exact; ECX after 41b130 is
 * popped by both callers (45fca8, 461608), so the framework returns the
 * entry ECX.
 *
 * throttle_gauge calls the slider's class-table slot +14h through the
 * plain bridge (counter slot_14; the slider event and base hooks
 * (throttle_hooks.h) keep their modes below it); its ECX is dead at the only caller (41b288
 * writes it). throttle_record and hud_record touch no memory (EAX = the
 * slot + 0B1h, EDX = the entry EDX) and accept compare mode; cockpit_sync
 * and throttle_gauge refuse it (game and virtual calls). No game
 * instruction is embedded; the assembly only marshals registers and moves
 * ESP. */
#include "es2/core/cockpit_sync.h"
/* 448 KiB, as for the logic frame: the weapon fire tree under weapon_frame
 * (emitter and bullet shims with their gaps), the HUD gadget and slider
 * trees and the nested hooks' host frames run above the gap. */
#define COCKPIT_SYNC_PAGES 112
#define COCKPIT_SYNC_STRING_(x) #x
#define COCKPIT_SYNC_STRING(x) COCKPIT_SYNC_STRING_(x)
typedef struct cockpit_sync_stack { uint32_t ebp,top,edi; } cockpit_sync_stack;
__attribute__((used)) static cockpit_sync_stack *cockpit_sync_pending;
__attribute__((used)) static void *cockpit_sync_bridge;
typedef struct cockpit_sync_guest { const cockpit_sync_stack *stack; es2_cockpit_sync frame; uint32_t ebp; } cockpit_sync_guest;
static void cockpit_sync_stack_call(void *context,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    const cockpit_sync_guest *c=context;
    const uint32_t call[6]={c->frame.call.ebx,c->frame.call.esi,c->frame.call.edi,c->stack->ebp,c->stack->top,c->frame.call.ret};
    ai_behaviour_stack_call(f,a,n,r,call);
}
static void cockpit_sync_plain_call(void *context,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    const cockpit_sync_guest *c=context;
    const uint32_t call[4]={c->frame.call.ebx,c->frame.call.esi,c->frame.call.edi,c->ebp};
    logic_frame_machine_call(f,a,n,r,call);
}
static uint64_t __cdecl cockpit_sync_cockpit_sync(const uint32_t *a,void *u,const uint32_t *r) {
    /* The shim publishes its context right before the framework's
     * dispatch; it belongs to this call only if the slot above its EBP is
     * this call's argument. */
    const cockpit_sync_stack *s=cockpit_sync_pending;cockpit_sync_pending=NULL;
    if(s&&math_native_read32(NULL,s->ebp+8u)!=a[0])s=NULL;
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_cockpit_sync);
    BYTE frame[ES2_COCKPIT_SYNC_FRAME]={0};
    cockpit_sync_guest c={s,{{0,0,0,0},0},(uint32_t)(uintptr_t)(frame+ES2_COCKPIT_SYNC_FRAME)};
    uint32_t esi=0,edi=0;
    if(s) {
        c.frame.top=s->top;esi=math_native_read32(NULL,s->ebp-0x48u);edi=s->edi;
        g.scratch=s->ebp-ES2_COCKPIT_SYNC_FRAME;g.call=cockpit_sync_stack_call;g.context=&c;
    } else { g.scratch=(uint32_t)(uintptr_t)frame;g.call=cockpit_sync_plain_call;g.context=&c; }
    return es2_cockpit_sync_registers(&math_native_memory,&g,&c.frame,esi,edi,a[0],r);
}
static uint64_t __cdecl cockpit_sync_throttle_gauge(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_throttle_gauge);
    return es2_throttle_gauge_registers(&math_native_memory,&g,a[0],a[1],r);
}
static uint64_t __cdecl cockpit_sync_throttle_record(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;return es2_throttle_record_registers(a[0],r);
}
static uint64_t __cdecl cockpit_sync_hud_record(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;return es2_hud_record_registers(a[0],r);
}
/* Callee counters (hk_<hook>_<suffix>), incremented only while the hook is
 * non-original. Keys: the callee VA for calls, the vtable offset for
 * slots. */
static callee_counter cockpit_sync_counters[]={
    {"call_4450f8",ES2_COLLIDE_CALL,0x4450f8,0},
    {"call_413bc8",ES2_COLLIDE_CALL,0x413bc8,0},
    {"call_49288c",ES2_COLLIDE_CALL,0x49288c,0},
    {"call_410b40",ES2_COLLIDE_CALL,0x410b40,0},
    {"call_447df8",ES2_COLLIDE_CALL,0x447df8,0},
    {"call_447da8",ES2_COLLIDE_CALL,0x447da8,0},
    {"call_43dbb0",ES2_COLLIDE_CALL,0x43dbb0,0},
    {"call_40e484",ES2_COLLIDE_CALL,0x40e484,0},
    {"call_467a48",ES2_COLLIDE_CALL,0x467a48,0},
    {"call_43d9b4",ES2_COLLIDE_CALL,0x43d9b4,0},
    {"call_41b728",ES2_COLLIDE_CALL,0x41b728,0},
    {"call_434a24",ES2_COLLIDE_CALL,0x434a24,0},
};
static callee_counter throttle_gauge_counters[]={
    {"slot_14",ES2_COLLIDE_SLOT,0x14,0},
};
static callee_counter *cockpit_sync_hook_counters(unsigned hook,unsigned *count) {
    if(hook==ES2_HOOK_INDEX_cockpit_sync){*count=sizeof(cockpit_sync_counters)/sizeof(cockpit_sync_counters[0]);return cockpit_sync_counters;}
    if(hook==ES2_HOOK_INDEX_throttle_gauge){*count=1;return throttle_gauge_counters;}
    *count=0;return NULL;
}
/* 41b130: push ebp; mov ebp,esp; add esp,-40h; push ebx; push esi; mov
 * ebx,[ebp+8]. Relocated 6 bytes (1,2,3). */
static const BYTE cockpit_sync_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xc0,0x53,0x56,0x8b,0x5d,0x08};
/* 447da8: push ebp; mov ebp,esp; push ebx; push esi; mov esi,[ebp+0Ch];
 * mov ebx,[ebp+8]. Relocated 5 bytes (1,2,1,1). */
static const BYTE cockpit_sync_gauge_sig[]={0x55,0x8b,0xec,0x53,0x56,0x8b,0x75,0x0c,0x8b,0x5d,0x08};
/* 447df8 and 43dbb0: push ebp; mov ebp,esp; mov eax,[ebp+8]; add
 * eax,0B1h. Relocated 6 bytes (1,2,3). */
static const BYTE cockpit_sync_record_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x05,0xb1,0x00,0x00,0x00};
static const BYTE cockpit_sync_frame_lens[]={1,2,3},cockpit_sync_saved_lens[]={1,2,1,1};
#define COCKPIT_SYNC_EXT(name,unsupported) \
    static const es2_hook_extension cockpit_sync_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,cockpit_sync_##name,unsupported}; \
    static uint32_t __cdecl cockpit_sync_##name##32(const uint32_t *a,void *u) {return (uint32_t)cockpit_sync_##name(a,u,weapons_no_registers);}
COCKPIT_SYNC_EXT(cockpit_sync,TRUE)
COCKPIT_SYNC_EXT(throttle_record,FALSE)
COCKPIT_SYNC_EXT(throttle_gauge,TRUE)
COCKPIT_SYNC_EXT(hud_record,FALSE)
#undef COCKPIT_SYNC_EXT
static es2_hook_descriptor cockpit_sync_hook_descriptors[]={
    {ES2_HOOK_ID(cockpit_sync),cockpit_sync_sig,11,6,cockpit_sync_frame_lens,3,1,cockpit_sync_cockpit_sync32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(throttle_record),cockpit_sync_record_sig,11,6,cockpit_sync_frame_lens,3,1,cockpit_sync_throttle_record32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(throttle_gauge),cockpit_sync_gauge_sig,11,5,cockpit_sync_saved_lens,4,2,cockpit_sync_throttle_gauge32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(hud_record),cockpit_sync_record_sig,11,6,cockpit_sync_frame_lens,3,1,cockpit_sync_hud_record32,NULL,NULL,NULL,0},
};
/* The entry shim, as logic_frame's: PUSH EBP; the caller's EBX/ESI to
 * EBP-44h/-48h; EBX counts COCKPIT_SYNC_PAGES guard-probed pages (OR 0
 * keeps their bytes) and is reloaded from its slot; the context {EBP,
 * EBP-48h, the caller's EDI} below the gap; the slot copied for the
 * framework's dispatch. EAX/EDX/ECX pass through (41b130 does not read
 * them). ECX after 41b130 is popped by both callers. */
__attribute__((naked)) static void cockpit_sync_entry(void) {
    __asm__ volatile(
        "pushl %ebp\n\tmovl %esp,%ebp\n\tmovl %ebx,-0x44(%ebp)\n\tmovl %esi,-0x48(%ebp)\n\t"
        "movl $" COCKPIT_SYNC_STRING(COCKPIT_SYNC_PAGES) ",%ebx\n\t1: subl $4096,%esp\n\torl $0,(%esp)\n\tdecl %ebx\n\tjnz 1b\n\t" ES2_SHIM_FLOOR
        "subl $12,%esp\n\tmovl %ebp,(%esp)\n\tleal -0x48(%ebp),%ebx\n\tmovl %ebx,4(%esp)\n\tmovl %edi,8(%esp)\n\t"
        "movl %esp,_cockpit_sync_pending\n\tmovl -0x44(%ebp),%ebx\n\t"
        "pushl 8(%ebp)\n\tcall *_cockpit_sync_bridge\n\t"
        "movl $0,_cockpit_sync_pending\n\tmovl %ebp,%esp\n\tpopl %ebp\n\tret\n\t");
}
/* Called by flyer_ai_capture_entry after the framework installed its
 * jump: redirect the jump of cockpit_sync to its shim and keep h->patch
 * consistent, so uninstall still validates the exact patch. The chain
 * continues in
 * rotation_helpers_capture_entry (rotation_helpers_hooks.h). */
static BOOL rotation_helpers_capture_entry(es2_hook *h);
static BOOL cockpit_sync_capture_entry(es2_hook *h) {
    if(h->descriptor->rva!=0x1b130u)return rotation_helpers_capture_entry(h);
    DWORD old,ignored;
    if(memcmp(h->site,h->patch,h->descriptor->relocate_size))return FALSE;
    if(!VirtualProtect(h->site,h->descriptor->relocate_size,PAGE_EXECUTE_READWRITE,&old))return FALSE;
    cockpit_sync_bridge=(void *)(uintptr_t)((uint32_t)(uintptr_t)(h->site+5)+math_native_read32(NULL,(uint32_t)(uintptr_t)(h->patch+1)));
    math_native_write32(NULL,(uint32_t)(uintptr_t)(h->patch+1),(uint32_t)(uintptr_t)cockpit_sync_entry-(uint32_t)(uintptr_t)(h->site+5));
    memcpy(h->site,h->patch,h->descriptor->relocate_size);
    BOOL flushed=FlushInstructionCache(GetCurrentProcess(),h->site,h->descriptor->relocate_size);
    BOOL protected=VirtualProtect(h->site,h->descriptor->relocate_size,old,&ignored);
    return flushed && protected;
}
#endif
