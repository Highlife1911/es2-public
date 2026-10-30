#include "es2/formats/common.h"

const char *es2_status_string(es2_status status)
{
    switch (status) {
    case ES2_OK: return "ok";
    case ES2_ERR_ARGUMENT: return "invalid argument";
    case ES2_ERR_TRUNCATED: return "truncated input";
    case ES2_ERR_MAGIC: return "wrong signature or tag";
    case ES2_ERR_VERSION: return "unsupported version";
    case ES2_ERR_COUNT: return "invalid count";
    case ES2_ERR_OFFSET: return "offset out of range";
    case ES2_ERR_LENGTH: return "inconsistent length or trailing bytes";
    case ES2_ERR_OVERLAP: return "overlapping ranges";
    case ES2_ERR_STRING: return "invalid name or string termination";
    case ES2_ERR_UNSUPPORTED: return "unsupported encoding or object type";
    case ES2_ERR_CAPACITY: return "insufficient caller capacity";
    case ES2_ERR_DEPTH: return "object depth limit exceeded";
    case ES2_ERR_CALLBACK: return "visitor stopped traversal";
    case ES2_ERR_NOT_FOUND: return "member not found";
    default: return "unknown error";
    }
}

es2_status es2_fail(es2_error *error, es2_status status, size_t offset)
{
    if (error != NULL) {
        error->code = status;
        error->offset = offset;
        error->message = es2_status_string(status);
    }
    return status;
}

void es2_error_clear(es2_error *error) { (void)es2_fail(error, ES2_OK, 0); }

es2_status es2_subspan(es2_span input, size_t offset, size_t length,
                       es2_span *out, es2_error *error)
{
    es2_span value;
    if (out == NULL || (input.data == NULL && input.size != 0))
        return es2_fail(error, ES2_ERR_ARGUMENT, offset);
    if (offset > input.size)
        return es2_fail(error, ES2_ERR_OFFSET, offset);
    if (length > input.size - offset)
        return es2_fail(error, ES2_ERR_TRUNCATED, offset);
    value.data = input.data == NULL ? NULL : input.data + offset;
    value.size = length;
    *out = value;
    return ES2_OK;
}

es2_status es2_reader_init(es2_reader *reader, es2_span input, es2_error *error)
{
    if (reader == NULL) return es2_fail(error, ES2_ERR_ARGUMENT, 0);
    reader->span = input;
    reader->offset = 0;
    reader->error = error;
    reader->status = ES2_OK;
    es2_error_clear(error);
    if (input.data == NULL && input.size != 0)
        reader->status = es2_fail(error, ES2_ERR_ARGUMENT, 0);
    return reader->status;
}

size_t es2_remaining(const es2_reader *reader)
{
    if (reader == NULL || reader->offset > reader->span.size) return 0;
    return reader->span.size - reader->offset;
}

static es2_status reader_bad(es2_reader *reader, es2_status status)
{
    if (reader == NULL) return ES2_ERR_ARGUMENT;
    if (reader->status == ES2_OK)
        reader->status = es2_fail(reader->error, status, reader->offset);
    return reader->status;
}

es2_status es2_read_span(es2_reader *reader, size_t length, es2_span *out)
{
    es2_status status;
    if (reader == NULL) return ES2_ERR_ARGUMENT;
    if (reader->status != ES2_OK) return reader->status;
    if (out == NULL) return reader_bad(reader, ES2_ERR_ARGUMENT);
    status = es2_subspan(reader->span, reader->offset, length, out, reader->error);
    if (status != ES2_OK) { reader->status = status; return status; }
    reader->offset += length;
    return ES2_OK;
}

es2_status es2_skip(es2_reader *reader, size_t length)
{
    es2_span ignored;
    return es2_read_span(reader, length, &ignored);
}

es2_status es2_read_u8(es2_reader *reader, uint8_t *out)
{
    es2_span bytes;
    es2_status status;
    if (out == NULL) return reader_bad(reader, ES2_ERR_ARGUMENT);
    status = es2_read_span(reader, 1, &bytes);
    if (status == ES2_OK) *out = bytes.data[0];
    return status;
}

es2_status es2_read_u16(es2_reader *reader, uint16_t *out)
{
    es2_span bytes;
    es2_status status;
    if (out == NULL) return reader_bad(reader, ES2_ERR_ARGUMENT);
    status = es2_read_span(reader, 2, &bytes);
    if (status == ES2_OK)
        *out = (uint16_t)((uint16_t)bytes.data[0] | ((uint16_t)bytes.data[1] << 8));
    return status;
}

es2_status es2_read_u32(es2_reader *reader, uint32_t *out)
{
    es2_span bytes;
    es2_status status;
    if (out == NULL) return reader_bad(reader, ES2_ERR_ARGUMENT);
    status = es2_read_span(reader, 4, &bytes);
    if (status == ES2_OK)
        *out = (uint32_t)bytes.data[0] | ((uint32_t)bytes.data[1] << 8)
             | ((uint32_t)bytes.data[2] << 16) | ((uint32_t)bytes.data[3] << 24);
    return status;
}

es2_status es2_read_i16(es2_reader *reader, int16_t *out)
{
    uint16_t value;
    es2_status status;
    if (out == NULL) return reader_bad(reader, ES2_ERR_ARGUMENT);
    status = es2_read_u16(reader, &value);
    if (status == ES2_OK) {
        /* Keep conversion representable; do not rely on unsigned-to-signed casts. */
        *out = value <= INT16_MAX ? (int16_t)value
             : (int16_t)(-1 - (int16_t)(UINT16_MAX - value));
    }
    return status;
}

es2_status es2_read_i32(es2_reader *reader, int32_t *out)
{
    uint32_t value;
    es2_status status;
    if (out == NULL) return reader_bad(reader, ES2_ERR_ARGUMENT);
    status = es2_read_u32(reader, &value);
    if (status == ES2_OK)
        *out = value <= INT32_MAX ? (int32_t)value
             : -1 - (int32_t)(UINT32_MAX - value);
    return status;
}

es2_status es2_reader_end(es2_reader *reader)
{
    if (reader == NULL) return ES2_ERR_ARGUMENT;
    if (reader->status != ES2_OK) return reader->status;
    if (reader->offset != reader->span.size) return reader_bad(reader, ES2_ERR_LENGTH);
    return ES2_OK;
}
