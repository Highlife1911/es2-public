/* Test-only copy of the unchanged own entry stub, directed to the baseline. */
.text
.globl _baseline_entry_extended
_baseline_entry_extended:
    pushfl
    pushal
    subl $108, %esp
    fnsave (%esp)
    frstor (%esp)
    movl %esp, %eax
    pushl %eax
    call _baseline_dispatch_extended
    addl $4, %esp
    frstor (%esp)
    addl $108, %esp
    popal
    popfl
    ret
