/* Seams of the proxy's adapter headers: bridge macro, machine call, FPU save, shim floor, stack probe, divide. */
void entry(unsigned char *fpu) {
    ES2_SIM_BRIDGE_ARG8                                            /* EXPECT: bridge-seam */
    es2_hook_machine_call(&call);                                  /* EXPECT: machine-call */
    es2_hook_fpu_save(fpu);                                        /* EXPECT: x87 */
}
void floor_check(void) { __asm__ volatile("movl $1, %eax" ES2_SHIM_FLOOR); } /* EXPECT: inline-asm,shim-floor */
void probe(void) { __asm__ volatile("subl $4096, %esp\n\torl $0, (%esp)"); } /* EXPECT: inline-asm,esp,stack-probe */
int divide(int a, int b) { int q; __asm__("cltd\n\tidivl %2" : "=a"(q) : "a"(a), "r"(b) : "edx"); return q; } /* EXPECT: inline-asm,idiv */
void fpu_init(unsigned short cw) { __asm__ volatile("fninit; fldcw %0" : : "m"(cw)); } /* EXPECT: inline-asm,x87 */
/* ES2_SIM_BRIDGE, es2_hook_machine_call, ES2_SHIM_FLOOR and orl $0,(%esp) in a comment */
