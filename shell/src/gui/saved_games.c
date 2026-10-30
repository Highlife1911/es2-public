/* The Saved games screen 0x438c40 (es2/shell/gui/saved_games.h).
 *
 * Contract: docs/reverse-engineering/vshell_ui.md sections 4.3, 9.3, 13 and
 * 26. Original: VSHELL.EXE 0x4377bc..0x438c3f
 * and 0x43a19c..0x43a42e. */
#include "es2/shell/gui/saved_games.h"

#include <stdio.h>
#include <string.h>

#include "es2/shell/draw.h"

/* Colours the screen writes (label +0xb5, button +0x4d, row +0xbb). */
enum { ON_LABEL = 0x29, ON_BUTTON = 0x22, OFF = 0x26, ROW_NORMAL = 0x27, ROW_SELECTED = 0x29 };

/* ---- helpers ----------------------------------------------------------------- */

static es2_ui_id id_of(const es2_saved_games *sg, int control) { return sg->ids[control]; }
static es2_ui_id row_of(const es2_saved_games *sg, int slot) { return sg->ids[ES2_SAVED_GAMES_ROW_FIRST + slot]; }

static es2_saved_games *screen_of(es2_ui *ui, es2_ui_id window) {
    es2_ui_widget *w = es2_ui_get(ui, window);
    return w ? (es2_saved_games *)w->user : NULL;
}

static int flag_of(const es2_saved_games *sg, int slot) {
    return sg->table && slot >= 0 && slot < ES2_SHELL_SLOT_COUNT && sg->table->slot[slot].flag;
}

static const char *text_of(const es2_saved_games *sg, int32_t id) {
    const char *t = sg->host.text ? sg->host.text(sg->host.ctx, (uint32_t)id) : NULL;
    return t ? t : "";
}

/* A button's two looks (0x29 / 0x22, enabled; 0x26 / 0x26, disabled): label
 * colour, button colour, redraw of the tree (0x41f3ed), +0x49, draw (vtable +4). */
static void button(es2_saved_games *sg, int control, int on) {
    es2_ui *ui = sg->ui;
    es2_ui_id id = id_of(sg, control);
    es2_ui_widget *b = es2_ui_get(ui, id), *label;
    if (!b) return;
    label = es2_ui_get(ui, b->label);
    if (label) label->text_colour = on ? ON_LABEL : OFF;
    b->colour = on ? ON_BUTTON : OFF;
    es2_ui_redraw_tree(ui, id);
    b->enabled = on ? 1 : 0;
    es2_ui_draw(ui, id);
}

/* A row's colour +0xbb and its draw 0x40c6ff. */
static void row_colour(es2_saved_games *sg, int slot, int colour) {
    es2_ui_widget *w = es2_ui_get(sg->ui, row_of(sg, slot));
    if (!w) return;
    w->edit.colour = (uint8_t)colour;
    es2_ui_draw(sg->ui, row_of(sg, slot));
}

/* 0x4381cd(slot, name): the slot text (name NULL) or "%2d. %s" with slot + 1
 * and the name, into the row with setText 0x40c294 (no draw). */
static void row_text(es2_saved_games *sg, int slot, const char *name) {
    char text[100];
    if (name)
        snprintf(text, sizeof text, "%2d. %s", slot + 1, name);
    else
        snprintf(text, sizeof text, "%s", sg->table ? sg->table->slot[slot].text : "");
    (void)es2_ui_dialog_set(sg->ui, row_of(sg, slot), text, 0);
}

static void label_set(es2_saved_games *sg, int control, const char *text) {
    es2_ui_widget *w = es2_ui_get(sg->ui, id_of(sg, control));
    char clipped[ES2_UI_TEXT];
    if (!w) return;
    snprintf(clipped, sizeof clipped, "%s", text); /* setText refuses 100 bytes and more */
    (void)es2_ui_message_set(sg->ui, id_of(sg, control), clipped, w->align, w->text_colour, 1);
}

static void label_number(es2_saved_games *sg, int control, long value) {
    char text[24];
    snprintf(text, sizeof text, "%ld", value);
    label_set(sg, control, text);
}

static void finish(es2_saved_games *sg, uint32_t handler, es2_shell_flow_result r) {
    sg->last_handler = handler;
    sg->last_result = r;
    sg->events++;
    if (sg->host.changed) sg->host.changed(sg->host.ctx, handler, r);
}

static es2_shell_flow_result flow_event(es2_saved_games *sg, uint32_t handler, const char *text) {
    return sg->flow ? es2_shell_flow_event(sg->flow, (es2_shell_flow_handler)handler, text) : ES2_FLOW_IGNORED;
}

/* ---- resolver for the layout table
 * ---------------------------------------------- */

static const es2_dfn *resolve_font(void *context, uint32_t global) {
    es2_saved_games *sg = context;
    return sg->host.font ? sg->host.font(sg->host.ctx, global) : NULL;
}

static const es2_bitmap *resolve_bitmap(void *context, const es2_ui_layout_ref *ref) {
    es2_saved_games *sg = context;
    const char *file = NULL;
    if (!sg->host.bitmap) return NULL;
    if (ref->kind == ES2_UI_REF_DBM) file = es2_ui_layout_file(ref->a);
    else if (ref->kind == ES2_UI_REF_GLOBAL && ref->a == 0x46de0c) file = es2_menu_background_file(4); /* bay2a_84 */
    return file ? sg->host.bitmap(sg->host.ctx, file) : NULL;
}

/* Labels: ESTEXT IDs; the value labels' literals (0x4758aa.., empty strings)
 * stay empty until the panel writes them. */
static const char *resolve_text(void *context, const es2_ui_layout_ref *ref) {
    es2_saved_games *sg = context;
    if (ref->kind != ES2_UI_REF_ESTEXT || !sg->host.text) return NULL;
    return sg->host.text(sg->host.ctx, (uint32_t)ref->a);
}

static void on_control(es2_ui *ui, es2_ui_id window, const es2_gui_event *ev);

static es2_ui_handler resolve_handler(void *context, uint32_t address) {
    (void)context;
    for (int k = 0; k < ES2_FLOW_USER_SLOTS; k++)
        if (address == ES2_SAVED_GAMES_ROW_HANDLER(k)) return on_control;
    switch (address) {
    case ES2_FLOW_SLOT_SAVE: case ES2_FLOW_SLOT_RESTORE: case ES2_FLOW_SLOT_EXIT:
    case ES2_FLOW_SLOT_CANCEL: case ES2_FLOW_SLOT_ACCEPT:
        return on_control;
    default: return NULL;
    }
}

/* Original: VSHELL.EXE 0x438c40 (constructor; the table is the layout table's).
 * */
int es2_screen_saved_games_build(es2_ui *ui, void *ctx) {
    es2_saved_games *sg = ctx;
    es2_ui_resolver r;
    if (!ui || !sg) return -1;
    sg->ui = ui;
    r.context = sg;
    r.font = resolve_font;
    r.bitmap = resolve_bitmap;
    r.text = resolve_text;
    r.handler = resolve_handler;
    memset(sg->ids, 0, sizeof sg->ids);
    if (es2_ui_layout_build(ui, ES2_UI_SCREEN_SAVED_GAMES, &r, sg->ids, ES2_SAVED_GAMES_CONTROLS) < 0) return -1;
    for (int k = 0; k < ES2_SAVED_GAMES_CONTROLS; k++) {
        es2_ui_widget *w = es2_ui_get(ui, sg->ids[k]);
        if (!w) return -1;
        w->user = sg;
    }
    return 0;
}

/* ---- panel, select, show, hide ---------------------------------------------- */

/* Original: VSHELL.EXE 0x4377bc */
void es2_saved_games_panel(es2_saved_games *sg) {
    static const int kills[6] = {ES2_SAVED_GAMES_HERC, ES2_SAVED_GAMES_HERC + 1, ES2_SAVED_GAMES_FLYER,
                                 ES2_SAVED_GAMES_FLYER + 1, ES2_SAVED_GAMES_BASE, ES2_SAVED_GAMES_BASE + 1};
    static const int values[12] = {ES2_SAVED_GAMES_NAME, ES2_SAVED_GAMES_SKILL, ES2_SAVED_GAMES_RANK,
                                   ES2_SAVED_GAMES_HERC, ES2_SAVED_GAMES_HERC + 1, ES2_SAVED_GAMES_FLYER,
                                   ES2_SAVED_GAMES_FLYER + 1, ES2_SAVED_GAMES_BASE, ES2_SAVED_GAMES_BASE + 1,
                                   ES2_SAVED_GAMES_SALVAGE, ES2_SAVED_GAMES_SECTOR, ES2_SAVED_GAMES_MISSION};
    es2_shell_stats zero;
    es2_shell_stats_view v;
    char text[ES2_UI_TEXT];
    int slot = sg->flow ? sg->flow->slot : ES2_FLOW_SLOT_CAMPAIGN;
    int loaded = sg->flow && sg->flow->game != ES2_FLOW_GAME_NONE;
    if (!((slot >= 0 && slot < ES2_FLOW_USER_SLOTS && flag_of(sg, slot)) || (slot == ES2_FLOW_SLOT_CAMPAIGN && loaded))) {
        /* 0x437bd5..0x437e5d: ESTEXT 0 into each value */
        for (int i = 0; i < 12; i++) label_set(sg, values[i], text_of(sg, 0));
        return;
    }
    memset(&zero, 0, sizeof zero);
    es2_shell_stats_show(sg->stats ? &sg->stats[slot] : &zero, &v);
    label_set(sg, ES2_SAVED_GAMES_NAME, v.name);                   /* 0x437831 */
    label_set(sg, ES2_SAVED_GAMES_SKILL, text_of(sg, v.skill_text)); /* "%s" 0x4752e2 */
    label_set(sg, ES2_SAVED_GAMES_RANK, text_of(sg, v.rank_text));   /* "%s" 0x4752e5 */
    for (int i = 0; i < 6; i++) label_number(sg, kills[i], v.kills[i]); /* "%d" 0x4752e8.. */
    snprintf(text, sizeof text, "%ld %s", (long)v.salvage, text_of(sg, v.salvage_text)); /* "%d %s" 0x4752fa */
    label_set(sg, ES2_SAVED_GAMES_SALVAGE, text);
    label_set(sg, ES2_SAVED_GAMES_SECTOR, text_of(sg, v.sector_text)); /* "%s" 0x475300 */
    label_number(sg, ES2_SAVED_GAMES_MISSION, v.mission);              /* "%d" 0x475303 */
}

/* 0x437fef's effects from slot `old` to `slot` (its two tests passed). */
static void select_effects(es2_saved_games *sg, int old, int slot) {
    es2_shell_flow *f = sg->flow;
    if (old >= 0 && old < ES2_FLOW_USER_SLOTS) row_colour(sg, old, ROW_NORMAL);       /* 0x438038 */
    if (slot >= 0 && slot < ES2_FLOW_USER_SLOTS) row_colour(sg, slot, ROW_SELECTED);  /* 0x438059 */
    es2_shell_flow_select_slot(f, slot); /* VSHELL stores it last (0x4381be); the rules read the new row */
    button(sg, ES2_SAVED_GAMES_SAVE, es2_shell_flow_enabled(f, ES2_FLOW_SLOT_SAVE, 0));                 /* 0x43807b */
    button(sg, ES2_SAVED_GAMES_RESTORE, es2_shell_flow_enabled(f, ES2_FLOW_SLOT_RESTORE, flag_of(sg, slot))); /* 0x438126 */
    es2_saved_games_panel(sg);                                                        /* 0x4381c5 */
}

/* Original: VSHELL.EXE 0x437fef */
void es2_saved_games_select(es2_saved_games *sg, int slot) {
    if (!sg || !sg->flow) return;
    if (sg->flow->naming) return;           /* 0x438015: naming 2 */
    if (slot == sg->flow->slot) return;     /* 0x438009 */
    select_effects(sg, sg->flow->slot, slot);
}

/* 0x43a3f6 after the flow already holds slot 10: select(10) from `old`. */
static void hide_from(es2_saved_games *sg, int old) {
    es2_ui *ui = sg->ui;
    if (old != ES2_FLOW_SLOT_CAMPAIGN && sg->flow && !sg->flow->naming) select_effects(sg, old, ES2_FLOW_SLOT_CAMPAIGN);
    es2_ui_hide(ui, id_of(sg, ES2_SAVED_GAMES_PANEL));
    es2_ui_hide(ui, id_of(sg, ES2_SAVED_GAMES_BACKGROUND));
    es2_ui_hide(ui, id_of(sg, ES2_SAVED_GAMES_STATS));
    es2_ui_hide(ui, id_of(sg, ES2_SAVED_GAMES_SPARE));
}

/* Original: VSHELL.EXE 0x43a3f6 */
void es2_saved_games_hide(es2_saved_games *sg) {
    if (!sg || !sg->ui) return;
    hide_from(sg, sg->flow ? sg->flow->slot : ES2_FLOW_SLOT_CAMPAIGN);
}

/* Original: VSHELL.EXE 0x43a19c */
void es2_saved_games_show(es2_saved_games *sg) {
    es2_ui *ui;
    int slot, loaded;
    if (!sg || !sg->ui) return;
    ui = sg->ui;
    slot = sg->flow ? sg->flow->slot : ES2_FLOW_SLOT_CAMPAIGN;
    loaded = sg->flow && sg->flow->game != ES2_FLOW_GAME_NONE;
    for (int k = 0; k < ES2_FLOW_USER_SLOTS; k++) row_text(sg, k, NULL);
    es2_ui_show(ui, id_of(sg, ES2_SAVED_GAMES_BACKGROUND));
    es2_ui_show(ui, id_of(sg, ES2_SAVED_GAMES_PANEL));
    es2_ui_show(ui, id_of(sg, ES2_SAVED_GAMES_STATS));
    es2_ui_hide(ui, id_of(sg, ES2_SAVED_GAMES_SPARE));
    es2_ui_set_enabled(ui, id_of(sg, ES2_SAVED_GAMES_ROWS), 0); /* 0x43a1e8 */
    button(sg, ES2_SAVED_GAMES_ACCEPT, 0);
    button(sg, ES2_SAVED_GAMES_CANCEL, 0);
    button(sg, ES2_SAVED_GAMES_SAVE, slot < ES2_FLOW_USER_SLOTS && loaded);       /* 0x43a273..0x43a288 */
    if (slot < ES2_FLOW_USER_SLOTS) row_colour(sg, slot, ROW_SELECTED);          /* 0x43a32f */
    button(sg, ES2_SAVED_GAMES_RESTORE, slot < ES2_FLOW_USER_SLOTS && flag_of(sg, slot)); /* 0x43a357 */
    es2_saved_games_panel(sg);
}

/* ---- the controls ------------------------------------------------------------- */

/* 0x437e62 (naming 2): the selected row becomes the edit field. */
static void start_naming(es2_saved_games *sg) {
    es2_ui *ui = sg->ui;
    int slot = sg->flow->slot;
    es2_ui_id row = row_of(sg, slot);
    es2_ui_widget *w = es2_ui_get(ui, row);
    es2_gui_event down;
    if (!w) return;
    /* 0x437e8d..0x437ee4: a left-button-down event posted to the row */
    memset(&down, 0, sizeof down);
    down.type = ES2_GUI_EV_MOUSE;
    down.code = ES2_GUI_MOUSE_LEFT_DOWN;
    down.target = row;
    if (ui->shell) (void)es2_gui_post(&ui->shell->input, down);
    /* 0x469ffc(queue, row): leave 0x46a0b4 and enter 0x46a15c, then the
     * capture flag [queue + 0x1f] := 1 */
    if (!ui->capture) {
        es2_gui_event e;
        memset(&e, 0, sizeof e);
        e.x = ui->cursor_x;
        e.y = ui->cursor_y;
        if (ui->hover) {
            e.type = ES2_GUI_EV_LEAVE;
            e.target = ui->hover;
            es2_ui_on_event(ui, &e);
            ui->hover = 0;
            ui->hover_display = 0;
        }
        ui->hover = row;
        ui->hover_display = 1;
        e.type = ES2_GUI_EV_ENTER;
        e.target = row;
        es2_ui_on_event(ui, &e);
    }
    ui->capture = 1;
    w = es2_ui_get(ui, row);
    w->editable = 1;   /* +0xbf */
    w->edit.blink = 1; /* +0xb3 */
    row_text(sg, slot, ""); /* "%2d. " (0x475306 is "") */
    button(sg, ES2_SAVED_GAMES_CANCEL, 1);
    button(sg, ES2_SAVED_GAMES_ACCEPT, 1);
    es2_ui_redraw_tree(ui, id_of(sg, ES2_SAVED_GAMES_ROWS)); /* 0x437fdb */
}

/* ACCEPT and CANCEL end: CANCEL/ACCEPT off, SAVE/RESTORE/EXIT on. */
static void end_naming_buttons(es2_saved_games *sg, int redraw_rows) {
    button(sg, ES2_SAVED_GAMES_CANCEL, 0);
    button(sg, ES2_SAVED_GAMES_ACCEPT, 0);
    if (redraw_rows) es2_ui_redraw_tree(sg->ui, id_of(sg, ES2_SAVED_GAMES_ROWS)); /* CANCEL 0x43855a */
    button(sg, ES2_SAVED_GAMES_SAVE, 1);
    button(sg, ES2_SAVED_GAMES_RESTORE, 1);
    button(sg, ES2_SAVED_GAMES_EXIT, 1);
}

es2_shell_flow_result es2_saved_games_press(es2_saved_games *sg, uint32_t handler) {
    es2_shell_flow *f;
    es2_shell_flow_result r;
    int old;
    if (!sg || !sg->ui || !sg->flow) return ES2_FLOW_IGNORED;
    f = sg->flow;
    for (int k = 0; k < ES2_FLOW_USER_SLOTS; k++)
        if (handler == ES2_SAVED_GAMES_ROW_HANDLER(k)) { /* 0x438873 + 0x61 k */
            es2_saved_games_select(sg, k);
            return ES2_FLOW_DONE;
        }
    old = f->slot;
    switch (handler) {
    case ES2_FLOW_SLOT_SAVE: /* 0x438263 */
        r = flow_event(sg, handler, NULL);
        if (r == ES2_FLOW_DONE) {
            button(sg, ES2_SAVED_GAMES_SAVE, 0);
            button(sg, ES2_SAVED_GAMES_RESTORE, 0);
            button(sg, ES2_SAVED_GAMES_EXIT, 0);
            start_naming(sg);
        }
        break;
    case ES2_FLOW_SLOT_ACCEPT: { /* 0x43868a */
        es2_ui_widget *row = es2_ui_get(sg->ui, row_of(sg, old >= 0 && old < ES2_FLOW_USER_SLOTS ? old : 0));
        const char *text = row && old >= 0 && old < ES2_FLOW_USER_SLOTS ? row->edit.text : ""; /* 0x438239 */
        if (!es2_shell_flow_enabled(f, ES2_FLOW_SLOT_ACCEPT, 0)) return ES2_FLOW_IGNORED;
        if (strlen(text) >= ES2_SHELL_SLOT_TEXT) return ES2_FLOW_REFUSED; /* own: the 80-byte refusal of es2_shell_slot_row_typed */
        r = flow_event(sg, handler, text);
        if (sg->host.refresh) sg->host.refresh(sg->host.ctx, old); /* 0x430ec4 */
        es2_saved_games_panel(sg);                                  /* 0x4386c6 */
        end_naming_buttons(sg, 0);
        break;
    }
    case ES2_FLOW_SLOT_CANCEL: /* 0x4384aa */
        r = flow_event(sg, handler, NULL);
        if (r != ES2_FLOW_DONE) break;
        if (old >= 0 && old < ES2_FLOW_USER_SLOTS) row_text(sg, old, NULL);
        end_naming_buttons(sg, 1);
        if (old != ES2_FLOW_SLOT_CAMPAIGN) select_effects(sg, old, ES2_FLOW_SLOT_CAMPAIGN); /* 0x43863b */
        break;
    case ES2_FLOW_SLOT_RESTORE: /* 0x438393: load, save 10, hide, 0x477946 := 0, bay */
        r = flow_event(sg, handler, NULL);
        if (f->screen == ES2_FLOW_SAVED_GAMES) break; /* refused or ignored: the screen stays */
        hide_from(sg, old);
        if (sg->movies) sg->movies->group_shown = 0; /* 0x4383c6 */
        if (f->screen == ES2_FLOW_MAIN_MENU && sg->main_menu) es2_main_menu_show(sg->main_menu); /* own failure path */
        break;
    case ES2_FLOW_SLOT_EXIT: /* 0x438424: hide, then the caller's screen */
        if (!es2_shell_flow_enabled(f, ES2_FLOW_SLOT_EXIT, 0)) return ES2_FLOW_IGNORED;
        es2_saved_games_hide(sg);
        r = flow_event(sg, handler, NULL);
        if (f->screen == ES2_FLOW_MAIN_MENU && sg->main_menu) es2_main_menu_show(sg->main_menu); /* 0x43844c */
        break;
    default:
        return ES2_FLOW_IGNORED;
    }
    finish(sg, handler, r);
    return r;
}

static void on_control(es2_ui *ui, es2_ui_id window, const es2_gui_event *ev) {
    es2_saved_games *sg = screen_of(ui, window);
    es2_ui_widget *w = es2_ui_get(ui, window);
    (void)ev;
    if (sg && w) (void)es2_saved_games_press(sg, w->original_handler);
}
