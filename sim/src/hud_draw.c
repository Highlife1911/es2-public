/* The full draws (method slot +4) of the cockpit panel's elements and the HUD,
 * and the draws they call, as own code. They run at the layout set of the load
 * (0x42a1f0 -> 0x4316c0) and on the full redraws of the mission. Contract:
 * hud.md, "the HUD line contract — coupling set and frame writes" (the coupling
 * table), "the HUD line in the own process" and "the HUD's pixels"; every
 * function names its listing range of the
 * reference DBSIM.EXE (llvm-objdump -d -M intel, SHA-256 8561aa47...c4ad).
 *
 * Kept: every gate, every coupling call (the mission-time clock 0x43dce4, the
 * radar blip scan 0x43ed34 and its button_press), every call of other code
 * through the dispatch, the state the functions leave in objects and globals
 * (texts, positions, saved rectangles, the GC fields left pointing at a local),
 * the canvas push/pop. The pixels are drawn through the graphics rows
 * (the blits 0x4816a8/0x4817c8, the fills, lines, points and ellipses 0x486704,
 * 0x488143, 0x48817c, 0x483a04, 0x4895ec, 0x482cec, 0x488012, the rotated blit
 * 0x48185c, the text 0x482634 and 0x482534), with every rectangle, point and
 * colour record in a guest local laid out as the original's and alive while the
 * callee runs (the callees read the records through GC+0x22c/+0x228 and may
 * change their arguments). The shield palette 0x443918 (src/display.c), the
 * altitude ladder 0x43dd98 (src/hud_frame.c), the radio records
 * 0x44aea0/0x44b3dc and the HDD title 0x44a704 (src/mfd.c) are other files'
 * rows reached through the dispatch. The gunsights, the nav gauge, the roving
 * gunsight and the fixed rotation gauge are own code in src/gadgets.c. Nothing
 * here counts `headless`. */
#include "hud_internal.h"
#include "gadgets_internal.h"

#define VA_GC 0x006c5f70u
#define VA_VIEW 0x006c5ed0u
#define VA_PANEL 0x0049b088u
#define VA_DESC 0x004d24bcu        /* the display descriptor of the blits */
#define VA_ORIGIN_X 0x004d2556u    /* the 640x480 offsets */
#define VA_ORIGIN_Y 0x004d255au
#define VA_TEXT_X 0x004d17b4u      /* the label text position */
#define VA_TEXT_Y 0x004d17b8u
#define VA_COLOURS 0x0049b0acu     /* the label colour table */
#define VA_BLIT 0x004816a8u
#define VA_SCALED_BLIT 0x004817c8u
/* The graphics rows the full draws call (own code in src/gfx.c and its
 * siblings) */
#define VA_FILL 0x00486704u        /* rect_fill(rect), colour GC+0x22c */
#define VA_RESTORE 0x00488143u     /* bitmap_rect_blit(rect, at, bitmap) */
#define VA_ELLIPSE 0x0048817cu     /* gfx_ellipse(rect), colour GC+0x22c */
#define VA_LINE 0x00483a04u        /* fill_line(p0, p1), colour GC+0x228 */
#define VA_STRING 0x00482634u      /* gfx_string(font, pos, text) */
#define VA_CHAR 0x00482534u        /* gfx_char(font, pos, ch) */
#define VA_SAVE 0x00488012u        /* gfx_save(rect, bitmap) */
#define VA_ROTATED 0x0048185cu     /* rotated_blit(bitmap, at, 0, to, turn) */
#define VA_POINT 0x004895ecu       /* gfx_point(pos), colour GC+0x228 */
#define VA_OBJECT_FILL 0x00482cecu /* gl_object_rect_fill(object) */
#define VA_VISIBLE 0x00431410u     /* panel_visible (hook) */
#define VA_CANVAS_PUSH 0x004311e0u
#define VA_CANVAS_POP 0x00431210u
#define VA_GC_SELECT 0x00480d44u
#define VA_VIEW_TRANSFORM 0x0048c444u

static int32_t sx16(const hud_ctx *c, uint32_t a) { return (int32_t)(int16_t)hr16(c, a); }
static void add32(const hud_ctx *c, uint32_t a, uint32_t v) { hw32(c, a, hr32(c, a) + v); }
static unsigned shx(const hud_ctx *c) { return hr16(c, 0x004d253au) & 31u; }

/* Widget slot 0 (the draw, [[W+0x17]]) and object slot 0 ([[O]]). */
static int wdraw(hud_ctx *c, uint32_t w) { return hud_call(c, hr32(c, hr32(c, w + 0x17u)), (const uint32_t[]){w}, 1, NULL); }
static int odraw(hud_ctx *c, uint32_t o) { return hud_call(c, hr32(c, hr32(c, o)), (const uint32_t[]){o}, 1, NULL); }
static int visible(hud_ctx *c, uint32_t rect, int *yes) {
    uint32_t al = 0;
    if (HCALL(c, VA_VISIBLE, &al, rect)) return 1;
    *yes = (al & 0xffu) != 0;
    return 0;
}
static int canvas_push(hud_ctx *c, uint32_t *canvas) { return HCALL0(c, VA_CANVAS_PUSH, canvas); }
static int canvas_pop(hud_ctx *c) { return HCALL0(c, VA_CANVAS_POP, NULL); }
/* 0x471f24(p): the signed word [p] (an animation's state). */
static int32_t anim_state(hud_ctx *c, uint32_t p) { return sx16(c, p); }
/* The blink-off or animation test of the weapon draws (0x4411fd..,
 * 0x4423d3..): byte +0x76 set, or an animation [+0x6c] whose state is not
 * 0. */
static int busy(hud_ctx *c, uint32_t g) {
    if (hr8(c, g + 0x76u)) return 1;
    uint32_t a = hr32(c, g + 0x6cu);
    return a && anim_state(c, a) != 0;
}

/* 0x438920(L, text) (listing 0x438920..0x438a3f): the label's text. The
 * position 0x4d17b4/0x4d17b8: x from word L+0x14, centred (flag byte
 * L+0x19 bit 2: minus half of the width less one pixel unit, sar 1) or
 * right-aligned (bit 4: minus that width; the width is 0x4824ec's), y word
 * L+0x16. With a background colour [L+0x1d] the GC G = [[0x6c5ed0]+0x44]
 * - 4 (0 when that is 0: the original reads [0x22c] then) gets +0x22c <-
 * the local record (0, [L+0x1d]), the label's rectangle L+4..+0x10 (less
 * the 640x480 offsets when byte L+0x18) filled (0x486704) and +0x22c put
 * back. Byte L+0x18 subtracts the offsets from the text position; the
 * text 0x482634([L], 0x4d17b4, text). */
static int label_draw(hud_ctx *c, uint32_t l, uint32_t text) {
    int32_t w = (int32_t)(hud_text_width(c, hr32(c, l), text) - (1u << shx(c)));
    uint8_t f = hr8(c, l + 0x19u);
    int32_t x = sx16(c, l + 0x14u);
    if (f & 2u) x -= w >> 1;
    else if (f & 4u) x -= w;
    hw32(c, VA_TEXT_X, (uint32_t)x);
    hw32(c, VA_TEXT_Y, (uint32_t)sx16(c, l + 0x16u));
    uint32_t g = hr32(c, hr32(c, VA_VIEW) + 0x44u);
    if (g) g -= 4u;
    if (hr32(c, l + 0x1du)) {
        if (!g) return hud_null_refuse(c, 0x004389a5u, "label draw", "no GC in the view (the original reads [0x22c])");
        uint32_t top = c->l->stack_hi, r = hud_frame_push(c, 0x18u);
        if (!r) return hud_refuse(c, 0x00438920u, "label draw", "the own code's call area is exhausted");
        uint32_t saved = hr32(c, g + 0x22cu);
        hw32(c, r + 0x10u, 0);
        hw32(c, g + 0x22cu, r + 0x10u);
        hw32(c, r + 0x14u, hr32(c, l + 0x1du));
        for (uint32_t k = 0; k < 4u; ++k) hw32(c, r + 4u * k, hr32(c, l + 4u + 4u * k));
        if (hr8(c, l + 0x18u))
            for (uint32_t k = 0; k < 4u; ++k) add32(c, r + 4u * k, 0u - hr32(c, (k & 1u) ? VA_ORIGIN_Y : VA_ORIGIN_X));
        int bad = HCALL(c, VA_FILL, NULL, r);
        hud_frame_pop(c, top);
        if (bad) return 1;
        hw32(c, g + 0x22cu, saved);
    }
    if (hr8(c, l + 0x18u)) {
        hw32(c, VA_TEXT_X, hr32(c, VA_TEXT_X) - hr32(c, VA_ORIGIN_X));
        hw32(c, VA_TEXT_Y, hr32(c, VA_TEXT_Y) - hr32(c, VA_ORIGIN_Y));
    }
    return HCALL(c, VA_STRING, NULL, hr32(c, l), VA_TEXT_X, text);
}
/* A number through _itoa (radix 10) into a local buffer, then 0x438920
 * (0x438a40(L, n), listing 0x438a40..0x438a6d, and the inline copies). */
static int label_number(hud_ctx *c, uint32_t l, uint32_t n) {
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0x20u);
    if (!f) return hud_refuse(c, 0x00438a40u, "label number", "the own code's call area is exhausted");
    int bad = HCALL(c, HUD_VA_ITOA, NULL, n, f, 10u);
    if (!bad) bad = label_draw(c, l, f);
    hud_frame_pop(c, top);
    return bad;
}
/* 0x45c8bc(desc, B, pt) (listing 0x45c8bc..0x45c963): bitmap B at pt less
 * desc+0x9a. Scaled descriptors (byte desc+0x7b = 3, byte desc+0xb0 = 0):
 * the scaled size 0x4d26d8/0x4d26dc <- words B+6, B+4 shifted by words
 * desc+0x7e/+0x80, then 0x4817c8 (through the dispatch, not reconstructed);
 * else the blit 0x4816a8. */
static int blit_at(hud_ctx *c, uint32_t desc, uint32_t b, uint32_t pt) {
    if (!b) return 0;
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 8u);
    if (!f) return hud_refuse(c, 0x0045c8bcu, "bitmap at", "the own code's call area is exhausted");
    hw32(c, f, hr32(c, pt) - hr32(c, desc + 0x9au));
    hw32(c, f + 4u, hr32(c, pt + 4u) - hr32(c, desc + 0x9eu));
    int bad;
    if ((int8_t)hr8(c, desc + 0x7bu) == 3 && !hr8(c, desc + 0xb0u)) {
        hw32(c, 0x004d26d8u, (uint32_t)sx16(c, b + 6u) << (hr16(c, desc + 0x7eu) & 31u));
        hw32(c, 0x004d26dcu, (uint32_t)sx16(c, b + 4u) << (hr16(c, desc + 0x80u) & 31u));
        bad = HCALL(c, VA_SCALED_BLIT, NULL, b, f, 0u, 0x004d26d8u);
    } else {
        bad = HCALL(c, VA_BLIT, NULL, b, f, 0u);
    }
    hud_frame_pop(c, top);
    return bad;
}
void es2_sim_service_blit_at(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (blit_at(&c, sc->slots[0], sc->slots[1], sc->slots[2])) hud_failed(sc);
}
/* 0x45c964(desc, B, pt, z) (listing 0x45c964..0x45ca0e), 0x45c8bc with the
 * blits' third argument z instead of 0. Registers: EAX and EDX the blit's, ECX
 * the local y popped at the end (pt.y less [desc+0x9e]); B = 0 leaves EAX =
 * desc, EDX = 0 and ECX the unwritten local's stack residue, here 0 (the zero
 * model, own_process.md, "the headless acceptance"). */
void es2_sim_service_blit_at_z(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t desc = sc->slots[0], b = sc->slots[1], pt = sc->slots[2], z = sc->slots[3];
    if (!b) {
        sc->eax = desc;
        sc->edx = 0;
        sc->ecx = 0;
        return;
    }
    uint32_t top = c.l->stack_hi, f = hud_frame_push(&c, 8u);
    if (!f) {
        hud_refuse(&c, 0x0045c964u, "bitmap at z", "the own code's call area is exhausted");
        hud_failed(sc);
        return;
    }
    hw32(&c, f, hr32(&c, pt) - hr32(&c, desc + 0x9au));
    hw32(&c, f + 4u, hr32(&c, pt + 4u) - hr32(&c, desc + 0x9eu));
    es2_sim_result r;
    int bad;
    if ((int8_t)hr8(&c, desc + 0x7bu) == 3 && !hr8(&c, desc + 0xb0u)) {
        hw32(&c, 0x004d26d8u, (uint32_t)sx16(&c, b + 6u) << (hr16(&c, desc + 0x7eu) & 31u));
        hw32(&c, 0x004d26dcu, (uint32_t)sx16(&c, b + 4u) << (hr16(&c, desc + 0x80u) & 31u));
        bad = hud_call_regs(&c, VA_SCALED_BLIT, (const uint32_t[]){b, f, z, 0x004d26d8u}, 4, NULL, &r);
    } else {
        bad = hud_call_regs(&c, VA_BLIT, (const uint32_t[]){b, f, z}, 3, NULL, &r);
    }
    uint32_t y = hr32(&c, f + 4u);
    hud_frame_pop(&c, top);
    if (bad) { hud_failed(sc); return; }
    sc->eax = r.eax; sc->edx = r.edx; sc->ecx = y;
}
void es2_sim_service_label_draw(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (label_draw(&c, sc->slots[0], sc->slots[1])) hud_failed(sc);
}

/* ======================================================================
 * The panel elements' full draws (slot +4). */

/* 0x4411dc(G) (listing 0x4411dc..0x44128f), ProjectileWeaponGauge: when
 * powered (+0x8c) the widget [G+0x18]; then unless busy, with the label
 * [G+0xdb] visible, the widget not disabled (word +0x1b = 2) and byte
 * G+0xd3 clear: the ammunition count G+0xc2 as text, the label colour
 * [0x49b0ac + 4 (byte G+0xd6 ? 10 : 11)]. */
static int projectile_draw(hud_ctx *c, uint32_t g) {
    if (!hr8(c, g + 0x8cu)) return 0;
    if (wdraw(c, hr32(c, g + 0x18u))) return 1;
    if (busy(c, g)) return 0;
    uint32_t l = hr32(c, g + 0xdbu);
    int yes;
    if (visible(c, l + 4u, &yes)) return 1;
    if (!yes || hr16(c, hr32(c, g + 0x18u) + 0x1bu) == 2u || hr8(c, g + 0xd3u)) return 0;
    hw32(c, l, hr32(c, VA_COLOURS + 4u * (hr8(c, g + 0xd6u) ? 10u : 11u)));
    return label_number(c, l, hr32(c, g + 0xc2u));
}
/* 0x442080(G) (listing 0x442080..0x4420a2), WeaponRangeGauge: the four
 * widgets [G+0x18..+0x24]. */
static int range_draw(hud_ctx *c, uint32_t g) {
    for (uint32_t i = 0; i < 4u; ++i) if (wdraw(c, hr32(c, g + 0x18u + 4u * i))) return 1;
    return 0;
}
/* 0x444f48(G) (listing 0x444f48..0x445005), EnergyPool (WeaponSelect)
 * gauge: with the bar [G+0xb5] (rectangle +4) visible: GC+0x22c <- a dead
 * local; an own context [G+0xbf] gets the GC's rectangle +0x210..+0x21c
 * and origin +0x220/+0x224 and is selected (0x480d44); the bar's slot 0
 * with the word G+0xb1; the GC selected again. */
static int pool_draw(hud_ctx *c, uint32_t g) {
    int yes;
    if (visible(c, hr32(c, g + 0xb5u) + 4u, &yes)) return 1;
    if (!yes) return 0;
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 8u);
    if (!f) return hud_refuse(c, 0x00444f48u, "pool draw", "the own code's call area is exhausted");
    uint32_t gc = hr32(c, VA_GC), b = hr32(c, g + 0xbfu);
    hw32(c, gc + 0x22cu, f);
    int bad = 0;
    uint32_t eax = b;  /* the slot of the word argument keeps EAX's upper half */
    if (b) {
        for (uint32_t k = 0; k < 6u; ++k) hw32(c, b + 0x210u + 4u * k, hr32(c, gc + 0x210u + 4u * k));
        bad = HCALL(c, VA_GC_SELECT, &eax, hr32(c, g + 0xbfu));
    }
    uint32_t bar = hr32(c, g + 0xb5u);
    if (!bad) bad = hud_call(c, hr32(c, hr32(c, bar)), (const uint32_t[]){bar, (eax & 0xffff0000u) | hr16(c, g + 0xb1u)}, 2, NULL);
    if (!bad) bad = HCALL(c, VA_GC_SELECT, NULL, gc);
    hud_frame_pop(c, top);
    return bad;
}
/* 0x447d60(G) (listing 0x447d60..0x447d90), ThrottleGauge: the widget
 * [G+0x18], then the scale bitmap [[[0x49cfc4]+8]] at G+0xc2 (0x4816a8). */
static int throttle_draw(hud_ctx *c, uint32_t g) {
    if (wdraw(c, hr32(c, g + 0x18u))) return 1;
    return HCALL(c, VA_BLIT, NULL, hr32(c, hr32(c, hr32(c, 0x0049cfc4u) + 8u)), g + 0xc2u, 0u);
}
/* The GC's 0x81 dwords at +4 and its +0x208 (saved and restored around
 * an own rectangle by 0x446160 and 0x449ee4). */
static void gc_save(hud_ctx *c, uint32_t gc, uint32_t to) {
    for (uint32_t i = 0; i < 0x81u; ++i) hw32(c, to + 4u * i, hr32(c, gc + 4u + 4u * i));
}
static void gc_load(hud_ctx *c, uint32_t gc, uint32_t from) {
    for (uint32_t i = 0; i < 0x81u; ++i) hw32(c, gc + 4u + 4u * i, hr32(c, from + 4u * i));
}
/* 0x446160(G) (listing 0x446160..0x44634d), MFDGauge: with G+0xeb
 * visible: the buttons 0..5 marked in the page's row of 0x49cbd8 (13
 * bytes per page) drawn; the GC's 0x81 dwords and +0x208 saved, the MFD's
 * own rectangle (G+0x104) loaded, +0x208 <- 2, +0x22c <- a local; the
 * page frame bitmap (pages 0, 1, 4: [[[0x49cbcc]+8]+4], page 3: +8, pages
 * 2, 5: none) at G+0xeb (0x45c8bc); the marked buttons 0..12 drawn again;
 * unless blinking off (+0x76): page 3 before the sweep is done (+0x8d = 0)
 * shows the radar's bitmap [[[G+0x341]+8]] at [G+0xd9]+0x18, else the
 * page object [G+0xcd + 4 page] draws (slot 0); the page title label
 * [G+0xc9] with [0x4d135c + 4 page]; the GC restored. */
static int mfd_draw(hud_ctx *c, uint32_t g) {
    int yes;
    if (visible(c, g + 0xebu, &yes)) return 1;
    if (!yes) return 0;
    for (uint32_t i = 0; i < 6u; ++i)
        if (hr8(c, 0x0049cbd8u + hr32(c, g + 0xc1u) * 13u + i) && wdraw(c, hr32(c, g + 0x18u + 4u * i))) return 1;
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0x210u);
    if (!f) return hud_refuse(c, 0x00446160u, "MFD draw", "the own code's call area is exhausted");
    uint32_t gc = hr32(c, VA_GC), saved = hr32(c, gc + 0x208u);
    gc_save(c, gc, f + 8u);
    gc_load(c, gc, g + 0x104u);
    hw32(c, gc + 0x208u, 2);
    hw32(c, gc + 0x22cu, f);
    uint32_t page = hr32(c, g + 0xc1u), frame = 0;
    int bad = 0;
    switch (page) {
    case 0: case 1: case 4: frame = hr32(c, hr32(c, hr32(c, 0x0049cbccu) + 8u) + 4u); break;
    case 3: frame = hr32(c, hr32(c, hr32(c, 0x0049cbccu) + 8u) + 8u); break;
    case 2: case 5: break;
    default: bad = hud_refuse(c, 0x0044621fu, "MFD draw", "a page above 5 reads an unset local in the original");
    }
    if (!bad && frame) bad = blit_at(c, VA_DESC, frame, g + 0xebu);
    for (uint32_t i = 0; !bad && i < 13u; ++i)
        if (hr8(c, 0x0049cbd8u + hr32(c, g + 0xc1u) * 13u + i)) bad = wdraw(c, hr32(c, g + 0x18u + 4u * i));
    if (!bad && !hr8(c, g + 0x76u)) {
        if (!hr8(c, g + 0x8du) && hr32(c, g + 0xc1u) == 3u)
            bad = blit_at(c, VA_DESC, hr32(c, hr32(c, hr32(c, g + 0x341u) + 8u)), hr32(c, g + 0xd9u) + 0x18u);
        else bad = odraw(c, hr32(c, g + 0xcdu + 4u * hr32(c, g + 0xc1u)));
    }
    if (!bad) bad = label_draw(c, hr32(c, g + 0xc9u), hr32(c, 0x004d135cu + 4u * hr32(c, g + 0xc1u)));
    gc = hr32(c, VA_GC);
    gc_load(c, gc, f + 8u);
    hw32(c, gc + 0x208u, saved);
    hud_frame_pop(c, top);
    return bad;
}

/* ---- the HDD (0x448cf0) */

/* 0x41bac8(V) (listing 0x41bac8..0x41bb39): the order symbol of actor V:
 * the word [[V+0x45]+0x3c]; unless V is dead or removed (bytes V+0xa4,
 * V+0x99), the order 5, or byte [V+0x45]+9 set, the word V+0x23e maps 1
 * -> 1, 2 -> 2, 3 and 6 -> 4. */
static uint16_t order_symbol(hud_ctx *c, uint32_t v) {
    uint16_t d = hr16(c, hr32(c, v + 0x4du) + 0x3cu);
    if (hr8(c, v + 0x92u + 0x12u) || hr8(c, v + 0x92u + 7u) || d == 5u || hr8(c, hr32(c, v + 0x4du) + 9u)) return d;
    switch ((int16_t)hr16(c, v + 0x23eu)) {
    case 1: return 1;
    case 2: return 2;
    case 3: case 6: return 4;
    default: return d;
    }
}
/* 0x449ee4(H) (listing 0x449ee4..0x449f61): the current HDD page P =
 * [H + 0x101 + 4 [H+0x109]] with the GC saved: P+0xc <- the signed byte
 * H+0xb0 (the redraw counter), P's slot 0, the counter decremented, the
 * GC's 0x81 dwords and +0x208 restored. */
static int hdd_page_draw(hud_ctx *c, uint32_t h) {
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0x204u);
    if (!f) return hud_refuse(c, 0x00449ee4u, "HDD page draw", "the own code's call area is exhausted");
    uint32_t gc = hr32(c, VA_GC), saved = hr32(c, gc + 0x208u);
    gc_save(c, gc, f);
    uint32_t p = hr32(c, h + 0x101u + 4u * hr32(c, h + 0x109u));
    hw32(c, p + 0xcu, hsx8(hr8(c, h + 0xb0u)));
    int bad = odraw(c, hr32(c, h + 0x101u + 4u * hr32(c, h + 0x109u)));
    hw8(c, h + 0xb0u, (uint8_t)(hr8(c, h + 0xb0u) - 1u));
    gc_load(c, gc, f);
    hw32(c, gc + 0x208u, saved);
    hud_frame_pop(c, top);
    return bad;
}
/* 0x449a78(H) (listing 0x449a78..0x449bf6), HDDGauge: with H+0xc1
 * visible: the GC G = [0x6c5f70] taken; the [H+0x68] buttons [H+0x18..];
 * the current page (when set) slot +0xc, then 0x449ee4; G+0x22c <- the
 * local record (0, word 0x4d3b9a when byte H+0x51f, else word 0x4d3b96),
 * the title rectangle H+0xf1 filled (0x486704, no offsets) and the title
 * 0x44a704 (src/mfd.c); per radio record k = 0..2 (S = H + 0x12d + 0x14e
 * k) its rectangle S+0x1d <- [H+0x40+4k]'s four dwords and a local copy
 * inset by one; a record with an actor (S+0x137) draws (0x44aea0,
 * src/mfd.c), one without has G+0x22c <- the record, its colour <- word
 * 0x4d3ba2 and the inset rectangle less the 640x480 offsets filled. The
 * fills are drawn, the record draws go through the dispatch. */
static int hdd_draw(hud_ctx *c, uint32_t h) {
    int yes;
    if (visible(c, h + 0xc1u, &yes)) return 1;
    if (!yes) return 0;
    uint32_t gc = hr32(c, VA_GC);
    for (uint32_t i = 0; (int32_t)i < (int32_t)hr32(c, h + 0x68u); ++i) if (wdraw(c, hr32(c, h + 0x18u + 4u * i))) return 1;
    uint32_t p = hr32(c, h + 0x101u + 4u * hr32(c, h + 0x109u));
    if (p) {
        if (hud_call(c, hr32(c, hr32(c, p) + 0xcu), (const uint32_t[]){p}, 1, NULL) || hdd_page_draw(c, h)) return 1;
    }
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0x28u);
    if (!f) return hud_refuse(c, 0x00449a78u, "HDD draw", "the own code's call area is exhausted");
    const uint32_t rec = f + 0x1cu;  /* ebp-0xc; the inset rectangle at ebp-0x28 */
    hw32(c, f + 0x24u, gc);
    hw32(c, rec, 0);
    hw32(c, gc + 0x22cu, rec);
    hw32(c, rec + 4u, (uint32_t)sx16(c, hr8(c, h + 0x51fu) ? 0x004d3b9au : 0x004d3b96u));
    int bad = HCALL(c, VA_FILL, NULL, h + 0xf1u);
    if (!bad) bad = HCALL(c, 0x0044a704u, NULL, h);
    for (uint32_t k = 0; !bad && k < 3u; ++k) {
        uint32_t s = h + 0x12du + 0x14eu * k, r = hr32(c, h + 0x40u + 4u * k);
        for (uint32_t j = 0; j < 4u; ++j) hw32(c, s + 0x1du + 4u * j, hr32(c, r + 4u * j));
        hw32(c, f + 0x10u, h + 0x40u + 4u * k);  /* the copy's pointers (ebp-0x18, -0x14, -0x10) */
        hw32(c, f + 0x14u, s + 0x25u);
        hw32(c, f + 0x18u, r + 8u);
        for (uint32_t j = 0; j < 4u; ++j) hw32(c, f + 4u * j, hr32(c, s + 0x1du + 4u * j) + (j < 2u ? 1u : 0xffffffffu));
        if (hr32(c, s + 0x137u)) {
            bad = HCALL(c, 0x0044aea0u, NULL, h, k);
        } else {
            hw32(c, gc + 0x22cu, rec);
            hw32(c, rec + 4u, (uint32_t)sx16(c, 0x004d3ba2u));
            for (uint32_t j = 0; j < 4u; ++j) add32(c, f + 4u * j, 0u - hr32(c, (j & 1u) ? VA_ORIGIN_Y : VA_ORIGIN_X));
            bad = HCALL(c, VA_FILL, NULL, f);
        }
    }
    hud_frame_pop(c, top);
    return bad;
}

/* ---- the HUD (0x43c800) */

/* 0x43dcd4(H) (listing 0x43dcd4..0x43dd95): the mission time. The clock
 * (site 0x43dce4); above the deadline [0x4d1804] (unsigned): deadline <-
 * now + 60, the "MM" 0x4d17dc / "SS" 0x4d17df digits advance one second
 * (99:59 wraps to 00:00), 0x4d17e4 <- "MM" + [0x4d17e2] + "SS". */
static int mission_time(hud_ctx *c) {
    uint32_t now = 0;
    if (hud_clock(c, 0x0043dce4u, &now)) return 1;
    if (now <= hr32(c, 0x004d1804u)) return 0;
    hw32(c, 0x004d1804u, now + 0x3cu);
    const uint32_t m = 0x004d17dcu, s = 0x004d17dfu;
    hw8(c, s + 1u, (uint8_t)(hr8(c, s + 1u) + 1u));
    if ((int8_t)hr8(c, s + 1u) > 0x39) {
        hw8(c, s + 1u, 0x30);
        hw8(c, s, (uint8_t)(hr8(c, s) + 1u));
        if ((int8_t)hr8(c, s) > 0x35) {
            hw8(c, s, 0x30);
            hw8(c, m + 1u, (uint8_t)(hr8(c, m + 1u) + 1u));
            if ((int8_t)hr8(c, m + 1u) > 0x39 && (int8_t)hr8(c, m) == 0x39) {
                hw8(c, s + 1u, 0x30);
                hw8(c, s, 0x30);
                hw8(c, m + 1u, 0x30);
                hw8(c, m, 0x30);
            } else if ((int8_t)hr8(c, m + 1u) > 0x39) {
                hw8(c, m + 1u, 0x30);
                hw8(c, m, (uint8_t)(hr8(c, m) + 1u));
            }
        }
    }
    hud_gstrcpy(c, 0x004d17e4u, m);
    return HCALL(c, HUD_VA_STRCAT, NULL, 0x004d17e4u, 0x004d17e2u) || HCALL(c, HUD_VA_STRCAT, NULL, 0x004d17e4u, s);
}
/* 0x43dca0(H) (listing 0x43dca0..0x43dcd2): the speed text 0x4d17ee <-
 * hud_speed([0x4d24e6]) (hook) in radix 10 and [0x49c112]. */
static int speed_text(hud_ctx *c) {
    uint32_t v = 0;
    if (HCALL(c, 0x0041bb3cu, &v, hr32(c, 0x004d24e6u))) return 1;
    return HCALL(c, HUD_VA_ITOA, NULL, v, 0x004d17eeu, 10u) || HCALL(c, HUD_VA_STRCAT, NULL, 0x004d17eeu, 0x0049c112u);
}
/* 0x43ec08(R) (listing 0x43ec08..0x43eeda): the radar's blip scan.
 * R+0x518 (count) and +0x51c (the target's blip) <- 0; camera_current
 * (hook); the range R+0x30 <- [[R+8]+0xc]; the player P = [H+0x203], the
 * target T = [H+0x210]. The radar key: ebx = 1 when the player's byte
 * +0x96 is 0; on a change against R+0x34: button_press([H+0x1ed] (0x445234
 * -> 0x447919), ebx ? 11 : 12, 1) (hook); R+0x34 <- ebx. Per actor of
 * [0x4a9af8] (count word 0x4a9afe), not the player, not dead or removed,
 * group [[+0x45]+0x14] = 0, not a hidden building (word +0x1a8 = 1 with
 * word [[+0x1f2]+0x1e] set): side s = signed byte [[+0x45]+0x12]; an
 * enemy (s = 1) not marked (+0x95) reads the clock (site 0x43ed34) and is
 * skipped in the off half of the blink (bit 0x20) or without a seen
 * position (+0x1aa, none when byte +0xa7 is set); else its position +0x26.
 * Relative to P, d = dist_octagonal(dx, dy) (hook; 0 -> 1); sincos_pair of
 * -word [P+0x10] (hook; the upper half of the slot is P's, as pushed);
 * beyond the range: skipped; rotate_2d (hook): the blip (x, -y, colour by
 * type word +0x1a8 and side, words 0x4d3b88..0x4d3ba0) to the next 12
 * bytes of R+0x68; the target's: R+0x51c <- it, R+0x520 <- (d / 1000) *
 * 6; count + 1. */
static int blip_scan(hud_ctx *c, uint32_t r) {
    hw32(c, r + 0x518u, 0);
    hw32(c, r + 0x51cu, 0);
    uint32_t edi = r + 0x68u;
    if (HCALL0(c, 0x0045edf8u, NULL)) return 1;
    hw32(c, r + 0x30u, hr32(c, hr32(c, r + 8u) + 0xcu));
    uint32_t panel = hr32(c, VA_PANEL), target = hr32(c, panel + 0x210u), p = hr32(c, panel + 0x203u);
    uint32_t px = hr32(c, p + 0x26u), py = hr32(c, p + 0x2au);
    uint32_t key = hr8(c, p + 0x92u + 4u) ? 0u : 1u;
    if (key != hr32(c, r + 0x34u)) {
        uint32_t n = key ? 0xbu : 0xcu;
        if (HCALL(c, 0x00438d9cu, NULL, hr32(c, hr32(c, VA_PANEL) + 0x1edu), n, 1u)) return 1;
    }
    hw32(c, r + 0x34u, key);
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0x20u);
    if (!f) return hud_refuse(c, 0x0043ec08u, "blip scan", "the own code's call area is exhausted");
    /* f: +0 pos (x, y), +8 sincos (2 dwords), +0x10 rotated (x, y) */
    uint32_t list = hr32(c, 0x004a9af8u);
    int bad = 0;
    for (uint16_t i = 0;; ) {
        uint16_t was = i++;
        if ((int16_t)was >= (int16_t)hr16(c, 0x004a9afeu)) break;
        uint32_t v = hr32(c, list);
        list += 4u;
        if (!v) break;
        int32_t side = (int32_t)(int8_t)hr8(c, hr32(c, v + 0x45u) + 0x12u);
        if (v == p) continue;
        if (hr8(c, v + 0x92u + 0x12u) || hr8(c, v + 0x92u + 7u)) continue;
        if (hr32(c, hr32(c, v + 0x45u) + 0x14u)) continue;
        if (hr16(c, v + 0x1a8u) == 1u && hr16(c, hr32(c, v + 0x1f2u) + 0x1eu)) continue;
        uint32_t pos;
        if (!hr8(c, v + 0x92u + 3u) && (int16_t)side == 1) {
            uint32_t now = 0;
            if ((bad = hud_clock(c, 0x0043ed34u, &now)) != 0) break;
            if (now & 0x20u) continue;
            pos = hr8(c, v + 0x92u + 0x15u) ? 0u : v + 0x1aau;
            if (!pos) continue;
        } else {
            pos = v + 0x26u;
        }
        hw32(c, f, hr32(c, pos) - px);
        hw32(c, f + 4u, hr32(c, pos + 4u) - py);
        uint32_t d = 0;
        if ((bad = HCALL(c, 0x0047de4cu, &d, hr32(c, f), hr32(c, f + 4u))) != 0) break;
        if (!d) d = 1;
        uint32_t angle = (p & 0xffff0000u) | (uint16_t)(0u - hr16(c, p + 0x10u));
        if ((bad = HCALL(c, 0x0047eb30u, NULL, angle, f + 8u)) != 0) break;
        if ((int32_t)d >= (int32_t)hr32(c, r + 0x30u)) continue;
        if ((bad = HCALL(c, 0x0047ffe8u, NULL, f, f + 8u, f + 0x10u)) != 0) break;
        hw32(c, f, hr32(c, f + 0x10u));
        hw32(c, f + 4u, 0u - hr32(c, f + 0x14u));
        int32_t colour;
        int enemy = (int16_t)side != 0;
        switch ((int16_t)hr16(c, v + 0x1a8u)) {
        case 0: colour = sx16(c, enemy ? 0x004d3b8eu : 0x004d3b9cu); break;
        case 1: case 3: colour = sx16(c, enemy ? 0x004d3b94u : 0x004d3b88u); break;
        case 2: colour = sx16(c, enemy ? 0x004d3b9au : 0x004d3b9eu); break;
        default: colour = sx16(c, 0x004d3ba0u); break;
        }
        hw32(c, edi, hr32(c, f));
        hw32(c, edi + 4u, hr32(c, f + 4u));
        hw32(c, edi + 8u, (uint32_t)colour);
        if (v == target) {
            hw32(c, r + 0x51cu, edi);
            hw32(c, r + 0x520u, (uint32_t)(((int32_t)d / 1000) * 6));
        }
        edi += 0xcu;
        hw32(c, r + 0x518u, hr32(c, r + 0x518u) + 1u);
    }
    hud_frame_pop(c, top);
    return bad;
}
void es2_sim_service_blip_scan(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (blip_scan(&c, sc->slots[0])) hud_failed(sc);
}
/* 0x43f2d8(R, pt), the HUD radar: own code in src/panel.c. */
static int idiv_ok(int32_t a, int32_t b) { return b != 0 && !(a == INT32_MIN && b == -1); }
static int hud_radar(hud_ctx *c, uint32_t r, uint32_t pt) { return es2_sim_panel_hud_radar(c, r, pt); }
/* 0x43dd98(H) (listing 0x43dd98..0x43e110): the altitude ladder, for a flyer
 * chassis only. The ladder
 * with its blits, lines and fill is es2_sim_hud_altitude_ladder in
 * src/hud_frame.c (hud.md, "the HUD's slot 2 draw, the gadgets' slot 1 thunks
 * and the altitude ladder with pixels"); 0x43d5f0 and this row delegate. */
static int altitude_ladder(hud_ctx *c, uint32_t h) {
    es2_sim_result r;
    return es2_sim_hud_altitude_ladder(c, h, h, &r);
}
/* 0x43d5f0(H) (listing 0x43d5f0..0x43d701), the HUD's full draw: unless
 * blinking off (+0x76), with [H+0xd7] (rectangle +4) visible: the canvas
 * pushed; slot 0 of the [H+0xff] gadgets [H+0xd7..]; when the throttle
 * gauge [[0x49b088]+0x1e1]'s byte +0xb3 is set, the reverse bitmap
 * [[[0x49be78]+8]+0x38] at [H+0x103]+4 (0x4816a8) and the label [H+0x103]
 * with [0x4d1650]; the mission time (0x43dcd4, clock), the speed text
 * (0x43dca0); the labels [0x4d17cc] [0x4d1658], [0x4d17d0] 0x4d17e4,
 * [0x4d17d4] [0x4d165c], [0x4d17d8] 0x4d17ee; the radar (0x43e114 ->
 * 0x43f2d8([[[0x49b088]+0x1ed]+0xd9], H+0x10b)); the altitude ladder
 * (0x43dd98); the canvas popped. */
static int hud_full(hud_ctx *c, uint32_t h) {
    if (hr8(c, h + 0x76u)) return 0;
    int yes;
    if (visible(c, hr32(c, h + 0xd7u) + 4u, &yes)) return 1;
    if (!yes) return 0;
    if (canvas_push(c, NULL)) return 1;
    for (uint32_t i = 0; (int32_t)i < (int32_t)hr32(c, h + 0xffu); ++i) if (odraw(c, hr32(c, h + 0xd7u + 4u * i))) return 1;
    if (hr8(c, hr32(c, hr32(c, VA_PANEL) + 0x1e1u) + 0xb3u)) {
        if (HCALL(c, VA_BLIT, NULL, hr32(c, hr32(c, hr32(c, 0x0049be78u) + 8u) + 0x38u), hr32(c, h + 0x103u) + 4u, 0u)) return 1;
        if (label_draw(c, hr32(c, h + 0x103u), hr32(c, 0x004d1650u))) return 1;
    }
    if (mission_time(c) || speed_text(c)) return 1;
    if (label_draw(c, hr32(c, 0x004d17ccu), hr32(c, 0x004d1658u))) return 1;
    if (label_draw(c, hr32(c, 0x004d17d0u), 0x004d17e4u)) return 1;
    if (label_draw(c, hr32(c, 0x004d17d4u), hr32(c, 0x004d165cu))) return 1;
    if (label_draw(c, hr32(c, 0x004d17d8u), 0x004d17eeu)) return 1;
    if (hud_radar(c, hr32(c, hr32(c, hr32(c, VA_PANEL) + 0x1edu) + 0xd9u), h + 0x10bu)) return 1;
    if (altitude_ladder(c, h)) return 1;
    return canvas_pop(c);
}

/* ---- the HUD gadgets' draws (slot 0 of [H+0xd7..]) */

/* 0x43b808(D), the fixed gunsight, and 0x43b978(D), the locking gunsight: own
 * code in src/gadgets.c. */
static int gunsight_fixed(hud_ctx *c, uint32_t d) { return es2_sim_gadget_gunsight_fixed(c, d); }
static int gunsight_target(hud_ctx *c, uint32_t d) { return es2_sim_gadget_gunsight_target(c, d); }
/* 0x43b704(D) (listing 0x43b704..0x43b7cb), the reverse rotation gauge:
 * the canvas pushed, its rectangle and origin moved to D's (restored after,
 * no other effect), the scale [D+0x48] at ([D+0x44], [D+8]) and the needle
 * [D+0x4c] at ([D+0x44] + [D+0x28], [D+8]) through 0x45c8bc, the canvas
 * popped. */
static int rotation_reverse(hud_ctx *c, uint32_t d) {
    uint32_t canvas = 0;
    if (canvas_push(c, &canvas)) return 1;
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 8u);
    if (!f) return hud_refuse(c, 0x0043b704u, "rotation gauge", "the own code's call area is exhausted");
    uint32_t s210 = hr32(c, canvas + 0x210u), s218 = hr32(c, canvas + 0x218u), s220 = hr32(c, canvas + 0x220u), s224 = hr32(c, canvas + 0x224u);
    hw32(c, canvas + 0x210u, hr32(c, d + 4u) - s220);
    hw32(c, canvas + 0x218u, hr32(c, d + 0xcu) - s220);
    hw32(c, canvas + 0x220u, hr32(c, d + 4u) - s220);
    hw32(c, f, hr32(c, d + 0x44u));
    hw32(c, f + 4u, hr32(c, d + 8u));
    int bad = blit_at(c, VA_DESC, hr32(c, d + 0x48u), f);
    if (!bad) {
        add32(c, f, hr32(c, d + 0x28u));
        bad = blit_at(c, VA_DESC, hr32(c, d + 0x4cu), f);
    }
    hw32(c, canvas + 0x210u, s210);
    hw32(c, canvas + 0x218u, s218);
    hw32(c, canvas + 0x220u, s220);
    hw32(c, canvas + 0x224u, s224);
    hud_frame_pop(c, top);
    return bad || canvas_pop(c);
}
/* 0x43b4cc(D), the fixed rotation gauge: own code in src/gadgets.c. */
static int rotation_fixed(hud_ctx *c, uint32_t d) { return es2_sim_gadget_rotation_fixed(c, d); }

/* ---- widget draws (slot 0 of [W+0x17]) */

/* 0x434748(B) (listing 0x434748..0x43490e), the system button: the canvas
 * pushed; state word B+0x1b: 0 and changed from B+0x5c: the save-under
 * rectangle B+0x48..+0x54 <- B's rectangle less the canvas [0x4d03a8]
 * rectangle +0x210 plus its origin +0x220 (both corners), the background
 * saved (0x488012(B+0x44, [B+0x58])) and the rectangle shifted back; 0 in
 * any case: the face bitmap [B+0x30] at B (0x4816a8, flag 0); 3 and
 * changed: the save-under
 * rectangle <- B's rectangle and the background restored
 * (0x488143(B+0x44, B, [B+0x58])); B+0x5c <- the state; the canvas popped. */
static int system_button_draw(hud_ctx *c, uint32_t b) {
    if (canvas_push(c, NULL)) return 1;
    int32_t st = sx16(c, b + 0x1bu);
    uint32_t cv = hr32(c, 0x004d03a8u);
    if (st == 0) {
        if ((uint32_t)st != hr32(c, b + 0x5cu)) {
            for (uint32_t k = 0; k < 4u; ++k) hw32(c, b + 0x48u + 4u * k, hr32(c, b + 4u * k));
            for (uint32_t k = 0; k < 2u; ++k) {
                add32(c, b + 0x48u + 4u * k, 0u - hr32(c, cv + 0x210u + 4u * k));
                add32(c, b + 0x50u + 4u * k, 0u - hr32(c, cv + 0x210u + 4u * k));
            }
            for (uint32_t k = 0; k < 2u; ++k) {
                add32(c, b + 0x48u + 4u * k, hr32(c, cv + 0x220u + 4u * k));
                add32(c, b + 0x50u + 4u * k, hr32(c, cv + 0x220u + 4u * k));
            }
            if (HCALL(c, VA_SAVE, NULL, b + 0x44u, hr32(c, b + 0x58u))) return 1;
            for (uint32_t k = 0; k < 2u; ++k) {
                add32(c, b + 0x48u + 4u * k, hr32(c, cv + 0x210u + 4u * k));
                add32(c, b + 0x50u + 4u * k, hr32(c, cv + 0x210u + 4u * k));
            }
            for (uint32_t k = 0; k < 2u; ++k) {
                add32(c, b + 0x48u + 4u * k, 0u - hr32(c, cv + 0x220u + 4u * k));
                add32(c, b + 0x50u + 4u * k, 0u - hr32(c, cv + 0x220u + 4u * k));
            }
        }
        if (HCALL(c, VA_BLIT, NULL, hr32(c, b + 0x30u), b, 0u)) return 1;
    } else if (st == 3 && (uint32_t)st != hr32(c, b + 0x5cu)) {
        for (uint32_t k = 0; k < 4u; ++k) hw32(c, b + 0x48u + 4u * k, hr32(c, b + 4u * k));
        if (HCALL(c, VA_RESTORE, NULL, b + 0x44u, b, hr32(c, b + 0x58u))) return 1;
    }
    hw32(c, b + 0x5cu, (uint32_t)st);
    return canvas_pop(c);
}
/* 0x442cb0(B) (listing 0x442cb0..0x442db9), the range toggle: with B
 * visible and not disabled (state 2): by B+0x28 (0 the range: bitmap
 * [[0x49c4d0]+8] slot state + 2, text [0x49c71c + 4 word [[B+0x24]+0xb1]];
 * 1, 2 the modes: text [0x4d134c + 4 B+0x28], bitmap by state or (2) by
 * byte B+0x40) the bitmap through 0x45c8bc and the label [B+0x2c] with
 * colour [0x49b0ac + 4 (on ? 12 : 10)]; other modes draw nothing. */
static int range_toggle_draw(hud_ctx *c, uint32_t b) {
    int yes;
    if (visible(c, b, &yes)) return 1;
    if (!yes || hr16(c, b + 0x1bu) == 2u) return 0;
    uint32_t bmp = 0, text = 0, colour = 0, t = hr32(c, hr32(c, 0x0049c4d0u) + 8u);
    uint32_t mode = hr32(c, b + 0x28u);
    if (mode <= 1u) {
        int16_t st = (int16_t)hr16(c, b + 0x1bu);
        bmp = hr32(c, t + 4u * (uint32_t)(int32_t)st + 8u);
        text = mode == 0u ? 0x0049c71cu + 4u * (uint32_t)sx16(c, hr32(c, b + 0x24u) + 0xb1u) : hr32(c, 0x004d134cu + 4u * mode);
        colour = hr32(c, VA_COLOURS + 4u * (st ? 12u : 10u));
    } else if (mode == 2u) {
        uint8_t on = hr8(c, b + 0x40u);
        bmp = hr32(c, t + 4u * on + 8u);
        text = hr32(c, 0x004d134cu + 4u * mode);
        colour = hr32(c, VA_COLOURS + 4u * (on ? 12u : 10u));
    }  /* other modes: nothing (the registers start at 0) */
    if (bmp && blit_at(c, VA_DESC, bmp, b)) return 1;
    if (text) {
        hw32(c, hr32(c, b + 0x2cu), colour);
        if (label_draw(c, hr32(c, b + 0x2cu), text)) return 1;
    }
    return 0;
}
/* 0x4423bc(B) (listing 0x4423bc..0x442448), the weapon button: with B
 * visible, its gauge G = [B+0x24] not busy and powered: the face
 * [[[0x49c4d4]+8]] through 0x45c8bc; the label [B+0x40] colour [0x49b0ac
 * + 4 (state 0 ? 11 : 10)] with the text B+0x44. */
static int weapon_button_draw(hud_ctx *c, uint32_t b) {
    int yes;
    if (visible(c, b, &yes)) return 1;
    if (!yes) return 0;
    uint32_t g = hr32(c, b + 0x24u);
    if (busy(c, g) || !hr8(c, hr32(c, b + 0x24u) + 0x8cu)) return 0;
    if (blit_at(c, VA_DESC, hr32(c, hr32(c, hr32(c, 0x0049c4d4u) + 8u)), b)) return 1;
    uint32_t l = hr32(c, b + 0x40u);
    hw32(c, l, hr32(c, VA_COLOURS + 4u * (hr16(c, b + 0x1bu) ? 10u : 11u)));
    if (label_draw(c, l, b + 0x44u)) return 1;
    return 0;
}

/* 0x434228(v) (listing 0x434228..0x43423c): (v idiv 1000) * 6, the radar
 * range as shown. */
static uint32_t radar_units(uint32_t v) { return (uint32_t)((int32_t)v / 1000) * 6u; }
/* 0x43eef4(R), MFDRadar slot 0, the radar draw: own code in src/panel.c. */
static int radar_draw(hud_ctx *c, uint32_t r) { return es2_sim_panel_radar_draw(c, r); }

/* ---- service rows */
#define DRAW_ROW(name, fn) \
    void es2_sim_service_##name(es2_sim_service_call *sc, void *user) { \
        (void)user; HSERVE(sc, c); if (fn(&c, sc->slots[0])) hud_failed(sc); }
DRAW_ROW(projectile_draw, projectile_draw)
DRAW_ROW(range_draw, range_draw)
DRAW_ROW(pool_draw, pool_draw)
DRAW_ROW(throttle_draw, throttle_draw)
DRAW_ROW(mfd_draw, mfd_draw)
DRAW_ROW(hdd_draw, hdd_draw)
DRAW_ROW(hdd_page_draw, hdd_page_draw)
DRAW_ROW(hud_full, hud_full)
DRAW_ROW(gunsight_fixed, gunsight_fixed)
DRAW_ROW(gunsight_target, gunsight_target)
DRAW_ROW(rotation_reverse, rotation_reverse)
DRAW_ROW(system_button_draw, system_button_draw)
DRAW_ROW(range_toggle_draw, range_toggle_draw)
DRAW_ROW(weapon_button_draw, weapon_button_draw)
void es2_sim_service_rotation_fixed(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (rotation_fixed(&c, sc->slots[0])) hud_failed(sc);
}
void es2_sim_service_mission_time(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (mission_time(&c)) hud_failed(sc);
}
void es2_sim_service_speed_text(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (speed_text(&c)) hud_failed(sc);
}
void es2_sim_service_hud_radar(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (hud_radar(&c, sc->slots[0], sc->slots[1])) hud_failed(sc);
}
/* 0x43e114(H): 0x43f2d8([[[0x49b088]+0x1ed]+0xd9], H+0x10b). */
void es2_sim_service_hud_radar_forward(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (hud_radar(&c, hr32(&c, hr32(&c, hr32(&c, VA_PANEL) + 0x1edu) + 0xd9u), sc->slots[0] + 0x10bu)) hud_failed(sc);
}
void es2_sim_service_altitude_ladder(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    es2_sim_result r;
    memset(&r, 0, sizeof(r));
    if (es2_sim_hud_altitude_ladder(&c, sc->slots[0], sc->ecx, &r)) { hud_failed(sc); return; }
    sc->eax = r.eax;
    sc->edx = r.edx;
    sc->ecx = r.ecx;
}
/* 0x445234(): [[0x49b088]+0x1ed] (through 0x447919). */
void es2_sim_service_mfd_object(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    sc->eax = hr32(&c, hr32(&c, VA_PANEL) + 0x1edu);
}
void es2_sim_service_order_symbol(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    sc->eax = order_symbol(&c, sc->slots[0]);
}
/* The row of the nav gauge 0x43c40c and the roving gunsight 0x43c204: both
 * are own code in src/gadgets.c. The shield palette 0x443918 is
 * src/display.c's row. No other VA reaches it. */
void es2_sim_service_hud_draw_headless(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    int bad;
    if (sc->va == 0x0043c40cu) bad = es2_sim_gadget_nav_gauge(&c, sc->slots[0]);
    else if (sc->va == 0x0043c204u) bad = es2_sim_gadget_roving_gunsight(&c, sc->slots[0]);
    else bad = hud_refuse(&c, sc->va, "HUD draw", "no own draw for this address");
    if (bad) hud_failed(sc);
}

/* 0x4480c8(W) (listing 0x4480c8..0x4481c1) and 0x448660(W) (listing
 * 0x448660..0x448737), the throttle sliders' draws (vertical,
 * horizontal): with W visible, the knob rectangle from the value word
 * W+0x28 and the knob size word W+0x2c (vertical: W+0x3a <- v, W+0x32 <- v
 * - size; horizontal: W+0x2e <- v, W+0x36 <- v + size); the view's page
 * +0x38 <- +0x3c and, for the current view, [0x4a3686] <- it and the
 * driver slot [0x4a57ec]; the save-under rectangle W+0x5e..+0x6c <- the
 * knob rectangle W+0x2e..+0x3c plus the GC origin (both corners); the
 * background saved (0x488012(W+0x5a, [W+0x52])) and the knob [W+0x4e]
 * drawn at W+0x2e (0x4816a8; flag 2 for a vertical knob right of x 0xa0
 * scaled by 0x4d253a, else 0);
 * W+0x6e/+0x72 <- W+0x2e/+0x32 (the last knob position). */
static int slider_draw(hud_ctx *c, uint32_t w, int vertical) {
    int yes;
    if (visible(c, w, &yes)) return 1;
    if (!yes) return 0;
    uint32_t v = hr32(c, VA_VIEW), gc = hr32(c, VA_GC);
    int32_t val = sx16(c, w + 0x28u), size = sx16(c, w + 0x2cu);
    if (vertical) {
        hw32(c, w + 0x3au, (uint32_t)val);
        hw32(c, w + 0x32u, (uint32_t)(val - size));
    } else {
        hw32(c, w + 0x2eu, (uint32_t)val);
        hw32(c, w + 0x36u, (uint32_t)(val + size));
    }
    uint32_t page = hr32(c, v + 0x3cu);
    hw32(c, v + 0x38u, page);
    if (v == hr32(c, VA_VIEW)) {
        hw32(c, 0x004a3686u, hr32(c, v + 0x38u));
        if (hud_call(c, hr32(c, 0x004a57ecu), (const uint32_t[]){page}, 1, NULL)) return 1;
    }
    for (uint32_t k = 0; k < 4u; ++k) hw32(c, w + 0x5eu + 4u * k, hr32(c, w + 0x2eu + 4u * k));
    for (uint32_t k = 0; k < 2u; ++k) {
        add32(c, w + 0x5eu + 4u * k, hr32(c, gc + 0x220u + 4u * k));
        add32(c, w + 0x66u + 4u * k, hr32(c, gc + 0x220u + 4u * k));
    }
    if (HCALL(c, VA_SAVE, NULL, w + 0x5au, hr32(c, w + 0x52u))) return 1;
    /* the vertical knob's blit flag: 2 right of x 0xa0 (scaled), else 0 */
    uint32_t flip = vertical && (int32_t)(0xa0u << shx(c)) < (int32_t)hr32(c, w + 0x2eu) ? 2u : 0u;
    if (HCALL(c, VA_BLIT, NULL, hr32(c, w + 0x4eu), w + 0x2eu, flip)) return 1;
    hw32(c, w + 0x6eu, hr32(c, w + 0x2eu));
    hw32(c, w + 0x72u, hr32(c, w + 0x32u));
    return 0;
}
void es2_sim_service_slider_draw_v(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (slider_draw(&c, sc->slots[0], 1)) hud_failed(sc);
}
void es2_sim_service_slider_draw_h(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (slider_draw(&c, sc->slots[0], 0)) hud_failed(sc);
}

/* 0x442b60(W) (listing 0x442b60..0x442c0d), the power slider's draw: with
 * W visible, not disabled, its gauge [W+0x42] not busy and powered:
 * GC+0x22c <- the local record (0, its unwritten second dword: 0 here); the
 * bar [W+0x46] slot 0 with the word W+0x7a;
 * the knob W+0x2e <- word W+0x28, W+0x36 <- it + word W+0x2c; W+0x6e/+0x72
 * <- W+0x2e/+0x32; W+0x4a <- W+0x7a. */
static int power_slider_draw(hud_ctx *c, uint32_t w) {
    int yes;
    if (visible(c, w, &yes)) return 1;
    if (!yes || hr16(c, w + 0x1bu) == 2u) return 0;
    uint32_t g = hr32(c, w + 0x42u);
    if (busy(c, g) || !hr8(c, hr32(c, w + 0x42u) + 0x8cu)) return 0;
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 8u);
    if (!f) return hud_refuse(c, 0x00442b60u, "power slider draw", "the own code's call area is exhausted");
    hw32(c, hr32(c, VA_GC) + 0x22cu, f);
    uint32_t bar = hr32(c, w + 0x46u);
    int bad = hud_call(c, hr32(c, hr32(c, bar)), (const uint32_t[]){bar, hr16(c, w + 0x7au)}, 2, NULL);
    hud_frame_pop(c, top);
    if (bad) return 1;
    hw32(c, w + 0x2eu, (uint32_t)sx16(c, w + 0x28u));
    hw32(c, w + 0x36u, (uint32_t)(sx16(c, w + 0x28u) + sx16(c, w + 0x2cu)));
    hw32(c, w + 0x6eu, hr32(c, w + 0x2eu));
    hw32(c, w + 0x72u, hr32(c, w + 0x32u));
    hw32(c, w + 0x4au, hr32(c, w + 0x7au));
    return 0;
}
/* 0x4426e8(W) (listing 0x4426e8..0x4428f8), the weapon toggle's draw: with
 * W visible and its gauge G = [W+0x24] powered. Unless G is busy: the
 * weapon button (0x4423bc); then, G jammed (byte +0xd3): the label [W+0x2c]
 * placed by 0x438884 over W+0x56 (W+0x56 <- [G+4] when byte W+0x66, else
 * W+0x5e <- [G+0xc]; flags 2), its fill +0x1d <- 0x2e, text [0x4d1344],
 * colour [0x49b0d8], the jam bitmap [[0x49c4d0]+8]+0x18; else the name
 * G+0xb1 with colour [0x49b0d4] (selected, byte +0xd6) or [0x49b0d8] and,
 * when selected or linked (+0xd7), the bitmap +0x10 (+0xd8) or +0x14; the
 * label [W+0x40] colour [0x49b0ac + 4 (selected ? 13 : 11)] with W+0x44;
 * the bitmap at W+0x46 through 0x45c8bc, without one the rectangle filled
 * (GC+0x22c <- the local record (0, 0x2e), 0x486704(W+0x46)). In any
 * case the frame
 * bitmap (selected: +0, else +4 of [[0x49c4d0]+8]) at W's corner less one
 * pixel unit. */
static int weapon_toggle_draw(hud_ctx *c, uint32_t w) {
    int yes;
    if (visible(c, w, &yes)) return 1;
    if (!yes) return 0;
    uint32_t g = hr32(c, w + 0x24u);
    if (!hr8(c, g + 0x8cu)) return 0;
    uint8_t jam = hr8(c, g + 0xd3u), sel = hr8(c, g + 0xd6u), link = hr8(c, g + 0xd7u), alt = hr8(c, g + 0xd8u);
    uint32_t t = hr32(c, hr32(c, 0x0049c4d0u) + 8u);
    if (!busy(c, g)) {
        if (weapon_button_draw(c, w)) return 1;
        uint32_t bmp, colour, text;
        if (jam) {
            colour = hr32(c, 0x0049b0d8u);
            bmp = hr32(c, t + 0x18u);
            uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 8u);
            if (!f) return hud_refuse(c, 0x004426e8u, "weapon toggle draw", "the own code's call area is exhausted");
            if (hr8(c, w + 0x66u)) hw32(c, w + 0x56u, hr32(c, hr32(c, w + 0x24u) + 4u));
            else hw32(c, w + 0x5eu, hr32(c, hr32(c, w + 0x24u) + 0xcu));
            int bad = HCALL(c, 0x00438884u, NULL, hr32(c, w + 0x2cu), f, w + 0x56u, 2u);
            hud_frame_pop(c, top);
            if (bad) return 1;
            hw32(c, hr32(c, w + 0x2cu) + 0x1du, 0x2eu);
            text = hr32(c, 0x004d1344u);
        } else {
            bmp = (link || sel) ? hr32(c, t + (alt ? 0x10u : 0x14u)) : 0u;
            colour = hr32(c, sel ? 0x0049b0d4u : 0x0049b0d8u);
            text = hr32(c, w + 0x24u) + 0xb1u;
        }
        hw32(c, hr32(c, w + 0x2cu), colour);
        if (label_draw(c, hr32(c, w + 0x2cu), text)) return 1;
        hw32(c, hr32(c, w + 0x40u), hr32(c, VA_COLOURS + 4u * (sel ? 13u : 11u)));
        if (label_draw(c, hr32(c, w + 0x40u), w + 0x44u)) return 1;
        if (bmp) {
            if (blit_at(c, VA_DESC, bmp, w + 0x46u)) return 1;
        } else {
            uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 8u);
            if (!f) return hud_refuse(c, 0x004426e8u, "weapon toggle draw", "the own code's call area is exhausted");
            hw32(c, f, 0);
            hw32(c, f + 4u, 0x2eu);
            hw32(c, hr32(c, VA_GC) + 0x22cu, f);
            int bad = HCALL(c, VA_FILL, NULL, w + 0x46u);
            hud_frame_pop(c, top);
            if (bad) return 1;
        }
    }
    uint32_t frame = hr32(c, t + (sel ? 0u : 4u));
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 8u);
    if (!f) return hud_refuse(c, 0x004426e8u, "weapon toggle draw", "the own code's call area is exhausted");
    uint32_t one = 1u << shx(c);
    hw32(c, f, hr32(c, w) - one);
    hw32(c, f + 4u, hr32(c, w + 4u) - one);
    int bad = blit_at(c, VA_DESC, frame, f);
    hud_frame_pop(c, top);
    return bad;
}
DRAW_ROW(power_slider_draw, power_slider_draw)
DRAW_ROW(weapon_toggle_draw, weapon_toggle_draw)

/* The bar draws (slot 0 of [B], with (B, value)) 0x4395e8, 0x439398 and
 * 0x439118, and the shields gauge's slot +4 0x443758: own code in src/panel.c
 * (cockpit.md, "bar and gauge family, power-on sweeps, balance numbers"); the
 * rows delegate. */
#define PANEL_ROW(name, expr) \
    void es2_sim_service_##name(es2_sim_service_call *sc, void *user) { \
        (void)user; HSERVE(sc, c); uint32_t eax = sc->eax; \
        if (expr) { hud_failed(sc); return; } sc->eax = eax; }
PANEL_ROW(bar_draw_led, es2_sim_panel_bar_draw(&c, sc->slots[0], sc->slots[1], 2u, &eax))
PANEL_ROW(bar_draw_neg, es2_sim_panel_bar_draw(&c, sc->slots[0], sc->slots[1], 1u, &eax))
PANEL_ROW(bar_draw_pos, es2_sim_panel_bar_draw(&c, sc->slots[0], sc->slots[1], 0u, &eax))
PANEL_ROW(shields_draw, es2_sim_panel_shields_draw(&c, sc->slots[0], &eax))

/* 0x44dd74(M, i, b, flag) (listing 0x44dd74..0x44ddec): the map's marker
 * i: bitmap [[0x49d23c]+8] index (flag ? 4 : b + 2) at the corner of the
 * map icon [M + 0x3d + 4i] + 4 (less 0 shifted, as the original computes
 * it) through 0x45c8bc. */
static int map_marker(hud_ctx *c, uint32_t m, uint32_t i, uint32_t b, uint32_t flag) {
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 8u);
    if (!f) return hud_refuse(c, 0x0044dd74u, "map marker", "the own code's call area is exhausted");
    uint32_t icon = hr32(c, m + 0x3du + 4u * i) + 4u;
    hw32(c, f, hr32(c, icon));
    hw32(c, f + 4u, hr32(c, icon + 4u));
    uint32_t idx = (flag & 0xffu) ? 4u : (b & 0xffu) + 2u;
    int bad = blit_at(c, VA_DESC, hr32(c, hr32(c, hr32(c, 0x0049d23cu) + 8u) + 4u * idx), f);
    hud_frame_pop(c, top);
    return bad;
}
/* 0x44ddf0(M, b) (listing 0x44ddf0..0x44de12): the selected map icon
 * [M+0x5d] (-1: none) marked: 0x44dd74(M, [M+0x5d] - 10, b, 0). */
static int map_selected(hud_ctx *c, uint32_t m, uint32_t b) {
    uint32_t s = hr32(c, m + 0x5du);
    if (s == 0xffffffffu) return 0;
    return map_marker(c, m, s - 10u, b, 0u);
}
/* 0x44bb60(B) (listing 0x44bb60..0x44be17), the HDD button's draw: state
 * word B+0x1b below 2 (signed); by the kind B+0x28 (index bytes 0x44bb9f,
 * jump table 0x44bbae): 0, 1 (wingman keys): the face [B + 0x30 + 4 byte
 * B+0x40] through 0x45c8bc and the label [B+0x2c] colour [0x49b0ac + 4
 * (B+0x40 ? 12 : 10)] with "F" + ('7' + kind) (the bytes of 0x49d4f5 and
 * the kind); 2..7: the face of the state; 13: the face, the label colour
 * [0x49b0bc + 4 state] text [0x4d13b0], on the map page ([[B+0x24]+0x109]
 * = 0) the selected icon marked (0x44ddf0([H+0x101], the state byte));
 * 14: the face, colour as 13, text [0x4d13b4]; 10..12 by word [B+0x24]+0x529 (H
 * = [B+0x24]): 0: GC+0x22c <- the local record (4, word 0x4d3b94 when byte
 * B+0x40, else word 0x4d3ba2) and B's rectangle less the 640x480 offsets
 * filled; 1: GC+0x22c <- the record (0, word 0x4d3b9a when byte B+0x40, else
 * 0x4d3b96) and the radio record n = [H+0x517] (byte B+0x40) or [H+0x52b]'s
 * rectangle H + 0x15a + 0x14e n less the offsets filled unless n = -1 (listing
 * 0x44bcde..0x44be12); other words, 8, 9 and above 14: nothing. */
static int hdd_button_draw(hud_ctx *c, uint32_t b) {
    int32_t st = sx16(c, b + 0x1bu);
    if (st >= 2) return 0;
    uint32_t kind = hr32(c, b + 0x28u);
    if (kind > 0xeu) return 0;
    switch (hr8(c, 0x0044bb9fu + kind)) {
    case 5: {
        uint8_t on = hr8(c, b + 0x40u);
        if (blit_at(c, VA_DESC, hr32(c, b + 0x30u + 4u * on), b)) return 1;
        uint32_t colour = hr32(c, VA_COLOURS + 4u * (hr8(c, b + 0x40u) ? 12u : 10u));
        uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 4u);
        if (!f) return hud_refuse(c, 0x0044bb60u, "HDD button draw", "the own code's call area is exhausted");
        hw16(c, f, hr16(c, 0x0049d4f5u));
        hw8(c, f + 2u, hr8(c, 0x0049d4f7u));
        hw8(c, f + 1u, (uint8_t)(hr8(c, b + 0x28u) + 0x37u));
        hw32(c, hr32(c, b + 0x2cu), colour);
        int bad = label_draw(c, hr32(c, b + 0x2cu), f);
        hud_frame_pop(c, top);
        return bad;
    }
    case 4:
        return blit_at(c, VA_DESC, hr32(c, b + 0x30u + 4u * (uint32_t)st), b);
    case 2: {
        if (blit_at(c, VA_DESC, hr32(c, b + 0x30u + 4u * (uint32_t)st), b)) return 1;
        hw32(c, hr32(c, b + 0x2cu), hr32(c, 0x0049b0bcu + 4u * (uint32_t)sx16(c, b + 0x1bu)));
        if (label_draw(c, hr32(c, b + 0x2cu), hr32(c, 0x004d13b0u))) return 1;
        uint32_t h = hr32(c, b + 0x24u);
        if (hr32(c, h + 0x109u)) return 0;
        return map_selected(c, hr32(c, h + 0x101u), hr8(c, b + 0x1bu));
    }
    case 1:
        if (blit_at(c, VA_DESC, hr32(c, b + 0x30u + 4u * (uint32_t)st), b)) return 1;
        hw32(c, hr32(c, b + 0x2cu), hr32(c, 0x0049b0bcu + 4u * (uint32_t)sx16(c, b + 0x1bu)));
        if (label_draw(c, hr32(c, b + 0x2cu), hr32(c, 0x004d13b4u))) return 1;
        return 0;
    case 3: {
        /* 0x44bbc6..: locals ebp-0x10 (4, colour), ebp-8 (0, colour), the
         * rectangle at ebp-0x30; the GC taken at the entry */
        uint32_t gc = hr32(c, VA_GC);
        int16_t w = (int16_t)hr16(c, hr32(c, b + 0x24u) + 0x529u);
        if (w != 0 && w != 1) return 0;
        uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0x30u);
        if (!f) return hud_refuse(c, 0x0044bb60u, "HDD button draw", "the own code's call area is exhausted");
        const uint32_t frame = f + 0x20u, plain = f + 0x28u;
        hw32(c, frame, 4u);
        hw32(c, plain, 0);
        uint32_t src = 0;
        if (w == 0) {
            hw32(c, gc + 0x22cu, frame);
            hw32(c, frame + 4u, (uint32_t)sx16(c, hr8(c, b + 0x40u) ? 0x004d3b94u : 0x004d3ba2u));
            src = b;
        } else {
            uint32_t h = hr32(c, b + 0x24u), n;
            hw32(c, gc + 0x22cu, plain);
            if (hr8(c, b + 0x40u)) {
                hw32(c, plain + 4u, (uint32_t)sx16(c, 0x004d3b9au));
                n = hr32(c, h + 0x517u);
            } else {
                hw32(c, plain + 4u, (uint32_t)sx16(c, 0x004d3b96u));
                n = hr32(c, h + 0x52bu);
            }
            if (n != 0xffffffffu) src = h + 0x15au + 0x14eu * n;
        }
        int bad = 0;
        if (src) {
            for (uint32_t j = 0; j < 4u; ++j) hw32(c, f + 4u * j, hr32(c, src + 4u * j) - hr32(c, (j & 1u) ? VA_ORIGIN_Y : VA_ORIGIN_X));
            bad = HCALL(c, VA_FILL, NULL, f);
        }
        hud_frame_pop(c, top);
        return bad;
    }
    default:
        return 0;
    }
}
DRAW_ROW(hdd_button_draw, hdd_button_draw)
void es2_sim_service_map_marker(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (map_marker(&c, sc->slots[0], sc->slots[1], sc->slots[2], sc->slots[3])) hud_failed(sc);
}
void es2_sim_service_map_selected(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (map_selected(&c, sc->slots[0], sc->slots[1])) hud_failed(sc);
}

/* ---- the HDD map page (HDDMap, vtable 0x49d9d8) */

/* 0x438aac(L, text, n) (listing 0x438aac..0x438b1b): the label's text
 * (0x438a94 -> 0x438920) and, when the text is at least n long (signed
 * 16-bit), the key letter text[n] marked: x 0x4d17b4 advanced by the width
 * of the first n characters (the byte cut to 0 and put back) and the
 * letter drawn in the label's second font [L+0x21] (0x482534([L+0x21],
 * 0x4d17b4, the signed letter)). */
static int label_key(hud_ctx *c, uint32_t l, uint32_t text, uint32_t n) {
    if (label_draw(c, l, text)) return 1;
    uint32_t len = 0;
    if (HCALL(c, 0x00496766u, &len, text)) return 1;
    int16_t k = (int16_t)(uint16_t)n;
    if ((int16_t)(uint16_t)len < k) return 0;
    uint8_t ch = hr8(c, text + (uint32_t)(int32_t)k);
    hw8(c, text + (uint32_t)(int32_t)k, 0);
    uint32_t w = hud_text_width(c, hr32(c, l), text);
    hw8(c, text + (uint32_t)(int32_t)k, ch);
    add32(c, VA_TEXT_X, (uint32_t)(int32_t)(int16_t)w);
    return HCALL(c, VA_CHAR, NULL, hr32(c, l + 0x21u), VA_TEXT_X, hsx8(ch));
}
/* 0x44de14(M) (listing 0x44de14..0x44defc): the eight wingman keys: per
 * i, label [M+0x3d+4i] colours ([L], [L+0x21]): slot byte M+0x131+i clear
 * or the MFD blinking off ([[0x49d6b0]+8]+0x76): [0x49b0ac] for both; the
 * selected one (i + 10 = [M+0x5d]): [0x49b0b8], [0x49b0b4]; else
 * [0x49b0b0], [0x49b0b4]; text [0x4d12a8 + 4 (i+10)] with key letter
 * index byte [[0x4d12f0 + 4 (i+10)]] (0x438aac). Byte M+0x139 <- 0; the
 * selected key marked (0x44dd74(M, i, 0, 0)). */
static int map_keys(hud_ctx *c, uint32_t m) {
    uint8_t off = hr8(c, hr32(c, hr32(c, 0x0049d6b0u) + 8u) + 0x76u);
    uint32_t c8 = hr32(c, 0x0049b0acu), cc = hr32(c, 0x0049b0b4u), c10 = hr32(c, 0x0049b0b0u), c14 = hr32(c, 0x0049b0b8u);
    uint32_t sel = 0xffffffffu;
    for (uint32_t i = 0; i < 8u; ++i) {
        uint32_t idx = i + 10u, a, b;
        if (!hr8(c, m + 0x131u + i) || off) { a = c8; b = c8; }
        else if (idx == hr32(c, m + 0x5du)) { a = c14; b = cc; sel = i; }
        else { a = c10; b = cc; }
        uint32_t l = hr32(c, m + 0x3du + 4u * i);
        hw32(c, l, a);
        hw32(c, hr32(c, m + 0x3du + 4u * i) + 0x21u, b);
        if (label_key(c, hr32(c, m + 0x3du + 4u * i), hr32(c, 0x004d12a8u + 4u * idx), hr8(c, hr32(c, 0x004d12f0u + 4u * idx)))) return 1;
    }
    hw8(c, m + 0x139u, 0);
    return sel == 0xffffffffu ? 0 : map_marker(c, m, sel, 0u, 0u);
}
/* 0x44f65c(I, s) (listing 0x44f65c..0x44f676): the icon's size: +8, +0xc
 * <- s, +0x2a, +0x26 <- s sar 1. */
static void icon_size(hud_ctx *c, uint32_t i, uint32_t s) {
    hw32(c, i + 8u, s);
    hw32(c, i + 0xcu, s);
    hw32(c, i + 0x2au, (uint32_t)((int32_t)s >> 1));
    hw32(c, i + 0x26u, (uint32_t)((int32_t)s >> 1));
}
static uint32_t icon_next(hud_ctx *c, uint32_t m) {
    uint16_t k = hr16(c, m + 0x10au);
    hw16(c, m + 0x10au, (uint16_t)(k + 1u));
    return hr32(c, hr32(c, m + 0x31u) + 4u * (uint32_t)(int32_t)(int16_t)k);
}
/* 0x44e0a8(M, V) (listing 0x44e0a8..0x44e331): the map icon of actor V by
 * type word V+0x1a8. MECH (0): the player 0x2a, a friend 9 w + 0x33 by
 * wingman_index (hook; -1: 0x21), an enemy 0x18, size 5 units; vehicle
 * (2): 6, +9 for an enemy, size 5; building and object (1, 3), with the
 * extra fields (flag +0x4c): word [[V+0x1f2]+0x32] set: 0x58 / 0x59
 * (friend / enemy, colour word 0x4d3b86 / 0x4d3b8e), size 3 units, word
 * +0x4d 3; else by word [[V+0x1f2]+0x28] (byte table 0x44e1f4, 29 rows,
 * above 0x1c the default): row 1: 3 / 5, size 7 units, +0x4d 7; default: 2
 * / 4 (enemy colour 4), size 9 units, +0x4d 9 units; +0x4f 0, +0x57 25000.
 * Other types: no icon. The icon is the next of [M+0x31] (word M+0x10a):
 * +0x24 <- the code, +0x38 <- V, word +0x48 <- the type, the size
 * (0x44f65c), +0x4c and the extra fields. */
static int map_icon_add(hud_ctx *c, uint32_t m, uint32_t v) {
    uint32_t p = hr32(c, hr32(c, VA_PANEL) + 0x203u);
    int32_t code = -1;
    uint8_t flag = 0;
    uint16_t type = hr16(c, v + 0x1a8u), w2 = 0, w4 = 0, w6 = 0;
    uint32_t size = 0, reach = 0;
    int enemy = hr8(c, hr32(c, v + 0x45u) + 0x12u) != 0;
    switch ((int16_t)type) {
    case 0:
        if (p == v) code = 0x2a;
        else if (!enemy) {
            uint32_t w = 0;
            if (HCALL(c, 0x00433134u, &w, v)) return 1;
            code = w != 0xffffffffu ? (int32_t)(w * 9u + 0x33u) : 0x21;
        } else code = 0x18;
        w2 = (uint16_t)(5u << shx(c));
        size = (uint32_t)(int32_t)(int16_t)w2;
        break;
    case 2:
        code = enemy ? 6 + 9 : 6;
        w2 = (uint16_t)(5u << shx(c));
        size = (uint32_t)(int32_t)(int16_t)w2;
        break;
    case 1: case 3: {
        flag = 1;
        uint32_t ch = hr32(c, v + 0x1f2u);
        if (hr16(c, ch + 0x32u)) {
            if (!enemy) { w6 = hr16(c, 0x004d3b86u); code = 0x58; }
            else { w6 = hr16(c, 0x004d3b8eu); code = 0x59; }
            size = 3u << shx(c);
            reach = 0x61a8u;
            w2 = 3;
            w4 = 0;
        } else {
            uint32_t t = (uint32_t)(int32_t)(int16_t)hr16(c, ch + 0x28u);
            if (t <= 0x1cu && hr8(c, 0x0044e1f4u + t) == 1u) {
                if (!enemy) { w6 = hr16(c, 0x004d3b86u); code = 3; }
                else { w6 = hr16(c, 0x004d3b8eu); code = 5; }
                size = 7u << shx(c);
                reach = 0x61a8u;
                w2 = 7;
                w4 = 0;
            } else {
                if (!enemy) { w6 = hr16(c, 0x004d3b86u); code = 2; }
                else { w6 = 4; code = 4; }
                size = 9u << shx(c);
                reach = 0x61a8u;
                w2 = (uint16_t)(9u << shx(c));
                w4 = 0;
            }
        }
        break;
    }
    default:
        return 0;
    }
    uint32_t i = icon_next(c, m);
    hw16(c, i + 0x24u, (uint16_t)code);
    hw32(c, i + 0x38u, v);
    hw16(c, i + 0x48u, type);
    icon_size(c, i, size);
    hw8(c, i + 0x4cu, flag);
    if (flag) {
        hw16(c, i + 0x4du, w2);
        hw16(c, i + 0x4fu, w4);
        hw16(c, i + 0x51u, w6);
        hw32(c, i + 0x57u, reach);
    }
    return 0;
}
/* 0x44df00(M) (listing 0x44df00..0x44e0a6), the map's icon list, built on
 * the first draw (byte M+0x109): the player's route [[P+0x45]+6] (when
 * set): its first min(count - 1, 9) waypoints as icons 0x4e.. (position
 * from [[R+2] + 4 + 4i], size 9 units); the actors of the pools
 * [0x4a95a0], [0x4a9b7a], [0x4a9db9], each walked from its end (pool_prev),
 * through 0x44e0a8; one icon 0x57 of size 4 units; M+0x109 <- 1; the
 * unused icons of the 0x8c (from word M+0x10a) deleted, their pointers
 * left in the table. */
static int map_icons(hud_ctx *c, uint32_t m) {
    uint32_t p = hr32(c, hr32(c, VA_PANEL) + 0x203u), r = hr32(c, hr32(c, p + 0x45u) + 6u);
    if (r) {
        int32_t n = sx16(c, r) - 1;
        if (9 < n) n = 9;
        uint16_t code = 0x4e;
        for (int32_t i = 0; i < n; ++i) {
            uint32_t ic = icon_next(c, m);
            hw16(c, ic + 0x24u, code++);
            uint32_t e = hr32(c, hr32(c, r + 2u) + 4u * (uint32_t)i + 4u);
            hw32(c, ic + 0x3cu + 8u, hr32(c, e + 8u));
            hw32(c, ic + 0x3cu, hr32(c, e));
            hw32(c, ic + 0x3cu + 4u, hr32(c, e + 4u));
            icon_size(c, ic, 9u << shx(c));
        }
    }
    static const uint32_t pools[3] = {0x004a95a0u, 0x004a9b7au, 0x004a9db9u};
    for (unsigned k = 0; k < 3u; ++k) {
        uint32_t item = 0;
        for (;;) {
            if (HCALL(c, 0x00471c70u, &item, hr32(c, pools[k]), item)) return 1;  /* pool_prev (hook) */
            if (!item) break;
            if (map_icon_add(c, m, item)) return 1;
        }
    }
    uint32_t ic = icon_next(c, m);
    hw16(c, ic + 0x24u, 0x57);
    icon_size(c, ic, 4u << shx(c));
    hw8(c, m + 0x109u, 1);
    for (int32_t j = sx16(c, m + 0x10au); j < 0x8c; ++j)
        if (HCALL(c, HUD_VA_DELETE, NULL, hr32(c, hr32(c, m + 0x31u) + 4u * (uint32_t)j))) return 1;
    return 0;
}
/* 0x44d2c4(M) (listing 0x44d2c4..0x44d36f): the map's own context [M+0x10]
 * <- the map rectangle M+0x71..+0x7c less the GC origin and the 640x480
 * offsets (both corners), selected (0x480d44); EAX the context. */
static int map_context(hud_ctx *c, uint32_t m, uint32_t *out) {
    uint32_t gc = hr32(c, VA_GC), b = hr32(c, m + 0x10u);
    for (uint32_t k = 0; k < 4u; ++k) hw32(c, b + 0x210u + 4u * k, hr32(c, m + 0x71u + 4u * k));
    for (uint32_t k = 0; k < 2u; ++k) {
        add32(c, b + 0x210u + 4u * k, 0u - hr32(c, gc + 0x220u + 4u * k));
        add32(c, b + 0x218u + 4u * k, 0u - hr32(c, gc + 0x220u + 4u * k));
    }
    for (uint32_t k = 0; k < 2u; ++k) {
        add32(c, b + 0x210u + 4u * k, 0u - hr32(c, VA_ORIGIN_X + 4u * k));
        add32(c, b + 0x218u + 4u * k, 0u - hr32(c, VA_ORIGIN_X + 4u * k));
    }
    if (HCALL(c, VA_GC_SELECT, NULL, hr32(c, m + 0x10u))) return 1;
    *out = hr32(c, m + 0x10u);
    return 0;
}
/* 0x44d188(M) (listing 0x44d188..0x44d24a): the map centre M+0x18/+0x1c
 * clamped to the terrain bounds 0x4aa640..0x4aa64c widened by 60000 and
 * narrowed by the half view ((-origin * zoom M+0x20) sar [0x49d6bc]); a
 * clamp sets the map redraw byte 0x49d6ae; the centre's three dwords to
 * [M+0x14]+4. */
static void map_clamp(hud_ctx *c, uint32_t m) {
    unsigned sh = hr8(c, 0x0049d6bcu) & 31u;
    uint32_t b = hr32(c, m + 0x10u);
    for (uint32_t k = 0; k < 2u; ++k) {
        int32_t half = (int32_t)((0u - hr32(c, b + 0x220u + 4u * k)) * hr32(c, m + 0x20u)) >> sh;
        int32_t lo = (int32_t)(hr32(c, 0x004aa640u + 4u * k) + 0xffff15a0u) + half;
        if (lo > (int32_t)hr32(c, m + 0x18u + 4u * k)) { hw32(c, m + 0x18u + 4u * k, (uint32_t)lo); hw8(c, 0x0049d6aeu, 1); }
        int32_t hi = (int32_t)(hr32(c, 0x004aa648u + 4u * k) + 0xea60u) - half;
        if (hi < (int32_t)hr32(c, m + 0x18u + 4u * k)) { hw32(c, m + 0x18u + 4u * k, (uint32_t)hi); hw8(c, 0x0049d6aeu, 1); }
    }
    uint32_t t = hr32(c, m + 0x14u) + 4u;
    hw32(c, t + 8u, hr32(c, m + 0x20u));
    hw32(c, t, hr32(c, m + 0x18u));
    hw32(c, t + 4u, hr32(c, m + 0x1cu));
}
/* The map draw after its view set-up and what it calls. */
#define VA_TRANSFORM 0x0048c57cu   /* a point through the view matrix (view.c row) */
#define VA_PROJECT 0x0048c5fcu     /* the projection (view.c row) */
#define VA_TO_SCREEN 0x0048c6d0u   /* to the screen centre (view.c row) */
#define VA_MAP_SELECTED 0x004d03c8u
static void copy3(hud_ctx *c, uint32_t d, uint32_t s) {
    hw32(c, d + 8u, hr32(c, s + 8u));
    hw32(c, d, hr32(c, s));
    hw32(c, d + 4u, hr32(c, s + 4u));
}
/* 0x434280(R) (listing 0x434280..0x434297): the colour of wingman R, the
 * signed word 0x4d3b7c[the signed word 0x49b040[R]]. */
static uint32_t wing_colour(hud_ctx *c, uint32_t r) {
    return (uint32_t)sx16(c, 0x004d3b7cu + 2u * (uint32_t)sx16(c, 0x0049b040u + 2u * r));
}
/* p (3 dwords) through the view matrix (0x48c57c) into tmp and projected
 * (0x48c5fc) into s: the call pairs of the map draws. */
static int map_project(hud_ctx *c, uint32_t p, uint32_t tmp, uint32_t s) {
    return HCALL(c, VA_TRANSFORM, NULL, p, tmp) || HCALL(c, VA_PROJECT, NULL, tmp, s);
}
/* 0x44c250(p, out) (listing 0x44c250..0x44c28b; the linear listing decodes
 * it as data, read with --start-address): p through the view matrix and
 * projected into locals, then moved to the screen centre (0x48c6d0) into
 * out. */
static int map_to_screen(hud_ctx *c, uint32_t p, uint32_t out) {
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0x14u);
    if (!f) return hud_refuse(c, 0x0044c250u, "map to screen", "the own code's call area is exhausted");
    int bad = map_project(c, p, f, f + 0xcu) || HCALL(c, VA_TO_SCREEN, NULL, f + 0xcu, out);
    hud_frame_pop(c, top);
    return bad;
}
/* 0x45030c(a) (listing 0x45030c..0x450456), the map's terrain picture: the
 * object matrix reset (0x48c444(0)); the camera object [0x4d24ea] +4 and
 * the terrain corner ([0x4aa640] - 60000, [0x4aa64c] + 60000, 0) to the
 * screen (0x44c250); the turn a (sincos_pair 0x47eb30) of their
 * difference (rotate_2d 0x47ffe8); the far corner ([0x4aa648] + 60000,
 * [0x4aa644] - 60000, 0) to the screen; then the rotated blit 0x48185c of
 * the map bitmap 0x4d1cf6 from the turned corner (ebp-0x10) to the far one
 * (ebp-0x20) by a (0x48185c(0x4d1cf6, ebp-0x10, 0, ebp-0x20, a)). Every
 * write but the callees' goes to locals. */
static int map_terrain(hud_ctx *c, uint32_t a) {
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0x6cu);
    if (!f) return hud_refuse(c, 0x0045030cu, "map terrain", "the own code's call area is exhausted");
    uint32_t e = f + 0x6cu;
#define L(k) (e - (k))
    int bad = HCALL(c, VA_VIEW_TRANSFORM, NULL, 0u);
    if (!bad) {
        copy3(c, L(0x54), hr32(c, 0x004d24eau) + 4u);
        bad = map_to_screen(c, L(0x54), L(8));
    }
    if (!bad) {
        hw32(c, L(0x60), hr32(c, 0x004aa640u) + 0xffff15a0u);
        hw32(c, L(0x5c), hr32(c, 0x004aa64cu) + 0xea60u);
        hw32(c, L(0x58), 0);
        bad = map_to_screen(c, L(0x60), L(0x10));
    }
    if (!bad) {
        hw32(c, L(0x30), hr32(c, L(8)) - hr32(c, L(0x10)));
        hw32(c, L(0x2c), hr32(c, L(4)) - hr32(c, L(0xc)));
        bad = HCALL(c, 0x0047eb30u, NULL, a, L(0x38));
    }
    if (!bad) {
        hw32(c, L(0x40), hr32(c, L(8)) - hr32(c, L(0x10)));
        hw32(c, L(0x3c), hr32(c, L(4)) - hr32(c, L(0xc)));
        bad = HCALL(c, 0x0047ffe8u, NULL, L(0x40), L(0x38), L(0x18));
    }
    if (!bad) {
        add32(c, L(0x18), hr32(c, L(0x10)));
        add32(c, L(0x14), hr32(c, L(0xc)));
        hw32(c, L(0x48), hr32(c, L(8)) - hr32(c, L(0x18)));
        hw32(c, L(0x44), hr32(c, L(4)) - hr32(c, L(0x14)));
        add32(c, L(0x10), hr32(c, L(0x48)));
        add32(c, L(0xc), hr32(c, L(0x44)));
        hw32(c, L(0x6c), hr32(c, 0x004aa648u) + 0xea60u);
        hw32(c, L(0x68), hr32(c, 0x004aa644u) + 0xffff15a0u);
        hw32(c, L(0x64), 0);
        bad = map_to_screen(c, L(0x6c), L(0x20));
    }
    if (!bad) {
        add32(c, L(0x20), 0u - hr32(c, L(0x10)));
        add32(c, L(0x1c), 0u - hr32(c, L(0xc)));
        add32(c, L(0x20), hr32(c, L(0x48)));
        add32(c, L(0x1c), hr32(c, L(0x44)));
        bad = HCALL(c, VA_ROTATED, NULL, 0x004d1cf6u, L(0x10), 0u, L(0x20), a);
    }
#undef L
    hud_frame_pop(c, top);
    return bad;
}
/* The grid lines of 0x44e334 (0x44e50e..0x44e63e), one direction, over
 * the points p0 = ebp-0x34 and p1 = ebp-0x3c and the base ebp-0x40 of the
 * map draw's frame (e its ebp; the moving coordinate at +0 of both points
 * for the vertical lines, +4 for the horizontal ones; their other
 * coordinates are set once, not per line). The context's half extent
 * [O+0x220+off] is read on every test, the moving coordinate from p0:
 * from the base up while -[O+0x220+off] > p0, then from base - (step sar
 * 4) down while [O+0x220+off] < p0, each pass a line 0x483a04(p0, p1)
 * first, then x = base + ((i * step) sar 4) (32-bit imul) into both
 * points. A step that never leaves the range loops forever in the
 * original: refused after 2^20 lines. */
static int map_grid(hud_ctx *c, uint32_t site, uint32_t e, uint32_t o, uint32_t m, uint32_t off) {
    const uint32_t p0 = e - 0x34u, p1 = e - 0x3cu, base = e - 0x40u;
    uint32_t n = 0;
    int32_t i = 0;
    while ((int32_t)(0u - hr32(c, o + 0x220u + off)) > (int32_t)hr32(c, p0 + off)) {
        if (++n > 0x100000u) return hud_refuse(c, site, "map grid", "the grid step never leaves the map (the original loops)");
        if (HCALL(c, VA_LINE, NULL, p0, p1)) return 1;
        ++i;
        uint32_t x = (uint32_t)((int32_t)((uint32_t)i * hr32(c, m + 0x2cu)) >> 4) + hr32(c, base);
        hw32(c, p1 + off, x);
        hw32(c, p0 + off, x);
    }
    i = -1;
    {
        uint32_t x = hr32(c, base) - (uint32_t)((int32_t)hr32(c, m + 0x2cu) >> 4);
        hw32(c, p1 + off, x);
        hw32(c, p0 + off, x);
    }
    while ((int32_t)hr32(c, o + 0x220u + off) < (int32_t)hr32(c, p0 + off)) {
        if (++n > 0x100000u) return hud_refuse(c, site, "map grid", "the grid step never leaves the map (the original loops)");
        if (HCALL(c, VA_LINE, NULL, p0, p1)) return 1;
        --i;
        uint32_t x = (uint32_t)((int32_t)((uint32_t)i * hr32(c, m + 0x2cu)) >> 4) + hr32(c, base);
        hw32(c, p1 + off, x);
        hw32(c, p0 + off, x);
    }
    return 0;
}
/* 0x44d288(M, s, t) (listing 0x44d288..0x44d2c1): t = s less the map
 * rectangle's corner M+0x71/+0x75 and its half size ((M+0x79 - M+0x71)
 * sar 1, (M+0x7d - M+0x75) sar 1); s.x is read before t.x is written,
 * s.y after (s = t is fine). */
static void map_relative(hud_ctx *c, uint32_t m, uint32_t s, uint32_t t) {
    hw32(c, t, hr32(c, s));
    hw32(c, t + 4u, hr32(c, s + 4u));
    for (uint32_t k = 0; k < 2u; ++k) {
        add32(c, t + 4u * k, 0u - hr32(c, m + 0x71u + 4u * k));
        add32(c, t + 4u * k, 0u - (uint32_t)((int32_t)(hr32(c, m + 0x79u + 4u * k) - hr32(c, m + 0x71u + 4u * k)) >> 1));
    }
}
/* 0x44f1bc(I) (listing 0x44f1bc..0x44f5cd), map icon slot 0, the draw: the
 * canvas pushed (0x4311e0); the map context of M = [I+0x20] (0x44d2c4);
 * the position P (ebp-0x3c) <- I's corner relative to the map (0x44d288).
 * The bitmap from the table [[0x49d6b4]+8] by the code word I+0x24: an
 * actor V = [I+0x38] of type word V+0x1a8 = 0 or 2 takes the heading row
 * k (0 for the heading word V+0x10 outside (0x1000, 0xf000), else 7 less
 * one per 0x2000 step from 0x3000 below it), moves P by the signed bytes
 * 0x4d1cd0[k], 0x4d1cd8[k] shifted by the scale words 0x4d253a/0x4d253c
 * (16-bit) and adds, unless destroyed (byte V+0x99), the word 0x49d67c[k]
 * to the code; types 1 and 3 the code alone; any other type: the canvas
 * popped, done. The locals: the GC (ebp-0x40), the records ebp-0x48 (0,
 * .), ebp-0x50 (0, .), ebp-0x58 (4, .), word ebp-0x24 <- 8, the temporary
 * object ebp-0x14 (vtable 0x49d984 after its two bases; counter + 3); the
 * centre C (ebp-0x68) = P + I+0x26. With the range flag byte I+0x4c: I+0x53
 * <- 1 when 0, q = ([I+0x57] << 7) idiv [I+0x53] into a = ebp-0x60 and b =
 * ebp-0x5c; q < word I+0x4f: a, b halved, C less a (both), the record
 * ebp-0x48 gets the colour word I+0x51, GC+0x228 <- it, the point 0x4895ec
 * (C), word I+0x4a <- 0; else the bitmap's word +4 above q: a, b halved,
 * the object's rectangle (ebp-0x10) C -/+ (a, b), the records ebp-0x50
 * (word 0x4d3b9c) and ebp-0x58 (word I+0x51) in turn GC+0x22c and the
 * object filled (0x482cec), word I+0x4a <- a; else (and without the flag):
 * a, b <- word I+0x4d sar 1 shifted by the scale words, P plus the 640x480
 * offsets, the blit 0x4816a8(bitmap, P, 0), P back, word I+0x4a <- a.
 * Type 1 with byte V+0x96 set and not destroyed: r = (0x222e0 << [K+0x1a])
 * idiv [K+0xc] (K = [M+0x14]), C less (a sar 1, b sar 1), the rectangle C
 * -/+ r, the record ebp-0x58 gets the colour word I+0x51, GC+0x22c <- it,
 * the ellipse 0x48817c. The canvas popped, the temporaries destroyed
 * (counter - 3). Faults kept as refusals: the idivs and a NULL bitmap
 * read. The frame record of 0x49630c is not linked. */
static int icon_draw(hud_ctx *c, uint32_t i) {
    uint32_t m = hr32(c, i + 0x20u), o = 0;
    int16_t type = -1;
    if (canvas_push(c, NULL) || map_context(c, m, &o)) return 1;
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0x78u);
    if (!f) return hud_refuse(c, 0x0044f1bcu, "map icon draw", "the own code's call area is exhausted");
    uint32_t e = f + 0x78u;
#define L(k) (e - (k))
    map_relative(c, m, i, L(0x3c));
    uint32_t v = hr32(c, i + 0x38u);
    uint16_t code = hr16(c, i + 0x24u);
    if (v) {
        type = (int16_t)hr16(c, v + 0x1a8u);
        if (type == 0 || type == 2) {
            uint16_t h = hr16(c, v + 0x10u), d = 0x3000;
            uint32_t k = 0;
            if (h < 0xf000u && h > 0x1000u)
                for (k = 7; d < h; --k) d = (uint16_t)(d + 0x2000u);
            unsigned sxs = hr16(c, 0x004d253au) & 31u, sys = hr16(c, 0x004d253cu) & 31u;
            add32(c, L(0x3c), hsx16((uint16_t)(hsx8(hr8(c, 0x004d1cd0u + k)) << sxs)));
            add32(c, L(0x38), hsx16((uint16_t)(hsx8(hr8(c, 0x004d1cd8u + k)) << sys)));
            if (!hr8(c, v + 0x99u)) code = (uint16_t)(code + hr16(c, 0x0049d67cu + 2u * k));
        } else if (type != 1 && type != 3) {
            hud_frame_pop(c, top);
            return canvas_pop(c);
        }
    }
    uint32_t bm = hr32(c, hr32(c, hr32(c, 0x0049d6b4u) + 8u) + 4u * (uint32_t)(int32_t)(int16_t)code);
    uint32_t g = hr32(c, VA_GC);
    hw32(c, L(0x40), g);
    hw32(c, L(0x48), 0);
    hw32(c, L(0x50), 0);
    hw32(c, L(0x58), 4u);
    hw16(c, L(0x24), 8u);
    hw32(c, L(0x14), 0x0049d984u);
    hud_counter(c, 3);
    hw32(c, L(0x68), hr32(c, L(0x3c)) + hr32(c, i + 0x26u));
    hw32(c, L(0x64), hr32(c, L(0x38)) + hr32(c, i + 0x2au));
    const uint32_t qa = L(0x60), qb = L(0x5c);
    int bad = 0, blit = 0;
    if (hr8(c, i + 0x4cu)) {
        if (!hr32(c, i + 0x53u)) hw32(c, i + 0x53u, 1);
        int32_t num = (int32_t)(hr32(c, i + 0x57u) << 7), den = (int32_t)hr32(c, i + 0x53u);
        if (!idiv_ok(num, den)) {
            bad = hud_refuse(c, 0x0044f371u, "map icon draw", "the range quotient faults (the original's idiv)");
            goto out;
        }
        int32_t q = num / den;
        hw32(c, qb, (uint32_t)q);
        hw32(c, qa, (uint32_t)q);
        if (q < sx16(c, i + 0x4fu)) {
            hw32(c, qa, (uint32_t)(q >> 1));
            hw32(c, qb, (uint32_t)(q >> 1));
            add32(c, L(0x68), 0u - hr32(c, qa));
            add32(c, L(0x64), 0u - hr32(c, qa));
            hw32(c, L(0x44), (uint32_t)sx16(c, i + 0x51u));
            hw32(c, g + 0x228u, L(0x48));
            if ((bad = HCALL(c, VA_POINT, NULL, L(0x68))) != 0) goto out;
            hw16(c, i + 0x4au, 0);
        } else if (!bm) {
            bad = hud_null_refuse(c, 0x0044f3bbu, "map icon draw", "no bitmap for the icon code (the original reads through NULL)");
            goto out;
        } else if (sx16(c, bm + 4u) > q) {
            hw32(c, qa, (uint32_t)(q >> 1));
            hw32(c, qb, (uint32_t)(q >> 1));
            hw32(c, L(0x54), (uint32_t)sx16(c, i + 0x51u));
            hw32(c, L(0x4c), (uint32_t)sx16(c, 0x004d3b9cu));
            hw32(c, L(0x10), hr32(c, L(0x68)) - hr32(c, qa));
            hw32(c, L(0xc), hr32(c, L(0x64)) - hr32(c, qb));
            hw32(c, L(8), hr32(c, L(0x68)) + hr32(c, qa));
            hw32(c, L(4), hr32(c, L(0x64)) + hr32(c, qb));
            hw32(c, g + 0x22cu, L(0x50));
            if ((bad = HCALL(c, VA_OBJECT_FILL, NULL, L(0x14))) != 0) goto out;
            hw32(c, g + 0x22cu, L(0x58));
            if ((bad = HCALL(c, VA_OBJECT_FILL, NULL, L(0x14))) != 0) goto out;
            hw16(c, i + 0x4au, hr16(c, qa));
        } else {
            blit = 1;
        }
    } else {
        blit = 1;
    }
    if (blit) {
        uint32_t half = (uint32_t)((int32_t)sx16(c, i + 0x4du) >> 1);
        hw32(c, qa, half << (hr16(c, 0x004d253au) & 31u));
        hw32(c, qb, half << (hr16(c, 0x004d253cu) & 31u));
        uint32_t dx = 0u - hr32(c, VA_ORIGIN_X), dy = 0u - hr32(c, VA_ORIGIN_Y);
        add32(c, L(0x3c), 0u - dx);
        add32(c, L(0x38), 0u - dy);
        if ((bad = HCALL(c, VA_BLIT, NULL, bm, L(0x3c), 0u)) != 0) goto out;
        add32(c, L(0x3c), dx);
        add32(c, L(0x38), dy);
        hw16(c, i + 0x4au, hr16(c, qa));
    }
    if (type == 1 && hr8(c, v + 0x96u) && !hr8(c, v + 0x99u)) {
        uint32_t k = hr32(c, m + 0x14u);
        int32_t num = (int32_t)(0x222e0u << (hr8(c, k + 0x1au) & 31u)), den = (int32_t)hr32(c, k + 0xcu);
        if (!idiv_ok(num, den)) {
            bad = hud_divide_refuse(c, 0x0044f54bu, (uint64_t)(int64_t)num, (uint32_t)den, "map icon draw",
                                    "the building ring's divisor faults (the original's idiv)");
            goto out;
        }
        uint32_t r = (uint32_t)(num / den);
        add32(c, L(0x68), 0u - (uint32_t)((int32_t)hr32(c, qa) >> 1));
        add32(c, L(0x64), 0u - (uint32_t)((int32_t)hr32(c, qb) >> 1));
        hw32(c, L(0x10), hr32(c, L(0x68)) - r);
        hw32(c, L(0xc), hr32(c, L(0x64)) - r);
        hw32(c, L(8), hr32(c, L(0x68)) + r);
        hw32(c, L(4), hr32(c, L(0x64)) + r);
        hw32(c, L(0x54), (uint32_t)sx16(c, i + 0x51u));
        hw32(c, g + 0x22cu, L(0x58));
        if ((bad = HCALL(c, VA_ELLIPSE, NULL, L(0x10))) != 0) goto out;
    }
out:
#undef L
    hud_frame_pop(c, top);
    if (bad) return 1;
    if (canvas_pop(c)) return 1;
    hud_counter(c, -3);
    return 0;
}
/* 0x44e954(M, I, sel) (listing 0x44e954..0x44eddd), one icon of the map
 * draw: GC+0x228 <- the local record ebp-0x10 (0, colour ebp-0xc). The
 * position: without an actor V =
 * [I+0x38] the vector I+0x3c. An actor destroyed or removed (bytes V+0x99,
 * V+0xa4) whose side [V+0x45] has byte +0x12 set: nothing. Byte V+0x95
 * clear and (side byte +0x12 or side dword +0x14 set): the clock (site
 * 0x44e9fc), bit 5 set: nothing (the blink); byte V+0xa7 set: nothing;
 * else (V+0x1aa, V+0x1ae, 0). Otherwise (V+0x26, V+0x2a, 0). Then the
 * object matrix reset (0x48c444(0)), the position transformed and
 * projected, x less [I+0x26], y <- -(y + [I+0x2a]), plus the centre of the
 * map rectangle M+0x71..+0x7d (0x44d24c); I+0x2e/+0x32 <- I+0/+4 and the
 * icon rectangle moved there keeping its size (0x452454). With an actor
 * and byte sel: the order line of the selected wingman's record R =
 * [M+0x105] (42-byte records; byte +0xa6: colour 0x434280(R), target by
 * kind [+0xab]: 0 the point M+0x91+42R, 1 the unit [+0xa1] (side byte
 * +0x12 = 1: its V+0x26 when byte +0x95, else its V+0x1aa unless byte
 * +0xa7, which clears the colour; other sides V+0x26); else byte +0xa5:
 * colour 0x434280(R) - 1, kind [+0xa7]: 0 the point M+0x85+42R, 1 the unit
 * [+0x9d]'s V+0x26). A colour (ebp-0xc): GC+0x228 <- the record again, the
 * target (z 0) transformed and projected, y negated, the line 0x483a04
 * from the icon's corner relative to the map (0x44d288 on a local copy of
 * I's rectangle, ebp-0x84) plus I+0x26/+0x2a to it. Then bytes M+0x381 and 0x49d6ad both set: done. The range
 * flag byte I+0x4c: I+0x53 <- dist3d_delta (0x47de72) of the position less
 * the map centre M+0x18. Then the icon's slot 0 (0x44f1bc). */
static int map_icon(hud_ctx *c, uint32_t m, uint32_t i, uint32_t sel) {
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0x84u);
    if (!f) return hud_refuse(c, 0x0044e954u, "map icon", "the own code's call area is exhausted");
    uint32_t e = f + 0x84u;
#define L(k) (e - (k))
    int bad = 0;
    uint32_t g = hr32(c, VA_GC), v = hr32(c, i + 0x38u);
    hw32(c, L(0x10), 0);
    hw32(c, g + 0x228u, L(0x10));
    if (v) {
        uint32_t side = hr32(c, v + 0x45u);
        if ((hr8(c, v + 0xa4u) || hr8(c, v + 0x99u)) && hr8(c, side + 0x12u)) goto done;
        if (!hr8(c, v + 0x95u) && (hr8(c, side + 0x12u) || hr32(c, side + 0x14u))) {
            uint32_t now = 0;
            if ((bad = hud_clock(c, 0x0044e9fcu, &now)) != 0 || (now & 0x20u) || hr8(c, v + 0xa7u)) goto done;
            hw32(c, L(0x74), hr32(c, v + 0x1aau));
            hw32(c, L(0x70), hr32(c, v + 0x1aeu));
        } else {
            copy3(c, L(0x74), v + 0x26u);
        }
        hw32(c, L(0x6c), 0);
    } else {
        copy3(c, L(0x74), i + 0x3cu);
    }
    if ((bad = HCALL(c, VA_VIEW_TRANSFORM, NULL, 0u)) != 0 || (bad = map_project(c, L(0x74), L(0x5c), L(8))) != 0) goto done;
    hw32(c, L(8), hr32(c, L(8)) - hr32(c, i + 0x26u));
    hw32(c, L(4), 0u - (hr32(c, L(4)) + hr32(c, i + 0x2au)));
    /* 0x44d24c(M, s, t) (listing 0x44d24c..0x44d287): t = s + the centre
     * of the map rectangle (half sizes sar 1) */
    hw32(c, L(0x1c), hr32(c, L(8)) + hr32(c, m + 0x71u) + (uint32_t)((int32_t)(hr32(c, m + 0x79u) - hr32(c, m + 0x71u)) >> 1));
    hw32(c, L(0x18), hr32(c, L(4)) + hr32(c, m + 0x75u) + (uint32_t)((int32_t)(hr32(c, m + 0x7du) - hr32(c, m + 0x75u)) >> 1));
    hw32(c, i + 0x2eu, hr32(c, i));
    hw32(c, i + 0x32u, hr32(c, i + 4u));
    /* 0x452454(r, pt) (listing 0x452454..0x45249e): r moved to pt keeping
     * its size */
    {
        uint32_t w = hr32(c, i + 8u) - hr32(c, i), h = hr32(c, i + 0xcu) - hr32(c, i + 4u);
        hw32(c, i, hr32(c, L(0x1c)));
        hw32(c, i + 4u, hr32(c, L(0x18)));
        hw32(c, i + 8u, hr32(c, i) + w);
        hw32(c, i + 0xcu, hr32(c, i + 4u) + h);
    }
    if (v && (sel & 0xffu)) {
        /* the icon rectangle relative to the map (ebp-0x84..-0x78) */
        for (uint32_t j = 0; j < 4u; ++j) hw32(c, L(0x84) + 4u * j, hr32(c, i + 4u * j));
        map_relative(c, m, L(0x84), L(0x84));
        map_relative(c, m, L(0x7c), L(0x7c));
        uint32_t r = hr32(c, m + 0x105u), rec = m + 0x2au * r, colour = 0;
        if (hr8(c, rec + 0xa6u)) {
            colour = wing_colour(c, r);
            uint32_t kind = hr32(c, rec + 0xabu);
            if (kind == 0) {
                copy3(c, L(0x68), rec + 0x91u);
            } else if (kind == 1) {
                uint32_t w = hr32(c, rec + 0xa1u);
                if ((int8_t)hr8(c, hr32(c, w + 0x45u) + 0x12u) != 1) {
                    copy3(c, L(0x68), w + 0x26u);
                } else if (hr8(c, w + 0x95u)) {
                    copy3(c, L(0x68), w + 0x26u);
                } else if (!hr8(c, w + 0xa7u)) {
                    hw32(c, L(0x68), hr32(c, w + 0x1aau));
                    hw32(c, L(0x64), hr32(c, w + 0x1aeu));
                    hw32(c, L(0x60), 0);
                } else {
                    colour = 0;
                }
            }
        } else if (hr8(c, rec + 0xa5u)) {
            colour = wing_colour(c, r) - 1u;
            uint32_t kind = hr32(c, rec + 0xa7u);
            if (kind == 0) copy3(c, L(0x68), rec + 0x85u);
            else if (kind == 1) copy3(c, L(0x68), hr32(c, rec + 0x9du) + 0x26u);
        }
        hw32(c, L(0xc), colour);
        if (colour) {
            hw32(c, g + 0x228u, L(0x10));
            hw32(c, L(0x44), hr32(c, L(0x84)) + hr32(c, i + 0x26u));
            hw32(c, L(0x40), hr32(c, L(0x80)) + hr32(c, i + 0x2au));
            hw32(c, L(0x60), 0);
            if ((bad = map_project(c, L(0x68), L(0x5c), L(8))) != 0) goto done;
            hw32(c, L(4), 0u - hr32(c, L(4)));
            if ((bad = HCALL(c, VA_LINE, NULL, L(0x44), L(8))) != 0) goto done;
        }
        if (hr8(c, m + 0x381u) && hr8(c, 0x0049d6adu)) goto done;
    }
    if (hr8(c, i + 0x4cu)) {
        hw32(c, L(0x6c), hr32(c, L(0x6c)) - hr32(c, m + 0x20u));
        hw32(c, L(0x74), hr32(c, L(0x74)) - hr32(c, m + 0x18u));
        hw32(c, L(0x70), hr32(c, L(0x70)) - hr32(c, m + 0x1cu));
        uint32_t d = 0;
        if ((bad = HCALL(c, 0x0047de72u, &d, hr32(c, L(0x74)), hr32(c, L(0x70)), hr32(c, L(0x6c)))) != 0) goto done;
        hw32(c, i + 0x53u, d);
    }
    bad = wdraw(c, i);
done:
#undef L
    hud_frame_pop(c, top);
    return bad;
}
/* 0x44e334(M) (listing 0x44e334..0x44e952), the map draw: the map context
 * O (0x44d2c4) with O+0x22c/+0x228 <- the local records ebp-8 (0, word
 * 0x4d3ba2) and ebp-0x10 (0, .), the background O+0x220.. to its negation
 * filled (0x486704); the MFD blinking off: the GC saved at the entry selected
 * again, done. Else the centre M+0x18/+0x1c <- the player's position +
 * M+0x114/+0x118; changed against M+0x125..: byte M+0x30 <- 1 and the
 * copy; the clamp (0x44d188); the view set-up 0x48c2e4([M+0x14]) and the
 * object matrix reset 0x48c444(0) (view.c rows); O+0x20c <- 0, the terrain
 * picture 0x45030c(0), O+0x20c <- 1. Byte M+0x30 set: (0, 0, 0) and
 * (0x30d400, 0x30d400, 0) transformed and projected: M+0x24 <- x0, M+0x28
 * <- -y0, M+0x2c <- x1 - x0, byte M+0x30 <- 0. The grid lines 0x483a04
 * (colour word 0x4d3b92 at ebp-0xc; bounds O+0x220/+0x224; map_grid). The terrain bounds M+0x160.. and M+0x168..
 * projected, O+0x22c <- the local record (4, word 0x4d3b8e), their frame
 * filled (0x486704(ebp-0xb4)). The terrain
 * objects [0x4aaa78] (count word 0x4aaa7c, a NULL entry ends): word
 * [[E+0x34]+8] <- 1, slot 0, <- 0. 0x48c444(0). The nav point (0x43495c:
 * H+0x25e when byte H+0x26a, H = [0x49b088]): the last used icon's +0x3c
 * <- it, its slot 0. The icons 0..word M+0x10a: the one whose actor is the
 * selected wingman [0x4d03c8 + 4[M+0x105]] (none: the player [H+0x203]) is
 * held back (the last such), the one of [M+0x15c] remembered; every other
 * through 0x44e954(M, I, 0); then the held one with sel = (its actor is the
 * selected wingman); the remembered one's highlight frame (0x44d288 on
 * a local copy, plus and less the icon's +0x26/+0x2a, its radius word
 * +0x4a and (2, 1); O+0x22c <- the record (4, word 0x4d3b9c), 0x486704). The GC saved at the entry
 * selected again. */
static int map_draw(hud_ctx *c, uint32_t m) {
    uint32_t gc = hr32(c, VA_GC), b = 0;
    if (map_context(c, m, &b)) return 1;
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0xc4u);
    if (!f) return hud_refuse(c, 0x0044e334u, "map draw", "the own code's call area is exhausted");
    uint32_t e = f + 0xc4u;
#define L(k) (e - (k))
    int bad = 0;
    hw32(c, L(0x18), hr32(c, VA_VIEW));
    hw32(c, L(0x1c), gc);
    hw32(c, b + 0x22cu, L(8));
    hw32(c, b + 0x228u, L(0x10));
    hw32(c, L(0x8c), hr32(c, b + 0x220u));
    hw32(c, L(0x88), hr32(c, b + 0x224u));
    hw32(c, L(0x84), 0u - hr32(c, b + 0x220u));
    hw32(c, L(0x80), 0u - hr32(c, b + 0x224u));
    hw32(c, L(4), (uint32_t)sx16(c, 0x004d3ba2u));
    if ((bad = HCALL(c, VA_FILL, NULL, L(0x8c))) != 0) goto done;
    if (hr8(c, hr32(c, hr32(c, 0x0049d6b0u) + 8u) + 0x76u)) {
        bad = HCALL(c, VA_GC_SELECT, NULL, gc);
        goto done;
    }
    uint32_t p = hr32(c, hr32(c, VA_PANEL) + 0x203u) + 0x26u;
    hw32(c, m + 0x18u, hr32(c, p) + hr32(c, m + 0x114u));
    hw32(c, m + 0x1cu, hr32(c, p + 4u) + hr32(c, m + 0x118u));
    if (hr32(c, m + 0x18u) != hr32(c, m + 0x125u) || hr32(c, m + 0x1cu) != hr32(c, m + 0x129u) || hr32(c, m + 0x20u) != hr32(c, m + 0x12du)) {
        hw8(c, m + 0x30u, 1);
        copy3(c, m + 0x125u, m + 0x18u);
    }
    map_clamp(c, m);
    if ((bad = HCALL(c, 0x0048c2e4u, NULL, hr32(c, m + 0x14u))) != 0) goto done;
    if ((bad = HCALL(c, VA_VIEW_TRANSFORM, NULL, 0u)) != 0) goto done;
    hw32(c, b + 0x20cu, 0);
    if ((bad = map_terrain(c, 0)) != 0) goto done;
    hw32(c, b + 0x20cu, 1);
    if (hr8(c, m + 0x30u)) {
        if ((bad = map_project(c, L(0x98), L(0x7c), L(0x24))) != 0) goto done;
        hw32(c, L(0x20), 0u - hr32(c, L(0x20)));
        add32(c, L(0x98), 0x30d400u);
        add32(c, L(0x94), 0x30d400u);
        if ((bad = map_project(c, L(0x98), L(0x7c), L(0x2c))) != 0) goto done;
        hw32(c, L(0x28), 0u - hr32(c, L(0x28)));
        hw32(c, m + 0x24u, hr32(c, L(0x24)));
        hw32(c, m + 0x28u, hr32(c, L(0x20)));
        hw32(c, m + 0x2cu, hr32(c, L(0x2c)) - hr32(c, L(0x24)));
        hw8(c, m + 0x30u, 0);
    }
    hw32(c, L(0xc), (uint32_t)sx16(c, 0x004d3b92u));
    hw32(c, L(0x40), hr32(c, m + 0x24u));
    hw32(c, L(0x3c), hr32(c, L(0x40)));
    hw32(c, L(0x34), hr32(c, L(0x40)));
    hw32(c, L(0x30), hr32(c, b + 0x224u));
    hw32(c, L(0x38), 0u - hr32(c, b + 0x224u));
    if ((bad = map_grid(c, 0x0044e539u, e, b, m, 0u)) != 0) goto done;
    hw32(c, L(0x40), hr32(c, m + 0x28u));
    hw32(c, L(0x38), hr32(c, L(0x40)));
    hw32(c, L(0x30), hr32(c, L(0x40)));
    hw32(c, L(0x34), hr32(c, b + 0x220u));
    hw32(c, L(0x3c), 0u - hr32(c, b + 0x220u));
    if ((bad = map_grid(c, 0x0044e5cdu, e, b, m, 4u)) != 0) goto done;
    hw32(c, L(0xa4), hr32(c, m + 0x160u));
    hw32(c, L(0xa0), hr32(c, m + 0x164u));
    hw32(c, L(0x9c), 0);
    if ((bad = map_project(c, L(0xa4), L(0x7c), L(0xb4))) != 0) goto done;
    hw32(c, L(0xb0), 0u - hr32(c, L(0xb0)));
    hw32(c, L(0xa4), hr32(c, m + 0x168u));
    hw32(c, L(0xa0), hr32(c, m + 0x16cu));
    hw32(c, L(0x9c), 0);
    if ((bad = map_project(c, L(0xa4), L(0x7c), L(0xac))) != 0) goto done;
    hw32(c, L(0xa8), 0u - hr32(c, L(0xa8)));
    hw32(c, L(0x48), 4);
    hw32(c, L(0x44), (uint32_t)sx16(c, 0x004d3b8eu));
    hw32(c, b + 0x22cu, L(0x48));
    if ((bad = HCALL(c, VA_FILL, NULL, L(0xb4))) != 0) goto done;
    {
        uint16_t n = hr16(c, 0x004aaa7cu);
        uint32_t q = hr32(c, 0x004aaa78u);
        for (;;) {
            uint32_t o = 0;
            if (n--) {
                o = hr32(c, q);
                q += 4u;
            }
            if (!o) break;
            hw16(c, hr32(c, hr32(c, o + 0x34u) + 8u), 1);
            if ((bad = odraw(c, o)) != 0) goto done;
            hw16(c, hr32(c, hr32(c, o + 0x34u) + 8u), 0);
        }
    }
    if ((bad = HCALL(c, VA_VIEW_TRANSFORM, NULL, 0u)) != 0) goto done;
    {
        uint32_t h = hr32(c, VA_PANEL);
        if (hr8(c, h + 0x26au)) {
            uint32_t ic = hr32(c, hr32(c, m + 0x31u) + 4u * (uint32_t)sx16(c, m + 0x10au) - 4u);
            copy3(c, ic + 0x3cu, h + 0x25eu);
            if ((bad = wdraw(c, ic)) != 0) goto done;
        }
    }
    {
        uint32_t r = hr32(c, m + 0x105u) == 0xffffffffu ? 0u : hr32(c, VA_MAP_SELECTED + 4u * hr32(c, m + 0x105u));
        uint32_t player = hr32(c, hr32(c, VA_PANEL) + 0x203u), hl = 0, held = 0;
        for (int32_t k = 0; k < sx16(c, m + 0x10au); ++k) {
            uint32_t ic = hr32(c, hr32(c, m + 0x31u) + 4u * (uint32_t)k), v = hr32(c, ic + 0x38u);
            if (v && v == hr32(c, m + 0x15cu)) hl = ic;
            if (v && v == (r ? r : player)) {
                held = ic;
                continue;
            }
            if ((bad = map_icon(c, m, ic, 0)) != 0) goto done;
        }
        if (held) {
            uint32_t v = hr32(c, held + 0x38u);
            if ((bad = map_icon(c, m, held, v && v == r ? 1u : 0u)) != 0) goto done;
        }
        hw32(c, L(0x60), hl);
        if (hl) {
            const uint32_t r0 = L(0xc4), r1 = L(0xbc);
            for (uint32_t j = 0; j < 4u; ++j) hw32(c, r0 + 4u * j, hr32(c, hl + 4u * j));
            map_relative(c, m, r0, r0);
            map_relative(c, m, r1, r1);
            for (uint32_t j = 0; j < 2u; ++j) {
                add32(c, r0 + 4u * j, hr32(c, hl + 0x26u + 4u * j));
                add32(c, r1 + 4u * j, 0u - hr32(c, hl + 0x26u + 4u * j));
            }
            uint32_t rad = (uint32_t)sx16(c, hl + 0x4au);
            for (uint32_t j = 0; j < 2u; ++j) {
                add32(c, r0 + 4u * j, 0u - rad);
                add32(c, r1 + 4u * j, rad);
            }
            for (uint32_t j = 0; j < 2u; ++j) {
                add32(c, r0 + 4u * j, 0xfffffffeu);
                add32(c, r1 + 4u * j, 1u);
            }
            hw32(c, L(0x70), 4);
            hw32(c, L(0x6c), (uint32_t)sx16(c, 0x004d3b9cu));
            hw32(c, b + 0x22cu, L(0x70));
            if ((bad = HCALL(c, VA_FILL, NULL, r0)) != 0) goto done;
        }
    }
    bad = HCALL(c, VA_GC_SELECT, NULL, gc);
done:
#undef L
    hud_frame_pop(c, top);
    return bad;
}
/* 0x44c8bc(M) (listing 0x44c8bc..0x44c986), HDDMap slot 0: GC+0x22c <- a
 * local record (0, word 0x4d3ba2), the frame [M+4] filled; the keys
 * (0x44de14); the selected key's rectangle M+0x61.. (M+0x65 <- [icon+8] +
 * 3, M+0x6d <- [icon+0x10] of [M + 0x15 + 4 [M+0x5d]] + 4) filled in the
 * record's colour <- word 0x4d3b9a; the zoom widgets [[M+8]+0x4c] and [[M+8]+0x50]; the icon
 * list on the first draw (0x44df00); the map (0x44e334); the message label
 * [M+0x13a] with [M+0x140] when word M+0x13e is set and byte M+0x144
 * clear. */
static int map_page_draw(hud_ctx *c, uint32_t m) {
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 8u);
    if (!f) return hud_refuse(c, 0x0044c8bcu, "map page draw", "the own code's call area is exhausted");
    int bad = 0;
    hw32(c, f, 0);
    hw32(c, f + 4u, (uint32_t)sx16(c, 0x004d3ba2u));
    hw32(c, hr32(c, VA_GC) + 0x22cu, f);
    if ((bad = HCALL(c, VA_FILL, NULL, hr32(c, m + 4u))) != 0 || (bad = map_keys(c, m)) != 0) goto done;
    uint32_t s = hr32(c, m + 0x5du);
    if (s != 0xffffffffu) {
        uint32_t e = hr32(c, m + 0x15u + 4u * s) + 4u;
        hw32(c, m + 0x65u, hr32(c, e + 4u) + 3u);
        hw32(c, m + 0x6du, hr32(c, e + 0xcu));
        hw32(c, f + 4u, (uint32_t)sx16(c, 0x004d3b9au));
        if ((bad = HCALL(c, VA_FILL, NULL, m + 0x61u)) != 0) goto done;
    }
    if ((bad = wdraw(c, hr32(c, hr32(c, m + 8u) + 0x4cu))) != 0 || (bad = wdraw(c, hr32(c, hr32(c, m + 8u) + 0x50u))) != 0) goto done;
    if (!hr8(c, m + 0x109u) && (bad = map_icons(c, m)) != 0) goto done;
    if ((bad = HCALL(c, 0x0044e334u, NULL, m)) != 0) goto done;
    if (hr16(c, m + 0x13eu) && !hr8(c, m + 0x144u)) bad = label_draw(c, hr32(c, m + 0x13au), hr32(c, m + 0x140u));
done:
    hud_frame_pop(c, top);
    return bad;
}
DRAW_ROW(map_page_draw, map_page_draw)
DRAW_ROW(map_draw, map_draw)
DRAW_ROW(map_keys, map_keys)
DRAW_ROW(map_icons, map_icons)
DRAW_ROW(map_terrain, map_terrain)
DRAW_ROW(icon_draw, icon_draw)
DRAW_ROW(radar_draw, radar_draw)
void es2_sim_service_radar_units(es2_sim_service_call *sc, void *user) {
    (void)user;
    sc->eax = radar_units(sc->slots[0]);
    sc->edx = (uint32_t)((int32_t)sc->slots[0] % 1000);
    sc->ecx = 1000u;
}
void es2_sim_service_map_icon(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (map_icon(&c, sc->slots[0], sc->slots[1], sc->slots[2])) hud_failed(sc);
}
void es2_sim_service_map_to_screen(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (map_to_screen(&c, sc->slots[0], sc->slots[1])) hud_failed(sc);
}
void es2_sim_service_wing_colour(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    sc->eax = wing_colour(&c, sc->slots[0]);
}
void es2_sim_service_map_icon_add(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (map_icon_add(&c, sc->slots[0], sc->slots[1])) hud_failed(sc);
}
void es2_sim_service_label_key(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (label_key(&c, sc->slots[0], sc->slots[1], sc->slots[2])) hud_failed(sc);
}
void es2_sim_service_map_clamp(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    map_clamp(&c, sc->slots[0]);
}
void es2_sim_service_map_context(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (map_context(&c, sc->slots[0], &sc->eax)) hud_failed(sc);
}
void es2_sim_service_icon_size(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    icon_size(&c, sc->slots[0], sc->slots[1]);
}
/* 0x480d44(O): hud_gc_select (src/hud_load.c); EAX O+4 (0x489558's
 * argument). */
void es2_sim_service_gc_select(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    hud_gc_select(&c, sc->slots[0]);
    sc->eax = sc->slots[0] + 4u;
}

/* 0x441744(G) (listing 0x441744..0x441877) and 0x441c3c(G) (listing
 * 0x441c3c..0x441d9c), the pod gauges' full draws: powered: the toggle
 * [G+0x18]; then unless busy, with the count label L = [G+0xca] visible
 * (0x441c3c: and the toggle not disabled): GC+0x22c <- a local record (0,
 * the unwritten dword: 0 here), alive until the end; a
 * jammed pod (byte G+0xc3): L placed by 0x438884 over (x0 + (0xb << sx),
 * y0, x1, y1) of G (flags 2), the toggle disabled (word +0x1b <- 2), text
 * [0x4d1344], colour [0x49b0d8], fill 0x2e; a loaded one (byte G+0xc2):
 * the name G+0xb1, colour [0x49b0dc], fill word 0x4d3b94; else the name,
 * [0x49b0d8], 0x2e; L <- colour, L+0x1d <- fill, the text (0x438920).
 * 0x441c3c then, not jammed, draws the charge bar [G+0xde] (slot 0) with
 * the word G+0xc6. */
static int pod_draw(hud_ctx *c, uint32_t g, int energy) {
    if (!hr8(c, g + 0x8cu)) return 0;
    if (wdraw(c, hr32(c, g + 0x18u))) return 1;
    if (busy(c, g)) return 0;
    int yes;
    if (visible(c, hr32(c, g + 0xcau) + 4u, &yes)) return 1;
    if (!yes) return 0;
    if (energy && hr16(c, hr32(c, g + 0x18u) + 0x1bu) == 2u) return 0;
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0x20u);
    if (!f) return hud_refuse(c, energy ? 0x00441c3cu : 0x00441744u, "pod draw", "the own code's call area is exhausted");
    hw32(c, hr32(c, VA_GC) + 0x22cu, f);
    uint32_t text = g + 0xb1u, colour, fill;
    int bad = 0;
    if (hr8(c, g + 0xc3u)) {
        hw32(c, f + 8u, 0);
        hw32(c, f + 0xcu, 0);
        hw32(c, f + 0x10u, hr32(c, g + 4u) + (0xbu << shx(c)));
        hw32(c, f + 0x14u, hr32(c, g + 8u));
        hw32(c, f + 0x18u, hr32(c, g + 0xcu));
        hw32(c, f + 0x1cu, hr32(c, g + 0x10u));
        bad = HCALL(c, 0x00438884u, NULL, hr32(c, g + 0xcau), f + 8u, f + 0x10u, 2u);
        hw16(c, hr32(c, g + 0x18u) + 0x1bu, 2);
        text = hr32(c, 0x004d1344u);
        colour = hr32(c, 0x0049b0d8u);
        fill = 0x2eu;
    } else if (hr8(c, g + 0xc2u)) {
        fill = (uint32_t)sx16(c, 0x004d3b94u);
        colour = hr32(c, 0x0049b0dcu);
    } else {
        fill = 0x2eu;
        colour = hr32(c, 0x0049b0d8u);
    }
    if (!bad) {
        uint32_t l = hr32(c, g + 0xcau);
        hw32(c, l, colour);
        hw32(c, l + 0x1du, fill);
        bad = label_draw(c, l, text);
    }
    /* The record GC+0x22c points at stays alive for the bar */
    if (!bad && energy && !hr8(c, g + 0xc3u)) {
        uint32_t bar = hr32(c, g + 0xdeu);
        bad = hud_call(c, hr32(c, hr32(c, bar)), (const uint32_t[]){bar, hr16(c, g + 0xc6u)}, 2, NULL);
    }
    hud_frame_pop(c, top);
    return bad;
}
void es2_sim_service_pod_draw(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (pod_draw(&c, sc->slots[0], 0)) hud_failed(sc);
}
void es2_sim_service_energy_pod_draw(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (pod_draw(&c, sc->slots[0], 1)) hud_failed(sc);
}
/* The pod gauges' record sets: 0x441970(G, rec) (listing
 * 0x441970..0x44199d) on a change of byte rec+1 (against G+0xc3),
 * 0x4419ec(G, rec) (0x4419ec..0x441a23) of byte rec+0 or +1, 0x441bac(G,
 * rec) (0x441bac..0x441c00) of byte +0, +1 or dword +4 (and, before the
 * sweep is done (byte G+0x8d = 0), G+0xe2 <- [rec+4]): G+0xc2 <- [rec],
 * G+0xc6 <- [rec+4], byte G+0xb0 <- 2. EAX G, EDX rec; ECX approximated
 * (its upper bytes are the caller's in the original; no caller reads it). */
static void pod_record(es2_sim_service_call *sc, int kind) {
    HSERVE(sc, c);
    uint32_t g = sc->slots[0], r = sc->slots[1];
    int changed;
    sc->eax = g;
    if (kind == 0) { sc->ecx = hr8(&c, g + 0xc3u); changed = hr8(&c, g + 0xc3u) != hr8(&c, r + 1u); }
    else if (kind == 1) { sc->ecx = hr8(&c, g + 0xc3u); changed = hr8(&c, g + 0xc2u) != hr8(&c, r) || hr8(&c, g + 0xc3u) != hr8(&c, r + 1u); if (hr8(&c, g + 0xc2u) != hr8(&c, r)) sc->ecx = hr8(&c, g + 0xc2u); }
    else {
        sc->ecx = hr32(&c, g + 0xc6u);
        changed = hr8(&c, g + 0xc2u) != hr8(&c, r) || hr8(&c, g + 0xc3u) != hr8(&c, r + 1u) || hr32(&c, g + 0xc6u) != hr32(&c, r + 4u);
        if (changed && !hr8(&c, g + 0x8du)) hw32(&c, g + 0xe2u, hr32(&c, r + 4u));
    }
    sc->edx = r;
    if (!changed) return;
    hw32(&c, g + 0xc2u, hr32(&c, r));
    sc->ecx = hr32(&c, r + 4u);
    hw32(&c, g + 0xc6u, sc->ecx);
    hw8(&c, g + 0xb0u, 2);
}
void es2_sim_service_pod_record_441970(es2_sim_service_call *sc, void *user) { (void)user; pod_record(sc, 0); }
void es2_sim_service_pod_record_4419ec(es2_sim_service_call *sc, void *user) { (void)user; pod_record(sc, 1); }
void es2_sim_service_pod_record_441bac(es2_sim_service_call *sc, void *user) { (void)user; pod_record(sc, 2); }
