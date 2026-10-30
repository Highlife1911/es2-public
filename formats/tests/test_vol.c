#include "es2/formats/vol.h"
#include "test_support.h"
#include <string.h>

/* Original synthetic design: two tiny text-named blobs, not CD header bytes. */
static size_t fixture(uint8_t *b)
{
    memset(b, 0, 128);
    memcpy(b, "VOLN", 4); b[4] = 7; b[5] = 8; b[8] = 42; b[9] = 2;
    test_put16(b + 6, 0x3232); test_put16(b + 10, 6);
    memcpy(b + 12, "A\\\0B\\\0", 6);
    test_put16(b + 18, 2); test_put32(b + 20, 36);
    memcpy(b + 24, "ONE.BIN", 8); b[36] = 0xe1; b[37] = 0;
    test_put32(b + 38, 60);
    memcpy(b + 42, "TWO.DAT", 8); b[55] = 1; test_put32(b + 56, 73);
    b[60] = 2; test_put32(b + 61, 3); test_put32(b + 65, 0x123);
    b[69] = 17; b[70] = 18; b[71] = 19; b[72] = 19;
    b[73] = 2; test_put32(b + 74, 2); b[82] = 20; b[83] = 21; b[84] = 21;
    return 85;
}

int test_vol(void)
{
    uint8_t b[128], swap[18];
    size_t n = fixture(b), i;
    es2_vol v, unchanged;
    es2_vol_member m, table[2];
    es2_vol_directory dirs[2];
    es2_span span;
    es2_error error;
    CHECK(es2_vol_open((es2_span){b, n}, &v, &error) == ES2_OK);
    CHECK(v.directory_count == 2 && v.member_count == 2 && v.blocks_offset == 60);
    CHECK(v.flag0 == 7 && v.flag1 == 8 && v.reserved == 0x3232 && v.priority == 42);
    CHECK(es2_vol_read_tables(&v, dirs, 2, table, 2, &error) == ES2_OK);
    CHECK(dirs[0].name.size == 2 && table[0].length == 3 && table[0].metadata == 0x123);
    CHECK(table[1].block_offset == 73 && table[1].payload_offset == 82);
    CHECK(es2_vol_find(&v, "a/one.bin", &m, &error) == ES2_OK && m.payload.data[2] == 19);
    CHECK(es2_vol_find(&v, "B\\tWo.DaT", &m, &error) == ES2_OK && m.length == 2);
    CHECK(es2_vol_find(&v, "ONE.BIN", &m, &error) == ES2_ERR_NOT_FOUND);
    CHECK(es2_vol_find(&v, "a/one.binx", &m, &error) == ES2_ERR_NOT_FOUND);
    CHECK(es2_vol_payload(&v, 0, &span, &error) == ES2_OK && span.size == 3);
    CHECK(es2_vol_payload(&v, 2, &span, &error) == ES2_ERR_OFFSET);
    CHECK(es2_vol_payload(&v, 0, NULL, &error) == ES2_ERR_ARGUMENT);
    CHECK(es2_vol_read_tables(&v, dirs, 1, table, 2, &error) == ES2_ERR_CAPACITY);
    CHECK(es2_vol_read_tables(&v, NULL, 2, table, 2, &error) == ES2_ERR_ARGUMENT);
    CHECK(es2_vol_read_tables(&v, dirs, 2, NULL, 2, &error) == ES2_ERR_ARGUMENT);
    CHECK(es2_vol_read_tables(NULL, dirs, 2, table, 2, &error) == ES2_ERR_ARGUMENT);
    CHECK(es2_vol_member_at(&v, SIZE_MAX, &m, &error) == ES2_ERR_OFFSET);
    CHECK(es2_vol_member_at(NULL, 0, &m, &error) == ES2_ERR_ARGUMENT);
    CHECK(es2_vol_member_at(&v, 0, NULL, &error) == ES2_ERR_ARGUMENT);
    CHECK(es2_vol_directory_at(&v, 2, &span, &error) == ES2_ERR_OFFSET);
    CHECK(es2_vol_directory_at(NULL, 0, &span, &error) == ES2_ERR_ARGUMENT);
    CHECK(es2_vol_directory_at(&v, 0, NULL, &error) == ES2_ERR_ARGUMENT);
    CHECK(es2_vol_find(NULL, "A/ONE.BIN", &m, &error) == ES2_ERR_ARGUMENT);
    CHECK(es2_vol_find(&v, NULL, &m, &error) == ES2_ERR_ARGUMENT);
    CHECK(es2_vol_find(&v, "A/ONE.BIN", NULL, &error) == ES2_ERR_ARGUMENT);
    CHECK(es2_vol_open((es2_span){b, n}, NULL, &error) == ES2_ERR_ARGUMENT);
    CHECK(es2_vol_open((es2_span){NULL, 1}, &v, &error) == ES2_ERR_ARGUMENT);
    (void)es2_vol_open((es2_span){b, n}, &v, &error); unchanged = v;
    for (i = 0; i < n; ++i) {
        CHECK(es2_vol_open((es2_span){b, i}, &v, &error) != ES2_OK);
        CHECK(v.input.data == unchanged.input.data && v.input.size == unchanged.input.size);
    }
    /* Physical block order need not match member-table order. */
    memcpy(swap, b + 24, 18); memcpy(b + 24, b + 42, 18); memcpy(b + 42, swap, 18);
    CHECK(es2_vol_open((es2_span){b, n}, &v, &error) == ES2_OK);
    CHECK(es2_vol_member_at(&v, 0, &m, &error) == ES2_OK && m.block_offset == 73);
    (void)fixture(b); b[55] = 0; /* Unused B directory remains valid. */
    CHECK(es2_vol_open((es2_span){b, n}, &v, &error) == ES2_OK);
    (void)fixture(b); b[0] = '!';
    CHECK(es2_vol_open((es2_span){b, n}, &v, &error) == ES2_ERR_MAGIC);
    (void)fixture(b); b[14] = '!';
    CHECK(es2_vol_open((es2_span){b, n}, &v, &error) == ES2_ERR_STRING);
    (void)fixture(b); b[12] = 0;
    CHECK(es2_vol_open((es2_span){b, n}, &v, &error) == ES2_ERR_STRING);
    (void)fixture(b); b[13] = '/';
    CHECK(es2_vol_open((es2_span){b, n}, &v, &error) == ES2_ERR_STRING);
    (void)fixture(b); b[12] = 0xff;
    CHECK(es2_vol_open((es2_span){b, n}, &v, &error) == ES2_ERR_STRING);
    (void)fixture(b); b[17] = '!';
    CHECK(es2_vol_open((es2_span){b, n}, &v, &error) == ES2_ERR_STRING);
    (void)fixture(b); b[9] = 1;
    CHECK(es2_vol_open((es2_span){b, n}, &v, &error) == ES2_ERR_COUNT);
    (void)fixture(b); test_put32(b + 20, 37);
    CHECK(es2_vol_open((es2_span){b, n}, &v, &error) == ES2_ERR_LENGTH);
    (void)fixture(b); memset(b + 24, 'X', 13);
    CHECK(es2_vol_open((es2_span){b, n}, &v, &error) == ES2_ERR_STRING);
    (void)fixture(b); b[24] = 0;
    CHECK(es2_vol_open((es2_span){b, n}, &v, &error) == ES2_ERR_STRING);
    (void)fixture(b); b[25] = '\\';
    CHECK(es2_vol_open((es2_span){b, n}, &v, &error) == ES2_ERR_STRING);
    (void)fixture(b); b[37] = 2;
    CHECK(es2_vol_open((es2_span){b, n}, &v, &error) == ES2_ERR_OFFSET);
    (void)fixture(b); test_put32(b + 38, UINT32_MAX);
    CHECK(es2_vol_open((es2_span){b, n}, &v, &error) == ES2_ERR_OFFSET);
    (void)fixture(b); test_put32(b + 38, 20);
    CHECK(es2_vol_open((es2_span){b, n}, &v, &error) == ES2_ERR_OVERLAP);
    (void)fixture(b); test_put32(b + 56, 60);
    CHECK(es2_vol_open((es2_span){b, n}, &v, &error) == ES2_ERR_OVERLAP);
    (void)fixture(b); memcpy(b + 42, b + 24, 13); b[55] = 0;
    CHECK(es2_vol_open((es2_span){b, n}, &v, &error) == ES2_ERR_STRING);
    (void)fixture(b); memcpy(b + 42, "one.bin", 8); b[15] = 'a';
    CHECK(es2_vol_open((es2_span){b, n}, &v, &error) == ES2_ERR_STRING);
    (void)fixture(b); b[60] = 9;
    CHECK(es2_vol_open((es2_span){b, n}, &v, &error) == ES2_ERR_UNSUPPORTED);
    (void)fixture(b); test_put32(b + 61, 0);
    CHECK(es2_vol_open((es2_span){b, n}, &v, &error) == ES2_ERR_LENGTH);
    (void)fixture(b); test_put32(b + 61, UINT32_MAX);
    CHECK(es2_vol_open((es2_span){b, n}, &v, &error) == ES2_ERR_TRUNCATED);
    (void)fixture(b); b[84] ^= 1;
    CHECK(es2_vol_open((es2_span){b, n}, &v, &error) == ES2_ERR_LENGTH);
    /* The tables alone: no payload or suffix byte is read; member_at still checks the suffix. */
    (void)fixture(b); CHECK(es2_vol_open((es2_span){b, n}, &unchanged, &error) == ES2_OK);
    memset(b + 69, 0xde, 3); b[72] = 0xad; memset(b + 82, 0xde, 2); b[84] = 0xad;
    CHECK(es2_vol_open((es2_span){b, n}, &v, &error) == ES2_ERR_LENGTH);
    CHECK(es2_vol_open_tables((es2_span){b, n}, &v, &error) == ES2_OK);
    CHECK(v.member_count == 2 && v.table_offset == unchanged.table_offset && v.blocks_offset == 60);
    CHECK(es2_vol_member_at(&v, 0, &m, &error) == ES2_ERR_LENGTH);
    for (i = 0; i < n; ++i) CHECK(es2_vol_open_tables((es2_span){b, i}, &v, &error) != ES2_OK);
    (void)fixture(b); test_put32(b + 56, 60);
    CHECK(es2_vol_open_tables((es2_span){b, n}, &v, &error) == ES2_ERR_OVERLAP);
    (void)fixture(b);
    CHECK(es2_vol_open_tables((es2_span){b, n + 1}, &v, &error) == ES2_ERR_LENGTH);
    CHECK(es2_vol_open_tables((es2_span){b, n}, NULL, &error) == ES2_ERR_ARGUMENT);
    (void)fixture(b);
    CHECK(es2_vol_open((es2_span){b, n + 1}, &v, &error) == ES2_ERR_LENGTH);
    /* Gap before all blocks, keeping every block internally valid. */
    memmove(b + 61, b + 60, 25); test_put32(b + 38, 61); test_put32(b + 56, 74);
    CHECK(es2_vol_open((es2_span){b, n + 1}, &v, &error) == ES2_ERR_LENGTH);
    return 0;
}
