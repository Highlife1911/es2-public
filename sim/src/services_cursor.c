/* ClientToScreen and SetCursorPos as service rows (es2/sim/cursor.h,
 * sim/dispatch/services/cursor_warp.h; own_process.md, "the window's open points").
 * DBSIM calls them from the cursor warp 0x4808dc (ClientToScreen at 0x480931,
 * SetCursorPos at 0x48093e) and SetCursorPos also from the full-screen toggle
 * (0x466923, the DirectDraw branch, which no run takes). Neither caller reads
 * EAX. */
#include <stddef.h>
#include "es2/sim/cursor.h"
#include "es2/sim/services.h"
#include "es2/sim/memory.h"

static const es2_sim_cursor_platform *bound;
static es2_sim_cursor_calls calls;

void es2_sim_cursor_bind(const es2_sim_cursor_platform *platform) { bound = platform; }
const es2_sim_cursor_calls *es2_sim_cursor_calls_get(void) { return &calls; }
void es2_sim_cursor_calls_reset(void) { calls = (es2_sim_cursor_calls){0, 0, 0, 0}; }

/* ClientToScreen (thunk 0x49699a, stdcall)(hwnd, point): reads the POINT,
 * converts it, writes it back; EAX the BOOL. A NULL point is refused (DBSIM
 * always passes its local). */
void es2_sim_service_client_to_screen(es2_sim_service_call *call, void *user);
void es2_sim_service_client_to_screen(es2_sim_service_call *call, void *user) {
    es2_sim_services *s = user;
    const es2_guest_memory *m = s->memory;
    uint32_t p = call->slots[1];
    ++calls.client_to_screen;
    if (!p) { call->refused = 1; call->reason = "ClientToScreen with a NULL point"; return; }
    int32_t x = (int32_t)m->read32(m->context, p), y = (int32_t)m->read32(m->context, p + 4u);
    uint32_t ok = 1;
    if (bound && bound->client_to_screen) ok = bound->client_to_screen(bound->user, call->slots[0], &x, &y);
    m->write32(m->context, p, (uint32_t)x);
    m->write32(m->context, p + 4u, (uint32_t)y);
    call->eax = ok;
}

/* SetCursorPos (thunk 0x4969e2, stdcall)(x, y): EAX the BOOL. */
void es2_sim_service_set_cursor_pos(es2_sim_service_call *call, void *user);
void es2_sim_service_set_cursor_pos(es2_sim_service_call *call, void *user) {
    (void)user;
    ++calls.set_pos;
    calls.last_x = (int32_t)call->slots[0];
    calls.last_y = (int32_t)call->slots[1];
    call->eax = bound && bound->set_pos ? bound->set_pos(bound->user, calls.last_x, calls.last_y) : 1u;
}
