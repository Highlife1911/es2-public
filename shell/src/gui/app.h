#ifndef ES2_SHELL_GUI_APP_H
#define ES2_SHELL_GUI_APP_H

/* The graphical shell's presenter and host (es2shell.exe). It binds
 * the merged pieces into one program: the shared host (shell/src/host.h:
 * menu.ini, the flow's file layer over the state engine, slots, practice
 * launch), the layout table's screens on the widget toolkit (main menu,
 * Registration, Saved games, practice, Preferences, service bay and squad
 * panel, WEAPONS, the mission screen, the boxes), their resources from the
 * installation's VOLs and VSHELL's tables in the span store
 * (VER95\VSHELL.EXE is not opened), the movie player, the sounds and the
 * reaction to the flow's state after every event.
 *
 * Contract: docs/reverse-engineering/launcher.md, section "Graphical
 * shell"; the screens' contracts are vshell_ui.md sections 18 .. 33. */
#include "host.h"
#include "es2/shell/gui/loop.h"
#include "es2/shell/gui/widgets.h"
#include <stdio.h>

typedef struct es2_app es2_app;

/* What the process gives the presenter. */
typedef struct es2_app_platform {
    int headless;            /* no window: movies are logged, no audio, no message boxes */
    void *native_window;     /* the movie player's parent (the HWND of the SDL window), NULL headless */
    es2_os_stream *log;               /* shell.log of the menu directory (may be NULL) */
    int sound;               /* the sound flag 0x482356 (-s, -v, -? clear it) */
    int cheat;               /* 0x482368 (-@): the DEBUG dialog stays open */
    /* the audio device (es2_shell_audio_sdl_device; headless
     * es2_app_sound_null_device, no output); NULL: silent */
    int (*audio_device)(void *device);
    /* milliseconds (SDL_GetTicks or the headless clock) */
    uint32_t (*clock)(void *context);
    void *clock_context;
    /* The window's client size in pixels (the movie frame's layout); 0 when
     * there is none. */
    int (*client_size)(void *context, int *width, int *height);
    /* Waits up to `milliseconds` or until input arrives (the player's wait
     * for the focus). */
    void (*idle)(void *context, uint32_t milliseconds);
    /* Sleeps `milliseconds` regardless of input (the movie queue's WAIT);
     * NULL: es2_os_sleep. The web build keeps its sound ring filled. */
    void (*sleep)(void *context, uint32_t milliseconds);
    /* MessageBoxA owned by the window, with its type flags. */
    void (*message_box)(void *context, const char *text, const char *title, uint32_t flags);
    void *window_context;    /* of client_size, idle, sleep and message_box */
} es2_app_platform;

/* A presenter over `host` (configured and started by the caller) and the
 * shell `shell` (its platform set). NULL when memory ran out. */
es2_app *es2_app_create(es2_host *host, es2_gui_shell *shell, const es2_app_platform *platform);
void es2_app_destroy(es2_app *app);
/* The resources and every screen in VSHELL's construction order; the
 * toolkit installed into the shell's hooks. 0, or -1 with the reason said
 * through the host. */
int es2_app_build(es2_app *app);
/* The process start after the build: the sounds' start-up (-s clears it),
 * the entry dispatch 0x40188a with the intro movies or the results path,
 * the first screen. */
void es2_app_enter(es2_app *app, int state);
/* The loop end: the exit code (the flow's, or 1 after an assertion). */
int es2_app_finish(es2_app *app);

/* For the headless script: the toolkit and the window of the layout table's
 * control "<screen>.<NN>" (0 when unknown or not built). */
es2_ui *es2_app_ui(es2_app *app);
es2_ui_id es2_app_control(es2_app *app, const char *name);
/* A line describing the flow and the presenter state (headless report). */
void es2_app_describe(es2_app *app, char *out, size_t capacity);

#endif
