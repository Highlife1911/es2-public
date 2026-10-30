/* Frame hashes of the Preferences screen 0x435598 (screen test harness).
 *
 * Synthetic cases (main_menu_fixture.h: a fixed-width font, seeded images
 * for bay2a_84 and the two chk_box members, a seeded palette) and real cases
 * (needs_data: dfn\font2.dfn, dbm\bay2a_84.dbm, dba\chk_box.dba,
 * dpl\palette.dpl, ENG\ESTEXT.BIN from the CD's VOL\SHELL0.VOL, SHELL1.VOL
 * and LANG0.VOL). The screen is opened as PREFERENCES 0x431b9b does (the main
 * menu's hide, then the show 0x436d45) from a prefs block with music on,
 * repair 1, weapons building 0, high resolution and the window. States: the
 * screen, a check box and a radio after their clicks, the Alert box of Full
 * Screen and the screen after its ACCEPT, and what CANCEL and ACCEPT leave
 * (the main menu). No capture-tour reference exists yet; the capture states a
 * capture tour needs are listed in vshell_ui.md section 27. */
#include "screen_harness.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "es2/formats/dbm.h"
#include "es2/formats/dpl.h"
#include "es2/formats/vfs.h"
#include "es2/formats/vol.h"
#include "es2/shell/gui/preferences.h"
#include "main_menu_fixture.h"
#include "menu.h"

typedef struct pf_state {
    es2_ui ui;
    es2_shell_flow flow;
    es2_main_menu menu;
    es2_boxes boxes;
    es2_preferences prefs;
    int real;
} pf_state;

/* ---- resources ------------------------------------------------------------- */
enum { PF_TEXTS = 0x128 };
typedef struct pf_resources {
    int ready;
    es2_dfn font;
    es2_bitmap background, boxes[2];
    uint8_t records[256][4];
    char texts[PF_TEXTS][48];
} pf_resources;
static pf_resources synthetic, real;
static uint8_t synthetic_font[16384];
static uint8_t synthetic_background[640 * 480], synthetic_boxes[2][18 * 16];
static uint8_t real_background[640 * 480], real_boxes[2][4096];

static int synthetic_load(void) {
    if (synthetic.ready) return 0;
    if (mmf_font(synthetic_font, &synthetic.font)) return 1;
    mmf_image(synthetic_background, 4);
    synthetic.background = (es2_bitmap){480, 640, 0, synthetic_background};
    for (int m = 0; m < 2; m++) {
        for (int i = 0; i < 18 * 16; i++) synthetic_boxes[m][i] = (uint8_t)(m ? 30 + (i % 18 + i / 18) % 4 : 200 + i % 7);
        synthetic.boxes[m] = (es2_bitmap){16, 18, 0, synthetic_boxes[m]};
    }
    mmf_palette(synthetic.records);
    for (int id = 0; id < PF_TEXTS; id++) snprintf(synthetic.texts[id], sizeof synthetic.texts[id], "ENTRY %X", id);
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

/* The VOLs stay mapped for the process (the bitmaps and the font borrow them). */
static es2_vol vols[3];
static es2_vfs_source sources[3];
static es2_vfs vfs;
static int real_load(const char *root) {
    static const char *const names[3] = {"SHELL0.VOL", "SHELL1.VOL", "LANG0.VOL"};
    es2_vfs_copy c;
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
    if (es2_vfs_find(&vfs, "dfn\\font2.dfn", &c, NULL) != ES2_OK || es2_dfn_parse(c.payload, &real.font, NULL) != ES2_OK)
        return 4;
    {
        es2_dbm dbm;
        if (es2_vfs_find(&vfs, es2_menu_background_file(4), &c, NULL) != ES2_OK ||
            es2_dbm_parse(c.payload, &dbm, NULL) != ES2_OK ||
            es2_draw_bitmap_from_dbm(&dbm, real_background, sizeof real_background, &real.background) != ES2_DRAW_DONE)
            return 5;
    }
    {
        es2_dba dba;
        if (es2_vfs_find(&vfs, "dba\\chk_box.dba", &c, NULL) != ES2_OK || es2_dba_parse(c.payload, &dba, NULL) != ES2_OK)
            return 6;
        for (int m = 0; m < 2; m++) {
            es2_dbm dbm;
            if (es2_dba_image_at(&dba, (size_t)m, &dbm, NULL) != ES2_OK ||
                es2_draw_bitmap_from_dbm(&dbm, real_boxes[m], sizeof real_boxes[m], &real.boxes[m]) != ES2_DRAW_DONE)
                return 6;
        }
    }
    {
        es2_dpl dpl;
        if (es2_vfs_find(&vfs, es2_palette_file(ES2_PALETTE_MAIN), &c, NULL) != ES2_OK ||
            es2_dpl_parse(c.payload, &dpl, NULL) != ES2_OK)
            return 7;
        memset(real.records, 0, sizeof real.records);
        for (size_t i = 0; i < 256 && i < dpl.colour_count; i++) {
            es2_dpl_colour col;
            if (es2_dpl_colour_at(&dpl, i, &col, NULL) != ES2_OK) return 7;
            real.records[i][0] = col.red;
            real.records[i][1] = col.green;
            real.records[i][2] = col.blue;
            real.records[i][3] = col.flags;
        }
    }
    if (es2_vfs_find(&vfs, "ENG/ESTEXT.BIN", &c, NULL) != ES2_OK) return 8;
    for (int id = 0; id < PF_TEXTS; id++) {
        es2_span t;
        if (es2m_bin_raw(c.payload, (size_t)id, &t, NULL) != ES2_OK) continue;
        size_t n = t.size < sizeof real.texts[id] - 1 ? t.size : sizeof real.texts[id] - 1;
        memcpy(real.texts[id], t.data, n);
        real.texts[id][n] = 0;
    }
    if (!real.texts[0x103][0] || !real.texts[0x126][0]) return 9; /* the panel title, the Alert title */
    real.ready = 1;
    return 0;
}

static pf_resources *resources_of(void *ctx) { return ((pf_state *)ctx)->real ? &real : &synthetic; }
static const es2_dfn *h_font(void *ctx, uint32_t global) {
    return global == 0x46ddfc || global == 0x46de00 ? &resources_of(ctx)->font : NULL;
}
static const es2_bitmap *h_menu_bitmap(void *ctx, const char *file) {
    return !strcmp(file, es2_menu_background_file(4)) ? &resources_of(ctx)->background : NULL;
}
static const es2_bitmap *h_bitmap(void *ctx, const char *file, int member) {
    if (!strcmp(file, "dba\\chk_box.dba") && (member == 0 || member == 1)) return &resources_of(ctx)->boxes[member];
    if (!strcmp(file, es2_menu_background_file(4))) return &resources_of(ctx)->background;
    return NULL;
}
static const char *h_text(void *ctx, uint32_t id) { return id < PF_TEXTS ? resources_of(ctx)->texts[id] : NULL; }
static int f_full_screen(void *ctx) { return ((pf_state *)ctx)->ui.shell->fullscreen; }

static void on_event(void *context, const es2_gui_event *ev) { es2_ui_on_event(&((pf_state *)context)->ui, ev); }
static void on_timer(void *context, es2_gui_shell *shell) {
    pf_state *st = context;
    st->ui.now += 100; /* 100 ms per main-loop pass */
    es2_ui_on_timer(&st->ui, shell);
}

static int setup(es2_screen_session *s, void *state, int real_data, int tour, const char *root) {
    pf_state *st = state;
    es2_shell_flow_host fh;
    es2_boxes_host bh;
    es2_preferences_host ph = {st, h_font, h_bitmap, h_text, NULL, NULL};
    uint8_t file[ES2_SHELL_PREFS_SIZE] = {0};
    es2_span pf = {file, sizeof file};
    int code = real_data ? real_load(root) : synthetic_load();
    if (code) return code;
    if (real_data && es2_screen_main_menu_history(s, root, "19")) return 15; /* PREFERENCES over the menu */
    st->real = real_data;
    if (es2_ui_init(&st->ui, s->shell)) return 10;
    st->ui.context = st;
    file[0] = 1;                       /* music on, sound effects off */
    file[ES2_SHELL_PREFS_REPAIR] = 1;  /* repair 1 */
    if (tour) {
        /* The settings of the tour's clone (calibration-r111 DATA\PREFS.CFG,
         * unchanged by the tour): sound effects on, repair 0 */
        file[1] = 1;
        file[ES2_SHELL_PREFS_REPAIR] = 0;
    }
    memset(&fh, 0, sizeof fh);
    fh.ctx = st;
    fh.full_screen = f_full_screen;
    es2_shell_flow_init(&st->flow, &fh, &pf);
    st->menu.flow = &st->flow;
    st->menu.host = (es2_main_menu_host){st, h_font, h_menu_bitmap, NULL, NULL, NULL, NULL, NULL, NULL};
    st->menu.host.text = h_text;
    if (es2_screen_main_menu_build(&st->ui, &st->menu)) return 11;
    memset(&bh, 0, sizeof bh);
    bh.ctx = &st->prefs;
    bh.prefs_refresh = es2_preferences_display_refresh;
    es2_boxes_init(&st->boxes, &st->ui, &st->flow, &bh, 0);
    if (es2_boxes_build(&st->boxes)) return 12;
    es2_preferences_init(&st->prefs, &st->flow, &st->menu, &st->boxes, &ph);
    if (es2_screen_preferences_build(&st->ui, &st->prefs)) return 13;
    es2_ui_show(&st->ui, st->ui.root);
    /* the main menu's first appearance left: the gate open, palette.dpl selected and applied */
    s->shell->frame.realize_gate = 1;
    es2_gui_palette_select(&s->shell->frame, resources_of(st)->records);
    es2_gui_palette_apply(&s->shell->frame);
    /* PREFERENCES 0x431b9b: the main menu's hide, then 0x436d45 */
    if (es2_main_menu_press(&st->menu, ES2_FLOW_PREFERENCES_BUTTON) != ES2_FLOW_DONE) return 14;
    es2_preferences_show(&st->prefs);
    s->screen.context = st;
    s->screen.handle = on_event;
    s->screen.timer = on_timer;
    return 0;
}
static int setup_synthetic(es2_screen_session *s, void *state, const char *root) { return setup(s, state, 0, 0, root); }
static int setup_real(es2_screen_session *s, void *state, const char *root) { return setup(s, state, 1, 0, root); }
static int setup_real_tour(es2_screen_session *s, void *state, const char *root) { return setup(s, state, 1, 1, root); }

/* ---- scripts ---------------------------------------------------------------- */
static const char script_shown[] = "step 2\nsnap shown\n";
static const char script_music[] = "step 2\nclick preferences.35\nstep 2\nsnap music_off\n";
static const char script_repair[] = "step 2\nclick preferences.47\nstep 2\nsnap repair_2\n";
static const char script_alert[] = "step 2\nclick preferences.22\nstep 2\nsnap alert\nclick preferences.05\nstep 2\nsnap full_screen\n";
static const char script_cancel[] = "step 2\nclick preferences.35\nclick preferences.09\nstep 2\nsnap after\n";
static const char script_accept[] = "step 2\nclick preferences.35\nclick preferences.11\nstep 2\nsnap after\n";

/* ---- expectations (our hashes; reference NULL until a capture tour) -------------- */
/* CANCEL and ACCEPT leave the same frame: the main menu over the hidden screen. */
#define T_SYN "98fd384cc75016998f60c80a25cbc264969c4900c6962998d4e01d91a1a9384c"
#define T_REAL "96b549df7c20ad4f6b89e13333204a31f25ffedaf43cbf4bcc330bef14f4042a"
static const es2_screen_expect expect_shown[] = {
    {"shown", "7f17c6f51a78ef2b1ff7d3d3764dad483611d013078c0a2963c7006933a4d87f", T_SYN, NULL}};
static const es2_screen_expect expect_music[] = {
    {"music_off", "793c969f8c338ef9a974425a4bf20130b85eb52154ef32c8405bee9fd88369b8", T_SYN, NULL}};
static const es2_screen_expect expect_repair[] = {
    {"repair_2", "5bbd1454f903f2b214328d4f553931d4e37aa63452c77f93ac17b315cdc7b246", T_SYN, NULL}};
static const es2_screen_expect expect_alert[] = {
    {"alert", "f987ce018bebb37b8353718943c46d888d355faec648b3f301783246d389eeeb", T_SYN, NULL},
    {"full_screen", "b692e19e7d92b4fa3343ccf19703258c498e1af0958a39f17b9390f6545527d6", T_SYN, NULL}};
static const es2_screen_expect expect_cancel[] = {
    {"after", "0e290c4030f70651c38bbd2c9f649d6e8b6f4a7260c18943cb71befa1fa3f8d1", T_SYN, NULL}};
static const es2_screen_expect expect_accept[] = {
    {"after", "0e290c4030f70651c38bbd2c9f649d6e8b6f4a7260c18943cb71befa1fa3f8d1", T_SYN, NULL}};
static const es2_screen_expect expect_real_shown[] = {
    {"shown", "bdd48fe5409837906b0800c00f6ab86208cdc5495197bf909c33aa98bd61add4", T_REAL, "game_r:preferences:from_main_menu"}};
static const es2_screen_expect expect_real_alert[] = {
    {"alert", "1f1400f350366f5765e9ec247534f39b07352086e234e042914a7de5b6e91253", T_REAL, NULL},
    {"full_screen", "ddebfefa2f39e2b9eae58a8a2b70f8805d96bada5fb3e5db6dca9f2ea1a71142", T_REAL, NULL}};

#define CASE(name, data, setup_fn, script, expect)                                                             \
    {"preferences." name, "preferences", data, sizeof(pf_state), setup_fn, script, expect,                     \
     sizeof(expect) / sizeof((expect)[0]), NULL}

static const es2_screen_case cases[] = {
    CASE("shown", 0, setup_synthetic, script_shown, expect_shown),
    CASE("music", 0, setup_synthetic, script_music, expect_music),
    CASE("repair", 0, setup_synthetic, script_repair, expect_repair),
    CASE("alert", 0, setup_synthetic, script_alert, expect_alert),
    CASE("cancel", 0, setup_synthetic, script_cancel, expect_cancel),
    CASE("accept", 0, setup_synthetic, script_accept, expect_accept),
    CASE("real_shown", 1, setup_real_tour, script_shown, expect_real_shown),
    CASE("real_alert", 1, setup_real, script_alert, expect_real_alert),
};

ES2_SCREEN_SUITE(preferences, cases)
