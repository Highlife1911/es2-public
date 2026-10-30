/* The BUILD and ARMORY screens and the weapons scrap warning
 * (es2/shell/gui/build_armory.h) on synthetic resources and a synthetic
 * loaded game (build_armory_fixture.h): the builds and their registration,
 * BUILD's show (row enables from the nine-word bank, type 0, grid 0, the
 * Herc pictures, the texts, Scrap and Build enabling), a type row, the bay
 * selector, Build (the library's new record, the squad panel's host call),
 * Scrap (REPAIR's box through the host), the hide; ARMORY's show in auto and
 * manual building (Clear, rows, marks, counts, texts, the row's image and
 * wpn_info lines), the selection, add and remove by the two mouse buttons,
 * Clear, the weapons scrap warning with CANCEL and ACCEPT, the hide; and
 * the refusals. */
#include "es2/shell/gui/build_armory.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "build_armory_fixture.h"
#include "main_menu_fixture.h"

static unsigned checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static uint8_t font_bytes[16384];
static es2_dfn font;
static baf fx;
static es2_gui_shell *shell;
static es2_ui *ui;
static es2_shell_flow flow;
static es2_shell_editor_session session;
static es2_build_armory_game game;
static es2_build_armory ba;
static int herc_scraps, squads, changes;

static const es2_dfn *h_font(void *c, uint32_t g) { (void)c; return g == 0x46ddfc || g == 0x46de00 ? &font : NULL; }
static const es2_bitmap *h_bitmap(void *c, const char *file, int m) { (void)c; return baf_bitmap(&fx, file, m); }
static const char *h_text(void *c, uint32_t id) {
    static char t[64][16];
    (void)c;
    snprintf(t[id & 63], sizeof t[0], "T%X", (unsigned)id);
    return t[id & 63];
}
static const char *h_name(void *c, int id) {
    static char t[33][16];
    (void)c;
    if (id < 0 || id > 32) return NULL;
    snprintf(t[id], sizeof t[0], "WPN %d", id);
    return t[id];
}
static const char *h_info(void *c, int index) {
    static char t[200][16];
    (void)c;
    if (index < 0 || index >= 200) return NULL;
    snprintf(t[index], sizeof t[0], "INFO %d", index);
    return t[index];
}
static es2_span h_file(void *c, const char *name) { (void)c; return baf_file(&fx, name); }
static void h_scrap(void *c) { (void)c; herc_scraps++; }
static void h_squad(void *c) { (void)c; squads++; }
static void h_changed(void *c, uint32_t h, int a) { (void)c; (void)h; (void)a; changes++; }

/* A fresh shell and game; salvage and building byte as given. */
static void fresh(uint32_t salvage, int building, int16_t bay) {
    uint8_t prefs[ES2_SHELL_PREFS_SIZE] = {0};
    es2_span pf = {prefs, sizeof prefs};
    es2_shell_flow_host fh;
    es2_build_armory_host host = {NULL, h_font, h_bitmap, h_text, h_name, h_info, h_file, h_scrap, h_squad, h_changed};
    const es2_ui_screen_entry *e;
    if (ui) es2_ui_release(ui);
    free(ui);
    free(shell);
    free(game.save.data);
    herc_scraps = squads = changes = 0;
    baf_init(&fx);
    baf_salvage(&fx, salvage);
    CHECK(baf_game(&fx, &game, &session, bay) == 0);
    shell = calloc(1, sizeof *shell);
    ui = calloc(1, sizeof *ui);
    es2_gui_shell_init(shell, NULL, NULL, NULL);
    CHECK(es2_ui_init(ui, shell) == 0);
    es2_ui_install(ui, &shell->hooks);
    memset(&fh, 0, sizeof fh);
    prefs[ES2_SHELL_PREFS_BUILD] = (uint8_t)building;
    es2_shell_flow_init(&flow, &fh, &pf);
    es2_build_armory_init(&ba, &flow, &game, &host);
    e = es2_ui_screen_find(ES2_UI_SCREEN_BUILD);
    CHECK(e && e->build(ui, &ba) == 0);
    e = es2_ui_screen_find(ES2_UI_SCREEN_ARMORY);
    CHECK(e && e->build(ui, &ba) == 0);
    e = es2_ui_screen_find(ES2_UI_SCREEN_WEAPON_SCRAP_BOX);
    CHECK(e && e->build(ui, &ba) == 0);
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
static void click_id(es2_ui_id id, int right) {
    es2_ui_widget *w = es2_ui_get(ui, id);
    int x = (w->abs.left + w->abs.right) / 2, y = (w->abs.top + w->abs.bottom) / 2;
    es2_gui_on_mouse(shell, ES2_GUI_WM_MOUSEMOVE, x, y, 0, 0);
    step();
    es2_gui_on_mouse(shell, right ? ES2_GUI_WM_RBUTTONDOWN : ES2_GUI_WM_LBUTTONDOWN, x, y, !right, right);
    step();
    es2_gui_on_mouse(shell, right ? ES2_GUI_WM_RBUTTONUP : ES2_GUI_WM_LBUTTONUP, x, y, 0, 0);
    step();
}
static es2_ui_widget *bw(int k) { return es2_ui_get(ui, ba.build_ids[k]); }
static es2_ui_widget *aw(int k) { return es2_ui_get(ui, ba.armory_ids[k]); }
static es2_ui_widget *col(es2_ui_id quad, int k) { return es2_ui_get(ui, es2_ui_get(ui, quad)->columns[k]); }
static es2_ui_id brow(int t) { return ba.build_ids[ES2_BUILD_ROW + 5 * t]; }
static es2_ui_id arow(int r) { return ba.armory_ids[ES2_ARMORY_ROW + 5 * r]; }
static const char *label_text(es2_ui_widget *button) { return es2_ui_get(ui, button->label)->text; }
static uint32_t salvage_now(void) {
    es2_sav s;
    CHECK(es2_sav_parse((es2_span){game.save.data, game.save.size}, &s, NULL) == ES2_OK);
    return (uint32_t)s.tally.data[0] | (uint32_t)s.tally.data[1] << 8 | (uint32_t)s.tally.data[2] << 16 |
           (uint32_t)s.tally.data[3] << 24;
}
static size_t hangar_count(void) {
    es2_sav s;
    CHECK(es2_sav_parse((es2_span){game.save.data, game.save.size}, &s, NULL) == ES2_OK);
    return s.loadout_count;
}

static void test_build_screen(void) {
    fresh(50000, 0, 0);
    for (int k = 0; k < ES2_BUILD_CONTROLS; k++) CHECK(k == 1 || ba.build_ids[k] != 0);
    for (int k = 0; k < ES2_ARMORY_CONTROLS; k++) CHECK(k == ES2_ARMORY_IMAGE || ba.armory_ids[k] != 0);
    for (int k = 0; k < ES2_WEAPON_SCRAP_CONTROLS; k++) CHECK(ba.scrap_ids[k] != 0);
    for (int r = 0; r < ES2_SHELL_ARMORY_ROWS; r++) {
        CHECK(ba.images[r] != 0 && !visible(ba.images[r]));
        CHECK(es2_ui_get(ui, arow(r))->original_handler == ES2_ARMORY_ON_ROW0 + 0x74u * (unsigned)r);
    }
    /* image of row 5: x 20 + 70, y 10 + 60, frame 1 (40 x 20), under the ESArm */
    {
        es2_ui_widget *im = es2_ui_get(ui, ba.images[5]), *arm = aw(ES2_ARMORY_ARM);
        CHECK(im->parent == ba.armory_ids[ES2_ARMORY_ARM] && im->bitmap == &fx.bitmaps[1] && im->frame == 0);
        CHECK(im->abs.left == arm->abs.left + 90 && im->abs.top == arm->abs.top + 70);
        CHECK(im->abs.right == arm->abs.left + 130 && im->abs.bottom == arm->abs.top + 90);
    }
    /* the row texts: name and cost (category 4: 1000 * (1 + 0)) */
    CHECK(!strcmp(col(arow(3), 1)->text, "WPN 4") && !strcmp(col(arow(3), 3)->text, "1"));
    CHECK(!strcmp(col(arow(2), 3)->text, "4") && !strcmp(col(arow(3), 0)->text, "[   ]"));
    for (int t = 0; t < 9; t++) CHECK(es2_ui_get(ui, brow(t))->original_handler == ES2_BUILD_ON_ROW0 + 0x61u * (unsigned)t);
    CHECK(bw(ES2_BUILD_SCRAP)->original_handler == 0x447570 && bw(ES2_BUILD_BUILD)->original_handler == 0x4475ce);
    CHECK(es2_ui_get(ui, ba.scrap_ids[ES2_WEAPON_SCRAP_ACCEPT])->original_handler == 0x448447);
    CHECK(!visible(ba.build_ids[ES2_BUILD_PANEL]) && !visible(ba.armory_ids[ES2_ARMORY_PANEL]));

    /* the show from the bay (screen word -1) */
    CHECK(es2_build_show(&ba) == ES2_OK);
    step();
    CHECK(session.screen == ES2_SHELL_SCREEN_BUILD && session.herc_type == 0);
    CHECK(visible(ba.build_ids[ES2_BUILD_PANEL]) && visible(ba.build_ids[ES2_BUILD_LOWER]));
    CHECK(visible(ba.build_ids[ES2_BUILD_GRID]) && !visible(ba.build_ids[ES2_BUILD_GRID + 1]));
    for (int t = 0; t < 9; t++) CHECK(es2_ui_get(ui, brow(t))->enabled == (t == 0 || t == 1 || t == 2 || t == 4));
    CHECK(col(brow(3), 1)->text_colour == 0x10 && col(brow(1), 1)->text_colour == 0x27);
    CHECK(col(brow(0), 0)->text_colour == 0x29 && es2_ui_get(ui, brow(0))->colour == 0x29);
    CHECK(!strcmp(bw(ES2_BUILD_TONS)->text, "30 TC6") && !strcmp(bw(ES2_BUILD_SPEED)->text, "60 TC7"));
    CHECK(!strcmp(bw(ES2_BUILD_WORD6)->text, "0") && !strcmp(bw(ES2_BUILD_COST)->text, "100 TC6"));
    /* bank cost: one entry of category 2 = 3000 */
    CHECK(!strcmp(bw(ES2_BUILD_AVAILABLE)->text, "47 TC6"));
    /* bay 0 holds the player's Herc, two functional: Scrap on, Build off */
    CHECK(bw(ES2_BUILD_SCRAP)->enabled == 1 && bw(ES2_BUILD_SCRAP)->colour == 0x22);
    CHECK(es2_ui_get(ui, bw(ES2_BUILD_SCRAP)->label)->text_colour == 0x29);
    CHECK(bw(ES2_BUILD_BUILD)->enabled == 0 && bw(ES2_BUILD_BUILD)->colour == 0x26);
    /* the Herc pictures: three layers of rpr_outl in grid 0 */
    {
        const es2_ui_layer *k = ui->layers[bw(ES2_BUILD_GRID)->grid];
        CHECK(k[0].bitmap == &fx.bitmaps[4] && k[1].bitmap == &fx.bitmaps[5] && k[2].bitmap == &fx.bitmaps[6]);
        CHECK(k[1].x == 40 && k[1].y == 70 && k[2].mirror == 1 && k[0].mirror == 0);
        CHECK(k[0].from[0] == 0x0e && k[0].to[0] == 0x0e && k[0].to[1] == 0x10 && !k[3].bitmap);
        CHECK(ui->layers[bw(ES2_BUILD_GRID + 8)->grid][0].bitmap == &fx.bitmaps[4 + 8 % 4]);
    }
    /* Scrap: REPAIR's warning through the host */
    click_id(ba.build_ids[ES2_BUILD_SCRAP], 0);
    CHECK(herc_scraps == 1 && ba.last_handler == ES2_BUILD_ON_SCRAP);
    /* type rows: a disabled row does nothing; type 1 */
    click_id(brow(3), 0);
    CHECK(session.herc_type == 0);
    click_id(brow(1), 0);
    CHECK(session.herc_type == 1 && ba.last_applied == 1);
    CHECK(!visible(ba.build_ids[ES2_BUILD_GRID]) && visible(ba.build_ids[ES2_BUILD_GRID + 1]));
    CHECK(col(brow(0), 0)->text_colour == 0x27 && es2_ui_get(ui, brow(0))->colour == 0x10);
    CHECK(!strcmp(bw(ES2_BUILD_COST)->text, "110 TC6"));
    /* bay 2 (empty): Scrap off; Build off (47,000 is not above 110,000) */
    CHECK(es2_build_select_bay(&ba, 2) == ES2_OK && session.bay == 2);
    CHECK(bw(ES2_BUILD_SCRAP)->enabled == 0 && bw(ES2_BUILD_BUILD)->enabled == 0);
    /* the hide */
    es2_build_hide(&ba);
    step();
    CHECK(!visible(ba.build_ids[ES2_BUILD_PANEL]) && !visible(ba.build_ids[ES2_BUILD_GRID + 1]));
    CHECK(!ui->layers[bw(ES2_BUILD_GRID)->grid][0].bitmap);
    CHECK(changes == 2);
}

static void test_build_build(void) {
    fresh(200000, 0, 2);
    CHECK(es2_build_show(&ba) == ES2_OK);
    step();
    /* 200,000 - 3,000 > 100,000: Build on */
    CHECK(bw(ES2_BUILD_BUILD)->enabled == 1 && bw(ES2_BUILD_SCRAP)->enabled == 0);
    CHECK(hangar_count() == 2);
    click_id(ba.build_ids[ES2_BUILD_BUILD], 0);
    CHECK(ba.last_handler == ES2_BUILD_ON_BUILD && ba.last_applied == 1 && squads == 1);
    CHECK(hangar_count() == 3 && salvage_now() == 100000);
    CHECK(!strcmp(bw(ES2_BUILD_AVAILABLE)->text, "97 TC6"));
    /* a bay with a record under construction: Build off; Scrap needs the bank entry of vehicle 0: on */
    CHECK(bw(ES2_BUILD_BUILD)->enabled == 0 && bw(ES2_BUILD_SCRAP)->enabled == 1);
    /* the second show keeps type 0 (no re-selection) */
    es2_build_hide(&ba);
    session.screen = ES2_SHELL_SCREEN_BAY;
    CHECK(es2_build_show(&ba) == ES2_OK && session.herc_type == 0);
    step();
    CHECK(visible(ba.build_ids[ES2_BUILD_GRID]));
}

static void test_armory_auto(void) {
    fresh(50000, 0, 0);
    CHECK(es2_armory_show(&ba) == ES2_OK);
    step();
    CHECK(session.screen == ES2_SHELL_SCREEN_ARMORY && session.armory_row == 0);
    CHECK(visible(ba.armory_ids[ES2_ARMORY_PANEL]) && visible(ba.armory_ids[ES2_ARMORY_LIST]));
    CHECK(visible(ba.images[0]) && !visible(ba.images[1]));
    CHECK(aw(ES2_ARMORY_CLEAR)->enabled == 0 && aw(ES2_ARMORY_CLEAR)->colour == 0x26);
    /* rows of categories 5 and 9 disabled */
    for (int r = 0; r < 26; r++) CHECK(es2_ui_get(ui, arow(r))->enabled == (r != 4 && r != 8));
    CHECK(col(arow(4), 0)->text_colour == 0x10);
    CHECK(!strcmp(col(arow(1), 0)->text, "[ 1 ]") && !strcmp(col(arow(0), 0)->text, "[   ]"));
    CHECK(!strcmp(col(arow(0), 2)->text, "2") && col(arow(0), 2)->text_colour == 0x29);
    CHECK(!strcmp(col(arow(2), 2)->text, "1") && col(arow(2), 2)->text_colour == 0x27);
    CHECK(!strcmp(label_text(aw(ES2_ARMORY_AVAILABLE)), "47000 TC8"));
    CHECK(!strcmp(label_text(aw(ES2_ARMORY_BANK)), "3000 TC8"));
    CHECK(!strcmp(aw(ES2_ARMORY_FREE)->text, "4") && !strcmp(aw(ES2_ARMORY_USED)->text, "1"));
    CHECK(!strcmp(aw(ES2_ARMORY_INFO)->text, "INFO 0") && !strcmp(aw(ES2_ARMORY_INFO + 4)->text, "INFO 4"));
    /* auto: a left press on the selected row selects (nothing), another row is selected */
    click_id(arow(0), 0);
    CHECK(session.armory_row == 0 && ba.last_applied == 0);
    click_id(arow(2), 1);
    CHECK(session.armory_row == 2 && ba.last_applied == 1);
    CHECK(!visible(ba.images[0]) && visible(ba.images[2]));
    CHECK(col(arow(0), 1)->text_colour == 0x27 && col(arow(2), 1)->text_colour == 0x29);
    CHECK(!strcmp(aw(ES2_ARMORY_INFO + 1)->text, "INFO 11"));
    /* a disabled row */
    click_id(arow(4), 0);
    CHECK(session.armory_row == 2);
    /* the empty "+" and "-" */
    CHECK(es2_build_armory_press(&ba, ES2_ARMORY_ON_PLUS, 1) == 0 && session.armory_row == 2);
    /* the hide */
    es2_armory_hide(&ba);
    step();
    CHECK(session.armory_row == -1 && !visible(ba.armory_ids[ES2_ARMORY_PANEL]) && !visible(ba.images[2]));
}

static void test_armory_manual(void) {
    fresh(50000, 1, 0);
    CHECK(es2_armory_show(&ba) == ES2_OK);
    step();
    CHECK(aw(ES2_ARMORY_CLEAR)->enabled == 1 && aw(ES2_ARMORY_CLEAR)->colour == 0x22);
    /* add category 1 (cost 2000) twice on the selected row 0 */
    click_id(arow(0), 0);
    CHECK(ba.last_applied == 1 && !strcmp(col(arow(0), 0)->text, "[ 1 ]") && col(arow(0), 0)->text_colour == 0x29);
    click_id(arow(0), 0);
    CHECK(!strcmp(col(arow(0), 0)->text, "[ 2 ]") && !strcmp(aw(ES2_ARMORY_FREE)->text, "2"));
    CHECK(!strcmp(label_text(aw(ES2_ARMORY_BANK)), "7000 TC8"));
    CHECK(!strcmp(label_text(aw(ES2_ARMORY_AVAILABLE)), "43000 TC8"));
    /* remove (right button): one entry fewer */
    click_id(arow(0), 1);
    CHECK(!strcmp(col(arow(0), 0)->text, "[ 1 ]") && !strcmp(aw(ES2_ARMORY_FREE)->text, "3"));
    /* Clear */
    click_id(arow(0), 0);
    CHECK(!strcmp(col(arow(0), 0)->text, "[ 2 ]"));
    click_id(ba.armory_ids[ES2_ARMORY_CLEAR], 0);
    CHECK(ba.last_handler == ES2_ARMORY_ON_CLEAR && ba.last_applied == 1);
    CHECK(!strcmp(col(arow(0), 0)->text, "[   ]") && !strcmp(aw(ES2_ARMORY_FREE)->text, "4"));
    /* a full bank: no more entries; another row's left press selects it */
    for (int k = 0; k < 6; k++) click_id(arow(0), 0);
    CHECK(!strcmp(col(arow(0), 0)->text, "[ 4 ]") && !strcmp(aw(ES2_ARMORY_FREE)->text, "0"));
    CHECK(ba.last_applied == 0);
    click_id(arow(3), 0);
    CHECK(session.armory_row == 3);
}

static void test_weapon_scrap(void) {
    fresh(50000, 0, 0);
    CHECK(es2_armory_show(&ba) == ES2_OK);
    step();
    /* row 0: category 1, two items of cost 2000: max(1, 2 * 2 / 10) = 1 */
    click_id(ba.armory_ids[ES2_ARMORY_SCRAP], 0);
    CHECK(visible(ba.scrap_ids[ES2_WEAPON_SCRAP_ALERT]) && visible(ba.scrap_ids[ES2_WEAPON_SCRAP_SHIELD]));
    CHECK(ba.box_category == 1);
    CHECK(!strcmp(es2_ui_get(ui, ba.scrap_ids[ES2_WEAPON_SCRAP_NUMBER])->text, "1 TCD"));
    click_id(ba.scrap_ids[ES2_WEAPON_SCRAP_CANCEL], 0);
    CHECK(!visible(ba.scrap_ids[ES2_WEAPON_SCRAP_ALERT]) && !visible(ba.scrap_ids[ES2_WEAPON_SCRAP_SHIELD]));
    CHECK(salvage_now() == 50000);
    click_id(ba.armory_ids[ES2_ARMORY_SCRAP], 0);
    click_id(ba.scrap_ids[ES2_WEAPON_SCRAP_ACCEPT], 0);
    CHECK(ba.last_handler == ES2_WEAPON_SCRAP_ON_ACCEPT && ba.last_applied == 1);
    CHECK(!visible(ba.scrap_ids[ES2_WEAPON_SCRAP_ALERT]));
    CHECK(salvage_now() == 51000 && !strcmp(col(arow(0), 2)->text, "0"));
    /* then 0x412a37 in auto building refills the bank by priority (the library's restock) */
    CHECK(!strcmp(label_text(aw(ES2_ARMORY_AVAILABLE)), "38000 TC8") && !strcmp(aw(ES2_ARMORY_FREE)->text, "0"));
    CHECK(!strcmp(label_text(aw(ES2_ARMORY_BANK)), "13000 TC8"));
    /* 0x4499b9 after the scrap: the selected row's columns back to 0x27 */
    CHECK(col(arow(0), 1)->text_colour == 0x27);
}

static void test_refusals(void) {
    es2_build_armory none;
    fresh(50000, 0, 0);
    CHECK(es2_screen_build_build(NULL, &ba) == -1 && es2_screen_build_build(ui, NULL) == -1);
    CHECK(es2_screen_armory_build(NULL, &ba) == -1 && es2_screen_weapon_scrap_box_build(ui, NULL) == -1);
    CHECK(es2_build_armory_press(&ba, 0x431b9b, 1) == -1);
    CHECK(es2_build_armory_press(NULL, ES2_BUILD_ON_BUILD, 1) == -1);
    es2_build_armory_init(&none, NULL, NULL, NULL);
    CHECK(es2_build_show(&none) == ES2_ERR_ARGUMENT && es2_armory_show(&none) == ES2_ERR_ARGUMENT);
    CHECK(es2_build_select_bay(&none, 0) == ES2_ERR_ARGUMENT);
    es2_build_hide(&none);
    es2_armory_hide(&none);
    /* the library refuses an ARMORY operation while BUILD is shown */
    CHECK(es2_build_show(&ba) == ES2_OK);
    CHECK(es2_build_armory_press(&ba, ES2_ARMORY_ON_ROW0, 1) == -1 && ba.last_status == ES2_ERR_ARGUMENT);
    /* the library refuses Build without a bay selection */
    session.bay = -1;
    CHECK(es2_build_armory_press(&ba, ES2_BUILD_ON_BUILD, 1) == -1 && ba.last_status == ES2_ERR_OFFSET);
    /* a ui of another screen set is refused */
    {
        es2_ui *other = calloc(1, sizeof *other);
        CHECK(es2_ui_init(other, shell) == 0);
        CHECK(es2_screen_build_build(other, &ba) == -1);
        es2_ui_release(other);
        free(other);
    }
}

/* The cost column comes from the start-up weapon table (0x401d4e
 * before the construction 0x401ff8): an ARMORY built without a loaded game
 * shows the costs as one built with it. */
static void test_armory_costs_without_game(void) {
    static es2_build_armory nogame;
    es2_build_armory_host host = {NULL, h_font, h_bitmap, h_text, h_name, h_info, h_file, h_scrap, h_squad, h_changed};
    es2_build_armory_game g;
    es2_ui *u = calloc(1, sizeof *u);
    const es2_ui_screen_entry *e = es2_ui_screen_find(ES2_UI_SCREEN_ARMORY);
    fresh(50000, 0, 0);
    g = game;
    g.save = (es2_shell_buffer){NULL, 0};
    CHECK(u && es2_ui_init(u, shell) == 0);
    es2_build_armory_init(&nogame, &flow, &g, &host);
    CHECK(e && e->build(u, &nogame) == 0);
    for (int r = 0; r < ES2_SHELL_ARMORY_ROWS; r++) {
        es2_ui_widget *mine = es2_ui_get(u, es2_ui_get(u, nogame.armory_ids[ES2_ARMORY_ROW + 5 * r])->columns[3]);
        CHECK(mine && !strcmp(mine->text, col(arow(r), 3)->text));
    }
    CHECK(!strcmp(es2_ui_get(u, es2_ui_get(u, nogame.armory_ids[ES2_ARMORY_ROW + 15])->columns[3])->text, "1"));
    es2_ui_release(u);
    free(u);
}

int main(void) {
    if (mmf_font(font_bytes, &font)) {
        printf("FAIL font\n");
        return 1;
    }
    test_build_screen();
    test_build_build();
    test_armory_auto();
    test_armory_manual();
    test_weapon_scrap();
    test_refusals();
    test_armory_costs_without_game();
    if (ui) es2_ui_release(ui);
    free(ui);
    free(shell);
    free(game.save.data);
    printf("build_armory_test: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
