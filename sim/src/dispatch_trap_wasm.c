/* The stage-1 trap in the web build (es2/sim/trap.h): wasm executes no guest
 * instruction, so no call can reach an original VA and there is nothing to
 * trap. The install checks stay those of the Windows trap; DEP has no
 * flags. */
#include "es2/sim/trap.h"

static const es2_sim_dispatch *trap_dispatch;

int es2_sim_enable_dep(unsigned long *flags, int *permanent) {
    if (flags) *flags = 0;
    if (permanent) *permanent = 0;
    return 0;
}

int es2_sim_trap_install(es2_sim_dispatch *d, const es2_sim_trap_range *ranges, unsigned count) {
    if (trap_dispatch || !d || !d->sealed || !count || count > ES2_SIM_TRAP_RANGES) return 1;
    for (unsigned i = 0; i < count; ++i)
        if (ranges[i].lo >= ranges[i].hi || (ranges[i].keys && !ranges[i].stride)) return 1;
    trap_dispatch = d;
    return 0;
}

void es2_sim_trap_remove(void) { trap_dispatch = 0; }

uint64_t es2_sim_trap_calls(void) { return 0; }
