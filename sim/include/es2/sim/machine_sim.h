/* The sim backend of the machine interface (es2/hook/machine.h, web_build.md
 * section 4.2): es2_machine_call never jumps. It pushes the argument slots and
 * ES2_MACHINE_SIM_SENTINEL as the return address onto the modelled guest
 * stack, looks the VA up in the dispatch and runs the entry's C code with the
 * machine: an entry (es2_sim_entry_fn) gets it as it is; a hook gets its
 * arguments from EAX, EDX, ECX and the slots at ESP + 4 (es2_sim_hook_args);
 * a service gets the slots, EAX, EDX, ECX and EBP. The slots are popped per
 * ABI. An unlisted VA is refused through the dispatch's refusal callback
 * (es2sim exits with 70 there), and so is an entry whose stack cleanup is not
 * the call's ABI's.
 *
 * The guest stack is one 1 MiB region of guest memory, by preference at
 * ES2_MACHINE_SIM_STACK_LIMIT, directly below the heap's preferred base
 * (es2/sim/heap.h): no heap reservation, image page or other host region
 * moves for it. Its top 8 bytes hold the FS:0 sentinel record
 * {next ES2_MACHINE_FS0_END, handler 0}; the FS:0 head is a variable that
 * starts there. A push below the limit is a model error ("the guest stack
 * is exhausted"; the dispatch's model-error callback, es2sim exits with 71).
 *
 * A hook with an entry shim (es2_sim_entry.shim) runs the shim's C entry
 * first, on the guest stack; a shim's gap that passes the limit is the same
 * model error (es2_machine_probe). */
#ifndef ES2_SIM_MACHINE_SIM_H
#define ES2_SIM_MACHINE_SIM_H
#include <stdint.h>
#include "es2/hook/machine.h"
#include "es2/sim/dispatch.h"

#define ES2_MACHINE_SIM_STACK_SIZE 0x00100000u
#define ES2_MACHINE_SIM_STACK_LIMIT 0x1ff00000u
/* The return address of every pushed call: no guest code byte, no host
 * code address. */
#define ES2_MACHINE_SIM_SENTINEL 0xfffe0000u
/* The bytes below the caller's ESP that read as zero when an entry shim runs
 * (the residue model, docs/reference-game/own_process.md section 5). */
#define ES2_MACHINE_SIM_SHIM_ZERO 4096u

/* Binds es2_machine_call to `d` (sealed) and the guest stack [limit, base)
 * (committed, read-write, 16-byte aligned); writes the FS:0 sentinel record
 * at base - 8 and sets the FS:0 head and the start ESP to it. */
void es2_machine_sim_bind(es2_sim_dispatch *d, uint32_t limit, uint32_t base);
const es2_machine_stack *es2_machine_sim_stack(void);

/* es2_machine_fs0 and es2_machine_set_fs0 through `read` and `write` instead
 * of the variable (NULL, NULL: the variable again). The in-vitro sim suites
 * pass the real %fs:0 accessors (es2/hook/machine_x86.h): their original code
 * links its records on the real chain beside the services. es2sim and the web
 * build keep the variable. */
void es2_machine_sim_fs0_host(uint32_t (*read)(void), void (*write)(uint32_t));

/* The machine's faults: `fn` gets es2_machine_idiv's 0xc0000094 (zero
 * divisor) or 0xc0000095 (quotient beyond 32 bits), which end the run as the
 * original's unhandled divide fault (es2sim: main.c, the entry as the site),
 * or es2_machine_stack_overflow's 0xc00000fd. Without one, or when it
 * returns, es2_machine_idiv returns 0 and es2_machine_stack_overflow returns. */
typedef void (*es2_machine_sim_fault_fn)(uint32_t code, void *user);
void es2_machine_sim_on_fault(es2_machine_sim_fault_fn fn, void *user);

/* es2_machine_call on `d`. */
int es2_machine_sim_call(es2_sim_dispatch *d, es2_machine *m, uint32_t va, es2_machine_abi abi, const uint32_t *args,
                         unsigned count);
/* es2_machine_call_planted on `d`: the plants go below the pushed return
 * slot; an entry shim's C entry reads them there like the original. */
int es2_machine_sim_call_planted(es2_sim_dispatch *d, es2_machine *m, uint32_t va, es2_machine_abi abi,
                                 const uint32_t *args, unsigned count, const es2_machine_plant *plants,
                                 unsigned plant_count, uint32_t *entry);

/* es2_sim_call's path: the call of `va` with its stack already in place,
 * m->esp at the return address slot (neither read nor written: the entry is
 * told return address 0), `pushed` slots above it, any callee cleanup. An
 * entry with a shim runs on the guest stack below the innermost machine
 * call, its slots and a sentinel return address copied there. Out:
 * the registers in *m (ESP unchanged), the result's EAX, EDX, ECX and pop
 * bytes in *out. 0, or 1 when refused. */
int es2_machine_sim_enter(es2_sim_dispatch *d, es2_machine *m, uint32_t va, unsigned pushed, es2_sim_result *out);

/* A gap of `bytes` below `esp` that passes the guest stack's limit (an entry's
 * shim gap or probe loop, web_build.md section 4.6): the model-error callback
 * of the bound dispatch with `reason`, then 1; 0 when it fits or `esp` lies
 * outside the guest stack. */
int es2_machine_sim_below_limit(uint32_t va, uint32_t esp, uint32_t bytes, const char *reason);

/* Guest entries running now through this backend (web_build.md section 6). */
unsigned es2_machine_sim_depth(void);

/* The guest entries running now through this backend, innermost first: the
 * VA of the entry `outward` levels out of the innermost one (0: the
 * innermost), 0 past the outermost or when more than ES2_MACHINE_SIM_ENTRIES
 * entries are open. The NULL-page and divide rules of main.c and the present
 * row's call site (present.c) name their entry by it. */
#define ES2_MACHINE_SIM_ENTRIES 1024u
uint32_t es2_sim_guest_entry_va(unsigned outward);

/* The exit code of a wasm trap of kind `kind` (ES2_MACHINE_SIM_TRAP_*,
 * web_build.md section 6): inside a guest entry the original's divide codes,
 * otherwise and for every other kind ES2_MACHINE_SIM_TRAP_EXIT_FAULT. */
#define ES2_MACHINE_SIM_TRAP_OTHER 0u
#define ES2_MACHINE_SIM_TRAP_DIVIDE_BY_ZERO 1u
#define ES2_MACHINE_SIM_TRAP_INTEGER_OVERFLOW 2u
#define ES2_MACHINE_SIM_TRAP_EXIT_FAULT 71u
uint32_t es2_machine_sim_trap_exit_code(unsigned kind, uint32_t entry);

#endif
