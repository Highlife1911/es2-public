#ifndef ES2_HDD_MAP_HOOKS_H
#define ES2_HDD_MAP_HOOKS_H
/* hdd_map group (docs/reverse-engineering/cockpit.md, section
 * "HDD map frame"): the HDD map frame 44c988 (map_frame), slot 1 of
 * the HDDMap table 49d9d8, called by 449f64 under the HDD draw 449bf8.
 * Core in core/src/hdd_map.c. Every game call goes through the callee
 * observer (weapons_graph) with the original's EAX/EDX/ECX, so the nested
 * hooks (map_areas 44d450, map_click 44d6e0 and every hook below the guest
 * calls, and the wingman pick 44d82c, the hook wingman_pick)
 * keep their modes; the clock 467828, the tick 44d370, the button blink 44f644, the panel, label and
 * button draws 44de14/43122c/438aac/44dd74/44dd14, the clip 486704 and the
 * map draw 44e334 stay guest calls. EDX:EAX exact; ECX is popped by the
 * only caller (449fbb), so the framework returns the entry ECX (no exit
 * shim). Refuses compare mode (game calls, draws).
 *
 * The frame address goes into the graphics context: 44c988 stores
 * EBP-0Ch in [[6c5f70h]+22Ch] before the button and map draws (44cb2e,
 * 44cc32) and leaves it there after it returns; the draws read the record
 * {[EBP-0Ch] = 0, [EBP-8], [EBP-4] = the context} through it, and on the
 * button path [EBP-8] is the residue of the stack below the caller. So
 * map_frame has an ENTRY SHIM (hdd_map_entry, chained from
 * mission_root_capture_entry through hdd_map_capture_entry) that runs it at the original stack coordinate:
 * PUSH EBP gives the original's EBP; the caller's EBX/ESI go to the
 * original's save slots EBP-24h/-28h; EAX counts the guard-probed pages
 * and is restored; ES2_SHIM_FLOOR (weapons_hooks.h); {EBP, outgoing
 * ESP = EBP-28h, EBX, ESI, EDI} below the gap for the adapter, whose
 * locals are then the original's (EBP-20h..EBP-1, the stored pointer and
 * the residue the draws read are the original's) and whose game calls run
 * through ai_behaviour_stack_call (ai_behaviour_hooks.h) at the original's
 * outgoing ESP with the EBX/ESI/EDI and return address the core names; the
 * core re-reads the argument slot for each POP ECX. 28 pages (112 KiB) for
 * the map draw tree and the nested hooks' host frames: the shim is reached
 * only under panel_frame's 48-page gap (through 449bf8 and 449f64, about
 * 0.6 KiB of game frames), so its probe ends inside that gap. Without the
 * shim (direct and legacy calls) the plain bridge is used and the locals
 * live in a zeroed scratch frame (hdd_map_plain_frame, read by the in-vitro
 * cases). No game instruction is embedded: the assembly only marshals
 * registers and moves ESP. */
#include "es2/core/hdd_map.h"
#define HDD_MAP_PAGES 28
#define HDD_MAP_STRING_(x) #x
#define HDD_MAP_STRING(x) HDD_MAP_STRING_(x)
/* The shim's context: {EBP, outgoing ESP, the caller's EBX/ESI/EDI}. */
typedef struct hdd_map_stack { uint32_t ebp,top,ebx,esi,edi; } hdd_map_stack;
typedef struct hdd_map_guest { const hdd_map_stack *stack; es2_map_frame frame; } hdd_map_guest;
__attribute__((used)) static hdd_map_stack *hdd_map_pending;
__attribute__((used)) static void *hdd_map_bridge;
/* The shim's EAX while it probes the gap (main thread only). */
__attribute__((used)) static uint32_t hdd_map_eax;
/* The plain path's frame pointer of the last call (host memory). */
__attribute__((used)) static uint32_t hdd_map_plain_frame;
static void hdd_map_stack_call(void *context,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    const hdd_map_guest *c=context;
    const uint32_t call[6]={c->frame.call.ebx,c->frame.call.esi,c->frame.call.edi,c->stack->ebp,c->stack->top,c->frame.call.ret};
    ai_behaviour_stack_call(f,a,n,r,call);
}
/* The shim's context belongs to this invocation only if the slot above its
 * EBP is this call's argument. */
static const hdd_map_stack *hdd_map_take(const uint32_t *a) {
    const hdd_map_stack *s=hdd_map_pending;hdd_map_pending=NULL;
    if(s&&math_native_read32(NULL,s->ebp+8u)!=a[0])s=NULL;
    return s;
}
static uint64_t __cdecl hdd_map_map_frame(const uint32_t *a,void *u,const uint32_t *r) {
    const hdd_map_stack *s=hdd_map_take(a);
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_map_frame);
    if(s) {
        hdd_map_guest c={s,{{0,0,0,0},s->top}};
        g.call=hdd_map_stack_call;g.context=&c;
        return es2_map_frame_registers(&math_native_memory,&g,&c.frame,s->ebp,s->esi,s->edi,a[0],r);
    }
    BYTE scratch[0x30]={0};
    const uint32_t f=(uint32_t)(uintptr_t)(scratch+0x28);
    hdd_map_plain_frame=f;
    return es2_map_frame_registers(&math_native_memory,&g,NULL,f,0,0,a[0],r);
}
/* Callee counters (hk_map_frame_<suffix>), incremented only while the hook
 * is non-original; keys are the callee VAs. */
#define HDD_MAP_CALL(va) {"call_" #va,ES2_COLLIDE_CALL,0x##va,0}
static callee_counter map_frame_counters[]={
    HDD_MAP_CALL(44d450),HDD_MAP_CALL(467828),HDD_MAP_CALL(44d370),HDD_MAP_CALL(44d6e0),HDD_MAP_CALL(44d82c),
    HDD_MAP_CALL(44f644),HDD_MAP_CALL(44de14),HDD_MAP_CALL(43122c),HDD_MAP_CALL(438aac),HDD_MAP_CALL(44dd74),
    HDD_MAP_CALL(44dd14),HDD_MAP_CALL(486704),HDD_MAP_CALL(44e334),
};
#undef HDD_MAP_CALL
static callee_counter *hdd_map_hook_counters(unsigned hook,unsigned *count) {
    if(hook==ES2_HOOK_INDEX_map_frame){*count=sizeof(map_frame_counters)/sizeof(map_frame_counters[0]);return map_frame_counters;}
    *count=0;return NULL;
}
/* 44c988: push ebp; mov ebp,esp; add esp,-20h; push ebx; push esi; mov
 * ebx,[ebp+8]; cmp byte [ebx+14Dh],0. Relocated 6 bytes (1,2,3). */
static const BYTE hdd_map_map_frame_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xe0,0x53,0x56,0x8b,0x5d,0x08,0x80,0xbb,0x4d,0x01,0x00,0x00,0x00};
static const BYTE hdd_map_lens[]={1,2,3};
static const es2_hook_extension hdd_map_map_frame_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,hdd_map_map_frame,TRUE};
static uint32_t __cdecl hdd_map_map_frame32(const uint32_t *a,void *u) {return (uint32_t)hdd_map_map_frame(a,u,weapons_no_registers);}
static es2_hook_descriptor hdd_map_hook_descriptors[]={
    {ES2_HOOK_ID(map_frame),hdd_map_map_frame_sig,18,6,hdd_map_lens,3,1,hdd_map_map_frame32,NULL,NULL,NULL,0},
};
/* The entry shim: PUSH EBP; the caller's EBX/ESI to EBP-24h/-28h (where
 * 44c988's prologue pushes them after its 20h bytes of locals); EAX counts
 * HDD_MAP_PAGES guard-probed pages (OR 0 keeps their bytes) and is
 * restored; ES2_SHIM_FLOOR; the context below the gap (or the floor); the
 * slot copied for the framework's dispatch; then MOV ESP,EBP; POP EBP; RET
 * (EBX/ESI/EDI are the caller's, which the dispatch preserved). EAX/EDX/ECX
 * pass through to the dispatch (they reach map_areas or the clock). */
__attribute__((naked)) static void hdd_map_entry(void) {
    __asm__ volatile(
        "pushl %ebp\n\tmovl %esp,%ebp\n\tmovl %ebx,-0x24(%ebp)\n\tmovl %esi,-0x28(%ebp)\n\t"
        "movl %eax,_hdd_map_eax\n\tmovl $" HDD_MAP_STRING(HDD_MAP_PAGES) ",%eax\n\t1: subl $4096,%esp\n\torl $0,(%esp)\n\tdecl %eax\n\tjnz 1b\n\t" ES2_SHIM_FLOOR
        "subl $20,%esp\n\tmovl %ebp,(%esp)\n\tleal -0x28(%ebp),%eax\n\tmovl %eax,4(%esp)\n\t"
        "movl %ebx,8(%esp)\n\tmovl %esi,12(%esp)\n\tmovl %edi,16(%esp)\n\t"
        "movl %esp,_hdd_map_pending\n\tmovl _hdd_map_eax,%eax\n\t"
        "pushl 8(%ebp)\n\tcall *_hdd_map_bridge\n\t"
        "movl $0,_hdd_map_pending\n\tmovl %ebp,%esp\n\tpopl %ebp\n\tret\n\t");
}
/* Called by mission_root_capture_entry after the framework installed its
 * jump: redirect the jump of map_frame to its shim and keep h->patch
 * consistent, so uninstall still validates the exact patch. The chain
 * continues in mfd_keys_capture_entry (mfd_keys_hooks.h). */
static BOOL mfd_keys_capture_entry(es2_hook *h);
static BOOL hdd_map_capture_entry(es2_hook *h) {
    if(h->descriptor->rva!=0x4c988u)return mfd_keys_capture_entry(h);
    DWORD old,ignored;
    if(memcmp(h->site,h->patch,h->descriptor->relocate_size))return FALSE;
    if(!VirtualProtect(h->site,h->descriptor->relocate_size,PAGE_EXECUTE_READWRITE,&old))return FALSE;
    hdd_map_bridge=(void *)(uintptr_t)((uint32_t)(uintptr_t)(h->site+5)+math_native_read32(NULL,(uint32_t)(uintptr_t)(h->patch+1)));
    math_native_write32(NULL,(uint32_t)(uintptr_t)(h->patch+1),(uint32_t)(uintptr_t)hdd_map_entry-(uint32_t)(uintptr_t)(h->site+5));
    memcpy(h->site,h->patch,h->descriptor->relocate_size);
    BOOL flushed=FlushInstructionCache(GetCurrentProcess(),h->site,h->descriptor->relocate_size);
    BOOL protected=VirtualProtect(h->site,h->descriptor->relocate_size,old,&ignored);
    return flushed && protected;
}
#endif
