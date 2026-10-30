#ifndef ES2_SHELL_DRAW_H
#define ES2_SHELL_DRAW_H

/* Images and palettes on VSHELL's 8-bit frame
 * (docs/reverse-engineering/vshell_presentation.md; contract in
 * docs/formats/dbm.md, "images and palettes on the frame"). No Win32, no file
 * I/O, no allocation: the caller owns the frame, the decoded-pixel scratch and
 * the colour table. */

#include <stddef.h>
#include <stdint.h>

#include "es2/formats/dbm.h"
#include "es2/formats/dpl.h"

/* VSHELL's frame: 640 x 480 indices, top-down, stride 640 (0x406434). */
#define ES2_FRAME_WIDTH 640
#define ES2_FRAME_HEIGHT 480

/* A destination: byte (x, y) is pixels[y * stride + x]; size bytes are
 * writable. width/height are the unclipped bounds ([0x47c4a6], [0x47c4aa]). */
typedef struct es2_frame {
    uint8_t *pixels;
    size_t size;
    int32_t width, height, stride;
} es2_frame;

/* Inclusive rectangle, as the blitter's clip argument. */
typedef struct es2_rect {
    int32_t left, top, right, bottom;
} es2_rect;

/* The blitter's view of a bitmap (the 14-byte header at GLBitmap +4):
 * height, width, flags (+5) and width * height decoded pixels, row-major. */
typedef struct es2_bitmap {
    uint16_t height, width;
    uint8_t flags;
    const uint8_t *pixels;
} es2_bitmap;

#define ES2_BITMAP_OPAQUE 0x10u /* flag: copy index 0 as well */

enum {
    ES2_MIRROR_NONE = 0,       /* 0x45a1c3 */
    ES2_MIRROR_VERTICAL = 1,   /* 0x45a205: rows bottom-up */
    ES2_MIRROR_HORIZONTAL = 2, /* 0x45a255: columns right to left */
    ES2_MIRROR_BOTH = 3        /* 0x45a2a1 */
};

typedef enum es2_draw_result {
    ES2_DRAW_DONE = 0,          /* at least one pixel position visited */
    ES2_DRAW_OUTSIDE = 1,       /* clipped away; the original returns too */
    ES2_DRAW_EMPTY = 2,         /* zero width or height (deviation, see dbm.md) */
    ES2_DRAW_BAD_ARGUMENT = -1, /* null pointer, frame larger than its buffer */
    ES2_DRAW_BAD_CLIP = -2,     /* clip not inside the frame or l > r, t > b */
    ES2_DRAW_UNSUPPORTED = -3,  /* depth, layout nibble or compression kind */
    ES2_DRAW_BAD_IMAGE = -4,    /* stored data does not decode to w * h bytes */
    ES2_DRAW_CAPACITY = -5      /* scratch too small for the decoded pixels */
} es2_draw_result;

/* Original: VSHELL.EXE 0x45a09c (graphics-driver slot 26/28). Draws bitmap
 * b with its top-left corner at (x, y): with clip == NULL inside
 * [0, width) x [0, height), else inside the inclusive clip rectangle; the
 * mirror mode is mirror & 3; index 0 is skipped unless b->flags has
 * ES2_BITMAP_OPAQUE. Returns DONE, OUTSIDE or EMPTY, or a negative refusal
 * without touching the frame. */
es2_draw_result es2_draw_blit(const es2_frame *frame, const es2_bitmap *b,
                              int32_t x, int32_t y, const es2_rect *clip,
                              unsigned mirror);

/* The pixels a draw of a DBM record uses (0x4537e8 decompresses per draw
 * when the kind word is nonzero, 0x453a50): kind 0 borrows the stored bytes,
 * kind 1 (RLE) is decoded into scratch. Depth 8 and layout nibble 0 only;
 * kind 3 (LZH) and others are refused (ES2_DRAW_UNSUPPORTED). */
es2_draw_result es2_draw_bitmap_from_dbm(const es2_dbm *image, uint8_t *scratch,
                                         size_t capacity, es2_bitmap *out);

/* es2_draw_bitmap_from_dbm + es2_draw_blit. */
es2_draw_result es2_draw_dbm(const es2_frame *frame, const es2_dbm *image,
                             int32_t x, int32_t y, const es2_rect *clip,
                             unsigned mirror, uint8_t *scratch, size_t capacity);

/* Member index of a DBA/HBA array (the state frames of the layout table's
 * image buttons), drawn as es2_draw_dbm. */
es2_draw_result es2_draw_dba_member(const es2_frame *frame, const es2_dba *array,
                                    size_t index, int32_t x, int32_t y,
                                    const es2_rect *clip, unsigned mirror,
                                    uint8_t *scratch, size_t capacity);

/* Palettes ---------------------------------------------------------------*/

#define ES2_PALETTE_COUNT 20 /* the name table 0x46de14 */

/* "dpl\\intr_pt1.dpl" ... "dpl\\luna.dpl" (0x46de98..), NULL outside 0..19
 * (0x407b62 has no bounds check). */
const char *es2_palette_file(int index);

/* Scene bindings (vshell_presentation.md, "Scene bindings"). */
#define ES2_PALETTE_MAIN 1   /* palette.dpl: start-up, main menu, service bay, REPAIR */
#define ES2_PALETTE_ARMING 2 /* arming.dpl: WEAPONS, BUILD, ARMORY, CREW */

/* Original: VSHELL.EXE 0x43b7f2: the palette the bay background screen gets
 * for a bay screen word (0x4758b8), the mission-screen page (0x481108) and
 * the campaign group (0x46fc58). Returns the table index, or -1 when the word
 * selects no palette (0, 1, 8, out of range, or MISSION on another page). */
int es2_palette_for_bay_screen(int16_t screen_word, int16_t page, int16_t group);

/* Original: VSHELL.EXE 0x4071fe step 2 (0x407236..0x4072a1): the DIB
 * colour table from a DPL, as 256 RGB triples (the form of the capture
 * tour's palette_sha256). Record i with flags bit 0: each component << 2 (low
 * byte kept); else bit 1: the components unchanged; else the entry is not
 * written. Entries the original reads past the DPL's colour count are not
 * written either. Not-written entries keep rgb's previous value (deviation:
 * the original leaves uninitialised stack there). Returns the number of
 * entries written, or -1 for a null argument. */
int es2_palette_convert(const es2_dpl *palette, uint8_t rgb[768]);

/* The animated main-menu background ------------------------------------- */

#define ES2_MENU_BACKGROUND_FRAMES 6     /* 0x430fc4: bay2a_80..84, 84 twice */
#define ES2_MENU_BACKGROUND_PERIOD_MS 500 /* timer 0x46a3f0(service, w, 500, 500, 0) */

/* "dbm\\bay2a_80.dbm" ... for 0..5 (strings 0x4736e5..0x47373a), else NULL. */
const char *es2_menu_background_file(int index);

/* Original: VSHELL.EXE 0x40cf31..0x40cf49: on each timer event the frame
 * index advances modulo the count unless the window's +0x11 bit 1 is set. */
int32_t es2_menu_background_next(int32_t index, int32_t count, int frozen);

#endif
