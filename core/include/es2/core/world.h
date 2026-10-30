#ifndef ES2_CORE_WORLD_H
#define ES2_CORE_WORLD_H
#include "es2/core/weapons.h"
/* Terrain draw pass helpers (docs/reverse-engineering/terrain.md).
 * A polygon is {signed count, points}; poly_clip/poly_spans use 2D points
 * (two dwords), view_rect writes 3D points (three dwords). Every function
 * reads and writes guest memory in the original's order, so overlapping
 * buffers behave as in the original. All return EDX:EAX. The original's ECX
 * at return is dead after every caller and is not reproduced. */

/* 0x472bcc: one clipping step of a polygon at the line x = line (axis low
 * byte nonzero) or y = line (zero); keep low byte nonzero keeps the side
 * >= 0, zero the side <= 0. Writes out->points and out->count. A divisor of
 * -1 with an INT_MIN dividend faults in the original and is outside the
 * contract. */
uint64_t es2_poly_clip_registers(const es2_guest_memory *, uint32_t keep, uint32_t axis,
    uint32_t line, uint32_t in, uint32_t out);
/* 0x472b14: translates the polygon by its minimum corner (in place), runs
 * the original scan converter 0x4930c1 through g->call with the original
 * entry registers, then fills the extent record and moves the span x values
 * back by minX. The span buffer must hold the 2 * [spans+4] dwords the
 * converter announces. */
uint64_t es2_poly_spans_registers(const es2_guest_memory *, const es2_weapon_graph *,
    uint32_t polygon, uint32_t spans, uint32_t extent);
/* 0x46d324: the square of the view radius around the eye position
 * [0x6b4f44]/[0x6b4f48], clamped to the grid, as a four-point polygon (or
 * count 0). image is the image base (addresses are image + VA - 0x400000). */
uint64_t es2_view_rect_registers(const es2_guest_memory *, uint32_t image, uint32_t map, uint32_t polygon);
/* 0x46bdf8: texture coordinate quad of a terrain cell from the table
 * [0x6b4f40] (level 0) or from u/v scaled by the grid's cell shift
 * [[0x4a0be0]+0x108] (other levels). */
uint64_t es2_cell_uv_registers(const es2_guest_memory *, uint32_t image, uint32_t cell,
    uint32_t u, uint32_t v, uint32_t out);
#endif
