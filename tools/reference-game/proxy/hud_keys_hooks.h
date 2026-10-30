#ifndef ES2_HUD_KEYS_HOOKS_H
#define ES2_HUD_KEYS_HOOKS_H
#include "guest_native.h"
/* hud_keys group (docs/reverse-engineering/hud.md, section "HUD key handler
 * as own code"): the key handler 432bc8 (hud_key), the button
 * press 438d9c (button_press) and the button-release scheduler 4530bc
 * (button_release). Every game call goes through the weapons guest bridge and
 * callee observer (weapons_graph) with the original's entry EAX/EDX/ECX, so
 * the nested hooks (panel_weapon 432a50, cockpit_next 43349c, cockpit_select
 * 4332dc, and within the group button_press and button_release) keep their
 * own modes. Without hud_key's entry shim its exception frame 49630c gets
 * the synthetic EBP (ES2_HUD_KEYS_FRAME in a scratch) through
 * weapons_impact_guest_call; the adapter restores FS:0 to the record's saved
 * head (the core's *fs_head), as the original's epilogue does. The display
 * requests 42a3f4, the getter 429820, the F11 dialog 47749c/457538/4543b0,
 * the MFD forward 446c38, the R toggle 41b468, the Tab lock 40e4ac, the nav
 * marker 434974, the clock 467828 and the eleven panel gadget virtual slots
 * stay guest calls. EDX:EAX exact; ECX is dead at every caller
 * (45feb8/player_input read only AL of 432bc8; 438d9c/4530bc results are
 * overwritten or ignored): the framework returns the entry ECX.
 * hud_key has an ENTRY SHIM (hud_keys_hud_key_entry) that runs it at the original
 * stack coordinate (below). All three refuse compare mode (game calls,
 * allocations, the clock). No game instruction is embedded. */
#include "es2/core/hud_keys.h"
/* hud_key at the original stack coordinate (input.md, "stack residue under
 * the command handler").
 * The entry shim hud_keys_hud_key_entry (below) keeps the original's EBP
 * (T-4 for entry ESP T), stores the caller's EBX/ESI/EDI where 432bc8's
 * prologue saves them (EBP-34h/-38h/-3Ch), reserves a guard-probed gap and
 * leaves {EBP, outgoing ESP = EBP-3Ch} in hud_keys_hud_key_pending. The
 * core's frame is then the original one (f = EBP; 49630c, called with the
 * original's EBP, links the runtime record at EBP-28h) and every game call
 * runs through ai_behaviour_stack_call with the EBX/ESI/EDI and return
 * address the core names; where the original has pushed the request before
 * the getter 429820 the call runs 4 bytes lower over that slot. So the MFD
 * forward 446c38 and the group order 4231a4 below it read the stack the
 * original leaves (the live Alt+O difference). Without the shim (direct and
 * legacy calls) the synthetic EBP in the adapter's scratch is used. */
__attribute__((used)) static ai_behaviour_stack *hud_keys_hud_key_pending;
typedef struct hud_keys_guest { const ai_behaviour_stack *stack; es2_hud_key_frame frame; } hud_keys_guest;
static void hud_keys_stack_call(void *context,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    const hud_keys_guest *c=context;uint32_t top=c->stack->top;
    if(c->frame.pushed) { top-=4u;math_native_write32(NULL,top,c->frame.temp); }
    const uint32_t call[6]={c->frame.call.ebx,c->frame.call.esi,c->frame.call.edi,c->stack->ebp,top,c->frame.call.ret};
    ai_behaviour_stack_call(f,a,n,r,call);
}
static uint64_t __cdecl hud_keys_hud_key(const uint32_t *a,void *u,const uint32_t *r) {
    const ai_behaviour_stack *s=ai_behaviour2_take(&hud_keys_hud_key_pending,a,2);
    BYTE scratch[ES2_HUD_KEYS_SCRATCH]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_hud_key);
    uint32_t head;uint64_t result;
    if(s) {
        hud_keys_guest c={s,{{0,0,0,0},0,0}};
        g.scratch=s->ebp-ES2_HUD_KEYS_FRAME;g.call=hud_keys_stack_call;g.context=&c;
        result=es2_hud_key_frame_registers(&math_native_memory,&g,&c.frame,math_native_read32(NULL,s->ebp-0x38u),a[0],a[1],r,&head);
    } else {
        weapons_impact_context c={(uint32_t)(uintptr_t)u,(uint32_t)(uintptr_t)(scratch+ES2_HUD_KEYS_FRAME)};
        g.call=weapons_impact_guest_call;g.context=&c;
        result=es2_hud_key_registers(&math_native_memory,&g,a[0],a[1],r,&head);
    }
    es2_machine_set_fs0(head);return result;
}
/* hud_key's entry shim, as torso_aim's: PUSH EBP; the caller's EBX/ESI/EDI
 * to EBP-34h/-38h/-3Ch; EBX counts 32 guard-probed pages (128 KiB: the MFD
 * and panel gadgets, the button helpers, the target hooks and the F11
 * dialog run above the adapter's host frames; a nested hook in compare mode
 * reserves 64 KiB) and is reloaded; {EBP, EBP-3Ch} below the gap; the two
 * slots copied for the framework's dispatch. EAX/EDX/ECX pass through
 * (432bc8 writes EAX first; its EDX/ECX reach 49630c, which ignores them).
 * ECX after 432bc8 is dead at every caller. */
static int hud_keys_hud_key_entry(es2_machine *m,uint32_t bridge) {
    uint32_t ebp;const uint32_t context=shim_saved_gap(m,bridge,&ebp,-0x34,32u);
    hud_keys_hud_key_pending=es2_guest_span(context,sizeof(ai_behaviour_stack));
    const int ran=shim_gap_call(m,bridge,ebp,context,2);
    hud_keys_hud_key_pending=NULL;return ran;
}
ES2_SHIM(hud_keys_hud_key_shim,0x32bc8u,hud_keys_hud_key_entry,0);
static uint64_t __cdecl hud_keys_button_press(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_button_press);
    return es2_button_press_registers(&math_native_memory,&g,a[0],a[1],a[2],r);
}
static uint64_t __cdecl hud_keys_button_release(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_button_release);
    return es2_button_release_registers(&math_native_memory,&g,a[0],a[1],r);
}
/* Callee counters (hk_<hook>_<suffix>), incremented only while the hook is
 * non-original. Keys: the callee VA for calls, the vtable offset for slots. */
static callee_counter hud_key_counters[]={
    {"call_49630c",ES2_COLLIDE_CALL,0x49630c,0},
    {"call_432a50",ES2_COLLIDE_CALL,0x432a50,0},
    {"call_438d9c",ES2_COLLIDE_CALL,0x438d9c,0},
    {"call_42a3f4",ES2_COLLIDE_CALL,0x42a3f4,0},
    {"call_43349c",ES2_COLLIDE_CALL,0x43349c,0},
    {"call_4332dc",ES2_COLLIDE_CALL,0x4332dc,0},
    {"call_40e4ac",ES2_COLLIDE_CALL,0x40e4ac,0},
    {"call_41b468",ES2_COLLIDE_CALL,0x41b468,0},
    {"call_446c38",ES2_COLLIDE_CALL,0x446c38,0},
    {"call_434974",ES2_COLLIDE_CALL,0x434974,0},
    {"call_47749c",ES2_COLLIDE_CALL,0x47749c,0},
    {"call_457538",ES2_COLLIDE_CALL,0x457538,0},
    {"call_4543b0",ES2_COLLIDE_CALL,0x4543b0,0},
    {"slot_8",ES2_COLLIDE_SLOT,0x8,0},
    {"slot_c",ES2_COLLIDE_SLOT,0xc,0},
};
static callee_counter button_press_counters[]={
    {"call_4530bc",ES2_COLLIDE_CALL,0x4530bc,0},
    {"slot_8",ES2_COLLIDE_SLOT,0x8,0},
};
static callee_counter button_release_counters[]={
    {"call_467828",ES2_COLLIDE_CALL,0x467828,0},
};
static callee_counter *hud_keys_hook_counters(unsigned hook,unsigned *count) {
    if(hook==ES2_HOOK_INDEX_hud_key){*count=sizeof(hud_key_counters)/sizeof(hud_key_counters[0]);return hud_key_counters;}
    if(hook==ES2_HOOK_INDEX_button_press){*count=sizeof(button_press_counters)/sizeof(button_press_counters[0]);return button_press_counters;}
    if(hook==ES2_HOOK_INDEX_button_release){*count=sizeof(button_release_counters)/sizeof(button_release_counters[0]);return button_release_counters;}
    *count=0;return NULL;
}
/* 432bc8: push ebp; mov ebp,esp; add esp,-30h; push ebx; push esi; push edi;
 * mov edi,[ebp+0Ch]; mov ebx,[ebp+8]. Relocated 6 bytes (1,2,3). */
static const BYTE hud_key_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xd0,0x53,0x56,0x57,0x8b,0x7d,0x0c,0x8b,0x5d,0x08};
static const BYTE hud_key_lens[]={1,2,3};
/* 438d9c: push ebp; mov ebp,esp; push ebx; push esi; mov esi,[ebp+0Ch];
 * mov ebx,[ebp+8]. Relocated 5 bytes (1,2,1,1). */
static const BYTE button_press_sig[]={0x55,0x8b,0xec,0x53,0x56,0x8b,0x75,0x0c,0x8b,0x5d,0x08};
static const BYTE button_press_lens[]={1,2,1,1};
/* 4530bc: push ebp; mov ebp,esp; push ebx; mov eax,[ebp+8]; cmp word
 * [eax+396h],7. Relocated 7 bytes (1,2,1,3). */
static const BYTE button_release_sig[]={0x55,0x8b,0xec,0x53,0x8b,0x45,0x08,0x66,0x83,0xb8,0x96,0x03,0x00,0x00,0x07};
static const BYTE button_release_lens[]={1,2,1,3};
#define HUD_KEYS_EXT(name,lensn) \
    static const es2_hook_extension hud_keys_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,hud_keys_##name,TRUE}; \
    static uint32_t __cdecl hud_keys_##name##32(const uint32_t *a,void *u) {return (uint32_t)hud_keys_##name(a,u,weapons_no_registers);}
HUD_KEYS_EXT(hud_key,3)
HUD_KEYS_EXT(button_press,4)
HUD_KEYS_EXT(button_release,4)
#undef HUD_KEYS_EXT
static es2_hook_descriptor hud_keys_hook_descriptors[]={
    {ES2_HOOK_ID(hud_key),hud_key_sig,15,6,hud_key_lens,3,2,hud_keys_hud_key32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(button_press),button_press_sig,11,5,button_press_lens,4,3,hud_keys_button_press32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(button_release),button_release_sig,15,7,button_release_lens,4,2,hud_keys_button_release32,NULL,NULL,NULL,0},
};
#endif
