/* The SDL3 sink of the SOS mixing device (audio.md, "The mixing device"). The
 * mixed buffers go to an SDL3 audio stream on the default playback device in
 * the format the caps record asked for (8-bit: SDL_AUDIO_U8, 16-bit:
 * SDL_AUDIO_S16LE; the rate and channel count as given); SDL converts to the
 * device format. Not part of the headless es2sim: a windowed host links
 * :audio_sdl and binds es2_sim_sos_mixer_device with this sink. */
#ifndef ES2_SIM_AUDIO_SDL_H
#define ES2_SIM_AUDIO_SDL_H
#include "es2/sim/audio.h"

typedef struct es2_sim_audio_sdl {
    void *stream;             /* SDL_AudioStream * (NULL: closed) */
    int subsystem;            /* SDL_INIT_AUDIO held by this sink */
    uint32_t opens, writes, write_bytes;
    char error[128];          /* SDL_GetError() of the last failure */
} es2_sim_audio_sdl;

/* Fills `sink` with the SDL3 functions over `s` (zeroed here). */
void es2_sim_audio_sdl_sink(es2_sim_audio_sdl *s, es2_sim_sos_sink *sink);

/* ---- the windowed audio of es2sim_window (audio.md, "sound and CD
 * music in the window"). The device behind the audio seam: the SOS calls go to
 * the SOS mixer (audio.h) over the SDL3 sink (the format the caps record asks for: with
 * the CD's sound.cfg 8-bit unsigned, 22,050 Hz, stereo), the mciSendCommandA
 * calls to winmm through es2_sim_mci_send. MCI_PLAY's notify window is a
 * message-only window of this layer on the simulation thread; its MM_MCINOTIFY
 * (3B9h; wParam the MCI_NOTIFY_* code, lParam the device) is queued and handed
 * to the guest by the loop's peek as a posted message (before the window
 * layer's input), so the window procedure's handler list reaches 0x462398 as in
 * the original. The same peek, the pause sleep and the limiter's pace pump the
 * mixer: the buffers SDL has played come back and are refilled (the waveOut
 * service thread's work, done on the simulation thread). */
#define ES2_SIM_AUDIO_NOTIFY_QUEUE 16u
#define ES2_MM_MCINOTIFY 0x03b9u
typedef struct es2_sim_audio_window {
    es2_sim_audio_sdl sdl;
    es2_sim_sos_sink sink;
    es2_sim_sos_mixer mixer;
    es2_sim_audio_device mixer_device;      /* the mixer's SOS half */
    es2_sim_mci_host mci;
    void *notify_window;                    /* HWND (NULL: none, MCI_NOTIFY's callback passed as the guest's) */
    struct { uint32_t wparam, lparam; } notify[ES2_SIM_AUDIO_NOTIFY_QUEUE];
    unsigned notify_head, notify_count;
    es2_sim_loop *loop;
    const es2_sim_loop_window *previous;    /* loop->window before the attach (restored by the close) */
    es2_sim_loop_window inner;              /* a copy of it, called through */
    es2_sim_loop_window ops;                /* what loop->window points at after the attach */
    int attached;
    uint64_t notifies, delivered, dropped, pumps;
    char error[128];
} es2_sim_audio_window;

/* `sink` NULL: the SDL3 sink (opened only when the driver initialises);
 * `send` NULL: winmm's mciSendCommandA. `notify`: create the notify
 * window (on the calling thread). 0, or 1 with a->error (no notify window:
 * the run goes on with the guest's callback passed as is). */
int es2_sim_audio_window_init(es2_sim_audio_window *a, const es2_vt *vt, const es2_sim_sos_sink *sink,
                              uint32_t (*send)(void *user, uint32_t device, uint32_t message, uint32_t flags, void *params),
                              void *send_user, int notify);
void es2_sim_audio_window_device(es2_sim_audio_window *a, es2_sim_audio_device *device);
/* After es2_sim_window_init: wraps loop->window (peek, sleep, pace pump;
 * the rest passes through). */
void es2_sim_audio_window_attach(es2_sim_audio_window *a, es2_sim_loop *loop);
/* One MM_MCINOTIFY into the queue (the notify window's procedure; tests). */
void es2_sim_audio_window_notify(es2_sim_audio_window *a, uint32_t wparam, uint32_t lparam);
/* The notify window's pending MM_MCINOTIFY messages into the queue. */
void es2_sim_audio_window_collect(es2_sim_audio_window *a);
/* Restores loop->window, closes a sink the guest left open, destroys the
 * notify window. An MCI device the guest left open stays as the original
 * leaves it (winmm's own process-exit handling). */
void es2_sim_audio_window_close(es2_sim_audio_window *a);

#endif
