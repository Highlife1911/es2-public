/* Unit tests of the boxes (es2/shell/gui/boxes.h): the six box screens built from
 * the layout table, their show functions and texts, every button's effect on the
 * windows, the shell and the flow, the keys in the boxes (Enter = click at
 * the pointer, Esc nothing, Ctrl+Q and Alt+F4 the Quit box), the Quit-box
 * repaint every 51st pass, the CD prompt's wait with the drain helper, the
 * DEBUG dialog with and without -@, and the Preferences Alert box. Synthetic
 * font and texts; no original data. */
#include "es2/shell/gui/boxes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

/* ---- a synthetic DFN: codes 32..126, height 6 ----------------------------- */
enum { FC = 32, FN = 95, FH = 6 };
static uint8_t font_bytes[16384];
static es2_dfn font;
static size_t put16(uint8_t *p, size_t at, int v) { p[at] = (uint8_t)v; p[at + 1] = (uint8_t)((unsigned)v >> 8); return at + 2; }
static size_t put32(uint8_t *p, size_t at, uint32_t v) { put16(p, at, (int)(v & 0xffff)); return put16(p, at + 2, (int)(v >> 16)); }
static int glyph_w(int c) { return 3 + c % 2; }
static void make_font(void) {
    uint8_t *b = font_bytes;
    size_t at = 8, data_size = 0, data_at, off = 0;
    for (int i = 0; i < FN; i++) data_size += (size_t)glyph_w(FC + i) * FH;
    at = put16(b, at, FN); at = put16(b, at, 0); at = put16(b, at, FC); at = put16(b, at, FH);
    at = put16(b, at, -1); at = put16(b, at, FH); at = put16(b, at, 5); at = put16(b, at, 8);
    at = put16(b, at, 0); at = put16(b, at, 5); at = put16(b, at, 0); at = put32(b, at, (uint32_t)data_size);
    data_at = at;
    for (int i = 0; i < FN; i++) {
        int w = glyph_w(FC + i);
        for (int r = 0; r < FH; r++)
            for (int c = 0; c < w; c++) b[data_at + off + (size_t)r * (size_t)w + (size_t)c] = (uint8_t)((r * 7 + c + i) % 4 ? 41 : 0);
        off += (size_t)w * FH;
    }
    at = data_at + data_size;
    off = 0;
    for (int i = 0; i < FN; i++) {
        at = put32(b, at, (uint32_t)off);
        off += (size_t)glyph_w(FC + i) * FH;
    }
    for (int i = 0; i < FN; i++) b[at++] = (uint8_t)glyph_w(FC + i);
    b[0] = 5; b[1] = 0; b[2] = 0x28; b[3] = 0;
    put32(b, 4, (uint32_t)(at - 8));
    if ((at - 8) & 1) b[at++] = 0;
    CHECK(es2_dfn_parse((es2_span){font_bytes, at}, &font, NULL) == ES2_OK);
}

static uint8_t bg_pixels[640 * 480];
static es2_bitmap background = {480, 640, 0x10, bg_pixels};

/* ---- host ------------------------------------------------------------------ */
static char estext_buf[64][16];
static int estext_n;
static const char *h_estext(void *ctx, int id) {
    (void)ctx;
    char *s = estext_buf[estext_n++ % 64];
    snprintf(s, 16, "T%03X", (unsigned)id);
    return s;
}
static const char *h_literal(void *ctx, uint32_t a) { (void)ctx; return a == 0x47a52b ? "" : "LIT"; }
static const es2_dfn *h_font(void *ctx, uint32_t g) { (void)ctx; (void)g; return &font; }
static const es2_bitmap *h_bitmap(void *ctx, uint32_t g) { (void)ctx; return g == 0x46de0c ? &background : NULL; }
static int screens, defaults, accepts, refreshes;
static char accepted[64];
static void h_screen(void *ctx, es2_shell_flow *f) { (void)ctx; (void)f; screens++; }
static void h_default(void *ctx) { (void)ctx; defaults++; }
static void h_accept(void *ctx, const char *t) { (void)ctx; accepts++; snprintf(accepted, sizeof accepted, "%s", t); }
static void h_refresh(void *ctx) { (void)ctx; refreshes++; }

/* flow host */
static int saves, loads, save_slot = -1, load_slot = -1;
static int f_save(void *ctx, int slot, const char *text) { (void)ctx; (void)text; saves++; save_slot = slot; return 1; }
static int f_load(void *ctx, int slot, int16_t *fw) { (void)ctx; loads++; load_slot = slot; *fw = 2; return 1; }

/* ---- shell, toolkit, hooks -------------------------------------------------- */
static es2_gui_shell *shell;
static es2_ui *ui;
static es2_shell_flow flow;
static es2_boxes boxes;
static unsigned redraws_of_quit;

static void on_event(void *c, const es2_gui_event *ev) {
    (void)c;
    if (ev->type == ES2_GUI_EV_REDRAW && ev->target == boxes.quit[1]) redraws_of_quit++;
    es2_ui_on_event(ui, ev);
}
static void on_timer(void *c, es2_gui_shell *s) { (void)c; es2_ui_on_timer(ui, s); }
static void on_map(void *c, es2_gui_shell *s) { (void)c; es2_boxes_pass(&boxes, s); }
static void on_system(void *c, es2_gui_shell *s, es2_gui_system_action a) { (void)c; es2_boxes_system(&boxes, s, a); }

/* scripted pump for the CD prompt: one window message per pump call */
typedef struct msg { int kind; int x, y; } msg; /* kind 0 move, 1 down, 2 up, 3 WM_QUIT */
static msg script[16];
static int script_n, script_at, pumps;
static int pump(void *c, es2_gui_shell *s) {
    (void)c;
    pumps++;
    if (script_at >= script_n) return pumps > 5000; /* safety: WM_QUIT */
    msg m = script[script_at++];
    if (m.kind == 0) es2_gui_on_mouse(s, ES2_GUI_WM_MOUSEMOVE, m.x, m.y, 0, 0);
    else if (m.kind == 1) es2_gui_on_mouse(s, ES2_GUI_WM_LBUTTONDOWN, m.x, m.y, 1, 0);
    else if (m.kind == 2) es2_gui_on_mouse(s, ES2_GUI_WM_LBUTTONUP, m.x, m.y, 0, 0);
    else return 1;
    return 0;
}

static void setup(int cheat) {
    es2_gui_hooks hooks = {0};
    es2_gui_platform platform = {0};
    es2_shell_flow_host fh = {0};
    es2_boxes_host bh = {0};
    platform.pump = pump;
    hooks.handle = on_event;
    hooks.timer = on_timer;
    hooks.map = on_map;
    hooks.system = on_system;
    if (!shell) shell = malloc(sizeof *shell);
    if (!ui) ui = malloc(sizeof *ui);
    es2_gui_shell_init(shell, NULL, &platform, &hooks);
    shell->input.interactive = 1;
    shell->input.cursor_x = 10;
    shell->input.cursor_y = 10;
    es2_ui_init(ui, shell);
    fh.save = f_save;
    fh.load = f_load;
    es2_shell_flow_init(&flow, &fh, NULL);
    bh.font = h_font;
    bh.bitmap = h_bitmap;
    bh.estext = h_estext;
    bh.literal = h_literal;
    bh.screen = h_screen;
    bh.debug_default = h_default;
    bh.debug_accept = h_accept;
    bh.prefs_refresh = h_refresh;
    es2_boxes_init(&boxes, ui, &flow, &bh, cheat);
    CHECK(es2_boxes_build(&boxes) == 0);
    es2_ui_show(ui, ui->root);
    script_n = script_at = pumps = 0;
    redraws_of_quit = 0;
}

static int shown(es2_ui_id id) { es2_ui_widget *w = es2_ui_get(ui, id); return w && !(w->flags & ES2_UI_HIDDEN); }
static const char *text_of(es2_ui_id id) { es2_ui_widget *w = es2_ui_get(ui, id); return w ? w->text : ""; }
static void centre(es2_ui_id id, int *x, int *y) {
    es2_ui_widget *w = es2_ui_get(ui, id);
    *x = (w->abs.left + w->abs.right) / 2;
    *y = (w->abs.top + w->abs.bottom) / 2;
}
static void step(int n) { for (int i = 0; i < n; i++) (void)es2_gui_step(shell); }
static void click(es2_ui_id id) {
    int x, y;
    centre(id, &x, &y);
    es2_gui_on_mouse(shell, ES2_GUI_WM_MOUSEMOVE, x, y, 0, 0);
    step(1);
    es2_gui_on_mouse(shell, ES2_GUI_WM_LBUTTONDOWN, x, y, 1, 0);
    step(1);
    es2_gui_on_mouse(shell, ES2_GUI_WM_LBUTTONUP, x, y, 0, 0);
    step(1);
}
static void key(unsigned vk, int shift, int alt, int ctrl) {
    es2_gui_on_key(shell, vk, 0, shift, alt, ctrl);
    step(1);
    es2_gui_on_key(shell, vk, 1, shift, alt, ctrl);
    step(1);
}

static void test_build(void) {
    setup(0);
    for (int k = 0; k < ES2_BOXES_QUIT_N; k++) CHECK(boxes.quit[k] != 0);
    for (int k = 0; k < ES2_BOXES_END_N; k++) CHECK(boxes.end[k] != 0);
    for (int k = 0; k < ES2_BOXES_WARNING_N; k++) CHECK(boxes.warning[k] != 0);
    for (int k = 0; k < ES2_BOXES_REPLAY_N; k++) CHECK(boxes.replay[k] != 0);
    for (int k = 0; k < ES2_BOXES_CD_N; k++) CHECK(boxes.cd[k] != 0);
    for (int k = 0; k < ES2_BOXES_DEBUG_N; k++) CHECK(boxes.debug[k] != 0);
    CHECK(ui->quit_box == boxes.quit[1]);
    CHECK(es2_ui_get(ui, boxes.quit[1])->cls == ES2_UI_ALERT);
    CHECK(es2_ui_get(ui, boxes.replay[0])->bitmap == &background);
    CHECK(es2_ui_get(ui, boxes.replay[1])->cls == ES2_UI_TITLE);
    CHECK(es2_ui_get(ui, boxes.quit[5])->original_handler == ES2_BOXES_QUIT_ACCEPT);
    CHECK(!strcmp(text_of(boxes.quit[2]), "T012"));  /* ESTEXT 18 title */
    CHECK(!strcmp(text_of(boxes.debug[3]), "LIT"));  /* executable literal 0x47a4c7 */
    /* every box hidden after construction, the CD prompt and the boxes too */
    CHECK(!shown(boxes.quit[0]) && !shown(boxes.quit[1]) && !shown(boxes.cd[0]) && !shown(boxes.cd[1]));
    CHECK(!shown(boxes.replay[0]) && !shown(boxes.debug[0]) && !shown(boxes.end[1]) && !shown(boxes.warning[1]));
    /* registration */
    const int scr[6] = {ES2_UI_SCREEN_QUIT_BOX, ES2_UI_SCREEN_END_OF_GAME_BOX, ES2_UI_SCREEN_LAUNCH_WARNING_BOX,
                        ES2_UI_SCREEN_REPLAY_BOX, ES2_UI_SCREEN_CD_PROMPT, ES2_UI_SCREEN_DEBUG_DIALOG};
    for (int i = 0; i < 6; i++) {
        const es2_ui_screen_entry *e = es2_ui_screen_find(scr[i]);
        CHECK(e && e->screen == scr[i]);
    }
    CHECK(es2_ui_screen_find(ES2_UI_SCREEN_QUIT_BOX)->build == es2_screen_quit_box_build);
    /* handler table */
    CHECK(es2_boxes_handler(0x432237) && es2_boxes_handler(0x437678) && !es2_boxes_handler(0x431935));
    /* refusals */
    CHECK(es2_screen_quit_box_build(ui, NULL) == -1);
    es2_boxes other;
    es2_boxes_init(&other, NULL, &flow, NULL, 0);
    CHECK(es2_boxes_build(&other) == -1);
    CHECK(es2_screen_end_of_game_box_build(ui, &other) == -1); /* boxes of another toolkit */
}

static void test_quit(void) {
    setup(0);
    /* Alt+F4: the system hook shows the Quit box with its shield */
    es2_gui_on_key(shell, 0x73, 0, 0, 1, 0);
    CHECK(shown(boxes.quit[0]) && shown(boxes.quit[1]) && shown(boxes.quit[5]));
    step(2);
    /* Esc: no consumer, the box stays, nothing quits */
    key(0x1b, 0, 0, 0);
    CHECK(shown(boxes.quit[1]) && !flow.quit && !shell->quit);
    /* CANCEL: the modal hide */
    click(boxes.quit[3]);
    CHECK(!shown(boxes.quit[1]) && !shown(boxes.quit[0]) && !flow.quit);
    /* Ctrl+Q through the WinConsumer */
    key('Q', 0, 0, 1);
    CHECK(shown(boxes.quit[1]));
    /* the repaint of step 10: every 51st pass while shown */
    boxes.quit_passes = 0;
    redraws_of_quit = 0;
    step(50);
    CHECK(redraws_of_quit == 0 && boxes.quit_passes == 50);
    step(2); /* pass 51 posts, pass 52 drains */
    CHECK(redraws_of_quit == 1 && boxes.quit_passes == 1);
    /* hidden: the counter stands still */
    click(boxes.quit[3]);
    unsigned p = boxes.quit_passes;
    step(60);
    CHECK(boxes.quit_passes == p);
    /* Enter over ACCEPT: a left click at the pointer; the flag only, the box stays */
    key('Q', 0, 0, 1);
    int x, y;
    centre(boxes.quit[5], &x, &y);
    es2_gui_on_mouse(shell, ES2_GUI_WM_MOUSEMOVE, x, y, 0, 0);
    step(1);
    es2_gui_on_key(shell, 0x0d, 0, 0, 0, 0);
    es2_gui_on_key(shell, 0x0d, 1, 0, 0, 0);
    step(1);
    CHECK(flow.quit && shell->quit && flow.exit_code == 0);
    CHECK(shown(boxes.quit[1]));
}

static void test_end_warning(void) {
    setup(0);
    flow.box = ES2_FLOW_BOX_END_OF_GAME;
    flow.box_value = 0;
    es2_boxes_sync(&boxes);
    CHECK(shown(boxes.end[1]) && shown(boxes.end[0]) && !strcmp(text_of(boxes.end[3]), "T130"));
    CHECK(!strcmp(text_of(boxes.end[4]), "T132") && !strcmp(text_of(boxes.end[6]), "T133"));
    step(2);
    click(boxes.end[5]);
    CHECK(!shown(boxes.end[1]) && !shown(boxes.end[0]) && flow.box == ES2_FLOW_BOX_NONE && boxes.shown == ES2_FLOW_BOX_NONE);
    const int words[4][2] = {{1, 0x131}, {3, 0x12f}, {7, 0x12f}, {2, 0x12f}};
    for (int i = 0; i < 4; i++) {
        char want[8];
        flow.box = ES2_FLOW_BOX_END_OF_GAME;
        flow.box_value = words[i][0];
        es2_boxes_sync(&boxes);
        snprintf(want, sizeof want, "T%03X", (unsigned)words[i][1]);
        CHECK(!strcmp(text_of(boxes.end[3]), want)); /* 7 and 2 keep the previous line */
        click(boxes.end[5]);
        CHECK(flow.box == ES2_FLOW_BOX_NONE);
    }
    /* the OKAY of a hidden box is not reachable: a click there goes to what lies below */
    for (int n = 0; n < 6; n++) {
        flow.box = ES2_FLOW_BOX_WARNING;
        flow.box_value = n;
        es2_boxes_sync(&boxes);
        CHECK(shown(boxes.warning[1]));
        if (n <= 3) {
            char a[8], b2[8];
            snprintf(a, sizeof a, "T%03X", (unsigned)(0x13c + 2 * n));
            snprintf(b2, sizeof b2, "T%03X", (unsigned)(0x13d + 2 * n));
            CHECK(!strcmp(text_of(boxes.warning[3]), a) && !strcmp(text_of(boxes.warning[4]), b2));
        } else {
            CHECK(!strcmp(text_of(boxes.warning[3]), "T142")); /* kept from n = 3 */
        }
        click(boxes.warning[5]);
        CHECK(!shown(boxes.warning[1]) && flow.box == ES2_FLOW_BOX_NONE);
    }
    /* sync of an unchanged flow shows nothing twice */
    flow.box = ES2_FLOW_BOX_NONE;
    es2_boxes_sync(&boxes);
    CHECK(!shown(boxes.warning[1]) && !shown(boxes.end[1]));
}

static void test_replay(void) {
    setup(0);
    flow.box = ES2_FLOW_BOX_REPLAY;
    flow.box_value = 3;
    flow.game = ES2_FLOW_GAME_CAMPAIGN;
    es2_boxes_sync(&boxes);
    CHECK(shown(boxes.replay[0]) && shown(boxes.replay[1]) && shown(boxes.replay[5]));
    CHECK(!strcmp(text_of(boxes.replay[3]), "T135") && !strcmp(text_of(boxes.replay[4]), "T136"));
    step(2);
    saves = screens = 0;
    click(boxes.replay[7]); /* No */
    CHECK(saves == 1 && save_slot == 10 && screens == 1 && flow.screen == ES2_FLOW_MAIN_MENU);
    CHECK(!shown(boxes.replay[0]) && !shown(boxes.replay[1]) && flow.box == ES2_FLOW_BOX_NONE && !flow.quit);
    flow.box = ES2_FLOW_BOX_REPLAY;
    flow.box_value = 0;
    es2_boxes_sync(&boxes);
    CHECK(!strcmp(text_of(boxes.replay[3]), "T137") && !strcmp(text_of(boxes.replay[4]), "T138"));
    loads = 0;
    click(boxes.replay[5]); /* Yes */
    CHECK(loads == 1 && load_slot == 10 && flow.quit && flow.exit_code == 2 && shell->quit);
    CHECK(!shown(boxes.replay[0]));
    /* a kind other than 0 and 3 keeps the lines */
    setup(0);
    es2_boxes_replay_show(&boxes, 0);
    es2_boxes_replay_hide(&boxes);
    es2_boxes_replay_show(&boxes, 1);
    CHECK(!strcmp(text_of(boxes.replay[3]), "T137") && shown(boxes.replay[1]));
}

static void test_cd_prompt(void) {
    int x, y;
    setup(0);
    ui->gate = 1; /* a movie is playing */
    centre(boxes.cd[3], &x, &y);
    script[0] = (msg){0, x, y};
    script[1] = (msg){1, x, y};
    script[2] = (msg){2, x, y};
    script_n = 3;
    CHECK(es2_boxes_cd_prompt(&boxes) == 1);
    CHECK(ui->gate == 1 && boxes.cd_retry == 0 && !shell->quit);
    CHECK(!shown(boxes.cd[0]) && !shown(boxes.cd[1]));
    CHECK(pumps == 24); /* the first drain helper round set the flag; its 24 rounds finish */
    /* Quit: the quit flag ends the wait at once */
    centre(boxes.cd[5], &x, &y);
    script[0] = (msg){0, x, y};
    script[1] = (msg){1, x, y};
    script[2] = (msg){2, x, y};
    script_n = 3;
    script_at = pumps = 0;
    CHECK(es2_boxes_cd_prompt(&boxes) == 0);
    CHECK(shell->quit && flow.quit && flow.exit_code == 0 && ui->gate == 1 && pumps == 3);
    CHECK(shown(boxes.cd[1])); /* VSHELL hides nothing on Quit */
    /* WM_QUIT inside the helper: the helper returns, the wait goes on until the flag */
    setup(0);
    script[0] = (msg){3, 0, 0};
    script_n = 1;
    shell->quit = 0;
    es2_boxes_drain_helper(shell);
    CHECK(pumps == 1);
    /* unfocused: pumps only */
    setup(0);
    shell->focused = 0;
    es2_boxes_drain_helper(shell);
    CHECK(pumps == 24 && shell->presents == 0);
}

static void test_debug(void) {
    setup(0);
    defaults = accepts = 0;
    es2_boxes_debug_show(&boxes, "C1_01");
    CHECK(shown(boxes.debug[0]) && shown(boxes.debug[1]) && !strcmp(text_of(boxes.debug[8]), "C1_01"));
    step(1); /* the two posted clicks press and fire Use Default in one drain */
    CHECK(defaults == 1 && accepts == 0 && !shown(boxes.debug[0]) && !shown(boxes.debug[1]));
    /* with -@ the dialog stays; typed text goes to ACCEPT */
    setup(1);
    defaults = accepts = 0;
    es2_boxes_debug_show(&boxes, "C1_01");
    step(3);
    CHECK(shown(boxes.debug[1]) && defaults == 0);
    click(boxes.debug[6]); /* the edit field takes the capture */
    key('A', 0, 0, 0);
    key('B', 0, 0, 0);
    key('1', 0, 0, 0);
    key(0x0d, 0, 0, 0); /* Enter ends the edit */
    CHECK(!strcmp(es2_ui_get(ui, boxes.debug[6])->edit.text, "AB1"));
    click(boxes.debug[11]);
    CHECK(accepts == 1 && !strcmp(accepted, "AB1") && !shown(boxes.debug[1]) && defaults == 0);
    /* Use Default by hand with -@ */
    es2_boxes_debug_show(&boxes, "X");
    step(2);
    click(boxes.debug[9]);
    CHECK(defaults == 1 && !shown(boxes.debug[0]));
}

static const es2_dfn *p_font(void *c, uint32_t g) { (void)c; (void)g; return &font; }
static es2_ui_handler p_handler(void *c, uint32_t a) { (void)c; return es2_boxes_handler(a); }

static void test_alert(void) {
    es2_ui_id ids[64];
    es2_ui_resolver r = {NULL, p_font, NULL, NULL, p_handler};
    es2_gui_event ev = {0};
    setup(0);
    CHECK(es2_boxes_bind_preferences(&boxes) == -1); /* not built yet */
    CHECK(es2_ui_layout_build(ui, ES2_UI_SCREEN_PREFERENCES, &r, ids, 64) > 0);
    CHECK(es2_boxes_bind_preferences(&boxes) == 0);
    flow.screen = ES2_FLOW_PREFERENCES;
    es2_ui_handler full = es2_ui_get(ui, ids[22])->handler, accept = es2_ui_get(ui, ids[5])->handler;
    CHECK(full == es2_boxes_handler(0x437608) && accept == es2_boxes_handler(0x437678));
    /* full screen already: nothing */
    shell->fullscreen = 1;
    full(ui, ids[22], &ev);
    CHECK(!shown(ids[1]) && flow.box == ES2_FLOW_BOX_NONE);
    /* windowed: the shield and its pending box are shown, the flow's Alert box */
    shell->fullscreen = 0;
    full(ui, ids[22], &ev);
    CHECK(shown(ids[1]) && shown(ids[2]) && shown(ids[5]) && flow.box == ES2_FLOW_BOX_ALERT);
    step(2);
    refreshes = 0;
    click(ids[5]);
    CHECK(!shown(ids[1]) && !shown(ids[2]) && shell->fullscreen == 1 && flow.box == ES2_FLOW_BOX_NONE);
    CHECK(flow.prefs.current[6] == 1 && refreshes == 1);
}

int main(void) {
    make_font();
    for (int i = 0; i < 640 * 480; i++) bg_pixels[i] = (uint8_t)(16 + i % 200);
    test_build();
    test_quit();
    test_end_warning();
    test_replay();
    test_cd_prompt();
    test_debug();
    test_alert();
    if (ui) es2_ui_release(ui);
    printf("boxes_test: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
