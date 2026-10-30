/* Frame hashes of the practice screen 0x44b310 (screen test harness).
 *
 * Synthetic cases (main_menu_fixture.h: a fixed-width font, a seeded image
 * for bay2a_84, a seeded palette, a synthetic executable table) and real
 * cases (needs_data: dfn\font2.dfn, dbm\bay2a_84.dbm, dpl\palette.dpl,
 * ENG\ESTEXT.BIN from the CD's VOL\SHELL0.VOL, SHELL1.VOL and LANG0.VOL; the
 * Herc type table and the literals from the hash-checked VER95\VSHELL.EXE at
 * run time). States: the screen as PRACTICE MISSIONS shows it (entry 0), an
 * entry with Herc Type enabled, a parameter button held down, each parameter
 * after a left and a right release, Herc Type clicked while disabled, and
 * what Main Menu and Begin Mission leave. No capture-tour reference exists yet;
 * the capture states a capture tour needs are listed in vshell_ui.md section
 * 25. */
#include "screen_harness.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "es2/formats/dbm.h"
#include "es2/formats/dpl.h"
#include "es2/formats/vfs.h"
#include "es2/formats/vol.h"
#include "es2/shell/gui/practice_screen.h"
#include "main_menu_fixture.h"
#include "menu.h"
#include "es2/formats/sha256.h"
#include "vshell_file.h"

#define VSHELL_SHA256 "1534aa8f732bdecd826484d893ad58b8a5e6ebd4d25ab3158a2469b8f69ba16e"

typedef struct pr_state {
    es2_ui ui;
    es2_shell_flow flow;
    es2_shell_practice practice;
    es2_main_menu menu;
    es2_practice_screen screen;
    int real;
} pr_state;

/* ---- resources ------------------------------------------------------------- */
typedef struct pr_resources {
    int ready;
    es2_dfn font;
    es2_bitmap background;
    uint8_t records[256][4];
    uint8_t tables[ES2_SHELL_PRACTICE_TABLES_SIZE];
} pr_resources;
static pr_resources synthetic, real;
static uint8_t synthetic_font[16384];
static uint8_t synthetic_image[640 * 480];
static uint8_t real_scratch[640 * 480];
static es2_span real_estext;
static uint8_t *real_exe;
static size_t real_exe_size;

static int synthetic_load(void) {
    if (synthetic.ready) return 0;
    if (mmf_font(synthetic_font, &synthetic.font)) return 1;
    mmf_image(synthetic_image, 4);
    synthetic.background = (es2_bitmap){480, 640, 0, synthetic_image};
    mmf_palette(synthetic.records);
    /* not VSHELL's values: a synthetic Herc type per position and two names */
    for (int i = 0; i < 12; i++) synthetic.tables[2 * i] = (uint8_t)((i * 5 + 3) % 9);
    memcpy(synthetic.tables + 24, "SYNTH", 6);
    memcpy(synthetic.tables + 32, "QUICK", 6);
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

/* The VOLs stay mapped for the process (the bitmap, the font and the texts borrow them). */
static es2_vol vols[3];
static es2_vfs_source sources[3];
static es2_vfs vfs;
static int real_load(const char *root) {
    static const char *const names[3] = {"SHELL0.VOL", "SHELL1.VOL", "LANG0.VOL"};
    es2_vfs_copy c;
    char path[1024], hash[65];
    if (real.ready) return real.ready < 0 ? 2 : 0;
    real.ready = -1;
    for (int i = 0; i < 3; i++) {
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
            es2_draw_bitmap_from_dbm(&dbm, real_scratch, sizeof real_scratch, &real.background) != ES2_DRAW_DONE)
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
    real_estext = c.payload;
    snprintf(path, sizeof path, "%s/VER95/VSHELL.EXE", root);
    if (!(real_exe = slurp(path, &real_exe_size))) return 8;
    es2f_sha256_of(real_exe, real_exe_size, hash);
    if (strcmp(hash, VSHELL_SHA256)) return 9;
    es2_test_vshell_file_install();   /* the file through the tests' own section walk */
    if (es2_shell_practice_tables((es2_span){real_exe, real_exe_size}, real.tables, NULL) != ES2_OK) return 10;
    real.ready = 1;
    return 0;
}

static int is_real(void *ctx) { return ((pr_state *)ctx)->real; }
static pr_resources *resources_of(void *ctx) { return is_real(ctx) ? &real : &synthetic; }
static const es2_dfn *h_font(void *ctx, uint32_t global) {
    return global == 0x46ddfc || global == 0x46de00 ? &resources_of(ctx)->font : NULL;
}
static const es2_bitmap *h_bitmap(void *ctx, const char *file) {
    return !strcmp(file, es2_menu_background_file(4)) ? &resources_of(ctx)->background : NULL;
}
static const char *h_text(void *ctx, uint32_t id) {
    static char texts[64][24];
    static unsigned next;
    char *t = texts[next++ & 63];
    if (is_real(ctx)) {
        es2_span s;
        size_t n;
        if (es2m_bin_raw(real_estext, id, &s, NULL) != ES2_OK) return NULL;
        n = s.size < sizeof texts[0] - 1 ? s.size : sizeof texts[0] - 1;
        memcpy(t, s.data, n);
        t[n] = 0;
        return t;
    }
    snprintf(t, sizeof texts[0], "TEXT %X", (unsigned)id);
    return t;
}
static const char *h_literal(void *ctx, uint32_t address) {
    es2_span s;
    if (!is_real(ctx)) return address == 0x479c31 || address == 0x479c35 ? ":" : "";
    if (es2_shell_exe_span((es2_span){real_exe, real_exe_size}, address, 2, &s, NULL) != ES2_OK ||
        !memchr(s.data, 0, s.size))
        return NULL;
    return (const char *)s.data;
}
static int h_launch(void *ctx, const es2_shell_practice_launch *launch) { (void)ctx; (void)launch; return 1; }

static void on_event(void *context, const es2_gui_event *ev) { es2_ui_on_event(&((pr_state *)context)->ui, ev); }
static void on_timer(void *context, es2_gui_shell *shell) {
    pr_state *st = context;
    st->ui.now += 100;
    es2_ui_on_timer(&st->ui, shell);
}

static int setup(es2_screen_session *s, void *state, int real_data, const char *root) {
    pr_state *st = state;
    es2_shell_flow_host fh;
    es2_gui_rect all = {0, 0, ES2_GUI_WIDTH - 1, ES2_GUI_HEIGHT - 1};
    uint8_t prefs[ES2_SHELL_PREFS_SIZE];
    es2_span file = {prefs, sizeof prefs};
    pr_resources *r;
    int code = real_data ? real_load(root) : synthetic_load();
    if (code) return code;
    st->real = real_data;
    r = real_data ? &real : &synthetic;
    if (es2_ui_init(&st->ui, s->shell)) return 20;
    st->ui.context = st;
    memset(&fh, 0, sizeof fh);
    memset(prefs, 0, sizeof prefs);
    prefs[0x27] = 1;            /* Mission Difficulty: the second label */
    prefs[ES2_SHELL_PREFS_MODE] = 1;
    es2_shell_flow_init(&st->flow, &fh, &file);
    es2_shell_practice_open(&st->practice);
    st->menu.flow = &st->flow;
    st->menu.host = (es2_main_menu_host){st, h_font, h_bitmap, h_text, NULL, NULL, NULL, NULL, NULL};
    if (es2_screen_main_menu_build(&st->ui, &st->menu)) return 21;
    st->screen.flow = &st->flow;
    st->screen.practice = &st->practice;
    st->screen.tables = r->tables;
    st->screen.menu = &st->menu;
    st->screen.host = (es2_practice_screen_host){st, h_font, h_bitmap, h_text, h_literal, h_launch, NULL};
    if (es2_screen_practice_build(&st->ui, &st->screen)) return 22;
    es2_ui_show(&st->ui, st->ui.root);
    /* the main menu's palette and the cleared frame, then PRACTICE MISSIONS 0x431f3a */
    memset(s->shell->frame.pixels, 0, ES2_GUI_PIXELS);
    s->shell->frame.realize_gate = 1;
    es2_gui_palette_select(&s->shell->frame, (const uint8_t(*)[4])r->records);
    es2_gui_palette_apply(&s->shell->frame);
    es2_gui_invalidate_window(&s->shell->frame, all);
    s->shell->input.interactive = 1;
    es2_practice_screen_show(&st->screen);
    if (es2_shell_flow_event(&st->flow, ES2_FLOW_PRACTICE_MISSIONS, NULL) != ES2_FLOW_DONE) return 23;
    s->screen.context = st;
    s->screen.handle = on_event;
    s->screen.timer = on_timer;
    return 0;
}
static int setup_synthetic(es2_screen_session *s, void *state, const char *root) { return setup(s, state, 0, root); }
static int setup_real(es2_screen_session *s, void *state, const char *root) { return setup(s, state, 1, root); }

/* ---- scripts ---------------------------------------------------------------- */
#define CLICK(control) "step 2\nclick practice." control "\nstep 2\nsnap after\n"
#define RCLICK(control) "step 2\nclick practice." control " right\nstep 2\nsnap after\n"
static const char script_shown[] = "step 2\nsnap shown\n";
/* Begin Mission's button held down (two passes after the press), then released over it. */
static const char script_pressed[] = "step 2\nmove 428 350\nstep\nldown\nstep 2\nsnap pressed\nlup\nstep 2\nsnap released\n";

/* ---- expectations (our hashes; reference NULL until a capture tour)
 * -------------- */
#define T_SYN "98fd384cc75016998f60c80a25cbc264969c4900c6962998d4e01d91a1a9384c"
#define T_REAL "96b549df7c20ad4f6b89e13333204a31f25ffedaf43cbf4bcc330bef14f4042a"
/* The screen after the show; Herc Type clicked while disabled, Begin Mission (which hides
 * nothing; the launch is the host's) and the released button leave it as it was. */
#define F_SHOWN "03866972b90b681d08b4a786d1c005b61fb066b945a47a33dd505a1db39db989"
static const es2_screen_expect expect_shown[] = {{"shown", F_SHOWN, T_SYN, NULL}};
static const es2_screen_expect expect_unchanged[] = {{"after", F_SHOWN, T_SYN, NULL}};
static const es2_screen_expect expect_pressed[] = {
    {"pressed", "89490cfe72174cab5204de9d60921674a54c416aa64da163dff9130ea9433613", T_SYN, NULL},
    {"released", F_SHOWN, T_SYN, NULL}};
#define AFTER(name, frame) static const es2_screen_expect name[] = {{"after", frame, T_SYN, NULL}};
AFTER(expect_entry_strike, "f594a79348e7d42d74430307619a63a1cea61d86ae08b2d31823a29b3a866bd1")
AFTER(expect_damage_left, "19fe76eb57e0d57703a32fd951cb1423f12abe505336042fb9f2df1ff094f047")
AFTER(expect_ammo_right, "f946c0fc0a9955fd4033095e9014b28aa898f70bcc4530bcc212029eae1ed3ad")
AFTER(expect_difficulty_left, "fd76861a2d37702628b634f26017ebe45167501265b6818b824ee8b60dab7225")
AFTER(expect_difficulty_right, "3e2e727c73f865eb46d1371cb686a84a24d08ce4f44ec11bb82959a58666b0dc")
AFTER(expect_time_left, "f48cc6c6eefe54e7e4e2abd0397d3a764cbb72f7c5e92fd9ef33507e91ce5c33")
/* Main Menu: the screen hidden and the main menu's panel shown (0x431718) */
AFTER(expect_main_menu, "5f1ca290db1d1096be8a82007c91d804c1c4fc4318639eb294e4cb3cc527b86b")
static const es2_screen_expect expect_real_shown[] = {
    {"shown", "a1bb819fab77b17c7113fa5ea8f4b05fc8c21c1e4b01ad44fb63521e690d4184", T_REAL, NULL}};
#define REAL_AFTER(name, frame) static const es2_screen_expect name[] = {{"after", frame, T_REAL, NULL}};
REAL_AFTER(expect_real_entry_strike, "21e7f6bdf90d8406364aa4d8f40cdc1d231025403b41343184c5e3138a58cbcf")
REAL_AFTER(expect_real_damage_left, "0e765bc33ab862e66caf0f7fc037ed0f132206c9e91aef2433606dcf2361e4a7")
REAL_AFTER(expect_real_difficulty_right, "c459d7d08d22d2026f5d506f4864f95389a5b56e2dbbe776748c513ce8d23f08")

#define CASE(name, data, setup_fn, script, expect)                                                             \
    {"practice." name, "practice", data, sizeof(pr_state), setup_fn, script, expect,                           \
     sizeof(expect) / sizeof((expect)[0]), NULL}

static const es2_screen_case cases[] = {
    CASE("shown", 0, setup_synthetic, script_shown, expect_shown),
    CASE("pressed", 0, setup_synthetic, script_pressed, expect_pressed),
    CASE("entry_strike", 0, setup_synthetic, CLICK("56"), expect_entry_strike),
    CASE("herc_disabled", 0, setup_synthetic, CLICK("13"), expect_unchanged),
    CASE("damage_left", 0, setup_synthetic, CLICK("05"), expect_damage_left),
    CASE("ammo_right", 0, setup_synthetic, RCLICK("07"), expect_ammo_right),
    CASE("difficulty_left", 0, setup_synthetic, CLICK("09"), expect_difficulty_left),
    CASE("difficulty_right", 0, setup_synthetic, RCLICK("09"), expect_difficulty_right),
    CASE("time_left", 0, setup_synthetic, CLICK("11"), expect_time_left),
    CASE("after_main_menu", 0, setup_synthetic, CLICK("31"), expect_main_menu),
    CASE("after_begin_mission", 0, setup_synthetic, CLICK("33"), expect_unchanged),
    CASE("real_shown", 1, setup_real, script_shown, expect_real_shown),
    CASE("real_entry_strike", 1, setup_real, CLICK("56"), expect_real_entry_strike),
    CASE("real_damage_left", 1, setup_real, CLICK("05"), expect_real_damage_left),
    CASE("real_difficulty_right", 1, setup_real, RCLICK("09"), expect_real_difficulty_right),
};

ES2_SCREEN_SUITE(practice, cases)
