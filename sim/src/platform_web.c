/* The web backend of the windowed platform layer (es2/sim/platform_web.h)
 * over the "es2sim" imports of web/es2sim_imports.mjs. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "es2/sim/platform_web.h"
#include "es2/sim/input.h"

#define IMPORT(name) __attribute__((import_module("es2sim"), import_name(#name)))
/* Milliseconds since the module's start. */
IMPORT(window_now) double web_import_now(void);
/* Waits `ms` milliseconds (Sleep: input does not end it). */
IMPORT(window_sleep) void web_import_sleep(uint32_t ms);
/* Up to `count` entries of the input ring; the number taken. */
IMPORT(window_take_input) int32_t web_import_take_input(es2_sim_web_input *out, int32_t count);
/* An RGBA picture into the next free frame slot. */
IMPORT(window_present) void web_import_present(const uint8_t *rgba, int32_t width, int32_t height);
/* SetCursor's shape: 1 the wait cursor, 0 the arrow. */
IMPORT(window_cursor) void web_import_cursor(int32_t wait);
/* The page's full screen (1) or window (0); 1, or 0 when refused. */
IMPORT(window_display) int32_t web_import_display(int32_t fullscreen);
/* SM_CXSCREEN, SM_CYSCREEN, SM_CXFRAME, SM_CYFRAME, SM_CYCAPTION. */
IMPORT(window_metrics) void web_import_metrics(uint32_t *out);
/* The key bitmap: per VK 0x80 down, 0x01 toggled. */
IMPORT(window_keys) void web_import_keys(uint8_t *out);
/* MessageBoxA's modal dialog on the page; the button (IDOK 1, ...). */
IMPORT(window_message_box) uint32_t web_import_message_box(const char *text, const char *caption, uint32_t style);

#define VK_CAPITAL_ 0x14u
#define VK_NUMLOCK_ 0x90u
#define VK_SCROLL_ 0x91u

/* One window per process: the MCI receiver and the virtual cursor. */
static struct {
    void (*mci)(void *user, uint32_t wparam, uint32_t lparam);
    void *mci_user;
    int32_t cursor_x, cursor_y;          /* GetCursorPos's point (desktop; full screen the game's) */
    int warp_pending;                    /* SetCursorPos's move, sent as a motion event at the next pump */
    int32_t warp_x, warp_y;              /* its client point */
    int cursor_shape;                    /* the shape last set: -1 none, 0 arrow, 1 wait */
} web = {NULL, NULL, 0, 0, 0, 0, 0, -1};

void es2_sim_window_web_mci(es2_sim_window *w, void (*notify)(void *user, uint32_t wparam, uint32_t lparam), void *user) {
    (void)w;
    web.mci = notify;
    web.mci_user = user;
}

/* The keypad's VKs by set-1 scan code 0x47..0x53 (es2_sim_window_vk's
 * keypad_on and keypad_off; 0: not a keypad digit). */
static const uint8_t keypad_on[13] = {0x67, 0x68, 0x69, 0, 0x64, 0x65, 0x66, 0, 0x61, 0x62, 0x63, 0x60, 0x6e};
static const uint8_t keypad_off[13] = {0x24, 0x26, 0x21, 0, 0x25, 0x0c, 0x27, 0, 0x23, 0x28, 0x22, 0x2d, 0x2e};

static void client_to_cursor(const es2_sim_window *w, int32_t x, int32_t y) {
    web.cursor_x = w->fullscreen ? x : x + w->origin_x;
    web.cursor_y = w->fullscreen ? y : y + w->origin_y;
}

int es2_sim_window_web_event(es2_sim_window *w, const es2_sim_web_input *e) {
    switch (e->type) {
    case ES2_SIM_WEB_INPUT_KEY: {
        const uint32_t scan = (uint32_t)e->a & 0xffu;
        const int extended = (e->a & 0x100) != 0;
        uint32_t vk = (uint32_t)e->b & 0xffu;
        if (!extended && scan >= 0x47u && scan <= 0x53u && keypad_on[scan - 0x47u])
            vk = (w->keys[VK_NUMLOCK_] & 1u) ? keypad_on[scan - 0x47u] : keypad_off[scan - 0x47u];
        return es2_sim_window_key_event(w, w->focused ? vk : 0u, scan, extended, !(e->c & ES2_SIM_WEB_KEY_UP), 0);
    }
    case ES2_SIM_WEB_INPUT_MOVE:
        client_to_cursor(w, e->a, e->b);
        return es2_sim_window_motion_event(w, e->a, e->b);
    case ES2_SIM_WEB_INPUT_BUTTON:
        if (e->c < 0 || e->c > 2) break;
        return es2_sim_window_button_event(w, (unsigned)e->c, e->d != 0, e->a, e->b);
    case ES2_SIM_WEB_INPUT_WHEEL: return es2_sim_window_wheel_event(w, e->a);
    case ES2_SIM_WEB_INPUT_FOCUS: return es2_sim_window_focus_event(w, e->a != 0);
    case ES2_SIM_WEB_INPUT_EXPOSE: return es2_sim_window_expose_event(w);
    case ES2_SIM_WEB_INPUT_CLOSE: return es2_sim_window_close_event(w);
    case ES2_SIM_WEB_INPUT_SIZE: return es2_sim_window_minimize_event(w, e->a == 1);
    case ES2_SIM_WEB_INPUT_MCI:
        if (!web.mci) break;
        web.mci(web.mci_user, (uint32_t)e->a, (uint32_t)e->b);
        return 0;
    default: break;
    }
    ++w->ignored;
    return 0;
}

uint32_t es2_sim_window_web_message_box(const char *text, const char *caption, uint32_t style) {
    return web_import_message_box(text, caption, style);
}

/* ---- the backend */
static int web_pump(es2_sim_window *w) {
    if (web.warp_pending) {
        web.warp_pending = 0;
        ++w->events;
        if (es2_sim_window_motion_event(w, web.warp_x, web.warp_y)) return 1;
    }
    es2_sim_web_input e;
    while (web_import_take_input(&e, 1) == 1) {
        ++w->events;
        if (es2_sim_window_web_event(w, &e)) return 1;
    }
    return 0;
}
static unsigned web_drain(es2_sim_window *w) {
    (void)w;
    unsigned n = 0;
    web.warp_pending = 0;
    es2_sim_web_input e;
    while (web_import_take_input(&e, 1) == 1) {
        ++n;
        if (e.type == ES2_SIM_WEB_INPUT_MCI && web.mci) web.mci(web.mci_user, (uint32_t)e.a, (uint32_t)e.b);
    }
    return n;
}
static void web_set_cursor(es2_sim_window *w, int wait) {
    if (web.cursor_shape == wait) return;
    web.cursor_shape = wait;
    web_import_cursor(wait);
    ++w->cursors;
}
static void web_show(es2_sim_window *w, int32_t sw, int32_t sh, const int32_t d[4]) {
    (void)d;
    web_import_present((const uint8_t *)w->pixels, sw, sh);
}
static uint32_t web_cursor_pos(const es2_sim_window *w, int32_t *x, int32_t *y) {
    (void)w;
    *x = web.cursor_x;
    *y = web.cursor_y;
    return 1u;
}
static int web_display_mode(es2_sim_window *w, int fullscreen) {
    (void)w;
    return web_import_display(fullscreen ? 1 : 0) != 0;
}
static int web_letterboxed(const es2_sim_window *w) { return w->open; }
static void web_game_to_desktop(const es2_sim_window *w, int32_t *x, int32_t *y) { (void)w; (void)x; (void)y; }
static uint32_t web_set_cursor_pos(void *user, int32_t x, int32_t y) {
    es2_sim_window *w = user;
    web.cursor_x = x;
    web.cursor_y = y;
    web.warp_x = w->fullscreen ? x : x - w->origin_x;
    web.warp_y = w->fullscreen ? y : y - w->origin_y;
    web.warp_pending = 1;
    return 1u;
}
/* No joystick: the answers of a machine without one (input.c's fallbacks). */
static uint32_t no_joy_caps(void *user, uint32_t id, uint8_t *caps, uint32_t size) {
    (void)user; (void)id; (void)caps; (void)size;
    return ES2_JOYERR_PARMS;
}
static uint32_t no_joy_pos(void *user, uint32_t id, uint8_t *info, uint32_t size) {
    (void)user; (void)id; (void)info; (void)size;
    return ES2_JOYERR_UNPLUGGED;
}
static void sync_from_page(void *user, uint8_t state[256]) {
    (void)user;
    web_import_keys(state);
}
static uint64_t web_now(void *user) { (void)user; return (uint64_t)web_import_now(); }
static void web_delay(void *user, uint32_t ms) { (void)user; web_import_sleep(ms); }

/* ---- set-up */
void es2_sim_window_init(es2_sim_window *w, es2_sim_loop *loop, es2_sim_input *input, es2_sim_present *present) {
    es2_sim_window_core_init(w, loop, input, present);
    w->pixel_format = ES2_SIM_WINDOW_RGBA;
    w->backend.pump = web_pump;
    w->backend.drain = web_drain;
    w->backend.set_cursor = web_set_cursor;
    w->backend.show = web_show;
    w->backend.cursor_pos = web_cursor_pos;
    w->backend.display_mode = web_display_mode;
    w->backend.letterboxed = web_letterboxed;
    w->backend.game_to_desktop = web_game_to_desktop;
    w->now_ms = web_now;
    w->delay_ms = web_delay;
    w->joystick.caps = no_joy_caps;
    w->joystick.pos = no_joy_pos;
    w->key_sync = sync_from_page;
    w->set_cursor_pos = web_set_cursor_pos;
    w->time_user = NULL;
}

int es2_sim_window_video_init(es2_sim_window *w) {
    (void)w;
    return 0;
}

void es2_sim_window_metrics(uint32_t *screen_cx, uint32_t *screen_cy, uint32_t *frame_cx, uint32_t *frame_cy, uint32_t *caption) {
    uint32_t m[5] = {0, 0, 0, 0, 0};
    web_import_metrics(m);
    *screen_cx = m[0];
    *screen_cy = m[1];
    *frame_cx = m[2];
    *frame_cy = m[3];
    *caption = m[4];
}

/* The page's canvas is the window; its icon is the page's. */
int es2_sim_window_open(es2_sim_window *w, int32_t width, int32_t height, int32_t x, int32_t y) {
    if (width <= 0 || height <= 0) { snprintf(w->error, sizeof(w->error), "game size %dx%d", (int)width, (int)height); return 1; }
    w->client_w = w->game_w = width;
    w->client_h = w->game_h = height;
    w->origin_x = x;
    w->origin_y = y;
    web.cursor_x = x;
    web.cursor_y = y;
    web.warp_pending = 0;
    web.cursor_shape = -1;
    uint8_t state[256];
    memset(state, 0, sizeof(state));
    web_import_keys(state);
    w->keys[VK_CAPITAL_] = state[VK_CAPITAL_] & 1u;
    w->keys[VK_NUMLOCK_] = state[VK_NUMLOCK_] & 1u;
    w->keys[VK_SCROLL_] = state[VK_SCROLL_] & 1u;
    w->altgr = 0;
    w->open = 1;
    /* UpdateWindow's paint and the activation, as the SDL backend's open. */
    w->paint_pending = 1;
    w->focus_pending = 1;
    return 0;
}

void es2_sim_window_close(es2_sim_window *w) {
    w->open = 0;
    free(w->pixels);
    w->pixels = NULL;
    w->pixels_capacity = 0;
    es2_sim_cursor_bind(NULL);
}
