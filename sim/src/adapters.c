/* The proxy's hook adapters compiled for the own process (es2/sim/adapters.h).
 * The table below is generated at compile time from the registry X-macro, as
 * hook_registry_ids.h is; es2_sim_dispatch_seal sorts it with the services
 * and refuses a VA listed twice. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* What the adapter headers expect from their includer (as in the harness):
 * windows.h's BYTE, BOOL, TRUE, FALSE and __cdecl, and the event log. */
typedef uint8_t BYTE;
typedef int BOOL;
#define TRUE 1
#define FALSE 0
#ifndef _WIN32
#define __cdecl
#endif
static void log_event(const char *event) { (void)event; }
#include "tools/reference-game/proxy/rng_hooks.h"
#include "es2/sim/adapters.h"

typedef struct adapter_row {
    uint32_t rva;
    const char *name;
    const es2_hook_descriptor *descriptor;
    const es2_hook_extension *extension;
} adapter_row;

static const adapter_row adapter_rows[] = {
#define ES2_HOOK(name, rva, descriptor, extension, group) {rva, #name, &(descriptor), extension},
#include "tools/reference-game/proxy/hook_registry.h"
#undef ES2_HOOK
};
_Static_assert(sizeof(adapter_rows) / sizeof(adapter_rows[0]) == ES2_HOOK_COUNT, "one row per registry hook");

/* Registry hooks whose VA a service row already serves (the MFD keys
 * 0x4469e8, the entry steps 0x44703c/0x447070, the order key 0x447158 and the
 * HDD button executor 0x44a1a0 of sim/src/mfd.c, the Alt order 0x446c38 of
 * sim/src/panel.c). es2_sim_dispatch_seal refuses a VA listed twice, so these
 * adapters are not registered here and the service rows stay. */
static const uint32_t served_rvas[] = {0x469e8u, 0x46c38u, 0x47158u, 0x4703cu, 0x47070u, 0x4a1a0u};
static int served(uint32_t rva) {
    for (unsigned i = 0; i < sizeof(served_rvas) / sizeof(served_rvas[0]); ++i)
        if (served_rvas[i] == rva) return 1;
    return 0;
}

unsigned es2_sim_adapter_count(void) {
    return (unsigned)ES2_HOOK_COUNT - (unsigned)(sizeof(served_rvas) / sizeof(served_rvas[0]));
}

void es2_sim_adapters_set_user(void *image) {
    function_hooks_set_user(image);
}

/* The proxy's entry shims (own_process.md section 4.4, item 3): a hook with
 * a shim of entry_shims.h gets its C entry, which the dispatch calls on the
 * guest stack (machine_sim.c). */
int es2_sim_register_adapters(es2_sim_dispatch *d, uint32_t image_base) {
    for (unsigned i = 0; i < (unsigned)ES2_HOOK_COUNT; ++i) {
        if (served(adapter_rows[i].rva)) continue;
        if (es2_sim_dispatch_add_hook(d, image_base + adapter_rows[i].rva, adapter_rows[i].name,
                                      adapter_rows[i].descriptor, adapter_rows[i].extension)) return 1;
        const es2_shim *s = adapter_shim(adapter_rows[i].rva);
        if (s) d->entries[d->count - 1u].shim = s->entry;
    }
    return 0;
}

unsigned es2_sim_adapter_shims(const es2_sim_dispatch *d) {
    unsigned n = 0;
    for (unsigned i = 0; i < d->count; ++i) n += d->entries[i].kind == ES2_SIM_HOOK && d->entries[i].shim;
    return n;
}

unsigned es2_sim_adapter_shim_count(void) {
    unsigned n = 0;
    for (unsigned i = 0; i < ADAPTER_SHIM_COUNT; ++i) n += !served(adapter_shims[i]->rva);
    return n;
}
