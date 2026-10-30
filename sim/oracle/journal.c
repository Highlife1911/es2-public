/* The input journal of the oracle; see journal.h. The proxy's input_journal.h
 * uses it; the proxy's input_journal_* fixtures guard the parsing, the
 * consumption order and the counters. */
#include "sim/oracle/journal.h"
#include <stdlib.h>
#include <string.h>

const char *const es2_journal_api_names[ES2_JOURNAL_API_COUNT] = {"msg", "key", "joycaps", "joypos", "cursor"};

void es2_journal_init(es2_journal *j) {
    memset(j, 0, sizeof(*j));
    j->mouse_x = 320;
    j->mouse_y = 240;
    j->pump_frame = 0xffffffffu;
}

void es2_journal_free(es2_journal *j) {
    for (uint32_t i = 0; i < j->count; ++i) free(j->entries[i].blob);
    free(j->entries);
    j->entries = NULL;
    j->count = 0;
    memset(j->cursor, 0, sizeof(j->cursor));
}

int es2_journal_is_input_message(uint32_t message) {
    return message == ES2_WM_KEYDOWN || message == ES2_WM_KEYUP || message == ES2_WM_SYSKEYDOWN ||
           message == ES2_WM_SYSKEYUP || (message >= ES2_WM_MOUSEMOVE && message <= ES2_WM_MOUSELAST);
}

/* ---- parsing */
static const char *field(const char *line, const char *end, const char *name) {
    size_t length = strlen(name);
    for (const char *p = line; p + length + 3 < end; ++p)
        if (p[0] == '"' && !memcmp(p + 1, name, length) && p[1 + length] == '"') {
            /* Tolerate the spacing of common JSON writers (`"key": value`). */
            const char *value = p + 2 + length;
            while (value < end && (*value == ' ' || *value == '\t')) ++value;
            if (value < end && *value == ':') {
                ++value;
                while (value < end && (*value == ' ' || *value == '\t')) ++value;
                return value;
            }
        }
    return NULL;
}

static int32_t number(const char *line, const char *end, const char *name, int32_t fallback) {
    const char *value = field(line, end, name);
    return value ? (int32_t)strtol(value, NULL, 10) : fallback;
}

static int nibble(char c) {
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

int es2_journal_parse_line(const char *line, const char *end, es2_journal_entry *entry, es2_journal_vk_scan scan) {
    const char *api = field(line, end, "api");
    if (!api || *api != '"') return 0;
    ++api;
    memset(entry, 0, sizeof(*entry));
    entry->frame = (uint32_t)number(line, end, "f", 0);
    entry->pump = (uint32_t)number(line, end, "p", 0);
    if (!strncmp(api, "keydown\"", 8) || !strncmp(api, "keyup\"", 6)) {
        int down = api[3] == 'd';
        int32_t vk = number(line, end, "vk", 0);
        uint32_t code = scan ? scan((unsigned)vk) : 0u;
        entry->api = ES2_JOURNAL_MSG;
        entry->a = down ? (int32_t)ES2_WM_KEYDOWN : (int32_t)ES2_WM_KEYUP;
        entry->b = vk;
        entry->c = (int32_t)(1u | (code << 16) | (down ? 0u : 0xc0000000u));
        return vk > 0 && vk < 256;
    }
    if (!strncmp(api, "mousedown\"", 10) || !strncmp(api, "mouseup\"", 8) || !strncmp(api, "mousemove\"", 10)) {
        int32_t x = number(line, end, "x", -1), y = number(line, end, "y", -1);
        entry->api = ES2_JOURNAL_MSG;
        entry->a = api[5] == 'd' ? (int32_t)ES2_WM_LBUTTONDOWN : api[5] == 'u' ? (int32_t)ES2_WM_LBUTTONUP : (int32_t)ES2_WM_MOUSEMOVE;
        entry->b = api[5] == 'd' ? (int32_t)ES2_MK_LBUTTON : 0;
        entry->c = (int32_t)((uint32_t)(uint16_t)x | (uint32_t)(uint16_t)y << 16);
        return x >= 0 && x < 0x8000 && y >= 0 && y < 0x8000;
    }
    for (uint32_t i = 0; i < ES2_JOURNAL_API_COUNT; ++i) {
        size_t length = strlen(es2_journal_api_names[i]);
        if (!strncmp(api, es2_journal_api_names[i], length) && api[length] == '"') {
            entry->api = i;
            entry->a = number(line, end, "a", 0);
            entry->b = number(line, end, "b", 0);
            entry->c = number(line, end, "c", 0);
            entry->d = number(line, end, "d", 0);
            const char *hex = field(line, end, "hex");
            if (hex && *hex == '"') {
                const char *start = hex + 1, *stop = start;
                while (stop < end && *stop != '"') ++stop;
                uint32_t size = (uint32_t)(stop - start) / 2u;
                if (size && size <= ES2_JOURNAL_BLOB_MAX && (entry->blob = malloc(size))) {
                    for (uint32_t k = 0; k < size; ++k) {
                        int high = nibble(start[2 * k]), low = nibble(start[2 * k + 1]);
                        if (high < 0 || low < 0) {
                            free(entry->blob);
                            entry->blob = NULL;
                            return 0;
                        }
                        entry->blob[k] = (uint8_t)(high << 4 | low);
                    }
                    entry->blob_size = size;
                }
            }
            return 1;
        }
    }
    return 0;
}

int es2_journal_load_text(es2_journal *j, const char *data, size_t size, es2_journal_vk_scan scan) {
    char *text = malloc(size + 1u);
    if (!text) return 1;
    memcpy(text, data, size);
    text[size] = 0;
    size_t lines = 1;
    for (size_t i = 0; i < size; ++i) lines += text[i] == '\n';
    es2_journal_entry *entries = calloc(lines, sizeof(*entries));
    if (!entries) { free(text); return 1; }
    es2_journal_free(j);
    j->entries = entries;
    for (char *line = text; *line;) {
        char *end = strchr(line, '\n');
        if (!end) end = text + size;
        if (es2_journal_parse_line(line, end, &j->entries[j->count], scan)) ++j->count;
        line = *end ? end + 1 : end;
    }
    free(text);
    return 0;
}

/* ---- replay */
void es2_journal_count_pump(es2_journal *j, uint32_t frame) {
    if (frame != j->pump_frame) { j->pump_frame = frame; j->pump_count = 0; }
    else if (j->pump_count != 0xffffffffu) ++j->pump_count;
}

es2_journal_entry *es2_journal_take(es2_journal *j, uint32_t api, uint32_t frame, es2_journal_match match, int32_t key) {
    uint32_t *cursor = &j->cursor[api];
    while (*cursor < j->count) {
        es2_journal_entry *entry = &j->entries[*cursor];
        if (entry->api != api) { ++*cursor; continue; }
        if (entry->frame < frame && api != ES2_JOURNAL_MSG) { ++*cursor; continue; }
        if (entry->frame > frame) return NULL;
        if (api == ES2_JOURNAL_MSG && entry->frame == frame && entry->pump > j->pump_count) return NULL;
        if (match && !match(entry, key)) return NULL;
        ++*cursor;
        return entry;
    }
    return NULL;
}

int es2_journal_match_first(const es2_journal_entry *entry, int32_t key) { return entry->a == key; }

void es2_journal_apply_message(es2_journal *j, uint32_t message, uint32_t wparam, uint32_t lparam) {
    if (wparam < 256u) {
        if (message == ES2_WM_KEYDOWN || message == ES2_WM_SYSKEYDOWN) {
            if (!(j->keys[wparam] & 0x80)) j->keys[wparam] ^= 1;
            j->keys[wparam] |= 0x80;
        } else if (message == ES2_WM_KEYUP || message == ES2_WM_SYSKEYUP) {
            j->keys[wparam] &= (uint8_t)~0x80;
        }
    }
    if (message >= ES2_WM_MOUSEMOVE && message <= ES2_WM_MOUSELAST) {
        j->mouse_x = (int16_t)(uint16_t)lparam;
        j->mouse_y = (int16_t)(uint16_t)(lparam >> 16);
    }
}

int16_t es2_journal_key_fallback(es2_journal *j, int32_t vk) {
    ++j->fallbacks;
    uint8_t state = vk >= 0 && vk < 256 ? j->keys[vk] : 0;
    return (int16_t)((state & 0x80 ? 0x8000 : 0) | (state & 1));
}
