/* Screen cases of the CREW screen (es2/shell/gui/crew.h) with the
 * service bay and the squad panel on the screen test harness.
 *
 * The game state is crew_fixture.h's synthetic SAV, changed through the editors
 * library by the host. Synthetic cases: main_menu_fixture.h's font, seeded
 * images for the bay background, the bar buttons, the pilot portraits and
 * the star, a seeded palette per palette index, placeholder texts ("TAC"
 * for ESTEXT 0xac, ...). The real case reads dfn\black.dfn, dfn\font2.dfn,
 * dba\mnu_bttn.dba, dba\online.dba, dba\c_pilots.dba, dba\star.dba,
 * dbm\bay2a_84.dbm, dpl\palette.dpl, dpl\arming.dpl and ENG\ESTEXT.BIN from
 * the CD's VOL archives (the team stays synthetic). VSHELL references do
 * not exist yet: the expectations are our own hashes; vshell_ui.md
 * section 32 lists the states a capture tour must capture. */
#include "screen_harness.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "crew_fixture.h"
#include "es2/formats/dbm.h"
#include "es2/formats/dpl.h"
#include "es2/formats/vfs.h"
#include "es2/formats/vol.h"
#include "es2/shell/gui/crew.h"
#include "es2/shell/gui/service_bay.h"
#include "main_menu_fixture.h"
#include "menu.h"

#define CREW_PORTRAITS 16

typedef struct cr_state {
    es2_ui ui;
    es2_shell_flow flow;
    es2_service_bay bay;
    es2_crew crew;
    crew_world world;
    int real;
    char text[64];
} cr_state;

typedef struct cr_resources {
    int ready;
    es2_dfn font, font2;
    es2_bitmap background, button[4], online[2], portrait[CREW_PORTRAITS], star;
    int has_portrait[CREW_PORTRAITS];
    uint8_t records[20][256][4];
    int has_records[20];
    es2_span estext;
} cr_resources;

static cr_resources synthetic, real;

/* ---- host ------------------------------------------------------------------------- */
static cr_resources *res(void *ctx) { return ((cr_state *)ctx)->real ? &real : &synthetic; }
static cr_state *state_of(void *world) { return (cr_state *)((char *)world - offsetof(cr_state, world)); }

static const es2_dfn *h_font(void *ctx, uint32_t global) {
    cr_resources *r = res(state_of(ctx));
    return global == 0x46de04 ? &r->font : global == 0x46de00 || global == 0x46ddfc ? &r->font2 : NULL;
}
static const es2_bitmap *h_bitmap(void *ctx, const char *file, int frame) {
    cr_resources *r = res(state_of(ctx));
    if (frame < 0) return &r->background;
    if (!strcmp(file, "dba\\mnu_bttn.dba") && frame >= 1 && frame <= 3) return &r->button[frame];
    if (!strcmp(file, "dba\\online.dba") && frame >= 0 && frame <= 1) return &r->online[frame];
    if (!strcmp(file, "dba\\c_pilots.dba") && frame >= 0 && frame < CREW_PORTRAITS && r->has_portrait[frame])
        return &r->portrait[frame];
    if (!strcmp(file, "dba\\star.dba") && frame == 0) return &r->star;
    return NULL;
}
static const char *h_text(void *ctx, uint32_t id) {
    cr_state *st = state_of(ctx);
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
static void h_screen(void *ctx, int word, int show) {
    cr_state *st = state_of(ctx);
    if (word != ES2_SHELL_SCREEN_CREW) return;
    if (show) es2_crew_show(&st->crew);
    else es2_crew_leave(&st->crew);
}
static void h_selected(void *ctx, int word, int16_t k, int assign) {
    if (word == ES2_SHELL_SCREEN_CREW && assign) es2_crew_selected(&state_of(ctx)->crew, k);
}
static void palette(es2_ui *ui, uint16_t word) {
    cr_resources *r = res(ui->context);
    if (word < 20 && r->has_records[word]) es2_gui_palette_select(&ui->shell->frame, (const uint8_t(*)[4])r->records[word]);
    es2_gui_palette_apply(&ui->shell->frame);
}
static int f_rebalance(void *ctx, uint8_t b) { (void)ctx; (void)b; return 1; }

/* ---- resources ------------------------------------------------------------------------ */
static uint8_t synthetic_font_bytes[8 + 26 + MMF_COUNT * (MMF_W * MMF_H + 4) + 2];
static uint8_t synthetic_pixels[640 * 480], synthetic_buttons[4][75 * 24], synthetic_online[2][17 * 24];
static uint8_t synthetic_portraits[CREW_PORTRAITS][56 * 52], synthetic_star[56 * 52];
static int synthetic_load(void) {
    if (synthetic.ready) return 0;
    if (mmf_font(synthetic_font_bytes, &synthetic.font)) return 1;
    synthetic.font2 = synthetic.font;
    es2_screen_fixture_image(synthetic_pixels, 640, 480, 34);
    synthetic.background = (es2_bitmap){480, 640, 0, synthetic_pixels};
    for (int k = 1; k <= 3; k++) {
        es2_screen_fixture_image(synthetic_buttons[k], 75, 24, 340u + (unsigned)k);
        synthetic.button[k] = (es2_bitmap){24, 75, 0, synthetic_buttons[k]};
    }
    for (int k = 0; k < 2; k++) {
        es2_screen_fixture_image(synthetic_online[k], 17, 24, 350u + (unsigned)k);
        synthetic.online[k] = (es2_bitmap){24, 17, 0, synthetic_online[k]};
    }
    for (int k = 0; k < CREW_PORTRAITS; k++) {
        es2_screen_fixture_image(synthetic_portraits[k], 56, 52, 380u + (unsigned)k);
        synthetic.portrait[k] = (es2_bitmap){52, 56, 0, synthetic_portraits[k]};
        synthetic.has_portrait[k] = 1;
    }
    es2_screen_fixture_image(synthetic_star, 56, 52, 399);
    synthetic.star = (es2_bitmap){52, 56, 0, synthetic_star};
    for (int p = 0; p < 20; p++) {
        es2_screen_fixture_palette(synthetic.records[p], 60u + (unsigned)p);
        synthetic.has_records[p] = 1;
    }
    synthetic.ready = 1;
    return 0;
}

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
static es2_vol vols[3];
static es2_vfs_source sources[3];
static es2_vfs vfs;
static uint8_t real_pixels[640 * 480], real_buttons[4][4096], real_online[2][2048];
static uint8_t real_portraits[CREW_PORTRAITS][8192], real_star[8192];
static int member(const char *file, size_t index, uint8_t *scratch, size_t size, es2_bitmap *out) {
    es2_vfs_copy c;
    es2_dba dba;
    es2_dbm dbm;
    return es2_vfs_find(&vfs, file, &c, NULL) == ES2_OK && es2_dba_parse(c.payload, &dba, NULL) == ES2_OK &&
           es2_dba_image_at(&dba, index, &dbm, NULL) == ES2_OK &&
           es2_draw_bitmap_from_dbm(&dbm, scratch, size, out) == ES2_DRAW_DONE;
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
    for (int k = 0; k < CREW_PORTRAITS; k++)
        real.has_portrait[k] =
            member("dba\\c_pilots.dba", (size_t)k, real_portraits[k], sizeof real_portraits[k], &real.portrait[k]);
    if (!real.has_portrait[0] || !real.has_portrait[11]) return 8;
    if (!member("dba\\star.dba", 0, real_star, sizeof real_star, &real.star)) return 9;
    for (int p = 1; p <= 2; p++) {
        es2_dpl dpl;
        if (es2_vfs_find(&vfs, es2_palette_file(p), &c, NULL) != ES2_OK || es2_dpl_parse(c.payload, &dpl, NULL) != ES2_OK)
            return 10;
        for (size_t i = 0; i < 256 && i < dpl.colour_count; i++) {
            es2_dpl_colour col;
            if (es2_dpl_colour_at(&dpl, i, &col, NULL) != ES2_OK) return 10;
            real.records[p][i][0] = col.red;
            real.records[p][i][1] = col.green;
            real.records[p][i][2] = col.blue;
            real.records[p][i][3] = col.flags;
        }
        real.has_records[p] = 1;
    }
    if (es2_vfs_find(&vfs, "ENG/ESTEXT.BIN", &c, NULL) != ES2_OK) return 11;
    real.estext = c.payload;
    real.ready = 1;
    return 0;
}

/* ---- the presenter --------------------------------------------------------------------- */
static void present(void *context, const es2_gui_event *ev) { es2_ui_on_event(&((cr_state *)context)->ui, ev); }
static void timer(void *context, es2_gui_shell *shell) { es2_ui_on_timer(&((cr_state *)context)->ui, shell); }
static void movie(void *context, es2_gui_shell *shell) {
    (void)shell;
    ((cr_state *)context)->bay.loop_started = 1; /* 0x4020b8 */
}

/* The bay as a new career leaves it (mode 1, word -1), CREW built. */
static int open_bay(es2_screen_session *s, cr_state *st) {
    es2_shell_flow_host fh;
    es2_service_bay_host *h = &st->bay.host;
    static const int16_t limits[5] = {89, 79, 59, 29, 0};
    static const int16_t colours[6] = {41, 42, 43, 44, 45, 39};
    cw_init(&st->world);
    if (cw_open(&st->world)) return 4;
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
    h->ctx = &st->world;
    h->font = h_font;
    h->bitmap = h_bitmap;
    h->text = h_text;
    h->screen = h_screen;
    h->bay = cw_bay_view;
    h->selected = h_selected;
    memcpy(st->bay.band_limit, limits, sizeof limits);
    memcpy(st->bay.band_colour, colours, sizeof colours);
    if (es2_screen_service_bay_build(&st->ui, &st->bay) || es2_screen_squad_panel_build(&st->ui, &st->bay)) return 3;
    st->crew.bay = &st->bay;
    st->crew.host = (es2_crew_host){&st->world, h_font, h_bitmap, h_text, cw_team, cw_edit, NULL};
    if (es2_screen_crew_build(&st->ui, &st->crew)) return 5;
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
    cr_state *st = state;
    int r;
    if (!data_root) return 1;
    if ((r = real_load(data_root))) return 10 + r;
    st->real = 1;
    return open_bay(s, state);
}

/* ---- cases -------------------------------------------------------------------------------- */
/* crew.15 / crew.12 are the order rows' images (their centres; the rows'
 * own centres hold labels), squad_panel.17 is bay 0's selector */
static const char tour[] =
    "step\n"
    "click service_bay.17\n"
    "snap crew\n"
    "click crew.15\n"
    "snap order3\n"
    "click crew.04\n"
    "snap pilot\n"
    "click squad_panel.17\n"
    "snap assigned\n"
    "click crew.05\n"
    "snap cleared\n"
    "click crew.12\n"
    "snap player\n"
    "click service_bay.09\n"
    "snap left\n";

static const es2_screen_expect tour_expect[] = {
    {"crew", "a4c0ac6617871a63bf50874d77da50ded84b939c805ccf3cc3a9db39130278f5",
     "f7d8e3d4cd197d576a2fd1b6c74ac7b93f8cd21ced1397208b16941973620cb0", NULL},
    {"order3", "f09cc424df4ffbc1221af43e862b014620719b039ae5cdbe7be50bc3889fec6e",
     "f7d8e3d4cd197d576a2fd1b6c74ac7b93f8cd21ced1397208b16941973620cb0", NULL},
    {"pilot", "472cf64d891e78adf00c907e63014323b03392fb031b9078c5ed50edafab5d33",
     "f7d8e3d4cd197d576a2fd1b6c74ac7b93f8cd21ced1397208b16941973620cb0", NULL},
    {"assigned", "cbce2d966e948e7c97363147d401e1c4abd611846c20aa9eedc3f3ebe285dcef",
     "f7d8e3d4cd197d576a2fd1b6c74ac7b93f8cd21ced1397208b16941973620cb0", NULL},
    {"cleared", "b4a2cfcc1135ba96c02996fc5fbbde0b48afe281ce8f5ea57cd756bfb9398394",
     "f7d8e3d4cd197d576a2fd1b6c74ac7b93f8cd21ced1397208b16941973620cb0", NULL},
    {"player", "5d561758b35cdc2c312bd6b97ba1838fbf4d33d6c9b00773b6a9d842dc9c599c",
     "f7d8e3d4cd197d576a2fd1b6c74ac7b93f8cd21ced1397208b16941973620cb0", NULL},
    {"left", "a2467744cb6e1971946ce14cb8c389d0a95c713b2c4ce25ccb48bbb923f8fda1",
     "f7d8e3d4cd197d576a2fd1b6c74ac7b93f8cd21ced1397208b16941973620cb0", NULL},
};
static const es2_screen_expect real_expect[] = {
    {"crew", "7e8599656ca51931d42219d2509ae827f00f2e101cce3bdea12795cfdb3a4722",
     "744dc2f654b97b0e0cd30247585d27d7f7efaf2964fc3b14c04e873d8a4d9587", NULL},
    {"order3", "22ba8211b2e7a9c49911af3db47ee395df3dd1e47a11c03ecc8f91df755ff4cf",
     "744dc2f654b97b0e0cd30247585d27d7f7efaf2964fc3b14c04e873d8a4d9587", NULL},
    {"pilot", "ffd42f544e305137a16b1b2822a41d1561b8a00ad01eb6750a5eb563fb8e096d",
     "744dc2f654b97b0e0cd30247585d27d7f7efaf2964fc3b14c04e873d8a4d9587", NULL},
    {"assigned", "411fe3c9f8c6eba2f304d08fb4a1ef981cf44855816fee014527443995aa44f9",
     "744dc2f654b97b0e0cd30247585d27d7f7efaf2964fc3b14c04e873d8a4d9587", NULL},
    {"cleared", "58255dd8ada818011d1bf2dd5aa38383948d644d30b4f43cacfa12b2069f6065",
     "744dc2f654b97b0e0cd30247585d27d7f7efaf2964fc3b14c04e873d8a4d9587", NULL},
    {"player", "04e443f340600db70d4366a3d69422a3cbc15f2cdc919be1976548255d4c424a",
     "744dc2f654b97b0e0cd30247585d27d7f7efaf2964fc3b14c04e873d8a4d9587", NULL},
    {"left", "b340069132484920de3dc596c5aea152aa623d59497c989cdd7a0304a5e39a12",
     "744dc2f654b97b0e0cd30247585d27d7f7efaf2964fc3b14c04e873d8a4d9587", NULL},
};

#define N(a) (sizeof(a) / sizeof((a)[0]))
static const es2_screen_case cases[] = {
    {"crew.tour", "crew", 0, sizeof(cr_state), setup, tour, tour_expect, N(tour_expect), NULL},
    {"crew.real", "crew", 1, sizeof(cr_state), setup_real, tour, real_expect, N(real_expect), NULL},
};

ES2_SCREEN_SUITE(crew, cases)
