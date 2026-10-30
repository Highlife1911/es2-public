/* The Preferences screen 0x435598 (es2/shell/gui/preferences.h).
 *
 * Contract: docs/reverse-engineering/vshell_ui.md sections 4.5, 11.4, 22.2
 * and 27. Original: VSHELL.EXE 0x435598..0x4377b8. */
#include "es2/shell/gui/preferences.h"

#include <string.h>

#include "es2/shell/draw.h"

/* ---- helpers ----------------------------------------------------------------- */

static es2_ui_id id_of(const es2_preferences *p, int control) { return p->ids[control]; }

static es2_preferences *prefs_of(es2_ui *ui, es2_ui_id window) {
    es2_ui_widget *w = es2_ui_get(ui, window);
    return w ? (es2_preferences *)w->user : NULL;
}

static uint8_t byte_of(const es2_preferences *p, int index) {
    return p->flow ? p->flow->prefs.current[index] : 0;
}

/* The radio's +0x69 := value, then its draw (vtable +4). */
static void set_radio(es2_preferences *p, int control, int32_t value) {
    es2_ui_widget *w = es2_ui_get(p->ui, id_of(p, control));
    if (!w) return;
    w->checked = value;
    es2_ui_draw(p->ui, id_of(p, control));
}

/* A group of radios from a byte: value v checks the v-th, clears the
 * others; a value outside the group changes nothing. */
static void set_group(es2_preferences *p, const int *controls, int count, uint8_t value) {
    if (value >= count) return;
    for (int k = 0; k < count; k++) set_radio(p, controls[k], k == value);
}

static void music(es2_preferences *p, uint32_t address) {
    if (p->host.music && p->flow) p->host.music(p->host.ctx, address, p->flow->prefs.current);
}

/* 0x407636: the display toggle (the shell's word 0x481f4c and the mode). */
static void display_toggle(es2_preferences *p) {
    es2_gui_shell *sh = p->ui ? p->ui->shell : NULL;
    if (!sh) return;
    sh->fullscreen = !sh->fullscreen;
    if (sh->platform.fullscreen) sh->platform.fullscreen(sh->platform.context, sh->fullscreen);
}

static int full_screen(const es2_preferences *p) { return p->ui && p->ui->shell && p->ui->shell->fullscreen; }

static es2_shell_flow_result note(es2_preferences *p, uint32_t handler, es2_shell_flow_result r) {
    p->last_handler = handler;
    p->last_result = r;
    p->events++;
    if (p->host.changed) p->host.changed(p->host.ctx, handler, r);
    return r;
}

static es2_shell_flow_result control(es2_preferences *p, int which) {
    return p->flow ? es2_shell_flow_prefs_control(p->flow, which) : ES2_FLOW_IGNORED;
}

/* ---- the radio groups (0x436e20, 0x436fbe, 0x4370b8, 0x43716c, 0x436e5d) ------ */

/* Original: VSHELL.EXE 0x436e20: the two check boxes take bytes 0 and 1 as
 * they are (movzx, not a 0/1 test). */
static void refresh_audio(es2_preferences *p) {
    set_radio(p, ES2_PREFERENCES_MUSIC, byte_of(p, 0));
    set_radio(p, ES2_PREFERENCES_SFX, byte_of(p, 1));
}

/* Original: VSHELL.EXE 0x436fbe (byte +0x2c, cases 0, 1, 2) */
static void refresh_repair(es2_preferences *p) {
    static const int group[3] = {ES2_PREFERENCES_REPAIR_0, ES2_PREFERENCES_REPAIR_1, ES2_PREFERENCES_REPAIR_2};
    set_group(p, group, 3, byte_of(p, ES2_SHELL_PREFS_REPAIR));
}

/* Original: VSHELL.EXE 0x4370b8 (byte +4: 0 High Res, 1 Low Res) */
static void refresh_resolution(es2_preferences *p) {
    static const int group[2] = {ES2_PREFERENCES_HIGH_RES, ES2_PREFERENCES_LOW_RES};
    set_group(p, group, 2, byte_of(p, 4));
}

/* Original: VSHELL.EXE 0x43716c (byte +0x2d) */
static void refresh_build(es2_preferences *p) {
    static const int group[2] = {ES2_PREFERENCES_BUILD_0, ES2_PREFERENCES_BUILD_1};
    set_group(p, group, 2, byte_of(p, ES2_SHELL_PREFS_BUILD));
}

/* Original: VSHELL.EXE 0x436e5d (byte +6: 0 Window, 1 Full Screen) */
void es2_preferences_display_refresh(void *context) {
    static const int group[2] = {ES2_PREFERENCES_WINDOW, ES2_PREFERENCES_FULL_SCREEN};
    es2_preferences *p = context;
    if (p && p->ui) set_group(p, group, 2, byte_of(p, 6));
}

/* ---- resolver for the layout table
 * ---------------------------------------------- */

static const es2_dfn *resolve_font(void *context, uint32_t global) {
    es2_preferences *p = context;
    return p->host.font ? p->host.font(p->host.ctx, global) : NULL;
}

static const es2_bitmap *resolve_bitmap(void *context, const es2_ui_layout_ref *ref) {
    es2_preferences *p = context;
    if (!p->host.bitmap) return NULL;
    if (ref->kind == ES2_UI_REF_DBA) {
        const char *file = es2_ui_layout_file(ref->a);
        return file ? p->host.bitmap(p->host.ctx, file, ref->b) : NULL;
    }
    if (ref->kind == ES2_UI_REF_GLOBAL && ref->a == 0x46de0c) /* bay2a_84 */
        return p->host.bitmap(p->host.ctx, es2_menu_background_file(4), -1);
    return NULL;
}

static const char *resolve_text(void *context, const es2_ui_layout_ref *ref) {
    es2_preferences *p = context;
    if (ref->kind != ES2_UI_REF_ESTEXT || !p->host.text) return NULL; /* the radios' 0x474d4d is empty */
    return p->host.text(p->host.ctx, (uint32_t)ref->a);
}

static void on_control(es2_ui *ui, es2_ui_id window, const es2_gui_event *ev);

static es2_ui_handler resolve_handler(void *context, uint32_t address) {
    (void)context;
    switch (address) {
    case ES2_BOXES_PREFS_FULL_SCREEN: case ES2_BOXES_ALERT_ACCEPT: return es2_boxes_handler(address);
    case ES2_FLOW_PREFS_CANCEL: case ES2_FLOW_PREFS_ACCEPT:
    case ES2_PREFERENCES_ON_MUSIC: case ES2_PREFERENCES_ON_SFX:
    case ES2_PREFERENCES_ON_REPAIR_0: case ES2_PREFERENCES_ON_REPAIR_1: case ES2_PREFERENCES_ON_REPAIR_2:
    case ES2_PREFERENCES_ON_BUILD_0: case ES2_PREFERENCES_ON_BUILD_1: case ES2_PREFERENCES_ON_WINDOW:
    case ES2_PREFERENCES_ON_LOW_RES: case ES2_PREFERENCES_ON_HIGH_RES:
        return on_control;
    default: return NULL;
    }
}

void es2_preferences_init(es2_preferences *p, es2_shell_flow *flow, es2_main_menu *menu, es2_boxes *boxes,
                          const es2_preferences_host *host) {
    if (!p) return;
    memset(p, 0, sizeof *p);
    p->flow = flow;
    p->menu = menu;
    p->boxes = boxes;
    if (host) p->host = *host;
    p->shadow_repair = p->shadow_resolution = p->shadow_build = p->shadow_display = -1;
}

/* Original: VSHELL.EXE 0x435598 (constructor; the table is the layout table's,
 * with the modal show 0x435739 and the hide 0x43587b of the shield in the
 * table's after_construction states). */
int es2_screen_preferences_build(es2_ui *ui, void *ctx) {
    es2_preferences *p = ctx;
    es2_ui_resolver r;
    if (!ui || !p) return -1;
    p->ui = ui;
    r.context = p;
    r.font = resolve_font;
    r.bitmap = resolve_bitmap;
    r.text = resolve_text;
    r.handler = resolve_handler;
    memset(p->ids, 0, sizeof p->ids);
    if (es2_ui_layout_build(ui, ES2_UI_SCREEN_PREFERENCES, &r, p->ids, ES2_PREFERENCES_CONTROLS) < 0) return -1;
    for (int k = 0; k < ES2_PREFERENCES_CONTROLS; k++) {
        es2_ui_widget *w = es2_ui_get(ui, p->ids[k]);
        if (!w) return -1;
        w->user = p;
    }
    /* the boxes' two handlers read their `user` as the boxes */
    es2_ui_get(ui, id_of(p, ES2_PREFERENCES_FULL_SCREEN))->user = NULL;
    es2_ui_get(ui, id_of(p, ES2_PREFERENCES_ALERT_ACCEPT))->user = NULL;
    if (p->boxes && es2_boxes_bind_preferences(p->boxes)) return -1;
    return 0;
}

/* ---- show and hide ---------------------------------------------------------- */

/* Original: VSHELL.EXE 0x436d45: 0x436e20, 0x436fbe, 0x43716c, then the
 * setters 0x43712c(+0x2c), 0x4371e0(+0x2d), 0x43714c(+4), 0x437200(+6)
 * with the bytes' own values (each re-stores its byte, writes its shadow
 * and redraws its group), then show [0x48d384] and [0x48d388]. */
void es2_preferences_show(es2_preferences *p) {
    if (!p || !p->ui) return;
    refresh_audio(p);
    refresh_repair(p);
    refresh_build(p);
    p->shadow_repair = byte_of(p, ES2_SHELL_PREFS_REPAIR);
    refresh_repair(p);
    p->shadow_build = byte_of(p, ES2_SHELL_PREFS_BUILD);
    refresh_build(p);
    p->shadow_resolution = byte_of(p, 4);
    refresh_resolution(p);
    p->shadow_display = byte_of(p, 6);
    es2_preferences_display_refresh(p);
    es2_ui_show(p->ui, id_of(p, ES2_PREFERENCES_BACKGROUND));
    es2_ui_show(p->ui, id_of(p, ES2_PREFERENCES_PANEL));
}

/* Original: VSHELL.EXE 0x436da7 */
void es2_preferences_hide(es2_preferences *p) {
    if (!p || !p->ui) return;
    es2_ui_hide(p->ui, id_of(p, ES2_PREFERENCES_BACKGROUND));
    es2_ui_hide(p->ui, id_of(p, ES2_PREFERENCES_PANEL));
}

/* ---- handlers ----------------------------------------------------------------- */

/* Original: VSHELL.EXE 0x437220: revert (0x40ddb6(0)); the display back to
 * byte 6 (0x407636 when byte 6 is set while windowed or clear in full
 * screen); the music: byte 0 set -> ramp up 0x42f894, else byte 0 toggled,
 * ramp down 0x42f7f0, toggled back; hide 0x436da7; main menu 0x431718. */
static es2_shell_flow_result cancel(es2_preferences *p) {
    es2_shell_flow_result r = p->flow ? es2_shell_flow_event(p->flow, ES2_FLOW_PREFS_CANCEL, NULL) : ES2_FLOW_IGNORED;
    if (r == ES2_FLOW_DONE) {
        int display = byte_of(p, 6) != 0;
        if (display != full_screen(p)) display_toggle(p);
        if (byte_of(p, 0)) {
            music(p, ES2_PREFERENCES_MUSIC_IN);
        } else {
            p->flow->prefs.current[0] ^= 1; /* 0x40de35(0, 1) */
            music(p, ES2_PREFERENCES_MUSIC_OUT);
            p->flow->prefs.current[0] ^= 1;
        }
        es2_preferences_hide(p);
        if (p->menu) es2_main_menu_show(p->menu);
    }
    return note(p, ES2_FLOW_PREFS_CANCEL, r);
}

/* Original: VSHELL.EXE 0x4372e1: commit (0x40dd6e(0)), the writer 0x40dd0a
 * (both the flow's), hide 0x436da7, main menu 0x431718. */
static es2_shell_flow_result accept(es2_preferences *p) {
    es2_shell_flow_result r = p->flow ? es2_shell_flow_event(p->flow, ES2_FLOW_PREFS_ACCEPT, NULL) : ES2_FLOW_IGNORED;
    if (r == ES2_FLOW_DONE || r == ES2_FLOW_FAILED) { /* the hide and the menu follow the write whatever it did */
        es2_preferences_hide(p);
        if (p->menu) es2_main_menu_show(p->menu);
    }
    return note(p, ES2_FLOW_PREFS_ACCEPT, r);
}

/* Original: VSHELL.EXE 0x436ed1(0) (Music 0x437351): byte 0 set -> ramp down
 * 0x42f7f0, then toggled; clear -> toggled, then ramp up 0x42f894; then the
 * check boxes 0x436e20. */
static es2_shell_flow_result music_toggle(es2_preferences *p) {
    es2_shell_flow_result r;
    if (byte_of(p, 0)) {
        if (p->flow && (p->flow->screen != ES2_FLOW_PREFERENCES || p->flow->box != ES2_FLOW_BOX_NONE))
            return note(p, ES2_PREFERENCES_ON_MUSIC, ES2_FLOW_IGNORED);
        music(p, ES2_PREFERENCES_MUSIC_OUT);
        r = control(p, ES2_SHELL_PREFS_MUSIC_TOGGLE);
    } else {
        r = control(p, ES2_SHELL_PREFS_MUSIC_TOGGLE);
        if (r == ES2_FLOW_DONE) music(p, ES2_PREFERENCES_MUSIC_IN);
    }
    if (r == ES2_FLOW_DONE) refresh_audio(p);
    return note(p, ES2_PREFERENCES_ON_MUSIC, r);
}

/* Original: VSHELL.EXE 0x437597: leave full screen (0x407636) when the
 * display is full screen, then 0x437200(0): shadow 0x474d66, byte 6 := 0,
 * the display radios 0x436e5d. */
static es2_shell_flow_result window(es2_preferences *p) {
    es2_shell_flow_result r;
    if (!p->flow || p->flow->screen != ES2_FLOW_PREFERENCES || p->flow->box != ES2_FLOW_BOX_NONE)
        return note(p, ES2_PREFERENCES_ON_WINDOW, ES2_FLOW_IGNORED);
    if (full_screen(p)) display_toggle(p);
    p->shadow_display = 0;
    r = control(p, ES2_SHELL_PREFS_WINDOW);
    if (r == ES2_FLOW_DONE) es2_preferences_display_refresh(p);
    return note(p, ES2_PREFERENCES_ON_WINDOW, r);
}

/* A setter 0x43712c / 0x4371e0 / 0x43714c: the shadow, the byte, its group. */
static es2_shell_flow_result setter(es2_preferences *p, uint32_t handler, int which, int16_t *shadow, int16_t value,
                                    void (*refresh)(es2_preferences *)) {
    es2_shell_flow_result r = control(p, which);
    if (r == ES2_FLOW_DONE) {
        *shadow = value;
        refresh(p);
    }
    return note(p, handler, r);
}

es2_shell_flow_result es2_preferences_press(es2_preferences *p, uint32_t handler) {
    es2_shell_flow_result r;
    if (!p || !p->ui) return ES2_FLOW_IGNORED;
    switch (handler) {
    case ES2_FLOW_PREFS_CANCEL: return cancel(p);
    case ES2_FLOW_PREFS_ACCEPT: return accept(p);
    case ES2_PREFERENCES_ON_MUSIC: return music_toggle(p);
    case ES2_PREFERENCES_ON_SFX: /* 0x4373b2 -> 0x436ed1(1): byte 1 toggled, the check boxes */
        r = control(p, ES2_SHELL_PREFS_SFX_TOGGLE);
        if (r == ES2_FLOW_DONE) refresh_audio(p);
        return note(p, handler, r);
    case ES2_PREFERENCES_ON_REPAIR_0: /* 0x437413 -> 0x43712c(0) */
        return setter(p, handler, ES2_SHELL_PREFS_REPAIR_0, &p->shadow_repair, 0, refresh_repair);
    case ES2_PREFERENCES_ON_REPAIR_1: /* 0x437474 -> 0x43712c(1) */
        return setter(p, handler, ES2_SHELL_PREFS_REPAIR_1, &p->shadow_repair, 1, refresh_repair);
    case ES2_PREFERENCES_ON_REPAIR_2: /* 0x4374d5 -> 0x43712c(2) */
        return setter(p, handler, ES2_SHELL_PREFS_REPAIR_2, &p->shadow_repair, 2, refresh_repair);
    case ES2_PREFERENCES_ON_BUILD_0: /* 0x437536 -> 0x4371e0(0) */
        return setter(p, handler, ES2_SHELL_PREFS_BUILD_0, &p->shadow_build, 0, refresh_build);
    case ES2_PREFERENCES_ON_BUILD_1: /* 0x437696 -> 0x4371e0(1) */
        return setter(p, handler, ES2_SHELL_PREFS_BUILD_1, &p->shadow_build, 1, refresh_build);
    case ES2_PREFERENCES_ON_HIGH_RES: /* 0x437758 -> 0x43714c(0) */
        return setter(p, handler, ES2_SHELL_PREFS_HIGH_RES, &p->shadow_resolution, 0, refresh_resolution);
    case ES2_PREFERENCES_ON_LOW_RES: /* 0x4376f7 -> 0x43714c(1) */
        return setter(p, handler, ES2_SHELL_PREFS_LOW_RES, &p->shadow_resolution, 1, refresh_resolution);
    case ES2_PREFERENCES_ON_WINDOW: return window(p);
    default: return ES2_FLOW_IGNORED;
    }
}

static void on_control(es2_ui *ui, es2_ui_id window_id, const es2_gui_event *ev) {
    es2_preferences *p = prefs_of(ui, window_id);
    es2_ui_widget *w = es2_ui_get(ui, window_id);
    (void)ev;
    if (p && w) (void)es2_preferences_press(p, w->original_handler);
}
