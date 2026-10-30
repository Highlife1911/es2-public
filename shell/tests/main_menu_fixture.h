#ifndef ES2_SHELL_TESTS_MAIN_MENU_FIXTURE_H
#define ES2_SHELL_TESTS_MAIN_MENU_FIXTURE_H

/* Synthetic resources for the main menu's tests (no game data): a
 * fixed-width DFN (codes 32..126, 4 x 7), five seeded 640 x 480 images
 * standing in for dbm\bay2a_80..84 and a seeded palette. Header-only, used
 * by test_main_menu.c and screen_main_menu.c. */
#include <stdint.h>
#include <string.h>

#include "es2/formats/dfn.h"
#include "es2/shell/draw.h"

enum { MMF_FIRST = 32, MMF_COUNT = 95, MMF_W = 4, MMF_H = 7 };

static inline size_t mmf_put16(uint8_t *p, size_t at, int v) {
    p[at] = (uint8_t)v;
    p[at + 1] = (uint8_t)((unsigned)v >> 8);
    return at + 2;
}
static inline size_t mmf_put32(uint8_t *p, size_t at, uint32_t v) {
    mmf_put16(p, at, (int)(v & 0xffffu));
    return mmf_put16(p, at + 2, (int)(v >> 16));
}

/* bytes: at least 8 + 26 + MMF_COUNT * (MMF_W * MMF_H + 4) + 1. Returns 0 on success. */
static inline int mmf_font(uint8_t *bytes, es2_dfn *out) {
    size_t at = 8, data_size = (size_t)MMF_COUNT * MMF_W * MMF_H, data_at;
    at = mmf_put16(bytes, at, MMF_COUNT);
    at = mmf_put16(bytes, at, 0);
    at = mmf_put16(bytes, at, MMF_FIRST);
    at = mmf_put16(bytes, at, MMF_H);
    at = mmf_put16(bytes, at, MMF_W);  /* fixed width */
    at = mmf_put16(bytes, at, MMF_H);
    at = mmf_put16(bytes, at, 0);
    at = mmf_put16(bytes, at, 8);      /* bits per pixel */
    at = mmf_put16(bytes, at, 0);      /* uncompressed */
    at = mmf_put16(bytes, at, MMF_H - 1);
    at = mmf_put16(bytes, at, 0);      /* no colour records */
    at = mmf_put32(bytes, at, (uint32_t)data_size);
    data_at = at;
    for (int g = 0; g < MMF_COUNT; g++)
        for (int k = 0; k < MMF_W * MMF_H; k++)
            bytes[data_at + (size_t)g * MMF_W * MMF_H + (size_t)k] =
                (uint8_t)(((unsigned)(g * 31 + k * 7) % 5) < 2 ? 41 : 0);
    at = data_at + data_size;
    for (int g = 0; g < MMF_COUNT; g++) at = mmf_put32(bytes, at, (uint32_t)(g * MMF_W * MMF_H));
    bytes[0] = 5;
    bytes[1] = 0;
    bytes[2] = 0x28;
    bytes[3] = 0;
    mmf_put32(bytes, 4, (uint32_t)(at - 8));
    if ((at - 8) & 1) bytes[at++] = 0;
    return es2_dfn_parse((es2_span){bytes, at}, out, NULL) == ES2_OK ? 0 : -1;
}

/* Image k (0..4) of the background: bands whose index depends on k. */
static inline void mmf_image(uint8_t *pixels, int k) {
    for (int y = 0; y < 480; y++)
        for (int x = 0; x < 640; x++)
            pixels[y * 640 + x] = (uint8_t)(64 + 24 * k + ((x / 16 + y / 12 + k * 3) % 20));
}

/* A palette in DPL record form (6-bit values, flag 1). */
static inline void mmf_palette(uint8_t records[256][4]) {
    for (int i = 0; i < 256; i++) {
        records[i][0] = (uint8_t)(i % 64);
        records[i][1] = (uint8_t)((i * 3) % 64);
        records[i][2] = (uint8_t)((i * 7) % 64);
        records[i][3] = 1;
    }
}

#endif
