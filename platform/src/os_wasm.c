/* es2/platform/os.h in the web build: the imports of the JS host (module
 * "es2os"), which serves them from the worker's VFS, performance.now() and
 * Date (docs/web_build.md sections 7 and 8), and under Node from node:fs.
 * Pointers are offsets in the instance's memory; strings are 0-terminated
 * ANSI or UTF-16LE. Every import keeps the Win32 result and sets the error
 * es2_os_last_error reports. INI files go through platform/src/ini.c; the
 * environment exists only as the block of --environment FILE. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "es2/platform/ini.h"
#include "es2/platform/os.h"
#include "os_internal.h"
#include "os_stream.h"

#define IMPORT(name) __attribute__((import_module("es2os"), import_name(#name)))

IMPORT(last_error) uint32_t host_last_error(void);
IMPORT(set_last_error) void host_set_last_error(uint32_t error);
IMPORT(create_file) uint32_t host_create_file(const char *path, uint32_t access, uint32_t share, uint32_t disposition,
                                              uint32_t attributes);
IMPORT(create_file_utf16) uint32_t host_create_file_utf16(const uint16_t *path, uint32_t access, uint32_t share,
                                                          uint32_t disposition, uint32_t attributes);
IMPORT(read_file) int32_t host_read_file(uint32_t h, void *buffer, uint32_t size, uint32_t *done);
IMPORT(write_file) int32_t host_write_file(uint32_t h, const void *buffer, uint32_t size, uint32_t *done);
IMPORT(set_file_pointer) uint32_t host_set_file_pointer(uint32_t h, int32_t distance, uint32_t method);
IMPORT(file_size) uint32_t host_file_size(uint32_t h);
IMPORT(file_type) uint32_t host_file_type(uint32_t h);
IMPORT(close_handle) int32_t host_close_handle(uint32_t h);
IMPORT(file_attributes) uint32_t host_file_attributes(const char *path);
IMPORT(delete_file) int32_t host_delete_file(const char *path);
IMPORT(create_directory) int32_t host_create_directory(const char *path);
IMPORT(file_mtime) int32_t host_file_mtime(const char *path, int64_t *seconds);
IMPORT(move_file_replace) int32_t host_move_file_replace(const char *from, const char *to);
IMPORT(find_first) uint32_t host_find_first(const char *pattern, uint8_t *find_data);
IMPORT(find_next) int32_t host_find_next(uint32_t h, uint8_t *find_data);
IMPORT(find_close) int32_t host_find_close(uint32_t h);
IMPORT(time_zone_bias) int32_t host_time_zone_bias(uint32_t low, uint32_t high);
IMPORT(current_directory) uint32_t host_current_directory(char *out, uint32_t capacity);
IMPORT(full_path) uint32_t host_full_path(const char *path, char *out, uint32_t capacity);
IMPORT(counter) uint64_t host_counter(void);
IMPORT(counter_frequency) uint64_t host_counter_frequency(void);
IMPORT(tick_count) uint32_t host_tick_count(void);
IMPORT(local_time) void host_local_time(es2_os_time *out);
IMPORT(system_time) void host_system_time(es2_os_time *out);
IMPORT(time_zone) uint32_t host_time_zone(uint8_t *out);
IMPORT(sleep) void host_sleep(uint32_t milliseconds);
IMPORT(heap_reserve) uintptr_t host_heap_reserve(uintptr_t address, uint32_t size);
IMPORT(exit) _Noreturn void host_exit(uint32_t code);
IMPORT(map_view) uint32_t host_map_view(uint32_t h, uint8_t *bytes, uint32_t size);
IMPORT(view_ensure) void host_view_ensure(uint32_t mapping, uint32_t offset, uint32_t length);
IMPORT(unmap_view) void host_unmap_view(uint32_t mapping);

/* ---- errors */
uint32_t es2_os_last_error(void) { return host_last_error(); }
void es2_os_set_last_error(uint32_t error) { host_set_last_error(error); }

/* ---- files; handles are the host's numbers, 0xffffffff is invalid. */
static es2_os_handle handle(uint32_t h) { return h == 0xffffffffu ? ES2_OS_INVALID_HANDLE : (es2_os_handle)h; }
es2_os_handle es2_os_create_file(const char *path, uint32_t access, uint32_t share, int inherit,
                                 uint32_t disposition, uint32_t attributes) {
    (void)inherit;
    return handle(host_create_file(path, access, share, disposition, attributes));
}
es2_os_handle es2_os_create_file_utf16(const uint16_t *path, uint32_t access, uint32_t share, int inherit,
                                       uint32_t disposition, uint32_t attributes) {
    (void)inherit;
    return handle(host_create_file_utf16(path, access, share, disposition, attributes));
}
int es2_os_read_file(es2_os_handle h, void *buffer, uint32_t size, uint32_t *done) {
    uint32_t n = 0;
    int ok = host_read_file((uint32_t)h, buffer, size, &n) != 0;
    if (done) *done = n;
    return ok;
}
int es2_os_write_file(es2_os_handle h, const void *buffer, uint32_t size, uint32_t *done) {
    uint32_t n = 0;
    int ok = host_write_file((uint32_t)h, buffer, size, &n) != 0;
    if (done) *done = n;
    return ok;
}
uint32_t es2_os_set_file_pointer(es2_os_handle h, int32_t distance, uint32_t method) {
    return host_set_file_pointer((uint32_t)h, distance, method);
}
uint32_t es2_os_file_size(es2_os_handle h) { return host_file_size((uint32_t)h); }
uint32_t es2_os_file_type(es2_os_handle h) { return host_file_type((uint32_t)h); }
int es2_os_close_handle(es2_os_handle h) { return host_close_handle((uint32_t)h) != 0; }
uint32_t es2_os_file_attributes(const char *path) { return host_file_attributes(path); }
int es2_os_delete_file(const char *path) { return host_delete_file(path) != 0; }
int es2_os_create_directory(const char *path) { return host_create_directory(path) != 0; }
int es2_os_file_mtime(const char *path, int64_t *seconds) { return host_file_mtime(path, seconds) != 0; }
int es2_os_move_file_replace(const char *from, const char *to) { return host_move_file_replace(from, to) != 0; }

/* ---- whole-file view: a buffer in the instance's memory. map_view gives a
 * mapping number (kept in view->mapping) when the host fills the buffer on
 * demand (view_ensure), 0 when the file is read whole here. */
void es2_os_unmap_file(es2_os_file_view *view) {
    if (view->mapping) host_unmap_view((uint32_t)view->mapping);
    free((void *)view->bytes);
    *view = (es2_os_file_view){NULL, 0, 0, 0};
}
int es2_os_view_sparse(const es2_os_file_view *view) { return view->mapping != 0; }
void es2_os_view_ensure(const es2_os_file_view *view, size_t offset, size_t length) {
    if (!view->mapping || offset >= view->size) return;
    if (length > view->size - offset) length = view->size - offset;
    if (length) host_view_ensure((uint32_t)view->mapping, (uint32_t)offset, (uint32_t)length);
}
int es2_os_map_file(const char *path, es2_os_file_view *view) {
    *view = (es2_os_file_view){NULL, 0, 0, 0};
    es2_os_handle h = es2_os_create_file(path, ES2_OS_GENERIC_READ, ES2_OS_FILE_SHARE_READ | ES2_OS_FILE_SHARE_WRITE, 0,
                                         ES2_OS_OPEN_EXISTING, ES2_OS_FILE_ATTRIBUTE_NORMAL);
    if (h == ES2_OS_INVALID_HANDLE) return 0;
    const uint32_t size = es2_os_file_size(h);
    uint8_t *bytes = size && size <= 0x7fffffffu ? malloc(size) : NULL;
    const uint32_t mapping = bytes ? host_map_view((uint32_t)h, bytes, size) : 0;
    uint32_t done = 0;
    int ok = bytes && (mapping || (es2_os_read_file(h, bytes, size, &done) && done == size));
    es2_os_close_handle(h);
    if (!ok) { free(bytes); return 0; }
    view->bytes = bytes;
    view->size = size;
    view->mapping = mapping;
    return 1;
}
es2_os_handle es2_os_find_first(const char *pattern, uint8_t find_data[ES2_OS_FIND_DATA_SIZE]) {
    memset(find_data, 0, ES2_OS_FIND_DATA_SIZE);
    return handle(host_find_first(pattern, find_data));
}
int es2_os_find_next(es2_os_handle h, uint8_t find_data[ES2_OS_FIND_DATA_SIZE]) {
    memset(find_data, 0, ES2_OS_FIND_DATA_SIZE);
    return host_find_next((uint32_t)h, find_data) != 0;
}
int es2_os_find_close(es2_os_handle h) { return host_find_close((uint32_t)h) != 0; }
/* The host gives today's local bias in minutes: FileTimeToLocalFileTime applies the current
   bias to every instant. */
int es2_os_file_time_to_local(const uint32_t in[2], uint32_t out[2]) {
    uint64_t t = (uint64_t)in[1] << 32 | in[0];
    t -= (uint64_t)((int64_t)host_time_zone_bias(in[0], in[1]) * 600000000);
    out[0] = (uint32_t)t;
    out[1] = (uint32_t)(t >> 32);
    return 1;
}
/* FILETIME (100 ns since 1601) to the DOS date and time, 1980..2107. */
int es2_os_file_time_to_dos(const uint32_t in[2], uint16_t *date, uint16_t *time) {
    uint64_t t = ((uint64_t)in[1] << 32 | in[0]) / 10000000u;
    uint64_t days = t / 86400u, rest = t % 86400u;
    /* days since 1601-01-01 to the civil date (proleptic Gregorian) */
    int64_t z = (int64_t)days - 134774 + 719468;
    int64_t era = z / 146097, doe = z - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
    int64_t day = doy - (153 * mp + 2) / 5 + 1, month = mp < 10 ? mp + 3 : mp - 9;
    int64_t year = yoe + era * 400 + (month <= 2);
    if (year < 1980 || year > 2107) {
        es2_os_set_last_error(ES2_OS_ERROR_INVALID_PARAMETER);
        return 0;
    }
    *date = (uint16_t)((year - 1980) << 9 | month << 5 | day);
    *time = (uint16_t)(rest / 3600u << 11 | rest / 60u % 60u << 5 | rest % 60u / 2u);
    return 1;
}
uint32_t es2_os_current_directory(char *out, uint32_t capacity) { return host_current_directory(out, capacity); }
uint32_t es2_os_full_path(const char *path, char *out, uint32_t capacity) { return host_full_path(path, out, capacity); }

/* ---- streams: os_stream.c over the handles above; the standard streams
 * stay the C library's (its stdout and stderr reach the host's console). */
struct es2_os_stream {
    FILE *crt;
    es2_os_hstream *own;
    es2_os_stream *next;   /* the open streams, flushed at the process end as the C runtime's */
};
static es2_os_stream std_stream[3];
static es2_os_stream *open_streams;

static void flush_open_streams(void) {
    for (es2_os_stream *s = open_streams; s; s = s->next) es2_os_hstream_flush(s->own);
}

es2_os_stream *es2_os_fopen(const char *path, const char *mode) {
    static int registered;
    es2_os_stream *s = malloc(sizeof *s);
    if (!s) return NULL;
    s->crt = NULL;
    if (!(s->own = es2_os_hstream_open(path, mode))) {
        free(s);
        return NULL;
    }
    if (!registered) registered = atexit(flush_open_streams) == 0;
    s->next = open_streams;
    open_streams = s;
    return s;
}
int es2_os_fclose(es2_os_stream *s) {
    if (s->crt) return fclose(s->crt);
    for (es2_os_stream **at = &open_streams; *at; at = &(*at)->next)
        if (*at == s) {
            *at = s->next;
            break;
        }
    int result = es2_os_hstream_close(s->own);
    free(s);
    return result;
}
size_t es2_os_fread(void *buffer, size_t size, size_t count, es2_os_stream *s) {
    return s->crt ? fread(buffer, size, count, s->crt) : es2_os_hstream_read(buffer, size, count, s->own);
}
size_t es2_os_fwrite(const void *buffer, size_t size, size_t count, es2_os_stream *s) {
    return s->crt ? fwrite(buffer, size, count, s->crt) : es2_os_hstream_write(buffer, size, count, s->own);
}
int es2_os_fseek(es2_os_stream *s, long offset, int origin) {
    return s->crt ? fseek(s->crt, offset, origin) : es2_os_hstream_seek(s->own, offset, origin);
}
long es2_os_ftell(es2_os_stream *s) { return s->crt ? ftell(s->crt) : es2_os_hstream_tell(s->own); }
int es2_os_fgetc(es2_os_stream *s) { return s->crt ? fgetc(s->crt) : es2_os_hstream_getc(s->own); }
int es2_os_fputc(int c, es2_os_stream *s) { return s->crt ? fputc(c, s->crt) : es2_os_hstream_putc(c, s->own); }
int es2_os_fputs(const char *text, es2_os_stream *s) {
    return s->crt ? fputs(text, s->crt) : es2_os_hstream_puts(text, s->own);
}
int es2_os_fflush(es2_os_stream *s) { return s->crt ? fflush(s->crt) : es2_os_hstream_flush(s->own); }
int es2_os_ferror(es2_os_stream *s) { return s->crt ? ferror(s->crt) : es2_os_hstream_error(s->own); }
int es2_os_vfprintf(es2_os_stream *s, const char *format, va_list args) {
    return s->crt ? vfprintf(s->crt, format, args) : es2_os_hstream_vprintf(s->own, format, args);
}
int es2_os_fprintf(es2_os_stream *s, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int n = es2_os_vfprintf(s, format, args);
    va_end(args);
    return n;
}
int es2_os_remove(const char *path) { return host_delete_file(path) != 0 ? 0 : -1; }
es2_os_stream *es2_os_stdin(void) {
    std_stream[0].crt = stdin;
    return &std_stream[0];
}
es2_os_stream *es2_os_stdout(void) {
    std_stream[1].crt = stdout;
    return &std_stream[1];
}
es2_os_stream *es2_os_stderr(void) {
    std_stream[2].crt = stderr;
    return &std_stream[2];
}

/* ---- clocks */
uint64_t es2_os_counter(void) { return host_counter(); }
uint64_t es2_os_counter_frequency(void) { return host_counter_frequency(); }
uint32_t es2_os_tick_count(void) { return host_tick_count(); }
void es2_os_local_time(es2_os_time *out) { host_local_time(out); }
void es2_os_system_time(es2_os_time *out) { host_system_time(out); }
uint32_t es2_os_time_zone(uint8_t out[ES2_OS_TIME_ZONE_SIZE]) {
    memset(out, 0, ES2_OS_TIME_ZONE_SIZE);
    return host_time_zone(out);
}
void es2_os_sleep(uint32_t milliseconds) { host_sleep(milliseconds); }

/* ---- memory: the guest lies in linear memory (docs/web_build.md section
 * 5). Reservations grow the memory to the address; there is no protection
 * and no release. */
uintptr_t es2_os_virtual_alloc(uintptr_t address, size_t size, uint32_t type, uint32_t protect) {
    (void)protect;
    if (!address || !(type & ES2_OS_MEM_RESERVE)) return address ? address : (uintptr_t)calloc(1, size);
    return host_heap_reserve(address, (uint32_t)size);
}
int es2_os_virtual_free(uintptr_t address, size_t size, uint32_t type) {
    (void)address;
    (void)size;
    (void)type;
    return 1;
}
int es2_os_virtual_protect(uintptr_t address, size_t size, uint32_t protect, uint32_t *old) {
    (void)address;
    (void)size;
    (void)protect;
    if (old) *old = ES2_OS_PAGE_READWRITE;
    return 1;
}
int es2_os_virtual_query(uintptr_t address, es2_os_region *out) {
    memset(out, 0, sizeof(*out));
    out->base = address & ~(uintptr_t)0xfff;
    out->allocation_base = 0;
    out->size = 0x1000;
    out->state = ES2_OS_MEM_COMMIT;
    out->protect = out->allocation_protect = ES2_OS_PAGE_READWRITE;
    return 1;
}
void es2_os_address_range(uintptr_t *lowest, uintptr_t *highest) {
    *lowest = 0x10000u;
    *highest = 0x7ffeffffu;
}
uint32_t es2_os_module_file_name(uintptr_t module, char *out, uint32_t capacity) {
    (void)module;
    if (capacity) out[0] = 0;
    return 0;
}
/* Windows NT, as on the Windows build's hosts. */
uint32_t es2_os_heap_platform(void) { return 0u; }

/* ---- process */
_Noreturn void es2_os_exit(uint32_t code) {
    flush_open_streams();
    fflush(NULL);
    host_exit(code);
}
uint32_t es2_os_process_id(void) { return 1u; }
uint32_t es2_os_thread_id(void) { return 1u; }
static char *command_line;
const char *es2_os_command_line(void) { return command_line ? command_line : ""; }
int es2_os_set_command_line(int argc, char *const *argv) {
    size_t size = 1;
    for (int i = 0; i < argc; i++) size += 2 * strlen(argv[i]) + 3;
    char *line = malloc(size), *out = line;
    if (!line) return 0;
    for (int i = 0; i < argc; i++) {
        const char *a = argv[i];
        if (i) *out++ = ' ';
        if (*a && !strpbrk(a, " \t\"")) {
            out += strlen(strcpy(out, a));
            continue;
        }
        *out++ = '"';
        for (size_t slashes = 0;; a++) {
            if (*a == '\\') { slashes++; continue; }
            /* backslashes before a quote or the closing quote are doubled */
            size_t n = (*a == '"' || !*a) ? 2 * slashes : slashes;
            while (n--) *out++ = '\\';
            slashes = 0;
            if (!*a) break;
            if (*a == '"') *out++ = '\\';
            *out++ = *a;
        }
        *out++ = '"';
    }
    *out = 0;
    free(command_line);
    command_line = line;
    return 1;
}

/* ---- environment: only the loaded block. */
char *es2_os_host_environment(void) {
    char *block = malloc(2);
    if (block) block[0] = block[1] = 0;
    return block;
}
const char *es2_os_host_getenv(const char *name) {
    (void)name;
    return NULL;
}

/* ---- text: Windows-1252 as the ANSI code page. */
int es2_os_utf16_from_ansi(const char *in, int in_count, uint16_t *out, int capacity) {
    size_t n = in_count < 0 ? strlen(in) + 1u : (size_t)in_count;
    if (!capacity || n > (size_t)capacity) return 0;
    return (int)es2_ini_decode((const uint8_t *)in, n, out);
}
int es2_os_ansi_from_utf16(const uint16_t *in, int in_count, char *out, int capacity) {
    size_t n = 0;
    if (in_count < 0)
        while (in[n++]) {
        }
    else
        n = (size_t)in_count;
    if (!capacity || n > (size_t)capacity) return 0;
    for (size_t i = 0; i < n; ++i) {
        /* The reverse of es2_ini_decode, '?' for what 1252 lacks. */
        uint16_t c = in[i];
        out[i] = c < 0x80 || (c >= 0xa0 && c < 0x100) ? (char)c : '?';
        for (unsigned v = 0x80; v < 0xa0 && out[i] == '?' && c >= 0x80; ++v) {
            uint8_t byte = (uint8_t)v;
            uint16_t w;
            es2_ini_decode(&byte, 1, &w);
            if (w == c) out[i] = (char)v;
        }
    }
    return (int)n;
}

/* ---- INI files */
static void widen(const char *in, uint16_t *out, size_t capacity) {
    size_t i = 0;
    for (; in && in[i] && i + 1 < capacity; ++i) out[i] = (unsigned char)in[i];
    out[i] = 0;
}
uint32_t es2_os_profile_string(const char *section, const char *key, const char *fallback, uint16_t *out,
                               uint32_t capacity, const uint16_t *file) {
    uint16_t s[64], k[64], f[256];
    widen(section, s, 64);
    widen(key, k, 64);
    widen(fallback, f, 256);
    return es2_ini_file_string(file, s, k, fallback ? f : NULL, out, capacity);
}
int32_t es2_os_profile_int(const char *section, const char *key, int32_t fallback, const uint16_t *file) {
    uint16_t s[64], k[64];
    widen(section, s, 64);
    widen(key, k, 64);
    return (int32_t)es2_ini_file_int(file, s, k, fallback);
}
uint32_t es2_os_profile_string_ansi(const char *section, const char *key, const char *fallback, char *out,
                                    uint32_t capacity, const char *file) {
    return es2_ini_file_string_ansi(file, section, key, fallback, out, capacity);
}
