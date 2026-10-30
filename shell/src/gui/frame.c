/* Frame, palette, dirty-rectangle queue and screen of the own shell.
 * Contract: docs/reverse-engineering/vshell_presentation.md and its
 * section "the own shell's frame, presenter and window". */
#include "es2/shell/gui/frame.h"

#include <string.h>

void es2_gui_frame_init(es2_gui_frame *frame) {
    memset(frame, 0, sizeof *frame);
}

static es2_gui_rect full_rect(void) {
    es2_gui_rect r = {0, 0, ES2_GUI_WIDTH - 1, ES2_GUI_HEIGHT - 1};
    return r;
}

/* Own replacement of VSHELL's overflow assertion (severity 4 at
 * 0x41ffee..0x420010 and in 0x42004b): one rectangle covering the frame. */
static void overflow(es2_gui_frame *frame) {
    frame->dirty_overflows++;
    frame->dirty[0] = full_rect();
    frame->dirty_count = 1;
}

/* Original: VSHELL.EXE 0x41fef9..0x420042 */
int es2_gui_invalidate_window(es2_gui_frame *frame, es2_gui_rect r) {
    int i = 0;
    for (; i < frame->dirty_count; i++) {
        const es2_gui_rect *q = &frame->dirty[i];
        if (q->left <= r.left && q->right >= r.right && q->top <= r.top && q->bottom >= r.bottom)
            return 0; /* a queued rectangle already covers it */
        if (q->left > r.left && q->right < r.right && q->top > r.top && q->bottom < r.bottom) {
            frame->dirty_count--;
            frame->dirty[i] = frame->dirty[frame->dirty_count];
            i--; /* the moved rectangle is checked at the same index */
        }
    }
    if (frame->dirty_count >= ES2_GUI_DIRTY_MAX) {
        overflow(frame);
        return 0;
    }
    frame->dirty[frame->dirty_count++] = r;
    return 1;
}

/* Original: VSHELL.EXE 0x42004b (queue part) */
int es2_gui_invalidate_rect(es2_gui_frame *frame, es2_gui_rect r) {
    if (frame->dirty_count >= ES2_GUI_DIRTY_MAX) {
        overflow(frame);
        return 0;
    }
    frame->dirty[frame->dirty_count++] = r;
    return 1;
}

/* Original: VSHELL.EXE 0x407b62 (pending byte 0x46de10 := 1) */
void es2_gui_palette_select(es2_gui_frame *frame, const uint8_t records[256][4]) {
    memcpy(frame->records, records, sizeof frame->records);
    frame->palette_pending = 1;
}

/* Original: VSHELL.EXE 0x407c1b, 0x4071fe step 2 (0x407248..0x407294) */
int es2_gui_palette_apply(es2_gui_frame *frame) {
    if (!frame->palette_pending) return 0;
    frame->palette_pending = 0;
    if (!frame->realize_gate) return 0; /* 0x40720b: the install stops before the table */
    uint8_t before[256][3];
    memcpy(before, frame->table, sizeof before);
    for (int i = 0; i < 256; i++) {
        const uint8_t *rec = frame->records[i];
        if (rec[3] & 1) {
            frame->table[i][0] = (uint8_t)(rec[0] << 2);
            frame->table[i][1] = (uint8_t)(rec[1] << 2);
            frame->table[i][2] = (uint8_t)(rec[2] << 2);
        } else if (rec[3] & 2) {
            frame->table[i][0] = rec[0];
            frame->table[i][1] = rec[1];
            frame->table[i][2] = rec[2];
        }
        /* else: VSHELL leaves the entry of its (uninitialised) local table;
         * the own shell keeps the previous upload's entry. */
    }
    frame->uploads++;
    if (memcmp(before, frame->table, sizeof before)) frame->recolour = 1;
    return 1;
}

void es2_gui_palette_lookup(const es2_gui_frame *frame, uint32_t out[256]) {
    for (int i = 0; i < 256; i++)
        out[i] = (uint32_t)frame->table[i][0] << 16 | (uint32_t)frame->table[i][1] << 8 | frame->table[i][2];
}

static size_t copy_block(es2_gui_screen *screen, const es2_gui_frame *frame, int32_t x0, int32_t y0, int64_t x1,
                         int64_t y1) {
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > ES2_GUI_WIDTH) x1 = ES2_GUI_WIDTH;
    if (y1 > ES2_GUI_HEIGHT) y1 = ES2_GUI_HEIGHT;
    if (x0 >= x1 || y0 >= y1) return 0;
    uint32_t lookup[256];
    es2_gui_palette_lookup(frame, lookup);
    for (int32_t y = y0; y < (int32_t)y1; y++) {
        const uint8_t *src = frame->pixels + (size_t)y * ES2_GUI_WIDTH;
        uint32_t *dst = screen->rgb + (size_t)y * ES2_GUI_WIDTH;
        for (int32_t x = x0; x < (int32_t)x1; x++) dst[x] = lookup[src[x]];
    }
    return (size_t)(x1 - x0) * (size_t)(y1 - y0);
}

/* Original: VSHELL.EXE 0x406bfb GDI branch, BitBlt at 0x406e52 */
size_t es2_gui_screen_present_rect(es2_gui_screen *screen, const es2_gui_frame *frame, es2_gui_rect r) {
    int64_t w = (int64_t)r.right - r.left + 1, h = (int64_t)r.bottom - r.top + 1;
    if (w <= 0 || h <= 0) return 0;
    return copy_block(screen, frame, r.left, r.top, 2 * (int64_t)r.left + w, 2 * (int64_t)r.top + h);
}

int es2_gui_recolour(es2_gui_screen *screen, es2_gui_frame *frame) {
    if (!frame->recolour) return 0;
    frame->recolour = 0;
    es2_gui_screen_present_full(screen, frame);
    return 1;
}

/* Original: VSHELL.EXE 0x406e87 */
void es2_gui_screen_present_full(es2_gui_screen *screen, const es2_gui_frame *frame) {
    copy_block(screen, frame, 0, 0, ES2_GUI_WIDTH, ES2_GUI_HEIGHT);
}

/* Original: VSHELL.EXE 0x41fe49 -> 0x41fe2c */
int es2_gui_flush(es2_gui_screen *screen, es2_gui_frame *frame) {
    int n = frame->dirty_count;
    for (int i = 0; i < n; i++) es2_gui_screen_present_rect(screen, frame, frame->dirty[i]);
    frame->dirty_count = 0;
    return n;
}

/* Original: VSHELL.EXE 0x4077ee GDI branch (memset, StretchBlt at 0x40791f) */
void es2_gui_clear(es2_gui_screen *screen, es2_gui_frame *frame) {
    memset(frame->pixels, 0, sizeof frame->pixels);
    uint32_t lookup[256];
    es2_gui_palette_lookup(frame, lookup);
    for (size_t i = 0; i < ES2_GUI_PIXELS; i++) screen->rgb[i] = lookup[0];
}

int es2_gui_layout_compute(int cw, int ch, es2_gui_layout *out) {
    if (cw <= 0 || ch <= 0) return 0;
    int k = cw / ES2_GUI_WIDTH < ch / ES2_GUI_HEIGHT ? cw / ES2_GUI_WIDTH : ch / ES2_GUI_HEIGHT;
    if (k >= 1) {
        out->scale = k;
        out->width = ES2_GUI_WIDTH * k;
        out->height = ES2_GUI_HEIGHT * k;
    } else {
        out->scale = 0;
        if ((int64_t)cw * ES2_GUI_HEIGHT <= (int64_t)ch * ES2_GUI_WIDTH) {
            out->width = cw;
            out->height = (int)((int64_t)ES2_GUI_HEIGHT * cw / ES2_GUI_WIDTH);
        } else {
            out->height = ch;
            out->width = (int)((int64_t)ES2_GUI_WIDTH * ch / ES2_GUI_HEIGHT);
        }
        if (out->width < 1) out->width = 1;
        if (out->height < 1) out->height = 1;
    }
    out->x = (cw - out->width) / 2;
    out->y = (ch - out->height) / 2;
    return 1;
}

static int floor_div(int64_t a, int64_t b) {
    int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) q--;
    return (int)q;
}

void es2_gui_layout_to_frame(const es2_gui_layout *l, int px, int py, int *fx, int *fy) {
    *fx = floor_div((int64_t)(px - l->x) * ES2_GUI_WIDTH, l->width);
    *fy = floor_div((int64_t)(py - l->y) * ES2_GUI_HEIGHT, l->height);
}
