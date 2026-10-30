/* //media:iv32_bind fixture tests. One mode per cc_test (the binding
 * state is per process, and a refusal is sticky by contract):
 *
 *   events        status -> proxy log event names (pure)
 *   bad_argument  incomplete driver table: refused before any call, not sticky;
 *                 a missing wrapper then sticks even for a later system bind
 *   no_vfw        wrapper file present, msvfw32.dll not a loadable image
 *   no_exports    msvfw32.dll loads but has no ICOpen/ICClose/ICInstall
 *                 (argv[2]: the fixture DLL)
 *   bind          system bind, decompressor opens, release removes it, rebind;
 *                 Drivers32 vidc.IV32 unchanged in HKLM and HKCU (32-bit view)
 *   two_process   two own processes, each with its own binding, at the same time
 *   two_decode    the same with real IV32 frames (ES2_DATA_ROOT, read only)
 *
 * Children are this executable again (child-* modes), started with
 * CREATE_NO_WINDOW. Prints SKIP and exits 0 when the system has no
 * ir32_32.dll wrapper, when IV32 is already mapped system-wide, or (two_decode)
 * when ES2_DATA_ROOT is unset. */
#include "es2/media/iv32_bind.h"

#include <vfw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define IV32 mmioFOURCC('I', 'V', '3', '2')

static int failures;
#define CHECK(cond) do { if (!(cond)) { ++failures; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

/* Child exit codes. */
enum { CHILD_OPEN = 0, CHILD_NO_CODEC = 1, CHILD_NOT_BOUND = 2, CHILD_QUERY = 3, CHILD_DECODE = 4, CHILD_SETUP = 77 };

static es2_iv32_drivers drivers(void) {
    es2_iv32_drivers d = {0};
    if (!es2_iv32_system_drivers(&d)) { printf("FAIL: system winmm driver entry points\n"); exit(1); }
    return d;
}

static int wrapper_present(void) {
    WCHAR path[MAX_PATH];
    UINT n = GetSystemDirectoryW(path, MAX_PATH);
    if (!n || n + 13 >= MAX_PATH) return 0;
    wcscat(path, L"\\ir32_32.dll");
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

/* A synthetic IV32 source format: no codec data is needed for the query. */
static void iv32_format(BITMAPINFOHEADER *in) {
    memset(in, 0, sizeof(*in));
    in->biSize = sizeof(*in);
    in->biWidth = 320;
    in->biHeight = 240;
    in->biPlanes = 1;
    in->biBitCount = 24;
    in->biCompression = IV32;
}

/* Opens an IV32 decompressor; returns CHILD_OPEN / CHILD_NO_CODEC / CHILD_QUERY.
 * *held receives the open handle when held is not NULL. */
static int open_decompressor(HIC *held) {
    HIC codec = ICOpen(ICTYPE_VIDEO, IV32, ICMODE_DECOMPRESS);
    if (!codec) return CHILD_NO_CODEC;
    BITMAPINFOHEADER in;
    iv32_format(&in);
    LRESULT query = ICDecompressQuery(codec, &in, NULL);
    if (held && query == ICERR_OK) *held = codec; else ICClose(codec);
    return query == ICERR_OK ? CHILD_OPEN : CHILD_QUERY;
}

static int vidc_mapping(HKEY hive, char *value, DWORD size) {
    HKEY key;
    value[0] = 0;
    if (RegOpenKeyExA(hive, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Drivers32", 0, KEY_READ | KEY_WOW64_32KEY, &key) != ERROR_SUCCESS) return 0;
    DWORD type = 0;
    LONG r = RegQueryValueExA(key, "vidc.IV32", NULL, &type, (BYTE *)value, &size);
    RegCloseKey(key);
    return r == ERROR_SUCCESS;
}

/* ---------------------------------------------------------------- children */

static int child_main(const char *mode, const char *movie) {
    if (!strcmp(mode, "child-nobind")) return open_decompressor(NULL);
    es2_iv32_drivers d = drivers();
    if (es2_iv32_bind(NULL, &d) != ES2_IV32_BOUND) return CHILD_NOT_BOUND;
    if (!strcmp(mode, "child-bind")) return open_decompressor(NULL);
    if (strcmp(mode, "child-decode") || !movie) return CHILD_SETUP;
    int result = CHILD_DECODE;
    AVIFileInit();
    PAVIFILE file = NULL; PAVISTREAM stream = NULL; PGETFRAME frames = NULL;
    if (AVIFileOpenA(&file, movie, OF_READ | OF_SHARE_DENY_WRITE, NULL) != AVIERR_OK) { result = CHILD_SETUP; goto done; }
    if (AVIFileGetStream(file, &stream, streamtypeVIDEO, 0) != AVIERR_OK) { result = CHILD_SETUP; goto done; }
    frames = AVIStreamGetFrameOpen(stream, NULL);
    if (!frames) { result = CHILD_NO_CODEC; goto done; }
    LONG start = AVIStreamStart(stream), decoded = 0;
    for (LONG n = 0; n < 30; ++n) if (AVIStreamGetFrame(frames, start + n)) ++decoded;
    result = decoded == 30 ? CHILD_OPEN : CHILD_DECODE;
done:
    if (frames) AVIStreamGetFrameClose(frames);
    if (stream) AVIStreamRelease(stream);
    if (file) AVIFileRelease(file);
    AVIFileExit();
    return result;
}

static int run_child(const char *mode, const char *movie) {
    char exe[MAX_PATH], line[3 * MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, exe, MAX_PATH);
    if (!n || n >= MAX_PATH) return -1;
    snprintf(line, sizeof(line), "\"%s\" %s%s%s%s", exe, mode, movie ? " \"" : "", movie ? movie : "", movie ? "\"" : "");
    STARTUPINFOA startup = {0};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION child = {0};
    fflush(stdout);
    if (!CreateProcessA(exe, line, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &startup, &child)) return -1;
    DWORD wait = WaitForSingleObject(child.hProcess, 60000);
    DWORD code = (DWORD)-1;
    if (wait == WAIT_OBJECT_0) GetExitCodeProcess(child.hProcess, &code);
    else TerminateProcess(child.hProcess, 99);
    CloseHandle(child.hThread);
    CloseHandle(child.hProcess);
    return (int)code;
}

static const char *child_text(int code) {
    switch (code) {
    case CHILD_OPEN: return "open";
    case CHILD_NO_CODEC: return "no_codec";
    case CHILD_NOT_BOUND: return "not_bound";
    case CHILD_QUERY: return "query_refused";
    case CHILD_DECODE: return "decode_incomplete";
    case CHILD_SETUP: return "setup";
    default: return "error";
    }
}

static int report(const char *step, int code) {
    printf("{\"step\":\"%s\",\"child\":\"%s\",\"exit\":%d}\n", step, child_text(code), code);
    return code;
}

/* ---------------------------------------------------------------- modes */

static void make_dir(WCHAR *dir) {
    WCHAR base[MAX_PATH];
    const char *tmp = getenv("TEST_TMPDIR");
    if (tmp) MultiByteToWideChar(CP_ACP, 0, tmp, -1, base, MAX_PATH);
    else GetTempPathW(MAX_PATH, base);
    swprintf(dir, MAX_PATH, L"%ls\\iv32-%lu", base, (unsigned long)GetCurrentProcessId());
    CreateDirectoryW(dir, NULL);
}

static void write_file(const WCHAR *dir, const WCHAR *name, const char *text) {
    WCHAR path[MAX_PATH];
    swprintf(path, MAX_PATH, L"%ls\\%ls", dir, name);
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD written;
    if (f != INVALID_HANDLE_VALUE) { WriteFile(f, text, (DWORD)strlen(text), &written, NULL); CloseHandle(f); }
}

static void mode_events(void) {
    CHECK(!strcmp(es2_iv32_event(ES2_IV32_WRAPPER_MISSING), "system_indeo_missing"));
    CHECK(!strcmp(es2_iv32_event(ES2_IV32_VFW_MISSING), "system_vfw_missing"));
    CHECK(!strcmp(es2_iv32_event(ES2_IV32_VFW_EXPORTS_MISSING), "vfw_exports_missing"));
    CHECK(!strcmp(es2_iv32_event(ES2_IV32_EXISTING), "existing_indeo_available"));
    CHECK(!strcmp(es2_iv32_event(ES2_IV32_BOUND), "system_indeo_process_function_ready"));
    CHECK(!strcmp(es2_iv32_event(ES2_IV32_INSTALL_FAILED), "system_indeo_process_function_failed"));
    CHECK(es2_iv32_event(ES2_IV32_NOT_ATTEMPTED) == NULL);
    CHECK(es2_iv32_event(ES2_IV32_BAD_ARGUMENT) == NULL);
    CHECK(es2_iv32_event(ES2_IV32_VFW_PATH) == NULL);
    CHECK(!es2_iv32_bound());
    CHECK(!es2_iv32_release());
}

static void mode_bad_argument(void) {
    es2_iv32_drivers d = drivers(), partial = d;
    partial.driver_message = NULL;
    CHECK(es2_iv32_bind(NULL, NULL) == ES2_IV32_BAD_ARGUMENT);
    CHECK(es2_iv32_bind(NULL, &partial) == ES2_IV32_BAD_ARGUMENT);
    WCHAR dir[MAX_PATH];
    make_dir(dir);
    CHECK(es2_iv32_bind(dir, &d) == ES2_IV32_WRAPPER_MISSING);
    /* Sticky: the system directory is not tried until a release. */
    CHECK(es2_iv32_bind(NULL, &d) == ES2_IV32_WRAPPER_MISSING);
    CHECK(!es2_iv32_bound());
    CHECK(!es2_iv32_release());
}

static void mode_no_vfw(void) {
    es2_iv32_drivers d = drivers();
    WCHAR dir[MAX_PATH];
    make_dir(dir);
    write_file(dir, L"ir32_32.dll", "not a driver");
    write_file(dir, L"msvfw32.dll", "not an image");
    CHECK(es2_iv32_bind(dir, &d) == ES2_IV32_VFW_MISSING);
    CHECK(!es2_iv32_bound());
}

/* Bazel on Windows provides runfiles through a manifest ("<key> <path>" lines). */
static int resolve(const char *rootpath, char *out) {
    if (GetFileAttributesA(rootpath) != INVALID_FILE_ATTRIBUTES)
        return GetFullPathNameA(rootpath, MAX_PATH, out, NULL) != 0;
    const char *manifest = getenv("RUNFILES_MANIFEST_FILE");
    FILE *f = manifest ? fopen(manifest, "r") : NULL;
    char line[2 * MAX_PATH], key[MAX_PATH];
    snprintf(key, sizeof key, "_main/%s", rootpath);
    size_t n = strlen(key);
    int found = 0;
    while (f && !found && fgets(line, sizeof line, f)) {
        if (strncmp(line, key, n) || line[n] != ' ') continue;
        char *path = line + n + 1;
        path[strcspn(path, "\r\n")] = 0;
        for (char *p = path; *p; ++p) if (*p == '/') *p = '\\';
        found = GetFullPathNameA(path, MAX_PATH, out, NULL) != 0;
    }
    if (f) fclose(f);
    return found;
}

static void mode_no_exports(const char *fixture_dll) {
    es2_iv32_drivers d = drivers();
    WCHAR dir[MAX_PATH], source[MAX_PATH], target[MAX_PATH];
    char resolved[MAX_PATH];
    make_dir(dir);
    write_file(dir, L"ir32_32.dll", "not a driver");
    CHECK(resolve(fixture_dll, resolved));
    MultiByteToWideChar(CP_ACP, 0, resolved, -1, source, MAX_PATH);
    swprintf(target, MAX_PATH, L"%ls\\msvfw32.dll", dir);
    CHECK(CopyFileW(source, target, FALSE));
    CHECK(es2_iv32_bind(dir, &d) == ES2_IV32_VFW_EXPORTS_MISSING);
    CHECK(!es2_iv32_bound());
}

static int precondition(void) {
    if (!wrapper_present()) { printf("SKIP: no ir32_32.dll in the system directory\n"); return 0; }
    HIC existing = ICOpen(ICTYPE_VIDEO, IV32, ICMODE_DECOMPRESS);
    if (existing) { ICClose(existing); printf("SKIP: IV32 is mapped system-wide on this machine\n"); return 0; }
    return 1;
}

static void mode_bind(void) {
    char before_lm[512], before_cu[512], after_lm[512], after_cu[512];
    int had_lm = vidc_mapping(HKEY_LOCAL_MACHINE, before_lm, sizeof(before_lm));
    int had_cu = vidc_mapping(HKEY_CURRENT_USER, before_cu, sizeof(before_cu));
    es2_iv32_drivers d = drivers();
    CHECK(open_decompressor(NULL) == CHILD_NO_CODEC);
    CHECK(es2_iv32_bind(NULL, &d) == ES2_IV32_BOUND);
    CHECK(es2_iv32_bound());
    CHECK(es2_iv32_bind(NULL, &d) == ES2_IV32_BOUND); /* repeated: no second ICInstall */
    HIC held = NULL;
    CHECK(open_decompressor(&held) == CHILD_OPEN);
    CHECK(es2_iv32_release());
    CHECK(!es2_iv32_bound());
    CHECK(!es2_iv32_release());
    CHECK(open_decompressor(NULL) == CHILD_NO_CODEC);
    /* The decompressor opened before the release keeps its driver instance. */
    if (held) {
        BITMAPINFOHEADER in;
        iv32_format(&in);
        CHECK(ICDecompressQuery(held, &in, NULL) == ICERR_OK);
        ICClose(held);
    }
    CHECK(es2_iv32_bind(NULL, &d) == ES2_IV32_BOUND);
    CHECK(open_decompressor(NULL) == CHILD_OPEN);
    CHECK(es2_iv32_release());
    CHECK(vidc_mapping(HKEY_LOCAL_MACHINE, after_lm, sizeof(after_lm)) == had_lm);
    CHECK(vidc_mapping(HKEY_CURRENT_USER, after_cu, sizeof(after_cu)) == had_cu);
    CHECK(!had_lm || !strcmp(before_lm, after_lm));
    CHECK(!had_cu || !strcmp(before_cu, after_cu));
}

/* The measurement media_compatibility.md asks for: does a second process get
 * IV32 while a first one holds its binding (and an open decompressor)? */
static void mode_two_process(void) {
    es2_iv32_drivers d = drivers();
    CHECK(report("first_unbound/second_bind", run_child("child-bind", NULL)) == CHILD_OPEN);
    CHECK(es2_iv32_bind(NULL, &d) == ES2_IV32_BOUND);
    CHECK(report("first_bound/second_bind", run_child("child-bind", NULL)) == CHILD_OPEN);
    /* Isolation: a process without its own binding has no IV32. */
    CHECK(report("first_bound/second_nobind", run_child("child-nobind", NULL)) == CHILD_NO_CODEC);
    HIC held = NULL;
    CHECK(open_decompressor(&held) == CHILD_OPEN);
    CHECK(report("first_open/second_bind", run_child("child-bind", NULL)) == CHILD_OPEN);
    CHECK(report("first_open/second_nobind", run_child("child-nobind", NULL)) == CHILD_NO_CODEC);
    if (held) ICClose(held);
    CHECK(es2_iv32_release());
    CHECK(report("first_released/second_bind", run_child("child-bind", NULL)) == CHILD_OPEN);
}

static void mode_two_decode(void) {
    const char *root = getenv("ES2_DATA_ROOT");
    char movie[MAX_PATH], other[MAX_PATH];
    if (!root || !*root) { printf("SKIP: ES2_DATA_ROOT not set\n"); return; }
    snprintf(movie, sizeof(movie), "%s\\AVI\\INTR_PT1.AVI", root);
    snprintf(other, sizeof(other), "%s\\AVG\\INTR_PT1.AVI", root);
    if (GetFileAttributesA(movie) == INVALID_FILE_ATTRIBUTES || GetFileAttributesA(other) == INVALID_FILE_ATTRIBUTES) {
        printf("SKIP: intro movies not under ES2_DATA_ROOT\n"); return;
    }
    es2_iv32_drivers d = drivers();
    CHECK(es2_iv32_bind(NULL, &d) == ES2_IV32_BOUND);
    AVIFileInit();
    PAVIFILE file = NULL; PAVISTREAM stream = NULL; PGETFRAME frames = NULL;
    CHECK(AVIFileOpenA(&file, movie, OF_READ | OF_SHARE_DENY_WRITE, NULL) == AVIERR_OK);
    if (file) CHECK(AVIFileGetStream(file, &stream, streamtypeVIDEO, 0) == AVIERR_OK);
    if (stream) frames = AVIStreamGetFrameOpen(stream, NULL);
    CHECK(frames != NULL);
    LONG decoded = 0, start = stream ? AVIStreamStart(stream) : 0;
    if (frames) for (LONG n = 0; n < 10; ++n) if (AVIStreamGetFrame(frames, start + n)) ++decoded;
    CHECK(decoded == 10);
    /* While this process decodes the English intro, a second one decodes the
     * German one, and one decodes the same file. */
    CHECK(report("first_decoding/second_decode_other", run_child("child-decode", other)) == CHILD_OPEN);
    CHECK(report("first_decoding/second_decode_same", run_child("child-decode", movie)) == CHILD_OPEN);
    if (frames) for (LONG n = 10; n < 20; ++n) if (AVIStreamGetFrame(frames, start + n)) ++decoded;
    CHECK(decoded == 20);
    printf("{\"step\":\"first_after_children\",\"decoded\":%ld}\n", (long)decoded);
    if (frames) AVIStreamGetFrameClose(frames);
    if (stream) AVIStreamRelease(stream);
    if (file) AVIFileRelease(file);
    AVIFileExit();
    CHECK(es2_iv32_release());
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    if (argc < 2) { printf("usage: iv32_bind_fixture <mode>\n"); return 2; }
    const char *mode = argv[1];
    if (!strncmp(mode, "child-", 6)) return child_main(mode, argc > 2 ? argv[2] : NULL);
    if (!strcmp(mode, "events")) mode_events();
    else if (!strcmp(mode, "bad_argument")) mode_bad_argument();
    else if (!strcmp(mode, "no_vfw")) mode_no_vfw();
    else if (!strcmp(mode, "no_exports")) {
        if (argc < 3) { printf("no_exports needs the fixture DLL\n"); return 2; }
        mode_no_exports(argv[2]);
    } else if (!strcmp(mode, "bind") || !strcmp(mode, "two_process") || !strcmp(mode, "two_decode")) {
        if (!precondition()) return 0;
        if (!strcmp(mode, "bind")) mode_bind();
        else if (!strcmp(mode, "two_process")) mode_two_process();
        else mode_two_decode();
    } else { printf("unknown mode %s\n", mode); return 2; }
    printf("%s: %s (%d failures)\n", mode, failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
