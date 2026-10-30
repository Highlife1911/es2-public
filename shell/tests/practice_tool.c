/* Test driver of //shell:practice_real_test: our practice screen,
 * INSTANT ACTION, VIEW DEMO and DEBUG dialog (es2/shell/practice.h) over
 * stdin, for the comparison with the mapped VSHELL's handlers.
 *
 *   practice_tool CD_ROOT LANGUAGE(ENG|FRE|GER)
 *
 * Input lines: the save tables (hex, 164 default + 60 item-index bytes), the
 * nine preset paths (hex, 9 x 64), the practice tables (hex,
 * ES2_SHELL_PRACTICE_TABLES_SIZE), the start RNG (hex, 114), data\prefs.cfg
 * (hex, or "-" when missing), then one operation per line with the names of
 * the worker's slot-mode operations: prefs-load, practice-show,
 * practice-entry:I, practice-control:K:B, practice-state, practice-menu,
 * practice-begin, instant-action, use-default, debug-accept:TEXT, view-demo,
 * practice-reset, practice-emit. The DATA files are kept as VSHELL leaves
 * them: script.dat and MISSION.STR are rewritten without truncation,
 * mission.var, player.mec and prefs.cfg replaced.
 *
 * Output: "STATE index flag herc_enabled" for practice-state, "EMIT hex"
 * for practice-emit (the worker's practice.emit record layout: five files
 * as u32 size or 0xffffffff and bytes, the SAV, eight dwords), "REFUSED op
 * status" when ours refuses an operation (nothing changes). The mode word
 * starts at 1, as the worker's --mode default.
 *
 * Reads the CD's ZONES/SHELL0/LANG0.VOL; no other file I/O. */
#include "es2/shell/practice.h"
#include "es2/formats/sav.h"
#include "es2/formats/vol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char line[1 << 17];
enum { SCRIPT, STRINGS, VARIABLES, PLAYER, PREFS, FILES };
typedef struct file { int present; uint8_t *data; size_t size; } file;
static file files[FILES];

static size_t unhex(const char *text, uint8_t *out, size_t capacity) {
    size_t n = 0;
    unsigned v;
    while (text[0] && text[1] && text[0] != '\n' && text[0] != '\r' && n < capacity && sscanf(text, "%2x", &v) == 1) {
        out[n++] = (uint8_t)v;
        text += 2;
    }
    return n;
}
static int read_file(const char *path, es2_span *out) {
    FILE *f = fopen(path, "rb");
    long n;
    uint8_t *b;
    if (!f) return 0;
    if (fseek(f, 0, SEEK_END) || (n = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) || !(b = malloc((size_t)n + 1)) ||
        fread(b, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        return 0;
    }
    fclose(f);
    *out = (es2_span){b, (size_t)n};
    return 1;
}
static int next_line(void) {
    if (!fgets(line, sizeof line, stdin)) return 0;
    line[strcspn(line, "\r\n")] = 0;
    return 1;
}
/* Replaces (truncate) or overlays (no truncation) a DATA file. */
static void store(int index, const uint8_t *data, size_t size, int overlay) {
    file *f = &files[index];
    size_t total = overlay && f->present && f->size > size ? f->size : size;
    uint8_t *b = malloc(total ? total : 1);
    if (!b) exit(3);
    if (total > size) memcpy(b + size, f->data + size, total - size);
    memcpy(b, data, size);
    free(f->data);
    f->data = b;
    f->size = total;
    f->present = 1;
}

static uint8_t record[1 << 16];
static size_t record_size;
static void put(const void *p, size_t n) {
    if (record_size + n > sizeof record) exit(3);
    memcpy(record + record_size, p, n);
    record_size += n;
}
static void put32(uint32_t v) {
    uint8_t b[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
    put(b, 4);
}

int main(int argc, char **argv) {
    const char *names[] = {"ZONES.VOL", "SHELL0.VOL", "LANG0.VOL"};
    es2_span archives[3] = {{0}};
    es2_vol vols[3];
    es2_vfs_source sources[3];
    es2_vfs vfs;
    es2_error e;
    char path[1024];
    static uint8_t defaults[164], items[60], paths[576], tables[ES2_SHELL_PRACTICE_TABLES_SIZE], rng_bytes[114];
    static uint8_t buffer[1 << 16];
    es2_shell_save_tables save_tables = {{defaults, 164}, {items, 60}};
    es2_shell_practice practice;
    es2_shell_prefs_session session;
    es2_shell_rng rng;
    int16_t selectors[30] = {0};
    es2_shell_practice_launch launch;
    es2_shell_buffer sav = {0}, pending_game = {0};
    int pending = 0, mode = 1, exit_code = 0, quit = 0, loaded = 0;
    if (argc != 3) return 2;
    for (int i = 0; i < 3; i++) {
        if (snprintf(path, sizeof path, "%s/VOL/%s", argv[1], names[i]) >= (int)sizeof path ||
            !read_file(path, &archives[i]) || es2_vol_open(archives[i], &vols[i], &e) != ES2_OK) {
            fprintf(stderr, "archive %s\n", names[i]);
            return 2;
        }
        sources[i].kind = ES2_VFS_VOL;
        sources[i].data.vol = &vols[i];
    }
    if (es2_vfs_open(sources, 3, &vfs, &e) != ES2_OK) return 2;
    if (!next_line() || unhex(line, buffer, sizeof buffer) != 224) return 2;
    memcpy(defaults, buffer, 164);
    memcpy(items, buffer + 164, 60);
    if (!next_line() || unhex(line, paths, sizeof paths) != sizeof paths) return 2;
    if (!next_line() || unhex(line, tables, sizeof tables) != sizeof tables) return 2;
    if (!next_line() || unhex(line, rng_bytes, sizeof rng_bytes) != sizeof rng_bytes) return 2;
    for (int i = 0; i < 56; i++) rng.words[i] = (uint16_t)(rng_bytes[2 * i] | rng_bytes[2 * i + 1] << 8);
    rng.a = rng_bytes[112];
    rng.b = rng_bytes[113];
    if (!next_line()) return 2;
    if (strcmp(line, "-")) {
        size_t n = unhex(line, buffer, sizeof buffer);
        store(PREFS, buffer, n, 0);
    }
    es2_shell_practice_open(&practice);
    es2_shell_prefs_session_open(NULL, &session);
    while (next_line()) {
        unsigned a, b;
        int changed, applied;
        uint8_t out[ES2_SHELL_PREFS_SIZE], mode_out[ES2_SHELL_PREFS_SIZE];
        es2_status status = ES2_OK;
        es2_span prefs = {files[PREFS].data, files[PREFS].size};
        if (!strcmp(line, "prefs-load")) {
            es2_shell_prefs_session_open(files[PREFS].present ? &prefs : NULL, &session);
        } else if (!strcmp(line, "practice-show")) {
            status = es2_shell_practice_show(&practice, &session, tables, files[PREFS].present ? &prefs : NULL, out, &e);
            if (status == ES2_OK) { store(PREFS, out, sizeof out, 0); mode = 0; }
        } else if (sscanf(line, "practice-entry:%u", &a) == 1) {
            status = es2_shell_practice_entry(&practice, &session, tables, (int)a, &changed, &e);
        } else if (sscanf(line, "practice-control:%u:%u", &a, &b) == 2) {
            status = es2_shell_practice_control(&practice, &session, (int)a, (int)b, &applied, &e);
        } else if (!strcmp(line, "practice-state")) {
            printf("STATE %d %u %d\n", practice.index, (unsigned)practice.preset_flag,
                   es2_shell_practice_herc_enabled(&practice));
        } else if (!strcmp(line, "practice-menu")) {
            /* 0x44c96a: hide, main menu; no state */
        } else if (!strcmp(line, "practice-begin") || !strcmp(line, "instant-action")) {
            int instant = line[0] == 'i';
            es2_shell_practice p = practice;
            es2_shell_prefs_session s = session;
            es2_shell_rng r = rng;
            es2_shell_buffer game = {0};
            status = instant ? es2_shell_instant_action(&p, &s, tables, files[PREFS].present ? &prefs : NULL, mode_out,
                                                        out, &launch, &e)
                             : es2_shell_practice_begin(&p, &s, tables, out, &launch, &e);
            es2_span carry = {sav.data, sav.size};   /* the game the process holds, if any */
            if (status == ES2_OK)
                status = es2_shell_practice_new_game(&vfs, argv[2], &save_tables, &launch, &carry, &r, &game, &e);
            if (status == ES2_OK) {
                practice = p;
                session = s;
                rng = r;
                store(PREFS, out, sizeof out, 0);
                if (instant) mode = 0;
                free(pending_game.data);
                pending_game = game;
                pending = 1;
                free(sav.data);
                sav.data = malloc(game.size);
                if (!sav.data) return 3;
                memcpy(sav.data, game.data, game.size);
                sav.size = game.size;
                loaded = 1;
                if (instant) {
                    /* 0x4319aa: 0x40f68c on the new game's state, exit code 2, quit flag */
                    es2_shell_buffer mec = {0}, var = {0};
                    if (es2_shell_practice_new_game_files((es2_span){sav.data, sav.size}, &mec, &var, &e) == ES2_OK) {
                        store(VARIABLES, var.data, var.size, 0);
                        store(PLAYER, mec.data, mec.size, 0);
                    } else
                        printf("NOTE launch files of the new game refused: %s\n", es2_status_string(e.code));
                    free(mec.data);
                    free(var.data);
                    exit_code = 2;
                    quit = 1;
                }
            } else
                free(game.data);
        } else if (!strcmp(line, "use-default") || !strncmp(line, "debug-accept:", 13)) {
            char stem[32];
            const char *mission = NULL;
            es2_shell_output f = {0};
            es2_shell_buffer s = {0};
            es2_shell_rng r = rng;
            int16_t sel[30];
            memcpy(sel, selectors, sizeof sel);
            if (!pending) status = es2_fail(&e, ES2_ERR_ARGUMENT, 0);
            if (status == ES2_OK && line[0] == 'd') {
                status = es2_shell_debug_mission(line + 13, stem, sizeof stem, &e);
                mission = stem;
            }
            if (status == ES2_OK)
                status = es2_shell_practice_prepare(&vfs, argv[2], &save_tables, (es2_span){paths, sizeof paths}, &launch,
                                                    mission, (es2_span){pending_game.data, pending_game.size}, &r, sel,
                                                    &f, &s, &e);
            if (status == ES2_OK) {
                rng = r;
                memcpy(selectors, sel, sizeof sel);
                store(SCRIPT, f.script.data, f.script.size, 1);
                store(STRINGS, f.strings.data, f.strings.size, 1);
                store(VARIABLES, f.variables.data, f.variables.size, 0);
                store(PLAYER, f.player.data, f.player.size, 0);
                free(sav.data);
                sav = s;
                exit_code = 2;
                quit = 1;
                es2_shell_output_free(&f);
            }
        } else if (!strncmp(line, "restore-hex:", 12)) {
            /* the worker's restore:N: the game this process held before */
            size_t n = unhex(line + 12, buffer, sizeof buffer);
            free(sav.data);
            sav.data = malloc(n ? n : 1);
            if (!sav.data) return 3;
            memcpy(sav.data, buffer, n);
            sav.size = n;
            loaded = 1;
        } else if (!strcmp(line, "prefs:accept")) {
            uint8_t written[ES2_SHELL_PREFS_SIZE];
            es2_shell_prefs_accept(&session, written);
            store(PREFS, written, sizeof written, 0);
        } else if (!strcmp(line, "prefs:cancel")) {
            es2_shell_prefs_cancel(&session);
        } else if (!strcmp(line, "view-demo")) {
            exit_code = ES2_SHELL_EXIT_DEMO;
            quit = 1;
        } else if (!strcmp(line, "practice-reset")) {
            practice.index = -1;
            practice.preset_flag = 0;
        } else if (!strcmp(line, "practice-emit")) {
            es2_sav parsed;
            int16_t group = -1, position = -1;
            record_size = 0;
            for (int i = 0; i < FILES; i++) {
                put32(files[i].present ? (uint32_t)files[i].size : 0xffffffffu);
                if (files[i].present) put(files[i].data, files[i].size);
            }
            put32((uint32_t)sav.size);
            if (sav.size) put(sav.data, sav.size);
            if (sav.size && es2_sav_parse((es2_span){sav.data, sav.size}, &parsed, &e) == ES2_OK) {
                group = (int16_t)(parsed.selection.data[0] | parsed.selection.data[1] << 8);
                position = (int16_t)(parsed.selection.data[2] | parsed.selection.data[3] << 8);
            }
            put32((uint32_t)(int32_t)practice.index);
            put32(practice.preset_flag);
            put32((uint32_t)exit_code);
            put32((uint32_t)quit);
            put32((uint32_t)(int32_t)mode);
            put32((uint32_t)(int32_t)group);
            put32((uint32_t)(int32_t)position);
            put32((uint32_t)loaded);
            printf("EMIT ");
            for (size_t i = 0; i < record_size; i++) printf("%02x", record[i]);
            printf("\n");
        } else {
            fprintf(stderr, "unknown operation %s\n", line);
            return 2;
        }
        if (status != ES2_OK) printf("REFUSED %s %s %zu\n", line, es2_status_string(status), e.offset);
        fflush(stdout);
    }
    return 0;
}
