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
    install_mission_map_guard();
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
// DllMain does no loading, codec work or file I/O; that is deferred to the first MCI call.
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
    }
    // The proxy is an import dependency for the lifetime of the process. The function
    // table and driver instances are reclaimed at process exit; no registry cleanup.
    return TRUE;
}
