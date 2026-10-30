/* Own x86 candidate counter increment; test-only until measured. */
#ifndef ES2_COUNTER_CANDIDATE_H
#define ES2_COUNTER_CANDIDATE_H
#include <stdint.h>
static inline void counter_candidate(volatile uint64_t *value) {
    /* Declare the entire counter read/write. Only the low dword is normally
     * stored; the high dword changes on carry. The original gate is serial. */
    __asm__ volatile("addl $1,(%1); jnc 1f; addl $1,4(%1); 1:"
                     : "+m"(*value) : "r"(value) : "cc");
}
#endif
