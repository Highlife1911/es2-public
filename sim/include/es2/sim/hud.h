/* The HUD and cockpit line of DBSIM without its drawing (hud.md, cockpit.md,
 * display_modes.md, "transitions, slides, jolt, bump and the shield palette in
 * the HUD line" and hud.md, "the HUD line in the own process"): the HUD and
 * cockpit construction calls of the
 * mission load (src/hud_load.c: state yes, pixels no) and the per-frame HUD
 * control flow with every coupling call (clock wrapper 0x467828, RNG stream B
 * 0x492ee0/0x492f24, the sound API) in the original order (src/hud_state.c).
 * Both are service rows of sim/dispatch/services.h; guest state is read and
 * written only through the bound main loop's guest view; every call of other
 * DBSIM code goes through the dispatch.
 *
 * Headless the pixel work of every drawing entry is not done; what such an
 * entry writes outside the pixels (heap objects, counters, globals the
 * control flow or the simulation reads) is. Each skipped piece is counted
 * in `headless` and named in its function's comment. */
#ifndef ES2_SIM_HUD_H
#define ES2_SIM_HUD_H
#include <stdint.h>
#include "es2/sim/loop.h"

/* One coupling call of the HUD line (hud.md, "the HUD line contract — coupling
 * set and frame writes"): the clock, a stream-B draw, a sound-API call, in the
 * order the own code made them. */
typedef enum es2_sim_hud_kind {
    ES2_SIM_HUD_CLOCK = 'C',
    ES2_SIM_HUD_STREAM_B = 'B',
    ES2_SIM_HUD_SOUND = 'A'
} es2_sim_hud_kind;

typedef struct es2_sim_hud_event {
    uint8_t kind;          /* es2_sim_hud_kind */
    uint32_t site;         /* the original's call site (the return address minus 5) */
    uint32_t target;       /* the VA called (0x467828, 0x492ee0, 0x492f24, a sound hook) */
    uint32_t argument;     /* the first argument slot (sound row, bound), 0 for the clock */
    uint32_t result;       /* EAX */
} es2_sim_hud_event;

typedef struct es2_sim_hud {
    es2_sim_loop *loop;
    /* The coupling trace (NULL: none kept; `trace_count` counts on). */
    es2_sim_hud_event *trace;
    unsigned trace_capacity, trace_count;
    /* Pixel work not done (one count per skipped drawing call). */
    unsigned headless;
    uint32_t refused_va;
} es2_sim_hud;

/* Zeroes the record over `loop`. */
void es2_sim_hud_init(es2_sim_hud *hud, es2_sim_loop *loop);
/* The instance the service rows use. Unbound, the rows use an own record
 * over es2_sim_loop_bound(). */
void es2_sim_hud_bind(es2_sim_hud *hud);

#endif
