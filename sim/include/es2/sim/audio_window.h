/* The windowed audio of es2sim_window (audio.md, "sound and CD music in the
 * window"), the part both backends share. The device behind the audio seam:
 * the SOS calls go to the SOS mixer (audio.h) over the backend's sink (the
 * format the caps record asks for: with the CD's sound.cfg 8-bit unsigned,
 * 22,050 Hz, stereo), the mciSendCommandA calls to the backend's MCI through
 * es2_sim_mci_send. MCI_PLAY's notify window is the backend's; its
 * MM_MCINOTIFY (3B9h; wParam the MCI_NOTIFY_* code, lParam the device) is
 * queued and handed to the guest by the loop's peek as a posted message
 * (before the window layer's input), so the window procedure's handler list
 * reaches 0x462398 as in the original. The same peek, the pause sleep and the
 * limiter's pace pump the mixer: the buffers the sink has played come back and
 * are refilled (the waveOut service thread's work, done on the simulation
 * thread). The backends: SDL3 and winmm in es2/sim/audio_sdl.h, the web
 * platform's audio ring and cdaudio in es2/sim/audio_web.h. */
#ifndef ES2_SIM_AUDIO_WINDOW_H
#define ES2_SIM_AUDIO_WINDOW_H
#include "es2/sim/audio.h"

#define ES2_SIM_AUDIO_NOTIFY_QUEUE 16u
#define ES2_MM_MCINOTIFY 0x03b9u

typedef uint32_t (*es2_sim_audio_window_send)(void *user, uint32_t device, uint32_t message, uint32_t flags, void *params);

typedef struct es2_sim_audio_window es2_sim_audio_window;
struct es2_sim_audio_window {
    es2_sim_sos_sink sink;                  /* what the mixer writes to: `target` with the byte count */
    es2_sim_sos_sink target;                /* the backend's sink, or the caller's */
    uint64_t sink_writes, sink_bytes;
    es2_sim_sos_mixer mixer;
    es2_sim_audio_device mixer_device;      /* the mixer's SOS half */
    es2_sim_mci_host mci;
    void *notify_window;                    /* the backend's notify window (NULL: none, MCI_NOTIFY's callback passed as the guest's) */
    /* The backend's: the notify window's pending messages into the queue, and
     * its destruction. */
    void (*collect)(es2_sim_audio_window *a);
    void (*destroy_notify)(es2_sim_audio_window *a);
    struct { uint32_t wparam, lparam; } notify[ES2_SIM_AUDIO_NOTIFY_QUEUE];
    unsigned notify_head, notify_count;
    es2_sim_loop *loop;
    const es2_sim_loop_window *previous;    /* loop->window before the attach (restored by the close) */
    es2_sim_loop_window inner;              /* a copy of it, called through */
    es2_sim_loop_window ops;                /* what loop->window points at after the attach */
    int attached;
    uint64_t notifies, delivered, dropped, pumps;
    char error[128];
};

/* Zeroes the record; the mixer over `sink`, MCI through `send` (NULL: every
 * command answers 306) with the notify window `notify_window`. The backends'
 * es2_sim_audio_window_init call this. */
void es2_sim_audio_window_core_init(es2_sim_audio_window *a, const es2_vt *vt, const es2_sim_sos_sink *sink,
                                    es2_sim_audio_window_send send, void *send_user, void *notify_window);

/* ---- per backend (audio_sdl.c or audio_web.c). `sink` NULL: the backend's
 * sink (opened only when the driver initialises); `send` NULL: the backend's
 * MCI. `notify`: create the notify window (on the calling thread). 0, or 1
 * with a->error (no notify window: the run goes on with the guest's callback
 * passed as is). */
int es2_sim_audio_window_init(es2_sim_audio_window *a, const es2_vt *vt, const es2_sim_sos_sink *sink,
                              es2_sim_audio_window_send send, void *send_user, int notify);

void es2_sim_audio_window_device(es2_sim_audio_window *a, es2_sim_audio_device *device);
/* After es2_sim_window_init: wraps loop->window (peek, sleep, pace pump;
 * the rest passes through). */
void es2_sim_audio_window_attach(es2_sim_audio_window *a, es2_sim_loop *loop);
/* One MM_MCINOTIFY into the queue (the notify window's procedure; tests). */
void es2_sim_audio_window_notify(es2_sim_audio_window *a, uint32_t wparam, uint32_t lparam);
/* Restores loop->window, closes a sink the guest left open, destroys the
 * notify window. An MCI device the guest left open stays as the original
 * leaves it (winmm's own process-exit handling). */
void es2_sim_audio_window_close(es2_sim_audio_window *a);

#endif
