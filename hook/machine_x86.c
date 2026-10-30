/* The x86 backend of the machine interface (es2/hook/machine.h): the original
 * code runs, through machine_x86.S. */
#include <stddef.h>
#include <windows.h>
#include "es2/hook/machine.h"
#include "es2/hook/machine_x86.h"

_Static_assert(offsetof(es2_machine, esp) == 16 && offsetof(es2_machine, edi) == 28, "machine_x86.S layout");
_Static_assert(offsetof(es2_machine_plant, byte) == 4 && sizeof(es2_machine_plant) == 8, "machine_x86.S plant layout");

int es2_machine_call(es2_machine *m, uint32_t va, es2_machine_abi abi, const uint32_t *args, unsigned count) {
    return es2_machine_x86_call(m, va, abi, args, count);
}

int es2_machine_call_planted(es2_machine *m, uint32_t va, es2_machine_abi abi, const uint32_t *args, unsigned count,
                             const es2_machine_plant *plants, unsigned plant_count, uint32_t *entry) {
    return es2_machine_x86_call_planted(m, va, abi, args, count, plants, plant_count, entry);
}

void es2_machine_stack_overflow(void) { RaiseException(EXCEPTION_STACK_OVERFLOW, EXCEPTION_NONCONTINUABLE, 0, NULL); }

uint32_t es2_machine_fs0(void) { return es2_machine_x86_fs0(); }
void es2_machine_set_fs0(uint32_t head) { es2_machine_x86_set_fs0(head); }

int es2_machine_probe(uint32_t va, uint32_t top, uint32_t bytes) {
    (void)va;
    for (uint32_t k = 4096u; k <= bytes; k += 4096u) {
        volatile uint32_t *p = (volatile uint32_t *)(uintptr_t)(top - k);
        *p |= 0u;
    }
    return 0;
}

uint32_t es2_machine_floor(uint32_t esp) {
    uint32_t floor = es2_machine_x86_floor;
    return floor && floor < esp && floor >= es2_machine_x86_stack_limit() ? floor : esp;
}

uint64_t es2_machine_idiv(uint32_t dividend, uint32_t divisor) {
    uint32_t eax = dividend, edx;
    __asm__ volatile("cltd\n\tidivl %2" : "+a"(eax), "=&d"(edx) : "r"(divisor) : "cc");
    return (uint64_t)edx << 32 | eax;
}
