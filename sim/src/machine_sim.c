/* The sim backend of the machine interface; see es2/sim/machine_sim.h. */
#include "es2/sim/machine_sim.h"
#include <stddef.h>
#include <string.h>
#include "es2/core/divide.h"
#include "es2/sim/guest_memory.h"

_Static_assert((int)ES2_MACHINE_CDECL == (int)ES2_ABI_CDECL && (int)ES2_MACHINE_STDCALL == (int)ES2_ABI_STDCALL &&
               (int)ES2_MACHINE_BORLAND == (int)ES2_ABI_BORLAND, "machine and hook ABIs");

static es2_sim_dispatch *bound;
static es2_machine_stack stack;
static uint32_t start_esp;
static uint32_t fs0 = ES2_MACHINE_FS0_END;
/* The machine of the innermost es2_machine_sim_call: a nested call with ESP 0
 * pushes below its ESP. es2_sim_call's machines are not set here: their ESP
 * lies in the caller's call area or on the host stack. */
static const es2_machine *active;
static unsigned depth;

static void put32(uint32_t va, uint32_t v) { es2_sim_guest_write32(va, v); }

/* The guest entries this backend runs, outermost first. */
static uint32_t entries[ES2_MACHINE_SIM_ENTRIES];
static void begin_entry(uint32_t va) {
    if (depth < ES2_MACHINE_SIM_ENTRIES) entries[depth] = va;
    ++depth;
}
uint32_t es2_sim_guest_entry_va(unsigned outward) {
    if (depth > ES2_MACHINE_SIM_ENTRIES || outward >= depth) return 0;
    return entries[depth - 1u - outward];
}

void es2_machine_sim_bind(es2_sim_dispatch *d, uint32_t limit, uint32_t base) {
    bound = d;
    stack.base = base; stack.limit = limit;
    put32(base - 8u, ES2_MACHINE_FS0_END);
    put32(base - 4u, 0u);
    fs0 = base - 8u;
    start_esp = base - 8u;
    active = NULL;
    depth = 0;
}

const es2_machine_stack *es2_machine_sim_stack(void) { return &stack; }
unsigned es2_machine_sim_depth(void) { return depth; }
static uint32_t (*host_fs0)(void);
static void (*host_set_fs0)(uint32_t);
void es2_machine_sim_fs0_host(uint32_t (*read)(void), void (*write)(uint32_t)) {
    host_fs0 = read; host_set_fs0 = write;
}
uint32_t es2_machine_fs0(void) { return host_fs0 ? host_fs0() : fs0; }
void es2_machine_set_fs0(uint32_t head) {
    if (host_set_fs0) host_set_fs0(head);
    else fs0 = head;
}

static es2_machine_sim_fault_fn machine_fault;
static void *machine_fault_user;
void es2_machine_sim_on_fault(es2_machine_sim_fault_fn fn, void *user) {
    machine_fault = fn; machine_fault_user = user;
}
void es2_machine_stack_overflow(void) {
    if (machine_fault) machine_fault(0xc00000fdu, machine_fault_user);
}
uint64_t es2_machine_idiv(uint32_t dividend, uint32_t divisor) {
    uint32_t quotient, remainder;
    /* CDQ; IDIV: EDX:EAX is the sign-extended dividend. */
    uint32_t code = es2_divide((uint64_t)(int64_t)(int32_t)dividend, divisor, 1, &quotient, &remainder);
    if (code) {
        if (machine_fault) machine_fault(code, machine_fault_user);
        return 0;
    }
    return (uint64_t)remainder << 32 | quotient;
}

static int refuse(es2_sim_dispatch *d, uint32_t va, const char *reason, uint32_t return_address, uint32_t frame) {
    es2_sim_refusal r = {va, es2_sim_dispatch_name(d, va), reason, return_address, frame};
    ++d->refusals;
    if (d->refuse) d->refuse(&r, d->refuse_user);
    return 1;
}

static int model_error(es2_sim_dispatch *d, uint32_t va, const char *reason, uint32_t frame) {
    es2_sim_refusal r = {va, es2_sim_dispatch_name(d, va), reason, ES2_MACHINE_SIM_SENTINEL, frame};
    ++d->model_errors;
    if (d->model_error) d->model_error(&r, d->model_error_user);
    return 1;
}

int es2_machine_sim_below_limit(uint32_t va, uint32_t esp, uint32_t bytes, const char *reason) {
    if (!(esp > stack.limit && esp <= stack.base) || (uint64_t)esp - stack.limit >= bytes) return 0;
    if (!bound) return 1;
    return model_error(bound, va, reason, esp);
}

int es2_machine_probe(uint32_t va, uint32_t top, uint32_t bytes) {
    return es2_machine_sim_below_limit(va, top, bytes, "a shim's stack gap passes the guest stack's limit");
}

uint32_t es2_machine_floor(uint32_t esp) { return esp; }

static unsigned stack_slots_read(const es2_sim_entry *e) {
    if (e->kind != ES2_SIM_HOOK) return e->stack_slots;
    unsigned n = e->descriptor->arg_count;
    if (e->extension && e->extension->abi == ES2_ABI_BORLAND) return n > 3 ? n - 3 : 0;
    return n;
}

static unsigned entry_pop_bytes(const es2_sim_entry *e) {
    return e->kind == ES2_SIM_HOOK ? es2_sim_hook_pop_bytes(e->descriptor, e->extension) : e->pop_bytes;
}

/* The hook whose shim is running and has not yet called its replacement
 * (es2/hook/machine.h, es2_machine_shim): its call of `bridge`, the VA, runs
 * the replacement; every other call of the VA runs the shim again. */
static uint32_t shim_open;

static int enter(es2_sim_dispatch *d, es2_machine *m, uint32_t va, unsigned pushed, int machine, unsigned abi_pop,
                 es2_sim_result *out);

/* The residue model (docs/reference-game/own_process.md section 5): the
 * ES2_MACHINE_SIM_SHIM_ZERO bytes below the caller's ESP read as zero when a
 * shim runs, within the guest stack. */
static void zero_residue(uint32_t esp) {
    if (!(esp > stack.limit && esp <= stack.base)) return;
    uint32_t low = esp - stack.limit < ES2_MACHINE_SIM_SHIM_ZERO ? stack.limit : esp - ES2_MACHINE_SIM_SHIM_ZERO;
    void *p = es2_sim_guest_writable(low, esp - low);
    if (p) memset(p, 0, esp - low);
}

/* A shim's entry reached by es2_sim_call (slots in the caller's call area or
 * on the host stack): the slots and a sentinel return address go onto the
 * guest stack below the innermost machine call, so that the shim's frame and
 * its gap lie on the guest stack. */
static int enter_on_guest_stack(es2_sim_dispatch *d, es2_machine *m, uint32_t va, unsigned pushed, unsigned pop,
                                es2_sim_result *out) {
    uint32_t top = active ? active->esp : start_esp, bytes = 4u * (pushed + 1u);
    if (!top) return refuse(d, va, "no guest stack", 0u, m->esp);
    if (top > stack.limit && top <= stack.base && (uint64_t)top - stack.limit < bytes)
        return model_error(d, va, "the guest stack is exhausted", top);
    const uint32_t *slots = es2_sim_guest_span(m->esp + 4u, 4u * pushed);
    if (!slots && pushed) return model_error(d, va, "the argument slots lie outside guest memory", m->esp);
    uint32_t at = top - bytes;
    put32(at, ES2_MACHINE_SIM_SENTINEL);
    for (unsigned i = 0; i < pushed; ++i) put32(at + 4u + 4u * i, slots[i]);
    es2_machine c = *m;
    c.esp = at;
    if (enter(d, &c, va, pushed, 1, pop, out)) return 1;
    c.esp = m->esp;
    *m = c;
    return 0;
}

/* `machine`: an es2_machine_sim_call (return address the sentinel, cleanup
 * `abi_pop` checked, all registers); else es2_sim_call's path. */
static int enter(es2_sim_dispatch *d, es2_machine *m, uint32_t va, unsigned pushed, int machine, unsigned abi_pop,
                 es2_sim_result *out) {
    uint32_t return_address = machine ? ES2_MACHINE_SIM_SENTINEL : 0u, frame = machine ? m->esp : 0u;
    memset(out, 0, sizeof(*out));
    const es2_sim_entry *found = es2_sim_dispatch_find(d, va);
    if (!found || found->kind == ES2_SIM_REFUSED) return refuse(d, va, "unmapped", return_address, frame);
    es2_sim_entry *e = (es2_sim_entry *)found;
    if (stack_slots_read(e) > pushed)
        return refuse(d, va, "fewer argument slots than the entry reads", return_address, frame);
    unsigned pop = entry_pop_bytes(e);
    if (machine && pop != abi_pop) return refuse(d, va, "stack cleanup differs from the call's ABI", return_address, frame);
    const uint32_t *slots = es2_sim_guest_span(m->esp + 4u, 4u * pushed);
    if (!slots && pushed) return model_error(d, va, "the argument slots lie outside guest memory", frame);
    uint32_t esp = m->esp;
    if (e->kind == ES2_SIM_HOOK && e->shim) {
        if (va == shim_open) {
            shim_open = 0;
        } else if (!machine) {
            return enter_on_guest_stack(d, m, va, pushed, pop, out);
        } else {
            uint32_t outer = shim_open;
            begin_entry(va);
            shim_open = va;
            zero_residue(m->esp);
            int ran = e->shim(m, va);
            shim_open = outer;
            --depth;
            if (ran) {
                m->esp = esp;
                out->eax = m->eax; out->edx = m->edx; out->ecx = m->ecx; out->pop_bytes = pop;
                return 0;
            }
            m->esp = esp;
        }
    }
    ++e->calls;
    begin_entry(va);
    const es2_machine *outer = active;
    if (machine) active = m;
    const char *reason = NULL;
    if (e->kind == ES2_SIM_SERVICE) {
        es2_sim_service_call call = {va, return_address, m->esp + 4u, slots, m->eax, m->edx, m->ecx, m->ebp, 0, NULL};
        e->service(&call, e->service_user);
        if (call.refused) reason = call.reason ? call.reason : "service refused";
        else { m->eax = call.eax; m->edx = call.edx; m->ecx = call.ecx; }
    } else if (e->kind == ES2_SIM_ENTRY) {
        e->entry(m, e->service_user);
    } else {
        uint32_t regs[3] = {m->eax, m->edx, m->ecx}, args[ES2_HOOK_MAX_ARGS];
        es2_sim_hook_args(e->descriptor, e->extension, slots, regs, args);
        const es2_hook_extension *x = e->extension;
        void *user = e->descriptor->user;
        if (!x) {
            m->eax = e->descriptor->replacement(args, user);
        } else if (x->replacement_context) {
            uint64_t r = x->replacement_context(args, user, regs);
            m->eax = (uint32_t)r; m->edx = (uint32_t)(r >> 32);
        } else if (x->result_kind == ES2_RESULT_PAIR) {
            uint64_t r = x->replacement64(args, user);
            m->eax = (uint32_t)r; m->edx = (uint32_t)(r >> 32);
        } else {
            m->eax = e->descriptor->replacement(args, user);
        }
    }
    active = outer;
    --depth;
    m->esp = esp;
    if (reason) return refuse(d, va, reason, return_address, frame);
    out->eax = m->eax; out->edx = m->edx; out->ecx = m->ecx; out->pop_bytes = pop;
    return 0;
}

int es2_machine_sim_enter(es2_sim_dispatch *d, es2_machine *m, uint32_t va, unsigned pushed, es2_sim_result *out) {
    return enter(d, m, va, pushed, 0, 0, out);
}

int es2_machine_sim_call_planted(es2_sim_dispatch *d, es2_machine *m, uint32_t va, es2_machine_abi abi,
                                 const uint32_t *args, unsigned count, const es2_machine_plant *plants,
                                 unsigned plant_count, uint32_t *entry) {
    if (!d) return 1;
    uint32_t top = m->esp ? m->esp : active ? active->esp : start_esp;
    if (count > ES2_MACHINE_MAX_ARGS)
        return refuse(d, va, "more arguments than a machine call takes", ES2_MACHINE_SIM_SENTINEL, top);
    if (plant_count > ES2_MACHINE_MAX_PLANTS)
        return refuse(d, va, "more plants than a machine call takes", ES2_MACHINE_SIM_SENTINEL, top);
    if (!top) return refuse(d, va, "no guest stack", ES2_MACHINE_SIM_SENTINEL, top);
    es2_machine c = *m;
    uint32_t slots[ES2_MACHINE_MAX_ARGS];
    unsigned n = es2_machine_layout(&c, abi, args, count, slots);
    uint32_t bytes = 4u * (n + 1u), below = 0;
    for (unsigned i = 0; i < plant_count; ++i)
        if (plants[i].offset + 1u > below) below = plants[i].offset + 1u;
    if (top > stack.limit && top <= stack.base && (uint64_t)top - stack.limit < (uint64_t)bytes + below)
        return model_error(d, va, "the guest stack is exhausted", top);
    uint32_t at = top - bytes;
    put32(at, ES2_MACHINE_SIM_SENTINEL);
    for (unsigned i = 0; i < n; ++i) put32(at + 4u + 4u * i, slots[i]);
    for (unsigned i = 0; i < plant_count; ++i) es2_sim_guest_write8(at - plants[i].offset, (uint8_t)plants[i].byte);
    if (entry) *entry = at;
    c.esp = at;
    es2_sim_result out;
    if (enter(d, &c, va, n, 1, es2_machine_pop_bytes(abi, count), &out)) return 1;
    c.esp = m->esp;
    *m = c;
    return 0;
}

int es2_machine_sim_call(es2_sim_dispatch *d, es2_machine *m, uint32_t va, es2_machine_abi abi, const uint32_t *args,
                         unsigned count) {
    return es2_machine_sim_call_planted(d, m, va, abi, args, count, NULL, 0, NULL);
}

int es2_machine_call(es2_machine *m, uint32_t va, es2_machine_abi abi, const uint32_t *args, unsigned count) {
    return es2_machine_sim_call(bound, m, va, abi, args, count);
}

int es2_machine_call_planted(es2_machine *m, uint32_t va, es2_machine_abi abi, const uint32_t *args, unsigned count,
                             const es2_machine_plant *plants, unsigned plant_count, uint32_t *entry) {
    return es2_machine_sim_call_planted(bound, m, va, abi, args, count, plants, plant_count, entry);
}

uint32_t es2_machine_sim_trap_exit_code(unsigned kind, uint32_t entry) {
    if (!entry) return ES2_MACHINE_SIM_TRAP_EXIT_FAULT;
    if (kind == ES2_MACHINE_SIM_TRAP_DIVIDE_BY_ZERO) return ES2_DIVIDE_BY_ZERO;
    if (kind == ES2_MACHINE_SIM_TRAP_INTEGER_OVERFLOW) return ES2_DIVIDE_OVERFLOW;
    return ES2_MACHINE_SIM_TRAP_EXIT_FAULT;
}
