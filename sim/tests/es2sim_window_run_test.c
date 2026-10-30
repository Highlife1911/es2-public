/* The windowed es2sim (//sim:es2sim_window, argv[1]: its runfiles path) as a process
 * on SDL's dummy video driver (SDL_VIDEODRIVER=dummy in the environment the children
 * inherit; no visible window). Usage refused with 64 (`--headless` is optional in this
 * build). No DBSIM.EXE is read and no game data is needed, and the image claim of
 * _es2sim_start still holds with SDL3.dll loaded. The launcher's line in a synthetic installation directory (as es2sim_run_test's case 3) windowed:
 * the window opens after the DIB section and the run ends in the fatal report of the
 * mission load's launch peek without script.dat (its box and exit code 1; headless
 * refused at its __exit, 70), with the metrics and the opened window (640 x 480)
 * printed. Every child starts with CREATE_NO_WINDOW. ES2SIM_MESSAGE_BOX=none
 * (no desktop: the box is printed, not shown, and answers IDOK); without
 * data\drive.cfg the fatal report's box (owner 0, caption "Error", style 0) and
 * __exit(1) end the windowed run with 1 as headless; without -eggplant the eggplant
 * box (style 0x30). */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

static char *resolve(const char *name) {
    const char *manifest = getenv("RUNFILES_MANIFEST_FILE");
    char line[16384], key[4096];
    if (!manifest || !*manifest || snprintf(key, sizeof key, "_main/%s ", name) >= (int)sizeof key) return NULL;
    FILE *stream = fopen(manifest, "rb");
    if (!stream) return NULL;
    size_t key_size = strlen(key);
    char *out = NULL;
    while (fgets(line, sizeof line, stream)) {
        if (strncmp(line, key, key_size)) continue;
        size_t n = strcspn(line + key_size, "\r\n");
        out = malloc(n + 1);
        if (out) { memcpy(out, line + key_size, n); out[n] = 0; }
        break;
    }
    fclose(stream);
    return out;
}

static char exe[MAX_PATH];

/* Runs `es2sim_window <arguments>` in `directory` with its standard output
 * in `output` and its standard error in `output`.err (NULL: both
 * inherited); the exit code, -1 on a start failure. */
static long run_in(const char *arguments, const char *directory, const char *output) {
    char command[4096];
    if (snprintf(command, sizeof command, "\"%s\" %s", exe, arguments) >= (int)sizeof command) return -1;
    STARTUPINFOA si; PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof si); si.cb = sizeof si;
    HANDLE out = INVALID_HANDLE_VALUE, err = INVALID_HANDLE_VALUE;
    if (output) {
        SECURITY_ATTRIBUTES sa = {sizeof sa, NULL, TRUE};
        char err_path[MAX_PATH];
        if (snprintf(err_path, sizeof err_path, "%s.err", output) >= (int)sizeof err_path) return -1;
        out = CreateFileA(output, GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (out == INVALID_HANDLE_VALUE) return -1;
        err = CreateFileA(err_path, GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (err == INVALID_HANDLE_VALUE) { CloseHandle(out); return -1; }
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdOutput = out;
        si.hStdError = err;
        si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    }
    BOOL started = CreateProcessA(exe, command, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, directory, &si, &pi);
    if (out != INVALID_HANDLE_VALUE) CloseHandle(out);
    if (err != INVALID_HANDLE_VALUE) CloseHandle(err);
    if (!started) return -1;
    DWORD code = (DWORD)-1;
    WaitForSingleObject(pi.hProcess, 120000);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return (long)code;
}
static long run(const char *arguments) { return run_in(arguments, NULL, NULL); }

static size_t slurp(const char *path, char *text, size_t size) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    size_t n = fread(text, 1, size - 1, f);
    fclose(f);
    text[n] = 0;
    return n;
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    char *resolved = resolve(argv[1]);
    if (!GetFullPathNameA(resolved ? resolved : argv[1], MAX_PATH, exe, NULL)) return 2;
    const char *tmp = getenv("TEST_TMPDIR");
    CHECK(tmp && *tmp);
    CHECK(SetEnvironmentVariableA("SDL_VIDEODRIVER", "dummy"));
    CHECK(SetEnvironmentVariableA("ES2SIM_MESSAGE_BOX", "none"));
    CHECK(run("--bogus") == 64 && run("--headless --config") == 64 && run("--tick-log") == 64);
    puts("PASS: usage 64");
    /* A synthetic installation directory: data\prefs.cfg with byte 6 = 0
     * (the GDI path) and data\drive.cfg, no script.dat. */
    char install[MAX_PATH], path[MAX_PATH], log[MAX_PATH], out[MAX_PATH], args[3 * MAX_PATH], text[16384];
    snprintf(install, sizeof install, "%s\\install", tmp);
    CreateDirectoryA(install, NULL);
    snprintf(path, sizeof path, "%s\\data", install);
    CreateDirectoryA(path, NULL);
    snprintf(path, sizeof path, "%s\\data\\prefs.cfg", install);
    FILE *f = fopen(path, "wb");
    CHECK(f && fwrite("\0\0\0\0\0\0\0", 1, 7, f) == 7 && !fclose(f));
    snprintf(path, sizeof path, "%s\\data\\drive.cfg", install);
    f = fopen(path, "wb");
    CHECK(f && fputs("C:\\CD\r\n", f) >= 0 && !fclose(f));
    /* The fatal report 0x479c78 called at 0x461524 (no script.dat) runs its
     * notify list, the mission start's clean-up 0x461304, then its box and
     * __exit(1): headless no box and exit code 1 (D, the headless process
     * record), windowed the "Error" box and exit code 1. */
    snprintf(log, sizeof log, "%s\\headless.jsonl", tmp);
    snprintf(args, sizeof args, "--headless --tick-log \"%s\" --arguments \"dummy -eggplant -X2 -R0 -G\"", log);
    long code = run_in(args, install, NULL);
    CHECK(code == 1);
    CHECK(slurp(log, text, sizeof text) && !strstr(text, "{\"event\":\"refused\"") && strstr(text, "[\"0x00461304\",1]") &&
          strstr(text, "{\"event\":\"exit\",\"reason\":\"fatal_error\",\"exit_code\":1,"));
    snprintf(log, sizeof log, "%s\\window.jsonl", tmp);
    snprintf(out, sizeof out, "%s\\window.out", tmp);
    snprintf(args, sizeof args, "--tick-log \"%s\" --arguments \"dummy -eggplant -X2 -R0 -G\"", log);
    CHECK(run_in(args, install, out) == 1);
    CHECK(slurp(log, text, sizeof text) && !strstr(text, "{\"event\":\"refused\"") && strstr(text, "[\"0x00461304\",1]"));
    CHECK(strstr(text, "{\"event\":\"exit\",\"reason\":\"fatal_error\",\"exit_code\":1"));
    CHECK(slurp(out, text, sizeof text));
    CHECK(strstr(text, "es2sim: windowed (SDL3): screen ") && strstr(text, "es2sim: window 640x480 at ") && strstr(text, "(renderer "));
    CHECK(strstr(text, "built the guest image from the own tables"));
    CHECK(strstr(text, "es2sim: MessageBoxA(0x0, \"") && strstr(text, "Line 222 in \"INIT.CPP\": data\\script.dat\", \"Error\", 0x0) = 1 (not shown)"));
    CHECK(strstr(text, "es2sim: __exit; ExitProcess(1) without exit records"));
    puts("PASS: the launcher's line without script.dat: headless exit code 1 at the fatal report's __exit (no box); windowed "
         "(dummy driver) the window after the DIB section, the \"Error\" box (line 222) and exit code 1");
    /* No data\drive.cfg: the fatal report, exit code 1 in both builds; windowed
     * the box goes to the process platform. */
    char bare[MAX_PATH];
    snprintf(bare, sizeof bare, "%s\\nodrive", tmp);
    CreateDirectoryA(bare, NULL);
    snprintf(path, sizeof path, "%s\\data", bare);
    CreateDirectoryA(path, NULL);
    snprintf(path, sizeof path, "%s\\data\\prefs.cfg", bare);   /* the GDI path, as above */
    f = fopen(path, "wb");
    CHECK(f && fwrite("\0\0\0\0\0\0\0", 1, 7, f) == 7 && !fclose(f));
    snprintf(log, sizeof log, "%s\\fatal-headless.jsonl", tmp);
    snprintf(out, sizeof out, "%s\\fatal-headless.out", tmp);
    snprintf(args, sizeof args, "--headless --tick-log \"%s\" --arguments \"dummy -eggplant -X2 -R0 -G\"", log);
    CHECK(run_in(args, bare, out) == 1);
    CHECK(slurp(out, text, sizeof text) && !strstr(text, "es2sim: MessageBoxA(") && strstr(text, "es2sim: __exit; ExitProcess(1) without exit records"));
    CHECK(slurp(log, text, sizeof text) && strstr(text, "{\"event\":\"exit\",\"reason\":\"fatal_error\",\"exit_code\":1"));
    snprintf(log, sizeof log, "%s\\fatal-window.jsonl", tmp);
    snprintf(out, sizeof out, "%s\\fatal-window.out", tmp);
    snprintf(args, sizeof args, "--tick-log \"%s\" --arguments \"dummy -eggplant -X2 -R0 -G\"", log);
    CHECK(run_in(args, bare, out) == 1);
    CHECK(slurp(out, text, sizeof text) && strstr(text, "es2sim: MessageBoxA(0x0, \"") && strstr(text, "\", \"Error\", 0x0) = 1 (not shown)"));
    CHECK(strstr(text, "data\\drive.cfg") && strstr(text, "es2sim: __exit; ExitProcess(1) without exit records"));
    CHECK(slurp(log, text, sizeof text) && strstr(text, "{\"event\":\"exit\",\"reason\":\"fatal_error\",\"exit_code\":1"));
    /* Without -eggplant: the eggplant box, MB_ICONEXCLAMATION, owner 0. */
    snprintf(out, sizeof out, "%s\\eggplant-window.out", tmp);
    snprintf(args, sizeof args, "--arguments \"dummy -X2 -R0 -G\"");
    run_in(args, install, out);
    CHECK(slurp(out, text, sizeof text) && strstr(text, "es2sim: MessageBoxA(0x0, \"") && strstr(text, ", 0x30) = 1 (not shown)"));
    puts("PASS: no drive.cfg ends with 1 headless (no box) and windowed (the fatal box, __exit(1)); the eggplant "
         "box with style 0x30");
    return 0;
}
