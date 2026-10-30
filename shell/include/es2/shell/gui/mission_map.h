#ifndef ES2_SHELL_GUI_MISSION_MAP_H
#define ES2_SHELL_GUI_MISSION_MAP_H

/* The mission map of the MISSION screen as own drawing
 * (docs/reverse-engineering/vshell_mission_map.md, section 14 "Own
 * implementation"). Two layers:
 *
 *  - the raster layer: VSHELL's drawing context (the 0x239-byte context of
 *    vshell_presentation.md: clip mode, offset flag, rectangle, origin, pen and
 * brush) and the
 *    primitives the map draws with, each reproducing one original routine
 *    (pixel 0x459cdc, line 0x455624, polygon 0x455ad8 with its clipper
 *    0x455f58 and span builder 0x456340, the colour-banded triangle
 *    0x457de8, the textured quad 0x45399c, the ellipse 0x45886c, the
 *    rectangle 0x4576a4, the 1:1 bitmap 0x4537e8 and the scaled bitmap
 *    0x453908). Compared byte for byte with the originals in vitro
 *    (//shell:mission_map_real_test, slot-mode op map-raster).
 *  - the map: the object 0x40e75f builds (inputs from data\script.dat,
 *    data\player.mec, data\mforms.dat and the zone's height grid), the
 *    cached terrain image 0x427634, the paint 0x425a5e, the fly-over
 *    0x4262cf and the six arrows. The MISSION screen calls these
 *    functions; nothing here reads files or draws text.
 *
 * No Win32, no file I/O. Pixels outside the surface are never written (the
 * original has no such guard; where its arithmetic would leave the surface
 * this is a documented refusal, see section 15). */

#include <stddef.h>
#include <stdint.h>

#include "es2/shell/draw.h"

/* ---- raster layer ---------------------------------------------------------- */

/* The bound screen: rows of `stride` bytes ([0x47c482] row table,
 * [0x47c4a6] width, [0x47c4aa] height). */
typedef struct es2_map_surface {
    uint8_t *pixels;
    int32_t width, height, stride;
} es2_map_surface;

/* VSHELL's drawing context (fields of the 0x239-byte context,
 * vshell_presentation.md). */
typedef struct es2_map_dc {
    es2_map_surface *surface;
    int32_t clip_mode;           /* +0x208: 0 none, 1 the rectangle (2, region lists: refused) */
    int32_t offset;              /* +0x20c: 1 = coordinates relative to the origin */
    int32_t left, top, right, bottom; /* +0x210..+0x21c, inclusive */
    int32_t origin_x, origin_y;  /* +0x220, +0x224: offset = (left - origin_x, top - origin_y) */
    uint32_t pen;                /* the pen record {0, colour} at +0x228: its colour dword */
    int32_t brush_kind;          /* the brush record at +0x22c: 0 solid, 4 outline */
    uint32_t brush;              /* its colour dword */
    int32_t screen_height;       /* [0x47c48a]: the bound screen's height (ellipse clamp) */
} es2_map_dc;

/* Original: VSHELL.EXE 0x459cdc (pen). */
void es2_map_pixel(const es2_map_dc *dc, int32_t x, int32_t y);
/* Original: VSHELL.EXE 0x455624 (pen; clip 0x45588c, driver slot 34 0x45a8e8). */
void es2_map_line(const es2_map_dc *dc, int32_t x0, int32_t y0, int32_t x1, int32_t y1);
/* Original: VSHELL.EXE 0x455ad8 with a kind-0 brush (clip 0x455f58, spans
 * 0x456340, span fill slot 0 0x45a674). points = n (x, y) pairs, n <= 40. */
void es2_map_polygon(const es2_map_dc *dc, int n, const int32_t *points);
/* Original: VSHELL.EXE 0x457de8: the triangle split into one band per
 * colour level between its vertices' words, each band one polygon of the
 * brush colour max - i. points = 3 (x, y) pairs. */
void es2_map_triangle(es2_map_dc *dc, const int32_t points[6], const uint16_t colours[3]);
/* Original: VSHELL.EXE 0x45399c(bitmap, &(x, y), 0, &(sx, sy), 0): the
 * bitmap mapped onto the rectangle (x, y)..(x + sx, y + sy) through the
 * textured polygon driver (slot 33 0x45ae5c), index 0 transparent. */
void es2_map_textured_quad(const es2_map_dc *dc, const es2_bitmap *b, int32_t x, int32_t y, int32_t sx,
                           int32_t sy);
/* Original: VSHELL.EXE 0x45886c with a kind-0 brush: the filled ellipse of
 * the box (x0, y0)..(x1, y1). Degenerate boxes (zero radius) are refused
 * (the original draws a line with an uninitialised pen kind). */
void es2_map_ellipse(const es2_map_dc *dc, int32_t x0, int32_t y0, int32_t x1, int32_t y1);
/* Original: VSHELL.EXE 0x4576a4: brush kind 0 fills the rectangle (driver
 * slot 2), kind 4 draws its outline as four pen lines of the brush colour. */
void es2_map_rect(const es2_map_dc *dc, int32_t x0, int32_t y0, int32_t x1, int32_t y1);
/* Original: VSHELL.EXE 0x4537e8(bitmap, &(x, y), 0): es2_draw_blit (draw.h). */
void es2_map_bitmap(const es2_map_dc *dc, const es2_bitmap *b, int32_t x, int32_t y);
/* Original: VSHELL.EXE 0x453908(bitmap, &(x, y), 0, &(sx, sy)) -> 0x4591b8
 * -> driver slot 29 0x45b4c0: the bitmap resampled to sx x sy. */
void es2_map_scaled(const es2_map_dc *dc, const es2_bitmap *b, int32_t x, int32_t y, int32_t sx, int32_t sy);

/* ---- the map --------------------------------------------------------------- */

#include "es2/formats/common.h"
#include "es2/shell/prepare.h" /* es2_shell_rng: VSHELL's generator state (the engine's convention) */

#define ES2_MAP_LEFT 291   /* the map rectangle of 0x40e75f */
#define ES2_MAP_TOP 67
#define ES2_MAP_RIGHT 585
#define ES2_MAP_BOTTOM 292
#define ES2_MAP_ICONS 26   /* dba\mis_icon.dba frames 0..25 (3..25 are used) */
#define ES2_MAP_SQUAD 20   /* member slots of an actor group */

/* Tables of VSHELL.EXE, read by the host at run time from the span store
 * (vshell_store.h): the cosine table 0x47b480 (0x401 LE words) of 0x451b90 and the
 * arc-tangent table 0x47aaf4 (0x201 bytes) of 0x450c80. */
#define ES2_MAP_COSINE_OFFSET 0x47b480u
#define ES2_MAP_COSINE_SIZE (0x401 * 2)
#define ES2_MAP_ATAN_OFFSET 0x47aaf4u
#define ES2_MAP_ATAN_SIZE 0x201

/* What 0x40e75f reads (vshell_mission_map.md section 4); the host reads the
 * files (VOL-aware like VSHELL) and decodes the icons. */
typedef struct es2_mission_map_inputs {
    es2_span script;          /* data\script.dat (the prepared script) */
    es2_span player_mec;      /* data\player.mec: word 1 = the squad size N */
    es2_span mforms;          /* data\mforms.dat */
    int32_t cell_shift;       /* dat\zone<N>.dat: its third dword */
    uint16_t grid_width, grid_height; /* dba\zone<N>.dba element 0 header (+6, +4) */
    es2_span grid_bytes;      /* its stored bytes (0x4293af reads them as heights) */
    uint32_t material_count;  /* data\mat0.dat record count */
    int32_t material_level;   /* data\mat0.dat record 0 dword +4 */
    int16_t mission_lines;    /* data\mission.str group 0 string count */
    const es2_bitmap *icons;  /* dba\mis_icon.dba elements, decoded; NULL entries draw nothing */
    size_t icon_count;
    es2_span cosine, atan;    /* the two EXE tables above */
} es2_mission_map_inputs;

/* The process globals the fly-over keeps between map objects (0x48a39e ..
 * 0x48a3ce, the tick 0x471dc4) and VSHELL's second RNG state 0x48110a,
 * which only the map build advances. The host keeps one per process. */
typedef struct es2_mission_map_globals {
    int32_t delta[3], start[3], target[3]; /* 0x48a39e, 0x48a3aa, 0x48a3b6 */
    int32_t leg;                           /* 0x48a3c8: ticks of the current leg */
    int16_t next_child;                    /* 0x48a3cc */
    uint8_t text_flag;                     /* 0x48a3ce */
    int32_t cursor;                        /* 0x48a3c4: route child index, -1 = none */
    uint32_t tick;                         /* 0x471dc4 */
    es2_shell_rng material_rng;            /* 0x48110a */
} es2_mission_map_globals;

typedef struct es2_mission_map_route { int16_t count; int16_t *children; } es2_mission_map_route;

/* The map object (0x3c5 bytes in VSHELL; fields named by their offsets). */
typedef struct es2_mission_map {
    int built;
    /* view (+0x12, +0x16, +0x1a), pan (+0x3a, +0x3e), pan step (+0x42) */
    int32_t view[3], pan[2], step;
    int32_t camera[3];        /* camera +4: the bound position */
    int32_t grid_spacing;     /* +0x26 */
    /* script lists */
    int32_t *coords; int16_t coord_count;           /* +0x52, +0x74 */
    int16_t *angles; int16_t angle_count;           /* +0x56, +0x76 */
    es2_mission_map_route *routes; int16_t route_count; /* +0x5a, +0x72 */
    int16_t mech_count;                             /* +0x7a */
    uint8_t *bases; int16_t base_count;             /* +0x62: 52-byte records, +0x78 */
    uint8_t *orders; int16_t order_count;           /* +0x6a: 14-byte records, +0x7c */
    uint8_t *groups; int16_t group_count;           /* +0x6e: 156-byte records, +0x7e */
    int player_route;                               /* +0x66: route index, -1 = none */
    int16_t squad_count;                            /* +0x80 */
    int32_t squad[ES2_MAP_SQUAD][3];                /* +0x82 */
    uint8_t squad_set[ES2_MAP_SQUAD];               /* own: the slot has a position */
    /* fly-over */
    uint16_t state, next_state;                     /* +0x172, +0x174 */
    uint32_t timer;                                 /* +0x176 */
    uint8_t done, final_count;                      /* +0x17a, +0x17b */
    int16_t line;                                   /* +0x17c */
    uint8_t first_paint;                            /* +0x182 */
    uint8_t squad_shown, route_shown, markers_shown; /* +0x183, +0x184, +0x185 */
    int32_t overview[3], squad_view[3];             /* +0x187, +0x193 */
    int32_t bbox_min[3], bbox_max[3];               /* +0x19f, +0x1ab */
    int32_t max_altitude;                           /* +0x3bd */
    int16_t mission_lines;                          /* +0x217 */
    /* the cached terrain image (0x48a390 / 0x48a3cf) */
    uint8_t *image; uint16_t image_width, image_height;
    const es2_bitmap *icons; size_t icon_count;
    uint32_t wait_ticks;  /* own: busy-wait ticks the last paint spent (first paint only) */
    int refused;          /* own: a documented refusal happened (vshell_mission_map.md section 14.2) */
} es2_mission_map;

/* Header word 1 of the script: the zone number N of dat\zone<N>.dat and
 * dba\zone<N>.dba (0x42519c). -1 when the script is too short. */
int es2_mission_map_zone(es2_span script);

/* Original: VSHELL.EXE 0x40e75f -> 0x424597 (constructor, the script
 * reader 0x424a2b, the zone 0x42519c, the squad 0x425404, the overview
 * 0x4258a2 and squad view 0x425935) and the terrain image 0x427634. Frees
 * an older map in `map` first. g->material_rng advances by the material
 * draws of 0x4293af. Returns 0, or -1 for input the original asserts on
 * (map left empty). */
int es2_mission_map_build(es2_mission_map *map, const es2_mission_map_inputs *in, es2_mission_map_globals *g);
void es2_mission_map_free(es2_mission_map *map);

/* Original: VSHELL.EXE 0x425a5e (vtable slot 0): the map into the frame
 * (640 x 480 indices, stride 640) inside the map rectangle. With the
 * first-paint flag the busy waits are not performed: their ticks are added
 * to g->tick and reported in map->wait_ticks. */
void es2_mission_map_paint(es2_mission_map *map, es2_mission_map_globals *g, uint8_t *frame);

/* Original: VSHELL.EXE 0x4262cf: one fly-over step at tick `now` (16 ms
 * units, 0x465d5c); paints when the state asks for it. Returns 1 while the
 * fly-over continues (VSHELL presents after every step), 0 when done. */
int es2_mission_map_step(es2_mission_map *map, es2_mission_map_globals *g, uint32_t now, uint8_t *frame);
/* The first paint's busy waits in wall time (0x425be3..0x425c04,
 * 0x425f20..0x425f41 and the grid lines' 0x426055..0x426082 with its three
 * siblings; vshell_ui.md 42.6): reads `clock` (milliseconds) until its tick
 * (ms / 16, as 0x465d5c) is no longer below `target` (the step's `now` plus
 * map->wait_ticks). Nothing is painted, presented or pumped meanwhile. The
 * host calls it after the step and before the present, never with a clock
 * that does not run by itself (the headless one). Returns the clock reads. */
uint32_t es2_mission_map_wait(uint32_t target, uint32_t (*clock)(void *context), void *context);
/* Original: VSHELL.EXE 0x425a43 (Esc, Space or a click during the loop). */
void es2_mission_map_skip(es2_mission_map *map);

typedef enum es2_mission_map_arrow {
    ES2_MAP_NORTH, ES2_MAP_SOUTH, ES2_MAP_WEST, ES2_MAP_EAST, ES2_MAP_ZOOM_IN, ES2_MAP_ZOOM_OUT
} es2_mission_map_arrow;
/* The arrow handlers 0x445577 .. 0x445982: the number of steps for the
 * button's repeat counter (+0x65): 1, 2, 3 or 4. */
int es2_mission_map_arrow_steps(int32_t repeat_counter);
/* One step: the vtable slot (0x428093, 0x4280e7, 0x427feb, 0x42803f,
 * 0x420738, 0x427f9a), then the paint; the caller presents after it. */
void es2_mission_map_arrow_step(es2_mission_map *map, es2_mission_map_globals *g, es2_mission_map_arrow a,
                                uint8_t *frame);

/* The projection 0x420646 with the bound camera: frame pixel of a world
 * point. Returns 0 when the depth is 0 (the original divides by zero). */
int es2_mission_map_project(const es2_mission_map *map, const int32_t p[3], int32_t *x, int32_t *y);

#endif
