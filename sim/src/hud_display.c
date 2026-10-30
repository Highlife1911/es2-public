/* The cockpit display modes (the layout set at the end of the load and the view
 * switches during the mission), as own code. Contract: hud.md, "the HUD line in
 * the own process", display_modes.md and its section "the view border and the
 * layout set draw"; every function
 * names its listing range of the reference DBSIM.EXE (llvm-objdump -d -M intel,
 * SHA-256 8561aa47...c4ad). The pixel work (the rectangle fills 0x486704, the
 * bitmap blits 0x488143, 0x4816a8 and, at 640x480, the stretch 0x4817c8) goes
 * through the graphics rows (gfx.c and its neighbours; nothing is counted
 * headless); every state field, every guest call and the dangling GC fields of the
 * original are kept. */
#include "hud_internal.h"

#define VA_GC 0x006c5f70u          /* the current graphics context */
#define VA_VIEW 0x006c5ed0u        /* the current view surface */
#define VA_DISPLAY 0x004cf99cu     /* the cockpit display (0x429820) */
#define VA_RES_MODE 0x004d2538u    /* byte: 0 low, 1 the 400-line mode, 2 640x480 */
#define VA_SPLIT 0x004d2680u       /* word: per-mode view shapes */
#define VA_PAGED 0x004d2566u       /* word: page-flipped surface */
#define VA_PAGE_X 0x004d3018u      /* the page origin of 0x4649d8 */
#define VA_PAGE_Y 0x004d301cu
#define VA_JOLT 0x0049ac44u        /* byte: a jolt offset is applied */
#define VA_JOLT_DONE 0x0049ac45u
#define VA_JOLT_Y 0x004cfa70u      /* word: the jolt's y offset */
#define VA_PEN_RECORD 0x004a5af8u
#define VA_ORIGIN 0x004cf9a0u      /* the mode's x, y offsets (dwords) */
#define VA_DRV_ORIGIN 0x004a57e8u  /* driver slot: the page origin (BMP_8 0x48990e) */
#define VA_DRV_PAGE 0x004a57f0u    /* driver slot: the page select (BMP_8 0x489902, empty) */
#define VA_CUR_PAGE 0x004a368au
#define VA_SURFACE_ATTRIBUTE 0x00481350u
#define VA_PORT_A 0x00433158u
#define VA_BLIT 0x004816a8u
#define VA_BITMAP_DESTROY 0x0048349cu
#define VA_CANVAS_PUSH 0x004311e0u
#define VA_CANVAS_POP 0x00431210u

static int32_t sx8(const hud_ctx *c, uint32_t a) { return (int32_t)(int8_t)hr8(c, a); }
static int32_t sx16(const hud_ctx *c, uint32_t a) { return (int32_t)(int16_t)hr16(c, a); }
static void add32(const hud_ctx *c, uint32_t a, uint32_t v) { hw32(c, a, hr32(c, a) + v); }

/* A call through a slot of the current driver table copy. */
static int driver_call(hud_ctx *c, uint32_t slot, const uint32_t *a, unsigned n) {
    return hud_call(c, hr32(c, slot), a, n, NULL);
}
/* The page select of the view's page p, as 0x42a378.. and 0x42d773.. do
 * it: V+0x3c <- p; V current: [0x4a368a] <- p and the driver's page slot. */
static int page_select(hud_ctx *c, uint32_t v, uint32_t p) {
    hw32(c, v + 0x3cu, p);
    if (v != hr32(c, VA_VIEW)) return 0;
    hw32(c, VA_CUR_PAGE, hr32(c, v + 0x3cu));
    return driver_call(c, VA_DRV_PAGE, (const uint32_t[]){p}, 1);
}

/* 0x452434(r, pt) (listing 0x452434..0x452454): the rectangle r moved by
 * the point pt (x to +0, +8; y to +4, +0xc). */
static void rect_move(hud_ctx *c, uint32_t r, uint32_t pt) {
    add32(c, r, hr32(c, pt));
    add32(c, r + 8u, hr32(c, pt));
    add32(c, r + 4u, hr32(c, pt + 4u));
    add32(c, r + 0xcu, hr32(c, pt + 4u));
}
/* 0x45275c(W, pt) (listing 0x45275c..0x4527c8), reached by value through
 * 0x452bec(W, x, y): the two surface attributes (0x481350(V, 0), (V, 1),
 * the results unused), W+0x233/+0x237 and W+0x22b/+0x22f moved, then the
 * widget's rectangle (0x452434). */
static int widget_move(hud_ctx *c, uint32_t w, uint32_t pt) {
    if (HCALL(c, VA_SURFACE_ATTRIBUTE, NULL, hr32(c, VA_VIEW), 0u) ||
        HCALL(c, VA_SURFACE_ATTRIBUTE, NULL, hr32(c, VA_VIEW), 1u)) return 1;
    add32(c, w + 0x233u, hr32(c, pt));
    add32(c, w + 0x237u, hr32(c, pt + 4u));
    add32(c, w + 0x22bu, hr32(c, pt));
    add32(c, w + 0x22fu, hr32(c, pt + 4u));
    rect_move(c, w, pt);
    return 0;
}
/* 0x433968(P, pt) (listing 0x433968..0x433a86): the panel's three slide
 * rectangles [P+0x21a..0x222] after a move by pt. A move beyond 5 pixels
 * (either sign, each axis) flips each rectangle to the other side of its
 * edge and takes one surface width (height) off the offset; the offset is
 * then -(pt - that) on both corners. */
static int panel_slide(hud_ctx *c, uint32_t p, uint32_t pt) {
    uint32_t w = 0, h = 0;
    if (HCALL(c, VA_SURFACE_ATTRIBUTE, &w, hr32(c, VA_VIEW), 0u) ||
        HCALL(c, VA_SURFACE_ATTRIBUTE, &h, hr32(c, VA_VIEW), 1u)) return 1;
    int32_t x = (int32_t)hr32(c, pt), y = (int32_t)hr32(c, pt + 4u);
    int dx = 0, dy = 0;
    uint32_t sx = 0, sy = 0;
    if (x > 5) { sx = (uint32_t)(int32_t)(int16_t)w; dx = 1; }
    else if (x < -5) { dx = -1; sx = (uint32_t)-(int32_t)(int16_t)w; }
    if (y > 5) { sy = (uint32_t)(int32_t)(int16_t)h; dy = 1; }
    else if (y < -5) { dy = -1; sy = (uint32_t)-(int32_t)(int16_t)h; }
    uint32_t ox = 0u - ((uint32_t)x - sx), oy = 0u - ((uint32_t)y - sy);
    for (uint32_t k = 0; k < 3u; ++k) {
        uint32_t r = hr32(c, p + 0x21au + 4u * k);
        uint32_t rw = hr32(c, r + 8u) - hr32(c, r), rh = hr32(c, r + 0xcu) - hr32(c, r + 4u);
        if (dx > 0) { hw32(c, r + 8u, hr32(c, r)); hw32(c, r, hr32(c, r) - rw); }
        if (dx < 0) { hw32(c, r, hr32(c, r + 8u)); hw32(c, r + 8u, hr32(c, r) + rw); }
        if (dy > 0) { hw32(c, r + 0xcu, hr32(c, r + 4u)); hw32(c, r + 4u, hr32(c, r + 4u) - rh); }
        if (dy < 0) { hw32(c, r + 4u, hr32(c, r + 0xcu)); hw32(c, r + 0xcu, hr32(c, r + 4u) + rh); }
        add32(c, r, ox);
        add32(c, r + 4u, oy);
        add32(c, r + 8u, ox);
        add32(c, r + 0xcu, oy);
    }
    return 0;
}
/* 0x43271c(P, pt) (listing 0x43271c..0x4327a8): the panel moved by pt:
 * its root widget [P] (0x452bec), the canvas origin [[P+4]+0x220] back by
 * pt, the slide rectangles when byte P+0x23e is set (0x433968), then each
 * of the [P+0x25a] child rectangles [P+0x246..] by -pt (0x452434). */
static int panel_move(hud_ctx *c, uint32_t p, uint32_t pt) {
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 16u);
    if (!f) return hud_refuse(c, 0x0043271cu, "panel move", "the own code's call area is exhausted");
    hw32(c, f, hr32(c, pt));
    hw32(c, f + 4u, hr32(c, pt + 4u));
    if (widget_move(c, hr32(c, p), f)) { hud_frame_pop(c, top); return 1; }
    uint32_t o = hr32(c, p + 4u) + 0x220u;
    hw32(c, o, hr32(c, o) - hr32(c, pt));
    hw32(c, o + 4u, hr32(c, o + 4u) - hr32(c, pt + 4u));
    if (hr8(c, p + 0x23eu) && panel_slide(c, p, pt)) { hud_frame_pop(c, top); return 1; }
    hw32(c, f + 8u, 0u - hr32(c, pt));
    hw32(c, f + 0xcu, 0u - hr32(c, pt + 4u));
    for (uint32_t i = 0; (int32_t)i < (int32_t)hr32(c, p + 0x25au); ++i) rect_move(c, hr32(c, p + 0x246u + 4u * i), f + 8u);
    hud_frame_pop(c, top);
    return 0;
}
/* 0x435574(port, pt) (listing 0x435574..0x4355a6): the port's two points
 * +0x4a7 and +0x4af back by pt. */
static void port_move(hud_ctx *c, uint32_t port, uint32_t pt) {
    for (uint32_t k = 0; k < 2u; ++k) {
        uint32_t a = port + 0x4a7u + 8u * k;
        hw32(c, a, hr32(c, a) - hr32(c, pt));
        hw32(c, a + 4u, hr32(c, a + 4u) - hr32(c, pt + 4u));
    }
}
/* Both moves of 0x429fbe.. / 0x42a109..: the panel [D+0x1f] by pt, then
 * port A (0x433158, called with &pt left on its stack) by pt. */
static int display_move(hud_ctx *c, uint32_t d, uint32_t pt) {
    uint32_t port = 0;
    if (panel_move(c, hr32(c, d + 0x1fu), pt) || HCALL(c, VA_PORT_A, &port, pt)) return 1;
    port_move(c, port, pt);
    return 0;
}

/* 0x4316c0(P) (listing 0x4316c0..0x431776): the panel's full draw unless
 * byte P+0x20f (hidden): the canvas push 0x4311e0, slot +4 of each set
 * element P+0x70..+0x94 and of P+0x1e1, +0x1e5, +0x1e9, +0x1f1, +0x1ed,
 * +0x1f9, the canvas pop 0x431210, then slot +4 of P+0x1f5. The slot +4
 * functions are the elements' full draws, called through the dispatch:
 * their coupling calls (the HUD's 0x43d5f0 reads the clock in 0x43dcd4)
 * are theirs. */
static int panel_draw(hud_ctx *c, uint32_t p) {
    static const uint16_t later[6] = {0x1e1, 0x1e5, 0x1e9, 0x1f1, 0x1ed, 0x1f9};
    if (hr8(c, p + 0x20fu)) return 0;
    if (HCALL0(c, VA_CANVAS_PUSH, NULL)) return 1;
    for (uint32_t i = 0; i < 10u; ++i) {
        uint32_t e = hr32(c, p + 0x70u + 4u * i);
        if (e && HVCALL(c, e, 4u, NULL, e)) return 1;
    }
    for (unsigned k = 0; k < 6u; ++k) {
        uint32_t e = hr32(c, p + later[k]);
        if (e && HVCALL(c, e, 4u, NULL, e)) return 1;
    }
    if (HCALL0(c, VA_CANVAS_POP, NULL)) return 1;
    uint32_t e = hr32(c, p + 0x1f5u);
    return e && HVCALL(c, e, 4u, NULL, e);
}

/* 0x42da08(D) (listing 0x42da08..0x42db0e), the border of the full-screen view:
 * GC+0x22c <- a local pair (0, word [0x4d3ba2]) (left dangling, as in the
 * original), GC+0x208 and +0x20c cleared, the four rectangles around the view,
 * each filled through the row 0x486704 when it is not empty, the two fields
 * restored. D is not read. The fills draw.
 * The view's rectangle is (L, T, R, B) = dwords 0x4cfa40, 0x4cfa44, 0x4cfa48,
 * 0x4cfa4c, the screen W = [0x4d253e], H = [0x4d2542]; in the listing's order
 * (each when its last corner is not below its first, signed): top (0, 0, W-1)
 * when T-1 >= 0; bottom (0, B+1, W-1, H-1) when H-1 >= B+1; left (0, T, L-1, B)
 * when L-1 >= 0; right (R+1, T, W-1, B) when W-1 >= R+1. The locals keep the
 * listing's frame offsets (ebp-0x48 .. ebp-4; the pair at ebp-8). EAX the right
 * fill's when it is drawn, else W-1 (0x42daea); EDX and ECX are not kept. */
static int view_border(hud_ctx *c, uint32_t *eax) {
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0x48u);
    if (!f) return hud_refuse(c, 0x0042da08u, "view border", "the own code's call area is exhausted");
    uint32_t e = f + 0x48u;  /* ebp */
    uint32_t gc = hr32(c, VA_GC);
    hw32(c, e - 8u, 0);
    hw32(c, e - 4u, (uint32_t)sx16(c, 0x004d3ba2u));
    hw32(c, gc + 0x22cu, e - 8u);
    uint32_t a = hr32(c, gc + 0x208u), b = hr32(c, gc + 0x20cu);
    hw32(c, gc + 0x208u, 0);
    hw32(c, gc + 0x20cu, 0);
    uint32_t w1 = hr32(c, 0x004d253eu) - 1u, t1 = hr32(c, 0x004cfa44u) - 1u, b1 = hr32(c, 0x004cfa4cu) + 1u;
    uint32_t h1 = hr32(c, 0x004d2542u) - 1u, l1 = hr32(c, 0x004cfa40u) - 1u, r1 = hr32(c, 0x004cfa48u) + 1u;
    /* top (ebp-0x18), bottom (-0x28), left (-0x38), right (-0x48) */
    const uint32_t rect[4][4] = {{0u, 0u, w1, t1}, {0u, b1, w1, h1}, {0u, t1 + 1u, l1, b1 - 1u}, {r1, t1 + 1u, w1, b1 - 1u}};
    for (unsigned k = 0; k < 4u; ++k)
        for (unsigned i = 0; i < 4u; ++i) hw32(c, e - 0x18u - 0x10u * k + 4u * i, rect[k][i]);
    static const uint8_t first[4] = {1, 1, 0, 0}, last[4] = {3, 3, 2, 2};  /* the compared corners */
    uint32_t out = w1;  /* EAX: the right fill's, else W-1 (0x42daea) */
    for (unsigned k = 0; k < 4u; ++k) {
        uint32_t r = e - 0x18u - 0x10u * k;
        if ((int32_t)hr32(c, r + 4u * last[k]) < (int32_t)hr32(c, r + 4u * first[k])) continue;
        if (HCALL(c, 0x00486704u, k == 3u ? &out : NULL, r)) { hud_frame_pop(c, top); return 1; }
    }
    if (eax) *eax = out;
    hw32(c, gc + 0x208u, a);
    hw32(c, gc + 0x20cu, b);
    hud_frame_pop(c, top);
    return 0;
}

/* 0x42d624() (listing 0x42d624..0x42d81c): the end of a jolt (byte
 * 0x49ac44). Display D = [0x4cf99c] in a mode other than 4: the page
 * origin of the view's page [V+0x8c] at its point + (0x4cfa58, 0x4cfa5c)
 * (driver slot [0x4a57e8]); the [rec] entries of the mode's record
 * [D+0xc] + 0x204 mode: type 0 moves dword +0x10 of the entry's object
 * (entry - 4 + 0x14), type 2 the word at [entry - 4 + 8] by word
 * [0x4cfa70] = j; the pen record [0x4a5af8] (when set) +0 by -8j, +4 by
 * +j; GC+0x218, +0x21c by j; modes 2, 3: both pages blitted back
 * (0x488143 with 0x4cfa7e, 0x4cfa82, [0x4cfa94]) and selected
 * (V+0x88, then V+0x8c) with GC+0x208/+0x20c cleared around it; the panel
 * [D+0x1f] moved by (0, j) (0x43271c); j <- 0, the byte <- 0. */
static int jolt_end(hud_ctx *c) {
    if (!hr8(c, VA_JOLT)) return 0;
    uint32_t d = hr32(c, VA_DISPLAY), mode = hr32(c, d + 0x14u);
    if (mode == 4u) return 0;
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 16u);
    if (!f) return hud_refuse(c, 0x0042d624u, "jolt end", "the own code's call area is exhausted");
    uint32_t v = hr32(c, VA_VIEW), page = hr32(c, v + 0x8cu), q = v + 8u * page + 0x48u;
    hw32(c, f, hr32(c, q) + hr32(c, 0x004cfa58u));
    hw32(c, f + 4u, hr32(c, q + 4u) + hr32(c, 0x004cfa5cu));
    if (driver_call(c, VA_DRV_ORIGIN, (const uint32_t[]){page, f}, 2)) { hud_frame_pop(c, top); return 1; }
    uint32_t rec = hr32(c, hr32(c, VA_DISPLAY) + 0xcu) + mode * 0x204u;
    for (uint32_t i = 0; (int32_t)i < (int32_t)hr32(c, rec); ++i) {
        uint32_t e = hr32(c, rec + 4u + 4u * i), t = hr32(c, e), o = e ? e - 4u : 0u;
        if (t == 0u) add32(c, o + 0x14u, (uint32_t)sx16(c, VA_JOLT_Y));
        else if (t == 2u) {
            uint32_t w = hr32(c, o + 8u);
            if (w) hw16(c, w, (uint16_t)(hr16(c, w) + hr16(c, VA_JOLT_Y)));
        }
    }
    uint32_t j = (uint32_t)sx16(c, VA_JOLT_Y), n = 0u - j, pen = hr32(c, VA_PEN_RECORD);
    if (pen) {
        add32(c, pen, n << 3);
        hw32(c, pen + 4u, hr32(c, pen + 4u) - n);
    }
    uint32_t gc = hr32(c, VA_GC);
    add32(c, gc + 0x218u, (uint32_t)sx16(c, VA_JOLT_Y));
    add32(c, gc + 0x21cu, (uint32_t)sx16(c, VA_JOLT_Y));
    if (mode - 2u < 2u) {
        uint32_t a = hr32(c, gc + 0x208u), b = hr32(c, gc + 0x20cu);
        hw32(c, gc + 0x208u, 0);
        hw32(c, gc + 0x20cu, 0);
        /* The strip restored on both pages through gfx.c's row 0x488143
         * (gfx_restore), as the original draws it. */
        if (HCALL(c, 0x00488143u, NULL, 0x004cfa7eu, 0x004cfa82u, hr32(c, 0x004cfa94u)) ||
            page_select(c, v, hr32(c, v + 0x88u))) { hud_frame_pop(c, top); return 1; }
        if (HCALL(c, 0x00488143u, NULL, 0x004cfa7eu, 0x004cfa82u, hr32(c, 0x004cfa94u)) ||
            page_select(c, v, hr32(c, v + 0x8cu))) { hud_frame_pop(c, top); return 1; }
        hw32(c, gc + 0x208u, a);
        hw32(c, gc + 0x20cu, b);
    }
    hw32(c, f + 8u, 0);
    hw32(c, f + 0xcu, (uint32_t)sx16(c, VA_JOLT_Y));
    if (panel_move(c, hr32(c, hr32(c, VA_DISPLAY) + 0x1fu), f + 8u)) { hud_frame_pop(c, top); return 1; }
    hw16(c, VA_JOLT_Y, 0);
    hw8(c, VA_JOLT, 0);
    hud_frame_pop(c, top);
    return 0;
}

/* The GC rectangle and offsets of a mode record (0x42a123.. / 0x429ebd..):
 * 0x81 dwords to GC+4, GC+0x208 <- 2, GC+0x210..+0x224 from the record. */
static void gc_copy(hud_ctx *c, uint32_t gc, uint32_t src) {
    for (uint32_t i = 0; i < 0x81u; ++i) hw32(c, gc + 4u + 4u * i, hr32(c, src + 4u * i));
}

/* 0x429e60(D, mode) (listing 0x429e60..0x42a1ec): the switch of the
 * cockpit display D to mode (0..3 cockpit views, 4 the full-screen view).
 * Same mode or mode > 4: nothing. A running jolt ends first (0x42d624,
 * byte 0x49ac45 <- 1).
 * Mode 4 from another mode: byte 0x49ac45 <- 0, D+0x23 <- the old mode,
 * the GC from 0x4cfa98 and 0x4cfa40..0x4cfa54, [0x4a5af8] <- 0, D+0x14 <-
 * 4, the panel hidden (0x431500), 0x4cf9a0/a4 <- 0, the border (0x42da08).
 * Other modes: the old mode's entry D+4 + 32 old (from 4: D+0x23's, the
 * panel shown 0x431518); old modes 2, 3 (not 640x480) move the panel and
 * port A back by the old entry's offset +0x18; paged surfaces set page 0's
 * origin; the new entry's offset is applied to the page origin and (unless
 * modes 2, 3 on an unpaged surface) the panel moved by old - new; then the
 * GC from the mode's record [D+0xc] + 0x204 mode, [0x4a5af8] <- [[D+0x10]
 * + 4 mode], the rectangle from the entry, and the mode offset either
 * added to the GC origin (0x4cf9a0/a4) or, at 640x480, stored at
 * 0x4d2556/5a. D+0x14 is set by the caller (0x42a1f0). */
static int display_switch(hud_ctx *c, uint32_t d, uint32_t mode) {
    uint32_t v = hr32(c, VA_VIEW);
    if (mode == hr32(c, d + 0x14u) || (int32_t)mode > 4) return 0;
    if (hr8(c, VA_JOLT)) {
        if (jolt_end(c)) return 1;
        hw8(c, VA_JOLT_DONE, 1);
    }
    uint32_t gc = hr32(c, VA_GC);
    if (mode == 4u) {
        hw8(c, VA_JOLT_DONE, 0);
        uint32_t old = hr32(c, d + 0x14u);
        if (old == 4u) return 0;
        hw32(c, d + 0x23u, old);
        gc = hr32(c, VA_GC);
        gc_copy(c, gc, 0x004cfa98u);
        hw32(c, gc + 0x208u, 2);
        hw32(c, VA_PEN_RECORD, 0);
        for (uint32_t k = 0; k < 6u; ++k) hw32(c, gc + 0x210u + 4u * k, hr32(c, 0x004cfa40u + 4u * k));
        hw32(c, d + 0x14u, 4);
        if (HCALL(c, 0x00431500u, NULL, hr32(c, d + 0x1fu))) return 1;
        hw32(c, VA_ORIGIN + 4u, 0);
        hw32(c, VA_ORIGIN, 0);
        return view_border(c, NULL);
    }
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0x20u);
    if (!f) return hud_refuse(c, 0x00429e60u, "display switch", "the own code's call area is exhausted");
    int moves = 1;
    uint32_t old = hr32(c, d + 0x14u), e = (old << 5) + hr32(c, d + 4u);
    if (old == 4u) {
        moves = 0;
        if (HCALL(c, 0x00431518u, NULL, hr32(c, d + 0x1fu))) goto refused;
        e = (hr32(c, d + 0x23u) << 5) + hr32(c, d + 4u);
    } else if (old == 3u || old == 2u) {
        moves = 0;
        if (sx8(c, VA_RES_MODE) != 2) {
            uint32_t p = (old << 5) + hr32(c, d + 4u) + 0x18u;
            hw32(c, f, hr32(c, p));
            hw32(c, f + 4u, hr32(c, p + 4u));
            if (display_move(c, d, f)) goto refused;
        }
    }
    if (hr16(c, VA_PAGED)) {
        uint32_t dx = hr32(c, VA_PAGE_X) - hr32(c, e + 0x18u), dy = hr32(c, e + 0x1cu);
        hw32(c, f + 8u, hr32(c, v + 0x48u) + dx);
        hw32(c, f + 0xcu, hr32(c, v + 0x4cu) + dy);
        if (driver_call(c, VA_DRV_ORIGIN, (const uint32_t[]){0u, f + 8u}, 2)) goto refused;
    }
    uint32_t n = (mode << 5) + hr32(c, d + 4u);
    if (sx8(c, VA_RES_MODE) == 2) moves = 0;
    if (moves) {
        uint32_t x = hr32(c, n + 0x18u), y = hr32(c, n + 0x1cu), page = hr32(c, v + 0x3cu), q = v + 8u * page + 0x48u;
        if (hr16(c, VA_PAGED)) {
            x += hr32(c, VA_PAGE_X);
            hw32(c, f + 0x10u, hr32(c, q) + x);
            hw32(c, f + 0x14u, hr32(c, q + 4u) + y);
            if (driver_call(c, VA_DRV_ORIGIN, (const uint32_t[]){page, f + 0x10u}, 2)) goto refused;
        } else if (mode != 3u && mode != 2u) {
            hw32(c, f + 0x10u, hr32(c, q) + x);
            hw32(c, f + 0x14u, hr32(c, q + 4u) + y);
            if (driver_call(c, VA_DRV_ORIGIN, (const uint32_t[]){page, f + 0x10u}, 2)) goto refused;
        }
        uint32_t p = f + 0x18u;  /* the zero pair of 0x42a0cd */
        if (hr32(c, d + 0x14u) != 0xffffffffu) p = (hr32(c, d + 0x14u) << 5) + hr32(c, d + 4u) + 0x18u;
        hw32(c, f, hr32(c, p) - hr32(c, n + 0x18u));
        hw32(c, f + 4u, hr32(c, p + 4u) - hr32(c, n + 0x1cu));
        if (display_move(c, d, f)) goto refused;
    }
    hud_frame_pop(c, top);
    gc = hr32(c, VA_GC);
    gc_copy(c, gc, hr32(c, d + 0xcu) + mode * 0x204u);
    hw32(c, VA_PEN_RECORD, hr32(c, hr32(c, d + 0x10u) + 4u * mode));
    hw32(c, gc + 0x208u, 2);
    for (uint32_t k = 0; k < 6u; ++k) hw32(c, gc + 0x210u + 4u * k, hr32(c, n + 4u * k));
    if (sx8(c, VA_RES_MODE) != 2) {
        hw32(c, VA_ORIGIN, hr32(c, n + 0x18u));
        hw32(c, VA_ORIGIN + 4u, hr32(c, n + 0x1cu));
        add32(c, gc + 0x220u, hr32(c, VA_ORIGIN));
        add32(c, gc + 0x224u, hr32(c, VA_ORIGIN + 4u));
    } else {
        hw32(c, 0x004d2556u, hr32(c, n + 0x18u));
        hw32(c, 0x004d255au, hr32(c, n + 0x1cu));
    }
    return 0;
refused:
    hud_frame_pop(c, top);
    return 1;
}

/* 0x429de4(D, mode) (listing 0x429de4..0x429e5e), with word 0x4d2680 set:
 * the mode's bitmaps freed after the switch. Mode 0 at 400 lines: the
 * second bitmap [[D+8] + 4 mode + 4] (counter + 1, 0x48349c(b, 3), slot
 * <- 0); modes 0..2: the first bitmap [[D+8] + 4 mode] likewise, and mode 2
 * clears the second slot too. */
static int view_free(hud_ctx *c, uint32_t d, uint32_t mode) {
    if (mode > 2u) return 0;
    if (mode == 0u && sx8(c, VA_RES_MODE) == 1) {
        hud_counter(c, 1);
        if (HCALL(c, VA_BITMAP_DESTROY, NULL, hr32(c, hr32(c, d + 8u) + 4u * mode + 4u), 3u)) return 1;
        hw32(c, hr32(c, d + 8u) + 4u * mode + 4u, 0);
    }
    hud_counter(c, 1);
    if (HCALL(c, VA_BITMAP_DESTROY, NULL, hr32(c, hr32(c, d + 8u) + 4u * mode), 3u)) return 1;
    uint32_t t = hr32(c, d + 8u);
    hw32(c, t + 4u * mode, 0);
    if (mode == 2u) hw32(c, t + 4u * mode + 4u, 0);
    return 0;
}

/* 0x42a1f0(D, mode) (listing 0x42a1f0..0x42a3f1): the cockpit display set
 * to mode. Mode > 4, or mode 1 at 400 lines: nothing. The switch
 * (0x429e60); mode 4 then only stores D+0x14. Else: the mode's view shape
 * when word 0x4d2680 is set (0x429c2c); GC+0x208 cleared around the blit
 * of the mode's bitmap [[[D+8] + 4 mode] + 8] at (0, 0) (flag 2 in mode 3,
 * else 0; 0x4816a8; at 640x480 the stretch 0x4817c8, see below); at 400
 * lines mode 0 adds the second bitmap at y 0xc5, modes 2, 3 select page
 * [V+0x8c] with GC+0x22c on a local pair (0, word 0x4d3ba2) (left
 * dangling) and fill (0, 0xc5, 0x140, 0x1e0) (0x486704); the panel's full
 * draw for modes 0..3 (0x4316c0); the bitmaps freed when the word is set
 * (0x429de4); D+0x14 <- mode. The switch, the panel draw and the
 * free are called through the dispatch as the listing calls them (their
 * rows are this file's functions). EAX as the listing leaves it: the
 * entry's for mode > 4, 0 for mode 1 at 400 lines, the switch's for mode
 * 4, else the free's, the panel draw's or the saved GC+0x208 (the last
 * one run); EDX and ECX are not kept. */
static int display_set(hud_ctx *c, uint32_t d, uint32_t mode, uint32_t *eax) {
    if ((int32_t)mode > 4) return 0;   /* EAX the entry's */
    if (sx8(c, VA_RES_MODE) == 1 && mode == 1u) { *eax = 0; return 0; }
    if (HCALL(c, 0x00429e60u, eax, d, mode)) return 1;
    if (mode != 4u) {
        if (hr16(c, VA_SPLIT) && HCALL(c, 0x00429c2cu, NULL, d, mode)) return 1;
        /* The locals at the listing's frame offsets (ebp-0x3c .. ebp-4): the
         * point (ebp-8, ebp-4), the 640x480 size (ebp-0x18) and saved GC+0x21c
         * (ebp-0x1c), the pair (ebp-0x28) and the fill rectangle (ebp-0x3c). */
        uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0x3cu);
        if (!f) return hud_refuse(c, 0x0042a1f0u, "display set", "the own code's call area is exhausted");
        uint32_t e = f + 0x3cu, pt = e - 8u;
        uint32_t gc = hr32(c, VA_GC), saved = hr32(c, gc + 0x208u), flag = mode == 3u ? 2u : 0u;
        hw32(c, gc + 0x208u, 0);
        uint32_t table = hr32(c, d + 8u);
        if (sx8(c, VA_RES_MODE) == 2) {
            /* (listing 0x42a27d..0x42a2dd): 640x480 stretches the
             * mode's bitmap to 0x140 x 0x1e0 through scene.c's row 0x4817c8;
             * mode 3 with GC+0x208 <- 1, GC+0x21c <- 0x1df and x -1 (GC+0x21c
             * restored after the blit, +0x208 with the rest below). */
            uint32_t keep = hr32(c, gc + 0x21cu);
            hw32(c, e - 0x1cu, keep);
            hw32(c, e - 0x18u, 0x140u);
            hw32(c, e - 0x14u, 0x1e0u);
            if (mode == 3u) {
                hw32(c, gc + 0x208u, 1);
                hw32(c, gc + 0x21cu, hr32(c, e - 0x14u) - 1u);
                hw32(c, pt, 0xffffffffu);
            }
            if (HCALL(c, 0x004817c8u, NULL, hr32(c, hr32(c, hr32(c, table + 4u * mode) + 8u)), pt, flag, e - 0x18u)) { hud_frame_pop(c, top); return 1; }
            hw32(c, gc + 0x21cu, hr32(c, e - 0x1cu));
        } else if (HCALL(c, VA_BLIT, NULL, hr32(c, hr32(c, hr32(c, table + 4u * mode) + 8u)), pt, flag)) { hud_frame_pop(c, top); return 1; }
        if (sx8(c, VA_RES_MODE) == 1) {
            uint32_t v = hr32(c, VA_VIEW);
            hw32(c, e - 0x3cu, 0);
            hw32(c, e - 0x38u, 0xc5u);
            hw32(c, e - 0x34u, 0x140u);
            hw32(c, e - 0x30u, 0x1e0u);
            if (mode == 0u) {
                add32(c, pt + 4u, 0xc5u);
                if (HCALL(c, VA_BLIT, NULL, hr32(c, hr32(c, hr32(c, hr32(c, d + 8u) + 4u * mode + 4u) + 8u)), pt, flag)) { hud_frame_pop(c, top); return 1; }
            } else if (mode == 2u || mode == 3u) {
                hw32(c, e - 0x28u, 0);
                hw32(c, e - 0x24u, (uint32_t)sx16(c, 0x004d3ba2u));
                hw32(c, gc + 0x22cu, e - 0x28u);
                if (page_select(c, v, hr32(c, v + 0x8cu))) { hud_frame_pop(c, top); return 1; }
                /* The fill (0, 0xc5, 0x140, 0x1e0) draws (RD's row). */
                if (HCALL(c, 0x00486704u, NULL, e - 0x3cu)) { hud_frame_pop(c, top); return 1; }
            }
        }
        hud_frame_pop(c, top);
        hw32(c, gc + 0x208u, saved);
        *eax = saved;   /* 0x42a3b8, then the calls' */
        if (mode < 4u && HCALL(c, 0x004316c0u, eax, hr32(c, d + 0x1fu))) return 1;
        if (hr16(c, VA_SPLIT) && HCALL(c, 0x00429de4u, eax, d, mode)) return 1;
    }
    hw32(c, d + 0x14u, mode);
    return 0;
}

/* ---- service rows */
void es2_sim_service_display_set(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t eax = sc->eax;
    if (display_set(&c, sc->slots[0], sc->slots[1], &eax)) { hud_failed(sc); return; }
    sc->eax = eax;
}
void es2_sim_service_display_switch(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (display_switch(&c, sc->slots[0], sc->slots[1])) { hud_failed(sc); return; }
    sc->eax = hr32(&c, VA_GC);
}
void es2_sim_service_display_current(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    sc->eax = hr32(&c, VA_DISPLAY);
}
void es2_sim_service_jolt_end(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (jolt_end(&c)) hud_failed(sc);
}
void es2_sim_service_view_border(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t eax = 0;
    if (view_border(&c, &eax)) { hud_failed(sc); return; }
    sc->eax = eax;
}
void es2_sim_service_view_free(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (view_free(&c, sc->slots[0], sc->slots[1])) hud_failed(sc);
}
/* 0x431500(P) / 0x431518(P): byte P+0x20f (hidden) set / cleared; EAX P. */
void es2_sim_service_panel_hide(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    hw8(&c, sc->slots[0] + 0x20fu, 1);
    sc->eax = sc->slots[0];
}
void es2_sim_service_panel_show(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    hw8(&c, sc->slots[0] + 0x20fu, 0);
    sc->eax = sc->slots[0];
}
void es2_sim_service_panel_move(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (panel_move(&c, sc->slots[0], sc->slots[1])) hud_failed(sc);
}
void es2_sim_service_panel_draw(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (panel_draw(&c, sc->slots[0])) hud_failed(sc);
}
void es2_sim_service_panel_slide(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (panel_slide(&c, sc->slots[0], sc->slots[1])) hud_failed(sc);
}
void es2_sim_service_port_move(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    port_move(&c, sc->slots[0], sc->slots[1]);
}
void es2_sim_service_rect_move(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    rect_move(&c, sc->slots[0], sc->slots[1]);
    sc->eax = sc->slots[0];
}
/* 0x452bec(W, x, y): 0x45275c(W, &x) (the point by value). */
void es2_sim_service_widget_move_value(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t top = c.l->stack_hi, f = hud_frame_push(&c, 8u);
    if (!f) { sc->refused = 1; sc->reason = "the own code's call area is exhausted"; return; }
    hw32(&c, f, sc->slots[1]);
    hw32(&c, f + 4u, sc->slots[2]);
    int bad = widget_move(&c, sc->slots[0], f);
    hud_frame_pop(&c, top);
    if (bad) hud_failed(sc);
}
void es2_sim_service_widget_move(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (widget_move(&c, sc->slots[0], sc->slots[1])) hud_failed(sc);
}
/* 0x48990e(page, pt) -> 0x4649d8(page, pt) (listing 0x4649d8..0x464a10),
 * the BMP_8 driver's page origin: the surface descriptor [0x4d2ff0]'s
 * pointer +0xa and the first row [[0x4a03d0]] <- pt.y * word desc+2 +
 * [0x4d2ff4] + pt.x; 0x4d3018/0x4d301c <- pt. */
void es2_sim_service_page_origin(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t pt = sc->slots[1], desc = hr32(&c, 0x004d2ff0u);
    uint32_t p = (uint32_t)(int32_t)(int16_t)hr16(&c, desc + 2u) * hr32(&c, pt + 4u) + hr32(&c, 0x004d2ff4u) + hr32(&c, pt);
    hw32(&c, desc + 0xau, p);
    hw32(&c, hr32(&c, 0x004a03d0u), p);
    hw32(&c, VA_PAGE_X, hr32(&c, pt));
    sc->eax = hr32(&c, pt + 4u);
    hw32(&c, VA_PAGE_Y, sc->eax);
}
/* The empty BMP_8 driver slots 0x4898fc, 0x489902, 0x489908 (enter/leave)
 * and 0x4898d4, 0x4898df (register saves only); EAX is not set by them and
 * no caller reads it. */
void es2_sim_service_hud_noop(es2_sim_service_call *sc, void *user) {
    (void)user;
    sc->eax = 0;
}
/* 0x433158() (listing 0x433158..0x433170): port A of the cockpit panel
 * [0x49b088] (+0x207), 0 without a panel. */
void es2_sim_service_port_a(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t p = hr32(&c, 0x0049b088u);
    sc->eax = p ? hr32(&c, p + 0x207u) : 0u;
}

/* ======================================================================
 * The full draws (slot +4) of the panel's elements, reached from 0x4316c0
 * at the layout set and from the per-frame redraws. */
#define WSLOT0(c, w) hud_call((c), hr32((c), hr32((c), (w) + 0x17u)), (const uint32_t[]){(w)}, 1, NULL)

/* 0x440c90(G) (listing 0x440c90..0x440cb6), the weapon gauges' full draw:
 * with byte G+0x8c set (powered) the draws (widget slot 0) of the two
 * widgets [G+0x18] and [G+0x1c]. */
void es2_sim_service_weapon_gauge_draw(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t g = sc->slots[0];
    if (hr8(&c, g + 0x8cu) && (WSLOT0(&c, hr32(&c, g + 0x18u)) || WSLOT0(&c, hr32(&c, g + 0x1cu)))) hud_failed(sc);
}
