/* Es2/shell/draw.h on synthetic bitmaps, DBM/DBA records and DPLs.
 * ref_blit below follows the original blitter's address arithmetic step by
 * step (start pointers and row steps per mirror mode, VSHELL.EXE
 * 0x45a1c3..0x45a353); es2_draw_blit is written as a coordinate mapping, so
 * the sweep compares two derivations. The in-vitro comparison with the
 * original itself is //shell:draw_real_test. */
#include "es2/shell/draw.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)

static void put16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, v & 0xffffu); put16(p + 2, v >> 16); }

static uint32_t rng_state = 0x2601u;
static uint32_t rnd(void)
{
    rng_state = rng_state * 1103515245u + 12345u;
    return rng_state >> 8;
}

/* The listing's arithmetic: start pointers esi/edi and the steps [ebp-0x20]
 * (source) and [ebp-0x1c] (destination) of each mode. */
static void ref_blit(uint8_t *frame, int W, int H, const es2_bitmap *b, int x, int y,
                     const es2_rect *clip, unsigned mirror)
{
    int x0, y0, sx, sy, cols, rows, e, w = b->width, h = b->height;
    long src, dst, src_step, dst_step;
    if (clip) {
        if (x > clip->right) return;
        x0 = x > clip->left ? x : clip->left; sx = x0 - x;
        if (y > clip->bottom) return;
        y0 = y > clip->top ? y : clip->top; sy = y0 - y;
        e = x + w; if (e <= clip->left) return;
        if (e >= clip->right + 1) e = clip->right + 1;
        cols = e - x0;
        e = y + h; if (e <= clip->top) return;
        if (e >= clip->bottom + 1) e = clip->bottom + 1;
        rows = e - y0;
    } else {
        if (x >= W) return;
        x0 = x < 0 ? 0 : x; sx = x0 - x;
        if (y >= H) return;
        y0 = y < 0 ? 0 : y; sy = y0 - y;
        e = x + w; if (e <= 0) return;
        if (e >= W) e = W;
        cols = e - x0;
        e = y + h; if (e <= 0) return;
        if (e >= H) e = H;
        rows = e - y0;
    }
    switch (mirror & 3) {
    case 0:
        src = (long)w * sy + sx; dst = (long)y0 * W + x0;
        src_step = w - cols; dst_step = W - cols; break;
    case 1:
        src = (long)(h - sy - rows) * w + sx; dst = (long)(y0 + rows - 1) * W + x0;
        src_step = w - cols; dst_step = -W - cols; break;
    case 2:
        src = (long)(sy + rows) * w - 1 - sx; dst = (long)(y0 + rows - 1) * W + x0;
        src_step = -w + cols; dst_step = -W - cols; break;
    default:
        src = (long)(h - sy) * w - 1 - sx; dst = (long)y0 * W + x0;
        src_step = -w + cols; dst_step = W - cols; break;
    }
    while (rows-- > 0) {
        int n = cols;
        while (n-- > 0) {
            uint8_t v = b->pixels[src];
            if ((b->flags & 0x10) || v) frame[dst] = v;
            src += (mirror & 2) ? -1 : 1;
            ++dst;
        }
        src += src_step;
        dst += dst_step;
    }
}

static void test_basic(void)
{
    uint8_t frame[8 * 6], pixels[6] = {1, 0, 2, 3, 4, 0};
    es2_frame f = {frame, sizeof frame, 8, 6, 8};
    es2_bitmap b = {2, 3, 0, pixels}; /* 3 wide, 2 high: [1 0 2][3 4 0] */
    memset(frame, 9, sizeof frame);
    CHECK(es2_draw_blit(&f, &b, 1, 1, NULL, ES2_MIRROR_NONE) == ES2_DRAW_DONE);
    CHECK(frame[9] == 1 && frame[10] == 9 && frame[11] == 2);  /* index 0 skipped */
    CHECK(frame[17] == 3 && frame[18] == 4 && frame[19] == 9);
    memset(frame, 9, sizeof frame);
    b.flags = ES2_BITMAP_OPAQUE;
    CHECK(es2_draw_blit(&f, &b, 1, 1, NULL, ES2_MIRROR_NONE) == ES2_DRAW_DONE);
    CHECK(frame[10] == 0 && frame[19] == 0);
    memset(frame, 9, sizeof frame);
    CHECK(es2_draw_blit(&f, &b, 0, 0, NULL, ES2_MIRROR_VERTICAL) == ES2_DRAW_DONE);
    CHECK(frame[0] == 3 && frame[1] == 4 && frame[2] == 0 && frame[8] == 1 && frame[10] == 2);
    memset(frame, 9, sizeof frame);
    CHECK(es2_draw_blit(&f, &b, 0, 0, NULL, ES2_MIRROR_HORIZONTAL) == ES2_DRAW_DONE);
    CHECK(frame[0] == 2 && frame[1] == 0 && frame[2] == 1 && frame[8] == 0 && frame[10] == 3);
    memset(frame, 9, sizeof frame);
    CHECK(es2_draw_blit(&f, &b, 0, 0, NULL, ES2_MIRROR_BOTH | 4u) == ES2_DRAW_DONE); /* & 3 */
    CHECK(frame[0] == 0 && frame[1] == 4 && frame[2] == 3 && frame[8] == 2 && frame[10] == 1);
    /* Clipped mirrored image = clip of the mirrored image: column 0 of the
     * horizontal mirror is source column 2. */
    {
        es2_rect clip = {1, 0, 7, 5};
        memset(frame, 9, sizeof frame);
        CHECK(es2_draw_blit(&f, &b, 0, 0, &clip, ES2_MIRROR_HORIZONTAL) == ES2_DRAW_DONE);
        CHECK(frame[0] == 9 && frame[1] == 0 && frame[2] == 1);
    }
}

static void test_clip_edges(void)
{
    uint8_t frame[10 * 10], pixels[16];
    es2_frame f = {frame, sizeof frame, 10, 10, 10};
    es2_bitmap b = {4, 4, ES2_BITMAP_OPAQUE, pixels};
    es2_rect clip = {2, 3, 6, 7};
    size_t i;
    for (i = 0; i < 16; ++i) pixels[i] = (uint8_t)(i + 1);
    /* inclusive edges: x == r draws one column, x + w == l + 1 one column */
    memset(frame, 0, sizeof frame);
    CHECK(es2_draw_blit(&f, &b, 6, 3, &clip, 0) == ES2_DRAW_DONE);
    CHECK(frame[3 * 10 + 6] == 1 && frame[3 * 10 + 7] == 0);
    CHECK(es2_draw_blit(&f, &b, 7, 3, &clip, 0) == ES2_DRAW_OUTSIDE);
    memset(frame, 0, sizeof frame);
    CHECK(es2_draw_blit(&f, &b, -1, 3, &clip, 0) == ES2_DRAW_DONE);
    CHECK(frame[3 * 10 + 2] == 4 && frame[3 * 10 + 1] == 0);
    CHECK(es2_draw_blit(&f, &b, -2, 3, &clip, 0) == ES2_DRAW_OUTSIDE);
    CHECK(es2_draw_blit(&f, &b, 2, 8, &clip, 0) == ES2_DRAW_OUTSIDE);
    CHECK(es2_draw_blit(&f, &b, 2, -1, &clip, 0) == ES2_DRAW_OUTSIDE);
    memset(frame, 0, sizeof frame);
    CHECK(es2_draw_blit(&f, &b, 2, 7, &clip, 0) == ES2_DRAW_DONE);
    CHECK(frame[7 * 10 + 2] == 1 && frame[8 * 10 + 2] == 0);
    /* frame bounds without a clip */
    CHECK(es2_draw_blit(&f, &b, 10, 0, NULL, 0) == ES2_DRAW_OUTSIDE);
    CHECK(es2_draw_blit(&f, &b, 0, 10, NULL, 0) == ES2_DRAW_OUTSIDE);
    CHECK(es2_draw_blit(&f, &b, -4, 0, NULL, 0) == ES2_DRAW_OUTSIDE);
    CHECK(es2_draw_blit(&f, &b, 0, -4, NULL, 0) == ES2_DRAW_OUTSIDE);
    memset(frame, 0, sizeof frame);
    CHECK(es2_draw_blit(&f, &b, 9, 9, NULL, 0) == ES2_DRAW_DONE);
    CHECK(frame[99] == 1);
    memset(frame, 0, sizeof frame);
    CHECK(es2_draw_blit(&f, &b, -3, -3, NULL, 0) == ES2_DRAW_DONE);
    CHECK(frame[0] == 16 && frame[1] == 0 && frame[10] == 0);
    /* refusals */
    {
        es2_rect bad = {3, 0, 2, 5}, wide = {0, 0, 10, 5}, neg = {-1, 0, 2, 2};
        es2_bitmap empty = {0, 4, 0, pixels};
        es2_frame small = {frame, 99, 10, 10, 10}, narrow = {frame, 100, 10, 10, 9};
        CHECK(es2_draw_blit(&f, &b, 0, 0, &bad, 0) == ES2_DRAW_BAD_CLIP);
        CHECK(es2_draw_blit(&f, &b, 0, 0, &wide, 0) == ES2_DRAW_BAD_CLIP);
        CHECK(es2_draw_blit(&f, &b, 0, 0, &neg, 0) == ES2_DRAW_BAD_CLIP);
        CHECK(es2_draw_blit(&f, &empty, 1, 1, NULL, 0) == ES2_DRAW_EMPTY);
        CHECK(es2_draw_blit(&small, &b, 0, 0, NULL, 0) == ES2_DRAW_BAD_ARGUMENT);
        CHECK(es2_draw_blit(&narrow, &b, 0, 0, NULL, 0) == ES2_DRAW_BAD_ARGUMENT);
        CHECK(es2_draw_blit(NULL, &b, 0, 0, NULL, 0) == ES2_DRAW_BAD_ARGUMENT);
        CHECK(es2_draw_blit(&f, NULL, 0, 0, NULL, 0) == ES2_DRAW_BAD_ARGUMENT);
    }
}

/* Every mirror mode, opaque and keyed, random positions and clips against
 * ref_blit. */
static void test_sweep(void)
{
    enum { W = 37, H = 29 };
    static uint8_t ours[W * H], theirs[W * H], pixels[40 * 40];
    unsigned n, differ = 0;
    es2_frame f = {ours, sizeof ours, W, H, W};
    for (n = 0; n < 20000; ++n) {
        es2_bitmap b;
        es2_rect clip;
        int use_clip = (int)(rnd() % 3), x, y;
        unsigned mirror = rnd() % 8, i;
        b.width = (uint16_t)(1 + rnd() % 40);
        b.height = (uint16_t)(1 + rnd() % 40);
        b.flags = (uint8_t)((rnd() & 1) ? ES2_BITMAP_OPAQUE : 0) | (uint8_t)(rnd() & 0xe0);
        b.pixels = pixels;
        for (i = 0; i < (unsigned)b.width * b.height; ++i)
            pixels[i] = (uint8_t)((rnd() % 4) ? rnd() : 0);
        for (i = 0; i < W * H; ++i) ours[i] = theirs[i] = (uint8_t)(i * 7 + n);
        x = (int)(rnd() % 100) - 50;
        y = (int)(rnd() % 90) - 45;
        clip.left = (int32_t)(rnd() % W); clip.right = clip.left + (int32_t)(rnd() % (W - clip.left));
        clip.top = (int32_t)(rnd() % H); clip.bottom = clip.top + (int32_t)(rnd() % (H - clip.top));
        es2_draw_blit(&f, &b, x, y, use_clip ? &clip : NULL, mirror);
        ref_blit(theirs, W, H, &b, x, y, use_clip ? &clip : NULL, mirror);
        differ += memcmp(ours, theirs, sizeof ours) != 0;
    }
    CHECK(differ == 0);
    printf("sweep: 20000 blits against the listing's arithmetic, %u different\n", differ);
}

static size_t dbm_record(uint8_t *out, unsigned w, unsigned h, unsigned flags, unsigned kind,
                         const uint8_t *data, size_t size)
{
    uint32_t body = 13u + (uint32_t)size;
    put32(out, 0x0028000eu); put32(out + 4, body);
    put16(out + 8, h); put16(out + 10, w);
    out[12] = 8; out[13] = (uint8_t)flags; out[14] = (uint8_t)kind;
    put32(out + 15, (uint32_t)size); put16(out + 19, 0);
    memcpy(out + 21, data, size);
    if (body & 1u) { out[8 + body] = 0; return 9 + body; }
    return 8 + body;
}

static void test_dbm_dba(void)
{
    uint8_t record[64], frame[4 * 4], scratch[16], array[160];
    uint8_t raw[6] = {5, 0, 6, 7, 8, 9};
    /* kind 1: U = 6, run of three 5s, literal 2 bytes, run of one 4 */
    uint8_t rle[] = {6, 0, 0, 0, 0x83, 5, 0x02, 7, 8, 0x81, 4};
    es2_frame f = {frame, sizeof frame, 4, 4, 4};
    es2_dbm image;
    es2_dba dba;
    es2_bitmap b;
    size_t n, at;
    n = dbm_record(record, 3, 2, 0, 0, raw, sizeof raw);
    CHECK(es2_dbm_parse((es2_span){record, n}, &image, NULL) == ES2_OK);
    memset(frame, 1, sizeof frame);
    CHECK(es2_draw_dbm(&f, &image, 1, 1, NULL, 0, NULL, 0) == ES2_DRAW_DONE);
    CHECK(frame[5] == 5 && frame[6] == 1 && frame[7] == 6 && frame[9] == 7);
    n = dbm_record(record, 3, 2, 0, 1, rle, sizeof rle);
    CHECK(es2_dbm_parse((es2_span){record, n}, &image, NULL) == ES2_OK);
    CHECK(es2_draw_bitmap_from_dbm(&image, scratch, 5, &b) == ES2_DRAW_CAPACITY);
    CHECK(es2_draw_bitmap_from_dbm(&image, scratch, sizeof scratch, &b) == ES2_DRAW_DONE);
    CHECK(b.pixels == scratch && scratch[0] == 5 && scratch[2] == 5 && scratch[3] == 7 &&
          scratch[5] == 4);
    memset(frame, 1, sizeof frame);
    CHECK(es2_draw_dbm(&f, &image, 0, 0, NULL, ES2_MIRROR_BOTH, scratch, sizeof scratch) ==
          ES2_DRAW_DONE);
    CHECK(frame[0] == 4 && frame[1] == 8 && frame[2] == 7 && frame[4] == 5);
    /* refusals: kind 3, layout nibble 1, depth 4, kind 1 with a short stream */
    n = dbm_record(record, 3, 2, 0, 3, rle, sizeof rle);
    CHECK(es2_dbm_parse((es2_span){record, n}, &image, NULL) == ES2_OK);
    CHECK(es2_draw_dbm(&f, &image, 0, 0, NULL, 0, scratch, sizeof scratch) == ES2_DRAW_UNSUPPORTED);
    n = dbm_record(record, 3, 2, 1, 0, raw, sizeof raw);
    CHECK(es2_dbm_parse((es2_span){record, n}, &image, NULL) == ES2_OK);
    CHECK(es2_draw_dbm(&f, &image, 0, 0, NULL, 0, scratch, sizeof scratch) == ES2_DRAW_UNSUPPORTED);
    n = dbm_record(record, 3, 2, 0, 0, raw, sizeof raw);
    record[12] = 4;
    CHECK(es2_dbm_parse((es2_span){record, n}, &image, NULL) == ES2_OK);
    CHECK(es2_draw_dbm(&f, &image, 0, 0, NULL, 0, scratch, sizeof scratch) == ES2_DRAW_UNSUPPORTED);
    n = dbm_record(record, 3, 2, 0, 1, rle, sizeof rle - 1);
    CHECK(es2_dbm_parse((es2_span){record, n}, &image, NULL) == ES2_OK);
    CHECK(es2_draw_dbm(&f, &image, 0, 0, NULL, 0, scratch, sizeof scratch) == ES2_DRAW_BAD_IMAGE);
    CHECK(es2_draw_bitmap_from_dbm(NULL, scratch, 6, &b) == ES2_DRAW_BAD_ARGUMENT);
    /* DBA with two members; member 1 is the opaque one */
    put32(array, 0x00280001u);
    at = 12;
    at += dbm_record(array + at, 3, 2, 0, 0, raw, sizeof raw);
    at += dbm_record(array + at, 3, 2, ES2_BITMAP_OPAQUE, 1, rle, sizeof rle);
    put32(array + 4, (uint32_t)(at - 8));
    put32(array + 8, 2);
    CHECK(es2_dba_parse((es2_span){array, at}, &dba, NULL) == ES2_OK);
    memset(frame, 1, sizeof frame);
    CHECK(es2_draw_dba_member(&f, &dba, 0, 0, 0, NULL, 0, scratch, sizeof scratch) == ES2_DRAW_DONE);
    CHECK(frame[0] == 5 && frame[1] == 1);
    CHECK(es2_draw_dba_member(&f, &dba, 1, 1, 2, NULL, ES2_MIRROR_HORIZONTAL, scratch,
                              sizeof scratch) == ES2_DRAW_DONE);
    CHECK(frame[9] == 5 && frame[13] == 4 && frame[14] == 8 && frame[15] == 7);
    CHECK(es2_draw_dba_member(&f, &dba, 2, 0, 0, NULL, 0, scratch, sizeof scratch) ==
          ES2_DRAW_BAD_ARGUMENT);
}

static void test_palette(void)
{
    uint8_t file[8 + 4 + 4 * 256 + 4];
    uint8_t rgb[768];
    es2_dpl dpl;
    size_t n = 8;
    unsigned i;
    put32(file, 0x0028000fu);
    put16(file + n, 256); put16(file + n + 2, 0); n += 4;
    for (i = 0; i < 256; ++i, n += 4) {
        file[n] = (uint8_t)i; file[n + 1] = (uint8_t)(i / 2); file[n + 2] = (uint8_t)(63 - i % 64);
        file[n + 3] = (uint8_t)(i == 5 ? 2 : i == 6 ? 0 : i == 7 ? 0xfc : i == 8 ? 3 : 1);
    }
    put16(file + n, 0); put16(file + n + 2, 0); n += 4;
    put32(file + 4, (uint32_t)(n - 8));
    CHECK(es2_dpl_parse((es2_span){file, n}, &dpl, NULL) == ES2_OK);
    memset(rgb, 0xaa, sizeof rgb);
    CHECK(es2_palette_convert(&dpl, rgb) == 254);
    CHECK(rgb[3] == 4 && rgb[4] == 0 && rgb[5] == 248);       /* (1, 0, 62) << 2 */
    CHECK(rgb[3 * 5] == 5 && rgb[3 * 5 + 1] == 2 && rgb[3 * 5 + 2] == 58); /* bit 1: unscaled */
    CHECK(rgb[3 * 6] == 0xaa && rgb[3 * 7 + 2] == 0xaa);       /* not written */
    CHECK(rgb[3 * 8] == 32);                                   /* bit 0 wins */
    CHECK(rgb[3 * 100] == (uint8_t)(100 << 2));                /* low byte kept */
    /* fewer than 256 colours: the rest is not written */
    put16(file + 8, 2);
    memmove(file + 12 + 8, file + 12 + 1024, 4);
    put32(file + 4, 4 + 8 + 4);
    CHECK(es2_dpl_parse((es2_span){file, 8 + 4 + 8 + 4}, &dpl, NULL) == ES2_OK);
    memset(rgb, 0x55, sizeof rgb);
    CHECK(es2_palette_convert(&dpl, rgb) == 2);
    CHECK(rgb[6] == 0x55 && rgb[767] == 0x55);
    CHECK(es2_palette_convert(NULL, rgb) == -1);
    CHECK(strcmp(es2_palette_file(0), "dpl\\intr_pt1.dpl") == 0);
    CHECK(strcmp(es2_palette_file(19), "dpl\\luna.dpl") == 0);
    CHECK(es2_palette_file(20) == NULL && es2_palette_file(-1) == NULL);
    /* bay screen words (0x43b7f2) */
    CHECK(es2_palette_for_bay_screen(0, 0, 1) == -1 && es2_palette_for_bay_screen(1, 0, 1) == -1);
    CHECK(es2_palette_for_bay_screen(2, 0, 1) == 2 && es2_palette_for_bay_screen(3, 0, 1) == 1);
    CHECK(es2_palette_for_bay_screen(4, 0, 1) == 2 && es2_palette_for_bay_screen(5, 0, 1) == 2);
    CHECK(es2_palette_for_bay_screen(6, 0, 1) == 2 && es2_palette_for_bay_screen(8, 0, 1) == -1);
    CHECK(es2_palette_for_bay_screen(9, 0, 1) == -1 && es2_palette_for_bay_screen(-1, 0, 1) == -1);
    CHECK(es2_palette_for_bay_screen(7, 0, 1) == 3 && es2_palette_for_bay_screen(7, 0, 4) == 3);
    CHECK(es2_palette_for_bay_screen(7, 0, 5) == 4);
    CHECK(es2_palette_for_bay_screen(7, 1, 1) == 5 && es2_palette_for_bay_screen(7, 1, 5) == 9);
    CHECK(es2_palette_for_bay_screen(7, 4, 1) == 10 && es2_palette_for_bay_screen(7, 4, 5) == 14);
    CHECK(es2_palette_for_bay_screen(7, 2, 1) == -1 && es2_palette_for_bay_screen(7, 3, 1) == -1);
}

static void test_background(void)
{
    int32_t i = 0, seen[6] = {0}, k;
    CHECK(strcmp(es2_menu_background_file(0), "dbm\\bay2a_80.dbm") == 0);
    CHECK(strcmp(es2_menu_background_file(4), es2_menu_background_file(5)) == 0);
    CHECK(es2_menu_background_file(6) == NULL);
    for (k = 0; k < 12; ++k) {
        i = es2_menu_background_next(i, ES2_MENU_BACKGROUND_FRAMES, 0);
        ++seen[i];
    }
    CHECK(seen[0] == 2 && seen[5] == 2 && i == 0);
    CHECK(es2_menu_background_next(3, 6, 1) == 3);
    CHECK(es2_menu_background_next(9, 6, 0) == 0);
}

int main(void)
{
    test_basic();
    test_clip_edges();
    test_sweep();
    test_dbm_dba();
    test_palette();
    test_background();
    printf("draw: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
