/* The real FS:0 head for the x86 backend of es2/hook/machine.h
 * (hook/machine_x86.c) and for the test harnesses that run original code next
 * to the sim backend. */
#ifndef ES2_HOOK_MACHINE_X86_H
#define ES2_HOOK_MACHINE_X86_H
#include <stdint.h>

static inline uint32_t es2_machine_x86_fs0(void) {
    uint32_t head;
    __asm__ volatile("movl %%fs:0, %0" : "=r"(head));
    return head;
}
static inline void es2_machine_x86_set_fs0(uint32_t head) {
    __asm__ volatile("movl %0, %%fs:0" : : "r"(head) : "memory");
}
/* TIB StackLimit (FS:8), the lowest committed byte of the thread's stack. */
static inline uint32_t es2_machine_x86_stack_limit(void) {
    uint32_t limit;
    __asm__ volatile("movl %%fs:8, %0" : "=r"(limit));
    return limit;
}

#endif
