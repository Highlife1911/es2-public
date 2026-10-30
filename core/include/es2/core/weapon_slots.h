#ifndef ES2_CORE_WEAPON_SLOTS_H
#define ES2_CORE_WEAPON_SLOTS_H
#include "es2/core/weapons.h"
/* The player weapon system per rendered frame and the range-gauge
 * state behind it (docs/reverse-engineering/weapons.md, section "player weapon
 * system"). W is a weapon system ([P+202h]): dword W+4 the slot list, word W+8
 * the slot count, bytes W+14h/16h/18h, dword W+31h, word table at W+0Ah, fire
 * group byte W+1Ch, selected slot s8 W+1Dh, group records at W+1Fh+6g. H is the
 * range gauge ([[49B088h]+1E1h], vtable 49C85Ch): word H+B1h (fire group
 * shown), bytes H+B3h (lamp) and H+B4h (link), buttons at H+18h, button count
 * dword H+68h.
 *
 * Every function returns the original's EDX:EAX and stores the original's
 * ECX in *ecx (the adapters' exit shims load it). Game code (the weapon
 * virtuals +2Ch/+50h/+60h, weapon_range 40e5f8, select_step 41074c,
 * fire_group 410ae4, weapon_select 410708, the lamp 441fa4 and the other
 * functions of this module) is called through g->call with the EAX/EDX/ECX
 * the original holds there; before each call the core stores in *saved the
 * EBX, ESI and EDI the original holds there (on entry *saved holds the
 * caller's, which the original passes on where it has not set its own).
 * g->image is the loaded image base: the words 4d2526/4d2528, the HUD root
 * 49b088 and the gauge copy 4d1a5c are read or written at image + RVA. */
typedef struct es2_weapon_slots_saved { uint32_t ebx, esi, edi; } es2_weapon_slots_saved;

/* weapon_frame 410b40 (W, x): the per-frame step: select_step(W, 1) when
 * word 4d2528 is set and 4d2526 clear (then byte W+18h <- 0); dword W+31h
 * <- x; ready_step; the gauge copy (gauge_state) decides the fire group
 * (fire_group, or the gauge record takes s8 W+1Ch when byte W+16h is set,
 * which is cleared), byte W+14h (latch_set or 0) and the link toggle
 * (link_toggle); gauge_sync(H, record); then for each listed weapon E the
 * link exchange (bytes E+4Bh of E and its partner, weapon_select),
 * chain_ready(W, E, i, 0) and E.V50(E, selected, member, ready). The
 * 8-byte record (the original's local at EBP-8) is placed at `record`
 * (guest address, 8 bytes). */
uint64_t es2_weapon_frame_registers(const es2_guest_memory *, const es2_weapon_graph *, es2_weapon_slots_saved *saved,
    uint32_t weapons, uint32_t range, uint32_t record, const uint32_t entry[3], uint32_t *ecx);
/* ready_step 410a3c (W): while the selected weapon is not usable
 * (selected_ready), select_step(W, 1) up to count + 1 times; restores the
 * old selection through weapon_select when it comes back; byte W+18h <- 0
 * when a usable weapon is found. */
uint64_t es2_ready_step_registers(const es2_guest_memory *, const es2_weapon_graph *, es2_weapon_slots_saved *saved,
    uint32_t weapons, const uint32_t entry[3], uint32_t *ecx);
/* selected_ready 410a04 (W, E, i): AX = 1 when byte W+18h or word 4d2526
 * is set, else chain_ready(W, E, i with the entry ECX's high word, 0). */
uint64_t es2_selected_ready_registers(const es2_guest_memory *, const es2_weapon_graph *, es2_weapon_slots_saved *saved,
    uint32_t weapons, uint32_t weapon, uint32_t index, const uint32_t entry[3], uint32_t *ecx);
/* chain_ready 410970 (W, E, i, f): E.V2C, weapon_range(E, W+31h) when that
 * dword is set, E.V60(E, 0) with the word table W+0Ah, then for f = 0 and
 * byte E+4Bh set chain_ready(W, partner, i + definition byte, 1). EAX = 0
 * or AX = 1 (or the recursion's). */
uint64_t es2_chain_ready_registers(const es2_guest_memory *, const es2_weapon_graph *, es2_weapon_slots_saved *saved,
    uint32_t weapons, uint32_t weapon, uint32_t index, uint32_t flag, const uint32_t entry[3], uint32_t *ecx);
/* latch_set 410f04 (W): byte W+14h <- 1; EAX = W+14h, EDX and ECX kept. */
uint64_t es2_latch_set_registers(const es2_guest_memory *, uint32_t weapons, const uint32_t entry[3]);
/* link_toggle 410f14 (W): when the selected weapon has a nonzero definition
 * byte, its partner slot holds a weapon and both carriers have the same
 * word +56h: byte E+4Bh of both flipped. No call. */
uint64_t es2_link_toggle_registers(const es2_guest_memory *, uint32_t weapons, const uint32_t entry[3], uint32_t *ecx);
/* gauge_sync 4420c8 (H, R): word R -> H+B1h (and gauge_mode) when they
 * differ, the lamp 441fa4 when byte H+B3h differs from R+2, gauge_link(H,
 * R+3) when byte H+B4h differs from R+3. */
uint64_t es2_gauge_sync_registers(const es2_guest_memory *, const es2_weapon_graph *, es2_weapon_slots_saved *saved,
    uint32_t gauge, uint32_t record, const uint32_t entry[3], uint32_t *ecx);
/* gauge_state 442120 (H): copies word H+B1h, bytes H+B3h/B4h to
 * image+D1A5Ch..D1A5Fh and returns that address. No call. */
uint64_t es2_gauge_state_registers(const es2_guest_memory *, uint32_t image, uint32_t gauge, const uint32_t entry[3],
    uint32_t *ecx);
/* gauge_link 442054 (H, b): byte H+B4h <- b, then link_toggle of the
 * player's weapon system [[[49B088h]+203h]+202h]; ECX = that W. */
uint64_t es2_gauge_link_registers(const es2_guest_memory *, const es2_weapon_graph *, es2_weapon_slots_saved *saved,
    uint32_t gauge, uint32_t value, const uint32_t entry[3], uint32_t *ecx);
/* gauge_button 442154 (H, x), slot 0 of the gauge's vtable: finds x among
 * the dwords H+18h.. (count H+68h); button 0 cycles word H+B1h (0..2) and
 * calls gauge_mode, button 1 gauge_link(H, 1), button 2 the lamp 441fa4. */
uint64_t es2_gauge_button_registers(const es2_guest_memory *, const es2_weapon_graph *, es2_weapon_slots_saved *saved,
    uint32_t gauge, uint32_t button, uint32_t *ecx);
/* gauge_mode 441f90 (H, m): word H+B1h <- m; EAX = H, EDX = the entry
 * EDX's high word over m, ECX kept. */
uint64_t es2_gauge_mode_registers(const es2_guest_memory *, uint32_t gauge, uint32_t mode, const uint32_t entry[3]);
/* member_toggle 410cd0 (W, i): flips bit 0 of the current group's
 * membership word of slot s16 i. EAX = W, EDX = s16 i. */
uint64_t es2_member_toggle_registers(const es2_guest_memory *, uint32_t weapons, uint32_t index, uint32_t *ecx);
#endif
