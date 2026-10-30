/* Five leaves reached only from hook adapters
 * (own_process.md, "Called gap roots"), as service rows of
 * sim/dispatch/services/adapter_leaves.h. "Listing" = llvm-objdump -d -M intel
 * of the reference DBSIM.EXE (SHA-256 8561aa47...c4ad). The first four read and
 * write guest memory through the bound loop's guest view, in the listing's
 * order, and leave EAX, EDX and ECX as the original does. The fifth,
 * 0x467ad8, is a breakpoint that ends the process as the original's
 * unhandled faults do (own_process.md, "The original's fault paths"). */
#include <stdint.h>
#include "es2/sim/loop.h"
#include "es2/sim/memory.h"
#include "es2/sim/dispatch.h"

void es2_sim_service_camera_mode(es2_sim_service_call *sc, void *user);
void es2_sim_service_flyer_slot(es2_sim_service_call *sc, void *user);
void es2_sim_service_sweep_clear(es2_sim_service_call *sc, void *user);
void es2_sim_service_sweep_read(es2_sim_service_call *sc, void *user);
void es2_sim_service_debug_break(es2_sim_service_call *sc, void *user);

#define VA_FLYER_TABLE 0x004a9dfcu   /* dword: the base of the 18-byte type rows */
#define VA_SWEEP 0x004aaad0u         /* dword */
#define VA_DEBUG_BREAK 0x00467ad8u
#define BREAKPOINT 0x80000003u       /* EXCEPTION_BREAKPOINT */

static int bound(es2_sim_service_call *sc, es2_sim_loop **l) {
    *l = es2_sim_loop_bound();
    if (!*l) { sc->refused = 1; sc->reason = "no main loop bound"; return 1; }
    return 0;
}
static uint32_t r32(const es2_sim_loop *l, uint32_t a) { return l->memory->read32(l->memory->context, a); }
static void w32(const es2_sim_loop *l, uint32_t a, uint32_t v) { l->memory->write32(l->memory->context, a, v); }
static void w16(const es2_sim_loop *l, uint32_t a, uint16_t v) { l->memory->write16(l->memory->context, a, v); }

/* Original: DBSIM.EXE 0x401148(C, mode) (listing 0x401148..0x40119e), the
 * camera's mode word C+0x36 <- mode for mode 0..3 unless dword C+0x4a is
 * non-zero (read again on each branch). EAX C; EDX the entry's when +0x4a
 * is set, otherwise movsx(mode) - 1 minus one per step of the chain (mode
 * 0: -1, 1..3: 0, any other: mode - 3); ECX the entry's. */
void es2_sim_service_camera_mode(es2_sim_service_call *sc, void *user) {
    es2_sim_loop *l;
    (void)user;
    if (bound(sc, &l)) return;
    uint32_t c = sc->slots[0];
    int32_t mode = (int16_t)(uint16_t)sc->slots[1];
    sc->eax = c;
    if (r32(l, c + 0x4au)) return;
    if (mode < 0 || mode > 3) { sc->edx = (uint32_t)(mode - 3); return; }
    sc->edx = mode == 0 ? 0xffffffffu : 0u;
    if (r32(l, c + 0x4au)) return;
    w16(l, c + 0x36u, (uint16_t)mode);
}

/* Original: DBSIM.EXE 0x423044(type, slot) (listing 0x423044..0x423067),
 * the flyer slot address [0x4a9dfc] + 18*movsx(type) + 6*movsx(slot) - 6.
 * EAX the address, EDX movsx(slot), ECX 6*movsx(slot). */
void es2_sim_service_flyer_slot(es2_sim_service_call *sc, void *user) {
    es2_sim_loop *l;
    (void)user;
    if (bound(sc, &l)) return;
    uint32_t type = (uint32_t)(int32_t)(int16_t)(uint16_t)sc->slots[0];
    uint32_t slot = (uint32_t)(int32_t)(int16_t)(uint16_t)sc->slots[1];
    sc->edx = slot;
    sc->ecx = 6u * slot;
    sc->eax = 18u * type + r32(l, VA_FLYER_TABLE) + 6u * slot - 6u;
}

/* Original: DBSIM.EXE 0x427d98 (listing 0x427d98..0x427d9f): dword 0x4aaad0
 * <- 0, EAX 0. */
void es2_sim_service_sweep_clear(es2_sim_service_call *sc, void *user) {
    es2_sim_loop *l;
    (void)user;
    if (bound(sc, &l)) return;
    sc->eax = 0;
    w32(l, VA_SWEEP, 0);
}

/* Original: DBSIM.EXE 0x427da0 (listing 0x427da0..0x427da5): EAX dword
 * 0x4aaad0. */
void es2_sim_service_sweep_read(es2_sim_service_call *sc, void *user) {
    es2_sim_loop *l;
    (void)user;
    if (bound(sc, &l)) return;
    sc->eax = r32(l, VA_SWEEP);
}

/* Original: DBSIM.EXE 0x467ad8 (listing 0x467ad8..0x467ad9): int3; ret.
 * Reached from the playback keys 0x45fe6c on Ctrl+B with the option word
 * 0x4d252c (-B). No handler of DBSIM takes EXCEPTION_BREAKPOINT, so without
 * a debugger the process ends with that code at 0x467ad8, as on a divide
 * fault; with the record's process_fault es2sim ends there, without it the
 * row refuses. */
void es2_sim_service_debug_break(es2_sim_service_call *sc, void *user) {
    (void)user;
    sc->refused = 1;
    sc->reason = es2_sim_loop_fault(es2_sim_loop_bound(), BREAKPOINT, VA_DEBUG_BREAK)
                     ? "the process ended at the original's breakpoint"
                     : "breakpoint (int3) without a process record";
}
