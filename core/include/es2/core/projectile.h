#ifndef ES2_CORE_PROJECTILE_H
#define ES2_CORE_PROJECTILE_H
#include "es2/core/weapons.h"
/* Projectile and bullet steering. Contracts:
 * docs/reverse-engineering/weapons.md, "Projectile and bullet steering".
 * All functions return EDX:EAX. `corrections` is the 513-byte ratio
 * correction table of angle_ratio (0x4a1c34). */

/* 0x49290c: angle of (x, y); (0, 0) is treated as (1, 0). Same domain as
 * angle_ratio (y != 0 with an INT_MIN argument faults in the original). */
uint64_t es2_vector_angle_registers(uint32_t x, uint32_t y, const void *corrections);
/* The ECX the original returns: the divisor register of 0x47d32c. */
uint32_t es2_vector_angle_ecx(uint32_t x, uint32_t y);
/* 0x492990: writes the words pitch (out+0), 0 (out+2) and heading (out+4)
 * of the direction start -> end in the original's read/write order (out may
 * overlap the points). EAX = out. The original's ECX is end.x - start.x read
 * before any write. */
uint64_t es2_aim_angles_registers(const es2_guest_memory *, uint32_t out, uint32_t end, uint32_t start,
    const void *corrections);
/* 0x40a254 and 0x40aff0: one homing step of a projectile (target [O+56h])
 * or a bullet (target [O+5Bh]). Every game callee (virtual +24/+58,
 * 47ebb8, 48043c, 492990, 467adc, 467924) goes through g->call with the
 * original's registers. g->scratch is the original's 28h-byte frame: at
 * least 40 accessible guest bytes, disjoint from game state; the locals
 * EBP-x live at scratch + 28h - x. entry[] holds EAX/EDX/ECX at entry. */
uint64_t es2_projectile_steer_registers(const es2_guest_memory *, const es2_weapon_graph *,
    uint32_t object, const uint32_t entry[3]);
uint64_t es2_bullet_steer_registers(const es2_guest_memory *, const es2_weapon_graph *,
    uint32_t object, const uint32_t entry[3]);
#endif
