/* The windowed platform layer's shared part; see es2/sim/platform_window.h and own_process.md, "the
 * windowed platform layer" (AltGr's fake Ctrl, the key state at activation, the cursor rows, empty and
 * scaled presents). The window procedure, the pump and the input functions stay the own code of loop.c
 * and input.c; this file only produces what Windows would hand them (messages, key states, the client
 * rectangle, the cursor position, the joystick answers) and converts what the present drew for the
 * backend to show. */
#include <stdlib.h>
#include <string.h>
#include "es2/sim/platform_window.h"

#define VA_HWND 0x004d3610u
#define VA_GAME_W 0x004d253eu
#define VA_GAME_H 0x004d2542u
#define VA_DIB_STRIDE 0x004d2546u   /* the DIB's width, also its row stride */
#define VA_DIB_BITS 0x004d3028u

/* Virtual-key codes (winuser.h). */
#define VK_SHIFT_ 0x10u
#define VK_CONTROL_ 0x11u
#define VK_MENU_ 0x12u
#define VK_F4_ 0x73u
#define VK_F10_ 0x79u
/* MK_ flags of the mouse messages' wParam. */
#define MK_LBUTTON_ 0x0001u
#define MK_RBUTTON_ 0x0002u
#define MK_SHIFT_ 0x0004u
#define MK_CONTROL_ 0x0008u
#define MK_MBUTTON_ 0x0010u
/* WM_SYSCOMMAND and the commands DefWindowProcA sends for keys. */
#define WM_SYSCOMMAND_ 0x0112u
#define SC_CLOSE_ 0xf060u
#define SC_KEYMENU_ 0xf100u

static uint32_t r32(const es2_sim_window *w, uint32_t va) {
    const es2_guest_memory *m = w->loop->memory;
    return m->read32(m->context, va);
}

uint32_t es2_sim_window_key_lparam(uint32_t scan, int extended, int alt, int previous, int up) {
    return 1u | (scan & 0xffu) << 16 | (extended ? 1u << 24 : 0u) | (alt ? 1u << 29 : 0u) | (previous ? 1u << 30 : 0u) |
           (up ? 1u << 31 : 0u);
}

/* The left/right code of a modifier from its message (GetKeyState answers
 * VK_LSHIFT..VK_RMENU too). */
static uint32_t side_vk(uint32_t vk, uint32_t lparam) {
    int extended = (lparam >> 24) & 1u;
    if (vk == VK_SHIFT_) return ((lparam >> 16) & 0xffu) == 0x36u ? 0xa1u : 0xa0u;
    if (vk == VK_CONTROL_) return extended ? 0xa3u : 0xa2u;
    if (vk == VK_MENU_) return extended ? 0xa5u : 0xa4u;
    return 0;
}

/* ---- the queue */
static void enqueue(es2_sim_window *w, uint32_t message, uint32_t wparam, uint32_t lparam) {
    if (w->count == ES2_SIM_WINDOW_QUEUE) { ++w->dropped; return; }   /* a full queue drops input, as Windows' does */
    unsigned at = (w->head + w->count) % ES2_SIM_WINDOW_QUEUE;
    w->queue[at].message = message;
    w->queue[at].wparam = wparam;
    w->queue[at].lparam = lparam;
    ++w->count;
    ++w->queued;
}
static int send(es2_sim_window *w, uint32_t message, uint32_t wparam, uint32_t lparam) {
    ++w->sent;
    uint32_t result = 0;
    return es2_sim_window_proc(w->loop, r32(w, VA_HWND), message, wparam, lparam, &result);
}
/* GetKeyState follows the retrieved messages (the thread's synchronous key
 * state): a down sets 0x80 and flips the toggle bit when the key was up. */
static void key_retrieved(es2_sim_window *w, uint32_t message, uint32_t wparam, uint32_t lparam) {
    if (message != ES2_WM_KEYDOWN && message != ES2_WM_SYSKEYDOWN && message != ES2_WM_KEYUP && message != ES2_WM_SYSKEYUP)
        return;
    int down = message == ES2_WM_KEYDOWN || message == ES2_WM_SYSKEYDOWN;
    uint32_t codes[2] = {side_vk(wparam, lparam), wparam & 0xffu};
    for (unsigned i = 0; i < 2u; ++i) {
        uint32_t vk = codes[i];
        if (!vk) continue;
        if (i == 1u && codes[0]) {
            /* The generic modifier is down while either side is. */
            uint32_t a = codes[0] & ~1u;
            int any = (w->keys[a] | w->keys[a + 1u]) & 0x80u;
            if (any && !(w->keys[vk] & 0x80u)) w->keys[vk] ^= 1u;
            w->keys[vk] = (uint8_t)((w->keys[vk] & 1u) | (any ? 0x80u : 0u));
            continue;
        }
        if (down) {
            if (!(w->keys[vk] & 0x80u)) w->keys[vk] ^= 1u;
            w->keys[vk] |= 0x80u;
        } else {
            w->keys[vk] &= (uint8_t)~0x80u;
        }
    }
}

/* ---- the key state at activation. Windows brings the thread's key
 * state up to the keyboard's when the thread becomes the active one
 * (hypothesis from the documented GetKeyState behaviour; not measured):
 * GetKeyState's table takes the down and toggle bits, the event state the
 * down bits. A held right Alt with a held left Ctrl counts as AltGr's fake
 * Ctrl on an AltGr layout. */
void es2_sim_window_resync(es2_sim_window *w, const uint8_t state[256]) {
    ++w->resyncs;
    for (unsigned vk = 1; vk < 256u; ++vk) {
        w->keys[vk] = state[vk] & 0x81u;
        w->async[vk] = state[vk] & 0x80u;
    }
    w->altgr_ctrl = w->altgr && (state[0xa5] & 0x80u) && (state[0xa2] & 0x80u);
    w->last_down = 0;
    w->alt_alone = 0;
}

/* ---- events: the asynchronous key state (as the events arrive) builds the
 * messages. */
static int async_down(const es2_sim_window *w, uint32_t vk) { return (w->async[vk & 0xffu] & 0x80u) != 0; }
static uint32_t mouse_flags(const es2_sim_window *w) {
    return w->buttons | (async_down(w, VK_SHIFT_) ? MK_SHIFT_ : 0u) | (async_down(w, VK_CONTROL_) ? MK_CONTROL_ : 0u);
}
static int in_client(const es2_sim_window *w, int32_t x, int32_t y) {
    return x >= 0 && y >= 0 && x < w->client_w && y < w->client_h && !w->minimized;
}
static uint32_t point(int32_t x, int32_t y) { return ((uint32_t)y & 0xffffu) << 16 | ((uint32_t)x & 0xffffu); }

/* One key message from the event state: `vk` goes down or up now. */
static void key_message(es2_sim_window *w, uint32_t vk, uint32_t scan, int extended, int down) {
    int previous = async_down(w, vk);
    if (down) w->async[vk] |= 0x80u;
    else w->async[vk] &= (uint8_t)~0x80u;
    int alt = async_down(w, VK_MENU_), ctrl = async_down(w, VK_CONTROL_);
    /* WM_SYS* for F10 and for any key, Alt itself included, while Alt is down
     * without Ctrl; the context bit is that state. With Ctrl down (held, or
     * AltGr's fake left Ctrl) Alt's key-down is WM_KEYDOWN. */
    /* Alt's own key-up is WM_SYSKEYUP only when its key-down was a
     * WM_SYSKEYDOWN and no other key went down after it (the keyboard menu's
     * case), else WM_KEYUP (hypothesis from the documented Win32 sequences:
     * Alt+key SYSKEYDOWN, SYSKEYDOWN, SYSKEYUP, KEYUP; AltGr KEYDOWN Ctrl,
     * KEYDOWN Alt, KEYUP Ctrl, KEYUP Alt; own_process.md, "the window's open
     * points"). */
    int sys;
    if (vk == VK_MENU_ && !down) sys = w->last_down == VK_MENU_;
    else sys = vk == VK_F10_ || (alt && !ctrl);
    /* last_down VK_MENU_ only for a system Alt down; a plain one (with Ctrl)
     * marks itself as another key so its key-up stays WM_KEYUP. */
    if (down) w->last_down = vk == VK_MENU_ && !sys ? 0xffu : vk;
    uint32_t message = down ? (sys ? ES2_WM_SYSKEYDOWN : ES2_WM_KEYDOWN) : (sys ? ES2_WM_SYSKEYUP : ES2_WM_KEYUP);
    enqueue(w, message, vk, es2_sim_window_key_lparam(scan, extended, alt && !ctrl, down ? previous : 1, !down));
}
int es2_sim_window_key_event(es2_sim_window *w, uint32_t vk, uint32_t scan, int extended, int down, int altgr_key) {
    if (!w->focused) { ++w->ignored; return 0; }   /* keys go to the focus window only */
    if (!vk) { ++w->ignored; return 0; }
    /* With a layout that has AltGr, Windows puts a left Ctrl (scan 0x1d, not
     * extended) before every right-Alt key-down, repeats included, and its
     * key-up before the right Alt's; SDL leaves it out. Added only when no Ctrl
     * is down already (a delivered left Ctrl is not doubled). Hypothesis from
     * the documented behaviour, not measured (own_process.md, "the window's
     * open points"). */
    if (altgr_key && down && (w->altgr_ctrl || !async_down(w, VK_CONTROL_))) {
        w->altgr_ctrl = 1;
        ++w->altgr_ctrls;
        key_message(w, VK_CONTROL_, 0x1du, 0, 1);
    }
    if (altgr_key && !down && w->altgr_ctrl) {
        w->altgr_ctrl = 0;
        key_message(w, VK_CONTROL_, 0x1du, 0, 0);
    }
    key_message(w, vk, scan, extended, down);
    return 0;
}
int es2_sim_window_motion_event(es2_sim_window *w, int32_t x, int32_t y) {
    if (!in_client(w, x, y)) { ++w->ignored; return 0; }   /* no capture: moves outside reach other windows */
    w->mouse_x = x;
    w->mouse_y = y;
    enqueue(w, ES2_WM_MOUSEMOVE, mouse_flags(w), point(x, y));
    return 0;
}
int es2_sim_window_button_event(es2_sim_window *w, unsigned button, int down, int32_t x, int32_t y) {
    static const uint32_t flags[3] = {MK_LBUTTON_, MK_RBUTTON_, MK_MBUTTON_};
    static const uint32_t messages[3] = {ES2_WM_LBUTTONDOWN, ES2_WM_RBUTTONDOWN, ES2_WM_MBUTTONDOWN};
    if (button > 2u) { ++w->ignored; return 0; }
    if (down) w->buttons |= flags[button];
    else w->buttons &= ~flags[button];
    if (!in_client(w, x, y)) { ++w->ignored; return 0; }
    /* The class has no CS_DBLCLKS: a second click is another down. */
    enqueue(w, down ? messages[button] : messages[button] + 1u, mouse_flags(w), point(x, y));
    return 0;
}
int es2_sim_window_wheel_event(es2_sim_window *w, int32_t delta) {
    if (!w->focused || !delta) { ++w->ignored; return 0; }
    int32_t x = w->origin_x + w->mouse_x, y = w->origin_y + w->mouse_y;   /* screen coordinates */
    enqueue(w, ES2_WM_MOUSEWHEEL, ((uint32_t)delta & 0xffffu) << 16 | (mouse_flags(w) & 0xffffu), point(x, y));
    return 0;
}
int es2_sim_window_focus_event(es2_sim_window *w, int gained) {
    if (gained) {
        if (w->focused) { ++w->ignored; return 0; }
        w->focused = 1;
        /* The activation synchronises the thread's key state with the keyboard
         * (keys pressed or released while the focus was away). */
        if (w->key_sync) {
            uint8_t state[256];
            memset(state, 0, sizeof(state));
            w->key_sync(w, state);
            es2_sim_window_resync(w, state);
        }
        w->focus_pending = 0;
        if (w->guest_focus) { ++w->ignored; return 0; }
        w->guest_focus = 1;
        return send(w, ES2_WM_SETFOCUS, 0, 0);
    }
    if (!w->focused) { ++w->ignored; return 0; }
    w->focused = 0;
    memset(w->async, 0, sizeof(w->async));
    if (!w->guest_focus) { ++w->ignored; return 0; }
    w->guest_focus = 0;
    return send(w, ES2_WM_KILLFOCUS, 0, 0);
}
int es2_sim_window_close_event(es2_sim_window *w) { return send(w, ES2_WM_CLOSE, 0, 0); }
int es2_sim_window_expose_event(es2_sim_window *w) {
    w->paint_pending = 1;
    return 0;
}
int es2_sim_window_minimize_event(es2_sim_window *w, int minimized) {
    if (minimized) {
        if (w->minimized) { ++w->ignored; return 0; }
        w->minimized = 1;
        return send(w, ES2_WM_SIZE, 1u /* SIZE_MINIMIZED */, 0);
    }
    if (!w->minimized) { ++w->ignored; return 0; }
    w->minimized = 0;
    w->paint_pending = 1;
    return send(w, ES2_WM_SIZE, 0u /* SIZE_RESTORED */, point(w->client_w, w->client_h));
}
int es2_sim_window_move_event(es2_sim_window *w, int32_t x, int32_t y) {
    w->origin_x = x;
    w->origin_y = y;
    return 0;
}

unsigned es2_sim_window_drain(es2_sim_window *w) {
    return w->open && w->backend.drain ? w->backend.drain(w) : 0u;
}

/* ---- the loop's seam */
static int op_peek(void *user, es2_sim_loop *l, uint32_t *message, uint32_t *wparam, uint32_t *lparam) {
    es2_sim_window *w = user;
    if (w->open && w->backend.pump && w->backend.pump(w)) return -1;
    /* Deviation: the original's ShowWindow activates the new
     * window, so WM_SETFOCUS (the resume 0x45f108) comes once before the sound
     * is set up, and from then on WM_KILLFOCUS and WM_SETFOCUS alternate.
     * Windows may give this window the focus late (started behind another
     * window) or not at all; a first WM_SETFOCUS after the sound's set-up would
     * make the resume 0x463238 start every sound row whose byte p[7] no pause
     * has written (it overlaps the next row's record: nonzero), the endless
     * loops among them, until all 32 mixer voices are taken. So the activation
     * is sent at the first peek when the backend has not reported the focus by
     * then, and the guest gets only alternating focus messages. */
    if (w->focus_pending && !w->guest_focus) {
        w->focus_pending = 0;
        w->guest_focus = 1;
        if (send(w, ES2_WM_SETFOCUS, 0, 0)) return -1;
    }
    w->focus_pending = 0;
    if (w->count) {
        *message = w->queue[w->head].message;
        *wparam = w->queue[w->head].wparam;
        *lparam = w->queue[w->head].lparam;
        w->head = (w->head + 1u) % ES2_SIM_WINDOW_QUEUE;
        --w->count;
        key_retrieved(w, *message, *wparam, *lparam);
        return 1;
    }
    /* WM_PAINT after the input and after a posted WM_QUIT; retrieved once
     * per invalidation (the present validates). */
    if (w->paint_pending && !l->quit_posted) {
        w->paint_pending = 0;
        ++w->paints;
        *message = ES2_WM_PAINT;
        *wparam = 0;
        *lparam = 0;
        return 1;
    }
    return 0;
}
static void op_set_cursor(void *user, uint32_t handle) {
    es2_sim_window *w = user;
    if (!w->open || !w->backend.set_cursor) return;
    w->backend.set_cursor(w, handle == w->cursor_wait);
}
static void op_sleep(void *user, uint32_t ms) {
    es2_sim_window *w = user;
    ++w->sleeps;
    w->slept_ms += ms;
    w->delay_ms(w->time_user, ms);
}
/* Virtual time follows the wall clock: before a poll the clock moves up to
 * the wall clock's time since the base (a slow frame's elapsed time is the
 * real one, as GetTickCount's); after it the pace waits until the wall
 * clock reaches the clock (a fast frame lasts the step). More than a
 * second ahead (a long run of clock reads without a poll) rebases instead
 * of sleeping. */
static void rebase(es2_sim_window *w, uint64_t now, uint32_t clock) {
    w->clock_based = 1;
    w->wall_base = now;
    w->clock_base = clock;
}
static uint32_t wall_clock(const es2_sim_window *w, uint64_t now) { return w->clock_base + (uint32_t)(now - w->wall_base); }
static void op_clock(void *user, es2_vt *vt) {
    es2_sim_window *w = user;
    uint64_t now = w->now_ms(w->time_user);
    if (!w->clock_based) { rebase(w, now, vt->clock_ms); return; }
    uint32_t wall = wall_clock(w, now);
    if ((int32_t)(wall - vt->clock_ms) > 0) { vt->clock_ms = wall; ++w->catch_ups; }
}
/* The pace has no clock argument: it reads the loop's. */
static void op_pace(void *user) {
    es2_sim_window *w = user;
    uint64_t now = w->now_ms(w->time_user);
    uint32_t clock = w->loop->vt->clock_ms;
    if (!w->clock_based) { rebase(w, now, clock); return; }
    int32_t ahead = (int32_t)(clock - wall_clock(w, now));
    if (ahead <= 0) return;
    if (ahead > 1000) { rebase(w, now, clock); ++w->rebases; return; }
    ++w->paces;
    w->delay_ms(w->time_user, (uint32_t)ahead);
}
/* DefWindowProcA for the messages the window procedure leaves to it:
 * Alt+F4 sends WM_SYSCOMMAND(SC_CLOSE), whose default sends WM_CLOSE; Alt
 * released alone and F10 send WM_SYSCOMMAND(SC_KEYMENU), whose default would
 * enter the system menu's modal loop (not modelled: counted; a visible run
 * with injected F10 showed no loop holding the main thread, a real keypress
 * is not measured). Everything else has no effect
 * on the game. */
static int op_def_window_proc(void *user, es2_sim_loop *l, uint32_t message, uint32_t wparam, uint32_t lparam, uint32_t *result) {
    es2_sim_window *w = user;
    (void)l;
    *result = 0;
    switch (message) {
    case ES2_WM_SYSKEYDOWN:
        w->alt_alone = wparam == VK_MENU_;
        if (wparam == VK_F4_ && (lparam & (1u << 29))) { ++w->closes_alt_f4; return send(w, WM_SYSCOMMAND_, SC_CLOSE_, 0); }
        return 0;
    case ES2_WM_KEYDOWN: w->alt_alone = 0; return 0;
    case ES2_WM_SYSKEYUP:
        if (wparam == VK_F10_ || (wparam == VK_MENU_ && w->alt_alone)) {
            w->alt_alone = 0;
            return send(w, WM_SYSCOMMAND_, SC_KEYMENU_, 0);
        }
        return 0;
    case WM_SYSCOMMAND_:
        if ((wparam & 0xfff0u) == SC_CLOSE_) return send(w, ES2_WM_CLOSE, 0, 0);
        if ((wparam & 0xfff0u) == SC_KEYMENU_) ++w->menu_keys;
        return 0;
    default: return 0;
    }
}

/* ---- the input seam */
static int16_t in_key_state(void *user, uint32_t frame, int32_t vk) {
    (void)frame;
    const es2_sim_window *w = user;
    uint8_t k = w->keys[(uint32_t)vk & 0xffu];
    return (int16_t)((k & 0x80u ? 0xff80u : 0u) | (k & 1u));
}
static uint32_t in_joy_caps(void *user, uint32_t frame, uint32_t id, uint8_t *caps, uint32_t size) {
    (void)frame;
    es2_sim_window *w = user;
    return w->joystick.caps(w->joystick.user, id, caps, size);
}
static uint32_t in_joy_pos(void *user, uint32_t frame, uint32_t id, uint8_t *info, uint32_t size) {
    (void)frame;
    es2_sim_window *w = user;
    return w->joystick.pos(w->joystick.user, id, info, size);
}
/* GetCursorPos through the backend (desktop coordinates; full screen the
 * game's letterboxed ones, presentation.md, "The DirectDraw full-screen
 * switch"). */
static uint32_t in_cursor(void *user, uint32_t frame, int32_t *x, int32_t *y) {
    (void)frame;
    const es2_sim_window *w = user;
    *x = 0;
    *y = 0;
    return w->backend.cursor_pos ? w->backend.cursor_pos(w, x, y) : 0u;
}
static void in_client_size(void *user, int32_t *width, int32_t *height) {
    const es2_sim_window *w = user;
    if (w->minimized) { *width = 0; *height = 0; return; }
    *width = w->client_w ? w->client_w : (int32_t)r32(w, VA_GAME_W);
    *height = w->client_h ? w->client_h : (int32_t)r32(w, VA_GAME_H);
}

/* ---- the present */
void es2_sim_window_convert(const uint8_t *bits, int32_t stride, int32_t w, int32_t h, const uint8_t *table, uint32_t *out) {
    for (int32_t y = 0; y < h; ++y) {
        const uint8_t *row = bits + (size_t)y * (size_t)stride;
        for (int32_t x = 0; x < w; ++x) {
            const uint8_t *q = table + 4u * row[x];   /* RGBQUAD: blue, green, red, reserved */
            out[(size_t)y * (size_t)w + (size_t)x] = (uint32_t)q[2] << 16 | (uint32_t)q[1] << 8 | q[0];
        }
    }
}
void es2_sim_window_convert_rgba(const uint8_t *bits, int32_t stride, int32_t w, int32_t h, const uint8_t *table,
                                 uint8_t *out) {
    for (int32_t y = 0; y < h; ++y) {
        const uint8_t *row = bits + (size_t)y * (size_t)stride;
        uint8_t *o = out + (size_t)y * (size_t)w * 4u;
        for (int32_t x = 0; x < w; ++x) {
            const uint8_t *q = table + 4u * row[x];
            o[4 * x] = q[2];
            o[4 * x + 1] = q[1];
            o[4 * x + 2] = q[0];
            o[4 * x + 3] = 0xffu;
        }
    }
}
static void on_present(void *user, const es2_sim_present_record *r) {
    es2_sim_window *w = user;
    ++w->presents;
    w->paint_pending = 0;   /* BeginPaint validated the client area */
    if (!w->open || !w->backend.show) return;
    if (strcmp(r->source, "dib")) { ++w->presents_outside; return; }
    int32_t sw = r->src[2], sh = r->src[3], stride = (int32_t)r32(w, VA_DIB_STRIDE);
    /* StretchBlt with an empty destination draws nothing (a negative extent
     * would mirror; no present computes one, so it is not drawn either). DBSIM
     * never sets a stretch mode (no SetStretchBltMode import), so a scaled
     * present would use the paint DC's default; this window keeps the game
     * size, so every present is 1:1, and a scaled one is counted and drawn with
     * nearest scaling (own_process.md, "the window's open points"). */
    if (r->dst[2] <= 0 || r->dst[3] <= 0) { ++w->presents_empty; return; }
    if (r->dst[2] != sw || r->dst[3] != sh) ++w->presents_scaled;
    uint32_t bits = r32(w, VA_DIB_BITS);
    const es2_sim_load_platform *table = es2_sim_present_table(w->present);
    if (!table || sw <= 0 || sh <= 0) return;
    if ((uint64_t)sw * (uint64_t)sh > w->pixels_capacity) {
        uint32_t *p = realloc(w->pixels, (size_t)sw * (size_t)sh * sizeof(uint32_t));
        if (!p) return;
        w->pixels = p;
        w->pixels_capacity = (uint32_t)sw * (uint32_t)sh;
    }
    const es2_guest_memory *m = w->loop->memory;
    for (int32_t y = 0; y < sh; ++y) {
        uint32_t at = bits + (uint32_t)(r->src[1] + y) * (uint32_t)stride + (uint32_t)r->src[0];
        const uint8_t *row = m->host(m->context, at), *last = m->host(m->context, at + (uint32_t)sw - 1u);
        if (!row || last != row + sw - 1) return;   /* the row must be one mapped run */
        if (w->pixel_format == ES2_SIM_WINDOW_RGBA)
            es2_sim_window_convert_rgba(row, stride, sw, 1, table->color_table, (uint8_t *)(w->pixels + (size_t)y * (size_t)sw));
        else
            es2_sim_window_convert(row, stride, sw, 1, table->color_table, w->pixels + (size_t)y * (size_t)sw);
    }
    w->backend.show(w, sw, sh, r->dst);
}

/* ---- the cursor rows (es2/sim/cursor.h). ClientToScreen adds the
 * client origin on the desktop (the process is DPI-unaware, as the original,
 * so it is GetCursorPos's space); SetCursorPos moves the cursor, whose move
 * comes back as a motion event (WM_MOUSEMOVE) where the backend's cursor
 * follows, as Windows posts one. */
static int letterboxed(const es2_sim_window *w) {
    return w->fullscreen && w->open && w->backend.letterboxed && w->backend.letterboxed(w);
}
static uint32_t cursor_to_screen(void *user, uint32_t hwnd, int32_t *x, int32_t *y) {
    (void)hwnd;
    const es2_sim_window *w = user;
    if (letterboxed(w)) return 1;   /* full screen: the client is the game's screen */
    *x += w->origin_x;
    *y += w->origin_y;
    return 1;
}
static uint32_t cursor_set(void *user, int32_t x, int32_t y) {
    es2_sim_window *w = user;
    ++w->cursor_warps;
    if (letterboxed(w) && w->backend.game_to_desktop) w->backend.game_to_desktop(w, &x, &y);
    return w->set_cursor_pos(w, x, y);
}

/* ---- the full-screen switch's request (present.h, display_mode). */
static void display_mode(void *user, int fullscreen) {
    es2_sim_window *w = user;
    ++w->display_switches;
    w->fullscreen = fullscreen;
    if (!w->open || !w->backend.display_mode) { ++w->display_failures; return; }
    if (!w->backend.display_mode(w, fullscreen)) ++w->display_failures;
}

/* ---- set-up */
static uint32_t no_cursor_pos(void *user, int32_t x, int32_t y) {
    (void)user; (void)x; (void)y;
    return 0;
}
void es2_sim_window_core_init(es2_sim_window *w, es2_sim_loop *loop, es2_sim_input *input, es2_sim_present *present) {
    memset(w, 0, sizeof(*w));
    w->loop = loop;
    w->input = input;
    w->present = present;
    w->cursor_arrow = loop->platform.cursor_arrow;
    w->cursor_wait = 0x00010003u;   /* the start-up's wait-cursor stand-in (es2_sim_startup_defaults) */
    w->ops.peek = op_peek;
    w->ops.set_cursor = op_set_cursor;
    w->ops.sleep = op_sleep;
    w->ops.clock = op_clock;
    w->ops.pace = op_pace;
    w->ops.def_window_proc = op_def_window_proc;
    w->ops.user = w;
    w->set_cursor_pos = no_cursor_pos;
    w->cursor.client_to_screen = cursor_to_screen;
    w->cursor.set_pos = cursor_set;
    w->cursor.user = w;
    es2_sim_cursor_bind(&w->cursor);
    loop->window = &w->ops;
    if (input) {
        input->platform.key_state = in_key_state;
        input->platform.joy_caps = in_joy_caps;
        input->platform.joy_pos = in_joy_pos;
        input->platform.cursor_pos = in_cursor;
        input->platform.client_size = in_client_size;
        input->platform.user = w;
    }
    if (present) {
        present->on_present = on_present;
        present->user = w;
        present->platform.display_mode = display_mode;
        present->platform.display_user = w;
    }
}
