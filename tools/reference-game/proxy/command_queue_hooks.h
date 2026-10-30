#ifndef ES2_COMMAND_QUEUE_HOOKS_H
#define ES2_COMMAND_QUEUE_HOOKS_H
/* command_queue group (docs/reverse-engineering/input.md, section
 * "command-key queue callback 45a498"): command_queue 45a498, the
 * hot-key callback of the list 49eb35 that the keyboard handler 477bec
 * calls (cdecl, one slot: the whole key code; plain RET). While the
 * capture byte 49eaa4 is set it appends the low word of the code to the
 * queue 4d20c4 at the unchecked dword index 49eaa0 and increments the
 * index. No game call and no Win32 function (no graph, no counters,
 * nothing for the input journal). EAX (1 consumed, 2 pass) and EDX are
 * exact: MOV DX keeps the high word of the entry EDX, which reaches the
 * adapter as r[1]. ECX is not touched by the original and not touched by
 * the hook. No local and no entry register other than EDX is read, so no
 * entry shim. Refuses compare mode: the queue index is unbounded, so the
 * written address is not a declarable region. No game instruction is
 * embedded. */
#include "es2/core/control_record.h"
static uint64_t __cdecl command_queue_command_queue(const uint32_t *a,void *u,const uint32_t *r) {
    return es2_command_queue_registers(&math_native_memory,(uint32_t)(uintptr_t)u,a[0],r[1]);
}
static callee_counter *command_queue_hook_counters(unsigned hook,unsigned *count) {
    (void)hook;*count=0;return NULL;
}
/* push ebp; mov ebp,esp; cmp byte [49eaa4h],0 */
static const BYTE command_queue_command_queue_sig[]={0x55,0x8b,0xec,0x80,0x3d,0xa4,0xea,0x49,0x00,0x00};
static const BYTE command_queue_frame_lens[]={1,2,7};
static const es2_hook_extension command_queue_command_queue_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,command_queue_command_queue,TRUE};
static uint32_t __cdecl command_queue_command_queue32(const uint32_t *a,void *u) {return (uint32_t)command_queue_command_queue(a,u,weapons_no_registers);}
static es2_hook_descriptor command_queue_hook_descriptors[]={
    {ES2_HOOK_ID(command_queue),command_queue_command_queue_sig,10,10,command_queue_frame_lens,3,1,command_queue_command_queue32,NULL,NULL,NULL,0},
};
#endif
