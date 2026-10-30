/* The service bay 0x43afd4 and the squad panel 0x43d029
 * (es2/shell/gui/service_bay.h).
 *
 * Contract: docs/reverse-engineering/vshell_ui.md sections 4.6 and 28.
 * Original: VSHELL.EXE 0x43a430..0x43b8ed (bar handlers, bay
 * show and hide) and 0x43cfa5..0x43e7ee (squad panel). */
#include "es2/shell/gui/service_bay.h"

#include <stdio.h>
#include <string.h>

#include "es2/shell/draw.h"

/* ---- helpers ------------------------------------------------------------------ */

static es2_service_bay *bay_of(es2_ui *ui, es2_ui_id window) {
    es2_ui_widget *w = es2_ui_get(ui, window);
    return w ? (es2_service_bay *)w->user : NULL;
}

static const char *text(const es2_service_bay *b, uint32_t id) {
    const char *t = b->host.text ? b->host.text(b->host.ctx, id) : NULL;
    return t ? t : "";
}

static void sound(es2_service_bay *b) {
    if (b->host.sound) b->host.sound(b->host.ctx, ES2_SERVICE_BAY_SOUND);
}

static void screen_hook(es2_service_bay *b, int word, int show) {
    if (b->host.screen) b->host.screen(b->host.ctx, word, show);
}

static int bay_data(const es2_service_bay *b, int k, es2_squad_bay *d) {
    memset(d, 0, sizeof *d);
    if (k < 0 || k > 7 || !b->host.bay || !b->host.bay(b->host.ctx, k, d)) {
        memset(d, 0, sizeof *d);
        return 0;
    }
    return 1;
}

static es2_shell_flow_result flow_event(es2_service_bay *b, uint32_t handler) {
    es2_shell_flow_result r = es2_shell_flow_event(b->flow, (es2_shell_flow_handler)handler, NULL);
    b->last_handler = handler;
    b->last_result = r;
    b->events++;
    if (b->host.changed) b->host.changed(b->host.ctx, handler, r);
    return r;
}

static es2_ui_id grid(const es2_service_bay *b, int16_t k) {
    /* [0x48d59c + 4k]; k = -1 reads [0x48d598], Registration's ACCEPT,
     * which is not this screen's: nothing (section 28) */
    return k >= 0 && k <= 8 ? b->squad[ES2_SQUAD_GRID + k] : 0;
}

static es2_ui_id row(const es2_service_bay *b, int16_t k) {
    /* [0x48d5dc + 4k]; k = -1 reads [0x48d5d8], the Squad Inventory title */
    if (k == -1) return b->squad[ES2_SQUAD_INVENTORY];
    return k >= 0 && k <= 7 ? b->squad[ES2_SQUAD_ROW + 5 * k] : 0;
}

/* +0x4d := colour, then 0x41f3ed (the redraw event of the tree). */
static void row_colour(es2_service_bay *b, int16_t k, int32_t colour) {
    es2_ui_widget *w = es2_ui_get(b->ui, row(b, k));
    if (!w) return;
    w->colour = colour;
    es2_ui_redraw_tree(b->ui, row(b, k));
}

static void set_label(es2_service_bay *b, es2_ui_id label, const char *t, int32_t align, int32_t colour) {
    (void)es2_ui_message_set(b->ui, label, t, align, colour, 1); /* 0x40b8bb(label, text, align, colour, 1) */
}

/* ---- the bay -------------------------------------------------------------------- */

/* Original: VSHELL.EXE 0x43a430(p): show the ESPal (its draw fills and
 * presents at once), its word +0x45 := p, hide it (palette p selected and
 * applied). */
static void palette_window(es2_service_bay *b, uint16_t p) {
    es2_ui_id pal = b->bay[ES2_BAY_PALETTE];
    es2_ui_widget *w = es2_ui_get(b->ui, pal);
    if (!w) return;
    es2_ui_show(b->ui, pal);
    w->palette = p;
    es2_ui_hide(b->ui, pal);
}

/* Original: VSHELL.EXE 0x43b7f2(word): 8 shows the frame; 2..7 the palette
 * of es2_palette_for_bay_screen (jump table 0x43b80a). */
static void bay_screen(es2_service_bay *b, int word) {
    int16_t group = 0, position = 0;
    int shown = 0, p;
    if (word == 8) {
        es2_ui_show(b->ui, b->bay[ES2_BAY_FRAME]);
        return;
    }
    if (word == 7 && b->host.career) b->host.career(b->host.ctx, &group, &position, &shown);
    p = es2_palette_for_bay_screen((int16_t)word, (int16_t)b->flow->page, group);
    if (p >= 0) palette_window(b, (uint16_t)p);
}

/* Original: VSHELL.EXE 0x43a45b: +0x45 := 0 and the draw for the nine
 * buttons [0x48d52c] .. [0x48d54c]. */
static void release_all(es2_service_bay *b) {
    for (int k = ES2_BAY_ONLINE; k <= ES2_BAY_MISSION; k += 2) {
        es2_ui_widget *w = es2_ui_get(b->ui, b->bay[k]);
        if (!w) continue;
        w->pressed = 0;
        es2_ui_draw(b->ui, b->bay[k]);
    }
}

static void press_button(es2_service_bay *b, int control) {
    es2_ui_widget *w = es2_ui_get(b->ui, b->bay[control]);
    if (!w) return;
    w->pressed = 1;
    es2_ui_draw(b->ui, b->bay[control]);
}

/* Original: VSHELL.EXE 0x43a537 (jump table 0x43a54a), its UI part: 2
 * 0x43d779(2) and 0x43fc62; 3 0x43d779(3) and 0x433a7b; 4 0x43d779(4) and
 * 0x446fde; 5 0x449c46; 6 0x43d779(2) (sic) and 0x44218a; 7 0x445095; -1, 0,
 * 1 nothing. The rebalance 0x412a37 after 2, 3, 4 is the flow's. */
static void leave_ui(es2_service_bay *b) {
    int word = b->session.screen;
    switch (word) {
    case 2: case 3: case 4: case 6:
        es2_squad_panel_hide(b, word == 6 ? 2 : word);
        /* fall through */
    case 5: case 7:
        screen_hook(b, word, 0);
        break;
    default:
        break;
    }
}

void es2_service_bay_show(es2_service_bay *b) {
    es2_ui *ui;
    static const int enable[5] = {ES2_BAY_WEAPONS, ES2_BAY_REPAIR, ES2_BAY_BUILD, ES2_BAY_ARMORY, ES2_BAY_CREW};
    if (!b || !b->ui || !b->flow) return;
    ui = b->ui;
    bay_screen(b, 8);                        /* 0x43b7f2(8) */
    es2_ui_show(ui, b->bay[ES2_BAY_BAR]);    /* 0x43b758 */
    for (int i = 0; i < 5; i++) {
        es2_ui_widget *w = es2_ui_get(ui, b->bay[enable[i]]);
        /* 0x43b764: mode 0 -> WEAPONS and CREW 1, REPAIR, BUILD, ARMORY 0;
         * else all five 1; MAIN MENU, SAVE, MISSION and the image keep +0x49 */
        if (w) w->enabled = b->flow->mode != 0 || enable[i] == ES2_BAY_WEAPONS || enable[i] == ES2_BAY_CREW;
    }
    b->session.screen = ES2_SHELL_SCREEN_BAY; /* 0x43b7e8 */
    if (b->host.changed) b->host.changed(b->host.ctx, ES2_SERVICE_BAY_SHOW, ES2_FLOW_DONE);
}

void es2_service_bay_hide(es2_service_bay *b) {
    if (!b || !b->ui) return;
    es2_ui_hide(b->ui, b->bay[ES2_BAY_BAR]);
    es2_ui_hide(b->ui, b->bay[ES2_BAY_FRAME]);
    if (b->ui->palette) b->ui->palette(b->ui, ES2_PALETTE_MAIN); /* 0x407b62(1) */
}

void es2_service_bay_game_loaded(es2_service_bay *b) {
    if (b) b->session.bay = 0;
}

/* MAIN MENU 0x43a66c and SAVE 0x43a768: release, leave, palette 1 and the
 * bay hidden unless the word was -1, then the next screen; the word; bptlt2. */
static es2_shell_flow_result leave_bay(es2_service_bay *b, uint32_t handler, int word) {
    es2_shell_flow_result r;
    int was = b->session.screen;
    release_all(b);
    leave_ui(b);
    r = flow_event(b, handler);              /* leave 0x412a37, word, caller 8 / save 0x40e933(10, 0) */
    if (was != ES2_SHELL_SCREEN_BAY) palette_window(b, ES2_PALETTE_MAIN); /* 0x43a430(1) */
    es2_service_bay_hide(b);                 /* 0x43b8cd */
    screen_hook(b, word, 1);                 /* 0x431718 / 0x43a19c */
    b->session.screen = (int16_t)word;
    sound(b);
    return r;
}

/* WEAPONS 0x43a862, REPAIR 0x43a962, BUILD 0x43aa62, ARMORY 0x43ab62, CREW
 * 0x43ac5a: release, press, leave, palette, squad panel (not ARMORY), the
 * show and the word (CREW: the word before its show), bptlt2. */
static es2_shell_flow_result editor(es2_service_bay *b, uint32_t handler, int word, int control) {
    es2_shell_flow_result r;
    release_all(b);
    press_button(b, control);
    leave_ui(b);
    r = flow_event(b, handler);
    bay_screen(b, word);                     /* 0x43b7f2(word) */
    if (word != 5) es2_squad_panel_show(b, word); /* 0x43d677(word) */
    if (word == 6) b->session.screen = 6;    /* 0x43ad00 before 0x442091 */
    screen_hook(b, word, 1);
    b->session.screen = (int16_t)word;
    sound(b);
    return r;
}

/* MISSION 0x43ad5a: release, press, 0x43aee7(page, 0) with page 0 when the
 * group's movies were not shown (0x477946) and the position 0x46fc5a is 0,
 * else 1: page word 0x481108 := page, leave, 0x43b7f2(7), 0x444873(page),
 * word 7; then bptlt2. The click argument 0 posts nothing. */
static es2_shell_flow_result mission(es2_service_bay *b) {
    es2_shell_flow_result r;
    int16_t group = 0, position = 0;
    int shown = 0;
    release_all(b);
    press_button(b, ES2_BAY_MISSION);
    if (b->host.career) b->host.career(b->host.ctx, &group, &position, &shown);
    b->flow->page = !shown && position == 0 ? 0 : 1;
    leave_ui(b);
    r = flow_event(b, ES2_FLOW_BAY_MISSION);
    bay_screen(b, 7);
    screen_hook(b, 7, 1);
    b->session.screen = 7;
    sound(b);
    return r;
}

es2_shell_flow_result es2_service_bay_press(es2_service_bay *b, uint32_t handler) {
    int word;
    if (!b || !b->ui || !b->flow) return ES2_FLOW_IGNORED;
    /* every bar handler: nothing before the main loop's step 5 (0x46c280) */
    if (!b->loop_started) return ES2_FLOW_IGNORED;
    word = b->session.screen;
    switch (handler) {
    case ES2_FLOW_BAY_ONLINE: /* 0x43a5bd -> 0x431e79: full screen left (0x4077df), then the flow */
        if (b->ui->shell && b->ui->shell->fullscreen) {
            es2_gui_shell *sh = b->ui->shell;
            sh->fullscreen = 0;
            if (sh->platform.fullscreen) sh->platform.fullscreen(sh->platform.context, 0);
        }
        return flow_event(b, handler);
    case ES2_FLOW_BAY_MAIN_MENU: return word == 0 ? ES2_FLOW_IGNORED : leave_bay(b, handler, 0);
    case ES2_FLOW_BAY_SAVE: return word == 1 ? ES2_FLOW_IGNORED : leave_bay(b, handler, 1);
    case ES2_FLOW_BAY_WEAPONS: return word == 2 ? ES2_FLOW_IGNORED : editor(b, handler, 2, ES2_BAY_WEAPONS);
    case ES2_FLOW_BAY_REPAIR: return word == 3 ? ES2_FLOW_IGNORED : editor(b, handler, 3, ES2_BAY_REPAIR);
    case ES2_FLOW_BAY_BUILD: return word == 4 ? ES2_FLOW_IGNORED : editor(b, handler, 4, ES2_BAY_BUILD);
    case ES2_FLOW_BAY_ARMORY: return word == 5 ? ES2_FLOW_IGNORED : editor(b, handler, 5, ES2_BAY_ARMORY);
    case ES2_FLOW_BAY_CREW: return word == 6 ? ES2_FLOW_IGNORED : editor(b, handler, 6, ES2_BAY_CREW);
    case ES2_FLOW_BAY_MISSION: return word == 7 ? ES2_FLOW_IGNORED : mission(b);
    default: return ES2_FLOW_IGNORED;
    }
}

static void on_bar(es2_ui *ui, es2_ui_id window, const es2_gui_event *ev) {
    es2_service_bay *b = bay_of(ui, window);
    es2_ui_widget *w = es2_ui_get(ui, window);
    (void)ev;
    if (b && w) (void)es2_service_bay_press(b, w->original_handler);
}

/* ---- the squad panel --------------------------------------------------------------- */

int es2_squad_band(const es2_service_bay *b, int16_t condition) {
    int i;
    for (i = 0; i < 5; i++) /* 0x43e087..0x43e099: signed compare, first limit below */
        if (condition > b->band_limit[i]) break;
    return i;
}

/* 0x43e09f: the band's colour (index 5 is the word after the five). */
static int32_t band_colour(const es2_service_bay *b, int16_t condition) {
    return b->band_colour[es2_squad_band(b, condition)];
}

/* sprintf(buf, "%d%s", progress, ESTEXT 0x6c) (0x476a3f, 0x476a45) */
static void progress_text(const es2_service_bay *b, const es2_squad_bay *d, char *buf, size_t size) {
    snprintf(buf, size, "%d%s", (int)d->progress, text(b, 0x6c));
}

void es2_squad_panel_labels(es2_service_bay *b, int16_t k) {
    es2_squad_bay d;
    char name[ES2_UI_TEXT + 1];
    int16_t skill;
    char buf[64];
    if (!b || !b->ui) return;
    (void)bay_data(b, k, &d); /* 0x410844(team, -1) is NULL; a bay without data reads as empty */
    if (k != -1 && d.pilot) {
        skill = d.skill;
        snprintf(name, sizeof name, "%s", d.pilot);
    } else {
        skill = -1;
        snprintf(name, sizeof name, "%s", k == -1 || !d.record ? text(b, 0) : text(b, 0x6d));
    }
    if (k == -1) { /* 0x43dc21 */
        set_label(b, b->squad[ES2_SQUAD_PILOT], text(b, 0), 0, 0x17);
        set_label(b, b->squad[ES2_SQUAD_CONDITION], text(b, 0), 1, 0x17);
    } else {
        int16_t c = d.record ? d.condition : 100;
        int band = es2_squad_band(b, c);
        int32_t colour = band_colour(b, c);
        if (!d.record) {
            set_label(b, b->squad[ES2_SQUAD_CONDITION], text(b, 0), 1, colour);
        } else if (!d.finished) {
            progress_text(b, &d, buf, sizeof buf);
            set_label(b, b->squad[ES2_SQUAD_CONDITION], buf, 1, 0x20);
        } else {
            set_label(b, b->squad[ES2_SQUAD_CONDITION], text(b, (uint32_t)(band + 0x68)), 1, colour);
        }
    }
    set_label(b, b->squad[ES2_SQUAD_PILOT], name, 0, 0x17); /* 0x43dc6b */
    set_label(b, b->squad[ES2_SQUAD_SKILL], skill >= 0 ? text(b, (uint32_t)(skill + 0x35)) : text(b, 0), 0, 0x17);
}

void es2_squad_panel_hercs(es2_service_bay *b) {
    if (!b || !b->ui) return;
    for (int16_t k = 0; k < 8; k++) {
        es2_squad_bay d;
        es2_ui_widget *q = es2_ui_get(b->ui, row(b, k));
        (void)bay_data(b, k, &d);
        if (!q) continue;
        /* column +0x59: the record's name or "" (0x476a44), the band colour */
        set_label(b, q->columns[1], d.record && d.herc ? d.herc : "", 0, band_colour(b, d.record ? d.condition : 100));
    }
}

void es2_squad_panel_pilots(es2_service_bay *b) {
    char buf[64];
    if (!b || !b->ui) return;
    for (int16_t k = 0; k < 8; k++) {
        es2_squad_bay d;
        es2_ui_widget *q = es2_ui_get(b->ui, row(b, k));
        (void)bay_data(b, k, &d);
        if (!q) continue;
        /* column +0x61, colour 0x27: the construction progress, else
         * 0x410890: the pilot's name or ESTEXT 0 */
        if (d.record && !d.finished) {
            progress_text(b, &d, buf, sizeof buf);
            set_label(b, q->columns[3], buf, 0, 0x27);
        } else {
            set_label(b, q->columns[3], d.pilot ? d.pilot : text(b, 0), 0, 0x27);
        }
    }
}

static void show_labels(es2_service_bay *b, int show) {
    for (int k = ES2_SQUAD_PILOT_CAPTION; k <= ES2_SQUAD_INVENTORY; k++) {
        if (show) es2_ui_show(b->ui, b->squad[k]);
        else es2_ui_hide(b->ui, b->squad[k]);
    }
}

void es2_squad_panel_show(es2_service_bay *b, int word) {
    int16_t sel;
    if (!b || !b->ui) return;
    if (b->host.panel) b->host.panel(b->host.ctx, word, 1); /* 0x43cfa5 */
    sel = b->session.bay;
    es2_squad_panel_labels(b, sel);                          /* 0x43da0c */
    es2_squad_panel_hercs(b);                                /* 0x43e0d7 */
    es2_squad_panel_pilots(b);                               /* 0x43e167 */
    if (sel == -1) {
        es2_ui_hide(b->ui, b->squad[ES2_SQUAD_GRID_NONE]);
        es2_ui_show(b->ui, b->squad[ES2_SQUAD_GRID_NONE]);
    } else {
        es2_ui_show(b->ui, grid(b, sel));
    }
    show_labels(b, 1);
    if (sel == -1) return;
    for (int16_t k = 0; k < 8; k++) row_colour(b, k, k == sel ? 0x29 : 0x10);
}

void es2_squad_panel_hide(es2_service_bay *b, int word) {
    int16_t sel;
    if (!b || !b->ui) return;
    if (b->host.panel) b->host.panel(b->host.ctx, word, 0); /* 0x43cfea */
    sel = b->session.bay;
    es2_ui_hide(b->ui, grid(b, sel));
    if (word == 3 && b->host.repair_grid) es2_ui_hide(b->ui, b->host.repair_grid(b->host.ctx, sel));
    show_labels(b, 0);
}

static int finished_or_none(const es2_service_bay *b, int16_t k) {
    es2_squad_bay d;
    if (k == -1) return 1;
    return bay_data(b, k, &d) && d.record && d.finished;
}

void es2_squad_panel_select(es2_service_bay *b, int16_t k, int assign) {
    int16_t sel;
    es2_squad_bay d;
    if (!b || !b->ui || k < -1 || k > 7) return;
    sel = b->session.bay;
    if (k == sel) return;
    switch (b->session.screen) {
    case 3: /* REPAIR 0x43dd1c */
        if (!finished_or_none(b, k)) break;
        if (b->session.tab == 0) es2_ui_hide(b->ui, grid(b, sel));
        else if (b->host.repair_grid) es2_ui_hide(b->ui, b->host.repair_grid(b->host.ctx, sel));
        es2_ui_show(b->ui, grid(b, k));
        row_colour(b, sel, 0x10);
        row_colour(b, k, 0x29);
        b->session.bay = k;
        if (b->host.selected) b->host.selected(b->host.ctx, 3, k, assign);
        break;
    case 6: /* CREW 0x43ddf7 */
        if (!finished_or_none(b, k)) break;
        if (k != -1 && b->session.order != 0 && bay_data(b, k, &d) && d.vehicle == 8) break;
        if (sel != -1) {
            es2_ui_hide(b->ui, grid(b, sel));
            row_colour(b, sel, 0x10);
        } else {
            es2_ui_hide(b->ui, b->squad[ES2_SQUAD_GRID_NONE]);
        }
        if (k != -1) {
            es2_ui_show(b->ui, grid(b, k));
            row_colour(b, k, 0x29);
        } else {
            es2_ui_show(b->ui, b->squad[ES2_SQUAD_GRID_NONE]);
        }
        b->session.bay = k;
        if (assign) {
            if (b->host.selected) b->host.selected(b->host.ctx, 6, k, 1); /* 0x4426e5 */
            es2_squad_panel_pilots(b);                                   /* 0x43e167 */
        }
        es2_squad_panel_labels(b, k);
        break;
    case 4: /* BUILD 0x43df0b */
        es2_ui_hide(b->ui, grid(b, sel));
        es2_ui_show(b->ui, grid(b, k));
        row_colour(b, sel, 0x10);
        row_colour(b, k, 0x29);
        b->session.bay = k;
        es2_squad_panel_labels(b, k);
        if (k != -1 && b->host.selected) b->host.selected(b->host.ctx, 4, k, assign); /* 0x447064 */
        break;
    case 2: /* WEAPONS 0x43df86, then the common case */
        if (!finished_or_none(b, k)) break;
        if (b->host.selected) b->host.selected(b->host.ctx, 2, k, assign);
        /* fall through */
    default: /* 0x43dfe8 */
        es2_ui_hide(b->ui, grid(b, sel));
        es2_ui_show(b->ui, grid(b, k));
        row_colour(b, sel, 0x10);
        row_colour(b, k, 0x29);
        b->session.bay = k;
        es2_squad_panel_labels(b, k);
        break;
    }
    es2_ui_redraw_tree(b->ui, b->squad[ES2_SQUAD_INVENTORY]); /* 0x43e04f */
}

/* The eight row handlers 0x43e477 + 0x6f k: flag 0x47665a := 1, 0x43dcdd(k),
 * flag := 0. The title's handler 0x43e41e does nothing. */
static void on_row(es2_ui *ui, es2_ui_id window, const es2_gui_event *ev) {
    es2_service_bay *b = bay_of(ui, window);
    (void)ev;
    if (!b) return;
    for (int16_t k = 0; k < 8; k++)
        if (row(b, k) == window) es2_squad_panel_select(b, k, 1);
}

/* ---- resolvers and builds ----------------------------------------------------------- */

static const es2_dfn *resolve_font(void *context, uint32_t global) {
    es2_service_bay *b = context;
    return b->host.font ? b->host.font(b->host.ctx, global) : NULL;
}

static const es2_bitmap *resolve_bitmap(void *context, const es2_ui_layout_ref *ref) {
    es2_service_bay *b = context;
    if (!b->host.bitmap) return NULL;
    if (ref->kind == ES2_UI_REF_DBA) return b->host.bitmap(b->host.ctx, es2_ui_layout_file(ref->a), ref->b);
    if (ref->kind == ES2_UI_REF_GLOBAL && ref->a == 0x46de0c) return b->host.bitmap(b->host.ctx, "dbm\\bay2a_84.dbm", -1);
    return NULL;
}

static const char *resolve_text(void *context, const es2_ui_layout_ref *ref) {
    es2_service_bay *b = context;
    static char numbers[8][4];
    switch (ref->kind) {
    case ES2_UI_REF_ESTEXT: return text(b, (uint32_t)ref->a);
    case ES2_UI_REF_EXE_STRING:
        /* 0x475e69 "?" (the online image), 0x476654 "-"; 0x476653 and
         * 0x476656 are empty */
        return ref->a == 0x475e69 ? "?" : ref->a == 0x476654 ? "-" : "";
    case ES2_UI_REF_SPRINTF: /* 0x47664f "%d.": the row numbers 1 .. 8 */
        if (ref->b < 1 || ref->b > 8) return "";
        snprintf(numbers[ref->b - 1], sizeof numbers[0], "%d.", (int)ref->b);
        return numbers[ref->b - 1];
    default: return NULL;
    }
}

static es2_ui_handler resolve_handler(void *context, uint32_t address) {
    (void)context;
    switch (address) {
    case ES2_FLOW_BAY_ONLINE: case ES2_FLOW_BAY_MAIN_MENU: case ES2_FLOW_BAY_SAVE: case ES2_FLOW_BAY_WEAPONS:
    case ES2_FLOW_BAY_REPAIR: case ES2_FLOW_BAY_BUILD: case ES2_FLOW_BAY_ARMORY: case ES2_FLOW_BAY_CREW:
    case ES2_FLOW_BAY_MISSION:
        return on_bar;
    default:
        /* the rows 0x43e477, 0x43e4e6, ... 0x43e780 (step 0x6f) */
        if (address >= 0x43e477u && address <= 0x43e780u && (address - 0x43e477u) % 0x6fu == 0) return on_row;
        return NULL; /* the title's 0x43e41e only destroys the event */
    }
}

static int build(es2_ui *ui, es2_service_bay *b, int screen, es2_ui_id *ids, int count) {
    es2_ui_resolver r;
    r.context = b;
    r.font = resolve_font;
    r.bitmap = resolve_bitmap;
    r.text = resolve_text;
    r.handler = resolve_handler;
    memset(ids, 0, (size_t)count * sizeof ids[0]);
    if (es2_ui_layout_build(ui, screen, &r, ids, count) <= 0) return -1;
    for (int k = 0; k < count; k++) {
        es2_ui_widget *w = es2_ui_get(ui, ids[k]);
        if (!w) return -1;
        w->user = b;
    }
    return 0;
}

/* Original: VSHELL.EXE 0x43afd4 (the table is the layout table's) */
int es2_screen_service_bay_build(es2_ui *ui, void *ctx) {
    es2_service_bay *b = ctx;
    if (!ui || !b || !b->flow) return -1;
    b->ui = ui;
    /* image values: 0x482bc9 = 0, 0x4758b8 = -1, 0x477778 = 0, 0x474094 = 0 */
    es2_shell_editor_session_init(&b->session, 0);
    return build(ui, b, ES2_UI_SCREEN_SERVICE_BAY, b->bay, ES2_SCREEN_SERVICE_BAY_CONTROLS);
}

/* Original: VSHELL.EXE 0x43d029 */
int es2_screen_squad_panel_build(es2_ui *ui, void *ctx) {
    es2_service_bay *b = ctx;
    if (!ui || !b || !b->flow) return -1;
    b->ui = ui;
    return build(ui, b, ES2_UI_SCREEN_SQUAD_PANEL, b->squad, ES2_SCREEN_SQUAD_PANEL_CONTROLS);
}
