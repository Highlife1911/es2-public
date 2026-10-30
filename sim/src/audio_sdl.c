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
 * The windowed audio's SDL and winmm backend (es2/sim/audio_sdl.h). */

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
static void destroy_notify(es2_sim_audio_window *a) { DestroyWindow((HWND)a->notify_window); }

/* The SDL sink when the caller gives none (one windowed audio per process). */
static es2_sim_audio_sdl default_sdl;

int es2_sim_audio_window_init(es2_sim_audio_window *a, const es2_vt *vt, const es2_sim_sos_sink *sink,
                              es2_sim_audio_window_send send, void *send_user, int notify) {
    es2_sim_sos_sink own;
    if (!sink) {
        es2_sim_audio_sdl_sink(&default_sdl, &own);
        sink = &own;
    }
    int bad = 0;
    HWND w = NULL;
    char error[128] = "";
    if (notify) {
        static const char class_name[] = "es2sim.mci_notify";
        WNDCLASSA wc;
        memset(&wc, 0, sizeof(wc));
        wc.lpfnWndProc = notify_proc;
        wc.hInstance = GetModuleHandleA(NULL);
        wc.lpszClassName = class_name;
        if (RegisterClassA(&wc) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS)
            w = CreateWindowExA(0, class_name, "", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, wc.hInstance, NULL);
        if (!w) {
            snprintf(error, sizeof(error), "the MCI notify window could not be created (Win32 %lu)", (unsigned long)GetLastError());
            bad = 1;
        }
    }
    es2_sim_audio_window_core_init(a, vt, sink, send ? send : winmm_send, send ? send_user : NULL, w);
    if (w) SetWindowLongPtrA(w, GWLP_USERDATA, (LONG_PTR)a);
    memcpy(a->error, error, sizeof(error));
    a->collect = es2_sim_audio_window_collect;
    a->destroy_notify = destroy_notify;
    return bad;
}
