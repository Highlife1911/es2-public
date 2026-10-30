/* es2/shell/slots.h on synthetic slot tables (own strings only). The
 * byte-for-byte comparison with VSHELL's own functions is slots_real_test. */
#include "es2/shell/slots.h"
#include <stdio.h>
#include <string.h>

static unsigned checks, failures;
#define CHECK(x) do { checks++; if (!(x)) { failures++; fprintf(stderr, "line %d: %s\n", __LINE__, #x); } } while (0)

static const es2_span EMPTY = {(const uint8_t *)"NONE", 4};

typedef struct builder { uint8_t b[2048]; size_t n; } builder;
static void w16(builder *x, unsigned v) { x->b[x->n++] = (uint8_t)v; x->b[x->n++] = (uint8_t)(v >> 8); }
static void entry(builder *x, const char *s, size_t length, uint8_t tag) {
    w16(x, (unsigned)length);
    memcpy(x->b + x->n, s, length);
    x->n += length;
    x->b[x->n++] = tag;
}
/* File names F0..F11; texts " 1. " (empty name), " 2. AB", "abc", "", the
 * rest "Rk" (k = slot); flags on slots 1 and 10. */
static size_t table(builder *x, int count0, int count1) {
    char s[32];
    x->n = 4;
    w16(x, (unsigned)count0);
    for (int i = 0; i < count0; i++) { snprintf(s, sizeof s, "F%d", i); entry(x, s, strlen(s) + 1, 9); }
    w16(x, (unsigned)count1);
    for (int i = 0; i < count1; i++) {
        if (i == 0) snprintf(s, sizeof s, " 1. ");
        else if (i == 1) snprintf(s, sizeof s, " 2. AB");
        else if (i == 2) snprintf(s, sizeof s, "abc");
        else if (i == 3) s[0] = 0;
        else snprintf(s, sizeof s, "R%d", i);
        entry(x, s, strlen(s) + 1, (uint8_t)(i == 1 || i == 10));
    }
    x->b[0] = (uint8_t)(x->n - 4); x->b[1] = (uint8_t)((x->n - 4) >> 8); x->b[2] = x->b[3] = 0;
    return x->n;
}

static void load(void) {
    builder x;
    es2_shell_slot_table t;
    es2_error e;
    table(&x, 12, 12);
    CHECK(es2_shell_slots_load((es2_span){x.b, x.n}, EMPTY, &t, &e) == ES2_OK);
    CHECK(!strcmp(t.slot[10].file, "F10") && !strcmp(t.slot[11].file, "F11"));
    /* Fifth byte zero: the empty text is appended (texts shorter than 5 too). */
    CHECK(!strcmp(t.slot[0].text, " 1. NONE") && !strcmp(t.slot[1].text, " 2. AB"));
    CHECK(!strcmp(t.slot[2].text, "abcNONE") && !strcmp(t.slot[3].text, "NONE") && !strcmp(t.slot[4].text, "R4NONE"));
    CHECK(t.slot[1].flag == 1 && t.slot[10].flag == 1 && t.slot[0].flag == 0);
    /* Fewer entries leave the rest zero; a negative count reads nothing. */
    table(&x, 12, 10);
    CHECK(es2_shell_slots_load((es2_span){x.b, x.n}, EMPTY, &t, &e) == ES2_OK && !t.slot[11].text[0] && !t.slot[10].flag);
    table(&x, 0, 0);
    x.b[4] = 0xff; x.b[5] = 0xff;   /* count0 -1 */
    CHECK(es2_shell_slots_load((es2_span){x.b, x.n}, EMPTY, &t, &e) == ES2_OK && !t.slot[0].file[0]);
    /* Refusals. */
    table(&x, 13, 12);
    CHECK(es2_shell_slots_load((es2_span){x.b, x.n}, EMPTY, &t, &e) == ES2_ERR_COUNT);
    table(&x, 12, 12);
    CHECK(es2_shell_slots_load((es2_span){x.b, x.n - 1}, EMPTY, &t, &e) == ES2_ERR_TRUNCATED);
    x.n = 4; w16(&x, 1); entry(&x, "ABCDEFGHIJKLMN", 14, 0); w16(&x, 0);
    CHECK(es2_shell_slots_load((es2_span){x.b, x.n}, EMPTY, &t, &e) == ES2_ERR_LENGTH);
    x.n = 4; w16(&x, 1); entry(&x, "ABCDEFGHIJKLM", 13, 0); w16(&x, 0);
    CHECK(es2_shell_slots_load((es2_span){x.b, x.n}, EMPTY, &t, &e) == ES2_ERR_STRING);
    {
        char long_text[80];
        memset(long_text, 'x', sizeof long_text);
        x.n = 4; w16(&x, 0); w16(&x, 1); entry(&x, long_text, 80, 0);
        CHECK(es2_shell_slots_load((es2_span){x.b, x.n}, EMPTY, &t, &e) == ES2_ERR_STRING);  /* no NUL in 80 */
        long_text[79] = 0;                      /* 79 x, fifth byte nonzero: no append */
        x.n = 4; w16(&x, 0); w16(&x, 1); entry(&x, long_text, 80, 0);
        CHECK(es2_shell_slots_load((es2_span){x.b, x.n}, EMPTY, &t, &e) == ES2_OK && strlen(t.slot[0].text) == 79);
        memset(long_text, 'y', sizeof long_text);
        long_text[4] = 0; long_text[3] = 0;     /* "yyy" and other bytes: the append starts at the NUL */
        x.n = 4; w16(&x, 0); w16(&x, 1); entry(&x, long_text, 77, 0);
        CHECK(es2_shell_slots_load((es2_span){x.b, x.n}, EMPTY, &t, &e) == ES2_OK && !strcmp(t.slot[0].text, "yyyNONE"));
        /* An appended text that does not fit the record. */
        memset(long_text, 'q', sizeof long_text);
        x.n = 4; w16(&x, 0); w16(&x, 1); entry(&x, " 1. ", 5, 0);
        CHECK(es2_shell_slots_load((es2_span){x.b, x.n}, (es2_span){(const uint8_t *)long_text, 75}, &t, &e) == ES2_OK &&
              strlen(t.slot[0].text) == 79);
        CHECK(es2_shell_slots_load((es2_span){x.b, x.n}, (es2_span){(const uint8_t *)long_text, 76}, &t, &e) == ES2_ERR_STRING);
    }
    /* An empty text is needed only where a text gets it. */
    table(&x, 12, 12);
    CHECK(es2_shell_slots_load((es2_span){x.b, x.n}, (es2_span){0}, &t, &e) == ES2_OK && !strcmp(t.slot[0].text, " 1. "));
}

static void write_and_mark(void) {
    builder x;
    es2_shell_slot_table t;
    uint8_t out[2048], again[2048], tail[2048];
    size_t n, m, k;
    char name[ES2_SHELL_SLOT_FILE], row[ES2_SHELL_SLOT_TEXT];
    int effective;
    es2_error e;
    table(&x, 12, 12);
    CHECK(es2_shell_slots_load((es2_span){x.b, x.n}, EMPTY, &t, &e) == ES2_OK);
    CHECK(es2_shell_slots_write(&t, (es2_span){0}, NULL, 0, &n, &e) == ES2_OK);
    CHECK(es2_shell_slots_write(&t, (es2_span){0}, out, n - 1, &m, &e) == ES2_ERR_CAPACITY && m == n);
    CHECK(es2_shell_slots_write(&t, (es2_span){0}, out, sizeof out, &m, &e) == ES2_OK && m == n);
    /* Group 0 tags become 0; group 1 carries texts as loaded and the flags. */
    CHECK(out[0] == (uint8_t)(n - 4) && out[4] == 12 && out[6] == 3 && !memcmp(out + 8, "F0", 3) && out[11] == 0);
    {
        es2_shell_slot_table u;
        CHECK(es2_shell_slots_load((es2_span){out, m}, (es2_span){0}, &u, &e) == ES2_OK && !memcmp(&u, &t, sizeof t));
    }
    /* Non-truncating rewrite over a longer previous file keeps its tail. */
    memset(tail, 0xab, sizeof tail);
    CHECK(es2_shell_slots_write(&t, (es2_span){tail, n + 7}, out, sizeof out, &m, &e) == ES2_OK && m == n + 7);
    CHECK(out[0] == (uint8_t)(n + 3) && !memcmp(out + n, tail + n, 7));
    CHECK(es2_shell_slots_write(&t, (es2_span){tail, n - 7}, out, sizeof out, &m, &e) == ES2_OK && m == n);
    CHECK(es2_shell_slots_write(&t, (es2_span){out, n}, out, sizeof out, &m, &e) == ES2_ERR_OVERLAP);
    /* Mark: text and flag for 0..9, only the flag for 10/11. */
    CHECK(es2_shell_slots_mark(&t, 3, " 4. NEW", &e) == ES2_OK && !strcmp(t.slot[3].text, " 4. NEW") && t.slot[3].flag == 1);
    CHECK(es2_shell_slots_mark(&t, 11, "ignored", &e) == ES2_OK && !strcmp(t.slot[11].text, "R11NONE") && t.slot[11].flag == 1);
    CHECK(es2_shell_slots_mark(&t, 5, NULL, &e) == ES2_ERR_ARGUMENT && t.slot[5].flag == 0);
    {
        char long_text[100];
        memset(long_text, 'z', 80); long_text[80] = 0;
        CHECK(es2_shell_slots_mark(&t, 5, long_text, &e) == ES2_ERR_STRING);
        long_text[79] = 0;
        CHECK(es2_shell_slots_mark(&t, 5, long_text, &e) == ES2_OK);
    }
    CHECK(es2_shell_slots_mark(&t, 12, NULL, &e) == ES2_ERR_ARGUMENT && es2_shell_slots_mark(&t, -1, NULL, &e) == ES2_ERR_ARGUMENT);
    CHECK(es2_shell_slot_effective(10, 0) == 11 && es2_shell_slot_effective(10, 1) == 10 &&
          es2_shell_slot_effective(3, 0) == 3 && es2_shell_slot_effective(11, 1) == 11);
    /* Rows: "%2d. %s", the edit set, the own length limit. */
    CHECK(es2_shell_slot_row(0, "Ab 9^z", row, &e) == ES2_OK && !strcmp(row, " 1. Ab 9^z"));
    CHECK(es2_shell_slot_row(9, "", row, &e) == ES2_OK && !strcmp(row, "10. "));
    CHECK(es2_shell_slot_row(2, "a!b", row, &e) == ES2_ERR_STRING && es2_shell_slot_row(2, "a_b", row, &e) == ES2_ERR_STRING);
    CHECK(es2_shell_slot_row(2, "abcdefghijklmnopqrstuvwx", row, &e) == ES2_OK);
    CHECK(es2_shell_slot_row(2, "abcdefghijklmnopqrstuvwxy", row, &e) == ES2_ERR_LENGTH);
    CHECK(es2_shell_slot_row(10, "a", row, &e) == ES2_ERR_ARGUMENT);
    /* The composite: load, effective slot, mark, write; idempotent for 10. */
    table(&x, 12, 12);
    CHECK(es2_shell_slot_gamefile((es2_span){x.b, x.n}, EMPTY, 10, NULL, 0, &effective, name, NULL, 0, &n, &e) == ES2_OK &&
          effective == 11 && !strcmp(name, "F11"));
    CHECK(es2_shell_slot_gamefile((es2_span){x.b, x.n}, EMPTY, 10, NULL, 1, &effective, name, out, sizeof out, &m, &e) == ES2_OK &&
          effective == 10 && !strcmp(name, "F10"));
    /* Saving again keeps the image, except that a text still shorter than
     * five bytes after its append (slot 3: "NONE") gets it again at every
     * load, as in the original. */
    CHECK(es2_shell_slot_gamefile((es2_span){out, m}, EMPTY, 10, NULL, 1, &effective, name, again, sizeof again, &k, &e) == ES2_OK &&
          k == m + 4);
    CHECK(es2_shell_slots_load((es2_span){again, k}, (es2_span){0}, &t, &e) == ES2_OK && !strcmp(t.slot[3].text, "NONENONE") &&
          !strcmp(t.slot[2].text, "abcNONE") && !strcmp(t.slot[0].text, " 1. NONE"));
    CHECK(es2_shell_slot_gamefile((es2_span){x.b, x.n}, EMPTY, 4, " 5. X", 1, &effective, name, out, sizeof out, &m, &e) == ES2_OK);
    CHECK(es2_shell_slots_load((es2_span){out, m}, EMPTY, &t, &e) == ES2_OK && !strcmp(t.slot[4].text, " 5. X") &&
          t.slot[4].flag == 1 && !strcmp(t.slot[0].text, " 1. NONE"));
    x.n = 4; w16(&x, 0); w16(&x, 0);   /* no entries: slot 10 has no file name */
    CHECK(es2_shell_slot_gamefile((es2_span){x.b, x.n}, EMPTY, 10, NULL, 1, &effective, name, out, sizeof out, &m, &e) == ES2_ERR_NOT_FOUND);
}

static void companions(void) {
    char name[32];
    const char *data;
    es2_error e;
    CHECK(es2_shell_slot_companion(10, 0, name, sizeof name, &data, &e) == ES2_OK && !strcmp(name, "script10.dat") &&
          !strcmp(data, "script.dat"));
    CHECK(es2_shell_slot_companion(3, 1, name, sizeof name, &data, &e) == ES2_OK && !strcmp(name, "missn3.str") &&
          !strcmp(data, "mission.str"));
    CHECK(es2_shell_slot_companion(11, 2, name, sizeof name, &data, &e) == ES2_OK && !strcmp(name, "player11.mec") &&
          !strcmp(data, "player.mec"));
    CHECK(es2_shell_slot_companion(11, 3, name, sizeof name, &data, &e) == ES2_ERR_ARGUMENT);
    CHECK(es2_shell_slot_companion(11, 2, name, 5, &data, &e) == ES2_ERR_CAPACITY);
}

/* ---- Slot statistics on own SAV-shaped bytes ----------------------- */
typedef struct sav_builder { uint8_t b[8192]; size_t n; } sav_builder;
static void s8(sav_builder *x, unsigned v) { x->b[x->n++] = (uint8_t)v; }
static void s16(sav_builder *x, int v) { s8(x, (unsigned)v & 0xff); s8(x, ((unsigned)v >> 8) & 0xff); }
static void pad(sav_builder *x, size_t n, uint8_t v) { memset(x->b + x->n, v, n); x->n += n; }
/* A SAV-shaped file: 33 blocks with count k % 3, CAREER sector 3 mission 7,
 * 36 roster pilots, the player "PILOT" (name_length bytes of 'P' when not
 * negative 5), `records` hangar records with m = 2, salvage 123456. */
static size_t sav(sav_builder *x, int name_length, int records) {
    x->n = 0;
    for (int k = 0; k < 33; k++) { s8(x, 0xee); s16(x, k % 3); pad(x, 10u * (unsigned)(k % 3), 0xee); }
    pad(x, 22, 0xdd); s16(x, 3); s16(x, 7); pad(x, 148, 0xdd);
    for (int k = 0; k < 36; k++) { pad(x, 4, 0xcc); s16(x, 4); pad(x, 4 + 25, 0xcc); }
    pad(x, 16, 0xbb);
    s16(x, 0x1111); s16(x, 0x2222);
    if (name_length < 0) { s16(x, 5); memcpy(x->b + x->n, "PILOT", 5); x->n += 5; }
    else { s16(x, name_length); pad(x, (size_t)name_length, 'P'); }
    s16(x, 0x3333); s8(x, 0x44);
    for (int k = 0; k < 11; k++) s16(x, 0x100 + k);
    s16(x, records);
    for (int k = 0; k < (records > 0 ? records : 0); k++) { pad(x, 0x4e, 0xaa); s16(x, 2); pad(x, 24, 0xaa); }
    pad(x, 0x12, 0x99);
    s16(x, 123456 & 0xffff); s16(x, 123456 >> 16);
    return x->n;
}
static void statistics(void) {
    static sav_builder x;
    es2_shell_stats r, before, memory;
    es2_shell_stats_view v;
    es2_error e;
    size_t n = sav(&x, -1, 2);
    memset(&r, 0, sizeof r);
    CHECK(es2_shell_stats_read((es2_span){x.b, n}, &r, &e) == ES2_OK);
    CHECK(r.raw[0] == 0x11 && r.raw[2] == 0x22 && !memcmp(r.raw + 4, "PILOT", 6));
    CHECK(r.raw[0x22] == 0x33 && r.raw[0x24] == 0x44 && r.raw[0x25] == 0 && r.raw[0x26] == 1 && r.raw[0x39] == 10);
    CHECK(r.raw[0x3f] == 3 && r.raw[0x41] == 7 && r.raw[0x3b] == (123456 & 0xff));
    es2_shell_stats_show(&r, &v);
    CHECK(!strcmp(v.name, "PILOT") && v.skill_text == 0x35 + 0x100 && v.rank_text == 0x39 + 0x102);
    CHECK(v.kills[0] == 0x104 && v.kills[1] == 0x107 && v.kills[2] == 0x105 && v.kills[5] == 0x109);
    CHECK(v.salvage == 123 && v.salvage_text == 0x2f && v.sector_text == 0x79 && v.mission == 8);
    /* the original writes over the record: bytes the name does not reach stay */
    memset(&r, 0x5a, sizeof r);
    CHECK(es2_shell_stats_read((es2_span){x.b, n}, &r, &e) == ES2_OK && r.raw[8] == 'T' && r.raw[9] == 0x5a);
    /* a long name runs into the later fields, which overwrite it */
    memset(&r, 0, sizeof r);
    n = sav(&x, 59, 0);
    CHECK(es2_shell_stats_read((es2_span){x.b, n}, &r, &e) == ES2_OK && r.raw[0x21] == 'P' && r.raw[0x22] == 0x33 &&
          r.raw[0x3f] == 3 && r.raw[0x3b] == (123456 & 0xff));
    /* a negative hangar count reads no records */
    n = sav(&x, -1, -4);
    CHECK(es2_shell_stats_read((es2_span){x.b, n}, &r, &e) == ES2_OK);
    /* refusals leave the record untouched */
    before = r;
    n = sav(&x, 60, 0);
    CHECK(es2_shell_stats_read((es2_span){x.b, n}, &r, &e) == ES2_ERR_LENGTH && !memcmp(&r, &before, sizeof r));
    n = sav(&x, -1, 0);
    for (size_t cut = 0; cut < n; cut += 97)
        CHECK(es2_shell_stats_read((es2_span){x.b, cut}, &r, &e) == ES2_ERR_TRUNCATED && !memcmp(&r, &before, sizeof r));
    CHECK(es2_shell_stats_read((es2_span){x.b, n - 1}, &r, &e) == ES2_ERR_TRUNCATED);
    CHECK(es2_shell_stats_read((es2_span){x.b, n}, NULL, &e) == ES2_ERR_ARGUMENT);
    n = sav(&x, -1, 0);
    x.b[1] = 0xff; x.b[2] = 0xff;   /* first block count -1: a seek before the start */
    CHECK(es2_shell_stats_read((es2_span){x.b, n}, &r, &e) == ES2_ERR_TRUNCATED);
    /* refresh: name with strcpy, the copied words; the others stay */
    memset(&r, 0x77, sizeof r);
    memset(&memory, 0x11, sizeof memory);
    memcpy(memory.raw + 4, "AB", 3);
    CHECK(es2_shell_stats_refresh(&r, &memory, &e) == ES2_OK);
    CHECK(!memcmp(r.raw + 4, "AB", 3) && r.raw[7] == 0x77);
    CHECK(r.raw[0] == 0x77 && r.raw[2] == 0x77 && r.raw[0x22] == 0x77 && r.raw[0x24] == 0x77 && r.raw[0x27] == 0x77 &&
          r.raw[0x2b] == 0x77 && r.raw[0x39] == 0x77);
    CHECK(r.raw[0x25] == 0x11 && r.raw[0x29] == 0x11 && r.raw[0x2d] == 0x11 && r.raw[0x38] == 0x11 &&
          r.raw[0x3b] == 0x11 && r.raw[0x3e] == 0x11 && r.raw[0x3f] == 0x11 && r.raw[0x42] == 0x11);
    memset(memory.raw + 4, 'X', 0x1e);
    before = r;
    CHECK(es2_shell_stats_refresh(&r, &memory, &e) == ES2_ERR_STRING && !memcmp(&r, &before, sizeof r));
    CHECK(es2_shell_stats_refresh(NULL, &memory, &e) == ES2_ERR_ARGUMENT);
    /* the panel: a 30-byte name without NUL shows 30 bytes; negative salvage truncates */
    memset(&r, 0, sizeof r);
    memset(r.raw + 4, 'N', 0x1e);
    r.raw[0x3b] = 0x18; r.raw[0x3c] = 0xfc; r.raw[0x3d] = 0xff; r.raw[0x3e] = 0xff;   /* -1000 */
    r.raw[0x3b] = 0x17;                                                                /* -1001 */
    es2_shell_stats_show(&r, &v);
    CHECK(strlen(v.name) == 30 && v.salvage == -1 && v.mission == 1 && v.sector_text == 0x76);
}

int main(void) {
    load();
    write_and_mark();
    companions();
    statistics();
    printf("slots synthetic: %u checks, %u failures\n", checks, failures);
    return failures != 0;
}
