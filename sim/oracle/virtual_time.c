/* Virtual time of the oracle; see virtual_time.h. The proxy's virtual_time.h
 * uses it; the proxy's fixtures
 * (//tools/reference-game/proxy:virtual_time_*_test) guard it. */
#include "sim/oracle/virtual_time.h"

void es2_vt_enable(es2_vt *vt, int enabled, uint32_t step, uint32_t start, uint32_t stall_limit, int intra) {
    vt->enabled = enabled && step >= ES2_VT_MIN_STEP;
    vt->step = step;
    vt->clock_ms = start;
    vt->step_base = start;
    vt->intra = intra;
    vt->stall_limit = stall_limit ? stall_limit : ES2_VT_DEFAULT_STALL_LIMIT;
}

uint32_t es2_vt_limiter_poll(es2_vt *vt, uint32_t previous, es2_vt_pace pace, void *user) {
    uint32_t target = previous + vt->step;
    /* Never move backwards (only possible after stall advances). */
    if ((int32_t)(target - vt->clock_ms) > 0) vt->clock_ms = target;
    if (pace) pace(user);
    ++vt->limiter_polls;
    vt->since_limiter = 0;
    vt->intra_frame = 0;
    vt->step_base = vt->clock_ms;
    return vt->clock_ms;
}

uint32_t es2_vt_read(es2_vt *vt) {
    ++vt->since_limiter;
    if (vt->intra && vt->since_limiter > 1 && vt->intra_frame + 1 < vt->step) {
        ++vt->clock_ms; ++vt->intra_frame; ++vt->intra_advances;
    } else if (vt->since_limiter > vt->stall_limit) {
        /* Deterministic fallback against a busy wait outside the limiter. */
        ++vt->clock_ms; ++vt->stall_advances;
    }
    return vt->clock_ms;
}

void es2_vt_frame_boundary(es2_vt *vt) {
    if (!vt->enabled) return;
    if (vt->boundary_seen && vt->limiter_polls == vt->boundary_polls) {
        uint32_t target = vt->step_base + vt->step;
        if ((int32_t)(target - vt->clock_ms) > 0) vt->clock_ms = target;
        vt->step_base = vt->clock_ms;
        vt->since_limiter = 0;
        vt->intra_frame = 0;
        ++vt->bypass_steps;
    }
    vt->boundary_seen = 1;
    vt->boundary_polls = vt->limiter_polls;
}
