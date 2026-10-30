#ifndef ES2_SHELL_WEB_WINDOW_WEB_H
#define ES2_SHELL_WEB_WINDOW_WEB_H

/* The shell's window in the web build (docs/web_build.md sections 3, 9 and
 * 12), in place of window.c and present_sdl.c: the input ring's entries
 * are taken into a window queue (as Windows dispatches messages into SDL's
 * event queue) and the pump turns them into VSHELL's window messages as
 * window.c turns SDL's events; a present converts the screen to RGBA
 * (es2_gui_screen_pixels) into a frame slot. The page shows the 640 x 480
 * picture scaled and turns its pointer positions into client pixels, so the
 * client area is the frame. */
#include "es2/shell/gui/loop.h"
#include "web/web_imports.h"

es2_gui_platform es2_web_window_platform(void);
/* Takes every entry of the input ring into the window queue; returns 1
 * when an ES2_WEB_INPUT_QUIT was among them (it is not queued). */
int es2_web_window_take_all(void);
/* The window queue's part of a dispatch: queues `e`, except the MCI
 * notifications, which the window procedure leaves to DefWindowProc. */
void es2_web_window_dispatch(const es2_web_input *e);

#endif
