/* The Saved games screen (es2/shell/gui/saved_games.h) on synthetic
 * resources, a synthetic slot table and statistics, and a synthetic flow
 * host: the build from the layout table, the show 0x43a19c, the row selection
 * 0x437fef with the enabling rules, the statistics panel 0x4377bc, naming a
 * slot through the toolkit's input path (SAVE, typing, ACCEPT, CANCEL),
 * RESTORE (VSHELL's bay for every final word), EXIT to either caller and the
 * own 80-byte refusal. */
#include "es2/shell/gui/saved_games.h"

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

typedef struct host_log {
    int saves, save_slot, loads, load_slot, load_answer, refreshes, refresh_slot, changed, campaigns;
    uint32_t changed_handler;
    char save_text[128];
    int16_t final_word;
} host_log;
static host_log hl;
static es2_gui_shell *shell;
static es2_ui *ui;
static es2_shell_flow flow;
static es2_shell_movies movies;
static es2_saved_games sg;
static es2_main_menu mm;
static es2_shell_slot_table table;
static es2_shell_stats stats[ES2_SHELL_STATS_SLOTS];

static const es2_dfn *h_font(void *c, uint32_t g) { (void)c; return g == 0x46ddfc || g == 0x46de00 ? &font : NULL; }
static const es2_bitmap *h_bitmap(void *c, const char *file) {
    (void)c;
    for (int k = 0; k < 5; k++)
        if (!strcmp(file, es2_menu_background_file(k))) return &bitmaps[k];
    return NULL;
}
static const char *h_text(void *c, uint32_t id) {
    static char t[8][16];
    static int next;
    char *s = t[next++ & 7];
    (void)c;
    snprintf(s, sizeof t[0], "T%X", (unsigned)id);
    return s;
}
static void h_refresh(void *c, int slot) { (void)c; hl.refreshes++; hl.refresh_slot = slot; }
static void h_changed(void *c, uint32_t h, es2_shell_flow_result r) { (void)c; (void)r; hl.changed++; hl.changed_handler = h; }
static int f_save(void *c, int slot, const char *text) {
    (void)c;
    hl.saves++;
    hl.save_slot = slot;
    snprintf(hl.save_text, sizeof hl.save_text, "%s", text ? text : "(null)");
    if (slot < 10 && text) { /* the host's save marks the slot (0x40e6cd) */
        snprintf(table.slot[slot].text, sizeof table.slot[slot].text, "%s", text);
        table.slot[slot].flag = 1;
    }
    return 1;
}
static int f_load(void *c, int slot, int16_t *fw) {
    (void)c;
    hl.loads++;
    hl.load_slot = slot;
    if (hl.load_answer > 0) *fw = hl.final_word;
    return hl.load_answer;
}
static int f_store(void *c, uint8_t mode) { (void)c; (void)mode; return 1; }
static int f_campaign(void *c) { (void)c; hl.campaigns++; return 1; }

static void put16(es2_shell_stats *r, int at, int v) { r->raw[at] = (uint8_t)v; r->raw[at + 1] = (uint8_t)((unsigned)v >> 8); }

/* A slot table with slots 1 and 4 used, the statistics of slot 1 and 10. */
static void fresh(int game) {
    es2_shell_flow_host fh;
    if (ui) es2_ui_release(ui);
    free(ui);
    free(shell);
    memset(&hl, 0, sizeof hl);
    hl.load_answer = 1;
    hl.final_word = 2;
    shell = calloc(1, sizeof *shell);
    ui = calloc(1, sizeof *ui);
    es2_gui_shell_init(shell, NULL, NULL, NULL);
    CHECK(es2_ui_init(ui, shell) == 0);
    es2_ui_install(ui, &shell->hooks);
    memset(&table, 0, sizeof table);
    for (int k = 0; k < ES2_SHELL_SLOT_COUNT; k++) {
        snprintf(table.slot[k].file, sizeof table.slot[k].file, "GAME_%d.SAV", k);
        snprintf(table.slot[k].text, sizeof table.slot[k].text, "%2d. EMPTY", k + 1);
    }
    snprintf(table.slot[1].text, sizeof table.slot[1].text, " 2. ALPHA");
    table.slot[1].flag = 1;
    snprintf(table.slot[4].text, sizeof table.slot[4].text, " 5. BRAVO");
    table.slot[4].flag = 1;
    memset(stats, 0, sizeof stats);
    memcpy(stats[1].raw + 4, "MAVERICK", 9);
    put16(&stats[1], 0x25, 2);    /* skill: ESTEXT 0x37 */
    put16(&stats[1], 0x29, 3);    /* rank: 0x3c */
    for (int i = 0; i < 6; i++) put16(&stats[1], 0x2d + 2 * i, 10 + i);
    stats[1].raw[0x3b] = 0x40; stats[1].raw[0x3c] = 0xe2; stats[1].raw[0x3d] = 0x01; /* 123456 -> 123 */
    put16(&stats[1], 0x3f, 4);    /* sector: 0x7a */
    put16(&stats[1], 0x41, 6);    /* mission 7 */
    memcpy(stats[10].raw + 4, "CAMPAIGN", 9);
    memset(&fh, 0, sizeof fh);
    fh.store_mode = f_store;
    fh.save = f_save;
    fh.load = f_load;
    fh.campaign = f_campaign;
    es2_shell_flow_init(&flow, &fh, NULL);
    flow.mode = 1;
    if (game) { flow.game = ES2_FLOW_GAME_CAMPAIGN; flow.final_word = 2; }
    es2_shell_movies_init(&movies, 1);
    movies.group_shown = 1;
    memset(&sg, 0, sizeof sg);
    sg.flow = &flow;
    sg.movies = &movies;
    sg.table = &table;
    sg.stats = stats;
    sg.host = (es2_saved_games_host){NULL, h_font, h_bitmap, h_text, h_refresh, h_changed};
    const es2_ui_screen_entry *e = es2_ui_screen_find(ES2_UI_SCREEN_SAVED_GAMES);
    CHECK(e && e->build(ui, &sg) == 0);
    memset(&mm, 0, sizeof mm);
    mm.flow = &flow;
    mm.host = (es2_main_menu_host){NULL, h_font, h_bitmap, h_text, NULL, NULL, NULL, NULL, NULL};
    CHECK(es2_screen_main_menu_build(ui, &mm) == 0);
    sg.main_menu = &mm;
    es2_ui_show(ui, ui->root);
}
static void step(int n) { for (int i = 0; i < n; i++) { ui->now += 100; (void)es2_gui_step(shell); } }
static es2_ui_widget *W(int control) { return es2_ui_get(ui, sg.ids[control]); }
static es2_ui_widget *row(int slot) { return W(ES2_SAVED_GAMES_ROW_FIRST + slot); }
static int visible(es2_ui_id id) { es2_ui_widget *w = es2_ui_get(ui, id); return w && !(w->flags & ES2_UI_HIDDEN); }
static int enabled(int control) { return W(control)->enabled; }
static int looks_on(int control) {
    es2_ui_widget *b = W(control), *l = es2_ui_get(ui, b->label);
    return b->enabled == 1 && b->colour == 0x22 && l && l->text_colour == 0x29;
}
static int looks_off(int control) {
    es2_ui_widget *b = W(control), *l = es2_ui_get(ui, b->label);
    return b->enabled == 0 && b->colour == 0x26 && l && l->text_colour == 0x26;
}
static void click(es2_ui_id id) {
    es2_ui_widget *w = es2_ui_get(ui, id);
    int x = (w->abs.left + w->abs.right) / 2, y = (w->abs.top + w->abs.bottom) / 2;
    es2_gui_on_mouse(shell, ES2_GUI_WM_MOUSEMOVE, x, y, 0, 0);
    step(1);
    es2_gui_on_mouse(shell, ES2_GUI_WM_LBUTTONDOWN, x, y, 1, 0);
    step(1);
    es2_gui_on_mouse(shell, ES2_GUI_WM_LBUTTONUP, x, y, 0, 0);
    step(1);
}
static void click_control(int control) { click(sg.ids[control]); }
static void key(unsigned vk, int shift) {
    es2_gui_on_key(shell, vk, 0, shift, 0, 0);
    step(1);
    es2_gui_on_key(shell, vk, 1, shift, 0, 0);
    step(1);
}
/* Capitals, digits and space through the key path. */
static void type(const char *s) {
    for (; *s; s++) key((unsigned char)*s, *s >= 'A' && *s <= 'Z');
}
static const char *value(int control) { return W(control)->text; }
/* The main menu's SAVE/RESTORE path: caller 0, then the show. */
static void enter_from_menu(void) {
    CHECK(es2_shell_flow_event(&flow, ES2_FLOW_SAVE_RESTORE, NULL) == ES2_FLOW_DONE);
    es2_saved_games_show(&sg);
    step(2);
}

static void build(void) {
    fresh(0);
    for (int k = 0; k < ES2_SAVED_GAMES_CONTROLS; k++) CHECK(sg.ids[k] != 0);
    for (int k = 0; k < 10; k++) {
        es2_ui_widget *r = row(k);
        CHECK(r->cls == ES2_UI_DIALOG && r->original_handler == ES2_SAVED_GAMES_ROW_HANDLER(k));
        CHECK(r->edit.allowed == es2_shell_edit_set_slots && r->edit.prefix == 4 && r->edit.colour == 0x27);
        CHECK(r->editable == 0 && r->edit.blink == 0);
        CHECK(r->edit.rect.r - r->edit.rect.l == (k == 0 || k == 9 ? 325 : 327));
    }
    CHECK(ES2_SAVED_GAMES_ROW_HANDLER(9) == 0x438bdc);
    CHECK(W(ES2_SAVED_GAMES_SAVE)->original_handler == 0x438263 && W(ES2_SAVED_GAMES_RESTORE)->original_handler == 0x438393);
    CHECK(W(ES2_SAVED_GAMES_EXIT)->original_handler == 0x438424 && W(ES2_SAVED_GAMES_CANCEL)->original_handler == 0x4384aa);
    CHECK(W(ES2_SAVED_GAMES_ACCEPT)->original_handler == 0x43868a);
    CHECK(W(ES2_SAVED_GAMES_BACKGROUND)->bitmap == &bitmaps[4]);
    CHECK(!visible(sg.ids[ES2_SAVED_GAMES_BACKGROUND]) && !visible(sg.ids[ES2_SAVED_GAMES_PANEL]));
    CHECK(es2_screen_saved_games_build(NULL, &sg) == -1 && es2_screen_saved_games_build(ui, NULL) == -1);
}

static void show_without_game(void) {
    fresh(0);
    enter_from_menu();
    CHECK(flow.screen == ES2_FLOW_SAVED_GAMES && flow.caller == 0 && flow.slot == 10);
    CHECK(visible(sg.ids[ES2_SAVED_GAMES_BACKGROUND]) && visible(sg.ids[ES2_SAVED_GAMES_PANEL]));
    CHECK(visible(sg.ids[ES2_SAVED_GAMES_STATS]) && !visible(sg.ids[ES2_SAVED_GAMES_SPARE]));
    CHECK(visible(sg.ids[ES2_SAVED_GAMES_ROW_FIRST]) && W(ES2_SAVED_GAMES_ROWS)->enabled == 0);
    CHECK(!strcmp(row(1)->edit.text, " 2. ALPHA") && !strcmp(row(9)->edit.text, "10. EMPTY"));
    CHECK(looks_off(ES2_SAVED_GAMES_ACCEPT) && looks_off(ES2_SAVED_GAMES_CANCEL));
    CHECK(looks_off(ES2_SAVED_GAMES_SAVE) && looks_off(ES2_SAVED_GAMES_RESTORE));
    CHECK(enabled(ES2_SAVED_GAMES_EXIT));
    /* slot 10 without a game: ESTEXT 0 in each value */
    CHECK(!strcmp(value(ES2_SAVED_GAMES_NAME), "T0") && !strcmp(value(ES2_SAVED_GAMES_MISSION), "T0"));
    CHECK(!strcmp(value(ES2_SAVED_GAMES_HERC + 1), "T0") && !strcmp(value(ES2_SAVED_GAMES_SALVAGE), "T0"));
    /* a used row: highlighted, RESTORE enabled, SAVE not (no game), its statistics */
    click(sg.ids[ES2_SAVED_GAMES_ROW_FIRST + 1]);
    CHECK(flow.slot == 1 && row(1)->edit.colour == 0x29);
    CHECK(looks_on(ES2_SAVED_GAMES_RESTORE) && looks_off(ES2_SAVED_GAMES_SAVE));
    CHECK(!strcmp(value(ES2_SAVED_GAMES_NAME), "MAVERICK") && !strcmp(value(ES2_SAVED_GAMES_SKILL), "T37"));
    CHECK(!strcmp(value(ES2_SAVED_GAMES_RANK), "T3C") && !strcmp(value(ES2_SAVED_GAMES_SECTOR), "T7A"));
    CHECK(!strcmp(value(ES2_SAVED_GAMES_SALVAGE), "123 T2F") && !strcmp(value(ES2_SAVED_GAMES_MISSION), "7"));
    /* the six kill words in the widget order +0x2d, +0x33, +0x2f, +0x35, +0x31, +0x37 */
    CHECK(!strcmp(value(ES2_SAVED_GAMES_HERC), "10") && !strcmp(value(ES2_SAVED_GAMES_HERC + 1), "13"));
    CHECK(!strcmp(value(ES2_SAVED_GAMES_FLYER), "11") && !strcmp(value(ES2_SAVED_GAMES_FLYER + 1), "14"));
    CHECK(!strcmp(value(ES2_SAVED_GAMES_BASE), "12") && !strcmp(value(ES2_SAVED_GAMES_BASE + 1), "15"));
    /* an empty row: the old row back to 0x27, RESTORE disabled, ESTEXT 0 */
    click(sg.ids[ES2_SAVED_GAMES_ROW_FIRST + 6]);
    CHECK(flow.slot == 6 && row(1)->edit.colour == 0x27 && row(6)->edit.colour == 0x29);
    CHECK(looks_off(ES2_SAVED_GAMES_RESTORE) && !strcmp(value(ES2_SAVED_GAMES_NAME), "T0"));
    /* the same row again: nothing (0x438009) */
    row(6)->edit.colour = 0x55;
    es2_saved_games_select(&sg, 6);
    CHECK(row(6)->edit.colour == 0x55);
    row(6)->edit.colour = 0x29;
    /* SAVE is disabled: a click does nothing, a direct press is refused by the flow */
    click_control(ES2_SAVED_GAMES_SAVE);
    CHECK(flow.naming == 0 && hl.saves == 0);
    CHECK(es2_saved_games_press(&sg, ES2_FLOW_SLOT_SAVE) == ES2_FLOW_REFUSED && flow.naming == 0);
    /* EXIT: caller 0 -> the main menu's show, the screen hidden, slot 10 */
    hl.changed = 0;
    click_control(ES2_SAVED_GAMES_EXIT);
    CHECK(flow.screen == ES2_FLOW_MAIN_MENU && flow.slot == 10 && row(6)->edit.colour == 0x27);
    CHECK(!visible(sg.ids[ES2_SAVED_GAMES_PANEL]) && !visible(sg.ids[ES2_SAVED_GAMES_BACKGROUND]));
    CHECK(!visible(sg.ids[ES2_SAVED_GAMES_STATS]) && !visible(sg.ids[ES2_SAVED_GAMES_SPARE]));
    CHECK(visible(mm.ids[ES2_MAIN_MENU_PANEL]) && hl.changed == 1 && hl.changed_handler == 0x438424);
}

static void naming(void) {
    fresh(1);
    enter_from_menu();
    click(sg.ids[ES2_SAVED_GAMES_ROW_FIRST + 3]);
    CHECK(flow.slot == 3 && looks_on(ES2_SAVED_GAMES_SAVE) && looks_off(ES2_SAVED_GAMES_RESTORE));
    click_control(ES2_SAVED_GAMES_SAVE);
    CHECK(flow.naming == 1 && row(3)->editable == 1 && row(3)->edit.active == 1 && ui->capture == 1);
    CHECK(!strcmp(row(3)->edit.text, " 4. ") && ui->hover == sg.ids[ES2_SAVED_GAMES_ROW_FIRST + 3]);
    CHECK(looks_off(ES2_SAVED_GAMES_SAVE) && looks_off(ES2_SAVED_GAMES_RESTORE) && looks_off(ES2_SAVED_GAMES_EXIT));
    CHECK(looks_on(ES2_SAVED_GAMES_ACCEPT) && looks_on(ES2_SAVED_GAMES_CANCEL));
    type("ACE 7");
    CHECK(!strcmp(row(3)->edit.text, " 4. ACE 7"));
    key(0x08, 0); /* backspace */
    key(0x08, 0);
    CHECK(!strcmp(row(3)->edit.text, " 4. ACE"));
    for (int i = 0; i < 6; i++) key(0x08, 0); /* the four-character prefix stays */
    CHECK(!strcmp(row(3)->edit.text, " 4. "));
    type("ACE");
    /* a row click while naming changes nothing */
    CHECK(es2_saved_games_press(&sg, ES2_SAVED_GAMES_ROW_HANDLER(5)) == ES2_FLOW_DONE && flow.slot == 3);
    CHECK(es2_saved_games_press(&sg, ES2_FLOW_SLOT_EXIT) == ES2_FLOW_IGNORED && flow.screen == ES2_FLOW_SAVED_GAMES);
    CHECK(es2_saved_games_press(&sg, ES2_FLOW_SLOT_RESTORE) == ES2_FLOW_IGNORED && hl.loads == 0);
    /* ACCEPT: the save of the row text, the refresh, the panel, the buttons */
    hl.changed = 0;
    click_control(ES2_SAVED_GAMES_ACCEPT);
    CHECK(hl.saves == 1 && hl.save_slot == 3 && !strcmp(hl.save_text, " 4. ACE"));
    CHECK(hl.refreshes == 1 && hl.refresh_slot == 3 && hl.changed == 1 && sg.last_handler == 0x43868a);
    CHECK(flow.naming == 0 && flow.slot == 3 && row(3)->edit.active == 0);
    CHECK(looks_off(ES2_SAVED_GAMES_ACCEPT) && looks_off(ES2_SAVED_GAMES_CANCEL));
    CHECK(looks_on(ES2_SAVED_GAMES_SAVE) && looks_on(ES2_SAVED_GAMES_RESTORE) && looks_on(ES2_SAVED_GAMES_EXIT));
    CHECK(!strcmp(value(ES2_SAVED_GAMES_NAME), "") ); /* the host marked slot 3; its (zero) record */
    /* CANCEL: the slot text back, then 0x437fef(10) */
    click_control(ES2_SAVED_GAMES_SAVE);
    type("ZULU");
    CHECK(!strcmp(row(3)->edit.text, " 4. ZULU"));
    click_control(ES2_SAVED_GAMES_CANCEL);
    CHECK(hl.saves == 1 && flow.naming == 0 && flow.slot == 10 && !strcmp(row(3)->edit.text, " 4. ACE"));
    CHECK(row(3)->edit.colour == 0x27 && looks_off(ES2_SAVED_GAMES_SAVE) && looks_off(ES2_SAVED_GAMES_RESTORE));
    CHECK(looks_on(ES2_SAVED_GAMES_EXIT) && looks_off(ES2_SAVED_GAMES_ACCEPT));
    CHECK(!strcmp(value(ES2_SAVED_GAMES_NAME), "CAMPAIGN")); /* slot 10 with a game loaded */
    /* the own refusal: a row of 80 bytes or more is not saved */
    click(sg.ids[ES2_SAVED_GAMES_ROW_FIRST + 2]);
    click_control(ES2_SAVED_GAMES_SAVE);
    {
        char long_row[90];
        memset(long_row, 'X', 85);
        long_row[85] = 0;
        CHECK(es2_ui_dialog_set(ui, sg.ids[ES2_SAVED_GAMES_ROW_FIRST + 2], long_row, 0) == 0);
    }
    CHECK(es2_saved_games_press(&sg, ES2_FLOW_SLOT_ACCEPT) == ES2_FLOW_REFUSED && hl.saves == 1 && flow.naming == 1);
    CHECK(es2_saved_games_press(&sg, ES2_FLOW_SLOT_CANCEL) == ES2_FLOW_DONE && flow.naming == 0);
    CHECK(es2_saved_games_press(&sg, ES2_FLOW_SLOT_ACCEPT) == ES2_FLOW_IGNORED);
    CHECK(es2_saved_games_press(&sg, 0x12345) == ES2_FLOW_IGNORED);
}

/* A hot key with Shift, Ctrl or Alt posts the command byte read past the table (a
 * synthetic stand-in); a row under the pointer is fired by any command, as by a plain hot key. */
static void hover_row(int slot) {
    es2_ui_widget *w = row(slot);
    es2_gui_on_mouse(shell, ES2_GUI_WM_MOUSEMOVE, (w->abs.left + w->abs.right) / 2, (w->abs.top + w->abs.bottom) / 2, 0, 0);
    step(1);
}
static void modifier_hot_keys(void) {
    static uint8_t bytes[ES2_GUI_HOT_BYTES_SIZE];
    memset(bytes, 0xff, sizeof bytes);
    bytes[0x80f] = 0;   /* Shift+Tab down */
    bytes[0xec7] = 4;   /* Shift+Ctrl+Alt+Home up */
    fresh(1);
    enter_from_menu();
    CHECK(flow.slot != 2 && flow.slot != 4);
    /* a plain hot key over row 2 selects it (the row's handler is 0x437fef(k) for every event) */
    hover_row(2);
    key(0x1b, 0);
    CHECK(flow.slot == 2);
    /* Shift+Tab: nothing without the bytes, the byte 0 event with them */
    hover_row(4);
    es2_gui_on_key(shell, 0x09, 0, 1, 0, 0);
    es2_gui_on_key(shell, 0x09, 1, 1, 0, 0);
    step(2);
    CHECK(flow.slot == 2);
    CHECK(es2_gui_input_set_hot_bytes(&shell->input, bytes, sizeof bytes));
    es2_gui_on_key(shell, 0x09, 0, 1, 0, 0);
    es2_gui_on_key(shell, 0x09, 1, 1, 0, 0);
    step(2);
    CHECK(flow.slot == 4);
    /* a word whose byte is 0xff does nothing: Shift+Esc over row 1 */
    hover_row(1);
    es2_gui_on_key(shell, 0x1b, 0, 1, 0, 0);
    es2_gui_on_key(shell, 0x1b, 1, 1, 0, 0);
    step(2);
    CHECK(flow.slot == 4);
    /* naming: byte 4 (Shift+Ctrl+Alt+Home, key-up) deletes the last character of the active row */
    click(sg.ids[ES2_SAVED_GAMES_ROW_FIRST + 3]);
    click_control(ES2_SAVED_GAMES_SAVE);
    type("AB");
    CHECK(!strcmp(row(3)->edit.text, " 4. AB"));
    es2_gui_on_key(shell, 0x24, 0, 1, 1, 1);
    es2_gui_on_key(shell, 0x24, 1, 1, 1, 1);
    step(2);
    CHECK(!strcmp(row(3)->edit.text, " 4. A"));
}

static void restore(void) {
    static const int16_t words[5] = {2, 0, 1, 3, 9};
    for (int i = 0; i < 5; i++) {
        fresh(0);
        enter_from_menu();
        click(sg.ids[ES2_SAVED_GAMES_ROW_FIRST + 4]);
        hl.final_word = words[i];
        hl.changed = 0;
        click_control(ES2_SAVED_GAMES_RESTORE);
        CHECK(hl.loads == 1 && hl.load_slot == 4 && hl.saves == 1 && hl.save_slot == 10);
        /* VSHELL: the bay for every final word (0x4383cd..0x4383d5), no End box */
        CHECK(flow.screen == ES2_FLOW_SERVICE_BAY && flow.bay_word == -1 && flow.box == ES2_FLOW_BOX_NONE);
        CHECK(flow.game == (words[i] == 2 ? ES2_FLOW_GAME_CAMPAIGN : ES2_FLOW_GAME_SLOT) && hl.campaigns == (words[i] == 2));
        CHECK(flow.slot == 10 && row(4)->edit.colour == 0x27 && movies.group_shown == 0);
        CHECK(!visible(sg.ids[ES2_SAVED_GAMES_PANEL]) && !visible(sg.ids[ES2_SAVED_GAMES_STATS]));
        CHECK(!visible(mm.ids[ES2_MAIN_MENU_PANEL]) && hl.changed == 1 && hl.changed_handler == 0x438393);
    }
    /* a slot that cannot be loaded: refused, the screen stays */
    fresh(0);
    enter_from_menu();
    click(sg.ids[ES2_SAVED_GAMES_ROW_FIRST + 1]);
    hl.load_answer = 0;
    CHECK(es2_saved_games_press(&sg, ES2_FLOW_SLOT_RESTORE) == ES2_FLOW_REFUSED);
    CHECK(flow.screen == ES2_FLOW_SAVED_GAMES && visible(sg.ids[ES2_SAVED_GAMES_PANEL]) && movies.group_shown == 1);
}

static void from_bay(void) {
    fresh(1);
    flow.screen = ES2_FLOW_SERVICE_BAY;
    flow.bay_word = -1;
    CHECK(es2_shell_flow_event(&flow, ES2_FLOW_BAY_SAVE, NULL) == ES2_FLOW_DONE && flow.caller == 8);
    es2_saved_games_show(&sg);
    step(2);
    CHECK(flow.screen == ES2_FLOW_SAVED_GAMES && !strcmp(value(ES2_SAVED_GAMES_NAME), "CAMPAIGN"));
    click(sg.ids[ES2_SAVED_GAMES_ROW_FIRST + 0]);
    CHECK(looks_on(ES2_SAVED_GAMES_SAVE) && looks_off(ES2_SAVED_GAMES_RESTORE));
    click_control(ES2_SAVED_GAMES_EXIT);
    CHECK(flow.screen == ES2_FLOW_SERVICE_BAY && flow.bay_word == -1 && flow.slot == 10);
    CHECK(!visible(sg.ids[ES2_SAVED_GAMES_PANEL]) && !visible(mm.ids[ES2_MAIN_MENU_PANEL]));
    /* the show with a selected row: highlighted at once */
    fresh(1);
    enter_from_menu();
    click(sg.ids[ES2_SAVED_GAMES_ROW_FIRST + 4]);
    es2_saved_games_hide(&sg);
    CHECK(flow.slot == 10 && row(4)->edit.colour == 0x27);
    flow.slot = 4; /* as a caller's state could leave it */
    es2_saved_games_show(&sg);
    CHECK(row(4)->edit.colour == 0x29 && looks_on(ES2_SAVED_GAMES_SAVE) && looks_on(ES2_SAVED_GAMES_RESTORE));
}

int main(void) {
    if (mmf_font(font_bytes, &font)) {
        printf("FAIL font fixture\n");
        return 1;
    }
    for (int k = 0; k < 5; k++) {
        mmf_image(images[k], k);
        bitmaps[k] = (es2_bitmap){480, 640, 0, images[k]};
    }
    build();
    show_without_game();
    naming();
    modifier_hot_keys();
    restore();
    from_bay();
    if (ui) es2_ui_release(ui);
    free(ui);
    free(shell);
    printf("saved_games_test: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
