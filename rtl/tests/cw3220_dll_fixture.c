/* //rtl:cw3220_dll_test: the own cw3220.dll as a DLL.
 *
 * `exports <dll>`: loads it, checks its export table against
 * rtl/src/cw3220_exports.h (65 runtime names and the descriptor), its
 * imports (system DLLs only), the state its DllMain set up (CW3220's start-up
 * of the handle table, the FILE array, the heap, the time zone, the
 * lifecycle), and calls exports of every family on the owner thread and from
 * another thread.
 * `processes <dll> <fake original> <stand-in>`: a stand-in executable that
 * imports four names statically runs as ES.EXE, VSHELL.EXE and OTHER.EXE
 * next to the own DLL (as CW3220.DLL) and a fake original
 * (CW3220-ORIGINAL.DLL): ES and VSHELL must reach the original for every
 * name (delegate mode, the executable's imports rebound), OTHER the own code
 * and the original only for a name outside DBSIM's 53, loaded on demand. */
#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "es2/rtl/own_runtime.h"
#include "rtl/src/cw3220_exports.h"

static unsigned failures, checks;
static void check(int passed, const char *expression, int line)
{
    ++checks;
    if (!passed) {
        if (failures < 40) fprintf(stderr, "cw3220_dll:%d: %s\n", line, expression);
        ++failures;
    }
}
#define CHECK(expression) check((expression) ? 1 : 0, #expression, __LINE__)

static const char *const expected_names[] = {
#define NAME_CODE(name, id, kind, model) name,
#define NAME_DATA(name, id) name,
    OWN_EXPORTS(NAME_CODE, NAME_DATA)
#undef NAME_CODE
#undef NAME_DATA
};
static const char *const data_names[] = {"_errno", "__streams", "__ctype"};
#define EXPECTED_COUNT (sizeof(expected_names) / sizeof(expected_names[0]))
_Static_assert(EXPECTED_COUNT == OWN_ALL_NAMES, "65 runtime names");

static uint32_t u32(const void *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static const IMAGE_SECTION_HEADER *section_of(const BYTE *image, uint32_t rva)
{
    const IMAGE_NT_HEADERS32 *nt = (const IMAGE_NT_HEADERS32 *)(image + ((const IMAGE_DOS_HEADER *)image)->e_lfanew);
    const IMAGE_SECTION_HEADER *s = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i)
        if (rva - s[i].VirtualAddress < (s[i].Misc.VirtualSize > s[i].SizeOfRawData ? s[i].Misc.VirtualSize : s[i].SizeOfRawData)) return &s[i];
    return NULL;
}
static int is_data_name(const char *name)
{
    for (unsigned i = 0; i < 3; ++i) if (!strcmp(name, data_names[i])) return 1;
    return 0;
}

static void check_tables(HMODULE dll)
{
    const BYTE *image = (const BYTE *)dll;
    const IMAGE_NT_HEADERS32 *nt = (const IMAGE_NT_HEADERS32 *)(image + ((const IMAGE_DOS_HEADER *)image)->e_lfanew);
    const IMAGE_DATA_DIRECTORY *ed = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    const IMAGE_EXPORT_DIRECTORY *exports = (const IMAGE_EXPORT_DIRECTORY *)(image + ed->VirtualAddress);
    CHECK(!_stricmp((const char *)image + exports->Name, "cw3220.dll"));
    CHECK(exports->NumberOfNames == EXPECTED_COUNT + 1u);
    unsigned seen[EXPECTED_COUNT + 1] = {0};
    for (unsigned i = 0; i < exports->NumberOfNames; ++i) {
        const char *name = (const char *)image + u32(image + exports->AddressOfNames + 4u * i);
        unsigned ordinal = *(const uint16_t *)(image + exports->AddressOfNameOrdinals + 2u * i);
        uint32_t rva = u32(image + exports->AddressOfFunctions + 4u * ordinal);
        const IMAGE_SECTION_HEADER *s = section_of(image, rva);
        int known = 0;
        for (unsigned k = 0; k < EXPECTED_COUNT; ++k) if (!strcmp(name, expected_names[k])) { ++seen[k]; known = 1; }
        if (!strcmp(name, ES2_OWN_RUNTIME_EXPORT)) { ++seen[EXPECTED_COUNT]; known = 1; }
        if (!known) fprintf(stderr, "unexpected export %s\n", name);
        CHECK(known);
        CHECK(s != NULL);
        if (s && known) {
            int code = (s->Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0, writable = (s->Characteristics & IMAGE_SCN_MEM_WRITE) != 0;
            if (is_data_name(name)) CHECK(!code && writable);
            else CHECK(code);
        }
    }
    for (unsigned k = 0; k <= EXPECTED_COUNT; ++k) CHECK(seen[k] == 1);
    /* Imports: system DLLs only; never a cw3220. */
    const IMAGE_DATA_DIRECTORY *id = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    for (const IMAGE_IMPORT_DESCRIPTOR *d = (const IMAGE_IMPORT_DESCRIPTOR *)(image + id->VirtualAddress); d->Name; ++d) {
        const char *name = (const char *)image + d->Name;
        int allowed = !_stricmp(name, "KERNEL32.dll") || !_stricmp(name, "USER32.dll") || !_strnicmp(name, "api-ms-win-crt-", 15);
        if (!allowed) fprintf(stderr, "unexpected import of %s\n", name);
        CHECK(allowed);
    }
    CHECK(nt->OptionalHeader.ImageBase == 0x0f000000u && !(nt->OptionalHeader.DllCharacteristics & IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE));
}

static uint32_t get32(uint32_t a) { return *(const uint32_t *)(uintptr_t)a; }
static uint32_t heap_read(void *context, uint32_t a) { (void)context; return get32(a); }
static uint32_t get16(uint32_t a) { return *(const uint16_t *)(uintptr_t)a; }
static uint8_t get8(uint32_t a) { return *(const uint8_t *)(uintptr_t)a; }

static void check_state(HMODULE dll, const es2_own_runtime_info *info)
{
    CHECK(info->version == ES2_OWN_RUNTIME_VERSION && info->mode == ES2_OWN_MODE_OWN);
    CHECK(info->image_base == (uint32_t)(uintptr_t)dll && info->image_size >= 0x10000u);
    const es2_io_layout *io = info->io;
    CHECK((uint32_t)(uintptr_t)GetProcAddress(dll, "__streams") == io->streams);
    CHECK((uint32_t)(uintptr_t)GetProcAddress(dll, "_errno") == io->errno_word);
    const uint8_t *ctype = (const uint8_t *)GetProcAddress(dll, "__ctype");
    CHECK(ctype && ctype[0] == 0 && (uint32_t)(uintptr_t)ctype == info->ctype_block + 0x100u);
    /* The table holds 0 for the high bytes; a signed index reaches the
     * tolower table below it, which es2_io_ctype models. */
    for (unsigned b = 0; ctype && b < 256u; ++b) {
        CHECK(ctype[b + 1] == (b < 0x80u ? (uint8_t)es2_io_ctype((uint8_t)b) : 0u));
        CHECK(ctype[1 + (int8_t)b] == (uint8_t)es2_io_ctype((uint8_t)b));
    }
    for (uint32_t k = 0; k < ES2_IO_ERROR_MAP_SIZE; ++k) CHECK(get8(io->error_map + k) == es2_io_error_map(k));
    for (uint32_t k = 0; k < ES2_IO_SCAN_CLASSES; ++k) CHECK(get8(io->scan_class + k) == es2_io_scan_class((uint8_t)k));
    CHECK(get32(io->error_count) == 49 && get32(io->umask_word) == 0xffffffffu && get32(get32(io->fmode_pointer)) == 0x4000);
    /* The handle table as CW3220's 409014 sets it up for this process. */
    STARTUPINFOA startup;
    GetStartupInfoA(&startup);
    CHECK(get32(io->nfile) == 50);
    if (!startup.cbReserved2) {
        static const DWORD which[3] = {STD_INPUT_HANDLE, STD_OUTPUT_HANDLE, STD_ERROR_HANDLE};
        for (uint32_t fd = 0; fd < 3; ++fd) {
            HANDLE h = GetStdHandle(which[fd]);
            CHECK(get32(io->handles + 4 * fd) == (uint32_t)(uintptr_t)h);
            uint32_t device = GetFileType(h) == FILE_TYPE_CHAR ? 0x2000u : 0u;
            CHECK(get32(io->openfd + 4 * fd) == (0x4000u | device | (fd ? 1u : 0u)));
        }
    } else printf("NOTE: the test runs with an inherited handle block of %u bytes\n", (unsigned)startup.cbReserved2);
    for (uint32_t fd = 3; fd < 50; ++fd) CHECK(!startup.cbReserved2 ? get32(io->openfd + 4 * fd) == 0 : 1);
    for (uint32_t i = 0; i < 50; ++i) {
        uint32_t fp = io->streams + 0x18u * i;
        CHECK(get16(fp + 0x14) == (fp & 0xffffu));
        CHECK((int8_t)get8(fp + 0x16) == (i < 3 ? (int8_t)i : -1));
    }
    /* Standard input: line buffered on a device, else fully; output: unbuffered on a device. */
    for (uint32_t i = 0; i < 2; ++i) {
        uint32_t fp = io->streams + 0x18u * i, device = get32(io->openfd + 4u * i) & 0x2000u;
        uint32_t flags = get16(fp + 0x12);
        CHECK(((flags & 0x200u) != 0) == (device != 0));
        if (i == 1 && device) CHECK(get32(fp + 0xc) == 0);
        else CHECK(get32(fp + 0xc) == 0x200 && (flags & 4u) && get32(get32(fp + 4) - 4u) == (0x208u | 1u));
    }
    const uint32_t *hooks = (const uint32_t *)(uintptr_t)info->exit_hooks;
    CHECK(hooks[0] != info->dummy_hook && hooks[1] == info->dummy_hook && hooks[2] == info->dummy_hook);
    /* Time zone: TZ, else GetTimeZoneInformation. */
    char tz[64];
    if (!GetEnvironmentVariableA("TZ", tz, sizeof(tz))) {
        TIME_ZONE_INFORMATION zone;
        DWORD result = GetTimeZoneInformation(&zone);
        CHECK(get32(io->time_zone) == (uint32_t)((zone.Bias + zone.StandardBias) * 60) && get32(io->summer_time) == (result == 2u));
    }
    /* Heap: CW3220's layout, the head block in the DLL; the standard
     * streams' buffers are its first blocks. */
    CHECK(info->heap->sentinel - info->image_base < info->image_size && info->heap->region_count >= 1u);
    CHECK(es2_heap_walk(info->heap, heap_read, NULL, NULL, NULL) >= 2);
    CHECK(info->lifecycle->exitbuf == info->dummy_hook && info->lifecycle->stream_hooks == info->exit_hooks);
    CHECK(info->counters->exit_status == -1 && info->counters->calls == 0);
}

typedef unsigned (__cdecl *fn_strlen)(const char *);
typedef void *(__cdecl *fn_calloc)(unsigned, unsigned);
typedef void *(__cdecl *fn_realloc)(void *, unsigned);
typedef void (__cdecl *fn_free)(void *);
typedef int (__cdecl *fn_sprintf)(char *, const char *, ...);
typedef void *(__cdecl *fn_fopen)(const char *, const char *);
typedef unsigned (__cdecl *fn_fio)(void *, unsigned, unsigned, void *);
typedef int (__cdecl *fn_fclose)(void *);
typedef int (__cdecl *fn_open)(const char *, int, ...);
typedef long (__cdecl *fn_long2)(int, ...);
typedef int (__cdecl *fn_read)(int, void *, unsigned);
typedef int (__cdecl *fn_findfirst)(const char *, void *, int);
typedef int (__cdecl *fn_findnext)(void *);
typedef int (__cdecl *fn_atexit)(void (__cdecl *)(void));
typedef long (__cdecl *fn_time)(long *);
typedef int (__cdecl *fn_handler)(void *, void *, void *, void *);
typedef void (__cdecl *fn_void)(void);
#define GET(type, name) ((type)(void *)GetProcAddress(dll, name))

static void __cdecl never_called(void) {}
static fn_strlen thread_strlen;
static DWORD WINAPI other_thread(void *argument) { *(unsigned *)argument = thread_strlen("abcd"); return 0; }

static void check_calls(HMODULE dll, const es2_own_runtime_info *info, const char *tmp)
{
    const es2_own_counters *counters = info->counters;
    fn_strlen my_strlen = GET(fn_strlen, "_strlen");
    CHECK(my_strlen && my_strlen("hello") == 5 && counters->calls == 1 && counters->foreign_calls == 0);
    fn_calloc my_calloc = GET(fn_calloc, "_calloc");
    fn_realloc my_realloc = GET(fn_realloc, "_realloc");
    fn_free my_free = GET(fn_free, "_free");
    uint8_t *p = my_calloc(3, 5);
    CHECK(p && get32((uint32_t)(uintptr_t)p - 4u) == 25u);
    int zero = 1;
    for (unsigned i = 0; p && i < 15; ++i) if (p[i]) zero = 0;
    CHECK(zero);
    if (p) { memcpy(p, "0123456789abcde", 15); p = my_realloc(p, 100); }
    CHECK(p && !memcmp(p, "0123456789abcde", 15) && get32((uint32_t)(uintptr_t)p - 4u) == 109u);
    if (p) my_free(p);
    char text[64];
    fn_sprintf my_sprintf = GET(fn_sprintf, "_sprintf");
    CHECK(my_sprintf(text, "%d-%s|%04x", 42, "x", 0xbeef) == 9 && !strcmp(text, "42-x|beef"));
    uint64_t declined = counters->declined;
    CHECK(my_sprintf(text, "") == 0 && text[0] == 0 && counters->declined == declined + 1);
    /* Files through the FILE functions and the handle functions. */
    char path[MAX_PATH], pattern[MAX_PATH];
    snprintf(path, sizeof(path), "%s\\own_runtime.bin", tmp);
    snprintf(pattern, sizeof(pattern), "%s\\own_runtime.*", tmp);
    fn_fopen my_fopen = GET(fn_fopen, "_fopen");
    fn_fio my_fwrite = GET(fn_fio, "_fwrite"), my_fread = GET(fn_fio, "_fread");
    fn_fclose my_fclose = GET(fn_fclose, "_fclose");
    void *fp = my_fopen(path, "wb");
    CHECK(fp == (void *)(uintptr_t)(info->io->streams + 3u * 0x18u));
    CHECK(fp && my_fwrite("abc", 1, 3, fp) == 3 && my_fclose(fp) == 0);
    fp = my_fopen(path, "rb");
    memset(text, 0, sizeof(text));
    CHECK(fp && my_fread(text, 1, 10, fp) == 3 && !strcmp(text, "abc") && my_fclose(fp) == 0);
    fn_open my_open = GET(fn_open, "_open");
    fn_long2 my_filelength = GET(fn_long2, "_filelength"), my_lseek = GET(fn_long2, "_lseek"), my_close = GET(fn_long2, "_close");
    fn_read my_read = GET(fn_read, "_read");
    int fd = my_open(path, 0x8000);
    CHECK(fd == 3 && my_filelength(fd) == 3 && my_lseek(fd, 1L, 0) == 1);
    memset(text, 0, sizeof(text));
    CHECK(my_read(fd, text, 10) == 2 && !strcmp(text, "bc") && my_close(fd) == 0);
    uint8_t block[0x120];
    fn_findfirst my_findfirst = GET(fn_findfirst, "_findfirst");
    fn_findnext my_findnext = GET(fn_findnext, "_findnext");
    CHECK(my_findfirst(pattern, block, 0) == 0 && !strcmp((const char *)block + 0x10, "own_runtime.bin") && get32((uint32_t)(uintptr_t)block + 4) == 3);
    CHECK(my_findnext(block) == -1 && get32(info->io->errno_word) == 2);
    long now = 0;
    fn_time my_time = GET(fn_time, "_time");
    CHECK(my_time(&now) == now && now > 1600000000L);
    /* Lifecycle and exceptions: register (exit is not run here). */
    fn_atexit my_atexit = GET(fn_atexit, "_atexit");
    CHECK(my_atexit(never_called) == 0 && counters->atexit_registered == 1 && info->lifecycle->atexit_count == 1 &&
          info->lifecycle->atexit_table[0] == (uint32_t)(uintptr_t)never_called);
    fn_handler handler = GET(fn_handler, "__ExceptionHandler");
    EXCEPTION_RECORD record;
    memset(&record, 0, sizeof(record));
    record.ExceptionCode = EXCEPTION_ACCESS_VIOLATION;
    CHECK(handler && handler(&record, NULL, NULL, NULL) == ExceptionContinueSearch);
    /* @_CatchCleanup$qv with the context block (StackBase-8) naming an empty
     * list, as DBSIM's entry leaves it: nothing happens. */
    uint32_t base;
    __asm__ volatile("movl %%fs:4, %0" : "=r"(base));
    uint32_t saved = get32(base - 8u), empty = 0;
    *(volatile uint32_t *)(uintptr_t)(base - 8u) = (uint32_t)(uintptr_t)&empty;
    declined = counters->declined;
    GET(fn_void, "@_CatchCleanup$qv")();
    *(volatile uint32_t *)(uintptr_t)(base - 8u) = saved;
    CHECK(counters->declined == declined);
    /* Another thread runs on its own stack. */
    unsigned length = 0;
    thread_strlen = my_strlen;
    HANDLE thread = CreateThread(NULL, 0, other_thread, &length, 0, NULL);
    CHECK(thread && WaitForSingleObject(thread, 10000) == WAIT_OBJECT_0);
    if (thread) CloseHandle(thread);
    CHECK(length == 4 && counters->foreign_calls == 1);
    CHECK(counters->nested_calls == 0 && counters->delegated == 0);
    /* No module of this process is the original runtime. */
    CHECK(GetModuleHandleA(ES2_OWN_ORIGINAL_FILE) == NULL);
    DeleteFileA(path);
}

static int run_exports(const char *dll_path)
{
    HMODULE dll = LoadLibraryA(dll_path);
    CHECK(dll != NULL);
    if (!dll) return 1;
    check_tables(dll);
    es2_own_runtime_fn describe = (es2_own_runtime_fn)(void *)GetProcAddress(dll, ES2_OWN_RUNTIME_EXPORT);
    CHECK(describe != NULL);
    if (!describe) return 1;
    const es2_own_runtime_info *info = describe();
    check_state(dll, info);
    char tmp[MAX_PATH];
    DWORD n = GetEnvironmentVariableA("TEST_TMPDIR", tmp, sizeof(tmp));
    if (!n || n >= sizeof(tmp)) GetTempPathA(sizeof(tmp), tmp);
    check_calls(dll, info, tmp);
    CHECK(FreeLibrary(dll));
    return 0;
}

/* ------------------------------------------------------------------------
 * The stand-in under three names. */
static int run_standin(const char *dir, const char *exe, char *out, size_t size)
{
    char command[MAX_PATH * 2];
    snprintf(command, sizeof(command), "\"%s\\%s\"", dir, exe);
    SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, TRUE};
    HANDLE read_end, write_end;
    if (!CreatePipe(&read_end, &write_end, &sa, 0)) return -1;
    SetHandleInformation(read_end, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOA si;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = write_end; si.hStdError = write_end; si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi;
    BOOL started = CreateProcessA(NULL, command, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, dir, &si, &pi);
    CloseHandle(write_end);
    if (!started) { CloseHandle(read_end); return -1; }
    size_t used = 0;
    DWORD got;
    while (used + 1 < size && ReadFile(read_end, out + used, (DWORD)(size - 1 - used), &got, NULL) && got) used += got;
    out[used] = 0;
    /* The stand-in's text-mode output ends its line with CR LF. */
    size_t kept = 0;
    for (size_t i = 0; i < used; ++i) if (out[i] != '\r') out[kept++] = out[i];
    out[kept] = 0;
    CloseHandle(read_end);
    DWORD code = 1;
    WaitForSingleObject(pi.hProcess, 20000);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    return (int)code;
}
static int run_processes(const char *dll, const char *fake, const char *standin)
{
    char root[MAX_PATH], dir[MAX_PATH], to[MAX_PATH];
    DWORD n = GetEnvironmentVariableA("TEST_TMPDIR", root, sizeof(root));
    if (!n || n >= sizeof(root)) GetTempPathA(sizeof(root), root);
    snprintf(dir, sizeof(dir), "%s\\delegate", root);
    CreateDirectoryA(dir, NULL);
    snprintf(to, sizeof(to), "%s\\CW3220.DLL", dir); CHECK(CopyFileA(dll, to, FALSE));
    snprintf(to, sizeof(to), "%s\\" ES2_OWN_ORIGINAL_FILE, dir); CHECK(CopyFileA(fake, to, FALSE));
    static const char *const names[] = {"ES.EXE", "VSHELL.EXE", "OTHER.EXE"};
    for (unsigned i = 0; i < 3; ++i) { snprintf(to, sizeof(to), "%s\\%s", dir, names[i]); CHECK(CopyFileA(standin, to, FALSE)); }
    char out[512];
    for (unsigned i = 0; i < 2; ++i) {
        int code = run_standin(dir, names[i], out, sizeof(out));
        printf("%s: %s", names[i], out);
        CHECK(code == 0);
        CHECK(!strcmp(out, "before=1 strlen=1003 memcmp=3000 streams=original middle=1 strcmp=2000 after=1\n"));
    }
    int code = run_standin(dir, "OTHER.EXE", out, sizeof(out));
    printf("OTHER.EXE: %s", out);
    CHECK(code == 0);
    CHECK(!strcmp(out, "before=0 strlen=3 memcmp=0 streams=own middle=0 strcmp=2000 after=1\n"));
    return 0;
}

/* A data file's runfiles path: Windows tests get a manifest (one line
 * "_main/<path> <absolute path>" per file) instead of a tree. */
static const char *resolve(const char *path)
{
    static char results[4][MAX_PATH];
    static unsigned used;
    const char *manifest = getenv("RUNFILES_MANIFEST_FILE");
    FILE *stream = manifest && *manifest ? fopen(manifest, "rb") : NULL;
    if (!stream || used >= 4) { if (stream) fclose(stream); return path; }
    char line[4096], key[MAX_PATH + 8];
    snprintf(key, sizeof(key), "_main/%s ", path);
    size_t n = strlen(key);
    const char *found = path;
    while (fgets(line, sizeof(line), stream)) {
        if (strncmp(line, key, n)) continue;
        char *out = results[used++];
        snprintf(out, MAX_PATH, "%s", line + n);
        out[strcspn(out, "\r\n")] = 0;
        for (char *p = out; *p; ++p) if (*p == '/') *p = '\\';
        found = out;
        break;
    }
    fclose(stream);
    return found;
}

int main(int argc, char **argv)
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (argc == 3 && !strcmp(argv[1], "exports")) run_exports(resolve(argv[2]));
    else if (argc == 5 && !strcmp(argv[1], "processes")) run_processes(resolve(argv[2]), resolve(argv[3]), resolve(argv[4]));
    else { fprintf(stderr, "usage: exports <dll> | processes <dll> <fake> <stand-in>\n"); return 2; }
    printf("cw3220_dll %s: %u checks, %u failures\n", argv[1], checks, failures);
    return failures ? 1 : 0;
}
