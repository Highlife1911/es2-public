/* The terrain draw's view frustum library as own code (terrain.md, "the frustum
 * library as own code"). The frustum record's constructor 0x4949a4, its
 * placement 0x4949e4 (six planes from the camera and its view rectangle), the
 * polygon clip 0x494e58 against the planes the record's flag byte selects, and
 * their callees: the plane from a normal 0x47e450 (with the square root
 * 0x47df9c and the 64-bit helpers 0x47eaa1/0x47eabe computed inline), the
 * polygon split at a plane 0x47e73c, the side test 0x47eaf8, the polygon
 * offsets 0x4950a4/0x4950ec, the plane element constructor 0x49520e, the dword
 * swap 0x495221, the vertical-plane test 0x495236 and the plane shift 0x495254.
 * Every function is a service row of sim/dispatch/services.h whose function
 * lives in src/frustum.c; guest state is read and written only through the
 * bound main loop's guest view. The registry hooks (matrix_ctor, rotate_point
 * through 0x4954e4, line_plane) and the other packages' rows (0x49667c,
 * 0x495157, 0x495162, 0x49516d, 0x495178, 0x4951d6, 0x492a7d, 0x48d97a) are
 * called through the dispatch over the loop's call area. */
#ifndef ES2_SIM_FRUSTUM_H
#define ES2_SIM_FRUSTUM_H
#include <stdint.h>
#include "es2/sim/loop.h"

typedef struct es2_sim_frustum {
    es2_sim_loop *loop;      /* NULL: the bound main loop (es2_sim_loop_bound) at the first call */
    uint32_t refused_va;     /* the VA of the last refusal raised by this module */
    uint64_t calls;          /* service calls answered */
    uint64_t splits;         /* polygon splits (0x47e73c) */
    /* Branch counters (for the tests): the split's cases prev*3 + side + 4,
     * its crossings line_plane did not cut, inputs below three points, the
     * whole input kept; 0x47e450's long normals (0x492a7d); the clip's
     * vertical planes and the shifts by -b and by a. */
    uint64_t cases[9], misses, short_inputs, kept, long_normals, vertical, b_shifts, a_shifts;
} es2_sim_frustum;

/* The instance the rows use; NULL (the default) binds a private record to
 * the main loop bound at the first call. */
void es2_sim_frustum_bind(es2_sim_frustum *frustum);

#endif
