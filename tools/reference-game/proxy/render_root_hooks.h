#ifndef ES2_RENDER_ROOT_HOOKS_H
#define ES2_RENDER_ROOT_HOOKS_H
#include "guest_native.h"
/* render_root group (docs/reverse-engineering/main_loop.md, section
 * "the render root as own code"): the render root 45fca8
 * (render_root), the stage dispatcher 401d94 (stage_dispatch) and the
 * vector copy 40707c (vector_copy).
 *
 * render_root calls every game function through the callee observer
 * (weapons_graph) with the original's EAX/EDX/ECX, EBX (the view block
 * 4D24BCh), the caller's ESI/EDI and EBP and the return address, so the
 * nested hooks (stage_dispatch, panel_frame, vector_copy, cockpit_sync and
 * every hook below the draws) keep their own modes; the detail settings
 * 45d490/45d50c, the jolt point 42d870, the camera setup 48c2e4, the
 * terrain draw 46cb9c, the object binning 42841c, the view test 42db18, the
 * view transform 48c444, the sky/horizon 42e700, the eye cell objects
 * 42883c, the HUD draw (virtual +8 of [H+1F5h]), the port getter 433158,
 * the two port frames (virtual +10h) and the widget draw 434520 stay guest
 * calls (H3). The callees read stack residue (the panel frame's EventPort
 * records, the draw trees were not audited), so render_root has an ENTRY
 * SHIM (render_root_entry) that runs it at the original
 * stack coordinate: 45fca8 has no frame pointer, so the shim does the
 * original's PUSH EBX itself (the caller's EBX at T-4 for entry ESP T),
 * reserves a guard-probed gap of RENDER_ROOT_PAGES pages and leaves {the
 * caller's EBP, outgoing ESP = T-14h, ESI, EDI, ECX} below it in
 * render_root_pending; the core's 16 bytes of locals are the original's
 * (T-14h .. T-5), every game call runs through ai_behaviour_stack_call
 * (ai_behaviour_hooks.h) with the registers and return address the core
 * names, and the core re-reads the argument slot for each POP ECX. The
 * adapter writes the core's ECX into the context, which the shim loads
 * after the framework's dispatch, then the shim pops the caller's EBX from
 * T-4 as the original's epilogue does: EAX, EDX and ECX return exactly.
 * Without the shim (direct and legacy calls) the adapter keeps the locals
 * in a buffer and calls through logic_frame_machine_call
 * (logic_frame_hooks.h; EBX as named, ESI/EDI 0, EBP the buffer's end).
 *
 * Stack budget (main_loop.md, "Stack budget"): the gap must hold every callee's
 * footprint below the original coordinate, the largest being cockpit_sync's
 * own entry shim (112 pages) with its host frames; the cockpit_sync and
 * panel_frame gaps lie INSIDE this gap (they start at the original
 * coordinate, above the render root's host frames), they do not add to it.
 *
 * stage_dispatch calls the table entries through logic_frame_machine_call
 * with the original's EBX (the entry), ESI (the index) and EDI (the stage)
 * and EBP at a host copy of the original's frame ([EBP+8] = the stage); no
 * callback reads them before saving them (main_loop.md). vector_copy is a
 * leaf on guest memory and accepts compare mode with a 12-byte span on its
 * destination. render_root and stage_dispatch refuse compare (game calls).
 * No game instruction is embedded; the shim only marshals registers
 * and moves ESP. */
#include "es2/core/render_root.h"
/* 464 KiB: cockpit_sync's 112-page gap (448 KiB) and its host frames (below
 * two pages) plus a margin; panel_frame's 48 pages, the stage callbacks
 * and the H3 draw trees are smaller. */
#define RENDER_ROOT_PAGES 116
typedef struct render_root_stack { uint32_t ebp,top,esi,edi,ecx; } render_root_stack;
__attribute__((used)) static render_root_stack *render_root_pending;
typedef struct render_root_guest { const render_root_stack *stack; es2_render_root frame; uint32_t ebp; } render_root_guest;
static void render_root_stack_call(void *context,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    const render_root_guest *c=context;
    const uint32_t call[6]={c->frame.call.ebx,c->frame.call.esi,c->frame.call.edi,c->stack->ebp,c->stack->top,c->frame.call.ret};
    ai_behaviour_stack_call(f,a,n,r,call);
}
static void render_root_plain_call(void *context,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    const render_root_guest *c=context;
    const uint32_t call[4]={c->frame.call.ebx,c->frame.call.esi,c->frame.call.edi,c->ebp};
    logic_frame_machine_call(f,a,n,r,call);
}
static uint64_t __cdecl render_root_render_root(const uint32_t *a,void *u,const uint32_t *r) {
    (void)a;
    /* The shim publishes its context right before the framework's dispatch
     * and nothing can enter 45fca8 in between; without the shim the
     * context is NULL. */
    render_root_stack *s=render_root_pending;render_root_pending=NULL;
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_render_root);
    BYTE frame[ES2_RENDER_ROOT_FRAME]={0};
    render_root_guest c={s,{{0,0,0,0},0},(uint32_t)(uintptr_t)(frame+ES2_RENDER_ROOT_FRAME)};
    uint32_t ecx,esi=0,edi=0;
    if(s) {
        c.frame.top=s->top;esi=s->esi;edi=s->edi;
        g.scratch=s->top;g.call=render_root_stack_call;g.context=&c;
    } else { g.scratch=(uint32_t)(uintptr_t)frame;g.call=render_root_plain_call;g.context=&c; }
    const uint64_t v=es2_render_root_registers(&math_native_memory,&g,&c.frame,esi,edi,r,&ecx);
    if(s)s->ecx=ecx;
    return v;
}
typedef struct render_root_dispatch_guest { es2_weapon_frame frame; uint32_t ebp; } render_root_dispatch_guest;
static void render_root_dispatch_call(void *context,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    const render_root_dispatch_guest *c=context;
    const uint32_t call[4]={c->frame.ebx,c->frame.esi,c->frame.edi,c->ebp};
    logic_frame_machine_call(f,a,n,r,call);
}
static uint64_t __cdecl render_root_stage_dispatch(const uint32_t *a,void *u,const uint32_t *r) {
    /* A copy of the original's frame for EBP: saved EBP, return, stage. */
    uint32_t original[3]={0,0,a[0]};
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_stage_dispatch);
    render_root_dispatch_guest c={{0,0,0,0},(uint32_t)(uintptr_t)original};
    g.call=render_root_dispatch_call;g.context=&c;
    return es2_stage_dispatch_registers(&math_native_memory,&g,&c.frame,a[0],r);
}
static uint64_t __cdecl render_root_vector_copy(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;(void)r;uint32_t ecx;return es2_vector_copy_registers(&math_native_memory,a[0],a[1],&ecx);
}
/* Callee counters (hk_<hook>_<suffix>), incremented only while the hook is
 * non-original. Keys: the callee VA for calls, the vtable offset for slots
 * (+8 the HUD draw, +10h both port frames); for stage_dispatch the original
 * VAs of the stage-5 and stage-6 callbacks. */
static callee_counter render_root_counters[]={
    {"call_401d94",ES2_COLLIDE_CALL,0x401d94,0},
    {"call_45d490",ES2_COLLIDE_CALL,0x45d490,0},
    {"call_45d50c",ES2_COLLIDE_CALL,0x45d50c,0},
    {"call_42d870",ES2_COLLIDE_CALL,0x42d870,0},
    {"call_48c2e4",ES2_COLLIDE_CALL,0x48c2e4,0},
    {"call_46cb9c",ES2_COLLIDE_CALL,0x46cb9c,0},
    {"call_4327ac",ES2_COLLIDE_CALL,0x4327ac,0},
    {"call_42841c",ES2_COLLIDE_CALL,0x42841c,0},
    {"call_42db18",ES2_COLLIDE_CALL,0x42db18,0},
    {"call_40707c",ES2_COLLIDE_CALL,0x40707c,0},
    {"call_48c444",ES2_COLLIDE_CALL,0x48c444,0},
    {"call_42e700",ES2_COLLIDE_CALL,0x42e700,0},
    {"call_42883c",ES2_COLLIDE_CALL,0x42883c,0},
    {"call_41b130",ES2_COLLIDE_CALL,0x41b130,0},
    {"slot_8",ES2_COLLIDE_SLOT,0x8,0},
    {"call_433158",ES2_COLLIDE_CALL,0x433158,0},
    {"slot_10",ES2_COLLIDE_SLOT,0x10,0},
    {"call_434520",ES2_COLLIDE_CALL,0x434520,0},
};
static callee_counter stage_dispatch_counters[]={
    {"call_405ea4",ES2_COLLIDE_CALL,0x405ea4,0},
    {"call_4077e8",ES2_COLLIDE_CALL,0x4077e8,0},
    {"call_407b3c",ES2_COLLIDE_CALL,0x407b3c,0},
    {"call_4083e0",ES2_COLLIDE_CALL,0x4083e0,0},
    {"call_408f9c",ES2_COLLIDE_CALL,0x408f9c,0},
    {"call_409904",ES2_COLLIDE_CALL,0x409904,0},
    {"call_409a1c",ES2_COLLIDE_CALL,0x409a1c,0},
    {"call_40a108",ES2_COLLIDE_CALL,0x40a108,0},
    {"call_41544c",ES2_COLLIDE_CALL,0x41544c,0},
    {"call_4215a8",ES2_COLLIDE_CALL,0x4215a8,0},
    {"call_46b188",ES2_COLLIDE_CALL,0x46b188,0},
    {"call_401e5c",ES2_COLLIDE_CALL,0x401e5c,0},
};
static callee_counter *render_root_hook_counters(unsigned hook,unsigned *count) {
    if(hook==ES2_HOOK_INDEX_render_root){*count=sizeof(render_root_counters)/sizeof(render_root_counters[0]);return render_root_counters;}
    if(hook==ES2_HOOK_INDEX_stage_dispatch){*count=sizeof(stage_dispatch_counters)/sizeof(stage_dispatch_counters[0]);return stage_dispatch_counters;}
    *count=0;return NULL;
}
/* 45fca8: push ebx; add esp,-10h; mov ebx,4D24BCh; push 5. Relocated 9
 * bytes (1,3,5). */
static const BYTE render_root_sig[]={0x53,0x83,0xc4,0xf0,0xbb,0xbc,0x24,0x4d,0x00,0x6a,0x05};
static const BYTE render_root_lens[]={1,3,5};
/* 401d94: push ebp; mov ebp,esp; push ebx; push esi; push edi; mov
 * ebx,4A804Ch. Relocated 5 bytes (1,2,1,1). */
static const BYTE render_root_dispatch_sig[]={0x55,0x8b,0xec,0x53,0x56,0x57,0xbb,0x4c,0x80,0x4a,0x00};
static const BYTE render_root_dispatch_lens[]={1,2,1,1};
/* 40707c: push ebp; mov ebp,esp; mov eax,[ebp+8]; mov edx,[ebp+0Ch].
 * Relocated 6 bytes (1,2,3). */
static const BYTE render_root_copy_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x8b,0x55,0x0c};
static const BYTE render_root_copy_lens[]={1,2,3};
#define RENDER_ROOT_EXT(name,unsupported) \
    static const es2_hook_extension render_root_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,render_root_##name,unsupported}; \
    static uint32_t __cdecl render_root_##name##32(const uint32_t *a,void *u) {return (uint32_t)render_root_##name(a,u,weapons_no_registers);}
RENDER_ROOT_EXT(render_root,TRUE)
RENDER_ROOT_EXT(stage_dispatch,TRUE)
RENDER_ROOT_EXT(vector_copy,FALSE)
#undef RENDER_ROOT_EXT
/* Compare: the destination D[0..11] as a span; EAX is D (the clone in the
 * replacement phase), EDX the source's +4 dword. */
static const es2_hook_span render_root_copy_spans[]={{0,0,12}};
static BOOL __cdecl render_root_copy_equal(const es2_hook_comparison *v,void *u) {
    (void)u;const uint32_t o=(uint32_t)(uintptr_t)v->original[0],r=(uint32_t)(uintptr_t)v->replacement[0];
    return !memcmp(v->original[0],v->replacement[0],12)&&v->original_result-o==v->replacement_result-r&&
        v->original_edx==v->replacement_edx;
}
static es2_hook_descriptor render_root_hook_descriptors[]={
    {ES2_HOOK_ID(render_root),render_root_sig,11,9,render_root_lens,3,0,render_root_render_root32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(stage_dispatch),render_root_dispatch_sig,11,5,render_root_dispatch_lens,4,1,render_root_stage_dispatch32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(vector_copy),render_root_copy_sig,9,6,render_root_copy_lens,3,2,render_root_vector_copy32,render_root_copy_equal,NULL,render_root_copy_spans,1},
};
/* The entry shim, at the entry ESP T. PUSH EBX is the original's first
 * instruction (the caller's EBX at T-4); RENDER_ROOT_PAGES guard-probed
 * pages below T-4, then the floor; the context {EBP, T-14h, ESI, EDI, ECX}
 * below the gap (or the stack floor); the framework's dispatch (no argument
 * slot) with the caller's registers; then ECX from the context (the core's
 * when the replacement ran) and EBX from T-4 (the original's ADD ESP,10h;
 * POP EBX; RET). */
static int render_root_entry(es2_machine *m,uint32_t bridge) {
    const uint32_t top=m->esp-4u;shim_w32(top,m->ebx);
    const uint32_t context=shim_gap(bridge,top,RENDER_ROOT_PAGES)-20u;
    const uint32_t value[5]={m->ebp,top-16u,m->esi,m->edi,m->ecx};
    for(unsigned i=0;i<5u;++i)shim_w32(context+4u*i,value[i]);
    render_root_pending=es2_guest_span(context,sizeof(render_root_stack));
    es2_machine c=*m;c.esp=context;
    (void)es2_machine_call(&c,bridge,ES2_MACHINE_CDECL,NULL,0);
    c.ecx=shim_r32(context+16u);c.ebx=shim_r32(top);render_root_pending=NULL;
    return shim_return(m,&c,m->esp,0);
}
ES2_SHIM(render_root_shim,0x5fca8u,render_root_entry,0);
#endif
