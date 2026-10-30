/* The stage-1 trampoline (see dispatch_trap.c, es2/sim/trap.h). The trap
 * handler continues here with the caller's registers and stack exactly as at
 * the faulting call: ESP points at the return address, the arguments follow.
 *
 * Frame after pushfl/pushal (EBP points at it):
 *   +0 EDI  +4 ESI  +8 EBP  +12 ESP  +16 EBX  +20 EDX  +24 ECX  +28 EAX
 *   +32 EFLAGS  +36 return address  +40 first argument slot
 * es2_sim_trap_prepare(frame) returns the plan {target, a0, a1, a2,
 * ecx_out, pop_bytes}; the target is called here (cdecl, three arguments),
 * so an x87 result stays in ST0 untouched. EAX/EDX/ECX of the frame are then
 * replaced by the results, the 40-byte block (frame, flags, return address)
 * is moved up by pop_bytes over the arguments the callee removes, and popal/
 * popfl/ret return to the caller. */
.text
.globl _es2_sim_trap_entry
_es2_sim_trap_entry:
    pushfl
    pushal
    cld
    movl %esp, %ebp
    andl $-16, %esp
    subl $12, %esp
    pushl %ebp
    call _es2_sim_trap_prepare
    movl %eax, %esi
    andl $-16, %esp
    subl $4, %esp
    pushl 12(%esi)
    pushl 8(%esi)
    pushl 4(%esi)
    call *0(%esi)
    movl %eax, 28(%ebp)
    movl %edx, 20(%ebp)
    movl 16(%esi), %eax
    movl %eax, 24(%ebp)
    movl 20(%esi), %ecx
    decl _es2_sim_trap_level
    leal (%ebp,%ecx), %edi
    movl $9, %edx
1:
    movl (%ebp,%edx,4), %eax
    movl %eax, (%edi,%edx,4)
    decl %edx
    jns 1b
    movl %edi, %esp
    popal
    popfl
    ret

/* The entry of a hook with a shim (es2/sim/trap.h). The handler has
 * left ESP, EAX, EDX, ECX and the other registers as at the call; the
 * ES2_SIM_SHIM_ZERO (4096) bytes below ESP are zeroed top-down (the stack
 * grows page by page through its guard page), then the shim runs with the
 * registers of the call. The flags are not kept (the shims' probe loops
 * clobber them as well). */
.globl _es2_sim_trap_shim_entry
_es2_sim_trap_shim_entry:
    movl %eax, _es2_sim_trap_shim_save
    movl %ecx, _es2_sim_trap_shim_save+4
    movl %edi, _es2_sim_trap_shim_save+8
    leal -4(%esp), %edi
    movl $1024, %ecx
    xorl %eax, %eax
    std
    rep stosl
    cld
    movl _es2_sim_trap_shim_save, %eax
    movl _es2_sim_trap_shim_save+4, %ecx
    movl _es2_sim_trap_shim_save+8, %edi
    jmp *_es2_sim_trap_pending_shim

/* es2_sim_trap_call(va, slots, count, regs, out): cdecl. The VA goes
 * through the bridge seam first (es2/sim/trap.h); nothing between it and the
 * CALL reaches the dispatch. */
.globl _es2_sim_trap_call
_es2_sim_trap_call:
    pushl %ebp
    movl %esp, %ebp
    pushl %ebx
    pushl %esi
    pushl %edi
    pushl 8(%ebp)
    call _es2_sim_bridge_target
    addl $4, %esp
    movl %eax, 8(%ebp)
    movl 16(%ebp), %ecx
    movl 12(%ebp), %esi
1:
    testl %ecx, %ecx
    jz 2f
    decl %ecx
    pushl (%esi,%ecx,4)
    jmp 1b
2:
    movl 20(%ebp), %ebx
    movl (%ebx), %eax
    movl 4(%ebx), %edx
    movl 8(%ebx), %ecx
    call *8(%ebp)
    movl 24(%ebp), %ebx
    movl %eax, (%ebx)
    movl %edx, 4(%ebx)
    movl %ecx, 8(%ebx)
    leal -12(%ebp), %esp
    popl %edi
    popl %esi
    popl %ebx
    popl %ebp
    ret
