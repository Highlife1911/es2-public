/* The practice screen 0x44b310 (es2/shell/gui/practice_screen.h).
 *
 * Contract: docs/reverse-engineering/vshell_ui.md sections 4.4, 15.2 and
 * 25. Original: VSHELL.EXE 0x44b310..0x44cdab.
 * The state rules (entry, cyclic controls, commit) are es2/shell/practice.h's.
 * */
#include "es2/shell/gui/practice_screen.h"

#include <string.h>

#include "es2/shell/draw.h"

/* ---- helpers ----------------------------------------------------------------- */

static es2_ui_id id_of(const es2_practice_screen *ps, int control) { return ps->ids[control]; }

static es2_practice_screen *screen_of(es2_ui *ui, es2_ui_id window) {
    es2_ui_widget *w = es2_ui_get(ui, window);
    return w ? (es2_practice_screen *)w->user : NULL;
}

static const char *estext(const es2_practice_screen *ps, unsigned id) {
    return ps->host.text ? ps->host.text(ps->host.ctx, id) : NULL;
}

/* The five parameter controls: the library's control id, the button, the value
 * button whose label shows ESTEXT base + byte, the handler. */
static const struct {
    int control, button, value;
    uint32_t handler;
} params[5] = {
    {ES2_SHELL_PRACTICE_DAMAGE, ES2_PRACTICE_DAMAGE, ES2_PRACTICE_VALUE_DAMAGE, 0x44c5b9},
    {ES2_SHELL_PRACTICE_AMMO, ES2_PRACTICE_AMMO, ES2_PRACTICE_VALUE_AMMO, 0x44c676},
    {ES2_SHELL_PRACTICE_DIFFICULTY, ES2_PRACTICE_DIFFICULTY, ES2_PRACTICE_VALUE_DIFFICULTY, 0x44c733},
    {ES2_SHELL_PRACTICE_TIME, ES2_PRACTICE_TIME, ES2_PRACTICE_VALUE_TIME, 0x44c7f0},
    {ES2_SHELL_PRACTICE_HERC, ES2_PRACTICE_HERC, ES2_PRACTICE_VALUE_HERC, 0x44c8ad},
};

static int param_of(uint32_t handler) {
    for (int k = 0; k < 5; k++)
        if (params[k].handler == handler) return k;
    return -1;
}

static int entry_of(uint32_t handler) {
    for (int i = 0; i < ES2_SHELL_PRACTICE_ENTRIES; i++)
        if (handler == ES2_PRACTICE_ENTRY_HANDLER0 + 0x61u * (uint32_t)i) return i;
    return -1;
}

/* 0x4087f0(base + byte) and 0x40b8bb(label, text, label +0x45, label +0xb5,
 * redraw): the value label keeps its alignment and colour. */
static void set_value(es2_practice_screen *ps, int k, int redraw) {
    es2_ui_widget *b = es2_ui_get(ps->ui, id_of(ps, params[k].value)), *l;
    if (!b || !ps->flow) return;
    l = es2_ui_get(ps->ui, b->label);
    if (!l) return;
    (void)es2_ui_message_set(ps->ui, b->label, estext(ps, es2_shell_practice_label(params[k].control, &ps->flow->prefs)),
                             l->align, l->text_colour, redraw);
}

static es2_shell_flow_result report(es2_practice_screen *ps, uint32_t handler, es2_shell_flow_result r, int changed) {
    ps->last_handler = handler;
    ps->last_result = r;
    ps->events++;
    if (changed && ps->host.changed) ps->host.changed(ps->host.ctx, handler, r);
    return r;
}

/* ---- resolver for the layout table
 * ---------------------------------------------- */

static const es2_dfn *resolve_font(void *context, uint32_t global) {
    es2_practice_screen *ps = context;
    return ps->host.font ? ps->host.font(ps->host.ctx, global) : NULL;
}

static const es2_bitmap *resolve_bitmap(void *context, const es2_ui_layout_ref *ref) {
    es2_practice_screen *ps = context;
    if (!ps->host.bitmap || ref->kind != ES2_UI_REF_GLOBAL || ref->a != 0x46de0c) return NULL;
    return ps->host.bitmap(ps->host.ctx, es2_menu_background_file(4)); /* bay2a_84 */
}

static const char *resolve_text(void *context, const es2_ui_layout_ref *ref) {
    es2_practice_screen *ps = context;
    if (ref->kind == ES2_UI_REF_ESTEXT) return estext(ps, (unsigned)ref->a);
    if (ref->kind == ES2_UI_REF_EXE_STRING && ps->host.literal) return ps->host.literal(ps->host.ctx, (uint32_t)ref->a);
    return NULL; /* the value labels' run-time IDs: set after the build */
}

static void on_handler(es2_ui *ui, es2_ui_id window, const es2_gui_event *ev);

static es2_ui_handler resolve_handler(void *context, uint32_t address) {
    (void)context;
    if (param_of(address) >= 0 || entry_of(address) >= 0 || address == ES2_FLOW_PRACTICE_MAIN_MENU ||
        address == ES2_PRACTICE_BEGIN_HANDLER)
        return on_handler;
    return NULL;
}

/* Original: VSHELL.EXE 0x44b310 (constructor; the table is the layout table's).
 * */
int es2_screen_practice_build(es2_ui *ui, void *ctx) {
    es2_practice_screen *ps = ctx;
    es2_ui_resolver r;
    if (!ui || !ps) return -1;
    ps->ui = ui;
    r.context = ps;
    r.font = resolve_font;
    r.bitmap = resolve_bitmap;
    r.text = resolve_text;
    r.handler = resolve_handler;
    memset(ps->ids, 0, sizeof ps->ids);
    if (es2_ui_layout_build(ui, ES2_UI_SCREEN_PRACTICE, &r, ps->ids, ES2_PRACTICE_CONTROLS) < 0) return -1;
    for (int k = 0; k < ES2_PRACTICE_CONTROLS; k++) {
        es2_ui_widget *w = es2_ui_get(ui, ps->ids[k]);
        if (!w) return -1;
        w->user = ps;
    }
    /* 0x44bc06..: the value buttons are built with 0x4087f0(base + byte) of
     * the current block (loaded at start-up); hidden, so nothing is drawn */
    for (int k = 0; k < 5; k++) set_value(ps, k, 0);
    return 0;
}

/* ---- show, hide, entry ------------------------------------------------------ */

/* Original: VSHELL.EXE 0x44c322 */
void es2_practice_screen_show(es2_practice_screen *ps) {
    if (!ps || !ps->ui) return;
    es2_ui_show(ps->ui, id_of(ps, ES2_PRACTICE_BACKGROUND));
    es2_ui_show(ps->ui, id_of(ps, ES2_PRACTICE_PANEL));
    (void)es2_practice_screen_entry(ps, 0);
}

/* Original: VSHELL.EXE 0x44c343 */
void es2_practice_screen_hide(es2_practice_screen *ps) {
    if (!ps || !ps->ui) return;
    es2_ui_hide(ps->ui, id_of(ps, ES2_PRACTICE_BACKGROUND));
    es2_ui_hide(ps->ui, id_of(ps, ES2_PRACTICE_PANEL));
}

/* 0x40ab3b(row, 0, colour), row +0x4d := 0x10, 0x41f3ed(row). */
static void row_colour(es2_practice_screen *ps, int i, int32_t colour) {
    es2_ui_id row = id_of(ps, ES2_PRACTICE_ENTRY0 + 5 * i);
    es2_ui_widget *w = es2_ui_get(ps->ui, row);
    if (!w) return;
    es2_ui_quad_set_colour(ps->ui, row, 0, colour);
    w->colour = 0x10;
    es2_ui_redraw_tree(ps->ui, row);
}

/* Original: VSHELL.EXE 0x44c40c */
int es2_practice_screen_entry(es2_practice_screen *ps, int index) {
    es2_ui_id herc;
    es2_ui_widget *b, *l;
    int old, changed = 0;
    if (!ps || !ps->ui || !ps->flow || !ps->practice || index < 0 || index >= ES2_SHELL_PRACTICE_ENTRIES) return -1;
    old = ps->practice->index;
    if (es2_shell_practice_entry(ps->practice, &ps->flow->prefs, ps->tables, index, &changed, NULL) != ES2_OK)
        return -1;
    if (!changed) return 0; /* 0x44c41c: the selection itself */
    /* 0x44c422..0x44c464: the old row normal (a previous 8..10 is INSTANT
     * ACTION's, whose process ends before a show: not reached) */
    if (old >= 0 && old < ES2_SHELL_PRACTICE_ENTRIES) row_colour(ps, old, 0x27);
    /* 0x44c46f..0x44c49c: the new row highlighted */
    row_colour(ps, index, 0x29);
    /* 0x44c49d..0x44c52d: Herc Type greyed and disabled for 0..3, else enabled */
    herc = id_of(ps, ES2_PRACTICE_HERC);
    b = es2_ui_get(ps->ui, herc);
    if (b) {
        int on = index >= 4;
        l = es2_ui_get(ps->ui, b->label);
        if (l) l->text_colour = on ? 0x29 : 0x26;
        b->colour = on ? 0x22 : 0x26;
        es2_ui_redraw_tree(ps->ui, herc);
        b->enabled = on;
        es2_ui_draw(ps->ui, herc); /* vtable +4 */
    }
    /* 0x44c52e..0x44c57e: +0x28 from the table (the library's), its label */
    set_value(ps, 4, 1);
    return 1;
}

/* ---- the handlers ------------------------------------------------------------ */

/* Original: VSHELL.EXE 0x44c96a */
static es2_shell_flow_result main_menu(es2_practice_screen *ps) {
    es2_shell_flow_result r;
    es2_practice_screen_hide(ps);                /* 0x44c97a */
    if (ps->menu) es2_main_menu_show(ps->menu);  /* 0x44c97f */
    r = ps->flow ? es2_shell_flow_event(ps->flow, ES2_FLOW_PRACTICE_MAIN_MENU, NULL) : ES2_FLOW_IGNORED;
    return r;
}

/* Original: VSHELL.EXE 0x44ca26 */
static es2_shell_flow_result begin_mission(es2_practice_screen *ps) {
    uint8_t out[ES2_SHELL_PREFS_SIZE];
    es2_shell_practice_launch launch;
    es2_shell_flow *flow = ps->flow;
    if (!flow || !ps->practice ||
        es2_shell_practice_begin(ps->practice, &flow->prefs, ps->tables, out, &launch, NULL) != ES2_OK)
        return ES2_FLOW_REFUSED;
    /* 0x40dd6e(1) (done by the library), 0x40dd0a */
    if (flow->host.write_prefs && !flow->host.write_prefs(flow->host.ctx, out)) {
        (void)main_menu(ps); /* own: the text menu's answer to a failed step */
        return ES2_FLOW_FAILED;
    }
    /* 0x40e8a5(0x479f70, +0x27) in mode 0, then the posted Use Default */
    if (!ps->host.launch || !ps->host.launch(ps->host.ctx, &launch)) {
        (void)main_menu(ps);
        return ES2_FLOW_FAILED;
    }
    es2_shell_flow_game_created(flow);
    es2_shell_flow_dbsim(flow);
    return ES2_FLOW_DONE;
}

es2_shell_flow_result es2_practice_screen_press(es2_practice_screen *ps, uint32_t handler, int click) {
    int k, i;
    if (!ps || !ps->ui) return ES2_FLOW_IGNORED;
    if ((i = entry_of(handler)) >= 0) {
        int changed = es2_practice_screen_entry(ps, i);
        return report(ps, handler, changed > 0 ? ES2_FLOW_DONE : changed == 0 ? ES2_FLOW_IGNORED : ES2_FLOW_REFUSED, 0);
    }
    if ((k = param_of(handler)) >= 0) {
        /* 0x44c5cd: `dec; jne`: 1 (left) forward 0x40de5b, anything else back 0x40de88 */
        int applied = 0;
        if (!ps->flow || !ps->practice ||
            es2_shell_practice_control(ps->practice, &ps->flow->prefs, params[k].control,
                                       click == ES2_SHELL_CLICK_LEFT ? ES2_SHELL_CLICK_LEFT : ES2_SHELL_CLICK_RIGHT,
                                       &applied, NULL) != ES2_OK)
            return report(ps, handler, ES2_FLOW_REFUSED, 0);
        if (!applied) return report(ps, handler, ES2_FLOW_IGNORED, 0);
        set_value(ps, k, 1);
        return report(ps, handler, ES2_FLOW_DONE, 0);
    }
    if (handler == ES2_FLOW_PRACTICE_MAIN_MENU) return report(ps, handler, main_menu(ps), 1);
    if (handler == ES2_PRACTICE_BEGIN_HANDLER) return report(ps, handler, begin_mission(ps), 1);
    return ES2_FLOW_IGNORED;
}

static void on_handler(es2_ui *ui, es2_ui_id window, const es2_gui_event *ev) {
    es2_practice_screen *ps = screen_of(ui, window);
    es2_ui_widget *w = es2_ui_get(ui, window);
    if (ps && w)
        (void)es2_practice_screen_press(ps, w->original_handler,
                                        ev && ev->code == ES2_GUI_MOUSE_LEFT_UP ? ES2_SHELL_CLICK_LEFT
                                                                                : ES2_SHELL_CLICK_RIGHT);
}
