#ifndef ES2_MFD_KEYS_HOOKS_H
#define ES2_MFD_KEYS_HOOKS_H
#include "guest_native.h"
/* mfd_keys group (docs/reverse-engineering/cockpit.md, section
 * "MFD keys"): the MFDGauge's key method 4469e8 (mfd_key, slot
 * +0Ch of 49cfa0), hud_key's Alt order keys 446c38 (mfd_alt_key), the key
 * selection 447158 (mfd_key_select), the menu entry select 43f9e0
 * (menu_select) and the entry steps 44703c/447070 (mfd_entry_next,
 * mfd_entry_prev). Core in core/src/mfd_keys.c. Every game call goes
 * through the weapons guest bridge and callee observer (weapons_graph)
 * with the original's registers, so the nested hooks (panel_visible
 * 431410, button_press 438d9c, mfd_range 446ff0, mfd_order 447248,
 * menu_code 43f9c0, mfd_entry 4470a4 and this group's own hooks) keep
 * their modes; no callee stays a guest call. EDX:EAX exact; ECX is the
 * framework's (the entry ECX) except menu_select's (k, set by its shim):
 * every caller pops or overwrites it before reading it (cockpit.md,
 * "MFD keys", "Registers").
 *
 * ENTRY SHIMS, for the reason of mfd_orders_hooks.h:
 * 446c38 calls the order 447248 on the Alt+O path, whose group order reads
 * the uninitialized priority word [EBP-1Ch]; every function on that path
 * and every function whose frames lie where the next Alt order's frames
 * will lie runs without touching the game stack near the original frames:
 * - frame shims (mfd_key, mfd_alt_key: push ebx; push esi, outgoing
 *   EBP-8; mfd_key_select, mfd_entry_next, mfd_entry_prev: push ebx,
 *   outgoing EBP-4), built like mfd_orders' (the caller's EBX/ESI in the
 *   original's save slots, guard-probed pages, the floor, the context
 *   {EBP, outgoing ESP, EBX, ESI, EDI} below the gap, game calls through
 *   ai_behaviour_stack_call at the original's outgoing ESP);
 * - a gap shim for the leaf menu_select (PUSH EBP, 4 probed pages,
 *   the floor): the original writes its saved EBP there; after the
 *   dispatch the shim sets ECX to the slot k, the original's ECX.
 * Gap sizes decrease along every nesting: hud_key 32 > mfd_key and
 * mfd_alt_key 31 > mfd_key_select, mfd_entry_next, mfd_entry_prev 29 >
 * the leaves 4; mfd_key reaches mfd_button 28 through button_press's host
 * frames (3 pages left for them), mfd_range 26 directly, and mfd_alt_key
 * mfd_order 26.
 * Without the shims (direct and legacy calls) the plain bridge is used.
 *
 * Compare: menu_select (span menu+32h, 7 bytes; EAX = the argument,
 * compared relative to the span base) accepts it; the others refuse it
 * (game calls). No game instruction is embedded: the shim only
 * marshals registers (menu_select's ECX from its slot) and moves ESP. */
#include "es2/core/mfd_keys.h"
/* A frame shim (mfd_orders' MFD_ORDERS_FRAME_SHIM): the context type and
 * the stack call are mfd_orders' (mfd_orders_hooks.h). */
#define MFD_KEYS_FRAME_SHIM(name,rva,pages,top,saved,count) \
    __attribute__((used)) static mfd_orders_stack *mfd_keys_##name##_pending; \
    static int mfd_keys_##name##_entry(es2_machine *m,uint32_t bridge) { \
        uint32_t ebp;const uint32_t context=shim_record_gap(m,bridge,&ebp,-4,saved,pages,top); \
        mfd_keys_##name##_pending=es2_guest_span(context,sizeof(mfd_orders_stack)); \
        const int ran=shim_gap_call(m,bridge,ebp,context,count); \
        mfd_keys_##name##_pending=NULL;return ran; } \
    ES2_SHIM(mfd_keys_##name##_shim,rva,mfd_keys_##name##_entry,0);
/* 4469e8, 446c38: push ebx; push esi (EBP-4/-8), outgoing EBP-8. */
MFD_KEYS_FRAME_SHIM(mfd_key,0x469e8u,31u,0x8u,2u,2)
MFD_KEYS_FRAME_SHIM(mfd_alt_key,0x46c38u,31u,0x8u,2u,2)
/* 447158, 44703c, 447070: push ebx (EBP-4), outgoing EBP-4. */
MFD_KEYS_FRAME_SHIM(mfd_key_select,0x47158u,29u,0x4u,1u,3)
MFD_KEYS_FRAME_SHIM(mfd_entry_next,0x4703cu,29u,0x4u,1u,1)
MFD_KEYS_FRAME_SHIM(mfd_entry_prev,0x47070u,29u,0x4u,1u,1)
#undef MFD_KEYS_FRAME_SHIM
/* 43f9e0 leaves ECX = k (its second slot, which it does not write); the
 * framework returns EDX:EAX only, and 446c38 calls mfd_order 447248 right
 * after it without touching ECX, so the shim (mfd_orders' gap shim)
 * reloads ECX from the slot in every mode (the original's own value in
 * original and compare mode). */
static int mfd_keys_menu_select_entry(es2_machine *m,uint32_t bridge) {
    const uint32_t slot=m->esp+8u;const int ran=mfd_orders_gap_call(m,bridge,2);
    m->ecx=shim_r32(slot);return ran;
}
ES2_SHIM(mfd_keys_menu_select_shim,0x3f9e0u,mfd_keys_menu_select_entry,0);
/* The frame adapters: with the shim's context the stack call, else the
 * plain bridge. */
#define MFD_KEYS_GUEST(name,n) \
    const mfd_orders_stack *s=mfd_orders_take(&mfd_keys_##name##_pending,a,n); \
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_##name); \
    mfd_orders_guest c={s,{{0,0,0,0},s?s->top:0}}; \
    if(s){g.call=mfd_orders_stack_call;g.context=&c;}
static uint64_t __cdecl mfd_keys_mfd_key(const uint32_t *a,void *u,const uint32_t *r) {
    MFD_KEYS_GUEST(mfd_key,2)
    return es2_mfd_key_registers(&math_native_memory,&g,s?&c.frame:NULL,s?s->edi:0,a[0],a[1],r);
}
static uint64_t __cdecl mfd_keys_mfd_alt_key(const uint32_t *a,void *u,const uint32_t *r) {
    MFD_KEYS_GUEST(mfd_alt_key,2)
    return es2_mfd_alt_key_registers(&math_native_memory,&g,s?&c.frame:NULL,s?s->edi:0,a[0],a[1],r);
}
static uint64_t __cdecl mfd_keys_mfd_key_select(const uint32_t *a,void *u,const uint32_t *r) {
    MFD_KEYS_GUEST(mfd_key_select,3)
    return es2_mfd_key_select_registers(&math_native_memory,&g,s?&c.frame:NULL,s?s->esi:0,s?s->edi:0,a[0],a[1],a[2],r);
}
static uint64_t __cdecl mfd_keys_mfd_entry_next(const uint32_t *a,void *u,const uint32_t *r) {
    MFD_KEYS_GUEST(mfd_entry_next,1)
    return es2_mfd_entry_next_registers(&math_native_memory,&g,s?&c.frame:NULL,s?s->esi:0,s?s->edi:0,a[0],r);
}
static uint64_t __cdecl mfd_keys_mfd_entry_prev(const uint32_t *a,void *u,const uint32_t *r) {
    MFD_KEYS_GUEST(mfd_entry_prev,1)
    return es2_mfd_entry_prev_registers(&math_native_memory,&g,s?&c.frame:NULL,s?s->esi:0,s?s->edi:0,a[0],r);
}
#undef MFD_KEYS_GUEST
static uint64_t __cdecl mfd_keys_menu_select(const uint32_t *a,void *u,const uint32_t *r) {
    (void)u;uint32_t ecx;return es2_menu_select_registers(&math_native_memory,a[0],a[1],r,&ecx);
}
/* Callee counters (hk_<hook>_<suffix>), incremented only while the hook is
 * non-original. Keys: the callee VA. */
static callee_counter mfd_key_counters[]={
    {"call_431410",ES2_COLLIDE_CALL,0x431410,0},
    {"call_438d9c",ES2_COLLIDE_CALL,0x438d9c,0},
    {"call_446ff0",ES2_COLLIDE_CALL,0x446ff0,0},
    {"call_44703c",ES2_COLLIDE_CALL,0x44703c,0},
    {"call_447070",ES2_COLLIDE_CALL,0x447070,0},
    {"call_447158",ES2_COLLIDE_CALL,0x447158,0},
};
static callee_counter mfd_alt_key_counters[]={
    {"call_43f9e0",ES2_COLLIDE_CALL,0x43f9e0,0},
    {"call_43f9c0",ES2_COLLIDE_CALL,0x43f9c0,0},
    {"call_447248",ES2_COLLIDE_CALL,0x447248,0},
    {"call_447158",ES2_COLLIDE_CALL,0x447158,0},
};
static callee_counter mfd_key_select_counters[]={{"call_4470a4",ES2_COLLIDE_CALL,0x4470a4,0}};
static callee_counter mfd_entry_next_counters[]={{"call_4470a4",ES2_COLLIDE_CALL,0x4470a4,0}};
static callee_counter mfd_entry_prev_counters[]={{"call_4470a4",ES2_COLLIDE_CALL,0x4470a4,0}};
static callee_counter *mfd_keys_hook_counters(unsigned hook,unsigned *count) {
#define MFD_KEYS_COUNTERS(name) if(hook==ES2_HOOK_INDEX_##name) { \
        *count=sizeof(name##_counters)/sizeof(name##_counters[0]);return name##_counters; }
    MFD_KEYS_COUNTERS(mfd_key)
    MFD_KEYS_COUNTERS(mfd_alt_key)
    MFD_KEYS_COUNTERS(mfd_key_select)
    MFD_KEYS_COUNTERS(mfd_entry_next)
    MFD_KEYS_COUNTERS(mfd_entry_prev)
#undef MFD_KEYS_COUNTERS
    *count=0;return NULL;
}
static const es2_hook_span mfd_keys_select_span[]={{0,0x32,7}};
/* Verified entry bytes (reference DBSIM.EXE). */
/* 4469e8: push ebp; mov ebp,esp; push ebx; push esi; mov esi,[ebp+0Ch]; mov ebx,[ebp+8] */
static const BYTE mfd_keys_mfd_key_sig[]={0x55,0x8b,0xec,0x53,0x56,0x8b,0x75,0x0c,0x8b,0x5d,0x08};
/* 446c38: push ebp; mov ebp,esp; push ebx; push esi; movzx eax,word [ebp+0Ch]; mov ebx,[ebp+8] */
static const BYTE mfd_keys_mfd_alt_key_sig[]={0x55,0x8b,0xec,0x53,0x56,0x0f,0xb7,0x45,0x0c,0x8b,0x5d,0x08};
/* 447158: push ebp; mov ebp,esp; push ebx; mov ebx,[ebp+8]; mov eax,[ebp+0Ch]; cmp eax,[ebx+0FBh] */
static const BYTE mfd_keys_mfd_key_select_sig[]={0x55,0x8b,0xec,0x53,0x8b,0x5d,0x08,0x8b,0x45,0x0c,0x3b,0x83,0xfb,0x00,0x00,0x00};
/* 43f9e0: push ebp; mov ebp,esp; mov eax,[ebp+8]; mov dx,[eax+32h]; mov [eax+37h],dx */
static const BYTE mfd_keys_menu_select_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x66,0x8b,0x50,0x32,0x66,0x89,0x50,0x37};
/* 44703c and 447070: push ebp; mov ebp,esp; push ebx; mov eax,[ebp+8]; mov edx,[eax+0D1h] */
static const BYTE mfd_keys_mfd_entry_next_sig[]={0x55,0x8b,0xec,0x53,0x8b,0x45,0x08,0x8b,0x90,0xd1,0x00,0x00,0x00};
static const BYTE mfd_keys_mfd_entry_prev_sig[]={0x55,0x8b,0xec,0x53,0x8b,0x45,0x08,0x8b,0x90,0xd1,0x00,0x00,0x00};
/* Relocated prologues: PUSH EBP; MOV EBP,ESP; then PUSH EBX; PUSH ESI
 * (1+1), a load (3), PUSH EBX + load (1+3). */
static const BYTE mfd_keys_push_lens[]={1,2,1,1},mfd_keys_load_lens[]={1,2,3},mfd_keys_ebx_lens[]={1,2,1,3};
/* Compare: EAX (the argument) relative to the span base, EDX exact. */
static BOOL __cdecl mfd_keys_span_equal(const es2_hook_comparison *v,void *u) {
    (void)u;
    return v->original_edx==v->replacement_edx&&!memcmp(v->original[0],v->replacement[0],v->sizes[0])&&
        v->original_result-(uint32_t)(uintptr_t)v->original[0]==v->replacement_result-(uint32_t)(uintptr_t)v->replacement[0];
}
#define MFD_KEYS_EXT(name,unsupported) \
    static const es2_hook_extension mfd_keys_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,mfd_keys_##name,unsupported}; \
    static uint32_t __cdecl mfd_keys_##name##32(const uint32_t *a,void *u) {return (uint32_t)mfd_keys_##name(a,u,weapons_no_registers);}
MFD_KEYS_EXT(mfd_key,TRUE)
MFD_KEYS_EXT(mfd_alt_key,TRUE)
MFD_KEYS_EXT(mfd_key_select,TRUE)
MFD_KEYS_EXT(menu_select,FALSE)
MFD_KEYS_EXT(mfd_entry_next,TRUE)
MFD_KEYS_EXT(mfd_entry_prev,TRUE)
#undef MFD_KEYS_EXT
static es2_hook_descriptor mfd_keys_hook_descriptors[]={
    {ES2_HOOK_ID(mfd_key),mfd_keys_mfd_key_sig,11,5,mfd_keys_push_lens,4,2,mfd_keys_mfd_key32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(mfd_alt_key),mfd_keys_mfd_alt_key_sig,12,5,mfd_keys_push_lens,4,2,mfd_keys_mfd_alt_key32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(mfd_key_select),mfd_keys_mfd_key_select_sig,16,7,mfd_keys_ebx_lens,4,3,mfd_keys_mfd_key_select32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(menu_select),mfd_keys_menu_select_sig,14,6,mfd_keys_load_lens,3,2,mfd_keys_menu_select32,
        mfd_keys_span_equal,NULL,mfd_keys_select_span,1},
    {ES2_HOOK_ID(mfd_entry_next),mfd_keys_mfd_entry_next_sig,13,7,mfd_keys_ebx_lens,4,1,mfd_keys_mfd_entry_next32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(mfd_entry_prev),mfd_keys_mfd_entry_prev_sig,13,7,mfd_keys_ebx_lens,4,1,mfd_keys_mfd_entry_prev32,NULL,NULL,NULL,0},
};
#endif
