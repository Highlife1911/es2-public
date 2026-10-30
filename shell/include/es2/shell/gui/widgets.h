#ifndef ES2_SHELL_GUI_WIDGETS_H
#define ES2_SHELL_GUI_WIDGETS_H

/* VSHELL's window framework as the own shell's widget toolkit
 * (contract: docs/reverse-engineering/vshell_widgets.md sections 2, 3 and
 * 7.3, and "widget toolkit"). Portable C17, no platform call,
 * no file I/O; draws into the frame (gui/frame.h) through the views of canvas.h with
 * the blitter (draw.h) and the text routines (gui/text.h), and registers every drawn
 * rectangle with the frame's dirty queue first (0x41fbd9 -> display slot 0).
 *
 * Windows form VSHELL's tree: each has a parent, its children are linked
 * newest first, rectangles are relative to the parent and inclusive, every
 * window starts hidden. Events come from the event queue (gui/events.h):
 * es2_ui_install puts the toolkit into the main loop (the WinConsumer pass and
 * the timer list before the drain, the dispatch in the drain). Handlers receive
 * the event with its position relative to the target window, as VSHELL's do.
 *
 * Window ids are 1-based indices into ui->w; 0 is "none". The event target
 * of es2_gui_event is such an id. */
#include <stddef.h>
#include <stdint.h>

#include "es2/formats/dfn.h"
#include "es2/shell/draw.h"
#include "es2/shell/gui/loop.h"
#include "es2/shell/gui/text.h"
#include "es2/shell/texts.h"

typedef uint32_t es2_ui_id;

#define ES2_UI_MAX 1536          /* VSHELL builds 1,051 controls (the layout table) */
#define ES2_UI_TIMERS 20         /* timer list [0x5ddcb4]: 20 records (0x46a403) */
#define ES2_UI_GRIDS 48          /* ESGrid layer tables (VSHELL builds 27) */
#define ES2_UI_GRID_LAYERS 30    /* 0x40bdbb */
#define ES2_UI_ANIM_FRAMES 10    /* ESAnim2 +0x45: ten slots */
#define ES2_UI_TEXT 100          /* ESMessage text: fewer than 100 bytes (0x40b905) */
#define ES2_UI_LAYOUT_SLOTS 1100 /* windows of the layout table's controls, by table index (1,051 used) */

/* RTTI classes (vshell_widgets.md section 2.2), plus the text panel. */
typedef enum es2_ui_class {
    ES2_UI_WINDOW,         /* Window: the display root */
    ES2_UI_ESWINDOW,       /* 0x409f20: frames, shields, the root [0x481180] */
    ES2_UI_RECT,           /* 0x40aca4 */
    ES2_UI_BUTTON_FONT,    /* 0x40a004 text button */
    ES2_UI_BUTTON_BITMAP,  /* 0x40a2c4 image button */
    ES2_UI_RADIO,          /* 0x40a6b0 check box / radio button */
    ES2_UI_REGION_FILL,    /* 0x40af1c */
    ES2_UI_TITLE,          /* 0x40b0fc panel with title bar */
    ES2_UI_ALERT,          /* 0x40b590 modal box */
    ES2_UI_MESSAGE,        /* 0x40b828 label */
    ES2_UI_BITMAP,         /* 0x40bc48 */
    ES2_UI_GRID,           /* 0x40bd90 layered bitmap panel */
    ES2_UI_DIALOG,         /* 0x40c1a4 edit field */
    ES2_UI_ARM,            /* 0x40c928 */
    ES2_UI_ANIM2,          /* 0x40ce0c */
    ES2_UI_PAL,            /* 0x40d01c */
    ES2_UI_QUAD,           /* ESRect + vtable 0x479c4c + 0x40a8c0: list row */
    ES2_UI_REPORT,         /* text panel: ESREPORT pages (0x40d231/0x40d492) */
    ES2_UI_CLASS_COUNT
} es2_ui_class;

/* Window flags (+0x11). */
#define ES2_UI_HIDDEN 0x2u
#define ES2_UI_PENDING 0x4u

/* Event masks (+0x39): base 0x1f; WinButton |= 0x60; image buttons and
 * ESAnim2 |= 0x200; ESDialog |= 0x360; ESMessage &= ~0x18. */
#define ES2_UI_MASK_BASE 0x1fu

/* One ESGrid layer record (0x38 bytes at +0x55 + 0x38 i). */
typedef struct es2_ui_layer {
    int32_t x, y;                 /* +0x00 draw point */
    const es2_bitmap *bitmap;     /* +0x08 (NULL: layer off) */
    int16_t from[10];             /* +0x0c recolour sources (default 0x0e) */
    int16_t to[10];               /* +0x20 targets (0x10 = pair off) */
    int32_t mirror;               /* +0x34 */
} es2_ui_layer;

typedef struct es2_ui es2_ui;
/* A window's handler (+0x3d): "fire" calls it with the event. The position
 * is relative to the event's target window. */
typedef void (*es2_ui_handler)(es2_ui *ui, es2_ui_id window, const es2_gui_event *ev);

typedef struct es2_ui_widget {
    es2_ui_class cls;
    es2_ui_id parent;             /* walk 0x41f8d7 */
    es2_ui_id first;              /* +0x0d: newest child */
    es2_ui_id next;               /* +0x05: next older sibling */
    es2_ui_id newer;              /* +0x09 when not first: next newer sibling */
    uint32_t flags;               /* +0x11 */
    es2_gui_rect abs;             /* +0x15 */
    es2_gui_rect rect;            /* +0x25: relative to the parent */
    uint32_t mask;                /* +0x39 */
    es2_ui_handler handler;       /* +0x3d */
    void *user;                   /* free for the screen */
    uint32_t original_handler;    /* the VSHELL handler address a screen binds (layout table) */
    /* WinButton */
    int32_t pressed;              /* +0x45 */
    int32_t enabled;              /* +0x49 (also on labels: ESButtonFont copies arg8 there) */
    /* ESRect */
    int32_t colour;               /* +0x4d frame colour */
    int32_t frame;                /* +0x51 frame on */
    /* label children */
    es2_ui_id label;              /* ESButtonFont +0x55, ESButtonBitmap +0x4d, ESTitle +0x69 */
    int32_t arg8;                 /* ESButtonFont +0x59 */
    /* ESButtonBitmap / ESRadioButton */
    const es2_bitmap *bitmap_normal;  /* +0x51 */
    const es2_bitmap *bitmap_active;  /* +0x55 */
    const es2_bitmap *bitmap_extra;   /* +0x59 (never read) */
    int32_t latch;                /* +0x5d */
    int32_t repeat;               /* +0x61 auto-repeat */
    int32_t repeat_count;         /* +0x65 */
    int32_t checked;              /* +0x69 (radio) */
    /* ESRegionFill (+0x55 dither, +0x59 fill), ESTitle (+0x55 bar, +0x59 body
     * fill, +0x5d body dither, +0x61 bar height, +0x65 stripes, +0x6d/+0x71
     * gap), ESArm (+0x55 split, +0x59 lower colour, +0x5d bevel) */
    int32_t f55, f59, f5d, f61, f65, f6d, f71;
    /* ESMessage */
    char text[ES2_UI_TEXT + 1];   /* +0x4d */
    int32_t align;                /* +0x45 */
    const es2_dfn *font;          /* +0xb1 (also ESDialog +0xa3) */
    int32_t text_colour;          /* +0xb5 */
    int32_t style;                /* +0xb9 (not read by the draw) */
    int32_t opaque;               /* +0xc1 */
    int32_t background;           /* +0xc5 */
    /* ESBitmap */
    const es2_bitmap *bitmap;     /* +0x5d */
    int32_t ox, oy;               /* +0x55, +0x59 */
    /* ESGrid */
    int grid;                     /* index of the layer table in ui->layers */
    uint8_t grid_lines;           /* +0x6e5 */
    int16_t grid_colour;          /* +0x6e6 */
    /* ESDialog (gui/text.h's field state; edit.rect is the absolute rectangle)
     * */
    es2_shell_edit edit;
    int32_t editable;             /* +0xbf */
    /* ESAnim2 */
    const es2_bitmap *frames[ES2_UI_ANIM_FRAMES];  /* +0x45 */
    int32_t frame_index, frame_count;              /* +0x6d, +0x71 */
    /* ESPal */
    uint16_t palette;             /* +0x45 */
    /* ESQuad columns +0x55..+0x61 */
    es2_ui_id columns[4];
    /* ESAlert: the screen under the box, (w + 1) x (h + 1) bytes (+0x79) */
    uint8_t *saved;
    /* text panel */
    const es2_shell_report *report;
    int32_t page;
} es2_ui_widget;

typedef struct es2_ui_timer {
    es2_ui_id window;
    int32_t remaining, period;
    uint32_t arg;
} es2_ui_timer;

struct es2_ui {
    es2_gui_shell *shell;
    es2_ui_widget w[ES2_UI_MAX];
    unsigned count;
    es2_ui_id display;            /* the display's root window ([[0x47f940]+5]) */
    es2_ui_id root;               /* VSHELL's root [0x481180] */
    /* the event queue's window state ([0x5ddcb0]) */
    es2_ui_id hover;              /* +0x01: window under the pointer, or the capturing field */
    int hover_display;            /* +0x05 != 0 */
    int32_t cursor_x, cursor_y;   /* +0x09: position of the last drained move */
    int capture;                  /* +0x1f */
    /* timer list [0x5ddcb4] */
    es2_ui_timer timers[ES2_UI_TIMERS];
    int timer_count;              /* +0x141 */
    uint32_t timer_last;          /* +0x145 */
    uint32_t (*clock)(void *context); /* 0x46baf0, milliseconds (NULL: ui->now) */
    void *clock_context;
    uint32_t now;
    es2_ui_layer layers[ES2_UI_GRIDS][ES2_UI_GRID_LAYERS];
    int grids;
    /* globals the classes read */
    int sound;                    /* [0x482356] */
    int gate;                     /* [0x470fb0]; [0x470eb0] is shell->input.movie_open */
    void *context;                /* for the callbacks below and the screens */
    void (*click_sound)(es2_ui *ui);                  /* 0x42f547 */
    void (*palette)(es2_ui *ui, uint16_t word);       /* 0x407b62 + 0x407c1b */
    void (*warp)(es2_ui *ui, int32_t x, int32_t y);   /* 0x468564: SetCursorPos (NULL: a WM_MOUSEMOVE is fed) */
    es2_ui_id quit_box;           /* [0x48d1dc] for Ctrl+Q (0: none built) */
    uint32_t refused;             /* VSHELL assertions the toolkit refused (timer list full, pool full, ...) */
    es2_ui_id layout_ids[ES2_UI_LAYOUT_SLOTS]; /* es2_ui_layout_build: control index -> window */
};

/* Fresh toolkit on a shell: the display root (shown, 640 x 480) and VSHELL's
 * root ESWindow with the same rectangle (start-up 0x401f1f..0x401f89), still
 * hidden: screens are built under the hidden root, as the layout table records
 * them (every control hidden or pending after construction); the flow shows
 * the root (es2_ui_show(ui, ui->root)) when the screens are built. Returns 0,
 * -1 for a null argument. */
int es2_ui_init(es2_ui *ui, es2_gui_shell *shell);
/* The millisecond clock of the timer list (NULL: ui->now, which the caller
 * advances); the list's last time becomes the clock's present value. */
void es2_ui_set_clock(es2_ui *ui, uint32_t (*clock)(void *context), void *context);
/* Frees the ESAlert save buffers. */
void es2_ui_release(es2_ui *ui);
/* Puts the toolkit into the main loop: hooks->context = ui, hooks->handle =
 * es2_ui_on_event, hooks->timer = es2_ui_on_timer. Other hooks are kept. */
void es2_ui_install(es2_ui *ui, es2_gui_hooks *hooks);
/* The loop's step between the device poll and the drain: the WinConsumer
 * pass over the events the poll posted (0x409996), then the timer poll
 * 0x46a508. */
void es2_ui_on_timer(void *context, es2_gui_shell *shell);
/* The drain's per-event step (0x469ee4 body): hover and capture for moves,
 * the hover window for events without a target, then 0x46a274. */
void es2_ui_on_event(void *context, const es2_gui_event *ev);

static inline es2_ui_widget *es2_ui_get(es2_ui *ui, es2_ui_id id) {
    return ui && id && id <= ui->count ? &ui->w[id - 1] : NULL;
}

/* ---- constructors (0 when the pool is full or an argument is bad) ------- */
/* Arguments in the order of vshell_widgets.md section 2.3; handler may be NULL. */
es2_ui_id es2_ui_window(es2_ui *ui, es2_ui_id parent, es2_gui_rect rect, es2_ui_handler handler);
es2_ui_id es2_ui_rect(es2_ui *ui, es2_ui_id parent, es2_gui_rect rect, es2_ui_handler handler, int32_t colour);
es2_ui_id es2_ui_button_font(es2_ui *ui, es2_ui_id parent, es2_gui_rect rect, const es2_dfn *font,
                             es2_ui_handler handler, const char *label, int32_t colour, int32_t arg8);
es2_ui_id es2_ui_button_bitmap(es2_ui *ui, es2_ui_id parent, es2_gui_rect rect, const es2_dfn *font,
                               const es2_bitmap *normal, const es2_bitmap *active, const es2_bitmap *extra,
                               es2_ui_handler handler, const char *label);
es2_ui_id es2_ui_radio(es2_ui *ui, es2_ui_id parent, es2_gui_rect rect, const es2_dfn *font,
                       const es2_bitmap *normal, const es2_bitmap *active, const es2_bitmap *extra,
                       es2_ui_handler handler, const char *label);
es2_ui_id es2_ui_region_fill(es2_ui *ui, es2_ui_id parent, es2_gui_rect rect, es2_ui_handler handler, int32_t fill,
                             int32_t colour);
es2_ui_id es2_ui_title(es2_ui *ui, es2_ui_id parent, es2_gui_rect rect, const es2_dfn *font, es2_ui_handler handler,
                       int32_t colour, const char *label, int32_t title_height);
es2_ui_id es2_ui_alert(es2_ui *ui, es2_ui_id parent, es2_gui_rect rect, const es2_dfn *font, es2_ui_handler handler,
                       int32_t colour, const char *label);
es2_ui_id es2_ui_message(es2_ui *ui, es2_ui_id parent, es2_gui_rect rect, const es2_dfn *font, es2_ui_handler handler,
                         const char *text, int32_t align, int32_t colour, int32_t opaque, int32_t style);
es2_ui_id es2_ui_bitmap(es2_ui *ui, es2_ui_id parent, es2_gui_rect rect, const es2_bitmap *bitmap,
                        es2_ui_handler handler, int32_t colour);
es2_ui_id es2_ui_grid(es2_ui *ui, es2_ui_id parent, es2_gui_rect rect, es2_ui_handler handler, int32_t colour);
es2_ui_id es2_ui_dialog(es2_ui *ui, es2_ui_id parent, es2_gui_rect rect, const es2_dfn *font, es2_ui_handler handler);
es2_ui_id es2_ui_arm(es2_ui *ui, es2_ui_id parent, es2_gui_rect rect, es2_ui_handler handler, int32_t colour);
es2_ui_id es2_ui_anim2(es2_ui *ui, es2_ui_id parent, es2_gui_rect rect, es2_ui_handler handler);
es2_ui_id es2_ui_pal(es2_ui *ui, es2_ui_id parent, es2_gui_rect rect, es2_ui_handler handler);
/* ESRect, then the ESQuad vtable, then 0x40a8c0 with the four columns:
 * (2..x1), (x1..x2), (x2..x3), (x3..w-1), opaque labels of colour 41;
 * enabled := 0. */
es2_ui_id es2_ui_quad(es2_ui *ui, es2_ui_id parent, es2_gui_rect rect, es2_ui_handler handler, int32_t colour,
                      const es2_dfn *font, const char *const text[4], const int32_t x[3], const int32_t align[4]);
/* Text panel: the ESREPORT pages (es2_shell_report_draw) in the
 * rectangle; shown with the parent like a label. */
es2_ui_id es2_ui_report(es2_ui *ui, es2_ui_id parent, es2_gui_rect rect, const es2_dfn *font,
                        const es2_shell_report *report);

/* ---- window operations -------------------------------------------------- */
/* 0x41f93a / 0x41fabd (events 1 + 4, or 2, posted to the queue; pending
 * children follow their parent; the hover window is recomputed). */
void es2_ui_show(es2_ui *ui, es2_ui_id window);
void es2_ui_hide(es2_ui *ui, es2_ui_id window);
/* 0x40b7e9: show the box's parent (its shield), then the box; 0x40b808: hide
 * the box, then the shield. */
void es2_ui_modal_show(es2_ui *ui, es2_ui_id box);
void es2_ui_modal_hide(es2_ui *ui, es2_ui_id box);
/* 0x41f3ed: a redraw event for a shown window and (recursively) its children. */
void es2_ui_redraw_tree(es2_ui *ui, es2_ui_id window);
/* The class's draw (vtable slot 1, or the draw its handler runs on event 4). */
void es2_ui_draw(es2_ui *ui, es2_ui_id window);
/* The word +0x49 (no redraw, as VSHELL's screens write it). */
void es2_ui_set_enabled(es2_ui *ui, es2_ui_id window, int enabled);
/* 0x46a05c from `window` at the frame point (x, y): 0 if hidden or outside. */
es2_ui_id es2_ui_hit_test(const es2_ui *ui, es2_ui_id window, int32_t x, int32_t y);
/* 0x41fc28: the handling window's handler, if any, gets the event. */
void es2_ui_fire(es2_ui *ui, es2_ui_id window, const es2_gui_event *ev);
/* 0x46a01c: capture flag := 0, then the hover window from the cursor. */
void es2_ui_release_capture(es2_ui *ui);
/* 0x46a3f0 / 0x46a478. Add returns 0 when the list is full (VSHELL asserts). */
int es2_ui_timer_add(es2_ui *ui, es2_ui_id window, int32_t delay, int32_t period, uint32_t arg);
void es2_ui_timer_remove(es2_ui *ui, es2_ui_id window);
/* 0x46a508 at the clock's time. */
void es2_ui_timer_poll(es2_ui *ui);

/* ---- class setters ------------------------------------------------------- */
/* ESMessage setText 0x40b8bb: NULL or "" is the empty text; refused (-1,
 * nothing changed) at 100 bytes or more (VSHELL asserts). redraw != 0 draws. */
int es2_ui_message_set(es2_ui *ui, es2_ui_id label, const char *text, int32_t align, int32_t colour, int redraw);
/* ESQuad 0x40ab3b: column colour (no redraw). */
void es2_ui_quad_set_colour(es2_ui *ui, es2_ui_id quad, int column, int32_t colour);
/* ESAnim2 0x40cfb6: append a frame (refused beyond ten). */
int es2_ui_anim_add_frame(es2_ui *ui, es2_ui_id anim, const es2_bitmap *frame);
/* ESGrid 0x40be7f (index 0..29) and 0x40c182 (all layers off). */
int es2_ui_grid_set_layer(es2_ui *ui, es2_ui_id grid, int index, const es2_ui_layer *layer);
void es2_ui_grid_clear(es2_ui *ui, es2_ui_id grid);
/* ESPal 0x40d047: store the palette word, select and apply it (ui->palette). */
void es2_ui_pal_set(es2_ui *ui, es2_ui_id pal, uint16_t word);
/* ESDialog setText 0x40c294 / 0x40c355 (gui/text.h rules), with a redraw. */
int es2_ui_dialog_set(es2_ui *ui, es2_ui_id field, const char *text, int redraw);
/* Text panel page (redraws when shown). */
void es2_ui_report_page(es2_ui *ui, es2_ui_id panel, int32_t page);

/* Alt+F4 and Ctrl+Q: release the capture, then show the Quit box modally
 * (0x405283..0x40528f, 0x409c60..0x409c72); nothing without ui->quit_box. */
void es2_ui_quit_box(es2_ui *ui);

#endif
