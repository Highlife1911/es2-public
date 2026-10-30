/* es2shell.wasm: the graphical shell (shell_main.h) on the web platform
 * (window_web.h, sound_web.h, the "es2shell" imports). */
#include "gui/shell_main.h"
#include "web/sound_web.h"
#include "web/web_imports.h"
#include "web/window_web.h"

#define MB_ICONSTOP 0x10

static int window_token;

/* The fly-over's busy waits (es2_mission_map_wait) read only the clock;
 * natively SDL's audio thread plays on meanwhile. */
static uint32_t clock_ms(void *context) {
    (void)context;
    es2_web_sound_fill();
    return es2_web_clock();
}

static int client_size(void *context, int *width, int *height) {
    (void)context;
    *width = ES2_GUI_WIDTH;
    *height = ES2_GUI_HEIGHT;
    return 1;
}

static void idle(void *context, uint32_t milliseconds) {
    (void)context;
    es2_web_sound_wait(milliseconds);
}

static void sleep_ms(void *context, uint32_t milliseconds) {
    (void)context;
    es2_web_sound_sleep(milliseconds);
}

static void message_box(void *context, const char *text, const char *title, uint32_t flags) {
    (void)context;
    es2_web_message_box(text, title ? title : "", flags);
}

static void *display_open(void *context, int fullscreen, es2_gui_platform *platform, es2_app_platform *app,
                          es2_os_stream *log) {
    (void)context;
    (void)log;
    *platform = es2_web_window_platform();
    if (fullscreen) es2_web_fullscreen(1);
    if (app) {
        app->native_window = &window_token;   /* the player's parent: the page's canvas */
        app->audio_device = es2_web_sound_device;
        app->clock = clock_ms;
        app->client_size = client_size;
        app->idle = idle;
        app->sleep = sleep_ms;
        app->message_box = message_box;
        app->window_context = NULL;
    }
    return &window_token;
}

static void display_close(void *context, void *window) {
    (void)context;
    (void)window;
    es2_web_sound_shutdown();
}

static void display_wait(void *context, void *window) {
    (void)context;
    (void)window;
    es2_web_sound_wait(1);
}

static void stop_box(void *context, const char *text) {
    (void)context;
    es2_web_message_box(text, "EarthSiege II", MB_ICONSTOP);
}

int main(int argc, char **argv) {
    static const es2_shell_display display = {NULL, display_open, display_close, display_wait, stop_box};
    if (!es2_os_set_command_line(argc, argv)) return 1;
    return es2_shell_main(argc, argv, &display);
}
