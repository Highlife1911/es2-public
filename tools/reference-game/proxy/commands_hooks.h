#ifndef ES2_COMMANDS_HOOKS_H
#define ES2_COMMANDS_HOOKS_H
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
 * playback_key has an ENTRY SHIM (commands_playback_entry, chained from
 * transform_helpers_capture_entry through commands_capture_entry): its result is the caller's EBX high word over 1,
 * and its entry AX is overwritten (45fe74) before anything reads it, so the
 * shim moves EBX's high word into AX before the framework's dispatch; EBX
 * and the high word of EAX are unchanged. Its two callees (the display
 * switch 4667c8 and the debug trap 467ad8) read no callee-saved register
 * and go through the plain bridge. command_key has an ENTRY SHIM as
 * well (commands_command_key_entry) that runs it at the original stack
 * coordinate through the stack bridge of ai_behaviour_hooks.h, and
 * commands_capture_entry also installs hud_key's (hud_keys_hooks.h). Both
 * hooks refuse compare mode (game calls,
 * dialogs, allocation). No game instruction is embedded; the assembly only
 * marshals registers and moves ESP. */
#include "es2/core/commands.h"
/* Like weapons_machine_call, plus EBX, ESI and EDI from the core's block. */
__attribute__((naked,noinline)) static void __cdecl commands_machine_call(
    uint32_t target,const uint32_t *args,unsigned count,es2_weapon_registers *registers,const es2_commands_saved *saved) {
    __asm__ volatile(
        "pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "movl 12(%ebp),%esi\n\tmovl 16(%ebp),%ebx\n\tmovl %ebx,_weapons_bridge_count\n\t"
        "testl %ebx,%ebx\n\tjz 2f\n\t1: decl %ebx\n\tpushl (%esi,%ebx,4)\n\ttestl %ebx,%ebx\n\tjnz 1b\n\t"
        "2: movl %esp,_weapons_bridge_args\n\tmovl 24(%ebp),%eax\n\tmovl 0(%eax),%ebx\n\tmovl 4(%eax),%esi\n\tmovl 8(%eax),%edi\n\t"
        "movl 20(%ebp),%eax\n\tmovl 4(%eax),%edx\n\tmovl 8(%eax),%ecx\n\tmovl 0(%eax),%eax\n\tcall *8(%ebp)\n\t"
        "movl $0,_weapons_bridge_args\n\tmovl 20(%ebp),%edi\n\tmovl %eax,0(%edi)\n\tmovl %edx,4(%edi)\n\tmovl %ecx,8(%edi)\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
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
__attribute__((used)) static void *commands_command_key_bridge;
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
    __asm__ volatile("movl %0,%%fs:0"::"r"(head):"memory");return result;
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
__attribute__((used)) static void *commands_playback_bridge;
__attribute__((naked)) static void commands_playback_entry(void) {
    __asm__ volatile("pushl %ebx\n\tshrl $16,%ebx\n\tmovw %bx,%ax\n\tpopl %ebx\n\tjmp *_commands_playback_bridge\n\t");
}
/* command_key's entry shim, as hud_key's: PUSH EBP; the caller's
 * EBX/ESI/EDI to EBP-60h/-64h/-68h; EBX counts 48 guard-probed pages (192
 * KiB: the HUD key handler with its own 32-page gap and host frames, the
 * key method, the dialogs and the view requests run above the adapter's
 * host frames) and is reloaded; {EBP, EBP-68h} below the gap; the slot
 * copied for the framework's dispatch. EAX/EDX/ECX pass through (45feb8
 * writes EAX first; EDX/ECX reach 432bc8 through 49630c). ECX after 45feb8
 * is popped by every caller. */
__attribute__((used)) static void *commands_command_key_bridge;
__attribute__((naked)) static void commands_command_key_entry(void) {
    __asm__ volatile(
        "pushl %ebp\n\tmovl %esp,%ebp\n\tmovl %ebx,-0x60(%ebp)\n\tmovl %esi,-0x64(%ebp)\n\tmovl %edi,-0x68(%ebp)\n\t"
        "movl $48,%ebx\n\t1: subl $4096,%esp\n\torl $0,(%esp)\n\tdecl %ebx\n\tjnz 1b\n\t" ES2_SHIM_FLOOR
        "subl $8,%esp\n\tmovl %ebp,(%esp)\n\tleal -0x68(%ebp),%ebx\n\tmovl %ebx,4(%esp)\n\t"
        "movl %esp,_commands_command_key_pending\n\tmovl -0x60(%ebp),%ebx\n\t"
        "pushl 8(%ebp)\n\tcall *_commands_command_key_bridge\n\t"
        "movl $0,_commands_command_key_pending\n\tmovl %ebp,%esp\n\tpopl %ebp\n\tret\n\t");
}
/* Called by transform_helpers_capture_entry after the framework installed
 * its jump: redirect the jump of playback_key, command_key or
 * hud_key (hud_keys_hooks.h) to its shim and keep h->patch
 * consistent, so uninstall still validates the exact patch. Other hooks
 * continue in vfs_capture_entry (vfs_hooks.h; the end of the chain). */
static BOOL vfs_capture_entry(es2_hook *h);
static BOOL commands_capture_entry(es2_hook *h) {
    void *target;void **bridge;
    if(h->descriptor->rva==0x5fe6cu) { target=(void *)commands_playback_entry;bridge=&commands_playback_bridge; }
    else if(h->descriptor->rva==0x5feb8u) { target=(void *)commands_command_key_entry;bridge=&commands_command_key_bridge; }
    else if(h->descriptor->rva==0x32bc8u) { target=(void *)hud_keys_hud_key_entry;bridge=&hud_keys_hud_key_bridge; }
    else return vfs_capture_entry(h);
    DWORD old,ignored;
    if(memcmp(h->site,h->patch,h->descriptor->relocate_size))return FALSE;
    if(!VirtualProtect(h->site,h->descriptor->relocate_size,PAGE_EXECUTE_READWRITE,&old))return FALSE;
    *bridge=(void *)(uintptr_t)((uint32_t)(uintptr_t)(h->site+5)+math_native_read32(NULL,(uint32_t)(uintptr_t)(h->patch+1)));
    math_native_write32(NULL,(uint32_t)(uintptr_t)(h->patch+1),(uint32_t)(uintptr_t)target-(uint32_t)(uintptr_t)(h->site+5));
    memcpy(h->site,h->patch,h->descriptor->relocate_size);
    BOOL flushed=FlushInstructionCache(GetCurrentProcess(),h->site,h->descriptor->relocate_size);
    BOOL protected=VirtualProtect(h->site,h->descriptor->relocate_size,old,&ignored);
    return flushed && protected;
}
#endif
