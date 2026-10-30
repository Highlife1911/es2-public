#include "es2/formats/dpl.h"

#define TRY(expression) do { es2_status result_ = (expression); \
    if (result_ != ES2_OK) return result_; } while (0)

es2_status es2_dpl_parse(es2_span input, es2_dpl *out, es2_error *error)
{
    es2_reader reader;
    es2_dpl parsed = {0};
    uint32_t tag, body_size;
    size_t i, ramps_offset;
    if (out == NULL) return es2_fail(error, ES2_ERR_ARGUMENT, 0);
    TRY(es2_reader_init(&reader, input, error));
    TRY(es2_read_u32(&reader, &tag));
    if (tag != UINT32_C(0x0028000f)) return es2_fail(error, ES2_ERR_MAGIC, 0);
    TRY(es2_read_u32(&reader, &body_size));
    if ((size_t)body_size > es2_remaining(&reader))
        return es2_fail(error, ES2_ERR_TRUNCATED, reader.offset);
    if ((size_t)body_size != es2_remaining(&reader))
        return es2_fail(error, ES2_ERR_LENGTH, 4);
    TRY(es2_read_u16(&reader, &parsed.colour_count));
    TRY(es2_read_u16(&reader, &parsed.reserved_word));
    TRY(es2_read_span(&reader, (size_t)parsed.colour_count * 4, &parsed.colours));
    TRY(es2_read_u16(&reader, &parsed.ramp_count));
    TRY(es2_read_u16(&reader, &parsed.ramp_base));
    ramps_offset = reader.offset;
    for (i = 0; i < parsed.ramp_count; ++i) {
        uint16_t length;
        size_t j;
        TRY(es2_read_u16(&reader, &length));
        for (j = 0; j < length; ++j) {
            uint16_t index;
            size_t index_offset = reader.offset;
            TRY(es2_read_u16(&reader, &index));
            if (index >= parsed.colour_count)
                return es2_fail(error, ES2_ERR_OFFSET, index_offset);
        }
    }
    TRY(es2_reader_end(&reader));
    TRY(es2_subspan(input, ramps_offset, reader.offset - ramps_offset,
                   &parsed.ramps, error));
    *out = parsed;
    return ES2_OK;
}

es2_status es2_dpl_colour_at(const es2_dpl *palette, size_t index,
                             es2_dpl_colour *out, es2_error *error)
{
    es2_span colour;
    es2_error_clear(error);
    if (palette == NULL || out == NULL)
        return es2_fail(error, ES2_ERR_ARGUMENT, 0);
    if (index >= palette->colour_count)
        return es2_fail(error, ES2_ERR_OFFSET, index);
    TRY(es2_subspan(palette->colours, index * 4, 4, &colour, error));
    out->red = colour.data[0]; out->green = colour.data[1];
    out->blue = colour.data[2]; out->flags = colour.data[3];
    return ES2_OK;
}

es2_status es2_dpl_ramp_at(const es2_dpl *palette, size_t index,
                           es2_dpl_ramp *out, es2_error *error)
{
    es2_reader reader;
    size_t i;
    es2_error_clear(error);
    if (palette == NULL || out == NULL)
        return es2_fail(error, ES2_ERR_ARGUMENT, 0);
    if (index >= palette->ramp_count)
        return es2_fail(error, ES2_ERR_OFFSET, index);
    TRY(es2_reader_init(&reader, palette->ramps, error));
    for (i = 0; i <= index; ++i) {
        uint16_t length;
        es2_span indices;
        TRY(es2_read_u16(&reader, &length));
        TRY(es2_read_span(&reader, (size_t)length * 2, &indices));
        if (i == index) {
            out->count = length;
            out->indices = indices;
        }
    }
    return ES2_OK;
}
