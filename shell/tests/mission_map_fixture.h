#ifndef ES2_SHELL_TESTS_MISSION_MAP_FIXTURE_H
#define ES2_SHELL_TESTS_MISSION_MAP_FIXTURE_H
/* A synthetic mission for the map (no game data, no EXE tables): the
 * script, player.mec, mforms.dat, a 4 x 4 height grid, a computed cosine
 * table, a dummy arc-tangent table and 26 plain icons. */
#include "es2/shell/gui/mission_map.h"

#include <string.h>

#include <math.h>

static uint8_t script[4096];
static size_t script_size;
static void w16(int16_t v) { script[script_size++] = (uint8_t)v; script[script_size++] = (uint8_t)((uint16_t)v >> 8); }
static void w32(int32_t v) { w16((int16_t)(v & 0xffff)); w16((int16_t)((uint32_t)v >> 16)); }
static void zeros(size_t n) { memset(script + script_size, 0, n); script_size += n; }
static void put16at(size_t at, int16_t v) { script[at] = (uint8_t)v; script[at + 1] = (uint8_t)((uint16_t)v >> 8); }

/* Not VSHELL's data: four coordinates, one route over three of them, three
 * bases (a visible class A, a visible side-1 class B, a hidden class C), one
 * order, one actor group of two members. */
static void make_script(int16_t group_count, int16_t formation) {
    script_size = 0;
    w16(0); w16(1); zeros(16);                       /* header: zone 1 */
    w16(4);                                          /* coordinates */
    static const int32_t c[4][3] = {{0, 0, 0}, {100000, 50000, 0}, {300000, 200000, 0}, {500000, 100000, 0}};
    for (int i = 0; i < 4; i++) { w32(c[i][0]); w32(c[i][1]); w32(c[i][2]); }
    w16(1); w16(90);                                 /* angles */
    w16(1); w16(3); w16(1); w16(2); w16(3);          /* one route: children 1, 2, 3 */
    w16(0); w16(0); w16(0);                          /* regions, triggers, timed triggers */
    w16(2); zeros(2 * 134);                          /* mechs */
    w16(0);                                          /* flyers */
    w16(3);                                          /* bases */
    static const int16_t base[3][3] = {{5, 1, 1}, {30, 2, 1}, {50, 3, 0}};
    for (int i = 0; i < 3; i++) {
        size_t at = script_size;
        zeros(52);
        put16at(at, base[i][0]);
        put16at(at + 2, base[i][1]);
        put16at(at + 0x1a, base[i][2]);
    }
    w16(1);                                          /* orders */
    {
        size_t at = script_size;
        zeros(14);
        put16at(at + 2, formation);
        put16at(at + 4, -1);
        put16at(at + 6, 0);
    }
    w16(group_count);                                /* actor groups */
    for (int g = 0; g < group_count; g++) {
        size_t at = script_size;
        zeros(156);
        put16at(at + 0x2c, -1);
        put16at(at + 0x2e, -1);
        put16at(at + 0x30, -1);
        for (int k = 0; k < 20; k++) put16at(at + 0x32 + 2 * k, k < 2 ? (int16_t)k : -1);
        put16at(at + 0x5a, 0);
    }
    w16(0); w16(0);                                  /* objectives, string indices */
}

static uint8_t mec[4] = {0, 0, 2, 0};
static uint8_t forms[2 + 28];
static uint8_t grid[16];
static uint8_t cosine[ES2_MAP_COSINE_SIZE], atan_table[ES2_MAP_ATAN_SIZE];
static uint8_t icon_pixels[ES2_MAP_ICONS][64];
static es2_bitmap icons[ES2_MAP_ICONS];

static es2_mission_map_inputs inputs(void) {
    es2_mission_map_inputs in;
    memset(&in, 0, sizeof in);
    forms[0] = 1;
    for (int k = 0; k < 7; k++) { forms[2 + 4 * k] = 0x20; forms[3 + 4 * k] = 0xb1; forms[4 + 4 * k] = 0x30; forms[5 + 4 * k] = 0xf8; }
    for (int i = 0; i < 16; i++) grid[i] = (uint8_t)(i * 9);
    for (int i = 0; i <= 0x400; i++) { /* a synthetic cosine table (not the EXE's) */
        int16_t v = (int16_t)lround(16384.0 * cos(i * 3.14159265358979 / 2048.0));
        cosine[2 * i] = (uint8_t)v;
        cosine[2 * i + 1] = (uint8_t)((uint16_t)v >> 8);
    }
    for (int i = 0; i < ES2_MAP_ATAN_SIZE; i++) atan_table[i] = (uint8_t)(i % 4);
    for (int i = 0; i < ES2_MAP_ICONS; i++) {
        memset(icon_pixels[i], 40 + i, sizeof icon_pixels[i]);
        icons[i] = (es2_bitmap){8, 8, 0, icon_pixels[i]};
    }
    in.script = (es2_span){script, script_size};
    in.player_mec = (es2_span){mec, sizeof mec};
    in.mforms = (es2_span){forms, sizeof forms};
    in.cell_shift = 18;
    in.grid_width = 4;
    in.grid_height = 4;
    in.grid_bytes = (es2_span){grid, sizeof grid};
    in.material_count = 3;
    in.material_level = 6;
    in.mission_lines = 2;
    in.icons = icons;
    in.icon_count = ES2_MAP_ICONS;
    in.cosine = (es2_span){cosine, sizeof cosine};
    in.atan = (es2_span){atan_table, sizeof atan_table};
    return in;
}

#endif
