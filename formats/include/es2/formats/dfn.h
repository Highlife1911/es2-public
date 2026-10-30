#ifndef ES2_FORMATS_DFN_H
#define ES2_FORMATS_DFN_H

#include "es2/formats/common.h"

/* DFN/HFN bitmap fonts (persistent-object tag 05 00 28 00, class GLFont).
 * Layout and VSHELL use: docs/formats/dfn.md; text drawing:
 * docs/reverse-engineering/vshell_text.md. Header words keep the signed
 * 16-bit reading of the original reader (VSHELL.EXE 0x4547b4). */
typedef struct es2_dfn {
    uint32_t body_size;     /* file +0x04: bytes after the 8-byte envelope */
    int16_t glyph_count;    /* +0x08: N, entries of the offset table */
    int16_t map_count;      /* +0x0a: words of the character map (first_code == -1 only) */
    int16_t first_code;     /* +0x0c: byte value of glyph 0; -1 = character map stored */
    int16_t height;         /* +0x0e: glyph bitmap height in rows */
    int16_t fixed_width;    /* +0x10: advance of every glyph; -1 = per-glyph widths stored */
    int16_t word_12;        /* +0x12: not read by VSHELL's text code (equals height on the CD) */
    int16_t word_14;        /* +0x14: not read by VSHELL's text code */
    int16_t bits_per_pixel; /* +0x16: 8 on the CD */
    int16_t compression;    /* +0x18: 0 on the CD; VSHELL draws only 0 */
    int16_t baseline;       /* +0x1a: rows from the glyph top to the pen position */
    int16_t colour_count;   /* +0x1c: four-byte colour records after the header */
    uint32_t data_size;     /* +0x1e: bytes of glyph pixel data */
    es2_span colours;       /* colour_count * 4 bytes, borrowed */
    es2_span data;          /* data_size bytes of glyph pixels, borrowed */
    es2_span offsets;       /* glyph_count little-endian u32 offsets into data */
    es2_span widths;        /* glyph_count bytes when fixed_width == -1, else empty */
    es2_span char_map;      /* map_count little-endian words when first_code == -1 */
    size_t serialized_size; /* 8 + body_size, plus the zero pad byte if present */
} es2_dfn;

typedef struct es2_dfn_glyph {
    uint8_t code;     /* the byte value asked for */
    uint16_t width;   /* advance and bitmap width (they are the same value) */
    uint16_t height;  /* the font height */
    uint32_t offset;  /* start of the bitmap inside data */
    es2_span pixels;  /* width * height palette indices, rows top first */
} es2_dfn_glyph;

/* Parses one complete DFN/HFN member. Refuses: a wrong tag (also the "Old
 * GLFont" tag 03 00 28 00, whose VSHELL factory 0x454608 returns no object),
 * a body that does not fit, bytes after the body other than one zero pad byte
 * after an odd body, a body not consumed exactly, negative counts, a negative
 * height or a fixed width below -1, and (for uncompressed 8-bit fonts without
 * a character map) any glyph bitmap outside the pixel data. The original reader
 * checks none of this. Output is valid only on ES2_OK and borrows input. */
es2_status es2_dfn_parse(es2_span input, es2_dfn *out, es2_error *error);

/* The advance of byte value code (VSHELL 0x4010fd). ES2_ERR_NOT_FOUND outside
 * [first_code, first_code + glyph_count): the original logs a warning and reads
 * outside its width table there, which has no defined result. A font with a
 * character map is ES2_ERR_UNSUPPORTED (VSHELL never consults the map). */
es2_status es2_dfn_advance(const es2_dfn *font, uint8_t code, uint16_t *advance,
                           es2_error *error);

/* The bitmap of byte value code; uncompressed 8-bit fonts only (otherwise
 * ES2_ERR_UNSUPPORTED). Range rule and refusals as es2_dfn_advance; VSHELL's
 * glyph draw substitutes '?' for a code outside the range (0x401293). */
es2_status es2_dfn_glyph_at(const es2_dfn *font, uint8_t code,
                            es2_dfn_glyph *out, es2_error *error);

/* Sum of the advances of length bytes (VSHELL 0x4011af over a NUL-terminated
 * string). The first byte without a glyph fails with ES2_ERR_NOT_FOUND and its
 * index as the error offset; *width is written only on success. */
es2_status es2_dfn_text_width(const es2_dfn *font, const uint8_t *text,
                              size_t length, uint32_t *width, es2_error *error);

#endif
