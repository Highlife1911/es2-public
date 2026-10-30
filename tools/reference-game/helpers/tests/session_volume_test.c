// Runs session_volume.exe (path in argv[1]): bad arguments are refused with
// exit 2, and the test's own process, which never opens an audio device,
// has no session, so nothing is set and the helper still exits 0.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "session_volume_test line %d: %s\n", __LINE__, #x); return 1; } } while (0)

static DWORD run(const wchar_t *exe, const wchar_t *args, char *out, DWORD size) {
    wchar_t line[1024];
    swprintf(line, 1024, L"\"%ls\" %ls", exe, args);
    SECURITY_ATTRIBUTES sa = {sizeof sa, NULL, TRUE};
    HANDLE read = NULL, write = NULL;
    if (!CreatePipe(&read, &write, &sa, 0)) return 1000;
    SetHandleInformation(read, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si;
    ZeroMemory(&si, sizeof si);
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = write;
    si.hStdError = write;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi;
    if (!CreateProcessW(NULL, line, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) return 1001;
    CloseHandle(write);
    DWORD used = 0, n = 0;
    while (used + 1 < size && ReadFile(read, out + used, size - 1 - used, &n, NULL) && n) used += n;
    out[used] = 0;
    CloseHandle(read);
    WaitForSingleObject(pi.hProcess, 30000);
    DWORD code = 1002;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return code;
}

// The helper's runfile through the runfiles manifest (Windows has no
// runfiles tree), as bazel/defs/python_test.c resolves its inputs.
static BOOL resolve(const wchar_t *name, wchar_t *path, size_t size) {
    const char *manifest = getenv("RUNFILES_MANIFEST_FILE");
    char key[1024], line[4096];
    if (!manifest || snprintf(key, sizeof key, "_main/%ls ", name) >= (int)sizeof key) return FALSE;
    FILE *stream = fopen(manifest, "rb");
    if (!stream) return FALSE;
    BOOL found = FALSE;
    while (!found && fgets(line, sizeof line, stream)) {
        if (strncmp(line, key, strlen(key))) continue;
        char *value = line + strlen(key);
        value[strcspn(value, "\r\n")] = 0;
        found = MultiByteToWideChar(CP_UTF8, 0, value, -1, path, (int)size) > 0;
    }
    fclose(stream);
    return found;
}

int wmain(int argc, wchar_t **argv) {
    CHECK(argc == 2);
    wchar_t exe[MAX_PATH];
    CHECK(resolve(argv[1], exe, MAX_PATH));
    char out[4096];
    CHECK(run(exe, L"--pid 1", out, sizeof out) == 2);
    CHECK(run(exe, L"--pid 1 --percent 101", out, sizeof out) == 2);
    CHECK(run(exe, L"--pid 1 --percent 10 --level 3", out, sizeof out) == 2);
    wchar_t args[128];
    swprintf(args, 128, L"--pid %lu --percent 10 --wait-ms 0", (unsigned long)GetCurrentProcessId());
    CHECK(run(exe, args, out, sizeof out) == 0);
    CHECK(strstr(out, "\"percent\":10,") != NULL);
    CHECK(strstr(out, "\"sessions_seen\":0,\"volumes_set\":0}") != NULL);
    printf("PASS: argument checks; own process without an audio session: %s", out);
    return 0;
}
