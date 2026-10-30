#ifndef ES2_SHELL_GUI_SOUND_SDL_H
#define ES2_SHELL_GUI_SOUND_SDL_H

/* es2_shell_audio_sdl_device and its shutdown behind the presenter's
 * `audio_device` signature (an es2_shell_audio_device *), for callers of
 * gui/app.h: es2/shell/sounds.h and gui/text.h both name
 * ES2_SHELL_BUTTON_*. */
int es2_app_sound_sdl_device(void *device);
void es2_app_sound_sdl_shutdown(void);

#endif
