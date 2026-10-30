#ifndef ES2_WEAPON_SLOTS_HOOKS_H
#define ES2_WEAPON_SLOTS_HOOKS_H
/* weapon_slots group (docs/reverse-engineering/weapons.md, section
 * "player weapon system"): weapon_frame 410b40, ready_step 410a3c,
 * selected_ready 410a04, chain_ready 410970, latch_set 410f04, link_toggle
 * 410f14, member_toggle 410cd0 and the range gauge gauge_sync 4420c8,
 * gauge_state 442120, gauge_link 442054, gauge_button 442154 and
 * gauge_mode 441f90. Every game call goes through the callee observer
 * (weapons_graph) and this group's bridge, which loads the EAX/EDX/ECX and
 * the EBX/ESI/EDI the original holds at that call (the weapon virtual +2Ch
 * body 40ecdc returns its caller's EBX high word; select_step 41074c reads
 * its caller's ESI), so the nested hooks (select_step, fire_group,
 * weapon_select, weapon_range and this group's own) keep their modes; the
 * weapon virtuals +2Ch/+50h/+60h and the lamp 441fa4 (H3) stay guest calls.
 * Exit shims for the ten hooks whose ECX differs from the entry ECX (all
 * but latch_set and gauge_mode): the shim records the caller's EBX/ESI/EDI
 * (read by the adapter before anything else, passed on where the original
 * passes them on), calls the framework's dispatch with the arguments
 * copied and, when the replacement ran, loads the ECX the core computed
 * (the panel_weapons pattern). Chained from vfs_capture_entry through
 * weapon_slots_capture_entry. No entry register is read beyond
 * the framework's EAX/EDX/ECX and the shim's EBX/ESI/EDI; no uninitialized
 * local. latch_set (span W+14h) and gauge_mode (span H+0B1h..0B2h) accept
 * compare mode (EAX compared relative to the span); the ten
 * with an exit shim refuse it (their ECX comes from the replacement phase
 * and no comparator sees it), and so does every hook with a game call.
 * No game instruction is embedded; the assembly only marshals registers. */
#include "es2/core/weapon_slots.h"
#include "sim_bridge.h"
/* Like weapons_machine_call, plus EBX, ESI and EDI from the core's block. */
__attribute__((naked,noinline)) static void __cdecl weapon_slots_machine_call(
    uint32_t target,const uint32_t *args,unsigned count,es2_weapon_registers *registers,const es2_weapon_slots_saved *saved) {
    __asm__ volatile(
        "pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        ES2_SIM_BRIDGE_ARG8
        "movl 12(%ebp),%esi\n\tmovl 16(%ebp),%ebx\n\tmovl %ebx,_weapons_bridge_count\n\t"
        "testl %ebx,%ebx\n\tjz 2f\n\t1: decl %ebx\n\tpushl (%esi,%ebx,4)\n\ttestl %ebx,%ebx\n\tjnz 1b\n\t"
        "2: movl %esp,_weapons_bridge_args\n\tmovl 24(%ebp),%eax\n\tmovl 0(%eax),%ebx\n\tmovl 4(%eax),%esi\n\tmovl 8(%eax),%edi\n\t"
        "movl 20(%ebp),%eax\n\tmovl 4(%eax),%edx\n\tmovl 8(%eax),%ecx\n\tmovl 0(%eax),%eax\n\tcall *8(%ebp)\n\t"
        "movl $0,_weapons_bridge_args\n\tmovl 20(%ebp),%edi\n\tmovl %eax,0(%edi)\n\tmovl %edx,4(%edi)\n\tmovl %ecx,8(%edi)\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
static void weapon_slots_guest_call(void *context,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    weapon_slots_machine_call(f,a,n,r,context);
}
/* Written by the exit shims (the caller's EBX/ESI/EDI) and read first by
 * the adapter; the adapter's result ECX for the shim. A nested hook of the
 * group writes and consumes its own values before the outer adapter reads
 * or writes them. */
__attribute__((used)) static uint32_t weapon_slots_ebx,weapon_slots_esi,weapon_slots_edi,weapon_slots_ecx,weapon_slots_ecx_valid;
static es2_weapon_slots_saved weapon_slots_entry(void) {
    es2_weapon_slots_saved s={weapon_slots_ebx,weapon_slots_esi,weapon_slots_edi};return s;
}
static es2_weapon_graph weapon_slots_graph(void *u,BYTE *scratch,unsigned hook,es2_weapon_slots_saved *saved) {
    es2_weapon_graph g=weapons_graph(u,scratch,hook);g.call=weapon_slots_guest_call;g.context=saved;return g;
}
static uint64_t weapon_slots_exit(uint64_t v,uint32_t ecx) { weapon_slots_ecx=ecx;weapon_slots_ecx_valid=1;return v; }
static uint64_t __cdecl weapon_slots_weapon_frame(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_slots_saved s=weapon_slots_entry();BYTE scratch[128]={0};uint32_t ecx;
    es2_weapon_graph g=weapon_slots_graph(u,scratch,ES2_HOOK_INDEX_weapon_frame,&s);
    return weapon_slots_exit(es2_weapon_frame_registers(&math_native_memory,&g,&s,a[0],a[1],(uint32_t)(uintptr_t)scratch,r,&ecx),ecx);
}
static uint64_t __cdecl weapon_slots_ready_step(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_slots_saved s=weapon_slots_entry();uint32_t ecx;
    es2_weapon_graph g=weapon_slots_graph(u,NULL,ES2_HOOK_INDEX_ready_step,&s);
    return weapon_slots_exit(es2_ready_step_registers(&math_native_memory,&g,&s,a[0],r,&ecx),ecx);
}
static uint64_t __cdecl weapon_slots_selected_ready(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_slots_saved s=weapon_slots_entry();uint32_t ecx;
    es2_weapon_graph g=weapon_slots_graph(u,NULL,ES2_HOOK_INDEX_selected_ready,&s);
    return weapon_slots_exit(es2_selected_ready_registers(&math_native_memory,&g,&s,a[0],a[1],a[2],r,&ecx),ecx);
}
static uint64_t __cdecl weapon_slots_chain_ready(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_slots_saved s=weapon_slots_entry();uint32_t ecx;
    es2_weapon_graph g=weapon_slots_graph(u,NULL,ES2_HOOK_INDEX_chain_ready,&s);
    return weapon_slots_exit(es2_chain_ready_registers(&math_native_memory,&g,&s,a[0],a[1],a[2],a[3],r,&ecx),ecx);
}
static uint64_t __cdecl weapon_slots_latch_set(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;return es2_latch_set_registers(&math_native_memory,a[0],r);
}
static uint64_t __cdecl weapon_slots_link_toggle(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;uint32_t ecx;
    return weapon_slots_exit(es2_link_toggle_registers(&math_native_memory,a[0],r,&ecx),ecx);
}
static uint64_t __cdecl weapon_slots_member_toggle(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;(void)r;uint32_t ecx;
    return weapon_slots_exit(es2_member_toggle_registers(&math_native_memory,a[0],a[1],&ecx),ecx);
}
static uint64_t __cdecl weapon_slots_gauge_sync(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_slots_saved s=weapon_slots_entry();uint32_t ecx;
    es2_weapon_graph g=weapon_slots_graph(u,NULL,ES2_HOOK_INDEX_gauge_sync,&s);
    return weapon_slots_exit(es2_gauge_sync_registers(&math_native_memory,&g,&s,a[0],a[1],r,&ecx),ecx);
}
static uint64_t __cdecl weapon_slots_gauge_state(const uint32_t *a,void *u,const uint32_t *r) {
    uint32_t ecx;
    return weapon_slots_exit(es2_gauge_state_registers(&math_native_memory,(uint32_t)(uintptr_t)u,a[0],r,&ecx),ecx);
}
static uint64_t __cdecl weapon_slots_gauge_link(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_slots_saved s=weapon_slots_entry();uint32_t ecx;
    es2_weapon_graph g=weapon_slots_graph(u,NULL,ES2_HOOK_INDEX_gauge_link,&s);
    return weapon_slots_exit(es2_gauge_link_registers(&math_native_memory,&g,&s,a[0],a[1],r,&ecx),ecx);
}
static uint64_t __cdecl weapon_slots_gauge_button(const uint32_t *a,void *u,const uint32_t *r) {
    (void)r;es2_weapon_slots_saved s=weapon_slots_entry();uint32_t ecx;
    es2_weapon_graph g=weapon_slots_graph(u,NULL,ES2_HOOK_INDEX_gauge_button,&s);
    return weapon_slots_exit(es2_gauge_button_registers(&math_native_memory,&g,&s,a[0],a[1],&ecx),ecx);
}
static uint64_t __cdecl weapon_slots_gauge_mode(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;return es2_gauge_mode_registers(&math_native_memory,a[0],a[1],r);
}
/* Callee counters (hk_<hook>_<suffix>), incremented only while the hook is
 * non-original. Keys: the callee VA for calls, the vtable offset for slots. */
static callee_counter weapon_frame_counters[]={
    {"call_41074c",ES2_COLLIDE_CALL,0x41074c,0},
    {"call_410a3c",ES2_COLLIDE_CALL,0x410a3c,0},
    {"call_442120",ES2_COLLIDE_CALL,0x442120,0},
    {"call_410ae4",ES2_COLLIDE_CALL,0x410ae4,0},
    {"call_410f04",ES2_COLLIDE_CALL,0x410f04,0},
    {"call_410f14",ES2_COLLIDE_CALL,0x410f14,0},
    {"call_4420c8",ES2_COLLIDE_CALL,0x4420c8,0},
    {"call_410708",ES2_COLLIDE_CALL,0x410708,0},
    {"call_410970",ES2_COLLIDE_CALL,0x410970,0},
    {"slot_50",ES2_COLLIDE_SLOT,0x50,0},
};
static callee_counter ready_step_counters[]={
    {"call_410a04",ES2_COLLIDE_CALL,0x410a04,0},
    {"call_41074c",ES2_COLLIDE_CALL,0x41074c,0},
    {"call_410708",ES2_COLLIDE_CALL,0x410708,0},
};
static callee_counter selected_ready_counters[]={
    {"call_410970",ES2_COLLIDE_CALL,0x410970,0},
};
static callee_counter chain_ready_counters[]={
    {"slot_2c",ES2_COLLIDE_SLOT,0x2c,0},
    {"call_40e5f8",ES2_COLLIDE_CALL,0x40e5f8,0},
    {"slot_60",ES2_COLLIDE_SLOT,0x60,0},
    {"call_410970",ES2_COLLIDE_CALL,0x410970,0},
};
static callee_counter gauge_sync_counters[]={
    {"call_441f90",ES2_COLLIDE_CALL,0x441f90,0},
    {"call_441fa4",ES2_COLLIDE_CALL,0x441fa4,0},
    {"call_442054",ES2_COLLIDE_CALL,0x442054,0},
};
static callee_counter gauge_link_counters[]={
    {"call_410f14",ES2_COLLIDE_CALL,0x410f14,0},
};
static callee_counter gauge_button_counters[]={
    {"call_441f90",ES2_COLLIDE_CALL,0x441f90,0},
    {"call_442054",ES2_COLLIDE_CALL,0x442054,0},
    {"call_441fa4",ES2_COLLIDE_CALL,0x441fa4,0},
};
static callee_counter *weapon_slots_hook_counters(unsigned hook,unsigned *count) {
#define WEAPON_SLOTS_COUNTERS(name) if(hook==ES2_HOOK_INDEX_##name) { *count=sizeof(name##_counters)/sizeof(name##_counters[0]);return name##_counters; }
    WEAPON_SLOTS_COUNTERS(weapon_frame)
    WEAPON_SLOTS_COUNTERS(ready_step)
    WEAPON_SLOTS_COUNTERS(selected_ready)
    WEAPON_SLOTS_COUNTERS(chain_ready)
    WEAPON_SLOTS_COUNTERS(gauge_sync)
    WEAPON_SLOTS_COUNTERS(gauge_link)
    WEAPON_SLOTS_COUNTERS(gauge_button)
#undef WEAPON_SLOTS_COUNTERS
    *count=0;return NULL;
}
/* Prologues: push ebp; mov ebp,esp; then add esp,-18h (410b40), push
 * ecx/ebx/esi/edi (410a3c, 410970, 4420c8, 410cd0), mov eax,[ebp+8]
 * (410a04, 410f04, 442120, 442054, 441f90) or push ebx with a three-byte
 * load (410f14 mov eax,[ebp+8]; 442154 mov ebx,[ebp+0Ch]). */
static const BYTE weapon_slots_frame_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xe8,0x66,0x83,0x3d,0x28,0x25,0x4d,0x00,0x00};
static const BYTE weapon_slots_ready_sig[]={0x55,0x8b,0xec,0x51,0x53,0x56,0x57,0x8b,0x5d,0x08};
static const BYTE weapon_slots_selected_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x8d,0x50,0x14};
static const BYTE weapon_slots_chain_sig[]={0x55,0x8b,0xec,0x53,0x56,0x57,0x8b,0x7d,0x10};
static const BYTE weapon_slots_latch_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x83,0xc0,0x14};
static const BYTE weapon_slots_link_sig[]={0x55,0x8b,0xec,0x53,0x8b,0x45,0x08,0x8a,0x50,0x1d};
static const BYTE weapon_slots_member_sig[]={0x55,0x8b,0xec,0x53,0x56,0x8b,0x45,0x08,0x8b,0x55,0x0c};
static const BYTE weapon_slots_sync_sig[]={0x55,0x8b,0xec,0x53,0x56,0x8b,0x75,0x0c,0x8b,0x5d,0x08};
static const BYTE weapon_slots_state_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x66,0x8b,0x90,0xb1,0x00,0x00,0x00};
static const BYTE weapon_slots_glink_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x8a,0x55,0x0c};
static const BYTE weapon_slots_button_sig[]={0x55,0x8b,0xec,0x53,0x8b,0x5d,0x0c,0x8b,0x4d,0x08};
static const BYTE weapon_slots_mode_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x66,0x8b,0x55,0x0c};
static const BYTE weapon_slots_frame_lens[]={1,2,3},weapon_slots_push_lens[]={1,2,1,1},weapon_slots_load_lens[]={1,2,1,3};
/* Compare: the declared bytes, EDX equal (the entry EDX or its high
 * word with m), EAX relative to the span (latch_set W+14h, gauge_mode H). */
static BOOL __cdecl weapon_slots_span_equal(const es2_hook_comparison *v,void *u) {
    (void)u;
    return !memcmp(v->original[0],v->replacement[0],v->sizes[0])&&v->original_edx==v->replacement_edx&&
        v->original_result-(uint32_t)(uintptr_t)v->original[0]==v->replacement_result-(uint32_t)(uintptr_t)v->replacement[0];
}
static const es2_hook_span weapon_slots_latch_span[]={{0,0x14,1}},weapon_slots_mode_span[]={{0,0xb1,2}};
#define WEAPON_SLOTS_EXT(name) WEAPON_SLOTS_EXT_MODE(name,TRUE)
#define WEAPON_SLOTS_EXT_MODE(name,unsupported) \
    static const es2_hook_extension weapon_slots_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,weapon_slots_##name,unsupported}; \
    static uint32_t __cdecl weapon_slots_##name##32(const uint32_t *a,void *u) {return (uint32_t)weapon_slots_##name(a,u,weapons_no_registers);}
WEAPON_SLOTS_EXT(weapon_frame)
WEAPON_SLOTS_EXT(ready_step)
WEAPON_SLOTS_EXT(selected_ready)
WEAPON_SLOTS_EXT(chain_ready)
WEAPON_SLOTS_EXT_MODE(latch_set,FALSE)
WEAPON_SLOTS_EXT(link_toggle)
WEAPON_SLOTS_EXT(member_toggle)
WEAPON_SLOTS_EXT(gauge_sync)
WEAPON_SLOTS_EXT(gauge_state)
WEAPON_SLOTS_EXT(gauge_link)
WEAPON_SLOTS_EXT(gauge_button)
WEAPON_SLOTS_EXT_MODE(gauge_mode,FALSE)
#undef WEAPON_SLOTS_EXT
#undef WEAPON_SLOTS_EXT_MODE
static es2_hook_descriptor weapon_slots_hook_descriptors[]={
    {ES2_HOOK_ID(weapon_frame),weapon_slots_frame_sig,14,6,weapon_slots_frame_lens,3,2,weapon_slots_weapon_frame32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(ready_step),weapon_slots_ready_sig,10,5,weapon_slots_push_lens,4,1,weapon_slots_ready_step32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(selected_ready),weapon_slots_selected_sig,9,6,weapon_slots_frame_lens,3,3,weapon_slots_selected_ready32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(chain_ready),weapon_slots_chain_sig,9,5,weapon_slots_push_lens,4,4,weapon_slots_chain_ready32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(latch_set),weapon_slots_latch_sig,9,6,weapon_slots_frame_lens,3,1,weapon_slots_latch_set32,
        weapon_slots_span_equal,NULL,weapon_slots_latch_span,1},
    {ES2_HOOK_ID(link_toggle),weapon_slots_link_sig,10,7,weapon_slots_load_lens,4,1,weapon_slots_link_toggle32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(member_toggle),weapon_slots_member_sig,11,5,weapon_slots_push_lens,4,2,weapon_slots_member_toggle32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(gauge_sync),weapon_slots_sync_sig,11,5,weapon_slots_push_lens,4,2,weapon_slots_gauge_sync32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(gauge_state),weapon_slots_state_sig,13,6,weapon_slots_frame_lens,3,1,weapon_slots_gauge_state32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(gauge_link),weapon_slots_glink_sig,9,6,weapon_slots_frame_lens,3,2,weapon_slots_gauge_link32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(gauge_button),weapon_slots_button_sig,10,7,weapon_slots_load_lens,4,2,weapon_slots_gauge_button32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(gauge_mode),weapon_slots_mode_sig,10,6,weapon_slots_frame_lens,3,2,weapon_slots_gauge_mode32,
        weapon_slots_span_equal,NULL,weapon_slots_mode_span,1},
};
/* The exit shims: a frame, the caller's EBX/ESI/EDI recorded, the
 * arguments copied, the framework's dispatch as a call, then the core's ECX
 * if the replacement ran (the original mode keeps the original's own ECX).
 * Flags are not preserved (no caller reads them after these calls). */
#define WEAPON_SLOTS_SHIM(name,pushes) __attribute__((used)) static void *weapon_slots_##name##_bridge; \
    __attribute__((naked)) static void weapon_slots_##name##_entry(void) { __asm__ volatile( \
    "pushl %ebp\n\tmovl %esp,%ebp\n\tmovl %ebx,_weapon_slots_ebx\n\tmovl %esi,_weapon_slots_esi\n\tmovl %edi,_weapon_slots_edi\n\t" \
    pushes "movl $0,_weapon_slots_ecx_valid\n\tcall *_weapon_slots_" #name "_bridge\n\tmovl %ebp,%esp\n\t" \
    "cmpl $0,_weapon_slots_ecx_valid\n\tje 1f\n\tmovl _weapon_slots_ecx,%ecx\n\tmovl $0,_weapon_slots_ecx_valid\n\t" \
    "1: popl %ebp\n\tret\n\t"); }
#define WEAPON_SLOTS_ARGS1 "pushl 8(%ebp)\n\t"
#define WEAPON_SLOTS_ARGS2 "pushl 12(%ebp)\n\t" WEAPON_SLOTS_ARGS1
#define WEAPON_SLOTS_ARGS3 "pushl 16(%ebp)\n\t" WEAPON_SLOTS_ARGS2
#define WEAPON_SLOTS_ARGS4 "pushl 20(%ebp)\n\t" WEAPON_SLOTS_ARGS3
WEAPON_SLOTS_SHIM(frame,WEAPON_SLOTS_ARGS2)
WEAPON_SLOTS_SHIM(ready,WEAPON_SLOTS_ARGS1)
WEAPON_SLOTS_SHIM(selected,WEAPON_SLOTS_ARGS3)
WEAPON_SLOTS_SHIM(chain,WEAPON_SLOTS_ARGS4)
WEAPON_SLOTS_SHIM(link,WEAPON_SLOTS_ARGS1)
WEAPON_SLOTS_SHIM(member,WEAPON_SLOTS_ARGS2)
WEAPON_SLOTS_SHIM(sync,WEAPON_SLOTS_ARGS2)
WEAPON_SLOTS_SHIM(state,WEAPON_SLOTS_ARGS1)
WEAPON_SLOTS_SHIM(glink,WEAPON_SLOTS_ARGS2)
WEAPON_SLOTS_SHIM(button,WEAPON_SLOTS_ARGS2)
#undef WEAPON_SLOTS_SHIM
/* Called by vfs_capture_entry after the framework installed its jump:
 * redirect the jumps of the ten shimmed hooks to their shims and keep
 * h->patch consistent, so uninstall still validates the exact patch. The
 * chain continues in
 * panel_frame_capture_entry (panel_frame_hooks.h). */
static BOOL panel_frame_capture_entry(es2_hook *h);
static BOOL weapon_slots_capture_entry(es2_hook *h) {
    void **bridge;void (*entry)(void);
    switch(h->descriptor->rva) {
    case 0x10b40u:bridge=&weapon_slots_frame_bridge;entry=weapon_slots_frame_entry;break;
    case 0x10a3cu:bridge=&weapon_slots_ready_bridge;entry=weapon_slots_ready_entry;break;
    case 0x10a04u:bridge=&weapon_slots_selected_bridge;entry=weapon_slots_selected_entry;break;
    case 0x10970u:bridge=&weapon_slots_chain_bridge;entry=weapon_slots_chain_entry;break;
    case 0x10f14u:bridge=&weapon_slots_link_bridge;entry=weapon_slots_link_entry;break;
    case 0x10cd0u:bridge=&weapon_slots_member_bridge;entry=weapon_slots_member_entry;break;
    case 0x420c8u:bridge=&weapon_slots_sync_bridge;entry=weapon_slots_sync_entry;break;
    case 0x42120u:bridge=&weapon_slots_state_bridge;entry=weapon_slots_state_entry;break;
    case 0x42054u:bridge=&weapon_slots_glink_bridge;entry=weapon_slots_glink_entry;break;
    case 0x42154u:bridge=&weapon_slots_button_bridge;entry=weapon_slots_button_entry;break;
    default:return panel_frame_capture_entry(h);
    }
    DWORD old,ignored;
    if(memcmp(h->site,h->patch,h->descriptor->relocate_size))return FALSE;
    if(!VirtualProtect(h->site,h->descriptor->relocate_size,PAGE_EXECUTE_READWRITE,&old))return FALSE;
    *bridge=(void *)(uintptr_t)((uint32_t)(uintptr_t)(h->site+5)+math_native_read32(NULL,(uint32_t)(uintptr_t)(h->patch+1)));
    math_native_write32(NULL,(uint32_t)(uintptr_t)(h->patch+1),(uint32_t)(uintptr_t)entry-(uint32_t)(uintptr_t)(h->site+5));
    memcpy(h->site,h->patch,h->descriptor->relocate_size);
    BOOL flushed=FlushInstructionCache(GetCurrentProcess(),h->site,h->descriptor->relocate_size);
    BOOL protected=VirtualProtect(h->site,h->descriptor->relocate_size,old,&ignored);
    return flushed && protected;
}
#endif
