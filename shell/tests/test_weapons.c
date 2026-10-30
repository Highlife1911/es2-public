/* The WEAPONS screen (es2/shell/gui/weapons.h) on synthetic state
 * (weapons_fixture.h: an engine over a composed SAV, a synthetic row table,
 * synthetic ARM answers, a synthetic gam\arm_weap.dat and frames): the
 * record parser, the build (controls, images at the templates' place, rows
 * enabled by availability), the show with the bay rule, the row selection
 * 0x43fdac with its early returns and the mount, the list refresh, the
 * previous row's restore, the guidance panel and buttons, the description
 * button, slot prev/next and the slot selection, the squad panel's case, the
 * refusals. */
#include "es2/shell/gui/weapons.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "main_menu_fixture.h"
#include "weapons_fixture.h"

static unsigned checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static uint8_t font_bytes[16384];
static es2_dfn font;
static wpf fx;
static es2_gui_shell *shell;
static es2_ui *ui;
static es2_weapons_screen ws;
static int16_t selection;

typedef struct host_log {
    int selects, shown, changed;
    int16_t selected, last_slot;
    int previous;
    uint32_t changed_handler;
    int no_file;
} host_log;
static host_log hl;

static const es2_dfn *h_font(void *c, uint32_t g) { (void)c; return g == 0x46de00 ? &font : NULL; }
static const es2_bitmap *h_frame(void *c, const char *file, int frame) {
    (void)c;
    return !strcmp(file, "dba\\arm_weap.dba") && frame >= 0 && frame < WPF_FRAMES ? &fx.frames[frame] : NULL;
}
static es2_span h_file(void *c, const char *name) {
    (void)c;
    if (hl.no_file || strcmp(name, "gam\\arm_weap.dat")) return (es2_span){NULL, 0};
    return (es2_span){fx.arm_weap, fx.arm_weap_size};
}
static const char *fmt(char p, uint32_t id) {
    static char t[64][16];
    static unsigned n;
    char *s = t[n++ & 63];
    snprintf(s, sizeof t[0], "%c%X", p, (unsigned)id);
    return s;
}
static const char *h_text(void *c, uint32_t id) { (void)c; return fmt('T', id); }
static const char *h_desc(void *c, uint32_t id) { (void)c; return fmt('D', id); }
static const char *h_literal(void *c, uint32_t a) {
    (void)c;
    switch (a) {
    case 0x4774c1: return "  ";
    case 0x476ddb: case 0x476de1: return "0";
    case 0x476de3: return "<";
    case 0x476de5: return ">";
    default: return a >= 0x476dd7 && a <= 0x476ddf ? " " : "";
    }
}
static void h_select(void *c, int16_t bay, int previous) {
    (void)c;
    hl.selects++;
    hl.selected = bay;
    hl.previous = previous;
    selection = bay;
}
static void h_shown(void *c, int16_t slot) { (void)c; hl.shown++; hl.last_slot = slot; }
static void h_changed(void *c, uint32_t h) { (void)c; hl.changed++; hl.changed_handler = h; }

static void fresh(int finished0, int loaded, int with_select) {
    if (ui) es2_ui_release(ui);
    free(ui);
    free(shell);
    memset(&hl, 0, sizeof hl);
    CHECK(wpf_open(&fx, finished0) == 0);
    if (!loaded) es2_shell_engine_free(&fx.engine);
    shell = calloc(1, sizeof *shell);
    ui = calloc(1, sizeof *ui);
    es2_gui_shell_init(shell, NULL, NULL, NULL);
    CHECK(es2_ui_init(ui, shell) == 0);
    es2_ui_install(ui, &shell->hooks);
    memset(&ws, 0, sizeof ws);
    selection = 0;
    ws.engine = &fx.engine;
    ws.bay = &selection;
    ws.host = (es2_weapons_screen_host){NULL, h_font, h_frame, h_file, h_text, h_desc, h_literal,
                                        with_select ? h_select : NULL, h_shown, h_changed};
    const es2_ui_screen_entry *e = es2_ui_screen_find(ES2_UI_SCREEN_WEAPONS);
    CHECK(e && e->build(ui, &ws) == 0);
    es2_ui_show(ui, ui->root);
}
static void step(void) {
    ui->now += 100;
    es2_gui_step(shell);
}
static void steps(int n) { while (n-- > 0) step(); }
static es2_ui_widget *W(int control) { return es2_ui_get(ui, ws.ids[control]); }
static int visible(es2_ui_id id) {
    es2_ui_widget *w = es2_ui_get(ui, id);
    return w && !(w->flags & ES2_UI_HIDDEN);
}
static void click_control(int control) {
    es2_ui_widget *w = W(control);
    int x = (w->abs.left + w->abs.right) / 2, y = (w->abs.top + w->abs.bottom) / 2;
    es2_gui_on_mouse(shell, ES2_GUI_WM_MOUSEMOVE, x, y, 0, 0);
    step();
    es2_gui_on_mouse(shell, ES2_GUI_WM_LBUTTONDOWN, x, y, 1, 0);
    step();
    es2_gui_on_mouse(shell, ES2_GUI_WM_LBUTTONUP, x, y, 0, 0);
    step();
}
static int rowc(int r) { return ES2_WEAPONS_ROW0 + 5 * r; }
static es2_ui_widget *column(int r, int c) { return es2_ui_get(ui, W(rowc(r))->columns[c]); }
static const char *line(int k) { return W(ES2_WEAPONS_TEXT0 + k)->text; }
static char expect[16];
static const char *D(unsigned id) { snprintf(expect, sizeof expect, "D%X", id); return expect; }
static uint32_t row_handler(int r) { return ES2_WEAPONS_ROW_HANDLER0 + 0x6fu * (uint32_t)r; }
static es2_shell_bay_view view(int bay) {
    es2_shell_bay_view v;
    memset(&v, 0, sizeof v);
    CHECK(es2_shell_engine_bay_view(&fx.engine, bay, 0, &v, NULL) == ES2_OK);
    return v;
}
static int count_of(int id) {
    es2_shell_bay_view v = view(0);
    return v.row_count[wpf_row(id)];
}

static void records(void) {
    es2_weapons_record r[40];
    wpf_files(&fx);
    CHECK(es2_weapons_records((es2_span){fx.arm_weap, fx.arm_weap_size}, r, 40) == 29);
    CHECK(r[0].id == 1 && r[0].list == 1 && r[0].x == 29 && r[0].y == 13 && r[0].frame == 1);
    CHECK(r[25].id == 0 && r[25].list == 2 && r[25].x == 150 && r[25].y == 30 && r[25].frame == 6 && r[28].id == 3);
    CHECK(es2_weapons_records((es2_span){fx.arm_weap, fx.arm_weap_size}, NULL, 0) == 29);
    CHECK(es2_weapons_records((es2_span){fx.arm_weap, fx.arm_weap_size - 1}, r, 40) == -1);
    CHECK(es2_weapons_records((es2_span){fx.arm_weap, 1}, r, 40) == -1);
    CHECK(es2_weapons_records((es2_span){NULL, 4}, r, 40) == -1);
    {   /* bytes after the second list are not read; a negative count reads no record */
        uint8_t b[8] = {0xff, 0xff, 0, 0, 9, 9, 9, 9};
        CHECK(es2_weapons_records((es2_span){b, sizeof b}, r, 40) == 0);
    }
}

static void build(void) {
    fresh(1, 1, 1);
    for (int k = 0; k < ES2_WEAPONS_CONTROLS; k++) CHECK((k == 5 || k == 6) ? ws.ids[k] == 0 : ws.ids[k] != 0);
    CHECK(W(ES2_WEAPONS_PANEL)->cls == ES2_UI_TITLE && W(ES2_WEAPONS_ARM)->cls == ES2_UI_ARM &&
          W(ES2_WEAPONS_NO_IMAGE)->cls == ES2_UI_RECT && W(ES2_WEAPONS_NO_IMAGE)->frame == 0);
    static const struct { int control; uint32_t handler; } b[7] = {
        {ES2_WEAPONS_GUIDE_ARM, 0x440626}, {ES2_WEAPONS_GUIDE_ARH, 0x44068b}, {ES2_WEAPONS_GUIDE_SARH, 0x4406f0},
        {ES2_WEAPONS_GUIDE_EO, 0x440755}, {ES2_WEAPONS_DESCRIPTION, 0x4407ba}, {ES2_WEAPONS_SLOT_PREV, 0x4408d4},
        {ES2_WEAPONS_SLOT_NEXT, 0x440932}};
    for (int i = 0; i < 7; i++) CHECK(W(b[i].control)->original_handler == b[i].handler && W(b[i].control)->handler);
    CHECK(!strcmp(es2_ui_get(ui, W(ES2_WEAPONS_SLOT_PREV)->label)->text, "<") &&
          !strcmp(es2_ui_get(ui, W(ES2_WEAPONS_GUIDE_ARM)->label)->text, "TA1"));
    /* rows: handlers 0x440990 + 0x6f r, names ESTEXT 0x7e + table ID of the layout table's rows (synthetic host
     * text), enabled by the availability byte at construction */
    for (int r = 0; r < ES2_WEAPONS_ROWS; r++) {
        es2_ui_widget *q = W(rowc(r));
        int id = fx.tables.rows[r];
        CHECK(q->cls == ES2_UI_QUAD && q->original_handler == row_handler(r) && q->handler);
        CHECK(q->enabled == (id == 0 || id == 5 || id == 13 || id == 14 || id == 20));
    }
    /* images: list 1 by row, list 2 by mode, the two globals after them; ID 20 has none */
    for (int r = 0; r < 26; r++) {
        es2_ui_widget *im = es2_ui_get(ui, ws.pointers[r]);
        int id = r + 1;
        if (id == 20) { CHECK(!im); continue; }
        CHECK(im && im->cls == ES2_UI_BITMAP && im->parent == ws.ids[ES2_WEAPONS_ARM] && im->frame == 0 &&
              im->colour == 0x27 && (im->flags & ES2_UI_HIDDEN) && im->bitmap == &fx.frames[id % 6]);
        CHECK(im->rect.left == 20 + 9 * (id % 12) && im->rect.top == 6 + 7 * (id % 4) &&
              im->rect.right == im->rect.left + 12 + id % 6 && im->rect.bottom == im->rect.top + 8 + id % 6);
    }
    for (int m = 0; m < 4; m++) CHECK(es2_ui_get(ui, ws.pointers[26 + m])->bitmap == &fx.frames[6 + m]);
    CHECK(ws.pointers[30] == ws.ids[ES2_WEAPONS_NO_IMAGE] && ws.pointers[31] == ws.ids[ES2_WEAPONS_GUIDE_ARM]);
    /* the images are the ESArm's oldest children, list 1 first (VSHELL builds them before [0x48d6f8]) */
    {
        es2_ui_id c = W(ES2_WEAPONS_ARM)->first, order[64];
        int n = 0;
        while (c && n < 64) { order[n++] = c; c = es2_ui_get(ui, c)->next; }
        CHECK(n == 29 + 9 && order[n - 1] == ws.pointers[0] && order[n - 29] == ws.pointers[29] &&
              order[n - 30] == ws.ids[ES2_WEAPONS_NO_IMAGE] && order[0] == ws.ids[ES2_WEAPONS_TEXT0 + 2]);
        for (int k = 1; k < n; k++) CHECK(es2_ui_get(ui, order[k])->newer == order[k - 1]);
    }
    CHECK(ws.slot == -1 && ws.row == -1 && ws.guidance == -1 && ws.guidance_previous == -1 && !ws.panel && !ws.refused);
    CHECK(!visible(ws.ids[ES2_WEAPONS_PANEL]));
    /* nothing loaded: every row disabled; no file: no images; a malformed file refuses the build */
    fresh(1, 0, 1);
    for (int r = 0; r < ES2_WEAPONS_ROWS; r++) CHECK(W(rowc(r))->enabled == 0);
    {
        es2_ui *u2 = calloc(1, sizeof *u2);
        es2_weapons_screen w2;
        CHECK(es2_ui_init(u2, shell) == 0);
        memset(&w2, 0, sizeof w2);
        w2.host = ws.host;
        hl.no_file = 1;
        CHECK(es2_screen_weapons_build(u2, &w2) == 0 && !w2.pointers[0] && w2.pointers[30]);
        hl.no_file = 0;
        fx.arm_weap_size -= 3;
        memset(&w2, 0, sizeof w2);
        w2.host = ws.host;
        CHECK(es2_screen_weapons_build(u2, &w2) == -1);
        fx.arm_weap_size += 3;
        /* a list-1 ID of no row and a list-2 ID beyond the array are skipped (counted) */
        wpf_w16(fx.arm_weap + 2, 30);
        wpf_w16(fx.arm_weap + fx.arm_weap_size - 12, 4);
        memset(&w2, 0, sizeof w2);
        w2.host = ws.host;
        w2.engine = &fx.engine;
        CHECK(es2_screen_weapons_build(u2, &w2) == 0 && w2.refused == 2 && !w2.pointers[0]);
        wpf_files(&fx);
        es2_ui_release(u2);
        free(u2);
    }
    CHECK(es2_screen_weapons_build(NULL, &ws) == -1 && es2_screen_weapons_build(ui, NULL) == -1);
}

static void show(void) {
    fresh(1, 1, 1);
    es2_weapons_screen_show(&ws, -1);
    steps(2);
    CHECK(hl.selects == 0 && selection == 0);                  /* bay 0 holds a finished Herc: kept */
    CHECK(visible(ws.ids[ES2_WEAPONS_PANEL]) && visible(ws.ids[ES2_WEAPONS_LIST_PANEL]) &&
          visible(ws.ids[ES2_WEAPONS_ARM]) && visible(ws.ids[ES2_WEAPONS_NO_IMAGE]));
    CHECK(ws.slot == -1 && ws.row == 0 && ws.guidance == -1 && !ws.panel && hl.shown == 0);
    CHECK(visible(ws.pointers[0]) && !visible(ws.pointers[4]) && !visible(ws.ids[ES2_WEAPONS_GUIDE_ARM]) &&
          !visible(ws.ids[ES2_WEAPONS_DESCRIPTION]));
    CHECK(!strcmp(line(0), D(3)) && !strcmp(line(1), D(4)) && !strcmp(line(2), D(5)));
    /* row 0 (ID 1, not available): refreshed to 0x10 and disabled, then highlighted and enabled */
    CHECK(W(rowc(0))->enabled == 1 && W(rowc(0))->colour == 0x29 && column(0, 0)->text_colour == 0x29);
    /* the list refresh with no slot: 0x41501f admits every available row */
    CHECK(W(rowc(4))->enabled == 1 && column(4, 0)->text_colour == 0x27 && !strcmp(column(4, 3)->text, "2"));
    CHECK(W(rowc(6))->enabled == 0 && column(6, 0)->text_colour == 0x10 && !strcmp(column(6, 3)->text, "0"));
    CHECK(W(rowc(26))->enabled == 1 && !strcmp(column(26, 3)->text, "  ") && column(26, 3)->align == 1);
    CHECK(W(rowc(12))->enabled == 1 && !strcmp(column(12, 3)->text, "0"));
    /* hide, then the revisit: row 0, no guidance image, no slot: 0x43fdac returns at once */
    es2_weapons_screen_hide(&ws);
    CHECK(!visible(ws.ids[ES2_WEAPONS_PANEL]) && !visible(ws.ids[ES2_WEAPONS_ARM]));
    {
        es2_shell_bay_op op = {ES2_SHELL_BAY_MOUNT, 0, 1, 5, 0, 0};
        CHECK(es2_shell_engine_bay(&fx.engine, &op, NULL) == ES2_OK && count_of(5) == 1);
    }
    es2_weapons_screen_show(&ws, 3);
    CHECK(!strcmp(column(4, 3)->text, "2") && visible(ws.ids[ES2_WEAPONS_ARM]));   /* not refreshed */
    CHECK(es2_weapons_screen_row(&ws, 0, 0) == 0);
    /* the bay rule: -1, unfinished, no record -> select_bay(first finished, previous word) */
    fresh(1, 1, 1);
    selection = -1;
    es2_weapons_screen_show(&ws, 6);
    CHECK(hl.selects == 1 && hl.selected == 0 && hl.previous == 6 && selection == 0);
    fresh(1, 1, 1);
    selection = 1;
    es2_weapons_screen_show(&ws, 3);
    CHECK(hl.selects == 1 && hl.selected == 0 && hl.previous == 3);
    fresh(0, 1, 1);
    selection = 0;
    es2_weapons_screen_show(&ws, -1);
    CHECK(hl.selects == 1 && hl.selected == 2);
    fresh(1, 1, 0);   /* no squad panel: the word alone */
    selection = 5;
    es2_weapons_screen_show(&ws, -1);
    CHECK(selection == 0);
    fresh(1, 1, 0);
    ws.bay = NULL;
    ws.own_bay = 7;
    es2_weapons_screen_show(&ws, -1);
    CHECK(ws.own_bay == 0);
}

static void slots(void) {
    fresh(1, 1, 1);
    es2_weapons_screen_show(&ws, -1);
    steps(2);
    /* next from -1: slot 0 (item 5): row 4 selected without a mount, the marker twice */
    CHECK(es2_weapons_screen_press(&ws, ES2_WEAPONS_NEXT_HANDLER) == 1);
    CHECK(ws.slot == 0 && ws.row == 4 && hl.shown == 2 && hl.last_slot == 0 && ws.mounts == 0 && hl.changed == 0);
    CHECK(!visible(ws.pointers[0]) && visible(ws.pointers[4]) && !strcmp(line(0), D(15)));
    /* the list now answers for slot 0 of vehicle 1: 13 and 14 not admitted (greyed, disabled) */
    CHECK(W(rowc(12))->enabled == 0 && column(12, 0)->text_colour == 0x25);
    CHECK(W(rowc(4))->enabled == 1 && W(rowc(4))->colour == 0x29);
    /* the previous row (0, ID 1, not available) restored by 0x41501f alone: not admitted -> 0x25 */
    CHECK(W(rowc(0))->colour == 0x10 && column(0, 0)->text_colour == 0x25 && W(rowc(0))->enabled == 0);
    /* slot 1 (empty): the ID-0 row, the rect shown instead of an image */
    CHECK(es2_weapons_screen_press(&ws, ES2_WEAPONS_NEXT_HANDLER) == 1 && ws.slot == 1 && ws.row == 26);
    CHECK(!strcmp(line(0), D(0)) && !visible(ws.pointers[4]));
    /* slot 2 (rack 13, guidance 2): the panel, the description label, ARM highlighted (flag 0) */
    CHECK(es2_weapons_screen_press(&ws, ES2_WEAPONS_NEXT_HANDLER) == 1 && ws.slot == 2 && ws.row == 12);
    CHECK(ws.panel && visible(ws.ids[ES2_WEAPONS_GUIDE_SARH]) && visible(ws.ids[ES2_WEAPONS_DESCRIPTION]));
    CHECK(!strcmp(es2_ui_get(ui, W(ES2_WEAPONS_DESCRIPTION)->label)->text, "T8B"));
    CHECK(W(ES2_WEAPONS_GUIDE_ARM)->colour == 0x20 && W(ES2_WEAPONS_GUIDE_SARH)->colour == 0x22);
    CHECK(ws.guidance == -1 && ws.guidance_previous == 2 && ws.stores == 0 && visible(ws.pointers[12]));
    CHECK(!strcmp(line(0), D(39)));
    /* wrap and back */
    CHECK(es2_weapons_screen_press(&ws, ES2_WEAPONS_NEXT_HANDLER) == 1 && ws.slot == 0 && !ws.panel);
    CHECK(W(ES2_WEAPONS_GUIDE_ARM)->colour == 0x22);   /* 0x43fcd4 resets the four colours */
    CHECK(es2_weapons_screen_press(&ws, ES2_WEAPONS_PREV_HANDLER) == 1 && ws.slot == 2);
    CHECK(es2_weapons_screen_press(&ws, ES2_WEAPONS_PREV_HANDLER) == 1 && ws.slot == 1);
    CHECK(es2_weapons_screen_select_slot(&ws, 1) == 0);
    /* prev from -1 is capacity - 1 */
    es2_weapons_screen_bay_changed(&ws);
    CHECK(ws.slot == -1 && ws.row == 0);
    CHECK(es2_weapons_screen_press(&ws, ES2_WEAPONS_PREV_HANDLER) == 1 && ws.slot == 2);
    /* a bay without a record: refused (VSHELL divides by zero or reads a pilot as a record) */
    selection = 3;
    unsigned refused = ws.refused;
    CHECK(es2_weapons_screen_press(&ws, ES2_WEAPONS_NEXT_HANDLER) == -1 && ws.refused == refused + 1);
    CHECK(es2_weapons_screen_select_slot(&ws, 0) == -1);
    /* bay 2 slot 1 holds ID 30, no row: the slot is selected, the row selection not run */
    selection = 2;
    es2_weapons_screen_bay_changed(&ws);
    refused = ws.refused;
    CHECK(es2_weapons_screen_select_slot(&ws, 1) == 1 && ws.slot == 1 && ws.row == 0 && ws.refused == refused + 1);
    CHECK(es2_weapons_screen_select_slot(&ws, 10) == -1 && es2_weapons_screen_select_slot(&ws, -2) == -1);
}

static void mounts(void) {
    fresh(1, 1, 1);
    es2_weapons_screen_show(&ws, -1);
    steps(2);
    CHECK(es2_weapons_screen_select_slot(&ws, 1) == 1 && ws.row == 26);
    /* a click on row 4 (ID 5, two in the list): mounted into the empty slot 1 */
    click_control(rowc(4));
    CHECK(ws.mounts == 1 && hl.changed == 1 && hl.changed_handler == row_handler(4) && ws.row == 4);
    CHECK(view(0).item[0][1] == 5 && count_of(5) == 1 && !strcmp(column(4, 3)->text, "1"));
    /* the same SAV as the service bay's operation on the start state */
    {
        es2_shell_bay_op op = {ES2_SHELL_BAY_MOUNT, 0, 1, 5, 0, 0};
        es2_shell_buffer out = {0};
        CHECK(es2_shell_service_bay(&fx.vfs, (es2_span){fx.tables.results, ES2_SHELL_RESULTS_TABLES_SIZE},
                                    (es2_span){fx.sav, fx.sav_size}, &op, 1, &out, NULL) == ES2_OK);
        CHECK(out.size == fx.engine.memory.size && !memcmp(out.data, fx.engine.memory.data, out.size));
        free(out.data);
    }
    /* a disabled row does not fire (row 6, ID 7 not available) */
    click_control(rowc(6));
    CHECK(ws.mounts == 1 && ws.row == 4 && ws.events == 1);
    /* the description button reruns 0x43fdac(row) without the flag: no mount */
    CHECK(es2_weapons_screen_press(&ws, ES2_WEAPONS_DESCRIPTION_HANDLER) == 1 && ws.mounts == 1 && ws.row == 4);
    /* the early return: slot 0 holds 5 (word 5); ID 13 has no item and is not the slot's -> nothing */
    CHECK(es2_weapons_screen_select_slot(&ws, 0) == 1 && ws.row == 4);
    CHECK(es2_weapons_screen_press(&ws, row_handler(12)) == 0 && ws.row == 4 && ws.mounts == 1);
    /* the slot already holds the ID: mounted again (back to the list, the head taken) */
    CHECK(es2_weapons_screen_press(&ws, row_handler(4)) == 1 && ws.mounts == 2 && count_of(5) == 1);
    /* ID 0 empties the slot */
    CHECK(es2_weapons_screen_press(&ws, row_handler(26)) == 1 && ws.mounts == 3 && view(0).item[0][0] == 0 &&
          count_of(5) == 2);
    /* 0x43fe39 (sic): the empty slot's word 5 equals ID 5: the mount runs even with an empty list */
    {
        es2_shell_bay_op a = {ES2_SHELL_BAY_MOUNT, 2, 0, 5, 0, 0}, b = {ES2_SHELL_BAY_MOUNT, 2, 2, 5, 0, 0};
        CHECK(es2_shell_engine_bay(&fx.engine, &a, NULL) == ES2_OK && es2_shell_engine_bay(&fx.engine, &b, NULL) == ES2_OK);
        CHECK(count_of(5) == 0);
    }
    CHECK(es2_weapons_screen_press(&ws, row_handler(4)) == 1 && ws.mounts == 4 && view(0).item[0][0] == 0);
    /* row 19 (ID 20): no image; mounted into slot 1 after the slot's 5 went back */
    CHECK(es2_weapons_screen_select_slot(&ws, 1) == 1 && es2_weapons_screen_press(&ws, row_handler(19)) == 1);
    CHECK(view(0).item[0][1] == 20 && count_of(20) == 2 && count_of(5) == 1 && ws.row == 19);
    /* no slot: nothing mounted, no marker */
    es2_weapons_screen_bay_changed(&ws);
    int shown = hl.shown;
    CHECK(es2_weapons_screen_press(&ws, row_handler(4)) == 1 && ws.mounts == 5 && hl.shown == shown);
}

static void guidance(void) {
    fresh(1, 1, 1);
    es2_weapons_screen_show(&ws, -1);
    steps(2);
    CHECK(es2_weapons_screen_select_slot(&ws, 2) == 1 && ws.panel);
    click_control(ES2_WEAPONS_GUIDE_SARH);
    CHECK(ws.stores == 1 && hl.changed == 1 && hl.changed_handler == ES2_WEAPONS_SARH_HANDLER);
    CHECK(view(0).secondary[0][2] == 0 && ws.guidance == 0 && ws.guidance_previous == 0);
    CHECK(W(ES2_WEAPONS_GUIDE_SARH)->colour == 0x20 && W(ES2_WEAPONS_GUIDE_ARM)->colour == 0x22);
    CHECK(visible(ws.pointers[26]) && !visible(ws.pointers[12]));
    CHECK(!strcmp(line(0), D(0x63)) && !strcmp(line(2), D(0x65)));
    /* EO: the previous image hidden, mode 3 stored */
    CHECK(es2_weapons_screen_press(&ws, ES2_WEAPONS_EO_HANDLER) == 1 && view(0).secondary[0][2] == 3);
    CHECK(!visible(ws.pointers[26]) && visible(ws.pointers[29]) && !strcmp(line(1), D(0x6d)));
    CHECK(W(ES2_WEAPONS_GUIDE_EO)->colour == 0x20 && W(ES2_WEAPONS_GUIDE_SARH)->colour == 0x22);
    /* the same mode again: no store, no change callback */
    CHECK(es2_weapons_screen_press(&ws, ES2_WEAPONS_EO_HANDLER) == 1 && ws.stores == 2 && hl.changed == 2);
    CHECK(es2_weapons_screen_press(&ws, ES2_WEAPONS_ARH_HANDLER) == 1 && view(0).secondary[0][2] == 1);
    CHECK(es2_weapons_screen_press(&ws, ES2_WEAPONS_ARM_HANDLER) == 1 && view(0).secondary[0][2] == 2 && ws.stores == 4);
    /* the description button: 0x43fdac(row 12) without the flag; the guidance image hidden, 0x4403f9(2, 0) */
    CHECK(es2_weapons_screen_press(&ws, ES2_WEAPONS_DESCRIPTION_HANDLER) == 1 && !visible(ws.pointers[28]) &&
          visible(ws.pointers[12]) && ws.guidance == 2 && ws.guidance_previous == 2 && ws.mounts == 0);
    /* another row: the panel hidden, the guidance word kept, 0x476df8 := -1 */
    CHECK(es2_weapons_screen_press(&ws, row_handler(26)) == 1 && !ws.panel && ws.guidance == -1);
    CHECK(!visible(ws.ids[ES2_WEAPONS_GUIDE_EO]) && view(0).item[0][2] == 0 && count_of(13) == 1);
    /* the ID-0 row's pointer is list 2's mode 0: leaving row 26 hides that image */
    es2_ui_show(ui, ws.pointers[26]);
    CHECK(visible(ws.pointers[26]) && es2_weapons_screen_press(&ws, ES2_WEAPONS_PREV_HANDLER) == 1 && ws.row == 26);
    CHECK(!visible(ws.pointers[26]));
    /* a rack row without a slot: the panel, no guidance call; a button then stores nothing */
    es2_weapons_screen_bay_changed(&ws);
    CHECK(es2_weapons_screen_row(&ws, 12, 1) == 1 && ws.panel && ws.guidance_previous == 2);
    CHECK(es2_weapons_screen_press(&ws, ES2_WEAPONS_SARH_HANDLER) == 1 && ws.stores == 4 && ws.guidance == 0);
    /* a store the library refuses (a non-rack slot) is not made */
    CHECK(es2_weapons_screen_select_slot(&ws, 0) == 1);
    unsigned refused = ws.refused;
    es2_weapons_screen_guidance(&ws, 1, 1);
    CHECK(ws.refused == refused + 1 && view(0).secondary[0][0] == 5 && ws.guidance == 1);
}

static void quirks(void) {
    fresh(1, 1, 1);
    es2_weapons_screen_show(&ws, -1);
    steps(2);
    /* the previous row's restore tests 0x41501f only: with no slot an unavailable row comes back enabled */
    CHECK(es2_weapons_screen_row(&ws, 6, 1) == 1 && ws.row == 6);
    CHECK(es2_weapons_screen_row(&ws, 0, 1) == 1);
    CHECK(W(rowc(6))->enabled == 1 && column(6, 0)->text_colour == 0x27 && W(rowc(6))->colour == 0x10);
    /* refusals and unknown handlers */
    CHECK(es2_weapons_screen_row(&ws, 27, 1) == -1 && es2_weapons_screen_row(&ws, -1, 0) == -1);
    CHECK(es2_weapons_screen_press(&ws, 0x123456) == -1 && es2_weapons_screen_press(NULL, row_handler(0)) == -1);
    CHECK(es2_weapons_screen_row(NULL, 0, 0) == -1 && es2_weapons_screen_select_slot(NULL, 0) == -1);
    es2_weapons_screen_show(NULL, 0);
    es2_weapons_screen_hide(NULL);
    es2_weapons_screen_guidance(NULL, 0, 1);
    es2_weapons_screen_bay_changed(NULL);
    /* the description button before any row: refused */
    fresh(1, 1, 1);
    CHECK(es2_weapons_screen_press(&ws, ES2_WEAPONS_DESCRIPTION_HANDLER) == -1);
    /* nothing loaded: the show selects nothing to mount and the slot buttons refuse */
    fresh(1, 0, 1);
    es2_weapons_screen_show(&ws, -1);
    CHECK(hl.selects == 1 && hl.selected == -1 && ws.row == 0);
    CHECK(es2_weapons_screen_press(&ws, ES2_WEAPONS_NEXT_HANDLER) == -1);
}

int main(void) {
    if (mmf_font(font_bytes, &font)) {
        printf("FAIL font\n");
        return 1;
    }
    records();
    build();
    show();
    slots();
    mounts();
    guidance();
    quirks();
    if (ui) es2_ui_release(ui);
    free(ui);
    free(shell);
    es2_shell_engine_free(&fx.engine);
    printf("%u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
