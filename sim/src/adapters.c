/* The proxy's hook adapters compiled for the own process (es2/sim/adapters.h).
 * The table below is generated at compile time from the registry X-macro, as
 * hook_registry_ids.h is; es2_sim_dispatch_seal sorts it with the services
 * and refuses a VA listed twice. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* What the adapter headers expect from their includer (as in the harness):
 * the proxy INI path (unused: no adapter is installed) and the event log. */
static WCHAR config_path[MAX_PATH];
static void log_event(const char *event) { (void)event; }
#include "tools/reference-game/proxy/rng_hooks.h"
#include "es2/sim/adapters.h"
#include "es2/sim/trap.h"

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
    /* Referenced so that the proxy-only entry points of the headers stay
     * compiled without warnings; never called here. */
    (void)install_rng_hooks;
}

/* The proxy's entry shims (own_process.md section 4.4, item 3). The proxy
 * installs every registry hook through weapons_install_hook, whose capture
 * chain (weapons_capture_entry and the groups it hands on to) turns the
 * framework's jump at the hook's site to the group's shim and keeps the jump's
 * old target as the shim's bridge. Here the chain runs once per row on a
 * stand-in hook whose site is a buffer holding a jump to es2_sim_trap_entry:
 * every bridge becomes the trap's trampoline, and the buffer's jump target
 * after the chain names the row's shim (the trampoline itself: none). Two
 * groups only record the hook for their runtime calls (assets, vfs); those
 * records are cleared again, as no hook is installed. 0, or 1 when the chain
 * refuses a row. */
static int entry_shim(BYTE *site, const es2_hook_descriptor *dsc, void **shim) {
    static es2_hook h;
    size_t n = dsc->relocate_size;
    *shim = NULL;
    if (n < 5u || n > ES2_HOOK_MAX_PROLOGUE) return 1;
    memset(&h, 0, sizeof(h));
    h.descriptor = dsc;
    h.installed = 1;
    h.site = site;
    memset(h.patch, 0x90, sizeof(h.patch));
    uint32_t trampoline = (uint32_t)(uintptr_t)es2_sim_trap_entry;
    uint32_t displacement = trampoline - (uint32_t)(uintptr_t)(site + 5);
    h.patch[0] = 0xe9;
    memcpy(h.patch + 1, &displacement, 4);
    memcpy(site, h.patch, n);
    if (!weapons_capture_entry(&h)) return 1;
    memcpy(&displacement, site + 1, 4);
    uint32_t target = (uint32_t)(uintptr_t)(site + 5) + displacement;
    if (site[0] != 0xe9) return 1;
    if (target != trampoline) *shim = (void *)(uintptr_t)target;
    return 0;
}

int es2_sim_register_adapters(es2_sim_dispatch *d, uint32_t image_base) {
    BYTE *site = VirtualAlloc(NULL, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!site) return 1;
    int bad = 0;
    for (unsigned i = 0; i < (unsigned)ES2_HOOK_COUNT && !bad; ++i) {
        void *shim = NULL;
        if (served(adapter_rows[i].rva)) continue;
        if (es2_sim_dispatch_add_hook(d, image_base + adapter_rows[i].rva, adapter_rows[i].name,
                                      adapter_rows[i].descriptor, adapter_rows[i].extension) ||
            entry_shim(site, adapter_rows[i].descriptor, &shim)) bad = 1;
        else d->entries[d->count - 1u].entry_shim = shim;
    }
    memset(assets_runtime, 0, sizeof(assets_runtime));
    memset(vfs_runtime, 0, sizeof(vfs_runtime));
    VirtualFree(site, 0, MEM_RELEASE);
    return bad;
}

unsigned es2_sim_adapter_shims(const es2_sim_dispatch *d) {
    unsigned n = 0;
    for (unsigned i = 0; i < d->count; ++i) n += d->entries[i].kind == ES2_SIM_HOOK && d->entries[i].entry_shim;
    return n;
}
