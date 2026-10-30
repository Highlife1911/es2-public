/* Test driver of es2/shell/movies.h for movies_real_test. Reads
 * operations from stdin, one per line, in the memory worker's spelling:
 *   movie-init | movie-flag:V | movie-once:A:B:C | movie-enqueue:I:X:Y:W:H:P:C:F |
 *   movie-play:A:BAR:PANEL:FULL:PAGE | movie-dump | load | new |
 *   WORD <SAV hex> | NAMES
 * and prints DUMP <hex> (the 300 bytes the worker's movie-dump writes before
 * the two selection words), WORD <briefing movie word of the SAV>, NAME <i>
 * <name>, and at the end CALLS with the player actions the worker records
 * (address:arg). The player runs as the worker's does: the avivideo device
 * never opens, focus is held, the theatre step and the panels are no-ops. */
#include "es2/shell/movies.h"
#include "es2/shell/texts.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static es2_shell_movies m;
static unsigned bar, panel, full, page;
static char calls[1 << 16];
static size_t calls_at;

/* The calls the worker records (not its quiet traps). */
static int recorded(uint32_t a) {
    switch (a) {
    case ES2_SHELL_MOVIE_FOCUS_LOST: case ES2_SHELL_MOVIE_PALETTE: case ES2_SHELL_MOVIE_PALETTE_APPLY:
    case ES2_SHELL_MOVIE_TEXT_PANEL: case ES2_SHELL_MOVIE_OPEN_DEVICE: case ES2_SHELL_MOVIE_PUMP:
    case ES2_SHELL_MOVIE_BAY_SELECT: case ES2_SHELL_MOVIE_TEARDOWN: case ES2_SHELL_MOVIE_FOCUS_GAINED:
    case ES2_SHELL_MOVIE_THEATRE_STEP: case ES2_SHELL_MOVIE_WAIT: case ES2_SHELL_MOVIE_THEATRE_HIDE:
        return 1;
    default:
        return 0;
    }
}
static int32_t call(void *c, uint32_t action, int32_t arg, const es2_shell_movie_record *r) {
    (void)c; (void)r;
    if (recorded(action) && calls_at + 32 < sizeof calls)
        calls_at += (size_t)snprintf(calls + calls_at, sizeof calls - calls_at, " %x:%d", (unsigned)action, (int)arg);
    return 0;   /* OPEN_DEVICE: the device does not open */
}
static int32_t query(void *c, int what) {
    (void)c;
    switch (what) {
    case ES2_SHELL_MOVIE_Q_BAY_BAR: return (int32_t)bar;
    case ES2_SHELL_MOVIE_Q_PANEL: return (int32_t)panel;
    case ES2_SHELL_MOVIE_Q_FULLSCREEN: return (int32_t)full;
    case ES2_SHELL_MOVIE_Q_PAGE: return (int16_t)page;
    case ES2_SHELL_MOVIE_Q_FOCUS: return 1;
    default: return 0;
    }
}
static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static uint8_t *unhex(const char *s, size_t *n) {
    size_t len = strlen(s) / 2;
    uint8_t *out = malloc(len ? len : 1);
    for (size_t i = 0; out && i < len; i++) {
        unsigned v;
        if (sscanf(s + 2 * i, "%2x", &v) != 1) { free(out); return NULL; }
        out[i] = (uint8_t)v;
    }
    *n = len;
    return out;
}

int main(void) {
    static char line[1 << 17];
    unsigned v[8];
    es2_shell_movie_hooks hooks = {NULL, call, query};
    es2_shell_movies_init(&m, 1);
    while (fgets(line, sizeof line, stdin)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!strcmp(line, "movie-init")) {
            es2_shell_movies_init(&m, 1);
        } else if (sscanf(line, "movie-flag:%u", &v[0]) == 1) {
            m.enabled = (uint8_t)v[0];
        } else if (sscanf(line, "movie-once:%u:%u:%u", &v[0], &v[1], &v[2]) == 3) {
            m.group_shown = (uint8_t)v[0];
            m.briefing_shown = (uint8_t)v[1];
            m.debrief_shown = (uint8_t)v[2];
        } else if (sscanf(line, "movie-enqueue:%u:%u:%u:%u:%u:%u:%u:%u", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5],
                          &v[6], &v[7]) == 8) {
            es2_shell_movie_rect r = {(int32_t)v[1], (int32_t)v[2], (int32_t)v[3], (int32_t)v[4]};
            es2_shell_movie_enqueue(&m, (int32_t)v[0], r, (int32_t)v[5], v[6], v[7]);
        } else if (sscanf(line, "movie-play:%u:%u:%u:%u:%u", &v[0], &bar, &panel, &full, &page) == 5) {
            es2_shell_movies_play(&m, (int)v[0], &hooks);
        } else if (!strcmp(line, "movie-dump")) {
            uint8_t d[300];
            es2_shell_movies_image(&m, d);
            d[280] = (uint8_t)m.write; d[281] = (uint8_t)((uint16_t)m.write >> 8);
            d[282] = (uint8_t)m.play; d[283] = (uint8_t)((uint16_t)m.play >> 8);
            put32(d + 284, m.active);
            put32(d + 288, m.intro_skipped);
            put32(d + 292, m.intro);
            d[296] = m.enabled;
            d[297] = m.group_shown;
            d[298] = m.briefing_shown;
            d[299] = m.debrief_shown;
            printf("DUMP ");
            for (int i = 0; i < 300; i++) printf("%02x", d[i]);
            printf("\n");
        } else if (!strcmp(line, "load") || !strcmp(line, "new")) {
            /* 0x40eaaa and 0x40e8a5 both clear 0x477947/0x477948 */
            es2_shell_movies_load(&m, 0);
        } else if (!strncmp(line, "WORD ", 5)) {
            size_t n;
            uint8_t *sav = unhex(line + 5, &n), selection[ES2_SHELL_BRIEFING_SIZE];
            es2_error e;
            int16_t group, position;
            if (sav && es2_shell_briefing_selection((es2_span){sav, n}, selection, &group, &position, &e) == ES2_OK)
                printf("WORD %d\n", (int16_t)(uint16_t)(selection[146] | selection[147] << 8));
            else
                printf("WORD error\n");
            free(sav);
        } else if (!strcmp(line, "NAMES")) {
            for (int i = 0; i < ES2_SHELL_MOVIE_NAMES; i++) printf("NAME %d %s\n", i, es2_shell_movie_name(i));
        } else if (line[0]) {
            printf("ERROR %s\n", line);
            return 1;
        }
    }
    printf("CALLS%s\n", calls);
    return 0;
}
