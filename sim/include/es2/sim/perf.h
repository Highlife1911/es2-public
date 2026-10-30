/* es2sim's per-frame wall time and dispatch counts (own_process.md, "es2sim's frame
 * times"). Off by default; ES2SIM_PERF switches it on (es2_sim_perf_path). It only reads
 * the wall clock and the dispatch's counters and writes its own file: no guest memory, no
 * virtual time, no tick-log line changes.
 *
 * The file is NDJSON, one object per line:
 *   {"perf":"es2sim","version":1,"mode":"headless"|"window","clock_hz":F,"setup_us":S,
 *    "setup_traps":T,"setup_calls":C}
 *       once, at the first logic frame (S: from the recorder's start, the
 *       process's set-up, to the first frame's start: start-up and load;
 *       T, C: the counters at that point);
 *   {"f":N,"us":U,"work":W,"wait":A,"traps":T,"calls":C}
 *       per frame, at its tick record (N the frame counter before the
 *       increment). U: wall time since the previous frame's tick (frame 0:
 *       since its logic started); W: from the logic call to the tick (logic,
 *       camera, render, present, stage 6); A: time the window layer slept in
 *       this frame (the limiter's pace and Sleep(100) of a pause; always 0
 *       headless); T: trapped calls (unconverted calls, es2/sim/trap.h: the
 *       first ends the run); C: dispatched calls (the sum of the entries'
 *       counters);
 *   {"end":"<reason>","frames":N,"us":U,"traps":T,"calls":C,"hot":[["0x…",traps,calls],…]}
 *       once at the end (U: from the first frame's start; hot: the
 *       ES2_SIM_PERF_HOT entries with the most trapped calls over the whole
 *       run, set-up included, each with its trap and call count). */
#ifndef ES2_SIM_PERF_H
#define ES2_SIM_PERF_H

#include <stdint.h>
#include "es2/sim/dispatch.h"

#define ES2_SIM_PERF_HOT 24

typedef struct es2_sim_perf {
    int enabled;
    /* The clock (es2_os_counter by default; tests replace it). */
    uint64_t (*now)(void *user);
    void *now_user;
    uint64_t hz;
    /* The sink: n bytes of text; 0 on success (tests replace it). */
    int (*write)(void *user, const char *text, unsigned n);
    void *write_user;
    const char *mode;
    /* State. */
    uint64_t start, first, last, work_begin, wait_begin, wait;
    uint64_t traps0, calls0, traps, calls;
    uint64_t frames;
    int header_done, in_work, in_wait, failed;
} es2_sim_perf;

/* The switch: `value` is ES2SIM_PERF. Unset, empty or "0": off (returns 0).
 * "1": next to the tick log (its directory + "perf.ndjson"), or
 * "es2sim-perf.ndjson" in the current directory without one. Anything
 * else: that file. Returns 1 with the path in `out`; 0 when off or when
 * the path does not fit. */
int es2_sim_perf_path(const char *value, const char *tick_log, char *out, unsigned capacity);

/* A recorder over the given clock and sink; `mode` names the build's run
 * ("headless" or "window"). Starts the set-up interval. */
void es2_sim_perf_init(es2_sim_perf *p, const char *mode, uint64_t (*now)(void *), void *now_user, uint64_t hz,
                       int (*write)(void *, const char *, unsigned), void *write_user);
/* The logic call of a frame starts (the loop's before_logic); `traps` and
 * `calls` are the running totals (the first call takes them as the base). */
void es2_sim_perf_work(es2_sim_perf *p, uint64_t traps, uint64_t calls);
/* The window layer's sleep starts / ends. */
void es2_sim_perf_wait_begin(es2_sim_perf *p);
void es2_sim_perf_wait_end(es2_sim_perf *p);
/* A frame's tick: `traps` and `calls` are the running totals. */
void es2_sim_perf_frame(es2_sim_perf *p, uint32_t frame, uint64_t traps, uint64_t calls);
/* The end line with the hottest entries of `d` (may be NULL). */
void es2_sim_perf_end(es2_sim_perf *p, const char *reason, uint64_t traps, uint64_t calls, const es2_sim_dispatch *d);

/* The dispatched calls so far: the sum of every entry's counter. */
uint64_t es2_sim_perf_dispatch_calls(const es2_sim_dispatch *d);

/* The host binding: es2_os_counter and a file created (replaced)
 * at `path`. 0 on success; 1 (recorder stays off) when the file cannot be
 * created. */
int es2_sim_perf_open(es2_sim_perf *p, const char *mode, const char *path);
void es2_sim_perf_close(es2_sim_perf *p);

#endif
