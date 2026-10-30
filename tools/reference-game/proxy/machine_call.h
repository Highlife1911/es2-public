#ifndef ES2_ADAPTER_MACHINE_CALL_H
#define ES2_ADAPTER_MACHINE_CALL_H
/* The adapters' plain calls of guest code (docs/web_build.md section 4.2):
 * a call of `va` with `count` argument slots through es2_machine_call, on the
 * current stack, with EAX, EDX and ECX as given and EBX, EBP, ESI and EDI 0
 * (no callee reads them as inputs). The x86 backend runs the original code
 * (the proxy, the in-vitro harness), the sim backend the dispatch entry of
 * `va` (es2sim). Out: EAX, EDX and ECX as the callee leaves them.
 *
 * The ABI is the callee's: cdecl, except the stdcall entries of DBSIM (the
 * stdcall rows of the sim dispatch, by their row names: the Win32 import
 * thunks and the window and mouse callbacks; the registry's one stdcall
 * hook, key_message 477bec, is no bridge callee). Both push the same slots,
 * and the x86 backend restores ESP from its frame, so the choice matters only
 * to the sim backend, which refuses a cleanup other than the entry's. VAs at
 * the image base 0x400000 (the proxy, es2sim; the in-vitro harness maps the
 * image elsewhere and runs the x86 backend). */
#include <stdint.h>
#include "es2/hook/machine.h"
#include "entry_shim.h"

static inline es2_machine_abi adapter_callee_abi(uint32_t va) {
    static const uint32_t stdcall_vas[] = {
        0x00462398u, /* sound_window */
        0x004809f8u, /* mouse_handler */
        0x00496898u, /* module_handle */
        0x004968b0u, /* close_handle */
        0x004968ecu, /* get_tick_count */
        0x004968feu, /* create_file */
        0x00496910u, /* profile_string */
        0x0049694cu, /* window_rect */
        0x0049695eu, /* get_key_state */
        0x0049696au, /* cursor_pos */
        0x00496970u, /* client_rect */
        0x0049699au, /* client_to_screen */
        0x004969e2u, /* set_cursor_pos */
        0x00496a66u, /* joy_caps */
        0x00496a6cu, /* joy_pos */
    };
    for (unsigned i = 0; i < sizeof stdcall_vas / sizeof stdcall_vas[0]; ++i)
        if (stdcall_vas[i] == va) return ES2_MACHINE_STDCALL;
    return ES2_MACHINE_CDECL;
}

static inline void adapter_machine_call(uint32_t va, const uint32_t *args, unsigned count, uint32_t registers[3]) {
    es2_machine m = {registers[0], registers[2], registers[1], 0, 0, 0, 0, 0};
    (void)es2_machine_call(&m, va, adapter_callee_abi(va), args, count);
    registers[0] = m.eax; registers[1] = m.edx; registers[2] = m.ecx;
}

/* The same call; the result EDX:EAX. */
static inline uint64_t adapter_machine_pair(uint32_t va, const uint32_t *args, unsigned count, uint32_t eax, uint32_t edx,
                                            uint32_t ecx) {
    uint32_t r[3] = {eax, edx, ecx};
    adapter_machine_call(va, args, count, r);
    return (uint64_t)r[1] << 32 | r[0];
}
#endif
