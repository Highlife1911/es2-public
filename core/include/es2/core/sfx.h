#ifndef ES2_CORE_SFX_H
#define ES2_CORE_SFX_H
#include "es2/core/weapons.h"
/* The sound API calls the simulation makes (docs/reverse-engineering/
 * audio.md, section "sound/clock isolation"). Own code does what the
 * comparison can see: the context test [0x49f8ec], the row selection with
 * the RNG stream B draw rng_bounded(0x4d260b, variants) for rows with more
 * than one variant (before the effect gate, once per call), the effect gate
 * of 0x462784, the volume and pan arithmetic and the positional distance
 * test. What the player hears stays original: every call into the SFX
 * manager (0x464618 volume, 0x464038 play, 0x4646dc pan, 0x464118 stop,
 * 0x46466c rate) and the 3D view helpers 0x48c444/0x48c57c go through
 * g->call as game code, with the arguments and the EAX/EDX/ECX the original
 * holds there; rng_bounded 0x492f24, dist3d_delta 0x47de72 and angle_ratio
 * 0x47d32c are called the same way, so nested hooks keep their modes. None
 * of these functions reads the clock 0x467828 (audio.md).
 *
 * Tables: row pointers 0x4d2b78[i] (bytes +1 volume, +4/+5 near/far in
 * units of 1024, +6 variants, +8 volume percent), sample handles
 * 0x4d2c78[i]; the index wraps to 32 bits as the original's scaled
 * address does and is never range-checked. The gate 0x462784 and the
 * product 0x47e08d are pure and computed inline.
 *
 * Every function returns EDX:EAX exactly as the original and stores the
 * original's ECX at return in *ecx (the adapter's exit shim returns it).
 * entry[] holds the caller's EAX/EDX/ECX. g->scratch: 12 guest bytes for
 * the view-space position of es2_sfx_place_registers (the original's
 * EBP-18h); the other functions do not use it. The six API calls read no
 * clock; the timeline functions at the end do, through 0x467828. */

/* Original: DBSIM.EXE 0x462830 play(i) */
uint64_t es2_sfx_play_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t index,const uint32_t entry[3],uint32_t *ecx);
/* Original: DBSIM.EXE 0x4628e0 play_at(i, position) */
uint64_t es2_sfx_play_at_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t index,uint32_t position,const uint32_t entry[3],uint32_t *ecx);
/* Original: DBSIM.EXE 0x46299c place(i, position): distance volume and pan;
 * EAX = 1 when audible. */
uint64_t es2_sfx_place_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t index,uint32_t position,const uint32_t entry[3],uint32_t *ecx);
/* Original: DBSIM.EXE 0x46297c refresh(i, position) of a running loop */
uint64_t es2_sfx_refresh_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t index,uint32_t position,const uint32_t entry[3],uint32_t *ecx);
/* Original: DBSIM.EXE 0x462ac4 stop(i) */
uint64_t es2_sfx_stop_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t index,const uint32_t entry[3],uint32_t *ecx);
/* Original: DBSIM.EXE 0x463114 rate(i, rate) */
uint64_t es2_sfx_rate_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t index,uint32_t rate,const uint32_t entry[3],uint32_t *ecx);

/* The voice/lip timeline of the cockpit radio (the clock-reading half of
 * the sound code). Channel record: +0 sample handle, +4 next event time,
 * word +8 lip value (-1 = end), +0Ah script pointer, +2Fh script buffer.
 * Event queue: count word 0x49f964, record pointers 0x4d2ec8[k], records
 * 0x4d2e76 + 8*k (time, channel). Each clock read is one g->call of the
 * wrapper 0x467828 at the original's place, so the virtual clock sees the
 * same calls; the voice's sample still plays through 0x464038.
 * Original: DBSIM.EXE 0x463400 voice(ch): script pointer = buffer, time =
 * clock, 0x4634b0(ch), then the play when there is a context, the voice
 * byte [0x49f966], a lip value and a handle; otherwise AL = 1. */
uint64_t es2_sfx_voice_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t channel,const uint32_t entry[3],uint32_t *ecx);
/* Original: DBSIM.EXE 0x4634b0 lip(ch): read the clock, step the script
 * (lip byte, signed time step) while the clock is ahead of the event time,
 * store the lip value and insert the next event into the queue (always at
 * position 0, as the original's loop never advances). */
uint64_t es2_sfx_lip_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t channel,const uint32_t entry[3],uint32_t *ecx);
/* Original: DBSIM.EXE 0x4634a0 lip_due(ch): 0x4634b0(ch) for the queue
 * pass 0x4632c4. */
uint64_t es2_sfx_lip_due_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t channel,const uint32_t entry[3],uint32_t *ecx);
#endif
