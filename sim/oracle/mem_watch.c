/* The tick log's memory watches; see mem_watch.h. The parser, the
 * resolution, the comparison and the event format are the proxy's; the
 * memory access and the output are the caller's callbacks. */
#include "sim/oracle/mem_watch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int watch_number(const char **cursor, uint32_t *value) {
    char *end;
    unsigned long parsed = strtoul(*cursor, &end, 0);
    if (end == *cursor) return 0;
    *value = (uint32_t)parsed;
    *cursor = end;
    return 1;
}

int es2_mem_watch_add(es2_mem_watch_table *table, const char *spec) {
    if (table->count >= ES2_MEM_WATCH_MAX) return -1;
    es2_mem_watch watch;
    memset(&watch, 0, sizeof(watch));
    const char *cursor = spec;
    while (*cursor == ' ' || *cursor == '\t') ++cursor;
    size_t name_length = 0;
    while (cursor[name_length] && cursor[name_length] != ' ' && cursor[name_length] != '\t') {
        char c = cursor[name_length];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) return -1;
        ++name_length;
    }
    if (!name_length || name_length >= ES2_MEM_WATCH_NAME) return -1;
    memcpy(watch.name, cursor, name_length);
    cursor += name_length;
    while (*cursor == ' ' || *cursor == '\t') ++cursor;
    uint32_t value;
    if (!watch_number(&cursor, &value)) return -1;
    watch.base = value;
    while (*cursor == '@') {
        ++cursor;
        if (watch.steps >= ES2_MEM_WATCH_STEPS || !watch_number(&cursor, &value)) return -1;
        watch.offsets[watch.steps++] = value;
    }
    if (*cursor != ' ' && *cursor != '\t') return -1;
    while (*cursor == ' ' || *cursor == '\t') ++cursor;
    if (*cursor == 'w') {
        ++cursor;
        if (!watch_number(&cursor, &value) || *cursor++ != '*') return -1;
        watch.count_address = value;
        if (!watch_number(&cursor, &watch.length) || !watch.length || watch.length > ES2_MEM_WATCH_MAX_BYTES) return -1;
    } else if (!watch_number(&cursor, &watch.length) || !watch.length || watch.length > ES2_MEM_WATCH_MAX_BYTES) {
        return -1;
    }
    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n') ++cursor;
    if (*cursor) return -1;
    watch.previous = (uint8_t *)malloc(ES2_MEM_WATCH_MAX_BYTES);
    if (!watch.previous) return -1;
    table->watches[table->count++] = watch;
    return 0;
}

void es2_mem_watch_clear(es2_mem_watch_table *table) {
    for (unsigned i = 0; i < table->count; ++i) free(table->watches[i].previous);
    memset(table, 0, sizeof(*table));
}

/* The current address and length. The final copy is only an accessibility
 * probe; the caller's live bytes are read afterwards (mem_watch.h). */
static int watch_resolve(const es2_mem_watch *watch, const es2_mem_watch_access *access, uint32_t *address,
                         uint32_t *length, uint8_t *probe) {
    uint32_t current = watch->base;
    for (unsigned i = 0; i < watch->steps; ++i) {
        uint8_t raw[4];
        if (!access->copy(access->user, raw, current, sizeof(raw))) return 0;
        uint32_t pointer = (uint32_t)raw[0] | ((uint32_t)raw[1] << 8) | ((uint32_t)raw[2] << 16) | ((uint32_t)raw[3] << 24);
        if (!pointer) return 0;
        current = pointer + watch->offsets[i];
    }
    uint32_t size = watch->length;
    if (watch->count_address) {
        uint8_t raw[2];
        if (!access->copy(access->user, raw, watch->count_address, sizeof(raw))) return 0;
        int16_t count = (int16_t)((unsigned)raw[0] | ((unsigned)raw[1] << 8));
        if (count <= 0 || (uint32_t)count > ES2_MEM_WATCH_MAX_BYTES / watch->length) return 0;
        size = (uint32_t)count * watch->length;
    }
    if (!access->copy(access->user, probe, current, size)) return 0;
    *address = current;
    *length = size;
    return 1;
}

static int watch_hex(char *out, const uint8_t *bytes, uint32_t size) {
    static const char digits[] = "0123456789abcdef";
    for (uint32_t i = 0; i < size; ++i) {
        out[2 * i] = digits[bytes[i] >> 4];
        out[2 * i + 1] = digits[bytes[i] & 15];
    }
    return (int)(2 * size);
}

static unsigned phase_begin(const es2_mem_watch_access *access, unsigned phase) {
    return access->phase_begin ? access->phase_begin(access->user, phase) : 0u;
}
static void phase_end(const es2_mem_watch_access *access, unsigned token) {
    if (access->phase_end) access->phase_end(access->user, token);
}

void es2_mem_watch_check(es2_mem_watch_table *table, unsigned long sequence, unsigned long frame,
                         const es2_mem_watch_access *access, char *line, size_t line_size) {
    /* Main thread only and not reentrant, like the table; the 16 KiB probe
     * stays off the caller's stack. No contents are reused. */
    static uint8_t probe[ES2_MEM_WATCH_MAX_BYTES];
    for (unsigned w = 0; w < table->count; ++w) {
        unsigned phase = phase_begin(access, ES2_MEM_WATCH_PHASE_WATCH);
        es2_mem_watch *watch = &table->watches[w];
        uint32_t address = 0, length = 0;
        int n;
        const uint8_t *bytes = NULL;
        if (watch_resolve(watch, access, &address, &length, probe)) bytes = access->view(access->user, address, length);
        if (!bytes) {
            phase_end(access, phase);
            if (watch->previous_unreadable) continue;
            watch->previous_unreadable = 1;
            watch->previous_length = 0;
            phase = phase_begin(access, ES2_MEM_WATCH_PHASE_FORMAT);
            n = snprintf(line, line_size, "{\"event\":\"watch\",\"sequence\":%lu,\"frame\":%lu,\"name\":\"%s\",\"unreadable\":true}\r\n",
                         sequence, frame, watch->name);
            access->emit(access->user, line, n);
            phase_end(access, phase);
            continue;
        }
        int whole = !watch->previous_length || watch->previous_address != address || watch->previous_length != length;
        int unchanged = !whole && !memcmp(watch->previous, bytes, length);
        phase_end(access, phase);
        if (unchanged) continue;
        phase = phase_begin(access, ES2_MEM_WATCH_PHASE_FORMAT);
        n = snprintf(line, line_size, "{\"event\":\"watch\",\"sequence\":%lu,\"frame\":%lu,\"name\":\"%s\",\"address\":\"0x%08lx\",\"length\":%lu,",
                     sequence, frame, watch->name, (unsigned long)address, (unsigned long)length);
        if (whole || length <= ES2_MEM_WATCH_FULL_LIMIT) {
            n += snprintf(line + n, line_size - (size_t)n, "\"hex\":\"");
            n += watch_hex(line + n, bytes, length);
            n += snprintf(line + n, line_size - (size_t)n, "\"}\r\n");
        } else {
            n += snprintf(line + n, line_size - (size_t)n, "\"diff\":[");
            int first = 1;
            for (uint32_t i = 0; i < length;) {
                if (watch->previous[i] == bytes[i]) { ++i; continue; }
                uint32_t end = i + 1, last = i + 1;
                while (end < length && end - last < ES2_MEM_WATCH_MERGE_GAP) {
                    if (watch->previous[end] != bytes[end]) last = end + 1;
                    ++end;
                }
                if ((size_t)n + 2u * (last - i) + 32u > line_size) break;
                n += snprintf(line + n, line_size - (size_t)n, "%s[%lu,\"", first ? "" : ",", (unsigned long)i);
                n += watch_hex(line + n, bytes + i, last - i);
                n += snprintf(line + n, line_size - (size_t)n, "\"]");
                first = 0;
                i = last;
            }
            n += snprintf(line + n, line_size - (size_t)n, "]}\r\n");
        }
        access->emit(access->user, line, n);
        phase_end(access, phase);
        phase = phase_begin(access, ES2_MEM_WATCH_PHASE_WATCH);
        memcpy(watch->previous, bytes, length);
        watch->previous_length = length;
        watch->previous_address = address;
        watch->previous_unreadable = 0;
        phase_end(access, phase);
    }
}
