/* The main menu screen 0x430fc4 (es2/shell/gui/main_menu.h).
 *
 * Contract: docs/reverse-engineering/vshell_ui.md sections 4.1 and 23.
 * Original: VSHELL.EXE 0x430fc4..0x431fa4 and the entry
 * dispatch 0x40188a..0x4019c5. */
#include "es2/shell/gui/main_menu.h"

#include <string.h>

#include "es2/shell/draw.h"

/* ---- helpers ----------------------------------------------------------------- */

static es2_ui_id id_of(const es2_main_menu *m, int control) { return m->ids[control]; }

static es2_main_menu *menu_of(es2_ui *ui, es2_ui_id window) {
    es2_ui_widget *w = es2_ui_get(ui, window);
    return w ? (es2_main_menu *)w->user : NULL;
}

/* 0x4077ee: clear the frame and the screen. */
static void clear(es2_main_menu *m) {
    if (m->ui && m->ui->shell) es2_gui_shell_clear(m->ui->shell);
}

static int fullscreen(const es2_main_menu *m) { return m->ui && m->ui->shell && m->ui->shell->fullscreen; }

static void sound(es2_main_menu *m, uint32_t address) {
    if (m->host.sound) m->host.sound(m->host.ctx, address);
}

static int host_entry(es2_main_menu *m, uint32_t handler, int stage) {
    return m->host.entry ? m->host.entry(m->host.ctx, handler, stage) : 1;
}

static void play(es2_main_menu *m, int build_palette) {
    if (m->movies && m->host.play) m->host.play(m->host.ctx, m->movies, build_palette);
}

/* 0x407b62(1) + 0x407c1b: palette.dpl selected and applied. */
static void palette_main(es2_main_menu *m) {
    if (m->ui && m->ui->palette) m->ui->palette(m->ui, ES2_PALETTE_MAIN);
}

static es2_shell_flow_result flow_event(es2_main_menu *m, uint32_t handler) {
    es2_shell_flow_result r = m->flow ? es2_shell_flow_event(m->flow, (es2_shell_flow_handler)handler, NULL)
                                      : ES2_FLOW_IGNORED;
    m->last_handler = handler;
    m->last_result = r;
    m->events++;
    if (m->host.changed) m->host.changed(m->host.ctx, handler, r);
    return r;
}

/* ---- resolver for the layout table
 * ---------------------------------------------- */

static const es2_dfn *resolve_font(void *context, uint32_t global) {
    es2_main_menu *m = context;
    return m->host.font ? m->host.font(m->host.ctx, global) : NULL;
}

static const es2_bitmap *resolve_bitmap(void *context, const es2_ui_layout_ref *ref) {
    es2_main_menu *m = context;
    const char *file = NULL;
    if (!m->host.bitmap) return NULL;
    if (ref->kind == ES2_UI_REF_DBM) file = es2_ui_layout_file(ref->a);
    else if (ref->kind == ES2_UI_REF_GLOBAL && ref->a == 0x46de0c) file = es2_menu_background_file(4); /* bay2a_84 */
    return file ? m->host.bitmap(m->host.ctx, file) : NULL;
}

static const char *resolve_text(void *context, const es2_ui_layout_ref *ref) {
    es2_main_menu *m = context;
    if (ref->kind != ES2_UI_REF_ESTEXT || !m->host.text) return NULL;
    return m->host.text(m->host.ctx, (uint32_t)ref->a);
}

static void on_button(es2_ui *ui, es2_ui_id window, const es2_gui_event *ev);
static void on_animation(es2_ui *ui, es2_ui_id window, const es2_gui_event *ev);

static es2_ui_handler resolve_handler(void *context, uint32_t address) {
    (void)context;
    switch (address) {
    case ES2_MAIN_MENU_ANIMATION_STEP: return on_animation;
    case ES2_FLOW_INSTANT_ACTION: case ES2_FLOW_START_NEW_GAME: case ES2_FLOW_CONTINUE_GAME:
    case ES2_FLOW_SAVE_RESTORE: case ES2_FLOW_PREFERENCES_BUTTON: case ES2_FLOW_VIEW_DEMO:
    case ES2_FLOW_QUIT: case ES2_FLOW_PRACTICE_MISSIONS: case ES2_MAIN_MENU_CREDITS_HANDLER:
    case 0x431e1b: /* ONLINE MANUAL */
        return on_button;
    default: return NULL;
    }
}

/* Original: VSHELL.EXE 0x430fc4 (constructor; the table is the layout table's).
 * */
int es2_screen_main_menu_build(es2_ui *ui, void *ctx) {
    es2_main_menu *m = ctx;
    es2_ui_resolver r;
    int built;
    if (!ui || !m) return -1;
    m->ui = ui;
    r.context = m;
    r.font = resolve_font;
    r.bitmap = resolve_bitmap;
    r.text = resolve_text;
    r.handler = resolve_handler;
    memset(m->ids, 0, sizeof m->ids);
    built = es2_ui_layout_build(ui, ES2_UI_SCREEN_MAIN_MENU, &r, m->ids, ES2_MAIN_MENU_CONTROLS);
    if (built < 0) return -1;
    for (int k = 0; k < ES2_MAIN_MENU_CONTROLS; k++) {
        es2_ui_widget *w = es2_ui_get(ui, m->ids[k]);
        if (!w) return -1;
        w->user = m;
    }
    return 0;
}

/* ---- show, hide, start ------------------------------------------------------ */

/* Original: VSHELL.EXE 0x431718 */
void es2_main_menu_show(es2_main_menu *m) {
    es2_ui *ui = m->ui;
    es2_ui_id cont = id_of(m, ES2_MAIN_MENU_CONTINUE_GAME);
    es2_ui_widget *b = es2_ui_get(ui, cont), *label;
    es2_ui_show(ui, id_of(m, ES2_MAIN_MENU_BACKGROUND));
    es2_ui_show(ui, id_of(m, ES2_MAIN_MENU_PANEL));
    if (!b) return;
    label = es2_ui_get(ui, b->label);
    if (m->continue_flag) { /* byte [0x482afd] != 0 */
        if (label) label->text_colour = 0x29;
        b->colour = 0x22;
        es2_ui_redraw_tree(ui, cont);
        b->enabled = 1;
    } else {
        if (label) label->text_colour = 0x26;
        b->colour = 0x26;
        es2_ui_redraw_tree(ui, cont);
        b->enabled = 0;
    }
    es2_ui_draw(ui, cont); /* vtable +4 */
}

/* Original: VSHELL.EXE 0x4317c3 */
void es2_main_menu_hide(es2_main_menu *m) {
    es2_ui_hide(m->ui, id_of(m, ES2_MAIN_MENU_BACKGROUND));
    es2_ui_hide(m->ui, id_of(m, ES2_MAIN_MENU_PANEL));
}

/* Original: VSHELL.EXE 0x4018d0..0x4018f2 (-X6) and 0x40199a..0x4019bc
 * (after the intro): clear, gate := 1, palette 1, show [0x48d1a0]. */
void es2_main_menu_start(es2_main_menu *m) {
    clear(m);
    if (m->ui && m->ui->shell) m->ui->shell->frame.realize_gate = 1;
    palette_main(m);
    es2_ui_show(m->ui, id_of(m, ES2_MAIN_MENU_ANIMATION));
}

/* Original: VSHELL.EXE 0x40188a (entry dispatch) */
es2_shell_flow_entry es2_main_menu_enter(es2_main_menu *m, int state) {
    es2_shell_flow_entry entry = es2_shell_flow_entry_of(state);
    /* 0x401bed and again 0x4018a1: -X3/-X4 set the gate before slot 10 and the results path */
    if (entry == ES2_FLOW_ENTRY_RESULTS && m->ui && m->ui->shell) m->ui->shell->frame.realize_gate = 1;
    /* 0x401d37: the start-up 0x40796c(1) selects palette 1 and applies it
     * (0x4079f2..0x4079fa). With -X3/-X4 the gate is already set, so it
     * reaches the table: the killed path's Replay box (0x44d0e7) installs no
     * palette of its own. Otherwise it only clears the pending byte. */
    palette_main(m);
    if (entry == ES2_FLOW_ENTRY_INTRO) {
        /* 0x401955, 0x401983, 0x41e98c(0) at 0x40198f */
        if (m->movies && es2_shell_movies_intro(m->movies, (int16_t)state)) play(m, 0);
    } else if (entry == ES2_FLOW_ENTRY_MENU) {
        sound(m, ES2_MAIN_MENU_SOUND_FADE_IN); /* 0x4018c6 */
    }
    if (m->flow) {
        es2_shell_flow_start(m->flow, state);
        if (m->host.changed) m->host.changed(m->host.ctx, 0, ES2_FLOW_DONE);
    }
    if (entry != ES2_FLOW_ENTRY_RESULTS) {
        es2_main_menu_start(m);
        return entry;
    }
    if (!m->flow || m->flow->quit || m->flow->screen != ES2_FLOW_MAIN_MENU) return entry;
    switch (m->flow->outcome) {
    case ES2_FLOW_OUTCOME_WON:
        /* 0x40f58f..0x40f5e0: victory and credits, palette 1, the animation */
        if (m->movies) {
            es2_shell_movies_victory(m->movies);
            play(m, 1);
        }
        palette_main(m);
        es2_ui_show(m->ui, id_of(m, ES2_MAIN_MENU_ANIMATION));
        break;
    case ES2_FLOW_OUTCOME_REPLAY:
        break; /* the Replay box screen (0x44d0e7) */
    default:
        /* 0x40f662 (mode word not 1); the other main-menu ends are not
         * traced (vshell_ui.md section 23): the animation as well */
        es2_ui_show(m->ui, id_of(m, ES2_MAIN_MENU_ANIMATION));
        break;
    }
    return entry;
}

/* ---- the animation callback ------------------------------------------------- */

/* Original: VSHELL.EXE 0x431830 */
void es2_main_menu_animation_step(es2_main_menu *m) {
    es2_ui_widget *a = es2_ui_get(m->ui, id_of(m, ES2_MAIN_MENU_ANIMATION));
    if (m->menu_shown) return; /* [0x47374c] */
    if (!m->sound_played) {    /* [0x473750] */
        sound(m, ES2_MAIN_MENU_SOUND_SWITCH);
        m->sound_played = 1;
    }
    if (!a || a->frame_index != 5) return;
    m->menu_shown = 1;
    es2_ui_hide(m->ui, id_of(m, ES2_MAIN_MENU_ANIMATION));
    es2_main_menu_show(m);
    if (m->flow && m->flow->prefs.current[ES2_MAIN_MENU_PREFS_NOTICE] == 1) {
        /* 0x43189b..0x4318e6: the box, then byte 0x2f := 2 (0x40ddfe(0x2f, 2, 1)),
         * commit (0x40dd6e(0)) and the 54 bytes written (0x40dd0a) */
        if (m->host.notice) m->host.notice(m->host.ctx, 6, 2);
        m->flow->prefs.current[ES2_MAIN_MENU_PREFS_NOTICE] = 2;
        memcpy(m->flow->prefs.committed, m->flow->prefs.current, sizeof m->flow->prefs.committed);
        if (m->flow->host.write_prefs) (void)m->flow->host.write_prefs(m->flow->host.ctx, m->flow->prefs.current);
    }
}

static void on_animation(es2_ui *ui, es2_ui_id window, const es2_gui_event *ev) {
    es2_main_menu *m = menu_of(ui, window);
    (void)ev;
    if (m) es2_main_menu_animation_step(m);
}

/* ---- the ten buttons -------------------------------------------------------- */

/* Original: VSHELL.EXE 0x431c7b */
static void credits(es2_main_menu *m) {
    es2_ui *ui = m->ui;
    es2_gui_shell *sh = ui ? ui->shell : NULL;
    if (sh) sh->frame.realize_gate = 0;       /* 0x431cd8 */
    es2_ui_show(ui, id_of(m, ES2_MAIN_MENU_DISPLAY));
    clear(m);                                 /* 0x431cef */
    if (m->movies) {
        es2_shell_movies_credits(m->movies);  /* 0x431d1a: 0x54, full rectangle */
        play(m, 0);                           /* 0x431d24 */
    }
    if (sh) sh->frame.realize_gate = 1;       /* 0x431d2a */
    clear(m);                                 /* 0x431d34 */
    es2_ui_hide(ui, id_of(m, ES2_MAIN_MENU_DISPLAY));
    es2_ui_redraw_tree(ui, ui->root);         /* 0x431d50 */
}

es2_shell_flow_result es2_main_menu_press(es2_main_menu *m, uint32_t handler) {
    es2_shell_flow_result r;
    if (!m || !m->ui) return ES2_FLOW_IGNORED;
    switch (handler) {
    case ES2_FLOW_INSTANT_ACTION: /* 0x431935 */
        if (!host_entry(m, handler, 0)) return ES2_FLOW_REFUSED;
        r = flow_event(m, handler);
        if (fullscreen(m)) clear(m); /* 0x431966; windowed: the bay background flashes (0x431972, section 23) */
        (void)host_entry(m, handler, 1);
        return r;
    case ES2_FLOW_START_NEW_GAME: /* 0x431a08 */
        es2_main_menu_hide(m);
        return flow_event(m, handler);
    case ES2_FLOW_CONTINUE_GAME: /* 0x431a73 */
        es2_gui_set_busy(m->ui->shell, 1);
        r = flow_event(m, handler);
        if (m->flow && m->flow->screen == ES2_FLOW_SERVICE_BAY) es2_main_menu_hide(m); /* 0x431ab4 */
        es2_gui_set_busy(m->ui->shell, 0);
        return r;
    case ES2_FLOW_SAVE_RESTORE:       /* 0x431b27 */
    case ES2_FLOW_PREFERENCES_BUTTON: /* 0x431b9b */
        es2_main_menu_hide(m);
        return flow_event(m, handler);
    case ES2_FLOW_VIEW_DEMO: /* 0x431bfe */
        if (fullscreen(m)) clear(m);
        return flow_event(m, handler);
    case ES2_MAIN_MENU_CREDITS_HANDLER:
        credits(m);
        return ES2_FLOW_DONE;
    case ES2_FLOW_QUIT: /* 0x431db6: quit flag, then the clear */
        r = flow_event(m, handler);
        clear(m);
        return r;
    case 0x431e1b: /* ONLINE MANUAL -> 0x431e79: leave full screen (0x4077df), then the flow */
        if (fullscreen(m)) {
            es2_gui_shell *sh = m->ui->shell;
            sh->fullscreen = 0;
            if (sh->platform.fullscreen) sh->platform.fullscreen(sh->platform.context, 0);
        }
        return flow_event(m, handler);
    case ES2_FLOW_PRACTICE_MISSIONS: /* 0x431f3a: hide, the show 0x44c322, mode 0 */
        es2_main_menu_hide(m);
        if (!host_entry(m, handler, 0)) {
            es2_main_menu_show(m); /* own: a refused show leaves the menu, not an empty screen */
            return ES2_FLOW_REFUSED;
        }
        return flow_event(m, handler);
    default:
        return ES2_FLOW_IGNORED;
    }
}

static void on_button(es2_ui *ui, es2_ui_id window, const es2_gui_event *ev) {
    es2_main_menu *m = menu_of(ui, window);
    es2_ui_widget *w = es2_ui_get(ui, window);
    (void)ev;
    if (m && w) (void)es2_main_menu_press(m, w->original_handler);
}
