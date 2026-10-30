/* Screen cases of the squad panel's Herc pictures
 * (es2/shell/gui/squad_pictures.h) on the screen test harness, with the service
 * bay and squad panel.
 *
 * Synthetic cases: main_menu_fixture.h's font, seeded images for the bay
 * background and the button members, seeded palettes, placeholder texts,
 * squad_pictures_fixture.h's files, frames and eight records. The real case
 * reads the fonts, dba\mnu_bttn.dba, dba\online.dba, dbm\bay2a_84.dbm, the
 * palettes, ENG\ESTEXT.BIN and every Herc picture file and DBA from the CD's
 * VOL archives (the eight records stay synthetic). REPAIR's own screen is not
 * built: eight stand-in ESGrids at its panels' rectangle take the internal
 * pictures. VSHELL references do not exist yet: the expectations are
 * our own hashes; vshell_ui.md section 36 lists the states a capture tour must
 * capture. */
#include "screen_harness.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "es2/formats/dbm.h"
#include "es2/formats/dpl.h"
#include "es2/formats/vfs.h"
#include "es2/formats/vol.h"
#include "es2/shell/campaign.h"
#include "es2/shell/gui/service_bay.h"
#include "es2/shell/gui/squad_pictures.h"
#include "main_menu_fixture.h"
#include "menu.h"
#include "squad_pictures_fixture.h"

typedef struct sp_state {
    es2_ui ui;
    es2_shell_flow flow;
    es2_service_bay bay;
    es2_squad_pictures pictures;
    spf_fixture fx;
    es2_ui_id rgrid[8];
    int real;
    char text[64];
} sp_state;

typedef struct sp_resources {
    int ready;
    es2_dfn font, font2;
    es2_bitmap background;
    es2_bitmap button[4];
    es2_bitmap online[2];
    uint8_t records[20][256][4];
    int has_records[20];
    es2_span estext;
} sp_resources;

static sp_resources synthetic, real;

static sp_resources *res(void *ctx) { return ((sp_state *)ctx)->real ? &real : &synthetic; }

/* ---- real resources ---------------------------------------------------------------- */
static es2_vol vols[3];
static es2_vfs_source sources[3];
static es2_vfs vfs;
static uint8_t real_pixels[640 * 480], real_buttons[4][4096], real_online[2][2048];
typedef struct sp_frame {
    char file[24];
    int frame;
    es2_bitmap bitmap;
    uint8_t *pixels;
} sp_frame;
static sp_frame frames[400];
static int frame_count;

static uint8_t *slurp(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    uint8_t *data = NULL;
    long n;
    if (!f) return NULL;
    if (!fseek(f, 0, SEEK_END) && (n = ftell(f)) > 0 && !fseek(f, 0, SEEK_SET) && (data = malloc((size_t)n))) {
        if (fread(data, 1, (size_t)n, f) != (size_t)n) {
            free(data);
            data = NULL;
        } else
            *size = (size_t)n;
    }
    fclose(f);
    return data;
}
static int member(const char *file, size_t index, uint8_t *scratch, size_t size, es2_bitmap *out) {
    es2_vfs_copy c;
    es2_dba dba;
    es2_dbm dbm;
    return es2_vfs_find(&vfs, file, &c, NULL) == ES2_OK && es2_dba_parse(c.payload, &dba, NULL) == ES2_OK &&
           es2_dba_image_at(&dba, index, &dbm, NULL) == ES2_OK &&
           es2_draw_bitmap_from_dbm(&dbm, scratch, size, out) == ES2_DRAW_DONE;
}
/* any DBA frame, decoded once */
static const es2_bitmap *real_frame(const char *file, int n) {
    es2_vfs_copy c;
    es2_dba dba;
    es2_dbm dbm;
    sp_frame *f;
    if (n < 0) return NULL;
    for (int i = 0; i < frame_count; i++)
        if (frames[i].frame == n && !strcmp(frames[i].file, file)) return &frames[i].bitmap;
    if (frame_count == (int)(sizeof frames / sizeof frames[0])) return NULL;
    if (es2_vfs_find(&vfs, file, &c, NULL) != ES2_OK || es2_dba_parse(c.payload, &dba, NULL) != ES2_OK ||
        es2_dba_image_at(&dba, (size_t)n, &dbm, NULL) != ES2_OK)
        return NULL;
    f = &frames[frame_count];
    size_t size = (size_t)dbm.width * dbm.height;
    if (!(f->pixels = malloc(size ? size : 1))) return NULL;
    if (es2_draw_bitmap_from_dbm(&dbm, f->pixels, size ? size : 1, &f->bitmap) != ES2_DRAW_DONE) {
        free(f->pixels);
        return NULL;
    }
    snprintf(f->file, sizeof f->file, "%s", file);
    f->frame = n;
    frame_count++;
    return &f->bitmap;
}
static int real_load(const char *root) {
    static const char *const names[3] = {"SHELL0.VOL", "SHELL1.VOL", "LANG0.VOL"};
    es2_vfs_copy c;
    es2_dbm dbm;
    if (real.ready) return real.ready < 0 ? 2 : 0;
    real.ready = -1;
    for (int i = 0; i < 3; i++) {
        char path[1024];
        size_t size = 0;
        uint8_t *data;
        snprintf(path, sizeof path, "%s/VOL/%s", root, names[i]);
        if (!(data = slurp(path, &size)) || es2_vol_open((es2_span){data, size}, &vols[i], NULL) != ES2_OK) return 2;
        sources[i].kind = ES2_VFS_VOL;
        sources[i].data.vol = &vols[i];
    }
    if (es2_vfs_open(sources, 3, &vfs, NULL) != ES2_OK) return 3;
    if (es2_vfs_find(&vfs, "dfn\\black.dfn", &c, NULL) != ES2_OK || es2_dfn_parse(c.payload, &real.font, NULL) != ES2_OK)
        return 4;
    if (es2_vfs_find(&vfs, "dfn\\font2.dfn", &c, NULL) != ES2_OK || es2_dfn_parse(c.payload, &real.font2, NULL) != ES2_OK)
        return 4;
    if (es2_vfs_find(&vfs, "dbm\\bay2a_84.dbm", &c, NULL) != ES2_OK || es2_dbm_parse(c.payload, &dbm, NULL) != ES2_OK ||
        es2_draw_bitmap_from_dbm(&dbm, real_pixels, sizeof real_pixels, &real.background) != ES2_DRAW_DONE)
        return 5;
    for (int k = 1; k <= 3; k++)
        if (!member("dba\\mnu_bttn.dba", (size_t)k, real_buttons[k], sizeof real_buttons[k], &real.button[k])) return 6;
    for (int k = 0; k < 2; k++)
        if (!member("dba\\online.dba", (size_t)k, real_online[k], sizeof real_online[k], &real.online[k])) return 7;
    for (int p = 1; p <= 2; p++) {
        es2_dpl dpl;
        if (es2_vfs_find(&vfs, es2_palette_file(p), &c, NULL) != ES2_OK || es2_dpl_parse(c.payload, &dpl, NULL) != ES2_OK)
            return 8;
        for (size_t i = 0; i < 256 && i < dpl.colour_count; i++) {
            es2_dpl_colour col;
            if (es2_dpl_colour_at(&dpl, i, &col, NULL) != ES2_OK) return 8;
            real.records[p][i][0] = col.red;
            real.records[p][i][1] = col.green;
            real.records[p][i][2] = col.blue;
            real.records[p][i][3] = col.flags;
        }
        real.has_records[p] = 1;
    }
    if (es2_vfs_find(&vfs, "ENG/ESTEXT.BIN", &c, NULL) != ES2_OK) return 9;
    real.estext = c.payload;
    real.ready = 1;
    return 0;
}

static uint8_t synthetic_font_bytes[8 + 26 + MMF_COUNT * (MMF_W * MMF_H + 4) + 2];
static uint8_t synthetic_pixels[640 * 480], synthetic_buttons[4][75 * 24], synthetic_online[2][17 * 24];
static int synthetic_load(void) {
    if (synthetic.ready) return 0;
    if (mmf_font(synthetic_font_bytes, &synthetic.font)) return 1;
    synthetic.font2 = synthetic.font;
    es2_screen_fixture_image(synthetic_pixels, 640, 480, 47);
    synthetic.background = (es2_bitmap){480, 640, 0, synthetic_pixels};
    for (int k = 1; k <= 3; k++) {
        es2_screen_fixture_image(synthetic_buttons[k], 75, 24, 470u + (unsigned)k);
        synthetic.button[k] = (es2_bitmap){24, 75, 0, synthetic_buttons[k]};
    }
    for (int k = 0; k < 2; k++) {
        es2_screen_fixture_image(synthetic_online[k], 17, 24, 480u + (unsigned)k);
        synthetic.online[k] = (es2_bitmap){24, 17, 0, synthetic_online[k]};
    }
    for (int p = 0; p < 20; p++) {
        es2_screen_fixture_palette(synthetic.records[p], 90u + (unsigned)p);
        synthetic.has_records[p] = 1;
    }
    synthetic.ready = 1;
    return 0;
}

/* ---- hosts ---------------------------------------------------------------------------- */
static const es2_dfn *h_font(void *ctx, uint32_t global) {
    return global == 0x46de04 ? &res(ctx)->font : global == 0x46de00 ? &res(ctx)->font2 : NULL;
}
static const es2_bitmap *h_bitmap(void *ctx, const char *file, int frame) {
    sp_resources *r = res(ctx);
    if (frame < 0) return &r->background;
    if (!strcmp(file, "dba\\mnu_bttn.dba") && frame >= 1 && frame <= 3) return &r->button[frame];
    if (!strcmp(file, "dba\\online.dba") && frame >= 0 && frame <= 1) return &r->online[frame];
    return NULL;
}
static const char *h_text(void *ctx, uint32_t id) {
    sp_state *st = ctx;
    if (!st->real) {
        snprintf(st->text, sizeof st->text, "T%02X", (unsigned)id);
        return st->text;
    }
    es2_span t;
    st->text[0] = 0;
    if (es2m_bin_raw(real.estext, id, &t, NULL) == ES2_OK) {
        size_t n = t.size < sizeof st->text - 1 ? t.size : sizeof st->text - 1;
        memcpy(st->text, t.data, n);
        st->text[n] = 0;
    }
    return st->text;
}
/* the labels' view of the fixture's records (condition 0x4121f8) */
static int h_bay(void *ctx, int k, es2_squad_bay *out) {
    sp_state *st = ctx;
    static const char *const herc[8] = {"OUTLAW", "RAPTOR", "TOMAHAWK", "", "RAZOR", "APOC", "OGRE", "MAVERICK"};
    const es2_squad_record *d = &st->fx.bays[k];
    if (!st->fx.has[k]) return 0;
    out->record = 1;
    out->vehicle = d->vehicle;
    out->finished = d->progress == 100;
    out->progress = d->progress;
    out->herc = herc[k];
    if (es2_shell_overall_condition(d->words, d->capacity, &out->condition, NULL) != ES2_OK) out->condition = 100;
    out->pilot = k == 0 ? "ACE" : NULL;
    return 1;
}
static void h_panel(void *ctx, int word, int load) { es2_squad_pictures_panel(&((sp_state *)ctx)->bay, word, load); }
static es2_ui_id h_repair_grid(void *ctx, int16_t k) { return k >= 0 && k < 8 ? ((sp_state *)ctx)->rgrid[k] : 0; }
/* the editors' parts the pictures need: WEAPONS' bay change (0x43dfc8) and
 * REPAIR's diagram after its selection (0x433ad5 -> 0x414cbe) */
static void h_selected(void *ctx, int word, int16_t k, int assign) {
    sp_state *st = ctx;
    (void)k;
    (void)assign;
    if (word == 2) es2_squad_pictures_marker_off(&st->bay);
    if (word == 3) es2_squad_pictures_diagram(&st->bay);
}
static int16_t real_item_bays(int k, int s);
static int h_record(void *ctx, int k, es2_squad_record *out) {
    sp_state *st = ctx;
    if (!spf_record(&st->fx, k, out)) return 0;
    if (st->real)
        for (int s = 0; s < 10; s++) out->item[s] = real_item_bays(k, s);
    return 1;
}
static es2_span h_file(void *ctx, const char *name) {
    sp_state *st = ctx;
    es2_vfs_copy c;
    if (!st->real) return spf_file(&st->fx, name);
    return es2_vfs_find(&vfs, name, &c, NULL) == ES2_OK ? c.payload : (es2_span){NULL, 0};
}
static const es2_bitmap *h_frame(void *ctx, const char *file, int n) {
    sp_state *st = ctx;
    return st->real ? real_frame(file, n) : spf_frame(&st->fx, file, n);
}
static void palette(es2_ui *ui, uint16_t word) {
    sp_resources *r = res(ui->context);
    if (word < 20 && r->has_records[word]) es2_gui_palette_select(&ui->shell->frame, (const uint8_t(*)[4])r->records[word]);
    es2_gui_palette_apply(&ui->shell->frame);
}
static int f_rebalance(void *ctx, uint8_t b) { (void)ctx; (void)b; return 1; }

/* Items of the real case's records, on slots the CD's lists name: the
 * Outlaw's item 1 on slots 0..2, the Raptor's item 8, the Razor's item 1 and
 * the Ogre's item 1 on slots 0 and 1. */
static int16_t real_item_bays(int k, int s) {
    switch (k) {
    case 0: return (int16_t)(s < 3 ? 1 : 0);
    case 1: return (int16_t)(s < 2 ? 8 : 0);
    case 4: return (int16_t)(s < 2 ? 1 : 0);
    case 6: return (int16_t)(s < 2 ? 1 : 0);
    default: return 0;
    }
}

/* ---- the presenter ----------------------------------------------------------------------- */
static void present(void *context, const es2_gui_event *ev) { es2_ui_on_event(&((sp_state *)context)->ui, ev); }
static void timer(void *context, es2_gui_shell *shell) { es2_ui_on_timer(&((sp_state *)context)->ui, shell); }
static void movie(void *context, es2_gui_shell *shell) {
    (void)shell;
    ((sp_state *)context)->bay.loop_started = 1; /* 0x4020b8 */
}

static int open_bay(es2_screen_session *s, sp_state *st) {
    es2_shell_flow_host fh;
    es2_service_bay_host *h = &st->bay.host;
    static const int16_t limits[5] = {89, 79, 59, 29, 0};
    static const int16_t colours[6] = {41, 42, 43, 44, 45, 39};
    memset(&fh, 0, sizeof fh);
    fh.ctx = st;
    fh.rebalance = f_rebalance;
    es2_shell_flow_init(&st->flow, &fh, NULL);
    st->flow.mode = 1;
    st->flow.game = ES2_FLOW_GAME_CAMPAIGN;
    st->flow.final_word = 2;
    st->flow.screen = ES2_FLOW_SERVICE_BAY;
    if (es2_ui_init(&st->ui, s->shell)) return 2;
    st->ui.context = st;
    st->ui.palette = palette;
    st->bay.flow = &st->flow;
    h->ctx = st;
    h->font = h_font;
    h->bitmap = h_bitmap;
    h->text = h_text;
    h->bay = h_bay;
    h->panel = h_panel;
    h->selected = h_selected;
    h->repair_grid = h_repair_grid;
    memcpy(st->bay.band_limit, limits, sizeof limits);
    memcpy(st->bay.band_colour, colours, sizeof colours);
    if (es2_screen_service_bay_build(&st->ui, &st->bay) || es2_screen_squad_panel_build(&st->ui, &st->bay)) return 3;
    for (int k = 0; k < 8; k++)
        if (!(st->rgrid[k] = es2_ui_grid(&st->ui, st->ui.root, (es2_gui_rect){16, 47, 224, 303}, NULL, 0x22))) return 4;
    spf_init(&st->fx);
    spf_host(&st->fx, &st->pictures);
    st->pictures.host.ctx = st;
    st->pictures.host.file = h_file;
    st->pictures.host.frame = h_frame;
    st->pictures.host.record = h_record;
    if (st->real) { /* the executable's 0x46ffc0 (vshell_ui.md 36.5): the internal diagram's sources */
        static const int16_t from[9] = {29, 28, 24, 25, 23, 30, 22, 31, 27};
        static const int16_t groups[24] = {0, 1, 2, -1, 3, 4, 5, -1, 6, 7, -1, 0, 8, 9, -1, 0, 10, 11, -1, 0, 12, -1, 0, 0};
        memcpy(st->pictures.internal_from, from, sizeof from);
        memcpy(st->pictures.groups, groups, sizeof groups); /* synthetic part lists */
    }
    if (es2_squad_pictures_load(&st->pictures)) return 5;
    st->bay.pictures = &st->pictures;
    s->shell->frame.realize_gate = 1;
    palette(&st->ui, ES2_PALETTE_MAIN);
    s->shell->input.interactive = 1;
    s->screen.context = st;
    s->screen.handle = present;
    s->screen.timer = timer;
    s->screen.movie = movie;
    es2_ui_show(&st->ui, st->ui.root);
    es2_service_bay_show(&st->bay);
    return 0;
}
static int setup(es2_screen_session *s, void *state, const char *data_root) {
    (void)data_root;
    if (synthetic_load()) return 1;
    return open_bay(s, state);
}
static int setup_real(es2_screen_session *s, void *state, const char *data_root) {
    sp_state *st = state;
    int r;
    if (!data_root) return 1;
    if ((r = real_load(data_root))) return 10 + r;
    st->real = 1;
    return open_bay(s, state);
}

/* WEAPONS with the marker on slot 2, then on the empty slot 1 */
static int setup_marker(es2_screen_session *s, void *state, const char *data_root) {
    sp_state *st = state;
    int r = setup(s, state, data_root);
    if (r) return r;
    if (es2_screen_run(s, "step\nclick service_bay.09\n")) return 20;
    es2_squad_pictures_marker(&st->bay, 2);
    if (es2_screen_run(s, "step\nsnap slot2\n")) return 21;
    es2_squad_pictures_marker(&st->bay, 1);
    return 0;
}
/* REPAIR's internal tab: the squad panel's grid swapped for REPAIR's
 * (0x433fcd), tab 1, the diagram */
static int setup_internal(es2_screen_session *s, void *state, const char *data_root) {
    sp_state *st = state;
    int r = setup(s, state, data_root);
    if (r) return r;
    if (es2_screen_run(s, "step\nclick service_bay.11\n")) return 20;
    if (st->bay.session.screen != 3) return 21;
    es2_ui_hide(&st->ui, st->bay.squad[ES2_SQUAD_GRID + st->bay.session.bay]);
    es2_ui_show(&st->ui, st->rgrid[st->bay.session.bay]);
    st->bay.session.tab = 1;
    es2_squad_pictures_diagram(&st->bay);
    return 0;
}
static int setup_real_repair(es2_screen_session *s, void *state, const char *data_root) {
    sp_state *st = state;
    int r = setup_real(s, state, data_root);
    if (r) return r;
    if (es2_screen_run(s, "step\nclick service_bay.11\n") || st->bay.session.screen != 3) return 20;
    if (st->pictures.data_errors || st->pictures.refused) return 21;
    return 0;
}

/* ---- cases -------------------------------------------------------------------------------------- */
static const es2_screen_expect weapons_expect[] = {
    {"weapons", "5723a6885167e1648bad7188ea11b29590bc86549056f568800f10a5c7dc619b",
     "161437ec32150920c32dc15f5c19c80a71df994ef60526ab45c55b58e2a58c18", NULL},
    {"bay4", "4513fabe6837c1275f6014bf3c089b83c35fb19896bf8db3fb05580d0bf44226",
     "161437ec32150920c32dc15f5c19c80a71df994ef60526ab45c55b58e2a58c18", NULL},
    {"build", "9bb78f21bdb31c62d8e40c4eab7ceacfa2e8f503def2f9044088f4166324cd22",
     "161437ec32150920c32dc15f5c19c80a71df994ef60526ab45c55b58e2a58c18", NULL},
    {"bay2", "0653a7279d38787855fa7c6e1f9267a09d43c912372a596b6cf723502d3b3ff2",
     "161437ec32150920c32dc15f5c19c80a71df994ef60526ab45c55b58e2a58c18", NULL},
    {"crew", "fd6585e646ecebd32a0ecddda4048303912941774f2566e01b5f7ad9fa452663",
     "161437ec32150920c32dc15f5c19c80a71df994ef60526ab45c55b58e2a58c18", NULL},
};
static const es2_screen_expect repair_expect[] = {
    {"repair", "360e5c38c754d17bbf435f220367c5fb58e4a87eebf92cd5e1daf371faf4423e",
     "a0df143b6a6a5ad42a93c54bda883e05fdb94221319a0ca4dfcdf7b74c0f591c", NULL},
    {"bay4", "ad2ca78dc3845a44f1d67138f4992970381e41850398a0a6616ea22699e250a9",
     "a0df143b6a6a5ad42a93c54bda883e05fdb94221319a0ca4dfcdf7b74c0f591c", NULL},
    {"weapons", "9004a84199f3868a12e4a9d6861222b2a67034f2cba6e18b745cf4ea8b1944df",
     "161437ec32150920c32dc15f5c19c80a71df994ef60526ab45c55b58e2a58c18", NULL},
};
static const es2_screen_expect marker_expect[] = {
    {"slot2", "947436ab72c1ae339162beb56436df01a4213c6ddfbb93951939213b42174106",
     "161437ec32150920c32dc15f5c19c80a71df994ef60526ab45c55b58e2a58c18", NULL},
    {"slot1", "89e68d9da077d805e84381d2e1380f88b0f3af6a26203af83b27bfffe6ff1ffb",
     "161437ec32150920c32dc15f5c19c80a71df994ef60526ab45c55b58e2a58c18", NULL},
};
static const es2_screen_expect internal_expect[] = {{"internal", "f4d298910e1bd5a5c39cbc7ed6ef6a0242bdf40836a8748c48bc25f5160308f6",
     "a0df143b6a6a5ad42a93c54bda883e05fdb94221319a0ca4dfcdf7b74c0f591c", NULL}};
static const es2_screen_expect real_expect[] = {
    {"weapons", "c02c03e90d667b301504648032e91c955d88c426eb1d2b400e2008e5d06720ce",
     "744dc2f654b97b0e0cd30247585d27d7f7efaf2964fc3b14c04e873d8a4d9587", NULL},
    {"bay6", "4a50a99dba9b8bc5c7c9bc5252f54ef408e20648ed5b4d4697302c2ba04c536d",
     "744dc2f654b97b0e0cd30247585d27d7f7efaf2964fc3b14c04e873d8a4d9587", NULL},
    {"build", "6212d12b92c9b86f39eebddf7011004451a57d821c3481e065fb511396e1a57d",
     "744dc2f654b97b0e0cd30247585d27d7f7efaf2964fc3b14c04e873d8a4d9587", NULL},
    {"bay2", "912b4038c305171d7735ec346376f77e5ee05143d24e62f6eecd0ae79cea7618",
     "744dc2f654b97b0e0cd30247585d27d7f7efaf2964fc3b14c04e873d8a4d9587", NULL},
};
static const es2_screen_expect real_repair_expect[] = {
    {"repair", "2b36f83271ac80ccc72684fad212eb530cbe874094b4ee09452fc98aef75109c",
     "96b549df7c20ad4f6b89e13333204a31f25ffedaf43cbf4bcc330bef14f4042a", NULL},
    {"bay4", "fd8ba46d6ebf4c01a5ff17d38d7e0cfef508b4aaca038ff35b62c7372b25e7f2",
     "96b549df7c20ad4f6b89e13333204a31f25ffedaf43cbf4bcc330bef14f4042a", NULL},
};

#define N(a) (sizeof(a) / sizeof((a)[0]))
static const es2_screen_case cases[] = {
    {"squad_pictures.weapons", "squad_panel", 0, sizeof(sp_state), setup,
     "step\n"
     "click service_bay.09\n"
     "snap weapons\n"
     "click squad_panel.37\n"
     "snap bay4\n"
     "click service_bay.13\n"
     "snap build\n"
     "click squad_panel.27\n"
     "snap bay2\n"
     "click service_bay.17\n"
     "snap crew\n",
     weapons_expect, N(weapons_expect), NULL},
    {"squad_pictures.repair", "squad_panel", 0, sizeof(sp_state), setup,
     "step\n"
     "click service_bay.11\n"
     "snap repair\n"
     "click squad_panel.37\n"
     "snap bay4\n"
     "click service_bay.09\n"
     "snap weapons\n",
     repair_expect, N(repair_expect), NULL},
    {"squad_pictures.marker", "squad_panel", 0, sizeof(sp_state), setup_marker, "step\nsnap slot1\n", marker_expect,
     N(marker_expect), NULL},
    {"squad_pictures.internal", "squad_panel", 0, sizeof(sp_state), setup_internal, "step\nsnap internal\n",
     internal_expect, N(internal_expect), NULL},
    {"squad_pictures.real", "squad_panel", 1, sizeof(sp_state), setup_real,
     "step\n"
     "click service_bay.09\n"
     "snap weapons\n"
     "click squad_panel.47\n"
     "snap bay6\n"
     "click service_bay.13\n"
     "snap build\n"
     "click squad_panel.27\n"
     "snap bay2\n",
     real_expect, N(real_expect), NULL},
    {"squad_pictures.real_repair", "squad_panel", 1, sizeof(sp_state), setup_real_repair,
     "step\n"
     "snap repair\n"
     "click squad_panel.37\n"
     "snap bay4\n",
     real_repair_expect, N(real_repair_expect), NULL},
};

ES2_SCREEN_SUITE(squad_pictures, cases)
