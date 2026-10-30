/* DFN/HFN bitmap fonts (dfn.h). Default build: synthetic fixtures written byte
 * by byte below, no game data. With ES2_DFN_REAL_TEST: every DFN and HFN member
 * of the CD's VOL archives under ES2_DATA_ROOT, VSHELL's view of the shell
 * fonts and the ESTEXT byte coverage; skipped when ES2_DATA_ROOT is unset or
 * empty. The real test keeps counts, metrics and index sets only, no glyphs. */
#include "es2/formats/dfn.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)

static es2_span span(const uint8_t *data, size_t size) { es2_span s; s.data = data; s.size = size; return s; }

#ifndef ES2_DFN_REAL_TEST
static void put16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, v & 0xffffu); put16(p + 2, v >> 16); }

typedef struct spec {
    int glyphs, map_count, first, height, fixed, word12, word14, bpp, compression, baseline, colours;
    const uint8_t *widths;   /* glyphs entries when fixed == -1 */
    const uint32_t *offsets; /* NULL: contiguous from 0 */
    long data_size;          /* < 0: sum of width * height */
} spec;

/* Builds a member as the documented layout describes it; returns its length.
 * Pixel bytes are a synthetic counting pattern. */
static size_t build(uint8_t *b, const spec *s)
{
    size_t at = 8, i, data = 0;
    put32(b, 0x00280005u);
    put16(b + at, (unsigned)(s->glyphs & 0xffff)); at += 2;
    put16(b + at, (unsigned)(s->map_count & 0xffff)); at += 2;
    put16(b + at, (unsigned)(s->first & 0xffff)); at += 2;
    put16(b + at, (unsigned)(s->height & 0xffff)); at += 2;
    put16(b + at, (unsigned)(s->fixed & 0xffff)); at += 2;
    put16(b + at, (unsigned)(s->word12 & 0xffff)); at += 2;
    put16(b + at, (unsigned)(s->word14 & 0xffff)); at += 2;
    put16(b + at, (unsigned)(s->bpp & 0xffff)); at += 2;
    put16(b + at, (unsigned)(s->compression & 0xffff)); at += 2;
    put16(b + at, (unsigned)(s->baseline & 0xffff)); at += 2;
    put16(b + at, (unsigned)(s->colours & 0xffff)); at += 2;
    for (i = 0; i < (size_t)(s->glyphs > 0 ? s->glyphs : 0); ++i)
        data += (size_t)(s->fixed == -1 ? s->widths[i] : s->fixed) * (size_t)s->height;
    if (s->data_size >= 0) data = (size_t)s->data_size;
    put32(b + at, (uint32_t)data); at += 4;
    for (i = 0; i < (size_t)(s->colours > 0 ? s->colours : 0); ++i) { put32(b + at, 0x01000000u | (uint32_t)i); at += 4; }
    for (i = 0; i < data; ++i) b[at++] = (uint8_t)(i % 7 + 1);
    {
        uint32_t offset = 0;
        for (i = 0; i < (size_t)(s->glyphs > 0 ? s->glyphs : 0); ++i) {
            put32(b + at, s->offsets ? s->offsets[i] : offset); at += 4;
            offset += (uint32_t)((s->fixed == -1 ? s->widths[i] : s->fixed) * s->height);
        }
    }
    if (s->fixed == -1)
        for (i = 0; i < (size_t)(s->glyphs > 0 ? s->glyphs : 0); ++i) b[at++] = s->widths[i];
    if (s->first == -1)
        for (i = 0; i < (size_t)(s->map_count > 0 ? s->map_count : 0); ++i) { put16(b + at, (unsigned)(65 + i)); at += 2; }
    put32(b + 4, (uint32_t)(at - 8));
    return at;
}

static const uint8_t widths3[3] = {1, 0, 3};
/* 'A' 1 wide, 'B' zero wide, 'C' 3 wide, height 2, baseline 1: body 49 (odd). */
static spec base_spec(void)
{
    spec s = {3, 0, 'A', 2, -1, 2, 3, 8, 0, 1, 0, widths3, NULL, -1};
    return s;
}

static void test_parse_basic(void)
{
    uint8_t b[256], padded[256];
    spec s = base_spec();
    size_t n = build(b, &s);
    es2_dfn f;
    es2_error e;
    es2_dfn_glyph g;
    uint16_t adv = 0;
    uint32_t w = 99;
    CHECK(n == 57);
    CHECK(es2_dfn_parse(span(b, n), &f, &e) == ES2_OK && e.code == ES2_OK);
    CHECK(f.body_size == 49 && f.serialized_size == 57);
    CHECK(f.glyph_count == 3 && f.first_code == 'A' && f.height == 2 && f.fixed_width == -1);
    CHECK(f.word_12 == 2 && f.word_14 == 3 && f.bits_per_pixel == 8 && f.compression == 0);
    CHECK(f.baseline == 1 && f.colour_count == 0 && f.data_size == 8 && f.map_count == 0);
    CHECK(f.colours.size == 0 && f.data.size == 8 && f.data.data == b + 0x22);
    CHECK(f.offsets.size == 12 && f.offsets.data == b + 0x2a);
    CHECK(f.widths.size == 3 && f.widths.data == b + 0x36 && f.char_map.size == 0);
    CHECK(es2_dfn_advance(&f, 'A', &adv, &e) == ES2_OK && adv == 1);
    CHECK(es2_dfn_advance(&f, 'B', &adv, &e) == ES2_OK && adv == 0);
    CHECK(es2_dfn_advance(&f, 'C', &adv, &e) == ES2_OK && adv == 3);
    CHECK(es2_dfn_glyph_at(&f, 'C', &g, &e) == ES2_OK);
    CHECK(g.code == 'C' && g.width == 3 && g.height == 2 && g.offset == 2);
    CHECK(g.pixels.data == b + 0x22 + 2 && g.pixels.size == 6);
    CHECK(es2_dfn_glyph_at(&f, 'B', &g, &e) == ES2_OK && g.pixels.size == 0 && g.offset == 2);
    CHECK(es2_dfn_glyph_at(&f, 'A', &g, &e) == ES2_OK && g.pixels.size == 2 && g.offset == 0);
    CHECK(es2_dfn_text_width(&f, (const uint8_t *)"CAB", 3, &w, &e) == ES2_OK && w == 4);
    CHECK(es2_dfn_text_width(&f, NULL, 0, &w, &e) == ES2_OK && w == 0);
    CHECK(es2_dfn_text_width(&f, (const uint8_t *)"", 0, &w, NULL) == ES2_OK && w == 0);
    /* Range: below first, past the last glyph, the byte 0xff. */
    CHECK(es2_dfn_advance(&f, '@', &adv, &e) == ES2_ERR_NOT_FOUND && e.offset == '@');
    CHECK(es2_dfn_advance(&f, 'D', &adv, &e) == ES2_ERR_NOT_FOUND);
    CHECK(es2_dfn_advance(&f, 0xff, &adv, NULL) == ES2_ERR_NOT_FOUND);
    CHECK(es2_dfn_glyph_at(&f, 'D', &g, &e) == ES2_ERR_NOT_FOUND);
    w = 77;
    CHECK(es2_dfn_text_width(&f, (const uint8_t *)"ABxC", 4, &w, &e) == ES2_ERR_NOT_FOUND
          && e.offset == 2 && w == 77);
    CHECK(es2_dfn_text_width(&f, (const uint8_t *)"x", 1, &w, NULL) == ES2_ERR_NOT_FOUND);
    /* Arguments. */
    CHECK(es2_dfn_parse(span(b, n), NULL, &e) == ES2_ERR_ARGUMENT);
    CHECK(es2_dfn_parse(span(NULL, 4), &f, &e) == ES2_ERR_ARGUMENT);
    CHECK(es2_dfn_advance(NULL, 'A', &adv, &e) == ES2_ERR_ARGUMENT);
    CHECK(es2_dfn_advance(&f, 'A', NULL, &e) == ES2_ERR_ARGUMENT);
    CHECK(es2_dfn_glyph_at(NULL, 'A', &g, &e) == ES2_ERR_ARGUMENT);
    CHECK(es2_dfn_glyph_at(&f, 'A', NULL, &e) == ES2_ERR_ARGUMENT);
    CHECK(es2_dfn_text_width(NULL, (const uint8_t *)"A", 1, &w, &e) == ES2_ERR_ARGUMENT);
    CHECK(es2_dfn_text_width(&f, (const uint8_t *)"A", 1, NULL, &e) == ES2_ERR_ARGUMENT);
    CHECK(es2_dfn_text_width(&f, NULL, 1, &w, &e) == ES2_ERR_ARGUMENT);
    /* The odd body may carry one zero pad byte, nothing else. */
    memcpy(padded, b, n); padded[n] = 0;
    CHECK(es2_dfn_parse(span(padded, n + 1), &f, &e) == ES2_OK && f.serialized_size == 58);
    padded[n] = 1;
    CHECK(es2_dfn_parse(span(padded, n + 1), &f, &e) == ES2_ERR_LENGTH && e.offset == n);
    padded[n] = 0; padded[n + 1] = 0;
    CHECK(es2_dfn_parse(span(padded, n + 2), &f, &e) == ES2_ERR_LENGTH && e.offset == n + 1);
}

static void test_refusals(void)
{
    uint8_t b[256], a[256];
    spec s = base_spec();
    size_t n = build(b, &s), i;
    es2_dfn f, before;
    es2_error e;
    memset(&before, 0x5a, sizeof before);
    /* Every truncation, and the output stays untouched. */
    for (i = 0; i < n; ++i) {
        f = before;
        CHECK(es2_dfn_parse(span(b, i), &f, &e) != ES2_OK);
        CHECK(memcmp(&f, &before, sizeof f) == 0);
    }
    CHECK(es2_dfn_parse(span(b, 7), &f, &e) == ES2_ERR_TRUNCATED);
    /* Wrong tag, including the "Old GLFont" tag 3 and the GLBitmap tag. */
    memcpy(a, b, n); a[0] = 3;
    CHECK(es2_dfn_parse(span(a, n), &f, &e) == ES2_ERR_MAGIC && e.offset == 0);
    memcpy(a, b, n); a[0] = 0x0e;
    CHECK(es2_dfn_parse(span(a, n), &f, &e) == ES2_ERR_MAGIC);
    /* Declared body longer than the input; shorter than the content. */
    memcpy(a, b, n); put32(a + 4, 50);
    CHECK(es2_dfn_parse(span(a, n), &f, &e) == ES2_ERR_TRUNCATED && e.offset == 8);
    memcpy(a, b, n); put32(a + 4, 48);
    CHECK(es2_dfn_parse(span(a, n), &f, &e) == ES2_ERR_TRUNCATED);
    memcpy(a, b, n); put32(a + 4, 20);
    CHECK(es2_dfn_parse(span(a, n), &f, &e) == ES2_ERR_TRUNCATED);
    /* Even body with bytes after it. */
    {
        uint8_t c[256];
        static const uint8_t w2[2] = {1, 1};
        spec t = {2, 0, 'A', 2, -1, 2, 1, 8, 0, 1, 0, w2, NULL, -1};
        size_t m = build(c, &t);
        CHECK((m - 8) % 2 == 0);
        CHECK(es2_dfn_parse(span(c, m), &f, &e) == ES2_OK && f.serialized_size == m);
        c[m] = 0;
        CHECK(es2_dfn_parse(span(c, m + 1), &f, &e) == ES2_ERR_LENGTH && e.offset == m);
        /* Body with unread bytes inside it (the original skips them). */
        c[m] = 0; c[m + 1] = 0;
        put32(c + 4, (uint32_t)(m - 8 + 2));
        CHECK(es2_dfn_parse(span(c, m + 2), &f, &e) == ES2_ERR_LENGTH && e.offset == m);
    }
    /* Negative counts and metrics. */
    memcpy(a, b, n); put16(a + 0x08, 0xffff);
    CHECK(es2_dfn_parse(span(a, n), &f, &e) == ES2_ERR_COUNT && e.offset == 0x08);
    memcpy(a, b, n); put16(a + 0x1c, 0x8000);
    CHECK(es2_dfn_parse(span(a, n), &f, &e) == ES2_ERR_COUNT && e.offset == 0x1c);
    memcpy(a, b, n); put16(a + 0x0e, 0xfffe);
    CHECK(es2_dfn_parse(span(a, n), &f, &e) == ES2_ERR_LENGTH && e.offset == 0x0e);
    memcpy(a, b, n); put16(a + 0x10, 0xfffe);
    CHECK(es2_dfn_parse(span(a, n), &f, &e) == ES2_ERR_LENGTH && e.offset == 0x10);
    /* A negative map count matters only when the map is stored. */
    memcpy(a, b, n); put16(a + 0x0a, 0xffff);
    CHECK(es2_dfn_parse(span(a, n), &f, &e) == ES2_OK && f.map_count == -1);
    {
        spec t = base_spec();
        size_t m;
        t.first = -1; t.map_count = -2;
        m = build(a, &t);
        CHECK(es2_dfn_parse(span(a, m), &f, &e) == ES2_ERR_COUNT && e.offset == 0x0a);
    }
    /* Pixel data larger than the body. */
    memcpy(a, b, n); put32(a + 0x1e, 1000);
    CHECK(es2_dfn_parse(span(a, n), &f, &e) == ES2_ERR_TRUNCATED);
    /* A glyph bitmap outside the pixel data: offset past the end, then a
     * bitmap that starts inside but runs over. */
    memcpy(a, b, n); put32(a + 0x2a + 8, 9);
    CHECK(es2_dfn_parse(span(a, n), &f, &e) == ES2_ERR_OFFSET && e.offset == 0x2a + 8);
    memcpy(a, b, n); put32(a + 0x2a + 8, 3);
    CHECK(es2_dfn_parse(span(a, n), &f, &e) == ES2_ERR_OFFSET && e.offset == 0x2a + 8);
    memcpy(a, b, n); put32(a + 0x2a, 0xffffffffu);
    CHECK(es2_dfn_parse(span(a, n), &f, &e) == ES2_ERR_OFFSET && e.offset == 0x2a);
    /* The zero-width glyph may sit at the very end of the data. */
    memcpy(a, b, n); put32(a + 0x2a + 4, 8);
    CHECK(es2_dfn_parse(span(a, n), &f, &e) == ES2_OK);
    /* Non-contiguous and overlapping offsets are not the parser's business. */
    memcpy(a, b, n); put32(a + 0x2a + 8, 0);
    CHECK(es2_dfn_parse(span(a, n), &f, &e) == ES2_OK);
    /* A width byte that makes the last glyph overrun. */
    memcpy(a, b, n); a[0x36 + 2] = 4;
    CHECK(es2_dfn_parse(span(a, n), &f, &e) == ES2_ERR_OFFSET);
}

static void test_variants(void)
{
    uint8_t b[512];
    es2_dfn f;
    es2_error e;
    es2_dfn_glyph g;
    uint16_t adv;
    uint32_t w;
    size_t n;
    {   /* Fixed width 2: no width table; advance = the fixed width. */
        spec s = {4, 0, 32, 3, 2, 3, 2, 8, 0, 2, 0, NULL, NULL, -1};
        n = build(b, &s);
        CHECK(es2_dfn_parse(span(b, n), &f, &e) == ES2_OK);
        CHECK(f.widths.size == 0 && f.data_size == 24 && f.offsets.size == 16);
        CHECK(es2_dfn_advance(&f, 35, &adv, &e) == ES2_OK && adv == 2);
        CHECK(es2_dfn_advance(&f, 36, &adv, &e) == ES2_ERR_NOT_FOUND);
        CHECK(es2_dfn_glyph_at(&f, 33, &g, &e) == ES2_OK && g.offset == 6 && g.pixels.size == 6);
        CHECK(es2_dfn_text_width(&f, (const uint8_t *)"  !#", 4, &w, &e) == ES2_OK && w == 8);
    }
    {   /* Colour records (four bytes each) precede the pixel data. */
        spec s = base_spec();
        s.colours = 2;
        n = build(b, &s);
        CHECK(es2_dfn_parse(span(b, n), &f, &e) == ES2_OK);
        CHECK(f.colour_count == 2 && f.colours.size == 8 && f.colours.data == b + 0x22);
        CHECK(f.data.data == b + 0x2a);
        CHECK(es2_dfn_glyph_at(&f, 'C', &g, &e) == ES2_OK && g.pixels.data == b + 0x2a + 2);
        put32(b + 0x2a + 8 + 8, 9);
        CHECK(es2_dfn_parse(span(b, n), &f, &e) == ES2_ERR_OFFSET && e.offset == 0x2a + 8 + 8);
    }
    {   /* Character map (first code -1): stored and returned, never used. */
        spec s = base_spec();
        s.first = -1; s.map_count = 3;
        n = build(b, &s);
        CHECK(es2_dfn_parse(span(b, n), &f, &e) == ES2_OK);
        CHECK(f.char_map.size == 6 && f.char_map.data == b + n - 6);
        CHECK(es2_dfn_advance(&f, 'A', &adv, &e) == ES2_ERR_UNSUPPORTED);
        CHECK(es2_dfn_glyph_at(&f, 'A', &g, &e) == ES2_ERR_UNSUPPORTED);
        CHECK(es2_dfn_text_width(&f, (const uint8_t *)"A", 1, &w, &e) == ES2_ERR_UNSUPPORTED && e.offset == 0);
    }
    {   /* Compression or depth other than 0/8: spans kept, glyphs refused. */
        spec s = base_spec();
        s.compression = 1; s.data_size = 5;
        n = build(b, &s);
        CHECK(es2_dfn_parse(span(b, n), &f, &e) == ES2_OK && f.data_size == 5);
        CHECK(es2_dfn_advance(&f, 'C', &adv, &e) == ES2_OK && adv == 3);
        CHECK(es2_dfn_glyph_at(&f, 'C', &g, &e) == ES2_ERR_UNSUPPORTED);
        s = base_spec(); s.bpp = 4; s.data_size = 1;
        n = build(b, &s);
        CHECK(es2_dfn_parse(span(b, n), &f, &e) == ES2_OK);
        CHECK(es2_dfn_glyph_at(&f, 'A', &g, &e) == ES2_ERR_UNSUPPORTED);
    }
    {   /* No glyphs at all; codes near the int16 limits of first. */
        spec s = {0, 0, 32, 1, -1, 1, 0, 8, 0, 0, 0, NULL, NULL, -1};
        n = build(b, &s);
        CHECK(es2_dfn_parse(span(b, n), &f, &e) == ES2_OK && f.offsets.size == 0 && f.data.size == 0);
        CHECK(es2_dfn_advance(&f, 32, &adv, &e) == ES2_ERR_NOT_FOUND);
        s.glyphs = 1; s.first = 255; s.widths = widths3;
        n = build(b, &s);
        CHECK(es2_dfn_parse(span(b, n), &f, &e) == ES2_OK);
        CHECK(es2_dfn_advance(&f, 255, &adv, &e) == ES2_OK && adv == 1);
        CHECK(es2_dfn_advance(&f, 254, &adv, &e) == ES2_ERR_NOT_FOUND);
        s.first = -300;
        n = build(b, &s);
        CHECK(es2_dfn_parse(span(b, n), &f, &e) == ES2_OK);
        CHECK(es2_dfn_advance(&f, 0, &adv, &e) == ES2_ERR_NOT_FOUND);
    }
    {   /* Height zero: every bitmap is empty. */
        spec s = base_spec();
        s.height = 0;
        n = build(b, &s);
        CHECK(es2_dfn_parse(span(b, n), &f, &e) == ES2_OK && f.data_size == 0);
        CHECK(es2_dfn_glyph_at(&f, 'C', &g, &e) == ES2_OK && g.pixels.size == 0);
    }
}

int main(void)
{
    test_parse_basic();
    test_refusals();
    test_variants();
    printf("dfn synthetic: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}

#else /* ES2_DFN_REAL_TEST */
#include "es2/formats/vfs.h"

static const char *const archives[] = {
    "LANG0", "SHELL0", "SHELL1", "SHLSOUND", "SIMALERT", "SIMLANG",
    "SIMPATCH", "SIMSOUND", "SIMVOICE", "SIMVOICF", "SIMVOICG", "SIMVOL0", "ZONES"
};

static uint8_t *read_archive(const char *root, const char *name, size_t *length)
{
    char path[4096]; FILE *file; long size; uint8_t *bytes;
    int count = snprintf(path, sizeof path, "%s/VOL/%s.VOL", root, name);
    if (count < 0 || (size_t)count >= sizeof path) return NULL;
    file = fopen(path, "rb");
    if (file == NULL) return NULL;
    if (fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) < 0
        || fseek(file, 0, SEEK_SET) != 0) { fclose(file); return NULL; }
    bytes = malloc((size_t)size + 1);
    if (bytes == NULL) { fclose(file); return NULL; }
    if (fread(bytes, 1, (size_t)size, file) != (size_t)size) {
        free(bytes); fclose(file); return NULL;
    }
    fclose(file); *length = (size_t)size; return bytes;
}

static int has_suffix(es2_span name, const char *suffix)
{
    size_t n = strlen(suffix), i;
    if (name.size < n) return 0;
    for (i = 0; i < n; ++i) {
        uint8_t c = name.data[name.size - n + i];
        if (c >= 'a' && c <= 'z') c = (uint8_t)(c - 32);
        if (c != (uint8_t)suffix[i]) return 0;
    }
    return 1;
}

static uint32_t offset_at(const es2_dfn *f, size_t i)
{
    const uint8_t *p = f->offsets.data + 4 * i;
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Glyph offsets follow each other without gaps and the last bitmap ends at
 * the end of the pixel data. */
static int contiguous(const es2_dfn *f)
{
    uint32_t next = 0;
    size_t i;
    for (i = 0; i < (size_t)f->glyph_count; ++i) {
        es2_dfn_glyph g;
        if (offset_at(f, i) != next) return 0;
        if (es2_dfn_glyph_at(f, (uint8_t)(f->first_code + (int)i), &g, NULL) != ES2_OK) return 0;
        next += (uint32_t)g.pixels.size;
    }
    return next == f->data_size;
}

/* Bit set of the palette indices that glyph pixels use (0 included). */
static void ink_set(const es2_dfn *f, uint8_t used[256])
{
    size_t i;
    memset(used, 0, 256);
    for (i = 0; i < f->data.size; ++i) used[f->data.data[i]] = 1;
}

static int ink_is(const uint8_t used[256], const int *expected, size_t count)
{
    size_t i, n = 0;
    for (i = 0; i < count; ++i) if (!used[expected[i]]) return 0;
    for (i = 1; i < 256; ++i) n += used[i];
    return used[0] && n == count;
}

static void corpus(const es2_vol *vols, size_t *dfn, size_t *hfn, size_t *glyphs,
                   size_t *max_width_equal)
{
    size_t a, i;
    for (a = 0; a < 13; ++a) {
        for (i = 0; i < vols[a].member_count; ++i) {
            es2_vol_member m;
            es2_dfn f;
            es2_error e;
            int is_dfn, is_hfn;
            CHECK(es2_vol_member_at(&vols[a], i, &m, NULL) == ES2_OK);
            is_dfn = has_suffix(m.name, ".DFN");
            is_hfn = has_suffix(m.name, ".HFN");
            if (!is_dfn && !is_hfn) continue;
            *dfn += (size_t)is_dfn; *hfn += (size_t)is_hfn;
            CHECK(es2_dfn_parse(m.payload, &f, &e) == ES2_OK);
            if (e.code != ES2_OK) {
                fprintf(stderr, "%s member %zu: %s at %zu\n", archives[a], i, e.message, e.offset);
                continue;
            }
            /* Every CD font: no pad byte even after an odd body, proportional,
             * uncompressed 8-bit, first code 32, no colours, no map. */
            CHECK(f.serialized_size == m.payload.size && 8 + f.body_size == m.payload.size);
            CHECK(f.fixed_width == -1 && f.compression == 0 && f.bits_per_pixel == 8);
            CHECK(f.first_code == 32 && f.colour_count == 0 && f.map_count == 0);
            CHECK(f.glyph_count == 217 || f.glyph_count == 223);
            CHECK(f.word_12 == f.height);
            CHECK(contiguous(&f));
            *glyphs += (size_t)f.glyph_count;
            {   /* Word 0x14 is the widest glyph, except in the two shell FONT.DFN. */
                size_t k; int widest = 0;
                for (k = 0; k < f.widths.size; ++k)
                    if (f.widths.data[k] > widest) widest = f.widths.data[k];
                if (f.word_14 == widest) ++*max_width_equal;
            }
            if (is_hfn) CHECK(f.height == 13 && f.baseline == 11);
        }
    }
}

typedef struct shell_font {
    const char *path;
    int glyphs, height, baseline, word14;
    uint32_t data_size;
    int ink[2]; size_t inks;
} shell_font;

/* VSHELL's winners (SHELL1, priority 0x0a): derived metrics, not copied data. */
static const shell_font shell_fonts[] = {
    {"dfn\\font2.dfn", 223, 13, 10, 9, 21307, {41, 0}, 1},
    {"dfn\\black.dfn", 223, 13, 10, 9, 21307, {16, 0}, 1},
    {"dfn\\map.dfn", 223, 8, 7, 6, 4792, {58, 59}, 2},
    {"dfn\\font.dfn", 223, 18, 0, 16, 17208, {2, 0}, 1},
};

static void shell_view(const es2_vol *vols, es2_vfs_source *sources, es2_dfn *font2)
{
    es2_vfs vfs; size_t order[13], i;
    CHECK(es2_vfs_open_dbsim_order(sources, 13, 0x100, order, 13, &vfs, NULL) == ES2_OK);
    for (i = 0; i < 4; ++i) {
        const shell_font *s = &shell_fonts[i];
        es2_vfs_copy copy;
        es2_dfn f;
        uint8_t used[256];
        size_t zero = 0, c;
        CHECK(es2_vfs_find(&vfs, s->path, &copy, NULL) == ES2_OK);
        CHECK(sources[copy.source_index].data.vol == &vols[2]);
        CHECK(es2_dfn_parse(copy.payload, &f, NULL) == ES2_OK);
        CHECK(f.glyph_count == s->glyphs && f.height == s->height && f.baseline == s->baseline);
        CHECK(f.word_14 == s->word14 && f.data_size == s->data_size);
        ink_set(&f, used);
        CHECK(ink_is(used, s->ink, s->inks));
        for (c = 32; c < 255; ++c) {
            uint16_t adv;
            CHECK(es2_dfn_advance(&f, (uint8_t)c, &adv, NULL) == ES2_OK);
            zero += adv == 0;
        }
        CHECK(es2_dfn_advance(&f, 255, &(uint16_t){0}, NULL) == ES2_ERR_NOT_FOUND);
        CHECK(es2_dfn_advance(&f, 31, &(uint16_t){0}, NULL) == ES2_ERR_NOT_FOUND);
        /* FONT2 and BLACK of SHELL1 have five empty codes (249..251, 253, 254). */
        CHECK(zero == (i < 2 ? 5u : 0u));
        printf("%s: %d glyphs (32..%d), height %d, baseline %d, word 0x14 %d, %zu empty\n",
               s->path, f.glyph_count, 31 + f.glyph_count, f.height, f.baseline, f.word_14, zero);
        if (i == 0) *font2 = f;
    }
}

/* <lang>\ESTEXT.BIN (u32 count, then u16 offsets at 8, NUL-terminated strings
 * after them): every byte VSHELL can draw from it has a glyph in FONT2. */
static void estext_coverage(const es2_vol *lang0, const es2_dfn *font2)
{
    static const char *const files[] = {"ENG\\ESTEXT.BIN", "FRE\\ESTEXT.BIN", "GER\\ESTEXT.BIN"};
    static const size_t expected_high[] = {0, 1, 1};
    size_t l;
    for (l = 0; l < 3; ++l) {
        es2_vol_member m;
        uint32_t n = 0, i;
        size_t bytes = 0, high = 0, missing = 0;
        CHECK(es2_vol_find(lang0, files[l], &m, NULL) == ES2_OK);
        if (m.payload.size < 8) { CHECK(0); continue; }
        n = (uint32_t)m.payload.data[0] | ((uint32_t)m.payload.data[1] << 8)
          | ((uint32_t)m.payload.data[2] << 16) | ((uint32_t)m.payload.data[3] << 24);
        CHECK(8 + 2 * (size_t)n <= m.payload.size);
        if (8 + 2 * (size_t)n > m.payload.size) continue;
        for (i = 0; i < n; ++i) {
            size_t at = 8 + 2 * (size_t)n + ((size_t)m.payload.data[8 + 2 * i]
                      | ((size_t)m.payload.data[9 + 2 * i] << 8));
            for (; at < m.payload.size && m.payload.data[at] != 0; ++at) {
                uint16_t adv;
                ++bytes;
                high += m.payload.data[at] >= 0x80;
                missing += es2_dfn_advance(font2, m.payload.data[at], &adv, NULL) != ES2_OK;
            }
        }
        CHECK(missing == 0);
        CHECK((high != 0) == (expected_high[l] != 0));
        printf("%s: %u strings, %zu bytes, %zu above 0x7f, %zu without a glyph\n",
               files[l], n, bytes, high, missing);
    }
}

int main(void)
{
    const char *root = getenv("ES2_DATA_ROOT");
    uint8_t *bytes[13] = {0}; es2_vol vols[13]; es2_vfs_source sources[13];
    size_t i, dfn = 0, hfn = 0, glyphs = 0, widest = 0;
    es2_dfn font2;
    if (root == NULL || *root == 0) { puts("SKIP real dfn: ES2_DATA_ROOT not set"); return 0; }
    for (i = 0; i < 13; ++i) {
        size_t length = 0;
        bytes[i] = read_archive(root, archives[i], &length);
        CHECK(bytes[i] != NULL);
        if (bytes[i] == NULL) goto finish;
        if (es2_vol_open(span(bytes[i], length), &vols[i], NULL) != ES2_OK) { CHECK(0); goto finish; }
        sources[i] = (es2_vfs_source){.kind = ES2_VFS_VOL, .data.vol = &vols[i]};
    }
    corpus(vols, &dfn, &hfn, &glyphs, &widest);
    CHECK(dfn == 36 && hfn == 26 && widest == 60);
    printf("corpus: %zu DFN and %zu HFN members, %zu glyphs, all parsed and contiguous;"
           " word 0x14 = widest glyph in %zu\n", dfn, hfn, glyphs, widest);
    memset(&font2, 0, sizeof font2);
    shell_view(vols, sources, &font2);
    if (font2.glyph_count > 0) estext_coverage(&vols[0], &font2);
finish:
    for (i = 0; i < 13; ++i) free(bytes[i]);
    printf("dfn real data: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
#endif
