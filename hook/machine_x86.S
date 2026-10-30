/* The x86 backend's primitive (es2/hook/machine.h), after machine.S's
 * es2_hook_machine_call: es2_machine_x86_enter(m, target, slots, count,
 * plants, plant_count, entry), cdecl. es2_machine offsets: EAX 0, ECX 4,
 * EDX 8, EBX 12, ESP 16, EBP 20, ESI 24, EDI 28; es2_machine_plant: offset
 * 0, byte 4, 8 bytes each.
 *
 * Host frame below the saved EBP: EBX, ESI, EDI (12 bytes), the 108-byte
 * FNSAVE area, then the anchor block {host EBP, previous anchor} at
 * EBP - 128, the previous es2_machine_x86_slots at EBP - 132 and the
 * previous es2_machine_x86_floor at EBP - 136, the lowest host byte. Every
 * register holds a guest value from the load to the return, so the return
 * path finds the frame through the static anchor; the previous anchor,
 * slot address and floor are kept for nested calls. The guest stack gets
 * the slots and the return address, as a CALL would leave them, and the
 * planted bytes below them. */
.text
.globl _es2_machine_x86_enter
_es2_machine_x86_enter:
    pushl %ebp
    movl %esp, %ebp
    pushl %ebx
    pushl %esi
    pushl %edi
    subl $108, %esp
    fnsave (%esp)
    frstor (%esp)
    pushl _es2_machine_x86_anchor
    pushl %ebp
    movl %esp, _es2_machine_x86_anchor
    pushl _es2_machine_x86_slots
    pushl _es2_machine_x86_floor
    incl _es2_machine_x86_depth
    movl 8(%ebp), %edi
    movl 16(%edi), %eax
    testl %eax, %eax
    jz 1f
    /* ESP moves: the host frame ends here, the floor goes down to it, when
     * the frame lies on the thread's stack (TIB StackLimit, StackBase); the
     * entry shims' frames on their own stack (entry_shims_x86.h) never set
     * it. */
    cmpl %fs:8, %esp
    jb 6f
    cmpl %fs:4, %esp
    jae 6f
    movl _es2_machine_x86_floor, %ecx
    testl %ecx, %ecx
    jz 5f
    cmpl %ecx, %esp
    jae 6f
5:
    movl %esp, _es2_machine_x86_floor
6:
    movl %eax, %esp
1:
    movl 20(%ebp), %ecx
    movl 16(%ebp), %esi
2:
    testl %ecx, %ecx
    jz 3f
    decl %ecx
    pushl (%esi,%ecx,4)
    jmp 2b
3:
    movl %esp, _es2_machine_x86_slots
    /* The plants at entry ESP (ESP - 4 after the CALL) - offset, in order. */
    movl 28(%ebp), %ecx
    movl 24(%ebp), %esi
    testl %ecx, %ecx
    jz 8f
7:
    movl (%esi), %eax
    negl %eax
    movb 4(%esi), %dl
    movb %dl, -4(%esp,%eax)
    addl $8, %esi
    decl %ecx
    jnz 7b
8:
    movl 32(%ebp), %eax
    testl %eax, %eax
    jz 9f
    leal -4(%esp), %ecx
    movl %ecx, (%eax)
9:
    /* CALL without a free register: the target through a static, so
     * nothing is written below the return address. */
    movl 12(%ebp), %eax
    movl %eax, _es2_machine_x86_target
    pushl $4f
    movl 0(%edi), %eax
    movl 4(%edi), %ecx
    movl 8(%edi), %edx
    movl 12(%edi), %ebx
    movl 20(%edi), %ebp
    movl 24(%edi), %esi
    movl 28(%edi), %edi
    jmp *_es2_machine_x86_target
4:
    movl %ebp, _es2_machine_x86_ebp
    movl _es2_machine_x86_anchor, %ebp
    movl (%ebp), %ebp
    leal -136(%ebp), %esp
    fnsave 16(%esp)
    frstor 16(%esp)
    popl _es2_machine_x86_floor
    pushl %edi
    movl 8(%ebp), %edi
    movl %eax, 0(%edi)
    movl %ecx, 4(%edi)
    movl %edx, 8(%edi)
    movl %ebx, 12(%edi)
    movl _es2_machine_x86_ebp, %eax
    movl %eax, 20(%edi)
    movl %esi, 24(%edi)
    popl %eax
    movl %eax, 28(%edi)
    popl _es2_machine_x86_slots
    decl _es2_machine_x86_depth
    movl 4(%esp), %eax
    movl %eax, _es2_machine_x86_anchor
    leal -12(%ebp), %esp
    popl %edi
    popl %esi
    popl %ebx
    popl %ebp
    ret

.data
.align 4
.globl _es2_machine_x86_slots
_es2_machine_x86_slots:
    .long 0
.globl _es2_machine_x86_depth
_es2_machine_x86_depth:
    .long 0
.globl _es2_machine_x86_floor
_es2_machine_x86_floor:
    .long 0
_es2_machine_x86_anchor:
    .long 0
_es2_machine_x86_ebp:
    .long 0
_es2_machine_x86_target:
    .long 0
