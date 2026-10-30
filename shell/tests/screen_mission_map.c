/* Frame hashes of the mission map (screen test harness).
 *
 * The map drawn into the shell's frame by the main loop's map step (the
 * hook es2_gui_hooks.map, VSHELL's 0x4019c6): one fly-over step 0x4262cf
 * per loop pass at a simulated tick (3 ticks a pass), the map rectangle
 * invalidated after each step so the next pass presents it. Synthetic
 * mission only (mission_map_fixture.h: no game data, no EXE tables); the
 * in-vitro equality with VSHELL's own map is //shell:mission_map_real_test.
 * States: the first frame (overview, outline drawn), the squad flight, the
 * route flight, the end of the fly-over, after a skip, and the arrows. No
 * capture-tour reference exists yet; the states a capture tour must capture are
 * listed in vshell_mission_map.md section 15. */
#include "screen_harness.h"

#include <stdlib.h>
#include <string.h>

#include "mission_map_fixture.h"

typedef struct mm_state {
    es2_mission_map map;
    es2_mission_map_globals g;
    uint32_t tick;
    int running, skip_at, arrows;
} mm_state;

static void map_step(void *context, es2_gui_shell *shell) {
    mm_state *st = context;
    if (!st->running) return;
    if (st->skip_at && st->tick >= (uint32_t)st->skip_at) {
        es2_mission_map_skip(&st->map);
        st->skip_at = 0;
    }
    st->running = es2_mission_map_step(&st->map, &st->g, st->tick, shell->frame.pixels);
    st->tick = st->g.tick + 3;
    es2_gui_rect r = {ES2_MAP_LEFT, ES2_MAP_TOP, ES2_MAP_RIGHT, ES2_MAP_BOTTOM};
    es2_gui_invalidate_window(&shell->frame, r);
    if (!st->running && st->arrows) {
        static const es2_mission_map_arrow seq[] = {ES2_MAP_ZOOM_IN, ES2_MAP_ZOOM_IN, ES2_MAP_ZOOM_IN, ES2_MAP_NORTH,
                                                    ES2_MAP_EAST, ES2_MAP_ZOOM_OUT};
        for (size_t i = 0; i < sizeof seq / sizeof seq[0]; i++)
            es2_mission_map_arrow_step(&st->map, &st->g, seq[i], shell->frame.pixels);
        st->arrows = 0;
    }
}

static int setup(es2_screen_session *s, void *state, int skip_at, int arrows) {
    mm_state *st = state;
    for (int i = 0; i < 56; i++) st->g.material_rng.words[i] = (uint16_t)(i * 1237 + 11);
    st->g.material_rng.a = 24;
    st->g.material_rng.b = 55;
    make_script(1, 0);
    es2_mission_map_inputs in = inputs();
    if (es2_mission_map_build(&st->map, &in, &st->g)) return 1;
    es2_screen_fixture_palette(s->shell->frame.records, 40);
    s->shell->frame.realize_gate = 1;
    es2_gui_palette_select(&s->shell->frame, (const uint8_t(*)[4])s->shell->frame.records);
    st->tick = 1000;
    st->running = 1;
    st->skip_at = skip_at;
    st->arrows = arrows;
    s->screen.context = st;
    s->screen.map = map_step;
    return 0;
}

static int setup_plain(es2_screen_session *s, void *state, const char *root) {
    (void)root;
    return setup(s, state, 0, 0);
}
static int setup_skip(es2_screen_session *s, void *state, const char *root) {
    (void)root;
    return setup(s, state, 1100, 0);
}
static int setup_arrows(es2_screen_session *s, void *state, const char *root) {
    (void)root;
    return setup(s, state, 0, 1);
}

static const char script_flight[] = "step 2\nsnap first\nstep 40\nsnap squad\nstep 80\nsnap route\nstep 400\nsnap end\n";
static const char script_skip[] = "step 80\nsnap skipped\n";
static const char script_arrows[] = "step 500\nsnap arrows\n";

/* our hashes; the reference stays NULL until a capture tour records VSHELL's
 * map */
#define T_SYN "33170693a865ceda8c966bd71ea83582ab4a14f77d3f02795eb3ea6c21f16323"
#define F_END "22351caf8d5bea9f886672a633c739139b7fe22a8d88acc5462993acf844a625"
static const es2_screen_expect expect_flight[] = {
    {"first", "6656cade5418c374b76cee4412ef8d4f58b735b5706e4aff4eb2fae97b7f185a", T_SYN, NULL},
    {"squad", "a31b593f9a921814b5bd643d97702f18a1b01ebaf2b572870ebb26c762231cf6", T_SYN, NULL},
    {"route", "18fb4b842df3558456c2b932018536e52b2bc2d6339c2af19a2ec839e8e7f0d9", T_SYN, NULL},
    {"end", F_END, T_SYN, NULL}};
/* the skip plays the zoom-out: the same final view */
static const es2_screen_expect expect_skip[] = {{"skipped", F_END, T_SYN, NULL}};
static const es2_screen_expect expect_arrows[] = {
    {"arrows", "fb01552c9fcd8cf68cc6f5841ddc7f706ff1c90f857a1409f6ae9d0f8d9f0e9f", T_SYN, NULL}};

#define CASE(name, setup, script, expect)                                                                         \
    {"mission_map." name, "mission", 0, sizeof(mm_state), setup, script, expect, sizeof(expect) / sizeof((expect)[0]), \
     NULL}
static const es2_screen_case cases[] = {
    CASE("flight", setup_plain, script_flight, expect_flight),
    CASE("skip", setup_skip, script_skip, expect_skip),
    CASE("arrows", setup_arrows, script_arrows, expect_arrows),
};

ES2_SCREEN_SUITE(mission_map, cases)
