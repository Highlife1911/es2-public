/* Sky, horizon, object binning and the sort tree of the own dbsim.exe as own
 * code (horizon.md, "the sky and the horizon as own code", terrain.md,
 * "binning, the cell callback and the sort tree as own code"). The per-frame
 * draw order under the render root: the sky and horizon entry 0x42e700 with the
 * horizon line, the sky bands and the ground fill (0x42ebe8..0x42f0b0), the
 * terrain cells pass 0x428140, the jolt palette switches
 * 0x42ea44/0x42ea70/0x42ea94, the object binning 0x42841c with its cell table
 * (0x4281b0..0x4283b4), the cell callback 0x428c60, the distance cull
 * 0x428bc0/0x428c08, the sort tree 0x429460..0x429620, the per-object draw
 * 0x42876c, the scene finish 0x42883c and the pick save/restore
 * 0x428ae0/0x428b38. Every function is a service row of sim/dispatch/services.h
 * whose function lives in src/scene.c; guest state is read and written only
 * through the bound main loop's guest view, and the registry hooks, the other
 * packages' services and the graphics driver's slots are called through the
 * dispatch over the loop's call area. */
#ifndef ES2_SIM_SCENE_H
#define ES2_SIM_SCENE_H
#include <stdint.h>
#include "es2/sim/loop.h"

typedef struct es2_sim_scene {
    es2_sim_loop *loop;      /* NULL: the bound main loop (es2_sim_loop_bound) at the first call */
    uint32_t refused_va;     /* the VA of the last refusal raised by this module */
    uint64_t calls;          /* service calls answered */
} es2_sim_scene;

/* The instance the rows use; NULL (the default) binds a private record to
 * the main loop bound at the first call. */
void es2_sim_scene_bind(es2_sim_scene *scene);

#endif
