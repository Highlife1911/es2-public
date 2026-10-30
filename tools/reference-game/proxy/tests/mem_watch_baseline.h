// Memory watches for the DBSIM tick log. Each watch names a byte range whose
// address may be reached through pointer chains; after every tick record the
// ranges are compared with their previous contents and changes are written as
// separate "watch" events. Reads are bounds-checked with VirtualQuery and the
// game memory is never written.
//
// Configuration ([trace] in the proxy INI):
//   watch_count = N                  (at most MEM_WATCH_MAX)
//   watch1 = NAME ADDRESS LENGTH     (1..N)
// ADDRESS is a hexadecimal virtual address optionally followed by "@OFFSET"
// steps: each step replaces the current address by the 32-bit pointer stored
// there plus OFFSET. "0x4d24e6@0x45@0" therefore means [[0x4d24e6]+0x45]+0.
// LENGTH is a byte count, or "wADDRESS*STRIDE" for a signed 16-bit element
// count read at ADDRESS times STRIDE (re-evaluated on every check).
#ifndef ES2_MEM_WATCH_H
#define ES2_MEM_WATCH_H
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define MEM_WATCH_MAX 48u
#define MEM_WATCH_STEPS 6u
#define MEM_WATCH_NAME 32u
#define MEM_WATCH_MAX_BYTES 16384u
#define MEM_WATCH_FULL_LIMIT 256u   // ranges up to this size are always written whole
#define MEM_WATCH_MERGE_GAP 4u      // unchanged gaps shorter than this join two diff runs

typedef BOOL (*mem_watch_readable_fn)(const void *pointer, SIZE_T size);

typedef struct {
    char name[MEM_WATCH_NAME];
    uintptr_t base;
    DWORD offsets[MEM_WATCH_STEPS];
    unsigned steps;
    DWORD length;                    // fixed length, or stride when count_address != 0
    uintptr_t count_address;
    BYTE *previous;
    DWORD previous_length;           // 0: nothing recorded yet
    uintptr_t previous_address;
    BOOL previous_unreadable;
} mem_watch;

static mem_watch mem_watches[MEM_WATCH_MAX];
static unsigned mem_watch_count;

static BOOL mem_watch_number(const char **cursor, DWORD *value) {
    char *end;
    unsigned long parsed = strtoul(*cursor, &end, 0);
    if (end == *cursor) return FALSE;
    *value = (DWORD)parsed;
    *cursor = end;
    return TRUE;
}

// Parses "NAME ADDRESS LENGTH". Returns FALSE and leaves the table unchanged on
// any syntax error.
static BOOL mem_watch_add(const char *spec) {
    if (mem_watch_count >= MEM_WATCH_MAX) return FALSE;
    mem_watch watch;
    memset(&watch, 0, sizeof(watch));
    const char *cursor = spec;
    while (*cursor == ' ' || *cursor == '\t') ++cursor;
    size_t name_length = 0;
    while (cursor[name_length] && cursor[name_length] != ' ' && cursor[name_length] != '\t') {
        char c = cursor[name_length];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) return FALSE;
        ++name_length;
    }
    if (!name_length || name_length >= MEM_WATCH_NAME) return FALSE;
    memcpy(watch.name, cursor, name_length);
    cursor += name_length;
    while (*cursor == ' ' || *cursor == '\t') ++cursor;
    DWORD value;
    if (!mem_watch_number(&cursor, &value)) return FALSE;
    watch.base = value;
    while (*cursor == '@') {
        ++cursor;
        if (watch.steps >= MEM_WATCH_STEPS || !mem_watch_number(&cursor, &value)) return FALSE;
        watch.offsets[watch.steps++] = value;
    }
    if (*cursor != ' ' && *cursor != '\t') return FALSE;
    while (*cursor == ' ' || *cursor == '\t') ++cursor;
    if (*cursor == 'w') {
        ++cursor;
        if (!mem_watch_number(&cursor, &value) || *cursor++ != '*') return FALSE;
        watch.count_address = value;
        if (!mem_watch_number(&cursor, &watch.length) || !watch.length || watch.length > MEM_WATCH_MAX_BYTES) return FALSE;
    } else if (!mem_watch_number(&cursor, &watch.length) || !watch.length || watch.length > MEM_WATCH_MAX_BYTES) {
        return FALSE;
    }
    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n') ++cursor;
    if (*cursor) return FALSE;
    watch.previous = (BYTE *)malloc(MEM_WATCH_MAX_BYTES);
    if (!watch.previous) return FALSE;
    mem_watches[mem_watch_count++] = watch;
    return TRUE;
}

// Resolves the current address and length. Returns FALSE when a pointer in the
// chain or the count is unreadable, or the range is empty or too large.
static BOOL mem_watch_resolve(const mem_watch *watch, mem_watch_readable_fn readable, uintptr_t *address, DWORD *length) {
    uintptr_t current = watch->base;
    for (unsigned i = 0; i < watch->steps; ++i) {
        DWORD pointer;
        if (!readable((const void *)current, 4)) return FALSE;
        memcpy(&pointer, (const void *)current, 4);
        if (!pointer) return FALSE;
        current = (uintptr_t)pointer + watch->offsets[i];
    }
    DWORD size = watch->length;
    if (watch->count_address) {
        int16_t count;
        if (!readable((const void *)watch->count_address, 2)) return FALSE;
        memcpy(&count, (const void *)watch->count_address, 2);
        if (count <= 0 || (DWORD)count > MEM_WATCH_MAX_BYTES / watch->length) return FALSE;
        size = (DWORD)count * watch->length;
    }
    if (!readable((const void *)current, size)) return FALSE;
    *address = current;
    *length = size;
    return TRUE;
}

typedef void (*mem_watch_emit_fn)(const char *text, int length);

static int mem_watch_hex(char *out, const BYTE *bytes, DWORD size) {
    static const char digits[] = "0123456789abcdef";
    for (DWORD i = 0; i < size; ++i) {
        out[2 * i] = digits[bytes[i] >> 4];
        out[2 * i + 1] = digits[bytes[i] & 15];
    }
    return (int)(2 * size);
}

// Checks every watch and emits one JSON line per changed watch. The first
// record of a watch (and any change of address or length) is written whole;
// later changes of ranges above MEM_WATCH_FULL_LIMIT are written as runs of
// [offset, hex]. `line` must hold at least 2*MEM_WATCH_MAX_BYTES + 512 bytes
// plus 16 bytes per possible run.
static void mem_watch_check(unsigned long sequence, unsigned long frame, mem_watch_readable_fn readable,
                            mem_watch_emit_fn emit, char *line, size_t line_size) {
    for (unsigned w = 0; w < mem_watch_count; ++w) {
#ifdef ES2_ORACLE_PERF_H
        unsigned phase = perf_begin(PERF_WATCH);
#endif
        mem_watch *watch = &mem_watches[w];
        uintptr_t address = 0;
        DWORD length = 0;
        int n;
        BOOL resolved = mem_watch_resolve(watch, readable, &address, &length);
        if (!resolved) {
#ifdef ES2_ORACLE_PERF_H
            perf_end(phase);
#endif
            if (watch->previous_unreadable) continue;
            watch->previous_unreadable = TRUE;
            watch->previous_length = 0;
#ifdef ES2_ORACLE_PERF_H
            phase = perf_begin(PERF_FORMAT);
#endif
            n = snprintf(line, line_size, "{\"event\":\"watch\",\"sequence\":%lu,\"frame\":%lu,\"name\":\"%s\",\"unreadable\":true}\r\n",
                         sequence, frame, watch->name);
            emit(line, n);
#ifdef ES2_ORACLE_PERF_H
            perf_end(phase);
#endif
            continue;
        }
        const BYTE *bytes = (const BYTE *)address;
        BOOL whole = !watch->previous_length || watch->previous_address != address || watch->previous_length != length;
        BOOL unchanged = !whole && !memcmp(watch->previous, bytes, length);
#ifdef ES2_ORACLE_PERF_H
        perf_end(phase);
#endif
        if (unchanged) continue;
#ifdef ES2_ORACLE_PERF_H
        phase = perf_begin(PERF_FORMAT);
#endif
        n = snprintf(line, line_size, "{\"event\":\"watch\",\"sequence\":%lu,\"frame\":%lu,\"name\":\"%s\",\"address\":\"0x%08lx\",\"length\":%lu,",
                     sequence, frame, watch->name, (unsigned long)address, (unsigned long)length);
        if (whole || length <= MEM_WATCH_FULL_LIMIT) {
            n += snprintf(line + n, line_size - (size_t)n, "\"hex\":\"");
            n += mem_watch_hex(line + n, bytes, length);
            n += snprintf(line + n, line_size - (size_t)n, "\"}\r\n");
        } else {
            n += snprintf(line + n, line_size - (size_t)n, "\"diff\":[");
            BOOL first = TRUE;
            for (DWORD i = 0; i < length;) {
                if (watch->previous[i] == bytes[i]) { ++i; continue; }
                DWORD end = i + 1, last = i + 1;
                while (end < length && end - last < MEM_WATCH_MERGE_GAP) {
                    if (watch->previous[end] != bytes[end]) last = end + 1;
                    ++end;
                }
                if ((size_t)n + 2u * (last - i) + 32u > line_size) break;
                n += snprintf(line + n, line_size - (size_t)n, "%s[%lu,\"", first ? "" : ",", (unsigned long)i);
                n += mem_watch_hex(line + n, bytes + i, last - i);
                n += snprintf(line + n, line_size - (size_t)n, "\"]");
                first = FALSE;
                i = last;
            }
            n += snprintf(line + n, line_size - (size_t)n, "]}\r\n");
        }
        emit(line, n);
#ifdef ES2_ORACLE_PERF_H
        perf_end(phase);
        phase = perf_begin(PERF_WATCH);
#endif
        memcpy(watch->previous, bytes, length);
        watch->previous_length = length;
        watch->previous_address = address;
        watch->previous_unreadable = FALSE;
#ifdef ES2_ORACLE_PERF_H
        perf_end(phase);
#endif
    }
}
#endif
