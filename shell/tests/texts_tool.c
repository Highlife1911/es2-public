/* Test driver of the mission-screen texts (es2/shell/texts.h) for
 * texts_real_test. Standard input, one command per line; every answer ends
 * with "END":
 *   ROOT <cd-root>                 mount the VOL archives of <cd-root> in the memory
 *                                  worker's order (priority, then find order)
 *   CAMPAIGN <lang> <group>        page-0 text from <lang>\campaign.str
 *   TITLE <lang> <group>           page-0 title from <lang>\estext.bin
 *   SELECT <sav hex>               "S <selection hex>" (148 bytes)
 *   BRIEF <mission.str hex> <selection hex>   three texts
 *   STATS <lang> <sav hex> <O> <T> <L> <S>     ten 0x40f904 texts (words as
 *                                  unsigned; ESTEXT from <lang>\estext.bin)
 * Texts come back as "T <hex>", refusals as "ERR <status>". */
#include "es2/shell/texts.h"
#include "es2/formats/vfs.h"
#include "es2/formats/vol.h"
#include "menu.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *line(void) {
    static char *buffer;
    static size_t capacity;
    size_t n = 0;
    int c;
    while ((c = getchar()) != EOF && c != '\n') {
        if (n + 2 > capacity) {
            capacity = capacity ? 2 * capacity : 1 << 16;
            buffer = realloc(buffer, capacity);
            if (!buffer) exit(2);
        }
        if (c != '\r') buffer[n++] = (char)c;
    }
    if (c == EOF && !n) return NULL;
    if (!buffer) { buffer = malloc(1); capacity = 1; }
    buffer[n] = 0;
    return buffer;
}
static uint8_t *unhex(const char *s, size_t length, size_t *n) {
    uint8_t *out = malloc(length / 2 + 1);
    if (!out) exit(2);
    for (size_t i = 0; i < length / 2; i++) {
        unsigned v;
        if (sscanf(s + 2 * i, "%2x", &v) != 1) exit(2);
        out[i] = (uint8_t)v;
    }
    *n = length / 2;
    return out;
}
static void hex_line(const char *tag, const uint8_t *p, size_t n) {
    printf("%s ", tag);
    for (size_t i = 0; i < n; i++) printf("%02x", p[i]);
    putchar('\n');
}
static uint8_t *read_file(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    uint8_t *data = NULL;
    long n;
    if (!f) return NULL;
    if (!fseek(f, 0, SEEK_END) && (n = ftell(f)) >= 0 && !fseek(f, 0, SEEK_SET) && (data = malloc((size_t)n + 1))) {
        if (fread(data, 1, (size_t)n, f) != (size_t)n) { free(data); data = NULL; }
        else *size = (size_t)n;
    }
    fclose(f);
    return data;
}

static es2_vol vols[32];
static es2_vfs_source sources[32];
static unsigned vol_count;
static es2_vfs vfs;
static int mounted;
static int mount(const char *root) {
    char pattern[MAX_PATH], path[MAX_PATH];
    WIN32_FIND_DATAA f;
    HANDLE h;
    snprintf(pattern, sizeof pattern, "%s/VOL/*.VOL", root);
    h = FindFirstFileA(pattern, &f);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        size_t size = 0;
        uint8_t *data;
        es2_error e;
        if (vol_count == 32) return 0;
        snprintf(path, sizeof path, "%s/VOL/%s", root, f.cFileName);
        if (!(data = read_file(path, &size)) || es2_vol_open((es2_span){data, size}, &vols[vol_count], &e) != ES2_OK)
            return 0;
        sources[vol_count].kind = ES2_VFS_VOL;
        sources[vol_count].data.vol = &vols[vol_count];
        vol_count++;
    } while (FindNextFileA(h, &f));
    FindClose(h);
    /* the memory worker's order (tools/reference-game/shell-invitro/main.c) */
    for (unsigned i = 0; i < vol_count; i++)
        for (unsigned j = i + 1; j < vol_count; j++)
            if (sources[j].data.vol->priority > sources[i].data.vol->priority) {
                es2_vfs_source t = sources[i];
                sources[i] = sources[j];
                sources[j] = t;
            }
    return es2_vfs_open(sources, vol_count, &vfs, NULL) == ES2_OK;
}
static int find(const char *lang, const char *file, es2_span *out) {
    char path[64];
    es2_vfs_copy c;
    snprintf(path, sizeof path, "%s\\%s", lang, file);
    for (char *p = path; *p; p++) if (*p >= 'A' && *p <= 'Z') *p = (char)(*p + 32);
    if (!mounted || es2_vfs_find(&vfs, path, &c, NULL) != ES2_OK) return 0;
    *out = c.payload;
    return 1;
}
static void fail(es2_status s) { printf("ERR %s\n", es2_status_string(s)); }

int main(void) {
    char *l;
    while ((l = line()) != NULL) {
        char lang[8];
        int group;
        es2_error e;
        es2_status s;
        if (!strncmp(l, "ROOT ", 5)) {
            mounted = mount(l + 5);
            if (!mounted) printf("ERR mount\n");
        } else if (sscanf(l, "CAMPAIGN %3s %d", lang, &group) == 2) {
            es2_span file, text;
            if (!find(lang, "campaign.str", &file)) printf("ERR file\n");
            else if ((s = es2_shell_campaign_text(file, (int16_t)group, &text, &e)) != ES2_OK) fail(s);
            else hex_line("T", text.data, text.size);
        } else if (sscanf(l, "TITLE %3s %d", lang, &group) == 2) {
            es2_span cat, name, word;
            char title[128];
            if (!find(lang, "estext.bin", &cat)) printf("ERR file\n");
            else if (es2m_bin_raw(cat, (size_t)es2_shell_sector_name_id((int16_t)group), &name, &e) != ES2_OK ||
                     es2m_bin_raw(cat, ES2_SHELL_SECTOR_WORD, &word, &e) != ES2_OK)
                printf("ERR estext\n");
            else if ((s = es2_shell_sector_title(name, word, title, sizeof title, &e)) != ES2_OK) fail(s);
            else hex_line("T", (const uint8_t *)title, strlen(title));
        } else if (!strncmp(l, "SELECT ", 7)) {
            size_t n;
            uint8_t *sav = unhex(l + 7, strlen(l + 7), &n), sel[ES2_SHELL_BRIEFING_SIZE];
            if ((s = es2_shell_briefing_selection((es2_span){sav, n}, sel, NULL, NULL, &e)) != ES2_OK) fail(s);
            else hex_line("S", sel, sizeof sel);
            free(sav);
        } else if (!strncmp(l, "BRIEF ", 6)) {
            const char *a = l + 6, *b = strchr(a, ' ');
            size_t n, m;
            uint8_t *str, *sel;
            es2_shell_briefing_texts t;
            if (!b) return 2;
            str = unhex(a, (size_t)(b - a), &n);
            sel = unhex(b + 1, strlen(b + 1), &m);
            memset(&t, 0, sizeof t);
            if (m != ES2_SHELL_BRIEFING_SIZE) return 2;
            if ((s = es2_shell_briefing_texts_compose((es2_span){str, n}, sel, &t, &e)) != ES2_OK) fail(s);
            else for (int i = 0; i < ES2_SHELL_TEXT_COUNT; i++) hex_line("T", t.text[i].data, t.text[i].size);
            es2_shell_briefing_texts_free(&t);
            free(str);
            free(sel);
        } else if (!strncmp(l, "STATS ", 6)) {
            char *hex = malloc(strlen(l));
            unsigned o, t, lost, rec;
            size_t n;
            uint8_t *sav;
            es2_span cat, outcome, unit;
            es2_shell_debrief_words w;
            es2_shell_debrief_texts texts;
            if (!hex || sscanf(l, "STATS %3s %s %u %u %u %u", lang, hex, &o, &t, &lost, &rec) != 6) return 2;
            sav = unhex(hex, strlen(hex), &n);
            if (!find(lang, "estext.bin", &cat)) printf("ERR file\n");
            else if ((s = es2_shell_debrief_words_from_save((es2_span){sav, n}, (int16_t)o, t, (int16_t)lost,
                                                                (int16_t)rec, &w, &e)) != ES2_OK) fail(s);
            else if (es2m_bin_raw(cat, (size_t)(uint16_t)es2_shell_debrief_outcome_id(w.outcome), &outcome, &e) != ES2_OK ||
                     es2m_bin_raw(cat, ES2_SHELL_DEBRIEF_UNIT, &unit, &e) != ES2_OK)
                printf("ERR estext\n");
            else if ((s = es2_shell_debrief_texts_compose(&w, outcome, unit, &texts, &e)) != ES2_OK) fail(s);
            else for (int i = 0; i < ES2_SHELL_DEBRIEF_VALUES; i++)
                hex_line("T", (const uint8_t *)texts.text[i], strlen(texts.text[i]));
            free(sav);
            free(hex);
        } else if (!strncmp(l, "AFTER ", 6)) {
            /* AFTER <results.dat hex> <SAV hex>: "W outcome tally recovered lost you0..2 squad0..2" */
            const char *a = l + 6, *b = strchr(a, ' ');
            size_t n, m;
            uint8_t *res, *sav;
            es2_shell_debrief_words w;
            if (!b) return 2;
            res = unhex(a, (size_t)(b - a), &n);
            sav = unhex(b + 1, strlen(b + 1), &m);
            if ((s = es2_shell_debrief_words_after((es2_span){res, n}, (es2_span){sav, m}, &w, &e)) != ES2_OK) fail(s);
            else printf("W %d %lu %d %d %d %d %d %d %d %d\n", w.outcome, (unsigned long)w.tally, w.recovered, w.lost,
                        w.you[0], w.you[1], w.you[2], w.squad[0], w.squad[1], w.squad[2]);
            free(res);
            free(sav);
        } else return 2;
        puts("END");
        fflush(stdout);
    }
    return 0;
}
