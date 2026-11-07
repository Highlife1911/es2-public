/* The modal dialogs (es2/sim/dialogs.h; contract main_loop.md, "the main loop, pump,
 * window procedure, pause, limiter and clock", facts and headless decisions
 * main_loop.md, "the modal dialogs in the own process"). Every row below is the
 * original's function at its VA, read from the listing; guest state is read and
 * written only through the bound main loop's guest
 * view, every call of other DBSIM code (the pump, the control record, the clock, the
 * ports, the gadget and driver slots, the graphics library, the hooks) goes through
 * the dispatch over the loop's call area. The helpers of src/hud_internal.h are
 * reused: the clock is called with hud_clock (traced, kind 'C') at the original's
 * sites.
 *
 * The cursor placement 0x4808dc calls GetClientRect, ClientToScreen and SetCursorPos
 * through their rows. The empty routines 0x48992e, 0x486e70 and 0x4898d4 (they only
 * save and restore registers) are not called. The exception frames of 0x452c04 and
 * 0x456d68 (0x49630c, stack locals only) and 0x452c04's RTL counter bumps (+3, -1,
 * -1, -1: net 0) are not reproduced. */
#include "es2/sim/dialogs.h"
#include "hud_internal.h"
#include "es2/sim/present.h"

/* ---- guest VAs */
#define VA_RECORD 0x004d22c6u          /* the control record R (0x30 bytes) */
#define VA_STOP 0x004d2532u            /* word: WM_QUIT seen */
#define VA_QUIT 0x004d24feu            /* word: the quit request */
#define VA_PRESENT_ON 0x004d2566u      /* word: the dialog's presents are due (the DIB path) */
#define VA_NEXT_PRESENT 0x004d1e64u    /* the clock of the next present */
#define VA_PRESENT_PERIOD 0x004d1e60u
#define VA_DEPTH 0x0049dd8cu           /* word: open dialogs */
#define VA_FLAG_A 0x0049eaa4u          /* byte, 0 while a dialog is open */
#define VA_FLAG_B 0x0049dbecu          /* byte, 0 while a dialog is open */
#define VA_HOLD_POINTER 0x004d1e04u    /* -> a dword cleared at the open */
#define VA_COCKPIT 0x0049b088u
#define VA_SURFACE 0x006c5ed0u         /* the current surface S */
#define VA_GC_SAVED 0x006c5f70u
#define VA_PAGE_A 0x004a3686u
#define VA_PAGE_B 0x004a368au
#define VA_DRV_EC 0x004a57ecu          /* driver slots (called through them) */
#define VA_DRV_F0 0x004a57f0u
#define VA_DRV_28 0x004a5828u
#define VA_DRV_24 0x004a5824u
#define VA_DRAW_FLAG 0x0049dbdfu       /* byte */
#define VA_CB_OPEN 0x0049dd94u         /* optional callbacks (no arguments) */
#define VA_CB_CLOSE 0x0049dd98u
#define VA_FONT 0x0049dd90u            /* the dialog font, loaded at the first open */
#define VA_FONT_NAME 0x0049de85u
#define VA_FONT_DIR 0x004a0a14u
#define VA_SOUND 0x0049f8ecu           /* the sound object, serviced once per pass */
#define VA_LAST_PANEL 0x0049dbd8u
#define VA_LAST_ITEM 0x0049dbdcu       /* word */
#define VA_DEVICE_ID 0x004d24a0u
#define VA_DEVICE_TABLE 0x004d2468u
#define VA_KEY_FLAGS 0x004d22f5u       /* bytes */
#define VA_KEY_VALUES 0x004d22fdu      /* bytes */
#define VA_PEN_SAVE_A 0x004a3618u
#define VA_PEN_SAVE_B 0x004a3628u
#define VA_PEN_SAVE_C 0x004a362cu
#define VA_HWND 0x004d3610u
#define VA_CLIENT_RECT 0x006c5f5au
#define VA_HALF_W 0x004d3040u
#define VA_HALF_H 0x004d3044u
#define VA_PAUSED_TOTAL 0x004d1294u
#define VA_PREFS 0x004d1f38u           /* bytes */
#define VA_PREFS_PREVIOUS 0x004d1fa4u
#define VA_OPT_TABLE_4 0x0049e2b2u     /* words: the display option values */
#define VA_OPT_TABLE_6 0x0049e2bcu
#define VA_OPT_TABLE_7 0x0049e2c6u
#define VA_OPT_TABLE_8 0x0049e2d0u
#define VA_OPT_FLAG 0x0049e9a5u        /* byte: options 2 and 3 enabled */
#define VA_PREFS_BLOCK 0x0049e2dcu
/* Callees (through the dispatch). */
#define VA_PUMP 0x00464c78u
#define VA_CONTROL_RECORD 0x0045a810u
#define VA_PORT_A 0x00433158u
#define VA_SOUND_SERVICE 0x00464260u
#define VA_LABEL_DRAW 0x00438920u
#define VA_GC_SELECT 0x00480d44u
#define VA_PREFS_SET 0x00459958u
#define VA_PREFS_STORE 0x00459b94u
#define VA_BUTTON_RELEASE 0x004530bcu
#define VA_PRESENT 0x00464a14u
#define VA_FONT_LOAD 0x00430fb0u
#define VA_SURFACE_FILL 0x00488012u    /* the graphics library: the frame fill, blit and background */
#define VA_RECT_BLIT 0x00488143u
#define VA_RECT_CLEAR 0x00487e60u
#define VA_PEN_SET 0x00486e7cu
#define VA_PEN_SET_B 0x0048993au
#define VA_GET_CLIENT_RECT 0x00496970u

static es2_sim_dialogs own_dialogs, *bound_dialogs;
void es2_sim_dialogs_init(es2_sim_dialogs *d) { memset(d, 0, sizeof(*d)); }
void es2_sim_dialogs_bind(es2_sim_dialogs *d) { bound_dialogs = d; }
static es2_sim_dialogs *dialogs(void) { return bound_dialogs ? bound_dialogs : &own_dialogs; }

static void add32(hud_ctx *c, uint32_t a, uint32_t v) { hw32(c, a, hr32(c, a) + v); }
static int32_t count_of(hud_ctx *c, uint32_t d) { return (int32_t)hr32(c, d + 0x2cdu); }
static uint32_t gadget(hud_ctx *c, uint32_t d, uint32_t k) { return hr32(c, d + 0x291u + 4u * k); }
/* [[g+0x17]+slot](g, ...): a gadget's table call. */
static int gadget_call(hud_ctx *c, uint32_t g, uint32_t slot, const uint32_t *a, unsigned n) {
    return hud_call(c, hr32(c, hr32(c, g + 0x17u) + slot), a, n, NULL);
}
/* A call through a driver slot or callback pointer. */
static int slot_call(hud_ctx *c, uint32_t slot, const uint32_t *a, unsigned n) {
    return hud_call(c, hr32(c, slot), a, n, NULL);
}
/* 0x454c98's and 0x4548c8's copy of the control record (0x30 bytes; the
 * original copies word 0, bytes 0xa..0xd, words 0xe..0x14, bytes
 * 0x16..0x21, dwords 0x22..0x2a, word 0x2e and dwords 2 and 6: every byte
 * once). */
static void record_copy(hud_ctx *c, uint32_t dst, uint32_t src) {
    for (uint32_t i = 0; i < 0x30u; i += 4u) hw32(c, dst + i, hr32(c, src + i));
}
/* The page select of 0x45464c, 0x4548c8, 0x456d68 and 0x457480: S+slot <-
 * page; S current: [0x4a368a] (or [0x4a3686] for slot 0x38) <- it and the
 * driver's page call. */
static int page_select(hud_ctx *c, uint32_t s, uint32_t slot, uint32_t page, uint32_t global, uint32_t driver) {
    hw32(c, s + slot, page);
    if (s != hr32(c, VA_SURFACE)) return 0;
    hw32(c, global, hr32(c, s + slot));
    return slot_call(c, driver, (const uint32_t[]){page}, 1);
}

/* ---- 0x454ac0 (listing 0x454ac0..0x454acb): [0x4d1e64] <- the clock. */
static int present_time(hud_ctx *c, uint32_t *eax) {
    uint32_t now = 0;
    if (hud_clock(c, 0x00454ac0u, &now)) return 1;
    hw32(c, VA_NEXT_PRESENT, now);
    if (eax) *eax = now;
    return 0;
}

/* ---- 0x45b734(i, v) (listing 0x45b734..0x45b74f): the key flag i (low
 * byte) set with its value byte. */
static void key_flag(hud_ctx *c, uint32_t i, uint32_t v) {
    hw8(c, VA_KEY_FLAGS + (i & 0xffu), 1);
    hw8(c, VA_KEY_VALUES + (i & 0xffu), (uint8_t)v);
}
/* ---- 0x45bf68 (listing 0x45bf68..0x45bf8e; 0x45bd8c calls it): the
 * current pointing device: the table entry of [0x4d24a0] less 0x3a, 0
 * without an id or entry. */
static uint32_t device_current(hud_ctx *c) {
    uint32_t id = hr32(c, VA_DEVICE_ID);
    if (!id) return 0;
    uint32_t o = hr32(c, VA_DEVICE_TABLE + 4u * id);
    return o ? o - 0x3au : 0;
}
/* ---- 0x452bbc(w) (listing 0x452bbc..0x452bd1): the last panel, item -1. */
static void panel_last(hud_ctx *c, uint32_t w) {
    hw32(c, VA_LAST_PANEL, w);
    hw16(c, VA_LAST_ITEM, 0xffffu);
}
/* ---- 0x452c04(w) (listing 0x452c04..0x452c69): delete w when set (the
 * exception frame and the net-zero counter bumps are not reproduced). */
static int panel_free(hud_ctx *c, uint32_t w) {
    if (!w) return 0;
    return HCALL(c, HUD_VA_DELETE, NULL, w);
}

/* ---- the ports (0x435b38, 0x435b58, 0x435b80, 0x435298) */
/* 0x435b38(q, x) (listing 0x435b38..0x435b55): the port's redraw byte when x
 * is 0 or its item. */
static void port_mark(hud_ctx *c, uint32_t q, uint32_t x) {
    if (x == 0 || x == hr32(c, q + 0x49au)) hw8(c, q + 0x4cbu, 1);
}
/* 0x435b58(q) (listing 0x435b58..0x435b7c): the pause clock at the first
 * pause (site 0x435b68), the pause count up. */
static int port_pause(hud_ctx *c, uint32_t q) {
    if (!hr32(c, q + 0x4ccu)) {
        uint32_t now = 0;
        if (hud_clock(c, 0x00435b68u, &now)) return 1;
        hw32(c, q + 0x4ccu, now);
    }
    hw16(c, q + 0x4d0u, hr16(c, q + 0x4d0u) + 1u);
    return 0;
}
/* 0x435298(b, delta) (listing 0x435298..0x4352e9): the clock (site
 * 0x4352a1); every indexed entry (0x31 bytes, index table at b+0x1ea, count
 * word b+0x21c) whose times +0x24 and +0x28 lie ahead moves by delta. */
static int port_shift(hud_ctx *c, uint32_t b, uint32_t delta) {
    uint32_t now = 0;
    if (hud_clock(c, 0x004352a1u, &now)) return 1;
    for (int32_t k = 0; k < (int32_t)hsx16(hr16(c, b + 0x21cu)); ++k) {
        uint32_t e = b + hr32(c, b + 0x1eau + 4u * (uint32_t)k) * 0x31u;
        if (now < hr32(c, e + 0x24u)) add32(c, e + 0x24u, delta);
        if (now < hr32(c, e + 0x28u)) add32(c, e + 0x28u, delta);
    }
    return 0;
}
/* 0x435b80(q) (listing 0x435b80..0x435bec): the pause count down; at zero
 * the clock (site 0x435b9a), the paused time shifts the port's entries and
 * its item's times +0x1c/+0x20 when the item is still ahead (else the
 * redraw byte), the pause clock is cleared and [0x4d1294] grows by it. */
static int port_resume(hud_ctx *c, uint32_t q) {
    hw16(c, q + 0x4d0u, hr16(c, q + 0x4d0u) - 1u);
    if (hr16(c, q + 0x4d0u)) return 0;
    uint32_t now = 0;
    if (hud_clock(c, 0x00435b9au, &now)) return 1;
    uint32_t delta = now - hr32(c, q + 0x4ccu);
    if (port_shift(c, q + 4u, delta)) return 1;
    uint32_t a = hr32(c, q + 0x49au);
    if (a) {
        if (now < hr32(c, a + 0x1cu)) {
            add32(c, a + 0x1cu, delta);
            add32(c, hr32(c, q + 0x49au) + 0x20u, delta);
        } else {
            hw8(c, q + 0x4cbu, 1);
        }
    }
    hw32(c, q + 0x4ccu, 0);
    add32(c, VA_PAUSED_TOTAL, delta);
    return 0;
}

/* ---- the pressed-gadget release list of a panel (0x4530fc, 0x4531a4; the
 * entries are added by the hook 0x4530bc) */
/* 0x4531a4(p) (listing 0x4531a4..0x4531b2): the count, sign-extended. */
static uint32_t release_count(hud_ctx *c, uint32_t p) { return hsx16(hr16(c, p + 0x396u)); }
/* The list holds 8 entries (+0x356..+0x395). 0x4530bc compares the count with
 * 7 (0x4530c3) without a branch, so a ninth entry (+0x396..+0x39d) lands on
 * the count word and 6 bytes past the 0x398-byte panel, and the next scan
 * reads garbage gadgets past the list: the original faults at 0x453127
 * (live, DEMO1 tape playback). Both ends are refused instead
 * (crashes are avoided). */
#define RELEASE_CAPACITY 8
static int release_overrun(hud_ctx *c, uint32_t va) {
    return hud_refuse(c, va, "button release list",
                      "a ninth pending entry overruns the panel's 8-entry list (+0x356, count +0x396); the original "
                      "faults at 0x453127 in the next scan");
}
/* 0x4530bc(p, g) through the dispatch, refused when the list is full. */
static int release_append(hud_ctx *c, uint32_t p, uint32_t g) {
    if ((int32_t)release_count(c, p) >= RELEASE_CAPACITY) return release_overrun(c, VA_BUTTON_RELEASE);
    return HCALL(c, VA_BUTTON_RELEASE, NULL, p, g);
}
/* 0x4530fc(p) (listing 0x4530fc..0x4531a2): per entry the clock (site
 * 0x45311a); before the entry's time the gadget shows state 1 (redrawn
 * once), else state 0, redrawn, and the entry leaves the list. */
static int release_tick(hud_ctx *c, uint32_t p) {
    if ((int32_t)release_count(c, p) > RELEASE_CAPACITY) return release_overrun(c, 0x004530fcu);
    uint32_t i = 0;
    while ((int32_t)release_count(c, p) > (int32_t)i) {
        uint32_t e = p + 0x356u + 8u * i, g = hr32(c, e), now = 0;
        if (hud_clock(c, 0x0045311au, &now)) return 1;
        if (now < hr32(c, e + 4u)) {
            if (hr16(c, g + 0x1bu) != 1u) {
                hw16(c, g + 0x1bu, 1);
                if (gadget_call(c, g, 4u, (const uint32_t[]){g}, 1)) return 1;
            }
            ++i;
            continue;
        }
        hw16(c, g + 0x1bu, 0);
        if (gadget_call(c, g, 4u, (const uint32_t[]){g}, 1)) return 1;
        uint32_t dst = e, src = e + 8u;
        for (uint32_t k = i + 1u; (int32_t)k < (int32_t)release_count(c, p); ++k) {
            hw32(c, dst, hr32(c, src));
            hw32(c, dst + 4u, hr32(c, src + 4u));
            dst += 8u;
            src += 8u;
        }
        hw16(c, p + 0x396u, hr16(c, p + 0x396u) - 1u);
    }
    return 0;
}

/* ---- the cursor (0x4808dc modelled, 0x486e7c/0x48993a through the dispatch) */
/* 0x4808dc(dev, p) (listing 0x4808dc..0x480947), the same model as
 * src/hud_cockpit.c's: GetClientRect(hwnd, 0x6c5f5a) (the input layer's row,
 * result not read); x = ([p] * [0x6c5f62]) / ([0x4d3040] >> 1), then y = ([p+4]
 * * [0x6c5f66]) / ([0x4d3044] >> 1) (imul's low dword, cdq, idiv: a zero
 * divisor or 0x80000000 / -1 is refused at its idiv); ClientToScreen(hwnd,
 * &point) and SetCursorPos(x, y) through their rows (es2/sim/cursor.h). `dev`
 * is not read. */
static int cursor_divide(hud_ctx *c, uint32_t site, uint32_t a, uint32_t b, uint32_t half, int32_t *out) {
    int32_t n = (int32_t)(a * b), d = (int32_t)hr32(c, half) >> 1;
    if (!d || (n == INT32_MIN && d == -1)) return hud_divide_refuse(c, site, (uint64_t)(int64_t)n, (uint32_t)d, "cursor place", "the original's idiv faults (zero or overflowing quotient)");
    *out = n / d;
    return 0;
}
static int cursor_place(hud_ctx *c, uint32_t p) {
    if (HCALL(c, VA_GET_CLIENT_RECT, NULL, hr32(c, VA_HWND), VA_CLIENT_RECT)) return 1;
    int32_t x = 0, y = 0;
    if (cursor_divide(c, 0x00480909u, hr32(c, p), hr32(c, VA_CLIENT_RECT + 8u), VA_HALF_W, &x) ||
        cursor_divide(c, 0x00480922u, hr32(c, p + 4u), hr32(c, VA_CLIENT_RECT + 0xcu), VA_HALF_H, &y))
        return 1;
    uint32_t top = c->l->stack_hi, pt = hud_frame_push(c, 8u);
    if (!pt) return hud_refuse(c, 0x004808dcu, "cursor place", "the own code's call area is exhausted");
    hw32(c, pt, (uint32_t)x);
    hw32(c, pt + 4u, (uint32_t)y);
    if (HCALL(c, 0x0049699au, NULL, hr32(c, VA_HWND), pt) || HCALL(c, 0x004969e2u, NULL, hr32(c, pt), hr32(c, pt + 4u)))
        return 1;
    hud_frame_pop(c, top);
    return 0;
}

/* ---- focus and hit test */
/* 0x454c98(d, i) (listing 0x454c98..0x454d60): the device; i != -1: the
 * cursor to gadget i's centre plus the dialog origin (placed when a device
 * exists, then the pen 0x486e7c); i == -1 with a device: the control
 * record's point against every gadget's rectangle (signed, inclusive), the
 * first hit becomes i. D+0x2f7 <- i. */
static int focus_set(hud_ctx *c, uint32_t d, uint32_t i) {
    uint32_t dev = device_current(c);
    if (i != 0xffffffffu) {
        uint32_t g = gadget(c, d, i), top = c->l->stack_hi, pt = hud_frame_push(c, 8u);
        if (!pt) return hud_refuse(c, 0x00454c98u, "focus", "the own code's call area is exhausted");
        uint32_t x = hr32(c, g) + (uint32_t)((int32_t)(hr32(c, g + 8u) - hr32(c, g)) >> 1) + hr32(c, d + 4u);
        uint32_t y = hr32(c, g + 4u) + (uint32_t)((int32_t)(hr32(c, g + 0xcu) - hr32(c, g + 4u)) >> 1) + hr32(c, d + 8u);
        hw32(c, pt, x);
        hw32(c, pt + 4u, y);
        if (dev && cursor_place(c, pt)) return 1;
        if (HCALL(c, VA_PEN_SET, NULL, pt)) return 1;
        hud_frame_pop(c, top);
    } else if (dev) {
        uint32_t r = 0;
        if (HCALL0(c, VA_CONTROL_RECORD, &r)) return 1;
        int32_t x = (int32_t)hr32(c, r + 2u), y = (int32_t)hr32(c, r + 6u);
        for (int32_t k = 0; k < count_of(c, d); ++k) {
            uint32_t g = gadget(c, d, (uint32_t)k);
            if (x >= (int32_t)hr32(c, g) && x <= (int32_t)hr32(c, g + 8u) && y >= (int32_t)hr32(c, g + 4u) &&
                y <= (int32_t)hr32(c, g + 0xcu)) {
                i = (uint32_t)k;
                break;
            }
        }
    }
    hw32(c, d + 0x2f7u, i);
    return 0;
}
/* 0x454d64(d, g) (the focus to the first slot holding g) has no static
 * reference in DBSIM.EXE (no relocated pointer, no rel32 call: dead code,
 * //sim:table_test's entry evidence) and gets no row. */
/* 0x454d98(d) (listing 0x454d98..0x454dba): the next slot, wrapping at the count. */
static int focus_next(hud_ctx *c, uint32_t d) {
    uint32_t n = hr32(c, d + 0x2f7u) + 1u;
    if (n == hr32(c, d + 0x2cdu)) n = 0;
    return focus_set(c, d, n);
}
/* 0x454dbc(d) (listing 0x454dbc..0x454de4): the previous slot (from -1:
 * -1, then below 0: the last). */
static int focus_previous(hud_ctx *c, uint32_t d) {
    int32_t e = (int32_t)hr32(c, d + 0x2f7u);
    if (e != -1) --e;
    if (e < 0) e = count_of(c, d) - 1;
    return focus_set(c, d, (uint32_t)e);
}
/* 0x454de8(d, i, flag) (listing 0x454de8..0x454e2b): gadget i not in state
 * 2: its table's +8 (the press) and the panel's release entry (hook
 * 0x4530bc). */
static int gadget_press(hud_ctx *c, uint32_t d, uint32_t i, uint32_t flag) {
    uint32_t g = gadget(c, d, i);
    if (hsx16(hr16(c, g + 0x1bu)) == 2u) return 0;
    if (gadget_call(c, g, 8u, (const uint32_t[]){g, flag}, 2)) return 1;
    return release_append(c, hr32(c, d + 0x285u), gadget(c, d, i));
}
/* 0x454e2c(d, r) (listing 0x454e2c..0x454f10): the dialog's key handler
 * (vtable +0x10): the record's click byte +0xd presses the focused gadget
 * (none: focus 0) and sets key flag 1; byte +0x17 moves the focus on and
 * sets key flag 2; else the key word: 1 presses the default gadget
 * D+0x2fb, 0xf and 0x52 move on, 0x1c presses the focused gadget (or
 * focuses 0), 0x80f moves back. AL 1 when acted (the original leaves the
 * last callee's upper bytes), EAX 0 else. */
static int dialog_keys(hud_ctx *c, uint32_t d, uint32_t r, uint32_t *eax) {
    *eax = 1;
    if (hr8(c, r + 0xdu)) {
        uint32_t f = hr32(c, d + 0x2f7u);
        if (f != 0xffffffffu ? gadget_press(c, d, f, 1) : focus_set(c, d, 0)) return 1;
        key_flag(c, 1, 1);
        return 0;
    }
    if (hr8(c, r + 0x17u)) {
        if (focus_next(c, d)) return 1;
        key_flag(c, 2, 1);
        return 0;
    }
    switch (hr16(c, r)) {
    case 0x1u: {
        uint32_t f = hr32(c, d + 0x2fbu);
        return f != 0xffffffffu ? gadget_press(c, d, f, 1) : 0;
    }
    case 0xfu: case 0x52u: return focus_next(c, d);
    case 0x1cu: {
        uint32_t f = hr32(c, d + 0x2f7u);
        return f != 0xffffffffu ? gadget_press(c, d, f, 1) : focus_set(c, d, 0);
    }
    case 0x80fu: return focus_previous(c, d);
    default: *eax = 0; return 0;
    }
}
/* 0x455080(g) (listing 0x455080..0x455099): a pending tick count g+0x1f
 * draws the gadget (table +0) and counts down. */
static int gadget_tick(hud_ctx *c, uint32_t g) {
    if (!hr8(c, g + 0x1fu)) return 0;
    if (gadget_call(c, g, 0, (const uint32_t[]){g}, 1)) return 1;
    hw8(c, g + 0x1fu, hr8(c, g + 0x1fu) - 1u);
    return 0;
}
/* 0x45509c(g, flag) (listing 0x45509c..0x4550ca): the press: the owner's
 * vtable +0xc (owner, g, flag), the gadget's table +4 (redraw) and the
 * +0x20 part's table +8. */
static int gadget_fire(hud_ctx *c, uint32_t g, uint32_t flag) {
    uint32_t o = hr32(c, g + 0x24u);
    if (HVCALL(c, o, 0xcu, NULL, o, g, flag)) return 1;
    if (gadget_call(c, g, 4u, (const uint32_t[]){g}, 1)) return 1;
    return hud_call(c, hr32(c, hr32(c, g + 0x20u) + 8u), (const uint32_t[]){g + 0x20u}, 1, NULL);
}

/* ---- the dialog open and close */
/* 0x454b8c(d, f) (listing 0x454b8c..0x454c28): the pen state saved into the
 * dialog (+0x2e6 [0x4a3618]-4, +0x2ea/+0x2ee, +0x2f2 the font); the empty
 * 0x48992e/0x486e70 are not called. */
static void cursor_save(hud_ctx *c, uint32_t d, uint32_t f) {
    hw32(c, d + 0x2e6u, hr32(c, VA_PEN_SAVE_A) - 4u);
    hw32(c, d + 0x2eau, hr32(c, VA_PEN_SAVE_B));
    hw32(c, d + 0x2eeu, hr32(c, VA_PEN_SAVE_C));
    hw32(c, d + 0x2f2u, f);
}
/* 0x454c2c(d) (listing 0x454c2c..0x454c97): with byte d+0x330 only the empty
 * routines: no effect. */
/* 0x454b74(d, a) (listing 0x454b74..0x454b8b): the dialog's label draw. */
static int dialog_label(hud_ctx *c, uint32_t d, uint32_t a) {
    return HCALL(c, VA_LABEL_DRAW, NULL, hr32(c, d + 0x44u), a);
}
static void rect_shift(hud_ctx *c, uint32_t d, int back) {
    uint32_t ox = hr32(c, d + 4u), oy = hr32(c, d + 8u);
    if (back) { add32(c, d + 0x1cu, ox); add32(c, d + 0x20u, oy); add32(c, d + 0x24u, ox); add32(c, d + 0x28u, oy); }
    else { add32(c, d + 0x1cu, 0u - ox); add32(c, d + 0x20u, 0u - oy); add32(c, d + 0x24u, 0u - ox); add32(c, d + 0x28u, 0u - oy); }
}
/* 0x45464c(d) (listing 0x45464c..0x4548c4): the open. */
static int dialog_open(hud_ctx *c, uint32_t d) {
    uint32_t w = 0;
    if (HCALL0(c, VA_PORT_A, &w)) return 1;
    port_mark(c, w, 0);
    if (HVCALL(c, w, 0xcu, NULL, w)) return 1;
    if (port_pause(c, w)) return 1;
    if (port_pause(c, hr32(c, hr32(c, VA_COCKPIT) + 0x20bu))) return 1;
    if (!hr16(c, VA_DEPTH)) {
        hw8(c, VA_FLAG_A, 0);
        hw8(c, VA_FLAG_B, 0);
    }
    if (HCALL0(c, VA_PUMP, NULL)) return 1;   /* the result is ignored here */
    hw32(c, hr32(c, VA_HOLD_POINTER), 0);
    record_copy(c, d + 0x300u, VA_RECORD);
    for (uint32_t i = 1; i <= 4u; ++i) key_flag(c, i, 1);
    if (focus_set(c, d, 0xffffffffu)) return 1;
    panel_last(c, hr32(c, d + 0x285u));
    uint32_t s = hr32(c, VA_SURFACE);
    hw32(c, d + 0x281u, hr32(c, VA_GC_SAVED));
    if (HCALL(c, VA_GC_SELECT, NULL, d + 0x48u)) return 1;
    if (page_select(c, s, 0x38u, hr32(c, s + 0x88u), VA_PAGE_A, VA_DRV_EC)) return 1;
    if (page_select(c, s, 0x3cu, hr32(c, s + 0x88u), VA_PAGE_B, VA_DRV_F0)) return 1;
    hw8(c, d + 0x2f6u, hr8(c, VA_DRAW_FLAG));
    hw8(c, VA_DRAW_FLAG, 1);
    if (hr32(c, VA_CB_OPEN) && slot_call(c, VA_CB_OPEN, NULL, 0)) return 1;
    if (!hr32(c, VA_FONT)) {
        uint32_t path = 0, font = 0;
        if (HCALL(c, HUD_VA_PATH_DIR, &path, VA_FONT_NAME, hr32(c, VA_FONT_DIR))) return 1;
        if (HCALL(c, VA_FONT_LOAD, &font, path)) return 1;
        hw32(c, VA_FONT, font);
    }
    if (slot_call(c, VA_DRV_28, NULL, 0)) return 1;
    if (hr32(c, d + 0x2cu)) {
        rect_shift(c, d, 0);
        if (HCALL(c, VA_SURFACE_FILL, NULL, d + 0x18u, hr32(c, d + 0x2cu))) return 1;
        rect_shift(c, d, 1);
    }
    if (HVCALL(c, d, 0, NULL, d)) return 1;   /* the dialog's draw */
    cursor_save(c, d, hr32(c, VA_FONT));
    if (slot_call(c, VA_DRV_24, NULL, 0)) return 1;
    hw16(c, VA_DEPTH, hr16(c, VA_DEPTH) + 1u);
    return present_time(c, NULL);
}
/* 0x4548c8(d) (listing 0x4548c8..0x454abe): the close. */
static int dialog_close(hud_ctx *c, uint32_t d) {
    hw8(c, VA_DRAW_FLAG, 0);
    if (slot_call(c, VA_DRV_28, NULL, 0)) return 1;
    uint32_t surface = hr32(c, d + 0x2cu);
    if (surface && HCALL(c, VA_RECT_BLIT, NULL, d + 0x18u, d + 0x1cu, surface)) return 1;
    if (hr16(c, VA_PRESENT_ON)) {
        es2_sim_present_site(0x0045490bu);   /* the call at 0x454906 */
        if (HCALL0(c, VA_PRESENT, NULL)) return 1;
    }
    /* 0x454c2c(d): no effect. */
    uint8_t flag = hr8(c, d + 0x2f6u);
    hw8(c, VA_DRAW_FLAG, flag);
    if (flag && slot_call(c, VA_DRV_24, NULL, 0)) return 1;
    if (hr32(c, VA_CB_CLOSE) && slot_call(c, VA_CB_CLOSE, NULL, 0)) return 1;
    if (HCALL(c, VA_GC_SELECT, NULL, hr32(c, d + 0x281u))) return 1;
    uint32_t s = hr32(c, VA_SURFACE);
    if (page_select(c, s, 0x3cu, hr32(c, d + 0x28du), VA_PAGE_B, VA_DRV_F0)) return 1;
    if (panel_free(c, hr32(c, d + 0x285u))) return 1;
    if (hr32(c, d + 0x289u)) panel_last(c, hr32(c, d + 0x289u));
    record_copy(c, VA_RECORD, d + 0x300u);
    if (device_current(c)) {
        if (cursor_place(c, d + 0x302u)) return 1;
        if (HCALL(c, hr16(c, VA_PRESENT_ON) ? VA_PEN_SET_B : VA_PEN_SET, NULL, d + 0x302u)) return 1;
    }
    hw16(c, VA_DEPTH, hr16(c, VA_DEPTH) - 1u);
    if (!hr16(c, VA_DEPTH)) {
        hw8(c, VA_FLAG_A, 1);
        hw8(c, VA_FLAG_B, 1);
    }
    uint32_t w = 0;
    if (HCALL0(c, VA_PORT_A, &w)) return 1;
    if (port_resume(c, w)) return 1;
    return port_resume(c, hr32(c, hr32(c, VA_COCKPIT) + 0x20bu));
}
/* 0x454acc(d) (listing 0x454acc..0x454b73): the per-pass step: the quit
 * word ends the dialog, the sound object is serviced, the release list
 * ticks; an end state is held back (d+0x331) while a pressed gadget is
 * still on the list and comes back when the list is empty; with the
 * presents due, the clock (site 0x454b51) past [0x4d1e64] presents and
 * schedules the next. */
static int dialog_step(hud_ctx *c, uint32_t d) {
    if (hr16(c, VA_QUIT)) hw8(c, d + 0x2d1u, 1);
    uint32_t sound = hr32(c, VA_SOUND);
    if (sound && HCALL(c, VA_SOUND_SERVICE, NULL, sound)) return 1;
    uint32_t p = hr32(c, d + 0x285u);
    if (release_tick(c, p)) return 1;
    if (hr8(c, d + 0x2d1u) && release_count(c, hr32(c, d + 0x285u))) {
        hw8(c, d + 0x331u, hr8(c, d + 0x2d1u));
        hw8(c, d + 0x2d1u, 0);
    } else if (hr8(c, d + 0x331u)) {
        hw8(c, d + 0x2d1u, hr8(c, d + 0x331u));
        hw8(c, d + 0x331u, 0);
    }
    if (!hr16(c, VA_PRESENT_ON)) return 0;
    uint32_t now = 0;
    if (hud_clock(c, 0x00454b51u, &now)) return 1;
    if (now > hr32(c, VA_NEXT_PRESENT)) {
        es2_sim_present_site(0x00454b65u);   /* the call at 0x454b60 */
        if (HCALL0(c, VA_PRESENT, NULL)) return 1;
        hw32(c, VA_NEXT_PRESENT, now + hr32(c, VA_PRESENT_PERIOD));
    }
    return 0;
}

/* ---- the modal loops */
/* One pass counted against the limit. */
static int pass(hud_ctx *c, uint32_t va) {
    es2_sim_dialogs *dl = dialogs();
    ++dl->passes;
    if (dl->pass_limit && dl->passes > dl->pass_limit)
        return hud_refuse(c, va, "modal loop", "the pass limit was reached without the dialog ending");
    return 0;
}
/* 0x456000(d) (listing 0x456000..0x456083) and 0x457b00 (0x457b00..
 * 0x457b70, without the stop-word test): the open, focus 0, then per pass
 * the control record 0x45a810 (its pump), [with the stop word the end
 * state 1,] the key handler (vtable +0x10), every gadget's table +0xc
 * (the tick) and the step; the close; EAX the end state's bit 0. */
static int alert_run(hud_ctx *c, uint32_t d, int stop_test, uint32_t *eax) {
    if (dialog_open(c, d) || focus_set(c, d, 0)) return 1;
    while (!hr8(c, d + 0x2d1u)) {
        if (pass(c, stop_test ? 0x00456000u : 0x00457b00u)) return 1;
        uint32_t r = 0;
        if (HCALL0(c, VA_CONTROL_RECORD, &r)) return 1;
        if (stop_test && hr16(c, VA_STOP)) {
            hw8(c, d + 0x2d1u, 1);
            continue;
        }
        if (HVCALL(c, d, 0x10u, NULL, d, r)) return 1;
        for (int32_t k = 0; k < count_of(c, d); ++k) {
            uint32_t g = gadget(c, d, (uint32_t)k);
            if (gadget_call(c, g, 0xcu, (const uint32_t[]){g}, 1)) return 1;
        }
        if (dialog_step(c, d)) return 1;
    }
    if (dialog_close(c, d)) return 1;
    *eax = hr8(c, d + 0x2d1u) & 1u;
    return 0;
}
/* 0x45617c(d, g) (listing 0x45617c..0x4561c4): the alert's select (vtable
 * +0xc): slot 0 ends with 2, slot 1 with 3; then 0x454ac0. 0x457c4c
 * (0x457c4c..0x457c87): slot 0 ends with 1. */
static int alert_select(hud_ctx *c, uint32_t d, uint32_t g, int two) {
    for (int32_t k = 0; k < count_of(c, d); ++k) {
        if (gadget(c, d, (uint32_t)k) != g) continue;
        if (k == 0) hw8(c, d + 0x2d1u, two ? 2u : 1u);
        else if (k == 1 && two) hw8(c, d + 0x2d1u, 3);
    }
    return present_time(c, NULL);
}

/* ---- the options dialog (F11) */
/* 0x45999c(i, flag) (listing 0x45999c..0x4599d0): pref i's previous byte
 * kept, the byte toggled through 0x459958; EAX the new byte (the
 * original's upper bytes are the caller's EBX). */
static int prefs_toggle(hud_ctx *c, uint32_t i, uint32_t flag, uint32_t *eax) {
    uint32_t k = hsx16((uint16_t)i);
    hw8(c, VA_PREFS_PREVIOUS + k, hr8(c, VA_PREFS + k));
    uint32_t v = hr8(c, VA_PREFS + k) ^ 1u;
    if (HCALL(c, VA_PREFS_SET, NULL, i, v, flag)) return 1;
    *eax = v;
    return 0;
}
/* 0x4599d4(i, n, flag) (listing 0x4599d4..0x459a0d): the byte plus one,
 * wrapping to 0 at n (low bytes). */
static int prefs_cycle(hud_ctx *c, uint32_t i, uint32_t n, uint32_t flag, uint32_t *eax) {
    uint32_t k = hsx16((uint16_t)i);
    hw8(c, VA_PREFS_PREVIOUS + k, hr8(c, VA_PREFS + k));
    uint32_t v = hr8(c, VA_PREFS + k) + 1u;
    if ((uint8_t)v == (uint8_t)n) v = 0;
    if (HCALL(c, VA_PREFS_SET, NULL, i, v, flag)) return 1;
    *eax = v;
    return 0;
}
/* 0x459a10(i, n, flag) (listing 0x459a10..0x459a4d): the byte less one,
 * wrapping to n-1 below 0 (low bytes). */
static int prefs_cycle_back(hud_ctx *c, uint32_t i, uint32_t n, uint32_t flag, uint32_t *eax) {
    uint32_t k = hsx16((uint16_t)i);
    hw8(c, VA_PREFS_PREVIOUS + k, hr8(c, VA_PREFS + k));
    uint32_t v = (uint8_t)(hr8(c, VA_PREFS + k) - 1u);
    if (v == 0xffu) v = (uint8_t)(n - 1u);
    if (HCALL(c, VA_PREFS_SET, NULL, i, v, flag)) return 1;
    *eax = v;
    return 0;
}
/* 0x4574e8(d) (listing 0x4574e8..0x4574fb): prefs_store(9, 0x49e2dc). */
static int options_store(hud_ctx *c) { return HCALL(c, VA_PREFS_STORE, NULL, 9u, VA_PREFS_BLOCK); }
/* 0x457210(d, k) (listing 0x457210..0x4573c0): option k's label (d+0x332+4k)
 * drawn with its value text: options 0, 1 and 5 index the +0x38e table by
 * the pref byte, 2 and 3 the +0x396 table, 4, 6, 7 and 8 the +0x3a2 table
 * through the word tables 0x49e2b2/0x49e2bc/0x49e2c6/0x49e2d0 (sign
 * extended); k > 8 draws nothing; then 0x454ac0. */
static int options_label(hud_ctx *c, uint32_t d, uint32_t k) {
    static const struct { uint32_t label, table, pref, words; } rows[9] = {
        {0x332u, 0x38eu, 0x0u, 0}, {0x336u, 0x38eu, 0x1u, 0}, {0x33au, 0x396u, 0x2u, 0},
        {0x33eu, 0x396u, 0x3u, 0}, {0x342u, 0x3a2u, 0x7u, VA_OPT_TABLE_4}, {0x346u, 0x38eu, 0x8u, 0},
        {0x34au, 0x3a2u, 0x9u, VA_OPT_TABLE_6}, {0x34eu, 0x3a2u, 0xau, VA_OPT_TABLE_7}, {0x352u, 0x3a2u, 0xbu, VA_OPT_TABLE_8},
    };
    if (k <= 8u) {
        uint32_t index = hr8(c, VA_PREFS + rows[k].pref);
        if (rows[k].words) index = hsx16(hr16(c, rows[k].words + 2u * index));
        uint32_t value = hr32(c, d + rows[k].table + 4u * index);
        if (HCALL(c, VA_LABEL_DRAW, NULL, hr32(c, d + rows[k].label), value)) return 1;
    }
    return present_time(c, NULL);
}
/* 0x4573c4(d, g, flags) (listing 0x4573c4..0x457456): the options' select
 * (vtable +0xc): d+0x2ff <- flags bit 1 (the backward direction); the
 * slot of g becomes the pending option d+0x356; below 9 the previously
 * highlighted gadget d+0x3b6 goes to state 3 (redrawn), g to state 0
 * (redrawn) and becomes it; the focus follows; then 0x454ac0. */
static int options_select(hud_ctx *c, uint32_t d, uint32_t g, uint32_t flags) {
    hw8(c, d + 0x2ffu, (flags & 2u) ? 1u : 0u);
    for (int32_t k = 0; k < count_of(c, d); ++k) {
        if (gadget(c, d, (uint32_t)k) != g) continue;
        hw32(c, d + 0x356u, (uint32_t)k);
        if (k < 9) {
            uint32_t h = hr32(c, d + 0x3b6u);
            if (h != 0xffffffffu) {
                hw16(c, gadget(c, d, h) + 0x1bu, 3);
                uint32_t gh = gadget(c, d, hr32(c, d + 0x3b6u));
                if (gadget_call(c, gh, 4u, (const uint32_t[]){gh}, 1)) return 1;
            }
            hw32(c, d + 0x3b6u, (uint32_t)k);
            hw16(c, g + 0x1bu, 0);
            if (gadget_call(c, g, 4u, (const uint32_t[]){g}, 1)) return 1;
        }
        hw32(c, d + 0x2f7u, (uint32_t)k);
        break;
    }
    return present_time(c, NULL);
}
/* 0x457480(d) (listing 0x457480..0x4574e7): with a page hook d+0x3ba: the
 * surface's page +0x8c selected, the saved GC, the hook, the dialog's GC. */
static int options_page(hud_ctx *c, uint32_t d) {
    if (!hr32(c, d + 0x3bau)) return 0;
    uint32_t s = hr32(c, VA_SURFACE);
    if (page_select(c, s, 0x3cu, hr32(c, s + 0x8cu), VA_PAGE_B, VA_DRV_F0)) return 1;
    if (HCALL(c, VA_GC_SELECT, NULL, hr32(c, d + 0x281u))) return 1;
    if (HCALL0(c, hr32(c, d + 0x3bau), NULL)) return 1;
    return HCALL(c, VA_GC_SELECT, NULL, d + 0x48u);
}
/* 0x45719c(d) (listing 0x45719c..0x45720e): the options' step: the quit
 * word, every gadget's tick, the first pending label refresh (bytes
 * d+0x3be..+0x3c6, counted down), the release list, the present. */
static int options_step(hud_ctx *c, uint32_t d) {
    if (hr16(c, VA_QUIT)) hw8(c, d + 0x2d1u, 1);
    for (int32_t k = 0; k < count_of(c, d); ++k) {
        uint32_t g = gadget(c, d, (uint32_t)k);
        if (gadget_call(c, g, 0xcu, (const uint32_t[]){g}, 1)) return 1;
    }
    for (uint32_t k = 0; k < 9u; ++k) {
        if (!hr8(c, d + 0x3beu + k)) continue;
        if (options_label(c, d, k)) return 1;
        hw8(c, d + 0x3beu + k, hr8(c, d + 0x3beu + k) - 1u);
        break;
    }
    if (release_tick(c, hr32(c, d + 0x285u))) return 1;
    es2_sim_present_site(0x0045720au);   /* the call at 0x457205 */
    return HCALL0(c, VA_PRESENT, NULL);
}
/* 0x456d68(d) (listing 0x456d68..0x4570f8): the options dialog's modal loop:
 * without a sound object the first four gadgets start in state 2; the open,
 * focus 0, the page +0x8c with the background clear 0x487e60 (bracketed by the
 * driver slots when the presents are not due), the page +0x88; per pass the
 * control record, the key handler and the pending option: 0..8 toggle or cycle
 * a pref and schedule the label, 9 the joystick page (0x457036..0x457093), 10
 * stores the prefs and ends with 3; the page hook and the step; the close; EAX
 * the end state's bit 0. */
static int joystick_page(hud_ctx *c, uint32_t d);
static int options_run(hud_ctx *c, uint32_t d, uint32_t *eax) {
    if (!hr32(c, VA_SOUND))
        for (uint32_t k = 0; k < 4u; ++k) hw16(c, gadget(c, d, k) + 0x1bu, 2);
    hw32(c, d + 0x356u, 0xffffffffu);
    if (dialog_open(c, d) || focus_set(c, d, 0)) return 1;
    uint32_t s = hr32(c, VA_SURFACE);
    if (page_select(c, s, 0x3cu, hr32(c, s + 0x8cu), VA_PAGE_B, VA_DRV_F0)) return 1;
    uint32_t top = c->l->stack_hi, pt = hud_frame_push(c, 8u);
    if (!pt) return hud_refuse(c, 0x00456d68u, "options", "the own code's call area is exhausted");
    if (!hr16(c, VA_PRESENT_ON) && slot_call(c, VA_DRV_28, NULL, 0)) return 1;
    if (HCALL(c, VA_RECT_CLEAR, NULL, d + 0x18u, pt)) return 1;
    if (!hr16(c, VA_PRESENT_ON) && slot_call(c, VA_DRV_24, NULL, 0)) return 1;
    hud_frame_pop(c, top);
    if (page_select(c, s, 0x3cu, hr32(c, s + 0x88u), VA_PAGE_B, VA_DRV_F0)) return 1;
    while (!hr8(c, d + 0x2d1u)) {
        if (pass(c, 0x00456d68u)) return 1;
        uint32_t r = 0;
        if (HCALL0(c, VA_CONTROL_RECORD, &r)) return 1;
        if (HVCALL(c, d, 0x10u, NULL, d, r)) return 1;
        uint32_t a = hr32(c, d + 0x356u);
        if (a != 0xffffffffu) {
            uint32_t v = 0, back = hr8(c, d + 0x2ffu);
            int bad = 0;
            switch (a) {
            case 0: bad = prefs_toggle(c, 0, 1, &v); hw8(c, d + 0x3beu, 2); break;
            case 1: bad = prefs_toggle(c, 1, 1, &v); hw8(c, d + 0x3bfu, 2); break;
            case 2: if (hr8(c, VA_OPT_FLAG)) { bad = prefs_cycle(c, 2, 3, 1, &v); hw8(c, d + 0x3c0u, 2); } break;
            case 3: if (hr8(c, VA_OPT_FLAG)) { bad = prefs_cycle(c, 3, 3, 1, &v); hw8(c, d + 0x3c1u, 2); } break;
            case 4: bad = back ? prefs_cycle_back(c, 7, 3, 1, &v) : prefs_cycle(c, 7, 3, 1, &v); hw8(c, d + 0x3c2u, 2); break;
            case 5: bad = prefs_toggle(c, 8, 1, &v); hw8(c, d + 0x3c3u, 2); break;
            case 6: bad = back ? prefs_cycle_back(c, 9, 5, 1, &v) : prefs_cycle(c, 9, 5, 1, &v); hw8(c, d + 0x3c4u, 2); break;
            case 7: bad = prefs_cycle(c, 0xa, 3, 1, &v); hw8(c, d + 0x3c5u, 2); break;   /* both directions cycle forward */
            case 8: bad = prefs_cycle(c, 0xb, 3, 1, &v); hw8(c, d + 0x3c6u, 2); break;
            case 9: bad = joystick_page(c, d); break;
            case 10: bad = options_store(c); hw8(c, d + 0x2d1u, 3); break;
            default: break;
            }
            if (bad) return 1;
            hw32(c, d + 0x356u, 0xffffffffu);
        }
        if (options_page(c, d) || options_step(c, d)) return 1;
    }
    if (dialog_close(c, d)) return 1;
    *eax = hr8(c, d + 0x2d1u) & 1u;
    return 0;
}

/* ---- the service rows */
#define ROW(name) void es2_sim_service_##name(es2_sim_service_call *sc, void *user)
#define ROW_BEGIN (void)user; HSERVE(sc, c); uint32_t eax = 0; (void)eax
#define ROW_END(expr) do { if (expr) { hud_failed(sc); return; } sc->eax = eax; } while (0)

ROW(present_time) { ROW_BEGIN; ROW_END(present_time(&c, &eax)); }
ROW(dialog_step) { ROW_BEGIN; ROW_END(dialog_step(&c, sc->slots[0])); }
ROW(dialog_open) { ROW_BEGIN; ROW_END(dialog_open(&c, sc->slots[0])); }
ROW(dialog_close) { ROW_BEGIN; ROW_END(dialog_close(&c, sc->slots[0])); }
ROW(cursor_restore) { ROW_BEGIN; ROW_END(0); }
ROW(cursor_save) { ROW_BEGIN; cursor_save(&c, sc->slots[0], sc->slots[1]); ROW_END(0); }
ROW(dialog_label) { ROW_BEGIN; ROW_END(dialog_label(&c, sc->slots[0], sc->slots[1])); }
ROW(focus_set) { ROW_BEGIN; ROW_END(focus_set(&c, sc->slots[0], sc->slots[1])); }
ROW(focus_next) { ROW_BEGIN; ROW_END(focus_next(&c, sc->slots[0])); }
ROW(focus_previous) { ROW_BEGIN; ROW_END(focus_previous(&c, sc->slots[0])); }
ROW(gadget_press) { ROW_BEGIN; ROW_END(gadget_press(&c, sc->slots[0], sc->slots[1], sc->slots[2])); }
ROW(dialog_keys) { ROW_BEGIN; ROW_END(dialog_keys(&c, sc->slots[0], sc->slots[1], &eax)); }
ROW(gadget_tick) { ROW_BEGIN; ROW_END(gadget_tick(&c, sc->slots[0])); }
ROW(gadget_fire) { ROW_BEGIN; ROW_END(gadget_fire(&c, sc->slots[0], sc->slots[1])); }
ROW(alert_run) { ROW_BEGIN; ROW_END(alert_run(&c, sc->slots[0], 1, &eax)); }
ROW(alert_run_plain) { ROW_BEGIN; ROW_END(alert_run(&c, sc->slots[0], 0, &eax)); }
ROW(alert_select) { ROW_BEGIN; ROW_END(alert_select(&c, sc->slots[0], sc->slots[1], 1)); }
ROW(confirm_select) { ROW_BEGIN; ROW_END(alert_select(&c, sc->slots[0], sc->slots[1], 0)); }
/* 0x4561c8(d, i): [[d+0x336]+4i]; 0x457c88(d): [d+0x346]; 0x457458(d, i):
 * [d+0x362+4i]; 0x45746c(d, f): d+0x3ba <- f. */
ROW(alert_text) { ROW_BEGIN; eax = hr32(&c, hr32(&c, sc->slots[0] + 0x336u) + 4u * sc->slots[1]); ROW_END(0); }
ROW(confirm_text) { ROW_BEGIN; eax = hr32(&c, sc->slots[0] + 0x346u); ROW_END(0); }
ROW(options_text) { ROW_BEGIN; eax = hr32(&c, sc->slots[0] + 0x362u + 4u * sc->slots[1]); ROW_END(0); }
ROW(options_hook) { ROW_BEGIN; hw32(&c, sc->slots[0] + 0x3bau, sc->slots[1]); ROW_END(0); }
ROW(release_tick) { ROW_BEGIN; ROW_END(release_tick(&c, sc->slots[0])); }
ROW(release_count) { ROW_BEGIN; eax = release_count(&c, sc->slots[0]); ROW_END(0); }
ROW(port_mark) { ROW_BEGIN; port_mark(&c, sc->slots[0], sc->slots[1]); ROW_END(0); }
ROW(port_pause) { ROW_BEGIN; ROW_END(port_pause(&c, sc->slots[0])); }
ROW(port_resume) { ROW_BEGIN; ROW_END(port_resume(&c, sc->slots[0])); }
ROW(port_shift) { ROW_BEGIN; ROW_END(port_shift(&c, sc->slots[0], sc->slots[1])); }
ROW(key_flag) { ROW_BEGIN; key_flag(&c, sc->slots[0], sc->slots[1]); ROW_END(0); }
ROW(device_current) { ROW_BEGIN; eax = device_current(&c); ROW_END(0); }
ROW(panel_last) { ROW_BEGIN; panel_last(&c, sc->slots[0]); ROW_END(0); }
ROW(panel_free) { ROW_BEGIN; ROW_END(panel_free(&c, sc->slots[0])); }
ROW(options_run) { ROW_BEGIN; ROW_END(options_run(&c, sc->slots[0], &eax)); }
ROW(options_step) { ROW_BEGIN; ROW_END(options_step(&c, sc->slots[0])); }
ROW(options_label) { ROW_BEGIN; ROW_END(options_label(&c, sc->slots[0], sc->slots[1])); }
ROW(options_select) { ROW_BEGIN; ROW_END(options_select(&c, sc->slots[0], sc->slots[1], sc->slots[2])); }
ROW(options_page) { ROW_BEGIN; ROW_END(options_page(&c, sc->slots[0])); }
ROW(options_store) { ROW_BEGIN; ROW_END(options_store(&c)); }
ROW(prefs_toggle) { ROW_BEGIN; ROW_END(prefs_toggle(&c, sc->slots[0], sc->slots[1], &eax)); }
ROW(prefs_cycle) { ROW_BEGIN; ROW_END(prefs_cycle(&c, sc->slots[0], sc->slots[1], sc->slots[2], &eax)); }
ROW(prefs_cycle_back) { ROW_BEGIN; ROW_END(prefs_cycle_back(&c, sc->slots[0], sc->slots[1], sc->slots[2], &eax)); }

/* ==== the dialog constructors, destructors and draws, the F12/Alt+P options
 * session (main_loop.md, "dialog constructors and the F12/Alt+P session").
 * Every function below is the original's at its VA, read from the listing;
 * guest state through the guest view,
 * every call of other DBSIM code through the dispatch. The SEH frames
 * (0x49630c, stack locals only) are not reproduced; the RTL counter bumps are.
 * The original's stack locals that are passed by address (rectangles, points,
 * the view record) live in a frame of the call area; their addresses differ
 * from the original's, their contents do not. Where the original writes through
 * a NULL pointer (a failed new of a gadget, a NULL options dialog) the own code
 * refuses. */
#define VA_AXIS_X 0x004d253au          /* words: the resolution shifts */
#define VA_AXIS_Y 0x004d253cu
#define VA_DLG_RECT 0x004d1e88u        /* the rectangle 0x454f14 returns */
#define VA_DLG_RECT_ONCE 0x0049ddfcu
#define VA_SCREEN_W 0x004d1e70u
#define VA_SCREEN_H 0x004d1e74u
#define VA_HIRES 0x004d256cu           /* byte */
#define VA_FONT_TITLE 0x004d1e18u
#define VA_FONT_BUTTON 0x004d1e1cu
#define VA_FONT_TEXT 0x004d1e2cu
#define VA_BUTTON_IMAGES 0x0049dd88u   /* -> a bitmap set: +8 the image table */
#define VA_OPTIONS_BITMAP 0x004d1efcu
#define VA_SCROLL_X 0x004d2556u
#define VA_SCROLL_Y 0x004d255au
#define VA_SHELL 0x004d24bcu           /* the shell record G */
#define VA_DISPLAY 0x004cf99cu
#define VA_VIEW_RECT 0x004cfa40u       /* 0x42d89c's rectangle and origin (+0x10, +0x14) */
#define VA_VIEW_RECT_SAVED 0x004cfa28u
#define VA_VIEW_RECT_COPY 0x004cfca4u
#define VA_SESSION_FLAG 0x0049b104u    /* byte */
#define VA_SESSION_WORD 0x004d267eu
#define VA_VIEWS 0x004d24eau
/* Callees. */
#define VA_TEXT_LOAD 0x00453878u
#define VA_TEXT_SPLIT 0x004539a0u
#define VA_LANGUAGE_PATH 0x0045ef1cu
#define VA_GADGET_BASE 0x004524a0u
#define VA_PANEL_CTOR 0x004526ecu
#define VA_PANEL_ADD 0x00452c6cu
#define VA_LABEL_CTOR 0x004387acu
#define VA_LABEL_PLACE 0x00438884u
#define VA_BLIT_IMAGE 0x0045c8bcu
#define VA_TEXT_WIDTH 0x004824ecu
#define VA_SURFACE_SIZE 0x00488045u
#define VA_HOLDER_FREE 0x004838fdu
#define VA_BITMAP_FREE 0x0048349cu
#define VA_TEXT_NONEMPTY 0x00496766u
#define VA_RECT_CTOR 0x0048de2fu
#define VA_VIEW_CTOR 0x0048bd84u
#define VA_VIEW_DTOR 0x0048bcc4u
#define VA_CAMERA_ENTRY 0x00429820u
#define VA_CAMERA_RESET 0x0042da08u
#define VA_CAMERA_MODE 0x0042a1f0u
#define VA_DISPLAY_REQUEST 0x0042a3f4u
#define VA_SUBJECT_LABEL 0x0045e208u
#define VA_FRAME_INPUT 0x0045fba4u
#define VA_CAMERA_UPDATE 0x004011a0u
#define VA_RENDER_ROOT 0x0045fca8u
#define VA_VIEW_OFFSETS 0x0045edb0u
#define VA_OUTSIDE_CAMERA 0x00401c74u
#define VA_OPTIONS_HOOK 0x0045746cu
#define VA_OPTIONS_IDLE 0x0045cfc8u

static uint32_t d13_wsx(hud_ctx *c, uint32_t a) { return hsx16(hr16(c, a)); }
/* (int16)(v << axis): the 16-bit shl by the axis word's low five bits. */
static uint32_t d13_shs(hud_ctx *c, uint32_t v, uint32_t axis) { return hsx16((uint16_t)(v << (hr16(c, axis) & 31u))); }

/* The constructors' once tables: at the first construction each word is
 * its value shifted by its axis, then the guard byte is set. */
typedef struct d13_once { uint32_t va; uint16_t v; uint8_t y; } d13_once;
static void d13_once_fill(hud_ctx *c, uint32_t guard, const d13_once *t, unsigned n) {
    if (hr8(c, guard)) return;
    for (unsigned i = 0; i < n; ++i) hw16(c, t[i].va, (uint16_t)(t[i].v << (hr16(c, t[i].y ? VA_AXIS_Y : VA_AXIS_X) & 31u)));
    hw8(c, guard, 1);
}
/* 0x455950 (listing 0x4559e5..0x455b42). */
static const d13_once d13_alert_once[] = {
    {0x4d1e9cu, 0, 0}, {0x4d1e9eu, 0, 1}, {0x4d1ea0u, 8, 1}, {0x4d1ea2u, 0, 1}, {0x4d1ea4u, 0x50, 0}, {0x4d1ea6u, 0x29, 0},
    {0x4d1ea8u, 0x78, 0}, {0x4d1eaau, 0x5d, 1}, {0x4d1eacu, 0x50, 1}, {0x4d1eaeu, 0x3d, 0}, {0x4d1eb0u, 9, 1}, {0x4d1eb2u, 9, 1},
    {0x4d1eb4u, 0, 1}, {0x4d1eb6u, 0x32, 0}, {0x4d1eb8u, 0x1e, 1}, {0x4d1ebau, 0x7a, 0}, {0x4d1ebcu, 0xa, 1}, {0x4d1ebeu, 0xa, 1},
};
/* 0x4561dc (listing 0x456272..0x45636e). */
static const d13_once d13_pause_once[] = {
    {0x4d1ec4u, 0, 0}, {0x4d1ec6u, 0, 1}, {0x4d1ec8u, 8, 1}, {0x4d1ecau, 0, 1}, {0x4d1eccu, 0xe, 0}, {0x4d1eceu, 0xc, 0},
    {0x4d1ed0u, 0xe, 0}, {0x4d1ed2u, 8, 0}, {0x4d1ed4u, 0xe, 0}, {0x4d1ed6u, 0x15, 0}, {0x4d1ed8u, 0x3d, 0}, {0x4d1edau, 9, 1},
    {0x4d1edcu, 0, 1},
};
/* 0x4566e0 (listing 0x456758..0x45681a). */
static const d13_once d13_options_once[] = {
    {0x4d1ee0u, 0x13f, 0}, {0x4d1ee2u, 0x59, 1}, {0x4d1ee4u, 0, 0}, {0x4d1ee6u, 0x96, 1}, {0x4d1ee8u, 0, 0},
    {0x4d1eeau, 0, 1}, {0x4d1eecu, 8, 1}, {0x4d1eeeu, 0, 1}, {0x4d1ef6u, 2, 0}, {0x4d1ef8u, 0, 1},
};
/* 0x457538 (listing 0x4575d7..0x457720). */
static const d13_once d13_objectives_once[] = {
    {0x4d1f00u, 0x13b, 0}, {0x4d1f02u, 0x8b, 1}, {0x4d1f04u, 0, 0}, {0x4d1f06u, 0, 1}, {0x4d1f08u, 8, 1}, {0x4d1f0au, 0, 1},
    {0x4d1f0cu, 0x7f, 0}, {0x4d1f0eu, 0x5d, 1}, {0x4d1f10u, 0x3c, 0}, {0x4d1f12u, 0xa, 1}, {0x4d1f14u, 0, 1}, {0x4d1f16u, 0x14, 0},
    {0x4d1f18u, 0xa, 1}, {0x4d1f1au, 0x14, 0}, {0x4d1f1cu, 0x11, 1}, {0x4d1f1eu, 0xb4, 0}, {0x4d1f20u, 0xa, 1},
};

/* ---- 0x453824(h) (listing 0x453824..0x453847): the text block's three
 * dwords and two words cleared, counter +1. */
static void text_block_init(hud_ctx *c, uint32_t h) {
    hw32(c, h + 4u, 0);
    hw32(c, h, 0);
    hw32(c, h + 8u, 0);
    hw16(c, h + 0xeu, 0);
    hw16(c, h + 0xcu, 0);
    hud_counter(c, 1);
}
/* ---- 0x453848(h, f) (listing 0x453848..0x453876): counter -1; the
 * buffer [h] freed (delete[]), h itself with f bit 0. */
static int text_block_free(hud_ctx *c, uint32_t h, uint32_t f) {
    hud_counter(c, -1);
    if (!h) return 0;
    uint32_t b = hr32(c, h);
    if (b && HCALL(c, HUD_VA_DELETE_ARRAY, NULL, b)) return 1;
    return (f & 1u) ? HCALL(c, HUD_VA_DELETE, NULL, h) : 0;
}
/* ---- 0x454f14(x, y, w, h) (listing 0x454f14..0x454f4e): the once byte
 * 0x49ddfc raised from 0; [0x4d1e88] <- {x, y, x+w, y+h}. EAX the
 * rectangle, EDX y+h, ECX unchanged. */
static uint32_t dlg_rect(hud_ctx *c, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t *edx) {
    if (!hr8(c, VA_DLG_RECT_ONCE)) hw8(c, VA_DLG_RECT_ONCE, 1);
    hw32(c, VA_DLG_RECT, x);
    hw32(c, VA_DLG_RECT + 4u, y);
    hw32(c, VA_DLG_RECT + 8u, x + w);
    hw32(c, VA_DLG_RECT + 0xcu, y + h);
    if (edx) *edx = y + h;
    return VA_DLG_RECT;
}
/* ---- 0x454f50(w, h) (listing 0x454f50..0x454f7c): centred on the screen
 * size [0x4d1e70]/[0x4d1e74] (sar). ECX the x. */
static uint32_t dlg_rect_centred(hud_ctx *c, uint32_t w, uint32_t h, uint32_t *ecx, uint32_t *edx) {
    uint32_t x = (uint32_t)((int32_t)(hr32(c, VA_SCREEN_W) - w) >> 1);
    uint32_t y = (uint32_t)((int32_t)(hr32(c, VA_SCREEN_H) - h) >> 1);
    if (ecx) *ecx = x;
    return dlg_rect(c, x, y, w, h, edx);
}
/* The constructors' rectangle: the shifted size at the position [small]
 * (0x454f14) or centred (0x454f50). */
static uint32_t ctor_rect(hud_ctx *c, uint32_t small, uint32_t wv, uint32_t hv) {
    uint32_t h = d13_shs(c, hv, VA_AXIS_Y), w = d13_shs(c, wv, VA_AXIS_X);
    if (small) return dlg_rect(c, hr32(c, small), hr32(c, small + 4u), w, h, NULL);
    return dlg_rect_centred(c, w, h, NULL, NULL);
}

/* ---- 0x48358c(P) (listing 0x48358c..0x4835c6, called by 0x4541b8 only
 * among the own rows): the holder's tables, the empty constructor at P+8,
 * P+4 cleared, counter +2. */
static int holder_init(hud_ctx *c, uint32_t p) {
    hw32(c, p, 0x004a69f0u);
    hw32(c, p, 0x004a3368u);
    hud_counter(c, 1);
    hw32(c, p, 0x004a3340u);
    if (HCALL(c, HUD_VA_EMPTY_CTOR, NULL, p + 8u)) return 1;
    hw32(c, p + 4u, 0);
    hud_counter(c, 1);
    return 0;
}
/* The RTTI count and the virtual delete (+0x18, flag 3) of 0x4543b0's and
 * 0x4835c8's owned object q. */
static int owned_free(hud_ctx *c, uint32_t q) {
    uint32_t n = 0;
    if (HCALL(c, HUD_VA_RTTI_COUNT, &n, q, 0)) return 1;
    hud_counter(c, (int32_t)n);
    return HVCALL(c, q, 0x18u, NULL, q, 3);
}
/* ---- 0x4835c8(P, f) (listing 0x4835c8..0x483646): counter -1; the table,
 * the owned object P+4, 0x4838fd(P, 0), P itself with f bit 0. */
static int holder_free(hud_ctx *c, uint32_t p, uint32_t f) {
    hud_counter(c, -1);
    if (!p) return 0;
    hw32(c, p, 0x004a3340u);
    uint32_t q = hr32(c, p + 4u);
    if (q && owned_free(c, q)) return 1;
    if (HCALL(c, VA_HOLDER_FREE, NULL, p, 0)) return 1;
    return (f & 1u) ? HCALL(c, HUD_VA_DELETE, NULL, p) : 0;
}

/* ---- 0x4541b8(o, save, rect) (listing 0x4541b8..0x4543af): the dialog
 * base: tables, the text block o+0x34, the graphics context o+0x48
 * (0x480c34), the holder o+0x2e2, the rectangle o+4 (and its copy
 * o+0x258), the inner size o+0x1c, with save's low byte the save buffer
 * (0x488045's size, new[]), the panel o+0x285 (new 0x398, 0x4526ec), the
 * page o+0x28d, default -1. */
static int dlg_ctor(hud_ctx *c, uint32_t o, uint32_t save, uint32_t rect) {
    uint32_t e = o + 0x14u;
    hw32(c, o, 0x0049e034u);
    hw32(c, e, 0x004a69f0u);
    hw32(c, e, 0x004a3368u);
    hud_counter(c, 1);
    hw32(c, e, 0x004a3390u);
    hud_counter(c, 1);
    hw32(c, e, 0x004a3264u);
    hw32(c, e + 4u, 0);
    hud_counter(c, 1);
    text_block_init(c, o + 0x34u);
    if (hud_gc_construct(c, o + 0x48u) || holder_init(c, o + 0x2e2u)) return 1;
    hw32(c, o + 0x2cdu, 0);
    hw8(c, o + 0x2d1u, 0);
    for (uint32_t i = 0; i < 0x10u; i += 4u) hw32(c, o + 4u + i, hr32(c, rect + i));
    for (uint32_t i = 0; i < 0x10u; i += 4u) hw32(c, o + 0x258u + i, hr32(c, o + 4u + i));
    hw32(c, o + 0x26cu, 0);
    hw32(c, o + 0x268u, 0);
    hw32(c, o + 0x254u, 1);
    hw32(c, o + 0x250u, 1);
    hw32(c, o + 0x274u, 0x004d1e78u);
    hw32(c, o + 0x20u, 0);
    hw32(c, o + 0x1cu, 0);
    hw32(c, o + 0x24u, hr32(c, rect + 8u));
    hw32(c, o + 0x28u, hr32(c, rect + 0xcu));
    hw32(c, o + 0x24u, hr32(c, o + 0x24u) - hr32(c, rect));
    hw32(c, o + 0x28u, hr32(c, o + 0x28u) - hr32(c, rect + 4u));
    hw32(c, o + 0x2cu, 0);
    if (save & 0xffu) {
        uint32_t n = 0, b = 0;
        if (HCALL(c, VA_SURFACE_SIZE, &n, o + 0x18u) || HCALL(c, HUD_VA_NEW_ARRAY, &b, n)) return 1;
        hw32(c, o + 0x2cu, b);
    }
    hw32(c, o + 0x30u, 0);
    hw32(c, o + 0x289u, hr32(c, VA_LAST_PANEL));
    uint32_t p = 0;
    if (HCALL(c, HUD_VA_NEW, &p, 0x398u)) return 1;
    if (p) {
        if (HCALL(c, VA_PANEL_CTOR, NULL, p, o + 4u)) return 1;
        hud_counter(c, -3);
    }
    hw32(c, o + 0x285u, p);
    hw32(c, o + 0x28du, hr32(c, hr32(c, VA_SURFACE) + 0x3cu));
    hw32(c, o + 0x2fbu, 0xffffffffu);
    hw8(c, o + 0x330u, 1);
    hw8(c, o + 0x331u, 0);
    hw32(c, VA_PRESENT_PERIOD, 1);
    hud_counter(c, 1);
    return 0;
}
/* ---- 0x4543b0(o, f) (listing 0x4543b0..0x4544bd): counter -1; the
 * gadgets deleted, the save buffer (delete[]), the owned bitmap o+0x30
 * (RTTI count, virtual delete), the title label o+0x44, the holder
 * (0x4835c8 with 2), the text block (0x453848 with 2), counters -2 and -3,
 * o itself with f bit 0. */
static int dlg_dtor(hud_ctx *c, uint32_t o, uint32_t f) {
    hud_counter(c, -1);
    if (!o) return 0;
    hw32(c, o, 0x0049e034u);
    for (int32_t i = 0; i < count_of(c, o); ++i)
        if (HCALL(c, HUD_VA_DELETE, NULL, gadget(c, o, (uint32_t)i))) return 1;
    uint32_t b = hr32(c, o + 0x2cu);
    if (b && HCALL(c, HUD_VA_DELETE_ARRAY, NULL, b)) return 1;
    uint32_t q = hr32(c, o + 0x30u);
    if (q && owned_free(c, q)) return 1;
    if (HCALL(c, HUD_VA_DELETE, NULL, hr32(c, o + 0x44u))) return 1;
    hw32(c, o + 0x2e6u, 0);
    if (holder_free(c, o + 0x2e2u, 2)) return 1;
    hud_counter(c, -2);
    if (text_block_free(c, o + 0x34u, 2)) return 1;
    hud_counter(c, -3);
    return (f & 1u) ? HCALL(c, HUD_VA_DELETE, NULL, o) : 0;
}

/* ---- 0x454f80(g, rect, owner, idx) (listing 0x454f80..0x455012): the
 * button: the gadget base 0x4524a0, its tables, owner and index, state 0,
 * the label colours [0x4d1e20], [0x4d1e24], [0x4d1e28], [0x4d1e20],
 * counter +2. */
static int button_init(hud_ctx *c, uint32_t g, uint32_t rect, uint32_t owner, uint32_t idx) {
    if (HCALL(c, VA_GADGET_BASE, NULL, g, rect)) return 1;
    hw32(c, g + 0x17u, 0x0049defcu);
    hw16(c, g + 0x1du, 0);
    hw32(c, g + 0x20u, 0x0049dea0u);
    hw32(c, g + 0x24u, owner);
    hw32(c, g + 0x17u, 0x0049debcu);
    hw32(c, g + 0x20u, 0x0049dee0u);
    hw32(c, g + 0x28u, idx);
    hw16(c, g + 0x1bu, 0);
    hud_counter(c, 1);
    hw32(c, g + 0x17u, 0x0049dff4u);
    hw32(c, g + 0x20u, 0x0049e018u);
    hw32(c, g + 0x40u, hr32(c, 0x004d1e20u));
    hw32(c, g + 0x44u, hr32(c, 0x004d1e24u));
    hw32(c, g + 0x48u, hr32(c, 0x004d1e28u));
    hw32(c, g + 0x4cu, hr32(c, 0x004d1e20u));
    hud_counter(c, 1);
    return 0;
}
/* ---- 0x455014(g) (listing 0x455014..0x45507d): the button draw: the
 * state's image (0x45c8bc), the owner's text for the index (vtable +0x14),
 * the state's colour into the label, the label draw 0x438920; bracketed by
 * the driver slots when the presents are not due. */
static int button_draw(hud_ctx *c, uint32_t g) {
    if (!hr16(c, VA_PRESENT_ON) && slot_call(c, VA_DRV_28, NULL, 0)) return 1;
    uint32_t s = d13_wsx(c, g + 0x1bu);
    if (HCALL(c, VA_BLIT_IMAGE, NULL, VA_SHELL, hr32(c, g + 0x30u + 4u * s), g)) return 1;
    uint32_t owner = hr32(c, g + 0x24u), t = 0;
    if (HVCALL(c, owner, 0x14u, &t, owner, hr32(c, g + 0x28u))) return 1;
    uint32_t l = hr32(c, g + 0x2cu);
    hw32(c, l, hr32(c, g + 0x40u + 4u * d13_wsx(c, g + 0x1bu)));
    if (HCALL(c, VA_LABEL_DRAW, NULL, l, t)) return 1;
    if (!hr16(c, VA_PRESENT_ON) && slot_call(c, VA_DRV_24, NULL, 0)) return 1;
    return 0;
}

/* The constructors' pieces. new(0x21) and 0x4387ac(l, font, 0). */
static int label_new(hud_ctx *c, uint32_t font, uint32_t *out) {
    uint32_t l = 0;
    if (HCALL(c, HUD_VA_NEW, &l, 0x21u)) return 1;
    if (l && HCALL(c, VA_LABEL_CTOR, NULL, l, font, 0)) return 1;
    *out = l;
    return 0;
}
/* new(0x50), 0x454f80 (counter -2), the label placed in rect at pt
 * (0x438884, mode 2), g+0x2c <- the label. */
static int button_new(hud_ctx *c, uint32_t o, uint32_t k, uint32_t rect, uint32_t pt, uint32_t font, uint32_t *out) {
    uint32_t g = 0, l = 0;
    if (HCALL(c, HUD_VA_NEW, &g, 0x50u)) return 1;
    if (g) {
        if (button_init(c, g, rect, o, k)) return 1;
        hud_counter(c, -2);
    }
    if (label_new(c, font, &l) || HCALL(c, VA_LABEL_PLACE, NULL, l, pt, rect, 2)) return 1;
    if (!g) return hud_null_refuse(c, 0x0047749cu, "dialog button", "new returned NULL (the original writes through it)");
    hw32(c, g + 0x2cu, l);
    *out = g;
    return 0;
}
/* The texts: 0x45ef1c(name, ext), 0x453878(o+0x34, that), then per entry
 * 0x4539a0(o+0x34, o+off, count, 0). */
typedef struct d13_text { uint16_t off, count; } d13_text;
static int dlg_texts(hud_ctx *c, uint32_t o, uint32_t name, uint32_t ext, const d13_text *t, unsigned n) {
    uint32_t path = 0;
    if (HCALL(c, VA_LANGUAGE_PATH, &path, name, ext) || HCALL(c, VA_TEXT_LOAD, NULL, o + 0x34u, path)) return 1;
    for (unsigned i = 0; i < n; ++i)
        if (HCALL(c, VA_TEXT_SPLIT, NULL, o + 0x34u, o + t[i].off, t[i].count, 0)) return 1;
    return 0;
}
static const d13_text d13_alert_texts[] = {{0x34e, 0x14}, {0x39e, 0x28}, {0x43e, 0x50}};
/* The background bitmap: 0x492bec(dir, hires ? a : b), 0x45cdf4(path). */
static int dlg_bitmap(hud_ctx *c, uint32_t dir, uint32_t hires, uint32_t lores, uint32_t *b) {
    uint32_t path = 0;
    if (HCALL(c, HUD_VA_PATH_DIR, &path, dir, hr8(c, VA_HIRES) ? hires : lores)) return 1;
    return HCALL(c, HUD_VA_LOAD_BY_NAME, b, path);
}
/* The title: a label in [0x4d1e18]'s font at o+0x44, centred over the
 * inner width (0x4824ec's text width plus the pad word) into o+0x2d2..
 * +0x2de, placed (mode 2) at {0, words+6} in that rectangle or in a local
 * copy of it (the objectives). */
static int dlg_title(hud_ctx *c, uint32_t o, uint32_t words, uint32_t text, uint32_t pt, uint32_t local) {
    uint32_t font = hr32(c, VA_FONT_TITLE), l = 0, w = 0;
    if (label_new(c, font, &l)) return 1;
    hw32(c, o + 0x44u, l);
    uint32_t pad = d13_wsx(c, words), span = hr32(c, o + 0x24u) - hr32(c, o + 0x1cu);
    if (HCALL(c, VA_TEXT_WIDTH, &w, font, hr32(c, o + text))) return 1;
    hw32(c, o + 0x2d2u, (uint32_t)((int32_t)(span - w - pad) >> 1));
    hw32(c, o + 0x2d6u, d13_wsx(c, words + 2u));
    hw32(c, o + 0x2dau, pad + w + hr32(c, o + 0x2d2u));
    hw32(c, o + 0x2deu, d13_wsx(c, words + 4u) + hr32(c, o + 0x2d6u));
    uint32_t rect = o + 0x2d2u;
    if (local) {
        for (uint32_t i = 0; i < 0x10u; i += 4u) hw32(c, local + i, hr32(c, o + 0x2d2u + i));
        rect = local;
    }
    hw32(c, pt, 0);
    hw32(c, pt + 4u, d13_wsx(c, words + 6u));
    return HCALL(c, VA_LABEL_PLACE, NULL, hr32(c, o + 0x44u), pt, rect, 2);
}
/* The buttons' common tail: the panel add 0x452c6c and the slot. */
static int button_add(hud_ctx *c, uint32_t o, uint32_t g, uint32_t slot) {
    if (HCALL(c, VA_PANEL_ADD, NULL, hr32(c, o + 0x285u), g)) return 1;
    hw32(c, slot, g);
    return 0;
}
/* The alert's and the pause dialog's status texts. */
static void alert_texts(hud_ctx *c, uint32_t o, uint32_t s) {
    hw32(c, o + 0x332u, hr32(c, o + 0x34eu + 4u * s));
    hw32(c, o + 0x336u, o + 0x39eu + 8u * s);
    hw32(c, o + 0x33au, o + 0x43eu + 16u * s);
}
/* A two-rectangle array (0x49667c with the rectangle constructor). */
static int rect_pair(hud_ctx *c, uint32_t r) { return HCALL(c, HUD_VA_VECTOR_NEW, NULL, r, 0x10u, 2, 1, VA_RECT_CTOR); }

/* ---- 0x455924(o, status, save, rect) (listing 0x455924..0x45594e): the
 * dialog base with the alert's table, counter +1. */
static int alert_base(hud_ctx *c, uint32_t o, uint32_t save, uint32_t rect) {
    if (dlg_ctor(c, o, save, rect)) return 1;
    hw32(c, o, 0x0049e120u);
    hud_counter(c, 1);
    return 0;
}
/* The alert's status classes (the byte table 0x455bd8: 0 no button, 1 two
 * buttons, 2 one). */
static const uint8_t d13_alert_class[20] = {0, 0, 2, 2, 1, 1, 1, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 1};
/* ---- 0x455950(o, status, save, small) (listing 0x455950..0x455faa): the
 * mission-end alert: the base at the shifted 0xde x 0x6d size, the once
 * table, the texts (0x49e0f5/0x49e0fe), the buttons of the status class
 * (bitmap 0x49e102's first image, images 0/1 of [0x49dd88]), the status
 * texts (status 5: [0x4d1e98]), the title, four text lines in
 * [0x4d1e2c]'s font; default 0, status 2 clears o+0x330; counter +1. */
static int alert_ctor(hud_ctx *c, uint32_t o, uint32_t status, uint32_t save, uint32_t small) {
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0x38u);
    if (!f) return hud_refuse(c, 0x00455950u, "alert", "the own code's call area is exhausted");
    uint32_t r = f, pt = f + 0x20u, lr = f + 0x28u, b = 0;
    if (dlg_ctor(c, o, save, ctor_rect(c, small, 0xde, 0x6d))) return 1;
    hw32(c, o, 0x0049e120u);
    d13_once_fill(c, 0x0049e04cu, d13_alert_once, sizeof(d13_alert_once) / sizeof(d13_alert_once[0]));
    if (dlg_texts(c, o, 0x0049e0f5u, 0x0049e0feu, d13_alert_texts, 3)) return 1;
    uint32_t font = hr32(c, VA_FONT_BUTTON), t = 0;
    if (rect_pair(c, r)) return 1;
    if (status <= 0x13u) {
        if (d13_alert_class[status] == 2) {
            hw32(c, o + 0x2cdu, 1);
            hw32(c, r, d13_wsx(c, 0x4d1ea4u));
        } else if (d13_alert_class[status] == 1) {
            hw32(c, o + 0x2cdu, 2);
            hw32(c, r, d13_wsx(c, 0x4d1ea6u));
            hw32(c, r + 0x10u, d13_wsx(c, 0x4d1ea8u));
        }
    }
    if (dlg_bitmap(c, 0x0049e102u, 0x0049e10bu, 0x0049e10fu, &b)) return 1;
    hw32(c, o + 0x30u, hr32(c, hr32(c, b + 8u)));
    hw32(c, pt, 0);
    hw32(c, pt + 4u, d13_wsx(c, 0x4d1eb4u));
    for (int32_t k = 0; k < count_of(c, o); ++k) {
        uint32_t q = r + 0x10u * (uint32_t)k, g = 0;
        hw32(c, q + 8u, hr32(c, q) + d13_wsx(c, 0x4d1eaeu));
        hw32(c, q + 4u, d13_wsx(c, 0x4d1eacu));
        hw32(c, q + 0xcu, d13_wsx(c, 0x4d1eacu) + d13_wsx(c, 0x4d1eb2u));
        if (button_new(c, o, (uint32_t)k, q, pt, font, &g)) return 1;
        hw32(c, g + 0x30u, hr32(c, hr32(c, hr32(c, VA_BUTTON_IMAGES) + 8u) + 4u * t));
        hw32(c, g + 0x34u, hr32(c, hr32(c, hr32(c, VA_BUTTON_IMAGES) + 8u) + 4u * t + 4u));
        if (button_add(c, o, g, o + 0x291u + 4u * (uint32_t)k)) return 1;
    }
    alert_texts(c, o, status);
    if (status == 5u) hw32(c, o + 0x33au, hr32(c, 0x004d1e98u));
    if (dlg_title(c, o, 0x004d1e9cu, 0x332u, pt, 0)) return 1;
    font = hr32(c, VA_FONT_TEXT);
    hw32(c, pt + 4u, 0);
    uint32_t x = d13_wsx(c, 0x4d1eb6u), y = d13_wsx(c, 0x4d1eb8u);
    hw32(c, lr, x);
    hw32(c, lr + 8u, d13_wsx(c, 0x4d1ebau) + x);
    hw32(c, lr + 4u, y);
    hw32(c, lr + 0xcu, d13_wsx(c, 0x4d1ebcu) + y);
    for (uint32_t i = 0; i < 4u; ++i) {
        uint32_t l = 0;
        if (label_new(c, font, &l)) return 1;
        hw32(c, o + 0x33eu + 4u * i, l);
        if (HCALL(c, VA_LABEL_PLACE, NULL, l, pt, lr, 2)) return 1;
        uint32_t d = d13_wsx(c, 0x4d1ebeu);
        add32(c, lr + 4u, d);
        add32(c, lr + 0xcu, d);
    }
    hw32(c, o + 0x2fbu, 0);
    if (status == 2u) hw8(c, o + 0x330u, 0);
    hud_counter(c, 1);
    hud_frame_pop(c, top);
    return 0;
}
/* ---- 0x455fac(o, f) (listing 0x455fac..0x455ffc): counter -1; the four
 * line labels, the base (0x4543b0 with 0), o with f bit 0. */
static int alert_dtor(hud_ctx *c, uint32_t o, uint32_t f) {
    hud_counter(c, -1);
    if (!o) return 0;
    hw32(c, o, 0x0049e120u);
    for (uint32_t i = 0; i < 4u; ++i)
        if (HCALL(c, HUD_VA_DELETE, NULL, hr32(c, o + 0x33eu + 4u * i))) return 1;
    if (dlg_dtor(c, o, 0)) return 1;
    return (f & 1u) ? HCALL(c, HUD_VA_DELETE, NULL, o) : 0;
}
/* The draws' backdrop: the inner rectangle moved by the scroll, the
 * background bitmap o+0x30 (0x45c8bc), moved back, the frame (vtable +4)
 * with the title text. */
static int dlg_backdrop(hud_ctx *c, uint32_t d, uint32_t text) {
    uint32_t dx = 0u - hr32(c, VA_SCROLL_X), dy = 0u - hr32(c, VA_SCROLL_Y);
    add32(c, d + 0x1cu, 0u - dx);
    add32(c, d + 0x20u, 0u - dy);
    if (HCALL(c, VA_BLIT_IMAGE, NULL, VA_SHELL, hr32(c, d + 0x30u), d + 0x1cu)) return 1;
    add32(c, d + 0x1cu, dx);
    add32(c, d + 0x20u, dy);
    return HVCALL(c, d, 4u, NULL, d, hr32(c, d + text));
}
/* Every gadget's draw (its table's +0). */
static int gadgets_draw(hud_ctx *c, uint32_t d) {
    for (int32_t k = 0; k < count_of(c, d); ++k) {
        uint32_t g = gadget(c, d, (uint32_t)k);
        if (gadget_call(c, g, 0, (const uint32_t[]){g}, 1)) return 1;
    }
    return 0;
}
/* ---- 0x456084(d) (listing 0x456084..0x456178): the alert's draw: the
 * backdrop, the leading non-empty status lines (0x496766, at most 4; a
 * single line goes to the second label), the gadgets. */
static int alert_draw(hud_ctx *c, uint32_t d) {
    if (dlg_backdrop(c, d, 0x332u)) return 1;
    uint32_t n = 0;
    for (uint32_t i = 0; i < 4u; ++i) {
        uint32_t t = hr32(c, hr32(c, d + 0x33au) + 4u * i), v = 0;
        if (!t) break;
        if (HCALL(c, VA_TEXT_NONEMPTY, &v, t)) return 1;
        if (!v) break;
        ++n;
    }
    uint16_t s = (uint16_t)(n == 1u ? 1u : 0u);
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t t = hr32(c, hr32(c, d + 0x33au) + 4u * i);
        uint32_t l = hr32(c, d + 0x33eu + 4u * hsx16(s));
        ++s;
        if (HCALL(c, VA_LABEL_DRAW, NULL, l, t)) return 1;
    }
    return gadgets_draw(c, d);
}
/* ---- 0x4561dc(o, status, save, small) (listing 0x4561dc..0x4566de): the
 * pause dialog: the alert base at the shifted 0x59 x 0x22 size, its once
 * table, the texts (0x49e1b9/0x49e1c2), status 0 one button, status 1 two
 * (bitmap 0x49e1c6's second image), the status texts, the title; default
 * 0, counter +1. */
static int pause_ctor(hud_ctx *c, uint32_t o, uint32_t status, uint32_t save, uint32_t small) {
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0x28u);
    if (!f) return hud_refuse(c, 0x004561dcu, "pause dialog", "the own code's call area is exhausted");
    uint32_t r = f, pt = f + 0x20u, b = 0;
    if (alert_base(c, o, save, ctor_rect(c, small, 0x59, 0x22))) return 1;
    hw32(c, o, 0x0049e1e4u);
    d13_once_fill(c, 0x0049e138u, d13_pause_once, sizeof(d13_pause_once) / sizeof(d13_pause_once[0]));
    if (dlg_texts(c, o, 0x0049e1b9u, 0x0049e1c2u, d13_alert_texts, 3)) return 1;
    uint32_t font = hr32(c, VA_FONT_BUTTON), t = 0;
    if (rect_pair(c, r)) return 1;
    if (status == 0u) {
        hw32(c, o + 0x2cdu, 1);
        hw32(c, r, d13_wsx(c, 0x4d1eccu));
        hw32(c, r + 4u, d13_wsx(c, 0x4d1eceu));
    } else if (status == 1u) {
        hw32(c, o + 0x2cdu, 2);
        hw32(c, r, d13_wsx(c, 0x4d1ed0u));
        hw32(c, r + 4u, d13_wsx(c, 0x4d1ed2u));
        hw32(c, r + 0x10u, d13_wsx(c, 0x4d1ed4u));
        hw32(c, r + 0x14u, d13_wsx(c, 0x4d1ed6u));
    }
    if (dlg_bitmap(c, 0x0049e1c6u, 0x0049e1cfu, 0x0049e1d3u, &b)) return 1;
    hw32(c, o + 0x30u, hr32(c, hr32(c, b + 8u) + 4u));
    hw32(c, pt, 0);
    hw32(c, pt + 4u, d13_wsx(c, 0x4d1edcu));
    for (int32_t k = 0; k < count_of(c, o); ++k) {
        uint32_t q = r + 0x10u * (uint32_t)k, g = 0;
        hw32(c, q + 8u, hr32(c, q) + d13_wsx(c, 0x4d1ed8u));
        hw32(c, q + 0xcu, hr32(c, q + 4u) + d13_wsx(c, 0x4d1edau));
        if (button_new(c, o, (uint32_t)k, q, pt, font, &g)) return 1;
        hw32(c, g + 0x30u, hr32(c, hr32(c, hr32(c, VA_BUTTON_IMAGES) + 8u) + 4u * t));
        hw32(c, g + 0x34u, hr32(c, hr32(c, hr32(c, VA_BUTTON_IMAGES) + 8u) + 4u * t + 4u));
        if (button_add(c, o, g, o + 0x291u + 4u * (uint32_t)k)) return 1;
    }
    alert_texts(c, o, status);
    if (dlg_title(c, o, 0x004d1ec4u, 0x332u, pt, 0)) return 1;
    hw32(c, o + 0x2fbu, 0);
    hud_counter(c, 1);
    hud_frame_pop(c, top);
    return 0;
}
/* ---- 0x4566e0(o, save) (listing 0x4566e0..0x456cee): the F11/F12
 * options dialog: the base at the shifted (0, 0x96) 0x13f x 0x59, the
 * once table, the bitmap 0x49e3ba kept in [0x4d1efc], the texts
 * (0x49e3cb/0x49e3d4), eleven buttons from the word tables 0x49e1fc..
 * 0x49e254 (state 3; the last two state 0), the title, nine value labels
 * in [0x4d1e2c]'s font (mode 1, colour word 0x4d3ba2); default the last
 * gadget, no page hook, no pending refresh, no highlight; counter +1. */
static int options_ctor(hud_ctx *c, uint32_t o, uint32_t save) {
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0x18u);
    if (!f) return hud_refuse(c, 0x004566e0u, "options dialog", "the own code's call area is exhausted");
    uint32_t q = f, pt = f + 0x10u, b = 0;
    uint32_t h = d13_shs(c, 0x59, VA_AXIS_Y), w = d13_shs(c, 0x13f, VA_AXIS_X);
    uint32_t y = d13_shs(c, 0x96, VA_AXIS_Y), x = d13_shs(c, 0, VA_AXIS_X);
    if (dlg_ctor(c, o, save, dlg_rect(c, x, y, w, h, NULL))) return 1;
    hw32(c, o, 0x0049e3e4u);
    d13_once_fill(c, 0x0049e2dau, d13_options_once, sizeof(d13_options_once) / sizeof(d13_options_once[0]));
    if (dlg_bitmap(c, 0x0049e3bau, 0x0049e3c3u, 0x0049e3c7u, &b)) return 1;
    hw32(c, VA_OPTIONS_BITMAP, b);
    hw32(c, o + 0x30u, hr32(c, hr32(c, hr32(c, VA_OPTIONS_BITMAP) + 8u)));
    static const d13_text texts[] = {{0x35e, 1}, {0x362, 0xb}, {0x38e, 2}, {0x396, 3}, {0x3a2, 5}};
    if (dlg_texts(c, o, 0x0049e3cbu, 0x0049e3d4u, texts, 5)) return 1;
    uint32_t font = hr32(c, VA_FONT_BUTTON);
    hw32(c, pt, 0);
    hw32(c, pt + 4u, d13_shs(c, 0, VA_AXIS_Y));
    for (uint32_t k = 0; k < 11u; ++k) {
        uint32_t g = 0, rx = d13_shs(c, hr16(c, 0x0049e1fcu + 2u * k), VA_AXIS_X), ry = d13_shs(c, hr16(c, 0x0049e212u + 2u * k), VA_AXIS_Y);
        hw32(c, q, rx);
        hw32(c, q + 4u, ry);
        hw32(c, q + 8u, d13_shs(c, hr16(c, 0x0049e228u + 2u * k), VA_AXIS_X) + rx);
        hw32(c, q + 0xcu, d13_shs(c, hr16(c, 0x0049e23eu + 2u * k), VA_AXIS_Y) + ry);
        if (button_new(c, o, k, q, pt, font, &g)) return 1;
        uint32_t t = d13_wsx(c, 0x0049e254u + 2u * k);
        for (uint32_t j = 0; j < 4u; ++j)
            hw32(c, g + 0x30u + 4u * j, hr32(c, hr32(c, hr32(c, VA_OPTIONS_BITMAP) + 8u) + 4u * (t + j)));
        if (HCALL(c, VA_PANEL_ADD, NULL, hr32(c, o + 0x285u), g)) return 1;
        uint32_t n = hr32(c, o + 0x2cdu);
        hw32(c, o + 0x2cdu, n + 1u);
        hw32(c, o + 0x291u + 4u * n, g);
        hw16(c, g + 0x1bu, 3);
    }
    hw16(c, hr32(c, o + 0x28du + 4u * hr32(c, o + 0x2cdu)) + 0x1bu, 0);
    hw16(c, hr32(c, o + 0x289u + 4u * hr32(c, o + 0x2cdu)) + 0x1bu, 0);
    if (dlg_title(c, o, 0x004d1ee8u, 0x35eu, pt, 0)) return 1;
    font = hr32(c, VA_FONT_TEXT);
    hw32(c, pt, d13_wsx(c, 0x4d1ef6u));
    hw32(c, pt + 4u, d13_wsx(c, 0x4d1ef8u));
    for (uint32_t i = 0; i < 9u; ++i) {
        uint32_t l = 0;
        if (label_new(c, font, &l)) return 1;
        hw32(c, o + 0x332u + 4u * i, l);
        uint32_t rx = d13_shs(c, hr16(c, 0x0049e26au + 2u * i), VA_AXIS_X), ry = d13_shs(c, hr16(c, 0x0049e27cu + 2u * i), VA_AXIS_Y);
        hw32(c, q, rx);
        hw32(c, q + 4u, ry);
        hw32(c, q + 8u, d13_shs(c, hr16(c, 0x0049e28eu + 2u * i), VA_AXIS_X) + rx);
        hw32(c, q + 0xcu, d13_shs(c, hr16(c, 0x0049e2a0u + 2u * i), VA_AXIS_Y) + ry);
        if (HCALL(c, VA_LABEL_PLACE, NULL, hr32(c, o + 0x332u + 4u * i), pt, q, 1)) return 1;
        l = hr32(c, o + 0x332u + 4u * i);
        if (!l) return hud_null_refuse(c, 0x00456c7eu, "options dialog", "new returned NULL for a value label (the original writes through it)");
        hw32(c, l + 0x1du, d13_wsx(c, 0x004d3ba2u));
    }
    hw32(c, o + 0x2fbu, hr32(c, o + 0x2cdu) - 1u);
    hw32(c, o + 0x3bau, 0);
    for (uint32_t i = 0; i < 9u; ++i) hw8(c, o + 0x3beu + i, 0);
    hw32(c, o + 0x3b6u, 0xffffffffu);
    hud_counter(c, 1);
    hud_frame_pop(c, top);
    return 0;
}
/* ---- 0x456cf0(o, f) (listing 0x456cf0..0x456d67): counter -1; the nine
 * value labels; with a kept bitmap [0x4d1efc]: counter +1, 0x48349c(it, 3)
 * and o+0x30 cleared; the base (0x4543b0 with 0); o with f bit 0. */
static int options_dtor(hud_ctx *c, uint32_t o, uint32_t f) {
    hud_counter(c, -1);
    if (!o) return 0;
    hw32(c, o, 0x0049e3e4u);
    for (uint32_t i = 0; i < 9u; ++i)
        if (HCALL(c, HUD_VA_DELETE, NULL, hr32(c, o + 0x332u + 4u * i))) return 1;
    if (hr32(c, VA_OPTIONS_BITMAP)) {
        hud_counter(c, 1);
        if (HCALL(c, VA_BITMAP_FREE, NULL, hr32(c, VA_OPTIONS_BITMAP), 3)) return 1;
        hw32(c, o + 0x30u, 0);
    }
    if (dlg_dtor(c, o, 0)) return 1;
    return (f & 1u) ? HCALL(c, HUD_VA_DELETE, NULL, o) : 0;
}
/* ---- 0x4570fc(d) (listing 0x4570fc..0x457199): the backdrop, the
 * gadgets, the nine value labels (0x457210). */
static int options_draw(hud_ctx *c, uint32_t d) {
    if (dlg_backdrop(c, d, 0x35eu) || gadgets_draw(c, d)) return 1;
    for (uint32_t k = 0; k < 9u; ++k)
        if (options_label(c, d, k)) return 1;
    return 0;
}
/* ---- 0x457538(o, save, small) (listing 0x457538..0x457a9a): the mission
 * objectives: the base at the shifted 0x13b x 0x8b size, a second text
 * block o+0x332, the once table, the bitmap 0x49e4a5, the texts
 * (0x49e4b6/0x49e4bf), the objective texts (word count 0x4a9e44, indices
 * [0x4a9e48] into 0x4aa660), the title (placed in a local copy), one
 * button, seven line labels; default 0, counter +1. */
static int objectives_ctor(hud_ctx *c, uint32_t o, uint32_t save, uint32_t small) {
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0x28u);
    if (!f) return hud_refuse(c, 0x00457538u, "objectives", "the own code's call area is exhausted");
    uint32_t q = f, pt = f + 0x10u, tr = f + 0x18u, b = 0, g = 0, l = 0;
    if (dlg_ctor(c, o, save, ctor_rect(c, small, 0x13b, 0x8b))) return 1;
    hw32(c, o, 0x0049e4d0u);
    text_block_init(c, o + 0x332u);
    d13_once_fill(c, 0x0049e3fcu, d13_objectives_once, sizeof(d13_objectives_once) / sizeof(d13_objectives_once[0]));
    if (dlg_bitmap(c, 0x0049e4a5u, 0x0049e4aeu, 0x0049e4b2u, &b)) return 1;
    hw32(c, o + 0x30u, hr32(c, hr32(c, b + 8u)));
    static const d13_text texts[] = {{0x342, 1}, {0x346, 1}};
    if (dlg_texts(c, o, 0x0049e4b6u, 0x0049e4bfu, texts, 2)) return 1;
    uint16_t n = hr16(c, 0x004a9e44u);
    hw16(c, o + 0x39au, n);
    if ((int16_t)n > 0) {
        uint32_t p = hr32(c, 0x004a9e48u);
        for (int32_t i = 0; i < (int32_t)d13_wsx(c, o + 0x39au); ++i)
            hw32(c, o + 0x34au + 4u * (uint32_t)i, hr32(c, 0x004aa660u + 4u * d13_wsx(c, p + 2u * (uint32_t)i)));
    }
    if (dlg_title(c, o, 0x004d1f04u, 0x342u, pt, tr)) return 1;
    uint32_t font = hr32(c, VA_FONT_BUTTON);
    hw32(c, pt, 0);
    hw32(c, pt + 4u, d13_wsx(c, 0x4d1f14u));
    hw32(c, q, d13_wsx(c, 0x4d1f0cu));
    hw32(c, q + 4u, d13_wsx(c, 0x4d1f0eu));
    hw32(c, q + 8u, d13_wsx(c, 0x4d1f10u) + hr32(c, q));
    hw32(c, q + 0xcu, d13_wsx(c, 0x4d1f12u) + hr32(c, q + 4u));
    if (button_new(c, o, 0, q, pt, font, &g)) return 1;
    hw32(c, g + 0x30u, hr32(c, hr32(c, hr32(c, VA_BUTTON_IMAGES) + 8u)));
    hw32(c, g + 0x34u, hr32(c, hr32(c, hr32(c, VA_BUTTON_IMAGES) + 8u) + 4u));
    if (HCALL(c, VA_PANEL_ADD, NULL, hr32(c, o + 0x285u), g)) return 1;
    uint32_t k = hr32(c, o + 0x2cdu);
    hw32(c, o + 0x2cdu, k + 1u);
    hw32(c, o + 0x291u + 4u * k, g);
    uint32_t x = hr32(c, o + 4u), x2 = hr32(c, o + 0xcu);
    font = hr32(c, VA_FONT_BUTTON);
    hw32(c, pt + 4u, 0);
    hw32(c, q + 4u, d13_wsx(c, 0x4d1f1cu));
    hw32(c, q + 0xcu, d13_wsx(c, 0x4d1f20u) + d13_wsx(c, 0x4d1f1cu));
    for (uint32_t i = 0; i < 7u; ++i) {
        hw32(c, q, x);
        hw32(c, q + 8u, x2);
        if (label_new(c, font, &l)) return 1;
        hw32(c, o + 0x39cu + 4u * i, l);
        if (HCALL(c, VA_LABEL_PLACE, NULL, hr32(c, o + 0x39cu + 4u * i), pt, q, 2)) return 1;
        uint32_t d = d13_wsx(c, 0x4d1f18u);
        add32(c, q + 4u, d);
        add32(c, q + 0xcu, d);
    }
    hw32(c, o + 0x2fbu, 0);
    hud_counter(c, 1);
    hud_frame_pop(c, top);
    return 0;
}
/* ---- 0x457a9c(o, f) (listing 0x457a9c..0x457afd): counter -1; the seven
 * line labels, the second text block (0x453848 with 2), the base, o with
 * f bit 0. */
static int objectives_dtor(hud_ctx *c, uint32_t o, uint32_t f) {
    hud_counter(c, -1);
    if (!o) return 0;
    hw32(c, o, 0x0049e4d0u);
    for (uint32_t i = 0; i < 7u; ++i)
        if (HCALL(c, HUD_VA_DELETE, NULL, hr32(c, o + 0x39cu + 4u * i))) return 1;
    if (text_block_free(c, o + 0x332u, 2) || dlg_dtor(c, o, 0)) return 1;
    return (f & 1u) ? HCALL(c, HUD_VA_DELETE, NULL, o) : 0;
}
/* ---- 0x457b74(d) (listing 0x457b74..0x457c49): the backdrop, the
 * non-empty objective texts into the line labels (at most 7), the
 * gadgets. */
static int objectives_draw(hud_ctx *c, uint32_t d) {
    if (dlg_backdrop(c, d, 0x342u)) return 1;
    uint32_t j = 0;
    for (int32_t i = 0; i < (int32_t)d13_wsx(c, d + 0x39au); ++i) {
        uint32_t t = hr32(c, d + 0x34au + 4u * (uint32_t)i), v = 0;
        if (HCALL(c, VA_TEXT_NONEMPTY, &v, t)) return 1;
        if (v) {
            uint32_t l = hr32(c, d + 0x39cu + 4u * j);
            ++j;
            if (HCALL(c, VA_LABEL_DRAW, NULL, l, hr32(c, d + 0x34au + 4u * (uint32_t)i))) return 1;
        }
        if (j == 7u) break;
    }
    return gadgets_draw(c, d);
}

/* ---- 0x42d89c(rect, origin) (listing 0x42d89c..0x42d943) and 0x42d944
 * (0x42d944..0x42d9e5): the view rectangle 0x4cfa40 and origin 0x4cfa50
 * set (or restored from 0x4cfa28), the copy 0x4cfca4; with a camera entry
 * (0x429820) of kind 4 both into the current graphics context +0x210/
 * +0x220. EAX 0 (no entry), the entry (other kind) or the context +0x220. */
static int view_gc_copy(hud_ctx *c, uint32_t *eax) {
    uint32_t e = 0;
    if (HCALL0(c, VA_CAMERA_ENTRY, &e)) return 1;
    *eax = e;
    if (!e || hr32(c, e + 0x14u) != 4u) return 0;
    uint32_t g = hr32(c, VA_GC_SAVED);
    for (uint32_t i = 0; i < 0x10u; i += 4u) hw32(c, g + 0x210u + i, hr32(c, VA_VIEW_RECT + i));
    hw32(c, g + 0x220u, hr32(c, VA_VIEW_RECT + 0x10u));
    hw32(c, g + 0x224u, hr32(c, VA_VIEW_RECT + 0x14u));
    *eax = g + 0x220u;
    return 0;
}
static int view_rect_set(hud_ctx *c, uint32_t rect, uint32_t origin, uint32_t *eax) {
    for (uint32_t i = 0; i < 0x10u; i += 4u) hw32(c, VA_VIEW_RECT + i, hr32(c, rect + i));
    hw32(c, VA_VIEW_RECT + 0x10u, hr32(c, origin));
    hw32(c, VA_VIEW_RECT + 0x14u, hr32(c, origin + 4u));
    for (uint32_t i = 0; i < 0x10u; i += 4u) hw32(c, VA_VIEW_RECT_COPY + i, hr32(c, rect + i));
    return view_gc_copy(c, eax);
}
static int view_rect_reset(hud_ctx *c, uint32_t *eax) {
    uint32_t v[4];
    for (uint32_t i = 0; i < 4u; ++i) v[i] = hr32(c, VA_VIEW_RECT_SAVED + 4u * i);
    for (uint32_t i = 0; i < 4u; ++i) hw32(c, VA_VIEW_RECT + 4u * i, v[i]);
    hw32(c, VA_VIEW_RECT + 0x10u, hr32(c, VA_VIEW_RECT_SAVED + 0x10u));
    hw32(c, VA_VIEW_RECT + 0x14u, hr32(c, VA_VIEW_RECT_SAVED + 0x14u));
    for (uint32_t i = 0; i < 4u; ++i) hw32(c, VA_VIEW_RECT_COPY + 4u * i, v[i]);
    return view_gc_copy(c, eax);
}

/* ---- 0x45cfc8 (listing 0x45cfc8..0x45cfee): the options session's page
 * hook: the outside camera 0x401c74([0x4d24ea], -0x40, 0, 0), the camera
 * update and the render root (hooks, through the dispatch). */
static int options_idle(hud_ctx *c) {
    uint32_t v = hr32(c, VA_VIEWS);
    if (HCALL(c, VA_OUTSIDE_CAMERA, NULL, v, 0xffffffc0u, 0, 0)) return 1;
    if (HCALL(c, VA_CAMERA_UPDATE, NULL, hr32(c, VA_VIEWS))) return 1;
    return HCALL0(c, VA_RENDER_ROOT, NULL);
}
/* The session's view offsets 0x45edb0(G+0x2a) and the camera's redraw
 * word (C+0x36 <- 1 while C+0x4a is 0). */
static int session_offsets(hud_ctx *c) {
    if (HCALL(c, VA_VIEW_OFFSETS, NULL, hr32(c, VA_SHELL + 0x2au))) return 1;
    uint32_t cam = hr32(c, VA_SHELL + 0x2eu);
    if (!hr32(c, cam + 0x4au)) hw16(c, cam + 0x36u, 1);
    return 0;
}
static int session_frame(hud_ctx *c) {
    if (HCALL(c, VA_CAMERA_UPDATE, NULL, hr32(c, VA_SHELL + 0x2eu))) return 1;
    return HCALL0(c, VA_RENDER_ROOT, NULL);
}
/* ---- 0x45cff0 (listing 0x45cff0..0x45d48e): the F12/Alt+P options
 * session: the options dialog (new 0x3c7, 0x4566e0 with 0, counter -10)
 * over the 3D view: G+0x36 forced to 1 (its low byte kept), 0x4d267e
 * cleared, a view record L (0x48bd84, table 0x49f8e8) holding a copy of
 * the camera C = [G+0x2e] (bytes 4..0x4d), the view rectangle {0, 0,
 * [G+0x82]-1, [d+8]-1} with the origin {-x2 >> 1, (int16)(0xffce << the
 * shift G+0x80)} (0x42d89c); camera mode [[G+4]+0x14] other than 4: the
 * frames until the display [0x4cf99c] is idle (byte +0x1c clear, +0x18
 * -1), the display request 0x42a3f4(G+4's view, 2), then the frames until
 * the display turned busy and idle again (the offsets at its first busy
 * frame); mode 4: the offsets and one frame. Then the dialog's page hook
 * 0x45cfc8 and its modal loop (vtable +8), the dialog deleted (counter
 * +10, 0x456cf0 with 3), the page +0x8c, the camera restored from L, the
 * view rectangle restored (0x42d944), the camera mode restored over two
 * frames (mode 4: the camera reset 0x42da08 and the subject label
 * 0x45e208), G+0x36 restored, counter -2, L destroyed (0x48bcc4 with 0).
 * The frame loops count against the dialogs' pass limit. */
static int options_session(hud_ctx *c) {
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0x68u);
    if (!f) return hud_refuse(c, 0x0045cff0u, "options session", "the own code's call area is exhausted");
    uint32_t L = f, R = f + 0x50u, O = f + 0x60u, g = VA_SHELL, d = 0, mode = 0;
    uint32_t s0 = hr32(c, VA_SURFACE);
    if (HCALL(c, HUD_VA_NEW, &d, 0x3c7u)) return 1;
    if (d) {
        if (options_ctor(c, d, 0)) return 1;
        hud_counter(c, -0xa);
    }
    uint8_t saved = hr8(c, g + 0x36u);
    hw16(c, g + 0x36u, 1);
    hw16(c, VA_SESSION_WORD, 0);
    if (HCALL(c, VA_VIEW_CTOR, NULL, L)) return 1;
    hw32(c, L, 0x0049f8e8u);
    hw32(c, L + 0x32u, 0);
    hw32(c, L + 0x4au, 0);
    hw16(c, L + 0x36u, 0);
    hw16(c, L + 0x48u, 0);
    hw16(c, L + 0x46u, 0);
    hw16(c, L + 0x44u, 0);
    hud_counter(c, 1);
    uint32_t cam = hr32(c, g + 0x2eu);
    for (uint32_t i = 4u; i < 0x4eu; ++i) hw8(c, L + i, hr8(c, cam + i));
    if (!d) return hud_null_refuse(c, 0x0045d160u, "options session", "new returned NULL for the options dialog (the original reads through it)");
    hw32(c, R, 0);
    hw32(c, R + 4u, 0);
    hw32(c, R + 8u, hr32(c, g + 0x82u) - 1u);
    hw32(c, R + 0xcu, hr32(c, d + 8u) - 1u);
    hw32(c, O, (uint32_t)((int32_t)(0u - hr32(c, R + 8u)) >> 1));
    hw32(c, O + 4u, hsx16((uint16_t)(0xffceu << (hr16(c, g + 0x80u) & 31u))));
    uint32_t e = 0;
    if (view_rect_set(c, R, O, &e)) return 1;
    mode = hr32(c, hr32(c, g + 4u) + 0x14u);
    if (mode != 4u) {
        for (;;) {
            uint32_t disp = hr32(c, VA_DISPLAY);
            if (!hr8(c, disp + 0x1cu) && hr32(c, disp + 0x18u) == 0xffffffffu) break;
            if (pass(c, 0x0045d1c9u)) return 1;
            if (HCALL0(c, VA_FRAME_INPUT, NULL) || session_frame(c)) return 1;
        }
        uint32_t v = hr32(c, g + 4u);
        mode = hr32(c, v + 0x14u);
        if (HCALL(c, VA_DISPLAY_REQUEST, NULL, v, 2)) return 1;
        uint8_t busy = 0;
        do {
            if (pass(c, 0x0045d20du)) return 1;
            if (HCALL0(c, VA_FRAME_INPUT, NULL)) return 1;
            if (!busy) {
                busy = hr8(c, hr32(c, VA_DISPLAY) + 0x1cu);
                if (busy && session_offsets(c)) return 1;
            }
            if (session_frame(c)) return 1;
        } while (!busy || hr8(c, hr32(c, VA_DISPLAY) + 0x1cu));
    } else if (session_offsets(c) || session_frame(c)) {
        return 1;
    }
    hw8(c, VA_DRAW_FLAG, 1);
    if (slot_call(c, VA_DRV_24, NULL, 0)) return 1;
    if (HCALL(c, VA_OPTIONS_HOOK, NULL, d, VA_OPTIONS_IDLE)) return 1;
    if (HVCALL(c, d, 8u, NULL, d)) return 1;
    hud_counter(c, 0xa);
    if (options_dtor(c, d, 3)) return 1;
    if (page_select(c, s0, 0x3cu, hr32(c, s0 + 0x8cu), VA_PAGE_B, VA_DRV_F0)) return 1;
    cam = hr32(c, g + 0x2eu);
    for (uint32_t i = 4u; i < 0x4eu; ++i) hw8(c, cam + i, hr8(c, L + i));
    if (view_rect_reset(c, &e)) return 1;
    if (mode != 4u) {
        if (HCALL(c, VA_CAMERA_UPDATE, NULL, hr32(c, g + 0x2eu))) return 1;
        if (HCALL(c, VA_CAMERA_MODE, NULL, hr32(c, g + 4u), mode)) return 1;
        hw8(c, VA_SESSION_FLAG, 1);
        if (HCALL0(c, VA_RENDER_ROOT, NULL) || HCALL0(c, VA_FRAME_INPUT, NULL)) return 1;
        if (HCALL(c, VA_CAMERA_MODE, NULL, hr32(c, g + 4u), mode)) return 1;
        hw8(c, VA_SESSION_FLAG, 0);
        if (HCALL0(c, VA_RENDER_ROOT, NULL)) return 1;
    } else {
        if (HCALL0(c, VA_CAMERA_ENTRY, &e) || HCALL(c, VA_CAMERA_RESET, NULL, e) || HCALL0(c, VA_SUBJECT_LABEL, NULL)) return 1;
        if (session_frame(c) || HCALL0(c, VA_FRAME_INPUT, NULL)) return 1;
        if (HCALL0(c, VA_CAMERA_ENTRY, &e) || HCALL(c, VA_CAMERA_RESET, NULL, e) || HCALL0(c, VA_SUBJECT_LABEL, NULL)) return 1;
    }
    hw8(c, VA_DRAW_FLAG, 1);
    hw16(c, g + 0x36u, saved);
    hud_counter(c, -2);
    if (HCALL(c, VA_VIEW_DTOR, NULL, L, 0)) return 1;
    hud_frame_pop(c, top);
    return 0;
}

/* ---- the service rows of the dialog constructors and the options session */
ROW(text_block_init) { ROW_BEGIN; text_block_init(&c, sc->slots[0]); eax = sc->slots[0]; ROW_END(0); }
ROW(text_block_free) { ROW_BEGIN; ROW_END(text_block_free(&c, sc->slots[0], sc->slots[1])); }
ROW(dialog_ctor) { ROW_BEGIN; eax = sc->slots[0]; ROW_END(dlg_ctor(&c, sc->slots[0], sc->slots[1], sc->slots[2])); }
ROW(dialog_dtor) { ROW_BEGIN; ROW_END(dlg_dtor(&c, sc->slots[0], sc->slots[1])); }
ROW(dialog_rect) {
    ROW_BEGIN;
    uint32_t edx = 0;
    eax = dlg_rect(&c, sc->slots[0], sc->slots[1], sc->slots[2], sc->slots[3], &edx);
    sc->edx = edx;
    ROW_END(0);
}
ROW(dialog_rect_centred) {
    ROW_BEGIN;
    uint32_t ecx = 0, edx = 0;
    eax = dlg_rect_centred(&c, sc->slots[0], sc->slots[1], &ecx, &edx);
    sc->ecx = ecx;
    sc->edx = edx;
    ROW_END(0);
}
ROW(dialog_button_init) { ROW_BEGIN; eax = sc->slots[0]; ROW_END(button_init(&c, sc->slots[0], sc->slots[1], sc->slots[2], sc->slots[3])); }
ROW(dialog_button_draw) { ROW_BEGIN; ROW_END(button_draw(&c, sc->slots[0])); }
ROW(alert_base_ctor) { ROW_BEGIN; eax = sc->slots[0]; ROW_END(alert_base(&c, sc->slots[0], sc->slots[2], sc->slots[3])); }
ROW(alert_ctor) { ROW_BEGIN; eax = sc->slots[0]; ROW_END(alert_ctor(&c, sc->slots[0], sc->slots[1], sc->slots[2], sc->slots[3])); }
ROW(alert_dtor) { ROW_BEGIN; ROW_END(alert_dtor(&c, sc->slots[0], sc->slots[1])); }
ROW(alert_draw) { ROW_BEGIN; ROW_END(alert_draw(&c, sc->slots[0])); }
ROW(pause_ctor) { ROW_BEGIN; eax = sc->slots[0]; ROW_END(pause_ctor(&c, sc->slots[0], sc->slots[1], sc->slots[2], sc->slots[3])); }
ROW(options_ctor) { ROW_BEGIN; eax = sc->slots[0]; ROW_END(options_ctor(&c, sc->slots[0], sc->slots[1])); }
ROW(options_dtor) { ROW_BEGIN; ROW_END(options_dtor(&c, sc->slots[0], sc->slots[1])); }
ROW(options_draw) { ROW_BEGIN; ROW_END(options_draw(&c, sc->slots[0])); }
ROW(objectives_ctor) { ROW_BEGIN; eax = sc->slots[0]; ROW_END(objectives_ctor(&c, sc->slots[0], sc->slots[1], sc->slots[2])); }
ROW(objectives_dtor) { ROW_BEGIN; ROW_END(objectives_dtor(&c, sc->slots[0], sc->slots[1])); }
ROW(objectives_draw) { ROW_BEGIN; ROW_END(objectives_draw(&c, sc->slots[0])); }
ROW(view_rect_set) { ROW_BEGIN; ROW_END(view_rect_set(&c, sc->slots[0], sc->slots[1], &eax)); }
ROW(view_rect_reset) { ROW_BEGIN; ROW_END(view_rect_reset(&c, &eax)); }
ROW(options_idle) { ROW_BEGIN; ROW_END(options_idle(&c)); }
ROW(options_session) { ROW_BEGIN; ROW_END(options_session(&c)); }

/* ==== the joystick options page (options gadget 9; input.md, "the joystick
 * page and the SDL3 joystick"). Every function below is the original's at its
 * VA, read from the listing: guest state through the guest view, the dialog,
 * input and other rows and the other DBSIM code through the dispatch, the
 * dialog constructor pieces above called directly. The
 * SEH frame of 0x457d38 is not reproduced; the RTL counter bumps are. The page
 * object P (0x480 bytes, table 0x49e804): +0x332 the title text, +0x336
 * fourteen button texts, +0x36e/+0x37a/+0x386/+0x392 three value texts for each
 * of the four option rows, +0x39e 21 action names, +0x3f2 the heading text,
 * +0x3f6 twelve value labels, +0x426 twelve column labels, +0x456 the heading
 * label, +0x45a the pending gadget (index + 1), +0x45e four option values,
 * +0x462 eight chosen entries, +0x46a eight entry counts (last index), +0x472
 * the highlighted gadget, +0x476 the shown column, +0x47a the joystick summary
 * (0x477904) or 0. 0x459244 and 0x459af4 have no caller in DBSIM.EXE and get no
 * row. */
#define VA_PAGE_TABLE 0x0049e804u
#define VA_PAGE_LAYOUT 0x004d2571u     /* byte: the second entry table and the extra text split */
#define VA_PAGE_PREF_BASE 0x004d2577u  /* byte: the first joystick pref */
#define VA_PAGE_ENTRIES_A 0x0049e5f1u  /* 8 rows of 13 bytes */
#define VA_PAGE_ENTRIES_B 0x0049e659u
#define VA_PAGE_BITMAP 0x004d1f34u
#define VA_PAGE_COLOUR 0x004d3ba2u     /* word */
#define VA_PAGE_STORE_WORDS 0x0049e7bau
#define VA_PAGE_STORE_ONCE 0x0049e7d4u
#define VA_PAGE_EMPTY_A 0x0049e7f3u    /* the column texts of no row */
#define VA_PAGE_EMPTY_B 0x0049e7f4u
#define VA_PREFS_COMMITTED 0x004d1f6eu
#define VA_PREFS_CALLBACKS 0x004d1fdcu
#define VA_DIALOG_OPEN 0x0045464cu
#define VA_DIALOG_CLOSE 0x004548c8u
#define VA_DIALOG_STEP 0x00454accu
#define VA_FOCUS_SET 0x00454c98u
#define VA_FOCUS_NEXT 0x00454d98u
#define VA_FOCUS_PREVIOUS 0x00454dbcu
#define VA_GADGET_PRESS 0x00454de8u
#define VA_KEY_FLAG 0x0045b734u
#define VA_PREFS_CYCLE 0x004599d4u
#define VA_PREFS_CYCLE_BACK 0x00459a10u
#define VA_JOYSTICK_OBJECT 0x0045c524u
#define VA_JOYSTICK_SUMMARY 0x00477904u
#define VA_JOYSTICK_MAP 0x0045a274u
#define VA_PAGE_CTOR 0x00457d38u
#define VA_PAGE_RUN 0x0045866cu
#define VA_PAGE_DTOR 0x004585f4u

static uint32_t jp_base(hud_ctx *c) { return hr8(c, VA_PAGE_PREF_BASE); }
static uint32_t jp_pref(hud_ctx *c, uint32_t i) { return hr8(c, VA_PREFS + (uint8_t)i); }
static uint32_t jp_summary(hud_ctx *c, uint32_t o) { return hr32(c, o + 0x47au); }
/* A summary S and (int32)i < (int16)word S+2. */
static int jp_axis_ok(hud_ctx *c, uint32_t o, uint32_t i) {
    uint32_t s = jp_summary(c, o);
    return s && (int32_t)i < (int32_t)hsx16(hr16(c, s + 2u));
}
/* A gadget's redraw (its table's +4). */
static int jp_redraw(hud_ctx *c, uint32_t g) { return gadget_call(c, g, 4u, (const uint32_t[]){g}, 1); }
/* The highlight moved to slot k: the old one (if any) to state 3 and
 * redrawn, k to state 0 and redrawn. */
static int jp_highlight(hud_ctx *c, uint32_t o, uint32_t k) {
    if (hr32(c, o + 0x472u) != 0xffffffffu) {
        hw16(c, gadget(c, o, hr32(c, o + 0x472u)) + 0x1bu, 3);
        if (jp_redraw(c, gadget(c, o, hr32(c, o + 0x472u)))) return 1;
    }
    hw32(c, o + 0x472u, k);
    hw16(c, gadget(c, o, k) + 0x1bu, 0);
    return jp_redraw(c, gadget(c, o, k));
}

/* ---- 0x457cf8(i, j) (listing 0x457cf8..0x457d37): entry j of row i (both
 * sign-extended words) of the table 0x49e659 (byte [0x4d2571] set) or
 * 0x49e5f1, 13 bytes per row; AL the byte. The upper bytes of EAX differ by
 * path: with [0x4d2571] set they are those of the row address (EAX holds
 * it, 0x457d16), otherwise those of 13*i (EAX holds the row offset,
 * 0x457d2a; the address is formed in ECX). Callers push the full EAX. */
static uint32_t jp_entry(hud_ctx *c, uint32_t i, uint32_t j) {
    uint32_t r = 13u * hsx16((uint16_t)i);
    uint32_t a = (hr8(c, VA_PAGE_LAYOUT) ? VA_PAGE_ENTRIES_B : VA_PAGE_ENTRIES_A) + r;
    uint32_t hi = hr8(c, VA_PAGE_LAYOUT) ? a : r;
    return (hi & 0xffffff00u) | hr8(c, a + hsx16((uint16_t)j));
}
/* ---- 0x458d3c(P, k) (listing 0x458d3c..0x458ed7): value label k (k <= 12)
 * drawn, bracketed by the driver slots when the presents are not due: 1
 * while the summary has axes, 2..4 while its bytes +4..+6 are set, from
 * the row's value texts by P+0x45e+(k-1); 5..12 below the summary's axis
 * count from the action names by the pref [0x4d2577]+4+(k-5); then
 * 0x454ac0 (EAX its clock). */
static int jp_label(hud_ctx *c, uint32_t o, uint32_t k, uint32_t *eax) {
    if (!hr16(c, VA_PRESENT_ON) && slot_call(c, VA_DRV_28, NULL, 0)) return 1;
    if (k <= 12u && k != 0u) {
        uint32_t s = jp_summary(c, o), text = 0;
        int draw = 0;
        if (k == 1u) {
            draw = s && hr16(c, s);
            if (draw) text = hr32(c, o + 0x36eu + 4u * hr8(c, o + 0x45eu));
        } else if (k <= 4u) {
            draw = s && hr8(c, s + 2u + k);
            if (draw) text = hr32(c, o + 0x36eu + 0xcu * (k - 1u) + 4u * hr8(c, o + 0x45du + k));
        } else {
            draw = jp_axis_ok(c, o, k - 5u);
            if (draw) text = hr32(c, o + 0x39eu + 4u * jp_pref(c, k - 5u + jp_base(c) + 4u));
        }
        if (draw && HCALL(c, VA_LABEL_DRAW, NULL, hr32(c, o + 0x3f2u + 4u * k), text)) return 1;
    }
    if (!hr16(c, VA_PRESENT_ON) && slot_call(c, VA_DRV_24, NULL, 0)) return 1;
    return present_time(c, eax);
}
/* ---- 0x459414(P, i) (listing 0x459414..0x459487): the twelve column
 * labels: for i = -1 the text 0x49e7f3; else rows up to the count byte
 * P+0x46a+i (signed row against the zero-extended byte) the action name
 * of entry (i, row), past it the text 0x49e7f4. EAX the last draw's. */
static int jp_column(hud_ctx *c, uint32_t o, uint32_t i, uint32_t *eax) {
    for (uint32_t r = 0; r < 12u; ++r) {
        uint32_t text = VA_PAGE_EMPTY_A;
        if (i != 0xffffffffu)
            text = (int32_t)r <= (int32_t)hr8(c, o + i + 0x46au) ? hr32(c, o + 0x39eu + 4u * (jp_entry(c, i, r) & 0xffu))
                                                                 : VA_PAGE_EMPTY_B;
        if (HCALL(c, VA_LABEL_DRAW, eax, hr32(c, o + 0x426u + 4u * r), text)) return 1;
    }
    return 0;
}
/* ---- 0x459314(P, i) (listing 0x459314..0x45933a): another column is
 * drawn; P+0x476 <- i. */
static int jp_row(hud_ctx *c, uint32_t o, uint32_t i) {
    if (i != hr32(c, o + 0x476u) && jp_column(c, o, i, NULL)) return 1;
    hw32(c, o + 0x476u, i);
    return 0;
}
/* ---- 0x45933c(P, i) (listing 0x45933c..0x4593a4) and 0x4593a8 (0x4593a8..
 * 0x459410): the chosen entry of axis row i one on (past the count byte:
 * 0) or one back (from 0: the count byte), the pref [0x4d2577]+4+i set to
 * the entry (0x459958 with the whole EAX of 0x457cf8, flag 1), value
 * label i+5 (EAX its). */
static int jp_step(hud_ctx *c, uint32_t o, uint32_t i, int back, uint32_t *eax) {
    uint32_t a = o + i + 0x462u;
    if (!back) {
        hw8(c, a, (uint8_t)(hr8(c, a) + 1u));
        if (hr8(c, a) > hr8(c, o + i + 0x46au)) hw8(c, a, 0);
    } else {
        hw8(c, a, (uint8_t)(hr8(c, a) - 1u));
        if (hr8(c, a) == 0xffu) hw8(c, a, (uint8_t)hr8(c, o + i + 0x46au));
    }
    uint32_t v = jp_entry(c, i, hr8(c, a));
    if (HCALL(c, VA_PREFS_SET, NULL, (uint8_t)(i + jp_base(c) + 4u), v, 1)) return 1;
    return jp_label(c, o, i + 5u, eax);
}
/* ---- 0x45915c(P) (listing 0x45915c..0x459241): at the first call the 13
 * words 0x49e7ba <- the pref indices [0x4d2577]-1 .. +11 (low bytes; the
 * once byte 0x49e7d4 counts up after the first word); then the prefs
 * store 0x459b94(13, 0x49e7ba). */
static int jp_store(hud_ctx *c, uint32_t *eax) {
    if (!hr8(c, VA_PAGE_STORE_ONCE)) {
        hw16(c, VA_PAGE_STORE_WORDS, (uint8_t)(jp_base(c) - 1u));
        hw8(c, VA_PAGE_STORE_ONCE, (uint8_t)(hr8(c, VA_PAGE_STORE_ONCE) + 1u));
        for (uint32_t m = 1; m < 13u; ++m) hw16(c, VA_PAGE_STORE_WORDS + 2u * m, (uint8_t)(jp_base(c) + m - 1u));
    }
    return HCALL(c, VA_PREFS_STORE, eax, 0xdu, VA_PAGE_STORE_WORDS);
}
/* ---- 0x459894(flag) (listing 0x459894..0x4598f3): of the 0x36 prefs, a
 * byte that differs from its committed copy 0x4d1f6e calls its callback
 * [0x4d1fdc+4n] (with flag and a callback; the argument is EAX with the
 * byte in AL, the upper bytes left by the last callback) and is
 * committed; every byte is copied to the previous values 0x4d1fa4. */
static int prefs_commit(hud_ctx *c, uint32_t flag, uint32_t *eax) {
    uint32_t e = 0;
    for (uint32_t n = 0; n < 0x36u; ++n) {
        if (hr8(c, VA_PREFS_COMMITTED + n) != hr8(c, VA_PREFS + n)) {
            uint32_t cb = hr32(c, VA_PREFS_CALLBACKS + 4u * n);
            if ((flag & 0xffu) && cb) {
                e = (e & 0xffffff00u) | hr8(c, VA_PREFS + n);
                if (HCALL(c, cb, &e, e)) return 1;
            }
            hw8(c, VA_PREFS_COMMITTED + n, (uint8_t)hr8(c, VA_PREFS + n));
        }
        e = (e & 0xffffff00u) | hr8(c, VA_PREFS + n);
        hw8(c, VA_PREFS_PREVIOUS + n, (uint8_t)e);
    }
    *eax = e;
    return 0;
}

/* ---- 0x457d38(P, save, small) (listing 0x457d38..0x4585f0): the base at
 * the shifted 0xb9 x 0xba size, the once table (guard 0x49e5f0), the
 * bitmap 0x49e7d5 kept in [0x4d1f34], the texts (0x49e7e6/0x49e7ef; with
 * [0x4d2571] five of them split once more), fourteen buttons from the word
 * tables 0x49e4e8..0x49e574 (images from 0x49e558, state 3; the last two
 * state 0), the title, twelve value labels (mode 1, colour 0x4d3ba2), the
 * heading label in [0x4d1e18]'s font (32-bit shifts), twelve column labels
 * 7 << y apart, the entry counts; no pending gadget, the last gadget the
 * default, no column, no highlight; counter +1. The locals (a rectangle
 * and a point) live in a frame of the call area. */
static const d13_once jp_once[] = {
    {0x4d1f24u, 0xb9, 0}, {0x4d1f26u, 0xba, 1}, {0x4d1f28u, 0, 0}, {0x4d1f2au, 0, 1},
    {0x4d1f2cu, 8, 1}, {0x4d1f2eu, 0, 1}, {0x4d1f30u, 2, 0}, {0x4d1f32u, 0, 1},
};
static const d13_text jp_texts[] = {{0x332, 1}, {0x336, 0xe}, {0x39e, 0x15}, {0x3f2, 1}, {0x36e, 3}, {0x37a, 3}, {0x386, 3}, {0x392, 3}};
static const d13_text jp_texts_again[] = {{0x332, 1}, {0x36e, 3}, {0x37a, 3}, {0x386, 3}, {0x392, 3}};
/* A label placed (0x438884) and coloured: the colour word read before the
 * label pointer is read again (the original writes through a NULL one). */
static int jp_place(hud_ctx *c, uint32_t slot, uint32_t pt, uint32_t q, uint32_t mode, uint32_t va) {
    if (HCALL(c, VA_LABEL_PLACE, NULL, hr32(c, slot), pt, q, mode)) return 1;
    uint32_t colour = d13_wsx(c, VA_PAGE_COLOUR), l = hr32(c, slot);
    if (!l) return hud_null_refuse(c, va, "joystick page", "new returned NULL for a label (the original writes through it)");
    hw32(c, l + 0x1du, colour);
    return 0;
}
static void jp_rect(hud_ctx *c, uint32_t q, uint32_t xs, uint32_t ys, uint32_t ws, uint32_t hs, uint32_t k) {
    uint32_t rx = d13_shs(c, hr16(c, xs + 2u * k), VA_AXIS_X), ry = d13_shs(c, hr16(c, ys + 2u * k), VA_AXIS_Y);
    hw32(c, q, rx);
    hw32(c, q + 4u, ry);
    hw32(c, q + 8u, d13_shs(c, hr16(c, ws + 2u * k), VA_AXIS_X) + rx);
    hw32(c, q + 0xcu, d13_shs(c, hr16(c, hs + 2u * k), VA_AXIS_Y) + ry);
}
static int jp_ctor(hud_ctx *c, uint32_t o, uint32_t save, uint32_t small) {
    uint32_t top = c->l->stack_hi, f = hud_frame_push(c, 0x18u);
    if (!f) return hud_refuse(c, VA_PAGE_CTOR, "joystick page", "the own code's call area is exhausted");
    uint32_t q = f, pt = f + 0x10u, b = 0;
    if (dlg_ctor(c, o, save, ctor_rect(c, small, 0xb9, 0xba))) return 1;
    hw32(c, o, VA_PAGE_TABLE);
    d13_once_fill(c, 0x0049e5f0u, jp_once, sizeof(jp_once) / sizeof(jp_once[0]));
    if (dlg_bitmap(c, 0x0049e7d5u, 0x0049e7deu, 0x0049e7e2u, &b)) return 1;
    hw32(c, VA_PAGE_BITMAP, b);
    hw32(c, o + 0x30u, hr32(c, hr32(c, hr32(c, VA_PAGE_BITMAP) + 8u)));
    if (dlg_texts(c, o, 0x0049e7e6u, 0x0049e7efu, jp_texts, sizeof(jp_texts) / sizeof(jp_texts[0]))) return 1;
    if (hr8(c, VA_PAGE_LAYOUT))
        for (unsigned i = 0; i < sizeof(jp_texts_again) / sizeof(jp_texts_again[0]); ++i)
            if (HCALL(c, VA_TEXT_SPLIT, NULL, o + 0x34u, o + jp_texts_again[i].off, jp_texts_again[i].count, 0)) return 1;
    uint32_t font = hr32(c, VA_FONT_BUTTON);
    hw32(c, pt, 0);
    for (uint32_t k = 0; k < 14u; ++k) {
        uint32_t g = 0;
        hw32(c, pt + 4u, d13_shs(c, hr16(c, 0x0049e574u + 2u * k), VA_AXIS_Y));
        jp_rect(c, q, 0x0049e4e8u, 0x0049e504u, 0x0049e520u, 0x0049e53cu, k);
        if (button_new(c, o, k, q, pt, font, &g)) return 1;
        uint32_t t = d13_wsx(c, 0x0049e558u + 2u * k);
        for (uint32_t j = 0; j < 4u; ++j)
            hw32(c, g + 0x30u + 4u * j, hr32(c, hr32(c, hr32(c, VA_PAGE_BITMAP) + 8u) + 4u * (t + j)));
        if (HCALL(c, VA_PANEL_ADD, NULL, hr32(c, o + 0x285u), g)) return 1;
        uint32_t n = hr32(c, o + 0x2cdu);
        hw32(c, o + 0x2cdu, n + 1u);
        hw32(c, o + 0x291u + 4u * n, g);
        hw16(c, g + 0x1bu, 3);
    }
    hw16(c, hr32(c, o + 0x28du + 4u * hr32(c, o + 0x2cdu)) + 0x1bu, 0);
    hw16(c, hr32(c, o + 0x289u + 4u * hr32(c, o + 0x2cdu)) + 0x1bu, 0);
    if (dlg_title(c, o, 0x004d1f28u, 0x332u, pt, 0)) return 1;
    font = hr32(c, VA_FONT_TEXT);
    hw32(c, pt, d13_wsx(c, 0x4d1f30u));
    hw32(c, pt + 4u, d13_wsx(c, 0x4d1f32u));
    for (uint32_t i = 0; i < 12u; ++i) {
        uint32_t l = 0;
        if (label_new(c, font, &l)) return 1;
        hw32(c, o + 0x3f6u + 4u * i, l);
        jp_rect(c, q, 0x0049e590u, 0x0049e5a8u, 0x0049e5c0u, 0x0049e5d8u, i);
        if (jp_place(c, o + 0x3f6u + 4u * i, pt, q, 1, 0x0045840bu)) return 1;
    }
    /* The heading: 32-bit shl (the count masked to five bits). */
    uint32_t sx = hr16(c, VA_AXIS_X) & 31u, sy = hr16(c, VA_AXIS_Y) & 31u, l = 0;
    uint32_t d7 = 7u << sy, d5 = 5u << sy;
    hw32(c, q, 0x71u << sx);
    hw32(c, q + 4u, 0x45u << sy);
    hw32(c, q + 8u, 0xb1u << sx);
    hw32(c, q + 0xcu, hr32(c, q + 4u) + d5);
    if (label_new(c, hr32(c, VA_FONT_TITLE), &l)) return 1;
    hw32(c, o + 0x456u, l);
    if (HCALL(c, VA_LABEL_PLACE, NULL, hr32(c, o + 0x456u), pt, q, 2)) return 1;
    for (uint32_t i = 0; i < 12u; ++i) {
        if (label_new(c, font, &l)) return 1;
        hw32(c, o + 0x426u + 4u * i, l);
        add32(c, q + 4u, d7);
        add32(c, q + 0xcu, d7);
        if (jp_place(c, o + 0x426u + 4u * i, pt, q, 2, 0x0045856au)) return 1;
    }
    for (uint32_t i = 0; i < 8u; ++i) {
        uint32_t a = o + 0x46au + i;
        hw8(c, a, 0);
        for (uint32_t j = 0; j < 13u; ++j) {
            if (!(jp_entry(c, i, j) & 0xffu)) {
                hw8(c, a, (uint8_t)(hr8(c, a) - 1u));
                break;
            }
            hw8(c, a, (uint8_t)(hr8(c, a) + 1u));
        }
    }
    hw32(c, o + 0x45au, 0);
    hw32(c, o + 0x2fbu, hr32(c, o + 0x2cdu) - 1u);
    hw32(c, o + 0x476u, 0xffffffffu);
    hw32(c, o + 0x472u, 0xffffffffu);
    hud_counter(c, 1);
    hud_frame_pop(c, top);
    return 0;
}
/* ---- 0x4585f4(P, f) (listing 0x4585f4..0x45866b): counter -1; the twelve
 * value labels deleted (the column and heading labels are not); with a
 * kept bitmap [0x4d1f34]: counter +1, 0x48349c(it, 3), P+0x30 cleared;
 * the base (0x4543b0 with 0); P deleted with f bit 0. */
static int jp_dtor(hud_ctx *c, uint32_t o, uint32_t f) {
    hud_counter(c, -1);
    if (!o) return 0;
    hw32(c, o, VA_PAGE_TABLE);
    for (uint32_t i = 0; i < 12u; ++i)
        if (HCALL(c, HUD_VA_DELETE, NULL, hr32(c, o + 0x3f6u + 4u * i))) return 1;
    if (hr32(c, VA_PAGE_BITMAP)) {
        hud_counter(c, 1);
        if (HCALL(c, VA_BITMAP_FREE, NULL, hr32(c, VA_PAGE_BITMAP), 3)) return 1;
        hw32(c, o + 0x30u, 0);
    }
    if (dlg_dtor(c, o, 0)) return 1;
    return (f & 1u) ? HCALL(c, HUD_VA_DELETE, NULL, o) : 0;
}
/* ---- 0x458c84(P) (listing 0x458c84..0x458d3a, vtable +0): the backdrop
 * with the title, the gadgets, value labels 1..12, the heading. */
static int jp_draw(hud_ctx *c, uint32_t o, uint32_t *eax) {
    if (dlg_backdrop(c, o, 0x332u) || gadgets_draw(c, o)) return 1;
    for (uint32_t k = 1; k <= 12u; ++k)
        if (jp_label(c, o, k, NULL)) return 1;
    return HCALL(c, VA_LABEL_DRAW, eax, hr32(c, o + 0x456u), hr32(c, o + 0x3f2u));
}
/* ---- 0x458ed8(P, g, flags) (listing 0x458ed8..0x458fb6, vtable +0xc):
 * P+0x2ff <- flags bit 1; for g's slot k: below 12 the highlight moves to
 * it; an axis button (4..11) other than the shown column shows its
 * column, any other slot becomes the pending gadget k+1; slot 13 ends
 * with 3; then 0x454ac0. */
static int jp_select(hud_ctx *c, uint32_t o, uint32_t g, uint32_t flags, uint32_t *eax) {
    hw8(c, o + 0x2ffu, (flags & 2u) ? 1u : 0u);
    for (int32_t k = 0; k < count_of(c, o); ++k) {
        if (gadget(c, o, (uint32_t)k) != g) continue;
        if (k < 12 && jp_highlight(c, o, (uint32_t)k)) return 1;
        uint32_t i = (uint32_t)k - 4u;
        if (!(k >= 4 && k < 12) || i == hr32(c, o + 0x476u)) {
            hw32(c, o + 0x45au, (uint32_t)k + 1u);
        } else {
            if (jp_row(c, o, i)) return 1;
            hw32(c, o + 0x476u, i);
        }
        if (k == 13) hw8(c, o + 0x2d1u, 3);
        break;
    }
    return present_time(c, eax);
}
/* ---- 0x458fb8(P, r) (listing 0x458fb8..0x459146, vtable +0x10): the
 * record's byte +0xd copied to +0x16; the first set byte r+0x16+n (n < 8):
 * on the shown column the entry steps on (0x45933c) and the axis button
 * is pressed (0x4530bc); else, below the summary's axis count, the column
 * is shown, the button pressed and highlighted; key flag n+1 either way.
 * Then the key word: 1 presses the default gadget, 0xf and 0x52 move the
 * focus on, 0x1c presses the focused gadget (or focuses 0), 0x80f moves
 * back. AL 1 when the key word is one of these (the upper bytes are a
 * callee's), EAX 0 else. */
static int jp_keys(hud_ctx *c, uint32_t o, uint32_t r, uint32_t *eax) {
    hw8(c, r + 0x16u, (uint8_t)hr8(c, r + 0xdu));
    for (uint32_t n = 0; n < 8u; ++n) {
        if (!hr8(c, r + 0x16u + n)) continue;
        if (n == hr32(c, o + 0x476u)) {
            if (jp_step(c, o, n, 0, NULL)) return 1;
            if (release_append(c, hr32(c, o + 0x285u), gadget(c, o, n + 4u))) return 1;
        } else if (jp_axis_ok(c, o, n)) {
            if (jp_row(c, o, n)) return 1;
            if (release_append(c, hr32(c, o + 0x285u), gadget(c, o, n + 4u))) return 1;
            if (jp_highlight(c, o, n + 4u)) return 1;
        }
        if (HCALL(c, VA_KEY_FLAG, NULL, n + 1u, 1)) return 1;
        break;
    }
    *eax = 1;
    switch (hr16(c, r)) {
    case 0x1u: {
        uint32_t d = hr32(c, o + 0x2fbu);
        return d != 0xffffffffu ? HCALL(c, VA_GADGET_PRESS, NULL, o, d, 1) : 0;
    }
    case 0xfu:
    case 0x52u: return HCALL(c, VA_FOCUS_NEXT, NULL, o);
    case 0x1cu: {
        uint32_t f = hr32(c, o + 0x2f7u);
        return f != 0xffffffffu ? HCALL(c, VA_GADGET_PRESS, NULL, o, f, 1) : HCALL(c, VA_FOCUS_SET, NULL, o, 0);
    }
    case 0x80fu: return HCALL(c, VA_FOCUS_PREVIOUS, NULL, o);
    default: *eax = 0; return 0;
    }
}
/* Option row m (0..3): the pref [0x4d2577]+m cycled over three values
 * (back with P+0x2ff), P+0x45e+m <- AL, the pending gadget's label. */
static int jp_option(hud_ctx *c, uint32_t o, uint32_t m) {
    uint32_t v = 0;
    if (HCALL(c, hr8(c, o + 0x2ffu) ? VA_PREFS_CYCLE_BACK : VA_PREFS_CYCLE, &v, (uint8_t)(jp_base(c) + m), 3, 1)) return 1;
    hw8(c, o + 0x45eu + m, (uint8_t)v);
    return jp_label(c, o, hr32(c, o + 0x45au), NULL);
}
/* Pending gadget 13: the defaults of the joystick map 0x45a274([0x4d2571]):
 * the four option values (each set with the previous label's EAX in the
 * upper bytes), the eight axis entries found in their rows (else 0), each
 * set and its label drawn; the column cleared. */
static int jp_defaults(hud_ctx *c, uint32_t o) {
    uint32_t p = 0;
    if (HCALL(c, VA_JOYSTICK_MAP, &p, hr8(c, VA_PAGE_LAYOUT))) return 1;
    uint32_t e = p;
    for (uint32_t k = 0; k < 4u; ++k) {
        uint8_t v = (uint8_t)hr8(c, p + k);
        e = (e & 0xffffff00u) | v;
        hw8(c, o + 0x45eu + k, v);
        if (HCALL(c, VA_PREFS_SET, NULL, (uint8_t)(jp_base(c) + k), e, 1)) return 1;
        if (jp_label(c, o, k + 1u, &e)) return 1;
    }
    for (uint32_t i = 0; i < 8u; ++i) {
        uint32_t a = o + 0x462u + i;
        hw8(c, a, 0);
        for (uint32_t j = 0; j < 13u; ++j) {
            if ((uint8_t)jp_entry(c, i, j) == (uint8_t)hr8(c, p + 4u + i)) {
                hw8(c, a, (uint8_t)j);
                break;
            }
        }
        uint32_t v = jp_entry(c, i, hr8(c, a));
        if (HCALL(c, VA_PREFS_SET, NULL, (uint8_t)(i + jp_base(c) + 4u), v, 1)) return 1;
        if (jp_label(c, o, i + 5u, NULL)) return 1;
    }
    return jp_row(c, o, 0xffffffffu);
}
/* ---- 0x45866c(P) (listing 0x45866c..0x458c83, vtable +8): the summary
 * P+0x47a (0x477904 when the joystick object 0x45c524(3) has bit 0, else
 * 0); with axes the buttons of missing option rows and axes go to state
 * 2, without them all twelve; the option values and chosen entries from
 * the prefs; the open, focus 0; per pass the control record, the key
 * handler (vtable +0x10) and, with a summary that has axes, the pending
 * gadget: 1..4 cycle an option row, 5..12 step an axis entry, 13 the
 * defaults, 14 stores and commits the prefs (0x45915c, 0x459894(1)); every
 * gadget's tick and the step; the close; EAX the end state's bit 0. */
static int jp_run(hud_ctx *c, uint32_t o, uint32_t *eax) {
    uint32_t j = 0, s = 0;
    if (HCALL(c, VA_JOYSTICK_OBJECT, &j, 3)) return 1;
    int present = j && (hr8(c, j) & 1u);
    if (present && HCALL0(c, VA_JOYSTICK_SUMMARY, &s)) return 1;
    hw32(c, o + 0x47au, present ? s : 0u);
    s = jp_summary(c, o);
    if (s && hr16(c, s)) {
        for (uint32_t m = 1; m <= 3u; ++m)
            if (!hr8(c, jp_summary(c, o) + 3u + m)) hw16(c, gadget(c, o, m) + 0x1bu, 2);
        for (int32_t k = (int32_t)hsx16(hr16(c, jp_summary(c, o) + 2u)); k < 8; ++k)
            hw16(c, hr32(c, o + 0x2a1u + 4u * (uint32_t)k) + 0x1bu, 2);
    } else {
        for (uint32_t k = 0; k < 12u; ++k) hw16(c, gadget(c, o, k) + 0x1bu, 2);
    }
    for (uint32_t m = 0; m < 4u; ++m) hw8(c, o + 0x45eu + m, (uint8_t)jp_pref(c, jp_base(c) + m));
    for (uint32_t i = 0; i < 8u; ++i) {
        uint32_t a = o + 0x462u + i;
        hw8(c, a, 0);
        uint8_t v = (uint8_t)jp_pref(c, i + jp_base(c) + 4u);
        for (uint32_t k = 0; k < 13u; ++k) {
            if ((uint8_t)jp_entry(c, i, k) == v) {
                hw8(c, a, (uint8_t)k);
                break;
            }
        }
    }
    if (HCALL(c, VA_DIALOG_OPEN, NULL, o) || HCALL(c, VA_FOCUS_SET, NULL, o, 0)) return 1;
    while (!hr8(c, o + 0x2d1u)) {
        if (pass(c, VA_PAGE_RUN)) return 1;
        uint32_t r = 0;
        if (HCALL0(c, VA_CONTROL_RECORD, &r)) return 1;
        if (HVCALL(c, o, 0x10u, NULL, o, r)) return 1;
        uint32_t a = hr32(c, o + 0x45au);
        s = jp_summary(c, o);
        if (a && s && hr16(c, s)) {
            int bad = 0;
            if (a <= 4u) {
                s = jp_summary(c, o);
                if (s && (a == 1u ? hr16(c, s) != 0 : hr8(c, s + 2u + a) != 0)) bad = jp_option(c, o, a - 1u);
                if (!bad) bad = jp_row(c, o, 0xffffffffu);
            } else if (a <= 12u) {
                uint32_t i = hr32(c, o + 0x45au) - 5u;
                if (jp_axis_ok(c, o, i))
                    bad = jp_row(c, o, i) || jp_step(c, o, i, hr8(c, o + 0x2ffu) != 0, NULL) ||
                          jp_label(c, o, hr32(c, o + 0x45au), NULL);
            } else if (a == 13u) {
                bad = jp_defaults(c, o);
            } else if (a == 14u) {
                uint32_t v = 0;
                bad = jp_store(c, &v) || prefs_commit(c, 1, &v);
            }
            if (bad) return 1;
            hw32(c, o + 0x45au, 0);
        }
        for (int32_t k = 0; k < count_of(c, o); ++k) {
            uint32_t g = gadget(c, o, (uint32_t)k);
            if (gadget_call(c, g, 0xcu, (const uint32_t[]){g}, 1)) return 1;
        }
        if (HCALL(c, VA_DIALOG_STEP, NULL, o)) return 1;
    }
    if (HCALL(c, VA_DIALOG_CLOSE, NULL, o)) return 1;
    *eax = hr8(c, o + 0x2d1u) & 1u;
    return 0;
}
/* ---- options gadget 9 (0x456d68's case at 0x457036..0x457093): gadget 9
 * drawn (its table's +0), the page built in a 0x480-byte local (the
 * original's is uninitialised stack, ours a zeroed frame), run, the
 * options dialog drawn (vtable +0) and focused on its default gadget, the
 * page destroyed (0x4585f4 with 2: not deleted). */
static int joystick_page(hud_ctx *c, uint32_t d) {
    uint32_t g = gadget(c, d, 9);
    if (gadget_call(c, g, 0, (const uint32_t[]){g}, 1)) return 1;
    uint32_t top = c->l->stack_hi, p = hud_frame_push(c, 0x480u);
    if (!p) return hud_refuse(c, 0x00457036u, "joystick page", "the own code's call area is exhausted");
    if (HCALL(c, VA_PAGE_CTOR, NULL, p, 1, 0) || HCALL(c, VA_PAGE_RUN, NULL, p)) return 1;
    if (HVCALL(c, d, 0, NULL, d) || focus_set(c, d, hr32(c, d + 0x2fbu))) return 1;
    if (HCALL(c, VA_PAGE_DTOR, NULL, p, 2)) return 1;
    hud_frame_pop(c, top);
    return 0;
}

/* ---- the joystick page's service rows */
ROW(joy_table) { ROW_BEGIN; eax = jp_entry(&c, sc->slots[0], sc->slots[1]); ROW_END(0); }
ROW(joy_page_ctor) { ROW_BEGIN; eax = sc->slots[0]; ROW_END(jp_ctor(&c, sc->slots[0], sc->slots[1], sc->slots[2])); }
ROW(joy_page_dtor) { ROW_BEGIN; ROW_END(jp_dtor(&c, sc->slots[0], sc->slots[1])); }
ROW(joy_page_run) { ROW_BEGIN; ROW_END(jp_run(&c, sc->slots[0], &eax)); }
ROW(joy_page_draw) { ROW_BEGIN; ROW_END(jp_draw(&c, sc->slots[0], &eax)); }
ROW(joy_page_label) { ROW_BEGIN; ROW_END(jp_label(&c, sc->slots[0], sc->slots[1], &eax)); }
ROW(joy_page_select) { ROW_BEGIN; ROW_END(jp_select(&c, sc->slots[0], sc->slots[1], sc->slots[2], &eax)); }
ROW(joy_page_keys) { ROW_BEGIN; ROW_END(jp_keys(&c, sc->slots[0], sc->slots[1], &eax)); }
ROW(joy_page_text) { ROW_BEGIN; eax = hr32(&c, sc->slots[0] + 0x336u + 4u * sc->slots[1]); ROW_END(0); }
ROW(joy_page_store) { ROW_BEGIN; ROW_END(jp_store(&c, &eax)); }
ROW(joy_page_row) { ROW_BEGIN; ROW_END(jp_row(&c, sc->slots[0], sc->slots[1])); }
ROW(joy_page_next) { ROW_BEGIN; ROW_END(jp_step(&c, sc->slots[0], sc->slots[1], 0, &eax)); }
ROW(joy_page_back) { ROW_BEGIN; ROW_END(jp_step(&c, sc->slots[0], sc->slots[1], 1, &eax)); }
ROW(joy_page_column) { ROW_BEGIN; ROW_END(jp_column(&c, sc->slots[0], sc->slots[1], &eax)); }
ROW(prefs_commit) { ROW_BEGIN; ROW_END(prefs_commit(&c, sc->slots[0], &eax)); }
