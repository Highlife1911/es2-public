/* The trace of DBSIM's frame records after a fault: every call of
 * CW3220.DLL's __ExceptionHandler through DBSIM's import, with the
 * exception flags (search or unwind) and the record's context word before
 * and after, and the calls of kernel32's UnhandledExceptionFilter from
 * CW3220.DLL. Contract and use: docs/reference-game/own_process.md, "How
 * the original ends on an unhandled fault".
 *
 * Question it answers. Every DBSIM record names the thunk 0x4967f0
 * (`jmp [0x6cc33c]`, the import of __ExceptionHandler); the handler runs the
 * frame's clean-up 0x40415d only when the flags have EXCEPTION_UNWINDING or
 * EXCEPTION_EXIT_UNWIND (CW3220 0x40432f `test byte [rec+4], 6`). 0x40415d
 * walks the context word at record +10h down to 0, so a word that was
 * nonzero before an unwinding call and is 0 after it shows that the
 * clean-up ran. Whether the OS unwinds DBSIM's frames before the process
 * ends is seen in the records below; a missing summary shows that no
 * DLL_PROCESS_DETACH ran (the process was terminated, not exited).
 *
 * Off unless the environment has ES2_FAULT_TRACE=<path>: the NDJSON file
 * (appended) that receives the records; every `{suite}` is replaced as in
 * ES2_NEW_FAIL_LOG (new_fail_switch.h). DBSIM.EXE only. Installed by the
 * proxy once its own arming is complete (initialize_forwarding, after the
 * runtime's import check at DBSIM's entry, which would otherwise see the
 * changed slot).
 *
 * What it changes. Only DBSIM's import slot of __ExceptionHandler (RVA
 * 2CC33Ch), and only when the image is DBSIM's, the thunk at 0x4967f0 is
 * `ff 25` with that slot, the record set-up 0x49631c stores the thunk
 * (`c7 43 04 f0 67 49 00`) and the slot holds CW3220.DLL's export. The new
 * target ft_handler records the call, calls the real handler with the same
 * four arguments (cdecl) and records its answer. Without the new-fail
 * switch it also arms new_fail_switch.h's exit-path probe (the import slot
 * of UnhandledExceptionFilter in CW3220.DLL) into this file; with the
 * switch that probe is armed at the injection and writes to its own log.
 *
 * Records (one JSON object per line, CRLF):
 *   fault_trace_installed   image, slot, real handler, whether the handler
 *                           starts with CW3220's reference bytes, the error
 *                           mode, the exit probe's state
 *   fault_trace_refused     state (bad_image, site_mismatch, slot_mismatch,
 *                           write_refused, no_handler)
 *   fault_trace_handler     per call (the first 64 with an error code
 *                           C0000000h.. or an unwinding flag, the first 16
 *                           others): call, code, flags, address, frame,
 *                           table [frame+8], context word [frame+10h], the
 *                           word [frame+12h], counter [frame+1Ch], whether
 *                           the frame is on the thread's stack
 *   fault_trace_handler_result  call, result, context word after (absent
 *                           when the handler does not return: a catch)
 *   new_fail_exit_probe, new_fail_exit_filter, new_fail_exit_filter_result
 *                           the exit-path probe (new_fail_switch.h)
 *   fault_trace_summary     counters at process detach (absent when the
 *                           process is terminated)
 */
#ifndef ES2_FAULT_TRACE_H
#define ES2_FAULT_TRACE_H
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "hook/hook.h"
#include "new_fail_switch.h"

#define FT_THUNK_RVA 0x000967f0u   /* jmp dword ptr [0x6cc33c] */
#define FT_SLOT_RVA 0x002cc33cu    /* the import of CW3220's __ExceptionHandler */
#define FT_SETUP_RVA 0x0009631cu   /* mov dword ptr [ebx+4], 0x4967f0 */
#define FT_SEVERE_MAX 64u
#define FT_OTHER_MAX 16u

enum { FT_OFF, FT_INSTALLED, FT_REFUSED_IMAGE, FT_REFUSED_SITE, FT_REFUSED_SLOT, FT_REFUSED_WRITE, FT_REFUSED_HANDLER };

typedef int (__cdecl *ft_handler_fn)(EXCEPTION_RECORD *record, void *frame, CONTEXT *context, void *dispatcher);

static volatile LONG ft_state;
static uint32_t *ft_slot;
static ft_handler_fn ft_real;
static WCHAR ft_log_path[MAX_PATH];
static volatile LONG ft_calls, ft_search, ft_unwinding, ft_returns, ft_severe_logged, ft_other_logged, ft_cleared;

static void ft_event(const char *event, const char *fields) {
    if (!ft_log_path[0]) return;
    char line[1024];
    int n = snprintf(line, sizeof(line), "{\"event\":\"%s\",\"pid\":%lu%s%s}\r\n", event, (unsigned long)GetCurrentProcessId(),
                     fields && *fields ? "," : "", fields ? fields : "");
    if (n <= 0 || n >= (int)sizeof(line)) return;
    HANDLE file = CreateFileW(ft_log_path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD written;
    WriteFile(file, line, (DWORD)n, &written, NULL);
    CloseHandle(file);
}

static const char *ft_state_text(LONG state) {
    switch (state) {
    case FT_INSTALLED: return "installed";
    case FT_REFUSED_IMAGE: return "bad_image";
    case FT_REFUSED_SITE: return "site_mismatch";
    case FT_REFUSED_SLOT: return "slot_mismatch";
    case FT_REFUSED_WRITE: return "write_refused";
    case FT_REFUSED_HANDLER: return "no_handler";
    default: return "off";
    }
}

/* The context word at +10h of a readable frame record, else FFFFFFFFh. */
static uint32_t ft_context_word(const void *frame) {
    return nf_readable((uintptr_t)frame, 0x20) ? nf_u16((const BYTE *)frame + 0x10) : 0xffffffffu;
}

/* The slot's new target: same arguments, same answer (cdecl, as
 * __ExceptionHandler). */
__attribute__((used, noinline)) static int __cdecl ft_handler(EXCEPTION_RECORD *record, void *frame, CONTEXT *context, void *dispatcher) {
    LONG call = InterlockedIncrement(&ft_calls);
    uint32_t code = 0, flags = 0, address = 0;
    if (nf_readable((uintptr_t)record, 16)) {
        code = record->ExceptionCode;
        flags = record->ExceptionFlags;
        address = (uint32_t)(uintptr_t)record->ExceptionAddress;
    }
    const BOOL unwinding = (flags & 6u) != 0;
    InterlockedIncrement(unwinding ? &ft_unwinding : &ft_search);
    uint32_t words[8] = {0};
    const BOOL frame_ok = nf_readable((uintptr_t)frame, sizeof(words));
    if (frame_ok) memcpy(words, frame, sizeof(words));
    const uint32_t before = frame_ok ? (words[4] & 0xffffu) : 0xffffffffu;
    BOOL logged;
    if (unwinding || (code & 0xc0000000u) == 0xc0000000u) logged = InterlockedIncrement(&ft_severe_logged) <= (LONG)FT_SEVERE_MAX;
    else logged = InterlockedIncrement(&ft_other_logged) <= (LONG)FT_OTHER_MAX;
    if (logged) {
        char fields[512];
        snprintf(fields, sizeof(fields),
                 "\"call\":%ld,\"code\":\"0x%08lx\",\"flags\":\"0x%lx\",\"unwinding\":%s,\"address\":\"0x%lx\",\"frame\":\"0x%lx\","
                 "\"frame_readable\":%s,\"table\":\"0x%lx\",\"context_word\":\"0x%lx\",\"word_12\":\"0x%lx\",\"counter\":\"0x%lx\","
                 "\"frame_on_thread_stack\":%s",
                 (long)call, (unsigned long)code, (unsigned long)flags, unwinding ? "true" : "false", (unsigned long)address,
                 (unsigned long)(uintptr_t)frame, frame_ok ? "true" : "false", (unsigned long)words[2], (unsigned long)before,
                 (unsigned long)(words[4] >> 16), (unsigned long)words[7], nf_on_thread_stack((uintptr_t)frame) ? "true" : "false");
        ft_event("fault_trace_handler", fields);
    }
    int result = ft_real(record, frame, context, dispatcher);
    InterlockedIncrement(&ft_returns);
    const uint32_t after = ft_context_word(frame);
    if (unwinding && before && before != 0xffffffffu && after == 0) InterlockedIncrement(&ft_cleared);
    if (logged) {
        char fields[128];
        snprintf(fields, sizeof(fields), "\"call\":%ld,\"result\":%d,\"context_word_after\":\"0x%lx\"", (long)call, result, (unsigned long)after);
        ft_event("fault_trace_handler_result", fields);
    }
    return result;
}

static void ft_refused(LONG state) {
    ft_state = state;
    char fields[64];
    snprintf(fields, sizeof(fields), "\"state\":\"%s\"", ft_state_text(state));
    ft_event("fault_trace_refused", fields);
}

/* Memory only. `image` must be DBSIM's image based at expected_base, `real`
 * CW3220.DLL's __ExceptionHandler; `log` may be NULL. FALSE for a refusal
 * (ft_state says why; the refusal is logged). */
static BOOL ft_install_image(BYTE *image, uintptr_t expected_base, uintptr_t real, const WCHAR *log) {
    if (ft_state != FT_OFF) return FALSE;
    ft_log_path[0] = 0;
    if (log && wcslen(log) < MAX_PATH) wcscpy(ft_log_path, log);
    if (!real) { ft_refused(FT_REFUSED_HANDLER); return FALSE; }
    if (!es2_hook_image_valid(image, expected_base, NF_IMAGE_SIZE)) { ft_refused(FT_REFUSED_IMAGE); return FALSE; }
    const uint32_t base = (uint32_t)(uintptr_t)image;
    const BYTE *thunk = image + FT_THUNK_RVA, *setup = image + FT_SETUP_RVA;
    if (thunk[0] != 0xff || thunk[1] != 0x25 || nf_u32(thunk + 2) != base + FT_SLOT_RVA || setup[0] != 0xc7 || setup[1] != 0x43 ||
        setup[2] != 0x04 || nf_u32(setup + 3) != base + FT_THUNK_RVA) { ft_refused(FT_REFUSED_SITE); return FALSE; }
    uint32_t *slot = (uint32_t *)(image + FT_SLOT_RVA);
    if (*slot != (uint32_t)real) { ft_refused(FT_REFUSED_SLOT); return FALSE; }
    DWORD protection, ignored;
    if (!VirtualProtect(slot, 4, PAGE_READWRITE, &protection)) { ft_refused(FT_REFUSED_WRITE); return FALSE; }
    ft_real = (ft_handler_fn)real;
    ft_slot = slot;
    *slot = (uint32_t)(uintptr_t)ft_handler;
    VirtualProtect(slot, 4, protection, &ignored);
    ft_state = FT_INSTALLED;
    /* CW3220 0x4042f0: push ebp; mov ebp,esp; add esp,-34h; push ebx; push esi; push edi; mov eax,[ebp+8] */
    static const BYTE handler_head[12] = {0x55, 0x8b, 0xec, 0x83, 0xc4, 0xcc, 0x53, 0x56, 0x57, 0x8b, 0x45, 0x08};
    const BOOL reference = nf_readable(real, sizeof(handler_head)) && !memcmp((const void *)real, handler_head, sizeof(handler_head));
    char module[64], fields[384];
    uintptr_t module_base;
    nf_describe(real, module, &module_base);
    snprintf(fields, sizeof(fields),
             "\"image\":\"0x%lx\",\"slot\":\"0x%lx\",\"handler\":\"0x%lx\",\"handler_module\":\"%s\",\"handler_offset\":\"0x%lx\","
             "\"handler_reference_bytes\":%s,\"error_mode\":\"0x%lx\"",
             (unsigned long)base, (unsigned long)(uintptr_t)slot, (unsigned long)real, module,
             (unsigned long)(module_base ? real - module_base : 0), reference ? "true" : "false", (unsigned long)GetErrorMode());
    ft_event("fault_trace_installed", fields);
    return TRUE;
}

/* Puts the original target back. Tests only. */
__attribute__((unused)) static void ft_uninstall(void) {
    if (ft_state != FT_INSTALLED || !ft_slot) return;
    DWORD protection, ignored;
    if (VirtualProtect(ft_slot, 4, PAGE_READWRITE, &protection)) {
        *ft_slot = (uint32_t)(uintptr_t)ft_real;
        VirtualProtect(ft_slot, 4, protection, &ignored);
    }
    ft_state = FT_OFF;
    ft_slot = NULL;
}

/* The exit-path probe into this file, when the new-fail switch does not
 * own it. */
static void ft_arm_exit_probe(void) {
    if (ft_state != FT_INSTALLED || nf_state == NF_INSTALLED || nf_probe_state != NF_PROBE_OFF) return;
    wcscpy(nf_log_path, ft_log_path);
    nf_arm_exit_probe();
}

static void ft_shutdown(void) {
    if (ft_state != FT_INSTALLED) return;
    char fields[384];
    snprintf(fields, sizeof(fields),
             "\"calls\":%ld,\"search\":%ld,\"unwinding\":%ld,\"returns\":%ld,\"cleared\":%ld,\"exit_probe\":\"%s\","
             "\"exit_filter_calls\":%ld,\"exit_filter_returns\":%ld",
             (long)ft_calls, (long)ft_search, (long)ft_unwinding, (long)ft_returns, (long)ft_cleared, nf_probe_text(nf_probe_state),
             (long)nf_filter_calls, (long)nf_filter_returns);
    ft_event("fault_trace_summary", fields);
}

#ifndef ES2_NF_NO_PROCESS_ENTRY
/* initialize_forwarding (media_proxy.c): DBSIM.EXE only. */
static void install_fault_trace(void) {
    WCHAR log[MAX_PATH], exe[MAX_PATH];
    DWORD l = GetEnvironmentVariableW(L"ES2_FAULT_TRACE", log, MAX_PATH);
    if (!l || l >= MAX_PATH || ft_state != FT_OFF) return;
    DWORD length = GetModuleFileNameW(NULL, exe, MAX_PATH);
    WCHAR *name = length && length < MAX_PATH ? wcsrchr(exe, L'\\') : NULL;
    if (!name || _wcsicmp(name + 1, L"DBSIM.EXE")) return;
    if (wcsstr(log, L"{suite}")) {
        WCHAR ini[MAX_PATH], suite[65], expanded[MAX_PATH];
        suite[0] = 0;
        nf_suite_name(nf_proxy_ini(ini) ? ini : NULL, suite, 65);
        if (!nf_expand_suite(log, suite, expanded, MAX_PATH)) return;
        wcscpy(log, expanded);
    }
    HMODULE runtime = GetModuleHandleW(L"CW3220.DLL");
    FARPROC real = runtime ? GetProcAddress(runtime, "__ExceptionHandler") : NULL;
    if (ft_install_image((BYTE *)GetModuleHandleW(NULL), NF_LINK_BASE, (uintptr_t)real, log)) ft_arm_exit_probe();
}
#endif
#endif
