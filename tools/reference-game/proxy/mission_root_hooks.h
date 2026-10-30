#ifndef ES2_MISSION_ROOT_HOOKS_H
#define ES2_MISSION_ROOT_HOOKS_H
#include "guest_native.h"
/* mission_root group (docs/reverse-engineering/mission_load.md,
 * section "the mission-load root"): the mission-load root 461608
 * (mission_root), called once per process by the game main at 45f42b.
 *
 * mission_root calls every game function through the callee observer
 * (weapons_graph) with the original's EAX/EDX/ECX, EBX/ESI/EDI (EDI the
 * view block 4D24BCh, ESI f-8 and later the walks' cursors, EBX the
 * caller's and later the walks' objects), EBP and return address, so the
 * nested hooks (stage_dispatch, world_load, actor_load, removal_append,
 * the stage flushes, pool_prev, terrain_height, order_setup, cockpit_sync,
 * camera_update, dist_gate, render_root and every hook below the guest
 * calls) keep their own modes; the input, window, graphics, palette,
 * sound, HUD, cockpit, camera, presentation and start-sequence callees stay
 * guest calls (graphics, platform, audio, input; function_verdicts.md). Refuses
 * compare mode (game calls, allocation, file reads, the whole load).
 *
 * Arming (mission_load.md, "the mission-load root", "Arming"): the root is
 * installed with the other hooks. With the early arming (the default) that
 * happens in
 * the step at its only call site 45f42b, before its first instruction, and
 * the step's jump to 461608 then enters this hook; the arming point itself
 * does not move (the first MCI call inside the load, 46313c, finds the
 * proxy armed already, as before). Without early arming the proxy arms at
 * that MCI call inside the running original, the detour is written while
 * 461608 executes and its only call is never intercepted (0 calls), as for
 * every other load-only hook.
 *
 * The guest callees read stack residue (the sound init's five unwritten
 * argument words, the loaders and draw trees below the load were not
 * audited), so mission_root has an ENTRY SHIM (mission_root_entry) that
 * runs it at the original stack
 * coordinate: it keeps the original's EBP (T-4 for entry ESP T), stores the
 * caller's EBX/ESI/EDI where 461608's prologue pushes them (EBP-154h/-158h/
 * -15Ch), reserves a guard-probed gap of MISSION_ROOT_PAGES pages and
 * leaves {EBP, outgoing ESP = EBP-15Ch, ECX} below it in
 * mission_root_pending; the core's locals are the original's (EBP-150h ..
 * EBP-1), every game call (49630c included, so the runtime record lies at
 * the original EBP-30h and FS:0 links it as the original does) runs through
 * ai_behaviour_stack_call (ai_behaviour_hooks.h) with the registers and
 * return address the core names, and the core re-reads the argument slot
 * for each POP ECX. The adapter writes the core's ECX (the record's saved
 * head) into the context, which the shim loads after the framework's
 * dispatch, and sets FS:0 to it as the original's epilogue does: EAX, EDX
 * and ECX return exactly, EBX/ESI/EDI are the caller's. Without the shim
 * (direct and legacy calls) the adapter keeps the locals in a buffer and
 * calls through logic_frame_machine_call (logic_frame_hooks.h; EBX/ESI/EDI
 * as named, EBP the buffer's synthetic EBP). No game instruction is
 * embedded; the shim only marshals registers and moves ESP.
 *
 * Both call paths announce each game call (present_site.h: callee,
 * entry 461608, the core's return address), so the frame capture records
 * the present 464a14 the core calls at 461e1c with the original's site
 * 461e21 instead of the bridge's return address in the DLL. */
#include "es2/core/mission_root.h"
#include "present_site.h"
/* 448 KiB, as the logic frame: the load's game code below the original
 * coordinate (the loaders, the sound and MCI start, the presentation) and
 * the host frames of the nested hooks without a shim (world_load,
 * actor_load and their nested hooks); the nested shims (render_root,
 * cockpit_sync, panel_frame) put their host frames at the shared floor
 * (the floor, weapons_hooks.h). */
#define MISSION_ROOT_PAGES 112
typedef struct mission_root_stack { uint32_t ebp,top,ecx; } mission_root_stack;
__attribute__((used)) static mission_root_stack *mission_root_pending;
typedef struct mission_root_guest { const mission_root_stack *stack; es2_mission_root frame; uint32_t ebp; } mission_root_guest;
static void mission_root_stack_call(void *context,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    const mission_root_guest *c=context;
    const uint32_t call[6]={c->frame.call.ebx,c->frame.call.esi,c->frame.call.edi,c->stack->ebp,c->stack->top,c->frame.call.ret};
    const es2_present_site_call site=es2_present_site_begin(f,0x00461608u,c->frame.call.ret);
    ai_behaviour_stack_call(f,a,n,r,call);
    es2_present_site_end(site);
}
static void mission_root_plain_call(void *context,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    const mission_root_guest *c=context;
    const uint32_t call[4]={c->frame.call.ebx,c->frame.call.esi,c->frame.call.edi,c->ebp};
    const es2_present_site_call site=es2_present_site_begin(f,0x00461608u,c->frame.call.ret);
    logic_frame_machine_call(f,a,n,r,call);
    es2_present_site_end(site);
}
static uint64_t __cdecl mission_root_mission_root(const uint32_t *a,void *u,const uint32_t *r) {
    (void)a;
    /* The shim publishes its context right before the framework's dispatch
     * and nothing can enter 461608 in between (its only caller is the game
     * main); without the shim the context is NULL. */
    mission_root_stack *s=mission_root_pending;mission_root_pending=NULL;
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_mission_root);
    BYTE frame[ES2_MISSION_ROOT_SCRATCH]={0};
    mission_root_guest c={s,{{0,0,0,0},0},(uint32_t)(uintptr_t)(frame+ES2_MISSION_ROOT_LOCALS)};
    uint32_t ecx=0,ebx=0;
    if(s) {
        c.frame.top=s->top;g.scratch=s->ebp-ES2_MISSION_ROOT_LOCALS;g.call=mission_root_stack_call;g.context=&c;
        ebx=math_native_read32(NULL,s->ebp-0x154u);
    } else { g.scratch=(uint32_t)(uintptr_t)frame;g.call=mission_root_plain_call;g.context=&c; }
    const uint64_t v=es2_mission_root_registers(&math_native_memory,&g,&c.frame,ebx,r,&ecx);
    if(s)s->ecx=ecx;
    es2_machine_set_fs0(ecx);
    return v;
}
/* Callee counters (hk_mission_root_<suffix>), incremented only while the
 * hook is non-original: the callee VA for direct calls, the slot key
 * 4A5824h for the graphics-library call through that pointer. */
#define MISSION_ROOT_CALL(va) {"call_" #va,ES2_COLLIDE_CALL,0x##va,0}
static callee_counter mission_root_counters[]={
    MISSION_ROOT_CALL(49630c),MISSION_ROOT_CALL(479cfc),MISSION_ROOT_CALL(45a5dc),MISSION_ROOT_CALL(4955c4),
    MISSION_ROOT_CALL(49694c),MISSION_ROOT_CALL(496970),MISSION_ROOT_CALL(461294),MISSION_ROOT_CALL(464c78),
    MISSION_ROOT_CALL(46548c),MISSION_ROOT_CALL(4614c0),MISSION_ROOT_CALL(4967ea),MISSION_ROOT_CALL(492bec),
    MISSION_ROOT_CALL(465f7c),MISSION_ROOT_CALL(481fe0),MISSION_ROOT_CALL(461450),MISSION_ROOT_CALL(47749c),
    MISSION_ROOT_CALL(480af0),MISSION_ROOT_CALL(462410),MISSION_ROOT_CALL(463370),MISSION_ROOT_CALL(48dd08),
    MISSION_ROOT_CALL(4913dc),MISSION_ROOT_CALL(401d94),MISSION_ROOT_CALL(46134c),MISSION_ROOT_CALL(467840),
    MISSION_ROOT_CALL(424308),MISSION_ROOT_CALL(4282d8),MISSION_ROOT_CALL(452ae4),MISSION_ROOT_CALL(4253d8),
    MISSION_ROOT_CALL(44f6f4),MISSION_ROOT_CALL(471c70),MISSION_ROOT_CALL(478628),MISSION_ROOT_CALL(407b3c),
    MISSION_ROOT_CALL(4077e8),MISSION_ROOT_CALL(409904),MISSION_ROOT_CALL(4083e0),MISSION_ROOT_CALL(46e180),
    MISSION_ROOT_CALL(48bd84),MISSION_ROOT_CALL(45cdf4),MISSION_ROOT_CALL(465618),MISSION_ROOT_CALL(4544c0),
    MISSION_ROOT_CALL(48bda4),MISSION_ROOT_CALL(45edb0),MISSION_ROOT_CALL(429660),MISSION_ROOT_CALL(431530),
    MISSION_ROOT_CALL(437c8c),MISSION_ROOT_CALL(41b130),MISSION_ROOT_CALL(4011a0),MISSION_ROOT_CALL(42a1f0),
    MISSION_ROOT_CALL(49288c),MISSION_ROOT_CALL(46313c),MISSION_ROOT_CALL(42eb40),MISSION_ROOT_CALL(43048c),
    MISSION_ROOT_CALL(45fca8),MISSION_ROOT_CALL(45d548),MISSION_ROOT_CALL(464a14),MISSION_ROOT_CALL(43122c),
    MISSION_ROOT_CALL(4328cc),MISSION_ROOT_CALL(433f7c),{"slot_4a5824",ES2_COLLIDE_SLOT,0x4a5824,0},
    MISSION_ROOT_CALL(45d490),MISSION_ROOT_CALL(45d50c),MISSION_ROOT_CALL(45d85c),MISSION_ROOT_CALL(45b758),
    MISSION_ROOT_CALL(477abc),
};
#undef MISSION_ROOT_CALL
static callee_counter *mission_root_hook_counters(unsigned hook,unsigned *count) {
    if(hook==ES2_HOOK_INDEX_mission_root){*count=sizeof(mission_root_counters)/sizeof(mission_root_counters[0]);return mission_root_counters;}
    *count=0;return NULL;
}
/* 461608: push ebp; mov ebp,esp; add esp,-150h (imm32); mov eax,49F7C9h;
 * push ebx; push esi; push edi. Relocated 9 bytes (1,2,6). */
static const BYTE mission_root_sig[]={0x55,0x8b,0xec,0x81,0xc4,0xb0,0xfe,0xff,0xff,0xb8,0xc9,0xf7,0x49,0x00,0x53,0x56,0x57};
static const BYTE mission_root_lens[]={1,2,6};
static const es2_hook_extension mission_root_mission_root_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,
    mission_root_mission_root,TRUE};
static uint32_t __cdecl mission_root_mission_root32(const uint32_t *a,void *u) {
    return (uint32_t)mission_root_mission_root(a,u,weapons_no_registers);
}
static es2_hook_descriptor mission_root_hook_descriptors[]={
    {ES2_HOOK_ID(mission_root),mission_root_sig,17,9,mission_root_lens,3,0,mission_root_mission_root32,NULL,NULL,NULL,0},
};
/* The entry shim, as logic_frame's: PUSH EBP; the caller's EBX/ESI/EDI to
 * EBP-154h/-158h/-15Ch; EBX counts MISSION_ROOT_PAGES guard-probed pages
 * (OR 0 keeps their bytes); the floor (es2_machine_floor); the
 * context {EBP, EBP-15Ch, the entry ECX} below the gap (or the floor); EBX
 * reloaded from its slot; the framework's dispatch (no argument slot); then
 * ECX from the context (the core's when the replacement ran), MOV ESP,EBP;
 * POP EBP; RET (the original's epilogue pops EDI/ESI/EBX from the same
 * slots, which hold the caller's values the dispatch preserved). EAX/EDX/
 * ECX pass through to the dispatch (they reach 49630c and 479cfc). */
static int mission_root_entry(es2_machine *m,uint32_t bridge) {
    uint32_t ebp;const uint32_t context=shim_saved_gap_n(m,bridge,&ebp,-0x154,3u,MISSION_ROOT_PAGES,12u);
    shim_w32(context+8u,m->ecx);
    mission_root_pending=es2_guest_span(context,sizeof(mission_root_stack));
    es2_machine c=*m;c.ebp=ebp;c.esp=context;
    (void)es2_machine_call(&c,bridge,ES2_MACHINE_CDECL,NULL,0);
    c.ecx=shim_r32(context+8u);mission_root_pending=NULL;
    return shim_return(m,&c,ebp+4u,ebp);
}
ES2_SHIM(mission_root_shim,0x61608u,mission_root_entry,0);
#endif
