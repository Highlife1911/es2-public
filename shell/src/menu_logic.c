/* Portable parts of es2menu; see menu.h. */
#include "menu.h"
#include "es2/shell/bytes.h"
#include "es2/formats/sav.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


int es2m_parse_args(int argc, char **argv, es2m_args *out, char *message, size_t capacity) {
    es2m_args a;
    int i, have_state = 0;
    memset(&a, 0, sizeof a);
    strcpy(a.language, "ENG");
    for (i = 1; i < argc; i++) {
        const char *t = argv[i];
        if (!strcmp(t, "--menu") && i + 1 < argc) a.menu_dir = argv[++i];
        else if (!strcmp(t, "--input") && i + 1 < argc) a.input = argv[++i];
        else if (!strcmp(t, "-a") || !strcmp(t, "--no-movies")) a.no_movies = 1;
        else if (!strcmp(t, "dummy") || !strcmp(t, "-eggplant") || !strcmp(t, "-s") || !strcmp(t, "-@")) continue;
        else if (t[0] == '-' && t[1] == 'X') {
            char *end;
            long v = strtol(t + 2, &end, 10);
            if (have_state || end == t + 2 || *end || v < -32768 || v > 32767) {
                snprintf(message, capacity, "bad or repeated shell state %s", t);
                return -1;
            }
            a.state = (int)v;
            have_state = 1;
        } else if (t[0] == '-' && t[1] && !t[2] && (t[1] == 'G' || t[1] == 'F' || t[1] == 'E')) {
            strcpy(a.language, t[1] == 'G' ? "GER" : t[1] == 'F' ? "FRE" : "ENG");
        } else {
            snprintf(message, capacity, "unexpected argument %s", t);
            return -1;
        }
    }
    if (!a.menu_dir || !have_state) {
        snprintf(message, capacity, "usage: es2menu --menu DIR [--input FILE] dummy -eggplant -X<state> [-<language>]");
        return -1;
    }
    *out = a;
    return 0;
}

es2_status es2m_bin_raw(es2_span c, size_t index, es2_span *text, es2_error *e) {
    size_t n, base, offset;
    const uint8_t *start, *end;
    if (!c.data || !text) return es2_fail(e, ES2_ERR_ARGUMENT, 0);
    if (c.size < 8) return es2_fail(e, ES2_ERR_TRUNCATED, 0);
    n = es2_le16(c.data) | (size_t)es2_le16(c.data + 2) << 16;
    if (n > (c.size - 8) / 2 || index >= n) return es2_fail(e, ES2_ERR_COUNT, index);
    base = 8 + 2 * n;
    offset = es2_le16(c.data + 8 + 2 * index);
    if (offset >= c.size - base) return es2_fail(e, ES2_ERR_OFFSET, index);
    start = c.data + base + offset;
    end = memchr(start, 0, c.size - base - offset);
    if (!end) return es2_fail(e, ES2_ERR_STRING, index);
    *text = (es2_span){start, (size_t)(end - start)};
    es2_error_clear(e);
    return ES2_OK;
}

es2_status es2m_bin_string(es2_span c, size_t index, es2_span *text, es2_error *e) {
    es2_span raw;
    es2_status status = es2m_bin_raw(c, index, &raw, e);
    if (status != ES2_OK) return status;
    while (raw.size && (raw.data[raw.size - 1] == 13 || raw.data[raw.size - 1] == 10)) raw.size--;   /* CR, LF */
    *text = raw;
    return ES2_OK;
}

es2_status es2m_save_info_read(es2_span sav, es2m_save_info *out, es2_error *e) {
    es2_sav s;
    es2m_save_info info;
    size_t n;
    es2_status status = es2_sav_parse(sav, &s, e);
    if (status != ES2_OK) return status;
    if (!out) return es2_fail(e, ES2_ERR_ARGUMENT, 0);
    memset(&info, 0, sizeof info);
    n = s.player.name.size ? s.player.name.size - 1 : 0;
    if (n >= sizeof info.name) n = sizeof info.name - 1;
    memcpy(info.name, s.player.name.data, n);
    info.name[n] = 0;
    info.rank = (uint16_t)es2_le16(s.player.suffix.data + 3);
    info.group = es2_le16s(s.selection.data);
    info.position = es2_le16s(s.selection.data + 2);
    info.final_word = es2_le16s(s.final_fields.data);
    info.salvage = es2_le16(s.tally.data) | (uint32_t)es2_le16(s.tally.data + 2) << 16;
    info.image_size = s.reader_end;
    *out = info;
    return ES2_OK;
}
