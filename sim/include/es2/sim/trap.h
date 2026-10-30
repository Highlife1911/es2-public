/* Stage 1 of the call path (own_process.md section 4.4): the guest's CODE is
 * mapped readable and not executable, so a machine call of an adapter or core
 * bridge to an original VA faults. A vectored exception handler (first in the
 * chain) takes the execute fault, keeps the VA, and continues at a host
 * trampoline that runs the dispatch on the caller's stack and returns like the
 * original function: EAX/EDX (or ST0) as the entry's result, ECX as the entry
 * leaves it, every other register and the flags as at the call, the callee's
 * stack arguments removed. No original instruction executes.
 *
 * Requirements: DEP on for the process with ATL thunk emulation off (Windows
 * would otherwise emulate some byte patterns of non-executable pages); one
 * simulation thread (the thread that installs the trap; faults on other
 * threads are not handled). Windows x86 only. */
#ifndef ES2_SIM_TRAP_H
#define ES2_SIM_TRAP_H
#include <stdint.h>
#include "es2/sim/dispatch.h"

/* A trapped address range [lo, hi). With `keys`, the dispatch key of the
 * address lo + stride * i is keys[i] (the import-slot traps: one trap address
 * per IAT slot, keyed by the slot's VA); without, the key is the address. */
typedef struct es2_sim_trap_range {
    uint32_t lo, hi;
    const uint32_t *keys;
    uint32_t stride;
} es2_sim_trap_range;

#define ES2_SIM_TRAP_RANGES 4u
#define ES2_SIM_TRAP_DEPTH 64u

/* Turns DEP on for the process with ATL thunk emulation off (or confirms it
 * is on permanently). 0 on success. */
int es2_sim_enable_dep(unsigned long *flags, int *permanent);

/* Installs the handler for calls from the current thread into `ranges`,
 * dispatched by `d` (sealed). 0 or 1 (already installed, bad ranges). */
int es2_sim_trap_install(es2_sim_dispatch *d, const es2_sim_trap_range *ranges, unsigned count);
void es2_sim_trap_remove(void);
/* Trapped calls so far (also refused ones), and the current nesting depth. */
uint64_t es2_sim_trap_calls(void);
unsigned es2_sim_trap_depth(void);
/* For tests that longjmp out of a refused trapped call. */
void es2_sim_trap_reset_depth(void);

/* Entries with an entry shim (es2_sim_entry.entry_shim, set by
 * es2_sim_register_adapters). The handler continues at the shim instead of the
 * trampoline; the shim moves registers and the stack as under the proxy and
 * reaches the trampoline through its bridge (es2_sim_trap_entry). The exception
 * delivery has written below the caller's ESP, which the shims read as the
 * original's stack residue, so the ES2_SIM_SHIM_ZERO bytes there are set to the
 * residue table's zero model first (own_process.md sections 4.4 and 5). */
#define ES2_SIM_SHIM_ZERO 4096u
extern void es2_sim_trap_entry(void);
/* 1 while the trap is installed. */
int es2_sim_trap_active(void);
/* A machine call of `va` from own C code (es2_sim_call of an entry with a
 * shim, which only the trap can enter): the slots pushed, EAX/EDX/ECX from
 * regs; EAX/EDX/ECX after the call into out. */
void es2_sim_trap_call(uint32_t va, const uint32_t *slots, uint32_t count, const uint32_t regs[3], uint32_t out[3]);

/* The VA of the entry the trapped call `outward` levels out of the innermost
 * one runs (0: the innermost; the present row's attribution of own-code
 * callers, present.c), 0 past the outermost. */
uint32_t es2_sim_trap_entry_va(unsigned outward);

/* Stage 2 of the call path (own_process.md section 4.4 item 2 and "the
 * bridge seam for the hot VAs"): the compile-time bridge seam. The adapter bridges that
 * make the hot machine calls (weapons_machine_call, weapons_exception_call,
 * ai_behaviour_stack_call, math_guest_call, math_effects_call; built with
 * ES2_SIM_HOST in //sim:adapters only, the proxy's bytes unchanged) and
 * es2_sim_trap_call pass their target through es2_sim_bridge_target right
 * before the CALL. For a bridged VA it sets the state the handler would set
 * for the fault at that VA (the pending key, the pending shim) and returns
 * the address the handler would continue at (the trampoline, or the zeroing
 * stub of an entry with a shim); the CALL then reaches it with the caller's
 * registers, flags and stack exactly as the fault would, without the
 * exception. Any other VA comes back unchanged and faults as before. The
 * entry counts the call in `calls` and `bridges` (not `traps`); the global
 * es2_sim_trap_calls counts faults only. Only the trap's thread is bridged. */
#define ES2_SIM_BRIDGE_MAX 64u
/* The bridged VAs, sorted ascending, each an entry of the dispatch in a range
 * keyed by its address; after es2_sim_trap_install. 0, or 1 (nothing is
 * bridged then). es2_sim_trap_remove clears the set. */
int es2_sim_bridge_set(const uint32_t *vas, unsigned count);
unsigned es2_sim_bridge_count(void);
uint32_t __cdecl es2_sim_bridge_target(uint32_t va);
/* Calls that came through the bridge so far. */
uint64_t es2_sim_bridge_calls(void);

#endif
