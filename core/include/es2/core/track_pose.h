#ifndef ES2_CORE_TRACK_POSE_H
#define ES2_CORE_TRACK_POSE_H
#include "es2/core/weapons.h"
/* Track seek 0x479344 and the building turret aim 0x403d5c.
 * Every game callee, including the hooked ones (track_clip, multiply_q8,
 * approach_word, track_seek, actor_animate), is called through g->call with
 * the original entry registers; 47e0c0 (Q14 word product) is computed here.
 * g->image is the image base (0x400000); g->scratch is unused. Contracts:
 * player_movement.md, "Track and pose remainder". */

/* 0x479344(T, clip, fraction): put track T at the frame and phase of clip
 * (low word) that lie at fraction/4000h (low word) of the clip's total
 * frame duration, via 0x4792ac. The frame walk has no bound in the
 * original; it must end inside the clip's frame records (caller fractions
 * 0..3fffh and nonnegative durations do). Returns 0x4792ac's EDX:EAX. */
uint64_t es2_track_seek_registers(const es2_guest_memory *, const es2_weapon_graph *,
    uint32_t track, uint32_t clip, uint32_t fraction);

/* 0x403d5c(B, P): for both turret axes, turn the rate B+0x209+2i toward the
 * clamped target from the words P+0/P+4 (0x47e0a0 twice, 0x467adc), add it
 * to the angle B+0x20d+2i within the image table limits, set the turret
 * track [B+0x1f9+4i] to that angle (0x479344); then 0x402604(B, 100), whose
 * EDX:EAX is returned. */
uint64_t es2_turret_aim_registers(const es2_guest_memory *, const es2_weapon_graph *,
    uint32_t building, uint32_t angles);
#endif
