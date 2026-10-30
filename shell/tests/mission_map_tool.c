/* Test driver of the mission map's raster layer (es2/shell/gui/mission_map.h)
 * for //shell:mission_map_real_test.
 *
 *   mission_map_tool raster <map.in> <map.emit>
 *       draws every record of map.in with our primitives and writes what the
 *       memory worker's `map-raster` writes (tools/reference-game/
 *       shell-invitro/main.c, s40_raster: format there): the SHA-256 of the
 *       canvas per record, followed by the canvas with flag 0x01.
 *
 * Synthetic cases only; no game data is read. */
#include "es2/shell/gui/mission_map.h"
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
static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

#define RECORD 124

static void hash_out(FILE *out, const uint8_t *canvas, size_t n, int with_canvas) {
    es2f_sha256 s;
    char hex[65];
    uint8_t hash[32];
    es2f_sha256_init(&s);
    es2f_sha256_update(&s, canvas, n);
    es2f_sha256_hex(&s, hex);
    for (int i = 0; i < 32; i++) {
        unsigned b;
        sscanf(hex + 2 * i, "%2x", &b);
        hash[i] = (uint8_t)b;
    }
    fwrite(hash, 1, 32, out);
    if (with_canvas) fwrite(canvas, 1, n, out);
}

static int blank_only;

static int raster(const char *in_path, const char *out_path) {
    size_t size = 0, at = 4;
    uint8_t *in = read_file(in_path, &size), *canvas;
    FILE *out;
    if (!in || size < 4) return 2;
    unsigned cw = le16(in), ch = le16(in + 2);
    if (!cw || !ch || cw > 640 || ch > 480) return 2;
    canvas = malloc((size_t)cw * ch);
    if (!canvas || !(out = fopen(out_path, "wb"))) return 2;
    es2_map_surface surface = {canvas, (int32_t)cw, (int32_t)ch, (int32_t)cw};
    while (at + RECORD <= size) {
        const uint8_t *r = in + at;
        unsigned kind = r[0], flags = r[3], bw = le16(r + 116), bh = le16(r + 118);
        int32_t a[20];
        uint32_t seed = le32(r + 28), colour = le32(r + 32);
        if (size - at - RECORD < (size_t)bw * bh) return 3;
        for (int i = 0; i < 20; i++) a[i] = (int32_t)le32(r + 36 + 4 * i);
        es2_bitmap b = {(uint16_t)bh, (uint16_t)bw, r[120], r + RECORD};
        es2_map_dc dc;
        memset(&dc, 0, sizeof dc);
        dc.surface = &surface;
        dc.clip_mode = r[1];
        dc.offset = r[2];
        dc.left = (int32_t)le32(r + 4);
        dc.top = (int32_t)le32(r + 8);
        dc.right = (int32_t)le32(r + 12);
        dc.bottom = (int32_t)le32(r + 16);
        dc.origin_x = (int32_t)le32(r + 20);
        dc.origin_y = (int32_t)le32(r + 24);
        dc.pen = colour;
        dc.brush = colour;
        dc.brush_kind = kind == 6 ? 4 : 0;
        dc.screen_height = (int32_t)ch;
        at += RECORD + (size_t)bw * bh;
        for (size_t i = 0; i < (size_t)cw * ch; i++) {
            seed = seed * 1103515245u + 12345u;
            canvas[i] = (uint8_t)((seed >> 16) & 0xff);
        }
        switch (blank_only ? 99u : kind) {
        case 99: break;
        case 0: {
            uint16_t c[3] = {(uint16_t)a[6], (uint16_t)a[7], (uint16_t)a[8]};
            es2_map_triangle(&dc, a, c);
            break;
        }
        case 1: es2_map_polygon(&dc, a[0], a + 1); break;
        case 2: es2_map_textured_quad(&dc, &b, a[0], a[1], a[2], a[3]); break;
        case 3: es2_map_line(&dc, a[0], a[1], a[2], a[3]); break;
        case 4: es2_map_ellipse(&dc, a[0], a[1], a[2], a[3]); break;
        case 5: es2_map_pixel(&dc, a[0], a[1]); break;
        case 6: case 7: es2_map_rect(&dc, a[0], a[1], a[2], a[3]); break;
        case 8: es2_map_bitmap(&dc, &b, a[0], a[1]); break;
        case 9: es2_map_scaled(&dc, &b, a[0], a[1], a[2], a[3]); break;
        default: return 3;
        }
        hash_out(out, canvas, (size_t)cw * ch, flags & 1);
    }
    fclose(out);
    free(canvas);
    free(in);
    return 0;
}

/* ---- the map: the same operations as the worker's map-* ops ---------------- */

static void hash_bytes(const uint8_t *data, size_t n, uint8_t out[32]) {
    char hex[65];
    es2f_sha256_of(data ? (const void *)data : (const void *)"", n, hex);
    for (int i = 0; i < 32; i++) {
        unsigned b;
        sscanf(hex + 2 * i, "%2x", &b);
        out[i] = (uint8_t)b;
    }
}

/* A VSHELL.EXE virtual address to the file bytes (the hash-checked image). */
static const uint8_t *exe_at(const uint8_t *image, size_t size, uint32_t va, size_t n) {
    if (size < 64) return NULL;
    uint32_t pe = le32(image + 60);
    if (pe + 24 > size) return NULL;
    unsigned count = le16(image + pe + 6), optional = le16(image + pe + 20);
    for (unsigned i = 0; i < count; i++) {
        size_t at = pe + 24 + optional + 40 * (size_t)i;
        if (at + 40 > size) return NULL;
        uint32_t address = le32(image + at + 12), raw_size = le32(image + at + 16), raw = le32(image + at + 20);
        uint32_t off = va - 0x400000u - address;
        if (va >= 0x400000u + address && off + n <= raw_size && raw + off + n <= size) return image + raw + off;
    }
    return NULL;
}

#include <dirent.h>
#include "es2/formats/dbm.h"
#include "es2/formats/vfs.h"
#include "es2/formats/vol.h"

static es2_vol vols[32];
static es2_vfs_source sources[32];
static es2_vfs vfs;

static int mount(const char *cd) {
    char path[1024];
    unsigned n = 0;
    snprintf(path, sizeof path, "%s/VOL", cd);
    DIR *d = opendir(path);
    struct dirent *e;
    if (!d) return -1;
    while ((e = readdir(d)) && n < 32) {
        size_t len = strlen(e->d_name);
        if (len < 4 || (strcmp(e->d_name + len - 4, ".VOL") && strcmp(e->d_name + len - 4, ".vol"))) continue;
        size_t size = 0;
        snprintf(path, sizeof path, "%s/VOL/%s", cd, e->d_name);
        uint8_t *data = read_file(path, &size);
        if (!data || es2_vol_open((es2_span){data, size}, &vols[n], NULL) != ES2_OK) { closedir(d); return -1; }
        sources[n].kind = ES2_VFS_VOL;
        sources[n].data.vol = &vols[n];
        n++;
    }
    closedir(d);
    /* the worker's order: by priority, higher first (selection sort as there) */
    for (unsigned i = 0; i < n; i++)
        for (unsigned j = i + 1; j < n; j++)
            if (sources[j].data.vol->priority > sources[i].data.vol->priority) {
                es2_vfs_source t = sources[i];
                sources[i] = sources[j];
                sources[j] = t;
            }
    return es2_vfs_open(sources, n, &vfs, NULL) == ES2_OK ? 0 : -1;
}

static es2_span cd_file(const char *name) {
    es2_vfs_copy c;
    if (es2_vfs_find(&vfs, name, &c, NULL) != ES2_OK) return (es2_span){NULL, 0};
    return c.payload;
}

static void probe(FILE *out, const es2_mission_map *m, const es2_mission_map_globals *g, const uint8_t *frame,
                  unsigned result) {
    uint8_t p[64];
    memset(p, 0, sizeof p);
    hash_bytes(frame, 640 * 480, p);
    p[32] = (uint8_t)m->state;
    p[33] = (uint8_t)(m->state >> 8);
    p[34] = (uint8_t)result;
    p[35] = m->done;
    for (int i = 0; i < 4; i++) p[36 + i] = (uint8_t)(g->tick >> (8 * i));
    for (int k = 0; k < 3; k++)
        for (int i = 0; i < 4; i++) p[40 + 4 * k + i] = (uint8_t)((uint32_t)m->view[k] >> (8 * i));
    for (int k = 0; k < 2; k++)
        for (int i = 0; i < 4; i++) p[52 + 4 * k + i] = (uint8_t)((uint32_t)m->pan[k] >> (8 * i));
    for (int i = 0; i < 4; i++) p[60 + i] = (uint8_t)((uint32_t)m->step >> (8 * i));
    fwrite(p, 1, 64, out);
}

/* mission_map_tool map <cd-root> <maps.in> <map.emit> <map.frames> <op>... */
static int map_ops(int argc, char **argv) {
    const char *cd = argv[2];
    char path[1024];
    size_t exe_size = 0, maps_size = 0;
    snprintf(path, sizeof path, "%s/VER95/VSHELL.EXE", cd);
    uint8_t *exe = read_file(path, &exe_size), *maps = read_file(argv[3], &maps_size);
    char hex[65];
    if (!exe || !maps || mount(cd)) return 2;
    es2f_sha256_of(exe, exe_size, hex);
    if (strcmp(hex, "1534aa8f732bdecd826484d893ad58b8a5e6ebd4d25ab3158a2469b8f69ba16e")) return 2;
    const uint8_t *cosine = exe_at(exe, exe_size, ES2_MAP_COSINE_OFFSET, ES2_MAP_COSINE_SIZE);
    const uint8_t *atan = exe_at(exe, exe_size, ES2_MAP_ATAN_OFFSET, ES2_MAP_ATAN_SIZE);
    const uint8_t *rng = exe_at(exe, exe_size, 0x47f850u, 112);
    if (!cosine || !atan || !rng) return 2;
    /* the CD's loose DATA files */
    size_t mforms_size = 0, mat0_size = 0;
    snprintf(path, sizeof path, "%s/DATA/MFORMS.DAT", cd);
    uint8_t *mforms = read_file(path, &mforms_size);
    snprintf(path, sizeof path, "%s/DATA/MAT0.DAT", cd);
    uint8_t *mat0 = read_file(path, &mat0_size);
    if (!mforms || !mat0 || mat0_size < 12) return 2;
    /* dba\mis_icon.dba, decoded */
    static es2_bitmap icons[ES2_MAP_ICONS];
    static uint8_t icon_pixels[ES2_MAP_ICONS][64 * 64];
    es2_span icon_file = cd_file("dba\\mis_icon.dba");
    es2_dba dba;
    if (!icon_file.data || es2_dba_parse(icon_file, &dba, NULL) != ES2_OK) return 2;
    for (size_t i = 0; i < ES2_MAP_ICONS && i < dba.count; i++) {
        es2_dbm image;
        if (es2_dba_image_at(&dba, i, &image, NULL) == ES2_OK &&
            es2_draw_bitmap_from_dbm(&image, icon_pixels[i], sizeof icon_pixels[i], &icons[i]) != ES2_DRAW_DONE)
            memset(&icons[i], 0, sizeof icons[i]);
    }
    FILE *emit = fopen(argv[4], "wb"), *frames = fopen(argv[5], "wb");
    if (!emit || !frames) return 2;
    static uint8_t frame[640 * 480];
    es2_mission_map map;
    es2_mission_map_globals g;
    memset(&map, 0, sizeof map);
    memset(&g, 0, sizeof g);
    if (es2_shell_rng_init((es2_span){rng, 112}, 0, &g.material_rng, NULL) != ES2_OK) return 2;
    for (int i = 6; i < argc; i++) {
        const char *op = argv[i];
        unsigned a;
        if (sscanf(op, "map-build:%u", &a) == 1) {
            size_t at = 0;
            es2_span part[3] = {{0}};
            int found = 0;
            while (!found && maps_size - at >= 4) {
                size_t o = at + 4;
                for (int k = 0; k < 3; k++) {
                    if (maps_size - o < 4) return 3;
                    size_t n = le32(maps + o);
                    if (maps_size - o - 4 < n) return 3;
                    part[k] = (es2_span){maps + o + 4, n};
                    o += 4 + n;
                }
                found = le32(maps + at) == a;
                at = o;
            }
            if (!found) return 3;
            int zone = es2_mission_map_zone(part[0]);
            char name[64];
            snprintf(name, sizeof name, "dat\\zone%d.dat", zone);
            es2_span zdat = cd_file(name);
            snprintf(name, sizeof name, "dba\\zone%d.dba", zone);
            es2_span zdba = cd_file(name);
            es2_dba grid;
            es2_dbm cell;
            if (zdat.size < 16 || !zdba.data || es2_dba_parse(zdba, &grid, NULL) != ES2_OK ||
                es2_dba_image_at(&grid, 0, &cell, NULL) != ES2_OK)
                return 4;
            es2_mission_map_inputs in;
            memset(&in, 0, sizeof in);
            in.script = part[0];
            in.player_mec = part[1];
            in.mforms = (es2_span){mforms, mforms_size};
            in.cell_shift = (int32_t)le32(zdat.data + 8);
            in.grid_width = cell.width;
            in.grid_height = cell.height;
            in.grid_bytes = cell.pixels;
            in.material_count = le32(mat0);
            in.material_level = (int32_t)le32(mat0 + 8);
            in.mission_lines = part[2].size >= 6 && le32(part[2].data) >= 2 ? (int16_t)le16(part[2].data + 4) : 0;
            in.icons = icons;
            in.icon_count = ES2_MAP_ICONS;
            in.cosine = (es2_span){cosine, ES2_MAP_COSINE_SIZE};
            in.atan = (es2_span){atan, ES2_MAP_ATAN_SIZE};
            memset(frame, 0, sizeof frame);
            if (es2_mission_map_build(&map, &in, &g)) return 5;
            uint8_t head[36 + 114];
            memset(head, 0, sizeof head);
            hash_bytes(map.image, (size_t)map.image_width * map.image_height, head);
            head[32] = (uint8_t)map.image_width;
            head[33] = (uint8_t)(map.image_width >> 8);
            head[34] = (uint8_t)map.image_height;
            head[35] = (uint8_t)(map.image_height >> 8);
            for (int k = 0; k < 56; k++) {
                head[36 + 2 * k] = (uint8_t)g.material_rng.words[k];
                head[37 + 2 * k] = (uint8_t)(g.material_rng.words[k] >> 8);
            }
            head[36 + 112] = g.material_rng.a;
            head[36 + 113] = g.material_rng.b;
            fwrite(head, 1, sizeof head, emit);
            probe(emit, &map, &g, frame, 0xff);
            fprintf(stderr, "map %u: zone %d, image %ux%u, refused %d (grid %ux%u kind %u, %u bytes, shift %d)\n", a,
                    zone, map.image_width, map.image_height, map.refused, cell.width, cell.height, cell.compression,
                    (unsigned)cell.pixels.size, (int)in.cell_shift);
        } else if (sscanf(op, "map-step:%u", &a) == 1) {
            unsigned r = (unsigned)es2_mission_map_step(&map, &g, a, frame);
            probe(emit, &map, &g, frame, r);
        } else if (sscanf(op, "map-arrow:%u", &a) == 1 && a < 6) {
            es2_mission_map_arrow_step(&map, &g, (es2_mission_map_arrow)a, frame);
            probe(emit, &map, &g, frame, 0xff);
        } else if (!strcmp(op, "map-skip")) {
            es2_mission_map_skip(&map);
            probe(emit, &map, &g, frame, 0xff);
        } else if (!strcmp(op, "map-paint")) {
            es2_mission_map_paint(&map, &g, frame);
            probe(emit, &map, &g, frame, 0xff);
        } else if (!strcmp(op, "map-frame")) {
            fwrite(frame, 1, sizeof frame, frames);
        } else
            return 6;
    }
    fclose(emit);
    fclose(frames);
    es2_mission_map_free(&map);
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 4 && !strcmp(argv[1], "raster")) return raster(argv[2], argv[3]);
    if (argc >= 6 && !strcmp(argv[1], "map")) return map_ops(argc, argv);
    if (argc == 4 && !strcmp(argv[1], "blank")) { /* the canvases without the draw */
        blank_only = 1;
        return raster(argv[2], argv[3]);
    }
    fprintf(stderr, "usage: mission_map_tool raster|blank <map.in> <map.emit>\n");
    return 2;
}
