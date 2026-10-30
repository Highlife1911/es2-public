// Lowers the Windows audio session volume of one running process (the
// per-application slider of the volume mixer) from outside that process:
// nothing in the game's address space is read or written, so the oracle's
// compared state cannot change. The suite runners start it next to DBSIM so
// that unattended test runs are quiet. A process gets its session only when
// it opens the audio device, so the helper polls the default render
// endpoint until the process exits or the wait expires and sets every
// session of the process it sees.
//
// Usage: session_volume.exe --pid PID --percent 0..100 [--wait-ms MS]
// Prints one JSON line; exit 0 also when no session was found.
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <stdio.h>
#include <wchar.h>

static unsigned sessions_seen, volumes_set;

// One pass over the endpoint's sessions: every session of `pid` whose level
// differs from `level` is set.
static void set_sessions(IAudioSessionManager2 *manager, DWORD pid, float level) {
    IAudioSessionEnumerator *list = NULL;
    int count = 0;
    if (FAILED(IAudioSessionManager2_GetSessionEnumerator(manager, &list))) return;
    if (SUCCEEDED(IAudioSessionEnumerator_GetCount(list, &count))) {
        for (int i = 0; i < count; ++i) {
            IAudioSessionControl *control = NULL;
            IAudioSessionControl2 *control2 = NULL;
            ISimpleAudioVolume *volume = NULL;
            DWORD owner = 0;
            float now = -1.0f;
            if (FAILED(IAudioSessionEnumerator_GetSession(list, i, &control))) continue;
            if (SUCCEEDED(IAudioSessionControl_QueryInterface(control, &IID_IAudioSessionControl2, (void **)&control2)) &&
                IAudioSessionControl2_GetProcessId(control2, &owner) == S_OK && owner == pid) {
                ++sessions_seen;
                if (SUCCEEDED(IAudioSessionControl_QueryInterface(control, &IID_ISimpleAudioVolume, (void **)&volume))) {
                    if (SUCCEEDED(ISimpleAudioVolume_GetMasterVolume(volume, &now)) && now != level &&
                        SUCCEEDED(ISimpleAudioVolume_SetMasterVolume(volume, level, NULL))) ++volumes_set;
                    ISimpleAudioVolume_Release(volume);
                }
            }
            if (control2) IAudioSessionControl2_Release(control2);
            IAudioSessionControl_Release(control);
        }
    }
    IAudioSessionEnumerator_Release(list);
}

int wmain(int argc, wchar_t **argv) {
    DWORD pid = 0;
    long percent = -1, wait_ms = 0;
    BOOL valid = argc % 2 == 1;
    for (int i = 1; valid && i + 1 < argc; i += 2) {
        if (!wcscmp(argv[i], L"--pid")) pid = wcstoul(argv[i + 1], NULL, 10);
        else if (!wcscmp(argv[i], L"--percent")) percent = wcstol(argv[i + 1], NULL, 10);
        else if (!wcscmp(argv[i], L"--wait-ms")) wait_ms = wcstol(argv[i + 1], NULL, 10);
        else valid = FALSE;
    }
    if (!valid || !pid || percent < 0 || percent > 100 || wait_ms < 0) {
        fputs("usage: session_volume.exe --pid PID --percent 0..100 [--wait-ms MS]\n", stderr);
        return 2;
    }
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid);
    const char *status = "ok";
    IMMDeviceEnumerator *devices = NULL;
    IMMDevice *device = NULL;
    IAudioSessionManager2 *manager = NULL;
    if (FAILED(CoInitializeEx(NULL, COINIT_MULTITHREADED))) status = "com_failed";
    else if (FAILED(CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, &IID_IMMDeviceEnumerator, (void **)&devices)))
        status = "no_enumerator";
    else if (FAILED(IMMDeviceEnumerator_GetDefaultAudioEndpoint(devices, eRender, eConsole, &device)))
        status = "no_render_device";
    else if (FAILED(IMMDevice_Activate(device, &IID_IAudioSessionManager2, CLSCTX_ALL, NULL, (void **)&manager)))
        status = "no_session_manager";
    if (manager) {
        const float level = (float)percent / 100.0f;
        const ULONGLONG end = GetTickCount64() + (ULONGLONG)wait_ms;
        for (;;) {
            set_sessions(manager, pid, level);
            if (!process || GetTickCount64() >= end) break;
            if (WaitForSingleObject(process, 200) == WAIT_OBJECT_0) break;
        }
        IAudioSessionManager2_Release(manager);
    }
    if (device) IMMDevice_Release(device);
    if (devices) IMMDeviceEnumerator_Release(devices);
    if (process) CloseHandle(process);
    printf("{\"pid\":%lu,\"percent\":%ld,\"status\":\"%s\",\"sessions_seen\":%u,\"volumes_set\":%u}\n",
           (unsigned long)pid, percent, status, sessions_seen, volumes_set);
    return 0;
}
