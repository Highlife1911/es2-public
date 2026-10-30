// Read-only Win32 graphics / Video for Windows diagnostics. No codec installation.
#define COBJMACROS
#include <windows.h>
#include <ddraw.h>
#include <vfw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// OpenDriver supplies the real HDRVR required by the Windows Indeo wrapper.
// Calling its DriverProc through ICOpenFunction with a null HDRVR is not supported.
static WCHAR system_indeo[MAX_PATH];
static LRESULT CALLBACK local_indeo(DWORD_PTR instance, HDRVR driver, UINT message, LPARAM a, LPARAM b) {
    (void)driver;
    switch (message) {
    case DRV_LOAD: case DRV_ENABLE: case DRV_DISABLE: case DRV_FREE: return 1;
    case DRV_OPEN: return (LRESULT)OpenDriver(system_indeo, NULL, b);
    case DRV_CLOSE: return instance ? CloseDriver((HDRVR)instance, a, b) : 0;
    default: return instance ? SendDriverMessage((HDRVR)instance, message, a, b) : 0;
    }
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    HDC dc = GetDC(NULL);
    printf("{\"kind\":\"display\",\"process_bits\":%u,\"bits_pixel\":%d,\"planes\":%d,\"palette_size\":%d,\"raster_caps\":%d}\n",
           (unsigned)(sizeof(void *) * 8), GetDeviceCaps(dc, BITSPIXEL), GetDeviceCaps(dc, PLANES),
           GetDeviceCaps(dc, SIZEPALETTE), GetDeviceCaps(dc, RASTERCAPS));
    ReleaseDC(NULL, dc);
    HIC codec = ICOpen(ICTYPE_VIDEO, mmioFOURCC('I','V','3','2'), ICMODE_DECOMPRESS);
    printf("{\"kind\":\"codec\",\"fourcc\":\"IV32\",\"open\":%s}\n", codec ? "true" : "false");
    if (codec) ICClose(codec);
    if (argc == 1) return codec ? 4 : 0;
    if (argc > 3 || (argc == 3 && strcmp(argv[2], "--system-indeo") != 0 && strcmp(argv[2], "--proxy-mci") != 0)) return 2;
    BOOL local_codec = FALSE;
    if (argc == 3 && strcmp(argv[2], "--system-indeo") == 0) {
        UINT length = GetSystemDirectoryW(system_indeo, MAX_PATH);
        if (!length || length >= MAX_PATH - 13) return 2;
        wcscat(system_indeo, L"\\ir32_32.dll");
        // The in-memory ICINSTALL_FUNCTION branch never writes the codec registry.
        local_codec = ICInstall(ICTYPE_VIDEO, mmioFOURCC('I','V','3','2'), (LPARAM)local_indeo, NULL, ICINSTALL_FUNCTION);
        printf("{\"kind\":\"process_codec\",\"registered\":%s}\n", local_codec ? "true" : "false");
        // The child has no function table entry: this probes isolation while ours is still active.
        char executable[MAX_PATH], command[MAX_PATH + 4];
        if (GetModuleFileNameA(NULL, executable, MAX_PATH) >= MAX_PATH) return 2;
        snprintf(command, sizeof(command), "\"%s\"", executable);
        STARTUPINFOA startup = {0};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION child = {0};
        fflush(stdout);
        if (!CreateProcessA(executable, command, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &startup, &child)) return 2;
        DWORD wait = WaitForSingleObject(child.hProcess, 10000);
        DWORD childExit = 99;
        GetExitCodeProcess(child.hProcess, &childExit);
        printf("{\"kind\":\"child_codec_isolation\",\"codec_absent\":%s}\n", childExit == 0 ? "true" : "false");
        CloseHandle(child.hThread);
        CloseHandle(child.hProcess);
        if (wait != WAIT_OBJECT_0) return 2;
    }
    if (argc == 3 && strcmp(argv[2], "--proxy-mci") == 0) {
        char reply[80] = {0};
        MCIERROR error = mciSendStringA("sysinfo all quantity", reply, sizeof(reply), NULL);
        printf("{\"kind\":\"mci_probe\",\"error\":%lu,\"devices\":\"%s\",\"time\":%lu,\"wave_devices\":%u,\"joysticks\":%u}\n",
               (unsigned long)error, reply, (unsigned long)timeGetTime(), waveOutGetNumDevs(), joyGetNumDevs());
        if (error) return 1;
        HIC ready = ICOpen(ICTYPE_VIDEO, mmioFOURCC('I','V','3','2'), ICMODE_DECOMPRESS);
        printf("{\"kind\":\"codec_before_directdraw\",\"open\":%s}\n", ready ? "true" : "false");
        if (!ready) return 1;
        ICClose(ready);
        // Deliberately use MCI before DirectDraw, as VSHELL does at intro startup.
        LPDIRECTDRAW draw = NULL;
        HRESULT drawing = DirectDrawCreate(NULL, &draw, NULL);
        printf("{\"kind\":\"directdraw_probe\",\"hresult\":\"0x%08lx\"}\n", (unsigned long)drawing);
        if (draw) IDirectDraw_Release(draw);
        if (FAILED(drawing)) return 1;

    }
    const char *modules[] = { "winmm.dll", "ddraw.dll", "msvfw32.dll" };
    for (unsigned i = 0; i < sizeof(modules) / sizeof(modules[0]); ++i) {
        char path[MAX_PATH] = {0};
        HMODULE module = GetModuleHandleA(modules[i]);
        if (module) GetModuleFileNameA(module, path, MAX_PATH);
        for (char *p = path; *p; ++p) if (*p == '\\') *p = '/';
        printf("{\"kind\":\"module\",\"name\":\"%s\",\"path\":\"%s\"}\n", modules[i], path);
    }
    AVIFileInit();
    PAVIFILE file = NULL;
    PAVISTREAM stream = NULL;
    PGETFRAME frames = NULL;
    int result = 1;
    HRESULT hr = AVIFileOpenA(&file, argv[1], OF_READ | OF_SHARE_DENY_WRITE, NULL);
    printf("{\"kind\":\"avi_open\",\"hresult\":\"0x%08lx\"}\n", (unsigned long)hr);
    if (hr != AVIERR_OK) goto cleanup;
    hr = AVIFileGetStream(file, &stream, streamtypeVIDEO, 0);
    if (hr != AVIERR_OK) goto cleanup;
    AVISTREAMINFOA info = {0};
    hr = AVIStreamInfoA(stream, &info, sizeof(info));
    if (hr != AVIERR_OK) goto cleanup;
    char fourcc[5] = {0};
    memcpy(fourcc, &info.fccHandler, 4);
    printf("{\"kind\":\"video\",\"fourcc\":\"%.4s\",\"frames\":%lu,\"rate\":%lu,\"scale\":%lu}\n",
           fourcc, (unsigned long)info.dwLength, (unsigned long)info.dwRate, (unsigned long)info.dwScale);
    frames = AVIStreamGetFrameOpen(stream, NULL);
    printf("{\"kind\":\"frame_decoder\",\"open\":%s}\n", frames ? "true" : "false");
    if (frames) {
        DWORD decoded = 0;
        DWORD non_black = 0;
        LONG start = AVIStreamStart(stream);
        for (DWORD n = 0; n < info.dwLength; ++n) {
            BITMAPINFOHEADER *frame = AVIStreamGetFrame(frames, start + (LONG)n);
            if (!frame) break;
            ++decoded;
            if (frame->biCompression == BI_RGB && frame->biWidth > 0 && frame->biHeight > 0) {
                size_t stride = (((size_t)frame->biWidth * frame->biBitCount + 31) / 32) * 4;
                DWORD colors = frame->biClrUsed ? frame->biClrUsed : (frame->biBitCount <= 8 ? 1u << frame->biBitCount : 0);
                const unsigned char *pixels = (const unsigned char *)frame + frame->biSize + colors * sizeof(RGBQUAD);
                size_t bytes = stride * (size_t)frame->biHeight;
                for (size_t i = 0; i < bytes; ++i) if (pixels[i]) { ++non_black; break; }
            }
        }
        printf("{\"kind\":\"frames_decoded\",\"decoded\":%lu,\"expected\":%lu,\"frames_with_nonzero_pixels\":%lu}\n",
               (unsigned long)decoded, (unsigned long)info.dwLength, (unsigned long)non_black);
        result = decoded == info.dwLength && decoded > 0 ? 0 : 1;
    }
cleanup:
    if (frames) AVIStreamGetFrameClose(frames);
    if (stream) AVIStreamRelease(stream);
    if (file) AVIFileRelease(file);
    AVIFileExit();
    // Remove only an entry successfully created by this process; otherwise ICRemove could touch the registry.
    if (local_codec) ICRemove(ICTYPE_VIDEO, mmioFOURCC('I','V','3','2'), 0);
    return result;
}