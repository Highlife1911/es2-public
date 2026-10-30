/* The host OS layer: the only place where es2sim, es2shell and the launcher
 * meet the operating system for files, clocks, memory, the environment, INI
 * files and the process end. os_win32.c implements it with the Win32 and CRT
 * calls; os_wasm.c declares the imports of the JS host.
 *
 * The functions keep the Win32 semantics, including GetLastError, so that a
 * caller sees the same results on both builds. Values and layouts that reach
 * the guest (dispositions, attributes, WIN32_FIND_DATAA, SYSTEMTIME,
 * TIME_ZONE_INFORMATION, FILETIME) are the Win32 ones. */
#ifndef ES2_PLATFORM_OS_H
#define ES2_PLATFORM_OS_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A file or find handle; ES2_OS_INVALID_HANDLE is INVALID_HANDLE_VALUE. */
typedef uintptr_t es2_os_handle;
#define ES2_OS_INVALID_HANDLE ((es2_os_handle)~(uintptr_t)0)
#define ES2_OS_GENERIC_READ 0x80000000u
#define ES2_OS_INVALID_FILE_ATTRIBUTES 0xffffffffu
#define ES2_OS_INVALID_FILE_SIZE 0xffffffffu

enum {
    ES2_OS_GENERIC_WRITE = 0x40000000u,
    ES2_OS_FILE_APPEND_DATA = 0x00000004u,
    ES2_OS_FILE_SHARE_READ = 1u,
    ES2_OS_FILE_SHARE_WRITE = 2u,
    ES2_OS_CREATE_NEW = 1u,
    ES2_OS_CREATE_ALWAYS = 2u,
    ES2_OS_OPEN_EXISTING = 3u,
    ES2_OS_OPEN_ALWAYS = 4u,
    ES2_OS_FILE_ATTRIBUTE_DIRECTORY = 0x10u,
    ES2_OS_FILE_ATTRIBUTE_NORMAL = 0x80u,
    ES2_OS_FILE_TYPE_UNKNOWN = 0u,
    ES2_OS_ERROR_FILE_NOT_FOUND = 2u,
    ES2_OS_ERROR_INVALID_PARAMETER = 87u,
    ES2_OS_ERROR_ALREADY_EXISTS = 183u,
    ES2_OS_MAX_PATH = 260u,
    /* WIN32_FIND_DATAA */
    ES2_OS_FIND_DATA_SIZE = 320u,
    /* TIME_ZONE_INFORMATION */
    ES2_OS_TIME_ZONE_SIZE = 172u
};

/* Memory: the VirtualAlloc/VirtualProtect values. */
enum {
    ES2_OS_MEM_COMMIT = 0x1000u,
    ES2_OS_MEM_RESERVE = 0x2000u,
    ES2_OS_MEM_RELEASE = 0x8000u,
    ES2_OS_MEM_FREE = 0x10000u,
    ES2_OS_MEM_IMAGE = 0x1000000u,
    ES2_OS_PAGE_NOACCESS = 0x01u,
    ES2_OS_PAGE_READONLY = 0x02u,
    ES2_OS_PAGE_READWRITE = 0x04u,
    ES2_OS_PAGE_EXECUTE_READ = 0x20u,
    ES2_OS_PAGE_EXECUTE_READWRITE = 0x40u,
    ES2_OS_PAGE_GUARD = 0x100u
};

/* ---- errors */
uint32_t es2_os_last_error(void);
void es2_os_set_last_error(uint32_t error);

/* ---- files (ANSI paths in the current code page, as the A functions) */
es2_os_handle es2_os_create_file(const char *path, uint32_t access, uint32_t share, int inherit,
                                 uint32_t disposition, uint32_t attributes);
es2_os_handle es2_os_create_file_utf16(const uint16_t *path, uint32_t access, uint32_t share, int inherit,
                                       uint32_t disposition, uint32_t attributes);
/* ReadFile/WriteFile: nonzero on success, *done the bytes moved. */
int es2_os_read_file(es2_os_handle h, void *buffer, uint32_t size, uint32_t *done);
int es2_os_write_file(es2_os_handle h, const void *buffer, uint32_t size, uint32_t *done);
/* SetFilePointer without the high part: the new position or 0xffffffff. */
uint32_t es2_os_set_file_pointer(es2_os_handle h, int32_t distance, uint32_t method);
uint32_t es2_os_file_size(es2_os_handle h);
uint32_t es2_os_file_type(es2_os_handle h);
int es2_os_close_handle(es2_os_handle h);
uint32_t es2_os_file_attributes(const char *path);
int es2_os_delete_file(const char *path);
int es2_os_create_directory(const char *path);
/* The modification time (the C runtime's stat st_mtime, seconds since 1970);
 * nonzero when the file exists. */
int es2_os_file_mtime(const char *path, int64_t *seconds);
/* MoveFileExA with MOVEFILE_REPLACE_EXISTING. */
int es2_os_move_file_replace(const char *from, const char *to);
/* FindFirstFileA/FindNextFileA; find_data receives the WIN32_FIND_DATAA
 * bytes. */
es2_os_handle es2_os_find_first(const char *pattern, uint8_t find_data[ES2_OS_FIND_DATA_SIZE]);
int es2_os_find_next(es2_os_handle h, uint8_t find_data[ES2_OS_FIND_DATA_SIZE]);
int es2_os_find_close(es2_os_handle h);
/* FileTimeToLocalFileTime and FileTimeToDosDateTime on {low, high}. */
int es2_os_file_time_to_local(const uint32_t in[2], uint32_t out[2]);
int es2_os_file_time_to_dos(const uint32_t in[2], uint16_t *date, uint16_t *time);
/* GetCurrentDirectoryA and GetFullPathNameA: the length written, the size
 * needed when it does not fit, 0 on failure. */
uint32_t es2_os_current_directory(char *out, uint32_t capacity);
uint32_t es2_os_full_path(const char *path, char *out, uint32_t capacity);

/* ---- whole-file view: a file's bytes, read-only, for 1 to 0x7fffffff bytes.
 * The file is opened GENERIC_READ with share read and write, OPEN_EXISTING.
 * Windows maps it (CreateFileMappingA, MapViewOfFile) and keeps both handles
 * until the unmap. Wasm gives a buffer of the file's size: the host fills it
 * on demand when it serves the file from the web build's VFS (a sparse view,
 * docs/web_build.md section 8.3), else it is read whole at the map. */
typedef struct es2_os_file_view {
    const uint8_t *bytes;
    size_t size;
    uintptr_t file, mapping;
} es2_os_file_view;
/* Nonzero with *view filled; 0 with *view empty. */
int es2_os_map_file(const char *path, es2_os_file_view *view);
/* Releases a view (an empty one too) and leaves it empty. */
void es2_os_unmap_file(es2_os_file_view *view);
/* Nonzero for a sparse view: only the ensured ranges hold the file's bytes. */
int es2_os_view_sparse(const es2_os_file_view *view);
/* Makes the bytes [offset, offset + length) of a view (clipped to its size)
 * hold the file's bytes; nothing for a view that is not sparse. */
void es2_os_view_ensure(const es2_os_file_view *view, size_t offset, size_t length);

/* ---- streams: the CRT's buffered FILE, opaque to the callers */
typedef struct es2_os_stream es2_os_stream;
es2_os_stream *es2_os_fopen(const char *path, const char *mode);
int es2_os_fclose(es2_os_stream *s);
size_t es2_os_fread(void *buffer, size_t size, size_t count, es2_os_stream *s);
size_t es2_os_fwrite(const void *buffer, size_t size, size_t count, es2_os_stream *s);
int es2_os_fseek(es2_os_stream *s, long offset, int origin);
long es2_os_ftell(es2_os_stream *s);
int es2_os_fgetc(es2_os_stream *s);
int es2_os_fputc(int c, es2_os_stream *s);
int es2_os_fputs(const char *text, es2_os_stream *s);
int es2_os_fflush(es2_os_stream *s);
int es2_os_ferror(es2_os_stream *s);
int es2_os_fprintf(es2_os_stream *s, const char *format, ...) __attribute__((format(printf, 2, 3)));
int es2_os_vfprintf(es2_os_stream *s, const char *format, va_list args);
/* remove(): 0 on success. */
int es2_os_remove(const char *path);
/* The standard streams. */
es2_os_stream *es2_os_stdin(void);
es2_os_stream *es2_os_stdout(void);
es2_os_stream *es2_os_stderr(void);

/* ---- clocks and calendar */
/* QueryPerformanceCounter/Frequency; the frequency is 0 when there is none. */
uint64_t es2_os_counter(void);
uint64_t es2_os_counter_frequency(void);
uint32_t es2_os_tick_count(void);
/* SYSTEMTIME: year, month, day of week, day, hour, minute, second, ms. */
typedef struct es2_os_time {
    uint16_t year, month, day_of_week, day, hour, minute, second, milliseconds;
} es2_os_time;
void es2_os_local_time(es2_os_time *out);
void es2_os_system_time(es2_os_time *out);
/* GetTimeZoneInformation: the TIME_ZONE_INFORMATION bytes and its result. */
uint32_t es2_os_time_zone(uint8_t out[ES2_OS_TIME_ZONE_SIZE]);
void es2_os_sleep(uint32_t milliseconds);

/* ---- memory */
/* VirtualAlloc/VirtualFree/VirtualProtect; the address 0 on failure. */
uintptr_t es2_os_virtual_alloc(uintptr_t address, size_t size, uint32_t type, uint32_t protect);
int es2_os_virtual_free(uintptr_t address, size_t size, uint32_t type);
int es2_os_virtual_protect(uintptr_t address, size_t size, uint32_t protect, uint32_t *old);
/* VirtualQuery's MEMORY_BASIC_INFORMATION; 0 when the address is beyond the
 * user range. */
typedef struct es2_os_region {
    uintptr_t base, allocation_base;
    size_t size;
    uint32_t allocation_protect, state, protect, type;
} es2_os_region;
int es2_os_virtual_query(uintptr_t address, es2_os_region *out);
/* GetSystemInfo's lowest and highest application addresses. */
void es2_os_address_range(uintptr_t *lowest, uintptr_t *highest);
/* GetModuleFileNameA of the module containing address, "" when none. */
uint32_t es2_os_module_file_name(uintptr_t address, char *out, uint32_t capacity);
/* The C runtime's platform word: 2 Win32s, 1 Windows 9x, 0 Windows NT. */
uint32_t es2_os_heap_platform(void);

/* ---- process */
_Noreturn void es2_os_exit(uint32_t code);
uint32_t es2_os_process_id(void);
uint32_t es2_os_thread_id(void);
/* GetCommandLineA. */
const char *es2_os_command_line(void);
#ifdef __EMSCRIPTEN__
/* A wasm process has no command line of its own: its main sets it from argv,
 * each argument quoted as CommandLineToArgvW splits it back (spaces, tabs or
 * an empty argument in quotes, a quote as \" after its backslashes doubled).
 * 0 when out of memory. */
int es2_os_set_command_line(int argc, char *const *argv);
#endif

/* ---- the environment
 * es2_os_environment_load takes the block from a file: NUL-terminated
 * "NAME=value" strings ending with an empty one, the bytes
 * GetEnvironmentStringsA returns. Once loaded, it replaces the process
 * environment for es2_os_environment and es2_os_getenv. Returns 0, or 1 when
 * the file cannot be read or is not such a block. */
int es2_os_environment_load(const char *path);
/* The block (loaded, or the process's): free with es2_os_environment_free. */
char *es2_os_environment(void);
void es2_os_environment_free(char *block);
/* A variable of the block, the name compared without case; NULL if absent. */
const char *es2_os_getenv(const char *name);
/* Checks a block of at most size bytes: its length including the final NUL,
 * or 0. */
size_t es2_os_environment_block_size(const char *block, size_t size);

/* ---- text: the current ANSI code page and UTF-16 (MultiByteToWideChar and
 * WideCharToMultiByte with CP_ACP); the count written, 0 on failure. */
int es2_os_utf16_from_ansi(const char *in, int in_count, uint16_t *out, int capacity);
int es2_os_ansi_from_utf16(const uint16_t *in, int in_count, char *out, int capacity);

/* ---- INI files: GetPrivateProfileStringW/IntW and GetPrivateProfileStringA.
 * section, key and fallback are ASCII. */
uint32_t es2_os_profile_string(const char *section, const char *key, const char *fallback, uint16_t *out,
                               uint32_t capacity, const uint16_t *file);
int32_t es2_os_profile_int(const char *section, const char *key, int32_t fallback, const uint16_t *file);
uint32_t es2_os_profile_string_ansi(const char *section, const char *key, const char *fallback, char *out,
                                    uint32_t capacity, const char *file);

#ifdef __cplusplus
}
#endif

#endif
