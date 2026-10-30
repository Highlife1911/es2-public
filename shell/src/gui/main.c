/* es2shell.exe: the graphical shell (shell_main.h) on SDL3 and Win32. GUI
 * subsystem like VSHELL.EXE: no console, also not in headless mode. */
#define SDL_MAIN_HANDLED
#include "gui/shell_main.h"
#include "gui/sound_sdl.h"
#include "es2/shell/gui/window.h"

#include <SDL3/SDL.h>
#include <windows.h>

static uint32_t sdl_clock(void *context) { (void)context; return (uint32_t)SDL_GetTicks(); }

static int client_size(void *context, int *width, int *height) {
    RECT r;
    if (!context || !GetClientRect((HWND)context, &r)) return 0;
    *width = r.right - r.left;
    *height = r.bottom - r.top;
    return 1;
}

static void idle(void *context, uint32_t milliseconds) {
    (void)context;
    MsgWaitForMultipleObjects(0, NULL, FALSE, milliseconds, QS_ALLINPUT);
}

static void message_box(void *context, const char *text, const char *title, uint32_t flags) {
    MessageBoxA((HWND)context, text, title, flags);
}

static void *display_open(void *context, int fullscreen, es2_gui_platform *platform, es2_app_platform *app,
                          es2_os_stream *log) {
    char error[256];
    (void)context;
    es2_gui_window *window = es2_gui_window_open(fullscreen, error, sizeof error);
    if (!window) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "EarthSiege II", error, NULL);
        return NULL;
    }
    *platform = es2_gui_window_platform(window);
    if (app) {
        /* VSHELL's class icon "es2icon" (0x40687c), platform deviation 9;
         * from the span store */
        if (!es2_gui_window_icon(window, error, sizeof error) && log)
            es2_os_fprintf(log, "no window icon: %s\n", error);
        app->native_window = es2_gui_window_native(window);
        app->audio_device = es2_app_sound_sdl_device;
        app->clock = sdl_clock;
        app->client_size = client_size;
        app->idle = idle;
        app->message_box = message_box;
        app->window_context = app->native_window;
    }
    return window;
}

static void display_close(void *context, void *window) {
    (void)context;
    es2_app_sound_sdl_shutdown();
    es2_gui_window_close(window);
}

/* own: no busy spin between the passes (a deviation) */
static void display_wait(void *context, void *window) {
    (void)context;
    (void)window;
    SDL_WaitEventTimeout(NULL, 1);
}

static void stop_box(void *context, const char *text) {
    (void)context;
    MessageBoxA(NULL, text, "EarthSiege II", MB_ICONSTOP);
}

int main(int argc, char **argv) {
    static const es2_shell_display display = {NULL, display_open, display_close, display_wait, stop_box};
    return es2_shell_main(argc, argv, &display);
}
