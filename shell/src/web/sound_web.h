#ifndef ES2_SHELL_WEB_SOUND_WEB_H
#define ES2_SHELL_WEB_SOUND_WEB_H

#include <stdint.h>

/* The web build's shell sound device (sound_web.c) behind the presenter's
 * `audio_device` signature (an es2_shell_audio_device *), like
 * gui/sound_sdl.h. */
int es2_web_sound_device(void *device);
void es2_web_sound_shutdown(void);
/* Mixes into the shell sound ring as far as it has room. */
void es2_web_sound_fill(void);
/* es2_web_wait that keeps the shell sound ring filled, in slices short
 * against the ring, as SDL's audio thread plays on during the native
 * shell's waits. 1 when the input ring is not empty. */
int32_t es2_web_sound_wait(uint32_t milliseconds);
/* es2_os_sleep that keeps the ring filled the same way, for the whole
 * time whatever input arrives. */
void es2_web_sound_sleep(uint32_t milliseconds);

#endif
