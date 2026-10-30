/* The region hashes of a captured frame (own_process.md "the frame compare
 * as tests").
 *
 * Both frame captures write them: the proxy's (presenter.h, [presentation]
 * capture_regions=1, the original DBSIM) and es2sim's (sim/src/present.c,
 * --frames). A record then carries, besides the whole-frame SHA-256, one
 * short hash per region of the frame, so that dbsim_frames.py can say which
 * part of the screen differs first: the 3D view, the HUD, the cockpit panel
 * or the MFD/HDD screens.
 *
 * The regions follow the display mode m = dword [[0x4d24c0] + 0x14] (the
 * display object D's mode, display_modes.md "Objects": 0 cockpit, 1 the
 * scrolled panel, 2 and 3 the side views, 4 the outside view; -1 when D is
 * not built yet). Each mode has a list of rectangles in the coordinates of a
 * 640 x 480 window (left, top, right, bottom; right and bottom exclusive),
 * scaled to the window that was hashed (x * w / 640, y * h / 480). A pixel
 * belongs to the first rectangle of the list that holds it, in the priority
 * order MFD, HUD, panel, view; a pixel no rectangle holds belongs to "rest".
 * The rectangles were measured on es2sim's own frames of c1_01-display-keys
 * (640 x 480; frames 105, 325, 400, 485, 605) with the view rectangle dwords
 * 0x4cfa40..0x4cfa4c read at the present (0, 0, 639, 389 inclusive in every
 * mode of that suite). They are a localisation aid, a few pixels generous
 * around each instrument, not a contract of the drawing.
 *
 * The hash of a region is the SHA-256 of its pixels' index bytes in row-major
 * order (top row first, left to right), cut to its first 16 hex digits; a
 * region without pixels has no hash. FR_TABLE_ID names this table: a capture
 * writes it into its start line, and dbsim_frames.py compares region hashes
 * only between captures with the same table.
 *
 * Header-only, C17, no Windows call. */
#ifndef ES2_FRAME_REGIONS_H
#define ES2_FRAME_REGIONS_H
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "sha256.h"

#define FR_TABLE_ID "t66-1"
#define FR_DISPLAY_VA 0x004d24c0u    /* D: the display object */
#define FR_DISPLAY_MODE_OFFSET 0x14u /* D+0x14: the display mode */
#define FR_REGION_COUNT 5
#define FR_HASH_DIGITS 16
#define FR_WIDTH_MAX 4096

enum { FR_VIEW = 0, FR_HUD = 1, FR_PANEL = 2, FR_MFD = 3, FR_REST = 4 };
static const char *const fr_region_names[FR_REGION_COUNT] = {"view", "hud", "panel", "mfd", "rest"};

typedef struct fr_rect { uint8_t region; int16_t left, top, right, bottom; } fr_rect;

/* Mode 0, the cockpit (frame 105): the MFD screen with its six page buttons;
 * the HUD's compass, waypoint line and message box, the radar circle, the
 * aim reticle, the speed and time line; the instrument panel below; the 3D
 * view (0x4cfa40: rows 0..389). */
static const fr_rect fr_mode0[] = {
    {FR_MFD, 318, 296, 562, 434},
    {FR_HUD, 196, 0, 444, 92},
    {FR_HUD, 84, 50, 188, 152},
    {FR_HUD, 290, 180, 350, 240},
    {FR_HUD, 190, 262, 450, 286},
    {FR_PANEL, 0, 286, 640, 480},
    {FR_VIEW, 0, 0, 640, 390},
};
/* Mode 1, the scrolled panel (frame 605): the HDD (map and orders), the
 * three lower screens, the screens' control bar; the panel art around. */
static const fr_rect fr_mode1[] = {
    {FR_MFD, 92, 82, 556, 286},
    {FR_MFD, 133, 292, 518, 402},
    {FR_MFD, 136, 14, 506, 82},
    {FR_PANEL, 0, 0, 640, 480},
};
/* Modes 2 and 3, the side views (frames 325, 485): the whole window is the
 * view with the cockpit frame drawn over it. */
static const fr_rect fr_mode2[] = {
    {FR_VIEW, 0, 0, 640, 480},
};
/* Mode 4, the outside view (frame 400): the view rectangle and the border
 * band below it (the border fill with the camera labels), counted as
 * panel. */
static const fr_rect fr_mode4[] = {
    {FR_VIEW, 0, 0, 640, 390},
    {FR_PANEL, 0, 390, 640, 480},
};

/* The rectangle list of a display mode (count 0: the whole window is rest). */
static const fr_rect *fr_table(int32_t mode, size_t *count) {
    switch (mode) {
    case 0: *count = sizeof(fr_mode0) / sizeof(fr_mode0[0]); return fr_mode0;
    case 1: *count = sizeof(fr_mode1) / sizeof(fr_mode1[0]); return fr_mode1;
    case 2:
    case 3: *count = sizeof(fr_mode2) / sizeof(fr_mode2[0]); return fr_mode2;
    case 4: *count = sizeof(fr_mode4) / sizeof(fr_mode4[0]); return fr_mode4;
    default: *count = 0; return NULL;
    }
}

/* Row y (0 = top) of the hashed window, w bytes; NULL when it cannot be read. */
typedef const uint8_t *(*fr_row_fn)(void *context, int32_t y);

/* The region hashes of a w x h window shown in display mode `mode`. out[i]
 * gets 16 hex digits and a NUL, or "" when region i has no pixels. 0, or 1
 * (out all "") when w or h is out of range or a row cannot be read. */
static int fr_region_hashes(int32_t mode, int32_t w, int32_t h, fr_row_fn row, void *context,
                            char out[FR_REGION_COUNT][FR_HASH_DIGITS + 1]) {
    static uint8_t label[FR_WIDTH_MAX];
    sha256_ctx s[FR_REGION_COUNT];
    int used[FR_REGION_COUNT] = {0};
    for (int i = 0; i < FR_REGION_COUNT; ++i) { out[i][0] = 0; sha256_init(&s[i]); }
    if (w <= 0 || h <= 0 || w > FR_WIDTH_MAX || h > 65535) return 1;
    size_t count = 0;
    const fr_rect *table = fr_table(mode, &count);
    for (int32_t y = 0; y < h; ++y) {
        const uint8_t *p = row(context, y);
        if (!p) return 1;
        memset(label, FR_REST, (size_t)w);
        /* Lowest priority first, so that the earlier rectangles of the list win. */
        for (size_t k = count; k-- > 0;) {
            const fr_rect *r = &table[k];
            int32_t top = (int32_t)r->top * h / 480, bottom = (int32_t)r->bottom * h / 480;
            if (y < top || y >= bottom) continue;
            int32_t left = (int32_t)r->left * w / 640, right = (int32_t)r->right * w / 640;
            if (right > left) memset(label + left, r->region, (size_t)(right - left));
        }
        int32_t x = 0;
        while (x < w) {
            int32_t end = x + 1;
            while (end < w && label[end] == label[x]) ++end;
            sha256_update(&s[label[x]], p + x, (size_t)(end - x));
            used[label[x]] = 1;
            x = end;
        }
    }
    for (int i = 0; i < FR_REGION_COUNT; ++i) {
        char hex[65];
        sha256_hex(&s[i], hex);
        if (used[i]) { memcpy(out[i], hex, FR_HASH_DIGITS); out[i][FR_HASH_DIGITS] = 0; }
    }
    return 0;
}

/* The record's fields `,"display":m,"regions":{"view":"..."|null,...}` into
 * buf (NUL-terminated). The number of characters, or -1 when buf is short. */
static int fr_format(char *buf, size_t size, int32_t mode, char hashes[FR_REGION_COUNT][FR_HASH_DIGITS + 1]) {
    int n = snprintf(buf, size, ",\"display\":%ld,\"regions\":{", (long)mode);
    for (int i = 0; i < FR_REGION_COUNT && n >= 0 && (size_t)n < size; ++i) {
        int m = hashes[i][0]
            ? snprintf(buf + n, size - (size_t)n, "%s\"%s\":\"%s\"", i ? "," : "", fr_region_names[i], hashes[i])
            : snprintf(buf + n, size - (size_t)n, "%s\"%s\":null", i ? "," : "", fr_region_names[i]);
        n = m < 0 ? -1 : n + m;
    }
    if (n < 0 || (size_t)n + 1 >= size) return -1;
    buf[n++] = '}';
    buf[n] = 0;
    return n;
}

#endif
