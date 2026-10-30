/* The SDL3 backend of the windowed platform layer (es2/sim/platform_window.h;
 * own_process.md, "the windowed platform layer"). `es2sim` without
 * `--headless` (the windowed build //sim:es2sim_window) opens an SDL window of
 * the game size and presents the 8-bit frame of the GDI present
 * (presentation.md) through the last uploaded colour table; SDL3 events become
 * the shared layer's events, and Windows answers what the original asks it
 * (the layout's virtual keys, GetKeyboardState at activation, GetCursorPos,
 * SetCursorPos, joyGetDevCapsA/joyGetPosEx, GetSystemMetrics, the class icon). */
#ifndef ES2_SIM_PLATFORM_SDL_H
#define ES2_SIM_PLATFORM_SDL_H
#include <stdint.h>
#include "es2/sim/platform_window.h"
#include "es2/sim/image.h"

union SDL_Event;

/* What the start-up line names as the platform. */
#define ES2_SIM_WINDOW_PLATFORM "SDL3"

/* es2_sim_window_init (platform_window.h) fills the SDL backend, the wall
 * clock (SDL_GetTicks / SDL_Delay), winmm's joystick, GetKeyboardState at
 * activation and user32's SetCursorPos. es2_sim_window_video_init sets SDL's
 * video subsystem up with the hints of the contract (Alt+F4 and the last
 * window's close are the game's, DPI-unaware, no mouse auto-capture, no signal
 * handlers). es2_sim_window_open opens the window not resizable, with the class
 * icon when `icon_name` is set (a missing icon leaves w->icon_error and no
 * icon, as a failed LoadIconA leaves the class without one), and reads the
 * toggles and the layout's AltGr. */

/* LoadIconA(hInstance, name) over the own resources (es2_sim_resource_own, the
 * bytes of sim/data/dbsim_image_tables.c): the group icon `name`, its best
 * image for cx x cy as Windows chooses it (LookupIconIdFromDirectoryEx) and the
 * HICON of that size (CreateIconFromResourceEx); NULL and *error when there is
 * none. The window open takes the class icon from here. */
void *es2_sim_window_load_icon_own(const char *name, int cx, int cy, const char **error);

/* One SDL event into the window's messages (the peek pumps SDL and calls
 * this for every event); 0, or 1 when a sent message was refused. */
int es2_sim_window_event(es2_sim_window *w, const union SDL_Event *event);

/* The key's virtual-key code, set-1 scan code and extended flag for an SDL
 * scancode (`numlock`: the keypad's digits); 0 when the key has none. */
uint32_t es2_sim_window_vk(uint32_t sdl_scancode, int numlock, uint32_t *scan, int *extended);

/* Whether the keyboard layout has AltGr (Windows then sends a fake left Ctrl
 * before the right Alt): some virtual key gives a character with Ctrl and Alt
 * down (ToUnicodeEx without changing the keyboard state). */
int es2_sim_window_layout_altgr(void);

#endif
