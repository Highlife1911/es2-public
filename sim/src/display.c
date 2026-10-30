/* The display transitions (the slides between the cockpit display modes) and
 * the shield palette, as own code. Contract: display_modes.md, "the display
 * transitions and the shield palette as own code"; every function names its
 * listing range of the reference DBSIM.EXE (llvm-objdump -d -M intel, SHA-256
 * 8561aa47...c4ad). The scroll 0x487e60 (gfx.c's row), the present 0x464a14
 * (present.c's es2_sim_present_frame with the original call's return address as
 * the site), the palette writes 0x4303c4 (hud_load.h's row) and every other callee stay
 * calls at the original's sites and in its order; the original's stack locals
 * (the scroll region, the points, the origin temporaries, the palette array)
 * live in a frame of the call area. The retrace wait 0x45c638 is a documented
 * platform deviation (no port I/O). Refused (the original reads undefined stack
 * data or a null pointer there): a transition request above 6 and a view
 * without a graphics context. */
#include "hud_internal.h"
#include "es2/sim/present.h"

#define VA_VIEW 0x006c5ed0u        /* the current view surface V */
#define VA_HUD 0x0049b088u         /* the cockpit root H */
#define VA_PAGED 0x004d2566u       /* word: the present path (nonzero) or the page swap */
#define VA_SPLIT_DIRTY 0x004d267eu /* word */
#define VA_KIND 0x004d2537u        /* signed byte: 1 the 400-line kind */
#define VA_RES 0x004d2538u         /* signed byte: 2 = 640x480 */
#define VA_GAME_W 0x004d253eu
#define VA_GAME_H 0x004d2542u
#define VA_PAGE_X 0x004d3018u      /* the page origin S */
#define VA_PAGE_Y 0x004d301cu
#define VA_SLIDING 0x004a3646u     /* dword: 0 while a slide runs */
#define VA_PANEL_UP 0x0049dbdfu    /* byte */
#define VA_SOUND_ON 0x0049f8f8u    /* byte */
#define VA_SAVED_LEVEL 0x004cfcb4u
#define VA_JOLT_DONE 0x0049ac45u   /* byte */
#define VA_CAMERA 0x004d24eau
#define VA_DRV_ORIGIN 0x004a57e8u  /* driver slots of the current table copy */
#define VA_DRV_DRAW 0x004a57ecu
#define VA_DRV_SHOW 0x004a57f0u
#define VA_DRV_VIEW 0x004a57f4u
#define VA_DRV_LOCK 0x004a5828u
#define VA_DRV_UNLOCK 0x004a5824u
#define VA_CUR_VIEW 0x004a3682u
#define VA_CUR_DRAW 0x004a3686u
#define VA_CUR_SHOW 0x004a368au
#define VA_SCROLL 0x00487e60u      /* gfx.c's row */
#define VA_PALETTE_ENTRIES 0x004303c4u
#define VA_Q10 0x0047e0b0u         /* the 10-bit fixed-point multiply (a hook) */
#define VA_PORT_A 0x00433158u
#define VA_DISPLAY_SET 0x0042a1f0u
#define VA_DISPLAY_SWITCH 0x00429e60u
#define VA_PANEL_SHOW 0x00431518u
#define VA_VIEW_BORDER 0x0042da08u
#define VA_BORDER_OFF 0x0042d9e8u
#define VA_RETRACE 0x0045c638u
#define VA_CAMERA_GET 0x0045edf8u
#define VA_BUTTONS_DOWN 0x00434604u
#define VA_BUTTONS_UP 0x00434634u
#define VA_BUTTON_DOWN 0x00434928u
#define VA_BUTTON_UP 0x00434944u
#define VA_LEVEL 0x00462fe0u
#define VA_SET_LEVEL 0x00463014u
#define VA_SLIDE_SOUND 0x0045b68cu /* the flight-key capture switch (row flight_capture) */
#define VA_PORT_REDRAW 0x00437164u /* open (no row) */
#define VA_SLIDE_UP_DONE 0x00433f7cu
#define VA_SLIDE_DONE 0x00433ee8u  /* the cockpit done step (row cockpit_done) */
#define VA_JOLT 0x0042d2f8u

static int res2(const hud_ctx *c) { return (int8_t)hr8(c, VA_RES) == 2; }
static int kind1(const hud_ctx *c) { return (int8_t)hr8(c, VA_KIND) == 1; }
static int paged(const hud_ctx *c) { return hr16(c, VA_PAGED) != 0; }
static int32_t s16(uint16_t v) { return (int32_t)(int16_t)v; }

/* A page attribute of V: V+off <- x; V current (read again): the global
 * copy <- V+off (read again) and the driver slot called with x
 * (0x34: [0x4a3682], slot 0x4a57f4; 0x38: [0x4a3686], 0x4a57ec; 0x3c:
 * [0x4a368a], 0x4a57f0). */
static int page_attr(hud_ctx *c, uint32_t v, uint32_t off, uint32_t x) {
    uint32_t global = off == 0x34u ? VA_CUR_VIEW : off == 0x38u ? VA_CUR_DRAW : VA_CUR_SHOW;
    uint32_t slot = off == 0x34u ? VA_DRV_VIEW : off == 0x38u ? VA_DRV_DRAW : VA_DRV_SHOW;
    hw32(c, v + off, x);
    if (v != hr32(c, VA_VIEW)) return 0;
    hw32(c, global, hr32(c, v + off));
    return hud_call(c, hr32(c, slot), &x, 1, NULL);
}
static int driver(hud_ctx *c, uint32_t slot) { return hud_call(c, hr32(c, slot), NULL, 0, NULL); }
/* The page origin: tmp <- V's origin of page p plus (x, y);
 * [0x4a57e8](p, &tmp). */
static int origin(hud_ctx *c, uint32_t v, uint32_t tmp, uint32_t p, uint32_t x, uint32_t y) {
    uint32_t o = v + 0x48u + 8u * p;
    hw32(c, tmp, hr32(c, o));
    hw32(c, tmp + 4u, hr32(c, o + 4u));
    hw32(c, tmp, hr32(c, tmp) + x);
    hw32(c, tmp + 4u, hr32(c, tmp + 4u) + y);
    return HCALL(c, hr32(c, VA_DRV_ORIGIN), NULL, p, tmp);
}
/* The present 0x464a14 when word [0x4d2566] is set (read at each site);
 * `site` is the return address of the original's call. */
static int present(hud_ctx *c, uint32_t site) {
    if (!paged(c)) return 0;
    return es2_sim_present_frame(es2_sim_present_bound(), site);
}
static int port_call(hud_ctx *c, uint32_t port, uint32_t off) { return HVCALL(c, port, off, NULL, port); }

/* ======================================================================
 * 0x443918(G) (listing 0x443918..0x444a8d), the shield palette: the
 * channel deltas d2 = [0x49c9d0] - [0x49c9cd], d1 = [0x49c9cf] -
 * [0x49c9cc], d0 = [0x49c9ce] - [0x49c9cb] (bytes, 16-bit difference,
 * sign-extended); for v = [G+0xb5], then v = [G+0xb9]: three levels t1,
 * t2, t3 from e = v - 0x400 (signed v > 0x400) or e = v: t1 = e, t2 = 0x400
 * above 0x300, 0 below 0x100, else (e - 0x100) * 2; t3 = 0x400 above
 * 0x280, 0 below 0x180, else (e - 0x180) * 4. Per level the channels 2, 1,
 * 0: high form q = q10(t, d) + A (A the byte at 0x49c9cd/cc/cb, read at
 * each use), channel = q > 0 ? (uint8)(q10(t, d) + A) (the multiply
 * called again) : 0; low form q = q10(t, A), channel = q > 0 ? (uint8)
 * q10(t, A) : 0. Entry = 0x01000000 | c2 << 16 | c1 << 8 | c0, in the
 * array [t3, t2, t1] for +0xb5 and [t3, t2, t1] for +0xb9 behind it;
 * then 0x4303c4(0x42, 6, array). EAX 0x4303c4's. */
static int q10(hud_ctx *c, uint32_t t, uint32_t d, uint32_t *r) { return HCALL(c, VA_Q10, r, t, d); }
static int shield_entry(hud_ctx *c, uint32_t t, int high, const uint32_t d[3], uint32_t *entry) {
    static const uint32_t base[3] = {0x0049c9cbu, 0x0049c9ccu, 0x0049c9cdu};
    uint32_t ch[3] = {0, 0, 0};
    for (int k = 2; k >= 0; --k) {
        uint32_t r = 0;
        if (high) {
            if (q10(c, t, d[k], &r)) return 1;
            if ((int32_t)(r + hr8(c, base[k])) > 0) {
                if (q10(c, t, d[k], &r)) return 1;
                ch[k] = (uint8_t)(r + hr8(c, base[k]));
            }
        } else {
            if (q10(c, t, hr8(c, base[k]), &r)) return 1;
            if ((int32_t)r > 0) {
                if (q10(c, t, hr8(c, base[k]), &r)) return 1;
                ch[k] = (uint8_t)r;
            }
        }
    }
    *entry = 0x01000000u | ch[2] << 16 | ch[1] << 8 | ch[0];
    return 0;
}
static int shield_palette(hud_ctx *c, uint32_t g, uint32_t *eax) {
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0x18u);
    if (!f) return hud_refuse(c, 0x00443918u, "shield palette", "the own code's call area is exhausted");
    uint32_t d[3];
    d[1] = hsx16((uint16_t)(hr8(c, 0x0049c9cfu) - hr8(c, 0x0049c9ccu)));
    d[0] = hsx16((uint16_t)(hr8(c, 0x0049c9ceu) - hr8(c, 0x0049c9cbu)));
    d[2] = hsx16((uint16_t)(hr8(c, 0x0049c9d0u) - hr8(c, 0x0049c9cdu)));
    for (uint32_t half = 0; half < 2u; ++half) {
        int32_t v = (int32_t)hr32(c, g + (half ? 0xb9u : 0xb5u));
        int high = v > 0x400;
        int32_t e = high ? (int32_t)((uint32_t)v - 0x400u) : v;
        int32_t t2 = e > 0x300 ? 0x400 : e < 0x100 ? 0 : (int32_t)(((uint32_t)e - 0x100u) * 2u);
        int32_t t3 = e > 0x280 ? 0x400 : e < 0x180 ? 0 : (int32_t)(((uint32_t)e - 0x180u) * 4u);
        const int32_t t[3] = {e, t2, t3};
        for (uint32_t i = 0; i < 3u; ++i) {
            uint32_t entry;
            if (shield_entry(c, (uint32_t)t[i], high, d, &entry)) return 1;
            hw32(c, f + 4u * (3u * half + 2u - i), entry);
        }
    }
    if (HCALL(c, VA_PALETTE_ENTRIES, eax, 0x42u, 6u, f)) return 1;
    hud_frame_pop(c, top);
    return 0;
}

/* ======================================================================
 * The small callees. */
/* 0x45edf8 (the camera, EAX [0x4d24ea]) is the hook camera_current
 * (core/src/camera_view.c); it is called, not reconstructed here. */
/* 0x42d9e8(D) (listing 0x42d9e8..0x42da04): mode [D+0x14] 4 ->
 * 0x42da08(D); byte D+0x36 <- 0. EAX the callee's, else mode - 4. */
static int border_off(hud_ctx *c, uint32_t d, uint32_t *eax) {
    uint32_t r = hr32(c, d + 0x14u) - 4u;
    if (!r && HCALL(c, VA_VIEW_BORDER, &r, d)) return 1;
    hw8(c, d + 0x36u, 0);
    *eax = r;
    return 0;
}
/* 0x434928(b) (listing 0x434928..0x434943): dword b+0x5c <- sign-extended
 * word b+0x1b; word b+0x1b <- 3; [[b+0x17]](b). 0x434944(b) (listing
 * 0x434944..0x43495a): word b+0x1b <- word b+0x5c; [[b+0x17]](b). EAX the
 * call's. */
static int button(hud_ctx *c, uint32_t b, int down, uint32_t *eax) {
    if (down) {
        hw32(c, b + 0x5cu, hsx16(hr16(c, b + 0x1bu)));
        hw16(c, b + 0x1bu, 3);
    } else hw16(c, b + 0x1bu, hr16(c, b + 0x5cu));
    return HVCALL(c, b + 0x17u, 0, eax, b);
}
/* 0x434604(H) (listing 0x434604..0x434630) and 0x434634(H) (listing
 * 0x434634..0x434660): for i below the signed count [H+0x25a] (read each
 * pass) 0x434928 / 0x434944 of the button [H+0x246+4i]. EAX the last
 * callee's (unchanged without one). */
static int buttons(hud_ctx *c, uint32_t h, int down, uint32_t *eax) {
    for (uint32_t i = 0; (int32_t)i < (int32_t)hr32(c, h + 0x25au); ++i)
        if (HCALL(c, down ? VA_BUTTON_DOWN : VA_BUTTON_UP, eax, hr32(c, h + 0x246u + 4u * i))) return 1;
    return 0;
}

/* ======================================================================
 * 0x42a4c4(D) (listing 0x42a4c4..0x42a9be), the display's leave half:
 * byte D+0x36 -> 0x42d9e8(D); a cool-down word D+0x1d counts down and
 * returns. With word [0x4d2566] (the present path), the requests 4 (S.x
 * set), 5 (S.x clear) and 6 (mode 2 and S.x clear, or mode 3 and S.x set)
 * scroll the game area by -w / w (counter + 3; the GC's +0x208/+0x20c
 * saved and cleared; region type 0, (0, 0, w - 1, h - 1); 0x487e60(region,
 * pt); pt.x += S.x; [0x4a57e8](0, V's page-0 origin + pt); the GC fields
 * restored; counter - 3). Without it the page shown [V+0x88] must already
 * be the target page (n = 1 for kind 1, else 2: request 4 page 0, 5 page
 * n, 6 mode 3 page 0, 6 mode 2 page n, at 640x480 also 0 page 0, 1 page
 * n), else it returns; then the retrace wait 0x45c638. Word [0x4d267e] <-
 * 0. Not yet started (byte D+0x1c clear): 0x433158(); the request starts
 * the mode change (0: next mode, 1: previous mode, through 0x42a1f0 with
 * the mode kept at 640x480 or a scrolled-out page and else 0x429e60; 2:
 * 0x429e60(D, 4); 3: 0x42a1f0(D, [D+0x23]); 4/5: 0x42a1f0(D, 2 / 3) and
 * for kind 1 the camera's word +0x48 turned by 0xca00 / 0x3600; 6:
 * 0x42a1f0(D, 0), 0x431518([D+0x1f]) and for kind 1 two camera turns),
 * byte D+0x1c <- 1. Started: 0x434604([0x49b088]). EAX the FS:0 head the
 * frame record saved, on every path. */
static int turn_camera(hud_ctx *c, uint16_t by) {
    uint32_t cam;
    if (HCALL0(c, VA_CAMERA_GET, &cam)) return 1;
    if (!cam || hr32(c, cam + 0x4au)) return 0;
    hw16(c, cam + 0x48u, (uint16_t)(hr16(c, cam + 0x48u) + by));
    hw16(c, cam + 0x44u, hr16(c, cam + 0x44u));
    hw16(c, cam + 0x46u, hr16(c, cam + 0x46u));
    return 0;
}
static int set_keep(hud_ctx *c, uint32_t d, uint32_t mode) {
    uint32_t old = hr32(c, d + 0x14u);
    if (HCALL(c, VA_DISPLAY_SET, NULL, d, mode)) return 1;
    hw32(c, d + 0x14u, old);
    return 0;
}
static int leave(hud_ctx *c, uint32_t d) {
    if (hr8(c, d + 0x36u) && HCALL(c, VA_BORDER_OFF, NULL, d)) return 1;
    uint16_t cool = hr16(c, d + 0x1du);
    if (cool) {
        hw16(c, d + 0x1du, (uint16_t)(cool - 1u));
        return 0;
    }
    if (paged(c)) {
        uint32_t sx = hr32(c, VA_PAGE_X), h = hr32(c, VA_GAME_H), w = hr32(c, VA_GAME_W), v = hr32(c, VA_VIEW), dx = 0;
        int go = 0;
        switch (hr32(c, d + 0x18u)) {
        case 4: if (sx) { dx = 0u - w; go = 1; } break;
        case 5: if (!sx) { dx = w; go = 1; } break;
        case 6:
            if (hr32(c, d + 0x14u) == 2u && !sx) { dx = w; go = 1; }
            else if (hr32(c, d + 0x14u) == 3u && sx) { dx = 0u - w; go = 1; }
            break;
        default: break;
        }
        if (go) {
            hud_counter(c, 3);
            uint32_t gc = hr32(c, v + 0x44u);
            if (!gc) return hud_null_refuse(c, 0x0042a5e7u, "display leave", "the view has no graphics context");
            gc -= 4u;
            uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0x24u);
            if (!f) return hud_refuse(c, 0x0042a4c4u, "display leave", "the own code's call area is exhausted");
            uint32_t region = f, pt = f + 0x14u, tmp = f + 0x1cu;
            uint32_t save208 = hr32(c, gc + 0x208u);
            hw32(c, gc + 0x208u, 0);
            uint32_t save20c = hr32(c, gc + 0x20cu);
            hw32(c, gc + 0x20cu, 0);
            hw32(c, region + 0xcu, w - 1u);
            hw32(c, region + 0x10u, h - 1u);
            hw32(c, pt, dx);
            if (HCALL(c, VA_SCROLL, NULL, region, pt)) return 1;
            hw32(c, pt, hr32(c, pt) + sx);
            if (origin(c, v, tmp, 0, hr32(c, pt), hr32(c, pt + 4u))) return 1;
            hw32(c, gc + 0x208u, save208);
            hw32(c, gc + 0x20cu, save20c);
            hud_counter(c, -3);
            hud_frame_pop(c, top);
        }
    } else {
        uint32_t n = kind1(c) ? 1u : 2u, p = hr32(c, hr32(c, VA_VIEW) + 0x88u), req = hr32(c, d + 0x18u);
        if (req == 4u && p) return 0;
        if (req == 5u && p != n) return 0;
        if (req == 6u && hr32(c, d + 0x14u) == 3u && p) return 0;
        if (req == 6u && hr32(c, d + 0x14u) == 2u && p != n) return 0;
        if (res2(c) && ((req == 0u && p) || (req == 1u && p != n))) return 0;
    }
    if (!paged(c) && HCALL0(c, VA_RETRACE, NULL)) return 1;
    hw16(c, VA_SPLIT_DIRTY, 0);
    if (!hr8(c, d + 0x1cu)) {
        if (HCALL0(c, VA_PORT_A, NULL)) return 1;
        switch (hr32(c, d + 0x18u)) {
        case 0:
            hw8(c, d + 0x1cu, 1);
            if (res2(c) || hr32(c, VA_PAGE_X) == hr32(c, VA_GAME_W)) {
                if (set_keep(c, d, hr32(c, d + 0x14u) + 1u)) return 1;
            } else if (HCALL(c, VA_DISPLAY_SWITCH, NULL, d, hr32(c, d + 0x14u) + 1u)) return 1;
            break;
        case 1:
            hw8(c, d + 0x1cu, 1);
            if (res2(c)) {
                if (set_keep(c, d, hr32(c, d + 0x14u) - 1u)) return 1;
            } else if (HCALL(c, VA_DISPLAY_SWITCH, NULL, d, hr32(c, d + 0x14u) - 1u)) return 1;
            break;
        case 2:
            if (HCALL(c, VA_DISPLAY_SWITCH, NULL, d, 4u)) return 1;
            hw8(c, d + 0x1cu, 1);
            break;
        case 3:
            if (set_keep(c, d, hr32(c, d + 0x23u))) return 1;
            hw8(c, d + 0x1cu, 1);
            break;
        case 4:
        case 5: {
            int four = hr32(c, d + 0x18u) == 4u;
            if (set_keep(c, d, four ? 2u : 3u)) return 1;
            if (kind1(c) && turn_camera(c, four ? 0xca00u : 0x3600u)) return 1;
            hw8(c, d + 0x1cu, 1);
            break;
        }
        case 6:
            if (set_keep(c, d, 0) || HCALL(c, VA_PANEL_SHOW, NULL, hr32(c, d + 0x1fu))) return 1;
            if (kind1(c)) {
                uint32_t cam;
                if (HCALL0(c, VA_CAMERA_GET, &cam)) return 1;
                if (cam) {
                    uint16_t by = hr32(c, d + 0x14u) == 2u ? 0x3600u : 0xca00u;
                    if (!hr32(c, cam + 0x4au)) {
                        hw16(c, cam + 0x48u, (uint16_t)(hr16(c, cam + 0x48u) + by));
                        hw16(c, cam + 0x44u, hr16(c, cam + 0x44u));
                        hw16(c, cam + 0x46u, hr16(c, cam + 0x46u));
                    }
                }
            }
            if (kind1(c) && turn_camera(c, 0xca00u)) return 1;
            hw8(c, d + 0x1cu, 1);
            break;
        default: break;
        }
    }
    if (hr8(c, d + 0x1cu) && HCALL(c, VA_BUTTONS_DOWN, NULL, hr32(c, VA_HUD))) return 1;
    return 0;
}

/* ======================================================================
 * 0x42a9c0(D) (listing 0x42a9c0..0x42d2b7), the display's enter half: the
 * slide of a started request (none with [D+0x18] = -1 or byte D+0x1c
 * clear: EAX [D+0x14]). Counter + 3; the region type 0; V = [0x6c5ed0],
 * its GC (V+0x44 less 4), the pages p50 = [V+0x88] (shown) and p54 =
 * [V+0x8c] (drawn); the GC's +0x208/+0x20c saved and cleared; word
 * [0x4d267e] <- 0.
 * First part, per request (R = [D+4], 32-byte records; m = [D+0x14]):
 * 0 (none at m = 1: counter - 3, EAX m, the GC fields left cleared) the
 * delta R[m+1] - R[m] (+0x18/+0x1c), the area (-dx, 0xf0 - dy, 0x140,
 * 0xf0), step 10; 1 (none at m = 0) the delta R[m-1] - R[m], the area (0,
 * 0, 0x140 - dx, -dy), step 10 (both: no scroll at 640x480 or on the
 * present path); 2 [0x4a5828](), byte [0x49dbdf] <- 0; 3 the whole area
 * (0, 0, 0x140, 0x1e0 / 0xf0 for kind 1 / else) with a zero delta unless
 * on the present path, byte [0x49dbdf] <- 1; 4 the delta R[0] - R[2]; 5
 * R[0] + R[3]; 6 R[0] + R[m] (4..6 step 0x14, no scroll); every request
 * but 2 calls the cockpit port's slot +0x18 ([[0x49b088]+0x20b]). The
 * scroll: draw page p54, show page p50, [0x4a5828](), 0x487e60(region,
 * delta), [0x4a5824](), show page p54. The GC fields restored; the base
 * (bx, by) = S on the present path, else (0, 0); byte D+0x1c <- 0;
 * port = 0x433158().
 * Second part: 0/1 slide the view down / up by the delta's y (the whole
 * 0x1e0 rows in steps of 2 * step at 640x480), 4/5/6 sideways by the
 * delta's x, each step a new page origin [0x4a57e8](p50, origin + base),
 * with the port's slots +0x14/+0x18 around it when byte port+0x49e is
 * set (then the show page p54 and 0x437164(port, 0) after the last), and
 * a present per step on the present path (the sites are the original's
 * return addresses). At 640x480 (0/1 after the slide, 4/5/6 instead of
 * it) and for 4/5/6 without the present path the page flip: view page
 * [V+0x8c], show page [V+0x88], the two swapped, (for the slides) the
 * origin of p50, the whole area scrolled with GC+0x208 cleared around it.
 * Ends: 0 mode + 1, 0x45b68c(0); 1 mode - 1, 0x45b68c(1); 2 mode 4, byte
 * D+0x36 <- 1, [0x4cfcb4] <- 0x462fe0(0x2d) and with sound
 * 0x463014(0x2d, 0x64); 3 a present, [0x4a5824](), the port's +0x18, mode
 * [D+0x23], with sound 0x463014(0x2d, [0x4cfcb4]); 4/5/6 modes 2/3/0.
 * [0x4a3646] is 0 during the slides. Tail: [D+0x18] <- -1, word D+0x1d
 * <- 2, byte [0x49ac45] -> 0x42d2f8(10), 0x434634([0x49b088]); EAX
 * [D+0x14]; counter - 3. */
typedef struct slide {
    uint32_t d, v, gc, port, p50, p54, save208, region, delta, pt, tmp;
    uint32_t bx, by;
    uint16_t step;
} slide;

/* The page flip of 640x480 and the sideways slides (with_origin: the
 * slides' copy, which sets the origin of p50 first). */
static int flip(hud_ctx *c, slide *s, int with_origin) {
    uint32_t v = s->v;
    if (page_attr(c, v, 0x34u, hr32(c, v + 0x8cu)) || page_attr(c, v, 0x3cu, hr32(c, v + 0x88u))) return 1;
    uint32_t shown = hr32(c, v + 0x88u);
    hw32(c, v + 0x88u, hr32(c, v + 0x8cu));
    hw32(c, v + 0x8cu, shown);
    hw32(c, s->pt, 0);
    hw32(c, s->pt + 4u, 0);
    if (with_origin && origin(c, v, s->tmp, s->p50, hr32(c, s->pt), hr32(c, s->pt + 4u))) return 1;
    hw32(c, s->region + 4u, 0);
    hw32(c, s->region + 8u, 0);
    hw32(c, s->region + 0xcu, 0x140u);
    hw32(c, s->region + 0x10u, kind1(c) ? 0x1e0u : 0xf0u);
    if (page_attr(c, v, 0x38u, s->p54) || page_attr(c, v, 0x3cu, s->p50) || driver(c, VA_DRV_LOCK)) return 1;
    hw32(c, s->gc + 0x208u, 0);
    if (HCALL(c, VA_SCROLL, NULL, s->region, s->pt)) return 1;
    hw32(c, s->gc + 0x208u, s->save208);
    return driver(c, VA_DRV_UNLOCK);
}

/* The slide loops. kind: 0 down, 1 up, 4 right, 5 left, 6 back (the
 * request); i runs from `start` while the condition holds. */
typedef struct run {
    int kind;
    int32_t a2, start, end, s324, s328;
    uint32_t sites[4];   /* port loop, port end, plain loop, plain end */
} run;
static int run_more(const slide *s, const run *r, int32_t i) {
    (void)s;
    if (r->kind == 1) return i >= r->end;
    if (r->kind == 6) return i > r->end;
    return i < r->a2;
}
static void run_step(slide *s, const run *r, int32_t *i) {
    uint32_t st = (uint32_t)s16(s->step);
    switch (r->kind) {
    case 1: s->by -= st; *i = (int32_t)((uint32_t)*i - st); break;
    case 0: s->by += st; *i = (int32_t)((uint32_t)*i + st); break;
    case 4: s->bx += st; *i = (int32_t)((uint32_t)*i + st); break;
    case 5: s->bx -= st; *i = (int32_t)((uint32_t)*i + st); break;
    default: break;
    }
}
static void run_last(hud_ctx *c, slide *s, const run *r, int32_t i, int on_port) {
    switch (r->kind) {
    case 1:
        if (!on_port && res2(c)) s->by = (uint32_t)r->end;
        else s->by -= (uint32_t)i;
        break;
    case 0: s->by += (uint32_t)r->a2 - (uint32_t)i; break;
    case 4: case 5: s->bx += (uint32_t)r->a2 - (uint32_t)i; break;
    default: s->bx -= (uint32_t)i - (uint32_t)r->end; break;
    }
}
static int run_slide(hud_ctx *c, slide *s, const run *r) {
    int32_t i = r->start;
    if (hr8(c, s->port + 0x49eu)) {
        if (page_attr(c, s->v, 0x38u, s->p50) || page_attr(c, s->v, 0x3cu, s->p50)) return 1;
        while (run_more(s, r, i)) {
            if (port_call(c, s->port, 0x14u)) return 1;
            if (r->kind == 6) s->bx -= (uint32_t)r->s328;
            else run_step(s, r, &i);
            if (origin(c, s->v, s->tmp, s->p50, s->bx, s->by) || port_call(c, s->port, 0x18u) || present(c, r->sites[0])) return 1;
            if (r->kind == 6) i = (int32_t)((uint32_t)i - (uint32_t)r->s324);
        }
        if (port_call(c, s->port, 0x14u)) return 1;
        run_last(c, s, r, i, 1);
        if (origin(c, s->v, s->tmp, s->p50, s->bx, s->by) || port_call(c, s->port, 0x18u) || page_attr(c, s->v, 0x3cu, s->p54) ||
            HCALL(c, VA_PORT_REDRAW, NULL, s->port, 0u) || present(c, r->sites[1]))
            return 1;
        return 0;
    }
    while (run_more(s, r, i)) {
        if (r->kind == 6) s->bx -= (uint32_t)r->s328;
        else run_step(s, r, &i);
        if (origin(c, s->v, s->tmp, s->p50, s->bx, s->by) || present(c, r->sites[2])) return 1;
        if (r->kind == 6) i = (int32_t)((uint32_t)i - (uint32_t)r->s324);
    }
    run_last(c, s, r, i, 0);
    return origin(c, s->v, s->tmp, s->p50, s->bx, s->by) || present(c, r->sites[3]);
}

static int enter_slides(hud_ctx *c, slide *s, uint32_t req) {
    uint32_t d = s->d, recs = hr32(c, d + 4u);
    int32_t dx = (int32_t)hr32(c, s->delta), dy = (int32_t)hr32(c, s->delta + 4u);
    run r;
    memset(&r, 0, sizeof(r));
    r.kind = (int)req;
    switch (req) {
    case 1: {
        if (res2(c)) {
            r.a2 = 0x1e0;
            s->step = (uint16_t)(s->step << 1);
            r.start = s16((uint16_t)(0u - s->step));
            r.end = s16((uint16_t)(0u - 0x1e0u));
        } else {
            r.a2 = s16((uint16_t)(0u - (uint32_t)dy));
            s->by += (uint32_t)r.a2;
            r.start = r.a2;
            r.end = s16(s->step);
        }
        hw32(c, VA_SLIDING, 0);
        if (paged(c)) {
            s->by -= hr32(c, recs + 0x1cu);
            if (origin(c, s->v, s->tmp, 0, s->bx, s->by)) return 1;
        }
        static const uint32_t sites[4] = {0x0042b13au, 0x0042b22cu, 0x0042b294u, 0x0042b33eu};
        memcpy(r.sites, sites, sizeof(sites));
        if (run_slide(c, s, &r) || HCALL0(c, VA_SLIDE_UP_DONE, NULL)) return 1;
        if (res2(c) && flip(c, s, 1)) return 1;
        hw32(c, VA_SLIDING, 1);
        hw32(c, d + 0x14u, hr32(c, d + 0x14u) - 1u);
        return HCALL(c, VA_SLIDE_SOUND, NULL, 1u);
    }
    case 0: {
        if (res2(c)) {
            r.a2 = 0x1e0;
            s->step = (uint16_t)(s->step << 1);
        } else r.a2 = s16((uint16_t)dy);
        hw32(c, VA_SLIDING, 0);
        if (paged(c)) {
            s->by -= hr32(c, recs + 0x3cu);
            if (origin(c, s->v, s->tmp, 0, s->bx, s->by)) return 1;
        }
        static const uint32_t sites[4] = {0x0042b720u, 0x0042b81cu, 0x0042b87fu, 0x0042b917u};
        memcpy(r.sites, sites, sizeof(sites));
        if (run_slide(c, s, &r) || HCALL0(c, VA_SLIDE_DONE, NULL)) return 1;
        if (res2(c) && flip(c, s, 1)) return 1;
        hw32(c, VA_SLIDING, 1);
        hw32(c, d + 0x14u, hr32(c, d + 0x14u) + 1u);
        return HCALL(c, VA_SLIDE_SOUND, NULL, 0u);
    }
    case 2: {
        hw32(c, d + 0x14u, 4);
        hw8(c, d + 0x36u, 1);
        uint32_t level = 0;
        if (HCALL(c, VA_LEVEL, &level, 0x2du)) return 1;
        hw32(c, VA_SAVED_LEVEL, level);
        if (hr8(c, VA_SOUND_ON) && HCALL(c, VA_SET_LEVEL, NULL, 0x2du, 0x64u)) return 1;
        return 0;
    }
    case 3:
        if (present(c, 0x0042bb84u) || driver(c, VA_DRV_UNLOCK) || port_call(c, s->port, 0x18u)) return 1;
        hw32(c, d + 0x14u, hr32(c, d + 0x23u));
        if (hr8(c, VA_SOUND_ON) && HCALL(c, VA_SET_LEVEL, NULL, 0x2du, hr32(c, VA_SAVED_LEVEL))) return 1;
        return 0;
    default: break;
    }
    /* 4, 5, 6: the sideways slides */
    uint32_t mode_after = req == 4u ? 2u : req == 5u ? 3u : 0u;
    if (res2(c)) {
        if (flip(c, s, 0)) return 1;
        hw32(c, d + 0x14u, mode_after);
        return 0;
    }
    uint32_t rp;
    if (req == 6u) {
        int32_t step = s16(s->step);
        if (hr32(c, d + 0x14u) == 2u) {
            r.a2 = s16((uint16_t)dx);
            r.s324 = step;
            r.s328 = step;
            r.start = r.a2;
            r.end = 0;
            rp = recs + 0x60u;
        } else {
            r.a2 = s16((uint16_t)(0u - (uint32_t)dx));
            r.s324 = step;
            r.s328 = (int32_t)(0u - (uint32_t)step);
            r.start = 0;
            r.end = (int32_t)(0u - (uint32_t)r.a2);
            rp = recs + 0x40u;
        }
        if (paged(c)) {
            s->bx -= hr32(c, rp + 0x18u);
            if (origin(c, s->v, s->tmp, 0, s->bx, s->by)) return 1;
        }
        hw32(c, VA_SLIDING, 0);
        static const uint32_t sites[4] = {0x0042ce4eu, 0x0042cf44u, 0x0042cfb3u, 0x0042d045u};
        memcpy(r.sites, sites, sizeof(sites));
    } else {
        r.a2 = s16((uint16_t)(0u - (uint32_t)dx));
        hw32(c, VA_SLIDING, 0);
        if (paged(c)) {
            s->bx -= hr32(c, recs + (req == 4u ? 0x58u : 0x78u));
            if (origin(c, s->v, s->tmp, 0, s->bx, s->by)) return 1;
        }
        static const uint32_t four[4] = {0x0042bf07u, 0x0042c003u, 0x0042c066u, 0x0042c0feu};
        static const uint32_t five[4] = {0x0042c668u, 0x0042c764u, 0x0042c7c7u, 0x0042c85fu};
        memcpy(r.sites, req == 4u ? four : five, sizeof(four));
    }
    if (run_slide(c, s, &r) || HCALL0(c, VA_SLIDE_DONE, NULL)) return 1;
    if (!paged(c) && flip(c, s, 1)) return 1;
    hw32(c, VA_SLIDING, 1);
    hw32(c, d + 0x14u, mode_after);
    return 0;
}

static int enter(hud_ctx *c, uint32_t d, uint32_t *eax) {
    if (hr32(c, d + 0x18u) == 0xffffffffu || !hr8(c, d + 0x1cu)) {
        *eax = hr32(c, d + 0x14u);
        return 0;
    }
    uint32_t req = hr32(c, d + 0x18u);
    if (req > 6u) return hud_refuse(c, 0x0042aaa5u, "display enter", "a request above 6 scrolls with an undefined area and delta");
    slide s;
    memset(&s, 0, sizeof(s));
    s.d = d;
    s.v = hr32(c, VA_VIEW);
    s.gc = hr32(c, s.v + 0x44u);
    if (!s.gc) return hud_null_refuse(c, 0x0042aa49u, "display enter", "the view has no graphics context");
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0x2cu);
    if (!f) return hud_refuse(c, 0x0042a9c0u, "display enter", "the own code's call area is exhausted");
    s.region = f;
    s.delta = f + 0x14u;
    s.pt = f + 0x1cu;
    s.tmp = f + 0x24u;
    hud_counter(c, 3);
    s.gc -= 4u;
    s.p50 = hr32(c, s.v + 0x88u);
    s.p54 = hr32(c, s.v + 0x8cu);
    s.save208 = hr32(c, s.gc + 0x208u);
    hw32(c, s.gc + 0x208u, 0);
    uint32_t save20c = hr32(c, s.gc + 0x20cu);
    hw32(c, s.gc + 0x20cu, 0);
    hw16(c, VA_SPLIT_DIRTY, 0);
    int scroll = 1;
    uint32_t recs = hr32(c, d + 4u), mode = hr32(c, d + 0x14u), rect = s.region + 4u, hport = 0;
    switch (req) {
    case 0:
    case 1: {
        if (mode == (req == 0u ? 1u : 0u)) {
            hud_counter(c, -3);
            *eax = hr32(c, d + 0x14u);
            hud_frame_pop(c, top);
            return 0;
        }
        uint32_t to = recs + 32u * mode + (req == 0u ? 0x38u : 0xfffffff8u), at = recs + 32u * mode + 0x18u;
        hw32(c, s.delta, hr32(c, to) - hr32(c, at));
        hw32(c, s.delta + 4u, hr32(c, to + 4u) - hr32(c, at + 4u));
        if (res2(c) || paged(c)) scroll = 0;
        else if (req == 0u) {
            hw32(c, rect, 0u - hr32(c, s.delta));
            hw32(c, rect + 4u, 0xf0u - hr32(c, s.delta + 4u));
            hw32(c, rect + 8u, 0x140u);
            hw32(c, rect + 0xcu, 0xf0u);
        } else {
            hw32(c, rect, 0);
            hw32(c, rect + 4u, 0);
            hw32(c, rect + 8u, 0x140u - hr32(c, s.delta));
            hw32(c, rect + 0xcu, 0u - hr32(c, s.delta + 4u));
        }
        s.step = 10;
        hport = 1;
        break;
    }
    case 2:
        scroll = 0;
        if (driver(c, VA_DRV_LOCK)) return 1;
        hw8(c, VA_PANEL_UP, 0);
        break;
    case 3:
        if (paged(c)) scroll = 0;
        else {
            hw32(c, rect + 4u, 0);
            hw32(c, rect, 0);
            hw32(c, rect + 8u, 0x140u);
            hw32(c, rect + 0xcu, kind1(c) ? 0x1e0u : 0xf0u);
            hw32(c, s.delta + 4u, 0);
            hw32(c, s.delta, 0);
        }
        hw8(c, VA_PANEL_UP, 1);
        hport = 1;
        break;
    default: {
        uint32_t other = req == 4u ? recs + 0x58u : req == 5u ? recs + 0x78u : recs + 32u * mode + 0x18u;
        uint32_t x = hr32(c, recs + 0x18u), y = hr32(c, recs + 0x1cu);
        if (req == 4u) { x -= hr32(c, other); y -= hr32(c, other + 4u); }
        else { x += hr32(c, other); y += hr32(c, other + 4u); }
        hw32(c, s.delta, x);
        hw32(c, s.delta + 4u, y);
        scroll = 0;
        s.step = 0x14;
        hport = 1;
        break;
    }
    }
    if (hport) {
        uint32_t p = hr32(c, hr32(c, VA_HUD) + 0x20bu);
        if (port_call(c, p, 0x18u)) return 1;
    }
    if (scroll) {
        if (page_attr(c, s.v, 0x38u, s.p54) || page_attr(c, s.v, 0x3cu, s.p50) || driver(c, VA_DRV_LOCK) ||
            HCALL(c, VA_SCROLL, NULL, s.region, s.delta) || driver(c, VA_DRV_UNLOCK) || page_attr(c, s.v, 0x3cu, s.p54))
            return 1;
    }
    hw32(c, s.gc + 0x208u, s.save208);
    hw32(c, s.gc + 0x20cu, save20c);
    if (paged(c)) {
        s.bx = hr32(c, VA_PAGE_X);
        s.by = hr32(c, VA_PAGE_Y);
    }
    hw8(c, d + 0x1cu, 0);
    if (HCALL0(c, VA_PORT_A, &s.port)) return 1;
    req = hr32(c, d + 0x18u);
    if (req <= 6u && enter_slides(c, &s, req)) return 1;
    /* the tail 0x42d260 */
    hw32(c, d + 0x18u, 0xffffffffu);
    hw16(c, d + 0x1du, 2);
    if (hr8(c, VA_JOLT_DONE) && HCALL(c, VA_JOLT, NULL, 10u)) return 1;
    if (HCALL(c, VA_BUTTONS_UP, NULL, hr32(c, VA_HUD))) return 1;
    *eax = hr32(c, d + 0x14u);
    hud_counter(c, -3);
    hud_frame_pop(c, top);
    return 0;
}

/* ======================================================================
 * The service rows. */
void es2_sim_service_display_leave(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (leave(&c, sc->slots[0])) { hud_failed(sc); return; }
    sc->eax = hud_fs0();
}
void es2_sim_service_display_enter(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (enter(&c, sc->slots[0], &sc->eax)) hud_failed(sc);
}
void es2_sim_service_shield_palette(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (shield_palette(&c, sc->slots[0], &sc->eax)) hud_failed(sc);
}
void es2_sim_service_display_border_off(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (border_off(&c, sc->slots[0], &sc->eax)) hud_failed(sc);
}
void es2_sim_service_buttons_down(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (buttons(&c, sc->slots[0], 1, &sc->eax)) hud_failed(sc);
}
void es2_sim_service_buttons_up(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (buttons(&c, sc->slots[0], 0, &sc->eax)) hud_failed(sc);
}
void es2_sim_service_button_down(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (button(&c, sc->slots[0], 1, &sc->eax)) hud_failed(sc);
}
void es2_sim_service_button_up(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (button(&c, sc->slots[0], 0, &sc->eax)) hud_failed(sc);
}
/* 0x45c638 (listing 0x45c638..0x45c641): polls port 0x3da until the vertical
 * retrace bit (8) is set. Platform deviation (display_modes.md, "the display
 * transitions and the shield palette as own code"): no port I/O under Windows
 * NT; the presenter paces the frames. No guest effect; AL is the port byte in
 * the original and is not read by its callers. */
void es2_sim_service_retrace_wait(es2_sim_service_call *sc, void *user) {
    (void)user;
    (void)sc;
}

/* ---- the capture switch and its neighbours (display_modes.md, "the capture switch, the done step, the surface frame and the intro set-up") */
#define VA_CAPTURE 0x0049eaa5u       /* byte: the flight keys captured */
#define VA_CAPTURE_SKIP 0x0049eb61u  /* byte: no hot-key registration */
#define VA_ID_KEYBOARD 0x004d249cu   /* the current keyboard's device id */
#define VA_DEVICE_SLOTS 0x004d2468u  /* the device records by id */
#define VA_HOTKEYS 0x006bb8b8u       /* ten 12-byte entries: +0 used, +4 list, +8 callback */

/* 0x477b18(K, callback, list) (listing 0x477b18..0x477b4c): the first free
 * entry of the hot-key table gets used 1, callback and list (K is not
 * read); EAX the entry's address with AL = 1, or 0 when all ten are used. */
static uint32_t capture_hotkey(hud_ctx *c, uint32_t callback, uint32_t list) {
    for (uint32_t k = 0, e = VA_HOTKEYS; k < 10u; ++k, e += 12u) {
        if (hr32(c, e)) continue;
        hw32(c, e, 1);
        hw32(c, e + 8u, callback);
        hw32(c, e + 4u, list);
        return (e & ~0xffu) | 1u;
    }
    return 0;
}
/* 0x45b68c(on) (listing 0x45b68c..0x45b732), the flight-key capture switch
 * that the slides of 0x42a9c0 call with 1 and 0. Only the low byte of the slot counts. Equal to
 * [0x49eaa5]: nothing. Else the current keyboard 0x45bd84 -> 0x45bf40
 * ([0x4d249c] the id, nonzero: the record [0x4d2468 + 4 id], nonzero:
 * record - 4); none: EAX 0. With on and [0x49eb61] clear the hot keys
 * 0x477b18(K, 0x45a324, 0x49eb0b) and 0x477b18(K, 0x45a498, 0x49eb35);
 * then [0x49eaa5] ^= 1 and, now clear, the key arrays 0x4d2394, 0x4d23a3
 * and 0x4d23b2 (15 bytes each) zeroed through memset 0x49681a. EAX: the
 * second registration's result; otherwise EBX with BL = 1 (the upper bytes
 * are the caller's EBX, which a service does not see: 1 here). No caller
 * reads it (0x42a9c0 at 0x42b55c/0x42bb34 jumps to its tail, which loads
 * EAX again; 0x45a655, 0x46126b, 0x46128b discard it). */
static int flight_capture(hud_ctx *c, uint32_t on, uint32_t *eax) {
    uint8_t want = (uint8_t)on;
    uint32_t result = 1;
    *eax = 1;
    if (want == hr8(c, VA_CAPTURE)) return 0;
    uint32_t id = hr32(c, VA_ID_KEYBOARD), record = id ? hr32(c, VA_DEVICE_SLOTS + 4u * id) : 0;
    if (!record) { *eax = 0; return 0; }
    if (want && !hr8(c, VA_CAPTURE_SKIP)) {
        capture_hotkey(c, 0x0045a324u, 0x0049eb0bu);   /* the flight keys (hook flight_key) */
        result = capture_hotkey(c, 0x0045a498u, 0x0049eb35u);   /* the command keys */
    }
    hw8(c, VA_CAPTURE, (uint8_t)(hr8(c, VA_CAPTURE) ^ 1u));
    if (!hr8(c, VA_CAPTURE)) {
        static const uint32_t arrays[3] = {0x004d2394u, 0x004d23a3u, 0x004d23b2u};
        for (unsigned i = 0; i < 3u; ++i)
            if (HCALL(c, HUD_VA_MEMSET, NULL, arrays[i], 0u, 15u)) return 1;
    }
    *eax = result;
    return 0;
}
void es2_sim_service_flight_capture(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    if (flight_capture(&c, sc->slots[0], &sc->eax)) hud_failed(sc);
}

/* 0x433ee8() (listing 0x433ee8..0x433f7b), the cockpit done step: the
 * twin of load.c's cockpit_step 0x433f7c with the record H+0x23a
 * instead of H+0x236. H = [0x49b088]. [H+0x226] equal to H+0x232:
 * [H+0x22a] <- H+0x23a, EAX H, EDX H+0x23a. Else [H+0x226] <- H+0x23a, P
 * = [H+0x23a], link [P+4], q = link + 4 (0 without a link), and the empty
 * 0x48992e(q, P+8) (word [0x4d2566] set) or 0x486e70(q, P+8) (enter, three
 * pushes and pops, leave: gadgets.c's bare rows; not called here, they change
 * nothing). Registers as the original leaves them: paged EAX H with a link
 * (else P), EDX P with a link (else P+8), ECX q; unpaged EAX q, EDX P+8,
 * ECX q. Callers 0x42b917, 0x42c0fe, 0x42c85f, 0x42d045 (0x42a9c0). */
void es2_sim_service_cockpit_done(es2_sim_service_call *sc, void *user) {
    (void)user;
    HSERVE(sc, c);
    uint32_t h = hr32(&c, VA_HUD);
    if (h + 0x232u == hr32(&c, h + 0x226u)) {
        hw32(&c, h + 0x22au, h + 0x23au);
        sc->eax = h;
        sc->edx = h + 0x23au;
        return;
    }
    hw32(&c, h + 0x226u, h + 0x23au);
    uint32_t p = hr32(&c, h + 0x23au), link = hr32(&c, p + 4u), q = link ? link + 4u : 0u;
    if (paged(&c)) {
        sc->eax = link ? h : p;
        sc->edx = link ? p : p + 8u;
    } else {
        sc->eax = q;
        sc->edx = p + 8u;
    }
    sc->ecx = q;
}
