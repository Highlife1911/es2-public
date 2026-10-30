#ifndef ES2_BRIDGED_LEAVES_HOOKS_H
#define ES2_BRIDGED_LEAVES_HOOKS_H
#include "guest_native.h"
/* bridged_leaves group (docs/reverse-engineering/hud.md, section
 * "the bridged leaves"): the original leaves that own code still
 * reached through the guest bridge. weapon_target_word 40e484 (under
 * cockpit_sync), list_next 411e40 and list_prev 411e60 (command_key's
 * view-subject steps), r_key_toggle 41b468 and nav_marker_set 434974
 * (hud_key's R and Alt+D), throttle_handler 447e08 (slot 0 of the
 * ThrottleGauge table, under slider_base), view_pref_toggle 45fe38
 * (command_key's Tab/Enter) and the empty matrix constructors
 * matrix_ctor 495132, rotation_ctor 495194, rotation_base_ctor 49514f.
 * Core in core/src/bridged_leaves.c.
 *
 * EDX:EAX exact. ECX is the framework's (the entry ECX): 40e484 and
 * 49514f leave ECX unchanged; the others change it and every caller
 * writes or pops ECX before reading it (hud.md, "the bridged leaves",
 * "Callers' registers"). The game calls (r_key_toggle's sound row 462830, the two
 * cancels 435ac8 and the EventPort's slot 0; the constructors' 495194,
 * 4953c4 and 49514f) go through the weapons guest bridge with the
 * original's registers, so nested hooks keep their modes.
 *
 * r_key_toggle has an ENTRY SHIM (bridged_leaves_r_key_entry):
 * the EventPort receive 436a3c copies bytes 2..15h of the message record
 * at EBP-18h, of which 41b468 writes only bytes 0..5, so the rest is the
 * residue of the frame; the shim runs the replacement at the original
 * stack coordinate as panel_frame's does (EBP = T-4 for entry ESP T, the
 * caller's EBX/ESI where the prologue pushes them, EBP-1Ch/-20h, a
 * guard-probed gap, {EBP, outgoing ESP = EBP-20h, the caller's EDI} in
 * bridged_leaves_r_key_pending) and every game call runs through
 * ai_behaviour_stack_call with EBX = the EventPort, ESI = P+92h, the
 * caller's EDI and the original return addresses. Without the shim
 * (direct and legacy calls) the record lies in a zeroed scratch.
 *
 * Compare: r_key_toggle refuses it (sound, messages). The others accept
 * it: weapon_target_word declares the word W+7Dh (span of argument 0, EAX
 * compared relative to it); throttle_handler declares G+0..C1h (span of
 * argument 0: the gadget table, the count, the handle and the flag byte;
 * EDX compared relative to it); nav_marker_set the 14 bytes H+25Eh of
 * the cockpit [49B088h] (a one-hop region); view_pref_toggle the words
 * 4D2518h..4D2521h (an image region); the list steps and the
 * constructors write nothing. No game instruction is embedded; the
 * shim only marshals registers and moves ESP. */
#include "es2/core/bridged_leaves.h"
static uint64_t __cdecl bridged_leaves_weapon_target_word(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;uint32_t ecx;return es2_weapon_target_word_registers(&math_native_memory,a[0],a[1],r,&ecx);
}
static uint64_t __cdecl bridged_leaves_list_next(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;uint32_t ecx;return es2_list_next_registers(&math_native_memory,a[0],a[1],r,&ecx);
}
static uint64_t __cdecl bridged_leaves_list_prev(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;uint32_t ecx;return es2_list_prev_registers(&math_native_memory,a[0],a[1],r,&ecx);
}
/* r_key_toggle at the original stack coordinate (see above). */
typedef struct bridged_leaves_stack { uint32_t ebp,top,edi; } bridged_leaves_stack;
__attribute__((used)) static bridged_leaves_stack *bridged_leaves_r_key_pending;
typedef struct bridged_leaves_guest { const bridged_leaves_stack *stack; es2_r_key_frame frame; } bridged_leaves_guest;
static void bridged_leaves_stack_call(void *context,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    const bridged_leaves_guest *c=context;
    const uint32_t call[6]={c->frame.call.ebx,c->frame.call.esi,c->frame.call.edi,c->stack->ebp,c->stack->top,c->frame.call.ret};
    ai_behaviour_stack_call(f,a,n,r,call);
}
static uint64_t __cdecl bridged_leaves_r_key_toggle(const uint32_t *a,void *u,const uint32_t *r) {
    /* The shim's context belongs to this invocation only if the slot above
     * its EBP is this call's. */
    const bridged_leaves_stack *s=bridged_leaves_r_key_pending;bridged_leaves_r_key_pending=NULL;
    if(s&&math_native_read32(NULL,s->ebp+8u)!=a[0])s=NULL;
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_r_key_toggle);uint32_t ecx;
    if(s) {
        bridged_leaves_guest c={s,{{0,0,0,0},s->top}};
        g.call=bridged_leaves_stack_call;g.context=&c;
        return es2_r_key_toggle_registers(&math_native_memory,&g,&c.frame,s->ebp,s->edi,a[0],r,&ecx);
    }
    BYTE scratch[0x20]={0};
    return es2_r_key_toggle_registers(&math_native_memory,&g,NULL,(uint32_t)(uintptr_t)(scratch+0x18),0,a[0],r,&ecx);
}
static uint64_t __cdecl bridged_leaves_nav_marker_set(const uint32_t *a,void *u,const uint32_t *r) {
    (void)a;uint32_t ecx;return es2_nav_marker_set_registers(&math_native_memory,(uint32_t)(uintptr_t)u,r,&ecx);
}
static uint64_t __cdecl bridged_leaves_throttle_handler(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;uint32_t ecx;return es2_throttle_handler_registers(&math_native_memory,a[0],a[1],a[2],r,&ecx);
}
static uint64_t __cdecl bridged_leaves_view_pref_toggle(const uint32_t *a,void *u,const uint32_t *r) {
    (void)a;uint32_t ecx;return es2_view_pref_toggle_registers(&math_native_memory,(uint32_t)(uintptr_t)u,r,&ecx);
}
static uint64_t __cdecl bridged_leaves_matrix_ctor(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_matrix_ctor);uint32_t ecx;
    return es2_matrix_ctor_registers(&g,a[0],r,&ecx);
}
static uint64_t __cdecl bridged_leaves_rotation_ctor(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_rotation_ctor);uint32_t ecx;
    return es2_rotation_ctor_registers(&g,a[0],r,&ecx);
}
static uint64_t __cdecl bridged_leaves_rotation_base_ctor(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;uint32_t ecx;return es2_rotation_base_ctor_registers(a[0],r,&ecx);
}
/* Callee counters (hk_<hook>_<suffix>), incremented only while the hook is
 * non-original. The EventPort's slot 0 is keyed 20Bh (its offset in the
 * cockpit H), since slot key 0 means a direct call. */
static callee_counter r_key_toggle_counters[]={
    {"call_462830",ES2_COLLIDE_CALL,0x462830,0},
    {"call_435ac8",ES2_COLLIDE_CALL,0x435ac8,0},
    {"slot_20b",ES2_COLLIDE_SLOT,0x20b,0},
};
static callee_counter matrix_ctor_counters[]={
    {"call_495194",ES2_COLLIDE_CALL,0x495194,0},
    {"call_4953c4",ES2_COLLIDE_CALL,0x4953c4,0},
};
static callee_counter rotation_ctor_counters[]={{"call_49514f",ES2_COLLIDE_CALL,0x49514f,0}};
static callee_counter *bridged_leaves_hook_counters(unsigned hook,unsigned *count) {
#define BRIDGED_LEAVES_COUNTERS(name) if(hook==ES2_HOOK_INDEX_##name) { \
        *count=sizeof(name##_counters)/sizeof(name##_counters[0]);return name##_counters; }
    BRIDGED_LEAVES_COUNTERS(r_key_toggle)
    BRIDGED_LEAVES_COUNTERS(matrix_ctor)
    BRIDGED_LEAVES_COUNTERS(rotation_ctor)
#undef BRIDGED_LEAVES_COUNTERS
    *count=0;return NULL;
}
/* Compare: weapon_target_word's EAX is W, 7Dh below the declared word. */
static BOOL __cdecl bridged_leaves_weapon_word_equal(const es2_hook_comparison *v,void *u) {
    (void)u;
    return v->original_edx==v->replacement_edx&&!memcmp(v->original[0],v->replacement[0],2)&&
        v->original_result-(uint32_t)(uintptr_t)v->original[0]==v->replacement_result-(uint32_t)(uintptr_t)v->replacement[0];
}
/* throttle_handler's EDX points into the declared table (G+18h+4k). */
static BOOL __cdecl bridged_leaves_throttle_equal(const es2_hook_comparison *v,void *u) {
    (void)u;
    return v->original_result==v->replacement_result&&!memcmp(v->original[0],v->replacement[0],v->sizes[0])&&
        v->original_edx-(uint32_t)(uintptr_t)v->original[0]==v->replacement_edx-(uint32_t)(uintptr_t)v->replacement[0];
}
static const es2_hook_span bridged_leaves_weapon_word_span[]={{0,0x7d,2}};
static const es2_hook_span bridged_leaves_throttle_span[]={{0,0,0xc2}};
static const es2_hook_region bridged_leaves_nav_marker_regions[]={{ES2_REGION_RVA_INDIRECT,0,0x9b088,0,0x25e,0xe,FALSE,0,{0,0}}};
static const es2_hook_region bridged_leaves_view_regions[]={{ES2_REGION_RVA,0,0xd2518,0,0,0xa,FALSE,0,{0,0}}};
/* Verified entry bytes (reference DBSIM.EXE). */
/* 40e484: push ebp; mov ebp,esp; mov edx,[ebp+0Ch]; mov eax,[ebp+8]; test edx,edx; je */
static const BYTE weapon_target_word_sig[]={0x55,0x8b,0xec,0x8b,0x55,0x0c,0x8b,0x45,0x08,0x85,0xd2,0x74,0x12};
/* 411e40: push ebp; mov ebp,esp; push ebx; mov ecx,[ebp+8]; mov eax,[ebp+0Ch]; movsx eax,word [eax+4Bh]; movsx edx,word [ecx+6] */
static const BYTE list_next_sig[]={0x55,0x8b,0xec,0x53,0x8b,0x4d,0x08,0x8b,0x45,0x0c,0x0f,0xbf,0x40,0x4b,0x0f,0xbf,0x51,0x06};
/* 411e60: push ebp; mov ebp,esp; push ebx; mov ecx,[ebp+8]; mov eax,[ebp+0Ch]; movsx eax,word [eax+4Bh]; movsx ebx,word [ecx+6] */
static const BYTE list_prev_sig[]={0x55,0x8b,0xec,0x53,0x8b,0x4d,0x08,0x8b,0x45,0x0c,0x0f,0xbf,0x40,0x4b,0x0f,0xbf,0x59,0x06};
/* 41b468: push ebp; mov ebp,esp; add esp,-18h; push ebx; push esi; mov eax,[ebp+8]; lea edx,[eax+92h] */
static const BYTE r_key_toggle_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xe8,0x53,0x56,0x8b,0x45,0x08,0x8d,0x90,0x92,0x00,0x00,0x00};
/* 434974: push ebx; mov edx,[49B088h]; mov ecx,[edx+203h]; add ecx,26h */
static const BYTE nav_marker_set_sig[]={0x53,0x8b,0x15,0x88,0xb0,0x49,0x00,0x8b,0x8a,0x03,0x02,0x00,0x00,0x83,0xc1,0x26};
/* 447e08: push ebp; mov ebp,esp; push ebx; push esi; mov esi,[ebp+10h]; mov ebx,[ebp+0Ch]; mov ecx,[ebp+8] */
static const BYTE throttle_handler_sig[]={0x55,0x8b,0xec,0x53,0x56,0x8b,0x75,0x10,0x8b,0x5d,0x0c,0x8b,0x4d,0x08};
/* 45fe38: cmp word [4D2518h],0; sete al; and eax,1; mov [4D2518h],ax */
static const BYTE view_pref_toggle_sig[]={0x66,0x83,0x3d,0x18,0x25,0x4d,0x00,0x00,0x0f,0x94,0xc0,0x83,0xe0,0x01,
    0x66,0xa3,0x18,0x25,0x4d,0x00};
/* 495132: push ebp; mov ebp,esp; push ebx; mov ebx,[ebp+8]; push ebx; call 495194 */
static const BYTE matrix_ctor_sig[]={0x55,0x8b,0xec,0x53,0x8b,0x5d,0x08,0x53,0xe8,0x55,0x00,0x00,0x00};
/* 495194: push ebp; mov ebp,esp; push ebx; mov ebx,[ebp+8]; push ebx; call 49514f */
static const BYTE rotation_ctor_sig[]={0x55,0x8b,0xec,0x53,0x8b,0x5d,0x08,0x53,0xe8,0xae,0xff,0xff,0xff};
/* 49514f: push ebp; mov ebp,esp; mov eax,[ebp+8]; pop ebp; ret */
static const BYTE rotation_base_ctor_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x5d,0xc3};
/* Relocated prologues: PUSH EBP; MOV EBP,ESP; then a load or ADD ESP,imm8
 * (3), PUSH EBX + load (1+3), PUSH EBX; PUSH ESI (1+1); PUSH EBX + MOV
 * EDX,[moffs] (1+6); the 16-bit CMP [moffs],imm8 (8). */
static const BYTE bridged_leaves_load_lens[]={1,2,3},bridged_leaves_ebx_lens[]={1,2,1,3},bridged_leaves_saved_lens[]={1,2,1,1},
    bridged_leaves_abs_lens[]={1,6},bridged_leaves_cmp_lens[]={8};
#define BRIDGED_LEAVES_EXT(name,regions,count,unsupported) \
    static const es2_hook_extension bridged_leaves_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,regions,count,bridged_leaves_##name,unsupported}; \
    static uint32_t __cdecl bridged_leaves_##name##32(const uint32_t *a,void *u) {return (uint32_t)bridged_leaves_##name(a,u,weapons_no_registers);}
BRIDGED_LEAVES_EXT(weapon_target_word,NULL,0,FALSE)
BRIDGED_LEAVES_EXT(list_next,NULL,0,FALSE)
BRIDGED_LEAVES_EXT(list_prev,NULL,0,FALSE)
BRIDGED_LEAVES_EXT(r_key_toggle,NULL,0,TRUE)
BRIDGED_LEAVES_EXT(nav_marker_set,bridged_leaves_nav_marker_regions,1,FALSE)
BRIDGED_LEAVES_EXT(throttle_handler,NULL,0,FALSE)
BRIDGED_LEAVES_EXT(view_pref_toggle,bridged_leaves_view_regions,1,FALSE)
BRIDGED_LEAVES_EXT(matrix_ctor,NULL,0,FALSE)
BRIDGED_LEAVES_EXT(rotation_ctor,NULL,0,FALSE)
BRIDGED_LEAVES_EXT(rotation_base_ctor,NULL,0,FALSE)
#undef BRIDGED_LEAVES_EXT
static es2_hook_descriptor bridged_leaves_hook_descriptors[]={
    {ES2_HOOK_ID(weapon_target_word),weapon_target_word_sig,13,6,bridged_leaves_load_lens,3,2,bridged_leaves_weapon_target_word32,
        bridged_leaves_weapon_word_equal,NULL,bridged_leaves_weapon_word_span,1},
    {ES2_HOOK_ID(list_next),list_next_sig,18,7,bridged_leaves_ebx_lens,4,2,bridged_leaves_list_next32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(list_prev),list_prev_sig,18,7,bridged_leaves_ebx_lens,4,2,bridged_leaves_list_prev32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(r_key_toggle),r_key_toggle_sig,17,6,bridged_leaves_load_lens,3,1,bridged_leaves_r_key_toggle32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(nav_marker_set),nav_marker_set_sig,16,7,bridged_leaves_abs_lens,2,0,bridged_leaves_nav_marker_set32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(throttle_handler),throttle_handler_sig,14,5,bridged_leaves_saved_lens,4,3,bridged_leaves_throttle_handler32,
        bridged_leaves_throttle_equal,NULL,bridged_leaves_throttle_span,1},
    {ES2_HOOK_ID(view_pref_toggle),view_pref_toggle_sig,20,8,bridged_leaves_cmp_lens,1,0,bridged_leaves_view_pref_toggle32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(matrix_ctor),matrix_ctor_sig,13,7,bridged_leaves_ebx_lens,4,1,bridged_leaves_matrix_ctor32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(rotation_ctor),rotation_ctor_sig,13,7,bridged_leaves_ebx_lens,4,1,bridged_leaves_rotation_ctor32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(rotation_base_ctor),rotation_base_ctor_sig,8,6,bridged_leaves_load_lens,3,1,bridged_leaves_rotation_base_ctor32,NULL,NULL,NULL,0},
};
/* r_key_toggle's entry shim, as panel_frame's: PUSH EBP; the caller's
 * EBX/ESI to EBP-1Ch/-20h (where 41b468 pushes them below its 18h bytes of
 * locals, which the shim does not touch); EBX counts 48 guard-probed pages
 * (192 KiB: the sound row, the message cancels, the EventPort receive and
 * the nested hooks and their host frames run above the adapter's host
 * frames) and is reloaded; {EBP, EBP-20h, EDI} below the gap; the slot
 * copied for the framework's dispatch. EAX/EDX/ECX pass through (41b468
 * writes all three before its first call). */
static int bridged_leaves_r_key_entry(es2_machine *m,uint32_t bridge) {
    uint32_t ebp;const uint32_t context=shim_saved_gap_n(m,bridge,&ebp,-0x1c,2u,48u,12u);
    shim_w32(context+8u,m->edi);
    bridged_leaves_r_key_pending=es2_guest_span(context,sizeof(bridged_leaves_stack));
    const int ran=shim_gap_call(m,bridge,ebp,context,1);
    bridged_leaves_r_key_pending=NULL;return ran;
}
ES2_SHIM(bridged_leaves_r_key_shim,0x1b468u,bridged_leaves_r_key_entry,0);
#endif
