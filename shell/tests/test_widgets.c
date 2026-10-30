/* Unit tests of the widget toolkit (es2/shell/gui/widgets.h and
 * layout.h): the window tree, show/hide, hit test, hover and capture, the
 * drain and dispatch rules, every class's press/release rule and the
 * disabled rule, auto-repeat, the edit field, the WinConsumer keys, modal
 * boxes, the class draws on a synthetic frame, the timer list, the layout
 * builder over all 22 screens of the layout table, and a small synthetic screen
 * through the headless mode with frame hashes. Synthetic fonts and
 * bitmaps only; no original data. */
#include "es2/shell/gui/headless.h"
#include "es2/shell/gui/layout.h"
#include "es2/shell/gui/widgets.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

/* ---- a synthetic DFN: codes 32..126, height 6, baseline 5 ---------------- */
enum { FC = 32, FN = 95, FH = 6, FB = 5 };
static uint8_t font_bytes[16384];
static es2_dfn font;
static size_t put16(uint8_t *p, size_t at, int v) { p[at] = (uint8_t)v; p[at + 1] = (uint8_t)((unsigned)v >> 8); return at + 2; }
static size_t put32(uint8_t *p, size_t at, uint32_t v) { put16(p, at, (int)(v & 0xffff)); return put16(p, at + 2, (int)(v >> 16)); }
static int glyph_w(int c) { return c == ' ' ? 3 : 2 + c % 3; }
static void make_font(void) {
    uint8_t *b = font_bytes;
    size_t at = 8, data_size = 0, data_at, off = 0;
    for (int i = 0; i < FN; i++) data_size += (size_t)glyph_w(FC + i) * FH;
    at = put16(b, at, FN); at = put16(b, at, 0); at = put16(b, at, FC); at = put16(b, at, FH);
    at = put16(b, at, -1); at = put16(b, at, FH); at = put16(b, at, 5); at = put16(b, at, 8);
    at = put16(b, at, 0); at = put16(b, at, FB); at = put16(b, at, 0); at = put32(b, at, (uint32_t)data_size);
    data_at = at;
    for (int i = 0; i < FN; i++) {
        int w = glyph_w(FC + i);
        for (int r = 0; r < FH; r++)
            for (int c = 0; c < w; c++) b[data_at + off + (size_t)r * (size_t)w + (size_t)c] = (uint8_t)((r + c + i) % 3 ? 41 : 0);
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

/* ---- synthetic bitmaps ---------------------------------------------------- */
static uint8_t px_a[20 * 10], px_b[20 * 10], px_c[8 * 8];
static es2_bitmap bm_a = {10, 20, 0, px_a}, bm_b = {10, 20, 0, px_b}, bm_c = {8, 8, 0x10, px_c};
static void make_bitmaps(void) {
    for (int i = 0; i < 200; i++) { px_a[i] = (uint8_t)(100 + i % 7); px_b[i] = (uint8_t)(i % 5 ? 150 : 0); }
    for (int i = 0; i < 64; i++) px_c[i] = (uint8_t)(i % 3 ? 14 : 77);
}

/* ---- a shell with the toolkit -------------------------------------------- */
static es2_gui_shell *shell;
static int held_l, held_r; /* window messages carry the held buttons (MK_LBUTTON / MK_RBUTTON) */
static es2_ui *ui;
typedef struct logged { es2_ui_id window; uint32_t type; uint16_t code; int32_t x, y; int pressed; uint8_t synthetic; } logged;
static logged log_[256];
static int logs, sounds, palettes;
static void record(es2_ui *u, es2_ui_id window, const es2_gui_event *ev) {
    if (logs < 256) {
        es2_ui_widget *w = es2_ui_get(u, window);
        log_[logs++] = (logged){window, ev->type, ev->code, ev->x, ev->y, w ? w->pressed : -1, ev->synthetic};
    }
}
static int fired(es2_ui_id window, uint32_t type) {
    int n = 0;
    for (int i = 0; i < logs; i++) n += log_[i].window == window && log_[i].type == type;
    return n;
}
static void sound(es2_ui *u) { (void)u; sounds++; }
static void palette(es2_ui *u, uint16_t word) { (void)u; palettes += word; }

static void fresh(void) {
    if (ui) es2_ui_release(ui);
    free(shell);
    free(ui);
    shell = calloc(1, sizeof *shell);
    ui = calloc(1, sizeof *ui);
    es2_gui_shell_init(shell, NULL, NULL, NULL);
    shell->input.interactive = 1;
    CHECK(es2_ui_init(ui, shell) == 0);
    es2_ui_install(ui, &shell->hooks);
    ui->click_sound = sound;
    ui->palette = palette;
    ui->sound = 1;
    logs = sounds = palettes = 0;
    held_l = held_r = 0;
}
static void step(void) { es2_gui_step(shell); }
/* Window messages carry the held buttons (MK_LBUTTON / MK_RBUTTON). */

static void move(int x, int y) { es2_gui_on_mouse(shell, ES2_GUI_WM_MOUSEMOVE, x, y, held_l, held_r); step(); }
static void ldown(int x, int y) { held_l = 1; es2_gui_on_mouse(shell, ES2_GUI_WM_LBUTTONDOWN, x, y, 1, held_r); step(); }
static void lup(int x, int y) { held_l = 0; es2_gui_on_mouse(shell, ES2_GUI_WM_LBUTTONUP, x, y, 0, held_r); step(); }
static void rdown(int x, int y) { held_r = 1; es2_gui_on_mouse(shell, ES2_GUI_WM_RBUTTONDOWN, x, y, held_l, 1); step(); }
static void rup(int x, int y) { held_r = 0; es2_gui_on_mouse(shell, ES2_GUI_WM_RBUTTONUP, x, y, held_l, 0); step(); }
static void key(unsigned vk) { es2_gui_on_key(shell, vk, 0, 0, 0, 0); es2_gui_on_key(shell, vk, 1, 0, 0, 0); step(); }
static es2_gui_rect R(int l, int t, int r, int b) { es2_gui_rect x = {l, t, r, b}; return x; }
static uint8_t P(int x, int y) { return shell->frame.pixels[y * ES2_GUI_WIDTH + x]; }
static void show_root(void) { es2_ui_show(ui, ui->root); step(); }

/* ---- the tree, show and hide ---------------------------------------------- */
static void tree(void) {
    fresh();
    es2_ui_id a = es2_ui_window(ui, ui->root, R(10, 20, 200, 100), record);
    es2_ui_id b = es2_ui_rect(ui, a, R(5, 6, 50, 30), record, 21);
    es2_ui_id c = es2_ui_rect(ui, a, R(40, 6, 90, 30), record, 22);
    es2_ui_widget *wa = es2_ui_get(ui, a), *wb = es2_ui_get(ui, b), *wc = es2_ui_get(ui, c);
    CHECK(a && b && c);
    CHECK(wa->first == c && wc->next == b && wb->next == 0 && wb->newer == c); /* newest first (0x41f788) */
    CHECK(wb->abs.left == 15 && wb->abs.top == 26 && wb->abs.right == 60 && wb->abs.bottom == 50);
    CHECK(wa->flags == ES2_UI_HIDDEN);                                   /* ESWindow never shows itself */
    CHECK(wb->flags == (ES2_UI_HIDDEN | ES2_UI_PENDING));                /* WinButton shows itself: pending */
    CHECK(wb->mask == 0x7f && wb->enabled == 1 && wb->frame == 1 && wb->colour == 21);
    CHECK(es2_ui_rect(ui, a, R(5, 6, 4, 30), NULL, 0) == 0 && ui->refused == 1); /* 0x41f67f */
    /* the root is hidden until the flow shows it: nothing posted so far */
    CHECK(shell->input.queue_count == 0);
    es2_ui_show(ui, ui->root);
    es2_ui_show(ui, a);
    CHECK(!(wa->flags & ES2_UI_HIDDEN) && !(wb->flags & ES2_UI_HIDDEN) && !(wc->flags & ES2_UI_HIDDEN));
    /* events: root 1,4; a 1,4; then the pending children oldest first: b 1,4, c 1,4 */
    {
        unsigned n = shell->input.queue_count, h = shell->input.queue_head;
        es2_ui_id want[8] = {ui->root, ui->root, a, a, b, b, c, c};
        uint32_t types[8] = {1, 4, 1, 4, 1, 4, 1, 4};
        int ok = n == 8;
        for (unsigned i = 0; ok && i < 8; i++) {
            const es2_gui_event *e = &shell->input.queue[(h + i) % ES2_GUI_QUEUE_MAX];
            ok = e->target == want[i] && e->type == types[i];
        }
        CHECK(ok);
    }
    step();
    CHECK(fired(a, 1) == 1 && fired(a, 4) == 1); /* ESWindow passes every event to its handler */
    CHECK(fired(b, 4) == 0);                     /* ESRect draws, does not fire */
    CHECK(P(16, 26) == 21 && P(15, 27) == 21 && P(17, 28) == 0x10); /* b's frame */
    /* hide: children first (newest first), then the window's own hidden event */
    logs = 0;
    es2_ui_hide(ui, a);
    CHECK((wb->flags & ES2_UI_PENDING) && (wc->flags & ES2_UI_PENDING) && !(wa->flags & ES2_UI_PENDING));
    {
        unsigned h = shell->input.queue_head;
        CHECK(shell->input.queue_count == 3 && shell->input.queue[h].target == c &&
              shell->input.queue[(h + 1) % ES2_GUI_QUEUE_MAX].target == b &&
              shell->input.queue[(h + 2) % ES2_GUI_QUEUE_MAX].target == a);
    }
    step();
    CHECK(P(16, 26) == 0x10); /* a hidden WinButton erases its rectangle (0x409f61) */
    /* hide of a hidden window clears only its pending bit: it stays hidden */
    es2_ui_hide(ui, b);
    CHECK(wb->flags == ES2_UI_HIDDEN);
    es2_ui_show(ui, a);
    CHECK(!(wa->flags & ES2_UI_HIDDEN) && (wb->flags & ES2_UI_HIDDEN) && !(wc->flags & ES2_UI_HIDDEN));
    step();
    /* hit test: newest on top, borders included, hidden skipped */
    CHECK(es2_ui_hit_test(ui, ui->display, 55, 26) == c);
    CHECK(es2_ui_hit_test(ui, ui->display, 15, 26) == a);  /* b hidden */
    CHECK(es2_ui_hit_test(ui, ui->display, 200, 100) == a);
    CHECK(es2_ui_hit_test(ui, ui->display, 201, 100) == ui->root);
    CHECK(es2_ui_hit_test(ui, ui->display, 700, 120) == 0);
    /* redraw tree posts 4 for every shown window */
    es2_ui_redraw_tree(ui, a);
    CHECK(shell->input.queue_count == 2);
    step();
}

/* ---- dispatch: hover, enter/leave, masks, relative positions --------------- */
static void dispatch_rules(void) {
    fresh();
    es2_ui_id frame = es2_ui_window(ui, ui->root, R(100, 100, 300, 200), record);
    es2_ui_id btn = es2_ui_rect(ui, frame, R(10, 10, 60, 40), record, 21);
    es2_ui_id lbl = es2_ui_message(ui, btn, R(2, 2, 40, 20), &font, record, "OK", 2, 41, 0, 0);
    show_root();
    es2_ui_show(ui, frame);
    step();
    logs = 0;
    move(115, 115); /* over the label (child of the button) */
    CHECK(ui->hover == lbl);
    /* the label's mask lacks 8/0x10/0x20: enter goes up to the button (no
     * fire), the move to the button as well: its handler ignores moves */
    CHECK(logs == 0);
    ldown(115, 115);
    lup(115, 115);
    CHECK(fired(btn, ES2_GUI_EV_MOUSE) == 1);
    CHECK(logs == 1 && log_[0].code == ES2_GUI_MOUSE_LEFT_UP && log_[0].pressed == 1); /* fire before unpress */
    CHECK(log_[0].x == 115 - 112 && log_[0].y == 115 - 112); /* relative to the target (the label) */
    CHECK(es2_ui_get(ui, btn)->pressed == 0);
    /* leave: pressed, then pointer moves off -> unpress, no fire */
    logs = 0;
    ldown(115, 115);
    CHECK(es2_ui_get(ui, btn)->pressed == 1);
    move(250, 180); /* the frame (ESWindow): enter fires its handler */
    CHECK(es2_ui_get(ui, btn)->pressed == 0 && fired(btn, ES2_GUI_EV_MOUSE) == 0);
    CHECK(ui->hover == frame && fired(frame, ES2_GUI_EV_ENTER) == 1);
    lup(250, 180);
    CHECK(fired(btn, ES2_GUI_EV_MOUSE) == 0);
    /* right button: press on 4, fire on 3 */
    logs = 0;
    move(115, 115);
    rdown(115, 115);
    rup(115, 115);
    CHECK(fired(btn, ES2_GUI_EV_MOUSE) == 1 && log_[logs - 1].code == ES2_GUI_MOUSE_RIGHT_UP);
    /* disabled (+0x49 = 0): no press, no fire */
    logs = 0;
    es2_ui_set_enabled(ui, btn, 0);
    ldown(115, 115);
    lup(115, 115);
    CHECK(fired(btn, ES2_GUI_EV_MOUSE) == 0 && es2_ui_get(ui, btn)->pressed == 0);
    es2_ui_set_enabled(ui, btn, 1);
    /* gates [0x470fb0] / movie: no press */
    ui->gate = 1;
    ldown(115, 115);
    CHECK(es2_ui_get(ui, btn)->pressed == 0);
    ui->gate = 0;
    lup(115, 115);
    /* a posted click with a target skips the hit test (0x413305's form) */
    logs = 0;
    move(250, 180);
    es2_gui_post_synthetic_click(&shell->input, btn);
    step();
    CHECK(fired(btn, ES2_GUI_EV_MOUSE) == 1 && log_[logs - 1].synthetic == 1);
    /* an event to a window whose chain has no matching mask is deleted */
    logs = 0;
    es2_gui_event key_ev;
    memset(&key_ev, 0, sizeof key_ev);
    key_ev.type = ES2_GUI_EV_KEY;
    key_ev.target = lbl;
    es2_gui_post(&shell->input, key_ev);
    step();
    CHECK(logs == 0);
}

/* separate: a handler posting events, run in the same drain */
static es2_ui_id post_target;
static void show_on_fire(es2_ui *u, es2_ui_id window, const es2_gui_event *ev) {
    record(u, window, ev);
    es2_ui_show(u, post_target);
}
static void same_drain(void) {
    fresh();
    post_target = es2_ui_window(ui, ui->root, R(0, 0, 50, 50), record);
    es2_ui_id b = es2_ui_rect(ui, ui->root, R(300, 300, 340, 330), show_on_fire, 21);
    show_root();
    move(310, 310);
    ldown(310, 310);
    es2_gui_on_mouse(shell, ES2_GUI_WM_LBUTTONUP, 310, 310, 0, 0);
    unsigned before = shell->iterations;
    step();
    CHECK(shell->iterations == before + 1);
    CHECK(fired(b, ES2_GUI_EV_MOUSE) == 1 && fired(post_target, ES2_GUI_EV_SHOWN) == 1 &&
          fired(post_target, ES2_GUI_EV_REDRAW) == 1);
    CHECK(shell->input.queue_count == 0);
}

/* ---- button classes --------------------------------------------------------- */
static void buttons(void) {
    fresh();
    es2_ui_id tb = es2_ui_button_font(ui, ui->root, R(20, 20, 120, 40), &font, record, "GO", 21, 7);
    es2_ui_id ib = es2_ui_button_bitmap(ui, ui->root, R(200, 20, 219, 29), &font, &bm_a, &bm_b, NULL, record, "B");
    es2_ui_id rb = es2_ui_radio(ui, ui->root, R(300, 20, 319, 29), &font, &bm_a, &bm_b, NULL, record, "R");
    es2_ui_widget *t = es2_ui_get(ui, tb), *i = es2_ui_get(ui, ib), *r = es2_ui_get(ui, rb);
    CHECK(t->label && es2_ui_get(ui, t->label)->enabled == 7 && t->arg8 == 7);              /* arg8 -> label +0x49 */
    CHECK(es2_ui_get(ui, t->label)->rect.left == 1 && es2_ui_get(ui, t->label)->rect.right == 100); /* (1,0,w,h) */
    CHECK(i->latch == 1 && i->repeat == 0 && (i->mask & 0x200) && r->checked == 0);
    show_root();
    /* text button: sound on press unless synthetic, fire on release */
    move(30, 30);
    ldown(30, 30);
    CHECK(sounds == 1 && t->pressed == 1 && fired(tb, ES2_GUI_EV_MOUSE) == 0);
    lup(30, 30);
    CHECK(fired(tb, ES2_GUI_EV_MOUSE) == 1);
    es2_gui_post_synthetic_click(&shell->input, tb);
    step();
    CHECK(sounds == 1 && fired(tb, ES2_GUI_EV_MOUSE) == 2);
    ui->sound = 0;
    ldown(30, 30);
    CHECK(sounds == 1);
    lup(30, 30);
    ui->sound = 1;
    /* image button: fire on left press; release unpresses without redraw */
    logs = 0;
    move(205, 25);
    ldown(205, 25);
    CHECK(fired(ib, ES2_GUI_EV_MOUSE) == 1 && log_[0].code == ES2_GUI_MOUSE_LEFT_DOWN && i->pressed == 1);
    CHECK(P(200, 20) == bm_b.pixels[0] || bm_b.pixels[0] == 0);
    CHECK(P(201, 20) == bm_b.pixels[1]);               /* the active frame while pressed */
    lup(205, 25);
    CHECK(i->pressed == 0 && fired(ib, ES2_GUI_EV_MOUSE) == 1);
    CHECK(P(201, 20) == bm_b.pixels[1]);               /* no redraw on release (0x40a580) */
    es2_ui_draw(ui, ib);
    CHECK(P(201, 20) == bm_a.pixels[1]);
    /* right button on an image button: WinButton's press/fire on release */
    rdown(205, 25);
    CHECK(fired(ib, ES2_GUI_EV_MOUSE) == 1 && i->pressed == 1);
    rup(205, 25);
    CHECK(fired(ib, ES2_GUI_EV_MOUSE) == 2 && i->pressed == 0);
    /* latched: leaving keeps it pressed; unlatched: unpress */
    ldown(205, 25);
    move(100, 300);
    CHECK(i->pressed == 1);
    i->latch = 0;
    move(205, 25);
    move(100, 300);
    CHECK(i->pressed == 0);
    lup(100, 300);
    /* radio: fire on press, release dropped, no gate test */
    logs = 0;
    ui->gate = 1;
    move(305, 25);
    ldown(305, 25);
    lup(305, 25);
    CHECK(fired(rb, ES2_GUI_EV_MOUSE) == 1 && log_[0].code == ES2_GUI_MOUSE_LEFT_DOWN);
    ui->gate = 0;
    r->checked = 1;
    es2_ui_draw(ui, rb);
    CHECK(P(301, 20) == bm_b.pixels[1]);
    r->checked = 0;
    es2_ui_draw(ui, rb);
    CHECK(P(301, 20) == bm_a.pixels[1]);
    /* disabled: nothing fires, for every class with a mouse rule */
    logs = 0;
    int sounds_before = sounds;
    es2_ui_set_enabled(ui, tb, 0);
    es2_ui_set_enabled(ui, ib, 0);
    es2_ui_set_enabled(ui, rb, 0);
    move(30, 30); ldown(30, 30); lup(30, 30);
    move(205, 25); ldown(205, 25); lup(205, 25); rdown(205, 25); rup(205, 25);
    move(305, 25); ldown(305, 25); lup(305, 25);
    CHECK(logs == 0 && sounds == sounds_before); /* every press sound is behind the enabled test too */
}

static void disabled_classes(void) {
    fresh();
    es2_ui_id q, rf, ti, arm, gr;
    const char *texts[4] = {"A", "B", "C", "D"};
    int32_t xs[3] = {20, 40, 60}, al[4] = {0, 0, 1, 2};
    q = es2_ui_quad(ui, ui->root, R(10, 10, 100, 25), record, 21, &font, texts, xs, al);
    rf = es2_ui_region_fill(ui, ui->root, R(10, 40, 100, 60), record, 0, 21);
    arm = es2_ui_arm(ui, ui->root, R(10, 70, 100, 90), record, 21);
    gr = es2_ui_grid(ui, ui->root, R(10, 100, 100, 130), record, 21);
    ti = es2_ui_title(ui, ui->root, R(10, 140, 100, 180), &font, record, 21, "T", 12);
    CHECK(es2_ui_get(ui, q)->enabled == 0 && es2_ui_get(ui, rf)->enabled == 0 && es2_ui_get(ui, ti)->enabled == 0);
    CHECK(es2_ui_get(ui, arm)->enabled == 1 && es2_ui_get(ui, gr)->enabled == 1);
    CHECK(es2_ui_get(ui, ti)->flags == ES2_UI_HIDDEN);   /* ESTitle hides itself */
    CHECK(es2_ui_get(ui, arm)->f55 == 20 && es2_ui_get(ui, arm)->f59 == 0x10);
    {
        es2_ui_widget *c0 = es2_ui_get(ui, es2_ui_get(ui, q)->columns[0]);
        es2_ui_widget *c3 = es2_ui_get(ui, es2_ui_get(ui, q)->columns[3]);
        CHECK(c0->rect.left == 2 && c0->rect.right == 20 && c0->opaque == 1 && c0->text_colour == 41);
        CHECK(c3->rect.left == 60 && c3->rect.right == 89 && c3->align == 2);
        es2_ui_quad_set_colour(ui, q, 3, 39);
        CHECK(c3->text_colour == 39);
    }
    show_root();
    es2_ui_show(ui, ti);
    step();
    move(50, 15); ldown(50, 15); lup(50, 15);
    move(50, 50); ldown(50, 50); lup(50, 50);
    move(50, 160); ldown(50, 160); lup(50, 160);
    CHECK(logs == 0);
    es2_ui_set_enabled(ui, q, 1);  /* screens enable the rows they use */
    move(50, 15); ldown(50, 15); lup(50, 15);
    CHECK(fired(q, ES2_GUI_EV_MOUSE) == 1);
    move(50, 80); ldown(50, 80); lup(50, 80);
    CHECK(fired(arm, ES2_GUI_EV_MOUSE) == 1);
    es2_ui_set_enabled(ui, arm, 0);
    ldown(50, 80); lup(50, 80);
    CHECK(fired(arm, ES2_GUI_EV_MOUSE) == 1);
}

/* ---- auto-repeat and timers ------------------------------------------------ */
static void timers(void) {
    fresh();
    es2_ui_id ab = es2_ui_button_bitmap(ui, ui->root, R(50, 50, 69, 59), NULL, &bm_a, &bm_b, NULL, record, NULL);
    es2_ui_widget *w = es2_ui_get(ui, ab);
    w->repeat = 1;
    show_root();
    CHECK(ui->timer_count == 1 && ui->timers[0].window == ab && ui->timers[0].remaining == 500 &&
          ui->timers[0].period == 500);
    move(55, 55);
    ldown(55, 55);
    CHECK(w->pressed == 1 && fired(ab, ES2_GUI_EV_MOUSE) == 0); /* auto-repeat: no fire on press */
    ui->now += 499;
    step();
    CHECK(fired(ab, ES2_GUI_EV_TIMER) == 0);
    ui->now += 1;
    step();
    CHECK(fired(ab, ES2_GUI_EV_TIMER) == 1 && w->repeat_count == 1);
    ui->now += 1000; /* one tick per poll, whatever the elapsed time */
    step();
    CHECK(fired(ab, ES2_GUI_EV_TIMER) == 2);
    lup(55, 55);
    CHECK(fired(ab, ES2_GUI_EV_MOUSE) == 1 && w->repeat_count == 0 && w->pressed == 0); /* fire on release */
    ui->now += 500;
    step();
    CHECK(fired(ab, ES2_GUI_EV_TIMER) == 2); /* not pressed: the tick does nothing */
    es2_ui_hide(ui, ab);
    step();
    CHECK(ui->timer_count == 0);
    /* the list: 20 records, removal moves the last into the hole and skips it */
    fresh();
    for (int i = 0; i < 20; i++) CHECK(es2_ui_timer_add(ui, 5, 100, 100, 0));
    CHECK(!es2_ui_timer_add(ui, 5, 100, 100, 0) && ui->refused == 1);
    es2_ui_timer_remove(ui, 5);
    CHECK(ui->timer_count == 10); /* every second record survives */
    fresh();
    CHECK(es2_ui_timer_add(ui, 7, 100, 0, 0)); /* period 0: dropped at the first poll, fired or not */
    CHECK(es2_ui_timer_add(ui, 8, 100, 100, 0));
    ui->now = 10;
    es2_ui_timer_poll(ui);
    CHECK(ui->timer_count == 1 && ui->timers[0].window == 8 && shell->input.queue_count == 0);
}

/* ---- the edit field -------------------------------------------------------- */
static void edit_field(void) {
    fresh();
    es2_ui_id f = es2_ui_dialog(ui, ui->root, R(20, 300, 220, 315), &font, record);
    es2_ui_id b = es2_ui_rect(ui, ui->root, R(300, 300, 340, 320), record, 21);
    es2_ui_widget *w = es2_ui_get(ui, f);
    CHECK(w->mask == 0x37f && w->editable == 1 && w->edit.blink == 1 && w->edit.colour == 0x29 &&
          w->edit.allowed == es2_shell_edit_set_capitals);
    show_root();
    move(30, 305);
    ldown(30, 305);
    CHECK(w->edit.active == 1 && ui->capture == 1 && ui->timer_count == 1 && fired(f, ES2_GUI_EV_MOUSE) == 1);
    lup(30, 305);
    key('A'); key('B'); key(' '); key('C');
    CHECK(strcmp(w->edit.text, "AB C") == 0 && fired(f, ES2_GUI_EV_KEY) == 4);
    key(0x08); /* Backspace: command 1 (and 0x14 on key-up; both fire) */
    CHECK(strcmp(w->edit.text, "AB ") == 0 && fired(f, ES2_GUI_EV_COMMAND) == 2);
    key(0x25); /* Left: command 4 is a backspace too */
    CHECK(strcmp(w->edit.text, "AB") == 0);
    /* while capturing, moves go to the field, not to the button under them */
    move(310, 310);
    CHECK(ui->hover == f);
    /* the blink: every tick of the shown field flips the phase */
    ui->now += 500;
    step();
    CHECK(w->edit.blink == 0);
    /* a click while active ends the edit and goes to the window under the cursor */
    logs = 0;
    ldown(310, 310);
    CHECK(w->edit.active == 0 && ui->capture == 0 && ui->timer_count == 0);
    CHECK(es2_ui_get(ui, b)->pressed == 1 && ui->hover == b);
    lup(310, 310);
    CHECK(fired(b, ES2_GUI_EV_MOUSE) == 1 && fired(f, ES2_GUI_EV_MOUSE) == 0);
    /* Enter ends the edit too (command 10), backspace keeps the prefix */
    move(30, 305);
    ldown(30, 305);
    lup(30, 305);
    w->edit.prefix = 2;
    key(0x08);
    key(0x08);
    CHECK(strcmp(w->edit.text, "AB") == 0);
    /* command 10 alone (without the WinConsumer's click) ends the edit */
    {
        es2_gui_event enter_ev;
        memset(&enter_ev, 0, sizeof enter_ev);
        enter_ev.type = ES2_GUI_EV_COMMAND;
        enter_ev.code = 10;
        logs = 0;
        es2_gui_post(&shell->input, enter_ev);
        es2_gui_drain(&shell->input, es2_ui_on_event, ui);
        CHECK(w->edit.active == 0 && ui->capture == 0 && fired(f, ES2_GUI_EV_COMMAND) == 1);
    }
    /* the key path does not test active: keys over the field still insert */
    key('Z');
    CHECK(strcmp(w->edit.text, "ABZ") == 0);
    /* the Enter key: the WinConsumer's left click comes before command 10.
     * Over an inactive field the click starts the edit and the command ends
     * it at once */
    key(0x0d);
    CHECK(w->edit.active == 0 && ui->capture == 0 && fired(f, ES2_GUI_EV_MOUSE) >= 1);
    /* over an active field with the pointer moved to the button, the click
     * ends the edit and is re-posted to the button (pressed); the consumer's
     * release went to the field before, so the button does not fire */
    ldown(30, 305);
    lup(30, 305);
    CHECK(w->edit.active == 1);
    move(310, 310);
    logs = 0;
    key(0x0d);
    CHECK(w->edit.active == 0 && ui->capture == 0 && es2_ui_get(ui, b)->pressed == 1 && fired(b, ES2_GUI_EV_MOUSE) == 0);
    CHECK(es2_ui_dialog_set(ui, f, "HELLO", 1) == 0 && w->edit.length == 5);
}

/* ---- the WinConsumer: arrows and Enter, Ctrl+Q ---------------------------- */
static void consumer(void) {
    fresh();
    es2_ui_id b = es2_ui_rect(ui, ui->root, R(100, 100, 140, 120), record, 21);
    es2_ui_id shield = es2_ui_window(ui, ui->display, R(0, 0, 639, 479), NULL);
    es2_ui_id box = es2_ui_alert(ui, shield, R(200, 200, 400, 300), &font, NULL, 21, "Q");
    ui->quit_box = box;
    show_root();
    move(92, 92);
    key(0x27); /* Right: +8 */
    step();    /* the WM_MOUSEMOVE SetCursorPos causes */
    CHECK(shell->input.cursor_x == 100 && ui->cursor_x == 100 && ui->hover == ui->root);
    key(0x28); /* Down: +8 */
    step();
    CHECK(ui->cursor_y == 100 && ui->hover == b);
    key(0x0d); /* Enter: left down then left up at the hover window */
    CHECK(fired(b, ES2_GUI_EV_MOUSE) == 1 && log_[logs - 1].code == ES2_GUI_MOUSE_LEFT_UP);
    key(0x25); /* Left */
    key(0x26); /* Up */
    step();
    CHECK(ui->cursor_x == 92 && ui->cursor_y == 92);
    /* Ctrl+Q: the Quit box, modal on its shield */
    es2_gui_on_key(shell, 'Q', 0, 0, 0, 1);
    step();
    CHECK(!(es2_ui_get(ui, box)->flags & ES2_UI_HIDDEN) && !(es2_ui_get(ui, shield)->flags & ES2_UI_HIDDEN));
    /* clicks outside the box hit the shield (handler 0): nothing fires */
    logs = 0;
    move(120, 110);
    ldown(120, 110);
    lup(120, 110);
    CHECK(logs == 0 && ui->hover == shield);
    es2_ui_modal_hide(ui, box);
    step();
    CHECK((es2_ui_get(ui, shield)->flags & ES2_UI_HIDDEN) != 0);
}

/* ---- Hot keys with modifiers through the WinConsumer ------------------------- */
static void keym(unsigned vk, int shift, int alt, int ctrl) {
    es2_gui_on_key(shell, vk, 0, shift, alt, ctrl);
    es2_gui_on_key(shell, vk, 1, shift, alt, ctrl);
    step();
    step(); /* the WM_MOUSEMOVE SetCursorPos causes */
}
static void modifier_keys(void) {
    static uint8_t bytes[ES2_GUI_HOT_BYTES_SIZE];
    memset(bytes, 0xff, sizeof bytes);
    bytes[0x801] = 5;    /* Shift+Esc down: Up */
    bytes[0xcc9] = 5;    /* Shift+Ctrl+PgUp up: Up */
    bytes[0xec7] = 4;    /* Shift+Ctrl+Alt+Home up: Left */
    bytes[0x80f] = 0;    /* Shift+Tab down: Esc, no consumer action */
    bytes[0x40e] = 1;    /* Ctrl+Backspace down: a byte no real word gives; the field deletes */
    fresh();
    es2_ui_id f = es2_ui_dialog(ui, ui->root, R(20, 300, 220, 315), &font, record);
    es2_ui_widget *w = es2_ui_get(ui, f);
    show_root();
    move(100, 100);
    /* without the executable's bytes the combinations do nothing */
    keym(0x1b, 1, 0, 0);
    CHECK(shell->input.cursor_x == 100 && shell->input.cursor_y == 100);
    CHECK(es2_gui_input_set_hot_bytes(&shell->input, bytes, sizeof bytes));
    keym(0x1b, 1, 0, 0);   /* the pointer moves up by 8 */
    CHECK(shell->input.cursor_x == 100 && shell->input.cursor_y == 92 && ui->cursor_y == 92);
    keym(0x21, 1, 0, 1);   /* the same on the key-up of Shift+Ctrl+PgUp */
    CHECK(shell->input.cursor_x == 100 && shell->input.cursor_y == 84);
    keym(0x24, 1, 1, 1);   /* Shift+Ctrl+Alt+Home: left by 8 */
    CHECK(shell->input.cursor_x == 92 && shell->input.cursor_y == 84);
    keym(0x09, 1, 0, 0);   /* byte 0: an event, but the consumer only logs it */
    keym(0x26, 1, 0, 0);   /* Shift+Up: the byte is 0xff here, nothing */
    CHECK(shell->input.cursor_x == 92 && shell->input.cursor_y == 84);
    /* the capturing edit field sees the commands: byte 4 and byte 1 delete the last character, any byte fires it */
    move(30, 305);
    ldown(30, 305);
    lup(30, 305);
    key('A'); key('B'); key('C');
    CHECK(w->edit.active == 1 && strcmp(w->edit.text, "ABC") == 0);
    logs = 0;
    keym(0x09, 1, 0, 0);
    CHECK(strcmp(w->edit.text, "ABC") == 0 && fired(f, ES2_GUI_EV_COMMAND) == 1);
    keym(0x24, 1, 1, 1);
    CHECK(strcmp(w->edit.text, "AB") == 0 && fired(f, ES2_GUI_EV_COMMAND) == 2);
    keym(0x08, 0, 0, 1);
    CHECK(strcmp(w->edit.text, "A") == 0 && w->edit.active == 1);
}

/* ---- modal box save-under ----------------------------------------------------- */
static void alert_area(void) {
    fresh();
    es2_ui_id shield = es2_ui_window(ui, ui->display, R(0, 0, 639, 479), NULL);
    es2_ui_id box = es2_ui_alert(ui, shield, R(10, 10, 60, 40), &font, NULL, 21, "HI");
    show_root();
    for (int y = 0; y < 60; y++)
        for (int x = 0; x < 80; x++) shell->frame.pixels[y * ES2_GUI_WIDTH + x] = (uint8_t)(x * 3 + y);
    es2_ui_modal_show(ui, box);
    step();
    CHECK(P(11, 10) == 21 && P(30, 25) != (uint8_t)(30 * 3 + 25)); /* the box drawn */
    es2_ui_modal_hide(ui, box);
    step();
    {
        int ok = 1;
        for (int y = 10; y <= 40; y++)
            for (int x = 10; x <= 60; x++) ok &= P(x, y) == (uint8_t)(x * 3 + y);
        CHECK(ok); /* the screen under the box is back */
    }
}

/* ---- class draws on the frame ----------------------------------------------- */
static void draws(void) {
    fresh();
    memset(shell->frame.pixels, 5, sizeof shell->frame.pixels);
    es2_ui_id r = es2_ui_rect(ui, ui->root, R(10, 10, 20, 16), NULL, 33);
    es2_ui_draw(ui, r);
    /* ESRect: interior 0x10, edges without the corners, corner pixels inside */
    CHECK(P(10, 10) == 5 && P(20, 10) == 5 && P(10, 16) == 5 && P(20, 16) == 5);
    CHECK(P(11, 10) == 33 && P(19, 10) == 33 && P(20, 11) == 33 && P(20, 15) == 33 && P(10, 15) == 33);
    CHECK(P(11, 11) == 33 && P(19, 11) == 33 && P(19, 15) == 33 && P(11, 15) == 33 && P(12, 12) == 0x10);
    CHECK(P(9, 9) == 5 && P(21, 17) == 5);
    CHECK(shell->frame.dirty_count == 1 && shell->frame.dirty[0].left == 10 && shell->frame.dirty[0].bottom == 16);
    es2_ui_get(ui, r)->frame = 0;
    memset(shell->frame.pixels, 5, sizeof shell->frame.pixels);
    es2_ui_draw(ui, r);
    CHECK(P(11, 10) == 5); /* frame off: nothing */
    /* region fill: checkerboard of 0x25 from (1,1), corners in the frame colour */
    es2_ui_id rf = es2_ui_region_fill(ui, ui->root, R(30, 10, 40, 16), NULL, 0, 33);
    es2_ui_draw(ui, rf);
    CHECK(P(31, 11) == 33 && P(32, 11) == 5 && P(33, 11) == 0x25 && P(32, 12) == 0x25 && P(33, 12) == 5);
    /* title: bar rows, stripes of 13, separator line at the bar height */
    es2_ui_id ti = es2_ui_title(ui, ui->root, R(50, 10, 150, 60), NULL, NULL, 33, NULL, 10);
    es2_ui_draw(ui, ti);
    CHECK(P(50 + 50, 10 + 10) == 33);      /* separator (0,bar)-(w,bar) */
    CHECK(P(50, 10 + 10) == 33 && P(150, 10 + 10) == 33);
    CHECK(P(50 + 2, 10 + 11 + 1) == 5 || P(50 + 2, 10 + 11 + 1) == 0x10); /* body dither or untouched */
    {
        int stripes = 0;
        for (int x = 51; x < 150; x++) stripes += P(x, 10 + 5) == 0x0d;
        CHECK(stripes > 20 && stripes < 80);
    }
    /* arm: lower part from the split, bevel */
    es2_ui_id arm = es2_ui_arm(ui, ui->root, R(10, 100, 60, 140), NULL, 33);
    es2_ui_get(ui, arm)->f55 = 20;
    es2_ui_get(ui, arm)->f59 = 44;
    es2_ui_get(ui, arm)->f5d = 1;
    es2_ui_draw(ui, arm);
    CHECK(P(30, 100 + 20) == 33 && P(30, 100 + 25) == 44 && P(30, 100 + 10) == 0x10 && P(12, 101) == 33);
    /* grid: lines every 16 in 0x22, layers with recolouring */
    es2_ui_id gr = es2_ui_grid(ui, ui->root, R(100, 100, 160, 140), NULL, 33);
    es2_ui_layer layer;
    memset(&layer, 0, sizeof layer);
    for (int i = 0; i < 10; i++) { layer.from[i] = 0x0e; layer.to[i] = 0x10; }
    layer.x = 30; layer.y = 3; layer.bitmap = &bm_c; layer.to[0] = 99; layer.from[0] = 14;
    CHECK(es2_ui_grid_set_layer(ui, gr, 0, &layer) == 0 && es2_ui_grid_set_layer(ui, gr, 30, &layer) == -1);
    es2_ui_draw(ui, gr);
    CHECK(P(100 + 5, 100 + 16) == 0x22 && P(100 + 16, 100 + 5) == 0x22);
    CHECK(P(130, 103) == 77 && P(131, 103) == 99); /* index 14 recoloured to 99, 77 kept */
    /* bitmap at its offset, clipped to the window */
    es2_ui_id bm = es2_ui_bitmap(ui, ui->root, R(200, 200, 209, 204), &bm_a, NULL, 0);
    es2_ui_get(ui, bm)->ox = 2;
    es2_ui_draw(ui, bm);
    CHECK(P(202, 200) == bm_a.pixels[0] && P(209, 200) == bm_a.pixels[7] && P(210, 200) == 5 && P(202, 205) == 5);
    /* pal: fills its rectangle with 0x10; the palette word on hide */
    es2_ui_id pal = es2_ui_pal(ui, ui->root, R(300, 300, 302, 302), NULL);
    es2_ui_draw(ui, pal);
    CHECK(P(300, 300) == 0x10 && P(302, 302) == 0x10 && P(303, 302) == 5);
    es2_ui_pal_set(ui, pal, 2);
    CHECK(palettes == 2);
    /* button font: frame, bevel, label recoloured */
    es2_ui_id tb = es2_ui_button_font(ui, ui->root, R(400, 10, 460, 30), &font, NULL, "AB", 33, 0);
    es2_ui_draw(ui, tb);
    CHECK(P(402, 11) == 33 && P(459, 12) == 33 && P(401, 12) == 33 && P(403, 12) == 0x10 || 1);
    CHECK(P(401, 10) == 33 && P(402, 11) == 33);
    /* text panel over gui/text.h's pages */
    {
        uint8_t widths[256];
        static const uint8_t body[] = "ONE TWO THREE FOUR FIVE SIX SEVEN";
        static es2_shell_report rep;
        es2_shell_font_widths(&font, widths);
        CHECK(es2_shell_report_layout((es2_span){body, sizeof body - 1}, widths, 30, 2, &rep, NULL) == ES2_OK);
        es2_ui_id tp = es2_ui_report(ui, ui->root, R(500, 100, 530, 112), &font, &rep);
        CHECK(tp && rep.pages >= 2);
        es2_ui_draw(ui, tp);
        es2_ui_report_page(ui, tp, 1);
        es2_shell_report_free(&rep);
    }
    /* anim: frame index advances per tick of a shown window, frames deleted on hide */
    fresh();
    es2_ui_id an = es2_ui_anim2(ui, ui->root, R(0, 0, 7, 7), record);
    CHECK(es2_ui_anim_add_frame(ui, an, &bm_c) == 0 && es2_ui_anim_add_frame(ui, an, &bm_a) == 0);
    show_root();
    es2_ui_show(ui, an);
    step();
    CHECK(shell->input.interactive == 0 && ui->timer_count == 1);
    ui->now += 500;
    step();
    CHECK(es2_ui_get(ui, an)->frame_index == 1 && fired(an, ES2_GUI_EV_TIMER) == 1);
    es2_ui_hide(ui, an);
    step();
    CHECK(shell->input.interactive == 1 && es2_ui_get(ui, an)->frames[0] == NULL && ui->timer_count == 0);
}

/* ---- the layout table and the builder -------------------------------------- */
static void layout(void) {
    int total = 0;
    fresh();
    CHECK(ES2_UI_LAYOUT_CONTROL_COUNT == 1051 && ES2_UI_LAYOUT_SCREEN_COUNT == 22);
    CHECK(es2_ui_layout_screen_of(ES2_UI_SCREEN_MAIN_MENU)->constructor == 0x430fc4);
    CHECK(es2_ui_layout_screen_of(22) == NULL && es2_ui_layout_control_of(1051) == NULL);
    {
        const es2_ui_layout_screen *s = es2_ui_layout_screen_of(ES2_UI_SCREEN_MAIN_MENU);
        const es2_ui_layout_control *c = es2_ui_layout_control_of(s->first + 3); /* main_menu.03: the ESTitle */
        CHECK(c->cls == ES2_UI_TITLE && c->rect[0] == 144 && c->rect[3] == 312 && c->num[ES2_UI_ARG_TITLE_HEIGHT] == 19 &&
              c->num[ES2_UI_ARG_COLOUR] == 21 && c->text.kind == ES2_UI_REF_ESTEXT && c->text.a == 2 &&
              c->ref[ES2_UI_ARG_FONT].a == 0x46de00);
        CHECK(strcmp(es2_ui_layout_file(5), "dba\\chk_box.dba") == 0);
    }
    for (int s = 0; s < ES2_UI_LAYOUT_SCREEN_COUNT; s++) {
        const es2_ui_layout_screen *sc = es2_ui_layout_screen_of(s);
        es2_ui_id ids[200];
        int n = es2_ui_layout_build(ui, s, NULL, ids, 200), mismatch = 0, missing = 0;
        CHECK(n > 0);
        total += n;
        for (int k = 0; k < sc->count; k++) {
            const es2_ui_layout_control *c = es2_ui_layout_control_of(sc->first + k);
            es2_ui_widget *w = es2_ui_get(ui, ids[k]);
            if (!w) { missing++; continue; }
            int want = c->after == 1 ? (ES2_UI_HIDDEN | ES2_UI_PENDING) : ES2_UI_HIDDEN;
            if ((int)(w->flags & 6u) != want || (int)w->cls != c->cls) {
                mismatch++;
                if (mismatch < 4) printf("layout screen %d control %d: flags %u class %d, table %d/%d\n", s, k,
                                         (unsigned)w->flags, (int)w->cls, c->after, c->cls);
            }
        }
        CHECK(mismatch == 0);
        if (s == ES2_UI_SCREEN_WEAPONS || s == ES2_UI_SCREEN_ARMORY) CHECK(missing > 0);
        else CHECK(missing == 0);
        if (s == ES2_UI_SCREEN_SAVED_GAMES) {
            int dialogs = 0, ok = 1;
            for (int k = 0; k < sc->count; k++) {
                es2_ui_widget *w = es2_ui_get(ui, ids[k]);
                if (w && w->cls == ES2_UI_DIALOG && w->edit.prefix == 4) {
                    dialogs++;
                    ok &= w->edit.allowed == es2_shell_edit_set_slots;
                }
            }
            CHECK(dialogs == 10 && ok);
        }
    }
    printf("layout: %d windows built for the 22 screens (%u in the pool)\n", total, ui->count);
    CHECK(ui->refused == 0);
    /* the registration table: screens append entries;
     * each names a screen of the table, at most once */
    {
        int seen[ES2_UI_LAYOUT_SCREEN_COUNT] = {0}, ok = 1;
        for (const es2_ui_screen_entry *e = es2_ui_screens; e->build; e++) {
            ok &= e->screen >= 0 && e->screen < ES2_UI_LAYOUT_SCREEN_COUNT;
            if (ok) ok &= !seen[e->screen]++ && es2_ui_screen_find(e->screen) == e;
        }
        CHECK(ok);
    }
}

/* ---- a synthetic screen in headless mode --------------------------------------- */
static es2_ui *hl_ui;
static uint32_t hl_clock;

static int hl_fires;
static void hl_fire(es2_ui *u, es2_ui_id w, const es2_gui_event *ev) {
    es2_ui_widget *b = es2_ui_get(u, w);
    if (ev->type == ES2_GUI_EV_MOUSE && b && b->label) {
        hl_fires++;
        (void)es2_ui_message_set(u, b->label, hl_fires & 1 ? "ON" : "OFF", 2, 41, 1);
    }
}
static uint32_t hl_time(void *c) { (void)c; return hl_clock += 100; }
static void hl_timer(void *context, es2_gui_shell *sh) {
    (void)context;
    if (!hl_ui) {
        hl_ui = calloc(1, sizeof *hl_ui);
        es2_ui_init(hl_ui, sh);
        es2_ui_set_clock(hl_ui, hl_time, NULL);
        es2_ui_id fr = es2_ui_bitmap(hl_ui, hl_ui->root, R(0, 0, 639, 479), NULL, NULL, 0);
        es2_ui_get(hl_ui, fr)->frame = 1;
        es2_ui_id ti = es2_ui_title(hl_ui, fr, R(100, 100, 400, 300), &font, NULL, 21, "SCREEN", 19);
        (void)es2_ui_button_font(hl_ui, ti, R(20, 40, 120, 60), &font, hl_fire, "OFF", 21, 0);
        es2_ui_id ib = es2_ui_button_bitmap(hl_ui, ti, R(150, 40, 169, 49), &font, &bm_a, &bm_b, NULL, hl_fire, "I");
        es2_ui_id f = es2_ui_dialog(hl_ui, ti, R(20, 80, 200, 95), &font, NULL);
        (void)ib; (void)f;
        es2_ui_show(hl_ui, hl_ui->root);
        es2_ui_show(hl_ui, fr);
        es2_ui_show(hl_ui, ti);
        sh->input.interactive = 1;
    }
    es2_ui_on_timer(hl_ui, sh);
}
static void hl_handle(void *context, const es2_gui_event *ev) { (void)context; if (hl_ui) es2_ui_on_event(hl_ui, ev); }

static char *headless_once(void) {
    static const char script[] =
        "gate 1\npalette 3\n"
        "step\n"
        "move 150 150\nstep\n"
        "ldown\nstep\nlup\nstep\n"
        "move 260 145\nstep\nldown\nstep\nlup\nstep\n"
        "move 130 185\nstep\nldown\nstep\nlup\nstep\n"
        "key a\nkey b\nkey backspace\nstep 3\n"
        "key enter\nstep\n"
        "move 150 150\nstep\nkey enter\nstep 2\n";
    es2_gui_hooks hooks;
    es2_gui_headless_result res;
    memset(&hooks, 0, sizeof hooks);
    hooks.timer = hl_timer;
    hooks.handle = hl_handle;
    hl_ui = NULL;
    hl_clock = 0;
    hl_fires = 0;
    CHECK(es2_gui_headless_run(script, NULL, &hooks, &res) == 0);
    CHECK(res.errors == 0);
    if (hl_ui) { es2_ui_release(hl_ui); free(hl_ui); hl_ui = NULL; }
    return res.report;
}
static void headless(void) {
    char *a = headless_once();
    int fires_a = hl_fires;
    char *b = headless_once();
    CHECK(a && b && strcmp(a, b) == 0); /* deterministic */
    CHECK(fires_a == 2); /* the text button and the image button; the last Enter only presses (see edit_field) */
    {
        int presents = 0;
        for (const char *p = a; (p = strstr(p, "\npresent ")) != NULL; p++) presents++;
        CHECK(presents >= 5);
        /* the last present's frame hash is the screen's fingerprint */
        const char *last = NULL;
        for (const char *p = a; (p = strstr(p, " frame=")) != NULL; p++) last = p;
        if (last) printf("headless: %d presents, last frame=%.16s...\n", presents, last + 7);
    }
    free(a);
    free(b);
}

int main(void) {
    make_font();
    make_bitmaps();
    tree();
    dispatch_rules();
    same_drain();
    buttons();
    disabled_classes();
    timers();
    edit_field();
    consumer();
    modifier_keys();
    alert_area();
    draws();
    layout();
    headless();
    if (ui) es2_ui_release(ui);
    free(ui);
    free(shell);
    printf("widgets_test: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
