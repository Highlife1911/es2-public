/* The windowed platform layer (src/platform_sdl.c) on SDL's dummy video driver
 * (no visible window): a synthetic guest image with the window procedure's
 * handler list, recording services for the handlers and the callees the window
 * procedure reaches (pause and resume, the quit dialog's command key, the
 * present, time_scale), scripted SDL events into the pump
 * (es2_sim_pump), the input seam (GetKeyState, GetClientRect), the
 * limiter with the wall-clock pacing on a fake clock, and the present's
 * conversion through a synthetic colour table. No game data. The key
 * state at activation, AltGr's fake Ctrl, the cursor rows ClientToScreen and
 * SetCursorPos windowed and headless, the empty and the scaled present. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <SDL3/SDL.h>
#include "es2/sim/memory.h"
#include "es2/sim/platform_sdl.h"
#include "es2/sim/image_file.h"
#include "es2/sim/services.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

#define IMAGE_BASE 0x00400000u
#define IMAGE_SIZE 0x002d8000u
#define LOCAL_SIZE 0x00020000u          /* call area 0..0x10000, the DIB bits 0x10000.. */
#define HWND_STANDIN 0x00010001u
#define CLOCK0 0x01000000u

static uint8_t *image, *local;
static uint32_t local_guest;
static es2_sim_arena arena;
static es2_guest_memory memory;
static es2_sim_entry storage[64];
static es2_sim_dispatch table;
static es2_sim_loop loop;
static es2_vt vt;
static es2_sim_input input;
static es2_sim_present present;
static es2_sim_window win;
static unsigned refusals;
static void on_refuse(const es2_sim_refusal *r, void *user) { (void)r; (void)user; ++refusals; }

static uint32_t r32(uint32_t a) { return memory.read32(memory.context, a); }
static void w32(uint32_t a, uint32_t v) { memory.write32(memory.context, a, v); }
static uint8_t r8(uint32_t a) { return es2_sim_read8(&memory, a); }
static void w8(uint32_t a, uint8_t v) { es2_sim_write8(&memory, a, v); }

/* ---- recording services */
static struct { uint32_t message, wparam, lparam; } seen[256];
static unsigned seen_count, pauses, resumes, closes, presents_called, scales;
static uint32_t close_answer = 1;
static void svc_keyboard(es2_sim_service_call *c, void *u) {
    (void)u;
    if (seen_count < 256u) { seen[seen_count].message = c->slots[1]; seen[seen_count].wparam = c->slots[2]; seen[seen_count].lparam = c->slots[3]; ++seen_count; }
    c->eax = 0;
}
static void svc_mouse(es2_sim_service_call *c, void *u) { (void)c; (void)u; c->eax = 0; }
static void svc_pause(es2_sim_service_call *c, void *u) { (void)c; (void)u; ++pauses; }
static void svc_resume(es2_sim_service_call *c, void *u) { (void)c; (void)u; ++resumes; }
static void svc_command(es2_sim_service_call *c, void *u) { (void)u; if (c->slots[0] == 0x410u) ++closes; c->eax = close_answer; }
static void svc_present(es2_sim_service_call *c, void *u) { (void)c; (void)u; ++presents_called; }
static void svc_scale(es2_sim_service_call *c, void *u) { (void)u; ++scales; c->eax = 0x100u; c->ecx = 0; c->edx = 0; }

static es2_sim_services cursor_services;
void es2_sim_service_client_to_screen(es2_sim_service_call *call, void *user);
void es2_sim_service_set_cursor_pos(es2_sim_service_call *call, void *user);

/* ---- a fake keyboard state for the activation, a fake SetCursorPos */
static const uint32_t regs0[3] = {0, 0, 0};
static uint8_t sync_state[256];
static unsigned syncs;
static void fake_sync(void *u, uint8_t state[256]) { (void)u; ++syncs; memcpy(state, sync_state, 256); }
static int32_t warp_x, warp_y;
static unsigned warps;
static uint32_t fake_set_cursor(void *u, int32_t x, int32_t y) { (void)u; ++warps; warp_x = x; warp_y = y; return 1; }

static int setup_dispatch(void) {
    es2_sim_dispatch_init(&table, storage, 64, on_refuse, NULL);
    struct { uint32_t va; es2_sim_service_fn f; uint8_t slots; uint16_t pop; } rows[] = {
        {0x00477becu, svc_keyboard, 4, 16}, {0x004809f8u, svc_mouse, 4, 16}, {0x0046317cu, svc_pause, 0, 0},
        {0x00463238u, svc_resume, 0, 0},    {0x0045feb8u, svc_command, 1, 0}, {0x00464a14u, svc_present, 0, 0},
        {0x00467894u, svc_scale, 2, 0},
    };
    for (unsigned i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i)
        if (es2_sim_dispatch_add_service(&table, rows[i].va, "test", rows[i].f, NULL, rows[i].pop, rows[i].slots)) return 1;
    /* The cursor rows. */
    cursor_services.memory = &memory;
    if (es2_sim_dispatch_add_service(&table, ES2_SIM_VA_CLIENT_TO_SCREEN, "ClientToScreen", es2_sim_service_client_to_screen,
                                     &cursor_services, 8, 2) ||
        es2_sim_dispatch_add_service(&table, ES2_SIM_VA_SET_CURSOR_POS, "SetCursorPos", es2_sim_service_set_cursor_pos,
                                     &cursor_services, 8, 2) ||
        es2_sim_dispatch_add_service(&table, 0x004667c8u, "fullscreen_switch", es2_sim_service_fullscreen_switch, NULL, 0, 0))
        return 1;
    const char *error = NULL;
    uint32_t bad = 0;
    return es2_sim_dispatch_seal(&table, &error, &bad);
}

/* ---- a fake wall clock */
static uint64_t now_value = 1000;
static uint32_t delays[16];
static unsigned delay_count;
static uint64_t fake_now(void *u) { (void)u; return now_value; }
static void fake_delay(void *u, uint32_t ms) { (void)u; if (delay_count < 16u) delays[delay_count++] = ms; now_value += ms; }

static int push_window(uint32_t type) {
    SDL_Event e;
    SDL_zero(e);
    e.type = type;
    e.window.windowID = win.window_id;
    return SDL_PushEvent(&e) ? 0 : 1;
}
static int push_key(uint32_t scancode, int down, int repeat) {
    SDL_Event e;
    SDL_zero(e);
    e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    e.key.windowID = win.window_id;
    e.key.scancode = (SDL_Scancode)scancode;
    e.key.down = down != 0;
    e.key.repeat = repeat != 0;
    return SDL_PushEvent(&e) ? 0 : 1;
}
static int push_motion(float x, float y) {
    SDL_Event e;
    SDL_zero(e);
    e.type = SDL_EVENT_MOUSE_MOTION;
    e.motion.windowID = win.window_id;
    e.motion.x = x;
    e.motion.y = y;
    return SDL_PushEvent(&e) ? 0 : 1;
}
static int push_button(uint8_t button, int down, float x, float y) {
    SDL_Event e;
    SDL_zero(e);
    e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    e.button.windowID = win.window_id;
    e.button.button = button;
    e.button.down = down != 0;
    e.button.x = x;
    e.button.y = y;
    return SDL_PushEvent(&e) ? 0 : 1;
}
static int pump(void) {
    uint32_t quit = 0;
    return es2_sim_pump(&loop, &quit) || quit;
}
static int16_t key_state(int32_t vk) { return input.platform.key_state(input.platform.user, 0, vk); }

static int vk_table(void) {
    uint32_t scan = 0;
    int ext = 0;
    CHECK(es2_sim_window_vk(41, 1, &scan, &ext) == 0x1bu && scan == 0x01u && !ext);           /* Escape */
    CHECK(es2_sim_window_vk(82, 1, &scan, &ext) == 0x26u && scan == 0x48u && ext);            /* Up */
    CHECK(es2_sim_window_vk(58, 1, &scan, &ext) == 0x70u && scan == 0x3bu && !ext);           /* F1 */
    CHECK(es2_sim_window_vk(67, 1, &scan, &ext) == 0x79u && scan == 0x44u);                    /* F10 */
    CHECK(es2_sim_window_vk(89, 1, &scan, &ext) == 0x61u && scan == 0x4fu && !ext);           /* keypad 1, Num Lock */
    CHECK(es2_sim_window_vk(89, 0, &scan, &ext) == 0x23u && scan == 0x4fu && !ext);           /* keypad 1: End */
    CHECK(es2_sim_window_vk(88, 0, &scan, &ext) == 0x0du && scan == 0x1cu && ext);            /* keypad Enter */
    CHECK(es2_sim_window_vk(228, 0, &scan, &ext) == 0x11u && scan == 0x1du && ext);           /* right Ctrl */
    CHECK(es2_sim_window_vk(229, 0, &scan, &ext) == 0x10u && scan == 0x36u && !ext);          /* right Shift */
    CHECK(es2_sim_window_vk(226, 0, &scan, &ext) == 0x12u && scan == 0x38u && !ext);          /* left Alt */
    CHECK(es2_sim_window_vk(0, 0, &scan, &ext) == 0u && es2_sim_window_vk(200, 0, &scan, &ext) == 0u);
    CHECK(es2_sim_window_key_lparam(0x48, 1, 0, 0, 0) == 0x01480001u);
    CHECK(es2_sim_window_key_lparam(0x3e, 0, 1, 1, 1) == 0xe03e0001u);
    CHECK(es2_sim_window_key_lparam(0x01, 0, 0, 1, 0) == 0x40010001u);
    /* The conversion: RGBQUAD (blue, green, red) to XRGB. */
    uint8_t colours[1024] = {0}, bits[8] = {0, 1, 2, 255, 1, 1, 0, 0};
    colours[4] = 0x01; colours[5] = 0x02; colours[6] = 0x03;
    colours[8] = 0xff;
    colours[1020] = 0x10; colours[1021] = 0x20; colours[1022] = 0x30; colours[1023] = 0x99;
    uint32_t out[4] = {0};
    es2_sim_window_convert(bits, 4, 2, 2, colours, out);   /* the left half of two rows */
    CHECK(out[0] == 0u && out[1] == 0x030201u && out[2] == 0x030201u && out[3] == 0x030201u);
    es2_sim_window_convert(bits, 4, 4, 1, colours, out);
    CHECK(out[2] == 0x0000ffu && out[3] == 0x302010u);
    return 0;
}

static int messages(void) {
    /* What the driver produced for the new window: the dummy driver gives
     * the shown window the keyboard focus (as ShowWindow activates it), so
     * WM_SETFOCUS was sent (the resume 0x45f108), then the creation paint
     * (WM_PAINT -> the present). */
    w8(0x004d2572u, 1);
    CHECK(pump() == 0);
    CHECK(win.focused && resumes == 1 && r8(0x004d2572u) == 0 && seen_count == 2 && seen[0].message == ES2_WM_SETFOCUS);
    CHECK(seen[1].message == ES2_WM_PAINT && presents_called == 1 && win.paints == 1);
    CHECK(win.guest_focus && !win.focus_pending);
    /* The guest has the focus; a focus SDL reports late (the window was not
     * active) resyncs the keys but sends no second WM_SETFOCUS, so the resume
     * does not run without a pause before it. */
    seen_count = 0;
    win.focused = 0;
    CHECK(push_window(SDL_EVENT_WINDOW_FOCUS_GAINED) == 0 && pump() == 0);
    CHECK(win.focused && resumes == 1 && seen_count == 0);
    /* A window that never got the focus: the activation at the first peek. */
    win.guest_focus = 0;
    win.focus_pending = 1;
    CHECK(pump() == 0 && resumes == 2 && seen_count == 1 && seen[0].message == ES2_WM_SETFOCUS && win.guest_focus);
    /* Focus lost: WM_KILLFOCUS (the pause 0x45f0d4, the pause byte); keys
     * only reach the focus window; focus regained: WM_SETFOCUS. */
    seen_count = 0;
    CHECK(push_window(SDL_EVENT_WINDOW_FOCUS_LOST) == 0 && pump() == 0 && pauses == 1 && r8(0x004d2572u) == 1);
    CHECK(seen_count == 1 && seen[0].message == ES2_WM_KILLFOCUS);
    CHECK(push_key(41, 1, 0) == 0 && pump() == 0 && seen_count == 1 && win.ignored >= 1u);
    CHECK(push_window(SDL_EVENT_WINDOW_FOCUS_GAINED) == 0 && pump() == 0);
    CHECK(resumes == 3 && r8(0x004d2572u) == 0 && seen_count == 2 && seen[1].message == ES2_WM_SETFOCUS);
    seen_count = 0;
    /* Escape down, its repeat, up: WM_KEYDOWN/WM_KEYUP with the set-1 lParam. */
    CHECK(push_key(41, 1, 0) == 0 && push_key(41, 1, 1) == 0 && push_key(41, 0, 0) == 0 && pump() == 0);
    CHECK(seen_count == 3);
    CHECK(seen[0].message == ES2_WM_KEYDOWN && seen[0].wparam == 0x1bu && seen[0].lparam == 0x00010001u);
    CHECK(seen[1].message == ES2_WM_KEYDOWN && seen[1].lparam == 0x40010001u);
    CHECK(seen[2].message == ES2_WM_KEYUP && seen[2].wparam == 0x1bu && seen[2].lparam == 0xc0010001u);
    CHECK(key_state(0x1b) == 1);   /* up, toggled once (the repeat does not toggle) */
    /* GetKeyState follows the retrieved messages, not the events. */
    seen_count = 0;
    CHECK(push_key(225, 1, 0) == 0 && push_key(82, 1, 0) == 0);
    uint32_t m = 0, wp = 0, lp = 0;
    CHECK(win.ops.peek(win.ops.user, &loop, &m, &wp, &lp) == 1 && m == ES2_WM_KEYDOWN && wp == 0x10u && lp == 0x002a0001u);
    CHECK(key_state(0x10) < 0 && key_state(0xa0) < 0 && key_state(0xa1) == 0 && key_state(0x26) == 0);
    CHECK(win.ops.peek(win.ops.user, &loop, &m, &wp, &lp) == 1 && m == ES2_WM_KEYDOWN && wp == 0x26u && lp == 0x01480001u);
    CHECK(key_state(0x26) == (int16_t)0xff81);
    CHECK(push_key(82, 0, 0) == 0 && push_key(225, 0, 0) == 0 && pump() == 0);
    CHECK(key_state(0x10) == 1 && key_state(0xa0) == 1 && key_state(0x26) == 1);
    /* Alt+F4: WM_SYSKEYDOWN; DefWindowProcA sends WM_SYSCOMMAND(SC_CLOSE),
     * its default WM_CLOSE: the quit dialog (command 0x410) and its answer. */
    seen_count = 0;
    close_answer = 2;
    CHECK(push_key(226, 1, 0) == 0 && push_key(61, 1, 0) == 0 && push_key(61, 0, 0) == 0 && push_key(226, 0, 0) == 0 && pump() == 0);
    CHECK(seen_count == 6);
    CHECK(seen[0].message == ES2_WM_SYSKEYDOWN && seen[0].wparam == 0x12u && seen[0].lparam == 0x20380001u);
    CHECK(seen[1].message == ES2_WM_SYSKEYDOWN && seen[1].wparam == 0x73u && seen[1].lparam == 0x203e0001u);
    CHECK(seen[2].message == 0x0112u && seen[2].wparam == 0xf060u);
    CHECK(seen[3].message == ES2_WM_CLOSE);
    CHECK(seen[4].message == ES2_WM_SYSKEYUP && seen[4].wparam == 0x73u && seen[4].lparam == 0xe03e0001u);
    CHECK(seen[5].message == ES2_WM_KEYUP && seen[5].wparam == 0x12u && seen[5].lparam == 0xc0380001u);
    CHECK(closes == 1 && memory.read16(memory.context, 0x004d370cu) == 2u && win.closes_alt_f4 == 1 && win.menu_keys == 0);
    /* Alt alone and F10: WM_SYSCOMMAND(SC_KEYMENU), counted, no refusal. */
    seen_count = 0;
    CHECK(push_key(226, 1, 0) == 0 && push_key(226, 0, 0) == 0 && push_key(67, 1, 0) == 0 && push_key(67, 0, 0) == 0 && pump() == 0);
    CHECK(win.menu_keys == 2 && refusals == 0 && loop.outcome == ES2_SIM_LOOP_RETURNED);
    CHECK(seen[2].message == 0x0112u && seen[2].wparam == 0xf100u && seen[3].message == ES2_WM_SYSKEYDOWN && seen[3].wparam == 0x79u);
    /* The mouse: client coordinates, MK_ flags, no capture outside the client area. */
    seen_count = 0;
    CHECK(push_motion(10.5f, 20.25f) == 0 && push_button(SDL_BUTTON_LEFT, 1, 11.0f, 21.0f) == 0 &&
          push_button(SDL_BUTTON_RIGHT, 1, 11.0f, 21.0f) == 0 && push_button(SDL_BUTTON_LEFT, 0, 12.0f, 22.0f) == 0 &&
          push_motion(400.0f, 10.0f) == 0 && push_button(SDL_BUTTON_RIGHT, 0, 400.0f, 10.0f) == 0 && pump() == 0);
    CHECK(seen_count == 4);
    CHECK(seen[0].message == ES2_WM_MOUSEMOVE && seen[0].wparam == 0u && seen[0].lparam == ((20u << 16) | 10u));
    CHECK(seen[1].message == ES2_WM_LBUTTONDOWN && seen[1].wparam == 1u && seen[1].lparam == ((21u << 16) | 11u));
    CHECK(seen[2].message == ES2_WM_RBUTTONDOWN && seen[2].wparam == 3u);
    CHECK(seen[3].message == ES2_WM_LBUTTONUP && seen[3].wparam == 2u && seen[3].lparam == ((22u << 16) | 12u));
    CHECK(win.buttons == 0u);
    /* The close button: WM_CLOSE sent. */
    CHECK(push_window(SDL_EVENT_WINDOW_CLOSE_REQUESTED) == 0 && pump() == 0 && closes == 2);
    /* Focus lost: WM_KILLFOCUS (the pause 0x45f0d4, the pause byte). */
    CHECK(push_window(SDL_EVENT_WINDOW_FOCUS_LOST) == 0 && pump() == 0 && pauses == 2 && r8(0x004d2572u) == 1);
    /* Minimized and restored: WM_SIZE sent; GetClientRect 0 x 0 while
     * iconic; the restore repaints. */
    int32_t cw = -1, ch = -1;
    seen_count = 0;
    CHECK(push_window(SDL_EVENT_WINDOW_MINIMIZED) == 0 && pump() == 0);
    input.platform.client_size(input.platform.user, &cw, &ch);
    CHECK(cw == 0 && ch == 0 && seen_count == 1 && seen[0].message == ES2_WM_SIZE && seen[0].wparam == 1u && seen[0].lparam == 0u);
    CHECK(push_window(SDL_EVENT_WINDOW_RESTORED) == 0 && pump() == 0);
    input.platform.client_size(input.platform.user, &cw, &ch);
    CHECK(cw == 320 && ch == 240 && seen[1].message == ES2_WM_SIZE && seen[1].wparam == 0u && seen[1].lparam == ((240u << 16) | 320u));
    CHECK(seen[2].message == ES2_WM_PAINT && presents_called == 2);
    /* The pump's SetCursor with the loop's handle. */
    w32(0x004d3720u, 0x00010005u);
    CHECK(pump() == 0 && loop.platform.last_cursor == 0x00010005u);
    /* WM_PAINT comes after a posted WM_QUIT. */
    win.paint_pending = 1;
    es2_sim_post_quit(&loop);
    uint32_t quit = 0;
    CHECK(es2_sim_pump(&loop, &quit) == 0 && quit == 1 && win.paint_pending == 1);
    CHECK(pump() == 0 && presents_called == 3 && win.paint_pending == 0);
    return 0;
}

static int cursor_warp(void) {
    /* The activation: the key state from the keyboard (Shift held while the
     * focus was away, Caps Lock toggled), GetKeyState's table and the event
     * state alike: Shift's key-up is a key-up of a key that was down. */
    seen_count = 0;
    memset(sync_state, 0, sizeof(sync_state));
    sync_state[0x10] = sync_state[0xa0] = 0x80u;
    sync_state[0x14] = 0x01u;
    unsigned resyncs = win.resyncs, synced = syncs;
    CHECK(!win.focused && push_window(SDL_EVENT_WINDOW_FOCUS_GAINED) == 0 && pump() == 0);
    CHECK(win.resyncs == resyncs + 1u && syncs == synced + 1u && seen_count == 1 && seen[0].message == ES2_WM_SETFOCUS);
    CHECK(key_state(0x10) == (int16_t)0xff80 && key_state(0xa0) == (int16_t)0xff80 && key_state(0x14) == 1 && key_state(0x1b) == 0);
    CHECK(push_key(225, 0, 0) == 0 && pump() == 0);
    CHECK(seen_count == 2 && seen[1].message == ES2_WM_KEYUP && seen[1].wparam == 0x10u && seen[1].lparam == 0xc02a0001u);
    CHECK(key_state(0x10) == 0 && key_state(0xa0) == 0);
    /* AltGr: right Alt with a fake left Ctrl (scan 0x1d, not extended) before
     * each key-down, the repeat included, and before the key-up; with Ctrl
     * down Alt's messages are WM_KEYDOWN/WM_KEYUP and no keyboard menu; the
     * fake Ctrl's key-up, with Alt still down, is a WM_SYSKEYUP (the
     * general rule). */
    seen_count = 0;
    win.altgr = 1;
    unsigned menus = win.menu_keys;
    CHECK(push_key(230, 1, 0) == 0 && push_key(230, 1, 1) == 0 && push_key(230, 0, 0) == 0 && pump() == 0);
    CHECK(seen_count == 6 && win.altgr_ctrls == 2u && win.menu_keys == menus && !win.altgr_ctrl);
    CHECK(seen[0].message == ES2_WM_KEYDOWN && seen[0].wparam == 0x11u && seen[0].lparam == 0x001d0001u);
    CHECK(seen[1].message == ES2_WM_KEYDOWN && seen[1].wparam == 0x12u && seen[1].lparam == 0x01380001u);
    CHECK(seen[2].message == ES2_WM_KEYDOWN && seen[2].wparam == 0x11u && seen[2].lparam == 0x401d0001u);
    CHECK(seen[3].message == ES2_WM_KEYDOWN && seen[3].wparam == 0x12u && seen[3].lparam == 0x41380001u);
    CHECK(seen[4].message == ES2_WM_SYSKEYUP && seen[4].wparam == 0x11u && seen[4].lparam == 0xe01d0001u);   /* Alt still down */
    CHECK(seen[5].message == ES2_WM_KEYUP && seen[5].wparam == 0x12u && seen[5].lparam == 0xc1380001u);
    CHECK(key_state(0x11) >= 0 && key_state(0xa2) >= 0 && key_state(0x12) >= 0 && key_state(0xa5) >= 0);   /* all up */
    /* A held left Ctrl is not doubled. */
    seen_count = 0;
    CHECK(push_key(224, 1, 0) == 0 && push_key(230, 1, 0) == 0 && push_key(230, 0, 0) == 0 && push_key(224, 0, 0) == 0 && pump() == 0);
    CHECK(seen_count == 4 && win.altgr_ctrls == 2u && seen[1].message == ES2_WM_KEYDOWN && seen[1].wparam == 0x12u);
    CHECK(seen[2].message == ES2_WM_KEYUP && seen[2].wparam == 0x12u && seen[3].message == ES2_WM_KEYUP && seen[3].wparam == 0x11u);
    /* Without AltGr right Alt is an Alt: WM_SYSKEYDOWN, alone the keyboard menu. */
    seen_count = 0;
    win.altgr = 0;
    CHECK(push_key(230, 1, 0) == 0 && push_key(230, 0, 0) == 0 && pump() == 0);
    CHECK(seen[0].message == ES2_WM_SYSKEYDOWN && seen[0].wparam == 0x12u && seen[0].lparam == 0x21380001u);
    CHECK(win.altgr_ctrls == 2u && win.menu_keys == menus + 1u);
    /* The cursor rows windowed: ClientToScreen adds the client origin,
     * SetCursorPos goes to the platform. */
    es2_sim_cursor_calls_reset();
    uint32_t pt = local_guest + 0x8000u, slots[2] = {HWND_STANDIN, pt};
    es2_sim_result out;
    w32(pt, 160u);
    w32(pt + 4u, 120u);
    CHECK(es2_sim_call(&table, ES2_SIM_VA_CLIENT_TO_SCREEN, slots, 2, regs0, &out) == 0 && out.eax == 1u);
    CHECK((int32_t)r32(pt) == 160 + win.origin_x && (int32_t)r32(pt + 4u) == 120 + win.origin_y);
    slots[0] = r32(pt);
    slots[1] = r32(pt + 4u);
    CHECK(es2_sim_call(&table, ES2_SIM_VA_SET_CURSOR_POS, slots, 2, regs0, &out) == 0 && out.eax == 1u);
    CHECK(warps == 1u && win.cursor_warps == 1u && warp_x == 160 + win.origin_x && warp_y == 120 + win.origin_y);
    const es2_sim_cursor_calls *calls = es2_sim_cursor_calls_get();
    CHECK(calls->client_to_screen == 1u && calls->set_pos == 1u && calls->last_x == warp_x && calls->last_y == warp_y);
    /* A NULL point is refused. */
    slots[0] = HWND_STANDIN;
    slots[1] = 0;
    CHECK(es2_sim_call(&table, ES2_SIM_VA_CLIENT_TO_SCREEN, slots, 2, regs0, &out) == 1 && refusals == 1u);
    refusals = 0;
    return 0;
}

static int pacing(void) {
    /* The limiter 0x4678c0 with the wall-clock pacing: a fast frame waits
     * for the step, a slow frame's elapsed time is the wall clock's. */
    w32(0x004d3b74u, CLOCK0);
    w32(0x004d3b68u, 125u);
    win.now_ms = fake_now;
    win.delay_ms = fake_delay;
    CHECK(es2_sim_limiter(&loop) == 0);
    CHECK(delay_count == 1 && delays[0] == 47u && r32(0x004d3b6cu) == 47u && r32(0x004d3b74u) == CLOCK0 + 47u);
    now_value += 100u;
    CHECK(es2_sim_limiter(&loop) == 0);
    CHECK(scales == 4u && delay_count == 1 && win.catch_ups == 1 && r32(0x004d3b6cu) == 100u && r32(0x004d3b74u) == CLOCK0 + 147u);
    /* The pause path's Sleep(100). */
    win.ops.sleep(win.ops.user, 100u);
    CHECK(delay_count == 2 && delays[1] == 100u && win.sleeps == 1);
    return 0;
}

static int presenting(void) {
    /* The present's record: the DIB window through the last table. */
    uint32_t bits = local_guest + 0x10000u;
    w32(0x004d2546u, 4u);
    w32(0x004d3028u, bits);
    for (uint32_t i = 0; i < 8u; ++i) es2_sim_write8(&memory, bits + i, (uint8_t)i);
    for (unsigned i = 0; i < 256u; ++i) { present.own_table.color_table[4u * i] = (uint8_t)i; present.own_table.color_table[4u * i + 2u] = (uint8_t)(255u - i); }
    es2_sim_present_record r;
    memset(&r, 0, sizeof(r));
    r.source = "dib";
    r.src[0] = 1; r.src[1] = 0; r.src[2] = 2; r.src[3] = 2;
    r.dst[0] = 0; r.dst[1] = 0; r.dst[2] = 320; r.dst[3] = 240;
    unsigned before = win.presents;
    win.paint_pending = 1;
    present.on_present(present.user, &r);
    CHECK(win.presents == before + 1u && win.paint_pending == 0);
    if (win.renderer) {
        CHECK(win.texture && win.texture_w == 2 && win.texture_h == 2);
        CHECK(win.pixels[0] == ((254u << 16) | 1u) && win.pixels[1] == ((253u << 16) | 2u) && win.pixels[2] == ((250u << 16) | 5u));
    }
    r.source = "dib_outside";
    present.on_present(present.user, &r);
    CHECK(win.presents == before + 2u && win.presents_outside == (win.renderer ? 1u : 0u));
    /* An empty destination draws nothing; a scaled one is counted (this
     * window's presents are 1:1; the 2 x 2 window above was drawn to 320 x
     * 240). */
    if (win.renderer) {
        CHECK(win.presents_scaled == 1u && win.presents_empty == 0u);
        r.source = "dib";
        r.dst[2] = 2; r.dst[3] = 2;
        present.on_present(present.user, &r);
        CHECK(win.presents_scaled == 1u && win.presents_empty == 0u);
        r.dst[2] = 0;
        present.on_present(present.user, &r);
        r.dst[2] = 2; r.dst[3] = -2;
        present.on_present(present.user, &r);
        CHECK(win.presents_empty == 2u && win.presents_scaled == 1u);
        r.dst[2] = 640; r.dst[3] = 480;
        present.on_present(present.user, &r);
        CHECK(win.presents_scaled == 2u && win.presents_empty == 2u);
    }
    return 0;
}

/* The full-screen switch's request: the row 0x4667c8 to full screen (the
 * start-up's rectangle kept, so no GetWindowRect) asks the window for
 * desktop full screen; the cursor rows then work in game coordinates
 * (ClientToScreen leaves the point); the way back is the request with 0. The
 * dummy driver's answer is counted, never fatal. */
static int fullscreen(void) {
    CHECK(present.platform.display_mode && present.platform.display_user == &win);
    es2_sim_present_bind(&present);
    w32(0x004d3700u, 1u);
    w32(0x004d255eu, 0);
    es2_sim_result out;
    unsigned warped = warps;
    CHECK(es2_sim_call(&table, 0x004667c8u, NULL, 0, regs0, &out) == 0 && refusals == 0);
    CHECK(win.fullscreen == 1 && win.display_switches == 1 && present.platform.display_requests == 1);
    CHECK(r32(0x004d255eu) == 1u && r32(0x004d3714u) == ES2_SIM_DD_STANDIN + 1u && warps == warped + 1u);
    printf("full screen: %u of %u requests refused by the driver\n", win.display_failures, win.display_switches);
    uint32_t pt = local_guest + 0x8000u, slots[2] = {HWND_STANDIN, pt};
    w32(pt, 7u);
    w32(pt + 4u, 9u);
    CHECK(es2_sim_call(&table, ES2_SIM_VA_CLIENT_TO_SCREEN, slots, 2, regs0, &out) == 0 && r32(pt) == 7u && r32(pt + 4u) == 9u);
    /* events and a present in full screen */
    CHECK(push_motion(10.0f, 10.0f) == 0 && pump() == 0);
    es2_sim_present_record r;
    memset(&r, 0, sizeof(r));
    r.source = "dib";
    r.src[2] = 2; r.src[3] = 2; r.dst[2] = 320; r.dst[3] = 240;
    unsigned before = win.presents;
    present.on_present(present.user, &r);
    CHECK(win.presents == before + 1u);
    present.platform.display_mode(present.platform.display_user, 0);
    CHECK(win.fullscreen == 0 && win.display_switches == 2);
    CHECK(es2_sim_call(&table, ES2_SIM_VA_CLIENT_TO_SCREEN, slots, 2, regs0, &out) == 0 && r32(pt) == 7u + (uint32_t)win.origin_x);
    es2_sim_present_bind(NULL);
    w32(0x004d255eu, 0);
    for (uint32_t k = 0; k < 4u; ++k) w32(0x004d3710u + 4u * k, 0);
    return 0;
}

/* The class icon from a synthetic image: a group "TESTICON" with one 32 x 32
 * 16-colour image (number 7) at RVA 0x400; Windows' choice and HICONs of both
 * sizes; a missing name. */
static uint8_t icon_image[0x1000];
static es2_sim_image_info icon_info;
static void iw16(uint32_t at, uint16_t v) { icon_image[at] = (uint8_t)v; icon_image[at + 1] = (uint8_t)(v >> 8); }
static void iw32(uint32_t at, uint32_t v) { iw16(at, (uint16_t)v); iw16(at + 2, (uint16_t)(v >> 16)); }
static int icon_width(void *icon, int *cx) {
    ICONINFO ii;
    BITMAP bm;
    if (!GetIconInfo((HICON)icon, &ii)) return 1;
    int ok = GetObjectA(ii.hbmColor ? ii.hbmColor : ii.hbmMask, sizeof(bm), &bm) != 0;
    *cx = bm.bmWidth;
    if (ii.hbmColor) DeleteObject(ii.hbmColor);
    DeleteObject(ii.hbmMask);
    return !ok;
}
static int icons(void) {
    memset(&icon_info, 0, sizeof(icon_info));
    icon_info.image_size = sizeof(icon_image);
    icon_info.resource_rva = 0x100;
    icon_info.resource_size = 0x200;
    /* The tree: root (types 3 and 14), type 3 (number 7), type 14 (the
     * name at 0x1a0), one language each, the data entries at 0x1c0/0x1d0. */
    const uint32_t r = 0x100;
    iw16(r + 0x0e, 2); iw32(r + 0x10, 3); iw32(r + 0x14, 0x80000020u); iw32(r + 0x18, 14); iw32(r + 0x1c, 0x80000040u);
    iw16(r + 0x2e, 1); iw32(r + 0x30, 7); iw32(r + 0x34, 0x80000060u);
    iw16(r + 0x4c, 1); iw32(r + 0x50, 0x800001a0u); iw32(r + 0x54, 0x80000080u);
    iw16(r + 0x6e, 1); iw32(r + 0x70, 0); iw32(r + 0x74, 0x1c0);
    iw16(r + 0x8e, 1); iw32(r + 0x90, 0); iw32(r + 0x94, 0x1d0);
    iw16(r + 0x1a0, 8);
    for (unsigned i = 0; i < 8; ++i) iw16(r + 0x1a2 + 2 * i, (uint16_t)"TESTICON"[i]);
    const uint32_t bits = 0x400, size = 40 + 64 + 512 + 128, group = 0x800;
    iw32(r + 0x1c0, bits); iw32(r + 0x1c4, size);
    iw32(r + 0x1d0, group); iw32(r + 0x1d4, 20);
    /* The group: type 1, one entry 32 x 32, 16 colours, 1 plane, 4 bits,
     * the size and number 7. */
    iw16(group + 2, 1); iw16(group + 4, 1);
    icon_image[group + 6] = 32; icon_image[group + 7] = 32; icon_image[group + 8] = 16;
    iw16(group + 10, 1); iw16(group + 12, 4); iw32(group + 14, size); iw16(group + 18, 7);
    /* The image: the header (height twice: colour and mask), a grey ramp
     * as the colours, stripes of colours, a mask with a clear corner. */
    iw32(bits, 40); iw32(bits + 4, 32); iw32(bits + 8, 64); iw16(bits + 12, 1); iw16(bits + 14, 4);
    for (unsigned i = 0; i < 16; ++i) { icon_image[bits + 40 + 4 * i] = icon_image[bits + 41 + 4 * i] = icon_image[bits + 42 + 4 * i] = (uint8_t)(17 * i); }
    for (unsigned i = 0; i < 512; ++i) icon_image[bits + 104 + i] = (uint8_t)((i / 16) * 0x11u);
    for (unsigned i = 0; i < 4; ++i) icon_image[bits + 616 + i] = 0xf0;
    const char *error = NULL;
    void *big = es2_sim_window_load_icon(icon_image, &icon_info, "testicon", 32, 32, &error);
    void *small = es2_sim_window_load_icon(icon_image, &icon_info, "TestIcon", 16, 16, &error);
    CHECK(big && small);
    int cx = 0;
    CHECK(icon_width(big, &cx) == 0 && cx == 32);
    CHECK(icon_width(small, &cx) == 0 && cx == 16);
    DestroyIcon((HICON)big);
    DestroyIcon((HICON)small);
    CHECK(!es2_sim_window_load_icon(icon_image, &icon_info, "es2icon", 32, 32, &error) && !strcmp(error, "resource not found"));
    /* The own resources: "es2icon" in any case, both sizes; the synthetic group
     * is not among them. */
    big = es2_sim_window_load_icon_own("es2icon", 32, 32, &error);
    small = es2_sim_window_load_icon_own("ES2ICON", 16, 16, &error);
    CHECK(big && small);
    CHECK(icon_width(big, &cx) == 0 && cx == 32);
    CHECK(icon_width(small, &cx) == 0 && cx == 16);
    DestroyIcon((HICON)big);
    DestroyIcon((HICON)small);
    CHECK(!es2_sim_window_load_icon_own("TESTICON", 32, 32, &error) && !strcmp(error, "resource not found"));
    /* The open gives the window its icon from the own resources (the window has
     * no placed-image fields); the dummy driver has no Win32 window,
     * so the icons are made but not sent. */
    win.icon_name = "es2icon";
    return 0;
}

int main(void) {
    image = calloc(1, IMAGE_SIZE);
    for (uint32_t a = 0x30000000u; !local && a < 0x38000000u; a += 0x01000000u)
        local = VirtualAlloc((void *)(uintptr_t)a, LOCAL_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    CHECK(image && local);
    local_guest = (uint32_t)(uintptr_t)local;
    CHECK(es2_sim_arena_add(&arena, IMAGE_BASE, image, IMAGE_SIZE));
    CHECK(es2_sim_arena_add(&arena, local_guest, local, LOCAL_SIZE));
    es2_sim_arena_memory(&arena, &memory);
    CHECK(setup_dispatch() == 0);
    if (vk_table()) return 1;
    /* The guest: the window handle, game size 320 x 240, the handler list
     * (keyboard slot 0, mouse slot 1). */
    w32(0x004d3610u, HWND_STANDIN);
    w32(0x004d253eu, 320u);
    w32(0x004d2542u, 240u);
    w32(0x004d3b30u, 0x00477becu);
    w32(0x004d3b34u, 0x004809f8u);
    es2_vt_enable(&vt, 1, 47, CLOCK0, 0, 1);
    es2_sim_loop_defaults(&loop);
    loop.memory = &memory;
    loop.dispatch = &table;
    loop.vt = &vt;
    loop.stack_lo = local_guest;
    loop.stack_hi = local_guest + 0x10000u;
    es2_sim_loop_bind(&loop);
    es2_sim_input_headless(&input, &loop, NULL, 0, 0);
    es2_sim_present_headless(&present, &loop);
    es2_sim_window_init(&win, &loop, &input, &present);
    CHECK(loop.window == &win.ops && input.platform.user == &win && present.user == &win);
    /* The fake keyboard state and cursor (no real ones in a test). */
    CHECK(win.key_sync && win.set_cursor_pos);
    win.key_sync = fake_sync;
    win.set_cursor_pos = fake_set_cursor;
    /* No window yet: an empty queue; GetClientRect is the game size. */
    uint32_t m = 0, wp = 0, lp = 0;
    int32_t cw = 0, ch = 0;
    CHECK(win.ops.peek(win.ops.user, &loop, &m, &wp, &lp) == 0);
    input.platform.client_size(input.platform.user, &cw, &ch);
    CHECK(cw == 320 && ch == 240);
    CHECK(SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy"));
    CHECK(es2_sim_window_video_init(&win) == 0);
    if (icons()) return 1;
    CHECK(es2_sim_window_open(&win, 320, 240, 100, 50) == 0 && win.window && win.paint_pending == 1);
    CHECK(win.icon_big && win.icon_small && win.icon_error && !strcmp(win.icon_error, "no Win32 window"));
    printf("renderer: %s\n", win.renderer ? SDL_GetRendererName(win.renderer) : "(none)");
    if (messages() || cursor_warp() || pacing() || presenting() || fullscreen()) return 1;
    CHECK(refusals == 0);
    es2_sim_window_close(&win);
    CHECK(!win.window && !win.renderer && !win.icon_big && !win.icon_small);
    /* Closed, the cursor rows are headless: the point unchanged, SetCursorPos
     * TRUE without a move. */
    {
        unsigned warped = warps;
        uint32_t pt = local_guest + 0x8000u, slots[2] = {HWND_STANDIN, pt};
        es2_sim_result out;
        w32(pt, 7u);
        w32(pt + 4u, 9u);
        CHECK(es2_sim_call(&table, ES2_SIM_VA_CLIENT_TO_SCREEN, slots, 2, regs0, &out) == 0 && out.eax == 1u && r32(pt) == 7u && r32(pt + 4u) == 9u);
        slots[0] = 7u;
        slots[1] = 9u;
        CHECK(es2_sim_call(&table, ES2_SIM_VA_SET_CURSOR_POS, slots, 2, regs0, &out) == 0 && out.eax == 1u && warps == warped);
    }
    SDL_Quit();
    puts("PASS: platform_sdl: the key table and lParams, focus/close/size sent, keys and mouse queued into the pump and "
         "the window procedure, GetKeyState after retrieval, Alt+F4 and the keyboard menu through DefWindowProcA, "
         "WM_PAINT after WM_QUIT, the wall-clock pacing of the limiter, the present's conversion; the key state at activation, "
         "AltGr's fake Ctrl, the cursor rows windowed and headless, empty and scaled presents, the full-screen request both ways; the class icon from a "
         "synthetic image and from the own resources (dummy video driver)");
    return 0;
}
