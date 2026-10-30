/* Own x86 ABI adapters. Layout offsets are asserted in hook.c. */
.text
.globl _es2_hook_fpu_save
_es2_hook_fpu_save:
    movl 4(%esp), %eax
    fnsave (%eax)
    frstor (%eax)
    ret

.globl _es2_hook_machine_call
_es2_hook_machine_call:
    pushl %ebp
    movl %esp, %ebp
    pushl %ebx
    pushl %esi
    pushl %edi
    movl 8(%ebp), %edi
    movl 20(%edi), %esi
    movl 0(%edi), %ebx
    movl 8(%edi), %ecx
    movl 12(%edi), %edx
    xorl %eax, %eax
    cmpl $2, 4(%edi)
    jne 1f
    movl $3, %eax
    /* Borland register convention pushes remaining slots left to right. */
    cmpl %ecx, %eax
    jae 3f
5:
    pushl (%edx,%eax,4)
    incl %eax
    cmpl %ecx, %eax
    jb 5b
    jmp 3f
1:
    cmpl %eax, %ecx
    jbe 3f
2:
    decl %ecx
    pushl (%edx,%ecx,4)
    cmpl %eax, %ecx
    ja 2b
3:
    movl 16(%edi), %eax
    frstor (%eax)
    cmpl $2, 4(%edi)
    je 6f
    cmpl $0, 24(%edi)
    je 4f
    movl 24(%edi), %eax
    movl 8(%eax), %ecx
    movl 4(%eax), %edx
    movl 0(%eax), %eax
    jmp 4f
6:
    /* All call sites supply MAX_ARGS accessible words, zero-filled as needed. */
    movl 8(%edx), %ecx
    movl 0(%edx), %eax
    movl 4(%edx), %edx
4:
    call *%ebx
    movl %eax, 0(%esi)
    movl %edx, 4(%esi)
    fnsave 8(%esi)
    frstor 8(%esi)
    leal -12(%ebp), %esp
    popl %edi
    popl %esi
    popl %ebx
    popl %ebp
    ret

/* Called by a generated stub: [return-to-stub, hook, caller-return, args].
 * Save incoming scratch registers before C code, and restore the selected
 * original/replacement x87 state immediately before returning to the caller. */
.globl _es2_hook_entry_extended
_es2_hook_entry_extended:
    pushfl
    pushal
    subl $108, %esp
    fnsave (%esp)
    frstor (%esp)
    movl %esp, %eax
    pushl %eax
    call _es2_hook_dispatch_extended
    addl $4, %esp
    frstor (%esp)
    addl $108, %esp
    popal
    popfl
    ret

/* Same register/stack frame as the generic entry. The 108-byte reservation
 * keeps one layout; on success it is never read or written. */
.globl _es2_hook_entry_fast
_es2_hook_entry_fast:
    pushfl
    pushal
    subl $108, %esp
    movl %esp, %eax
    pushl %eax
    call _es2_hook_dispatch_fast
    addl $4, %esp
    testl %eax, %eax
    jnz 7f
    fnsave (%esp)
    frstor (%esp)
    movl %esp, %eax
    pushl %eax
    call _es2_hook_dispatch_extended
    addl $4, %esp
    frstor (%esp)
7:
    addl $108, %esp
    popal
    popfl
    ret

/* All replacement callbacks are cdecl. Supplying the context argument to a
 * two-argument callback is harmless. Capture even ST0 returns here so C
 * never spills/pops/reloads ST0 (which would change status/tags/precision). */
.globl _es2_hook_fast_callback
_es2_hook_fast_callback:
    pushl %ebp
    movl %esp, %ebp
    pushl %esi
    movl 24(%ebp), %esi
    andl $-16, %esp
    subl $4, %esp
    pushl 20(%ebp)
    pushl 16(%ebp)
    pushl 12(%ebp)
    call *8(%ebp)
    movl %eax, 136(%esi)
    movl %edx, 128(%esi)
    leal -4(%ebp), %esp
    popl %esi
    popl %ebp
    ret
