/* The REPAIR screen and the Herc scrap warning
 * (es2/shell/gui/repair.h) on a synthetic memory state (repair_fixture.h),
 * synthetic resources and a synthetic flow: the build from the layout table,
 * the show (mode text, row texts, the selection, the labels and enable rules),
 * the rows through the toolkit's input path, REPAIR, REPAIR ALL, CANCEL, the
 * grid swap of the Internal list, the bay selector with the vehicle-8 names,
 * the warning's CANCEL and ACCEPT, and the refusals without a bay. The state
 * after each operation is compared with the editors library run directly. */
#include "es2/shell/gui/repair.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "main_menu_fixture.h"
#include "repair_fixture.h"

static unsigned checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static uint8_t font_bytes[16384];
static es2_dfn font;
static repair_fixture fx;
static es2_gui_shell *shell;
static es2_ui *ui;
static es2_shell_flow flow;
static es2_shell_buffer save;

typedef struct squad_call { uint32_t routine; int16_t a, b; } squad_call;
static squad_call calls[512];
static unsigned ncalls, changed;
static uint32_t changed_handler;

static const es2_dfn *h_font(void *c, uint32_t g) { (void)c; return g == 0x46de00 ? &font : NULL; }
static const char *h_text(void *c, uint32_t id) {
    static char t[128][16];
    static unsigned n;
    char *s = t[n++ & 127];
    (void)c;
    snprintf(s, sizeof t[0], "T%X", (unsigned)id);
    return s;
}
static const char *h_literal(void *c, uint32_t a) { (void)c; return a == 0x474080 ? "193" : ""; }
static es2_repair_screen rs;
static es2_shell_editor_session session;
static int panel_host, panel_words[8], panel_count; /* a stand-in for es2_squad_panel_select */
static void h_squad(void *c, uint32_t routine, int16_t a, int16_t b) {
    (void)c;
    if (ncalls < sizeof calls / sizeof calls[0]) calls[ncalls++] = (squad_call){routine, a, b};
    if (panel_host && routine == ES2_REPAIR_SQUAD_SELECT && session.bay != b) {
        if (panel_count < 8) panel_words[panel_count++] = session.screen;
        session.bay = b;
        if (session.screen == ES2_SHELL_SCREEN_REPAIR) CHECK(es2_repair_screen_selected(&rs) == 1);
    }
}
static void h_changed(void *c, uint32_t h, es2_shell_flow_result r) {
    (void)c;
    (void)r;
    changed++;
    changed_handler = h;
}
static int called(uint32_t routine, int16_t a) {
    for (unsigned i = 0; i < ncalls; i++)
        if (calls[i].routine == routine && calls[i].a == a) return 1;
    return 0;
}

static char expect[64];
static const char *T(unsigned id) { snprintf(expect, sizeof expect, "T%X", id); return expect; }

/* A fresh toolkit, flow and screen over the fixture's state (after edit). */
static void fresh(uint8_t repair_option, int16_t bay) {
    uint8_t prefs[ES2_SHELL_PREFS_SIZE];
    es2_shell_flow_host fh;
    es2_span pf = {prefs, sizeof prefs};
    size_t n = 0;
    if (ui) es2_ui_release(ui);
    free(ui);
    free(shell);
    free(save.data);
    shell = calloc(1, sizeof *shell);
    ui = calloc(1, sizeof *ui);
    es2_gui_shell_init(shell, NULL, NULL, NULL);
    CHECK(es2_ui_init(ui, shell) == 0);
    es2_ui_install(ui, &shell->hooks);
    memset(prefs, 0, sizeof prefs);
    prefs[ES2_SHELL_PREFS_MODE] = 1;
    prefs[ES2_SHELL_PREFS_REPAIR] = repair_option;
    memset(&fh, 0, sizeof fh);
    es2_shell_flow_init(&flow, &fh, &pf);
    es2_shell_editor_session_init(&session, bay);
    save.data = rf_save(&fx, &n);
    save.size = n;
    CHECK(save.data != NULL);
    CHECK(rf_vfs(&fx) == 0);
    memset(&rs, 0, sizeof rs);
    rs.flow = &flow;
    rs.session = &session;
    rs.vfs = &fx.vfs;
    rs.tables = (es2_span){fx.tables, sizeof fx.tables};
    rs.editor_tables = (es2_span){fx.editor, sizeof fx.editor};
    rs.save = &save;
    rs.host = (es2_repair_screen_host){NULL, h_font, h_text, h_literal, h_squad, h_changed};
    ncalls = changed = 0;
    {
        const es2_ui_screen_entry *e = es2_ui_screen_find(ES2_UI_SCREEN_REPAIR), *b = es2_ui_screen_find(ES2_UI_SCREEN_HERC_SCRAP_BOX);
        CHECK(e && e->build(ui, &rs) == 0);
        CHECK(b && b->build(ui, &rs) == 0);
    }
    es2_ui_show(ui, ui->root);
}
static void step(void) {
    ui->now += 100;
    es2_gui_step(shell);
}
static void steps(int k) { while (k-- > 0) step(); }
static es2_ui_widget *W(int control) { return es2_ui_get(ui, rs.ids[control]); }
static int visible(es2_ui_id id) {
    es2_ui_widget *w = es2_ui_get(ui, id);
    if (!w) return 0;
    for (; w; w = es2_ui_get(ui, w->parent))
        if (w->flags & ES2_UI_HIDDEN) return 0;
    return 1;
}
static const char *label_of(int control) { return es2_ui_get(ui, W(control)->label)->text; }
static es2_ui_widget *column(int tab, int row, int k) {
    return es2_ui_get(ui, W(es2_repair_row_control(tab, row))->columns[k]);
}
static void click_window(es2_ui_id id) {
    es2_ui_widget *w = es2_ui_get(ui, id);
    int x = (w->abs.left + w->abs.right) / 2, y = (w->abs.top + w->abs.bottom) / 2;
    es2_gui_on_mouse(shell, ES2_GUI_WM_MOUSEMOVE, x, y, 0, 0);
    step();
    es2_gui_on_mouse(shell, ES2_GUI_WM_LBUTTONDOWN, x, y, 1, 0);
    step();
    es2_gui_on_mouse(shell, ES2_GUI_WM_LBUTTONUP, x, y, 0, 0);
    step();
}
static void click(int control) { click_window(rs.ids[control]); }
static void click_row(int tab, int row) {
    /* the label column of the row: a child without handler, the quad handles it */
    click(es2_repair_row_control(tab, row));
}

/* The SAV state: a condition word of a bay, the salvage. */
static int cond_word(int bay, int word) {
    es2_sav s;
    CHECK(es2_sav_parse((es2_span){save.data, save.size}, &s, NULL) == ES2_OK);
    for (size_t i = 0; i < s.loadout_count; i++) {
        es2_sav_loadout l;
        CHECK(es2_sav_loadout_at(&s, i, &l, NULL) == ES2_OK);
        if (l.index == bay) return (int16_t)(l.fixed.data[4 + 2 * word] | l.fixed.data[5 + 2 * word] << 8);
    }
    return -1000;
}
static int has_bay(int bay) { return cond_word(bay, 0) != -1000; }
static uint32_t salvage(void) {
    es2_sav s;
    CHECK(es2_sav_parse((es2_span){save.data, save.size}, &s, NULL) == ES2_OK);
    return (uint32_t)(s.tally.data[0] | s.tally.data[1] << 8 | s.tally.data[2] << 16 | (uint32_t)s.tally.data[3] << 24);
}
/* The library's view of the present state (independent of the screen). */
static es2_shell_editor_view library_view(void) {
    es2_shell_editor_session s = session;
    es2_shell_editor_view v;
    es2_shell_buffer out = {0};
    memset(&v, 0, sizeof v);
    CHECK(es2_shell_editors(&fx.vfs, rs.tables, rs.editor_tables, (es2_span){save.data, save.size}, &s, NULL, 0, &out,
                            &v, NULL) == ES2_OK);
    free(out.data);
    return v;
}
static const char *amount(int32_t v) {
    static char t[64];
    snprintf(t, sizeof t, "%ld TC8", (long)v);
    return t;
}
static void check_labels(void) {
    es2_shell_editor_view v = library_view();
    CHECK(!strcmp(label_of(ES2_REPAIR_COST), amount(v.has_record ? v.item_cost : 0)));
    CHECK(!strcmp(label_of(ES2_REPAIR_ALL_COST), amount(v.has_record ? v.all_cost : 0)));
    CHECK(!strcmp(label_of(ES2_REPAIR_SALVAGE), amount((int32_t)(v.salvage - v.bank_cost))));
    CHECK(W(ES2_REPAIR_REPAIR)->enabled == v.repair_enabled);
    CHECK(W(ES2_REPAIR_REPAIR_ALL)->enabled == v.repair_all_enabled);
    CHECK(W(ES2_REPAIR_SCRAP)->enabled == v.scrap_enabled);
    CHECK(es2_ui_get(ui, W(ES2_REPAIR_REPAIR)->label)->text_colour == (v.repair_enabled ? 0x29 : 0x26));
    CHECK(W(ES2_REPAIR_REPAIR)->colour == (v.repair_enabled ? 0x22 : 0x26));
    CHECK(es2_ui_get(ui, W(ES2_REPAIR_CONDITION)->label)->text_colour == 0x17);
}

static void test_build(void) {
    static const struct { int control; uint32_t handler; } b[] = {
        {ES2_REPAIR_PANEL, 0x434693}, {ES2_REPAIR_EXTERNAL, 0x4346ec}, {ES2_REPAIR_INTERNAL, 0x434d95},
        {ES2_REPAIR_REPAIR, 0x4351bd}, {ES2_REPAIR_REPAIR_ALL, 0x4352e9}, {ES2_REPAIR_SCRAP, 0x4353a5},
        {ES2_REPAIR_CANCEL, 0x435403}};
    rf_init(&fx);
    fresh(0, 0);
    for (int k = 0; k < ES2_REPAIR_CONTROLS; k++) CHECK(rs.ids[k] != 0);
    for (int k = 0; k < ES2_HERC_SCRAP_CONTROLS; k++) CHECK(rs.box[k] != 0);
    for (int k = 0; k < 8; k++) CHECK(W(ES2_REPAIR_GRID0 + k)->cls == ES2_UI_GRID && !visible(rs.ids[k]));
    for (size_t i = 0; i < sizeof b / sizeof b[0]; i++)
        CHECK(W(b[i].control)->original_handler == b[i].handler && W(b[i].control)->handler != NULL);
    for (int r = 0; r < 16; r++) {
        CHECK(W(es2_repair_row_control(0, r))->cls == ES2_UI_QUAD);
        CHECK(W(es2_repair_row_control(0, r))->original_handler == 0x434745u + 0x65u * (unsigned)r);
    }
    for (int r = 0; r < 9; r++) CHECK(W(es2_repair_row_control(1, r))->original_handler == 0x434e30u + 0x65u * (unsigned)r);
    CHECK(es2_repair_row_control(0, 16) == -1 && es2_repair_row_control(1, 9) == -1 && es2_repair_row_control(2, 0) == -1);
    CHECK(!strcmp(column(0, 0, 0)->text, T(0x4e)) && !strcmp(column(1, 8, 0)->text, T(0x5c)));
    CHECK(!strcmp(label_of(ES2_REPAIR_MODE), T(0x41)) && !strcmp(label_of(ES2_REPAIR_COST), "193"));
    CHECK(es2_ui_get(ui, rs.box[ES2_HERC_SCRAP_ACCEPT])->original_handler == 0x448326u);
    CHECK(es2_ui_get(ui, rs.box[ES2_HERC_SCRAP_CANCEL])->original_handler == 0x4482c8u);
    CHECK(!visible(rs.ids[ES2_REPAIR_PANEL]) && !visible(rs.box[ES2_HERC_SCRAP_BOX]));
    CHECK(es2_screen_repair_build(NULL, &rs) == -1 && es2_screen_repair_build(ui, NULL) == -1);
}

static void test_show(void) {
    rf_init(&fx);
    fresh(0, 0);
    CHECK(es2_repair_screen_show(&rs, ES2_SHELL_SCREEN_BAY) == ES2_FLOW_DONE);
    steps(2);
    CHECK(session.screen == ES2_SHELL_SCREEN_REPAIR && session.bay == 0 && session.tab == 0 && session.row == 0);
    CHECK(session.salvage == 100000 && session.conditions[0] == 95 && session.conditions[23] == 64);
    CHECK(visible(rs.ids[ES2_REPAIR_PANEL]) && visible(rs.ids[ES2_REPAIR_EXTERNAL]) && visible(rs.ids[ES2_REPAIR_INTERNAL]));
    CHECK(visible(rs.ids[ES2_REPAIR_REPAIR]) && !visible(rs.ids[ES2_REPAIR_GRID0]));
    CHECK(!strcmp(label_of(ES2_REPAIR_MODE), T(0x42)));
    /* the part groups: signed averages of their words */
    CHECK(!strcmp(column(0, 0, 3)->text, "90%") && column(0, 0, 3)->text_colour == 0x0e && column(0, 0, 3)->align == 1);
    CHECK(!strcmp(column(0, 1, 3)->text, "45%") && column(0, 1, 3)->text_colour == 0x0b);
    CHECK(!strcmp(column(0, 2, 3)->text, "25%") && column(0, 2, 3)->text_colour == 0x0a);
    CHECK(!strcmp(column(0, 3, 3)->text, "100%") && !strcmp(column(0, 4, 3)->text, "66%"));
    CHECK(!strcmp(column(0, 5, 3)->text, "55%") && column(0, 5, 3)->text_colour == 0x0b);
    CHECK(!strcmp(column(1, 0, 3)->text, "45%") && !strcmp(column(1, 5, 3)->text, "75%") && column(1, 5, 3)->text_colour == 0x0c);
    CHECK(!strcmp(column(1, 2, 3)->text, "89%") && column(1, 2, 3)->text_colour == 0x0d);
    CHECK(!strcmp(column(1, 3, 3)->text, "0%") && column(1, 3, 3)->text_colour == 0x27);
    /* the hardpoints: item names, slot numbers, an empty slot, the rows above the capacity */
    CHECK(!strcmp(column(0, 6, 0)->text, T(0x81)) && column(0, 6, 0)->align == 0 && !strcmp(column(0, 6, 2)->text, "1"));
    CHECK(!strcmp(column(0, 6, 3)->text, "64%") && column(0, 6, 3)->text_colour == 0x0c);
    CHECK(!strcmp(column(0, 7, 0)->text, T(0x7d)) && !strcmp(column(0, 7, 2)->text, "2") && !strcmp(column(0, 7, 3)->text, " "));
    CHECK(!strcmp(column(0, 8, 0)->text, T(0x83)) && !strcmp(column(0, 8, 2)->text, "3"));
    for (int r = 9; r < 16; r++)
        CHECK(!strcmp(column(0, r, 0)->text, T(1)) && !strcmp(column(0, r, 2)->text, T(1)) &&
              !strcmp(column(0, r, 3)->text, T(1)) && column(0, r, 0)->align == 1);
    /* the selection (0, 0) was the image's: no row is coloured (0x434549 returns) */
    CHECK(column(0, 0, 0)->text_colour == 0x27 && W(es2_repair_row_control(0, 0))->colour == 16);
    CHECK(!strcmp(label_of(ES2_REPAIR_CONDITION), T(0x68)));
    check_labels();
    CHECK(called(ES2_REPAIR_SQUAD_LABELS, 0) && called(ES2_REPAIR_SQUAD_CONDITION, 0) && called(ES2_REPAIR_SQUAD_DIAGRAM, 0));
    CHECK(!called(ES2_REPAIR_SQUAD_SELECT, 0));
    /* the other mode texts */
    fresh(1, 0);
    CHECK(es2_repair_screen_show(&rs, ES2_SHELL_SCREEN_WEAPONS) == ES2_FLOW_DONE);
    CHECK(!strcmp(label_of(ES2_REPAIR_MODE), T(0x41)));
    fresh(2, 0);
    es2_ui_message_set(ui, W(ES2_REPAIR_MODE)->label, "kept", 2, 0x17, 0);
    CHECK(es2_repair_screen_show(&rs, ES2_SHELL_SCREEN_CREW) == ES2_FLOW_DONE);
    CHECK(!strcmp(label_of(ES2_REPAIR_MODE), "kept"));
    /* leaving */
    es2_repair_screen_hide(&rs);
    CHECK(!visible(rs.ids[ES2_REPAIR_PANEL]) && !visible(rs.ids[ES2_REPAIR_EXTERNAL]) && !visible(rs.ids[ES2_REPAIR_INTERNAL]));
}

static void test_rows_and_repairs(void) {
    uint32_t before;
    es2_shell_editor_view v;
    rf_init(&fx);
    fresh(0, 0);
    CHECK(es2_repair_screen_show(&rs, ES2_SHELL_SCREEN_BAY) == ES2_FLOW_DONE);
    steps(2);
    /* External row 2 through the input path */
    click_row(0, 2);
    CHECK(rs.last_handler == 0x43480f && rs.last_result == ES2_FLOW_DONE);
    CHECK(session.tab == 0 && session.row == 2 && session.family == 0 && session.index == 2);
    CHECK(column(0, 0, 0)->text_colour == 0x27 && W(es2_repair_row_control(0, 0))->colour == 0x10);
    CHECK(column(0, 2, 0)->text_colour == 0x29 && W(es2_repair_row_control(0, 2))->colour == 0x29);
    CHECK(!strcmp(label_of(ES2_REPAIR_CONDITION), T(0x6c)));
    check_labels();
    /* the same row again changes nothing */
    CHECK(es2_repair_screen_press(&rs, 0x43480f) == ES2_FLOW_IGNORED);
    /* REPAIR: 25 -> 30 in both words of the group, the cost paid */
    v = library_view();
    before = salvage();
    click(ES2_REPAIR_REPAIR);
    CHECK(rs.last_handler == 0x4351bd && rs.last_result == ES2_FLOW_DONE && changed == 1 && changed_handler == 0x4351bd);
    CHECK(cond_word(0, 3) == 30 && cond_word(0, 5) == 30 && salvage() == before - (uint32_t)v.item_cost);
    CHECK(!strcmp(column(0, 2, 3)->text, "30%"));
    check_labels();
    /* an empty slot cannot be picked; a mounted one can */
    CHECK(es2_repair_screen_press(&rs, 0x434745u + 0x65u * 7) == ES2_FLOW_IGNORED && session.row == 2);
    CHECK(es2_repair_screen_press(&rs, 0x434745u + 0x65u * 6) == ES2_FLOW_DONE && session.family == 2 && session.index == 0);
    CHECK(!strcmp(label_of(ES2_REPAIR_CONDITION), T(0x6a)));
    CHECK(es2_repair_screen_press(&rs, 0x4351bd) == ES2_FLOW_DONE && cond_word(0, 23) == 80);
    CHECK(!strcmp(column(0, 6, 3)->text, "80%"));
    /* REPAIR ALL, then CANCEL back to the snapshot of the show */
    CHECK(es2_repair_screen_press(&rs, 0x4352e9) == ES2_FLOW_DONE);
    for (int w = 0; w < 22; w++) CHECK(cond_word(0, w) == 100);
    CHECK(!strcmp(column(0, 1, 3)->text, "100%") && !strcmp(column(1, 0, 3)->text, "100%"));
    CHECK(es2_repair_screen_press(&rs, 0x435403) == ES2_FLOW_DONE);
    CHECK(salvage() == 100000 && cond_word(0, 3) == 50 && cond_word(0, 23) == 64 && cond_word(0, 13) == 45);
    CHECK(!strcmp(column(0, 2, 3)->text, "25%") && !strcmp(column(0, 6, 3)->text, "64%"));
    check_labels();
    /* the empty handlers */
    CHECK(es2_repair_screen_press(&rs, 0x434693) == ES2_FLOW_IGNORED && es2_repair_screen_press(&rs, 0x4346ec) == ES2_FLOW_IGNORED);
    CHECK(es2_repair_screen_press(&rs, 0x123456) == ES2_FLOW_IGNORED);
}

static void test_budget(void) {
    uint32_t bank;
    rf_init(&fx);
    fresh(0, 0);
    bank = library_view().bank_cost;
    rf_salvage(&fx, bank); /* exactly the bank's cost: nothing available */
    fresh(0, 0);
    CHECK(es2_repair_screen_show(&rs, ES2_SHELL_SCREEN_BAY) == ES2_FLOW_DONE);
    CHECK(!W(ES2_REPAIR_REPAIR)->enabled && !W(ES2_REPAIR_REPAIR_ALL)->enabled);
    CHECK(es2_ui_get(ui, W(ES2_REPAIR_REPAIR)->label)->text_colour == 0x26);
    CHECK(!strcmp(label_of(ES2_REPAIR_SALVAGE), "0 TC8"));
    CHECK(es2_repair_screen_press(&rs, 0x4351bd) == ES2_FLOW_IGNORED && es2_repair_screen_press(&rs, 0x4352e9) == ES2_FLOW_IGNORED);
    steps(2);
    click(ES2_REPAIR_REPAIR);
    CHECK(cond_word(0, 0) == 95 && salvage() == bank);
    check_labels();
}

static void test_internal_grids(void) {
    rf_init(&fx);
    fresh(0, 0);
    CHECK(es2_repair_screen_show(&rs, ES2_SHELL_SCREEN_BAY) == ES2_FLOW_DONE);
    ncalls = 0;
    /* an Internal row: the squad grid hidden, this screen's grid of bay 0 shown */
    CHECK(es2_repair_screen_press(&rs, 0x434e30u + 0x65u * 3) == ES2_FLOW_DONE);
    CHECK(session.tab == 1 && session.row == 3 && session.family == 1 && session.index == 3);
    CHECK(called(ES2_REPAIR_SQUAD_GRID_HIDE, 0) && visible(rs.ids[ES2_REPAIR_GRID0]));
    CHECK(column(1, 3, 0)->text_colour == 0x29 && column(0, 0, 0)->text_colour == 0x27);
    /* back to External: the grids swap back */
    ncalls = 0;
    CHECK(es2_repair_screen_press(&rs, 0x434745) == ES2_FLOW_DONE);
    CHECK(called(ES2_REPAIR_SQUAD_GRID_SHOW, 0) && !visible(rs.ids[ES2_REPAIR_GRID0]));
    /* the Internal panel itself with an External row selected */
    ncalls = 0;
    CHECK(es2_repair_screen_press(&rs, 0x434d95) == ES2_FLOW_DONE);
    CHECK(called(ES2_REPAIR_SQUAD_GRID_HIDE, 0) && visible(rs.ids[ES2_REPAIR_GRID0]) && session.tab == 0);
}

static void test_bay(void) {
    es2_shell_editor_session before;
    rf_init(&fx);
    fresh(0, 0);
    CHECK(es2_repair_screen_show(&rs, ES2_SHELL_SCREEN_BAY) == ES2_FLOW_DONE);
    CHECK(es2_repair_screen_press(&rs, 0x434e30u) == ES2_FLOW_DONE); /* Internal row 0 */
    CHECK(es2_repair_screen_press(&rs, 0x4351bd) == ES2_FLOW_DONE);
    ncalls = 0;
    CHECK(es2_repair_screen_bay(&rs, 0) == 0);
    CHECK(es2_repair_screen_bay(&rs, 1) == 1);
    CHECK(session.bay == 1 && session.tab == 0 && session.row == 0 && session.conditions[13] == 51);
    CHECK(session.salvage == salvage() && salvage() < 100000); /* the repair of bay 0 stays paid */
    CHECK(!visible(rs.ids[ES2_REPAIR_GRID0]) && called(ES2_REPAIR_SQUAD_GRID_SHOW, 1));
    CHECK(called(ES2_REPAIR_SQUAD_SELECT, 0));
    /* vehicle 8's names */
    CHECK(!strcmp(column(0, 0, 0)->text, T(0x4e)) && !strcmp(column(0, 1, 0)->text, T(0x5d)) &&
          !strcmp(column(0, 5, 0)->text, T(0x61)));
    CHECK(!strcmp(column(1, 0, 0)->text, T(0x62)) && !strcmp(column(1, 1, 0)->text, T(0x63)) &&
          !strcmp(column(1, 2, 0)->text, T(0x56)));
    CHECK(!strcmp(column(0, 7, 0)->text, T(0x7d)) && !strcmp(column(0, 8, 0)->text, T(1)));
    /* 0x43436f writes the names with colour 0x27 after the selection: only the frame shows it */
    CHECK(W(es2_repair_row_control(0, 0))->colour == 0x29 && column(0, 0, 0)->text_colour == 0x27);
    CHECK(W(es2_repair_row_control(1, 0))->colour == 0x10 && column(1, 0, 0)->text_colour == 0x27);
    check_labels();
    /* back to bay 0: the ordinary names */
    before = session;
    CHECK(es2_repair_screen_bay(&rs, 0) == 1 && session.bay == 0 && before.bay == 1);
    CHECK(!strcmp(column(0, 1, 0)->text, T(0x4f)) && !strcmp(column(1, 0, 0)->text, T(0x54)));
    /* a bay without a record is no selection here */
    CHECK(es2_repair_screen_bay(&rs, 5) == 0 && session.bay == 0);
}

static void test_scrap(void) {
    uint32_t before;
    int32_t value;
    rf_init(&fx);
    fresh(0, 0);
    CHECK(es2_repair_screen_show(&rs, ES2_SHELL_SCREEN_BAY) == ES2_FLOW_DONE);
    CHECK(W(ES2_REPAIR_SCRAP)->enabled);
    value = library_view().scrap_value;
    /* the warning and its CANCEL */
    CHECK(es2_repair_screen_press(&rs, 0x4353a5) == ES2_FLOW_DONE);
    CHECK(visible(rs.box[ES2_HERC_SCRAP_BOX]) && visible(rs.box[ES2_HERC_SCRAP_SHIELD]));
    snprintf(expect, sizeof expect, "%d TCD", (int)(value / 1000));
    CHECK(!strcmp(es2_ui_get(ui, rs.box[ES2_HERC_SCRAP_TEXT])->text, expect));
    steps(2);
    click_window(rs.box[ES2_HERC_SCRAP_CANCEL]);
    CHECK(rs.last_handler == 0x4482c8 && !visible(rs.box[ES2_HERC_SCRAP_BOX]) && has_bay(0));
    /* ACCEPT: the Herc scrapped, the first finished bay selected (1, vehicle 8) */
    before = salvage();
    CHECK(es2_repair_screen_press(&rs, 0x4353a5) == ES2_FLOW_DONE);
    ncalls = 0;
    steps(2);
    click_window(rs.box[ES2_HERC_SCRAP_ACCEPT]);
    CHECK(rs.last_handler == 0x448326 && rs.last_result == ES2_FLOW_DONE && changed_handler == 0x448326);
    CHECK(!visible(rs.box[ES2_HERC_SCRAP_BOX]) && !has_bay(0) && salvage() == before + (uint32_t)value);
    CHECK(session.bay == 1 && session.salvage == salvage());
    CHECK(called(ES2_REPAIR_SQUAD_REFRESH, 1) && called(ES2_REPAIR_SQUAD_SELECT, 0) && called(ES2_REPAIR_SQUAD_REDRAW, 1));
    CHECK(!strcmp(column(0, 1, 0)->text, T(0x5d)));
    check_labels();
    /* bay 1 and 2 left, both functional: the scrap of bay 1 is allowed; then one is left */
    CHECK(W(ES2_REPAIR_SCRAP)->enabled);
    CHECK(es2_repair_screen_press(&rs, 0x4353a5) == ES2_FLOW_DONE && es2_repair_screen_press(&rs, 0x448326) == ES2_FLOW_DONE);
    CHECK(session.bay == 2 && !W(ES2_REPAIR_SCRAP)->enabled);
    CHECK(es2_repair_screen_press(&rs, 0x4353a5) == ES2_FLOW_IGNORED && !visible(rs.box[ES2_HERC_SCRAP_BOX]));
}

/* With a squad panel that stores the bay itself: the show selects through it in the
 * previous word's case, the scrap's selection runs REPAIR's part through
 * es2_repair_screen_selected; the state equals the one without it. */
static void test_panel_host(void) {
    uint8_t *alone;
    size_t alone_size;
    es2_shell_editor_session alone_session;
    rf_init(&fx);
    rf_w(fx.bays[0] + 70, 40); /* bay 0 unfinished: the show selects bay 1 */
    fresh(0, 0);
    CHECK(es2_repair_screen_show(&rs, ES2_SHELL_SCREEN_WEAPONS) == ES2_FLOW_DONE);
    CHECK(session.bay == 1 && called(ES2_REPAIR_SQUAD_SELECT, 0));
    CHECK(es2_repair_screen_press(&rs, 0x434e30u) == ES2_FLOW_DONE);
    CHECK(es2_repair_screen_press(&rs, 0x4353a5) == ES2_FLOW_DONE && es2_repair_screen_press(&rs, 0x448326) == ES2_FLOW_DONE);
    CHECK(session.bay == 2);
    alone_size = save.size;
    alone = malloc(alone_size);
    memcpy(alone, save.data, alone_size);
    alone_session = session;
    rf_init(&fx);
    rf_w(fx.bays[0] + 70, 40);
    fresh(0, 0);
    panel_host = 1;
    panel_count = 0;
    CHECK(es2_repair_screen_show(&rs, ES2_SHELL_SCREEN_WEAPONS) == ES2_FLOW_DONE);
    CHECK(session.bay == 1 && session.screen == ES2_SHELL_SCREEN_REPAIR && panel_count == 1 && panel_words[0] == 2);
    CHECK(!strcmp(column(0, 1, 0)->text, T(0x4f))); /* no REPAIR case during the show: the names stay */
    CHECK(es2_repair_screen_press(&rs, 0x434e30u) == ES2_FLOW_DONE);
    CHECK(es2_repair_screen_press(&rs, 0x4353a5) == ES2_FLOW_DONE && es2_repair_screen_press(&rs, 0x448326) == ES2_FLOW_DONE);
    CHECK(session.bay == 2 && panel_count == 2 && panel_words[1] == ES2_SHELL_SCREEN_REPAIR);
    CHECK(session.tab == 0 && session.row == 0 && session.screen == ES2_SHELL_SCREEN_REPAIR);
    CHECK(save.size == alone_size && !memcmp(save.data, alone, alone_size));
    CHECK(!memcmp(&session, &alone_session, sizeof session));
    check_labels();
    /* the panel's own selector on word 3 (the service bay stores the bay, then
     * host.selected) */
    rf_init(&fx);
    fresh(0, 0);
    CHECK(es2_repair_screen_show(&rs, ES2_SHELL_SCREEN_BAY) == ES2_FLOW_DONE);
    CHECK(es2_repair_screen_press(&rs, 0x434e30u) == ES2_FLOW_DONE);
    session.bay = 1;
    CHECK(es2_repair_screen_selected(&rs) == 1 && session.conditions[13] == 51 && session.salvage == salvage());
    CHECK(session.tab == 0 && session.row == 0 && session.screen == ES2_SHELL_SCREEN_REPAIR);
    CHECK(!strcmp(column(0, 1, 0)->text, T(0x5d)) && W(es2_repair_row_control(0, 0))->colour == 0x29);
    check_labels();
    CHECK(es2_repair_screen_grid(&rs, 3) == rs.ids[3] && es2_repair_screen_grid(&rs, 8) == 0);
    panel_host = 0;
    free(alone);
}

static void test_no_bay(void) {
    rf_init(&fx);
    for (int k = 0; k < 3; k++) rf_w(fx.bays[k] + 70, 40); /* no finished Herc */
    fresh(0, 0);
    CHECK(es2_repair_screen_show(&rs, ES2_SHELL_SCREEN_BAY) == ES2_FLOW_DONE);
    CHECK(session.bay == -1);
    CHECK(!W(ES2_REPAIR_REPAIR)->enabled && !W(ES2_REPAIR_REPAIR_ALL)->enabled && !W(ES2_REPAIR_SCRAP)->enabled);
    CHECK(!strcmp(column(0, 0, 3)->text, "100%") && !strcmp(column(0, 6, 0)->text, T(1)));
    CHECK(!strcmp(label_of(ES2_REPAIR_COST), "0 TC8"));
    /* the library refuses what would read a pilot as a record */
    CHECK(es2_repair_screen_press(&rs, 0x435403) == ES2_FLOW_REFUSED && rs.status == ES2_ERR_OFFSET);
    CHECK(es2_repair_screen_press(&rs, 0x434745u + 0x65u * 6) == ES2_FLOW_REFUSED);
    CHECK(es2_repair_screen_press(&rs, 0x434745u + 0x65u * 1) == ES2_FLOW_DONE);
    CHECK(es2_repair_screen_press(&rs, 0x434d95) == ES2_FLOW_IGNORED);
    CHECK(es2_repair_screen_bay(&rs, 0) == 0);
    /* a broken state: FAILED and nothing shown changes */
    rs.vfs = NULL;
    CHECK(es2_repair_screen_show(&rs, ES2_SHELL_SCREEN_MISSION) == ES2_FLOW_FAILED);
}

int main(void) {
    if (mmf_font(font_bytes, &font)) {
        printf("FAIL font\n");
        return 1;
    }
    test_build();
    test_show();
    test_rows_and_repairs();
    test_budget();
    test_internal_grids();
    test_bay();
    test_scrap();
    test_panel_host();
    test_no_bay();
    if (ui) es2_ui_release(ui);
    free(ui);
    free(shell);
    free(save.data);
    printf("%u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
