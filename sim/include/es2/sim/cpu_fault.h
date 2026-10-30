/* D: the CPU's divide fault as the original's IDIV or DIV raises it
 * (own_process.md "The original's fault paths"). */
#ifndef ES2_SIM_CPU_FAULT_H
#define ES2_SIM_CPU_FAULT_H
#include <stdint.h>

/* Runs IDIV (is_signed) or DIV of EDX:EAX = dividend by the divisor, the
 * original's instruction on its operands, and returns the exception code
 * the OS raised for it (STATUS_INTEGER_DIVIDE_BY_ZERO 0xc0000094 or
 * STATUS_INTEGER_OVERFLOW 0xc0000095, the OS's choice), or 0 when the
 * division does not fault. A vectored handler of its own, first in the
 * list for the call's duration, steps over the instruction: no frame
 * record and no other handler sees the exception. Meant for the fault
 * paths only: every call adds and removes the handler. es2sim's loop uses it
 * on Windows; the portable link uses es2_divide (es2/core/divide.h). */
uint32_t es2_sim_divide_fault(uint64_t dividend, uint32_t divisor, int is_signed);
/* The same, with EDX:EAX after the instruction in *edx_eax (may be NULL):
 * remainder:quotient, or 0 after a fault. The oracle of es2_divide
 * (es2/core/divide.h) in //sim:fault_paths_test. */
uint32_t es2_sim_divide_cpu(uint64_t dividend, uint32_t divisor, int is_signed, uint64_t *edx_eax);

#endif
