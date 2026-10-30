/* The software rasterizer's polygon fills of DBSIM.EXE as own code
 * (rasterizer.md, "Software polygon fills"): the haze helpers
 * 0x4680e0/0x4680f0/0x468158, the 3D wrapper 0x46817c with its near-plane clip,
 * the 2D fill 0x468414, the screen clippers
 * 0x469894/0x4699c8/0x469b2c/0x469c64, the texture page set/init
 * 0x469dc8/0x469ddc, the affine span loops 0x46ac14 (texture), 0x46ad4c
 * (texture through a flat shade row) and 0x46aeb1 (texture through a Gouraud
 * shade), the edge tables 0x477d48, the run-list span clippers
 * 0x48b524/0x48b5af/0x48b63a, the depth-run builder 0x48b854, and the
 * fixed-point helpers 0x47e068/0x47e07d/0x47e08d and
 * 0x471cd8/0x471cf0/0x471d08/0x471d26/0x471d61.
 *
 * Every function reads and writes guest state only through the bound loop's
 * guest view, in the listing's order, and calls other DBSIM code (the view
 * transform and projection of view.c) through es2_sim_loop_call. Each returns 0,
 * or 1 when a call was refused or the original would fault (an integer divide
 * by zero or a quotient overflow, #DE) or run an undefined path (a named
 * refusal, rasterizer.md, "Software polygon fills"). `eax` (when given)
 * receives the original's EAX. Addresses are guest VAs.
 *
 * Vertex: 28 bytes {x, y, z +8, u +0xc, v +0x10, w +0x14, shade +0x18};
 * flag word: 8 z, 1 u/v (and inside it 2 w), 4 shade. Edge entry: 0x28
 * bytes per row {x, z >> 4 +4, u +8, v +0xc, w +0x10, shade +0x14, ...}. */
#ifndef ES2_SIM_POLY_H
#define ES2_SIM_POLY_H
#include <stdint.h>
#include "es2/sim/loop.h"

/* Guest VAs (DBSIM.EXE). */
#define ES2_SIM_VA_POLY_FAR 0x004a08acu          /* the haze's far distance (0x4680e0) */
#define ES2_SIM_VA_POLY_HAZE 0x004a08b0u         /* the haze term (0x4680f0), << 8 */
#define ES2_SIM_VA_POLY_SHADES 0x004d3cb4u       /* shade levels; 0x4d3cb8 haze levels */
#define ES2_SIM_VA_POLY_RECIP 0x004d3cbcu        /* 640 dwords: 0x10000 / n */
#define ES2_SIM_VA_POLY_TEXTURE 0x004d4f04u      /* texture pages, 0x10000 bytes each */
#define ES2_SIM_VA_POLY_EDGES 0x006c5390u        /* the edge result {first row, rows, left, right} */
#define ES2_SIM_VA_POLY_DEPTH_ON 0x0049f248u     /* nonzero: the depth path */
#define ES2_SIM_VA_POLY_RUNS 0x004a5af8u         /* -> -> per-row run lists {count, pointer} */

/* Fixed-point helpers (pure; 0 or a refusal on #DE). */
int es2_sim_poly_div16(es2_sim_loop *l, uint32_t va, int32_t a, int32_t b, uint32_t *eax);   /* 0x47e068: (a << 16) / b */
uint32_t es2_sim_poly_mul16(int32_t a, int32_t b);                                            /* 0x47e07d, 0x47e08d */
int es2_sim_poly_add64(es2_sim_loop *l, uint32_t lo, uint32_t hi, uint32_t add_lo, uint32_t add_hi); /* 0x471cd8 */
int es2_sim_poly_sub64(es2_sim_loop *l, uint32_t lo, uint32_t hi, uint32_t sub_lo, uint32_t sub_hi); /* 0x471cf0 */
int es2_sim_poly_shift64(es2_sim_loop *l, uint32_t lo, uint32_t hi, uint32_t count);          /* 0x471d08 */
int es2_sim_poly_divide_pair(es2_sim_loop *l, uint32_t out, uint32_t rec);                    /* 0x471d26 */
int es2_sim_poly_divide_three(es2_sim_loop *l, uint32_t out, uint32_t in, int32_t d, uint32_t count); /* 0x471d61 */

/* Haze. */
void es2_sim_poly_set_far(es2_sim_loop *l, uint32_t far, uint32_t *eax);                     /* 0x4680e0 */
int es2_sim_poly_haze(es2_sim_loop *l, int32_t distance, uint32_t *eax);                      /* 0x4680f0 */
void es2_sim_poly_shade_colour(es2_sim_loop *l, int32_t shade, uint32_t *eax);                /* 0x468158 */

/* Screen clippers. */
int es2_sim_poly_cut_x(es2_sim_loop *l, int32_t c, uint32_t a, uint32_t b, uint32_t out, uint32_t flags, uint32_t *eax); /* 0x469894 */
int es2_sim_poly_cut_y(es2_sim_loop *l, int32_t c, uint32_t a, uint32_t b, uint32_t out, uint32_t flags, uint32_t *eax); /* 0x469b2c */
int es2_sim_poly_clip_x(es2_sim_loop *l, int32_t n, uint32_t in, uint32_t out, uint32_t flags, uint32_t *eax);           /* 0x4699c8 */
int es2_sim_poly_clip_y(es2_sim_loop *l, int32_t n, uint32_t in, uint32_t out, uint32_t flags, uint32_t *eax);           /* 0x469c64 */

/* Texture pages. */
void es2_sim_poly_page_set(es2_sim_loop *l, uint32_t page, uint32_t *eax);                    /* 0x469dc8 */
void es2_sim_poly_page_init(es2_sim_loop *l, uint32_t table, uint32_t page, uint32_t *eax);   /* 0x469ddc */

/* Edge tables. */
int es2_sim_poly_edges(es2_sim_loop *l, int32_t n, uint32_t verts, uint32_t flags, uint32_t *eax); /* 0x477d48 */

/* Span loops and run clippers: no arguments, the span state in DATA. */
#define ES2_SIM_POLY_SPAN_TEXTURE 0u   /* 0x46ac14; runs 0x48b524 */
#define ES2_SIM_POLY_SPAN_FLAT 1u      /* 0x46ad4c; runs 0x48b5af */
#define ES2_SIM_POLY_SPAN_GOURAUD 2u   /* 0x46aeb1; runs 0x48b63a */
void es2_sim_poly_span(es2_sim_loop *l, unsigned kind, uint32_t *eax);
void es2_sim_poly_span_runs(es2_sim_loop *l, unsigned kind, uint32_t *eax);
void es2_sim_poly_depth_runs(es2_sim_loop *l, uint32_t *eax);                                /* 0x48b854 */

/* The fills. */
int es2_sim_poly_fill(es2_sim_loop *l, int32_t n, uint32_t verts, int32_t mode, uint32_t word, uint32_t shade, uint32_t flags, uint32_t *eax); /* 0x468414 */
int es2_sim_poly_fill_3d(es2_sim_loop *l, uint32_t points, uint32_t uv, int32_t n, uint32_t word, uint32_t shades, int32_t mode,
                         uint32_t transformed, uint32_t flags, uint32_t *eax);                /* 0x46817c */
/* The perspective fill (u/w, v/w per step of 1 << s pixels). */
int es2_sim_poly_fill_persp(es2_sim_loop *l, uint32_t points, uint32_t uv, int32_t n, uint32_t page, uint32_t shades, int32_t mode,
                            uint32_t transformed, uint32_t flags, uint32_t *eax);             /* 0x468760 */
/* The shade lookup (EAX = EDX = the int16 shade, ECX as the original leaves it). */
void es2_sim_poly_shade(es2_sim_loop *l, uint32_t colour, uint32_t level, uint32_t *eax, uint32_t *edx, uint32_t *ecx); /* 0x430e34 */

#endif
