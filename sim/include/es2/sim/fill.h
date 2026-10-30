/* The graphics library's 2D polygon fill layer and its line of DBSIM.EXE as own
 * code (rasterizer.md, "GL fill layer and line"): the fill
 * 0x483eb8, its reversed and orientation-dispatching entries 0x484222 and
 * 0x4842bb, the polygon clipper 0x484338, the span builder 0x484720, the line
 * 0x483a04 with its clipper 0x483c6c and span builder 0x483d6c, and the BMP_8
 * driver's line writer (slot 34, 0x48a3b0).
 *
 * Every function reads and writes guest state only through the bound
 * loop's guest view and calls code it does not reconstruct (the Gouraud
 * fill 0x487acc, driver slots whose table entry is not the BMP_8 body,
 * slot 33) through es2_sim_loop_call. Each returns 0, or 1 when a call
 * was refused or the original would run an undefined, endless or faulting
 * path (a named refusal; "#DE" in the reason for a divide fault). `eax`
 * (when given) receives the original's EAX where it is defined; where the
 * original leaves EAX undefined it is not written. Points are two dwords
 * {x, y}; rects four dwords {l, t, r, b}; span lists are words {ymin,
 * rows} followed by rows pairs of words {left, right}. */
#ifndef ES2_SIM_FILL_H
#define ES2_SIM_FILL_H
#include <stdint.h>
#include "es2/sim/loop.h"

#define ES2_SIM_VA_FILL_SAVED_FRAME 0x004a33d4u   /* the span builder's saved EBP (a host value in the own code) */

int es2_sim_fill_polygon(es2_sim_loop *l, uint32_t count, uint32_t points, uint32_t *eax);           /* 0x483eb8 */
int es2_sim_fill_polygon_reversed(es2_sim_loop *l, uint32_t count, uint32_t points, uint32_t *eax);  /* 0x484222 */
int es2_sim_fill_polygon_any(es2_sim_loop *l, uint32_t count, uint32_t points, uint32_t *eax);       /* 0x4842bb */
int es2_sim_fill_clip(es2_sim_loop *l, uint32_t count, uint32_t points, uint32_t out, uint32_t rect, uint32_t *eax); /* 0x484338 */
int es2_sim_fill_spans(es2_sim_loop *l, uint32_t count, uint32_t points, uint32_t out, uint32_t *eax);              /* 0x484720 */
int es2_sim_fill_line(es2_sim_loop *l, uint32_t p0, uint32_t p1);                                    /* 0x483a04 */
int es2_sim_fill_line_clip(es2_sim_loop *l, uint32_t a, uint32_t b, uint32_t rect, uint32_t *eax);   /* 0x483c6c */
int es2_sim_fill_line_spans(es2_sim_loop *l, uint32_t a, uint32_t b, uint32_t out);                  /* 0x483d6c */
int es2_sim_fill_line_slot(es2_sim_loop *l, uint32_t colour, uint32_t p0, uint32_t p1);              /* 0x48a3b0 */

#endif
