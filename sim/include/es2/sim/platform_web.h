/* The web backend of the windowed platform layer (es2/sim/platform_window.h;
 * docs/web_build.md sections 3, 9 and 12). es2sim_window's wasm build runs in
 * a worker; the page fills the input ring of web/shared.mjs, reads the frame
 * slots and keeps the key bitmap. The "es2sim" imports below are the worker's
 * side (web/es2sim_imports.mjs); web/node/sim_window_host.mjs serves the same
 * module from a worker thread under Node.
 *
 * Input entries become the shared layer's events: a key with its set-1 scan
 * code (0x100 for the E0 prefix) and the page's VK, whose keypad keys get
 * their VK from GetKeyState's NumLock as the SDL backend's do; the page sends
 * the fake left Ctrl of AltGr itself (w->altgr stays 0). An MCI entry is
 * MM_MCINOTIFY for the windowed audio (es2_sim_window_web_mci); the windowed
 * replay's drain routes it as well, as SDL_PollEvent dispatches the notify
 * window's messages natively. SetCursorPos sets a virtual cursor, which
 * GetCursorPos answers until the next real move, and its move comes back as
 * one motion event at the next pump. A message box is the page's modal
 * dialog; the worker waits for its button. Full screen is the page's, letterboxed
 * there: the client coordinates the page sends are the game's. */
#ifndef ES2_SIM_PLATFORM_WEB_H
#define ES2_SIM_PLATFORM_WEB_H
#include <stdint.h>
#include "es2/sim/platform_window.h"

/* What the start-up line names as the platform. */
#define ES2_SIM_WINDOW_PLATFORM "web"

/* One entry of the input ring (eight int32; web/shared.mjs). Coordinates are
 * client pixels of the game size. */
typedef struct es2_sim_web_input {
    int32_t type, a, b, c, d, time, reserved[2];
} es2_sim_web_input;

enum {
    ES2_SIM_WEB_INPUT_KEY = 1,      /* a scan (0x100 | code after E0), b VK, c ES2_SIM_WEB_KEY_* */
    ES2_SIM_WEB_INPUT_MOVE = 2,     /* a x, b y */
    ES2_SIM_WEB_INPUT_BUTTON = 3,   /* a x, b y, c button (0 left, 1 right, 2 middle), d 1 down / 0 up */
    ES2_SIM_WEB_INPUT_FOCUS = 4,    /* a 1 gained / 0 lost */
    ES2_SIM_WEB_INPUT_EXPOSE = 5,
    ES2_SIM_WEB_INPUT_CLOSE = 6,
    ES2_SIM_WEB_INPUT_QUIT = 7,     /* the end of the session (the simulation has no use for it) */
    ES2_SIM_WEB_INPUT_MCI = 8,      /* MM_MCINOTIFY: a wParam (MCI_NOTIFY_*), b device id */
    ES2_SIM_WEB_INPUT_SIZE = 9,     /* a 1 minimized / 0 restored */
    ES2_SIM_WEB_INPUT_WHEEL = 10,   /* a the delta, 120 per notch */
};

enum { ES2_SIM_WEB_KEY_UP = 1, ES2_SIM_WEB_KEY_REPEAT = 2 };

/* The receiver of the MCI entries (the windowed audio's notify queue). */
void es2_sim_window_web_mci(es2_sim_window *w, void (*notify)(void *user, uint32_t wparam, uint32_t lparam), void *user);

/* MessageBoxA as the page's modal dialog (ANSI text and caption, the MB_*
 * style); the button the page answers. */
uint32_t es2_sim_window_web_message_box(const char *text, const char *caption, uint32_t style);

/* One input entry into the window's messages; 0, or 1 when a sent message
 * was refused. */
int es2_sim_window_web_event(es2_sim_window *w, const es2_sim_web_input *e);

#endif
