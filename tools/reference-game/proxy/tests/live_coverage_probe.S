.text
.globl _lc_probe
_lc_probe:
    pushfl
    pushal
    subl $528,%esp
    movl %esp,%eax
    addl $15,%eax
    andl $-16,%eax
    fxsave (%eax)
    pushl %eax
    fninit
    fld1
    fld1
    fld1
    fld1
    fld1
    fld1
    fld1
    fld1
    fninit
    fld1
    fldpi
    pcmpeqd %xmm0,%xmm0
    movdqa %xmm0,%xmm1
    movdqa %xmm0,%xmm2
    movdqa %xmm0,%xmm3
    movdqa %xmm0,%xmm4
    movdqa %xmm0,%xmm5
    movdqa %xmm0,%xmm6
    movdqa %xmm0,%xmm7
    pslld $1,%xmm1
    pslld $2,%xmm2
    pslld $3,%xmm3
    pslld $4,%xmm4
    pslld $5,%xmm5
    pslld $6,%xmm6
    pslld $7,%xmm7
    pushl _lc_probe_flags
    popfl
    movl $0x12345678,%eax
    movl $0x23456789,%ebx
    movl $0x3456789a,%ecx
    movl $0x456789ab,%edx
    movl $0x56789abc,%esi
    movl $0x6789abcd,%edi
    movl $0x789abcde,%ebp
    call *_lc_probe_target
    pushfl
    pushal
    movl %esp,%esi
    movl $_lc_probe_result,%edi
    movl $9,%ecx
    cld
    rep movsl
    fxsave _lc_probe_fpu
    addl $36,%esp
    popl %eax
    fxrstor (%eax)
    addl $528,%esp
    popal
    popfl
    ret
