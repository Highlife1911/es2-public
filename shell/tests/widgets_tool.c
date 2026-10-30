/* Test driver of the widget toolkit (es2/shell/gui/widgets.h) for
 * //shell:widgets_real_test.
 *
 *   widgets_tool <widget.in> <widget.emit>
 *       draws every case of widget.in with our toolkit and writes what the
 *       memory worker's `widget-draw` writes (tools/reference-game/
 *       shell-invitro/main.c, s28_widget_draw: format there): the SHA-256
 *       of the canvas per case, followed by the canvas with flag 0x80.
 *
 * Synthetic cases only; no game data is read. */
#include "es2/shell/gui/widgets.h"
#include "es2/formats/sha256.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static uint32_t le32(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

int main(int argc, char **argv) {
    size_t size = 0, at = 4;
    uint8_t *in;
    FILE *out;
    es2_gui_shell *shell;
    es2_ui *ui;
    unsigned cw, ch;
    if (argc != 3 || !(in = read_file(argv[1], &size)) || size < 4) {
        fprintf(stderr, "usage: widgets_tool <widget.in> <widget.emit>\n");
        return 2;
    }
    cw = in[0] | (unsigned)in[1] << 8;
    ch = in[2] | (unsigned)in[3] << 8;
    if (!cw || !ch || cw > ES2_GUI_WIDTH || ch > ES2_GUI_HEIGHT) return 2;
    shell = calloc(1, sizeof *shell);
    ui = calloc(1, sizeof *ui);
    if (!shell || !ui || !(out = fopen(argv[2], "wb"))) return 2;
    es2_gui_shell_init(shell, NULL, NULL, NULL);
    while (at + 56 <= size) {
        const uint8_t *r = in + at;
        unsigned kind = r[0], flags = r[1];
        int32_t v[12];
        uint32_t seed;
        es2_ui_id id = 0;
        es2_ui_widget *w;
        es2_gui_rect rect;
        for (int i = 0; i < 12; i++) v[i] = (int32_t)le32(r + 4 + 4 * i);
        seed = le32(r + 52);
        at += 56;
        rect.left = v[0];
        rect.top = v[1];
        rect.right = v[2];
        rect.bottom = v[3];
        es2_ui_release(ui);
        es2_ui_init(ui, shell);
        for (unsigned y = 0; y < ch; y++)
            for (unsigned x = 0; x < cw; x++) {
                seed = seed * 1103515245u + 12345u;
                shell->frame.pixels[y * ES2_GUI_WIDTH + x] = (uint8_t)((seed >> 16) & 0xff);
            }
        switch (kind) {
        case 0: case 1: id = es2_ui_rect(ui, ui->root, rect, NULL, v[4]); break;
        case 2: id = es2_ui_button_font(ui, ui->root, rect, NULL, NULL, NULL, v[4], 0); break;
        case 3: id = es2_ui_region_fill(ui, ui->root, rect, NULL, flags & 1, v[4]); break;
        case 4: id = es2_ui_title(ui, ui->root, rect, NULL, NULL, v[4], NULL, v[8]); break;
        case 5: id = es2_ui_arm(ui, ui->root, rect, NULL, v[4]); break;
        case 6: id = es2_ui_grid(ui, ui->root, rect, NULL, v[4]); break;
        case 7: id = es2_ui_quad(ui, ui->root, rect, NULL, v[4], NULL, NULL, NULL, NULL); break;
        case 8: id = es2_ui_pal(ui, ui->root, rect, NULL); break;
        default: break;
        }
        if (kind == 1 && !(flags & 1)) { /* the fill-less frame: ESBitmap without a bitmap draws only it */
            es2_ui_release(ui);
            es2_ui_init(ui, shell);
            id = es2_ui_bitmap(ui, ui->root, rect, NULL, NULL, v[4]);
        }
        w = es2_ui_get(ui, id);
        if (!w) return 3;
        w->pressed = (flags >> 2) & 1;
        w->enabled = (flags >> 3) & 1;
        w->colour = v[4];
        w->frame = (flags >> 4) & 1;
        if (kind == 3 || kind == 4 || kind == 5) {
            w->f55 = v[5];
            w->f59 = kind == 5 ? v[6] : (int32_t)(flags & 1);
            w->f5d = kind == 5 ? (flags >> 6) & 1 : v[7];
            w->f61 = v[8];
            w->f65 = (flags >> 5) & 1;
            w->f6d = v[9];
            w->f71 = v[10];
        }
        if (kind == 6) {
            w->grid_lines = (uint8_t)((flags >> 1) & 1);
            w->grid_colour = (int16_t)v[11];
        }
        if (kind == 0) { /* a hidden WinButton: event 2 through the dispatch */
            es2_gui_event ev;
            memset(&ev, 0, sizeof ev);
            ev.type = ES2_GUI_EV_HIDDEN;
            ev.target = id;
            es2_ui_on_event(ui, &ev);
        } else {
            es2_ui_draw(ui, id);
        }
        {
            es2f_sha256 s;
            char hex[65];
            uint8_t hash[32];
            es2f_sha256_init(&s);
            for (unsigned y = 0; y < ch; y++) es2f_sha256_update(&s, shell->frame.pixels + y * ES2_GUI_WIDTH, cw);
            es2f_sha256_hex(&s, hex);
            for (int i = 0; i < 32; i++) {
                unsigned b;
                sscanf(hex + 2 * i, "%2x", &b);
                hash[i] = (uint8_t)b;
            }
            fwrite(hash, 1, 32, out);
            if (flags & 0x80) /* the canvas itself, as the worker writes it */
                for (unsigned y = 0; y < ch; y++) fwrite(shell->frame.pixels + y * ES2_GUI_WIDTH, 1, cw, out);
        }
    }
    fclose(out);
    es2_ui_release(ui);
    free(ui);
    free(shell);
    free(in);
    return 0;
}
