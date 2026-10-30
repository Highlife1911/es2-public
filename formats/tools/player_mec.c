/* Own template editor; CD tables are read at runtime, never compiled in. */
#include "es2/formats/player_mec.h"
#include "es2/formats/vol.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int load(const char *path, es2_span *span)
{
    FILE *f = fopen(path, "rb");
    long n; uint8_t *p;
    if (!f) { fprintf(stderr, "Cannot read %s\n", path); return 0; }
    if (fseek(f, 0, SEEK_END) || (n = ftell(f)) < 0 || fseek(f, 0, SEEK_SET)) { fclose(f); return 0; }
    p = malloc(n ? (size_t)n : 1);
    if (!p) { fclose(f); return 0; }
    if (fread(p, 1, (size_t)n, f) != (size_t)n) { free(p); fclose(f); return 0; }
    if (fclose(f)) { free(p); return 0; }
    *span = (es2_span){p, (size_t)n}; return 1;
}
static int failure(es2_status s, const es2_error *e)
{
    if (s == ES2_OK) return 0;
    fprintf(stderr, "MEC/table error: %s at byte %zu\n", es2_status_string(s), e->offset); return 1;
}
static int validate(const es2_mec *m, const char *root)
{
    char path[4096]; es2_span bytes = {0}; es2_vol vol; es2_vol_member member;
    es2_dat hercs, weapons, *loadouts = NULL; es2_error e = {0};
    es2_status s; size_t i, n = 0; unsigned warnings = 0; int result = 1;
    if (snprintf(path, sizeof path, "%s/VOL/SHELL0.VOL", root) >= (int)sizeof path || !load(path, &bytes)) return 1;
    s = es2_vol_open(bytes, &vol, &e);
    if (s == ES2_OK) s = es2_vol_find(&vol, "GAM/HERC_INF.DAT", &member, &e);
    if (s == ES2_OK) s = es2_dat_parse(ES2_DAT_HERC_INF, member.payload, 0, &hercs, &e);
    if (s == ES2_OK) s = es2_vol_find(&vol, "GAM/WEAPONS.DAT", &member, &e);
    if (s == ES2_OK) s = es2_dat_parse(ES2_DAT_SHELL_WEAPONS, member.payload, 0, &weapons, &e);
    if (failure(s, &e)) goto done;
    loadouts = calloc(vol.member_count, sizeof *loadouts);
    if (!loadouts) goto done;
    for (i = 0; i < vol.member_count; ++i) {
        s = es2_vol_member_at(&vol, i, &member, &e);
        if (s != ES2_OK) break;
        if (member.directory.size + member.name.size >= sizeof path) { s = ES2_ERR_LENGTH; break; }
        memcpy(path, member.directory.data, member.directory.size);
        memcpy(path + member.directory.size, member.name.data, member.name.size);
        path[member.directory.size + member.name.size] = 0;
        if (es2_dat_classify(path) == ES2_DAT_LOADOUT) {
            s = es2_dat_parse(ES2_DAT_LOADOUT, member.payload, 0, &loadouts[n], &e);
            if (s != ES2_OK) break;
            ++n;
        }
    }
    if (failure(s, &e)) goto done;
    if (!n) { fputs("No INI loadout evidence in shell archive\n", stderr); goto done; }
    s = es2_mec_validate(m, &hercs, &weapons, loadouts, n, &warnings, &e);
    if (failure(s, &e)) goto done;
    if (warnings & ES2_MEC_WARN_K) fputs("WARNING hypothesis: K differs from HERC_INF +6; that field is not proved to be MEC capacity.\n", stderr);
    if (warnings & ES2_MEC_WARN_MOUNT) fputs("WARNING hypothesis: some item/slot combinations are absent from INI loadouts; mount legality is unverified.\n", stderr);
    fputs("WARNING unverified: mission team capacity, condition blocks and secondary-word semantics; confirm generated loadouts in VSHELL/game.\n", stderr);
    result = 0;
done:
    free(loadouts); free((void *)bytes.data); return result;
}
static int roundtrip(const char *path, const char *root)
{
    es2_span bytes; es2_mec m; es2_error e; uint8_t *out = NULL; size_t n = 0; int bad = 1;
    if (!load(path, &bytes)) return 1;
    if (failure(es2_mec_parse(bytes, &m, &e), &e)) goto done;
    if (failure(es2_mec_write(&m, NULL, 0, 5, NULL, 0, &n, &e), &e)) goto done;
    out = malloc(n);
    if (!out || failure(es2_mec_write(&m, NULL, 0, 5, out, n, &n, &e), &e)) goto done;
    bad = n != bytes.size || memcmp(out, bytes.data, n) || (root && validate(&m, root));
    if (!bad) printf("roundtrip identical: %s (%zu bytes, %zu records; memory only)\n", path, n, m.count);
done:
    free(out); free((void *)bytes.data); return bad;
}
#ifdef ES2_MEC_REAL_TEST
int main(void)
{
    const char *root = getenv("ES2_DATA_ROOT");
    const char *examples = getenv("ES2_MEC_EXAMPLES");
    /* SAV/ only: a live regression replaces the clone's DATA/ during its
     * runs, and DATA/player.mec between runs is byte-identical to
     * SAV/player10.mec (SHA-256 f669d125..., formats/README.md). */
    const char *names[] = {"SAV/player0.mec", "SAV/player1.mec", "SAV/player10.mec", "SAV/player11.mec"};
    char base[4096], path[4096]; size_t i; int bad = 0;
    if (!root || !*root) { puts("SKIP: ES2_DATA_ROOT unset"); return 0; }
    if (!examples || !*examples) {
        if (snprintf(base, sizeof base, "%s/../../installs/timing-r111", root) >= (int)sizeof base) return 1;
        examples = base;
    }
    for (i = 0; i < sizeof names / sizeof names[0]; ++i) {
        if (snprintf(path, sizeof path, "%s/%s", examples, names[i]) >= (int)sizeof path) return 1;
        bad |= roundtrip(path, root);
    }
    return bad;
}
#else
static int number(const char *text, long maximum, long *out)
{
    char *end; long v;
    errno = 0; v = strtol(text, &end, 10);
    if (errno || !*text || *end || v < 0 || v > maximum) return 0;
    *out = v; return 1;
}
static void show_record(size_t i, const es2_mec_record *r)
{
    size_t j;
    printf("record=%zu offset=%zu vehicle=%d K=%zu items=", i, r->offset, r->vehicle_id, r->count);
    for (j = 0; j < r->count; ++j) { int16_t id; uint16_t v; (void)es2_mec_item_at(r, j, &id, &v, NULL); printf("%s%d", j ? "," : "", id); }
    printf(" secondary=");
    for (j = 0; j < r->count; ++j) { int16_t id; uint16_t v; (void)es2_mec_item_at(r, j, &id, &v, NULL); printf("%s%u", j ? "," : "", v); }
    puts("");
}
static void show(const es2_mec *m)
{
    size_t i;
    printf("records=%zu header=%u suffix_flags=33\n", m->count, m->header);
    for (i = 0; i < m->count; ++i) {
        es2_mec_record r; (void)es2_mec_record_at(m, i, &r, NULL);
        show_record(i, &r);
    }
}
static size_t differing_bytes(es2_span a, es2_span b)
{
    size_t i, common = a.size < b.size ? a.size : b.size;
    size_t n = a.size + b.size - 2 * common;
    for (i = 0; i < common; ++i) n += a.data[i] != b.data[i];
    return n;
}
static int diff(const char *left, const char *right)
{
    es2_span a = {0}, b = {0}; es2_mec ma, mb; es2_error e;
    size_t i, n; int result = 2;
    if (!load(left, &a) || !load(right, &b)) goto done;
    if (failure(es2_mec_parse(a, &ma, &e), &e) || failure(es2_mec_parse(b, &mb, &e), &e)) goto done;
    result = differing_bytes(a, b) ? 1 : 0;
    if (!result) { puts("identical (all bytes)"); goto done; }
    if (ma.header != mb.header) printf("header: %u -> %u\n", ma.header, mb.header);
    if (ma.count != mb.count) printf("records: %zu -> %zu\n", ma.count, mb.count);
    n = differing_bytes(ma.flags, mb.flags);
    if (n) printf("suffix flags: %zu changed bytes\n", n);
    for (i = 0; i < ma.count || i < mb.count; ++i) {
        es2_mec_record ra = {0}, rb = {0};
        if (i < ma.count) (void)es2_mec_record_at(&ma, i, &ra, NULL);
        if (i < mb.count) (void)es2_mec_record_at(&mb, i, &rb, NULL);
        if (i >= ma.count) { printf("added "); show_record(i, &rb); continue; }
        if (i >= mb.count) { printf("removed "); show_record(i, &ra); continue; }
        if (ra.vehicle_id != rb.vehicle_id || differing_bytes(ra.items, rb.items) ||
            differing_bytes(ra.secondary, rb.secondary)) {
            printf("before "); show_record(i, &ra);
            printf("after  "); show_record(i, &rb);
        }
        n = differing_bytes(ra.pilot, rb.pilot);
        if (n) printf("record=%zu pilot block: %zu changed bytes\n", i, n);
        n = differing_bytes(ra.tail, rb.tail);
        if (n) printf("record=%zu opaque tail: %zu changed bytes\n", i, n);
    }
done:
    free((void *)a.data); free((void *)b.data); return result;
}
int main(int argc, char **argv)
{
    es2_span bytes = {0}; es2_mec m, changed; es2_error e;
    es2_mec_edit *edits = NULL; size_t edit_count = 0, i, n = 0;
    uint8_t *out = NULL; int bad = 1;
    if (argc < 3) goto usage;
    if (!strcmp(argv[1], "diff") && argc == 4) return diff(argv[2], argv[3]);
    if (!strcmp(argv[1], "roundtrip") && (argc == 3 || argc == 4)) return roundtrip(argv[2], argc == 4 ? argv[3] : NULL);
    if (!strcmp(argv[1], "show") && (argc == 3 || argc == 4)) {
        if (!load(argv[2], &bytes)) return 1;
        if (!failure(es2_mec_parse(bytes, &m, &e), &e)) { show(&m); bad = argc == 4 ? validate(&m, argv[3]) : 0; }
        free((void *)bytes.data); return bad;
    }
    if (strcmp(argv[1], "set") || argc < 9 || (argc - 5) % 4) goto usage;
    if (!load(argv[2], &bytes)) return 1;
    if (failure(es2_mec_parse(bytes, &m, &e), &e)) goto done;
    edit_count = (size_t)(argc - 5) / 4;
    edits = calloc(edit_count, sizeof *edits);
    if (!edits) goto done;
    for (i = 0; i < edit_count; ++i) {
        long index, vehicle, item; char *p, *next, *list; int16_t *items;
        int a = 5 + (int)(4 * i);
        if (strcmp(argv[a], "--record") || !number(argv[a+1], INT16_MAX, &index) ||
            !number(argv[a+2], INT16_MAX, &vehicle)) goto done;
        edits[i].record = (size_t)index; edits[i].vehicle_id = (int16_t)vehicle;
        list = malloc(strlen(argv[a+3]) + 1); items = malloc(10 * sizeof *items);
        if (!list || !items) { free(list); free(items); goto done; }
        strcpy(list, argv[a+3]); edits[i].items = items;
        if (strcmp(list, "none")) {
            p = list;
            do {
                next = strchr(p, ','); if (next) *next++ = 0;
                if (edits[i].count == 10 || !number(p, INT16_MAX, &item)) { free(list); goto done; }
                items[edits[i].count++] = (int16_t)item; p = next;
            } while (p);
        }
        free(list);
    }
    if (failure(es2_mec_write(&m, edits, edit_count, 5, NULL, 0, &n, &e), &e)) goto done;
    out = malloc(n);
    if (!out || failure(es2_mec_write(&m, edits, edit_count, 5, out, n, &n, &e), &e)) goto done;
    if (failure(es2_mec_parse((es2_span){out,n}, &changed, &e), &e) || validate(&changed, argv[4])) goto done;
    {
        FILE *f = fopen(argv[3], "wbx"); size_t written; int closed;
        if (!f) { fprintf(stderr, "Output must be a new writable file: %s\n", argv[3]); goto done; }
        written = fwrite(out, 1, n, f); closed = fclose(f);
        if (written != n || closed) { fprintf(stderr, "Incomplete output: %s\n", argv[3]); goto done; }
    }
    show(&changed); bad = 0;
done:
    if (edits) for (i = 0; i < edit_count; ++i) free((void *)edits[i].items);
    free(edits); free(out); free((void *)bytes.data);
    if (bad) fputs("No valid edited loadout produced. See usage and diagnostics.\n", stderr);
    return bad;
usage:
    fputs("player_mec show FILE [CD_ROOT]\nplayer_mec diff BEFORE AFTER (exit 0 identical, 1 different, 2 invalid)\nplayer_mec roundtrip FILE [CD_ROOT] (memory only)\nplayer_mec set TEMPLATE NEW_OUTPUT CD_ROOT --record INDEX VEHICLE ID,ID,... [--record ...]\nIndices start at zero; 'none' sets K=0. New secondary words use VSHELL's absent-slot default 5.\n", stderr);
    return 2;
}
#endif
