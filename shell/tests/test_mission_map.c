/* Unit tests of es2/shell/gui/mission_map.h on synthetic state. */
#include "es2/shell/gui/mission_map.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures, checks;
#define CHECK(c)                                                              \
    do {                                                                      \
        checks++;                                                             \
        if (!(c)) { failures++; fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); } \
    } while (0)

static uint8_t pixels[64 * 48];
static es2_map_surface surface = {pixels, 64, 48, 64};

static es2_map_dc dc_new(void) {
    es2_map_dc dc;
    memset(&dc, 0, sizeof dc);
    dc.surface = &surface;
    dc.clip_mode = 1;
    dc.right = 63;
    dc.bottom = 47;
    dc.screen_height = 48;
    return dc;
}

static int count(uint8_t c) {
    int n = 0;
    for (size_t i = 0; i < sizeof pixels; i++) n += pixels[i] == c;
    return n;
}

static void test_raster(void) {
    es2_map_dc dc = dc_new();
    memset(pixels, 0, sizeof pixels);
    dc.pen = 7;
    es2_map_line(&dc, 2, 3, 12, 3);
    CHECK(count(7) == 11);
    dc.pen = 0x04000007; /* a ramp colour: refused */
    es2_map_line(&dc, 2, 5, 12, 5);
    CHECK(count(7) == 11);
    memset(pixels, 0, sizeof pixels);
    dc.brush = 9;
    es2_map_rect(&dc, 10, 10, 19, 14);
    CHECK(count(9) == 50);
    dc.left = 15;
    memset(pixels, 0, sizeof pixels);
    es2_map_rect(&dc, 10, 10, 19, 14);
    CHECK(count(9) == 25);
    /* a square polygon: the span builder covers both end columns and rows */
    dc = dc_new();
    memset(pixels, 0, sizeof pixels);
    dc.brush = 5;
    int32_t sq[8] = {4, 4, 9, 4, 9, 9, 4, 9};
    es2_map_polygon(&dc, 4, sq);
    CHECK(count(5) == 36);
    /* one colour level: the whole triangle as one polygon */
    memset(pixels, 0, sizeof pixels);
    int32_t tri[6] = {0, 10, 0, 0, 10, 0};
    uint16_t same[3] = {3, 3, 3};
    es2_map_triangle(&dc, tri, same);
    CHECK(count(3) > 50);
    /* degenerate: two equal vertices draw nothing */
    memset(pixels, 0, sizeof pixels);
    int32_t degenerate[6] = {5, 5, 5, 5, 9, 1};
    es2_map_triangle(&dc, degenerate, same);
    CHECK(count(3) == 0);
    /* the 7 x 7 ellipse */
    memset(pixels, 0, sizeof pixels);
    dc.brush = 14;
    es2_map_ellipse(&dc, 17, 17, 23, 23);
    CHECK(count(14) > 20 && count(14) < 49);
    /* zero radius: refused */
    memset(pixels, 0, sizeof pixels);
    es2_map_ellipse(&dc, 17, 17, 17, 23);
    CHECK(count(14) == 0);
}

/* ---- the map on a synthetic mission ------------------------------------------ */

#include "mission_map_fixture.h"

static uint8_t frame[640 * 480];

static int outside_changed(void) {
    for (int y = 0; y < 480; y++)
        for (int x = 0; x < 640; x++)
            if ((x < ES2_MAP_LEFT || x > ES2_MAP_RIGHT || y < ES2_MAP_TOP || y > ES2_MAP_BOTTOM) && frame[y * 640 + x] != 0xee)
                return 1;
    return 0;
}

static void test_map(void) {
    es2_mission_map m;
    es2_mission_map_globals g;
    memset(&m, 0, sizeof m);
    memset(&g, 0, sizeof g);
    for (int i = 0; i < 56; i++) g.material_rng.words[i] = (uint16_t)(i * 1237 + 11);
    g.material_rng.a = 24;
    g.material_rng.b = 55;
    make_script(1, 0);
    es2_mission_map_inputs in = inputs();
    CHECK(es2_mission_map_zone(in.script) == 1);
    CHECK(es2_mission_map_build(&m, &in, &g) == 0);
    CHECK(m.built && !m.refused);
    /* the bounding box: the coordinates widened by 10,000 */
    CHECK(m.bbox_min[0] == -10000 && m.bbox_min[1] == -10000 && m.bbox_max[0] == 510000 && m.bbox_max[1] == 210000);
    /* the overview: centre, altitude max((dx << 7) / 147, (dy << 7) / 112) */
    CHECK(m.overview[0] == 250000 && m.overview[1] == 100000);
    CHECK(m.overview[2] == (260000 << 7) / 147 && m.max_altitude == m.overview[2]);
    CHECK(m.player_route == 0 && m.squad_count == 2);
    /* member 0 at the route's first point, member 1 offset by the formation */
    CHECK(m.squad[0][0] == 100000 && m.squad[0][1] == 50000);
    CHECK(m.squad[1][0] != 100000 || m.squad[1][1] != 50000);
    /* the image: cells x -1 .. 2, y -1 .. 1, k = min(640 / 4, 400 / 3) */
    CHECK(m.image_width == 4 * 133 && m.image_height == 3 * 133);
    /* the material draws advanced the RNG */
    CHECK(g.material_rng.a != 24);
    CHECK(m.step == 20000);
    /* the fly-over: paints inside the map rectangle only, ends at state 0x13 */
    memset(frame, 0xee, sizeof frame);
    uint32_t t = 100;
    int steps = 0;
    /* The first step's busy waits are counted, not performed: the
     * step's tick moves on by them (the host waits until its clock gets
     * there); the next step has none */
    CHECK(es2_mission_map_step(&m, &g, t, frame) == 1);
    CHECK(m.wait_ticks > 20 && g.tick == t + m.wait_ticks && !m.first_paint);
    t = g.tick + 1;
    steps++;
    CHECK(es2_mission_map_step(&m, &g, t, frame) == 1 && m.wait_ticks == 0 && g.tick == t);
    t = g.tick + 1;
    steps++;
    while (es2_mission_map_step(&m, &g, t, frame) && steps < 5000) { t = g.tick + 1; steps++; }
    CHECK(steps < 5000 && m.state == 0x13 && m.done);
    CHECK(m.squad_shown == 2 && m.route_shown == 3 && m.markers_shown == 2);
    CHECK(!outside_changed());
    CHECK(frame[180 * 640 + 438] != 0xee);
    /* the first step's busy waits: 10 + 10 + one per grid line */
    /* arrows */
    CHECK(es2_mission_map_arrow_steps(0) == 1 && es2_mission_map_arrow_steps(3) == 2 &&
          es2_mission_map_arrow_steps(8) == 3 && es2_mission_map_arrow_steps(9) == 4);
    int32_t z = m.view[2];
    es2_mission_map_arrow_step(&m, &g, ES2_MAP_ZOOM_IN, frame);
    CHECK(m.view[2] == z - 50000);
    es2_mission_map_arrow_step(&m, &g, ES2_MAP_ZOOM_OUT, frame);
    CHECK(m.view[2] == z); /* back to the overview's altitude */
    es2_mission_map_arrow_step(&m, &g, ES2_MAP_ZOOM_OUT, frame);
    CHECK(m.view[2] == z); /* the clamp refuses more */
    /* at the overview the clamp holds the view: north does not move the pan */
    int32_t pan = m.pan[1];
    es2_mission_map_arrow_step(&m, &g, ES2_MAP_NORTH, frame);
    CHECK(m.pan[1] == pan);
    /* zoomed in, north moves the pan by the step */
    for (int i = 0; i < 6; i++) es2_mission_map_arrow_step(&m, &g, ES2_MAP_ZOOM_IN, frame);
    pan = m.pan[1];
    es2_mission_map_arrow_step(&m, &g, ES2_MAP_NORTH, frame);
    CHECK(m.pan[1] == pan + m.step);
    es2_mission_map_arrow_step(&m, &g, ES2_MAP_SOUTH, frame);
    CHECK(m.pan[1] == pan);
    int32_t pan_x = m.pan[0];
    es2_mission_map_arrow_step(&m, &g, ES2_MAP_EAST, frame);
    es2_mission_map_arrow_step(&m, &g, ES2_MAP_WEST, frame);
    CHECK(m.pan[0] == pan_x);
    CHECK(!outside_changed());
    /* the projection: the camera's ground point is the centre */
    int32_t gp[3] = {m.camera[0], m.camera[1], 0}, x, y;
    CHECK(es2_mission_map_project(&m, gp, &x, &y) && x == 438 && y == 180);
    int32_t up[3] = {m.camera[0], m.camera[1] + 100000, 0};
    CHECK(es2_mission_map_project(&m, up, &x, &y) && x == 438 && y < 180); /* north is up */
    /* skip: a new map plays the zoom-out only */
    CHECK(es2_mission_map_build(&m, &in, &g) == 0 && m.state == 0);
    es2_mission_map_step(&m, &g, 5000, frame);
    es2_mission_map_skip(&m);
    CHECK(m.state == 0x11);
    es2_mission_map_step(&m, &g, 5001, frame);
    CHECK(m.state == 0x12 && m.squad_shown == 2 && m.route_shown == 3);
    m.state = 0x13;
    es2_mission_map_skip(&m);
    CHECK(m.state == 0x13);
    es2_mission_map_free(&m);
    CHECK(!m.built && !m.image);

    /* refusals */
    make_script(0, 0); /* no actor group: asserted */
    in = inputs();
    CHECK(es2_mission_map_build(&m, &in, &g) == -1 && !m.built);
    make_script(1, 5); /* a formation past mforms.dat's records: asserted */
    in = inputs();
    CHECK(es2_mission_map_build(&m, &in, &g) == -1);
    make_script(1, 0);
    in = inputs();
    in.script.size = 200; /* truncated */
    CHECK(es2_mission_map_build(&m, &in, &g) == -1);
    in = inputs();
    in.cosine.size = 10; /* no tables */
    CHECK(es2_mission_map_build(&m, &in, &g) == -1);
    in = inputs();
    in.grid_height = 2; /* fewer grid rows than columns: rows past the cells, refused */
    in.grid_bytes.size = 8;
    CHECK(es2_mission_map_build(&m, &in, &g) == 0 && m.refused);
    es2_mission_map_free(&m);
    CHECK(es2_mission_map_build(NULL, &in, &g) == -1);
    es2_mission_map_step(NULL, &g, 0, frame);
    es2_mission_map_paint(NULL, &g, frame);
}

/* es2_mission_map_wait reads the clock until its tick (ms / 16) is
 * no longer below the target, as 0x425be3..0x425c04. The fake clock moves
 * by `step` ms per read. */
typedef struct fake_clock { uint32_t ms, step, reads; } fake_clock;
static uint32_t fake_read(void *context) {
    fake_clock *c = context;
    uint32_t now = c->ms;
    c->ms += c->step;
    c->reads++;
    return now;
}

static void test_wait(void) {
    /* from 100 ticks + 7 ms to 152 ticks, 3 ms per read: the first read at
     * or past 152 * 16 = 2432 ms is the 276th (1607 + 3 * 275 = 2432) */
    fake_clock c = {100u * 16u + 7u, 3, 0};
    CHECK(es2_mission_map_wait(152, fake_read, &c) == 276 && c.reads == 276);
    CHECK((c.ms - 3) / 16 == 152 && (c.ms - 6) / 16 == 151);
    /* the target already reached: one read, as the original's first compare
     * against the last reading */
    fake_clock d = {152u * 16u, 1, 0};
    CHECK(es2_mission_map_wait(152, fake_read, &d) == 1);
    fake_clock e = {200u * 16u + 15u, 1, 0};
    CHECK(es2_mission_map_wait(152, fake_read, &e) == 1);
    /* one tick: up to the next boundary */
    fake_clock f = {152u * 16u + 15u, 1, 0};
    CHECK(es2_mission_map_wait(153, fake_read, &f) == 2);
    /* no clock: nothing is waited */
    CHECK(es2_mission_map_wait(152, NULL, NULL) == 0);
}

int main(void) {
    test_raster();
    test_map();
    test_wait();
    printf("mission_map_test: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
