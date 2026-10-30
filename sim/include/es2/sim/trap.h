/* The stage-1 trap, a guard only (own_process.md section 4.4, web_build.md
 * section 4.8): the guest's CODE is mapped readable and not executable, and
 * every call reaches its entry through the sim backend (es2_sim_call,
 * es2_machine_call). A call site that still jumps to an original VA faults; a
 * vectored exception handler (first in the chain) takes the execute fault,
 * counts it (es2_sim_trap_calls, the entry's `traps`), calls the dispatch's
 * refusal callback with the reason "unconverted call", the VA (an import-slot
 * trap: the slot's VA), the return address and the call's ESP, and ends the
 * process with ES2_SIM_EXIT_REFUSED. No original instruction executes and the
 * thread never continues.
 *
 * Requirements: DEP on for the process with ATL thunk emulation off (Windows
 * would otherwise emulate some byte patterns of non-executable pages); one
 * simulation thread (the thread that installs the trap; faults on other
 * threads are not handled). Windows x86 only. */
#ifndef ES2_SIM_TRAP_H
#define ES2_SIM_TRAP_H
#include <stdint.h>
#include "es2/sim/dispatch.h"

/* A trapped address range [lo, hi). With `keys`, the dispatch key of the
 * address lo + stride * i is keys[i] (the import-slot traps: one trap address
 * per IAT slot, keyed by the slot's VA); without, the key is the address. */
typedef struct es2_sim_trap_range {
    uint32_t lo, hi;
    const uint32_t *keys;
    uint32_t stride;
} es2_sim_trap_range;

#define ES2_SIM_TRAP_RANGES 4u

/* Turns DEP on for the process with ATL thunk emulation off (or confirms it
 * is on permanently). 0 on success. */
int es2_sim_enable_dep(unsigned long *flags, int *permanent);

/* Installs the handler for calls from the current thread into `ranges`,
 * refused through `d` (sealed). 0 or 1 (already installed, bad ranges). */
int es2_sim_trap_install(es2_sim_dispatch *d, const es2_sim_trap_range *ranges, unsigned count);
void es2_sim_trap_remove(void);
/* Trapped calls so far. */
uint64_t es2_sim_trap_calls(void);

#endif
