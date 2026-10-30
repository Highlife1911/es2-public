/* EXPECT: asm-source */
    .text
_probe:
    movl %fs:0, %eax        /* EXPECT: fs-read */
    movl %eax, %fs:0        /* EXPECT: fs-write */
    call *%eax              /* EXPECT: call-indirect */
    movl %esp, %ebp         /* EXPECT: esp */
    orl $0, (%esp)          /* EXPECT: esp,stack-probe */
    ret
