/* Reads original inputs only when explicitly opted in. Never writes assets. */
#include "es2/formats/vfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)
static const char *const archives[] = {
    "LANG0", "SHELL0", "SHELL1", "SHLSOUND", "SIMALERT", "SIMLANG",
    "SIMPATCH", "SIMSOUND", "SIMVOICE", "SIMVOICF", "SIMVOICG", "SIMVOL0", "ZONES"
};
static const char *const paths[] = {
    "dfn\\map.dfn", "dfn\\black.dfn", "dfn\\font2.dfn", "dfn\\font.dfn",
    "dfn\\cpylw.dfn", "dfn\\green6x8.dfn", "hfn\\cpylw.hfn",
    "hfn\\green6x8.hfn", "pdg\\spider.pdg", "bnd\\results.bnd"
};
/* Derived contract expectations, not copied resource tables. */
static const int expected_sim[] = {-1, -1, -1, 11, 6, 6, 6, 6, 6, 6};
static const int expected_shell[] = {2, 2, 2, 2, -1, -1, -1, -1, -1, -1};

static uint8_t *read_archive(const char *root, const char *name, size_t *length)
{
    char path[4096]; FILE *file; long size; uint8_t *bytes;
    int count = snprintf(path, sizeof path, "%s/VOL/%s.VOL", root, name);
    if (count < 0 || (size_t)count >= sizeof path) return NULL;
    file = fopen(path, "rb");
    if (file == NULL) return NULL;
    if (fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) < 0
        || fseek(file, 0, SEEK_SET) != 0) { fclose(file); return NULL; }
    bytes = malloc((size_t)size);
    if (bytes == NULL) { fclose(file); return NULL; }
    if (fread(bytes, 1, (size_t)size, file) != (size_t)size) {
        free(bytes); fclose(file); return NULL;
    }
    fclose(file); *length = (size_t)size; return bytes;
}

static int identical(es2_span a, es2_span b)
{ return a.size == b.size && (a.size == 0 || memcmp(a.data, b.data, a.size) == 0); }

static void verify_view(const es2_vol *vols, es2_vfs_source *sources,
                        uint32_t mask, const int *expected)
{
    es2_vfs vfs; size_t order[13], i;
    CHECK(es2_vfs_open_dbsim_order(sources, 13, mask, order, 13, &vfs, NULL) == ES2_OK);
    CHECK(vfs.order_count == (mask == 1 ? 9u : 5u));
    CHECK(vfs.file_count == (mask == 1 ? 2963u : 524u));
    for (i = 0; i < 10; ++i) {
        es2_vfs_copy winner;
        es2_status status = es2_vfs_find(&vfs, paths[i], &winner, NULL);
        if (expected[i] < 0) { CHECK(status == ES2_ERR_NOT_FOUND); continue; }
        CHECK(status == ES2_OK);
        if (status == ES2_OK) {
            es2_vol_member member;
            CHECK(sources[winner.source_index].data.vol == &vols[expected[i]]);
            CHECK(es2_vol_find(&vols[expected[i]], paths[i], &member, NULL) == ES2_OK);
            CHECK(identical(winner.payload, member.payload));
        }
    }
}

static void corpus_overlap(es2_vfs_source *sources)
{
    es2_vfs vfs; size_t order[13], i, equal_different = 0;
    es2_vfs_entry *entries; es2_vfs_copy *copies; es2_vfs_stats stats;
    CHECK(es2_vfs_open_dbsim_order(sources, 13, UINT32_MAX, order, 13, &vfs, NULL) == ES2_OK);
    CHECK(vfs.file_count == 3163);
    entries = calloc(vfs.file_count, sizeof *entries);
    copies = calloc(vfs.file_count, sizeof *copies);
    CHECK(entries != NULL && copies != NULL);
    if (entries == NULL || copies == NULL) { free(entries); free(copies); return; }
    CHECK(es2_vfs_enumerate(&vfs, entries, vfs.file_count, copies, vfs.file_count,
                            &stats, NULL) == ES2_OK);
    CHECK(stats.unique_count == 2595 && stats.copy_count == 3163);
    CHECK(stats.duplicate_count == 567 && stats.identical_duplicate_count == 557);
    for (i = 0; i < stats.unique_count; ++i) {
        size_t a;
        for (a = entries[i].first_copy; a != SIZE_MAX; a = copies[a].next_copy) {
            size_t b;
            for (b = copies[a].next_copy; b != SIZE_MAX; b = copies[b].next_copy) {
                const es2_vol *va = sources[copies[a].source_index].data.vol;
                const es2_vol *vb = sources[copies[b].source_index].data.vol;
                if (va->priority != vb->priority || identical(copies[a].payload, copies[b].payload)) continue;
                ++equal_different;
                CHECK((va->flag0 & vb->flag0) == 0 && (va->flag1 & vb->flag1) == 0);
                CHECK((copies[a].payload.size == 18249 && copies[b].payload.size == 7749)
                    || (copies[b].payload.size == 18249 && copies[a].payload.size == 7749));
            }
        }
    }
    CHECK(equal_different == 1);
    /* Normal process masks split those ten differing paths into six and four. */
    for (i = 0; i < 2; ++i) {
        CHECK(es2_vfs_open_dbsim_order(sources, 13, i == 0 ? 1 : 0x100,
                                     order, 13, &vfs, NULL) == ES2_OK);
        CHECK(es2_vfs_enumerate(&vfs, entries, 3163, copies, 3163, &stats, NULL) == ES2_OK);
        CHECK(stats.unique_count == (i == 0 ? 2401u : 520u));
        CHECK(stats.duplicate_count == (i == 0 ? 562u : 4u));
        CHECK(stats.duplicate_count - stats.identical_duplicate_count == (i == 0 ? 6u : 4u));
    }
    free(entries); free(copies);
}

/* Every member of every archive is found by DBSIM's per-archive rule
 * under its own directory string and name (no archive repeats a directory
 * string), in lower case too; with '/' as the separator never. */
static void dbsim_self_lookup(const es2_vol *vols)
{
    size_t a, i, members = 0;
    for (a = 0; a < 13; ++a) {
        for (i = 0; i < vols[a].member_count; ++i) {
            es2_vol_member m; char path[300], lower[300]; size_t index = SIZE_MAX, k;
            CHECK(es2_vol_member_at(&vols[a], i, &m, NULL) == ES2_OK);
            CHECK(m.directory.size + m.name.size < sizeof path);
            memcpy(path, m.directory.data, m.directory.size);
            memcpy(path + m.directory.size, m.name.data, m.name.size);
            path[m.directory.size + m.name.size] = 0;
            for (k = 0; path[k]; ++k)
                lower[k] = (char)(path[k] >= 'A' && path[k] <= 'Z' ? path[k] + 32 : path[k]);
            lower[k] = 0;
            CHECK(es2_vfs_dbsim_member(&vols[a], path, &index, NULL) == ES2_OK && index == i);
            CHECK(es2_vfs_dbsim_member(&vols[a], lower, &index, NULL) == ES2_OK && index == i);
            path[m.directory.size - 1] = '/';
            CHECK(es2_vfs_dbsim_member(&vols[a], path, &index, NULL) == ES2_ERR_NOT_FOUND);
            ++members;
        }
    }
    CHECK(members == 3163);
}

int main(void)
{
    const char *root = getenv("ES2_DATA_ROOT");
    uint8_t *bytes[13] = {0}; es2_vol vols[13]; es2_vfs_source sources[13];
    size_t i, rotation, direction;
    if (root == NULL || *root == 0) { puts("SKIP: ES2_DATA_ROOT is unset"); return 0; }
    for (i = 0; i < 13; ++i) {
        size_t length = 0;
        bytes[i] = read_archive(root, archives[i], &length);
        CHECK(bytes[i] != NULL);
        if (bytes[i] == NULL) goto finish;
        if (es2_vol_open((es2_span){bytes[i], length}, &vols[i], NULL) != ES2_OK) {
            CHECK(0); goto finish;
        }
        sources[i] = (es2_vfs_source){.kind = ES2_VFS_VOL, .data.vol = &vols[i]};
    }
    corpus_overlap(sources);
    dbsim_self_lookup(vols);
    /* Different-payload winners are invariant over these 26 directory orders,
     * because their accepted conflicting copies have strictly lower priority. */
    for (direction = 0; direction < 2; ++direction) {
        for (rotation = 0; rotation < 13; ++rotation) {
            for (i = 0; i < 13; ++i) {
                size_t index = (rotation + (direction ? 12 - i : i)) % 13;
                sources[i].data.vol = &vols[index];
            }
            verify_view(vols, sources, 1, expected_sim);
            verify_view(vols, sources, 0x100, expected_shell);
        }
    }
finish:
    for (i = 0; i < 13; ++i) free(bytes[i]);
    printf("VFS DBSIM real data: %u checks, %u failures; ten paths, two process masks, 26 orders\n",
           checks, failures);
    return failures ? 1 : 0;
}
