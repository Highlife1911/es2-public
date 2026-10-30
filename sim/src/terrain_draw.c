/* The terrain renderer as own code (es2/sim/terrain_draw.h). Contract:
 * terrain.md, "the terrain renderer as own code" (every store and call below
 * with its listing address range); "listing" = llvm-objdump -d -M intel of the
 * reference DBSIM.EXE. Internal callees of the original are own functions here;
 * hooks, other packages' services, the fills and the grid's cell object
 * callback are called through the dispatch over the bound main loop's call
 * area. Guest state is read and written only through the guest view.
 *
 * Nothing here writes pixels: the module's state is the eye 0x6b4f44..4c,
 * the walk's eye cell 0x6b4f50/0x6b4f54 and flag 0x6b4f5c, the corner
 * quad 0x6b4fd4, the triangles 0x6b5004/0x6b5034, the texture coordinates
 * 0x6b4f7c..0x6b4fd3, the shade dwords 0x6b4f6c, the far flag 0x49f250,
 * the grid's view polygon, step vectors and visible byte, the height
 * cache entries and the walkers' frames. Where the original faults (a
 * corner cell outside the grid) the call is refused with the faulting
 * address. */
#include <stdint.h>
#include <string.h>
#include "es2/sim/terrain_draw.h"
#include "es2/sim/memory.h"
#include "es2/sim/services.h"

/* ---- guest VAs */
#define VA_EYE 0x006b4f44u              /* the camera position (3 dwords), from 0x46cb9c */
#define VA_EYE_CELL 0x006b4f50u         /* the eye's cell x, y (0x46d1a8) */
#define VA_EYE_FLAG 0x006b4f5cu         /* byte: the eye cell's mark, cleared when a line passes it */
#define VA_SHADES 0x006b4f6cu           /* 4 dwords: the shade words for the textured fills */
#define VA_UV 0x006b4f7cu               /* 8 dwords: cell_uv's output, the quad's corners */
#define VA_UV1 0x006b4f9cu              /* the first polygon's coordinates */
#define VA_UV2 0x006b4fbcu              /* the second triangle's */
#define VA_QUAD 0x006b4fd4u             /* the cell's four corners (4 x 3 dwords) */
#define VA_TRI1 0x006b5004u             /* the first polygon (a quad or a triangle) */
#define VA_TRI2 0x006b5034u             /* the second triangle */
#define VA_FAR_FLAG 0x0049f250u         /* the cell's depth above 25000 */
#define VA_SIMPLE 0x0049f248u           /* nonzero: the row walker's simple order */
#define VA_SPLIT_WALK 0x0049aad4u       /* zero: the simple order as well */
#define VA_NEAR 0x0049aad0u             /* twice this above the depth: the near fill 0x468760 */
#define VA_FILL_MODE 0x004aaaa8u        /* 0: flat (0x46bc44), 1: textured, other: none */
#define VA_SHADE_MODE 0x004aaaacu       /* the fills' shade mode */
#define VA_CONTEXT 0x006c5f70u          /* the graphics context; +0x22c saved around the walk */
#define VA_VIEW_MATRIX 0x006c5fb4u
#define VA_FAR_TABLE 0x004a0bb4u        /* words: the far distance per detail level */
#define VA_DETAIL 0x004d1f3fu           /* byte: the detail level */
/* callees (dispatch) */
#define VA_OBJECT_SELECT 0x0048c444u    /* service */
#define VA_DRAW_DISTANCES 0x00428bc0u   /* the scene's */
#define VA_VECTOR_NEW 0x0049667cu       /* service */
#define VA_EMPTY_CTOR 0x0049545bu
#define VA_VECTOR_CTOR 0x004953c4u
#define VA_HEIGHT_SET 0x004952e8u       /* services */
#define VA_HEIGHT_RESET 0x004952fcu
#define VA_TRANSFORM_POINT 0x0048043cu  /* hooks */
#define VA_ROTATE_POINT 0x00480304u
#define VA_POLY_CLIP 0x00472bccu
#define VA_POLY_SPANS 0x00472b14u
#define VA_VIEW_RECT 0x0046d324u
#define VA_CELL_UV 0x0046bdf8u
#define VA_CELL_FLAG_SET 0x00470ddcu
#define VA_CELL_FLAG_GET 0x00470e00u
#define VA_SET_FAR 0x004680e0u          /* the fills */
#define VA_HAZE 0x004680f0u
#define VA_FILL_FLAT 0x0046bc44u
#define VA_FILL_NEAR 0x00468760u
#define VA_FILL_TEXTURED 0x0046817cu
#define VA_FRUSTUM_NEW 0x004949a4u      /* the frustum library (open) */
#define VA_FRUSTUM_PLACE 0x004949e4u
#define VA_FRUSTUM_CLIP 0x00494e58u
#define VA_ROW_CELLS 0x0046dddcu        /* own rows: the walkers' callbacks */
#define VA_COLUMN_CELLS 0x0046dfa8u

static es2_sim_terrain_draw *bound_terrain, own_terrain;
void es2_sim_terrain_draw_bind(es2_sim_terrain_draw *terrain) { bound_terrain = terrain; }

/* ---- guest view and calls */
typedef struct ctx { es2_sim_terrain_draw *t; es2_sim_loop *l; const es2_guest_memory *m; } ctx;
static uint32_t r32(const ctx *c, uint32_t a) { return c->m->read32(c->m->context, a); }
static uint16_t r16(const ctx *c, uint32_t a) { return c->m->read16(c->m->context, a); }
static uint8_t r8(const ctx *c, uint32_t a) { return es2_sim_read8(c->m, a); }
static void w32(const ctx *c, uint32_t a, uint32_t v) { c->m->write32(c->m->context, a, v); }
static void w16(const ctx *c, uint32_t a, uint16_t v) { c->m->write16(c->m->context, a, v); }
static void w8(const ctx *c, uint32_t a, uint8_t v) { es2_sim_write8(c->m, a, v); }
static int32_t s32(const ctx *c, uint32_t a) { return (int32_t)r32(c, a); }
static int32_t sw(const ctx *c, uint32_t a) { return (int32_t)(int16_t)r16(c, a); }
static int refuse(ctx *c, uint32_t va, const char *name, const char *reason) {
    c->t->refused_va = va;
    return es2_sim_loop_refuse(c->l, va, name, reason);
}
/* The original reads, writes or calls through NULL at va (es2_sim_loop_null_refuse). */
static int null_refuse(ctx *c, uint32_t va, const char *name, const char *reason) {
    c->t->refused_va = va;
    return es2_sim_loop_null_refuse(c->l, va, name, reason);
}
static int call(ctx *c, uint32_t va, const uint32_t *slots, unsigned count, uint32_t *eax) {
    es2_sim_loop *l = c->l;
    uint32_t bytes = 4u * (count ? count : 1u), top = l->stack_hi;
    if (top - l->stack_lo < bytes) return refuse(c, va, NULL, "the own code's call area is exhausted");
    l->stack_hi -= bytes;
    for (unsigned i = 0; i < count; ++i) w32(c, l->stack_hi + 4u * i, slots[i]);
    uint32_t zero[3] = {0, 0, 0};
    es2_sim_result r;
    memset(&r, 0, sizeof(r));
    int refused = es2_sim_call(l->dispatch, va, c->m->host(c->m->context, l->stack_hi), count, zero, &r);
    l->stack_hi = top;
    if (refused) {
        c->t->refused_va = va;
        if (l->outcome == ES2_SIM_LOOP_EXITED) return 1;   /* the process ended inside */
        l->outcome = ES2_SIM_LOOP_REFUSED;
        l->refused_va = va;
        return 1;
    }
    if (eax) *eax = r.eax;
    return 0;
}
#define CALL(c, va, eax, ...) call((c), (va), (const uint32_t[]){__VA_ARGS__}, \
    (unsigned)(sizeof((const uint32_t[]){__VA_ARGS__}) / sizeof(uint32_t)), (eax))
/* A zero-filled block of the call area for the original's stack locals:
 * the original's EBP-relative local -o is at frame + size - o. */
static uint32_t frame_push(ctx *c, uint32_t bytes) {
    bytes = (bytes + 3u) & ~3u;
    if (c->l->stack_hi - c->l->stack_lo < bytes + 0x400u) return 0;
    c->l->stack_hi -= bytes;
    for (uint32_t i = 0; i < bytes; i += 4u) w32(c, c->l->stack_hi + i, 0);
    return c->l->stack_hi;
}
static void frame_pop(ctx *c, uint32_t top) { c->l->stack_hi = top; }
/* SHL/SAR r32, CL: the count masked to five bits like the processor. */
static uint32_t shl(uint32_t v, uint32_t n) { return v << (n & 31u); }
static uint32_t sar(uint32_t v, uint32_t n) {
    n &= 31u;
    int32_t s = (int32_t)v;
    return (uint32_t)(s < 0 ? ~(~s >> n) : s >> n);
}
/* The low dword of a product (IMUL r32, r/m32 and the low half of the one-operand IMUL). */
static uint32_t mul32(uint32_t a, uint32_t b) { return a * b; }

static int serving(es2_sim_service_call *sc, ctx *c) {
    c->t = bound_terrain ? bound_terrain : &own_terrain;
    if (!c->t->loop) c->t->loop = es2_sim_loop_bound();
    if (!c->t->loop) { sc->refused = 1; sc->reason = "no main loop bound"; return 1; }
    c->l = c->t->loop;
    c->m = c->l->memory;
    ++c->t->calls;
    return 0;
}
static void failed(es2_sim_service_call *sc) { sc->refused = 1; sc->reason = "a call inside the service was refused"; }
#define SERVE(sc, c) ctx c; if (serving((sc), &c)) return
#define A(n) (sc->slots[n])

/* Three dwords in the original's order (+8, +0, +4). */
static void copy3(const ctx *c, uint32_t d, uint32_t s) {
    w32(c, d + 8u, r32(c, s + 8u));
    w32(c, d, r32(c, s));
    w32(c, d + 4u, r32(c, s + 4u));
}

/* ======================================================================
 * Grid leaves. The grid G: +0xec the cells (16 bytes: +0 the height byte,
 * +1 and +7 the two triangles' normal words, +0xd/+0xe their shades, +0xf
 * the texture byte), +0xf0 the marks, +0x100/+0x104 log2 of the width and
 * height, +0x108 the cell shift. */
/* The cell of a grid index, or 0 outside 0 <= index < 1 << (w + h) (signed
 * compares; listing 0x46d46d..0x46d497 and its copies). */
static uint32_t cell_of(const ctx *c, uint32_t g, uint32_t index) {
    int32_t limit = (int32_t)shl(1u, r32(c, g + 0x100u) + r32(c, g + 0x104u));
    if ((int32_t)index < 0 || (int32_t)index >= limit) return 0;
    return r32(c, g + 0xecu) + (index << 4);
}
static uint32_t cell_index(const ctx *c, uint32_t g, uint32_t x, uint32_t y) { return shl(y, r32(c, g + 0x100u)) + x; }

/* 0x470d94(G, cell): the mark byte of the cell (listing 0x470d94..0x470db4). */
static uint32_t cell_mark(const ctx *c, uint32_t g, uint32_t cell) {
    return r8(c, r32(c, g + 0xf0u) + ((cell - r32(c, g + 0xecu)) >> 4));
}

/* 0x49534c(H, h): the height cache's entry for the height byte h, built at
 * the first use (listing 0x49534c..0x4953c3): v = h * [H+4] + [H+8]; the
 * entry is the vector [H+0x18] scaled by v (0x47e08d: the low dword of the
 * 64-bit product shifted right 16) and its flag byte [H+0x10]+h set. */
static uint32_t fix_mul(uint32_t a, uint32_t b) { return (uint32_t)(uint64_t)(((int64_t)(int32_t)a * (int32_t)b) >> 16); }
static uint32_t height_entry(const ctx *c, uint32_t h, uint32_t height) {
    uint32_t flag = r32(c, h + 0x10u) + height, e = r32(c, h + 0x14u) + 12u * height;
    if (r8(c, flag)) return e;
    uint32_t v = mul32(height, r32(c, h + 4u)) + r32(c, h + 8u), vec = r32(c, h + 0x18u);
    w32(c, e, fix_mul(r32(c, vec), v));
    w32(c, e + 4u, fix_mul(r32(c, vec + 4u), v));
    w32(c, e + 8u, fix_mul(r32(c, vec + 8u), v));
    w8(c, flag, 1);
    return e;
}

/* 0x46be9c(n, P): 1 when the word normal n faces the eye from P: the dot
 * product (0x47dd1a, wrapping) of n sign-extended and eye - P above 0
 * (listing 0x46be9c..0x46bef8). */
static uint32_t facing(const ctx *c, uint32_t n, uint32_t px, uint32_t py, uint32_t pz) {
    uint32_t ex = r32(c, VA_EYE) - px, ey = r32(c, VA_EYE + 4u) - py, ez = r32(c, VA_EYE + 8u) - pz;
    uint32_t sum = mul32((uint32_t)sw(c, n), ex) + mul32((uint32_t)sw(c, n + 2u), ey) + mul32((uint32_t)sw(c, n + 4u), ez);
    return (int32_t)sum > 0;
}

/* 0x46e5a4(G, x, y): the cell's object event (listing 0x46e5a4..0x46e628):
 * no cell -> EAX 0; else r = cell_flag_get(G, x, y); r != 0 -> the grid's
 * callback [G+0x125](x, y, r), then cell_flag_set(G, x, y, 0) whose EAX is
 * returned; r == 0 -> EAX 0. */
static int cell_event(ctx *c, uint32_t g, uint32_t x, uint32_t y, uint32_t *eax) {
    *eax = 0;
    if (!cell_of(c, g, cell_index(c, g, x, y))) return 0;
    uint32_t r = 0;
    if (CALL(c, VA_CELL_FLAG_GET, &r, g, x, y)) return 1;
    if (!r) return 0;
    if (CALL(c, r32(c, g + 0x125u), NULL, x, y, r)) return 1;
    return CALL(c, VA_CELL_FLAG_SET, eax, g, x, y, 0u);
}

/* ======================================================================
 * 0x46d448(x, y, G, Q): one cell (listing 0x46d448..0x46ddd8). Q holds the
 * cell's four corners in view space (Q0 (x, y), Q1 (x, y+1), Q2 (x+1, y+1),
 * Q3 (x+1, y)); the heights are added from the cache, the cell is split
 * along the diagonal with the larger z sum, each half is drawn when its
 * normal faces the eye. */
static int fill(ctx *c, uint32_t tri, uint32_t uv, uint32_t n, uint32_t tex, uint32_t shade, int32_t depth) {
    uint32_t mode = r32(c, VA_FILL_MODE), shades = r32(c, VA_SHADE_MODE);
    if (mode == 0u) {
        return CALL(c, VA_FILL_FLAT, NULL, tri, n, shade);
    }
    if (mode != 1u) return 0;
    for (uint32_t k = 0; k < 4u; ++k) w32(c, VA_SHADES + 4u * k, r16(c, shade + 2u * k));
    uint32_t va = (int32_t)(2u * r32(c, VA_NEAR)) > depth ? VA_FILL_NEAR : VA_FILL_TEXTURED;
    return CALL(c, va, NULL, tri, uv, n, tex, VA_SHADES, shades, 1u, 0u);
}
/* The shade word of a half: written in mode 0 when the shade mode is below
 * 2 (unsigned), in mode 1 when it is 1 (listing 0x46dba4..0x46dbcf). */
static void shade_word(const ctx *c, uint32_t shade, uint32_t cell_byte) {
    uint32_t mode = r32(c, VA_FILL_MODE), shades = r32(c, VA_SHADE_MODE);
    if ((mode == 0u && shades < 2u) || (mode == 1u && shades == 1u)) w16(c, shade, r8(c, cell_byte));
}
static void uv_pairs(const ctx *c, uint32_t d, unsigned a, unsigned b, unsigned e) {
    const unsigned k[3] = {a, b, e};
    for (unsigned i = 0; i < 3u; ++i) {
        w32(c, d + 8u * i, r32(c, VA_UV + 8u * k[i]));
        w32(c, d + 8u * i + 4u, r32(c, VA_UV + 8u * k[i] + 4u));
    }
}
static int cell_draw(ctx *c, uint32_t x, uint32_t y, uint32_t g, uint32_t q, uint32_t *eax) {
    static const uint32_t fault[4] = {0x0046d56au, 0x0046d594u, 0x0046d5cbu, 0x0046d603u};
    ++c->t->cells;
    uint32_t index = cell_index(c, g, x, y), row = shl(1u, r32(c, g + 0x100u));
    uint32_t cell[4] = {cell_of(c, g, index), cell_of(c, g, index + row), cell_of(c, g, index + row + 1u), cell_of(c, g, index + 1u)};
    for (unsigned i = 0; i < 4u; ++i) {
        if (!cell[i]) return null_refuse(c, fault[i], "terrain cell draw", "a corner cell outside the grid (the original reads its height at NULL)");
        uint32_t e = height_entry(c, g + 0xd0u, r8(c, cell[i])), p = q + 12u * i;
        w32(c, p + 8u, r32(c, p + 8u) + r32(c, e + 8u));
        w32(c, p, r32(c, p) + r32(c, e));
        w32(c, p + 4u, r32(c, p + 4u) + r32(c, e + 4u));
    }
    int32_t d1 = s32(c, q + 4u) < s32(c, q + 0x10u) ? s32(c, q + 4u) : s32(c, q + 0x10u);
    int32_t d2 = s32(c, q + 0x1cu) < s32(c, q + 0x28u) ? s32(c, q + 0x1cu) : s32(c, q + 0x28u);
    int32_t depth = d1 < d2 ? d1 : d2;
    int32_t split_sum = (int32_t)(r32(c, q + 8u) + r32(c, q + 0x20u) - (r32(c, q + 0x14u) + r32(c, q + 0x2cu)));
    uint32_t tex = 0, entry = (uint32_t)(r8(c, cell[0] + 0xfu) >> 2) * 8u + r32(c, g + 0x121u);
    if (CALL(c, VA_CELL_UV, &tex, entry, x, y, VA_UV)) return 1;
    int textured = r32(c, VA_FILL_MODE) == 1u, split;
    if (split_sum == 0) {
        for (unsigned i = 0; i < 4u; ++i) copy3(c, VA_TRI1 + 12u * i, q + 12u * i);
        if (textured) for (uint32_t i = 0; i < 0x20u; i += 4u) w32(c, VA_UV1 + i, r32(c, VA_UV + i));
        split = 0;
    } else if (split_sum > 0) {
        copy3(c, VA_TRI1, q); copy3(c, VA_TRI1 + 12u, q + 12u); copy3(c, VA_TRI1 + 24u, q + 36u);
        copy3(c, VA_TRI2, q + 12u); copy3(c, VA_TRI2 + 12u, q + 24u); copy3(c, VA_TRI2 + 24u, q + 36u);
        if (textured) { uv_pairs(c, VA_UV1, 0, 1, 3); uv_pairs(c, VA_UV2, 1, 2, 3); }
        split = -1;
    } else {
        copy3(c, VA_TRI1, q); copy3(c, VA_TRI1 + 12u, q + 12u); copy3(c, VA_TRI1 + 24u, q + 24u);
        copy3(c, VA_TRI2, q); copy3(c, VA_TRI2 + 12u, q + 24u); copy3(c, VA_TRI2 + 24u, q + 36u);
        if (textured) { uv_pairs(c, VA_UV1, 0, 1, 2); uv_pairs(c, VA_UV2, 0, 2, 3); }
        split = 1;
    }
    /* The halves' reference points: A the cell's corner (x, y), B the
     * corner (x+1, y+1) with the world origin added twice (listing
     * 0x46da57..0x46db4e). */
    uint32_t sh = r32(c, g + 0x108u), unit = shl(1u, sh);
    uint32_t ax = shl(x, sh) + r32(c, g + 0xf4u), ay = shl(y, sh) + r32(c, g + 0xf8u);
    uint32_t az = mul32(r8(c, cell[0]), r32(c, g + 0x118u)) + r32(c, g + 0x110u) + r32(c, g + 0xfcu);
    uint32_t bx = ax + unit + r32(c, g + 0xf4u), by = ay + unit + r32(c, g + 0xf8u);
    uint32_t bz = mul32(r8(c, cell[2]), r32(c, g + 0x118u)) + r32(c, g + 0x110u) + r32(c, g + 0xfcu);
    if (CALL(c, VA_SET_FAR, NULL, shl(r32(c, g + 0x10cu), sh)) || CALL(c, VA_HAZE, NULL, (uint32_t)depth)) return 1;
    w32(c, VA_FAR_FLAG, depth > 25000);
    /* The shade words -0x4c (first half) and -0x54 (second): stack words
     * the original writes only in some modes; modelled as zero (residue). */
    uint32_t top = c->l->stack_hi, shades = frame_push(c, 0x10u);
    if (!shades) return refuse(c, 0x0046d448u, "terrain cell draw", "the own code's call area is exhausted");
    int bad = 0;
    if (facing(c, cell[0] + 1u, ax, ay, az)) {
        shade_word(c, shades, cell[0] + 0xdu);
        bad = fill(c, VA_TRI1, VA_UV1, split ? 3u : 4u, tex, shades, depth);
    }
    if (!bad && split && facing(c, cell[0] + 7u, bx, by, bz)) {
        shade_word(c, shades + 8u, cell[0] + 0xeu);
        bad = fill(c, VA_TRI2, VA_UV2, 3u, tex, shades + 8u, depth);
    }
    frame_pop(c, top);
    if (bad) return 1;
    return cell_event(c, g, x, y, eax);
}

/* ======================================================================
 * The walkers' callbacks: one line of cells from a to b inclusive in the
 * walker's step direction. R: +0/+4 the eye cell, +8 the quadrant, +0xc
 * the row walker's x step, +0x10 the y step, +0x14 the callback, +0x18 G.
 * 0x46dddc(R, y, a, b): the cells (x, y) for x = a.. b (listing
 * 0x46dddc..0x46dfa4). */
static int row_cells(ctx *c, uint32_t r, uint32_t line, uint32_t a, uint32_t b) {
    uint32_t g = r32(c, r + 0x18u), step = r32(c, r + 0xcu), col[3], wv[3], d[3], p[3], p2[3];
    for (unsigned i = 0; i < 3u; ++i) {
        col[i] = r32(c, g + 4u + 4u * i);
        wv[i] = r32(c, g + 0x10u + 4u * i);
        d[i] = (int32_t)step < 0 ? 0u - col[i] : col[i];
        p[i] = r32(c, g + 0x1cu + 4u * i) + mul32(wv[i], line) + mul32(col[i], a);
        p2[i] = p[i] + wv[i];
    }
    uint32_t end = b + step, eax;
    for (uint32_t x = a; x != end; x += step) {
        for (unsigned i = 0; i < 3u; ++i) {
            w32(c, VA_QUAD + 4u * i, p[i]);
            w32(c, VA_QUAD + 12u + 4u * i, p2[i]);
            w32(c, VA_QUAD + 24u + 4u * i, p2[i] + col[i]);
            w32(c, VA_QUAD + 36u + 4u * i, p[i] + col[i]);
        }
        if (cell_draw(c, x, line, g, VA_QUAD, &eax)) return 1;
        for (unsigned i = 0; i < 3u; ++i) { p[i] += d[i]; p2[i] += d[i]; }
    }
    if (r8(c, VA_EYE_FLAG) && r32(c, VA_EYE_CELL + 4u) == line) {
        int32_t ex = s32(c, VA_EYE_CELL);
        if ((int32_t)step > 0 ? ex >= (int32_t)a && ex < (int32_t)end : ex > (int32_t)end && ex <= (int32_t)a) w8(c, VA_EYE_FLAG, 0);
    }
    return 0;
}
/* 0x46dfa8(R, x, a, b): the cells (x, y) for y = a.. b; the flag test
 * compares the eye's x with the end (listing 0x46dfa8..0x46e17c). */
static int column_cells(ctx *c, uint32_t r, uint32_t line, uint32_t a, uint32_t b) {
    uint32_t g = r32(c, r + 0x18u), step = r32(c, r + 0x10u), col[3], wv[3], d[3], p[3], p2[3];
    for (unsigned i = 0; i < 3u; ++i) {
        col[i] = r32(c, g + 4u + 4u * i);
        wv[i] = r32(c, g + 0x10u + 4u * i);
        d[i] = (int32_t)step < 0 ? 0u - wv[i] : wv[i];
        p[i] = r32(c, g + 0x1cu + 4u * i) + mul32(wv[i], a) + mul32(col[i], line);
        p2[i] = p[i] + col[i];
    }
    uint32_t end = b + step, eax;
    for (uint32_t y = a; y != end; y += step) {
        for (unsigned i = 0; i < 3u; ++i) {
            w32(c, VA_QUAD + 4u * i, p[i]);
            w32(c, VA_QUAD + 12u + 4u * i, p[i] + wv[i]);
            w32(c, VA_QUAD + 24u + 4u * i, p2[i] + wv[i]);
            w32(c, VA_QUAD + 36u + 4u * i, p2[i]);
        }
        if (cell_draw(c, line, y, g, VA_QUAD, &eax)) return 1;
        for (unsigned i = 0; i < 3u; ++i) { p[i] += d[i]; p2[i] += d[i]; }
    }
    if (r8(c, VA_EYE_FLAG) && r32(c, VA_EYE_CELL) == line) {
        int32_t ex = s32(c, VA_EYE_CELL), ey = s32(c, VA_EYE_CELL + 4u);
        if ((int32_t)step > 0 ? ey >= (int32_t)a && ex < (int32_t)end : ey > (int32_t)end && ex <= (int32_t)a) w8(c, VA_EYE_FLAG, 0);
    }
    return 0;
}

/* ======================================================================
 * The walkers. Locals at the original's EBP offsets inside a frame of the
 * call area: three point buffers of 20 points (0x49667c constructs 60
 * elements over all three), the polygons {count, points} -0x10 (the
 * clipped half), -0x8 and -0x18 (its quadrants), and two span headers
 * {min, max, count, spans} with their buffers. */
typedef struct walk {
    uint32_t r, eye, poly, f, size;
    uint32_t h8, b8, h18, b18;
} walk;
static uint32_t at(const walk *w, uint32_t o) { return w->f + w->size - o; }
static int walk_begin(ctx *c, walk *w, uint32_t size, uint32_t points, uint32_t q10, uint32_t q18, uint32_t h8, uint32_t b8,
                      uint32_t h18, uint32_t b18, uint32_t va) {
    w->size = size;
    w->f = frame_push(c, size);
    if (!w->f) return refuse(c, va, "terrain walker", "the own code's call area is exhausted");
    w->h8 = at(w, h8); w->b8 = at(w, b8); w->h18 = at(w, h18); w->b18 = at(w, b18);
    if (CALL(c, VA_VECTOR_NEW, NULL, at(w, points), 8u, 0x3cu, 1u, VA_EMPTY_CTOR)) return 1;
    w32(c, at(w, 4u), at(w, points));
    w32(c, at(w, 0xcu), at(w, q10));
    w32(c, at(w, 0x14u), at(w, q18));
    w32(c, w->r, r32(c, w->eye));
    w32(c, w->r + 4u, r32(c, w->eye + 4u));
    return 0;
}
static int clip(ctx *c, const walk *w, uint32_t keep, uint32_t axis, uint32_t line, uint32_t in, uint32_t out) {
    (void)w;
    return CALL(c, VA_POLY_CLIP, NULL, keep, axis, line, in, out);
}
/* The callback through R+0x14 (a call through the pointer, so the dispatch). */
static int line_call(ctx *c, const walk *w, uint32_t a1, uint32_t a2, uint32_t a3) {
    return CALL(c, r32(c, w->r + 0x14u), NULL, w->r, a1, a2, a3);
}
static int spans(ctx *c, const walk *w, uint32_t poly, uint32_t buffer, uint32_t header) {
    (void)w;
    return CALL(c, VA_POLY_SPANS, NULL, poly, buffer, header);
}
static uint32_t last_span(const ctx *c, uint32_t header) { return r32(c, header + 8u) * 8u + r32(c, header + 0xcu) - 8u; }
static int in_spans(const ctx *c, uint32_t header, int32_t row) { return row <= s32(c, header + 4u) && row >= s32(c, header); }

/* The two halves the main order shares (row and column walkers): the
 * half beyond the eye's line, its quadrants -0x8 (keep below eye+1) and
 * -0x18 (keep from eye), then the half before it. */
static int halves_top(ctx *c, const walk *w) {
    uint32_t p10 = at(w, 0x10u), p8 = at(w, 8u), p18 = at(w, 0x18u);
    return clip(c, w, 1u, 0u, r32(c, w->eye + 4u) + 1u, w->poly, p10) ||
           clip(c, w, 1u, 1u, r32(c, w->eye) + 1u, p10, p8) || clip(c, w, 0u, 1u, r32(c, w->eye), p10, p18);
}
static int halves_bottom(ctx *c, const walk *w) {
    uint32_t p10 = at(w, 0x10u), p8 = at(w, 8u), p18 = at(w, 0x18u);
    return clip(c, w, 0u, 0u, r32(c, w->eye + 4u), w->poly, p10) ||
           clip(c, w, 0u, 1u, r32(c, w->eye) - 1u, p10, p8) || clip(c, w, 1u, 1u, r32(c, w->eye), p10, p18);
}

/* 0x471f44(R, eye, poly): the row order (listing 0x471f44..0x4724b4). */
static int row_walk(ctx *c, uint32_t r, uint32_t eye, uint32_t poly) {
    walk w = {r, eye, poly, 0, 0, 0, 0, 0, 0};
    uint32_t top = c->l->stack_hi;
    int bad = walk_begin(c, &w, 0x14e4u, 0x204u, 0x164u, 0xc4u, 0x14d4u, 0xb64u, 0x14e4u, 0x14c4u, 0x00471f44u);
    uint32_t p8 = at(&w, 8u), p18 = at(&w, 0x18u), p10 = at(&w, 0x10u);
    if (bad) goto done;
    if (r32(c, VA_SIMPLE) == 0u && r32(c, VA_SPLIT_WALK) != 0u) {
        /* The half beyond the eye's row, rows descending. */
        w32(c, r + 0x10u, 0xffffffffu);
        if ((bad = halves_top(c, &w))) goto done;
        int32_t lo = 1000, hi = -1;
        uint32_t e8 = 0, e18 = 0;
        if (r32(c, p8)) {
            if ((bad = spans(c, &w, p8, w.b8, w.h8))) goto done;
            e8 = last_span(c, w.h8); hi = s32(c, w.h8 + 4u); lo = s32(c, w.h8);
        }
        if (r32(c, p18)) {
            if ((bad = spans(c, &w, p18, w.b18, w.h18))) goto done;
            e18 = last_span(c, w.h18);
            if (hi < s32(c, w.h18 + 4u)) hi = s32(c, w.h18 + 4u);
            if (s32(c, w.h18) < lo) lo = s32(c, w.h18);
        }
        if (hi > -1 && hi >= lo)
            for (int32_t row = hi; row >= lo; --row) {
                if (r32(c, p8) && in_spans(c, w.h8, row)) {
                    w32(c, r + 8u, 0); w32(c, r + 0xcu, 0xffffffffu);
                    if ((bad = line_call(c, &w, (uint32_t)row, r32(c, e8 + 4u), r32(c, e8)))) goto done;
                    e8 -= 8u;
                }
                if (r32(c, p18) && in_spans(c, w.h18, row)) {
                    w32(c, r + 8u, 1); w32(c, r + 0xcu, 1);
                    if ((bad = line_call(c, &w, (uint32_t)row, r32(c, e18), r32(c, e18 + 4u)))) goto done;
                    e18 -= 8u;
                }
            }
        /* The half before it, rows ascending. */
        w32(c, r + 0x10u, 1);
        if ((bad = halves_bottom(c, &w))) goto done;
        int32_t row = 1000;
        hi = -1;
        if (r32(c, p8)) {
            if ((bad = spans(c, &w, p8, w.b8, w.h8))) goto done;
            e8 = r32(c, w.h8 + 0xcu); row = s32(c, w.h8); hi = s32(c, w.h8 + 4u);
        }
        if (r32(c, p18)) {
            if ((bad = spans(c, &w, p18, w.b18, w.h18))) goto done;
            e18 = r32(c, w.h18 + 0xcu);
            if (s32(c, w.h18 + 4u) > hi) hi = s32(c, w.h18 + 4u);
            if (row > s32(c, w.h18)) row = s32(c, w.h18);
        }
        if (hi > -1 && row <= hi)
            for (; row <= hi; ++row) {
                if (r32(c, p8) && in_spans(c, w.h8, row)) {
                    w32(c, r + 8u, 2); w32(c, r + 0xcu, 1);
                    if ((bad = line_call(c, &w, (uint32_t)row, r32(c, e8), r32(c, e8 + 4u)))) goto done;
                    e8 += 8u;
                }
                if (r32(c, p18) && in_spans(c, w.h18, row)) {
                    w32(c, r + 8u, 3); w32(c, r + 0xcu, 0xffffffffu);
                    if ((bad = line_call(c, &w, (uint32_t)row, r32(c, e18 + 4u), r32(c, e18)))) goto done;
                    e18 += 8u;
                }
            }
        goto done;
    }
    /* The simple order (listing 0x472295..0x4724ae): the four quadrants
     * one after the other through the polygon -0x8 alone. */
    w32(c, r + 8u, 2); w32(c, r + 0x10u, 0xffffffffu); w32(c, r + 0xcu, 0xffffffffu);
    if ((bad = clip(c, &w, 0u, 0u, r32(c, eye + 4u), poly, p10)) || (bad = clip(c, &w, 0u, 1u, r32(c, eye) - 1u, p10, p8))) goto done;
    if (r32(c, p8)) {
        if ((bad = spans(c, &w, p8, w.b8, w.h8))) goto done;
        uint32_t e = last_span(c, w.h8);
        for (int32_t row = s32(c, w.h8 + 4u); row >= s32(c, w.h8); --row, e -= 8u)
            if ((bad = line_call(c, &w, (uint32_t)row, r32(c, e + 4u), r32(c, e)))) goto done;
    }
    w32(c, r + 8u, 3); w32(c, r + 0xcu, 1);
    if ((bad = clip(c, &w, 1u, 1u, r32(c, eye), p10, p8))) goto done;
    if (r32(c, p8)) {
        if ((bad = spans(c, &w, p8, w.b8, w.h8))) goto done;
        uint32_t e = last_span(c, w.h8);
        for (int32_t row = s32(c, w.h8 + 4u); row >= s32(c, w.h8); --row, e -= 8u)
            if ((bad = line_call(c, &w, (uint32_t)row, r32(c, e), r32(c, e + 4u)))) goto done;
    }
    w32(c, r + 8u, 0); w32(c, r + 0x10u, 1); w32(c, r + 0xcu, 1);
    if ((bad = clip(c, &w, 1u, 0u, r32(c, eye + 4u) + 1u, poly, p10)) || (bad = clip(c, &w, 1u, 1u, r32(c, eye) + 1u, p10, p8))) goto done;
    if (r32(c, p8)) {
        if ((bad = spans(c, &w, p8, w.b8, w.h8))) goto done;
        uint32_t e = r32(c, w.h8 + 0xcu);
        for (int32_t row = s32(c, w.h8); row <= s32(c, w.h8 + 4u); ++row, e += 8u)
            if ((bad = line_call(c, &w, (uint32_t)row, r32(c, e), r32(c, e + 4u)))) goto done;
    }
    w32(c, r + 8u, 1); w32(c, r + 0xcu, 0xffffffffu);
    if ((bad = clip(c, &w, 0u, 1u, r32(c, eye), p10, p8))) goto done;
    if (r32(c, p8)) {
        if ((bad = spans(c, &w, p8, w.b8, w.h8))) goto done;
        uint32_t e = r32(c, w.h8 + 0xcu);
        for (int32_t row = s32(c, w.h8); row <= s32(c, w.h8 + 4u); ++row, e += 8u)
            if ((bad = line_call(c, &w, (uint32_t)row, r32(c, e + 4u), r32(c, e)))) goto done;
    }
done:
    frame_pop(c, top);
    return bad;
}

/* 0x4724b8(px, py, c): the point turned a quarter about c (listing
 * 0x4724b8..0x4724dd); 0x4724e0 turns it back (0x4724e0..0x472503). EAX
 * c, EDX the new *py (0x4724b8) or px (0x4724e0), ECX py. */
static uint32_t turn(const ctx *c, uint32_t px, uint32_t py, uint32_t e) {
    uint32_t nx = r32(c, py) - r32(c, e + 4u) + r32(c, e), old = r32(c, px);
    w32(c, px, nx);
    uint32_t ny = r32(c, e) - old + r32(c, e + 4u);
    w32(c, py, ny);
    return ny;
}
static void turn_back(const ctx *c, uint32_t px, uint32_t py, uint32_t e) {
    uint32_t nx = r32(c, e + 4u) - r32(c, py) + r32(c, e), old = r32(c, px);
    w32(c, px, nx);
    w32(c, py, old - r32(c, e) + r32(c, e + 4u));
}
/* The column walker's x for a turned row and y for a turned end. */
static uint32_t back_x(const ctx *c, uint32_t e, uint32_t row) { return r32(c, e + 4u) - row + r32(c, e); }
static uint32_t back_y(const ctx *c, uint32_t e, uint32_t v) { return v - r32(c, e) + r32(c, e + 4u); }

/* 0x472504(R, eye, poly): the column order: the polygon turned a quarter
 * about the eye, walked by rows, every row a column (listing
 * 0x472504..0x47297a); the points turned back at the end. */
static int column_walk(ctx *c, uint32_t r, uint32_t eye, uint32_t poly) {
    walk w = {r, eye, poly, 0, 0, 0, 0, 0, 0};
    uint32_t top = c->l->stack_hi;
    int bad = walk_begin(c, &w, 0x152cu, 0x24cu, 0x1acu, 0x10cu, 0x151cu, 0xbacu, 0x152cu, 0x150cu, 0x00472504u);
    uint32_t p8 = at(&w, 8u), p18 = at(&w, 0x18u);
    if (bad) goto done;
    {
        uint32_t pts = r32(c, poly + 4u);
        for (uint32_t i = 0; s32(c, poly) > (int32_t)i; ++i, pts += 8u) turn(c, pts, pts + 4u, eye);
    }
    w32(c, r + 0xcu, 1);
    if ((bad = halves_top(c, &w))) goto done;
    {
        int32_t lo = 1000, hi = -1;
        uint32_t s8 = 0, s18 = 0;
        if (r32(c, p8)) {
            if ((bad = spans(c, &w, p8, w.b8, w.h8))) goto done;
            s8 = last_span(c, w.h8); hi = s32(c, w.h8 + 4u); lo = s32(c, w.h8);
        }
        if (r32(c, p18)) {
            if ((bad = spans(c, &w, p18, w.b18, w.h18))) goto done;
            s18 = last_span(c, w.h18);
            if (hi < s32(c, w.h18 + 4u)) hi = s32(c, w.h18 + 4u);
            if (s32(c, w.h18) < lo) lo = s32(c, w.h18);
        }
        if (hi > -1 && hi >= lo)
            for (int32_t row = hi; row >= lo; --row) {
                if (r32(c, p8) && in_spans(c, w.h8, row)) {
                    w32(c, r + 8u, 0); w32(c, r + 0x10u, 0xffffffffu);
                    uint32_t x = back_x(c, eye, (uint32_t)row), yb = back_y(c, eye, r32(c, s8 + 4u)), ya = back_y(c, eye, r32(c, s8));
                    if ((bad = line_call(c, &w, x, yb, ya))) goto done;
                    s8 -= 8u;
                }
                if (r32(c, p18) && in_spans(c, w.h18, row)) {
                    w32(c, r + 8u, 1); w32(c, r + 0x10u, 1);
                    uint32_t x = back_x(c, eye, (uint32_t)row), yb = back_y(c, eye, r32(c, s18 + 4u)), ya = back_y(c, eye, r32(c, s18));
                    if ((bad = line_call(c, &w, x, ya, yb))) goto done;
                    s18 -= 8u;
                }
            }
        w32(c, r + 0xcu, 0xffffffffu);
        if ((bad = halves_bottom(c, &w))) goto done;
        int32_t row = 1000;
        hi = -1;
        if (r32(c, p8)) {
            if ((bad = spans(c, &w, p8, w.b8, w.h8))) goto done;
            s8 = r32(c, w.h8 + 0xcu); row = s32(c, w.h8); hi = s32(c, w.h8 + 4u);
        }
        if (r32(c, p18)) {
            if ((bad = spans(c, &w, p18, w.b18, w.h18))) goto done;
            s18 = r32(c, w.h18 + 0xcu);
            if (s32(c, w.h18 + 4u) > hi) hi = s32(c, w.h18 + 4u);
            if (row > s32(c, w.h18)) row = s32(c, w.h18);
        }
        if (hi > -1 && row <= hi)
            for (; row <= hi; ++row) {
                if (r32(c, p8) && in_spans(c, w.h8, row)) {
                    w32(c, r + 8u, 2); w32(c, r + 0x10u, 1);
                    uint32_t x = back_x(c, eye, (uint32_t)row), yb = back_y(c, eye, r32(c, s8 + 4u)), ya = back_y(c, eye, r32(c, s8));
                    if ((bad = line_call(c, &w, x, ya, yb))) goto done;
                    s8 += 8u;
                }
                if (r32(c, p18) && in_spans(c, w.h18, row)) {
                    w32(c, r + 8u, 3); w32(c, r + 0x10u, 0xffffffffu);
                    uint32_t x = back_x(c, eye, (uint32_t)row), yb = back_y(c, eye, r32(c, s18 + 4u)), ya = back_y(c, eye, r32(c, s18));
                    if ((bad = line_call(c, &w, x, yb, ya))) goto done;
                    s18 += 8u;
                }
            }
    }
    {
        uint32_t pts = r32(c, poly + 4u);
        for (uint32_t i = 0; (int32_t)i < s32(c, poly); ++i, pts += 8u) turn_back(c, pts, pts + 4u, eye);
    }
done:
    frame_pop(c, top);
    return bad;
}

/* ======================================================================
 * 0x46d1a8(G, cam): the walk (listing 0x46d1a8..0x46d320). Nothing when the
 * visible byte G+0x11c is 0 (EAX then undefined: the entry's EAX). */
static int walk_draw(ctx *c, uint32_t g, uint32_t cam, uint32_t *eax) {
    if (!r8(c, g + 0x11cu)) return 0;
    if (CALL(c, VA_OBJECT_SELECT, NULL, 0u)) return 1;
    uint32_t top = c->l->stack_hi, f = frame_push(c, 0x38u);
    if (!f) return refuse(c, 0x0046d1a8u, "terrain walk", "the own code's call area is exhausted");
    uint32_t r = f, local = f + 0x30u;   /* R at -0x38, the dword -0x8 */
    uint32_t gc = r32(c, VA_CONTEXT), saved = r32(c, gc + 0x22cu);
    w32(c, gc + 0x22cu, local);
    uint32_t sh = r32(c, g + 0x108u), ex = sar(r32(c, cam + 4u), sh), ey = sar(r32(c, cam + 8u), sh);
    w32(c, VA_EYE_CELL, ex);
    w32(c, VA_EYE_CELL + 4u, ey);
    uint32_t cell = cell_of(c, g, cell_index(c, g, ex, ey));
    w8(c, VA_EYE_FLAG, (uint8_t)(cell ? cell_mark(c, g, cell) : 0u));
    uint16_t h = r16(c, cam + 0x14u);
    if (h == 0x8000u) h = 0x7fffu;
    else if ((int16_t)h < 0) h = (uint16_t)(0u - h);
    int columns = (int16_t)h > 0x2000 && (int16_t)h < 0x6000;
    w32(c, r + 0x14u, columns ? VA_COLUMN_CELLS : VA_ROW_CELLS);   /* 0x471f30 */
    w32(c, r + 0x18u, g);
    int bad = columns ? column_walk(c, r, VA_EYE_CELL, g + 0xc8u) : row_walk(c, r, VA_EYE_CELL, g + 0xc8u);
    uint32_t ignored;
    if (!bad && r8(c, VA_EYE_FLAG)) bad = cell_event(c, g, ex, ey, &ignored);
    if (!bad) {
        w32(c, r32(c, VA_CONTEXT) + 0x22cu, saved);
        *eax = saved;
    }
    frame_pop(c, top);
    return bad;
}

/* ======================================================================
 * 0x46cb9c(G, cam, Q): the frame's terrain set-up (listing
 * 0x46cb9c..0x46d1a4). Q != 0: the view polygon given (clamped to the
 * grid); Q == 0: the view polygon from the frustum library. Then the grid
 * origin and step vectors in view space and the height cache. */
static int basis(ctx *c, uint32_t g, uint32_t tmp, uint32_t copy1, uint32_t copy2, uint32_t *eax) {
    uint32_t sh = r32(c, g + 0x108u);
    w32(c, tmp + 8u, 0); w32(c, tmp + 4u, 0); w32(c, tmp, 0);
    if (CALL(c, VA_TRANSFORM_POINT, NULL, tmp, VA_VIEW_MATRIX, g + 0x1cu)) return 1;
    w32(c, tmp, shl(1u, sh));
    for (uint32_t i = 0; i < 0x14u; i += 2u) w16(c, copy1 + i, r16(c, VA_VIEW_MATRIX + i));
    if (CALL(c, VA_ROTATE_POINT, NULL, tmp, copy1, g + 4u)) return 1;
    w32(c, tmp, 0);
    w32(c, tmp + 4u, shl(1u, sh));
    for (uint32_t i = 0; i < 0x14u; i += 2u) w16(c, copy2 + i, r16(c, VA_VIEW_MATRIX + i));
    if (CALL(c, VA_ROTATE_POINT, NULL, tmp, copy2, g + 0x10u)) return 1;
    return CALL(c, VA_HEIGHT_SET, NULL, g + 0xd0u, r32(c, g + 0x110u), r32(c, g + 0x118u)) ||
           CALL(c, VA_HEIGHT_RESET, eax, g + 0xd0u);
}
static int terrain_setup(ctx *c, uint32_t g, uint32_t cam, uint32_t q, uint32_t *eax) {
    w32(c, VA_EYE + 8u, r32(c, cam + 0xcu));
    w32(c, VA_EYE, r32(c, cam + 4u));
    w32(c, VA_EYE + 4u, r32(c, cam + 8u));
    w32(c, g + 0x10cu, (uint32_t)sw(c, VA_FAR_TABLE + 2u * r8(c, VA_DETAIL)));
    uint32_t sh = r32(c, g + 0x108u);
    if ((int32_t)sh > 14) w32(c, g + 0x10cu, sar(r32(c, g + 0x10cu), sh - 14u));
    if (CALL(c, VA_DRAW_DISTANCES, NULL, shl(r32(c, g + 0x10cu), r32(c, g + 0x108u))) || CALL(c, VA_OBJECT_SELECT, NULL, 0u)) return 1;
    uint32_t top = c->l->stack_hi, f = frame_push(c, 0x21cu);
    if (!f) return refuse(c, 0x0046cb9cu, "terrain draw", "the own code's call area is exhausted");
#define L(o) (f + 0x21cu - (o))
    int bad = 0;
    sh = r32(c, g + 0x108u);
    if (q) {
        w32(c, g + 0xccu, r32(c, q + 4u));
        w32(c, g + 0xc8u, r32(c, q));
        int32_t mx = (int32_t)shl(shl(1u, r32(c, g + 0x100u)) - 2u, sh), my = (int32_t)shl(shl(1u, r32(c, g + 0x104u)) - 2u, sh);
        for (int32_t i = 0; i < s32(c, q); ++i) {
            uint32_t p = r32(c, q + 4u) + 8u * (uint32_t)i;
            if (0 > s32(c, p)) w32(c, p, 0);
            if (s32(c, p) > mx) w32(c, p, (uint32_t)mx);
            if (s32(c, p + 4u) < 0) w32(c, p + 4u, 0);
            if (s32(c, p + 4u) > my) w32(c, p + 4u, (uint32_t)my);
        }
        uint32_t pts = r32(c, q + 4u);
        uint8_t visible = s32(c, pts) < s32(c, pts + 0x10u) && s32(c, pts + 4u) < s32(c, pts + 0xcu);
        w8(c, g + 0x11cu, visible);
        *eax = visible;
        if (visible) {
            for (int32_t i = 0; i < s32(c, q); ++i) {
                uint32_t p = r32(c, q + 4u) + 8u * (uint32_t)i;
                w32(c, p, sar(r32(c, p), r32(c, g + 0x108u)));
                w32(c, p + 4u, sar(r32(c, p + 4u), r32(c, g + 0x108u)));
            }
            bad = basis(c, g, L(0x28u), L(0x3cu), L(0x50u), eax);
        }
    } else {
        uint32_t rec = L(0xc8u), al = 0;
        bad = CALL(c, VA_FRUSTUM_NEW, NULL, rec, shl(r32(c, g + 0x10cu), sh), shl(1u, sh) + shl(1u, sh - 1u)) ||
              CALL(c, VA_FRUSTUM_PLACE, NULL, rec, cam, r32(c, rec + 4u), r32(c, rec)) ||
              CALL(c, VA_VECTOR_NEW, NULL, L(0xf8u), 0xcu, 4u, 1u, VA_VECTOR_CTOR) ||
              CALL(c, VA_VECTOR_NEW, NULL, L(0x1e8u), 0xcu, 0x14u, 1u, VA_VECTOR_CTOR);
        if (!bad) {
            w32(c, L(0x14u), 4u);
            w32(c, L(0x10u), L(0xf8u));
            w32(c, L(0x18u), L(0x1e8u));
            bad = CALL(c, VA_VIEW_RECT, NULL, g, L(0x14u)) ||
                  CALL(c, VA_FRUSTUM_CLIP, &al, rec, L(0x14u), r32(c, g + 0x110u), r32(c, g + 0x114u), L(0x1cu));
        }
        if (!bad) {
            w8(c, g + 0x11cu, (uint8_t)al);
            *eax = al;
            if ((uint8_t)al) {
                w32(c, g + 0xccu, g + 0x28u);
                w32(c, g + 0xc8u, r32(c, L(0x1cu)));
                for (int32_t i = 0; i < s32(c, L(0x1cu)); ++i) {
                    uint32_t s = r32(c, L(0x18u)) + 12u * (uint32_t)i, d = r32(c, g + 0xccu) + 8u * (uint32_t)i;
                    w32(c, d, sar(r32(c, s), r32(c, g + 0x108u)));
                    w32(c, d + 4u, sar(r32(c, s + 4u), r32(c, g + 0x108u)));
                }
                bad = basis(c, g, L(0x1f4u), L(0x208u), L(0x21cu), eax);
            }
        }
    }
#undef L
    frame_pop(c, top);
    return bad;
}

/* ======================================================================
 * Service rows (sim/dispatch/services/terrain_draw.h). */
void es2_sim_service_terrain_draw(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t eax = 0;
    if (terrain_setup(&c, A(0), A(1), A(2), &eax)) { failed(sc); return; }
    sc->eax = eax;
}
void es2_sim_service_terrain_walk(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t eax = sc->eax;
    if (walk_draw(&c, A(0), A(1), &eax)) { failed(sc); return; }
    sc->eax = eax;
}
/* 0x471f30(R, callback, G): R+0x14, R+0x18 (listing 0x471f30..0x471f43). */
void es2_sim_service_terrain_walk_setup(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    w32(&c, A(0) + 0x14u, A(1));
    w32(&c, A(0) + 0x18u, A(2));
    sc->eax = A(0); sc->edx = A(1); sc->ecx = A(2);
}
void es2_sim_service_terrain_row_walk(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    if (row_walk(&c, A(0), A(1), A(2))) failed(sc);
}
void es2_sim_service_terrain_column_walk(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    if (column_walk(&c, A(0), A(1), A(2))) failed(sc);
}
void es2_sim_service_terrain_turn(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    sc->edx = turn(&c, A(0), A(1), A(2));
    sc->eax = A(2); sc->ecx = A(1);
}
void es2_sim_service_terrain_turn_back(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    turn_back(&c, A(0), A(1), A(2));
    sc->eax = A(2); sc->edx = A(0); sc->ecx = A(1);
}
void es2_sim_service_terrain_row_cells(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    if (row_cells(&c, A(0), A(1), A(2), A(3))) failed(sc);
}
void es2_sim_service_terrain_column_cells(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    if (column_cells(&c, A(0), A(1), A(2), A(3))) failed(sc);
}
void es2_sim_service_terrain_cell_draw(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t eax = 0;
    if (cell_draw(&c, A(0), A(1), A(2), A(3), &eax)) { failed(sc); return; }
    sc->eax = eax;
}
void es2_sim_service_terrain_cell_event(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t eax = 0;
    if (cell_event(&c, A(0), A(1), A(2), &eax)) { failed(sc); return; }
    sc->eax = eax;
}
void es2_sim_service_height_cache_get(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    sc->eax = height_entry(&c, A(0), A(1));
}
void es2_sim_service_terrain_cell_mark(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    sc->eax = cell_mark(&c, A(0), A(1));
}
void es2_sim_service_terrain_facing(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t p = A(1);
    sc->eax = facing(&c, A(0), r32(&c, p), r32(&c, p + 4u), r32(&c, p + 8u));
}

/* ======================================================================
 * The grid's binning leaves and the binning vector (rasterizer.md, "Renderer
 * leftovers"; rows in sim/dispatch/services/renderer_leaves.h). Every row sets EAX, EDX
 * and ECX as the original leaves them; a callee's registers pass through where
 * the original returns right after the call. */
#define VA_BIN_RECORD 0x006b5058u      /* 3 bytes: column, row, mark (0x46e8c4) */
#define VA_BIN_VECTOR 0x0049ac40u      /* the binning vector (0x428efc) */
#define VA_TERRAIN_FACE 0x0046e498u    /* hook */
#define VA_RT_MEMSET 0x0049681au       /* service */
#define VA_VECTOR_NEW_13 0x0049667cu   /* service */
#define VA_BIN_DTOR 0x00429655u        /* the vector's element destructor argument */

/* A dispatch call keeping the callee's EAX, EDX and ECX. */
static int call_r(ctx *c, uint32_t va, const uint32_t *slots, unsigned count, es2_sim_result *out) {
    es2_sim_loop *l = c->l;
    uint32_t bytes = 4u * (count ? count : 1u), top = l->stack_hi;
    if (top - l->stack_lo < bytes) return refuse(c, va, NULL, "the own code's call area is exhausted");
    l->stack_hi -= bytes;
    for (unsigned i = 0; i < count; ++i) w32(c, l->stack_hi + 4u * i, slots[i]);
    uint32_t zero[3] = {0, 0, 0};
    memset(out, 0, sizeof(*out));
    int refused = es2_sim_call(l->dispatch, va, c->m->host(c->m->context, l->stack_hi), count, zero, out);
    l->stack_hi = top;
    if (refused) {
        c->t->refused_va = va;
        if (l->outcome == ES2_SIM_LOOP_EXITED) return 1;   /* the process ended inside */
        l->outcome = ES2_SIM_LOOP_REFUSED;
        l->refused_va = va;
        return 1;
    }
    return 0;
}
#define CALL_R(c, va, out, ...) call_r((c), (va), (const uint32_t[]){__VA_ARGS__}, \
    (unsigned)(sizeof((const uint32_t[]){__VA_ARGS__}) / sizeof(uint32_t)), (out))
typedef struct regs13 { uint32_t eax, edx, ecx; } regs13;
static void put13(es2_sim_service_call *sc, const regs13 *r) { sc->eax = r->eax; sc->edx = r->edx; sc->ecx = r->ecx; }

/* 0x470db8(G, cell) (listing 0x470db8..0x470ddb): the cell's mark byte
 * [[G+0xf0] + (cell - [G+0xec]) >> 4] plus 1 (a byte add), returned
 * zero-extended. EDX the index (logical shift), ECX the entry ECX with its
 * low byte the mark. */
static void mark_add(const ctx *c, uint32_t g, uint32_t cell, uint32_t ecx_in, regs13 *r) {
    uint32_t idx = (cell - r32(c, g + 0xecu)) >> 4, at = r32(c, g + 0xf0u) + idx;
    uint8_t m = (uint8_t)(r8(c, at) + 1u);
    w8(c, at, m);
    r->eax = m; r->edx = idx; r->ecx = (ecx_in & 0xffffff00u) | m;
}
/* 0x46e8c4(G, w) (listing 0x46e8c4..0x46e943) on the column word `col`
 * and the row word `row` (sign-extended), with b0 = byte [w] and b2 =
 * byte [w+2]: idx = (row << [G+0x100]) + col; outside 0 <= idx < 1 <<
 * ([G+0x100] + [G+0x104]) (signed): EAX 0, EDX idx. Otherwise the record
 * 0x6b5058 <- b0, b2, then the cell's mark plus 1 (0x470db8, own code)
 * into 0x6b505a; EAX 0x6b5058, EDX that of 0x470db8 (idx). ECX on both
 * paths the column sign-extended: the epilogue's POP ECX takes the local
 * slot the column was stored in (0x46e8d7, 0x46e941). */
static void grid_cell(const ctx *c, uint32_t g, uint32_t w, int have_w, uint16_t col, uint16_t row, regs13 *r) {
    uint32_t idx = shl((uint32_t)(int32_t)(int16_t)row, r32(c, g + 0x100u)) + (uint32_t)(int32_t)(int16_t)col;
    uint32_t cell = 0;
    if ((int32_t)idx >= 0 && (int32_t)idx < (int32_t)shl(1u, r32(c, g + 0x100u) + r32(c, g + 0x104u)))
        cell = (idx << 4) + r32(c, g + 0xecu);
    if (!cell) { r->eax = 0; r->edx = idx; r->ecx = (uint32_t)(int32_t)(int16_t)col; return; }
    w8(c, VA_BIN_RECORD, have_w ? r8(c, w) : (uint8_t)col);
    w8(c, VA_BIN_RECORD + 1u, have_w ? r8(c, w + 2u) : (uint8_t)row);
    mark_add(c, g, cell, cell, r);
    w8(c, VA_BIN_RECORD + 2u, (uint8_t)r->eax);
    r->eax = VA_BIN_RECORD;
    r->ecx = (uint32_t)(int32_t)(int16_t)col;
}
/* 0x46e62c(G, P, size) (listing 0x46e62c..0x46e8c0): the binning cell of
 * the point P (x, y, z) for an object of radius `size`. With s = [G+0x108]
 * the cell (cx, cy) = (P.x >> s, P.y >> s) and the eye's cell (ex, ey) of
 * 0x6b4f44/0x6b4f48 (arithmetic shifts), the quadrant of the eye decides
 * which neighbour the object leans into when it lies within `size` of the
 * cell's edge (q0: ex < cx, cy > ey; q1: ex >= cx, cy > ey; q2: ex >= cx,
 * cy <= ey; q3: ex < cx, cy <= ey; every compare signed). A moved cell is
 * taken back when the terrain face under P (hook 0x46e498) does not face
 * the eye (0x46be9c, own code), and per axis when it lands one beyond the
 * bounds of the grid's view polygon ([G+0xc8] points at [G+0xcc]). Outside
 * the grid EAX 0; else the record of 0x46e8c4 for the words (cx, cy). EDX
 * and ECX as the original leaves them on each path. */
static int grid_at(ctx *c, uint32_t g, uint32_t pos, uint32_t size, regs13 *r) {
    uint32_t sh = r32(c, g + 0x108u), px = r32(c, pos), py = r32(c, pos + 4u);
    int32_t cx = (int32_t)sar(px, sh), cy = (int32_t)sar(py, sh), ox = cx, oy = cy;
    int32_t ex = (int32_t)sar(r32(c, VA_EYE), sh), ey = (int32_t)sar(r32(c, VA_EYE + 4u), sh);
    unsigned q = (ex < cx && cy > ey) ? 0u : (ex >= cx && cy > ey) ? 1u : (ex >= cx && cy <= ey) ? 2u : 3u;
    uint32_t edx = 0, ecx = px;
    int32_t t;
    switch (q) {
    case 0:
        t = (int32_t)(shl((uint32_t)cx, sh) + size);
        edx = shl((uint32_t)cy, sh) + size;
        if ((int32_t)px < t) --cx;
        if ((int32_t)edx > (int32_t)py) --cy;
        break;
    case 1:
        t = (int32_t)(shl((uint32_t)cx + 1u, sh) - size);
        edx = shl((uint32_t)cy, sh) + size;
        if ((int32_t)px > t && ex > cx) ++cx;
        if ((int32_t)edx > (int32_t)py) --cy;
        break;
    case 2:
        t = (int32_t)(shl((uint32_t)cx + 1u, sh) - size);
        edx = shl((uint32_t)cy + 1u, sh) - size;
        if ((int32_t)px > t && ex > cx) ++cx;
        if ((int32_t)edx < (int32_t)py && cy < ey) ++cy;
        break;
    default:
        t = (int32_t)(shl((uint32_t)cx, sh) + size);
        edx = shl((uint32_t)cy + 1u, sh) - size;
        ecx = pos;
        if ((int32_t)px < t) --cx;
        if ((int32_t)edx < (int32_t)py && cy < ey) ++cy;
        break;
    }
    if (cx != ox || cy != oy) {
        uint32_t face = 0;
        if (CALL(c, VA_TERRAIN_FACE, &face, g, pos)) return 1;
        if (face && !facing(c, face, r32(c, pos), r32(c, pos + 4u), r32(c, pos + 8u))) { cx = ox; cy = oy; }
        uint32_t minx = 0x7fffffffu, miny = 0x7fffffffu, maxx = 0x80000000u, maxy = 0x80000000u, p = r32(c, g + 0xccu);
        int32_t i = 0;
        edx = 0;
        for (; i < (int32_t)r32(c, g + 0xc8u); ++i, p += 8u) {
            edx = r32(c, p);
            if ((int32_t)edx < (int32_t)minx) minx = edx;
            if ((int32_t)edx > (int32_t)maxx) maxx = edx;
            edx = r32(c, p + 4u);
            if ((int32_t)edx < (int32_t)miny) miny = edx;
            if ((int32_t)edx > (int32_t)maxy) maxy = edx;
        }
        ecx = (uint32_t)i;
        if ((uint32_t)cx == minx - 1u || (uint32_t)cx == maxx + 1u) cx = ox;
        if ((uint32_t)cy == miny - 1u) cy = oy;
        else {
            edx = maxy + 1u;
            if ((uint32_t)cy == edx) cy = oy;
        }
    }
    r->eax = 0; r->edx = edx; r->ecx = ecx;
    if (cx < 0) return 0;
    r->ecx = r32(c, g + 0x100u);
    if (cx >= (int32_t)shl(1u, r->ecx) || cy < 0) return 0;
    r->ecx = r32(c, g + 0x104u);
    if (cy >= (int32_t)shl(1u, r->ecx)) return 0;
    grid_cell(c, g, 0, 0, (uint16_t)cx, (uint16_t)cy, r);
    return 0;
}

void es2_sim_service_terrain_grid_at(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    regs13 r;
    if (grid_at(&c, A(0), A(1), A(2), &r)) { failed(sc); return; }
    put13(sc, &r);
}
void es2_sim_service_terrain_grid_cell(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    regs13 r;
    uint32_t w = A(1);
    grid_cell(&c, A(0), w, 1, r16(&c, w), r16(&c, w + 2u), &r);
    put13(sc, &r);
}
/* 0x46e944(G) (listing 0x46e944..0x46e97c): the marks cleared,
 * memset([G+0xf0], 0, (1 << [G+0x100]) * (1 << [G+0x104])); EAX, EDX and
 * ECX the memset's. */
void es2_sim_service_terrain_grid_begin(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    uint32_t g = A(0), n = mul32(shl(1u, r32(&c, g + 0x100u)), shl(1u, r32(&c, g + 0x104u)));
    es2_sim_result r;
    if (CALL_R(&c, VA_RT_MEMSET, &r, r32(&c, g + 0xf0u), 0u, n)) { failed(sc); return; }
    sc->eax = r.eax; sc->edx = r.edx; sc->ecx = r.ecx;
}
void es2_sim_service_terrain_cell_mark_add(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    regs13 r;
    mark_add(&c, A(0), A(1), sc->ecx, &r);
    put13(sc, &r);
}
/* 0x428efc(n) (listing 0x428efc..0x428f1d): [0x49ac40] <- 0x49667c(0,
 * 0x1d, n as a sign-extended word, 1, 0x429655); EAX, EDX and ECX the
 * callee's. */
void es2_sim_service_binning_vector_new(es2_sim_service_call *sc, void *user) {
    (void)user; SERVE(sc, c);
    es2_sim_result r;
    if (CALL_R(&c, VA_VECTOR_NEW_13, &r, 0u, 0x1du, (uint32_t)(int32_t)(int16_t)A(0), 1u, VA_BIN_DTOR)) { failed(sc); return; }
    w32(&c, VA_BIN_VECTOR, r.eax);
    sc->eax = r.eax; sc->edx = r.edx; sc->ecx = r.ecx;
}
