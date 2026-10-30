/* The modal boxes and dialogs of VSHELL over the flow and the widget
 * toolkit. Contract: docs/reverse-engineering/vshell_ui.md, section 22. */
#include "es2/shell/gui/boxes.h"

#include <stddef.h>
#include <string.h>

/* Fonts, bitmaps, texts and handlers for the layout builder. */
static const es2_dfn *r_font(void *context, uint32_t global) {
    es2_boxes *b = context;
    return b->host.font ? b->host.font(b->host.ctx, global) : NULL;
}
static const es2_bitmap *r_bitmap(void *context, const es2_ui_layout_ref *ref) {
    es2_boxes *b = context;
    if (ref->kind != ES2_UI_REF_GLOBAL || !b->host.bitmap) return NULL;
    return b->host.bitmap(b->host.ctx, (uint32_t)ref->a);
}
static const char *r_text(void *context, const es2_ui_layout_ref *ref) {
    es2_boxes *b = context;
    if (ref->kind == ES2_UI_REF_ESTEXT) return b->host.estext ? b->host.estext(b->host.ctx, ref->a) : NULL;
    if (ref->kind == ES2_UI_REF_EXE_STRING) return b->host.literal ? b->host.literal(b->host.ctx, (uint32_t)ref->a) : NULL;
    return NULL;
}
static es2_ui_handler r_handler(void *context, uint32_t address) {
    (void)context;
    return es2_boxes_handler(address);
}

static const char *estext(const es2_boxes *b, int id) {
    return b->host.estext ? b->host.estext(b->host.ctx, id) : NULL;
}
static es2_gui_shell *shell_of(const es2_boxes *b) { return b->ui ? b->ui->shell : NULL; }
static es2_boxes *boxes_of(es2_ui *ui, es2_ui_id window) {
    es2_ui_widget *w = es2_ui_get(ui, window);
    return w ? w->user : NULL;
}

/* ESMessage setText 0x40b8bb(label, text, 2, 0x29, 1), the form every box
 * show function uses. */
static void set_line(es2_boxes *b, es2_ui_id label, int id) {
    (void)es2_ui_message_set(b->ui, label, estext(b, id), 2, 0x29, 1);
}

static int build(es2_ui *ui, void *context, int screen, es2_ui_id *ids, int n) {
    es2_boxes *b = context;
    es2_ui_resolver r = {b, r_font, r_bitmap, r_text, r_handler};
    if (!ui || !b || b->ui != ui) return -1;
    if (es2_ui_layout_build(ui, screen, &r, ids, n) < 0) return -1;
    for (int k = 0; k < n; k++) {
        es2_ui_widget *w = es2_ui_get(ui, ids[k]);
        if (!w) return -1;
        w->user = b;
    }
    return 0;
}

/* Original: VSHELL.EXE 0x431fa8 */
int es2_screen_quit_box_build(es2_ui *ui, void *context) {
    es2_boxes *b = context;
    if (build(ui, context, ES2_UI_SCREEN_QUIT_BOX, b ? b->quit : NULL, ES2_BOXES_QUIT_N)) return -1;
    ui->quit_box = b->quit[1]; /* [0x48d1dc] */
    return 0;
}
/* Original: VSHELL.EXE 0x44d2bc */
int es2_screen_end_of_game_box_build(es2_ui *ui, void *context) {
    es2_boxes *b = context;
    return build(ui, context, ES2_UI_SCREEN_END_OF_GAME_BOX, b ? b->end : NULL, ES2_BOXES_END_N);
}
/* Original: VSHELL.EXE 0x44d66c */
int es2_screen_launch_warning_box_build(es2_ui *ui, void *context) {
    es2_boxes *b = context;
    return build(ui, context, ES2_UI_SCREEN_LAUNCH_WARNING_BOX, b ? b->warning : NULL, ES2_BOXES_WARNING_N);
}
/* Original: VSHELL.EXE 0x44cdac */
int es2_screen_replay_box_build(es2_ui *ui, void *context) {
    es2_boxes *b = context;
    return build(ui, context, ES2_UI_SCREEN_REPLAY_BOX, b ? b->replay : NULL, ES2_BOXES_REPLAY_N);
}
/* Original: VSHELL.EXE 0x4322a8 (its modal show 0x4323c9 and the shield's
 * hide 0x4324e6 leave the state the table records) */
int es2_screen_cd_prompt_build(es2_ui *ui, void *context) {
    es2_boxes *b = context;
    return build(ui, context, ES2_UI_SCREEN_CD_PROMPT, b ? b->cd : NULL, ES2_BOXES_CD_N);
}
/* Original: VSHELL.EXE 0x44dd38 */
int es2_screen_debug_dialog_build(es2_ui *ui, void *context) {
    es2_boxes *b = context;
    return build(ui, context, ES2_UI_SCREEN_DEBUG_DIALOG, b ? b->debug : NULL, ES2_BOXES_DEBUG_N);
}

void es2_boxes_init(es2_boxes *boxes, es2_ui *ui, es2_shell_flow *flow, const es2_boxes_host *host, int cheat) {
    if (!boxes) return;
    memset(boxes, 0, sizeof *boxes);
    boxes->ui = ui;
    boxes->flow = flow;
    if (host) boxes->host = *host;
    boxes->cheat = cheat != 0;
}

int es2_boxes_build(es2_boxes *b) {
    /* the screen objects 0x401812..0x40187b in their order (vshell_widgets.md 4.3) */
    static const int order[] = {ES2_UI_SCREEN_DEBUG_DIALOG, ES2_UI_SCREEN_CD_PROMPT, ES2_UI_SCREEN_QUIT_BOX,
                                ES2_UI_SCREEN_END_OF_GAME_BOX, ES2_UI_SCREEN_LAUNCH_WARNING_BOX,
                                ES2_UI_SCREEN_REPLAY_BOX};
    if (!b || !b->ui) return -1;
    for (size_t i = 0; i < sizeof order / sizeof order[0]; i++) {
        int rc;
        switch (order[i]) {
        case ES2_UI_SCREEN_DEBUG_DIALOG: rc = es2_screen_debug_dialog_build(b->ui, b); break;
        case ES2_UI_SCREEN_CD_PROMPT: rc = es2_screen_cd_prompt_build(b->ui, b); break;
        case ES2_UI_SCREEN_QUIT_BOX: rc = es2_screen_quit_box_build(b->ui, b); break;
        case ES2_UI_SCREEN_END_OF_GAME_BOX: rc = es2_screen_end_of_game_box_build(b->ui, b); break;
        case ES2_UI_SCREEN_LAUNCH_WARNING_BOX: rc = es2_screen_launch_warning_box_build(b->ui, b); break;
        default: rc = es2_screen_replay_box_build(b->ui, b); break;
        }
        if (rc) return -1;
    }
    return 0;
}

/* ---- show functions ------------------------------------------------------ */

/* Original: VSHELL.EXE 0x44d55f: 0 -> 0x130, 1 -> 0x131, 3 -> 0x12f, any
 * other word keeps the label's text; then the modal show of [0x48dcb8]. */
void es2_boxes_end_show(es2_boxes *b, int16_t word) {
    if (!b || !b->end[1]) return;
    if (word == 0) set_line(b, b->end[3], 0x130);
    else if (word == 1) set_line(b, b->end[3], 0x131);
    else if (word == 3) set_line(b, b->end[3], 0x12f);
    es2_ui_modal_show(b->ui, b->end[1]);
}

/* Original: VSHELL.EXE 0x44d90c: n = 0..3 -> lines 0x13c + 2n, 0x13d + 2n;
 * other values keep both; then the modal show of [0x48dccc]. */
void es2_boxes_warning_show(es2_boxes *b, int16_t n) {
    if (!b || !b->warning[1]) return;
    if (n >= 0 && n <= 3) {
        set_line(b, b->warning[3], 0x13c + 2 * n);
        set_line(b, b->warning[4], 0x13d + 2 * n);
    }
    es2_ui_modal_show(b->ui, b->warning[1]);
}

/* Original: VSHELL.EXE 0x44d0e7: 0 -> 0x137/0x138, 3 -> 0x135/0x136, other
 * values keep both; then show 0x41f93a of the bitmap [0x48dc9c] and of the
 * panel [0x48dca0] (no shield: an ordinary screen on the root). */
void es2_boxes_replay_show(es2_boxes *b, int16_t kind) {
    if (!b || !b->replay[0]) return;
    if (kind == 0) {
        set_line(b, b->replay[3], 0x137);
        set_line(b, b->replay[4], 0x138);
    } else if (kind == 3) {
        set_line(b, b->replay[3], 0x135);
        set_line(b, b->replay[4], 0x136);
    }
    es2_ui_show(b->ui, b->replay[0]);
    es2_ui_show(b->ui, b->replay[1]);
}

/* Original: VSHELL.EXE 0x44d1b9: hide the bitmap, then the panel. */
void es2_boxes_replay_hide(es2_boxes *b) {
    if (!b || !b->replay[0]) return;
    es2_ui_hide(b->ui, b->replay[0]);
    es2_ui_hide(b->ui, b->replay[1]);
}

/* Original: VSHELL.EXE 0x413305 with 0x44e1b5 */
void es2_boxes_debug_show(es2_boxes *b, const char *default_name) {
    es2_gui_shell *shell = b ? shell_of(b) : NULL;
    if (!b || !b->debug[0] || !shell) return;
    (void)es2_ui_message_set(b->ui, b->debug[8], default_name, 2, 0x29, 1); /* [0x48dcec] */
    es2_ui_show(b->ui, b->debug[0]);
    es2_ui_show(b->ui, b->debug[1]);
    if (!b->cheat) /* 0x41335d..0x4133d0: left down, left up on [0x48dcfc] */
        es2_gui_post_synthetic_click(&shell->input, b->debug[9]);
}

/* Original: VSHELL.EXE 0x44e217 */
void es2_boxes_debug_hide(es2_boxes *b) {
    if (!b || !b->debug[0]) return;
    es2_ui_hide(b->ui, b->debug[0]);
    es2_ui_hide(b->ui, b->debug[1]);
}

void es2_boxes_sync(es2_boxes *b) {
    if (!b || !b->flow || b->flow->box == b->shown) return;
    switch (b->flow->box) {
    case ES2_FLOW_BOX_END_OF_GAME: es2_boxes_end_show(b, (int16_t)b->flow->box_value); break;
    case ES2_FLOW_BOX_WARNING: es2_boxes_warning_show(b, (int16_t)b->flow->box_value); break;
    case ES2_FLOW_BOX_REPLAY: es2_boxes_replay_show(b, (int16_t)b->flow->box_value); break;
    default: break; /* none, or the Alert box (its radio's handler shows it) */
    }
    b->shown = b->flow->box;
}

/* ---- handlers ------------------------------------------------------------ */

static void flow_event(es2_boxes *b, es2_shell_flow_handler event) {
    if (b->flow) (void)es2_shell_flow_event(b->flow, event, NULL);
}

/* Original: VSHELL.EXE 0x4321c1: the modal hide of [0x48d1dc] only. */
static void quit_cancel(es2_ui *ui, es2_ui_id window, const es2_gui_event *ev) {
    es2_boxes *b = boxes_of(ui, window);
    (void)ev;
    if (b) es2_ui_modal_hide(ui, b->quit[1]);
}

/* Original: VSHELL.EXE 0x432237: the quit flag only (the box stays shown;
 * the loop ends at its next quit test). */
static void quit_accept(es2_ui *ui, es2_ui_id window, const es2_gui_event *ev) {
    es2_boxes *b = boxes_of(ui, window);
    (void)ev;
    if (!b) return;
    ui->shell->quit = 1;
    flow_event(b, ES2_FLOW_QUIT_ACCEPT);
}

/* Original: VSHELL.EXE 0x4324fa: hide 0x41fabd of the shield [0x48d1e8],
 * the retry flag [0x473bcc] := 1. */
static void cd_continue(es2_ui *ui, es2_ui_id window, const es2_gui_event *ev) {
    es2_boxes *b = boxes_of(ui, window);
    (void)ev;
    if (!b) return;
    es2_ui_hide(ui, b->cd[0]);
    b->cd_retry = 1;
}

/* Original: VSHELL.EXE 0x432569: the quit flag only (the exit code stays). */
static void cd_quit(es2_ui *ui, es2_ui_id window, const es2_gui_event *ev) {
    es2_boxes *b = boxes_of(ui, window);
    (void)ev;
    if (!b) return;
    ui->shell->quit = 1;
    if (b->flow) b->flow->quit = 1;
}

/* Original: VSHELL.EXE 0x44d60b -> 0x44d5fe: the modal hide of [0x48dcb8]. */
static void end_okay(es2_ui *ui, es2_ui_id window, const es2_gui_event *ev) {
    es2_boxes *b = boxes_of(ui, window);
    (void)ev;
    if (!b) return;
    es2_ui_modal_hide(ui, b->end[1]);
    flow_event(b, ES2_FLOW_END_OKAY);
    if (b->shown == ES2_FLOW_BOX_END_OF_GAME) b->shown = ES2_FLOW_BOX_NONE;
}

/* Original: VSHELL.EXE 0x44da94 -> 0x44da87: the modal hide of [0x48dccc]. */
static void warning_okay(es2_ui *ui, es2_ui_id window, const es2_gui_event *ev) {
    es2_boxes *b = boxes_of(ui, window);
    (void)ev;
    if (!b) return;
    es2_ui_modal_hide(ui, b->warning[1]);
    flow_event(b, ES2_FLOW_WARNING_OKAY);
    if (b->shown == ES2_FLOW_BOX_WARNING) b->shown = ES2_FLOW_BOX_NONE;
}

/* Original: VSHELL.EXE 0x44d1d4: hide (0x44d1b9), then the load 0x40eaaa(10,
 * 0), exit code 2, quit flag (the flow's REPLAY_YES). */
static void replay_yes(es2_ui *ui, es2_ui_id window, const es2_gui_event *ev) {
    es2_boxes *b = boxes_of(ui, window);
    (void)ev;
    if (!b) return;
    es2_boxes_replay_hide(b);
    flow_event(b, ES2_FLOW_REPLAY_YES);
    if (b->shown == ES2_FLOW_BOX_REPLAY) b->shown = ES2_FLOW_BOX_NONE;
    if (b->flow && b->flow->quit) ui->shell->quit = 1;
}

/* Original: VSHELL.EXE 0x44d24d: the save 0x40e933(10, 0), hide (0x44d1b9),
 * the main menu 0x431718. */
static void replay_no(es2_ui *ui, es2_ui_id window, const es2_gui_event *ev) {
    es2_boxes *b = boxes_of(ui, window);
    (void)ev;
    if (!b) return;
    flow_event(b, ES2_FLOW_REPLAY_NO);
    es2_boxes_replay_hide(b);
    if (b->shown == ES2_FLOW_BOX_REPLAY) b->shown = ES2_FLOW_BOX_NONE;
    if (b->host.screen && b->flow) b->host.screen(b->host.ctx, b->flow);
}

/* Original: VSHELL.EXE 0x44dbea: hide (0x44e217), then Use Default 0x44db5c. */
static void debug_default(es2_ui *ui, es2_ui_id window, const es2_gui_event *ev) {
    es2_boxes *b = boxes_of(ui, window);
    (void)ev;
    if (!b) return;
    es2_boxes_debug_hide(b);
    if (b->host.debug_default) b->host.debug_default(b->host.ctx);
}

/* Original: VSHELL.EXE 0x44dc4d: hide (0x44e217), then the preparation of
 * msn\<field text>.msn (the field is [0x48dcf8] +0x45). */
static void debug_accept(es2_ui *ui, es2_ui_id window, const es2_gui_event *ev) {
    es2_boxes *b = boxes_of(ui, window);
    es2_ui_widget *field;
    char text[ES2_SHELL_EDIT_TEXT];
    (void)ev;
    if (!b) return;
    field = es2_ui_get(ui, b->debug[6]);
    memcpy(text, field ? field->edit.text : "", field ? sizeof text : 1);
    text[sizeof text - 1] = 0;
    es2_boxes_debug_hide(b);
    if (b->host.debug_accept) b->host.debug_accept(b->host.ctx, text);
}

static es2_ui_id prefs_control(es2_ui *ui, int index) {
    const es2_ui_layout_screen *s = es2_ui_layout_screen_of(ES2_UI_SCREEN_PREFERENCES);
    return s && index < s->count ? ui->layout_ids[s->first + index] : 0;
}

/* Original: VSHELL.EXE 0x437608: only while windowed ([0x481f4c] = 0), show
 * 0x41f93a of the Alert box's shield [0x48d3bc] (preferences.01). */
static void prefs_full_screen(es2_ui *ui, es2_ui_id window, const es2_gui_event *ev) {
    es2_boxes *b = boxes_of(ui, window);
    (void)ev;
    if (!b || ui->shell->fullscreen) return;
    es2_ui_show(ui, prefs_control(ui, 1));
    if (b->flow) (void)es2_shell_flow_prefs_control(b->flow, ES2_SHELL_PREFS_FULL_SCREEN);
}

/* Original: VSHELL.EXE 0x437678: hide the shield, the display toggle
 * 0x407636, 0x437200(1) (byte 6 := 1 in the current block, then the
 * Preferences refresh 0x436e5d). */
static void alert_accept(es2_ui *ui, es2_ui_id window, const es2_gui_event *ev) {
    es2_boxes *b = boxes_of(ui, window);
    es2_gui_shell *shell = ui->shell;
    (void)ev;
    if (!b) return;
    es2_ui_hide(ui, prefs_control(ui, 1));
    shell->fullscreen = !shell->fullscreen;
    if (shell->platform.fullscreen) shell->platform.fullscreen(shell->platform.context, shell->fullscreen);
    flow_event(b, ES2_FLOW_ALERT_ACCEPT);
    if (b->host.prefs_refresh) b->host.prefs_refresh(b->host.ctx);
}

es2_ui_handler es2_boxes_handler(uint32_t address) {
    switch (address) {
    case ES2_BOXES_QUIT_CANCEL: return quit_cancel;
    case ES2_BOXES_QUIT_ACCEPT: return quit_accept;
    case ES2_BOXES_CD_CONTINUE: return cd_continue;
    case ES2_BOXES_CD_QUIT: return cd_quit;
    case ES2_BOXES_END_OKAY: return end_okay;
    case ES2_BOXES_WARNING_OKAY: return warning_okay;
    case ES2_BOXES_REPLAY_YES: return replay_yes;
    case ES2_BOXES_REPLAY_NO: return replay_no;
    case ES2_BOXES_DEBUG_DEFAULT: return debug_default;
    case ES2_BOXES_DEBUG_ACCEPT: return debug_accept;
    case ES2_BOXES_PREFS_FULL_SCREEN: return prefs_full_screen;
    case ES2_BOXES_ALERT_ACCEPT: return alert_accept;
    default: return NULL;
    }
}

int es2_boxes_bind_preferences(es2_boxes *b) {
    es2_ui_widget *radio, *accept;
    if (!b || !b->ui) return -1;
    radio = es2_ui_get(b->ui, prefs_control(b->ui, 22));
    accept = es2_ui_get(b->ui, prefs_control(b->ui, 5));
    if (!radio || !accept || !prefs_control(b->ui, 1)) return -1;
    radio->user = b;
    accept->user = b;
    return 0;
}

/* ---- CD prompt and the drain helper -------------------------------------- */

/* Original: VSHELL.EXE 0x402266 */
void es2_boxes_drain_helper(es2_gui_shell *shell) {
    if (!shell) return;
    for (int round = 24; round > 0 && !shell->quit; round--) {
        if (shell->platform.pump && shell->platform.pump(shell->platform.context, shell)) return; /* WM_QUIT */
        if (!shell->focused) continue;
        es2_gui_poll(&shell->input);                                             /* 0x41e0bf */
        if (shell->hooks.timer) shell->hooks.timer(shell->hooks.context, shell); /* 0x46848c */
        es2_gui_drain(&shell->input, shell->hooks.handle, shell->hooks.context);
        es2_gui_palette_apply(&shell->frame);                                    /* 0x407c1b */
        int recoloured = es2_gui_recolour(&shell->screen, &shell->frame);
        if (shell->frame.dirty_count) {                                          /* 0x41fe49 */
            es2_gui_rect rects[ES2_GUI_DIRTY_MAX];
            int n = shell->frame.dirty_count;
            memcpy(rects, shell->frame.dirty, (size_t)n * sizeof rects[0]);
            es2_gui_flush(&shell->screen, &shell->frame);
            shell->presents++;
            if (shell->platform.present)
                shell->platform.present(shell->platform.context, shell, ES2_GUI_PRESENT_DIRTY, rects, n);
        } else if (recoloured) {
            shell->presents++;
            if (shell->platform.present) shell->platform.present(shell->platform.context, shell, ES2_GUI_PRESENT_FULL, NULL, 0);
        }
    }
}

/* Original: VSHELL.EXE 0x41edda..0x41ee10 */
int es2_boxes_cd_prompt(es2_boxes *b) {
    es2_gui_shell *shell = b ? shell_of(b) : NULL;
    int retry;
    if (!shell || !b->cd[0]) return 0;
    es2_ui_show(b->ui, b->cd[0]); /* 0x41ede0: the shield; the box is its pending child */
    b->ui->gate = 0;              /* [0x470fb0] */
    while (!b->cd_retry && !shell->quit) es2_boxes_drain_helper(shell);
    retry = b->cd_retry != 0;
    b->cd_retry = 0;
    b->ui->gate = 1;
    return retry && !shell->quit;
}

/* ---- main loop and system hook ------------------------------------------ */

/* Original: VSHELL.EXE 0x402151..0x40217b */
void es2_boxes_pass(void *context, es2_gui_shell *shell) {
    es2_boxes *b = context;
    es2_ui_widget *box;
    (void)shell;
    if (!b || !(box = es2_ui_get(b->ui, b->quit[1])) || (box->flags & ES2_UI_HIDDEN)) return;
    if (++b->quit_passes > 50) {
        b->quit_passes = 0;
        es2_ui_redraw_tree(b->ui, b->quit[1]);
    }
}

/* Original: VSHELL.EXE 0x405283..0x40528f (Alt+F4) */
void es2_boxes_system(void *context, es2_gui_shell *shell, es2_gui_system_action action) {
    es2_boxes *b = context;
    (void)shell;
    if (b && action == ES2_GUI_SYS_QUIT_BOX) es2_ui_quit_box(b->ui);
}
