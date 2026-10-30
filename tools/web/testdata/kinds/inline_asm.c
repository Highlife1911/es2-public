/* Inline assembler: lines with an EXPECT marker must report exactly those kinds, all others none. */
void basic(void) { __asm__ volatile("nop"); }                      /* EXPECT: inline-asm */
void extended(int v) { __asm__("addl %1, %0" : "+r"(v) : "r"(v)); } /* EXPECT: inline-asm */
void plain_keyword(void) { asm("nop"); }                           /* EXPECT: inline-asm */
void two_lines(void) {
    __asm__ volatile(                                              /* EXPECT: inline-asm */
        "nop\n\t"
        "nop");
}
int renamed(void) __asm__("_renamed");                             /* EXPECT: asm-label */
/* __asm__("nop") in a comment is not a statement. */
const char *quoted = "__asm__(\"nop\")";
int asm_count;
int use(int asm_value) { return asm_value + asm_count; }
