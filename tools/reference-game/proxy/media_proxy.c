// DirectDraw forwarding shim for EarthSiege II. No downloaded or bundled codec.
// Existing Windows Indeo is exposed through a process-local VfW function table.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <vfw.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "exports.h"
#include "es2/media/iv32_bind.h"

FARPROC targets[EXPORT_COUNT];
static INIT_ONCE forwarding_once = INIT_ONCE_STATIC_INIT;
static INIT_ONCE codec_once = INIT_ONCE_STATIC_INIT;
static HMODULE self_module, system_ddraw, system_winmm;
static WCHAR config_path[MAX_PATH];
typedef MCIERROR (WINAPI *MciCommand)(MCIDEVICEID, UINT, DWORD_PTR, DWORD_PTR);
typedef MCIERROR (WINAPI *MciString)(LPCSTR, LPSTR, UINT, HWND);
static MciCommand previous_mci_command;
static MciString previous_mci_string;
static BOOL CALLBACK initialize_forwarding(PINIT_ONCE once, PVOID arg, PVOID *context);
static HDRVR (WINAPI *real_open_driver)(LPCWSTR, LPCWSTR, LPARAM);
static LRESULT (WINAPI *real_close_driver)(HDRVR, LPARAM, LPARAM);
static LRESULT (WINAPI *real_driver_message)(HDRVR, UINT, LPARAM, LPARAM);

static void log_event(const char *event) {
    WCHAR path[MAX_PATH];
    if (!GetPrivateProfileStringW(L"video", L"log", L"", path, MAX_PATH, config_path)) return;
    HANDLE file = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return;
    char line[256];
    int length = snprintf(line, sizeof(line), "{\"pid\":%lu,\"event\":\"%s\"}\r\n", (unsigned long)GetCurrentProcessId(), event);
    DWORD written;
    if (length > 0 && length < (int)sizeof(line)) WriteFile(file, line, (DWORD)length, &written, NULL);
    CloseHandle(file);
}

#include "mission_map_guard.h"
#include "virtual_time.h"
#include "oracle_perf.h"
#include "input_journal.h"
#include "window_shim.h"
#include "presenter.h"
#include "ddraw_windowed.h"
#include "rng_hooks.h"
#include "rtl_imports.h"
#include "early_arming.h"
#include "bwcc32_switch.h"
#include "new_fail_switch.h"
#include "fault_trace.h"
#include "live_coverage.h"
#include "entry_probes.h"
#include "vshell_probes.h"
#include "vol_probes.h"
#include "tick_log.h"
#include "ddraw_observer.h"
#include "oracle_perf_runtime.h"
#include "vshell_capture.h"
#include "call_probe.h"

static BOOL system_path(WCHAR *buffer, const WCHAR *name) {
    UINT n = GetSystemDirectoryW(buffer, MAX_PATH);
    if (!n || n + 1 + wcslen(name) >= MAX_PATH) return FALSE;
    buffer[n++] = L'\\';
    wcscpy(buffer + n, name);
    return TRUE;
}

// The binding itself (OpenDriver on the system ir32_32.dll wrapper,
// ICInstall with ICINSTALL_FUNCTION) lives in //media:iv32_bind; same calls,
// same order, same log events. The [video] switch stays here.
static BOOL CALLBACK initialize_codec(PINIT_ONCE once, PVOID arg, PVOID *context) {
    (void)once; (void)arg; (void)context;
    if (!GetPrivateProfileIntW(L"video", L"system_indeo", 0, config_path)) return TRUE;
    es2_iv32_drivers drivers = {real_open_driver, real_close_driver, real_driver_message};
    const char *event = es2_iv32_event(es2_iv32_bind(NULL, &drivers));
    if (event) log_event(event);
    return TRUE;
}

static BOOL CALLBACK initialize_forwarding(PINIT_ONCE once, PVOID arg, PVOID *context) {
    (void)once; (void)arg; (void)context;
    WCHAR path[MAX_PATH];
    if (!system_path(path, L"ddraw.dll")) return FALSE;
    system_ddraw = LoadLibraryExW(path, NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!system_ddraw || system_ddraw == self_module) return FALSE;
    if (!system_path(path, L"winmm.dll")) return FALSE;
    system_winmm = LoadLibraryExW(path, NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!system_winmm || system_winmm == self_module) return FALSE;
    for (unsigned i = 0; i < EXPORT_COUNT; ++i) {
        targets[i] = GetProcAddress(system_ddraw, (LPCSTR)(ULONG_PTR)export_ordinals[i]);
    }
    real_open_driver = (void *)GetProcAddress(system_winmm, "OpenDriver");
    real_close_driver = (void *)GetProcAddress(system_winmm, "CloseDriver");
    real_driver_message = (void *)GetProcAddress(system_winmm, "SendDriverMessage");
    if (!real_open_driver || !real_close_driver || !real_driver_message) return FALSE;
    DWORD n = GetModuleFileNameW(self_module, config_path, MAX_PATH);
    if (!n || n >= MAX_PATH) return FALSE;
    WCHAR *slash = wcsrchr(config_path, L'\\');
    if (!slash || slash - config_path + 1 + wcslen(L"es2-oracle.ini") >= MAX_PATH) return FALSE;
    wcscpy(slash + 1, L"es2-oracle.ini");
    WCHAR speed_ini[MAX_PATH], exe[MAX_PATH];
    DWORD speed_n = GetEnvironmentVariableW(L"ES2_ORACLE_SPEED_CONFIG", speed_ini, MAX_PATH);
    if (!speed_n) wcscpy(speed_ini, config_path);
    DWORD exe_n = GetModuleFileNameW(NULL, exe, MAX_PATH);
    WCHAR *exe_name = exe_n && exe_n < MAX_PATH ? wcsrchr(exe, L'\\') : NULL;
    // [presentation] directdraw=windowed answers DirectDrawCreate with own objects
    // (ddraw_windowed.h); the observer below, when on, wraps them like the system's.
    if (ddw_requested()) for (unsigned i = 0; i < EXPORT_COUNT; ++i) if (!strcmp(export_names[i], "DirectDrawCreate")) {
        targets[i] = (FARPROC)ddw_create; ddw_enabled = TRUE; log_event("presentation_directdraw_windowed");
    }
    BOOL speed_requested = speed_n < MAX_PATH &&
        (GetPrivateProfileIntW(L"perf",L"enabled",0,speed_ini) ||
         GetPrivateProfileIntW(L"presentation",L"defer_flip",0,speed_ini) ||
         GetPrivateProfileIntW(L"presentation",L"every_n",0,speed_ini)>1 ||
         GetPrivateProfileIntW(L"trace",L"tick_log_buffered",0,config_path));
    BOOL reference = speed_requested && exe_name && !_wcsicmp(exe_name+1, L"DBSIM.EXE") &&
        rng_hook_reference_file(exe) && es2_hook_image_valid((BYTE *)GetModuleHandleW(NULL), 0x400000, RNG_HOOK_IMAGE_SIZE);
    if (reference && speed_n < MAX_PATH) {
        perf_module_sha256 = LC_MODULE_HASH;
        if (!perf_configure(speed_ini)) log_event("oracle_perf_refused");
        if (perf_enabled) {
            perf_hook_names=GetPrivateProfileIntW(L"perf",L"hook_names",1,speed_ini)!=0;
            perf_dispatch_target = perf_dispatch_measured;
            if (!perf_input_slots()) { perf_error=6; log_event("oracle_perf_input_refused"); }
        }
        dd_defer_flip = GetPrivateProfileIntW(L"presentation", L"defer_flip", 0, speed_ini) != 0;
        dd_present_every = GetPrivateProfileIntW(L"presentation", L"every_n", 0, speed_ini);
        if (dd_present_every>1000) dd_present_every=0;
        perf_presentation_every=dd_present_every;perf_defer_flip=dd_defer_flip;
        dd_observe = perf_enabled || dd_defer_flip || dd_present_every>1;
        if (dd_observe) perf_before_complete=dd_finish;
        if (dd_observe) for (unsigned i=0; i<EXPORT_COUNT; ++i) if (!strcmp(export_names[i], "DirectDrawCreate")) {
            dd_real_create = (void *)targets[i]; targets[i] = (FARPROC)dd_create;
        }
    }
    // vol_probes.h: armed at process attach; publish the startup records and check
    // the module file hash here, outside the loader lock.
    vol_probes_forwarding();
    install_mission_map_guard();
    install_tick_log();
    // Tick validation checks the unpatched RNG prologue; install RNG entries last.
    /* Primary INI always applies. A separate measurement INI may explicitly
     * override the key without the suite runner having to rewrite it. */
    WCHAR fast_override[8];
    const WCHAR *fast_ini = config_path;
    if (speed_n && speed_n < MAX_PATH && GetPrivateProfileStringW(L"hooks", L"fast_path", L"", fast_override, 8, speed_ini)) fast_ini = speed_ini;
    if (!es2_hook_configure_fast_path(fast_ini)) {
        es2_hook_set_fast_path(FALSE); log_event("hook_fast_path_config_refused");
    }
    if (perf_enabled) es2_hook_set_fast_observer(perf_fast_begin, perf_fast_end);
    if (install_rng_hooks() && install_live_coverage()) install_entry_probes();
    // Off unless the capture INI has [call_probe] enabled=1 (call_probe.h); after
    // the hooks and entry probes, so that a patched entry is seen and refused.
    install_call_probe();
    // The CODE pages' protection after all patches (only with ES2_NEW_FAIL).
    nf_report_armed();
    // Off unless ES2_FAULT_TRACE names a log (fault_trace.h); after the runtime's
    // import check at DBSIM's entry, which compares the slot it changes.
    install_fault_trace();
    if (reference && (perf_enabled || tick_io_buffered || dd_observe) && !speed_install_exit((BYTE *)GetModuleHandleW(NULL))) {
        perf_error = 4; log_event("oracle_speed_exit_refused");
    }
    return TRUE;
}

static MCIERROR WINAPI hook_mci_command(MCIDEVICEID id, UINT message, DWORD_PTR flags, DWORD_PTR parameters) {
    if (InitOnceExecuteOnce(&forwarding_once, initialize_forwarding, NULL, NULL)) {
        InitOnceExecuteOnce(&codec_once, initialize_codec, NULL, NULL);
    }
    return previous_mci_command(id, message, flags, parameters);
}

static MCIERROR WINAPI hook_mci_string(LPCSTR command, LPSTR result, UINT length, HWND callback) {
    if (InitOnceExecuteOnce(&forwarding_once, initialize_forwarding, NULL, NULL)) {
        InitOnceExecuteOnce(&codec_once, initialize_codec, NULL, NULL);
    }
    return previous_mci_string(command, result, length, callback);
}

// Only replace the importing executable's two MCI slots, not system code or files.
// DllMain does no loading, codec work or file I/O; that is deferred to the first MCI call
// (or, with [arming] early=1, to the call of the mission load: early_arming.h).
// Exception (vol_probes.h): only while ES2_LIVE_COVERAGE_CONFIG is set, DllMain reads
// its [vol_probes] section and, if enabled, reserves the report file (kernel32 file I/O).
static void hook_executable_mci(void) {
    BYTE *image = (BYTE *)GetModuleHandleW(NULL);
    if (!image) return;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)image;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return;
    IMAGE_NT_HEADERS32 *nt = (IMAGE_NT_HEADERS32 *)(image + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) return;
    DWORD size = nt->OptionalHeader.SizeOfImage;
    IMAGE_DATA_DIRECTORY imports = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!imports.VirtualAddress || imports.VirtualAddress >= size || imports.Size > size - imports.VirtualAddress) return;
    IMAGE_IMPORT_DESCRIPTOR *desc = (IMAGE_IMPORT_DESCRIPTOR *)(image + imports.VirtualAddress);
    for (DWORD i = 0; i < imports.Size / sizeof(*desc) && desc[i].Name; ++i) {
        if (desc[i].Name >= size || _stricmp((char *)(image + desc[i].Name), "winmm.dll")) continue;
        if (!desc[i].OriginalFirstThunk || desc[i].OriginalFirstThunk >= size || desc[i].FirstThunk >= size) continue;
        IMAGE_THUNK_DATA32 *names = (IMAGE_THUNK_DATA32 *)(image + desc[i].OriginalFirstThunk);
        IMAGE_THUNK_DATA32 *slots = (IMAGE_THUNK_DATA32 *)(image + desc[i].FirstThunk);
        DWORD count = (size - desc[i].OriginalFirstThunk) / sizeof(*names);
        DWORD slot_count = (size - desc[i].FirstThunk) / sizeof(*slots);
        if (slot_count < count) count = slot_count;
        for (DWORD j = 0; j < count && names[j].u1.AddressOfData; ++j) {
            if (IMAGE_SNAP_BY_ORDINAL32(names[j].u1.Ordinal) || names[j].u1.AddressOfData >= size - sizeof(IMAGE_IMPORT_BY_NAME)) continue;
            IMAGE_IMPORT_BY_NAME *name = (IMAGE_IMPORT_BY_NAME *)(image + names[j].u1.AddressOfData);
            FARPROC replacement = NULL;
            if (!strcmp(name->Name, "mciSendCommandA")) replacement = (FARPROC)hook_mci_command;
            if (!strcmp(name->Name, "mciSendStringA")) replacement = (FARPROC)hook_mci_string;
            if (!replacement) continue;
            DWORD protection;
            if (!VirtualProtect(&slots[j].u1.Function, sizeof(DWORD), PAGE_READWRITE, &protection)) continue;
            if (replacement == (FARPROC)hook_mci_command) previous_mci_command = (MciCommand)(ULONG_PTR)slots[j].u1.Function;
            else previous_mci_string = (MciString)(ULONG_PTR)slots[j].u1.Function;
            InterlockedExchange((LONG *)&slots[j].u1.Function, (LONG)(ULONG_PTR)replacement);
            DWORD ignored;
            VirtualProtect(&slots[j].u1.Function, sizeof(DWORD), protection, &ignored);
        }
    }
}

void __cdecl resolve_export(unsigned index) {
    if (!InitOnceExecuteOnce(&forwarding_once, initialize_forwarding, NULL, NULL) || index >= EXPORT_COUNT || !targets[index]) {
        TerminateProcess(GetCurrentProcess(), ERROR_PROC_NOT_FOUND);
        return;
    }
    if (!strcmp(export_names[index], "DirectDrawCreate") || !strcmp(export_names[index], "DirectDrawCreateEx")) {
        InitOnceExecuteOnce(&codec_once, initialize_codec, NULL, NULL);
    }
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        self_module = module;
        DisableThreadLibraryCalls(module);
        hook_executable_mci();
        // Only with ES2_RTL_CONFIG: arms the CW3220 import shim at DBSIM's entry.
        install_rtl_startup();
        // L0: only with [arming] early=1 or [snapshot] after_load=1 (the two
        // INI keys are read here, kernel32 only): arms at the call of the
        // mission load 0x45f42b and/or writes the after-load snapshot.
        install_early_arming();
        // Only with ES2_BWCC32=refuse: DBSIM's LoadLibraryA("BWCC32.DLL") at
        // 0x465140 fails (bwcc32_switch.h; kernel32 only, memory only).
        install_bwcc32_switch();
        // Off unless ES2_NEW_FAIL=<site>:<n> (DBSIM.EXE only): `new` answers NULL for the n-th
        // request of the registration 0x479cfc (new_fail_switch.h; kernel32 only, memory only).
        install_new_fail_switch();
        install_virtual_time();
        install_input_journal();
        // Chains CreateWindowExA; hides the window only in replay with [window] hidden=1.
        install_window_shim();
        // Chains CreateWindowExA; patches StretchBlt/SetDIBColorTable only with
        // [presentation] capture=1 or presenter=gdi (presenter.h).
        install_presenter();
        // VSHELL.EXE only, off unless ES2_LIVE_COVERAGE_CONFIG has [vshell_probes] enabled=1.
        install_vshell_probes(module);
        // Off unless ES2_LIVE_COVERAGE_CONFIG has [vol_probes] enabled=1.
        vol_probes_process_attach();
        // Off unless ES2_VSHELL_CAPTURE_CONFIG is set and the process is VSHELL.EXE.
        install_vshell_capture();
    } else if (reason == DLL_PROCESS_DETACH) {
        vol_probes_shutdown("detach");
        live_coverage_shutdown("detach");
        // Other threads may already be terminated while owning our locks.
        // Buffered runs drain through the runtime exit hooks / runner handshake.
        if (!tick_io_ready) tick_log_shutdown();
        input_journal_shutdown();
        presenter_shutdown();
        rng_hooks_shutdown();
        vshell_capture_shutdown();
        call_probe_shutdown();
        nf_shutdown();
        ft_shutdown();
    }
    // The proxy is an import dependency for the lifetime of the process. The function
    // table and driver instances are reclaimed at process exit; no registry cleanup.
    return TRUE;
}
