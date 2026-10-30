/* The SDL3 presenter of the own shell: one streaming texture holding
 * the shell's screen (es2_gui_screen, already converted through the colour
 * table the way VSHELL's BitBlt converts), drawn with nearest-neighbour
 * sampling at the largest integer scale that fits, centred on black
 * (VSHELL itself never scales, see vshell_presentation.md, "the own shell's
 * frame, presenter and window"). */
#include "sdl_window.h"

int es2_gui_sdl_layout(es2_gui_window *w, es2_gui_layout *layout, float *pixels_per_point) {
    int ow = 0, oh = 0, ww = 0, wh = 0;
    if (!SDL_GetCurrentRenderOutputSize(w->renderer, &ow, &oh)) return 0;
    if (!es2_gui_layout_compute(ow, oh, layout)) return 0;
    if (pixels_per_point) {
        *pixels_per_point = 1.0f;
        if (SDL_GetWindowSize(w->window, &ww, &wh) && ww > 0) *pixels_per_point = (float)ow / (float)ww;
    }
    return 1;
}

void es2_gui_sdl_present(es2_gui_window *w, const es2_gui_screen *screen) {
    es2_gui_layout layout;
    if (!SDL_UpdateTexture(w->texture, NULL, screen->rgb, ES2_GUI_WIDTH * 4)) return;
    if (!es2_gui_sdl_layout(w, &layout, NULL)) return;
    SDL_FRect target = {(float)layout.x, (float)layout.y, (float)layout.width, (float)layout.height};
    SDL_SetRenderDrawColor(w->renderer, 0, 0, 0, 255);
    SDL_RenderClear(w->renderer);
    SDL_RenderTexture(w->renderer, w->texture, NULL, &target);
    SDL_RenderPresent(w->renderer);
}
