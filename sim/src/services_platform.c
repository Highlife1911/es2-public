/* The KERNEL32 calls of the c0w32 entry and the init/exit records as headless
 * platform services (own_process.md section 6 and "the static initialisation
 * as own code"; rows in
 * sim/dispatch/services.h). Each keeps a record of its calls for the log and
 * the in-vitro comparison. */
#include <string.h>
#include "es2/sim/services.h"

static es2_sim_platform_calls calls;

const es2_sim_platform_calls *es2_sim_platform_calls_get(void) { return &calls; }
void es2_sim_platform_calls_reset(void) { memset(&calls, 0, sizeof(calls)); }

static void refuse(es2_sim_service_call *call, const char *reason) { call->refused = 1; call->reason = reason; }

/* CreateFileA (thunk 0x4968fe, stdcall, 7 arguments). DBSIM's one caller, init
 * record 64 (0x495500), opens "\\.\DARKMONO.VXD" with access 0, share 0,
 * disposition 0 and FILE_FLAG_DELETE_ON_CLOSE. On Windows NT there is no such
 * VxD and disposition 0 is not a valid creation disposition, so the call fails
 * with INVALID_HANDLE_VALUE (in vitro on this host: -1, error 87;
 * own_process.md, "the static initialisation as own code"). Headless the
 * service gives that result for every call and opens nothing. EAX = -1; ECX/EDX
 * left as at the call (the API does not define them; 0x495520.. reads only
 * EAX). */
void es2_sim_service_create_file(es2_sim_service_call *call, void *user) {
    es2_sim_services *s = user;
    ++calls.create_file;
    for (unsigned i = 0; i < 7; ++i) calls.create_file_args[i] = call->slots[i];
    unsigned n = 0;
    uint32_t name = call->slots[0];
    while (name && n + 1 < sizeof(calls.create_file_name)) {
        const uint8_t *p = s->memory->host(s->memory->context, name + n);
        if (!p || !*p) break;
        calls.create_file_name[n++] = (char)*p;
    }
    calls.create_file_name[n] = 0;
    calls.create_file_result = 0xffffffffu;
    call->eax = 0xffffffffu;
}

/* CloseHandle (thunk 0x4968b0, stdcall, 1 argument). Its DBSIM caller in
 * the records is exit record 25 (0x495530), which skips it for -1. The
 * headless platform never hands out a handle, so any call is refused. */
void es2_sim_service_close_handle(es2_sim_service_call *call, void *user) {
    (void)user;
    ++calls.close_handle;
    refuse(call, "CloseHandle: the headless platform opened no handle");
}

/* GetModuleHandleA (thunk 0x496898, stdcall, 1 argument). The c0w32 entry
 * asks for its own module (NULL): the original gets its image base,
 * 0x400000, which the host maps at that address. A name is refused (no
 * other module is modelled). EAX = 0x400000; ECX/EDX left as at the call. */
void es2_sim_service_module_handle(es2_sim_service_call *call, void *user) {
    (void)user;
    ++calls.module_handle;
    if (call->slots[0]) { refuse(call, "GetModuleHandleA with a module name"); return; }
    call->eax = 0x00400000u;
}
