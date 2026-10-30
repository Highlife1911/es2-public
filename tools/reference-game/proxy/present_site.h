/* The call site of a present 0x464a14 that own code makes
 * (docs/reverse-engineering/presentation.md, section "present sites from
 * own code in the proxy capture").
 *
 * The frame capture (presenter.h, ddraw_windowed.h) records as a present's
 * site the return address of the call of 0x464a14. A replaced hook calls
 * the present through a stack bridge (ai_behaviour_stack_call,
 * logic_frame_machine_call), whose CALL leaves a return address inside the
 * proxy DLL in that slot; the original's return address is written there
 * only after the call returns. So an adapter that calls game code from own
 * code announces each call: the callee, the original entry VA of the
 * replaced caller and the return address the core names for the call
 * (absolute, as passed to the bridge). The capture takes the announcement
 * only when the callee is the present (presenter.h pr_present_site).
 *
 * Main thread only, like the shims' pending contexts; begin/end nest (end
 * restores the announcement that was current before begin). Shared by the
 * proxy's capture and the hook adapters (and so compiled into the in-vitro
 * harness, where nothing reads it). */
#ifndef ES2_PRESENT_SITE_H
#define ES2_PRESENT_SITE_H
#include <stdint.h>

typedef struct es2_present_site_call { uint32_t target, caller, ret; } es2_present_site_call;
/* The guest call in progress from own code: target 0 when none is. */
__attribute__((used)) static es2_present_site_call es2_present_site_current;

/* Before the bridge's CALL: target = the callee's absolute address, caller =
 * the replaced function's original entry VA (reference base 0x400000), ret =
 * the original return address of the call (absolute, 0 when unknown). */
static inline es2_present_site_call es2_present_site_begin(uint32_t target, uint32_t caller, uint32_t ret) {
    es2_present_site_call previous = es2_present_site_current;
    es2_present_site_current.target = target;
    es2_present_site_current.caller = caller;
    es2_present_site_current.ret = ret;
    return previous;
}
/* After the bridge returned. */
static inline void es2_present_site_end(es2_present_site_call previous) { es2_present_site_current = previous; }
#endif
