/* es2launch.exe starts es2shell.exe through ES2_LAUNCH_SHELL with
 * VSHELL's argv appended; the shell runs headless (no window), parses the
 * argv as VSHELL does, presents its test pattern once and ends with exit
 * code 0, which ends the launcher's loop. Synthetic installation directory
 * (own error.str and language.cfg), no game file.
 * Arguments: <es2launch.exe> <es2shell.exe>. */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures, checks;
#define CHECK(c) do { ++checks; if (!(c)) { ++failures; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static void write_file(const char *path, const void *data, size_t size) {
    FILE *f = fopen(path, "wb");
    if (!f) { printf("cannot write %s\n", path); exit(2); }
    fwrite(data, 1, size, f);
    fclose(f);
}

/* Own error.str: group 0 with one message, group 1 with one caption. */
static void write_strings(const char *path) {
    unsigned char data[64];
    size_t n = 4;
    data[n++] = 1; data[n++] = 0;
    data[n++] = 4; data[n++] = 0; memcpy(data + n, "msg", 4); n += 4; data[n++] = 0;
    data[n++] = 1; data[n++] = 0;
    data[n++] = 4; data[n++] = 0; memcpy(data + n, "cap", 4); n += 4; data[n++] = 0;
    uint32_t body = (uint32_t)(n - 4);
    memcpy(data, &body, 4);
    write_file(path, data, n);
}

/* Runfiles: the path itself, else the manifest's "_main/<rootpath> <path>" line. */
static int resolve(const char *rootpath, char *out) {
    if (GetFileAttributesA(rootpath) != INVALID_FILE_ATTRIBUTES) return GetFullPathNameA(rootpath, MAX_PATH, out, NULL) != 0;
    const char *manifest = getenv("RUNFILES_MANIFEST_FILE");
    FILE *f = manifest ? fopen(manifest, "r") : NULL;
    char line[2 * MAX_PATH], key[MAX_PATH];
    snprintf(key, sizeof key, "_main/%s ", rootpath);
    int found = 0;
    while (f && !found && fgets(line, sizeof line, f)) {
        if (strncmp(line, key, strlen(key))) continue;
        char *path = line + strlen(key);
        path[strcspn(path, "\r\n")] = 0;
        for (char *c = path; *c; ++c) if (*c == '/') *c = '\\';
        found = GetFullPathNameA(path, MAX_PATH, out, NULL) != 0;
    }
    if (f) fclose(f);
    return found;
}

int main(int argc, char **argv) {
    if (argc < 3) { printf("usage: <es2launch.exe> <es2shell.exe>\n"); return 2; }
    char launcher[MAX_PATH], shell[MAX_PATH], root[MAX_PATH], path[MAX_PATH], report[MAX_PATH];
    if (!resolve(argv[1], launcher) || !resolve(argv[2], shell)) {
        printf("cannot resolve the programs\n");
        return 2;
    }
    char temp[MAX_PATH];
    const char *tmp = getenv("TEST_TMPDIR");
    if (!tmp || !*tmp) {
        GetTempPathA(MAX_PATH, temp);
        tmp = temp;
    }
    snprintf(root, sizeof root, "%s\\gui_launch_%lu", tmp, (unsigned long)GetCurrentProcessId());
    CreateDirectoryA(root, NULL);
    snprintf(path, sizeof path, "%s\\data", root);
    CreateDirectoryA(path, NULL);
    snprintf(path, sizeof path, "%s\\data\\language.cfg", root);
    write_file(path, "G", 1);
    snprintf(path, sizeof path, "%s\\error.str", root);
    write_strings(path);
    snprintf(report, sizeof report, "%s\\shell.txt", root);

    char prefix[3 * MAX_PATH], log_path[MAX_PATH], command[2 * MAX_PATH];
    snprintf(prefix, sizeof prefix, "\"%s\" --headless --report \"%s\"", shell, report);
    snprintf(log_path, sizeof log_path, "%s\\launch.log", root);
    SetEnvironmentVariableA("ES2_LAUNCH_SHELL", prefix);
    SetEnvironmentVariableA("ES2_LAUNCH_LOG", log_path);
    SetEnvironmentVariableA("ES2_LAUNCH_NO_UI", "1");
    snprintf(command, sizeof command, "\"%s\" -s", launcher);
    STARTUPINFOA startup;
    memset(&startup, 0, sizeof startup);
    startup.cb = sizeof startup;
    PROCESS_INFORMATION process;
    /* window-lint: allow gui-child es2launch.exe (-mwindows, launcher/BUILD.bazel): a GUI-subsystem program gets no console */
    if (!CreateProcessA(launcher, command, NULL, NULL, FALSE, 0, NULL, root, &startup, &process)) {
        printf("cannot start launcher %s in %s (%lu)\n", launcher, root, GetLastError());
        return 2;
    }
    CloseHandle(process.hThread);
    if (WaitForSingleObject(process.hProcess, 60000) != WAIT_OBJECT_0) {
        TerminateProcess(process.hProcess, 1);
        printf("launcher timed out\n");
        return 2;
    }
    DWORD code = 99;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hProcess);
    CHECK(code == 0);

    char text[16384];
    FILE *f = fopen(report, "rb");
    size_t n = f ? fread(text, 1, sizeof text - 1, f) : 0;
    if (f) fclose(f);
    text[n] = 0;
    CHECK(n > 0);
    CHECK(!strncmp(text, "argv dummy -eggplant -X1 -s -G\n", 31));
    CHECK(strstr(text, "\nargs entry=1 language=2 eggplant=1 movies=1 sound=0 keyboard=1 cheat=0\n") != NULL);
    CHECK(strstr(text, "\npresent 1 pass=1 kind=dirty rects=1 ") != NULL);
    CHECK(strstr(text, "\nend passes=1 presents=1 quit=0\n") != NULL);
    f = fopen(log_path, "rb");
    n = f ? fread(text, 1, sizeof text - 1, f) : 0;
    if (f) fclose(f);
    text[n] = 0;
    CHECK(strstr(text, "end 0") != NULL);
    if (failures) printf("report/log:\n%s\n", text);
    printf("%d checks, %d failures\n", checks, failures);
    return failures != 0;
}
