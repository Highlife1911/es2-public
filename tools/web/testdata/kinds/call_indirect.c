/* `call *` and `jmp *` inside asm strings. */
void go_register(void) { __asm__ volatile("call *%eax"); }          /* EXPECT: inline-asm,call-indirect */
void go_symbol(void) { __asm__ volatile("movl $1, %ecx\n\tjmp *_target"); } /* EXPECT: inline-asm,call-indirect */
void go_direct(void) { __asm__ volatile("call _plain\n\tjmp 1f\n1:"); } /* EXPECT: inline-asm */
const char *text = "call *%eax";
