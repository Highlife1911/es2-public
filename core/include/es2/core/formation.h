#ifndef ES2_CORE_FORMATION_H
#define ES2_CORE_FORMATION_H
#include "es2/core/weapons.h"
/* The formation-slot virtual +78h of the actor classes, which adds
 * the actor's rotated slot offset to a point P in place:
 *   formation_base  405c04 (BASE, RADAR_BASE, GUN_BASE, VEHICLE, LC_BASE)
 *                   with the slot helper 405b9c (static buffer 4975d6),
 *   formation_mech  417898 (MECH) with 4205cc (static buffer 49a430),
 *   formation_flyer 421e98 (FLYER) with 423044 (pointer into the table),
 * all three through the placement step 411d64. Exact contract:
 * docs/reverse-engineering/ai.md, section "Formation slot virtual
 * +78h". The slot tables 4a95ac/4a9d6c/4a9dfc and the static buffers are
 * reached through g->image. euler_matrix 47ebb8 and rotate_2d 47ffe8 run
 * through g->call with the original's entry registers (nested hooks keep
 * their modes); the helpers and 411d64 are computed here and reported to
 * g->observe by their address without a game call. g->scratch must hold
 * ES2_FORMATION_SCRATCH writable guest bytes: the rotated offset R (411d64's
 * EBP-10h) and, for MECH and FLYER, the slot point Q (their EBP-0Ch).
 * entry[] is EAX/EDX/ECX at entry; on the slot-0 paths they are returned
 * (MECH/FLYER with EAX's low word cleared). Returns EDX:EAX as the
 * original; ECX, EAX and EDX are dead at all seven call sites. */
#define ES2_FORMATION_SCRATCH 48u
uint64_t es2_formation_base_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t actor,uint32_t point,const uint32_t entry[3]);
uint64_t es2_formation_mech_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t actor,uint32_t point,const uint32_t entry[3]);
uint64_t es2_formation_flyer_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t actor,uint32_t point,const uint32_t entry[3]);
#endif
