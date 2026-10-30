/* Hardware faults of es2sim's own code; see es2/sim/host_fault.h. */
#include "es2/sim/host_fault.h"
#include <windows.h>
#include <stdint.h>

static PVOID fault_handle;
static es2_sim_host_fault_fn fault_callback;
static uintptr_t module_lo, module_hi;

int es2_sim_host_fault_owned(unsigned long code, const void *address) {
    uintptr_t a = (uintptr_t)address;
    return fault_handle && (code & 0xc0000000ul) == 0xc0000000ul && a >= module_lo && a < module_hi;
}

int es2_sim_host_fault_null_page(const void *record, unsigned *address) {
    const EXCEPTION_RECORD *r = record;
    if (!r || r->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || r->NumberParameters < 2 ||
        r->ExceptionInformation[0] > 1u || r->ExceptionInformation[1] >= ES2_SIM_NULL_PAGE_END)
        return 0;
    if (address) *address = (unsigned)r->ExceptionInformation[1];
    return 1;
}

int es2_sim_host_fault_divide(const void *record) {
    const EXCEPTION_RECORD *r = record;
    return r && (r->ExceptionCode == EXCEPTION_INT_DIVIDE_BY_ZERO || r->ExceptionCode == EXCEPTION_INT_OVERFLOW);
}

static LONG CALLBACK fault_handler(EXCEPTION_POINTERS *x) {
    const EXCEPTION_RECORD *r = x->ExceptionRecord;
    if (!es2_sim_host_fault_owned(r->ExceptionCode, r->ExceptionAddress) || !fault_callback) return EXCEPTION_CONTINUE_SEARCH;
    fault_callback(x);
    return EXCEPTION_CONTINUE_SEARCH;
}

int es2_sim_host_fault_install(es2_sim_host_fault_fn on_fault) {
    HMODULE module = NULL;
    if (fault_handle || !on_fault ||
        !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCWSTR)(uintptr_t)fault_handler, &module))
        return 1;
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)module;
    const IMAGE_NT_HEADERS *nt = (const IMAGE_NT_HEADERS *)((const uint8_t *)module + dos->e_lfanew);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || nt->Signature != IMAGE_NT_SIGNATURE) return 1;
    module_lo = (uintptr_t)module;
    module_hi = module_lo + nt->OptionalHeader.SizeOfImage;
    fault_callback = on_fault;
    /* Last in the list: the trap's execute faults on CODE and the other first
     * handlers are answered before this one runs. */
    fault_handle = AddVectoredExceptionHandler(0, fault_handler);
    return fault_handle ? 0 : 1;
}

void es2_sim_host_fault_remove(void) {
    if (fault_handle) RemoveVectoredExceptionHandler(fault_handle);
    fault_handle = NULL;
    fault_callback = NULL;
    module_lo = module_hi = 0;
}
