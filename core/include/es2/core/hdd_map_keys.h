#ifndef ES2_CORE_HDD_MAP_KEYS_H
#define ES2_CORE_HDD_MAP_KEYS_H
#include "es2/core/weapons.h"
/* The HDD map key path of the HDDMap object M (slot 4 of the table
 * 0x49d9d8, offered by hdd_key 0x449ff4) and the simulation-side callees it
 * reaches, plus the map frame's wingman pick. Contract in
 * docs/reverse-engineering/cockpit.md, section "HDD map key path".
 *
 * Addresses are guest addresses; g->image is the loaded image base
 * (0x400000 in the original), used for the globals 0x49b088 (graphics
 * context), 0x49d6b0 (the map gadget holder), 0x4a9af8/0x4a9afe (the actor
 * list and its count word) and the wingman table 0x4d03c8. Every game call
 * goes through g->call with the EAX/EDX/ECX the original holds at that call
 * and is reported to g->observe first (slot 0), so a hooked callee keeps its
 * own mode. `entry` holds the entry EAX/EDX/ECX. Each function returns the
 * original's EDX:EAX; ECX is dead at every caller and not reproduced. ECX
 * after a POP ECX that follows a one-argument call is the pushed argument. */

/* map_key 0x44cc68 (M, code): by the low word of code (jump tables
 * 0x44cc8c/0x44cccb): D/A/F/T/G/O/C/E (20h 1Eh 21h 14h 22h 18h 2Eh 12h) ->
 * map_press(M, [M+35h], 0Ah..11h); X/backspace (2Dh/0Eh) -> button_press
 * 0x438d9c([M+8], 0Dh/0Eh, 1); '.'/',' (34h/33h) -> mode_next/mode_prev;
 * Enter (1Ch) with byte M+103h set: in mode 0Bh/0Ch with a target [M+15Ch]
 * map_press(M, [M+39h], 1), without one in mode 0Ch, and in 0Dh/0Eh,
 * map_press(M, [M+39h], 0); Tab (0Fh) with byte M+103h set: in mode 0Bh/0Ch
 * [M+15Ch] <- target_cycle1/target_cycle0(M); a target goes into the selected
 * wingman's order record (+1Ch target, +26h = 1, byte +21h = 1), byte M+104h
 * <- 1, 0x44dd14(M), sfx_play(1Ah), no target sfx_play(1Bh); keypad 5 (4Ch)
 * clears [M+114h]/[M+118h]. Handled keys return AL = 1, the others EAX = 0. */
uint64_t es2_map_key_registers(const es2_guest_memory *, const es2_weapon_graph *, uint32_t map,
    uint32_t code, const uint32_t entry[3]);
/* map_press 0x44d5c0 (M, gadget, mode): byte M+15Ah <- 1; gadget [M+35h]:
 * the event point is mode button mode-0Ah's top left + (1, 1); gadget
 * [M+39h]: mode 0: the cursor 0x486ece less the context offsets +210h, plus
 * +220h; mode 1: the map view [M+14h] centred on M+18h..M+20h (0x48c2e4,
 * 0x48c444), the target [M+15Ch]+26h projected (0x48c57c, 0x48c5fc, y
 * negated) and placed on the map (0x44d24c); then the event 0x44d3cc(M,
 * gadget, &point). The locals live at the original's offsets below
 * `frame` (frame-24h..frame-1, at least 24h accessible guest bytes). */
uint64_t es2_map_press_registers(const es2_guest_memory *, const es2_weapon_graph *, uint32_t frame,
    uint32_t map, uint32_t gadget, uint32_t mode, const uint32_t entry[3]);
/* wingman_pick 0x44d82c (M): map_pick 0x44d888(M, M+11Dh); a living
 * (byte +99h clear) wingman (wingman_index != -1) -> 0x44a748([[0x49d6b0]+8],
 * index); then byte M+11Ch <- 0, byte 0x49d6ae <- 1. */
uint64_t es2_wingman_pick_registers(const es2_guest_memory *, const es2_weapon_graph *, uint32_t map,
    const uint32_t entry[3]);
/* wingman_index 0x433134 (actor): the index 0..2 of actor in the table
 * 0x4d03c8 (EDX = the entry's address), else -1 (EDX = 0x4d03d4); a null
 * actor gives -1 with the entry EDX. No game call (g->call unused). */
uint64_t es2_wingman_index_registers(const es2_guest_memory *, const es2_weapon_graph *, uint32_t actor,
    const uint32_t entry[3]);
/* mode_next 0x44ee48 / mode_prev 0x44ee88 (M): with a mode [M+5Dh] != -1,
 * map_mode 0x44d9f4(M, next/previous of 0Ah..11h, cyclic) and the map
 * gadget's redraw request. */
uint64_t es2_mode_next_registers(const es2_guest_memory *, const es2_weapon_graph *, uint32_t map,
    const uint32_t entry[3]);
uint64_t es2_mode_prev_registers(const es2_guest_memory *, const es2_weapon_graph *, uint32_t map,
    const uint32_t entry[3]);
/* target_cycle1 0x44ef08 / target_cycle0 0x44ef1c (M): target_cycle(M, 1/0). */
uint64_t es2_target_cycle1_registers(const es2_guest_memory *, const es2_weapon_graph *, uint32_t map,
    const uint32_t entry[3]);
uint64_t es2_target_cycle0_registers(const es2_guest_memory *, const es2_weapon_graph *, uint32_t map,
    const uint32_t entry[3]);
/* target_cycle 0x44ef30 (M, side): the next actor after the current target
 * [M+15Ch] (when its side byte [[T+45h]+12h] matches side's low word,
 * otherwise from the list start) in the list [0x4a9af8] of count word
 * [0x4a9afe] (signed), wrapping once at the end, for which 0x44f03c(M,
 * actor, side) returns AL != 0; EAX = that actor, else 0. */
uint64_t es2_target_cycle_registers(const es2_guest_memory *, const es2_weapon_graph *, uint32_t map,
    uint32_t side, const uint32_t entry[3]);
#endif
