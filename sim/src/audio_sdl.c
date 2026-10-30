/* The SDL3 sink of the SOS mixing device (es2/sim/audio_sdl.h). waveOutOpen
 * becomes an audio stream on the default playback device, waveOutWrite a put,
 * the played count the stream's queued bytes, waveOutReset a clear and
 * waveOutClose the stream's destruction. A forced deviation (audio.md, "audit
 * of the audio rest"): SDL's device buffer adds its own latency behind the
 * queued bytes, so a buffer counts as played when SDL has taken it from the
 * stream, not when the speaker has. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "es2/sim/audio_sdl.h"

static void sdl_error(es2_sim_audio_sdl *s) {
    snprintf(s->error, sizeof(s->error), "%s", SDL_GetError());
}
static uint32_t sdl_open(void *context, uint32_t rate, uint32_t channels, uint32_t bits) {
    es2_sim_audio_sdl *s = context;
    if (s->stream) return 4u;   /* MMSYSERR_ALLOCATED: already open */
    if (!s->subsystem) {
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) { sdl_error(s); return 2u; }   /* MMSYSERR_BADDEVICEID */
        s->subsystem = 1;
    }
    SDL_AudioSpec spec;
    spec.format = bits == 16u ? SDL_AUDIO_S16LE : SDL_AUDIO_U8;
    spec.channels = (int)channels;
    spec.freq = (int)rate;
    SDL_AudioStream *st = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    if (!st) { sdl_error(s); return 2u; }
    if (!SDL_ResumeAudioStreamDevice(st)) { sdl_error(s); SDL_DestroyAudioStream(st); return 1u; }
    s->stream = st;
    ++s->opens;
    return 0;
}
static uint32_t sdl_write(void *context, const uint8_t *data, uint32_t bytes) {
    es2_sim_audio_sdl *s = context;
    if (!s->stream) return 5u;   /* MMSYSERR_INVALHANDLE */
    if (bytes > 0x7fffffffu || !SDL_PutAudioStreamData(s->stream, data, (int)bytes)) { sdl_error(s); return 1u; }
    ++s->writes;
    s->write_bytes += bytes;
    return 0;
}
static uint32_t sdl_queued(void *context) {
    es2_sim_audio_sdl *s = context;
    if (!s->stream) return 0;
    int q = SDL_GetAudioStreamQueued(s->stream);
    return q > 0 ? (uint32_t)q : 0u;
}
static void sdl_reset(void *context) {
    es2_sim_audio_sdl *s = context;
    if (s->stream) SDL_ClearAudioStream(s->stream);
}
/* The ring runs dry (audio.md, "the ring runs dry as waveOut's does"): a stream whose
 * device rate differs from the input rate resamples, and until it is flushed it
 * keeps the last resampler-padding frames of its input queued (7 frames, 14
 * bytes of 8-bit stereo, in SDL 3.4.18 on this target): the buffer they belong
 * to would never count as played. The flush makes them playable (SDL pads the
 * resampler with silence); input put afterwards plays on. */
static void sdl_flush(void *context) {
    es2_sim_audio_sdl *s = context;
    if (s->stream && !SDL_FlushAudioStream(s->stream)) sdl_error(s);
}
static void sdl_close(void *context) {
    es2_sim_audio_sdl *s = context;
    if (s->stream) SDL_DestroyAudioStream(s->stream);
    s->stream = NULL;
    if (s->subsystem) SDL_QuitSubSystem(SDL_INIT_AUDIO);
    s->subsystem = 0;
}
void es2_sim_audio_sdl_sink(es2_sim_audio_sdl *s, es2_sim_sos_sink *sink) {
    memset(s, 0, sizeof(*s));
    sink->context = s;
    sink->open = sdl_open;
    sink->write = sdl_write;
    sink->queued = sdl_queued;
    sink->reset = sdl_reset;
    sink->close = sdl_close;
    sink->flush = sdl_flush;
}

/* ======================================================================
 * The windowed audio (es2/sim/audio_sdl.h, audio.md, "sound and CD music in the
 * window"). */

/* The host blocks of es2_sim_mci_send have the field order and widths of
 * winmm's structures on this target. */
_Static_assert(sizeof(MCI_OPEN_PARMSA) == 5u * sizeof(uintptr_t) && offsetof(MCI_OPEN_PARMSA, wDeviceID) == sizeof(uintptr_t) &&
                   offsetof(MCI_OPEN_PARMSA, lpstrDeviceType) == 2u * sizeof(uintptr_t) &&
                   offsetof(MCI_OPEN_PARMSA, lpstrAlias) == 4u * sizeof(uintptr_t),
               "MCI_OPEN_PARMSA");
_Static_assert(sizeof(MCI_PLAY_PARMS) == 3u * sizeof(uintptr_t) && offsetof(MCI_PLAY_PARMS, dwTo) == 2u * sizeof(uintptr_t),
               "MCI_PLAY_PARMS");
_Static_assert(sizeof(MCI_SET_PARMS) == 3u * sizeof(uintptr_t) && offsetof(MCI_SET_PARMS, dwTimeFormat) == sizeof(uintptr_t),
               "MCI_SET_PARMS");
_Static_assert(sizeof(MCI_STATUS_PARMS) == 4u * sizeof(uintptr_t) && offsetof(MCI_STATUS_PARMS, dwReturn) == sizeof(uintptr_t) &&
                   offsetof(MCI_STATUS_PARMS, dwTrack) == 3u * sizeof(uintptr_t),
               "MCI_STATUS_PARMS");
_Static_assert(sizeof(MCI_GENERIC_PARMS) == sizeof(uintptr_t), "MCI_GENERIC_PARMS");
_Static_assert(ES2_MM_MCINOTIFY == MM_MCINOTIFY, "MM_MCINOTIFY");
_Static_assert(sizeof(uintptr_t) == 4u, "the guest's blocks are 32-bit");

static uint32_t winmm_send(void *user, uint32_t device, uint32_t message, uint32_t flags, void *params) {
    (void)user;
    return (uint32_t)mciSendCommandA((MCIDEVICEID)device, (UINT)message, (DWORD_PTR)flags, (DWORD_PTR)params);
}

void es2_sim_audio_window_notify(es2_sim_audio_window *a, uint32_t wparam, uint32_t lparam) {
    ++a->notifies;
    if (a->notify_count == ES2_SIM_AUDIO_NOTIFY_QUEUE) { ++a->dropped; return; }
    unsigned at = (a->notify_head + a->notify_count) % ES2_SIM_AUDIO_NOTIFY_QUEUE;
    a->notify[at].wparam = wparam;
    a->notify[at].lparam = lparam;
    ++a->notify_count;
}
/* The notify window's procedure: SDL's event pump (PeekMessage over the
 * thread's queue, then DispatchMessage) or the collect below brings it here. */
static LRESULT CALLBACK notify_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == MM_MCINOTIFY) {
        es2_sim_audio_window *a = (es2_sim_audio_window *)GetWindowLongPtrA(window, GWLP_USERDATA);
        if (a) es2_sim_audio_window_notify(a, (uint32_t)wparam, (uint32_t)lparam);
        return 0;
    }
    return DefWindowProcA(window, message, wparam, lparam);
}
/* Platform deviation: a PeekMessage with a window or message
 * filter, called every frame, lets Windows flag the main window as not
 * responding about every 6 s (IsHungAppWindow; reproduced in a minimal SDL3
 * program). DWM then puts a ghost window in front, which takes focus, pauses
 * the game and, after a long focus loss, leaves it hung. The filtered peek
 * therefore runs only while a posted message is waiting; the notifications the
 * game sees are the same. */
void es2_sim_audio_window_collect(es2_sim_audio_window *a) {
    if (!a->notify_window) return;
    if (!(HIWORD(GetQueueStatus(QS_POSTMESSAGE)) & QS_POSTMESSAGE)) return;
    MSG msg;
    while (PeekMessageA(&msg, (HWND)a->notify_window, MM_MCINOTIFY, MM_MCINOTIFY, PM_REMOVE))
        es2_sim_audio_window_notify(a, (uint32_t)msg.wParam, (uint32_t)msg.lParam);
}
static uint32_t window_sos(void *context, const es2_guest_memory *m, es2_sim_sos_call call, const uint32_t *args, unsigned count) {
    es2_sim_audio_window *a = context;
    return a->mixer_device.sos(a->mixer_device.context, m, call, args, count);
}
static uint32_t window_mci(void *context, const es2_guest_memory *m, uint32_t device, uint32_t message, uint32_t flags, uint32_t params) {
    es2_sim_audio_window *a = context;
    return es2_sim_mci_send(&a->mci, m, device, message, flags, params);
}

int es2_sim_audio_window_init(es2_sim_audio_window *a, const es2_vt *vt, const es2_sim_sos_sink *sink,
                              uint32_t (*send)(void *user, uint32_t device, uint32_t message, uint32_t flags, void *params),
                              void *send_user, int notify) {
    memset(a, 0, sizeof(*a));
    if (sink) a->sink = *sink;
    else es2_sim_audio_sdl_sink(&a->sdl, &a->sink);
    es2_sim_sos_mixer_init(&a->mixer, vt, &a->sink);
    es2_sim_sos_mixer_device(&a->mixer, &a->mixer_device);
    int bad = 0;
    if (notify) {
        static const char class_name[] = "es2sim.mci_notify";
        WNDCLASSA wc;
        memset(&wc, 0, sizeof(wc));
        wc.lpfnWndProc = notify_proc;
        wc.hInstance = GetModuleHandleA(NULL);
        wc.lpszClassName = class_name;
        HWND w = NULL;
        if (RegisterClassA(&wc) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS)
            w = CreateWindowExA(0, class_name, "", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, wc.hInstance, NULL);
        if (w) {
            SetWindowLongPtrA(w, GWLP_USERDATA, (LONG_PTR)a);
            a->notify_window = w;
        } else {
            snprintf(a->error, sizeof(a->error), "the MCI notify window could not be created (Win32 %lu)", (unsigned long)GetLastError());
            bad = 1;
        }
    }
    es2_sim_mci_host_init(&a->mci, send ? send : winmm_send, send ? send_user : NULL, (uintptr_t)a->notify_window);
    return bad;
}
void es2_sim_audio_window_device(es2_sim_audio_window *a, es2_sim_audio_device *device) {
    device->context = a;
    device->sos = window_sos;
    device->mci = window_mci;
}

/* ---- the loop's seam, wrapped */
static void pump(es2_sim_audio_window *a) {
    if (!a->loop || !a->loop->memory) return;
    ++a->pumps;
    es2_sim_sos_pump(&a->mixer, a->loop->memory);
}
static int deliver(es2_sim_audio_window *a, uint32_t *message, uint32_t *wparam, uint32_t *lparam) {
    if (!a->notify_count) return 0;
    *message = ES2_MM_MCINOTIFY;
    *wparam = a->notify[a->notify_head].wparam;
    *lparam = a->notify[a->notify_head].lparam;
    a->notify_head = (a->notify_head + 1u) % ES2_SIM_AUDIO_NOTIFY_QUEUE;
    --a->notify_count;
    ++a->delivered;
    return 1;
}
/* A posted MM_MCINOTIFY is retrieved before the input of the window layer's
 * queue (PeekMessage: posted messages before input); one that arrives
 * while the window layer pumps SDL is retrieved once that queue is empty. */
static int wrap_peek(void *user, es2_sim_loop *l, uint32_t *message, uint32_t *wparam, uint32_t *lparam) {
    es2_sim_audio_window *a = user;
    pump(a);
    es2_sim_audio_window_collect(a);
    if (deliver(a, message, wparam, lparam)) return 1;
    int r = a->inner.peek ? a->inner.peek(a->inner.user, l, message, wparam, lparam) : 0;
    if (r) return r;
    es2_sim_audio_window_collect(a);
    return deliver(a, message, wparam, lparam);
}
static void wrap_set_cursor(void *user, uint32_t handle) {
    es2_sim_audio_window *a = user;
    if (a->inner.set_cursor) a->inner.set_cursor(a->inner.user, handle);
}
static void wrap_sleep(void *user, uint32_t ms) {
    es2_sim_audio_window *a = user;
    if (a->inner.sleep) a->inner.sleep(a->inner.user, ms);
    pump(a);
}
static void wrap_clock(void *user, es2_vt *vt) {
    es2_sim_audio_window *a = user;
    if (a->inner.clock) a->inner.clock(a->inner.user, vt);
}
static void wrap_pace(void *user) {
    es2_sim_audio_window *a = user;
    pump(a);
    if (a->inner.pace) a->inner.pace(a->inner.user);
    pump(a);
}
static int wrap_def_window_proc(void *user, es2_sim_loop *l, uint32_t message, uint32_t wparam, uint32_t lparam, uint32_t *result) {
    es2_sim_audio_window *a = user;
    return a->inner.def_window_proc(a->inner.user, l, message, wparam, lparam, result);
}
void es2_sim_audio_window_attach(es2_sim_audio_window *a, es2_sim_loop *loop) {
    a->loop = loop;
    a->previous = loop->window;
    memset(&a->inner, 0, sizeof(a->inner));
    if (loop->window) a->inner = *loop->window;
    memset(&a->ops, 0, sizeof(a->ops));
    a->ops.peek = wrap_peek;
    a->ops.set_cursor = wrap_set_cursor;
    a->ops.sleep = wrap_sleep;
    a->ops.clock = wrap_clock;
    a->ops.pace = a->inner.pace ? wrap_pace : NULL;   /* NULL keeps the poll unpaced, as before */
    /* NULL keeps the loop's headless DefWindowProcA. */
    a->ops.def_window_proc = a->inner.def_window_proc ? wrap_def_window_proc : NULL;
    a->ops.user = a;
    a->attached = 1;
    loop->window = &a->ops;
}
void es2_sim_audio_window_close(es2_sim_audio_window *a) {
    if (a->attached && a->loop && a->loop->window == &a->ops) a->loop->window = a->previous;
    a->attached = 0;
    if (a->mixer.opened && a->sink.close) a->sink.close(a->sink.context);
    a->mixer.opened = 0;
    if (a->notify_window) DestroyWindow((HWND)a->notify_window);
    a->notify_window = NULL;
    a->mci.notify_window = 0;
}
