/* The SDL3 sink of the SOS mixing device (audio.md, "The mixing device"). The
 * mixed buffers go to an SDL3 audio stream on the default playback device in
 * the format the caps record asked for (8-bit: SDL_AUDIO_U8, 16-bit:
 * SDL_AUDIO_S16LE; the rate and channel count as given); SDL converts to the
 * device format. Not part of the headless es2sim: a windowed host links
 * :audio_sdl and binds es2_sim_sos_mixer_device with this sink. */
#ifndef ES2_SIM_AUDIO_SDL_H
#define ES2_SIM_AUDIO_SDL_H
#include "es2/sim/audio.h"
#include "es2/sim/audio_window.h"

typedef struct es2_sim_audio_sdl {
    void *stream;             /* SDL_AudioStream * (NULL: closed) */
    int subsystem;            /* SDL_INIT_AUDIO held by this sink */
    uint32_t opens, writes, write_bytes;
    char error[128];          /* SDL_GetError() of the last failure */
} es2_sim_audio_sdl;

/* Fills `sink` with the SDL3 functions over `s` (zeroed here). */
void es2_sim_audio_sdl_sink(es2_sim_audio_sdl *s, es2_sim_sos_sink *sink);

/* ---- the windowed audio's SDL3 and winmm backend (es2/sim/audio_window.h).
 * es2_sim_audio_window_init's defaults: the SDL3 sink above and winmm's
 * mciSendCommandA; the notify window is a message-only window of this layer
 * on the simulation thread, whose MM_MCINOTIFY reaches the queue through SDL's
 * event pump (DispatchMessage) or the collect below. */
/* The notify window's pending MM_MCINOTIFY messages into the queue. */
void es2_sim_audio_window_collect(es2_sim_audio_window *a);

#endif
