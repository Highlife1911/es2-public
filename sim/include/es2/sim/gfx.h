/* The graphics library's primitives and the scan converter of DBSIM.EXE as own
 * code (rasterizer.md, "Graphics library primitives"): the clip helpers, the
 * rect, span and point fills, the bitmap draw and grab chain, save/size/
 * restore, scroll, the ellipse, the text functions and the BMP_8 driver slots
 * 0-6, 19, 20, 26-28 (0x480738..0x4928d0 in part), and the polygon scan
 * converter 0x4930c1/0x493141/0x493192.
 *
 * Every function reads and writes guest state only through the bound
 * loop's guest view and calls other DBSIM code through es2_sim_loop_call
 * (a driver slot not reconstructed here, poly.c's line 0x483a04). Each
 * returns 0, or 1 when a call was refused or the original would run an
 * undefined or endless path (a named refusal, rasterizer.md "GL refusals").
 * `eax` (when given) receives the original's EAX. Addresses are guest
 * VAs; rects are four dwords {l, t, r, b}, points two dwords {x, y}. */
#ifndef ES2_SIM_GFX_H
#define ES2_SIM_GFX_H
#include <stdint.h>
#include "es2/sim/loop.h"

/* Guest VAs (DBSIM.EXE). */
#define ES2_SIM_VA_GFX_CONTEXT 0x004a3614u     /* -> the graphics context record C */
#define ES2_SIM_VA_GFX_VIEW_ROWS 0x004a3652u   /* the current surface's row table */
#define ES2_SIM_VA_GFX_VIEW_FORMAT 0x004a3662u
#define ES2_SIM_VA_GFX_VIEW_PITCH 0x004a3676u
#define ES2_SIM_VA_GFX_VIEW_HEIGHT 0x004a367au
#define ES2_SIM_VA_GFX_SPANS_A 0x004a3691u     /* -> span buffer A (0x4a3699) */
#define ES2_SIM_VA_GFX_SPANS_B 0x004a3695u     /* -> span buffer B (0x4a431d) */
#define ES2_SIM_VA_GFX_RAMPS 0x004a4fa5u       /* the ramp table: 8-byte entries {pointer, int16 scale} */
#define ES2_SIM_VA_GFX_SLOTS 0x004a57acu       /* the current driver table */
#define ES2_SIM_VA_GFX_GC 0x006c5f70u          /* -> the GC object (0x490d34) */
#define ES2_SIM_VA_GFX_SURFACE 0x006c5ed0u     /* -> the current surface (0x48d99e) */
#define ES2_SIM_GFX_SPAN_ROWS 800u             /* rows a span buffer holds (0xc84 bytes) */

/* Clip helpers. */
int es2_sim_gfx_rect_clip(es2_sim_loop *l, uint32_t a, uint32_t b, uint32_t out, uint32_t *eax);          /* 0x48699c */
int es2_sim_gfx_rect_to_spans(es2_sim_loop *l, uint32_t rect, uint32_t out, uint32_t *eax);               /* 0x4869fc */
int es2_sim_gfx_spans_clip_spans(es2_sim_loop *l, uint32_t a, uint32_t b, uint32_t out, uint32_t *eax);   /* 0x486c00 */
int es2_sim_gfx_rect_clip_spans(es2_sim_loop *l, uint32_t rect, uint32_t spans, uint32_t out, uint32_t *eax); /* 0x486d15 */

/* Fills and the point. */
int es2_sim_gfx_rect_fill(es2_sim_loop *l, uint32_t rect);                            /* 0x486704 */
int es2_sim_gfx_span_fill(es2_sim_loop *l, uint32_t spans, uint32_t point);          /* 0x486a34 */
int es2_sim_gfx_point(es2_sim_loop *l, uint32_t point);                              /* 0x4895ec */
int es2_sim_gfx_ellipse(es2_sim_loop *l, uint32_t rect);                             /* 0x48817c */
uint32_t es2_sim_gfx_abs(uint32_t v);                                                /* 0x4887cb */

/* Bitmaps. */
int es2_sim_gfx_bitmap_blit(es2_sim_loop *l, uint32_t object, uint32_t point, uint32_t flags);            /* 0x4816a8 */
int es2_sim_gfx_bitmap_draw_on(es2_sim_loop *l, uint32_t surface, uint32_t header, uint32_t point, uint32_t flags); /* 0x4827bb */
int es2_sim_gfx_bitmap_draw(es2_sim_loop *l, uint32_t header, uint32_t point, uint32_t flags);            /* 0x4887d8 */
int es2_sim_gfx_bitmap_grab(es2_sim_loop *l, uint32_t header, uint32_t point, uint32_t flags);            /* 0x488974 */
int es2_sim_gfx_save(es2_sim_loop *l, uint32_t record, uint32_t pixels);                                 /* 0x488012 */
int es2_sim_gfx_size(es2_sim_loop *l, uint32_t record, uint32_t *eax);                                   /* 0x488045 */
int es2_sim_gfx_restore(es2_sim_loop *l, uint32_t record, uint32_t point, uint32_t pixels);              /* 0x488143 */
int es2_sim_gfx_restore_rect(es2_sim_loop *l, uint32_t record, uint32_t point, uint32_t pixels);         /* 0x488078 */
int es2_sim_gfx_scroll(es2_sim_loop *l, uint32_t record, uint32_t point);                                /* 0x487e60 */

/* The stretched blit (slot 29) and the rotated blit (fill.c's polygon fill
 * 0x483eb8 with a kind-5 textured fill). */
int es2_sim_gfx_stretch(es2_sim_loop *l, uint32_t header, uint32_t point, uint32_t flags, uint32_t size);                     /* 0x488aa8 */
int es2_sim_gfx_stretch_on(es2_sim_loop *l, uint32_t surface, uint32_t header, uint32_t point, uint32_t flags, uint32_t size); /* 0x481c3b */
int es2_sim_gfx_stretch_blit(es2_sim_loop *l, uint32_t object, uint32_t point, uint32_t flags, uint32_t size);                /* 0x4817c8 */
int es2_sim_gfx_rotate(es2_sim_loop *l, uint32_t header, uint32_t point, uint32_t flags, uint32_t size, uint32_t angle);      /* 0x488b98 */
int es2_sim_gfx_rotate_on(es2_sim_loop *l, uint32_t surface, uint32_t header, uint32_t point, uint32_t flags, uint32_t size, uint32_t angle); /* 0x481c58 */
int es2_sim_gfx_rotated_blit(es2_sim_loop *l, uint32_t object, uint32_t point, uint32_t flags, uint32_t size, uint32_t angle); /* 0x48185c */

/* Text. */
int es2_sim_gfx_char_width(es2_sim_loop *l, uint32_t font, uint32_t ch, uint32_t *eax);                  /* 0x482498 */
int es2_sim_gfx_string_width(es2_sim_loop *l, uint32_t font, uint32_t text, uint32_t *eax);              /* 0x4824ec */
int es2_sim_gfx_font_height(es2_sim_loop *l, uint32_t font, uint32_t *eax);                              /* 0x48251c */
int es2_sim_gfx_char(es2_sim_loop *l, uint32_t font, uint32_t point, uint32_t ch);                       /* 0x482534 */
int es2_sim_gfx_string(es2_sim_loop *l, uint32_t font, uint32_t point, uint32_t text);                   /* 0x482634 */

/* A driver slot: the own body for 0-6, 19, 20, 26-29, 33, else the table's
 * target through the dispatch. `args` are the slot's stack arguments. */
int es2_sim_gfx_slot(es2_sim_loop *l, unsigned index, const uint32_t *args, unsigned count, uint32_t *eax);

/* The scan converter: `points` = count pairs of dwords {x, y}; `out` =
 * two dwords {ymin, rows}, then from out+8 rows pairs of dwords {left,
 * right}. */
int es2_sim_gfx_scan(es2_sim_loop *l, uint32_t count, uint32_t points, uint32_t out, uint32_t *eax);          /* 0x4930c1 */
int es2_sim_gfx_scan_reversed(es2_sim_loop *l, uint32_t count, uint32_t points, uint32_t out, uint32_t *eax); /* 0x493141 */
int es2_sim_gfx_scan_convert(es2_sim_loop *l, uint32_t count, uint32_t points, uint32_t out, uint32_t *eax);  /* 0x493192 */

/* The blit and clear rest (rasterizer.md, "Blit, clear and cursor rows").
 * `regs` is {EAX, EDX, ECX}: in, the entry registers; out, the registers the
 * original leaves (the callee's where it ends with a call). */
typedef struct es2_sim_gfx_regs { uint32_t eax, edx, ecx; } es2_sim_gfx_regs;
int es2_sim_gfx_ramps_set(es2_sim_loop *l, uint32_t first, uint32_t count, uint32_t src, es2_sim_gfx_regs *regs); /* 0x486e10 */
int es2_sim_gfx_cursor_point_set(es2_sim_loop *l, uint32_t point, es2_sim_gfx_regs *regs);                      /* 0x486e7c */
int es2_sim_gfx_scroll_rect(es2_sim_loop *l, uint32_t record, uint32_t point);                                 /* 0x487bdc */
void es2_sim_gfx_rect_area(es2_sim_loop *l, uint32_t rect, es2_sim_gfx_regs *regs);                            /* 0x487e94 */
int es2_sim_gfx_save_rect(es2_sim_loop *l, uint32_t record, uint32_t pixels);                                  /* 0x487eac */
int es2_sim_gfx_rotation_matrix(es2_sim_loop *l, uint32_t matrix, uint32_t angle, es2_sim_gfx_regs *regs);     /* 0x488dc7 */
int es2_sim_gfx_rotate_point(es2_sim_loop *l, uint32_t v, uint32_t matrix, uint32_t out, es2_sim_gfx_regs *regs); /* 0x488ddd */

/* The BMP_8 driver's cursor point (regs = EAX, EDX, ECX as the original leaves
 * them) and slot 33, the textured polygon of a kind-5 or kind-7 fill (`points`
 * = n pairs of dwords {x, y}). */
int es2_sim_gfx_cursor_point(es2_sim_loop *l, uint32_t point, uint32_t regs[3]);                         /* 0x48993a */
int es2_sim_gfx_polygon(es2_sim_loop *l, uint32_t n, uint32_t points);                                   /* 0x48a924 */

#endif
