/* Frame hashes of the Saved games screen 0x438c40 (screen test harness).
 *
 * Synthetic cases (main_menu_fixture.h: a fixed-width font, the seeded image
 * for bay2a_84, a seeded palette; a synthetic slot table with slots 2 and 5
 * used and their statistics) and real cases (needs_data: dfn\font2.dfn,
 * dbm\bay2a_84.dbm, dpl\palette.dpl, ENG\ESTEXT.BIN from the CD's
 * VOL\SHELL0.VOL, SHELL1.VOL and LANG0.VOL; the slot table stays synthetic).
 * States: the screen as the main menu's SAVE/RESTORE shows it, a used row
 * selected (statistics), an empty row selected, SAVE with a loaded game
 * (the row as edit field with a typed name), after ACCEPT, after CANCEL,
 * after EXIT. No capture-tour reference exists yet; the capture states a
 * capture tour needs are listed in vshell_ui.md section 26. */
#include "screen_harness.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "es2/formats/dbm.h"
#include "es2/formats/dpl.h"
#include "es2/formats/vfs.h"
#include "es2/formats/vol.h"
#include "es2/shell/gui/saved_games.h"
#include "main_menu_fixture.h"
#include "menu.h"

#define SG_TEXTS 0x100

typedef struct sg_state {
    es2_ui ui;
    es2_shell_flow flow;
    es2_saved_games screen;
    es2_shell_slot_table table;
    es2_shell_stats stats[ES2_SHELL_STATS_SLOTS];
    int real;
} sg_state;

/* ---- resources ------------------------------------------------------------- */
typedef struct sg_resources {
    int ready;
    es2_dfn font;
    es2_bitmap background;
    uint8_t records[256][4];
    char texts[SG_TEXTS][48];
} sg_resources;
static sg_resources synthetic, real;
static uint8_t synthetic_font[16384];
static uint8_t synthetic_image[640 * 480];
static uint8_t real_scratch[640 * 480];

static int synthetic_load(void) {
    if (synthetic.ready) return 0;
    if (mmf_font(synthetic_font, &synthetic.font)) return 1;
    mmf_image(synthetic_image, 4);
    synthetic.background = (es2_bitmap){480, 640, 0, synthetic_image};
    mmf_palette(synthetic.records);
    for (int id = 0; id < SG_TEXTS; id++) snprintf(synthetic.texts[id], sizeof synthetic.texts[id], "ENTRY %d", id);
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

/* The VOLs stay mapped for the process (the bitmap and the font borrow them). */
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
    for (int id = 0; id < SG_TEXTS; id++) {
        es2_span t;
        if (es2m_bin_raw(c.payload, (size_t)id, &t, NULL) != ES2_OK) continue;
        size_t n = t.size < sizeof real.texts[id] - 1 ? t.size : sizeof real.texts[id] - 1;
        memcpy(real.texts[id], t.data, n);
        real.texts[id][n] = 0;
    }
    if (!real.texts[0x1b][0] || !real.texts[0x1d][0]) return 8; /* the title, SAVE */
    real.ready = 1;
    return 0;
}

static sg_resources *resources_of(void *ctx) { return ((sg_state *)ctx)->real ? &real : &synthetic; }
static const es2_dfn *h_font(void *ctx, uint32_t global) {
    return global == 0x46ddfc || global == 0x46de00 ? &resources_of(ctx)->font : NULL;
}
static const es2_bitmap *h_bitmap(void *ctx, const char *file) {
    return !strcmp(file, es2_menu_background_file(4)) ? &resources_of(ctx)->background : NULL;
}
static const char *h_text(void *ctx, uint32_t id) { return id < SG_TEXTS ? resources_of(ctx)->texts[id] : NULL; }
static int f_save(void *ctx, int slot, const char *text) {
    sg_state *st = ctx;
    if (slot < 10 && text) {
        snprintf(st->table.slot[slot].text, sizeof st->table.slot[slot].text, "%s", text);
        st->table.slot[slot].flag = 1;
    }
    return 1;
}
static void h_refresh(void *ctx, int slot) {
    sg_state *st = ctx;
    st->stats[slot] = st->stats[10]; /* the loaded game is slot 10's record here */
}

static void on_event(void *context, const es2_gui_event *ev) { es2_ui_on_event(&((sg_state *)context)->ui, ev); }
static void on_timer(void *context, es2_gui_shell *shell) {
    sg_state *st = context;
    st->ui.now += 100; /* 100 ms per main-loop pass */
    es2_ui_on_timer(&st->ui, shell);
}

static void put16(es2_shell_stats *r, int at, int v) { r->raw[at] = (uint8_t)v; r->raw[at + 1] = (uint8_t)((unsigned)v >> 8); }

static int setup(es2_screen_session *s, void *state, int real_data, int game, int tour, const char *root) {
    sg_state *st = state;
    es2_shell_flow_host fh;
    int code = real_data ? real_load(root) : synthetic_load();
    if (code) return code;
    if (real_data && es2_screen_main_menu_history(s, root, "11")) return 13; /* SAVE/RESTORE over the menu */
    st->real = real_data;
    if (es2_ui_init(&st->ui, s->shell)) return 10;
    st->ui.context = st;
    for (int k = 0; k < ES2_SHELL_SLOT_COUNT; k++) {
        snprintf(st->table.slot[k].file, sizeof st->table.slot[k].file, "GAME_%d.SAV", k);
        snprintf(st->table.slot[k].text, sizeof st->table.slot[k].text, "%2d. EMPTY", k + 1);
    }
    snprintf(st->table.slot[1].text, sizeof st->table.slot[1].text, " 2. ALPHA ONE");
    st->table.slot[1].flag = 1;
    snprintf(st->table.slot[4].text, sizeof st->table.slot[4].text, " 5. BRAVO");
    st->table.slot[4].flag = 1;
    if (tour) {
        /* The slot rows of the tour's clone (calibration-r111 SAV\GAMEFILE.STR,
         * unchanged by the tour): two used slots, the others named LEER */
        for (int k = 0; k < 10; k++) {
            snprintf(st->table.slot[k].text, sizeof st->table.slot[k].text, "%2d. LEER", k + 1);
            st->table.slot[k].flag = 0;
        }
        snprintf(st->table.slot[0].text, sizeof st->table.slot[0].text, " 1. S1");
        snprintf(st->table.slot[1].text, sizeof st->table.slot[1].text, " 2. Z1");
        st->table.slot[0].flag = st->table.slot[1].flag = 1;
    }
    memcpy(st->stats[1].raw + 4, "MAVERICK", 9);
    put16(&st->stats[1], 0x25, 2);
    put16(&st->stats[1], 0x29, 3);
    for (int i = 0; i < 6; i++) put16(&st->stats[1], 0x2d + 2 * i, 3 * i + 1);
    st->stats[1].raw[0x3b] = 0x40; st->stats[1].raw[0x3c] = 0xe2; st->stats[1].raw[0x3d] = 0x01;
    put16(&st->stats[1], 0x3f, 4);
    put16(&st->stats[1], 0x41, 6);
    memcpy(st->stats[10].raw + 4, "ICEMAN", 7);
    put16(&st->stats[10], 0x41, 2);
    memset(&fh, 0, sizeof fh);
    fh.ctx = st;
    fh.save = f_save;
    es2_shell_flow_init(&st->flow, &fh, NULL);
    st->flow.mode = 1;
    if (game) { st->flow.game = ES2_FLOW_GAME_CAMPAIGN; st->flow.final_word = 2; }
    st->screen.flow = &st->flow;
    st->screen.table = &st->table;
    st->screen.stats = st->stats;
    st->screen.host = (es2_saved_games_host){st, h_font, h_bitmap, h_text, h_refresh, NULL};
    if (es2_screen_saved_games_build(&st->ui, &st->screen)) return 11;
    es2_ui_show(&st->ui, st->ui.root);
    s->screen.context = st;
    s->screen.handle = on_event;
    s->screen.timer = on_timer;
    s->shell->frame.realize_gate = 1; /* as the main menu's first appearance leaves it */
    es2_gui_palette_select(&s->shell->frame, resources_of(st)->records); /* the main menu's palette 1 */
    es2_gui_palette_apply(&s->shell->frame);
    if (es2_shell_flow_event(&st->flow, ES2_FLOW_SAVE_RESTORE, NULL) != ES2_FLOW_DONE) return 12;
    es2_saved_games_show(&st->screen);
    return 0;
}
static int setup_synthetic(es2_screen_session *s, void *state, const char *root) { return setup(s, state, 0, 0, 0, root); }
static int setup_synthetic_game(es2_screen_session *s, void *state, const char *root) { return setup(s, state, 0, 1, 0, root); }
static int setup_real(es2_screen_session *s, void *state, const char *root) { return setup(s, state, 1, 0, 0, root); }
static int setup_real_tour(es2_screen_session *s, void *state, const char *root) { return setup(s, state, 1, 0, 1, root); }
static int setup_real_game(es2_screen_session *s, void *state, const char *root) { return setup(s, state, 1, 1, 0, root); }

/* ---- scripts ---------------------------------------------------------------- */
static const char script_shown[] = "step 2\nsnap shown\n";
static const char script_select[] =
    "step 2\nclick saved_games.05\nstep 2\nsnap used\nclick saved_games.09\nstep 2\nsnap empty\n";
static const char script_naming[] =
    "step 2\nclick saved_games.07\nstep 2\nsnap selected\nclick saved_games.18\nstep 2\nsnap naming\n"
    "type ACE 7\nstep 2\nsnap typed\nclick saved_games.16\nstep 2\nsnap accepted\n";
static const char script_cancel[] =
    "step 2\nclick saved_games.07\nstep 2\nclick saved_games.18\ntype ZULU\nclick saved_games.14\nstep 2\nsnap cancelled\n";
static const char script_exit[] = "step 2\nclick saved_games.22\nstep 2\nsnap after_exit\n";

/* ---- expectations (our hashes; reference NULL until a capture tour)
 * -------------- */
#define T_SYN "98fd384cc75016998f60c80a25cbc264969c4900c6962998d4e01d91a1a9384c"
#define T_REAL "96b549df7c20ad4f6b89e13333204a31f25ffedaf43cbf4bcc330bef14f4042a"
static const es2_screen_expect expect_shown[] = {
    {"shown", "c0a13b33c4213121196fc44c7634059721a81fc0f20209b1911923ed3b07649b", T_SYN, NULL}};
/* with a game: slot 10's statistics; CANCEL ends in the same picture */
static const es2_screen_expect expect_shown_game[] = {
    {"shown", "cc474b864479977d249425a34b78d358ba8e7be5a68f3a6315fb71721afae618", T_SYN, NULL}};
static const es2_screen_expect expect_select[] = {
    {"used", "1fc174a1f6a5440abc8dee7904dbba149c232b1a2af76cbc65f112760fcc4fdb", T_SYN, NULL},
    {"empty", "b2ba994869a60f793163ecc79cbc2e16376804d2b94e717b679eede1d0f95b6a", T_SYN, NULL}};
static const es2_screen_expect expect_naming[] = {
    {"selected", "333ee1ef9a3886d04da7855b05e3564b4411aa45ae663060358a3e734117a031", T_SYN, NULL},
    {"naming", "841a0fe90190f948d26d6816233d85ee3b02eb3f219d387d9da08a2b9e1c08ef", T_SYN, NULL},
    {"typed", "c03c0eaca75c3b2cf577880081bf638f800f104054bb821e6c4cfb1914c72072", T_SYN, NULL},
    {"accepted", "a3fb653ffdb8a7acf0f6d75eb3927c139b10839b4c9f8b385461192d092c566f", T_SYN, NULL}};
static const es2_screen_expect expect_cancel[] = {
    {"cancelled", "cc474b864479977d249425a34b78d358ba8e7be5a68f3a6315fb71721afae618", T_SYN, NULL}};
static const es2_screen_expect expect_exit[] = {
    {"after_exit", "5bfb8940131174a14b1182fe0437d034cb59b64d8d8dfdee8060efa80bc18896", T_SYN, NULL}};
static const es2_screen_expect expect_real_shown[] = {
    {"shown", "fd2941d10e6a6514899eb2e19af034283fd29621cc4f652e940d4170e30b5681", T_REAL, "game_r:saved_games:from_main_menu"}};
static const es2_screen_expect expect_real_select[] = {
    {"used", "fa52467b1e7e353e199b8264840711e5dc4ad8bd59e3e7b7bb684472ee15de7f", T_REAL, NULL},
    {"empty", "2054447792dc476ba1b9da9fe2639186422e15434d24d576b5695db5f88ad6a7", T_REAL, NULL}};
static const es2_screen_expect expect_real_naming[] = {
    {"selected", "3fd754f1ecf92560796f4279cd483d1a30fe540901eaf674798f45f3353c3ba9", T_REAL, NULL},
    {"naming", "8c4293820ab543b6ad067b982e76feab39546190f94b79ddc54af7b6229642db", T_REAL, NULL},
    {"typed", "16d30d0ee1d71952c02e58dab2618e16a436e9dc8a0a7141616e0734755eae17", T_REAL, NULL},
    {"accepted", "1f0dcefbd23ea8d871508f6d409aedb8dcd8308c6b0bf4e754c786a697b109cd", T_REAL, NULL}};

#define CASE(name, data, setup_fn, script, expect)                                                             \
    {"saved_games." name, "saved_games", data, sizeof(sg_state), setup_fn, script, expect,                     \
     sizeof(expect) / sizeof((expect)[0]), NULL}

static const es2_screen_case cases[] = {
    CASE("shown", 0, setup_synthetic, script_shown, expect_shown),
    CASE("shown_game", 0, setup_synthetic_game, script_shown, expect_shown_game),
    CASE("select", 0, setup_synthetic, script_select, expect_select),
    CASE("naming", 0, setup_synthetic_game, script_naming, expect_naming),
    CASE("cancel", 0, setup_synthetic_game, script_cancel, expect_cancel),
    CASE("exit", 0, setup_synthetic, script_exit, expect_exit),
    CASE("real_shown", 1, setup_real_tour, script_shown, expect_real_shown),
    CASE("real_select", 1, setup_real, script_select, expect_real_select),
    CASE("real_naming", 1, setup_real_game, script_naming, expect_real_naming),
};

ES2_SCREEN_SUITE(saved_games, cases)
