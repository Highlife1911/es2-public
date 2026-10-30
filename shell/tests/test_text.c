/* Unit tests of VSHELL's text drawing (es2/shell/gui/text.h) and the
 * Saved games width rule (es2_shell_slot_row_typed) on synthetic fonts;
 * no original data. */
#include "es2/shell/gui/text.h"
#include "es2/shell/slots.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

/* ---- a synthetic DFN: codes 32..126, height 4, baseline 3 ------------------ */
enum { F = 32, N = 95, H = 4, B = 3 };
static uint8_t font_bytes[8192];
static es2_dfn font, fixed_font;
static int width_of(int c) { return c == ' ' ? 2 : c == 'i' ? 1 : c == 'W' ? 5 : 1 + c % 3; }
/* Pixel of glyph c: transparent where (row + col + c) % 4 == 0, ink 41 on
 * the top row, else 60 + c % 7. */
static uint8_t pixel_of(int c, int row, int col) {
    if ((row + col + c) % 4 == 0) return 0;
    return row == 0 ? 41 : (uint8_t)(60 + c % 7);
}
static size_t put16(uint8_t *p, size_t at, int v) { p[at] = (uint8_t)v; p[at + 1] = (uint8_t)((unsigned)v >> 8); return at + 2; }
static size_t put32(uint8_t *p, size_t at, uint32_t v) { put16(p, at, (int)(v & 0xffff)); return put16(p, at + 2, (int)(v >> 16)); }
static size_t build(uint8_t *b, int fixed) {
    size_t at = 8, data_size = 0, data_at;
    for (int i = 0; i < N; i++) data_size += (size_t)(fixed > 0 ? fixed : width_of(F + i)) * H;
    at = put16(b, at, N); at = put16(b, at, 0); at = put16(b, at, F); at = put16(b, at, H);
    at = put16(b, at, fixed > 0 ? fixed : -1); at = put16(b, at, H); at = put16(b, at, 5);
    at = put16(b, at, 8); at = put16(b, at, 0); at = put16(b, at, B); at = put16(b, at, 0);
    at = put32(b, at, (uint32_t)data_size);
    data_at = at;
    {
        size_t off = 0;
        for (int i = 0; i < N; i++) {
            int w = fixed > 0 ? fixed : width_of(F + i);
            for (int r = 0; r < H; r++)
                for (int c = 0; c < w; c++) b[data_at + off + (size_t)r * (size_t)w + (size_t)c] = pixel_of(F + i, r, c);
            off += (size_t)w * H;
        }
        at = data_at + data_size;
        off = 0;
        for (int i = 0; i < N; i++) {
            at = put32(b, at, (uint32_t)off);
            off += (size_t)(fixed > 0 ? fixed : width_of(F + i)) * H;
        }
    }
    if (fixed <= 0)
        for (int i = 0; i < N; i++) b[at++] = (uint8_t)width_of(F + i);
    b[0] = 5; b[1] = 0; b[2] = 0x28; b[3] = 0;
    put32(b, 4, (uint32_t)(at - 8));
    if ((at - 8) & 1) b[at++] = 0;
    return at;
}
static void fonts(void) {
    static uint8_t fixed_bytes[8192];
    size_t n = build(font_bytes, 0), m = build(fixed_bytes, 3);
    CHECK(es2_dfn_parse((es2_span){font_bytes, n}, &font, NULL) == ES2_OK);
    CHECK(es2_dfn_parse((es2_span){fixed_bytes, m}, &fixed_font, NULL) == ES2_OK);
}

/* ---- a frame -------------------------------------------------------------- */
enum { W = 96, FH = 24 };
static uint8_t pixels[FH][W];
static es2_shell_text_frame frame = {&pixels[0][0], W, FH, W};
static void clear(uint8_t v) { memset(pixels, v, sizeof pixels); }
static int changed_outside(uint8_t v, es2_shell_text_rect r) {
    for (int y = 0; y < FH; y++)
        for (int x = 0; x < W; x++)
            if ((x < r.l || x > r.r || y < r.t || y > r.b) && pixels[y][x] != v) return 1;
    return 0;
}

static void primitives(void) {
    CHECK(es2_shell_glyph_advance(&font, 'A') == width_of('A'));
    CHECK(es2_shell_glyph_advance(&font, ' ') == 2);
    CHECK(es2_shell_glyph_advance(&font, 200) == width_of('?'));   /* out of range: the '?' choice */
    CHECK(es2_shell_glyph_advance(&font, 10) == width_of('?'));
    CHECK(es2_shell_glyph_advance(&fixed_font, 200) == 3);         /* 0x401186: fixed width for every byte */
    CHECK(es2_shell_glyph_present(&font, 'A') && !es2_shell_glyph_present(&font, 127) && !es2_shell_glyph_present(&font, 31));
    CHECK(es2_shell_string_width(&font, "AB i") == width_of('A') + width_of('B') + 2 + 1);
    CHECK(es2_shell_string_width(&font, "") == 0 && es2_shell_string_width(&font, NULL) == 0);
    {
        uint8_t w[256];
        es2_shell_font_widths(&font, w);
        CHECK(w['W'] == 5 && w[0] == width_of('?') && w[255] == width_of('?'));
    }
    /* glyph: top row at y - B, index 0 transparent, ink as stored */
    clear(7);
    es2_shell_glyph_draw(&frame, NULL, &font, 10, 8, 'W');
    {
        int ok = 1;
        for (int r = 0; r < H; r++)
            for (int c = 0; c < 5; c++) {
                uint8_t want = pixel_of('W', r, c);
                ok &= pixels[8 - B + r][10 + c] == (want ? want : 7);
            }
        CHECK(ok);
        CHECK(!changed_outside(7, (es2_shell_text_rect){10, 5, 14, 8}));
    }
    /* '?' for a byte without a glyph */
    clear(7);
    es2_shell_glyph_draw(&frame, NULL, &font, 0, 3, 200);
    {
        uint8_t q[H][W];
        memcpy(q, pixels, sizeof q);
        clear(7);
        es2_shell_glyph_draw(&frame, NULL, &font, 0, 3, '?');
        CHECK(!memcmp(q, pixels, sizeof q));
    }
    /* inclusive clip: columns l..r and rows t..b are drawn, nothing else */
    clear(7);
    {
        es2_shell_text_rect clip = {11, 6, 12, 6};
        es2_shell_glyph_draw(&frame, &clip, &font, 10, 8, 'W');
        CHECK(!changed_outside(7, clip));
        CHECK(pixels[6][11] == (pixel_of('W', 1, 1) ? pixel_of('W', 1, 1) : 7));
        CHECK(pixels[6][12] == (pixel_of('W', 1, 2) ? pixel_of('W', 1, 2) : 7));
    }
    /* frame guard: drawing at the edges does not write outside */
    clear(7);
    es2_shell_glyph_draw(&frame, NULL, &font, W - 2, 1, 'W');
    es2_shell_glyph_draw(&frame, NULL, &font, -3, FH + 1, 'W');
    CHECK(pixels[0][W - 2] == (pixel_of('W', 2, 0) ? pixel_of('W', 2, 0) : 7));
    /* fills: inclusive, intersected with the clip; unordered = empty */
    clear(7);
    es2_shell_fill(&frame, NULL, (es2_shell_text_rect){2, 3, 4, 5}, 9);
    CHECK(pixels[3][2] == 9 && pixels[5][4] == 9 && pixels[2][2] == 7 && pixels[5][5] == 7);
    CHECK(!changed_outside(7, (es2_shell_text_rect){2, 3, 4, 5}));
    clear(7);
    es2_shell_fill(&frame, &(es2_shell_text_rect){3, 0, 3, 99}, (es2_shell_text_rect){2, 3, 4, 5}, 9);
    CHECK(pixels[3][3] == 9 && pixels[3][2] == 7 && pixels[3][4] == 7);
    clear(7);
    es2_shell_fill(&frame, NULL, (es2_shell_text_rect){4, 3, 2, 5}, 9);
    CHECK(!changed_outside(7, (es2_shell_text_rect){0, 0, -1, -1}));
    /* recolour: 41 only */
    clear(41);
    pixels[4][4] = 40;
    es2_shell_recolour_ink(&frame, NULL, (es2_shell_text_rect){3, 3, 5, 5}, 23);
    CHECK(pixels[3][3] == 23 && pixels[4][4] == 40 && pixels[5][5] == 23 && pixels[2][3] == 41 && pixels[3][6] == 41);
}

/* String draw: pen positions per alignment. */
static int first_column(void) {
    for (int x = 0; x < W; x++)
        for (int y = 0; y < FH; y++)
            if (pixels[y][x] != 7) return x;
    return -1;
}
static void alignment(void) {
    const char *s = "AB";   /* 'A' column 0 row 0: (0+0+65)%4 = 1 -> drawn */
    int32_t tw = es2_shell_string_width(&font, s);
    clear(7);
    es2_shell_string_draw(&frame, NULL, &font, 5, 6, s, ES2_SHELL_ALIGN_LEFT, 40);
    CHECK(first_column() == 5);
    clear(7);
    es2_shell_string_draw(&frame, NULL, &font, 5, 6, s, ES2_SHELL_ALIGN_RIGHT, 40);
    CHECK(first_column() == 5 + 40 - tw);
    clear(7);
    es2_shell_string_draw(&frame, NULL, &font, 5, 6, s, ES2_SHELL_ALIGN_CENTRE, 40);
    CHECK(first_column() == 5 + ((40 - tw) >> 1));
    clear(7);
    es2_shell_string_draw(&frame, NULL, &font, 5, 6, s, ES2_SHELL_ALIGN_CENTRE, 40 + 1);
    CHECK(first_column() == 5 + ((41 - tw) >> 1));
    /* width below the text: arithmetic shift of a negative difference */
    clear(7);
    es2_shell_string_draw(&frame, NULL, &font, 20, 6, s, ES2_SHELL_ALIGN_CENTRE, tw - 3);
    CHECK(first_column() == 20 + (-3 >> 1) && (-3 >> 1) == -2);
    clear(7);
    es2_shell_string_draw(&frame, NULL, &font, 5, 6, s, 7, 40);   /* other values: left */
    CHECK(first_column() == 5);
    /* glyph after glyph: B starts at x + width('A') */
    {
        uint8_t a[FH][W];
        clear(7);
        es2_shell_string_draw(&frame, NULL, &font, 5, 6, s, 0, 0);
        memcpy(a, pixels, sizeof a);
        clear(7);
        es2_shell_glyph_draw(&frame, NULL, &font, 5, 6, 'A');
        es2_shell_glyph_draw(&frame, NULL, &font, 5 + width_of('A'), 6, 'B');
        CHECK(!memcmp(a, pixels, sizeof a));
    }
}

static void labels(void) {
    es2_shell_label l;
    es2_error e;
    char hundred[101];
    es2_shell_text_rect r = {10, 2, 40, 12};   /* w = 30, h = 10 */
    memset(hundred, 'A', 100);
    hundred[100] = 0;
    CHECK(es2_shell_label_init(&l, r, &font, "AB", 1, 23, 1, &e) == ES2_OK && l.background_colour == 16);
    CHECK(es2_shell_label_set_text(&l, hundred, 0, 41, &e) == ES2_ERR_LENGTH && !strcmp(l.text, "AB") && l.colour == 23);
    hundred[99] = 0;
    CHECK(es2_shell_label_set_text(&l, hundred, 0, 41, &e) == ES2_OK && strlen(l.text) == 99);
    CHECK(es2_shell_label_set_text(&l, NULL, 2, 26, &e) == ES2_OK && !l.text[0] && l.align == 2 && l.colour == 26);
    CHECK(es2_shell_label_set_text(NULL, "x", 0, 0, &e) == ES2_ERR_ARGUMENT);

    /* background fill (0,1)-(w-1,h-1), recolour 41 -> colour over (0,1)-(w,h-1) also without text */
    clear(41);
    es2_shell_label_init(&l, r, &font, "", 0, 23, 1, NULL);
    es2_shell_label_draw(&frame, &l, -1);
    CHECK(pixels[2][10] == 41 && pixels[3][10] == 16 && pixels[11][39] == 16 && pixels[11][40] == 23 && pixels[12][40] == 41);
    CHECK(pixels[3][9] == 41 && pixels[3][41] == 41);
    /* without background the 41 pixels under the label take the colour */
    clear(41);
    es2_shell_label_init(&l, r, &font, NULL, 0, 23, 0, NULL);
    es2_shell_label_draw(&frame, &l, -1);
    CHECK(pixels[3][10] == 23 && pixels[11][40] == 23 && pixels[2][10] == 41 && pixels[12][10] == 41);
    CHECK(!changed_outside(41, (es2_shell_text_rect){10, 3, 40, 11}));
    /* pen rows: y = -1 -> (h + 1) - (h + 1 - H) / 2 - 3; else y - 1 */
    {
        uint8_t a[FH][W];
        clear(7);
        es2_shell_label_init(&l, r, &font, "W", 0, 41, 0, NULL);
        es2_shell_label_draw(&frame, &l, -1);
        memcpy(a, pixels, sizeof a);
        clear(7);
        es2_shell_glyph_draw(&frame, &r, &font, 10, 2 + (11 - (11 - H) / 2 - 3), 'W');
        CHECK(!memcmp(a, pixels, sizeof a));
        clear(7);
        es2_shell_label_draw(&frame, &l, 9);
        memcpy(a, pixels, sizeof a);
        clear(7);
        es2_shell_glyph_draw(&frame, &r, &font, 10, 2 + 8, 'W');
        CHECK(!memcmp(a, pixels, sizeof a));
        /* (h + 1 - H) / 2 truncates towards zero for a negative value */
        es2_shell_text_rect low = {10, 2, 40, 3};   /* h = 1: (2 - 4) / 2 = -1 -> pen 2 + 1 - 3 = 0 */
        clear(7);
        es2_shell_label_init(&l, low, &font, "W", 0, 41, 0, NULL);
        es2_shell_label_draw(&frame, &l, -1);
        memcpy(a, pixels, sizeof a);
        clear(7);
        es2_shell_glyph_draw(&frame, &low, &font, 10, 2 + 0, 'W');
        CHECK(!memcmp(a, pixels, sizeof a));
    }
    /* recolour after the text: the glyph's ink 41 takes the colour; clipped */
    clear(7);
    es2_shell_label_init(&l, r, &font, "WWWWWWWWWW", 0, 26, 0, NULL);   /* 50 px into a 31 px rectangle */
    es2_shell_label_draw(&frame, &l, -1);
    {
        int ink = 0, foreign = 0;
        for (int y = 0; y < FH; y++)
            for (int x = 0; x < W; x++) {
                ink += pixels[y][x] == 41;
                foreign += pixels[y][x] != 7 && (x < 10 || x > 40);
            }
        CHECK(ink == 0 && foreign == 0);
    }
    /* no-draw bit */
    clear(7);
    l.no_draw = 1;
    es2_shell_label_draw(&frame, &l, -1);
    CHECK(!changed_outside(7, (es2_shell_text_rect){0, 0, -1, -1}));
    /* no font: fills and the recolour only */
    clear(41);
    es2_shell_label_init(&l, r, NULL, "W", 0, 26, 0, NULL);
    es2_shell_label_draw(&frame, &l, -1);
    CHECK(pixels[5][12] == 26);
}

static void buttons(void) {
    es2_shell_text_rect r = {20, 4, 60, 18};   /* h = 14: (H + h + 1) / 2 = 9 */
    uint8_t a[FH][W];
    es2_shell_label l;
    es2_error e;
    clear(7);
    CHECK(es2_shell_button_text_draw(&frame, ES2_SHELL_BUTTON_TEXT, r, &font, "AB", 0, &e) == ES2_OK);
    memcpy(a, pixels, sizeof a);
    clear(7);
    es2_shell_label_init(&l, (es2_shell_text_rect){21, 4, 60, 18}, &font, "AB", 2, 41, 0, NULL);
    es2_shell_label_draw(&frame, &l, 9 - 1);
    CHECK(!memcmp(a, pixels, sizeof a));
    clear(7);
    CHECK(es2_shell_button_text_draw(&frame, ES2_SHELL_BUTTON_BAR, r, &font, "AB", 1, &e) == ES2_OK);
    memcpy(a, pixels, sizeof a);
    clear(7);
    es2_shell_label_init(&l, r, &font, "AB", 2, 41, 0, NULL);
    es2_shell_label_draw(&frame, &l, 9 + 1);   /* pressed: two rows lower */
    CHECK(!memcmp(a, pixels, sizeof a));
    CHECK(es2_shell_button_text_draw(&frame, 2, r, &font, "AB", 1, &e) == ES2_ERR_ARGUMENT);
}

static void edits(void) {
    es2_shell_edit ed;
    es2_error e;
    char long_text[95];
    es2_shell_text_rect r = {0, 0, 30, 10};   /* r - l = 30 */
    es2_shell_edit_init(&ed, r, &font);
    CHECK(ed.allowed == es2_shell_edit_set_capitals && ed.blink == 1 && ed.colour == 41 && ed.prefix == 0 &&
          ed.length == 0 && ed.width == 0 && !ed.active);
    CHECK(es2_shell_edit_set_text(&ed, NULL, &e) == ES2_ERR_ARGUMENT);
    memset(long_text, 'A', 90);
    long_text[90] = 0;
    CHECK(es2_shell_edit_set_text(&ed, long_text, &e) == ES2_ERR_LENGTH);
    long_text[89] = 0;
    CHECK(es2_shell_edit_set_text(&ed, long_text, &e) == ES2_OK && ed.length == 89 && ed.width == 89 * width_of('A'));
    /* the width rule: width + advance + 6 < r - l */
    CHECK(es2_shell_edit_set_text(&ed, "", &e) == ES2_OK);
    /* 'W' = 5: 0+5+6 = 11 < 30; 5+5+6 = 16; 10+5+6 = 21; 15+5+6 = 26; 20+5+6 = 31 >= 30 */
    CHECK(es2_shell_edit_type(&ed, "WWWW", &e) == ES2_OK && ed.width == 20);
    CHECK(!es2_shell_edit_insert(&ed, 'W') && ed.length == 4);
    /* 'A' = 3: 20 + 3 + 6 = 29 < 30 taken; then 'B' = 1: 23 + 1 + 6 = 30 refused (the edge) */
    CHECK(width_of('A') == 3 && width_of('B') == 1);
    CHECK(es2_shell_edit_insert(&ed, 'A') && ed.width == 23);
    CHECK(!es2_shell_edit_insert(&ed, 'B') && ed.width == 23 && ed.length == 5);
    /* the set: lower case refused by the default set; a word with a high byte never matches */
    CHECK(!es2_shell_edit_insert(&ed, 'a') && !es2_shell_edit_insert(&ed, 0x100 + 'A'));
    CHECK(es2_shell_edit_type(&ed, "a", &e) == ES2_ERR_STRING && e.offset == 0);
    /* 0 matches the terminator of any set (0x40c26f) */
    ed.width = 0;
    CHECK(es2_shell_edit_insert(&ed, 0) && ed.length == 6 && strlen(ed.text) == 5);
    /* NULL set: everything, the low byte stored */
    es2_shell_edit_init(&ed, (es2_shell_text_rect){0, 0, 300, 10}, &font);
    ed.allowed = NULL;
    CHECK(es2_shell_edit_insert(&ed, 0x100 + 'b') && !strcmp(ed.text, "b") && ed.width == width_of('b'));
    /* length rule: length + 1 < 90 */
    es2_shell_edit_init(&ed, (es2_shell_text_rect){0, 0, 1000, 10}, &font);
    long_text[88] = 0;
    CHECK(es2_shell_edit_set_text(&ed, long_text, &e) == ES2_OK && ed.length == 88);
    CHECK(es2_shell_edit_insert(&ed, 'A') && ed.length == 89 && !es2_shell_edit_insert(&ed, 'A'));
    CHECK(es2_shell_edit_type(&ed, "A", &e) == ES2_ERR_LENGTH);
    /* backspace keeps the prefix */
    es2_shell_edit_init(&ed, (es2_shell_text_rect){0, 0, 300, 10}, &font);
    es2_shell_edit_set_text(&ed, " 1. AB", NULL);
    ed.prefix = 4;
    CHECK(es2_shell_edit_backspace(&ed) && es2_shell_edit_backspace(&ed) && !es2_shell_edit_backspace(&ed));
    CHECK(!strcmp(ed.text, " 1. ") && ed.width == es2_shell_string_width(&font, " 1. "));
    ed.prefix = 0;
    ed.length = 0;
    CHECK(!es2_shell_edit_backspace(&ed));

    /* draw: fill 16 over (0,0)-(w,h), text at (1, H/2 + (h+1)/2), caret 39, recolour (1,1)-(w-1,h-1) */
    {
        uint8_t a[FH][W];
        es2_shell_text_rect er = {4, 2, 40, 14};   /* w = 36, h = 12: pen 2 + 6 = 8 */
        clear(41);
        es2_shell_edit_init(&ed, er, &font);
        es2_shell_edit_set_text(&ed, "WA", NULL);
        ed.colour = 26;
        es2_shell_edit_draw(&frame, &ed);
        memcpy(a, pixels, sizeof a);
        clear(41);
        es2_shell_fill(&frame, NULL, er, 16);
        es2_shell_string_draw(&frame, &er, &font, 5, 2 + H / 2 + 13 / 2, "WA", 0, 0);
        es2_shell_recolour_ink(&frame, &er, (es2_shell_text_rect){5, 3, 39, 13}, 26);
        CHECK(!memcmp(a, pixels, sizeof a));
        CHECK(pixels[2][4] == 16 && pixels[14][40] == 16 && pixels[1][4] == 41);
        ed.active = 1;
        clear(41);
        es2_shell_edit_draw(&frame, &ed);
        CHECK(pixels[2 + 8 - 2][4 + ed.width] == 39 && pixels[2 + 8][4 + ed.width + 6] == 39 && pixels[2 + 8 + 1][4 + ed.width] == 16);
        ed.blink = 0;
        clear(41);
        es2_shell_edit_draw(&frame, &ed);
        CHECK(!memcmp(a, pixels, sizeof a));
    }
}

static void report(void) {
    es2_shell_report rep = {0};
    uint8_t widths[256];
    es2_error e;
    const char *text = "AB\nCD\nEF";
    es2_shell_text_rect panel = {2, 1, 2 + 12, 1 + 2 * H + 1};   /* rows = 9 / 4 = 2 */
    es2_shell_font_widths(&font, widths);
    CHECK(es2_shell_report_layout((es2_span){(const uint8_t *)text, strlen(text)}, widths, 12, 2, &rep, &e) == ES2_OK);
    CHECK(rep.lines == 3);
    {
        uint8_t a[FH][W];
        clear(7);
        CHECK(es2_shell_report_draw(&frame, panel, &font, &rep, 1, &e) == ES2_OK);
        memcpy(a, pixels, sizeof a);
        clear(7);
        for (size_t i = 2; i < rep.lines && i < 4; i++) {
            es2_shell_label l;
            int32_t j = (int32_t)(i % 2);
            es2_shell_label_init(&l, (es2_shell_text_rect){2, 1 + j * H, 14, 1 + j * H + H}, &font,
                                 (const char *)rep.text.data + rep.line[i], 0, 40, 1, NULL);
            es2_shell_label_draw(&frame, &l, -1);
        }
        CHECK(!memcmp(a, pixels, sizeof a));
        clear(7);
        CHECK(es2_shell_report_draw(&frame, panel, &font, &rep, 9, &e) == ES2_OK);
        CHECK(!changed_outside(7, (es2_shell_text_rect){0, 0, -1, -1}));
    }
    CHECK(es2_shell_report_draw(&frame, (es2_shell_text_rect){0, 0, 10, 3}, &font, &rep, 0, &e) == ES2_ERR_ARGUMENT);
    es2_shell_report_free(&rep);
    {
        char line[120];
        es2_shell_report big = {0};
        memset(line, 'i', 100);   /* 100 one-pixel glyphs on a wide panel: one line of 100 bytes */
        line[100] = 0;
        CHECK(es2_shell_report_layout((es2_span){(const uint8_t *)line, 100}, widths, 400, 2, &big, &e) == ES2_OK);
        CHECK(es2_shell_report_draw(&frame, (es2_shell_text_rect){0, 0, 400, 10}, &font, &big, 0, &e) == ES2_ERR_LENGTH);
        es2_shell_report_free(&big);
    }
}

static void slot_rows(void) {
    char row[ES2_SHELL_SLOT_TEXT], name[100];
    es2_error e;
    int32_t prefix0 = es2_shell_string_width(&font, " 1. "), prefix1 = es2_shell_string_width(&font, " 2. ");
    CHECK(es2_shell_slot_row_typed(0, "Ab 9^z", &font, row, &e) == ES2_OK && !strcmp(row, " 1. Ab 9^z"));
    CHECK(es2_shell_slot_row_typed(9, "", &font, row, &e) == ES2_OK && !strcmp(row, "10. "));
    CHECK(es2_shell_slot_row_typed(2, "a!b", &font, row, &e) == ES2_ERR_STRING && e.offset == 1);
    CHECK(es2_shell_slot_row_typed(10, "a", &font, row, &e) == ES2_ERR_ARGUMENT);
    CHECK(es2_shell_slot_row_typed(1, "a", NULL, row, &e) == ES2_ERR_ARGUMENT);
    /* 'W' rows: slot 0 (325) takes n with prefix + 5n + 6 < 325, slot 1 (327) the same with 327 */
    {
        int n0 = 0, n1 = 0;
        while (prefix0 + 5 * (n0 + 1) + 6 < 325) n0++;
        while (prefix1 + 5 * (n1 + 1) + 6 < 327) n1++;
        memset(name, 'W', (size_t)n0);
        name[n0] = 0;
        CHECK(es2_shell_slot_row_typed(0, name, &font, row, &e) == ES2_OK && strlen(row) == 4u + (size_t)n0);
        name[n0] = 'W';
        name[n0 + 1] = 0;
        CHECK(es2_shell_slot_row_typed(0, name, &font, row, &e) == ES2_ERR_LENGTH && e.offset == (size_t)n0);
        memset(name, 'W', (size_t)n1);
        name[n1] = 0;
        CHECK(es2_shell_slot_row_typed(1, name, &font, row, &e) == ES2_OK);
        name[n1] = 'W';
        name[n1 + 1] = 0;
        CHECK(es2_shell_slot_row_typed(1, name, &font, row, &e) == ES2_ERR_LENGTH);
    }
    /* narrow glyphs: the pixel rule would take 85 letters, the 80-byte record not */
    memset(name, 'i', 75);
    name[75] = 0;
    CHECK(es2_shell_slot_row_typed(3, name, &font, row, &e) == ES2_OK && strlen(row) == 79);
    name[75] = 'i';
    name[76] = 0;
    CHECK(es2_shell_slot_row_typed(3, name, &font, row, &e) == ES2_ERR_CAPACITY);
    memset(name, 'i', 86);
    name[86] = 0;
    CHECK(es2_shell_slot_row_typed(3, name, &font, row, &e) == ES2_ERR_LENGTH && e.offset == 85);   /* length + 1 < 90 */
}

int main(void) {
    fonts();
    primitives();
    alignment();
    labels();
    buttons();
    edits();
    report();
    slot_rows();
    printf("%u checks, %u failures\n", checks, failures);
    return failures != 0;
}
