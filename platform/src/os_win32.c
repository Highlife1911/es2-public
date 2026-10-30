/* es2/platform/os.h on Win32 and the C runtime. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "es2/platform/os.h"
#include "os_internal.h"

_Static_assert(sizeof(WIN32_FIND_DATAA) == ES2_OS_FIND_DATA_SIZE, "WIN32_FIND_DATAA");
_Static_assert(sizeof(TIME_ZONE_INFORMATION) == ES2_OS_TIME_ZONE_SIZE, "TIME_ZONE_INFORMATION");
_Static_assert(sizeof(SYSTEMTIME) == sizeof(es2_os_time), "SYSTEMTIME");
_Static_assert(sizeof(wchar_t) == sizeof(uint16_t), "UTF-16 wchar_t");

#define H(h) ((HANDLE)(h))
#define W(s) ((const wchar_t *)(s))

uint32_t es2_os_last_error(void) { return GetLastError(); }
void es2_os_set_last_error(uint32_t error) { SetLastError(error); }

/* ---- files */
es2_os_handle es2_os_create_file(const char *path, uint32_t access, uint32_t share, int inherit,
                                 uint32_t disposition, uint32_t attributes) {
    SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, (BOOL)inherit};
    return (es2_os_handle)CreateFileA(path, access, share, inherit ? &sa : NULL, disposition, attributes, NULL);
}
es2_os_handle es2_os_create_file_utf16(const uint16_t *path, uint32_t access, uint32_t share, int inherit,
                                       uint32_t disposition, uint32_t attributes) {
    SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, (BOOL)inherit};
    return (es2_os_handle)CreateFileW(W(path), access, share, inherit ? &sa : NULL, disposition, attributes, NULL);
}
int es2_os_read_file(es2_os_handle h, void *buffer, uint32_t size, uint32_t *done) {
    DWORD n = 0;
    BOOL ok = ReadFile(H(h), buffer, size, &n, NULL);
    if (done) *done = n;
    return ok != 0;
}
int es2_os_write_file(es2_os_handle h, const void *buffer, uint32_t size, uint32_t *done) {
    DWORD n = 0;
    BOOL ok = WriteFile(H(h), buffer, size, &n, NULL);
    if (done) *done = n;
    return ok != 0;
}
uint32_t es2_os_set_file_pointer(es2_os_handle h, int32_t distance, uint32_t method) {
    return SetFilePointer(H(h), (LONG)distance, NULL, method);
}
uint32_t es2_os_file_size(es2_os_handle h) { return GetFileSize(H(h), NULL); }
uint32_t es2_os_file_type(es2_os_handle h) { return GetFileType(H(h)); }
int es2_os_close_handle(es2_os_handle h) { return CloseHandle(H(h)) != 0; }
uint32_t es2_os_file_attributes(const char *path) { return GetFileAttributesA(path); }
int es2_os_delete_file(const char *path) { return DeleteFileA(path) != 0; }
int es2_os_create_directory(const char *path) { return CreateDirectoryA(path, NULL) != 0; }
int es2_os_file_mtime(const char *path, int64_t *seconds) {
    struct _stat64 st;
    if (_stat64(path, &st)) return 0;
    *seconds = (int64_t)st.st_mtime;
    return 1;
}
int es2_os_move_file_replace(const char *from, const char *to) { return MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING) != 0; }

/* ---- whole-file view */
void es2_os_unmap_file(es2_os_file_view *view) {
    if (view->bytes) UnmapViewOfFile(view->bytes);
    if (view->mapping) CloseHandle(H(view->mapping));
    if (view->file) CloseHandle(H(view->file));
    *view = (es2_os_file_view){NULL, 0, 0, 0};
}
int es2_os_map_file(const char *path, es2_os_file_view *view) {
    *view = (es2_os_file_view){NULL, 0, 0, 0};
    HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return 0;
    view->file = (uintptr_t)file;
    LARGE_INTEGER size;
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 || size.QuadPart > 0x7fffffff) { es2_os_unmap_file(view); return 0; }
    HANDLE mapping = CreateFileMappingA(file, NULL, PAGE_READONLY, 0, 0, NULL);
    view->mapping = (uintptr_t)mapping;
    const void *bytes = mapping ? MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0) : NULL;
    if (!bytes) { es2_os_unmap_file(view); return 0; }
    view->bytes = bytes;
    view->size = (size_t)size.QuadPart;
    return 1;
}
int es2_os_view_sparse(const es2_os_file_view *view) { (void)view; return 0; }
void es2_os_view_ensure(const es2_os_file_view *view, size_t offset, size_t length) {
    (void)view; (void)offset; (void)length;
}
es2_os_handle es2_os_find_first(const char *pattern, uint8_t find_data[ES2_OS_FIND_DATA_SIZE]) {
    WIN32_FIND_DATAA found;
    memset(&found, 0, sizeof(found));
    HANDLE h = FindFirstFileA(pattern, &found);
    memcpy(find_data, &found, ES2_OS_FIND_DATA_SIZE);
    return (es2_os_handle)h;
}
int es2_os_find_next(es2_os_handle h, uint8_t find_data[ES2_OS_FIND_DATA_SIZE]) {
    WIN32_FIND_DATAA found;
    memset(&found, 0, sizeof(found));
    BOOL ok = FindNextFileA(H(h), &found);
    memcpy(find_data, &found, ES2_OS_FIND_DATA_SIZE);
    return ok != 0;
}
int es2_os_find_close(es2_os_handle h) { return FindClose(H(h)) != 0; }
int es2_os_file_time_to_local(const uint32_t in[2], uint32_t out[2]) {
    FILETIME a = {in[0], in[1]}, b = {0, 0};
    BOOL ok = FileTimeToLocalFileTime(&a, &b);
    out[0] = b.dwLowDateTime;
    out[1] = b.dwHighDateTime;
    return ok != 0;
}
int es2_os_file_time_to_dos(const uint32_t in[2], uint16_t *date, uint16_t *time) {
    FILETIME a = {in[0], in[1]};
    WORD d = 0, t = 0;
    BOOL ok = FileTimeToDosDateTime(&a, &d, &t);
    *date = d;
    *time = t;
    return ok != 0;
}
uint32_t es2_os_current_directory(char *out, uint32_t capacity) { return GetCurrentDirectoryA(capacity, out); }
uint32_t es2_os_full_path(const char *path, char *out, uint32_t capacity) { return GetFullPathNameA(path, capacity, out, NULL); }

/* ---- streams */
#define F(s) ((FILE *)(s))
es2_os_stream *es2_os_fopen(const char *path, const char *mode) { return (es2_os_stream *)fopen(path, mode); }
int es2_os_fclose(es2_os_stream *s) { return fclose(F(s)); }
size_t es2_os_fread(void *buffer, size_t size, size_t count, es2_os_stream *s) { return fread(buffer, size, count, F(s)); }
size_t es2_os_fwrite(const void *buffer, size_t size, size_t count, es2_os_stream *s) { return fwrite(buffer, size, count, F(s)); }
int es2_os_fseek(es2_os_stream *s, long offset, int origin) { return fseek(F(s), offset, origin); }
long es2_os_ftell(es2_os_stream *s) { return ftell(F(s)); }
int es2_os_fgetc(es2_os_stream *s) { return fgetc(F(s)); }
int es2_os_fputc(int c, es2_os_stream *s) { return fputc(c, F(s)); }
int es2_os_fputs(const char *text, es2_os_stream *s) { return fputs(text, F(s)); }
int es2_os_fflush(es2_os_stream *s) { return fflush(F(s)); }
int es2_os_ferror(es2_os_stream *s) { return ferror(F(s)); }
int es2_os_vfprintf(es2_os_stream *s, const char *format, va_list args) { return vfprintf(F(s), format, args); }
int es2_os_fprintf(es2_os_stream *s, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int n = vfprintf(F(s), format, args);
    va_end(args);
    return n;
}
int es2_os_remove(const char *path) { return remove(path); }
es2_os_stream *es2_os_stdin(void) { return (es2_os_stream *)stdin; }
es2_os_stream *es2_os_stdout(void) { return (es2_os_stream *)stdout; }
es2_os_stream *es2_os_stderr(void) { return (es2_os_stream *)stderr; }

/* ---- clocks */
uint64_t es2_os_counter(void) {
    LARGE_INTEGER v;
    QueryPerformanceCounter(&v);
    return (uint64_t)v.QuadPart;
}
uint64_t es2_os_counter_frequency(void) {
    LARGE_INTEGER v;
    return QueryPerformanceFrequency(&v) && v.QuadPart > 0 ? (uint64_t)v.QuadPart : 0u;
}
uint32_t es2_os_tick_count(void) { return (uint32_t)GetTickCount(); }
void es2_os_local_time(es2_os_time *out) {
    SYSTEMTIME t;
    GetLocalTime(&t);
    memcpy(out, &t, sizeof(t));
}
void es2_os_system_time(es2_os_time *out) {
    SYSTEMTIME t;
    GetSystemTime(&t);
    memcpy(out, &t, sizeof(t));
}
uint32_t es2_os_time_zone(uint8_t out[ES2_OS_TIME_ZONE_SIZE]) {
    TIME_ZONE_INFORMATION zone;
    memset(&zone, 0, sizeof(zone));
    DWORD r = GetTimeZoneInformation(&zone);
    memcpy(out, &zone, sizeof(zone));
    return r;
}
void es2_os_sleep(uint32_t milliseconds) { Sleep(milliseconds); }

/* ---- memory */
uintptr_t es2_os_virtual_alloc(uintptr_t address, size_t size, uint32_t type, uint32_t protect) {
    return (uintptr_t)VirtualAlloc((void *)address, size, type, protect);
}
int es2_os_virtual_free(uintptr_t address, size_t size, uint32_t type) { return VirtualFree((void *)address, size, type) != 0; }
int es2_os_virtual_protect(uintptr_t address, size_t size, uint32_t protect, uint32_t *old) {
    DWORD previous = 0;
    BOOL ok = VirtualProtect((void *)address, size, protect, &previous);
    if (old) *old = previous;
    return ok != 0;
}
int es2_os_virtual_query(uintptr_t address, es2_os_region *out) {
    MEMORY_BASIC_INFORMATION m;
    if (!VirtualQuery((const void *)address, &m, sizeof(m))) return 0;
    out->base = (uintptr_t)m.BaseAddress;
    out->allocation_base = (uintptr_t)m.AllocationBase;
    out->size = m.RegionSize;
    out->allocation_protect = m.AllocationProtect;
    out->state = m.State;
    out->protect = m.Protect;
    out->type = m.Type;
    return 1;
}
void es2_os_address_range(uintptr_t *lowest, uintptr_t *highest) {
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    *lowest = (uintptr_t)info.lpMinimumApplicationAddress;
    *highest = (uintptr_t)info.lpMaximumApplicationAddress;
}
uint32_t es2_os_module_file_name(uintptr_t module, char *out, uint32_t capacity) {
    return GetModuleFileNameA((HMODULE)module, out, capacity);
}
/* CW3220's _osversion test: Win32s exports Borland32 from kernel32, Windows
 * 9x sets bit 31 of GetVersion. */
uint32_t es2_os_heap_platform(void) {
    HMODULE kernel = GetModuleHandleA("kernel32.dll");
    if (kernel && GetProcAddress(kernel, "Borland32")) return 2u;
    return (GetVersion() & 0x80000000u) ? 1u : 0u;
}

/* ---- process */
_Noreturn void es2_os_exit(uint32_t code) {
    ExitProcess(code);
}
uint32_t es2_os_process_id(void) { return GetCurrentProcessId(); }
uint32_t es2_os_thread_id(void) { return GetCurrentThreadId(); }
const char *es2_os_command_line(void) { return GetCommandLineA(); }

/* ---- environment */
char *es2_os_host_environment(void) {
    char *block = GetEnvironmentStringsA();
    if (!block) return NULL;
    size_t n = 0;
    while (block[n]) n += strlen(block + n) + 1;
    char *copy = malloc(n + 1);
    if (copy) memcpy(copy, block, n + 1);
    FreeEnvironmentStringsA(block);
    return copy;
}
const char *es2_os_host_getenv(const char *name) { return getenv(name); }

/* ---- text */
int es2_os_utf16_from_ansi(const char *in, int in_count, uint16_t *out, int capacity) {
    return MultiByteToWideChar(CP_ACP, 0, in, in_count, (wchar_t *)out, capacity);
}
int es2_os_ansi_from_utf16(const uint16_t *in, int in_count, char *out, int capacity) {
    return WideCharToMultiByte(CP_ACP, 0, W(in), in_count, out, capacity, NULL, NULL);
}

/* ---- INI files: the API itself; platform/src/ini.c is compared with it. */
static void widen(const char *in, wchar_t *out, size_t capacity) {
    size_t i = 0;
    for (; in[i] && i + 1 < capacity; ++i) out[i] = (unsigned char)in[i];
    out[i] = 0;
}
uint32_t es2_os_profile_string(const char *section, const char *key, const char *fallback, uint16_t *out,
                               uint32_t capacity, const uint16_t *file) {
    wchar_t s[64], k[64], f[256];
    widen(section, s, 64);
    widen(key, k, 64);
    if (fallback) widen(fallback, f, 256);
    return GetPrivateProfileStringW(s, k, fallback ? f : NULL, (wchar_t *)out, capacity, W(file));
}
int32_t es2_os_profile_int(const char *section, const char *key, int32_t fallback, const uint16_t *file) {
    wchar_t s[64], k[64];
    widen(section, s, 64);
    widen(key, k, 64);
    return (int32_t)GetPrivateProfileIntW(s, k, fallback, W(file));
}
uint32_t es2_os_profile_string_ansi(const char *section, const char *key, const char *fallback, char *out,
                                    uint32_t capacity, const char *file) {
    return GetPrivateProfileStringA(section, key, fallback, out, capacity, file);
}
