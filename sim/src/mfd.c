/* The MFD and HDD frames with their page dispatch as own code: the MFD frame
 * 0x446350 (MFDGauge vtable 0x49cfa0 slot +8) with its helpers (the damage
 * alert 0x4342e0 and 0x446ddc, the automatic page switch 0x44718c with
 * 0x434310/0x440a3c/0x440a64, the animation start/step/state
 * 0x471e10/0x471e88/0x471f24, the rotated bitmap 0x45caa4); the HDD frame
 * 0x449bf8 (HDDGauge vtable 0x49d64c slot +8) with its page frame call
 * 0x449f64, the title 0x44a704, the button record 0x44a174, the map page's edge
 * scroll 0x44cff8/0x44d05c and the HDD status page's draw/frame wrappers
 * 0x450b54/0x450b74; the MFD pages' frames: status 0x43b238, target 0x440314,
 * and the orders page complete (draw 0x43f7cc, frame 0x43f8a0, the order marker
 * 0x43fa5c). Contract: hud.md, "the MFD and HDD frames and pages as own code";
 * every function names its listing range of the reference DBSIM.EXE
 * (llvm-objdump -d -M intel, SHA-256 8561aa47...c4ad).
 *
 * Every guest read and write goes through the bound HUD's guest view;
 * every call of other code (the pages' and widgets' slots, the label
 * 0x438920, the blits, the panel copy, the clock, the registry hooks
 * panel_visible 0x431410 and the map frame 0x44c988, the radio panel
 * 0x44b620; the HDD button executor 0x44a1a0
 * and its callees are the block at the end) goes through
 * the dispatch. The stack locals the original hands to
 * callees live in a frame of the call area at the listing's offsets. */
#include "hud_internal.h"

#define VA_GC 0x006c5f70u
#define VA_PANEL 0x0049b088u
#define VA_DISPLAY 0x004cf99cu       /* the cockpit display: +0x14 its mode, byte +0x1c */
#define VA_DESC 0x004d24bcu          /* the screen descriptor of the bitmap calls */
#define VA_SHIFT_X 0x004d253au
#define VA_SHIFT_Y 0x004d253cu
#define VA_HUD_BITMAPS 0x0049cbccu   /* -> +8 the MFD bitmap table */
#define VA_DISPLAY_CURRENT 0x00429820u
#define VA_VISIBLE 0x00431410u       /* panel_visible (hook) */
#define VA_PANEL_COPY 0x0043122cu
#define VA_PORT_A 0x00433158u
#define VA_LABEL 0x00438920u
#define VA_LABEL_KEY 0x00438aacu
#define VA_GAUGE_BLINK 0x00438bc0u
#define VA_PART_CONDITION 0x0041b514u
#define VA_DAMAGE_CLASS 0x00438700u
#define VA_BLIT_AT 0x0045c8bcu
#define VA_ROTATED_BLIT 0x0048185cu
#define VA_RECT_FILL 0x00486704u
#define VA_BITMAP_ARRAY_DESTROY 0x0048349cu
#define VA_MFD_OBJECT 0x00445234u
#define VA_MFD_PAGE 0x00446e60u
#define VA_DAMAGE_ALERT 0x004342e0u
#define VA_ALERT_SET 0x00446ddcu
#define VA_AUTO_PAGE 0x0044718cu
#define VA_SELECTED_ENTRY 0x00434310u
#define VA_ENTRY_ROW 0x00440a3cu
#define VA_ENTRY_COLUMN 0x00440a64u
#define VA_ANIM_START 0x00471e10u
#define VA_ANIM_STEP 0x00471e88u
#define VA_ANIM_STATE 0x00471f24u
#define VA_ROTATED_AT 0x0045caa4u
#define VA_HDD_EXECUTE 0x0044a1a0u
#define VA_HDD_RADIO 0x0044b620u
#define VA_HDD_PAGE_DRAW 0x00449ee4u
#define VA_HDD_PAGE_FRAME 0x00449f64u
#define VA_HDD_TITLE 0x0044a704u
#define VA_MAP_SCROLL_LEFT 0x0044cff8u
#define VA_MAP_SCROLL_RIGHT 0x0044d05cu
#define VA_HDD_STATUS 0x00450c7cu
#define VA_ORDERS_MARK 0x0043fa5cu

static void gc_save(hud_ctx *c, uint32_t gc, uint32_t to) {
    for (uint32_t i = 0; i < 0x81u; ++i) hw32(c, to + 4u * i, hr32(c, gc + 4u + 4u * i));
}
static void gc_load(hud_ctx *c, uint32_t gc, uint32_t from) {
    for (uint32_t i = 0; i < 0x81u; ++i) hw32(c, gc + 4u + 4u * i, hr32(c, from + 4u * i));
}
/* The widget's draw: slot +0xc of the table at W+0x17. */
static int widget_draw(hud_ctx *c, uint32_t w) { return hud_call(c, hr32(c, hr32(c, w + 0x17u) + 0xcu), (const uint32_t[]){w}, 1, NULL); }
static int vslot(hud_ctx *c, uint32_t o, uint32_t offset) { return hud_call(c, hr32(c, hr32(c, o) + offset), (const uint32_t[]){o}, 1, NULL); }
static int visible(hud_ctx *c, uint32_t rect, int *yes) {
    uint32_t al = 0;
    if (HCALL(c, VA_VISIBLE, &al, rect)) return 1;
    *yes = (al & 0xffu) != 0;
    return 0;
}
static void dec8(hud_ctx *c, uint32_t a) { hw8(c, a, (uint8_t)(hr8(c, a) - 1u)); }
static uint32_t shift_x(const hud_ctx *c) { return hr16(c, VA_SHIFT_X) & 31u; }
static uint32_t shift_y(const hud_ctx *c) { return hr16(c, VA_SHIFT_Y) & 31u; }
static int no_frame(hud_ctx *c, uint32_t va, const char *name) { return hud_refuse(c, va, name, "the own code's call area is exhausted"); }

/* ---- the MFD helpers */

/* 0x4342e0(n) (listing 0x4342e0..0x43430e): damage_class 0x438700 of the
 * sign-extended word part_condition 0x41b514([[0x49b088]+0x203], 0, n)
 * returns. The third argument is pushed as a dword whose low word is n and
 * whose high word is the entry EAX's (the original loads only AX).
 * EAX: the damage class. */
static int damage_alert(hud_ctx *c, uint32_t n, uint32_t entry_eax, uint32_t *eax) {
    uint32_t r = 0;
    if (HCALL(c, VA_PART_CONDITION, &r, hr32(c, hr32(c, VA_PANEL) + 0x203u), 0u, (entry_eax & 0xffff0000u) | (n & 0xffffu))) return 1;
    return HCALL(c, VA_DAMAGE_CLASS, eax, hsx16((uint16_t)r));
}

/* 0x446ddc(G) (listing 0x446ddc..0x446e3b): the alert set e of class
 * word G+0x74 (e = word [0x49cb34 + 2 class]): G+0x6c <- [0x4d1c88 + 4e]
 * (an animation), G+0x7c/+0x80/+0x84/+0x88 <- the words of 0x49cb94,
 * 0x49cba0, 0x49cbac, 0x49cbb8 at e (sign-extended), word G+0x70 <- 2e,
 * word G+0x72 <- 2e + 1. EAX: G. */
static void alert_set(hud_ctx *c, uint32_t g) {
    uint32_t e = hsx16(hr16(c, 0x0049cb34u + 2u * hsx16(hr16(c, g + 0x74u))));
    hw32(c, g + 0x6cu, hr32(c, 0x004d1c88u + 4u * e));
    hw32(c, g + 0x7cu, hsx16(hr16(c, 0x0049cb94u + 2u * e)));
    hw32(c, g + 0x80u, hsx16(hr16(c, 0x0049cba0u + 2u * e)));
    hw32(c, g + 0x84u, hsx16(hr16(c, 0x0049cbacu + 2u * e)));
    hw32(c, g + 0x88u, hsx16(hr16(c, 0x0049cbb8u + 2u * e)));
    hw16(c, g + 0x70u, (uint16_t)(2u * e));
    hw16(c, g + 0x72u, (uint16_t)(2u * e + 1u));
}

/* 0x434310(H) (listing 0x434310..0x434366): with X = [[H+0x203]+0x202]
 * and the signed byte i = X+0x1d: the key k = [[X+4] + 4i] for i >= 0,
 * else 0; the first non-null entry of H+0x70 (the [H+0x98] dwords) whose
 * +0xbd is k, else 0. */
static uint32_t selected_entry(hud_ctx *c, uint32_t h) {
    uint32_t x = hr32(c, hr32(c, h + 0x203u) + 0x202u), key = 0;
    int32_t i = (int8_t)hr8(c, x + 0x1du);
    if (i + 1 > 0) key = hr32(c, hr32(c, x + 4u) + 4u * (uint32_t)i);
    for (int32_t n = 0; n < (int32_t)hr32(c, h + 0x98u); ++n) {
        uint32_t e = hr32(c, h + 0x70u + 4u * (uint32_t)n);
        if (e && key == hr32(c, e + 0xbdu)) return e;
    }
    return 0;
}

/* 0x440a3c(E) / 0x440a64(E) (listings 0x440a3c..0x440a63 and
 * 0x440a64..0x440a8c): with D = [E+0xbd] and its cell pointer Q = [D+0x20]
 * both set, Q's word +0 (+2 for 0x440a64) loaded into AX over Q (the high
 * word of EAX stays Q's); else -1. */
static uint32_t entry_word(hud_ctx *c, uint32_t e, uint32_t offset) {
    uint32_t d = hr32(c, e + 0xbdu);
    if (!d) return 0xffffffffu;
    uint32_t q = hr32(c, d + 0x20u);
    if (!q) return 0xffffffffu;
    return (q & 0xffff0000u) | hr16(c, q + offset);
}

/* 0x44718c(G) (listing 0x44718c..0x447246): the automatic page switch.
 * E = 0x434310([0x49b088]); the flag is set when E exists, its row word
 * (0x440a3c) is 0 and its column word (0x440a64) is 3. With the saved page
 * word 0x49cbc6 = -1 and the flag: when byte 0x49cbc8 is set, word
 * 0x49cbc4 <- word G+0xc1 and 0x446e60(G, 5), then 0x49cbc6 <- 5; else
 * 0x49cbc6 <- 5 and 0x49cbc8 <- 1. With 0x49cbc6 set (not -1) and the flag
 * clear: 0x446e60(G, word 0x49cbc4) and 0x49cbc8 <- 1. */
static int auto_page(hud_ctx *c, uint32_t g) {
    uint32_t e = 0, flag = 0;
    if (HCALL(c, VA_SELECTED_ENTRY, &e, hr32(c, VA_PANEL))) return 1;
    if (e) {
        uint32_t row = 0, column = 0;
        if (HCALL(c, VA_ENTRY_ROW, &row, e) || HCALL(c, VA_ENTRY_COLUMN, &column, e)) return 1;
        flag = (row & 0xffffu) == 0 && (column & 0xffffu) == 3u;
    }
    if (hr16(c, 0x0049cbc6u) == 0xffffu && flag) {
        if (hr8(c, 0x0049cbc8u)) {
            hw16(c, 0x0049cbc4u, hr16(c, g + 0xc1u));
            if (HCALL(c, VA_MFD_PAGE, NULL, g, 5u)) return 1;
            hw16(c, 0x0049cbc6u, 5);
        } else {
            hw16(c, 0x0049cbc6u, 5);
            hw8(c, 0x0049cbc8u, 1);
        }
    } else if (hr16(c, 0x0049cbc6u) != 0xffffu && !flag) {
        if (HCALL(c, VA_MFD_PAGE, NULL, g, hsx16(hr16(c, 0x0049cbc4u)))) return 1;
        hw8(c, 0x0049cbc8u, 1);
    }
    return 0;
}

/* 0x471e10(A, pt) (listing 0x471e10..0x471e74): an animation A starts:
 * S = [[A+0x12]+4] (its step list), the clock (site 0x471e1f), B =
 * [[[A+0x1e]+8] + 4 [S]] (the first step's bitmap); word A+4 <- 0, A+6 <-
 * now, A+0xa <- [S+4] (the step's length), A+0xe <- [A+6] + it; the 8
 * bytes at pt copied to A+0x22; word A <- 1 (running); B blitted at A+0x22
 * (0x45c8bc). EAX: the blit's with AL = 1. */
static int anim_start(hud_ctx *c, uint32_t a, uint32_t pt, uint32_t *eax) {
    uint32_t s = hr32(c, hr32(c, a + 0x12u) + 4u), now = 0;
    if (hud_clock(c, 0x00471e1fu, &now)) return 1;
    uint32_t b = hr32(c, hr32(c, hr32(c, a + 0x1eu) + 8u) + 4u * hr32(c, s));
    uint32_t len = hr32(c, s + 4u);
    hw16(c, a + 4u, 0);
    hw32(c, a + 6u, now);
    hw32(c, a + 0xau, len);
    hw32(c, a + 0xeu, hr32(c, a + 6u) + len);
    hw32(c, a + 0x22u, hr32(c, pt));
    hw32(c, a + 0x26u, hr32(c, pt + 4u));
    hw16(c, a, 1);
    uint32_t r = 0;
    if (HCALL(c, VA_BLIT_AT, &r, VA_DESC, b, a + 0x22u)) return 1;
    *eax = (r & 0xffffff00u) | 1u;
    return 0;
}

/* 0x471e88(A, redraw) (listing 0x471e88..0x471f20): one step of a running
 * animation (word A = 1): S = the step [[A+0x12]+4] + 8 word A+4; the
 * clock (site 0x471ea9); while now > A+0xe (unsigned): word A+4 += 1, S
 * advances; at step count [[A+0x12]] - 1 or beyond (unsigned) word A <- 2
 * (ended) and the loop stops, else A+0xa += [S+4] and the loop repeats
 * while now > [A+6] + [A+0xa]; then A+0xe <- [A+6] + [A+0xa] and the
 * redraw flag (the second argument's low byte) set. With the flag the step
 * bitmap [[[A+0x1e]+8] + 4 [S]] is blitted at A+0x22. EAX: AL = 1 over the
 * blit's EAX (or the clock's when nothing advanced and the flag is
 * clear). An ended animation (word A = 2) is reset to 0; EAX 0. */
static int anim_step(hud_ctx *c, uint32_t a, uint32_t redraw, uint32_t *eax) {
    uint32_t flag = redraw & 0xffu;
    if (hr16(c, a) != 1u) {
        if (hr16(c, a) == 2u) hw16(c, a, 0);
        *eax = 0;
        return 0;
    }
    uint32_t s = hr32(c, hr32(c, a + 0x12u) + 4u) + 8u * hsx16(hr16(c, a + 4u)), now = 0, r;
    if (hud_clock(c, 0x00471ea9u, &now)) return 1;
    r = now;
    if (now > hr32(c, a + 0xeu)) {
        for (;;) {
            hw16(c, a + 4u, (uint16_t)(hr16(c, a + 4u) + 1u));
            uint32_t i = hsx16(hr16(c, a + 4u));
            s += 8u;
            if (!(i < hr32(c, hr32(c, a + 0x12u)) - 1u)) {
                hw16(c, a, 2);
                break;
            }
            hw32(c, a + 0xau, hr32(c, a + 0xau) + hr32(c, s + 4u));
            if (!(now > hr32(c, a + 6u) + hr32(c, a + 0xau))) break;
        }
        r = hr32(c, a + 6u) + hr32(c, a + 0xau);
        flag = 1;
        hw32(c, a + 0xeu, r);
    }
    if (flag) {
        uint32_t b = hr32(c, hr32(c, hr32(c, a + 0x1eu) + 8u) + 4u * hr32(c, s));
        if (HCALL(c, VA_BLIT_AT, &r, VA_DESC, b, a + 0x22u)) return 1;
    }
    *eax = (r & 0xffffff00u) | 1u;
    return 0;
}

/* 0x45caa4(D, B, pt, angle, size, flags) (listing 0x45caa4..0x45caf2):
 * with a bitmap B, the point pt less D's origin D+0x9a/D+0x9e in a local
 * and 0x48185c(B, &local, angle, size, flags) (the flags dword pushed with
 * the low word of the argument over the high word of D+0x9a, the register
 * the origin was loaded into). EAX: the callee's, else the entry EAX. */
static int rotated_at(hud_ctx *c, uint32_t d, uint32_t b, uint32_t pt, uint32_t angle, uint32_t size, uint32_t flags, uint32_t *eax) {
    if (!b) return 0;
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 8u);
    if (!f) return no_frame(c, VA_ROTATED_AT, "rotated bitmap at");
    hw32(c, f, hr32(c, pt) - hr32(c, d + 0x9au));
    hw32(c, f + 4u, hr32(c, pt + 4u) - hr32(c, d + 0x9eu));
    int bad = HCALL(c, VA_ROTATED_BLIT, eax, b, f, angle, size, (hr32(c, d + 0x9au) & 0xffff0000u) | (flags & 0xffffu));
    hud_frame_pop(c, top);
    return bad;
}

/* ---- the MFD frame */

/* 0x446350(G) (listing 0x446350..0x446831), MFDGauge slot +8. Frame
 * 0x234 (the listing's offsets): the saved GC -0x234 (0x81 dwords), the
 * GC -4, its +0x208 -8, the alert block M -0xc, a fill record -0x14
 * {0, 0x11}, a point -0x1c, a size -0x24 (and its copy -0x20), the page
 * flag -0x29, the button table cursor -0x30.
 * Nothing with the display [0x429820()] in mode 4, G+0xeb not visible
 * (0x431410) or byte G+0x8c clear. Before the power-up is done (byte
 * G+0x8d clear): on page 3 the radar sweep animation [G+0x331] steps
 * (0x471e88(A, 0)) when it runs (A+6 set and word A not 0), else it starts
 * at [G+0xd9]+0x18 (0x471e10) and G+0x345 <- the clock (site 0x446401) +
 * 0x46; once word A is 0 byte G+0x8d <- 1; then nothing more. On other
 * pages G+0x8d <- 1 when the clock (site 0x446433) passes G+0x345
 * (unsigned). With A set and G+0x8d: A, [G+0x335], [G+0x339], [G+0x33d]
 * deleted (0x477548), the counter [0x6cbee4] up, 0x48349c([G+0x341], 3),
 * A <- 0. Word G+0x74 <- 0x4342e0(2); when not 0, 0x446ddc(G) and the
 * gauge blink 0x438bc0(G). Byte G+0x76 turning 0 (against G+0x77) sets
 * G+0xb0 <- 2. Nothing more while G+0x76 is set or the alert animation
 * [G+0x6c] runs (0x471f24 not 0). Byte [0x4cf99c]+0x1c sets G+0xb0 <- 2.
 * The buttons G+0x18 + 4i (i < 13) marked in the page's row of 0x49cbd8
 * (13 bytes per page; the page re-read per button) draw. The GC saved and
 * G+0x104 loaded, GC+0x208 <- 2; 0x44718c(G). With word 0x49cbc6 = -1
 * and M = [[0x49b088]+0x1f9]+0x766 holding a text [M] and a count
 * [M+0x1c]: the alert page: GC+0x22c <- the fill record, 0x486704(G+0xeb),
 * per i < [M+0x1c] (signed, re-read) the bitmap [M+4+4i] at
 * ([M+0xc+8i] + [G+0xeb] + (0x14 << x shift), [M+0x10+8i] + [G+0xef] +
 * (0 << y shift)) with the size (word B+4 << y shift, word B+6 << x
 * shift; 16-bit shifts, sign-extended) through 0x45caa4(0x4d24bc, B, &pt,
 * 0, &size, 0); when word M+0x22 is 2 the label [G+0x329] gets +0x1d <-
 * word M+0x20 (sign-extended) and the text [M] (0x438920); byte G+0x328
 * <- 1. Else: a set G+0x328 is cleared with G+0xb0 <- 2; G+0xb0 = 1: the
 * panel copy 0x43122c(0, G+0xeb, 0), G+0xb0 down, byte G+0xe5+page <- 1;
 * G+0xb0 = 2: the full draw (slot +4 of [G]) and G+0xb0 down. With byte
 * G+0xe5+page: pages 0 and 4 run the page frame only when the clock (site
 * 0x446767) passes G+0x100 (unsigned; then G+0x100 <- now + 0x1e, page 0:
 * G+0xb9 <- [G + 4 [G+0x318] + 0x308], page 4: G+0xb9 <- [[0x49b088]
 * +0x210]); pages 2, 3 and 5 always; page 1 and pages above 5 clear the
 * byte and run it. The page frame is slot +4 of [G+0xcd + 4 page]; on
 * pages 2 and 5 the title label [G+0xc9] gets [0x4d135c + 4 page]. The GC
 * restored. */
static int mfd_frame(hud_ctx *c, uint32_t g) {
    uint32_t e = 0;
    int yes;
    if (HCALL0(c, VA_DISPLAY_CURRENT, &e)) return 1;
    if (hr32(c, e + 0x14u) == 4u) return 0;
    if (visible(c, g + 0xebu, &yes)) return 1;
    if (!yes || !hr8(c, g + 0x8cu)) return 0;
    uint32_t now = 0, eax = 0;
    if (!hr8(c, g + 0x8du)) {
        if (hr32(c, g + 0xc1u) == 3u) {
            uint32_t a = hr32(c, g + 0x331u);
            if (hr32(c, a + 6u) != 0 && hr16(c, a) != 0) {
                if (HCALL(c, VA_ANIM_STEP, NULL, a, 0u)) return 1;
            } else {
                if (HCALL(c, VA_ANIM_START, NULL, a, hr32(c, g + 0xd9u) + 0x18u) || hud_clock(c, 0x00446401u, &now)) return 1;
                hw32(c, g + 0x345u, now + 0x46u);
            }
            if (hr16(c, hr32(c, g + 0x331u)) == 0) hw8(c, g + 0x8du, 1);
            return 0;
        }
        if (hud_clock(c, 0x00446433u, &now)) return 1;
        if (now > hr32(c, g + 0x345u)) hw8(c, g + 0x8du, 1);
    }
    uint32_t a = hr32(c, g + 0x331u);
    if (a && hr8(c, g + 0x8du)) {
        if (HCALL(c, HUD_VA_DELETE, NULL, a) || HCALL(c, HUD_VA_DELETE, NULL, hr32(c, g + 0x335u)) ||
            HCALL(c, HUD_VA_DELETE, NULL, hr32(c, g + 0x339u)) || HCALL(c, HUD_VA_DELETE, NULL, hr32(c, g + 0x33du)))
            return 1;
        hud_counter(c, 1);
        if (HCALL(c, VA_BITMAP_ARRAY_DESTROY, NULL, hr32(c, g + 0x341u), 3u)) return 1;
        hw32(c, g + 0x331u, 0);
    }
    if (HCALL(c, VA_DAMAGE_ALERT, &eax, 2u)) return 1;
    hw16(c, g + 0x74u, (uint16_t)eax);
    if ((eax & 0xffffu) != 0) {
        if (HCALL(c, VA_ALERT_SET, NULL, g) || HCALL(c, VA_GAUGE_BLINK, NULL, g)) return 1;
    }
    uint8_t blink = hr8(c, g + 0x76u);
    if (blink != hr8(c, g + 0x77u) && blink == 0) hw8(c, g + 0xb0u, 2);
    if (hr8(c, g + 0x76u)) return 0;
    uint32_t alert = hr32(c, g + 0x6cu);
    if (alert) {
        if (HCALL(c, VA_ANIM_STATE, &eax, alert)) return 1;
        if (eax) return 0;
    }
    if (hr8(c, hr32(c, VA_DISPLAY) + 0x1cu)) hw8(c, g + 0xb0u, 2);
    for (uint32_t i = 0; i < 13u; ++i) {
        uint32_t page = hr32(c, g + 0xc1u);
        if (hr8(c, 0x0049cbd8u + 13u * page + i) && widget_draw(c, hr32(c, g + 0x18u + 4u * i))) return 1;
    }
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0x234u);
    if (!f) return no_frame(c, 0x00446350u, "MFD frame");
#define L(k) (f + 0x234u - (k))
    int bad = 1;
    uint32_t gc = hr32(c, VA_GC);
    gc_save(c, gc, L(0x234));
    uint32_t saved = hr32(c, gc + 0x208u);
    gc_load(c, gc, g + 0x104u);
    hw32(c, gc + 0x208u, 2);
    if (HCALL(c, VA_AUTO_PAGE, NULL, g)) goto done;
    uint32_t m = hr32(c, hr32(c, VA_PANEL) + 0x1f9u) + 0x766u;
    if (hr16(c, 0x0049cbc6u) == 0xffffu && hr32(c, m) && hr32(c, m + 0x1cu)) {
        hw32(c, L(0x14), 0);
        hw32(c, gc + 0x22cu, L(0x14));
        hw32(c, L(0x10), 0x11);
        if (HCALL(c, VA_RECT_FILL, NULL, g + 0xebu)) goto done;
        for (int32_t i = 0; i < (int32_t)hr32(c, m + 0x1cu); ++i) {
            uint32_t p = m + 0xcu + 8u * (uint32_t)i;
            hw32(c, L(0x1c), hr32(c, p) + hr32(c, g + 0xebu) + (0x14u << shift_x(c)));
            hw32(c, L(0x18), hr32(c, p + 4u) + hr32(c, g + 0xefu) + (0u << shift_y(c)));
            uint32_t b = hr32(c, m + 4u + 4u * (uint32_t)i);
            hw32(c, L(0x28), hsx16((uint16_t)((uint32_t)hr16(c, b + 4u) << shift_y(c))));
            hw32(c, L(0x24), hsx16((uint16_t)((uint32_t)hr16(c, b + 6u) << shift_x(c))));
            hw32(c, L(0x20), hr32(c, L(0x28)));
            if (HCALL(c, VA_ROTATED_AT, NULL, VA_DESC, b, L(0x1c), 0u, L(0x24), 0u)) goto done;
        }
        if (hr16(c, m + 0x22u) == 2u) {
            uint32_t label = hr32(c, g + 0x329u);
            hw32(c, label + 0x1du, hsx16(hr16(c, m + 0x20u)));
            if (HCALL(c, VA_LABEL, NULL, label, hr32(c, m))) goto done;
        }
        hw8(c, g + 0x328u, 1);
    } else {
        if (hr8(c, g + 0x328u)) {
            hw8(c, g + 0x328u, 0);
            hw8(c, g + 0xb0u, 2);
        }
        uint8_t state = hr8(c, g + 0xb0u);
        if (state == 1u) {
            if (HCALL(c, VA_PANEL_COPY, NULL, 0u, g + 0xebu, 0u)) goto done;
            dec8(c, g + 0xb0u);
            hw8(c, g + hr32(c, g + 0xc1u) + 0xe5u, 1);
        } else if (state == 2u) {
            if (vslot(c, g, 4u)) goto done;
            dec8(c, g + 0xb0u);
        }
        uint32_t page = hr32(c, g + 0xc1u);
        if (hr8(c, g + page + 0xe5u)) {
            int run = 1;
            switch (page) {
            case 0: case 4:
                if (hud_clock(c, 0x00446767u, &now)) goto done;
                if (now > hr32(c, g + 0x100u)) {
                    hw32(c, g + 0x100u, now + 0x1eu);
                    if (hr32(c, g + 0xc1u) == 0u) hw32(c, g + 0xb9u, hr32(c, g + 4u * hr32(c, g + 0x318u) + 0x308u));
                    if (hr32(c, g + 0xc1u) == 4u) hw32(c, g + 0xb9u, hr32(c, hr32(c, VA_PANEL) + 0x210u));
                } else run = 0;
                break;
            case 2: case 3: case 5: break;
            default: hw8(c, g + hr32(c, g + 0xc1u) + 0xe5u, 0); break;
            }
            if (run) {
                page = hr32(c, g + 0xc1u);
                if (vslot(c, hr32(c, g + 4u * page + 0xcdu), 4u)) goto done;
                page = hr32(c, g + 0xc1u);
                if ((page == 2u || page == 5u) && HCALL(c, VA_LABEL, NULL, hr32(c, g + 0xc9u), hr32(c, 0x004d135cu + 4u * page))) goto done;
            }
        }
    }
    bad = 0;
done:
    gc_load(c, gc, L(0x234));
    hw32(c, gc + 0x208u, saved);
#undef L
    hud_frame_pop(c, top);
    return bad;
}

/* ---- the MFD pages' frames */

/* 0x43b238(X) (listing 0x43b238..0x43b287), the status page's frame: a
 * request X+0xc = 100 is taken (X+0xc <- 0, byte X+0x34 <- 1, X+0x35 <-
 * the clock (site 0x43b24e) + 0x1e); else a set X+0x34 clears once the
 * clock (site 0x43b261) passes X+0x35 (unsigned). Then the draw (slot 0
 * of [X]) and the panel copy 0x43122c(1, [X+4], 0). */
static int status_page_frame(hud_ctx *c, uint32_t x) {
    uint32_t now = 0;
    if (hr32(c, x + 0xcu) == 0x64u) {
        hw32(c, x + 0xcu, 0);
        hw8(c, x + 0x34u, 1);
        if (hud_clock(c, 0x0043b24eu, &now)) return 1;
        hw32(c, x + 0x35u, now + 0x1eu);
    } else if (hr8(c, x + 0x34u)) {
        if (hud_clock(c, 0x0043b261u, &now)) return 1;
        if (now > hr32(c, x + 0x35u)) hw8(c, x + 0x34u, 0);
    }
    if (vslot(c, x, 0)) return 1;
    return HCALL(c, VA_PANEL_COPY, NULL, 1u, hr32(c, x + 4u), 0u);
}

/* 0x440314(X) (listing 0x440314..0x44036f), the target page's frame:
 * byte X+0x43 <- 2 unless [0x49c394], X+0x1c and byte [[0x49b088]+0x1f5]
 * +0xd6 are all 0; X+0x43 = 1: the panel copy 0x43122c(0, 0x4d1a44, 0)
 * and X+0x43 down; = 2: the draw (slot 0 of [X]) and X+0x43 down. */
static int target_page_frame(hud_ctx *c, uint32_t x) {
    if (hr32(c, 0x0049c394u) != 0 || hr32(c, x + 0x1cu) != 0 || hr8(c, hr32(c, hr32(c, VA_PANEL) + 0x1f5u) + 0xd6u)) hw8(c, x + 0x43u, 2);
    uint8_t state = hr8(c, x + 0x43u);
    if (state == 2u) {
        if (vslot(c, x, 0)) return 1;
        dec8(c, x + 0x43u);
    } else if (state == 1u) {
        if (HCALL(c, VA_PANEL_COPY, NULL, 0u, 0x004d1a44u, 0u)) return 1;
        dec8(c, x + 0x43u);
    }
    return 0;
}

/* The orders page X: X+8 the order word's address, X+0x14 + 4i the six
 * order buttons, X+0x2c + i their flag bytes (1 disabled, 2 the second
 * text), X+0x32 the shown order, byte X+0x36 a text change, word X+0x37
 * the previous order (-1 none), X+0x39 its rectangle. */

/* 0x43fa5c(X, n, current) (listing 0x43fa5c..0x43faf2): the order marker
 * of button n (n above 6 is n - 3): O = 0x445234(X); the point of the
 * button's rectangle [X+0x14+4n]+4 less (0 << x shift, 0 << y shift) in a
 * local; bitmap 0xd when `current` (its low byte) is set, else 0xb + word
 * [[O+0x40]+0x1b] (sign-extended), of [[0x49cbcc]+8], blitted there
 * (0x45c8bc). */
static int orders_mark(hud_ctx *c, uint32_t x, uint32_t n, uint32_t current) {
    if ((int32_t)n > 6) n = 6u - (9u - n);
    uint32_t o = 0;
    if (HCALL(c, VA_MFD_OBJECT, &o, x)) return 1;
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 8u);
    if (!f) return no_frame(c, VA_ORDERS_MARK, "orders marker");
    uint32_t r = hr32(c, x + 0x14u + 4u * n) + 4u, s = hr32(c, o + 0x40u);
    hw32(c, f, hr32(c, r));
    hw32(c, f + 4u, hr32(c, r + 4u));
    hw32(c, f, hr32(c, f) - (0u << shift_x(c)));
    hw32(c, f + 4u, hr32(c, f + 4u) - (0u << shift_y(c)));
    uint32_t i = (current & 0xffu) ? 0xdu : hsx16(hr16(c, s + 0x1bu)) + 0xbu;
    int bad = HCALL(c, VA_BLIT_AT, NULL, VA_DESC, hr32(c, hr32(c, hr32(c, VA_HUD_BITMAPS) + 8u) + 4u * i), f);
    hud_frame_pop(c, top);
    return bad;
}
/* The text index of order n: n + 3 with flag 2 (16-bit), else n. */
static uint32_t order_text(const hud_ctx *c, uint32_t x, uint32_t n, uint32_t word) {
    return hsx16((uint16_t)((hr8(c, x + n + 0x2cu) & 2u) ? word + 3u : word));
}
static int order_label(hud_ctx *c, uint32_t w, uint32_t t) {
    uint32_t text = hr32(c, 0x004d12a8u + 4u * t);
    return HCALL(c, VA_LABEL_KEY, NULL, w, text, hr8(c, hr32(c, 0x004d12f0u + 4u * t)));
}

/* 0x43f7cc(X) (listing 0x43f7cc..0x43f89d), the orders page's draw:
 * GC+0x22c <- a fill record {0, 0x11} (a local), 0x486704(X+0x39); X+0x32
 * <- [[X+8]]; per button i < 6 its colour [W] <- [0x49b0c4] (disabled,
 * flag 1), [0x49b0b8] (the shown order) or [0x49b0b0], and its label
 * 0x438aac(W, [0x4d12a8 + 4t], byte [[0x4d12f0 + 4t]]) with t the text
 * index; then the marker 0x43fa5c(X, [X+0x32], 0). */
static int orders_draw(hud_ctx *c, uint32_t x) {
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 8u);
    if (!f) return no_frame(c, 0x0043f7ccu, "orders draw");
    int bad = 1;
    hw32(c, f, 0);
    hw32(c, hr32(c, VA_GC) + 0x22cu, f);
    hw32(c, f + 4u, 0x11);
    if (HCALL(c, VA_RECT_FILL, NULL, x + 0x39u)) goto done;
    hw32(c, x + 0x32u, hr32(c, hr32(c, x + 8u)));
    for (uint32_t i = 0; i < 6u; ++i) {
        uint32_t w = hr32(c, x + 0x14u + 4u * i), colour;
        if (hr8(c, x + 0x2cu + i) & 1u) colour = hr32(c, 0x0049b0c4u);
        else if (i == hr32(c, x + 0x32u)) colour = hr32(c, 0x0049b0b8u);
        else colour = hr32(c, 0x0049b0b0u);
        hw32(c, w, colour);
        if (order_label(c, hr32(c, x + 0x14u + 4u * i), order_text(c, x, i, i))) goto done;
    }
    bad = HCALL(c, VA_ORDERS_MARK, NULL, x, hr32(c, x + 0x32u), 0u);
done:
    hud_frame_pop(c, top);
    return bad;
}

/* 0x43f8a0(X) (listing 0x43f8a0..0x43f9bc), the orders page's frame:
 * nothing (but word X+0x37 <- -1) while the order [[X+8]] is the shown
 * one, X+0x36 is clear and X+0x37 is -1. Else the previous order p (word
 * X+0x37, or word X+0x32 when that is -1): its button gets [0x49b0b0],
 * the marker 0x43fa5c(X, p, 1), its label again and the panel copy
 * 0x43122c(1, W+4, 0); X+0x32 <- [[X+8]], its button gets [0x49b0b8], its
 * label, the marker 0x43fa5c(X, X+0x32, 0) and the panel copy; byte X+0x36
 * <- 0; word X+0x37 <- -1. */
static int orders_frame(hud_ctx *c, uint32_t x) {
    uint32_t order = hr32(c, hr32(c, x + 8u));
    if (!(order == hr32(c, x + 0x32u) && !hr8(c, x + 0x36u) && hr16(c, x + 0x37u) == 0xffffu)) {
        uint32_t p = hr16(c, x + 0x37u) == 0xffffu ? hr16(c, x + 0x32u) : hr16(c, x + 0x37u), n = hsx16((uint16_t)p);
        uint32_t w = hr32(c, x + 4u * n + 0x14u);
        hw32(c, w, hr32(c, 0x0049b0b0u));
        uint32_t t = order_text(c, x, n, p);
        uint32_t text = hr32(c, 0x004d12a8u + 4u * t);
        if (HCALL(c, VA_ORDERS_MARK, NULL, x, n, 1u)) return 1;
        if (HCALL(c, VA_LABEL_KEY, NULL, w, text, hr8(c, hr32(c, 0x004d12f0u + 4u * t)))) return 1;
        if (HCALL(c, VA_PANEL_COPY, NULL, 1u, w + 4u, 0u)) return 1;
        uint32_t now = hr32(c, hr32(c, x + 8u));
        hw32(c, x + 0x32u, now);
        w = hr32(c, x + 4u * now + 0x14u);
        hw32(c, w, hr32(c, 0x0049b0b8u));
        t = order_text(c, x, hr32(c, x + 0x32u), hr16(c, x + 0x32u));
        if (order_label(c, w, t)) return 1;
        if (HCALL(c, VA_ORDERS_MARK, NULL, x, hr32(c, x + 0x32u), 0u)) return 1;
        if (HCALL(c, VA_PANEL_COPY, NULL, 1u, w + 4u, 0u)) return 1;
        hw8(c, x + 0x36u, 0);
    }
    hw16(c, x + 0x37u, 0xffff);
    return 0;
}

/* ---- the HDD */

/* 0x449f64(H) (listing 0x449f64..0x449fe2): the page frame: the GC's 0x81
 * dwords (and +0x208) saved; P = [H+0x101 + 4 [H+0x109]] gets +0xc <-
 * byte H+0xb0 (sign-extended); P's frame (slot +4 of [P], the page re-read)
 * runs; H+0xb0 down; the GC restored. */
static int hdd_page_frame(hud_ctx *c, uint32_t h) {
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0x204u);
    if (!f) return no_frame(c, VA_HDD_PAGE_FRAME, "HDD page frame");
    uint32_t gc = hr32(c, VA_GC);
    gc_save(c, gc, f);
    uint32_t state = hsx8(hr8(c, h + 0xb0u)), p = hr32(c, h + 4u * hr32(c, h + 0x109u) + 0x101u);
    uint32_t saved = hr32(c, gc + 0x208u);
    hw32(c, p + 0xcu, state);
    int bad = vslot(c, hr32(c, h + 4u * hr32(c, h + 0x109u) + 0x101u), 4u);
    if (!bad) {
        dec8(c, h + 0xb0u);
        gc_load(c, gc, f);
        hw32(c, gc + 0x208u, saved);
    }
    hud_frame_pop(c, top);
    return bad;
}

/* 0x44a704(H) (listing 0x44a704..0x44a747): the HDD title [H+0x51b]:
 * page 0 [0x4d13d0], page 1 [0x4d13d8 + 4 [[H+0x105]+0x7c]], else none;
 * a null text draws nothing. */
static int hdd_title(hud_ctx *c, uint32_t h) {
    uint32_t t = 0, page = hr32(c, h + 0x109u);
    if (page == 0u) t = hr32(c, 0x004d13d0u);
    else if (page == 1u) t = hr32(c, 0x004d13d8u + 4u * hr32(c, hr32(c, h + 0x105u) + 0x7cu));
    if (!t) return 0;
    return HCALL(c, VA_LABEL, NULL, hr32(c, h + 0x51bu), t);
}

/* 0x44cff8(P) / 0x44d05c(P) (listings 0x44cff8..0x44d05a and
 * 0x44d05c..0x44d0bf): the map page's scroll toward the left / right
 * edge. With the shift s = [0x49d6bc] (cl), the half width k = (-[[P+0x10]
 * +0x220] * [P+0x20]) sar s and the step [P+0x145]: the room r = [P+0x18]
 * - ([0x4aa640] - 60000) - k (left) or ([0x4aa648] + 60000) - [P+0x18] - k
 * (right); r >= the step (signed) moves P+0x114 by the step, else a
 * positive r moves it by r (left: down, right: up); a move sets byte
 * 0x49d6ae. EAX: P. The vertical pair 0x44d124 (toward the
 * top: [0x4aa644], [[P+0x10]+0x224], P+0x1c, P+0x118 down; listing
 * 0x44d124..0x44d187) and 0x44d0c0 (toward the bottom: [0x4aa64c], P+0x118
 * up; 0x44d0c0..0x44d123) are the same with every operand 4 bytes on
 * (`y` = 4). */
static void map_scroll(hud_ctx *c, uint32_t p, int right, uint32_t y) {
    uint32_t s = hr32(c, 0x0049d6bcu) & 31u, step = hr32(c, p + 0x145u), at = p + 0x18u + y;
    uint32_t k = (uint32_t)((int32_t)((0u - hr32(c, hr32(c, p + 0x10u) + 0x220u + y)) * hr32(c, p + 0x20u)) >> s);
    uint32_t room = right ? hr32(c, 0x004aa648u + y) + 0xea60u - hr32(c, at) - k : hr32(c, at) - (hr32(c, 0x004aa640u + y) + 0xffff15a0u) - k;
    uint32_t by;
    if ((int32_t)room >= (int32_t)step) by = step;
    else if ((int32_t)room > 0) by = room;
    else return;
    hw32(c, p + 0x114u + y, right ? hr32(c, p + 0x114u + y) + by : hr32(c, p + 0x114u + y) - by);
    hw8(c, 0x0049d6aeu, 1);
}

/* 0x449bf8(H) (listing 0x449bf8..0x449ee3), HDDGauge slot +8. The
 * player's target T = [[0x49b088]+0x210]: none sets H+0x55e <- [0x4d14e4];
 * one with word T+0x1a8 set sets H+0x55e <- [0x4d14e8] and counts as none.
 * H+0x544 <- T; a change against [0x49d3dc] stores it there (and in
 * H+0x524 when word H+0x55c is 4) and sets byte H+0x110. The button
 * executor 0x44a1a0(H). The radio records k < 3 of [0x4d03c8 + 4k] (V):
 * with byte [0x4cf99c]+0x1c and the display (0x429820) in mode 4, a set
 * byte V+0x99 sets byte S+0x147 (S = H+0x12d + 0x14e k); V+0x99 and
 * S+0x147 both set give S+0x13b <- 1. The radio panel 0x44b620(H).
 * Nothing more in display mode 4 (0x429820 again). Word H+0x74 <-
 * 0x4342e0(2), not 0: the gauge blink 0x438bc0(H). A change of byte H+0x76
 * against H+0x52f marks the page for a full draw (byte H+0x10d+page) and
 * H+0x52f follows. With H+0xc1 visible (0x431410): the [H+0x68] buttons
 * draw; page 0 with byte [0x4cf99c]+0x1c scrolls the map (0x44cff8,
 * 0x44d05c on [H+0x101]) and marks byte H+0x10f+page with H+0xb0 <- 2;
 * page 1 does the same once the clock (site 0x449dc0) passes H+0x530
 * (unsigned; H+0x530 <- now + 0x1e). Byte H+0xb0 = 2: a marked full draw
 * runs 0x449ee4 and the title 0x44a704, else a marked frame sets P+0xc and
 * runs 0x449f64. = 1: a marked full draw runs the port 0x433158, the title,
 * H+0xb0 down and clears the mark; else a marked frame as above; then
 * pages 0 and 1 mark the frame again with H+0xb0 <- 2, other pages clear
 * the frame mark. */
static int hdd_frame(hud_ctx *c, uint32_t h) {
    uint32_t t = hr32(c, hr32(c, VA_PANEL) + 0x210u), e = 0, eax = 0;
    if (t) {
        if (hr16(c, t + 0x1a8u)) {
            hw32(c, h + 0x55eu, hr32(c, 0x004d14e8u));
            t = 0;
        }
    } else hw32(c, h + 0x55eu, hr32(c, 0x004d14e4u));
    hw32(c, h + 0x544u, t);
    if (t != hr32(c, 0x0049d3dcu)) {
        if (hr16(c, h + 0x55cu) == 4u) hw32(c, h + 0x524u, t);
        hw32(c, 0x0049d3dcu, t);
        hw8(c, h + 0x110u, 1);
    }
    if (HCALL(c, VA_HDD_EXECUTE, NULL, h)) return 1;
    int local = 0;
    if (hr8(c, hr32(c, VA_DISPLAY) + 0x1cu)) {
        if (HCALL0(c, VA_DISPLAY_CURRENT, &e)) return 1;
        if (hr32(c, e + 0x14u) == 4u) local = 1;
    }
    for (uint32_t k = 0; k < 3u; ++k) {
        uint32_t v = hr32(c, 0x004d03c8u + 4u * k), s = h + 0x12du + 0x14eu * k;
        if (!v) continue;
        if (local && hr8(c, v + 0x99u)) hw8(c, s + 0x147u, 1);
        if (hr8(c, v + 0x99u) && hr8(c, s + 0x147u)) hw32(c, s + 0x13bu, 1);
    }
    if (HCALL(c, VA_HDD_RADIO, NULL, h) || HCALL0(c, VA_DISPLAY_CURRENT, &e)) return 1;
    if (hr32(c, e + 0x14u) == 4u) return 0;
    if (HCALL(c, VA_DAMAGE_ALERT, &eax, 2u)) return 1;
    hw16(c, h + 0x74u, (uint16_t)eax);
    if ((eax & 0xffffu) != 0 && HCALL(c, VA_GAUGE_BLINK, NULL, h)) return 1;
    if (hr8(c, h + 0x76u) != hr8(c, h + 0x52fu)) hw8(c, h + hr32(c, h + 0x109u) + 0x10du, 1);
    hw8(c, h + 0x52fu, hr8(c, h + 0x76u));
    int yes;
    if (visible(c, h + 0xc1u, &yes)) return 1;
    if (!yes) return 0;
    for (int32_t i = 0; i < (int32_t)hr32(c, h + 0x68u); ++i)
        if (widget_draw(c, hr32(c, h + 0x18u + 4u * (uint32_t)i))) return 1;
    uint32_t page = hr32(c, h + 0x109u), now = 0;
    if (page == 0u) {
        if (hr8(c, hr32(c, VA_DISPLAY) + 0x1cu)) {
            if (HCALL(c, VA_MAP_SCROLL_LEFT, NULL, hr32(c, h + 0x101u)) || HCALL(c, VA_MAP_SCROLL_RIGHT, NULL, hr32(c, h + 0x101u))) return 1;
            hw8(c, h + hr32(c, h + 0x109u) + 0x10fu, 1);
            hw8(c, h + 0xb0u, 2);
        }
    } else if (page == 1u) {
        if (hud_clock(c, 0x00449dc0u, &now)) return 1;
        if (now > hr32(c, h + 0x530u)) {
            hw8(c, h + 0xb0u, 2);
            hw8(c, h + hr32(c, h + 0x109u) + 0x10fu, 1);
            hw32(c, h + 0x530u, now + 0x1eu);
        }
    }
    uint8_t state = hr8(c, h + 0xb0u);
    if (state == 2u) {
        page = hr32(c, h + 0x109u);
        if (hr8(c, h + page + 0x10du)) {
            if (HCALL(c, VA_HDD_PAGE_DRAW, NULL, h) || HCALL(c, VA_HDD_TITLE, NULL, h)) return 1;
        } else if (hr8(c, h + page + 0x10fu)) {
            hw32(c, hr32(c, h + 4u * page + 0x101u) + 0xcu, hsx8(hr8(c, h + 0xb0u)));
            if (HCALL(c, VA_HDD_PAGE_FRAME, NULL, h)) return 1;
        }
    } else if (state == 1u) {
        page = hr32(c, h + 0x109u);
        if (hr8(c, h + page + 0x10du)) {
            if (HCALL0(c, VA_PORT_A, NULL) || HCALL(c, VA_HDD_TITLE, NULL, h)) return 1;
            dec8(c, h + 0xb0u);
            hw8(c, h + hr32(c, h + 0x109u) + 0x10du, 0);
        } else if (hr8(c, h + page + 0x10fu)) {
            hw32(c, hr32(c, h + 4u * page + 0x101u) + 0xcu, hsx8(hr8(c, h + 0xb0u)));
            if (HCALL(c, VA_HDD_PAGE_FRAME, NULL, h)) return 1;
        }
        if (hr32(c, h + 0x109u) < 2u) {
            hw8(c, h + hr32(c, h + 0x109u) + 0x10fu, 1);
            hw8(c, h + 0xb0u, 2);
        } else hw8(c, h + hr32(c, h + 0x109u) + 0x10fu, 0);
    }
    return 0;
}

/* ---- service rows */
void es2_sim_service_mfd_frame(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (mfd_frame(&c, sc->slots[0])) hud_failed(sc);
}
void es2_sim_service_damage_alert(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t eax = 0;
    if (damage_alert(&c, sc->slots[0], sc->eax, &eax)) hud_failed(sc);
    else sc->eax = eax;
}
void es2_sim_service_mfd_alert_set(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    alert_set(&c, sc->slots[0]);
    sc->eax = sc->slots[0];
}
void es2_sim_service_mfd_auto_page(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (auto_page(&c, sc->slots[0])) hud_failed(sc);
}
void es2_sim_service_selected_entry(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    sc->eax = selected_entry(&c, sc->slots[0]);
}
void es2_sim_service_entry_row(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    sc->eax = entry_word(&c, sc->slots[0], 0);
}
void es2_sim_service_entry_column(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    sc->eax = entry_word(&c, sc->slots[0], 2u);
}
void es2_sim_service_anim_start(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t eax = 0;
    if (anim_start(&c, sc->slots[0], sc->slots[1], &eax)) hud_failed(sc);
    else sc->eax = eax;
}
void es2_sim_service_anim_step(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t eax = 0;
    if (anim_step(&c, sc->slots[0], sc->slots[1], &eax)) hud_failed(sc);
    else sc->eax = eax;
}
/* 0x471f24(A) (listing 0x471f24..0x471f2e): word A (the state),
 * sign-extended. */
void es2_sim_service_anim_state(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    sc->eax = hsx16(hr16(&c, sc->slots[0]));
}
void es2_sim_service_rotated_at(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t eax = sc->eax;
    if (rotated_at(&c, sc->slots[0], sc->slots[1], sc->slots[2], sc->slots[3], sc->slots[4], sc->slots[5], &eax)) hud_failed(sc);
    else sc->eax = eax;
}
void es2_sim_service_status_page_frame(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (status_page_frame(&c, sc->slots[0])) hud_failed(sc);
}
void es2_sim_service_target_page_frame(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (target_page_frame(&c, sc->slots[0])) hud_failed(sc);
}
void es2_sim_service_orders_mark(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (orders_mark(&c, sc->slots[0], sc->slots[1], sc->slots[2])) hud_failed(sc);
}
void es2_sim_service_orders_draw(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (orders_draw(&c, sc->slots[0])) hud_failed(sc);
}
void es2_sim_service_orders_frame(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (orders_frame(&c, sc->slots[0])) hud_failed(sc);
}
void es2_sim_service_hdd_frame(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (hdd_frame(&c, sc->slots[0])) hud_failed(sc);
}
void es2_sim_service_hdd_page_frame(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (hdd_page_frame(&c, sc->slots[0])) hud_failed(sc);
}
void es2_sim_service_hdd_title(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (hdd_title(&c, sc->slots[0])) hud_failed(sc);
}
/* 0x44a174(H, W) (listing 0x44a174..0x44a19d), HDDGauge slot 0: the index
 * of the pressed button W among the [H+0x68] buttons H+0x18.. into byte
 * H+0x528 (the executor's request); EAX the index reached. */
void es2_sim_service_hdd_button_pressed(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t h = sc->slots[0], i = 0;
    for (; (int32_t)i < (int32_t)hr32(&c, h + 0x68u); ++i)
        if (sc->slots[1] == hr32(&c, h + 0x18u + 4u * i)) {
            hw8(&c, h + 0x528u, (uint8_t)i);
            break;
        }
    sc->eax = i;
}
void es2_sim_service_map_scroll_left(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    map_scroll(&c, sc->slots[0], 0, 0);
    sc->eax = sc->slots[0];
}
void es2_sim_service_map_scroll_right(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    map_scroll(&c, sc->slots[0], 1, 0);
    sc->eax = sc->slots[0];
}
/* 0x450b54(X) (listing 0x450b54..0x450b70): X+0x80 <- -1, then
 * 0x450c7c(X, 0). 0x450b74(X) (listing 0x450b74..0x450b86): 0x450c7c(X, 1).
 * The HDD status page's draw and frame; EAX the callee's. */
void es2_sim_service_hdd_status_draw(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t eax = 0;
    hw32(&c, sc->slots[0] + 0x80u, 0xffffffffu);
    if (HCALL(&c, VA_HDD_STATUS, &eax, sc->slots[0], 0u)) hud_failed(sc);
    else sc->eax = eax;
}
void es2_sim_service_hdd_status_frame(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t eax = 0;
    if (HCALL(&c, VA_HDD_STATUS, &eax, sc->slots[0], 1u)) hud_failed(sc);
    else sc->eax = eax;
}

/* ==== The HDD button executor 0x44a1a0 with its callees, the radio
 * selection 0x44a748, the map point conversions 0x44d24c and the drag start
 * 0x44d3cc. Contract: hud.md, "the HDD button executor". */

#define VA_HDD_PAGE 0x0044a60cu
#define VA_HDD_SELECT 0x0044a748u
#define VA_MAP_SCROLL_DOWN 0x0044d0c0u
#define VA_MAP_SCROLL_UP 0x0044d124u
#define VA_MAP_ZOOM_IN 0x0044cf90u
#define VA_MAP_ZOOM_OUT 0x0044cfc4u
#define VA_MAP_ZOOM_BAR 0x0044eec8u
#define VA_STATUS_NEXT 0x00450bf4u
#define VA_STATUS_PREV 0x00450c18u
#define VA_STATUS_MODE 0x00450b88u
#define VA_TARGET_PREV 0x0044ba08u
#define VA_TARGET_NEXT 0x0044b9b0u
#define VA_ORDER_COMMIT 0x0044db9cu
#define VA_ORDER_COPY 0x0044db4cu
#define VA_ORDER_LABEL 0x0044dc6cu
#define VA_ORDER_TEXT 0x0044dd14u
#define VA_ORDER_CANCEL 0x0044dc10u      /* order_cancel (hook) */
#define VA_WINGMAN_SELECT 0x0044da98u    /* wingman_select (hook) */
#define VA_WINGMAN_ORDER 0x00431610u     /* wingman_order (hook) */
#define VA_CURSOR 0x00433e3cu
#define VA_CANVAS_PUSH 0x004311e0u
#define VA_CANVAS_POP 0x00431210u
#define VA_CURSOR_SET_A 0x0048992eu
#define VA_CURSOR_SET_B 0x00486e70u
#define VA_ORDER_RECORD 0x004d03d4u      /* the order record the executor fills */

static int call_regs(hud_ctx *c, uint32_t va, const uint32_t *slots, unsigned n, uint32_t eax, uint32_t edx, uint32_t ecx,
                     es2_sim_result *r) {
    const uint32_t regs[3] = {eax, edx, ecx};
    return hud_call_regs(c, va, slots, n, regs, r);
}

/* 0x44cf90(M) (listing 0x44cf90..0x44cfc0): the map zoom in: with the
 * step [0x4d1ce4], M+0x20 + step <= [0x4d1ce0] (signed) adds the step to
 * M+0x20, runs the zoom bar 0x44eec8(M), byte M+0x30 <- 1 and byte
 * 0x49d6ae <- 1. EAX: the step, or the zoom bar's EAX. */
static int map_zoom_in(hud_ctx *c, uint32_t m, uint32_t *eax) {
    uint32_t step = hr32(c, 0x004d1ce4u);
    *eax = step;
    if ((int32_t)(hr32(c, m + 0x20u) + step) > (int32_t)hr32(c, 0x004d1ce0u)) return 0;
    hw32(c, m + 0x20u, hr32(c, m + 0x20u) + step);
    if (HCALL(c, VA_MAP_ZOOM_BAR, eax, m)) return 1;
    hw8(c, m + 0x30u, 1);
    hw8(c, 0x0049d6aeu, 1);
    return 0;
}

/* 0x450bf4(X) / 0x450c18(X) (listings 0x450bf4..0x450c17 and
 * 0x450c18..0x450c3e): the HDD status page's next / previous mode: n =
 * [X+0x7c] + 1 below 2 (signed), else 0 / n = [X+0x7c] - 1 above 0,
 * else 2; status_mode 0x450b88(X, n). EAX: n; EDX/ECX: status_mode's. */
static int status_step(hud_ctx *c, uint32_t x, int next, es2_sim_result *r) {
    int32_t v = (int32_t)hr32(c, x + 0x7cu);
    uint32_t n = next ? (v < 2 ? (uint32_t)v + 1u : 0u) : (v > 0 ? (uint32_t)v - 1u : 2u);
    if (call_regs(c, VA_STATUS_MODE, (const uint32_t[]){x, n}, 2, x, 0, 0, r)) return 1;
    r->eax = n;
    return 0;
}

/* 0x44ba08(H) / 0x44b9b0(H) (listings 0x44ba08..0x44ba53 and
 * 0x44b9b0..0x44ba04): the HDD's previous / next target among the five
 * dwords H+0x534. The word k = H+0x55c steps down (below 0, signed, it
 * becomes 4) / up (at 5 it becomes 0); a null slot k steps again (the
 * original recurses; the upward step also stops at k = 4). EAX: the slot
 * [H+0x534 + 4 sx(k)] (the word re-read). ECX: H after a repeated step
 * (the recursion's POP ECX). Otherwise, down: the wrap leaves ECX = 4 and
 * EDX as it was (the entry EDX, or sx(k) of the previous test), a slot
 * found leaves ECX = EDX = sx(k); up: the wrap leaves EDX = 0 and ECX as
 * it was, a found slot or k = 4 leaves ECX = EDX = sx(k). */
static void target_step(hud_ctx *c, uint32_t h, int next, uint32_t *eax, uint32_t *edx, uint32_t *ecx) {
    uint32_t depth = 0, k;
    for (;;) {
        uint16_t w = (uint16_t)(hr16(c, h + 0x55cu) + (next ? 1u : 0xffffu));
        hw16(c, h + 0x55cu, w);
        if (next) {
            if (w == 5u) {
                hw16(c, h + 0x55cu, 0);
                k = 0;
                *edx = 0;
                break;
            }
            k = hsx16(w);
            *ecx = k;
            if (hr32(c, h + 0x534u + 4u * k) || w == 4u) { *edx = k; break; }
        } else {
            if ((int16_t)w < 0) {
                hw16(c, h + 0x55cu, 4);
                k = 4;
                *ecx = 4;
                break;
            }
            k = hsx16(w);
            *edx = k;
            if (hr32(c, h + 0x534u + 4u * k)) { *ecx = k; break; }
        }
        ++depth;
    }
    k = hsx16(hr16(c, h + 0x55cu));
    *eax = hr32(c, h + 0x534u + 4u * k);
    if (depth) *ecx = h;
}

/* The map page's order record R = S + 0x85 + 42 [S+0x105] (one per
 * wingman; 42 n computed in 32 bits as the listing's two LEAs and ADD). */
static uint32_t map_order_record(const hud_ctx *c, uint32_t s) { return s + 0x85u + 42u * hr32(c, s + 0x105u); }

/* 0x44db9c(S) (listing 0x44db9c..0x44dc0e): the order commit. With byte
 * S+0x103 set: byte S+0x104 <- 1; the kind t = [R+0x26]: 0 copies the
 * point R+0xc..+0x17 to R+0..+0xb (the dwords +8, +0, +4 in that order),
 * 1 copies [R+0x1c] to R+0x18; then byte R+0x21 <- 0, R+0x22 <- [R+0x26],
 * byte R+0x20 <- 1. Without it byte R+0x20 <- 0. Byte S+0x104 <- 0.
 * EAX: R; EDX: S; ECX: [R+0x26] (re-read) with S+0x103 set, else
 * [S+0x105]. */
static void map_order_commit(hud_ctx *c, uint32_t s, uint32_t *eax, uint32_t *edx, uint32_t *ecx) {
    uint32_t r = map_order_record(c, s);
    *ecx = hr32(c, s + 0x105u);
    if (hr8(c, s + 0x103u)) {
        hw8(c, s + 0x104u, 1);
        uint32_t t = hr32(c, r + 0x26u);
        if (t == 0u) {
            hw32(c, r + 8u, hr32(c, r + 0x14u));
            hw32(c, r, hr32(c, r + 0xcu));
            hw32(c, r + 4u, hr32(c, r + 0x10u));
        } else if (t == 1u) hw32(c, r + 0x18u, hr32(c, r + 0x1cu));
        hw8(c, r + 0x21u, 0);
        *ecx = hr32(c, r + 0x26u);
        hw32(c, r + 0x22u, *ecx);
        hw8(c, r + 0x20u, 1);
    } else hw8(c, r + 0x20u, 0);
    hw8(c, s + 0x104u, 0);
    *eax = r;
    *edx = s;
}

/* 0x44db4c(S, O) (listing 0x44db4c..0x44db9a): the committed order into
 * the order record O. [S+0x105] = -1 copies nothing (EAX S, EDX -1, ECX
 * as at entry). Else with R and its kind t = [R+0x22]: 0 copies R+0..+0xb
 * to O+6 (the dwords +8, +0, +4; EAX O+6, EDX [R+4], ECX [R]); 1 copies
 * [R+0x18] to O+0x12 (EAX that dword, EDX 0, ECX O); other kinds nothing
 * (EAX R, EDX t-1, ECX 42 [S+0x105]). */
static void map_order_copy(hud_ctx *c, uint32_t s, uint32_t o, uint32_t *eax, uint32_t *edx, uint32_t *ecx) {
    uint32_t n = hr32(c, s + 0x105u);
    *eax = s;
    *edx = n;
    if (n == 0xffffffffu) return;
    uint32_t r = s + 0x85u + 42u * n, t = hr32(c, r + 0x22u);
    *eax = r;
    *edx = t - 1u;
    *ecx = 42u * n;
    if (t == 0u) {
        hw32(c, o + 6u + 8u, hr32(c, r + 8u));
        hw32(c, o + 6u, *ecx = hr32(c, r));
        hw32(c, o + 6u + 4u, *edx = hr32(c, r + 4u));
        *eax = o + 6u;
    } else if (t == 1u) {
        *ecx = o;
        *eax = hr32(c, r + 0x18u);
        hw32(c, o + 0x12u, *eax);
    }
}

/* 0x44dd14(S) (listing 0x44dd14..0x44dd71): the order text's background:
 * canvas_push 0x4311e0 (its EAX G), the fill record {0, sign-extended
 * word [0x4d3ba2]} in the frame (-8) at G+0x22c, the fill 0x486704 and
 * the panel copy 0x43122c(1, ., 0) of the rectangle [S+0x13a]+4, byte
 * S+0x144 <- 0, canvas_pop 0x431210. EAX/EDX: canvas_pop's; ECX: the
 * colour (the frame's -4 popped). */
static int map_order_text(hud_ctx *c, uint32_t s, es2_sim_result *out) {
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 8u), g = 0;
    if (!f) return no_frame(c, VA_ORDER_TEXT, "order text");
    int bad = HCALL0(c, VA_CANVAS_PUSH, &g);
    if (!bad) {
        uint32_t colour = hsx16(hr16(c, 0x004d3ba2u));
        hw32(c, f, 0);
        hw32(c, f + 4u, colour);
        hw32(c, g + 0x22cu, f);
        bad = HCALL(c, VA_RECT_FILL, NULL, hr32(c, s + 0x13au) + 4u) ||
              HCALL(c, VA_PANEL_COPY, NULL, 1u, hr32(c, s + 0x13au) + 4u, 0u);
        if (!bad) {
            hw8(c, s + 0x144u, 0);
            bad = hud_call_regs(c, VA_CANVAS_POP, NULL, 0, NULL, out);
            out->ecx = colour;
        }
    }
    hud_frame_pop(c, top);
    return bad;
}

/* 0x44dc6c(S, text, mode) (listing 0x44dc6c..0x44dd10): the order label.
 * A null text picks [0x4d1638 + 4 j]: with byte S+0x103 set j = 3 for the
 * map mode [S+0x5d] = 0xb, 2 for 0xc..0xe, else the entry EAX (the
 * original leaves EAX unset on that path); without it j = 0 for [S+0x105]
 * = -1, else 1. Then 0x44dd14(S), S+0x140 <- text, canvas_push, the label
 * 0x438920([S+0x13a], [S+0x140]), the panel copy 0x43122c(1, [S+0x13a]+4,
 * 0), canvas_pop; mode 1 sets word S+0x13e <- 6. EAX/EDX/ECX:
 * canvas_pop's. */
static int map_order_label(hud_ctx *c, uint32_t s, uint32_t text, uint32_t mode, uint32_t entry_eax, es2_sim_result *out) {
    if (!text) {
        uint32_t j = entry_eax;
        if (hr8(c, s + 0x103u)) {
            uint32_t m = hr32(c, s + 0x5du);
            if (m == 0xbu) j = 3;
            else if (m - 0xcu < 3u) j = 2;
        } else j = hr32(c, s + 0x105u) == 0xffffffffu ? 0u : 1u;
        text = hr32(c, 0x004d1638u + 4u * j);
    }
    es2_sim_result r;
    if (call_regs(c, VA_ORDER_TEXT, (const uint32_t[]){s}, 1, 0, 0, 0, &r)) return 1;
    hw32(c, s + 0x140u, text);
    if (HCALL0(c, VA_CANVAS_PUSH, NULL) || HCALL(c, VA_LABEL, NULL, hr32(c, s + 0x13au), hr32(c, s + 0x140u)) ||
        HCALL(c, VA_PANEL_COPY, NULL, 1u, hr32(c, s + 0x13au) + 4u, 0u) || hud_call_regs(c, VA_CANVAS_POP, NULL, 0, NULL, out))
        return 1;
    if (mode == 1u) hw16(c, s + 0x13eu, 6);
    return 0;
}

/* 0x433e3c(n) (listing 0x433e3c..0x433ee6): the panel cursor. With byte
 * 0x49b105 set and n not the current [0x49b630]: P = [0x49b088], P+0x23a
 * <- d = [0x49b08c + 4n]; unless P+0x232 is [P+0x226], the cursor is set
 * with the image [[P+0x23a]+4] (+4 when [d+4] is not null) and the hot
 * spot [P+0x23a]+8: 0x48992e with word [0x4d2566] set, else 0x486e70;
 * [0x49b630] <- n. EAX/EDX/ECX are not part of the contract (callers
 * ignore them). */
static int cursor_select(hud_ctx *c, uint32_t n) {
    if (!hr8(c, 0x0049b105u) || n == hr32(c, 0x0049b630u)) return 0;
    uint32_t p = hr32(c, VA_PANEL), d = hr32(c, 0x0049b08cu + 4u * n);
    hw32(c, p + 0x23au, d);
    if (p + 0x232u != hr32(c, p + 0x226u)) {
        uint32_t x = hr32(c, p + 0x23au);
        if (hr16(c, 0x004d2566u)) {
            uint32_t image = hr32(c, d + 4u) ? hr32(c, hr32(c, p + 0x23au) + 4u) + 4u : hr32(c, hr32(c, p + 0x23au) + 4u);
            if (HCALL(c, VA_CURSOR_SET_A, NULL, image, x + 8u)) return 1;
        } else {
            uint32_t image = hr32(c, x + 4u) ? hr32(c, x + 4u) + 4u : hr32(c, x + 4u);
            if (HCALL(c, VA_CURSOR_SET_B, NULL, image, x + 8u)) return 1;
        }
    }
    hw32(c, 0x0049b630u, n);
    return 0;
}

/* 0x44a748(H, i) (listing 0x44a748..0x44a7e4): the radio selection. The
 * lamp of the old selection [H+0x517] (button W = [H+0x40 + 4 old], not
 * for -1): a lit byte W+0x40 goes out, and W's slot +4 runs either way.
 * H+0x52b <- the old selection, H+0x517 <- i. For i != -1: button i's
 * lamp (W = [H+0x40 + 4i]) is lit and its slot +4 runs unless it was lit;
 * the DialogPort 0x433158 receives (slot 0 of its table) the message at
 * the frame's -0x18: word 0x22 at +0, the radio record [0x4d03c8 + 4i] at
 * +2. The original writes only these 6 bytes; its +6..+0x15 are whatever
 * the stack held (they reach the queue entry through 0x434e8c's copy; no
 * reader of them was found); here they are 0 (a zero-filled frame).
 * Last wingman_select 0x44da98([H+0x101], [H+0x517]). EAX/EDX/ECX:
 * wingman_select's. */
static int hdd_select(hud_ctx *c, uint32_t h, uint32_t i, es2_sim_result *out) {
    uint32_t old = hr32(c, h + 0x517u);
    if (old != 0xffffffffu) {
        uint32_t w = hr32(c, h + 0x40u + 4u * old);
        if (hr8(c, w + 0x40u)) hw8(c, w + 0x40u, 0);
        if (hud_call(c, hr32(c, hr32(c, w + 0x17u) + 4u), (const uint32_t[]){w}, 1, NULL)) return 1;
    }
    hw32(c, h + 0x52bu, hr32(c, h + 0x517u));
    hw32(c, h + 0x517u, i);
    uint32_t eax = i;
    if (i != 0xffffffffu) {
        uint32_t w = hr32(c, h + 0x40u + 4u * i);
        if (!hr8(c, w + 0x40u)) {
            hw8(c, w + 0x40u, 1);
            if (hud_call(c, hr32(c, hr32(c, w + 0x17u) + 4u), (const uint32_t[]){w}, 1, NULL)) return 1;
        }
        uint32_t port = 0;
        if (HCALL0(c, VA_PORT_A, &port)) return 1;
        uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0x18u);
        if (!f) return no_frame(c, VA_HDD_SELECT, "radio selection");
        hw16(c, f, 0x22u);
        hw32(c, f + 2u, hr32(c, 0x004d03c8u + 4u * i));
        int bad = hud_call(c, hr32(c, hr32(c, port)), (const uint32_t[]){port, f}, 2, &eax);
        hud_frame_pop(c, top);
        if (bad) return 1;
    }
    return call_regs(c, VA_WINGMAN_SELECT, (const uint32_t[]){hr32(c, h + 0x101u), hr32(c, h + 0x517u)}, 2, eax,
                     hr32(c, h + 0x517u), hr32(c, h + 0x101u), out);
}

/* The executor's lamp block (listing 0x44a381..0x44a43a) after a target
 * step on page 1: lit = [H+0x524] set with byte [[H+0x524]+0xa3]; a lit
 * lamp H+0x51f without a lit target goes out (colour: sign-extended word
 * [0x4d3b96]), an unlit one with a lit target comes on (word [0x4d3b9a]);
 * a nonzero colour fills the rectangle H+0xf1 (the fill record {0, colour}
 * at the frame's -8 into GC+0x22c, GC = [0x6c5f70] read at the
 * executor's entry) and copies it to the panel with the third dword one
 * larger (0x43122c(1, the frame's -0x20, 0)). Byte H+0x110 <- 1, byte
 * H+0xb0 <- 2. */
static int lamp_block(hud_ctx *c, uint32_t h, uint32_t gc, uint32_t f) {
    uint32_t t = hr32(c, h + 0x524u), colour = 0;
    hw32(c, f + 0x1cu, 0);
    int lit = t && hr8(c, t + 0xa3u);
    if (hr8(c, h + 0x51fu) && !lit) {
        hw8(c, h + 0x51fu, 0);
        colour = hsx16(hr16(c, 0x004d3b96u));
        hw32(c, f + 0x1cu, colour);
    } else if (!hr8(c, h + 0x51fu) && lit) {
        hw8(c, h + 0x51fu, 1);
        colour = hsx16(hr16(c, 0x004d3b9au));
        hw32(c, f + 0x1cu, colour);
    }
    if (hr32(c, f + 0x1cu)) {
        hw32(c, gc + 0x22cu, f + 0x18u);
        if (HCALL(c, VA_RECT_FILL, NULL, h + 0xf1u)) return 1;
        for (uint32_t k = 0; k < 16u; k += 4u) hw32(c, f + k, hr32(c, h + 0xf1u + k));
        hw32(c, f + 8u, hr32(c, f + 8u) + 1u);
        if (HCALL(c, VA_PANEL_COPY, NULL, 1u, f, 0u)) return 1;
    }
    hw8(c, h + 0x110u, 1);
    hw8(c, h + 0xb0u, 2);
    return 0;
}

/* 0x44a1a0(H) (listing 0x44a1a0..0x44a609; the jump table of 15 cases at
 * 0x44a1f7): the HDD button executor, run by the HDD frame. The request i
 * = sign-extended byte H+0x528 (set by 0x44a174): -1 does nothing; with
 * byte H+0x76 set (the HDD is minimised) only buttons 0 and 1 run (i - 1
 * > 0 signed returns, keeping the request). Otherwise the case runs and
 * byte H+0x528 <- -1 (i > 14 unsigned runs no case). "Mark n" below is
 * byte H+0x10e+n <- 1 (n = 1: page 0, 0: page 1 as the listing writes
 * H+0x10f and H+0x10e) with byte H+0xb0 <- 2; page p = [H+0x109];
 * S = [H+0x101] (the map page), X = [H+0x105] (the status page).
 *  0/1: hdd_page 0x44a60c(H, 0/1).
 *  2/3: p 0: 0x44d0c0/0x44d124(S) (map scroll down/up), mark H+0x10f;
 *       p 1: 0x450bf4/0x450c18(X) (status mode next/previous), mark H+0x10e.
 *  4/5: p 0: 0x44cff8/0x44d05c(S) (map scroll left/right), mark H+0x10f;
 *       p 1: H+0x524 <- 0x44ba08/0x44b9b0(H) (target previous/next), the
 *       lamp block.
 *  6/7: p 0 only: 0x44cf90/0x44cfc4(S) (zoom in/out), mark H+0x10f.
 *  8: nothing. 9: p 1 only: 0x450bf4(X), mark H+0x10e.
 *  10..12: p 0, or p 1 after hdd_page(H, 0): j = i - 10 (i re-read); j
 *       not the selection [H+0x517], V = [0x4d03c8 + 4j] not null and byte
 *       V+0x99 clear: 0x44a748(H, j), and with byte S+0x103 (S re-read)
 *       the cursor 0x433e3c(2).
 *  13: p 0 only: byte S+0x104 clear: 0x44dc6c(S, 0, 1) (entry EAX 0);
 *       else 0x44db9c(S) and, with a selection, word [0x4d03d4] <- word
 *       S+0x5d, 0x44db4c([H+0x101], 0x4d03d4) (entry ECX S, from the
 *       listing's POP ECX), wingman_order 0x431610([H+0x14], 0x4d03d4,
 *       ECX with its low word replaced by word [H+0x517]: the high word is
 *       0x44db4c's exit ECX), the cursor 0x433e3c(0), order_cancel
 *       0x44dc10(S), 0x44a748(H, -1).
 *  14: order_cancel 0x44dc10([H+0x101]), 0x433e3c(0), 0x44a748(H, -1).
 * The registers it leaves are not part of the contract (the caller
 * ignores them). */
static int hdd_execute(hud_ctx *c, uint32_t h) {
    uint32_t i = hsx8(hr8(c, h + 0x528u));
    if (i == 0xffffffffu) return 0;
    if (hr8(c, h + 0x76u) && (int32_t)(i - 1u) > 0) return 0;
    uint32_t gc = hr32(c, VA_GC);
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0x20u);   /* the frame's -0x20..-1 */
    if (!f) return no_frame(c, VA_HDD_EXECUTE, "HDD button executor");
    hw32(c, f + 0x18u, 0);
    hw32(c, f + 0x10u, 4u);
    int bad = 0;
    uint32_t page = hr32(c, h + 0x109u), eax = 0;
    es2_sim_result r;
    switch (i) {
    case 0: case 1:
        bad = HCALL(c, VA_HDD_PAGE, NULL, h, i);
        break;
    case 2: case 3:
        if (page == 0u) {
            bad = HCALL(c, i == 2u ? VA_MAP_SCROLL_DOWN : VA_MAP_SCROLL_UP, NULL, hr32(c, h + 0x101u));
            if (!bad) { hw8(c, h + 0x10fu, 1); hw8(c, h + 0xb0u, 2); }
        } else if (page == 1u) {
            bad = HCALL(c, i == 2u ? VA_STATUS_NEXT : VA_STATUS_PREV, NULL, hr32(c, h + 0x105u));
            if (!bad) { hw8(c, h + 0x10eu, 1); hw8(c, h + 0xb0u, 2); }
        }
        break;
    case 4: case 5:
        if (page == 0u) {
            bad = HCALL(c, i == 4u ? VA_MAP_SCROLL_LEFT : VA_MAP_SCROLL_RIGHT, NULL, hr32(c, h + 0x101u));
            if (!bad) { hw8(c, h + 0x10fu, 1); hw8(c, h + 0xb0u, 2); }
        } else if (page == 1u) {
            bad = HCALL(c, i == 4u ? VA_TARGET_PREV : VA_TARGET_NEXT, &eax, h);
            if (!bad) {
                hw32(c, h + 0x524u, eax);
                bad = lamp_block(c, h, gc, f);
            }
        }
        break;
    case 6: case 7:
        if (page == 0u) {
            bad = HCALL(c, i == 6u ? VA_MAP_ZOOM_IN : VA_MAP_ZOOM_OUT, NULL, hr32(c, h + 0x101u));
            if (!bad) { hw8(c, h + 0x10fu, 1); hw8(c, h + 0xb0u, 2); }
        }
        break;
    case 9:
        if (page == 1u) {
            bad = HCALL(c, VA_STATUS_NEXT, NULL, hr32(c, h + 0x105u));
            if (!bad) { hw8(c, h + 0x10eu, 1); hw8(c, h + 0xb0u, 2); }
        }
        break;
    case 10: case 11: case 12:
        if (page > 1u) break;
        if (page == 1u && HCALL(c, VA_HDD_PAGE, NULL, h, 0u)) { bad = 1; break; }
        {
            uint32_t j = hsx8(hr8(c, h + 0x528u)) - 10u, v = hr32(c, 0x004d03c8u + 4u * j);
            if (j == hr32(c, h + 0x517u) || !v || hr8(c, v + 0x99u)) break;
            bad = call_regs(c, VA_HDD_SELECT, (const uint32_t[]){h, j}, 2, j, v, 0, &r);
            if (!bad && hr8(c, hr32(c, h + 0x101u) + 0x103u)) bad = HCALL(c, VA_CURSOR, NULL, 2u);
        }
        break;
    case 13:
        if (page != 0u) break;
        {
            uint32_t s = hr32(c, h + 0x101u);
            if (!hr8(c, s + 0x104u)) {
                bad = call_regs(c, VA_ORDER_LABEL, (const uint32_t[]){s, 0u, 1u}, 3, 0, i, 0, &r);
                break;
            }
            if (HCALL(c, VA_ORDER_COMMIT, &eax, s)) { bad = 1; break; }
            if (hr32(c, h + 0x517u) == 0xffffffffu) break;
            hw16(c, VA_ORDER_RECORD, hr16(c, s + 0x5du));
            bad = call_regs(c, VA_ORDER_COPY, (const uint32_t[]){hr32(c, h + 0x101u), VA_ORDER_RECORD}, 2,
                            (eax & 0xffff0000u) | hr16(c, s + 0x5du), hr32(c, h + 0x101u), s, &r);
            if (bad) break;
            uint32_t word = (r.ecx & 0xffff0000u) | hr16(c, h + 0x517u);
            bad = call_regs(c, VA_WINGMAN_ORDER, (const uint32_t[]){hr32(c, h + 0x14u), VA_ORDER_RECORD, word}, 3,
                            hr32(c, h + 0x14u), r.edx, word, &r) ||
                  HCALL(c, VA_CURSOR, NULL, 0u) || HCALL(c, VA_ORDER_CANCEL, NULL, s) ||
                  call_regs(c, VA_HDD_SELECT, (const uint32_t[]){h, 0xffffffffu}, 2, 0, 0, 0, &r);
        }
        break;
    case 14:
        bad = HCALL(c, VA_ORDER_CANCEL, NULL, hr32(c, h + 0x101u)) || HCALL(c, VA_CURSOR, NULL, 0u) ||
              call_regs(c, VA_HDD_SELECT, (const uint32_t[]){h, 0xffffffffu}, 2, 0, 0, 0, &r);
        break;
    default:
        break;
    }
    hud_frame_pop(c, top);
    if (bad) return 1;
    hw8(c, h + 0x528u, 0xffu);
    return 0;
}

/* 0x44d24c(M, src, dst) (listing 0x44d24c..0x44d285): a map point to the
 * screen: dst <- src (the dwords +0, +4 in that order), then dst.x +=
 * [M+0x71], dst.x += sar1([M+0x79] - [M+0x71]), dst.y += [M+0x75], dst.y
 * += sar1([M+0x7d] - [M+0x75]) (each a read-modify-write of dst, each M
 * dword re-read). EAX: M; EDX: dst; ECX: the last half, sar1([M+0x7d] -
 * [M+0x75]). */
static void map_to_screen(hud_ctx *c, uint32_t m, uint32_t src, uint32_t dst, uint32_t *ecx) {
    hw32(c, dst, hr32(c, src));
    hw32(c, dst + 4u, hr32(c, src + 4u));
    hw32(c, dst, hr32(c, dst) + hr32(c, m + 0x71u));
    uint32_t half = (uint32_t)((int32_t)(hr32(c, m + 0x79u) - hr32(c, m + 0x71u)) >> 1);
    hw32(c, dst, hr32(c, dst) + half);
    hw32(c, dst + 4u, hr32(c, dst + 4u) + hr32(c, m + 0x75u));
    half = (uint32_t)((int32_t)(hr32(c, m + 0x7du) - hr32(c, m + 0x75u)) >> 1);
    hw32(c, dst + 4u, hr32(c, dst + 4u) + half);
    *ecx = half;
}

/* 0x44d3cc(M, gadget, pt) (listing 0x44d3cc..0x44d44c): the map's drag
 * start: byte M+0x14d <- 1, byte M+0x15b <- byte [0x49dbe0], M+0x14e <-
 * gadget, M+0x152 <- pt (the dwords +0, +4 in that order), then M+0x152
 * -= -[0x4d2556] and M+0x156 -= -[0x4d255a] (the negated shifts in the
 * frame's -8/-4); the HDD G = [[0x49d6b0]+8]: byte G+0x10f <- 1, byte
 * G+0xb0 <- 2. EAX: G; EDX and ECX: -[0x4d255a] (ECX popped from the
 * frame's -4). */
static uint32_t map_drag_start(hud_ctx *c, uint32_t m, uint32_t gadget, uint32_t pt, uint32_t *edx) {
    hw8(c, m + 0x14du, 1);
    hw8(c, m + 0x15bu, hr8(c, 0x0049dbe0u));
    hw32(c, m + 0x14eu, gadget);
    hw32(c, m + 0x152u, hr32(c, pt));
    hw32(c, m + 0x156u, hr32(c, pt + 4u));
    uint32_t dx = 0u - hr32(c, 0x004d2556u), dy = 0u - hr32(c, 0x004d255au);
    hw32(c, m + 0x152u, hr32(c, m + 0x152u) - dx);
    hw32(c, m + 0x156u, hr32(c, m + 0x156u) - dy);
    uint32_t g = hr32(c, hr32(c, 0x0049d6b0u) + 8u);
    hw8(c, g + 0x10fu, 1);
    hw8(c, g + 0xb0u, 2);
    *edx = dy;
    return g;
}

/* ---- their service rows */
void es2_sim_service_hdd_execute(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (hdd_execute(&c, sc->slots[0])) hud_failed(sc);
}
void es2_sim_service_hdd_select(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    es2_sim_result r;
    if (hdd_select(&c, sc->slots[0], sc->slots[1], &r)) { hud_failed(sc); return; }
    sc->eax = r.eax; sc->edx = r.edx; sc->ecx = r.ecx;
}
void es2_sim_service_map_scroll_down(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    map_scroll(&c, sc->slots[0], 1, 4u);
    sc->eax = sc->slots[0];
}
void es2_sim_service_map_scroll_up(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    map_scroll(&c, sc->slots[0], 0, 4u);
    sc->eax = sc->slots[0];
}
void es2_sim_service_map_zoom_in(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t eax = 0;
    if (map_zoom_in(&c, sc->slots[0], &eax)) hud_failed(sc);
    else sc->eax = eax;
}
void es2_sim_service_status_next(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    es2_sim_result r;
    if (status_step(&c, sc->slots[0], 1, &r)) { hud_failed(sc); return; }
    sc->eax = r.eax; sc->edx = r.edx; sc->ecx = r.ecx;
}
void es2_sim_service_status_prev(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    es2_sim_result r;
    if (status_step(&c, sc->slots[0], 0, &r)) { hud_failed(sc); return; }
    sc->eax = r.eax; sc->edx = r.edx; sc->ecx = r.ecx;
}
void es2_sim_service_target_prev(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    target_step(&c, sc->slots[0], 0, &sc->eax, &sc->edx, &sc->ecx);
}
void es2_sim_service_target_next(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    target_step(&c, sc->slots[0], 1, &sc->eax, &sc->edx, &sc->ecx);
}
void es2_sim_service_order_commit(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    map_order_commit(&c, sc->slots[0], &sc->eax, &sc->edx, &sc->ecx);
}
void es2_sim_service_order_copy(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    map_order_copy(&c, sc->slots[0], sc->slots[1], &sc->eax, &sc->edx, &sc->ecx);
}
void es2_sim_service_order_text(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    es2_sim_result r;
    if (map_order_text(&c, sc->slots[0], &r)) { hud_failed(sc); return; }
    sc->eax = r.eax; sc->edx = r.edx; sc->ecx = r.ecx;
}
void es2_sim_service_order_label(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    es2_sim_result r;
    if (map_order_label(&c, sc->slots[0], sc->slots[1], sc->slots[2], sc->eax, &r)) { hud_failed(sc); return; }
    sc->eax = r.eax; sc->edx = r.edx; sc->ecx = r.ecx;
}
void es2_sim_service_cursor_select(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (cursor_select(&c, sc->slots[0])) hud_failed(sc);
}
void es2_sim_service_map_point_screen(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t ecx = 0;
    map_to_screen(&c, sc->slots[0], sc->slots[1], sc->slots[2], &ecx);
    sc->eax = sc->slots[0]; sc->edx = sc->slots[2]; sc->ecx = ecx;
}
void es2_sim_service_map_drag_start(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t edx = 0;
    sc->eax = map_drag_start(&c, sc->slots[0], sc->slots[1], sc->slots[2], &edx);
    sc->edx = edx; sc->ecx = edx;
}

/* ==== The HDD radio panel 0x44b620. */

#define VA_LIP_PASS 0x004632c4u
#define VA_RADIO_DRAW 0x0044aea0u       /* the radio record's draw (hud_draw.c's static copy) */
#define VA_RADIO_FRAME 0x0044b3dcu      /* the radio record's frame (no own code) */
#define VA_RADIO_TALK 0x0044b344u       /* (no own code) */
#define VA_RADIO_LINE 0x0044b148u       /* (no own code) */
#define VA_PORT_DONE 0x00435b14u
#define VA_PORT_MARK 0x00435b38u
#define VA_VOICE_ACTIVE 0x00463400u     /* sfx hook */
#define VA_LIP_VALUE 0x0046356cu
#define VA_SFX_PLAYING 0x00462af0u
#define VA_SFX_PLAY 0x00462830u         /* sfx hook */

/* 0x44b620(H) (listing 0x44b620..0x44b861): the HDD radio panel, run by
 * the HDD frame. 0x4632c4(), now = the clock (site 0x44b631). Per radio
 * record k < 3 (S = H+0x12d + 0x14e k, V = [0x4d03c8 + 4k]) with an actor
 * A = [S+0x137]: A = [[0x49d230]+0x766] with byte S+0x147 set or state
 * [S+0x13b] 0 clears [[0x49d230]+0x766]. Then by the state:
 *  0: with [S+0x13f] set or byte 0x49d4f4 set, the draw 0x44aea0(H, k)
 *     and the panel copy 0x43122c(1, S+0x1d, 0); byte 0x49d4f4 <- the
 *     copy's AL ^ 1.
 *  3: now below [S+0x143] (unsigned) runs the frame 0x44b3dc(H, S); else
 *     the state becomes 0 and continues as 0.
 *  1: now below [S+0x143] or byte S+0x147 set runs 0x44b3dc(H, S); else
 *     the state becomes 2 and continues as 2.
 *  2: a new state (against [S+0x13f]): 0x435b14(port, [S+0x14a]) with
 *     the port 0x433158; the voice test 0x463400([S+0x12f]) (AL) picks
 *     0x46356c([S+0x12f]) or -1; byte S+0x12d = 0x25 sets S+0x143 <- now
 *     + 5. An old state: e = 0x46356c([S+0x12f]). A word e of -1:
 *     0x435b38(port, [S+0x14a]); byte S+0x12d = 0x25 sets byte S+0x147
 *     and state 1; else state 3 with S+0x143 <- now + 0x14, the sound
 *     0x462830(0x1c) unless 0x462af0(0x1c) says it plays, and byte
 *     V+0x99 set sets byte S+0x147; then 0x44b3dc(H, S). Otherwise byte
 *     S+0x12d = 0x25 runs 0x44b344(H, S), else 0x44b148(H, S, e).
 * Other states run nothing. Each record with an actor ends with S+0x13f
 * <- [S+0x13b]. The registers it leaves are not part of the contract (the
 * HDD frame ignores them). */
static int radio_panel(hud_ctx *c, uint32_t h) {
    uint32_t now = 0, eax = 0, port = 0;
    if (HCALL0(c, VA_LIP_PASS, NULL) || hud_clock(c, 0x0044b631u, &now)) return 1;
    for (uint32_t k = 0; k < 3u; ++k) {
        uint32_t s = h + 0x12du + 0x14eu * k, vp = 0x004d03c8u + 4u * k, a = hr32(c, s + 0x137u);
        if (!a) continue;
        if (a == hr32(c, hr32(c, 0x0049d230u) + 0x766u) && (hr8(c, s + 0x147u) || !hr32(c, s + 0x13bu)))
            hw32(c, hr32(c, 0x0049d230u) + 0x766u, 0);
        uint32_t state = hr32(c, s + 0x13bu);
        int frame = 0;
        if (state == 3u) {
            if (now < hr32(c, s + 0x143u)) frame = 1;
            else {
                hw32(c, s + 0x13bu, 0);
                state = 0;
            }
        } else if (state == 1u) {
            if (now < hr32(c, s + 0x143u) || hr8(c, s + 0x147u)) frame = 1;
            else {
                hw32(c, s + 0x13bu, 2);
                state = 2;
            }
        }
        if (frame) {
            if (HCALL(c, VA_RADIO_FRAME, NULL, h, s)) return 1;
        } else if (state == 0u) {
            if (hr32(c, s + 0x13fu) || hr8(c, 0x0049d4f4u)) {
                if (HCALL(c, VA_RADIO_DRAW, NULL, h, k) || HCALL(c, VA_PANEL_COPY, &eax, 1u, s + 0x1du, 0u)) return 1;
                hw8(c, 0x0049d4f4u, (uint8_t)(eax ^ 1u));
            }
        } else if (state == 2u) {
            uint32_t e;
            if (hr32(c, s + 0x13fu) != hr32(c, s + 0x13bu)) {
                if (HCALL0(c, VA_PORT_A, &port) || HCALL(c, VA_PORT_DONE, NULL, port, hr32(c, s + 0x14au)) ||
                    HCALL(c, VA_VOICE_ACTIVE, &eax, hr32(c, s + 0x12fu)))
                    return 1;
                e = 0xffffffffu;
                if ((uint8_t)eax && HCALL(c, VA_LIP_VALUE, &e, hr32(c, s + 0x12fu))) return 1;
                if (hr8(c, s + 0x12du) == 0x25u) hw32(c, s + 0x143u, now + 5u);
            } else if (HCALL(c, VA_LIP_VALUE, &e, hr32(c, s + 0x12fu))) return 1;
            if ((e & 0xffffu) != 0xffffu) {
                if (hr8(c, s + 0x12du) == 0x25u ? HCALL(c, VA_RADIO_TALK, NULL, h, s) : HCALL(c, VA_RADIO_LINE, NULL, h, s, e)) return 1;
            } else {
                if (HCALL0(c, VA_PORT_A, &port) || HCALL(c, VA_PORT_MARK, NULL, port, hr32(c, s + 0x14au))) return 1;
                if (hr8(c, s + 0x12du) == 0x25u) {
                    hw8(c, s + 0x147u, 1);
                    hw32(c, s + 0x13bu, 1);
                } else {
                    hw32(c, s + 0x13bu, 3);
                    hw32(c, s + 0x143u, now + 0x14u);
                    if (HCALL(c, VA_SFX_PLAYING, &eax, 0x1cu)) return 1;
                    if (!eax && HCALL(c, VA_SFX_PLAY, NULL, 0x1cu)) return 1;
                    if (hr8(c, hr32(c, vp) + 0x99u)) hw8(c, s + 0x147u, 1);
                }
                if (HCALL(c, VA_RADIO_FRAME, NULL, h, s)) return 1;
            }
        }
        hw32(c, s + 0x13fu, hr32(c, s + 0x13bu));
    }
    return 0;
}
void es2_sim_service_hdd_radio(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (radio_panel(&c, sc->slots[0])) hud_failed(sc);
}

/* ==== The MFD camera page frame 0x44060c. */

#define VA_GC_SELECT 0x00480d44u
#define VA_CAMERA_CURRENT 0x0045edf8u   /* camera_view hook */
#define VA_VIEW_SETUP 0x0048c2e4u
#define VA_MAP_TERRAIN 0x0045030cu
#define VA_VIEW_FILL 0x00483a04u        /* (no own code) */

/* 0x44060c(X) (listing 0x44060c..0x440814): the camera page's frame (MFD
 * page slot +4). Nothing with byte [0x4cf99c]+0x1c set. Else, with GC =
 * [0x6c5f70] and the frame f = EBP-0x38: the fill record {0, sign-extended
 * word [0x4d3ba2]} at f+0x30 into GC+0x22c, the fill 0x486704([X+4]). The
 * view V = [X+0x10] (re-read where the listing re-reads it): V+0x210 <-
 * the dwords X+0x18, X+0x1c, V+0x218 <- X+0x20, X+0x24, both points less
 * GC+0x220/+0x224, V+4..V+0x207 <- GC+4.. (a forward dword copy), V+0x208
 * <- GC+0x208; gc_select 0x480d44(V); camera_current 0x45edf8(). The
 * player's point P+0x26 (P = [[0x49b088]+0x203]) with its z replaced by
 * 200000 goes to the camera C = [X+0x14] at C+4 (the dwords +8, +0, +4 in
 * that order), C+0x10 <- word [0x49c454] (high word 0), word C+0x14 <- 0,
 * after V+0x20c <- 0; view_setup 0x48c2e4(C); map_terrain 0x45030c with
 * the dword (C & 0xffff0000) | word [P+0x10] (the listing's POP ECX of C
 * with CX loaded). V+0x20c <- 1, the fill record {0, sign-extended word
 * [0x4d3b9c]} at f+0x1c into V+0x228; 0x483a04(f+0x14, f+0xc) with the
 * points {-1 << s, 0} and {1 << s, 0} for s = word [0x4d253a], then with
 * {0, -1 << t} and {0, 1 << t} for t = word [0x4d253c] (shift counts mod
 * 32); gc_select(GC); the panel copy 0x43122c(1, [X+4], 0). The registers
 * it leaves are not part of the contract (the MFD frame ignores them). */
static int camera_frame(hud_ctx *c, uint32_t x) {
    if (hr8(c, hr32(c, VA_DISPLAY) + 0x1cu)) return 0;
    uint32_t gc = hr32(c, VA_GC);
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0x38u);
    if (!f) return no_frame(c, 0x0044060cu, "camera page frame");
    int bad = 1;
    hw32(c, f + 0x30u, 0);
    hw32(c, gc + 0x22cu, f + 0x30u);
    hw32(c, f + 0x34u, hsx16(hr16(c, 0x004d3ba2u)));
    if (HCALL(c, VA_RECT_FILL, NULL, hr32(c, x + 4u))) goto done;
    uint32_t v = hr32(c, x + 0x10u);
    hw32(c, v + 0x210u, hr32(c, x + 0x18u));
    hw32(c, v + 0x214u, hr32(c, x + 0x1cu));
    hw32(c, f + 0x2cu, x + 0x20u);
    hw32(c, v + 0x218u, hr32(c, hr32(c, f + 0x2cu)));
    hw32(c, v + 0x21cu, hr32(c, hr32(c, f + 0x2cu) + 4u));
    v = hr32(c, x + 0x10u);
    hw32(c, v + 0x210u, hr32(c, v + 0x210u) - hr32(c, gc + 0x220u));
    hw32(c, v + 0x214u, hr32(c, v + 0x214u) - hr32(c, gc + 0x224u));
    v = hr32(c, x + 0x10u);
    hw32(c, v + 0x218u, hr32(c, v + 0x218u) - hr32(c, gc + 0x220u));
    hw32(c, v + 0x21cu, hr32(c, v + 0x21cu) - hr32(c, gc + 0x224u));
    v = hr32(c, x + 0x10u);
    for (uint32_t i = 0; i < 0x204u; i += 4u) hw32(c, v + 4u + i, hr32(c, gc + 4u + i));
    hw32(c, v + 0x208u, hr32(c, gc + 0x208u));
    if (HCALL(c, VA_GC_SELECT, NULL, v) || HCALL0(c, VA_CAMERA_CURRENT, NULL)) goto done;
    uint32_t p = hr32(c, hr32(c, VA_PANEL) + 0x203u) + 0x26u;
    hw32(c, f, hr32(c, p));
    hw32(c, f + 4u, hr32(c, p + 4u));
    hw32(c, f + 8u, hr32(c, p + 8u));
    hw32(c, f + 8u, 0x30d40u);
    hw16(c, f + 0x24u, hr16(c, 0x0049c454u));
    hw16(c, f + 0x28u, 0);
    hw16(c, f + 0x26u, 0);
    hw32(c, hr32(c, x + 0x10u) + 0x20cu, 0);
    uint32_t cam = hr32(c, x + 0x14u) + 4u;
    hw32(c, cam + 8u, hr32(c, f + 8u));
    hw32(c, cam, hr32(c, f));
    hw32(c, cam + 4u, hr32(c, f + 4u));
    cam = hr32(c, x + 0x14u);
    hw32(c, cam + 0x10u, hr32(c, f + 0x24u));
    hw16(c, cam + 0x14u, hr16(c, f + 0x28u));
    if (HCALL(c, VA_VIEW_SETUP, NULL, cam)) goto done;
    uint32_t arg = (cam & 0xffff0000u) | hr16(c, hr32(c, hr32(c, VA_PANEL) + 0x203u) + 0x10u);
    if (HCALL(c, VA_MAP_TERRAIN, NULL, arg)) goto done;
    v = hr32(c, x + 0x10u);
    hw32(c, v + 0x20cu, 1);
    hw32(c, f + 0x1cu, 0);
    hw32(c, v + 0x228u, f + 0x1cu);
    uint32_t s = hr16(c, 0x004d253au) & 31u;
    hw32(c, f + 0x20u, hsx16(hr16(c, 0x004d3b9cu)));
    hw32(c, f + 0x14u, 0xffffffffu << s);
    hw32(c, f + 0x18u, 0);
    hw32(c, f + 0xcu, 1u << s);
    hw32(c, f + 0x10u, 0);
    if (HCALL(c, VA_VIEW_FILL, NULL, f + 0x14u, f + 0xcu)) goto done;
    hw32(c, f + 0x14u, 0);
    s = hr16(c, 0x004d253cu) & 31u;
    hw32(c, f + 0x18u, 0xffffffffu << s);
    hw32(c, f + 0xcu, 0);
    hw32(c, f + 0x10u, 1u << s);
    if (HCALL(c, VA_VIEW_FILL, NULL, f + 0x14u, f + 0xcu) || HCALL(c, VA_GC_SELECT, NULL, gc) ||
        HCALL(c, VA_PANEL_COPY, NULL, 1u, hr32(c, x + 4u), 0u))
        goto done;
    bad = 0;
done:
    hud_frame_pop(c, top);
    return bad;
}
void es2_sim_service_mfd_camera_frame(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (camera_frame(&c, sc->slots[0])) hud_failed(sc);
}

/* ==== The MFD keys 0x4469e8. */

#define VA_BUTTON_PRESS 0x00438d9cu     /* button_press (hook) */
#define VA_MFD_RANGE 0x00446ff0u        /* mfd_range (hook) */
#define VA_MFD_NEXT 0x00447070u         /* (no own code) */
#define VA_MFD_PREV 0x0044703cu         /* (no own code) */
#define VA_MFD_ORDER_KEY 0x00447158u    /* (no own code) */

/* 0x4469e8(G, key) (listing 0x4469e8..0x446c36; the byte table 0x446b78
 * and the jump table 0x446b95): the MFD's key handler (a vtable slot).
 * Not visible (panel_visible 0x431410(G+0xeb), AL) returns 0. With the
 * page p = [G+0xc1] and the button table B = 0x49cbd8 + 13 p, by the
 * key's low word: 0x20, 0x213, 0x214, 0x2d press the buttons 7, 8, 9, 10
 * (button_press 0x438d9c(G, n, 1)) when byte B+n is set; 0x213 without
 * it runs mfd_range 0x446ff0(G); 0x33 / 0x34 on page 1 run 0x447070(G) /
 * 0x44703c(G) and set byte G+0xe5+p (p re-read). Any other key: page 1
 * with a key of 0x12, 0x2e (4), 0x18 (3), 0x1e (0), 0x21 (5), 0x22 (1),
 * 0x23 (2) runs 0x447158(G, [G+0xfb], n); other keys, or another page,
 * return 0. Every handled key returns AL = 1 with EAX's upper bytes as
 * the listing leaves them (the callee's EAX, else the page or the key
 * less 0x12 with the table byte, else panel_visible's EAX). */
static int mfd_keys(hud_ctx *c, uint32_t g, uint32_t key, uint32_t *eax) {
    uint32_t r = 0;
    if (HCALL(c, VA_VISIBLE, &r, g + 0xebu)) return 1;
    if (!(r & 0xffu)) { *eax = 0; return 0; }
    uint32_t k = key & 0xffffu, n = 0;
    switch (k) {
    case 0x20u: n = 7; break;
    case 0x213u: n = 8; break;
    case 0x214u: n = 9; break;
    case 0x2du: n = 10; break;
    default: break;
    }
    if (n) {
        if (n != 8u) r = hr32(c, g + 0xc1u);
        if (hr8(c, 0x0049cbd8u + 13u * hr32(c, g + 0xc1u) + n)) {
            if (HCALL(c, VA_BUTTON_PRESS, &r, g, n, 1u)) return 1;
        } else if (n == 8u && HCALL(c, VA_MFD_RANGE, &r, g)) return 1;
    } else if (k == 0x33u || k == 0x34u) {
        if (hr32(c, g + 0xc1u) == 1u) {
            if (HCALL(c, k == 0x33u ? VA_MFD_NEXT : VA_MFD_PREV, &r, g)) return 1;
            hw8(c, g + hr32(c, g + 0xc1u) + 0xe5u, 1);
        }
    } else {
        static const uint8_t table[29] = {1, 0, 0, 0, 0, 0, 6, 0, 0, 0, 0, 0, 5, 0, 0, 4, 3, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
        static const uint8_t order[7] = {0xff, 4, 2, 1, 5, 0, 3};   /* the jump table's targets as 0x447158's n */
        if (hr32(c, g + 0xc1u) != 1u || k - 0x12u > 0x1cu || !table[k - 0x12u]) { *eax = 0; return 0; }
        if (HCALL(c, VA_MFD_ORDER_KEY, &r, g, hr32(c, g + 0xfbu), order[table[k - 0x12u]])) return 1;
    }
    *eax = (r & 0xffffff00u) | 1u;
    return 0;
}
void es2_sim_service_mfd_keys(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t eax = 0;
    if (mfd_keys(&c, sc->slots[0], sc->slots[1], &eax)) hud_failed(sc);
    else sc->eax = eax;
}

/* ==== The MFD status draw 0x43a5a0, the MFD target draw 0x43fe44
 * and the HDD status body 0x450c7c, with the callees 0x44aea0,
 * 0x44b3dc, 0x44b344, 0x44b148 (the radio record's draw, frame, talk and line)
 * and 0x447070, 0x44703c, 0x447158 (the MFD entry steps and
 * the order key). Contract: hud.md, "the MFD status and target draws, the HDD
 * status body, the radio records and the entry steps". The registers these
 * draws leave are not part of the contract (their callers ignore them; the HDD
 * status wrappers 0x450b54 / 0x450b74 forward an EAX nobody reads), so the rows
 * leave EAX alone. */

#define VA_MFD_ENTRY_SET 0x004470a4u    /* (no own code) */
#define VA_RADIO_CLASS 0x0044ae1cu      /* condition class (hud_draw.c's static copy) */
#define VA_ORDER_SYMBOL 0x0041bac8u
#define VA_RADIO_CONTEXT 0x0044b864u    /* (hud_draw.c's static copy) */
#define VA_RADIO_CONTEXT_END 0x0044b920u
#define VA_PART_VALUES 0x0041b4ecu
#define VA_PART_STATE 0x00438624u
#define VA_PART_BAR 0x00437e94u
#define VA_PART_BITMAP 0x00438230u
#define VA_STATUS_TAIL 0x0043b288u
#define VA_TARGET_SLOT 0x00433134u
#define VA_WINGMAN_NAME 0x0044b928u
#define VA_RANGE_POINTS 0x0049288cu
#define VA_RADAR_UNITS 0x00434228u
#define VA_STRUPR 0x00496754u
#define VA_STRLEN 0x00496766u
#define VA_MEMCMP 0x004967bau
#define VA_WEAPON_NAME 0x0040e18cu
#define VA_LIST_STEP 0x00471c70u        /* (no own code) */
#define VA_ANGLE_A 0x0047dff0u          /* (no own code) */
#define VA_ANGLE_B 0x0047e0c0u          /* (no own code) */
#define VA_JOLT_RECORD 0x0042d870u
#define VA_BIN_OBJECTS 0x0042841cu
#define VA_TERRAIN_DRAW 0x0046cb9cu
#define VA_TERRAIN_CELLS 0x00428140u
#define VA_ELLIPSE 0x0048817cu
#define VA_TARGET_LOCKED 0x00440370u    /* (no own code) */
#define VA_HDD_LEGEND 0x0044ba54u       /* (no own code) */
#define VA_HDD_PARTS 0x00451de0u
#define VA_OFF_X 0x004d2556u            /* the 640x480 offsets */
#define VA_OFF_Y 0x004d255au

static uint32_t shl16(uint16_t w, uint32_t s) { return hsx16((uint16_t)((uint32_t)w << s)); }
static int canvas_push2(hud_ctx *c, uint32_t *cv) { return HCALL0(c, VA_CANVAS_PUSH, cv); }
static int canvas_pop2(hud_ctx *c) { return HCALL0(c, VA_CANVAS_POP, NULL); }
static int label2(hud_ctx *c, uint32_t w, uint32_t t) { return HCALL(c, VA_LABEL, NULL, w, t); }
/* rect <- the four dwords at src less the 640x480 offsets. */
static void rect_less(hud_ctx *c, uint32_t rect, uint32_t src) {
    for (uint32_t k = 0; k < 4u; ++k) hw32(c, rect + 4u * k, hr32(c, src + 4u * k));
    for (uint32_t k = 0; k < 4u; ++k) hw32(c, rect + 4u * k, hr32(c, rect + 4u * k) - hr32(c, (k & 1u) ? VA_OFF_Y : VA_OFF_X));
}

/* 0x44703c(G) (listing 0x44703c..0x44706c) and 0x447070(G) (listing
 * 0x447070..0x4470a2): from i = [G+0xb1] the next (0x44703c: i+1, 6 wraps
 * to 0) or previous (0x447070: i-1, a negative i wraps to 5) entry whose
 * byte E+0x2c+i (E = [G+0xd1]) has bit 0 clear, then 0x4470a4(G, i); EAX
 * = i. The original loops until it finds one; the own code refuses after
 * 64 steps (all six set: the original never returns). */
static int entry_step(hud_ctx *c, uint32_t g, int up, uint32_t *eax) {
    uint32_t e = hr32(c, g + 0xd1u), i = hr32(c, g + 0xb1u);
    for (unsigned n = 0;; ++n) {
        if (n == 64u)
            return hud_refuse(c, up ? 0x0044703cu : 0x00447070u, up ? "MFD entry up" : "MFD entry down",
                              "no entry with bit 0 clear within 64 steps (the original loops on)");
        if (up) {
            if (++i == 6u) i = 0;
        } else if ((int32_t)--i < 0) i = 5;
        if (!(hr8(c, e + i + 0x2cu) & 1u)) break;
    }
    if (HCALL(c, VA_MFD_ENTRY_SET, NULL, g, i)) return 1;
    *eax = i;
    return 0;
}
/* 0x447158(G, o, n) (listing 0x447158..0x447189): o equal to [G+0xfb]
 * with page [G+0xc1] 1 runs 0x4470a4(G, n) and sets byte G+0xe6; EAX the
 * callee's, else o. */
static int order_key(hud_ctx *c, uint32_t g, uint32_t o, uint32_t n, uint32_t *eax) {
    *eax = o;
    if (o != hr32(c, g + 0xfbu) || hr32(c, g + 0xc1u) != 1u) return 0;
    if (HCALL(c, VA_MFD_ENTRY_SET, eax, g, n)) return 1;
    hw8(c, g + 0xe6u, 1);
    return 0;
}

/* 0x44aea0(H, k) (listing 0x44aea0..0x44afef): with H+0xc1 visible
 * (0x431410, AL) the radio record k (S = H+0x12d + 0x14e k, V = [0x4d03c8
 * + 4k]): byte V+0x99 set runs the frame 0x44b3dc(H, S); else the canvas
 * pushed with the fill record {0, word [0x4d3ba2]} at f+0x10 (frame f =
 * EBP-0x18) in its +0x22c, the fill 0x486704 of S+0x1d..+0x28 less the
 * offsets (f), the labels [S] [S+0x137], [S+4] [0x4d1648], [S+8]
 * [0x4d1614 + 4 0x44ae1c(H, V)] (the full EAX), [S+0xc] [0x4d164c],
 * [S+0x10] [0x4d16b0 + 4 sx16 0x41bac8(V)], the canvas popped. */
static int radio_draw(hud_ctx *c, uint32_t h, uint32_t k) {
    int yes;
    if (visible(c, h + 0xc1u, &yes)) return 1;
    if (!yes) return 0;
    uint32_t v = hr32(c, 0x004d03c8u + 4u * k), s = h + 0x12du + 0x14eu * k;
    if (hr8(c, v + 0x99u)) return HCALL(c, VA_RADIO_FRAME, NULL, h, s);
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0x18u);
    if (!f) return no_frame(c, 0x0044aea0u, "radio record draw");
    int bad = 1;
    uint32_t cv = 0, r = 0;
    if (canvas_push2(c, &cv)) goto done;
    hw32(c, f + 0x10u, 0);
    hw32(c, f + 0x14u, hsx16(hr16(c, 0x004d3ba2u)));
    hw32(c, cv + 0x22cu, f + 0x10u);
    rect_less(c, f, s + 0x1du);
    if (HCALL(c, VA_RECT_FILL, NULL, f) || label2(c, hr32(c, s), hr32(c, s + 0x137u)) ||
        label2(c, hr32(c, s + 4u), hr32(c, 0x004d1648u)) || HCALL(c, VA_RADIO_CLASS, &r, h, v) ||
        label2(c, hr32(c, s + 8u), hr32(c, 0x004d1614u + 4u * r)) || label2(c, hr32(c, s + 0xcu), hr32(c, 0x004d164cu)) ||
        HCALL(c, VA_ORDER_SYMBOL, &r, v) || label2(c, hr32(c, s + 0x10u), hr32(c, 0x004d16b0u + 4u * hsx16((uint16_t)r))) ||
        canvas_pop2(c))
        goto done;
    bad = 0;
done:
    hud_frame_pop(c, top);
    return bad;
}
/* The speaker test shared by 0x44b3dc and 0x44b148: S's actor [S+0x137]
 * equal to H+0x766 gives the HDD view the bitmap and point. */
static void radio_speaker(hud_ctx *c, uint32_t h, uint32_t s, uint32_t bmp, uint32_t px, uint32_t py) {
    if (hr32(c, s + 0x137u) != hr32(c, h + 0x766u)) return;
    hw32(c, h + 0x76au, bmp);
    hw32(c, h + 0x772u, px);
    hw32(c, h + 0x776u, py);
    hw16(c, h + 0x788u, hr16(c, s + 0x13bu));
    hw32(c, h + 0x782u, 1);
}
/* The rotated draw's size record {sx16(word B+6 << sx), sx16(word B+4 <<
 * sy)} (the 16-bit shifts by the words 0x4d253a / 0x4d253c, mod 32). */
static void bitmap_size(hud_ctx *c, uint32_t at, uint32_t b) {
    uint32_t hy = shl16(hr16(c, b + 4u), shift_y(c));
    hw32(c, at, shl16(hr16(c, b + 6u), shift_x(c)));
    hw32(c, at + 4u, hy);
}
/* The name label between 0x44b920 and a canvas push/pop. */
static int radio_name(hud_ctx *c, uint32_t s) {
    return HCALL0(c, VA_RADIO_CONTEXT_END, NULL) || canvas_push2(c, NULL) || label2(c, hr32(c, s), hr32(c, s + 0x137u)) ||
           canvas_pop2(c);
}
/* 0x44b3dc(H, S) (listing 0x44b3dc..0x44b511): the bitmap F = [[[0x49d234]
 * +8] + 4 byte S+0x12e]; the speaker test (point {0, 0}); byte S+0x12e
 * advances, 5 wraps to 0. With H+0xc1 visible: 0x44b864(S); the point
 * 0x49d240/0x49d244 less t = {0 - [0x4d2556], 0 - [0x4d255a]}; the size
 * record at f (frame f = EBP-0x10); 0x45caa4(0x4d24bc, F, 0x49d240, 0, f,
 * 0); the point plus t; 0x44b920(); the canvas pushed, the label [S]
 * [S+0x137], popped. */
static int radio_frame(hud_ctx *c, uint32_t h, uint32_t s) {
    uint32_t b = hr32(c, hr32(c, hr32(c, 0x0049d234u) + 8u) + 4u * hr8(c, s + 0x12eu));
    radio_speaker(c, h, s, b, 0, 0);
    hw8(c, s + 0x12eu, (uint8_t)(hr8(c, s + 0x12eu) + 1u));
    if (hr8(c, s + 0x12eu) == 5u) hw8(c, s + 0x12eu, 0);
    int yes;
    if (visible(c, h + 0xc1u, &yes)) return 1;
    if (!yes) return 0;
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0x10u);
    if (!f) return no_frame(c, 0x0044b3dcu, "radio record frame");
    int bad = 1;
    if (HCALL(c, VA_RADIO_CONTEXT, NULL, s)) goto done;
    hw32(c, f + 8u, 0u - hr32(c, VA_OFF_X));
    hw32(c, f + 0xcu, 0u - hr32(c, VA_OFF_Y));
    hw32(c, 0x0049d240u, hr32(c, 0x0049d240u) - hr32(c, f + 8u));
    hw32(c, 0x0049d244u, hr32(c, 0x0049d244u) - hr32(c, f + 0xcu));
    bitmap_size(c, f, b);
    if (HCALL(c, VA_ROTATED_AT, NULL, VA_DESC, b, 0x0049d240u, 0u, f, 0u)) goto done;
    hw32(c, 0x0049d240u, hr32(c, 0x0049d240u) + hr32(c, f + 8u));
    hw32(c, 0x0049d244u, hr32(c, 0x0049d244u) + hr32(c, f + 0xcu));
    if (radio_name(c, s)) goto done;
    bad = 0;
done:
    hud_frame_pop(c, top);
    return bad;
}
/* 0x44b148(H, S, e) (listing 0x44b148..0x44b342), e a word: with byte
 * S+0x133 set rng_next (stream B, site 0x44b165; its value only feeds a
 * dead read) and the bitmap B = [[[S+0x18]+8] + 4 sx16 e], else B =
 * [[[S+0x18]+8] + 4 sx16 word [0x49d1f6 + 2 sx16 e]]; the point p =
 * {[S+0x3d+8e], [S+0x41+8e]} at f+0x30 (frame f = EBP-0x38); the speaker
 * test with B and p. With H+0xc1 visible: the canvas pushed with the fill
 * record {0, word [0x4d3ba2]} at f+0x20, the fill of S+0x1d..+0x28 less
 * the offsets (f), popped; 0x44b864(S); p less {0 - [0x4d2556], 0 -
 * [0x4d255a]}; the size record at f+0x10; 0x45caa4(0x4d24bc, B, f+0x30,
 * 0, f+0x10, 0); 0x44b920(); the name label as in 0x44b3dc. */
static int radio_line(hud_ctx *c, uint32_t h, uint32_t s, uint32_t e) {
    uint32_t ei = hsx16((uint16_t)e), b;
    if (hr8(c, s + 0x133u)) {
        if (hud_rng_next(c, 0x0044b165u, NULL)) return 1;
        b = hr32(c, hr32(c, hr32(c, s + 0x18u) + 8u) + 4u * ei);
    } else
        b = hr32(c, hr32(c, hr32(c, s + 0x18u) + 8u) + 4u * hsx16(hr16(c, 0x0049d1f6u + 2u * ei)));
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0x38u);
    if (!f) return no_frame(c, 0x0044b148u, "radio record line");
    int bad = 1;
    hw32(c, f + 0x30u, hr32(c, s + 0x3du + 8u * ei));
    hw32(c, f + 0x34u, hr32(c, s + 0x41u + 8u * ei));
    radio_speaker(c, h, s, b, hr32(c, f + 0x30u), hr32(c, f + 0x34u));
    int yes;
    uint32_t cv = 0;
    if (visible(c, h + 0xc1u, &yes)) goto done;
    if (!yes) { bad = 0; goto done; }
    if (canvas_push2(c, &cv)) goto done;
    hw32(c, f + 0x20u, 0);
    hw32(c, cv + 0x22cu, f + 0x20u);
    hw32(c, f + 0x24u, hsx16(hr16(c, 0x004d3ba2u)));
    rect_less(c, f, s + 0x1du);
    if (HCALL(c, VA_RECT_FILL, NULL, f) || canvas_pop2(c) || HCALL(c, VA_RADIO_CONTEXT, NULL, s)) goto done;
    hw32(c, f + 0x18u, 0u - hr32(c, VA_OFF_X));
    hw32(c, f + 0x1cu, 0u - hr32(c, VA_OFF_Y));
    hw32(c, f + 0x30u, hr32(c, f + 0x30u) - hr32(c, f + 0x18u));
    hw32(c, f + 0x34u, hr32(c, f + 0x34u) - hr32(c, f + 0x1cu));
    bitmap_size(c, f + 0x10u, b);
    if (HCALL(c, VA_ROTATED_AT, NULL, VA_DESC, b, f + 0x30u, 0u, f + 0x10u, 0u) || radio_name(c, s)) goto done;
    bad = 0;
done:
    hud_frame_pop(c, top);
    return bad;
}
/* 0x44b344(H, S) (listing 0x44b344..0x44b3db): now = the clock (site
 * 0x44b350). Byte S+0x148 0: 0x44b148(H, S, 0x1b), now above [S+0x143]
 * (unsigned) increments it; 1: 0x44b3dc(H, S), now above decrements it.
 * A state unlike byte S+0x149 sets S+0x143 <- now + max_unsigned(5, sx16
 * rng_bounded(stream B, 20) (site 0x44b3ae)); S+0x149 <- S+0x148. */
static int radio_talk(hud_ctx *c, uint32_t h, uint32_t s) {
    uint32_t now = 0, r = 0;
    if (hud_clock(c, 0x0044b350u, &now)) return 1;
    uint8_t st = hr8(c, s + 0x148u);
    if (st == 0u) {
        if (HCALL(c, VA_RADIO_LINE, NULL, h, s, 0x1bu)) return 1;
        if (now > hr32(c, s + 0x143u)) hw8(c, s + 0x148u, (uint8_t)(hr8(c, s + 0x148u) + 1u));
    } else if (st == 1u) {
        if (HCALL(c, VA_RADIO_FRAME, NULL, h, s)) return 1;
        if (now > hr32(c, s + 0x143u)) hw8(c, s + 0x148u, (uint8_t)(hr8(c, s + 0x148u) - 1u));
    }
    if (hr8(c, s + 0x148u) != hr8(c, s + 0x149u)) {
        if (hud_rng_bounded(c, 0x0044b3aeu, 0x14u, &r)) return 1;
        r = hsx16((uint16_t)r);
        if (r < 5u) r = 5u;
        hw32(c, s + 0x143u, r + now);
    }
    hw8(c, s + 0x149u, hr8(c, s + 0x148u));
    return 0;
}

/* 0x43fe44(X) (listing 0x43fe44..0x440312): the MFD target page's draw.
 * GC = [0x6c5f70] at entry; the frame f = EBP-0x70 (T(k) = EBP-k), the
 * fill record T(0x10) = {0, ...}.
 *  Blink: byte [0x49c398] set and unlike byte X+0x45: X+0x46 <- the clock
 *  (site 0x43fe74) + 0x1e, byte X+0x20 <- 1, X+0x45 <- [0x49c398]. A clock
 *  (site 0x43fe8c) below X+0x46 (unsigned): byte X+0x20 ^= 1, GC+0x22c <-
 *  T(0x10) with +4 the word [0x4d3b9c] (X+0x20 1) or [0x4d3ba2], the fill
 *  0x486704(0x4d1a44), done. Else bytes X+0x45 and [0x49c398] <- 0.
 *  Target [0x49c394] set: the list [0x4a96c2] stepped (0x471c70(list,
 *  cur) from 0) until 0 or the target. 0: [0x49c394] <- 0. Found: the
 *  target's point +0x26 (3 dwords), +0xc and the word +0x10 a, x +=
 *  sx16 0x47e0c0(500, 0x47dff0(w)) and y += sx16 0x47e0c0(500,
 *  0x47dff0(w + 0xc000)) with w = (cur & 0xffff0000) | a (the listing's
 *  ESI); stored to X+0x21.., X+0x2d, word X+0x31. p = 0x42d870(); the
 *  view V = [X+0x10] (re-read each time) +0x210/+0x218 less p's two
 *  dwords; V+4.. <- GC's 0x81 dwords, V+0x208 <- GC+0x208; 0x480d44(V);
 *  the camera C = [X+0x14] gets the point at +4, +0x10 and word +0x14;
 *  0x48c2e4(C); V+0x22c <- T(0x10) with +4 word [0x4d3b9c], [0x4d1a4c]
 *  decremented around the fill (0x4d1a44); [0x4d24ea] <- C around
 *  0x42841c(), 0x46cb9c([0x4a0be0], C, 0) and 0x428140(C) with
 *  [0x4aaaa8]/[0x4aaaac] <- 0/1 around the last two; V+0x20c <- 1, V+0x228
 *  <- T(0x3c) {0, word [0x4d3ba2]}; 0x483a04(T(0x2c), T(0x34)) with the
 *  points {-W, 0}, {W, 0} (W = V+0x220), then {0, -H}, {0, H} (H =
 *  V+0x224); V+0x22c <- T(0x44) {4, word [0x4d3ba2]}; the ellipse
 *  0x48817c(T(0x70)) of {-m, -m, m, m}, m = (-H) sar 1; V+0x20c <- 0;
 *  0x480d44(GC); p added back.
 *  Then, with [0x49c394] clear: GC+0x22c <- T(0x10) with +4 = 0x11, the
 *  fill (0x4d1a44); 0x440370(X) nonzero: labels [X+0x33] [0x4d1668] and
 *  [X+0x37] [0x4d166c], else [X+0x37] [0x4d1670]; [[X+0x3f]] <-
 *  [0x49b0ac]; byte [[[0x49b088]+0x1f5]+0xd6] set: [[X+0x3f]] <-
 *  [0x49b0b8], [X+0x3f]+0x1d <- word [0x4d3b8e] when the clock (site
 *  0x4402bb) has bit 5, else word [0x4d3ba2]; clear: [[X+0x3f]] <-
 *  [0x49b0ac], +0x1d <- word [0x4d3b84]; the label [X+0x3f] [0x4d1674].
 * The list walk refuses after 65536 steps (a cyclic list: the original
 * loops on). */
static int target_draw(hud_ctx *c, uint32_t x) {
    uint32_t gc = hr32(c, VA_GC), top = c->l->stack_hi, f = hud_frame_push(c, 0x70u);
    if (!f) return no_frame(c, 0x0043fe44u, "MFD target draw");
#define T(k) (f + 0x70u - (k))
    int bad = 1;
    uint32_t now = 0, r = 0;
    hw32(c, T(0x10), 0);
    uint8_t blink = hr8(c, 0x0049c398u);
    if (blink) {
        if (blink != hr8(c, x + 0x45u)) {
            if (hud_clock(c, 0x0043fe74u, &now)) goto done;
            hw32(c, x + 0x46u, now + 0x1eu);
            hw8(c, x + 0x20u, 1);
            hw8(c, x + 0x45u, hr8(c, 0x0049c398u));
        }
        if (hud_clock(c, 0x0043fe8cu, &now)) goto done;
        if (now < hr32(c, x + 0x46u)) {
            hw8(c, x + 0x20u, (uint8_t)(hr8(c, x + 0x20u) ^ 1u));
            hw32(c, gc + 0x22cu, T(0x10));
            hw32(c, T(0xc), hsx16(hr16(c, hr8(c, x + 0x20u) == 1u ? 0x004d3b9cu : 0x004d3ba2u)));
            bad = HCALL(c, VA_RECT_FILL, NULL, 0x004d1a44u);
            goto done;
        }
        hw8(c, x + 0x45u, 0);
        hw8(c, 0x0049c398u, 0);
    }
    if (hr32(c, 0x0049c394u)) {
        uint32_t cur = 0;
        for (uint32_t n = 0;; ++n) {
            if (n == 0x10000u) {
                hud_refuse(c, 0x0043fe44u, "MFD target draw", "the target list did not end within 65536 steps");
                goto done;
            }
            if (HCALL(c, VA_LIST_STEP, &cur, hr32(c, 0x004a96c2u), cur)) goto done;
            if (!cur || cur == hr32(c, 0x0049c394u)) break;
        }
        if (!cur) hw32(c, 0x0049c394u, 0);
        else {
            uint32_t t = hr32(c, 0x0049c394u), p = 0, v;
            for (uint32_t k = 0; k < 3u; ++k) hw32(c, T(0x60) + 4u * k, hr32(c, t + 0x26u + 4u * k));
            hw32(c, T(0x18), hr32(c, t + 0xcu));
            hw16(c, T(0x14), hr16(c, t + 0x10u));
            uint32_t w = (cur & 0xffff0000u) | hr16(c, T(0x14));
            if (HCALL(c, VA_ANGLE_A, &r, w) || HCALL(c, VA_ANGLE_B, &r, 0x1f4u, r)) goto done;
            hw32(c, T(0x60), hr32(c, T(0x60)) + hsx16((uint16_t)r));
            w = ((cur & 0xffff0000u) | hr16(c, T(0x14))) + 0xc000u;
            if (HCALL(c, VA_ANGLE_A, &r, w) || HCALL(c, VA_ANGLE_B, &r, 0x1f4u, r)) goto done;
            hw32(c, T(0x5c), hr32(c, T(0x5c)) + hsx16((uint16_t)r));
            for (uint32_t k = 0; k < 3u; ++k) hw32(c, x + 0x21u + 4u * k, hr32(c, T(0x60) + 4u * k));
            hw32(c, x + 0x2du, hr32(c, T(0x18)));
            hw16(c, x + 0x31u, hr16(c, T(0x14)));
            if (HCALL0(c, VA_JOLT_RECORD, &p)) goto done;
            for (uint32_t k = 0; k < 2u; ++k) {
                v = hr32(c, x + 0x10u) + 0x210u + 8u * k;
                hw32(c, v, hr32(c, v) - hr32(c, p));
                hw32(c, v + 4u, hr32(c, v + 4u) - hr32(c, p + 4u));
            }
            v = hr32(c, x + 0x10u);
            for (uint32_t i = 0; i < 0x204u; i += 4u) hw32(c, v + 4u + i, hr32(c, gc + 4u + i));
            hw32(c, v + 0x208u, hr32(c, gc + 0x208u));
            if (HCALL(c, VA_GC_SELECT, NULL, v)) goto done;
            for (uint32_t k = 0; k < 3u; ++k) hw32(c, hr32(c, x + 0x14u) + 4u + 4u * k, hr32(c, T(0x60) + 4u * k));
            v = hr32(c, x + 0x14u);
            hw32(c, v + 0x10u, hr32(c, T(0x18)));
            hw16(c, v + 0x14u, hr16(c, T(0x14)));
            if (HCALL(c, VA_VIEW_SETUP, NULL, v)) goto done;
            hw32(c, hr32(c, x + 0x10u) + 0x22cu, T(0x10));
            hw32(c, T(0xc), hsx16(hr16(c, 0x004d3b9cu)));
            hw32(c, 0x004d1a4cu, hr32(c, 0x004d1a4cu) - 1u);
            if (HCALL(c, VA_RECT_FILL, NULL, 0x004d1a44u)) goto done;
            hw32(c, 0x004d1a4cu, hr32(c, 0x004d1a4cu) + 1u);
            uint32_t saved = hr32(c, 0x004d24eau);
            hw32(c, 0x004d24eau, hr32(c, x + 0x14u));
            if (HCALL0(c, VA_BIN_OBJECTS, NULL)) goto done;
            uint32_t sa = hr32(c, 0x004aaaa8u), sb = hr32(c, 0x004aaaacu);
            hw32(c, 0x004aaaa8u, 0);
            hw32(c, 0x004aaaacu, 1);
            if (HCALL(c, VA_TERRAIN_DRAW, NULL, hr32(c, 0x004a0be0u), hr32(c, x + 0x14u), 0u) ||
                HCALL(c, VA_TERRAIN_CELLS, NULL, hr32(c, x + 0x14u)))
                goto done;
            hw32(c, 0x004aaaa8u, sa);
            hw32(c, 0x004aaaacu, sb);
            hw32(c, 0x004d24eau, saved);
            v = hr32(c, x + 0x10u);
            hw32(c, v + 0x20cu, 1);
            hw32(c, T(0x3c), 0);
            hw32(c, T(0x44), 4);
            hw32(c, T(0x38), hsx16(hr16(c, 0x004d3ba2u)));
            hw32(c, v + 0x228u, T(0x3c));
            hw32(c, T(0x2c), 0u - hr32(c, hr32(c, x + 0x10u) + 0x220u));
            hw32(c, T(0x34), hr32(c, hr32(c, x + 0x10u) + 0x220u));
            hw32(c, T(0x30), 0);
            hw32(c, T(0x28), 0);
            if (HCALL(c, VA_VIEW_FILL, NULL, T(0x2c), T(0x34))) goto done;
            hw32(c, T(0x34), 0);
            hw32(c, T(0x2c), 0);
            hw32(c, T(0x28), 0u - hr32(c, hr32(c, x + 0x10u) + 0x224u));
            hw32(c, T(0x30), hr32(c, hr32(c, x + 0x10u) + 0x224u));
            if (HCALL(c, VA_VIEW_FILL, NULL, T(0x2c), T(0x34))) goto done;
            hw32(c, T(0x68), (uint32_t)((int32_t)(0u - hr32(c, hr32(c, x + 0x10u) + 0x224u)) >> 1));
            hw32(c, T(0x64), (uint32_t)((int32_t)(0u - hr32(c, hr32(c, x + 0x10u) + 0x224u)) >> 1));
            hw32(c, T(0x70), 0u - hr32(c, T(0x68)));
            hw32(c, T(0x6c), 0u - hr32(c, T(0x64)));
            hw32(c, hr32(c, x + 0x10u) + 0x22cu, T(0x44));
            hw32(c, T(0x40), hsx16(hr16(c, 0x004d3ba2u)));
            if (HCALL(c, VA_ELLIPSE, NULL, T(0x70))) goto done;
            hw32(c, hr32(c, x + 0x10u) + 0x20cu, 0);
            if (HCALL(c, VA_GC_SELECT, NULL, gc)) goto done;
            for (uint32_t k = 0; k < 2u; ++k) {
                v = hr32(c, x + 0x10u) + 0x210u + 8u * k;
                hw32(c, v, hr32(c, v) + hr32(c, p));
                hw32(c, v + 4u, hr32(c, v + 4u) + hr32(c, p + 4u));
            }
        }
    }
    if (!hr32(c, 0x0049c394u)) {
        hw32(c, gc + 0x22cu, T(0x10));
        hw32(c, T(0xc), 0x11u);
        if (HCALL(c, VA_RECT_FILL, NULL, 0x004d1a44u) || HCALL(c, VA_TARGET_LOCKED, &r, x)) goto done;
        if (r) {
            if (label2(c, hr32(c, x + 0x33u), hr32(c, 0x004d1668u)) || label2(c, hr32(c, x + 0x37u), hr32(c, 0x004d166cu))) goto done;
        } else if (label2(c, hr32(c, x + 0x37u), hr32(c, 0x004d1670u)))
            goto done;
        hw32(c, hr32(c, x + 0x3fu), hr32(c, 0x0049b0acu));
        if (hr8(c, hr32(c, hr32(c, VA_PANEL) + 0x1f5u) + 0xd6u)) {
            hw32(c, hr32(c, x + 0x3fu), hr32(c, 0x0049b0b8u));
            if (hud_clock(c, 0x004402bbu, &now)) goto done;
            hw32(c, hr32(c, x + 0x3fu) + 0x1du, hsx16(hr16(c, (now & 0x20u) ? 0x004d3b8eu : 0x004d3ba2u)));
        } else {
            hw32(c, hr32(c, x + 0x3fu), hr32(c, 0x0049b0acu));
            hw32(c, hr32(c, x + 0x3fu) + 0x1du, hsx16(hr16(c, 0x004d3b84u)));
        }
        if (label2(c, hr32(c, x + 0x3fu), hr32(c, 0x004d1674u))) goto done;
    }
    bad = 0;
done:
    hud_frame_pop(c, top);
    return bad;
#undef T
}

/* The value offsets 0x43a5a0 sums per part type t (into the part values
 * at buf+2): with the damage flag set (the jump table 0x43a8fe) and clear
 * (0x43a99d); 0xff ends a list. */
static const uint8_t status_offsets[2][15][4] = {
    {{0x00, 0x02, 0xff}, {0xff}, {0xff}, {0xff}, {0x08, 0xff}, {0x0a, 0xff}, {0x0c, 0xff}, {0x0e, 0x12, 0x16, 0xff},
     {0x10, 0x14, 0x18, 0xff}, {0xff}, {0xff}, {0xff}, {0xff}, {0x1a, 0x1e, 0x22, 0xff}, {0x1c, 0x20, 0x24, 0xff}},
    {{0x00, 0xff}, {0xff}, {0xff}, {0xff}, {0x08, 0xff}, {0x0a, 0xff}, {0x0c, 0xff}, {0x0e, 0xff}, {0x10, 0xff},
     {0xff}, {0xff}, {0xff}, {0xff}, {0xff}, {0xff}},
};
/* 0x43a5a0's centred blit point (paths B/C): pt <- ((X+0x2c - X+0x24 -
 * sx16 w) sar 1 + X+0x24, the same for y with +0x30/+0x28 and h), w/h the
 * bitmap's words +6/+4, shifted by the words 0x4d253a/0x4d253c when the
 * signed byte [0x4d2537] is 3 and byte [0x4d256c] clear. */
static void status_centre(hud_ctx *c, uint32_t x, uint32_t b, uint32_t pt) {
    int cond = (int8_t)hr8(c, 0x004d2537u) == 3 && !hr8(c, 0x004d256cu);
    uint32_t w = cond ? shl16(hr16(c, b + 6u), shift_x(c)) : hsx16(hr16(c, b + 6u));
    hw32(c, pt, (uint32_t)((int32_t)(hr32(c, x + 0x2cu) - hr32(c, x + 0x24u) - w) >> 1));
    cond = (int8_t)hr8(c, 0x004d2537u) == 3 && !hr8(c, 0x004d256cu);
    uint32_t hgt = cond ? shl16(hr16(c, b + 4u), shift_y(c)) : hsx16(hr16(c, b + 4u));
    hw32(c, pt + 4u, (uint32_t)((int32_t)(hr32(c, x + 0x30u) - hr32(c, x + 0x28u) - hgt) >> 1));
    hw32(c, pt, hr32(c, pt) + hr32(c, x + 0x24u));
    hw32(c, pt + 4u, hr32(c, pt + 4u) + hr32(c, x + 0x28u));
}
/* The highlight's part-type map (the byte table 0x43acd5 and the jump
 * table 0x43ace8): w <= 0x12 maps 1 -> 0, 9/11 -> 7, 10/12 -> 8, 15..18
 * -> 13, others unchanged. */
static uint32_t status_map(uint32_t w) {
    switch (w) {
    case 1: return 0;
    case 9: case 11: return 7;
    case 10: case 12: return 8;
    case 15: case 16: case 17: case 18: return 13;
    default: return w;
    }
}
/* The condition value, method +0x40 (O, the side-0 flag, k), and the tail
 * 0x43b288(X, value, ...): the original pops only O and leaves the method's
 * two other argument slots, as the method left them, as the tail's third
 * and fourth arguments (0x43ac3d, 0x43af9e, 0x43b127). */
static int status_tail(hud_ctx *c, uint32_t x, uint32_t o, uint32_t k) {
    uint32_t a = c->l->stack_hi, v;
    if (hud_call(c, hr32(c, hr32(c, o) + 0x40u), (const uint32_t[]){o, hr8(c, hr32(c, o + 0x45u) + 0x12u) == 0u, k}, 3, &v)) return 1;
    uint32_t flag = hr32(c, a - 8u), kk = hr32(c, a - 4u);
    return HCALL(c, VA_STATUS_TAIL, NULL, x, hsx16((uint16_t)v), flag, kk);
}
/* 0x43a5a0(X) (listing 0x43a5a0..0x43b235): the MFD status page's draw; the
 * contract in hud.md, "the MFD status and target draws, the HDD status body,
 * the radio records and the entry steps". Frame f = EBP-0x144 (S(k) = EBP-k).
 * Refusals where the original faults: a part whose type sums no value (idiv by
 * 0 at 0x43aae2), no part at all (0x43ab79), a name longer than 0xff bytes for
 * the 20-byte local (the own frame would overflow). */
static int status_draw(hud_ctx *c, uint32_t x) {
    uint32_t gc = hr32(c, VA_GC), top = c->l->stack_hi, f = hud_frame_push(c, 0x144u);
    if (!f) return no_frame(c, 0x0043a5a0u, "MFD status draw");
#define S(k) (f + 0x144u - (k))
    int bad = 1;
    uint32_t now = 0, r = 0, text = 0, esi = 0, v = 0;
    hw32(c, S(0xc), 0);
    uint32_t sh = 1u << (hr16(c, 0x004d253au) & 31u);
    hw32(c, gc + 0x22cu, S(0xc));
    hw32(c, S(8), 0x11u);
    hw32(c, x + 0x28u, hr32(c, x + 0x28u) + sh);
    hw32(c, x + 0x2cu, hr32(c, x + 0x2cu) - sh);
    hw32(c, x + 0x30u, hr32(c, x + 0x30u) - sh);
    if (HCALL(c, VA_RECT_FILL, NULL, x + 0x24u)) goto done;
    hw32(c, x + 0x28u, hr32(c, x + 0x28u) - sh);
    hw32(c, x + 0x2cu, hr32(c, x + 0x2cu) + sh);
    hw32(c, x + 0x30u, hr32(c, x + 0x30u) + sh);
    if (hr32(c, x + 0x3eu) && !hr32(c, hr32(c, x + 8u) + 8u)) {
        uint32_t o = hr32(c, x + 0x3eu);
        if (hr8(c, o + 0xa4u) || hr8(c, o + 0x99u)) {
            if (!hr8(c, x + 0x39u)) {
                hw8(c, x + 0x39u, 1);
                if (hud_clock(c, 0x0043a659u, &now)) goto done;
                hw32(c, x + 0x3au, now + 300u);
            } else {
                if (hud_clock(c, 0x0043a668u, &now)) goto done;
                if (now > hr32(c, x + 0x3au)) {
                    hw8(c, x + 0x39u, 0);
                    hw32(c, x + 0x3eu, 0);
                }
            }
        } else
            hw32(c, x + 0x3eu, 0);
    } else
        hw32(c, x + 0x3eu, hr32(c, hr32(c, x + 8u) + 8u));
    uint32_t o = hr32(c, x + 0x3eu);
    if (!o) {
        hw32(c, hr32(c, x + 0x14u), hr32(c, 0x0049b0acu));
        if (label2(c, hr32(c, x + 0x10u), hr32(c, 0x004d14f0u)) || label2(c, hr32(c, x + 0x14u), hr32(c, 0x004d160cu)) ||
            label2(c, hr32(c, x + 0x18u), 0x0049be23u) || label2(c, hr32(c, x + 0x1cu), 0x0049be24u) ||
            label2(c, hr32(c, x + 0x20u), 0x0049be25u))
            goto done;
        bad = 0;
        goto done;
    }
    int16_t kind = (int16_t)hr16(c, o + 0x1a8u);
    if (kind == 0) {
        int self = hr32(c, hr32(c, VA_PANEL) + 0x203u) == o;
        uint32_t d2 = hr32(c, o + 0x1f2u), p = hr32(c, d2 + 0xdau) + 0x38u, bm = hr32(c, p);
        hw32(c, S(0x2c), hr32(c, p + 4u) + hr32(c, x + 0x24u) + 0x11u);
        hw32(c, S(0x28), hr32(c, p + 8u) + hr32(c, x + 0x28u) + 2u);
        if (!hr8(c, x + 0x34u) && HCALL(c, VA_BLIT_AT, NULL, VA_DESC, bm, S(0x2c))) goto done;
        uint8_t flag = hr8(c, hr32(c, o + 0x1f2u) + 0x50u);
        if (HCALL(c, VA_PART_VALUES, NULL, o, S(0x110)) || HCALL(c, VA_TARGET_SLOT, &r, o)) goto done;
        if (self || r != 0xffffffffu) {
            esi = hr32(c, 0x004d14ecu);
            if (self) text = hr32(c, 0x004d14dcu);
            else if (HCALL(c, VA_WINGMAN_NAME, &text, hr32(c, hr32(c, VA_PANEL) + 0x1f9u), r)) goto done;
        } else {
            esi = hr32(c, 0x004d14f0u);
            if (HCALL(c, VA_STRUPR, &text, d2 + 0x58u)) goto done;
        }
        if (hr8(c, x + 0x34u)) text = 0x0049bdb0u;
        if (label2(c, hr32(c, x + 0x10u), esi)) goto done;
        uint16_t w5a = (uint16_t)hsx8(hr8(c, hr32(c, o + 0x45u) + 0x12u));
        hw32(c, hr32(c, x + 0x14u), hr32(c, w5a == 0u ? 0x0049b0b0u : 0x0049b0b4u));
        if (label2(c, hr32(c, x + 0x14u), text) || label2(c, hr32(c, x + 0x18u), hr32(c, 0x004d14f8u))) goto done;
        uint32_t tcount = 0, buf = S(0x110);
        for (uint32_t i = 0; (int32_t)i < (int32_t)hr32(c, p + 0x14u); ++i) {
            uint32_t rec = hr32(c, p + 0x18u) + 0x1cu * i, t = hr32(c, rec), sum = 0, n = 0;
            if (t <= 0xeu && (!flag || t <= 8u)) {
                const uint8_t *offs = status_offsets[flag ? 1 : 0][t];
                for (; *offs != 0xffu; ++offs, ++n) sum += hsx16(hr16(c, buf + 2u + *offs));
            }
            tcount += n;
            if (!n) {
                hud_refuse(c, 0x0043a5a0u, "MFD status draw", "a part type that sums no value (the original divides by 0)");
                goto done;
            }
            if (HCALL(c, VA_PART_STATE, NULL, (uint32_t)((int32_t)sum / (int32_t)n), rec, S(0x68))) goto done;
            hw32(c, S(0x120), hr32(c, rec + 4u) + hr32(c, S(0x2c)));
            hw32(c, S(0x11c), hr32(c, rec + 8u) + hr32(c, S(0x28)));
            hw32(c, S(0x118), hr32(c, rec + 0xcu) + hr32(c, S(0x2c)));
            hw32(c, S(0x114), hr32(c, rec + 0x10u) + hr32(c, S(0x28)));
            if (HCALL(c, VA_PART_BAR, NULL, S(0x120), hr32(c, rec + 0x14u), hr32(c, S(0x64)), hr32(c, rec + 0x18u))) goto done;
        }
        if (!tcount) {
            hud_refuse(c, 0x0043a5a0u, "MFD status draw", "no part (the original divides by 0)");
            goto done;
        }
        uint32_t q = o + 0x92u, st = 0, edx;
        if (hr8(c, q + 7u)) {
            st = 4;
            edx = q;
        } else {
            int32_t mn = 0x100, mx = 0;
            for (uint32_t j = 0; j < 12u; ++j) {
                int32_t e = (int16_t)hr16(c, buf + 0x28u + 2u * j);
                if (mn > e) mn = e;
                if (e > mx) mx = e;
            }
            edx = (uint32_t)mn;
            if (mn > 0x80) st = 3;
            else if (mx > 0) st = 2;
        }
        if (!st && hr8(c, o + 0x92u + 0x1eu)) st = 1;
        if (status_tail(c, x, o, (edx & 0xffff0000u) | st))
            goto done;
        if (!hr8(c, x + 0x34u) && w5a == 1u) {
            uint32_t rr = hr32(c, VA_PANEL) + 0x26cu;
            if (hr16(c, rr + 0x10u)) {
                for (uint32_t i = 0; (int32_t)i < (int32_t)hr32(c, p + 0x14u); ++i) {
                    uint32_t rec = hr32(c, p + 0x18u) + 0x1cu * i, t = hr32(c, rec), w = hsx16(hr16(c, rr + 0x12u));
                    if (t != w && (w > 0x12u || t != status_map(w))) continue;
                    hw32(c, S(0x130), hr32(c, rec + 4u) + hr32(c, S(0x2c)) - 1u);
                    hw32(c, S(0x12c), hr32(c, rec + 8u) + hr32(c, S(0x28)) - 1u);
                    hw32(c, S(0x128), hr32(c, rec + 0xcu) + hr32(c, S(0x2c)) + 1u);
                    hw32(c, S(0x124), hr32(c, rec + 0x10u) + hr32(c, S(0x28)) + 1u);
                    hw32(c, S(0x9c), 4);
                    hw32(c, gc + 0x22cu, S(0x9c));
                    hw32(c, S(0x98), hsx16(hr16(c, 0x004d3b9cu)));
                    if (HCALL(c, VA_RECT_FILL, NULL, S(0x130))) goto done;
                    break;
                }
            }
        }
    } else if (kind == 1 || kind == 3 || kind == 2) {
        uint32_t b;
        if (kind == 2) b = hr32(c, hr32(c, hr32(c, 0x0049bda8u) + 8u));
        else {
            uint32_t idx = hsx16(hr16(c, hr32(c, o + 0x1f2u) + 0x28u)), tb;
            if (hr16(c, hr32(c, o + 0x1f2u) + 0x32u)) {
                tb = hr32(c, 0x0049bdacu);
                esi = hr32(c, 0x004d15f0u + 4u * idx);
            } else {
                tb = hr32(c, 0x0049bda4u);
                esi = hr32(c, 0x004d1550u + 4u * idx);
            }
            b = hr32(c, hr32(c, tb + 8u) + 4u * idx);
        }
        status_centre(c, x, b, S(0x2c));
        if (HCALL(c, VA_BLIT_AT, NULL, VA_DESC, b, S(0x2c)) || label2(c, hr32(c, x + 0x10u), hr32(c, 0x004d14f0u))) goto done;
        if (hr8(c, x + 0x34u)) esi = 0x0049bdb0u;
        else if (kind == 2 && HCALL(c, VA_STRUPR, &esi, hr32(c, o + 0x1fau) + 0x12u)) goto done;
        if (label2(c, hr32(c, x + 0x14u), esi) || label2(c, hr32(c, x + 0x18u), hr32(c, 0x004d14f8u)) ||
            status_tail(c, x, o, 0xffffffffu))
            goto done;
    } else {
        hw32(c, hr32(c, x + 0x14u), hr32(c, 0x0049b0acu));
        if (label2(c, hr32(c, x + 0x10u), hr32(c, 0x004d14f0u)) ||
            label2(c, hr32(c, x + 0x14u), hr8(c, x + 0x34u) ? 0x0049bdb0u : hr32(c, 0x004d1610u)))
            goto done;
    }
    if ((int8_t)hr8(c, hr32(c, o + 0x45u) + 0x12u) == 1) {
        uint32_t src = hr32(c, 0x004d14f4u), n = 0;
        while (n < 0x100u && hr8(c, src + n)) ++n;
        if (n == 0x100u) {
            hud_refuse(c, 0x0043a5a0u, "MFD status draw", "the range text is longer than the own frame holds");
            goto done;
        }
        hud_gstrcpy(c, S(0x144), src);
        if (hr8(c, x + 0x34u)) {
            if (HCALL(c, HUD_VA_STRCAT, NULL, S(0x144), 0x0049be26u)) goto done;
        } else {
            if (HCALL(c, VA_RANGE_POINTS, &r, hr32(c, hr32(c, VA_PANEL) + 0x203u) + 0x26u, o + 0x26u) ||
                HCALL(c, VA_RADAR_UNITS, &v, r) || HCALL(c, VA_STRLEN, &n, S(0x144)) ||
                HCALL(c, HUD_VA_ITOA, NULL, v, S(0x144) + n, 10u))
                goto done;
        }
        if (label2(c, hr32(c, x + 0x20u), S(0x144))) goto done;
    }
    bad = 0;
done:
    hud_frame_pop(c, top);
    return bad;
#undef S
}

/* The HDD canvas driver block of 0x450c7c: t = G+0x3c into G+0x38 and,
 * with [0x6c5ed0] still G, [0x4a3686] <- G+0x38 and the driver slot
 * [0x4a57ec](t). */
static int hdd_driver(hud_ctx *c, uint32_t g) {
    uint32_t t = hr32(c, g + 0x3cu);
    hw32(c, g + 0x38u, t);
    if (hr32(c, 0x006c5ed0u) != g) return 0;
    hw32(c, 0x004a3686u, hr32(c, g + 0x38u));
    return hud_call(c, hr32(c, 0x004a57ecu), (const uint32_t[]){t}, 1, NULL);
}
/* The part frames of 0x450c7c's redraw: per entry E < [L] of the list
 * [L+4] (0x22 bytes each) the bar 0x437e94(E+0xc..+0x18 + pt, a, b, 1). */
static int hdd_list_bars(hud_ctx *c, uint32_t l, uint32_t pt, uint32_t rect, uint32_t a, uint32_t b) {
    uint32_t e = hr32(c, l + 4u);
    for (uint32_t i = 0; (int32_t)i < (int32_t)hr32(c, l); ++i, e += 0x22u) {
        for (uint32_t k = 0; k < 4u; ++k) hw32(c, rect + 4u * k, hr32(c, e + 0xcu + 4u * k) + hr32(c, pt + 4u * (k & 1u)));
        if (HCALL(c, VA_PART_BAR, NULL, rect, a, b, 1u)) return 1;
    }
    return 0;
}
/* 0x450c7c(X, draw) (listing 0x450c7c..0x451db9; the jump table 0x451c2e), draw
 * a byte: the HDD status page's body; the contract in hud.md, "the MFD status
 * and target draws, the HDD status body, the radio records and the entry
 * steps". Frame f = EBP-0x1c8 (H(k) = EBP-k). Refusals where the original runs
 * on undefined locals: a mode [X+0x7c] above 2 (unsigned), a mode changed by a
 * callee during the draw, a condition class above 4 before any class was set. */
static int hdd_status(hud_ctx *c, uint32_t x, uint32_t draw32) {
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0x1c8u);
    if (!f) return no_frame(c, 0x00450c7cu, "HDD status body");
#define H(k) (f + 0x1c8u - (k))
    int bad = 1, draw = (draw32 & 0xffu) != 0;
    uint32_t cv = 0, r = 0;
    if (hr8(c, hr32(c, x + 8u) + 0x76u)) {
        if (canvas_push2(c, &cv)) goto done;
        hw32(c, H(0x10), 0);
        hw32(c, cv + 0x22cu, H(0x10));
        hw32(c, H(0xc), hsx16(hr16(c, 0x004d3b82u)));
        rect_less(c, H(0x144), hr32(c, x + 4u));
        if (HCALL(c, VA_RECT_FILL, NULL, H(0x144)) || (draw && HCALL(c, VA_PANEL_COPY, NULL, 1u, hr32(c, x + 4u), 0u)) ||
            canvas_pop2(c))
            goto done;
        bad = 0;
        goto done;
    }
    hw32(c, x + 0xecu, hr32(c, hr32(c, x + 8u) + 0x524u));
    int f19 = hr32(c, x + 0xecu) != hr32(c, x + 0xe8u), f1a = hr32(c, x + 0x7cu) != hr32(c, x + 0x80u);
    int f1b = hr32(c, x + 0x84u) != hr32(c, x + 0x88u);
    if (f19) {
        if (HCALL(c, VA_STATUS_MODE, NULL, x, hr32(c, x + 0x7cu))) goto done;
        hw32(c, x + 0xe8u, hr32(c, x + 0xecu));
    }
    if (f1a) hw32(c, x + 0x80u, hr32(c, x + 0x7cu));
    if (f1b) hw32(c, x + 0x88u, hr32(c, x + 0x84u));
    if (!hr32(c, x + 0xecu)) {
        if (!f19 && !f1a) { bad = 0; goto done; }
        hw32(c, x + 0x84u, hr32(c, x + 0x88u));
        uint32_t g = hr32(c, 0x006c5ed0u);
        if (canvas_push2(c, &cv)) goto done;
        hw32(c, H(0x2c), 0);
        hw32(c, cv + 0x22cu, H(0x2c));
        if (hdd_driver(c, g)) goto done;
        hw32(c, H(0x28), hsx16(hr16(c, 0x004d3b82u)));
        rect_less(c, H(0x144), hr32(c, x + 4u));
        if (HCALL(c, VA_RECT_FILL, NULL, H(0x144)) || HCALL(c, VA_HDD_LEGEND, NULL, hr32(c, x + 8u), hr32(c, x + 0x78u)) ||
            label2(c, hr32(c, x + 0xf0u), hr32(c, hr32(c, x + 8u) + 0x55eu)) || canvas_pop2(c) ||
            (draw && HCALL(c, VA_PANEL_COPY, NULL, 1u, hr32(c, x + 4u), 0u)))
            goto done;
        bad = 0;
        goto done;
    }
    uint32_t buf = H(0x198), tab = 0, src = 0, old = 0, sel = 0;
    uint16_t n = 0;
    if (HCALL(c, VA_PART_VALUES, NULL, hr32(c, x + 0xecu), buf)) goto done;
    uint8_t f59 = hr8(c, hr32(c, hr32(c, x + 0xecu) + 0x1f2u) + 0x50u);
    switch (hr32(c, x + 0x7cu)) {
    case 0:
        tab = f59 ? 0x004d1430u : 0x004d13e4u;
        old = x + 0x96u;
        src = buf + 2u;
        n = 0x13;
        sel = 0;
        break;
    case 1:
        tab = f59 ? 0x004d14acu : 0x004d147cu;
        old = x + 0xbcu;
        src = buf + 0x28u;
        n = 0xc;
        sel = 1;
        break;
    case 2: {
        uint32_t k = 0;
        tab = 0x004d1d10u;
        src = buf + 0x40u;
        old = x + 0xd4u;
        n = hr16(c, x + 0x90u);
        sel = 0;
        hw32(c, x + 0x90u, 0);
        for (uint32_t i = 0; (int32_t)i < (int32_t)(int16_t)n; ++i) {
            uint32_t w = hr32(c, hr32(c, hr32(c, hr32(c, x + 0xecu) + 0x202u) + 4u) + 4u * hsx16((uint16_t)i)), name = 0;
            if (w) {
                if (HCALL(c, VA_WEAPON_NAME, &name, w)) goto done;
                hud_gstrcpy(c, 0x004d1d38u + 8u * k, name);
                hw32(c, tab + 4u * hr32(c, x + 0x90u), 0x004d1d38u + 8u * k);
                ++k;
            }
            hw32(c, x + 0x90u, hr32(c, x + 0x90u) + 1u);
        }
        break;
    }
    default:
        hud_refuse(c, 0x00450c7cu, "HDD status body", "a mode above 2 (the original runs on undefined locals)");
        goto done;
    }
    f1b |= f19 | f1a;
    if (!f1b) {
        if (HCALL(c, VA_MEMCMP, &r, src, old, hsx16((uint16_t)(n * 2u)))) goto done;
        if (!(r & 0xffu)) { bad = 0; goto done; }
    }
    uint32_t p = hr32(c, hr32(c, hr32(c, x + 0xecu) + 0x1f2u) + 0xdau) + 0x1cu * sel, edi = hr32(c, p + 0x18u);
    uint32_t l = hr32(c, hr32(c, x + 0xecu) + 0x1feu);
    int32_t lim = (int32_t)(hr32(c, x + 0x84u) + 0xdu);
    if (lim >= (int32_t)hr32(c, x + 0x90u)) lim = (int32_t)hr32(c, x + 0x90u);
    if (hr32(c, x + 0x7cu) == 2u && (int32_t)hr32(c, l) < lim) lim = (int32_t)hr32(c, l);
    uint32_t pt = H(0x8c);
    hw32(c, pt, hr32(c, p + 4u) + hr32(c, hr32(c, x + 4u)) - hr32(c, VA_OFF_X));
    hw32(c, pt + 4u, hr32(c, p + 8u) + hr32(c, hr32(c, x + 4u) + 4u) - hr32(c, VA_OFF_Y));
    uint32_t g2 = hr32(c, 0x006c5ed0u);
    if (canvas_push2(c, &cv)) goto done;
    hw32(c, H(0xa4), 0);
    hw32(c, cv + 0x22cu, H(0xa4));
    if (hdd_driver(c, g2)) goto done;
    if (f19 || f1a) {
        hw32(c, H(0xa0), hsx16(hr16(c, 0x004d3b82u)));
        rect_less(c, H(0x144), hr32(c, x + 4u));
        if (HCALL(c, VA_RECT_FILL, NULL, H(0x144)) || HCALL(c, VA_HDD_LEGEND, NULL, hr32(c, x + 8u), hr32(c, x + 0x78u))) goto done;
        if (!hr32(c, x + 0xecu)) {
            if (label2(c, hr32(c, x + 0xf0u), hr32(c, hr32(c, x + 8u) + 0x55eu)) || canvas_pop2(c) ||
                (draw && HCALL(c, VA_PANEL_COPY, NULL, 1u, hr32(c, x + 4u), 0u)))
                goto done;
            bad = 0;
            goto done;
        }
        if (HCALL(c, VA_BLIT_AT, NULL, VA_DESC, hr32(c, p), pt)) goto done;
        uint32_t mode = hr32(c, x + 0x7cu);
        if (mode < 2u) {
            if (HCALL(c, VA_HDD_PARTS, NULL, x, l, pt) ||
                hdd_list_bars(c, l, pt, H(0x1a8), hsx16(hr16(c, 0x004d3b94u)), hsx16(hr16(c, 0x004d3b88u))))
                goto done;
        } else if (mode == 2u) {
            hw32(c, H(0x1a8), hr32(c, pt));
            hw32(c, H(0x1a4), hr32(c, pt + 4u));
            hw32(c, H(0x1a0), hr32(c, pt) + hr32(c, p + 0xcu));
            hw32(c, H(0x19c), hr32(c, pt + 4u) + hr32(c, p + 0x10u));
            if (HCALL(c, VA_PART_BAR, NULL, H(0x1a8), hsx16(hr16(c, 0x004d3b94u)), hsx16(hr16(c, 0x004d3b88u)), 3u)) goto done;
            edi = hr32(c, p + 0x18u);
            for (uint32_t i = 0; (int32_t)i < (int32_t)hr32(c, p + 0x14u); ++i, edi += 0x1cu) {
                if (hr32(c, edi + 0x14u) == hsx16(hr16(c, 0x004d3b94u))) continue;
                for (uint32_t k = 0; k < 4u; ++k) hw32(c, H(0x1a8) + 4u * k, hr32(c, edi + 4u + 4u * k) + hr32(c, pt + 4u * (k & 1u)));
                if (HCALL(c, VA_PART_BAR, NULL, H(0x1a8), hr32(c, edi + 0x14u), hsx16(hr16(c, 0x004d3b88u)), 3u)) goto done;
            }
            if (HCALL(c, VA_HDD_PARTS, NULL, x, l, pt) ||
                hdd_list_bars(c, l, pt, H(0x1a8), hsx16(hr16(c, 0x004d3b94u)), hsx16(hr16(c, 0x004d3b94u))))
                goto done;
        }
    } else if (f1b) {
        hw32(c, H(0x1b8), hr32(c, hr32(c, x + 0x10u) + 4u));
        hw32(c, H(0x1b4), hr32(c, hr32(c, x + 0x10u) + 8u));
        hw32(c, H(0x1b0), hr32(c, hr32(c, x + 0x74u) + 0xcu));
        hw32(c, H(0x1ac), hr32(c, hr32(c, x + 0x74u) + 0x10u));
        hw32(c, H(0xa0), hsx16(hr16(c, 0x004d3b82u)));
        for (uint32_t k = 0; k < 4u; ++k) hw32(c, H(0x1b8) + 4u * k, hr32(c, H(0x1b8) + 4u * k) - hr32(c, (k & 1u) ? VA_OFF_Y : VA_OFF_X));
        if (HCALL(c, VA_RECT_FILL, NULL, H(0x1b8))) goto done;
    }
    uint32_t mode = hr32(c, x + 0x7cu);
    if (mode < 2u) n = hr16(c, p + 0x14u);
    else if (mode == 2u) n = hr16(c, l);
    int have_cls = 0;
    uint32_t cls = 0, rect = H(0x1c8);
    for (uint32_t i = 0; (int32_t)i < (int32_t)(int16_t)n; ++i) {
        int a = (int32_t)i >= (int32_t)hr32(c, x + 0x84u) && (int32_t)i < lim, d9 = a, da = a, al = 1;
        uint32_t e8, f0, f4;
        uint16_t e0, dc, e2, de;
        mode = hr32(c, x + 0x7cu);
        if (mode == 2u) {
            uint32_t e = hr32(c, l + 4u) + 0x22u * i;
            for (uint32_t k = 0; k < 4u; ++k) hw32(c, rect + 4u * k, hr32(c, e + 0xcu + 4u * k));
            f0 = hsx16(hr16(c, 0x004d3b94u));
            f4 = 0;
            e0 = dc = hr16(c, src + 2u * i);
            e2 = de = hr16(c, old + 2u * i);
            e8 = hsx16(hr16(c, e + 0x20u));
        } else if (mode < 2u) {
            edi = hr32(c, p + 0x18u) + 0x1cu * i;
            e8 = hr32(c, edi);
            e0 = dc = hr16(c, src + 2u * e8);
            e2 = de = hr16(c, old + 2u * e8);
            if (mode == 0u && !f59) {
                if (e8 == 0u) {
                    e0 = (uint16_t)((int32_t)(hsx16(hr16(c, src + 2u * e8 + 2u)) + hsx16(dc)) >> 1);
                    e2 = (uint16_t)((int32_t)(hsx16(hr16(c, old + 2u * e8 + 2u)) + hsx16(de)) >> 1);
                } else if (e8 == 1u) {
                    edi -= 0x1cu;
                    al = 0;
                }
            }
            for (uint32_t k = 0; k < 4u; ++k) hw32(c, rect + 4u * k, hr32(c, edi + 4u + 4u * k));
            f0 = hr32(c, edi + 0x14u);
            f4 = hr32(c, edi + 0x18u);
        } else {
            hud_refuse(c, 0x00450c7cu, "HDD status body", "the mode changed above 2 during the draw (undefined locals)");
            goto done;
        }
        uint32_t ec = hr32(c, tab + 4u * e8);
        for (uint32_t k = 0; k < 4u; ++k) hw32(c, rect + 4u * k, hr32(c, rect + 4u * k) + hr32(c, pt + 4u * (k & 1u)));
        if (al && (f1b || e0 != e2)) {
            if (!hr8(c, 0x0049da9cu)) hw8(c, 0x0049da9cu, 1);
            if (hr32(c, x + 0x7cu) == 2u) {
                hw32(c, 0x004d1db0u, 0);
                edi = hr32(c, p + 0x18u);
            }
            if (HCALL(c, VA_PART_STATE, NULL, hsx16(e0), edi, H(0x110)) || HCALL(c, VA_PART_STATE, NULL, hsx16(e2), edi, H(0x118)))
                goto done;
            if (f1b || hr32(c, H(0x110)) != hr32(c, H(0x118))) {
                if (hr32(c, x + 0x7cu) == 2u) {
                    if (!f1b) f0 = hr32(c, H(0x114));
                    if (HCALL(c, VA_PART_BAR, NULL, rect, f0, hr32(c, H(0x10c)), f4)) goto done;
                } else if (HCALL(c, VA_PART_BITMAP, NULL, rect, hr32(c, p), pt, f0, hr32(c, H(0x10c)), f4))
                    goto done;
                if (draw && HCALL(c, VA_PANEL_COPY, NULL, 1u, rect, 0u)) goto done;
            }
        }
        if (!ec || (!f1b && dc == de)) d9 = da = 0;
        else {
            if (HCALL(c, VA_PART_STATE, NULL, hsx16(dc), edi, H(0x110)) || HCALL(c, VA_PART_STATE, NULL, hsx16(de), edi, H(0x118)))
                goto done;
            if (!f1b && hr32(c, H(0x110)) == hr32(c, H(0x118))) da = 0;
        }
        if (!d9 && !da) continue;
        if (HCALL(c, HUD_VA_ITOA, NULL, (uint32_t)((int32_t)((0x100u - hsx16(dc)) * 100u) >> 8), H(0x11c), 10u)) goto done;
        uint32_t o0 = hr32(c, H(0x110));
        if (o0 <= 4u) {
            static const uint8_t classes[5] = {1, 3, 9, 2, 7};
            cls = classes[o0];
            have_cls = 1;
        } else if (!have_cls) {
            hud_refuse(c, 0x00450c7cu, "HDD status body", "a condition above 4 before any class was set (an undefined local)");
            goto done;
        }
        uint32_t colour = hr32(c, 0x0049b0acu + 4u * cls), j = i - hr32(c, x + 0x84u);
        if (da) {
            uint32_t lb = hr32(c, x + 0x10u + 4u * j);
            hw32(c, lb, colour);
            if (label2(c, lb, ec) || (!f1b && draw && HCALL(c, VA_PANEL_COPY, NULL, 1u, lb + 4u, 0u))) goto done;
        }
        if (d9) {
            uint32_t lb = hr32(c, x + 0x44u + 4u * j);
            hw32(c, lb, colour);
            if (label2(c, lb, H(0x11c)) || (!f1b && draw && HCALL(c, VA_PANEL_COPY, NULL, 1u, lb + 4u, 0u))) goto done;
        }
    }
    for (uint32_t k = 0; k < 0x54u; k += 4u) hw32(c, x + 0x94u + k, hr32(c, buf + k));
    if (canvas_pop2(c) || (f1b && draw && HCALL(c, VA_PANEL_COPY, NULL, 1u, hr32(c, x + 4u), 0u))) goto done;
    bad = 0;
done:
    hud_frame_pop(c, top);
    return bad;
#undef H
}

void es2_sim_service_mfd_entry_up(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t eax = 0;
    if (entry_step(&c, sc->slots[0], 1, &eax)) hud_failed(sc);
    else sc->eax = eax;
}
void es2_sim_service_mfd_entry_down(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t eax = 0;
    if (entry_step(&c, sc->slots[0], 0, &eax)) hud_failed(sc);
    else sc->eax = eax;
}
void es2_sim_service_mfd_order_key(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t eax = 0;
    if (order_key(&c, sc->slots[0], sc->slots[1], sc->slots[2], &eax)) hud_failed(sc);
    else sc->eax = eax;
}
void es2_sim_service_radio_draw(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (radio_draw(&c, sc->slots[0], sc->slots[1])) hud_failed(sc);
}
void es2_sim_service_radio_frame(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (radio_frame(&c, sc->slots[0], sc->slots[1])) hud_failed(sc);
}
void es2_sim_service_radio_line(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (radio_line(&c, sc->slots[0], sc->slots[1], sc->slots[2])) hud_failed(sc);
}
void es2_sim_service_radio_talk(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (radio_talk(&c, sc->slots[0], sc->slots[1])) hud_failed(sc);
}
void es2_sim_service_mfd_target_draw(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (target_draw(&c, sc->slots[0])) hud_failed(sc);
}
void es2_sim_service_mfd_status_draw(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (status_draw(&c, sc->slots[0])) hud_failed(sc);
}
void es2_sim_service_hdd_status_body(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (hdd_status(&c, sc->slots[0], sc->slots[1])) hud_failed(sc);
}

/* ======================================================================
 * The HDD map's button and icon leaves as rows (hud.md, "the MFD pages, the HDD
 * map and status page, and the map's button and icon leaves"). The page
 * constructors of the row (MFD 0x43f600, 0x43faf4, 0x4404bc; the HDD map
 * 0x44c28c with the icons 0x44f158 and the drag widget 0x44f678; the HDD status
 * page 0x4507c4) are own code inside mfd_ctor and hdd_ctor
 * (src/hud_cockpit.c) and get no row; 0x44d288 is computed inside its callers'
 * own code. What is left are the leaves that the hooks bridge (hdd_orders:
 * 0x44ede0, 0x44ee00, 0x44ee24; hdd_map: the tick 0x44d370 and the
 * wingman-button blink 0x44f644) and the method table targets of the map icon
 * (vtable 0x49d9b0 slots +4, +8, +0xc) and of the drag widget (0x49d944 slot
 * +8).
 *
 * Registers: each row leaves EAX, EDX and ECX as the original does. Where
 * the original pops its pushed argument into ECX after a call, ECX is that
 * argument slot as the callee left it (read back from the call area). */
#define VA_MAP_HOLDER 0x0049d6b0u       /* the HDD map page M; [M+8] is the HDD */
#define VA_MAP_DRAG_START 0x0044d3ccu   /* map_drag_start (hdd_buttons.h) */
#define VA_MAP_DRAG_POINT 0x004d1de3u

/* A call through the dispatch with the entry registers in[3] = {EAX, EDX,
 * ECX}; r->ecx becomes the first argument slot after the call (the
 * original's `pop ecx`). */
static int hd11_call_pop(hud_ctx *c, uint32_t va, const uint32_t *slots, unsigned count, const uint32_t in[3], es2_sim_result *r) {
    uint32_t top = c->l->stack_hi;
    if (hud_call_regs(c, va, slots, count, in, r)) return 1;
    r->ecx = hr32(c, top - 4u * (count ? count : 1u));
    return 0;
}

/* 0x44ede0(M, i) (listing 0x44ede0..0x44edfc): the map button lamp: B =
 * [[M+0x31] + 4i], byte B+0x36 ^= 1, then B's slot +4 ([B+0x17]+4) with
 * (B), entered with EAX B, EDX i, ECX the table. EAX/EDX the slot's, ECX
 * the popped argument. */
static int map_lamp(hud_ctx *c, uint32_t m, uint32_t i, es2_sim_result *r) {
    uint32_t b = hr32(c, hr32(c, m + 0x31u) + 4u * i);
    hw8(c, b + 0x36u, (uint8_t)(hr8(c, b + 0x36u) ^ 1u));
    uint32_t vt = hr32(c, b + 0x17u);
    return hd11_call_pop(c, hr32(c, vt + 4u), &b, 1, (const uint32_t[]){b, i, vt}, r);
}
void es2_sim_service_map_lamp(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    es2_sim_result r;
    if (map_lamp(&c, sc->slots[0], sc->slots[1], &r)) { hud_failed(sc); return; }
    sc->eax = r.eax; sc->edx = r.edx; sc->ecx = r.ecx;
}

/* 0x44ee00(M) / 0x44ee24(M) (listings 0x44ee00..0x44ee20 and
 * 0x44ee24..0x44ee44): the eight button bytes M+0x131..M+0x138 <- 1 (on)
 * or 0 (off), in ascending order; byte M+0x139 <- 1 in both. EAX M+0x139,
 * EDX 8, ECX M. */
static void map_buttons(hud_ctx *c, es2_sim_service_call *sc, uint8_t v) {
    uint32_t m = sc->slots[0];
    for (uint32_t k = 0; k < 8u; ++k) hw8(c, m + 0x131u + k, v);
    hw8(c, m + 0x139u, 1);
    sc->eax = m + 0x139u; sc->edx = 8u; sc->ecx = m;
}
void es2_sim_service_map_buttons_on(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    map_buttons(&c, sc, 1);
}
void es2_sim_service_map_buttons_off(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    map_buttons(&c, sc, 0);
}

/* 0x44d370() (listing 0x44d370..0x44d3cb): the map frame's 30-tick tick:
 * bytes 0x49d6ac, 0x49d6ae <- 1, byte 0x49d6ad ^= 1; the HDD G =
 * [[0x49d6b0]+8]: byte G+0x10f <- 1, byte G+0xb0 <- 2; with M = [0x49d6b0]
 * (read again) and word M+0x13e set: the word counts down, and when it
 * reaches 0 (M read again) byte M+0x144 <- 1. EAX M; EDX and ECX the
 * caller's. */
void es2_sim_service_map_tick(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    hw8(&c, 0x0049d6acu, 1);
    hw8(&c, 0x0049d6aeu, 1);
    hw8(&c, 0x0049d6adu, (uint8_t)(hr8(&c, 0x0049d6adu) ^ 1u));
    uint32_t g = hr32(&c, hr32(&c, VA_MAP_HOLDER) + 8u);
    hw8(&c, g + 0x10fu, 1);
    hw8(&c, g + 0xb0u, 2);
    uint32_t m = hr32(&c, VA_MAP_HOLDER);
    if (hr16(&c, m + 0x13eu)) {
        hw16(&c, m + 0x13eu, (uint16_t)(hr16(&c, m + 0x13eu) - 1u));
        m = hr32(&c, VA_MAP_HOLDER);
        if (!hr16(&c, m + 0x13eu)) hw8(&c, m + 0x144u, 1);
    }
    sc->eax = m;
}

/* 0x44f5d0(I) (listing 0x44f5d0..0x44f5f1), the map icon's slot +4: byte
 * I+0x1f <- 2; the HDD G = [[0x49d6b0]+8]: byte G+0x10f <- 1, byte G+0xb0
 * <- 2. EAX G; EDX and ECX the caller's. */
void es2_sim_service_icon_mark(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    hw8(&c, sc->slots[0] + 0x1fu, 2);
    uint32_t g = hr32(&c, hr32(&c, VA_MAP_HOLDER) + 8u);
    hw8(&c, g + 0x10fu, 1);
    hw8(&c, g + 0xb0u, 2);
    sc->eax = g;
}

/* 0x44f5f4(I) (listing 0x44f5f4..0x44f62f), the map icon's slot +0xc:
 * panel_visible 0x431410(I), entered with the caller's registers. When AL
 * is set and byte I+0x1f is 2:
 *   - byte I+0x37 <- 1;
 *   - I's slot 0 with (I), entered with visible's EAX (AL 0), EDX the
 *     table and ECX the popped argument;
 *   - byte I+0x37 <- 0;
 *   - the panel copy 0x43122c(1, I, 0), entered with the slot's EAX/EDX
 *     and the popped argument;
 *   - byte I+0x1f <- 0.
 * EAX, EDX, ECX: the panel copy's; on the early returns visible's EAX
 * (with AL = byte I+0x1f - 2 when AL was set) and EDX, and ECX the popped
 * argument. */
static int icon_frame(hud_ctx *c, uint32_t i, es2_sim_result *r) {
    es2_sim_result v;
    if (hd11_call_pop(c, VA_VISIBLE, &i, 1, (const uint32_t[]){r->eax, r->edx, r->ecx}, &v)) return 1;
    r->eax = v.eax; r->edx = v.edx; r->ecx = v.ecx;
    if (!(v.eax & 0xffu)) return 0;
    uint8_t al = (uint8_t)(hr8(c, i + 0x1fu) - 2u);
    r->eax = (v.eax & 0xffffff00u) | al;
    if (al) return 0;
    hw8(c, i + 0x37u, 1);
    uint32_t vt = hr32(c, i + 0x17u);
    if (hd11_call_pop(c, hr32(c, vt), &i, 1, (const uint32_t[]){r->eax, vt, r->ecx}, &v)) return 1;
    hw8(c, i + 0x37u, 0);
    if (hud_call_regs(c, VA_PANEL_COPY, (const uint32_t[]){1u, i, 0u}, 3, (const uint32_t[]){v.eax, v.edx, v.ecx}, &v)) return 1;
    hw8(c, i + 0x1fu, 0);
    r->eax = v.eax; r->edx = v.edx; r->ecx = v.ecx;
    return 0;
}
void es2_sim_service_icon_frame(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    es2_sim_result r = {.eax = sc->eax, .edx = sc->edx, .ecx = sc->ecx};
    if (icon_frame(&c, sc->slots[0], &r)) { hud_failed(sc); return; }
    sc->eax = r.eax; sc->edx = r.edx; sc->ecx = r.ecx;
}

/* 0x44f630(I, f) (listing 0x44f630..0x44f641), the map icon's slot +8:
 * with bit 0 of f, byte I+0x37 <- 0. EAX I; EDX and ECX the caller's. */
void es2_sim_service_icon_release(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (sc->slots[1] & 1u) hw8(&c, sc->slots[0] + 0x37u, 0);
    sc->eax = sc->slots[0];
}

/* 0x44f644(B) (listing 0x44f644..0x44f658): the wingman button's blink:
 * unless byte B+0x37 is set, B's slot 0 ([B+0x17]+0) with (B), entered
 * with EAX the table. EAX B, or the slot's EAX and EDX with ECX the
 * popped argument. */
void es2_sim_service_map_blink(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t b = sc->slots[0];
    sc->eax = b;
    if (hr8(&c, b + 0x37u)) return;
    uint32_t vt = hr32(&c, b + 0x17u);
    es2_sim_result r;
    if (hd11_call_pop(&c, hr32(&c, vt), &b, 1, (const uint32_t[]){vt, sc->edx, sc->ecx}, &r)) { hud_failed(sc); return; }
    sc->eax = r.eax; sc->edx = r.edx; sc->ecx = r.ecx;
}

/* 0x44f6d4(W, f) (listing 0x44f6d4..0x44f6f3), the map drag widget's slot
 * +8: with bit 0 of f, map_drag_start 0x44d3cc([W+0x28], W, 0x4d1de3),
 * entered with EAX [W+0x28] and the caller's EDX/ECX. EAX W, or the drag
 * start's EAX, EDX and ECX. */
void es2_sim_service_drag_press(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t w = sc->slots[0];
    sc->eax = w;
    if (!(sc->slots[1] & 1u)) return;
    uint32_t m = hr32(&c, w + 0x28u);
    es2_sim_result r;
    if (hud_call_regs(&c, VA_MAP_DRAG_START, (const uint32_t[]){m, w, VA_MAP_DRAG_POINT}, 3, (const uint32_t[]){m, sc->edx, sc->ecx}, &r)) {
        hud_failed(sc);
        return;
    }
    sc->eax = r.eax; sc->edx = r.edx; sc->ecx = r.ecx;
}

/* 0x438624(v, P, out) (listing 0x438624..0x4386fd), the part state of the MFD
 * status: the percentage q = (0x100 - v) * 100 >> 8 (arithmetic) and the part
 * kind [P+0x18]. Kinds 0, 2, 3: the state 0..4 at q >= 0x5a, 0x4a, 0x33, 1,
 * else, with the colour word of the state (0x4d3b94, 0x4d3b9a, 0x4d3ba4,
 * 0x4d3b8e, 0x4d3ba0; sign-extended); kind 1: the same states with the colour
 * [P+0x14] for the first three and 0x4d3b88, 0x4d3b9e for the last two; another
 * kind stores the entry EDX and ECX. out[0] <- the state (EDX), out[1] <- the
 * colour (ECX); EAX = q, less 1 for states 3 and 4. */
void es2_sim_service_part_state(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t p = sc->slots[1], out = sc->slots[2];
    uint32_t q = (uint32_t)((int32_t)((0x100u - sc->slots[0]) * 100u) >> 8);
    uint32_t kind = hr32(&c, p + 0x18u), st = sc->edx, colour = sc->ecx;
    if (kind <= 3u) {
        static const uint32_t words[2][5] = {{0x004d3b94u, 0x004d3b9au, 0x004d3ba4u, 0x004d3b8eu, 0x004d3ba0u},
                                             {0, 0, 0, 0x004d3b88u, 0x004d3b9eu}};
        unsigned k = kind == 1u;
        if (k) colour = hr32(&c, p + 0x14u);
        if ((int32_t)q >= 0x5a) st = 0;
        else if ((int32_t)q >= 0x4a) st = 1;
        else if ((int32_t)q >= 0x33) st = 2;
        else st = (int32_t)q >= 1 ? 3u : 4u;
        if (st >= 3u) --q;
        if (!k || st >= 3u) colour = hsx16(hr16(&c, words[k][st]));
    }
    hw32(&c, out, st);
    hw32(&c, out + 4u, colour);
    sc->eax = q;
    sc->edx = st;
    sc->ecx = colour;
}

/* 0x44f0c4(M, pos) (listing 0x44f0c4..0x44f157), whether a point lands inside
 * the HDD map: the map's camera [M+0x14]+4 <- the 12 bytes at M+0x18 (dwords
 * +8, +0, +4), view_setup([M+0x14]), object_select(0), view_transform(pos, t),
 * project_point(t, s), s.y negated, map_point_screen( M, s, xy), then x in
 * [M+0x71, M+0x79] and y in [M+0x75, M+0x7d] (signed). EAX 0/1; EDX and ECX
 * map_point_screen's, or y once x passes (y also in ECX at the last test). The
 * 0x1c bytes of locals are the call area's zero model (own_process.md section
 * 5). */
static int map_inside(hud_ctx *c, uint32_t m, uint32_t pos, es2_sim_result *out) {
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0x1cu);
    if (!f) return hud_refuse(c, 0x0044f0c4u, "map inside", "the own code's call area is exhausted");
    uint32_t t = f, xy = f + 0xcu, s = f + 0x14u, cam = hr32(c, m + 0x14u) + 4u;
    es2_sim_result r;
    hw32(c, cam + 8u, hr32(c, m + 0x20u));
    hw32(c, cam, hr32(c, m + 0x18u));
    hw32(c, cam + 4u, hr32(c, m + 0x1cu));
    int bad = HCALL(c, 0x0048c2e4u, NULL, hr32(c, m + 0x14u)) || HCALL(c, 0x0048c444u, NULL, 0) ||
              HCALL(c, 0x0048c57cu, NULL, pos, t) || HCALL(c, 0x0048c5fcu, NULL, t, s);
    if (!bad) {
        hw32(c, s + 4u, 0u - hr32(c, s + 4u));
        bad = hud_call_regs(c, 0x0044d24cu, (const uint32_t[]){m, s, xy}, 3, NULL, &r);
    }
    hud_frame_pop(c, top);
    if (bad) return 1;
    int32_t x = (int32_t)hr32(c, xy), y = (int32_t)hr32(c, xy + 4u);
    out->eax = 0;
    out->edx = r.edx;
    out->ecx = r.ecx;
    if (x < (int32_t)hr32(c, m + 0x71u) || x > (int32_t)hr32(c, m + 0x79u)) return 0;
    out->edx = (uint32_t)y;
    if (y < (int32_t)hr32(c, m + 0x75u)) return 0;
    out->ecx = (uint32_t)y;
    out->eax = y <= (int32_t)hr32(c, m + 0x7du);
    return 0;
}
void es2_sim_service_map_inside(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    es2_sim_result r;
    if (map_inside(&c, sc->slots[0], sc->slots[1], &r)) { hud_failed(sc); return; }
    sc->eax = r.eax; sc->edx = r.edx; sc->ecx = r.ecx;
}
/* 0x44f03c(M, A, side) (listing 0x44f03c..0x44f0c1), the HDD map's target test
 * (target_cycle calls it): A's side, the s8 [[A+0x45] +0x12] as a
 * word, equals the word `side`; A's byte +0x99 is clear; A's position A+0x26 is
 * inside the map (0x44f0c4, its AL); A's byte +0x98 is set; then side 0 passes,
 * another side passes when target_known([[0x49b088] +0x203], A) (hook) answers
 * a non-zero AX. EAX 0/1; EDX and ECX as the listing leaves them on each exit. */
void es2_sim_service_map_target(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t m = sc->slots[0], a = sc->slots[1];
    uint32_t panel = hr32(&c, hr32(&c, VA_PANEL) + 0x203u);
    uint32_t b = hr32(&c, a + 0x45u);
    uint16_t side = (uint16_t)hsx8(hr8(&c, b + 0x12u));
    es2_sim_result r;
    sc->eax = 0;
    sc->edx = panel;
    sc->ecx = b;
    if (side != (uint16_t)sc->slots[2]) return;
    sc->edx = a + 0x92u;
    if (hr8(&c, a + 0x99u)) return;
    if (map_inside(&c, m, a + 0x26u, &r)) { hud_failed(sc); return; }
    sc->edx = r.edx;
    sc->ecx = r.ecx;
    if (!(uint8_t)r.eax) return;
    sc->edx = a + 0x92u;
    if (!hr8(&c, a + 0x98u)) return;
    if (!side) { sc->eax = 1; return; }
    if (hud_call_regs(&c, 0x00411c58u, (const uint32_t[]){panel, a}, 2, NULL, &r)) { hud_failed(sc); return; }
    sc->edx = r.edx;
    sc->ecx = r.ecx;
    sc->eax = (uint16_t)r.eax != 0;
}
/* 0x447014(G, i) (listing 0x447014..0x447039), the MFD radar range (mfd_range
 * 0x446ff0 calls it with the next index): an index other than [G+0xc5] is
 * stored there and its range [0x4d1c70 + 4i] into G+0xbd. EAX = G; EDX = i, or
 * the range when one is stored; ECX as at entry. */
void es2_sim_service_range_set(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t g = sc->slots[0], i = sc->slots[1];
    sc->eax = g;
    sc->edx = i;
    if (i == hr32(&c, g + 0xc5u)) return;
    hw32(&c, g + 0xc5u, i);
    sc->edx = hr32(&c, 0x004d1c70u + 4u * i);
    hw32(&c, g + 0xbdu, sc->edx);
}
/* 0x437e94(R, a, b, mode) (listing 0x437e94..0x43822d), the HDD part bar
 * (hdd_list_bars above calls it): recolours the pixels of the rectangle R (x0,
 * y0, x1, y1, inclusive), moved by the port [0x6c5f70]'s +0x210 less its
 * +0x220, through the driver slots [0x4a57bc](&pt) (read) and [0x4a57c0](&pt,
 * colour) (write). Steps: sx = 1 << word [0x4d253a] and sy = 1 << word
 * [0x4d253c] (the shift count's low five bits) when the signed byte [0x4d2537]
 * is 3 and the byte [0x4d256c] is 0, read again for each step, else 1; a
 * matching pixel is written as an sx by sy block from pt. Mode 0: a pixel equal
 * to a becomes p + (b - a); 1 and 3: a pixel equal to a becomes b; 2: a pixel
 * other than the s16 [0x4d3ba2] (read after each pixel read) becomes b + p.
 * Modes 0 and 1 return at once when b equals a; a moved y0 below zero or
 * another mode draws nothing. First the canvas driver block of 0x450c7c
 * (hdd_driver). The locals live in the original's 0x2c-byte frame (L(k) =
 * EBP-k, pt at L(0x10)) and are read back from it, as the drivers get &pt; EAX,
 * EDX and ECX follow the listing to each exit. A step of 1 << 31 makes the
 * original loop for ever; so does this. */
void es2_sim_service_part_bar(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t top = c.l->stack_hi, f = hud_frame_push(&c, 0x2cu);
    if (!f) { no_frame(&c, 0x00437e94u, "part bar"); hud_failed(sc); return; }
#define L(k) (f + 0x2cu - (k))
    uint32_t a = sc->slots[1], b = sc->slots[2], mode = sc->slots[3], eax, edx, ecx, p;
    es2_sim_result r;
    int dbl = (int8_t)hr8(&c, 0x004d2537u) == 3 && !hr8(&c, 0x004d256cu);
    uint32_t sx = dbl ? 1u << (hr16(&c, 0x004d253au) & 31u) : 1u;
    dbl = (int8_t)hr8(&c, 0x004d2537u) == 3 && !hr8(&c, 0x004d256cu);
    hw32(&c, L(4), dbl ? 1u << (hr16(&c, 0x004d253cu) & 31u) : 1u);
    uint32_t s = hr32(&c, 0x006c5ed0u), port = hr32(&c, 0x006c5f70u);
    hw32(&c, L(8), hr32(&c, s + 0x3cu));
    hw32(&c, s + 0x38u, hr32(&c, L(8)));
    if (s == hr32(&c, 0x006c5ed0u)) {
        uint32_t v = hr32(&c, s + 0x38u), t = hr32(&c, L(8));
        hw32(&c, 0x004a3686u, v);
        if (hud_call(&c, hr32(&c, 0x004a57ecu), (const uint32_t[]){t}, 1, NULL)) goto fail;
    }
    uint32_t rect = sc->slots[0];
    for (uint32_t k = 0; k < 4u; ++k) hw32(&c, L(0x2cu - 4u * k), hr32(&c, rect + 4u * k));
    for (uint32_t k = 0; k < 4u; ++k) hw32(&c, L(0x2cu - 4u * k), hr32(&c, L(0x2cu - 4u * k)) + hr32(&c, port + 0x210u + 4u * (k & 1u)));
    for (uint32_t k = 0; k < 4u; ++k) hw32(&c, L(0x2cu - 4u * k), hr32(&c, L(0x2cu - 4u * k)) - hr32(&c, port + 0x220u + 4u * (k & 1u)));
    edx = hr32(&c, port + 0x220u);
    ecx = hr32(&c, port + 0x224u);
    eax = hr32(&c, L(0x28));
    if ((int32_t)eax < 0) goto out;
    edx = hr32(&c, L(0x2c));
    hw32(&c, L(0x10), edx);
    hw32(&c, L(0xc), eax);
    ecx = a;
    hw32(&c, L(0x14), ecx);
    if (mode == 0) {
        eax = 0xffffffffu;
        edx = b;
        if (b == a) goto out;
        ecx = b - a;
        hw32(&c, L(0x1c), ecx);
    } else if (mode == 1) {
        eax = b;
        if (b == a) goto out;
    } else if (mode == 2 || mode == 3) {
        eax = 0;
    } else {
        eax = mode - 3u;
        goto out;
    }
    for (;;) {
        edx = hr32(&c, L(0xc));
        if ((int32_t)edx > (int32_t)hr32(&c, L(0x20))) break;
        ecx = hr32(&c, L(0x10));
        if ((int32_t)ecx > (int32_t)hr32(&c, L(0x24))) break;
        if (hud_call_regs(&c, hr32(&c, 0x004a57bcu), (const uint32_t[]){L(0x10)}, 1, NULL, &r)) goto fail;
        ecx = L(0x10);
        eax = p = r.eax;
        edx = r.edx;
        int match;
        if (mode == 1) {
            match = p == a;
        } else if (mode == 2) {
            eax = hsx16(hr16(&c, 0x004d3ba2u));
            match = p != eax;
        } else {
            match = (int32_t)p >= (int32_t)a && (int32_t)p <= (int32_t)hr32(&c, L(0x14));
        }
        if (match) {
            int more;
            hw32(&c, L(0x18), 0);
            if (mode == 2) {
                edx = 0;
                ecx = 0;
            } else {
                eax = 0;
                edx = 0;
            }
            more = 0 < (int32_t)hr32(&c, L(4));
            while (more) {
                for (uint32_t j = 0; (int32_t)sx > (int32_t)j; ++j) {
                    uint32_t colour = mode == 0 ? hr32(&c, L(0x1c)) + p : mode == 2 ? b + p : b;
                    if (hud_call_regs(&c, hr32(&c, 0x004a57c0u), (const uint32_t[]){L(0x10), colour}, 2, NULL, &r)) goto fail;
                    eax = r.eax; edx = r.edx; ecx = r.ecx;
                    hw32(&c, L(0x10), hr32(&c, L(0x10)) + 1u);
                }
                hw32(&c, L(0x10), hr32(&c, L(0x10)) - sx);
                hw32(&c, L(0x18), hr32(&c, L(0x18)) + 1u);
                hw32(&c, L(0xc), hr32(&c, L(0xc)) + 1u);
                ecx = hr32(&c, L(0x18));
                more = (int32_t)ecx < (int32_t)hr32(&c, L(4));
            }
            eax = hr32(&c, L(4));
            hw32(&c, L(0xc), hr32(&c, L(0xc)) - eax);
        }
        hw32(&c, L(0x10), hr32(&c, L(0x10)) + sx);
        edx = hr32(&c, L(0x10));
        if ((int32_t)edx > (int32_t)hr32(&c, L(0x24))) {
            ecx = hr32(&c, L(0x2c));
            hw32(&c, L(0x10), ecx);
            eax = hr32(&c, L(4));
            hw32(&c, L(0xc), hr32(&c, L(0xc)) + eax);
        }
    }
out:
    hud_frame_pop(&c, top);
    sc->eax = eax; sc->edx = edx; sc->ecx = ecx;
    return;
fail:
    hud_frame_pop(&c, top);
    hud_failed(sc);
#undef L
}
/* 0x438230's step condition: the signed byte [0x4d2537] is 3 and the byte
 * [0x4d256c] is 0 (read again for each of the four steps). */
static int bitmap_double(const hud_ctx *c) { return (int8_t)hr8(c, 0x004d2537u) == 3 && !hr8(c, 0x004d256cu); }
/* 0x438230(R, B, P, a, b, mode) (listing 0x438230..0x438623), the HDD part
 * bitmap (the HDD draw calls it where 0x437e94 draws the bar). Under the step
 * condition: the bitmap's width and height s16(word [B+6] << word [0x4d253a])
 * and s16(word [B+4] << word [0x4d253a]) (both by 0x4d253a; only stored), sx =
 * 1 << word [0x4d253a], sy = 1 << word [0x4d253c] (the shift counts' low five
 * bits), else the words and 1. Then 0x437e94's canvas driver block, and R and P
 * moved by the port's +0x210 less its +0x220; a moved y0 below zero draws
 * nothing. Mode 0: R in steps of sx by sy, one bitmap byte per step from
 * [B+0xe] + ((R.y0 - P.y) >> (sy - 1)) * s16 [B+6] + ((R.x0 - P.x) >> (sx - 1))
 * (arithmetic shifts by the low five bits), at a row's end the rest of the
 * bitmap row skipped (s16 [B+6] less ((R.x1 - R.x0) >> (sx - 1)) less 1); a
 * byte equal to a is written as (b - a) + byte, an sx by sy block from pt.
 * Modes 1, 2 and 3: every pixel of R through the read slot; 1 and 3: a pixel
 * equal to a becomes b (mode 1 returns at once when b equals a); 2: a pixel
 * other than the s16 [0x4d3ba2] (read after each pixel read) becomes pixel + b.
 * Other modes draw nothing. The locals live in the original's 0x5c-byte frame
 * (L(k) = EBP-k, pt at L(0x1c)) and are read back from it, as the drivers get
 * &pt; EAX, EDX and ECX follow the listing to each exit. */
void es2_sim_service_part_bitmap(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t top = c.l->stack_hi, f = hud_frame_push(&c, 0x5cu);
    if (!f) { no_frame(&c, VA_PART_BITMAP, "part bitmap"); hud_failed(sc); return; }
#define L(k) (f + 0x5cu - (k))
#define LR(k) hr32(&c, L(k))
    uint32_t bm = sc->slots[1], a = sc->slots[3], b = sc->slots[4], mode = sc->slots[5], eax, edx, ecx, p;
    es2_sim_result r;
    uint16_t w = hr16(&c, bm + 6u);
    hw32(&c, L(4), hsx16(bitmap_double(&c) ? (uint16_t)((uint32_t)w << (hr16(&c, 0x004d253au) & 31u)) : w));
    w = hr16(&c, bm + 4u);
    hw32(&c, L(8), hsx16(bitmap_double(&c) ? (uint16_t)((uint32_t)w << (hr16(&c, 0x004d253au) & 31u)) : w));
    hw32(&c, L(0xc), bitmap_double(&c) ? 1u << (hr16(&c, 0x004d253au) & 31u) : 1u);
    hw32(&c, L(0x10), bitmap_double(&c) ? 1u << (hr16(&c, 0x004d253cu) & 31u) : 1u);
    uint32_t s = hr32(&c, 0x006c5ed0u), port = hr32(&c, 0x006c5f70u);
    hw32(&c, L(0x14), hr32(&c, s + 0x3cu));
    hw32(&c, s + 0x38u, LR(0x14));
    if (s == hr32(&c, 0x006c5ed0u)) {
        hw32(&c, 0x004a3686u, hr32(&c, s + 0x38u));
        if (hud_call(&c, hr32(&c, 0x004a57ecu), (const uint32_t[]){LR(0x14)}, 1, NULL)) goto fail;
    }
    uint32_t rect = sc->slots[0], pt = sc->slots[2];
    for (uint32_t k = 0; k < 4u; ++k) hw32(&c, L(0x4cu - 4u * k), hr32(&c, rect + 4u * k));
    for (uint32_t k = 0; k < 4u; ++k) hw32(&c, L(0x4cu - 4u * k), LR(0x4cu - 4u * k) + hr32(&c, port + 0x210u + 4u * (k & 1u)));
    for (uint32_t k = 0; k < 4u; ++k) hw32(&c, L(0x4cu - 4u * k), LR(0x4cu - 4u * k) - hr32(&c, port + 0x220u + 4u * (k & 1u)));
    eax = hr32(&c, port + 0x224u);
    edx = port + 0x210u;
    ecx = port + 0x220u;
    if ((int32_t)LR(0x48) < 0) goto out;
    hw32(&c, L(0x5c), hr32(&c, pt));
    hw32(&c, L(0x58), hr32(&c, pt + 4u));
    hw32(&c, L(0x5c), LR(0x5c) + hr32(&c, port + 0x210u));
    hw32(&c, L(0x58), LR(0x58) + hr32(&c, port + 0x214u));
    hw32(&c, L(0x5c), LR(0x5c) - hr32(&c, port + 0x220u));
    hw32(&c, L(0x58), LR(0x58) - hr32(&c, port + 0x224u));
    hw32(&c, L(0x54), LR(0x5c));
    hw32(&c, L(0x50), LR(0x58));
    hw32(&c, L(0x54), LR(0x54) + LR(4) - 1u);
    hw32(&c, L(0x50), LR(0x50) + LR(8) - 1u);
    hw32(&c, L(0x1c), LR(0x4c));
    hw32(&c, L(0x18), LR(0x48));
    hw32(&c, L(0x24), a);
    ecx = LR(0x48);
    eax = a;
    if (mode == 0) {
        hw32(&c, L(0x28), b - a);
        edx = hsx16(hr16(&c, bm + 6u));
        eax = (uint32_t)((int32_t)(LR(0x48) - LR(0x58)) >> ((LR(0x10) - 1u) & 31u)) * edx;
        edx = (uint32_t)((int32_t)(LR(0x4c) - LR(0x5c)) >> ((LR(0xc) - 1u) & 31u));
        hw32(&c, L(0x2c), eax + edx);
        edx = hsx16(hr16(&c, bm + 6u));
        ecx = LR(0xc) - 1u;
        eax = (uint32_t)((int32_t)(LR(0x44) - LR(0x4c)) >> (ecx & 31u));
        hw32(&c, L(0x30), edx - eax - 1u);
        edx = LR(0x30);
        hw32(&c, L(0x20), hr32(&c, bm + 0xeu) + LR(0x2c));
        for (;;) {
            eax = LR(0x18);
            if ((int32_t)eax > (int32_t)LR(0x40)) break;
            edx = LR(0x1c);
            if ((int32_t)edx > (int32_t)LR(0x44)) break;
            edx = LR(0x20);
            p = hr8(&c, edx);
            if ((int32_t)p >= (int32_t)a && (int32_t)p <= (int32_t)LR(0x24)) {
                for (uint32_t i = 0; (int32_t)i < (int32_t)LR(0x10); ++i) {
                    for (uint32_t j = 0; (int32_t)j < (int32_t)LR(0xc); ++j) {
                        if (hud_call_regs(&c, hr32(&c, 0x004a57c0u), (const uint32_t[]){L(0x1c), LR(0x28) + p}, 2, NULL, &r)) goto fail;
                        eax = r.eax; edx = r.edx; ecx = r.ecx;
                        hw32(&c, L(0x1c), LR(0x1c) + 1u);
                    }
                    ecx = LR(0xc);
                    hw32(&c, L(0x1c), LR(0x1c) - ecx);
                    hw32(&c, L(0x18), LR(0x18) + 1u);
                }
                eax = LR(0x10);
                hw32(&c, L(0x18), LR(0x18) - eax);
            }
            edx = LR(0xc);
            hw32(&c, L(0x1c), LR(0x1c) + edx);
            hw32(&c, L(0x20), LR(0x20) + 1u);
            ecx = LR(0x1c);
            if ((int32_t)ecx > (int32_t)LR(0x44)) {
                eax = LR(0x4c);
                hw32(&c, L(0x1c), eax);
                edx = LR(0x10);
                hw32(&c, L(0x18), LR(0x18) + edx);
                ecx = LR(0x30);
                hw32(&c, L(0x20), LR(0x20) + ecx);
            }
        }
    } else if (mode - 1u < 3u) {
        edx = 0;
        if (mode == 1u) {
            ecx = b;
            if (b == a) goto out;
        }
        for (;;) {
            ecx = LR(0x18);
            if ((int32_t)ecx > (int32_t)LR(0x40)) break;
            eax = LR(0x1c);
            if ((int32_t)eax > (int32_t)LR(0x44)) break;
            if (hud_call_regs(&c, hr32(&c, 0x004a57bcu), (const uint32_t[]){L(0x1c)}, 1, NULL, &r)) goto fail;
            ecx = L(0x1c);
            eax = p = r.eax;
            edx = r.edx;
            int match;
            uint32_t local = mode == 1u ? 0x34u : mode == 2u ? 0x38u : 0x3cu;
            if (mode == 2u) {
                eax = hsx16(hr16(&c, 0x004d3ba2u));
                match = p != eax;
            } else if (mode == 1u) {
                match = p == a;
            } else {
                match = (int32_t)p >= (int32_t)a && (int32_t)p <= (int32_t)LR(0x24);
            }
            if (match) {
                if (mode != 2u) eax = b;
                hw32(&c, L(local), mode == 2u ? p + b : b);
                if (hud_call_regs(&c, hr32(&c, 0x004a57c0u), (const uint32_t[]){L(0x1c), LR(local)}, 2, NULL, &r)) goto fail;
                eax = r.eax; edx = r.edx; ecx = r.ecx;
            }
            hw32(&c, L(0x1c), LR(0x1c) + 1u);
            eax = LR(0x1c);
            if ((int32_t)eax > (int32_t)LR(0x44)) {
                edx = LR(0x4c);
                hw32(&c, L(0x1c), edx);
                hw32(&c, L(0x18), LR(0x18) + 1u);
            }
        }
    } else {
        edx = mode - 3u;
    }
out:
    hud_frame_pop(&c, top);
    sc->eax = eax; sc->edx = edx; sc->ecx = ecx;
    return;
fail:
    hud_frame_pop(&c, top);
    hud_failed(sc);
#undef LR
#undef L
}
/* 0x440370(X) (listing 0x440370..0x4403e0), the summed lock values of the MFD
 * entries (0x43fe8c calls it; X is not read): strlen([0x4d1664]) with the
 * result unused, then for each of the [P+0x98] entries at P+0x70 (P =
 * [0x49b088]) with an entry E and E's range object Q = [E+0xbd]: Q's method
 * +0x60 (Q, &w); an AX other than 5 adds the s16 w. EAX and ECX the sum; EDX
 * the last method's, or strlen's. The word w is the call area's zero model
 * (own_process.md section 5) until a method writes it. */
void es2_sim_service_target_locked(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t top = c.l->stack_hi, f = hud_frame_push(&c, 8u);
    if (!f) { hud_refuse(&c, 0x00440370u, "target locked", "the own code's call area is exhausted"); hud_failed(sc); return; }
    uint32_t p = hr32(&c, VA_PANEL), n = hr32(&c, p + 0x98u), e = p + 0x70u, w = f + 2u, sum = 0;
    es2_sim_result r;
    if (hud_call_regs(&c, VA_STRLEN, (const uint32_t[]){hr32(&c, 0x004d1664u)}, 1, (const uint32_t[3]){p, sc->edx, sc->ecx}, &r)) goto fail;
    uint32_t edx = r.edx;
    for (uint32_t i = 0; (int32_t)i < (int32_t)n; ++i, e += 4u) {
        uint32_t x = hr32(&c, e), q = x ? hr32(&c, x + 0xbdu) : 0;
        if (!q) continue;
        uint32_t vt = hr32(&c, q);
        if (hud_call_regs(&c, hr32(&c, vt + 0x60u), (const uint32_t[]){q, w}, 2, (const uint32_t[3]){q, w, vt}, &r)) goto fail;
        edx = r.edx;
        if ((uint16_t)r.eax != 5u) sum += hsx16(hr16(&c, w));
    }
    hud_frame_pop(&c, top);
    sc->eax = sum; sc->edx = edx; sc->ecx = sum;
    return;
fail:
    hud_frame_pop(&c, top);
    hud_failed(sc);
}
/* 0x43b288(T, i, flag, k) (listing 0x43b288..0x43b358), the status page's
 * condition tail (the status page passes the method +0x40's last two arguments
 * on as flag and k): T's byte +0x34 set: the labels [T+0x1c] 0x49bdba and, with
 * the low byte of flag, [T+0x20] 0x49bdc1. Otherwise a word k of -1 is replaced
 * by damage_class(i); the label [T+0x1c] the class string [0x4d1614 + 4 *
 * (s16)k]; with flag, [T+0x20] the percentage text: the 3 bytes at 0x49be2c,
 * itoa(((0x100 - i) * 100) >> 8, at +2, 10) and strcat of 0x49be2f, in the 12
 * bytes of locals (the call area's zero model). EAX/EDX/ECX the last call's. */
void es2_sim_service_status_tail(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t t = sc->slots[0], i = sc->slots[1], k = sc->slots[3];
    uint8_t flag = (uint8_t)sc->slots[2];
    uint32_t top = c.l->stack_hi, f = hud_frame_push(&c, 0xcu);
    if (!f) { hud_refuse(&c, 0x0043b288u, "status tail", "the own code's call area is exhausted"); hud_failed(sc); return; }
    es2_sim_result r;
    uint32_t a = hr32(&c, t + 0x1cu);
    if (hr8(&c, t + 0x34u)) {
        if (hud_call_regs(&c, 0x00438920u, (const uint32_t[]){a, 0x0049bdbau}, 2, (const uint32_t[3]){a, sc->edx, sc->ecx}, &r)) goto fail;
        if (flag) {
            uint32_t b = hr32(&c, t + 0x20u);
            if (hud_call_regs(&c, 0x00438920u, (const uint32_t[]){b, 0x0049bdc1u}, 2, (const uint32_t[3]){r.eax, b, r.ecx}, &r)) goto fail;
        }
    } else {
        uint32_t eax = k, edx = sc->edx;
        if ((uint16_t)k == 0xffffu) {
            if (hud_call_regs(&c, 0x00438700u, (const uint32_t[]){i}, 1, (const uint32_t[3]){k, sc->edx, sc->ecx}, &r)) goto fail;
            eax = r.eax; edx = r.edx;
        }
        (void)edx;
        uint32_t cls = hsx16((uint16_t)eax), s = hr32(&c, 0x004d1614u + 4u * cls);
        if (hud_call_regs(&c, 0x00438920u, (const uint32_t[]){a, s}, 2, (const uint32_t[3]){s, a, cls}, &r)) goto fail;
        if (flag) {
            uint32_t d = 0x100u - i, q = (uint32_t)((int32_t)(d * 100u) >> 8);
            for (uint32_t j = 0; j < 3u; ++j) hw8(&c, f + j, hr8(&c, 0x0049be2cu + j));
            if (hud_call_regs(&c, 0x00496820u, (const uint32_t[]){q, f + 2u, 0xau}, 3, (const uint32_t[3]){f, f + 2u, d}, &r)) goto fail;
            if (hud_call_regs(&c, 0x004967eau, (const uint32_t[]){f, 0x0049be2fu}, 2, (const uint32_t[3]){r.eax, r.edx, f}, &r)) goto fail;
            uint32_t b = hr32(&c, t + 0x20u);
            if (hud_call_regs(&c, 0x00438920u, (const uint32_t[]){b, f}, 2, (const uint32_t[3]){f, b, r.ecx}, &r)) goto fail;
        }
    }
    hud_frame_pop(&c, top);
    sc->eax = r.eax; sc->edx = r.edx; sc->ecx = r.ecx;
    return;
fail:
    hud_frame_pop(&c, top);
    hud_failed(sc);
}
/* 0x451de0(x, L, pt) (listing 0x451de0..0x451e39), the HDD's part bitmaps: for
 * i < [L] (read each pass) over the 0x22-byte entries from [L+4], an entry with
 * a bitmap [E] goes to 0x45c964(0x4d24bc, [E], E+4 + pt, [E+0x1c]); x is not
 * read. Registers: EAX the last entry's bitmap or the call's EAX, EDX the last
 * call's (or as on entry), ECX the local y popped at the end: the last drawn
 * entry's y, or with none drawn the unwritten local's stack residue, here 0
 * (the zero model, own_process.md, "the headless acceptance"). */
void es2_sim_service_hdd_parts(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t l = sc->slots[1], pt = sc->slots[2], e = hr32(&c, l + 4u);
    uint32_t top = c.l->stack_hi, f = hud_frame_push(&c, 8u);
    if (!f) {
        hud_refuse(&c, VA_HDD_PARTS, "HDD parts", "the own code's call area is exhausted");
        hud_failed(sc);
        return;
    }
    uint32_t y = 0;
    for (int32_t i = 0; i < (int32_t)hr32(&c, l); ++i, e += 0x22u) {
        uint32_t b = hr32(&c, e);
        sc->eax = b;
        if (!b) continue;
        hw32(&c, f, hr32(&c, e + 4u) + hr32(&c, pt));
        hw32(&c, f + 4u, hr32(&c, e + 8u) + hr32(&c, pt + 4u));
        uint32_t z = hr32(&c, e + 0x1cu);
        es2_sim_result r;
        if (hud_call_regs(&c, 0x0045c964u, (const uint32_t[]){VA_DESC, b, f, z}, 4, (const uint32_t[3]){b, f, z}, &r)) {
            hud_frame_pop(&c, top);
            hud_failed(sc);
            return;
        }
        sc->eax = r.eax; sc->edx = r.edx;
        y = hr32(&c, f + 4u);
    }
    sc->ecx = y;
    hud_frame_pop(&c, top);
}
