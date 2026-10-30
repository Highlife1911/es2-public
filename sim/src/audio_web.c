/* The web backend of the windowed audio (es2/sim/audio_web.h). */
#include <string.h>
#include "es2/sim/audio_web.h"

#define IMPORT(name) __attribute__((import_module("es2sim"), import_name(#name)))
/* The ring's open for rate, channels, bits; 0, or an MMRESULT-like code. */
IMPORT(audio_open) uint32_t web_import_audio_open(uint32_t rate, uint32_t channels, uint32_t bits);
/* One buffer into the ring; 0 = queued. */
IMPORT(audio_write) uint32_t web_import_audio_write(const uint8_t *data, uint32_t bytes);
/* The ring's fill in bytes. */
IMPORT(audio_queued) uint32_t web_import_audio_queued(void);
IMPORT(audio_reset) void web_import_audio_reset(void);
IMPORT(audio_close) void web_import_audio_close(void);
/* mciSendCommandA over the host block (es2_sim_mci_host: 32-bit fields in
 * MCI_*_PARMS order, NULL when the guest passed none); its MCIERROR. */
IMPORT(mci_send) uint32_t web_import_mci_send(uint32_t device, uint32_t message, uint32_t flags, void *params);

_Static_assert(sizeof(uintptr_t) == 4u, "the host block's fields are 32-bit");

static uint32_t web_open(void *context, uint32_t rate, uint32_t channels, uint32_t bits) {
    (void)context;
    return web_import_audio_open(rate, channels, bits);
}
static uint32_t web_write(void *context, const uint8_t *data, uint32_t bytes) {
    (void)context;
    return web_import_audio_write(data, bytes);
}
static uint32_t web_queued(void *context) {
    (void)context;
    return web_import_audio_queued();
}
static void web_reset(void *context) {
    (void)context;
    web_import_audio_reset();
}
static void web_close(void *context) {
    (void)context;
    web_import_audio_close();
}
void es2_sim_audio_web_sink(es2_sim_sos_sink *sink) {
    memset(sink, 0, sizeof(*sink));
    sink->open = web_open;
    sink->write = web_write;
    sink->queued = web_queued;
    sink->reset = web_reset;
    sink->close = web_close;
}

static uint32_t web_send(void *user, uint32_t device, uint32_t message, uint32_t flags, void *params) {
    (void)user;
    return web_import_mci_send(device, message, flags, params);
}

void es2_sim_audio_web_notify(void *a, uint32_t wparam, uint32_t lparam) {
    es2_sim_audio_window_notify(a, wparam, lparam);
}

int es2_sim_audio_window_init(es2_sim_audio_window *a, const es2_vt *vt, const es2_sim_sos_sink *sink,
                              es2_sim_audio_window_send send, void *send_user, int notify) {
    es2_sim_sos_sink own;
    if (!sink) {
        es2_sim_audio_web_sink(&own);
        sink = &own;
    }
    es2_sim_audio_window_core_init(a, vt, sink, send ? send : web_send, send ? send_user : NULL, notify ? (void *)a : NULL);
    return 0;
}
