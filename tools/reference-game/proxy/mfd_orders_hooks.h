#ifndef ES2_MFD_ORDERS_HOOKS_H
#define ES2_MFD_ORDERS_HOOKS_H
#include "guest_native.h"
/* mfd_orders group (docs/reverse-engineering/cockpit.md, section
 * "MFD order path"): the MFDGauge's button owner 446844
 * (mfd_button), the entry click 4470c0 (mfd_click), the order 447248
 * (mfd_order), the order call 43166c (order_call), the group order 4231a4
 * (group_order), the order code 43f9c0 (menu_code) and its toggle 43f9f8
 * (menu_toggle), the entry select 4470a4 (mfd_entry), the target step
 * 446fb0 (mfd_target), the status-view and range cycles 446fc4/446ff0
 * (mfd_view_next, mfd_range) and the mission-load set-up of the order group
 * 431530 (order_setup). Core in core/src/mfd_orders.c. Every game call goes
 * through the weapons guest bridge and callee observer (weapons_graph) with
 * the original's registers, so the nested hooks (button_press 438d9c,
 * cockpit_next 43349c, dist_gate 49288c, pool_prev 471c70, the members'
 * order methods and this group's own hooks) keep their modes; the page
 * switch 446e60, the radio buttons' slot +4, the range set 447014, memset
 * 49681a, the HDD panels 44a7e8 and the MFD list 4472bc stay guest calls.
 * EDX:EAX exact; ECX is the framework's (the entry ECX): every caller pops
 * or overwrites it (cockpit.md, "MFD order path", "ECX after the twelve").
 *
 * ENTRY SHIMS. The group order reads its priority word
 * [EBP-1Ch] uninitialized for the codes 3, 6, 9..0Eh and above 0Fh
 * (input.md, "The residue reader"); Alt+O (code 3) is given or refused by
 * what the stack holds there. So every hook on the path to it, and every
 * hook called from there, runs without touching the game stack near the
 * original frames:
 * - frame shims (mfd_button, mfd_click, mfd_order, order_call, group_order,
 *   mfd_target, mfd_range): PUSH EBP gives the original's EBP; the caller's
 *   EBX/ESI/EDI go to the original's save slots (where its prologue has
 *   them); EAX counts the guard-probed pages (below) and is restored;
 *   the floor; {EBP, outgoing ESP, EBX, ESI, EDI} below the gap for
 *   the adapter, whose locals are then the original's frame (group_order's
 *   priority word is the residue the original reads) and whose game calls
 *   run through ai_behaviour_stack_call at the original's outgoing ESP
 *   with the EBX/ESI/EDI, EBP and return address the core names;
 * - gap shims (menu_code, menu_toggle, mfd_entry, mfd_view_next: pure
 *   leaves that write nothing on the stack but the saved EBP): PUSH EBP, 4
 *   probed pages, the floor, then the framework's dispatch, whose
 *   frames would otherwise overwrite the bytes the group order reads later
 *   (447248 calls 43f9c0 just before the order and 43f9f8 after it).
 * Gap sizes decrease along every nesting, so that an inner shim's probe
 * ends in the unused part of the outer gap: a store below
 * the probe's end before ESP moves to the floor would overwrite one of the
 * outer's live host frames. mfd_button and
 * mfd_click 28 pages (reached under hud_key's 32 through button_press's
 * host frames, or under player_input's 64), mfd_order, mfd_target and
 * mfd_range 26 (under those, or under hud_key through 446c38), order_call
 * 24, group_order 22 (88 KiB at 4231a4's coordinate for the members' order
 * methods and a nested hook in compare mode, whose buffers take 64 KiB),
 * the leaves 4.
 * Without the shims (direct and legacy calls) the plain bridge is used and
 * group_order's frame is a zeroed scratch (priority residue 0). order_setup
 * (load time, no uninitialized read) has none.
 *
 * Compare: menu_code (pure reader), menu_toggle (span menu+2Ch, 0Ch bytes),
 * mfd_entry (span G+B1h, 4) and mfd_view_next (span G+318h, 8) accept it,
 * EAX compared relative to the span base where it is the argument; the
 * others refuse it (game calls). No game instruction is embedded: the
 * shim only marshals registers and moves ESP. */
#include "es2/core/mfd_orders.h"
/* A frame shim's context: {EBP, outgoing ESP, the caller's EBX/ESI/EDI}. */
typedef struct mfd_orders_stack { uint32_t ebp,top,ebx,esi,edi; } mfd_orders_stack;
typedef struct mfd_orders_guest { const mfd_orders_stack *stack; es2_mfd_frame frame; } mfd_orders_guest;
static void mfd_orders_stack_call(void *context,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    const mfd_orders_guest *c=context;
    const uint32_t call[6]={c->frame.call.ebx,c->frame.call.esi,c->frame.call.edi,c->stack->ebp,c->stack->top,c->frame.call.ret};
    ai_behaviour_stack_call(f,a,n,r,call);
}
/* The shim's context belongs to this invocation only if the slots above its
 * EBP are this call's. */
static const mfd_orders_stack *mfd_orders_take(mfd_orders_stack **pending,const uint32_t *a,unsigned n) {
    const mfd_orders_stack *s=*pending;*pending=NULL;
    for(unsigned i=0;s&&i<n;++i)if(math_native_read32(NULL,s->ebp+8u+4u*i)!=a[i])s=NULL;
    return s;
}
/* A frame shim: `pages` probed pages, the caller's EBX(/ESI/EDI; `saved`
 * of them) in the original's save slots from EBP+save down, `top` the
 * outgoing ESP below EBP, `count` argument slots copied for the framework's
 * dispatch (shim_record_gap). */
#define MFD_ORDERS_FRAME_SHIM(name,rva,pages,top,save,saved,count) \
    __attribute__((used)) static mfd_orders_stack *mfd_orders_##name##_pending; \
    static int mfd_orders_##name##_entry(es2_machine *m,uint32_t bridge) { \
        uint32_t ebp;const uint32_t context=shim_record_gap(m,bridge,&ebp,save,saved,pages,top); \
        mfd_orders_##name##_pending=es2_guest_span(context,sizeof(mfd_orders_stack)); \
        const int ran=shim_gap_call(m,bridge,ebp,context,count); \
        mfd_orders_##name##_pending=NULL;return ran; } \
    ES2_SHIM(mfd_orders_##name##_shim,rva,mfd_orders_##name##_entry,0);
/* A gap shim for a pure leaf: a frame, 4 guard-probed pages, the slots
 * copied below the floor. */
static int mfd_orders_gap_call(es2_machine *m,uint32_t bridge,unsigned count) {
    const uint32_t ebp=shim_frame(m),esp=shim_gap(bridge,ebp,4u);
    uint32_t args[3];
    for(unsigned i=0;i<count;++i)args[i]=shim_r32(ebp+8u+4u*i);
    return shim_call(m,bridge,ES2_MACHINE_CDECL,ebp,esp,m->ecx,args,count);
}
#define MFD_ORDERS_GAP_SHIM(name,rva,count) \
    static int mfd_orders_##name##_entry(es2_machine *m,uint32_t bridge) {return mfd_orders_gap_call(m,bridge,count);} \
    ES2_SHIM(mfd_orders_##name##_shim,rva,mfd_orders_##name##_entry,0);
/* 446844: push ebx; push esi (EBP-4/-8), outgoing EBP-8. */
MFD_ORDERS_FRAME_SHIM(mfd_button,0x46844u,28u,0x8u,-4,2u,2)
/* 4470c0, 447248: push ebx; push esi; push edi (EBP-4/-8/-0Ch). */
MFD_ORDERS_FRAME_SHIM(mfd_click,0x470c0u,28u,0xcu,-4,3u,3)
MFD_ORDERS_FRAME_SHIM(mfd_order,0x47248u,26u,0xcu,-4,3u,1)
/* 43166c, 446fb0: no saves, outgoing ESP = EBP. */
MFD_ORDERS_FRAME_SHIM(order_call,0x3166cu,24u,0x0u,0,0u,2)
MFD_ORDERS_FRAME_SHIM(mfd_target,0x46fb0u,26u,0x0u,0,0u,1)
/* 4231a4: 20h bytes of locals, then push ebx; push esi; push edi
 * (EBP-24h/-28h/-2Ch); the locals (the priority word at EBP-1Ch) are not
 * touched. */
MFD_ORDERS_FRAME_SHIM(group_order,0x231a4u,22u,0x2cu,-0x24,3u,2)
/* 446ff0: push ebx (EBP-4). */
MFD_ORDERS_FRAME_SHIM(mfd_range,0x46ff0u,26u,0x4u,-4,1u,1)
MFD_ORDERS_GAP_SHIM(menu_code,0x3f9c0u,1)
MFD_ORDERS_GAP_SHIM(menu_toggle,0x3f9f8u,1)
MFD_ORDERS_GAP_SHIM(mfd_entry,0x470a4u,2)
MFD_ORDERS_GAP_SHIM(mfd_view_next,0x46fc4u,1)
#undef MFD_ORDERS_FRAME_SHIM
#undef MFD_ORDERS_GAP_SHIM
/* The frame adapters: with the shim's context the stack call, else the
 * plain bridge. */
#define MFD_ORDERS_GUEST(name,n) \
    const mfd_orders_stack *s=mfd_orders_take(&mfd_orders_##name##_pending,a,n); \
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_##name); \
    mfd_orders_guest c={s,{{0,0,0,0},s?s->top:0}}; \
    if(s){g.call=mfd_orders_stack_call;g.context=&c;}
static uint64_t __cdecl mfd_orders_mfd_button(const uint32_t *a,void *u,const uint32_t *r) {
    MFD_ORDERS_GUEST(mfd_button,2)
    return es2_mfd_button_registers(&math_native_memory,&g,s?&c.frame:NULL,s?s->esi:0,s?s->edi:0,a[0],a[1],r);
}
static uint64_t __cdecl mfd_orders_mfd_click(const uint32_t *a,void *u,const uint32_t *r) {
    MFD_ORDERS_GUEST(mfd_click,3)
    return es2_mfd_click_registers(&math_native_memory,&g,s?&c.frame:NULL,a[0],a[1],a[2],r);
}
static uint64_t __cdecl mfd_orders_mfd_order(const uint32_t *a,void *u,const uint32_t *r) {
    MFD_ORDERS_GUEST(mfd_order,1)
    return es2_mfd_order_registers(&math_native_memory,&g,s?&c.frame:NULL,a[0],r);
}
static uint64_t __cdecl mfd_orders_order_call(const uint32_t *a,void *u,const uint32_t *r) {
    MFD_ORDERS_GUEST(order_call,2)
    const uint32_t caller[3]={s?s->ebx:0,s?s->esi:0,s?s->edi:0};
    return es2_order_call_registers(&math_native_memory,&g,s?&c.frame:NULL,caller,a[0],a[1],r);
}
/* Without the shim: a zeroed frame of 20h bytes of locals, then the saved
 * EBP, the return address and the two argument slots the core rereads. */
static uint64_t __cdecl mfd_orders_group_order(const uint32_t *a,void *u,const uint32_t *r) {
    MFD_ORDERS_GUEST(group_order,2)
    if(s) {
        const uint32_t caller[2]={s->ebx,s->esi};
        return es2_group_order_registers(&math_native_memory,&g,&c.frame,s->ebp,caller,a[0],a[1],r);
    }
    BYTE scratch[0x30]={0};const uint32_t f=(uint32_t)(uintptr_t)(scratch+0x20);
    math_native_write32(NULL,f+8u,a[0]);math_native_write32(NULL,f+0xcu,a[1]);
    const uint32_t caller[2]={0,0};
    return es2_group_order_registers(&math_native_memory,&g,NULL,f,caller,a[0],a[1],r);
}
static uint64_t __cdecl mfd_orders_mfd_target(const uint32_t *a,void *u,const uint32_t *r) {
    MFD_ORDERS_GUEST(mfd_target,1)
    const uint32_t caller[3]={s?s->ebx:0,s?s->esi:0,s?s->edi:0};
    return es2_mfd_target_registers(&math_native_memory,&g,s?&c.frame:NULL,caller,a[0],r);
}
static uint64_t __cdecl mfd_orders_mfd_range(const uint32_t *a,void *u,const uint32_t *r) {
    MFD_ORDERS_GUEST(mfd_range,1)
    const uint32_t caller[2]={s?s->esi:0,s?s->edi:0};
    return es2_mfd_range_registers(&math_native_memory,&g,s?&c.frame:NULL,caller,a[0],r);
}
#undef MFD_ORDERS_GUEST
static uint64_t __cdecl mfd_orders_order_setup(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_order_setup);
    return es2_order_setup_registers(&math_native_memory,&g,a[0],r);
}
static uint64_t __cdecl mfd_orders_menu_code(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;(void)r;uint32_t ecx;return es2_menu_code_registers(&math_native_memory,a[0],&ecx);
}
static uint64_t __cdecl mfd_orders_menu_toggle(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;uint32_t ecx;return es2_menu_toggle_registers(&math_native_memory,a[0],r,&ecx);
}
static uint64_t __cdecl mfd_orders_mfd_entry(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;uint32_t ecx;return es2_mfd_entry_registers(&math_native_memory,a[0],a[1],r,&ecx);
}
static uint64_t __cdecl mfd_orders_mfd_view_next(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;uint32_t ecx;return es2_mfd_view_next_registers(&math_native_memory,a[0],r,&ecx);
}
/* Callee counters (hk_<hook>_<suffix>), incremented only while the hook is
 * non-original. Keys: the callee VA for calls; for slots the offset of the
 * button in G (radio pair 44h/48h) or the order slot 28h. */
static callee_counter mfd_button_counters[]={
    {"call_446e60",ES2_COLLIDE_CALL,0x446e60,0},
    {"call_446ff0",ES2_COLLIDE_CALL,0x446ff0,0},
    {"call_446fc4",ES2_COLLIDE_CALL,0x446fc4,0},
    {"call_446fb0",ES2_COLLIDE_CALL,0x446fb0,0},
    {"call_447248",ES2_COLLIDE_CALL,0x447248,0},
    {"slot_44",ES2_COLLIDE_SLOT,0x44,0},
    {"slot_48",ES2_COLLIDE_SLOT,0x48,0},
};
static callee_counter mfd_click_counters[]={
    {"call_438d9c",ES2_COLLIDE_CALL,0x438d9c,0},
    {"call_447248",ES2_COLLIDE_CALL,0x447248,0},
    {"call_4470a4",ES2_COLLIDE_CALL,0x4470a4,0},
};
static callee_counter mfd_order_counters[]={
    {"call_43f9c0",ES2_COLLIDE_CALL,0x43f9c0,0},
    {"call_43166c",ES2_COLLIDE_CALL,0x43166c,0},
    {"call_43f9f8",ES2_COLLIDE_CALL,0x43f9f8,0},
};
static callee_counter order_call_counters[]={{"call_4231a4",ES2_COLLIDE_CALL,0x4231a4,0}};
static callee_counter group_order_counters[]={
    {"call_49681a",ES2_COLLIDE_CALL,0x49681a,0},
    {"call_49288c",ES2_COLLIDE_CALL,0x49288c,0},
    {"slot_28",ES2_COLLIDE_SLOT,0x28,0},
};
static callee_counter mfd_target_counters[]={{"call_43349c",ES2_COLLIDE_CALL,0x43349c,0}};
static callee_counter mfd_range_counters[]={{"call_447014",ES2_COLLIDE_CALL,0x447014,0}};
static callee_counter order_setup_counters[]={
    {"call_471c70",ES2_COLLIDE_CALL,0x471c70,0},
    {"call_44a7e8",ES2_COLLIDE_CALL,0x44a7e8,0},
    {"call_4472bc",ES2_COLLIDE_CALL,0x4472bc,0},
};
static callee_counter *mfd_orders_hook_counters(unsigned hook,unsigned *count) {
#define MFD_ORDERS_COUNTERS(name) if(hook==ES2_HOOK_INDEX_##name) { \
        *count=sizeof(name##_counters)/sizeof(name##_counters[0]);return name##_counters; }
    MFD_ORDERS_COUNTERS(mfd_button)
    MFD_ORDERS_COUNTERS(mfd_click)
    MFD_ORDERS_COUNTERS(mfd_order)
    MFD_ORDERS_COUNTERS(order_call)
    MFD_ORDERS_COUNTERS(group_order)
    MFD_ORDERS_COUNTERS(mfd_target)
    MFD_ORDERS_COUNTERS(mfd_range)
    MFD_ORDERS_COUNTERS(order_setup)
#undef MFD_ORDERS_COUNTERS
    *count=0;return NULL;
}
/* Compare: EAX (the argument) relative to the span base, EDX exact. */
static BOOL __cdecl mfd_orders_span_equal(const es2_hook_comparison *v,void *u) {
    (void)u;
    return v->original_edx==v->replacement_edx&&!memcmp(v->original[0],v->replacement[0],v->sizes[0])&&
        v->original_result-(uint32_t)(uintptr_t)v->original[0]==v->replacement_result-(uint32_t)(uintptr_t)v->replacement[0];
}
/* menu_code writes nothing: EDX:EAX exact. */
static BOOL __cdecl mfd_orders_pair_equal(const es2_hook_comparison *v,void *u) {
    (void)u;return v->original_result==v->replacement_result&&v->original_edx==v->replacement_edx;
}
static const es2_hook_span mfd_orders_toggle_span[]={{0,0x2c,0xc}};
static const es2_hook_span mfd_orders_entry_span[]={{0,0xb1,4}};
static const es2_hook_span mfd_orders_view_span[]={{0,0x318,8}};
/* Verified entry bytes (reference DBSIM.EXE). */
/* 446844: push ebp; mov ebp,esp; push ebx; push esi; mov ecx,[ebp+0Ch]; mov ebx,[ebp+8]; xor eax,eax */
static const BYTE mfd_orders_mfd_button_sig[]={0x55,0x8b,0xec,0x53,0x56,0x8b,0x4d,0x0c,0x8b,0x5d,0x08,0x33,0xc0};
/* 4470c0: push ebp; mov ebp,esp; push ebx; push esi; push edi; mov edx,[ebp+10h]; mov ebx,[ebp+8] */
static const BYTE mfd_orders_mfd_click_sig[]={0x55,0x8b,0xec,0x53,0x56,0x57,0x8b,0x55,0x10,0x8b,0x5d,0x08};
/* 447248: push ebp; mov ebp,esp; push ebx; push esi; push edi; mov ebx,[ebp+8]; mov esi,4D03D4h */
static const BYTE mfd_orders_mfd_order_sig[]={0x55,0x8b,0xec,0x53,0x56,0x57,0x8b,0x5d,0x08,0xbe,0xd4,0x03,0x4d,0x00};
/* 43166c: push ebp; mov ebp,esp; mov edx,[ebp+0Ch]; mov eax,[ebp+8]; mov eax,[eax+203h] */
static const BYTE mfd_orders_order_call_sig[]={0x55,0x8b,0xec,0x8b,0x55,0x0c,0x8b,0x45,0x08,0x8b,0x80,0x03,0x02,0x00,0x00};
/* 4231a4: push ebp; mov ebp,esp; add esp,-20h; push ebx; push esi; push edi; mov edi,[ebp+0Ch] */
static const BYTE mfd_orders_group_order_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xe0,0x53,0x56,0x57,0x8b,0x7d,0x0c};
/* 43f9c0: push ebp; mov ebp,esp; mov eax,[ebp+8]; mov edx,[eax+32h]; test byte [eax+edx+2Ch],2 */
static const BYTE mfd_orders_menu_code_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x8b,0x50,0x32,0xf6,0x44,0x10,0x2c,0x02};
/* 43f9f8: push ebp; mov ebp,esp; mov eax,[ebp+8]; mov edx,[eax+32h]; add edx,-4; sub edx,2 */
static const BYTE mfd_orders_menu_toggle_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x8b,0x50,0x32,0x83,0xc2,0xfc,0x83,0xea,0x02};
/* 4470a4: push ebp; mov ebp,esp; mov edx,[ebp+0Ch]; mov eax,[ebp+8]; cmp edx,[eax+0B1h] */
static const BYTE mfd_orders_mfd_entry_sig[]={0x55,0x8b,0xec,0x8b,0x55,0x0c,0x8b,0x45,0x08,0x3b,0x90,0xb1,0x00,0x00,0x00};
/* 446fb0: push ebp; mov ebp,esp; mov eax,[49B088h]; push eax; call 43349c */
static const BYTE mfd_orders_mfd_target_sig[]={0x55,0x8b,0xec,0xa1,0x88,0xb0,0x49,0x00,0x50,0xe8,0xde,0xc4,0xfe,0xff};
/* 446fc4: push ebp; mov ebp,esp; mov eax,[ebp+8]; inc dword [eax+318h] */
static const BYTE mfd_orders_mfd_view_next_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0xff,0x80,0x18,0x03,0x00,0x00};
/* 446ff0: push ebp; mov ebp,esp; push ebx; mov eax,[ebp+8]; mov ebx,[eax+0C5h] */
static const BYTE mfd_orders_mfd_range_sig[]={0x55,0x8b,0xec,0x53,0x8b,0x45,0x08,0x8b,0x98,0xc5,0x00,0x00,0x00};
/* 431530: push ebp; mov ebp,esp; add esp,-10h; xor eax,eax; push ebx; push esi; push edi */
static const BYTE mfd_orders_order_setup_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xf0,0x33,0xc0,0x53,0x56,0x57};
/* Relocated prologues: PUSH EBP; MOV EBP,ESP; then PUSH EBX; PUSH ESI
 * (1+1), a load or ADD ESP,imm8 (3), MOV EAX,[moffs32] (5), PUSH EBX +
 * load (1+3). */
static const BYTE mfd_orders_push_lens[]={1,2,1,1},mfd_orders_load_lens[]={1,2,3},mfd_orders_abs_lens[]={1,2,5},
    mfd_orders_ebx_lens[]={1,2,1,3};
#define MFD_ORDERS_EXT(name,regions,count,unsupported) \
    static const es2_hook_extension mfd_orders_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,regions,count,mfd_orders_##name,unsupported}; \
    static uint32_t __cdecl mfd_orders_##name##32(const uint32_t *a,void *u) {return (uint32_t)mfd_orders_##name(a,u,weapons_no_registers);}
MFD_ORDERS_EXT(mfd_button,NULL,0,TRUE)
MFD_ORDERS_EXT(mfd_click,NULL,0,TRUE)
MFD_ORDERS_EXT(mfd_order,NULL,0,TRUE)
MFD_ORDERS_EXT(order_call,NULL,0,TRUE)
MFD_ORDERS_EXT(group_order,NULL,0,TRUE)
MFD_ORDERS_EXT(menu_code,NULL,0,FALSE)
MFD_ORDERS_EXT(menu_toggle,NULL,0,FALSE)
MFD_ORDERS_EXT(mfd_entry,NULL,0,FALSE)
MFD_ORDERS_EXT(mfd_target,NULL,0,TRUE)
MFD_ORDERS_EXT(mfd_view_next,NULL,0,FALSE)
MFD_ORDERS_EXT(mfd_range,NULL,0,TRUE)
MFD_ORDERS_EXT(order_setup,NULL,0,TRUE)
#undef MFD_ORDERS_EXT
static es2_hook_descriptor mfd_orders_hook_descriptors[]={
    {ES2_HOOK_ID(mfd_button),mfd_orders_mfd_button_sig,13,5,mfd_orders_push_lens,4,2,mfd_orders_mfd_button32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(mfd_click),mfd_orders_mfd_click_sig,12,5,mfd_orders_push_lens,4,3,mfd_orders_mfd_click32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(mfd_order),mfd_orders_mfd_order_sig,14,5,mfd_orders_push_lens,4,1,mfd_orders_mfd_order32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(order_call),mfd_orders_order_call_sig,15,6,mfd_orders_load_lens,3,2,mfd_orders_order_call32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(group_order),mfd_orders_group_order_sig,12,6,mfd_orders_load_lens,3,2,mfd_orders_group_order32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(menu_code),mfd_orders_menu_code_sig,14,6,mfd_orders_load_lens,3,1,mfd_orders_menu_code32,
        mfd_orders_pair_equal,NULL,NULL,0},
    {ES2_HOOK_ID(menu_toggle),mfd_orders_menu_toggle_sig,15,6,mfd_orders_load_lens,3,1,mfd_orders_menu_toggle32,
        mfd_orders_span_equal,NULL,mfd_orders_toggle_span,1},
    {ES2_HOOK_ID(mfd_entry),mfd_orders_mfd_entry_sig,15,6,mfd_orders_load_lens,3,2,mfd_orders_mfd_entry32,
        mfd_orders_span_equal,NULL,mfd_orders_entry_span,1},
    {ES2_HOOK_ID(mfd_target),mfd_orders_mfd_target_sig,14,8,mfd_orders_abs_lens,3,1,mfd_orders_mfd_target32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(mfd_view_next),mfd_orders_mfd_view_next_sig,12,6,mfd_orders_load_lens,3,1,mfd_orders_mfd_view_next32,
        mfd_orders_span_equal,NULL,mfd_orders_view_span,1},
    {ES2_HOOK_ID(mfd_range),mfd_orders_mfd_range_sig,13,7,mfd_orders_ebx_lens,4,1,mfd_orders_mfd_range32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(order_setup),mfd_orders_order_setup_sig,11,6,mfd_orders_load_lens,3,1,mfd_orders_order_setup32,NULL,NULL,NULL,0},
};
#endif
