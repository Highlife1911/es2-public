#ifndef ES2_HDD_EXEC_HOOKS_H
#define ES2_HDD_EXEC_HOOKS_H
#include "guest_native.h"
/* hdd_exec group (docs/reverse-engineering/cockpit.md, section
 * "HDD button executor"): the HDD button executor 44a1a0
 * (hdd_execute), called only by the HDD draw 449bf8 (slot 2 of the HDD
 * table 49d64c, at 449c64) with the pending command [H+528h]. Core in
 * core/src/hdd_exec.c. Every game call goes through the callee observer
 * (weapons_graph) with the original's EAX/EDX/ECX, so the hooked callees
 * (wingman_order 431610, order_cancel 44dc10 and every hook below the
 * guest calls) keep their modes; the page button 44a60c, the page commands
 * 44d0c0/44d124/44cff8/44d05c/44cf90/44cfc4/450bf4/450c18, the wingman
 * getters 44ba08/44b9b0, the clip 486704, the label draw 43122c, the
 * wingman select 44a748, the sound 433e3c and the order steps
 * 44db9c/44db4c/44dc6c stay guest calls. EDX:EAX exact; ECX is popped by
 * the only caller (449c69), so the framework returns the entry ECX (no exit
 * shim). Refuses compare mode (game calls, draws).
 *
 * The frame address goes into the graphics context: on the lamp path of
 * commands 4 and 5 (page 1) 44a1a0 stores EBP-8 in [[6c5f70h]+22Ch]
 * (44a3e9) before the clip and the label draw, and leaves it there after it
 * returns; the record {[EBP-8] = 0, [EBP-4] = the lamp sound} is read
 * through it. So hdd_execute has an ENTRY SHIM (hdd_exec_entry) that runs it at the original stack coordinate:
 * PUSH EBP gives the original's EBP; the caller's EBX/ESI/EDI go to the
 * original's save slots EBP-24h/-28h/-2Ch; EAX counts the guard-probed
 * pages and is restored; the floor (es2_machine_floor); {EBP,
 * outgoing ESP = EBP-2Ch, EBX, ESI, EDI} below the gap for the adapter,
 * whose locals are then the original's (EBP-20h..EBP-1: the stored pointer
 * and the record behind it are the original's) and whose game calls run
 * through mfd_orders_stack_call (mfd_orders_hooks.h) at the original's
 * outgoing ESP with the EBX/ESI/EDI and return address the core names; the
 * core re-reads the argument slot for each POP ECX. 28 pages (112 KiB), as
 * its sibling map_frame under the same 449bf8: the shim is reached only
 * under panel_frame's 48-page gap (through 449bf8, whose frame, return
 * address and argument slot take 24h bytes), so its probe ends inside that
 * gap; no shimmed hook is reachable from 44a1a0. Without the shim (direct and legacy calls) the plain bridge
 * is used and the locals live in a static scratch frame, zeroed per call
 * (hdd_exec_plain_frame, read by the in-vitro cases), so the stored pointer
 * then names that scratch (a deviation of the plain path only). No game
 * instruction is embedded: the shim only marshals registers and moves
 * ESP. */
#include "es2/core/hdd_exec.h"
#define HDD_EXEC_PAGES 28
__attribute__((used)) static mfd_orders_stack *hdd_exec_pending;
/* The plain path's frame: 30h bytes, f at +2Ch (f-2Ch..f-1 the original's
 * locals and saves); its address in hdd_exec_plain_frame. */
static BYTE hdd_exec_plain[0x30];
__attribute__((used)) static uint32_t hdd_exec_plain_frame;
static uint64_t __cdecl hdd_exec_hdd_execute(const uint32_t *a,void *u,const uint32_t *r) {
    const mfd_orders_stack *s=mfd_orders_take(&hdd_exec_pending,a,1);
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_hdd_execute);
    if(s) {
        mfd_orders_guest c={s,{{0,0,0,0},s->top}};
        g.call=mfd_orders_stack_call;g.context=&c;
        return es2_hdd_execute_registers(&math_native_memory,&g,&c.frame,s->ebp,s->esi,s->edi,a[0],r);
    }
    memset(hdd_exec_plain,0,sizeof(hdd_exec_plain));
    const uint32_t f=(uint32_t)(uintptr_t)(hdd_exec_plain+0x2c);
    hdd_exec_plain_frame=f;
    return es2_hdd_execute_registers(&math_native_memory,&g,NULL,f,0,0,a[0],r);
}
/* Callee counters (hk_hdd_execute_<suffix>), incremented only while the
 * hook is non-original; keys are the callee VAs. */
#define HDD_EXEC_CALL(va) {"call_" #va,ES2_COLLIDE_CALL,0x##va,0}
static callee_counter hdd_execute_counters[]={
    HDD_EXEC_CALL(44a60c),HDD_EXEC_CALL(44d0c0),HDD_EXEC_CALL(450bf4),HDD_EXEC_CALL(44d124),HDD_EXEC_CALL(450c18),
    HDD_EXEC_CALL(44cff8),HDD_EXEC_CALL(44ba08),HDD_EXEC_CALL(44d05c),HDD_EXEC_CALL(44b9b0),HDD_EXEC_CALL(486704),
    HDD_EXEC_CALL(43122c),HDD_EXEC_CALL(44cf90),HDD_EXEC_CALL(44cfc4),HDD_EXEC_CALL(44a748),HDD_EXEC_CALL(433e3c),
    HDD_EXEC_CALL(44db9c),HDD_EXEC_CALL(44db4c),HDD_EXEC_CALL(431610),HDD_EXEC_CALL(44dc10),HDD_EXEC_CALL(44dc6c),
};
#undef HDD_EXEC_CALL
static callee_counter *hdd_exec_hook_counters(unsigned hook,unsigned *count) {
    if(hook==ES2_HOOK_INDEX_hdd_execute){*count=sizeof(hdd_execute_counters)/sizeof(hdd_execute_counters[0]);return hdd_execute_counters;}
    *count=0;return NULL;
}
/* 44a1a0: push ebp; mov ebp,esp; add esp,-20h; push ebx; push esi; push
 * edi; mov ebx,[ebp+8]; movsx eax,byte [ebx+528h]. Relocated 6 bytes
 * (1,2,3). */
static const BYTE hdd_exec_hdd_execute_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xe0,0x53,0x56,0x57,0x8b,0x5d,0x08,0x0f,0xbe,0x83,0x28,0x05,0x00,0x00};
static const BYTE hdd_exec_lens[]={1,2,3};
static const es2_hook_extension hdd_exec_hdd_execute_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,hdd_exec_hdd_execute,TRUE};
static uint32_t __cdecl hdd_exec_hdd_execute32(const uint32_t *a,void *u) {return (uint32_t)hdd_exec_hdd_execute(a,u,weapons_no_registers);}
static es2_hook_descriptor hdd_exec_hook_descriptors[]={
    {ES2_HOOK_ID(hdd_execute),hdd_exec_hdd_execute_sig,19,6,hdd_exec_lens,3,1,hdd_exec_hdd_execute32,NULL,NULL,NULL,0},
};
/* The entry shim: PUSH EBP; the caller's EBX/ESI/EDI to EBP-24h/-28h/-2Ch
 * (where 44a1a0's prologue pushes them after its 20h bytes of locals); EAX
 * counts HDD_EXEC_PAGES guard-probed pages (OR 0 keeps their bytes) and is
 * restored; the floor; the context below the gap (or the floor); the
 * slot copied for the framework's dispatch; then MOV ESP,EBP; POP EBP; RET
 * (EBX/ESI/EDI are the caller's, which the dispatch preserved). EAX/EDX/ECX
 * pass through to the dispatch (they reach the callees). */
static int hdd_exec_entry(es2_machine *m,uint32_t bridge) {
    uint32_t ebp;const uint32_t context=shim_record_gap(m,bridge,&ebp,-0x24,3u,HDD_EXEC_PAGES,0x2cu);
    hdd_exec_pending=es2_guest_span(context,sizeof(mfd_orders_stack));
    const int ran=shim_gap_call(m,bridge,ebp,context,1);
    hdd_exec_pending=NULL;return ran;
}
ES2_SHIM(hdd_exec_shim,0x4a1a0u,hdd_exec_entry,0);
#endif
