/* The REPAIR screen 0x4326c7 and the Herc scrap warning 0x4479b8
 * (es2/shell/gui/repair.h).
 *
 * Contract: docs/reverse-engineering/vshell_ui.md sections 4.7, 12 and
 * 30. Original: VSHELL.EXE 0x4326c7..0x435596 and
 * 0x447da1..0x4483e8. The state rules are the editors library
 * (es2_shell_editors); this file draws what the original's routines draw from
 * that state. */
#include "es2/shell/gui/repair.h"
#include "es2/shell/bytes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "es2/formats/sav.h"

/* ESTEXT IDs of the External rows 0..5 and the Internal rows 0..8: the word
 * tables 0x47409c (every vehicle) and 0x4740ba (vehicle 8), read by 0x43436f. */
static const uint16_t row_labels[2][15] = {
    {0x4e, 0x4f, 0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x5b, 0x5c},
    {0x4e, 0x5d, 0x5e, 0x5f, 0x60, 0x61, 0x62, 0x63, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x5b, 0x5c}};
/* 0x43e09f: the colour of condition band 0..5 (the six words at 0x476666). */
static const int32_t band_colours[6] = {0x0e, 0x0d, 0x0c, 0x0b, 0x0a, 0x27};

enum { COLOUR_LABEL = 0x27, COLOUR_SELECTED = 0x29, COLOUR_FRAME = 0x10, COLOUR_VALUE = 0x17 };

/* ---- helpers ----------------------------------------------------------------- */

static es2_ui_id id_of(const es2_repair_screen *rs, int control) {
    return control >= 0 && control < ES2_REPAIR_CONTROLS ? rs->ids[control] : 0;
}
static es2_repair_screen *screen_of(es2_ui *ui, es2_ui_id window) {
    es2_ui_widget *w = es2_ui_get(ui, window);
    return w ? (es2_repair_screen *)w->user : NULL;
}
static const char *estext(const es2_repair_screen *rs, unsigned id) {
    return rs->host.text ? rs->host.text(rs->host.ctx, id) : NULL;
}
static void squad(es2_repair_screen *rs, uint32_t routine, int16_t a, int16_t b) {
    if (rs->host.squad) rs->host.squad(rs->host.ctx, routine, a, b);
}

int es2_repair_row_control(int tab, int row) {
    if (tab == 0 && row >= 0 && row < ES2_REPAIR_EXTERNAL_ROWS) return ES2_REPAIR_EXTERNAL_ROW0 + 5 * row;
    if (tab == 1 && row >= 0 && row < ES2_REPAIR_INTERNAL_ROWS) return ES2_REPAIR_INTERNAL_ROW0 + 5 * row;
    return -1;
}
/* 0x433aa0 / 0x433ac1 */
static int16_t family_of(int tab, int row) { return (int16_t)(tab ? 1 : row > 5 ? 2 : 0); }
static int16_t index_of(int16_t family, int row) { return (int16_t)(family == 2 ? row - 6 : row); }

/* 0x43e05f: the first band limit (0x47665c, the editor tables' first five
 * words) that c exceeds, signed 16-bit; 5 when none. */
static int band_of(const es2_repair_screen *rs, int16_t c) {
    int i;
    if (!rs->editor_tables.data || rs->editor_tables.size < 10) return 5;
    for (i = 0; i < 5; i++)
        if (c > es2_le16s(rs->editor_tables.data + 2 * i)) break;
    return i;
}

/* One run of the library on the memory state. commit: the new SAV replaces
 * *save when the operations applied. Returns 1 applied, 0 not, -1 refused. */
static int run(es2_repair_screen *rs, es2_shell_editor_session *session, es2_shell_editor_op *ops, size_t count,
               es2_shell_editor_view *view, int commit) {
    es2_shell_buffer out = {0};
    int applied = 0;
    if (!rs->vfs || !rs->save || !rs->save->data || !session) {
        rs->status = ES2_ERR_ARGUMENT;
        return -1;
    }
    rs->status = es2_shell_editors(rs->vfs, rs->tables, rs->editor_tables, (es2_span){rs->save->data, rs->save->size},
                                   session, ops, count, &out, view, &rs->error);
    if (rs->status != ES2_OK) return -1;
    for (size_t i = 0; i < count; i++) applied |= ops[i].applied;
    if (commit && applied) {
        free(rs->save->data);
        *rs->save = out;
    } else
        free(out.data);
    return applied;
}
static int apply(es2_repair_screen *rs, int kind, int16_t a, int16_t b) {
    es2_shell_editor_op op = {kind, a, b, 0};
    return run(rs, rs->session, &op, 1, &rs->view, 1);
}
static void update_view(es2_repair_screen *rs) { (void)run(rs, rs->session, NULL, 0, &rs->view, 0); }

/* 0x41232a(rec + 8, family, index) of a row: the library's view of a session
 * copy with that row selected (100 without a record). */
static int16_t row_condition(es2_repair_screen *rs, int tab, int row) {
    es2_shell_editor_session s = *rs->session;
    es2_shell_editor_view v;
    s.tab = (int16_t)tab;
    s.row = (int16_t)row;
    s.family = family_of(tab, row);
    s.index = index_of(s.family, row);
    return run(rs, &s, NULL, 0, &v, 0) < 0 ? 100 : v.condition;
}

/* The selected bay's hangar record as the row texts read it: vehicle word
 * +0, capacity +0x4c, the item ID word of each slot's item (0 for none). */
typedef struct bay_record {
    int present;
    int16_t vehicle, capacity, item[10];
} bay_record;
static void record_of(const es2_repair_screen *rs, bay_record *out) {
    es2_sav sav;
    memset(out, 0, sizeof *out);
    out->vehicle = -1;
    if (!rs->save || !rs->save->data || !rs->session || rs->session->bay < 0 ||
        es2_sav_parse((es2_span){rs->save->data, rs->save->size}, &sav, NULL) != ES2_OK)
        return;
    for (size_t i = 0; i < sav.loadout_count; i++) {
        es2_sav_loadout l;
        if (es2_sav_loadout_at(&sav, i, &l, NULL) != ES2_OK || l.index != rs->session->bay) continue;
        out->present = 1;
        out->vehicle = es2_le16s(l.fixed.data);
        out->capacity = es2_le16s(l.fixed.data + 74);
        for (size_t j = 0; j < l.count; j++) {
            const uint8_t *r = l.items.data + 12 * j;
            int16_t slot = es2_le16s(r);
            if (slot >= 0 && slot < 10) out->item[slot] = es2_le16s(r + 2);
        }
    }
}

/* 0x40b8bb(label, text, align, colour, 1) on a column of a row. */
static void set_column(es2_repair_screen *rs, int control, int column, const char *text, int32_t align, int32_t colour) {
    es2_ui_widget *q = es2_ui_get(rs->ui, id_of(rs, control));
    if (q) (void)es2_ui_message_set(rs->ui, q->columns[column], text, align, colour, 1);
}
/* 0x40b8bb on a button's label (+0x55). */
static void set_label(es2_repair_screen *rs, int control, const char *text, int32_t align, int32_t colour, int redraw) {
    es2_ui_widget *b = es2_ui_get(rs->ui, id_of(rs, control));
    if (b) (void)es2_ui_message_set(rs->ui, b->label, text, align, colour, redraw);
}

/* ---- the drawing routines ----------------------------------------------------- */

/* Original: VSHELL.EXE 0x434042 (the row's texts) */
static void row_text(es2_repair_screen *rs, int tab, int row, const bay_record *rec) {
    char condition[16], number[16];
    int16_t family = family_of(tab, row), index = index_of(family, row);
    int16_t c = row_condition(rs, tab, row);
    int32_t colour = band_colours[band_of(rs, c)];
    int control = es2_repair_row_control(tab, row);
    snprintf(condition, sizeof condition, "%d%%", c); /* 0x4746f3 */
    if (family != 2) {                                /* 0x434124, 0x43414b */
        set_column(rs, control, 3, condition, 1, colour);
        return;
    }
    if (index >= (rec->present ? rec->capacity : 0)) { /* 0x43419a: ESTEXT 1 (a blank) in three columns */
        set_column(rs, control, 0, estext(rs, 1), 1, COLOUR_LABEL);
        set_column(rs, control, 2, estext(rs, 1), 1, COLOUR_LABEL);
        set_column(rs, control, 3, estext(rs, 1), 1, COLOUR_LABEL);
        return;
    }
    if (!rec->item[index]) { /* 0x434271: "--Empty--" and the condition " " (0x4746f8) */
        set_column(rs, control, 0, estext(rs, 0x7d), 0, COLOUR_LABEL);
        snprintf(condition, sizeof condition, " ");
    } else /* 0x4342b0 */
        set_column(rs, control, 0, estext(rs, (unsigned)(uint16_t)(int16_t)(0x7e + rec->item[index])), 0, COLOUR_LABEL);
    snprintf(number, sizeof number, "%d", index + 1); /* 0x4746fa */
    set_column(rs, control, 2, number, 0, COLOUR_LABEL);
    set_column(rs, control, 3, condition, 1, colour);
}

/* One of REPAIR, REPAIR ALL, SCRAP (0x433cfb..0x433d85 and the two copies). */
static void set_button(es2_repair_screen *rs, int control, int on) {
    es2_ui_id id = id_of(rs, control);
    es2_ui_widget *b = es2_ui_get(rs->ui, id), *l;
    if (!b) return;
    l = es2_ui_get(rs->ui, b->label);
    if (l) l->text_colour = on ? 0x29 : 0x26;
    b->colour = on ? 0x22 : 0x26;
    es2_ui_redraw_tree(rs->ui, id);
    b->enabled = on;
    es2_ui_draw(rs->ui, id); /* vtable +4 */
}

/* "%ld %s" (0x4746da, 0x4746e1, 0x4746e8) with ESTEXT 0xc8. */
static void set_amount(es2_repair_screen *rs, int control, int32_t value) {
    char text[64];
    const char *unit = estext(rs, 0xc8);
    snprintf(text, sizeof text, "%ld %s", (long)value, unit ? unit : "");
    set_label(rs, control, text, 2, COLOUR_VALUE, 1);
}

/* Original: VSHELL.EXE 0x433ad5 (labels and enable rules) */
static void refresh(es2_repair_screen *rs) {
    const es2_shell_editor_view *v = &rs->view;
    es2_ui_widget *b;
    int band;
    update_view(rs);
    set_amount(rs, ES2_REPAIR_COST, v->has_record ? v->item_cost : 0);
    /* 0x433b5c..0x433bef: +0xb5 := 0x43e09f(c), then overwritten by the call's 0x17 */
    band = band_of(rs, v->condition);
    if ((b = es2_ui_get(rs->ui, id_of(rs, ES2_REPAIR_CONDITION))) && es2_ui_get(rs->ui, b->label))
        es2_ui_get(rs->ui, b->label)->text_colour = band_colours[band];
    set_label(rs, ES2_REPAIR_CONDITION, estext(rs, (unsigned)(0x68 + band)), 2, COLOUR_VALUE, 1);
    set_amount(rs, ES2_REPAIR_ALL_COST, v->has_record ? v->all_cost : 0);
    set_amount(rs, ES2_REPAIR_SALVAGE, (int32_t)(v->salvage - v->bank_cost));
    set_button(rs, ES2_REPAIR_REPAIR, v->repair_enabled);
    set_button(rs, ES2_REPAIR_REPAIR_ALL, v->repair_all_enabled);
    set_button(rs, ES2_REPAIR_SCRAP, v->scrap_enabled);
    squad(rs, ES2_REPAIR_SQUAD_LABELS, rs->session->bay, 0);
    squad(rs, ES2_REPAIR_SQUAD_CONDITION, rs->session->bay, 0);
    squad(rs, ES2_REPAIR_SQUAD_DIAGRAM, rs->session->bay, 0);
}

/* Original: VSHELL.EXE 0x43433f (every row, then 0x433ad5) */
static void refresh_rows(es2_repair_screen *rs) {
    bay_record rec;
    record_of(rs, &rec);
    for (int r = 0; r < ES2_REPAIR_EXTERNAL_ROWS; r++) row_text(rs, 0, r, &rec);
    for (int r = 0; r < ES2_REPAIR_INTERNAL_ROWS; r++) row_text(rs, 1, r, &rec);
    refresh(rs);
}

/* Original: VSHELL.EXE 0x43436f (the part and system names by vehicle) */
static void vehicle_labels(es2_repair_screen *rs) {
    bay_record rec;
    const uint16_t *t;
    record_of(rs, &rec);
    t = row_labels[rec.vehicle == 8];
    for (int r = 0; r < 6; r++) set_column(rs, es2_repair_row_control(0, r), 0, estext(rs, t[r]), 0, COLOUR_LABEL);
    for (int r = 0; r < ES2_REPAIR_INTERNAL_ROWS; r++)
        set_column(rs, es2_repair_row_control(1, r), 0, estext(rs, t[6 + r]), 0, COLOUR_LABEL);
}

/* Original: VSHELL.EXE 0x4344cc (frame colour +0x4d, column 0 colour) */
static void row_colour(es2_repair_screen *rs, int tab, int row, int32_t frame, int32_t colour) {
    es2_ui_id q = id_of(rs, es2_repair_row_control(tab, row));
    es2_ui_widget *w = es2_ui_get(rs->ui, q);
    if (!w) return;
    es2_ui_quad_set_colour(rs->ui, q, 0, colour);
    w->colour = frame;
    es2_ui_redraw_tree(rs->ui, q);
}

/* Original: VSHELL.EXE 0x433fcd(old tab, new tab): the squad grid and this
 * screen's grid of the selected bay swap places. A bay of -1 reads the words
 * before both arrays; ours does nothing then. */
static void swap_grids(es2_repair_screen *rs, int from, int to) {
    int16_t bay = rs->session->bay;
    if (bay < 0 || bay > 7) return;
    if (from == 0 && to == 1) {
        squad(rs, ES2_REPAIR_SQUAD_GRID_HIDE, bay, 0);
        es2_ui_show(rs->ui, id_of(rs, ES2_REPAIR_GRID0 + bay));
    }
    if (from == 1 && to == 0) {
        es2_ui_hide(rs->ui, id_of(rs, ES2_REPAIR_GRID0 + bay));
        squad(rs, ES2_REPAIR_SQUAD_GRID_SHOW, bay, 0);
    }
}

/* The screen side of 0x434549 after the library stored a new selection
 * (0x43461a..0x434688). */
static void selected(es2_repair_screen *rs, const es2_shell_editor_session *old) {
    const es2_shell_editor_session *s = rs->session;
    if (s->tab == old->tab && s->row == old->row) return;
    row_colour(rs, old->tab, old->row, COLOUR_FRAME, COLOUR_LABEL);
    row_colour(rs, s->tab, s->row, COLOUR_SELECTED, COLOUR_SELECTED);
    swap_grids(rs, old->tab, s->tab);
    refresh(rs);
}

/* The squad panel's selection 0x43dcdd(new_bay) on the present screen
 * word: host.squad(ES2_REPAIR_SQUAD_SELECT) is called with the session's
 * bay still old_bay. A host with the squad panel
 * (es2_squad_panel_select) stores the new bay itself and runs its word's case
 * (on word 3 that case calls es2_repair_screen_selected): returns 1. Without
 * such a host the bay is stored here: returns 0. */
static int host_select(es2_repair_screen *rs, int16_t old_bay, int16_t new_bay) {
    rs->session->bay = old_bay;
    squad(rs, ES2_REPAIR_SQUAD_SELECT, old_bay, new_bay);
    if (rs->session->bay != old_bay) return 1;
    rs->session->bay = new_bay;
    return 0;
}

/* REPAIR's part of 0x43dcdd after the bay changed (0x43dddb..0x43dded): the
 * row (0, 0), the names, the rows; the snapshot is the library's. */
static void repair_part(es2_repair_screen *rs, const es2_shell_editor_session *old) {
    selected(rs, old);
    vehicle_labels(rs);
    refresh_rows(rs);
}

/* 0x43dcdd's REPAIR case after the library changed the bay (0x43dd55..
 * 0x43ddf2). With the squad panel's host its part runs there (and calls
 * es2_repair_screen_selected); otherwise here: the old bay's grid hidden, the
 * new bay's squad grid shown, then REPAIR's part. */
static void bay_changed(es2_repair_screen *rs, const es2_shell_editor_session *old) {
    int16_t bay = rs->session->bay;
    if (host_select(rs, old->bay, bay)) return;
    if (old->bay >= 0 && old->bay <= 7) {
        if (old->tab == 0)
            squad(rs, ES2_REPAIR_SQUAD_GRID_HIDE, old->bay, 0);
        else
            es2_ui_hide(rs->ui, id_of(rs, ES2_REPAIR_GRID0 + old->bay));
    }
    squad(rs, ES2_REPAIR_SQUAD_GRID_SHOW, bay, 0);
    repair_part(rs, old);
}

/* ---- resolver for the layout table
 * ---------------------------------------------- */

static const es2_dfn *resolve_font(void *context, uint32_t global) {
    es2_repair_screen *rs = context;
    return rs->host.font ? rs->host.font(rs->host.ctx, global) : NULL;
}
static const es2_bitmap *resolve_bitmap(void *context, const es2_ui_layout_ref *ref) {
    (void)context;
    (void)ref;
    return NULL; /* neither screen has an image */
}
static const char *resolve_text(void *context, const es2_ui_layout_ref *ref) {
    es2_repair_screen *rs = context;
    if (ref->kind == ES2_UI_REF_ESTEXT) return estext(rs, (unsigned)ref->a);
    if (ref->kind == ES2_UI_REF_EXE_STRING && rs->host.literal) return rs->host.literal(rs->host.ctx, (uint32_t)ref->a);
    return NULL;
}

static void on_handler(es2_ui *ui, es2_ui_id window, const es2_gui_event *ev);

static int row_of(uint32_t handler, int *tab) {
    for (int t = 0; t < 2; t++) {
        uint32_t first = t ? ES2_REPAIR_INTERNAL_ROW_HANDLER0 : ES2_REPAIR_EXTERNAL_ROW_HANDLER0;
        int rows = t ? ES2_REPAIR_INTERNAL_ROWS : ES2_REPAIR_EXTERNAL_ROWS;
        if (handler >= first && (handler - first) % 0x65u == 0 && (handler - first) / 0x65u < (uint32_t)rows) {
            *tab = t;
            return (int)((handler - first) / 0x65u);
        }
    }
    return -1;
}

static es2_ui_handler resolve_handler(void *context, uint32_t address) {
    int tab;
    (void)context;
    switch (address) {
    case ES2_REPAIR_PANEL_HANDLER: case ES2_REPAIR_EXTERNAL_HANDLER: case ES2_REPAIR_INTERNAL_HANDLER:
    case ES2_REPAIR_REPAIR_HANDLER: case ES2_REPAIR_ALL_HANDLER: case ES2_REPAIR_SCRAP_HANDLER:
    case ES2_REPAIR_CANCEL_HANDLER: case ES2_HERC_SCRAP_CANCEL_HANDLER: case ES2_HERC_SCRAP_ACCEPT_HANDLER:
        return on_handler;
    default:
        return row_of(address, &tab) >= 0 ? on_handler : NULL;
    }
}

static int build(es2_ui *ui, es2_repair_screen *rs, int screen, es2_ui_id *ids, int n) {
    es2_ui_resolver r;
    r.context = rs;
    r.font = resolve_font;
    r.bitmap = resolve_bitmap;
    r.text = resolve_text;
    r.handler = resolve_handler;
    memset(ids, 0, sizeof *ids * (size_t)n);
    if (es2_ui_layout_build(ui, screen, &r, ids, n) < 0) return -1;
    for (int k = 0; k < n; k++) {
        es2_ui_widget *w = es2_ui_get(ui, ids[k]);
        if (!w) return -1;
        w->user = rs;
    }
    return 0;
}

/* Original: VSHELL.EXE 0x4326c7 (constructor; the table is the layout table's)
 * */
int es2_screen_repair_build(es2_ui *ui, void *ctx) {
    es2_repair_screen *rs = ctx;
    if (!ui || !rs) return -1;
    rs->ui = ui;
    return build(ui, rs, ES2_UI_SCREEN_REPAIR, rs->ids, ES2_REPAIR_CONTROLS);
}

/* Original: VSHELL.EXE 0x4479b8 */
int es2_screen_herc_scrap_box_build(es2_ui *ui, void *ctx) {
    es2_repair_screen *rs = ctx;
    if (!ui || !rs || (rs->ui && rs->ui != ui)) return -1;
    rs->ui = ui;
    return build(ui, rs, ES2_UI_SCREEN_HERC_SCRAP_BOX, rs->box, ES2_HERC_SCRAP_CONTROLS);
}

/* ---- show, hide, bay ---------------------------------------------------------- */

/* Original: VSHELL.EXE 0x43397c */
es2_shell_flow_result es2_repair_screen_show(es2_repair_screen *rs, int previous_word) {
    es2_shell_editor_session old, trial;
    uint8_t build_byte, option;
    int16_t mapped;
    if (!rs || !rs->ui || !rs->session || !rs->flow) return ES2_FLOW_FAILED;
    es2_ui_show(rs->ui, id_of(rs, ES2_REPAIR_PANEL));
    es2_ui_show(rs->ui, id_of(rs, ES2_REPAIR_EXTERNAL));
    /* 0x433995: the repair option of the current block; other values keep the label */
    option = rs->flow->prefs.current[ES2_SHELL_PREFS_REPAIR];
    if (option <= 1) {
        es2_ui_widget *b = es2_ui_get(rs->ui, id_of(rs, ES2_REPAIR_MODE)), *l = b ? es2_ui_get(rs->ui, b->label) : NULL;
        if (l) (void)es2_ui_message_set(rs->ui, b->label, estext(rs, option ? 0x41 : 0x42), l->align, l->text_colour, 0);
    }
    es2_ui_show(rs->ui, id_of(rs, ES2_REPAIR_INTERNAL));
    /* the bay check 0x433a10..0x433a62, the snapshot 0x433f86 and 0x434549(0, 0)
     * are the library's OPEN; the flow left the previous screen (its rebalance) */
    build_byte = rs->flow->prefs.current[ES2_SHELL_PREFS_BUILD];
    mapped = (int16_t)(previous_word >= ES2_SHELL_SCREEN_WEAPONS && previous_word <= ES2_SHELL_SCREEN_BUILD
                           ? ES2_SHELL_SCREEN_BAY
                           : previous_word);
    old = *rs->session;
    /* the bay the show selects (0x433a51..0x433a5d), learnt from the library on a copy; the
     * selection 0x43dcdd runs in the previous word's case, the squad panel's (host.squad) */
    trial = *rs->session;
    trial.screen = mapped;
    {
        es2_shell_editor_op op = {ES2_SHELL_EDITOR_OPEN, ES2_SHELL_SCREEN_REPAIR, build_byte, 0};
        es2_shell_editor_view v;
        if (run(rs, &trial, &op, 1, &v, 0) < 0) return ES2_FLOW_FAILED;
    }
    rs->session->screen = (int16_t)previous_word;
    if (trial.bay != old.bay) (void)host_select(rs, old.bay, trial.bay);
    rs->session->screen = mapped;
    if (apply(rs, ES2_SHELL_EDITOR_OPEN, ES2_SHELL_SCREEN_REPAIR, build_byte) < 0) {
        rs->session->screen = old.screen;
        return ES2_FLOW_FAILED;
    }
    refresh_rows(rs);  /* 0x433a63 */
    selected(rs, &old); /* 0x433a71 */
    return ES2_FLOW_DONE;
}

/* Original: VSHELL.EXE 0x433a7b */
void es2_repair_screen_hide(es2_repair_screen *rs) {
    if (!rs || !rs->ui) return;
    es2_ui_hide(rs->ui, id_of(rs, ES2_REPAIR_PANEL));
    es2_ui_hide(rs->ui, id_of(rs, ES2_REPAIR_EXTERNAL));
    es2_ui_hide(rs->ui, id_of(rs, ES2_REPAIR_INTERNAL));
}

int es2_repair_screen_bay(es2_repair_screen *rs, int16_t bay) {
    es2_shell_editor_session old;
    int applied;
    if (!rs || !rs->ui || !rs->session) return -1;
    old = *rs->session;
    applied = apply(rs, ES2_SHELL_EDITOR_BAY, bay, 0);
    if (applied <= 0) return applied;
    bay_changed(rs, &old);
    return 1;
}

/* REPAIR's branch of 0x43dcdd for a squad panel that stored the bay itself
 * (the service bay's host.selected(3, bay, assign)): the row (0, 0) and the
 * snapshot through the library's OPEN from the bay screen (the bay kept: a
 * finished Herc or none), then the names and the rows. */
int es2_repair_screen_selected(es2_repair_screen *rs) {
    es2_shell_editor_session old;
    int applied;
    if (!rs || !rs->ui || !rs->session || !rs->flow) return -1;
    old = *rs->session;
    if (rs->session->bay == -1) { /* OPEN would select the first finished Herc */
        rs->session->screen = ES2_SHELL_SCREEN_REPAIR;
        applied = apply(rs, ES2_SHELL_EDITOR_REPAIR_PICK, 0, 0);
    } else {
        rs->session->screen = ES2_SHELL_SCREEN_BAY;
        applied = apply(rs, ES2_SHELL_EDITOR_OPEN, ES2_SHELL_SCREEN_REPAIR, rs->flow->prefs.current[ES2_SHELL_PREFS_BUILD]);
    }
    if (applied < 0) {
        rs->session->screen = old.screen;
        return -1;
    }
    repair_part(rs, &old);
    return 1;
}

es2_ui_id es2_repair_screen_grid(es2_repair_screen *rs, int16_t bay) {
    return rs && bay >= 0 && bay <= 7 ? rs->ids[ES2_REPAIR_GRID0 + bay] : 0;
}

/* Original: VSHELL.EXE 0x433f43 */
void es2_repair_screen_salvage(es2_repair_screen *rs) {
    char text[32];
    if (!rs || !rs->ui || !rs->session) return;
    update_view(rs);
    snprintf(text, sizeof text, "%ld", (long)(int32_t)(rs->view.salvage - rs->view.bank_cost)); /* 0x4746ef */
    set_label(rs, ES2_REPAIR_SALVAGE, text, 2, COLOUR_VALUE, 1);
}

/* ---- the warning ---------------------------------------------------------------- */

/* Original: VSHELL.EXE 0x447da1 */
void es2_herc_scrap_box_show(es2_repair_screen *rs) {
    char text[64];
    const char *tail;
    es2_ui_widget *l;
    if (!rs || !rs->ui || !rs->session) return;
    update_view(rs);
    tail = estext(rs, 0xcd);
    /* 0x411a33: 0x414174(rec) / 1000, signed (cdq, idiv); "%d %s" (0x478c41) */
    snprintf(text, sizeof text, "%d %s", (int)(rs->view.scrap_value / 1000), tail ? tail : "");
    if ((l = es2_ui_get(rs->ui, rs->box[ES2_HERC_SCRAP_TEXT])))
        (void)es2_ui_message_set(rs->ui, rs->box[ES2_HERC_SCRAP_TEXT], text, l->align, l->text_colour, 1);
    es2_ui_show(rs->ui, rs->box[ES2_HERC_SCRAP_SHIELD]);
    es2_ui_modal_show(rs->ui, rs->box[ES2_HERC_SCRAP_BOX]);
}

/* Original: VSHELL.EXE 0x447e25 */
void es2_herc_scrap_box_hide(es2_repair_screen *rs) {
    if (!rs || !rs->ui) return;
    es2_ui_hide(rs->ui, rs->box[ES2_HERC_SCRAP_SHIELD]);
    es2_ui_modal_hide(rs->ui, rs->box[ES2_HERC_SCRAP_BOX]);
}

/* Original: VSHELL.EXE 0x448326 */
static es2_shell_flow_result accept(es2_repair_screen *rs) {
    es2_shell_editor_session old = *rs->session;
    int applied;
    es2_herc_scrap_box_hide(rs);
    applied = apply(rs, ES2_SHELL_EDITOR_SCRAP, 0, 0); /* 0x40ed0f (and 0x43dcdd(0x41124e) on word 3) */
    if (applied < 0) return ES2_FLOW_REFUSED;
    squad(rs, ES2_REPAIR_SQUAD_REFRESH, rs->session->bay, 0); /* 0x43d80c */
    if (rs->session->screen == ES2_SHELL_SCREEN_REPAIR) es2_repair_screen_salvage(rs); /* 0x43d80c -> 0x433f43 */
    if (rs->session->screen == ES2_SHELL_SCREEN_BUILD) squad(rs, ES2_REPAIR_BUILD_REFRESH, rs->session->bay, 0);
    if (rs->session->screen == ES2_SHELL_SCREEN_REPAIR) {
        if (rs->session->bay != old.bay) bay_changed(rs, &old);
        refresh(rs); /* 0x448376 */
    }
    es2_ui_redraw_tree(rs->ui, id_of(rs, ES2_REPAIR_EXTERNAL));
    es2_ui_redraw_tree(rs->ui, id_of(rs, ES2_REPAIR_CONDITION));
    squad(rs, ES2_REPAIR_SQUAD_REDRAW, rs->session->bay, 0);
    return applied ? ES2_FLOW_DONE : ES2_FLOW_IGNORED;
}

/* ---- the handlers ------------------------------------------------------------ */

static es2_shell_flow_result report(es2_repair_screen *rs, uint32_t handler, es2_shell_flow_result r, int changed) {
    rs->last_handler = handler;
    rs->last_result = r;
    rs->events++;
    if (changed && r == ES2_FLOW_DONE && rs->host.changed) rs->host.changed(rs->host.ctx, handler, r);
    return r;
}
static int enabled(const es2_repair_screen *rs, int control) {
    es2_ui_widget *w = es2_ui_get(rs->ui, id_of(rs, control));
    return w && w->enabled;
}
static es2_shell_flow_result result_of(int applied) {
    return applied < 0 ? ES2_FLOW_REFUSED : applied ? ES2_FLOW_DONE : ES2_FLOW_IGNORED;
}

es2_shell_flow_result es2_repair_screen_press(es2_repair_screen *rs, uint32_t handler) {
    es2_shell_editor_session old;
    int tab, row, applied;
    if (!rs || !rs->ui || !rs->session) return ES2_FLOW_IGNORED;
    old = *rs->session;
    if ((row = row_of(handler, &tab)) >= 0) { /* 0x434549(tab, row) */
        applied = apply(rs, ES2_SHELL_EDITOR_REPAIR_PICK, (int16_t)tab, (int16_t)row);
        if (applied > 0) selected(rs, &old);
        return report(rs, handler, result_of(applied), 0);
    }
    switch (handler) {
    case ES2_REPAIR_INTERNAL_HANDLER: /* 0x434d95 */
        if (rs->session->screen != ES2_SHELL_SCREEN_REPAIR || rs->session->tab != 0 || rs->session->bay < 0)
            return report(rs, handler, ES2_FLOW_IGNORED, 0);
        squad(rs, ES2_REPAIR_SQUAD_GRID_HIDE, rs->session->bay, 0);
        es2_ui_show(rs->ui, id_of(rs, ES2_REPAIR_GRID0 + rs->session->bay));
        return report(rs, handler, ES2_FLOW_DONE, 0);
    case ES2_REPAIR_PANEL_HANDLER:    /* 0x434693: empty */
    case ES2_REPAIR_EXTERNAL_HANDLER: /* 0x4346ec: empty */
        return report(rs, handler, ES2_FLOW_IGNORED, 0);
    case ES2_REPAIR_REPAIR_HANDLER: /* 0x4351bd */
        if (!enabled(rs, ES2_REPAIR_REPAIR)) return ES2_FLOW_IGNORED;
        applied = apply(rs, ES2_SHELL_EDITOR_REPAIR, 0, 0);
        if (applied > 0) {
            bay_record rec;
            record_of(rs, &rec);
            row_text(rs, rs->session->tab, rs->session->row, &rec);
            refresh(rs);
        }
        return report(rs, handler, result_of(applied), 1);
    case ES2_REPAIR_ALL_HANDLER: /* 0x4352e9 */
        if (!enabled(rs, ES2_REPAIR_REPAIR_ALL)) return ES2_FLOW_IGNORED;
        applied = apply(rs, ES2_SHELL_EDITOR_REPAIR_ALL, 0, 0);
        if (applied > 0) refresh_rows(rs);
        return report(rs, handler, result_of(applied), 1);
    case ES2_REPAIR_CANCEL_HANDLER: /* 0x435403 */
        applied = apply(rs, ES2_SHELL_EDITOR_REPAIR_CANCEL, 0, 0);
        if (applied > 0) refresh_rows(rs);
        return report(rs, handler, result_of(applied), 1);
    case ES2_REPAIR_SCRAP_HANDLER: /* 0x4353a5 */
        if (!enabled(rs, ES2_REPAIR_SCRAP)) return ES2_FLOW_IGNORED;
        es2_herc_scrap_box_show(rs);
        return report(rs, handler, ES2_FLOW_DONE, 0);
    case ES2_HERC_SCRAP_CANCEL_HANDLER: /* 0x4482c8 */
        es2_herc_scrap_box_hide(rs);
        return report(rs, handler, ES2_FLOW_DONE, 0);
    case ES2_HERC_SCRAP_ACCEPT_HANDLER:
        return report(rs, handler, accept(rs), 1);
    default:
        return ES2_FLOW_IGNORED;
    }
}

static void on_handler(es2_ui *ui, es2_ui_id window, const es2_gui_event *ev) {
    es2_repair_screen *rs = screen_of(ui, window);
    es2_ui_widget *w = es2_ui_get(ui, window);
    (void)ev;
    if (rs && w) (void)es2_repair_screen_press(rs, w->original_handler);
}
