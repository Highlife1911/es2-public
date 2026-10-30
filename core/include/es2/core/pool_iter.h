#ifndef ES2_CORE_POOL_ITER_H
#define ES2_CORE_POOL_ITER_H
#include "es2/core/weapons.h"
/* The object-pool iterator (docs/reverse-engineering/main_loop.md,
 * section "pool iterator 471c70 / 471c18"): pool_next 0x471c18 and
 * pool_prev 0x471c70.
 *
 * A pool header P has its used-list head node at P+0Eh; a node N is the
 * 8-byte link header before its object (next [N], previous [N+4]); the
 * object pointer is N+8. Game code is called through g->call with the
 * original's entry EAX/EDX/ECX: pool_prev's steps 0x471c18 (so a nested
 * pool_next keeps its mode) and pool_next's debug report, the CW3220
 * sprintf thunk 0x496796, whose buffer is g->scratch
 * (ES2_POOL_ITER_SCRATCH bytes). Both return the original's EDX:EAX; ECX
 * after return is the entry ECX in the original too, except after the
 * debug report.
 * Precondition (the original loops forever): the used list reached by
 * pool_prev(P, 0) has no cycle. */
#define ES2_POOL_ITER_SCRATCH 0x80u
/* pool_next 471c18 (P, O): the object after O in P's used list (the head
 * for O = 0), 0 at the end. EDX = 0 for O = 0, else O-8 (or the report's
 * EDX when O-8 < 100000h). */
uint64_t es2_pool_next_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t pool,uint32_t object,
    const uint32_t entry[3]);
/* pool_prev 471c70 (P, O): the object before O (O != 0; EDX = entry EDX),
 * or for O = 0 the last object of the used list, found by stepping
 * pool_next from the head (EDX = the last step's). */
uint64_t es2_pool_prev_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t pool,uint32_t object,
    const uint32_t entry[3]);
#endif
