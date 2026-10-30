/* The windowed audio's shared part (es2/sim/audio_window.h; audio.md, "sound and CD music in the
 * window"): the mixer over the backend's sink, MCI through es2_sim_mci_send, the MM_MCINOTIFY queue
 * and the loop's seam wrapped. */
#include <string.h>
#include "es2/sim/audio_window.h"

/* ---- the sink, with the bytes written counted */
static uint32_t sink_open(void *context, uint32_t rate, uint32_t channels, uint32_t bits) {
    es2_sim_audio_window *a = context;
    return a->target.open(a->target.context, rate, channels, bits);
}
static uint32_t sink_write(void *context, const uint8_t *data, uint32_t bytes) {
    es2_sim_audio_window *a = context;
    uint32_t r = a->target.write(a->target.context, data, bytes);
    if (!r) { ++a->sink_writes; a->sink_bytes += bytes; }
    return r;
}
static uint32_t sink_queued(void *context) {
    es2_sim_audio_window *a = context;
    return a->target.queued(a->target.context);
}
static void sink_reset(void *context) {
    es2_sim_audio_window *a = context;
    a->target.reset(a->target.context);
}
static void sink_close(void *context) {
    es2_sim_audio_window *a = context;
    a->target.close(a->target.context);
}
static void sink_flush(void *context) {
    es2_sim_audio_window *a = context;
    a->target.flush(a->target.context);
}

void es2_sim_audio_window_notify(es2_sim_audio_window *a, uint32_t wparam, uint32_t lparam) {
    ++a->notifies;
    if (a->notify_count == ES2_SIM_AUDIO_NOTIFY_QUEUE) { ++a->dropped; return; }
    unsigned at = (a->notify_head + a->notify_count) % ES2_SIM_AUDIO_NOTIFY_QUEUE;
    a->notify[at].wparam = wparam;
    a->notify[at].lparam = lparam;
    ++a->notify_count;
}
static uint32_t window_sos(void *context, const es2_guest_memory *m, es2_sim_sos_call call, const uint32_t *args, unsigned count) {
    es2_sim_audio_window *a = context;
    return a->mixer_device.sos(a->mixer_device.context, m, call, args, count);
}
static uint32_t window_mci(void *context, const es2_guest_memory *m, uint32_t device, uint32_t message, uint32_t flags, uint32_t params) {
    es2_sim_audio_window *a = context;
    return es2_sim_mci_send(&a->mci, m, device, message, flags, params);
}

void es2_sim_audio_window_core_init(es2_sim_audio_window *a, const es2_vt *vt, const es2_sim_sos_sink *sink,
                                    es2_sim_audio_window_send send, void *send_user, void *notify_window) {
    memset(a, 0, sizeof(*a));
    a->target = *sink;
    a->sink.context = a;
    a->sink.open = sink_open;
    a->sink.write = sink_write;
    a->sink.queued = sink_queued;
    a->sink.reset = sink_reset;
    a->sink.close = sink_close;
    a->sink.flush = sink->flush ? sink_flush : NULL;
    a->notify_window = notify_window;
    es2_sim_sos_mixer_init(&a->mixer, vt, &a->sink);
    es2_sim_sos_mixer_device(&a->mixer, &a->mixer_device);
    es2_sim_mci_host_init(&a->mci, send, send_user, (uintptr_t)notify_window);
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
static void collect(es2_sim_audio_window *a) {
    if (a->collect) a->collect(a);
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
 * while the window layer pumps its events is retrieved once that queue is
 * empty. */
static int wrap_peek(void *user, es2_sim_loop *l, uint32_t *message, uint32_t *wparam, uint32_t *lparam) {
    es2_sim_audio_window *a = user;
    pump(a);
    collect(a);
    if (deliver(a, message, wparam, lparam)) return 1;
    int r = a->inner.peek ? a->inner.peek(a->inner.user, l, message, wparam, lparam) : 0;
    if (r) return r;
    collect(a);
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
    a->ops.pace = a->inner.pace ? wrap_pace : NULL;   /* NULL keeps the poll unpaced */
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
    if (a->notify_window && a->destroy_notify) a->destroy_notify(a);
    a->notify_window = NULL;
    a->mci.notify_window = 0;
}
