/* The INI reader; see es2/platform/ini.h. The rules follow the API as
 * platform/tests/ini_test.c observes it on Windows. */
#include <stdlib.h>
#include <string.h>
#include "es2/platform/ini.h"
#include "es2/platform/os.h"

/* Windows-1252 0x80..0x9f; the rest of the page is Latin-1. Undefined
 * positions map to the C1 control of the same value, as MultiByteToWideChar
 * does. */
static const uint16_t cp1252_high[32] = {
    0x20ac, 0x0081, 0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021, 0x02c6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008d, 0x017d, 0x008f,
    0x0090, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014, 0x02dc, 0x2122, 0x0161, 0x203a, 0x0153, 0x009d, 0x017e, 0x0178};

size_t es2_ini_decode(const uint8_t *bytes, size_t count, uint16_t *out) {
    size_t n = 0;
    if (count >= 2 && bytes[0] == 0xff && bytes[1] == 0xfe) {
        for (size_t i = 2; i + 1 < count; i += 2) out[n++] = (uint16_t)(bytes[i] | bytes[i + 1] << 8);
        return n;
    }
    for (size_t i = 0; i < count; ++i) out[n++] = bytes[i] >= 0x80 && bytes[i] < 0xa0 ? cp1252_high[bytes[i] - 0x80] : bytes[i];
    return n;
}

static int blank(uint16_t c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f' || c == 0x1a; }
static uint16_t fold(uint16_t c) { return c >= 'a' && c <= 'z' ? (uint16_t)(c - 32) : c; }
static size_t length(const uint16_t *s) {
    size_t n = 0;
    while (s[n]) ++n;
    return n;
}
/* [a, a+n) equals the 0-terminated b without case (ASCII folding). */
static int same(const uint16_t *a, size_t n, const uint16_t *b, size_t bn) {
    if (n != bn) return 0;
    for (size_t i = 0; i < n; ++i)
        if (fold(a[i]) != fold(b[i])) return 0;
    return 1;
}
static void trim(const uint16_t **start, const uint16_t **end) {
    while (*start < *end && blank(**start)) ++*start;
    while (*end > *start && blank((*end)[-1])) --*end;
}

/* Finds the entry's value [*value, *value_end). Lines end at '\n' or '\r'.
 * A line starting with '[' names a section up to the first ']' or the line's
 * end. Only the first section of a name is searched, and in it the first key
 * of a name. A line without '=' is a key without a value, which reads as
 * missing. */
static int find(const uint16_t *text, size_t count, const uint16_t *section, const uint16_t *key, const uint16_t **value,
                const uint16_t **value_end) {
    const uint16_t *want_s = section, *want_s_end = section + length(section);
    const uint16_t *want_k = key, *want_k_end = key + length(key);
    trim(&want_s, &want_s_end);
    trim(&want_k, &want_k_end);
    const uint16_t *p = text, *end = text + count;
    int in_section = 0;
    while (p < end) {
        const uint16_t *line = p, *line_end = p;
        while (line_end < end && *line_end != '\n' && *line_end != '\r') ++line_end;
        p = line_end < end ? line_end + 1 : end;
        trim(&line, &line_end);
        if (line >= line_end) continue;
        if (*line == '[') {
            if (in_section) return 0;
            const uint16_t *name = line + 1, *name_end = name;
            while (name_end < line_end && *name_end != ']') ++name_end;
            trim(&name, &name_end);
            in_section = same(name, (size_t)(name_end - name), want_s, (size_t)(want_s_end - want_s));
            continue;
        }
        if (!in_section) continue;
        const uint16_t *name = line, *name_end = line_end, *v = NULL, *v_end = NULL;
        for (const uint16_t *q = line; q < line_end; ++q) {
            if (*q == '=') {
                name_end = q;
                v = q + 1;
                v_end = line_end;
                break;
            }
        }
        trim(&name, &name_end);
        if (v) trim(&v, &v_end);
        if (same(name, (size_t)(name_end - name), want_k, (size_t)(want_k_end - want_k))) {
            if (!v) return 0;
            *value = v;
            *value_end = v_end;
            return 1;
        }
    }
    return 0;
}

/* The copy with one pair of matching quotes removed. */
static uint32_t copy(const uint16_t *v, const uint16_t *v_end, uint16_t *out, uint32_t capacity) {
    if (v_end - v >= 2 && (*v == '"' || *v == '\'') && v_end[-1] == *v) {
        ++v;
        --v_end;
    }
    uint32_t n = 0;
    while (v < v_end && n + 1 < capacity) out[n++] = *v++;
    out[n] = 0;
    return n;
}

uint32_t es2_ini_string(const uint16_t *text, size_t count, const uint16_t *section, const uint16_t *key,
                        const uint16_t *fallback, uint16_t *out, uint32_t capacity) {
    static const uint16_t empty[1] = {0};
    if (!capacity) return 0;
    const uint16_t *v, *v_end;
    if (text && section && key && find(text, count, section, key, &v, &v_end)) return copy(v, v_end, out, capacity);
    if (!fallback) fallback = empty;
    const uint16_t *f_end = fallback + length(fallback);
    while (f_end > fallback && f_end[-1] == ' ') --f_end;
    uint32_t n = 0;
    while (fallback < f_end && n + 1 < capacity) out[n++] = *fallback++;
    out[n] = 0;
    return n;
}

uint32_t es2_ini_parse_int(const uint16_t *s) {
    while (*s && *s <= ' ') ++s;
    int negative = 0;
    if (*s == '+' || *s == '-') negative = *s++ == '-';
    uint32_t base = 10;
    if (s[0] == '0' && s[1] == 'x') { base = 16; s += 2; }
    else if (s[0] == '0' && s[1] == 'o') { base = 8; s += 2; }
    else if (s[0] == '0' && s[1] == 'b') { base = 2; s += 2; }
    uint32_t value = 0;
    for (;; ++s) {
        uint32_t c = *s, digit;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'a' && c <= 'z') digit = c - 'a' + 10u;
        else if (c >= 'A' && c <= 'Z') digit = c - 'A' + 10u;
        else break;
        if (digit >= base) break;
        value = value * base + digit;
    }
    return negative ? 0u - value : value;
}

uint32_t es2_ini_int(const uint16_t *text, size_t count, const uint16_t *section, const uint16_t *key,
                     int32_t fallback) {
    static const uint16_t empty[1] = {0};
    uint16_t buffer[256];
    if (!es2_ini_string(text, count, section, key, empty, buffer, 256)) return (uint32_t)fallback;
    return es2_ini_parse_int(buffer);
}

/* ---- files */
static uint16_t *load(es2_os_handle h, size_t *count) {
    uint32_t size = es2_os_file_size(h), got = 0;
    uint8_t *bytes = size != ES2_OS_INVALID_FILE_SIZE && size <= ES2_INI_FILE_MAX ? malloc(size ? size : 1u) : NULL;
    uint16_t *text = bytes ? malloc(((size_t)size + 1u) * sizeof(uint16_t)) : NULL;
    int ok = text && (!size || (es2_os_read_file(h, bytes, size, &got) && got == size));
    es2_os_close_handle(h);
    if (ok) *count = es2_ini_decode(bytes, size, text);
    free(bytes);
    if (!ok) { free(text); return NULL; }
    return text;
}
static uint16_t *load_utf16(const uint16_t *file, size_t *count) {
    es2_os_handle h = file ? es2_os_create_file_utf16(file, ES2_OS_GENERIC_READ, ES2_OS_FILE_SHARE_READ | ES2_OS_FILE_SHARE_WRITE, 0,
                                                     ES2_OS_OPEN_EXISTING, 0)
                           : ES2_OS_INVALID_HANDLE;
    return h == ES2_OS_INVALID_HANDLE ? NULL : load(h, count);
}

uint32_t es2_ini_file_string(const uint16_t *file, const uint16_t *section, const uint16_t *key,
                             const uint16_t *fallback, uint16_t *out, uint32_t capacity) {
    size_t count = 0;
    uint16_t *text = load_utf16(file, &count);
    uint32_t n = es2_ini_string(text, count, section, key, fallback, out, capacity);
    free(text);
    return n;
}

uint32_t es2_ini_file_int(const uint16_t *file, const uint16_t *section, const uint16_t *key, int32_t fallback) {
    size_t count = 0;
    uint16_t *text = load_utf16(file, &count);
    uint32_t n = es2_ini_int(text, count, section, key, fallback);
    free(text);
    return n;
}

static void ascii16(const char *in, uint16_t *out, size_t capacity) {
    size_t i = 0;
    for (; in && in[i] && i + 1 < capacity; ++i) out[i] = (unsigned char)in[i];
    out[i] = 0;
}
static char ansi_of(uint16_t c) {
    if (c < 0x80 || (c >= 0xa0 && c < 0x100)) return (char)c;
    for (unsigned i = 0; i < 32u; ++i)
        if (cp1252_high[i] == c) return (char)(0x80 + i);
    return '?';
}

uint32_t es2_ini_file_string_ansi(const char *file, const char *section, const char *key, const char *fallback,
                                  char *out, uint32_t capacity) {
    if (!capacity) return 0;
    uint16_t s[256], k[256], f[1024], value[4096];
    ascii16(section, s, 256);
    ascii16(key, k, 256);
    ascii16(fallback, f, 1024);
    es2_os_handle h = file ? es2_os_create_file(file, ES2_OS_GENERIC_READ, ES2_OS_FILE_SHARE_READ | ES2_OS_FILE_SHARE_WRITE, 0,
                                                ES2_OS_OPEN_EXISTING, 0)
                           : ES2_OS_INVALID_HANDLE;
    size_t count = 0;
    uint16_t *text = h == ES2_OS_INVALID_HANDLE ? NULL : load(h, &count);
    uint32_t cap = capacity < 4096u ? capacity : 4096u;
    uint32_t n = es2_ini_string(text, count, section ? s : NULL, key ? k : NULL, fallback ? f : NULL, value, cap);
    free(text);
    for (uint32_t i = 0; i < n; ++i) out[i] = ansi_of(value[i]);
    out[n] = 0;
    return n;
}
