/* Stage-1 trap: see es2/sim/trap.h. The handler only redirects; the work runs
 * in es2_sim_trap_prepare (called by the trampoline in dispatch_trap.S on the
 * caller's stack) and in the entry's target, which the trampoline calls
 * itself so that an x87 result stays in ST0 untouched by C code. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stddef.h>
#include <string.h>
#include "es2/sim/trap.h"

/* What the trampoline calls: target(a0, a1, a2), cdecl; then ECX ← ecx_out
 * and `pop_bytes` of stack arguments are removed. Layout shared with
 * dispatch_trap.S (offsets 0..20). */
typedef struct es2_sim_trap_plan {
    void *target;
    uint32_t a0, a1, a2;
    uint32_t ecx_out;
    uint32_t pop_bytes;
    /* private */
    uint32_t args[ES2_HOOK_MAX_ARGS];
    uint32_t regs[3];
    es2_sim_entry *entry;
    es2_sim_service_call call;
} es2_sim_trap_plan;
_Static_assert(offsetof(es2_sim_trap_plan, ecx_out) == 16 && offsetof(es2_sim_trap_plan, pop_bytes) == 20, "trampoline layout");

extern void es2_sim_trap_entry(void);
/* Decremented by the trampoline after the target returns. */
volatile unsigned es2_sim_trap_level;

static es2_sim_dispatch *trap_dispatch;
static es2_sim_trap_range trap_ranges[ES2_SIM_TRAP_RANGES];
static unsigned trap_range_count;
static DWORD trap_thread;
static PVOID trap_handle;
static volatile uint32_t trap_pending_key, trap_pending_eip;
static uint64_t trap_calls;
/* The bridged VAs (sorted) and their entries; 1 from es2_sim_bridge_target to the
 * trampoline's es2_sim_trap_prepare when the pending call came through the bridge
 * instead of the handler. */
static uint32_t bridge_vas[ES2_SIM_BRIDGE_MAX];
static es2_sim_entry *bridge_entries[ES2_SIM_BRIDGE_MAX];
static unsigned bridge_count;
static volatile int trap_bridged;
static uint64_t bridge_calls;
static es2_sim_trap_plan trap_plans[ES2_SIM_TRAP_DEPTH];
/* The shim the zeroing stub (dispatch_trap.S) continues at, and its register
 * save area. */
void *volatile es2_sim_trap_pending_shim;
uint32_t es2_sim_trap_shim_save[3];
extern void es2_sim_trap_shim_entry(void);

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
        /* An address between two slot traps is no entry: keyed by itself, refused. */
        *key = offset % r->stride ? eip : r->keys[offset / r->stride];
        return 1;
    }
    return 0;
}

static LONG CALLBACK trap_handler(EXCEPTION_POINTERS *x) {
    const EXCEPTION_RECORD *r = x->ExceptionRecord;
    CONTEXT *c = x->ContextRecord;
    if (r->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || r->NumberParameters < 2 ||
        r->ExceptionInformation[0] != 8 /* execute */ || GetCurrentThreadId() != trap_thread) return EXCEPTION_CONTINUE_SEARCH;
    uint32_t eip = (uint32_t)c->Eip, key;
    if ((uint32_t)r->ExceptionInformation[1] != eip || !trap_key(eip, &key)) return EXCEPTION_CONTINUE_SEARCH;
    trap_pending_key = key;
    trap_pending_eip = eip;
    trap_bridged = 0;
    ++trap_calls;
    /* An entry with a shim continues at the shim, behind the zeroing stub
     * (es2/sim/trap.h). */
    const es2_sim_entry *e = es2_sim_dispatch_find(trap_dispatch, key);
    es2_sim_trap_pending_shim = e ? e->entry_shim : NULL;
    c->Eip = (DWORD)(uintptr_t)(es2_sim_trap_pending_shim ? es2_sim_trap_shim_entry : es2_sim_trap_entry);
    return EXCEPTION_CONTINUE_EXECUTION;
}

int es2_sim_trap_install(es2_sim_dispatch *d, const es2_sim_trap_range *ranges, unsigned count) {
    if (trap_handle || !d || !d->sealed || !count || count > ES2_SIM_TRAP_RANGES) return 1;
    for (unsigned i = 0; i < count; ++i)
        if (ranges[i].lo >= ranges[i].hi || (ranges[i].keys && !ranges[i].stride)) return 1;
    memcpy(trap_ranges, ranges, count * sizeof(*ranges));
    trap_range_count = count;
    trap_dispatch = d;
    trap_thread = GetCurrentThreadId();
    es2_sim_trap_level = 0;
    trap_handle = AddVectoredExceptionHandler(1, trap_handler);
    return trap_handle ? 0 : 1;
}

int es2_sim_trap_active(void) { return trap_handle != NULL; }

void es2_sim_trap_remove(void) {
    if (trap_handle) RemoveVectoredExceptionHandler(trap_handle);
    trap_handle = NULL;
    bridge_count = 0;
    trap_bridged = 0;
    trap_range_count = 0;
    trap_dispatch = NULL;
}

uint64_t es2_sim_trap_calls(void) { return trap_calls; }
uint64_t es2_sim_bridge_calls(void) { return bridge_calls; }

int es2_sim_bridge_set(const uint32_t *vas, unsigned count) {
    bridge_count = 0;
    if (!trap_handle || count > ES2_SIM_BRIDGE_MAX || (count && !vas)) return 1;
    for (unsigned i = 0; i < count; ++i) {
        uint32_t key;
        /* Only an entry of the dispatch at an address the trap keys by itself
         * (CODE, not an import-slot trap); in VA order, no VA twice. */
        const es2_sim_entry *e = es2_sim_dispatch_find(trap_dispatch, vas[i]);
        if (!e || e->kind == ES2_SIM_REFUSED || !trap_key(vas[i], &key) || key != vas[i] || (i && vas[i] <= vas[i - 1]))
            return 1;
        bridge_vas[i] = vas[i];
        bridge_entries[i] = (es2_sim_entry *)e;
    }
    bridge_count = count;
    return 0;
}

unsigned es2_sim_bridge_count(void) { return bridge_count; }

/* The state the handler leaves for a fault at `va`, set without the fault:
 * the caller's next instruction is its CALL of the returned address, so the
 * trampoline (or the zeroing stub and the shim) starts with the registers,
 * the flags and the stack of that CALL, exactly as after the handler. */
uint32_t __cdecl es2_sim_bridge_target(uint32_t va) {
    unsigned lo = 0, hi = bridge_count;
    while (lo < hi) {
        unsigned mid = (lo + hi) / 2u;
        if (bridge_vas[mid] < va) lo = mid + 1u; else hi = mid;
    }
    if (lo == bridge_count || bridge_vas[lo] != va || GetCurrentThreadId() != trap_thread) return va;
    const es2_sim_entry *e = bridge_entries[lo];
    trap_pending_key = va;
    trap_pending_eip = va;
    trap_bridged = 1;
    es2_sim_trap_pending_shim = e->entry_shim;
    return (uint32_t)(uintptr_t)(e->entry_shim ? es2_sim_trap_shim_entry : es2_sim_trap_entry);
}
unsigned es2_sim_trap_depth(void) { return es2_sim_trap_level; }
void es2_sim_trap_reset_depth(void) { es2_sim_trap_level = 0; }

/* A refused trapped call cannot continue: the callback logs (and may longjmp
 * out); if it returns, the process ends with ES2_SIM_EXIT_REFUSED. */
static void trap_refuse(uint32_t key, const char *reason, uint32_t return_address, uint32_t frame) {
    es2_sim_dispatch *d = trap_dispatch;
    es2_sim_refusal r = {key, es2_sim_dispatch_name(d, key), reason, return_address, frame};
    ++d->refusals;
    if (d->refuse) d->refuse(&r, d->refuse_user);
    ExitProcess(ES2_SIM_EXIT_REFUSED);
}

static uint64_t __cdecl service_bridge(es2_sim_trap_plan *plan, void *unused1, void *unused2) {
    (void)unused1; (void)unused2;
    es2_sim_entry *e = plan->entry;
    plan->call.refused = 0; plan->call.reason = NULL;
    e->service(&plan->call, e->service_user);
    if (plan->call.refused) {
        --es2_sim_trap_level;
        trap_refuse(e->va, plan->call.reason ? plan->call.reason : "service refused", plan->call.return_address,
                    plan->call.stack - 4u);
    }
    plan->ecx_out = plan->call.ecx;
    return (uint64_t)plan->call.eax | (uint64_t)plan->call.edx << 32;
}

/* `frame`: the trampoline's pushfl/pushal block: EDI ESI EBP ESP EBX EDX ECX
 * EAX EFLAGS, then the caller's return address and the argument slots. */
es2_sim_trap_plan *__cdecl es2_sim_trap_prepare(uint32_t *frame);
es2_sim_trap_plan *__cdecl es2_sim_trap_prepare(uint32_t *frame) {
    uint32_t key = trap_pending_key;
    uint32_t return_address = frame[9];
    const uint32_t *slots = frame + 10;
    uint32_t frame_va = (uint32_t)(uintptr_t)(frame + 9);
    es2_sim_dispatch *d = trap_dispatch;
    if (!d) ExitProcess(ES2_SIM_EXIT_REFUSED);
    const es2_sim_entry *found = es2_sim_dispatch_find(d, key);
    if (!found || found->kind == ES2_SIM_REFUSED) trap_refuse(key, "unmapped", return_address, frame_va);
    if (es2_sim_trap_level >= ES2_SIM_TRAP_DEPTH) trap_refuse(key, "trap nesting too deep", return_address, frame_va);
    es2_sim_entry *e = (es2_sim_entry *)found;
    es2_sim_trap_plan *p = &trap_plans[es2_sim_trap_level++];
    ++e->calls;
    if (trap_bridged) { trap_bridged = 0; ++e->bridges; ++bridge_calls; }
    else ++e->traps;
    p->entry = e;
    p->regs[0] = frame[7]; p->regs[1] = frame[5]; p->regs[2] = frame[6];
    p->ecx_out = frame[6];
    if (e->kind == ES2_SIM_HOOK) {
        const es2_hook_descriptor *dsc = e->descriptor;
        const es2_hook_extension *x = e->extension;
        es2_sim_hook_args(dsc, x, slots, p->regs, p->args);
        p->target = !x ? (void *)dsc->replacement :
            x->replacement_context ? (void *)x->replacement_context :
            x->result_kind == ES2_RESULT_PAIR ? (void *)x->replacement64 :
            x->result_kind == ES2_RESULT_ST0 ? (void *)x->replacement80 : (void *)dsc->replacement;
        /* Every replacement is cdecl; a two-argument callback ignores the
         * third (as in the hook framework's fast path). */
        p->a0 = (uint32_t)(uintptr_t)p->args;
        p->a1 = (uint32_t)(uintptr_t)dsc->user;
        p->a2 = (uint32_t)(uintptr_t)p->regs;
        p->pop_bytes = es2_sim_hook_pop_bytes(dsc, x);
    } else {
        p->call.va = key; p->call.return_address = return_address;
        p->call.stack = (uint32_t)(uintptr_t)slots; p->call.slots = slots;
        p->call.eax = p->regs[0]; p->call.edx = p->regs[1]; p->call.ecx = p->regs[2];
        p->call.ebp = frame[2];
        p->target = (void *)service_bridge;
        p->a0 = (uint32_t)(uintptr_t)p; p->a1 = 0; p->a2 = 0;
        p->pop_bytes = e->pop_bytes;
    }
    return p;
}

/* The VA of the entry the trapped call `outward` levels out of the innermost
 * one runs (0: the innermost), 0 past the outermost. */
uint32_t es2_sim_trap_entry_va(unsigned outward) {
    unsigned level = es2_sim_trap_level;
    if (outward >= level || level > ES2_SIM_TRAP_DEPTH) return 0;
    const es2_sim_entry *e = trap_plans[level - 1u - outward].entry;
    return e ? e->va : 0;
}
