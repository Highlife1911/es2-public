/* The registry's hook adapters compiled into the own process (own_process.md
 * sections 1 and 4.3): every ES2_HOOK row of
 * tools/reference-game/proxy/hook_registry.h becomes a dispatch entry of kind
 * hook at image base + RVA, with the adapter's descriptor and extension. The
 * adapters run their direct path (the one the in-vitro "direct" cases
 * validate); no patching. A hook the proxy enters
 * through an entry shim gets the shim's C entry (es2_sim_entry.shim). */
#ifndef ES2_SIM_ADAPTERS_H
#define ES2_SIM_ADAPTERS_H
#include <stdint.h>
#include "es2/sim/dispatch.h"

unsigned es2_sim_adapter_count(void);
/* Gives every mutable descriptor the image base as user pointer (as the
 * proxy and the harness do). */
void es2_sim_adapters_set_user(void *image);
/* Adds one hook entry per registry row at image_base + RVA, with the row's
 * entry shim (entry_shims.h). 0 or 1 (full). */
int es2_sim_register_adapters(es2_sim_dispatch *d, uint32_t image_base);
/* The hook entries of d with an entry shim. */
unsigned es2_sim_adapter_shims(const es2_sim_dispatch *d);
/* The entry shims of entry_shims.h whose hook es2_sim_register_adapters
 * registers (not those of the served VAs). */
unsigned es2_sim_adapter_shim_count(void);

#endif
