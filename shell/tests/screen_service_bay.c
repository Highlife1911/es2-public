/* Screen cases of the service bay and the squad panel
 * (es2/shell/gui/service_bay.h) on the screen test harness.
 *
 * Synthetic cases: main_menu_fixture.h's font, seeded images for the bay
 * background and the button members, a seeded palette per palette index,
 * placeholder texts ("T13" for ESTEXT 0x13, ...), eight synthetic bays and
 * synthetic band tables. The real case reads dfn\black.dfn, dfn\font2.dfn,
 * dba\mnu_bttn.dba, dba\online.dba, dbm\bay2a_84.dbm, dpl\palette.dpl,
 * dpl\arming.dpl and ENG\ESTEXT.BIN from the CD's VOL archives (the bays
 * stay synthetic). The main loop's step 5 is the harness's movie hook, so
 * the bar acts from the second pass on, as VSHELL's 0x46c280. VSHELL
 * references do not exist yet: the expectations are our own hashes;
 * vshell_ui.md section 28 lists the states a capture tour must capture. */
#include "screen_harness.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "es2/formats/dbm.h"
#include "es2/formats/dpl.h"
#include "es2/formats/vfs.h"
#include "es2/formats/vol.h"
#include "es2/shell/gui/service_bay.h"
#include "main_menu_fixture.h"
#include "menu.h"

typedef struct sb_state {
    es2_ui ui;
    es2_shell_flow flow;
    es2_service_bay bay;
    int real;
    int no_loop;          /* keep 0x46c280 at 0 */
    char text[64];
    /* what the hooks saw */
    int sounds, saves, rebalances, manuals, panels;
    char log[512];        /* screen and selection hooks in order */
    int word_at_show[8];  /* the screen word 0x4758b8 when a show ran */
} sb_state;

typedef struct sb_resources {
    int ready;
    es2_dfn font, font2;  /* [0x46de04] dfn\black, [0x46de00] dfn\font2 (synthetic: one font) */
    es2_bitmap background;
    es2_bitmap button[4];  /* mnu_bttn 1, 2, 3; unused 0 */
    es2_bitmap online[2];
    uint8_t records[20][256][4];
    int has_records[20];
    es2_span estext;
} sb_resources;

static sb_resources synthetic, real;

/* ---- the eight synthetic bays ---------------------------------------------------- */
/* 0: finished, pilot; 1: finished, no pilot; 2: under construction; 3: empty;
 * 4: finished vehicle 8 with a pilot; 5..7: finished, falling conditions */
static int bay_view(void *ctx, int k, es2_squad_bay *out) {
    static const char *const herc[8] = {"OUTLAW", "RAZOR", "APOC", "", "PITBULL", "BADGER", "AVATAR", "EMPEROR"};
    static const char *const pilot[8] = {"ACE", NULL, NULL, NULL, "STONE", "CHEN", NULL, "MOREAU"};
    static const int16_t condition[8] = {100, 85, 100, 0, 72, 55, 20, 0};
    (void)ctx;
    if (k == 3) return 0;
    out->record = 1;
    out->finished = k != 2;
    out->vehicle = k == 4 ? 8 : (int16_t)k;
    out->herc = herc[k];
    out->condition = condition[k];
    out->progress = k == 2 ? 40 : 0;
    out->pilot = pilot[k];
    out->skill = (int16_t)(k % 4);
    return 1;
}

/* ---- host -------------------------------------------------------------------------- */
static sb_resources *res(void *ctx) { return ((sb_state *)ctx)->real ? &real : &synthetic; }
static void logf_(sb_state *st, const char *s) {
    size_t n = strlen(st->log);
    snprintf(st->log + n, sizeof st->log - n, "%s ", s);
}
static const es2_dfn *h_font(void *ctx, uint32_t global) {
    return global == 0x46de04 ? &res(ctx)->font : global == 0x46de00 ? &res(ctx)->font2 : NULL;
}
static const es2_bitmap *h_bitmap(void *ctx, const char *file, int frame) {
    sb_resources *r = res(ctx);
    if (frame < 0) return &r->background;
    if (!strcmp(file, "dba\\mnu_bttn.dba") && frame >= 1 && frame <= 3) return &r->button[frame];
    if (!strcmp(file, "dba\\online.dba") && frame >= 0 && frame <= 1) return &r->online[frame];
    return NULL;
}
static const char *h_text(void *ctx, uint32_t id) {
    sb_state *st = ctx;
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
static void h_sound(void *ctx, uint32_t address) {
    if (address == ES2_SERVICE_BAY_SOUND) ((sb_state *)ctx)->sounds++;
}
static void h_career(void *ctx, int16_t *group, int16_t *position, int *shown) {
    (void)ctx;
    *group = 2;
    *position = 0;
    *shown = 0;
}
static void h_screen(void *ctx, int word, int show) {
    char s[16];
    sb_state *st = ctx;
    if (show && word >= 0 && word < 8) st->word_at_show[word] = st->bay.session.screen;
    snprintf(s, sizeof s, "%c%d", show ? 'S' : 'L', word);
    logf_(ctx, s);
}
static void h_panel(void *ctx, int word, int load) {
    char s[16];
    ((sb_state *)ctx)->panels++;
    snprintf(s, sizeof s, "%c%d", load ? 'P' : 'U', word);
    logf_(ctx, s);
}
static void h_selected(void *ctx, int word, int16_t k, int assign) {
    char s[24];
    snprintf(s, sizeof s, "E%d:%d:%d", word, k, assign);
    logf_(ctx, s);
}
static void palette(es2_ui *ui, uint16_t word) {
    sb_resources *r = res(ui->context);
    if (word < 20 && r->has_records[word]) es2_gui_palette_select(&ui->shell->frame, (const uint8_t(*)[4])r->records[word]);
    es2_gui_palette_apply(&ui->shell->frame);
}
/* flow host */
static int f_save(void *ctx, int slot, const char *t) { (void)slot; (void)t; ((sb_state *)ctx)->saves++; return 1; }
static int f_rebalance(void *ctx, uint8_t b) { (void)b; ((sb_state *)ctx)->rebalances++; return 1; }
static int f_prefs(void *ctx, const uint8_t block[ES2_SHELL_PREFS_SIZE]) { (void)ctx; (void)block; return 1; }
static void f_manual(void *ctx) { ((sb_state *)ctx)->manuals++; }

/* ---- resources ------------------------------------------------------------------------ */
static uint8_t synthetic_font_bytes[8 + 26 + MMF_COUNT * (MMF_W * MMF_H + 4) + 2];
static uint8_t synthetic_pixels[640 * 480], synthetic_buttons[4][75 * 24], synthetic_online[2][17 * 24];
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

/* ---- the presenter --------------------------------------------------------------------- */
static void present(void *context, const es2_gui_event *ev) { es2_ui_on_event(&((sb_state *)context)->ui, ev); }
static void timer(void *context, es2_gui_shell *shell) { es2_ui_on_timer(&((sb_state *)context)->ui, shell); }
static void movie(void *context, es2_gui_shell *shell) {
    sb_state *st = context;
    (void)shell;
    if (!st->no_loop) st->bay.loop_started = 1; /* 0x4020b8 */
}

/* The bay as Registration ACCEPT leaves it: a campaign, mode 1, word -1. */
static int open_bay(es2_screen_session *s, sb_state *st) {
    es2_shell_flow_host fh;
    es2_service_bay_host *h = &st->bay.host;
    static const int16_t limits[5] = {89, 79, 59, 29, 0};       /* synthetic band limits */
    static const int16_t colours[6] = {41, 42, 43, 44, 45, 39}; /* synthetic band colours */
    memset(&fh, 0, sizeof fh);
    fh.ctx = st;
    fh.save = f_save;
    fh.rebalance = f_rebalance;
    fh.write_prefs = f_prefs;
    fh.online_manual = f_manual;
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
    h->sound = h_sound;
    h->career = h_career;
    h->screen = h_screen;
    h->bay = bay_view;
    h->panel = h_panel;
    h->selected = h_selected;
    memcpy(st->bay.band_limit, limits, sizeof limits);
    memcpy(st->bay.band_colour, colours, sizeof colours);
    if (es2_screen_service_bay_build(&st->ui, &st->bay) || es2_screen_squad_panel_build(&st->ui, &st->bay)) return 3;
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
static int setup_mode0(es2_screen_session *s, void *state, const char *data_root) {
    sb_state *st = state;
    int r = setup(s, state, data_root);
    if (r) return r;
    st->flow.mode = 0;
    es2_service_bay_show(&st->bay); /* the enable rule again with mode 0 */
    return 0;
}
static int setup_real(es2_screen_session *s, void *state, const char *data_root) {
    sb_state *st = state;
    int r;
    if (!data_root) return 1;
    if ((r = real_load(data_root))) return 10 + r;
    if (es2_screen_main_menu_history(s, data_root, "09")) return 20; /* CONTINUE GAME over the menu */
    st->real = 1;
    return open_bay(s, state);
}

static const char *label_text(sb_state *st, int control) {
    es2_ui_widget *w = es2_ui_get(&st->ui, st->bay.squad[control]);
    return w ? w->text : "";
}
static int label_colour(sb_state *st, int control) {
    es2_ui_widget *w = es2_ui_get(&st->ui, st->bay.squad[control]);
    return w ? w->text_colour : -1;
}
static es2_ui_widget *row_of(sb_state *st, int k) { return es2_ui_get(&st->ui, st->bay.squad[ES2_SQUAD_ROW + 5 * k]); }
static es2_ui_widget *column(sb_state *st, int k, int c) { return es2_ui_get(&st->ui, row_of(st, k)->columns[c]); }
static int hidden(sb_state *st, es2_ui_id id) { return (es2_ui_get(&st->ui, id)->flags & ES2_UI_HIDDEN) != 0; }

/* Behaviour through the screen (no hashes): a failed check returns its number. */
static int setup_behaviour(es2_screen_session *s, void *state, const char *data_root) {
    sb_state *st = state;
    es2_service_bay *b = &st->bay;
    int r = setup(s, state, data_root);
    if (r) return r;
    /* built and shown: word -1, selection 0, all five enabled in mode 1,
     * ARMORY built disabled; the squad panel hidden */
    if (b->session.screen != -1 || b->session.bay != 0) return 10;
    for (int k = ES2_BAY_WEAPONS; k <= ES2_BAY_CREW; k += 2)
        if (!es2_ui_get(&st->ui, b->bay[k])->enabled) return 11;
    if (!hidden(st, b->squad[ES2_SQUAD_INVENTORY])) return 12;
    /* before the main loop's step 5 has run (0x46c280 = 0) WEAPONS does nothing */
    st->no_loop = 1;
    if (es2_screen_run(s, "click service_bay.09\n") || b->session.screen != -1 || st->sounds || st->flow.bay_word != -1)
        return 13;
    st->no_loop = 0;
    if (es2_screen_run(s, "step\n") || !b->loop_started) return 14;
    /* WEAPONS: pressed, word 2, the panel shown with selection 0 (P2), the
     * show S2, bptlt2 */
    st->log[0] = 0;
    if (es2_screen_run(s, "click service_bay.09\n")) return 15;
    if (b->session.screen != 2 || st->flow.bay_word != 2 || st->sounds != 1 || strcmp(st->log, "P2 S2 ")) return 16;
    /* (the release clears +0x45 without a redraw: the button stays drawn pressed) */
    if (hidden(st, b->squad[ES2_SQUAD_GRID + 0]) || hidden(st, b->squad[ES2_SQUAD_INVENTORY])) return 17;
    if (row_of(st, 0)->colour != 0x29 || row_of(st, 1)->colour != 0x10) return 18;
    /* labels of bay 0: pilot ACE, skill T35, condition band 0 (100 > 89): T68 in colour 41 */
    if (strcmp(label_text(st, ES2_SQUAD_PILOT), "ACE") || strcmp(label_text(st, ES2_SQUAD_SKILL), "T35")) return 19;
    if (strcmp(label_text(st, ES2_SQUAD_CONDITION), "T68") || label_colour(st, ES2_SQUAD_CONDITION) != 41) return 20;
    /* rows: Herc names in the band colour, pilots in 0x27; bay 2 shows its
     * construction ("40T6C"); bay 3 is empty (ESTEXT 0 as pilot) */
    if (strcmp(column(st, 5, 1)->text, "BADGER") || column(st, 5, 1)->text_colour != 44) return 21;
    if (strcmp(column(st, 2, 3)->text, "40T6C") || column(st, 3, 3)->text[0] != 'T' || column(st, 0, 3)->text_colour != 0x27)
        return 22;
    if (strcmp(column(st, 0, 0)->text, "1.") || strcmp(column(st, 7, 0)->text, "8.") || strcmp(column(st, 4, 2)->text, "-"))
        return 23;
    /* WEAPONS again: nothing */
    if (es2_screen_run(s, "click service_bay.09\n") || st->sounds != 1) return 24;
    /* WEAPONS selection: the unfinished bay 2 and the empty bay 3 are refused,
     * bay 1 is taken (the editor's part first) */
    st->log[0] = 0;
    if (es2_screen_run(s, "click squad_panel.27\nclick squad_panel.32\n") || b->session.bay != 0 || st->log[0]) return 25;
    if (es2_screen_run(s, "click squad_panel.22\n") || b->session.bay != 1 || strcmp(st->log, "E2:1:1 ")) return 26;
    if (row_of(st, 1)->colour != 0x29 || row_of(st, 0)->colour != 0x10 || !hidden(st, b->squad[ES2_SQUAD_GRID + 0]))
        return 27;
    /* bay 1 has no pilot: ESTEXT 0x6d, skill ESTEXT 0; band 1 (85 > 79) */
    if (strcmp(label_text(st, ES2_SQUAD_PILOT), "T6D") || strcmp(label_text(st, ES2_SQUAD_SKILL), "T00")) return 28;
    if (strcmp(label_text(st, ES2_SQUAD_CONDITION), "T69") || label_colour(st, ES2_SQUAD_CONDITION) != 42) return 29;
    /* BUILD: leave WEAPONS (U2 L2, the flow's rebalance), panel, show; any bay */
    st->log[0] = 0;
    if (es2_screen_run(s, "click service_bay.13\n") || st->rebalances != 1 || strcmp(st->log, "U2 L2 P4 S4 ")) return 30;
    st->log[0] = 0;
    if (es2_screen_run(s, "click squad_panel.32\n") || b->session.bay != 3 || strcmp(st->log, "E4:3:1 ")) return 31;
    /* an empty bay: ESTEXT 0 as condition in band 0's colour (100), pilot ESTEXT 0 */
    if (strcmp(label_text(st, ES2_SQUAD_CONDITION), "T00") || label_colour(st, ES2_SQUAD_CONDITION) != 41 ||
        strcmp(label_text(st, ES2_SQUAD_PILOT), "T00"))
        return 32;
    if (es2_screen_run(s, "click squad_panel.27\n") || strcmp(label_text(st, ES2_SQUAD_CONDITION), "40T6C") ||
        label_colour(st, ES2_SQUAD_CONDITION) != 0x20)
        return 33;
    /* CREW: the word before the show; a wingman order refuses the vehicle-8 bay */
    st->log[0] = 0;
    if (es2_screen_run(s, "click service_bay.17\n") || st->rebalances != 2 || strcmp(st->log, "U4 L4 P6 S6 ")) return 34;
    /* WEAPONS and BUILD store the word after their show, CREW before (0x43ad00) */
    if (st->word_at_show[2] != -1 || st->word_at_show[4] != 2 || st->word_at_show[6] != 6) return 48;
    b->session.order = 1;
    st->log[0] = 0;
    if (es2_screen_run(s, "click squad_panel.37\n") || b->session.bay != 2) return 35; /* bay 4 is vehicle 8 */
    if (es2_screen_run(s, "click squad_panel.42\n") || b->session.bay != 5 || strcmp(st->log, "E6:5:1 ")) return 36;
    es2_squad_panel_select(b, -1, 1); /* CLEAR: the panel without a selection */
    if (b->session.bay != -1 || hidden(st, b->squad[ES2_SQUAD_GRID_NONE]) || !hidden(st, b->squad[ES2_SQUAD_GRID + 5]))
        return 37;
    if (strcmp(label_text(st, ES2_SQUAD_PILOT), "T00") || label_colour(st, ES2_SQUAD_PILOT) != 0x17) return 38;
    /* ARMORY: leave CREW (hide with 2, sic), no panel, palette 2 */
    st->log[0] = 0;
    if (es2_screen_run(s, "click service_bay.15\n") || st->rebalances != 2 || strcmp(st->log, "U2 L6 S5 ")) return 39;
    if (!hidden(st, b->squad[ES2_SQUAD_INVENTORY])) return 40;
    /* the -1 selection's grid stays shown (0x43d779 hides [0x48d598]) */
    if (hidden(st, b->squad[ES2_SQUAD_GRID_NONE])) return 41;
    /* MISSION: page 0 (movies not shown, position 0), word 7 */
    st->flow.page = 3;
    st->log[0] = 0;
    if (es2_screen_run(s, "click service_bay.19\n") || st->flow.page != 0 || b->session.screen != 7 || strcmp(st->log, "L5 S7 "))
        return 42;
    /* SAVE: caller 8, Saved games, the bay hidden */
    st->log[0] = 0;
    if (es2_screen_run(s, "click service_bay.07\n") || st->flow.screen != ES2_FLOW_SAVED_GAMES || st->flow.caller != 8 ||
        strcmp(st->log, "L7 S1 ") || !hidden(st, b->bay[ES2_BAY_FRAME]))
        return 43;
    /* back in the bay (Saved games EXIT): MAIN MENU saves slot 10 */
    (void)es2_shell_flow_event(&st->flow, ES2_FLOW_SLOT_EXIT, NULL);
    es2_service_bay_show(b);
    st->log[0] = 0;
    if (es2_screen_run(s, "step\nclick service_bay.05\n") || st->flow.screen != ES2_FLOW_MAIN_MENU || st->saves != 1 ||
        b->session.screen != 0 || strcmp(st->log, "S0 ") || st->sounds != 7)
        return 44;
    /* the online image: the help routine */
    es2_service_bay_show(b);
    st->flow.screen = ES2_FLOW_SERVICE_BAY;
    st->flow.bay_word = -1;
    if (es2_screen_run(s, "step\nclick service_bay.03\n") || st->manuals != 1 || st->sounds != 7) return 45;
    /* a new game or a load: the selection 0 */
    es2_service_bay_game_loaded(b);
    if (b->session.bay != 0) return 46;
    /* the band function */
    if (es2_squad_band(b, 90) != 0 || es2_squad_band(b, 89) != 1 || es2_squad_band(b, 1) != 4 || es2_squad_band(b, 0) != 5 ||
        es2_squad_band(b, -3) != 5)
        return 47;
    return 0;
}

/* mode 0: WEAPONS and CREW only of the five; MAIN MENU, SAVE, MISSION keep theirs */
static int setup_mode0_behaviour(es2_screen_session *s, void *state, const char *data_root) {
    sb_state *st = state;
    es2_service_bay *b = &st->bay;
    int r = setup_mode0(s, state, data_root);
    if (r) return r;
    if (!es2_ui_get(&st->ui, b->bay[ES2_BAY_WEAPONS])->enabled || !es2_ui_get(&st->ui, b->bay[ES2_BAY_CREW])->enabled) return 10;
    if (es2_ui_get(&st->ui, b->bay[ES2_BAY_REPAIR])->enabled || es2_ui_get(&st->ui, b->bay[ES2_BAY_BUILD])->enabled ||
        es2_ui_get(&st->ui, b->bay[ES2_BAY_ARMORY])->enabled)
        return 11;
    if (!es2_ui_get(&st->ui, b->bay[ES2_BAY_MAIN_MENU])->enabled || !es2_ui_get(&st->ui, b->bay[ES2_BAY_MISSION])->enabled)
        return 12;
    if (es2_screen_run(s, "step\nclick service_bay.11\n") || b->session.screen != -1 || st->sounds) return 13;
    return 0;
}

/* ---- cases -------------------------------------------------------------------------------- */
static const es2_screen_expect shown_expect[] = {
    {"bay", "266988f2021406f63ce9223030d734edcef414850374505fd6dc0e7d3e36319d",
     "03a823b28b17ed3b61a79914cbdae4c7985a59fd2bda98a5267b70cec9ec1f5e", NULL},
};
static const es2_screen_expect tour_expect[] = {
    {"bay", "266988f2021406f63ce9223030d734edcef414850374505fd6dc0e7d3e36319d",
     "03a823b28b17ed3b61a79914cbdae4c7985a59fd2bda98a5267b70cec9ec1f5e", NULL},
    {"held", "2f2a989b40558180b460eef0994977f139f51befb9f8e7ce8374186ff95b24d8",
     "f7d8e3d4cd197d576a2fd1b6c74ac7b93f8cd21ced1397208b16941973620cb0", NULL},
    {"weapons", "2f2a989b40558180b460eef0994977f139f51befb9f8e7ce8374186ff95b24d8",
     "f7d8e3d4cd197d576a2fd1b6c74ac7b93f8cd21ced1397208b16941973620cb0", NULL},
    {"bay2", "7429ed1d7281fbe7e5a5a231c93443b60b374a0125efc1ce68e00c8146183205",
     "f7d8e3d4cd197d576a2fd1b6c74ac7b93f8cd21ced1397208b16941973620cb0", NULL},
    {"build", "60a30b7746684f9dad073aacc8f8c573cd00b67f0b8251ceb718417073ca96b6",
     "f7d8e3d4cd197d576a2fd1b6c74ac7b93f8cd21ced1397208b16941973620cb0", NULL},
    {"crew", "bb30ce01b7735f70d633799de912561ebfaa27f890ce3ec60c6f428d1be22046",
     "f7d8e3d4cd197d576a2fd1b6c74ac7b93f8cd21ced1397208b16941973620cb0", NULL},
    {"armory", "668ff56df380d521c94ca5e8eb88bc5540f0e5c31cdca1e41253001cb86eb856",
     "f7d8e3d4cd197d576a2fd1b6c74ac7b93f8cd21ced1397208b16941973620cb0", NULL},
    {"mission", "559dc24013f2a227055ba28e4927ea30e5a09b7e7e81b5f413fac42646996468",
     "ab230ae300feab8bc9601410d33e219638dd20beb5688ba32152f8de92eb85b2", NULL},
    {"main", "a8af67bcc3049924565a17ea266ed24d2fb822ec4d5fdf439083622e214dc1cd",
     "03a823b28b17ed3b61a79914cbdae4c7985a59fd2bda98a5267b70cec9ec1f5e", NULL},
};
static const es2_screen_expect behaviour_expect[] = {{"end", "53856d7a1bbea4637358c29848c37a0c46a73ef09d246c1d68c0c63ffec1c99f",
     "03a823b28b17ed3b61a79914cbdae4c7985a59fd2bda98a5267b70cec9ec1f5e", NULL}};
static const es2_screen_expect mode0_expect[] = {{"mode0", "266988f2021406f63ce9223030d734edcef414850374505fd6dc0e7d3e36319d",
     "03a823b28b17ed3b61a79914cbdae4c7985a59fd2bda98a5267b70cec9ec1f5e", NULL}};
static const es2_screen_expect real_expect[] = {
    {"bay", "111700f617471470129b47a3abbf4f5665c147fd3dc69a5a16fb40964d284a9f",
     "96b549df7c20ad4f6b89e13333204a31f25ffedaf43cbf4bcc330bef14f4042a", "game_r:service_bay:after_continue"},
    {"weapons", "49f4329d3963c378953d8b6f71d3ef1e525b771055a1ac674f92c354d53c3360",
     "744dc2f654b97b0e0cd30247585d27d7f7efaf2964fc3b14c04e873d8a4d9587", NULL},
    {"armory", "882f95ee4b07b352194e531f16e4d1dfd66615c9fcc30c1a91aed01f757202c0",
     "744dc2f654b97b0e0cd30247585d27d7f7efaf2964fc3b14c04e873d8a4d9587", NULL},
};

#define N(a) (sizeof(a) / sizeof((a)[0]))
static const es2_screen_case cases[] = {
    {"service_bay.shown", "service_bay", 0, sizeof(sb_state), setup, "step\nsnap bay\n", shown_expect, N(shown_expect), NULL},
    {"service_bay.tour", "service_bay", 0, sizeof(sb_state), setup,
     "step\n"
     "snap bay\n"
     "move 639 479\nmove 0 0\nmove 214 15\nstep 2\nldown\nstep 2\n"
     "snap held\n"
     "lup\nstep\n"
     "snap weapons\n"
     "click squad_panel.22\n"
     "snap bay2\n"
     "click service_bay.13\n"
     "snap build\n"
     "click service_bay.17\n"
     "snap crew\n"
     "click service_bay.15\n"
     "snap armory\n"
     "click service_bay.19\n"
     "snap mission\n"
     "click service_bay.05\n"
     "snap main\n",
     tour_expect, N(tour_expect), NULL},
    {"service_bay.behaviour", "service_bay", 0, sizeof(sb_state), setup_behaviour, "step\nsnap end\n", behaviour_expect,
     N(behaviour_expect), NULL},
    {"service_bay.mode0", "service_bay", 0, sizeof(sb_state), setup_mode0_behaviour, "step\nsnap mode0\n", mode0_expect,
     N(mode0_expect), NULL},
    {"service_bay.real", "service_bay", 1, sizeof(sb_state), setup_real,
     "step\n"
     "snap bay\n"
     "click service_bay.09\n"
     "snap weapons\n"
     "click service_bay.15\n"
     "snap armory\n",
     real_expect, N(real_expect), NULL},
};

ES2_SCREEN_SUITE(service_bay, cases)
