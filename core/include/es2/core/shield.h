#ifndef ES2_CORE_SHIELD_H
#define ES2_CORE_SHIELD_H
#include "es2/core/weapons.h"
/* The shield balance step 413bc8 with its helper 413af8, called once
 * per rendered frame from the cockpit/player synchronisation 41b130 with
 * s = player + 222h. Exact contract: docs/reverse-engineering/cockpit.md
 * (sections "0x413bc8(S) contract (simulation)" and "shield balance
 * as own code"). The cockpit pointer 49b088 is reached through g->image.
 * g->scratch must hold 16 writable guest bytes: the 15-byte gauge record L
 * that the gauge setter 443880 receives by pointer. 443880 runs through
 * g->call with the registers the original holds at 413c56 (EAX = L,
 * ECX = gauge, EDX = cockpit), so it stays original code; the getter
 * 443908 and the step 413af8 are computed here (an applied step is
 * reported to g->observe as 413af8). Returns EDX:EAX of 443880; EDX and
 * ECX are dead at the only caller. A zero divisor word s+8 raises the
 * original's fault (es2_divide_raise). */
uint64_t es2_shield_balance_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t s);
#endif
