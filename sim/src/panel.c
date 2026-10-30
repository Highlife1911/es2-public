/* The cockpit panel pieces outside gadgets.c, as own code: the scroll zones
 * 0x433770 the panel frame creates on its first pass. Contract: hud.md, "the
 * scroll zones and the radar page"; every function names its listing range of
 * the reference DBSIM.EXE (llvm-objdump -d -M intel, SHA-256 8561aa47...c4ad).
 *
 * Every guest read and write goes through the bound HUD's guest view in
 * the listing's order; every call of other code (operator new, the surface
 * attribute 0x481350) goes through the dispatch. */
#include "gadgets_internal.h"

#define VA_VIEW 0x006c5ed0u
#define VA_SHIFT_X 0x004d253au
#define VA_SHIFT_Y 0x004d253cu
#define VA_SURFACE_ATTRIBUTE 0x00481350u

/* 0x4524a0(W, rect) (listing 0x4524a0..0x4524cf), inlined here: byte
 * W+0x10 <- 0, W+0x17 <- 0x49dbb6, the 16 bytes of rect to W+0 (dword by
 * dword), byte W+0x1f <- 0. */
static void pn_widget_init(hud_ctx *c, uint32_t w, const uint32_t rect[4]) {
    hw8(c, w + 0x10u, 0);
    hw32(c, w + 0x17u, 0x0049dbb6u);
    for (uint32_t i = 0; i < 4u; ++i) hw32(c, w + 4u * i, rect[i]);
    hw8(c, w + 0x1fu, 0);
}
/* 0x452c6c(R, W) (listing 0x452c6c..0x452c8b), inlined: [R + 0x256 + 4 *
 * sx16(word R+0x254)] <- W, the word + 1 (no bound, as the original). EAX
 * R, ECX W. */
static void pn_widget_add(hud_ctx *c, uint32_t root, uint32_t w) {
    uint16_t n = hr16(c, root + 0x254u);
    hw32(c, root + 0x256u + 4u * hsx16(n), w);
    hw16(c, root + 0x254u, (uint16_t)(n + 1u));
}

/* 0x433770(H) (listing 0x433770..0x433965), the panel's three scroll
 * zones (ScrollTrigger, 0x24 bytes, vtable 0x49b7b4). The frame record
 * 0x49630c is the exception frame (stack only). Of the surface V =
 * [0x6c5ed0]: w = 0x481350(V, 0) - 1, h = 0x481350(V, 1) - 1, both used as
 * their sign-extended low words; sx = 5 << word [0x4d253a], sy = 3 <<
 * word [0x4d253c] (shift counts masked to 5 bits, the results as signed
 * words). The rectangles (x0, y0, x1, y1): the bottom (0, h - sy, w, h),
 * the left (0, 0, sx, h), the right (w - sx, 0, w, h). For each: Z =
 * new(0x24); when not NULL 0x4524a0(Z, rect), Z+0x17 <- 0x49defc, word
 * Z+0x1d <- 0, Z+0x17 <- 0x49b7b4, Z+0x20 <- H; then H+0x21a/+0x21e/+0x222
 * <- Z (NULL too) and 0x452c6c([H], [that field]) (the field re-read; [H]
 * read after the store). EAX [H] (0x452c6c's), ECX the last Z, EDX the
 * restored FS:0. */
static int scroll_zones(hud_ctx *c, uint32_t h, uint32_t *ecx) {
    uint32_t v = hr32(c, VA_VIEW), aw = 0, ah = 0;
    if (HCALL(c, VA_SURFACE_ATTRIBUTE, &aw, v, 0u)) return 1;
    if (HCALL(c, VA_SURFACE_ATTRIBUTE, &ah, v, 1u)) return 1;
    uint32_t sx = hsx16((uint16_t)(5u << (hr16(c, VA_SHIFT_X) & 31u)));
    uint32_t sy = hsx16((uint16_t)(3u << (hr16(c, VA_SHIFT_Y) & 31u)));
    uint32_t w = hsx16((uint16_t)(aw - 1u)), hh = hsx16((uint16_t)(ah - 1u));
    const uint32_t rects[3][4] = {{0, hh - sy, w, hh}, {0, 0, sx, hh}, {w - sx, 0, w, hh}};
    uint32_t z = 0;
    for (unsigned k = 0; k < 3u; ++k) {
        if (HCALL(c, HUD_VA_NEW, &z, 0x24u)) return 1;
        if (z) {
            pn_widget_init(c, z, rects[k]);
            hw32(c, z + 0x17u, 0x0049defcu);
            hw16(c, z + 0x1du, 0);
            hw32(c, z + 0x17u, 0x0049b7b4u);
            hw32(c, z + 0x20u, h);
        }
        uint32_t field = h + 0x21au + 4u * k;
        hw32(c, field, z);
        pn_widget_add(c, hr32(c, h), hr32(c, field));
    }
    *ecx = z;
    return 0;
}

/* ---- service rows */
void es2_sim_service_scroll_zones(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t h = sc->slots[0], ecx = 0;
    if (scroll_zones(&c, h, &ecx)) { hud_failed(sc); return; }
    sc->eax = hr32(&c, h);
    sc->ecx = ecx;
    sc->edx = hud_fs0();
}

/* ======== the radar page (MFDRadar, vtable 0x49c328) ======== */

#define VA_GC 0x006c5f70u
#define VA_PANEL 0x0049b088u
#define VA_DESC 0x004d24bcu          /* the display descriptor of the blits */
#define VA_MFD_BITMAPS 0x0049cbccu   /* -> {+8 the MFD bitmap table} */
#define VA_RECT_FILL 0x00486704u
#define VA_ELLIPSE 0x0048817cu
#define VA_POINT 0x004895ecu
#define VA_LINE 0x00483a04u          /* the span/line fill: no row yet */
#define VA_ROTATED_AT 0x0045caa4u    /* the rotated bitmap: no row yet */
#define VA_BLIT_AT 0x0045c8bcu
#define VA_LABEL 0x00438920u
#define VA_RADAR_UNITS 0x00434228u
#define VA_BLIP_SCAN 0x0043ec08u
#define VA_SINCOS 0x0047eb30u
#define VA_ROTATE2D 0x0047ffe8u
#define VA_STRLEN 0x00496766u

static unsigned pn_shx(const hud_ctx *c) { return hr16(c, VA_SHIFT_X) & 31u; }
static unsigned pn_shy(const hud_ctx *c) { return hr16(c, VA_SHIFT_Y) & 31u; }
static void pn_add(const hud_ctx *c, uint32_t a, uint32_t v) { hw32(c, a, hr32(c, a) + v); }
static void pn_copy8(const hud_ctx *c, uint32_t d, uint32_t s) {
    hw32(c, d, hr32(c, s));
    hw32(c, d + 4u, hr32(c, s + 4u));
}
/* idiv: the quotient, or the refusal of the original's #DE. */
static int pn_idiv(hud_ctx *c, uint32_t va, const char *name, uint32_t a, uint32_t b, uint32_t *q) {
    if (b == 0 || (a == 0x80000000u && b == 0xffffffffu))
        return hud_divide_refuse(c, va, (uint64_t)(int64_t)(int32_t)a, b, name, "an integer divide fault (#DE) in the original");
    *q = (uint32_t)((int32_t)a / (int32_t)b);
    return 0;
}
/* A call of a graphics function another file reconstructs (the line 0x483a04
 * of poly.c, the rotated bitmap 0x45caa4 of mfd.c) through the dispatch; both
 * have rows, nothing is counted headless. */
static int pn_through(hud_ctx *c, uint32_t va, const uint32_t *a, unsigned n) { return hud_call(c, va, a, n, NULL); }
/* Byte [0x4d2537] = 3 and byte [0x4d256c] = 0 (the second read only on
 * the first's match): the descriptor shifts apply to the bitmap size. */
static int pn_scaled(const hud_ctx *c) { return hr8(c, 0x004d2537u) == 3u && !hr8(c, 0x004d256cu); }
/* The four-digit fields of the radar draw (0x43f209..0x43f262 and
 * 0x43f278..0x43f2cd): _itoa(n, s, 10), its length len (0x496766), the
 * len + 1 bytes from s+len copied downwards to s+4 (byte by byte, the
 * source re-read after the overlapping writes), then the digit zero from
 * there down to s; then the label 0x438920(L, s). */
static int pn_digits(hud_ctx *c, uint32_t l, uint32_t n, uint32_t s) {
    uint32_t len = 0;
    if (HCALL(c, HUD_VA_ITOA, NULL, n, s, 10u) || HCALL(c, VA_STRLEN, &len, s)) return 1;
    uint32_t d = s + 4u, src = s + len;
    for (uint32_t i = 0; (int32_t)i < (int32_t)(len + 1u); ++i) hw8(c, d--, hr8(c, src--));
    for (; d >= s; --d) hw8(c, d, 0x30u);
    return HCALL(c, VA_LABEL, NULL, l, s);
}

/* 0x43eef4(R) (listing 0x43eef4..0x43f2d6), MFDRadar slot 0, the radar
 * draw. The locals sit in a frame at the listing's offsets (L(k) =
 * EBP - k); G = [0x6c5f70] is read once at entry and kept (L10).
 *   L18 <- 0, L20 <- 0, L28 <- 4; G+0x228 <- &L18, G+0x22c <- &L20, L1c <-
 *   0x11; the background 0x486704(R+0x48).
 *   R+0x30 <- [[R+8]+0xc]; R+0x2c <- (R+0x30 idiv [R+0x28]) sar word
 *   [0x4d253a].
 *   The heading a = slot +0x3c of P = [[0x49b088]+0x203] (P), its low word
 *   plus 0xa000 (the high word kept). The sweep size from the bitmap B =
 *   [R+0x14]: y = word B+4 (shifted left by word [0x4d253c] when scaled),
 *   x = word B+6 (by word [0x4d253a] when scaled), both 16-bit shifts,
 *   sign-extended; L34 <- x, L30 <- y; 0x45caa4(0x4d24bc, [R+0x14], R+0x20,
 *   0, &L34, a).
 *   The frame 0x45c8bc(0x4d24bc, [R+0x10], R+0x18).
 *   With [R+0x34] = 1 and [R+0x30] > 0x222e0 (signed): q = 0x222e0 idiv
 *   [R+0x2c]; the ring (x - q, y - q, x + q, y + q) around R+0x20 at L58,
 *   G+0x22c <- &L28, L24 <- the word [0x4d3b92]; 0x48817c(&L58).
 *   The bitmaps +0x44 and +0x48 of [[0x49cbcc]+8] at R+0x58 and R+0x60.
 *   The blips R+0x68 (stride 0xc) while i < [R+0x518] (signed, re-read):
 *   (x, y) idiv [R+0x2c] (read once per blip) plus R+0x20/R+0x24 at L8;
 *   with the low byte of word [0x4d253a] set: the rectangle (x, y, x + 1,
 *   y + 1) at L68, G+0x22c <- &L20, L1c <- the colour [+8], 0x486704(&L68);
 *   else G+0x228 <- &L18, L14 <- the colour, 0x4895ec(&L8).
 *   A target T = [R+0x51c]: (x, y) idiv [R+0x2c] plus R+0x20/R+0x24 less 2
 *   << word [0x4d253a], at L8; the bitmap +0x40 there (0x45c8bc).
 *   The labels 0x438920: [R+0x38] with [0x4d1630]; [R+0x3c] with
 *   0x434228([R+0x30]) as four digits; [R+0x40] with [0x4d1634]; [R+0x44]
 *   with [R+0x520] as four digits (pn_digits, s = &L48).
 * Refused: a #DE of any idiv, a NULL sweep bitmap (the original reads
 * through it). */
int es2_sim_panel_radar_draw(hud_ctx *c, uint32_t r) {
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0x68u);
    if (!f) return hud_refuse(c, 0x0043eef4u, "radar draw", "the own code's call area is exhausted");
    uint32_t e = f + 0x68u;
#define L(k) (e - (uint32_t)(k))
    int bad = 1;
    uint32_t v = hr32(c, VA_VIEW), g = hr32(c, VA_GC), q = 0;
    hw32(c, L(0xc), v);
    hw32(c, L(0x10), g);
    hw32(c, L(0x18), 0);
    hw32(c, L(0x20), 0);
    hw32(c, L(0x28), 4u);
    hw32(c, g + 0x228u, L(0x18));
    hw32(c, g + 0x22cu, L(0x20));
    hw32(c, L(0x1c), 0x11u);
    if (HCALL(c, VA_RECT_FILL, NULL, r + 0x48u)) goto done;
    uint32_t range = hr32(c, hr32(c, r + 8u) + 0xcu);
    hw32(c, r + 0x30u, range);
    unsigned sh = pn_shx(c);
    if (pn_idiv(c, 0x0043ef5eu, "radar draw", range, hr32(c, r + 0x28u), &q)) goto done;
    hw32(c, r + 0x2cu, (uint32_t)((int32_t)q >> sh));
    uint32_t p = hr32(c, hr32(c, VA_PANEL) + 0x203u), a = 0;
    if (HVCALL(c, p, 0x3cu, &a, p)) goto done;
    a = (a & 0xffff0000u) | (uint16_t)(a + 0xa000u);
    uint32_t b = hr32(c, r + 0x14u);
    if (!b) {
        hud_null_refuse(c, 0x0043ef82u, "radar draw", "no sweep bitmap (the original reads through NULL)");
        goto done;
    }
    uint16_t ys = hr16(c, b + 4u);
    if (pn_scaled(c)) ys = (uint16_t)(ys << pn_shy(c));
    uint16_t xs = hr16(c, hr32(c, r + 0x14u) + 6u);
    if (pn_scaled(c)) xs = (uint16_t)(xs << pn_shx(c));
    hw32(c, L(0x2c), hsx16(xs));
    hw32(c, L(0x34), hsx16(xs));
    hw32(c, L(0x30), hsx16(ys));
    if (pn_through(c, VA_ROTATED_AT, (const uint32_t[]){VA_DESC, hr32(c, r + 0x14u), r + 0x20u, 0u, L(0x34), a}, 6)) goto done;
    if (HCALL(c, VA_BLIT_AT, NULL, VA_DESC, hr32(c, r + 0x10u), r + 0x18u)) goto done;
    if (hr32(c, r + 0x34u) == 1u && (int32_t)hr32(c, r + 0x30u) > 0x222e0) {
        if (pn_idiv(c, 0x0043f048u, "radar draw", 0x222e0u, hr32(c, r + 0x2cu), &q)) goto done;
        pn_copy8(c, L(0x50), r + 0x20u);
        pn_copy8(c, L(0x58), L(0x50));
        pn_add(c, L(0x58), 0u - q);
        pn_add(c, L(0x54), 0u - q);
        pn_add(c, L(0x50), q);
        pn_add(c, L(0x4c), q);
        hw32(c, hr32(c, L(0x10)) + 0x22cu, L(0x28));
        hw32(c, L(0x24), hsx16(hr16(c, 0x004d3b92u)));
        if (HCALL(c, VA_ELLIPSE, NULL, L(0x58))) goto done;
    }
    if (HCALL(c, VA_BLIT_AT, NULL, VA_DESC, hr32(c, hr32(c, hr32(c, VA_MFD_BITMAPS) + 8u) + 0x44u), r + 0x58u) ||
        HCALL(c, VA_BLIT_AT, NULL, VA_DESC, hr32(c, hr32(c, hr32(c, VA_MFD_BITMAPS) + 8u) + 0x48u), r + 0x60u))
        goto done;
    uint8_t dots = (uint8_t)hr16(c, VA_SHIFT_X);
    uint32_t ent = r + 0x68u;
    for (uint32_t i = 0; (int32_t)i < (int32_t)hr32(c, r + 0x518u); ++i, ent += 0xcu) {
        pn_copy8(c, L(8), ent);
        uint32_t s = hr32(c, r + 0x2cu);
        if (pn_idiv(c, 0x0043f0fcu, "radar draw", hr32(c, L(8)), s, &q)) goto done;
        hw32(c, L(8), q);
        if (pn_idiv(c, 0x0043f108u, "radar draw", hr32(c, L(4)), s, &q)) goto done;
        hw32(c, L(4), q);
        pn_add(c, L(8), hr32(c, r + 0x20u));
        pn_add(c, L(4), hr32(c, r + 0x24u));
        if (dots) {
            pn_copy8(c, L(0x60), L(8));
            pn_copy8(c, L(0x68), L(0x60));
            pn_add(c, L(0x60), 1u);
            pn_add(c, L(0x5c), 1u);
            hw32(c, hr32(c, L(0x10)) + 0x22cu, L(0x20));
            hw32(c, L(0x1c), hr32(c, ent + 8u));
            if (HCALL(c, VA_RECT_FILL, NULL, L(0x68))) goto done;
        } else {
            hw32(c, hr32(c, L(0x10)) + 0x228u, L(0x18));
            hw32(c, L(0x14), hr32(c, ent + 8u));
            if (HCALL(c, VA_POINT, NULL, L(8))) goto done;
        }
    }
    if (hr32(c, r + 0x51cu)) {
        pn_copy8(c, L(8), hr32(c, r + 0x51cu));
        uint32_t s = hr32(c, r + 0x2cu);
        if (pn_idiv(c, 0x0043f1aau, "radar draw", hr32(c, L(8)), s, &q)) goto done;
        hw32(c, L(8), q);
        if (pn_idiv(c, 0x0043f1b3u, "radar draw", hr32(c, L(4)), s, &q)) goto done;
        hw32(c, L(4), q);
        pn_add(c, L(8), hr32(c, r + 0x20u));
        pn_add(c, L(4), hr32(c, r + 0x24u));
        uint32_t d = 2u << pn_shx(c);
        pn_add(c, L(8), 0u - d);
        pn_add(c, L(4), 0u - d);
        if (HCALL(c, VA_BLIT_AT, NULL, VA_DESC, hr32(c, hr32(c, hr32(c, VA_MFD_BITMAPS) + 8u) + 0x40u), L(8))) goto done;
    }
    uint32_t units = 0;
    if (HCALL(c, VA_LABEL, NULL, hr32(c, r + 0x38u), hr32(c, 0x004d1630u)) ||
        HCALL(c, VA_RADAR_UNITS, &units, hr32(c, r + 0x30u)) || pn_digits(c, hr32(c, r + 0x3cu), units, L(0x48)) ||
        HCALL(c, VA_LABEL, NULL, hr32(c, r + 0x40u), hr32(c, 0x004d1634u)) ||
        pn_digits(c, hr32(c, r + 0x44u), hr32(c, r + 0x520u), L(0x48)))
        goto done;
    bad = 0;
done:
#undef L
    hud_frame_pop(c, top);
    return bad;
}

/* 0x43f2d8(R, pt) (listing 0x43f2d8..0x43f5f4), the HUD radar of the
 * MFDRadar R at the point pt; nothing when the MFD M = [[0x49b088]+0x1ed]
 * shows R (R = [M+0xcd + 4 [M+0xc1]]). Locals at the listing's offsets
 * (L(k) = EBP - k); G = [0x6c5f70] read once after the scan (L4).
 *   The blip scan 0x43ec08(R).
 *   Lc <- 0, L14 <- 0, L1c <- 4; G+0x228 <- &Lc, G+0x22c <- &L1c.
 *   w = 0x2e << word [0x4d253a], h = 0x2e << word [0x4d253c] (32-bit
 *   shifts); the circle (pt, pt + (w, h)) at L64; the centre (L24, L20) =
 *   pt + (w sar 1, h sar 1), L18 <- the word [0x4d3b8e]; 0x48817c(&L64).
 *   The heading a = slot +0x3c of P = [[0x49b088]+0x203] (P). L3c <- 0,
 *   L38 <- -(h sar 1), L8 <- the word [0x4d3b8e]. Two view lines, for a +
 *   0xe000 and a + 0x2000 (32-bit adds): 0x47eb30(angle, &L34),
 *   0x47ffe8(&L3c, &L34, &L44), L44/L40 plus the centre, the line
 *   0x483a04(&L24, &L44).
 *   The frame 0x45c8bc(0x4d24bc, [[0x49cbcc]+8]+0x48, &L3c) at the centre
 *   less (3 << word [0x4d253a], 0).
 *   R+0x2c <- [R+0x30] idiv (h sar 1); G+0x22c <- &L14.
 *   The blips R+0x68 (stride 0xc) while i < [R+0x518] (signed, re-read):
 *   (x, y) idiv [R+0x2c] (read once per blip) plus the centre, at L4c;
 *   with the low byte of word [0x4d253a] set: the square (x - 2, y - 2,
 *   x + 2, y + 2) at L74 with L10 <- the word [0x4d3ba2], 0x48817c(&L74),
 *   then shrunk by one on each side with L10 <- the colour [+8],
 *   0x48817c(&L74); else G+0x228 <- &Lc, L8 <- the colour, 0x4895ec(&L4c).
 *   A target T = [R+0x51c]: (x, y) idiv [R+0x2c] plus the centre less 2 <<
 *   word [0x4d253a], at L4c; the bitmap [[0x49cbcc]+8]+0x40 there
 *   (0x45c8bc).
 * Refused: a #DE of any idiv. */
int es2_sim_panel_hud_radar(hud_ctx *c, uint32_t r, uint32_t pt) {
    uint32_t m = hr32(c, hr32(c, VA_PANEL) + 0x1edu);
    if (r == hr32(c, m + 0xcdu + 4u * hr32(c, m + 0xc1u))) return 0;
    if (HCALL(c, VA_BLIP_SCAN, NULL, r)) return 1;
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0x74u);
    if (!f) return hud_refuse(c, 0x0043f2d8u, "HUD radar", "the own code's call area is exhausted");
    uint32_t e = f + 0x74u;
#define L(k) (e - (uint32_t)(k))
    int bad = 1;
    uint32_t q = 0;
    hw32(c, L(4), hr32(c, VA_GC));
    hw32(c, L(0xc), 0);
    hw32(c, L(0x14), 0);
    hw32(c, L(0x1c), 4u);
    hw32(c, hr32(c, L(4)) + 0x228u, L(0xc));
    hw32(c, hr32(c, L(4)) + 0x22cu, L(0x1c));
    uint32_t h = 0x2eu << pn_shy(c), w = 0x2eu << pn_shx(c);
    hw32(c, L(0x2c), w);
    hw32(c, L(0x28), h);
    pn_copy8(c, L(0x5c), pt);
    pn_copy8(c, L(0x64), L(0x5c));
    pn_add(c, L(0x5c), hr32(c, L(0x2c)));
    pn_add(c, L(0x58), h);
    hw32(c, L(0x2c), (uint32_t)((int32_t)hr32(c, L(0x2c)) >> 1));
    hw32(c, L(0x28), (uint32_t)((int32_t)hr32(c, L(0x28)) >> 1));
    hw32(c, L(0x24), hr32(c, L(0x64)) + hr32(c, L(0x2c)));
    uint32_t colour = hsx16(hr16(c, 0x004d3b8eu));
    hw32(c, L(0x20), hr32(c, L(0x60)) + hr32(c, L(0x28)));
    hw32(c, L(0x18), colour);
    if (HCALL(c, VA_ELLIPSE, NULL, L(0x64))) goto done;
    uint32_t p = hr32(c, hr32(c, VA_PANEL) + 0x203u), a = 0;
    if (HVCALL(c, p, 0x3cu, &a, p)) goto done;
    colour = hsx16(hr16(c, 0x004d3b8eu));
    hw32(c, L(0x3c), 0);
    hw32(c, L(0x38), 0u - hr32(c, L(0x28)));
    hw32(c, L(8), colour);
    for (unsigned k = 0; k < 2u; ++k) {
        if (HCALL(c, VA_SINCOS, NULL, a + (k ? 0x2000u : 0xe000u), L(0x34)) ||
            HCALL(c, VA_ROTATE2D, NULL, L(0x3c), L(0x34), L(0x44)))
            goto done;
        pn_add(c, L(0x44), hr32(c, L(0x24)));
        pn_add(c, L(0x40), hr32(c, L(0x20)));
        if (pn_through(c, VA_LINE, (const uint32_t[]){L(0x24), L(0x44)}, 2)) goto done;
    }
    unsigned sh = pn_shx(c);
    hw32(c, L(0x3c), hr32(c, L(0x24)));
    hw32(c, L(0x38), hr32(c, L(0x20)));
    uint32_t tb = hr32(c, VA_MFD_BITMAPS);
    pn_add(c, L(0x3c), 0u - (3u << sh));
    if (HCALL(c, VA_BLIT_AT, NULL, VA_DESC, hr32(c, hr32(c, tb + 8u) + 0x48u), L(0x3c))) goto done;
    uint8_t dots = (uint8_t)hr16(c, VA_SHIFT_X);
    if (pn_idiv(c, 0x0043f4abu, "HUD radar", hr32(c, r + 0x30u), hr32(c, L(0x28)), &q)) goto done;
    hw32(c, r + 0x2cu, q);
    hw32(c, hr32(c, L(4)) + 0x22cu, L(0x14));
    uint32_t ent = r + 0x68u;
    for (uint32_t i = 0; (int32_t)i < (int32_t)hr32(c, r + 0x518u); ++i, ent += 0xcu) {
        pn_copy8(c, L(0x4c), ent);
        uint32_t s = hr32(c, r + 0x2cu);
        if (pn_idiv(c, 0x0043f4dbu, "HUD radar", hr32(c, L(0x4c)), s, &q)) goto done;
        hw32(c, L(0x4c), q);
        if (pn_idiv(c, 0x0043f4e4u, "HUD radar", hr32(c, L(0x48)), s, &q)) goto done;
        hw32(c, L(0x48), q);
        pn_add(c, L(0x4c), hr32(c, L(0x24)));
        pn_add(c, L(0x48), hr32(c, L(0x20)));
        if (dots) {
            pn_copy8(c, L(0x6c), L(0x4c));
            pn_copy8(c, L(0x74), L(0x6c));
            pn_add(c, L(0x74), 0u - 2u);
            pn_add(c, L(0x70), 0u - 2u);
            pn_add(c, L(0x6c), 2u);
            pn_add(c, L(0x68), 2u);
            hw32(c, L(0x10), hsx16(hr16(c, 0x004d3ba2u)));
            if (HCALL(c, VA_ELLIPSE, NULL, L(0x74))) goto done;
            pn_add(c, L(0x74), 1u);
            pn_add(c, L(0x70), 1u);
            pn_add(c, L(0x6c), 0u - 1u);
            pn_add(c, L(0x68), 0u - 1u);
            hw32(c, L(0x10), hr32(c, ent + 8u));
            if (HCALL(c, VA_ELLIPSE, NULL, L(0x74))) goto done;
        } else {
            hw32(c, hr32(c, L(4)) + 0x228u, L(0xc));
            hw32(c, L(8), hr32(c, ent + 8u));
            if (HCALL(c, VA_POINT, NULL, L(0x4c))) goto done;
        }
    }
    uint32_t t = hr32(c, r + 0x51cu);
    if (t) {
        pn_copy8(c, L(0x4c), t);
        uint32_t s = hr32(c, r + 0x2cu);
        if (pn_idiv(c, 0x0043f5a2u, "HUD radar", hr32(c, L(0x4c)), s, &q)) goto done;
        hw32(c, L(0x4c), q);
        if (pn_idiv(c, 0x0043f5abu, "HUD radar", hr32(c, L(0x48)), s, &q)) goto done;
        hw32(c, L(0x48), q);
        pn_add(c, L(0x4c), hr32(c, L(0x24)));
        pn_add(c, L(0x48), hr32(c, L(0x20)));
        uint32_t d = 2u << pn_shx(c);
        pn_add(c, L(0x4c), 0u - d);
        pn_add(c, L(0x48), 0u - d);
        if (HCALL(c, VA_BLIT_AT, NULL, VA_DESC, hr32(c, hr32(c, hr32(c, VA_MFD_BITMAPS) + 8u) + 0x40u), L(0x4c))) goto done;
    }
    bad = 0;
done:
#undef L
    hud_frame_pop(c, top);
    return bad;
}

/* 0x43eedc(R) (listing 0x43eedc..0x43eef2), MFDRadar slot +4: the blip
 * scan 0x43ec08(R), then R's slot 0 (the draw, 0x43eef4) with R. EAX the
 * slot's, ECX R (its popped argument). */
void es2_sim_service_radar_update(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t r = sc->slots[0], eax = 0;
    if (HCALL(&c, VA_BLIP_SCAN, NULL, r) || HVCALL(&c, r, 0u, &eax, r)) { hud_failed(sc); return; }
    sc->eax = eax;
    sc->ecx = r;
}

/* ======================================================================
 * The bar draws with their fills, the LED segments, the shields gauge's frame
 * with its power-on sweep and balance numbers, the energy pool's frame, the
 * MFD's Alt order keys, the system button's action and its event base, and the
 * scroll trigger's event (cockpit.md, "bar and gauge family, power-on sweeps,
 * balance numbers"). Every function names its listing range of the reference
 * DBSIM.EXE (llvm-objdump -d -M intel, SHA-256 8561aa47...c4ad); every call
 * goes through the dispatch in the listing's order. */

#define VA_PANEL_VISIBLE 0x00431410u
#define VA_WIDGET_PLUS4 0x004390b8u
#define VA_LED 0x00439758u
#define VA_SHIELD_PALETTE 0x00443918u
#define VA_SHIELD_NUMBERS 0x00444a90u
#define VA_GC_SELECT 0x00480d44u
#define VA_PANEL_COPY 0x0043122cu
#define VA_MENU_CODE 0x0043f9c0u
#define VA_MFD_ORDER 0x00447248u
#define VA_MFD_ORDER_KEY 0x00447158u
#define VA_PLAYBACK_WORD 0x004d24d6u
#define VA_DISPLAY_CURRENT 0x00429820u
#define VA_VIEW_SET 0x0042a3f4u

#define PN_ROW(name) void es2_sim_service_##name(es2_sim_service_call *sc, void *user)
#define PN_ROW_RUN(expr) do { (void)user; HSERVE(sc, c); uint32_t eax = sc->eax; \
    if (expr) { hud_failed(sc); return; } sc->eax = eax; } while (0)

/* The bar draws, slot 0 of [B] with (B, value), sharing one body (kind 0:
 * 0x439118, listing 0x439118..0x4391dc; kind 1: 0x439398, 0x439398..
 * 0x43945c; kind 2: 0x4395e8, 0x4395e8..0x4396a4). The pen P =
 * [[[0x6c5ed0]+0x44]+0x228] and its colour [P+4] are read on entry; k =
 * [B+0x14]. Forward (kinds 0 and 2: k > 0; kind 1: k < 0): e = ((signed
 * word value * k) sar 16) + [B+0x18]; the rectangle B+4 = ([B+0x18], .,
 * e, .) is filled (0x486704) in colour [B+0x28], or for kind 2 drawn as
 * LED segments 0x439758(B, B+4) without a colour; then B+4 <- e + 1,
 * B+0xc <- [B+0x1c] filled in [B+0x24]. Backward: e = [B+0x1c] - (value
 * * [B+0x14] sar 16); B+0xc <- [B+0x1c], B+4 <- e, the same first part;
 * then [B+0x24], B+0xc <- e - 1, B+4 <- [B+0x18], filled, and the colour
 * restored once more than on the forward path. Then [P+4] <- the old
 * colour, B+0x20 <- e. EAX the last fill's. */
int es2_sim_panel_bar_draw(hud_ctx *c, uint32_t b, uint32_t value, unsigned kind, uint32_t *eax) {
    uint32_t p = hr32(c, hr32(c, hr32(c, VA_VIEW) + 0x44u) + 0x228u);
    uint32_t old = hr32(c, p + 4u);
    int32_t k = (int32_t)hr32(c, b + 0x14u);
    int forward = kind == 1u ? k < 0 : k > 0;
    uint32_t v = hsx16((uint16_t)value), e, r = b + 4u;
    if (forward) {
        uint32_t s = (uint32_t)((int32_t)(v * (uint32_t)k) >> 16);
        if (kind == 2u) {
            uint32_t x0 = hr32(c, b + 0x18u);
            e = s + hr32(c, b + 0x18u);
            hw32(c, b + 4u, x0);
            hw32(c, b + 0xcu, e);
            if (HCALL(c, VA_LED, eax, b, r)) return 1;
        } else {
            uint32_t colour = hr32(c, b + 0x28u);
            e = s + hr32(c, b + 0x18u);
            hw32(c, p + 4u, colour);
            hw32(c, b + 4u, hr32(c, b + 0x18u));
            hw32(c, b + 0xcu, e);
            if (HCALL(c, VA_RECT_FILL, eax, r)) return 1;
        }
        hw32(c, p + 4u, hr32(c, b + 0x24u));
        hw32(c, b + 4u, e + 1u);
        hw32(c, b + 0xcu, hr32(c, b + 0x1cu));
        if (HCALL(c, VA_RECT_FILL, eax, r)) return 1;
    } else {
        uint32_t s = (uint32_t)((int32_t)(v * hr32(c, b + 0x14u)) >> 16);
        if (kind == 2u) {
            uint32_t x1 = hr32(c, b + 0x1cu);
            e = x1 - s;
            hw32(c, b + 0xcu, x1);
            hw32(c, b + 4u, e);
            if (HCALL(c, VA_LED, eax, b, r)) return 1;
        } else {
            e = hr32(c, b + 0x1cu);
            uint32_t colour = hr32(c, b + 0x28u);
            e -= s;
            hw32(c, p + 4u, colour);
            hw32(c, b + 0xcu, hr32(c, b + 0x1cu));
            hw32(c, b + 4u, e);
            if (HCALL(c, VA_RECT_FILL, eax, r)) return 1;
        }
        hw32(c, p + 4u, hr32(c, b + 0x24u));
        hw32(c, b + 0xcu, e - 1u);
        hw32(c, b + 4u, hr32(c, b + 0x18u));
        if (HCALL(c, VA_RECT_FILL, eax, r)) return 1;
        hw32(c, p + 4u, old);
    }
    hw32(c, p + 4u, old);
    hw32(c, b + 0x20u, e);
    return 0;
}

/* 0x439758(B, rect) (listing 0x439758..0x439830), the LED segments: the
 * 0x24-byte frame holds (from its bottom) p2 = (x, y1), p1 = (x, y0), x0,
 * the saved pen, the context G', and the pen record {0, colour}. G' =
 * [V+0x44] - 4 when [V+0x44] is not 0 (V = [0x6c5ed0]); its pen [G'+0x228]
 * is saved and pointed at the record. Two passes of vertical lines
 * 0x483a04(&p1, &p2), x stepping by 2 while x1 >= x (signed): colour
 * [B+0x2c] from the odd x at or after x0, then [B+0x30] from the even one.
 * x1 is read once at entry. The pen of G' is restored; EAX G'. */
static int pn_led(hud_ctx *c, uint32_t b, uint32_t rect, uint32_t *eax) {
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0x24u);
    if (!f) return hud_refuse(c, VA_LED, "LED segments", "the own code's call area is exhausted");
    const uint32_t p2 = f, p1 = f + 8u, x0 = f + 0x10u, saved = f + 0x14u, ctx = f + 0x18u, pen = f + 0x1cu;
    hw32(c, pen, 0);
    hw32(c, x0, hr32(c, rect));
    uint32_t x1 = hr32(c, rect + 8u);
    hw32(c, p1 + 4u, hr32(c, rect + 4u));
    hw32(c, p2 + 4u, hr32(c, rect + 0xcu));
    uint32_t v = hr32(c, VA_VIEW);
    hw32(c, ctx, hr32(c, v + 0x44u) ? hr32(c, v + 0x44u) - 4u : hr32(c, v + 0x44u));
    hw32(c, saved, hr32(c, hr32(c, ctx) + 0x228u));
    hw32(c, hr32(c, ctx) + 0x228u, pen);
    for (unsigned pass = 0; pass < 2u; ++pass) {
        hw32(c, pen + 4u, hr32(c, b + (pass ? 0x30u : 0x2cu)));
        uint32_t x = hr32(c, x0);
        hw32(c, p2, x);
        hw32(c, p1, x);
        if (((x & 1u) != 0) == (pass == 0u)) {
            hw32(c, p1, hr32(c, p1) + 1u);
            hw32(c, p2, hr32(c, p2) + 1u);
        }
        while ((int32_t)x1 >= (int32_t)hr32(c, p1)) {
            if (HCALL(c, VA_LINE, NULL, p1, p2)) { hud_frame_pop(c, top); return 1; }
            hw32(c, p1, hr32(c, p1) + 2u);
            hw32(c, p2, hr32(c, p2) + 2u);
        }
    }
    *eax = hr32(c, ctx);
    hw32(c, *eax + 0x228u, hr32(c, saved));
    hud_frame_pop(c, top);
    return 0;
}
PN_ROW(led_segments) { PN_ROW_RUN(pn_led(&c, sc->slots[0], sc->slots[1], &eax)); }

/* 0x4437cc(G) (listing 0x4437cc..0x443832), the shields gauge's power-on
 * sweep (only caller 0x443770, inlined there): t = the clock (site
 * 0x4437d3) - [G+0x90]; [G+0xb5] <- min([G+0xcc], t), [G+0xb9] <-
 * min([G+0xd0], t) (signed); when both now equal their targets, byte
 * G+0x8d <- 1; byte G+0xb0 <- 2. EAX the second minimum, or [G+0xb9] when
 * [G+0xb5] reached its target. */
static int pn_shield_sweep(hud_ctx *c, uint32_t g, uint32_t *eax) {
    uint32_t now = 0;
    if (hud_clock(c, 0x004437d3u, &now)) return 1;
    int32_t t = (int32_t)(now - hr32(c, g + 0x90u));
    int32_t front = (int32_t)hr32(c, g + 0xccu);
    hw32(c, g + 0xb5u, (uint32_t)(front <= t ? front : t));
    int32_t rear = (int32_t)hr32(c, g + 0xd0u);
    *eax = (uint32_t)(rear > t ? t : rear);
    hw32(c, g + 0xb9u, *eax);
    if (hr32(c, g + 0xb5u) == hr32(c, g + 0xccu)) {
        *eax = hr32(c, g + 0xb9u);
        if (*eax == hr32(c, g + 0xd0u)) hw8(c, g + 0x8du, 1);
    }
    hw8(c, g + 0xb0u, 2);
    return 0;
}
/* 0x444a90(G) (listing 0x444a90..0x444b11), the balance numbers: when
 * 0x431410([G+0xc4]+4) answers AL set, a = ([G+0xbd] * 200) sar 10 (the
 * product wraps at 32 bits), _itoa(a) and _itoa(200 - a) (radix 10) into
 * two 8-byte locals, then 0x438920([G+0xc4], a's) and 0x438920([G+0xc8],
 * the other's). EAX the last call's. */
static int pn_shield_numbers(hud_ctx *c, uint32_t g, uint32_t *eax) {
    if (HCALL(c, VA_PANEL_VISIBLE, eax, hr32(c, g + 0xc4u) + 4u)) return 1;
    if (!(*eax & 0xffu)) return 0;
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0x10u);
    if (!f) return hud_refuse(c, VA_SHIELD_NUMBERS, "shield numbers", "the own code's call area is exhausted");
    uint32_t a = (uint32_t)((int32_t)(hr32(c, g + 0xbdu) * 200u) >> 10);
    int bad = HCALL(c, HUD_VA_ITOA, eax, a, f + 8u, 10u) || HCALL(c, HUD_VA_ITOA, eax, 200u - a, f, 10u) ||
              HCALL(c, VA_LABEL, eax, hr32(c, g + 0xc4u), f + 8u) || HCALL(c, VA_LABEL, eax, hr32(c, g + 0xc8u), f);
    hud_frame_pop(c, top);
    return bad;
}
PN_ROW(shield_numbers) { PN_ROW_RUN(pn_shield_numbers(&c, sc->slots[0], &eax)); }
/* 0x443758(G) (listing 0x443758..0x44376f), ShieldsGauge slot +4: the
 * shield palette 0x443918(G), then 0x444a90(G); EAX the second's. The row
 * in hud_load.h delegates here. */
int es2_sim_panel_shields_draw(hud_ctx *c, uint32_t g, uint32_t *eax) {
    return HCALL(c, VA_SHIELD_PALETTE, eax, g) || HCALL(c, VA_SHIELD_NUMBERS, eax, g);
}
/* 0x443770(G) (listing 0x443770..0x4437cb), ShieldsGauge slot +8, the
 * frame: when 0x431410(G+4) answers AL set and byte G+0x8c is set: the
 * sweep 0x4437cc while byte G+0x8d is clear; then by byte G+0xb0: 1 counts
 * it down; 2: 0x443918(G), 0x444a90(G), counted down; other values
 * nothing. EAX the last call's (the sweep's value when it ran last). */
static int pn_shields_frame(hud_ctx *c, uint32_t g, uint32_t *eax) {
    if (HCALL(c, VA_PANEL_VISIBLE, eax, g + 4u)) return 1;
    if (!(*eax & 0xffu) || !hr8(c, g + 0x8cu)) return 0;
    if (!hr8(c, g + 0x8du) && pn_shield_sweep(c, g, eax)) return 1;
    uint8_t s = hr8(c, g + 0xb0u);
    if (s == 1u) {
        hw8(c, g + 0xb0u, (uint8_t)(hr8(c, g + 0xb0u) - 1u));
        return 0;
    }
    if (s != 2u) return 0;
    if (HCALL(c, VA_SHIELD_PALETTE, eax, g) || HCALL(c, VA_SHIELD_NUMBERS, eax, g)) return 1;
    hw8(c, g + 0xb0u, (uint8_t)(hr8(c, g + 0xb0u) - 1u));
    return 0;
}
PN_ROW(shields_frame) { PN_ROW_RUN(pn_shields_frame(&c, sc->slots[0], &eax)); }

/* 0x445008(G) (listing 0x445008..0x4450f4), EnergyPool slot +8, the frame:
 * when 0x431410(0x4390b8([G+0xb5])) answers AL set and byte G+0xb0 is 2:
 * the GC S = [0x6c5f70] gets +0x22c <- the address of a zeroed local (left
 * behind, as the original leaves it); when the own context [G+0xbf] is
 * set, S's rectangle +0x210..+0x21c and origin +0x220/+0x224 are copied
 * into it (the pointer to the second pair kept in a local and re-read) and
 * it is selected (0x480d44). The bar [G+0xb5]'s slot 0 with (bar, the word
 * G+0xb1 over the upper half of EAX: that of 0x480d44, or 0 without a
 * context), the panel copy 0x43122c(1, G+4, 0), S selected again when
 * [G+0xbf] (re-read) is set, byte G+0xb0 <- 0. EAX the last call's. */
static int pn_pool_frame(hud_ctx *c, uint32_t g, uint32_t *eax) {
    uint32_t w = 0;
    if (HCALL(c, VA_WIDGET_PLUS4, &w, hr32(c, g + 0xb5u)) || HCALL(c, VA_PANEL_VISIBLE, eax, w)) return 1;
    if (!(*eax & 0xffu) || hr8(c, g + 0xb0u) != 2u) return 0;
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0xcu);
    if (!f) return hud_refuse(c, 0x00445008u, "pool frame", "the own code's call area is exhausted");
    uint32_t gc = hr32(c, VA_GC), local = f + 4u, pair = f;
    hw32(c, local, 0);
    hw32(c, gc + 0x22cu, local);
    *eax = hr32(c, g + 0xbfu);
    int bad = 0;
    if (*eax) {
        uint32_t d = *eax + 0x210u;
        hw32(c, d, hr32(c, gc + 0x210u));
        hw32(c, d + 4u, hr32(c, gc + 0x214u));
        hw32(c, pair, gc + 0x218u);
        hw32(c, d + 8u, hr32(c, hr32(c, pair)));
        hw32(c, d + 0xcu, hr32(c, hr32(c, pair) + 4u));
        d = hr32(c, g + 0xbfu) + 0x220u;
        hw32(c, d, hr32(c, gc + 0x220u));
        hw32(c, d + 4u, hr32(c, gc + 0x224u));
        bad = HCALL(c, VA_GC_SELECT, eax, hr32(c, g + 0xbfu));
    }
    if (!bad) {
        uint32_t bar = hr32(c, g + 0xb5u);
        bad = HVCALL(c, bar, 0u, eax, bar, (*eax & 0xffff0000u) | hr16(c, g + 0xb1u)) ||
              HCALL(c, VA_PANEL_COPY, eax, 1u, g + 4u, 0u) || (hr32(c, g + 0xbfu) && HCALL(c, VA_GC_SELECT, eax, gc));
    }
    if (!bad) hw8(c, g + 0xb0u, 0);
    hud_frame_pop(c, top);
    return bad;
}
PN_ROW(pool_frame) { PN_ROW_RUN(pn_pool_frame(&c, sc->slots[0], &eax)); }

/* 0x446c38(M, code) (listing 0x446c38..0x446dd9), the MFD's Alt order
 * keys (called by the hook hud_key, 0x432f9a): E = [M+0xd1]; the word
 * code - 0x212 indexes a byte table (0x446c65, 0x1d entries) of a jump
 * table (0x446c82, 8 entries): 0x21e item 0, 0x222 item 1, 0x223 item 2,
 * 0x218 item 3, 0x221 item 5, 0x22e and 0x212 item 4 with the check
 * 0x43f9c0(E) == 4 (0x22e) or 7 (0x212); every other code EAX 0. An item
 * n: 0x43f9e0(E, n) inline (word E+0x37 <- word E+0x32, E+0x32 <- n), for
 * the checked items the check (a failed one ends here), then the hook
 * 0x447248(M) and 0x447158(M, [M+0xfb], n). Handled: AL 1 over the upper
 * bytes of the last call's EAX. */
static int pn_mfd_alt_order(hud_ctx *c, uint32_t m, uint32_t code, uint32_t *eax) {
    uint32_t e = hr32(c, m + 0xd1u), item, check = 0;
    switch (code & 0xffffu) {
    case 0x21eu: item = 0; break;
    case 0x222u: item = 1; break;
    case 0x223u: item = 2; break;
    case 0x218u: item = 3; break;
    case 0x221u: item = 5; break;
    case 0x22eu: item = 4; check = 4; break;
    case 0x212u: item = 4; check = 7; break;
    default: *eax = 0; return 0;
    }
    hw16(c, e + 0x37u, hr16(c, e + 0x32u));
    hw32(c, e + 0x32u, item);
    *eax = e;
    if (check && HCALL(c, VA_MENU_CODE, eax, e)) return 1;
    if (!check || *eax == check) {
        if (HCALL(c, VA_MFD_ORDER, eax, m) || HCALL(c, VA_MFD_ORDER_KEY, eax, m, hr32(c, m + 0xfbu), item)) return 1;
    }
    *eax = (*eax & ~0xffu) | 1u;
    return 0;
}
PN_ROW(mfd_alt_order) { PN_ROW_RUN(pn_mfd_alt_order(&c, sc->slots[0], sc->slots[1], &eax)); }

/* 0x434910(B) (listing 0x434910..0x434926), SystemGadget slot +8, the
 * action: 0x4345a0([0x49b088], B) (listing 0x4345a0..0x434600, only caller
 * this one, inlined): with the word [0x4d24d6] (tape playback) set,
 * nothing (EAX the panel H). Otherwise the first i < [H+0x25a] (signed)
 * with [H+0x246+4i] == B: i 0: 0x4669c4(0x45f070()); i 1: 0x4667c8(),
 * then [H+0x1e9]'s slot +4; a later i: nothing, EAX i - 1. No match: EAX
 * the count reached (0 for a count below 1). */
static int pn_system_action(hud_ctx *c, uint32_t b, uint32_t *eax) {
    uint32_t h = hr32(c, VA_PANEL);
    *eax = h;
    if (hr16(c, VA_PLAYBACK_WORD)) return 0;
    uint32_t i = 0, d = h + 0x246u;
    for (; (int32_t)i < (int32_t)hr32(c, h + 0x25au); ++i, d += 4u) {
        if (b != hr32(c, d)) continue;
        if (i == 0) {
            uint32_t x = 0;
            return HCALL0(c, 0x0045f070u, &x) || HCALL(c, 0x004669c4u, eax, x);
        }
        if (i == 1u) {
            if (HCALL0(c, 0x004667c8u, eax)) return 1;
            uint32_t o = hr32(c, h + 0x1e9u);
            return HVCALL(c, o, 4u, eax, o);
        }
        *eax = i - 1u;
        return 0;
    }
    *eax = i;
    return 0;
}
PN_ROW(system_button_action) { PN_ROW_RUN(pn_system_action(&c, sc->slots[0], &eax)); }

/* The SystemGadget event base's entries (listing 0x434dd2..0x434dea, each
 * `add dword [esp+4], -0x20; jmp target`): 0x434dd2 -> 0x434910,
 * 0x434ddc -> 0x434dcd (empty), 0x434de6 -> 0x434748 (the draw). The
 * gadget's method runs for the base's this less 0x20, the registers passed
 * through. The original leaves the adjusted value in the caller's argument
 * slot, which its cdecl callers pop without reading. */
PN_ROW(system_base_thunk) {
    (void)user;
    HSERVE(sc, c);
    static const uint32_t entries[][2] = {
        {0x00434dd2u, 0x00434910u}, {0x00434ddcu, 0x00434dcdu}, {0x00434de6u, 0x00434748u},
    };
    unsigned i = 0;
    while (i < 3u && entries[i][0] != sc->va) ++i;
    if (i == 3u) {
        sc->refused = 1;
        sc->reason = "not an event-base entry of the system gadget";
        return;
    }
    uint32_t slots[1] = {sc->slots[0] - 0x20u};
    es2_sim_result r;
    if (hud_call_regs(&c, entries[i][1], slots, 1, (const uint32_t[]){sc->eax, sc->edx, sc->ecx}, &r)) { hud_failed(sc); return; }
    sc->eax = r.eax; sc->edx = r.edx; sc->ecx = r.ecx;
}

/* 0x434df0(Z) (listing 0x434df0..0x434e04), ScrollTrigger slot +8: 0x433a88
 * ([Z+0x20], Z) (listing 0x433a88..0x433b1f, only caller this one,
 * inlined): D = 0x429820() (the current display), n = [D+0x14] read once.
 * Z the bottom zone [H+0x21a]: n 0: 0x42a3f4(D, 0); n 1: 0x42a3f4(D, 1).
 * The left zone [H+0x21e]: n 0: (D, 5); n 3: (D, 6). The right zone
 * [H+0x222]: n 0: (D, 4); n 2: (D, 6). EAX the last 0x42a3f4's, else Z. */
static int pn_scroll_event(hud_ctx *c, uint32_t z, uint32_t *eax) {
    uint32_t h = hr32(c, z + 0x20u), d = 0;
    if (HCALL0(c, VA_DISPLAY_CURRENT, &d)) return 1;
    uint32_t n = hr32(c, d + 0x14u);
    *eax = z;
    uint32_t first, later, when;
    if (z == hr32(c, h + 0x21au)) { first = 0; later = 1; when = 1; }
    else if (z == hr32(c, h + 0x21eu)) { first = 5; later = 6; when = 3; }
    else if (z == hr32(c, h + 0x222u)) { first = 4; later = 6; when = 2; }
    else return 0;
    if (!n && HCALL(c, VA_VIEW_SET, eax, d, first)) return 1;
    if (n == when && HCALL(c, VA_VIEW_SET, eax, d, later)) return 1;
    return 0;
}
PN_ROW(scroll_trigger_event) { PN_ROW_RUN(pn_scroll_event(&c, sc->slots[0], &eax)); }
