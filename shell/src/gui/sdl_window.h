#ifndef ES2_SHELL_GUI_SDL_WINDOW_H
#define ES2_SHELL_GUI_SDL_WINDOW_H
/* State shared by window.c and present_sdl.c (SDL3 only). */
#include "es2/shell/gui/window.h"

#include <SDL3/SDL.h>

struct es2_gui_window {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;   /* one streaming 640 x 480 XRGB8888 texture */
    SDL_Cursor *arrow, *wait;
    int busy_shown;         /* the wait cursor is up until the next mouse move */
    int held_left, held_right;
    void *icon_big, *icon_small; /* HICONs of es2_gui_window_icon, destroyed on close */
};

/* Upload the shell's screen and draw it scaled and centred (present_sdl.c). */
void es2_gui_sdl_present(es2_gui_window *window, const es2_gui_screen *screen);
/* Output pixel layout of the current window size (present_sdl.c). */
int es2_gui_sdl_layout(es2_gui_window *window, es2_gui_layout *layout, float *pixels_per_point);

#endif
