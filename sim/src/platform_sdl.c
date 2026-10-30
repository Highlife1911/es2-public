/* The SDL3 backend of the windowed platform layer; see es2/sim/platform_sdl.h. SDL events become the
 * shared layer's events (es2/sim/platform_window.h); Windows answers the layout, the key state at
 * activation, the cursor position, the joystick, the metrics and the icon. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "es2/sim/platform_sdl.h"

/* Virtual-key codes (winuser.h). */
#define VK_SHIFT_ 0x10u
#define VK_CONTROL_ 0x11u
#define VK_MENU_ 0x12u
#define VK_NUMLOCK_ 0x90u
#define VK_CAPITAL_ 0x14u
#define VK_SCROLL_ 0x91u
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

/* The default key state at activation: GetKeyboardState, the thread's own
 * state, which Windows has synchronised by the time the activation reaches
 * SDL. */
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

/* ---- SDL events into the shared layer's events */
static int key_event(es2_sim_window *w, const SDL_KeyboardEvent *k) {
    uint32_t scan = 0;
    int extended = 0;
    uint32_t vk = w->focused ? es2_sim_window_vk((uint32_t)k->scancode, w->keys[VK_NUMLOCK_] & 1u, &scan, &extended) : 0u;
    return es2_sim_window_key_event(w, vk, scan, extended, k->down, k->scancode == SDL_SCANCODE_RALT && w->altgr);
}
static int button_event(es2_sim_window *w, const SDL_MouseButtonEvent *b) {
    unsigned button;
    switch (b->button) {
    case SDL_BUTTON_LEFT: button = 0; break;
    case SDL_BUTTON_RIGHT: button = 1; break;
    case SDL_BUTTON_MIDDLE: button = 2; break;
    default: ++w->ignored; return 0;
    }
    return es2_sim_window_button_event(w, button, b->down, (int32_t)SDL_floorf(b->x), (int32_t)SDL_floorf(b->y));
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
        return es2_sim_window_motion_event(w, (int32_t)SDL_floorf(e->motion.x), (int32_t)SDL_floorf(e->motion.y));
    case SDL_EVENT_MOUSE_BUTTON_DOWN: case SDL_EVENT_MOUSE_BUTTON_UP:
        if (e->button.windowID != w->window_id) break;
        return button_event(w, &e->button);
    case SDL_EVENT_MOUSE_WHEEL:
        if (e->wheel.windowID != w->window_id) break;
        return es2_sim_window_wheel_event(w, (int32_t)(e->wheel.y * 120.0f));
    case SDL_EVENT_WINDOW_FOCUS_GAINED: case SDL_EVENT_WINDOW_FOCUS_LOST:
        if (e->window.windowID != w->window_id) break;
        return es2_sim_window_focus_event(w, e->type == SDL_EVENT_WINDOW_FOCUS_GAINED);
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        if (e->window.windowID != w->window_id) break;
        return es2_sim_window_close_event(w);
    case SDL_EVENT_WINDOW_EXPOSED:
        if (e->window.windowID != w->window_id) break;
        return es2_sim_window_expose_event(w);
    case SDL_EVENT_WINDOW_MINIMIZED: case SDL_EVENT_WINDOW_RESTORED:
        if (e->window.windowID != w->window_id) break;
        return es2_sim_window_minimize_event(w, e->type == SDL_EVENT_WINDOW_MINIMIZED);
    case SDL_EVENT_WINDOW_MOVED:
        if (e->window.windowID != w->window_id) break;
        return es2_sim_window_move_event(w, e->window.data1, e->window.data2);
    default: break;
    }
    ++w->ignored;
    return 0;
}

/* ---- the backend */
static int sdl_pump(es2_sim_window *w) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        ++w->events;
        if (es2_sim_window_event(w, &e)) return 1;
    }
    return 0;
}
/* SDL_PollEvent also dispatches the thread's Win32 messages (the windowed
 * audio's notify window among them). */
static unsigned sdl_drain(es2_sim_window *w) {
    (void)w;
    unsigned n = 0;
    SDL_Event e;
    while (SDL_PollEvent(&e)) ++n;
    return n;
}
static void sdl_set_cursor(es2_sim_window *w, int wait) {
    SDL_Cursor *c = wait ? w->wait : w->arrow;
    if (c && SDL_GetCursor() != c) { SDL_SetCursor(c); ++w->cursors; }
}
static void sdl_show(es2_sim_window *w, int32_t sw, int32_t sh, const int32_t d[4]) {
    if (!w->texture || w->texture_w != sw || w->texture_h != sh) {
        if (w->texture) SDL_DestroyTexture(w->texture);
        w->texture = SDL_CreateTexture(w->renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, sw, sh);
        if (!w->texture) return;
        SDL_SetTextureScaleMode(w->texture, SDL_SCALEMODE_NEAREST);
        w->texture_w = sw;
        w->texture_h = sh;
    }
    SDL_UpdateTexture(w->texture, NULL, w->pixels, sw * (int)sizeof(uint32_t));
    SDL_FRect dst = {(float)d[0], (float)d[1], (float)d[2], (float)d[3]};
    SDL_SetRenderDrawColor(w->renderer, 0, 0, 0, 255);
    SDL_RenderClear(w->renderer);
    SDL_RenderTexture(w->renderer, w->texture, NULL, &dst);
    SDL_RenderPresent(w->renderer);
}
static int sdl_letterboxed(const es2_sim_window *w) { return w->window && w->renderer; }
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
/* GetCursorPos: the original's own call (the process is DPI-unaware, so
 * the point is in the same desktop coordinates as the window's origin). */
static uint32_t sdl_cursor_pos(const es2_sim_window *w, int32_t *x, int32_t *y) {
    POINT p = {0, 0};
    BOOL ok = GetCursorPos(&p);
    *x = (int32_t)p.x;
    *y = (int32_t)p.y;
    if (ok && w->fullscreen && w->window && w->renderer) desktop_to_game(w, x, y);
    return ok ? 1u : 0u;
}
/* Platform choice: the original's exclusive 640 x 480 8-bit mode becomes
 * SDL's desktop full screen with the frame scaled into a letterbox; the way
 * back is SDL's windowed geometry (the original's SetWindowPos to 0x4d36cc). */
static int sdl_display_mode(es2_sim_window *w, int fullscreen) {
    int ok = SDL_SetWindowFullscreenMode(w->window, NULL);
    ok = SDL_SetWindowFullscreen(w->window, fullscreen ? true : false) && ok;
    ok = SDL_SetRenderLogicalPresentation(w->renderer, fullscreen ? w->game_w : 0, fullscreen ? w->game_h : 0,
                                          fullscreen ? SDL_LOGICAL_PRESENTATION_LETTERBOX : SDL_LOGICAL_PRESENTATION_DISABLED) && ok;
    SDL_SyncWindow(w->window);
    if (!fullscreen) {
        int px = 0, py = 0;
        if (SDL_GetWindowPosition(w->window, &px, &py)) { w->origin_x = px; w->origin_y = py; }
    }
    return ok;
}
static uint32_t user32_set_cursor_pos(void *user, int32_t x, int32_t y) {
    (void)user;
    return SetCursorPos(x, y) ? 1u : 0u;
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

/* ---- set-up */
static uint64_t sdl_now(void *user) { (void)user; return SDL_GetTicks(); }
static void sdl_delay(void *user, uint32_t ms) { (void)user; SDL_Delay(ms); }

void es2_sim_window_init(es2_sim_window *w, es2_sim_loop *loop, es2_sim_input *input, es2_sim_present *present) {
    es2_sim_window_core_init(w, loop, input, present);
    w->pixel_format = ES2_SIM_WINDOW_XRGB8888;
    w->backend.pump = sdl_pump;
    w->backend.drain = sdl_drain;
    w->backend.set_cursor = sdl_set_cursor;
    w->backend.show = sdl_show;
    w->backend.cursor_pos = sdl_cursor_pos;
    w->backend.display_mode = sdl_display_mode;
    w->backend.letterboxed = sdl_letterboxed;
    w->backend.game_to_desktop = game_to_desktop;
    w->now_ms = sdl_now;
    w->delay_ms = sdl_delay;
    w->joystick.caps = winmm_caps;
    w->joystick.pos = winmm_pos;
    w->key_sync = sync_from_thread;
    w->set_cursor_pos = user32_set_cursor_pos;
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
    w->open = 1;
    /* UpdateWindow's paint after ShowWindow: retrieved by the next peek;
     * the activation too (op_peek). */
    w->paint_pending = 1;
    w->focus_pending = 1;
    return 0;
}

void es2_sim_window_close(es2_sim_window *w) {
    w->open = 0;
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
