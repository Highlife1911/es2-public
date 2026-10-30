/* Process-local IV32 binding; contract in es2/media/iv32_bind.h and
 * docs/reference-game/media_compatibility.md. */
#include "es2/media/iv32_bind.h"

#include <vfw.h>
#include <wchar.h>

static es2_iv32_drivers bound_drivers;
static WCHAR indeo_path[MAX_PATH];
static HMODULE system_vfw;
static es2_iv32_status bind_status = ES2_IV32_NOT_ATTEMPTED;

/* <directory>\<name>; directory NULL = GetSystemDirectoryW. */
static BOOL system_path(WCHAR *buffer, const wchar_t *directory, const WCHAR *name) {
    size_t n;
    if (directory) {
        n = wcslen(directory);
        if (!n || n >= MAX_PATH) return FALSE;
        wcscpy(buffer, directory);
    } else {
        n = GetSystemDirectoryW(buffer, MAX_PATH);
        if (!n) return FALSE;
    }
    if (n + 1 + wcslen(name) >= MAX_PATH) return FALSE;
    buffer[n++] = L'\\';
    wcscpy(buffer + n, name);
    return TRUE;
}

/* The VfW function-table callback. The driver-lifetime messages are answered
 * here; DRV_OPEN opens the wrapper itself and returns its HDRVR as the
 * instance, every other message goes to that instance. */
static LRESULT CALLBACK indeo_bridge(DWORD_PTR instance, HDRVR ignored, UINT message, LPARAM a, LPARAM b) {
    (void)ignored;
    switch (message) {
    case DRV_LOAD: case DRV_ENABLE: case DRV_DISABLE: case DRV_FREE: return 1;
    case DRV_OPEN: return (LRESULT)bound_drivers.open_driver(indeo_path, NULL, b);
    case DRV_CLOSE: return instance ? bound_drivers.close_driver((HDRVR)instance, a, b) : 0;
    default: return instance ? bound_drivers.driver_message((HDRVR)instance, message, a, b) : 0;
    }
}

static es2_iv32_status bind_once(const wchar_t *system_directory) {
    if (!system_path(indeo_path, system_directory, L"ir32_32.dll") || GetFileAttributesW(indeo_path) == INVALID_FILE_ATTRIBUTES)
        return ES2_IV32_WRAPPER_MISSING;
    WCHAR vfw_path[MAX_PATH];
    if (!system_path(vfw_path, system_directory, L"msvfw32.dll")) return ES2_IV32_VFW_PATH;
    system_vfw = LoadLibraryExW(vfw_path, NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!system_vfw) return ES2_IV32_VFW_MISSING;
    HIC (VFWAPI *open_codec)(DWORD, DWORD, UINT) = (void *)GetProcAddress(system_vfw, "ICOpen");
    LRESULT (VFWAPI *close_codec)(HIC) = (void *)GetProcAddress(system_vfw, "ICClose");
    BOOL (VFWAPI *register_function)(DWORD, DWORD, LPARAM, LPSTR, UINT) = (void *)GetProcAddress(system_vfw, "ICInstall");
    if (!open_codec || !close_codec || !register_function) return ES2_IV32_VFW_EXPORTS_MISSING;
    HIC existing = open_codec(ICTYPE_VIDEO, mmioFOURCC('I','V','3','2'), ICMODE_DECOMPRESS);
    if (existing) { close_codec(existing); return ES2_IV32_EXISTING; }
    /* On Windows 10, ICINSTALL_FUNCTION (1) edits only this process's in-memory
     * table. ICINSTALL_DRIVER (2), which writes Drivers32, is never used. */
    return register_function(ICTYPE_VIDEO, mmioFOURCC('I','V','3','2'), (LPARAM)indeo_bridge, NULL, ICINSTALL_FUNCTION)
        ? ES2_IV32_BOUND : ES2_IV32_INSTALL_FAILED;
}

es2_iv32_status es2_iv32_bind(const wchar_t *system_directory, const es2_iv32_drivers *drivers) {
    if (bind_status != ES2_IV32_NOT_ATTEMPTED) return bind_status;
    if (!drivers || !drivers->open_driver || !drivers->close_driver || !drivers->driver_message) return ES2_IV32_BAD_ARGUMENT;
    bound_drivers = *drivers;
    bind_status = bind_once(system_directory);
    return bind_status;
}

const char *es2_iv32_event(es2_iv32_status status) {
    switch (status) {
    case ES2_IV32_WRAPPER_MISSING: return "system_indeo_missing";
    case ES2_IV32_VFW_MISSING: return "system_vfw_missing";
    case ES2_IV32_VFW_EXPORTS_MISSING: return "vfw_exports_missing";
    case ES2_IV32_EXISTING: return "existing_indeo_available";
    case ES2_IV32_BOUND: return "system_indeo_process_function_ready";
    case ES2_IV32_INSTALL_FAILED: return "system_indeo_process_function_failed";
    default: return NULL;
    }
}

int es2_iv32_system_drivers(es2_iv32_drivers *out) {
    WCHAR path[MAX_PATH];
    if (!out || !system_path(path, NULL, L"winmm.dll")) return 0;
    HMODULE winmm = LoadLibraryExW(path, NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!winmm) return 0;
    es2_iv32_drivers found;
    found.open_driver = (void *)GetProcAddress(winmm, "OpenDriver");
    found.close_driver = (void *)GetProcAddress(winmm, "CloseDriver");
    found.driver_message = (void *)GetProcAddress(winmm, "SendDriverMessage");
    if (!found.open_driver || !found.close_driver || !found.driver_message) return 0;
    *out = found;
    return 1;
}

int es2_iv32_bound(void) {
    return bind_status == ES2_IV32_BOUND;
}

int es2_iv32_release(void) {
    if (bind_status != ES2_IV32_BOUND) return 0;
    BOOL (VFWAPI *remove_codec)(DWORD, DWORD, UINT) = (void *)GetProcAddress(system_vfw, "ICRemove");
    if (!remove_codec || !remove_codec(ICTYPE_VIDEO, mmioFOURCC('I','V','3','2'), 0)) return 0;
    bind_status = ES2_IV32_NOT_ATTEMPTED;
    return 1;
}
