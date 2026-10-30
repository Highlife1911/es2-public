/* VSHELL's window framework (docs/reverse-engineering/vshell_widgets.md,
 * sections 2, 3 and 7.3 and "widget toolkit"). */
#include "es2/shell/gui/widgets.h"

#include <stdlib.h>
#include <string.h>

#include "es2/shell/gui/canvas.h"

#define W(ui, id) (&(ui)->w[(id) - 1])
#define FILL_COLOUR 0x10 /* the brush {0, 0x10} of the frame fills and the erase */

/* ---- drawing context (0x41fbd9 -> display slot 0 0x41fe84) ---------------- */

typedef struct dc {
    es2_gui_frame *frame;
    es2_gui_rect clip;   /* the window's absolute rectangle, clip mode 1 */
    int32_t ox, oy;      /* offset mode 1: window-relative coordinates */
    uint32_t pen;        /* the pen {kind 0, colour dword} at context +0x228 */
} dc;

static dc begin_draw(es2_ui *ui, const es2_ui_widget *w) {
    dc d;
    es2_gui_invalidate_window(&ui->shell->frame, w->abs);
    d.frame = &ui->shell->frame;
    d.clip = w->abs;
    d.ox = w->abs.left;
    d.oy = w->abs.top;
    d.pen = 0;
    return d;
}

static void put(const dc *d, int32_t x, int32_t y, uint8_t colour) {
    x += d->ox;
    y += d->oy;
    if (x < d->clip.left || x > d->clip.right || y < d->clip.top || y > d->clip.bottom) return;
    if (x < 0 || y < 0 || x >= ES2_GUI_WIDTH || y >= ES2_GUI_HEIGHT) return; /* own: the frame */
    d->frame->pixels[y * ES2_GUI_WIDTH + x] = colour;
}

/* Pixel 0x459cdc with the pen: a colour dword whose high byte is 0 writes
 * its low byte (0x459d60); high byte 4 selects a ramp entry (0x459d2f, not
 * used by any widget: refused here); any other value draws nothing
 * (0x459d1a). The line 0x455624 reads the pen the same way. */
static int pen_plain(const dc *d) { return (d->pen >> 24) == 0; }
static void pixel(const dc *d, int32_t x, int32_t y) {
    if (pen_plain(d)) put(d, x, y, (uint8_t)d->pen);
}

/* Line 0x455624 with the pen, both end points drawn. The widgets draw only
 * horizontal, vertical and 45-degree lines, where every rasteriser agrees;
 * other slopes step as Bresenham's (H, never used by a widget). */
static void line(const dc *d, int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
    if (!pen_plain(d)) return;
    int32_t dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y1 - y0 : y0 - y1;
    int32_t sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx - dy;
    for (;;) {
        pixel(d, x0, y0);
        if (x0 == x1 && y0 == y1) break;
        int32_t e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx) { err += dx; y0 += sy; }
    }
}

/* Rectangle fill 0x4576a4 over the callers' min/max-normalised corners
 * (0x40ad42..0x40ada2 and the like), inclusive, clipped. recolour = the
 * kind-6 brush with an identity table except table[from] = to. */
static void fill(const dc *d, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint8_t colour, int recolour,
                 uint8_t from) {
    int32_t l = (x0 < x1 ? x0 : x1) + d->ox, r = (x0 < x1 ? x1 : x0) + d->ox;
    int32_t t = (y0 < y1 ? y0 : y1) + d->oy, b = (y0 < y1 ? y1 : y0) + d->oy;
    if (l < d->clip.left) l = d->clip.left;
    if (r > d->clip.right) r = d->clip.right;
    if (t < d->clip.top) t = d->clip.top;
    if (b > d->clip.bottom) b = d->clip.bottom;
    if (l < 0) l = 0;
    if (t < 0) t = 0;
    if (r >= ES2_GUI_WIDTH) r = ES2_GUI_WIDTH - 1;
    if (b >= ES2_GUI_HEIGHT) b = ES2_GUI_HEIGHT - 1;
    for (int32_t y = t; y <= b; y++)
        for (int32_t x = l; x <= r; x++) {
            uint8_t *p = &d->frame->pixels[y * ES2_GUI_WIDTH + x];
            if (!recolour) *p = colour;
            else if (*p == from) *p = colour;
        }
}

/* A bitmap at the window-relative point (0x4537e8 / GLBitmap draw), clipped
 * to the window (and the frame). */
static void blit(const dc *d, const es2_bitmap *b, int32_t x, int32_t y, unsigned mirror) {
    es2_frame view;
    es2_rect clip;
    if (!b) return;
    view = es2_gui_frame_draw_view(d->frame);
    clip = es2_gui_rect_draw(d->clip);
    if (clip.left < 0) clip.left = 0;
    if (clip.top < 0) clip.top = 0;
    if (clip.right >= ES2_GUI_WIDTH) clip.right = ES2_GUI_WIDTH - 1;
    if (clip.bottom >= ES2_GUI_HEIGHT) clip.bottom = ES2_GUI_HEIGHT - 1;
    if (clip.left > clip.right || clip.top > clip.bottom) return;
    (void)es2_draw_blit(&view, b, x + d->ox, y + d->oy, &clip, mirror);
}

static int32_t width_of(const es2_ui_widget *w) { return w->rect.right - w->rect.left; }
static int32_t height_of(const es2_ui_widget *w) { return w->rect.bottom - w->rect.top; }

/* ---- class draws --------------------------------------------------------- */

/* Original: VSHELL.EXE 0x40acd6 (ESRect frame) */
static void draw_frame(es2_ui *ui, es2_ui_widget *w, int filled) {
    dc d;
    int32_t ww = width_of(w), h = height_of(w);
    if (!w->frame) return;
    d = begin_draw(ui, w);
    if (filled) fill(&d, 1, 1, ww - 1, h - 1, FILL_COLOUR, 0, 0);
    d.pen = (uint32_t)w->colour;
    line(&d, 1, 0, ww - 1, 0);
    line(&d, ww, 1, ww, h - 1);
    line(&d, 1, h, ww - 1, h);
    line(&d, 0, 1, 0, h - 1);
    pixel(&d, 1, 1);
    pixel(&d, ww - 1, 1);
    pixel(&d, ww - 1, h - 1);
    pixel(&d, 1, h - 1);
}

/* Original: VSHELL.EXE 0x40b9e9 (label draw; y = -1 centres the text) */
static void draw_label(es2_ui *ui, es2_ui_widget *l, int32_t y) {
    es2_shell_label label;
    es2_shell_text_frame view;
    if (l->flags & ES2_UI_HIDDEN) return; /* 0x40b9fb, before 0x41fbd9 */
    (void)begin_draw(ui, l);
    memset(&label, 0, sizeof label);
    label.rect = es2_gui_rect_text(l->abs);
    label.font = l->font;
    memcpy(label.text, l->text, sizeof label.text);
    label.align = l->align;
    label.colour = (uint8_t)l->text_colour;
    label.background = l->opaque != 0;
    label.background_colour = (uint8_t)l->background;
    view = es2_gui_frame_text_view(&ui->shell->frame);
    es2_shell_label_draw(&view, &label, y);
}

/* The button label line 0x40a240..0x40a2b3 / 0x40a631..0x40a6a4:
 * (H + h + 1) / 2 + 1 while pressed and enabled, else - 1. */
static void draw_button_label(es2_ui *ui, es2_ui_widget *w) {
    es2_ui_widget *l = es2_ui_get(ui, w->label);
    int32_t y;
    if (!l || !l->font) return;
    y = (l->font->height + height_of(w) + 1) / 2 + (w->pressed && w->enabled ? 1 : -1);
    draw_label(ui, l, y);
}

/* Original: VSHELL.EXE 0x40a129 */
static void draw_button_font(es2_ui *ui, es2_ui_widget *w) {
    dc d = begin_draw(ui, w);
    int32_t ww = width_of(w), h = height_of(w);
    draw_frame(ui, w, 1);
    d.pen = (uint32_t)w->colour;
    line(&d, 2, 1, ww - 2, 1);
    line(&d, ww - 1, 2, ww - 1, h - 2);
    line(&d, ww - 2, h - 1, 2, h - 1);
    line(&d, 1, h - 2, 1, 2);
    draw_button_label(ui, w);
}

/* Original: VSHELL.EXE 0x40a60d (image button) and 0x40a81d (radio) */
static void draw_button_bitmap(es2_ui *ui, es2_ui_widget *w) {
    dc d = begin_draw(ui, w);
    int on = w->cls == ES2_UI_RADIO ? w->checked : w->pressed;
    const es2_bitmap *b = on ? w->bitmap_active : w->bitmap_normal;
    if (b) blit(&d, b, 0, 0, 0);
    else ui->refused++; /* VSHELL calls through a null bitmap */
    draw_button_label(ui, w);
}

static void dither(const dc *d, int32_t from_y, int32_t ww, int32_t h) {
    int32_t phase = 0;
    for (int32_t y = from_y; y < h; y++) {
        for (int32_t x = phase + 1; x < ww; x += 2) pixel(d, x, y);
        phase = 1 - phase;
    }
}

static void corners(const dc *d, int32_t ww, int32_t h) {
    pixel(d, 1, 1);
    pixel(d, ww - 1, 1);
    pixel(d, ww - 1, h - 1);
    pixel(d, 1, h - 1);
}

/* Original: VSHELL.EXE 0x40af56 */
static void draw_region_fill(es2_ui *ui, es2_ui_widget *w) {
    dc d = begin_draw(ui, w);
    int32_t ww = width_of(w), h = height_of(w);
    if (w->f59) fill(&d, 1, 1, ww - 1, h - 1, FILL_COLOUR, 0, 0);
    draw_frame(ui, w, w->f59);
    d.pen = (uint32_t)w->f55;
    dither(&d, 1, ww, h);
    d.pen = (uint32_t)w->colour;
    corners(&d, ww, h);
}

/* Original: VSHELL.EXE 0x40b1d7 */
static void draw_title(es2_ui *ui, es2_ui_widget *w) {
    dc d = begin_draw(ui, w);
    int32_t ww = width_of(w), h = height_of(w), bar = w->f61;
    if (w->f59) fill(&d, 1, 1, ww - 1, h - 1, FILL_COLOUR, 0, 0);
    draw_frame(ui, w, w->f59);
    d.pen = (uint32_t)w->f55;
    for (int32_t y = 1; y < bar; y++) line(&d, 1, y, ww - 1, y);
    if (w->f65) {
        int32_t edi = -5;
        d.pen = 0x0d;
        for (int group = 0; group <= 25; group++) { /* 0x40b332..0x40b378: 26 groups of 14 lines, 28 apart */
            int32_t x = edi;
            for (; x < edi + 14; x++) line(&d, x, bar - 1, bar + x - 2, 1);
            edi = x + 14;
        }
        d.pen = (uint32_t)w->colour;
        line(&d, ww, 1, ww, bar);
        line(&d, 0, 1, 0, bar);
        d.pen = (uint32_t)w->f55;
        for (int32_t y = 1; y < bar; y++) line(&d, w->f6d, y, w->f71, y);
    }
    if (!w->f59) {
        d.pen = (uint32_t)w->f5d;
        dither(&d, bar, ww, h);
    }
    d.pen = (uint32_t)w->colour;
    corners(&d, ww, h);
    line(&d, 0, bar, ww, bar);
}

/* Original: VSHELL.EXE 0x40bd22 */
static void draw_bitmap(es2_ui *ui, es2_ui_widget *w) {
    dc d = begin_draw(ui, w);
    if (w->bitmap) blit(&d, w->bitmap, w->ox, w->oy, 0);
    draw_frame(ui, w, 0);
}

/* Original: VSHELL.EXE 0x40bf2c */
static void draw_grid(es2_ui *ui, es2_ui_widget *w) {
    dc d = begin_draw(ui, w);
    int32_t ww = width_of(w), h = height_of(w);
    draw_frame(ui, w, 1);
    d.pen = (uint32_t)(int32_t)w->grid_colour; /* movsx of the word */
    if (w->grid_lines) {
        for (int32_t y = 16; y < h; y += 16) line(&d, 0, y, ww, y);
        for (int32_t x = 16; x < ww; x += 16) line(&d, x, 0, x, h);
    }
    if (w->grid < 0) return;
    for (int i = 0; i < ES2_UI_GRID_LAYERS; i++) {
        const es2_ui_layer *k = &ui->layers[w->grid][i];
        if (!k->bitmap) continue;
        blit(&d, k->bitmap, k->x, k->y, (unsigned)k->mirror);
        for (int p = 0; p < 10; p++) {
            if (k->to[p] == 0x10) continue;
            if (k->from[p] < 0 || k->from[p] > 255) { /* VSHELL writes past its 256-byte table */
                ui->refused++;
                continue;
            }
            fill(&d, k->x, k->y, k->x + k->bitmap->width, k->y + k->bitmap->height, (uint8_t)k->to[p], 1,
                 (uint8_t)k->from[p]);
        }
    }
}

/* Original: VSHELL.EXE 0x40cac3 */
static void draw_arm(es2_ui *ui, es2_ui_widget *w) {
    dc d = begin_draw(ui, w);
    int32_t ww = width_of(w), h = height_of(w);
    draw_frame(ui, w, 1);
    d.pen = (uint32_t)w->f59;
    for (int32_t y = w->f55; y < h; y++) line(&d, 1, y, ww - 1, y);
    d.pen = (uint32_t)w->colour;
    line(&d, 1, w->f55, ww - 1, w->f55);
    draw_frame(ui, w, 0);
    if (w->f5d) {
        d.pen = (uint32_t)w->colour;
        line(&d, 2, 1, ww - 1, 1);
        line(&d, ww - 1, 2, ww - 1, h - 2);
        line(&d, ww - 1, h - 1, 2, h - 1);
        line(&d, 1, h - 2, 1, 2);
    }
}

/* Original: VSHELL.EXE 0x40abb0 */
static void draw_quad(es2_ui *ui, es2_ui_widget *w) {
    dc d = begin_draw(ui, w);
    es2_ui_widget *c0 = es2_ui_get(ui, w->columns[0]);
    fill(&d, 1, 1, width_of(w) - 1, height_of(w) - 1, FILL_COLOUR, 0, 0);
    draw_frame(ui, w, 1);
    if (!c0 || !c0->font) return;
    for (int i = 0; i < 4; i++) {
        es2_ui_widget *c = es2_ui_get(ui, w->columns[i]);
        if (c) draw_label(ui, c, -1);
    }
}

/* Original: VSHELL.EXE 0x409f61 (hidden WinButton) */
static void draw_erase(es2_ui *ui, es2_ui_widget *w) {
    dc d = begin_draw(ui, w);
    fill(&d, 0, 0, width_of(w), height_of(w), FILL_COLOUR, 0, 0);
}

/* Original: VSHELL.EXE 0x40d0f0 (ESPal): the erase, then at once the
 * display flush 0x41fe49 (the main loop's present step, 0x40d193). */
static void draw_pal(es2_ui *ui, es2_ui_widget *w) {
    es2_gui_shell *sh = ui->shell;
    draw_erase(ui, w);
    if (sh->frame.dirty_count) {
        es2_gui_rect rects[ES2_GUI_DIRTY_MAX];
        int n = sh->frame.dirty_count;
        memcpy(rects, sh->frame.dirty, (size_t)n * sizeof rects[0]);
        es2_gui_flush(&sh->screen, &sh->frame);
        sh->presents++;
        if (sh->platform.present) sh->platform.present(sh->platform.context, sh, ES2_GUI_PRESENT_DIRTY, rects, n);
    }
}

/* Original: VSHELL.EXE 0x40cfcb */
static void draw_anim(es2_ui *ui, es2_ui_widget *w) {
    dc d = begin_draw(ui, w);
    const es2_bitmap *b = w->frame_index >= 0 && w->frame_index < ES2_UI_ANIM_FRAMES ? w->frames[w->frame_index] : NULL;
    if (!b) { /* 0x40cfea: assertion, then a call through the null frame */
        ui->refused++;
        return;
    }
    blit(&d, b, 0, 0, 0);
}

static void draw_dialog(es2_ui *ui, es2_ui_widget *w) { /* 0x40c6ff */
    es2_shell_text_frame view;
    (void)begin_draw(ui, w);
    view = es2_gui_frame_text_view(&ui->shell->frame);
    es2_shell_edit_draw(&view, &w->edit);
}

/* VSHELL has no window here: each line of the page is a label (0x40d37d)
 * whose draw 0x40b9e9 queues its own row (0x41fbd9 -> 0x41fe84), so only
 * the rows of the shown lines enter the dirty list. */
static void draw_report(es2_ui *ui, es2_ui_widget *w) {
    es2_shell_text_frame view;
    int32_t H, rows;
    if (!w->report || !w->font) return;
    H = w->font->height;
    rows = H > 0 ? (w->abs.bottom - w->abs.top) / H : 0;
    for (size_t i = 0; rows > 0 && i < w->report->lines; i++) {
        int32_t j = (int32_t)(i % (size_t)rows);
        if ((int64_t)(i / (size_t)rows) != w->page) continue;
        es2_gui_invalidate_window(&ui->shell->frame,
                                  (es2_gui_rect){w->abs.left, w->abs.top + j * H, w->abs.right, w->abs.top + j * H + H});
    }
    view = es2_gui_frame_text_view(&ui->shell->frame);
    (void)es2_shell_report_draw(&view, es2_gui_rect_text(w->abs), w->font, w->report, w->page, NULL);
}

void es2_ui_draw(es2_ui *ui, es2_ui_id id) {
    es2_ui_widget *w = es2_ui_get(ui, id);
    if (!w) return;
    switch (w->cls) {
    case ES2_UI_RECT: draw_frame(ui, w, 1); break;              /* 0x40af09 */
    case ES2_UI_BUTTON_FONT: draw_button_font(ui, w); break;
    case ES2_UI_BUTTON_BITMAP:
    case ES2_UI_RADIO: draw_button_bitmap(ui, w); break;
    case ES2_UI_REGION_FILL: draw_region_fill(ui, w); break;
    case ES2_UI_TITLE: draw_title(ui, w); break;
    case ES2_UI_ALERT: (void)begin_draw(ui, w); draw_title(ui, w); break; /* 0x40b7d1 */
    case ES2_UI_MESSAGE: draw_label(ui, w, -1); break;
    case ES2_UI_BITMAP: draw_bitmap(ui, w); break;
    case ES2_UI_GRID: draw_grid(ui, w); break;
    case ES2_UI_DIALOG: draw_dialog(ui, w); break;
    case ES2_UI_ARM: draw_arm(ui, w); break;
    case ES2_UI_ANIM2: draw_anim(ui, w); break;
    case ES2_UI_PAL: draw_pal(ui, w); break;
    case ES2_UI_QUAD: draw_quad(ui, w); break;
    case ES2_UI_REPORT: draw_report(ui, w); break;
    default: break; /* Window, ESWindow: no draw */
    }
}

/* ---- queue state: leave, enter, dispatch ---------------------------------- */

static es2_gui_event make_event(uint32_t type, es2_ui_id target, int32_t x, int32_t y) {
    es2_gui_event ev;
    memset(&ev, 0, sizeof ev);
    ev.type = type;
    ev.target = target;
    ev.x = x;
    ev.y = y;
    return ev;
}

static void post(es2_ui *ui, uint32_t type, es2_ui_id target) {
    (void)es2_gui_post(&ui->shell->input, make_event(type, target, 0, 0));
}

static void handle(es2_ui *ui, es2_ui_id id, const es2_gui_event *ev);

/* Original: VSHELL.EXE 0x46a274 (0x46a2f8 handling window, 0x46a324 position) */
static void dispatch(es2_ui *ui, es2_gui_event ev) {
    es2_ui_id h = ev.target;
    const es2_ui_widget *t = es2_ui_get(ui, ev.target);
    if (!t) return;
    while (h && !(W(ui, h)->mask & ev.type)) h = W(ui, h)->parent;
    if (!h) return; /* deleted */
    ev.x -= t->abs.left; /* the target's origin, not the handling window's */
    ev.y -= t->abs.top;
    handle(ui, h, &ev);
}

/* Original: VSHELL.EXE 0x46a0b4 */
static void leave(es2_ui *ui) {
    if (ui->capture || !ui->hover) return;
    dispatch(ui, make_event(ES2_GUI_EV_LEAVE, ui->hover, ui->cursor_x, ui->cursor_y));
    ui->hover = 0;
    ui->hover_display = 0;
}

/* Original: VSHELL.EXE 0x46a15c (window 0: hit test at the cursor) */
static void enter(es2_ui *ui, es2_ui_id window) {
    if (ui->capture) return;
    if (!window) window = es2_ui_hit_test(ui, ui->display, ui->cursor_x, ui->cursor_y);
    if (window == ui->hover) return;
    ui->hover = window;
    if (!window) return;
    ui->hover_display = 1;
    dispatch(ui, make_event(ES2_GUI_EV_ENTER, window, ui->cursor_x, ui->cursor_y));
    /* 0x46a240..0x46a260: the window cursor (none of the own windows sets one) */
}

static int inside(const es2_gui_rect *r, int32_t x, int32_t y) { /* 0x41f906 */
    return r->left <= x && r->top <= y && r->right >= x && r->bottom >= y;
}

es2_ui_id es2_ui_hit_test(const es2_ui *ui, es2_ui_id id, int32_t x, int32_t y) {
    const es2_ui_widget *w;
    if (!ui || !id || id > ui->count) return 0;
    w = &ui->w[id - 1];
    if ((w->flags & ES2_UI_HIDDEN) || !inside(&w->abs, x, y)) return 0;
    for (es2_ui_id c = w->first; c; c = ui->w[c - 1].next) {
        es2_ui_id hit = es2_ui_hit_test(ui, c, x, y);
        if (hit) return hit;
    }
    return id;
}

/* Original: VSHELL.EXE 0x469ee4 (per popped event) */
void es2_ui_on_event(void *context, const es2_gui_event *in) {
    es2_ui *ui = context;
    es2_gui_event ev;
    if (!ui || !in) return;
    ev = *in;
    if (ev.target > ui->count) ev.target = 0; /* not one of ours */
    if (ev.type == ES2_GUI_EV_MOUSE && ev.code == ES2_GUI_MOUSE_MOVE) {
        if (ui->capture) {
            ev.target = ui->hover;
        } else {
            ev.target = es2_ui_hit_test(ui, ui->display, ev.x, ev.y);
            if (!ui->hover_display || ev.target != ui->hover) {
                leave(ui);
                if (ev.target) enter(ui, ev.target);
            }
        }
        ui->cursor_x = ev.x;
        ui->cursor_y = ev.y;
    } else if (!ev.target) {
        ev.target = ui->hover;
    }
    if (ev.target) dispatch(ui, ev);
}

void es2_ui_fire(es2_ui *ui, es2_ui_id id, const es2_gui_event *ev) {
    es2_ui_widget *w = es2_ui_get(ui, id);
    if (w && w->handler) w->handler(ui, id, ev);
}

void es2_ui_release_capture(es2_ui *ui) { /* 0x46a01c */
    es2_ui_id now;
    if (!ui) return;
    ui->capture = 0;
    now = es2_ui_hit_test(ui, ui->display, ui->cursor_x, ui->cursor_y);
    if (now != ui->hover) {
        leave(ui);
        enter(ui, now);
    }
}

/* ---- timers (0x46a3f0, 0x46a478, 0x46a4ac, 0x46a508) ---------------------- */

static uint32_t clock_now(es2_ui *ui) { return ui->clock ? ui->clock(ui->clock_context) : ui->now; }

int es2_ui_timer_add(es2_ui *ui, es2_ui_id window, int32_t delay, int32_t period, uint32_t arg) {
    es2_ui_timer *t;
    if (!ui) return 0;
    if (ui->timer_count >= ES2_UI_TIMERS) { /* 0x46a403: severity-4 assertion */
        ui->refused++;
        return 0;
    }
    t = &ui->timers[ui->timer_count++];
    t->window = window;
    t->remaining = delay;
    t->period = period;
    t->arg = arg;
    return 1;
}

static void timer_drop(es2_ui *ui, int i) { /* 0x46a4ac: the last record moves into the hole */
    ui->timers[i] = ui->timers[ui->timer_count - 1];
    ui->timer_count--;
}

void es2_ui_timer_remove(es2_ui *ui, es2_ui_id window) {
    if (!ui) return;
    /* 0x46a481..0x46a4a5: the index advances past the record moved into the
     * hole, so a second timer of the window moved there survives. */
    for (int i = 0; i < ui->timer_count; i++)
        if (ui->timers[i].window == window) timer_drop(ui, i);
}

void es2_ui_timer_poll(es2_ui *ui) {
    uint32_t now;
    int32_t elapsed;
    if (!ui) return;
    now = clock_now(ui);
    elapsed = (int32_t)(now - ui->timer_last);
    ui->timer_last = now;
    for (int i = 0; i < ui->timer_count; i++) {
        es2_ui_timer *t = &ui->timers[i];
        if (!t->window) continue;
        t->remaining -= elapsed;
        if (t->remaining <= 0) {
            es2_gui_event ev = make_event(ES2_GUI_EV_TIMER, t->window, 0, 0);
            ev.code = (uint16_t)t->arg; /* +0x24 (a dword in VSHELL; every caller passes 0) */
            (void)es2_gui_post(&ui->shell->input, ev);
            t->remaining = t->period;
        }
        if (!t->period) timer_drop(ui, i); /* checked whether or not it fired (0x46a59e) */
    }
}

/* ---- the WinConsumer 0x409996 ---------------------------------------------- */

static void queue_raw(es2_gui_input *in, const es2_gui_event *ev) {
    if (in->queue_count == ES2_GUI_QUEUE_MAX) {
        in->queue_overflows++;
        return;
    }
    in->queue[(in->queue_head + in->queue_count) % ES2_GUI_QUEUE_MAX] = *ev;
    in->queue_count++;
}

/* 0x46863c / 0x468564: move the system pointer by (dx, dy). */
static void nudge(es2_ui *ui, int32_t dx, int32_t dy) {
    es2_gui_input *in = &ui->shell->input;
    int32_t x = in->cursor_x + dx, y = in->cursor_y + dy;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= ES2_GUI_WIDTH) x = ES2_GUI_WIDTH - 1;
    if (y >= ES2_GUI_HEIGHT) y = ES2_GUI_HEIGHT - 1;
    if (ui->warp) {
        ui->warp(ui, x, y);
    } else { /* SetCursorPos: Windows sends the window a WM_MOUSEMOVE */
        es2_gui_mouse_message_in(in, ES2_GUI_WM_MOUSEMOVE, x, y, in->buttons & 1, (in->buttons >> 1) & 1);
    }
}

static void consume(es2_ui *ui, const es2_gui_event *ev) {
    if (ev->type != ES2_GUI_EV_COMMAND) return;
    switch (ev->code) {
    case 0x04: nudge(ui, -8, 0); break; /* Left  0x409bae */
    case 0x05: nudge(ui, 0, -8); break; /* Up    0x409c0a */
    case 0x06: nudge(ui, 8, 0); break;  /* Right 0x409bdc */
    case 0x07: nudge(ui, 0, 8); break;  /* Down  0x409c35 */
    case 0x0a:                          /* Enter down 0x409aea: left down at the hover window */
    case 0x1d: {                        /* Enter up   0x409b4c: left up */
        es2_gui_event click = make_event(ES2_GUI_EV_MOUSE, ui->hover, 0, 0);
        click.code = ev->code == 0x0a ? ES2_GUI_MOUSE_LEFT_DOWN : ES2_GUI_MOUSE_LEFT_UP;
        (void)es2_gui_post(&ui->shell->input, click);
        break;
    }
    case 0x11: es2_ui_quit_box(ui); break; /* Ctrl+Q 0x409c60 */
    default: break;                        /* logged only */
    }
}

void es2_ui_on_timer(void *context, es2_gui_shell *shell) {
    es2_ui *ui = context;
    es2_gui_input *in;
    unsigned n;
    if (!ui || !shell) return;
    in = &shell->input;
    /* The device's events reach the queue through the consumer (it posts
     * each after acting on it, 0x409c9e): actions and the clicks it posts
     * keep that order. */
    n = in->queue_count;
    for (unsigned k = 0; k < n; k++) {
        es2_gui_event ev = in->queue[in->queue_head];
        in->queue_head = (in->queue_head + 1) % ES2_GUI_QUEUE_MAX;
        in->queue_count--;
        consume(ui, &ev);
        queue_raw(in, &ev);
    }
    es2_ui_timer_poll(ui);
}

void es2_ui_install(es2_ui *ui, es2_gui_hooks *hooks) {
    if (!ui || !hooks) return;
    hooks->context = ui;
    hooks->handle = es2_ui_on_event;
    hooks->timer = es2_ui_on_timer;
}

void es2_ui_quit_box(es2_ui *ui) {
    if (!ui) return;
    es2_ui_release_capture(ui);
    if (ui->quit_box) es2_ui_modal_show(ui, ui->quit_box);
}

/* ---- show, hide, redraw ------------------------------------------------- */

static es2_ui_id oldest_child(const es2_ui *ui, es2_ui_id id) { /* 0x41f7b7 */
    es2_ui_id c = ui->w[id - 1].first;
    if (!c) return 0;
    while (ui->w[c - 1].next) c = ui->w[c - 1].next;
    return c;
}

void es2_ui_show(es2_ui *ui, es2_ui_id id) { /* 0x41f93a */
    es2_ui_widget *w = es2_ui_get(ui, id), *p;
    if (!w || !(w->flags & ES2_UI_HIDDEN)) return;
    p = es2_ui_get(ui, w->parent);
    if (p && (p->flags & ES2_UI_HIDDEN)) {
        w->flags |= ES2_UI_HIDDEN | ES2_UI_PENDING;
        return;
    }
    w->flags &= ~(ES2_UI_HIDDEN | ES2_UI_PENDING);
    if (ui->hover_display && inside(&w->abs, ui->cursor_x, ui->cursor_y)) leave(ui);
    post(ui, ES2_GUI_EV_SHOWN, id);
    post(ui, ES2_GUI_EV_REDRAW, id);
    for (es2_ui_id c = oldest_child(ui, id); c; c = ui->w[c - 1].newer) /* oldest first, 0x41fa7d */
        if (ui->w[c - 1].flags & ES2_UI_PENDING) es2_ui_show(ui, c);
    if (!ui->hover) enter(ui, 0);
}

void es2_ui_hide(es2_ui *ui, es2_ui_id id) { /* 0x41fabd */
    es2_ui_widget *w = es2_ui_get(ui, id), *p;
    if (!w) return;
    if (w->flags & ES2_UI_HIDDEN) {
        w->flags &= ~ES2_UI_PENDING;
        return;
    }
    w->flags |= ES2_UI_HIDDEN;
    p = es2_ui_get(ui, w->parent);
    if (p && (p->flags & ES2_UI_HIDDEN)) w->flags |= ES2_UI_PENDING;
    if (ui->hover_display && inside(&w->abs, ui->cursor_x, ui->cursor_y)) leave(ui);
    for (es2_ui_id c = w->first; c; c = ui->w[c - 1].next) /* newest first */
        if (!(ui->w[c - 1].flags & ES2_UI_HIDDEN)) es2_ui_hide(ui, c);
    post(ui, ES2_GUI_EV_HIDDEN, id);
    if (!ui->hover) enter(ui, 0);
}

void es2_ui_modal_show(es2_ui *ui, es2_ui_id box) { /* 0x40b7e9 */
    es2_ui_widget *w = es2_ui_get(ui, box);
    if (!w) return;
    es2_ui_show(ui, w->parent);
    es2_ui_show(ui, box);
}

void es2_ui_modal_hide(es2_ui *ui, es2_ui_id box) { /* 0x40b808 */
    es2_ui_widget *w = es2_ui_get(ui, box);
    if (!w) return;
    es2_ui_hide(ui, box);
    es2_ui_hide(ui, w->parent);
}

void es2_ui_redraw_tree(es2_ui *ui, es2_ui_id id) { /* 0x41f3ed */
    es2_ui_widget *w = es2_ui_get(ui, id);
    if (!w || (w->flags & ES2_UI_HIDDEN)) return;
    post(ui, ES2_GUI_EV_REDRAW, id);
    for (es2_ui_id c = w->first; c; c = ui->w[c - 1].next) es2_ui_redraw_tree(ui, c);
}

void es2_ui_set_enabled(es2_ui *ui, es2_ui_id id, int enabled) {
    es2_ui_widget *w = es2_ui_get(ui, id);
    if (w) w->enabled = enabled;
}

/* ---- class handlers (vtable slot 0) ----------------------------------------- */

static int gates_open(const es2_ui *ui) { /* [0x470eb0], [0x470fb0] */
    return !ui->shell->input.movie_open && !ui->gate;
}

static void click_sound(es2_ui *ui) {
    if (ui->click_sound) ui->click_sound(ui);
}

/* Original: VSHELL.EXE 0x409d8a */
static void handle_winbutton(es2_ui *ui, es2_ui_id id, const es2_gui_event *ev) {
    es2_ui_widget *w = W(ui, id);
    switch (ev->type) {
    case ES2_GUI_EV_HIDDEN: draw_erase(ui, w); break;
    case ES2_GUI_EV_REDRAW: es2_ui_draw(ui, id); break;
    case ES2_GUI_EV_LEAVE:
        if (w->pressed) {
            w->pressed = 0;
            es2_ui_draw(ui, id);
        }
        break;
    case ES2_GUI_EV_MOUSE:
        if (!w->enabled || !gates_open(ui)) break;
        switch (ev->code) {
        case ES2_GUI_MOUSE_LEFT_DOWN:
        case ES2_GUI_MOUSE_RIGHT_DOWN:
            w->pressed = 1;
            es2_ui_draw(ui, id);
            break;
        case ES2_GUI_MOUSE_LEFT_UP:
        case ES2_GUI_MOUSE_RIGHT_UP:
            if (!w->pressed) break;
            es2_ui_fire(ui, id, ev); /* fire, then unpress and redraw */
            w = W(ui, id);
            w->pressed = 0;
            es2_ui_draw(ui, id);
            break;
        default: break;
        }
        break;
    default: break;
    }
}

/* Original: VSHELL.EXE 0x40a0bf */
static void handle_button_font(es2_ui *ui, es2_ui_id id, const es2_gui_event *ev) {
    es2_ui_widget *w = W(ui, id);
    if (ev->type == ES2_GUI_EV_MOUSE &&
        (ev->code == ES2_GUI_MOUSE_LEFT_DOWN || ev->code == ES2_GUI_MOUSE_RIGHT_DOWN) && w->enabled && ui->sound &&
        !ev->synthetic)
        click_sound(ui);
    handle_winbutton(ui, id, ev);
}

/* Original: VSHELL.EXE 0x40a3a2 */
static void handle_button_bitmap(es2_ui *ui, es2_ui_id id, const es2_gui_event *ev) {
    es2_ui_widget *w = W(ui, id);
    switch (ev->type) {
    case ES2_GUI_EV_SHOWN:
        if (w->repeat) (void)es2_ui_timer_add(ui, id, 500, 500, 0);
        break; /* then 0x409d8a, which ignores type 1 */
    case ES2_GUI_EV_HIDDEN:
        if (w->repeat) es2_ui_timer_remove(ui, id);
        break; /* then 0x409d8a: erase */
    case ES2_GUI_EV_LEAVE:
        if (w->pressed && !w->latch) {
            w->pressed = 0;
            w->repeat_count = 0;
            es2_ui_draw(ui, id);
        }
        return;
    case ES2_GUI_EV_TIMER:
        if (w->repeat && !(w->flags & ES2_UI_HIDDEN) && w->pressed && w->enabled) {
            w->repeat_count++;
            es2_ui_draw(ui, id);
            es2_ui_fire(ui, id, ev);
            return;
        }
        break;
    case ES2_GUI_EV_MOUSE:
        if (!w->enabled || !gates_open(ui)) break;
        if (ev->code == ES2_GUI_MOUSE_LEFT_UP) {
            w->pressed = 0; /* no redraw unless auto-repeat (0x40a580) */
            if (w->repeat) {
                w->repeat_count = 0;
                es2_ui_draw(ui, id);
                es2_ui_fire(ui, id, ev);
            }
            return;
        }
        if (ev->code == ES2_GUI_MOUSE_LEFT_DOWN) {
            w->pressed = 1;
            if (ui->sound) click_sound(ui); /* no synthetic test here */
            w->pressed = 1;
            es2_ui_draw(ui, id);
            if (!w->repeat) es2_ui_fire(ui, id, ev);
            return;
        }
        break;
    default: break;
    }
    handle_winbutton(ui, id, ev);
}

/* Original: VSHELL.EXE 0x40a6e9 */
static void handle_radio(es2_ui *ui, es2_ui_id id, const es2_gui_event *ev) {
    es2_ui_widget *w = W(ui, id);
    if (ev->type == ES2_GUI_EV_LEAVE) {
        if (w->pressed && !w->latch) {
            w->pressed = 0;
            es2_ui_draw(ui, id);
        }
        return;
    }
    if (ev->type == ES2_GUI_EV_MOUSE && w->enabled) { /* no gate test */
        if (ev->code == ES2_GUI_MOUSE_LEFT_DOWN) {
            if (ui->sound) click_sound(ui);
            w->pressed = 1;
            es2_ui_draw(ui, id);
            es2_ui_fire(ui, id, ev);
            return;
        }
        if (ev->code == ES2_GUI_MOUSE_LEFT_UP) return; /* dropped */
    }
    handle_winbutton(ui, id, ev);
}

/* Original: VSHELL.EXE 0x40b6f3: the screen under the box is saved when it
 * is shown (0x459080) and put back when it is hidden (0x458ee4, opaque). */
static void alert_area(es2_ui *ui, es2_ui_widget *w, int restore) {
    int32_t ww = width_of(w) + 1, h = height_of(w) + 1;
    (void)begin_draw(ui, w);
    if (!w->saved) return;
    for (int32_t y = 0; y < h; y++) {
        int32_t fy = w->abs.top + y;
        if (fy < 0 || fy >= ES2_GUI_HEIGHT) continue;
        for (int32_t x = 0; x < ww; x++) {
            int32_t fx = w->abs.left + x;
            uint8_t *p, *s;
            if (fx < 0 || fx >= ES2_GUI_WIDTH) continue;
            p = &ui->shell->frame.pixels[fy * ES2_GUI_WIDTH + fx];
            s = &w->saved[(size_t)y * (size_t)ww + (size_t)x];
            if (restore) *p = *s;
            else *s = *p;
        }
    }
}

static void handle_alert(es2_ui *ui, es2_ui_id id, const es2_gui_event *ev) {
    es2_ui_widget *w = W(ui, id);
    if (ev->type == ES2_GUI_EV_SHOWN) alert_area(ui, w, 0);
    else if (ev->type == ES2_GUI_EV_HIDDEN) alert_area(ui, w, 1);
    else if (ev->type == ES2_GUI_EV_REDRAW) es2_ui_draw(ui, id);
}

/* Original: VSHELL.EXE 0x40b957 */
static void handle_message(es2_ui *ui, es2_ui_id id, const es2_gui_event *ev) {
    if (ev->type == ES2_GUI_EV_HIDDEN) es2_ui_fire(ui, id, ev);
    else if (ev->type == ES2_GUI_EV_REDRAW) draw_label(ui, W(ui, id), -1);
}

/* Original: VSHELL.EXE 0x40bc8a */
static void handle_bitmap(es2_ui *ui, es2_ui_id id, const es2_gui_event *ev) {
    if (ev->type == ES2_GUI_EV_REDRAW) es2_ui_draw(ui, id);
    else if (ev->type == ES2_GUI_EV_MOUSE && ev->code == ES2_GUI_MOUSE_LEFT_UP) es2_ui_fire(ui, id, ev);
}

/* Original: VSHELL.EXE 0x40c45f */
static void handle_dialog(es2_ui *ui, es2_ui_id id, const es2_gui_event *ev) {
    es2_ui_widget *w = W(ui, id);
    switch (ev->type) {
    case ES2_GUI_EV_REDRAW: draw_dialog(ui, w); break;
    case ES2_GUI_EV_LEAVE: es2_ui_timer_remove(ui, id); break;
    case ES2_GUI_EV_KEY:
        if (w->editable && es2_shell_edit_insert(&w->edit, ev->code)) draw_dialog(ui, w); /* 0x40c3fb */
        es2_ui_fire(ui, id, ev);
        break;
    case ES2_GUI_EV_TIMER:
        if (w->flags & ES2_UI_HIDDEN) break;
        w->edit.blink = w->editable ? 1 - w->edit.blink : 0;
        draw_dialog(ui, w);
        break;
    case ES2_GUI_EV_COMMAND:
        if (w->editable && w->edit.active) {
            if (ev->code == 1 || ev->code == 4) {
                if (es2_shell_edit_backspace(&w->edit)) draw_dialog(ui, w); /* 0x40c455 */
            } else if (ev->code == 10) {
                es2_ui_release_capture(ui);
                w = W(ui, id);
                w->edit.active = 0;
                draw_dialog(ui, w);
            }
        }
        es2_ui_fire(ui, id, ev);
        break;
    case ES2_GUI_EV_MOUSE:
        if (ev->code != ES2_GUI_MOUSE_LEFT_DOWN) break;
        if (w->edit.active) { /* 0x40c609: end the edit and give the click to the window under the cursor */
            es2_gui_event again = *ev;
            w->edit.active = 0;
            es2_ui_release_capture(ui);
            es2_ui_timer_remove(ui, id);
            leave(ui);
            again.target = 0;
            enter(ui, 0);
            (void)es2_gui_post(&ui->shell->input, again);
            draw_dialog(ui, W(ui, id));
        } else {
            (void)es2_ui_timer_add(ui, id, 500, 500, 0);
            ui->capture = 1;
            w->edit.active = 1;
            draw_dialog(ui, w);
            es2_ui_fire(ui, id, ev);
        }
        break;
    default: break;
    }
}

/* Original: VSHELL.EXE 0x40c965 */
static void handle_arm(es2_ui *ui, es2_ui_id id, const es2_gui_event *ev) {
    es2_ui_widget *w = W(ui, id);
    switch (ev->type) {
    case ES2_GUI_EV_HIDDEN: draw_erase(ui, w); break;
    case ES2_GUI_EV_REDRAW: es2_ui_draw(ui, id); break;
    case ES2_GUI_EV_LEAVE:
        if (w->pressed) {
            w->pressed = 0;
            es2_ui_redraw_tree(ui, id); /* posts, unlike WinButton's direct draw */
        }
        break;
    case ES2_GUI_EV_MOUSE:
        if (!w->enabled) break; /* no gate test */
        if (ev->code == ES2_GUI_MOUSE_LEFT_DOWN || ev->code == ES2_GUI_MOUSE_RIGHT_DOWN) {
            w->pressed = 1;
            es2_ui_draw(ui, id);
        } else if ((ev->code == ES2_GUI_MOUSE_LEFT_UP || ev->code == ES2_GUI_MOUSE_RIGHT_UP) && w->pressed) {
            es2_ui_fire(ui, id, ev);
            w = W(ui, id);
            w->pressed = 0;
            es2_ui_draw(ui, id);
        }
        break;
    default: break;
    }
}

/* Original: VSHELL.EXE 0x40ce63 */
static void handle_anim(es2_ui *ui, es2_ui_id id, const es2_gui_event *ev) {
    es2_ui_widget *w = W(ui, id);
    switch (ev->type) {
    case ES2_GUI_EV_SHOWN:
        (void)es2_ui_timer_add(ui, id, 500, 500, 0);
        ui->shell->input.interactive = 0; /* [0x46c28c] := 0 */
        break;
    case ES2_GUI_EV_HIDDEN:
        es2_ui_timer_remove(ui, id);
        for (int i = 0; i < w->frame_count && i < ES2_UI_ANIM_FRAMES; i++) w->frames[i] = NULL; /* deleted */
        ui->shell->input.interactive = 1;
        break;
    case ES2_GUI_EV_REDRAW: draw_anim(ui, w); break;
    case ES2_GUI_EV_TIMER:
        if (!(w->flags & ES2_UI_HIDDEN)) {
            if (++w->frame_index >= w->frame_count) w->frame_index = 0;
            draw_anim(ui, w);
        }
        es2_ui_fire(ui, id, ev);
        break;
    default: break;
    }
}

/* Original: VSHELL.EXE 0x40d067 */
static void handle_pal(es2_ui *ui, es2_ui_id id, const es2_gui_event *ev) {
    es2_ui_widget *w = W(ui, id);
    if (ev->type == ES2_GUI_EV_HIDDEN) {
        if (ui->palette) ui->palette(ui, w->palette);
    } else if (ev->type == ES2_GUI_EV_REDRAW) {
        draw_pal(ui, w);
    }
}

static void handle(es2_ui *ui, es2_ui_id id, const es2_gui_event *ev) {
    switch (W(ui, id)->cls) {
    case ES2_UI_WINDOW:
    case ES2_UI_ESWINDOW: es2_ui_fire(ui, id, ev); break; /* 0x41fc28 */
    case ES2_UI_RECT:
    case ES2_UI_REGION_FILL:
    case ES2_UI_TITLE:
    case ES2_UI_GRID:
    case ES2_UI_QUAD: handle_winbutton(ui, id, ev); break;
    case ES2_UI_BUTTON_FONT: handle_button_font(ui, id, ev); break;
    case ES2_UI_BUTTON_BITMAP: handle_button_bitmap(ui, id, ev); break;
    case ES2_UI_RADIO: handle_radio(ui, id, ev); break;
    case ES2_UI_ALERT: handle_alert(ui, id, ev); break;
    case ES2_UI_MESSAGE: handle_message(ui, id, ev); break;
    case ES2_UI_BITMAP: handle_bitmap(ui, id, ev); break;
    case ES2_UI_DIALOG: handle_dialog(ui, id, ev); break;
    case ES2_UI_ARM: handle_arm(ui, id, ev); break;
    case ES2_UI_ANIM2: handle_anim(ui, id, ev); break;
    case ES2_UI_PAL: handle_pal(ui, id, ev); break;
    case ES2_UI_REPORT:
        if (ev->type == ES2_GUI_EV_REDRAW) es2_ui_draw(ui, id);
        break;
    default: break;
    }
}

/* ---- construction ------------------------------------------------------------ */

/* Original: VSHELL.EXE 0x41f1b0 (link 0x41f788, absolute rectangle 0x41f599,
 * hidden 0x41f239) and 0x409f20 */
static es2_ui_id create(es2_ui *ui, es2_ui_class cls, es2_ui_id parent, es2_gui_rect rect, es2_ui_handler handler) {
    es2_ui_widget *w, *p;
    es2_ui_id id;
    if (!ui) return 0;
    p = es2_ui_get(ui, parent);
    if (ui->count >= ES2_UI_MAX || (parent && !p) || rect.left > rect.right || rect.top > rect.bottom) {
        ui->refused++; /* pool full, or 0x41f67f's assertion */
        return 0;
    }
    id = ++ui->count;
    w = W(ui, id);
    memset(w, 0, sizeof *w);
    w->cls = cls;
    w->parent = parent;
    w->rect = rect;
    w->abs = rect;
    if (p) {
        w->abs.left += p->abs.left;
        w->abs.right += p->abs.left;
        w->abs.top += p->abs.top;
        w->abs.bottom += p->abs.top;
        w->next = p->first;
        if (p->first) W(ui, p->first)->newer = id;
        p->first = id;
    }
    w->flags = ES2_UI_HIDDEN;
    w->mask = ES2_UI_MASK_BASE;
    w->handler = handler;
    w->grid = -1;
    return id;
}

es2_ui_id es2_ui_window(es2_ui *ui, es2_ui_id parent, es2_gui_rect rect, es2_ui_handler handler) {
    return create(ui, ES2_UI_ESWINDOW, parent, rect, handler);
}

/* Original: VSHELL.EXE 0x409d38 */
static es2_ui_id winbutton(es2_ui *ui, es2_ui_class cls, es2_ui_id parent, es2_gui_rect rect, es2_ui_handler handler) {
    es2_ui_id id = create(ui, cls, parent, rect, handler);
    if (!id) return 0;
    W(ui, id)->mask |= 0x60;
    W(ui, id)->enabled = 1;
    es2_ui_show(ui, id);
    return id;
}

static es2_ui_id rect_base(es2_ui *ui, es2_ui_class cls, es2_ui_id parent, es2_gui_rect rect, es2_ui_handler handler,
                           int32_t colour) { /* 0x40aca4 */
    es2_ui_id id = winbutton(ui, cls, parent, rect, handler);
    if (!id) return 0;
    W(ui, id)->colour = colour;
    W(ui, id)->frame = 1;
    return id;
}

es2_ui_id es2_ui_rect(es2_ui *ui, es2_ui_id parent, es2_gui_rect rect, es2_ui_handler handler, int32_t colour) {
    return rect_base(ui, ES2_UI_RECT, parent, rect, handler, colour);
}

static es2_gui_rect rel(int32_t l, int32_t t, int32_t r, int32_t b) {
    es2_gui_rect x;
    x.left = l;
    x.top = t;
    x.right = r;
    x.bottom = b;
    return x;
}

static es2_ui_id message(es2_ui *ui, es2_ui_id parent, es2_gui_rect rect, const es2_dfn *font,
                         es2_ui_handler handler, const char *text, int32_t align, int32_t colour, int32_t opaque,
                         int32_t style) { /* 0x40b828 */
    es2_ui_id id = create(ui, ES2_UI_MESSAGE, parent, rect, handler);
    es2_ui_widget *w;
    if (!id) return 0;
    w = W(ui, id);
    w->align = align;
    w->text_colour = colour;
    w->background = 0x10;
    w->opaque = opaque;
    w->style = style;
    w->font = font;
    if (es2_ui_message_set(ui, id, text, align, colour, 0) != 0) ui->refused++;
    w->mask &= ~0x18u;
    es2_ui_show(ui, id);
    return id;
}

es2_ui_id es2_ui_message(es2_ui *ui, es2_ui_id parent, es2_gui_rect rect, const es2_dfn *font, es2_ui_handler handler,
                         const char *text, int32_t align, int32_t colour, int32_t opaque, int32_t style) {
    return message(ui, parent, rect, font, handler, text, align, colour, opaque, style);
}

int es2_ui_message_set(es2_ui *ui, es2_ui_id id, const char *text, int32_t align, int32_t colour, int redraw) {
    es2_ui_widget *w = es2_ui_get(ui, id);
    size_t n;
    if (!w) return -1;
    n = text ? strlen(text) : 0;
    if (n >= ES2_UI_TEXT) return -1; /* 0x40b905: VSHELL asserts above 100 (and overruns at 100) */
    w->align = align;
    w->text_colour = colour;
    memcpy(w->text, text ? text : "", n);
    w->text[n] = 0;
    if (redraw) draw_label(ui, w, -1);
    return 0;
}

es2_ui_id es2_ui_button_font(es2_ui *ui, es2_ui_id parent, es2_gui_rect rect, const es2_dfn *font,
                             es2_ui_handler handler, const char *label, int32_t colour, int32_t arg8) {
    es2_ui_id id = rect_base(ui, ES2_UI_BUTTON_FONT, parent, rect, handler, colour), l;
    if (!id) return 0;
    l = message(ui, id, rel(1, 0, rect.right - rect.left, rect.bottom - rect.top), font, NULL, label, 2, 0x29, 0, 0);
    W(ui, id)->label = l;
    W(ui, id)->arg8 = arg8;
    if (l) W(ui, l)->enabled = arg8; /* label +0x49 */
    return id;
}

static es2_ui_id button_bitmap(es2_ui *ui, es2_ui_class cls, es2_ui_id parent, es2_gui_rect rect,
                               const es2_dfn *font, const es2_bitmap *normal, const es2_bitmap *active,
                               const es2_bitmap *extra, es2_ui_handler handler, const char *label) { /* 0x40a2c4 */
    es2_ui_id id = winbutton(ui, cls, parent, rect, handler);
    es2_ui_widget *w;
    if (!id) return 0;
    W(ui, id)->label =
        message(ui, id, rel(0, 0, rect.right - rect.left, rect.bottom - rect.top), font, NULL, label, 2, 0x29, 0, 0);
    w = W(ui, id);
    w->bitmap_normal = normal;
    w->bitmap_active = active;
    w->bitmap_extra = extra;
    w->latch = 1;
    w->repeat = 0;
    w->mask |= 0x200;
    w->repeat_count = 0;
    return id;
}

es2_ui_id es2_ui_button_bitmap(es2_ui *ui, es2_ui_id parent, es2_gui_rect rect, const es2_dfn *font,
                               const es2_bitmap *normal, const es2_bitmap *active, const es2_bitmap *extra,
                               es2_ui_handler handler, const char *label) {
    return button_bitmap(ui, ES2_UI_BUTTON_BITMAP, parent, rect, font, normal, active, extra, handler, label);
}

es2_ui_id es2_ui_radio(es2_ui *ui, es2_ui_id parent, es2_gui_rect rect, const es2_dfn *font, const es2_bitmap *normal,
                       const es2_bitmap *active, const es2_bitmap *extra, es2_ui_handler handler, const char *label) {
    return button_bitmap(ui, ES2_UI_RADIO, parent, rect, font, normal, active, extra, handler, label); /* +0x69 = 0 */
}

es2_ui_id es2_ui_region_fill(es2_ui *ui, es2_ui_id parent, es2_gui_rect rect, es2_ui_handler handler, int32_t fill_on,
                             int32_t colour) { /* 0x40af1c */
    es2_ui_id id = rect_base(ui, ES2_UI_REGION_FILL, parent, rect, handler, colour);
    if (!id) return 0;
    W(ui, id)->f55 = 0x25;
    W(ui, id)->f59 = fill_on;
    W(ui, id)->enabled = 0;
    return id;
}

static es2_ui_id title(es2_ui *ui, es2_ui_class cls, es2_ui_id parent, es2_gui_rect rect, const es2_dfn *font,
                       es2_ui_handler handler, int32_t colour, const char *label, int32_t bar) { /* 0x40b0fc */
    es2_ui_id id = rect_base(ui, cls, parent, rect, handler, colour);
    es2_ui_widget *w;
    if (!id) return 0;
    w = W(ui, id);
    w->f59 = 1;
    w->f55 = 0x24;
    w->f5d = 0x10;
    w->f65 = 1;
    w->f61 = bar;
    w->f71 = w->f6d = 0;
    W(ui, id)->label = message(ui, id, rel(0, 0, rect.right - rect.left, bar), font, NULL, label, 2, 0x29, 0, 0);
    W(ui, id)->enabled = 0;
    es2_ui_hide(ui, id);
    return id;
}

es2_ui_id es2_ui_title(es2_ui *ui, es2_ui_id parent, es2_gui_rect rect, const es2_dfn *font, es2_ui_handler handler,
                       int32_t colour, const char *label, int32_t title_height) {
    return title(ui, ES2_UI_TITLE, parent, rect, font, handler, colour, label, title_height);
}

es2_ui_id es2_ui_alert(es2_ui *ui, es2_ui_id parent, es2_gui_rect rect, const es2_dfn *font, es2_ui_handler handler,
                       int32_t colour, const char *label) { /* 0x40b590 */
    es2_ui_id id = title(ui, ES2_UI_ALERT, parent, rect, font, handler, colour, label, 0x14);
    es2_ui_widget *w;
    if (!id) return 0;
    w = W(ui, id);
    w->mask |= 0x60;
    w->saved = calloc((size_t)(rect.right - rect.left + 1) * (size_t)(rect.bottom - rect.top + 1), 1);
    if (!w->saved) ui->refused++;
    return id;
}

es2_ui_id es2_ui_bitmap(es2_ui *ui, es2_ui_id parent, es2_gui_rect rect, const es2_bitmap *bitmap,
                        es2_ui_handler handler, int32_t colour) { /* 0x40bc48 */
    es2_ui_id id = rect_base(ui, ES2_UI_BITMAP, parent, rect, handler, colour);
    if (!id) return 0;
    W(ui, id)->bitmap = bitmap;
    W(ui, id)->frame = 0;
    es2_ui_hide(ui, id);
    W(ui, id)->ox = W(ui, id)->oy = 0;
    return id;
}

static void layer_reset(es2_ui_layer *k) { /* 0x40bdf4 */
    memset(k, 0, sizeof *k);
    for (int i = 0; i < 10; i++) {
        k->from[i] = 0x0e;
        k->to[i] = 0x10;
    }
}

es2_ui_id es2_ui_grid(es2_ui *ui, es2_ui_id parent, es2_gui_rect rect, es2_ui_handler handler, int32_t colour) {
    es2_ui_id id; /* 0x40bd90 */
    if (!ui || ui->grids >= ES2_UI_GRIDS) {
        if (ui) ui->refused++;
        return 0;
    }
    id = rect_base(ui, ES2_UI_GRID, parent, rect, handler, colour);
    if (!id) return 0;
    W(ui, id)->grid = ui->grids++;
    es2_ui_grid_clear(ui, id);
    W(ui, id)->grid_colour = 0x22;
    W(ui, id)->grid_lines = 1;
    return id;
}

int es2_ui_grid_set_layer(es2_ui *ui, es2_ui_id id, int index, const es2_ui_layer *layer) { /* 0x40be7f */
    es2_ui_widget *w = es2_ui_get(ui, id);
    if (!w || w->cls != ES2_UI_GRID || w->grid < 0 || index < 0 || index >= ES2_UI_GRID_LAYERS || !layer) return -1;
    ui->layers[w->grid][index] = *layer;
    return 0;
}

void es2_ui_grid_clear(es2_ui *ui, es2_ui_id id) { /* 0x40c182 */
    es2_ui_widget *w = es2_ui_get(ui, id);
    if (!w || w->cls != ES2_UI_GRID || w->grid < 0) return;
    for (int i = 0; i < ES2_UI_GRID_LAYERS; i++) layer_reset(&ui->layers[w->grid][i]);
}

es2_ui_id es2_ui_dialog(es2_ui *ui, es2_ui_id parent, es2_gui_rect rect, const es2_dfn *font,
                        es2_ui_handler handler) { /* 0x40c1a4 */
    es2_ui_id id = create(ui, ES2_UI_DIALOG, parent, rect, handler);
    es2_ui_widget *w;
    if (!id) return 0;
    w = W(ui, id);
    w->font = font;
    es2_shell_edit_init(&w->edit, es2_gui_rect_text(w->abs), font); /* text "", set 0x46ee48, blink 1, colour 0x29 */
    w->mask |= 0x360;
    es2_ui_show(ui, id);
    w = W(ui, id);
    w->edit.active = 0;
    w->edit.width = 0;
    w->edit.blink = 1;
    w->edit.prefix = 0;
    w->edit.colour = 0x29;
    w->editable = 1;
    return id;
}

int es2_ui_dialog_set(es2_ui *ui, es2_ui_id id, const char *text, int redraw) {
    es2_ui_widget *w = es2_ui_get(ui, id);
    if (!w || w->cls != ES2_UI_DIALOG) return -1;
    if (es2_shell_edit_set_text(&w->edit, text, NULL) != ES2_OK) return -1;
    if (redraw) draw_dialog(ui, w);
    return 0;
}

es2_ui_id es2_ui_arm(es2_ui *ui, es2_ui_id parent, es2_gui_rect rect, es2_ui_handler handler, int32_t colour) {
    es2_ui_id id = rect_base(ui, ES2_UI_ARM, parent, rect, handler, colour); /* 0x40c928 */
    if (!id) return 0;
    W(ui, id)->f55 = rect.bottom - rect.top;
    W(ui, id)->f59 = 0x10;
    W(ui, id)->f5d = 0;
    return id;
}

es2_ui_id es2_ui_anim2(es2_ui *ui, es2_ui_id parent, es2_gui_rect rect, es2_ui_handler handler) { /* 0x40ce0c */
    es2_ui_id id = create(ui, ES2_UI_ANIM2, parent, rect, handler);
    if (!id) return 0;
    W(ui, id)->mask |= 0x200;
    return id;
}

int es2_ui_anim_add_frame(es2_ui *ui, es2_ui_id id, const es2_bitmap *frame) { /* 0x40cfb6 */
    es2_ui_widget *w = es2_ui_get(ui, id);
    if (!w || w->cls != ES2_UI_ANIM2 || w->frame_count >= ES2_UI_ANIM_FRAMES) return -1;
    w->frames[w->frame_count++] = frame;
    return 0;
}

es2_ui_id es2_ui_pal(es2_ui *ui, es2_ui_id parent, es2_gui_rect rect, es2_ui_handler handler) { /* 0x40d01c */
    return create(ui, ES2_UI_PAL, parent, rect, handler);
}

void es2_ui_pal_set(es2_ui *ui, es2_ui_id id, uint16_t word) { /* 0x40d047 */
    es2_ui_widget *w = es2_ui_get(ui, id);
    if (!w || w->cls != ES2_UI_PAL) return;
    w->palette = word;
    if (ui->palette) ui->palette(ui, word);
}

es2_ui_id es2_ui_quad(es2_ui *ui, es2_ui_id parent, es2_gui_rect rect, es2_ui_handler handler, int32_t colour,
                      const es2_dfn *font, const char *const text[4], const int32_t x[3], const int32_t align[4]) {
    es2_ui_id id = rect_base(ui, ES2_UI_QUAD, parent, rect, handler, colour);
    int32_t h = rect.bottom - rect.top, w1 = rect.right - rect.left - 1;
    int32_t left[4], right[4];
    if (!id || !x || !align) return id;
    left[0] = 2;    right[0] = x[0];  /* 0x40a8dc..0x40a8f3 */
    left[1] = x[0]; right[1] = x[1];
    left[2] = x[1]; right[2] = x[2];
    left[3] = x[2]; right[3] = w1;
    for (int i = 0; i < 4; i++) {
        es2_ui_id c = message(ui, id, rel(left[i], 0, right[i], h), font, NULL, text ? text[i] : NULL, align[i], 0x29, 0,
                              0);
        W(ui, id)->columns[i] = c;
        if (c) W(ui, c)->opaque = 1;
    }
    W(ui, id)->enabled = 0; /* 0x40aa73 */
    return id;
}

void es2_ui_quad_set_colour(es2_ui *ui, es2_ui_id id, int column, int32_t colour) { /* 0x40ab3b */
    es2_ui_widget *w = es2_ui_get(ui, id), *c;
    if (!w || w->cls != ES2_UI_QUAD || column < 0 || column > 3) {
        if (ui) ui->refused++; /* 0x40ab8b: assertion */
        return;
    }
    c = es2_ui_get(ui, w->columns[column]);
    if (c) c->text_colour = colour;
}

es2_ui_id es2_ui_report(es2_ui *ui, es2_ui_id parent, es2_gui_rect rect, const es2_dfn *font,
                        const es2_shell_report *report) {
    es2_ui_id id = create(ui, ES2_UI_REPORT, parent, rect, NULL);
    if (!id) return 0;
    W(ui, id)->font = font;
    W(ui, id)->report = report;
    W(ui, id)->mask &= ~0x18u;
    es2_ui_show(ui, id);
    return id;
}

void es2_ui_report_page(es2_ui *ui, es2_ui_id id, int32_t page) {
    es2_ui_widget *w = es2_ui_get(ui, id);
    if (!w || w->cls != ES2_UI_REPORT) return;
    w->page = page;
    if (!(w->flags & ES2_UI_HIDDEN)) draw_report(ui, w);
}

int es2_ui_init(es2_ui *ui, es2_gui_shell *shell) {
    es2_gui_rect all = {0, 0, ES2_GUI_WIDTH - 1, ES2_GUI_HEIGHT - 1};
    if (!ui || !shell) return -1;
    memset(ui, 0, sizeof *ui);
    ui->shell = shell;
    ui->display = create(ui, ES2_UI_WINDOW, 0, all, NULL);
    W(ui, ui->display)->flags = 0; /* the display's root is visible */
    ui->root = create(ui, ES2_UI_ESWINDOW, ui->display, all, NULL);
    ui->cursor_x = shell->input.cursor_x;
    ui->cursor_y = shell->input.cursor_y;
    ui->timer_last = clock_now(ui);
    return 0;
}

void es2_ui_set_clock(es2_ui *ui, uint32_t (*clock)(void *context), void *context) {
    if (!ui) return;
    ui->clock = clock;
    ui->clock_context = context;
    ui->timer_last = clock_now(ui);
}

void es2_ui_release(es2_ui *ui) {
    if (!ui) return;
    for (unsigned i = 0; i < ui->count; i++) {
        free(ui->w[i].saved);
        ui->w[i].saved = NULL;
    }
}
