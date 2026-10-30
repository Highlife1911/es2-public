/* Unit tests of the mission-screen texts (es2/shell/texts.h) on
 * synthetic STR files and SAV images; no original data. */
#include "es2/shell/texts.h"
#include "es2/formats/sav.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

typedef struct builder { uint8_t b[4096]; size_t n; } builder;
static void w8(builder *x, unsigned v) { x->b[x->n++] = (uint8_t)v; }
static void w16(builder *x, unsigned v) { w8(x, v & 0xff); w8(x, (v >> 8) & 0xff); }
static void put16(uint8_t *p, int v) { p[0] = (uint8_t)v; p[1] = (uint8_t)((unsigned)v >> 8); }
/* One group of strings; aux > 0 appends that many E bytes (0xAA). */
static es2_span str_file(builder *x, const char *const *s, int count, int declared, unsigned aux) {
    x->n = 4;
    w16(x, (unsigned)declared);
    for (int i = 0; i < count; i++) {
        size_t n = strlen(s[i]) + 1;
        w16(x, (unsigned)n);
        memcpy(x->b + x->n, s[i], n);
        x->n += n;
        w8(x, aux);
        for (unsigned k = 0; k < aux; k++) w8(x, 0xaa);
    }
    size_t body = x->n - 4;
    x->b[0] = (uint8_t)body; x->b[1] = (uint8_t)(body >> 8); x->b[2] = 0; x->b[3] = 0;
    return (es2_span){x->b, x->n};
}
static int same(es2_shell_buffer b, const char *s) { return b.size == strlen(s) && !memcmp(b.data, s, b.size) && !b.data[b.size]; }
static int same_span(es2_span b, const char *s) { return b.size == strlen(s) && !memcmp(b.data, s, b.size); }

static void page0(void) {
    static const char *const camp[] = {"one", "two\0hidden", "three"};
    builder x;
    es2_span t;
    es2_error e;
    char title[16];
    es2_span file = str_file(&x, camp, 3, 3, 2);
    CHECK(es2_shell_campaign_text(file, 1, &t, &e) == ES2_OK && same_span(t, "one"));
    CHECK(es2_shell_campaign_text(file, 2, &t, &e) == ES2_OK && same_span(t, "two"));
    CHECK(es2_shell_campaign_text(file, 3, &t, &e) == ES2_OK && same_span(t, "three"));
    t = (es2_span){NULL, 7};
    CHECK(es2_shell_campaign_text(file, 0, &t, &e) == ES2_ERR_NOT_FOUND && t.size == 7);   /* mode 0's group 0 */
    CHECK(es2_shell_campaign_text(file, 4, &t, &e) == ES2_ERR_NOT_FOUND);
    CHECK(es2_shell_campaign_text(file, -32768, &t, &e) == ES2_ERR_NOT_FOUND);
    file.size -= 1;
    CHECK(es2_shell_campaign_text(file, 1, &t, &e) != ES2_OK);
    CHECK(es2_shell_campaign_text(file, 1, NULL, &e) == ES2_ERR_ARGUMENT);
    CHECK(es2_shell_sector_name_id(1) == 0x77 && es2_shell_sector_name_id(5) == 0x7b);
    CHECK(es2_shell_sector_name_id(0) == 0x76 && es2_shell_sector_name_id(-32768) == (int16_t)0x8076);
    CHECK(es2_shell_sector_title((es2_span){(const uint8_t *)"Io\0x", 4}, (es2_span){(const uint8_t *)"Sec", 3},
                                 title, sizeof title, &e) == ES2_OK && !strcmp(title, "Io Sec"));
    CHECK(es2_shell_sector_title((es2_span){0}, (es2_span){0}, title, sizeof title, &e) == ES2_OK && !strcmp(title, " "));
    CHECK(es2_shell_sector_title((es2_span){(const uint8_t *)"0123456789abcd", 14}, (es2_span){(const uint8_t *)"x", 1},
                                 title, sizeof title, &e) == ES2_ERR_CAPACITY);
    {
        static char longer[128], out[256];
        memset(longer, 'a', 99);
        CHECK(es2_shell_sector_title((es2_span){(const uint8_t *)longer, 99}, (es2_span){0}, out, sizeof out, &e) == ES2_OK);
        CHECK(es2_shell_sector_title((es2_span){(const uint8_t *)longer, 99}, (es2_span){(const uint8_t *)"b", 1}, out,
                                     sizeof out, &e) == ES2_ERR_LENGTH);
    }
}

static void selection_refs(uint8_t sel[ES2_SHELL_BRIEFING_SIZE], const int *obj, const int *brief, const int *intel) {
    memset(sel, 0xff, ES2_SHELL_BRIEFING_SIZE);
    put16(sel, 0); put16(sel + 0x16, 0); put16(sel + 0x54, 0); put16(sel + 146, 7);
    for (int i = 0; obj[i] != -2; i++) put16(sel + 2 + 2 * i, obj[i]);
    for (int i = 0; brief[i] != -2; i++) put16(sel + 0x18 + 2 * i, brief[i]);
    for (int i = 0; intel[i] != -2; i++) put16(sel + 0x56 + 2 * i, intel[i]);
}
static void page1(void) {
    static const char *const s[] = {"A", "bb", "", "ccc\n", "D"};
    builder x;
    es2_error e;
    uint8_t sel[ES2_SHELL_BRIEFING_SIZE];
    es2_shell_briefing_texts t;
    es2_span file = str_file(&x, s, 5, 5, 0);
    static const int obj[] = {3, -1, 0, -2}, brief[] = {-1, 1, 1, 4, -2}, intel[] = {-2};
    selection_refs(sel, obj, brief, intel);
    memset(&t, 0, sizeof t);
    CHECK(es2_shell_briefing_texts_compose(file, sel, &t, &e) == ES2_OK);
    CHECK(same(t.text[ES2_SHELL_TEXT_BRIEFING], "bbbbD"));      /* first reference -1: zeroed block, strcat */
    CHECK(same(t.text[ES2_SHELL_TEXT_OBJECTIVES], "ccc\nA"));
    CHECK(same(t.text[ES2_SHELL_TEXT_INTELLIGENCE], ""));
    CHECK(es2_shell_briefing_texts_compose(file, sel, &t, &e) == ES2_ERR_ARGUMENT);   /* out not zero */
    es2_shell_briefing_texts_free(&t);
    CHECK(!t.text[0].data);
    /* the stored counts are ignored */
    put16(sel, 9); put16(sel + 0x16, -3);
    CHECK(es2_shell_briefing_texts_compose(file, sel, &t, &e) == ES2_OK && same(t.text[0], "bbbbD"));
    es2_shell_briefing_texts_free(&t);
    /* an E byte is not auxiliary data here (tag byte) */
    file = str_file(&x, s, 5, 5, 0);
    x.b[4 + 2 + 2 + 2] = 9;   /* E of "A" */
    CHECK(es2_shell_briefing_texts_compose(file, sel, &t, &e) == ES2_OK);
    es2_shell_briefing_texts_free(&t);
    /* references outside the group, other negative values */
    static const int bad1[] = {5, -2}, bad2[] = {-3, -2}, none[] = {-2};
    file = str_file(&x, s, 5, 5, 0);
    selection_refs(sel, none, bad1, none);
    CHECK(es2_shell_briefing_texts_compose(file, sel, &t, &e) == ES2_ERR_OFFSET && !t.text[0].data && e.offset == 0x18);
    selection_refs(sel, none, none, bad2);
    CHECK(es2_shell_briefing_texts_compose(file, sel, &t, &e) == ES2_ERR_OFFSET && !t.text[2].data && e.offset == 0x56);
    /* group count 128 and above: refused (0x42863a reads its low byte) */
    {
        static const char *many[130];
        for (int i = 0; i < 130; i++) many[i] = "z";
        static builder big;
        selection_refs(sel, none, none, none);
        CHECK(es2_shell_briefing_texts_compose(str_file(&big, many, 127, 127, 0), sel, &t, &e) == ES2_OK);
        es2_shell_briefing_texts_free(&t);
        CHECK(es2_shell_briefing_texts_compose(str_file(&big, many, 128, 128, 0), sel, &t, &e) == ES2_ERR_COUNT);
    }
    /* empty group, every reference -1 */
    file = str_file(&x, s, 0, 0, 0);
    CHECK(es2_shell_briefing_texts_compose(file, sel, &t, &e) == ES2_OK && same(t.text[1], ""));
    es2_shell_briefing_texts_free(&t);
    /* stale tail after the group is ignored; a truncated group is not */
    file = str_file(&x, s, 2, 2, 0);
    x.b[x.n++] = 0x55; x.b[x.n++] = 0x66;
    file.size = x.n;
    CHECK(es2_shell_briefing_texts_compose(file, sel, &t, &e) == ES2_OK);
    es2_shell_briefing_texts_free(&t);
    file = str_file(&x, s, 2, 3, 0);
    CHECK(es2_shell_briefing_texts_compose(file, sel, &t, &e) != ES2_OK);
    CHECK(es2_shell_briefing_texts_compose(file, NULL, &t, &e) == ES2_ERR_ARGUMENT);
}

/* ---- synthetic SAV ---------------------------------------------------------- */
static uint8_t prefix[37][4], suffix[37][25], selection[152], choice[12], team[4], five[22], nine[18], tally[4],
    variables[2000], final_fields[22];
static uint8_t image[16384];
static void pilot_set(int r, unsigned off, int v) { put16(suffix[r] + off - 0x22, v); }
static size_t compose(void) {
    static uint8_t name[2] = {'P', 0};
    es2_sav_image m;
    es2_sav_pilot roster[37];
    size_t required = 0;
    es2_error e;
    memset(&m, 0, sizeof m);
    for (int i = 0; i < 37; i++) {
        roster[i].prefix = (es2_span){prefix[i], 4};
        roster[i].name = (es2_span){name, 2};
        roster[i].suffix = (es2_span){suffix[i], 25};
        roster[i].offset = 0;
    }
    for (int i = 0; i < 5; i++) put16(five + 2 + 4 * i, i);
    m.five_entry_bank = (es2_span){five, 22};
    m.selection = (es2_span){selection, 152};
    m.roster = roster;
    m.roster_selection = (es2_span){choice, 12};
    m.team = (es2_span){team, 4};
    m.player = &roster[36];
    m.nine_word_bank = (es2_span){nine, 18};
    m.tally = (es2_span){tally, 4};
    m.variables = (es2_span){variables, 2000};
    m.final_fields = (es2_span){final_fields, 22};
    CHECK(es2_sav_compose(&m, image, sizeof image, &required, &e) == ES2_OK);
    return required;
}
static void page4(void) {
    es2_shell_debrief_words w;
    es2_shell_debrief_texts t;
    es2_error e;
    size_t n;
    /* player: 1, 2, 3 at +0x2d, +0x31, +0x2f; group g selects pilot 2 of its 12 */
    pilot_set(36, 0x2d, 1); pilot_set(36, 0x31, 2); pilot_set(36, 0x2f, 3);
    for (int g = 0; g < 3; g++) {
        int r = g * 12 + 2;
        put16(choice + 2 * g, 2);
        put16(choice + 6 + 2 * g, 3);
        pilot_set(r, 0x27, g + 1);         /* orders 1, 2, 3 */
        suffix[r][2] = 1;                  /* +0x24 */
        pilot_set(r, 0x2d, 10 * (g + 1)); pilot_set(r, 0x31, 100 * (g + 1)); pilot_set(r, 0x2f, 1000 * (g + 1));
    }
    put16(team, 3);   /* word 0: orders 1..2 */
    put16(selection, 2); put16(selection + 2, 4);
    n = compose();
    CHECK(es2_shell_debrief_words_from_save((es2_span){image, n}, 1, 123456, 2, 5, &w, &e) == ES2_OK);
    CHECK(w.outcome == 1 && w.tally == 123456 && w.lost == 2 && w.recovered == 5);
    CHECK(w.you[0] == 1 && w.you[1] == 2 && w.you[2] == 3);
    CHECK(w.squad[0] == 31 && w.squad[1] == 302 && w.squad[2] == 3003);
    /* inactive pilot of order 2 skipped; order 3 only with team word 0 = 4 */
    suffix[1 * 12 + 2][2] = 0;
    put16(team, 4);
    n = compose();
    CHECK(es2_shell_debrief_words_from_save((es2_span){image, n}, 0, 0, 0, 0, &w, &e) == ES2_OK);
    CHECK(w.squad[0] == 41 && w.squad[1] == 402 && w.squad[2] == 4003);
    /* two groups with the same order: the first wins; wrapping sums */
    suffix[1 * 12 + 2][2] = 1;
    pilot_set(1 * 12 + 2, 0x27, 1);
    pilot_set(0 * 12 + 2, 0x2d, 32767);
    n = compose();
    CHECK(es2_shell_debrief_words_from_save((es2_span){image, n}, 0, 0, 0, 0, &w, &e) == ES2_OK);
    CHECK(w.squad[0] == (int16_t)(1 + 32767 + 30) && w.squad[1] == 2 + 100 + 300);
    /* team word 0 of 0 or negative: You only */
    put16(team, -1);
    n = compose();
    CHECK(es2_shell_debrief_words_from_save((es2_span){image, n}, 0, 0, 0, 0, &w, &e) == ES2_OK && w.squad[2] == 3);
    /* a selected index above 11 */
    put16(choice + 2, 12);
    n = compose();
    CHECK(es2_shell_debrief_words_from_save((es2_span){image, n}, 0, 0, 0, 0, &w, &e) != ES2_OK);
    put16(choice + 2, 2);
    CHECK(es2_shell_debrief_words_from_save((es2_span){image, 10}, 0, 0, 0, 0, &w, &e) != ES2_OK);
    CHECK(es2_shell_debrief_words_from_save((es2_span){image, n}, 0, 0, 0, 0, NULL, &e) == ES2_ERR_ARGUMENT);
    /* selection of the SAV */
    {
        uint8_t sel[ES2_SHELL_BRIEFING_SIZE];
        int16_t g = 0, p = 0;
        selection[4] = 0x5a; selection[151] = 0x33;
        n = compose();
        CHECK(es2_shell_briefing_selection((es2_span){image, n}, sel, &g, &p, &e) == ES2_OK && g == 2 && p == 4 &&
              sel[0] == 0x5a && sel[147] == 0x33);
    }
    /* from results: P at +6 plus added; ending must be the advancement */
    {
        es2_shell_results_output done;
        uint8_t res[8] = {1, 0, 0x40, 0x42, 0x0f, 0, 3, 0};   /* outcome 1, tally 1000000, P 3 */
        put16(team, 2);
        n = compose();
        memset(&done, 0, sizeof done);
        done.save.data = image; done.save.size = n; done.lost = 1; done.added = 4;
        done.ending = ES2_SHELL_RESULTS_OTHER_MODE;
        CHECK(es2_shell_debrief_words_from_results((es2_span){res, 8}, &done, &w, &e) == ES2_ERR_UNSUPPORTED);
        done.ending = ES2_SHELL_RESULTS_ADVANCED;
        CHECK(es2_shell_debrief_words_from_results((es2_span){res, 7}, &done, &w, &e) == ES2_ERR_TRUNCATED);
        CHECK(es2_shell_debrief_words_from_results((es2_span){res, 8}, &done, &w, &e) == ES2_OK);
        CHECK(w.outcome == 1 && w.tally == 1000000 && w.recovered == 7 && w.lost == 1);
        done.added = 32767;
        CHECK(es2_shell_debrief_words_from_results((es2_span){res, 8}, &done, &w, &e) == ES2_OK && w.recovered == -32766);
        /* from the written SAV: bank word 9, positive words 21..49 */
        put16(variables + 2 * 9, 2);
        put16(variables + 2 * 21, 4); put16(variables + 2 * 30, -7); put16(variables + 2 * 49, 1);
        put16(variables + 2 * 50, 100); put16(variables + 2 * 20, 100);
        n = compose();
        CHECK(es2_shell_debrief_words_after((es2_span){res, 8}, (es2_span){image, n}, &w, &e) == ES2_OK);
        CHECK(w.outcome == 1 && w.tally == 1000000 && w.lost == 2 && w.recovered == 3 + 5);
        CHECK(es2_shell_debrief_words_after((es2_span){res, 7}, (es2_span){image, n}, &w, &e) == ES2_ERR_TRUNCATED);
        CHECK(es2_shell_debrief_words_after((es2_span){res, 8}, (es2_span){image, 40}, &w, &e) != ES2_OK);
        memset(variables, 0, sizeof variables);
    }
    /* texts */
    CHECK(es2_shell_debrief_outcome_id(0) == 0x146 && es2_shell_debrief_outcome_id(1) == 0x147);
    CHECK(es2_shell_debrief_outcome_id(-0x147) == -1 && es2_shell_debrief_outcome_id(32767) == (int16_t)(32767 + 0x146));
    w.outcome = 1; w.tally = 4294967295u; w.recovered = -3; w.lost = 2;
    w.you[0] = 1; w.you[1] = 2; w.you[2] = 3; w.squad[0] = -32768; w.squad[1] = 5; w.squad[2] = 6;
    CHECK(es2_shell_debrief_texts_compose(&w, (es2_span){(const uint8_t *)"WON\0junk", 8},
                                          (es2_span){(const uint8_t *)"t", 1}, &t, &e) == ES2_OK);
    CHECK(!strcmp(t.text[0], "WON") && !strcmp(t.text[1], "4294967 t") && !strcmp(t.text[2], "-3"));
    CHECK(!strcmp(t.text[3], "1") && !strcmp(t.text[4], "-32768") && !strcmp(t.text[5], "2") && !strcmp(t.text[6], "5"));
    CHECK(!strcmp(t.text[7], "3") && !strcmp(t.text[8], "6") && !strcmp(t.text[9], "2"));
    CHECK(t.widget[0] == 0x48d91c && t.widget[4] == 0x48d958 && t.widget[9] == 0x48d964);
    w.tally = 999;
    CHECK(es2_shell_debrief_texts_compose(&w, (es2_span){0}, (es2_span){0}, &t, &e) == ES2_OK &&
          !strcmp(t.text[0], "") && !strcmp(t.text[1], "0 "));
    {
        static char unit[64];
        memset(unit, 'u', sizeof unit);
        w.tally = 9999;   /* "9 " + 37 = 39 */
        CHECK(es2_shell_debrief_texts_compose(&w, (es2_span){0}, (es2_span){(const uint8_t *)unit, 37}, &t, &e) == ES2_OK);
        CHECK(es2_shell_debrief_texts_compose(&w, (es2_span){0}, (es2_span){(const uint8_t *)unit, 38}, &t, &e) == ES2_ERR_LENGTH);
        static char outcome[128];
        memset(outcome, 'o', sizeof outcome);
        CHECK(es2_shell_debrief_texts_compose(&w, (es2_span){(const uint8_t *)outcome, 100}, (es2_span){0}, &t, &e) == ES2_OK);
        CHECK(es2_shell_debrief_texts_compose(&w, (es2_span){(const uint8_t *)outcome, 101}, (es2_span){0}, &t, &e) == ES2_ERR_LENGTH);
    }
    CHECK(es2_shell_debrief_texts_compose(NULL, (es2_span){0}, (es2_span){0}, &t, &e) == ES2_ERR_ARGUMENT);
}

/* ---- report layout: every byte one unit wide except where set ------------ */
static uint8_t widths[256];
static int layout(const char *text, int width, int rows, es2_shell_report *r) {
    es2_error e;
    memset(r, 0, sizeof *r);
    return es2_shell_report_layout((es2_span){(const uint8_t *)text, strlen(text)}, widths, (int16_t)width,
                                   (int16_t)rows, r, &e);
}
static const char *line(const es2_shell_report *r, size_t i) { return (const char *)r->text.data + r->line[i]; }
static void report(void) {
    es2_shell_report r;
    for (int i = 0; i < 256; i++) widths[i] = 1;
    CHECK(layout("aa bb cc", 5, 2, &r) == ES2_OK && r.lines == 2 && !strcmp(line(&r, 0), "aa bb") &&
          !strcmp(line(&r, 1), "cc") && r.pages == 2);   /* 2/2 + (3 != 2) */
    es2_shell_report_free(&r);
    CHECK(layout("one\ntwo\tx", 20, 3, &r) == ES2_OK && r.lines == 2 && !strcmp(line(&r, 0), "one") &&
          !strcmp(line(&r, 1), "two x") && r.pages == 0);   /* 2/3 + (3 != 3) */
    es2_shell_report_free(&r);
    /* a line ending exactly at the width stays; bytes 0x80.. are not spaces */
    CHECK(layout("abcd\x81 e", 5, 1, &r) == ES2_OK && r.lines == 2 && !strcmp(line(&r, 0), "abcd\x81") && r.pages == 3);
    es2_shell_report_free(&r);
    /* ordinary breaks */
    CHECK(layout("aaa bbb ccc ddd eee", 7, 10, &r) == ES2_OK);
    CHECK(r.lines == 3 && !strcmp(line(&r, 0), "aaa bbb") && !strcmp(line(&r, 1), "ccc ddd") && !strcmp(line(&r, 2), "eee"));
    es2_shell_report_free(&r);
    /* the last break is not moved: at the end the same line twice */
    CHECK(layout("aaa bbb cccccc dd", 7, 10, &r) == ES2_OK);
    CHECK(r.lines == 3 && !strcmp(line(&r, 0), "aaa bbb") && r.line[1] == r.line[2] && !strcmp(line(&r, 1), "cccccc dd"));
    es2_shell_report_free(&r);
    /* ... and inside the text the assertion */
    CHECK(layout("aaa bbbbbb cc dd", 7, 10, &r) == ES2_ERR_LENGTH);
    /* a first word wider than the line: the break at the text start (not a
     * space) leaves the byte and starts the next line one byte later; that
     * line is still too wide: the assertion */
    CHECK(layout("abcdefgh ij", 5, 1, &r) == ES2_ERR_LENGTH && !r.line);
    /* the end case: no break so far, the whole text one word: the first byte
     * becomes NUL and the second line starts after it */
    CHECK(layout("abcdefgh", 5, 1, &r) == ES2_OK && r.lines == 2 && !strcmp(line(&r, 0), "") &&
          !strcmp(line(&r, 1), "bcdefgh"));
    es2_shell_report_free(&r);
    /* the end case with a space break */
    CHECK(layout("aa bbbb", 4, 1, &r) == ES2_OK && r.lines == 2 && !strcmp(line(&r, 0), "aa") &&
          !strcmp(line(&r, 1), "bbbb"));
    es2_shell_report_free(&r);
    widths[' '] = 0;
    widths['W'] = 9;
    CHECK(layout("W a", 9, 1, &r) == ES2_OK && r.lines == 2 && !strcmp(line(&r, 0), "W"));
    es2_shell_report_free(&r);
    CHECK(layout("", 9, 1, &r) == ES2_OK && r.lines == 1 && r.pages == 2);
    es2_shell_report_free(&r);
    CHECK(layout("x", 0, 1, &r) == ES2_ERR_ARGUMENT && layout("x", 4, 0, &r) == ES2_ERR_ARGUMENT);
}

int main(void) {
    page0();
    page1();
    page4();
    report();
    printf("%u checks, %u failures\n", checks, failures);
    return failures != 0;
}
