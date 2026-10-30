/* Test driver of //shell:editors_real_test: our service-bay editors
 * (es2/shell/editors.h) over stdin, for the comparison with the mapped VSHELL.
 *
 *   editors_tool CD_ROOT
 *
 * Input: line 1 the results tables (hex, ES2_SHELL_RESULTS_TABLES_SIZE
 * bytes), line 2 the editor tables (hex, ES2_SHELL_EDITOR_TABLES_SIZE); then
 * cases, each "CASE bay", "SAVE hex" (a memory-state SAV), any number of
 * "OP kind a b" and "END". Every operation is applied on its own to the state
 * and session the kept operations left; a refused one is dropped. Output per
 * case: "VIEW v..." for the start, then per operation "STEP i 1|0" (applied)
 * followed by "SESSION ..." and "VIEW ...", or "DROP i status"; at the end
 * "BATCH same|differs" (the kept operations in one call against the steps),
 * "SAV hex" and "END". On a start state ours rejects: "REJECT status", "END".
 *
 * SESSION screen bay tab row family index herc_type salvage conditions(hex)
 * VIEW salvage bank first one record condition item all value bank9 build
 *      repair repair_all scrap build_enabled
 * CREW and ARMORY add to SESSION: order crew0 crew1 crew2 armory_row building;
 * to VIEW: armory_rows clear category items in_bank free scrap_value. "CASE bay
 * building" also sets the session's weapons-building byte.
 *
 * Reads the CD's ZONES/SHELL0/LANG0.VOL; no other file I/O. */
#include "es2/shell/editors.h"
#include "es2/formats/vol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char line[1 << 17];

static size_t unhex(const char *text, uint8_t *out, size_t capacity) {
    size_t n = 0;
    unsigned v;
    while (text[0] && text[1] && text[0] != '\n' && text[0] != '\r' && n < capacity && sscanf(text, "%2x", &v) == 1) {
        out[n++] = (uint8_t)v;
        text += 2;
    }
    return n;
}
static void hex(const uint8_t *p, size_t n) {
    for (size_t i = 0; i < n; i++) printf("%02x", p[i]);
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
static void print_session(const es2_shell_editor_session *s) {
    uint8_t c[66];
    for (int i = 0; i < 33; i++) {
        c[2 * i] = (uint8_t)s->conditions[i];
        c[2 * i + 1] = (uint8_t)((uint16_t)s->conditions[i] >> 8);
    }
    printf("SESSION %d %d %d %d %d %d %d %lu ", s->screen, s->bay, s->tab, s->row, s->family, s->index, s->herc_type,
           (unsigned long)s->salvage);
    hex(c, 66);
    printf(" %d %d %d %d %d %d\n", s->order, s->crew[0], s->crew[1], s->crew[2], s->armory_row, s->building);
}
static void print_view(const es2_shell_editor_view *v) {
    printf("VIEW %lu %lu %d %d %d %d %ld %ld %ld %d %ld %d %d %d %d %lu %d %d %d %d %d %ld\n", (unsigned long)v->salvage,
           (unsigned long)v->bank_cost, v->first_finished, v->one_functional, v->has_record, v->condition,
           (long)v->item_cost, (long)v->all_cost, (long)v->scrap_value, v->scrap_bank, (long)v->build_cost,
           v->repair_enabled, v->repair_all_enabled, v->scrap_enabled, v->build_enabled,
           (unsigned long)v->armory_rows, v->armory_clear_enabled, v->armory_category, v->armory_items,
           v->armory_in_bank, v->armory_free, (long)v->armory_scrap_value);
}

static int session_equal(const es2_shell_editor_session *a, const es2_shell_editor_session *b) {
    return a->screen == b->screen && a->bay == b->bay && a->tab == b->tab && a->row == b->row &&
           a->family == b->family && a->index == b->index && a->salvage == b->salvage &&
           a->herc_type == b->herc_type && !memcmp(a->conditions, b->conditions, sizeof a->conditions) &&
           a->order == b->order && !memcmp(a->crew, b->crew, sizeof a->crew) && a->armory_row == b->armory_row &&
           a->building == b->building;
}

#define MAX_OPS 256
int main(int argc, char **argv) {
    const char *names[] = {"ZONES.VOL", "SHELL0.VOL", "LANG0.VOL"};
    static uint8_t tables[ES2_SHELL_RESULTS_TABLES_SIZE], editor[ES2_SHELL_EDITOR_TABLES_SIZE], start[1 << 16];
    es2_span archives[3];
    es2_vol vols[3];
    es2_vfs_source sources[3];
    es2_vfs vfs;
    es2_error e;
    char path[4096];
    if (argc != 2) {
        fprintf(stderr, "usage: editors_tool CD_ROOT\n");
        return 2;
    }
    for (int i = 0; i < 3; i++) {
        if (snprintf(path, sizeof path, "%s/VOL/%s", argv[1], names[i]) >= (int)sizeof path ||
            !read_file(path, &archives[i]) || es2_vol_open(archives[i], &vols[i], &e) != ES2_OK) {
            fprintf(stderr, "archive read failed: %s\n", names[i]);
            return 2;
        }
        sources[i].kind = ES2_VFS_VOL;
        sources[i].data.vol = &vols[i];
    }
    if (es2_vfs_open(sources, 3, &vfs, &e) != ES2_OK) return 2;
    if (!fgets(line, sizeof line, stdin) || unhex(line, tables, sizeof tables) != sizeof tables ||
        !fgets(line, sizeof line, stdin) || unhex(line, editor, sizeof editor) != sizeof editor) {
        fprintf(stderr, "tables expected\n");
        return 2;
    }
    es2_span t = {tables, sizeof tables}, x = {editor, sizeof editor};
    while (fgets(line, sizeof line, stdin)) {
        static es2_shell_editor_op ops[MAX_OPS], kept[MAX_OPS];
        static int flags[MAX_OPS];
        size_t count = 0, nkept = 0, size = 0;
        int bay, building = 0;
        if (sscanf(line, "CASE %d %d", &bay, &building) < 1) {
            fprintf(stderr, "CASE expected\n");
            return 2;
        }
        if (!fgets(line, sizeof line, stdin) || strncmp(line, "SAVE ", 5)) {
            fprintf(stderr, "SAVE expected\n");
            return 2;
        }
        size = unhex(line + 5, start, sizeof start);
        while (fgets(line, sizeof line, stdin) && strncmp(line, "END", 3)) {
            int k, a, b;
            if (count == MAX_OPS || sscanf(line, "OP %d %d %d", &k, &a, &b) != 3) {
                fprintf(stderr, "OP expected\n");
                return 2;
            }
            ops[count++] = (es2_shell_editor_op){k, (int16_t)a, (int16_t)b, 0};
        }
        es2_shell_editor_session session, first;
        es2_shell_editor_view view;
        es2_shell_buffer state = {0}, next = {0};
        es2_status status;
        es2_shell_editor_session_init(&session, (int16_t)bay);
        session.building = (int16_t)building;
        first = session;
        status = es2_shell_editors(&vfs, t, x, (es2_span){start, size}, &session, NULL, 0, &state, &view, &e);
        if (status != ES2_OK) {
            printf("REJECT %s\nEND\n", es2_status_string(status));
            free(state.data);
            continue;
        }
        print_view(&view);
        for (size_t i = 0; i < count; i++) {
            es2_shell_editor_session s = session;
            es2_shell_editor_op op = ops[i];
            status = es2_shell_editors(&vfs, t, x, (es2_span){state.data, state.size}, &s, &op, 1, &next, &view, &e);
            if (status != ES2_OK) {
                printf("DROP %zu %s\n", i, es2_status_string(status));
                continue;
            }
            free(state.data);
            state = next;
            next = (es2_shell_buffer){0};
            session = s;
            flags[nkept] = op.applied;
            kept[nkept++] = op;
            printf("STEP %zu %d\n", i, op.applied);
            print_session(&session);
            print_view(&view);
        }
        es2_shell_editor_session s = first;
        es2_shell_buffer batch = {0};
        int same = 0;
        for (size_t i = 0; i < nkept; i++) kept[i].applied = -1;
        if (es2_shell_editors(&vfs, t, x, (es2_span){start, size}, &s, kept, nkept, &batch, NULL, &e) == ES2_OK)
            same = batch.size == state.size && !memcmp(batch.data, state.data, state.size) &&
                   session_equal(&s, &session);
        for (size_t i = 0; i < nkept; i++) same = same && kept[i].applied == flags[i];
        printf("BATCH %s\nSAV ", same ? "same" : "differs");
        hex(state.data, state.size);
        printf("\nEND\n");
        free(batch.data);
        free(state.data);
        fflush(stdout);
    }
    return 0;
}
