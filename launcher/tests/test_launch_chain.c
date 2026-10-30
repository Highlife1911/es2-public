/* Runs es2launch.exe against synthetic stand-ins for vshell.EXE / dbsim.EXE in
 * a temporary installation directory and compares the recorded chain with the
 * contract in docs/reverse-engineering/launcher.md. No game file is used.
 * Arguments: <es2launch.exe> <stand_in_child.exe>. */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures, checks;
#define CHECK(c) do { ++checks; if (!(c)) { ++failures; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static char launcher[MAX_PATH], child[MAX_PATH], root[MAX_PATH];

static void write_file(const char *dir, const char *name, const void *data, size_t size) {
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%s\\%s", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) { printf("cannot write %s\n", path); exit(2); }
    fwrite(data, 1, size, f);
    fclose(f);
}

static size_t read_file(const char *dir, const char *name, char *out, size_t capacity) {
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%s\\%s", dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) { out[0] = 0; return 0; }
    size_t n = fread(out, 1, capacity - 1, f);
    fclose(f);
    out[n] = 0;
    return n;
}

/* Own synthetic error.str: group 0 with three messages, group 1 with a caption. */
static void write_strings(const char *dir) {
    static const char *g0[] = {"memory 0", "memory 1", "memory 2"};
    unsigned char data[256];
    size_t at = 4;
    data[at++] = 3; data[at++] = 0;
    for (int i = 0; i < 3; ++i) {
        size_t n = strlen(g0[i]) + 1;
        data[at++] = (unsigned char)n; data[at++] = 0;
        memcpy(data + at, g0[i], n); at += n;
        data[at++] = 0;
    }
    data[at++] = 1; data[at++] = 0;
    data[at++] = 8; data[at++] = 0;
    memcpy(data + at, "caption", 8); at += 8;
    data[at++] = 0;
    uint32_t body = (uint32_t)(at - 4);
    memcpy(data, &body, 4);
    write_file(dir, "error.str", data, at);
}

static void prepare(const char *name, const char *language, const char *codes, int with_strings, int with_dbsim) {
    char dir[MAX_PATH], path[MAX_PATH];
    snprintf(dir, sizeof dir, "%s\\%s", root, name);
    CreateDirectoryA(dir, NULL);
    snprintf(path, sizeof path, "%s\\data", dir);
    CreateDirectoryA(path, NULL);
    snprintf(path, sizeof path, "%s\\vshell.EXE", dir);
    CopyFileA(child, path, FALSE);
    if (with_dbsim) { snprintf(path, sizeof path, "%s\\dbsim.EXE", dir); CopyFileA(child, path, FALSE); }
    if (language) write_file(dir, "data\\language.cfg", language, strlen(language));
    if (with_strings) write_strings(dir);
    write_file(dir, "codes.txt", codes, strlen(codes));
}

/* Runs the launcher in <root>\<name> and returns its exit code. */
static DWORD run(const char *name, const char *options) {
    char dir[MAX_PATH], command[512], log_path[MAX_PATH];
    snprintf(dir, sizeof dir, "%s\\%s", root, name);
    snprintf(log_path, sizeof log_path, "%s\\launch.log", dir);
    snprintf(command, sizeof command, "\"%s\" %s", launcher, options);
    SetEnvironmentVariableA("ES2_LAUNCH_LOG", log_path);
    SetEnvironmentVariableA("ES2_LAUNCH_NO_UI", "1");
    STARTUPINFOA startup;
    memset(&startup, 0, sizeof startup);
    startup.cb = sizeof startup;
    PROCESS_INFORMATION process;
    /* window-lint: allow gui-child es2launch.exe (-mwindows, launcher/BUILD.bazel): a GUI-subsystem program gets no console */
    if (!CreateProcessA(launcher, command, NULL, NULL, FALSE, 0, NULL, dir, &startup, &process)) {
        printf("cannot start launcher (%lu)\n", GetLastError());
        exit(2);
    }
    CloseHandle(process.hThread);
    if (WaitForSingleObject(process.hProcess, 60000) != WAIT_OBJECT_0) {
        TerminateProcess(process.hProcess, 1);
        printf("launcher timed out in %s\n", name);
        exit(2);
    }
    DWORD code = 99;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hProcess);
    return code;
}

static void expect_chain(const char *name, const char *const *expected, int count) {
    char dir[MAX_PATH], text[8192];
    snprintf(dir, sizeof dir, "%s\\%s", root, name);
    read_file(dir, "chain.log", text, sizeof text);
    char *line = text;
    for (int i = 0; i < count; ++i) {
        char *end = strchr(line, '\n');
        CHECK(end != NULL);
        if (!end) return;
        *end = 0;
        if (end > line && end[-1] == '\r') end[-1] = 0;
        char want[MAX_PATH * 2];
        snprintf(want, sizeof want, "%s|%s", expected[i], dir);
        if (strcmp(line, want)) printf("%s step %d\n  got  %s\n  want %s\n", name, i, line, want);
        CHECK(!strcmp(line, want));
        line = end + 1;
    }
    CHECK(*line == 0); /* no further child */
}

static int not_large_address_aware(const char *path) {
    FILE *f = fopen(path, "rb");
    unsigned char h[1024];
    size_t n = f ? fread(h, 1, sizeof h, f) : 0;
    if (f) fclose(f);
    if (n < 0x40) return 0;
    uint32_t pe = h[0x3c] | h[0x3d] << 8 | h[0x3e] << 16 | (uint32_t)h[0x3f] << 24;
    if (pe + 24 > n) return 0;
    unsigned characteristics = h[pe + 22] | h[pe + 23] << 8;
    return !(characteristics & 0x20);
}

/* Bazel on Windows provides runfiles through a manifest ("<key> <path>" lines). */
static int resolve(const char *rootpath, char *out) {
    if (GetFileAttributesA(rootpath) != INVALID_FILE_ATTRIBUTES)
        return GetFullPathNameA(rootpath, MAX_PATH, out, NULL) != 0;
    const char *manifest = getenv("RUNFILES_MANIFEST_FILE");
    FILE *f = manifest ? fopen(manifest, "r") : NULL;
    char line[2 * MAX_PATH], key[MAX_PATH];
    snprintf(key, sizeof key, "_main/%s", rootpath);
    int found = 0;
    while (f && !found && fgets(line, sizeof line, f)) {
        size_t n = strlen(key);
        if (!strncmp(line, key, n) && line[n] == ' ') {
            char *path = line + n + 1;
            path[strcspn(path, "\r\n")] = 0;
            for (char *p = path; *p; ++p) if (*p == '/') *p = '\\';
            found = GetFullPathNameA(path, MAX_PATH, out, NULL) != 0;
        }
    }
    if (f) fclose(f);
    return found;
}

int main(int argc, char **argv) {
    if (argc != 3 || !resolve(argv[1], launcher) || !resolve(argv[2], child)) {
        printf("usage: test_launch_chain <es2launch.exe> <stand_in_child.exe>\n");
        return 2;
    }
    const char *tmp = getenv("TEST_TMPDIR");
    if (!tmp || !GetFullPathNameA(tmp, MAX_PATH, root, NULL)) GetTempPathA(MAX_PATH, root);
    size_t root_length = strlen(root);
    if (root_length && root[root_length - 1] == '\\') root[root_length - 1] = 0;

    CHECK(not_large_address_aware(launcher)); /* GlobalMemoryStatus must see the values ES.EXE sees */

    /* A: no options, German; shell -> sim -> shell -> demo sim -> shell -> quit. */
    prepare("a", "G", "2 1 5 3 0", 1, 1);
    CHECK(run("a", "") == 0);
    static const char *const a[] = {
        "vshell.EXE|dummy -eggplant -X1 -G",
        "dbsim.EXE|dummy -eggplant -X2 -R0 -G",
        "vshell.EXE|dummy -eggplant -X1 -G",
        "dbsim.EXE|dummy -eggplant -X5 -R1 -D -G",
        "vshell.EXE|dummy -eggplant -X3 -G",
    };
    expect_chain("a", a, 5);

    /* B: all pass-through options, English (no language argument), code 7 ends the loop. */
    prepare("b", "E", "2 7", 1, 1);
    CHECK(run("b", "-s -a -L -rabc -SPRUNKNOWN -rx -D5 ignored") == 0);
    static const char *const b[] = {
        "vshell.EXE|dummy -eggplant -X1 -s -a -@",
        "dbsim.EXE|dummy -eggplant -X2 -R0 -s -l -rabcrx -SPRUNKNOWN",
    };
    expect_chain("b", b, 2);

    /* C: forced simulator, no language.cfg; DBSIM is restarted until it returns 0. */
    prepare("c", NULL, "1 4 0", 1, 1);
    CHECK(run("c", "-SPRUNKNOWN -X") == 0);
    static const char *const c[] = {
        "dbsim.EXE|dummy -eggplant -X2 -R0 -SPRUNKNOWN",
        "dbsim.EXE|dummy -eggplant -X2 -R1 -SPRUNKNOWN",
        "dbsim.EXE|dummy -eggplant -X2 -R2 -SPRUNKNOWN",
    };
    expect_chain("c", c, 3);

    /* D: missing error.str starts nothing and still returns 0. */
    prepare("d", "G", "2", 0, 1);
    CHECK(run("d", "") == 0);
    expect_chain("d", NULL, 0);
    char text[4096], dir[MAX_PATH];
    snprintf(dir, sizeof dir, "%s\\d", root);
    read_file(dir, "launch.log", text, sizeof text);
    CHECK(strstr(text, " message ") != NULL);

    /* E: a missing dbsim.EXE ends the loop like a failed _spawnv (-1). */
    prepare("e", "G", "2", 1, 0);
    CHECK(run("e", "") == 0);
    static const char *const e[] = {"vshell.EXE|dummy -eggplant -X1 -G"};
    expect_chain("e", e, 1);
    snprintf(dir, sizeof dir, "%s\\e", root);
    read_file(dir, "launch.log", text, sizeof text);
    CHECK(strstr(text, "start_failed dbsim.EXE") != NULL && strstr(text, " end -1") != NULL);

    /* F: ES2_LAUNCH_SHELL replaces VSHELL; the VSHELL argv is appended
     * to the prefix. Code 2 starts DBSIM; the replacement's 1 ends the loop
     * (VSHELL's 1 would restart VSHELL). */
    prepare("f", "G", "2 3 2 1 1", 1, 1);
    snprintf(dir, sizeof dir, "%s\\f", root);
    char replacement[MAX_PATH], prefix[2 * MAX_PATH];
    snprintf(replacement, sizeof replacement, "%s\\shellstep.exe", dir);
    CopyFileA(child, replacement, FALSE);
    snprintf(prefix, sizeof prefix, "\"%s\" --state x", replacement);
    SetEnvironmentVariableA("ES2_LAUNCH_SHELL", prefix);
    CHECK(run("f", "-s") == 0);
    char f0[3 * MAX_PATH], f2[3 * MAX_PATH], f4[3 * MAX_PATH];
    snprintf(f0, sizeof f0, "shellstep.exe|%s dummy -eggplant -X1 -s -G", prefix);
    snprintf(f2, sizeof f2, "shellstep.exe|%s dummy -eggplant -X3 -s -G", prefix);
    snprintf(f4, sizeof f4, "shellstep.exe|%s dummy -eggplant -X1 -s -G", prefix);
    const char *const f[] = {
        f0, "dbsim.EXE|dummy -eggplant -X2 -R0 -s -G",
        f2, "dbsim.EXE|dummy -eggplant -X2 -R1 -s -G",
        f4,
    };
    expect_chain("f", f, 5);
    read_file(dir, "launch.log", text, sizeof text);
    CHECK(strstr(text, "exit shell-replacement 1") != NULL && strstr(text, " end 0") != NULL);
    CHECK(strstr(text, "vshell.EXE") == NULL);

    /* G: a replacement that cannot be started ends the loop (-1 -> 0). */
    prepare("g", "G", "2", 1, 1);
    SetEnvironmentVariableA("ES2_LAUNCH_SHELL", "\"Z:\\missing\\none.exe\" step");
    CHECK(run("g", "") == 0);
    expect_chain("g", NULL, 0);
    snprintf(dir, sizeof dir, "%s\\g", root);
    read_file(dir, "launch.log", text, sizeof text);
    CHECK(strstr(text, "start_failed shell-replacement") != NULL && strstr(text, " end 0") != NULL);
    SetEnvironmentVariableA("ES2_LAUNCH_SHELL", NULL);

    /* H: the replacement's 5 (VIEW DEMO) starts DBSIM with -D, as
     * ES.EXE does for VSHELL's 5; DBSIM's 6 starts the replacement with -X6. */
    prepare("h", "G", "5 6 0", 1, 1);
    snprintf(dir, sizeof dir, "%s\\h", root);
    snprintf(replacement, sizeof replacement, "%s\\shellstep.exe", dir);
    CopyFileA(child, replacement, FALSE);
    snprintf(prefix, sizeof prefix, "\"%s\"", replacement);
    SetEnvironmentVariableA("ES2_LAUNCH_SHELL", prefix);
    CHECK(run("h", "") == 0);
    char h0[3 * MAX_PATH], h2[3 * MAX_PATH];
    snprintf(h0, sizeof h0, "shellstep.exe|%s dummy -eggplant -X1 -G", prefix);
    snprintf(h2, sizeof h2, "shellstep.exe|%s dummy -eggplant -X6 -G", prefix);
    const char *const h[] = {h0, "dbsim.EXE|dummy -eggplant -X5 -R0 -D -G", h2};
    expect_chain("h", h, 3);
    read_file(dir, "launch.log", text, sizeof text);
    CHECK(strstr(text, "exit shell-replacement 5") != NULL && strstr(text, "exit dbsim.EXE 6") != NULL &&
          strstr(text, " end 0") != NULL);
    SetEnvironmentVariableA("ES2_LAUNCH_SHELL", NULL);

    /* I: the replacement starts without the launcher's __COMPAT_LAYER
     * (an own program converts its colours itself), DBSIM keeps it, and the
     * launcher's own value is unchanged for the next start. */
    prepare("i", "G", "2 3 1", 1, 1);
    snprintf(dir, sizeof dir, "%s\\i", root);
    snprintf(replacement, sizeof replacement, "%s\\shellstep.exe", dir);
    CopyFileA(child, replacement, FALSE);
    snprintf(prefix, sizeof prefix, "\"%s\"", replacement);
    SetEnvironmentVariableA("ES2_LAUNCH_SHELL", prefix);
    SetEnvironmentVariableA("__COMPAT_LAYER", "ES2TESTLAYER");
    CHECK(run("i", "") == 0);
    SetEnvironmentVariableA("__COMPAT_LAYER", NULL);
    SetEnvironmentVariableA("ES2_LAUNCH_SHELL", NULL);
    char i0[3 * MAX_PATH], i2[3 * MAX_PATH];
    snprintf(i0, sizeof i0, "shellstep.exe|%s dummy -eggplant -X1 -G", prefix);
    snprintf(i2, sizeof i2, "shellstep.exe|%s dummy -eggplant -X3 -G", prefix);
    const char *const i_chain[] = {i0, "dbsim.EXE|dummy -eggplant -X2 -R0 -G", i2};
    expect_chain("i", i_chain, 3);
    /* Windows' application compatibility rewrites the inherited value (an
     * unknown layer arrives as "ElevateCreateProcess"); what matters is that
     * DBSIM gets one and the replacement none. */
    read_file(dir, "layers.log", text, sizeof text);
    if (!strstr(text, "dbsim.EXE|") || strstr(text, "shellstep.exe|")) printf("layers.log: [%s]\n", text);
    CHECK(strstr(text, "dbsim.EXE|") != NULL);
    CHECK(strstr(text, "shellstep.exe|") == NULL);

    /* J: ES2_LAUNCH_SIM replaces DBSIM (none is installed); DBSIM's
     * argv without "dummy" follows as one --arguments value, the output goes
     * to ES2_LAUNCH_SIM_OUTPUT, the compatibility layer is left out, and
     * es2sim's own exit code 66 (CLAIM) ends the loop like any code above 6. */
    prepare("j", "G", "2 3 2 66", 1, 0);
    snprintf(dir, sizeof dir, "%s\\j", root);
    snprintf(replacement, sizeof replacement, "%s\\simstep.exe", dir);
    CopyFileA(child, replacement, FALSE);
    write_file(dir, "simstep.stdout", "", 0);
    snprintf(prefix, sizeof prefix, "\"%s\" --headless", replacement);
    char output[MAX_PATH];
    snprintf(output, sizeof output, "%s\\sim.out", dir);
    SetEnvironmentVariableA("ES2_LAUNCH_SIM", prefix);
    SetEnvironmentVariableA("ES2_LAUNCH_SIM_OUTPUT", output);
    SetEnvironmentVariableA("__COMPAT_LAYER", "ES2TESTLAYER");
    CHECK(run("j", "") == 0);
    SetEnvironmentVariableA("__COMPAT_LAYER", NULL);
    SetEnvironmentVariableA("ES2_LAUNCH_SIM_OUTPUT", NULL);
    char j1[3 * MAX_PATH], j3[3 * MAX_PATH];
    snprintf(j1, sizeof j1, "simstep.exe|%s --arguments \"-eggplant -X2 -R0 -G\"", prefix);
    snprintf(j3, sizeof j3, "simstep.exe|%s --arguments \"-eggplant -X2 -R1 -G\"", prefix);
    const char *const j[] = {"vshell.EXE|dummy -eggplant -X1 -G", j1, "vshell.EXE|dummy -eggplant -X3 -G", j3};
    expect_chain("j", j, 4);
    read_file(dir, "launch.log", text, sizeof text);
    CHECK(strstr(text, "exit sim-replacement 3") != NULL && strstr(text, "exit sim-replacement 66") != NULL &&
          strstr(text, " end 66") != NULL && strstr(text, "dbsim.EXE") == NULL);
    read_file(dir, "sim.out", text, sizeof text);
    CHECK(!strcmp(text, "simstep.exe output\r\nsimstep.exe output\r\n"));
    read_file(dir, "layers.log", text, sizeof text);
    if (!strstr(text, "vshell.EXE|") || strstr(text, "simstep.exe|")) printf("layers.log: [%s]\n", text);
    CHECK(strstr(text, "vshell.EXE|") != NULL && strstr(text, "simstep.exe|") == NULL);
    /* The removed value reaches es2sim as ES2SIM_COMPAT_LAYER (es2sim puts it
     * back into the guest's environment copy), the same one VSHELL (and
     * DBSIM) inherits; nothing else gets it. Windows rewrites the test's
     * layer on the way to the launcher, so the value is compared, not named. */
    char shown[256], carried[256];
    snprintf(shown, sizeof shown, "%s", strstr(text, "vshell.EXE|") ? strstr(text, "vshell.EXE|") + 11 : "");
    shown[strcspn(shown, "\r\n")] = 0;
    read_file(dir, "carried.log", text, sizeof text);
    snprintf(carried, sizeof carried, "%s", strstr(text, "simstep.exe|") ? strstr(text, "simstep.exe|") + 12 : "");
    carried[strcspn(carried, "\r\n")] = 0;
    if (!shown[0] || strcmp(shown, carried)) printf("layers [%s] carried [%s]\n", shown, carried);
    CHECK(shown[0] && !strcmp(shown, carried) && strstr(text, "vshell.EXE|") == NULL);
    CHECK(GetEnvironmentVariableA("ES2SIM_COMPAT_LAYER", NULL, 0) == 0);

    /* K: without the environment variable, es2launch.ini [sim] command of the
     * working directory names the replacement (configure-sim.ps1). The
     * command ends with a quote; configure-sim.ps1 writes it inside one more quote pair, which GetPrivateProfileString
     * strips. */
    SetEnvironmentVariableA("ES2_LAUNCH_SIM", NULL);
    prepare("k", "G", "2 0", 1, 0);
    snprintf(dir, sizeof dir, "%s\\k", root);
    snprintf(replacement, sizeof replacement, "%s\\simstep.exe", dir);
    CopyFileA(child, replacement, FALSE);
    snprintf(prefix, sizeof prefix, "\"%s\"", replacement);
    char ini[4 * MAX_PATH];
    snprintf(ini, sizeof ini, "[sim]\r\ncommand = \"%s\"\r\n", prefix);
    write_file(dir, "es2launch.ini", ini, strlen(ini));
    CHECK(run("k", "") == 0);
    char k1[3 * MAX_PATH];
    snprintf(k1, sizeof k1, "simstep.exe|%s --arguments \"-eggplant -X2 -R0 -G\"", prefix);
    const char *const k[] = {"vshell.EXE|dummy -eggplant -X1 -G", k1};
    expect_chain("k", k, 2);
    read_file(dir, "launch.log", text, sizeof text);
    CHECK(strstr(text, "exit sim-replacement 0") != NULL && strstr(text, " end 0") != NULL);

    /* L: the environment overrides the INI; M: a replacement that cannot be
     * started ends the loop like a failed DBSIM start (-1). */
    static const char broken[] = "[sim]\r\ncommand = \"\"Z:\\missing\\none.exe\"\"\r\n";
    prepare("l", "G", "2 0", 1, 0);
    snprintf(dir, sizeof dir, "%s\\l", root);
    snprintf(replacement, sizeof replacement, "%s\\simstep.exe", dir);
    CopyFileA(child, replacement, FALSE);
    write_file(dir, "es2launch.ini", broken, strlen(broken));
    snprintf(prefix, sizeof prefix, "\"%s\"", replacement);
    SetEnvironmentVariableA("ES2_LAUNCH_SIM", prefix);
    CHECK(run("l", "") == 0);
    SetEnvironmentVariableA("ES2_LAUNCH_SIM", NULL);
    char l1[3 * MAX_PATH];
    snprintf(l1, sizeof l1, "simstep.exe|%s --arguments \"-eggplant -X2 -R0 -G\"", prefix);
    const char *const l_chain[] = {"vshell.EXE|dummy -eggplant -X1 -G", l1};
    expect_chain("l", l_chain, 2);
    prepare("m", "G", "2 0", 1, 1);
    snprintf(dir, sizeof dir, "%s\\m", root);
    write_file(dir, "es2launch.ini", broken, strlen(broken));
    CHECK(run("m", "") == 0);
    const char *const m[] = {"vshell.EXE|dummy -eggplant -X1 -G"};
    expect_chain("m", m, 1);
    read_file(dir, "launch.log", text, sizeof text);
    CHECK(strstr(text, "start_failed sim-replacement") != NULL && strstr(text, " end -1") != NULL &&
          strstr(text, "dbsim.EXE") == NULL);

    printf("%d checks, %d failures\n", checks, failures);
    return failures != 0;
}
