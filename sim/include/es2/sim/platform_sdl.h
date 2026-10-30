/* The windowed platform of the own dbsim.exe over SDL3 (own_process.md, "the
 * windowed platform layer"). `es2sim` without `--headless` (the windowed build
 * //sim:es2sim_window) opens a window of the game size, presents the 8-bit
 * frame of the GDI present (presentation.md) through the last uploaded colour
 * table, and answers the loop's window seam (es2_sim_loop_window: the pump's
 * queue, SetCursor, the pause Sleep, the limiter's clock) and the input
 * seam (es2_sim_input_platform: GetKeyState, joyGetDevCapsA/joyGetPosEx,
 * GetCursorPos, GetClientRect) from SDL3 events and devices. The headless
 * binary does not link this.
 *
 * Messages: SDL events become the Win32 messages the original's window
 * procedure 0x466034 receives (main_loop.md, "the main loop, pump, window
 * procedure, pause, limiter and clock"): input messages (WM_KEYDOWN/UP,
 * WM_SYSKEYDOWN/UP with the set-1 scan code lParam, the mouse messages with
 * client coordinates and MK_ flags) are queued and retrieved by the pump;
 * focus, close and size are sent (dispatched inside the peek, as SendMessage
 * would); an exposed window queues WM_PAINT, which is retrieved last and
 * cleared by every present (BeginPaint validates). */
#ifndef ES2_SIM_PLATFORM_SDL_H
#define ES2_SIM_PLATFORM_SDL_H
#include <stdint.h>
#include "es2/sim/input.h"
#include "es2/sim/loop.h"
#include "es2/sim/present.h"
#include "es2/sim/cursor.h"
#include "es2/sim/image.h"

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;
struct SDL_Cursor;
union SDL_Event;

#define ES2_SIM_WINDOW_QUEUE 128u

/* Win32 message numbers the window layer produces besides journal.h's
 * (winuser.h). */
#define ES2_WM_SIZE 0x0005u
#define ES2_WM_RBUTTONDOWN 0x0204u
#define ES2_WM_MBUTTONDOWN 0x0207u
#define ES2_WM_MOUSEWHEEL 0x020au

/* The joystick (winmm's joyGetDevCapsA / joyGetPosEx by default: the
 * original's own API; tests replace it). */
typedef struct es2_sim_window_joystick {
    uint32_t (*caps)(void *user, uint32_t id, uint8_t *caps, uint32_t size);
    uint32_t (*pos)(void *user, uint32_t id, uint8_t *info, uint32_t size);
    void *user;
} es2_sim_window_joystick;

typedef struct es2_sim_window {
    es2_sim_loop *loop;
    es2_sim_input *input;
    es2_sim_present *present;
    /* The seams this layer fills (loop->window points at `ops`). */
    es2_sim_loop_window ops;
    es2_sim_window_joystick joystick;
    /* The wall clock in ms and the sleep (SDL_GetTicks / SDL_Delay; tests
     * replace them). */
    uint64_t (*now_ms)(void *user);
    void (*delay_ms)(void *user, uint32_t ms);
    void *time_user;
    /* SDL objects (NULL until es2_sim_window_open). */
    struct SDL_Window *window;
    struct SDL_Renderer *renderer;
    struct SDL_Texture *texture;
    struct SDL_Cursor *arrow, *wait;
    uint32_t window_id;
    int32_t texture_w, texture_h;
    uint32_t *pixels;                     /* the XRGB8888 conversion buffer (DIB w x h) */
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
    /* AltGr: `altgr` the layout has one (es2_sim_window_layout_altgr
     * at the open; tests set it), `altgr_ctrl` the fake left Ctrl this layer
     * sent for the right Alt is down. */
    int altgr, altgr_ctrl;
    /* The thread's key state at activation (GetKeyboardState by default:
     * 0x80 down, 0x01 toggled; tests replace it). */
    void (*key_sync)(void *user, uint8_t state[256]);
    /* SetCursorPos for the cursor rows (user32's by default; tests replace
     * it) and the binding es2/sim/cursor.h uses (client origin + point). */
    uint32_t (*set_cursor_pos)(void *user, int32_t x, int32_t y);
    es2_sim_cursor_platform cursor;
    unsigned resyncs, altgr_ctrls, presents_scaled, presents_empty, cursor_warps;
    /* The full-screen switch's requests (present.h, display_mode): the mode
     * asked last (1 desktop full screen with the game size letterboxed, 0 the
     * window), the requests, and the ones SDL refused (no window, or the
     * video driver's answer). */
    int fullscreen;
    unsigned display_switches, display_failures;
    /* The class icon: the name LoadIconA is given, set before the open (looked
     * up in the own resources, es2_sim_window_load_icon_own); the two HICONs the window shows and why there
     * are none. */
    const char *icon_name;
    void *icon_big, *icon_small;
    const char *icon_error;
} es2_sim_window;

/* Zeroes the record and fills the seams over `loop`, `input` and `present`
 * (loop->window, the input platform, present->on_present). No window yet:
 * the peek answers an empty queue until es2_sim_window_open. */
void es2_sim_window_init(es2_sim_window *w, es2_sim_loop *loop, es2_sim_input *input, es2_sim_present *present);

/* SDL's video subsystem with the hints of the contract (Alt+F4 and the last
 * window's close are the game's, DPI-unaware, no mouse auto-capture, no
 * signal handlers); 0, or 1 with w->error. */
int es2_sim_window_video_init(es2_sim_window *w);

/* The desktop and frame metrics the start-up's window rectangle reads
 * (GetSystemMetrics SM_CXSCREEN, SM_CYSCREEN, SM_CXFRAME, SM_CYFRAME,
 * SM_CYCAPTION), from the system as the original reads them. */
void es2_sim_window_metrics(uint32_t *screen_cx, uint32_t *screen_cy, uint32_t *frame_cx, uint32_t *frame_cy,
                            uint32_t *caption);

/* LoadIconA(hInstance, name) over the own resources (es2_sim_resource_own, the
 * bytes of sim/data/dbsim_image_tables.c): the group icon `name`, its best
 * image for cx x cy as Windows chooses it (LookupIconIdFromDirectoryEx) and the
 * HICON of that size (CreateIconFromResourceEx); NULL and *error when there is
 * none. The window open takes the class icon from here. */
void *es2_sim_window_load_icon_own(const char *name, int cx, int cy, const char **error);

/* Opens the window "EarthSiege II" with a client area of `width` x
 * `height` (the game size [0x4d253e] x [0x4d2542]) at the client origin
 * (x, y), not resizable, shown, with the class icon when `icon_name` is set
 * (a missing icon leaves w->icon_error and no icon, as a failed LoadIconA
 * leaves the class without one); queues the creation paint (UpdateWindow).
 * 0, or 1 with w->error. */
int es2_sim_window_open(es2_sim_window *w, int32_t width, int32_t height, int32_t x, int32_t y);
void es2_sim_window_close(es2_sim_window *w);

/* One SDL event into the window's messages (the peek pumps SDL and calls
 * this for every event); 0, or 1 when a sent message was refused. */
int es2_sim_window_event(es2_sim_window *w, const union SDL_Event *event);

/* The key's virtual-key code, set-1 scan code and extended flag for an SDL
 * scancode (`numlock`: the keypad's digits); 0 when the key has none. */
uint32_t es2_sim_window_vk(uint32_t sdl_scancode, int numlock, uint32_t *scan, int *extended);

/* The lParam of a key message (repeat count 1). */
uint32_t es2_sim_window_key_lparam(uint32_t scan, int extended, int alt, int previous, int up);

/* Whether the keyboard layout has AltGr (Windows then sends a fake left Ctrl
 * before the right Alt): some virtual key gives a character with Ctrl and Alt
 * down (ToUnicodeEx without changing the keyboard state). */
int es2_sim_window_layout_altgr(void);

/* The activation's key-state synchronisation: GetKeyState's table and the event
 * state from `state` (GetKeyboardState's layout); called before WM_SETFOCUS is
 * sent. */
void es2_sim_window_resync(es2_sim_window *w, const uint8_t state[256]);

/* The 8-bit window of a present through a colour table (RGBQUAD x 256)
 * into XRGB8888 pixels (w x h, rows `stride` bytes apart). */
void es2_sim_window_convert(const uint8_t *bits, int32_t stride, int32_t w, int32_t h, const uint8_t *table,
                            uint32_t *out);

#endif
