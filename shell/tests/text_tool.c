/* Test driver of the text drawing (es2/shell/gui/text.h) for
 * //shell:text_real_test.
 *
 *   text_tool run <cd-root> <text.in> <text.emit>
 *       draws every case of text.in with our library and writes the records
 *       the memory worker's `text-draw` writes (tools/reference-game/
 *       shell-invitro/main.c, s27_text_draw: format there): SHA-256 of the
 *       canvas, the edit field's length, width and 90 text bytes for kinds 2
 *       and 3, the canvas itself with flag 0x08.
 *   text_tool estext <cd-root> <eng|fre|ger> <text.in>
 *       writes cases from ESTEXT.BIN: every string with a glyph for each
 *       byte, once per shell font (FONT2, BLACK, FONT, MAP): a label case
 *       (below 100 bytes) or a string draw case; prints counts only.
 *   text_tool widths <cd-root>
 *       prints each font's header numbers and its codes of zero width.
 *
 * Fonts are dfn\font2.dfn, dfn\black.dfn, dfn\font.dfn, dfn\map.dfn from
 * the CD's VOL archives in VSHELL's order (priority, as the worker). Nothing
 * of the CD is printed or stored beyond the hashes the test compares. */
#define WIN32_LEAN_AND_MEAN
#include "es2/shell/gui/text.h"
#include "es2/shell/slots.h"
#include "es2/formats/sha256.h"
#include "es2/formats/vfs.h"
#include "es2/formats/vol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

static uint8_t *read_file(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    uint8_t *data = NULL;
    long n;
    if (!f) return NULL;
    if (!fseek(f, 0, SEEK_END) && (n = ftell(f)) >= 0 && !fseek(f, 0, SEEK_SET) && (data = malloc((size_t)n + 1))) {
        if (fread(data, 1, (size_t)n, f) != (size_t)n) { free(data); data = NULL; }
        else *size = (size_t)n;
    }
    fclose(f);
    return data;
}

static es2_vol vols[32];
static es2_vfs_source sources[32];
static unsigned vol_count;
static es2_vfs vfs;
static es2_dfn fonts[4];
static int mount(const char *root) {
    static const char *names[4] = {"dfn\\font2.dfn", "dfn\\black.dfn", "dfn\\font.dfn", "dfn\\map.dfn"};
    char pattern[MAX_PATH], path[MAX_PATH];
    WIN32_FIND_DATAA f;
    HANDLE h;
    snprintf(pattern, sizeof pattern, "%s/VOL/*.VOL", root);
    h = FindFirstFileA(pattern, &f);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        size_t size = 0;
        uint8_t *data;
        if (vol_count == 32) return 0;
        snprintf(path, sizeof path, "%s/VOL/%s", root, f.cFileName);
        if (!(data = read_file(path, &size)) || es2_vol_open((es2_span){data, size}, &vols[vol_count], NULL) != ES2_OK)
            return 0;
        sources[vol_count].kind = ES2_VFS_VOL;
        sources[vol_count].data.vol = &vols[vol_count];
        vol_count++;
    } while (FindNextFileA(h, &f));
    FindClose(h);
    for (unsigned i = 0; i < vol_count; i++)
        for (unsigned j = i + 1; j < vol_count; j++)
            if (sources[j].data.vol->priority > sources[i].data.vol->priority) {
                es2_vfs_source t = sources[i];
                sources[i] = sources[j];
                sources[j] = t;
            }
    if (es2_vfs_open(sources, vol_count, &vfs, NULL) != ES2_OK) return 0;
    for (int k = 0; k < 4; k++) {
        es2_vfs_copy c;
        if (es2_vfs_find(&vfs, names[k], &c, NULL) != ES2_OK || es2_dfn_parse(c.payload, &fonts[k], NULL) != ES2_OK)
            return 0;
    }
    return 1;
}

static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static unsigned get16(const uint8_t *p) { return (unsigned)p[0] | (unsigned)p[1] << 8; }

/* SHA-256 of the canvas as 32 raw bytes: the shared implementation (formats/sha256.h). */
static void sha256(const uint8_t *data, size_t size, uint8_t out[32]) {
    char hex[65];
    es2f_sha256_of(data, size, hex);
    for (int i = 0; i < 32; i++) {
        unsigned hi = (unsigned)(hex[2 * i] <= '9' ? hex[2 * i] - '0' : hex[2 * i] - 'a' + 10);
        unsigned lo = (unsigned)(hex[2 * i + 1] <= '9' ? hex[2 * i + 1] - '0' : hex[2 * i + 1] - 'a' + 10);
        out[i] = (uint8_t)(hi << 4 | lo);
    }
}

static uint8_t canvas[640 * 480];
static int run(const char *in_path, const char *out_path) {
    static const char *const sets[4] = {NULL, es2_shell_edit_set_capitals, es2_shell_edit_set_slots, es2_shell_edit_set_names};
    size_t size = 0, at = 4;
    uint8_t *d = read_file(in_path, &size);
    FILE *out;
    unsigned cw, ch, records = 0;
    if (!d || size < 4) return 2;
    cw = get16(d);
    ch = get16(d + 2);
    if (!cw || !ch || cw > 640 || ch > 480) return 2;
    if (!(out = fopen(out_path, "wb"))) return 2;
    while (at < size) {
        const uint8_t *r = d + at;
        unsigned kind = r[0], font = r[1], flags = r[4], set = r[6], prefix = r[7], n;
        int32_t l = (int32_t)get32(r + 8), t = (int32_t)get32(r + 12), rr = (int32_t)get32(r + 16),
                b = (int32_t)get32(r + 20), x = (int32_t)get32(r + 24), y = (int32_t)get32(r + 28),
                width = (int32_t)get32(r + 32);
        uint32_t seed = get32(r + 36);
        es2_shell_text_frame frame = {canvas, (int32_t)cw, (int32_t)ch, (int32_t)cw};
        es2_shell_text_rect rect = {l, t, rr, b};
        char text[256];
        uint8_t hash[32], state[98] = {0};
        if (size - at < 42) return 3;
        n = get16(r + 40);
        if (size - at - 42 < n || n > 255 || kind > 3 || font > 3 || set > 3) return 3;
        memcpy(text, r + 42, n);
        text[n] = 0;
        at += 42 + n;
        for (unsigned i = 0; i < cw * ch; i++) {
            unsigned v;
            seed = seed * 1103515245u + 12345u;
            v = (seed >> 16) & 0xff;
            canvas[i] = (uint8_t)(v < 48 ? 41 : v < 80 ? 0 : v);
        }
        if (kind == 0) {
            es2_shell_string_draw(&frame, &rect, &fonts[font], l + x, t + y, text, r[2], width);
        } else if (kind == 1) {
            es2_shell_label label;
            if (es2_shell_label_init(&label, rect, &fonts[font], text, r[2], r[3], flags & 1, NULL) != ES2_OK) return 3;
            label.background_colour = r[5];
            es2_shell_label_draw(&frame, &label, y);
        } else {
            es2_shell_edit edit;
            char start[256];
            unsigned keep = kind == 2 ? n : prefix;
            if (prefix > n) return 3;
            memcpy(start, text, keep);
            start[keep] = 0;
            es2_shell_edit_init(&edit, rect, &fonts[font]);
            edit.allowed = sets[set];
            edit.active = (flags >> 1) & 1;
            edit.blink = (flags >> 2) & 1;
            edit.colour = r[3];
            if (es2_shell_edit_set_text(&edit, start, NULL) != ES2_OK) return 3;
            edit.prefix = kind == 2 ? 0 : (int32_t)prefix;
            for (unsigned i = prefix; kind == 3 && i < n; i++) {
                if ((uint8_t)text[i] == 8) es2_shell_edit_backspace(&edit);
                else es2_shell_edit_insert(&edit, (uint8_t)text[i]);
            }
            es2_shell_edit_draw(&frame, &edit);
            for (int i = 0; i < 4; i++) {
                state[i] = (uint8_t)((uint32_t)edit.length >> (8 * i));
                state[4 + i] = (uint8_t)((uint32_t)edit.width >> (8 * i));
            }
            /* the bytes after the NUL are what the field's buffer holds */
            memcpy(state + 8, edit.text, sizeof edit.text);
        }
        sha256(canvas, (size_t)cw * ch, hash);
        fwrite(hash, 1, 32, out);
        if (kind >= 2) fwrite(state, 1, 98, out);
        if (flags & 8) fwrite(canvas, 1, (size_t)cw * ch, out);
        records++;
    }
    fclose(out);
    printf("records %u\n", records);
    return 0;
}

/* ESTEXT cases on a 640 x 20 canvas: label (2,2)-(637,17) or a string draw
 * clipped to (0,0)-(639,19); alignment, colour, background and seed vary
 * with the string index. */
static int estext(const char *lang, const char *out_path) {
    char path[64];
    es2_vfs_copy c;
    FILE *out;
    uint32_t count;
    unsigned labels = 0, strings = 0, skipped = 0, glyphs = 0;
    snprintf(path, sizeof path, "%s\\estext.bin", lang);
    if (es2_vfs_find(&vfs, path, &c, NULL) != ES2_OK || c.payload.size < 8) return 2;
    count = get32(c.payload.data);
    if (8 + 2 * (size_t)count > c.payload.size) return 2;
    if (!(out = fopen(out_path, "wb"))) return 2;
    fwrite("\x80\x02\x14\x00", 1, 4, out);   /* 640 x 20 */
    for (uint32_t i = 0; i < count; i++) {
        size_t base = 8 + 2 * (size_t)count, off = base + get16(c.payload.data + 8 + 2 * i), n = 0;
        const uint8_t *s;
        int ok = 1;
        if (off >= c.payload.size) { skipped++; continue; }
        s = c.payload.data + off;
        while (off + n < c.payload.size && s[n]) n++;
        if (!n || n > 200) { skipped += n != 0; continue; }
        for (int k = 0; k < 4 && ok; k++)
            for (size_t j = 0; j < n; j++) ok &= es2_shell_glyph_present(&fonts[k], s[j]);
        if (!ok) { skipped++; continue; }
        for (int k = 0; k < 4; k++) {
            uint8_t rec[42] = {0};
            int label = n < 100;
            int32_t v[7];
            rec[0] = (uint8_t)(label ? 1 : 0);
            rec[1] = (uint8_t)k;
            rec[2] = (uint8_t)((i + (uint32_t)k) % 3);
            rec[3] = (uint8_t)((i % 4) == 0 ? 41 : (i % 4) == 1 ? 26 : (i % 4) == 2 ? 39 : 40);
            rec[4] = (uint8_t)(i % 5 == 0);
            rec[5] = 16;
            if (label) { v[0] = 2; v[1] = 2; v[2] = 637; v[3] = 17; v[4] = 0; v[5] = -1; v[6] = 0; }
            else { v[0] = 0; v[1] = 0; v[2] = 639; v[3] = 19; v[4] = 1; v[5] = 13; v[6] = 640; }
            for (int f = 0; f < 7; f++)
                for (int b = 0; b < 4; b++) rec[8 + 4 * f + b] = (uint8_t)((uint32_t)v[f] >> (8 * b));
            for (int b = 0; b < 4; b++) rec[36 + b] = (uint8_t)((i * 7919u + (uint32_t)k) >> (8 * b));
            rec[40] = (uint8_t)n;
            rec[41] = (uint8_t)(n >> 8);
            fwrite(rec, 1, 42, out);
            fwrite(s, 1, n, out);
            labels += (unsigned)label;
            strings += (unsigned)!label;
            glyphs += (unsigned)n;
        }
    }
    fclose(out);
    printf("estext %s entries %u label-cases %u string-cases %u skipped %u glyphs %u\n", lang, (unsigned)count, labels,
           strings, skipped, glyphs);
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 5 && !strcmp(argv[1], "run")) {
        if (!mount(argv[2])) return 2;
        return run(argv[3], argv[4]);
    }
    if (argc == 5 && !strcmp(argv[1], "estext")) {
        if (!mount(argv[2])) return 2;
        return estext(argv[3], argv[4]);
    }
    if (argc == 3 && !strcmp(argv[1], "widths")) {
        static const char *names[4] = {"FONT2", "BLACK", "FONT", "MAP"};
        if (!mount(argv[2])) return 2;
        for (int k = 0; k < 4; k++) {
            unsigned zero = 0;
            printf("%s first %d count %d height %d baseline %d zero-width:", names[k], fonts[k].first_code,
                   fonts[k].glyph_count, fonts[k].height, fonts[k].baseline);
            for (int c = 0; c < 256; c++)
                if (es2_shell_glyph_present(&fonts[k], (uint8_t)c) && !es2_shell_glyph_advance(&fonts[k], (uint8_t)c)) {
                    printf(" %d", c);
                    zero++;
                }
            printf(" (%u)\n", zero);
        }
        {
            /* The Saved games row: the longest name of the narrowest slot-set character. */
            int narrow = 0, best = 99;
            char name[100], row[ES2_SHELL_SLOT_TEXT];
            for (const char *p = es2_shell_edit_set_slots; *p; p++) {
                int a = es2_shell_glyph_advance(&fonts[0], (uint8_t)*p);
                if (a < best) { best = a; narrow = (unsigned char)*p; }
            }
            int n = 0;
            for (;;) {
                es2_shell_edit edit;
                es2_shell_edit_init(&edit, (es2_shell_text_rect){0, 0, ES2_SHELL_EDIT_WIDTH_SLOT, 12}, &fonts[0]);
                edit.allowed = es2_shell_edit_set_slots;
                es2_shell_edit_set_text(&edit, " 2. ", NULL);
                memset(name, narrow, (size_t)n + 1);
                name[n + 1] = 0;
                if (es2_shell_edit_type(&edit, name, NULL) != ES2_OK) break;
                n++;
            }
            printf("FONT2 slot set: narrowest %d (advance %d); a 327-pixel row takes %d of them, row %d bytes; %s\n",
                   narrow, best, n, n + 4,
                   es2_shell_slot_row_typed(1, (memset(name, narrow, (size_t)n), name[n] = 0, name), &fonts[0], row, NULL) ==
                       ES2_OK ? "fits the record" : "refused");
        }
        return 0;
    }
    fprintf(stderr, "usage: text_tool run <cd-root> <text.in> <text.emit> | estext <cd-root> <lang> <text.in>\n");
    return 2;
}
