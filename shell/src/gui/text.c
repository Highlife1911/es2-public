/* VSHELL's text drawing (docs/reverse-engineering/vshell_text.md). Every
 * function names the original it follows. */
#include "es2/shell/gui/text.h"

#include <string.h>

const char es2_shell_edit_set_capitals[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ ";
const char es2_shell_edit_set_slots[] = "^0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ ";
const char es2_shell_edit_set_names[] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ ";

static int in_font(const es2_dfn *font, uint8_t c) {
    /* 0x401132..0x401146: F <= c < F + N on the zero-extended byte. */
    return font && font->first_code != -1 && c >= font->first_code && (int32_t)c < font->first_code + font->glyph_count;
}

int es2_shell_glyph_present(const es2_dfn *font, uint8_t c) {
    es2_dfn_glyph g;
    return in_font(font, c) && es2_dfn_glyph_at(font, c, &g, NULL) == ES2_OK;
}

/* Original: VSHELL.EXE 0x4010fd (getWidth). */
int32_t es2_shell_glyph_advance(const es2_dfn *font, uint8_t c) {
    uint16_t advance;
    if (!font) return 0;
    /* 0x401186..0x4011a6: a fixed width answers every byte, in range or not. */
    if (font->fixed_width != -1) return font->fixed_width;
    if (!in_font(font, c)) {
        /* Out of range VSHELL reads outside its width table; our choice is
         * the advance of the '?' it draws instead (vshell_text.md). */
        if (c == '?' || !in_font(font, '?')) return 0;
        c = '?';
    }
    return es2_dfn_advance(font, c, &advance, NULL) == ES2_OK ? advance : 0;
}

/* Original: VSHELL.EXE 0x4011af. */
int32_t es2_shell_string_width(const es2_dfn *font, const char *text) {
    int32_t sum = 0;
    if (!text) return 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) sum += es2_shell_glyph_advance(font, *p);
    return sum;
}

void es2_shell_font_widths(const es2_dfn *font, uint8_t widths[256]) {
    for (int c = 0; c < 256; c++) {
        int32_t a = es2_shell_glyph_advance(font, (uint8_t)c);
        widths[c] = (uint8_t)(a < 0 ? 0 : a > 255 ? 255 : a);
    }
}

/* The clip of a draw: the widget rectangle, and the frame as a guard. */
static int bounds(const es2_shell_text_frame *f, const es2_shell_text_rect *clip, es2_shell_text_rect *out) {
    es2_shell_text_rect r = {0, 0, f->width - 1, f->height - 1};
    if (!f->pixels || f->width <= 0 || f->height <= 0) return 0;
    if (clip) {
        if (clip->l > r.l) r.l = clip->l;
        if (clip->t > r.t) r.t = clip->t;
        if (clip->r < r.r) r.r = clip->r;
        if (clip->b < r.b) r.b = clip->b;
    }
    *out = r;
    return r.l <= r.r && r.t <= r.b;
}

/* Original: VSHELL.EXE 0x4011f6 (glyph draw) through 0x458ee4 and the
 * blitter 0x45a09c (index 0 transparent: the header flags have bit 0x10
 * clear, 0x4012b9..0x4012bd). */
void es2_shell_glyph_draw(const es2_shell_text_frame *frame, const es2_shell_text_rect *clip,
                          const es2_dfn *font, int32_t x, int32_t y, uint8_t c) {
    es2_shell_text_rect k;
    es2_dfn_glyph g;
    int32_t top;
    if (!frame || !font || !bounds(frame, clip, &k)) return;
    if (!in_font(font, c)) c = '?';   /* 0x401293 */
    if (es2_dfn_glyph_at(font, c, &g, NULL) != ES2_OK) return;
    top = y - font->baseline;          /* 0x4012ee */
    for (int32_t row = 0; row < g.height; row++) {
        int32_t py = top + row;
        if (py < k.t || py > k.b) continue;
        for (int32_t col = 0; col < g.width; col++) {
            int32_t px = x + col;
            uint8_t v = g.pixels.data[(size_t)row * g.width + (size_t)col];
            if (px < k.l || px > k.r || !v) continue;
            frame->pixels[(size_t)py * (size_t)frame->stride + (size_t)px] = v;
        }
    }
}

/* Original: VSHELL.EXE 0x401355 (string draw). */
void es2_shell_string_draw(const es2_shell_text_frame *frame, const es2_shell_text_rect *clip,
                           const es2_dfn *font, int32_t x, int32_t y, const char *text,
                           int32_t align, int32_t width) {
    if (!text || !font) return;
    if (align == ES2_SHELL_ALIGN_RIGHT) x += width - es2_shell_string_width(font, text);          /* 0x4013ba */
    else if (align == ES2_SHELL_ALIGN_CENTRE) x += (width - es2_shell_string_width(font, text)) >> 1;   /* sar, 0x4013ff */
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        es2_shell_glyph_draw(frame, clip, font, x, y, *p);
        x += es2_shell_glyph_advance(font, *p);   /* getWidth of the original byte, 0x401421 */
    }
}

/* Original: VSHELL.EXE 0x4576a4 kinds 0 and 6: the clip intersection 0x457914
 * and the driver fills 0x45abfc / 0x45ac84 (inclusive rectangles). */
static void fill_kind(const es2_shell_text_frame *frame, const es2_shell_text_rect *clip, es2_shell_text_rect r,
                      int recolour, uint8_t colour) {
    es2_shell_text_rect k;
    if (!frame || !bounds(frame, clip, &k)) return;
    if (r.l < k.l) r.l = k.l;
    if (r.r > k.r) r.r = k.r;
    if (r.t < k.t) r.t = k.t;
    if (r.b > k.b) r.b = k.b;
    if (r.l > r.r || r.t > r.b) return;
    for (int32_t y = r.t; y <= r.b; y++) {
        uint8_t *p = frame->pixels + (size_t)y * (size_t)frame->stride;
        for (int32_t x = r.l; x <= r.r; x++)
            if (!recolour) p[x] = colour;
            else if (p[x] == ES2_SHELL_TEXT_INK) p[x] = colour;
    }
}
void es2_shell_fill(const es2_shell_text_frame *frame, const es2_shell_text_rect *clip,
                    es2_shell_text_rect rect, uint8_t colour) {
    fill_kind(frame, clip, rect, 0, colour);
}
void es2_shell_recolour_ink(const es2_shell_text_frame *frame, const es2_shell_text_rect *clip,
                            es2_shell_text_rect rect, uint8_t colour) {
    fill_kind(frame, clip, rect, 1, colour);
}

/* The callers' min/max normalisation of a relative rectangle
 * (0x40ba5b..0x40baa0 and the like), moved to the widget's corner. */
static es2_shell_text_rect normalised(const es2_shell_text_rect *w, int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
    es2_shell_text_rect r;
    r.l = w->l + (x0 < x1 ? x0 : x1);
    r.r = w->l + (x0 < x1 ? x1 : x0);
    r.t = w->t + (y0 < y1 ? y0 : y1);
    r.b = w->t + (y0 < y1 ? y1 : y0);
    return r;
}

/* Original: VSHELL.EXE 0x40b8bb (setText). */
es2_status es2_shell_label_set_text(es2_shell_label *label, const char *text, int32_t align, uint8_t colour,
                                    es2_error *e) {
    size_t n;
    if (!label) return es2_fail(e, ES2_ERR_ARGUMENT, 0);
    n = text ? strlen(text) : 0;
    if (n >= ES2_SHELL_LABEL_TEXT) return es2_fail(e, ES2_ERR_LENGTH, n);
    label->align = align;
    label->colour = colour;
    memcpy(label->text, text ? text : "", n);
    label->text[n] = 0;
    es2_error_clear(e);
    return ES2_OK;
}

/* Original: VSHELL.EXE 0x40b828 (constructor). */
es2_status es2_shell_label_init(es2_shell_label *label, es2_shell_text_rect rect, const es2_dfn *font,
                                const char *text, int32_t align, uint8_t colour, int background, es2_error *e) {
    if (!label) return es2_fail(e, ES2_ERR_ARGUMENT, 0);
    memset(label, 0, sizeof *label);
    label->rect = rect;
    label->font = font;
    label->background = background != 0;
    label->background_colour = 16;
    return es2_shell_label_set_text(label, text, align, colour, e);
}

/* Original: VSHELL.EXE 0x40b9e9 (draw). */
void es2_shell_label_draw(const es2_shell_text_frame *frame, const es2_shell_label *label, int32_t y) {
    const es2_shell_text_rect *r;
    int32_t w, h;
    if (!frame || !label || label->no_draw) return;   /* 0x40b9fb */
    r = &label->rect;
    w = r->r - r->l;
    h = r->b - r->t;
    if (label->background)                            /* 0x40ba12..0x40baa7 */
        es2_shell_fill(frame, r, normalised(r, 0, 1, w - 1, h - 1), label->background_colour);
    if (label->text[0] && label->font) {              /* 0x40baad..0x40bb76 */
        int32_t pen;
        if (y == -1) {
            int32_t H = label->font->height;
            pen = (h + 1) - (h + 1 - H) / 2 - 3;
        } else {
            pen = y - 1;
        }
        es2_shell_string_draw(frame, r, label->font, r->l, r->t + pen, label->text, label->align, w);
    }
    es2_shell_recolour_ink(frame, r, normalised(r, 0, 1, w, h - 1), label->colour);   /* 0x40bb79..0x40bc3b */
}

/* Original: VSHELL.EXE 0x40a004 / 0x40a2c4 (child label) and their draws
 * 0x40a240..0x40a2b3 / 0x40a631..0x40a6a4. */
es2_status es2_shell_button_text_draw(const es2_shell_text_frame *frame, int kind, es2_shell_text_rect rect,
                                      const es2_dfn *font, const char *text, int pressed, es2_error *e) {
    es2_shell_label label;
    es2_shell_text_rect child = rect;
    es2_status s;
    int32_t h = rect.b - rect.t, y;
    if (kind != ES2_SHELL_BUTTON_TEXT && kind != ES2_SHELL_BUTTON_BAR) return es2_fail(e, ES2_ERR_ARGUMENT, 0);
    if (kind == ES2_SHELL_BUTTON_TEXT) child.l += 1;
    if ((s = es2_shell_label_init(&label, child, font, text, ES2_SHELL_ALIGN_CENTRE, ES2_SHELL_TEXT_INK, 0, e)) != ES2_OK)
        return s;
    if (!font) return ES2_OK;   /* 0x40a249: no font, no label draw */
    y = (font->height + h + 1) / 2 + (pressed ? 1 : -1);
    es2_shell_label_draw(frame, &label, y);
    return ES2_OK;
}

/* Original: VSHELL.EXE 0x40c1a4 (constructor defaults). */
void es2_shell_edit_init(es2_shell_edit *edit, es2_shell_text_rect rect, const es2_dfn *font) {
    if (!edit) return;
    memset(edit, 0, sizeof *edit);
    edit->rect = rect;
    edit->font = font;
    edit->allowed = es2_shell_edit_set_capitals;
    edit->blink = 1;
    edit->colour = ES2_SHELL_TEXT_INK;
}

/* Original: VSHELL.EXE 0x40c294 (setText). */
es2_status es2_shell_edit_set_text(es2_shell_edit *edit, const char *text, es2_error *e) {
    size_t n;
    if (!edit || !text) return es2_fail(e, ES2_ERR_ARGUMENT, 0);
    n = strlen(text);
    if (n >= ES2_SHELL_EDIT_TEXT) return es2_fail(e, ES2_ERR_LENGTH, n);
    memcpy(edit->text, text, n + 1);
    edit->length = (int32_t)n;
    edit->width = 0;
    for (size_t i = 0; i < n; i++) edit->width += es2_shell_glyph_advance(edit->font, (uint8_t)text[i]);
    es2_error_clear(e);
    return ES2_OK;
}

/* Original: VSHELL.EXE 0x40c237 (the set test). */
static int accepted(const es2_shell_edit *edit, uint16_t c) {
    size_t i = 0;
    if (!edit->allowed) return 1;
    while (edit->allowed[i] && c != (int32_t)(signed char)edit->allowed[i]) i++;
    return c == (int32_t)(signed char)edit->allowed[i];
}

/* Original: VSHELL.EXE 0x40c382 (insert; the width rule 0x40c39b..0x40c3c7). */
static int insert(es2_shell_edit *edit, uint16_t c, es2_status *why) {
    int32_t advance;
    if (!accepted(edit, c)) { *why = ES2_ERR_STRING; return 0; }
    if (edit->length + 1 >= ES2_SHELL_EDIT_TEXT) { *why = ES2_ERR_LENGTH; return 0; }
    advance = es2_shell_glyph_advance(edit->font, (uint8_t)c);
    if (advance + edit->width + 6 >= edit->rect.r - edit->rect.l) { *why = ES2_ERR_LENGTH; return 0; }
    edit->width += advance;
    edit->text[edit->length++] = (char)(uint8_t)c;
    edit->text[edit->length] = 0;
    return 1;
}
int es2_shell_edit_insert(es2_shell_edit *edit, uint16_t c) {
    es2_status why;
    return edit ? insert(edit, c, &why) : 0;
}

/* Original: VSHELL.EXE 0x40c406 (backspace). */
int es2_shell_edit_backspace(es2_shell_edit *edit) {
    if (!edit || edit->length <= 0 || edit->length <= edit->prefix) return 0;
    edit->length--;
    edit->width -= es2_shell_glyph_advance(edit->font, (uint8_t)edit->text[edit->length]);
    edit->text[edit->length] = 0;
    return 1;
}

es2_status es2_shell_edit_type(es2_shell_edit *edit, const char *typed, es2_error *e) {
    if (!edit || !typed) return es2_fail(e, ES2_ERR_ARGUMENT, 0);
    for (size_t i = 0; typed[i]; i++) {
        es2_status why = ES2_OK;
        if (!insert(edit, (uint8_t)typed[i], &why)) return es2_fail(e, why, i);
    }
    es2_error_clear(e);
    return ES2_OK;
}

/* Original: VSHELL.EXE 0x40c6ff (draw). */
void es2_shell_edit_draw(const es2_shell_text_frame *frame, const es2_shell_edit *edit) {
    const es2_shell_text_rect *r;
    int32_t w, h, pen;
    if (!frame || !edit) return;
    r = &edit->rect;
    w = r->r - r->l;
    h = r->b - r->t;
    es2_shell_fill(frame, r, normalised(r, 0, 0, w, h), 16);                 /* 0x40c71a..0x40c799 */
    pen = (edit->font ? edit->font->height : 0) / 2 + (h + 1) / 2;         /* 0x40c7cb..0x40c7ec */
    if (edit->font)
        es2_shell_string_draw(frame, r, edit->font, r->l + 1, r->t + pen, edit->text, ES2_SHELL_ALIGN_LEFT, w + 1);
    if (edit->active && edit->blink) {                                      /* 0x40c810..0x40c850, not normalised */
        es2_shell_text_rect caret = {r->l + edit->width, r->t + pen - 2, r->l + edit->width + 6, r->t + pen};
        es2_shell_fill(frame, r, caret, 39);
    }
    es2_shell_recolour_ink(frame, r, normalised(r, 1, 1, w - 1, h - 1), edit->colour);   /* 0x40c856..0x40c91b */
}

/* Original: VSHELL.EXE 0x40d231 (one label per line, 0x40d37d) and the
 * page show 0x40d492. */
es2_status es2_shell_report_draw(const es2_shell_text_frame *frame, es2_shell_text_rect panel, const es2_dfn *font,
                                 const es2_shell_report *report, int32_t page, es2_error *e) {
    int32_t H, rows;
    if (!font || !report || (report->lines && (!report->line || !report->text.data))) return es2_fail(e, ES2_ERR_ARGUMENT, 0);
    H = font->height;
    rows = H > 0 ? (panel.b - panel.t) / H : 0;
    if (rows <= 0) return es2_fail(e, ES2_ERR_ARGUMENT, 0);
    for (size_t i = 0; i < report->lines; i++)
        if (strlen((const char *)report->text.data + report->line[i]) >= ES2_SHELL_LABEL_TEXT)
            return es2_fail(e, ES2_ERR_LENGTH, i);
    for (size_t i = 0; i < report->lines; i++) {
        es2_shell_label label;
        int32_t j = (int32_t)(i % (size_t)rows);
        es2_shell_text_rect row = {panel.l, panel.t + j * H, panel.r, panel.t + j * H + H};
        if ((int64_t)(i / (size_t)rows) != page) continue;
        es2_shell_label_init(&label, row, font, (const char *)report->text.data + report->line[i], ES2_SHELL_ALIGN_LEFT,
                             40, 1, NULL);
        es2_shell_label_draw(frame, &label, -1);
    }
    es2_error_clear(e);
    return ES2_OK;
}
