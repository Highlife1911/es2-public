/* Naked functions; the asm inside one is a hit of its own, flagged in_naked. */
__attribute__((naked)) void shim(void) {                           /* EXPECT: naked */
    __asm__ volatile("ret");                                       /* EXPECT: inline-asm */
}
__attribute__((naked, noinline)) int shim2(void) { __asm__("ret"); } /* EXPECT: naked,inline-asm */
__attribute__((noinline)) void normal(void) { }
int naked;
int naked_count;
/* __attribute__((naked)) in a comment */
