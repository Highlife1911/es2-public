/* The Herc picture readers, the panels' layers, the diagram colours,
 * the slot marker and the hot-spot rectangles
 * (es2/shell/gui/squad_pictures.h) on squad_pictures_fixture.h's synthetic
 * files and records, and the condition helpers 0x4121f8 / 0x41232a
 * (es2/shell/campaign.h). Expected values follow from vshell_ui.md section
 * 36 and docs/formats/herc_pictures.md. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "es2/shell/campaign.h"
#include "es2/shell/gui/loop.h"
#include "es2/shell/gui/service_bay.h"
#include "squad_pictures_fixture.h"

static unsigned checks, failures;
#define CHECK(x) do { checks++; if (!(x)) { failures++; fprintf(stderr, "line %d: %s\n", __LINE__, #x); } } while (0)

static spf_fixture fx;
static es2_squad_pictures pics;
static es2_herc_type_pictures tp;
static es2_gui_shell *shell;
static es2_ui *ui;
static es2_shell_flow flow;
static es2_service_bay bay;
static es2_ui_id rgrids[8];

/* ---- condition helpers ------------------------------------------------------------- */
static void test_conditions(void) {
    int16_t w[33], c = 0;
    uint8_t fixed[76];
    static const int16_t groups[24] = {0, 1, -1, 0, 2, -1, 0, 0, 3, 4, 5, -1, 6, -1, 0, 0, 7, 8, -1, 0, 9, 10, 11, 12};
    for (int i = 0; i < 33; i++) w[i] = 100;
    for (int16_t cap = 0; cap <= 10; cap++) {
        CHECK(es2_shell_overall_condition(w, cap, &c, NULL) == ES2_OK && c == 100);
    }
    /* word 22 never counts; words past the capacity never count */
    w[22] = -30000, w[30] = -30000;
    CHECK(es2_shell_overall_condition(w, 7, &c, NULL) == ES2_OK && c == 100);
    CHECK(es2_shell_overall_condition(w, 8, &c, NULL) == ES2_OK && c == (int16_t)((100 * 29 - 30000) / 30));
    /* the 16-bit sum wraps before the division: 22 x 2000 = 44000 -> -21536 */
    for (int i = 0; i < 33; i++) w[i] = 2000;
    CHECK(es2_shell_overall_condition(w, 0, &c, NULL) == ES2_OK && c == (int16_t)(-21536 / 22));
    /* truncation toward zero of a negative sum */
    for (int i = 0; i < 33; i++) w[i] = 0;
    w[0] = -23;
    CHECK(es2_shell_overall_condition(w, 0, &c, NULL) == ES2_OK && c == -1);
    w[0] = -21;
    CHECK(es2_shell_overall_condition(w, 0, &c, NULL) == ES2_OK && c == 0);
    /* refusals */
    CHECK(es2_shell_overall_condition(w, -1, &c, NULL) == ES2_ERR_ARGUMENT);
    CHECK(es2_shell_overall_condition(w, 11, &c, NULL) == ES2_ERR_ARGUMENT);
    CHECK(es2_shell_overall_condition(NULL, 1, &c, NULL) == ES2_ERR_ARGUMENT);
    /* the SAV form: words at +4, the capacity at +74 */
    memset(fixed, 0, sizeof fixed);
    for (int i = 0; i < 33; i++) fixed[4 + 2 * i] = 50;
    fixed[74] = 3;
    CHECK(es2_shell_record_condition((es2_span){fixed, 76}, &c, NULL) == ES2_OK && c == 50);
    fixed[4] = 0x10, fixed[5] = 0x27; /* word 0 = 10000 */
    CHECK(es2_shell_record_condition((es2_span){fixed, 76}, &c, NULL) == ES2_OK && c == (int16_t)((10000 + 24 * 50) / 25));
    CHECK(es2_shell_record_condition((es2_span){fixed, 75}, &c, NULL) == ES2_ERR_TRUNCATED);
    fixed[74] = 11;
    CHECK(es2_shell_record_condition((es2_span){fixed, 76}, &c, NULL) == ES2_ERR_ARGUMENT);
    /* 0x41232a */
    for (int i = 0; i < 33; i++) w[i] = (int16_t)(i * 3 - 20);
    CHECK(es2_shell_condition(groups, w, 0, 0, &c, NULL) == ES2_OK && c == (int16_t)((-20 + -17) / 2));
    CHECK(es2_shell_condition(groups, w, 0, 2, &c, NULL) == ES2_OK && c == (int16_t)((-11 + -8 + -5) / 3));
    CHECK(es2_shell_condition(groups, w, 0, 5, &c, NULL) == ES2_ERR_UNSUPPORTED); /* unterminated */
    CHECK(es2_shell_condition(groups, w, 0, 6, &c, NULL) == ES2_ERR_UNSUPPORTED);
    CHECK(es2_shell_condition(groups, w, 0, -1, &c, NULL) == ES2_ERR_UNSUPPORTED);
    CHECK(es2_shell_condition(groups, w, 1, 0, &c, NULL) == ES2_OK && c == w[13]);
    CHECK(es2_shell_condition(groups, w, 1, 8, &c, NULL) == ES2_OK && c == w[21]);
    CHECK(es2_shell_condition(groups, w, 2, 9, &c, NULL) == ES2_OK && c == w[32]);
    CHECK(es2_shell_condition(groups, w, 2, 10, &c, NULL) == ES2_ERR_OFFSET);
    CHECK(es2_shell_condition(groups, w, 1, -14, &c, NULL) == ES2_ERR_OFFSET);
    CHECK(es2_shell_condition(groups, w, 3, 0, &c, NULL) == ES2_ERR_ARGUMENT);
    CHECK(es2_shell_condition(NULL, w, 0, 0, &c, NULL) == ES2_ERR_ARGUMENT);
    CHECK(es2_shell_condition(NULL, w, 2, 0, &c, NULL) == ES2_OK);
    {
        static const int16_t bad[24] = {40, -1};
        CHECK(es2_shell_condition(bad, w, 0, 0, &c, NULL) == ES2_ERR_OFFSET);
    }
}

/* ---- readers ------------------------------------------------------------------------- */
static void test_readers(void) {
    const es2_herc_picture *r;
    spf_init(&fx);
    memset(&tp, 0, sizeof tp);
    CHECK(es2_herc_arm_parse((es2_span){fx.arm[3].data, fx.arm[3].size}, &tp) == 0 && tp.arm_loaded);
    CHECK(tp.body[0].layer == 0 && tp.body[0].x == 4 && tp.body[0].y == 1 && tp.body[0].frame == 0);
    CHECK(tp.body[1].layer == 1 && tp.body[1].y == 140 && tp.body[1].frame == 1 && tp.body[1].mirror == 1);
    CHECK(tp.arm[0].count == 3 && tp.arm[1].count == 3 && tp.arm[5].count == 2 && tp.arm[2].count == 0);
    /* the slot lists' points are one more than the file's (0x415335..) */
    r = es2_herc_lookup(&tp, 0, 1, 3);
    CHECK(r && r->x == 53 && r->y == 105 && r->x2 == 63 && r->y2 == 65 && r->frame == 3 && r->mirror == 0);
    r = es2_herc_lookup(&tp, 0, 1, 2);
    CHECK(r && r->mirror == 2);
    r = es2_herc_lookup(&tp, 0, 5, 2);
    CHECK(r && r->frame == -1);
    CHECK(!es2_herc_lookup(&tp, 0, 5, 4) && !es2_herc_lookup(&tp, 0, 2, 2) && !es2_herc_lookup(&tp, 0, 33, 2) &&
          !es2_herc_lookup(&tp, 0, -1, 2) && !es2_herc_lookup(&tp, 1, 1, 6));
    CHECK(es2_herc_rpr_parse((es2_span){fx.rpr[3].data, fx.rpr[3].size}, &tp) == 0 && tp.rpr_loaded);
    CHECK(tp.layer_count == 3 && tp.records[tp.layer_first].x == 8 && tp.records[tp.layer_first + 2].layer == 17);
    CHECK(tp.records[tp.layer_first + 1].mirror == 2);
    CHECK(tp.diagram.layer == 0 && tp.diagram.x == 20 && tp.diagram.y == 30);
    r = es2_herc_lookup(&tp, 1, 1, 7);
    CHECK(r && r->x == 45 && r->y == 150 && r->frame == 4); /* rpr lists: no +1 */
    CHECK(es2_herc_lookup(&tp, 1, 5, 7) && !es2_herc_lookup(&tp, 1, 5, 6));
    /* every truncation refused */
    int refused = 1;
    for (size_t n = 0; n < fx.arm[0].size; n++) {
        es2_herc_type_pictures t2;
        memset(&t2, 0, sizeof t2);
        if (es2_herc_arm_parse((es2_span){fx.arm[0].data, n}, &t2) != -1 || t2.arm_loaded) refused = 0;
    }
    for (size_t n = 0; n < fx.rpr[0].size; n++) {
        es2_herc_type_pictures t2;
        memset(&t2, 0, sizeof t2);
        if (es2_herc_rpr_parse((es2_span){fx.rpr[0].data, n}, &t2) != -1 || t2.rpr_loaded) refused = 0;
    }
    for (size_t n = 0; n < fx.arm_hots.size; n++) {
        es2_squad_hots h;
        if (es2_squad_hots_parse((es2_span){fx.arm_hots.data, n}, &h) != -1) refused = 0;
    }
    CHECK(refused);
    /* trailing bytes are not read */
    {
        spf_buffer b = fx.arm[0];
        es2_herc_type_pictures t2;
        memset(&t2, 0, sizeof t2);
        b.data[b.size++] = 0x55;
        CHECK(es2_herc_arm_parse((es2_span){b.data, b.size}, &t2) == 0);
    }
    /* an item ID outside 0..32, a negative count */
    {
        spf_buffer b = fx.arm[0];
        es2_herc_type_pictures t2;
        memset(&t2, 0, sizeof t2);
        b.data[46] = 33; /* the first list's ID (after the two records and the list count) */
        CHECK(es2_herc_arm_parse((es2_span){b.data, b.size}, &t2) == -1);
        b = fx.arm[0];
        b.data[48] = 0xff, b.data[49] = 0xff;
        CHECK(es2_herc_arm_parse((es2_span){b.data, b.size}, &t2) == -1);
        b = fx.rpr[0];
        b.data[0] = 0xff, b.data[1] = 0xff;
        CHECK(es2_herc_rpr_parse((es2_span){b.data, b.size}, &t2) == -1);
    }
    /* a later list of the same ID replaces the earlier (0x415267 stores the count again) */
    {
        spf_buffer b;
        es2_herc_type_pictures t2;
        memset(&t2, 0, sizeof t2);
        b.size = 0;
        spf_rec(&b, 0, 1, 1, 0, 0, 0, 0, 1);
        spf_rec(&b, 1, 1, 1, 0, 0, 0, 0, 1);
        spf_w(&b, 2);
        spf_w(&b, 4), spf_w(&b, 1), spf_rec(&b, 2, 5, 5, 5, 5, 1, 0, 1);
        spf_w(&b, 4), spf_w(&b, 1), spf_rec(&b, 3, 6, 6, 6, 6, 2, 0, 1);
        CHECK(es2_herc_arm_parse((es2_span){b.data, b.size}, &t2) == 0 && t2.arm[4].count == 1 &&
              !es2_herc_lookup(&t2, 0, 4, 2) && es2_herc_lookup(&t2, 0, 4, 3));
    }
    /* hots: the asserted 9, the rectangles */
    {
        es2_squad_hots h;
        CHECK(es2_squad_hots_parse((es2_span){fx.arm_hots.data, fx.arm_hots.size}, &h) == 0 && h.loaded);
        CHECK(h.count[8] == 4 && h.rect[8][3][0] == 130 && h.rect[8][3][1] == 108 && h.rect[8][3][3] == 120);
        spf_buffer b = fx.arm_hots;
        b.data[0] = 8;
        CHECK(es2_squad_hots_parse((es2_span){b.data, b.size}, &h) == -1);
        b = fx.arm_hots;
        b.data[4] = 33; /* count of type 0 above the pool */
        CHECK(es2_squad_hots_parse((es2_span){b.data, b.size}, &h) == -1);
    }
    CHECK(es2_herc_arm_parse((es2_span){NULL, 0}, &tp) == -1 && es2_herc_arm_parse((es2_span){fx.arm[0].data, 4}, NULL) == -1);
}

/* ---- the panels ---------------------------------------------------------------------- */
static es2_ui_id h_repair_grid(void *ctx, int16_t k) { (void)ctx; return k >= 0 && k < 8 ? rgrids[k] : 0; }

static void fresh(void) {
    es2_shell_flow_host fh;
    if (ui) es2_ui_release(ui);
    free(ui);
    free(shell);
    spf_init(&fx);
    spf_host(&fx, &pics);
    CHECK(es2_squad_pictures_load(&pics) == 0);
    shell = calloc(1, sizeof *shell);
    ui = calloc(1, sizeof *ui);
    es2_gui_shell_init(shell, NULL, NULL, NULL);
    CHECK(es2_ui_init(ui, shell) == 0);
    memset(&fh, 0, sizeof fh);
    es2_shell_flow_init(&flow, &fh, NULL);
    flow.mode = 1;
    memset(&bay, 0, sizeof bay);
    bay.flow = &flow;
    bay.host.repair_grid = h_repair_grid;
    for (int i = 0; i < 5; i++) bay.band_limit[i] = (int16_t)(89 - 20 * i);
    for (int i = 0; i < 6; i++) bay.band_colour[i] = 40 + i;
    CHECK(es2_screen_service_bay_build(ui, &bay) == 0 && es2_screen_squad_panel_build(ui, &bay) == 0);
    for (int k = 0; k < 8; k++) rgrids[k] = es2_ui_grid(ui, ui->root, (es2_gui_rect){16, 47, 224, 303}, NULL, 0x22);
    bay.pictures = &pics;
    es2_ui_show(ui, ui->root);
}

static es2_ui_layer *L(es2_ui_id grid, int i) {
    es2_ui_widget *w = es2_ui_get(ui, grid);
    return w && w->grid >= 0 ? &ui->layers[w->grid][i] : NULL;
}
static es2_ui_id G(int k) { return bay.squad[ES2_SQUAD_GRID + k]; }
static const es2_bitmap *F(const char *file, int n) { return spf_frame(&fx, file, n); }
static int pairs_default(const es2_ui_layer *l) {
    for (int i = 0; i < 10; i++)
        if (l->from[i] != 0x0e || l->to[i] != 0x10) return 0;
    return 1;
}
static int empty_grid(es2_ui_id grid) {
    for (int i = 0; i < ES2_UI_GRID_LAYERS; i++)
        if (L(grid, i)->bitmap) return 0;
    return 1;
}
static int16_t colour_of(int16_t c) { return (int16_t)bay.band_colour[es2_squad_band(&bay, c)]; }

static void test_arm_panel(void) {
    es2_ui_widget *g;
    fresh();
    for (int word = 2; word <= 6; word += 2) {
        es2_squad_pictures_panel(&bay, word, 0);
        es2_squad_pictures_panel(&bay, word, 1);
        /* geometry 0x41f471(5, 0x2b), width 0xe7, height 0x105 (one row less than built), no lines */
        g = es2_ui_get(ui, G(0));
        CHECK(g->rect.left == 5 && g->rect.top == 43 && g->rect.right == 235 && g->rect.bottom == 303 && !g->grid_lines);
        CHECK(g->abs.bottom == 303 && g->abs.right == 235);
        /* the panel without a selection */
        CHECK(L(G(8), 0)->bitmap == F("dba\\mt_3qtr.dba", 0) && L(G(8), 0)->x == 1 && L(G(8), 0)->y == 1);
        CHECK(L(G(8), 1)->bitmap == F("dba\\mt_3qtr.dba", 1) && L(G(8), 1)->y == 0x8c);
        /* bay 0: finished, body and outline frames of the record */
        CHECK(L(G(0), 0)->bitmap == F("dba\\out_bod.dba", 0) && L(G(0), 0)->x == 1 && L(G(0), 0)->y == 1);
        CHECK(L(G(0), 1)->bitmap == F("dba\\out_bod.dba", 1) && L(G(0), 1)->y == 140 && L(G(0), 1)->mirror == 0);
        CHECK(pairs_default(L(G(0), 0)) && pairs_default(L(G(0), 1)));
        /* slot 0 item 5: its key-2 record has frame -1 (no layer); slot 1 empty;
         * slot 2 item 1: layer 4 of the weapons DBA at (93, 105) */
        CHECK(!L(G(0), 2)->bitmap && !L(G(0), 3)->bitmap);
        CHECK(L(G(0), 4)->bitmap == F("dba\\out_wep.dba", 4) && L(G(0), 4)->x == 93 && L(G(0), 4)->y == 105);
        /* bay 1 at progress 0: the empty bay picture; mirror of the outline kept (type 1) */
        CHECK(L(G(1), 0)->bitmap == F("dba\\mt_3qtr.dba", 0) && L(G(1), 0)->x == 2);
        CHECK(L(G(1), 1)->bitmap == F("dba\\mt_3qtr.dba", 1) && L(G(1), 1)->mirror == 1);
        /* bay 2 at 30: body frames 2 and 3; bay 5 at 70: 4 and 5 */
        CHECK(L(G(2), 0)->bitmap == F("dba\\tom_bod.dba", 2) && L(G(2), 1)->bitmap == F("dba\\tom_bod.dba", 3));
        CHECK(L(G(5), 0)->bitmap == F("dba\\apoc_bod.dba", 4) && L(G(5), 1)->bitmap == F("dba\\apoc_bod.dba", 5));
        /* bay 3 without a record: the empty picture at (1, 1) and (1, 140) */
        CHECK(L(G(3), 0)->bitmap == F("dba\\mt_3qtr.dba", 0) && L(G(3), 1)->bitmap == F("dba\\mt_3qtr.dba", 1) &&
              L(G(3), 1)->x == 1 && L(G(3), 1)->y == 0x8c && L(G(3), 1)->mirror == 0);
        /* bay 4, type 8 (the Razor's files): slots 0 and 1 hold item 1, key-2
         * record mirrored */
        CHECK(L(G(4), 0)->bitmap == F("dba\\fly_bod.dba", 0) && L(G(4), 2)->bitmap == F("dba\\fly_wep.dba", 2) &&
              L(G(4), 2)->mirror == 2 && L(G(4), 3)->bitmap == F("dba\\fly_wep.dba", 3) && !L(G(4), 4)->bitmap);
        /* bay 7: ten slots of item 1; only keys 2..4 have records */
        CHECK(L(G(7), 2)->bitmap && L(G(7), 4)->bitmap && !L(G(7), 5)->bitmap && !L(G(7), 11)->bitmap);
    }
    /* the unload clears every panel, the one without a selection too */
    es2_squad_pictures_panel(&bay, 6, 0);
    int all = empty_grid(G(8));
    for (int k = 0; k < 8; k++) all &= empty_grid(G(k));
    CHECK(all);
    CHECK(pics.refused == 0 && pics.data_errors == 0);
}

static void test_rpr_panel(void) {
    es2_ui_widget *g;
    int16_t c;
    fresh();
    bay.session.bay = 4;
    bay.session.tab = 0;
    es2_squad_pictures_panel(&bay, 3, 1);
    g = es2_ui_get(ui, G(0));
    CHECK(g->rect.left == 16 && g->rect.top == 47 && g->rect.right == 223 && g->rect.bottom == 302 && g->grid_lines == 1);
    /* bay 0: layers 0, 2, 17 of rpr_outl, sources 0x0e */
    CHECK(L(G(0), 0)->bitmap == F("dba\\rpr_outl.dba", 0) && L(G(0), 0)->x == 5 && L(G(0), 0)->from[0] == 0x0e);
    CHECK(L(G(0), 2)->bitmap == F("dba\\rpr_outl.dba", 1) && L(G(0), 2)->mirror == 2);
    CHECK(L(G(0), 17)->bitmap == F("dba\\rpr_outl.dba", 2) && L(G(0), 17)->x == 100);
    /* slot 2 item 1: key 8, rpr_wpns frame 5, source 0x0f; slot 0 item 5 has no key 6 */
    CHECK(L(G(0), 8)->bitmap == F("dba\\rpr_wpns.dba", 5) && L(G(0), 8)->from[0] == 0x0f && !L(G(0), 6)->bitmap);
    /* REPAIR's panel: the internal picture, mirror 0, the source the last layer left (0x0f) */
    CHECK(L(rgrids[0], 0)->bitmap == F("dba\\outl_int.dba", 0) && L(rgrids[0], 0)->x == 20 && L(rgrids[0], 0)->mirror == 0);
    CHECK(L(rgrids[0], 0)->from[0] == 0x0f);
    /* bay 4 (type 8): its weapon layers 6, 7; the Razor's second internal frame at (0x1d, 0x0e) */
    CHECK(L(G(4), 6)->bitmap == F("dba\\rpr_wpns.dba", 3) && L(G(4), 7)->bitmap == F("dba\\rpr_wpns.dba", 4));
    CHECK(L(rgrids[4], 1)->bitmap == F("dba\\razr_int.dba", 1) && L(rgrids[4], 1)->x == 0x1d && L(rgrids[4], 1)->y == 0x0e);
    /* bay 3: nothing */
    CHECK(empty_grid(G(3)) && empty_grid(rgrids[3]));
    /* the diagram of bay 4 (tab 0): layer 0 group 0, layer 2 group 2, layer 17 group 1; slots 6 + s */
    const es2_squad_record *d = &fx.bays[4];
    CHECK(es2_shell_condition(pics.groups, d->words, 0, 0, &c, NULL) == ES2_OK && L(G(4), 0)->to[0] == colour_of(c));
    CHECK(es2_shell_condition(pics.groups, d->words, 0, 2, &c, NULL) == ES2_OK && L(G(4), 2)->to[0] == colour_of(c));
    CHECK(es2_shell_condition(pics.groups, d->words, 0, 1, &c, NULL) == ES2_OK && L(G(4), 17)->to[0] == colour_of(c));
    for (int s = 0; s < 4; s++) CHECK(L(G(4), 6 + s)->to[0] == colour_of(d->words[23 + s]));
    CHECK(L(G(0), 0)->to[0] == 0x10); /* other bays keep their pairs off */
    /* the data error: bay 0 slot 0 holds item 5, whose list has no key 6 */
    CHECK(fx.errors == 1 && pics.data_errors == 1 && !strcmp(fx.error, "Data Error: Herc: T6E   Socket: 0 \tWeapon: T83"));
    /* tab 1: REPAIR's layer 0 gets nine pairs: sources 0x46ffc0, targets by the family-1 words */
    bay.session.tab = 1;
    es2_squad_pictures_diagram(&bay);
    for (int s = 0; s < 9; s++) {
        CHECK(L(rgrids[4], 0)->from[s] == 0x20 + s && L(rgrids[4], 0)->to[s] == colour_of(d->words[13 + s]));
    }
    CHECK(L(rgrids[4], 0)->from[9] == 0x0f || L(rgrids[4], 0)->from[9] == 0x0e);
    /* no record selected: nothing */
    bay.session.bay = 3;
    es2_squad_pictures_diagram(&bay);
    bay.session.bay = -1;
    es2_squad_pictures_diagram(&bay);
    /* the unload: both panels of every bay */
    es2_squad_pictures_panel(&bay, 3, 0);
    int all = 1;
    for (int k = 0; k < 8; k++) all &= empty_grid(G(k)) && empty_grid(rgrids[k]);
    CHECK(all);
}

static void test_marker(void) {
    const es2_ui_layer *m;
    fresh();
    es2_squad_pictures_panel(&bay, 2, 1);
    bay.session.bay = 0;
    /* slot 2 (item 1, key 4): the weapon layer and the marker from the
     * outline DBA at the second point, 0xba -> 0x63 */
    es2_squad_pictures_marker(&bay, 2);
    m = L(G(0), 12);
    CHECK(L(G(0), 4)->bitmap == F("dba\\out_wep.dba", 4));
    CHECK(m->bitmap == F("dba\\out_out.dba", 4) && m->x == 103 && m->y == 65 && m->from[0] == 0xba && m->to[0] == 0x63);
    CHECK(m->to[1] == 0x10 && m->from[1] == 0x0e);
    /* slot 1 (empty): layer 3 off, the marker at item 0's key-3 record */
    es2_squad_pictures_marker(&bay, 1);
    CHECK(!L(G(0), 3)->bitmap && m->bitmap == F("dba\\out_out.dba", 7) && m->x == 61 && m->y == 61);
    /* slot 0 (item 5, key 2 has frame -1): layer 2 off, the marker stays */
    es2_squad_pictures_marker(&bay, 0);
    CHECK(!L(G(0), 2)->bitmap && m->bitmap == F("dba\\out_out.dba", 7));
    /* the bay change: layer 12 off */
    es2_squad_pictures_marker_off(&bay);
    CHECK(!m->bitmap && pairs_default(m));
    /* refusals: a slot outside the ten, a bay without a record, bay -1 */
    unsigned before = pics.refused;
    es2_squad_pictures_marker(&bay, 10);
    CHECK(pics.refused == before + 1);
    bay.session.bay = 3;
    es2_squad_pictures_marker(&bay, 0);
    CHECK(empty_grid(G(3)) == 0 && pics.refused == before + 1); /* no record: nothing changed (the mt picture stays) */
    bay.session.bay = -1;
    es2_squad_pictures_marker_off(&bay);
    CHECK(pics.refused == before + 2);
    /* without pictures nothing changes */
    bay.pictures = NULL;
    bay.session.bay = 0;
    es2_squad_pictures_marker(&bay, 2);
    es2_squad_pictures_panel(&bay, 2, 0);
    CHECK(!empty_grid(G(0)) && !L(G(0), 12)->bitmap);
}

static void test_hot_spots(void) {
    es2_squad_hot_spot h[32];
    fresh();
    /* WEAPONS: one rectangle per slot below the capacity, handlers 0x43e7ef + 0x61 s */
    CHECK(es2_squad_hot_spots(&bay, 2, 0, h, 32) == 3);
    CHECK(h[0].rect[0] == 100 && h[0].rect[1] == 100 && h[2].rect[0] == 120 && h[2].handler == 0x43e7efu + 0xc2u);
    /* more slots than rectangles: the list ends (VSHELL reads past it) */
    unsigned before = pics.refused;
    CHECK(es2_squad_hot_spots(&bay, 2, 7, h, 32) == 4 && pics.refused == before + 1);
    /* REPAIR: the type's rectangles with the External rows' handlers, then
     * the mounted slots' (rpr_wpns frame size) with rows 6 + s */
    CHECK(es2_squad_hot_spots(&bay, 3, 4, h, 32) == 6);
    CHECK(h[0].rect[0] == 50 && h[0].rect[1] == 58 && h[3].handler == 0x434745u + 3u * 0x65u);
    CHECK(h[4].rect[0] == 15 && h[4].rect[1] == 150 && h[4].rect[2] == 25 && h[4].rect[3] == 158 &&
          h[4].handler == 0x434745u + 6u * 0x65u && h[5].handler == 0x434745u + 7u * 0x65u);
    /* bay 0's item 5 has no rpr record: VSHELL's process ends there */
    CHECK(es2_squad_hot_spots(&bay, 3, 0, h, 32) == 4);
    CHECK(es2_squad_hot_spots(&bay, 4, 0, h, 32) == 0 && es2_squad_hot_spots(&bay, 2, 3, h, 32) == 0);
}

int main(void) {
    test_conditions();
    test_readers();
    test_arm_panel();
    test_rpr_panel();
    test_marker();
    test_hot_spots();
    if (ui) es2_ui_release(ui);
    free(ui);
    free(shell);
    printf("%u checks, %u failures\n", checks, failures);
    return failures != 0;
}
