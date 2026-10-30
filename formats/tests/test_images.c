#include "test_support.h"
#include "es2/formats/dpl.h"
#include "es2/formats/dbm.h"

#include <string.h>

static size_t palette_fixture(uint8_t *bytes)
{
    size_t i;
    memset(bytes, 0, 64);
    test_put32(bytes, UINT32_C(0x0028000f));
    test_put32(bytes + 4, 28);
    test_put16(bytes + 8, 3);
    test_put16(bytes + 10, 19); /* Unknown field must be preserved. */
    for (i = 0; i < 3; ++i) {
        bytes[12 + i * 4] = (uint8_t)(7 + i);
        bytes[13 + i * 4] = (uint8_t)(11 + i);
        bytes[14 + i * 4] = (uint8_t)(17 + i);
        bytes[15 + i * 4] = (uint8_t)(i + 1);
    }
    test_put16(bytes + 24, 2);
    test_put16(bytes + 26, 7);
    test_put16(bytes + 28, 2);
    test_put16(bytes + 30, 2);
    test_put16(bytes + 32, 0); /* Descending is valid. */
    test_put16(bytes + 34, 0); /* Empty ramp has no undocumented rejection. */
    return 36;
}

static size_t bitmap_fixture(uint8_t *bytes, uint16_t width, uint16_t height,
                             uint8_t compression, const uint8_t *pixels,
                             size_t pixel_size, uint16_t colours)
{
    size_t body_size = 13 + pixel_size + (size_t)colours * 4;
    size_t size = 8 + body_size + (body_size & 1u);
    size_t i;
    memset(bytes, 0, size);
    test_put32(bytes, UINT32_C(0x0028000e));
    test_put32(bytes + 4, (uint32_t)body_size);
    test_put16(bytes + 8, height);
    test_put16(bytes + 10, width);
    bytes[12] = 8;
    bytes[14] = compression;
    test_put32(bytes + 15, (uint32_t)pixel_size);
    test_put16(bytes + 19, colours);
    if (pixel_size != 0) memcpy(bytes + 21, pixels, pixel_size);
    for (i = 0; i < (size_t)colours * 4; ++i)
        bytes[21 + pixel_size + i] = (uint8_t)(i + 1);
    return size;
}

static void test_palettes(void)
{
    uint8_t bytes[64], altered[64];
    size_t length = palette_fixture(bytes), i;
    es2_span input = {bytes, length};
    es2_dpl palette;
    es2_dpl_colour colour;
    es2_dpl_ramp ramp;
    es2_error error;
    CHECK(es2_dpl_parse(input, &palette, &error) == ES2_OK);
    CHECK(palette.colour_count == 3 && palette.reserved_word == 19);
    CHECK(palette.ramp_count == 2 && palette.ramp_base == 7);
    CHECK(palette.colours.data == bytes + 12 && palette.colours.size == 12);
    CHECK(palette.ramps.data == bytes + 28 && palette.ramps.size == 8);
    CHECK(es2_dpl_colour_at(&palette, 1, &colour, &error) == ES2_OK);
    CHECK(colour.red == 8 && colour.green == 12 && colour.blue == 18 && colour.flags == 2);
    CHECK(es2_dpl_ramp_at(&palette, 0, &ramp, &error) == ES2_OK);
    CHECK(ramp.count == 2 && ramp.indices.size == 4 && ramp.indices.data[0] == 2);
    CHECK(es2_dpl_ramp_at(&palette, 1, &ramp, &error) == ES2_OK);
    CHECK(ramp.count == 0 && ramp.indices.size == 0);
    CHECK(es2_dpl_colour_at(&palette, 3, &colour, &error) == ES2_ERR_OFFSET);
    CHECK(es2_dpl_ramp_at(&palette, 2, &ramp, &error) == ES2_ERR_OFFSET);
    CHECK(es2_dpl_parse(input, NULL, &error) == ES2_ERR_ARGUMENT);
    CHECK(es2_dpl_colour_at(NULL, 0, &colour, &error) == ES2_ERR_ARGUMENT);
    CHECK(es2_dpl_colour_at(&palette, 0, NULL, &error) == ES2_ERR_ARGUMENT);
    CHECK(es2_dpl_ramp_at(NULL, 0, &ramp, &error) == ES2_ERR_ARGUMENT);
    CHECK(es2_dpl_ramp_at(&palette, 0, NULL, &error) == ES2_ERR_ARGUMENT);
    for (i = 0; i < length; ++i) {
        es2_span short_input = {bytes, i};
        CHECK(es2_dpl_parse(short_input, &palette, &error) != ES2_OK);
    }
    memcpy(altered, bytes, length); altered[0] ^= 1;
    CHECK(es2_dpl_parse((es2_span){altered, length}, &palette, &error) == ES2_ERR_MAGIC);
    memcpy(altered, bytes, length); test_put32(altered + 4, 27);
    CHECK(es2_dpl_parse((es2_span){altered, length}, &palette, &error) == ES2_ERR_LENGTH);
    memcpy(altered, bytes, length); test_put16(altered + 8, UINT16_MAX);
    CHECK(es2_dpl_parse((es2_span){altered, length}, &palette, &error) == ES2_ERR_TRUNCATED);
    memcpy(altered, bytes, length); test_put16(altered + 28, UINT16_MAX);
    CHECK(es2_dpl_parse((es2_span){altered, length}, &palette, &error) == ES2_ERR_TRUNCATED);
    memcpy(altered, bytes, length); test_put16(altered + 30, 3);
    CHECK(es2_dpl_parse((es2_span){altered, length}, &palette, &error) == ES2_ERR_OFFSET);
    CHECK(error.offset == 30);
    memcpy(altered, bytes, length); test_put16(altered + 24, 1);
    CHECK(es2_dpl_parse((es2_span){altered, length}, &palette, &error) == ES2_ERR_LENGTH);
    /* Accessors check their borrowed spans too, even for damaged descriptors. */
    CHECK(es2_dpl_parse(input, &palette, &error) == ES2_OK);
    palette.colours.size = 0;
    CHECK(es2_dpl_colour_at(&palette, 0, &colour, &error) == ES2_ERR_TRUNCATED);
    palette.ramps.size = 1;
    CHECK(es2_dpl_ramp_at(&palette, 0, &ramp, &error) == ES2_ERR_TRUNCATED);
    palette.ramps.size = 3;
    CHECK(es2_dpl_ramp_at(&palette, 0, &ramp, &error) == ES2_ERR_TRUNCATED);
    /* A palette with no colours and no ramps has complete structural framing. */
    memset(altered, 0, 16);
    test_put32(altered, UINT32_C(0x0028000f)); test_put32(altered + 4, 8);
    CHECK(es2_dpl_parse((es2_span){altered, 16}, &palette, NULL) == ES2_OK);
}

static void test_bitmaps(void)
{
    const uint8_t pixels[] = {0, 3, 8, 4, 2, 9};
    uint8_t bytes[96], altered[96], output[8];
    size_t length = bitmap_fixture(bytes, 3, 2, 0, pixels, sizeof pixels, 1);
    size_t i, written, consumed;
    es2_dbm image;
    es2_error error;
    CHECK(es2_dbm_parse((es2_span){bytes, length}, &image, &error) == ES2_OK);
    CHECK(image.width == 3 && image.height == 2 && image.serialized_size == length);
    CHECK(image.colour_count == 1 && image.colours.size == 4 && image.pixels.size == 6);
    memset(output, 77, sizeof output);
    CHECK(es2_dbm_decode(&image, output, 6, &written, &consumed, &error) == ES2_OK);
    CHECK(written == 6 && consumed == 6 && memcmp(output, pixels, 6) == 0);
    CHECK(output[6] == 77 && output[7] == 77);
    CHECK(es2_dbm_decode(&image, output, 5, &written, &consumed, &error) == ES2_ERR_CAPACITY);
    CHECK(written == 0 && consumed == 0);
    CHECK(es2_dbm_decode(&image, NULL, 6, NULL, NULL, &error) == ES2_ERR_ARGUMENT);
    CHECK(es2_dbm_decode(NULL, output, 6, NULL, NULL, &error) == ES2_ERR_ARGUMENT);
    CHECK(es2_dbm_parse((es2_span){bytes, length}, NULL, &error) == ES2_ERR_ARGUMENT);
    for (i = 0; i < length; ++i)
        CHECK(es2_dbm_parse((es2_span){bytes, i}, &image, &error) != ES2_OK);
    memcpy(altered, bytes, length); altered[0] ^= 1;
    CHECK(es2_dbm_parse((es2_span){altered, length}, &image, &error) == ES2_ERR_MAGIC);
    memcpy(altered, bytes, length); test_put32(altered + 4, 12);
    CHECK(es2_dbm_parse((es2_span){altered, length}, &image, &error) == ES2_ERR_LENGTH);
    memcpy(altered, bytes, length); test_put32(altered + 15, UINT32_MAX);
    CHECK(es2_dbm_parse((es2_span){altered, length}, &image, &error) == ES2_ERR_LENGTH);
    memcpy(altered, bytes, length); test_put16(altered + 19, 2);
    CHECK(es2_dbm_parse((es2_span){altered, length}, &image, &error) == ES2_ERR_LENGTH);
    memcpy(altered, bytes, length); test_put16(altered + 10, 2);
    CHECK(es2_dbm_parse((es2_span){altered, length}, &image, &error) == ES2_ERR_LENGTH);
    memcpy(altered, bytes, length); altered[length - 1] = 1;
    CHECK(es2_dbm_parse((es2_span){altered, length}, &image, &error) == ES2_ERR_LENGTH);
    CHECK(error.offset == length - 1);
    bytes[length] = 11;
    CHECK(es2_dbm_parse((es2_span){bytes, length + 1}, &image, &error) == ES2_ERR_LENGTH);
    /* Header-only support retains unknown depths/layouts and stream kinds. */
    memcpy(altered, bytes, length); altered[12] = 4;
    CHECK(es2_dbm_parse((es2_span){altered, length}, &image, &error) == ES2_OK);
    CHECK(es2_dbm_decode(&image, output, 8, NULL, NULL, &error) == ES2_ERR_UNSUPPORTED);
    altered[12] = 8; altered[13] = 1;
    CHECK(es2_dbm_parse((es2_span){altered, length}, &image, &error) == ES2_OK);
    CHECK(es2_dbm_decode(&image, output, 8, NULL, NULL, &error) == ES2_ERR_UNSUPPORTED);
    altered[13] = 16; altered[14] = 3;
    CHECK(es2_dbm_parse((es2_span){altered, length}, &image, &error) == ES2_OK);
    CHECK(es2_dbm_decode(&image, output, 8, NULL, NULL, &error) == ES2_ERR_UNSUPPORTED);
    altered[14] = 2;
    CHECK(es2_dbm_parse((es2_span){altered, length}, &image, &error) == ES2_OK);
    CHECK(es2_dbm_decode(&image, output, 8, NULL, NULL, &error) == ES2_ERR_UNSUPPORTED);
    altered[14] = 0;
    CHECK(es2_dbm_parse((es2_span){altered, length}, &image, &error) == ES2_OK);
    CHECK(es2_dbm_decode(&image, output, 8, NULL, NULL, &error) == ES2_OK);
    image.pixels.size = 5;
    CHECK(es2_dbm_decode(&image, output, 8, NULL, NULL, &error) == ES2_ERR_LENGTH);
    length = bitmap_fixture(bytes, 1, 1, 1, pixels, 3, 0);
    CHECK(es2_dbm_parse((es2_span){bytes, length}, &image, &error) == ES2_ERR_TRUNCATED);
    /* Even body requires no alignment; empty raw images stay representable. */
    length = bitmap_fixture(bytes, 1, 1, 0, pixels, 1, 0);
    CHECK(es2_dbm_parse((es2_span){bytes, length}, &image, &error) == ES2_OK);
    length = bitmap_fixture(bytes, 0, 0, 0, NULL, 0, 0);
    CHECK(es2_dbm_parse((es2_span){bytes, length}, &image, &error) == ES2_OK);
    CHECK(es2_dbm_decode(&image, NULL, 0, &written, &consumed, &error) == ES2_OK);
    CHECK(written == 0 && consumed == 0);
}

static void test_rle(void)
{
    uint8_t bytes[96], stream[32], output[12];
    const uint8_t expected[] = {11, 12, 23, 23, 23, 31};
    size_t length, written, consumed, i;
    es2_dbm image;
    es2_error error;
    memset(stream, 0, sizeof stream);
    test_put32(stream, 6);
    /* Zero literal and zero repeat are not terminators. */
    stream[4] = 0; stream[5] = 128; stream[6] = 99;
    stream[7] = 2; stream[8] = 11; stream[9] = 12;
    stream[10] = 131; stream[11] = 23;
    stream[12] = 1; stream[13] = 31; stream[14] = 88;
    length = bitmap_fixture(bytes, 3, 2, 1, stream, 15, 0);
    CHECK(es2_dbm_parse((es2_span){bytes, length}, &image, &error) == ES2_OK);
    memset(output, 77, sizeof output);
    CHECK(es2_dbm_decode(&image, output, sizeof output, &written, &consumed, &error) == ES2_OK);
    CHECK(written == 6 && consumed == 14 && memcmp(output, expected, 6) == 0);
    CHECK(output[6] == 77);
    /* Every shortened stream is rejected unless all requested pixels exist. */
    for (i = 0; i < 14; ++i) {
        image.pixels.size = i;
        CHECK(es2_dbm_decode(&image, output, sizeof output, &written, &consumed, &error) == ES2_ERR_TRUNCATED);
        CHECK(written == 0 && consumed == 0);
    }
    image.pixels.size = 15;
    bytes[21] = 5;
    CHECK(es2_dbm_decode(&image, output, sizeof output, NULL, NULL, &error) == ES2_ERR_LENGTH);
    /* Stop midway through repeat and literal runs, preserving consumed count. */
    test_put32(stream, 2); stream[4] = 133; stream[5] = 42;
    length = bitmap_fixture(bytes, 2, 1, 1, stream, 6, 0);
    CHECK(es2_dbm_parse((es2_span){bytes, length}, &image, &error) == ES2_OK);
    CHECK(es2_dbm_decode(&image, output, sizeof output, &written, &consumed, &error) == ES2_OK);
    CHECK(written == 2 && consumed == 6 && output[0] == 42 && output[1] == 42);
    stream[4] = 5; stream[5] = 41; stream[6] = 43;
    length = bitmap_fixture(bytes, 2, 1, 1, stream, 7, 0);
    CHECK(es2_dbm_parse((es2_span){bytes, length}, &image, &error) == ES2_OK);
    CHECK(es2_dbm_decode(&image, output, sizeof output, &written, &consumed, &error) == ES2_OK);
    CHECK(written == 2 && consumed == 7 && output[0] == 41 && output[1] == 43);
    test_put32(stream, 0);
    length = bitmap_fixture(bytes, 0, 0, 1, stream, 4, 0);
    CHECK(es2_dbm_parse((es2_span){bytes, length}, &image, &error) == ES2_OK);
    CHECK(es2_dbm_decode(&image, NULL, 0, &written, &consumed, &error) == ES2_OK);
    CHECK(written == 0 && consumed == 4);
}

static void test_arrays(void)
{
    const uint8_t pixels[] = {17, 22, 29};
    uint8_t bytes[128], altered[128];
    size_t first, second, length, i;
    es2_dba array;
    es2_dbm image;
    es2_error error;
    memset(bytes, 0, sizeof bytes);
    test_put32(bytes, UINT32_C(0x00280001));
    test_put32(bytes + 8, 2);
    first = bitmap_fixture(bytes + 12, 3, 1, 0, pixels, 3, 0);
    second = bitmap_fixture(bytes + 12 + first, 2, 1, 0, pixels, 2, 0);
    length = 12 + first + second;
    test_put32(bytes + 4, (uint32_t)(length - 8));
    CHECK(es2_dba_parse((es2_span){bytes, length}, &array, &error) == ES2_OK);
    CHECK(array.count == 2 && array.serialized_size == length && array.total_pixel_bytes == 5);
    CHECK(array.children.data == bytes + 12 && array.trailing.size == 0);
    CHECK(es2_dba_image_at(&array, 0, &image, &error) == ES2_OK);
    CHECK(image.offset == 12 && image.width == 3);
    CHECK(es2_dba_image_at(&array, 1, &image, &error) == ES2_OK);
    CHECK(image.offset == 12 + first && image.width == 2);
    CHECK(es2_dba_image_at(&array, 2, &image, &error) == ES2_ERR_OFFSET);
    CHECK(es2_dba_parse((es2_span){bytes, length}, NULL, &error) == ES2_ERR_ARGUMENT);
    CHECK(es2_dba_parse_prefix((es2_span){bytes, length}, NULL, &error) == ES2_ERR_ARGUMENT);
    CHECK(es2_dba_image_at(NULL, 0, &image, &error) == ES2_ERR_ARGUMENT);
    CHECK(es2_dba_image_at(&array, 0, NULL, &error) == ES2_ERR_ARGUMENT);
    for (i = 0; i < length; ++i)
        CHECK(es2_dba_parse((es2_span){bytes, i}, &array, &error) != ES2_OK);
    /* Trailer remains outside the declared array, never an inferred child. */
    bytes[length] = 37; bytes[length + 1] = 38;
    CHECK(es2_dba_parse((es2_span){bytes, length + 2}, &array, &error) == ES2_ERR_LENGTH);
    CHECK(error.offset == length);
    CHECK(es2_dba_parse_prefix((es2_span){bytes, length + 2}, &array, &error) == ES2_OK);
    CHECK(array.count == 2 && array.trailing.size == 2 && array.trailing.data[0] == 37);
    memcpy(altered, bytes, length); altered[0] ^= 1;
    CHECK(es2_dba_parse((es2_span){altered, length}, &array, &error) == ES2_ERR_MAGIC);
    memcpy(altered, bytes, length); test_put32(altered + 4, 3);
    CHECK(es2_dba_parse((es2_span){altered, length}, &array, &error) == ES2_ERR_LENGTH);
    memcpy(altered, bytes, length); test_put32(altered + 8, UINT32_MAX);
    CHECK(es2_dba_parse((es2_span){altered, length}, &array, &error) == ES2_ERR_COUNT);
    memcpy(altered, bytes, length); altered[12] ^= 1;
    CHECK(es2_dba_parse((es2_span){altered, length}, &array, &error) == ES2_ERR_MAGIC);
    memcpy(altered, bytes, length); test_put32(altered + 16, UINT32_MAX);
    CHECK(es2_dba_parse((es2_span){altered, length}, &array, &error) == ES2_ERR_TRUNCATED);
    memcpy(altered, bytes, length); test_put32(altered + 8, 1);
    CHECK(es2_dba_parse((es2_span){altered, length}, &array, &error) == ES2_ERR_LENGTH);
    CHECK(es2_dba_parse((es2_span){bytes, length}, &array, &error) == ES2_OK);
    array.children.size = 1;
    CHECK(es2_dba_image_at(&array, 0, &image, &error) == ES2_ERR_TRUNCATED);
    test_put32(bytes + 4, 4); test_put32(bytes + 8, 0);
    CHECK(es2_dba_parse((es2_span){bytes, 12}, &array, &error) == ES2_OK);
    CHECK(array.count == 0 && array.children.size == 0);
}

int test_images(void)
{
    test_palettes();
    test_bitmaps();
    test_rle();
    test_arrays();
    return 0;
}
