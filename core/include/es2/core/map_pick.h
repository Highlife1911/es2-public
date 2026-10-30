#ifndef ES2_CORE_MAP_PICK_H
#define ES2_CORE_MAP_PICK_H
#include "es2/core/weapons.h"
/* The HDD map pick 44d888 with its helper 44d288 computed inside.
 * Contract in docs/reverse-engineering/cockpit.md (section "map pick
 * 44d888"). Addresses are guest addresses; `image` is the loaded image
 * base (0x400000 in the original), used for the two display shift words
 * 4d253a/4d253c and the actor list 4a9af8 with its count word 4a9afe.
 *
 * map_pick 44d888 (map, point): converts the screen point to world
 * coordinates, stores them at map+374h..+37Fh (the only writes, in the
 * original's order) and returns the first listed actor whose position
 * +26h/+2Ah lies strictly inside the pick square around that point.
 * EDX:EAX as the original: a hit returns EAX = the actor and EDX = the
 * stored y map+378h; no hit returns EAX = 0 and EDX with the counter word
 * of the last loop test in the low half and the high half of the value
 * EDX held before that test. ECX (the actor or 0) is dead after both
 * callers and is not reproduced; no entry register is read. */
uint64_t es2_map_pick_registers(const es2_guest_memory *, uint32_t image, uint32_t map, uint32_t point);
/* The HDD map click 44d6e0 (map), the handler of a pending click
 * (contract in cockpit.md, section "map click 44d6e0"). Calls the
 * map pick 44d888 (M, M+11Dh), then by the map mode M+5Dh stores the
 * picked actor (modes 0Bh/0Ch with the wanted side byte [[T+45h]+12h]) or
 * the stored point M+374h (modes 0Ch/0Dh/0Eh) into the selected wingman's
 * record M+85h+2Ah*[M+105h] and calls the sound 462830 (14h) and the
 * label update 44dd14 (M); always clears M+11Ch and sets the byte 49d6ae.
 * Game calls go through g->call (g->image = the loaded image base) with
 * the original's entry registers; entry[1]/entry[2] (EDX/ECX) are passed
 * on to 44d888. Returns the original's EDX:EAX (44dd14's after a store,
 * else EAX = the picked actor and EDX from the mode tests); ECX is dead at
 * the only caller and not reproduced. */
uint64_t es2_map_click_registers(const es2_guest_memory *, const es2_weapon_graph *, uint32_t map,
    const uint32_t entry[3]);
#endif
