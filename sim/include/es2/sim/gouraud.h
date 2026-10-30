/* The Gouraud fill of DBSIM.EXE's graphics library and the terrain's flat fill
 * as own code (rasterizer.md, "Gouraud and flat fill"): the Gouraud fill
 * 0x487acc (fill kind 1 of 0x483eb8: a fan of triangles), its triangle 0x486ee8
 * (bands of one ramp colour each, filled as quads through 0x483eb8) and the
 * flat fill 0x46bc44 (one terrain polygon projected, clipped and filled through
 * the shape fill 0x48d5c0).
 *
 * The functions read and write guest state only through the loop's guest
 * view; code without an own row (the projection 0x48cce4, 0x42e8e8) and
 * the shape rows are called through es2_sim_loop_call, the fill layer of
 * fill.h directly. Each returns 0, or 1 when a call was refused or the
 * original would run an undefined or faulting path (a named refusal).
 * `eax` (when given) receives the original's EAX. */
#ifndef ES2_SIM_GOURAUD_H
#define ES2_SIM_GOURAUD_H
#include <stdint.h>
#include "es2/sim/loop.h"

#define ES2_SIM_VA_GOURAUD 0x00487accu            /* the Gouraud fill (n, points) */
#define ES2_SIM_VA_GOURAUD_TRIANGLE 0x00486ee8u   /* its triangle (points, shades) */
#define ES2_SIM_VA_FLAT_FILL 0x0046bc44u          /* the terrain's flat fill (points, n, shades) */

/* 0x487acc(n, points): EAX the fill record F = [[0x4a3614]+0x228], EDX
 * the colour word it restores into F+4, ECX n - 2. */
int es2_sim_fill_gouraud(es2_sim_loop *l, uint32_t n, uint32_t points, uint32_t *eax, uint32_t *edx, uint32_t *ecx);
/* 0x486ee8(points, shades): three dword points, three shade words. */
int es2_sim_fill_gouraud_triangle(es2_sim_loop *l, uint32_t points, uint32_t shades, uint32_t *eax);
/* 0x46bc44(points, n, shades): points of three dwords. EAX = EDX the
 * construction counter pointer [0x6cbee4], ECX the calling thread's FS:0
 * (the head the original's frame record saved and restores). */
int es2_sim_flat_fill(es2_sim_loop *l, uint32_t points, uint32_t n, uint32_t shades, uint32_t *eax, uint32_t *edx, uint32_t *ecx);

#endif
