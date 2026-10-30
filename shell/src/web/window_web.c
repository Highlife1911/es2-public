/* The shell's window in the web build (window_web.h). */
#include "web/window_web.h"

#include <stdint.h>

#define QUEUE 4096   /* the input ring's size: one ring of the page's entries fits */

static struct {
    es2_web_input entry[QUEUE];
    unsigned head, count;
    int held_left, held_right;
    int busy_shown;
    uint8_t rgba[ES2_GUI_WIDTH * ES2_GUI_HEIGHT * 4];
} w;

/* The VKs es2_gui_sdl_vk gives: window.c passes no other key to the
 * window procedure. */
static int known_vk(unsigned vk) {
    if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z')) return 1;
    if (vk >= 0x21 && vk <= 0x28) return 1;                 /* PageUp .. Down */
    if (vk >= 0x60 && vk <= 0x69) return 1;                 /* VK_NUMPAD0 .. 9 */
    if (vk >= 0x70 && vk <= 0x7b) return 1;                 /* F1 .. F12 */
    switch (vk) {
    case 0x08: case 0x09: case 0x0c: case 0x0d: case 0x10: case 0x11: case 0x12: case 0x14: case 0x1b: case 0x20:
    case 0x2d: case 0x2e: case 0x6b: case 0x6d: case 0x6e: case 0x90: case 0x91:
        return 1;
    default: return 0;
    }
}

void es2_web_window_dispatch(const es2_web_input *e) {
    if (e->type == ES2_WEB_INPUT_MCI) return;
    if (w.count == QUEUE) return;   /* SDL drops events beyond its queue as well */
    w.entry[(w.head + w.count) % QUEUE] = *e;
    w.count++;
}

int es2_web_window_take_all(void) {
    es2_web_input batch[64];
    int quit = 0, n;
    while ((n = es2_web_input_take(batch, 64)) > 0) {
        for (int i = 0; i < n; i++) {
            if (batch[i].type == ES2_WEB_INPUT_QUIT) quit = 1;
            else es2_web_window_dispatch(&batch[i]);
        }
    }
    return quit;
}

static int pump(void *context, es2_gui_shell *shell) {
    (void)context;
    if (es2_web_window_take_all()) return 1;   /* end of the session (window.c's SDL_EVENT_QUIT) */
    while (w.count) {
        es2_web_input e = w.entry[w.head];
        w.head = (w.head + 1) % QUEUE;
        w.count--;
        switch (e.type) {
        case ES2_WEB_INPUT_CLOSE: es2_gui_on_close(shell); break;
        case ES2_WEB_INPUT_FOCUS: es2_gui_on_focus(shell, e.a != 0); break;
        case ES2_WEB_INPUT_EXPOSE: es2_gui_on_paint(shell); break;
        case ES2_WEB_INPUT_MOVE:
            if (w.busy_shown) { /* DefWindowProc's WM_SETCURSOR restores the class arrow */
                es2_web_cursor(0);
                w.busy_shown = 0;
            }
            es2_gui_on_mouse(shell, ES2_GUI_WM_MOUSEMOVE, e.a, e.b, w.held_left, w.held_right);
            break;
        case ES2_WEB_INPUT_BUTTON: {
            int down = e.d != 0, left = e.c == 0;
            if (e.c != 0 && e.c != 1) break;
            if (left) w.held_left = down;
            else w.held_right = down;
            es2_gui_mouse_message msg = left ? (down ? ES2_GUI_WM_LBUTTONDOWN : ES2_GUI_WM_LBUTTONUP)
                                             : (down ? ES2_GUI_WM_RBUTTONDOWN : ES2_GUI_WM_RBUTTONUP);
            es2_gui_on_mouse(shell, msg, e.a, e.b, w.held_left, w.held_right);
            break;
        }
        case ES2_WEB_INPUT_KEY:
            if (known_vk((unsigned)e.b))
                es2_gui_on_key(shell, (unsigned)e.b, (e.c & ES2_WEB_KEY_UP) != 0, (e.c & ES2_WEB_KEY_SHIFT) != 0,
                               (e.c & ES2_WEB_KEY_ALT) != 0, (e.c & ES2_WEB_KEY_CTRL) != 0);
            break;
        default: break;
        }
    }
    return 0;
}

static void present(void *context, es2_gui_shell *shell, es2_gui_present_kind kind, const es2_gui_rect *rects,
                    int count) {
    (void)context;
    (void)kind;
    (void)rects;
    (void)count;
    if (es2_gui_screen_pixels(&shell->screen, ES2_GUI_PIXELS_RGBA, w.rgba, ES2_GUI_WIDTH * 4))
        es2_web_present(w.rgba, ES2_GUI_WIDTH, ES2_GUI_HEIGHT);
}

/* Original: VSHELL.EXE 0x408d2f (SetCursor(LoadCursor(IDC_WAIT / IDC_ARROW)), ShowCursor) */
static void cursor(void *context, int busy) {
    (void)context;
    es2_web_cursor(busy != 0);
    w.busy_shown = busy;
}

static void fullscreen(void *context, int on) {
    (void)context;
    es2_web_fullscreen(on != 0);
}

es2_gui_platform es2_web_window_platform(void) {
    es2_gui_platform p = {NULL, pump, present, cursor, fullscreen};
    return p;
}
