#ifndef ES2_ADAPTER_HOST_H
#define ES2_ADAPTER_HOST_H
/* What the adapters ask of the process they run in.
 *
 * The proxy: the x86 framework's runtime of an installed hook (hook/hook.h),
 * recorded by its group after the install (assets_hooks.h, vfs_hooks.h read
 * its mode and count skipped and bypassed calls), and the comparators'
 * probes of the real process: committed pages (VirtualQuery) and the
 * thread's stack (TIB StackLimit..StackBase).
 *
 * es2sim (ES2_SIM_HOST) installs no hook and runs no comparator: there is no
 * runtime, every call commits and a PASS calls the original's VA; the
 * readability probe is es2_guest_probe alone (docs/web_build.md section 5)
 * and the stack is the guest stack of the sim backend. */
#include <stdint.h>
#include "es2/hook/descriptor.h"
#ifndef ES2_SIM_HOST
#include "hook/hook.h"

/* `h` while it is installed at image + rva (entry patched), else NULL. */
static inline es2_hook *adapter_hook_installed(es2_hook *h, void *image, uint32_t rva) {
    const BYTE *site = (const BYTE *)image + rva;
    return h && h->installed == 1 && h->site == site && site[0] == 0xe9u ? h : NULL;
}
static inline int adapter_hook_compares(const es2_hook *h) { return h && h->options.mode == ES2_HOOK_COMPARE; }
static inline void adapter_hook_skipped(es2_hook *h) { if (h) ++h->skipped; }
/* A PASS: counts it and returns the original's entry (the trampoline of an
 * installed hook). */
static inline uint32_t adapter_hook_pass(es2_hook *h, void *image, uint32_t rva) {
    if (h) ++h->bypassed;
    return (uint32_t)(uintptr_t)(h ? h->trampoline : (BYTE *)image + rva);
}
/* Every page of [va, va + n) committed and neither no-access nor guard. */
static inline int adapter_committed(uint32_t va, uint32_t n) {
    uintptr_t at = va;
    const uintptr_t end = (uintptr_t)va + n;
    while (at < end) {
        MEMORY_BASIC_INFORMATION m;
        if (!VirtualQuery((void *)at, &m, sizeof(m)) || m.State != MEM_COMMIT || (m.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
            return 0;
        at = (uintptr_t)m.BaseAddress + m.RegionSize;
    }
    return 1;
}
/* `value` lies in the committed stack of the current thread. */
static inline int adapter_stack_address(uint32_t value) {
    const NT_TIB *t = (const NT_TIB *)NtCurrentTeb();
    return value >= (uint32_t)(uintptr_t)t->StackLimit && value < (uint32_t)(uintptr_t)t->StackBase;
}
#else
#include "es2/sim/machine_sim.h"

static inline es2_hook *adapter_hook_installed(es2_hook *h, void *image, uint32_t rva) {
    (void)h; (void)image; (void)rva;
    return NULL;
}
static inline int adapter_hook_compares(const es2_hook *h) { (void)h; return 0; }
static inline void adapter_hook_skipped(es2_hook *h) { (void)h; }
static inline uint32_t adapter_hook_pass(es2_hook *h, void *image, uint32_t rva) {
    (void)h;
    return (uint32_t)(uintptr_t)image + rva;
}
static inline int adapter_committed(uint32_t va, uint32_t n) { (void)va; (void)n; return 1; }
static inline int adapter_stack_address(uint32_t value) {
    const es2_machine_stack *s = es2_machine_sim_stack();
    return value >= s->limit && value < s->base;
}
#endif
#endif
