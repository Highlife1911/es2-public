/* The pure parts of the presenter (proxy/presenter.h): SHA-256 of
 * indexed frames and colour tables, the layout of the scaled image in the
 * client area, and the index -> RGB conversion with nearest-neighbour
 * scaling. No Windows call, no global state: the offline tool
 * presenter_offline.c and the fixtures use the same code as the DLL.
 * Contract: docs/reverse-engineering/presentation.md, section "capture,
 * own GDI presenter, windowed DirectDraw". */
#ifndef ES2_PRESENTER_FRAME_H
#define ES2_PRESENTER_FRAME_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <windows.h>
#include "sha256.h"

#define PR_MAX_PIXELS (4096u * 4096u)

/* SHA-256: the proxy's shared sha256.h. */

/* ---- conversion and scaling (pure; offline tool and fixture use them) --- */
typedef struct pr_layout { int scale; int x, y, width, height; } pr_layout;

/* Place a w x h image in a cw x ch client: the largest integer scale k >= 1
 * with w*k <= cw and h*k <= ch, centred (letterbox and pillarbox black);
 * when the client is smaller than the image in either direction (k would
 * be 0), the largest aspect-correct size that fits (scale 0 = fractional).
 * Offsets round down. FALSE for empty or oversized inputs. */
static BOOL pr_compute_layout(int w, int h, int cw, int ch, pr_layout *out) {
    if (w <= 0 || h <= 0 || cw <= 0 || ch <= 0 || (uint64_t)cw * (uint64_t)ch > PR_MAX_PIXELS) return FALSE;
    int k = cw / w < ch / h ? cw / w : ch / h;
    if (k >= 1) {
        out->scale = k; out->width = w * k; out->height = h * k;
    } else {
        out->scale = 0;
        if ((int64_t)cw * h <= (int64_t)ch * w) { out->width = cw; out->height = (int)((int64_t)h * cw / w); }
        else { out->height = ch; out->width = (int)((int64_t)w * ch / h); }
        if (out->width < 1) out->width = 1;
        if (out->height < 1) out->height = 1;
    }
    out->x = (cw - out->width) / 2; out->y = (ch - out->height) / 2;
    return TRUE;
}

/* table[i] = 0x00RRGGBB (a BI_RGB 32-bit pixel: blue, green, red, 0 in memory). */
static void pr_table_from_quads(const RGBQUAD quads[256], DWORD table[256]) {
    for (unsigned i = 0; i < 256; ++i)
        table[i] = (DWORD)quads[i].rgbRed << 16 | (DWORD)quads[i].rgbGreen << 8 | quads[i].rgbBlue;
}

/* Nearest neighbour: output pixel (x, y) inside the image area shows source
 * (x * w / width, y * h / height); everything outside is black. columns needs
 * layout->width entries. For an integer scale k this is source (x / k, y / k). */
static void pr_render(const BYTE *bits, int stride, int w, int h, const DWORD table[256],
                      DWORD *out, int cw, int ch, const pr_layout *l, int *columns) {
    for (int x = 0; x < l->width; ++x) columns[x] = (int)((int64_t)x * w / l->width);
    for (int y = 0; y < ch; ++y) {
        DWORD *row = out + (size_t)y * (size_t)cw;
        if (y < l->y || y >= l->y + l->height) { memset(row, 0, (size_t)cw * 4u); continue; }
        const BYTE *src = bits + (size_t)((int64_t)(y - l->y) * h / l->height) * (size_t)stride;
        if (l->x) memset(row, 0, (size_t)l->x * 4u);
        DWORD *dst = row + l->x;
        if (l->scale == 1) for (int x = 0; x < l->width; ++x) dst[x] = table[src[x]];
        else for (int x = 0; x < l->width; ++x) dst[x] = table[src[columns[x]]];
        int right = cw - l->x - l->width;
        if (right > 0) memset(dst + l->width, 0, (size_t)right * 4u);
    }
}
#endif
