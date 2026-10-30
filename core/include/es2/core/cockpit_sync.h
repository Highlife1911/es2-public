#ifndef ES2_CORE_COCKPIT_SYNC_H
#define ES2_CORE_COCKPIT_SYNC_H
#include "es2/core/weapons.h"
/* The cockpit<->player synchronisation 0x41b130(P) and its three
 * small callees (docs/reverse-engineering/hud.md, section "cockpit<->player
 * synchronisation as own code").
 *
 * es2_cockpit_sync_registers: the energy pool word P+292h in Q10 into
 * 4A9CE8h and the EnergyPoolGauge 4450f8; shield_balance(P+222h); the
 * target distance (dist_gate, from the view subject 4D2684h when 49EF34h
 * is set and P is the player 4D24E6h); weapon_frame([P+202h], d); the
 * throttle record 447df8 read back into P+290h (flyer chassis also
 * P+2D7h) unless byte P+93h hands the input's set point to the gauge; the
 * speed ratio and set point into 447da8; the HUD record 43dbb0 copied and
 * patched (pitch, heading/twist unless byte P+0A4h, target, P+9Bh); the
 * cockpit target [H+210h] copied into P+1A4h with the reference counts
 * +1A2h, bytes P+9Bh/P+9Dh, 40e484 for the weapon [P+30Bh], the countdown
 * word P+31Ch <- 4500 and timer_word_step(P+31Bh) -> word 4D2504h; the
 * HUD record set 43d9b4; aim_point and 434a24. Returns 434a24's EDX:EAX;
 * ECX is dead at both callers and not reproduced.
 *
 * Every game call goes through g->call (observed first, by its original VA and
 * slot 0) with the EAX/EDX/ECX the original holds there, so a hooked callee
 * keeps its own mode. g->image is the loaded image base (globals and callees
 * are relocated by it). g->scratch is the frame: the original's locals
 * EBP-40h..EBP-1 (ES2_COCKPIT_SYNC_FRAME bytes; the frame's EBP is g->scratch +
 * 40h); the addresses passed to callees are frame addresses, and the bytes the
 * original leaves unwritten (the aim point, the words at EBP-0Ah/-0Ch without a
 * target) are read as they are, so a caller that wants the original's residue
 * puts the frame at the original stack coordinate.
 *
 * frame (NULL when the bridge needs none of it): before every game call
 * the core stores there the EBX, ESI and EDI that 0x41b130 holds at that
 * call (EBX = P, ESI = the caller's ESI, then [P+1A4h]+26h on the target
 * path, then the cockpit target; EDI = the caller's) and the return
 * address of its CALL (an image VA relative to g->image); the adapter's
 * plain bridge loads them too. With frame->top nonzero (the
 * original's outgoing ESP, EBP-48h) ECX after a one-slot call is re-read
 * from the argument slot top-4 (the original's POP ECX); otherwise it is
 * the pushed argument. Precondition (the original faults): the chassis
 * speed limit words [[P+1F2h]+4] (negated) and +6 are nonzero. */
#define ES2_COCKPIT_SYNC_FRAME 0x40u
typedef struct es2_cockpit_sync { es2_weapon_frame call; uint32_t top; } es2_cockpit_sync;
uint64_t es2_cockpit_sync_registers(const es2_guest_memory *, const es2_weapon_graph *, es2_cockpit_sync *frame,
    uint32_t caller_esi, uint32_t caller_edi, uint32_t player, const uint32_t entry[3]);
/* 0x447da8(G, L): the ThrottleGauge record set. When the set point [L+4]
 * differs from [G+0B5h]: the slider [G+18h]'s class-table slot +14h
 * (class table at [S+17h]) with (S, entry EDX with DX <- word [L+4]),
 * EAX = the class table, ECX = S (through g->call, observed with slot
 * 14h); when [L] differs from [G+0B1h]: byte [[G+18h]+1Fh] <- 2; then
 * G+0B1h <- [L], G+0B5h <- [L+4]. Returns EDX = [L], EAX = [L+4]. */
uint64_t es2_throttle_gauge_registers(const es2_guest_memory *, const es2_weapon_graph *, uint32_t gauge,
    uint32_t record, const uint32_t entry[3]);
/* 0x447df8(G) and 0x43dbb0(G): EAX = G + 0B1h, EDX = the entry EDX; no
 * memory access. */
uint64_t es2_throttle_record_registers(uint32_t gauge, const uint32_t entry[3]);
uint64_t es2_hud_record_registers(uint32_t hud, const uint32_t entry[3]);
#endif
