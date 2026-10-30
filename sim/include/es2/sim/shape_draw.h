/* The DTS shape draw slots of the own dbsim.exe as own code (rasterizer.md,
 * "DTS shape draw"). The part of the renderer's module table (rasterizer.md,
 * "The renderer line (R3D)") that walks a shape instance and hands polygons to
 * the fill layer: the instance slot 0x478da4 and its matrix push/pop
 * 0x478d60/0x478d90, the shape draws 0x490ab8/0x490c1c with their leaves
 * 0x490d3a..0x490d58, the detail test 0x476248, the part transforms
 * 0x4760e4/0x476120/0x47613c with the saved views 0x48d718, the part list
 * 0x476808, the group 0x4759d4, the cell animation 0x4768f0, the detail part
 * 0x4769c8 and its setters 0x4769b8, the BSP part 0x476c18/0x476b28 with
 * 0x4911c0, the solid, shaded and Gouraud polygons 0x474ec0/0x475538/0x4756d4,
 * the back face tests 0x48c72c/0x48c844, the vertex projections
 * 0x48c954/0x4912a0, the near-plane clips 0x48cf20/0x48d38c, the fill dispatch
 * 0x48d5c0..0x48d708 and its forwards 0x48d9ba/0x48dbac/0x48dbc1, and the actor
 * draws 0x401fe4/0x46b8ea/0x4033e4/0x4034f4 (their detail level tables
 * 0x45d490/0x45d50c are hud_load.h's rows). Every function is a service row of
 * sim/dispatch/services.h whose function lives in src/shape_draw.c; guest state
 * is read and written only through the bound main loop's guest view; the
 * registry hooks (matrix_compose, euler_matrix, angle_ratio, heading_cos,
 * multiply_q10, dist3d_delta, matrix_point), the other files' rows (view.c's
 * rows, the fill layer 0x484222/0x483eb8/ 0x4842bb, the haze table 0x468158,
 * the shade lookup 0x430e34) and every part's virtual draw slot are called
 * through the dispatch over the loop's call area. Nothing here writes
 * pixels. */
#ifndef ES2_SIM_SHAPE_DRAW_H
#define ES2_SIM_SHAPE_DRAW_H
#include <stdint.h>
#include "es2/sim/loop.h"

typedef struct es2_sim_shape_draw {
    es2_sim_loop *loop;      /* NULL: the bound main loop (es2_sim_loop_bound) at the first call */
    uint32_t refused_va;     /* the VA of the last refusal raised by this module */
    uint64_t calls;          /* service calls answered */
} es2_sim_shape_draw;

/* The instance the rows use; NULL (the default) binds a private record to
 * the main loop bound at the first call. */
void es2_sim_shape_draw_bind(es2_sim_shape_draw *draw);

#endif
