#ifndef ES2_CORE_MECH_FLY_H
#define ES2_CORE_MECH_FLY_H
#include "es2/core/weapons.h"
/* The MECH "player fly" movement 0x4198f4(A), the +24h member of
 * the MECH state record "player fly", called only by the trampoline
 * mech_move 0x415afc (docs/reverse-engineering/hit_tests.md, the
 * section "MECH player fly movement"). A is the MECH; R =
 * A+2B9h its motion record (position dwords +0/+4/+8, velocity dwords
 * +0Ch/+10h/+14h, the pitch and roll rate words +18h/+1Ah).
 *
 * Order of work: the five one-time probe offsets (guard bytes 0x49a194,
 * 0x49a1a1, 0x49a1ae, 0x49a1bb, 0x49a1c8); the lazy matrix (euler_matrix
 * when word A+32h is 0); a copy of A's 32-byte pose at f-DCh and the five
 * probes through it; the velocity integration (three movement_step calls,
 * unless byte A+A4h); the segment at f-114h and the static sweep packet
 * 0x49a170 (+0 the segment, words +4/+6 8000, +8 0, +0Eh owner 0, word
 * +12h 5, +14h A; dword +0Ah is never written); the probe blocks of the
 * parts 8, 7 (terrain or sweep contact on the roll rate), 4, 5 (sweep
 * contact; both pass the part-4 point f-B0h to the part hit, as the
 * original does), the nose block (part 0, pitch rate; debris and the dead
 * byte A+A4h when its region flag is cleared), the slope block, the ground
 * clamp (part 6; debris when its region flag is cleared), the cockpit
 * sound for the player and weapon_converge(A, distance to the target).
 *
 * Every game call goes through g->call with the EAX/EDX/ECX the original
 * holds there: euler_matrix, transform_point, movement_step,
 * terrain_height, sweep, multiply_q10, mech_part_hit, debris_table,
 * debris_burst, the SOS calls 0x462ac4/0x463114/0x46297c (original),
 * dist3d_delta, dist_gate and weapon_converge (nested hooks keep their
 * modes). The frame lives in g->scratch (ES2_MECH_FLY_SCRATCH guest
 * bytes, synthetic EBP f = scratch + ES2_MECH_FLY_FRAME, every local at its
 * original EBP offset), so [0x49a170] receives the address of the segment
 * in that scratch instead of the original's stack address; it is read
 * only during the call (by the sweep). entry = EAX/EDX/ECX at entry (ECX
 * reaches euler_matrix when the fifth probe block did not run).
 *
 * Result: EDX:EAX exactly as the original leaves them (AX = 1, the high
 * word of EAX and EDX from weapon_converge). ECX is dead at the
 * trampoline. */
#define ES2_MECH_FLY_SCRATCH 320u
#define ES2_MECH_FLY_FRAME 304u
uint64_t es2_mech_fly_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t mech,const uint32_t entry[3]);
#endif
