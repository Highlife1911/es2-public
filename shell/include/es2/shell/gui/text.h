#ifndef ES2_SHELL_GUI_TEXT_H
#define ES2_SHELL_GUI_TEXT_H

#include "es2/formats/dfn.h"
#include "es2/shell/texts.h"

/* VSHELL's text drawing (docs/reverse-engineering/vshell_text.md and its
 * sections "Own implementation" and "In-vitro acceptance"). Glyphs come from a
 * parsed DFN font (//formats:dfn):
 * byte b of a string draws glyph b - first_code as stored (DOS code page),
 * index 0 transparent, every other index written as stored, advance = glyph
 * width, top row at y - baseline. No shadow, no underline (every VSHELL label
 * passes underline 0). The font primitives take no colour: labels and edit
 * fields recolour index 41 inside their rectangle after drawing.
 *
 * Coordinates are frame coordinates; a widget rectangle is inclusive (l, t,
 * r, b) as VSHELL's +0x25..+0x31, and its draw clips to that rectangle
 * (0x41fbd9: clip mode 1, offset mode 1, origin 0). No Win32, no file I/O. */

typedef struct es2_shell_text_frame {
    uint8_t *pixels;   /* palette indices, rows top first */
    int32_t width, height, stride;
} es2_shell_text_frame;

typedef struct es2_shell_text_rect {
    int32_t l, t, r, b;   /* inclusive */
} es2_shell_text_rect;

enum {
    ES2_SHELL_ALIGN_LEFT = 0,     /* context +0x231 */
    ES2_SHELL_ALIGN_RIGHT = 1,
    ES2_SHELL_ALIGN_CENTRE = 2,
    ES2_SHELL_TEXT_INK = 41       /* the index the label and edit draws recolour */
};

/* ---- primitives (src32\GL_FONT.CPP) -------------------------------------- */

/* getWidth 0x4010fd for byte c. Outside [first_code, first_code + count)
 * VSHELL reads its width table out of bounds (undefined); documented choice:
 * the advance of '?', the glyph its draw substitutes (0 if '?' is missing
 * too). A font with a character map counts as having no glyphs. */
int32_t es2_shell_glyph_advance(const es2_dfn *font, uint8_t c);
/* 1 if byte c has its own glyph (no substitution). */
int es2_shell_glyph_present(const es2_dfn *font, uint8_t c);
/* 0x4011af: sum of the advances up to the NUL. */
int32_t es2_shell_string_width(const es2_dfn *font, const char *text);
/* The 256 advances as es2_shell_report_layout takes them (clamped to 255). */
void es2_shell_font_widths(const es2_dfn *font, uint8_t widths[256]);

/* Glyph draw 0x4011f6 at pen (x, y): top row y - baseline; '?' for a byte
 * without a glyph (0x401293); clipped to `clip` (inclusive, the blitter
 * 0x45a09c) and, beyond VSHELL, to the frame. */
void es2_shell_glyph_draw(const es2_shell_text_frame *frame, const es2_shell_text_rect *clip,
                          const es2_dfn *font, int32_t x, int32_t y, uint8_t c);
/* String draw 0x401355 with the context's alignment and width: right adds
 * width - textwidth, centre (width - textwidth) >> 1 (arithmetic), any other
 * value is left; then glyph by glyph, x += advance. */
void es2_shell_string_draw(const es2_shell_text_frame *frame, const es2_shell_text_rect *clip,
                           const es2_dfn *font, int32_t x, int32_t y, const char *text,
                           int32_t align, int32_t width);
/* Rectangle fill 0x4576a4 as text uses it: `rect` intersected with `clip`
 * (0x457914; nothing if empty), then kind 0 (solid colour, driver slot 2)
 * or the kind-6 table that maps index 41 to `colour` and keeps every other
 * index (slot 3). Rectangles are inclusive. */
void es2_shell_fill(const es2_shell_text_frame *frame, const es2_shell_text_rect *clip,
                    es2_shell_text_rect rect, uint8_t colour);
void es2_shell_recolour_ink(const es2_shell_text_frame *frame, const es2_shell_text_rect *clip,
                            es2_shell_text_rect rect, uint8_t colour);

/* ---- label (ESLabel 0x40b828, 0xc9 bytes) -------------------------------- */
#define ES2_SHELL_LABEL_TEXT 100
typedef struct es2_shell_label {
    es2_shell_text_rect rect;       /* +0x25 */
    const es2_dfn *font;            /* +0xb1 (NULL: no text drawn) */
    char text[ES2_SHELL_LABEL_TEXT + 1];   /* +0x4d */
    int32_t align;                  /* +0x45 */
    uint8_t colour;                 /* +0xb5 */
    int background;                 /* +0xc1 */
    uint8_t background_colour;      /* +0xc5, 16 from the constructor */
    int no_draw;                    /* bit 0x02 of +0x11 */
} es2_shell_label;
/* Constructor 0x40b828 (background colour 16, underline 0) and setText. */
es2_status es2_shell_label_init(es2_shell_label *label, es2_shell_text_rect rect, const es2_dfn *font,
    const char *text, int32_t align, uint8_t colour, int background, es2_error *error);
/* setText 0x40b8bb: NULL is the empty text. Refused: 100 bytes or more
 * (above 100 VSHELL asserts; exactly 100 writes the NUL over the low byte of
 * its font pointer, which is not reproduced). */
es2_status es2_shell_label_set_text(es2_shell_label *label, const char *text, int32_t align,
    uint8_t colour, es2_error *error);
/* Draw 0x40b9e9(label, y): background fill (0,1)-(w-1,h-1); text with
 * context width w and the label's alignment at pen (0, y - 1), or for
 * y = -1 at (0, (h + 1) - (h + 1 - H) / 2 - 3); then index 41 -> colour over
 * (0,1)-(w,h-1), also without text. Relative to the rectangle's top left. */
void es2_shell_label_draw(const es2_shell_text_frame *frame, const es2_shell_label *label, int32_t y);

/* ---- buttons with text (0x40a004, 0x40a2c4) ------------------------------- */
enum {
    ES2_SHELL_BUTTON_TEXT = 0,   /* 0x40a004: child label (1,0)-(w,h) */
    ES2_SHELL_BUTTON_BAR = 1     /* 0x40a2c4 (and 0x40a6b0): child label (0,0)-(w,h) */
};
/* The button's text: a centred label of colour 41 drawn with y =
 * (H + h + 1) / 2 + 1 when pressed (both state words set), else - 1; h of
 * the button. Refused: kind, text as es2_shell_label_set_text. */
es2_status es2_shell_button_text_draw(const es2_shell_text_frame *frame, int kind, es2_shell_text_rect rect,
    const es2_dfn *font, const char *text, int pressed, es2_error *error);

/* ---- edit field (ESDialog 0x40c1a4, 0xc3 bytes) -------------------------- */
#define ES2_SHELL_EDIT_TEXT 90
/* The accepted-character sets of VSHELL (C strings as in the image). */
extern const char es2_shell_edit_set_capitals[];   /* 0x46ee48, the constructor's default */
extern const char es2_shell_edit_set_slots[];      /* 0x475869 (Saved games), 0x47a4e1 (DEBUG) */
extern const char es2_shell_edit_set_names[];      /* 0x4761c3 (name entry), 0x476205 (registration) */
/* Widths r - l of VSHELL's edit fields (the layout table). */
enum {
    ES2_SHELL_EDIT_WIDTH_SLOT_EDGE = 325,      /* Saved games rows 0 and 9: (12,..)-(337,..) */
    ES2_SHELL_EDIT_WIDTH_SLOT = 327,           /* rows 1..8: (10,..)-(337,..) */
    ES2_SHELL_EDIT_WIDTH_REGISTRATION = 206    /* registration.07: (1,1)-(207,17) */
};
typedef struct es2_shell_edit {
    es2_shell_text_rect rect;          /* +0x25 */
    const es2_dfn *font;               /* +0xa3 */
    char text[ES2_SHELL_EDIT_TEXT];    /* +0x45 */
    int32_t length;                    /* +0xab */
    int32_t width;                     /* +0xaf, pixels */
    const char *allowed;               /* +0x9f, NULL accepts every character */
    int32_t prefix;                    /* +0xb7, backspace keeps this many */
    uint8_t colour;                    /* +0xbb */
    int active;                        /* +0xa7 */
    int blink;                         /* +0xb3 */
} es2_shell_edit;
/* Constructor defaults: empty text, the capitals set, blink 1, prefix 0,
 * colour 41, not active. */
void es2_shell_edit_init(es2_shell_edit *edit, es2_shell_text_rect rect, const es2_dfn *font);
/* setText 0x40c294: refused NULL and 90 bytes or more (fatal assertions). */
es2_status es2_shell_edit_set_text(es2_shell_edit *edit, const char *text, es2_error *error);
/* Insert 0x40c382 (the width rule): the character word c must be in the set
 * (0x40c237: the word against each sign-extended set byte up to and
 * including the terminator, so 0 always passes), length + 1 < 90 and
 * width + advance(c) + 6 < r - l (signed); the low byte is appended.
 * Returns 1 when accepted (VSHELL then redraws). */
int es2_shell_edit_insert(es2_shell_edit *edit, uint16_t c);
/* Backspace 0x40c406: only while length > 0 and length > prefix. */
int es2_shell_edit_backspace(es2_shell_edit *edit);
/* Every byte of `typed` inserted in turn. Refused at the first byte the
 * field does not take: ES2_ERR_STRING (not in the set) or ES2_ERR_LENGTH
 * (length or width rule), error offset = its index; the field keeps the
 * bytes taken before it. */
es2_status es2_shell_edit_type(es2_shell_edit *edit, const char *typed, es2_error *error);
/* Draw 0x40c6ff: fill colour 16 over (0,0)-(w,h); text at pen
 * (1, H/2 + (h+1)/2), left; caret (active and blink) colour 39 over
 * (width, y-2)-(width+6, y); index 41 -> colour over (1,1)-(w-1,h-1). */
void es2_shell_edit_draw(const es2_shell_text_frame *frame, const es2_shell_edit *edit);

/* ---- text pages (ESREPORT, 0x40d231/0x40d492) ---------------------------- */
/* Page `page` of a laid-out panel (es2_shell_report_layout with the font's
 * widths and width r - l): line i shown when i / rows == page, as a label
 * (l, t + j*H)-(r, t + j*H + H), j = i mod rows, rows = (b - t) / H, align 0,
 * colour 40, background on (16), drawn with y = -1, in line order. Refused:
 * rows not positive, any line of 100 bytes or more (VSHELL's setText of
 * that line asserts while the panel is built, whatever the page). */
es2_status es2_shell_report_draw(const es2_shell_text_frame *frame, es2_shell_text_rect panel,
    const es2_dfn *font, const es2_shell_report *report, int32_t page, es2_error *error);

#endif
