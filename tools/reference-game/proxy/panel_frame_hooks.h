#ifndef ES2_PANEL_FRAME_HOOKS_H
#define ES2_PANEL_FRAME_HOOKS_H
/* panel_frame group (docs/reverse-engineering/hud.md, section "cockpit
 * panel frame as own code"): the cockpit panel frame 4327ac
 * (panel_frame). Every game call goes through the callee observer
 * (weapons_graph) with the original's EAX/EDX/ECX, so the nested hook
 * cockpit_valid 433174 (and every hook reached from the draws) keeps its
 * own mode; the scroll zones 433770, the startup sequence 432924, the jolt
 * 43408c, the bump 434194, the context push/pop 4311e0/431210, the cursor
 * 433c54, the button releases 4530fc, the nav marker 4349ac and the panel
 * virtuals (+8 of the ten elements and six sub-objects, +14h of the port)
 * stay guest calls. EDX:EAX exact; ECX is popped by the only caller
 * (45fca8), so the framework returns the entry ECX (no exit shim).
 * The callees read stack residue (the EventPort message records of 432924
 * and 4349ac; the panel draw tree was not audited), so panel_frame has an
 * ENTRY SHIM (panel_frame_entry) that runs it at the
 * original stack coordinate: it keeps the original's EBP (T-4 for entry
 * ESP T), stores the caller's EBX/ESI/EDI where 4327ac's prologue pushes
 * them (EBP-4/-8/-0Ch), reserves a guard-probed gap and leaves {EBP,
 * outgoing ESP = EBP-0Ch} in panel_frame_pending; every game call then
 * runs through ai_behaviour_stack_call (ai_behaviour_hooks.h) with the
 * EBX/ESI/EDI and return address the core names, and the core re-reads
 * the argument slot for each POP ECX. Without the shim (direct and legacy
 * calls) the plain bridge is used. Refuses compare mode (game calls,
 * draws). No game instruction is embedded; the shim only marshals
 * registers and moves ESP. */
#include "es2/core/panel_frame.h"
__attribute__((used)) static ai_behaviour_stack *panel_frame_pending;
typedef struct panel_frame_guest { const ai_behaviour_stack *stack; es2_panel_frame frame; } panel_frame_guest;
static void panel_frame_stack_call(void *context,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    const panel_frame_guest *c=context;
    const uint32_t call[6]={c->frame.call.ebx,c->frame.call.esi,c->frame.call.edi,c->stack->ebp,c->stack->top,c->frame.call.ret};
    ai_behaviour_stack_call(f,a,n,r,call);
}
static uint64_t __cdecl panel_frame_panel_frame(const uint32_t *a,void *u,const uint32_t *r) {
    const ai_behaviour_stack *s=ai_behaviour2_take(&panel_frame_pending,a,1);
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_panel_frame);
    if(s) {
        panel_frame_guest c={s,{{0,0,0,0},s->top}};
        g.call=panel_frame_stack_call;g.context=&c;
        return es2_panel_frame_registers(&math_native_memory,&g,&c.frame,math_native_read32(NULL,s->ebp-8u),
            math_native_read32(NULL,s->ebp-0xcu),a[0],r);
    }
    return es2_panel_frame_registers(&math_native_memory,&g,NULL,0,0,a[0],r);
}
/* Callee counters (hk_<hook>_<suffix>), incremented only while the hook is
 * non-original. Keys: the callee VA for calls, the vtable offset for slots. */
static callee_counter panel_frame_counters[]={
    {"call_433770",ES2_COLLIDE_CALL,0x433770,0},
    {"call_432924",ES2_COLLIDE_CALL,0x432924,0},
    {"call_43408c",ES2_COLLIDE_CALL,0x43408c,0},
    {"call_434194",ES2_COLLIDE_CALL,0x434194,0},
    {"call_4311e0",ES2_COLLIDE_CALL,0x4311e0,0},
    {"call_431210",ES2_COLLIDE_CALL,0x431210,0},
    {"call_433c54",ES2_COLLIDE_CALL,0x433c54,0},
    {"call_433174",ES2_COLLIDE_CALL,0x433174,0},
    {"call_4530fc",ES2_COLLIDE_CALL,0x4530fc,0},
    {"call_4349ac",ES2_COLLIDE_CALL,0x4349ac,0},
    {"slot_8",ES2_COLLIDE_SLOT,0x8,0},
    {"slot_14",ES2_COLLIDE_SLOT,0x14,0},
};
static callee_counter *panel_frame_hook_counters(unsigned hook,unsigned *count) {
    if(hook==ES2_HOOK_INDEX_panel_frame){*count=sizeof(panel_frame_counters)/sizeof(panel_frame_counters[0]);return panel_frame_counters;}
    *count=0;return NULL;
}
/* 4327ac: push ebp; mov ebp,esp; push ebx; push esi; push edi; cmp byte
 * [49B104h],0; mov ebx,[ebp+8]. Relocated 5 bytes (1,2,1,1). */
static const BYTE panel_frame_sig[]={0x55,0x8b,0xec,0x53,0x56,0x57,0x80,0x3d,0x04,0xb1,0x49,0x00,0x00,0x8b,0x5d,0x08};
static const BYTE panel_frame_lens[]={1,2,1,1};
static const es2_hook_extension panel_frame_panel_frame_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,panel_frame_panel_frame,TRUE};
static uint32_t __cdecl panel_frame_panel_frame32(const uint32_t *a,void *u) {return (uint32_t)panel_frame_panel_frame(a,u,weapons_no_registers);}
static es2_hook_descriptor panel_frame_hook_descriptors[]={
    {ES2_HOOK_ID(panel_frame),panel_frame_sig,16,5,panel_frame_lens,4,1,panel_frame_panel_frame32,NULL,NULL,NULL,0},
};
/* The entry shim, as command_key's: PUSH EBP; the caller's EBX/ESI/EDI to
 * EBP-4/-8/-0Ch; EBX counts 48 guard-probed pages (192 KiB: the panel draw
 * tree, the nested hooks and their host frames run above the adapter's
 * host frames) and is reloaded; {EBP, EBP-0Ch} below the gap; the slot
 * copied for the framework's dispatch. EAX/EDX/ECX pass through (they
 * reach the first callee). ECX after 4327ac is popped by its caller. */
ES2_SHIM_GAPPED(panel_frame,0x327acu,panel_frame_pending,-4,48u,1);
#endif
