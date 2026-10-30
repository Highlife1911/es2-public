/* The Registration screen (es2/shell/gui/registration.h).
 * Contract: docs/reverse-engineering/vshell_ui.md sections 4.2 and 24. */
#include "es2/shell/gui/registration.h"

#include <stddef.h>

static es2_screen_registration *of(es2_ui *ui, es2_ui_id window) {
    es2_ui_widget *w = es2_ui_get(ui, window);
    return w ? (es2_screen_registration *)w->user : NULL;
}

static const char *estext(const es2_screen_registration *r, int id) {
    const char *t = r->estext ? r->estext(r->estext_context, id) : NULL;
    return t ? t : "";
}

/* Original: VSHELL.EXE 0x43c671: the label of [0x48d58c] := ESTEXT
 * ((int16)(0x476248 + 0x35)) with the label's own alignment and colour,
 * redrawn. */
static void skill_label(es2_screen_registration *r) {
    es2_ui_widget *b = es2_ui_get(r->ui, r->ids[ES2_REGISTRATION_SKILL_VALUE]);
    es2_ui_widget *l = b ? es2_ui_get(r->ui, b->label) : NULL;
    if (!l) return;
    (void)es2_ui_message_set(r->ui, b->label, estext(r, (int16_t)(r->flow->skill + 0x35)), l->align, l->text_colour, 1);
}

/* Original: VSHELL.EXE 0x43c47e (name field, +0x3d). Key (0x40) and command
 * (0x100) events: with an empty text ACCEPT's label colour and frame colour
 * become 38, a redraw is posted, it is disabled and drawn; otherwise 41 and
 * 34, enabled. (The trace calls 0x46a834 change no state.) */
static void on_name(es2_ui *ui, es2_ui_id window, const es2_gui_event *ev) {
    es2_screen_registration *r = of(ui, window);
    es2_ui_widget *field, *accept, *label;
    es2_ui_id id;
    int empty;
    if (!r || (ev->type != ES2_GUI_EV_KEY && ev->type != ES2_GUI_EV_COMMAND)) return;
    field = es2_ui_get(ui, r->ids[ES2_REGISTRATION_FIELD]);
    id = r->ids[ES2_REGISTRATION_ACCEPT];
    accept = es2_ui_get(ui, id);
    if (!field || !accept) return;
    label = es2_ui_get(ui, accept->label);
    empty = field->edit.text[0] == 0;
    if (label) label->text_colour = empty ? 38 : 41;
    accept->colour = empty ? 38 : 34;
    es2_ui_redraw_tree(ui, id);
    accept->enabled = !empty;
    es2_ui_draw(ui, id);
}

/* Original: VSHELL.EXE 0x43c6ad: 0x476248 := (v + 1) mod 4, then 0x43c671.
 * Every event the button fires (left or right release) steps it. */
static void on_skill(es2_ui *ui, es2_ui_id window, const es2_gui_event *ev) {
    es2_screen_registration *r = of(ui, window);
    (void)ev;
    if (!r) return;
    (void)es2_shell_flow_event(r->flow, ES2_FLOW_SKILL, NULL);
    skill_label(r);
}

/* Original: VSHELL.EXE 0x43c728: hide 0x43c349, main menu 0x431718 (the
 * flow's screen; the main menu's presenter shows it). */
static void on_cancel(es2_ui *ui, es2_ui_id window, const es2_gui_event *ev) {
    es2_screen_registration *r = of(ui, window);
    (void)ev;
    if (!r) return;
    es2_screen_registration_hide(r);
    (void)es2_shell_flow_event(r->flow, ES2_FLOW_REGISTRATION_CANCEL, NULL);
}

/* Original: VSHELL.EXE 0x43c78b: the HERC_INF reload 0x411e40 (a read of
 * gam\herc_inf.dat: the host's), hide 0x43c349, then the flow's ACCEPT:
 * 0x477946 := 0 and 0x40e8a5(text +0x45, skill) through the host's new
 * game, the page word 0x481108, the bay. */
static void on_accept(es2_ui *ui, es2_ui_id window, const es2_gui_event *ev) {
    es2_screen_registration *r = of(ui, window);
    (void)ev;
    if (!r) return;
    es2_screen_registration_hide(r);
    (void)es2_shell_flow_event(r->flow, ES2_FLOW_REGISTRATION_ACCEPT, es2_screen_registration_name(r));
}

/* ---- resolver ------------------------------------------------------------ */
static const es2_dfn *resolve_font(void *context, uint32_t global) {
    const es2_screen_registration *r = context;
    return global == 0x46ddfc ? r->font_button : global == 0x46de00 ? r->font_label : NULL;
}
static const es2_bitmap *resolve_bitmap(void *context, const es2_ui_layout_ref *ref) {
    const es2_screen_registration *r = context;
    return ref->kind == ES2_UI_REF_GLOBAL && ref->a == 0x46de0c ? r->background : NULL;
}
static const char *resolve_text(void *context, const es2_ui_layout_ref *ref) {
    const es2_screen_registration *r = context;
    switch (ref->kind) {
    case ES2_UI_REF_ESTEXT: return estext(r, ref->a);
    case ES2_UI_REF_EXE_STRING:
        /* 0x476203: one space (the field frame's label); 0x476245: empty */
        return ref->a == 0x476203 ? " " : "";
    default: return NULL;
    }
}
static es2_ui_handler resolve_handler(void *context, uint32_t address) {
    (void)context;
    switch (address) {
    case 0x43c47e: return on_name;
    case 0x43c6ad: return on_skill;
    case 0x43c728: return on_cancel;
    case 0x43c78b: return on_accept;
    default: return NULL;
    }
}

int es2_screen_registration_build(es2_ui *ui, void *context) {
    es2_screen_registration *r = context;
    es2_ui_resolver resolver;
    int built;
    if (!ui || !r || !r->flow) return -1;
    r->ui = ui;
    resolver.context = r;
    resolver.font = resolve_font;
    resolver.bitmap = resolve_bitmap;
    resolver.text = resolve_text;
    resolver.handler = resolve_handler;
    built = es2_ui_layout_build(ui, ES2_UI_SCREEN_REGISTRATION, &resolver, r->ids, ES2_SCREEN_REGISTRATION_CONTROLS);
    if (built <= 0) return -1;
    for (int k = 0; k < ES2_SCREEN_REGISTRATION_CONTROLS; k++) {
        es2_ui_widget *w = es2_ui_get(ui, r->ids[k]);
        if (w) w->user = r;
    }
    /* 0x43c0fe: the skill label right after [0x48d58c]'s fields; ACCEPT's
     * label colour 38 and +0x49 := 0 (0x43c278..0x43c289) are table fields */
    skill_label(r);
    return 0;
}

/* Original: VSHELL.EXE 0x469ffc(queue, window): 0x46a0b4 (leave) then
 * 0x46a15c(window) (enter), as the widget toolkit models both: no change while
 * the capture flag is set. */
static void hover_to(es2_ui *ui, es2_ui_id window) {
    es2_gui_event ev = {0};
    ev.x = ui->cursor_x;
    ev.y = ui->cursor_y;
    if (!ui->capture && ui->hover) {
        ev.type = ES2_GUI_EV_LEAVE;
        ev.target = ui->hover;
        es2_ui_on_event(ui, &ev);
        ui->hover = 0;
        ui->hover_display = 0;
    }
    if (ui->capture || !window || window == ui->hover) return;
    ui->hover = window;
    ui->hover_display = 1;
    ev.type = ES2_GUI_EV_ENTER;
    ev.target = window;
    es2_ui_on_event(ui, &ev);
}

void es2_screen_registration_show(es2_screen_registration *r) {
    es2_gui_event press = {0};
    es2_ui *ui;
    if (!r || !r->ui) return;
    ui = r->ui;
    es2_ui_show(ui, r->ids[ES2_REGISTRATION_BACKGROUND]);
    es2_ui_show(ui, r->ids[ES2_REGISTRATION_PANEL]);
    es2_ui_show(ui, r->ids[ES2_REGISTRATION_BODY]);
    /* 0x43c2ce..0x43c318: a mouse event (0x46903c: type 0x20, +0x25 := 0)
     * with code 2 (left press) and target [0x48d588], posted */
    press.type = ES2_GUI_EV_MOUSE;
    press.code = ES2_GUI_MOUSE_LEFT_DOWN;
    press.target = r->ids[ES2_REGISTRATION_FIELD];
    (void)es2_gui_post(&ui->shell->input, press);
    hover_to(ui, r->ids[ES2_REGISTRATION_FIELD]);   /* 0x43c32a */
    ui->capture = 1;                                /* 0x43c338 */
}

void es2_screen_registration_hide(es2_screen_registration *r) {
    if (!r || !r->ui) return;
    es2_ui_hide(r->ui, r->ids[ES2_REGISTRATION_BACKGROUND]);
    es2_ui_hide(r->ui, r->ids[ES2_REGISTRATION_PANEL]);
    es2_ui_hide(r->ui, r->ids[ES2_REGISTRATION_BODY]);
}

const char *es2_screen_registration_name(const es2_screen_registration *r) {
    const es2_ui_widget *w = r && r->ui ? es2_ui_get(r->ui, r->ids[ES2_REGISTRATION_FIELD]) : NULL;
    return w ? w->edit.text : "";
}
