/* Mission-screen texts (docs/reverse-engineering/vshell_ui.md section 16);
 * see es2/shell/texts.h. */
#include "es2/shell/texts.h"
#include "es2/shell/bytes.h"
#include "es2/formats/sav.h"
#include "es2/formats/str.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int16_t wrap16(long v) { return (int16_t)(uint16_t)(v & 0xffff); }
/* A C string inside a span: the bytes up to the first NUL (or the span). */
static es2_span c_string(es2_span s) {
    const uint8_t *end = s.data ? (const uint8_t *)memchr(s.data, 0, s.size) : NULL;
    if (end) s.size = (size_t)(end - s.data);
    return s;
}

/* ---- Page 0 --------------------------------------------------------------- */
int es2_shell_sector_name_id(int16_t group) {
    /* Original: VSHELL.EXE 0x444a12 (si = [0x46fc58] - 1), 0x444af5 (0x77 + si, movsx) */
    return wrap16((long)ES2_SHELL_SECTOR_FIRST + group - 1);
}

es2_status es2_shell_sector_title(es2_span name, es2_span word, char *out, size_t capacity, es2_error *e) {
    size_t n;
    if (!out || (!name.data && name.size) || (!word.data && word.size)) return es2_fail(e, ES2_ERR_ARGUMENT, 0);
    name = c_string(name);
    word = c_string(word);
    n = name.size + 1 + word.size;
    /* Original: 0x444b17 sprintf("%s %s") into a stack buffer, then setText
     * 0x40b8bb, which asserts above 100 bytes (0x40b905). */
    if (n > 100) return es2_fail(e, ES2_ERR_LENGTH, n);
    if (n >= capacity) return es2_fail(e, ES2_ERR_CAPACITY, n);
    if (name.size) memcpy(out, name.data, name.size);
    out[name.size] = ' ';
    if (word.size) memcpy(out + name.size + 1, word.data, word.size);
    out[n] = 0;
    return ES2_OK;
}

es2_status es2_shell_campaign_text(es2_span campaign_str, int16_t group, es2_span *text, es2_error *e) {
    es2_str str;
    es2_str_string s;
    size_t count;
    es2_status status;
    int index = group - 1;   /* 0x444a19: dec esi */
    if (!text) return es2_fail(e, ES2_ERR_ARGUMENT, 0);
    /* Original: 0x40fd2d reads u32, i16 N, then for every string i16 L, L
     * text bytes (kept only for the wanted index), E, E bytes skipped. */
    if ((status = es2_str_parse(campaign_str, 1, 0, &str, e)) != ES2_OK) return status;
    if ((status = es2_str_group_size(&str, 0, &count, e)) != ES2_OK) return status;
    /* 0x40fe5f..0x40fe64: only index == ebx (0..N-1) allocates the text;
     * otherwise [0x46f520] stays null and 0x40d231 faults in strlen. */
    if (index < 0 || (size_t)index >= count) return es2_fail(e, ES2_ERR_NOT_FOUND, (size_t)(uint16_t)group);
    if ((status = es2_str_string_at(&str, 0, (size_t)index, &s, e)) != ES2_OK) return status;
    /* L bytes copied into a zeroed block; the panel copies up to the NUL. */
    *text = c_string(s.text);
    return ES2_OK;
}

/* ---- Page 1 --------------------------------------------------------------- */
void es2_shell_briefing_texts_free(es2_shell_briefing_texts *out) {
    if (!out) return;
    for (int i = 0; i < ES2_SHELL_TEXT_COUNT; i++) free(out->text[i].data);
    memset(out, 0, sizeof *out);
}

es2_status es2_shell_briefing_texts_compose(es2_span mission_str, const uint8_t selection[ES2_SHELL_BRIEFING_SIZE],
    es2_shell_briefing_texts *out, es2_error *e) {
    /* Reference lists in the order 0x4135bb composes them: briefing 0x4840e4
     * (30), objectives 0x4840ce (10), intelligence 0x484122 (30); offsets in
     * the selection that starts at 0x4840cc. */
    static const size_t at[ES2_SHELL_TEXT_COUNT] = {0x18, 0x02, 0x56};
    static const size_t n[ES2_SHELL_TEXT_COUNT] = {30, 10, 30};
    es2_str str;
    es2_span strings[128];
    size_t count, i, k;
    es2_shell_briefing_texts result;
    es2_status status;
    if (!selection || !out) return es2_fail(e, ES2_ERR_ARGUMENT, 0);
    for (i = 0; i < ES2_SHELL_TEXT_COUNT; i++)
        if (out->text[i].data) return es2_fail(e, ES2_ERR_ARGUMENT, 0);
    /* 0x428521 load; 0x4286e2 with a null aux pointer: E is a plain byte. */
    if ((status = es2_str_parse(mission_str, 1, ES2_STR_TAG_BYTE, &str, e)) != ES2_OK) return status;
    if ((status = es2_str_group_size(&str, 0, &count, e)) != ES2_OK) return status;
    /* 0x42863a returns the count's low byte, sign-extended; the pointer array
     * has that many entries and 0x413650 asserts when the group count differs. */
    if (count > 127) return es2_fail(e, ES2_ERR_COUNT, 4);
    for (i = 0; i < count; i++) {
        es2_str_string s;
        if ((status = es2_str_string_at(&str, 0, i, &s, e)) != ES2_OK) return status;
        strings[i] = c_string(s.text);
    }
    memset(&result, 0, sizeof result);
    for (i = 0; i < ES2_SHELL_TEXT_COUNT; i++) {
        size_t total = 0;
        /* 0x413681..0x4136bc: size 1 + sum of strlen; references index the
         * pointer array unchecked (-1 skipped). */
        for (k = 0; k < n[i]; k++) {
            int16_t ref = es2_le16s(selection + at[i] + 2 * k);
            if (ref == -1) continue;
            if (ref < 0 || (size_t)ref >= count) {
                es2_shell_briefing_texts_free(&result);
                return es2_fail(e, ES2_ERR_OFFSET, at[i] + 2 * k);
            }
            total += strings[ref].size;
        }
        /* 0x40870a returns a zeroed block, so a first reference of -1 leaves
         * an empty string for the strcat calls of 1..n-1 (0x41371d, 0x413754). */
        result.text[i].data = (uint8_t *)malloc(total + 1);
        if (!result.text[i].data) {
            es2_shell_briefing_texts_free(&result);
            return es2_fail(e, ES2_ERR_CAPACITY, 0);
        }
        for (k = 0; k < n[i]; k++) {
            int16_t ref = es2_le16s(selection + at[i] + 2 * k);
            if (ref == -1) continue;
            if (strings[ref].size) memcpy(result.text[i].data + result.text[i].size, strings[ref].data, strings[ref].size);
            result.text[i].size += strings[ref].size;
        }
        result.text[i].data[result.text[i].size] = 0;
    }
    *out = result;
    return ES2_OK;
}

es2_status es2_shell_briefing_selection(es2_span save, uint8_t selection[ES2_SHELL_BRIEFING_SIZE],
    int16_t *group, int16_t *position, es2_error *e) {
    es2_sav s;
    es2_status status;
    if (!selection) return es2_fail(e, ES2_ERR_ARGUMENT, 0);
    if ((status = es2_sav_parse(save, &s, e)) != ES2_OK) return status;
    if (s.selection.size != 4 + ES2_SHELL_BRIEFING_SIZE) return es2_fail(e, ES2_ERR_LENGTH, s.selection.size);
    memcpy(selection, s.selection.data + 4, ES2_SHELL_BRIEFING_SIZE);
    if (group) *group = es2_le16s(s.selection.data);
    if (position) *position = es2_le16s(s.selection.data + 2);
    return ES2_OK;
}

/* ---- Page 4 --------------------------------------------------------------- */
/* A pilot word at record offset `off` (>= 0x22): the 25 suffix bytes start
 * at record +0x22 (4 prefix bytes, the 30-byte name field). */
static int16_t pilot_word(const es2_sav_pilot *p, unsigned off) { return es2_le16s(p->suffix.data + off - 0x22); }

es2_status es2_shell_debrief_words_from_save(es2_span save, int16_t outcome, uint32_t tally, int16_t lost,
    int16_t recovered, es2_shell_debrief_words *out, es2_error *e) {
    static const unsigned column[3] = {0x2d, 0x31, 0x2f};   /* 0x40f90d..0x40f929 */
    es2_shell_debrief_words w;
    es2_sav s;
    es2_sav_pilot selected[3];
    es2_status status;
    int16_t capacity;
    int c, g;
    if (!out) return es2_fail(e, ES2_ERR_ARGUMENT, 0);
    if ((status = es2_sav_parse(save, &s, e)) != ES2_OK) return status;
    if (s.team.size < 4 || s.roster_selection.size < 6 || s.player.suffix.size != 25)
        return es2_fail(e, ES2_ERR_LENGTH, 0);
    for (g = 0; g < 3; g++) {
        unsigned index = (unsigned)(uint16_t)es2_le16s(s.roster_selection.data + 2 * g);
        if (index > 11) return es2_fail(e, ES2_ERR_OFFSET, (size_t)g);
        if ((status = es2_sav_pilot_at(&s, (size_t)g * 12 + index, &selected[g], e)) != ES2_OK) return status;
        if (selected[g].suffix.size != 25) return es2_fail(e, ES2_ERR_LENGTH, (size_t)g);
    }
    memset(&w, 0, sizeof w);
    w.outcome = outcome;
    w.tally = tally;
    w.lost = lost;
    w.recovered = recovered;
    for (c = 0; c < 3; c++) w.you[c] = w.squad[c] = pilot_word(&s.player, column[c]);
    capacity = es2_le16s(s.team.data);   /* [0x482b5c], compared signed at 0x40fa81 */
    for (int order = 1; order < capacity; order++) {
        int found = -1;
        /* 0x4108fa: the first group 0..2 whose selected pilot has +0x27 == order (16-bit) */
        for (g = 0; g < 3 && found < 0; g++)
            if (pilot_word(&selected[g], 0x27) == wrap16(order)) found = g;
        if (found < 0 || !selected[found].suffix.data[0x24 - 0x22]) continue;   /* 0x40f9a5 */
        for (c = 0; c < 3; c++) w.squad[c] = wrap16((long)w.squad[c] + pilot_word(&selected[found], column[c]));
    }
    *out = w;
    return ES2_OK;
}

es2_status es2_shell_debrief_words_from_results(es2_span results, const es2_shell_results_output *done,
    es2_shell_debrief_words *out, es2_error *e) {
    if (!done || !out || !results.data) return es2_fail(e, ES2_ERR_ARGUMENT, 0);
    if (results.size < 8) return es2_fail(e, ES2_ERR_TRUNCATED, results.size);
    /* 0x40f9a8: 0x40f904 runs in step 9, mode 1 only (after step 8). */
    if (done->ending != ES2_SHELL_RESULTS_ADVANCED) return es2_fail(e, ES2_ERR_UNSUPPORTED, 0);
    /* 0x40f510..0x40f51e: outcome 0x482bcd and tally 0x482bcf from results.dat
     * +0/+2, the pair count [ebp-0x2e] (+6) plus 0x412ce2's count (bx). */
    return es2_shell_debrief_words_from_save((es2_span){done->save.data, done->save.size}, es2_le16s(results.data),
        es2_le32(results.data + 2), done->lost, wrap16((long)es2_le16s(results.data + 6) + done->added), out, e);
}

es2_status es2_shell_debrief_words_after(es2_span results, es2_span save_after, es2_shell_debrief_words *out,
    es2_error *e) {
    es2_sav s;
    es2_status status;
    long added = 0;
    if (!out || !results.data) return es2_fail(e, ES2_ERR_ARGUMENT, 0);
    if (results.size < 8) return es2_fail(e, ES2_ERR_TRUNCATED, results.size);
    if ((status = es2_sav_parse(save_after, &s, e)) != ES2_OK) return status;
    if (s.variables.size != 2000) return es2_fail(e, ES2_ERR_LENGTH, s.variables.size);
    /* 0x412ce2 (vshell_preparation.md "Results path"): n = bank word i, n items for n > 0 */
    for (int i = 21; i < 50; i++) {
        int16_t n = es2_le16s(s.variables.data + 2 * i);
        if (n > 0) added += n;
    }
    return es2_shell_debrief_words_from_save(save_after, es2_le16s(results.data), es2_le32(results.data + 2),
        es2_le16s(s.variables.data + 2 * 9), wrap16((long)es2_le16s(results.data + 6) + wrap16(added)), out, e);
}

int es2_shell_debrief_outcome_id(int16_t outcome) {
    return wrap16((long)outcome + 0x146);   /* 0x40fa90..0x40fa99: add bx, then movsx */
}

es2_status es2_shell_debrief_texts_compose(const es2_shell_debrief_words *w, es2_span outcome_text,
    es2_span unit_text, es2_shell_debrief_texts *out, es2_error *e) {
    static const uint32_t widget[ES2_SHELL_DEBRIEF_VALUES] = {0x48d91c, 0x48d924, 0x48d92c, 0x48d94c, 0x48d958,
                                                              0x48d950, 0x48d95c, 0x48d954, 0x48d960, 0x48d964};
    es2_shell_debrief_texts t;
    char salvage[64];
    int n, i;
    if (!w || !out || (!outcome_text.data && outcome_text.size) || (!unit_text.data && unit_text.size))
        return es2_fail(e, ES2_ERR_ARGUMENT, 0);
    outcome_text = c_string(outcome_text);
    unit_text = c_string(unit_text);
    if (outcome_text.size > 100) return es2_fail(e, ES2_ERR_LENGTH, outcome_text.size);
    /* 0x40face..0x40fae4: unsigned tally / 1000, sprintf("%d %s") into the
     * 40-byte buffer [ebp-0x34]. */
    n = snprintf(salvage, sizeof salvage, "%d ", (int)(w->tally / 1000u));
    if (n < 0 || (size_t)n + unit_text.size > 39) return es2_fail(e, ES2_ERR_LENGTH, (size_t)n + unit_text.size);
    memset(&t, 0, sizeof t);
    memcpy(t.text[0], outcome_text.data ? (const char *)outcome_text.data : "", outcome_text.size);
    memcpy(t.text[1], salvage, (size_t)n);
    if (unit_text.size) memcpy(t.text[1] + n, unit_text.data, unit_text.size);
    snprintf(t.text[2], sizeof t.text[2], "%d", w->recovered);
    for (i = 0; i < 3; i++) {
        snprintf(t.text[3 + 2 * i], sizeof t.text[0], "%d", w->you[i]);
        snprintf(t.text[4 + 2 * i], sizeof t.text[0], "%d", w->squad[i]);
    }
    snprintf(t.text[9], sizeof t.text[9], "%d", w->lost);
    memcpy(t.widget, widget, sizeof widget);
    *out = t;
    return ES2_OK;
}

/* ---- Text panels ------------------------------------------------------------ */
void es2_shell_report_free(es2_shell_report *r) {
    if (!r) return;
    free(r->text.data);
    free(r->line);
    memset(r, 0, sizeof *r);
}

/* 0x4011af: the sum of getWidth over the string up to its NUL. */
static long string_width(const uint8_t *s, const uint8_t widths[256]) {
    long sum = 0;
    for (; *s; s++) sum += widths[*s];
    return sum;
}
static int push_line(es2_shell_report *r, size_t *capacity, size_t start) {
    if (r->lines == *capacity) {
        size_t n = *capacity ? *capacity * 2 : 16;
        size_t *grown = (size_t *)realloc(r->line, n * sizeof *grown);
        if (!grown) return 0;
        r->line = grown;
        *capacity = n;
    }
    r->line[r->lines++] = start;
    return 1;
}

es2_status es2_shell_report_layout(es2_span text, const uint8_t widths[256], int16_t width, int16_t rows,
    es2_shell_report *out, es2_error *e) {
    es2_shell_report r;
    uint8_t *t;
    size_t capacity = 0, p, last = 0, size;
    if (!out || !widths || out->text.data || out->line || (!text.data && text.size)) return es2_fail(e, ES2_ERR_ARGUMENT, 0);
    /* 0x40d4ff asserts on a zero width; 0x40d2db divides by the rows. */
    if (width <= 0 || rows <= 0) return es2_fail(e, ES2_ERR_ARGUMENT, 0);
    text = c_string(text);
    size = text.size;
    memset(&r, 0, sizeof r);
    t = (uint8_t *)malloc(size + 1);
    if (!t) return es2_fail(e, ES2_ERR_CAPACITY, 0);
    if (size) memcpy(t, text.data, size);
    t[size] = 0;
    r.text.data = t;
    r.text.size = size;
    /* Original: 0x40d686. lines[0] := text, last break := text. */
    if (!push_line(&r, &capacity, 0)) goto memory;
    for (p = 0; t[p]; p++) {
        int8_t c = (int8_t)t[p];
        if (c == '\n') {                     /* 0x40d6b9: NUL, break, next line after it */
            t[p] = 0;
            last = p;
            if (!push_line(&r, &capacity, p + 1)) goto memory;
        } else if (c < 0x20 && c >= 0) {     /* 0x40d6d7..0x40d6e5: signed compare */
            t[p] = ' ';
        } else if (c == ' ') {               /* 0x40d6f9 */
            t[p] = 0;
            if (string_width(t + r.line[r.lines - 1], widths) <= width) {
                last = p;
            } else {
                /* 0x40d71c..0x40d738: break at the last break, which is not moved */
                if (t[last] == ' ') t[last] = 0;
                if (!push_line(&r, &capacity, last + 1)) goto memory;
                if (string_width(t + r.line[r.lines - 1], widths) >= width) {   /* assertion 0x40d798 */
                    es2_shell_report_free(&r);
                    return es2_fail(e, ES2_ERR_LENGTH, p);
                }
            }
            t[p] = ' ';
        }
    }
    /* 0x40d7b3..0x40d7e0: the last line too wide: NUL at the last break
     * (whatever byte it holds) and one more line after it. */
    if (string_width(t + r.line[r.lines - 1], widths) > width) {
        t[last] = 0;
        if (!push_line(&r, &capacity, last + 1)) goto memory;
    }
    /* 0x40d2d0..0x40d2ee (16-bit) */
    r.pages = wrap16((long)wrap16((long)r.lines) / rows + ((long)wrap16((long)r.lines) + 1 != rows));
    *out = r;
    return ES2_OK;
memory:
    es2_shell_report_free(&r);
    return es2_fail(e, ES2_ERR_CAPACITY, 0);
}
