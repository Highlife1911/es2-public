#ifndef ES2_SHELL_WEB_WEB_IMPORTS_H
#define ES2_SHELL_WEB_WEB_IMPORTS_H

/* The "es2shell" imports of es2shell.wasm (docs/web_build.md sections 3, 9
 * to 12). The worker's side is web/es2shell_imports.mjs over the shared
 * regions of web/shared.mjs; Node tests serve the same module from a
 * worker thread (web/node/shell_host.mjs). Strings are NUL-terminated
 * ANSI. */
#include <stdint.h>

#define ES2_WEB_IMPORT(name) __attribute__((import_module("es2shell"), import_name(#name)))

/* One entry of the input ring (eight little-endian int32): type, four
 * arguments, the event time in milliseconds, two reserved. Coordinates are
 * client pixels of the 640 x 480 window. */
typedef struct es2_web_input {
    int32_t type, a, b, c, d, time, reserved[2];
} es2_web_input;

enum {
    ES2_WEB_INPUT_KEY = 1,      /* a scancode (0x100 | code after E0), b VK, c ES2_WEB_KEY_* flags */
    ES2_WEB_INPUT_MOVE = 2,     /* a x, b y */
    ES2_WEB_INPUT_BUTTON = 3,   /* a x, b y, c button (0 left, 1 right, 2 middle), d 1 down / 0 up */
    ES2_WEB_INPUT_FOCUS = 4,    /* a 1 gained / 0 lost */
    ES2_WEB_INPUT_EXPOSE = 5,
    ES2_WEB_INPUT_CLOSE = 6,    /* the window's close request */
    ES2_WEB_INPUT_QUIT = 7,     /* the end of the session */
    ES2_WEB_INPUT_MCI = 8,      /* MM_MCINOTIFY: a wParam (MCI_NOTIFY_*), b device id */
    ES2_WEB_INPUT_SIZE = 9,     /* a SIZE_* (1 minimized, 0 restored) */
};

enum {
    ES2_WEB_KEY_UP = 1, ES2_WEB_KEY_REPEAT = 2, ES2_WEB_KEY_SHIFT = 4, ES2_WEB_KEY_ALT = 8, ES2_WEB_KEY_CTRL = 16,
    ES2_WEB_KEY_NUMLOCK = 32,
};

/* Milliseconds since the module's start (performance.now). */
ES2_WEB_IMPORT(now) uint32_t es2_web_clock(void);
/* Atomics.wait on the wake word up to `milliseconds` or until the input
 * ring is not empty; 1 when it is not. */
ES2_WEB_IMPORT(wait_input) int32_t es2_web_wait(uint32_t milliseconds);
/* Up to `count` entries from the input ring; the number taken. */
ES2_WEB_IMPORT(take_input) int32_t es2_web_input_take(es2_web_input *out, int32_t count);
/* An RGBA picture into the next free frame slot. */
ES2_WEB_IMPORT(present) void es2_web_present(const uint8_t *rgba, int32_t width, int32_t height);
/* 1 the wait cursor, 0 the arrow. */
ES2_WEB_IMPORT(cursor) void es2_web_cursor(int32_t busy);
ES2_WEB_IMPORT(fullscreen) void es2_web_fullscreen(int32_t on);
/* A modal box; the button id (IDOK 1, ...). */
ES2_WEB_IMPORT(message_box) int32_t es2_web_message_box(const char *text, const char *title, uint32_t flags);
/* Plays the movie of the CD path in a video element at the client
 * rectangle; its end arrives as ES2_WEB_INPUT_MCI with MCI_NOTIFY_SUCCESSFUL.
 * 0 when there is no such movie. */
ES2_WEB_IMPORT(movie_play) int32_t es2_web_movie_play(const char *path, int32_t x, int32_t y, int32_t width,
                                                      int32_t height);
ES2_WEB_IMPORT(movie_stop) void es2_web_movie_stop(void);
/* The online manual: the page opens the project's page in a new tab. */
ES2_WEB_IMPORT(manual) void es2_web_manual(void);
/* Free stereo frames in the shell sound ring, and a write of `frames`
 * interleaved float stereo frames at 22,050 Hz. */
ES2_WEB_IMPORT(sound_space) int32_t es2_web_sound_space(void);
ES2_WEB_IMPORT(sound_write) void es2_web_sound_write(const float *frames, int32_t count);

#endif
