/* Screen cases of the mission screen, pages 0, 1 and 4
 * (es2/shell/gui/mission.h) on the screen test harness.
 *
 * Synthetic cases: an 8 x 10 font built like main_menu_fixture.h's, seeded
 * images for dba\miss_arw.dba, dba\terradef.dba and dba\th_earth.dba, a
 * seeded palette per palette index, placeholder texts ("T13" for ESTEXT
 * 0x13) and generated panel texts. The real cases read dfn\font2.dfn,
 * dba\miss_arw.dba, dba\terradef.dba, dba\th_earth.dba, dpl\cam_er.dpl,
 * dpl\br_w2.dpl, dpl\db_w2.dpl, ENG\ESTEXT.BIN and ENG\CAMPAIGN.STR from the
 * CD's VOL archives (page-1 and page-4 texts stay generated). The screen is
 * drawn without the service bay under it (es2shell puts them together). VSHELL
 * references do not exist yet: the expectations are our own hashes;
 * vshell_ui.md section 33 lists the states a capture tour must capture. */
#include "screen_harness.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "es2/formats/dbm.h"
#include "es2/formats/dpl.h"
#include "es2/formats/vfs.h"
#include "es2/formats/vol.h"
#include "es2/shell/gui/mission.h"
#include "main_menu_fixture.h"
#include "menu.h"

typedef struct ms_state {
    es2_ui ui;
    es2_shell_flow flow;
    es2_shell_movies movies;
    es2_mission_screen m;
    int real;
    char text[128];
} ms_state;

enum { IMG_ARROW = 0, IMG_TERRADEF = 12, IMG_EARTH = 13, IMG_COUNT = 14 };

typedef struct ms_resources {
    int ready;
    es2_dfn font;
    es2_bitmap images[IMG_COUNT];
    uint8_t records[20][256][4];
    int has_records[20];
    es2_span estext, campaign;
} ms_resources;

static ms_resources synthetic, real;
static char texts[5][4096];

/* ---- host -------------------------------------------------------------------------- */
static ms_resources *res(void *ctx) { return ((ms_state *)ctx)->real ? &real : &synthetic; }
static const es2_dfn *h_font(void *ctx, uint32_t global) {
    return global == 0x46de00 || global == 0x46ddfc ? &res(ctx)->font : NULL;
}
static const es2_bitmap *h_bitmap(void *ctx, const char *file, int frame) {
    ms_resources *r = res(ctx);
    if (!strcmp(file, "dba\\miss_arw.dba") && frame >= 0 && frame < 12) return &r->images[IMG_ARROW + frame];
    if (!strcmp(file, "dba\\terradef.dba") && frame == 0) return &r->images[IMG_TERRADEF];
    if (!strcmp(file, "dba\\th_earth.dba") && frame == 0) return &r->images[IMG_EARTH];
    return NULL;
}
static const char *h_text(void *ctx, uint32_t id) {
    ms_state *st = ctx;
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
static int16_t h_group(void *ctx) { (void)ctx; return 2; }
static const char *h_campaign(void *ctx, int16_t index) {
    static char text[4096];
    es2_span t;
    if (!((ms_state *)ctx)->real) return texts[0];
    if (es2_shell_campaign_text(real.campaign, (int16_t)(index + 1), &t, NULL) != ES2_OK || t.size >= sizeof text) return NULL;
    memcpy(text, t.data, t.size);
    text[t.size] = 0;
    return text;
}
static const char *h_report(void *ctx, int panel) { (void)ctx; return panel >= 1 && panel <= 4 ? texts[panel] : NULL; }
static void h_words(void *ctx, int16_t *b, int16_t *d) { (void)ctx; *b = 7; *d = 9; }
static void palette(es2_ui *ui, uint16_t word) {
    ms_resources *r = res(ui->context);
    if (word < 20 && r->has_records[word]) es2_gui_palette_select(&ui->shell->frame, (const uint8_t(*)[4])r->records[word]);
    es2_gui_palette_apply(&ui->shell->frame);
}

/* ---- resources ------------------------------------------------------------------------ */
static int font_8x10(uint8_t *bytes, es2_dfn *out) {
    enum { W = 8, H = 10 };
    size_t at = 8, data_size = (size_t)MMF_COUNT * W * H, data_at;
    at = mmf_put16(bytes, at, MMF_COUNT);
    at = mmf_put16(bytes, at, 0);
    at = mmf_put16(bytes, at, MMF_FIRST);
    at = mmf_put16(bytes, at, H);
    at = mmf_put16(bytes, at, W);
    at = mmf_put16(bytes, at, H);
    at = mmf_put16(bytes, at, 0);
    at = mmf_put16(bytes, at, 8);
    at = mmf_put16(bytes, at, 0);
    at = mmf_put16(bytes, at, H - 1);
    at = mmf_put16(bytes, at, 0);
    at = mmf_put32(bytes, at, (uint32_t)data_size);
    data_at = at;
    for (int g = 0; g < MMF_COUNT; g++)
        for (int k = 0; k < W * H; k++)
            bytes[data_at + (size_t)g * W * H + (size_t)k] = (uint8_t)(((unsigned)(g * 31 + k * 7) % 5) < 2 ? 41 : 0);
    at = data_at + data_size;
    for (int g = 0; g < MMF_COUNT; g++) at = mmf_put32(bytes, at, (uint32_t)(g * W * H));
    bytes[0] = 5;
    bytes[1] = 0;
    bytes[2] = 0x28;
    bytes[3] = 0;
    mmf_put32(bytes, 4, (uint32_t)(at - 8));
    if ((at - 8) & 1) bytes[at++] = 0;
    return es2_dfn_parse((es2_span){bytes, at}, out, NULL) == ES2_OK ? 0 : -1;
}

static void words(char *out, size_t size, const char *stem, int n) {
    size_t at = 0;
    out[0] = 0;
    for (int k = 0; k < n && at + 16 < size; k++) at += (size_t)snprintf(out + at, size - at, "%s%03d ", stem, k);
}

static uint8_t synthetic_font_bytes[16384];
static uint8_t synthetic_pixels[IMG_COUNT][300 * 300];
static int synthetic_load(void) {
    if (synthetic.ready) return 0;
    if (font_8x10(synthetic_font_bytes, &synthetic.font)) return 1;
    for (int k = 0; k < IMG_COUNT; k++) {
        int w = k < IMG_TERRADEF ? 30 : k == IMG_TERRADEF ? 240 : 295, h = k < IMG_TERRADEF ? 30 : k == IMG_TERRADEF ? 237 : 226;
        es2_screen_fixture_image(synthetic_pixels[k], w, h, 390u + (unsigned)k);
        synthetic.images[k] = (es2_bitmap){(uint16_t)h, (uint16_t)w, 0, synthetic_pixels[k]};
    }
    for (int p = 0; p < 20; p++) {
        es2_screen_fixture_palette(synthetic.records[p], 80u + (unsigned)p);
        synthetic.has_records[p] = 1;
    }
    words(texts[0], sizeof texts[0], "sector", 120);
    words(texts[1], sizeof texts[1], "brief", 300);
    words(texts[2], sizeof texts[2], "goal", 40);
    words(texts[3], sizeof texts[3], "intel", 90);
    words(texts[4], sizeof texts[4], "debrief", 60);
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
static uint8_t real_pixels[IMG_COUNT][80000];
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
    static const int palettes[3] = {3, 6, 11}; /* cam_er, br_w2, db_w2: 0x43b7f2(7) for group 2 */
    es2_vfs_copy c;
    if (real.ready) return real.ready < 0 ? 2 : 0;
    real.ready = -1;
    if (synthetic_load()) return 1; /* the generated page-1/4 texts */
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
    if (es2_vfs_find(&vfs, "dfn\\font2.dfn", &c, NULL) != ES2_OK || es2_dfn_parse(c.payload, &real.font, NULL) != ES2_OK)
        return 4;
    for (int k = 0; k < 12; k++)
        if (!member("dba\\miss_arw.dba", (size_t)k, real_pixels[k], sizeof real_pixels[k], &real.images[k])) return 5;
    if (!member("dba\\terradef.dba", 0, real_pixels[IMG_TERRADEF], sizeof real_pixels[0], &real.images[IMG_TERRADEF])) return 6;
    if (!member("dba\\th_earth.dba", 0, real_pixels[IMG_EARTH], sizeof real_pixels[0], &real.images[IMG_EARTH])) return 7;
    for (int k = 0; k < 3; k++) {
        es2_dpl dpl;
        int p = palettes[k];
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
    if (es2_vfs_find(&vfs, "eng\\campaign.str", &c, NULL) != ES2_OK) return 10;
    real.campaign = c.payload;
    real.ready = 1;
    return 0;
}

/* ---- the presenter --------------------------------------------------------------------- */
static void present(void *context, const es2_gui_event *ev) { es2_ui_on_event(&((ms_state *)context)->ui, ev); }
static void timer(void *context, es2_gui_shell *shell) { es2_ui_on_timer(&((ms_state *)context)->ui, shell); }

/* The screen built and opened on `page` (as the service bay's MISSION would,
 * without the bay under it), with the page's palette (0x43b7f2(7)); with
 * `stats` the results path's 0x40f904 runs first (vshell_ui.md 14.2). */
static void statistics(ms_state *st) {
    static const uint32_t g[10] = {0x48d91c, 0x48d924, 0x48d92c, 0x48d94c, 0x48d958,
                                   0x48d950, 0x48d95c, 0x48d954, 0x48d960, 0x48d964};
    static const char *const v[10] = {"T146", "12 T149", "3", "2", "5", "0", "1", "4", "6", "1"};
    es2_shell_debrief_texts t;
    memset(&t, 0, sizeof t);
    for (int k = 0; k < 10; k++) {
        snprintf(t.text[k], sizeof t.text[k], "%s", v[k]);
        t.widget[k] = g[k];
    }
    es2_mission_screen_statistics(&st->m, &t);
}

static int open_screen(es2_screen_session *s, ms_state *st, int page, int stats) {
    es2_shell_flow_host fh;
    int p;
    memset(&fh, 0, sizeof fh);
    es2_shell_flow_init(&st->flow, &fh, NULL);
    st->flow.mode = 1;
    st->flow.game = ES2_FLOW_GAME_CAMPAIGN;
    st->flow.final_word = 2;
    st->flow.screen = ES2_FLOW_SERVICE_BAY;
    st->flow.bay_word = ES2_FLOW_WORD_MISSION;
    st->flow.page = page;
    es2_shell_movies_init(&st->movies, 1);
    if (es2_ui_init(&st->ui, s->shell)) return 2;
    st->ui.context = st;
    st->ui.palette = palette;
    st->m.flow = &st->flow;
    st->m.movies = &st->movies;
    st->m.host = (es2_mission_screen_host){st, h_font, h_bitmap, h_text, h_group, h_campaign, h_report, h_words,
                                           NULL, NULL, NULL};
    if (es2_screen_mission_build(&st->ui, &st->m)) return 3;
    s->shell->frame.realize_gate = 1;
    p = es2_palette_for_bay_screen(7, (int16_t)page, 2);
    palette(&st->ui, (uint16_t)(p >= 0 ? p : ES2_PALETTE_MAIN));
    s->shell->input.interactive = 1;
    s->screen.context = st;
    s->screen.handle = present;
    s->screen.timer = timer;
    es2_ui_show(&st->ui, st->ui.root);
    if (stats) statistics(st);
    es2_mission_screen_show(&st->m, page);
    return 0;
}

static int setup_page0(es2_screen_session *s, void *state, const char *data_root) {
    (void)data_root;
    if (synthetic_load()) return 1;
    return open_screen(s, state, 0, 0);
}
static int setup_page1(es2_screen_session *s, void *state, const char *data_root) {
    (void)data_root;
    if (synthetic_load()) return 1;
    return open_screen(s, state, 1, 0);
}
/* the results path's entry: 0x40f904 fills the statistics before the first
 * page-4 visit, so they show (vshell_ui.md 14.2) */
static int setup_debrief(es2_screen_session *s, void *state, const char *data_root) {
    (void)data_root;
    if (synthetic_load()) return 1;
    return open_screen(s, state, 4, 1);
}
/* a second page-4 visit after a leave: the statistics stay hidden (hypothesis) */
static int setup_debrief_again(es2_screen_session *s, void *state, const char *data_root) {
    ms_state *st = state;
    int r;
    if ((r = setup_debrief(s, state, data_root))) return r;
    es2_mission_screen_leave(&st->m);
    es2_mission_screen_show(&st->m, 4);
    return 0;
}
static int setup_real(es2_screen_session *s, void *state, const char *data_root, int page) {
    int r;
    if (!data_root) return 1;
    if ((r = real_load(data_root))) return 10 + r;
    ((ms_state *)state)->real = 1;
    return open_screen(s, state, page, page == 4);
}
static int setup_real0(es2_screen_session *s, void *state, const char *root) { return setup_real(s, state, root, 0); }
static int setup_real1(es2_screen_session *s, void *state, const char *root) { return setup_real(s, state, root, 1); }
static int setup_real4(es2_screen_session *s, void *state, const char *root) { return setup_real(s, state, root, 4); }

/* ---- cases -------------------------------------------------------------------------------- */
static const es2_screen_expect page0_expect[] = {{"page0", "93ad4329e0b3255ab3a5fa7b54412fa18fa6a2651ef331aefa439708d02fa61b",
     "a838a5a52d97bc1efc2b533540b3882ab5046b4ebf75b410891a837eb6e9cf2a", NULL}};
static const es2_screen_expect page1_expect[] = {
    {"page1", "ab0fc6a66fbff2b1e1f61c292884990a062c7dcb17189caafd49287e23dcf540",
     "7100a9676ec0b1c5a9e4a0ec684e1bdf8a072aa88b14a4b3dfd27e37196153bc", NULL}, {"objectives", "e259d5b74412088182315c79ec6f36adbe76ef38a8b41b9c74babf58a8057cd5",
     "7100a9676ec0b1c5a9e4a0ec684e1bdf8a072aa88b14a4b3dfd27e37196153bc", NULL}, {"intelligence", "18e98ad4185637cf3050f97a6fd6b887c1f7f5eaa92583502a6c6d71549e2cad",
     "7100a9676ec0b1c5a9e4a0ec684e1bdf8a072aa88b14a4b3dfd27e37196153bc", NULL},
    {"briefing", "ab0fc6a66fbff2b1e1f61c292884990a062c7dcb17189caafd49287e23dcf540",
     "7100a9676ec0b1c5a9e4a0ec684e1bdf8a072aa88b14a4b3dfd27e37196153bc", NULL}, {"next", "a98afbc76ed9d91dd3d922446ec42f11de1ee1b315e49ed6970efd5f44226140",
     "7100a9676ec0b1c5a9e4a0ec684e1bdf8a072aa88b14a4b3dfd27e37196153bc", NULL},
};
static const es2_screen_expect debrief_expect[] = {{"debrief", "223261f8446a7e4802075e87f9d31aa973949f659fb6d7f5cff3631dedd26427",
     "a0df143b6a6a5ad42a93c54bda883e05fdb94221319a0ca4dfcdf7b74c0f591c", NULL}};
static const es2_screen_expect debrief_again_expect[] = {{"debrief_again", "761cdb316c75b0ac4424904aa80cc6d42e884317c125f1961ae5a1cfa650bebf",
     "a0df143b6a6a5ad42a93c54bda883e05fdb94221319a0ca4dfcdf7b74c0f591c", NULL}};
/* VSHELL's game_r:mission:opened is the page over the bay with the
 * tab bar, a composition this case does not draw; //shell:gui_tour_real_test
 * compares es2shell's whole frame with it (equal). */
static const es2_screen_expect real0_expect[] = {{"page0", "083734e8957048d5667adf82d85f1c7cbf02ac5272361ec60fcace9fd18d623f",
     "b351ec285e88e84084544b298cc599c654fe74069019fd222e70f96172489f3b", NULL}};
static const es2_screen_expect real1_expect[] = {{"page1", "7a22adcf46c4ead5eac7fce4acfd072e88b2a8661b6e107f086436b4bd2c9309",
     "50d3adfcb6f2d283e1ecbd9283f93af82d97fe5f5749603b066725c909098c8a", NULL}, {"objectives", "dcf64d9ff074aca49adc8e1e8dd9d017878bf9ad6a21e094b212f3389c4eb418",
     "50d3adfcb6f2d283e1ecbd9283f93af82d97fe5f5749603b066725c909098c8a", NULL}};
static const es2_screen_expect real4_expect[] = {{"debrief", "2a8419ff221be71b398f8f08a9b719267d89353e17b1a1afe37bdf5eb96509d6",
     "50d3adfcb6f2d283e1ecbd9283f93af82d97fe5f5749603b066725c909098c8a", NULL}};

#define N(a) (sizeof(a) / sizeof((a)[0]))
static const es2_screen_case cases[] = {
    {"mission.page0", "mission", 0, sizeof(ms_state), setup_page0, "step\nsnap page0\n", page0_expect, N(page0_expect), NULL},
    {"mission.page1", "mission", 0, sizeof(ms_state), setup_page1,
     "step\n"
     "snap page1\n"
     "click mission.23\n"
     "snap objectives\n"
     "click mission.25\n"
     "snap intelligence\n"
     "click mission.21\n"
     "snap briefing\n"
     "click mission.53\n"
     "snap next\n",
     page1_expect, N(page1_expect), NULL},
    {"mission.debrief", "mission", 0, sizeof(ms_state), setup_debrief, "step\nsnap debrief\n", debrief_expect,
     N(debrief_expect), NULL},
    {"mission.debrief_again", "mission", 0, sizeof(ms_state), setup_debrief_again, "step\nsnap debrief_again\n",
     debrief_again_expect, N(debrief_again_expect), NULL},
    {"mission.real_page0", "mission", 1, sizeof(ms_state), setup_real0, "step\nsnap page0\n", real0_expect, N(real0_expect),
     NULL},
    {"mission.real_page1", "mission", 1, sizeof(ms_state), setup_real1, "step\nsnap page1\nclick mission.23\nsnap objectives\n",
     real1_expect, N(real1_expect), NULL},
    {"mission.real_debrief", "mission", 1, sizeof(ms_state), setup_real4, "step\nsnap debrief\n", real4_expect,
     N(real4_expect), NULL},
};

ES2_SCREEN_SUITE(mission, cases)
