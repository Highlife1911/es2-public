#ifndef ES2_CORE_GROUP_JOINS_H
#define ES2_CORE_GROUP_JOINS_H
#include "es2/core/weapons.h"
/* The group joins of group_ctor 423b34 (the members' placement
 * during the mission load) and the building footprint marks. Contract in
 * docs/reverse-engineering/mission_load.md, section "group joins and the
 * building footprint". Addresses are guest addresses; g->image is
 * the loaded image base (0x400000 in the original). Every game call goes
 * through g->call with the original's entry EAX/EDX/ECX and is reported to
 * g->observe first (reference VA and slot 0; a virtual call reports its
 * target and slot), so a callee that is itself a hook keeps its own mode.
 * `entry` holds the entry EAX/EDX/ECX. Each function returns the
 * original's EDX:EAX and, when `ecx` is not NULL, stores the original's ECX
 * there (ECX is dead at every caller). sx() below is the sign extension of
 * a word, lo(R, w) a 16-bit load that keeps R's high word. */

/* The 12-byte vector of a join (the original's EBP-14h, building EBP-18h)
 * and the point footprint_marks hands to dist_gate (EBP-1Ch) lie at
 * g->scratch, which needs ES2_GROUP_JOINS_SCRATCH bytes. */
#define ES2_GROUP_JOINS_SCRATCH 0x10u

/* mech_join 417aa8 (A, G, slot), the MECH class's join called by
 * group_ctor per member: [A+45h] = G, word [A+49h] = slot. When word
 * [A+0Ch] is 8000h: the angle words A+0Ch, +0Eh = 0, +10h = word [G+0Ah]
 * (G reread from A+45h), word [A+32h] = 0. When [A+2Eh] is FFFF8000h: the
 * vector v = the group point [G+18h] or, without one, the point
 * route_point 423b0c(G+4, word [G+4]) returns; v.z = terrain_height
 * 46e180([4A0BE0h], v) + sx(word [T+16h]) (+ 1388h when word [T+50h] is
 * not 0; T = [A+1F2h]); the formation slot +78h of A's vtable (A, v); then
 * A+26h = v (+8, +0, +4). */
uint64_t es2_mech_join_registers(const es2_guest_memory *, const es2_weapon_graph *, uint32_t actor, uint32_t group,
    uint32_t slot, const uint32_t entry[3], uint32_t *ecx);
/* flyer_join 421ee8 (F, G, slot): as mech_join without the height (no
 * terrain_height, no type words); afterwards, on every path, [F+2Eh] = 1388h
 * when it is 0. */
uint64_t es2_flyer_join_registers(const es2_guest_memory *, const es2_weapon_graph *, uint32_t flyer, uint32_t group,
    uint32_t slot, const uint32_t entry[3], uint32_t *ecx);
/* building_join 405c3c (B, G, slot, snap): as mech_join, but the angle word
 * +10h adds, for a nonzero slot, the layout word [[[4A95ACh] + 1Ch x
 * sx(word [G+2]) + 4] + 0Ah x sx(slot) - 2]; route_point's second slot is
 * ECX with the low word [G+4]; when snap (the dword) is not 0 the point is
 * moved to the base layout's grid (s = [L+14h] << 5, m = (s << 8) - 1:
 * x' = (x & ~m) + [L+0Ch] x s, y' = (y & ~m) + m + 1 - [L+10h] x s, low
 * dwords; L = [4A95ACh] + 1Ch x sx(word [G+2])); then the slot +78h (B, v),
 * the shape word of slot +10h (B), footprint_marks 470ed4([4A0BE0h], v,
 * sx(that word)), v.z = terrain_height([4A0BE0h], v) and B+26h = v. */
uint64_t es2_building_join_registers(const es2_guest_memory *, const es2_weapon_graph *, uint32_t building,
    uint32_t group, uint32_t slot, uint32_t snap, const uint32_t entry[3], uint32_t *ecx);
/* footprint_marks 470ed4 (T, P, r): with s = [T+108h] (low five bits; SAR
 * and SHL by CL), the cell box x0 = (Px - r) >> s .. x1 = (Px + r) >> s,
 * y0 = (Py - r) >> s .. y1 = (Py + r) >> s (arithmetic shifts); first
 * cell_flag_set 470ddc(T, Px >> s, Py >> s, 1), then per row y = y0 .. y1
 * and column x = x0 .. x1 (signed, inclusive, [T+108h] reread) the point
 * (x << s, y << s, Pz) and, when dist_gate 49288c(point, P) <= r (signed),
 * the cells (x, y), and (x - 1, y), (x - 1, y - 1) for x > 0 and (x, y -
 * 1) for y > 0, each set to 1 through 470ddc. */
uint64_t es2_footprint_marks_registers(const es2_guest_memory *, const es2_weapon_graph *, uint32_t terrain,
    uint32_t point, uint32_t radius, const uint32_t entry[3], uint32_t *ecx);
/* cell_flag_set 470ddc (T, x, y, v): byte [[T+0F0h] + (y << [T+100h]) + x]
 * = v (SHL by CL). EAX = [T+0F0h], EDX = the index, ECX = [T+100h] with v
 * in its low byte. No game calls. */
uint64_t es2_cell_flag_set_registers(const es2_guest_memory *, uint32_t terrain, uint32_t x, uint32_t y,
    uint32_t value, const uint32_t entry[3], uint32_t *ecx);
#endif
