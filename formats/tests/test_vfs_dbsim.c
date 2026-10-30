#include "es2/formats/vfs.h"
#include <stdio.h>
#include <string.h>

static unsigned checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)
static void put16(uint8_t *p, uint16_t v)
{ p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v)
{ put16(p, (uint16_t)v); put16(p + 2, (uint16_t)(v >> 16)); }

/* Entirely synthetic one-member archive, independently specified here. */
static void fixture(uint8_t bytes[64], uint32_t mask, uint8_t priority,
                    uint8_t payload, es2_vol *vol)
{
    memset(bytes, 0, 64);
    memcpy(bytes, "VOLN", 4); put32(bytes + 4, mask);
    bytes[8] = priority; bytes[9] = 1; put16(bytes + 10, 5);
    memcpy(bytes + 12, "dat\\", 5);
    put16(bytes + 17, 1); put32(bytes + 19, 18);
    memcpy(bytes + 23, "ONE.BIN", 8); put32(bytes + 37, 41);
    bytes[41] = 2; put32(bytes + 42, 1);
    bytes[50] = payload; bytes[51] = payload;
    CHECK(es2_vol_open((es2_span){bytes, 52}, vol, NULL) == ES2_OK);
}

static void expect_order(const es2_vfs_source *sources, size_t count,
                         const size_t *expected, size_t expected_count)
{
    size_t order[32], i;
    es2_vfs vfs;
    es2_vfs_copy winner;
    CHECK(es2_vfs_open_dbsim_order(sources, count, 1, order, 32, &vfs, NULL) == ES2_OK);
    CHECK(vfs.policy == ES2_VFS_DBSIM_ORDER && vfs.order_count == expected_count);
    CHECK(vfs.source_count == count && vfs.source_order == order);
    for (i = 0; i < expected_count; ++i) CHECK(order[i] == expected[i]);
    CHECK(es2_vfs_find(&vfs, "DaT\\OnE.bIn", &winner, NULL) == ES2_OK);
    CHECK(winner.source_index == expected[0]);
}

static void priority_and_ties(void)
{
    uint8_t bytes[5][64];
    es2_vol vols[5];
    es2_vfs_source sources[5], reverse[5];
    size_t i, order[5];
    es2_vfs vfs;
    es2_vfs_copy winner;
    const size_t equal[] = {0, 1}, high_last[] = {2, 1, 0};
    const size_t reversed[] = {0, 1, 2}, two_high[] = {2, 3, 0, 1};
    const size_t unsigned_order[] = {4, 3, 1, 0, 2};
    for (i = 0; i < 5; ++i) {
        fixture(bytes[i], 1, 5, (uint8_t)i, &vols[i]);
        sources[i] = (es2_vfs_source){.kind = ES2_VFS_VOL, .data.vol = &vols[i]};
    }
    expect_order(sources, 2, equal, 2);
    reverse[0] = sources[1]; reverse[1] = sources[0];
    expect_order(reverse, 2, equal, 2); /* Earlier caller entry wins either way. */
    CHECK(es2_vfs_open_dbsim_order(reverse, 2, 1, order, 5, &vfs, NULL) == ES2_OK);
    CHECK(es2_vfs_find(&vfs, "dat\\one.bin", &winner, NULL) == ES2_OK);
    CHECK(winner.payload.data[0] == 1);
    fixture(bytes[2], 1, 10, 2, &vols[2]);
    expect_order(sources, 3, high_last, 3); /* Displaced A passes equal B. */
    /* A high-priority archive with a different path still changes the winner
     * between two older equal-priority copies of ONE.BIN. */
    memcpy(bytes[2] + 23, "TWO.BIN", 8);
    CHECK(es2_vol_open((es2_span){bytes[2], 52}, &vols[2], NULL) == ES2_OK);
    CHECK(es2_vfs_open_dbsim_order(sources, 3, 1, order, 5, &vfs, NULL) == ES2_OK);
    CHECK(es2_vfs_find(&vfs, "dat\\one.bin", &winner, NULL) == ES2_OK);
    CHECK(winner.source_index == 1 && winner.payload.data[0] == 1);
    fixture(bytes[2], 1, 10, 2, &vols[2]);
    reverse[0] = sources[2]; reverse[1] = sources[1]; reverse[2] = sources[0];
    expect_order(reverse, 3, reversed, 3);
    fixture(bytes[3], 1, 10, 3, &vols[3]);
    expect_order(sources, 4, two_high, 4); /* A second carry rotates lows again. */
    fixture(bytes[0], 1, 127, 0, &vols[0]);
    fixture(bytes[1], 1, 128, 1, &vols[1]);
    fixture(bytes[2], 1, 0, 2, &vols[2]);
    fixture(bytes[3], 1, 254, 3, &vols[3]);
    fixture(bytes[4], 1, 255, 4, &vols[4]);
    expect_order(sources, 5, unsigned_order, 5);
    /* Default API keeps caller order and slash folding despite DBSIM order. */
    CHECK(es2_vfs_open(sources, 5, &vfs, NULL) == ES2_OK);
    CHECK(vfs.policy == ES2_VFS_CALLER_ORDER && vfs.source_order == NULL);
    CHECK(es2_vfs_find(&vfs, "DAT/ONE.BIN", &winner, NULL) == ES2_OK);
    CHECK(winner.source_index == 0);
}

static void masks_loose_and_paths(void)
{
    uint8_t bytes[4][64], override = 99, second = 88;
    es2_vol vols[4];
    es2_vfs_source sources[6];
    es2_vfs_file loose = {"DAT/one.bin", {&override, 1}};
    es2_vfs_file later = {"dat\\ONE.BIN", {&second, 1}};
    es2_vfs vfs;
    es2_vfs_copy winner, copies[6];
    es2_vfs_entry entries[6];
    es2_vfs_stats stats;
    size_t order[6], i;
    const uint32_t masks[] = {1, 0x100, 0x101, UINT32_C(0x80000000)};
    for (i = 0; i < 4; ++i) {
        fixture(bytes[i], masks[i], (uint8_t)(i * 70), (uint8_t)i, &vols[i]);
        sources[i] = (es2_vfs_source){.kind = ES2_VFS_VOL, .data.vol = &vols[i]};
    }
    sources[4] = (es2_vfs_source){.kind = ES2_VFS_LOOSE, .data.loose = {&loose, 1}};
    sources[5] = (es2_vfs_source){.kind = ES2_VFS_LOOSE, .data.loose = {&later, 1}};
    CHECK(es2_vfs_open_dbsim_order(sources, 6, 1, order, 6, &vfs, NULL) == ES2_OK);
    CHECK(vfs.order_count == 4 && vfs.file_count == 4);
    CHECK(order[0] == 4 && order[1] == 5 && order[2] == 2 && order[3] == 0);
    CHECK(es2_vfs_find(&vfs, "dat\\ONE.bin", &winner, NULL) == ES2_OK);
    CHECK(winner.source_index == 4 && winner.payload.data[0] == 99);
    CHECK(es2_vfs_find(&vfs, "dat/ONE.bin", &winner, NULL) == ES2_OK);
    CHECK(winner.source_index == 4);
    CHECK(es2_vfs_enumerate(&vfs, entries, 6, copies, 6, &stats, NULL) == ES2_OK);
    CHECK(stats.unique_count == 1 && stats.copy_count == 4 && stats.duplicate_count == 1);
    CHECK(stats.identical_duplicate_count == 0 && entries[0].copy_count == 4);
    CHECK(copies[0].source_index == 4 && copies[1].source_index == 5
        && copies[2].source_index == 2 && copies[3].source_index == 0);
    /* Omit unopenable loose sources: archive fallback, then absent file. */
    CHECK(es2_vfs_open_dbsim_order(sources, 4, 1, order, 6, &vfs, NULL) == ES2_OK);
    CHECK(es2_vfs_find(&vfs, "dAt\\ONE.BIN", &winner, NULL) == ES2_OK);
    CHECK(winner.source_index == 2);
    CHECK(es2_vfs_find(&vfs, "dat/one.bin", &winner, NULL) == ES2_ERR_NOT_FOUND);
    CHECK(es2_vfs_find(&vfs, "dat\\absent.bin", &winner, NULL) == ES2_ERR_NOT_FOUND);
    CHECK(es2_vfs_find(&vfs, "dat\\..\\one.bin", &winner, NULL) == ES2_ERR_STRING);
    CHECK(es2_vfs_open_dbsim_order(sources, 4, 0x100, order, 6, &vfs, NULL) == ES2_OK);
    CHECK(vfs.order_count == 2 && order[0] == 2 && order[1] == 1);
    CHECK(es2_vfs_open_dbsim_order(sources, 4, UINT32_C(0x80000000), order, 6,
                                  &vfs, NULL) == ES2_OK);
    CHECK(vfs.order_count == 1 && order[0] == 3);
    CHECK(es2_vfs_open_dbsim_order(sources, 4, 2, order, 6, &vfs, NULL) == ES2_OK);
    CHECK(vfs.order_count == 0 && vfs.file_count == 0);
    CHECK(es2_vfs_find(&vfs, "dat\\one.bin", &winner, NULL) == ES2_ERR_NOT_FOUND);
    CHECK(es2_vfs_enumerate(&vfs, NULL, 0, NULL, 0, &stats, NULL) == ES2_OK);
    CHECK(stats.copy_count == 0 && stats.unique_count == 0);
    CHECK(es2_vfs_open_dbsim_order(NULL, 0, 0, NULL, 0, &vfs, NULL) == ES2_OK);
}

static void errors_and_capacity(void)
{
    uint8_t bytes[64]; es2_vol vol;
    es2_vfs_source sources[31];
    size_t order[31], i;
    es2_vfs unchanged = {0}, vfs = {0};
    es2_error error;
    fixture(bytes, 1, 0, 1, &vol);
    for (i = 0; i < 31; ++i)
        sources[i] = (es2_vfs_source){.kind = ES2_VFS_VOL, .data.vol = &vol};
    CHECK(es2_vfs_open_dbsim_order(sources, 1, 1, order, 0, &vfs, &error) == ES2_ERR_CAPACITY);
    CHECK(memcmp(&vfs, &unchanged, sizeof vfs) == 0 && error.offset == 1);
    CHECK(es2_vfs_open_dbsim_order(NULL, 1, 1, order, 31, &vfs, NULL) == ES2_ERR_ARGUMENT);
    CHECK(es2_vfs_open_dbsim_order(sources, 1, 1, NULL, 31, &vfs, NULL) == ES2_ERR_ARGUMENT);
    CHECK(es2_vfs_open_dbsim_order(sources, 1, 1, order, 31, NULL, NULL) == ES2_ERR_ARGUMENT);
    CHECK(es2_vfs_open_dbsim_order(sources, SIZE_MAX, 1, order, SIZE_MAX, &vfs, NULL) == ES2_ERR_COUNT);
    CHECK(es2_vfs_open_dbsim_order(sources, 31, 1, order, 31, &vfs, &error) == ES2_ERR_CAPACITY);
    CHECK(error.offset == 30 && memcmp(&vfs, &unchanged, sizeof vfs) == 0);
    CHECK(es2_vfs_open_dbsim_order(sources, 30, 1, order, 31, &vfs, NULL) == ES2_OK);
    CHECK(vfs.order_count == 30 && vfs.file_count == 30);
    for (i = 0; i < 30; ++i) CHECK(order[i] == i);
    CHECK(es2_vfs_open_dbsim_order(sources, 31, 0, order, 31, &vfs, NULL) == ES2_OK);
    CHECK(vfs.order_count == 0);
    unchanged = vfs; sources[0].data.vol = NULL;
    CHECK(es2_vfs_open_dbsim_order(sources, 1, 0, order, 31, &vfs, NULL) == ES2_ERR_ARGUMENT);
    CHECK(memcmp(&vfs, &unchanged, sizeof vfs) == 0);
}

/* Synthetic archive for es2_vfs_dbsim_member: directory strings and
 * (name, directory) members with one-byte payloads packed after the table. */
typedef struct member_spec { const char *name; uint8_t directory; } member_spec;
static void build_archive(uint8_t *out, size_t capacity, const char *const *dirs, size_t dir_count,
                          const member_spec *members, size_t member_count, es2_vol *vol)
{
    size_t at = 12, i, table, blocks;
    memset(out, 0, capacity);
    memcpy(out, "VOLN", 4); put32(out + 4, 1); out[8] = 5; out[9] = (uint8_t)dir_count;
    for (i = 0; i < dir_count; ++i) { size_t n = strlen(dirs[i]) + 1; memcpy(out + at, dirs[i], n); at += n; }
    put16(out + 10, (uint16_t)(at - 12));
    put16(out + at, (uint16_t)member_count); put32(out + at + 2, (uint32_t)(18 * member_count));
    table = at + 6; blocks = table + 18 * member_count;
    for (i = 0; i < member_count; ++i) {
        uint8_t *slot = out + table + 18 * i;
        memcpy(slot, members[i].name, strlen(members[i].name));
        slot[13] = members[i].directory; put32(slot + 14, (uint32_t)blocks);
        out[blocks] = 2; put32(out + blocks + 1, 1); out[blocks + 9] = (uint8_t)(0x40 + i);
        out[blocks + 10] = (uint8_t)(0x40 + i); blocks += 11;
    }
    CHECK(blocks <= capacity);
    CHECK(es2_vol_open((es2_span){out, blocks}, vol, NULL) == ES2_OK);
}

static void dbsim_member(void)
{
    static uint8_t bytes[1024];
    static char long_path[400];
    const char *const dirs[] = {"dat\\", "DAT\\", "C:\\", "a\\b\\", "[x\\"};
    const member_spec members[] = {
        {"ONE.BIN", 0}, {"TWO.BIN", 1}, {"A.B.C", 0}, {"X.BIN", 2}, {"Z", 3}, {"Q", 4}, {"NOEXT", 0}
    };
    es2_vol vol;
    es2_error error;
    size_t index = 99, i;
    build_archive(bytes, sizeof bytes, dirs, 5, members, 7, &vol);
    CHECK(es2_vfs_dbsim_member(&vol, "dat\\one.bin", &index, NULL) == ES2_OK && index == 0);
    CHECK(es2_vfs_dbsim_member(&vol, "DaT\\OnE.BiN", &index, NULL) == ES2_OK && index == 0);
    /* The first equal directory wins: TWO.BIN under the second "DAT\" is unreachable. */
    CHECK(es2_vfs_dbsim_member(&vol, "DAT\\TWO.BIN", &index, NULL) == ES2_ERR_NOT_FOUND);
    /* '/' is not a separator: no directory part, directory index 0xff. */
    CHECK(es2_vfs_dbsim_member(&vol, "dat/one.bin", &index, NULL) == ES2_ERR_NOT_FOUND);
    CHECK(es2_vfs_dbsim_member(&vol, "one.bin", &index, NULL) == ES2_ERR_NOT_FOUND);
    CHECK(es2_vfs_dbsim_member(&vol, "", &index, NULL) == ES2_ERR_NOT_FOUND);
    CHECK(es2_vfs_dbsim_member(&vol, "dat\\a.b.c", &index, NULL) == ES2_OK && index == 2);
    CHECK(es2_vfs_dbsim_member(&vol, "dat\\noext", &index, NULL) == ES2_OK && index == 6);
    CHECK(es2_vfs_dbsim_member(&vol, "dat\\noext.", &index, NULL) == ES2_ERR_NOT_FOUND);
    /* Drive: the two bytes stay in the directory part. */
    CHECK(es2_vfs_dbsim_member(&vol, "c:\\x.bin", &index, NULL) == ES2_OK && index == 3);
    CHECK(es2_vfs_dbsim_member(&vol, "C:x.bin", &index, NULL) == ES2_ERR_NOT_FOUND);
    CHECK(es2_vfs_dbsim_member(&vol, "A\\B\\z", &index, NULL) == ES2_OK && index == 4);
    CHECK(es2_vfs_dbsim_member(&vol, "a\\b\\c\\z", &index, NULL) == ES2_ERR_NOT_FOUND);
    /* Only 'a'..'z' fold: '{' is not '['. */
    CHECK(es2_vfs_dbsim_member(&vol, "[X\\q", &index, NULL) == ES2_OK && index == 5);
    CHECK(es2_vfs_dbsim_member(&vol, "{x\\Q", &index, NULL) == ES2_ERR_NOT_FOUND);
    CHECK(es2_vfs_dbsim_member(&vol, "dat\\ONE.BIN\xe4", &index, NULL) == ES2_ERR_NOT_FOUND);
    /* 255 bytes are looked up, 256 are refused. */
    for (i = 0; i < 255; ++i) long_path[i] = 'k';
    long_path[255] = 0;
    CHECK(es2_vfs_dbsim_member(&vol, long_path, &index, NULL) == ES2_ERR_NOT_FOUND);
    long_path[255] = 'k'; long_path[256] = 0;
    CHECK(es2_vfs_dbsim_member(&vol, long_path, &index, &error) == ES2_ERR_LENGTH && error.offset == 256);
    CHECK(es2_vfs_dbsim_member(NULL, "dat\\one.bin", &index, NULL) == ES2_ERR_ARGUMENT);
    CHECK(es2_vfs_dbsim_member(&vol, NULL, &index, NULL) == ES2_ERR_ARGUMENT);
    CHECK(es2_vfs_dbsim_member(&vol, "dat\\one.bin", NULL, NULL) == ES2_ERR_ARGUMENT);
}

int main(void)
{
    priority_and_ties(); masks_loose_and_paths(); errors_and_capacity(); dbsim_member();
    printf("VFS DBSIM policy: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
