/* The machine interface (docs/web_build.md section 4.2): a call of a guest VA
 * with the eight general registers. Two backends implement es2_machine_call,
 * and a link holds exactly one:
 *   x86  (//hook:machine_x86, hook/machine_x86.c over machine_x86.S): loads
 *        the registers, switches ESP when asked and calls the VA; the
 *        original code runs. The proxy and the in-vitro harness.
 *   sim  (//sim:dispatch, sim/src/machine_sim.c): pushes the arguments and a
 *        return sentinel onto the modelled guest stack and runs the dispatch
 *        entry of the VA; nothing jumps to the VA. es2sim.
 * Portable C17: no Windows header, no x86 assumption outside the
 * __i386__ part at the end. */
#ifndef ES2_HOOK_MACHINE_H
#define ES2_HOOK_MACHINE_H
#include <stddef.h>
#include <stdint.h>

/* The general registers. Layout shared with machine_x86.S (offsets 0..28). */
typedef struct es2_machine {
    uint32_t eax, ecx, edx, ebx, esp, ebp, esi, edi;
} es2_machine;

/* The hook framework's conventions (values equal to es2_hook_abi):
 *   CDECL    stack slots, the caller removes them;
 *   STDCALL  stack slots, the callee removes them;
 *   BORLAND  EAX, EDX, ECX, then stack slots pushed left to right, the callee
 *            removes the stack slots. */
typedef enum es2_machine_abi { ES2_MACHINE_CDECL, ES2_MACHINE_STDCALL, ES2_MACHINE_BORLAND } es2_machine_abi;
#define ES2_MACHINE_MAX_ARGS 16u

/* The guest stack: guest memory [limit, base), growing down from base, as
 * Windows' TIB StackBase/StackLimit. */
typedef struct es2_machine_stack {
    uint32_t base, limit;
} es2_machine_stack;

/* The end of the FS:0 chain (Windows' value; the `next` of the last record). */
#define ES2_MACHINE_FS0_END 0xffffffffu

/* Calls `va` with `count` arguments under `abi`. In: every register the
 * callee reads; m->esp the stack to push on, 0 for the current one (x86: the
 * host stack; sim: the innermost machine call's stack, at start the guest
 * stack's top). BORLAND takes args[0..2] into EAX, EDX, ECX (as many as
 * `count` has) and the rest onto the stack. Out: EAX, ECX, EDX, EBX, EBP,
 * ESI, EDI as the callee leaves them; the result is EAX or EDX:EAX
 * (es2_machine_pair). m->esp is unchanged: the arguments are removed per
 * ABI, the caller's part included. 0, or 1 when the call was refused (the
 * dispatch's refusal callback ran and returned; the x86 backend refuses only
 * a count above ES2_MACHINE_MAX_ARGS). */
int es2_machine_call(es2_machine *m, uint32_t va, es2_machine_abi abi, const uint32_t *args, unsigned count);

/* Planted stack residue (docs/web_build.md section 4.3): one byte at
 * `offset` bytes below the callee's entry ESP (the slot of its return
 * address). Layout shared with machine_x86.S (offset 0, byte 4). */
typedef struct es2_machine_plant {
    uint32_t offset, byte;
} es2_machine_plant;
#define ES2_MACHINE_MAX_PLANTS 16u

/* es2_machine_call that also writes `plants` (at most
 * ES2_MACHINE_MAX_PLANTS; byte 0 of each `byte`) below the callee's entry
 * ESP after the arguments are pushed and before the callee runs, and stores
 * that entry ESP in *entry unless it is NULL. Refused like es2_machine_call,
 * and for too many plants. */
int es2_machine_call_planted(es2_machine *m, uint32_t va, es2_machine_abi abi, const uint32_t *args, unsigned count,
                             const es2_machine_plant *plants, unsigned plant_count, uint32_t *entry);

/* The head of the FS:0 chain: x86 the real %fs:0 (es2/hook/machine_x86.h),
 * sim a variable. */
uint32_t es2_machine_fs0(void);
void es2_machine_set_fs0(uint32_t head);

/* The original's IDIV: EDX:EAX = `dividend` sign-extended (CDQ) divided by
 * `divisor`; returns EDX (remainder) << 32 | EAX (quotient). A zero divisor
 * or a quotient beyond 32 bits is the original's divide fault: x86 executes
 * the instruction, which raises it; sim ends the run with 0xc0000094 or
 * 0xc0000095 (es2_machine_sim_on_fault). */
uint64_t es2_machine_idiv(uint32_t dividend, uint32_t divisor);

/* A stack overflow of own code (EXCEPTION_STACK_OVERFLOW, 0xc00000fd,
 * noncontinuable): x86 raises it; sim ends the run through the fault callback
 * (es2_machine_sim_on_fault). */
void es2_machine_stack_overflow(void);

/* An adapter's entry shim as a C entry (docs/web_build.md section 4.4,
 * tools/reference-game/proxy/entry_shim.h): it reads the entry registers from
 * *m and the stack from guest memory at m->esp (the return address there).
 * Returns 0 when the call continues into the hook's replacement with *m (ESP
 * unchanged), 1 when the shim ran the replacement itself through
 * es2_machine_call(m, bridge, ...) and *m holds the registers at its return,
 * m->esp the entry ESP again. `bridge`: x86 the framework's entry of the
 * hook, sim the hook's VA (the dispatch then skips the shim once). */
typedef int (*es2_machine_shim)(es2_machine *m, uint32_t bridge);

/* A shim's stack gap: `bytes` below `top` become the shim's frame. x86
 * touches each page below `top` in 4096-byte steps (the original's probe
 * loop; guard pages commit); sim checks the guest stack's limit, and passing
 * it is a model error (es2sim exits 71). `va` names the shim in that error.
 * 0, or 1 when the model error callback returned. */
int es2_machine_probe(uint32_t va, uint32_t top, uint32_t bytes);

/* The ESP a gapped shim continues at. x86: the lowest host
 * ESP of a suspended machine call that switched ESP (es2_machine_x86_floor)
 * when it lies below `esp` and on the thread's committed stack, else `esp`;
 * sim: `esp` (no host frame lives on the guest stack). */
uint32_t es2_machine_floor(uint32_t esp);

static inline uint64_t es2_machine_pair(const es2_machine *m) { return (uint64_t)m->edx << 32 | m->eax; }

/* The stack bytes the callee removes. */
static inline unsigned es2_machine_pop_bytes(es2_machine_abi abi, unsigned count) {
    if (abi == ES2_MACHINE_STDCALL) return 4u * count;
    if (abi == ES2_MACHINE_BORLAND) return count > 3u ? 4u * (count - 3u) : 0u;
    return 0u;
}

/* The argument layout shared by both backends: sets the BORLAND register
 * arguments in *m and fills `slots` in address order (slots[0] at ESP + 4
 * after the call). Returns the slot count; count <= ES2_MACHINE_MAX_ARGS. */
static inline unsigned es2_machine_layout(es2_machine *m, es2_machine_abi abi, const uint32_t *args, unsigned count,
                                          uint32_t slots[ES2_MACHINE_MAX_ARGS]) {
    if (abi != ES2_MACHINE_BORLAND) {
        for (unsigned i = 0; i < count; ++i) slots[i] = args[i];
        return count;
    }
    if (count > 0u) m->eax = args[0];
    if (count > 1u) m->edx = args[1];
    if (count > 2u) m->ecx = args[2];
    unsigned n = count > 3u ? count - 3u : 0u;
    /* Left to right: args[3] is pushed first and lies highest. */
    for (unsigned i = 0; i < n; ++i) slots[i] = args[count - 1u - i];
    return n;
}

#if defined(__i386__)
/* The x86 primitive (machine_x86.S): pushes slots[count-1] .. slots[0] on
 * m->esp (0: the host stack), writes the plants below the entry ESP and the
 * entry ESP to *entry (when not NULL), loads EAX..EDI from *m, calls `target` and
 * stores EAX, ECX, EDX, EBX, EBP, ESI, EDI back; the host ESP is restored
 * from its own frame, so whatever the callee pops, m->esp is unchanged. The
 * x87 state is saved and restored around the call (fnsave/frstor), so the
 * callee starts with the caller's state and the caller continues with the
 * callee's. One thread at a time (a static anchor finds the host frame after
 * the call). */
void __attribute__((cdecl)) es2_machine_x86_enter(es2_machine *m, uint32_t target, const uint32_t *slots, unsigned count,
                                                  const es2_machine_plant *plants, unsigned plant_count, uint32_t *entry);
/* The innermost es2_machine_x86_enter in progress: the address of its
 * slots[0] (ESP + 4 at the CALL; 0 outside every call) and the number of
 * calls in progress. Host data only; the in-vitro logging stubs tell a call
 * straight from a bridge by them. */
extern uint32_t es2_machine_x86_slots;
extern unsigned es2_machine_x86_depth;
/* The lowest host ESP of the calls in progress that moved ESP (m->esp not 0),
 * 0 while none does: every host frame still suspended below a switched call
 * lies at or above it. The adapters' entry shims move their ESP down to it
 * (es2_machine_floor; tools/reference-game/proxy/weapons_hooks.h). */
extern uint32_t es2_machine_x86_floor;

static inline int es2_machine_x86_call_planted(es2_machine *m, uint32_t va, es2_machine_abi abi, const uint32_t *args,
                                               unsigned count, const es2_machine_plant *plants, unsigned plant_count,
                                               uint32_t *entry) {
    uint32_t slots[ES2_MACHINE_MAX_ARGS];
    if (count > ES2_MACHINE_MAX_ARGS || plant_count > ES2_MACHINE_MAX_PLANTS) return 1;
    unsigned n = es2_machine_layout(m, abi, args, count, slots);
    es2_machine_x86_enter(m, va, slots, n, plants, plant_count, entry);
    return 0;
}
static inline int es2_machine_x86_call(es2_machine *m, uint32_t va, es2_machine_abi abi, const uint32_t *args,
                                       unsigned count) {
    return es2_machine_x86_call_planted(m, va, abi, args, count, NULL, 0, NULL);
}
#endif

#endif
