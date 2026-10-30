/* Packed string files (str.h) and configuration files (cfg.h).
 * Default build: synthetic fixtures built byte by byte below, no game data.
 * With ES2_STR_CFG_REAL_TEST: every STR and CFG under ES2_DATA_ROOT (loose files
 * and members of the VOL archives); skipped when ES2_DATA_ROOT is unset or empty.
 * The real test reads only and keeps no text. */
#include "es2/formats/cfg.h"
#include "es2/formats/str.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)

static es2_span span(const uint8_t *data, size_t size) { es2_span s; s.data = data; s.size = size; return s; }

#ifndef ES2_STR_CFG_REAL_TEST
static int same(es2_span a, const void *b, size_t n) { return a.size == n && (n == 0 || memcmp(a.data, b, n) == 0); }
/* ---- synthetic STR ------------------------------------------------------- */

static void put16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, v & 0xffffu); put16(p + 2, v >> 16); }

/* Appends one string (text without NUL) with E auxiliary bytes. */
static size_t add_string(uint8_t *b, size_t at, const char *text, const uint8_t *aux, unsigned e)
{
    size_t n = strlen(text);
    put16(b + at, (unsigned)n + 1);
    memcpy(b + at + 2, text, n + 1);
    b[at + 2 + n + 1] = (uint8_t)e;
    if (e && aux) memcpy(b + at + 2 + n + 2, aux, e);
    return at + 2 + n + 2 + e;
}

/* A string whose E byte is a plain tag (no payload follows it). */
static size_t add_tagged(uint8_t *b, size_t at, const char *text, uint8_t tag)
{
    size_t end = add_string(b, at, text, NULL, 0);
    b[end - 1] = tag;
    return end;
}

/* Two groups: {"ab", "c"} and {"xyz"}; the body size is patched in. */
static size_t basic_file(uint8_t *b)
{
    size_t at = 4;
    put16(b + at, 2); at += 2;
    at = add_string(b, at, "ab", NULL, 0);
    at = add_string(b, at, "c", NULL, 0);
    put16(b + at, 1); at += 2;
    at = add_string(b, at, "xyz", NULL, 0);
    put32(b, (uint32_t)(at - 4));
    return at;
}

static void test_str_basic(void)
{
    uint8_t b[128], out[128], guard[128];
    size_t n = basic_file(b), need = 0, g, i;
    es2_str s;
    es2_error e;
    es2_str_string str;
    es2_str_cursor c;
    size_t count = 0;
    es2_str_group_in groups[2];
    es2_str_string g0[2], g1[1];

    CHECK(es2_str_parse(span(b, n), 0, 0, &s, &e) == ES2_OK);
    CHECK(s.group_count == 2 && s.string_count == 3 && s.groups_end == n - 4 && s.trailing.size == 0);
    CHECK(s.body.size == n - 4 && s.body.data == b + 4);
    CHECK(es2_str_group_size(&s, 0, &count, &e) == ES2_OK && count == 2);
    CHECK(es2_str_group_size(&s, 1, &count, &e) == ES2_OK && count == 1);
    CHECK(es2_str_group_size(&s, 2, &count, &e) == ES2_ERR_NOT_FOUND);
    CHECK(es2_str_string_at(&s, 0, 1, &str, &e) == ES2_OK && same(str.text, "c", 1) && str.aux.size == 0);
    CHECK(str.text.data[str.text.size] == 0);     /* the NUL follows the text in the input */
    CHECK(es2_str_string_at(&s, 1, 0, &str, &e) == ES2_OK && same(str.text, "xyz", 3));
    CHECK(es2_str_string_at(&s, 0, 2, &str, &e) == ES2_ERR_NOT_FOUND);
    CHECK(es2_str_string_at(&s, 5, 0, &str, &e) == ES2_ERR_NOT_FOUND);

    /* cursor order rules */
    CHECK(es2_str_cursor_init(&s, &c, &e) == ES2_OK);
    CHECK(es2_str_next_string(&c, &str, &e) == ES2_ERR_NOT_FOUND);   /* no group entered */
    CHECK(es2_str_next_group(&c, &count, &e) == ES2_OK && count == 2);
    CHECK(es2_str_next_group(&c, &count, &e) == ES2_ERR_NOT_FOUND);   /* strings unread */
    CHECK(es2_str_next_string(&c, &str, &e) == ES2_OK && same(str.text, "ab", 2));
    CHECK(es2_str_next_string(&c, &str, &e) == ES2_OK && same(str.text, "c", 1));
    CHECK(es2_str_next_string(&c, &str, &e) == ES2_ERR_NOT_FOUND);
    CHECK(es2_str_next_group(&c, &count, &e) == ES2_OK && count == 1);
    CHECK(es2_str_next_string(&c, &str, &e) == ES2_OK && same(str.text, "xyz", 3));
    CHECK(es2_str_next_group(&c, &count, &e) == ES2_ERR_NOT_FOUND);   /* past the last group */

    /* the writer reproduces the file */
    g0[0].text = span((const uint8_t *)"ab", 2); g0[0].aux = span(NULL, 0);
    g0[1].text = span((const uint8_t *)"c", 1);  g0[1].aux = span(NULL, 0);
    g1[0].text = span((const uint8_t *)"xyz", 3); g1[0].aux = span(NULL, 0);
    groups[0].strings = g0; groups[0].count = 2;
    groups[1].strings = g1; groups[1].count = 1;
    CHECK(es2_str_write(groups, 2, 0, NULL, 0, &need, &e) == ES2_OK && need == n);
    memset(out, 0xa5, sizeof out);
    CHECK(es2_str_write(groups, 2, 0, out, sizeof out, &need, &e) == ES2_OK && need == n && memcmp(out, b, n) == 0);
    CHECK(out[n] == 0xa5);
    memset(guard, 0x5a, sizeof guard);
    CHECK(es2_str_write(groups, 2, 0, guard, n - 1, &need, &e) == ES2_ERR_CAPACITY && need == n);
    for (i = 0; i < sizeof guard; ++i) if (guard[i] != 0x5a) { CHECK(0); break; }

    /* stale bytes after the body are reported, not parsed */
    b[n] = 0x11; b[n + 1] = 0x22;
    CHECK(es2_str_parse(span(b, n + 2), 0, 0, &s, &e) == ES2_OK && s.trailing.size == 2 && s.trailing.data == b + n);

    /* a reader that asks for fewer groups ignores the rest (max_groups) */
    CHECK(es2_str_parse(span(b, n), 1, 0, &s, &e) == ES2_OK && s.group_count == 1 && s.string_count == 2);
    CHECK(s.groups_end == 13);                    /* count 2 + "ab" (6) + "c" (5) */
    CHECK(es2_str_parse(span(b, n), 3, 0, &s, &e) == ES2_ERR_TRUNCATED);
    for (g = 0; g < 2; ++g) CHECK(es2_str_parse(span(b, n), g + 1, 0, &s, &e) == ES2_OK && s.group_count == g + 1);
}

static void test_str_errors(void)
{
    uint8_t b[128], t[128];
    size_t n = basic_file(b), i;
    es2_str s;
    es2_error e;

    CHECK(es2_str_parse(span(b, 3), 0, 0, &s, &e) == ES2_ERR_TRUNCATED);
    CHECK(es2_str_parse(span(NULL, 0), 0, 0, &s, &e) == ES2_ERR_TRUNCATED);
    CHECK(es2_str_parse(span(NULL, 4), 0, 0, &s, &e) == ES2_ERR_ARGUMENT);
    CHECK(es2_str_parse(span(b, n), 0, 0, NULL, &e) == ES2_ERR_ARGUMENT);
    CHECK(es2_str_parse(span(b, n), 0, 2u, &s, &e) == ES2_ERR_ARGUMENT);
    CHECK(es2_str_parse(span(b, n - 1), 0, 0, &s, &e) == ES2_ERR_LENGTH && e.offset == 0);

    /* empty body: no group at all */
    memset(t, 0, 4);
    CHECK(es2_str_parse(span(t, 4), 0, 0, &s, &e) == ES2_OK && s.group_count == 0 && s.string_count == 0);
    CHECK(es2_str_parse(span(t, 4), 1, 0, &s, &e) == ES2_ERR_TRUNCATED);

    /* an empty group (N = 0) is valid framing */
    put32(t, 2); put16(t + 4, 0);
    CHECK(es2_str_parse(span(t, 6), 0, 0, &s, &e) == ES2_OK && s.group_count == 1 && s.string_count == 0);
    { size_t k = 9; CHECK(es2_str_group_size(&s, 0, &k, &e) == ES2_OK && k == 0); }

    /* a single byte left over cannot be a group header */
    put32(t, 1); t[4] = 0;
    CHECK(es2_str_parse(span(t, 5), 0, 0, &s, &e) == ES2_ERR_TRUNCATED && e.offset == 4);

    /* negative group count */
    put32(t, 2); put16(t + 4, 0xffffu);
    CHECK(es2_str_parse(span(t, 6), 0, 0, &s, &e) == ES2_ERR_COUNT && e.offset == 4);

    /* L = 0, L < 0, text not ending in NUL, string beyond the body */
    memcpy(t, b, n); put16(t + 6, 0);
    CHECK(es2_str_parse(span(t, n), 0, 0, &s, &e) == ES2_ERR_STRING && e.offset == 6);
    memcpy(t, b, n); put16(t + 6, 0x8000u);
    CHECK(es2_str_parse(span(t, n), 0, 0, &s, &e) == ES2_ERR_STRING);
    memcpy(t, b, n); t[8 + 2] = 'Z';                     /* the NUL of "ab" */
    CHECK(es2_str_parse(span(t, n), 0, 0, &s, &e) == ES2_ERR_STRING && e.offset == 10);
    memcpy(t, b, n); put16(t + 6, 200);
    CHECK(es2_str_parse(span(t, n), 0, 0, &s, &e) == ES2_ERR_TRUNCATED);

    /* every cut of the body at a non-boundary fails; boundaries parse */
    for (i = 4; i <= n; ++i) {
        es2_status status;
        memcpy(t, b, i);
        put32(t, (uint32_t)(i - 4));
        status = es2_str_parse(span(t, i), 0, 0, &s, &e);
        if (i == 4 || i == 17 || i == n) CHECK(status == ES2_OK);        /* group boundaries */
        else CHECK(status == ES2_ERR_TRUNCATED);
    }
}

static void test_str_aux(void)
{
    /* Group {"bc", "a" + aux "xy"} then group {"d"}. */
    uint8_t b[64], t[64], out[64];
    static const uint8_t aux[2] = {'x', 'y'};
    size_t at = 4, n, need;
    es2_str s;
    es2_error e;
    es2_str_string str;
    es2_str_group_in group[2];
    es2_str_string strings[3];

    put16(b + at, 2); at += 2;
    at = add_string(b, at, "bc", NULL, 0);
    at = add_string(b, at, "a", aux, 2);
    put16(b + at, 1); at += 2;
    at = add_string(b, at, "d", NULL, 0);
    put32(b, (uint32_t)(at - 4));
    n = at;
    CHECK(b[16] == 2);                                   /* the E byte of "a" */

    CHECK(es2_str_parse(span(b, n), 0, 0, &s, &e) == ES2_OK && s.group_count == 2 && s.string_count == 3);
    CHECK(es2_str_string_at(&s, 0, 0, &str, &e) == ES2_OK && same(str.text, "bc", 2) && str.aux.size == 0);
    CHECK(es2_str_string_at(&s, 0, 1, &str, &e) == ES2_OK && same(str.text, "a", 1) && same(str.aux, aux, 2));
    CHECK(es2_str_string_at(&s, 1, 0, &str, &e) == ES2_OK && same(str.text, "d", 1));
    CHECK(s.groups_end == n - 4);

    /* A reader that ignores the auxiliary pointer consumes only the E byte:
     * group 0 still frames, but it ends two bytes early and sees no aux data. */
    CHECK(es2_str_parse(span(b, n), 1, ES2_STR_TAG_BYTE, &s, &e) == ES2_OK && s.group_count == 1);
    CHECK(s.groups_end == 2 + 6 + 5);
    CHECK(es2_str_string_at(&s, 0, 1, &str, &e) == ES2_OK && same(str.text, "a", 1) && str.aux.size == 0);
    CHECK(es2_str_parse(span(b, n), 0, ES2_STR_TAG_BYTE, &s, &e) != ES2_OK);   /* 'x' 'y' start a bogus group */

    /* E above 127 is rejected when auxiliary data is read. */
    memcpy(t, b, n); t[16] = 200;
    CHECK(es2_str_parse(span(t, n), 0, 0, &s, &e) == ES2_ERR_STRING && e.offset == 16);
    memcpy(t, b, n); t[16] = 128;                        /* the smallest rejected value */
    CHECK(es2_str_parse(span(t, n), 0, 0, &s, &e) == ES2_ERR_STRING);
    /* E beyond the body */
    memcpy(t, b, n); t[16] = 100;
    CHECK(es2_str_parse(span(t, n), 0, 0, &s, &e) == ES2_ERR_TRUNCATED);
    /* ... but as one byte it is fine for a reader that does not read aux data */
    memcpy(t, b, n); t[16] = 200;
    CHECK(es2_str_parse(span(t, n), 1, ES2_STR_TAG_BYTE, &s, &e) == ES2_OK);

    /* the writer round-trips auxiliary data and rejects bad input */
    strings[0].text = span((const uint8_t *)"bc", 2); strings[0].aux = span(NULL, 0);
    strings[1].text = span((const uint8_t *)"a", 1); strings[1].aux = span(aux, 2);
    strings[2].text = span((const uint8_t *)"d", 1); strings[2].aux = span(NULL, 0);
    group[0].strings = strings; group[0].count = 2;
    group[1].strings = strings + 2; group[1].count = 1;
    CHECK(es2_str_write(group, 2, 0, out, sizeof out, &need, &e) == ES2_OK && need == n && memcmp(out, b, n) == 0);
    strings[1].text = span((const uint8_t *)"a\0b", 3);
    CHECK(es2_str_write(group, 2, 0, out, sizeof out, &need, &e) == ES2_ERR_STRING);
    strings[1].text = span((const uint8_t *)"a", 1);
    { uint8_t big[128]; memset(big, 1, sizeof big); strings[1].aux = span(big, 128);
      CHECK(es2_str_write(group, 2, 0, out, sizeof out, &need, &e) == ES2_ERR_LENGTH); }
    { es2_str_group_in none; none.strings = NULL; none.count = 0;
      CHECK(es2_str_write(&none, 1, 0, out, sizeof out, &need, &e) == ES2_OK && need == 6 && out[0] == 2 && out[4] == 0 && out[5] == 0);
      CHECK(es2_str_write(NULL, 0, 0, out, sizeof out, &need, &e) == ES2_OK && need == 4 && same(span(out, 4), "\0\0\0\0", 4)); }
}


/* gamefile.str shape: E is a slot flag, and the size field counts a stale tail. */
static void test_str_tag(void)
{
    uint8_t b[96], out[96];
    size_t at = 4, n, need, i;
    es2_str s;
    es2_error e;
    es2_str_string str;
    es2_str_group_in group[2];
    es2_str_string names[2], labels[2];

    put16(b + at, 2); at += 2;
    at = add_string(b, at, "F0", NULL, 0);
    at = add_string(b, at, "F1", NULL, 0);
    put16(b + at, 2); at += 2;
    at = add_tagged(b, at, "L0", 1);             /* slot flag 1 */
    at = add_string(b, at, "L1", NULL, 0);
    n = at;
    put32(b, (uint32_t)(n - 4));
    CHECK(es2_str_parse(span(b, n), 0, ES2_STR_TAG_BYTE, &s, &e) == ES2_OK && s.group_count == 2 && s.string_count == 4);
    CHECK(es2_str_string_at(&s, 1, 0, &str, &e) == ES2_OK && same(str.text, "L0", 2) && str.tag == 1 && str.aux.size == 0);
    CHECK(es2_str_string_at(&s, 1, 1, &str, &e) == ES2_OK && same(str.text, "L1", 2) && str.tag == 0);
    /* Read as auxiliary data the flag would swallow the first byte of the next string. */
    CHECK(es2_str_parse(span(b, n), 0, 0, &s, &e) != ES2_OK);

    /* the writer reproduces it with tags */
    for (i = 0; i < 2; ++i) { names[i].aux = span(NULL, 0); names[i].tag = 0; labels[i].aux = span(NULL, 0); labels[i].tag = (uint8_t)(1 - i); }
    names[0].text = span((const uint8_t *)"F0", 2); names[1].text = span((const uint8_t *)"F1", 2);
    labels[0].text = span((const uint8_t *)"L0", 2); labels[1].text = span((const uint8_t *)"L1", 2);
    labels[1].tag = 0;
    group[0].strings = names; group[0].count = 2;
    group[1].strings = labels; group[1].count = 2;
    CHECK(es2_str_write(group, 2, ES2_STR_TAG_BYTE, out, sizeof out, &need, &e) == ES2_OK && need == n && memcmp(out, b, n) == 0);
    labels[0].aux = span((const uint8_t *)"x", 1);
    CHECK(es2_str_write(group, 2, ES2_STR_TAG_BYTE, out, sizeof out, &need, &e) == ES2_ERR_LENGTH);
    CHECK(es2_str_write(group, 2, 4u, out, sizeof out, &need, &e) == ES2_ERR_ARGUMENT);

    /* Four stale bytes inside the size field (0x40e66d takes the file length):
     * two groups are consumed, the unlimited parse cannot frame the tail. */
    b[n] = 0; b[n + 1] = 1; b[n + 2] = 0; b[n + 3] = 1;
    put32(b, (uint32_t)(n + 4 - 4));
    CHECK(es2_str_parse(span(b, n + 4), 2, ES2_STR_TAG_BYTE, &s, &e) == ES2_OK);
    CHECK(s.groups_end == n - 4 && s.body.size == n && s.body.size - s.groups_end == 4 && s.trailing.size == 0);
    CHECK(es2_str_parse(span(b, n + 4), 0, ES2_STR_TAG_BYTE, &s, &e) != ES2_OK);
}

/* ---- synthetic CFG ------------------------------------------------------- */

static es2_span text(const char *s) { return span((const uint8_t *)s, strlen(s)); }

static void test_prefs(void)
{
    uint8_t file[60], out[ES2_PREFS_SIZE], idx[3] = {2, 0x2a, 53};
    es2_prefs p, q;
    es2_prefs_video v;
    es2_error e;
    size_t i;

    for (i = 0; i < sizeof file; ++i) file[i] = (uint8_t)(i + 1);
    CHECK(es2_prefs_load(span(NULL, 0), &p, &e) == ES2_OK);
    for (i = 0; i < ES2_PREFS_SIZE; ++i) if (p.byte[i] != 0) { CHECK(0); break; }
    CHECK(es2_prefs_load(span(file, 3), &p, &e) == ES2_OK && p.byte[2] == 3 && p.byte[3] == 0 && p.byte[53] == 0);
    CHECK(es2_prefs_load(span(file, 54), &p, &e) == ES2_OK && p.byte[53] == 54);
    CHECK(es2_prefs_load(span(file, 60), &p, &e) == ES2_OK && p.byte[53] == 54);
    CHECK(es2_prefs_load(span(NULL, 1), &p, &e) == ES2_ERR_ARGUMENT);
    CHECK(es2_prefs_load(span(file, 1), NULL, &e) == ES2_ERR_ARGUMENT);

    es2_prefs_serialize(&p, out);
    CHECK(memcmp(out, file, 54) == 0);

    /* named offsets used by the shell are the documented ones */
    CHECK(ES2_PREFS_SHELL_MODE == 0x2a && ES2_PREFS_REPAIR == 0x2c && ES2_PREFS_BUILD == 0x2d);

    /* read-modify-write overlay */
    memset(q.byte, 0x77, sizeof q.byte);
    CHECK(es2_prefs_overlay(span(file, 54), &q, idx, 3, out, &e) == ES2_OK);
    for (i = 0; i < 54; ++i) CHECK(out[i] == (i == 2 || i == 0x2a || i == 53 ? 0x77 : (uint8_t)(i + 1)));
    CHECK(es2_prefs_overlay(span(file, 5), &q, idx, 3, out, &e) == ES2_OK);      /* short file: zeros, never stale */
    CHECK(out[0] == 1 && out[4] == 5 && out[5] == 0 && out[2] == 0x77 && out[0x2a] == 0x77 && out[53] == 0x77 && out[30] == 0);
    CHECK(es2_prefs_overlay(span(NULL, 0), &q, NULL, 0, out, &e) == ES2_OK && out[0] == 0);
    idx[1] = 54;
    memset(out, 0xee, sizeof out);
    CHECK(es2_prefs_overlay(span(file, 54), &q, idx, 3, out, &e) == ES2_ERR_OFFSET && out[0] == 0xee);

    /* the 7-byte video read */
    CHECK(es2_prefs_video_read(span(file, 6), &v, &e) == ES2_ERR_TRUNCATED);
    CHECK(es2_prefs_video_read(span(file, 7), &v, &e) == ES2_OK && v.resolution == 5 && v.display == 7);
    CHECK(es2_prefs_video_mode(1) == 0 && es2_prefs_video_mode(0) == 3 && es2_prefs_video_mode(2) == 3 && es2_prefs_video_mode(255) == 3);
}

static void test_ini(void)
{
    char out[16];
    int found = 7;
    es2_error e;
    es2_span f = text("; leading comment\r\n[Other]\r\nKey = nope\r\n\r\n[Sound]\r\n  driver\t=  DirectSound  \r\n"
                      "Quoted = \"a b\"\r\nSingle='q'\r\nLong = 0123456789ABCDEFGHIJ\r\nEmpty =\r\nNoEquals\r\n"
                      "Dup = first\r\nDup = second\r\n;Hidden = x\r\n[sound]\r\nLate = in second section\r\n\x1a");
    CHECK(es2_cfg_ini_get(f, "Sound", "Driver", out, sizeof out, &found, &e) == ES2_OK && found == 1 && !strcmp(out, "DirectSound"));
    CHECK(es2_cfg_ini_get(f, "SOUND", "DRIVER", out, sizeof out, &found, &e) == ES2_OK && found == 1 && !strcmp(out, "DirectSound"));
    CHECK(es2_cfg_ini_get(f, "Sound", "Quoted", out, sizeof out, &found, &e) == ES2_OK && !strcmp(out, "a b"));
    CHECK(es2_cfg_ini_get(f, "Sound", "Single", out, sizeof out, &found, &e) == ES2_OK && !strcmp(out, "q"));
    CHECK(es2_cfg_ini_get(f, "Sound", "Long", out, sizeof out, &found, &e) == ES2_OK && !strcmp(out, "0123456789ABCDE") && strlen(out) == 15);
    CHECK(es2_cfg_ini_get(f, "Sound", "Empty", out, sizeof out, &found, &e) == ES2_OK && found == 1 && out[0] == 0);
    CHECK(es2_cfg_ini_get(f, "Sound", "NoEquals", out, sizeof out, &found, &e) == ES2_OK && found == 0 && out[0] == 0);
    CHECK(es2_cfg_ini_get(f, "Sound", "Dup", out, sizeof out, &found, &e) == ES2_OK && !strcmp(out, "first"));
    CHECK(es2_cfg_ini_get(f, "Sound", "Hidden", out, sizeof out, &found, &e) == ES2_OK && found == 0);
    CHECK(es2_cfg_ini_get(f, "Sound", "Key", out, sizeof out, &found, &e) == ES2_OK && found == 0);   /* other section */
    CHECK(es2_cfg_ini_get(f, "Other", "Key", out, sizeof out, &found, &e) == ES2_OK && found == 1 && !strcmp(out, "nope"));
    CHECK(es2_cfg_ini_get(f, "Sound", "Late", out, sizeof out, &found, &e) == ES2_OK && found == 1);    /* repeated section header */
    CHECK(es2_cfg_ini_get(f, "Missing", "Driver", out, sizeof out, &found, &e) == ES2_OK && found == 0);
    /* LF only, no final newline, capacity 1 */
    CHECK(es2_cfg_ini_get(text("[S]\nK=v"), "S", "K", out, sizeof out, &found, &e) == ES2_OK && found == 1 && !strcmp(out, "v"));
    CHECK(es2_cfg_ini_get(text("[S]\nK=v"), "S", "K", out, 1, &found, &e) == ES2_OK && found == 1 && out[0] == 0);
    CHECK(es2_cfg_ini_get(span(NULL, 0), "S", "K", out, sizeof out, &found, &e) == ES2_OK && found == 0 && out[0] == 0);
    CHECK(es2_cfg_ini_get(f, NULL, "K", out, sizeof out, &found, &e) == ES2_ERR_ARGUMENT);
    CHECK(es2_cfg_ini_get(f, "S", "K", out, 0, &found, &e) == ES2_ERR_ARGUMENT);
    CHECK(es2_cfg_ini_get(f, "S", "K", NULL, 4, &found, &e) == ES2_ERR_ARGUMENT);
}

static void test_sound_keyjoy(void)
{
    es2_cfg_sound s;
    es2_cfg_keyjoy k;
    es2_error e;
    unsigned i;
    /* No file: every default is empty (docs/formats/cfg.md). */
    CHECK(es2_cfg_sound_parse(span(NULL, 0), ES2_CFG_READER_DBSIM, &s, &e) == ES2_OK);
    CHECK(s.driver == ES2_SOUND_DRIVER_MME && s.buffers == 5 && s.rate_code == 0x20 && s.width_code == 8);
    /* The shipped content shape. */
    CHECK(es2_cfg_sound_parse(text("[Sound]\r\n\r\nDriver = MME\r\nBuffers = 5\r\nRate = 22\r\nWidth = Stereo\r\n"),
                              ES2_CFG_READER_DBSIM, &s, &e) == ES2_OK);
    CHECK(s.driver == 1 && s.buffers == 5 && s.rate_code == 0x20 && s.width_code == 8);
    CHECK(es2_cfg_sound_parse(text("[Sound]\nDriver=directsound\nBuffers=64\nRate=11\nWidth=MONO\n"),
                              ES2_CFG_READER_DBSIM, &s, &e) == ES2_OK);
    CHECK(s.driver == 2 && s.buffers == 64 && s.rate_code == 0x10 && s.width_code == 4);
    /* VSHELL reads Driver but always selects MME. */
    CHECK(es2_cfg_sound_parse(text("[Sound]\nDriver=DirectSound\n"), ES2_CFG_READER_VSHELL, &s, &e) == ES2_OK && s.driver == 1);
    {
        static const struct { const char *value; unsigned expect; } buffers[] = {
            {"0", 5}, {"1", 1}, {"64", 64}, {"65", 5}, {"-3", 5}, {"12abc", 12}, {"abc", 5}, {"  9", 9}, {"+7", 7}, {"", 5}
        };
        char file[64];
        for (i = 0; i < sizeof buffers / sizeof buffers[0]; ++i) {
            sprintf(file, "[Sound]\nBuffers = %s\n", buffers[i].value);
            CHECK(es2_cfg_sound_parse(text(file), ES2_CFG_READER_DBSIM, &s, &e) == ES2_OK && s.buffers == buffers[i].expect);
        }
        CHECK(es2_cfg_sound_parse(text("[Sound]\nRate = 011\n"), ES2_CFG_READER_DBSIM, &s, &e) == ES2_OK && s.rate_code == 0x10);
        CHECK(es2_cfg_sound_parse(text("[Sound]\nRate = 11.5\n"), ES2_CFG_READER_DBSIM, &s, &e) == ES2_OK && s.rate_code == 0x10);
        CHECK(es2_cfg_sound_parse(text("[Sound]\nRate = 12\n"), ES2_CFG_READER_DBSIM, &s, &e) == ES2_OK && s.rate_code == 0x20);
        CHECK(es2_cfg_sound_parse(text("[Other]\nRate = 11\n"), ES2_CFG_READER_DBSIM, &s, &e) == ES2_OK && s.rate_code == 0x20);
    }
    CHECK(es2_cfg_sound_parse(text("[Sound]\nDriver = DirectSound\n"), (es2_cfg_reader)99, &s, &e) == ES2_ERR_ARGUMENT);
    CHECK(es2_cfg_sound_parse(text(""), ES2_CFG_READER_DBSIM, NULL, &e) == ES2_ERR_ARGUMENT);

    CHECK(es2_cfg_keyjoy_parse(span(NULL, 0), &k, &e) == ES2_OK && !k.tilt && !k.backturn && !k.missile && !k.rudder);
    CHECK(es2_cfg_keyjoy_parse(text("[Keyjoy]\r\nTilt = Default\r\nBackturn = Default\r\nMissile = Default\r\nRudder = Default\r\n"),
                               &k, &e) == ES2_OK && !k.tilt && !k.backturn && !k.missile && !k.rudder);
    CHECK(es2_cfg_keyjoy_parse(text("[Keyjoy]\nTilt = Reverse\nBackturn=reverse\nMissile = REVERSE\nRudder = Reversed\n"),
                               &k, &e) == ES2_OK && k.tilt == 1 && k.backturn == 1 && k.missile == 1 && k.rudder == 0);
    CHECK(es2_cfg_keyjoy_parse(text("[Keyjoy]\nRudder = Reverse\n"), &k, &e) == ES2_OK && !k.tilt && !k.backturn && !k.missile && k.rudder == 1);
    CHECK(es2_cfg_keyjoy_parse(text("[Sound]\nTilt = Reverse\n"), &k, &e) == ES2_OK && !k.tilt);
    CHECK(es2_cfg_keyjoy_parse(text(""), NULL, &e) == ES2_ERR_ARGUMENT);
}

/* ---- tapes ---------------------------------------------------------------- */

static void test_tape(void)
{
    uint8_t b[256];
    es2_tape_header h;
    es2_tape_entry t;
    es2_error e;
    size_t at = 0;
    unsigned i;
    memset(b, 0, sizeof b);
    for (i = 0; i < ES2_TAPE_MEMBERS; ++i) {
        put32(b + at, i == 5 ? 0u : i + 1u); at += 4;
        if (i != 5) { memset(b + at, 0x30 + (int)i, i + 1); at += i + 1; }
    }
    CHECK(es2_tape_header_parse(span(b, at), &h, &e) == ES2_OK && h.size == at);
    CHECK(h.member[0].size == 1 && h.member[0].data[0] == 0x30 && h.member[5].size == 0 && h.member[6].size == 7);
    CHECK(es2_tape_header_parse(span(b, at - 1), &h, &e) == ES2_ERR_TRUNCATED);
    CHECK(!strcmp(es2_tape_member_name(0), "script.dat") && !strcmp(es2_tape_member_name(6), "keyjoy.cfg"));
    CHECK(es2_tape_member_name(7) == NULL && es2_tape_member_in_tapes(3) && es2_tape_member_in_tapes(6) && !es2_tape_member_in_tapes(0));
    /* First entry: prefix, record, one event, two keys; second: plain. */
    {
        size_t first = at;
        memset(b + at, 0xa5, 8); at += 8;
        put16(b + at, 0x412); put16(b + at + 2, 1); put16(b + at + 4, 2); put16(b + at + 6, 3); put16(b + at + 8, 4);
        put16(b + at + 10, 0x100); put32(b + at + 12, 0x10203u); at += 16;
        put32(b + at, 1); put32(b + at + 4, 2); at += 8;
        memset(b + at, 0x5e, 14); at += 14;
        put16(b + at, 0x430); put16(b + at + 2, 0x431); at += 4;
        CHECK(es2_tape_entry_parse(span(b, at), first, 1, &t, &e) == ES2_OK && t.size == at - first);
        CHECK(t.prefix.size == 8 && t.key == 0x412 && t.axis[3] == 4 && t.time_scale == 0x100 && t.bits == 0x10203u);
        CHECK(t.event_count == 1 && t.events.size == 14 && t.key_count == 2 && t.keys.data[2] == 0x31);
        CHECK(es2_tape_entry_parse(span(b, at), first, 0, &t, &e) == ES2_ERR_TRUNCATED);
        CHECK(es2_tape_entry_parse(span(b, at - 1), first, 1, &t, &e) == ES2_ERR_TRUNCATED);
        {
            size_t second = at;
            memset(b + at, 0, 24); put32(b + at + 16, 0xffffffffu); at += 24;
            CHECK(es2_tape_entry_parse(span(b, at), second, 0, &t, &e) == ES2_ERR_TRUNCATED);
            put32(b + second + 16, 0);
            CHECK(es2_tape_entry_parse(span(b, at), second, 0, &t, &e) == ES2_OK && t.size == 24 && t.prefix.size == 0);
        }
        CHECK(es2_tape_entry_parse(span(b, at), at + 1, 0, &t, &e) == ES2_ERR_OFFSET);
    }
}

int main(void)
{
    test_str_basic();
    test_str_errors();
    test_str_aux();
    test_str_tag();
    test_prefs();
    test_ini();
    test_sound_keyjoy();
    test_tape();
    printf("str/cfg synthetic: %u checks, %u failures\n", checks, failures);
    return failures != 0;
}

#else
/* ---- real data: read-only, nothing kept ---------------------------------- */
#include "es2/formats/vol.h"
#include <dirent.h>
#include <sys/stat.h>

static uint8_t *read_file(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    long length;
    uint8_t *data;
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0 || (length = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    data = (uint8_t *)malloc((size_t)length + 1);
    if (data && fread(data, 1, (size_t)length, f) != (size_t)length) { free(data); data = NULL; }
    fclose(f);
    if (data) *size = (size_t)length;
    return data;
}

static int ends_with(const char *s, const char *suffix)
{
    size_t n = strlen(s), m = strlen(suffix), i;
    if (m > n) return 0;
    for (i = 0; i < m; ++i) {
        char a = s[n - m + i], b = suffix[i];
        if (a >= 'a' && a <= 'z') a = (char)(a - 32);
        if (b >= 'a' && b <= 'z') b = (char)(b - 32);
        if (a != b) return 0;
    }
    return 1;
}

static unsigned str_files, str_groups, str_strings, str_aux_strings, cfg_files, vol_members_seen;

/* Structural invariants of one packed string file; `name` is for messages. */
static void check_str(const char *name, es2_span bytes, int expect_exact)
{
    es2_str s;
    es2_error e;
    es2_str_cursor c;
    size_t g, count, total = 0;
    uint8_t *rebuilt = NULL;
    es2_str_group_in *groups = NULL;
    es2_str_string *strings = NULL;
    size_t need = 0, k = 0;
    es2_status status = es2_str_parse(bytes, 0, 0, &s, &e);
    if (status != ES2_OK) { fprintf(stderr, "%s: parse failed (%s at %zu)\n", name, es2_status_string(status), e.offset); CHECK(0); return; }
    ++str_files;
    str_groups += (unsigned)s.group_count;
    str_strings += (unsigned)s.string_count;
    if (expect_exact) CHECK(s.trailing.size == 0);
    CHECK(s.groups_end == s.body.size);
    groups = (es2_str_group_in *)calloc(s.group_count ? s.group_count : 1, sizeof *groups);
    strings = (es2_str_string *)calloc(s.string_count ? s.string_count : 1, sizeof *strings);
    if (!groups || !strings) { CHECK(0); free(groups); free(strings); return; }
    CHECK(es2_str_cursor_init(&s, &c, &e) == ES2_OK);
    for (g = 0; g < s.group_count; ++g) {
        size_t i;
        if (es2_str_next_group(&c, &count, &e) != ES2_OK) { CHECK(0); break; }
        groups[g].strings = strings + k;
        groups[g].count = count;
        for (i = 0; i < count; ++i) {
            es2_str_string str;
            if (es2_str_next_string(&c, &str, &e) != ES2_OK) { CHECK(0); break; }
            CHECK(str.text.data[str.text.size] == 0);
            CHECK(memchr(str.text.data, 0, str.text.size) == NULL);
            if (str.aux.size) ++str_aux_strings;
            strings[k++] = str;
            ++total;
        }
    }
    CHECK(total == s.string_count);
    /* The writer reproduces body and header byte for byte. */
    CHECK(es2_str_write(groups, s.group_count, 0, NULL, 0, &need, &e) == ES2_OK && need == 4 + s.body.size);
    rebuilt = (uint8_t *)malloc(need ? need : 1);
    if (rebuilt && es2_str_write(groups, s.group_count, 0, rebuilt, need, &need, &e) == ES2_OK)
        CHECK(memcmp(rebuilt, bytes.data, need) == 0);
    else CHECK(0);
    free(rebuilt); free(groups); free(strings);
}

/* Group-count signatures that are structural facts of the shipped files
 * (no text): looked up by the base name of the file. */
static void check_signature(const char *base, es2_span bytes)
{
    static const struct { const char *name; size_t n; size_t g[3]; } sigs[] = {
        {"ERROR.STR", 2, {6, 3, 0}}, {"ERROR.ENG", 2, {12, 3, 0}}, {"ERROR.FRE", 2, {12, 3, 0}},
        {"ERROR.GER", 2, {12, 3, 0}}, {"GAMEFILE.STR", 2, {12, 12, 0}}, {"GNL_ALRT.STR", 3, {20, 40, 80}},
        {"MAPLABEL.STR", 2, {2, 11, 0}}, {"PILOTS.STR", 1, {36, 0, 0}}, {"OBJ_ALRT.STR", 2, {1, 1, 0}},
        {"CAMPAIGN.STR", 1, {5, 0, 0}}
    };
    size_t i;
    es2_str s;
    for (i = 0; i < sizeof sigs / sizeof sigs[0]; ++i) {
        size_t j;
        if (!ends_with(base, sigs[i].name)) continue;
        if (es2_str_parse(bytes, 0, 0, &s, NULL) != ES2_OK) { CHECK(0); return; }
        CHECK(s.group_count == sigs[i].n);
        for (j = 0; j < sigs[i].n && j < s.group_count; ++j) {
            size_t count = 0;
            CHECK(es2_str_group_size(&s, j, &count, NULL) == ES2_OK && count == sigs[i].g[j]);
        }
        /* The launcher's view of error files: two groups, the rest ignored. */
        if (ends_with(base, "ERROR.STR") || ends_with(base, "ERROR.ENG") || ends_with(base, "ERROR.FRE") || ends_with(base, "ERROR.GER"))
            CHECK(es2_str_parse(bytes, 2, ES2_STR_TAG_BYTE, &s, NULL) == ES2_OK && s.group_count == 2);
    }
}

/* Tapes: the header and every entry parse exactly to the end of the file;
 * the CD's demo tapes have the entry counts of cfg.md, "Tape files". */
static unsigned tape_files, tape_entries;
static void check_tape(const char *base, es2_span bytes)
{
    es2_tape_header h;
    es2_tape_entry t;
    es2_error e;
    size_t at;
    unsigned n = 0;
    ++tape_files;
    CHECK(es2_tape_header_parse(bytes, &h, &e) == ES2_OK);
    if (e.code != ES2_OK) return;
    CHECK(h.member[3].size == ES2_PREFS_SIZE);
    for (at = h.size; at < bytes.size; at += t.size, ++n)
        if (es2_tape_entry_parse(bytes, at, n == 0, &t, &e) != ES2_OK) { CHECK(0); return; }
    CHECK(at == bytes.size);
    tape_entries += n;
    if (ends_with(base, "DEMO1.TAP")) CHECK(n == 1499);
    if (ends_with(base, "DEMO2.TAP")) CHECK(n == 1901);
    if (ends_with(base, "DEMO3.TAP")) CHECK(n == 3660);
    printf("tape %s: header %u bytes (%u/%u/%u/%u/%u/%u/%u), %u entries\n", base, (unsigned)h.size,
           (unsigned)h.member[0].size, (unsigned)h.member[1].size, (unsigned)h.member[2].size, (unsigned)h.member[3].size,
           (unsigned)h.member[4].size, (unsigned)h.member[5].size, (unsigned)h.member[6].size, n);
}

static void check_cfg(const char *path, const char *base, es2_span bytes)
{
    (void)path;
    ++cfg_files;
    if (ends_with(base, "PREFS.CFG") && !ends_with(base, "PBPREFS.CFG")) {
        es2_prefs p;
        es2_prefs_video v;
        uint8_t image[ES2_PREFS_SIZE];
        CHECK(bytes.size == ES2_PREFS_SIZE);
        CHECK(es2_prefs_load(bytes, &p, NULL) == ES2_OK);
        es2_prefs_serialize(&p, image);
        CHECK(bytes.size == ES2_PREFS_SIZE && memcmp(image, bytes.data, ES2_PREFS_SIZE) == 0);
        CHECK(es2_prefs_video_read(bytes, &v, NULL) == ES2_OK);
        CHECK(v.display <= 1);
        CHECK(p.byte[ES2_PREFS_SHELL_MODE] <= 1 && p.byte[ES2_PREFS_REPAIR] <= 2 && p.byte[ES2_PREFS_BUILD] <= 1);
    } else if (ends_with(base, "SOUND.CFG")) {
        es2_cfg_sound s;
        char v[40];
        int found = 0;
        CHECK(es2_cfg_ini_get(bytes, "Sound", "Driver", v, sizeof v, &found, NULL) == ES2_OK && found == 1);
        CHECK(es2_cfg_ini_get(bytes, "Sound", "Buffers", v, sizeof v, &found, NULL) == ES2_OK && found == 1);
        CHECK(es2_cfg_ini_get(bytes, "Sound", "Rate", v, sizeof v, &found, NULL) == ES2_OK && found == 1);
        CHECK(es2_cfg_ini_get(bytes, "Sound", "Width", v, sizeof v, &found, NULL) == ES2_OK && found == 1);
        CHECK(es2_cfg_sound_parse(bytes, ES2_CFG_READER_DBSIM, &s, NULL) == ES2_OK);
        CHECK(s.driver == ES2_SOUND_DRIVER_MME && s.buffers >= 1 && s.buffers <= 64 && (s.rate_code == 0x10 || s.rate_code == 0x20));
        CHECK(s.width_code == 4 || s.width_code == 8);
        CHECK(es2_cfg_sound_parse(bytes, ES2_CFG_READER_VSHELL, &s, NULL) == ES2_OK && s.driver == ES2_SOUND_DRIVER_MME);
    } else if (ends_with(base, "KEYJOY.CFG")) {
        es2_cfg_keyjoy k;
        char v[40];
        int found = 0;
        static const char *const keys[] = {"Tilt", "Backturn", "Missile", "Rudder"};
        unsigned i;
        for (i = 0; i < 4; ++i) CHECK(es2_cfg_ini_get(bytes, "Keyjoy", keys[i], v, sizeof v, &found, NULL) == ES2_OK && found == 1);
        CHECK(es2_cfg_keyjoy_parse(bytes, &k, NULL) == ES2_OK);
        CHECK(k.tilt <= 1 && k.backturn <= 1 && k.missile <= 1 && k.rudder <= 1);
    } else if (ends_with(base, "EXIT.CFG")) {
        CHECK(bytes.size == 2);
    } else {
        /* Other .cfg files of the media (joystick, demo prefs): the INI reader must simply not fail. */
        char v[40];
        int found = 0;
        CHECK(es2_cfg_ini_get(bytes, "Joystick", "Hat", v, sizeof v, &found, NULL) == ES2_OK);
    }
}

static void check_vol(const char *path)
{
    size_t size = 0, i;
    uint8_t *data = read_file(path, &size);
    es2_vol vol;
    if (!data) { CHECK(0); return; }
    if (es2_vol_open(span(data, size), &vol, NULL) == ES2_OK) {
        for (i = 0; i < vol.member_count; ++i) {
            es2_vol_member m;
            char name[64];
            if (es2_vol_member_at(&vol, i, &m, NULL) != ES2_OK || m.name.size + m.directory.size >= sizeof name) continue;
            memcpy(name, m.directory.data, m.directory.size);
            memcpy(name + m.directory.size, m.name.data, m.name.size);
            name[m.directory.size + m.name.size] = 0;
            ++vol_members_seen;
            if (ends_with(name, ".STR")) {
                check_str(name, m.payload, 1);
                check_signature(name, m.payload);
            }
        }
    }
    free(data);
}

static void walk(const char *dir, int depth)
{
    DIR *d = opendir(dir);
    struct dirent *entry;
    if (!d) return;
    while ((entry = readdir(d)) != NULL) {
        char path[4096];
        struct stat st;
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        if (snprintf(path, sizeof path, "%s/%s", dir, entry->d_name) >= (int)sizeof path) continue;
        if (stat(path, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) { if (depth < 6) walk(path, depth + 1); continue; }
        if (ends_with(entry->d_name, ".VOL")) { check_vol(path); continue; }
        if (ends_with(entry->d_name, ".STR") || ends_with(entry->d_name, ".ENG") ||
            ends_with(entry->d_name, ".FRE") || ends_with(entry->d_name, ".GER") || ends_with(entry->d_name, ".CFG") ||
            ends_with(entry->d_name, ".TAP")) {
            size_t size = 0;
            uint8_t *data = read_file(path, &size);
            if (!data) { fprintf(stderr, "cannot read %s\n", path); CHECK(0); continue; }
            if (ends_with(entry->d_name, ".TAP")) check_tape(entry->d_name, span(data, size));
            else if (ends_with(entry->d_name, ".CFG")) check_cfg(path, entry->d_name, span(data, size));
            else if (ends_with(entry->d_name, "DEMO.STR") && size == 4) {
                /* The root DEMO.STR is a four-byte text file, not a packed STR. */
                es2_str s;
                CHECK(es2_str_parse(span(data, size), 0, 0, &s, NULL) == ES2_ERR_LENGTH);
            } else {
                check_str(path, span(data, size), 1);
                check_signature(entry->d_name, span(data, size));
            }
            free(data);
        }
    }
    closedir(d);
}

int main(void)
{
    const char *root = getenv("ES2_DATA_ROOT");
    if (!root || !*root) { puts("SKIP real str/cfg: ES2_DATA_ROOT not set"); return 0; }
    walk(root, 0);
    printf("str/cfg real: %u STR files (%u groups, %u strings, %u with auxiliary data), %u CFG files, %u tapes (%u entries), %u VOL members seen; %u checks, %u failures\n",
           str_files, str_groups, str_strings, str_aux_strings, cfg_files, tape_files, tape_entries, vol_members_seen, checks, failures);
    CHECK(str_files > 0 || vol_members_seen == 0);
    return failures != 0;
}
#endif
