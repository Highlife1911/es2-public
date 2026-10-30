/* es2launch.exe: our replacement for ES.EXE. Win32 glue around the
 * portable decision logic; contract in docs/reverse-engineering/launcher.md.
 *
 * Own additions are environment variables (and es2launch.ini) only,
 * so the command line keeps exactly the original meaning:
 *   ES2_LAUNCH_LOG     append one line per decision, start and exit
 *   ES2_LAUNCH_SCRIPT  dry run: comma-separated exit codes, nothing started
 *   ES2_LAUNCH_NO_UI   write messages to the log instead of a message box
 *   ES2_LAUNCH_SHELL   command prefix of a shell replacement started instead
 *                      of VSHELL (campaign chain, shell/tools/es2chain.py);
 *                      the VSHELL argv is appended, exit code 2 starts DBSIM,
 *                      5 starts DBSIM -D, any other code ends the loop
 *   ES2_LAUNCH_SHELL_CONSOLE  nonempty and not "0": the replacement gets a
 *                      console window of its own (the text menu
 *                      es2menu.exe); otherwise it runs without a window
 *   ES2_LAUNCH_SIM     command prefix of the own DBSIM process (es2sim)
 *                      started instead of DBSIM.EXE; ` --arguments "<DBSIM argv
 *                      without dummy>"` is appended and its exit code is used
 *                      like DBSIM's (es2sim's own codes 64..71 end the loop).
 *                      Unset or empty: [sim] command of es2launch.ini in the
 *                      working directory (configure-sim.ps1 writes it)
 *   ES2_LAUNCH_SIM_OUTPUT  (else es2launch.ini [sim] output): es2sim's
 *                      stdout and stderr are appended to this file */
#include "launch_logic.h"
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static FILE *log_file;
static const char *script;
static const char *shell_replacement;
static bool shell_console;
static const char *sim_replacement, *sim_output;
static char ini_command[2048], ini_output[MAX_PATH];

static void log_line(const char *format, ...) {
    if (!log_file) return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    fprintf(log_file, "%02u:%02u:%02u.%03u ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list args;
    va_start(args, format);
    vfprintf(log_file, format, args);
    va_end(args);
    fputc('\n', log_file);
    fflush(log_file);
}

static void message(const char *text, const char *caption) {
    log_line("message %s", text ? text : "");
    if (!getenv("ES2_LAUNCH_NO_UI")) MessageBoxA(NULL, text, caption, MB_ICONEXCLAMATION);
}

static int language_byte(void) {
    FILE *f = fopen("data\\language.cfg", "rb");
    if (!f) return -1;
    int c = fgetc(f);
    fclose(f);
    return c == EOF ? -1 : c;
}

static uint8_t *read_file(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    uint8_t *data = NULL;
    long length;
    if (!fseek(f, 0, SEEK_END) && (length = ftell(f)) >= 0 && !fseek(f, 0, SEEK_SET) &&
        (data = malloc(length ? (size_t)length : 1)) && fread(data, 1, (size_t)length, f) != (size_t)length) {
        free(data);
        data = NULL;
    }
    fclose(f);
    *size = data ? (size_t)length : 0;
    return data;
}

static bool memory_ok(const es2l_strings *strings) {
    uint32_t size = 20000000u;
    HGLOBAL block;
    while (!(block = GlobalAlloc(GMEM_FIXED, size))) size -= 200000u;
    MEMORYSTATUS status;
    memset(&status, 0, sizeof status);
    status.dwLength = sizeof status;
    GlobalMemoryStatus(&status);
    GlobalFree(block);
    int index = es2l_memory_check((uint32_t)status.dwTotalPhys, (uint32_t)status.dwTotalPageFile,
                                  (uint32_t)status.dwAvailPageFile, size);
    if (index < 0) return true;
    message(strings->group0[index], strings->group1[0]);
    return false;
}

/* Appends es2sim's output: an inheritable handle, or INVALID_HANDLE_VALUE. */
static HANDLE output_handle(void) {
    SECURITY_ATTRIBUTES inherit = {sizeof inherit, NULL, TRUE};
    HANDLE h = CreateFileA(sim_output, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) log_line("output_failed sim-replacement %ld", (long)GetLastError());
    return h;
}

/* Exit code of the child, or -1 like a failed _spawnv. */
static int32_t run(const es2l_launch *launch) {
    char command[4096], program[32];
    bool shell = launch->program == ES2L_SHELL && shell_replacement;
    bool sim = launch->program == ES2L_SIM && sim_replacement;
    bool replacement = shell || sim;
    if (shell) snprintf(program, sizeof program, "shell-replacement");
    else if (sim) snprintf(program, sizeof program, "sim-replacement");
    else snprintf(program, sizeof program, "%s.EXE", launch->name);
    if (shell ? !es2l_replacement_command(shell_replacement, launch, command, sizeof command)
        : sim ? !es2l_sim_command(sim_replacement, launch, command, sizeof command)
              : !es2l_command_line(launch, command, sizeof command)) {
        if (sim) log_line("start_failed %s command", program);
        return -1;
    }
    log_line("start %s %s", program, command);
    if (script) {
        char *end;
        long code = strtol(script, &end, 10);
        if (end == script) code = 0;
        script = *end == ',' ? end + 1 : end;
        log_line("exit %s %ld (scripted)", program, code);
        return (int32_t)code;
    }
    STARTUPINFOA startup;
    memset(&startup, 0, sizeof startup);
    startup.cb = sizeof startup;
    PROCESS_INFORMATION process;
    /* The replacements are console programs; they get no window of their own
     * unless the shell is the interactive menu (ES2_LAUNCH_SHELL_CONSOLE).
     * es2sim opens its game window itself (SDL3). */
    DWORD flags = !replacement ? 0 : shell && shell_console ? CREATE_NEW_CONSOLE : CREATE_NO_WINDOW;
    HANDLE output = sim && sim_output ? output_handle() : INVALID_HANDLE_VALUE;
    if (output != INVALID_HANDLE_VALUE) {
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = NULL;
        startup.hStdOutput = startup.hStdError = output;
    }
    /* A replacement is an own program that converts its 8-bit frame to the
     * desktop's colours itself (es2shell, es2sim); the start scripts'
     * __COMPAT_LAYER (256COLOR, for VSHELL and DBSIM) would make Windows
     * quantise that true-colour output to a 256-colour palette. It is left
     * out of the replacement's environment only; VSHELL and DBSIM keep it.
     * DBSIM's environment block holds it, though, and es2sim copies its
     * environment into the guest's heap (rtl_imports.md, "The environment
     * copy"): es2sim gets the value as ES2SIM_COMPAT_LAYER and puts it back
     * into the guest's copy only. */
    char layer[1024];
    DWORD kept = replacement ? GetEnvironmentVariableA("__COMPAT_LAYER", layer, sizeof layer) : 0;
    if (kept >= sizeof layer) kept = 0;   /* too long to restore: left as it is */
    if (kept) SetEnvironmentVariableA("__COMPAT_LAYER", NULL);
    if (kept && sim) SetEnvironmentVariableA("ES2SIM_COMPAT_LAYER", layer);
    /* window-lint: allow gui-child VSHELL.EXE/DBSIM.EXE (flags 0); the console replacements get CREATE_NO_WINDOW or CREATE_NEW_CONSOLE above */
    BOOL started = CreateProcessA(replacement ? NULL : program, command, NULL, NULL, output != INVALID_HANDLE_VALUE,
                                  flags, NULL, NULL, &startup, &process);
    DWORD error = started ? 0 : GetLastError();
    if (output != INVALID_HANDLE_VALUE) CloseHandle(output);
    if (kept) SetEnvironmentVariableA("__COMPAT_LAYER", layer);
    if (kept && sim) SetEnvironmentVariableA("ES2SIM_COMPAT_LAYER", NULL);
    if (!started) {
        log_line("start_failed %s %ld", program, (long)error);
        return -1;
    }
    CloseHandle(process.hThread);
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD code = 0;
    if (!GetExitCodeProcess(process.hProcess, &code)) code = (DWORD)-1;
    CloseHandle(process.hProcess);
    log_line("exit %s %ld", program, (long)(int32_t)code);
    return (int32_t)code;
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command_line, int show) {
    (void)instance; (void)previous; (void)show;
    const char *log_path = getenv("ES2_LAUNCH_LOG");
    /* "N": not inherited by a sim replacement that inherits its output handle. */
    if (log_path && *log_path) log_file = fopen(log_path, "aN");
    script = getenv("ES2_LAUNCH_SCRIPT");
    shell_replacement = getenv("ES2_LAUNCH_SHELL");
    if (shell_replacement && !*shell_replacement) shell_replacement = NULL;
    shell_console = es2l_replacement_console(getenv("ES2_LAUNCH_SHELL_CONSOLE"));
    /* es2launch.ini of the working directory (the installation). */
    char ini_path[MAX_PATH];
    DWORD ini_length = GetFullPathNameA("es2launch.ini", sizeof ini_path, ini_path, NULL);
    if (ini_length && ini_length < sizeof ini_path) {
        GetPrivateProfileStringA("sim", "command", "", ini_command, sizeof ini_command, ini_path);
        GetPrivateProfileStringA("sim", "output", "", ini_output, sizeof ini_output, ini_path);
    }
    sim_replacement = es2l_sim_prefix(getenv("ES2_LAUNCH_SIM"), ini_command);
    sim_output = es2l_sim_prefix(getenv("ES2_LAUNCH_SIM_OUTPUT"), ini_output);
    log_line("launcher %s", command_line ? command_line : "");

    es2l_options options;
    es2l_parse(command_line, &options);
    char language = es2l_language(language_byte());

    size_t size = 0;
    uint8_t *data = read_file("error.str", &size);
    es2l_strings strings;
    if (!data || !es2l_parse_strings(data, size, &strings)) {
        message("error.str could not be loaded.", "Error");
        free(data);
        if (log_file) fclose(log_file);
        return 0;
    }

    int32_t state = memory_ok(&strings) ? 1 : 0, counter = 0;
    while (state != 0) {
        es2l_launch launch = es2l_next(state, &options, language, &counter);
        if (launch.program == ES2L_QUIT) break;
        state = run(&launch);
        if (launch.program == ES2L_SHELL && shell_replacement) state = es2l_after_replacement(state);
    }
    log_line("end %ld", (long)state);
    free(data);
    if (log_file) fclose(log_file);
    return 0;
}
