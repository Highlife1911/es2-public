/* The HUD gadgets as own code: the fixed gunsight 0x43b808, the locking
 * gunsight 0x43b978, the roving gunsight 0x43c204 with the cursor shape
 * 0x433d90 and the mouse position 0x486ece, the fixed rotation gauge 0x43b4cc,
 * the rotation step 0x43b67c and the redraw 0x43b7cc of the reverse gauge, the
 * nav gauge 0x43c40c with its marker point 0x43495c. Contract: hud.md, "the
 * gunsights and gauges as own code"; every function names its listing range of
 * the reference DBSIM.EXE (llvm-objdump -d -M intel, SHA-256 8561aa47...c4ad).
 *
 * Every guest read and write goes through the bound HUD's guest view in the
 * listing's order; every call of other code (view.c's rows, gfx.c's blit
 * 0x4816a8, the canvas push/pop, the registry hooks, the runtime's
 * itoa/strcat, the label 0x438920) goes through the dispatch. The stack locals
 * the original hands to callees live in a frame of the call area at the
 * listing's offsets. The 2D polygon fills 0x483eb8 and 0x4842bb go through
 * their rows. An integer divide by zero or a quotient overflow (the
 * original's #DE) is refused by name. */
#include "gadgets_internal.h"

#define VA_GC 0x006c5f70u
#define VA_VIEW 0x006c5ed0u
#define VA_PANEL 0x0049b088u
#define VA_PLAYER_VIEW 0x004d24eau  /* the view object: +4 the eye, +0x1a a shift, +0x1e the near distance */
#define VA_BITMAPS 0x0049be78u      /* -> {.., +8 the HUD bitmap table} */
#define VA_STEPS 0x0049be70u        /* -> {+4 count, +8 the gauge bitmap table} */
#define VA_CAMERA 0x006c5ff8u
#define VA_SHIFT_X 0x004d253au
#define VA_SHIFT_Y 0x004d253cu
#define VA_BLIT 0x004816a8u
#define VA_CANVAS_PUSH 0x004311e0u
#define VA_CANVAS_POP 0x00431210u
#define VA_OBJECT_SELECT 0x0048c444u
#define VA_VIEW_TRANSFORM 0x0048c57cu
#define VA_PROJECT 0x0048c5fcu
#define VA_TO_SCREEN 0x0048c6d0u
#define VA_DIST2D 0x004928d0u
#define VA_DIST3D 0x0047de72u
#define VA_ANGLE_RATIO 0x0047d32cu
#define VA_SINCOS 0x0047eb30u
#define VA_ROTATE2D 0x0047ffe8u
#define VA_DISTANCE_DISPLAY 0x0047d694u
#define VA_VECTOR_ANGLE 0x0049290cu
#define VA_ROUTE_POINT 0x00423b0cu
#define VA_POLY_FILL 0x00483eb8u
#define VA_POLY_ANY 0x004842bbu
#define VA_RADAR_UNITS 0x00434228u
#define VA_LABEL 0x00438920u
#define VA_CURSOR_SHAPE 0x00433d90u
#define VA_MOUSE_POSITION 0x00486eceu
#define VA_MARKER_POINT 0x0043495cu
#define VA_MUL16 0x0047e08du

static int32_t sx16(const hud_ctx *c, uint32_t a) { return (int32_t)(int16_t)hr16(c, a); }
static int32_t s32(const hud_ctx *c, uint32_t a) { return (int32_t)hr32(c, a); }
static void add32(const hud_ctx *c, uint32_t a, uint32_t v) { hw32(c, a, hr32(c, a) + v); }
static unsigned shx(const hud_ctx *c) { return hr16(c, VA_SHIFT_X) & 31u; }
static unsigned shy(const hud_ctx *c) { return hr16(c, VA_SHIFT_Y) & 31u; }
/* cdq / xor / sub: |v|, with |INT32_MIN| = INT32_MIN. */
static int32_t abs_cdq(uint32_t v) { return (int32_t)((int32_t)v < 0 ? 0u - v : v); }
static uint32_t sar1(uint32_t v) { return (uint32_t)((int32_t)v >> 1); }
static uint32_t hud_bitmap(const hud_ctx *c, uint32_t i) { return hr32(c, hr32(c, hr32(c, VA_BITMAPS) + 8u) + 4u * i); }
static void copy8(const hud_ctx *c, uint32_t d, uint32_t s) {
    hw32(c, d, hr32(c, s));
    hw32(c, d + 4u, hr32(c, s + 4u));
}
static int canvas_push(hud_ctx *c, uint32_t *canvas) { return HCALL0(c, VA_CANVAS_PUSH, canvas); }
static int canvas_pop(hud_ctx *c) { return HCALL0(c, VA_CANVAS_POP, NULL); }
static int blit(hud_ctx *c, uint32_t bitmap, uint32_t point, uint32_t flags) { return HCALL(c, VA_BLIT, NULL, bitmap, point, flags); }
/* idiv: 0 with the quotient, or the refusal of the original's #DE. */
static int idiv(hud_ctx *c, uint32_t va, const char *name, int32_t a, int32_t b, int32_t *q, int32_t *r) {
    if (b == 0 || (a == INT32_MIN && b == -1)) return hud_divide_refuse(c, va, (uint64_t)(int64_t)a, (uint32_t)b, name, "an integer divide fault (#DE) in the original");
    if (q) *q = a / b;
    if (r) *r = a % b;
    return 0;
}
/* A polygon fill (count, points) through the dispatch (fill.c's rows;
 * nothing is counted headless). */
static int polygon(hud_ctx *c, uint32_t va, uint32_t count, uint32_t points) { return HCALL(c, va, NULL, count, points); }
static int frame_refused(hud_ctx *c, uint32_t va, const char *name) { return hud_refuse(c, va, name, "the own code's call area is exhausted"); }

/* ---- the gunsights */

/* 0x43b808(D) (listing 0x43b808..0x43b94c), the fixed gunsight. Frame
 * 0x24: out -0x24 (12), pt -0x18 (12), s -8 (8). The canvas pushed.
 * Without a target (D+0x1a = 0) bitmap 0 of the table. With one: the
 * camera [0x6c5ff8] kept, 0x48c444(0); pt <- the 12 bytes at D+0x1e;
 * 0x48c57c(&pt, &out); when [[0x4d24ea]+0x1e] > out+4 (signed) bitmap 0;
 * else 0x48c5fc(&out, &s), 0x48c6d0(&s, &s) and bitmap 0 when 5 << the x
 * shift < |s.x - [D+0x42]|, else 1 (byte D+0x39 set) or 2 when 5 << the y
 * shift >= |s.y - [D+0x46]|, else 0; 0x48c444(the kept camera). The
 * bitmap blitted (flags 0) centred on (D+0x42, D+0x46) (half its width
 * word +6 and height word +4, sar 1); the canvas popped. */
int es2_sim_gadget_gunsight_fixed(hud_ctx *c, uint32_t d) {
    if (canvas_push(c, NULL)) return 1;
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0x24u);
    if (!f) return frame_refused(c, 0x0043b808u, "gunsight_fixed");
#define L(k) (f + 0x24u - (k))
    int bad = 1;
    uint32_t bmp;
    if (!hr32(c, d + 0x1au)) bmp = hud_bitmap(c, 0);
    else {
        uint32_t saved = hr32(c, VA_CAMERA);
        if (HCALL(c, VA_OBJECT_SELECT, NULL, 0u)) goto done;
        hw32(c, L(0x10), hr32(c, d + 0x26u));
        hw32(c, L(0x18), hr32(c, d + 0x1eu));
        hw32(c, L(0x14), hr32(c, d + 0x22u));
        if (HCALL(c, VA_VIEW_TRANSFORM, NULL, L(0x18), L(0x24))) goto done;
        if (s32(c, hr32(c, VA_PLAYER_VIEW) + 0x1eu) > s32(c, L(0x20))) bmp = hud_bitmap(c, 0);
        else {
            if (HCALL(c, VA_PROJECT, NULL, L(0x24), L(8)) || HCALL(c, VA_TO_SCREEN, NULL, L(8), L(8))) goto done;
            if ((int32_t)(5u << shx(c)) < abs_cdq(hr32(c, L(8)) - hr32(c, d + 0x42u))) bmp = hud_bitmap(c, 0);
            else if ((int32_t)(5u << shy(c)) >= abs_cdq(hr32(c, L(4)) - hr32(c, d + 0x46u))) bmp = hud_bitmap(c, hr8(c, d + 0x39u) ? 1u : 2u);
            else bmp = hud_bitmap(c, 0);
        }
        if (HCALL(c, VA_OBJECT_SELECT, NULL, saved)) goto done;
    }
    hw32(c, L(8), hr32(c, d + 0x42u));
    hw32(c, L(4), hr32(c, d + 0x46u));
    add32(c, L(8), 0u - (uint32_t)(sx16(c, bmp + 6u) >> 1));
    add32(c, L(4), 0u - (uint32_t)(sx16(c, bmp + 4u) >> 1));
    if (blit(c, bmp, L(8), 0)) goto done;
    bad = canvas_pop(c);
done:
#undef L
    hud_frame_pop(c, top);
    return bad;
}

/* 0x43b978(D) (listing 0x43b978..0x43c146), the locking gunsight: only
 * with a target (D+0x1a) and byte D+0x38. Frame 0x114 (offsets below are
 * the listing's). The 0x20 bytes of the camera [0x6c5ff8] copied to -0xf0
 * and a flag (bl) set; 0x48c444(0); pt -0xc4 <- the 12 bytes at D+0x1e;
 * 0x48c57c(&pt, &out -0xd0); when [[0x4d24ea]+0x1e] > out.y (signed) the
 * flag is cleared and an out.y of 0 becomes 1; 0x48c5fc(&out, &s -8),
 * 0x48c6d0(&s, &s).
 *
 * The bracket (the flag set and s outside the +-5 << shift box around
 * (D+0x42, D+0x46)): the canvas popped; the half size e = ((sar 1
 * [D+0x30]) << [[0x4d24ea]+0x1a]) idiv dist2d([[0x49b088]+0x203]+0x26,
 * D+0x1e); the vertical span s.y -+ e clamped to 25..75 (the excess
 * halved as a word); D+0x4a..+0x56 <- the bracket rectangle; the bitmap
 * table index 7 (D+0x39, or s strictly inside the box) or 3; that bitmap
 * centred on s; with word D+0x2c = 0 the four corners (bitmap idx+1 with
 * flags 0, 2, 1, 3) and the four edge marks (idx+3 below and above, idx+2
 * right and left); the canvas pushed again.
 *
 * The arrow (0x43bdd3; skipped when the flag is set and s lies inside the
 * rectangle D+4..D+0x10): the delta pt - [0x4d24ea]+4 to dist3d 0x47de72;
 * the flag cleared when r < out.z or -r > out.z; without the flag out <-
 * (+-10000, 0x400, 0) by the sign of out.x, projected again and s.y <-
 * [D+0x46]. The direction (s - (D+0x42, D+0x46), zeros as 1) cut at the
 * rectangle's side (x) or its top/bottom (y); the angle 0x47d32c + 0xc000
 * to sincos 0x47eb30 into the matrix -0x7c; three points (-hx, -t), (0,
 * 0), (hx, -t) with hx = (6 << y shift) sar 1 and t = 10 << y shift into
 * a vector of three (0x49667c) through rotate_2d 0x47ffe8, moved to the
 * cut point; the fill record {0, colour} at -0xb0 put into GC+0x22c (the
 * colour word 0x4d3b8e with D+0x39, else 0x4d3b94, sign-extended); the
 * polygon 0x4842bb(3, points). In any case the kept camera reselected
 * (0x48c444(&copy)). */
int es2_sim_gadget_gunsight_target(hud_ctx *c, uint32_t d) {
    if (!hr32(c, d + 0x1au) || !hr8(c, d + 0x38u)) return 0;
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0x114u);
    if (!f) return frame_refused(c, 0x0043b978u, "gunsight_target");
#define L(k) (f + 0x114u - (k))
    int bad = 1;
    uint32_t cam = hr32(c, VA_CAMERA);
    int flag = 1;
    for (uint32_t i = 0; i < 0x20u; i += 4u) hw32(c, L(0xf0) + i, hr32(c, cam + i));
    if (HCALL(c, VA_OBJECT_SELECT, NULL, 0u)) goto done;
    hw32(c, L(0xbc), hr32(c, d + 0x26u));
    hw32(c, L(0xc4), hr32(c, d + 0x1eu));
    hw32(c, L(0xc0), hr32(c, d + 0x22u));
    if (HCALL(c, VA_VIEW_TRANSFORM, NULL, L(0xc4), L(0xd0))) goto done;
    if (s32(c, hr32(c, VA_PLAYER_VIEW) + 0x1eu) > s32(c, L(0xcc))) {
        flag = 0;
        if (!hr32(c, L(0xcc))) hw32(c, L(0xcc), 1u);
    }
    if (HCALL(c, VA_PROJECT, NULL, L(0xd0), L(8)) || HCALL(c, VA_TO_SCREEN, NULL, L(8), L(8))) goto done;
    if (flag && ((int32_t)(5u << shx(c)) < abs_cdq(hr32(c, L(8)) - hr32(c, d + 0x42u)) ||
                 (int32_t)(5u << shy(c)) < abs_cdq(hr32(c, L(4)) - hr32(c, d + 0x46u)))) {
        if (canvas_pop(c)) goto done;
        uint32_t size = sar1(hr32(c, d + 0x30u)), dist = 0;
        if (HCALL(c, VA_DIST2D, &dist, hr32(c, hr32(c, VA_PANEL) + 0x203u) + 0x26u, d + 0x1eu)) goto done;
        int32_t e;
        if (idiv(c, 0x0043b978u, "gunsight_target", (int32_t)(size << (hr32(c, hr32(c, VA_PLAYER_VIEW) + 0x1au) & 31u)), (int32_t)dist, &e, NULL))
            goto done;
        hw32(c, L(0x18), hr32(c, L(8)));
        hw32(c, L(0x14), hr32(c, L(4)));
        hw32(c, L(0x10), hr32(c, L(0x18)));
        hw32(c, L(0xc), hr32(c, L(0x14)));
        add32(c, L(0x14), 0u - (uint32_t)e);
        add32(c, L(0xc), (uint32_t)e);
        uint32_t h = hr32(c, L(0xc)) - hr32(c, L(0x14));
        if ((int32_t)h < 0) h = 0u - h;
        if ((int32_t)h < 25) {
            uint16_t k = (uint16_t)sar1(25u - h);
            hw16(c, L(0x1e), k);
            add32(c, L(0x14), 0u - hsx16(k));
            add32(c, L(0xc), hsx16(k));
            h = 25u;
        } else if ((int32_t)h > 75) {
            uint16_t k = (uint16_t)sar1(h - 75u);
            hw16(c, L(0x20), k);
            add32(c, L(0x14), hsx16(k));
            add32(c, L(0xc), 0u - hsx16(k));
            h = 75u;
        }
        hw32(c, d + 0x4au, hr32(c, L(0x18)) - sar1(h));
        hw32(c, d + 0x4eu, hr32(c, L(0x14)));
        hw32(c, d + 0x52u, sar1(h) + hr32(c, L(0x18)));
        hw32(c, d + 0x56u, hr32(c, L(0xc)));
        int inside = 0;
        if ((int32_t)(5u << shx(c)) > abs_cdq(hr32(c, L(8)) - hr32(c, d + 0x42u))) {
            uint32_t dy = hr32(c, L(4)) - hr32(c, d + 0x46u);
            hw32(c, L(0x30), dy);
            inside = (int32_t)(5u << shy(c)) > abs_cdq(dy);
        }
        uint32_t idx = (hr8(c, d + 0x39u) || inside) ? 7u : 3u;
        hw32(c, L(0x2c), idx);
        uint32_t bmp = hud_bitmap(c, idx);
        hw32(c, L(0x3c), (uint32_t)(sx16(c, bmp + 4u) >> 1));
        hw32(c, L(0x40), (uint32_t)(sx16(c, bmp + 6u) >> 1));
        hw32(c, L(0x38), hr32(c, L(0x40)));
        hw32(c, L(0x34), hr32(c, L(0x3c)));
        add32(c, L(8), 0u - hr32(c, L(0x38)));
        add32(c, L(4), 0u - hr32(c, L(0x34)));
        if (blit(c, bmp, L(8), 0)) goto done;
        add32(c, L(8), hr32(c, L(0x38)));
        add32(c, L(4), hr32(c, L(0x34)));
        if (!hr16(c, d + 0x2cu)) {
            uint32_t corner = hud_bitmap(c, idx + 1u);
            hw32(c, L(0x24), hud_bitmap(c, idx + 2u));
            hw32(c, L(0x28), hud_bitmap(c, idx + 3u));
            hw32(c, L(0x10), hr32(c, d + 0x52u));
            hw32(c, L(0xc), hr32(c, d + 0x4eu));
            hw32(c, L(0x18), hr32(c, d + 0x4au));
            hw32(c, L(0x14), hr32(c, d + 0x56u));
            hw32(c, L(0x44), (uint32_t)sx16(c, corner + 6u));
            hw32(c, L(0x48), (uint32_t)sx16(c, corner + 4u));
            if (blit(c, corner, d + 0x4au, 0)) goto done;
            add32(c, L(0x10), 0u - hr32(c, L(0x44)));
            if (blit(c, corner, L(0x10), 2)) goto done;
            add32(c, L(0x14), 0u - hr32(c, L(0x48)));
            if (blit(c, corner, L(0x18), 1)) goto done;
            add32(c, d + 0x52u, 0u - hr32(c, L(0x44)));
            add32(c, d + 0x56u, 0u - hr32(c, L(0x48)));
            if (blit(c, corner, d + 0x52u, 3)) goto done;
            uint32_t mark_v = hr32(c, L(0x28)), mark_h = hr32(c, L(0x24));
            hw32(c, L(0x10), hr32(c, L(8)));
            hw32(c, L(0xc), hr32(c, d + 0x56u) + hr32(c, L(0x48)));
            if (blit(c, mark_v, L(0x10), 0)) goto done;
            hw32(c, L(0xc), hr32(c, d + 0x4eu) - (uint32_t)sx16(c, mark_v + 4u));
            if (blit(c, mark_v, L(0x10), 0)) goto done;
            hw32(c, L(0x10), hr32(c, d + 0x52u) + hr32(c, L(0x44)));
            hw32(c, L(0xc), hr32(c, L(4)));
            if (blit(c, mark_h, L(0x10), 0)) goto done;
            hw32(c, L(0x10), hr32(c, d + 0x4au) - (uint32_t)sx16(c, mark_h + 6u));
            if (blit(c, mark_h, L(0x10), 0)) goto done;
        }
        if (canvas_push(c, NULL)) goto done;
    }
    /* 0x43bdd3: the arrow towards a target off the sight's rectangle. */
    if (!(flag && s32(c, L(8)) >= s32(c, d + 4u) && s32(c, L(8)) <= s32(c, d + 0xcu) && s32(c, L(4)) >= s32(c, d + 8u) &&
          s32(c, L(4)) <= s32(c, d + 0x10u))) {
        uint32_t eye = hr32(c, VA_PLAYER_VIEW) + 4u;
        hw32(c, L(0xfc), hr32(c, L(0xc4)));
        hw32(c, L(0xf8), hr32(c, L(0xc0)));
        hw32(c, L(0xf4), hr32(c, L(0xbc)));
        add32(c, L(0xf4), 0u - hr32(c, eye + 8u));
        add32(c, L(0xfc), 0u - hr32(c, eye));
        add32(c, L(0xf8), 0u - hr32(c, eye + 4u));
        hw32(c, L(0x4c), hr32(c, L(0xf4)));
        hw32(c, L(0x50), hr32(c, L(0xf8)));
        hw32(c, L(0x54), hr32(c, L(0xfc)));
        uint32_t r = 0;
        if (HCALL(c, VA_DIST3D, &r, hr32(c, L(0x54)), hr32(c, L(0x50)), hr32(c, L(0x4c)))) goto done;
        if ((int32_t)r < s32(c, L(0xc8)) || (int32_t)(0u - r) > s32(c, L(0xc8))) flag = 0;
        if (!flag) {
            hw32(c, L(0xd0), s32(c, L(0xd0)) > 0 ? 10000u : (uint32_t)-10000);
            hw32(c, L(0xcc), 0x400u);
            hw32(c, L(0xc8), 0);
            if (HCALL(c, VA_PROJECT, NULL, L(0xd0), L(8)) || HCALL(c, VA_TO_SCREEN, NULL, L(8), L(8))) goto done;
            hw32(c, L(4), hr32(c, d + 0x46u));
        }
        hw32(c, L(0x5c), hr32(c, L(8)));
        hw32(c, L(0x58), hr32(c, L(4)));
        add32(c, L(0x5c), 0u - hr32(c, d + 0x42u));
        add32(c, L(0x58), 0u - hr32(c, d + 0x46u));
        if (!hr32(c, L(0x5c))) hw32(c, L(0x5c), 1u);
        if (!hr32(c, L(0x58))) hw32(c, L(0x58), 1u);
        uint32_t side = (s32(c, L(8)) < s32(c, d + 0x42u) ? hr32(c, d + 4u) : hr32(c, d + 0xcu)) - hr32(c, d + 0x42u);
        int32_t y;
        if (idiv(c, 0x0043b978u, "gunsight_target", (int32_t)(side * hr32(c, L(0x58))), s32(c, L(0x5c)), &y, NULL)) goto done;
        y += s32(c, d + 0x46u);
        if (y > s32(c, d + 8u) && y < s32(c, d + 0x10u)) {
            hw32(c, L(0x64), side);
            hw32(c, L(0x60), (uint32_t)y - hr32(c, d + 0x46u));
        } else {
            uint32_t centre = hr32(c, d + 0x46u);
            hw32(c, L(0x60), (y < (int32_t)centre ? hr32(c, d + 8u) : hr32(c, d + 0x10u)) - centre);
            int32_t x;
            if (idiv(c, 0x0043b978u, "gunsight_target", (int32_t)(hr32(c, L(0x60)) * hr32(c, L(0x5c))), s32(c, L(0x58)), &x, NULL)) goto done;
            hw32(c, L(0x64), (uint32_t)x);
        }
        hw32(c, L(0x6c), hr32(c, L(0x60)));
        hw32(c, L(0x70), hr32(c, L(0x64)));
        uint32_t a = 0;
        if (HCALL(c, VA_ANGLE_RATIO, &a, hr32(c, L(0x70)), hr32(c, L(0x6c)))) goto done;
        uint16_t angle = (uint16_t)(a + 0xc000u);
        hw16(c, L(0x66), (uint16_t)a);
        hw16(c, L(0x72), angle);
        hw16(c, L(0x7e), angle);
        if (HCALL(c, VA_SINCOS, NULL, (a & 0xffff0000u) | angle, L(0x7c))) goto done;
        uint32_t hx = sar1(6u << shy(c)), t = 10u << shy(c);
        hw32(c, L(0x88), 0u - hx);
        hw32(c, L(0x84), 0u - t);
        hw32(c, L(0x90), 0);
        hw32(c, L(0x8c), 0);
        hw32(c, L(0x98), hx);
        hw32(c, L(0x94), 0u - t);
        if (HCALL(c, HUD_VA_VECTOR_NEW, NULL, L(0x114), 8u, 3u, 1u, HUD_VA_EMPTY_CTOR)) goto done;
        if (HCALL(c, VA_ROTATE2D, NULL, L(0x88), L(0x7c), L(0x114)) || HCALL(c, VA_ROTATE2D, NULL, L(0x90), L(0x7c), L(0x10c)) ||
            HCALL(c, VA_ROTATE2D, NULL, L(0x98), L(0x7c), L(0x104)))
            goto done;
        hw32(c, L(0xa0), hr32(c, d + 0x42u));
        hw32(c, L(0x9c), hr32(c, d + 0x46u));
        add32(c, L(0xa0), hr32(c, L(0x64)));
        add32(c, L(0x9c), hr32(c, L(0x60)));
        uint32_t ox = hr32(c, L(0xa0)), oy = hr32(c, L(0x9c));
        for (uint32_t i = 0; i < 3u; ++i) {
            add32(c, L(0x114) + 8u * i, ox);
            add32(c, L(0x110) + 8u * i, oy);
        }
        hw32(c, L(0xa4), hr32(c, VA_VIEW));
        hw32(c, L(0xa8), hr32(c, VA_GC));
        hw32(c, L(0xb0), 0);
        hw32(c, L(0xb8), 0);
        hw32(c, hr32(c, L(0xa8)) + 0x22cu, L(0xb0));
        hw32(c, L(0xac), hsx16(hr16(c, hr8(c, d + 0x39u) ? 0x004d3b8eu : 0x004d3b94u)));
        if (polygon(c, VA_POLY_ANY, 3u, L(0x114))) goto done;
    }
    bad = HCALL(c, VA_OBJECT_SELECT, NULL, L(0xf0));
done:
#undef L
    hud_frame_pop(c, top);
    return bad;
}

/* 0x43c204(D) (listing 0x43c204..0x43c267), the roving gunsight: D+0x42
 * <- the mouse position (0x486ece); the cursor shape 0x433d90(1) when it
 * lies inside the rectangle D+4..D+0x10 (signed, edges included), else
 * 0x433d90(0). */
int es2_sim_gadget_roving_gunsight(hud_ctx *c, uint32_t d) {
    if (HCALL(c, VA_MOUSE_POSITION, NULL, d + 0x42u)) return 1;
    int32_t x0 = s32(c, d + 4u), y0 = s32(c, d + 8u), x1 = s32(c, d + 0xcu), y1 = s32(c, d + 0x10u), x = s32(c, d + 0x42u);
    int in = x >= x0 && x <= x1 && s32(c, d + 0x46u) >= y0 && s32(c, d + 0x46u) <= y1;
    return HCALL(c, VA_CURSOR_SHAPE, NULL, in ? 1u : 0u);
}

/* 0x433d90(i) (listing 0x433d90..0x433e3a), the cursor shape: only with
 * byte 0x49b105 set and i not the current shape [0x49b62c]. H =
 * [0x49b088]; v = [0x49b08c + 4i]; [H+0x236] <- v; when H+0x232 is not
 * [H+0x226] the cursor set: word 0x4d2566 set: 0x48992e(([v+4] ?
 * [[H+0x236]+4] + 4 : [[H+0x236]+4]), [H+0x236] + 8); else 0x486e70 with
 * the same arguments read in the other order (both empty in this build);
 * [0x49b62c] <- i. */
static int cursor_shape(hud_ctx *c, uint32_t i) {
    if (!hr8(c, 0x0049b105u) || i == hr32(c, 0x0049b62cu)) return 0;
    uint32_t h = hr32(c, VA_PANEL), v = hr32(c, 0x0049b08cu + 4u * i);
    hw32(c, h + 0x236u, v);
    if (h + 0x232u != hr32(c, h + 0x226u)) {
        if (hr16(c, 0x004d2566u)) {
            uint32_t second = hr32(c, h + 0x236u) + 8u, first;
            if (hr32(c, v + 4u)) first = hr32(c, hr32(c, h + 0x236u) + 4u) + 4u;
            else first = hr32(c, hr32(c, h + 0x236u) + 4u);
            if (HCALL(c, 0x0048992eu, NULL, first, second)) return 1;
        } else {
            uint32_t e = hr32(c, h + 0x236u), first = hr32(c, e + 4u) ? hr32(c, e + 4u) + 4u : hr32(c, e + 4u);
            if (HCALL(c, 0x00486e70u, NULL, first, e + 8u)) return 1;
        }
    }
    hw32(c, 0x0049b62cu, i);
    return 0;
}

/* ---- the gauges */

/* 0x43b4cc(D) (listing 0x43b4cc..0x43b573), the fixed rotation gauge: the
 * canvas pushed; the scale (bitmap 11 of the table) at D+0x2e; the needle
 * x = ((word D+0x3a - word D+0x24) & 0xffff) * [D+0x28] idiv word D+0x2c
 * (unsigned) + [D+4], one more for a negative D+0x3a, plus 0xfffffff1 << the
 * x shift; y [D+8]; the needle bitmap 12 when the signed word D+0x3a is
 * >= 300 or <= -300, else 13; the canvas popped. */
int es2_sim_gadget_rotation_fixed(hud_ctx *c, uint32_t d) {
    if (canvas_push(c, NULL)) return 1;
    if (blit(c, hud_bitmap(c, 11u), d + 0x2eu, 0)) return 1;
    uint16_t si = hr16(c, d + 0x3au);
    uint32_t product = (((uint32_t)si - (uint32_t)hr16(c, d + 0x24u)) & 0xffffu) * hr32(c, d + 0x28u);
    int32_t x;
    if (idiv(c, 0x0043b4ccu, "rotation_fixed", (int32_t)product, (int32_t)(uint32_t)hr16(c, d + 0x2cu), &x, NULL)) return 1;
    x += s32(c, d + 4u);
    if ((int16_t)si < 0) ++x;
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 8u);
    if (!f) return frame_refused(c, 0x0043b4ccu, "rotation_fixed");
    hw32(c, f, 0xfffffff1u << shx(c));
    add32(c, f, (uint32_t)x);
    hw32(c, f + 4u, hr32(c, d + 8u));
    uint32_t idx = ((int16_t)si >= 300 || (int16_t)si <= -300) ? 12u : 13u;
    int bad = blit(c, hud_bitmap(c, idx), f, 0) || canvas_pop(c);
    hud_frame_pop(c, top);
    return bad;
}

/* 0x43b67c(D, v) (listing 0x43b67c..0x43b701), the reverse gauge's step:
 * p = 0x47e08d(word v, [D+0x40]); q, r = p idiv [D+0x28]; r >= [D+0x28]
 * (signed): r -= [D+0x28], q + 1 (0 at the count [[0x49be70]+4]); n = q +
 * 1 (0 at the count); D+0x44 <- [D+4] - r, D+0x4c <- table[n], D+0x48 <-
 * table[q] (table [[0x49be70]+8]). EAX the table. */
static int rotation_step(hud_ctx *c, uint32_t d, uint32_t v, uint32_t *eax) {
    uint32_t p = 0;
    if (HCALL(c, VA_MUL16, &p, v & 0xffffu, hr32(c, d + 0x40u))) return 1;
    int32_t step = s32(c, d + 0x28u), q, r;
    if (idiv(c, 0x0043b67cu, "rotation_step", (int32_t)p, step, &q, &r)) return 1;
    if (!(step > r)) {
        r -= step;
        ++q;
        if ((uint32_t)q == hr32(c, hr32(c, VA_STEPS) + 4u)) q = 0;
    }
    uint32_t n = (uint32_t)q + 1u;
    if (n == hr32(c, hr32(c, VA_STEPS) + 4u)) n = 0;
    hw32(c, d + 0x44u, hr32(c, d + 4u) - (uint32_t)r);
    hw32(c, d + 0x4cu, hr32(c, hr32(c, hr32(c, VA_STEPS) + 8u) + 4u * n));
    uint32_t table = hr32(c, hr32(c, VA_STEPS) + 8u);
    hw32(c, d + 0x48u, hr32(c, table + 4u * (uint32_t)q));
    *eax = table;
    return 0;
}

/* 0x43c40c(D) (listing 0x43c40c..0x43c7fc), the nav gauge. Frame 0x6c;
 * P = [[0x49b088]+0x203]. The point D+0x3c: with byte D+0x45 the marker
 * 0x43495c (none: return without the canvas) and the colour word
 * 0x4d3b9a; else, with a route [P+0x45]: when [0x4d2a6c] the target
 * [P+0x1a4] (+0x26), else route_point 0x423b0c(route+4, word [route+4] +
 * 1) when [route+6] is not 0, the waypoint number word -2 <- word [route+4]
 * + 1 below the count word [[route+6]], else -1 (no point); then the colour
 * word 0x4d3b7c. No point: return. D+0x48 <- distance_display 0x47d694(D+0x3c,
 * P+0x26 copy); D+0x3a <- vector_angle 0x49290c(D+0x3c - P+0x26) + 0xc000;
 * the heading difference word [P+0x10] - D+0x3a: 0xe38 < d < 0x8000 sets
 * byte D+0x4d (right arrow), 0x8000 <= d < 0xf1c8 byte D+0x4c (left
 * arrow). The canvas pushed; four points (a vector 0x49667c) at -0x50; the
 * fill record {0, colour} at -0xc put into the canvas GC+0x22c; the left
 * or right arrow (three points) or the centre diamond (four points, the
 * offset d * [D+0x28] idiv word D+0x2c, or minus (0x10000 - d) * ...)
 * filled (0x483eb8). Without byte D+0x45 the text: D+0x48 <- radar_units
 * 0x434228(D+0x48); the string [0x4d1654] copied to -0x6c, the waypoint
 * number, 0x49c0f9, the distance and 0x49c0fc appended (itoa 0x496820,
 * strcat 0x4967ea), the label 0x438920([D+0x4e], text). The canvas
 * popped. */
int es2_sim_gadget_nav_gauge(hud_ctx *c, uint32_t d) {
    uint32_t p = hr32(c, hr32(c, VA_PANEL) + 0x203u);
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0x6cu);
    if (!f) return frame_refused(c, 0x0043c40cu, "nav_gauge");
#define L(k) (f + 0x6cu - (k))
    int bad = 1, have = 0;
    hw16(c, L(2), 0);
    hw32(c, L(0xc), 0);
    if (hr8(c, d + 0x45u)) {
        uint32_t e = 0;
        if (HCALL0(c, VA_MARKER_POINT, &e)) goto done;
        if (!e) { bad = 0; goto done; }
        copy8(c, d + 0x3cu, e);
        have = 1;
        hw16(c, d + 0x46u, hr16(c, 0x004d3b9au));
    } else {
        uint32_t route = hr32(c, p + 0x45u);
        if (route) {
            if (hr32(c, 0x004d2a6cu)) {
                have = hr32(c, p + 0x1a4u) != 0;
                if (have) copy8(c, d + 0x3cu, hr32(c, p + 0x1a4u) + 0x26u);
            } else {
                uint32_t pt = 0, w = route + 4u;
                if (w && hr32(c, w + 2u)) {
                    if (HCALL(c, VA_ROUTE_POINT, &pt, w, (uint32_t)hr16(c, w) + 1u)) goto done;
                    uint16_t n = hr16(c, w);
                    hw16(c, L(2), (int16_t)n < (int16_t)hr16(c, hr32(c, w + 2u)) ? (uint16_t)(n + 1u) : 0xffffu);
                }
                if (pt && hr16(c, L(2)) != 0xffffu) {
                    copy8(c, d + 0x3cu, pt);
                    have = 1;
                }
            }
            hw16(c, d + 0x46u, hr16(c, 0x004d3b7cu));
        }
    }
    if (!have) { bad = 0; goto done; }
    hw32(c, L(0x1c), hr32(c, p + 0x26u));
    hw32(c, L(0x18), hr32(c, p + 0x2au));
    uint32_t v = 0;
    if (HCALL(c, VA_DISTANCE_DISPLAY, &v, d + 0x3cu, L(0x1c))) goto done;
    hw32(c, d + 0x48u, v);
    uint32_t dy = hr32(c, d + 0x40u) - hr32(c, L(0x18)), dx = hr32(c, d + 0x3cu) - hr32(c, L(0x1c));
    if (HCALL(c, VA_VECTOR_ANGLE, &v, dx, dy)) goto done;
    uint16_t angle = (uint16_t)(v + 0xc000u);
    hw16(c, d + 0x3au, angle);
    uint16_t heading = (uint16_t)(hr16(c, p + 0x10u) - angle);
    hw8(c, d + 0x4du, 0);
    hw8(c, d + 0x4cu, 0);
    if (heading > 0x0e38u && heading < 0x8000u) hw8(c, d + 0x4du, 1);
    else if (heading < 0xf1c8u && heading >= 0x8000u) hw8(c, d + 0x4cu, 1);
    hw32(c, L(0x20), hr32(c, VA_VIEW));
    uint32_t gc = 0;
    if (canvas_push(c, &gc)) goto done;
    if (HCALL(c, HUD_VA_VECTOR_NEW, NULL, L(0x50), 8u, 4u, 1u, HUD_VA_EMPTY_CTOR)) goto done;
    hw32(c, L(0x24), L(0x50));
    hw32(c, gc + 0x22cu, L(0xc));
    hw32(c, L(8), hsx16(hr16(c, d + 0x46u)));
    if (hr8(c, d + 0x4cu) || hr8(c, d + 0x4du)) {
        uint32_t half = sar1(hr32(c, 0x004d1814u)), y = hr32(c, d + 8u) + hr32(c, 0x004d1824u), x;
        if (hr8(c, d + 0x4cu)) {
            x = hr32(c, d + 4u) - hr32(c, 0x004d1820u);
            hw32(c, L(0x50), x - hr32(c, 0x004d1810u));
            hw32(c, L(0x4c), y);
            hw32(c, L(0x48), x);
            hw32(c, L(0x44), y - half);
            hw32(c, L(0x40), x);
            hw32(c, L(0x3c), y + half);
        } else {
            x = hr32(c, d + 0xcu) + hr32(c, 0x004d1820u);
            hw32(c, L(0x50), x + hr32(c, 0x004d1810u));
            hw32(c, L(0x4c), y);
            hw32(c, L(0x48), x);
            hw32(c, L(0x44), y + half);
            hw32(c, L(0x40), x);
            hw32(c, L(0x3c), y - half);
        }
        if (polygon(c, VA_POLY_FILL, 3u, L(0x50))) goto done;
    } else {
        uint32_t x = sar1(hr32(c, d + 0xcu) - hr32(c, d + 4u)) + hr32(c, d + 4u);
        int32_t q;
        if (heading < 0x8000u) {
            if (idiv(c, 0x0043c40cu, "nav_gauge", (int32_t)((uint32_t)heading * hr32(c, d + 0x28u)), (int32_t)(uint32_t)hr16(c, d + 0x2cu), &q, NULL))
                goto done;
            x += (uint32_t)q;
        } else {
            if (idiv(c, 0x0043c40cu, "nav_gauge", (int32_t)((0x10000u - heading) * hr32(c, d + 0x28u)), (int32_t)(uint32_t)hr16(c, d + 0x2cu), &q,
                     NULL))
                goto done;
            x -= (uint32_t)q;
        }
        uint32_t a = hr32(c, 0x004d1818u), b = sar1(hr32(c, 0x004d180cu)), w = sar1(hr32(c, 0x004d1808u)), k = hr32(c, 0x004d181cu),
                 y = hr32(c, d + 8u);
        hw32(c, L(0x50), a + x);
        hw32(c, L(0x4c), b + y + k);
        hw32(c, L(0x48), x - w + a);
        hw32(c, L(0x44), y + k);
        hw32(c, L(0x40), a + x);
        hw32(c, L(0x3c), y - b + k);
        hw32(c, L(0x38), x + w + a);
        hw32(c, L(0x34), y + k);
        if (polygon(c, VA_POLY_FILL, 4u, L(0x50))) goto done;
    }
    if (!hr8(c, d + 0x45u)) {
        uint32_t units = 0;
        if (HCALL(c, VA_RADAR_UNITS, &units, hr32(c, d + 0x48u))) goto done;
        hw32(c, d + 0x48u, units);
        hud_gstrcpy(c, L(0x6c), hr32(c, 0x004d1654u));
        if (HCALL(c, HUD_VA_ITOA, NULL, hsx16(hr16(c, L(2))), L(0x30), 10u) || HCALL(c, HUD_VA_STRCAT, NULL, L(0x6c), L(0x30)) ||
            HCALL(c, HUD_VA_STRCAT, NULL, L(0x6c), 0x0049c0f9u) || HCALL(c, HUD_VA_ITOA, NULL, hr32(c, d + 0x48u), L(0x30), 10u) ||
            HCALL(c, HUD_VA_STRCAT, NULL, L(0x6c), L(0x30)) || HCALL(c, HUD_VA_STRCAT, NULL, L(0x6c), 0x0049c0fcu) ||
            HCALL(c, VA_LABEL, NULL, hr32(c, d + 0x4eu), L(0x6c)))
            goto done;
    }
    bad = canvas_pop(c);
done:
#undef L
    hud_frame_pop(c, top);
    return bad;
}

/* ---- service rows */
/* 0x433d90(i). */
void es2_sim_service_cursor_shape(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (cursor_shape(&c, sc->slots[0])) hud_failed(sc);
}
/* 0x486ece(p) (listing 0x486ece..0x486ee5): the 8 bytes of the mouse
 * point 0x4a3620 copied to p; EAX p. */
void es2_sim_service_mouse_position(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    copy8(&c, sc->slots[0], 0x004a3620u);
    sc->eax = sc->slots[0];
}
/* 0x43495c() (listing 0x43495c..0x434973): [0x49b088]+0x25e when byte
 * +0x26a of it is set, else 0. */
void es2_sim_service_nav_marker_point(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t h = hr32(&c, VA_PANEL);
    sc->eax = hr8(&c, h + 0x26au) ? h + 0x25eu : 0;
}
void es2_sim_service_rotation_step(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t eax = 0;
    if (rotation_step(&c, sc->slots[0], sc->slots[1], &eax)) hud_failed(sc);
    else sc->eax = eax;
}
/* 0x43b7cc(D) (listing 0x43b7cc..0x43b7d9): the draw, slot 0 of [D]; EAX
 * its EAX. */
void es2_sim_service_gadget_redraw(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t d = sc->slots[0], eax = 0;
    if (hud_call(&c, hr32(&c, hr32(&c, d)), (const uint32_t[]){d}, 1, &eax)) hud_failed(sc);
    else sc->eax = eax;
}
