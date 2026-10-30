/* The HUD and cockpit construction calls of the mission-load root 0x461608 as
 * own code (es2/sim/hud.h): state yes, pixels no. Contract: hud.md, "the HUD
 * line in the own process" (every store and call below with its listing range);
 * "listing" = llvm-objdump -d -M intel of the reference DBSIM.EXE (SHA-256
 * 8561aa47...c4ad). Hooks, other packages' services and the imports are called
 * through the dispatch; guest state only through the guest view. The frame
 * records of 0x49630c are not linked (own code; FS:0 is the same at the return
 * as in the original), their context words live on the original's stack only. */
#include "hud_internal.h"

/* ---- binding */
static es2_sim_hud *bound_hud;
static es2_sim_hud own_hud;
void es2_sim_hud_init(es2_sim_hud *hud, es2_sim_loop *loop) {
    memset(hud, 0, sizeof(*hud));
    hud->loop = loop;
}
void es2_sim_hud_bind(es2_sim_hud *hud) { bound_hud = hud; }
int es2_sim_hud_serving(es2_sim_service_call *sc, hud_ctx *c) {
    c->h = bound_hud;
    if (!c->h) {
        own_hud.loop = es2_sim_loop_bound();
        c->h = &own_hud;
    }
    if (!c->h->loop) { sc->refused = 1; sc->reason = "no main loop bound (H5-06)"; return 1; }
    c->l = c->h->loop;
    c->m = c->l->memory;
    return 0;
}

/* ---- guest VAs */
#define VA_TERRAIN 0x004a0be0u           /* the terrain grid T */
#define VA_MAP_BOUNDS 0x004aa640u        /* x0, y0, x1, y1 (dwords) */
#define VA_MAP_DESC 0x004d1ce8u          /* the map image descriptor: +0 h, +2 w, +4 bits per pixel, +5 flags, +6 size, +0xa bits */
#define VA_MAP_DESC2 0x004d1cfau         /* its copy */
#define VA_DISPLAY 0x004d3020u           /* D: +0 the DIB descriptor, +4 the surface */
#define VA_SURFACE 0x004d3024u
#define VA_GAME_W 0x004d253eu
#define VA_GAME_H 0x004d2542u
#define VA_FORCE_W 0x004a03c8u
#define VA_FORCE_H 0x004a03ccu
#define VA_CURRENT_SURFACE 0x006c5ed0u
#define VA_GC_CURRENT 0x006c5f70u
#define VA_CLIP_CURRENT 0x004a3614u
#define VA_BMP_8_MAP 0x0049d8ddu         /* "BMP_8" */
#define VA_BMP_8_DISPLAY 0x0049d8e9u     /* "BMP_8" */
#define VA_BITMAP_DESTROY 0x004815b8u
#define VA_GOURAUD_TRIANGLE 0x00486ee8u  /* gouraud.c's row fill_gouraud_triangle */

/* 0x48156c(obj) (listing 0x48156c..0x4815b6): the bitmap object's
 * constructor: [obj] <- 0x4a69f0, 0x4a3368, counter + 1, 0x4a3040,
 * counter + 1, 0x4a306c; +0xa, word +0x14, +0x16, +0xe <- 0; counter + 1. */
void hud_bitmap_init(hud_ctx *c, uint32_t o) {
    hw32(c, o, 0x004a69f0u);
    hw32(c, o, 0x004a3368u);
    hud_counter(c, 1);
    hw32(c, o, 0x004a3040u);
    hud_counter(c, 1);
    hw32(c, o, 0x004a306cu);
    hw32(c, o + 0xau, 0);
    hw16(c, o + 0x14u, 0);
    hw32(c, o + 0x16u, 0);
    hw32(c, o + 0xeu, 0);
    hud_counter(c, 1);
}

/* 0x480c34(O) (listing 0x480c34..0x480d43): a graphics context at O: [O]
 * <- 0x4a69f0, 0x4a3368, counter + 1 (the element constructors 0x48de2f
 * and 0x49545b on O+0x210/O+0x220 store nothing), [O] <- 0x4a2e10; the
 * current surface's first clip record p = [[0x6c5ed0]+0x40]: 0x81 dwords
 * to O+4, then O+4+{0x204, 0x208, 0x224, 0x228, 0x20c, 0x210, 0x214,
 * 0x218, 0x21c, 0x220}; byte O+0x230, dwords O+0x231, O+0x235 <- 0;
 * counter + 1. A NULL surface or record (the original reads through NULL)
 * is refused. */
int hud_gc_construct(hud_ctx *c, uint32_t o) {
    hw32(c, o, 0x004a69f0u);
    hw32(c, o, 0x004a3368u);
    hud_counter(c, 1);
    hw32(c, o, 0x004a2e10u);
    uint32_t s = hr32(c, VA_CURRENT_SURFACE);
    if (!s) return hud_null_refuse(c, 0x00480ec4u, "graphics context", "no current surface (the original reads through NULL)");
    uint32_t p = hr32(c, s + 0x40u), d = o + 4u;
    if (!p) return hud_null_refuse(c, 0x00480c91u, "graphics context", "no clip record (the original reads through NULL)");
    for (uint32_t i = 0; i < 0x81u; ++i) hw32(c, d + 4u * i, hr32(c, p + 4u * i));
    static const uint32_t tail[] = {0x204u, 0x208u, 0x224u, 0x228u, 0x20cu, 0x210u, 0x214u, 0x218u, 0x21cu, 0x220u};
    for (unsigned i = 0; i < sizeof(tail) / sizeof(tail[0]); ++i) hw32(c, d + tail[i], hr32(c, p + tail[i]));
    hw8(c, o + 0x230u, 0);
    hw32(c, o + 0x231u, 0);
    hw32(c, o + 0x235u, 0);
    hud_counter(c, 1);
    return 0;
}
/* 0x480d44(O) (listing 0x480d44..0x480d6e) with 0x480ecf: [0x6c5f70] <- O;
 * [s+0x44] <- O+4 for the current surface s and, s being the current
 * surface (0x48151d reads the same slot), [0x4a3614] <- O+4; then
 * 0x489558(O+4): [0x4a3614] <- O+4. */
void hud_gc_select(hud_ctx *c, uint32_t o) {
    hw32(c, VA_GC_CURRENT, o);
    uint32_t s = hr32(c, VA_CURRENT_SURFACE);
    hw32(c, s + 0x44u, o + 4u);
    if (s == hr32(c, VA_CURRENT_SURFACE)) hw32(c, VA_CLIP_CURRENT, o + 4u);
    hw32(c, VA_CLIP_CURRENT, o + 4u);
}

/* One surface of 0x44f6f4 (listing 0x44fa27..0x44fd11 and
 * 0x44ffd8..0x4502ca, the same sequence twice): new(0xa0) = s; when not
 * NULL 0x480f58(s, name), 0x49667c(s+0x48, 8, 4, 1, 0x49545b) and
 * (s+0x68, ...), _strncpy(s+0x90, name, 0xf), counter + 1 - 2; D+4 <- s;
 * the size forced to (w, h) when each is not negative; 0x481028(s, desc)
 * and on success the shared finish; 0x481224(D+4); 0x4813f8(D+4, 9, 0). */
static int map_surface(hud_ctx *c, uint32_t name, uint32_t desc, int32_t w, int32_t h) {
    uint32_t s = 0;
    if (HCALL(c, HUD_VA_NEW, &s, 0xa0u)) return 1;
    if (s) {
        if (es2_sim_gl_surface_construct(c->l, s, name)) return 1;
        if (HCALL(c, HUD_VA_VECTOR_NEW, NULL, s + 0x48u, 8u, 4u, 1u, HUD_VA_EMPTY_CTOR)) return 1;
        if (HCALL(c, HUD_VA_VECTOR_NEW, NULL, s + 0x68u, 8u, 4u, 1u, HUD_VA_EMPTY_CTOR)) return 1;
        if (HCALL(c, HUD_VA_STRNCPY, NULL, s + 0x90u, name, 0xfu)) return 1;
        hud_counter(c, 1);
        hud_counter(c, -2);
    }
    hw32(c, VA_SURFACE, s);
    if (w >= 0) hw32(c, VA_FORCE_W, (uint32_t)w);
    if (h >= 0) hw32(c, VA_FORCE_H, (uint32_t)h);
    s = hr32(c, VA_SURFACE);
    if (!s) return hud_null_refuse(c, 0x0044fb32u, "map set-up", "operator new returned NULL (the original goes on with a NULL surface)");
    uint32_t attached = 0, ignored = 0;
    if (es2_sim_gl_surface_create(c->l, s, desc, &attached)) return 1;
    if (attached && es2_sim_gl_surface_finish(c->l, s)) return 1;
    if (es2_sim_gl_surface_select(c->l, hr32(c, VA_SURFACE))) return 1;
    return es2_sim_gl_surface_set(c->l, hr32(c, VA_SURFACE), 9u, 0u, &ignored);
}

/* ======================================================================
 * 0x44f6f4 (listing 0x44f6f4..0x450309): the HDD map image and the
 * display surface, root step 10. T = [0x4a0be0], w = 1 << [T+0x100],
 * h = 1 << [T+0x104].
 *
 * 1. A local bitmap object L (0x48156c at f-1c) with the descriptor at
 *    f-18: words h' = w' = w (both from 1 << [T+0x100]), 8 bits, flags
 *    (& 0xf0 | 0x10), size w * w, bits new[](size). The loop
 *    0x44f79a..0x44f842 fills the bits with (min(height byte, 0x7f) / 8 +
 *    0x10) per cell (the map's colours).
 * 2. The map bounds [0x4aa640..0x4aa64c] widened by 60000 and shifted
 *    right (arithmetic) by word [T+0x108]; rows = y1 - y0 + 1, columns =
 *    x1 - x0 + 1; m = min(640 / columns, 400 / rows) (signed); word
 *    0x4d1cea <- columns * m, word 0x4d1ce8 <- rows * m (the map
 *    dimensions the map hooks read), byte 0x4d1cec <- 8, 0x4d1ced <- (&
 *    0xf0 | 0x10), 0x4d1cee <- their product, 0x4d1cf2 <- new[](it),
 *    _memset(it, 0, n); the copy at 0x4d1cfa (h, w, bits per pixel, the low
 *    nibble and bit 4 of the flags into the copy's flags, size, bits).
 * 3. The map surface on 0x4d1ce8 ("BMP_8", forced to its w and h), then a
 *    graphics context at f-258 (0x480c34; [O+0x208] <- 1, [O+0x20c] <- 0)
 *    selected after saving [0x6c5f70].
 * 4. The draw loop 0x44fd72..0x44ff7d: two 0x486ee8 triangles per map
 *  cell into the map surface (the Gouraud fill, rasterizer.md, "Couplings and
 *  headless reach" item 6).
 * 5. The map surface released: counter + 2 - 1, 0x480ffc(s, 0) (counter -
 *    1, 0x4811e8(s)), operator delete(s).
 * 6. The display surface again: "BMP_8" on the DIB descriptor [0x4d3020],
 *    forced to the game size [0x4d253e]/[0x4d2542].
 * 7. 0x480d44 of the saved context, counter - 2, 0x4815b8(L, 2) (frees the
 *    bits). EAX the frame record's saved head (FS:0 at entry), EDX/ECX as
 *    0x4815b8 leaves them.
 * A zero row or column count (the original's idiv faults) is refused. */
/* Step 4, the draw loop 0x44fd4e..0x44ff7d in the frame f (the original's EBP).
 * The GC's +0x22c <- the local dword f-0x2f8 (0). Rows r = y1 down to y0 + 1,
 * pixel rows from 0 in steps of m (top t, bottom b); columns k = x0 .. x1 - 1,
 * pixel columns from 0 in steps of m (left l, right r'); the colour pointer B =
 * bits + w * r + x0 (w the sign-extended word f-0x16), one further per column.
 * A cell outside 0 .. (1 << [T+0x104]) - 1 (rows) or 0 .. (1 << [T+0x100]) - 1
 * (columns) has the four colours 0; else c0 = [B]; with r < h (the word f-0x18)
 * c1 = [B + w] and, with k < w, c2 = [B + w + 1], c3 = [B + 1], else c2 = c3 =
 * c0; with r >= h c1 = c2 = c3 = c0 (the reads past the bitmap's last row are
 * the original's). Per cell: vector_new(f-0x36c, 8, 3, 1, 0x49545b), the points
 * {l, b}, {l, t}, {r', t} with the colour words c0, c1, c2 at f-0x300 to
 * 0x486ee8; then point 1 <- point 2, point 2 <- {r', b} (read back from the
 * frame as the listing does) with c0, c2, c3. */
static int map_cells(hud_ctx *c, uint32_t f, uint32_t t, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t m) {
    uint32_t gc = f - 0x258u, p = f - 0x36cu, colours = f - 0x300u;
    hw32(c, f - 0x2f8u, 0);
    hw32(c, gc + 0x22cu, f - 0x2f8u);
    int32_t w = (int32_t)hsx16(hr16(c, f - 0x16u)), h = (int32_t)hsx16(hr16(c, f - 0x18u));
    int32_t rows_limit = (int32_t)(1u << (hr32(c, t + 0x104u) & 31u)), columns_limit = (int32_t)(1u << (hr32(c, t + 0x100u) & 31u));
    uint32_t bits = hr32(c, f - 0xeu);
    uint32_t bottom = 0;
    for (int32_t r = y1; r > y0; --r) {
        uint32_t b = (uint32_t)w * (uint32_t)r + bits + (uint32_t)x0;
        uint32_t right = 0, top = bottom;
        bottom += (uint32_t)m;
        for (int32_t k = x0; k < x1; ++k, ++b) {
            uint32_t left = right;
            right += (uint32_t)m;
            uint16_t c0 = 0, c1 = 0, c2 = 0, c3 = 0;
            if (r >= 0 && r < rows_limit && k >= 0 && k < columns_limit) {
                c0 = hr8(c, b);
                if (r < h) {
                    c1 = hr8(c, b + (uint32_t)w);
                    if (w > k) {
                        c2 = hr8(c, b + (uint32_t)w + 1u);
                        c3 = hr8(c, b + 1u);
                    } else
                        c2 = c3 = c0;
                } else
                    c1 = c2 = c3 = c0;
            }
            if (HCALL(c, HUD_VA_VECTOR_NEW, NULL, p, 8u, 3u, 1u, HUD_VA_EMPTY_CTOR)) return 1;
            hw32(c, p, left);
            hw32(c, p + 4u, bottom);
            hw32(c, p + 8u, left);
            hw32(c, p + 12u, top);
            hw32(c, p + 16u, right);
            hw32(c, p + 20u, top);
            hw16(c, colours, c0);
            hw16(c, colours + 2u, c1);
            hw16(c, colours + 4u, c2);
            if (HCALL(c, VA_GOURAUD_TRIANGLE, NULL, p, colours)) return 1;
            hw32(c, p + 8u, hr32(c, p + 16u));
            hw32(c, p + 12u, hr32(c, p + 20u));
            hw32(c, p + 16u, right);
            hw32(c, p + 20u, bottom);
            hw16(c, colours, c0);
            hw16(c, colours + 2u, c2);
            hw16(c, colours + 4u, c3);
            if (HCALL(c, VA_GOURAUD_TRIANGLE, NULL, p, colours)) return 1;
        }
    }
    return 0;
}

static int map_setup(hud_ctx *c, es2_sim_result *out) {
    uint32_t t = hr32(c, VA_TERRAIN);
    if (!t) return hud_null_refuse(c, 0x0044f718u, "map set-up", "no terrain grid (the original reads through NULL)");
    uint32_t w = 1u << (hr32(c, t + 0x100u) & 31u);
    uint32_t top = c->l->stack_hi;
    uint32_t f = hud_frame_push(c, 0x380u);
    if (!f) return hud_refuse(c, 0x0044f6f4u, "map set-up", "the own code's call area is exhausted");
    f += 0x380u;   /* f = the original's EBP */
    uint32_t bitmap = f - 0x1cu;
    hud_bitmap_init(c, bitmap);
    hw16(c, f - 0x16u, (uint16_t)w);
    hw16(c, f - 0x18u, (uint16_t)w);
    hw8(c, f - 0x14u, 8);
    hw8(c, f - 0x13u, (uint8_t)((hr8(c, f - 0x13u) & 0xf0u) | 0x10u));
    hw32(c, f - 0x12u, hsx16(hr16(c, f - 0x16u)) * hsx16(hr16(c, f - 0x18u)));
    uint32_t bits = 0;
    if (HCALL(c, HUD_VA_NEW_ARRAY, &bits, hr32(c, f - 0x12u))) return 1;
    hw32(c, f - 0xeu, bits);
    /* Step 1's colours (0x44f786..0x44f842). Rows i and columns j both run to
     * the sign-extended word w; the cell index (i << [T+0x100]) + j outside 0
     * .. (1 << ([T+0x100] + [T+0x104])) - 1 makes the original read the byte at
     * address 0 (refused). The byte is the first of the 16-byte cell [T+0xec] +
     * 16 * index; from 0x80 on (unsigned) it is 0x7f; the colour is it / 8 +
     * 0x10. */
    {
        int32_t size = (int32_t)hsx16(hr16(c, f - 0x16u)), rows = (int32_t)hsx16(hr16(c, f - 0x18u));
        uint32_t s100 = hr32(c, t + 0x100u), limit = 1u << ((s100 + hr32(c, t + 0x104u)) & 31u);
        uint32_t cells = hr32(c, t + 0xecu), at = bits;
        for (int32_t i = 0; i < rows; ++i)
            for (int32_t j = 0; j < size; ++j) {
                int32_t index = (int32_t)(((uint32_t)i << (s100 & 31u)) + (uint32_t)j);
                if (index < 0 || (int32_t)limit <= index)
                    return hud_null_refuse(c, 0x0044f80cu, "map set-up", "a cell outside the terrain grid (the original reads address 0)");
                uint8_t v = hr8(c, cells + ((uint32_t)index << 4));
                if (v >= 0x80u) v = 0x7fu;
                hw8(c, at++, (uint8_t)(v / 8u + 0x10u));
            }
    }

    /* 32-bit wrapping sums: a coordinate within 60000 of 2^31 wraps (0x44f877..0x44f895) */
    int32_t x0 = (int32_t)(hr32(c, VA_MAP_BOUNDS) - 60000u), y0 = (int32_t)(hr32(c, VA_MAP_BOUNDS + 4u) - 60000u);
    int32_t x1 = (int32_t)(hr32(c, VA_MAP_BOUNDS + 8u) + 60000u), y1 = (int32_t)(hr32(c, VA_MAP_BOUNDS + 12u) + 60000u);
    unsigned shift = hr16(c, t + 0x108u) & 31u;
    x0 >>= shift; y0 >>= shift; x1 >>= shift; y1 >>= shift;
    int32_t rows = (int32_t)((uint32_t)y1 - (uint32_t)y0 + 1u), columns = (int32_t)((uint32_t)x1 - (uint32_t)x0 + 1u);
    if (!rows) return hud_divide_refuse(c, 0x0044f8f3u, 400, 0, "map set-up", "a zero map extent (the original's idiv faults)");
    if (!columns) return hud_divide_refuse(c, 0x0044f905u, 640, 0, "map set-up", "a zero map extent (the original's idiv faults)");
    int32_t by_rows = 400 / rows, by_columns = 640 / columns;
    int32_t m = by_columns >= by_rows ? by_rows : by_columns;
    hw16(c, VA_MAP_DESC + 2u, (uint16_t)(uint32_t)(columns * m));
    hw16(c, VA_MAP_DESC, (uint16_t)(uint32_t)(rows * m));
    hw8(c, VA_MAP_DESC + 4u, 8);
    hw8(c, VA_MAP_DESC + 5u, (uint8_t)((hr8(c, VA_MAP_DESC + 5u) & 0xf0u) | 0x10u));
    hw32(c, VA_MAP_DESC + 6u, hsx16(hr16(c, VA_MAP_DESC + 2u)) * hsx16(hr16(c, VA_MAP_DESC)));
    uint32_t image = 0;
    if (HCALL(c, HUD_VA_NEW_ARRAY, &image, hr32(c, VA_MAP_DESC + 6u))) return 1;
    hw32(c, VA_MAP_DESC + 0xau, image);
    if (HCALL(c, HUD_VA_MEMSET, NULL, hr32(c, VA_MAP_DESC + 0xau), 0u, hr32(c, VA_MAP_DESC + 6u))) return 1;
    hw16(c, VA_MAP_DESC2 + 2u, hr16(c, VA_MAP_DESC + 2u));
    hw16(c, VA_MAP_DESC2, hr16(c, VA_MAP_DESC));
    hw8(c, VA_MAP_DESC2 + 4u, hr8(c, VA_MAP_DESC + 4u));
    uint8_t flags = hr8(c, VA_MAP_DESC + 5u);
    hw8(c, VA_MAP_DESC2 + 5u, (uint8_t)((hr8(c, VA_MAP_DESC2 + 5u) & 0xf0u) | (flags & 0x0fu)));
    hw8(c, VA_MAP_DESC2 + 5u, (uint8_t)((hr8(c, VA_MAP_DESC2 + 5u) & 0xefu) | (((flags >> 4) & 1u) << 4)));
    hw32(c, VA_MAP_DESC2 + 6u, hr32(c, VA_MAP_DESC + 6u));
    hw32(c, VA_MAP_DESC2 + 0xau, hr32(c, VA_MAP_DESC + 0xau));

    if (map_surface(c, VA_BMP_8_MAP, VA_MAP_DESC, (int32_t)hsx16(hr16(c, VA_MAP_DESC + 2u)), (int32_t)hsx16(hr16(c, VA_MAP_DESC))))
        return 1;
    uint32_t gc = f - 0x258u;
    if (hud_gc_construct(c, gc)) return 1;
    uint32_t saved = hr32(c, VA_GC_CURRENT);
    hw32(c, f - 0x50u, 1);
    hw32(c, f - 0x4cu, 0);
    hud_gc_select(c, gc);
    if (map_cells(c, f, t, x0, y0, x1, y1, m)) return 1;

    uint32_t s = hr32(c, VA_SURFACE);
    if (s) {
        hud_counter(c, 2);
        hud_counter(c, -1);
        hud_counter(c, -1);   /* 0x480ffc */
        if (es2_sim_gl_surface_destroy(c->l, s)) return 1;
        if (HCALL(c, HUD_VA_DELETE, NULL, s)) return 1;
    }
    uint32_t display = hr32(c, VA_DISPLAY);
    if (map_surface(c, VA_BMP_8_DISPLAY, display, (int32_t)hr32(c, VA_GAME_W), (int32_t)hr32(c, VA_GAME_H))) return 1;
    hud_gc_select(c, saved);
    hud_counter(c, -1);
    hud_counter(c, -1);
    es2_sim_result r;
    if (hud_call_regs(c, VA_BITMAP_DESTROY, (const uint32_t[]){bitmap, 2u}, 2, NULL, &r)) return 1;
    hud_frame_pop(c, top);
    *out = r;
    out->eax = hud_fs0();
    return 0;
}
void es2_sim_service_hud_map_setup(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    es2_sim_result r;
    if (map_setup(&c, &r)) { hud_failed(sc); return; }
    sc->eax = r.eax; sc->edx = r.edx; sc->ecx = r.ecx;
}

/* ======================================================================
 * The cameras of root steps 14 and 17.
 *
 * 0x48bd84(K) (listing 0x48bd84..0x48bda3): 0x48bc40(K) ([K] <- 0x4a5b39,
 * the element constructors 0x4953c4/0x4922d3 on K+4/K+0x10 store nothing,
 * counter + 1), [K] <- 0x4a5b29, counter + 1. EAX K, ECX K (the POP ECX),
 * EDX K+0x10 (0x48bc40's LEA). */
void es2_sim_service_camera_ctor(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t k = sc->slots[0];
    hw32(&c, k, 0x004a5b39u);
    hud_counter(&c, 1);
    hw32(&c, k, 0x004a5b29u);
    hud_counter(&c, 1);
    sc->eax = k; sc->ecx = k; sc->edx = k + 0x10u;
}
/* 0x48bda4(K, a, b, d, v, w) (listing 0x48bda4..0x48bde0) with 0x48bc70
 * (0x48bc70..0x48bcc3): [K] <- 0x4a5b39; the dwords K+0xc, K+4, K+8 <-
 * [v+8], [v], [v+4]; K+0x10 <- [w], word K+0x14 <- word [w+4]; counter + 1;
 * [K] <- 0x4a5b29; K+0x1a <- b, K+0x1e <- d, K+0x16 <- a; counter + 1. EAX
 * K, EDX a, ECX the counter pointer [0x6cbee4]. */
void es2_sim_service_camera_main_ctor(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t k = sc->slots[0], a = sc->slots[1], b = sc->slots[2], d = sc->slots[3], v = sc->slots[4], w = sc->slots[5];
    hw32(&c, k, 0x004a5b39u);
    hw32(&c, k + 0xcu, hr32(&c, v + 8u));
    hw32(&c, k + 4u, hr32(&c, v));
    hw32(&c, k + 8u, hr32(&c, v + 4u));
    hw32(&c, k + 0x10u, hr32(&c, w));
    hw16(&c, k + 0x14u, hr16(&c, w + 4u));
    hud_counter(&c, 1);
    hw32(&c, k, 0x004a5b29u);
    hw32(&c, k + 0x1au, b);
    hw32(&c, k + 0x1eu, d);
    hw32(&c, k + 0x16u, a);
    hud_counter(&c, 1);
    sc->eax = k; sc->edx = a; sc->ecx = hr32(&c, HUD_VA_COUNTER_POINTER);
}
/* ======================================================================
 * Root step 16: the HUD set-up 0x4544c0 (listing 0x4544c0..0x454648).
 *
 * The one-shape loaders 0x453630 and 0x453684 (0x453630..0x453683,
 * 0x453684..0x4536d7; the same bytes but the frame descriptor): a VOL
 * stream at f-30 on the path (0x472f14), dts_load(stream, 0) into
 * [out], stream_destroy(stream, 2). */
#define VA_VOL_STREAM 0x00472f14u
#define VA_DTS_LOAD 0x0047a144u
#define VA_STREAM_DESTROY 0x00472f64u
#define VA_PATH_DIR 0x00492becu
#define VA_SURFACE_ATTRIBUTE 0x00481350u
#define VA_LOAD_BY_NAME 0x0045cdf4u
int hud_shape_one(hud_ctx *c, uint32_t out, uint32_t path) {
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0x50u);
    if (!f) return hud_refuse(c, 0x00453630u, "font load", "the own code's call area is exhausted");
    f += 0x50u;
    uint32_t s = f - 0x30u, x = 0;
    if (HCALL(c, VA_VOL_STREAM, NULL, s, path)) return 1;
    if (HCALL(c, VA_DTS_LOAD, &x, s, 0u)) return 1;
    hw32(c, out, x);
    if (HCALL(c, VA_STREAM_DESTROY, NULL, s, 2u)) return 1;
    hud_frame_pop(c, top);
    return 0;
}
/* ESI = 0x49de34 (the names "hfn", "dfn", "title", "cpylw", "active",
 * "pushed", "inactive", "green6x8", "red6x8", "corners", "alert", "hba",
 * "dba"), EDI = 0x4d1e18. The extension is "hfn" when byte [0x4d2537] is 3,
 * else "dfn"; the seven fonts title .. red6x8 into 0x4d1e18 + 4k
 * (0x492bec(name, ext), 0x453630); "corners" with the extension
 * [0x4a0a10] into 0x4d1e14 (0x453684); [0x4d1e6c], [0x4d1e68] <- 0;
 * [0x4d1e70] <- 0x481350(s, 0), [0x4d1e74] <- 0x481350(s, 1) for s =
 * [0x6c5ed0]; dword 0x4d1e80 <- (int16)(4 << word [0x4d253a]) and
 * 0x4d1e84 <- the same of word [0x4d253c] (16-bit shifts, count masked to
 * five bits); when [0x49dd88] is 0, 0x45cdf4(0x492bec("alert", byte
 * [0x4d256c] ? "hba" : "dba")) into it. EAX/EDX/ECX: 0x45cdf4's EAX and
 * EDX and the path (the POP ECX), else the last sign extension, its DX and
 * CX over the high words 0x481350 left. */
static int hud_setup(hud_ctx *c, es2_sim_result *out) {
    const uint32_t esi = 0x0049de34u, edi = 0x004d1e18u;
    uint32_t ext = (int8_t)hr8(c, 0x004d2537u) == 3 ? esi : esi + 4u, path = 0;
    static const uint8_t names[] = {0x8u, 0xeu, 0x14u, 0x1bu, 0x22u, 0x2bu, 0x34u};
    for (unsigned k = 0; k < sizeof(names); ++k) {
        if (HCALL(c, VA_PATH_DIR, &path, esi + names[k], ext)) return 1;
        if (hud_shape_one(c, edi + 4u * k, path)) return 1;
    }
    if (HCALL(c, VA_PATH_DIR, &path, esi + 0x3bu, hr32(c, 0x004a0a10u))) return 1;
    if (hud_shape_one(c, 0x004d1e14u, path)) return 1;
    uint32_t s = hr32(c, 0x006c5ed0u);
    hw32(c, 0x004d1e6cu, 0);
    hw32(c, 0x004d1e68u, 0);
    es2_sim_result r;
    if (hud_call_regs(c, VA_SURFACE_ATTRIBUTE, (const uint32_t[]){s, 0u}, 2, NULL, &r)) return 1;
    hw32(c, 0x004d1e70u, r.eax);
    if (hud_call_regs(c, VA_SURFACE_ATTRIBUTE, (const uint32_t[]){s, 1u}, 2, NULL, &r)) return 1;
    hw32(c, 0x004d1e74u, r.eax);
    uint16_t cx = hr16(c, 0x004d253au), dx = (uint16_t)(4u << (cx & 31u));
    hw32(c, 0x004d1e80u, hsx16(dx));
    cx = hr16(c, 0x004d253cu);
    dx = (uint16_t)(4u << (cx & 31u));
    hw32(c, 0x004d1e84u, hsx16(dx));
    out->eax = hsx16(dx);
    out->ecx = (r.ecx & 0xffff0000u) | cx;
    out->edx = (r.edx & 0xffff0000u) | dx;
    if (!hr32(c, 0x0049dd88u)) {
        uint32_t x = hr8(c, 0x004d256cu) ? esi + 0x49u : esi + 0x4du;
        if (HCALL(c, VA_PATH_DIR, &path, esi + 0x43u, x)) return 1;
        if (hud_call_regs(c, VA_LOAD_BY_NAME, (const uint32_t[]){path}, 1, NULL, &r)) return 1;
        hw32(c, 0x0049dd88u, r.eax);
        out->eax = r.eax;
        out->edx = r.edx;
        out->ecx = path;
    }
    return 0;
}
void es2_sim_service_hud_setup(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    es2_sim_result r;
    if (hud_setup(&c, &r)) { hud_failed(sc); return; }
    sc->eax = r.eax; sc->edx = r.edx; sc->ecx = r.ecx;
}
/* ======================================================================
 * The GLFont persistent class the HUD set-up's fonts load (dts_load
 * dispatches its factory by the class tag).
 *
 * Factory 0x4827d4(S, arg) (listing 0x4827d4..0x482864): P =
 * 0x47a2c8(0x32, [0x6c5830]); [P] <- 0x4a69f0, 0x4a3368, counter + 1,
 * 0x4a31ac, counter + 1, counter - 2; the read slot +0xc (P, S, arg). EAX
 * P, EDX the head at entry. A NULL P (the original calls through it) is
 * refused.
 *
 * Read 0x482a14(F, S) (0x482a14..0x482bd6), S.read(n, p) = S's slot +0x18
 * (through 0x492430, 0x482f21, 0x48f3ea, 0x482bd7, 0x482c8a, 0x491171):
 * the eleven words F+4 .. F+0x18 (2 bytes each), the dword F+0x1a (4);
 * word F+0x18 = n: F+0x1e <- 0x47a288(4n, [0x6c5830]) and read 4n there,
 * else 0; dword F+0x1a = m: F+0x22 <- arena(m), read m, else 0; word F+4
 * = k: F+0x26 <- arena(4k), read 4k, else 0; word F+0xc = -1: F+0x2a <-
 * arena(k), read k, else 0; word F+8 = -1: F+0x2e <- arena(2 word F+6),
 * read that, else 0 (the counts sign-extended). EAX the last read's. */
#define VA_ARENA_CURRENT 0x006c5830u
#define VA_ARENA_ALLOC 0x0047a288u
#define VA_ARENA_ALLOC_B 0x0047a2c8u
int hud_sread(hud_ctx *c, uint32_t s, uint32_t n, uint32_t buf, uint32_t *eax) {
    return HVCALL(c, s, 0x18u, eax, s, n, buf);
}
static int font_part(hud_ctx *c, uint32_t s, uint32_t f, uint32_t off, int nonzero, uint32_t n, uint32_t *eax) {
    if (!nonzero) { hw32(c, f + off, 0); return 0; }
    uint32_t b = 0;
    if (HCALL(c, VA_ARENA_ALLOC, &b, n, hr32(c, VA_ARENA_CURRENT))) return 1;
    hw32(c, f + off, b);
    return hud_sread(c, s, n, hr32(c, f + off), eax);
}
static int font_read(hud_ctx *c, uint32_t f, uint32_t s, uint32_t *eax) {
    for (uint32_t off = 4u; off <= 0x18u; off += 2u)
        if (hud_sread(c, s, 2u, f + off, eax)) return 1;
    if (hud_sread(c, s, 4u, f + 0x1au, eax)) return 1;
    uint16_t n = hr16(c, f + 0x18u);
    if (font_part(c, s, f, 0x1eu, n != 0, hsx16(n) << 2, eax)) return 1;
    uint32_t m = hr32(c, f + 0x1au);
    if (font_part(c, s, f, 0x22u, m != 0, m, eax)) return 1;
    uint16_t k = hr16(c, f + 4u);
    if (font_part(c, s, f, 0x26u, k != 0, hsx16(k) << 2, eax)) return 1;
    if (font_part(c, s, f, 0x2au, hr16(c, f + 0xcu) == 0xffffu, hsx16(hr16(c, f + 4u)), eax)) return 1;
    return font_part(c, s, f, 0x2eu, hr16(c, f + 8u) == 0xffffu, hsx16(hr16(c, f + 6u)) * 2u, eax);
}
void es2_sim_service_font_read(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t eax = sc->eax;
    if (font_read(&c, sc->slots[0], sc->slots[1], &eax)) { hud_failed(sc); return; }
    sc->eax = eax;
}
void es2_sim_service_font_new(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t head = hud_fs0(), p = 0;
    if (HCALL(&c, VA_ARENA_ALLOC_B, &p, 0x32u, hr32(&c, VA_ARENA_CURRENT))) { hud_failed(sc); return; }
    if (!p) { hud_null_refuse(&c, 0x0048284eu, "GLFont", "the allocation returned NULL (the original faults)"); hud_failed(sc); return; }
    hw32(&c, p, 0x004a69f0u);
    hw32(&c, p, 0x004a3368u);
    hud_counter(&c, 1);
    hw32(&c, p, 0x004a31acu);
    hud_counter(&c, 1);
    hud_counter(&c, -2);
    if (HVCALL(&c, p, 0xcu, NULL, p, sc->slots[0], sc->slots[1])) { hud_failed(sc); return; }
    sc->eax = p; sc->edx = head;
}
/* The holder class of vtable 0x4a3340 (an 8-byte header and one inner
 * object; the HUD's shape files hold them).
 *
 * Factory 0x483374(S, arg) (listing 0x483374..0x4833e5): P =
 * 0x47a2c8(0x10, [0x6c5830]); the constructor 0x48358c (0x48358c..0x4835c6):
 * [P] <- 0x4a69f0, 0x4a3368, counter + 1, 0x4a3340 (0x49545b(P+8) stores
 * nothing), P+4 <- 0, counter + 1; counter - 2; the read slot +0xc (P, S,
 * arg). EAX P, EDX the head at entry; NULL refused.
 * Read 0x48342c(P, S) (0x48342c..0x483457): S.read(8, P+8), P+4 <-
 * dts_load(S, 0). EAX the inner object. */
void es2_sim_service_holder_read(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t p = sc->slots[0], s = sc->slots[1], x = 0;
    if (hud_sread(&c, s, 8u, p + 8u, NULL) || HCALL(&c, VA_DTS_LOAD, &x, s, 0u)) { hud_failed(sc); return; }
    hw32(&c, p + 4u, x);
    sc->eax = x;
}
void es2_sim_service_holder_new(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t head = hud_fs0(), p = 0;
    if (HCALL(&c, VA_ARENA_ALLOC_B, &p, 0x10u, hr32(&c, VA_ARENA_CURRENT))) { hud_failed(sc); return; }
    if (!p) { hud_null_refuse(&c, 0x004833cfu, "holder", "the allocation returned NULL (the original faults)"); hud_failed(sc); return; }
    hw32(&c, p, 0x004a69f0u);
    hw32(&c, p, 0x004a3368u);
    hud_counter(&c, 1);
    hw32(&c, p, 0x004a3340u);
    hw32(&c, p + 4u, 0);
    hud_counter(&c, 1);
    hud_counter(&c, -2);
    if (HVCALL(&c, p, 0xcu, NULL, p, sc->slots[0], sc->slots[1])) { hud_failed(sc); return; }
    sc->eax = p; sc->edx = head;
}
/* ---- the load root's detail settings (root steps after the layout
 * set 0x42a1f0; hud.md, "the map draw's tail, the load root's last steps, the
 * radar draw and the per-frame row-1 walk"). */
/* 0x45d490(n) (listing 0x45d490..0x45d50a): the detail levels from the
 * setting n (the signed word of the argument). The first k < 5 with n <=
 * the word 0x49efcc[k]: [0x497368] <- the signed word 0x49efd6[k]; then the
 * first k < 5 with n <= the word 0x49efe0[k]: [0x49736c] <- the signed word
 * 0x49efea[k], [0x4a9868] <- the signed word 0x49eff4[k]. No match leaves
 * the global. EAX the last value stored (5 when the second search finds
 * nothing), EDX the second value or the table pointer, ECX the argument. */
void es2_sim_service_detail_levels(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    int16_t n = (int16_t)sc->slots[0];
    uint32_t k;
    for (k = 0; k < 5u; ++k)
        if (n <= (int16_t)hr16(&c, 0x0049efccu + 2u * k)) {
            hw32(&c, 0x00497368u, hsx16(hr16(&c, 0x0049efd6u + 2u * k)));
            break;
        }
    sc->eax = 5;
    sc->edx = 0x0049efeau;
    for (k = 0; k < 5u; ++k)
        if (n <= (int16_t)hr16(&c, 0x0049efe0u + 2u * k)) {
            sc->edx = hsx16(hr16(&c, 0x0049efeau + 2u * k));
            hw32(&c, 0x0049736cu, sc->edx);
            sc->eax = hsx16(hr16(&c, 0x0049eff4u + 2u * k));
            hw32(&c, 0x004a9868u, sc->eax);
            break;
        }
    sc->ecx = sc->slots[0];
}
/* 0x45d50c(n) (listing 0x45d50c..0x45d546): the first k < 5 with n (signed
 * word) <= the word 0x49effe[k]: [0x4a95b4] and [0x4a9dc4] <- the signed
 * word 0x49f004[k]. EAX and ECX that value, EDX 0x49f004 + 2k; no match:
 * EAX 0x49f008, ECX 5, EDX 0x49f00e, nothing stored. */
void es2_sim_service_detail_pools(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    int16_t n = (int16_t)sc->slots[0];
    for (uint32_t k = 0; k < 5u; ++k)
        if (n <= (int16_t)hr16(&c, 0x0049effeu + 2u * k)) {
            uint32_t v = hsx16(hr16(&c, 0x0049f004u + 2u * k));
            hw32(&c, 0x004a95b4u, v);
            hw32(&c, 0x004a9dc4u, v);
            sc->eax = sc->ecx = v;
            sc->edx = 0x0049f004u + 2u * k;
            return;
        }
    sc->eax = 0x0049f008u;
    sc->ecx = 5;
    sc->edx = 0x0049f00eu;
}
