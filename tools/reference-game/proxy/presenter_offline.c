/* Presenter acceptance pass 3 (docs/reverse-engineering/presentation.md,
 * "capture, own GDI presenter, windowed DirectDraw"):
 * applies the presenter's conversion and scaler (presenter_frame.h, the
 * code of the DLL) to one frame dump of the capture and writes the 32-bit
 * client image, so scripts/presenter_capture.py can compare it with its own
 * reference implementation.
 *
 *   presenter_offline <dump.es2f> <client width> <client height> <out file>
 *
 * Output: client width x height pixels, rows top-down, 4 bytes each in BI_RGB
 * order (blue, green, red, 0). Prints one JSON line with the layout. */
#include <stdio.h>
#include <stdlib.h>
#include "presenter_frame.h"

static DWORD get32(const BYTE *p) { return (DWORD)p[0] | (DWORD)p[1] << 8 | (DWORD)p[2] << 16 | (DWORD)p[3] << 24; }

int main(int argc, char **argv) {
    if (argc != 5) { fprintf(stderr, "usage: presenter_offline <dump> <width> <height> <out>\n"); return 2; }
    int cw = atoi(argv[2]), ch = atoi(argv[3]);
    FILE *f = fopen(argv[1], "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", argv[1]); return 2; }
    BYTE header[32 + 768];
    if (fread(header, 1, sizeof(header), f) != sizeof(header) || memcmp(header, "ES2F", 4) || get32(header + 4) != 1) {
        fprintf(stderr, "not an ES2F version 1 dump\n"); fclose(f); return 2;
    }
    DWORD w = get32(header + 20), h = get32(header + 24);
    if (!w || !h || w > 4096 || h > 4096) { fprintf(stderr, "bad frame size\n"); fclose(f); return 2; }
    BYTE *bits = malloc((size_t)w * h);
    if (!bits || fread(bits, 1, (size_t)w * h, f) != (size_t)w * h || fgetc(f) != EOF) {
        fprintf(stderr, "truncated or oversized dump\n"); fclose(f); free(bits); return 2;
    }
    fclose(f);
    RGBQUAD quads[256];
    for (unsigned i = 0; i < 256; ++i) {
        quads[i].rgbRed = header[32 + 3 * i]; quads[i].rgbGreen = header[33 + 3 * i];
        quads[i].rgbBlue = header[34 + 3 * i]; quads[i].rgbReserved = 0;
    }
    DWORD table[256];
    pr_table_from_quads(quads, table);
    pr_layout l;
    if (!pr_compute_layout((int)w, (int)h, cw, ch, &l)) { fprintf(stderr, "bad client size\n"); free(bits); return 2; }
    DWORD *out = malloc((size_t)cw * (size_t)ch * 4u);
    int *columns = malloc((size_t)l.width * sizeof(int));
    if (!out || !columns) { free(bits); free(out); free(columns); return 2; }
    pr_render(bits, (int)w, (int)w, (int)h, table, out, cw, ch, &l, columns);
    FILE *o = fopen(argv[4], "wb");
    BOOL ok = o != NULL;
    for (size_t i = 0; ok && i < (size_t)cw * (size_t)ch; ++i) {
        BYTE px[4] = {(BYTE)out[i], (BYTE)(out[i] >> 8), (BYTE)(out[i] >> 16), 0};
        ok = fwrite(px, 1, 4, o) == 4;
    }
    if (o && fclose(o)) ok = FALSE;
    free(bits); free(out); free(columns);
    if (!ok) { fprintf(stderr, "cannot write %s\n", argv[4]); return 2; }
    printf("{\"width\":%lu,\"height\":%lu,\"client\":[%d,%d],\"scale\":%d,\"x\":%d,\"y\":%d,\"image\":[%d,%d]}\n",
           (unsigned long)w, (unsigned long)h, cw, ch, l.scale, l.x, l.y, l.width, l.height);
    return 0;
}
