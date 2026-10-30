/* The live leaves of the widget library (hud.md, "Widget-leaf verdicts" and
 * "the live widget leaves"), as own code. Every function names its listing
 * range of the reference DBSIM.EXE (llvm-objdump -d -M intel, SHA-256
 * 8561aa47...c4ad).
 *
 * The widget (base 0x4524a0): the rectangle x0, y0, x1, y1 at +0, the
 * round flag byte +0x10 with the centre words +0x11/+0x13 and the radius
 * word +0x15, the method table +0x17, the state word +0x1b (0 up, 1
 * pressed, 2 disabled, 3 the panel's off-row state), the kind word +0x1d
 * (1: the press goes to slot +0x18), byte +0x1f. The slider (0x4524d0)
 * adds the scale +0x20, the bounds words +0x24/+0x26, the position words
 * +0x28 (current) and +0x2a (previous) and the thumb word +0x2c. The root
 * (0x4526ec): its method table +0x17, a graphics context at +0x1b, the
 * dword +0x223, the drawn flag +0x227, a copy of the rectangle at +0x22b,
 * the item count word +0x254 and the items at +0x256.
 *
 * Kept: the clock read of 0x4520d8 at its site 0x45213d (hud.md, "the HUD line
 * contract — coupling set and frame writes"), the original's read and write
 * order, its 16-bit compares and its unused x of 0x434520. Every call of other
 * DBSIM code (the canvas 0x4311e0/ 0x431210, the panel copy 0x43122c, the panel
 * visibility 0x431410, the label draw 0x438920, the strcat 0x4967ea, delete
 * 0x477548, the RTTI count 0x496406 and every method-table slot) goes through
 * the dispatch; the graphics-context construction 0x480c34 and the layout scale
 * 0x438df8 are inline, as in src/hud_cockpit.c and src/hud_load.c. No pixels
 * are drawn here. */
#include "hud_internal.h"

#define VA_CANVAS_PUSH 0x004311e0u
#define VA_CANVAS_POP 0x00431210u
#define VA_PANEL_COPY 0x0043122cu
#define VA_PANEL_VISIBLE 0x00431410u
#define VA_LABEL_DRAW 0x00438920u
#define VA_HIT 0x004523b0u
#define VA_FIND 0x00452a28u
#define VA_ITEM_REDRAW 0x00452ab8u
#define VA_CLOCK_TICK 0x0045216cu
#define VA_WIDGET_INIT 0x004524a0u
#define VA_PANEL_GC 0x004d03a8u      /* the panel's graphics context: origin +0x220/+0x224 */
#define VA_DISPLAY 0x004cf99cu       /* the cockpit display record; byte +0x1c: redraw all */
#define VA_PRESSED 0x0049dbdcu       /* word: the pressed item's index, -1 none */
#define VA_GRABBED 0x0049dbdeu       /* byte */
#define VA_MOUSE_FLAGS 0x0049db6cu   /* byte; bit 1: release on any item */
#define VA_SCROLLED 0x0049dbecu      /* byte: the scroll offset applies */
#define VA_SCROLL_X 0x004d2556u
#define VA_SCROLL_Y 0x004d255au
#define VA_SCALE_X 0x004d253au       /* word: the layout's shift counts */
#define VA_SCALE_Y 0x004d253cu
#define VA_COLON 0x0049dac4u         /* byte: the clock's colon phase */
#define VA_HOURS 0x004d1db4u         /* two digits */
#define VA_MINUTES 0x004d1db7u       /* two digits */
#define VA_COLON_TEXT 0x004d1dbau
#define VA_TIME_TEXT 0x004d1dbcu
#define VA_TIME_SEPARATOR 0x0049db4eu

/* A call through slot `offset` of the method table at T+0x17. */
static int table_call(hud_ctx *c, uint32_t t, uint32_t offset, const uint32_t *slots, unsigned count, uint32_t *eax) {
    return hud_call(c, hr32(c, hr32(c, t + 0x17u) + offset), slots, count, eax);
}
#define TCALL(c, t, off, eax, ...) table_call((c), (t), (off), (const uint32_t[]){__VA_ARGS__}, \
    (unsigned)(sizeof((const uint32_t[]){__VA_ARGS__}) / sizeof(uint32_t)), (eax))
static int32_t s32(const hud_ctx *c, uint32_t a) { return (int32_t)hr32(c, a); }
static int16_t s16(const hud_ctx *c, uint32_t a) { return (int16_t)hr16(c, a); }
static uint32_t root_item(const hud_ctx *c, uint32_t r, int32_t i) { return hr32(c, r + 0x256u + 4u * (uint32_t)i); }

/* 0x434520(P, pt) (listing 0x434520..0x43459c): the canvas pushed; y =
 * pt.y + the panel GC's origin y (+0x224; x = pt.x + origin x is computed
 * and never used); for each of the [P+0x25a] items at P+0x246 (signed
 * count, re-read every pass) the state word +0x1b <- 0 when y lies in the
 * item's [y0, y1] (signed), else 3, then the item's slot 0. The canvas
 * popped; EAX its answer. */
static int panel_rows(hud_ctx *c, uint32_t p, uint32_t pt, uint32_t *eax) {
    if (HCALL0(c, VA_CANVAS_PUSH, NULL)) return 1;
    uint32_t gc = hr32(c, VA_PANEL_GC);
    if (!gc) return hud_null_refuse(c, 0x00434538u, "panel rows", "no panel graphics context (the original reads through NULL)");
    int32_t y = (int32_t)(hr32(c, pt + 4u) + hr32(c, gc + 0x224u));
    for (int32_t i = 0; i < s32(c, p + 0x25au); ++i) {
        uint32_t it = hr32(c, p + 0x246u + 4u * (uint32_t)i);
        int in = y >= s32(c, it + 4u) && y <= s32(c, it + 0xcu);
        hw16(c, it + 0x1bu, in ? 0 : 3);
        if (TCALL(c, it, 0, NULL, it)) return 1;
    }
    return HCALL0(c, VA_CANVAS_POP, eax);
}

/* 0x438e70(W, flags) (listing 0x438e70..0x438f10), the destructor of the
 * three-shape widget (exception-table reference 0x4594e4): counter - 1
 * first; W not NULL: W+0x17 <- 0x49debc, W+0x20 <- 0x49dee0, delete
 * [W+0x2c]; for each of the three shapes at W+0x30 that is not NULL the
 * counter + 0x496406(s, 0) and s's slot +0x18 (s, 3); flags bit 0: delete
 * W. The frame record of 0x49630c is not linked (FS:0 unchanged); EAX is
 * the restored FS:0. */
static int three_shape_destroy(hud_ctx *c, uint32_t w, uint32_t flags) {
    hud_counter(c, -1);
    if (!w) return 0;
    hw32(c, w + 0x17u, 0x0049debcu);
    hw32(c, w + 0x20u, 0x0049dee0u);
    if (HCALL(c, HUD_VA_DELETE, NULL, hr32(c, w + 0x2cu))) return 1;
    for (uint32_t i = 0; i < 3u; ++i) {
        uint32_t s = hr32(c, w + 0x30u + 4u * i), n = 0;
        if (!s) continue;
        if (HCALL(c, HUD_VA_RTTI_COUNT, &n, s, 0)) return 1;
        hud_counter(c, (int32_t)n);
        if (HVCALL(c, s, 0x18u, NULL, s, 3)) return 1;
    }
    if (flags & 1u) return HCALL(c, HUD_VA_DELETE, NULL, w);
    return 0;
}

/* 0x4520d8(W) (listing 0x4520d8..0x452168), the mission clock's slot +4
 * (0x49db64): the display's redraw byte (+0x1c) set: W+0xb0 <- 2; W+0xb0
 * not 0: the box {[[W+0xb1]+4], [+8], [[W+0xb5]+0xc], [+0x10]} copied
 * (0x43122c(0, box, 0)), W+0xb0 <- 0. The clock (site 0x45213d) above the
 * next tick [W+0xbd] (unsigned): 0x45216c(W), [W+0xbd] <- now + 30, W+0xb0
 * <- 1. EAX the clock's or the tick's answer. */
static int clock_frame(hud_ctx *c, uint32_t w, uint32_t *eax) {
    uint32_t d = hr32(c, VA_DISPLAY);
    if (!d) return hud_null_refuse(c, 0x004520e5u, "clock frame", "no display record (the original reads through NULL)");
    if (hr8(c, d + 0x1cu)) hw8(c, w + 0xb0u, 2);
    if (hr8(c, w + 0xb0u)) {
        uint32_t top = c->l->stack_hi, box = hud_frame_push(c, 16u);
        if (!box) return hud_refuse(c, 0x004520d8u, "clock frame", "the own code's call area is exhausted");
        uint32_t a = hr32(c, w + 0xb1u) + 4u, b = hr32(c, w + 0xb5u) + 0xcu;
        hw32(c, box, hr32(c, a));
        hw32(c, box + 4u, hr32(c, a + 4u));
        hw32(c, box + 8u, hr32(c, b));
        hw32(c, box + 0xcu, hr32(c, b + 4u));
        int bad = HCALL(c, VA_PANEL_COPY, NULL, 0, box, 0);
        hud_frame_pop(c, top);
        if (bad) return 1;
        hw8(c, w + 0xb0u, 0);
    }
    uint32_t now = 0;
    if (hud_clock(c, 0x0045213du, &now)) return 1;
    *eax = now;
    if (now > hr32(c, w + 0xbdu)) {
        if (HCALL(c, VA_CLOCK_TICK, eax, w)) return 1;
        hw32(c, w + 0xbdu, now + 0x1eu);
        hw8(c, w + 0xb0u, 1);
    }
    return 0;
}
/* A digit byte incremented in place; its new value (signed). */
static int32_t digit_up(hud_ctx *c, uint32_t a) {
    hw8(c, a, (uint8_t)(hr8(c, a) + 1u));
    return (int32_t)(int8_t)hr8(c, a);
}
/* 0x45216c(W) (listing 0x45216c..0x45223c), the clock's tick: the colon
 * byte toggled; at 1 a minute is counted on the digits mm (0x4d1db7) and
 * hh (0x4d1db4) with signed compares (units above '9' carry, minute tens
 * above '5' carry into the hours, hours above "99" wrap to "00:00" when
 * the tens are '9'). Then 0x431410([W+0xb1]+4); AL set: the canvas pushed,
 * the labels [W+0xb1] <- hh and [W+0xb5] <- mm (0x438920), with the colon
 * byte set also [W+0xb9] <- 0x4d1dba, the canvas popped. EAX the
 * visibility's or the pop's answer. */
static int clock_tick(hud_ctx *c, uint32_t w, uint32_t *eax) {
    hw8(c, VA_COLON, (uint8_t)(hr8(c, VA_COLON) ^ 1u));
    if (hr8(c, VA_COLON) == 1u && digit_up(c, VA_MINUTES + 1u) > 0x39) {
        hw8(c, VA_MINUTES + 1u, 0x30);
        if (digit_up(c, VA_MINUTES) > 0x35) {
            hw8(c, VA_MINUTES, 0x30);
            if (digit_up(c, VA_HOURS + 1u) > 0x39 && (int8_t)hr8(c, VA_HOURS) == 0x39) {
                hw8(c, VA_MINUTES + 1u, 0x30);
                hw8(c, VA_MINUTES, 0x30);
                hw8(c, VA_HOURS + 1u, 0x30);
                hw8(c, VA_HOURS, 0x30);
            } else if ((int8_t)hr8(c, VA_HOURS + 1u) > 0x39) {
                hw8(c, VA_HOURS + 1u, 0x30);
                hw8(c, VA_HOURS, (uint8_t)(hr8(c, VA_HOURS) + 1u));
            }
        }
    }
    if (HCALL(c, VA_PANEL_VISIBLE, eax, hr32(c, w + 0xb1u) + 4u)) return 1;
    if (!(*eax & 0xffu)) return 0;
    if (HCALL0(c, VA_CANVAS_PUSH, NULL)) return 1;
    if (HCALL(c, VA_LABEL_DRAW, NULL, hr32(c, w + 0xb1u), VA_HOURS)) return 1;
    if (HCALL(c, VA_LABEL_DRAW, NULL, hr32(c, w + 0xb5u), VA_MINUTES)) return 1;
    if (hr8(c, VA_COLON) && HCALL(c, VA_LABEL_DRAW, NULL, hr32(c, w + 0xb9u), VA_COLON_TEXT)) return 1;
    return HCALL0(c, VA_CANVAS_POP, eax);
}
/* 0x452240() (listing 0x452240..0x452298): the time text: the inline
 * strcpy (length taken first, then the bytes copied forward) of hh to
 * 0x4d1dbc, then strcat the separator 0x49db4e and mm. EAX 0x4d1dbc. */
static int time_text(hud_ctx *c) {
    uint32_t n = 0;
    while (hr8(c, VA_HOURS + n)) ++n;
    for (uint32_t i = 0; i <= n; ++i) hw8(c, VA_TIME_TEXT + i, hr8(c, VA_HOURS + i));
    if (HCALL(c, HUD_VA_STRCAT, NULL, VA_TIME_TEXT, VA_TIME_SEPARATOR)) return 1;
    return HCALL(c, HUD_VA_STRCAT, NULL, VA_TIME_TEXT, VA_MINUTES);
}
/* 0x45229c(r) (listing 0x45229c..0x4522ee, with 0x438df8 at
 * 0x438df8..0x438e28 inline): the five points of r (x at +0, +8, ..., +0x20;
 * y at +4, ..., +0x24) shifted left by the words [0x4d253a] (x) and
 * [0x4d253c] (y), modulo 32 as the shl by CL does, in the order x, y per
 * point. EAX r + 0x20; ECX r's high half with the y count in CX. */
static void layout_scale4(hud_ctx *c, uint32_t r) {
    for (uint32_t k = 0; k < 10u; ++k) {
        uint32_t a = r + 4u * k, n = hr16(c, (k & 1u) ? VA_SCALE_Y : VA_SCALE_X) & 31u;
        hw32(c, a, hr32(c, a) << n);
    }
}
/* 0x4523b0(it, pt) (listing 0x4523b0..0x452431): the hit test. Square
 * (byte +0x10 clear): pt.y in [y0, y1] then pt.x in [x0, x1], signed; EAX
 * 1 or 0. Round: |pt.x - cx| + |pt.y - cy| (centre words +0x11/+0x13, sign
 * extended) not above the radius word +0x15; EAX 1 or 0 (setle). */
static uint32_t hit(hud_ctx *c, uint32_t it, uint32_t pt) {
    int32_t x = s32(c, pt), y = s32(c, pt + 4u);
    if (!hr8(c, it + 0x10u))
        return y >= s32(c, it + 4u) && y <= s32(c, it + 0xcu) && x >= s32(c, it) && x <= s32(c, it + 8u);
    uint32_t dx = (uint32_t)x - (uint32_t)(int32_t)s16(c, it + 0x11u), dy = (uint32_t)y - (uint32_t)(int32_t)s16(c, it + 0x13u);
    if ((int32_t)dx < 0) dx = 0u - dx;
    if ((int32_t)dy < 0) dy = 0u - dy;
    return (int32_t)(dx + dy) <= (int32_t)s16(c, it + 0x15u);
}
/* 0x452454(r, pt) (listing 0x452454..0x45249e): r moved to pt keeping its
 * size: w, h taken; (x1, y1) <- pt, (x0, y0) <- (x1, y1), then x1 += w,
 * y1 += h. EAX r + 8, EDX w, ECX h. */
static void rect_place(hud_ctx *c, uint32_t r, uint32_t pt, es2_sim_service_call *sc) {
    uint32_t w = hr32(c, r + 8u) - hr32(c, r), h = hr32(c, r + 0xcu) - hr32(c, r + 4u);
    hw32(c, r + 8u, hr32(c, pt));
    hw32(c, r + 0xcu, hr32(c, pt + 4u));
    hw32(c, r, hr32(c, r + 8u));
    hw32(c, r + 4u, hr32(c, r + 0xcu));
    hw32(c, r + 8u, hr32(c, r + 8u) + w);
    hw32(c, r + 0xcu, hr32(c, r + 0xcu) + h);
    sc->eax = r + 8u;
    sc->edx = w;
    sc->ecx = h;
}
/* 0x4524a0(W, rect) (listing 0x4524a0..0x4524cf): the widget base: byte
 * +0x10 <- 0, +0x17 <- 0x49dbb6, the 16 bytes of rect to W, byte +0x1f <-
 * 0. EAX W, ECX rect's x1, EDX rect's y1. */
static void widget_init(hud_ctx *c, uint32_t w, uint32_t rect, es2_sim_service_call *sc) {
    hw8(c, w + 0x10u, 0);
    hw32(c, w + 0x17u, 0x0049dbb6u);
    for (uint32_t i = 0; i < 16u; i += 4u) hw32(c, w + i, hr32(c, rect + i));
    hw8(c, w + 0x1fu, 0);
    sc->eax = w;
    sc->ecx = hr32(c, rect + 8u);
    sc->edx = hr32(c, rect + 0xcu);
}
/* 0x4524d0(W, rect, lo, hi, v, step) (listing 0x4524d0..0x45251e): the
 * slider: 0x4524a0(W, rect); +0x17 <- 0x49db8a, +0x20 <- 0; the words
 * +0x24 lo, +0x26 hi, +0x2a v, +0x28 v, +0x2c step; the kind word +0x1d
 * <- 1. EAX W, ECX the base's ECX with lo in CX, EDX v (16 bits). */
static int slider_init(hud_ctx *c, es2_sim_service_call *sc) {
    uint32_t w = sc->slots[0];
    es2_sim_result r;
    if (hud_call_regs(c, VA_WIDGET_INIT, (const uint32_t[]){w, sc->slots[1]}, 2, NULL, &r)) return 1;
    hw32(c, w + 0x17u, 0x0049db8au);
    hw32(c, w + 0x20u, 0);
    hw16(c, w + 0x24u, (uint16_t)sc->slots[2]);
    hw16(c, w + 0x26u, (uint16_t)sc->slots[3]);
    hw16(c, w + 0x2au, (uint16_t)sc->slots[4]);
    hw16(c, w + 0x28u, (uint16_t)sc->slots[4]);
    hw16(c, w + 0x2cu, (uint16_t)sc->slots[5]);
    hw16(c, w + 0x1du, 1);
    sc->eax = w;
    sc->ecx = (r.ecx & 0xffff0000u) | (sc->slots[2] & 0xffffu);
    sc->edx = sc->slots[4] & 0xffffu;
    return 0;
}
/* 0x452520(W, pt) (listing 0x452520..0x452569), the horizontal sliders'
 * slot 0: the previous position +0x2a <- +0x28; then +0x28 <- x0 when
 * pt.x is left of x0, x1 - thumb when x1 - thumb (signed) is left of
 * pt.x, else pt.x (16 bits). EAX W. */
static void slider_h(hud_ctx *c, uint32_t w, uint32_t pt) {
    hw16(c, w + 0x2au, hr16(c, w + 0x28u));
    int32_t x = s32(c, pt);
    if (x < s32(c, w)) hw16(c, w + 0x28u, hr16(c, w));
    else if ((int32_t)(hr32(c, w + 8u) - (uint32_t)(int32_t)s16(c, w + 0x2cu)) < x)
        hw16(c, w + 0x28u, (uint16_t)(hr16(c, w + 8u) - hr16(c, w + 0x2cu)));
    else hw16(c, w + 0x28u, (uint16_t)x);
}
/* 0x452600(W, pt) (listing 0x452600..0x45264c), the vertical sliders'
 * slot 0: +0x2a <- +0x28; then +0x28 <- y0 + thumb when that (signed) is
 * greater than pt.y, y1 when pt.y is greater than y1, else pt.y (16
 * bits). EAX W. */
static void slider_v(hud_ctx *c, uint32_t w, uint32_t pt) {
    hw16(c, w + 0x2au, hr16(c, w + 0x28u));
    int32_t y = s32(c, pt + 4u);
    if ((int32_t)((uint32_t)(int32_t)s16(c, w + 0x2cu) + hr32(c, w + 4u)) > y)
        hw16(c, w + 0x28u, (uint16_t)(hr16(c, w + 4u) + hr16(c, w + 0x2cu)));
    else if (y > s32(c, w + 0xcu)) hw16(c, w + 0x28u, hr16(c, w + 0xcu));
    else hw16(c, w + 0x28u, (uint16_t)y);
}
/* 0x4526ec(W, rect) (listing 0x4526ec..0x45275a): the root: byte +0x10 <-
 * 0, +0x17 <- 0x49db7a, the graphics context 0x480c34(W+0x1b) (inline),
 * the 16 bytes of rect to W and W+0x22b, +0x223 <- 1, the count word
 * +0x254 <- 0, counter + 1. EAX W, ECX W+0x1b, EDX the counter's
 * address. */
static int root_init(hud_ctx *c, uint32_t w, uint32_t rect, es2_sim_service_call *sc) {
    hw8(c, w + 0x10u, 0);
    hw32(c, w + 0x17u, 0x0049db7au);
    if (hud_gc_construct(c, w + 0x1bu)) return 1;
    for (uint32_t i = 0; i < 16u; i += 4u) {
        uint32_t v = hr32(c, rect + i);
        hw32(c, w + i, v);
        hw32(c, w + 0x22bu + i, v);
    }
    hw32(c, w + 0x223u, 1);
    hw16(c, w + 0x254u, 0);
    hud_counter(c, 1);
    sc->eax = w;
    sc->ecx = w + 0x1bu;
    sc->edx = hr32(c, HUD_VA_COUNTER_POINTER);
    return 0;
}
/* The pointer made local to the root R: pt - (x0, y0), and with the scroll
 * byte 0x49dbec set minus (0 - [0x4d2556], 0 - [0x4d255a]) as well. */
static void root_local(hud_ctx *c, uint32_t r, uint32_t pt, uint32_t local) {
    hw32(c, local, hr32(c, pt) - hr32(c, r));
    hw32(c, local + 4u, hr32(c, pt + 4u) - hr32(c, r + 4u));
    if (!hr8(c, VA_SCROLLED)) return;
    hw32(c, local, hr32(c, local) - (0u - hr32(c, VA_SCROLL_X)));
    hw32(c, local + 4u, hr32(c, local + 4u) - (0u - hr32(c, VA_SCROLL_Y)));
}
/* 0x4527c8(R, pt) (listing 0x4527c8..0x452897), the press: i =
 * 0x452a28(R, local) to the word 0x49dbdc; not -1 (16 bits): the item's
 * state word <- 1; its kind word 1: its slot +0x18 (it, local) and byte
 * 0x49dbde <- 1; then 0x452ab8(R, item) (the index re-read after each
 * call). EAX the find's or the redraw's answer. */
static int root_press(hud_ctx *c, uint32_t r, uint32_t pt, uint32_t *eax) {
    uint32_t top = c->l->stack_hi, local = hud_frame_push(c, 8u);
    if (!local) return hud_refuse(c, 0x004527c8u, "root press", "the own code's call area is exhausted");
    root_local(c, r, pt, local);
    int bad = HCALL(c, VA_FIND, eax, r, local);
    if (!bad) {
        hw16(c, VA_PRESSED, (uint16_t)*eax);
        if ((uint16_t)*eax != 0xffffu) {
            hw16(c, root_item(c, r, (int16_t)*eax) + 0x1bu, 1);
            uint32_t it = root_item(c, r, s16(c, VA_PRESSED));
            if (hr16(c, it + 0x1du) == 1u) {
                bad = TCALL(c, it, 0x18u, NULL, it, local);
                if (!bad) hw8(c, VA_GRABBED, 1);
            }
            if (!bad) bad = HCALL(c, VA_ITEM_REDRAW, eax, r, root_item(c, r, s16(c, VA_PRESSED)));
        }
    }
    hud_frame_pop(c, top);
    return bad;
}
/* 0x452898(R, pt) (listing 0x452898..0x45297b), the release: the old
 * index taken, the new one 0x452a28(R, local) to 0x49dbdc; not -1: when
 * it equals the old one or bit 1 of 0x49db6c is set, the item's slot +0x10
 * (its value) and slot +8 (it, value sign-extended from 16 bits); then its
 * state word <- 0, 0x452ab8(R, item), 0x49dbdc <- -1. EAX the find's or
 * the redraw's answer. */
static int root_release(hud_ctx *c, uint32_t r, uint32_t pt, uint32_t *eax) {
    uint16_t old = hr16(c, VA_PRESSED);
    uint32_t top = c->l->stack_hi, local = hud_frame_push(c, 8u);
    if (!local) return hud_refuse(c, 0x00452898u, "root release", "the own code's call area is exhausted");
    root_local(c, r, pt, local);
    int bad = HCALL(c, VA_FIND, eax, r, local);
    hud_frame_pop(c, top);
    if (bad) return 1;
    hw16(c, VA_PRESSED, (uint16_t)*eax);
    if ((uint16_t)*eax == 0xffffu) return 0;
    uint16_t now = hr16(c, VA_PRESSED);
    if (old == now || (hr8(c, VA_MOUSE_FLAGS) & 2u)) {
        uint32_t it = root_item(c, r, (int16_t)now), v = 0;
        if (TCALL(c, it, 0x10u, &v, it)) return 1;
        if (TCALL(c, it, 8u, NULL, it, hsx16((uint16_t)v))) return 1;
    }
    hw16(c, root_item(c, r, s16(c, VA_PRESSED)) + 0x1bu, 0);
    if (HCALL(c, VA_ITEM_REDRAW, eax, r, root_item(c, r, s16(c, VA_PRESSED)))) return 1;
    hw16(c, VA_PRESSED, 0xffffu);
    return 0;
}
/* 0x45297c(R, pt) (listing 0x45297c..0x452a27), the move while pressed
 * (0x49dbdc not -1): the find 0x452a28(R, pt) on the unlocalised point;
 * on the pressed item with state 0: state 1 and 0x452ab8; off it with the
 * pressed item's state 1: state 0 and 0x452ab8. EAX the entry EAX, the
 * find's answer, the pressed item or the redraw's answer. */
static int root_move(hud_ctx *c, uint32_t r, uint32_t pt, uint32_t *eax) {
    if (hr16(c, VA_PRESSED) == 0xffffu) return 0;
    if (HCALL(c, VA_FIND, eax, r, pt)) return 1;
    uint16_t pressed = hr16(c, VA_PRESSED);
    if ((uint16_t)*eax == pressed) {
        if (hr16(c, root_item(c, r, (int16_t)pressed) + 0x1bu) != 0u) return 0;
        hw16(c, root_item(c, r, s16(c, VA_PRESSED)) + 0x1bu, 1);
    } else {
        uint32_t it = root_item(c, r, s16(c, VA_PRESSED));
        *eax = it;
        if (hr16(c, it + 0x1bu) != 1u) return 0;
        hw16(c, it + 0x1bu, 0);
    }
    return HCALL(c, VA_ITEM_REDRAW, eax, r, root_item(c, r, s16(c, VA_PRESSED)));
}
/* 0x452a28(R, pt) (listing 0x452a28..0x452a6f): the index of the first
 * item (16-bit signed count +0x254) that the hit test 0x4523b0 answers
 * with AL set and whose state word is not 2; else -1. */
static int root_find(hud_ctx *c, uint32_t r, uint32_t pt, uint32_t *eax) {
    for (uint32_t i = 0; (int16_t)i < s16(c, r + 0x254u); ++i) {
        uint32_t it = hr32(c, r + 0x256u + 4u * i), h = 0;
        if (HCALL(c, VA_HIT, &h, it, pt)) return 1;
        if ((h & 0xffu) && hr16(c, hr32(c, r + 0x256u + 4u * i) + 0x1bu) != 2u) { *eax = i; return 0; }
    }
    *eax = 0xffffffffu;
    return 0;
}
/* 0x452a70(R) (listing 0x452a70..0x452ab6), the full draw: +0x227 <- 0,
 * R's slot 0, +0x227 <- 1, each item's slot 0. EAX the last slot's
 * answer. */
static int root_draw(hud_ctx *c, uint32_t r, uint32_t *eax) {
    hw32(c, r + 0x227u, 0);
    if (TCALL(c, r, 0, eax, r)) return 1;
    hw32(c, r + 0x227u, 1);
    for (uint32_t i = 0; (int16_t)i < s16(c, r + 0x254u); ++i) {
        uint32_t it = hr32(c, r + 0x256u + 4u * i);
        if (TCALL(c, it, 0, eax, it)) return 1;
    }
    return 0;
}

/* ---- the service rows */
#define ROW(name) void es2_sim_service_##name(es2_sim_service_call *sc, void *user)
#define ROW_BEGIN (void)user; HSERVE(sc, c); uint32_t eax = sc->eax
#define ROW_END(expr) do { if (expr) { hud_failed(sc); return; } sc->eax = eax; } while (0)

ROW(widget_panel_rows) { ROW_BEGIN; ROW_END(panel_rows(&c, sc->slots[0], sc->slots[1], &eax)); }
ROW(widget_three_shape_destroy) {
    ROW_BEGIN;
    eax = hud_fs0();
    ROW_END(three_shape_destroy(&c, sc->slots[0], sc->slots[1]));
}
/* 0x4390b8(x) (listing 0x4390b8..0x4390c2): EAX x + 4. */
ROW(widget_plus4) { ROW_BEGIN; eax = sc->slots[0] + 4u; ROW_END(0); }
/* 0x4520c8(W) (listing 0x4520c8..0x4520d6), the mission clock's slot 0
 * (0x49db60): 0x45216c(W). */
ROW(widget_clock_draw) { ROW_BEGIN; ROW_END(HCALL(&c, VA_CLOCK_TICK, &eax, sc->slots[0])); }
ROW(widget_clock_frame) { ROW_BEGIN; ROW_END(clock_frame(&c, sc->slots[0], &eax)); }
ROW(widget_clock_tick) { ROW_BEGIN; ROW_END(clock_tick(&c, sc->slots[0], &eax)); }
ROW(widget_time_text) {
    ROW_BEGIN;
    eax = VA_TIME_TEXT;
    ROW_END(time_text(&c));
}
ROW(widget_layout_scale4) {
    ROW_BEGIN;
    uint32_t r = sc->slots[0];
    layout_scale4(&c, r);
    eax = r + 0x20u;
    sc->ecx = (r & 0xffff0000u) | hr16(&c, VA_SCALE_Y);
    ROW_END(0);
}
ROW(widget_hit) { ROW_BEGIN; eax = hit(&c, sc->slots[0], sc->slots[1]); ROW_END(0); }
ROW(widget_rect_place) { (void)user; HSERVE(sc, c); rect_place(&c, sc->slots[0], sc->slots[1], sc); }
ROW(widget_init) { (void)user; HSERVE(sc, c); widget_init(&c, sc->slots[0], sc->slots[1], sc); }
ROW(widget_slider_init) { (void)user; HSERVE(sc, c); if (slider_init(&c, sc)) hud_failed(sc); }
ROW(widget_slider_h) { ROW_BEGIN; slider_h(&c, sc->slots[0], sc->slots[1]); eax = sc->slots[0]; ROW_END(0); }
ROW(widget_slider_v) { ROW_BEGIN; slider_v(&c, sc->slots[0], sc->slots[1]); eax = sc->slots[0]; ROW_END(0); }
ROW(widget_root_init) { (void)user; HSERVE(sc, c); if (root_init(&c, sc->slots[0], sc->slots[1], sc)) hud_failed(sc); }
ROW(widget_root_press) { ROW_BEGIN; ROW_END(root_press(&c, sc->slots[0], sc->slots[1], &eax)); }
ROW(widget_root_release) { ROW_BEGIN; ROW_END(root_release(&c, sc->slots[0], sc->slots[1], &eax)); }
ROW(widget_root_move) { ROW_BEGIN; ROW_END(root_move(&c, sc->slots[0], sc->slots[1], &eax)); }
ROW(widget_root_find) { ROW_BEGIN; ROW_END(root_find(&c, sc->slots[0], sc->slots[1], &eax)); }
ROW(widget_root_draw) { ROW_BEGIN; ROW_END(root_draw(&c, sc->slots[0], &eax)); }
/* 0x452ab8(R, it) (listing 0x452ab8..0x452ad4), the item redraw: R+0x227
 * <- 1, the item's slot +4. EAX its answer. */
ROW(widget_item_redraw) {
    ROW_BEGIN;
    hw32(&c, sc->slots[0] + 0x227u, 1);
    ROW_END(TCALL(&c, sc->slots[1], 4u, &eax, sc->slots[1]));
}
/* 0x452c6c(R, W) (listing 0x452c6c..0x452c8b), the item add: R+0x256 +
 * 4 * count (sign-extended word +0x254) <- W, the count + 1. EAX R, EDX the
 * old count, ECX W. */
ROW(widget_root_add) {
    ROW_BEGIN;
    uint32_t r = sc->slots[0], n = hsx16(hr16(&c, r + 0x254u));
    hw32(&c, r + 0x256u + 4u * n, sc->slots[1]);
    hw16(&c, r + 0x254u, (uint16_t)(hr16(&c, r + 0x254u) + 1u));
    eax = r;
    sc->edx = n;
    sc->ecx = sc->slots[1];
    ROW_END(0);
}
