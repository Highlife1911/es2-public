/* The Preferences screen (es2/shell/gui/preferences.h) on synthetic
 * resources and a synthetic flow host: the build from the layout table and the
 * registration, the show 0x436d45 (every radio group from the current
 * block, out-of-range bytes, the shadows), every control through the
 * toolkit's input path with its byte, its group and the music ramps, Window
 * in full screen, Full Screen with the boxes' Alert box windowed and in full
 * screen, CANCEL (revert, display and music re-synchronised, the main menu)
 * and ACCEPT (the 54 bytes, the main menu, also after a failed write). */
#include "es2/shell/gui/preferences.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "main_menu_fixture.h"

static unsigned checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static uint8_t font_bytes[16384];
static es2_dfn font;
static uint8_t background_pixels[640 * 480], box_pixels[2][18 * 16];
static es2_bitmap background, boxes_bitmaps[2];

typedef struct host_log {
    int ramps_in, ramps_out, ramp_byte0, changed, writes, write_ok, refreshes, display_asks;
    uint32_t last_changed;
    uint8_t written[ES2_SHELL_PREFS_SIZE];
} host_log;
static host_log hl;
static es2_gui_shell *shell;
static es2_ui *ui;
static es2_shell_flow flow;
static es2_main_menu menu;
static es2_boxes boxes;
static es2_preferences prefs;

static const es2_dfn *h_font(void *c, uint32_t g) { (void)c; return g == 0x46ddfc || g == 0x46de00 ? &font : NULL; }
static const es2_bitmap *h_menu_bitmap(void *c, const char *file) {
    (void)c;
    return !strcmp(file, es2_menu_background_file(4)) ? &background : NULL;
}
static const es2_bitmap *h_bitmap(void *c, const char *file, int member) {
    (void)c;
    if (!strcmp(file, "dba\\chk_box.dba") && (member == 0 || member == 1)) return &boxes_bitmaps[member];
    if (!strcmp(file, es2_menu_background_file(4)) && member == -1) return &background;
    return NULL;
}
static const char *h_text(void *c, uint32_t id) {
    static char t[64][16];
    (void)c;
    snprintf(t[id & 63], sizeof t[0], "T%X", (unsigned)id);
    return t[id & 63];
}
static void h_music(void *c, uint32_t address, const uint8_t p[ES2_SHELL_PREFS_SIZE]) {
    (void)c;
    if (address == ES2_PREFERENCES_MUSIC_IN) hl.ramps_in++;
    if (address == ES2_PREFERENCES_MUSIC_OUT) hl.ramps_out++;
    hl.ramp_byte0 = p[0];
}
static void h_changed(void *c, uint32_t h, es2_shell_flow_result r) { (void)c; (void)r; hl.changed++; hl.last_changed = h; }
static int f_write(void *c, const uint8_t b[ES2_SHELL_PREFS_SIZE]) {
    (void)c;
    hl.writes++;
    memcpy(hl.written, b, ES2_SHELL_PREFS_SIZE);
    return hl.write_ok;
}
static int f_full_screen(void *c) { (void)c; hl.display_asks++; return shell->fullscreen; }
static void b_refresh(void *c) { hl.refreshes++; es2_preferences_display_refresh(c); }

static void fresh(const uint8_t file[ES2_SHELL_PREFS_SIZE]) {
    es2_shell_flow_host fh;
    es2_boxes_host bh;
    es2_preferences_host ph = {NULL, h_font, h_bitmap, h_text, h_music, h_changed};
    es2_span pf = {file, ES2_SHELL_PREFS_SIZE};
    const es2_ui_screen_entry *e;
    if (ui) es2_ui_release(ui);
    free(ui);
    free(shell);
    memset(&hl, 0, sizeof hl);
    hl.write_ok = 1;
    shell = calloc(1, sizeof *shell);
    ui = calloc(1, sizeof *ui);
    es2_gui_shell_init(shell, NULL, NULL, NULL);
    CHECK(es2_ui_init(ui, shell) == 0);
    es2_ui_install(ui, &shell->hooks);
    memset(&fh, 0, sizeof fh);
    fh.write_prefs = f_write;
    fh.full_screen = f_full_screen;
    es2_shell_flow_init(&flow, &fh, &pf);
    memset(&menu, 0, sizeof menu);
    menu.flow = &flow;
    menu.host = (es2_main_menu_host){NULL, h_font, h_menu_bitmap, NULL, NULL, NULL, NULL, NULL, NULL};
    CHECK(es2_screen_main_menu_build(ui, &menu) == 0);
    memset(&bh, 0, sizeof bh);
    bh.ctx = &prefs;
    bh.prefs_refresh = b_refresh;
    es2_boxes_init(&boxes, ui, &flow, &bh, 0);
    CHECK(es2_boxes_build(&boxes) == 0);
    es2_preferences_init(&prefs, &flow, &menu, &boxes, &ph);
    e = es2_ui_screen_find(ES2_UI_SCREEN_PREFERENCES);
    CHECK(e && e->build(ui, &prefs) == 0);
    es2_ui_show(ui, ui->root);
}
static void step(void) {
    ui->now += 100;
    es2_gui_step(shell);
}
static int visible(es2_ui_id id) {
    es2_ui_widget *w = es2_ui_get(ui, id);
    return w && !(w->flags & ES2_UI_HIDDEN);
}
static int shown(int control) { return visible(prefs.ids[control]); }
static int32_t checked(int control) { return es2_ui_get(ui, prefs.ids[control])->checked; }
static void click_id(es2_ui_id id) {
    es2_ui_widget *w = es2_ui_get(ui, id);
    int x = (w->abs.left + w->abs.right) / 2, y = (w->abs.top + w->abs.bottom) / 2;
    es2_gui_on_mouse(shell, ES2_GUI_WM_MOUSEMOVE, x, y, 0, 0);
    step();
    es2_gui_on_mouse(shell, ES2_GUI_WM_LBUTTONDOWN, x, y, 1, 0);
    step();
    es2_gui_on_mouse(shell, ES2_GUI_WM_LBUTTONUP, x, y, 0, 0);
    step();
}
static void click(int control) { click_id(prefs.ids[control]); }
/* The main menu's PREFERENCES (0x431b9b: hide, then 0x436d45). */
static void open_screen(void) {
    es2_main_menu_show(&menu);
    step();
    CHECK(es2_main_menu_press(&menu, ES2_FLOW_PREFERENCES_BUTTON) == ES2_FLOW_DONE);
    CHECK(flow.screen == ES2_FLOW_PREFERENCES);
    es2_preferences_show(&prefs);
    step();
}

static void test_build(void) {
    uint8_t file[ES2_SHELL_PREFS_SIZE] = {0};
    fresh(file);
    for (int k = 0; k < ES2_PREFERENCES_CONTROLS; k++) CHECK(prefs.ids[k] != 0);
    /* the constructor leaves everything hidden: the shield and the panel
     * hidden, the ESAlert pending under the shield */
    CHECK(!shown(ES2_PREFERENCES_BACKGROUND) && !shown(ES2_PREFERENCES_PANEL) && !shown(ES2_PREFERENCES_SHIELD));
    CHECK(es2_ui_get(ui, prefs.ids[ES2_PREFERENCES_ALERT])->flags & ES2_UI_PENDING);
    CHECK(es2_ui_get(ui, prefs.ids[ES2_PREFERENCES_FULL_SCREEN])->handler == es2_boxes_handler(0x437608));
    CHECK(es2_ui_get(ui, prefs.ids[ES2_PREFERENCES_ALERT_ACCEPT])->handler == es2_boxes_handler(0x437678));
    CHECK(es2_ui_get(ui, prefs.ids[ES2_PREFERENCES_FULL_SCREEN])->user == &boxes);
    CHECK(es2_ui_get(ui, prefs.ids[ES2_PREFERENCES_ALERT_ACCEPT])->user == &boxes);
    CHECK(es2_ui_get(ui, prefs.ids[ES2_PREFERENCES_CANCEL])->original_handler == 0x437220);
    CHECK(es2_ui_get(ui, prefs.ids[ES2_PREFERENCES_ACCEPT])->original_handler == 0x4372e1);
    CHECK(es2_ui_get(ui, prefs.ids[ES2_PREFERENCES_LOW_RES])->original_handler == 0x4376f7);
    CHECK(es2_ui_get(ui, prefs.ids[ES2_PREFERENCES_BUILD_1])->original_handler == 0x437696);
    CHECK(es2_ui_get(ui, prefs.ids[ES2_PREFERENCES_MUSIC])->bitmap_active == &boxes_bitmaps[0]);
    CHECK(es2_ui_get(ui, prefs.ids[ES2_PREFERENCES_MUSIC])->bitmap_normal == &boxes_bitmaps[1]);
    CHECK(es2_ui_get(ui, prefs.ids[ES2_PREFERENCES_BACKGROUND])->bitmap == &background);
    CHECK(prefs.shadow_repair == -1 && prefs.shadow_resolution == -1 && prefs.shadow_build == -1 && prefs.shadow_display == -1);
    /* refusals */
    CHECK(es2_screen_preferences_build(NULL, &prefs) == -1 && es2_screen_preferences_build(ui, NULL) == -1);
    CHECK(es2_preferences_press(&prefs, 0x431b9b) == ES2_FLOW_IGNORED);
    {
        es2_preferences none;
        es2_preferences_init(&none, &flow, NULL, NULL, NULL);
        CHECK(es2_preferences_press(&none, ES2_PREFERENCES_ON_MUSIC) == ES2_FLOW_IGNORED); /* not built */
        es2_preferences_show(&none);
        es2_preferences_hide(&none);
    }
}

static void test_show(void) {
    uint8_t file[ES2_SHELL_PREFS_SIZE] = {0};
    file[0] = 1; file[1] = 0; file[4] = 1; file[6] = 0; file[ES2_SHELL_PREFS_REPAIR] = 2; file[ES2_SHELL_PREFS_BUILD] = 1;
    fresh(file);
    open_screen();
    CHECK(!visible(menu.ids[ES2_MAIN_MENU_PANEL]));
    CHECK(shown(ES2_PREFERENCES_BACKGROUND) && shown(ES2_PREFERENCES_PANEL) && !shown(ES2_PREFERENCES_SHIELD));
    CHECK(checked(ES2_PREFERENCES_MUSIC) == 1 && checked(ES2_PREFERENCES_SFX) == 0);
    CHECK(checked(ES2_PREFERENCES_REPAIR_0) == 0 && checked(ES2_PREFERENCES_REPAIR_1) == 0 && checked(ES2_PREFERENCES_REPAIR_2) == 1);
    CHECK(checked(ES2_PREFERENCES_BUILD_0) == 0 && checked(ES2_PREFERENCES_BUILD_1) == 1);
    CHECK(checked(ES2_PREFERENCES_HIGH_RES) == 0 && checked(ES2_PREFERENCES_LOW_RES) == 1);
    CHECK(checked(ES2_PREFERENCES_WINDOW) == 1 && checked(ES2_PREFERENCES_FULL_SCREEN) == 0);
    CHECK(prefs.shadow_repair == 2 && prefs.shadow_build == 1 && prefs.shadow_resolution == 1 && prefs.shadow_display == 0);
    CHECK(!memcmp(flow.prefs.current, file, sizeof file) && hl.writes == 0); /* the re-stores change nothing */
    /* out-of-range bytes leave their group as it was; the check boxes take the byte as it is */
    file[0] = 2; file[4] = 5; file[6] = 3; file[ES2_SHELL_PREFS_REPAIR] = 7; file[ES2_SHELL_PREFS_BUILD] = 9;
    fresh(file);
    open_screen();
    CHECK(checked(ES2_PREFERENCES_MUSIC) == 2);
    CHECK(!checked(ES2_PREFERENCES_REPAIR_0) && !checked(ES2_PREFERENCES_REPAIR_1) && !checked(ES2_PREFERENCES_REPAIR_2));
    CHECK(!checked(ES2_PREFERENCES_BUILD_0) && !checked(ES2_PREFERENCES_BUILD_1));
    CHECK(!checked(ES2_PREFERENCES_HIGH_RES) && !checked(ES2_PREFERENCES_LOW_RES));
    CHECK(!checked(ES2_PREFERENCES_WINDOW) && !checked(ES2_PREFERENCES_FULL_SCREEN));
    CHECK(prefs.shadow_repair == 7 && prefs.shadow_build == 9 && prefs.shadow_resolution == 5 && prefs.shadow_display == 3);
    /* hide */
    es2_preferences_hide(&prefs);
    CHECK(!shown(ES2_PREFERENCES_BACKGROUND) && !shown(ES2_PREFERENCES_PANEL));
}

static void test_controls(void) {
    uint8_t file[ES2_SHELL_PREFS_SIZE] = {0};
    file[0] = 1;
    fresh(file);
    /* not on the Preferences screen: the flow ignores, no ramp */
    CHECK(es2_preferences_press(&prefs, ES2_PREFERENCES_ON_MUSIC) == ES2_FLOW_IGNORED && hl.ramps_out == 0);
    CHECK(es2_preferences_press(&prefs, ES2_PREFERENCES_ON_WINDOW) == ES2_FLOW_IGNORED);
    CHECK(es2_preferences_press(&prefs, ES2_PREFERENCES_ON_REPAIR_1) == ES2_FLOW_IGNORED && flow.prefs.current[ES2_SHELL_PREFS_REPAIR] == 0);
    open_screen();
    hl.changed = 0;
    /* Music: set -> ramp down with byte 0 still 1, then cleared */
    click(ES2_PREFERENCES_MUSIC);
    CHECK(flow.prefs.current[0] == 0 && hl.ramps_out == 1 && hl.ramp_byte0 == 1 && checked(ES2_PREFERENCES_MUSIC) == 0);
    CHECK(prefs.last_handler == ES2_PREFERENCES_ON_MUSIC && prefs.last_result == ES2_FLOW_DONE && hl.changed == 1);
    /* clear -> set, then ramp up with byte 0 = 1 */
    click(ES2_PREFERENCES_MUSIC);
    CHECK(flow.prefs.current[0] == 1 && hl.ramps_in == 1 && hl.ramp_byte0 == 1 && checked(ES2_PREFERENCES_MUSIC) == 1);
    /* Sound Effects: byte 1, no ramp */
    click(ES2_PREFERENCES_SFX);
    CHECK(flow.prefs.current[1] == 1 && checked(ES2_PREFERENCES_SFX) == 1 && hl.ramps_in == 1 && hl.ramps_out == 1);
    click(ES2_PREFERENCES_SFX);
    CHECK(flow.prefs.current[1] == 0 && checked(ES2_PREFERENCES_SFX) == 0);
    /* repair radios */
    click(ES2_PREFERENCES_REPAIR_1);
    CHECK(flow.prefs.current[ES2_SHELL_PREFS_REPAIR] == 1 && prefs.shadow_repair == 1);
    CHECK(!checked(ES2_PREFERENCES_REPAIR_0) && checked(ES2_PREFERENCES_REPAIR_1) && !checked(ES2_PREFERENCES_REPAIR_2));
    click(ES2_PREFERENCES_REPAIR_2);
    CHECK(flow.prefs.current[ES2_SHELL_PREFS_REPAIR] == 2 && checked(ES2_PREFERENCES_REPAIR_2) && !checked(ES2_PREFERENCES_REPAIR_1));
    click(ES2_PREFERENCES_REPAIR_0);
    CHECK(flow.prefs.current[ES2_SHELL_PREFS_REPAIR] == 0 && checked(ES2_PREFERENCES_REPAIR_0) && prefs.shadow_repair == 0);
    /* weapons building */
    click(ES2_PREFERENCES_BUILD_1);
    CHECK(flow.prefs.current[ES2_SHELL_PREFS_BUILD] == 1 && checked(ES2_PREFERENCES_BUILD_1) && !checked(ES2_PREFERENCES_BUILD_0));
    click(ES2_PREFERENCES_BUILD_0);
    CHECK(flow.prefs.current[ES2_SHELL_PREFS_BUILD] == 0 && checked(ES2_PREFERENCES_BUILD_0) && prefs.shadow_build == 0);
    /* resolution */
    click(ES2_PREFERENCES_LOW_RES);
    CHECK(flow.prefs.current[4] == 1 && checked(ES2_PREFERENCES_LOW_RES) && !checked(ES2_PREFERENCES_HIGH_RES));
    click(ES2_PREFERENCES_HIGH_RES);
    CHECK(flow.prefs.current[4] == 0 && checked(ES2_PREFERENCES_HIGH_RES) && prefs.shadow_resolution == 0);
    CHECK(hl.writes == 0 && flow.screen == ES2_FLOW_PREFERENCES);
}

static void test_display(void) {
    uint8_t file[ES2_SHELL_PREFS_SIZE] = {0};
    file[6] = 1;
    fresh(file);
    shell->fullscreen = 1;
    open_screen();
    CHECK(checked(ES2_PREFERENCES_FULL_SCREEN) == 1 && !checked(ES2_PREFERENCES_WINDOW));
    /* Full Screen in full screen: nothing (the boxes' handler and the flow) */
    click(ES2_PREFERENCES_FULL_SCREEN);
    CHECK(!shown(ES2_PREFERENCES_SHIELD) && flow.box == ES2_FLOW_BOX_NONE && shell->fullscreen == 1);
    /* the flow refuses as well when asked directly */
    CHECK(es2_shell_flow_prefs_control(&flow, ES2_SHELL_PREFS_FULL_SCREEN) == ES2_FLOW_IGNORED && hl.display_asks == 1);
    /* Window: leave full screen, byte 6 := 0 */
    click(ES2_PREFERENCES_WINDOW);
    CHECK(shell->fullscreen == 0 && flow.prefs.current[6] == 0 && prefs.shadow_display == 0);
    CHECK(checked(ES2_PREFERENCES_WINDOW) == 1 && !checked(ES2_PREFERENCES_FULL_SCREEN));
    /* Window while windowed: no toggle */
    click(ES2_PREFERENCES_WINDOW);
    CHECK(shell->fullscreen == 0 && flow.prefs.current[6] == 0);
    /* Full Screen windowed: the Alert box, then its ACCEPT: the display, byte 6, the radios */
    click(ES2_PREFERENCES_FULL_SCREEN);
    CHECK(shown(ES2_PREFERENCES_SHIELD) && shown(ES2_PREFERENCES_ALERT) && flow.box == ES2_FLOW_BOX_ALERT);
    CHECK(flow.prefs.current[6] == 0);
    /* the panel's controls are under the shield: a click reaches the shield only */
    click(ES2_PREFERENCES_MUSIC);
    CHECK(flow.prefs.current[0] == 0 && flow.box == ES2_FLOW_BOX_ALERT);
    click(ES2_PREFERENCES_ALERT_ACCEPT);
    CHECK(!shown(ES2_PREFERENCES_SHIELD) && flow.box == ES2_FLOW_BOX_NONE && shell->fullscreen == 1);
    CHECK(flow.prefs.current[6] == 1 && hl.refreshes == 1);
    CHECK(checked(ES2_PREFERENCES_FULL_SCREEN) == 1 && !checked(ES2_PREFERENCES_WINDOW));
}

static void test_cancel(void) {
    uint8_t file[ES2_SHELL_PREFS_SIZE] = {0};
    /* committed: music off, windowed; changed: music on, full screen */
    fresh(file);
    open_screen();
    click(ES2_PREFERENCES_MUSIC);
    click(ES2_PREFERENCES_FULL_SCREEN);
    click(ES2_PREFERENCES_ALERT_ACCEPT);
    click(ES2_PREFERENCES_REPAIR_2);
    CHECK(flow.prefs.current[0] == 1 && flow.prefs.current[6] == 1 && shell->fullscreen == 1);
    hl.ramps_in = hl.ramps_out = 0;
    click(ES2_PREFERENCES_CANCEL);
    CHECK(flow.screen == ES2_FLOW_MAIN_MENU && !memcmp(flow.prefs.current, file, sizeof file) && hl.writes == 0);
    CHECK(shell->fullscreen == 0);                                  /* byte 6 = 0 in full screen: toggled */
    CHECK(hl.ramps_out == 1 && hl.ramp_byte0 == 1 && hl.ramps_in == 0); /* byte 0 toggled around the ramp down */
    CHECK(flow.prefs.current[0] == 0);
    CHECK(!shown(ES2_PREFERENCES_BACKGROUND) && !shown(ES2_PREFERENCES_PANEL) && visible(menu.ids[ES2_MAIN_MENU_PANEL]));
    CHECK(prefs.last_handler == ES2_FLOW_PREFS_CANCEL && hl.last_changed == ES2_FLOW_PREFS_CANCEL);
    /* committed: music on, full screen; the display windowed: entered again, ramp up */
    file[0] = 1; file[6] = 1;
    fresh(file);
    open_screen();
    hl.ramps_in = hl.ramps_out = 0;
    click(ES2_PREFERENCES_CANCEL);
    CHECK(shell->fullscreen == 1 && hl.ramps_in == 1 && hl.ramps_out == 0);
    /* the same display: no toggle */
    fresh(file);
    shell->fullscreen = 1;
    open_screen();
    click(ES2_PREFERENCES_CANCEL);
    CHECK(shell->fullscreen == 1);
    /* a second CANCEL on the main menu: the flow ignores it, nothing else runs */
    hl.ramps_in = 0;
    CHECK(es2_preferences_press(&prefs, ES2_FLOW_PREFS_CANCEL) == ES2_FLOW_IGNORED && hl.ramps_in == 0);
}

static void test_accept(void) {
    uint8_t file[ES2_SHELL_PREFS_SIZE] = {0};
    fresh(file);
    open_screen();
    click(ES2_PREFERENCES_SFX);
    click(ES2_PREFERENCES_BUILD_1);
    click(ES2_PREFERENCES_ACCEPT);
    CHECK(hl.writes == 1 && hl.written[1] == 1 && hl.written[ES2_SHELL_PREFS_BUILD] == 1);
    CHECK(!memcmp(flow.prefs.committed, flow.prefs.current, ES2_SHELL_PREFS_SIZE));
    CHECK(flow.screen == ES2_FLOW_MAIN_MENU && !shown(ES2_PREFERENCES_PANEL) && visible(menu.ids[ES2_MAIN_MENU_PANEL]));
    CHECK(shell->fullscreen == 0 && hl.ramps_in == 0 && hl.ramps_out == 0); /* no display or music change */
    /* a failed write: the hide and the main menu all the same */
    open_screen();
    hl.write_ok = 0;
    click(ES2_PREFERENCES_ACCEPT);
    CHECK(hl.writes == 2 && prefs.last_result == ES2_FLOW_FAILED && flow.screen == ES2_FLOW_MAIN_MENU);
    CHECK(!shown(ES2_PREFERENCES_PANEL) && visible(menu.ids[ES2_MAIN_MENU_PANEL]));
}

int main(void) {
    if (mmf_font(font_bytes, &font)) {
        printf("font fixture failed\n");
        return 1;
    }
    mmf_image(background_pixels, 4);
    background = (es2_bitmap){480, 640, 0, background_pixels};
    for (int m = 0; m < 2; m++) {
        for (int i = 0; i < 18 * 16; i++) box_pixels[m][i] = (uint8_t)(m ? 30 + i % 3 : 200 + i % 5);
        boxes_bitmaps[m] = (es2_bitmap){16, 18, 0, box_pixels[m]};
    }
    test_build();
    test_show();
    test_controls();
    test_display();
    test_cancel();
    test_accept();
    if (ui) es2_ui_release(ui);
    free(ui);
    free(shell);
    printf("preferences_test: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
