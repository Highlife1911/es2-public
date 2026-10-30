/* The graphics library's primitives and the scan converter of DBSIM.EXE as own
 * code (rasterizer.md, "Graphics library primitives"; es2/sim/gfx.h). "Listing"
 * = llvm-objdump -d -M intel of the reference DBSIM.EXE (SHA-256
 * 8561aa47...c4ad). Every function reads and writes guest memory through the
 * bound loop's guest view, in the listing's order; a driver slot whose table
 * entry is not the BMP_8 driver's and the line 0x483a04 are called through the
 * dispatch. Paths the original leaves undefined (a dead branch, an
 * uninitialised local, an endless loop, a store past the span buffer) are
 * refused by name ("GL refusals"). */
#include <string.h>
#include "es2/sim/gfx.h"
#include "es2/sim/memory.h"
#include "es2/sim/dispatch.h"

/* ---- guest VAs (DBSIM.EXE) */
#define VA_CONTEXT ES2_SIM_VA_GFX_CONTEXT
#define VA_VIEW_ROWS ES2_SIM_VA_GFX_VIEW_ROWS
#define VA_VIEW_FORMAT ES2_SIM_VA_GFX_VIEW_FORMAT
#define VA_VIEW_PITCH ES2_SIM_VA_GFX_VIEW_PITCH
#define VA_VIEW_HEIGHT ES2_SIM_VA_GFX_VIEW_HEIGHT
#define VA_SPANS_A ES2_SIM_VA_GFX_SPANS_A
#define VA_SPANS_B ES2_SIM_VA_GFX_SPANS_B
#define VA_RAMPS ES2_SIM_VA_GFX_RAMPS
#define VA_SLOTS ES2_SIM_VA_GFX_SLOTS
#define VA_GC ES2_SIM_VA_GFX_GC
#define VA_SURFACE ES2_SIM_VA_GFX_SURFACE
#define VA_MEMSET 0x0049681au             /* CW3220 _memset thunk */
#define VA_LINE 0x00483a04u               /* the line (pen) */

/* The BMP_8 driver's slot bodies this file provides (the table 0x4a586c). */
#define SLOT_SPAN_SOLID 0u      /* 0x489f84 */
#define SLOT_SPAN_TABLE 1u      /* 0x48a016 */
#define SLOT_RECT_SOLID 2u      /* 0x48a6c4 */
#define SLOT_RECT_TABLE 3u      /* 0x48a74c */
#define SLOT_PIXEL_GET 4u       /* 0x48a340 */
#define SLOT_PIXEL_PUT 5u       /* 0x48a361 */
#define SLOT_PIXEL_TABLE 6u     /* 0x48a384 */
#define SLOT_SCROLL_SPANS 19u   /* 0x48a08a */
#define SLOT_SCROLL_RECT 20u    /* 0x48a7a7 */
#define SLOT_BLIT 26u           /* 0x4899ac */
#define SLOT_GRAB 27u           /* 0x489c6e */
#define SLOT_BLIT_REGION 28u    /* 0x489ede */
#define SLOT_STRETCH 29u        /* 0x48afc8 */
#define SLOT_POLYGON 33u        /* 0x48a924 */
static uint32_t slot_body_va(unsigned index) {
    switch (index) {
    case SLOT_SPAN_SOLID: return 0x00489f84u;
    case SLOT_SPAN_TABLE: return 0x0048a016u;
    case SLOT_RECT_SOLID: return 0x0048a6c4u;
    case SLOT_RECT_TABLE: return 0x0048a74cu;
    case SLOT_PIXEL_GET: return 0x0048a340u;
    case SLOT_PIXEL_PUT: return 0x0048a361u;
    case SLOT_PIXEL_TABLE: return 0x0048a384u;
    case SLOT_SCROLL_SPANS: return 0x0048a08au;
    case SLOT_SCROLL_RECT: return 0x0048a7a7u;
    case SLOT_BLIT: return 0x004899acu;
    case SLOT_GRAB: return 0x00489c6eu;
    case SLOT_BLIT_REGION: return 0x00489edeu;
    case SLOT_STRETCH: return 0x0048afc8u;
    case SLOT_POLYGON: return 0x0048a924u;
    default: return 0;
    }
}

/* ---- the guest view */
static uint32_t r32(const es2_sim_loop *l, uint32_t a) { return l->memory->read32(l->memory->context, a); }
static uint16_t r16(const es2_sim_loop *l, uint32_t a) { return l->memory->read16(l->memory->context, a); }
static uint8_t r8(const es2_sim_loop *l, uint32_t a) { return es2_sim_read8(l->memory, a); }
static void w32(const es2_sim_loop *l, uint32_t a, uint32_t v) { l->memory->write32(l->memory->context, a, v); }
static void w16(const es2_sim_loop *l, uint32_t a, uint16_t v) { l->memory->write16(l->memory->context, a, v); }
static void w8(const es2_sim_loop *l, uint32_t a, uint8_t v) { es2_sim_write8(l->memory, a, v); }
static int32_t s16(const es2_sim_loop *l, uint32_t a) { return (int16_t)r16(l, a); }
static int32_t s32(const es2_sim_loop *l, uint32_t a) { return (int32_t)r32(l, a); }
static int32_t imin(int32_t a, int32_t b) { return a < b ? a : b; }
static int32_t imax(int32_t a, int32_t b) { return a > b ? a : b; }

static int refuse(es2_sim_loop *l, uint32_t va, const char *name, const char *reason) {
    return es2_sim_loop_refuse(l, va, name, reason);
}
/* The original reads, writes or calls through NULL at va (es2_sim_loop_null_refuse). */
static int null_refuse(es2_sim_loop *l, uint32_t va, const char *name, const char *reason) {
    return es2_sim_loop_null_refuse(l, va, name, reason);
}
static int call(es2_sim_loop *l, uint32_t va, const uint32_t *slots, unsigned count, uint32_t *eax) {
    es2_sim_result r;
    if (es2_sim_loop_call(l, va, slots, count, &r)) return 1;
    if (eax) *eax = r.eax;
    return 0;
}
/* Zero-filled guest locals below the own call area (the original's are
 * stack residue; every field read is written first). 0 when exhausted. */
static uint32_t frame_push(es2_sim_loop *l, uint32_t bytes) {
    bytes = (bytes + 3u) & ~3u;
    if (l->stack_hi - l->stack_lo < bytes + 0x400u) return 0;
    l->stack_hi -= bytes;
    for (uint32_t i = 0; i < bytes; i += 4u) w32(l, l->stack_hi + i, 0);
    return l->stack_hi;
}
static void frame_pop(es2_sim_loop *l, uint32_t top) { l->stack_hi = top; }
static int frame_refused(es2_sim_loop *l, uint32_t va, const char *name) {
    return refuse(l, va, name, "the own code's call area is exhausted");
}

/* The surface's pixel address of (x, y): row table T = [0x4a3652], T[0] +
 * T[y + 1] + x (every slot body computes it this way). */
static uint32_t pixel_at(const es2_sim_loop *l, int32_t x, int32_t y) {
    uint32_t t = r32(l, VA_VIEW_ROWS);
    return r32(l, t) + r32(l, t + 4u * (uint32_t)(y + 1)) + (uint32_t)x;
}
static uint32_t pitch(const es2_sim_loop *l) { return r32(l, VA_VIEW_PITCH); }

/* The colour word of a kind-0 pen or fill (0x486741..0x486791, repeated
 * in 0x486a34 and 0x4895ec): the high byte 0 passes the word, 4 reads
 * the ramp table 0x4a4fa5 (8-byte entries {pointer, int16 scale}: the
 * entry of the low word, index ((byte 2 * scale) >> 7) & 0xfffe, the
 * sign-extended word there); any other high byte draws nothing. */
static int resolve_colour(const es2_sim_loop *l, uint32_t c, uint32_t *out) {
    uint32_t hi = c >> 24;
    if (hi == 0) { *out = c; return 1; }
    if (hi != 4u) return 0;
    uint32_t e = VA_RAMPS + ((uint32_t)(int32_t)(int16_t)c << 3);
    uint32_t i = ((((c >> 16) & 0xffu) * (uint32_t)(int32_t)s16(l, e + 4u)) >> 7) & 0xfffeu;
    *out = (uint32_t)s16(l, r32(l, e) + i);
    return 1;
}
/* The origin shift (dx, dy) the primitives add when [C+0x208] is set:
 * clip rect's (l, t) - origin. */
static void origin_shift(const es2_sim_loop *l, uint32_t c, int32_t *dx, int32_t *dy, int only_one) {
    uint32_t f = r32(l, c + 0x208u);
    *dx = *dy = 0;
    if (only_one ? f == 1u : f != 0) {
        *dx = s32(l, c + 0x20cu) - s32(l, c + 0x21cu);
        *dy = s32(l, c + 0x210u) - s32(l, c + 0x220u);
    }
}

/* ======================================================================
 * Clip helpers (listing 0x48699c..0x486e0d). */

/* 0x48699c(a, b, out): the intersection of two rects; EAX = out, or 0
 * (then at most l and r are written). */
int es2_sim_gfx_rect_clip(es2_sim_loop *l, uint32_t a, uint32_t b, uint32_t out, uint32_t *eax) {
    int32_t left = imax(s32(l, a), s32(l, b)), right = imin(s32(l, a + 8u), s32(l, b + 8u));
    if (left > right) { *eax = 0; return 0; }
    w32(l, out, (uint32_t)left);
    w32(l, out + 8u, (uint32_t)right);
    int32_t top = imax(s32(l, a + 4u), s32(l, b + 4u)), bottom = imin(s32(l, a + 12u), s32(l, b + 12u));
    if (top > bottom) { *eax = 0; return 0; }
    w32(l, out + 4u, (uint32_t)top);
    w32(l, out + 12u, (uint32_t)bottom);
    *eax = out;
    return 0;
}

/* 0x4869fc(rect, out): the rect as a span list: word t, word b - t + 1,
 * then that many pairs {l, r}; EAX = out. A negative count is an endless
 * rep stosd (refused); a count past the span buffer would overrun it. */
int es2_sim_gfx_rect_to_spans(es2_sim_loop *l, uint32_t rect, uint32_t out, uint32_t *eax) {
    int32_t left = s32(l, rect), top = s32(l, rect + 4u), right = s32(l, rect + 8u), bottom = s32(l, rect + 12u);
    int32_t count = bottom - top + 1;
    if (count < 0) return refuse(l, 0x004869fcu, "rect_to_spans", "a negative row count (the original's rep stosd runs away)");
    if (count > (int32_t)ES2_SIM_GFX_SPAN_ROWS) return refuse(l, 0x004869fcu, "rect_to_spans", "more rows than the span buffer holds");
    w16(l, out, (uint16_t)top);
    w16(l, out + 2u, (uint16_t)count);
    uint32_t pair = ((uint32_t)left & 0xffffu) | ((uint32_t)right << 16);
    for (int32_t i = 0; i < count; ++i) w32(l, out + 4u + 4u * (uint32_t)i, pair);
    *eax = out;
    return 0;
}

/* 0x486c00(a, b, out): the intersection of two span lists; EAX = out, or
 * 0 when no row overlaps (out then holds a header only). */
int es2_sim_gfx_spans_clip_spans(es2_sim_loop *l, uint32_t a, uint32_t b, uint32_t out, uint32_t *eax) {
    int32_t ya = s16(l, a), a_end = ya + s16(l, a + 2u) - 1;
    int32_t yb = s16(l, b), b_end = yb + s16(l, b + 2u) - 1;
    a += 4u; b += 4u;
    if (ya > b_end || yb > a_end) { *eax = 0; return 0; }
    int32_t d = ya - yb, y;
    if (d > 0) { b += 4u * (uint32_t)d; y = ya; } else { a += 4u * (uint32_t)(-d); y = yb; }
    w16(l, out, (uint16_t)y);
    w16(l, out + 2u, (uint16_t)y);
    int32_t n = imin(a_end, b_end) - y;
    if (n < 0) { *eax = 0; return 0; }
    uint32_t w = out + 4u;
    int found = 0, last = 0;
    for (int32_t r = 0; r <= n; ++r, a += 4u, b += 4u) {
        int32_t la = s16(l, a), ra = s16(l, a + 2u), lb = s16(l, b), rb = s16(l, b + 2u);
        if (la <= rb && ra >= lb) {
            if (!found) {
                found = 1;
                w16(l, out, (uint16_t)((int32_t)(int16_t)r16(l, out) + r));
                w16(l, out + 2u, (uint16_t)(-r));
            }
            last = r;
            w16(l, w, (uint16_t)imax(la, lb)); w += 2u;
            w16(l, w, (uint16_t)imin(ra, rb)); w += 2u;
        } else if (found) {
            w16(l, w, 1u); w += 2u;
            w16(l, w, 0u); w += 2u;
        }
    }
    if (!found) { *eax = 0; return 0; }
    w16(l, out + 2u, (uint16_t)((int32_t)(int16_t)r16(l, out + 2u) + last + 1));
    *eax = out;
    return 0;
}

/* 0x486d15(rect, spans, out): a span list clipped to a rect; as 0x486c00. */
int es2_sim_gfx_rect_clip_spans(es2_sim_loop *l, uint32_t rect, uint32_t spans, uint32_t out, uint32_t *eax) {
    int32_t left = s32(l, rect), top = s32(l, rect + 4u), right = s32(l, rect + 8u), bottom = s32(l, rect + 12u);
    int32_t ys = s16(l, spans), s_end = ys + s16(l, spans + 2u) - 1;
    spans += 4u;
    if (top > s_end || ys > bottom) { *eax = 0; return 0; }
    int32_t d = top - ys, y = ys;
    if (d > 0) { spans += 4u * (uint32_t)d; y = top; }
    w16(l, out, (uint16_t)y);
    w16(l, out + 2u, (uint16_t)y);
    int32_t n = imin(bottom, s_end) - y;
    if (n < 0) { *eax = 0; return 0; }
    uint32_t w = out + 4u;
    int found = 0, last = 0;
    for (int32_t r = 0; r <= n; ++r, spans += 4u) {
        int32_t ls = s16(l, spans), rs = s16(l, spans + 2u);
        if (left <= rs && right >= ls) {
            if (!found) {
                found = 1;
                w16(l, out, (uint16_t)((int32_t)(int16_t)r16(l, out) + r));
                w16(l, out + 2u, (uint16_t)(-r));
            }
            last = r;
            w16(l, w, (uint16_t)imax(ls, left)); w += 2u;
            w16(l, w, (uint16_t)imin(rs, right)); w += 2u;
        } else if (found) {
            w16(l, w, 1u); w += 2u;
            w16(l, w, 0u); w += 2u;
        }
    }
    if (!found) { *eax = 0; return 0; }
    w16(l, out + 2u, (uint16_t)((int32_t)(int16_t)r16(l, out + 2u) + last + 1));
    *eax = out;
    return 0;
}

/* ======================================================================
 * The BMP_8 driver's slot bodies (listing 0x489f84..0x48a016, 0x48a016..
 * 0x48a08a, 0x48a6c4..0x48a7a7, 0x48a340..0x48a3a8, 0x48a08a..0x48a1d4,
 * 0x48a7a7..0x48a8e2, 0x4899ac..0x489c6e, 0x489c6e..0x489ede, 0x489ede..
 * 0x489f84). `args` are the slot's stack arguments (guest values). */

/* Slot 0 (0x489f84, spans, colour) and slot 1 (0x48a016, spans, table): a
 * span list filled with the colour byte, or through the byte table. Rows
 * above 0 are skipped (slot 0; slot 1 reads the pairs without the skip,
 * a bug of the original: refused). */
static int slot_span_fill(es2_sim_loop *l, uint32_t spans, uint32_t arg, int table) {
    int32_t count = r16(l, spans + 2u);
    if (count == 0) return 0;
    int32_t y0 = s16(l, spans);
    if (y0 < 0) {
        if (table) return refuse(l, 0x0048a016u, "span_fill_table", "a span list starting above row 0 (the original reads the wrong pairs)");
        count -= -y0;
        if (count <= 0) return 0;
        spans += 4u * (uint32_t)(-y0);
        y0 = 0;
    }
    uint32_t base = pixel_at(l, 0, y0), step = pitch(l);
    for (int32_t i = 0; i < count; ++i, base += step) {
        int32_t left = s16(l, spans + 4u + 4u * (uint32_t)i), right = s16(l, spans + 6u + 4u * (uint32_t)i);
        int32_t n = right - left + 1;
        if (n <= 0) continue;
        uint32_t p = base + (uint32_t)left;
        if (table) for (int32_t k = 0; k < n; ++k) w8(l, p + (uint32_t)k, r8(l, arg + r8(l, p + (uint32_t)k)));
        else for (int32_t k = 0; k < n; ++k) w8(l, p + (uint32_t)k, (uint8_t)arg);
    }
    return 0;
}

/* Slot 2 (0x48a6c4, rect, colour): the rect filled with the colour byte.
 * Rows: a 32-bit counter when the width is past 16, else the 16-bit bx
 * (65536 rows for a height that is 0 mod 65536); a non-positive height or
 * a negative width is an endless loop. */
static int slot_rect_solid(es2_sim_loop *l, uint32_t rect, uint32_t colour) {
    int32_t left = s32(l, rect), top = s32(l, rect + 4u), right = s32(l, rect + 8u), bottom = s32(l, rect + 12u);
    int32_t h = bottom - top + 1, w = right - left + 1;
    if (w > 16 ? h <= 0 : (h & 0xffff) == 0 || h < 0) return refuse(l, 0x0048a6c4u, "rect_fill_solid", "a non-positive height (the original's row loop runs away)");
    if (w < 0) return refuse(l, 0x0048a6c4u, "rect_fill_solid", "a negative width (the original's rep stosb runs away)");
    if (w <= 16) h &= 0xffff;
    uint32_t p = pixel_at(l, left, top), step = pitch(l);
    for (int32_t y = 0; y < h; ++y, p += step)
        for (int32_t x = 0; x < w; ++x) w8(l, p + (uint32_t)x, (uint8_t)colour);
    return 0;
}

/* Slot 3 (0x48a74c, rect, table): every pixel of the rect through the
 * byte table; a non-positive width or height is an endless loop. */
static int slot_rect_table(es2_sim_loop *l, uint32_t rect, uint32_t table) {
    int32_t left = s32(l, rect), top = s32(l, rect + 4u), right = s32(l, rect + 8u), bottom = s32(l, rect + 12u);
    int32_t h = bottom - top + 1, w = right - left + 1;
    if (h <= 0 || w <= 0) return refuse(l, 0x0048a74cu, "rect_fill_table", "an empty rect (the original's loops run away)");
    uint32_t p = pixel_at(l, left, top), step = pitch(l);
    for (int32_t y = 0; y < h; ++y, p += step)
        for (int32_t x = 0; x < w; ++x) w8(l, p + (uint32_t)x, r8(l, table + r8(l, p + (uint32_t)x)));
    return 0;
}

/* Slot 19 (0x48a08a, spans, point): the span list's pixels moved by the
 * point (dx, dy): every row from the row dy above and dx left; rows
 * ascending for dy < 0 (and dy == 0, dx < 0), descending for dy > 0;
 * dy == 0 and dx > 0 copies each row backwards from its right word. */
static int slot_scroll_spans(es2_sim_loop *l, uint32_t spans, uint32_t point) {
    int32_t dx = s32(l, point), dy = s32(l, point + 4u);
    int32_t count = r16(l, spans + 2u);
    if (count == 0) return 0;
    int32_t row = r16(l, spans), rstep = 1;
    uint32_t p = spans + 4u;
    int backward = 0, from_right = 0;
    if (dy < 0 || (dy == 0 && dx < 0)) {
    } else if (dy > 0) {
        row += count - 1; p += 4u * (uint32_t)(count - 1); rstep = -1;
    } else if (dx > 0) {
        backward = 1; from_right = 1;
    } else {
        return 0;
    }
    for (int32_t i = 0; i < count; ++i, row += rstep, p += (uint32_t)(4 * rstep)) {
        int32_t left = s16(l, p), right = s16(l, p + 2u), n = right - left + 1;
        if (n <= 0) continue;
        int32_t x = from_right ? (int32_t)r16(l, p + 2u) : (int32_t)r16(l, p);
        uint32_t dst = pixel_at(l, x, row), src = pixel_at(l, x - dx, row - dy);
        if (backward) for (int32_t k = 0; k < n; ++k) w8(l, dst - (uint32_t)k, r8(l, src - (uint32_t)k));
        else for (int32_t k = 0; k < n; ++k) w8(l, dst + (uint32_t)k, r8(l, src + (uint32_t)k));
    }
    return 0;
}

/* Slot 20 (0x48a7a7, rect, point): the rect's pixels moved by (dx, dy):
 * a plain forward copy when the source does not overlap, else from the
 * top (dy < 0, or dy == 0 and dx < 0), from the bottom (dy > 0) or each
 * row backwards (dy == 0, dx > 0). A non-positive height or a negative
 * width is an endless loop. */
static int slot_scroll_rect(es2_sim_loop *l, uint32_t rect, uint32_t point) {
    int32_t left = s32(l, rect), top = s32(l, rect + 4u), right = s32(l, rect + 8u), bottom = s32(l, rect + 12u);
    int32_t dx = s32(l, point), dy = s32(l, point + 4u);
    int32_t w = right - left + 1, h = bottom - top + 1;
    int32_t ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
    int from_bottom = 0, backward = 0;
    if (ax >= w && ay >= h) {
    } else if ((dx | dy) == 0) {
        return 0;
    } else if (dy < 0) {
    } else if (dy > 0) {
        from_bottom = 1;
    } else if (dx > 0) {
        backward = 1;
    }
    if (h <= 0) return refuse(l, 0x0048a7a7u, "scroll_rect", "a non-positive height (the original's row loop runs away)");
    if (w < 0) return refuse(l, 0x0048a7a7u, "scroll_rect", "a negative width (the original's rep movsb runs away)");
    for (int32_t i = 0; i < h; ++i) {
        int32_t y = from_bottom ? bottom - i : top + i;
        if (backward) {
            uint32_t dst = pixel_at(l, right, y), src = pixel_at(l, right - dx, y - dy);
            for (int32_t k = 0; k < w; ++k) w8(l, dst - (uint32_t)k, r8(l, src - (uint32_t)k));
        } else {
            uint32_t dst = pixel_at(l, left, y), src = pixel_at(l, left - dx, y - dy);
            for (int32_t k = 0; k < w; ++k) w8(l, dst + (uint32_t)k, r8(l, src + (uint32_t)k));
        }
    }
    return 0;
}

/* The bitmap header H (word h, word w, byte bpp, byte flags, dword size,
 * pixels at +0xa) and the clipped placement slots 26 and 27 share
 * (0x4899ac..0x489ac3 and 0x489c6e..0x489d85): the visible columns and
 * rows of the bitmap at `point`, against the clip rect or, without one,
 * against the surface (pitch [0x4a3676], height [0x4a367a]). 0 when
 * nothing is visible. */
typedef struct placement {
    int32_t x0, y0, cols, rows, skipx, skipy, w, h;
    uint32_t pixels;
} placement;
static int place(const es2_sim_loop *l, uint32_t header, uint32_t point, uint32_t clip, placement *p) {
    int32_t x = s32(l, point), y = s32(l, point + 4u);
    p->w = r16(l, header + 2u); p->h = r16(l, header);
    p->pixels = r32(l, header + 0xau);
    int32_t xe = p->w + x, ye = p->h + y;
    if (clip) {
        int32_t cl = s32(l, clip), ct = s32(l, clip + 4u), cr = s32(l, clip + 8u), cb = s32(l, clip + 12u);
        if (x > cr) return 0;
        p->x0 = imax(x, cl); p->skipx = p->x0 - x;
        if (y > cb) return 0;
        p->y0 = imax(y, ct); p->skipy = p->y0 - y;
        if (xe <= cl) return 0;
        xe = imin(xe, cr + 1);
        if (ye <= ct) return 0;
        ye = imin(ye, cb + 1);
    } else {
        int32_t sw = (int32_t)r32(l, VA_VIEW_PITCH), sh = (int32_t)r32(l, VA_VIEW_HEIGHT);
        if (x >= sw) return 0;
        p->x0 = imax(x, 0); p->skipx = p->x0 - x;
        if (y >= sh) return 0;
        p->y0 = imax(y, 0); p->skipy = p->y0 - y;
        if (xe <= 0) return 0;
        xe = imin(xe, sw);
        if (ye <= 0) return 0;
        ye = imin(ye, sh);
    }
    p->cols = xe - p->x0;
    p->rows = ye - p->y0;
    return 1;
}

/* Slot 26 (0x4899ac, header, point, clip, flags): the bitmap drawn at the
 * point, clipped; flags & 3 place it (0 as is, 1 flipped vertically, 2
 * each row reversed (mirrored horizontally), 3 rotated by 180 degrees
 * (the last byte first); in vitro identical for all four; the jump table
 * 0x489ac3); flags 0x10 of the header copy every byte, else the zero
 * bytes are skipped. A non-positive column or row count is an endless
 * loop except the opaque forward kernels' zero columns. */
static int slot_blit(es2_sim_loop *l, uint32_t header, uint32_t point, uint32_t clip, uint32_t flags) {
    placement p;
    if (!place(l, header, point, clip, &p)) return 0;
    unsigned way = flags & 3u;
    int opaque = (r8(l, header + 5u) & 0x10u) != 0;
    if (p.rows <= 0) return refuse(l, 0x004899acu, "blit", "no visible row (the original's row loop runs away)");
    if (p.cols < 0 || (p.cols == 0 && (!opaque || way >= 2u))) return refuse(l, 0x004899acu, "blit", "no visible column (the original's column loop runs away)");
    uint32_t src, dst;
    int32_t sstep, dstep, sdir;
    int32_t pt = (int32_t)pitch(l);
    switch (way) {
    case 0:
        src = p.pixels + (uint32_t)(p.skipy * p.w + p.skipx); dst = pixel_at(l, p.x0, p.y0);
        sstep = p.w - p.cols; dstep = pt - p.cols; sdir = 1; break;
    case 1:
        src = p.pixels + (uint32_t)((p.h - p.skipy - p.rows) * p.w + p.skipx); dst = pixel_at(l, p.x0, p.y0 + p.rows - 1);
        sstep = p.w - p.cols; dstep = -pt - p.cols; sdir = 1; break;
    case 2:
        src = p.pixels + (uint32_t)((p.skipy + p.rows) * p.w - 1 - p.skipx); dst = pixel_at(l, p.x0, p.y0 + p.rows - 1);
        sstep = p.cols - p.w; dstep = -pt - p.cols; sdir = -1; break;
    default:
        src = p.pixels + (uint32_t)((p.h - p.skipy) * p.w - 1 - p.skipx); dst = pixel_at(l, p.x0, p.y0);
        sstep = p.cols - p.w; dstep = pt - p.cols; sdir = -1; break;
    }
    for (int32_t y = 0; y < p.rows; ++y) {
        for (int32_t x = 0; x < p.cols; ++x) {
            uint8_t b = r8(l, src);
            if (opaque || b) w8(l, dst, b);
            src += (uint32_t)sdir; ++dst;
        }
        src += (uint32_t)sstep; dst += (uint32_t)dstep;
    }
    return 0;
}

/* Slot 27 (0x489c6e, header, point, clip, flags): the surface's pixels
 * under the bitmap copied into it (the roles of slot 26 swapped, always
 * every byte; the jump table 0x489d85). */
static int slot_grab(es2_sim_loop *l, uint32_t header, uint32_t point, uint32_t clip, uint32_t flags) {
    placement p;
    if (!place(l, header, point, clip, &p)) return 0;
    unsigned way = flags & 3u;
    if (p.rows <= 0) return refuse(l, 0x00489c6eu, "grab", "no visible row (the original's row loop runs away)");
    if (p.cols < 0 || (p.cols == 0 && way >= 2u)) return refuse(l, 0x00489c6eu, "grab", "no visible column (the original's column loop runs away)");
    uint32_t src, dst;
    int32_t sstep, dstep, ddir;
    int32_t pt = (int32_t)pitch(l);
    switch (way) {
    case 0:
        dst = p.pixels + (uint32_t)(p.skipy * p.w + p.skipx); src = pixel_at(l, p.x0, p.y0);
        dstep = p.w - p.cols; sstep = pt - p.cols; ddir = 1; break;
    case 1:
        dst = p.pixels + (uint32_t)((p.h - p.skipy - p.rows) * p.w + p.skipx); src = pixel_at(l, p.x0, p.y0 + p.rows - 1);
        dstep = p.w - p.cols; sstep = -pt - p.cols; ddir = 1; break;
    case 2:
        dst = p.pixels + (uint32_t)((p.skipy + p.rows) * p.w - 1 - p.skipx); src = pixel_at(l, p.x0, p.y0 + p.rows - 1);
        dstep = p.cols - p.w; sstep = -pt - p.cols; ddir = -1; break;
    default:
        dst = p.pixels + (uint32_t)((p.h - p.skipy) * p.w - 1 - p.skipx); src = pixel_at(l, p.x0, p.y0);
        dstep = p.cols - p.w; sstep = pt - p.cols; ddir = -1; break;
    }
    for (int32_t y = 0; y < p.rows; ++y) {
        for (int32_t x = 0; x < p.cols; ++x) {
            w8(l, dst, r8(l, src));
            dst += (uint32_t)ddir; ++src;
        }
        src += (uint32_t)sstep; dst += (uint32_t)dstep;
    }
    return 0;
}

/* Slot 28 (0x489ede, header, point, clip, flags): slot 26 per clip
 * region when the clip mode is 2 (kind-0 entries as rects, kind-2 span
 * lists row by row as one-row rects; a zero-row list is an endless loop),
 * else slot 26 once with `clip`. */
static int slot_blit_region(es2_sim_loop *l, uint32_t header, uint32_t point, uint32_t clip, uint32_t flags) {
    uint32_t c = r32(l, VA_CONTEXT);
    if (r32(l, c + 0x204u) != 2u) return slot_blit(l, header, point, clip, flags);
    uint32_t count = r32(l, c);
    if (count == 0) return 0;
    uint32_t top = l->stack_hi, rect = frame_push(l, 16u);
    if (!rect) return frame_refused(l, 0x00489edeu, "blit_region");
    int bad = 0;
    for (uint32_t i = 0; i < count && !bad; ++i) {
        uint32_t e = r32(l, c + 4u + 4u * i), kind = r32(l, e);
        if (kind == 0) {
            bad = slot_blit(l, header, point, e + 4u, flags);
        } else if (kind == 2u) {
            uint32_t spans = r32(l, e + 4u);
            uint32_t y = r16(l, spans), n = r16(l, spans + 2u);
            if (n == 0) { bad = refuse(l, 0x00489edeu, "blit_region", "a zero-row span list (the original's row loop runs away)"); break; }
            for (uint32_t k = 0; k < n && !bad; ++k, ++y) {
                w32(l, rect, r16(l, spans + 4u + 4u * k));
                w32(l, rect + 4u, y);
                w32(l, rect + 8u, r16(l, spans + 6u + 4u * k));
                w32(l, rect + 12u, y);
                bad = slot_blit(l, header, point, rect, flags);
            }
        }
    }
    frame_pop(l, top);
    return bad;
}

/* Slot 29 (0x48afc8, header, w, h, point, clip, flags): the bitmap
 * stretched to w x h pixels at the point (listing 0x48afc8..0x48b520).
 * Source steps per destination column: q = bw / w whole bytes and an
 * error term (+ bw % w from -w; one more byte when it reaches 0); per row
 * the same with bh / h rows. Flags 1 start at the last row and step
 * upwards, 2 start at the row's last byte and step leftwards, 3 both; any
 * other value draws as 0. The clip: the rect `clip` (without one the
 * surface, pitch [0x4a3676] x height [0x4a367a]) once in modes 0 and 1;
 * per region in mode 2 (a kind-0 entry's rect; a kind-2 span list row by
 * row; any other kind ends the walk, the list pointer stays on it). The
 * destination is row table [0] + y * pitch + x (not the per-row
 * offsets). Header flags 0x10 copy every byte, else zero bytes are
 * skipped. */
static int32_t mul32(int32_t a, int32_t b) { return (int32_t)((uint32_t)a * (uint32_t)b); }
static int32_t add32(int32_t a, int32_t b) { return (int32_t)((uint32_t)a + (uint32_t)b); }
static int32_t neg32(int32_t a) { return (int32_t)(0u - (uint32_t)a); }
static int slot_stretch(es2_sim_loop *l, const uint32_t *args) {
    uint32_t header = args[0], point = args[3], clip = args[4], flags = args[5];
    int32_t dw = (int32_t)args[1], dh = (int32_t)args[2];
    if (dw == 0 || dh == 0) return 0;
    int32_t sw = s32(l, VA_VIEW_PITCH), x0, y0, x1, y1;
    if (clip) {
        x0 = s32(l, clip); y0 = s32(l, clip + 4u); x1 = s32(l, clip + 8u); y1 = s32(l, clip + 12u);
    } else {
        x0 = 0; y0 = 0; x1 = sw - 1; y1 = s32(l, VA_VIEW_HEIGHT) - 1;
    }
    int32_t bw = s16(l, header + 2u), bh = s16(l, header);
    /* 0x48b04f..0x48b0a5: the steps (idiv: truncated quotient and remainder). */
    int32_t xr = bw % dw, xq = bw / dw, yr = bh % dh;
    int32_t ystep = mul32(bh / dh, bw), stride = bw, xdir = 1;
    uint32_t src0 = r32(l, header + 0xau);
    switch (flags) {   /* 0x48b0a8 */
    case 1u:
        ystep = neg32(ystep); stride = neg32(stride);
        src0 += (uint32_t)mul32(bh - 1, bw);
        break;
    case 2u:
        xq = neg32(xq); xdir = -1;
        src0 += (uint32_t)(bw - 1);
        break;
    case 3u:
        xq = neg32(xq); xdir = -1; ystep = neg32(ystep); stride = neg32(stride);
        src0 += (uint32_t)(mul32(bh, bw) - 1);
        break;
    default: break;
    }
    uint32_t c = r32(l, VA_CONTEXT), mode = r32(l, c + 0x204u), count = 0, list = 0;
    if (mode == 0 || mode == 1u) count = 1;
    else if (mode == 2u) { count = r32(l, c); list = c + 4u; }
    if (count > 0x80u) return refuse(l, 0x0048afc8u, "stretch", "more regions than the clip record holds (the original walks past it)");
    while (count-- != 0) {
        uint32_t spans = 0;
        if (r32(l, r32(l, VA_CONTEXT) + 0x204u) == 2u) {   /* 0x48b160 */
            uint32_t e = r32(l, list), kind = r32(l, e);
            if (kind == 0) {
                x0 = s32(l, e + 4u); y0 = s32(l, e + 8u); x1 = s32(l, e + 12u); y1 = s32(l, e + 16u);
            } else if (kind == 2u) {
                spans = r32(l, e + 4u);
                x0 = 0; x1 = s32(l, VA_VIEW_PITCH) - 1;
                y0 = s16(l, spans); y1 = s16(l, spans + 2u) + y0 - 1;
            } else {
                continue;
            }
            list += 4u;
        }
        /* 0x48b1fe..0x48b2ab: the clip against the stretched rect. */
        int32_t px = s32(l, point), py = s32(l, point + 4u);
        if (px > x1 || add32(px, dw) <= x0 || py > y1 || add32(py, dh) <= y0) continue;
        x0 = imax(px, x0); x1 = imin(add32(px, dw) - 1, x1);
        y0 = imax(py, y0); y1 = imin(add32(py, dh) - 1, y1);
        int32_t cols = x1 - x0 + 1, rows = y1 - y0 + 1;
        if (rows < 0) return refuse(l, 0x0048afc8u, "stretch", "a negative row count (the original's row loop runs away)");
        if (!spans && cols < 0 && rows > 0) return refuse(l, 0x0048afc8u, "stretch", "a negative column count (the original's column loop runs away)");
        sw = s32(l, VA_VIEW_PITCH);
        uint32_t dst = r32(l, r32(l, VA_VIEW_ROWS)) + (uint32_t)mul32(y0, sw);
        int32_t rowskip = sw - cols, yerr = neg32(dh), xerr0 = neg32(dw);
        uint32_t srow = src0;
        if (y0 > py) {   /* 0x48b2fa: the rows above the clip skipped */
            int32_t d = y0 - py, t = mul32(d, yr);
            yerr += t % dh;
            srow += (uint32_t)(mul32(t / dh, stride) + mul32(d, ystep));
        }
        if (spans) {     /* 0x48b33d: the span rows above y0 skipped */
            int32_t sy = s16(l, spans);
            if (y0 > sy) spans += (uint32_t)(y0 - sy) * 4u;
        } else {         /* 0x48b358: the columns left of the clip skipped */
            dst += (uint32_t)x0;
            if (x0 > px) {
                int32_t d = x0 - px, t = mul32(d, xr);
                srow += (uint32_t)mul32(d, xq);
                xerr0 += t % dw;
                srow += (uint32_t)(xdir < 0 ? neg32(t / dw) : t / dw);
            }
        }
        for (int32_t row = 0; row < rows; ++row) {
            uint32_t src = srow;
            int32_t xe = xerr0, n = cols;
            int skip = 0;
            if (spans) {   /* 0x48b3b5: this row's span clipped to the stretched columns */
                px = s32(l, point);
                int32_t rx0 = imax(px, s16(l, spans + 4u)), rx1 = imin(add32(px, dw) - 1, s16(l, spans + 6u));
                n = rx1 - rx0 + 1;
                spans += 4u;
                if (n <= 0) {
                    dst += (uint32_t)s32(l, VA_VIEW_PITCH);
                    skip = 1;
                } else {
                    dst += (uint32_t)rx0;
                    rowskip = s32(l, VA_VIEW_PITCH) - 1 - rx1;
                    if (rx0 > px) {
                        int32_t d = rx0 - px, t = mul32(d, xr);
                        src += (uint32_t)mul32(d, xq);
                        xe += t % dw;
                        src += (uint32_t)(xdir < 0 ? neg32(t / dw) : t / dw);
                    }
                }
            }
            if (!skip) {   /* 0x48b46c: the three kernels (one behaviour) */
                int opaque = (r8(l, header + 5u) & 0x10u) != 0;
                for (int32_t i = 0; i < n; ++i) {
                    uint8_t b = r8(l, src);
                    if (opaque || b) w8(l, dst, b);
                    ++dst;
                    src += (uint32_t)xq;
                    xe = add32(xe, xr);
                    if (xe >= 0) { src += (uint32_t)xdir; xe -= dw; }
                }
                dst += (uint32_t)rowskip;
            }
            srow += (uint32_t)ystep;   /* 0x48b4de */
            yerr = add32(yerr, yr);
            if (yerr >= 0) { srow += (uint32_t)stride; yerr -= dh; }
        }
    }
    return 0;
}

int es2_sim_gfx_slot(es2_sim_loop *l, unsigned index, const uint32_t *args, unsigned count, uint32_t *eax) {
    uint32_t target = r32(l, VA_SLOTS + 4u * index);
    if (eax) *eax = 0;
    if (target != slot_body_va(index) || target == 0) return call(l, target, args, count, eax);
    switch (index) {
    case SLOT_SPAN_SOLID: return slot_span_fill(l, args[0], args[1], 0);
    case SLOT_SPAN_TABLE: return slot_span_fill(l, args[0], args[1], 1);
    case SLOT_RECT_SOLID: return slot_rect_solid(l, args[0], args[1]);
    case SLOT_RECT_TABLE: return slot_rect_table(l, args[0], args[1]);
    case SLOT_PIXEL_GET:
        if (eax) *eax = r8(l, pixel_at(l, s32(l, args[0]), s32(l, args[0] + 4u)));
        return 0;
    case SLOT_PIXEL_PUT:
        w8(l, pixel_at(l, s32(l, args[0]), s32(l, args[0] + 4u)), (uint8_t)args[1]);
        return 0;
    case SLOT_PIXEL_TABLE: {
        uint32_t p = pixel_at(l, s32(l, args[0]), s32(l, args[0] + 4u));
        w8(l, p, r8(l, args[1] + r8(l, p)));
        return 0;
    }
    case SLOT_SCROLL_SPANS: return slot_scroll_spans(l, args[0], args[1]);
    case SLOT_SCROLL_RECT: return slot_scroll_rect(l, args[0], args[1]);
    case SLOT_BLIT: return slot_blit(l, args[0], args[1], args[2], args[3]);
    case SLOT_GRAB: return slot_grab(l, args[0], args[1], args[2], args[3]);
    case SLOT_BLIT_REGION: return slot_blit_region(l, args[0], args[1], args[2], args[3]);
    case SLOT_STRETCH: return slot_stretch(l, args);
    case SLOT_POLYGON: return es2_sim_gfx_polygon(l, args[0], args[1]);
    default: return call(l, target, args, count, eax);
    }
}
static int slot2(es2_sim_loop *l, unsigned index, uint32_t a, uint32_t b) {
    uint32_t args[2] = {a, b};
    return es2_sim_gfx_slot(l, index, args, 2u, NULL);
}
static int slot4(es2_sim_loop *l, unsigned index, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    uint32_t args[4] = {a, b, c, d};
    return es2_sim_gfx_slot(l, index, args, 4u, NULL);
}

/* ======================================================================
 * The fills (listing 0x486704..0x48699c, 0x486a34..0x486c00) and the
 * point (0x4895ec..0x489789). */

/* 0x48690a: the kind-4 fill's rect outline: the line 0x483a04 four
 * times ((r,t)-(l,t), (l,b)-(l,t), (l,b)-(r,b), (r,t)-(r,b)) with a
 * temporary kind-0 pen {0, [fill+4]} as [C+0x224], restored after. */
static int ramp_outline(es2_sim_loop *l, uint32_t rect) {
    uint32_t c = r32(l, VA_CONTEXT), fill = r32(l, c + 0x228u);
    uint32_t top = l->stack_hi, f = frame_push(l, 24u);
    if (!f) return frame_refused(l, 0x0048690au, "rect_fill");
    uint32_t pen = f, a = f + 8u, b = f + 16u, old = r32(l, c + 0x224u);
    w32(l, pen, 0); w32(l, pen + 4u, r32(l, fill + 4u));
    w32(l, c + 0x224u, pen);
    uint32_t left = r32(l, rect), t = r32(l, rect + 4u), right = r32(l, rect + 8u), bottom = r32(l, rect + 12u);
    uint32_t args[2] = {b, a};
    int bad = 0;
    w32(l, a, left); w32(l, a + 4u, t); w32(l, b, right); w32(l, b + 4u, t);
    bad = call(l, VA_LINE, args, 2u, NULL);
    if (!bad) { w32(l, b, left); w32(l, b + 4u, bottom); bad = call(l, VA_LINE, args, 2u, NULL); }
    if (!bad) { w32(l, a, right); w32(l, a + 4u, bottom); bad = call(l, VA_LINE, args, 2u, NULL); }
    if (!bad) { w32(l, b, right); w32(l, b + 4u, t); bad = call(l, VA_LINE, args, 2u, NULL); }
    c = r32(l, VA_CONTEXT);
    w32(l, c + 0x224u, old);
    frame_pop(l, top);
    return bad;
}

/* 0x486704(rect): the rect filled with the current fill: kind 0 through
 * slot 2 with the resolved colour, kind 6 through slot 3 with the table
 * fill+4, kind 4 as the outline above (the unshifted rect); the rect
 * shifted by the origin when [C+0x208] is set, then clipped by the mode:
 * 0 as is, 1 against the clip rect, else against every region (kind-0
 * rects through the slot; kind-2 span lists: the rect clipped once into
 * itself and converted to span buffer B, each intersection in A filled
 * through slot 0 with the colour argument). */
int es2_sim_gfx_rect_fill(es2_sim_loop *l, uint32_t rect) {
    uint32_t c = r32(l, VA_CONTEXT), fill = r32(l, c + 0x228u), kind = r32(l, fill);
    unsigned writer;
    uint32_t colour;
    if (kind == 0) {
        writer = SLOT_RECT_SOLID;
        if (!resolve_colour(l, r32(l, fill + 4u), &colour)) return 0;
    } else if (kind == 4u) {
        return ramp_outline(l, rect);
    } else if (kind == 6u) {
        writer = SLOT_RECT_TABLE;
        colour = fill + 4u;
    } else {
        return 0;
    }
    int32_t dx, dy;
    origin_shift(l, c, &dx, &dy, 0);
    uint32_t top = l->stack_hi, f = frame_push(l, 32u);
    if (!f) return frame_refused(l, 0x00486704u, "rect_fill");
    uint32_t local = f, out = f + 16u;
    w32(l, local, r32(l, rect) + (uint32_t)dx);
    w32(l, local + 4u, r32(l, rect + 4u) + (uint32_t)dy);
    w32(l, local + 8u, r32(l, rect + 8u) + (uint32_t)dx);
    w32(l, local + 12u, r32(l, rect + 12u) + (uint32_t)dy);
    uint32_t mode = r32(l, c + 0x204u), r;
    int bad = 0;
    if (mode == 0) {
        bad = slot2(l, writer, local, colour);
    } else if (mode == 1u) {
        bad = es2_sim_gfx_rect_clip(l, local, c + 0x20cu, out, &r);
        if (!bad && r) bad = slot2(l, writer, r, colour);
    } else {
        uint32_t count = r32(l, c);
        int converted = 0;
        for (uint32_t i = count; i-- > 0 && !bad;) {
            uint32_t e = r32(l, c + 4u + 4u * i), ekind = r32(l, e);
            if (ekind == 0) {
                bad = es2_sim_gfx_rect_clip(l, local, e + 4u, out, &r);
                if (!bad && r) bad = slot2(l, writer, r, colour);
            } else if (ekind == 2u) {
                if (!converted) {
                    converted = 1;
                    bad = es2_sim_gfx_rect_clip(l, c + 0x20cu, local, local, &r);
                    if (bad || !r) break;
                    bad = es2_sim_gfx_rect_to_spans(l, local, r32(l, VA_SPANS_B), &r);
                    if (bad) break;
                }
                bad = es2_sim_gfx_spans_clip_spans(l, r32(l, VA_SPANS_B), r32(l, e + 4u), r32(l, VA_SPANS_A), &r);
                if (!bad && r) bad = slot2(l, SLOT_SPAN_SOLID, r, colour);
            }
        }
    }
    frame_pop(l, top);
    return bad;
}

/* 0x486a34(spans, point): the span list moved by the point (and the
 * origin shift when [C+0x208] is set) into span buffer A, filled with the
 * current fill: kind 0 through slot 0 with the resolved colour, kind 6
 * through slot 1 with fill+4; the mode clips as in 0x486704 (the
 * intersections in B). A zero-row list is an endless copy loop. */
int es2_sim_gfx_span_fill(es2_sim_loop *l, uint32_t spans, uint32_t point) {
    uint32_t c = r32(l, VA_CONTEXT), fill = r32(l, c + 0x228u), kind = r32(l, fill);
    unsigned writer;
    uint32_t colour;
    if (kind == 0) {
        writer = SLOT_SPAN_SOLID;
        if (!resolve_colour(l, r32(l, fill + 4u), &colour)) return 0;
    } else if (kind == 6u) {
        writer = SLOT_SPAN_TABLE;
        colour = fill + 4u;
    } else {
        return 0;
    }
    int32_t dx, dy;
    origin_shift(l, c, &dx, &dy, 0);
    dx += s32(l, point); dy += s32(l, point + 4u);
    uint32_t a = r32(l, VA_SPANS_A), b = r32(l, VA_SPANS_B);
    uint32_t count = r16(l, spans + 2u);
    if (count == 0) return refuse(l, 0x00486a34u, "span_fill", "a zero-row span list (the original's copy loop runs away)");
    if (count > ES2_SIM_GFX_SPAN_ROWS) return refuse(l, 0x00486a34u, "span_fill", "more rows than the span buffer holds");
    w16(l, a, (uint16_t)(r16(l, spans) + (uint32_t)dy));
    w16(l, a + 2u, (uint16_t)count);
    for (uint32_t i = 0; i < count; ++i) {
        w16(l, a + 4u + 4u * i, (uint16_t)(r16(l, spans + 4u + 4u * i) + (uint32_t)dx));
        w16(l, a + 6u + 4u * i, (uint16_t)(r16(l, spans + 6u + 4u * i) + (uint32_t)dx));
    }
    uint32_t mode = r32(l, c + 0x204u), r;
    if (mode == 0) return slot2(l, writer, a, colour);
    if (mode == 1u) {
        if (es2_sim_gfx_rect_clip_spans(l, c + 0x20cu, a, b, &r)) return 1;
        return r ? slot2(l, writer, r, colour) : 0;
    }
    uint32_t regions = r32(l, c);
    for (uint32_t i = regions; i-- > 0;) {
        uint32_t e = r32(l, c + 4u + 4u * i), ekind = r32(l, e);
        if (ekind == 0) {
            if (es2_sim_gfx_rect_clip_spans(l, e + 4u, a, b, &r)) return 1;
        } else if (ekind == 2u) {
            if (es2_sim_gfx_spans_clip_spans(l, a, r32(l, e + 4u), b, &r)) return 1;
        } else {
            continue;
        }
        if (r && slot2(l, writer, r, colour)) return 1;
    }
    return 0;
}

/* 0x4895ec(point): the point in the current pen: kind 0 through slot 5
 * with the resolved colour, kind 2 through slot 6 with pen+4; shifted by
 * the origin when [C+0x208] is set; mode 0 as is, 1 inside the clip rect,
 * else inside the first region (from the last) that holds it (kind-0
 * rects, kind-2 span lists). */
int es2_sim_gfx_point(es2_sim_loop *l, uint32_t point) {
    uint32_t c = r32(l, VA_CONTEXT), pen = r32(l, c + 0x224u), kind = r32(l, pen);
    unsigned writer;
    uint32_t colour;
    if (kind == 0) {
        writer = SLOT_PIXEL_PUT;
        if (!resolve_colour(l, r32(l, pen + 4u), &colour)) return 0;
    } else if (kind == 2u) {
        writer = SLOT_PIXEL_TABLE;
        colour = pen + 4u;
    } else {
        return 0;
    }
    int32_t dx, dy;
    origin_shift(l, c, &dx, &dy, 0);
    int32_t x = s32(l, point) + dx, y = s32(l, point + 4u) + dy;
    uint32_t top = l->stack_hi, p = frame_push(l, 8u);
    if (!p) return frame_refused(l, 0x004895ecu, "point");
    w32(l, p, (uint32_t)x); w32(l, p + 4u, (uint32_t)y);
    uint32_t mode = r32(l, c + 0x204u);
    int hit = 0;
    if (mode == 0) {
        hit = 1;
    } else if (mode == 1u) {
        uint32_t k = c + 0x20cu;
        hit = x >= s32(l, k) && x <= s32(l, k + 8u) && y >= s32(l, k + 4u) && y <= s32(l, k + 12u);
    } else {
        for (uint32_t i = r32(l, c); i-- > 0 && !hit;) {
            uint32_t e = r32(l, c + 4u + 4u * i), ekind = r32(l, e);
            if (ekind == 0) {
                hit = x >= s32(l, e + 4u) && x <= s32(l, e + 12u) && y >= s32(l, e + 8u) && y <= s32(l, e + 16u);
            } else if (ekind == 2u) {
                uint32_t s = r32(l, e + 4u);
                int32_t y0 = s16(l, s), n = s16(l, s + 2u);
                if (y < y0 || y >= y0 + n) continue;
                uint32_t pair = s + 4u + 4u * (uint32_t)(y - y0);
                hit = x >= s16(l, pair) && x <= s16(l, pair + 2u);
            }
        }
    }
    int bad = hit ? slot2(l, writer, p, colour) : 0;
    frame_pop(l, top);
    return bad;
}

/* ======================================================================
 * The ellipse (listing 0x48817c..0x4887cb) and 0x4887cb. */

uint32_t es2_sim_gfx_abs(uint32_t v) { return (int32_t)v < 0 ? (uint32_t)-(int32_t)v : v; }

/* Truncating halves (sar, +1 for a negative odd value: the listing's
 * sar; jns; adc). */
static int32_t half(int32_t v) { int32_t h = v >> 1; return (v < 0 && (v & 1)) ? h + 1 : h; }

/* The midpoint ellipse (a, b half axes) into span buffer B as 2b rows
 * from -b: the top rows forward from B+4, the bottom rows backward from
 * the last pair. `pass` 0 (kind-0 fill): pairs (-x, x); pass 1: the left
 * arc (-x, -x_prev); pass 2: the right arc (x_prev, x). */
static void ellipse_rows(es2_sim_loop *l, uint32_t buffer, int32_t a, int32_t b, int pass) {
    int32_t x = 0, y = b, x_prev = 0;
    int32_t a2 = a * a, b2 = b * b, a2x2 = 2 * a2, b2x2 = 2 * b2;
    int32_t d = b2 - a2 * b + (a2 < 0 ? (a2 + 3) >> 2 : a2 >> 2);
    int32_t dxa = 0, dya = a2x2 * b, ymin = -b, ymax = b;
    w16(l, buffer, (uint16_t)ymin);
    w16(l, buffer + 2u, (uint16_t)(ymax - ymin));
    uint32_t fwd = buffer + 4u, tail = buffer + 4u + 4u * (uint32_t)(ymax - ymin) - 2u;
#define EMIT_TOP(left, right) do { if (-y >= ymin && -y < ymax) { w16(l, fwd, (uint16_t)(left)); w16(l, fwd + 2u, (uint16_t)(right)); fwd += 4u; emitted = 1; } } while (0)
#define EMIT_BOTTOM(left, right) do { if (y <= ymax && y > ymin) { w16(l, tail, (uint16_t)(right)); w16(l, tail - 2u, (uint16_t)(left)); tail -= 4u; emitted = 1; } } while (0)
    while (dxa < dya) {
        if (d > 0) {
            int emitted = 0;
            if (pass == 0) { EMIT_TOP(-x, x); EMIT_BOTTOM(-x, x); }
            else if (pass == 1) { EMIT_TOP(-x, -x_prev); EMIT_BOTTOM(-x, -x_prev); }
            else { EMIT_TOP(x_prev, x); EMIT_BOTTOM(x_prev, x); }
            if (emitted) x_prev = x + 1;
            --y; dya -= a2x2; d -= dya;
        }
        ++x; dxa += b2x2; d += b2 + dxa;
    }
    d += half(half(3 * (a2 - b2)) - (dxa + dya));
    while (y > 0) {
        int emitted = 0;
        if (pass == 0) { EMIT_TOP(-x, x); EMIT_BOTTOM(-x, x); }
        else if (pass == 1) { EMIT_TOP(-x, -x_prev); EMIT_BOTTOM(-x, -x_prev); }
        else { EMIT_TOP(x_prev, x); EMIT_BOTTOM(x_prev, x); }
        (void)emitted;
        if (d < 0) { ++x; x_prev = x; dxa += b2x2; d += dxa; }
        --y; dya -= a2x2; d += a2 - dya;
    }
#undef EMIT_TOP
#undef EMIT_BOTTOM
}

/* 0x48817c(rect): the ellipse inscribed in the rect with the current
 * fill (kind 0 solid; kind 4: the fill set to kind 0 for two span fills,
 * the left and the right arc, then back to 4; other kinds draw nothing):
 * centre ((l+r)>>1, (t+b)>>1), half axes |r-l|>>1 and |b-t|>>1, the rows
 * in span buffer B through 0x486a34(B, &centre). A zero half axis takes
 * the degenerate line paths whose temporary pen's kind is stack residue
 * (refused). */
int es2_sim_gfx_ellipse(es2_sim_loop *l, uint32_t rect) {
    uint32_t c = r32(l, VA_CONTEXT), fill = r32(l, c + 0x228u), kind = r32(l, fill);
    if (kind != 0 && kind != 4u) return 0;
    int32_t left = s32(l, rect), t = s32(l, rect + 4u), right = s32(l, rect + 8u), bottom = s32(l, rect + 12u);
    int32_t cx = (left + right) >> 1, cy = (t + bottom) >> 1;
    int32_t a = (int32_t)es2_sim_gfx_abs((uint32_t)(right - left)) >> 1;
    int32_t b = (int32_t)es2_sim_gfx_abs((uint32_t)(bottom - t)) >> 1;
    if (a == 0 || b == 0) return refuse(l, 0x0048817cu, "ellipse", "a zero half axis (the original's line path uses an uninitialised pen)");
    if (-b >= b) return 0;
    if (2 * b > (int32_t)ES2_SIM_GFX_SPAN_ROWS) return refuse(l, 0x0048817cu, "ellipse", "more rows than the span buffer holds");
    uint32_t top = l->stack_hi, centre = frame_push(l, 8u);
    if (!centre) return frame_refused(l, 0x0048817cu, "ellipse");
    w32(l, centre, (uint32_t)cx); w32(l, centre + 4u, (uint32_t)cy);
    uint32_t buffer = r32(l, VA_SPANS_B);
    int bad;
    if (kind == 0) {
        ellipse_rows(l, buffer, a, b, 0);
        bad = es2_sim_gfx_span_fill(l, buffer, centre);
    } else {
        w32(l, fill, 0);
        ellipse_rows(l, buffer, a, b, 1);
        bad = es2_sim_gfx_span_fill(l, buffer, centre);
        if (!bad) {
            ellipse_rows(l, buffer, a, b, 2);
            bad = es2_sim_gfx_span_fill(l, buffer, centre);
        }
        w32(l, fill, 4u);
    }
    frame_pop(l, top);
    return bad;
}

/* ======================================================================
 * Bitmaps (listing 0x4816a8..0x481910, 0x4827bb..0x4827d4, 0x4887d8..
 * 0x4888c4, 0x488974..0x488a7c, 0x487e60..0x488178). */

/* 0x4887d8(header, point, flags): the bitmap at the point (shifted by
 * the origin when [C+0x208] == 1) through slot 26 (mode 0, no clip),
 * slot 28 with the clip rect (mode 1, and mode 2 with regions). An RLE
 * bitmap (flags low nibble 1) takes the dead decoder 0x488df8. */
int es2_sim_gfx_bitmap_draw(es2_sim_loop *l, uint32_t header, uint32_t point, uint32_t flags) {
    uint32_t c = r32(l, VA_CONTEXT);
    int32_t dx, dy;
    origin_shift(l, c, &dx, &dy, 1);
    uint32_t top = l->stack_hi, p = frame_push(l, 8u);
    if (!p) return frame_refused(l, 0x004887d8u, "bitmap_draw");
    w32(l, p, r32(l, point) + (uint32_t)dx);
    w32(l, p + 4u, r32(l, point + 4u) + (uint32_t)dy);
    int bad = 0;
    if ((r8(l, header + 5u) & 0xfu) == 1u) {
        bad = refuse(l, 0x004887d8u, "bitmap_draw", "an RLE bitmap (the decoder 0x488df8 is dead code)");
    } else {
        c = r32(l, VA_CONTEXT);
        uint32_t mode = r32(l, c + 0x204u);
        if (mode == 0) bad = slot4(l, SLOT_BLIT, header, p, 0, flags);
        else if (mode == 1u) bad = slot4(l, SLOT_BLIT_REGION, header, p, c + 0x20cu, flags);
        else if (mode == 2u && (int32_t)r32(l, c) > 0) bad = slot4(l, SLOT_BLIT_REGION, header, p, c + 0x20cu, flags);
    }
    frame_pop(l, top);
    return bad;
}

/* 0x4827bb(surface, header, point, flags): 0x4887d8 (the surface is not
 * used). */
int es2_sim_gfx_bitmap_draw_on(es2_sim_loop *l, uint32_t surface, uint32_t header, uint32_t point, uint32_t flags) {
    (void)surface;
    return es2_sim_gfx_bitmap_draw(l, header, point, flags);
}

/* 0x4816a8(object, point, flags): the GLBitmap drawn: its header at +4
 * through 0x4827bb on the current surface 0x48d99e(); a compressed one
 * (word +0x12) takes the dead decoder 0x481910. */
int es2_sim_gfx_bitmap_blit(es2_sim_loop *l, uint32_t object, uint32_t point, uint32_t flags) {
    if (r16(l, object + 0x12u)) return refuse(l, 0x004816a8u, "bitmap_blit", "a compressed GLBitmap (the decoder 0x481910 is dead code)");
    return es2_sim_gfx_bitmap_draw_on(l, r32(l, VA_SURFACE), object + 4u, point, flags);
}

/* 0x488974(header, point, flags): the surface under the bitmap grabbed
 * into it: the header's RLE nibble cleared, its bpp set to 8 for an 8-bit
 * (or 4-bit) surface, the pixels zeroed (CW3220 _memset), the point
 * shifted by the origin when [C+0x208] == 1, then slot 27 once (mode 0
 * without a clip, mode 1 with the clip rect) or per kind-0 region. */
int es2_sim_gfx_bitmap_grab(es2_sim_loop *l, uint32_t header, uint32_t point, uint32_t flags) {
    uint8_t fl = r8(l, header + 5u);
    if (fl & 0xfu) w8(l, header + 5u, fl & 0xf0u);
    uint32_t format = r32(l, VA_VIEW_FORMAT);
    if (format == 8u && r8(l, header + 4u) != 8u) w8(l, header + 4u, 8u);
    if (format == 4u && r8(l, header + 4u) != 4u) w8(l, header + 4u, 8u);
    uint32_t args[3] = {r32(l, header + 0xau), 0, r32(l, header + 6u)};
    if (call(l, VA_MEMSET, args, 3u, NULL)) return 1;
    uint32_t c = r32(l, VA_CONTEXT);
    int32_t dx, dy;
    origin_shift(l, c, &dx, &dy, 1);
    uint32_t top = l->stack_hi, p = frame_push(l, 8u);
    if (!p) return frame_refused(l, 0x00488974u, "bitmap_grab");
    w32(l, p, r32(l, point) + (uint32_t)dx);
    w32(l, p + 4u, r32(l, point + 4u) + (uint32_t)dy);
    int bad = 0;
    c = r32(l, VA_CONTEXT);
    uint32_t mode = r32(l, c + 0x204u);
    if (mode == 0) bad = slot4(l, SLOT_GRAB, header, p, 0, flags);
    else if (mode == 1u) bad = slot4(l, SLOT_GRAB, header, p, c + 0x20cu, flags);
    else if (mode == 2u) {
        uint32_t count = r32(l, c);
        for (uint32_t i = 0; i < count && !bad; ++i) {
            uint32_t e = r32(l, c + 4u + 4u * i);
            if (r32(l, e) == 0) bad = slot4(l, SLOT_GRAB, header, p, e + 4u, flags);
        }
    }
    frame_pop(l, top);
    return bad;
}

/* A bitmap header in guest locals: {h, w, bpp 8, flags, w*h, pixels}. */
static uint32_t header_push(es2_sim_loop *l, int32_t w, int32_t h, uint8_t flags, uint32_t pixels) {
    uint32_t hdr = frame_push(l, 0x10u);
    if (!hdr) return 0;
    w16(l, hdr, (uint16_t)h);
    w16(l, hdr + 2u, (uint16_t)w);
    w8(l, hdr + 4u, 8u);
    w8(l, hdr + 5u, flags);
    w32(l, hdr + 6u, (uint32_t)(w * h));
    w32(l, hdr + 0xau, pixels);
    return hdr;
}

/* 0x488012(record, pixels): the background save: a kind-0 record to 0x487eac
 * (es2_sim_gfx_save_rect), a kind-2 record takes the broken 0x487f6a, other
 * kinds do nothing. */
int es2_sim_gfx_save(es2_sim_loop *l, uint32_t record, uint32_t pixels) {
    uint32_t kind = r32(l, record);
    if (kind == 2u) return refuse(l, 0x00488012u, "save", "a span-list record (0x487f6a reads past its locals)");
    if (kind != 0) return 0;
    return es2_sim_gfx_save_rect(l, record, pixels);
}

/* 0x488045(record): the byte size of a kind-0 record's rect (0x487e94,
 * es2_sim_gfx_rect_area); a kind-2 record takes the dead 0x487f48. */
int es2_sim_gfx_size(es2_sim_loop *l, uint32_t record, uint32_t *eax) {
    uint32_t kind = r32(l, record);
    *eax = 0;
    if (kind == 2u) return refuse(l, 0x00488045u, "size", "a span-list record (0x487f48 is dead code)");
    if (kind != 0) return 0;
    es2_sim_gfx_regs regs = {0, 0, 0};
    es2_sim_gfx_rect_area(l, record + 4u, &regs);
    *eax = regs.eax;
    return 0;
}

/* 0x488143(record, point, pixels): a kind-0 record's rect restored from
 * `pixels` at the point (0x488078: a header {h, w, 8, 0x10, size, pixels}
 * through 0x4887d8 with flags 0); a kind-2 record takes the dead
 * 0x4880bc. */
int es2_sim_gfx_restore(es2_sim_loop *l, uint32_t record, uint32_t point, uint32_t pixels) {
    uint32_t kind = r32(l, record);
    if (kind == 2u) return refuse(l, 0x00488143u, "restore", "a span-list record (0x4880bc is dead code)");
    if (kind != 0) return 0;
    return es2_sim_gfx_restore_rect(l, record, point, pixels);
}
/* 0x488078(record, point, pixels): the kind-0 body (the record's kind is
 * not read; its rect at +4). */
int es2_sim_gfx_restore_rect(es2_sim_loop *l, uint32_t record, uint32_t point, uint32_t pixels) {
    int32_t left = s32(l, record + 4u), t = s32(l, record + 8u), right = s32(l, record + 12u), bottom = s32(l, record + 16u);
    uint32_t top = l->stack_hi, hdr = header_push(l, right - left + 1, bottom - t + 1, 0x10u, pixels);
    if (!hdr) return frame_refused(l, 0x00488078u, "restore_rect");
    int bad = es2_sim_gfx_bitmap_draw(l, hdr, point, 0);
    frame_pop(l, top);
    return bad;
}

/* 0x487e60(record, point): the record's pixels (the source) moved by the point: kind 0 is 0x487bdc
 * (es2_sim_gfx_scroll_rect); kind 2 (0x487d0d): the span list moved the same way into A, through slot 19 (mode 0),
 * clipped into B by the clip rect or per region. The slots get the unshifted point and read the source from destination
 * minus point. */
int es2_sim_gfx_scroll(es2_sim_loop *l, uint32_t record, uint32_t point) {
    uint32_t kind = r32(l, record);
    uint32_t c = r32(l, VA_CONTEXT);
    int32_t dx, dy;
    origin_shift(l, c, &dx, &dy, 0);
    uint32_t a = r32(l, VA_SPANS_A), b = r32(l, VA_SPANS_B), mode = r32(l, c + 0x204u), r;
    if (kind == 0) return es2_sim_gfx_scroll_rect(l, record, point);   /* 0x487bdc */
    if (kind != 2u) return 0;
    /* 0x487d0d: the record's +4 points at the list; its copy in A is moved
     * by the point (the scroll distance) plus the origin shift. */
    uint32_t spans = r32(l, record + 4u), count = r16(l, spans + 2u);
    if (count > ES2_SIM_GFX_SPAN_ROWS) return refuse(l, 0x00487e60u, "scroll", "more rows than the span buffer holds");
    uint32_t mx = r32(l, point) + (uint32_t)dx, my = r32(l, point + 4u) + (uint32_t)dy;
    w16(l, a, (uint16_t)(r16(l, spans) + my));
    w16(l, a + 2u, (uint16_t)count);
    for (uint32_t i = 0; i < count; ++i) {
        w16(l, a + 4u + 4u * i, (uint16_t)(r16(l, spans + 4u + 4u * i) + mx));
        w16(l, a + 6u + 4u * i, (uint16_t)(r16(l, spans + 6u + 4u * i) + mx));
    }
    if (mode == 0) return slot2(l, SLOT_SCROLL_SPANS, a, point);
    if (mode == 1u) {
        if (es2_sim_gfx_rect_clip_spans(l, c + 0x20cu, a, b, &r)) return 1;
        return r ? slot2(l, SLOT_SCROLL_SPANS, r, point) : 0;
    }
    for (uint32_t i = r32(l, c); i-- > 0;) {
        uint32_t e = r32(l, c + 4u + 4u * i), ekind = r32(l, e);
        if (ekind == 0) {
            if (es2_sim_gfx_rect_clip_spans(l, e + 4u, a, b, &r)) return 1;
        } else if (ekind == 2u) {
            if (es2_sim_gfx_spans_clip_spans(l, a, r32(l, e + 4u), b, &r)) return 1;
        } else {
            continue;
        }
        if (r && slot2(l, SLOT_SCROLL_SPANS, r, point)) return 1;
    }
    return 0;
}

/* ======================================================================
 * The stretched and the rotated blit (listing 0x4817c8..0x4818f8,
 * 0x481c3b..0x481c78, 0x488aa8..0x488df6). */

/* 0x488aa8(header, point, flags, size): slot 29 with the size's two
 * dwords as w and h, the point shifted by the origin when [C+0x208] ==
 * 1; mode 0 without a clip, mode 1 and mode 2 (with regions) with the
 * clip rect C+0x20c. */
int es2_sim_gfx_stretch(es2_sim_loop *l, uint32_t header, uint32_t point, uint32_t flags, uint32_t size) {
    uint32_t c = r32(l, VA_CONTEXT);
    int32_t dx, dy;
    origin_shift(l, c, &dx, &dy, 1);
    uint32_t top = l->stack_hi, p = frame_push(l, 8u);
    if (!p) return frame_refused(l, 0x00488aa8u, "stretch");
    w32(l, p, r32(l, point) + (uint32_t)dx);
    w32(l, p + 4u, r32(l, point + 4u) + (uint32_t)dy);
    c = r32(l, VA_CONTEXT);
    uint32_t mode = r32(l, c + 0x204u), args[6] = {header, r32(l, size), r32(l, size + 4u), p, 0, flags};
    int bad = 0;
    if (mode == 0) {
        bad = es2_sim_gfx_slot(l, SLOT_STRETCH, args, 6u, NULL);
    } else if (mode == 1u || (mode == 2u && (int32_t)r32(l, c) > 0)) {
        args[4] = c + 0x20cu;
        bad = es2_sim_gfx_slot(l, SLOT_STRETCH, args, 6u, NULL);
    }
    frame_pop(l, top);
    return bad;
}

/* 0x481c3b(surface, header, point, flags, size): 0x488aa8 (the surface is
 * not used). */
int es2_sim_gfx_stretch_on(es2_sim_loop *l, uint32_t surface, uint32_t header, uint32_t point, uint32_t flags, uint32_t size) {
    (void)surface;
    return es2_sim_gfx_stretch(l, header, point, flags, size);
}

/* 0x4817c8(object, point, flags, size): the GLBitmap stretched: its header
 * at +4 through 0x481c3b on the current surface; a compressed one (word
 * +0x12) takes the dead decoder 0x481910. */
int es2_sim_gfx_stretch_blit(es2_sim_loop *l, uint32_t object, uint32_t point, uint32_t flags, uint32_t size) {
    if (r16(l, object + 0x12u)) return refuse(l, 0x004817c8u, "stretch_blit", "a compressed GLBitmap (the decoder 0x481910 is dead code)");
    return es2_sim_gfx_stretch_on(l, r32(l, VA_SURFACE), object + 4u, point, flags, size);
}

/* 0x488b98(header, point, flags, size, angle): the bitmap as a textured
 * quad. The source corners (0,0), (w-1,0), (w-1,h-1), (0,h-1) of the
 * header's w x h (flags 2 swap corners 0/1 and 2/3, then flags 1 swap 0/3
 * and 1/2); the destination corners (0,0), (sx,0), (sx,sy), (0,sy) of the
 * size's two dwords, each rotated by the word angle (0x488dc7: sincos_pair
 * 0x47eb30 into a 2 x 2 matrix; 0x488ddd: rotate_2d 0x47ffe8) and moved by the point (the
 * vector add 0x48d9cf), then the polygon fill 0x483eb8(4, corners)
 * with a temporary fill {5, 4, source corners, header} as [C+0x228]
 * (restored after). The angle is pushed as the low word of a dword whose
 * high word is the matrix address's (0x488dce); 0x47eb30 reads the word.
 * The thunks are rows of their own (gfx_blit.h).
 * The empty constructors 0x49545b, 0x49514f and 0x49667c's element loop
 * have no effect and are not called. */
#define VA_SINCOS 0x0047eb30u
#define VA_ROTATE 0x0047ffe8u
#define VA_POLYGON 0x00483eb8u
int es2_sim_gfx_rotate(es2_sim_loop *l, uint32_t header, uint32_t point, uint32_t flags, uint32_t size, uint32_t angle) {
    uint32_t top = l->stack_hi, f = frame_push(l, 0x90u);
    if (!f) return frame_refused(l, 0x00488b98u, "rotate");
    uint32_t src = f, dst = f + 0x20u, m = f + 0x40u, out = f + 0x50u, fill = f + 0x70u;
    int32_t w = s16(l, header + 2u) - 1, h = s16(l, header) - 1;
    int32_t s[8] = {0, 0, w, 0, w, h, 0, h};
    if (flags & 2u) {
        int32_t t0 = s[0], t1 = s[1]; s[0] = s[2]; s[1] = s[3]; s[2] = t0; s[3] = t1;
        t0 = s[6]; t1 = s[7]; s[6] = s[4]; s[7] = s[5]; s[4] = t0; s[5] = t1;
    }
    if (flags & 1u) {
        int32_t t0 = s[0], t1 = s[1]; s[0] = s[6]; s[1] = s[7]; s[6] = t0; s[7] = t1;
        t0 = s[2]; t1 = s[3]; s[2] = s[4]; s[3] = s[5]; s[4] = t0; s[5] = t1;
    }
    for (unsigned i = 0; i < 8u; ++i) w32(l, src + 4u * i, (uint32_t)s[i]);
    uint32_t sx = r32(l, size), sy = r32(l, size + 4u);
    uint32_t d[8] = {0, 0, sx, 0, sx, sy, 0, sy};
    for (unsigned i = 0; i < 8u; ++i) w32(l, dst + 4u * i, d[i]);
    es2_sim_gfx_regs regs = {0, 0, 0};
    int bad = es2_sim_gfx_rotation_matrix(l, m, angle, &regs);            /* 0x488dc7 */
    for (unsigned i = 0; i < 4u && !bad; ++i)
        bad = es2_sim_gfx_rotate_point(l, dst + 8u * i, m, out + 8u * i, &regs);   /* 0x488ddd */
    if (!bad) {
        for (unsigned i = 0; i < 4u; ++i) {
            w32(l, out + 8u * i, r32(l, out + 8u * i) + r32(l, point));
            w32(l, out + 8u * i + 4u, r32(l, out + 8u * i + 4u) + r32(l, point + 4u));
        }
        uint32_t c = r32(l, VA_CONTEXT), saved = r32(l, c + 0x228u);
        w32(l, c + 0x228u, fill);
        w32(l, fill, 5u); w32(l, fill + 4u, 4u); w32(l, fill + 8u, src); w32(l, fill + 12u, header);
        uint32_t a4[2] = {4u, out};
        bad = call(l, VA_POLYGON, a4, 2u, NULL);
        w32(l, r32(l, VA_CONTEXT) + 0x228u, saved);
    }
    frame_pop(l, top);
    return bad;
}

/* 0x481c58(surface, header, point, flags, size, angle): 0x488b98 with the
 * angle's low word (the surface is not used). */
int es2_sim_gfx_rotate_on(es2_sim_loop *l, uint32_t surface, uint32_t header, uint32_t point, uint32_t flags, uint32_t size, uint32_t angle) {
    (void)surface;
    return es2_sim_gfx_rotate(l, header, point, flags, size, angle & 0xffffu);
}

/* 0x48185c(object, point, flags, size, angle): the GLBitmap rotated: its
 * header at +4 through 0x481c58 on the current surface; a compressed one
 * (word +0x12) takes the dead decoder 0x481910. */
int es2_sim_gfx_rotated_blit(es2_sim_loop *l, uint32_t object, uint32_t point, uint32_t flags, uint32_t size, uint32_t angle) {
    if (r16(l, object + 0x12u)) return refuse(l, 0x0048185cu, "rotated_blit", "a compressed GLBitmap (the decoder 0x481910 is dead code)");
    return es2_sim_gfx_rotate_on(l, r32(l, VA_SURFACE), object + 4u, point, flags, size, angle);
}

/* ======================================================================
 * Text (listing 0x482498..0x4827bb). The font F: word +8 first char,
 * +0xa height, +0xc fixed width or -1, byte +0x12 bpp, word +0x16
 * baseline, dword +0x22 pixel base, +0x26 glyph offsets, +0x2a widths. */

/* 0x482498(font, ch): the glyph width of the low byte of ch (the width
 * table entry, or the fixed width); the sprintf call of the original has
 * no lasting effect. */
int es2_sim_gfx_char_width(es2_sim_loop *l, uint32_t font, uint32_t ch, uint32_t *eax) {
    ch &= 0xffu;
    int32_t fixed = s16(l, font + 0xcu);
    if (fixed == -1) *eax = r8(l, r32(l, font + 0x2au) + ch - (uint32_t)s16(l, font + 8u));
    else *eax = (uint32_t)fixed;
    return 0;
}
int es2_sim_gfx_string_width(es2_sim_loop *l, uint32_t font, uint32_t text, uint32_t *eax) {
    uint32_t sum = 0, w;
    for (uint32_t i = 0;; ++i) {
        uint8_t ch = r8(l, text + i);
        if (!ch) break;
        es2_sim_gfx_char_width(l, font, ch, &w);
        sum += w;
    }
    *eax = sum;
    return 0;
}
int es2_sim_gfx_font_height(es2_sim_loop *l, uint32_t font, uint32_t *eax) {
    *eax = (uint32_t)s16(l, font + 0xau);
    return 0;
}

/* 0x482534(font, point, ch): the glyph drawn transparently as a bitmap
 * {height, width(ch), bpp, residue & 0xe0, w*h, [[F+0x26] + 4(index)] +
 * [F+0x22]} at (x, y - baseline) through 0x4827bb; the index is the low
 * word of ch sign-extended (0x48257d movsx ecx, si) minus the first char,
 * the width takes the low byte (0x4824aa). The string path hands over
 * each byte zero-extended (0x4826b2). With the GC's underline byte
 * GC+0x230 set (0x4825e8), the line thunk 0x482d44(surface, &p0, &p1) runs
 * from p0 = (x, y) to p1 = (x + width, y), y without the baseline
 * (0x4825f1..0x482622). Every GC constructor clears the byte (0x480d21,
 * 0x430f2e) and no instruction sets it, so play never takes this path. */
int es2_sim_gfx_char(es2_sim_loop *l, uint32_t font, uint32_t point, uint32_t ch) {
    uint32_t w;
    es2_sim_gfx_char_width(l, font, ch & 0xffu, &w);
    int32_t h = s16(l, font + 0xau);
    int32_t index = (int32_t)(int16_t)(ch & 0xffffu) - s16(l, font + 8u);
    uint32_t pixels = r32(l, r32(l, font + 0x26u) + 4u * (uint32_t)index) + r32(l, font + 0x22u);
    uint32_t top = l->stack_hi, hdr = header_push(l, (int32_t)w, h, 0, pixels), pt = frame_push(l, 8u);
    if (!hdr || !pt) { frame_pop(l, top); return frame_refused(l, 0x00482534u, "char"); }
    w8(l, hdr + 4u, r8(l, font + 0x12u));
    w32(l, pt, r32(l, point));
    w32(l, pt + 4u, r32(l, point + 4u) - (uint32_t)s16(l, font + 0x16u));
    int bad = es2_sim_gfx_bitmap_draw_on(l, r32(l, VA_SURFACE), hdr, pt, 0);
    frame_pop(l, top);
    if (bad) return bad;
    uint32_t gc = r32(l, VA_GC);
    if (!gc || !r8(l, gc + 0x230u)) return 0;
    uint32_t ends = frame_push(l, 16u);
    if (!ends) return frame_refused(l, 0x00482534u, "char");
    w32(l, ends, r32(l, point));
    w32(l, ends + 4u, r32(l, point + 4u));
    w32(l, ends + 8u, r32(l, point) + w);
    w32(l, ends + 12u, r32(l, point + 4u));
    const uint32_t a[3] = {r32(l, VA_SURFACE), ends, ends + 8u};
    bad = call(l, 0x00482d44u, a, 3, NULL);
    frame_pop(l, top);
    return bad;
}

/* 0x482634(font, point, text): the string glyph by glyph from the
 * point, x advanced by each width; the GC's alignment dword GC+0x231
 * (1 right, 2 centred) shifts the start by the GC's width GC+0x235. */
int es2_sim_gfx_string(es2_sim_loop *l, uint32_t font, uint32_t point, uint32_t text) {
    uint32_t gc = r32(l, VA_GC);
    if (!gc) return null_refuse(l, 0x00482634u, "string", "no GC object (the original dereferences NULL)");
    uint32_t top = l->stack_hi, pt = frame_push(l, 8u);
    if (!pt) return frame_refused(l, 0x00482634u, "string");
    uint32_t x = r32(l, point), w;
    uint32_t align = r32(l, gc + 0x231u);
    if (align == 1u) { es2_sim_gfx_string_width(l, font, text, &w); x += r32(l, gc + 0x235u) - w; }
    else if (align == 2u) { es2_sim_gfx_string_width(l, font, text, &w); x += (uint32_t)((int32_t)(r32(l, gc + 0x235u) - w) >> 1); }
    w32(l, pt + 4u, r32(l, point + 4u));
    int bad = 0;
    for (uint32_t i = 0; !bad; ++i) {
        uint8_t ch = r8(l, text + i);
        if (!ch) break;
        w32(l, pt, x);
        bad = es2_sim_gfx_char(l, font, pt, ch);
        es2_sim_gfx_char_width(l, font, ch, &w);
        x += w;
    }
    frame_pop(l, top);
    return bad;
}

/* ======================================================================
 * The scan converter (listing 0x4930a0..0x495132: 0x4930c1, 0x493141,
 * 0x493192 with the edge walkers to 0x49495f). `out`: dwords {ymin,
 * rows}, then per row {left, right} from out+8. */

#define SCAN_MAX 54u    /* the chain array of 0x493192 */
typedef struct vertex { int32_t x, y; } vertex;

static uint32_t row_slot(uint32_t out, int32_t ymin, int32_t y, int field) {
    return out + 8u * (uint32_t)(y - ymin + 1) + (uint32_t)field;
}

/* One edge of the polygon into the rows' field (0 left, 4 right), the
 * listing's five walkers: vertical, horizontal, y-major, x-major and
 * diagonal Bresenham. Rows past the row array are refused. */
static int edge(es2_sim_loop *l, uint32_t out, int32_t ymin, vertex p0, vertex p1, int field) {
    int32_t x0 = p0.x, y0 = p0.y, x1 = p1.x, y1 = p1.y;
    int32_t dx = x0 > x1 ? x0 - x1 : x1 - x0, dy = y0 > y1 ? y0 - y1 : y1 - y0;
    if (dx == 0) {
        if (y0 > y1) { int32_t t = y0; y0 = y1; y1 = t; }
        for (int32_t y = y0; y <= y1; ++y) w32(l, row_slot(out, ymin, y, field), (uint32_t)x0);
        return 0;
    }
    if (dy == 0) {
        /* 0x493335..0x49335f: only the chain's own field, the larger x
         * for the right ends, the smaller for the left ends. */
        if (x0 >= x1) { int32_t t = x0; x0 = x1; x1 = t; }
        w32(l, row_slot(out, ymin, y0, field), (uint32_t)(field ? x1 : x0));
        return 0;
    }
    if (dx < dy) {
        if (x0 >= x1) { int32_t t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }
        int32_t step = y0 < y1 ? 1 : -1, e = 2 * dx - dy, x = x0, y = y0;
        for (int32_t i = 0; i <= dy; ++i, y += step) {
            w32(l, row_slot(out, ymin, y, field), (uint32_t)x);
            if (e >= 0) { ++x; e += 2 * (dx - dy); } else e += 2 * dx;
        }
        return 0;
    }
    if (dx > dy) {
        int32_t xstep;
        if (field == 4) { if (x0 <= x1) { int32_t t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; } xstep = -1; }
        else { if (x0 >= x1) { int32_t t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; } xstep = 1; }
        int32_t step = y1 >= y0 ? 1 : -1, e = 2 * dy - dx, x = x0, y = y0;
        w32(l, row_slot(out, ymin, y, field), (uint32_t)x);
        x += xstep;
        if (e < 0) do { x += xstep; e += 2 * dy; } while (e < 0);
        for (int32_t i = 0; i < dy; ++i) {
            y += step;
            w32(l, row_slot(out, ymin, y, field), (uint32_t)x);
            x += xstep;
            if (i + 1 == dy) break;
            e += 2 * (dy - dx);
            if (e < 0) do { x += xstep; e += 2 * dy; } while (e < 0);
        }
        return 0;
    }
    if (y0 >= y1) { int32_t t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }
    int32_t xstep = x0 < x1 ? 1 : -1, x = x0;
    for (int32_t y = y0; y <= y1; ++y, x += xstep) w32(l, row_slot(out, ymin, y, field), (uint32_t)x);
    return 0;
}

/* 0x493192(count, points, out): the polygon's rows: the topmost vertex
 * (the last of the lowest y with the greatest x), the bottommost (the
 * last of the greatest y with the smallest x); a flat polygon writes one
 * row {ymin, 1, the bottommost's x (the smaller), the topmost's x (the
 * larger)} and returns the topmost's x. Else the chain from
 * the top forward to the bottom gives the right edges, the chain from the
 * bottom forward to the top the left edges; EAX = the row count. */
int es2_sim_gfx_scan_convert(es2_sim_loop *l, uint32_t count, uint32_t points, uint32_t out, uint32_t *eax) {
    if (count == 0) return 0;
    if (count > 1024u) return refuse(l, 0x00493192u, "scan_convert", "an implausible vertex count");
    vertex v[1024];
    for (uint32_t i = 0; i < count; ++i) { v[i].x = s32(l, points + 8u * i); v[i].y = s32(l, points + 8u * i + 4u); }
    uint32_t lo = 0, hi = 0;
    for (uint32_t i = count - 1u; i >= 1u; --i) {
        if (v[i].y < v[lo].y || (v[i].y == v[lo].y && v[i].x >= v[lo].x)) lo = i;
        if (v[i].y > v[hi].y || (v[i].y == v[hi].y && v[i].x <= v[hi].x)) hi = i;
    }
    int32_t ymin = v[lo].y, ymax = v[hi].y;
    if (ymax == ymin) {
        w32(l, out, (uint32_t)ymin);
        w32(l, out + 4u, 1u);
        w32(l, out + 8u, (uint32_t)v[hi].x);
        w32(l, out + 12u, (uint32_t)v[lo].x);
        *eax = (uint32_t)v[lo].x;
        return 0;
    }
    if (ymax - ymin + 1 > 400) return refuse(l, 0x00493192u, "scan_convert", "more rows than the row array holds");
    vertex chain[SCAN_MAX];
    uint32_t na = 0, nb = 0, i = lo;
    for (;;) {
        if (na + nb >= SCAN_MAX) return refuse(l, 0x00493192u, "scan_convert", "more chain vertices than the local array holds");
        chain[na++] = v[i];
        if (v[i].y >= ymax) break;
        i = (i + 1u) % count;
    }
    i = hi;
    for (;;) {
        if (na + nb >= SCAN_MAX) return refuse(l, 0x00493192u, "scan_convert", "more chain vertices than the local array holds");
        chain[na + nb++] = v[i];
        if (v[i].y <= ymin) break;
        i = (i + 1u) % count;
    }
    for (uint32_t k = 0; k + 1u < na; ++k) edge(l, out, ymin, chain[k], chain[k + 1u], 4);
    for (uint32_t k = na; k + 1u < na + nb; ++k) edge(l, out, ymin, chain[k], chain[k + 1u], 0);
    w32(l, out, (uint32_t)ymin);
    w32(l, out + 4u, (uint32_t)(ymax - ymin + 1));
    *eax = (uint32_t)(ymax - ymin + 1);
    return 0;
}

/* 0x493141(count, points, out): the polygon reversed into a 50-vertex
 * local, then 0x493192 (up to two vertices as they are). */
int es2_sim_gfx_scan_reversed(es2_sim_loop *l, uint32_t count, uint32_t points, uint32_t out, uint32_t *eax) {
    if (count == 0) return 0;
    if (count <= 2u) return es2_sim_gfx_scan_convert(l, count, points, out, eax);
    if (count > 50u) return refuse(l, 0x00493141u, "scan_reversed", "more than 50 vertices (the original's local array overflows)");
    uint32_t top = l->stack_hi, local = frame_push(l, 8u * count);
    if (!local) return frame_refused(l, 0x00493141u, "scan_reversed");
    for (uint32_t i = 0; i < count; ++i) {
        w32(l, local + 8u * i, r32(l, points + 8u * (count - 1u - i)));
        w32(l, local + 8u * i + 4u, r32(l, points + 8u * (count - 1u - i) + 4u));
    }
    int bad = es2_sim_gfx_scan_convert(l, count, local, out, eax);
    frame_pop(l, top);
    return bad;
}

/* 0x4930c1(count, points, out): the polygon's orientation from the
 * 64-bit twice-signed area (x[n-1]*y[0] - y[n-1]*x[0] + sum of x[i]*y[i+1]
 * - y[i]*x[i+1]): positive through 0x493192, else through 0x493141. */
int es2_sim_gfx_scan(es2_sim_loop *l, uint32_t count, uint32_t points, uint32_t out, uint32_t *eax) {
    if (count == 0) return 0;
    if (count <= 2u) return es2_sim_gfx_scan_convert(l, count, points, out, eax);
    int64_t acc = 0;
    int64_t xl = s32(l, points + 8u * (count - 1u)), yl = s32(l, points + 8u * (count - 1u) + 4u);
    acc += xl * (int64_t)s32(l, points + 4u) - yl * (int64_t)s32(l, points);
    for (uint32_t i = 0; i + 1u < count; ++i) {
        int64_t x = s32(l, points + 8u * i), y = s32(l, points + 8u * i + 4u);
        int64_t x1 = s32(l, points + 8u * (i + 1u)), y1 = s32(l, points + 8u * (i + 1u) + 4u);
        acc += x * y1 - y * x1;
    }
    int32_t hi = (int32_t)(acc >> 32), lo = (int32_t)(uint32_t)acc;
    if (hi > 0 || (hi == 0 && lo > 0)) return es2_sim_gfx_scan_convert(l, count, points, out, eax);
    return es2_sim_gfx_scan_reversed(l, count, points, out, eax);
}

/* ======================================================================
 * The service rows (sim/dispatch/services/gfx_primitives.h). */

static int serve(es2_sim_service_call *sc, es2_sim_loop **l) {
    *l = es2_sim_loop_bound();
    if (!*l) { sc->refused = 1; sc->reason = "no main loop bound"; return 1; }
    sc->eax = 0;
    return 0;
}
static void failed(es2_sim_service_call *sc) { sc->refused = 1; sc->reason = "a call inside the service was refused"; }
#define GFX_SERVICE(name, body) \
    void es2_sim_service_gfx_##name(es2_sim_service_call *sc, void *user) { \
        es2_sim_loop *l; (void)user; \
        if (serve(sc, &l)) return; \
        uint32_t eax = 0; (void)eax; \
        if (body) failed(sc); \
    }
GFX_SERVICE(rect_clip, (es2_sim_gfx_rect_clip(l, sc->slots[0], sc->slots[1], sc->slots[2], &eax), sc->eax = eax, 0))
GFX_SERVICE(rect_to_spans, (es2_sim_gfx_rect_to_spans(l, sc->slots[0], sc->slots[1], &eax) ? 1 : (sc->eax = eax, 0)))
GFX_SERVICE(spans_clip_spans, (es2_sim_gfx_spans_clip_spans(l, sc->slots[0], sc->slots[1], sc->slots[2], &eax), sc->eax = eax, 0))
GFX_SERVICE(rect_clip_spans, (es2_sim_gfx_rect_clip_spans(l, sc->slots[0], sc->slots[1], sc->slots[2], &eax), sc->eax = eax, 0))
GFX_SERVICE(rect_fill, es2_sim_gfx_rect_fill(l, sc->slots[0]))
GFX_SERVICE(span_fill, es2_sim_gfx_span_fill(l, sc->slots[0], sc->slots[1]))
GFX_SERVICE(point, es2_sim_gfx_point(l, sc->slots[0]))
GFX_SERVICE(ellipse, es2_sim_gfx_ellipse(l, sc->slots[0]))
GFX_SERVICE(abs, (sc->eax = es2_sim_gfx_abs(sc->slots[0]), 0))
GFX_SERVICE(bitmap_blit, es2_sim_gfx_bitmap_blit(l, sc->slots[0], sc->slots[1], sc->slots[2]))
GFX_SERVICE(bitmap_draw_on, es2_sim_gfx_bitmap_draw_on(l, sc->slots[0], sc->slots[1], sc->slots[2], sc->slots[3]))
GFX_SERVICE(bitmap_draw, es2_sim_gfx_bitmap_draw(l, sc->slots[0], sc->slots[1], sc->slots[2]))
GFX_SERVICE(bitmap_grab, es2_sim_gfx_bitmap_grab(l, sc->slots[0], sc->slots[1], sc->slots[2]))
GFX_SERVICE(save, es2_sim_gfx_save(l, sc->slots[0], sc->slots[1]))
GFX_SERVICE(size, (es2_sim_gfx_size(l, sc->slots[0], &eax) ? 1 : (sc->eax = eax, 0)))
GFX_SERVICE(restore, es2_sim_gfx_restore(l, sc->slots[0], sc->slots[1], sc->slots[2]))
GFX_SERVICE(restore_rect, es2_sim_gfx_restore_rect(l, sc->slots[0], sc->slots[1], sc->slots[2]))
GFX_SERVICE(scroll, es2_sim_gfx_scroll(l, sc->slots[0], sc->slots[1]))
GFX_SERVICE(char_width, (es2_sim_gfx_char_width(l, sc->slots[0], sc->slots[1], &eax), sc->eax = eax, 0))
GFX_SERVICE(string_width, (es2_sim_gfx_string_width(l, sc->slots[0], sc->slots[1], &eax), sc->eax = eax, 0))
GFX_SERVICE(font_height, (es2_sim_gfx_font_height(l, sc->slots[0], &eax), sc->eax = eax, 0))
GFX_SERVICE(char, es2_sim_gfx_char(l, sc->slots[0], sc->slots[1], sc->slots[2]))
GFX_SERVICE(string, es2_sim_gfx_string(l, sc->slots[0], sc->slots[1], sc->slots[2]))
GFX_SERVICE(scan, (es2_sim_gfx_scan(l, sc->slots[0], sc->slots[1], sc->slots[2], &eax) ? 1 : (sc->eax = eax, 0)))
GFX_SERVICE(scan_reversed, (es2_sim_gfx_scan_reversed(l, sc->slots[0], sc->slots[1], sc->slots[2], &eax) ? 1 : (sc->eax = eax, 0)))
GFX_SERVICE(scan_convert, (es2_sim_gfx_scan_convert(l, sc->slots[0], sc->slots[1], sc->slots[2], &eax) ? 1 : (sc->eax = eax, 0)))
GFX_SERVICE(current_gc, (sc->eax = r32(l, VA_GC), 0))
/* The slot bodies as rows (a caller that reads the table and calls the
 * BMP_8 body directly, as 0x4887d8 does with 0x4899ac). */
#define GFX_SLOT_SERVICE(name, index, n) \
    void es2_sim_service_gfx_##name(es2_sim_service_call *sc, void *user) { \
        es2_sim_loop *l; (void)user; \
        if (serve(sc, &l)) return; \
        uint32_t eax = 0, args[6] = {0, 0, 0, 0, 0, 0}; \
        for (unsigned i = 0; i < (n); ++i) args[i] = sc->slots[i]; \
        if (es2_sim_gfx_slot(l, (index), args, (n), &eax)) failed(sc); else sc->eax = eax; \
    }
GFX_SLOT_SERVICE(slot_span_solid, SLOT_SPAN_SOLID, 2u)
GFX_SLOT_SERVICE(slot_span_table, SLOT_SPAN_TABLE, 2u)
GFX_SLOT_SERVICE(slot_rect_solid, SLOT_RECT_SOLID, 2u)
GFX_SLOT_SERVICE(slot_rect_table, SLOT_RECT_TABLE, 2u)
GFX_SLOT_SERVICE(slot_pixel_get, SLOT_PIXEL_GET, 1u)
GFX_SLOT_SERVICE(slot_pixel_put, SLOT_PIXEL_PUT, 2u)
GFX_SLOT_SERVICE(slot_pixel_table, SLOT_PIXEL_TABLE, 2u)
GFX_SLOT_SERVICE(slot_scroll_spans, SLOT_SCROLL_SPANS, 2u)
GFX_SLOT_SERVICE(slot_scroll_rect, SLOT_SCROLL_RECT, 2u)
GFX_SLOT_SERVICE(slot_blit, SLOT_BLIT, 4u)
GFX_SLOT_SERVICE(slot_grab, SLOT_GRAB, 4u)
GFX_SLOT_SERVICE(slot_blit_region, SLOT_BLIT_REGION, 4u)
GFX_SLOT_SERVICE(slot_stretch, SLOT_STRETCH, 6u)
GFX_SERVICE(stretch, es2_sim_gfx_stretch(l, sc->slots[0], sc->slots[1], sc->slots[2], sc->slots[3]))
GFX_SERVICE(stretch_on, es2_sim_gfx_stretch_on(l, sc->slots[0], sc->slots[1], sc->slots[2], sc->slots[3], sc->slots[4]))
GFX_SERVICE(stretch_blit, es2_sim_gfx_stretch_blit(l, sc->slots[0], sc->slots[1], sc->slots[2], sc->slots[3]))
GFX_SERVICE(rotate, es2_sim_gfx_rotate(l, sc->slots[0], sc->slots[1], sc->slots[2], sc->slots[3], sc->slots[4] & 0xffffu))
GFX_SERVICE(rotate_on, es2_sim_gfx_rotate_on(l, sc->slots[0], sc->slots[1], sc->slots[2], sc->slots[3], sc->slots[4], sc->slots[5]))
GFX_SERVICE(rotated_blit, es2_sim_gfx_rotated_blit(l, sc->slots[0], sc->slots[1], sc->slots[2], sc->slots[3], sc->slots[4]))

/* ======================================================================
 * The graphics library's device objects, graphics contexts, surfaces and
 * bitmaps (listing 0x480738..0x480ffc and 0x481028..0x481f3d; rasterizer.md,
 * "Graphics objects and surfaces"; rows in
 * sim/dispatch/services/gfx_objects.h). The surface pieces 0x480f58, 0x481028,
 * 0x4811e8 and 0x481224 are frame_buffer.c's; these rows call
 * them. Other packages' functions are reached through the dispatch. */
#define RD09_COUNTER 0x006cbee4u        /* -> the construction counter */
#define RD09_DEVICES 0x006c5eb4u        /* the device table (0x48d98f(k) = [0x6c5eb4 + 4k]) */
#define RD09_HANDLERS 0x006c5ed8u       /* 10 entries of {used, fn, mask} */
#define RD09_DELETE 0x00477548u
#define RD09_PAIR_CTOR 0x0048de2fu
#define RD09_EMPTY_CTOR 0x0049545bu
#define RD09_BASE_DTOR 0x004838fdu
#define RD09_STREAM_BASE_DTOR 0x0047a8a4u
#define RD09_VECTOR_SET 0x0048d9a7u
#define RD09_BITMAP_BLIT 0x004816a8u
#define RD09_PUT_WORD 0x00492447u
#define RD09_PUT_BYTE 0x0049085du
#define RD09_PUT_HEADER 0x00482f0au
#define RD09_PUT_COLOURS 0x00482bf0u
#define RD09_TABLES 0x004a1690u         /* 0x47a0b8's count; (table, count) pairs at 0x6c5834 */
#define RD09_FORMAT_NAMES 0x004a2e88u
#define RD09_FORMAT_CODES 0x004a2e94u
#define RD09_SLOT_SET 32u               /* [0x4a582c]; BMP_8's 0x4898f6 is an empty body */

static void rd09_count(es2_sim_loop *l, int32_t n) {
    uint32_t p = r32(l, RD09_COUNTER);
    w32(l, p, r32(l, p) + (uint32_t)n);
}
/* `delete` when flags bit 0 is set (its EAX), else `eax` unchanged. */
static int rd09_delete(es2_sim_loop *l, uint32_t obj, uint32_t flags, uint32_t *eax) {
    if (!(flags & 1u)) return 0;
    uint32_t a[1] = {obj};
    return call(l, RD09_DELETE, a, 1, eax);
}

/* 0x480738(obj, out): the device's dwords +0xe, +0x12, +0x16, +0x1a into
 * out[0..3] (each read just before its write). EAX out + 8. */
static uint32_t rd09_device_rect_get(es2_sim_loop *l, uint32_t obj, uint32_t out) {
    w32(l, out, r32(l, obj + 0xeu));
    w32(l, out + 4u, r32(l, obj + 0x12u));
    w32(l, out + 8u, r32(l, obj + 0x16u));
    w32(l, out + 0xcu, r32(l, obj + 0x1au));
    return out + 8u;
}
/* 0x480764(obj, in): the words in+0x10..+0x14 into +0x1e..+0x22, then the
 * dwords in+0..+0xc into +0xe..+0x1a. EAX obj + 0xe. */
static uint32_t rd09_device_rect_set(es2_sim_loop *l, uint32_t obj, uint32_t in) {
    w16(l, obj + 0x1eu, r16(l, in + 0x10u));
    w16(l, obj + 0x20u, r16(l, in + 0x12u));
    w16(l, obj + 0x22u, r16(l, in + 0x14u));
    w32(l, obj + 0xeu, r32(l, in));
    w32(l, obj + 0x12u, r32(l, in + 4u));
    w32(l, obj + 0x16u, r32(l, in + 8u));
    w32(l, obj + 0x1au, r32(l, in + 0xcu));
    return obj + 0xeu;
}
/* 0x480848(_, mask, fn): the first free handler entry takes {1, fn, mask};
 * EAX its index, or AX 0xffff over the walk's end pointer. */
static uint32_t rd09_handler_add(es2_sim_loop *l, uint32_t mask, uint32_t fn) {
    for (uint32_t i = 0; i < 10u; ++i) {
        uint32_t e = RD09_HANDLERS + 12u * i;
        if (r32(l, e)) continue;
        w32(l, e + 4u, fn);
        w32(l, e + 8u, mask);
        w32(l, e, 1);
        return i;
    }
    return ((RD09_HANDLERS + 120u) & 0xffff0000u) | 0xffffu;
}
/* 0x480b38(obj, flags): the device destructor. */
static int rd09_device_destroy(es2_sim_loop *l, uint32_t obj, uint32_t flags, uint32_t *eax) {
    *eax = r32(l, RD09_COUNTER);
    rd09_count(l, -1);
    if (!obj) return 0;
    *eax = r32(l, RD09_DEVICES);                  /* 0x480bba */
    if (*eax) {                                   /* 0x480bc8 (empty), 0x480ba9(obj, 0) */
        w32(l, RD09_DEVICES, 0);
        *eax = 0;
    }
    return rd09_delete(l, obj, flags, eax);
}

/* 0x480c34(gc): the graphics context's constructor: the vtable stores, the
 * element constructors, the current surface's first clip record copied
 * (0x81 dwords, then the tail fields in the listing's order). EAX gc. */
static int rd09_gc_construct(es2_sim_loop *l, uint32_t gc, uint32_t *eax) {
    w32(l, gc, 0x004a69f0u);
    w32(l, gc, 0x004a3368u);
    rd09_count(l, 1);
    uint32_t a[1] = {gc + 4u + 0x20cu};
    if (call(l, RD09_PAIR_CTOR, a, 1, NULL)) return 1;
    a[0] = gc + 4u + 0x21cu;
    if (call(l, RD09_EMPTY_CTOR, a, 1, NULL)) return 1;
    w32(l, gc, 0x004a2e10u);
    uint32_t s = r32(l, VA_SURFACE);
    if (!s) return null_refuse(l, 0x00480c7eu, "graphics context", "no current surface (the original reads through NULL)");
    uint32_t p = r32(l, s + 0x40u);
    if (!p) return null_refuse(l, 0x00480c91u, "graphics context", "the surface has no clip record (the original reads through NULL)");
    uint32_t d = gc + 4u;
    for (uint32_t i = 0; i < 0x81u; ++i) w32(l, d + 4u * i, r32(l, p + 4u * i));
    static const uint16_t tail[] = {0x204u, 0x208u, 0x224u, 0x228u, 0x20cu, 0x210u, 0x214u, 0x218u, 0x21cu, 0x220u};
    for (unsigned i = 0; i < sizeof(tail) / sizeof(tail[0]); ++i) w32(l, d + tail[i], r32(l, p + tail[i]));
    w8(l, gc + 0x230u, 0);
    w32(l, gc + 0x231u, 0);
    w32(l, gc + 0x235u, 0);
    rd09_count(l, 1);
    *eax = gc;
    return 0;
}
/* 0x480dcf(gc, flags): its destructor (0x4838fd(gc, 0), then delete). */
static int rd09_gc_destroy(es2_sim_loop *l, uint32_t gc, uint32_t flags, uint32_t *eax) {
    if (!gc) return 0;
    rd09_count(l, -1);
    uint32_t a[2] = {gc, 0};
    if (call(l, RD09_BASE_DTOR, a, 2, eax)) return 1;
    return rd09_delete(l, gc, flags, eax);
}
/* 0x480ecf(s, clip): +0x44 = clip; for the current surface also
 * [0x4a3614] (0x489558). EAX clip, or the current surface. */
static uint32_t rd09_surface_set_clip(es2_sim_loop *l, uint32_t s, uint32_t clip) {
    w32(l, s + 0x44u, clip);
    uint32_t current = r32(l, VA_SURFACE);
    if (s != current) return current;
    w32(l, VA_CONTEXT, clip);
    return clip;
}
/* 0x480ef4: 0x47a0b8(0x4a2eb8, [0x4a2f24]): a nonzero count is registered
 * as the next (table, count) pair. EAX 0 or the pair's index. */
static uint32_t rd09_register(es2_sim_loop *l) {
    uint32_t count = r32(l, 0x004a2f24u);
    if (!count) return 0;
    uint32_t i = r32(l, RD09_TABLES);
    w32(l, 0x006c5838u + 8u * i, count);
    i = r32(l, RD09_TABLES);
    w32(l, 0x006c5834u + 8u * i, 0x004a2eb8u);
    w32(l, RD09_TABLES, i + 1u);
    return i;
}
/* 0x480f38: the current surface released when it is attached. EAX 0, or
 * the surface's flag word when its bit 0 is clear. */
static int rd09_surface_release(es2_sim_loop *l, uint32_t *eax) {
    uint32_t s = r32(l, VA_SURFACE);
    *eax = 0;
    if (!s) return 0;
    uint32_t f = r32(l, s);                       /* 0x481526 */
    if (!(f & 1u)) { *eax = f; return 0; }
    return es2_sim_gl_surface_destroy(l, s);
}
/* 0x480ffc(s, flags): the surface destructor (0x4811e8, then delete). */
static int rd09_surface_delete(es2_sim_loop *l, uint32_t s, uint32_t flags, uint32_t *eax) {
    *eax = r32(l, RD09_COUNTER);
    rd09_count(l, -1);
    if (!s) return 0;
    if (es2_sim_gl_surface_destroy(l, s)) return 1;
    *eax = 0;
    return rd09_delete(l, s, flags, eax);
}
/* 0x4812f0(obj, name): the format code of a name: the codes {1, 0, 3, 4}
 * copied from 0x4a2e94, the names at 0x4a2e88; the walk stops at code 4 or
 * at the first equal name. */
static int rd09_surface_format(es2_sim_loop *l, uint32_t name, uint32_t *eax) {
    uint32_t codes[4];
    for (unsigned i = 0; i < 4u; ++i) codes[i] = r32(l, RD09_FORMAT_CODES + 4u * i);
    unsigned i = 0;
    for (; codes[i] != 4u; ++i) {
        uint32_t a = r32(l, RD09_FORMAT_NAMES + 4u * i), k = 0;
        while (r8(l, a + k) == r8(l, name + k) && r8(l, a + k)) ++k;
        if (r8(l, a + k) == r8(l, name + k)) break;
        if (i == 3u) return refuse(l, 0x0048133eu, "surface format", "no code 4 in the table (the original walks past its copy)");
    }
    *eax = codes[i];
    return 0;
}
/* 0x4813f8(s, which, value): a surface setting through the byte table
 * 0x481418 (which 2, 9, 10, 11, 16, 17: EAX 1; the others and which >
 * 0x11: EAX 0). */
static int rd09_surface_set(es2_sim_loop *l, uint32_t s, uint32_t which, uint32_t value, uint32_t *eax) {
    *eax = 1;
    switch (which) {
    case 2:                                        /* slot 7 (value, s+4), then the origin pair */
        if (slot2(l, 7u, value, s + 4u)) return 1;
        if (s == r32(l, VA_SURFACE)) {
            w32(l, 0x004a3676u, r32(l, s + 0x28u));
            w32(l, 0x004a367au, r32(l, s + 0x2cu));
        }
        return 0;
    case 9: w32(l, s + 0x1cu, value); if (s == r32(l, VA_SURFACE)) w32(l, 0x004a366au, r32(l, s + 0x1cu)); return 0;
    case 10: w32(l, s + 0x20u, value); if (s == r32(l, VA_SURFACE)) w32(l, 0x004a366eu, r32(l, s + 0x20u)); return 0;
    case 11: w32(l, s + 0x24u, value); if (s == r32(l, VA_SURFACE)) w32(l, 0x004a3672u, r32(l, s + 0x24u)); return 0;
    case 16: w32(l, 0x004a368eu, value); return 0;
    case 17: {
        if (r32(l, VA_SLOTS + 4u * RD09_SLOT_SET) == 0x004898f6u) return 0;
        uint32_t a[1] = {value};
        return es2_sim_gfx_slot(l, RD09_SLOT_SET, a, 1u, NULL);
    }
    default: *eax = 0; return 0;
    }
}
/* 0x481559(s): 0x4805b4(7, s): slot 7 = s unless both are set (EAX 1). */
static uint32_t rd09_surface_claim(es2_sim_loop *l, uint32_t s) {
    if (s && r32(l, VA_SURFACE)) return 1;
    w32(l, VA_SURFACE, s);
    return 0;
}

/* 0x48156c(b): the bitmap's constructor. EAX b. */
static uint32_t rd09_bitmap_construct(es2_sim_loop *l, uint32_t b) {
    w32(l, b, 0x004a69f0u);
    w32(l, b, 0x004a3368u);
    rd09_count(l, 1);
    w32(l, b, 0x004a3040u);
    rd09_count(l, 1);
    w32(l, b, 0x004a306cu);
    w32(l, b + 0xau, 0);
    w16(l, b + 0x14u, 0);
    w32(l, b + 0x16u, 0);
    w32(l, b + 0xeu, 0);
    rd09_count(l, 1);
    return b;
}
/* 0x481bf1(st, flags): a stream's destructor (0x47a8a4(st, 0), delete). */
static int rd09_stream_destroy(es2_sim_loop *l, uint32_t st, uint32_t flags, uint32_t *eax) {
    if (!st) return 0;
    rd09_count(l, -1);
    uint32_t a[2] = {st, 0};
    if (call(l, RD09_STREAM_BASE_DTOR, a, 2, eax)) return 1;
    return rd09_delete(l, st, flags, eax);
}
/* 0x481cba(b, point): 0x4816a8(b, point, 0). */
static int rd09_bitmap_blit_at(es2_sim_loop *l, uint32_t b, uint32_t point, uint32_t *eax) {
    uint32_t a[3] = {b, point, 0};
    return call(l, RD09_BITMAP_BLIT, a, 3, eax);
}
/* 0x481c91(b): at the origin (a local point set by 0x48d9a7(&p, 0, 0)). */
static int rd09_bitmap_blit_origin(es2_sim_loop *l, uint32_t b, uint32_t *eax) {
    uint32_t top = l->stack_hi, p = frame_push(l, 8u);
    if (!p) return frame_refused(l, 0x00481c91u, "bitmap blit");
    uint32_t a[3] = {p, 0, 0}, point = 0;
    int bad = call(l, RD09_VECTOR_SET, a, 3, &point) || rd09_bitmap_blit_at(l, b, point, eax);
    frame_pop(l, top);
    return bad;
}
/* A stream slot: [[st] + offset](st, n, p). */
static int rd09_stream_slot(es2_sim_loop *l, uint32_t st, uint32_t offset, uint32_t n, uint32_t p, uint32_t *eax) {
    uint32_t a[3] = {st, n, p};
    return call(l, r32(l, r32(l, st) + offset), a, 3, eax);
}
/* 0x481f54(st, byte): slot 5 with one byte, from the argument's slot. */
static int rd09_stream_put_byte(es2_sim_loop *l, uint32_t st, uint8_t value) {
    uint32_t top = l->stack_hi, p = frame_push(l, 4u);
    if (!p) return frame_refused(l, 0x00481f54u, "bitmap write");
    w8(l, p, value);
    int bad = rd09_stream_slot(l, st, 0x14u, 1u, p, NULL);
    frame_pop(l, top);
    return bad;
}
/* 0x481e98(b, st): the bitmap written to a stream (the word and byte
 * arguments as the callee reads them). EAX the last slot call's. */
static int rd09_bitmap_write(es2_sim_loop *l, uint32_t b, uint32_t st, uint32_t *eax) {
    uint32_t a[3] = {st, r16(l, b + 4u), 0};
    if (call(l, RD09_PUT_WORD, a, 2, NULL)) return 1;
    a[1] = r16(l, b + 6u);
    if (call(l, RD09_PUT_WORD, a, 2, NULL)) return 1;
    a[1] = r8(l, b + 8u);
    if (call(l, RD09_PUT_BYTE, a, 2, NULL)) return 1;
    uint8_t bits = r8(l, b + 9u);
    if (rd09_stream_put_byte(l, st, (uint8_t)((bits & 0xfu) | (bits & 0x10u)))) return 1;
    if (rd09_stream_put_byte(l, st, r8(l, b + 0x12u))) return 1;
    a[1] = r32(l, b + 0xau);
    if (call(l, RD09_PUT_HEADER, a, 2, NULL)) return 1;
    a[1] = r16(l, b + 0x14u);
    if (call(l, RD09_PUT_WORD, a, 2, NULL)) return 1;
    a[1] = r32(l, b + 0xau);
    a[2] = r32(l, b + 0xeu);
    if (call(l, RD09_PUT_COLOURS, a, 3, NULL)) return 1;
    return rd09_stream_slot(l, st, 0x14u, (uint32_t)(s16(l, b + 0x14u) * 4), r32(l, b + 0x16u), eax);
}

/* The rows. RD09_SERVICE keeps the entry EAX where the original leaves it
 * (the destructors with a NULL object). */
#define RD09_SERVICE(name, body) \
    void es2_sim_service_gfx_##name(es2_sim_service_call *sc, void *user) { \
        es2_sim_loop *l; (void)user; \
        uint32_t eax = sc->eax; \
        if (serve(sc, &l)) return; \
        if (body) failed(sc); else sc->eax = eax; \
    }
RD09_SERVICE(gl_device_rect_get, (eax = rd09_device_rect_get(l, sc->slots[0], sc->slots[1]), 0))
RD09_SERVICE(gl_device_rect_set, (eax = rd09_device_rect_set(l, sc->slots[0], sc->slots[1]), 0))
RD09_SERVICE(gl_handler_add, (eax = rd09_handler_add(l, sc->slots[1], sc->slots[2]), 0))
RD09_SERVICE(gl_handler_remove, (w32(l, RD09_HANDLERS + 12u * (sc->slots[1] & 0xffffu), 0), eax = 0, 0))
RD09_SERVICE(gl_device_slot2, (eax = r32(l, RD09_DEVICES + 8u), 0))
RD09_SERVICE(gl_device_destroy, rd09_device_destroy(l, sc->slots[0], sc->slots[1], &eax))
RD09_SERVICE(gl_device_word, (w16(l, sc->slots[0] + 4u, (uint16_t)sc->slots[1]), eax = r32(l, sc->slots[0]) & 1u, 0))
RD09_SERVICE(gl_device_clear, (w32(l, RD09_DEVICES + 4u * sc->slots[1], 0), eax = sc->slots[1], 0))
RD09_SERVICE(gl_device_slot0, (eax = r32(l, RD09_DEVICES), 0))
RD09_SERVICE(gl_gc_construct, rd09_gc_construct(l, sc->slots[0], &eax))
RD09_SERVICE(gl_gc_destroy, rd09_gc_destroy(l, sc->slots[0], sc->slots[1], &eax))
RD09_SERVICE(gl_surface_clip, (eax = r32(l, sc->slots[0] + 0x40u), 0))
RD09_SERVICE(gl_surface_set_clip, (eax = rd09_surface_set_clip(l, sc->slots[0], sc->slots[1]), 0))
RD09_SERVICE(gl_register, (eax = rd09_register(l), 0))
RD09_SERVICE(gl_minus_one, (eax = 0xffffffffu, 0))
RD09_SERVICE(gl_surface_release, rd09_surface_release(l, &eax))
RD09_SERVICE(gl_surface_construct, (eax = sc->slots[0], es2_sim_gl_surface_construct(l, sc->slots[0], sc->slots[1])))
RD09_SERVICE(gl_surface_delete, rd09_surface_delete(l, sc->slots[0], sc->slots[1], &eax))
RD09_SERVICE(gl_surface_create, es2_sim_gl_surface_create(l, sc->slots[0], sc->slots[1], &eax))
RD09_SERVICE(gl_surface_destroy, (eax = 0, es2_sim_gl_surface_destroy(l, sc->slots[0])))
RD09_SERVICE(gl_surface_select, (eax = 0, es2_sim_gl_surface_select(l, sc->slots[0])))
RD09_SERVICE(gl_surface_format, rd09_surface_format(l, sc->slots[1], &eax))
RD09_SERVICE(gl_surface_set, rd09_surface_set(l, sc->slots[0], sc->slots[1], sc->slots[2], &eax))
RD09_SERVICE(gl_surface_current, (eax = r32(l, VA_SURFACE), 0))
RD09_SERVICE(gl_object_flags, (eax = r32(l, sc->slots[0]), 0))
RD09_SERVICE(gl_surface_unselect, (eax = rd09_surface_claim(l, 0), 0))
RD09_SERVICE(gl_surface_claim, (eax = rd09_surface_claim(l, sc->slots[0]), 0))
RD09_SERVICE(gl_bitmap_construct, (eax = rd09_bitmap_construct(l, sc->slots[0]), 0))
RD09_SERVICE(gl_stream_destroy, rd09_stream_destroy(l, sc->slots[0], sc->slots[1], &eax))
RD09_SERVICE(gl_bitmap_blit_origin, rd09_bitmap_blit_origin(l, sc->slots[0], &eax))
RD09_SERVICE(gl_bitmap_blit_at, rd09_bitmap_blit_at(l, sc->slots[0], sc->slots[1], &eax))
RD09_SERVICE(gl_bitmap_size_d, (eax = (uint32_t)(s16(l, sc->slots[0] + 0x14u) * 4) + r32(l, sc->slots[0] + 0xau) + 0xdu, 0))
RD09_SERVICE(gl_bitmap_size_1a, (eax = (uint32_t)(s16(l, sc->slots[0] + 0x14u) * 4) + r32(l, sc->slots[0] + 0xau) + 0x1au, 0))
RD09_SERVICE(gl_bitmap_write, rd09_bitmap_write(l, sc->slots[0], sc->slots[1], &eax))
RD09_SERVICE(gl_stream_read_byte, rd09_stream_slot(l, sc->slots[0], 0x18u, 1u, sc->slots[1], &eax))

/* ======================================================================
 * The blit and clear rest (listing 0x486e10..0x486e3e, 0x486e7c..0x486ecd,
 * 0x487bdc..0x487d0c, 0x487e94..0x487eab, 0x487eac..0x487f47,
 * 0x488dc7..0x488df5; rasterizer.md, "Blit, clear and cursor rows";
 * rows in sim/dispatch/services/gfx_blit.h). 0x488012, 0x488045 and 0x487e60 hand
 * their kind-0 records to these bodies; 0x488b98 calls the two rotation thunks. */
#define RD11_DATA_LO 0x00497000u           /* DATA..BSS of the image */
#define RD11_DATA_HI 0x006cc000u
#define RD11_CURSOR_ON 0x004a3646u         /* dword: 0 while a slide runs */
#define RD11_CURSOR_SHOWN 0x004a361cu      /* dword: the driver cursor is up */
#define RD11_CURSOR_POINT 0x004a3620u      /* the cursor point {x, y} */
#define RD11_CURSOR_HELD 0x004a364au       /* the point kept while a slide runs */
#define RD11_CURSOR_HIDE 0x004a5828u       /* -> the driver's cursor hide (no arguments) */
#define RD11_CURSOR_SHOW 0x004a5824u       /* -> the driver's cursor show (no arguments) */

static void rd11_regs(es2_sim_gfx_regs *regs, const es2_sim_result *r) {
    regs->eax = r->eax;
    regs->edx = r->edx;
    regs->ecx = r->ecx;
}

/* 0x486e10(first, count, src, _): count records of 8 bytes from src into
 * the ramp table 0x4a4fa5 from entry `first` (lodsd/stosd twice per
 * record, `loop` on ECX). EAX the last dword copied (the entry EAX for
 * count 0), ECX 0, EDX unchanged; the fourth argument is loaded into EBX
 * and not used. A copy that would leave DATA..BSS is refused (the
 * original writes on until it faults). */
int es2_sim_gfx_ramps_set(es2_sim_loop *l, uint32_t first, uint32_t count, uint32_t src, es2_sim_gfx_regs *regs) {
    uint32_t dst = (first << 3) + VA_RAMPS;
    if (count && (dst < RD11_DATA_LO || (uint64_t)dst + 8u * (uint64_t)count > RD11_DATA_HI))
        return refuse(l, 0x00486e10u, "ramps_set", "a copy past the image's DATA (the original writes on until it faults)");
    for (uint32_t i = 0; i < count; ++i, src += 8u, dst += 8u) {
        w32(l, dst, r32(l, src));
        uint32_t hi = r32(l, src + 4u);
        w32(l, dst + 4u, hi);
        regs->eax = hi;
    }
    regs->ecx = 0;
    return 0;
}

/* 0x486e7c(point): with [0x4a3646] set, the cursor point 0x4a3620 <- the
 * point's two dwords, bracketed by the driver's hide [0x4a5828] and show
 * [0x4a5824] when [0x4a361c] is set ([0x4a361c] read once, before the
 * hide); with [0x4a3646] 0 (a slide runs), 0x4a364a <- the point. EAX
 * the point's y, ECX the point, EDX the target, or the show's registers. */
int es2_sim_gfx_cursor_point_set(es2_sim_loop *l, uint32_t point, es2_sim_gfx_regs *regs) {
    uint32_t target = RD11_CURSOR_HELD, shown = 0;
    es2_sim_result r;
    if (r32(l, RD11_CURSOR_ON)) {
        target = RD11_CURSOR_POINT;
        shown = r32(l, RD11_CURSOR_SHOWN);
        if (shown && es2_sim_loop_call(l, r32(l, RD11_CURSOR_HIDE), NULL, 0, &r)) return 1;
    }
    w32(l, target, r32(l, point));
    uint32_t y = r32(l, point + 4u);
    w32(l, target + 4u, y);
    regs->eax = y;
    regs->ecx = point;
    regs->edx = target;
    if (shown) {
        if (es2_sim_loop_call(l, r32(l, RD11_CURSOR_SHOW), NULL, 0, &r)) return 1;
        rd11_regs(regs, &r);
    }
    return 0;
}

/* 0x487bdc(record, point): the kind-0 scroll (the record's kind is not
 * read). The point is read first, then shifted by the clip rect's (l, t)
 * minus the origin when [C+0x208] is non-zero; the record's rect at +4
 * moved by it is the destination: mode 0 through slot 20, mode 1 clipped
 * by the clip rect (0x48699c into a local), any other mode per region
 * from the last to the first (a kind-0 region clipped through 0x48699c
 * and slot 20, a kind-2 one through 0x486d15 into span buffer A and slot
 * 19, other kinds skipped; C re-read per region; the index starts at
 * count - 1 and goes on while the previous index was >= 1, signed). The
 * slots get the unshifted point. */
int es2_sim_gfx_scroll_rect(es2_sim_loop *l, uint32_t record, uint32_t point) {
    uint32_t mx = r32(l, point), my = r32(l, point + 4u);
    uint32_t c = r32(l, VA_CONTEXT);
    if (r32(l, c + 0x208u)) {
        mx += r32(l, c + 0x20cu) - r32(l, c + 0x21cu);
        my += r32(l, c + 0x210u) - r32(l, c + 0x220u);
    }
    uint32_t top = l->stack_hi, f = frame_push(l, 0x20u);
    if (!f) return frame_refused(l, 0x00487bdcu, "scroll_rect");
    uint32_t rect = f + 0x10u, out = f;
    w32(l, rect, r32(l, record + 4u) + mx);
    w32(l, rect + 4u, r32(l, record + 8u) + my);
    w32(l, rect + 8u, r32(l, record + 12u) + mx);
    w32(l, rect + 12u, r32(l, record + 16u) + my);
    uint32_t mode = r32(l, c + 0x204u), res = 0;
    int bad = 0;
    if (mode == 0) {
        bad = slot2(l, SLOT_SCROLL_RECT, rect, point);
    } else if (mode == 1u) {
        bad = es2_sim_gfx_rect_clip(l, rect, c + 0x20cu, out, &res);
        if (!bad && res) bad = slot2(l, SLOT_SCROLL_RECT, res, point);
    } else {
        uint32_t count = r32(l, c);
        if (count) {
            int32_t i = (int32_t)(count - 1u);
            for (;;) {
                uint32_t e = r32(l, r32(l, VA_CONTEXT) + 4u + 4u * (uint32_t)i), kind = r32(l, e);
                if (kind == 0) {
                    bad = es2_sim_gfx_rect_clip(l, rect, e + 4u, out, &res);
                    if (!bad && res) bad = slot2(l, SLOT_SCROLL_RECT, res, point);
                } else if (kind == 2u) {
                    bad = es2_sim_gfx_rect_clip_spans(l, rect, r32(l, e + 4u), r32(l, VA_SPANS_A), &res);
                    if (!bad && res) bad = slot2(l, SLOT_SCROLL_SPANS, res, point);
                }
                if (bad || i < 1) break;
                --i;
            }
        }
    }
    frame_pop(l, top);
    return bad;
}

/* 0x487e94(rect): (b - t + 1) * (r - l + 1) by a signed imul: EAX the low
 * dword, EDX the high one, ECX b - t + 1. */
void es2_sim_gfx_rect_area(es2_sim_loop *l, uint32_t rect, es2_sim_gfx_regs *regs) {
    uint32_t rows = r32(l, rect + 12u) - r32(l, rect + 4u) + 1u;
    uint32_t cols = r32(l, rect + 8u) - r32(l, rect) + 1u;
    int64_t p = (int64_t)(int32_t)cols * (int64_t)(int32_t)rows;
    regs->eax = (uint32_t)(uint64_t)p;
    regs->edx = (uint32_t)((uint64_t)p >> 32);
    regs->ecx = rows;
}

/* 0x487eac(record, pixels): the kind-0 save (the kind is not read): the
 * rect at +4, shifted into a local by the clip rect's (l, t) minus the
 * origin when [C+0x208] is non-zero, else the record's own; a header
 * {b - t + 1, r - l + 1, 8, 0, w * h, pixels} grabbed through 0x488974
 * at the rect's (l, t) with flags 0. */
int es2_sim_gfx_save_rect(es2_sim_loop *l, uint32_t record, uint32_t pixels) {
    uint32_t top = l->stack_hi, f = frame_push(l, 0x20u);
    if (!f) return frame_refused(l, 0x00487eacu, "save_rect");
    uint32_t c = r32(l, VA_CONTEXT), rect = record + 4u, hdr = f + 0x10u;
    if (r32(l, c + 0x208u)) {
        uint32_t dx = r32(l, c + 0x20cu) - r32(l, c + 0x21cu), dy = r32(l, c + 0x210u) - r32(l, c + 0x220u);
        rect = f;
        w32(l, rect, r32(l, record + 4u) + dx);
        w32(l, rect + 4u, r32(l, record + 8u) + dy);
        w32(l, rect + 8u, r32(l, record + 12u) + dx);
        w32(l, rect + 12u, r32(l, record + 16u) + dy);
    }
    uint32_t h = r32(l, rect + 12u) - r32(l, rect + 4u) + 1u, w = r32(l, rect + 8u) - r32(l, rect) + 1u;
    w16(l, hdr, (uint16_t)h);
    w16(l, hdr + 2u, (uint16_t)w);
    w8(l, hdr + 4u, 8u);
    w8(l, hdr + 5u, 0);
    w32(l, hdr + 6u, h * w);
    w32(l, hdr + 0xau, pixels);
    int bad = es2_sim_gfx_bitmap_grab(l, hdr, rect, 0);
    frame_pop(l, top);
    return bad;
}

/* 0x488dc7(matrix, angle): sincos_pair 0x47eb30(angle', matrix), angle'
 * the angle's low word under the matrix address's high word (mov ax over
 * EAX = matrix). The callee's registers. */
int es2_sim_gfx_rotation_matrix(es2_sim_loop *l, uint32_t matrix, uint32_t angle, es2_sim_gfx_regs *regs) {
    uint32_t a[2] = {(matrix & 0xffff0000u) | (angle & 0xffffu), matrix};
    es2_sim_result r;
    if (es2_sim_loop_call(l, VA_SINCOS, a, 2u, &r)) return 1;
    rd11_regs(regs, &r);
    return 0;
}
/* 0x488ddd(v, matrix, out): rotate_2d 0x47ffe8(v, matrix, out). The
 * callee's registers. */
int es2_sim_gfx_rotate_point(es2_sim_loop *l, uint32_t v, uint32_t matrix, uint32_t out, es2_sim_gfx_regs *regs) {
    uint32_t a[3] = {v, matrix, out};
    es2_sim_result r;
    if (es2_sim_loop_call(l, VA_ROTATE, a, 3u, &r)) return 1;
    rd11_regs(regs, &r);
    return 0;
}

/* The rows: the entry registers in, the original's out (EAX 0 where the
 * original leaves a callee's: 0x487bdc and 0x487eac end in driver slots
 * and 0x488974, as the primitive rows do). */
#define RD11_SERVICE(name, body) \
    void es2_sim_service_gfx_##name(es2_sim_service_call *sc, void *user) { \
        es2_sim_loop *l; (void)user; \
        es2_sim_gfx_regs regs = {sc->eax, sc->edx, sc->ecx}; \
        if (serve(sc, &l)) return; \
        if (body) { failed(sc); return; } \
        sc->eax = regs.eax; sc->edx = regs.edx; sc->ecx = regs.ecx; \
    }
RD11_SERVICE(ramps_set, es2_sim_gfx_ramps_set(l, sc->slots[0], sc->slots[1], sc->slots[2], &regs))
RD11_SERVICE(cursor_point_set, es2_sim_gfx_cursor_point_set(l, sc->slots[0], &regs))
RD11_SERVICE(scroll_rect, (regs.eax = 0, es2_sim_gfx_scroll_rect(l, sc->slots[0], sc->slots[1])))
RD11_SERVICE(rect_area, (es2_sim_gfx_rect_area(l, sc->slots[0], &regs), 0))
RD11_SERVICE(save_rect, (regs.eax = 0, es2_sim_gfx_save_rect(l, sc->slots[0], sc->slots[1])))
RD11_SERVICE(rotation_matrix, es2_sim_gfx_rotation_matrix(l, sc->slots[0], sc->slots[1], &regs))
RD11_SERVICE(rotate_point, es2_sim_gfx_rotate_point(l, sc->slots[0], sc->slots[1], sc->slots[2], &regs))

/* ======================================================================
 * The BMP_8 driver's cursor point 0x48993a and slot 33, the textured polygon
 * 0x48a924 with its edge step 0x48aa50 and span 0x48abe4 (listing
 * 0x48993a..0x489996, 0x48a924..0x48afc4; rasterizer.md, "Cursor point and
 * textured slot 33"). The edge set-up tex_edge 0x48aaac is the core function
 * es2_tex_edge_registers (the registry hook's body), called directly after the
 * checks of the faults its divisions have in the original. */
#include "es2/core/math_lib.h"

#define RD12_CURSOR_ON 0x004a3646u    /* the software cursor is in use */
#define RD12_CURSOR_SHOWN 0x004a361cu
#define RD12_CURSOR 0x004a3620u       /* its point */
#define RD12_POINT 0x004a364au        /* the point without the cursor */
#define RD12_SLOT_HIDE 31u            /* 0x4898df in BMP_8 */
#define RD12_SHOW 0x004898d4u
#define RD12_ROW 0x006c5f7cu          /* the polygon's current row y */
#define RD12_END 0x006c5f80u          /* the index of the lowest corner */
#define RD12_COUNT 0x006c5f84u
#define RD12_SCREEN 0x006c5f88u       /* the corner list {x, y} */
#define RD12_TEXTURE 0x006c5f8cu      /* the fill's texture corners {u, v} */
#define RD12_SPAN 0x004a5900u         /* end, A << 16, B << 16, steps[4], ESP, EBP */

/* 0x48993a(point): the cursor point of the BMP_8 driver. With the
 * software cursor on ([0x4a3646]): a shown cursor ([0x4a361c]) is hidden
 * through slot 31, the point stored at 0x4a3620, [0x4a361c] restored to
 * the value before the slot and the cursor shown again by 0x4898d4 (a
 * direct call; 0x486e7c uses slot 30 there). Off: the point goes to
 * 0x4a364a. EAX the point's y, EDX the target, ECX the point. */
int es2_sim_gfx_cursor_point(es2_sim_loop *l, uint32_t point, uint32_t regs[3]) {
    uint32_t target = RD12_POINT;
    if (r32(l, RD12_CURSOR_ON)) {
        uint32_t shown = r32(l, RD12_CURSOR_SHOWN);
        if (shown && es2_sim_gfx_slot(l, RD12_SLOT_HIDE, NULL, 0, NULL)) return 1;
        target = RD12_CURSOR;
        w32(l, target, r32(l, point));
        w32(l, target + 4u, r32(l, point + 4u));
        w32(l, RD12_CURSOR_SHOWN, shown);
        if (shown && call(l, RD12_SHOW, NULL, 0, NULL)) return 1;
    } else {
        w32(l, target, r32(l, point));
        w32(l, target + 4u, r32(l, point + 4u));
    }
    regs[0] = r32(l, point + 4u);
    regs[1] = target;
    regs[2] = point;
    return 0;
}

/* 0x47e068(a, b) as slot 33 uses it: (a << 16) sign-extended, IDIV b; a
 * zero divisor or a quotient outside 32 bits faults (#DE) in the
 * original and is refused. */
static int rd12_q16(es2_sim_loop *l, uint32_t a, uint32_t b, int32_t *out) {
    int64_t q;
    uint64_t n = (uint64_t)((int64_t)(int32_t)a * 65536);
    if ((int32_t)b == 0) return es2_sim_loop_divide_refuse(l, 0x0047e078u, n, b, 1, "textured polygon", "0x47e068 divides by zero (#DE)");
    q = (int64_t)(int32_t)a * 65536 / (int32_t)b;
    if (q < INT32_MIN || q > INT32_MAX)
        return es2_sim_loop_divide_refuse(l, 0x0047e078u, n, b, 1, "textured polygon", "0x47e068's quotient overflows (#DE)");
    *out = (int32_t)q;
    return 0;
}
/* 0x47e07d(a, b): imul, shrd 16 (bits 16..47 of the product). */
static uint32_t rd12_mul16(uint32_t a, uint32_t b) {
    return (uint32_t)((uint64_t)((int64_t)(int32_t)a * (int32_t)b) >> 16);
}

/* tex_edge 0x48aaac(edge, from) through the core function. Its walk
 * is repeated read-only first: the edge it settles on must not fault in
 * the original's divisions (0x47e068 by dy << 16 twice, IDIV dx / dy). */
static int rd12_tex_edge(es2_sim_loop *l, uint32_t e, uint32_t from, uint32_t *eax) {
    uint32_t n = r32(l, RD12_COUNT), end = r32(l, RD12_END), p = r32(l, RD12_SCREEN), t = r32(l, RD12_TEXTURE);
    uint32_t cur = from, to = 0, dy = 0;
    for (uint32_t k = 0; cur != end; ++k) {
        if (k > n) return refuse(l, 0x0048aaacu, "tex_edge", "the walk does not reach the end corner (the original loops)");
        to = r32(l, e) + cur;
        if (!((int32_t)to < (int32_t)n)) to = 0;
        else if ((int32_t)to < 0) to = n - 1u;
        dy = r32(l, p + 8u * to + 4u) - r32(l, p + 8u * cur + 4u);
        if (dy) break;
        cur = to;
    }
    if (cur != end) {
        int32_t q;
        uint32_t scaled = dy << 16;
        if (rd12_q16(l, (r32(l, t + 8u * to) << 16) - (r32(l, t + 8u * cur) << 16), scaled, &q) ||
            rd12_q16(l, (r32(l, t + 8u * to + 4u) << 16) - (r32(l, t + 8u * cur + 4u) << 16), scaled, &q))
            return 1;
        uint32_t dx = r32(l, p + 8u * to) - r32(l, p + 8u * cur);
        if ((int32_t)dx < 0) dx = 0u - dx;
        if (dx == 0x80000000u && dy == 0xffffffffu)
            return es2_sim_loop_divide_refuse(l, 0x0048aba2u, 0xffffffff80000000u, dy, 1, "tex_edge", "IDIV of -2^31 by -1 (#DE)");
    }
    *eax = (uint32_t)es2_tex_edge_registers(l->memory, 0x00400000u, e, from, 0);
    return 0;
}

/* 0x48aa50(edge): the edge one row down. The row count +4 runs out: the
 * next edge (tex_edge from +8), EAX its result. Else u +0xc and v +0x10
 * step by +0x14 and +0x18, x +0x1c by +0x20, the error +0x28 by +0x2c;
 * a positive error adds the sign +0x24 to x and takes +0x30 off. EAX 1. */
static int rd12_edge_step(es2_sim_loop *l, uint32_t e, uint32_t *more) {
    uint32_t rows = r32(l, e + 4u) - 1u;
    w32(l, e + 4u, rows);
    if (!rows) return rd12_tex_edge(l, e, r32(l, e + 8u), more);
    w32(l, e + 0xcu, r32(l, e + 0xcu) + r32(l, e + 0x14u));
    w32(l, e + 0x10u, r32(l, e + 0x10u) + r32(l, e + 0x18u));
    w32(l, e + 0x1cu, r32(l, e + 0x1cu) + r32(l, e + 0x20u));
    w32(l, e + 0x28u, r32(l, e + 0x28u) + r32(l, e + 0x2cu));
    if (s32(l, e + 0x28u) > 0) {
        w32(l, e + 0x1cu, r32(l, e + 0x1cu) + r32(l, e + 0x24u));
        w32(l, e + 0x28u, r32(l, e + 0x28u) - r32(l, e + 0x30u));
    }
    *more = 1;
    return 0;
}

/* The span state of 0x48abe4 after its set-up (0x48ac14..0x48acb8). */
typedef struct rd12_span_state {
    uint32_t u, v;          /* the left edge's u, v plus half a step (Q16) */
    uint32_t du, dv;        /* the steps per pixel (Q16) */
    uint32_t su, sv;        /* the carries' source steps: +-1, +-width */
    uint32_t step;          /* the integer parts: iu + iv * width */
    uint32_t width, pixels; /* the texture header's word +2 and pixels +0xa */
    uint32_t shade;         /* the kind-7 shade table, 0 */
    uint32_t frame;         /* the own stand-in for the original's EBP */
} rd12_span_state;

/* 0x48ae85..0x48afb4: the pixels xl..xr of the row [0x6c5f7c], the
 * texture coordinates (u, v) at xl. The rounding adds 0x8000 to the low
 * words (the carries go into the 16-bit integer parts, zero-extended),
 * the source is pixels + rv * width + ru; a negative step sign (the low
 * word of su or sv as int16) negates that step's fraction word and
 * complements the coordinate's. The kernels add the fractions as 32-bit
 * words and step the source by the carries: with a shade table each
 * texel is looked up in it, without it a 0 texel is transparent (the
 * steps table 0x4a590c..0x4a5918 indexed by the carries v, u). */
static int rd12_span_draw(es2_sim_loop *l, const rd12_span_state *s, int32_t xl, int32_t xr, uint32_t cu, uint32_t cv) {
    uint32_t cols = (uint32_t)xr - (uint32_t)xl + 1u;
    if ((int32_t)cols <= 0) return refuse(l, 0x0048af42u, "textured polygon", "no visible column (the original's span kernel runs away)");
    uint32_t lo = (cv & 0xffffu) + 0x8000u;
    uint32_t base = (((cv >> 16) & 0xffffu) + (lo >> 16)) * s->width + s->pixels;
    cv = (cv & 0xffff0000u) | (lo & 0xffffu);
    lo = (cu & 0xffffu) + 0x8000u;
    uint32_t src = ((cu >> 16) & 0xffffu) + (lo >> 16) + base;
    cu = (cu & 0xffff0000u) | (lo & 0xffffu);
    uint32_t dst = pixel_at(l, xl, s32(l, RD12_ROW));
    uint32_t a = s->du & 0xffffu, b = s->dv & 0xffffu;
    if ((int16_t)(uint16_t)s->su < 0) { a = (0u - a) & 0xffffu; cu ^= 0xffffu; }
    if ((int16_t)(uint16_t)s->sv < 0) { b = (0u - b) & 0xffffu; cv ^= 0xffffu; }
    uint32_t end = dst + cols;
    w32(l, RD12_SPAN, end);
    w32(l, RD12_SPAN + 0xcu, s->step);
    a <<= 16;
    b <<= 16;
    w32(l, RD12_SPAN + 4u, a);
    w32(l, RD12_SPAN + 8u, b);
    uint32_t fu = cu << 16, fv = cv << 16;
    if (s->shade) {                                   /* 0x48af19 */
        do {
            uint32_t texel = r8(l, src) + s->shade, f;
            src += s->step;
            f = fu + a; if (f < fu) src += s->su; fu = f;
            f = fv + b; if (f < fv) src += s->sv; fv = f;
            w8(l, dst, r8(l, texel));
            ++dst;
        } while (dst != end);
        return 0;
    }
    uint32_t steps[4] = {s->step, s->step + s->su, s->step + s->sv, s->step + s->su + s->sv};
    w32(l, RD12_SPAN + 0x10u, steps[1]);
    w32(l, RD12_SPAN + 0x18u, steps[3]);
    w32(l, RD12_SPAN + 0x14u, steps[2]);
    w32(l, RD12_SPAN + 0x1cu, l->stack_hi);           /* the original parks ESP and EBP here */
    w32(l, RD12_SPAN + 0x20u, s->frame);
    do {                                              /* 0x48af87 */
        uint32_t f = fv + b;
        unsigned k = f < fv;
        fv = f;
        f = fu + a;
        k = 2u * k + (f < fu);
        fu = f;
        uint8_t texel = r8(l, src);
        if (texel) w8(l, dst, texel);
        src += steps[k];
        ++dst;
    } while (dst != end);
    return 0;
}

/* 0x48abe4(left, right, header, shade): the row [0x6c5f7c] between the
 * edges' x (+0x1c). Nothing when the clip rect lies beside the span. The
 * steps du, dv are 0x47e068 of the edges' u and v differences by the
 * width << 16; their integer parts are rounded toward zero. Clip modes
 * other than 2: the span clipped to the rect, the coordinates advanced to
 * a clipped start by 0x47e07d. Mode 2: each region from the last to the
 * first, kind 0 (a rect, the row inside) and kind 2 (a span list; the
 * row's pair) clipped likewise; any other kind ends the span. */
static int rd12_span(es2_sim_loop *l, uint32_t le, uint32_t re, uint32_t header, uint32_t shade, uint32_t frame) {
    uint32_t c = r32(l, VA_CONTEXT);
    int32_t lx = s32(l, le + 0x1cu), rx = s32(l, re + 0x1cu);
    if (s32(l, c + 0x214u) < lx || s32(l, c + 0x20cu) > rx) return 0;
    rd12_span_state s;
    s.u = r32(l, le + 0xcu);
    s.v = r32(l, le + 0x10u);
    s.width = r16(l, header + 2u);
    s.pixels = r32(l, header + 0xau);
    s.shade = shade;
    s.frame = frame;
    uint32_t w = ((uint32_t)rx - (uint32_t)lx + 1u) << 16;
    int32_t du, dv, iu, iv;
    if (rd12_q16(l, r32(l, re + 0xcu) - s.u, w, &du)) return 1;
    s.su = du < 0 ? 0xffffffffu : 1u;
    iu = du < 0 && (du & 0xffff) ? (du + 0x10000) >> 16 : du >> 16;
    if (rd12_q16(l, r32(l, re + 0x10u) - s.v, w, &dv)) return 1;
    s.sv = dv < 0 ? 0u - s.width : s.width;
    iv = dv < 0 && (dv & 0xffff) ? (dv + 0x10000) >> 16 : dv >> 16;
    s.step = (uint32_t)iu + (uint32_t)iv * s.width;
    s.du = (uint32_t)du;
    s.dv = (uint32_t)dv;
    s.u += (uint32_t)(du >> 1);
    s.v += (uint32_t)(dv >> 1);
    c = r32(l, VA_CONTEXT);
    if (r32(l, c + 0x204u) != 2u) {
        int32_t xr = imin(rx, s32(l, c + 0x214u)), xl = lx, cl = s32(l, c + 0x20cu);
        uint32_t cu = s.u, cv = s.v;
        if (lx < cl) {
            uint32_t shift = ((uint32_t)cl - (uint32_t)lx) << 16;
            xl = cl;
            cu += rd12_mul16(s.du, shift);
            cv += rd12_mul16(s.dv, shift);
        }
        return rd12_span_draw(l, &s, xl, xr, cu, cv);
    }
    uint32_t i = r32(l, c);
    if (!i) return 0;
    do {
        --i;
        c = r32(l, VA_CONTEXT);
        uint32_t entry = r32(l, c + 4u + 4u * i), kind = r32(l, entry);
        int32_t y = s32(l, RD12_ROW), xl = lx, xr, sl;
        if (kind == 0) {
            if (y < s32(l, entry + 8u) || y > s32(l, entry + 0x10u) || rx < s32(l, entry + 4u)) continue;
            xr = imin(rx, s32(l, entry + 0xcu));
            if (lx > s32(l, entry + 0xcu)) continue;
            sl = s32(l, entry + 4u);
        } else if (kind == 2u) {
            uint32_t p = r32(l, entry + 4u);
            int32_t top = s16(l, p);
            if (y < top || y >= s16(l, p + 2u) + top) continue;
            uint32_t pair = p + (((uint32_t)y - (uint32_t)top + 1u) << 2);
            int32_t r = s16(l, pair + 2u);
            if (r < lx) continue;
            xr = imin(rx, r);
            sl = s16(l, pair);
            if (sl > rx) continue;
        } else {
            return 0;
        }
        uint32_t cu = s.u, cv = s.v;
        if (lx < sl) {
            uint32_t shift = ((uint32_t)sl - (uint32_t)lx) << 16;
            xl = sl;
            cu += rd12_mul16(s.du, shift);
            cv += rd12_mul16(s.dv, shift);
        }
        if (rd12_span_draw(l, &s, xl, xr, cu, cv)) return 1;
    } while (i);
    return 0;
}

/* Slot 33 (0x48a924, n, points): the polygon of n corners {x, y} filled
 * with the kind-5 or kind-7 fill [C+0x228] (+8 the texture corners {u,
 * v}, +0xc the texture header, kind 7 +0x10 a shade table). Nothing for
 * n < 3 or a flat polygon. From the first topmost corner a left edge
 * (direction -1) and a right edge (+1) walk down to the first lowest one
 * ([0x6c5f80]); each row from the top to the clip bottom is drawn by
 * 0x48abe4 from the clip top on, until the edges cross or one ends. The
 * two edge records (0x34 bytes each) live in the own call area. */
int es2_sim_gfx_polygon(es2_sim_loop *l, uint32_t n, uint32_t points) {
    w32(l, RD12_COUNT, n);
    if ((int32_t)n < 3) return 0;
    w32(l, RD12_SCREEN, points);
    uint32_t c = r32(l, VA_CONTEXT), fill = r32(l, c + 0x228u);
    w32(l, RD12_TEXTURE, r32(l, fill + 8u));
    uint32_t header = r32(l, fill + 0xcu), shade = r32(l, fill) == 7u ? r32(l, fill + 0x10u) : 0;
    int32_t ymin = 0x7fff, ymax = -0x8000;
    uint32_t top_index = 0;
    int have_top = 0;
    for (uint32_t i = 0; (int32_t)i < (int32_t)n; ++i) {
        int32_t y = s32(l, points + 8u * i + 4u);
        if (ymin > y) { ymin = y; top_index = i; have_top = 1; }
        if (ymax < y) { ymax = y; w32(l, RD12_END, i); }
    }
    if (ymax <= ymin) return 0;
    if (!have_top) return refuse(l, 0x0048a9bbu, "textured polygon", "no corner above y 0x7fff (the original reads an unset local)");
    uint32_t top = l->stack_hi, f = frame_push(l, 0x68u);
    if (!f) return frame_refused(l, 0x0048a924u, "textured polygon");
    uint32_t left = f + 0x34u, right = f, more = 0;
    int bad;
    w32(l, left, 0xffffffffu);
    bad = rd12_tex_edge(l, left, top_index, &more);
    if (!bad) {
        w32(l, right, 1u);
        bad = rd12_tex_edge(l, right, top_index, &more);
    }
    if (!bad) w32(l, RD12_ROW, (uint32_t)ymin);
    while (!bad) {
        c = r32(l, VA_CONTEXT);
        int32_t y = s32(l, RD12_ROW);
        if (s32(l, c + 0x218u) < y || s32(l, left + 0x1cu) > s32(l, right + 0x1cu)) break;
        if (y >= s32(l, c + 0x210u) && (bad = rd12_span(l, left, right, header, shade, f + 0x68u)) != 0) break;
        if ((bad = rd12_edge_step(l, left, &more)) != 0 || !more) break;
        if ((bad = rd12_edge_step(l, right, &more)) != 0 || !more) break;
        if (y == INT32_MAX) {
            bad = refuse(l, 0x0048aa41u, "textured polygon", "the row counter wraps (the original's row loop runs on)");
            break;
        }
        w32(l, RD12_ROW, r32(l, RD12_ROW) + 1u);
    }
    frame_pop(l, top);
    return bad;
}

/* The rows (sim/dispatch/services/gfx_bmp8.h): the code at each VA, whatever
 * the driver table holds. */
void es2_sim_service_gfx_cursor_point(es2_sim_service_call *sc, void *user) {
    es2_sim_loop *l;
    uint32_t regs[3];
    (void)user;
    if (serve(sc, &l)) return;
    if (es2_sim_gfx_cursor_point(l, sc->slots[0], regs)) { failed(sc); return; }
    sc->eax = regs[0];
    sc->edx = regs[1];
    sc->ecx = regs[2];
}
GFX_SERVICE(slot_polygon, es2_sim_gfx_polygon(l, sc->slots[0], sc->slots[1]))

/* ======================================================================
 * The open gap roots that own code calls (own_process.md, "SIM, R3D and HUD gap
 * roots as rows and verdicts", rows sim/dispatch/services/gap_roots_sim.h). The stream
 * helpers are graphics library code; the three HUD methods live here because
 * the package's write set is this file and scene.c. Each leaves EAX, EDX and
 * ECX as the listing does. */

/* Original: DBSIM.EXE 0x492447(st, w) (listing 0x492447..0x49245d) and
 * 0x49085d(st, b) (0x49085d..0x490873, the same bytes with a count of 1;
 * llvm-objdump decodes them from 0x49085f): the stream's write slot
 * [[st] + 0x14](st, n, p) with n = 2 or 1 and p the address of the
 * caller's second argument slot (the value is written from where the caller
 * pushed it). EAX, EDX, ECX the slot's. */
static void h524_stream_put(es2_sim_service_call *sc, uint32_t n) {
    es2_sim_loop *l;
    if (serve(sc, &l)) return;
    uint32_t st = sc->slots[0], a[3] = {st, n, sc->stack + 4u};
    es2_sim_result r;
    if (es2_sim_loop_call(l, r32(l, r32(l, st) + 0x14u), a, 3, &r)) { failed(sc); return; }
    sc->eax = r.eax; sc->edx = r.edx; sc->ecx = r.ecx;
}
void es2_sim_service_gfx_stream_put_word(es2_sim_service_call *sc, void *user) { (void)user; h524_stream_put(sc, 2u); }
void es2_sim_service_gfx_stream_put_byte(es2_sim_service_call *sc, void *user) { (void)user; h524_stream_put(sc, 1u); }

/* Original: DBSIM.EXE 0x446e3c(G) (listing 0x446e3c..0x446e5f), the next
 * MFD page (input_dispatch case 14 with [[0x49b088] + 0x1ed]): n = dword
 * G+0xc1 + 1, 0 when that is 6; the page switch 0x446e60(G, n) (the
 * row mfd_page). EAX n, EDX and ECX the switch's. */
#define H524_MFD_PAGE 0x00446e60u
void es2_sim_service_gfx_mfd_next(es2_sim_service_call *sc, void *user) {
    es2_sim_loop *l;
    (void)user;
    if (serve(sc, &l)) return;
    uint32_t g = sc->slots[0], n = r32(l, g + 0xc1u) + 1u;
    if (n == 6u) n = 0;
    uint32_t a[2] = {g, n};
    es2_sim_result r;
    if (es2_sim_loop_call(l, H524_MFD_PAGE, a, 2, &r)) { failed(sc); return; }
    sc->eax = n; sc->edx = r.edx; sc->ecx = r.ecx;
}

/* Original: DBSIM.EXE 0x444b84(B) (listing 0x444b84..0x444b94), slot 0 of
 * the shield arrow button's table 0x49c9dd (B+0x17): panel_visible
 * 0x431410(B) (a hook) and nothing else. EAX and EDX the callee's, ECX B
 * (the POP ECX). */
#define H524_PANEL_VISIBLE 0x00431410u
#define H524_PANEL_COPY 0x0043122cu
void es2_sim_service_gfx_shield_button_draw(es2_sim_service_call *sc, void *user) {
    es2_sim_loop *l;
    (void)user;
    if (serve(sc, &l)) return;
    uint32_t b = sc->slots[0], a[1] = {b};
    es2_sim_result r;
    if (es2_sim_loop_call(l, H524_PANEL_VISIBLE, a, 1, &r)) { failed(sc); return; }
    sc->eax = r.eax; sc->edx = r.edx; sc->ecx = b;
}
/* Original: DBSIM.EXE 0x444b98(B) (listing 0x444b98..0x444bc2), slot 3
 * (+0xc) of 0x49c9dd, the button's frame: AL <- byte B+0x1f; 1: the copy
 * to the screen 0x43122c(0, B, 0), then the byte - 1 (EAX, EDX, ECX the
 * copy's); 2: the byte - 1 (AL 0); any other value: nothing (AL the byte
 * - 2). Without the call EDX, ECX and EAX's upper bytes stay the entry's. */
void es2_sim_service_gfx_shield_button_frame(es2_sim_service_call *sc, void *user) {
    es2_sim_loop *l;
    (void)user;
    uint32_t in = sc->eax;
    if (serve(sc, &l)) return;
    uint32_t b = sc->slots[0];
    uint8_t s = r8(l, b + 0x1fu);
    if (s == 1u) {
        uint32_t a[3] = {0u, b, 0u};
        es2_sim_result r;
        if (es2_sim_loop_call(l, H524_PANEL_COPY, a, 3, &r)) { failed(sc); return; }
        w8(l, b + 0x1fu, (uint8_t)(r8(l, b + 0x1fu) - 1u));
        sc->eax = r.eax; sc->edx = r.edx; sc->ecx = r.ecx;
        return;
    }
    if (s == 2u) w8(l, b + 0x1fu, (uint8_t)(r8(l, b + 0x1fu) - 1u));
    sc->eax = (in & 0xffffff00u) | (uint8_t)(s - 2u);
}
