#ifndef ES2_CORE_DIVIDE_H
#define ES2_CORE_DIVIDE_H
#include <stddef.h>
#include <stdint.h>

/* The x86 integer divide in C: IDIV (is_signed) or DIV r/m32 of EDX:EAX =
 * dividend by the divisor, and the exception Windows raises for it. */
#define ES2_DIVIDE_BY_ZERO 0xc0000094u   /* STATUS_INTEGER_DIVIDE_BY_ZERO: a zero divisor */
#define ES2_DIVIDE_OVERFLOW 0xc0000095u  /* STATUS_INTEGER_OVERFLOW: the quotient does not fit 32 bits */

/* 0 with the quotient (EAX) and remainder (EDX), truncated toward zero and
 * the remainder with the dividend's sign, as the instruction leaves them;
 * otherwise the fault's code with both 0. Either pointer may be NULL. Equal
 * to the CPU's instruction on every operand pair (sim/src/cpu_fault.S, the
 * oracle of //sim:fault_paths_test). */
uint32_t es2_divide(uint64_t dividend, uint32_t divisor, int is_signed, uint32_t *quotient, uint32_t *remainder);

/* Raises the fault `code` (ES2_DIVIDE_BY_ZERO or ES2_DIVIDE_OVERFLOW) with a
 * host divide of its own: on x86 the CPU's #DE with that code, in wasm the
 * trap "integer divide by zero" or "integer overflow" (docs/web_build.md
 * section 6). Any other code returns. */
void es2_divide_raise(uint32_t code);

/* es2_divide, and on a fault es2_divide_raise: the explicit check before a
 * host divide whose fault is the original's. Returns the quotient (0 when
 * the raise returns). */
uint32_t es2_divide_or_raise(uint64_t dividend, uint32_t divisor, int is_signed, uint32_t *remainder);
#endif
