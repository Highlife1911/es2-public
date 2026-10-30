/* The windowed platform layer of the own dbsim.exe (own_process.md, "the
 * windowed platform layer"), the part both backends share: the Win32 messages
 * the original's window procedure 0x466034 receives (main_loop.md, "the main
 * loop, pump, window procedure, pause, limiter and clock"), the loop's window
 * seam (es2_sim_loop_window: the pump's queue, SetCursor, the pause Sleep, the
 * limiter's clock), the input seam (es2_sim_input_platform: GetKeyState, the
 * joystick, GetCursorPos, GetClientRect), the present's frame conversion and
 * the cursor rows. A backend turns its events into the calls below and shows
 * the converted frame: SDL3 in es2/sim/platform_sdl.h, the web platform's
 * SharedArrayBuffer in es2/sim/platform_web.h. The headless binary links
 * neither.
 *
 * Messages: input messages (WM_KEYDOWN/UP, WM_SYSKEYDOWN/UP with the set-1
 * scan code lParam, the mouse messages with client coordinates and MK_ flags)
 * are queued and retrieved by the pump; focus, close and size are sent
 * (dispatched inside the peek, as SendMessage would); an exposed window queues
 * WM_PAINT, which is retrieved last and cleared by every present (BeginPaint
 * validates). */
#ifndef ES2_SIM_PLATFORM_WINDOW_H
#define ES2_SIM_PLATFORM_WINDOW_H
#include <stdint.h>
#include "es2/sim/input.h"
#include "es2/sim/loop.h"
#include "es2/sim/present.h"
#include "es2/sim/cursor.h"

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;
struct SDL_Cursor;

#define ES2_SIM_WINDOW_QUEUE 128u

/* Win32 message numbers the window layer produces besides journal.h's
 * (winuser.h). */
#define ES2_WM_SIZE 0x0005u
#define ES2_WM_RBUTTONDOWN 0x0204u
#define ES2_WM_MBUTTONDOWN 0x0207u
#define ES2_WM_MOUSEWHEEL 0x020au

/* The joystick (the backend's default; tests replace it). */
typedef struct es2_sim_window_joystick {
    uint32_t (*caps)(void *user, uint32_t id, uint8_t *caps, uint32_t size);
    uint32_t (*pos)(void *user, uint32_t id, uint8_t *info, uint32_t size);
    void *user;
} es2_sim_window_joystick;

typedef struct es2_sim_window es2_sim_window;

/* What a backend does for the shared layer; NULL entries do nothing. */
typedef struct es2_sim_window_backend {
    /* The backend's pending events into the calls below; 0, or 1 when a
     * sent message was refused. */
    int (*pump)(es2_sim_window *w);
    /* The windowed replay: the pending events taken and dropped; their count. */
    unsigned (*drain)(es2_sim_window *w);
    /* SetCursor's shape: the wait cursor or the arrow. */
    void (*set_cursor)(es2_sim_window *w, int wait);
    /* The converted frame (w->pixels, width x height) into the window at
     * dst {x, y, w, h}. */
    void (*show)(es2_sim_window *w, int32_t width, int32_t height, const int32_t dst[4]);
    /* GetCursorPos in desktop coordinates (full screen: the game's); 1, or 0
     * as a failed call answers. */
    uint32_t (*cursor_pos)(const es2_sim_window *w, int32_t *x, int32_t *y);
    /* The full-screen switch; 1, or 0 when it was refused. */
    int (*display_mode)(es2_sim_window *w, int fullscreen);
    /* Full screen with the frame letterboxed into the desktop: the game's
     * point to the desktop's (SetCursorPos); NULL when the client is the
     * screen. */
    int (*letterboxed)(const es2_sim_window *w);
    void (*game_to_desktop)(const es2_sim_window *w, int32_t *x, int32_t *y);
} es2_sim_window_backend;

/* The pixel formats of w->pixels. */
#define ES2_SIM_WINDOW_XRGB8888 0   /* uint32_t 0x00RRGGBB */
#define ES2_SIM_WINDOW_RGBA 1       /* bytes R, G, B, 255 */

struct es2_sim_window {
    es2_sim_loop *loop;
    es2_sim_input *input;
    es2_sim_present *present;
    /* The seams this layer fills (loop->window points at `ops`). */
    es2_sim_loop_window ops;
    es2_sim_window_joystick joystick;
    es2_sim_window_backend backend;
    int pixel_format;
    /* The wall clock in ms and the sleep (the backend's; tests replace them). */
    uint64_t (*now_ms)(void *user);
    void (*delay_ms)(void *user, uint32_t ms);
    void *time_user;
    /* The window is open (es2_sim_window_open succeeded, not closed yet). */
    int open;
    /* The SDL backend's objects (NULL in the others). */
    struct SDL_Window *window;
    struct SDL_Renderer *renderer;
    struct SDL_Texture *texture;
    struct SDL_Cursor *arrow, *wait;
    uint32_t window_id;
    int32_t texture_w, texture_h;
    uint32_t *pixels;                     /* the conversion buffer (DIB w x h, 4 bytes per pixel in pixel_format) */
    uint32_t pixels_capacity;
    /* The client area (the game size; 0 x 0 while minimized, as
     * GetClientRect answers for an iconic window) and the window's client
     * origin on the desktop. */
    int32_t client_w, client_h, game_w, game_h, origin_x, origin_y;
    int minimized, focused;
    /* The guest's focus as the last WM_SETFOCUS (1) or WM_KILLFOCUS (0) told
     * it, and ShowWindow's activation still to be sent. */
    int guest_focus, focus_pending;
    /* The cursor handles of the start-up and the loop (stand-ins). */
    uint32_t cursor_arrow, cursor_wait;
    /* The queue of posted input messages and the pending paint. */
    struct { uint32_t message, wparam, lparam; } queue[ES2_SIM_WINDOW_QUEUE];
    unsigned head, count;
    int paint_pending;
    /* GetKeyState's table as of the last retrieved message: 0x80 down,
     * 0x01 toggled. */
    uint8_t keys[256];
    /* The key state as the events arrive (0x80 down): the messages' Alt and
     * previous-state bits, the MK_ flags. */
    uint8_t async[256];
    int alt_alone;                        /* DefWindowProcA: Alt went down with no key after it */
    uint32_t last_down;                   /* the last key that went down (Alt's key-up message) */
    uint32_t buttons;                     /* MK_LBUTTON 1, MK_RBUTTON 2, MK_MBUTTON 0x10 */
    int32_t mouse_x, mouse_y;
    /* The wall-clock pacing of virtual time. */
    int clock_based;
    uint64_t wall_base;
    uint32_t clock_base;
    /* Counts. */
    unsigned events, queued, dropped, sent, paints, presents, presents_outside, cursors, sleeps, paces, catch_ups,
        rebases, menu_keys, closes_alt_f4, ignored;
    uint64_t slept_ms;
    char error[160];
    /* AltGr: `altgr` the layout has one and this layer adds Windows' fake
     * left Ctrl (the SDL backend at the open; tests set it; the web platform's
     * page adds it itself), `altgr_ctrl` the fake left Ctrl this layer sent
     * for the right Alt is down. */
    int altgr, altgr_ctrl;
    /* The thread's key state at activation (0x80 down, 0x01 toggled; the
     * backend's; tests replace it). */
    void (*key_sync)(void *user, uint8_t state[256]);
    /* SetCursorPos for the cursor rows (the backend's; tests replace it) and
     * the binding es2/sim/cursor.h uses (client origin + point). */
    uint32_t (*set_cursor_pos)(void *user, int32_t x, int32_t y);
    es2_sim_cursor_platform cursor;
    unsigned resyncs, altgr_ctrls, presents_scaled, presents_empty, cursor_warps;
    /* The full-screen switch's requests (present.h, display_mode): the mode
     * asked last (1 full screen with the game size letterboxed, 0 the
     * window), the requests, and the ones the backend refused (no window, or
     * the backend's answer). */
    int fullscreen;
    unsigned display_switches, display_failures;
    /* The class icon: the name LoadIconA is given, set before the open; the
     * two HICONs the window shows and why there are none (SDL backend). */
    const char *icon_name;
    void *icon_big, *icon_small;
    const char *icon_error;
};

/* Zeroes the record and fills the seams over `loop`, `input` and `present`
 * (loop->window, the input platform, present->on_present) with the shared
 * layer; no backend. es2_sim_window_init (per backend) calls this and sets the
 * backend's defaults. */
void es2_sim_window_core_init(es2_sim_window *w, es2_sim_loop *loop, es2_sim_input *input, es2_sim_present *present);

/* ---- per backend (platform_sdl.c or platform_web.c) */
/* es2_sim_window_core_init with the backend's defaults. No window yet: the
 * peek answers an empty queue until es2_sim_window_open. */
void es2_sim_window_init(es2_sim_window *w, es2_sim_loop *loop, es2_sim_input *input, es2_sim_present *present);
/* The backend's video set-up; 0, or 1 with w->error. */
int es2_sim_window_video_init(es2_sim_window *w);
/* The desktop and frame metrics the start-up's window rectangle reads
 * (GetSystemMetrics SM_CXSCREEN, SM_CYSCREEN, SM_CXFRAME, SM_CYFRAME,
 * SM_CYCAPTION). */
void es2_sim_window_metrics(uint32_t *screen_cx, uint32_t *screen_cy, uint32_t *frame_cx, uint32_t *frame_cy,
                            uint32_t *caption);
/* Opens the window "EarthSiege II" with a client area of `width` x
 * `height` (the game size [0x4d253e] x [0x4d2542]) at the client origin
 * (x, y), shown; queues the creation paint (UpdateWindow) and the
 * activation. 0, or 1 with w->error. */
int es2_sim_window_open(es2_sim_window *w, int32_t width, int32_t height, int32_t x, int32_t y);
void es2_sim_window_close(es2_sim_window *w);

/* ---- the backend's events (shared). Each returns 0, or 1 when a sent
 * message was refused; an event that produces nothing counts as ignored. */
/* A key with its virtual-key code (0: a key the platform ignores), set-1 scan
 * code and extended flag; `altgr_key` the right Alt on a layout with AltGr
 * (w->altgr), which gets Windows' fake left Ctrl. */
int es2_sim_window_key_event(es2_sim_window *w, uint32_t vk, uint32_t scan, int extended, int down, int altgr_key);
/* The mouse at client (x, y); button 0 left, 1 right, 2 middle. */
int es2_sim_window_motion_event(es2_sim_window *w, int32_t x, int32_t y);
int es2_sim_window_button_event(es2_sim_window *w, unsigned button, int down, int32_t x, int32_t y);
/* The wheel's delta in WHEEL_DELTA units of 120 per notch. */
int es2_sim_window_wheel_event(es2_sim_window *w, int32_t delta);
int es2_sim_window_focus_event(es2_sim_window *w, int gained);
int es2_sim_window_close_event(es2_sim_window *w);
int es2_sim_window_expose_event(es2_sim_window *w);
int es2_sim_window_minimize_event(es2_sim_window *w, int minimized);
int es2_sim_window_move_event(es2_sim_window *w, int32_t x, int32_t y);

/* The windowed replay's peek: the backend's pending events dropped; their
 * count. */
unsigned es2_sim_window_drain(es2_sim_window *w);

/* The lParam of a key message (repeat count 1). */
uint32_t es2_sim_window_key_lparam(uint32_t scan, int extended, int alt, int previous, int up);

/* The activation's key-state synchronisation: GetKeyState's table and the event
 * state from `state` (GetKeyboardState's layout); called before WM_SETFOCUS is
 * sent. */
void es2_sim_window_resync(es2_sim_window *w, const uint8_t state[256]);

/* The 8-bit window of a present through a colour table (RGBQUAD x 256)
 * into XRGB8888 pixels (w x h, rows `stride` bytes apart), or into RGBA bytes
 * (R, G, B, 255; 4 x w x h). */
void es2_sim_window_convert(const uint8_t *bits, int32_t stride, int32_t w, int32_t h, const uint8_t *table,
                            uint32_t *out);
void es2_sim_window_convert_rgba(const uint8_t *bits, int32_t stride, int32_t w, int32_t h, const uint8_t *table,
                                 uint8_t *out);

#endif
