/* The two USER32 imports of DBSIM's cursor warp 0x4808dc (own_process.md, "the
 * window's open points"): ClientToScreen (thunk 0x49699a) and SetCursorPos
 * (thunk 0x4969e2), as service rows (sim/dispatch/services/cursor_warp.h) over a
 * bound platform. Headless (nothing bound) they answer as the oracle's hidden
 * window does: SetCursorPos TRUE without moving anything (the proxy's
 * window_shim.h), ClientToScreen TRUE with the point unchanged (only that
 * SetCursorPos reads it). The windowed layer (platform_sdl.c) binds the
 * window's client origin and the real cursor. */
#ifndef ES2_SIM_CURSOR_H
#define ES2_SIM_CURSOR_H
#include <stdint.h>

#define ES2_SIM_VA_CLIENT_TO_SCREEN 0x0049699au
#define ES2_SIM_VA_SET_CURSOR_POS 0x004969e2u

typedef struct es2_sim_cursor_platform {
    /* ClientToScreen(hwnd, point): the point from client to screen
     * coordinates; the BOOL result. */
    uint32_t (*client_to_screen)(void *user, uint32_t hwnd, int32_t *x, int32_t *y);
    /* SetCursorPos(x, y): the BOOL result. */
    uint32_t (*set_pos)(void *user, int32_t x, int32_t y);
    void *user;
} es2_sim_cursor_platform;

/* The platform the rows use (NULL: headless). The record must outlive the
 * binding. */
void es2_sim_cursor_bind(const es2_sim_cursor_platform *platform);

/* Calls of the two rows and the last SetCursorPos point, for the log and
 * the tests. */
typedef struct es2_sim_cursor_calls {
    unsigned client_to_screen, set_pos;
    int32_t last_x, last_y;
} es2_sim_cursor_calls;
const es2_sim_cursor_calls *es2_sim_cursor_calls_get(void);
void es2_sim_cursor_calls_reset(void);

#endif
