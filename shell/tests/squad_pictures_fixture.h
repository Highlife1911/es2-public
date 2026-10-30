#ifndef ES2_SHELL_TESTS_SQUAD_PICTURES_FIXTURE_H
#define ES2_SHELL_TESTS_SQUAD_PICTURES_FIXTURE_H

/* Synthetic Herc picture files (gam\arm_<herc>.dat, gam\rpr_<herc>.dat,
 * gam\arm_hots.dat, gam\rpr_hots.dat in docs/formats/herc_pictures.md's
 * layout), seeded frames for every DBA the pictures name, and eight
 * synthetic hangar records. Shared by squad_pictures_test and the
 * squad_pictures suite of screen_test. Nothing here comes from the CD. */
#include <stdio.h>
#include <string.h>

#include "es2/shell/gui/squad_pictures.h"

typedef struct spf_buffer {
    uint8_t data[1024];
    size_t size;
} spf_buffer;

static inline void spf_w(spf_buffer *b, int v) {
    b->data[b->size++] = (uint8_t)v;
    b->data[b->size++] = (uint8_t)((unsigned)v >> 8);
}
static inline void spf_d(spf_buffer *b, int32_t v) {
    spf_w(b, (int)(v & 0xffff));
    spf_w(b, (int)((uint32_t)v >> 16));
}
/* a record: layer, x, y, (x2, y2), frame, mirror */
static inline void spf_rec(spf_buffer *b, int layer, int x, int y, int x2, int y2, int frame, int mirror, int wide) {
    spf_w(b, layer);
    spf_d(b, x);
    spf_d(b, y);
    if (wide) {
        spf_d(b, x2);
        spf_d(b, y2);
    }
    spf_w(b, frame);
    spf_w(b, mirror);
}

typedef struct spf_fixture {
    spf_buffer arm[9], rpr[9], arm_hots, rpr_hots;
    /* frames: up to 64 (file, frame) pairs with seeded 10 x 8 pixels */
    char names[64][24];
    int frames[64];
    uint8_t pixels[64][10 * 8];
    es2_bitmap bitmaps[64];
    int count;
    es2_squad_record bays[8];
    int has[8];
    char error[160];
    int errors;
    char text[16];
} spf_fixture;

/* arm file of type t: body (layer 0, (1 + t, 1), frame 0) and outline
 * (layer 1, (1, 140), frame 1, mirror t & 1); item 0: keys 2, 3, 4 (the
 * empty-slot markers, frame 7); item 1: keys 2, 3, 4 (frames 2..4, key 2
 * mirrored); item 5: key 2 with frame -1, key 3 frame 6. */
static inline void spf_arm(spf_buffer *b, int t) {
    b->size = 0;
    spf_rec(b, 0, 1 + t, 1, 0, 0, 0, 0, 1);
    spf_rec(b, 1, 1, 140, 0, 0, 1, t & 1, 1);
    spf_w(b, 3);
    spf_w(b, 0), spf_w(b, 3);
    for (int k = 0; k < 3; k++) spf_rec(b, 2 + k, 10 + 40 * k, 100, 20 + 40 * k, 60, 7, 0, 1);
    spf_w(b, 1), spf_w(b, 3);
    for (int k = 0; k < 3; k++) spf_rec(b, 2 + k, 12 + 40 * k, 104, 22 + 40 * k, 64, 2 + k, k == 0 ? 2 : 0, 1);
    spf_w(b, 5), spf_w(b, 2);
    spf_rec(b, 2, 30, 30, 31, 31, -1, 0, 1);
    spf_rec(b, 3, 70, 30, 71, 31, 6, 0, 1);
}
/* rpr file of type t: layers 0, 2 and 17 (frames 0..2), the internal
 * picture (layer 0, (20, 30), frame 0); item 1: keys 6, 7, 8 (frames 3..5);
 * item 5: key 7 only (frame 6). */
static inline void spf_rpr(spf_buffer *b, int t) {
    b->size = 0;
    spf_w(b, 3);
    spf_rec(b, 0, 5 + t, 5, 0, 0, 0, 0, 0);
    spf_rec(b, 2, 60, 10, 0, 0, 1, 2, 0);
    spf_rec(b, 17, 100, 50, 0, 0, 2, 0, 0);
    spf_rec(b, 0, 20, 30, 0, 0, 0, 0, 0);
    spf_w(b, 2);
    spf_w(b, 1), spf_w(b, 3);
    for (int k = 0; k < 3; k++) spf_rec(b, 6 + k, 15 + 30 * k, 150, 0, 0, 3 + k, 0, 0);
    spf_w(b, 5), spf_w(b, 1);
    spf_rec(b, 7, 90, 160, 0, 0, 6, 0, 0);
}
/* hots: nine types of four rectangles each */
static inline void spf_hots(spf_buffer *b, int base) {
    b->size = 0;
    spf_w(b, 9);
    for (int t = 0; t < 9; t++) {
        spf_w(b, t), spf_w(b, 4);
        for (int k = 0; k < 4; k++) {
            spf_d(b, base + 10 * k), spf_d(b, base + t), spf_d(b, base + 10 * k + 8), spf_d(b, base + t + 12);
        }
    }
}

/* the eight bays: 0 finished type 0, capacity 3, items {5, 0, 1}; 1 type 1
 * at progress 0; 2 type 2 at 30; 3 empty; 4 finished type 8, capacity 4,
 * items {1, 1, 0, 0}; 5 type 5 at 70; 6 finished type 6, capacity 2, items
 * {0, 1}; 7 finished type 7, capacity 10, every slot item 1 */
static inline void spf_bays(spf_fixture *f) {
    static const int16_t vehicle[8] = {0, 1, 2, -1, 8, 5, 6, 7};
    static const int16_t progress[8] = {100, 0, 30, 0, 100, 70, 100, 100};
    static const int16_t capacity[8] = {3, 2, 2, 0, 4, 3, 2, 10};
    memset(f->bays, 0, sizeof f->bays);
    for (int k = 0; k < 8; k++) {
        es2_squad_record *d = &f->bays[k];
        f->has[k] = k != 3;
        if (!f->has[k]) continue;
        d->record = 1;
        d->vehicle = vehicle[k];
        d->progress = progress[k];
        d->capacity = capacity[k];
        for (int w = 0; w < 33; w++) d->words[w] = (int16_t)(100 - ((w * 7 + k * 13) % 101));
    }
    f->bays[0].item[0] = 5, f->bays[0].item[2] = 1;
    f->bays[4].item[0] = 1, f->bays[4].item[1] = 1;
    f->bays[6].item[1] = 1;
    for (int s = 0; s < 10; s++) f->bays[7].item[s] = 1;
}

static inline void spf_init(spf_fixture *f) {
    memset(f, 0, sizeof *f);
    for (int t = 0; t < 9; t++) {
        spf_arm(&f->arm[t], t);
        spf_rpr(&f->rpr[t], t);
    }
    spf_hots(&f->arm_hots, 100);
    spf_hots(&f->rpr_hots, 50);
    spf_bays(f);
}

/* ---- the host -------------------------------------------------------------------- */
static inline es2_span spf_file(void *ctx, const char *name) {
    spf_fixture *f = ctx;
    static const char *const herc[9] = {"outl", "rapt", "toma", "sams", "colo", "apoc", "ogre", "mavr", "razr"};
    char want[32];
    for (int t = 0; t < 9; t++) {
        snprintf(want, sizeof want, "gam\\arm_%s.dat", herc[t]);
        if (!strcmp(name, want)) return (es2_span){f->arm[t].data, f->arm[t].size};
        snprintf(want, sizeof want, "gam\\rpr_%s.dat", herc[t]);
        if (!strcmp(name, want)) return (es2_span){f->rpr[t].data, f->rpr[t].size};
    }
    if (!strcmp(name, "gam\\arm_hots.dat")) return (es2_span){f->arm_hots.data, f->arm_hots.size};
    if (!strcmp(name, "gam\\rpr_hots.dat")) return (es2_span){f->rpr_hots.data, f->rpr_hots.size};
    return (es2_span){NULL, 0};
}
/* A seeded 10 x 8 frame per (file, frame); pixels include 0x0e, 0x0f and
 * 0xba (the recolour sources) and 0 (transparent). */
static inline const es2_bitmap *spf_frame(void *ctx, const char *file, int n) {
    spf_fixture *f = ctx;
    unsigned seed = 2166136261u;
    if (n < 0 || n > 40) return NULL;
    for (int i = 0; i < f->count; i++)
        if (f->frames[i] == n && !strcmp(f->names[i], file)) return &f->bitmaps[i];
    if (f->count == 64) return NULL;
    for (const char *p = file; *p; p++) seed = (seed ^ (uint8_t)*p) * 16777619u;
    seed = (seed ^ (unsigned)n) * 16777619u;
    int i = f->count++;
    snprintf(f->names[i], sizeof f->names[i], "%s", file);
    f->frames[i] = n;
    for (int p = 0; p < 80; p++) {
        static const uint8_t pick[8] = {0, 0x0e, 0x0f, 0xba, 0x30, 0x50, 0x70, 0x90};
        seed = seed * 1103515245u + 12345u;
        f->pixels[i][p] = pick[(seed >> 16) & 7] + (uint8_t)((seed >> 24) & 3) * ((seed >> 19 & 7) > 3);
    }
    f->bitmaps[i] = (es2_bitmap){8, 10, 0, f->pixels[i]};
    return &f->bitmaps[i];
}
static inline int spf_record(void *ctx, int k, es2_squad_record *out) {
    spf_fixture *f = ctx;
    if (k < 0 || k > 7 || !f->has[k]) return 0;
    *out = f->bays[k];
    return 1;
}
static inline void spf_error(void *ctx, const char *text) {
    spf_fixture *f = ctx;
    f->errors++;
    snprintf(f->error, sizeof f->error, "%s", text);
}
static inline const char *spf_text(void *ctx, uint32_t id) {
    spf_fixture *f = ctx;
    snprintf(f->text, sizeof f->text, "T%02X", (unsigned)id);
    return f->text;
}
static inline void spf_host(spf_fixture *f, es2_squad_pictures *p) {
    memset(p, 0, sizeof *p);
    p->host = (es2_squad_pictures_host){f, spf_file, spf_frame, spf_record, spf_error, spf_text};
    /* part groups: {0, 1, -1}, {2, -1}, {3, 4, 5, -1}, {6, -1}, {7, 8, -1}, {9, 10, 11, 12} (unterminated) */
    static const int16_t groups[24] = {0, 1, -1, 0, 2, -1, 0, 0, 3, 4, 5, -1, 6, -1, 0, 0, 7, 8, -1, 0, 9, 10, 11, 12};
    memcpy(p->groups, groups, sizeof groups);
    for (int i = 0; i < 9; i++) p->internal_from[i] = (int16_t)(0x20 + i);
}

#endif
