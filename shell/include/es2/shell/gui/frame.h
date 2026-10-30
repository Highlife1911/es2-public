#ifndef ES2_SHELL_GUI_FRAME_H
#define ES2_SHELL_GUI_FRAME_H

/* The own shell's frame, palette, dirty-rectangle queue and the screen
 * it is presented to (contract: docs/reverse-engineering/vshell_presentation.md;
 * own deviations: vshell_presentation.md, "the own shell's frame, presenter
 * and window"). Portable C17,
 * no platform call, no file I/O.
 *
 * - The frame is VSHELL's one 8-bit picture: 640 x 480 indices, top-down,
 *   stride 640 ([0x481950]). Everything is drawn into it in software.
 * - The colour table is the DIB table VSHELL uploads with SetDIBColorTable
 *   (0x4071fe): per DPL record (r, g, b, flags) bit 0 gives (r, g, b) << 2,
 *   else bit 1 the unscaled bytes, else the entry keeps its value.
 * - The dirty queue is the display object's (0x41fe84 merge, 0x42004b plain
 *   append; 100 inclusive rectangles), flushed by the present step.
 * - The screen is what the window shows: 0x00RRGGBB pixels converted at the
 *   moment a rectangle is presented, as GDI's BitBlt converts through the DIB
 *   table of that moment on a true-colour desktop. */
#include <stddef.h>
#include <stdint.h>

#define ES2_GUI_WIDTH 640
#define ES2_GUI_HEIGHT 480
#define ES2_GUI_PIXELS (ES2_GUI_WIDTH * ES2_GUI_HEIGHT)
#define ES2_GUI_DIRTY_MAX 100

/* Inclusive rectangle (left, top, right, bottom) as the display object keeps it. */
typedef struct es2_gui_rect { int32_t left, top, right, bottom; } es2_gui_rect;

typedef struct es2_gui_frame {
    uint8_t pixels[ES2_GUI_PIXELS];
    uint8_t table[256][3];          /* uploaded colours: red, green, blue */
    uint8_t records[256][4];        /* selected palette (DPL r, g, b, flags), pending until applied */
    int palette_pending;            /* [0x46de10] */
    int realize_gate;               /* [0x46c274]: 0 = an install never reaches the table */
    uint32_t uploads;               /* number of table uploads (SetDIBColorTable calls) */
    int recolour;                   /* own: an upload changed the table since the last whole conversion */
    es2_gui_rect dirty[ES2_GUI_DIRTY_MAX];
    int dirty_count;
    uint32_t dirty_overflows;       /* own: see es2_gui_invalidate_window */
} es2_gui_frame;

/* Black table, empty queue, gate 0, frame zeroed. */
void es2_gui_frame_init(es2_gui_frame *frame);

/* Display slot 0 (0x41fe84 queue part, 0x41fef9..0x420042): skipped when a
 * queued rectangle contains it (borders included); queued rectangles lying
 * strictly inside it are removed (the last one moves into the hole and is
 * checked again); else appended. Returns 1 when appended. At 100 queued
 * rectangles VSHELL raises a severity-4 assertion (process exit 1); the own
 * shell instead replaces the queue by one full-frame rectangle (deviation,
 * counted in dirty_overflows). */
int es2_gui_invalidate_window(es2_gui_frame *frame, es2_gui_rect rect);
/* Display slot 1 (0x42004b): appended without merging (same overflow rule). */
int es2_gui_invalidate_rect(es2_gui_frame *frame, es2_gui_rect rect);

/* 0x407b62's effect on the screen side: the 256 records become pending. */
void es2_gui_palette_select(es2_gui_frame *frame, const uint8_t records[256][4]);
/* 0x407c1b: if pending, pending := 0 and, while the realization gate is set,
 * the table is uploaded from the records (0x4071fe step 2). Returns 1 when a
 * table was uploaded. An upload that changes the table sets recolour. */
int es2_gui_palette_apply(es2_gui_frame *frame);
/* Fill a 256-entry 0x00RRGGBB lookup from the current table. */
void es2_gui_palette_lookup(const es2_gui_frame *frame, uint32_t out[256]);

/* The window contents (what a frame hash of the screen covers). */
typedef struct es2_gui_screen { uint32_t rgb[ES2_GUI_PIXELS]; } es2_gui_screen;

/* 0x406bfb (GDI branch) with P = the rectangle's corner, as every dirty flush
 * calls it: BitBlt(x, y, w + x, h + y) from the same corner, i.e. the block
 * from (left, top) to (2 left + w, 2 top + h) clipped to the 640 x 480 client
 * is converted through the current table. Returns the number of pixels. */
size_t es2_gui_screen_present_rect(es2_gui_screen *screen, const es2_gui_frame *frame, es2_gui_rect rect);
/* 0x406e87: the whole frame (WM_PAINT, WM_SETFOCUS during a movie). */
void es2_gui_screen_present_full(es2_gui_screen *screen, const es2_gui_frame *frame);
/* Own deviation from VSHELL's GDI path: while recolour is set, the whole
 * frame is converted through the new table and recolour := 0. VSHELL on a
 * true-colour desktop converts only the dirty rectangles, so areas outside
 * them keep the previous palette's colours (the bay's button bar after
 * ESPal's early flush 0x40d0f0); an 8-bit display recolours everything on
 * the install, as this does. Returns 1 when it converted. */
int es2_gui_recolour(es2_gui_screen *screen, es2_gui_frame *frame);
/* 0x41fe49: present every queued rectangle in queue order, then count := 0.
 * Returns the number of rectangles presented. */
int es2_gui_flush(es2_gui_screen *screen, es2_gui_frame *frame);
/* 0x4077ee (GDI branch): the frame is zeroed and stretched over the client,
 * so the whole screen shows the colour of index 0. */
void es2_gui_clear(es2_gui_screen *screen, es2_gui_frame *frame);

/* Where the 640 x 480 picture lies in an output of cw x ch pixels: the
 * largest integer scale k >= 1 with 640 k <= cw and 480 k <= ch, centred
 * (offsets round down); a smaller output gets the largest aspect-correct
 * size that fits (scale 0 = fractional). The same rule as the GDI
 * presenter for DBSIM (presenter_frame.h). Returns 0 for empty outputs. */
typedef struct es2_gui_layout { int scale; int x, y, width, height; } es2_gui_layout;
int es2_gui_layout_compute(int cw, int ch, es2_gui_layout *out);
/* Output pixel -> frame coordinate (floor; may lie outside 0..639/0..479). */
void es2_gui_layout_to_frame(const es2_gui_layout *layout, int px, int py, int *fx, int *fy);

#endif
