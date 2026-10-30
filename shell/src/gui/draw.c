/* Images and palettes on VSHELL's 8-bit frame. Contract:
 * docs/formats/dbm.md, "images and palettes on the frame";
 * docs/reverse-engineering/vshell_presentation.md. */
#include "es2/shell/draw.h"

#include <string.h>

/* Original: VSHELL.EXE 0x45a09c. The original computes, for the visible span
 * x0..x0+cols-1 and y0..y0+rows-1 (inclusive clip, 0x45a0b7..0x45a127, or
 * the frame bounds, 0x45a12c..0x45a19c), a source start and row steps per
 * mirror mode (jump table 0x45a1b3); written here as the mapping each mode
 * produces: drawn image position (c, r) = (x0 - x + i, y0 - y + j) reads
 * source column c (or w - 1 - c with bit 1) of source row r (or h - 1 - r
 * with bit 0). Index 0 is skipped unless the flag 0x10 is set
 * (0x45a1f5, 0x45a31d..0x45a353). Rows and columns are visited in a
 * different order than the original for modes 1 and 2; source and frame
 * never overlap, so the written bytes are the same. */
es2_draw_result es2_draw_blit(const es2_frame *frame, const es2_bitmap *b,
                              int32_t x, int32_t y, const es2_rect *clip,
                              unsigned mirror)
{
    int64_t left, top, right, bottom; /* inclusive bounds of the drawn span */
    int64_t x0, y0, x1, y1, cols, rows, i, j;
    int64_t w, h;
    int opaque;
    if (frame == NULL || b == NULL || frame->pixels == NULL || frame->width <= 0 ||
        frame->height <= 0 || frame->stride < frame->width ||
        (uint64_t)frame->stride * (uint64_t)(frame->height - 1) + (uint64_t)frame->width >
            (uint64_t)frame->size)
        return ES2_DRAW_BAD_ARGUMENT;
    if (clip != NULL) {
        if (clip->left > clip->right || clip->top > clip->bottom || clip->left < 0 ||
            clip->top < 0 || clip->right >= frame->width || clip->bottom >= frame->height)
            return ES2_DRAW_BAD_CLIP;
        left = clip->left; top = clip->top; right = clip->right; bottom = clip->bottom;
    } else {
        left = 0; top = 0; right = (int64_t)frame->width - 1; bottom = (int64_t)frame->height - 1;
    }
    w = b->width;
    h = b->height;
    /* The rejections in the original's order: x > r, y > b, x + w <= l,
     * y + h <= t (with the frame bounds: x >= W, y >= H, x + w <= 0,
     * y + h <= 0). */
    if (x > right || y > bottom || x + w <= left || y + h <= top)
        return ES2_DRAW_OUTSIDE;
    if (w == 0 || h == 0)
        return ES2_DRAW_EMPTY;
    if (b->pixels == NULL)
        return ES2_DRAW_BAD_ARGUMENT;
    x0 = x > left ? x : left;
    y0 = y > top ? y : top;
    x1 = x + w < right + 1 ? x + w : right + 1;
    y1 = y + h < bottom + 1 ? y + h : bottom + 1;
    cols = x1 - x0;
    rows = y1 - y0;
    opaque = (b->flags & ES2_BITMAP_OPAQUE) != 0;
    for (j = 0; j < rows; ++j) {
        int64_t r = y0 - y + j;
        int64_t sr = (mirror & 1u) ? h - 1 - r : r;
        const uint8_t *src = b->pixels + sr * w;
        uint8_t *dst = frame->pixels + (y0 + j) * (int64_t)frame->stride + x0;
        for (i = 0; i < cols; ++i) {
            int64_t c = x0 - x + i;
            uint8_t v = src[(mirror & 2u) ? w - 1 - c : c];
            if (opaque || v != 0)
                dst[i] = v;
        }
    }
    return ES2_DRAW_DONE;
}

/* Original: VSHELL.EXE 0x4537e8 (kind word +0x12 nonzero: 0x453a50 first;
 * kinds 1 RLE, 3 LZH, others return 0) and the layout dispatch
 * 0x458f40..0x458f50 (nibble 1 goes to 0x459508, not reconstructed). */
es2_draw_result es2_draw_bitmap_from_dbm(const es2_dbm *image, uint8_t *scratch,
                                         size_t capacity, es2_bitmap *out)
{
    size_t needed;
    es2_status status;
    if (image == NULL || out == NULL)
        return ES2_DRAW_BAD_ARGUMENT;
    if (image->bits_per_pixel != 8 || (image->flags & 15u) != 0 ||
        (image->compression != 0 && image->compression != 1))
        return ES2_DRAW_UNSUPPORTED;
    needed = (size_t)image->width * image->height;
    out->height = image->height;
    out->width = image->width;
    out->flags = image->flags;
    if (image->compression == 0) {
        if (image->pixels.size != needed)
            return ES2_DRAW_BAD_IMAGE;
        out->pixels = image->pixels.data;
        return ES2_DRAW_DONE;
    }
    if (capacity < needed)
        return ES2_DRAW_CAPACITY;
    status = es2_dbm_decode(image, scratch, capacity, NULL, NULL, NULL);
    if (status == ES2_ERR_CAPACITY)
        return ES2_DRAW_CAPACITY;
    if (status != ES2_OK)
        return status == ES2_ERR_ARGUMENT ? ES2_DRAW_BAD_ARGUMENT : ES2_DRAW_BAD_IMAGE;
    out->pixels = scratch;
    return ES2_DRAW_DONE;
}

es2_draw_result es2_draw_dbm(const es2_frame *frame, const es2_dbm *image,
                             int32_t x, int32_t y, const es2_rect *clip,
                             unsigned mirror, uint8_t *scratch, size_t capacity)
{
    es2_bitmap b;
    es2_draw_result result = es2_draw_bitmap_from_dbm(image, scratch, capacity, &b);
    if (result != ES2_DRAW_DONE)
        return result;
    return es2_draw_blit(frame, &b, x, y, clip, mirror);
}

es2_draw_result es2_draw_dba_member(const es2_frame *frame, const es2_dba *array,
                                    size_t index, int32_t x, int32_t y,
                                    const es2_rect *clip, unsigned mirror,
                                    uint8_t *scratch, size_t capacity)
{
    es2_dbm image;
    if (array == NULL)
        return ES2_DRAW_BAD_ARGUMENT;
    if (es2_dba_image_at(array, index, &image, NULL) != ES2_OK)
        return ES2_DRAW_BAD_ARGUMENT;
    return es2_draw_dbm(frame, &image, x, y, clip, mirror, scratch, capacity);
}

/* Palettes ---------------------------------------------------------------*/

/* Original: VSHELL.EXE 0x46de14 (pointers) -> 0x46de98.. (names). */
static const char *const palette_files[ES2_PALETTE_COUNT] = {
    "dpl\\intr_pt1.dpl", "dpl\\palette.dpl", "dpl\\arming.dpl", "dpl\\cam_er.dpl",
    "dpl\\cam_moon.dpl", "dpl\\br_w1.dpl", "dpl\\br_w2.dpl", "dpl\\br_w3.dpl",
    "dpl\\br_w4.dpl", "dpl\\br_w5.dpl", "dpl\\db_w1.dpl", "dpl\\db_w2.dpl",
    "dpl\\db_w3.dpl", "dpl\\db_w4.dpl", "dpl\\db_w5.dpl", "dpl\\alph.dpl",
    "dpl\\delt.dpl", "dpl\\omic.dpl", "dpl\\brav.dpl", "dpl\\luna.dpl"};

const char *es2_palette_file(int index)
{
    return index >= 0 && index < ES2_PALETTE_COUNT ? palette_files[index] : NULL;
}

/* Original: VSHELL.EXE 0x43b7f2 (jump table 0x43b80a; words 0 and 1 change
 * nothing, 8 hides the frame 0x48d520, above 8 unsigned nothing). The index
 * goes through 0x43a430 as a 16-bit word. */
int es2_palette_for_bay_screen(int16_t screen_word, int16_t page, int16_t group)
{
    int16_t g = (int16_t)(group - 1); /* 0x43b877..0x43b87e */
    switch (screen_word) {
    case 2: case 4: case 5: case 6:
        return ES2_PALETTE_ARMING;
    case 3:
        return ES2_PALETTE_MAIN;
    case 7:
        if (page == 0)
            return g >= 4 ? 4 : 3;               /* cam_moon / cam_er */
        if (page == 1)
            return (int16_t)(g + 5);              /* br_w<group> */
        if (page == 4)
            return (int16_t)(g + 10);             /* db_w<group> */
        return -1;
    default:
        return -1;
    }
}

/* Original: VSHELL.EXE 0x407236..0x4072a1. */
int es2_palette_convert(const es2_dpl *palette, uint8_t rgb[768])
{
    int written = 0;
    size_t i;
    if (palette == NULL || rgb == NULL)
        return -1;
    for (i = 0; i < 256; ++i) {
        es2_dpl_colour c;
        if (es2_dpl_colour_at(palette, i, &c, NULL) != ES2_OK)
            continue;
        if (c.flags & 1u) {
            rgb[3 * i] = (uint8_t)(c.red << 2);
            rgb[3 * i + 1] = (uint8_t)(c.green << 2);
            rgb[3 * i + 2] = (uint8_t)(c.blue << 2);
        } else if (c.flags & 2u) {
            rgb[3 * i] = c.red;
            rgb[3 * i + 1] = c.green;
            rgb[3 * i + 2] = c.blue;
        } else {
            continue;
        }
        ++written;
    }
    return written;
}

/* The animated main-menu background --------------------------------------*/

/* Original: VSHELL.EXE 0x430fc4, names 0x4736e5, 0x4736f6, 0x473707,
 * 0x473718, 0x473729, 0x47373a (the last two both bay2a_84). */
static const char *const background_files[ES2_MENU_BACKGROUND_FRAMES] = {
    "dbm\\bay2a_80.dbm", "dbm\\bay2a_81.dbm", "dbm\\bay2a_82.dbm",
    "dbm\\bay2a_83.dbm", "dbm\\bay2a_84.dbm", "dbm\\bay2a_84.dbm"};

const char *es2_menu_background_file(int index)
{
    return index >= 0 && index < ES2_MENU_BACKGROUND_FRAMES ? background_files[index] : NULL;
}

int32_t es2_menu_background_next(int32_t index, int32_t count, int frozen)
{
    if (frozen)
        return index;
    if (index == INT32_MAX || index + 1 >= count)
        return 0;
    return index + 1;
}
