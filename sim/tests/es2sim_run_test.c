/* es2sim.exe as a process (argv[1]: its runfiles path): usage refused with 64;
 * no DBSIM.EXE is read and no game data is needed; `--headless` in a synthetic
 * installation directory: without
 * data\drive.cfg the fatal report (exit 1, assert.log, no exit records),
 * without -eggplant the refusal (WinMain returns 0, exit records), with the
 * launcher's line the fatal report of the launch peek without script.dat
 * (exit 1); the tick log's
 * start record, the VxD open of init record 64, the message box and the exit
 * records. Every child starts with CREATE_NO_WINDOW. */
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

/* Runs `es2sim <arguments>` and returns its exit code (-1 on a start
 * failure); with `output`, stdout and stderr go to that file. */
static long run_to(const char *arguments, const char *directory, const char *output) {
    char command[4096];
    if (snprintf(command, sizeof command, "\"%s\" %s", exe, arguments) >= (int)sizeof command) return -1;
    STARTUPINFOA si; PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof si); si.cb = sizeof si;
    HANDLE out = INVALID_HANDLE_VALUE;
    if (output) {
        SECURITY_ATTRIBUTES sa = {sizeof sa, NULL, TRUE};
        out = CreateFileA(output, GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (out == INVALID_HANDLE_VALUE) return -1;
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        si.hStdOutput = si.hStdError = out;
    }
    BOOL started = CreateProcessA(exe, command, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, directory, &si, &pi);
    if (out != INVALID_HANDLE_VALUE) CloseHandle(out);
    if (!started) return -1;
    DWORD code = (DWORD)-1;
    WaitForSingleObject(pi.hProcess, 120000);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return (long)code;
}
static long run_in(const char *arguments, const char *directory) { return run_to(arguments, directory, NULL); }
static long run(const char *arguments) { return run_in(arguments, NULL); }

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    char *resolved = resolve(argv[1]);
    if (!GetFullPathNameA(resolved ? resolved : argv[1], MAX_PATH, exe, NULL)) return 2;
    const char *tmp = getenv("TEST_TMPDIR");
    CHECK(tmp && *tmp);
    CHECK(run("") == 64 && run("--headless --config") == 64 && run("--bogus") == 64);
    puts("PASS: usage 64");
    /* es2sim runs in an installation directory (the working directory) with
     * DBSIM's command line from --arguments: the launcher contract's start-up
     * paths and their exit codes. */
    char install[MAX_PATH], drive[MAX_PATH], log[MAX_PATH], output[MAX_PATH], args[3 * MAX_PATH], text[16384];
    snprintf(install, sizeof install, "%s\\install", tmp);
    CreateDirectoryA(install, NULL);
    /* data\prefs.cfg with byte 6 = 0: without it init record 46's display
     * flag [0x4d255e] = 1 stands and WinMain asks for DirectDraw (case 4). */
    snprintf(drive, sizeof drive, "%s\\data", install);
    CreateDirectoryA(drive, NULL);
    snprintf(drive, sizeof drive, "%s\\data\\prefs.cfg", install);
    FILE *f = fopen(drive, "wb");
    CHECK(f && fwrite("\0\0\0\0\0\0\0", 1, 7, f) == 7 && !fclose(f));
    /* 1. No data\drive.cfg: the fatal report, exit 1 without the exit
     * records, assert.log written in the installation. */
    snprintf(log, sizeof log, "%s\\fatal.jsonl", tmp);
    snprintf(output, sizeof output, "%s\\fatal.out", tmp);
    snprintf(args, sizeof args, "--headless --tick-log \"%s\" --arguments \"dummy -eggplant -X2 -R0 -G\"", log);
    long code = run_to(args, install, output);
    CHECK(code == 1);
    f = fopen(output, "rb");
    CHECK(f);
    size_t n = fread(text, 1, sizeof text - 1, f);
    fclose(f);
    text[n] = 0;
    CHECK(strstr(text, "built the guest image from the own tables"));
    f = fopen(log, "rb");
    CHECK(f);
    n = fread(text, 1, sizeof text - 1, f);
    fclose(f);
    text[n] = 0;
    CHECK(!strncmp(text, "{\"event\":\"start\",\"schema\":2,\"mode\":\"own_process_headless\"", 57));
    /* The c0w32 entry and the init records ran (the VxD open logged). */
    CHECK(strstr(text, "{\"event\":\"platform\",\"call\":\"CreateFileA\",\"name\":\"\\\\\\\\.\\\\DARKMONO.VXD\",\"result\":\"0xffffffff\"}"));
    CHECK(strstr(text, "{\"event\":\"platform\",\"call\":\"MessageBoxA\""));
    CHECK(strstr(text, "{\"event\":\"exit\",\"reason\":\"fatal_error\",\"exit_code\":1,\"frames\":0"));
    CHECK(strstr(text, "\"imports\":134") && strstr(text, "\"refusals\":0"));
    snprintf(drive, sizeof drive, "%s\\assert.log", install);
    CHECK(GetFileAttributesA(drive) != INVALID_FILE_ATTRIBUTES);
    /* The log is never overwritten (CREATE_NEW): a second run refuses its set-up. */
    CHECK(run_in(args, install) == 67);
    /* 2. With data\drive.cfg, without error.str and VOL\, no -eggplant: the
     * refusal's message box, WinMain returns 0, the exit records run. */
    snprintf(drive, sizeof drive, "%s\\data", install);
    CreateDirectoryA(drive, NULL);
    snprintf(drive, sizeof drive, "%s\\data\\drive.cfg", install);
    f = fopen(drive, "wb");
    CHECK(f && fputs("C:\\CD\r\n", f) >= 0 && !fclose(f));
    snprintf(log, sizeof log, "%s\\refusal.jsonl", tmp);
    snprintf(args, sizeof args, "--headless --tick-log \"%s\" --arguments \"dummy -X3 -R0 -G\"", log);
    CHECK(run_in(args, install) == 0);
    f = fopen(log, "rb");
    CHECK(f);
    n = fread(text, 1, sizeof text - 1, f);
    fclose(f);
    text[n] = 0;
    CHECK(strstr(text, "{\"event\":\"exit\",\"reason\":\"winmain_returned\",\"exit_code\":0,\"frames\":0"));
    /* 3. The launcher's line with -eggplant: the start-up builds the DIB
     * section's objects, calls the mission load (hook mission_root), runs the
     * input set-up 0x45a5dc and the window rectangles, the graphics context and
     * frame clear, and stops in the launch peek 0x4614c0: this installation has
     * no data\script.dat, the original's fatal report 0x479c78 is called at
     * 0x461524, runs its notify list (the mission start's clean-up 0x461304),
     * writes assert.log and ends the process at its __exit 0x49679c with 1 (D:
     * the headless process record, no box). */
    snprintf(log, sizeof log, "%s\\load.jsonl", tmp);
    snprintf(args, sizeof args, "--headless --tick-log \"%s\" --arguments \"dummy -eggplant -X2 -R0 -G\"", log);
    CHECK(run_in(args, install) == 1);
    f = fopen(log, "rb");
    CHECK(f);
    n = fread(text, 1, sizeof text - 1, f);
    fclose(f);
    text[n] = 0;
    CHECK(!strstr(text, "{\"event\":\"refused\"") &&
          !strstr(text, "\"va\":\"0x00461304\"") && strstr(text, "[\"0x00461304\",1]") &&
          !strstr(text, "0x00461524") && !strstr(text, "\"va\":\"0x00461294\"") && !strstr(text, "\"va\":\"0x0045a5dc\"") && !strstr(text, "0x00464d44"));
    CHECK(strstr(text, "{\"event\":\"exit\",\"reason\":\"fatal_error\",\"exit_code\":1,"));
    snprintf(log, sizeof log, "%s\\assert.log", install);
    f = fopen(log, "rb");
    CHECK(f);
    n = fread(text, 1, sizeof text - 1, f);
    fclose(f);
    text[n] = 0;
    CHECK(strstr(text, "Line 222 in \"INIT.CPP\": data\\script.dat"));
    /* 4. The same with -Z1: WinMain's full-screen switch 0x4667c8 and the
     * DirectDraw frame clear run on the modelled surfaces (no window); the
     * run ends as in 3. */
    snprintf(log, sizeof log, "%s\\fullscreen.jsonl", tmp);
    snprintf(args, sizeof args, "--headless --tick-log \"%s\" --arguments \"dummy -eggplant -X2 -R0 -G -Z1\"", log);
    CHECK(run_in(args, install) == 1);
    f = fopen(log, "rb");
    CHECK(f);
    n = fread(text, 1, sizeof text - 1, f);
    fclose(f);
    text[n] = 0;
    CHECK(!strstr(text, "{\"event\":\"refused\"") && strstr(text, "{\"event\":\"exit\",\"reason\":\"fatal_error\",\"exit_code\":1,"));
    puts("PASS: es2sim --headless with the built-in image (no DBSIM.EXE): missing drive.cfg exits 1 with assert.log, the -eggplant "
         "refusal returns 0 after the exit records, the launcher's line runs the input set-up and calls the fatal report in the mission load's launch peek without script.dat (its notify callback 0x461304 runs, assert.log line 222, __exit ends with 1), and with -Z1 the full-screen switch runs headless to the same end");
    return 0;
}
