/* Test probe: a machine call as an adapter's bridge makes it, with the stack
 * slots pushed, EAX/EDX/ECX loaded and EBX set to a marker; afterwards the
 * results, the bytes the callee removed, EBX and (on request) ST0 are stored.
 * Optionally an FS:0 exception record with a given handler is linked around
 * the call (the guest's frame records, own_process.md 4.4).
 *
 * typedef struct sim_probe {
 *   0 target, 4 slot_count, 8 slots, 12 eax, 16 edx, 20 ecx,
 *   24 eax_out, 28 edx_out, 32 ecx_out, 36 popped, 40 st0[12], 52 want_st0,
 *   56 ebx_out, 60 seh_handler, 64 fs0_during
 * } */
.text
.globl _sim_probe_call
_sim_probe_call:
    pushl %ebp
    movl %esp, %ebp
    pushl %ebx
    pushl %esi
    pushl %edi
    movl 8(%ebp), %edi
    cmpl $0, 60(%edi)
    je 1f
    pushl 60(%edi)
    pushl %fs:0
    movl %esp, %fs:0
1:
    movl %fs:0, %eax
    movl %eax, 64(%edi)
    movl 4(%edi), %ecx
    movl 8(%edi), %edx
2:
    testl %ecx, %ecx
    jz 3f
    decl %ecx
    pushl (%edx,%ecx,4)
    jmp 2b
3:
    movl %esp, %esi
    movl $0x5a5a1234, %ebx
    movl 12(%edi), %eax
    movl 16(%edi), %edx
    movl 20(%edi), %ecx
    call *0(%edi)
    movl %eax, 24(%edi)
    movl %edx, 28(%edi)
    movl %ecx, 32(%edi)
    movl %esp, %eax
    subl %esi, %eax
    movl %eax, 36(%edi)
    movl %ebx, 56(%edi)
    cmpl $0, 52(%edi)
    je 4f
    fstpt 40(%edi)
4:
    cmpl $0, 60(%edi)
    je 5f
    movl -20(%ebp), %eax
    movl %eax, %fs:0
5:
    leal -12(%ebp), %esp
    popl %edi
    popl %esi
    popl %ebx
    popl %ebp
    ret

/* The same probe with the flags of the CALL set and the flags after
 * it stored (the layout above plus 68 flags_in, 72 flags_out). The flags
 * are loaded after the argument pushes and the register loads keep them
 * (MOV), so the callee starts with exactly flags_in. */
.globl _sim_probe_call_flags
_sim_probe_call_flags:
    pushl %ebp
    movl %esp, %ebp
    pushl %ebx
    pushl %esi
    pushl %edi
    movl 8(%ebp), %edi
    movl 4(%edi), %ecx
    movl 8(%edi), %edx
2:
    testl %ecx, %ecx
    jz 3f
    decl %ecx
    pushl (%edx,%ecx,4)
    jmp 2b
3:
    movl %esp, %esi
    pushl 68(%edi)
    popfl
    movl $0x5a5a1234, %ebx
    movl 12(%edi), %eax
    movl 16(%edi), %edx
    movl 20(%edi), %ecx
    call *0(%edi)
    pushfl
    popl 72(%edi)
    movl %eax, 24(%edi)
    movl %edx, 28(%edi)
    movl %ecx, 32(%edi)
    movl %esp, %eax
    subl %esi, %eax
    movl %eax, 36(%edi)
    movl %ebx, 56(%edi)
    cmpl $0, 52(%edi)
    je 4f
    fstpt 40(%edi)
4:
    cld
    leal -12(%ebp), %esp
    popl %edi
    popl %esi
    popl %ebx
    popl %ebp
    ret

/* A stand-in entry shim: counts its runs and continues at the trampoline, as
 * the proxy's shims reach it through their bridge. */
.globl _sim_test_shim
_sim_test_shim:
    incl _sim_test_shim_runs
    jmp _es2_sim_trap_entry

.data
.globl _sim_test_shim_runs
_sim_test_shim_runs:
    .long 0
