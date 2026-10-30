/* The terrain renderer of the own dbsim.exe as own code (terrain.md, "the
 * terrain renderer as own code"). The per-frame terrain set-up 0x46cb9c (the
 * view polygon on the grid, the step vectors, the height cache), the walk
 * 0x46d1a8 with its row and column walkers 0x471f44/0x472504 (and their helpers
 * 0x471f30, 0x4724b8, 0x4724e0), the per-line callbacks 0x46dddc/0x46dfa8, the
 * cell draw 0x46d448 with the texture selection, the height cache lookup
 * 0x49534c, the cell object event 0x46e5a4, the mark read 0x470d94 and the
 * facing test 0x46be9c. Every function is a service row of
 * sim/dispatch/services.h whose function lives in src/terrain_draw.c; guest
 * state is read and written only through the bound main loop's guest view. The
 * registry hooks (poly_clip, poly_spans, view_rect, cell_uv, cell_flag_get,
 * cell_flag_set, transform_point, rotate_point), the other packages' services
 * (0x48c444, 0x49667c, the height cache 0x4952e8/0x4952fc, the scene's
 * 0x428bc0, the fills 0x4680e0, 0x4680f0, 0x46bc44, 0x468760, 0x46817c), the
 * frustum library (0x4949a4, 0x4949e4, 0x494e58) and the grid's cell object
 * callback are called through the dispatch over the loop's call area. */
#ifndef ES2_SIM_TERRAIN_DRAW_H
#define ES2_SIM_TERRAIN_DRAW_H
#include <stdint.h>
#include "es2/sim/loop.h"

typedef struct es2_sim_terrain_draw {
    es2_sim_loop *loop;      /* NULL: the bound main loop (es2_sim_loop_bound) at the first call */
    uint32_t refused_va;     /* the VA of the last refusal raised by this module */
    uint64_t calls;          /* service calls answered */
    uint64_t cells;          /* cell draws (0x46d448) */
} es2_sim_terrain_draw;

/* The instance the rows use; NULL (the default) binds a private record to
 * the main loop bound at the first call. */
void es2_sim_terrain_draw_bind(es2_sim_terrain_draw *terrain);

#endif
