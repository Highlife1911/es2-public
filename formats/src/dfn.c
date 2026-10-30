/* DFN/HFN bitmap fonts (GLFont); layout in docs/formats/dfn.md. */
#include "es2/formats/dfn.h"

#define TRY(expression) do { es2_status result_ = (expression); \
    if (result_ != ES2_OK) return result_; } while (0)

#define DFN_TAG UINT32_C(0x00280005)
#define DFN_HEADER 0x22u /* envelope (8) + eleven words + the data size */

static uint32_t u32_at(es2_span span, size_t index)
{
    const uint8_t *p = span.data + index * 4;
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

/* Glyph width as VSHELL 0x4010fd takes it: the fixed width, or the byte of the
 * per-glyph table. index is already range-checked. */
static uint16_t width_of(const es2_dfn *font, size_t index)
{
    return font->fixed_width == -1 ? font->widths.data[index]
                                   : (uint16_t)font->fixed_width;
}

/* Original: VSHELL.EXE 0x4547b4 (GLFont read method, vtable 0x47c004 slot 3);
 * the envelope (tag, body size) is read by the object loader 0x404818. */
es2_status es2_dfn_parse(es2_span input, es2_dfn *out, es2_error *error)
{
    es2_reader reader;
    es2_dfn font = {0};
    uint32_t tag;
    size_t body_end, i;
    if (out == NULL) return es2_fail(error, ES2_ERR_ARGUMENT, 0);
    TRY(es2_reader_init(&reader, input, error));
    TRY(es2_read_u32(&reader, &tag));
    if (tag != DFN_TAG) return es2_fail(error, ES2_ERR_MAGIC, 0);
    TRY(es2_read_u32(&reader, &font.body_size));
    if ((size_t)font.body_size > es2_remaining(&reader))
        return es2_fail(error, ES2_ERR_TRUNCATED, reader.offset);
    body_end = reader.offset + font.body_size;
    reader.span.size = body_end;
    TRY(es2_read_i16(&reader, &font.glyph_count));
    TRY(es2_read_i16(&reader, &font.map_count));
    TRY(es2_read_i16(&reader, &font.first_code));
    TRY(es2_read_i16(&reader, &font.height));
    TRY(es2_read_i16(&reader, &font.fixed_width));
    TRY(es2_read_i16(&reader, &font.word_12));
    TRY(es2_read_i16(&reader, &font.word_14));
    TRY(es2_read_i16(&reader, &font.bits_per_pixel));
    TRY(es2_read_i16(&reader, &font.compression));
    TRY(es2_read_i16(&reader, &font.baseline));
    TRY(es2_read_i16(&reader, &font.colour_count));
    TRY(es2_read_u32(&reader, &font.data_size));
    if (font.glyph_count < 0) return es2_fail(error, ES2_ERR_COUNT, 0x08);
    if (font.first_code == -1 && font.map_count < 0)
        return es2_fail(error, ES2_ERR_COUNT, 0x0a);
    if (font.height < 0) return es2_fail(error, ES2_ERR_LENGTH, 0x0e);
    if (font.fixed_width < -1) return es2_fail(error, ES2_ERR_LENGTH, 0x10);
    if (font.colour_count < 0) return es2_fail(error, ES2_ERR_COUNT, 0x1c);
    /* Read order of 0x454859..0x454961: colours, pixel data, offsets,
     * widths (only when +0x10 is -1), character map (only when +0x0c is -1). */
    TRY(es2_read_span(&reader, (size_t)font.colour_count * 4, &font.colours));
    TRY(es2_read_span(&reader, font.data_size, &font.data));
    TRY(es2_read_span(&reader, (size_t)font.glyph_count * 4, &font.offsets));
    if (font.fixed_width == -1)
        TRY(es2_read_span(&reader, (size_t)font.glyph_count, &font.widths));
    if (font.first_code == -1)
        TRY(es2_read_span(&reader, (size_t)font.map_count * 2, &font.char_map));
    TRY(es2_reader_end(&reader));
    reader.span = input;
    if ((font.body_size & 1u) != 0 && es2_remaining(&reader) != 0) {
        uint8_t padding;
        TRY(es2_read_u8(&reader, &padding));
        if (padding != 0) return es2_fail(error, ES2_ERR_LENGTH, body_end);
    }
    if (es2_remaining(&reader) != 0)
        return es2_fail(error, ES2_ERR_LENGTH, reader.offset);
    if (font.compression == 0 && font.bits_per_pixel == 8 && font.first_code != -1) {
        for (i = 0; i < (size_t)font.glyph_count; ++i) {
            uint32_t offset = u32_at(font.offsets, i);
            size_t size = (size_t)width_of(&font, i) * (size_t)font.height;
            if (offset > font.data_size || size > font.data_size - offset)
                return es2_fail(error, ES2_ERR_OFFSET, DFN_HEADER
                    + (size_t)font.colour_count * 4 + font.data_size + i * 4);
        }
    }
    font.serialized_size = reader.offset;
    *out = font;
    return ES2_OK;
}

static es2_status glyph_index(const es2_dfn *font, uint8_t code, size_t *index,
                              es2_error *error)
{
    int32_t first, value = code;
    if (font->first_code == -1) return es2_fail(error, ES2_ERR_UNSUPPORTED, 0x0c);
    first = font->first_code;
    /* Range test of 0x40112e..0x401146: first <= code < first + count. */
    if (value < first || value >= first + (int32_t)font->glyph_count)
        return es2_fail(error, ES2_ERR_NOT_FOUND, code);
    *index = (size_t)(value - first);
    return ES2_OK;
}

/* Original: VSHELL.EXE 0x4010fd (GLFont::getWidth). */
es2_status es2_dfn_advance(const es2_dfn *font, uint8_t code, uint16_t *advance,
                           es2_error *error)
{
    size_t index;
    es2_error_clear(error);
    if (font == NULL || advance == NULL) return es2_fail(error, ES2_ERR_ARGUMENT, 0);
    TRY(glyph_index(font, code, &index, error));
    *advance = width_of(font, index);
    return ES2_OK;
}

/* Original: VSHELL.EXE 0x4011f6 (GLFont::render, one glyph), bitmap part. */
es2_status es2_dfn_glyph_at(const es2_dfn *font, uint8_t code,
                            es2_dfn_glyph *out, es2_error *error)
{
    es2_dfn_glyph glyph;
    size_t index;
    es2_error_clear(error);
    if (font == NULL || out == NULL) return es2_fail(error, ES2_ERR_ARGUMENT, 0);
    if (font->compression != 0 || font->bits_per_pixel != 8)
        return es2_fail(error, ES2_ERR_UNSUPPORTED, 0x16);
    TRY(glyph_index(font, code, &index, error));
    glyph.code = code;
    glyph.width = width_of(font, index);
    glyph.height = (uint16_t)font->height;
    glyph.offset = u32_at(font->offsets, index);
    TRY(es2_subspan(font->data, glyph.offset,
                    (size_t)glyph.width * glyph.height, &glyph.pixels, error));
    *out = glyph;
    return ES2_OK;
}

/* Original: VSHELL.EXE 0x4011af (sum of getWidth over a string). */
es2_status es2_dfn_text_width(const es2_dfn *font, const uint8_t *text,
                              size_t length, uint32_t *width, es2_error *error)
{
    uint32_t sum = 0;
    size_t i;
    es2_error_clear(error);
    if (font == NULL || width == NULL || (text == NULL && length != 0))
        return es2_fail(error, ES2_ERR_ARGUMENT, 0);
    for (i = 0; i < length; ++i) {
        uint16_t advance;
        es2_status status = es2_dfn_advance(font, text[i], &advance, error);
        if (status != ES2_OK) return es2_fail(error, status, i);
        sum += advance;
    }
    *width = sum;
    return ES2_OK;
}
