/* Virtual time of the oracle (own_process.md section 7; contract in
 * docs/reverse-engineering/timing.md "Virtual time"). The state machine that
 * answers DBSIM's GetTickCount reads (thunk 0x4968ec), shared by the proxy
 * (tools/reference-game/proxy/virtual_time.h, which keeps the IAT wrapper,
 * the configuration and the wall-clock pacing) and the own process (sim/).
 * Pure C17: no Win32, no clock, explicit state.
 *
 * A limiter poll (the call at 0x4678d0, return address 0x4678d5) receives
 * previous + step, where previous is the limiter's clock [0x4d3b74]; it never
 * moves the clock backwards. Every other read returns the current clock;
 * with `intra` every read after the first since the last poll advances the
 * clock by 1 ms, at most step - 1 times per frame; past `stall_limit` reads
 * without a poll each read advances it by 1 ms (the stall guard). The frame
 * boundary gives a frame that made no poll the step a poll would have given
 * it (tape playback skips the limiter). The counters are compared fields of
 * the tick record (limiter_polls, stall_advances) or diagnostics. */
#ifndef ES2_SIM_ORACLE_VIRTUAL_TIME_H
#define ES2_SIM_ORACLE_VIRTUAL_TIME_H
#include <stdint.h>

#define ES2_VT_MIN_STEP 40u                 /* the limiter's own threshold (0x4678cd: add ebx, 0x28) */
#define ES2_VT_DEFAULT_START 0x01000000u
#define ES2_VT_DEFAULT_STALL_LIMIT 100000u

typedef struct es2_vt {
    int enabled, intra;
    uint32_t step, clock_ms, stall_limit;
    uint32_t since_limiter, stall_advances, limiter_polls, late_polls, intra_advances, intra_frame;
    uint32_t step_base, boundary_polls, bypass_steps;   /* clock after the last step; polls at the last boundary */
    int boundary_seen;
} es2_vt;

/* Sets the parameters (counters are left as they are): enabled only with a
 * step of at least ES2_VT_MIN_STEP; a stall limit of 0 selects the default. */
void es2_vt_enable(es2_vt *vt, int enabled, uint32_t step, uint32_t start, uint32_t stall_limit, int intra);

/* A limiter poll with the limiter's previous clock. `pace` (may be NULL) runs
 * after the clock is set and before the poll is counted, as the proxy's
 * wall-clock pacing does; it must not change the clock. */
typedef void (*es2_vt_pace)(void *user);
uint32_t es2_vt_limiter_poll(es2_vt *vt, uint32_t previous, es2_vt_pace pace, void *user);

/* Any other read (the clock wrapper 0x467828, timing init 0x467883, a modal
 * loop's reads). */
uint32_t es2_vt_read(es2_vt *vt);

/* Once per main-loop frame, before the frame counter increment. */
void es2_vt_frame_boundary(es2_vt *vt);

#endif
