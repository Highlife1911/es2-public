#include "es2/formats/common.h"
#include "test_support.h"
#include <string.h>

int test_common(void)
{
    uint8_t b[16] = {0};
    es2_span input = {b, sizeof b}, part = {NULL, 0};
    es2_error error;
    es2_reader r;
    uint8_t u8 = 99;
    uint16_t u16 = 99;
    uint32_t u32 = 99;
    int16_t i16 = 99;
    int32_t i32 = 99;
    es2_status s;
    size_t i;
    b[0] = 0x72; test_put16(b + 1, 0x1234); test_put32(b + 3, UINT32_C(0x89abcdef));
    test_put16(b + 7, UINT16_C(0x8000)); test_put32(b + 9, UINT32_C(0x80000000));
    CHECK(es2_reader_init(&r, input, &error) == ES2_OK);
    CHECK(es2_read_u8(&r, &u8) == ES2_OK && u8 == 0x72);
    CHECK(es2_read_u16(&r, &u16) == ES2_OK && u16 == 0x1234);
    CHECK(es2_read_u32(&r, &u32) == ES2_OK && u32 == UINT32_C(0x89abcdef));
    CHECK(es2_read_i16(&r, &i16) == ES2_OK && i16 == INT16_MIN);
    CHECK(es2_read_i32(&r, &i32) == ES2_OK && i32 == INT32_MIN);
    CHECK(es2_remaining(&r) == 3);
    CHECK(es2_skip(&r, 3) == ES2_OK && es2_reader_end(&r) == ES2_OK);
    CHECK(es2_read_u8(&r, &u8) == ES2_ERR_TRUNCATED && r.offset == 16 && u8 == 0x72);
    CHECK(es2_read_u16(&r, &u16) == ES2_ERR_TRUNCATED && u16 == 0x1234);
    CHECK(error.code == ES2_ERR_TRUNCATED && error.offset == 16);
    CHECK(es2_skip(&r, 0) == ES2_ERR_TRUNCATED);
    CHECK(es2_reader_end(&r) == ES2_ERR_TRUNCATED);
    CHECK(es2_reader_init(&r, (es2_span){NULL, 0}, &error) == ES2_OK);
    CHECK(es2_read_span(&r, 0, &part) == ES2_OK && part.data == NULL && part.size == 0);
    CHECK(es2_reader_end(&r) == ES2_OK);
    CHECK(es2_reader_init(&r, (es2_span){NULL, 1}, &error) == ES2_ERR_ARGUMENT);
    CHECK(es2_reader_init(NULL, input, &error) == ES2_ERR_ARGUMENT);
    CHECK(es2_read_span(NULL, 0, &part) == ES2_ERR_ARGUMENT);
    CHECK(es2_read_u8(NULL, &u8) == ES2_ERR_ARGUMENT);
    CHECK(es2_read_u16(NULL, NULL) == ES2_ERR_ARGUMENT);
    CHECK(es2_reader_end(NULL) == ES2_ERR_ARGUMENT);
    CHECK(es2_remaining(NULL) == 0);
    CHECK(es2_subspan(input, SIZE_MAX, 0, &part, &error) == ES2_ERR_OFFSET);
    CHECK(es2_subspan(input, 1, SIZE_MAX, &part, &error) == ES2_ERR_TRUNCATED);
    CHECK(es2_subspan(input, 0, 0, NULL, &error) == ES2_ERR_ARGUMENT);
    CHECK(es2_subspan((es2_span){NULL, 1}, 0, 0, &part, &error) == ES2_ERR_ARGUMENT);
    CHECK(es2_subspan(input, 16, 0, &part, &error) == ES2_OK && part.data == b + 16);
    for (i = 0; i < 4; ++i) {
        CHECK(es2_reader_init(&r, (es2_span){b, i}, NULL) == ES2_OK);
        u32 = 123;
        CHECK(es2_read_u32(&r, &u32) == ES2_ERR_TRUNCATED && r.offset == 0 && u32 == 123);
    }
    for (i = 0; i < 2; ++i) {
        CHECK(es2_reader_init(&r, (es2_span){b, i}, NULL) == ES2_OK);
        i16 = 42;
        CHECK(es2_read_i16(&r, &i16) == ES2_ERR_TRUNCATED && i16 == 42);
    }
    for (i = 0; i < 6; ++i) {
        (void)es2_reader_init(&r, input, &error);
        switch (i) {
        case 0: s = es2_read_u8(&r, NULL); break;
        case 1: s = es2_read_u16(&r, NULL); break;
        case 2: s = es2_read_u32(&r, NULL); break;
        case 3: s = es2_read_i16(&r, NULL); break;
        case 4: s = es2_read_i32(&r, NULL); break;
        default: s = es2_read_span(&r, 0, NULL); break;
        }
        CHECK(s == ES2_ERR_ARGUMENT && r.offset == 0 && r.status == s);
    }
    for (i = 0; i < 4; ++i) {
        static const int32_t values[] = {0, INT32_MAX, -1, INT32_MIN};
        test_put32(b, (uint32_t)values[i]);
        (void)es2_reader_init(&r, (es2_span){b, 4}, &error);
        CHECK(es2_read_i32(&r, &i32) == ES2_OK && i32 == values[i]);
    }
    for (i = 0; i < 4; ++i) {
        static const int16_t values[] = {0, INT16_MAX, -1, INT16_MIN};
        test_put16(b, (uint16_t)values[i]);
        (void)es2_reader_init(&r, (es2_span){b, 2}, &error);
        CHECK(es2_read_i16(&r, &i16) == ES2_OK && i16 == values[i]);
    }
    (void)es2_reader_init(&r, input, &error);
    CHECK(es2_reader_end(&r) == ES2_ERR_LENGTH && error.offset == 0);
    r.offset = SIZE_MAX;
    CHECK(es2_remaining(&r) == 0);
    for (i = 0; i <= ES2_ERR_NOT_FOUND; ++i) {
        CHECK(es2_fail(&error, (es2_status)i, 7) == (es2_status)i);
        CHECK(error.offset == 7 && error.message != NULL && strlen(error.message) > 0);
    }
    CHECK(strcmp(es2_status_string((es2_status)999), "unknown error") == 0);
    es2_error_clear(&error); es2_error_clear(NULL);
    CHECK(error.code == ES2_OK && error.offset == 0);
    return 0;
}
