/* The dispatch table and the call from own C code; see es2/sim/dispatch.h. */
#include "es2/sim/dispatch.h"
#include "es2/sim/trap.h"
#include <stdlib.h>
#include <string.h>

void es2_sim_dispatch_init(es2_sim_dispatch *d, es2_sim_entry *storage, unsigned capacity,
                           es2_sim_refuse_fn refuse, void *refuse_user) {
    memset(d, 0, sizeof(*d));
    d->entries = storage; d->capacity = capacity;
    d->refuse = refuse; d->refuse_user = refuse_user;
}

static es2_sim_entry *add(es2_sim_dispatch *d, uint32_t va, const char *name, es2_sim_kind kind) {
    if (d->sealed || d->count >= d->capacity) return NULL;
    es2_sim_entry *e = &d->entries[d->count++];
    memset(e, 0, sizeof(*e));
    e->va = va; e->name = name; e->kind = kind;
    return e;
}

int es2_sim_dispatch_add_hook(es2_sim_dispatch *d, uint32_t va, const char *name,
                              const es2_hook_descriptor *descriptor, const es2_hook_extension *extension) {
    es2_sim_entry *e = add(d, va, name, ES2_SIM_HOOK);
    if (!e) return 1;
    e->descriptor = descriptor; e->extension = extension;
    return 0;
}

int es2_sim_dispatch_add_service(es2_sim_dispatch *d, uint32_t va, const char *name, es2_sim_service_fn service,
                                 void *user, uint16_t pop_bytes, uint8_t stack_slots) {
    es2_sim_entry *e = add(d, va, name, ES2_SIM_SERVICE);
    if (!e) return 1;
    e->service = service; e->service_user = user; e->pop_bytes = pop_bytes; e->stack_slots = stack_slots;
    return 0;
}

static int by_va(const void *a, const void *b) {
    uint32_t x = ((const es2_sim_entry *)a)->va, y = ((const es2_sim_entry *)b)->va;
    return x < y ? -1 : x > y;
}

/* The extension checks of es2_hook_install_ex (hook.c), so that an entry the
 * framework would refuse to install is refused here too. */
static int extension_valid(const es2_hook_descriptor *dsc, const es2_hook_extension *e) {
    if (!dsc || dsc->arg_count > ES2_HOOK_MAX_ARGS) return 0;
    if (!e) return dsc->replacement != NULL;
    if ((unsigned)e->abi > ES2_ABI_BORLAND || (unsigned)e->result_kind > ES2_RESULT_ST0) return 0;
    if (e->result_kind == ES2_RESULT_EAX && !dsc->replacement && !e->replacement_context) return 0;
    if (e->result_kind == ES2_RESULT_PAIR && !e->replacement64 && !e->replacement_context) return 0;
    if (e->replacement_context && e->result_kind != ES2_RESULT_PAIR) return 0;
    if (e->result_kind == ES2_RESULT_ST0 && !e->replacement80) return 0;
    return 1;
}

int es2_sim_dispatch_seal(es2_sim_dispatch *d, const char **error, uint32_t *bad) {
    qsort(d->entries, d->count, sizeof(*d->entries), by_va);
    for (unsigned i = 0; i < d->count; ++i) {
        const es2_sim_entry *e = &d->entries[i];
        if (i && d->entries[i - 1].va == e->va) { *error = "VA listed twice"; *bad = e->va; return 1; }
        if (e->kind == ES2_SIM_HOOK && !extension_valid(e->descriptor, e->extension)) {
            *error = "hook ABI or result kind invalid"; *bad = e->va; return 1;
        }
        if (e->kind == ES2_SIM_SERVICE && !e->service) { *error = "service without a function"; *bad = e->va; return 1; }
        if (e->kind == ES2_SIM_SERVICE && (e->pop_bytes & 3u)) {
            *error = "stack cleanup not a multiple of 4"; *bad = e->va; return 1;
        }
    }
    d->sealed = 1;
    return 0;
}

const es2_sim_entry *es2_sim_dispatch_find(const es2_sim_dispatch *d, uint32_t va) {
    unsigned lo = 0, hi = d->count;
    while (lo < hi) {
        unsigned mid = lo + (hi - lo) / 2;
        uint32_t v = d->entries[mid].va;
        if (v == va) return &d->entries[mid];
        if (v < va) lo = mid + 1; else hi = mid;
    }
    return NULL;
}

const char *es2_sim_dispatch_name(const es2_sim_dispatch *d, uint32_t va) {
    const es2_sim_entry *e = es2_sim_dispatch_find(d, va);
    if (e) return e->name;
    for (unsigned i = 0; i < d->import_count; ++i) if (d->import_slots[i] == va) return d->import_names[i];
    return NULL;
}

void es2_sim_hook_args(const es2_hook_descriptor *dsc, const es2_hook_extension *e,
                       const uint32_t *slots, const uint32_t regs[3], uint32_t args[ES2_HOOK_MAX_ARGS]) {
    unsigned n = dsc->arg_count;
    for (unsigned i = 0; i < ES2_HOOK_MAX_ARGS; ++i) args[i] = 0;
    if (e && e->abi == ES2_ABI_BORLAND) {
        /* EAX, EDX, ECX, then the stack slots in the order of the hook
         * framework's fast path (es2_hook_dispatch_fast). */
        if (n > 0) args[0] = regs[0];
        if (n > 1) args[1] = regs[1];
        if (n > 2) args[2] = regs[2];
        if (n > 3) args[3] = slots[n - 4];
        if (n > 4) args[4] = slots[n - 5];
        if (n > 5) args[5] = slots[0];
    } else {
        for (unsigned i = 0; i < n; ++i) args[i] = slots[i];
    }
}

unsigned es2_sim_hook_pop_bytes(const es2_hook_descriptor *dsc, const es2_hook_extension *e) {
    /* The hook bridge's `ret N` (hook.c): cdecl and legacy entries pop
     * nothing, stdcall every slot, Borland the slots beyond the three
     * register arguments. */
    if (!e || e->abi == ES2_ABI_CDECL) return 0;
    unsigned slots = dsc->arg_count;
    if (e->abi == ES2_ABI_BORLAND) slots = slots > 3 ? slots - 3 : 0;
    return 4u * slots;
}

static unsigned stack_slots_read(const es2_sim_entry *e) {
    if (e->kind != ES2_SIM_HOOK) return e->stack_slots;
    unsigned n = e->descriptor->arg_count;
    if (e->extension && e->extension->abi == ES2_ABI_BORLAND) return n > 3 ? n - 3 : 0;
    return n;
}

int es2_sim_call(es2_sim_dispatch *d, uint32_t va, const uint32_t *slots, unsigned count, const uint32_t regs[3],
                 es2_sim_result *out) {
    memset(out, 0, sizeof(*out));
    const es2_sim_entry *found = es2_sim_dispatch_find(d, va);
    es2_sim_refusal refusal = {va, es2_sim_dispatch_name(d, va), "unmapped", 0, 0};
    if (!found || found->kind == ES2_SIM_REFUSED) {
        ++d->refusals;
        if (d->refuse) d->refuse(&refusal, d->refuse_user);
        return 1;
    }
    es2_sim_entry *e = (es2_sim_entry *)found;
    if (stack_slots_read(e) > count) {
        refusal.reason = "fewer argument slots than the entry reads";
        ++d->refusals;
        if (d->refuse) d->refuse(&refusal, d->refuse_user);
        return 1;
    }
    /* An entry with a shim is entered as a machine call, so that the trap runs
     * the shim (es2/sim/trap.h); without the trap, or for an x87 result (the
     * machine call keeps no ST0), the direct path. */
    if (e->entry_shim && es2_sim_trap_active() && !(e->extension && e->extension->result_kind == ES2_RESULT_ST0)) {
        uint32_t r[3];
        es2_sim_trap_call(va, slots, count, regs, r);
        out->eax = r[0]; out->edx = r[1]; out->ecx = r[2];
        out->pop_bytes = es2_sim_hook_pop_bytes(e->descriptor, e->extension);
        return 0;
    }
    ++e->calls;
    out->eax = regs[0]; out->edx = regs[1]; out->ecx = regs[2];
    if (e->kind == ES2_SIM_SERVICE) {
        es2_sim_service_call call = {va, 0, (uint32_t)(uintptr_t)slots, slots, regs[0], regs[1], regs[2], 0, 0, NULL};
        e->service(&call, e->service_user);
        if (call.refused) {
            refusal.reason = call.reason ? call.reason : "service refused";
            ++d->refusals;
            if (d->refuse) d->refuse(&refusal, d->refuse_user);
            return 1;
        }
        out->eax = call.eax; out->edx = call.edx; out->ecx = call.ecx; out->pop_bytes = e->pop_bytes;
        return 0;
    }
    uint32_t args[ES2_HOOK_MAX_ARGS];
    es2_sim_hook_args(e->descriptor, e->extension, slots, regs, args);
    const es2_hook_extension *x = e->extension;
    void *user = e->descriptor->user;
    out->pop_bytes = es2_sim_hook_pop_bytes(e->descriptor, x);
    if (!x) {
        out->eax = e->descriptor->replacement(args, user);
    } else if (x->replacement_context) {
        uint64_t r = x->replacement_context(args, user, regs);
        out->eax = (uint32_t)r; out->edx = (uint32_t)(r >> 32);
    } else if (x->result_kind == ES2_RESULT_PAIR) {
        uint64_t r = x->replacement64(args, user);
        out->eax = (uint32_t)r; out->edx = (uint32_t)(r >> 32);
    } else if (x->result_kind == ES2_RESULT_ST0) {
        out->st0 = x->replacement80(args, user);
        out->has_st0 = 1;
    } else {
        out->eax = e->descriptor->replacement(args, user);
    }
    return 0;
}
