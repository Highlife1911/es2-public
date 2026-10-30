/* The main menu screen (es2/shell/gui/main_menu.h) on synthetic
 * resources and a synthetic flow host: the build from the layout table, the
 * animation (six frames, 500 ms, lswitch2 once, the menu at frame 5, the
 * prefs byte 0x2f notice), CONTINUE's two looks, every button through the
 * toolkit's input path and its flow outcome, CREDITS, the entry dispatch. */
#include "es2/shell/gui/main_menu.h"

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

/* ---- host ---------------------------------------------------------------------- */
typedef struct host_log {
    int sounds_switch, sounds_fade, notices, notice_message, notice_caption;
    int entries[2], entry_result, plays, play_palette, played_credits, changed;
    int gate_during_play;
    int stores, store_value, loads, load_result, writes;
    int16_t load_final;
    uint8_t written[ES2_SHELL_PREFS_SIZE];
    int palettes;
} host_log;
static host_log hl;
static es2_gui_shell *shell;
static es2_ui *ui;
static es2_shell_flow flow;
static es2_shell_movies movies;
static es2_main_menu menu;

static const es2_dfn *h_font(void *c, uint32_t g) { (void)c; return g == 0x46ddfc || g == 0x46de00 ? &font : NULL; }
static const es2_bitmap *h_bitmap(void *c, const char *file) {
    (void)c;
    for (int k = 0; k < 5; k++)
        if (!strcmp(file, es2_menu_background_file(k))) return &bitmaps[k];
    return NULL;
}
static const char *h_text(void *c, uint32_t id) {
    static char t[16][16];
    (void)c;
    snprintf(t[id & 15], sizeof t[0], "TEXT %u", (unsigned)id);
    return t[id & 15];
}
static void h_sound(void *c, uint32_t a) {
    (void)c;
    if (a == ES2_MAIN_MENU_SOUND_SWITCH) hl.sounds_switch++;
    if (a == ES2_MAIN_MENU_SOUND_FADE_IN) hl.sounds_fade++;
}
static void h_notice(void *c, int m, int cap) { (void)c; hl.notices++; hl.notice_message = m; hl.notice_caption = cap; }
static int h_entry(void *c, uint32_t h, int stage) {
    (void)c; (void)h;
    if (stage >= 0 && stage < 2) hl.entries[stage]++;
    if (h == ES2_FLOW_INSTANT_ACTION && stage == 1 && hl.entry_result) {
        es2_shell_flow_game_created(&flow);
        es2_shell_flow_dbsim(&flow);
    }
    return hl.entry_result;
}
static void h_play(void *c, es2_shell_movies *m, int build) {
    (void)c;
    hl.plays++;
    hl.play_palette = build;
    hl.gate_during_play = shell->frame.realize_gate;
    for (int i = 0; i < ES2_SHELL_MOVIE_RING; i++) {
        if (m->ring[i].index == ES2_SHELL_MOVIE_CREDITS) hl.played_credits++;
        m->ring[i].index = -1; /* played */
    }
}
static void h_changed(void *c, uint32_t h, es2_shell_flow_result r) { (void)c; (void)h; (void)r; hl.changed++; }
static int f_store(void *c, uint8_t mode) { (void)c; hl.stores++; hl.store_value = mode; return 1; }
static int f_load(void *c, int slot, int16_t *fw) { (void)c; (void)slot; hl.loads++; if (hl.load_result > 0) *fw = hl.load_final; return hl.load_result; }
static int f_write(void *c, const uint8_t b[ES2_SHELL_PREFS_SIZE]) { (void)c; hl.writes++; memcpy(hl.written, b, ES2_SHELL_PREFS_SIZE); return 1; }
static int f_campaign(void *c) { (void)c; return 1; }
static int f_lost(void *c, es2_shell_flow_results *r) { (void)c; r->ending = ES2_SHELL_RESULTS_PLAYER_LOST; return 1; }
static void ui_palette(es2_ui *u, uint16_t word) {
    uint8_t rec[256][4];
    hl.palettes += word;
    mmf_palette(rec);
    es2_gui_palette_select(&u->shell->frame, rec);
    es2_gui_palette_apply(&u->shell->frame);
}

static void fresh(const uint8_t *prefs_file, size_t prefs_size, int continue_flag) {
    es2_shell_flow_host fh;
    es2_span pf = {prefs_file, prefs_size};
    if (ui) es2_ui_release(ui);
    free(ui);
    free(shell);
    memset(&hl, 0, sizeof hl);
    hl.entry_result = 1;
    shell = calloc(1, sizeof *shell);
    ui = calloc(1, sizeof *ui);
    es2_gui_shell_init(shell, NULL, NULL, NULL);
    CHECK(es2_ui_init(ui, shell) == 0);
    es2_ui_install(ui, &shell->hooks);
    ui->palette = ui_palette;
    memset(&fh, 0, sizeof fh);
    fh.store_mode = f_store;
    fh.load = f_load;
    fh.write_prefs = f_write;
    fh.campaign = f_campaign;
    es2_shell_flow_init(&flow, &fh, prefs_file ? &pf : NULL);
    es2_shell_movies_init(&movies, 1);
    memset(&menu, 0, sizeof menu);
    menu.flow = &flow;
    menu.movies = &movies;
    menu.continue_flag = continue_flag;
    menu.host = (es2_main_menu_host){NULL, h_font, h_bitmap, h_text, h_sound, h_notice, h_entry, h_play, h_changed};
    const es2_ui_screen_entry *e = es2_ui_screen_find(ES2_UI_SCREEN_MAIN_MENU);
    CHECK(e && e->build(ui, &menu) == 0);
    es2_ui_show(ui, ui->root);
}
static void step(void) {
    ui->now += 100;
    es2_gui_step(shell);
}
static void steps(int n) { while (n-- > 0) step(); }
static int visible(int control) {
    es2_ui_widget *w = es2_ui_get(ui, menu.ids[control]);
    return w && !(w->flags & ES2_UI_HIDDEN);
}
static void click(int control) {
    es2_ui_widget *w = es2_ui_get(ui, menu.ids[control]);
    int x = (w->abs.left + w->abs.right) / 2, y = (w->abs.top + w->abs.bottom) / 2;
    es2_gui_on_mouse(shell, ES2_GUI_WM_MOUSEMOVE, x, y, 0, 0);
    step();
    es2_gui_on_mouse(shell, ES2_GUI_WM_LBUTTONDOWN, x, y, 1, 0);
    step();
    es2_gui_on_mouse(shell, ES2_GUI_WM_LBUTTONUP, x, y, 0, 0);
    step();
}
/* The main menu after the animation (started at once, 25 passes of 100 ms). */
static void to_menu(void) {
    es2_main_menu_start(&menu);
    steps(30);
}

static void build(void) {
    fresh(NULL, 0, 0);
    for (int k = 0; k < ES2_MAIN_MENU_CONTROLS; k++) CHECK(menu.ids[k] != 0);
    es2_ui_widget *a = es2_ui_get(ui, menu.ids[ES2_MAIN_MENU_ANIMATION]);
    CHECK(a->cls == ES2_UI_ANIM2 && a->frame_count == 6 && a->original_handler == 0x431830);
    for (int k = 0; k < 5; k++) CHECK(a->frames[k] == &bitmaps[k]);
    CHECK(a->frames[5] == &bitmaps[4]);
    CHECK(es2_ui_get(ui, menu.ids[ES2_MAIN_MENU_BACKGROUND])->bitmap == &bitmaps[4]);
    CHECK(es2_ui_get(ui, menu.ids[ES2_MAIN_MENU_DISPLAY])->parent == ui->display);
    static const struct { int control; uint32_t handler; const char *label; } b[10] = {
        {5, 0x431935, "TEXT 3"}, {7, 0x431a08, "TEXT 4"}, {9, 0x431a73, "TEXT 5"}, {11, 0x431b27, "TEXT 6"},
        {13, 0x431db6, "TEXT 14"}, {15, 0x431e1b, "TEXT 15"}, {17, 0x431f3a, "TEXT 7"}, {19, 0x431b9b, "TEXT 10"},
        {21, 0x431bfe, "TEXT 11"}, {23, 0x431c7b, "TEXT 13"}};
    for (int i = 0; i < 10; i++) {
        es2_ui_widget *w = es2_ui_get(ui, menu.ids[b[i].control]);
        es2_ui_widget *l = es2_ui_get(ui, menu.ids[b[i].control + 1]);
        CHECK(w->cls == ES2_UI_BUTTON_FONT && w->original_handler == b[i].handler && w->handler != NULL);
        CHECK(w->label == menu.ids[b[i].control + 1] && !strcmp(l->text, b[i].label));
    }
    CHECK(es2_screen_main_menu_build(NULL, &menu) == -1 && es2_screen_main_menu_build(ui, NULL) == -1);
}

static void animation(void) {
    fresh(NULL, 0, 0);
    es2_main_menu_start(&menu);
    CHECK(shell->frame.realize_gate == 1 && hl.palettes == 1);
    CHECK(visible(ES2_MAIN_MENU_ANIMATION) && !visible(ES2_MAIN_MENU_PANEL));
    step();
    es2_ui_widget *a = es2_ui_get(ui, menu.ids[ES2_MAIN_MENU_ANIMATION]);
    CHECK(shell->input.interactive == 0 && a->frame_index == 0 && hl.sounds_switch == 0);
    int seen[6] = {0}, first_sound_at = -1;
    for (int t = 1; t <= 25; t++) {
        step();
        if (hl.sounds_switch && first_sound_at < 0) first_sound_at = t;
        if (!menu.menu_shown && a->frame_index >= 0 && a->frame_index < 6) seen[a->frame_index]++;
    }
    /* one frame per 500 ms: indices 0..4 for five passes each, then frame 5 shows the menu */
    CHECK(seen[0] == 4 && seen[1] == 5 && seen[2] == 5 && seen[3] == 5 && seen[4] == 5);
    CHECK(first_sound_at == 5 && hl.sounds_switch == 1);
    CHECK(menu.menu_shown && menu.sound_played);
    CHECK(!visible(ES2_MAIN_MENU_ANIMATION) && visible(ES2_MAIN_MENU_BACKGROUND) && visible(ES2_MAIN_MENU_PANEL));
    CHECK(shell->input.interactive == 1 && ui->timer_count == 0 && a->frames[0] == NULL);
    CHECK(hl.notices == 0 && hl.writes == 0);
    steps(20);
    CHECK(hl.sounds_switch == 1);
    /* CONTINUE without slot 10: disabled, 0x26 / 0x26 */
    es2_ui_widget *c = es2_ui_get(ui, menu.ids[ES2_MAIN_MENU_CONTINUE_GAME]);
    CHECK(c->enabled == 0 && c->colour == 0x26 && es2_ui_get(ui, c->label)->text_colour == 0x26);
    click(ES2_MAIN_MENU_CONTINUE_GAME);
    CHECK(menu.events == 0 && hl.loads == 0);
    /* a second callback after the menu: nothing (0x431849) */
    es2_main_menu_animation_step(&menu);
    CHECK(hl.sounds_switch == 1 && menu.events == 0);
}

static void notice(void) {
    uint8_t prefs[ES2_SHELL_PREFS_SIZE];
    memset(prefs, 0, sizeof prefs);
    prefs[ES2_MAIN_MENU_PREFS_NOTICE] = 1;
    prefs[ES2_SHELL_PREFS_MODE] = 1;
    fresh(prefs, sizeof prefs, 1);
    to_menu();
    CHECK(hl.notices == 1 && hl.notice_message == 6 && hl.notice_caption == 2);
    CHECK(hl.writes == 1 && hl.written[0x2f] == 2 && hl.written[ES2_SHELL_PREFS_MODE] == 1);
    CHECK(flow.prefs.current[0x2f] == 2 && flow.prefs.committed[0x2f] == 2);
    /* CONTINUE with slot 10: enabled, 0x29 / 0x22 */
    es2_ui_widget *c = es2_ui_get(ui, menu.ids[ES2_MAIN_MENU_CONTINUE_GAME]);
    CHECK(c->enabled == 1 && c->colour == 0x22 && es2_ui_get(ui, c->label)->text_colour == 0x29);
    /* byte 2 (or anything but 1): no box */
    prefs[0x2f] = 2;
    fresh(prefs, sizeof prefs, 0);
    to_menu();
    CHECK(hl.notices == 0 && hl.writes == 0);
}

static void buttons(void) {
    /* START NEW GAME: menu hidden, mode 1 stored, Registration */
    fresh(NULL, 0, 0);
    to_menu();
    click(ES2_MAIN_MENU_START_NEW_GAME);
    CHECK(menu.last_handler == ES2_FLOW_START_NEW_GAME && menu.last_result == ES2_FLOW_DONE && hl.changed == 1);
    CHECK(flow.screen == ES2_FLOW_REGISTRATION && hl.stores == 1 && hl.store_value == 1 && flow.mode == 1);
    CHECK(!visible(ES2_MAIN_MENU_PANEL) && !visible(ES2_MAIN_MENU_BACKGROUND));
    /* back from another screen: 0x431718 */
    es2_main_menu_show(&menu);
    step();
    CHECK(visible(ES2_MAIN_MENU_PANEL) && visible(ES2_MAIN_MENU_BACKGROUND) && !visible(ES2_MAIN_MENU_ANIMATION));

    /* CONTINUE GAME, final word 2: the bay, menu hidden */
    fresh(NULL, 0, 1);
    to_menu();
    hl.load_result = 1;
    hl.load_final = 2;
    click(ES2_MAIN_MENU_CONTINUE_GAME);
    CHECK(flow.screen == ES2_FLOW_SERVICE_BAY && flow.slot == 10 && hl.loads == 1 && !visible(ES2_MAIN_MENU_PANEL));
    CHECK(shell->busy == 0);
    /* CONTINUE GAME, final word 3: the End box over the menu */
    fresh(NULL, 0, 1);
    to_menu();
    hl.load_result = 1;
    hl.load_final = 3;
    click(ES2_MAIN_MENU_CONTINUE_GAME);
    CHECK(flow.screen == ES2_FLOW_MAIN_MENU && flow.box == ES2_FLOW_BOX_END_OF_GAME && flow.box_value == 3);
    CHECK(visible(ES2_MAIN_MENU_PANEL));

    /* SAVE/RESTORE */
    fresh(NULL, 0, 0);
    to_menu();
    flow.caller = 8;
    click(ES2_MAIN_MENU_SAVE_RESTORE);
    CHECK(flow.screen == ES2_FLOW_SAVED_GAMES && flow.caller == 0 && hl.store_value == 1 && !visible(ES2_MAIN_MENU_PANEL));
    /* PREFERENCES: no mode store */
    fresh(NULL, 0, 0);
    to_menu();
    click(ES2_MAIN_MENU_PREFERENCES);
    CHECK(flow.screen == ES2_FLOW_PREFERENCES && hl.stores == 0 && !visible(ES2_MAIN_MENU_PANEL));
    /* PRACTICE MISSIONS: the host's show, then mode 0 and the screen */
    fresh(NULL, 0, 0);
    to_menu();
    click(ES2_MAIN_MENU_PRACTICE);
    CHECK(flow.screen == ES2_FLOW_PRACTICE && flow.mode == 0 && hl.entries[0] == 1 && !visible(ES2_MAIN_MENU_PANEL));
    /* ... refused by the host: the menu stays */
    fresh(NULL, 0, 0);
    to_menu();
    hl.entry_result = 0;
    click(ES2_MAIN_MENU_PRACTICE);
    step();
    CHECK(flow.screen == ES2_FLOW_MAIN_MENU && menu.events == 0 && visible(ES2_MAIN_MENU_PANEL));
    /* INSTANT ACTION: both host stages, mode 0, game created, exit 2 */
    fresh(NULL, 0, 0);
    to_menu();
    click(ES2_MAIN_MENU_INSTANT_ACTION);
    CHECK(hl.entries[0] == 1 && hl.entries[1] == 1 && flow.mode == 0 && flow.quit && flow.exit_code == ES2_FLOW_EXIT_DBSIM);
    CHECK(flow.game == ES2_FLOW_GAME_SLOT && flow.final_word == 2);
    /* ... refused at stage 0: nothing */
    fresh(NULL, 0, 0);
    to_menu();
    hl.entry_result = 0;
    click(ES2_MAIN_MENU_INSTANT_ACTION);
    CHECK(hl.entries[0] == 1 && hl.entries[1] == 0 && !flow.quit && menu.events == 0);
    /* VIEW DEMO, full screen: clear, exit 5 */
    fresh(NULL, 0, 0);
    to_menu();
    shell->fullscreen = 1;
    click(ES2_MAIN_MENU_VIEW_DEMO);
    CHECK(flow.quit && flow.exit_code == ES2_FLOW_EXIT_DEMO);
    /* QUIT: quit flag, exit 0, then the clear */
    fresh(NULL, 0, 0);
    to_menu();
    click(ES2_MAIN_MENU_QUIT);
    CHECK(flow.quit && flow.exit_code == ES2_FLOW_EXIT_QUIT && menu.last_handler == 0x431db6);
    /* ONLINE MANUAL: full screen left, the event to the flow (the help routine's) */
    fresh(NULL, 0, 0);
    to_menu();
    shell->fullscreen = 1;
    click(ES2_MAIN_MENU_ONLINE_MANUAL);
    CHECK(menu.last_handler == 0x431e1b && shell->fullscreen == 0 && flow.screen == ES2_FLOW_MAIN_MENU && !flow.quit);
    CHECK(visible(ES2_MAIN_MENU_PANEL));
    /* CREDITS: gate 0 while credits.avi plays, then 1; cover hidden; no flow event */
    fresh(NULL, 0, 0);
    to_menu();
    click(ES2_MAIN_MENU_CREDITS);
    step();
    CHECK(hl.plays == 1 && hl.played_credits == 1 && hl.play_palette == 0 && hl.gate_during_play == 0);
    CHECK(shell->frame.realize_gate == 1 && menu.events == 0 && !visible(ES2_MAIN_MENU_DISPLAY));
    CHECK(visible(ES2_MAIN_MENU_PANEL) && flow.screen == ES2_FLOW_MAIN_MENU);
    /* movies off (-a): nothing played */
    fresh(NULL, 0, 0);
    es2_shell_movies_init(&movies, 0);
    to_menu();
    click(ES2_MAIN_MENU_CREDITS);
    CHECK(hl.played_credits == 0);
    /* an unknown address */
    CHECK(es2_main_menu_press(&menu, 0x431830) == ES2_FLOW_IGNORED);
}

static void entry(void) {
    /* -X0: the two intro movies, then the animation */
    fresh(NULL, 0, 0);
    CHECK(es2_main_menu_enter(&menu, 0) == ES2_FLOW_ENTRY_INTRO);
    CHECK(hl.plays == 1 && hl.sounds_fade == 0 && visible(ES2_MAIN_MENU_ANIMATION) && shell->frame.realize_gate == 1);
    /* -X6: the fade-in, no movies */
    fresh(NULL, 0, 0);
    CHECK(es2_main_menu_enter(&menu, 6) == ES2_FLOW_ENTRY_MENU);
    CHECK(hl.plays == 0 && hl.sounds_fade == 1 && visible(ES2_MAIN_MENU_ANIMATION));
    /* -X3 without slot 10: the results path ends on the main menu: the animation */
    fresh(NULL, 0, 0);
    hl.load_result = -1;
    CHECK(es2_main_menu_enter(&menu, 3) == ES2_FLOW_ENTRY_RESULTS);
    CHECK(flow.outcome == ES2_FLOW_OUTCOME_NO_SLOT && hl.plays == 0 && visible(ES2_MAIN_MENU_ANIMATION));
    /* 0x4018a1: -X3 and -X4 set the gate (otherwise the screen stays black after a mission) */
    CHECK(shell->frame.realize_gate == 1);
    fresh(NULL, 0, 0);
    hl.load_result = -1;
    CHECK(es2_main_menu_enter(&menu, 4) == ES2_FLOW_ENTRY_RESULTS && shell->frame.realize_gate == 1);
    /* -X3 after a lost mission: the Replay box, no palette of its own; the
     * start-up palette 0x401d37 already reached the table (a black screen) */
    fresh(NULL, 0, 0);
    hl.load_result = 1;
    flow.host.results = f_lost;
    CHECK(es2_main_menu_enter(&menu, 3) == ES2_FLOW_ENTRY_RESULTS && flow.outcome == ES2_FLOW_OUTCOME_REPLAY);
    CHECK(hl.palettes == 1 && shell->frame.uploads == 1 && shell->frame.palette_pending == 0);
    CHECK(shell->frame.table[1][0] != 0 || shell->frame.table[1][1] != 0 || shell->frame.table[1][2] != 0);
    CHECK(!visible(ES2_MAIN_MENU_ANIMATION));
    /* -X6: the start-up palette only clears the pending byte (gate 0), the
     * main menu's start applies it again */
    fresh(NULL, 0, 0);
    CHECK(es2_main_menu_enter(&menu, 6) == ES2_FLOW_ENTRY_MENU);
    CHECK(hl.palettes == 2 && shell->frame.uploads == 1);
}

int main(void) {
    CHECK(mmf_font(font_bytes, &font) == 0);
    for (int k = 0; k < 5; k++) {
        mmf_image(images[k], k);
        bitmaps[k] = (es2_bitmap){480, 640, 0, images[k]};
    }
    build();
    animation();
    notice();
    buttons();
    entry();
    if (ui) es2_ui_release(ui);
    free(ui);
    free(shell);
    printf("main_menu_test: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
