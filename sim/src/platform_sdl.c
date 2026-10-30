/* The windowed platform over SDL3; see es2/sim/platform_sdl.h and own_process.md, "the windowed
 * platform layer" (AltGr's fake Ctrl, the key state at activation, the cursor rows, empty and scaled
 * presents). The window procedure, the pump and the input functions stay the own code of loop.c and
 * input.c; this file only produces what Windows would hand them (messages, key states, the client rectangle, the
 * cursor position, the joystick answers) and shows what the present drew. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "es2/sim/platform_sdl.h"

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
#define VK_NUMLOCK_ 0x90u
#define VK_CAPITAL_ 0x14u
#define VK_SCROLL_ 0x91u
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

/* ---- keys: an SDL scancode (USB HID usage) to the PC/AT set-1 scan code
 * and the extended flag that Windows puts into a key message's lParam, then
 * the layout's virtual-key code for that scan code. */
static const struct { uint8_t hid, scan, extended; } scan_table[] = {
    {4, 0x1e, 0}, {5, 0x30, 0}, {6, 0x2e, 0}, {7, 0x20, 0}, {8, 0x12, 0}, {9, 0x21, 0}, {10, 0x22, 0},
    {11, 0x23, 0}, {12, 0x17, 0}, {13, 0x24, 0}, {14, 0x25, 0}, {15, 0x26, 0}, {16, 0x32, 0}, {17, 0x31, 0},
    {18, 0x18, 0}, {19, 0x19, 0}, {20, 0x10, 0}, {21, 0x13, 0}, {22, 0x1f, 0}, {23, 0x14, 0}, {24, 0x16, 0},
    {25, 0x2f, 0}, {26, 0x11, 0}, {27, 0x2d, 0}, {28, 0x15, 0}, {29, 0x2c, 0},
    {30, 0x02, 0}, {31, 0x03, 0}, {32, 0x04, 0}, {33, 0x05, 0}, {34, 0x06, 0}, {35, 0x07, 0}, {36, 0x08, 0},
    {37, 0x09, 0}, {38, 0x0a, 0}, {39, 0x0b, 0},
    {40, 0x1c, 0}, {41, 0x01, 0}, {42, 0x0e, 0}, {43, 0x0f, 0}, {44, 0x39, 0}, {45, 0x0c, 0}, {46, 0x0d, 0},
    {47, 0x1a, 0}, {48, 0x1b, 0}, {49, 0x2b, 0}, {50, 0x2b, 0}, {51, 0x27, 0}, {52, 0x28, 0}, {53, 0x29, 0},
    {54, 0x33, 0}, {55, 0x34, 0}, {56, 0x35, 0}, {57, 0x3a, 0},
    {58, 0x3b, 0}, {59, 0x3c, 0}, {60, 0x3d, 0}, {61, 0x3e, 0}, {62, 0x3f, 0}, {63, 0x40, 0}, {64, 0x41, 0},
    {65, 0x42, 0}, {66, 0x43, 0}, {67, 0x44, 0}, {68, 0x57, 0}, {69, 0x58, 0},
    {70, 0x37, 1}, {71, 0x46, 0}, {72, 0x45, 0},
    {73, 0x52, 1}, {74, 0x47, 1}, {75, 0x49, 1}, {76, 0x53, 1}, {77, 0x4f, 1}, {78, 0x51, 1},
    {79, 0x4d, 1}, {80, 0x4b, 1}, {81, 0x50, 1}, {82, 0x48, 1},
    {83, 0x45, 1}, {84, 0x35, 1}, {85, 0x37, 0}, {86, 0x4a, 0}, {87, 0x4e, 0}, {88, 0x1c, 1},
    {89, 0x4f, 0}, {90, 0x50, 0}, {91, 0x51, 0}, {92, 0x4b, 0}, {93, 0x4c, 0}, {94, 0x4d, 0}, {95, 0x47, 0},
    {96, 0x48, 0}, {97, 0x49, 0}, {98, 0x52, 0}, {99, 0x53, 0}, {100, 0x56, 0}, {101, 0x5d, 1},
    {224, 0x1d, 0}, {225, 0x2a, 0}, {226, 0x38, 0}, {227, 0x5b, 1}, {228, 0x1d, 1}, {229, 0x36, 0},
    {230, 0x38, 1}, {231, 0x5c, 1},
};
/* The keypad keys 89..99 (1..9, 0, decimal point): with Num Lock the
 * VK_NUMPAD codes, without it the cursor block's (as the keyboard driver
 * translates them). */
static const uint8_t keypad_on[11] = {0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x60, 0x6e};
static const uint8_t keypad_off[11] = {0x23, 0x28, 0x22, 0x25, 0x0c, 0x27, 0x24, 0x26, 0x21, 0x2d, 0x2e};

uint32_t es2_sim_window_vk(uint32_t sdl_scancode, int numlock, uint32_t *scan, int *extended) {
    unsigned i = 0, n = sizeof(scan_table) / sizeof(scan_table[0]);
    while (i < n && scan_table[i].hid != sdl_scancode) ++i;
    if (i == n) return 0;
    *scan = scan_table[i].scan;
    *extended = scan_table[i].extended;
    if (sdl_scancode >= 89u && sdl_scancode <= 99u) return (numlock ? keypad_on : keypad_off)[sdl_scancode - 89u];
    switch (sdl_scancode) {
    case 72: return 0x13u;            /* Pause */
    case 83: return VK_NUMLOCK_;
    case 84: return 0x6fu;            /* keypad / */
    case 85: return 0x6au;            /* keypad * */
    case 86: return 0x6du;            /* keypad - */
    case 87: return 0x6bu;            /* keypad + */
    case 88: return 0x0du;            /* keypad Enter: VK_RETURN, extended */
    case 70: return 0x2cu;            /* Print Screen */
    case 224: case 228: return VK_CONTROL_;
    case 225: case 229: return VK_SHIFT_;
    case 226: case 230: return VK_MENU_;
    default: break;
    }
    /* The layout's code for the scan code (letters, digits, OEM keys, the
     * function keys and the cursor block). */
    UINT code = scan_table[i].scan | (scan_table[i].extended ? 0xe000u : 0u);
    return MapVirtualKeyW(code, 3 /* MAPVK_VSC_TO_VK_EX */) & 0xffu;
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
/* The default: GetKeyboardState, the thread's own state, which Windows has
 * synchronised by the time the activation reaches SDL. */
static void sync_from_thread(void *user, uint8_t state[256]) {
    (void)user;
    if (!GetKeyboardState(state)) memset(state, 0, 256);
}
/* Whether the current layout has AltGr (KLLF_ALTGR stands behind the fake
 * Ctrl): any key that gives a character with Ctrl and right Alt down. */
int es2_sim_window_layout_altgr(void) {
    HKL layout = GetKeyboardLayout(0);
    BYTE state[256];
    memset(state, 0, sizeof(state));
    state[0x11] = state[0x12] = state[0xa2] = state[0xa5] = 0x80u;   /* Ctrl, Alt, left Ctrl, right Alt */
    for (UINT vk = 0x30; vk <= 0xe2; ++vk) {
        WCHAR out[4];
        UINT scan = MapVirtualKeyExW(vk, 0 /* MAPVK_VK_TO_VSC */, layout);
        if (!scan) continue;
        int n = ToUnicodeEx(vk, scan, state, out, 4, 4u /* leave the dead-key state alone */, layout);
        if (n > 0 && out[0] >= 0x20u) return 1;
    }
    return 0;
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
static int key_event(es2_sim_window *w, const SDL_KeyboardEvent *k) {
    if (!w->focused) { ++w->ignored; return 0; }   /* keys go to the focus window only */
    uint32_t scan = 0;
    int extended = 0;
    uint32_t vk = es2_sim_window_vk((uint32_t)k->scancode, w->keys[VK_NUMLOCK_] & 1u, &scan, &extended);
    if (!vk) { ++w->ignored; return 0; }
    /* With a layout that has AltGr, Windows puts a left Ctrl (scan 0x1d, not
     * extended) before every right-Alt key-down, repeats included, and its
     * key-up before the right Alt's; SDL leaves it out. Added only when no Ctrl
     * is down already (a delivered left Ctrl is not doubled). Hypothesis from
     * the documented behaviour, not measured (own_process.md, "the window's
     * open points"). */
    int right_alt = k->scancode == SDL_SCANCODE_RALT && w->altgr;
    if (right_alt && k->down && (w->altgr_ctrl || !async_down(w, VK_CONTROL_))) {
        w->altgr_ctrl = 1;
        ++w->altgr_ctrls;
        key_message(w, VK_CONTROL_, 0x1du, 0, 1);
    }
    if (right_alt && !k->down && w->altgr_ctrl) {
        w->altgr_ctrl = 0;
        key_message(w, VK_CONTROL_, 0x1du, 0, 0);
    }
    key_message(w, vk, scan, extended, k->down);
    return 0;
}
static int motion_event(es2_sim_window *w, const SDL_MouseMotionEvent *m) {
    int32_t x = (int32_t)SDL_floorf(m->x), y = (int32_t)SDL_floorf(m->y);
    if (!in_client(w, x, y)) { ++w->ignored; return 0; }   /* no capture: moves outside reach other windows */
    w->mouse_x = x;
    w->mouse_y = y;
    enqueue(w, ES2_WM_MOUSEMOVE, mouse_flags(w), ((uint32_t)y & 0xffffu) << 16 | ((uint32_t)x & 0xffffu));
    return 0;
}
static int button_event(es2_sim_window *w, const SDL_MouseButtonEvent *b) {
    uint32_t flag, down_message;
    switch (b->button) {
    case SDL_BUTTON_LEFT: flag = MK_LBUTTON_; down_message = ES2_WM_LBUTTONDOWN; break;
    case SDL_BUTTON_RIGHT: flag = MK_RBUTTON_; down_message = ES2_WM_RBUTTONDOWN; break;
    case SDL_BUTTON_MIDDLE: flag = MK_MBUTTON_; down_message = ES2_WM_MBUTTONDOWN; break;
    default: ++w->ignored; return 0;
    }
    if (b->down) w->buttons |= flag;
    else w->buttons &= ~flag;
    int32_t x = (int32_t)SDL_floorf(b->x), y = (int32_t)SDL_floorf(b->y);
    if (!in_client(w, x, y)) { ++w->ignored; return 0; }
    /* The class has no CS_DBLCLKS: a second click is another down. */
    enqueue(w, b->down ? down_message : down_message + 1u, mouse_flags(w), ((uint32_t)y & 0xffffu) << 16 | ((uint32_t)x & 0xffffu));
    return 0;
}

int es2_sim_window_event(es2_sim_window *w, const SDL_Event *e) {
    if (!w->window) { ++w->ignored; return 0; }
    /* Full screen: the mouse events in the game's letterboxed coordinates. */
    SDL_Event mapped;
    if (w->fullscreen && w->renderer &&
        (e->type == SDL_EVENT_MOUSE_MOTION || e->type == SDL_EVENT_MOUSE_BUTTON_DOWN || e->type == SDL_EVENT_MOUSE_BUTTON_UP)) {
        mapped = *e;
        SDL_ConvertEventToRenderCoordinates(w->renderer, &mapped);
        e = &mapped;
    }
    switch (e->type) {
    case SDL_EVENT_KEY_DOWN: case SDL_EVENT_KEY_UP:
        if (e->key.windowID != w->window_id) break;
        return key_event(w, &e->key);
    case SDL_EVENT_MOUSE_MOTION:
        if (e->motion.windowID != w->window_id) break;
        return motion_event(w, &e->motion);
    case SDL_EVENT_MOUSE_BUTTON_DOWN: case SDL_EVENT_MOUSE_BUTTON_UP:
        if (e->button.windowID != w->window_id) break;
        return button_event(w, &e->button);
    case SDL_EVENT_MOUSE_WHEEL: {
        if (e->wheel.windowID != w->window_id || !w->focused) break;
        int32_t delta = (int32_t)(e->wheel.y * 120.0f);
        if (!delta) break;
        int32_t x = w->origin_x + w->mouse_x, y = w->origin_y + w->mouse_y;   /* screen coordinates */
        enqueue(w, ES2_WM_MOUSEWHEEL, ((uint32_t)delta & 0xffffu) << 16 | (mouse_flags(w) & 0xffffu),
                ((uint32_t)y & 0xffffu) << 16 | ((uint32_t)x & 0xffffu));
        return 0;
    }
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
        if (e->window.windowID != w->window_id || w->focused) break;
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
        if (w->guest_focus) break;
        w->guest_focus = 1;
        return send(w, ES2_WM_SETFOCUS, 0, 0);
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        if (e->window.windowID != w->window_id || !w->focused) break;
        w->focused = 0;
        memset(w->async, 0, sizeof(w->async));
        if (!w->guest_focus) break;
        w->guest_focus = 0;
        return send(w, ES2_WM_KILLFOCUS, 0, 0);
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        if (e->window.windowID != w->window_id) break;
        return send(w, ES2_WM_CLOSE, 0, 0);
    case SDL_EVENT_WINDOW_EXPOSED:
        if (e->window.windowID != w->window_id) break;
        w->paint_pending = 1;
        return 0;
    case SDL_EVENT_WINDOW_MINIMIZED:
        if (e->window.windowID != w->window_id || w->minimized) break;
        w->minimized = 1;
        return send(w, ES2_WM_SIZE, 1u /* SIZE_MINIMIZED */, 0);
    case SDL_EVENT_WINDOW_RESTORED:
        if (e->window.windowID != w->window_id || !w->minimized) break;
        w->minimized = 0;
        w->paint_pending = 1;
        return send(w, ES2_WM_SIZE, 0u /* SIZE_RESTORED */, ((uint32_t)w->client_h & 0xffffu) << 16 | ((uint32_t)w->client_w & 0xffffu));
    case SDL_EVENT_WINDOW_MOVED:
        if (e->window.windowID != w->window_id) break;
        w->origin_x = e->window.data1;
        w->origin_y = e->window.data2;
        return 0;
    default: break;
    }
    ++w->ignored;
    return 0;
}

/* ---- the loop's seam */
static int op_peek(void *user, es2_sim_loop *l, uint32_t *message, uint32_t *wparam, uint32_t *lparam) {
    es2_sim_window *w = user;
    if (w->window) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ++w->events;
            if (es2_sim_window_event(w, &e)) return -1;
        }
    }
    /* Deviation: the original's ShowWindow activates the new
     * window, so WM_SETFOCUS (the resume 0x45f108) comes once before the sound
     * is set up, and from then on WM_KILLFOCUS and WM_SETFOCUS alternate.
     * Windows may give this window the focus late (started behind another
     * window) or not at all; a first WM_SETFOCUS after the sound's set-up would
     * make the resume 0x463238 start every sound row whose byte p[7] no pause
     * has written (it overlaps the next row's record: nonzero), the endless
     * loops among them, until all 32 mixer voices are taken. So the activation
     * is sent at the first peek when SDL has not reported the focus by then,
     * and the guest gets only alternating focus messages. */
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
    if (!w->window) return;
    SDL_Cursor *c = handle == w->cursor_wait ? w->wait : w->arrow;
    if (c && SDL_GetCursor() != c) { SDL_SetCursor(c); ++w->cursors; }
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
/* GetCursorPos: the original's own call (the process is DPI-unaware, so
 * the point is in the same desktop coordinates as the window's origin). */
/* Full screen: the desktop point in the game's letterboxed coordinates
 * (the original's display mode is the game size, so its screen is the game's;
 * presentation.md, "The DirectDraw full-screen switch"). */
static void desktop_to_game(const es2_sim_window *w, int32_t *x, int32_t *y) {
    int wx = 0, wy = 0;
    float gx = 0, gy = 0;
    SDL_GetWindowPosition(w->window, &wx, &wy);
    if (!SDL_RenderCoordinatesFromWindow(w->renderer, (float)(*x - wx), (float)(*y - wy), &gx, &gy)) return;
    *x = (int32_t)SDL_floorf(gx);
    *y = (int32_t)SDL_floorf(gy);
}
static void game_to_desktop(const es2_sim_window *w, int32_t *x, int32_t *y) {
    int wx = 0, wy = 0;
    float dx = 0, dy = 0;
    SDL_GetWindowPosition(w->window, &wx, &wy);
    if (!SDL_RenderCoordinatesToWindow(w->renderer, (float)*x, (float)*y, &dx, &dy)) return;
    *x = (int32_t)SDL_floorf(dx) + wx;
    *y = (int32_t)SDL_floorf(dy) + wy;
}
static int mapped(const es2_sim_window *w) { return w->fullscreen && w->window && w->renderer; }
static uint32_t in_cursor(void *user, uint32_t frame, int32_t *x, int32_t *y) {
    (void)frame;
    const es2_sim_window *w = user;
    POINT p = {0, 0};
    BOOL ok = GetCursorPos(&p);
    *x = (int32_t)p.x;
    *y = (int32_t)p.y;
    if (ok && mapped(w)) desktop_to_game(w, x, y);
    return ok ? 1u : 0u;
}
static void in_client_size(void *user, int32_t *width, int32_t *height) {
    const es2_sim_window *w = user;
    if (w->minimized) { *width = 0; *height = 0; return; }
    *width = w->client_w ? w->client_w : (int32_t)r32(w, VA_GAME_W);
    *height = w->client_h ? w->client_h : (int32_t)r32(w, VA_GAME_H);
}
static uint32_t winmm_caps(void *user, uint32_t id, uint8_t *caps, uint32_t size) {
    (void)user;
    return joyGetDevCapsA((UINT_PTR)id, (LPJOYCAPSA)caps, size);
}
static uint32_t winmm_pos(void *user, uint32_t id, uint8_t *info, uint32_t size) {
    (void)user;
    if (!info || size < sizeof(JOYINFOEX)) return 165u;   /* JOYERR_PARMS */
    return joyGetPosEx(id, (LPJOYINFOEX)info);
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
static void on_present(void *user, const es2_sim_present_record *r) {
    es2_sim_window *w = user;
    ++w->presents;
    w->paint_pending = 0;   /* BeginPaint validated the client area */
    if (!w->renderer) return;
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
        es2_sim_window_convert(row, stride, sw, 1, table->color_table, w->pixels + (size_t)y * (size_t)sw);
    }
    if (!w->texture || w->texture_w != sw || w->texture_h != sh) {
        if (w->texture) SDL_DestroyTexture(w->texture);
        w->texture = SDL_CreateTexture(w->renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, sw, sh);
        if (!w->texture) return;
        SDL_SetTextureScaleMode(w->texture, SDL_SCALEMODE_NEAREST);
        w->texture_w = sw;
        w->texture_h = sh;
    }
    SDL_UpdateTexture(w->texture, NULL, w->pixels, sw * (int)sizeof(uint32_t));
    SDL_FRect dst = {(float)r->dst[0], (float)r->dst[1], (float)r->dst[2], (float)r->dst[3]};
    SDL_SetRenderDrawColor(w->renderer, 0, 0, 0, 255);
    SDL_RenderClear(w->renderer);
    SDL_RenderTexture(w->renderer, w->texture, NULL, &dst);
    SDL_RenderPresent(w->renderer);
}

/* ---- the cursor rows (es2/sim/cursor.h). ClientToScreen adds the
 * client origin on the desktop (the process is DPI-unaware, as the original,
 * so it is GetCursorPos's space); SetCursorPos moves the real cursor, whose
 * move comes back as an SDL motion event (WM_MOUSEMOVE), as Windows posts
 * one. */
static uint32_t cursor_to_screen(void *user, uint32_t hwnd, int32_t *x, int32_t *y) {
    (void)hwnd;
    const es2_sim_window *w = user;
    if (mapped(w)) return 1;   /* full screen: the client is the game's screen */
    *x += w->origin_x;
    *y += w->origin_y;
    return 1;
}
static uint32_t cursor_set(void *user, int32_t x, int32_t y) {
    es2_sim_window *w = user;
    ++w->cursor_warps;
    if (mapped(w)) game_to_desktop(w, &x, &y);
    return w->set_cursor_pos(w, x, y);
}

/* ---- the full-screen switch's request (present.h, display_mode). Platform
 * choice: the original's exclusive 640 x 480 8-bit mode becomes SDL's
 * desktop full screen with the frame scaled into a letterbox; the way back
 * is SDL's windowed geometry (the original's SetWindowPos to 0x4d36cc). */
static void display_mode(void *user, int fullscreen) {
    es2_sim_window *w = user;
    ++w->display_switches;
    w->fullscreen = fullscreen;
    if (!w->window || !w->renderer) { ++w->display_failures; return; }
    int ok = SDL_SetWindowFullscreenMode(w->window, NULL);
    ok = SDL_SetWindowFullscreen(w->window, fullscreen ? true : false) && ok;
    ok = SDL_SetRenderLogicalPresentation(w->renderer, fullscreen ? w->game_w : 0, fullscreen ? w->game_h : 0,
                                          fullscreen ? SDL_LOGICAL_PRESENTATION_LETTERBOX : SDL_LOGICAL_PRESENTATION_DISABLED) && ok;
    SDL_SyncWindow(w->window);
    if (!fullscreen) {
        int px = 0, py = 0;
        if (SDL_GetWindowPosition(w->window, &px, &py)) { w->origin_x = px; w->origin_y = py; }
    }
    if (!ok) ++w->display_failures;
}
static uint32_t user32_set_cursor_pos(void *user, int32_t x, int32_t y) {
    (void)user;
    return SetCursorPos(x, y) ? 1u : 0u;
}

/* ---- set-up */
static uint64_t sdl_now(void *user) { (void)user; return SDL_GetTicks(); }
static void sdl_delay(void *user, uint32_t ms) { (void)user; SDL_Delay(ms); }

void es2_sim_window_init(es2_sim_window *w, es2_sim_loop *loop, es2_sim_input *input, es2_sim_present *present) {
    memset(w, 0, sizeof(*w));
    w->loop = loop;
    w->input = input;
    w->present = present;
    w->now_ms = sdl_now;
    w->delay_ms = sdl_delay;
    w->joystick.caps = winmm_caps;
    w->joystick.pos = winmm_pos;
    w->cursor_arrow = loop->platform.cursor_arrow;
    w->cursor_wait = 0x00010003u;   /* the start-up's wait-cursor stand-in (es2_sim_startup_defaults) */
    w->ops.peek = op_peek;
    w->ops.set_cursor = op_set_cursor;
    w->ops.sleep = op_sleep;
    w->ops.clock = op_clock;
    w->ops.pace = op_pace;
    w->ops.def_window_proc = op_def_window_proc;
    w->ops.user = w;
    w->key_sync = sync_from_thread;
    w->set_cursor_pos = user32_set_cursor_pos;
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

int es2_sim_window_video_init(es2_sim_window *w) {
    /* Alt+F4 and the close button are the game's (WM_CLOSE asks the quit
     * dialog); the original declares no DPI awareness; it never captures
     * the mouse; Ctrl+C in the console ends the process as before. */
    SDL_SetHint(SDL_HINT_WINDOWS_CLOSE_ON_ALT_F4, "0");
    SDL_SetHint(SDL_HINT_QUIT_ON_LAST_WINDOW_CLOSE, "0");
    SDL_SetHint("SDL_WINDOWS_DPI_AWARENESS", "unaware");
    SDL_SetHint(SDL_HINT_MOUSE_AUTO_CAPTURE, "0");
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
    SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        SDL_snprintf(w->error, sizeof(w->error), "SDL video: %s", SDL_GetError());
        return 1;
    }
    return 0;
}

void es2_sim_window_metrics(uint32_t *screen_cx, uint32_t *screen_cy, uint32_t *frame_cx, uint32_t *frame_cy, uint32_t *caption) {
    *screen_cx = (uint32_t)GetSystemMetrics(SM_CXSCREEN);
    *screen_cy = (uint32_t)GetSystemMetrics(SM_CYSCREEN);
    *frame_cx = (uint32_t)GetSystemMetrics(SM_CXFRAME);
    *frame_cy = (uint32_t)GetSystemMetrics(SM_CYFRAME);
    *caption = (uint32_t)GetSystemMetrics(SM_CYCAPTION);
}

/* LoadIconA(hInstance, name) from the own resources (es2_sim_resource_own): no
 * read of a mapped .rsrc. The file-based twin for the tests is
 * es2_sim_window_load_icon in image_file.c. */
void *es2_sim_window_load_icon_own(const char *name, int cx, int cy, const char **error) {
    const uint8_t *bytes = NULL;
    uint32_t size = 0;
    if (es2_sim_resource_own(ES2_SIM_RT_GROUP_ICON, name, 0, &bytes, &size, error)) return NULL;
    if (size < 6u) { *error = "group icon too short"; return NULL; }
    int id = LookupIconIdFromDirectoryEx((PBYTE)bytes, TRUE, cx, cy, LR_DEFAULTCOLOR);
    if (id <= 0) { *error = "no icon in the group"; return NULL; }
    if (es2_sim_resource_own(ES2_SIM_RT_ICON, NULL, (uint32_t)id, &bytes, &size, error)) return NULL;
    HICON icon = CreateIconFromResourceEx((PBYTE)bytes, size, TRUE, 0x00030000, cx, cy, LR_DEFAULTCOLOR);
    if (!icon) { *error = "CreateIconFromResourceEx refused the icon"; return NULL; }
    return icon;
}

/* The class icon. Platform deviation: the original names it in its WNDCLASS
 * (0x465119, LoadIconA(hInstance, "es2icon")); SDL registers its own class, so
 * the layer gives the window the same icon with WM_SETICON: the large one, and
 * the small one Windows would make from the class icon (the best image for the
 * small size). The icon comes from the own resources (sim/data), not from
 * the mapped .rsrc. */
static void window_icon(es2_sim_window *w) {
    w->icon_error = NULL;
    if (!w->icon_name) return;
    const char *error = NULL;
    w->icon_big = es2_sim_window_load_icon_own(w->icon_name, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), &error);
    if (w->icon_big)
        w->icon_small = es2_sim_window_load_icon_own(w->icon_name, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
                                                     &error);
    if (!w->icon_big || !w->icon_small) { w->icon_error = error; return; }
    HWND hwnd = (HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(w->window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
    if (!hwnd) { w->icon_error = "no Win32 window"; return; }
    SendMessageA(hwnd, WM_SETICON, ICON_BIG, (LPARAM)w->icon_big);
    SendMessageA(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)w->icon_small);
}

int es2_sim_window_open(es2_sim_window *w, int32_t width, int32_t height, int32_t x, int32_t y) {
    if (width <= 0 || height <= 0) { SDL_snprintf(w->error, sizeof(w->error), "game size %dx%d", (int)width, (int)height); return 1; }
    w->window = SDL_CreateWindow("EarthSiege II", width, height, SDL_WINDOW_HIDDEN);
    if (!w->window) { SDL_snprintf(w->error, sizeof(w->error), "SDL window: %s", SDL_GetError()); return 1; }
    w->window_id = SDL_GetWindowID(w->window);
    window_icon(w);
    SDL_SetWindowPosition(w->window, x, y);
    w->renderer = SDL_CreateRenderer(w->window, NULL);
    if (!w->renderer) {
        SDL_snprintf(w->error, sizeof(w->error), "SDL renderer: %s", SDL_GetError());
        SDL_DestroyWindow(w->window);
        w->window = NULL;
        return 1;
    }
    SDL_SetRenderVSync(w->renderer, 0);
    w->arrow = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
    w->wait = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_WAIT);
    w->client_w = w->game_w = width;
    w->client_h = w->game_h = height;
    SDL_ShowWindow(w->window);
    int px = x, py = y;
    if (SDL_GetWindowPosition(w->window, &px, &py)) { w->origin_x = px; w->origin_y = py; }
    SDL_Keymod mod = SDL_GetModState();
    w->keys[VK_CAPITAL_] = (mod & SDL_KMOD_CAPS) ? 1u : 0u;
    w->keys[VK_NUMLOCK_] = (mod & SDL_KMOD_NUM) ? 1u : 0u;
    w->keys[VK_SCROLL_] = (mod & SDL_KMOD_SCROLL) ? 1u : 0u;
    w->altgr = es2_sim_window_layout_altgr();
    /* UpdateWindow's paint after ShowWindow: retrieved by the next peek;
     * the activation too (op_peek). */
    w->paint_pending = 1;
    w->focus_pending = 1;
    return 0;
}

void es2_sim_window_close(es2_sim_window *w) {
    if (w->texture) SDL_DestroyTexture(w->texture);
    if (w->renderer) SDL_DestroyRenderer(w->renderer);
    if (w->window) SDL_DestroyWindow(w->window);
    if (w->arrow) SDL_DestroyCursor(w->arrow);
    if (w->wait) SDL_DestroyCursor(w->wait);
    if (w->icon_big) DestroyIcon((HICON)w->icon_big);
    if (w->icon_small) DestroyIcon((HICON)w->icon_small);
    w->icon_big = w->icon_small = NULL;
    free(w->pixels);
    w->texture = NULL;
    w->renderer = NULL;
    w->window = NULL;
    w->arrow = w->wait = NULL;
    w->pixels = NULL;
    w->pixels_capacity = 0;
    es2_sim_cursor_bind(NULL);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}
