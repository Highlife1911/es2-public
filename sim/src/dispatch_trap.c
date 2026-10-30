/* The stage-1 trap as a guard: see es2/sim/trap.h. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string.h>
#include "es2/platform/os.h"
#include "es2/sim/trap.h"

static es2_sim_dispatch *trap_dispatch;
static es2_sim_trap_range trap_ranges[ES2_SIM_TRAP_RANGES];
static unsigned trap_range_count;
static DWORD trap_thread;
static PVOID trap_handle;
static uint64_t trap_calls;

int es2_sim_enable_dep(unsigned long *flags, int *permanent) {
    DWORD f = 0; BOOL p = FALSE;
    if (GetProcessDEPPolicy(GetCurrentProcess(), &f, &p) && (f & PROCESS_DEP_ENABLE) &&
        (p || (f & PROCESS_DEP_DISABLE_ATL_THUNK_EMULATION))) {
        if (flags) *flags = f;
        if (permanent) *permanent = p;
        return 0;
    }
    if (!SetProcessDEPPolicy(PROCESS_DEP_ENABLE | PROCESS_DEP_DISABLE_ATL_THUNK_EMULATION)) return 1;
    if (!GetProcessDEPPolicy(GetCurrentProcess(), &f, &p) || !(f & PROCESS_DEP_ENABLE)) return 1;
    if (flags) *flags = f;
    if (permanent) *permanent = p;
    return 0;
}

static int trap_key(uint32_t eip, uint32_t *key) {
    for (unsigned i = 0; i < trap_range_count; ++i) {
        const es2_sim_trap_range *r = &trap_ranges[i];
        if (eip < r->lo || eip >= r->hi) continue;
        if (!r->keys) { *key = eip; return 1; }
        uint32_t offset = eip - r->lo;
        /* An address between two slot traps is keyed by itself. */
        *key = offset % r->stride ? eip : r->keys[offset / r->stride];
        return 1;
    }
    return 0;
}

/* An execute fault in a trapped range is a call site that still jumps to an
 * original VA: the host's refusal callback with the reason "unconverted
 * call", then ES2_SIM_EXIT_REFUSED. The thread never continues. */
static LONG CALLBACK trap_handler(EXCEPTION_POINTERS *x) {
    const EXCEPTION_RECORD *r = x->ExceptionRecord;
    const CONTEXT *c = x->ContextRecord;
    if (r->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || r->NumberParameters < 2 ||
        r->ExceptionInformation[0] != 8 /* execute */ || GetCurrentThreadId() != trap_thread) return EXCEPTION_CONTINUE_SEARCH;
    uint32_t eip = (uint32_t)c->Eip, key;
    if ((uint32_t)r->ExceptionInformation[1] != eip || !trap_key(eip, &key)) return EXCEPTION_CONTINUE_SEARCH;
    ++trap_calls;
    es2_sim_dispatch *d = trap_dispatch;
    es2_sim_entry *e = (es2_sim_entry *)es2_sim_dispatch_find(d, key);
    if (e) ++e->traps;
    /* ESP at the call's return address slot. */
    const uint32_t frame = (uint32_t)c->Esp;
    es2_sim_refusal refusal = {key, es2_sim_dispatch_name(d, key), "unconverted call", *(const uint32_t *)(uintptr_t)frame, frame};
    ++d->refusals;
    if (d->refuse) d->refuse(&refusal, d->refuse_user);
    es2_os_exit(ES2_SIM_EXIT_REFUSED);
}

int es2_sim_trap_install(es2_sim_dispatch *d, const es2_sim_trap_range *ranges, unsigned count) {
    if (trap_handle || !d || !d->sealed || !count || count > ES2_SIM_TRAP_RANGES) return 1;
    for (unsigned i = 0; i < count; ++i)
        if (ranges[i].lo >= ranges[i].hi || (ranges[i].keys && !ranges[i].stride)) return 1;
    memcpy(trap_ranges, ranges, count * sizeof(*ranges));
    trap_range_count = count;
    trap_dispatch = d;
    trap_thread = GetCurrentThreadId();
    trap_handle = AddVectoredExceptionHandler(1, trap_handler);
    return trap_handle ? 0 : 1;
}

void es2_sim_trap_remove(void) {
    if (trap_handle) RemoveVectoredExceptionHandler(trap_handle);
    trap_handle = NULL;
    trap_range_count = 0;
    trap_dispatch = NULL;
}

uint64_t es2_sim_trap_calls(void) { return trap_calls; }
