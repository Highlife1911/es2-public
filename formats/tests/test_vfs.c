#include "es2/formats/vfs.h"
#include "test_support.h"
#include <string.h>

/* Two same-basename records in distinct directories, built only from literals. */
static size_t archive(uint8_t *bytes, uint8_t value, uint8_t priority)
{
    memset(bytes, 0, 128);
    memcpy(bytes, "VOLN", 4); bytes[8] = priority; bytes[9] = 2;
    test_put16(bytes + 10, 10);
    memcpy(bytes + 12, "dat\\\0GAM\\\0", 10);
    test_put16(bytes + 22, 2); test_put32(bytes + 24, 36);
    memcpy(bytes + 28, "WEAPONS.DAT", 12);
    test_put32(bytes + 42, 64);
    memcpy(bytes + 46, "WEAPONS.DAT", 12); bytes[59] = 1;
    test_put32(bytes + 60, 76);
    bytes[64] = 2; test_put32(bytes + 65, 2);
    bytes[73] = value; bytes[74] = value; bytes[75] = value;
    bytes[76] = 2; test_put32(bytes + 77, 1);
    bytes[85] = 3; bytes[86] = 3;
    return 87;
}

static void valid_enumeration(void)
{
    uint8_t first_bytes[128], last_bytes[128], equal_bytes[2] = {1, 1};
    uint8_t extra[1] = {4}, other[1] = {8};
    es2_vol first, last;
    es2_vfs_file loose[] = {{"DaT/weapons.dat", {equal_bytes, 2}},
        {"DAT/extra.bin", {extra, 1}}, {"OTHER/empty.bin", {NULL, 0}}};
    es2_vfs_file more[] = {{"other\\EMPTY.bin", {NULL, 0}},
        {"GAM/other.bin", {other, 1}}};
    es2_vfs_source sources[4], reversed[4];
    es2_vfs vfs;
    es2_vfs_entry entries[9], again[9];
    es2_vfs_copy copies[9], copies_again[9], found, sentinel;
    es2_vfs_stats stats, stats_again;
    es2_error error;
    size_t duplicate[5], required, i;
    CHECK(es2_vol_open((es2_span){first_bytes, archive(first_bytes, 1, 0)},
                       &first, &error) == ES2_OK);
    CHECK(es2_vol_open((es2_span){last_bytes, archive(last_bytes, 9, 255)},
                       &last, &error) == ES2_OK);
    sources[0] = (es2_vfs_source){.kind = ES2_VFS_VOL, .data.vol = &first};
    sources[1] = (es2_vfs_source){.kind = ES2_VFS_LOOSE, .data.loose = {loose, 3}};
    sources[2] = (es2_vfs_source){.kind = ES2_VFS_VOL, .data.vol = &last};
    sources[3] = (es2_vfs_source){.kind = ES2_VFS_LOOSE, .data.loose = {more, 2}};
    CHECK(es2_vfs_open(sources, 4, &vfs, &error) == ES2_OK);
    CHECK(vfs.file_count == 9 && vfs.source_count == 4 && vfs.sources == sources);
    CHECK(es2_vfs_find(&vfs, "DAT\\weapons.DAT", &found, &error) == ES2_OK);
    CHECK(found.source_index == 0 && found.file_index == 0 && found.payload.data[0] == 1);
    CHECK(found.path.prefix.size == 4 && found.path.name.size == 11 && found.next_copy == SIZE_MAX);
    CHECK(es2_vfs_find(&vfs, "gam/weapons.dat", &found, &error) == ES2_OK);
    CHECK(found.source_index == 0 && found.file_index == 1 && found.payload.size == 1);
    CHECK(es2_vfs_find(&vfs, "other/EMPTY.BIN", &found, &error) == ES2_OK);
    CHECK(found.source_index == 1 && found.file_index == 2 && found.payload.size == 0);
    sentinel = found;
    CHECK(es2_vfs_find(&vfs, "misc/weapons.dat", &found, &error) == ES2_ERR_NOT_FOUND);
    CHECK(found.source_index == sentinel.source_index && found.file_index == sentinel.file_index);
    CHECK(es2_vfs_find(&vfs, "WEAPONS.DAT", &found, &error) == ES2_ERR_STRING);
    CHECK(es2_vfs_enumerate(&vfs, entries, 5, copies, 9, &stats, &error) == ES2_OK);
    CHECK(stats.unique_count == 5 && stats.copy_count == 9 && stats.duplicate_count == 3);
    CHECK(stats.identical_duplicate_count == 2);
    CHECK(entries[0].first_copy == 0 && entries[0].last_copy == 5 && entries[0].copy_count == 3);
    CHECK(entries[0].all_identical == 0 && copies[0].next_copy == 2 && copies[2].next_copy == 5);
    CHECK(copies[5].next_copy == SIZE_MAX && copies[5].source_index == 2 && copies[5].file_index == 0);
    CHECK(entries[1].copy_count == 2 && entries[1].all_identical == 1 && copies[1].next_copy == 6);
    CHECK(entries[2].copy_count == 1 && entries[2].first_copy == 3 && entries[2].all_identical == 1);
    CHECK(entries[3].copy_count == 2 && entries[3].all_identical == 1 && copies[4].next_copy == 7);
    CHECK(entries[4].first_copy == 8 && entries[4].copy_count == 1);
    CHECK(es2_vfs_duplicate_indices(entries, stats.unique_count, duplicate, 5, &required, &error) == ES2_OK);
    CHECK(required == 3 && duplicate[0] == 0 && duplicate[1] == 1 && duplicate[2] == 3);
    duplicate[0] = 1234;
    CHECK(es2_vfs_duplicate_indices(entries, 5, duplicate, 2, &required, &error) == ES2_ERR_CAPACITY);
    CHECK(required == 3 && duplicate[0] == 1234);
    CHECK(es2_vfs_duplicate_indices(entries, 5, NULL, 0, &required, &error) == ES2_ERR_CAPACITY);
    CHECK(required == 3);
    CHECK(es2_vfs_enumerate(&vfs, again, 9, copies_again, 9, &stats_again, &error) == ES2_OK);
    CHECK(stats_again.unique_count == stats.unique_count && stats_again.copy_count == stats.copy_count);
    for (i = 0; i < stats.copy_count; ++i) {
        CHECK(copies[i].source_index == copies_again[i].source_index);
        CHECK(copies[i].file_index == copies_again[i].file_index);
        CHECK(copies[i].next_copy == copies_again[i].next_copy);
    }
    for (i = 0; i < stats.unique_count; ++i) {
        CHECK(entries[i].first_copy == again[i].first_copy);
        CHECK(entries[i].last_copy == again[i].last_copy);
        CHECK(entries[i].all_identical == again[i].all_identical);
    }
    /* Explicit source reordering changes the winner. Archive priority does not. */
    reversed[0] = sources[2]; reversed[1] = sources[3];
    reversed[2] = sources[1]; reversed[3] = sources[0];
    CHECK(es2_vfs_open(reversed, 4, &vfs, &error) == ES2_OK);
    CHECK(es2_vfs_find(&vfs, "dat/weapons.dat", &found, &error) == ES2_OK);
    CHECK(found.source_index == 0 && found.payload.data[0] == 9);
    CHECK(es2_vfs_enumerate(&vfs, entries, 9, copies, 9, &stats, &error) == ES2_OK);
    CHECK(copies[entries[0].first_copy].payload.data[0] == 9 && stats.duplicate_count == 3);
    /* Loose entries can win simply by placing their source first. */
    reversed[0] = sources[1]; reversed[2] = sources[2];
    CHECK(es2_vfs_open(reversed, 4, &vfs, &error) == ES2_OK);
    CHECK(es2_vfs_find(&vfs, "dat/weapons.dat", &found, &error) == ES2_OK);
    CHECK(found.source_index == 0 && found.path.prefix.size == 0 && found.payload.data == equal_bytes);
    /* A/B/A copies remain differing after the final copy matches the winner. */
    reversed[0] = sources[0]; reversed[1] = sources[2]; reversed[2] = sources[1];
    CHECK(es2_vfs_open(reversed, 3, &vfs, &error) == ES2_OK);
    CHECK(es2_vfs_enumerate(&vfs, entries, 9, copies, 9, &stats, &error) == ES2_OK);
    CHECK(entries[0].copy_count == 3 && entries[0].all_identical == 0);
    CHECK(copies[0].payload.data[0] == 1 && copies[2].payload.data[0] == 9
          && copies[4].payload.data[0] == 1);
    /* Reusing one archive as two explicit sources keeps both provenances. */
    reversed[0] = sources[0]; reversed[1] = sources[0];
    CHECK(es2_vfs_open(reversed, 2, &vfs, &error) == ES2_OK);
    CHECK(es2_vfs_enumerate(&vfs, entries, 2, copies, 4, &stats, &error) == ES2_OK);
    CHECK(stats.copy_count == 4 && stats.unique_count == 2 && stats.duplicate_count == 2);
    CHECK(stats.identical_duplicate_count == 2 && copies[0].source_index == 0
          && copies[2].source_index == 1 && copies[0].next_copy == 2);
    /* Same bytes but different lengths must remain a differing duplicate. */
    loose[0].data.size = 1;
    CHECK(es2_vfs_open(sources, 4, &vfs, &error) == ES2_OK);
    CHECK(es2_vfs_enumerate(&vfs, entries, 9, copies, 9, &stats, &error) == ES2_OK);
    CHECK(entries[0].all_identical == 0);
    /* Exact entry capacity is supported; partial arrays must be discarded. */
    stats.unique_count = 999;
    CHECK(es2_vfs_enumerate(&vfs, entries, 4, copies, 9, &stats, &error) == ES2_ERR_CAPACITY);
    CHECK(stats.unique_count == 999);
    CHECK(es2_vfs_enumerate(&vfs, entries, 9, copies, 8, &stats, &error) == ES2_ERR_CAPACITY);
    CHECK(es2_vfs_enumerate(&vfs, NULL, 0, copies, 9, &stats, &error) == ES2_ERR_CAPACITY);
    CHECK(es2_vfs_enumerate(&vfs, entries, 9, NULL, 0, &stats, &error) == ES2_ERR_CAPACITY);
    CHECK(es2_vfs_enumerate(&vfs, NULL, 9, copies, 9, &stats, &error) == ES2_ERR_ARGUMENT);
    CHECK(es2_vfs_enumerate(&vfs, entries, 9, NULL, 9, &stats, &error) == ES2_ERR_ARGUMENT);
    CHECK(es2_vfs_enumerate(&vfs, entries, 9, copies, 9, NULL, &error) == ES2_ERR_ARGUMENT);
    CHECK(es2_vfs_enumerate(NULL, entries, 9, copies, 9, &stats, &error) == ES2_ERR_ARGUMENT);
    CHECK(es2_vfs_find(NULL, "a/b", &found, &error) == ES2_ERR_ARGUMENT);
    CHECK(es2_vfs_find(&vfs, NULL, &found, &error) == ES2_ERR_ARGUMENT);
    CHECK(es2_vfs_find(&vfs, "a/b", NULL, &error) == ES2_ERR_ARGUMENT);
}

static void path_and_source_validation(void)
{
    const char *bad[] = {"", "basename.dat", "/dir/file", "\\dir/file", "dir/",
        "dir//file", "dir\\/file", "./dir/file", "dir/../file", "dir/./file",
        "dir/file/..", "C:/dir/file", "dir/:file", "dir/\x1f", "dir/\x7f", "dir/\xff"};
    uint8_t bytes[128];
    es2_vfs_file files[2] = {{"first/file.dat", {NULL, 0}}, {"FIRST\\FILE.DAT", {NULL, 0}}};
    es2_vfs_source source = {.kind = ES2_VFS_LOOSE, .data.loose = {files, 1}};
    es2_vfs vfs = {0};
    es2_vfs_copy found;
    es2_vol vol;
    es2_error error;
    size_t i;
    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        files[0].path = bad[i];
        CHECK(es2_vfs_open(&source, 1, &vfs, &error) == ES2_ERR_STRING);
        CHECK(vfs.sources == NULL && vfs.source_count == 0 && vfs.file_count == 0);
        CHECK(es2_vfs_find(&vfs, bad[i], &found, &error) == ES2_ERR_STRING);
    }
    files[0].path = "first/file.dat"; source.data.loose.count = 2;
    CHECK(es2_vfs_open(&source, 1, &vfs, &error) == ES2_ERR_STRING);
    CHECK(error.offset == 1);
    source.data.loose.count = 1; files[0].path = NULL;
    CHECK(es2_vfs_open(&source, 1, &vfs, &error) == ES2_ERR_ARGUMENT);
    files[0].path = "a/.../file"; files[0].data.size = 1;
    CHECK(es2_vfs_open(&source, 1, &vfs, &error) == ES2_ERR_ARGUMENT);
    files[0].data.size = 0;
    CHECK(es2_vfs_open(&source, 1, &vfs, &error) == ES2_OK);
    CHECK(es2_vfs_find(&vfs, "A/.../FILE", &found, &error) == ES2_OK);
    source.data.loose.files = NULL;
    CHECK(es2_vfs_open(&source, 1, &vfs, &error) == ES2_ERR_ARGUMENT);
    source.data.loose.files = files; source.data.loose.count = SIZE_MAX;
    CHECK(es2_vfs_open(&source, 1, &vfs, &error) == ES2_ERR_COUNT);
    source.kind = (es2_vfs_source_kind)99;
    CHECK(es2_vfs_open(&source, 1, &vfs, &error) == ES2_ERR_ARGUMENT);
    source.kind = ES2_VFS_VOL; source.data.vol = NULL;
    CHECK(es2_vfs_open(&source, 1, &vfs, &error) == ES2_ERR_ARGUMENT);
    CHECK(es2_vfs_open(NULL, 1, &vfs, &error) == ES2_ERR_ARGUMENT);
    CHECK(es2_vfs_open(&source, 1, NULL, &error) == ES2_ERR_ARGUMENT);
    CHECK(es2_vfs_open(&source, SIZE_MAX, &vfs, &error) == ES2_ERR_COUNT);
    memset(&vol, 0, sizeof(vol)); source.data.vol = &vol; vol.member_count = SIZE_MAX;
    CHECK(es2_vfs_open(&source, 1, &vfs, &error) == ES2_ERR_COUNT);
    /* Aggregate count rejection precedes traversal of either source. */
    {
        es2_vol huge = {0};
        es2_vfs_source many[2];
        huge.member_count = SIZE_MAX / sizeof(es2_vfs_copy);
        many[0] = (es2_vfs_source){.kind = ES2_VFS_VOL, .data.vol = &huge};
        many[1] = many[0];
        CHECK(es2_vfs_open(many, 2, &vfs, &error) == ES2_ERR_COUNT);
    }
    CHECK(es2_vol_open((es2_span){bytes, archive(bytes, 1, 0)}, &vol, &error) == ES2_OK);
    /* VOL has looser directory grammar; VFS applies its own relative-path rule. */
    bytes[12] = ':';
    CHECK(es2_vfs_open(&source, 1, &vfs, &error) == ES2_ERR_STRING);
    bytes[12] = 'd'; bytes[64] = 3;
    CHECK(es2_vfs_open(&source, 1, &vfs, &error) == ES2_ERR_UNSUPPORTED);
}

static void empty_cases(void)
{
    es2_vfs vfs;
    es2_vfs_copy found;
    es2_vfs_stats stats;
    es2_vfs_entry entry = {0};
    es2_vfs_source empty = {.kind = ES2_VFS_LOOSE, .data.loose = {NULL, 0}};
    es2_error error;
    size_t required = 99, indices[1];
    CHECK(es2_vfs_open(NULL, 0, &vfs, &error) == ES2_OK);
    CHECK(es2_vfs_find(&vfs, "a/b", &found, &error) == ES2_ERR_NOT_FOUND);
    CHECK(es2_vfs_enumerate(&vfs, NULL, 0, NULL, 0, &stats, &error) == ES2_OK);
    CHECK(stats.unique_count == 0 && stats.copy_count == 0 && stats.duplicate_count == 0);
    CHECK(stats.identical_duplicate_count == 0);
    CHECK(es2_vfs_open(&empty, 1, &vfs, &error) == ES2_OK && vfs.file_count == 0);
    CHECK(es2_vfs_enumerate(&vfs, NULL, 0, NULL, 0, &stats, &error) == ES2_OK);
    CHECK(es2_vfs_duplicate_indices(NULL, 0, NULL, 0, &required, &error) == ES2_OK);
    CHECK(required == 0);
    CHECK(es2_vfs_duplicate_indices(NULL, 1, indices, 1, &required, &error) == ES2_ERR_ARGUMENT);
    CHECK(es2_vfs_duplicate_indices(&entry, 1, NULL, 1, &required, &error) == ES2_ERR_ARGUMENT);
    CHECK(es2_vfs_duplicate_indices(&entry, 1, indices, 1, NULL, &error) == ES2_ERR_ARGUMENT);
    CHECK(es2_vfs_duplicate_indices(&entry, SIZE_MAX, indices, 1, &required, &error) == ES2_ERR_COUNT);
    entry.copy_count = 1;
    CHECK(es2_vfs_duplicate_indices(&entry, 1, NULL, 0, &required, NULL) == ES2_OK && required == 0);
}

int test_vfs(void)
{
    valid_enumeration();
    path_and_source_validation();
    empty_cases();
    return 0;
}
