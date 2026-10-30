/* The divide instructions of es2_sim_divide_fault (cpu_fault.c): cdecl
 * (low, high, divisor), EDX:EAX = high:low divided by the divisor with IDIV
 * or DIV, the result in EDX:EAX (remainder:quotient). The *_site labels
 * mark the instruction that faults, the *_resume labels the instruction
 * after it, where cpu_fault.c's handler continues. */
.text
.globl _es2_sim_cpu_idiv
.globl _es2_sim_cpu_idiv_site
.globl _es2_sim_cpu_idiv_resume
_es2_sim_cpu_idiv:
    movl 4(%esp), %eax
    movl 8(%esp), %edx
_es2_sim_cpu_idiv_site:
    idivl 12(%esp)
_es2_sim_cpu_idiv_resume:
    ret

.globl _es2_sim_cpu_div
.globl _es2_sim_cpu_div_site
.globl _es2_sim_cpu_div_resume
_es2_sim_cpu_div:
    movl 4(%esp), %eax
    movl 8(%esp), %edx
_es2_sim_cpu_div_site:
    divl 12(%esp)
_es2_sim_cpu_div_resume:
    ret
