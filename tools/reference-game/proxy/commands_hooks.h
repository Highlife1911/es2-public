#ifndef ES2_COMMANDS_HOOKS_H
#define ES2_COMMANDS_HOOKS_H
#include "guest_native.h"
/* commands group (docs/reverse-engineering/input.md, section "command
 * handler as own code"): the command handler 45feb8 (command_key)
 * and its playback key check 45fe6c (playback_key). Every game call goes
 * through the callee observer (weapons_graph) with the original's entry
 * EAX/EDX/ECX, so the nested hooks (hud_key 432bc8, playback_key, dist_gate
 * 49288c, status_eval 413180, cockpit_nearest 4333c8 and, through the
 * vtables, vehicle_keys 4157c8 (+2Ch) and vehicle_part 417de4 (+74h)) keep
 * their own modes. command_key's bridge (commands_machine_call) loads the
 * EBX, ESI and EDI the original holds at each call (the core writes them
 * into es2_commands_saved): the key method +2Ch returns its caller's ESI
 * high word through weapon_keys, 45fe6c its caller's EBX high word. Its
 * exception frame 49630c gets the synthetic EBP (ES2_COMMANDS_FRAME in the
 * scratch) through weapons_exception_call, and the adapter restores FS:0 to
 * the record's saved head (the core's *fs_head) as the original's epilogue
 * does. EDX:EAX exact; ECX is popped by every caller of both functions, so
 * the framework returns the entry ECX (no exit shim).
 * playback_key has an ENTRY SHIM (commands_playback_entry): its result is the caller's EBX high word over 1,
 * and its entry AX is overwritten (45fe74) before anything reads it, so the
 * shim moves EBX's high word into AX before the framework's dispatch; EBX
 * and the high word of EAX are unchanged. Its two callees (the display
 * switch 4667c8 and the debug trap 467ad8) read no callee-saved register
 * and go through the plain bridge. command_key has an ENTRY SHIM as
 * well (commands_command_key_entry) that runs it at the original stack
 * coordinate through the stack bridge of ai_behaviour_hooks.h, like
 * hud_key's (hud_keys_hooks.h). Both
 * hooks refuse compare mode (game calls,
 * dialogs, allocation). No game instruction is embedded; the shim only
 * marshals registers and moves ESP. */
#include "es2/core/commands.h"
/* Like weapons_machine_call, plus EBX, ESI and EDI from the core's block. */
static void __cdecl commands_machine_call(uint32_t target,const uint32_t *args,unsigned count,
    es2_weapon_registers *registers,const es2_commands_saved *saved) {
    const uint32_t frame[4]={saved->ebx,saved->esi,saved->edi,0};
    weapons_machine_call_at(target,args,count,registers,frame,0,NULL,0,NULL);
}
typedef struct commands_context { uint32_t image,frame; const es2_commands_saved *saved; } commands_context;
static void commands_guest_call(void *context,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    const commands_context *c=context;
    if(f==c->image+0x9630cu)weapons_exception_call(f,c->frame,r);
    else commands_machine_call(f,a,n,r,c->saved);
}
/* command_key at the original stack coordinate (input.md, "stack residue
 * under the command handler"), as hud_key: the entry shim commands_command_key_entry (below) keeps the
 * original's EBP, stores the caller's EBX/ESI/EDI where 45feb8's prologue
 * saves them (EBP-60h/-64h/-68h), reserves a guard-probed gap and leaves
 * {EBP, outgoing ESP = EBP-68h} in commands_command_key_pending; the core's
 * frame is the original one (f = EBP; 49630c links the record at EBP-38h)
 * and every game call, 49630c included, runs through ai_behaviour_stack_call
 * with the EBX/ESI/EDI and return address the core names (the caller's EBX
 * and EDI at 49630c and 432bc8). So hud_key and the MFD chain below it find
 * the original's stack bytes. Without the shim the plain bridge and the
 * synthetic EBP in the adapter's scratch stay. */
__attribute__((used)) static ai_behaviour_stack *commands_command_key_pending;
typedef struct commands_stack_guest { const ai_behaviour_stack *stack; es2_weapon_frame frame; } commands_stack_guest;
static void commands_stack_call(void *context,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    const commands_stack_guest *c=context;
    const uint32_t call[6]={c->frame.ebx,c->frame.esi,c->frame.edi,c->stack->ebp,c->stack->top,c->frame.ret};
    ai_behaviour_stack_call(f,a,n,r,call);
}
static uint64_t __cdecl commands_command_key(const uint32_t *a,void *u,const uint32_t *r) {
    const ai_behaviour_stack *s=ai_behaviour2_take(&commands_command_key_pending,a,1);
    BYTE scratch[ES2_COMMANDS_SCRATCH]={0};es2_commands_saved saved={0,0,0};
    es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_command_key);
    uint32_t head;uint64_t result;
    if(s) {
        commands_stack_guest c={s,{0,0,0,0}};
        g.scratch=s->ebp-ES2_COMMANDS_FRAME;g.call=commands_stack_call;g.context=&c;
        result=es2_command_key_frame_registers(&math_native_memory,&g,&c.frame,math_native_read32(NULL,s->ebp-0x60u),
            math_native_read32(NULL,s->ebp-0x68u),a[0],r,&head);
    } else {
        commands_context c={(uint32_t)(uintptr_t)u,(uint32_t)(uintptr_t)(scratch+ES2_COMMANDS_FRAME),&saved};
        g.call=commands_guest_call;g.context=&c;
        result=es2_command_key_registers(&math_native_memory,&g,&saved,a[0],r,&head);
    }
    es2_machine_set_fs0(head);return result;
}
/* r[0]: the caller's EAX high word over the caller's EBX high word (entry
 * shim below). */
static uint64_t __cdecl commands_playback_key(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_playback_key);
    const uint32_t entry[3]={r[0]&0xffff0000u,r[1],r[2]};
    return es2_playback_key_registers(&math_native_memory,&g,a[0],r[0]<<16,entry);
}
/* Callee counters (hk_<hook>_<suffix>), incremented only while the hook is
 * non-original. Keys: the callee VA for calls, the vtable offset for slots. */
static callee_counter command_key_counters[]={
    {"call_49630c",ES2_COLLIDE_CALL,0x49630c,0},
    {"call_432bc8",ES2_COLLIDE_CALL,0x432bc8,0},
    {"call_45fe6c",ES2_COLLIDE_CALL,0x45fe6c,0},
    {"call_401148",ES2_COLLIDE_CALL,0x401148,0},
    {"call_49288c",ES2_COLLIDE_CALL,0x49288c,0},
    {"call_411e40",ES2_COLLIDE_CALL,0x411e40,0},
    {"call_411e60",ES2_COLLIDE_CALL,0x411e60,0},
    {"call_42a3f4",ES2_COLLIDE_CALL,0x42a3f4,0},
    {"call_45df34",ES2_COLLIDE_CALL,0x45df34,0},
    {"call_45fe38",ES2_COLLIDE_CALL,0x45fe38,0},
    {"call_429828",ES2_COLLIDE_CALL,0x429828,0},
    {"call_4610ad",ES2_COLLIDE_CALL,0x4610ad,0},
    {"call_441fa4",ES2_COLLIDE_CALL,0x441fa4,0},
    {"call_442120",ES2_COLLIDE_CALL,0x442120,0},
    {"call_413180",ES2_COLLIDE_CALL,0x413180,0},
    {"call_47749c",ES2_COLLIDE_CALL,0x47749c,0},
    {"call_455950",ES2_COLLIDE_CALL,0x455950,0},
    {"call_4543b0",ES2_COLLIDE_CALL,0x4543b0,0},
    {"call_4561dc",ES2_COLLIDE_CALL,0x4561dc,0},
    {"call_45cff0",ES2_COLLIDE_CALL,0x45cff0,0},
    {"call_477904",ES2_COLLIDE_CALL,0x477904,0},
    {"call_4333c8",ES2_COLLIDE_CALL,0x4333c8,0},
    {"call_45f070",ES2_COLLIDE_CALL,0x45f070,0},
    {"call_4669c4",ES2_COLLIDE_CALL,0x4669c4,0},
    {"slot_74",ES2_COLLIDE_SLOT,0x74,0},
    {"slot_2c",ES2_COLLIDE_SLOT,0x2c,0},
    {"slot_3c",ES2_COLLIDE_SLOT,0x3c,0},
    {"slot_8",ES2_COLLIDE_SLOT,0x8,0},
};
static callee_counter playback_key_counters[]={
    {"call_4667c8",ES2_COLLIDE_CALL,0x4667c8,0},
    {"call_467ad8",ES2_COLLIDE_CALL,0x467ad8,0},
};
static callee_counter *commands_hook_counters(unsigned hook,unsigned *count) {
    if(hook==ES2_HOOK_INDEX_command_key){*count=sizeof(command_key_counters)/sizeof(command_key_counters[0]);return command_key_counters;}
    if(hook==ES2_HOOK_INDEX_playback_key){*count=sizeof(playback_key_counters)/sizeof(playback_key_counters[0]);return playback_key_counters;}
    *count=0;return NULL;
}
/* 45feb8: push ebp; mov ebp,esp; add esp,-5Ch; mov eax,49F648h; push ebx;
 * push esi; push edi; mov esi,[ebp+8]. Relocated 6 bytes (1,2,3). */
static const BYTE command_key_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xa4,0xb8,0x48,0xf6,0x49,0x00,0x53,0x56,0x57,0x8b,0x75,0x08};
static const BYTE command_key_lens[]={1,2,3};
/* 45fe6c: push ebp; mov ebp,esp; push ebx; mov bx,1; mov ax,[ebp+8];
 * sub ax,21Ch. Relocated 8 bytes (1,2,1,4: MOV r16,imm16 is on the
 * framework's whitelist). */
static const BYTE playback_key_sig[]={0x55,0x8b,0xec,0x53,0x66,0xbb,0x01,0x00,0x66,0x8b,0x45,0x08,0x66,0x2d,0x1c,0x02};
static const BYTE playback_key_lens[]={1,2,1,4};
#define COMMANDS_EXT(name) \
    static const es2_hook_extension commands_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,commands_##name,TRUE}; \
    static uint32_t __cdecl commands_##name##32(const uint32_t *a,void *u) {return (uint32_t)commands_##name(a,u,weapons_no_registers);}
COMMANDS_EXT(command_key)
COMMANDS_EXT(playback_key)
#undef COMMANDS_EXT
static es2_hook_descriptor commands_hook_descriptors[]={
    {ES2_HOOK_ID(command_key),command_key_sig,17,6,command_key_lens,3,1,commands_command_key32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(playback_key),playback_key_sig,16,8,playback_key_lens,4,1,commands_playback_key32,NULL,NULL,NULL,0},
};
/* playback_key's entry shim: AX <- the caller's EBX high word, then the
 * framework's dispatch (bridge address captured below). Flags are dead at
 * 45fe6c's entry (45fe78 SUB writes them first). */
static int commands_playback_entry(es2_machine *m,uint32_t bridge) {(void)bridge;m->eax=(m->eax&0xffff0000u)|m->ebx>>16;return 0;}
ES2_SHIM(commands_playback_shim,0x5fe6cu,commands_playback_entry,0);
static int commands_command_key_entry(es2_machine *m,uint32_t bridge) {
    uint32_t ebp;const uint32_t context=shim_saved_gap(m,bridge,&ebp,-0x60,48u);
    commands_command_key_pending=es2_guest_span(context,sizeof(ai_behaviour_stack));
    const int ran=shim_gap_call(m,bridge,ebp,context,1);
    commands_command_key_pending=NULL;return ran;
}
ES2_SHIM(commands_command_key_shim,0x5feb8u,commands_command_key_entry,0);
#endif
