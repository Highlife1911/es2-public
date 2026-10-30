/* The own shell's window on SDL3 and the pump that turns SDL events
 * into VSHELL's window messages (vshell_process.md sections 6 and 7;
 * vshell_widgets.md section 7). */
#include "sdl_window.h"
#include "es2/shell/vshell_store.h"

#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

/* Windows virtual-key codes the key table 0x46d4bc knows, from SDL's key
 * event. Letters follow the keyboard layout (like VK_A..VK_Z), the digit row
 * is VK_0..VK_9 on every layout, the keypad sends the navigation keys while
 * Num Lock is off and VK_NUMPAD0.. (not in VSHELL's table) while it is on. */
unsigned es2_gui_sdl_vk(unsigned keycode, unsigned scancode, int numlock) {
    if (scancode >= SDL_SCANCODE_1 && scancode <= SDL_SCANCODE_9) return '1' + (scancode - SDL_SCANCODE_1);
    if (scancode == SDL_SCANCODE_0) return '0';
    if (scancode >= SDL_SCANCODE_KP_1 && scancode <= SDL_SCANCODE_KP_PERIOD) {
        static const uint8_t off[] = {0x23, 0x28, 0x22, 0x25, 0x0c, 0x27, 0x24, 0x26, 0x21, 0x2d, 0x2e};
        unsigned i = scancode - SDL_SCANCODE_KP_1; /* KP_1..KP_9, KP_0, KP_PERIOD */
        if (!numlock) return off[i];
        return i < 9 ? 0x61 + i : i == 9 ? 0x60 : 0x6e;
    }
    if (keycode >= SDLK_A && keycode <= SDLK_Z) return 'A' + (keycode - SDLK_A);
    switch (keycode) {
    case SDLK_ESCAPE: return 0x1b;
    case SDLK_RETURN: case SDLK_KP_ENTER: return 0x0d;
    case SDLK_TAB: return 0x09;
    case SDLK_BACKSPACE: return 0x08;
    case SDLK_SPACE: return 0x20;
    case SDLK_LCTRL: case SDLK_RCTRL: return 0x11;
    case SDLK_LALT: case SDLK_RALT: return 0x12;
    case SDLK_LSHIFT: case SDLK_RSHIFT: return 0x10;
    case SDLK_CAPSLOCK: return 0x14;
    case SDLK_NUMLOCKCLEAR: return 0x90;
    case SDLK_SCROLLLOCK: return 0x91;
    case SDLK_HOME: return 0x24;
    case SDLK_END: return 0x23;
    case SDLK_PAGEUP: return 0x21;
    case SDLK_PAGEDOWN: return 0x22;
    case SDLK_UP: return 0x26;
    case SDLK_DOWN: return 0x28;
    case SDLK_LEFT: return 0x25;
    case SDLK_RIGHT: return 0x27;
    case SDLK_INSERT: return 0x2d;
    case SDLK_DELETE: return 0x2e;
    case SDLK_CLEAR: return 0x0c;
    case SDLK_KP_MINUS: return 0x6d;
    case SDLK_KP_PLUS: return 0x6b;
    default: break;
    }
    if (keycode >= SDLK_F1 && keycode <= SDLK_F12) return 0x70 + (keycode - SDLK_F1);
    return 0;
}

es2_gui_window *es2_gui_window_open(int fullscreen, char *error, size_t error_size) {
    /* VSHELL's window procedure keeps Alt+F4 (Quit box) and the system keys
     * away from DefWindowProc: no close on Alt+F4, no system menu by Alt. */
    SDL_SetHint(SDL_HINT_WINDOWS_CLOSE_ON_ALT_F4, "0");
    SDL_SetHint(SDL_HINT_QUIT_ON_LAST_WINDOW_CLOSE, "0");
    /* VSHELL.EXE declares no DPI awareness: on a scaled desktop Windows
     * shows its 640 x 480 client enlarged by the desktop scale and smoothed
     * (125 %: 800 x 600), which blends the images' dithered gradients. SDL3
     * declares per-monitor awareness by default, which gives a small window
     * with every pixel sharp. */
    SDL_SetHint("SDL_WINDOWS_DPI_AWARENESS", "unaware");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        snprintf(error, error_size, "SDL_Init: %s", SDL_GetError());
        return NULL;
    }
    es2_gui_window *w = calloc(1, sizeof *w);
    if (!w) {
        snprintf(error, error_size, "out of memory");
        SDL_Quit();
        return NULL;
    }
    SDL_WindowFlags flags = fullscreen ? SDL_WINDOW_FULLSCREEN : 0;
    w->window = SDL_CreateWindow("EarthSiege II", ES2_GUI_WIDTH, ES2_GUI_HEIGHT, flags);
    if (w->window) w->renderer = SDL_CreateRenderer(w->window, NULL);
    if (w->renderer)
        w->texture = SDL_CreateTexture(w->renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING,
                                       ES2_GUI_WIDTH, ES2_GUI_HEIGHT);
    if (!w->texture || !SDL_SetTextureScaleMode(w->texture, SDL_SCALEMODE_NEAREST)) {
        snprintf(error, error_size, "window: %s", SDL_GetError());
        es2_gui_window_close(w);
        return NULL;
    }
    w->arrow = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
    w->wait = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_WAIT);
    return w;
}

void es2_gui_window_close(es2_gui_window *w) {
    if (!w) return;
    if (w->arrow) SDL_DestroyCursor(w->arrow);
    if (w->wait) SDL_DestroyCursor(w->wait);
    if (w->texture) SDL_DestroyTexture(w->texture);
    if (w->renderer) SDL_DestroyRenderer(w->renderer);
    if (w->window) SDL_DestroyWindow(w->window);
#ifdef _WIN32
    if (w->icon_big) DestroyIcon(w->icon_big);
    if (w->icon_small) DestroyIcon(w->icon_small);
#endif
    free(w);
    SDL_Quit();
}

/* The cast (int)v of a window coordinate with v clamped to the 16-bit range
 * of the mouse messages' coordinates (NaN to its low end): out of range the
 * conversion differs between x86 and wasm. */
static int point_int(float v) {
    if (!(v > -32768.0f)) return -32768;
    if (v > 32767.0f) return 32767;
    return (int)v;
}

/* Window position (points) -> frame coordinates through the presenter's layout. */
static void to_frame(es2_gui_window *w, float px, float py, int32_t *fx, int32_t *fy) {
    es2_gui_layout layout;
    float scale = 1.0f;
    int x = point_int(px), y = point_int(py);
    if (es2_gui_sdl_layout(w, &layout, &scale)) {
        int ix, iy;
        es2_gui_layout_to_frame(&layout, point_int(SDL_floorf(px * scale)), point_int(SDL_floorf(py * scale)), &ix,
                                &iy);
        x = ix;
        y = iy;
    }
    *fx = x;
    *fy = y;
}

static int pump(void *context, es2_gui_shell *shell) {
    es2_gui_window *w = context;
    SDL_Event e;
    int32_t x, y;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_EVENT_QUIT: /* end of the session: VSHELL's WM_ENDSESSION -> WM_DESTROY -> WM_QUIT */
            return 1;
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED: es2_gui_on_close(shell); break;
        case SDL_EVENT_WINDOW_FOCUS_GAINED: es2_gui_on_focus(shell, 1); break;
        case SDL_EVENT_WINDOW_FOCUS_LOST: es2_gui_on_focus(shell, 0); break;
        case SDL_EVENT_WINDOW_EXPOSED: es2_gui_on_paint(shell); break;
        case SDL_EVENT_MOUSE_MOTION:
            if (w->busy_shown) { /* DefWindowProc's WM_SETCURSOR restores the class arrow */
                SDL_SetCursor(w->arrow);
                w->busy_shown = 0;
            }
            to_frame(w, e.motion.x, e.motion.y, &x, &y);
            es2_gui_on_mouse(shell, ES2_GUI_WM_MOUSEMOVE, x, y, w->held_left, w->held_right);
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            int down = e.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
            if (e.button.button != SDL_BUTTON_LEFT && e.button.button != SDL_BUTTON_RIGHT) break;
            int left = e.button.button == SDL_BUTTON_LEFT;
            if (left) w->held_left = down;
            else w->held_right = down;
            to_frame(w, e.button.x, e.button.y, &x, &y);
            es2_gui_mouse_message msg = left ? (down ? ES2_GUI_WM_LBUTTONDOWN : ES2_GUI_WM_LBUTTONUP)
                                             : (down ? ES2_GUI_WM_RBUTTONDOWN : ES2_GUI_WM_RBUTTONUP);
            es2_gui_on_mouse(shell, msg, x, y, w->held_left, w->held_right);
            break;
        }
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP: {
            unsigned vk = es2_gui_sdl_vk(e.key.key, e.key.scancode, (e.key.mod & SDL_KMOD_NUM) != 0);
            if (vk)
                es2_gui_on_key(shell, vk, e.type == SDL_EVENT_KEY_UP, (e.key.mod & SDL_KMOD_SHIFT) != 0,
                               (e.key.mod & SDL_KMOD_ALT) != 0, (e.key.mod & SDL_KMOD_CTRL) != 0);
            break;
        }
        default: break;
        }
    }
    return 0;
}

static void present(void *context, es2_gui_shell *shell, es2_gui_present_kind kind, const es2_gui_rect *rects,
                    int count) {
    (void)kind;
    (void)rects;
    (void)count;
    es2_gui_sdl_present(context, &shell->screen);
}

/* Original: VSHELL.EXE 0x408d2f (SetCursor(LoadCursor(IDC_WAIT / IDC_ARROW)), ShowCursor) */
static void cursor(void *context, int busy) {
    es2_gui_window *w = context;
    SDL_SetCursor(busy ? w->wait : w->arrow);
    SDL_ShowCursor();
    w->busy_shown = busy;
}

static void fullscreen(void *context, int on) {
    es2_gui_window *w = context;
    SDL_SetWindowFullscreen(w->window, on != 0);
}

es2_gui_platform es2_gui_window_platform(es2_gui_window *w) {
    es2_gui_platform p = {w, pump, present, cursor, fullscreen};
    return p;
}

/* The native window handle (HWND) for the movie player, which opens its
 * MCI child window and receives MCI notifications in VSHELL's main window
 * (0x41e640: "parent %d"). NULL when SDL reports none. */
void *es2_gui_window_native(es2_gui_window *w) {
    if (!w || !w->window) return NULL;
    return SDL_GetPointerProperty(SDL_GetWindowProperties(w->window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
}

/* The window's icon comes from the span store only; no executable
 * is opened for it. */
void *es2_gui_store_icon(int size, char *error, size_t error_size) {
#ifdef _WIN32
    int id = LookupIconIdFromDirectoryEx((PBYTE)es2_vshell_icon_group, TRUE, size, size, LR_DEFAULTCOLOR);
    for (size_t i = 0; id && i < es2_vshell_icon_image_count; i++) {
        const es2_vshell_icon_image *m = &es2_vshell_icon_images[i];
        if (m->id != id) continue;
        HICON icon = CreateIconFromResourceEx((PBYTE)m->bytes, m->size, TRUE, 0x00030000, size, size, LR_DEFAULTCOLOR);
        if (!icon) snprintf(error, error_size, "the stored icon image %d could not be created", id);
        return icon;
    }
    snprintf(error, error_size, "no stored icon image for %d pixels", size);
    return NULL;
#else
    (void)size;
    snprintf(error, error_size, "no Win32");
    return NULL;
#endif
}

int es2_gui_window_icon(es2_gui_window *w, char *error, size_t error_size) {
#ifdef _WIN32
    HWND hwnd = es2_gui_window_native(w);
    if (!hwnd) {
        snprintf(error, error_size, "no Win32 window");
        return 0;
    }
    HICON big = es2_gui_store_icon(GetSystemMetrics(SM_CXICON), error, error_size);
    HICON little = big ? es2_gui_store_icon(GetSystemMetrics(SM_CXSMICON), error, error_size) : NULL;
    if (!little) {
        if (big) DestroyIcon(big);
        return 0;
    }
    SendMessageA(hwnd, WM_SETICON, ICON_BIG, (LPARAM)big);
    SendMessageA(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)little);
    if (w->icon_big) DestroyIcon(w->icon_big);
    if (w->icon_small) DestroyIcon(w->icon_small);
    w->icon_big = big;
    w->icon_small = little;
    return 1;
#else
    (void)w;
    snprintf(error, error_size, "no Win32 window");
    return 0;
#endif
}
