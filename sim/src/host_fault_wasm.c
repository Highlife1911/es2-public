/* Host faults in the web build (es2/sim/host_fault.h): wasm raises no
 * exception record. A NULL-page access goes through the checked accessor,
 * a divide through the explicit checks, every other fault is a RuntimeError
 * (es2_wasm_trap_exit, docs/web_build.md section 6). */
#include "es2/sim/host_fault.h"

static es2_sim_host_fault_fn fault_callback;

int es2_sim_host_fault_owned(unsigned long code, const void *address) {
    (void)code;
    (void)address;
    return 0;
}

int es2_sim_host_fault_null_page(const void *record, unsigned *address) {
    (void)record;
    (void)address;
    return 0;
}

int es2_sim_host_fault_divide(const void *record) {
    (void)record;
    return 0;
}

int es2_sim_host_fault_install(es2_sim_host_fault_fn on_fault) {
    if (fault_callback || !on_fault) return 1;
    fault_callback = on_fault;
    return 0;
}

void es2_sim_host_fault_remove(void) { fault_callback = 0; }
