/* Screen cases of the modal boxes and dialogs (es2/shell/gui/boxes.h)
 * for the screen test harness. Each case builds the six box screens from the
 * layout table into a toolkit on the session's shell, shows one box the way
 * VSHELL shows it (Alt+F4, the End/warning/Replay show functions through the
 * flow, the CD prompt's shield, the DEBUG dialog with -@) and snaps the states.
 *
 * Synthetic cases (no game data): a generated font, own texts, a seeded
 * background and palette. Real cases (ES2_DATA_ROOT): dfn\font2.dfn from the
 * CD's VOL archives, eng\estext.bin, dbm\bay2a_84.dbm, dpl\palette.dpl and
 * the DEBUG dialog's literals read from VER95\VSHELL.EXE at run time; the
 * alert boxes are drawn over a cleared frame (VSHELL draws them over the
 * screen that opened them: the full frames need the graphical shell's
 * composition), the Replay box and the DEBUG dialog are full screens. No VSHELL
 * references exist yet; the reference field is NULL. */
#define WIN32_LEAN_AND_MEAN
#include "screen_harness.h"

#include "es2/formats/dbm.h"
#include "es2/formats/dpl.h"
#include "es2/formats/vfs.h"
#include "es2/formats/vol.h"
#include "es2/shell/draw.h"
#include "es2/shell/gui/boxes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#endif

typedef struct box_state {
    es2_ui ui;
    es2_boxes boxes;
    es2_shell_flow flow;
    int real;
} box_state;

/* ---- synthetic resources --------------------------------------------------- */

enum { FC = 32, FN = 95, FH = 7 };
static uint8_t synth_font_bytes[20000];
static es2_dfn synth_font;
static int synth_ready;
static size_t put16(uint8_t *p, size_t at, int v) { p[at] = (uint8_t)v; p[at + 1] = (uint8_t)((unsigned)v >> 8); return at + 2; }
static size_t put32(uint8_t *p, size_t at, uint32_t v) { put16(p, at, (int)(v & 0xffff)); return put16(p, at + 2, (int)(v >> 16)); }
static int glyph_w(int c) { return c == ' ' ? 3 : 4 + c % 2; }
static int make_synth_font(void) {
    uint8_t *b = synth_font_bytes;
    size_t at = 8, data_size = 0, data_at, off = 0;
    for (int i = 0; i < FN; i++) data_size += (size_t)glyph_w(FC + i) * FH;
    at = put16(b, at, FN); at = put16(b, at, 0); at = put16(b, at, FC); at = put16(b, at, FH);
    at = put16(b, at, -1); at = put16(b, at, FH); at = put16(b, at, 5); at = put16(b, at, 8);
    at = put16(b, at, 0); at = put16(b, at, 6); at = put16(b, at, 0); at = put32(b, at, (uint32_t)data_size);
    data_at = at;
    for (int i = 0; i < FN; i++) {
        int w = glyph_w(FC + i);
        for (int r = 0; r < FH; r++)
            for (int c = 0; c < w; c++)
                b[data_at + off + (size_t)r * (size_t)w + (size_t)c] = (uint8_t)(c < w - 1 && (r * 3 + c + i) % 5 < 2 ? 41 : 0);
        off += (size_t)w * FH;
    }
    at = data_at + data_size;
    off = 0;
    for (int i = 0; i < FN; i++) {
        at = put32(b, at, (uint32_t)off);
        off += (size_t)glyph_w(FC + i) * FH;
    }
    for (int i = 0; i < FN; i++) b[at++] = (uint8_t)glyph_w(FC + i);
    b[0] = 5; b[1] = 0; b[2] = 0x28; b[3] = 0;
    put32(b, 4, (uint32_t)(at - 8));
    if ((at - 8) & 1) b[at++] = 0;
    return es2_dfn_parse((es2_span){synth_font_bytes, at}, &synth_font, NULL) == ES2_OK ? 0 : -1;
}
static uint8_t synth_bg_pixels[ES2_GUI_PIXELS];
static es2_bitmap synth_bg = {ES2_GUI_HEIGHT, ES2_GUI_WIDTH, 0x10, synth_bg_pixels};
static char synth_texts[16][24];
static int synth_next;
static const char *synth_estext(void *ctx, int id) {
    (void)ctx;
    char *t = synth_texts[synth_next++ % 16];
    snprintf(t, sizeof synth_texts[0], "TEXT %d", id);
    return t;
}
static const char *synth_literal(void *ctx, uint32_t a) {
    (void)ctx;
    static char t[24];
    if (a == 0x47a52b) return "";
    snprintf(t, sizeof t, "LIT %X", (unsigned)(a & 0xfff));
    return t;
}
static const es2_dfn *synth_font_of(void *ctx, uint32_t g) { (void)ctx; (void)g; return &synth_font; }
static const es2_bitmap *synth_bitmap(void *ctx, uint32_t g) { (void)ctx; return g == 0x46de0c ? &synth_bg : NULL; }

/* ---- real resources (read once from ES2_DATA_ROOT) -------------------------- */

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

static struct {
    int state; /* 0 not tried, 1 ready, -1 failed */
    es2_vol vols[32];
    es2_vfs_source sources[32];
    unsigned vol_count;
    es2_vfs vfs;
    es2_dfn font;
    es2_span estext;
    uint8_t *exe;
    size_t exe_size;
    uint8_t bg_pixels[ES2_GUI_PIXELS];
    es2_bitmap bg;
    uint8_t palette[256][4];
} real;

static int add_vol(const char *root, const char *name) {
    char path[300];
    size_t size = 0;
    uint8_t *data;
    if (real.vol_count == 32) return 0;
    snprintf(path, sizeof path, "%s/VOL/%s", root, name);
    if (!(data = read_file(path, &size)) || es2_vol_open((es2_span){data, size}, &real.vols[real.vol_count], NULL) != ES2_OK)
        return 0;
    real.sources[real.vol_count].kind = ES2_VFS_VOL;
    real.sources[real.vol_count].data.vol = &real.vols[real.vol_count];
    real.vol_count++;
    return 1;
}

#ifndef _WIN32
/* FindFirstFileA's order on NTFS: names compared in upper case. */
static int upper_compare(const void *a, const void *b) {
    const unsigned char *x = *(const unsigned char *const *)a, *y = *(const unsigned char *const *)b;
    for (;; x++, y++) {
        int cx = *x >= 'a' && *x <= 'z' ? *x - 32 : *x, cy = *y >= 'a' && *y <= 'z' ? *y - 32 : *y;
        if (cx != cy || !cx) return cx - cy;
    }
}
#endif

static int mount(const char *root) {
    char path[300];
    es2_vfs_copy c;
#ifdef _WIN32
    char pattern[MAX_PATH];
    WIN32_FIND_DATAA f;
    HANDLE h;
    snprintf(pattern, sizeof pattern, "%s/VOL/*.VOL", root);
    h = FindFirstFileA(pattern, &f);
    if (h == INVALID_HANDLE_VALUE) return -1;
    do {
        if (!add_vol(root, f.cFileName)) break;
    } while (FindNextFileA(h, &f));
    FindClose(h);
#else
    char directory[300], *names[64];
    unsigned count = 0;
    struct dirent *e;
    DIR *d;
    snprintf(directory, sizeof directory, "%s/VOL", root);
    if (!(d = opendir(directory))) return -1;
    while ((e = readdir(d)) && count < 64) {
        size_t n = strlen(e->d_name);
        if (n > 4 && upper_compare(&(const char *){e->d_name + n - 4}, &(const char *){".VOL"}) == 0 &&
            (names[count] = malloc(n + 1)))
            memcpy(names[count++], e->d_name, n + 1);
    }
    closedir(d);
    if (!count) return -1;
    qsort(names, count, sizeof names[0], upper_compare);
    for (unsigned i = 0; i < count && add_vol(root, names[i]); i++) {
    }
    for (unsigned i = 0; i < count; i++) free(names[i]);
#endif
    for (unsigned i = 0; i < real.vol_count; i++) /* VSHELL's order: priority */
        for (unsigned j = i + 1; j < real.vol_count; j++)
            if (real.sources[j].data.vol->priority > real.sources[i].data.vol->priority) {
                es2_vfs_source t = real.sources[i];
                real.sources[i] = real.sources[j];
                real.sources[j] = t;
            }
    if (es2_vfs_open(real.sources, real.vol_count, &real.vfs, NULL) != ES2_OK) return -1;
    if (es2_vfs_find(&real.vfs, "dfn\\font2.dfn", &c, NULL) != ES2_OK || es2_dfn_parse(c.payload, &real.font, NULL) != ES2_OK)
        return -1;
    if (es2_vfs_find(&real.vfs, "eng\\estext.bin", &c, NULL) != ES2_OK || c.payload.size < 8) return -1;
    real.estext = c.payload;
    {
        es2_dbm dbm;
        es2_bitmap b;
        static uint8_t scratch[ES2_GUI_PIXELS];
        if (es2_vfs_find(&real.vfs, "dbm\\bay2a_84.dbm", &c, NULL) != ES2_OK || es2_dbm_parse(c.payload, &dbm, NULL) != ES2_OK ||
            es2_draw_bitmap_from_dbm(&dbm, scratch, sizeof scratch, &b) != ES2_DRAW_DONE ||
            (size_t)b.width * b.height > sizeof real.bg_pixels)
            return -1;
        memcpy(real.bg_pixels, b.pixels, (size_t)b.width * b.height);
        real.bg = b;
        real.bg.pixels = real.bg_pixels;
    }
    {
        es2_dpl dpl;
        if (es2_vfs_find(&real.vfs, es2_palette_file(ES2_PALETTE_MAIN), &c, NULL) != ES2_OK ||
            es2_dpl_parse(c.payload, &dpl, NULL) != ES2_OK)
            return -1;
        memset(real.palette, 0, sizeof real.palette);
        for (size_t i = 0; i < 256 && i < dpl.colour_count; i++) {
            es2_dpl_colour col;
            if (es2_dpl_colour_at(&dpl, i, &col, NULL) != ES2_OK) return -1;
            real.palette[i][0] = col.red;
            real.palette[i][1] = col.green;
            real.palette[i][2] = col.blue;
            real.palette[i][3] = col.flags;
        }
    }
    snprintf(path, sizeof path, "%s/VER95/VSHELL.EXE", root);
    if (!(real.exe = read_file(path, &real.exe_size))) return -1;
    return 0;
}

static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static unsigned get16(const uint8_t *p) { return (unsigned)p[0] | (unsigned)p[1] << 8; }

static const char *real_estext(void *ctx, int id) {
    const uint8_t *d = real.estext.data;
    uint32_t count = get32(d);
    size_t base, off;
    (void)ctx;
    if (id < 0 || (uint32_t)id >= count || 8 + 2 * (size_t)count > real.estext.size) return NULL;
    base = 8 + 2 * (size_t)count;
    off = base + get16(d + 8 + 2 * (size_t)id);
    if (off >= real.estext.size || !memchr(d + off, 0, real.estext.size - off)) return NULL;
    return (const char *)d + off;
}

/* A literal of VSHELL.EXE by virtual address, through its section table. */
static const char *real_literal(void *ctx, uint32_t va) {
    const uint8_t *e = real.exe;
    uint32_t pe;
    unsigned sections, optional;
    (void)ctx;
    if (real.exe_size < 0x40 || (pe = get32(e + 0x3c)) + 24 > real.exe_size) return NULL;
    sections = get16(e + pe + 6);
    optional = get16(e + pe + 20);
    for (unsigned i = 0; i < sections; i++) {
        size_t o = pe + 24 + optional + 40 * (size_t)i;
        uint32_t vsize, rva, rsize, raw;
        if (o + 40 > real.exe_size) return NULL;
        vsize = get32(e + o + 8); rva = get32(e + o + 12); rsize = get32(e + o + 16); raw = get32(e + o + 20);
        (void)vsize;
        if (va >= 0x400000 + rva && va < 0x400000 + rva + rsize) {
            size_t at = raw + (va - 0x400000 - rva);
            if (at >= real.exe_size || !memchr(e + at, 0, real.exe_size - at)) return NULL;
            return (const char *)e + at;
        }
    }
    return NULL;
}
static const es2_dfn *real_font(void *ctx, uint32_t g) { (void)ctx; (void)g; return &real.font; }
static const es2_bitmap *real_bitmap(void *ctx, uint32_t g) { (void)ctx; return g == 0x46de0c ? &real.bg : NULL; }

/* ---- hooks ------------------------------------------------------------------ */

static void h_event(void *c, const es2_gui_event *ev) { es2_ui_on_event(&((box_state *)c)->ui, ev); }
static void h_timer(void *c, es2_gui_shell *sh) { es2_ui_on_timer(&((box_state *)c)->ui, sh); }
static void h_map(void *c, es2_gui_shell *sh) { es2_boxes_pass(&((box_state *)c)->boxes, sh); }
static void h_system(void *c, es2_gui_shell *sh, es2_gui_system_action a) { es2_boxes_system(&((box_state *)c)->boxes, sh, a); }

static int open_boxes(es2_screen_session *s, box_state *st, const char *data_root, int real_data, int cheat) {
    es2_boxes_host host;
    uint8_t records[256][4];
    es2_gui_rect all = {0, 0, ES2_GUI_WIDTH - 1, ES2_GUI_HEIGHT - 1};
    memset(&host, 0, sizeof host);
    if (real_data) {
        if (!real.state) real.state = mount(data_root) ? -1 : 1;
        if (real.state < 0) return 2;
        host.font = real_font;
        host.bitmap = real_bitmap;
        host.estext = real_estext;
        host.literal = real_literal;
        memcpy(records, real.palette, sizeof records);
        memset(s->shell->frame.pixels, 0, ES2_GUI_PIXELS);
    } else {
        if (!synth_ready) {
            if (make_synth_font()) return 2;
            es2_screen_fixture_image(synth_bg_pixels, ES2_GUI_WIDTH, ES2_GUI_HEIGHT, 42);
            synth_ready = 1;
        }
        synth_next = 0;
        host.font = synth_font_of;
        host.bitmap = synth_bitmap;
        host.estext = synth_estext;
        host.literal = synth_literal;
        es2_screen_fixture_palette(records, 3);
        es2_screen_fixture_image(s->shell->frame.pixels, ES2_GUI_WIDTH, ES2_GUI_HEIGHT, 5);
    }
    st->real = real_data;
    if (es2_ui_init(&st->ui, s->shell)) return 3;
    es2_shell_flow_init(&st->flow, NULL, NULL);
    es2_boxes_init(&st->boxes, &st->ui, &st->flow, &host, cheat);
    if (es2_boxes_build(&st->boxes)) return 4;
    es2_ui_show(&st->ui, st->ui.root);
    s->shell->frame.realize_gate = 1;
    es2_gui_palette_select(&s->shell->frame, (const uint8_t(*)[4])records);
    es2_gui_invalidate_window(&s->shell->frame, all);
    s->shell->input.interactive = 1;
    s->screen.context = st;
    s->screen.handle = h_event;
    s->screen.timer = h_timer;
    s->screen.map = h_map;
    s->screen.system = h_system;
    return 0;
}

/* ---- case setups --------------------------------------------------------------- */

static int quit_setup(es2_screen_session *s, void *state, int real_data, const char *root) {
    return open_boxes(s, state, root, real_data, 0);
}
static int flow_box_setup(es2_screen_session *s, void *state, int real_data, const char *root, es2_shell_flow_box box,
                          int value) {
    box_state *st = state;
    int rc = open_boxes(s, state, root, real_data, 0);
    if (rc) return rc;
    st->flow.box = box;
    st->flow.box_value = value;
    es2_boxes_sync(&st->boxes);
    return 0;
}
static int cd_setup(es2_screen_session *s, void *state, int real_data, const char *root) {
    box_state *st = state;
    int rc = open_boxes(s, state, root, real_data, 0);
    if (rc) return rc;
    es2_ui_show(&st->ui, st->boxes.cd[0]); /* the player's show 0x41ede0; its wait is not run here */
    st->ui.gate = 0;
    return 0;
}
static int debug_setup(es2_screen_session *s, void *state, int real_data, const char *root) {
    box_state *st = state;
    int rc = open_boxes(s, state, root, real_data, 1);
    if (rc) return rc;
    es2_boxes_debug_show(&st->boxes, "C1_01");
    return 0;
}

static int synth_quit(es2_screen_session *s, void *st, const char *r) { return quit_setup(s, st, 0, r); }
static int synth_end(es2_screen_session *s, void *st, const char *r) { return flow_box_setup(s, st, 0, r, ES2_FLOW_BOX_END_OF_GAME, 0); }
static int synth_warning(es2_screen_session *s, void *st, const char *r) { return flow_box_setup(s, st, 0, r, ES2_FLOW_BOX_WARNING, 1); }
static int synth_replay(es2_screen_session *s, void *st, const char *r) { return flow_box_setup(s, st, 0, r, ES2_FLOW_BOX_REPLAY, 3); }
static int synth_cd(es2_screen_session *s, void *st, const char *r) { return cd_setup(s, st, 0, r); }
static int synth_debug(es2_screen_session *s, void *st, const char *r) { return debug_setup(s, st, 0, r); }
static int real_quit(es2_screen_session *s, void *st, const char *r) { return quit_setup(s, st, 1, r); }
static int real_end0(es2_screen_session *s, void *st, const char *r) { return flow_box_setup(s, st, 1, r, ES2_FLOW_BOX_END_OF_GAME, 0); }
static int real_end1(es2_screen_session *s, void *st, const char *r) { return flow_box_setup(s, st, 1, r, ES2_FLOW_BOX_END_OF_GAME, 1); }
static int real_end3(es2_screen_session *s, void *st, const char *r) { return flow_box_setup(s, st, 1, r, ES2_FLOW_BOX_END_OF_GAME, 3); }
static int real_warning0(es2_screen_session *s, void *st, const char *r) { return flow_box_setup(s, st, 1, r, ES2_FLOW_BOX_WARNING, 0); }
static int real_warning3(es2_screen_session *s, void *st, const char *r) { return flow_box_setup(s, st, 1, r, ES2_FLOW_BOX_WARNING, 3); }
static int real_replay3(es2_screen_session *s, void *st, const char *r) { return flow_box_setup(s, st, 1, r, ES2_FLOW_BOX_REPLAY, 3); }
static int real_replay0(es2_screen_session *s, void *st, const char *r) { return flow_box_setup(s, st, 1, r, ES2_FLOW_BOX_REPLAY, 0); }
static int real_cd(es2_screen_session *s, void *st, const char *r) { return cd_setup(s, st, 1, r); }
static int real_debug(es2_screen_session *s, void *st, const char *r) { return debug_setup(s, st, 1, r); }

/* ---- scripts and expectations ------------------------------------------------- */

static const char quit_script[] =
    "step\nsnap start\n"
    "key vk:0x73 alt\nstep 2\nsnap shown\n"
    "click quit_box.03\nstep\nsnap cancelled\n"
    "key q ctrl\nstep 2\nsnap ctrl_q\n";
static const char end_script[] = "step 2\nsnap shown\nclick end_of_game_box.05\nstep\nsnap closed\n";
static const char warning_script[] = "step 2\nsnap shown\nclick launch_warning_box.05\nstep\nsnap closed\n";
static const char shown_script[] = "step 2\nsnap shown\n";
static const char cd_script[] = "step 2\nsnap shown\nclick cd_prompt.03\nstep\nsnap continued\n";
static const char debug_script[] = "step 2\nsnap shown\nclick debug_dialog.06\ntype TRAIN1\nstep\nsnap typed\n";

/* Snap hashes recorded from the first run after checking the pictures
 * (frame = SHA-256 of the indices, table = of the 256 RGB triples); the
 * reference stays NULL until a capture tour captured VSHELL's states
 * (vshell_ui.md section 22.7 lists them). */
#define N(a) (sizeof(a) / sizeof((a)[0]))
static const es2_screen_expect x_quit[] = {
    {"start", "7afd8f333afb4e4013c0ae61be00ee11986df7587b953f7297e8b4717e702b78",
     "658be1475885634203d613c31c4386f27f4fc0941f981137d3566c5437f7d660", NULL},
    {"shown", "cbd858df3ab743c729afff0436c9bdc6f9d27edf93d154a037564edbd091c9b1",
     "658be1475885634203d613c31c4386f27f4fc0941f981137d3566c5437f7d660", NULL},
    {"cancelled", "7afd8f333afb4e4013c0ae61be00ee11986df7587b953f7297e8b4717e702b78",
     "658be1475885634203d613c31c4386f27f4fc0941f981137d3566c5437f7d660", NULL},
    {"ctrl_q", "cbd858df3ab743c729afff0436c9bdc6f9d27edf93d154a037564edbd091c9b1",
     "658be1475885634203d613c31c4386f27f4fc0941f981137d3566c5437f7d660", NULL},
};
static const es2_screen_expect x_end[] = {
    {"shown", "ab84b45e8fc45e990e2c719b7d823ce3052ad9808754f2824566b289fd636ebf",
     "658be1475885634203d613c31c4386f27f4fc0941f981137d3566c5437f7d660", NULL},
    {"closed", "7afd8f333afb4e4013c0ae61be00ee11986df7587b953f7297e8b4717e702b78",
     "658be1475885634203d613c31c4386f27f4fc0941f981137d3566c5437f7d660", NULL},
};
static const es2_screen_expect x_warning[] = {
    {"shown", "1aa8d5c02f5cbc0b98f5c561e308a32f7f8343ef084adedf52b6ae9aaaa07230",
     "658be1475885634203d613c31c4386f27f4fc0941f981137d3566c5437f7d660", NULL},
    {"closed", "7afd8f333afb4e4013c0ae61be00ee11986df7587b953f7297e8b4717e702b78",
     "658be1475885634203d613c31c4386f27f4fc0941f981137d3566c5437f7d660", NULL},
};
static const es2_screen_expect x_replay[] = {
    {"shown", "1feffd3a82ec2984f14c567389ca8583c21292b4aed0f219030fc1d815a31604",
     "658be1475885634203d613c31c4386f27f4fc0941f981137d3566c5437f7d660", NULL},
};
static const es2_screen_expect x_cd_prompt[] = {
    {"shown", "35a53cddb26b0aa16aad1c9f6b5348806b510392a04427a6de4d3897ef999848",
     "658be1475885634203d613c31c4386f27f4fc0941f981137d3566c5437f7d660", NULL},
    {"continued", "7afd8f333afb4e4013c0ae61be00ee11986df7587b953f7297e8b4717e702b78",
     "658be1475885634203d613c31c4386f27f4fc0941f981137d3566c5437f7d660", NULL},
};
static const es2_screen_expect x_debug[] = {
    {"shown", "4519ac9ff1de490ec5725ffbfa82d92080fa91326ba637badc0511c0dea1240a",
     "658be1475885634203d613c31c4386f27f4fc0941f981137d3566c5437f7d660", NULL},
    {"typed", "4c44578b0b94083db909b93fda1f61052626d5d8248ec184d7d26a26c0c68b32",
     "658be1475885634203d613c31c4386f27f4fc0941f981137d3566c5437f7d660", NULL},
};
static const es2_screen_expect x_real_quit[] = {
    {"start", "7818f5542a0404157573be6cffc0e0c8e68ce3c0f5d17d07ccdd9313fb700baf",
     "96b549df7c20ad4f6b89e13333204a31f25ffedaf43cbf4bcc330bef14f4042a", NULL},
    {"shown", "194664974a66af6e7e84833e36c405b6141827941b74ee47bda56da8536db23f",
     "96b549df7c20ad4f6b89e13333204a31f25ffedaf43cbf4bcc330bef14f4042a", NULL},
    {"cancelled", "7818f5542a0404157573be6cffc0e0c8e68ce3c0f5d17d07ccdd9313fb700baf",
     "96b549df7c20ad4f6b89e13333204a31f25ffedaf43cbf4bcc330bef14f4042a", NULL},
    {"ctrl_q", "194664974a66af6e7e84833e36c405b6141827941b74ee47bda56da8536db23f",
     "96b549df7c20ad4f6b89e13333204a31f25ffedaf43cbf4bcc330bef14f4042a", NULL},
};
static const es2_screen_expect x_real_end_war_lost[] = {
    {"shown", "a1d7c90f140701c159b97052ab044330c48dac4300150c998ac8b0dbcd2f4012",
     "96b549df7c20ad4f6b89e13333204a31f25ffedaf43cbf4bcc330bef14f4042a", NULL},
};
static const es2_screen_expect x_real_end_victory[] = {
    {"shown", "1b69f9c9aa9b3eba272e505ad8fc0f39cf7914e31373caf4fcdd49c478d34c98",
     "96b549df7c20ad4f6b89e13333204a31f25ffedaf43cbf4bcc330bef14f4042a", NULL},
};
static const es2_screen_expect x_real_end_killed[] = {
    {"shown", "282413f0d3f0903fe66160e640ada7ab2574a8a0ab4499e508fc0799a38a6f29",
     "96b549df7c20ad4f6b89e13333204a31f25ffedaf43cbf4bcc330bef14f4042a", NULL},
};
static const es2_screen_expect x_real_warning_not_functional[] = {
    {"shown", "2404006348da68d0dd14d800b33f578006ad8293f8e4cf3d4e084a60d4af3d14",
     "96b549df7c20ad4f6b89e13333204a31f25ffedaf43cbf4bcc330bef14f4042a", NULL},
};
static const es2_screen_expect x_real_warning_squad_unarmed[] = {
    {"shown", "917f49e22568ce3e6b98e53bae4fc2545cdad869890b848a377f3b29490cee3e",
     "96b549df7c20ad4f6b89e13333204a31f25ffedaf43cbf4bcc330bef14f4042a", NULL},
};
static const es2_screen_expect x_real_replay_killed[] = {
    {"shown", "a2790fd08ce43385e7ce52e626e00aa1cb4f20da3ce663b1d3241bd86c8e53df",
     "96b549df7c20ad4f6b89e13333204a31f25ffedaf43cbf4bcc330bef14f4042a", NULL},
};
static const es2_screen_expect x_real_replay_war_lost[] = {
    {"shown", "db3508cb5a99c8c275801846be0f1fed609a66beb5023350bd3cba0d67c21788",
     "96b549df7c20ad4f6b89e13333204a31f25ffedaf43cbf4bcc330bef14f4042a", NULL},
};
static const es2_screen_expect x_real_cd_prompt[] = {
    {"shown", "7453b96e9ebd86d404a413c0db089478913a09d6e704a8e79bff5b4d0d18039c",
     "96b549df7c20ad4f6b89e13333204a31f25ffedaf43cbf4bcc330bef14f4042a", NULL},
};
static const es2_screen_expect x_real_debug[] = {
    {"shown", "0d57feae0d00e4f40a4548906e3098c17f1083fdee8cccd3e59518aee70e0f6b",
     "96b549df7c20ad4f6b89e13333204a31f25ffedaf43cbf4bcc330bef14f4042a", NULL},
    {"typed", "fbe0e5e158ab18f8a88c57736843e1a73b0b4ec37296869b6f8eecb9599fca08",
     "96b549df7c20ad4f6b89e13333204a31f25ffedaf43cbf4bcc330bef14f4042a", NULL},
};

static const es2_screen_case boxes_cases[] = {
    {"boxes.quit", "quit_box", 0, sizeof(box_state), synth_quit, quit_script, x_quit, N(x_quit), NULL},
    {"boxes.end", "end_of_game_box", 0, sizeof(box_state), synth_end, end_script, x_end, N(x_end), NULL},
    {"boxes.warning", "launch_warning_box", 0, sizeof(box_state), synth_warning, warning_script, x_warning, N(x_warning), NULL},
    {"boxes.replay", "replay_box", 0, sizeof(box_state), synth_replay, shown_script, x_replay, N(x_replay), NULL},
    {"boxes.cd_prompt", "cd_prompt", 0, sizeof(box_state), synth_cd, cd_script, x_cd_prompt, N(x_cd_prompt), NULL},
    {"boxes.debug", "debug_dialog", 0, sizeof(box_state), synth_debug, debug_script, x_debug, N(x_debug), NULL},
    {"boxes.real_quit", "quit_box", 1, sizeof(box_state), real_quit, quit_script, x_real_quit, N(x_real_quit), NULL},
    {"boxes.real_end_war_lost", "end_of_game_box", 1, sizeof(box_state), real_end0, shown_script, x_real_end_war_lost, N(x_real_end_war_lost), NULL},
    {"boxes.real_end_victory", "end_of_game_box", 1, sizeof(box_state), real_end1, shown_script, x_real_end_victory, N(x_real_end_victory), NULL},
    {"boxes.real_end_killed", "end_of_game_box", 1, sizeof(box_state), real_end3, shown_script, x_real_end_killed, N(x_real_end_killed), NULL},
    {"boxes.real_warning_not_functional", "launch_warning_box", 1, sizeof(box_state), real_warning0, shown_script, x_real_warning_not_functional, N(x_real_warning_not_functional), NULL},
    {"boxes.real_warning_squad_unarmed", "launch_warning_box", 1, sizeof(box_state), real_warning3, shown_script, x_real_warning_squad_unarmed, N(x_real_warning_squad_unarmed), NULL},
    {"boxes.real_replay_killed", "replay_box", 1, sizeof(box_state), real_replay3, shown_script, x_real_replay_killed, N(x_real_replay_killed), NULL},
    {"boxes.real_replay_war_lost", "replay_box", 1, sizeof(box_state), real_replay0, shown_script, x_real_replay_war_lost, N(x_real_replay_war_lost), NULL},
    {"boxes.real_cd_prompt", "cd_prompt", 1, sizeof(box_state), real_cd, shown_script, x_real_cd_prompt, N(x_real_cd_prompt), NULL},
    {"boxes.real_debug", "debug_dialog", 1, sizeof(box_state), real_debug, debug_script, x_real_debug, N(x_real_debug), NULL},
};

ES2_SCREEN_SUITE(boxes, boxes_cases)
