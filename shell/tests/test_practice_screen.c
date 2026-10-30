/* The practice screen (es2/shell/gui/practice_screen.h) on synthetic
 * resources, a synthetic executable table and a synthetic flow host: the
 * build from the layout table with the value labels, the show with entry 0, the
 * entries through the toolkit's input path (rows, Herc Type, the table's
 * byte), the five parameter buttons with left and right releases, Main Menu
 * without revert, Begin Mission (commit, write, launch, exit 2) and its
 * refusals, PRACTICE MISSIONS from the main menu. */
#include "es2/shell/gui/practice_screen.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "main_menu_fixture.h"

static unsigned checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static uint8_t font_bytes[16384];
static es2_dfn font;
static uint8_t images[5][640 * 480];
static es2_bitmap bitmaps[5];
/* Synthetic stand-in for the executable's tables (not VSHELL's values):
 * Herc type of positions 0..10 (words), then two names. */
static uint8_t tables[ES2_SHELL_PRACTICE_TABLES_SIZE];

typedef struct host_log {
    int launches, launch_result, writes, stores, changed, entries;
    uint32_t changed_handler;
    es2_shell_flow_result changed_result;
    es2_shell_practice_launch launch;
    uint8_t written[ES2_SHELL_PREFS_SIZE];
} host_log;
static host_log hl;
static es2_gui_shell *shell;
static es2_ui *ui;
static es2_shell_flow flow;
static es2_shell_practice practice;
static es2_main_menu menu;
static es2_practice_screen ps;

static const es2_dfn *h_font(void *c, uint32_t g) { (void)c; return g == 0x46ddfc || g == 0x46de00 ? &font : NULL; }
static const es2_bitmap *h_bitmap(void *c, const char *file) {
    (void)c;
    for (int k = 0; k < 5; k++)
        if (!strcmp(file, es2_menu_background_file(k))) return &bitmaps[k];
    return NULL;
}
static const char *h_text(void *c, uint32_t id) {
    static char t[64][16];
    static unsigned n;
    char *s = t[n++ & 63];
    (void)c;
    snprintf(s, sizeof t[0], "T%X", (unsigned)id);
    return s;
}
static const char *h_literal(void *c, uint32_t a) { (void)c; return a == 0x479c31 ? ":" : ""; }
static int h_launch(void *c, const es2_shell_practice_launch *l) {
    (void)c;
    hl.launches++;
    hl.launch = *l;
    return hl.launch_result;
}
static void h_changed(void *c, uint32_t h, es2_shell_flow_result r) {
    (void)c;
    hl.changed++;
    hl.changed_handler = h;
    hl.changed_result = r;
}
/* The main menu's PRACTICE MISSIONS host step: the practice show. */
static int h_entry(void *c, uint32_t h, int stage) {
    (void)c;
    if (h == ES2_FLOW_PRACTICE_MISSIONS && stage == 0) {
        hl.entries++;
        es2_practice_screen_show(&ps);
    }
    return 1;
}
static int f_store(void *c, uint8_t mode) { (void)c; (void)mode; hl.stores++; return 1; }
static int f_write(void *c, const uint8_t b[ES2_SHELL_PREFS_SIZE]) {
    (void)c;
    hl.writes++;
    memcpy(hl.written, b, ES2_SHELL_PREFS_SIZE);
    return hl.writes < 100;
}

static void fresh(const uint8_t *prefs, size_t prefs_size) {
    es2_shell_flow_host fh;
    es2_span pf = {prefs, prefs_size};
    if (ui) es2_ui_release(ui);
    free(ui);
    free(shell);
    memset(&hl, 0, sizeof hl);
    hl.launch_result = 1;
    shell = calloc(1, sizeof *shell);
    ui = calloc(1, sizeof *ui);
    es2_gui_shell_init(shell, NULL, NULL, NULL);
    CHECK(es2_ui_init(ui, shell) == 0);
    es2_ui_install(ui, &shell->hooks);
    memset(&fh, 0, sizeof fh);
    fh.store_mode = f_store;
    fh.write_prefs = f_write;
    es2_shell_flow_init(&flow, &fh, prefs ? &pf : NULL);
    es2_shell_practice_open(&practice);
    memset(&menu, 0, sizeof menu);
    menu.flow = &flow;
    menu.host = (es2_main_menu_host){NULL, h_font, h_bitmap, h_text, NULL, NULL, h_entry, NULL, NULL};
    CHECK(es2_screen_main_menu_build(ui, &menu) == 0);
    memset(&ps, 0, sizeof ps);
    ps.flow = &flow;
    ps.practice = &practice;
    ps.tables = tables;
    ps.menu = &menu;
    ps.host = (es2_practice_screen_host){NULL, h_font, h_bitmap, h_text, h_literal, h_launch, h_changed};
    const es2_ui_screen_entry *e = es2_ui_screen_find(ES2_UI_SCREEN_PRACTICE);
    CHECK(e && e->build(ui, &ps) == 0);
    es2_ui_show(ui, ui->root);
}
static void step(void) {
    ui->now += 100;
    es2_gui_step(shell);
}
static void steps(int n) { while (n-- > 0) step(); }
static es2_ui_widget *W(int control) { return es2_ui_get(ui, ps.ids[control]); }
static int visible(es2_ui_id id) {
    es2_ui_widget *w = es2_ui_get(ui, id);
    return w && !(w->flags & ES2_UI_HIDDEN);
}
static const char *label_of(int control) { return es2_ui_get(ui, W(control)->label)->text; }
static void click_at(int control, int right) {
    es2_ui_widget *w = W(control);
    int x = (w->abs.left + w->abs.right) / 2, y = (w->abs.top + w->abs.bottom) / 2;
    es2_gui_on_mouse(shell, ES2_GUI_WM_MOUSEMOVE, x, y, 0, 0);
    step();
    es2_gui_on_mouse(shell, right ? ES2_GUI_WM_RBUTTONDOWN : ES2_GUI_WM_LBUTTONDOWN, x, y, !right, right);
    step();
    es2_gui_on_mouse(shell, right ? ES2_GUI_WM_RBUTTONUP : ES2_GUI_WM_LBUTTONUP, x, y, 0, 0);
    step();
}
static void click(int control) { click_at(control, 0); }
static int row(int i) { return ES2_PRACTICE_ENTRY0 + 5 * i; }
static int32_t row_colour(int i) { return es2_ui_get(ui, W(row(i))->columns[0])->text_colour; }
/* The practice screen as PRACTICE MISSIONS opens it: the show, then the flow (mode 0). */
static void open_screen(void) {
    es2_main_menu_hide(&menu);
    es2_practice_screen_show(&ps);
    CHECK(es2_shell_flow_event(&flow, ES2_FLOW_PRACTICE_MISSIONS, NULL) == ES2_FLOW_DONE);
    steps(2);
}
static char expect[16];
static const char *T(unsigned id) { snprintf(expect, sizeof expect, "T%X", id); return expect; }

static void build(void) {
    uint8_t prefs[ES2_SHELL_PREFS_SIZE];
    memset(prefs, 0, sizeof prefs);
    prefs[0x25] = 1; prefs[0x26] = 0; prefs[0x27] = 3; prefs[0x28] = 6; prefs[0x29] = 1;
    fresh(prefs, sizeof prefs);
    for (int k = 0; k < ES2_PRACTICE_CONTROLS; k++) CHECK(ps.ids[k] != 0);
    CHECK(W(ES2_PRACTICE_BACKGROUND)->cls == ES2_UI_BITMAP && W(ES2_PRACTICE_BACKGROUND)->bitmap == &bitmaps[4]);
    CHECK(W(ES2_PRACTICE_PANEL)->cls == ES2_UI_TITLE);
    static const struct { int control; uint32_t handler; unsigned label; } b[7] = {
        {5, 0x44c5b9, 0xfb}, {7, 0x44c676, 0xfc}, {9, 0x44c733, 0xfd}, {11, 0x44c7f0, 0xfe}, {13, 0x44c8ad, 0xff},
        {31, 0x44c96a, 0x100}, {33, 0x44ca26, 0x102}};
    for (int i = 0; i < 7; i++) {
        es2_ui_widget *w = W(b[i].control);
        CHECK(w->cls == ES2_UI_BUTTON_FONT && w->original_handler == b[i].handler && w->handler != NULL);
        CHECK(w->label == ps.ids[b[i].control + 1] && !strcmp(label_of(b[i].control), T(b[i].label)));
    }
    for (int i = 0; i < 8; i++) {
        es2_ui_widget *q = W(row(i));
        CHECK(q->cls == ES2_UI_QUAD && q->original_handler == 0x44caa3u + 0x61u * (unsigned)i && q->handler);
        CHECK(q->enabled == 1 && row_colour(i) == 0x27);
        CHECK(!strcmp(es2_ui_get(ui, q->columns[0])->text, T(0xf2 + (unsigned)i)));
    }
    /* the value buttons: disabled, labels base + byte of the current block */
    static const struct { int control; unsigned id; } v[5] = {
        {21, 0x128 + 0}, {23, 0x12a + 1}, {25, 0x35 + 3}, {27, 0x12c + 1}, {29, 0x6e + 6}};
    for (int i = 0; i < 5; i++) {
        es2_ui_widget *w = W(v[i].control), *l = es2_ui_get(ui, w->label);
        CHECK(w->enabled == 0 && w->handler == NULL && !strcmp(l->text, T(v[i].id)));
        CHECK(l->text_colour == 0x17 && l->opaque == 1 && l->align == 2);
    }
    CHECK(!strcmp(W(16)->text, ":") && W(17)->text[0] == 0);
    CHECK(!visible(ps.ids[ES2_PRACTICE_BACKGROUND]) && !visible(ps.ids[ES2_PRACTICE_PANEL]));
    CHECK(es2_screen_practice_build(NULL, &ps) == -1 && es2_screen_practice_build(ui, NULL) == -1);
}

static void show_and_entries(void) {
    fresh(NULL, 0);
    open_screen();
    CHECK(visible(ps.ids[ES2_PRACTICE_BACKGROUND]) && visible(ps.ids[ES2_PRACTICE_PANEL]));
    CHECK(visible(ps.ids[row(7)]) && visible(ps.ids[ES2_PRACTICE_BEGIN]));
    CHECK(practice.index == 0 && flow.prefs.current[0x28] == tables[0] && flow.mode == 0);
    CHECK(row_colour(0) == 0x29 && row_colour(1) == 0x27 && W(row(0))->colour == 0x10);
    es2_ui_widget *herc = W(ES2_PRACTICE_HERC);
    CHECK(herc->enabled == 0 && herc->colour == 0x26 && es2_ui_get(ui, herc->label)->text_colour == 0x26);
    CHECK(!strcmp(label_of(ES2_PRACTICE_VALUE_HERC), T(0x6e + tables[0])));
    /* Herc Type while disabled: no change */
    uint8_t before = flow.prefs.current[0x28];
    click(ES2_PRACTICE_HERC);
    CHECK(flow.prefs.current[0x28] == before && ps.events == 0);
    /* entry 5 */
    click(row(5));
    CHECK(ps.last_handler == 0x44cc88 && ps.last_result == ES2_FLOW_DONE && hl.changed == 0);
    CHECK(practice.index == 5 && flow.prefs.current[0x28] == tables[10]);
    CHECK(row_colour(0) == 0x27 && row_colour(5) == 0x29);
    CHECK(herc->enabled == 1 && herc->colour == 0x22 && es2_ui_get(ui, herc->label)->text_colour == 0x29);
    CHECK(!strcmp(label_of(ES2_PRACTICE_VALUE_HERC), T(0x6e + tables[10])));
    /* Herc Type now: forward, back, wrap at 0 */
    flow.prefs.current[0x28] = 8;
    click(ES2_PRACTICE_HERC);
    CHECK(flow.prefs.current[0x28] == 0 && !strcmp(label_of(ES2_PRACTICE_VALUE_HERC), T(0x6e)));
    click_at(ES2_PRACTICE_HERC, 1);
    CHECK(flow.prefs.current[0x28] == 8 && !strcmp(label_of(ES2_PRACTICE_VALUE_HERC), T(0x6e + 8)));
    /* the selected entry again: nothing at all (the hand-set byte stays) */
    flow.prefs.current[0x28] = 2;
    click(row(5));
    CHECK(ps.last_result == ES2_FLOW_IGNORED && flow.prefs.current[0x28] == 2 && row_colour(5) == 0x29);
    /* entry 2: Herc Type disabled again */
    click(row(2));
    CHECK(practice.index == 2 && row_colour(5) == 0x27 && row_colour(2) == 0x29 && herc->enabled == 0);
    /* entry 3 (the last below 4): still disabled; entry 4 (the first above): enabled */
    click(row(3));
    CHECK(practice.index == 3 && herc->enabled == 0 && herc->colour == 0x26);
    click(row(4));
    CHECK(practice.index == 4 && herc->enabled == 1 && herc->colour == 0x22 && row_colour(3) == 0x27);
    click(row(2));
    CHECK(es2_practice_screen_entry(&ps, 8) == -1 && es2_practice_screen_entry(&ps, -1) == -1);
    /* a second show keeps the selection (entry 0 only when not selected) */
    es2_practice_screen_hide(&ps);
    CHECK(!visible(ps.ids[ES2_PRACTICE_PANEL]));
    es2_practice_screen_show(&ps);
    CHECK(practice.index == 0 && row_colour(2) == 0x27 && row_colour(0) == 0x29);
}

static void parameters(void) {
    fresh(NULL, 0);
    open_screen();
    /* Damage: left forward with wrap, right back with wrap */
    click(ES2_PRACTICE_DAMAGE);
    CHECK(flow.prefs.current[0x26] == 1 && !strcmp(label_of(ES2_PRACTICE_VALUE_DAMAGE), T(0x129)));
    CHECK(ps.last_handler == 0x44c5b9 && ps.last_result == ES2_FLOW_DONE);
    click(ES2_PRACTICE_DAMAGE);
    CHECK(flow.prefs.current[0x26] == 0);
    click_at(ES2_PRACTICE_DAMAGE, 1);
    CHECK(flow.prefs.current[0x26] == 1 && !strcmp(label_of(ES2_PRACTICE_VALUE_DAMAGE), T(0x129)));
    /* Ammo, Time */
    click(ES2_PRACTICE_AMMO);
    CHECK(flow.prefs.current[0x25] == 1 && !strcmp(label_of(ES2_PRACTICE_VALUE_AMMO), T(0x12b)));
    click_at(ES2_PRACTICE_TIME, 1);
    CHECK(flow.prefs.current[0x29] == 1 && !strcmp(label_of(ES2_PRACTICE_VALUE_TIME), T(0x12d)));
    /* Difficulty: modulus 4; a byte above it grows without wrapping */
    for (int k = 0; k < 4; k++) click(ES2_PRACTICE_DIFFICULTY);
    CHECK(flow.prefs.current[0x27] == 0);
    flow.prefs.current[0x27] = 7;
    click(ES2_PRACTICE_DIFFICULTY);
    CHECK(flow.prefs.current[0x27] == 8 && !strcmp(label_of(ES2_PRACTICE_VALUE_DIFFICULTY), T(0x35 + 8)));
    click_at(ES2_PRACTICE_DIFFICULTY, 1);
    CHECK(flow.prefs.current[0x27] == 7);
    /* only the current block changes */
    CHECK(flow.prefs.committed[0x26] == 0 && flow.prefs.committed[0x27] == 0 && hl.writes == 0);
    /* direct press: a code other than left steps back */
    CHECK(es2_practice_screen_press(&ps, 0x44c733, 0) == ES2_FLOW_DONE && flow.prefs.current[0x27] == 6);
    CHECK(es2_practice_screen_press(&ps, 0x44c9cd, 1) == ES2_FLOW_IGNORED);
}

static void main_menu_button(void) {
    fresh(NULL, 0);
    open_screen();
    click(ES2_PRACTICE_DAMAGE);
    click(ES2_PRACTICE_MAIN_MENU);
    CHECK(ps.last_handler == 0x44c96a && ps.last_result == ES2_FLOW_DONE);
    CHECK(hl.changed == 1 && hl.changed_handler == 0x44c96a && flow.screen == ES2_FLOW_MAIN_MENU);
    CHECK(!visible(ps.ids[ES2_PRACTICE_PANEL]) && !visible(ps.ids[ES2_PRACTICE_BACKGROUND]));
    CHECK(visible(menu.ids[ES2_MAIN_MENU_PANEL]) && visible(menu.ids[ES2_MAIN_MENU_BACKGROUND]));
    /* no revert, no write */
    CHECK(flow.prefs.current[0x26] == 1 && flow.prefs.committed[0x26] == 0 && hl.writes == 0);
    /* PRACTICE MISSIONS from the main menu: the host's show */
    step();
    es2_ui_widget *p = es2_ui_get(ui, menu.ids[ES2_MAIN_MENU_PRACTICE]);
    int x = (p->abs.left + p->abs.right) / 2, y = (p->abs.top + p->abs.bottom) / 2;
    es2_gui_on_mouse(shell, ES2_GUI_WM_MOUSEMOVE, x, y, 0, 0);
    step();
    es2_gui_on_mouse(shell, ES2_GUI_WM_LBUTTONDOWN, x, y, 1, 0);
    step();
    es2_gui_on_mouse(shell, ES2_GUI_WM_LBUTTONUP, x, y, 0, 0);
    steps(2);
    CHECK(hl.entries == 1 && flow.screen == ES2_FLOW_PRACTICE && visible(ps.ids[ES2_PRACTICE_PANEL]));
    CHECK(!visible(menu.ids[ES2_MAIN_MENU_PANEL]) && flow.prefs.current[0x26] == 1);
}

static void begin_mission(void) {
    /* without a selection: refused, nothing written */
    fresh(NULL, 0);
    CHECK(es2_practice_screen_press(&ps, ES2_PRACTICE_BEGIN_HANDLER, 1) == ES2_FLOW_REFUSED);
    CHECK(hl.writes == 0 && hl.launches == 0 && hl.changed == 1 && hl.changed_result == ES2_FLOW_REFUSED);
    /* launched */
    fresh(NULL, 0);
    open_screen();
    click(row(6));
    click(ES2_PRACTICE_DIFFICULTY);
    click(ES2_PRACTICE_DIFFICULTY);
    click(ES2_PRACTICE_AMMO);
    click(ES2_PRACTICE_BEGIN);
    CHECK(ps.last_handler == 0x44ca26 && ps.last_result == ES2_FLOW_DONE && hl.changed_handler == 0x44ca26);
    CHECK(hl.writes == 1 && !memcmp(hl.written, flow.prefs.current, ES2_SHELL_PREFS_SIZE));
    CHECK(!memcmp(flow.prefs.committed, flow.prefs.current, ES2_SHELL_PREFS_SIZE));
    CHECK(hl.launches == 1 && !strcmp(hl.launch.name, "SYNTH") && hl.launch.rank == 2 && hl.launch.position == 6);
    CHECK(hl.launch.preset_index == 6 && hl.launch.preset_flag == 0 && hl.launch.controls[0] == 1 &&
          hl.launch.controls[3] == tables[12]);
    CHECK(flow.quit == 1 && flow.exit_code == ES2_FLOW_EXIT_DBSIM && flow.game == ES2_FLOW_GAME_SLOT &&
          flow.final_word == 2);
    CHECK(visible(ps.ids[ES2_PRACTICE_PANEL])); /* 0x44ca26 does not hide */
    /* the launch fails: the main menu (own) */
    fresh(NULL, 0);
    open_screen();
    hl.launch_result = 0;
    click(ES2_PRACTICE_BEGIN);
    CHECK(ps.last_result == ES2_FLOW_FAILED && flow.quit == 0 && flow.screen == ES2_FLOW_MAIN_MENU);
    CHECK(hl.writes == 1 && !visible(ps.ids[ES2_PRACTICE_PANEL]) && visible(menu.ids[ES2_MAIN_MENU_PANEL]));
    /* the write fails: no launch */
    fresh(NULL, 0);
    open_screen();
    hl.writes = 99;
    click(ES2_PRACTICE_BEGIN);
    CHECK(ps.last_result == ES2_FLOW_FAILED && hl.launches == 0 && flow.screen == ES2_FLOW_MAIN_MENU);
    /* no host launch at all */
    fresh(NULL, 0);
    open_screen();
    ps.host.launch = NULL;
    CHECK(es2_practice_screen_press(&ps, ES2_PRACTICE_BEGIN_HANDLER, 1) == ES2_FLOW_FAILED && flow.quit == 0);
}

int main(void) {
    if (mmf_font(font_bytes, &font)) {
        printf("FAIL font\n");
        return 1;
    }
    for (int k = 0; k < 5; k++) {
        mmf_image(images[k], k);
        bitmaps[k] = (es2_bitmap){480, 640, 0, images[k]};
    }
    for (int i = 0; i < 12; i++) tables[2 * i] = (uint8_t)((i * 5 + 3) % 9);
    memcpy(tables + 24, "SYNTH", 6);
    memcpy(tables + 32, "QUICK", 6);
    build();
    show_and_entries();
    parameters();
    main_menu_button();
    begin_mission();
    if (ui) es2_ui_release(ui);
    free(ui);
    free(shell);
    printf("practice_screen_test: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
