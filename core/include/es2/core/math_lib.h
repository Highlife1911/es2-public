#ifndef ES2_CORE_MATH_LIB_H
#define ES2_CORE_MATH_LIB_H
#include "es2/core/guest.h"
/* Pure helpers of the graphics library. Contract in
 * docs/reverse-engineering/player_movement.md (section "graphics-library
 * candidates and the texture edge setup 48aaac"). Addresses are guest
 * addresses; `image` is the loaded image base (0x400000 in the original).
 *
 * tex_edge 48aaac (edge, vertex): sets up the next edge of the textured
 * polygon fill 48a924 from `vertex`, walking in the direction [edge] over
 * the screen points [6c5f88] and texture points [6c5f8c] ([6c5f84]
 * vertices), skipping zero-height edges, until the end vertex [6c5f80].
 * Writes edge+4..edge+33h in the original's order (the record may overlap
 * the arrays). EDX:EAX as the original: EAX 1 for a new edge, 0 at the
 * end; EDX the IDIV remainder, the y of the last skipped vertex, or
 * `entry_edx` when `vertex` is already the end. ECX at return is dead
 * after every caller and is not reproduced. */
uint64_t es2_tex_edge_registers(const es2_guest_memory *, uint32_t image, uint32_t edge,
    uint32_t vertex, uint32_t entry_edx);
#endif
