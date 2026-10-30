/* The CREW screen 0x441548 (es2/shell/gui/crew.h).
 *
 * Contract: docs/reverse-engineering/vshell_ui.md sections 17 and 32.
 * Original: VSHELL.EXE 0x441548..0x442bc3. The state changes
 * (pilot orders, bays, activity, the team count) are the editors library, run
 * by the host; this file keeps VSHELL's UI order around those calls and the
 * session fields the original keeps in its own globals. */
#include "es2/shell/gui/crew.h"

#include <string.h>

static const char c_pilots[] = "dba\\c_pilots.dba"; /* [0x4774cc], 0x477725 */

/* ---- helpers ------------------------------------------------------------------ */

static es2_crew *crew_of(es2_ui *ui, es2_ui_id window) {
    es2_ui_widget *w = es2_ui_get(ui, window);
    return w ? (es2_crew *)w->user : NULL;
}

static const char *text(const es2_crew *c, int32_t id) {
    const char *t = c->host.text ? c->host.text(c->host.ctx, (uint32_t)id) : NULL;
    return t ? t : "";
}

/* 0x4087f0([0x46ddf8], (int16_t)(base + word)): the IDs are word sums */
static const char *text_plus(const es2_crew *c, int base, int16_t word) {
    return text(c, (int16_t)(uint16_t)(base + word));
}

static void read_team(const es2_crew *c, es2_crew_team *t) {
    memset(t, 0, sizeof *t);
    t->player.bay = -1;
    t->player.order = -1;
    for (int g = 0; g < 3; g++) {
        t->group[g].bay = -1;
        t->group[g].order = -1;
    }
    for (int k = 0; k < 8; k++) t->vehicle[k] = -1;
    if (c->host.team && !c->host.team(c->host.ctx, t)) read_team(&(es2_crew){0}, t);
}

/* 0x4108fa(team, o): the first group whose selected pilot has order o */
static int find_order(const es2_crew_team *t, int16_t o) {
    for (int g = 0; g < 3; g++)
        if (t->group[g].order == o) return g;
    return -1;
}

/* 0x411164(hangar, k): -1 for k < 0 or no record, else the vehicle word */
static int16_t vehicle_of(const es2_crew_team *t, int16_t k) {
    return k >= 0 && k < 8 ? t->vehicle[k] : -1;
}

static es2_ui_widget *control(es2_crew *c, int index) { return es2_ui_get(c->ui, c->ids[index]); }

static void set_text(es2_crew *c, int index, const char *t) {
    (void)es2_ui_message_set(c->ui, c->ids[index], t, 0, 0x29, 1); /* 0x40b8bb(label, text, 0, 0x29, 1) */
}

/* +0x4d := colour, then 0x41f3ed */
static void frame_colour(es2_crew *c, int index, int32_t colour) {
    es2_ui_widget *w = control(c, index);
    if (!w) return;
    w->colour = colour;
    es2_ui_redraw_tree(c->ui, c->ids[index]);
}

/* +0x5d := bitmap, then the draw method (vtable +4) */
static void set_image(es2_crew *c, int index, const es2_bitmap *bitmap) {
    es2_ui_widget *w = control(c, index);
    if (!w) return;
    w->bitmap = bitmap;
    es2_ui_draw(c->ui, c->ids[index]);
}

static const es2_bitmap *portrait(const es2_crew *c, int16_t frame) {
    return c->host.bitmap ? c->host.bitmap(c->host.ctx, c_pilots, frame) : NULL;
}

/* The library on a copy of the session; 1 when it was applied. */
static int edit(es2_crew *c, es2_shell_editor_session *s, int kind, int16_t a) {
    es2_shell_editor_op op;
    if (!c->host.edit) return 0;
    op.kind = kind;
    op.a = a;
    op.b = 0;
    op.applied = 0;
    c->edits++;
    if (c->host.edit(c->host.ctx, s, &op) != ES2_OK) {
        c->edit_failures++;
        return 0;
    }
    return 1;
}

/* The library's session replaces the screen's; a difference in the fields
 * both keep is counted (tests). */
static void adopt(es2_crew *c, const es2_shell_editor_session *s) {
    es2_shell_editor_session *own = &c->bay->session;
    if (own->screen != s->screen || own->bay != s->bay || own->order != s->order ||
        memcmp(own->crew, s->crew, sizeof own->crew))
        c->mismatches++;
    *own = *s;
}

static void changed(es2_crew *c, uint32_t handler) {
    c->last_handler = handler;
    if (c->host.changed) c->host.changed(c->host.ctx, handler);
}

/* ---- the screen's routines -------------------------------------------------------- */

/* Original: VSHELL.EXE 0x441ee7: the colours of the four order rows by the
 * capacity (signed: order o < capacity); fields only, no redraw. */
static void colours(es2_crew *c, const es2_crew_team *t) {
    for (int o = 0; o < 4; o++) {
        int below = o < t->capacity;
        es2_ui_widget *w = control(c, ES2_CREW_ROW + o);
        if (w) w->f59 = below ? 0x25 : 0x10; /* ESArm +0x59 */
        for (int k = ES2_CREW_NAME_CAPTION; k <= ES2_CREW_VEHICLE_CAPTION; k += 4)
            if ((w = control(c, k + o))) w->text_colour = below ? 0x19 : 0x1a; /* +0xb5 */
        for (int k = ES2_CREW_NAME; k <= ES2_CREW_VEHICLE; k += 4)
            if ((w = control(c, k + o))) {
                w->text_colour = below ? 0x16 : 0x17; /* +0xb5 */
                w->background = below ? 0x25 : 0x10;  /* +0xc5 */
            }
    }
}

static void vehicle_text(es2_crew *c, int o, const es2_crew_team *t, int16_t bay) {
    int16_t v = vehicle_of(t, bay);
    set_text(c, ES2_CREW_VEHICLE + o, v == -1 ? text(c, 0x7e) : text_plus(c, 0x6e, v));
}

static void texts(es2_crew *c, const es2_crew_team *t) {
    const es2_shell_editor_session *s = &c->bay->session;
    /* the player's row (its image is dba\star.dba 0 from the constructor) */
    set_text(c, ES2_CREW_NAME, t->player.name ? t->player.name : "");
    set_text(c, ES2_CREW_SKILL, text_plus(c, 0x35, t->player.skill));
    vehicle_text(c, 0, t, t->player.bay);
    /* 0x44239b: the three order pilots [0x477778 + 4o] */
    for (int o = 1; o <= 3; o++) {
        int g = s->crew[o - 1];
        if (g < 0 || g > 2) {
            set_image(c, ES2_CREW_IMAGE + o, NULL);
            set_text(c, ES2_CREW_NAME + o, text(c, 0));
            set_text(c, ES2_CREW_SKILL + o, text(c, 0));
            set_text(c, ES2_CREW_VEHICLE + o, text(c, 0));
            es2_ui_redraw_tree(c->ui, c->ids[ES2_CREW_ROW + o]);
            continue;
        }
        set_image(c, ES2_CREW_IMAGE + o, portrait(c, t->group[g].portrait));
        set_text(c, ES2_CREW_NAME + o, t->group[g].name ? t->group[g].name : "");
        set_text(c, ES2_CREW_SKILL + o, text_plus(c, 0x35, t->group[g].skill));
        vehicle_text(c, o, t, t->group[g].bay);
    }
}

void es2_crew_texts(es2_crew *c) {
    es2_crew_team t;
    if (!c || !c->ui || !c->bay) return;
    read_team(c, &t);
    texts(c, &t);
}

/* Original: VSHELL.EXE 0x442215(o): the previous row and its image 0x21,
 * the new 0x29, the squad panel's selection of that order's bay (the
 * player's for 0, the order pilot's or -1) decided with the previous order,
 * the row redrawn, then the order word. */
static void order_row(es2_crew *c, int16_t o) {
    es2_service_bay *b = c->bay;
    es2_crew_team t;
    int16_t old = b->session.order, k;
    read_team(c, &t);
    if (old >= 0 && old <= 3) {
        frame_colour(c, ES2_CREW_ROW + old, 0x21);
        frame_colour(c, ES2_CREW_IMAGE + old, 0x21);
    }
    frame_colour(c, ES2_CREW_ROW + o, 0x29);
    frame_colour(c, ES2_CREW_IMAGE + o, 0x29);
    if (o == 0) {
        k = t.player.bay;
    } else {
        int g = b->session.crew[o - 1];
        k = g >= 0 && g <= 2 ? t.group[g].bay : -1;
    }
    es2_squad_panel_select(b, k, 0); /* 0x43dcdd without the selector flag */
    es2_ui_redraw_tree(c->ui, c->ids[ES2_CREW_ROW + o]);
    b->session.order = o;
}

void es2_crew_show(es2_crew *c) {
    es2_service_bay *b;
    es2_shell_editor_session lib;
    es2_crew_team t;
    int have;
    if (!c || !c->ui || !c->bay) return;
    b = c->bay;
    read_team(c, &t);
    /* the library's show (the OPEN of screen 6 after a bay screen: no
     * leave, the order pilots, the five order rows, the bays) */
    lib = b->session;
    lib.screen = ES2_SHELL_SCREEN_BAY;
    have = edit(c, &lib, ES2_SHELL_EDITOR_OPEN, ES2_SHELL_SCREEN_CREW);
    colours(c, &t);                                     /* 0x441ee7 */
    for (int16_t o = 1; o <= 3; o++)                    /* 0x442198 */
        b->session.crew[o - 1] = (int16_t)find_order(&t, o);
    for (int g = 0; g < 3; g++)                         /* 0x4420a3..0x442139 */
        set_image(c, ES2_CREW_GROUP + g, portrait(c, t.group[g].portrait));
    for (int16_t o = 0; o < 4; o++) order_row(c, o);    /* 0x44213a */
    order_row(c, 0);                                    /* 0x442149 */
    texts(c, &t);                                       /* 0x442151 */
    for (int16_t k = 0; k < 8; k++)                     /* 0x442156: every bay with a record */
        if (vehicle_of(&t, k) != -1) es2_squad_panel_select(b, k, 0);
    es2_ui_show(c->ui, c->ids[ES2_CREW_FRAME]);         /* 0x442181 */
    if (have) adopt(c, &lib);
    changed(c, ES2_CREW_SHOW);
}

void es2_crew_leave(es2_crew *c) {
    if (!c || !c->ui) return;
    es2_ui_hide(c->ui, c->ids[ES2_CREW_FRAME]); /* 0x44218a */
}

/* Original: VSHELL.EXE 0x4426e5 for the selection k (already stored):
 * nothing when a wingman order has no pilot; else the library's assignment
 * (the bay selector BAY op on the session before the selection), then the
 * texts 0x4422df. */
void es2_crew_selected(es2_crew *c, int16_t k) {
    es2_service_bay *b;
    int16_t o;
    if (!c || !c->ui || !c->bay) return;
    b = c->bay;
    o = b->session.order;
    if (o < 0 || o > 3) return;
    if (o != 0 && b->session.crew[o - 1] < 0) return; /* 0x4426f0..0x442702 */
    if (k >= 0 && k <= 7) {
        es2_shell_editor_session lib = b->session;
        lib.bay = -1; /* the selection before (any other bay: the rules are the panel's) */
        if (edit(c, &lib, ES2_SHELL_EDITOR_BAY, k)) adopt(c, &lib);
    }
    /* k = -1 with a pilot is not reachable: the flag is set only by the
     * eight selectors and by CLEAR, which empties the order first */
    es2_crew_texts(c);
}

/* Original: VSHELL.EXE 0x442548(g) */
static void pilot_list(es2_crew *c, int g) {
    es2_service_bay *b = c->bay;
    es2_shell_editor_session lib;
    int16_t o = b->session.order;
    int have;
    if (o < 1 || o > 3) return; /* order 0: nothing */
    lib = b->session;
    have = edit(c, &lib, ES2_SHELL_EDITOR_CREW_PILOT, (int16_t)g);
    for (int i = 0; i < 3; i++) /* the pointer bookkeeping (0x4425e2..0x442605) */
        if (b->session.crew[i] == g) b->session.crew[i] = -1;
    b->session.crew[o - 1] = (int16_t)g;
    if (have) adopt(c, &lib);
    es2_crew_texts(c);                         /* the end of 0x4426e5 */
    es2_squad_panel_pilots(b);                 /* 0x43e167 */
    es2_squad_panel_labels(b, b->session.bay); /* 0x43da0c */
    es2_crew_texts(c);                         /* 0x4422df */
}

/* Original: VSHELL.EXE 0x442624 */
static void clear(es2_crew *c) {
    es2_service_bay *b = c->bay;
    es2_shell_editor_session lib;
    es2_crew_team t;
    int16_t o = b->session.order;
    int have;
    if (o < 1 || o > 3) return;
    read_team(c, &t);
    if (find_order(&t, o) < 0) return;
    lib = b->session;
    have = edit(c, &lib, ES2_SHELL_EDITOR_CREW_CLEAR, 0); /* 0x40ec8f(g, -1), order -1 */
    b->session.crew[o - 1] = -1;                          /* 0x442682 */
    es2_crew_texts(c);                                    /* 0x442689 */
    es2_squad_panel_select(b, -1, 1);                     /* flag 0x47665a around 0x43dcdd(-1) */
    if (have) adopt(c, &lib);
}

int es2_crew_press(es2_crew *c, uint32_t handler) {
    static const uint32_t group[3] = {ES2_CREW_GROUP_0, ES2_CREW_GROUP_1, ES2_CREW_GROUP_2};
    static const uint32_t order[4] = {ES2_CREW_ORDER_0, ES2_CREW_ORDER_1, ES2_CREW_ORDER_2, ES2_CREW_ORDER_3};
    if (!c || !c->ui || !c->bay) return 0;
    for (int g = 0; g < 3; g++) {
        if (handler != group[g]) continue;
        /* 0x4427e1, 0x44288c, 0x442937: the image +0x4d 0x29, the other two 0x21 */
        frame_colour(c, ES2_CREW_GROUP + g, 0x29);
        for (int i = 0; i < 3; i++)
            if (i != g) frame_colour(c, ES2_CREW_GROUP + i, 0x21);
        pilot_list(c, g);
        changed(c, handler);
        return 1;
    }
    for (int16_t o = 0; o < 4; o++) {
        es2_shell_editor_session lib;
        int have;
        if (handler != order[o]) continue;
        lib = c->bay->session;
        have = edit(c, &lib, ES2_SHELL_EDITOR_CREW_ORDER, o);
        order_row(c, o);
        if (have) adopt(c, &lib);
        changed(c, handler);
        return 1;
    }
    if (handler == ES2_CREW_CLEAR_BUTTON) {
        clear(c);
        changed(c, handler);
        return 1;
    }
    return 0;
}

static void on_press(es2_ui *ui, es2_ui_id window, const es2_gui_event *ev) {
    es2_crew *c = crew_of(ui, window);
    es2_ui_widget *w = es2_ui_get(ui, window);
    (void)ev;
    if (c && w) (void)es2_crew_press(c, w->original_handler);
}

/* ---- resolvers and the build ---------------------------------------------------------- */

static const es2_dfn *resolve_font(void *context, uint32_t global) {
    es2_crew *c = context;
    return c->host.font ? c->host.font(c->host.ctx, global) : NULL;
}

static const es2_bitmap *resolve_bitmap(void *context, const es2_ui_layout_ref *ref) {
    es2_crew *c = context;
    if (!c->host.bitmap || ref->kind != ES2_UI_REF_DBA) return NULL;
    return c->host.bitmap(c->host.ctx, es2_ui_layout_file(ref->a), ref->b);
}

static const char *resolve_text(void *context, const es2_ui_layout_ref *ref) {
    es2_crew *c = context;
    switch (ref->kind) {
    case ES2_UI_REF_ESTEXT: return text(c, ref->a);
    case ES2_UI_REF_EXE_STRING: return " "; /* 0x477745, 0x477747, 0x477749: one blank each */
    default: return NULL;
    }
}

static es2_ui_handler resolve_handler(void *context, uint32_t address) {
    (void)context;
    switch (address) {
    case ES2_CREW_GROUP_0: case ES2_CREW_GROUP_1: case ES2_CREW_GROUP_2: case ES2_CREW_CLEAR_BUTTON:
    case ES2_CREW_ORDER_0: case ES2_CREW_ORDER_1: case ES2_CREW_ORDER_2: case ES2_CREW_ORDER_3:
        return on_press;
    default:
        return NULL;
    }
}

/* Original: VSHELL.EXE 0x441548 (the table is the layout table's; the DBAs
 * star.dba and c_pilots.dba are opened there, [0x4774c8] and [0x4774cc]) */
int es2_screen_crew_build(es2_ui *ui, void *ctx) {
    es2_crew *c = ctx;
    es2_ui_resolver r;
    if (!ui || !c || !c->bay) return -1;
    c->ui = ui;
    r.context = c;
    r.font = resolve_font;
    r.bitmap = resolve_bitmap;
    r.text = resolve_text;
    r.handler = resolve_handler;
    memset(c->ids, 0, sizeof c->ids);
    if (es2_ui_layout_build(ui, ES2_UI_SCREEN_CREW, &r, c->ids, ES2_SCREEN_CREW_CONTROLS) <= 0) return -1;
    for (int k = 0; k < ES2_SCREEN_CREW_CONTROLS; k++) {
        es2_ui_widget *w = es2_ui_get(ui, c->ids[k]);
        if (!w) return -1;
        w->user = c;
    }
    return 0;
}
