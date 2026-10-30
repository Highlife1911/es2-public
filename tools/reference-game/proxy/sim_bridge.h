#ifndef ES2_SIM_BRIDGE_H
#define ES2_SIM_BRIDGE_H
/* Stage 2 of the own process's call path
 * (docs/reference-game/own_process.md, section 4.4 item 2 and "stage 2:
 * the bridge seam for the hot VAs"). In es2sim (//sim:adapters defines ES2_SIM_HOST) the bridges that make
 * the hot machine calls pass their target through es2_sim_bridge_target
 * (sim/include/es2/sim/trap.h) right before the CALL: a bridged VA comes back
 * as the trap's trampoline with the handler's pending state set, any other VA
 * unchanged, so the CALL reaches the dispatch without the execute fault and
 * with the same registers, flags and stack. Everywhere else (the proxy, the
 * in-vitro harness) both macros are empty and the bridges compile to the same
 * bytes as before.
 *
 * ES2_SIM_BRIDGE(va)  : for C bridges, the call target.
 * ES2_SIM_BRIDGE_ARG8 : for the naked asm bridges, right after their
 *                       prologue: replaces the cdecl argument at 8(%ebp)
 *                       (the target). Keeps the flags (pushfl/popfl) and
 *                       ESP; clobbers EAX, ECX and EDX, which those bridges
 *                       load afterwards. */
#ifdef ES2_SIM_HOST
#include <stdint.h>
uint32_t __cdecl es2_sim_bridge_target(uint32_t va);
#define ES2_SIM_BRIDGE(va) es2_sim_bridge_target(va)
#define ES2_SIM_BRIDGE_ARG8 \
    "pushfl\n\tpushl 8(%ebp)\n\tcall _es2_sim_bridge_target\n\taddl $4,%esp\n\tmovl %eax,8(%ebp)\n\tpopfl\n\t"
#else
#define ES2_SIM_BRIDGE(va) (va)
#define ES2_SIM_BRIDGE_ARG8 ""
#endif
#endif
