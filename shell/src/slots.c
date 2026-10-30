/* VSHELL save slots; contract in es2/shell/slots.h and vshell_ui.md
 * section 4.3. */
#include "es2/shell/slots.h"
#include "es2/shell/bytes.h"
#include "es2/shell/gui/text.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


es2_status es2_shell_slots_load(es2_span file, es2_span empty, es2_shell_slot_table *out, es2_error *e) {
    es2_shell_slot_table t;
    es2_reader r;
    es2_status s;
    uint32_t size;
    if (!out || (!file.data && file.size) || (!empty.data && empty.size)) return es2_fail(e, ES2_ERR_ARGUMENT, 0);
    memset(&t, 0, sizeof t);
    if ((s = es2_reader_init(&r, file, e)) != ES2_OK || (s = es2_read_u32(&r, &size)) != ES2_OK) return s;
    for (int group = 0; group < 2; group++) {
        int16_t count;
        if ((s = es2_read_i16(&r, &count)) != ES2_OK) return s;
        if (count > ES2_SHELL_SLOT_COUNT) return es2_fail(e, ES2_ERR_COUNT, r.offset - 2);
        for (int i = 0; i < count; i++) {
            es2_shell_slot *slot = &t.slot[i];
            char *field = group ? slot->text : slot->file;
            size_t limit = group ? ES2_SHELL_SLOT_TEXT : ES2_SHELL_SLOT_FILE;
            int16_t length;
            es2_span raw;
            uint8_t tag;
            if ((s = es2_read_i16(&r, &length)) != ES2_OK) return s;
            if (length < 0 || (size_t)length > limit) return es2_fail(e, ES2_ERR_LENGTH, r.offset - 2);
            if ((s = es2_read_span(&r, (size_t)length, &raw)) != ES2_OK) return s;
            memcpy(field, raw.data, raw.size);
            if (!memchr(field, 0, limit)) return es2_fail(e, ES2_ERR_STRING, r.offset - raw.size);
            if (group && !slot->text[4]) {
                /* 0x40e482..0x40e4ab: strcat of ESTEXT 0x21 */
                size_t n = strlen(slot->text);
                const uint8_t *nul = empty.size ? memchr(empty.data, 0, empty.size) : NULL;
                size_t add = nul ? (size_t)(nul - empty.data) : empty.size;
                if (n + add + 1 > ES2_SHELL_SLOT_TEXT) return es2_fail(e, ES2_ERR_STRING, r.offset - raw.size);
                if (add && !empty.data) return es2_fail(e, ES2_ERR_ARGUMENT, 0);
                memcpy(slot->text + n, empty.data, add);
                slot->text[n + add] = 0;
            }
            if ((s = es2_read_u8(&r, &tag)) != ES2_OK) return s;
            if (group) slot->flag = tag;
        }
    }
    (void)size;
    *out = t;
    es2_error_clear(e);
    return ES2_OK;
}

es2_status es2_shell_slots_write(const es2_shell_slot_table *t, es2_span previous, uint8_t *output, size_t capacity,
                                 size_t *required, es2_error *e) {
    size_t image = 4, total, at;
    if (!t || !required || (!previous.data && previous.size)) return es2_fail(e, ES2_ERR_ARGUMENT, 0);
    for (int group = 0; group < 2; group++) {
        image += 2;
        for (int i = 0; i < ES2_SHELL_SLOT_COUNT; i++) {
            const char *field = group ? t->slot[i].text : t->slot[i].file;
            size_t limit = group ? ES2_SHELL_SLOT_TEXT : ES2_SHELL_SLOT_FILE;
            if (!memchr(field, 0, limit)) return es2_fail(e, ES2_ERR_STRING, (size_t)i);
            image += 2 + strlen(field) + 1 + 1;
        }
    }
    total = previous.size > image ? previous.size : image;
    if (total - 4 > 0xffffffffu) return es2_fail(e, ES2_ERR_LENGTH, total);
    if (!output || !capacity) { *required = total; es2_error_clear(e); return ES2_OK; }
    if (capacity < total) { *required = total; return es2_fail(e, ES2_ERR_CAPACITY, total); }
    if (previous.size && output < previous.data + previous.size && previous.data < output + capacity)
        return es2_fail(e, ES2_ERR_OVERLAP, 0);
    /* Non-truncating rewrite (open 0x8102): old bytes stay after the image. */
    if (previous.size > image) memcpy(output + image, previous.data + image, previous.size - image);
    at = 4;
    for (int group = 0; group < 2; group++) {
        es2_put16(output + at, ES2_SHELL_SLOT_COUNT);
        at += 2;
        for (int i = 0; i < ES2_SHELL_SLOT_COUNT; i++) {
            const char *field = group ? t->slot[i].text : t->slot[i].file;
            size_t n = strlen(field) + 1;
            es2_put16(output + at, n);
            memcpy(output + at + 2, field, n);
            output[at + 2 + n] = group ? t->slot[i].flag : 0;
            at += 3 + n;
        }
    }
    es2_put32(output, total - 4);   /* 0x40e66d..0x40e69f: file length - 4 */
    *required = total;
    es2_error_clear(e);
    return ES2_OK;
}

int es2_shell_slot_effective(int slot, int16_t mode) {
    return slot == 10 && mode == 0 ? 11 : slot;
}

es2_status es2_shell_slots_mark(es2_shell_slot_table *t, int slot, const char *text, es2_error *e) {
    if (!t || slot < 0 || slot >= ES2_SHELL_SLOT_COUNT) return es2_fail(e, ES2_ERR_ARGUMENT, (size_t)(slot < 0 ? 0 : slot));
    if (slot < 10) {
        size_t n;
        /* strcpy(text, NULL) faults in the original (0x40e6ea); its UI always
         * passes the row text for slots 0..9. */
        if (!text) return es2_fail(e, ES2_ERR_ARGUMENT, (size_t)slot);
        n = strlen(text);
        if (n + 1 > ES2_SHELL_SLOT_TEXT) return es2_fail(e, ES2_ERR_STRING, n);
        memcpy(t->slot[slot].text, text, n + 1);
    }
    t->slot[slot].flag = 1;
    es2_error_clear(e);
    return ES2_OK;
}

es2_status es2_shell_slot_row(int slot, const char *typed, char out[ES2_SHELL_SLOT_TEXT], es2_error *e) {
    static const char allowed[] = "^0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ ";
    size_t n;
    if (!typed || !out || slot < 0 || slot > 9) return es2_fail(e, ES2_ERR_ARGUMENT, 0);
    n = strlen(typed);
    if (n > ES2_SHELL_SLOT_NAME_MAX) return es2_fail(e, ES2_ERR_LENGTH, n);
    for (size_t i = 0; i < n; i++)
        if (!memchr(allowed, typed[i], sizeof allowed - 1)) return es2_fail(e, ES2_ERR_STRING, i);
    snprintf(out, ES2_SHELL_SLOT_TEXT, "%2d. %s", slot + 1, typed);
    es2_error_clear(e);
    return ES2_OK;
}

es2_status es2_shell_slot_gamefile(es2_span file, es2_span empty, int slot, const char *text, int16_t mode,
                                   int *effective, char file_name[ES2_SHELL_SLOT_FILE], uint8_t *output,
                                   size_t capacity, size_t *required, es2_error *e) {
    es2_shell_slot_table t;
    es2_status s;
    int k;
    if (!effective || !file_name || !required || slot < 0 || slot >= ES2_SHELL_SLOT_COUNT)
        return es2_fail(e, ES2_ERR_ARGUMENT, 0);
    k = es2_shell_slot_effective(slot, mode);
    if ((s = es2_shell_slots_load(file, empty, &t, e)) != ES2_OK) return s;
    if ((s = es2_shell_slots_mark(&t, k, text, e)) != ES2_OK) return s;
    /* "sav\" plus an empty name is no file: the original's open fails and
     * its assertion stops the game (0x40e9a4). */
    if (!t.slot[k].file[0]) return es2_fail(e, ES2_ERR_NOT_FOUND, (size_t)k);
    if ((s = es2_shell_slots_write(&t, file, output, capacity, required, e)) != ES2_OK) return s;
    *effective = k;
    memcpy(file_name, t.slot[k].file, ES2_SHELL_SLOT_FILE);
    return ES2_OK;
}

es2_status es2_shell_slot_companion(int slot, int index, char *slot_name, size_t capacity, const char **data_name,
                                    es2_error *e) {
    static const char *const data[3] = {"script.dat", "mission.str", "player.mec"};
    static const char *const format[3] = {"script%d.dat", "missn%d.str", "player%d.mec"};
    int n;
    if (slot < 0 || slot >= ES2_SHELL_SLOT_COUNT || index < 0 || index > 2 || !slot_name || !data_name)
        return es2_fail(e, ES2_ERR_ARGUMENT, 0);
    n = snprintf(slot_name, capacity, format[index], slot);
    if (n < 0 || (size_t)n >= capacity) return es2_fail(e, ES2_ERR_CAPACITY, (size_t)(n < 0 ? 0 : n));
    *data_name = data[index];
    es2_error_clear(e);
    return ES2_OK;
}

/* ---- Slot statistics (stats.cpp; vshell_ui.md section 13) --------- */
typedef struct stats_cursor {
    es2_span sav;
    long at;
} stats_cursor;
/* The stream seek 0x44ef10 to tell + delta; a target outside the file is
 * refused (the original's following reads would fail). */
static int stats_skip(stats_cursor *c, long delta) {
    long to = c->at + delta;
    if (to < 0 || (size_t)to > c->sav.size) return 0;
    c->at = to;
    return 1;
}
/* The stream read (vtable +0x18) of n bytes into out. */
static int stats_take(stats_cursor *c, uint8_t *out, size_t n) {
    if ((size_t)c->at > c->sav.size || c->sav.size - (size_t)c->at < n) return 0;
    if (n) memcpy(out, c->sav.data + c->at, n);
    c->at += (long)n;
    return 1;
}
static int stats_word(stats_cursor *c, int16_t *v) {
    uint8_t b[2];
    if (!stats_take(c, b, 2)) return 0;
    *v = (int16_t)(b[0] | b[1] << 8);
    return 1;
}
static int16_t stats_get16(const uint8_t *p) { return (int16_t)(p[0] | p[1] << 8); }

/* Original: VSHELL.EXE 0x4309f0 (one iteration of its slot loop
 * 0x430a14..0x430e9c) with the pilot reader 0x410520. */
es2_status es2_shell_stats_read(es2_span sav, es2_shell_stats *record, es2_error *e) {
    stats_cursor c = {sav, 0};
    es2_shell_stats r;
    int16_t n;
    if (!record || (!sav.data && sav.size)) return es2_fail(e, ES2_ERR_ARGUMENT, 0);
    r = *record;
    /* 0x430a74..0x430aca: 33 blocks, 1 byte, count word, 10 bytes each */
    for (int i = 0; i < 0x21; i++)
        if (!stats_skip(&c, 1) || !stats_word(&c, &n) || !stats_skip(&c, 10L * n)) goto truncated;
    /* 0x430acc..0x430b49: 2 + 10 + 10 bytes, then the CAREER position */
    if (!stats_skip(&c, 22) || !stats_take(&c, r.raw + 0x3f, 2) || !stats_take(&c, r.raw + 0x41, 2)) goto truncated;
    /* 0x430b4c..0x430bff: 2, 0x14, 2, 0x3c, 2, 0x3c, 2 */
    if (!stats_skip(&c, 148)) goto truncated;
    /* 0x430c02..0x430c9f: 36 roster pilots: 4, name length word, name, 2, 1, 0x16 */
    for (int i = 0; i < 0x24; i++)
        if (!stats_skip(&c, 4) || !stats_word(&c, &n) || !stats_skip(&c, n) || !stats_skip(&c, 25)) goto truncated;
    /* 0x430ca5..0x430d0a: 6, 6, 2, 2 */
    if (!stats_skip(&c, 16)) goto truncated;
    /* 0x410520: +0, +2, name length (local), name at +4, +0x22, byte +0x24,
     * eleven words +0x25..+0x39 */
    if (!stats_take(&c, r.raw, 2) || !stats_take(&c, r.raw + 2, 2) || !stats_word(&c, &n)) goto truncated;
    if (n < 0 || n > 0x3f - 4) return es2_fail(e, ES2_ERR_LENGTH, (size_t)c.at - 2);
    if (!stats_take(&c, r.raw + 4, (size_t)n) || !stats_take(&c, r.raw + 0x22, 2) || !stats_take(&c, r.raw + 0x24, 1))
        goto truncated;
    for (int k = 0; k < 11; k++)
        if (!stats_take(&c, r.raw + 0x25 + 2 * k, 2)) goto truncated;
    /* 0x430d23..0x430e59: hangar records 2, 2, 2, 0x42, 2, 2, 2, word m, 2m, 10m */
    {
        int16_t count, m;
        if (!stats_word(&c, &count)) goto truncated;
        for (int16_t i = 0; i < count; i++)
            if (!stats_skip(&c, 0x4e) || !stats_word(&c, &m) || !stats_skip(&c, 2L * m) || !stats_skip(&c, 10L * m))
                goto truncated;
    }
    /* 0x430e5f..0x430e8c: 0x12 bytes, then the salvage dword */
    if (!stats_skip(&c, 0x12) || !stats_take(&c, r.raw + 0x3b, 4)) goto truncated;
    *record = r;
    es2_error_clear(e);
    return ES2_OK;
truncated:
    return es2_fail(e, ES2_ERR_TRUNCATED, (size_t)(c.at < 0 ? 0 : c.at));
}

/* Original: VSHELL.EXE 0x430ec4 */
es2_status es2_shell_stats_refresh(es2_shell_stats *record, const es2_shell_stats *memory, es2_error *e) {
    static const uint8_t words[] = {0x25, 0x29, 0x2d, 0x2f, 0x31, 0x33, 0x35, 0x37, 0x3f, 0x41};
    const uint8_t *nul;
    if (!record || !memory) return es2_fail(e, ES2_ERR_ARGUMENT, 0);
    nul = memchr(memory->raw + 4, 0, 0x1e);
    if (!nul) return es2_fail(e, ES2_ERR_STRING, 4);
    memcpy(record->raw + 4, memory->raw + 4, (size_t)(nul - memory->raw - 4) + 1);
    for (size_t i = 0; i < sizeof words; i++) memcpy(record->raw + words[i], memory->raw + words[i], 2);
    memcpy(record->raw + 0x3b, memory->raw + 0x3b, 4);
    es2_error_clear(e);
    return ES2_OK;
}

/* Original: VSHELL.EXE 0x4377bc (the panel's texts, 0x437803..0x437bc8) */
void es2_shell_stats_show(const es2_shell_stats *record, es2_shell_stats_view *v) {
    static const uint8_t kills[6] = {0x2d, 0x33, 0x2f, 0x35, 0x31, 0x37};
    const uint8_t *p = record->raw;
    const uint8_t *nul = memchr(p + 4, 0, 0x1e);
    size_t n = nul ? (size_t)(nul - p - 4) : 0x1e;
    int32_t salvage = (int32_t)((uint32_t)p[0x3b] | (uint32_t)p[0x3c] << 8 | (uint32_t)p[0x3d] << 16 |
                                (uint32_t)p[0x3e] << 24);
    memcpy(v->name, p + 4, n);
    v->name[n] = 0;
    v->skill_text = (int16_t)(stats_get16(p + 0x25) + 0x35);
    v->rank_text = (int16_t)(stats_get16(p + 0x29) + 0x39);
    for (int i = 0; i < 6; i++) v->kills[i] = stats_get16(p + kills[i]);
    v->salvage = salvage / 1000;
    v->salvage_text = 0x2f;
    v->sector_text = (int16_t)(stats_get16(p + 0x3f) + 0x76);
    v->mission = stats_get16(p + 0x41) + 1;
}

/* The naming row through the row's edit field. Original: VSHELL.EXE
 * 0x437e62 (naming starts: 0x4381cd(slot, "") sets "%2d. "), 0x438f95 (+0xb7
 * := 4, set 0x475869), 0x40c382 (insert, the width rule); rectangles from
 * the layout table (0x438ec7..0x438f23). */
es2_status es2_shell_slot_row_typed(int slot, const char *typed, const struct es2_dfn *font,
                                    char out[ES2_SHELL_SLOT_TEXT], es2_error *e) {
    es2_shell_edit edit;
    es2_shell_text_rect rect = {0, 0, 0, 12};
    char start[8];
    es2_status s;
    if (!typed || !out || !font || slot < 0 || slot > 9) return es2_fail(e, ES2_ERR_ARGUMENT, 0);
    rect.r = slot == 0 || slot == 9 ? ES2_SHELL_EDIT_WIDTH_SLOT_EDGE : ES2_SHELL_EDIT_WIDTH_SLOT;
    es2_shell_edit_init(&edit, rect, font);
    edit.allowed = es2_shell_edit_set_slots;
    edit.prefix = 4;
    snprintf(start, sizeof start, "%2d. ", slot + 1);
    if ((s = es2_shell_edit_set_text(&edit, start, e)) != ES2_OK) return s;
    if ((s = es2_shell_edit_type(&edit, typed, e)) != ES2_OK) return s;
    if (edit.length >= ES2_SHELL_SLOT_TEXT) return es2_fail(e, ES2_ERR_CAPACITY, (size_t)edit.length);
    memcpy(out, edit.text, (size_t)edit.length + 1);
    es2_error_clear(e);
    return ES2_OK;
}
