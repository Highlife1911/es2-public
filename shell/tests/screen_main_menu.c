/* Frame hashes of the main menu screen 0x430fc4 (screen test harness).
 *
 * Synthetic cases (main_menu_fixture.h: a fixed-width font, five seeded
 * images for bay2a_80..84, a seeded palette) and real cases (needs_data:
 * dfn\font2.dfn, dbm\bay2a_80..84.dbm, dpl\palette.dpl, ENG\ESTEXT.BIN from
 * the CD's VOL\SHELL0.VOL, SHELL1.VOL and LANG0.VOL). States: each animation
 * frame, the menu, a button held down, CONTINUE enabled, and the frame after
 * each button's handler (the flow's next screen is another package's: the
 * snap shows what the main menu leaves). The tour's capture (frames.json) gives
 * the reference of real_continue_enabled; real_animation's frames equal its
 * start-up presents (vshell_presentation.md, section "the tours"). */
#include "screen_harness.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "es2/formats/dbm.h"
#include "es2/formats/dpl.h"
#include "es2/formats/vfs.h"
#include "es2/formats/vol.h"
#include "es2/shell/gui/main_menu.h"
#include "main_menu_fixture.h"
#include "menu.h"

typedef struct mm_state {
    es2_ui ui;
    es2_shell_flow flow;
    es2_shell_movies movies;
    es2_main_menu menu;
    int real;
} mm_state;

/* ---- resources ------------------------------------------------------------- */
typedef struct mm_resources {
    int ready;
    es2_dfn font;
    es2_bitmap bitmaps[5];
    uint8_t records[256][4];
    char texts[16][64];
} mm_resources;
static mm_resources synthetic, real;
static uint8_t synthetic_font[16384];
static uint8_t synthetic_images[5][640 * 480];
static uint8_t real_scratch[5][640 * 480];

static int synthetic_load(void) {
    if (synthetic.ready) return 0;
    if (mmf_font(synthetic_font, &synthetic.font)) return 1;
    for (int k = 0; k < 5; k++) {
        mmf_image(synthetic_images[k], k);
        synthetic.bitmaps[k] = (es2_bitmap){480, 640, 0, synthetic_images[k]};
    }
    mmf_palette(synthetic.records);
    for (int id = 0; id < 16; id++) snprintf(synthetic.texts[id], sizeof synthetic.texts[id], "ENTRY %d", id);
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
    for (int k = 0; k < 5; k++) {
        es2_dbm dbm;
        if (es2_vfs_find(&vfs, es2_menu_background_file(k), &c, NULL) != ES2_OK ||
            es2_dbm_parse(c.payload, &dbm, NULL) != ES2_OK ||
            es2_draw_bitmap_from_dbm(&dbm, real_scratch[k], sizeof real_scratch[k], &real.bitmaps[k]) != ES2_DRAW_DONE)
            return 5;
    }
    {
        es2_dpl dpl;
        if (es2_vfs_find(&vfs, es2_palette_file(ES2_PALETTE_MAIN), &c, NULL) != ES2_OK ||
            es2_dpl_parse(c.payload, &dpl, NULL) != ES2_OK)
            return 6;
        memset(real.records, 0, sizeof real.records);
        for (size_t i = 0; i < 256 && i < dpl.colour_count; i++) {
            es2_dpl_colour col;
            if (es2_dpl_colour_at(&dpl, i, &col, NULL) != ES2_OK) return 6;
            real.records[i][0] = col.red;
            real.records[i][1] = col.green;
            real.records[i][2] = col.blue;
            real.records[i][3] = col.flags;
        }
    }
    if (es2_vfs_find(&vfs, "ENG/ESTEXT.BIN", &c, NULL) != ES2_OK) return 7;
    for (int id = 0; id < 16; id++) {
        es2_span t;
        if (es2m_bin_raw(c.payload, (size_t)id, &t, NULL) != ES2_OK) continue;
        size_t n = t.size < sizeof real.texts[id] - 1 ? t.size : sizeof real.texts[id] - 1;
        memcpy(real.texts[id], t.data, n);
        real.texts[id][n] = 0;
    }
    if (!real.texts[3][0] || !real.texts[0xe][0]) return 8; /* INSTANT ACTION, QUIT */
    real.ready = 1;
    return 0;
}

static mm_resources *resources_of(void *ctx) { return ((mm_state *)ctx)->real ? &real : &synthetic; }
static const es2_dfn *h_font(void *ctx, uint32_t global) {
    return global == 0x46ddfc || global == 0x46de00 ? &resources_of(ctx)->font : NULL;
}
static const es2_bitmap *h_bitmap(void *ctx, const char *file) {
    for (int k = 0; k < 5; k++)
        if (!strcmp(file, es2_menu_background_file(k))) return &resources_of(ctx)->bitmaps[k];
    return NULL;
}
static const char *h_text(void *ctx, uint32_t id) { return id < 16 ? resources_of(ctx)->texts[id] : NULL; }
static void palette(es2_ui *ui, uint16_t word) {
    mm_state *st = ui->context;
    if (word != ES2_PALETTE_MAIN) return;
    es2_gui_palette_select(&ui->shell->frame, resources_of(st)->records);
    es2_gui_palette_apply(&ui->shell->frame);
}
static int f_load(void *ctx, int slot, int16_t *final_word) { (void)ctx; (void)slot; *final_word = 2; return 1; }

static void on_event(void *context, const es2_gui_event *ev) { es2_ui_on_event(&((mm_state *)context)->ui, ev); }
static void on_timer(void *context, es2_gui_shell *shell) {
    mm_state *st = context;
    st->ui.now += 100; /* 100 ms per main-loop pass */
    es2_ui_on_timer(&st->ui, shell);
}

static int setup(es2_screen_session *s, void *state, int real_data, int continue_flag, const char *root) {
    mm_state *st = state;
    es2_shell_flow_host fh;
    int code = real_data ? real_load(root) : synthetic_load();
    if (code) return code;
    st->real = real_data;
    if (es2_ui_init(&st->ui, s->shell)) return 10;
    st->ui.context = st;
    st->ui.palette = palette;
    memset(&fh, 0, sizeof fh);
    fh.load = f_load;
    es2_shell_flow_init(&st->flow, &fh, NULL);
    es2_shell_movies_init(&st->movies, 0);
    st->menu.flow = &st->flow;
    st->menu.movies = &st->movies;
    st->menu.continue_flag = continue_flag;
    st->menu.host = (es2_main_menu_host){st, h_font, h_bitmap, h_text, NULL, NULL, NULL, NULL, NULL};
    if (es2_screen_main_menu_build(&st->ui, &st->menu)) return 11;
    es2_ui_show(&st->ui, st->ui.root);
    s->screen.context = st;
    s->screen.handle = on_event;
    s->screen.timer = on_timer;
    es2_main_menu_start(&st->menu);
    return 0;
}
static int setup_synthetic(es2_screen_session *s, void *state, const char *root) { return setup(s, state, 0, 0, root); }
static int setup_synthetic_slot(es2_screen_session *s, void *state, const char *root) { return setup(s, state, 0, 1, root); }
static int setup_real(es2_screen_session *s, void *state, const char *root) { return setup(s, state, 1, 0, root); }
static int setup_real_slot(es2_screen_session *s, void *state, const char *root) { return setup(s, state, 1, 1, root); }

/* ---- scripts ---------------------------------------------------------------- */
/* The animation: the first pass draws frame 0; a tick every five passes. */
#define ANIMATION "step\nsnap frame0\nstep 5\nsnap frame1\nstep 5\nsnap frame2\nstep 5\nsnap frame3\nstep 5\nsnap frame4\n" \
                  "step 5\nsnap menu\n"
static const char script_animation[] = ANIMATION;
/* START NEW GAME held down (two passes after the press), then released. */
static const char script_pressed[] = "step 30\nmove 231 230\nstep\nldown\nstep 2\nsnap pressed\nlup\nstep\nsnap released\n";
static const char script_menu[] = "step 30\nsnap menu\n";
#define AFTER(control) "step 30\nclick main_menu." control "\nstep 2\nsnap after\n"

/* ---- expectations (our hashes; reference NULL until a capture tour)
 * -------------- */
#define T_SYN "98fd384cc75016998f60c80a25cbc264969c4900c6962998d4e01d91a1a9384c"
#define T_REAL "96b549df7c20ad4f6b89e13333204a31f25ffedaf43cbf4bcc330bef14f4042a"
static const es2_screen_expect expect_animation[] = {
    {"frame0", "73a2ff2a37f71f5f371b0f91ede003035ef846c517133c466953dbb835ab10cb", T_SYN, NULL},
    {"frame1", "2c434d68d71680dfd0ff86d4d94f2ee6b8824734562306e475b26be5a3661793", T_SYN, NULL},
    {"frame2", "6faf9f90daff10d46fdb6b68b26f600f93eff6a79eea0beafa9abe4e9e4890a5", T_SYN, NULL},
    {"frame3", "f289823ac5d150fc90e534bec86d3a4683bcd7112c46fe683bcf5aae13f327af", T_SYN, NULL},
    {"frame4", "1b68a6599f1fd45b3a8131fb99e8ef65c66ff14c32becc3a63a122c5ba1fbc48", T_SYN, NULL},
    {"menu", "53cecdc53056278abc746715067ee3dbd5de4e2f97b13776c09841a3e746c20c", T_SYN, NULL}};
static const es2_screen_expect expect_pressed[] = {
    {"pressed", "c8411a3cef5cb459cc4861e64b022295ad8c46a3a38a15cb88c0775b6c952721", T_SYN, NULL},
    {"released", "2196ff8f738b04218408913be33aaac7b69e066ce340124558543d7881fcd4f0", T_SYN, NULL}};
static const es2_screen_expect expect_continue[] = {
    {"menu", "6c10134276341e81e2f5a7b062fd7b74c825dbeace05a64a06be818e6ce98740", T_SYN, NULL}};
/* After a handler: the menu as it was (INSTANT ACTION and VIEW DEMO windowed, ONLINE MANUAL,
 * CREDITS after its clear and the root's redraw), what the hidden panel leaves (the screens
 * that follow draw over it), or the clear (QUIT). */
static const es2_screen_expect expect_unchanged[] = {
    {"after", "53cecdc53056278abc746715067ee3dbd5de4e2f97b13776c09841a3e746c20c", T_SYN, NULL}};
static const es2_screen_expect expect_hidden[] = {
    {"after", "2196ff8f738b04218408913be33aaac7b69e066ce340124558543d7881fcd4f0", T_SYN, NULL}};
static const es2_screen_expect expect_cleared[] = {
    {"after", "9c6f112bb2fd2287bca78f727dcb9eeff554b3a5d9ae7b8af90a64aa2755c466", T_SYN, NULL}};
static const es2_screen_expect expect_real_animation[] = {
    {"frame0", "93558b13a223f323b3e601ccabf4f447923c00bb84c199d50c323866b2ee4ff1", T_REAL, NULL},
    {"frame1", "a3f754fa8ef30ee3b22c5590b22ff355e4c656ccb5d6726ce634f2253016d4de", T_REAL, NULL},
    {"frame2", "fc3d474d2867579e91e2f4a3e01685d84a5f8b3b735e1eb29ec5de4d4b214c9d", T_REAL, NULL},
    {"frame3", "4d61aa44486bc39a17f8267541f163edad43c74005be60c9bf305117d780a1de", T_REAL, NULL},
    {"frame4", "8880a17f3d5444ef98dcec7c71506e0696a3dff9a107b6e8a3e2c848cf69af01", T_REAL, NULL},
    {"menu", "3c0b450dc341da0f3a44a363f03eecd096a55132a00e0763e96e4b5082966085", T_REAL, NULL}};
static const es2_screen_expect expect_real_pressed[] = {
    {"pressed", "5f1c9a94d95f0acf96da31be7fe9f3d85473e13cdc5702e2dc0220f945313c40", T_REAL, NULL},
    {"released", "42a3f15d4e2f0d9abd8201aee0e1daced6d3786c883fe18fbf25b1e0dedebda5", T_REAL, NULL}};
static const es2_screen_expect expect_real_continue[] = {
    {"menu", "43295d0aa46c576ba0f5562fc94a094e83613f00c2c7a107e8a647b562d8b09c", T_REAL, "game_r:main_menu:start"}};

#define CASE(name, data, setup_fn, script, expect)                                                             \
    {"main_menu." name, "main_menu", data, sizeof(mm_state), setup_fn, script, expect,                         \
     sizeof(expect) / sizeof((expect)[0]), NULL}

static const es2_screen_case cases[] = {
    CASE("animation", 0, setup_synthetic, script_animation, expect_animation),
    CASE("pressed", 0, setup_synthetic, script_pressed, expect_pressed),
    CASE("continue_enabled", 0, setup_synthetic_slot, script_menu, expect_continue),
    CASE("after_instant_action", 0, setup_synthetic, AFTER("05"), expect_unchanged),
    CASE("after_start_new_game", 0, setup_synthetic, AFTER("07"), expect_hidden),
    CASE("after_continue_game", 0, setup_synthetic_slot, AFTER("09"), expect_hidden),
    CASE("after_save_restore", 0, setup_synthetic, AFTER("11"), expect_hidden),
    CASE("after_quit", 0, setup_synthetic, AFTER("13"), expect_cleared),
    CASE("after_online_manual", 0, setup_synthetic, AFTER("15"), expect_unchanged),
    CASE("after_practice", 0, setup_synthetic, AFTER("17"), expect_hidden),
    CASE("after_preferences", 0, setup_synthetic, AFTER("19"), expect_hidden),
    CASE("after_view_demo", 0, setup_synthetic, AFTER("21"), expect_unchanged),
    CASE("after_credits", 0, setup_synthetic, AFTER("23"), expect_unchanged),
    CASE("real_animation", 1, setup_real, script_animation, expect_real_animation),
    CASE("real_pressed", 1, setup_real, script_pressed, expect_real_pressed),
    CASE("real_continue_enabled", 1, setup_real_slot, script_menu, expect_real_continue),
};

ES2_SCREEN_SUITE(main_menu, cases)

/* Frame history (screen_harness.h). The real continue-enabled menu
 * (real_continue_enabled's frame, = game_r:main_menu:start), then its click
 * on the button that leaves it; the frame as the button's handler leaves it
 * goes into s's shell. */
int es2_screen_main_menu_history(es2_screen_session *s, const char *root, const char *control) {
    es2_screen_session h;
    mm_state *st = calloc(1, sizeof *st);
    char script[96];
    int code;
    if (!st) return 1;
    if (es2_screen_session_open(&h, s->layout, NULL, NULL)) {
        free(st);
        return 1;
    }
    code = setup(&h, st, 1, 1, root);
    if (!code) {
        snprintf(script, sizeof script, "step 30\nclick main_menu.%s\nstep 2\n", control);
        if (es2_screen_run(&h, script)) code = 20;
    }
    if (!code) memcpy(s->shell->frame.pixels, h.shell->frame.pixels, sizeof s->shell->frame.pixels);
    es2_screen_session_close(&h);
    free(st);
    return code;
}
