#ifndef ES2_SHELL_GUI_SHELL_MAIN_H
#define ES2_SHELL_GUI_SHELL_MAIN_H

/* The graphical shell's process (shell_main.c) over a display: main.c
 * (SDL3 and Win32) and shell/src/web/main_web.c (the web host). Headless
 * runs never call the display. */
#include "gui/app.h"
#include "es2/platform/os.h"
#include "es2/shell/gui/loop.h"

typedef struct es2_shell_display {
    void *context;
    /* Opens the window (`fullscreen`: 1 for --fullscreen) and fills the
     * frame's platform; with `app` also the presenter's window fields
     * (native_window, audio_device, clock, client_size, idle, message_box,
     * window_context), with `log` the window's notes. NULL when there is
     * no window, after the platform's own error report. */
    void *(*open)(void *context, int fullscreen, es2_gui_platform *platform, es2_app_platform *app,
                  es2_os_stream *log);
    void (*close)(void *context, void *window);
    /* Between main-loop passes: waits for input up to about a millisecond. */
    void (*wait)(void *context, void *window);
    /* A stop message without an owner window: MessageBoxA(NULL, text,
     * "EarthSiege II", MB_ICONSTOP). */
    void (*stop_box)(void *context, const char *text);
} es2_shell_display;

int es2_shell_main(int argc, char **argv, const es2_shell_display *display);

#endif
