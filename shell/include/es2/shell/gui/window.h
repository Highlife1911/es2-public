#ifndef ES2_SHELL_GUI_WINDOW_H
#define ES2_SHELL_GUI_WINDOW_H

/* The own shell's window on SDL3 (window.c) and its presenter
 * (present_sdl.c). VSHELL's window (vshell_process.md section 6):
 * title "EarthSiege II", a fixed 640 x 480 client that cannot be resized,
 * centred; with preference byte 6 full screen. The SDL events are turned
 * into the window procedure's messages (es2_gui_on_*) in the pump. Platform
 * deviations are listed in vshell_presentation.md, "the own shell's frame,
 * presenter and window". */
#include "es2/shell/gui/loop.h"

#include <stddef.h>

typedef struct es2_gui_window es2_gui_window;

/* Opens the window (full screen = a borderless desktop-size window). NULL on
 * failure with the reason in error. */
es2_gui_window *es2_gui_window_open(int fullscreen, char *error, size_t error_size);
void es2_gui_window_close(es2_gui_window *window);
/* The platform callbacks (pump, present, cursor, full screen) of the window. */
es2_gui_platform es2_gui_window_platform(es2_gui_window *window);

/* Windows virtual-key code for an SDL key event (keycode, scancode, the
 * numlock state); 0 when Windows would send none the shell knows. */
unsigned es2_gui_sdl_vk(unsigned keycode, unsigned scancode, int numlock);
/* The native window handle (HWND on Windows) of the window, for the
 * movie player's MCI child window; NULL when there is none. */
void *es2_gui_window_native(es2_gui_window *window);
/* VSHELL's class icon (0x40687c: LoadIconA(hInstance, "es2icon")) from the
 * own data (es2/shell/vshell_store.h: the group ES2ICON and its images
 * as generated from VSHELL.EXE), the image the group's directory picks for
 * size x size (LookupIconIdFromDirectoryEx, as LoadImage picks it from the
 * module); NULL with the reason in error. The HICON belongs to the caller
 * (DestroyIcon). */
void *es2_gui_store_icon(int size, char *error, size_t error_size);
/* Gives the window VSHELL's icon (es2_gui_store_icon at the system's large
 * and small icon sizes, WM_SETICON). SDL's own window class has no icon, so
 * the icon is set per window (a platform deviation in
 * vshell_presentation.md, "the own shell's frame, presenter and window",
 * item 11). 0 with the reason in
 * error, the window then keeps the default icon. */
int es2_gui_window_icon(es2_gui_window *window, char *error, size_t error_size);

#endif
