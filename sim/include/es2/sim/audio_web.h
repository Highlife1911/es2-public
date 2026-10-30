/* The web backend of the windowed audio (es2/sim/audio_window.h;
 * docs/web_build.md section 10) over the "es2sim" imports of
 * web/es2sim_imports.mjs. The sink writes the mixed buffers into the
 * simulation audio ring of web/shared.mjs (22,050 Hz stereo U8, the format
 * the CD's sound.cfg asks for; any other answers MMSYSERR_BADDEVICEID), and
 * `queued` is the ring's fill in bytes, which the AudioWorklet (or the Node
 * host) plays. MCI goes to the page's cdaudio device (web/cdaudio.mjs), whose
 * MM_MCINOTIFY comes back as an MCI entry of the input ring: the window
 * layer's pump hands it to es2_sim_audio_web_notify
 * (es2_sim_window_web_mci). The notify window is a token, so MCI_NOTIFY's
 * callback is the platform's as natively. */
#ifndef ES2_SIM_AUDIO_WEB_H
#define ES2_SIM_AUDIO_WEB_H
#include "es2/sim/audio.h"
#include "es2/sim/audio_window.h"

/* Fills `sink` with the ring's functions. */
void es2_sim_audio_web_sink(es2_sim_sos_sink *sink);

/* An MCI entry's MM_MCINOTIFY into `a`'s queue (`a` an es2_sim_audio_window). */
void es2_sim_audio_web_notify(void *a, uint32_t wparam, uint32_t lparam);

#endif
