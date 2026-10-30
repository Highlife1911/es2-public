#ifndef ES2_INPUT_DISPATCH_HOOKS_H
#define ES2_INPUT_DISPATCH_HOOKS_H
#include "guest_native.h"
/* input_dispatch group (docs/reverse-engineering/input.md, section "torso
 * helpers and the input dispatcher"): torso_center 41e8d4, torso_aim 41ef14 and player_input 460870.
 * Every game callee goes through the weapons guest bridge and callee
 * observer (weapons_graph), so all three refuse compare mode. torso_aim's
 * ACh-byte and player_input's 8Ch-byte frames are the adapter's scratch.
 * player_input's bridge also loads the EBX, ESI and EDI the original holds
 * at each call (410f78 and the key method +2Ch return the caller's ESI
 * high word). The command handler, the HUD and platform helpers, the
 * camera control and the virtuals stay original code. No game instruction
 * is embedded; the shim only marshals the ABI.
 * torso_aim has an entry shim (input_dispatch_torso_aim_entry): its
 * frame and game calls are the original's at the original stack
 * coordinate, so every caller (aim_fire 41f6f3/41f780, player_input
 * 460c8f) and the weapon bodies they reach later find the bytes the
 * original torso_aim leaves below their ESP (ai.md, "residue shims for
 * torso_aim and weapon_choose").
 * player_input has the same kind of entry shim
 * (input_dispatch_player_input_entry, installed from the same place), so the
 * command handler and the group order below it read the original's stack
 * (input.md, "stack residue under the command handler"). */
#include "es2/core/input_dispatch.h"
/* Like weapons_machine_call, plus EBX, ESI and EDI from the core's block. */
static void __cdecl input_dispatch_machine_call(uint32_t target,const uint32_t *args,unsigned count,
    es2_weapon_registers *registers,const es2_input_saved *saved) {
    const uint32_t frame[4]={saved->ebx,saved->esi,saved->edi,0};
    weapons_machine_call_at(target,args,count,registers,frame,0,NULL,0,NULL);
}
static void input_dispatch_guest_call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    input_dispatch_machine_call(f,a,n,r,(const es2_input_saved *)c);
}
static uint64_t __cdecl input_dispatch_torso_center(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_torso_center);
    return es2_torso_center_registers(&math_native_memory,&g,a[0],a[1],r);
}
/* torso_aim at the original stack coordinate. The entry shim
 * input_dispatch_torso_aim_entry (below) keeps the original's EBP (T-4 for
 * entry ESP T), stores the caller's EBX/ESI/EDI where 41ef14's prologue
 * saves them (EBP-0B0h/-0B4h/-0B8h), reserves a guard-probed gap and
 * leaves {EBP, outgoing ESP = EBP-0B8h} in input_dispatch_torso_aim_pending.
 * The adapter's frame is then the original one (scratch at EBP-0B0h, the
 * core's synthetic EBP = EBP) and every game call goes through
 * ai_behaviour_stack_call (ai_behaviour_hooks.h, the stack-residue bridge,
 * defined after this header): ESP = the outgoing coordinate, the EBX/ESI/
 * EDI the core names, the original's EBP, and the original's return address
 * left in the slot below the arguments. Without the shim (direct and legacy
 * calls) the adapter keeps the plain bridge and its own frame. */
typedef struct input_dispatch_stack { uint32_t ebp,top; } input_dispatch_stack;
typedef struct input_dispatch_guest { const input_dispatch_stack *stack; es2_input_call call; } input_dispatch_guest;
__attribute__((used)) static input_dispatch_stack *input_dispatch_torso_aim_pending;
/* The original's ECX at RET (41a808's) for the shim, valid only after a
 * replacement entered through the shim ran (as control_record_entry). */
__attribute__((used)) static uint32_t input_dispatch_torso_aim_ecx,input_dispatch_torso_aim_ecx_valid;
static void __cdecl ai_behaviour_stack_call(uint32_t target,const uint32_t *args,unsigned count,es2_weapon_registers *registers,
    const uint32_t *call);
static void input_dispatch_stack_guest_call(void *context,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    const input_dispatch_guest *c=context;uint32_t top=c->stack->top;
    /* Slots the original pushed before this call (41a808's last two at
     * 47de72) lie above its arguments. */
    for(uint32_t k=0;k<c->call.pushed&&k<2u;++k) { top-=4u;math_native_write32(NULL,top,c->call.temp[k]); }
    const uint32_t call[6]={c->call.ebx,c->call.esi,c->call.edi,c->stack->ebp,top,c->call.ret};
    ai_behaviour_stack_call(f,a,n,r,call);
}
static uint64_t __cdecl input_dispatch_torso_aim(const uint32_t *a,void *u,const uint32_t *r) {
    const input_dispatch_stack *s=input_dispatch_torso_aim_pending;input_dispatch_torso_aim_pending=NULL;
    /* The context belongs to this invocation only if the slots above the
     * shim's EBP are this call's. */
    for(unsigned i=0;s&&i<2;++i)if(math_native_read32(NULL,s->ebp+8u+4u*i)!=a[i])s=NULL;
    BYTE frame[ES2_TORSO_AIM_SCRATCH+4]={0};
    es2_weapon_graph g=weapons_graph(u,frame,ES2_HOOK_INDEX_torso_aim);
    input_dispatch_guest c={s,{0,0,0,0,0,{0,0}}};
    if(s) { g.scratch=s->ebp-ES2_TORSO_AIM_SCRATCH;g.call=input_dispatch_stack_guest_call;g.context=&c; }
    uint32_t ecx=0;
    const uint64_t v=es2_torso_aim_frame_registers(&math_native_memory,&g,s?&c.call:NULL,&ecx,a[0],a[1],r);
    if(s) { input_dispatch_torso_aim_ecx=ecx;input_dispatch_torso_aim_ecx_valid=1; }
    return v;
}
/* torso_aim's entry shim. PUSH EBP gives the original's EBP; the caller's
 * EBX/ESI/EDI go to the original's save slots; 48 probed pages = 192 KiB:
 * torso_aim's callees are the servos and math leaves, no emitter, but a
 * callee hook in compare mode reserves 64 KiB of comparison storage below
 * its entry and the in-vitro passes nest two of them; aim_fire's 64-page
 * gap still encloses this gap and torso_aim's host frames. The context
 * {EBP, EBP-0B8h} sits right below the gap; the two slots are copied for
 * the framework's dispatch. EAX, EDX and ECX pass through (41ef14 writes
 * all three before it reads them). After the dispatch the shim loads the
 * original's ECX at RET (41a808's) if the replacement ran; at 41f6f3 it
 * reaches the weapon +60h (40e62c ignores it), so this is exactness, not a
 * known reader. */
static int input_dispatch_torso_aim_entry(es2_machine *m,uint32_t bridge) {
    uint32_t ebp;const uint32_t context=shim_saved_gap(m,bridge,&ebp,-0xb0,48u);
    input_dispatch_torso_aim_pending=es2_guest_span(context,sizeof(input_dispatch_stack));input_dispatch_torso_aim_ecx_valid=0;
    const int ran=shim_gap_call(m,bridge,ebp,context,2);
    if(input_dispatch_torso_aim_ecx_valid) { m->ecx=input_dispatch_torso_aim_ecx;input_dispatch_torso_aim_ecx_valid=0; }
    input_dispatch_torso_aim_pending=NULL;return ran;
}
ES2_SHIM(input_dispatch_torso_aim_shim,0x1ef14u,input_dispatch_torso_aim_entry,0);
/* player_input at the original stack coordinate (input.md, "stack residue
 * under the command handler"). The
 * entry shim input_dispatch_player_input_entry (below) keeps the original's
 * EBP (T-4 for entry ESP T), stores the caller's EBX/ESI/EDI where 460870's
 * prologue saves them (EBP-90h/-94h/-98h), reserves a guard-probed gap and
 * leaves {EBP, outgoing ESP = EBP-98h} in input_dispatch_player_input_pending.
 * The core's frame is then the original one (scratch at EBP-90h, synthetic
 * EBP = EBP) and every game call runs through ai_behaviour_stack_call with
 * the EBX/ESI/EDI and return address the core names (the caller's EDI from
 * its save slot at the first two calls), so the command handler and
 * everything below it find the stack bytes the original leaves there: the
 * group order 4231a4 reads its uninitialized priority word EBP-1Ch for codes
 * 3, 6 and 9..0Eh (Alt+O, live c3_09-wingman-orders). Without the shim
 * (direct and legacy calls) the adapter keeps the plain bridge and its own
 * frame. */
__attribute__((used)) static input_dispatch_stack *input_dispatch_player_input_pending;
static uint64_t __cdecl input_dispatch_player_input(const uint32_t *a,void *u,const uint32_t *r) {
    const input_dispatch_stack *s=input_dispatch_player_input_pending;input_dispatch_player_input_pending=NULL;
    /* The context belongs to this invocation only if the slot above the
     * shim's EBP is this call's. */
    if(s&&math_native_read32(NULL,s->ebp+8u)!=a[0])s=NULL;
    BYTE frame[ES2_PLAYER_INPUT_SCRATCH+4]={0};
    es2_weapon_graph g=weapons_graph(u,frame,ES2_HOOK_INDEX_player_input);
    if(s) {
        input_dispatch_guest c={s,{0,0,0,0,0,{0,0}}};
        g.scratch=s->ebp-ES2_PLAYER_INPUT_SCRATCH;g.call=input_dispatch_stack_guest_call;g.context=&c;
        return es2_player_input_frame_registers(&math_native_memory,&g,&c.call,math_native_read32(NULL,s->ebp-0x98u),a[0],r);
    }
    es2_input_saved saved={0,0,0};
    g.call=input_dispatch_guest_call;g.context=&saved;
    return es2_player_input_registers(&math_native_memory,&g,&saved,a[0],r);
}
/* player_input's entry shim, as torso_aim's: 64 guard-probed pages (256
 * KiB: the callees' subtrees run above the adapter's host frames, among
 * them torso_aim's 48-page gap and the emitters' 32 pages under fire_line,
 * the command handler with its own gap and, below it, hud_key's); {EBP,
 * EBP-98h} sits right below the gap; the slot is copied for the framework's
 * dispatch. EAX/EDX/ECX pass through (460870 reads EAX's high word: it
 * reaches the command handler's slot). ECX after 460870 is dead (45f83d
 * pops it). */
static int input_dispatch_player_input_entry(es2_machine *m,uint32_t bridge) {
    uint32_t ebp;const uint32_t context=shim_saved_gap(m,bridge,&ebp,-0x90,64u);
    input_dispatch_player_input_pending=es2_guest_span(context,sizeof(input_dispatch_stack));
    const int ran=shim_gap_call(m,bridge,ebp,context,1);
    input_dispatch_player_input_pending=NULL;return ran;
}
ES2_SHIM(input_dispatch_player_input_shim,0x60870u,input_dispatch_player_input_entry,0);
static callee_counter torso_center_counters[]={
    {"call_47e0b0",ES2_COLLIDE_CALL,0x47e0b0,0},
    {"call_41a550",ES2_COLLIDE_CALL,0x41a550,0},
    {"call_41a808",ES2_COLLIDE_CALL,0x41a808,0},
};
static callee_counter torso_aim_counters[]={
    {"call_47ebb8",ES2_COLLIDE_CALL,0x47ebb8,0},
    {"call_47df19",ES2_COLLIDE_CALL,0x47df19,0},
    {"call_480304",ES2_COLLIDE_CALL,0x480304,0},
    {"call_48043c",ES2_COLLIDE_CALL,0x48043c,0},
    {"call_492990",ES2_COLLIDE_CALL,0x492990,0},
    {"call_47e0b0",ES2_COLLIDE_CALL,0x47e0b0,0},
    {"call_47e0a0",ES2_COLLIDE_CALL,0x47e0a0,0},
    {"call_41a550",ES2_COLLIDE_CALL,0x41a550,0},
    {"call_47de72",ES2_COLLIDE_CALL,0x47de72,0},
    {"call_41a808",ES2_COLLIDE_CALL,0x41a808,0},
    {"slot_20",ES2_COLLIDE_SLOT,0x20,0},
};
static callee_counter player_input_counters[]={
    {"call_45feb8",ES2_COLLIDE_CALL,0x45feb8,0},
    {"call_432b14",ES2_COLLIDE_CALL,0x432b14,0},
    {"call_45c524",ES2_COLLIDE_CALL,0x45c524,0},
    {"call_477904",ES2_COLLIDE_CALL,0x477904,0},
    {"call_401c74",ES2_COLLIDE_CALL,0x401c74,0},
    {"call_41bb9c",ES2_COLLIDE_CALL,0x41bb9c,0},
    {"call_49288c",ES2_COLLIDE_CALL,0x49288c,0},
    {"call_47e0b0",ES2_COLLIDE_CALL,0x47e0b0,0},
    {"call_4160dc",ES2_COLLIDE_CALL,0x4160dc,0},
    {"call_41a550",ES2_COLLIDE_CALL,0x41a550,0},
    {"call_41a808",ES2_COLLIDE_CALL,0x41a808,0},
    {"call_41b728",ES2_COLLIDE_CALL,0x41b728,0},
    {"call_41ef14",ES2_COLLIDE_CALL,0x41ef14,0},
    {"call_41e8d4",ES2_COLLIDE_CALL,0x41e8d4,0},
    {"call_415608",ES2_COLLIDE_CALL,0x415608,0},
    {"call_49681a",ES2_COLLIDE_CALL,0x49681a,0},
    {"call_45b734",ES2_COLLIDE_CALL,0x45b734,0},
    {"call_43349c",ES2_COLLIDE_CALL,0x43349c,0},
    {"call_4333c8",ES2_COLLIDE_CALL,0x4333c8,0},
    {"call_429820",ES2_COLLIDE_CALL,0x429820,0},
    {"call_432bc8",ES2_COLLIDE_CALL,0x432bc8,0},
    {"call_42a3f4",ES2_COLLIDE_CALL,0x42a3f4,0},
    {"call_446e3c",ES2_COLLIDE_CALL,0x446e3c,0},
    {"call_410f78",ES2_COLLIDE_CALL,0x410f78,0},
    {"slot_3c",ES2_COLLIDE_SLOT,0x3c,0},
    {"slot_38",ES2_COLLIDE_SLOT,0x38,0},
    {"slot_2c",ES2_COLLIDE_SLOT,0x2c,0},
    {"slot_0c",ES2_COLLIDE_SLOT,0x0c,0},
};
static callee_counter *input_dispatch_hook_counters(unsigned hook,unsigned *count) {
#define INPUT_DISPATCH_COUNTERS(name) if(hook==ES2_HOOK_INDEX_##name) { *count=sizeof(name##_counters)/sizeof(name##_counters[0]);return name##_counters; }
    INPUT_DISPATCH_COUNTERS(torso_center)
    INPUT_DISPATCH_COUNTERS(torso_aim)
    INPUT_DISPATCH_COUNTERS(player_input)
#undef INPUT_DISPATCH_COUNTERS
    *count=0;return NULL;
}
static const BYTE input_dispatch_center_sig[]={0x55,0x8b,0xec,0x51,0x53,0x56,0x57};
static const BYTE input_dispatch_aim_sig[]={0x55,0x8b,0xec,0x81,0xc4,0x54,0xff,0xff,0xff};
static const BYTE input_dispatch_player_sig[]={0x55,0x8b,0xec,0x81,0xc4,0x74,0xff,0xff,0xff};
static const BYTE input_dispatch_push_lens[]={1,2,1,1},input_dispatch_frame_lens[]={1,2,6};
#define INPUT_DISPATCH_EXT(name) \
    static const es2_hook_extension input_dispatch_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,input_dispatch_##name,TRUE}; \
    static uint32_t __cdecl input_dispatch_##name##32(const uint32_t *a,void *u) {return (uint32_t)input_dispatch_##name(a,u,weapons_no_registers);}
INPUT_DISPATCH_EXT(torso_center)
INPUT_DISPATCH_EXT(torso_aim)
INPUT_DISPATCH_EXT(player_input)
#undef INPUT_DISPATCH_EXT
static es2_hook_descriptor input_dispatch_hook_descriptors[]={
    {ES2_HOOK_ID(torso_center),input_dispatch_center_sig,7,5,input_dispatch_push_lens,4,2,input_dispatch_torso_center32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(torso_aim),input_dispatch_aim_sig,9,9,input_dispatch_frame_lens,3,2,input_dispatch_torso_aim32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(player_input),input_dispatch_player_sig,9,9,input_dispatch_frame_lens,3,1,input_dispatch_player_input32,NULL,NULL,NULL,0},
};
#endif
