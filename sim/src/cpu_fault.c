/* D: the CPU's divide fault on the original's operands
 * (es2/sim/cpu_fault.h, own_process.md "The original's fault paths"). */
#include "es2/sim/cpu_fault.h"

#include <windows.h>

uint64_t es2_sim_cpu_idiv(uint32_t low, uint32_t high, uint32_t divisor);
uint64_t es2_sim_cpu_div(uint32_t low, uint32_t high, uint32_t divisor);
extern const char es2_sim_cpu_idiv_site[], es2_sim_cpu_idiv_resume[], es2_sim_cpu_div_site[], es2_sim_cpu_div_resume[];

static DWORD fault_thread;
static volatile uint32_t fault_code;

/* A fault at one of the two sites on the dividing thread continues after
 * the instruction with EDX:EAX = 0 and its code kept; anything else goes on
 * to the next handler. */
static LONG CALLBACK step_over(EXCEPTION_POINTERS *x) {
    if (GetCurrentThreadId() != fault_thread) return EXCEPTION_CONTINUE_SEARCH;
    const void *at = x->ExceptionRecord->ExceptionAddress;
    CONTEXT *c = x->ContextRecord;
    if (at == es2_sim_cpu_idiv_site) c->Eip = (DWORD)(uintptr_t)es2_sim_cpu_idiv_resume;
    else if (at == es2_sim_cpu_div_site) c->Eip = (DWORD)(uintptr_t)es2_sim_cpu_div_resume;
    else return EXCEPTION_CONTINUE_SEARCH;
    fault_code = (uint32_t)x->ExceptionRecord->ExceptionCode;
    c->Eax = 0;
    c->Edx = 0;
    return EXCEPTION_CONTINUE_EXECUTION;
}

uint32_t es2_sim_divide_fault(uint64_t dividend, uint32_t divisor, int is_signed) {
    PVOID handler = AddVectoredExceptionHandler(1, step_over);
    if (!handler) return 0;
    fault_thread = GetCurrentThreadId();
    fault_code = 0;
    uint32_t low = (uint32_t)dividend, high = (uint32_t)(dividend >> 32);
    if (is_signed) (void)es2_sim_cpu_idiv(low, high, divisor);
    else (void)es2_sim_cpu_div(low, high, divisor);
    RemoveVectoredExceptionHandler(handler);
    fault_thread = 0;
    return fault_code;
}
