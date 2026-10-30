#ifndef ES2_SHELL_GUI_CANVAS_H
#define ES2_SHELL_GUI_CANVAS_H

/* One frame type for the own shell. The frame is es2_gui_frame
 * (VSHELL's single 640 x 480 8-bit picture with its colour table and dirty
 * queue); the image primitives (es2_frame, es2_rect) and the text
 * primitives (es2_shell_text_frame, es2_shell_text_rect) keep their own
 * small views so that their libraries stay independent, and these adapters
 * give those views of the one frame. The widget toolkit draws only through
 * them. All three rectangle types are inclusive (left, top, right, bottom). */
#include "es2/shell/draw.h"
#include "es2/shell/gui/frame.h"
#include "es2/shell/gui/text.h"

static inline es2_frame es2_gui_frame_draw_view(es2_gui_frame *frame) {
    es2_frame view;
    view.pixels = frame->pixels;
    view.size = sizeof frame->pixels;
    view.width = ES2_GUI_WIDTH;
    view.height = ES2_GUI_HEIGHT;
    view.stride = ES2_GUI_WIDTH;
    return view;
}

static inline es2_shell_text_frame es2_gui_frame_text_view(es2_gui_frame *frame) {
    es2_shell_text_frame view;
    view.pixels = frame->pixels;
    view.width = ES2_GUI_WIDTH;
    view.height = ES2_GUI_HEIGHT;
    view.stride = ES2_GUI_WIDTH;
    return view;
}

static inline es2_rect es2_gui_rect_draw(es2_gui_rect r) {
    es2_rect out;
    out.left = r.left;
    out.top = r.top;
    out.right = r.right;
    out.bottom = r.bottom;
    return out;
}

static inline es2_shell_text_rect es2_gui_rect_text(es2_gui_rect r) {
    es2_shell_text_rect out;
    out.l = r.left;
    out.t = r.top;
    out.r = r.right;
    out.b = r.bottom;
    return out;
}

#endif
