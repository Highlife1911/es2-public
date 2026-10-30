/* Own synthetic native-ABI loop; identical caller for both installed paths. */
.text
.globl _es2_hook_bench_run
_es2_hook_bench_run:
    pushl %ebp
    movl %esp,%ebp
    pushl %ebx
    pushl %esi
    pushl %edi
    movl 8(%ebp),%ebx
    movl 20(%ebp),%edi
1:
    movl 16(%ebp),%ecx
    cmpl $2,12(%ebp)
    je 3f
2:
    testl %ecx,%ecx
    jz 5f
    pushl %ecx
    decl %ecx
    jmp 2b
3:
    movl $3,%eax
4:
    cmpl %ecx,%eax
    jae 5f
    incl %eax
    pushl %eax
    jmp 4b
5:
    movl $1,%eax
    movl $2,%edx
    movl $3,%ecx
    call *%ebx
    leal -12(%ebp),%esp
    decl %edi
    jnz 1b
    popl %edi
    popl %esi
    popl %ebx
    popl %ebp
    ret
