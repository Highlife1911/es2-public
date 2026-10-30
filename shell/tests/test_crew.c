/* The CREW screen (es2/shell/gui/crew.h) with the service bay
 * on synthetic resources, and the editors library (es2_shell_editors) on
 * crew_fixture.h's synthetic SAV as the host's game state. Expected values
 * follow from vshell_ui.md sections 17, 28 and 29; the last test checks
 * that the screen's sequence leaves the same SAV and session as the same
 * operations given to the library directly. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "crew_fixture.h"
#include "es2/shell/gui/crew.h"
#include "es2/shell/gui/loop.h"
#include "es2/shell/gui/service_bay.h"
#include "main_menu_fixture.h"

static unsigned checks, failures;
#define CHECK(x) do { checks++; if (!(x)) { failures++; fprintf(stderr, "line %d: %s\n", __LINE__, #x); } } while (0)

static es2_gui_shell *shell;
static es2_ui *ui;
static es2_shell_flow flow;
static es2_service_bay bay;
static es2_crew crew;
static crew_world world;
static es2_dfn font;
static uint8_t font_bytes[8 + 26 + MMF_COUNT * (MMF_W * MMF_H + 4) + 2];
static uint8_t portrait_pixels[16][4 * 4], star_pixels[4 * 4];
static es2_bitmap portraits[16], star;
static char textbuf[16];
static char log_[512];
static int rebalances;

static void logs(const char *s) {
    size_t n = strlen(log_);
    snprintf(log_ + n, sizeof log_ - n, "%s ", s);
}

/* ---- hosts ------------------------------------------------------------------------- */
static const es2_dfn *h_font(void *c, uint32_t g) { (void)c; (void)g; return &font; }
static const es2_bitmap *h_bitmap(void *c, const char *file, int frame) {
    (void)c;
    if (!strcmp(file, "dba\\c_pilots.dba") && frame >= 0 && frame < 16) return &portraits[frame];
    if (!strcmp(file, "dba\\star.dba") && frame == 0) return &star;
    return NULL;
}
static const char *h_text(void *c, uint32_t id) {
    (void)c;
    snprintf(textbuf, sizeof textbuf, "T%02X", (unsigned)id);
    return textbuf;
}
/* the service bay's hooks route CREW's parts to the screen */
static void h_screen(void *c, int word, int show) {
    char s[16];
    (void)c;
    snprintf(s, sizeof s, "%c%d", show ? 'S' : 'L', word);
    logs(s);
    if (word == ES2_SHELL_SCREEN_CREW) {
        if (show) es2_crew_show(&crew);
        else es2_crew_leave(&crew);
    }
}
static void h_selected(void *c, int word, int16_t k, int assign) {
    char s[24];
    (void)c;
    snprintf(s, sizeof s, "E%d:%d:%d", word, k, assign);
    logs(s);
    if (word == ES2_SHELL_SCREEN_CREW && assign) es2_crew_selected(&crew, k);
}
static int f_rebalance(void *c, uint8_t b) { (void)c; (void)b; rebalances++; return 1; }

static void fresh(int capacity, int use_library) {
    es2_shell_flow_host fh;
    const es2_ui_screen_entry *e;
    if (ui) es2_ui_release(ui);
    free(ui);
    free(shell);
    log_[0] = 0;
    rebalances = 0;
    cw_init(&world);
    cw_w(world.team, (unsigned)capacity);
    CHECK(cw_open(&world) == 0);
    shell = calloc(1, sizeof *shell);
    ui = calloc(1, sizeof *ui);
    es2_gui_shell_init(shell, NULL, NULL, NULL);
    CHECK(es2_ui_init(ui, shell) == 0);
    es2_ui_install(ui, &shell->hooks);
    memset(&fh, 0, sizeof fh);
    fh.rebalance = f_rebalance;
    es2_shell_flow_init(&flow, &fh, NULL);
    flow.mode = 1;
    flow.game = ES2_FLOW_GAME_CAMPAIGN;
    flow.final_word = 2;
    flow.screen = ES2_FLOW_SERVICE_BAY;
    memset(&bay, 0, sizeof bay);
    bay.flow = &flow;
    bay.host.ctx = &world;
    bay.host.font = h_font;
    bay.host.text = h_text;
    bay.host.screen = h_screen;
    bay.host.bay = cw_bay_view;
    bay.host.selected = h_selected;
    for (int i = 0; i < 5; i++) bay.band_limit[i] = (int16_t)(89 - 20 * i);
    for (int i = 0; i < 6; i++) bay.band_colour[i] = 40 + i;
    CHECK(es2_screen_service_bay_build(ui, &bay) == 0 && es2_screen_squad_panel_build(ui, &bay) == 0);
    memset(&crew, 0, sizeof crew);
    crew.bay = &bay;
    crew.host = (es2_crew_host){&world, h_font, h_bitmap, h_text, cw_team, use_library ? cw_edit : NULL, NULL};
    e = es2_ui_screen_find(ES2_UI_SCREEN_CREW);
    CHECK(e && e->build(ui, &crew) == 0);
    es2_ui_show(ui, ui->root);
    es2_service_bay_show(&bay);
    bay.loop_started = 1;
}

static es2_ui_widget *ctl(int k) { return es2_ui_get(ui, crew.ids[k]); }
static int visible(es2_ui_id id) {
    es2_ui_widget *w = es2_ui_get(ui, id);
    return w && !(w->flags & ES2_UI_HIDDEN);
}
static const char *txt(int k) { return ctl(k)->text; }
static void click_id(es2_ui_id id) {
    es2_ui_widget *w = es2_ui_get(ui, id);
    int x = (w->abs.left + w->abs.right) / 2, y = (w->abs.top + w->abs.bottom) / 2;
    es2_gui_on_mouse(shell, ES2_GUI_WM_MOUSEMOVE, x, y, 0, 0);
    ui->now += 100;
    es2_gui_step(shell);
    es2_gui_on_mouse(shell, ES2_GUI_WM_LBUTTONDOWN, x, y, 1, 0);
    ui->now += 100;
    es2_gui_step(shell);
    es2_gui_on_mouse(shell, ES2_GUI_WM_LBUTTONUP, x, y, 0, 0);
    ui->now += 100;
    es2_gui_step(shell);
}
static void click(int k) { click_id(crew.ids[k]); }
static void selector(int k) { click_id(bay.squad[ES2_SQUAD_ROW + 5 * k]); }
static void open_crew(void) { CHECK(es2_service_bay_press(&bay, ES2_FLOW_BAY_CREW) == ES2_FLOW_DONE); }

/* ---- tests ------------------------------------------------------------------------- */
static void test_build(void) {
    fresh(4, 1);
    for (int k = 0; k < ES2_SCREEN_CREW_CONTROLS; k++) CHECK(crew.ids[k] != 0);
    CHECK(!visible(crew.ids[ES2_CREW_FRAME]));
    /* the constructor's fields (table): group images frame colour 0x21 and
     * c_pilots 0, the player's row image star.dba 0, the values opaque */
    CHECK(ctl(ES2_CREW_GROUP + 1)->colour == 0x21 && ctl(ES2_CREW_GROUP + 1)->bitmap == &portraits[0]);
    CHECK(ctl(ES2_CREW_IMAGE)->bitmap == &star && ctl(ES2_CREW_IMAGE + 1)->bitmap == NULL);
    CHECK(ctl(ES2_CREW_NAME + 2)->opaque == 1 && !strcmp(txt(ES2_CREW_NAME + 2), " "));
    CHECK(!strcmp(txt(ES2_CREW_NAME_CAPTION + 3), "TAC") && !strcmp(txt(ES2_CREW_VEHICLE_CAPTION), "TAE"));
    CHECK(ctl(ES2_CREW_NAME_CAPTION)->text_colour == 17);
    /* not this screen's handler */
    CHECK(es2_crew_press(&crew, 0x43a862u) == 0);
}

static void test_show(void) {
    fresh(4, 1);
    open_crew();
    /* the bar: panel hidden with nothing, the word 6 before the show */
    CHECK(!strcmp(log_, "S6 "));
    CHECK(bay.session.screen == 6 && visible(crew.ids[ES2_CREW_FRAME]));
    /* 0x442198: order 1 BAKER (group 0), 2 COLE (group 1), 3 nobody; the
     * order rows end on 0; the bays 0..4 with a record: 4 is unfinished,
     * 3 (vehicle 8) is taken with order 0 */
    CHECK(bay.session.crew[0] == 0 && bay.session.crew[1] == 1 && bay.session.crew[2] == -1);
    CHECK(bay.session.order == 0 && bay.session.bay == 3);
    CHECK(crew.edits == 1 && crew.edit_failures == 0 && crew.mismatches == 0);
    /* group images: the selected pilots' portraits */
    CHECK(ctl(ES2_CREW_GROUP)->bitmap == &portraits[3] && ctl(ES2_CREW_GROUP + 1)->bitmap == &portraits[7] &&
          ctl(ES2_CREW_GROUP + 2)->bitmap == &portraits[11]);
    /* rows: the player (skill 2 -> 0x37, vehicle 1 -> 0x6f), BAKER (skill 1,
     * bay 1 vehicle 2 -> 0x70), COLE (skill 3, bay 2 vehicle 0 -> 0x6e), the
     * empty order 3 (ESTEXT 0, no image) */
    CHECK(!strcmp(txt(ES2_CREW_NAME), "PLAYER") && !strcmp(txt(ES2_CREW_SKILL), "T37") &&
          !strcmp(txt(ES2_CREW_VEHICLE), "T6F"));
    CHECK(!strcmp(txt(ES2_CREW_NAME + 1), "BAKER") && !strcmp(txt(ES2_CREW_SKILL + 1), "T36") &&
          !strcmp(txt(ES2_CREW_VEHICLE + 1), "T70") && ctl(ES2_CREW_IMAGE + 1)->bitmap == &portraits[3]);
    CHECK(!strcmp(txt(ES2_CREW_NAME + 2), "COLE") && !strcmp(txt(ES2_CREW_SKILL + 2), "T38") &&
          !strcmp(txt(ES2_CREW_VEHICLE + 2), "T6E") && ctl(ES2_CREW_IMAGE + 2)->bitmap == &portraits[7]);
    CHECK(!strcmp(txt(ES2_CREW_NAME + 3), "T00") && !strcmp(txt(ES2_CREW_VEHICLE + 3), "T00") &&
          ctl(ES2_CREW_IMAGE + 3)->bitmap == NULL);
    /* colours 0x441ee7 with capacity 4 (all rows below it), the texts in 0x29 */
    for (int o = 0; o < 4; o++) {
        CHECK(ctl(ES2_CREW_ROW + o)->f59 == 0x25 && ctl(ES2_CREW_SKILL_CAPTION + o)->text_colour == 0x19);
        CHECK(ctl(ES2_CREW_VEHICLE + o)->background == 0x25 && ctl(ES2_CREW_VEHICLE + o)->text_colour == 0x29);
        CHECK(ctl(ES2_CREW_ROW + o)->colour == (o == 0 ? 0x29 : 0x21) && ctl(ES2_CREW_IMAGE + o)->colour == (o == 0 ? 0x29 : 0x21));
    }
    /* no state changed */
    CHECK(cw_count(&world) == 3 && cw_word(&world, 0, 0x27) == 1 && cw_word(&world, 36, 0x22) == 0);
    /* capacity 2: rows 2 and 3 at or above it */
    fresh(2, 1);
    open_crew();
    CHECK(ctl(ES2_CREW_ROW + 1)->f59 == 0x25 && ctl(ES2_CREW_ROW + 2)->f59 == 0x10);
    CHECK(ctl(ES2_CREW_NAME_CAPTION + 3)->text_colour == 0x1a && ctl(ES2_CREW_NAME + 3)->background == 0x10);
    CHECK(ctl(ES2_CREW_NAME_CAPTION + 1)->text_colour == 0x19 && ctl(ES2_CREW_NAME + 1)->background == 0x25);
    /* leaving: the squad panel hidden with 2 (sic), the frame hidden */
    log_[0] = 0;
    CHECK(es2_service_bay_press(&bay, ES2_FLOW_BAY_WEAPONS) == ES2_FLOW_DONE);
    CHECK(!strncmp(log_, "L6 ", 3) && !visible(crew.ids[ES2_CREW_FRAME]) && rebalances == 0);
}

static void test_orders_and_assignment(void) {
    fresh(4, 1);
    open_crew();
    /* order row 3 (no pilot): selection -1, the ninth grid */
    click(ES2_CREW_IMAGE + 3);
    CHECK(bay.session.order == 3 && bay.session.bay == -1 && visible(bay.squad[ES2_SQUAD_GRID_NONE]));
    CHECK(ctl(ES2_CREW_ROW + 3)->colour == 0x29 && ctl(ES2_CREW_ROW)->colour == 0x21 && ctl(ES2_CREW_IMAGE)->colour == 0x21);
    /* group 2 (DIAZ) takes order 3 */
    click(ES2_CREW_GROUP + 2);
    CHECK(ctl(ES2_CREW_GROUP + 2)->colour == 0x29 && ctl(ES2_CREW_GROUP)->colour == 0x21);
    CHECK(bay.session.crew[2] == 2 && cw_word(&world, 24, 0x27) == 3 && cw_word(&world, 24, 0x22) == -1);
    CHECK(!strcmp(txt(ES2_CREW_NAME + 3), "DIAZ") && !strcmp(txt(ES2_CREW_VEHICLE + 3), "T7E") &&
          ctl(ES2_CREW_IMAGE + 3)->bitmap == &portraits[11]);
    /* the vehicle-8 bay is refused while a wingman order is selected */
    log_[0] = 0;
    selector(3);
    CHECK(bay.session.bay == -1 && log_[0] == 0);
    /* the player's bay 0: taken from the player, given to DIAZ, activated */
    selector(0);
    CHECK(!strcmp(log_, "E6:0:1 ") && bay.session.bay == 0);
    CHECK(cw_word(&world, 36, 0x22) == -1 && cw_word(&world, 24, 0x22) == 0 && cw_active(&world, 24) == 1 &&
          cw_count(&world) == 4);
    CHECK(!strcmp(txt(ES2_CREW_VEHICLE), "T7E") && !strcmp(txt(ES2_CREW_VEHICLE + 3), "T6F"));
    /* the squad panel follows (0x43e167): bay 0's pilot is DIAZ */
    CHECK(!strcmp(es2_ui_get(ui, es2_ui_get(ui, bay.squad[ES2_SQUAD_ROW])->columns[3])->text, "DIAZ"));
    /* order 1 selects BAKER's bay 1; DIAZ takes order 1: BAKER keeps bay 1
     * until the assignment takes it (bay -1, inactive), DIAZ moves to bay 1 */
    click(ES2_CREW_IMAGE + 1);
    CHECK(bay.session.order == 1 && bay.session.bay == 1);
    click(ES2_CREW_GROUP + 2);
    CHECK(bay.session.crew[0] == 2 && bay.session.crew[2] == -1);
    CHECK(cw_word(&world, 0, 0x27) == -1 && cw_word(&world, 0, 0x22) == -1 && cw_active(&world, 0) == 0);
    CHECK(cw_word(&world, 24, 0x27) == 1 && cw_word(&world, 24, 0x22) == 1 && cw_count(&world) == 3);
    CHECK(!strcmp(txt(ES2_CREW_NAME + 1), "DIAZ") && !strcmp(txt(ES2_CREW_NAME + 3), "T00"));
    CHECK(!strcmp(es2_ui_get(ui, bay.squad[ES2_SQUAD_PILOT])->text, "DIAZ"));
    /* CLEAR: DIAZ loses bay and order, inactive; selection -1 */
    click(ES2_CREW_CLEAR);
    CHECK(bay.session.bay == -1 && bay.session.crew[0] == -1 && cw_word(&world, 24, 0x22) == -1 &&
          cw_word(&world, 24, 0x27) == -1 && cw_active(&world, 24) == 0 && cw_count(&world) == 2);
    CHECK(!strcmp(txt(ES2_CREW_NAME + 1), "T00") && visible(bay.squad[ES2_SQUAD_GRID_NONE]));
    /* CLEAR again (order 1 has nobody) and with order 0: nothing */
    unsigned edits = crew.edits;
    click(ES2_CREW_CLEAR);
    click(ES2_CREW_IMAGE);
    click(ES2_CREW_CLEAR);
    CHECK(crew.edits == edits + 1 && bay.session.order == 0);
    /* a group image with order 0: colours only */
    click(ES2_CREW_GROUP + 1);
    CHECK(crew.edits == edits + 1 && ctl(ES2_CREW_GROUP + 1)->colour == 0x29 && bay.session.crew[1] == 1);
    CHECK(crew.edit_failures == 0 && crew.mismatches == 0 && world.failures == 0);
}

/* The screen's presses give the library the same operations as this list. */
static void test_same_as_library(void) {
    static crew_world direct;
    es2_shell_editor_session x;
    es2_shell_editor_op op[9] = {
        {ES2_SHELL_EDITOR_OPEN, ES2_SHELL_SCREEN_CREW, 0, 0}, {ES2_SHELL_EDITOR_CREW_ORDER, 3, 0, 0},
        {ES2_SHELL_EDITOR_CREW_PILOT, 2, 0, 0},              {ES2_SHELL_EDITOR_BAY, 3, 0, 0},
        {ES2_SHELL_EDITOR_BAY, 0, 0, 0},                     {ES2_SHELL_EDITOR_CREW_ORDER, 2, 0, 0},
        {ES2_SHELL_EDITOR_CREW_PILOT, 0, 0, 0},              {ES2_SHELL_EDITOR_BAY, 1, 0, 0},
        {ES2_SHELL_EDITOR_CREW_CLEAR, 0, 0, 0}};
    fresh(3, 1);
    open_crew();
    click(ES2_CREW_IMAGE + 3);
    click(ES2_CREW_GROUP + 2);
    selector(3);
    selector(0);
    click(ES2_CREW_IMAGE + 2);
    click(ES2_CREW_GROUP);
    selector(1);
    click(ES2_CREW_CLEAR);
    cw_init(&direct);
    cw_w(direct.team, 3);
    CHECK(cw_open(&direct) == 0);
    es2_shell_editor_session_init(&x, 0);
    for (int i = 0; i < 9; i++) CHECK(cw_edit(&direct, &x, &op[i]) == ES2_OK);
    CHECK(direct.size == world.size && !memcmp(direct.image, world.image, world.size));
    CHECK(x.screen == bay.session.screen && x.bay == bay.session.bay && x.order == bay.session.order &&
          !memcmp(x.crew, bay.session.crew, sizeof x.crew));
    CHECK(crew.mismatches == 0);
    /* without the library the screen keeps its own session the same way */
    fresh(3, 0);
    open_crew();
    click(ES2_CREW_IMAGE + 3);
    click(ES2_CREW_GROUP + 2);
    CHECK(bay.session.order == 3 && bay.session.crew[2] == 2 && bay.session.bay == -1 && crew.edits == 0);
}

int main(void) {
    if (mmf_font(font_bytes, &font)) return 2;
    for (int f = 0; f < 16; f++) {
        memset(portrait_pixels[f], 20 + f, sizeof portrait_pixels[f]);
        portraits[f] = (es2_bitmap){4, 4, 0, portrait_pixels[f]};
    }
    memset(star_pixels, 9, sizeof star_pixels);
    star = (es2_bitmap){4, 4, 0, star_pixels};
    test_build();
    test_show();
    test_orders_and_assignment();
    test_same_as_library();
    if (ui) es2_ui_release(ui);
    free(ui);
    free(shell);
    printf("%u checks, %u failures\n", checks, failures);
    return failures != 0;
}
