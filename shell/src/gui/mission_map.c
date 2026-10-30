/* The mission map as own drawing (vshell_mission_map.md, "Own
 * implementation"). This file: the raster layer (VSHELL's context and the
 * primitives of the map), then the map object, its paint, the fly-over and
 * the arrows. */
#include "es2/shell/gui/mission_map.h"
#include "es2/shell/bytes.h"

#include <stdlib.h>
#include <string.h>

/* ---- raster layer ---------------------------------------------------------- */

/* A pixel of the surface; outside is never written (own guard). */
static void plot(const es2_map_surface *s, int32_t x, int32_t y, uint8_t c) {
    if (!s || x < 0 || y < 0 || x >= s->width || y >= s->height) return;
    s->pixels[(size_t)y * (size_t)s->stride + (size_t)x] = c;
}

/* The context offset (0 unless the offset flag is set). */
static void dc_offset(const es2_map_dc *dc, int32_t *dx, int32_t *dy) {
    *dx = dc->offset ? dc->left - dc->origin_x : 0;
    *dy = dc->offset ? dc->top - dc->origin_y : 0;
}

/* A colour dword whose high byte is 0 is an index; 4 selects a ramp entry
 * (not used by the map: refused), anything else draws nothing. */
static int plain(uint32_t colour) { return (colour >> 24) == 0; }

/* Driver slot 0 (0x45a674): the span table {y, n, (left, right) words per
 * row}; rows whose right - left + 1 is not positive are skipped. */
static void span_fill(const es2_map_surface *s, const int16_t *spans, uint8_t c) {
    int32_t y = spans[0], n = (uint16_t)spans[1];
    for (int32_t i = 0; i < n; i++, y++) {
        int32_t l = spans[2 + 2 * i], r = spans[3 + 2 * i];
        for (int32_t x = l; x <= r; x++) plot(s, x, y, c);
    }
}

/* Original: VSHELL.EXE 0x459cdc */
void es2_map_pixel(const es2_map_dc *dc, int32_t x, int32_t y) {
    int32_t dx, dy;
    if (!dc || !plain(dc->pen) || dc->clip_mode > 1) return;
    dc_offset(dc, &dx, &dy);
    x += dx;
    y += dy;
    if (dc->clip_mode == 1 && (x < dc->left || x > dc->right || y < dc->top || y > dc->bottom)) return;
    plot(dc->surface, x, y, (uint8_t)dc->pen);
}

/* Original: VSHELL.EXE 0x45a8e8 (driver slot 34): from the left end point,
 * x-major or y-major with the error term starting at minus half the major
 * span; both end points drawn. */
static void line_slot34(const es2_map_surface *s, uint8_t c, int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
    if (x0 > x1) {
        int32_t t = x0; x0 = x1; x1 = t;
        t = y0; y0 = y1; y1 = t;
    }
    int32_t ax = 2 * (x1 - x0), dy = y1 - y0, sy = 1;
    if (dy < 0) { dy = -dy; sy = -1; }
    int32_t ay = 2 * dy, x = x0, y = y0;
    if (ax > ay) {
        int32_t e = -(int32_t)((uint32_t)ax >> 1);
        for (int32_t n = (int32_t)((uint32_t)ax >> 1) + 1; n > 0; n--) {
            plot(s, x, y, c);
            x++;
            e += ay;
            if (e >= 0) { y += sy; e -= ax; }
        }
    } else {
        int32_t e = -(int32_t)((uint32_t)ay >> 1);
        for (int32_t n = (int32_t)((uint32_t)ay >> 1) + 1; n > 0; n--) {
            plot(s, x, y, c);
            y += sy;
            e += ax;
            if (e >= 0) { x++; e -= ay; }
        }
    }
}

/* Original: VSHELL.EXE 0x45588c: the line clipped against the rectangle,
 * top, left, bottom, right in that order; the end point outside is moved
 * (the points swap when the second one is outside). 0 = rejected. */
static int line_clip(int32_t *p, int32_t l, int32_t t, int32_t r, int32_t b) {
    int32_t x0 = p[0], y0 = p[1], x1 = p[2], y1 = p[3], q;
#define SWAP_ENDS() do { q = x0; x0 = x1; x1 = q; q = y0; y0 = y1; y1 = q; } while (0)
    if (y0 < t) { if (y1 < t) return 0; }
    else if (y1 < t) SWAP_ENDS();
    if (y0 < t) { x0 += (int32_t)((int64_t)(x0 - x1) * (t - y0) / (y0 - y1)); y0 = t; }
    if (x0 < l) { if (x1 < l) return 0; }
    else if (x1 < l) SWAP_ENDS();
    if (x0 < l) { y0 += (int32_t)((int64_t)(y0 - y1) * (l - x0) / (x0 - x1)); x0 = l; }
    if (y0 > b) { if (y1 > b) return 0; }
    else if (y1 > b) SWAP_ENDS();
    if (y0 > b) { x0 += (int32_t)((int64_t)(x0 - x1) * (b - y0) / (y0 - y1)); y0 = b; }
    if (x0 > r) { if (x1 > r) return 0; }
    else if (x1 > r) SWAP_ENDS();
    if (x0 > r) { y0 += (int32_t)((int64_t)(y0 - y1) * (r - x0) / (x0 - x1)); x0 = r; }
#undef SWAP_ENDS
    p[0] = x0; p[1] = y0; p[2] = x1; p[3] = y1;
    return 1;
}

static void line_colour(const es2_map_dc *dc, uint32_t colour, int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
    int32_t dx, dy, p[4];
    if (!plain(colour) || dc->clip_mode > 1) return;
    dc_offset(dc, &dx, &dy);
    p[0] = x0 + dx; p[1] = y0 + dy; p[2] = x1 + dx; p[3] = y1 + dy;
    if (dc->clip_mode == 1 && !line_clip(p, dc->left, dc->top, dc->right, dc->bottom)) return;
    line_slot34(dc->surface, (uint8_t)colour, p[0], p[1], p[2], p[3]);
}

/* Original: VSHELL.EXE 0x455624 */
void es2_map_line(const es2_map_dc *dc, int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
    if (dc) line_colour(dc, dc->pen, x0, y0, x1, y1);
}

/* Original: VSHELL.EXE 0x455f58: the polygon clipped against the
 * rectangle's x bounds, then its y bounds (64-bit products, truncating
 * division). Returns the vertex count (0: nothing left; 1: one vertex, which
 * the span builder cannot take). */
#define POLY_MAX 50
static int poly_clip(int n, const int32_t *in, int32_t *out, int32_t l, int32_t t, int32_t r, int32_t b) {
    int32_t tmp[2 * POLY_MAX];
    int m = 0, k = 0;
    if (n <= 1) return n;
    const int32_t *prev = in + 2 * (n - 1);
    int pc = prev[0] < l ? 1 : prev[0] > r ? 2 : 0;
    for (int i = 0; i < n; i++) {
        const int32_t *cur = in + 2 * i;
        int cc = cur[0] < l ? 1 : cur[0] > r ? 2 : 0;
        if (!pc && !cc) {
            tmp[2 * m] = cur[0]; tmp[2 * m + 1] = cur[1]; m++;
        } else if (!(pc & cc)) {
            if (pc) { /* from the outside: the previous vertex's boundary */
                int32_t x = pc == 1 ? l : r;
                tmp[2 * m] = x;
                tmp[2 * m + 1] = (int32_t)((int64_t)(x - prev[0]) * (cur[1] - prev[1]) / (cur[0] - prev[0])) + prev[1];
                m++;
            }
            if (cc) { /* to the outside: the current vertex's boundary */
                int32_t x = cc == 1 ? l : r;
                tmp[2 * m] = x;
                tmp[2 * m + 1] = (int32_t)((int64_t)(x - cur[0]) * (prev[1] - cur[1]) / (prev[0] - cur[0])) + cur[1];
                m++;
            } else {
                tmp[2 * m] = cur[0]; tmp[2 * m + 1] = cur[1]; m++;
            }
        }
        if (m > POLY_MAX - 2) return 0; /* own guard: the original's buffer holds 50 vertices */
        pc = cc;
        prev = cur;
    }
    if (m < 2) {
        if (!m) return 0;
        if (tmp[1] > b || tmp[1] < t) return 0;
        out[0] = tmp[0]; out[1] = tmp[1];
        return 1;
    }
    prev = tmp + 2 * (m - 1);
    pc = prev[1] > b ? 4 : prev[1] < t ? 8 : 0;
    for (int i = 0; i < m; i++) {
        const int32_t *cur = tmp + 2 * i;
        int cc = cur[1] > b ? 4 : cur[1] < t ? 8 : 0;
        if (!pc && !cc) {
            out[2 * k] = cur[0]; out[2 * k + 1] = cur[1]; k++;
        } else if (!(pc & cc)) {
            if (pc) {
                int32_t y = pc == 4 ? b : t;
                out[2 * k] = (int32_t)((int64_t)(y - prev[1]) * (cur[0] - prev[0]) / (cur[1] - prev[1])) + prev[0];
                out[2 * k + 1] = y;
                k++;
            }
            if (cc) {
                int32_t y = cc == 4 ? b : t;
                out[2 * k] = (int32_t)((int64_t)(y - cur[1]) * (prev[0] - cur[0]) / (prev[1] - cur[1])) + cur[0];
                out[2 * k + 1] = y;
                k++;
            } else {
                out[2 * k] = cur[0]; out[2 * k + 1] = cur[1]; k++;
            }
        }
        if (k > POLY_MAX - 2) return 0;
        pc = cc;
        prev = cur;
    }
    return k;
}

/* Original: VSHELL.EXE 0x456340: the span table of a polygon. Chain 1 runs
 * from the top vertex (least y, of those the greatest x) forward in index
 * order to the first vertex at the bottom row and writes the right word of
 * each row; chain 2 runs from the bottom vertex (greatest y, of those the
 * least x) forward to the first vertex at the top row and writes the left
 * word. Per edge: vertical, horizontal (chain 1 keeps the greater x, chain
 * 2 the lesser), 45 degrees, y-major (Bresenham from the left end) and
 * x-major (chain 1 from its right end stepping left, chain 2 from its left
 * end stepping right, each row getting the x where the run leaves it).
 * Returns 0 when the table would exceed `rows` rows (own guard). */
#define SPAN_ROWS 800 /* the static span buffer 0x47c4c9 holds 0xc84 bytes */
static int poly_spans(int n, const int32_t *p, int16_t *out) {
    int top = 0, bottom = 0;
    int32_t tx = p[0], ty = p[1], bx = p[0], by = p[1];
    for (int i = n - 1; i >= 1; i--) {
        int32_t x = p[2 * i], y = p[2 * i + 1];
        if (y < ty || (y == ty && x >= tx)) { top = i; tx = x; ty = y; }
    }
    for (int i = n - 1; i >= 1; i--) {
        int32_t x = p[2 * i], y = p[2 * i + 1];
        if (y > by || (y == by && x <= bx)) { bottom = i; bx = x; by = y; }
    }
    if (by == ty) {
        out[0] = (int16_t)ty;
        out[1] = 1;
        out[2] = (int16_t)p[2 * bottom];
        out[3] = (int16_t)p[2 * top];
        return 1;
    }
    if (by - ty + 1 > SPAN_ROWS) return 0;
    int32_t chain[2 * 54];
    int c1 = 0, c2 = 0, idx = top;
    for (;;) {
        if (c1 >= 54) return 0;
        chain[2 * c1] = p[2 * idx]; chain[2 * c1 + 1] = p[2 * idx + 1]; c1++;
        if (!(p[2 * idx + 1] < by)) break;
        idx = idx + 1 < n ? idx + 1 : 0;
    }
    idx = bottom;
    for (;;) {
        if (c1 + c2 >= 54) return 0;
        chain[2 * (c1 + c2)] = p[2 * idx]; chain[2 * (c1 + c2) + 1] = p[2 * idx + 1]; c2++;
        if (!(p[2 * idx + 1] > ty)) break;
        idx = idx + 1 < n ? idx + 1 : 0;
    }
    for (int pass = 0; pass < 2; pass++) {
        int first = pass ? c1 : 0, count = pass ? c2 : c1, off = pass ? 0 : 1;
        for (int e = 0; e + 1 < count; e++) {
            const int32_t *a = chain + 2 * (first + e), *z = a + 2;
            int32_t x0 = a[0], y0 = a[1], x1 = z[0], y1 = z[1];
            int32_t adx = x0 > x1 ? x0 - x1 : x1 - x0, ady = y0 > y1 ? y0 - y1 : y1 - y0;
#define ROW(y) (out + 2 + 2 * ((y) - ty) + off)
            if (!adx) {
                int32_t lo = y0 < y1 ? y0 : y1, hi = y0 < y1 ? y1 : y0;
                for (int32_t y = lo; y <= hi; y++) *ROW(y) = (int16_t)x1;
            } else if (!ady) {
                *ROW(y0) = (int16_t)(off ? (x0 > x1 ? x0 : x1) : (x0 < x1 ? x0 : x1));
            } else if (adx == ady) {
                int32_t xs = y0 < y1 ? x0 : x1, ys = y0 < y1 ? y0 : y1, xe = y0 < y1 ? x1 : x0;
                int32_t step = xs < xe ? 1 : -1;
                for (int32_t i = 0; i <= ady; i++) *ROW(ys + i) = (int16_t)(xs + i * step);
            } else if (adx < ady) {
                int32_t xs = x0, ys = y0, xe = x1, ye = y1;
                if (!(x0 < x1)) { xs = x1; ys = y1; xe = x0; ye = y0; }
                int32_t dir = ys < ye ? 1 : -1, dx = xe - xs, e = 2 * dx - ady, x = xs, y = ys;
                for (int32_t i = 0; i <= ady; i++, y += dir) {
                    *ROW(y) = (int16_t)x;
                    if (e >= 0) { x++; e += 2 * (dx - ady); }
                    else e += 2 * dx;
                }
            } else {
                int32_t xs = x0, ys = y0, ye = y1, s = off ? -1 : 1;
                if (off ? !(x0 > x1) : !(x0 < x1)) { xs = x1; ys = y1; ye = y0; }
                int32_t dir = ye >= ys ? 1 : -1, err = 2 * ady - adx, x = xs, y = ys, n2 = ady;
                *ROW(y) = (int16_t)x;
                y += dir;
                x += s;
                if (err < 0) goto inner;
                for (;;) {
                    *ROW(y) = (int16_t)x;
                    y += dir;
                    x += s;
                    if (--n2 == 0) break;
                    err += 2 * (ady - adx);
                    if (err >= 0) continue;
                inner:
                    do { x += s; err += 2 * ady; } while (err < 0);
                }
            }
#undef ROW
        }
    }
    out[0] = (int16_t)ty;
    out[1] = (int16_t)(by - ty + 1);
    return 1;
}

static void polygon_colour(const es2_map_dc *dc, int n, const int32_t *points, uint32_t colour) {
    int32_t moved[2 * POLY_MAX], clipped[2 * POLY_MAX], dx, dy;
    static int16_t spans[2 + 2 * SPAN_ROWS];
    const int32_t *p = points;
    if (!dc || n < 3 || n > 40 || !plain(colour) || dc->clip_mode > 1) return;
    if (dc->offset) {
        dc_offset(dc, &dx, &dy);
        for (int i = 0; i < n; i++) { moved[2 * i] = points[2 * i] + dx; moved[2 * i + 1] = points[2 * i + 1] + dy; }
        p = moved;
    }
    if (dc->clip_mode == 1) {
        n = poly_clip(n, p, clipped, dc->left, dc->top, dc->right, dc->bottom);
        /* one vertex left: the original span builder loops past its input
         * (0x45637d with a zero count); refused */
        if (n < 2) return;
        p = clipped;
    }
    if (poly_spans(n, p, spans)) span_fill(dc->surface, spans, (uint8_t)colour);
}

/* Original: VSHELL.EXE 0x455ad8 (brush kind 0, three or more vertices) */
void es2_map_polygon(const es2_map_dc *dc, int n, const int32_t *points) {
    if (dc && dc->brush_kind == 0) polygon_colour(dc, n, points, dc->brush);
}

/* Original: VSHELL.EXE 0x457de8 */
void es2_map_triangle(es2_map_dc *dc, const int32_t p[6], const uint16_t c[3]) {
    static int32_t lon[2 * 260], m1[2 * 260], m2[2 * 260];
    if (!dc) return;
    if ((p[0] == p[2] && p[1] == p[3]) || (p[0] == p[4] && p[1] == p[5]) || (p[4] == p[2] && p[5] == p[3])) return;
    uint16_t hi = c[0] > c[1] ? c[0] : c[1], lo = c[0] < c[1] ? c[0] : c[1];
    if (c[2] >= hi) hi = c[2];
    if (c[2] <= lo) lo = c[2];
    if (hi == lo) {
        polygon_colour(dc, 3, p, hi);
        return;
    }
    int imax = 0, imin = 0, imid = 0;
    for (int i = 0; i < 3; i++) {
        if (c[i] == hi) imax = i;
        if (c[i] == lo) imin = i;
    }
    int flag = (imax + 1) % 3 != imin;
    switch (imax + imin) { case 1: imid = 2; break; case 2: imid = 1; break; case 3: imid = 0; break; default: break; }
    uint16_t mid = c[imid];
    int32_t v0x = p[2 * imax], v0y = p[2 * imax + 1], v1x = p[2 * imid], v1y = p[2 * imid + 1];
    int32_t v2x = p[2 * imin], v2y = p[2 * imin + 1];
    int32_t d = hi - lo, a = hi - mid, bb = mid - lo, q[8];
    if (d > 255) return; /* own guard: colour words differ by at most 255 for the map */
    /* the long edge, max to min, at every colour level */
    lon[0] = v0x; lon[1] = v0y;
    for (int32_t i = 1; i < d; i++) {
        lon[2 * i] = (v2x - v0x) * i / d + v0x;
        lon[2 * i + 1] = (v2y - v0y) * i / d + v0y;
    }
    lon[2 * d] = v2x; lon[2 * d + 1] = v2y;
#define BAND(A, B, I, OFS, COLOUR)                                                         \
    do {                                                                                   \
        if (flag) {                                                                        \
            q[0] = A[2 * (I)]; q[1] = A[2 * (I) + 1]; q[2] = A[2 * (I) + 2]; q[3] = A[2 * (I) + 3]; \
            q[4] = B[2 * ((OFS) + (I)) + 2]; q[5] = B[2 * ((OFS) + (I)) + 3];               \
            q[6] = B[2 * ((OFS) + (I))]; q[7] = B[2 * ((OFS) + (I)) + 1];                   \
        } else {                                                                           \
            q[0] = B[2 * ((OFS) + (I))]; q[1] = B[2 * ((OFS) + (I)) + 1];                   \
            q[2] = B[2 * ((OFS) + (I)) + 2]; q[3] = B[2 * ((OFS) + (I)) + 3];               \
            q[4] = A[2 * (I) + 2]; q[5] = A[2 * (I) + 3]; q[6] = A[2 * (I)]; q[7] = A[2 * (I) + 1]; \
        }                                                                                  \
        polygon_colour(dc, 4, q, (uint32_t)(COLOUR));                                     \
    } while (0)
    if (a == 0) {
        /* max and mid share the top level: mid to min */
        m2[0] = v1x; m2[1] = v1y;
        for (int32_t i = 1; i < bb; i++) {
            m2[2 * i] = (v2x - v1x) * i / bb + v1x;
            m2[2 * i + 1] = (v2y - v1y) * i / bb + v1y;
        }
        m2[2 * bb] = v2x; m2[2 * bb + 1] = v2y;
        for (int32_t i = 0; i < d; i++) BAND(m2, lon, i, 0, hi - i);
    } else if (bb == 0) {
        m1[0] = v0x; m1[1] = v0y;
        for (int32_t i = 1; i < a; i++) {
            m1[2 * i] = (v1x - v0x) * i / a + v0x;
            m1[2 * i + 1] = (v1y - v0y) * i / a + v0y;
        }
        m1[2 * a] = v1x; m1[2 * a + 1] = v1y;
        for (int32_t i = 0; i < d; i++) BAND(m1, lon, i, 0, hi - i);
    } else {
        m1[0] = v0x; m1[1] = v0y;
        for (int32_t i = 1; i < a; i++) {
            m1[2 * i] = (v1x - v0x) * i / a + v0x;
            m1[2 * i + 1] = (v1y - v0y) * i / a + v0y;
        }
        m1[2 * a] = v1x; m1[2 * a + 1] = v1y;
        m2[0] = v1x; m2[1] = v1y;
        for (int32_t i = 1; i < bb; i++) {
            m2[2 * i] = (v2x - v1x) * i / bb + v1x;
            m2[2 * i + 1] = (v2y - v1y) * i / bb + v1y;
        }
        m2[2 * bb] = v2x; m2[2 * bb + 1] = v2y;
        for (int32_t i = 0; i < a; i++) BAND(m1, lon, i, 0, hi - i);
        for (int32_t i = 0; i < bb; i++) BAND(m2, lon, i, a, mid - i);
    }
#undef BAND
}

/* ---- the textured quad (0x45399c -> 0x4592a8 -> slot 33 0x45ae5c) ---------- */

/* 0x4510cc: (a << 16) / b, 64-bit dividend, truncating. */
static int32_t fixdiv(int32_t a, int32_t b) { return b ? (int32_t)(((int64_t)a * 65536) / b) : 0; }
/* 0x4510e1: (a * b) >> 16 of the 64-bit product, low 32 bits. */
static int32_t fixmul(int32_t a, int32_t b) { return (int32_t)(uint32_t)(uint64_t)(((int64_t)a * b) >> 16); }

typedef struct tex_edge {
    int32_t dir, count, idx, u, v, du, dv, x, step, sign, err, inc, dec;
} tex_edge;

typedef struct tex_poly {
    int n, bottom;
    const int32_t *xy, *uv;
} tex_poly;

/* 0x45afe8: the next edge with a nonzero height from vertex i. */
static int tex_setup(const tex_poly *t, tex_edge *w, int i) {
    for (;;) {
        if (i == t->bottom) return 0;
        int j = i + w->dir;
        if (j >= t->n) j = 0;
        else if (j < 0) j = t->n - 1;
        w->count = t->xy[2 * j + 1] - t->xy[2 * i + 1];
        if (!w->count) { i = j; continue; }
        int32_t h = (int32_t)((uint32_t)w->count << 16), d;
        w->idx = j;
        w->u = (int32_t)((uint32_t)t->uv[2 * i] << 16);
        w->v = (int32_t)((uint32_t)t->uv[2 * i + 1] << 16);
        w->du = fixdiv((int32_t)((uint32_t)t->uv[2 * j] << 16) - w->u, h);
        w->dv = fixdiv((int32_t)((uint32_t)t->uv[2 * j + 1] << 16) - w->v, h);
        w->x = t->xy[2 * i];
        d = t->xy[2 * j] - t->xy[2 * i];
        if (d < 0) {
            w->sign = -1;
            d = -d;
            w->err = 1 - w->count;
            w->step = -(d / w->count);
        } else {
            w->sign = 1;
            w->err = 0;
            w->step = d / w->count;
        }
        w->inc = d % w->count;
        w->dec = w->count;
        return 1;
    }
}

/* 0x45af88 */
static int tex_advance(const tex_poly *t, tex_edge *w) {
    if (--w->count == 0) return tex_setup(t, w, w->idx);
    w->u += w->du;
    w->v += w->dv;
    w->x += w->step;
    w->err += w->inc;
    if (w->err > 0) { w->x += w->sign; w->err -= w->dec; }
    return 1;
}

/* 0x45b118 (no remap table): one row from L to R; texels of index 0 are
 * not written. The texel address steps by the integer parts and 16-bit
 * carries of the fractions, rounded by half a texel at the start. */
static void tex_span(const es2_map_dc *dc, const tex_edge *L, const tex_edge *R, const es2_bitmap *b, int32_t y) {
    if (dc->right < L->x || dc->left > R->x) return;
    int32_t w = b->width, width = (int32_t)((uint32_t)(R->x - L->x + 1) << 16);
    int32_t du = fixdiv(R->u - L->u, width), dv = fixdiv(R->v - L->v, width), t;
    int32_t su = 1, sv = w, iu, iv;
    t = du;
    if (t < 0) { su = -1; if (t & 0xffff) t += 0x10000; }
    iu = t >> 16;
    t = dv;
    if (t < 0) { sv = -w; if (t & 0xffff) t += 0x10000; }
    iv = (t >> 16) * w;
    int32_t uu = L->u + (du >> 1), vv = L->v + (dv >> 1);
    int32_t xr = R->x < dc->right ? R->x : dc->right, xl = L->x;
    if (xl < dc->left) {
        int32_t d = (int32_t)((uint32_t)(dc->left - L->x) << 16);
        xl = dc->left;
        uu += fixmul(du, d);
        vv += fixmul(dv, d);
    }
    uint32_t vlow = ((uint32_t)vv & 0xffff) + 0x8000, ulow = ((uint32_t)uu & 0xffff) + 0x8000;
    uint32_t row = (((uint32_t)vv >> 16) & 0xffff) + (vlow >> 16);
    uint32_t col = (((uint32_t)uu >> 16) & 0xffff) + (ulow >> 16);
    uint16_t fu = (uint16_t)du, fv = (uint16_t)dv, au = (uint16_t)ulow, av = (uint16_t)vlow;
    uint32_t src = row * (uint32_t)w + col;
    size_t texels = (size_t)b->width * b->height;
    if (su < 0) { fu = (uint16_t)-fu; au = (uint16_t)~au; }
    if (sv < 0) { fv = (uint16_t)-fv; av = (uint16_t)~av; }
    for (int32_t x = xl; x <= xr; x++) {
        /* own guard: a texel outside the bitmap is not read (the original
         * reads whatever lies there) */
        uint8_t c = src < texels ? b->pixels[src] : 0;
        if (c) plot(dc->surface, x, y, c);
        uint32_t s;
        src += (uint32_t)iu;
        s = (uint32_t)au + fu;
        au = (uint16_t)s;
        if (s > 0xffff) src += (uint32_t)su;
        src += (uint32_t)iv;
        s = (uint32_t)av + fv;
        av = (uint16_t)s;
        if (s > 0xffff) src += (uint32_t)sv;
    }
}

/* Original: VSHELL.EXE 0x45399c(bitmap, &(x, y), 0, &(sx, sy), 0) */
void es2_map_textured_quad(const es2_map_dc *dc, const es2_bitmap *b, int32_t x, int32_t y, int32_t sx,
                           int32_t sy) {
    int32_t xy[8], uv[8], dx, dy;
    if (!dc || !b || !b->pixels || dc->clip_mode > 1) return;
    /* 0x4592a8: texture corners (0, 0), (w - 1, 0), (w - 1, h - 1), (0, h - 1);
     * screen corners (0, 0), (sx, 0), (sx, sy), (0, sy) rotated by angle 0
     * (0x452680 with cos 0x4000, sin 0: exact) and moved to (x, y) */
    uv[0] = 0; uv[1] = 0; uv[2] = b->width - 1; uv[3] = 0;
    uv[4] = b->width - 1; uv[5] = b->height - 1; uv[6] = 0; uv[7] = b->height - 1;
    xy[0] = 0; xy[1] = 0; xy[2] = sx; xy[3] = 0; xy[4] = sx; xy[5] = sy; xy[6] = 0; xy[7] = sy;
    dc_offset(dc, &dx, &dy);
    for (int i = 0; i < 4; i++) { xy[2 * i] += x + dx; xy[2 * i + 1] += y + dy; }
    /* slot 33 0x45ae5c */
    tex_poly t;
    int top = 0;
    int32_t ymin = 0x7fff, ymax = -0x8000;
    t.n = 4; t.bottom = 0; t.xy = xy; t.uv = uv;
    for (int i = 0; i < 4; i++) {
        if (ymin > xy[2 * i + 1]) { ymin = xy[2 * i + 1]; top = i; }
        if (ymax < xy[2 * i + 1]) { ymax = xy[2 * i + 1]; t.bottom = i; }
    }
    if (ymax <= ymin) return;
    tex_edge L, R;
    memset(&L, 0, sizeof L);
    memset(&R, 0, sizeof R);
    L.dir = -1;
    tex_setup(&t, &L, top);
    R.dir = 1;
    tex_setup(&t, &R, top);
    for (int32_t row = ymin;; row++) {
        if (dc->bottom < row || L.x > R.x) break;
        if (row >= dc->top) tex_span(dc, &L, &R, b, row);
        if (!tex_advance(&t, &L) || !tex_advance(&t, &R)) break;
    }
}

/* ---- ellipse, rectangle, bitmaps ------------------------------------------ */

/* The span table of 0x4579ac at (cx, cy): rows relative to cy, spans
 * relative to cx (16-bit), clipped per pixel in clip mode 1 (0x457c8d
 * drops rows and columns outside the rectangle, which leaves the same
 * pixels). */
static void rel_spans(const es2_map_dc *dc, const int16_t *t, int32_t cx, int32_t cy, uint8_t c) {
    int32_t dx, dy;
    dc_offset(dc, &dx, &dy);
    int16_t sx = (int16_t)(cx + dx), sy = (int16_t)(cy + dy);
    int32_t y = (int16_t)(t[0] + sy), n = (uint16_t)t[1];
    for (int32_t i = 0; i < n; i++, y++) {
        int32_t l = (int16_t)(t[2 + 2 * i] + sx), r = (int16_t)(t[3 + 2 * i] + sx);
        if (dc->clip_mode == 1) {
            if (y < dc->top || y > dc->bottom) continue;
            if (l < dc->left) l = dc->left;
            if (r > dc->right) r = dc->right;
        }
        for (int32_t x = l; x <= r; x++) plot(dc->surface, x, y, c);
    }
}

/* Original: VSHELL.EXE 0x45886c (kind-0 brush) */
void es2_map_ellipse(const es2_map_dc *dc, int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
    static int16_t table[2 + 2 * SPAN_ROWS];
    if (!dc || dc->brush_kind != 0 || !plain(dc->brush) || dc->clip_mode > 1) return;
    int32_t cx = (x0 + x1) >> 1, cy = (y0 + y1) >> 1;
    int32_t rx = x1 - x0, ry = y1 - y0;
    rx = (rx < 0 ? -rx : rx) >> 1;
    ry = (ry < 0 ? -ry : ry) >> 1;
    if (!rx || !ry) return; /* refused: the original's line uses an uninitialised pen kind */
    int32_t lo = cy - ry < 0 ? -cy : -ry;
    int32_t hi = cy + ry > dc->screen_height - 1 ? dc->screen_height - 1 - cy : ry;
    if (lo >= hi || hi - lo > SPAN_ROWS) return;
    int32_t a2 = rx * rx, b2 = ry * ry, x = 0, y = ry;
    int32_t d = b2 - a2 * ry + ((a2 < 0 ? a2 + 3 : a2) >> 2), px = 0, py = 2 * a2 * ry, e;
    int32_t top = 2, bot = 2 + 2 * (hi - lo) - 1; /* word indices: next top-row left, last row right */
    table[0] = (int16_t)lo;
    table[1] = (int16_t)(hi - lo);
#define ROWS()                                                                    \
    do {                                                                          \
        if (-y >= lo && -y < hi) { table[top] = (int16_t)-x; table[top + 1] = (int16_t)x; top += 2; } \
        if (y <= hi && y > lo) { table[bot] = (int16_t)x; table[bot - 1] = (int16_t)-x; bot -= 2; }   \
    } while (0)
    while (px < py) {
        if (d > 0) {
            ROWS();
            y--;
            py -= 2 * a2;
            d -= py;
        }
        x++;
        px += 2 * b2;
        d += b2 + px;
    }
    e = 3 * (a2 - b2);
    e = e / 2; /* sar with the carry added back for negatives: truncation */
    e = (e - (px + py)) / 2;
    d += e;
    for (; y > 0; y--) {
        ROWS();
        if (d < 0) { x++; px += 2 * b2; d += px; }
        py -= 2 * a2;
        d += a2 - py;
    }
#undef ROWS
    rel_spans(dc, table, cx, cy, (uint8_t)dc->brush);
}

/* Original: VSHELL.EXE 0x4576a4 (brush kinds 0 and 4) */
void es2_map_rect(const es2_map_dc *dc, int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
    int32_t dx, dy;
    if (!dc || dc->clip_mode > 1) return;
    if (dc->brush_kind == 4) {
        line_colour(dc, dc->brush, x0, y0, x1, y0);
        line_colour(dc, dc->brush, x0, y0, x0, y1);
        line_colour(dc, dc->brush, x1, y1, x0, y1);
        line_colour(dc, dc->brush, x1, y1, x1, y0);
        return;
    }
    if (dc->brush_kind != 0 || !plain(dc->brush)) return;
    dc_offset(dc, &dx, &dy);
    x0 += dx; x1 += dx; y0 += dy; y1 += dy;
    if (dc->clip_mode == 1) { /* 0x457914: the intersection, nothing when empty */
        if (x0 < dc->left) x0 = dc->left;
        if (x1 > dc->right) x1 = dc->right;
        if (x0 > x1) return;
        if (y0 < dc->top) y0 = dc->top;
        if (y1 > dc->bottom) y1 = dc->bottom;
        if (y0 > y1) return;
    }
    for (int32_t y = y0; y <= y1; y++)
        for (int32_t x = x0; x <= x1; x++) plot(dc->surface, x, y, (uint8_t)dc->brush);
}

static es2_frame surface_frame(const es2_map_surface *s) {
    es2_frame f;
    f.pixels = s->pixels;
    f.size = (size_t)s->stride * (size_t)s->height;
    f.width = s->width;
    f.height = s->height;
    f.stride = s->stride;
    return f;
}

/* Original: VSHELL.EXE 0x4537e8 -> 0x458ee4: the offset, then slot 26
 * (clip mode 0) or slot 28 (the context rectangle) = 0x45a09c, mirror 0. */
void es2_map_bitmap(const es2_map_dc *dc, const es2_bitmap *b, int32_t x, int32_t y) {
    int32_t dx, dy;
    if (!dc || !b || !dc->surface || dc->clip_mode > 1) return;
    es2_frame f = surface_frame(dc->surface);
    es2_rect clip = {dc->left, dc->top, dc->right, dc->bottom};
    dc_offset(dc, &dx, &dy);
    es2_draw_blit(&f, b, x + dx, y + dy, dc->clip_mode == 1 ? &clip : NULL, 0);
}

/* Original: VSHELL.EXE 0x453908 -> 0x4591b8 -> slot 29 0x45b4c0, mirror 0:
 * columns step by w / sx with the remainder w % sx carried as an error term
 * (the same for rows), clipped to the rectangle (clip mode 1) or the
 * surface; index 0 is skipped unless the bitmap is opaque. */
void es2_map_scaled(const es2_map_dc *dc, const es2_bitmap *b, int32_t x, int32_t y, int32_t sx, int32_t sy) {
    int32_t dx, dy;
    if (!dc || !b || !b->pixels || !dc->surface || dc->clip_mode > 1 || !sx || !sy) return;
    const es2_map_surface *s = dc->surface;
    dc_offset(dc, &dx, &dy);
    x += dx;
    y += dy;
    int32_t cl = 0, ct = 0, cr = s->width - 1, cb = s->height - 1;
    if (dc->clip_mode == 1) { cl = dc->left; ct = dc->top; cr = dc->right; cb = dc->bottom; }
    int32_t w = (int16_t)b->width, h = (int16_t)b->height;
    int32_t xq = w / sx, xr = w % sx, yq = (h / sy) * w, yr = h % sy;
    if (x > cr || x + sx <= cl || y > cb || y + sy <= ct) return;
    int32_t x0 = x > cl ? x : cl, x1 = x + sx - 1 < cr ? x + sx - 1 : cr;
    int32_t y0 = y > ct ? y : ct, y1 = y + sy - 1 < cb ? y + sy - 1 : cb;
    int32_t ncols = x1 - x0 + 1, nrows = y1 - y0 + 1, yerr = -sy, xerr0 = -sx;
    int64_t srow = 0;
    if (y0 > y) {
        int32_t k = y0 - y, t = k * yr;
        yerr += t % sy;
        srow += (int64_t)(t / sy) * w + (int64_t)k * yq;
    }
    if (x0 > x) {
        int32_t k = x0 - x, t = k * xr;
        srow += (int64_t)k * xq;
        xerr0 += t % sx;
        srow += t / sx;
    }
    int opaque = (b->flags & ES2_BITMAP_OPAQUE) != 0;
    size_t texels = (size_t)b->width * b->height;
    for (int32_t r = 0; r < nrows; r++) {
        int64_t src = srow;
        int32_t e = xerr0;
        for (int32_t c = 0; c < ncols; c++) {
            uint8_t v = src >= 0 && (size_t)src < texels ? b->pixels[src] : 0; /* own guard */
            if (opaque || v) plot(s, x0 + c, y0 + r, v);
            src += xq;
            e += xr;
            if (e >= 0) { src += 1; e -= sx; }
        }
        srow += yq;
        yerr += yr;
        if (yerr >= 0) { srow += w; yerr -= sy; }
    }
}

/* ---- the map object --------------------------------------------------------- */

/* Constants of the map (data words of VSHELL.EXE, vshell_mission_map.md). */
#define MAP_SHIFT 7                /* +0x10, camera +0x1a */
#define MAP_HALF_W 147             /* -ctx+0x220 = 294 sar 1 */
#define MAP_HALF_H 113             /* -ctx+0x224 = -(225 sar 1) negated */
#define MAP_CENTRE_X (ES2_MAP_LEFT + MAP_HALF_W)
#define MAP_CENTRE_Y (ES2_MAP_TOP + MAP_HALF_H)
#define MAP_BOX_MARGIN 10000       /* [0x471d34] */
#define MAP_WINDOW 100000          /* [0x471d38] */
#define MAP_SQUAD_MARGIN 250000    /* [0x471d4c] */
#define MAP_START_ALTITUDE 200000  /* [0x471988] */
#define MAP_ZOOM 50000             /* [0x47197c], [0x471980] */
#define MAP_ZOOM_MAX 1000000       /* [0x471984] */
#define MAP_STEP_MAX 100000        /* [0x471970] */
#define MAP_STEP_MIN 5000          /* [0x471974] */
#define MAP_GRID 200000            /* [0x471d54] */

static int32_t s32(const uint8_t *p) { return (int32_t)((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24); }
static void put16(uint8_t *p, int16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)((uint16_t)v >> 8); }

int es2_mission_map_zone(es2_span script) {
    return script.data && script.size >= 4 ? es2_le16s(script.data + 2) : -1;
}

void es2_mission_map_free(es2_mission_map *m) {
    if (!m) return;
    if (m->routes)
        for (int i = 0; i < m->route_count; i++) free(m->routes[i].children);
    free(m->coords);
    free(m->angles);
    free(m->routes);
    free(m->bases);
    free(m->orders);
    free(m->groups);
    free(m->image);
    memset(m, 0, sizeof *m);
}

/* 0x4207e9 (base constructor, zoom in) and 0x42813b (zoom out). */
static void step_in(es2_mission_map *m) {
    m->step = (((m->view[2] - MAP_ZOOM) >> 3) * ((MAP_STEP_MAX - MAP_STEP_MIN) >> 3) /
               ((MAP_ZOOM_MAX - MAP_ZOOM) >> 3)) * 8 + MAP_STEP_MIN;
}
static void step_out(es2_mission_map *m) {
    int32_t d = m->max_altitude - MAP_ZOOM;
    m->step = d ? (((m->view[2] - MAP_ZOOM) >> 8) * (MAP_STEP_MAX - MAP_STEP_MIN) / d) * 256 + MAP_STEP_MIN
                : m->step; /* own guard: a zero divisor faults in the original */
    if (!d) m->refused = 1;
}

/* 0x427ee5: altitude at most the overview's, x and y inside the window
 * widened by the half view at that altitude. */
static void clamp(const es2_mission_map *m, int32_t *p) {
    if (m->max_altitude < p[2]) p[2] = m->max_altitude;
    int32_t ex = (MAP_HALF_W * p[2]) >> MAP_SHIFT, ey = (MAP_HALF_H * p[2]) >> MAP_SHIFT;
    if (p[0] < ex + (m->bbox_min[0] - MAP_WINDOW)) p[0] = ex + (m->bbox_min[0] - MAP_WINDOW);
    if ((m->bbox_max[0] + MAP_WINDOW) - ex < p[0]) p[0] = (m->bbox_max[0] + MAP_WINDOW) - ex;
    if (p[1] < ey + (m->bbox_min[1] - MAP_WINDOW)) p[1] = ey + (m->bbox_min[1] - MAP_WINDOW);
    if ((m->bbox_max[1] + MAP_WINDOW) - ey < p[1]) p[1] = (m->bbox_max[1] + MAP_WINDOW) - ey;
}

/* 0x45e610 + 0x45e690 with the camera looking straight down (angles
 * 0xc000, 0, 0; hypothesis of vshell_mission_map.md section 6 confirmed in
 * vitro by the frame comparison of map-paint): centre-relative sx, sy with y
 * up. */
static int project_rel(const es2_mission_map *m, int32_t px, int32_t py, int32_t pz, int32_t *sx, int32_t *sy) {
    int32_t depth = m->camera[2] - pz;
    if (!depth) return 0;
    *sx = (int32_t)((uint32_t)(px - m->camera[0]) << MAP_SHIFT) / depth;
    *sy = (int32_t)((uint32_t)(py - m->camera[1]) << MAP_SHIFT) / depth;
    return 1;
}

/* 0x420646: + 0x45e764, the screen step about (438, 180). */
int es2_mission_map_project(const es2_mission_map *m, const int32_t p[3], int32_t *x, int32_t *y) {
    int32_t sx, sy;
    if (!m || !project_rel(m, p[0], p[1], p[2], &sx, &sy)) return 0;
    *x = MAP_CENTRE_X + sx;
    *y = MAP_CENTRE_Y - sy;
    return 1;
}

static int project_xy(es2_mission_map *m, const int32_t p[3], int32_t *x, int32_t *y) {
    if (es2_mission_map_project(m, p, x, y)) return 1;
    m->refused = 1; /* the original divides by zero */
    return 0;
}

/* The squad member word k of actor group 0 (G0 + 0x32 + 2k). */
static int16_t member(const es2_mission_map *m, int k) { return es2_le16s(m->groups + 0x32 + 2 * k); }

/* The script reader 0x424a2b: the lists of mission.md's prepared-script
 * framing, the bounding box, the group patch of the bases. */
static int read_script(es2_mission_map *m, es2_span s, int16_t header[10]) {
    size_t at = 20;
    const uint8_t *d = s.data;
#define NEED(n) do { if (s.size - at < (size_t)(n)) return -1; } while (0)
#define COUNT(v) do { NEED(2); (v) = es2_le16s(d + at); at += 2; } while (0)
    if (!d || s.size < 20) return -1;
    for (int i = 0; i < 10; i++) header[i] = es2_le16s(d + 2 * i);
    m->bbox_min[0] = m->bbox_min[1] = INT32_MAX;
    m->bbox_max[0] = m->bbox_max[1] = INT32_MIN;
    COUNT(m->coord_count);
    if (m->coord_count > 0) {
        NEED((size_t)m->coord_count * 12);
        if (!(m->coords = malloc((size_t)m->coord_count * 12))) return -1;
        for (int i = 0; i < m->coord_count; i++, at += 12) {
            int32_t *c = m->coords + 3 * i;
            c[0] = s32(d + at); c[1] = s32(d + at + 4); c[2] = s32(d + at + 8);
            if (c[0] < m->bbox_min[0]) m->bbox_min[0] = c[0];
            if (c[1] < m->bbox_min[1]) m->bbox_min[1] = c[1];
            if (c[0] > m->bbox_max[0]) m->bbox_max[0] = c[0];
            if (c[1] > m->bbox_max[1]) m->bbox_max[1] = c[1];
        }
    }
    for (int i = 0; i < 2; i++) {
        m->bbox_min[i] = (int32_t)((uint32_t)m->bbox_min[i] - MAP_BOX_MARGIN);
        m->bbox_max[i] = (int32_t)((uint32_t)m->bbox_max[i] + MAP_BOX_MARGIN);
    }
    COUNT(m->angle_count);
    if (m->angle_count > 0) {
        NEED((size_t)m->angle_count * 2);
        if (!(m->angles = malloc((size_t)m->angle_count * 2))) return -1;
        for (int i = 0; i < m->angle_count; i++, at += 2) m->angles[i] = es2_le16s(d + at);
    }
    COUNT(m->route_count);
    if (m->route_count > 0) {
        if (!(m->routes = calloc((size_t)m->route_count, sizeof *m->routes))) return -1;
        for (int i = 0; i < m->route_count; i++) {
            COUNT(m->routes[i].count);
            if (m->routes[i].count <= 0) return -1; /* asserted (line 0x15e) */
            NEED((size_t)m->routes[i].count * 2);
            if (!(m->routes[i].children = malloc((size_t)m->routes[i].count * 2))) return -1;
            for (int k = 0; k < m->routes[i].count; k++, at += 2) m->routes[i].children[k] = es2_le16s(d + at);
        }
    }
    static const unsigned skipped[3] = {6, 74, 24}; /* regions, triggers, timed triggers */
    for (int i = 0; i < 3; i++) {
        int16_t n;
        COUNT(n);
        if (n > 0) { NEED((size_t)n * skipped[i]); at += (size_t)n * skipped[i]; }
    }
    COUNT(m->mech_count);
    if (m->mech_count > 0) { NEED((size_t)m->mech_count * 134); at += (size_t)m->mech_count * 134; }
    {
        int16_t n;
        COUNT(n); /* flyers */
        if (n > 0) { NEED((size_t)n * 92); at += (size_t)n * 92; }
    }
    COUNT(m->base_count);
    if (m->base_count > 0) {
        NEED((size_t)m->base_count * 52);
        if (!(m->bases = malloc((size_t)m->base_count * 52))) return -1;
        memcpy(m->bases, d + at, (size_t)m->base_count * 52);
        at += (size_t)m->base_count * 52;
    }
    COUNT(m->order_count);
    if (m->order_count > 0) {
        NEED((size_t)m->order_count * 14);
        if (!(m->orders = malloc((size_t)m->order_count * 14))) return -1;
        memcpy(m->orders, d + at, (size_t)m->order_count * 14);
        at += (size_t)m->order_count * 14;
    }
    COUNT(m->group_count);
    if (m->group_count <= 0) return -1; /* asserted */
    NEED((size_t)m->group_count * 156);
    if (!(m->groups = malloc((size_t)m->group_count * 156))) return -1;
    memcpy(m->groups, d + at, (size_t)m->group_count * 156);
    /* the group patch 0x424f73: groups of type 2 hand their visibility,
     * word +0x6e and location to their member bases */
    for (int g = 0; g < m->group_count; g++) {
        const uint8_t *G = m->groups + 156 * g;
        for (int k = 0; k < ES2_MAP_SQUAD; k++) {
            int16_t mb = es2_le16s(G + 0x32 + 2 * k);
            if (mb < 0 || es2_le16s(G + 0x28) != 2) continue;
            if (mb >= m->base_count) { m->refused = 1; continue; } /* not range-checked by VSHELL */
            uint8_t *B = m->bases + 52 * mb;
            put16(B + 0x1a, es2_le16s(G + 0x9a));
            put16(B + 0x1c, es2_le16s(G + 0x6e));
            if (es2_le16s(G + 0x2c) != -1) put16(B + 2, es2_le16s(G + 0x2c));
            else if (es2_le16s(G + 0x30) != -1) {
                int16_t r = es2_le16s(G + 0x30);
                if (r < 0 || r >= m->route_count) { m->refused = 1; continue; }
                put16(B + 2, m->routes[r].children[0]);
            }
        }
    }
#undef COUNT
#undef NEED
    return 0;
}

/* 0x451b90: sine and cosine words of a 3Space angle from the cosine table. */
static int16_t cos_entry(const es2_span *t, int32_t i) { return es2_le16s(t->data + 2 * i); }
static int16_t cos_of(const es2_span *t, int16_t angle) {
    int32_t a = angle, i = (a >> 4) + ((a >> 3) & 1); /* sar 4, adc 0 */
    if (i < 0) i = -i;
    if (i >= 0x400) return (int16_t)-cos_entry(t, 0x800 - i);
    return cos_entry(t, i);
}
static void sincos(const es2_span *t, int16_t angle, int16_t sc[4]) {
    int16_t c = cos_of(t, angle), s = cos_of(t, (int16_t)(angle - 0x4000));
    sc[0] = c;
    sc[3] = c;
    sc[1] = s;
    sc[2] = (int16_t)-s;
}

/* 0x450c80: the 3Space angle of (x, y) from the arc-tangent table. */
static uint32_t atan_of(const es2_span *t, int32_t x, int32_t y) {
    uint32_t ax = (uint32_t)(x < 0 ? -x : x), ay = (uint32_t)(y < 0 ? -y : y), a = 0;
    /* (n << 9) as a 64-bit dividend whose low half gets d sar 1 without a
     * carry into the high half, unsigned division */
#define QUOTIENT(n, d) (uint32_t)(((uint64_t)((n) >> 23) << 32 | (uint32_t)(((n) << 9) + (uint32_t)((int32_t)(d) >> 1))) / (d))
    if (ay) {
        if ((int32_t)ay > (int32_t)ax) {
            uint32_t q = QUOTIENT(ax, ay);
            uint32_t v = q && q < ES2_MAP_ATAN_SIZE ? q * 4 + t->data[q] : 0;
            a = (uint32_t)-(int32_t)(v - 0x1000);
        } else {
            uint32_t q = QUOTIENT(ay, ax);
            a = q && q < ES2_MAP_ATAN_SIZE ? q * 4 + t->data[q] : 0;
        }
    }
#undef QUOTIENT
    if (x < 0) a = (uint32_t)-(int32_t)(a - 0x2000);
    if (y < 0) a = (uint32_t)-(int32_t)(a - 0x4000);
    return (a & 0x3fff) << 2;
}

/* 0x425404: the squad positions from the anchor, the heading and the
 * formation record of mforms.dat. */
static int place_squad(es2_mission_map *m, const es2_mission_map_inputs *in) {
    if (!in->player_mec.data || in->player_mec.size < 4) return -1;
    m->squad_count = es2_le16s(in->player_mec.data + 2);
    const uint8_t *G0 = m->groups;
    int16_t oi = es2_le16s(G0 + 0x5a);
    if (oi < 0 || oi >= m->order_count) return -1;
    const uint8_t *O = m->orders + 14 * oi;
    int16_t f = es2_le16s(O + 2);
    if (f == -1) return -1; /* asserted */
    if (!in->mforms.data || in->mforms.size < 2) return -1;
    int16_t fcount = es2_le16s(in->mforms.data);
    if (f < 0 || f >= fcount || in->mforms.size < 2 + 28 * ((size_t)f + 1)) return -1; /* asserted */
    const uint8_t *form = in->mforms.data + 2 + 28 * (size_t)f;
    /* the heading as stored (no x182), or the first route leg's direction */
    int16_t heading = 0;
    if (es2_le16s(G0 + 0x2e) >= 0) {
        int16_t ai = es2_le16s(G0 + 0x2e);
        if (ai >= m->angle_count) { m->refused = 1; }
        else heading = m->angles[ai];
    } else if (m->player_route >= 0 && m->routes[m->player_route].count >= 2) {
        const int16_t *ch = m->routes[m->player_route].children;
        if (ch[0] >= m->coord_count || ch[1] >= m->coord_count || ch[0] < 0 || ch[1] < 0) return -1;
        const int32_t *p0 = m->coords + 3 * ch[0], *p1 = m->coords + 3 * ch[1];
        heading = (int16_t)(atan_of(&in->atan, p1[0] - p0[0], p1[1] - p0[1]) + 0xc000);
    }
    int16_t sc[4];
    sincos(&in->cosine, heading, sc);
    /* the anchor */
    const int32_t *a = NULL;
    int16_t c = es2_le16s(G0 + 0x2c);
    if (c != -1) {
        if (c >= 0 && c < m->coord_count) a = m->coords + 3 * c;
    } else if (m->player_route >= 0) {
        int16_t ch = m->routes[m->player_route].children[0];
        if (ch >= 0 && ch < m->coord_count) a = m->coords + 3 * ch;
    } else if (es2_le16s(O + 4) != -1) {
        int16_t ci = es2_le16s(O + 4);
        if (ci >= 0 && ci < m->coord_count) a = m->coords + 3 * ci;
    } else if (es2_le16s(O + 6) != -1) {
        int16_t r = es2_le16s(O + 6);
        if (r >= 0 && r < m->route_count) {
            int16_t ch = m->routes[r].children[0];
            if (ch >= 0 && ch < m->coord_count) a = m->coords + 3 * ch;
        }
    } else
        return -1; /* asserted (line 0x230), anchor uninitialised */
    if (!a) return -1; /* own: an index outside the coordinates */
    /* the formation words: slots 1..7 the record's seven (x, y) pairs, slots
     * 8..11 the path buffer "data\mforms.dat" behind it on the stack (quirk);
     * slots 12 and up read uninitialised stack: refused */
    static const char path[16] = "data\\mforms.dat";
    for (int k = 0; k < ES2_MAP_SQUAD; k++) {
        int16_t mb = member(m, k);
        if (mb == -1 || mb >= m->mech_count) continue;
        int32_t *p = m->squad[k];
        p[0] = a[0]; p[1] = a[1]; p[2] = a[2];
        m->squad_set[k] = 1;
        if (k == 0) continue;
        int16_t dx, dy;
        if (k <= 7) { dx = es2_le16s(form + 4 * (k - 1)); dy = es2_le16s(form + 4 * (k - 1) + 2); }
        else if (k <= 11) { dx = es2_le16s((const uint8_t *)path + 4 * (k - 8)); dy = es2_le16s((const uint8_t *)path + 4 * (k - 8) + 2); }
        else { m->refused = 1; continue; }
        /* 0x452624: (x * m0 + y * m2 + 0x2000) sar 14, (x * m1 + y * m3 + 0x2000) sar 14, words */
        int16_t rx = (int16_t)((dx * sc[0] + dy * sc[2] + 0x2000) >> 14);
        int16_t ry = (int16_t)((dx * sc[1] + dy * sc[3] + 0x2000) >> 14);
        p[0] += rx;
        p[1] += ry;
    }
    return 0;
}

/* 0x4258a2 and 0x425935: centre and altitude of a box. */
static void view_of_box(int32_t minx, int32_t miny, int32_t maxx, int32_t maxy, int32_t out[3]) {
    int32_t dx = (int32_t)((uint32_t)maxx - (uint32_t)minx) >> 1, dy = (int32_t)((uint32_t)maxy - (uint32_t)miny) >> 1;
    int32_t ax = (int32_t)((uint32_t)dx << MAP_SHIFT) / MAP_HALF_W;
    int32_t ay = (int32_t)((uint32_t)dy << MAP_SHIFT) / (225 >> 1);
    out[0] = (int32_t)((uint32_t)dx + (uint32_t)minx);
    out[1] = (int32_t)((uint32_t)dy + (uint32_t)miny);
    out[2] = ax > ay ? ax : ay;
}

/* VSHELL's raw draw 0x465ce8 on the state 0x48110a. */
static uint16_t raw_draw(es2_shell_rng *r) {
    uint16_t x = (uint16_t)(r->words[r->a] + r->words[r->b]);
    r->words[r->b] = x;
    r->a = (uint8_t)((r->a + 1u) % 56u);
    r->b = (uint8_t)((r->b + 1u) % 56u);
    return x;
}

/* The material draws of 0x4293af (they reach only the RNG state). */
static void material_draws(const es2_mission_map_inputs *in, int c, int r, es2_shell_rng *rng) {
    int shift = 8 - in->material_level;
    if (shift < 0 || shift > 7 || rng->a >= 56 || rng->b >= 56) return; /* asserted / no state */
    uint32_t mask = (1u << shift) - 1;
    for (uint32_t y = 0; y < (1u << r); y++)
        for (uint32_t x = 0; x < (1u << c); x++) {
            if ((y & mask) || (x & mask)) continue;
            for (uint32_t i = 1; i < in->material_count; i++)
                if ((int32_t)((int16_t)raw_draw(rng) & 0xfff) < 30 * 41) break;
        }
}

/* 0x427634: the terrain image, built once per map. */
static void build_image(es2_mission_map *m, const es2_mission_map_inputs *in, int c, int r) {
    uint32_t W = 1u << c, H = 1u << r;
    int32_t s = in->cell_shift & 0x1f;
    uint8_t *T = malloc((size_t)W * W);
    if (!T) { m->refused = 1; return; }
    /* T[y][x] = 0xd1 + min(h, 127) / 5 for the cell (y << c) + x; the cell
     * of grid row gy is bitmap row 2^r - 1 - gy at (row << c) + x */
    for (uint32_t y = 0; y < W; y++)
        for (uint32_t x = 0; x < W; x++) {
            uint8_t h = 0;
            if (y < H) {
                size_t at = ((size_t)(H - 1 - y) << c) + x;
                if (at < in->grid_bytes.size) h = in->grid_bytes.data[at];
                else m->refused = 1;
            } else
                m->refused = 1; /* rows past the grid's cell array */
            if (h >= 0x80) h = 0x7f;
            T[(y << c) + x] = (uint8_t)(h / 5 + 0xd2 - 1);
        }
    int32_t x0 = (int32_t)((uint32_t)m->bbox_min[0] - MAP_WINDOW) >> s;
    int32_t y0 = (int32_t)((uint32_t)m->bbox_min[1] - MAP_WINDOW) >> s;
    int32_t x1 = (int32_t)((uint32_t)m->bbox_max[0] + MAP_WINDOW) >> s;
    int32_t y1 = (int32_t)((uint32_t)m->bbox_max[1] + MAP_WINDOW) >> s;
    int32_t ncols = x1 - x0 + 1, nrows = y1 - y0 + 1;
    if (ncols <= 0 || nrows <= 0) { free(T); m->refused = 1; return; }
    int32_t kx = 640 / ncols, ky = 400 / nrows, k = kx < ky ? kx : ky;
    int32_t w = (int16_t)(ncols * k), h = (int16_t)(nrows * k);
    if (k <= 0 || w <= 0 || h <= 0) { free(T); m->refused = 1; return; }
    m->image = calloc((size_t)w * h, 1);
    if (!m->image) { free(T); m->refused = 1; return; }
    m->image_width = (uint16_t)w;
    m->image_height = (uint16_t)h;
    es2_map_surface surface = {m->image, w, h, w};
    es2_map_dc dc;
    memset(&dc, 0, sizeof dc);
    dc.surface = &surface;
    dc.clip_mode = 1;
    dc.right = w - 1;
    dc.bottom = h - 1;
    dc.screen_height = h;
    size_t tsize = (size_t)W * W;
#define TV(i) ((i) < tsize ? T[i] : (m->refused = 1, (uint8_t)0)) /* past the buffer: refused */
    int32_t py1 = 0;
    for (int32_t y = y1; y > y0; y--) {
        int32_t py0 = py1, px1 = 0;
        py1 += k;
        for (int32_t x = x0; x < x1; x++) {
            int32_t px0 = px1;
            px1 += k;
            uint16_t t00 = 0, t01 = 0, t11 = 0, t10 = 0;
            if (!(y < 0 || (uint32_t)y >= H || x < 0 || (uint32_t)x >= W)) {
                size_t at = (size_t)x + (size_t)W * (size_t)y;
                t00 = TV(at);
                if ((uint32_t)y < W) {
                    t01 = TV(at + W);
                    t11 = TV(at + W + 1); /* x < W always holds here: the guard tests x, not x + 1 */
                    t10 = TV(at + 1);
                } else
                    t01 = t11 = t10 = t00;
            }
            int32_t a[6] = {px0, py1, px0, py0, px1, py0};
            uint16_t ca[3] = {t00, t01, t11};
            es2_map_triangle(&dc, a, ca);
            int32_t b[6] = {px0, py1, px1, py0, px1, py1};
            uint16_t cb[3] = {t00, t11, t10};
            es2_map_triangle(&dc, b, cb);
        }
    }
#undef TV
    free(T);
}

int es2_mission_map_build(es2_mission_map *m, const es2_mission_map_inputs *in, es2_mission_map_globals *g) {
    int16_t header[10];
    if (!m || !in || !g) return -1;
    es2_mission_map_free(m);
    /* the base view 0x42041c */
    m->view[0] = 0;
    m->view[1] = 0;
    m->view[2] = MAP_START_ALTITUDE;
    step_in(m);
    /* the map constructor 0x424597 */
    m->first_paint = 1;
    m->overview[2] = 100000;
    m->player_route = -1;
    m->icons = in->icons;
    m->icon_count = in->icon_count;
    m->mission_lines = in->mission_lines;
    if (!in->cosine.data || in->cosine.size < ES2_MAP_COSINE_SIZE || !in->atan.data ||
        in->atan.size < ES2_MAP_ATAN_SIZE || read_script(m, in->script, header))
        goto fail;
    /* the zone 0x42519c: its grid (0x4293af) */
    if (header[1] < 0) goto fail; /* asserted */
    int c = 0, r = 0;
    for (int32_t i = 1; i < (int16_t)in->grid_width; i *= 2) c++;
    for (int32_t i = 1; i < (int16_t)in->grid_height; i *= 2) r++;
    material_draws(in, c, r, &g->material_rng);
    /* the player route 0x4250a2: group 0's order's route */
    {
        int16_t oi = es2_le16s(m->groups + 0x5a);
        if (oi != -1) {
            if (oi < 0 || oi >= m->order_count) goto fail;
            int16_t ri = es2_le16s(m->orders + 14 * oi + 6);
            if (ri != -1) {
                if (ri < 0 || ri >= m->route_count) goto fail; /* asserted */
                m->player_route = ri;
            }
        }
    }
    if (place_squad(m, in)) goto fail;
    view_of_box(m->bbox_min[0], m->bbox_min[1], m->bbox_max[0], m->bbox_max[1], m->overview);
    m->max_altitude = m->overview[2];
    {
        int32_t lo[2] = {INT32_MAX, INT32_MAX}, hi[2] = {INT32_MIN, INT32_MIN};
        for (int k = 0; k < m->squad_count && k < ES2_MAP_SQUAD; k++) {
            if (member(m, k) == -1) continue;
            const int32_t *p = m->squad[k];
            if (p[0] < lo[0]) lo[0] = p[0];
            if (p[1] < lo[1]) lo[1] = p[1];
            if (hi[0] < p[0]) hi[0] = p[0];
            if (hi[1] < p[1]) hi[1] = p[1];
        }
        if (m->squad_count > ES2_MAP_SQUAD) m->refused = 1; /* the slots end at 20 */
        int32_t miny = (int32_t)((uint32_t)lo[1] - MAP_SQUAD_MARGIN);
        int32_t minx = (int32_t)((uint32_t)lo[0] - MAP_SQUAD_MARGIN);
        int32_t maxx = (int32_t)((uint32_t)hi[0] + MAP_SQUAD_MARGIN), maxy = (int32_t)((uint32_t)hi[1] + MAP_SQUAD_MARGIN);
        view_of_box(minx, miny, maxx, maxy, m->squad_view);
    }
    m->final_count = 2;
    build_image(m, in, c, r);
    m->built = 1;
    return 0;
fail:
    es2_mission_map_free(m);
    return -1;
}

/* ---- paint (0x425a5e) ------------------------------------------------------- */

typedef struct painter {
    es2_mission_map *m;
    es2_mission_map_globals *g;
    es2_map_surface surface;
    es2_map_dc dc;
} painter;

static void busy_wait(painter *p, uint32_t ticks) {
    p->g->tick += ticks;
    p->m->wait_ticks += ticks;
}

static const es2_bitmap *icon(const es2_mission_map *m, int frame) {
    return frame >= 0 && (size_t)frame < m->icon_count && m->icons && m->icons[frame].pixels ? &m->icons[frame] : NULL;
}

/* 0x4272e0: a dot, the scaled icon or the icon 1:1 by its size s at the
 * current altitude. */
static void draw_icon(painter *p, const int32_t P[3], int32_t D, int16_t half, int16_t dot, uint8_t index,
                      const es2_bitmap *b) {
    int32_t x, y;
    if (!project_xy(p->m, P, &x, &y)) return;
    int32_t z = p->m->view[2];
    if (!z) { p->m->refused = 1; return; }
    int32_t s = (int32_t)((uint32_t)D << MAP_SHIFT) / z;
    p->dc.offset = 0;
    if (dot > s) {
        p->dc.pen = index;
        es2_map_pixel(&p->dc, x, y);
    } else if (!b) {
        p->m->refused = 1; /* a missing frame: the original dereferences it */
    } else if ((int16_t)b->height > s) {
        es2_map_scaled(&p->dc, b, x - (s >> 1), y - (s >> 1), s, s);
    } else {
        es2_map_bitmap(&p->dc, b, x - (half >> 1), y - (half >> 1));
    }
}

static void paint_terrain(painter *p) {
    es2_mission_map *m = p->m;
    p->dc.offset = 0;
    if (m->image) {
        int32_t a[3] = {m->bbox_min[0] - MAP_WINDOW, m->bbox_max[1] + MAP_WINDOW, 0};
        int32_t b[3] = {m->bbox_max[0] + MAP_WINDOW, m->bbox_min[1] - MAP_WINDOW, 0};
        int32_t x1, y1, x2, y2;
        es2_bitmap img = {m->image_height, m->image_width, ES2_BITMAP_OPAQUE, m->image};
        /* the rotation by angle 0 about the camera's ground point is exact */
        if (project_xy(m, a, &x1, &y1) && project_xy(m, b, &x2, &y2))
            es2_map_textured_quad(&p->dc, &img, x1, y1, x2 - x1, y2 - y1);
    } else
        m->refused = 1; /* no image: the original takes the 3D terrain branch */
}

static void paint_grid(painter *p) {
    es2_mission_map *m = p->m;
    int32_t ox, oy, qx, qy;
    p->dc.offset = 1;
    if (!project_rel(m, 0, 0, 0, &ox, &oy) || !project_rel(m, MAP_GRID << 4, MAP_GRID << 4, 0, &qx, &qy)) {
        m->refused = 1;
        return;
    }
    oy = -oy;
    m->grid_spacing = qx - ox;
    if (m->grid_spacing < 0) m->grid_spacing = -m->grid_spacing;
    int32_t d = m->grid_spacing;
    p->dc.pen = 15;
    int32_t x = ox;
    for (int32_t i = 0; x != MAP_HALF_W && x <= MAP_HALF_W;) {
        es2_map_line(&p->dc, x, MAP_HALF_H, x, -MAP_HALF_H);
        i++;
        x = ((i * d) >> 4) + ox;
        if (m->first_paint) busy_wait(p, 1);
    }
    x = ox - (d >> 4);
    for (int32_t i = -1; -MAP_HALF_W < x;) {
        es2_map_line(&p->dc, x, MAP_HALF_H, x, -MAP_HALF_H);
        i--;
        x = ((i * d) >> 4) + ox;
        if (m->first_paint) busy_wait(p, 1);
    }
    int32_t y = oy;
    for (int32_t i = 0; y != MAP_HALF_H && y <= MAP_HALF_H;) {
        es2_map_line(&p->dc, MAP_HALF_W, y, -MAP_HALF_W, y);
        i++;
        y = ((i * d) >> 4) + oy;
        if (m->first_paint) busy_wait(p, 1);
    }
    y = oy - (d >> 4);
    for (int32_t i = -1; -MAP_HALF_H < y;) {
        es2_map_line(&p->dc, MAP_HALF_W, y, -MAP_HALF_W, y);
        i--;
        y = ((i * d) >> 4) + oy;
        if (m->first_paint) busy_wait(p, 1);
    }
    if (!m->first_paint) {
        int32_t ax, ay, bx, by;
        if (!project_rel(m, m->bbox_min[0], m->bbox_min[1], 0, &ax, &ay) ||
            !project_rel(m, m->bbox_max[0], m->bbox_max[1], 0, &bx, &by)) {
            m->refused = 1;
            return;
        }
        p->dc.brush_kind = 4;
        p->dc.brush = 10;
        es2_map_rect(&p->dc, ax, -ay, bx, -by);
    }
}

static const int32_t *coord(es2_mission_map *m, int16_t i) {
    if (i >= 0 && i < m->coord_count) return m->coords + 3 * i;
    m->refused = 1; /* asserted, then read outside the coordinates */
    return NULL;
}

static void paint_route(painter *p) {
    es2_mission_map *m = p->m;
    if (m->player_route < 0 || !m->route_shown) return;
    const es2_mission_map_route *rt = &m->routes[m->player_route];
    p->dc.offset = 0;
    p->dc.pen = 14;
    int32_t x0, y0, x1, y1;
    const int32_t *c;
    if (m->route_shown == (uint16_t)rt->count) {
        if (!(c = coord(m, rt->children[0])) || !project_xy(m, c, &x1, &y1)) return;
        for (int i = 1; i < rt->count && i < m->route_shown; i++) {
            x0 = x1;
            y0 = y1;
            if (!(c = coord(m, rt->children[i])) || !project_xy(m, c, &x1, &y1)) return;
            es2_map_line(&p->dc, x0, y0, x1, y1);
        }
    } else {
        int i = m->route_shown - 1;
        if (i >= rt->count) { m->refused = 1; return; }
        int32_t gp[3] = {m->view[0], m->view[1], 0};
        if (!(c = coord(m, rt->children[i])) || !project_xy(m, c, &x0, &y0) || !project_xy(m, gp, &x1, &y1)) return;
        p->dc.brush_kind = 0;
        p->dc.brush = 14;
        es2_map_line(&p->dc, x0, y0, x1, y1);
        es2_map_ellipse(&p->dc, x1 - 3, y1 - 3, x1 + 3, y1 + 3);
    }
}

static void paint_bases(painter *p) {
    es2_mission_map *m = p->m;
    for (int i = 0; i < m->base_count; i++) {
        const uint8_t *B = m->bases + 52 * i;
        int16_t t = es2_le16s(B);
        int side = !(t < 24 || (t > 44 && t < 55));
        int frame;
        uint8_t index;
        int16_t half, dot;
        if ((t >= 4 && t <= 6) || (t >= 8 && t <= 11) || t == 13 || t == 14 || t == 16 || (t >= 18 && t <= 20)) {
            frame = 11; index = 10; half = 10; dot = 5;          /* class A, [0x471d78] */
        } else if (t == 26 || (t >= 28 && t <= 30) || (t >= 32 && t <= 35) || t == 37 || t == 39 || t == 40) {
            frame = 9; index = 33; half = 10; dot = 5;           /* class B, [0x471d76] */
        } else if (t >= 45 && t <= 54) {
            frame = 24; index = 10; half = 5; dot = 3;           /* class C, [0x471d86] */
        } else if (t >= 55 && t <= 64) {
            frame = 25; index = 33; half = 5; dot = 3;           /* class D, [0x471d84] */
        } else if (side) {
            frame = 8; index = 33; half = 16; dot = 8;           /* [0x471d90] */
        } else {
            frame = 10; index = 10; half = 16; dot = 8;          /* [0x471d92] */
        }
        int16_t at = es2_le16s(B + 2), visible = es2_le16s(B + 0x1a);
        if (!((!side && visible != 0) || (side && visible == 1))) continue;
        const int32_t *c = coord(m, at); /* -1: "Base %d has no loc." */
        if (!c) continue;
        draw_icon(p, c, 50000, half, dot, index, icon(m, frame));
    }
}

static void paint_markers(painter *p) {
    es2_mission_map *m = p->m;
    if (m->player_route < 0 || !m->markers_shown) return;
    const es2_mission_map_route *rt = &m->routes[m->player_route];
    int n = rt->count - 1;
    if (n > 22 - 14 + 1) n = 22 - 14 + 1;
    p->dc.offset = 0;
    for (int i = 0; i < n && i < m->markers_shown; i++) {
        const int32_t *c = coord(m, rt->children[i + 1]);
        int32_t x, y;
        if (!c || !project_xy(m, c, &x, &y)) continue;
        const es2_bitmap *b = icon(m, 14 + i);
        if (b) es2_map_bitmap(&p->dc, b, x - (13 >> 1), y - (13 >> 1));
        else m->refused = 1;
    }
}

static void paint_squad(painter *p) {
    es2_mission_map *m = p->m;
    if (!m->squad_shown) return;
    p->dc.pen = 13;
    p->dc.offset = 0;
    for (int k = 0, drawn = 0; k < ES2_MAP_SQUAD && drawn < m->squad_shown; k++) {
        int16_t mb = member(m, k);
        if (mb == -1) continue;
        if (!m->squad_set[k]) m->refused = 1; /* a member past the mech list: never placed */
        draw_icon(p, m->squad[k], 100000, 10, 5, 33, icon(m, mb == 0 ? 3 : 5));
        drawn++;
    }
}

void es2_mission_map_paint(es2_mission_map *m, es2_mission_map_globals *g, uint8_t *frame) {
    painter p;
    if (!m || !m->built || !g || !frame) return;
    memset(&p, 0, sizeof p);
    p.m = m;
    p.g = g;
    p.surface.pixels = frame;
    p.surface.width = ES2_FRAME_WIDTH;
    p.surface.height = ES2_FRAME_HEIGHT;
    p.surface.stride = ES2_FRAME_WIDTH;
    p.dc.surface = &p.surface;
    p.dc.clip_mode = 1;
    p.dc.left = ES2_MAP_LEFT;
    p.dc.top = ES2_MAP_TOP;
    p.dc.right = ES2_MAP_RIGHT;
    p.dc.bottom = ES2_MAP_BOTTOM;
    p.dc.origin_x = -MAP_HALF_W;
    p.dc.origin_y = -MAP_HALF_H;
    p.dc.screen_height = 400; /* the screen rebound at 640 x 400 by the map build */
    /* the camera */
    if (m->state == 0x13) {
        int32_t q[3] = {m->view[0] + m->pan[0], m->view[1] + m->pan[1], m->view[2]};
        clamp(m, q);
        memcpy(m->camera, q, sizeof q);
        m->view[2] = q[2];
        m->view[0] = q[0] - m->pan[0];
        m->view[1] = q[1] - m->pan[1];
    } else {
        int32_t saved[3];
        memcpy(saved, m->view, sizeof saved);
        clamp(m, m->view);
        m->camera[0] = m->view[0] + m->pan[0];
        m->camera[1] = m->view[1] + m->pan[1];
        m->camera[2] = m->view[2];
        memcpy(m->view, saved, sizeof saved);
    }
    /* 1: the background */
    p.dc.offset = 1;
    p.dc.brush_kind = 0;
    p.dc.brush = 16;
    es2_map_rect(&p.dc, -MAP_HALF_W, -MAP_HALF_H, -MAP_HALF_W + 294, -MAP_HALF_H + 225);
    if (m->first_paint) busy_wait(&p, 10);
    paint_terrain(&p);
    if (m->first_paint) busy_wait(&p, 10);
    paint_grid(&p);
    paint_route(&p);
    paint_bases(&p);
    paint_markers(&p);
    paint_squad(&p);
}

/* ---- the fly-over (0x4262cf) ------------------------------------------------ */

static void lerp_setup(es2_mission_map *m, es2_mission_map_globals *g, const int32_t target[3], int32_t n) {
    memcpy(g->start, m->view, sizeof g->start);
    memcpy(g->target, target, sizeof g->target);
    for (int i = 0; i < 3; i++) g->delta[i] = n ? (g->target[i] - g->start[i]) / n : 0;
}

/* Position start + delta * (n - (timer - T)) while T < timer; 1 at the end. */
static int lerp(es2_mission_map *m, es2_mission_map_globals *g, int32_t n) {
    if (g->tick < m->timer) {
        int32_t k = n - (int32_t)(m->timer - g->tick);
        m->view[2] = g->start[2] + g->delta[2] * k;
        m->view[0] = g->start[0] + g->delta[0] * k;
        m->view[1] = g->start[1] + g->delta[1] * k;
        return 0;
    }
    m->view[2] = g->target[2];
    m->view[0] = g->target[0];
    m->view[1] = g->target[1];
    return 1;
}

static int16_t route_count(const es2_mission_map *m) {
    return m->player_route >= 0 ? m->routes[m->player_route].count : 0;
}

int es2_mission_map_step(es2_mission_map *m, es2_mission_map_globals *g, uint32_t now, uint8_t *frame) {
    if (!m || !m->built || !g) return 0;
    g->tick = now;
    m->wait_ticks = 0;
#define PAINT() es2_mission_map_paint(m, g, frame)
    switch (m->state) {
    case 0:
        if (m->timer == 0) {
            m->timer = g->tick + 60;
            m->view[2] = m->overview[2];
            m->view[0] = m->overview[0];
            m->view[1] = m->overview[1];
            PAINT(); /* with the first-paint flag (the page toggle around it is empty) */
            m->first_paint = 0;
            PAINT();
            break;
        }
        if (g->tick < m->timer) { PAINT(); break; }
        m->state = 1;
        /* fall through */
    case 1:
        m->timer += 60;
        lerp_setup(m, g, m->squad_view, 60);
        m->state = 2;
        /* fall through */
    case 2:
        if (lerp(m, g, 60)) { PAINT(); m->state = 4; }
        else PAINT();
        break;
    case 3:
        if (g->tick < m->timer) { PAINT(); break; }
        m->squad_shown++;
        /* fall through */
    case 4:
        PAINT();
        if ((int32_t)m->squad_shown < m->squad_count) {
            m->timer = g->tick + 15;
            m->state = 3;
        } else {
            g->next_child = 1;
            m->line = 0;
            g->cursor = m->player_route >= 0 ? 0 : -1;
            m->next_state = 5;
            m->timer = g->tick + 15;
            m->state = 0xf;
        }
        break;
    case 5:
        if (m->line >= m->mission_lines) goto state6;
        if (m->player_route < 0 || g->next_child < route_count(m)) {
            m->timer = g->tick + 0;
            m->next_state = 6;
            m->state = 0xd;
            PAINT();
            break;
        }
        m->line--;
        g->text_flag = 1;
        /* fall through */
    case 0x10:
        if (m->line < m->mission_lines) {
            if (!g->text_flag) {
                m->line++;
                m->next_state = 0x10;
                m->state = 0xc;
                g->text_flag = 1;
            } else {
                m->timer = g->tick + 0;
                m->next_state = 0x10;
                m->state = 0xe;
                g->text_flag = 0;
            }
        } else
            m->state = 5;
        break;
    case 6:
    state6:
        m->route_shown++;
        m->markers_shown++;
        if (g->cursor >= 0) g->cursor++;
        g->next_child++;
        if (m->player_route < 0 || route_count(m) < g->next_child) { m->state = 0x11; break; }
        {
            const es2_mission_map_route *rt = &m->routes[m->player_route];
            const int32_t *c = g->cursor < rt->count ? coord(m, rt->children[g->cursor]) : NULL;
            if (!c) { m->refused = 1; m->state = 0x11; break; }
            int32_t target[3] = {c[0], c[1], m->view[2]};
            int32_t dx = m->view[0] - target[0], dy = m->view[1] - target[1], dz = 0;
            /* 0x450f12: max + mid/4 + mid/16 + mid/32 + min/4 of the absolute values */
            int32_t v[3] = {dx < 0 ? -dx : dx, dy < 0 ? -dy : dy, dz}, t;
            if (v[0] < v[1]) { t = v[0]; v[0] = v[1]; v[1] = t; }
            if (v[1] < v[2]) { t = v[1]; v[1] = v[2]; v[2] = t; if (v[0] < v[1]) { t = v[0]; v[0] = v[1]; v[1] = t; } }
            int32_t len = (v[2] >> 2) + v[0] + (v[1] >> 2) + (v[1] >> 4) + (v[1] >> 5);
            g->leg = (len / 50000 + 1) * 15;
            lerp_setup(m, g, target, g->leg);
            m->timer = g->tick + (uint32_t)g->leg;
            m->state = 7;
        }
        /* fall through */
    case 7:
        if (!lerp(m, g, g->leg)) { PAINT(); break; }
        PAINT();
        m->state = 8;
        /* fall through */
    case 8:
        /* strlen(line) * [0x471db4] (= 0) - the leg is never positive */
        /* fall through */
    case 9:
        m->line++;
        if (m->line < m->mission_lines) {
            m->timer = g->tick + 0;
            m->next_state = 10;
            m->state = 0xe;
        } else if (m->line == m->mission_lines && m->player_route >= 0 && g->next_child < route_count(m)) {
            m->line--;
            m->state = 5;
        } else {
            m->timer = g->tick + 15;
            m->next_state = 10;
            m->state = 0xf;
        }
        break;
    case 10:
        if (m->line < m->mission_lines) {
            m->next_state = 0xb;
            m->state = 0xc;
            break;
        }
        /* fall through */
    case 0xb:
        if (m->line < m->mission_lines) {
            m->line++;
            if (m->line < m->mission_lines - 1) {
                m->timer = g->tick + 0;
                m->next_state = 5;
                m->state = 0xe;
            } else
                m->state = 5;
        } else
            m->state = 5;
        break;
    case 0xc:
        if (m->line < m->mission_lines) {
            m->timer = g->tick + 0; /* strlen * 0 */
            m->state = 0xd;
        }
        /* fall through */
    case 0xd:
    case 0xe:
    case 0xf:
        if (g->tick < m->timer) PAINT();
        else m->state = m->next_state;
        break;
    case 0x11:
        m->first_paint = 0;
        m->squad_shown = (uint8_t)m->squad_count;
        if (m->player_route >= 0) {
            m->route_shown = (uint8_t)route_count(m);
            m->markers_shown = (uint8_t)(m->route_shown - 1);
        }
        m->timer = g->tick + 60;
        lerp_setup(m, g, m->overview, 60);
        m->state = 0x12;
        /* fall through */
    case 0x12:
        if (lerp(m, g, 60)) m->state = 0x13;
        PAINT();
        break;
    case 0x13:
        PAINT();
        if (!m->done && --m->final_count == 0) m->done = 1;
        break;
    default:
        break;
    }
#undef PAINT
    return !m->done;
}

/* Original: VSHELL.EXE 0x425be3..0x425c04 (and its siblings): while target >
 * [0x471dc4] (unsigned, ja / jb), [0x471dc4] := 0x465d5c(). */
uint32_t es2_mission_map_wait(uint32_t target, uint32_t (*clock)(void *context), void *context) {
    uint32_t reads = 0;
    if (!clock) return 0;
    do reads++;
    while (target > clock(context) / 16u);
    return reads;
}

void es2_mission_map_skip(es2_mission_map *m) {
    if (m && (int16_t)m->state <= 0x11) m->state = 0x11;
}

int es2_mission_map_arrow_steps(int32_t repeat) {
    return repeat < 3 ? 1 : repeat < 6 ? 2 : repeat < 9 ? 3 : 4;
}

void es2_mission_map_arrow_step(es2_mission_map *m, es2_mission_map_globals *g, es2_mission_map_arrow a,
                                uint8_t *frame) {
    if (!m || !m->built) return;
    m->wait_ticks = 0;
    int32_t q[3] = {m->view[0], m->view[1], m->view[2]};
    switch (a) {
    case ES2_MAP_NORTH: case ES2_MAP_SOUTH: case ES2_MAP_WEST: case ES2_MAP_EAST: {
        int axis = a == ES2_MAP_WEST || a == ES2_MAP_EAST ? 0 : 1;
        int32_t sign = a == ES2_MAP_NORTH || a == ES2_MAP_EAST ? 1 : -1;
        q[0] += m->pan[0];
        q[1] += m->pan[1];
        q[axis] += sign * m->step;
        int32_t before = q[axis];
        clamp(m, q);
        if (q[axis] == before) m->pan[axis] += sign * m->step;
        break;
    }
    case ES2_MAP_ZOOM_IN: /* 0x420738 */
        if (m->view[2] - MAP_ZOOM >= MAP_ZOOM) {
            m->view[2] -= MAP_ZOOM;
            step_in(m);
        }
        break;
    case ES2_MAP_ZOOM_OUT: /* 0x427f9a */
        q[2] += MAP_ZOOM;
        {
            int32_t before = q[2];
            clamp(m, q);
            if (q[2] == before) {
                m->view[2] += MAP_ZOOM;
                step_out(m);
            }
        }
        break;
    }
    es2_mission_map_paint(m, g, frame);
}
