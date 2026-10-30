/* The R3D view transform, the projection and the lights of the own dbsim.exe as
 * own code (rasterizer.md, "View transform, projection and lights", camera.md,
 * "Own code state"). The module of the
 * renderer's module table that writes no pixels: the per-frame view set-up
 * 0x48c2e4 (the camera matrix 0x6c5f94, the view matrix 0x6c5fb4, its inverse
 * 0x6c5fd4, the eye 0x6c601c, the projection constants 0x6c6028..0x6c6038 and
 * the viewport 0x6c603c), the object selection 0x48c444, the projection leaves
 * 0x48c57c..0x48c6d0, the view load 0x48d7ec, the light objects
 * (0x48bc70..0x48bd6c, the active list 0x6c60ac remove 0x48be98, the polygon
 * and terrain lighting 0x48bfe8/0x48c16c) and the light table's per-object
 * lights (0x406ee4..0x407878). Every function is a service row of
 * sim/dispatch/services.h whose function lives in src/view.c; guest state is
 * read and written only through the bound main loop's guest view, the registry
 * hooks (matrix_ctor, euler_matrix, matrix_compose, matrix_transpose,
 * rotate_point, transform_point, matrix_point, vector_subtract, dist3d_forward,
 * vector_angle) and the other files' services (operator new/delete, 0x48bc40,
 * 0x48be50, 0x4800c0) are called through the dispatch over the loop's call
 * area. */
#ifndef ES2_SIM_VIEW_H
#define ES2_SIM_VIEW_H
#include <stdint.h>
#include "es2/sim/loop.h"

typedef struct es2_sim_view {
    es2_sim_loop *loop;      /* NULL: the bound main loop (es2_sim_loop_bound) at the first call */
    uint32_t refused_va;     /* the VA of the last refusal raised by this module */
    uint64_t calls;          /* service calls answered */
} es2_sim_view;

/* The instance the rows use; NULL (the default) binds a private record to
 * the main loop bound at the first call. */
void es2_sim_view_bind(es2_sim_view *view);

#endif
