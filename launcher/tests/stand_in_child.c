/* Synthetic stand-in for vshell.EXE and dbsim.EXE in the launcher chain test.
 * Appends "<own file name>|<command line>|<current directory>" to chain.log
 * and returns the next exit code from codes.txt (one per line), keeping its
 * position in position.txt. Everything lives in the current directory.
 * If "<own name without extension>.delegate" exists (e.g.
 * dbsim.delegate), its first line is a command line that is run instead of
 * reading codes.txt; its exit code is returned (the shell chain test uses a
 * Python stand-in for DBSIM this way).
 * If "<own name without extension>.stdout" exists, one line
 * "<own name> output" goes to the standard output handle it was given (the
 * launcher's ES2_LAUNCH_SIM_OUTPUT). */
#include <windows.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    char self[MAX_PATH], cwd[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, self, MAX_PATH);
    if (!n || n >= MAX_PATH || !GetCurrentDirectoryA(MAX_PATH, cwd)) return 99;
    const char *name = strrchr(self, '\\') ? strrchr(self, '\\') + 1 : self;
    FILE *log = fopen("chain.log", "a");
    if (!log) return 98;
    fprintf(log, "%s|%s|%s\n", name, GetCommandLineA(), cwd);
    fclose(log);
    /* The compatibility layer this start inherited, in layers.log (case I). */
    char layer[256];
    DWORD layer_length = GetEnvironmentVariableA("__COMPAT_LAYER", layer, sizeof layer);
    if (layer_length && layer_length < sizeof layer) {
        FILE *layers = fopen("layers.log", "a");
        if (layers) { fprintf(layers, "%s|%s\n", name, layer); fclose(layers); }
    }

    /* The layer es2launch hands a sim replacement on, in carried.log (case J). */
    DWORD carried_length = GetEnvironmentVariableA("ES2SIM_COMPAT_LAYER", layer, sizeof layer);
    if (carried_length && carried_length < sizeof layer) {
        FILE *carried = fopen("carried.log", "a");
        if (carried) { fprintf(carried, "%s|%s\n", name, layer); fclose(carried); }
    }

    char delegate[MAX_PATH], command[4096];
    int base = (int)(strrchr(name, '.') ? strrchr(name, '.') - name : (ptrdiff_t)strlen(name));
    snprintf(delegate, sizeof delegate, "%.*s.stdout", base, name);
    if (GetFileAttributesA(delegate) != INVALID_FILE_ATTRIBUTES) {
        HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD written;
        snprintf(command, sizeof command, "%s output\r\n", name);
        if (out && out != INVALID_HANDLE_VALUE) WriteFile(out, command, (DWORD)strlen(command), &written, NULL);
    }
    snprintf(delegate, sizeof delegate, "%.*s.delegate", base, name);
    FILE *d = fopen(delegate, "r");
    if (d) {
        if (!fgets(command, sizeof command, d)) command[0] = 0;
        fclose(d);
        command[strcspn(command, "\r\n")] = 0;
        STARTUPINFOA startup;
        memset(&startup, 0, sizeof startup);
        startup.cb = sizeof startup;
        PROCESS_INFORMATION process;
        /* This stand-in is a GUI program without a console; a console
         * delegate (the Python stand-in) must not open a visible one. */
        if (!CreateProcessA(NULL, command, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &startup, &process)) return 97;
        CloseHandle(process.hThread);
        WaitForSingleObject(process.hProcess, INFINITE);
        DWORD exit_code = 96;
        GetExitCodeProcess(process.hProcess, &exit_code);
        CloseHandle(process.hProcess);
        return (int)exit_code;
    }

    int position = 0, code = 0;
    FILE *f = fopen("position.txt", "r");
    if (f) { if (fscanf(f, "%d", &position) != 1) position = 0; fclose(f); }
    f = fopen("codes.txt", "r");
    if (f) {
        for (int i = 0; i <= position; ++i)
            if (fscanf(f, "%d", &code) != 1) { code = 0; break; }
        fclose(f);
    }
    f = fopen("position.txt", "w");
    if (f) { fprintf(f, "%d", position + 1); fclose(f); }
    return code;
}
