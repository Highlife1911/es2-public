#include "es2/formats/dbm.h"

#include <string.h>

#define TRY(expression) do { es2_status result_ = (expression); \
    if (result_ != ES2_OK) return result_; } while (0)

static es2_status image_record(es2_span input, size_t start, es2_dbm *out,
                               size_t *end, es2_error *error)
{
    es2_reader reader;
    es2_dbm image = {0};
    uint32_t tag, pixel_size;
    size_t body_end;
    TRY(es2_reader_init(&reader, input, error));
    TRY(es2_skip(&reader, start));
    TRY(es2_read_u32(&reader, &tag));
    if (tag != UINT32_C(0x0028000e))
        return es2_fail(error, ES2_ERR_MAGIC, start);
    TRY(es2_read_u32(&reader, &image.body_size));
    if ((size_t)image.body_size > es2_remaining(&reader))
        return es2_fail(error, ES2_ERR_TRUNCATED, reader.offset);
    if (image.body_size < 13)
        return es2_fail(error, ES2_ERR_LENGTH, start + 4);
    body_end = reader.offset + image.body_size;
    reader.span.size = body_end;
    TRY(es2_read_u16(&reader, &image.height));
    TRY(es2_read_u16(&reader, &image.width));
    TRY(es2_read_u8(&reader, &image.bits_per_pixel));
    TRY(es2_read_u8(&reader, &image.flags));
    TRY(es2_read_u8(&reader, &image.compression));
    TRY(es2_read_u32(&reader, &pixel_size));
    TRY(es2_read_u16(&reader, &image.colour_count));
    if ((size_t)pixel_size > es2_remaining(&reader) ||
        es2_remaining(&reader) - pixel_size != (size_t)image.colour_count * 4)
        return es2_fail(error, ES2_ERR_LENGTH, start + 15);
    TRY(es2_read_span(&reader, pixel_size, &image.pixels));
    TRY(es2_read_span(&reader, (size_t)image.colour_count * 4, &image.colours));
    if (image.compression == 0 && image.bits_per_pixel == 8 &&
        (image.flags & 15u) == 0 &&
        pixel_size != (uint32_t)image.width * image.height)
        return es2_fail(error, ES2_ERR_LENGTH, start + 15);
    if (image.compression != 0 && pixel_size < 4)
        return es2_fail(error, ES2_ERR_TRUNCATED, start + 21);
    reader.span = input;
    if ((image.body_size & 1u) != 0) {
        uint8_t padding;
        TRY(es2_read_u8(&reader, &padding));
        if (padding != 0) return es2_fail(error, ES2_ERR_LENGTH, body_end);
    }
    image.offset = start;
    image.serialized_size = reader.offset - start;
    *out = image;
    *end = reader.offset;
    return ES2_OK;
}

es2_status es2_dbm_parse(es2_span input, es2_dbm *out, es2_error *error)
{
    es2_dbm image;
    size_t end;
    es2_error_clear(error);
    if (out == NULL) return es2_fail(error, ES2_ERR_ARGUMENT, 0);
    TRY(image_record(input, 0, &image, &end, error));
    if (end != input.size) return es2_fail(error, ES2_ERR_LENGTH, end);
    *out = image;
    return ES2_OK;
}

es2_status es2_dba_parse_prefix(es2_span input, es2_dba *out, es2_error *error)
{
    es2_reader reader;
    es2_dba array = {0};
    uint32_t tag, body_size, i;
    size_t array_end, offset;
    es2_span bounded;
    if (out == NULL) return es2_fail(error, ES2_ERR_ARGUMENT, 0);
    TRY(es2_reader_init(&reader, input, error));
    TRY(es2_read_u32(&reader, &tag));
    if (tag != UINT32_C(0x00280001)) return es2_fail(error, ES2_ERR_MAGIC, 0);
    TRY(es2_read_u32(&reader, &body_size));
    if ((size_t)body_size > es2_remaining(&reader))
        return es2_fail(error, ES2_ERR_TRUNCATED, 8);
    if (body_size < 4) return es2_fail(error, ES2_ERR_LENGTH, 4);
    array_end = reader.offset + body_size;
    reader.span.size = array_end;
    TRY(es2_read_u32(&reader, &array.count));
    /* Every DBM needs its 21-byte header plus the odd-body alignment. */
    if ((size_t)array.count > es2_remaining(&reader) / 22)
        return es2_fail(error, ES2_ERR_COUNT, 8);
    bounded = input;
    bounded.size = array_end;
    offset = reader.offset;
    for (i = 0; i < array.count; ++i) {
        es2_dbm image;
        TRY(image_record(bounded, offset, &image, &offset, error));
        array.total_pixel_bytes += (uint64_t)image.width * image.height;
    }
    if (offset != array_end) return es2_fail(error, ES2_ERR_LENGTH, offset);
    array.serialized_size = array_end;
    TRY(es2_subspan(input, 12, array_end - 12, &array.children, error));
    TRY(es2_subspan(input, array_end, input.size - array_end,
                   &array.trailing, error));
    *out = array;
    return ES2_OK;
}

es2_status es2_dba_parse(es2_span input, es2_dba *out, es2_error *error)
{
    es2_dba array;
    if (out == NULL) return es2_fail(error, ES2_ERR_ARGUMENT, 0);
    TRY(es2_dba_parse_prefix(input, &array, error));
    if (array.trailing.size != 0)
        return es2_fail(error, ES2_ERR_LENGTH, array.serialized_size);
    *out = array;
    return ES2_OK;
}

es2_status es2_dba_image_at(const es2_dba *array, size_t index,
                            es2_dbm *out, es2_error *error)
{
    size_t i, offset = 0;
    es2_dbm image;
    es2_error_clear(error);
    if (array == NULL || out == NULL)
        return es2_fail(error, ES2_ERR_ARGUMENT, 0);
    if (index >= array->count) return es2_fail(error, ES2_ERR_OFFSET, index);
    for (i = 0; i <= index; ++i)
        TRY(image_record(array->children, offset, &image, &offset, error));
    /* children begins at byte 12 of the serialized array. */
    image.offset += 12;
    *out = image;
    return ES2_OK;
}

es2_status es2_dbm_decode(const es2_dbm *image, uint8_t *output, size_t capacity,
                          size_t *written, size_t *stored_consumed,
                          es2_error *error)
{
    es2_reader reader;
    size_t needed, produced = 0;
    uint32_t declared;
    if (written != NULL) *written = 0;
    if (stored_consumed != NULL) *stored_consumed = 0;
    es2_error_clear(error);
    if (image == NULL) return es2_fail(error, ES2_ERR_ARGUMENT, 0);
    if (image->bits_per_pixel != 8 || (image->flags & 15u) != 0 ||
        (image->compression != 0 && image->compression != 1))
        return es2_fail(error, ES2_ERR_UNSUPPORTED, 0);
    needed = (size_t)image->width * image->height;
    if (needed != 0 && output == NULL)
        return es2_fail(error, ES2_ERR_ARGUMENT, 0);
    if (capacity < needed) return es2_fail(error, ES2_ERR_CAPACITY, 0);
    TRY(es2_reader_init(&reader, image->pixels, error));
    if (image->compression == 0) {
        if (reader.span.size != needed) return es2_fail(error, ES2_ERR_LENGTH, 0);
        if (needed != 0) memmove(output, image->pixels.data, needed);
        if (written != NULL) *written = needed;
        if (stored_consumed != NULL) *stored_consumed = needed;
        return ES2_OK;
    }
    TRY(es2_read_u32(&reader, &declared));
    if ((size_t)declared != needed) return es2_fail(error, ES2_ERR_LENGTH, 0);
    while (produced < needed) {
        uint8_t control;
        size_t count;
        TRY(es2_read_u8(&reader, &control));
        count = control & 127u;
        if (count > needed - produced) count = needed - produced;
        if ((control & 128u) != 0) {
            uint8_t value;
            TRY(es2_read_u8(&reader, &value));
            if (count != 0) memset(output + produced, value, count);
        } else {
            es2_span literal;
            TRY(es2_read_span(&reader, count, &literal));
            if (count != 0) memcpy(output + produced, literal.data, count);
        }
        produced += count;
    }
    if (written != NULL) *written = produced;
    if (stored_consumed != NULL) *stored_consumed = reader.offset;
    return ES2_OK;
}
