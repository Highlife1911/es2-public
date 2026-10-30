/* Test driver for //shell:draw_real_test. Our side of the in-vitro
 * comparison with VSHELL's blitter 0x45a09c and decompressor 0x453a50 (the
 * memory worker's slot-mode ops blit and unpack, record formats in
 * tools/reference-game/shell-invitro/main.c, s26_op), and the case
 * generators:
 *
 *   draw_tool cases SEED COUNT OUT        synthetic blit records
 *   draw_tool cd-cases CD_ROOT SEED COUNT OUT
 *                                         blit records of SHELL0.VOL images
 *                                         into 640 x 480 frames
 *   draw_tool rle-cases SEED COUNT OUT    kind-1 unpack records
 *   draw_tool blit IN OUT                 es2_draw_blit per record
 *   draw_tool unpack IN OUT               es2_draw_bitmap_from_dbm per record
 *
 * Files are written only where the test says (its temporary directory).
 * CD pixels never leave that directory; the test prints hashes and counts. */
#include "es2/formats/vol.h"
#include "es2/shell/draw.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t state;
static uint32_t rnd(void)
{
    state = state * 1664525u + 1013904223u;
    return state >> 8;
}

static void put16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, v & 0xffffu); put16(p + 2, v >> 16); }
static unsigned get16(const uint8_t *p) { return p[0] | (unsigned)p[1] << 8; }
static uint32_t get32(const uint8_t *p) { return get16(p) | (uint32_t)get16(p + 2) << 16; }

_Noreturn static void die(const char *message)
{
    fprintf(stderr, "draw_tool: %s\n", message);
    exit(2);
}

static uint8_t *slurp(const char *path, size_t *size)
{
    FILE *file = fopen(path, "rb");
    long n = 0;
    uint8_t *bytes;
    if (!file) die("cannot open input");
    if (fseek(file, 0, SEEK_END) || (n = ftell(file)) < 0 || fseek(file, 0, SEEK_SET)) die("seek");
    bytes = malloc((size_t)n + 1);
    if (!bytes || fread(bytes, 1, (size_t)n, file) != (size_t)n) die("read");
    fclose(file);
    *size = (size_t)n;
    return bytes;
}

static FILE *create(const char *path)
{
    FILE *file = fopen(path, "wb");
    if (!file) die("cannot create output");
    return file;
}

static void emit(FILE *out, const void *data, size_t size)
{
    if (size && fwrite(data, 1, size, out) != size) die("write");
}

/* One blit record (main.c s26_op): header, frame, pixels. */
static void record(FILE *out, unsigned fw, unsigned fh, unsigned bw, unsigned bh, unsigned flags,
                   unsigned mirror, int has_clip, int32_t x, int32_t y, const es2_rect *clip,
                   const uint8_t *frame, const uint8_t *pixels)
{
    uint8_t h[36] = {0};
    put16(h, fw); put16(h + 2, fh); put16(h + 4, bw); put16(h + 6, bh);
    h[8] = (uint8_t)flags; h[9] = (uint8_t)mirror; h[10] = (uint8_t)has_clip;
    put32(h + 12, (uint32_t)x); put32(h + 16, (uint32_t)y);
    put32(h + 20, (uint32_t)clip->left); put32(h + 24, (uint32_t)clip->top);
    put32(h + 28, (uint32_t)clip->right); put32(h + 32, (uint32_t)clip->bottom);
    emit(out, h, sizeof h);
    emit(out, frame, (size_t)fw * fh);
    emit(out, pixels, (size_t)bw * bh);
}

/* A clip inside the frame, sometimes the whole frame or one row/column. */
static void random_clip(es2_rect *clip, unsigned fw, unsigned fh)
{
    switch (rnd() % 6) {
    case 0: clip->left = 0; clip->top = 0; clip->right = (int32_t)fw - 1; clip->bottom = (int32_t)fh - 1; break;
    case 1: clip->left = clip->right = (int32_t)(rnd() % fw);
            clip->top = (int32_t)(rnd() % fh); clip->bottom = (int32_t)fh - 1; break;
    case 2: clip->top = clip->bottom = (int32_t)(rnd() % fh);
            clip->left = (int32_t)(rnd() % fw); clip->right = (int32_t)fw - 1; break;
    default:
        clip->left = (int32_t)(rnd() % fw); clip->right = clip->left + (int32_t)(rnd() % (fw - (unsigned)clip->left));
        clip->top = (int32_t)(rnd() % fh); clip->bottom = clip->top + (int32_t)(rnd() % (fh - (unsigned)clip->top));
    }
}

/* Three of four positions overlap the drawing region (clip or frame), the
 * others anywhere within a bitmap's size around the frame. */
static void place(int has_clip, const es2_rect *clip, unsigned fw, unsigned fh, unsigned bw,
                  unsigned bh, int32_t *x, int32_t *y)
{
    es2_rect r = {0, 0, (int32_t)fw - 1, (int32_t)fh - 1};
    if (has_clip) r = *clip;
    if (rnd() % 4) {
        *x = r.left - (int32_t)bw + 1 + (int32_t)(rnd() % (unsigned)(r.right - r.left + (int32_t)bw));
        *y = r.top - (int32_t)bh + 1 + (int32_t)(rnd() % (unsigned)(r.bottom - r.top + (int32_t)bh));
    } else {
        *x = (int32_t)(rnd() % (fw + 2 * bw + 4)) - (int32_t)bw - 2;
        *y = (int32_t)(rnd() % (fh + 2 * bh + 4)) - (int32_t)bh - 2;
    }
}

static int cases(unsigned seed, unsigned count, const char *path)
{
    static uint8_t frame[96 * 72], pixels[80 * 80];
    FILE *out = create(path);
    unsigned n, i;
    state = seed;
    for (n = 0; n < count; ++n) {
        unsigned fw = 8 + rnd() % 89, fh = 8 + rnd() % 65;
        unsigned bw = 1 + rnd() % 80, bh = 1 + rnd() % 80;
        unsigned flags = (rnd() & 1 ? ES2_BITMAP_OPAQUE : 0) | (rnd() % 4 ? 0 : (rnd() & 0xef));
        unsigned mirror = n % 4 | (rnd() % 8 ? 0 : (rnd() & 0xfc));
        int has_clip = (int)(rnd() % 2);
        int32_t x, y;
        es2_rect clip;
        random_clip(&clip, fw, fh);
        place(has_clip, &clip, fw, fh, bw, bh, &x, &y);
        for (i = 0; i < fw * fh; ++i) frame[i] = (uint8_t)(rnd() | 1);
        for (i = 0; i < bw * bh; ++i) pixels[i] = (uint8_t)(rnd() % 3 ? rnd() : 0);
        record(out, fw, fh, bw, bh, flags & 0xffu, mirror & 0xffu, has_clip, x, y, &clip, frame, pixels);
    }
    fclose(out);
    return 0;
}

static int has_suffix(es2_span name, const char *suffix)
{
    size_t n = strlen(suffix), i;
    if (name.size < n) return 0;
    for (i = 0; i < n; ++i) {
        uint8_t c = name.data[name.size - n + i];
        if (c >= 'a' && c <= 'z') c = (uint8_t)(c - 32);
        if (c != (uint8_t)suffix[i]) return 0;
    }
    return 1;
}

/* Every k-th image of SHELL0.VOL's DBA/DBM members (in member order) at a
 * random position, clip and mirror mode over a patterned 640 x 480 frame. */
static int cd_cases(const char *root, unsigned seed, unsigned count, const char *path)
{
    static uint8_t frame[ES2_FRAME_WIDTH * ES2_FRAME_HEIGHT];
    char vol_path[4096];
    size_t size, m, images = 0, picked = 0, i;
    uint8_t *bytes;
    es2_vol vol;
    FILE *out;
    if (snprintf(vol_path, sizeof vol_path, "%s/VOL/SHELL0.VOL", root) >= (int)sizeof vol_path) die("path");
    bytes = slurp(vol_path, &size);
    if (es2_vol_open((es2_span){bytes, size}, &vol, NULL) != ES2_OK) die("SHELL0.VOL");
    for (m = 0; m < vol.member_count; ++m) {
        es2_vol_member member;
        es2_dba dba;
        es2_dbm dbm;
        if (es2_vol_member_at(&vol, m, &member, NULL) != ES2_OK) die("member");
        if (has_suffix(member.name, ".DBA") && es2_dba_parse_prefix(member.payload, &dba, NULL) == ES2_OK)
            images += dba.count;
        else if (has_suffix(member.name, ".DBM") && es2_dbm_parse(member.payload, &dbm, NULL) == ES2_OK)
            images += 1;
    }
    out = create(path);
    state = seed;
    for (m = 0; m < vol.member_count && picked < count; ++m) {
        es2_vol_member member;
        es2_dba dba;
        es2_dbm dbm;
        size_t k, n = 1;
        es2_vol_member_at(&vol, m, &member, NULL);
        if (has_suffix(member.name, ".DBA")) {
            if (es2_dba_parse_prefix(member.payload, &dba, NULL) != ES2_OK) continue;
            n = dba.count;
        } else if (!has_suffix(member.name, ".DBM")) {
            continue;
        }
        for (k = 0; k < n && picked < count; ++k) {
            es2_bitmap b;
            es2_rect clip;
            int has_clip;
            int32_t x, y;
            if (n == 1 && has_suffix(member.name, ".DBM")) {
                if (es2_dbm_parse(member.payload, &dbm, NULL) != ES2_OK) die("dbm");
            } else if (es2_dba_image_at(&dba, k, &dbm, NULL) != ES2_OK) {
                die("dba member");
            }
            if (rnd() % (images / count + 1) != 0) continue;
            if (es2_draw_bitmap_from_dbm(&dbm, NULL, 0, &b) != ES2_DRAW_DONE) die("not raw");
            has_clip = (int)(rnd() % 2);
            random_clip(&clip, ES2_FRAME_WIDTH, ES2_FRAME_HEIGHT);
            place(has_clip, &clip, ES2_FRAME_WIDTH, ES2_FRAME_HEIGHT, b.width, b.height, &x, &y);
            for (i = 0; i < sizeof frame; ++i) frame[i] = (uint8_t)(i * 13 + picked);
            record(out, ES2_FRAME_WIDTH, ES2_FRAME_HEIGHT, b.width, b.height,
                   b.flags | (rnd() % 4 ? 0 : ES2_BITMAP_OPAQUE), (unsigned)picked % 4, has_clip, x, y,
                   &clip, frame, b.pixels);
            ++picked;
        }
    }
    fclose(out);
    free(bytes);
    printf("cd-cases: %zu images in SHELL0.VOL, %zu picked\n", images, picked);
    return 0;
}

/* Kind-1 streams (dbm.md: control c, bit 7 = run of the next byte, c & 0x7f
 * times; else c literal bytes; counts 0 allowed) that produce at least U
 * bytes, the last run or literal often cut by U. */
static int rle_cases(unsigned seed, unsigned count, const char *path)
{
    static uint8_t stream[200000];
    FILE *out = create(path);
    unsigned n;
    state = seed;
    for (n = 0; n < count; ++n) {
        uint32_t u = 1 + rnd() % (n % 8 == 0 ? 60000 : 600), produced = 0;
        size_t at = 4;
        uint8_t h[6];
        put32(stream, u);
        while (produced < u) {
            unsigned c = rnd() & 0xff, k;
            if (rnd() % 16 == 0) c &= 0x80; /* zero counts */
            stream[at++] = (uint8_t)c;
            if (c & 0x80) {
                stream[at++] = (uint8_t)rnd();
            } else {
                for (k = 0; k < c; ++k) stream[at++] = (uint8_t)rnd();
            }
            produced += c & 0x7f;
        }
        for (; rnd() % 3 == 0;) stream[at++] = (uint8_t)rnd(); /* trailing bytes */
        put16(h, 1);
        put32(h + 2, (uint32_t)at);
        emit(out, h, 6);
        emit(out, stream, at);
    }
    fclose(out);
    return 0;
}

static int blit(const char *in_path, const char *out_path)
{
    size_t size, at = 0;
    uint8_t *in = slurp(in_path, &size);
    FILE *out = create(out_path);
    unsigned counts[3] = {0};
    while (at < size) {
        const uint8_t *r = in + at;
        unsigned fw, fh, bw, bh;
        size_t frame_bytes, pixel_bytes;
        es2_frame f;
        es2_bitmap b;
        es2_rect clip;
        uint8_t *frame;
        es2_draw_result result;
        if (size - at < 36) die("truncated record");
        fw = get16(r); fh = get16(r + 2); bw = get16(r + 4); bh = get16(r + 6);
        frame_bytes = (size_t)fw * fh;
        pixel_bytes = (size_t)bw * bh;
        if (size - at - 36 < frame_bytes + pixel_bytes) die("bad record");
        frame = malloc(frame_bytes + 1);
        if (!frame) die("memory");
        memcpy(frame, r + 36, frame_bytes);
        f.pixels = frame; f.size = frame_bytes; f.width = (int32_t)fw; f.height = (int32_t)fh;
        f.stride = (int32_t)fw;
        b.height = (uint16_t)bh; b.width = (uint16_t)bw; b.flags = r[8]; b.pixels = r + 36 + frame_bytes;
        clip.left = (int32_t)get32(r + 20); clip.top = (int32_t)get32(r + 24);
        clip.right = (int32_t)get32(r + 28); clip.bottom = (int32_t)get32(r + 32);
        result = es2_draw_blit(&f, &b, (int32_t)get32(r + 12), (int32_t)get32(r + 16),
                               r[10] ? &clip : NULL, r[9]);
        if (result < 0) die("refused record");
        ++counts[result];
        emit(out, frame, frame_bytes);
        free(frame);
        at += 36 + frame_bytes + pixel_bytes;
    }
    fclose(out);
    free(in);
    printf("blit: %u drawn, %u outside, %u empty\n", counts[0], counts[1], counts[2]);
    return 0;
}

static int unpack(const char *in_path, const char *out_path)
{
    size_t size, at = 0;
    uint8_t *in = slurp(in_path, &size);
    static uint8_t scratch[65536];
    FILE *out = create(out_path);
    while (at < size) {
        const uint8_t *r = in + at;
        uint32_t n, u;
        es2_dbm image;
        es2_bitmap b;
        uint8_t word[4];
        if (size - at < 6 || size - at - 6 < get32(r + 2) || get32(r + 2) < 4) die("bad record");
        n = get32(r + 2);
        u = get32(r + 6);
        if (get16(r) != 1 || u == 0 || u > 65535) die("only kind 1 with 1 <= U <= 65535");
        memset(&image, 0, sizeof image);
        image.height = 1; image.width = (uint16_t)u; image.bits_per_pixel = 8; image.compression = 1;
        image.pixels.data = r + 6; image.pixels.size = n;
        if (es2_draw_bitmap_from_dbm(&image, scratch, sizeof scratch, &b) != ES2_DRAW_DONE)
            die("decode refused");
        put32(word, u);
        emit(out, word, 4);
        emit(out, b.pixels, u);
        at += 6 + (size_t)n;
    }
    fclose(out);
    free(in);
    return 0;
}

/* The scene resources in SHELL0.VOL: the 20 palettes of the table (colour
 * count, entries written by the conversion, flags), the six background
 * frames (size, kind, drawn into a frame) and the mission screen's zone
 * images. Counts only. */
static int scenes(const char *root)
{
    static uint8_t frame[ES2_FRAME_WIDTH * ES2_FRAME_HEIGHT];
    static const char *const zones[4] = {"dba\\alph2.dba", "dba\\delt1.dba", "dba\\omic1.dba", "dba\\brav1.dba"};
    char vol_path[4096];
    size_t size;
    uint8_t *bytes;
    es2_vol vol;
    es2_frame f = {frame, sizeof frame, ES2_FRAME_WIDTH, ES2_FRAME_HEIGHT, ES2_FRAME_WIDTH};
    int k;
    if (snprintf(vol_path, sizeof vol_path, "%s/VOL/SHELL0.VOL", root) >= (int)sizeof vol_path) die("path");
    bytes = slurp(vol_path, &size);
    if (es2_vol_open((es2_span){bytes, size}, &vol, NULL) != ES2_OK) die("SHELL0.VOL");
    for (k = 0; k < ES2_PALETTE_COUNT; ++k) {
        es2_vol_member member;
        es2_dpl dpl;
        uint8_t rgb[768];
        unsigned bit0 = 0, bit1 = 0, none = 0, i;
        if (es2_vol_find(&vol, es2_palette_file(k), &member, NULL) != ES2_OK ||
            es2_dpl_parse(member.payload, &dpl, NULL) != ES2_OK) {
            printf("MISSING palette %d\n", k);
            continue;
        }
        for (i = 0; i < dpl.colour_count; ++i) {
            es2_dpl_colour c;
            es2_dpl_colour_at(&dpl, i, &c, NULL);
            if (c.flags & 1) ++bit0; else if (c.flags & 2) ++bit1; else ++none;
        }
        memset(rgb, 0, sizeof rgb);
        printf("palette %d: %u colours, %d written, flags bit0 %u bit1 %u none %u\n", k, dpl.colour_count,
               es2_palette_convert(&dpl, rgb), bit0, bit1, none);
    }
    for (k = 0; k < ES2_MENU_BACKGROUND_FRAMES; ++k) {
        es2_vol_member member;
        es2_dbm dbm;
        es2_draw_result r;
        if (es2_vol_find(&vol, es2_menu_background_file(k), &member, NULL) != ES2_OK ||
            es2_dbm_parse(member.payload, &dbm, NULL) != ES2_OK) {
            printf("MISSING background %d\n", k);
            continue;
        }
        r = es2_draw_dbm(&f, &dbm, 0, 0, NULL, 0, NULL, 0);
        printf("background %d: %ux%u kind %u flags %u draw %d\n", k, dbm.width, dbm.height, dbm.compression,
               dbm.flags, (int)r);
    }
    for (k = 0; k < 4; ++k) {
        es2_vol_member member;
        es2_dba dba;
        if (es2_vol_find(&vol, zones[k], &member, NULL) != ES2_OK ||
            es2_dba_parse(member.payload, &dba, NULL) != ES2_OK) {
            printf("MISSING zone %d\n", k);
            continue;
        }
        printf("zone %d: %u images, draw %d\n", k, dba.count,
               (int)es2_draw_dba_member(&f, &dba, 0, 0, 0, NULL, 0, NULL, 0));
    }
    free(bytes);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 3 && !strcmp(argv[1], "scenes"))
        return scenes(argv[2]);
    if (argc == 5 && !strcmp(argv[1], "cases"))
        return cases((unsigned)strtoul(argv[2], NULL, 0), (unsigned)strtoul(argv[3], NULL, 0), argv[4]);
    if (argc == 6 && !strcmp(argv[1], "cd-cases"))
        return cd_cases(argv[2], (unsigned)strtoul(argv[3], NULL, 0), (unsigned)strtoul(argv[4], NULL, 0),
                        argv[5]);
    if (argc == 5 && !strcmp(argv[1], "rle-cases"))
        return rle_cases((unsigned)strtoul(argv[2], NULL, 0), (unsigned)strtoul(argv[3], NULL, 0), argv[4]);
    if (argc == 4 && !strcmp(argv[1], "blit"))
        return blit(argv[2], argv[3]);
    if (argc == 4 && !strcmp(argv[1], "unpack"))
        return unpack(argv[2], argv[3]);
    fputs("usage: draw_tool cases|cd-cases|rle-cases|blit|unpack ...\n", stderr);
    return 2;
}
