/* The mission screen 0x442bc4, pages 0, 1 and 4
 * (es2/shell/gui/mission.h).
 *
 * Contract: docs/reverse-engineering/vshell_ui.md sections 4.8, 14, 16 and
 * 33. Original: VSHELL.EXE: the constructor
 * 0x442bc4..0x4445c2, the images 0x4445c3..0x44483d, the open
 * 0x444873..0x444fa3, the statistics hide 0x444fa4, the leave 0x445095, the
 * text panel 0x445289..0x4454b6, the handlers 0x4454b8..0x445de6, the
 * ESREPORT routines 0x40d1bc..0x40d834 and 0x43aee7. */
#include "es2/shell/gui/mission.h"

#include <stdio.h>
#include <string.h>

#include "es2/shell/draw.h"
#include "es2/shell/gui/text.h"

/* ---- helpers ------------------------------------------------------------------ */

static es2_mission_screen *screen_of(es2_ui *ui, es2_ui_id window) {
    es2_ui_widget *w = es2_ui_get(ui, window);
    return w ? (es2_mission_screen *)w->user : NULL;
}

static const char *text(const es2_mission_screen *m, uint32_t id) {
    const char *t = m->host.text ? m->host.text(m->host.ctx, id) : NULL;
    return t ? t : "";
}

static int16_t group(const es2_mission_screen *m) {
    return m->host.group ? m->host.group(m->host.ctx) : 0;
}

static const es2_bitmap *image(const es2_mission_screen *m, const char *file) {
    return m->host.bitmap ? m->host.bitmap(m->host.ctx, file, 0) : NULL; /* 0x408aa1: frame 0 (+8 -> [0]) */
}

static es2_ui_id id(const es2_mission_screen *m, int control) {
    return control >= 0 && control < ES2_SCREEN_MISSION_CONTROLS ? m->ids[control] : 0;
}

static es2_ui_widget *widget(const es2_mission_screen *m, int control) {
    return es2_ui_get(m->ui, id(m, control));
}

static void show(es2_mission_screen *m, int control) { es2_ui_show(m->ui, id(m, control)); }
static void hide(es2_mission_screen *m, int control) { es2_ui_hide(m->ui, id(m, control)); }

/* The class draw (vtable slot 1) as 0x4445c3 and the page-1/4 button code
 * call it; drawn here only while the window is shown (own: a hidden window
 * is drawn again by its show before anything is presented). */
static void draw(es2_mission_screen *m, es2_ui_id window) {
    es2_ui_widget *w = es2_ui_get(m->ui, window);
    if (w && !(w->flags & ES2_UI_HIDDEN)) es2_ui_draw(m->ui, window);
}

static es2_shell_flow_result flow_event(es2_mission_screen *m, uint32_t handler) {
    es2_shell_flow_result r = es2_shell_flow_event(m->flow, (es2_shell_flow_handler)handler, NULL);
    m->last_handler = handler;
    m->last_result = r;
    m->events++;
    if (m->host.changed) m->host.changed(m->host.ctx, handler, r);
    return r;
}

/* Original: VSHELL.EXE 0x41f243(window, rect): the relative rectangle, the
 * absolute ones of the window and its subtree (0x41f599), the redraw
 * 0x41f3ed. */
static void absolute(es2_ui *ui, es2_ui_id window) {
    es2_ui_widget *w = es2_ui_get(ui, window), *p;
    if (!w) return;
    p = es2_ui_get(ui, w->parent);
    w->abs = w->rect;
    if (p) {
        w->abs.left += p->abs.left;
        w->abs.right += p->abs.left;
        w->abs.top += p->abs.top;
        w->abs.bottom += p->abs.top;
    }
    for (es2_ui_id c = w->first; c; c = ui->w[c - 1].next) absolute(ui, c);
}

static void set_rect(es2_mission_screen *m, int control, es2_gui_rect rect) {
    es2_ui_widget *w = widget(m, control);
    if (!w) return;
    w->rect = rect;
    absolute(m->ui, id(m, control));
    es2_ui_redraw_tree(m->ui, id(m, control));
}

/* 0x40b8bb on the ESTitle's label (+0x69) with its own align (+0x45) and
 * colour (+0xb5), redraw 1; after 0x41f3ed of the title. */
static void set_title(es2_mission_screen *m, const char *t) {
    es2_ui_widget *w = widget(m, ES2_MISSION_TITLE), *label;
    if (!w) return;
    es2_ui_redraw_tree(m->ui, id(m, ES2_MISSION_TITLE));
    label = es2_ui_get(m->ui, w->label);
    if (!label) return;
    if (es2_ui_message_set(m->ui, w->label, t, label->align, label->text_colour, 1)) m->refused++;
}

/* The page-1 / page-4 button code: label (+0x55) colour, frame +0x4d, the
 * redraw 0x41f3ed, +0x49, the draw (vtable slot 1). */
static void button_state(es2_mission_screen *m, int control, int32_t label_colour, int32_t frame, int enabled) {
    es2_ui_widget *w = widget(m, control), *label;
    if (!w) return;
    label = es2_ui_get(m->ui, w->label);
    if (label) label->text_colour = label_colour;
    w->colour = frame;
    es2_ui_redraw_tree(m->ui, id(m, control));
    w->enabled = enabled;
    draw(m, id(m, control));
}

/* ---- text panels (ESREPORT 0x40d1bc..0x40d834) ---------------------------------- */

static es2_mission_panel *panel_of(es2_mission_screen *m, int p) {
    return p >= 0 && p < ES2_MISSION_PANELS ? &m->panels[p] : NULL;
}

/* 0x40d492: the lines of the current page shown while visible, else hidden
 * (one window drawing the page). A hidden line label does not draw, also on
 * a redraw event queued before its hide (0x40b9fb); the window keeps no
 * layout while hidden for the same effect. */
static void panel_lines(es2_mission_screen *m, es2_mission_panel *p) {
    es2_ui_widget *w = es2_ui_get(m->ui, p->window);
    if (!w) return;
    if (p->visible && p->has_text) {
        w->report = &p->report;
        w->page = p->page;
        if (w->flags & ES2_UI_HIDDEN) es2_ui_show(m->ui, p->window);
        else es2_ui_redraw_tree(m->ui, p->window);
    } else {
        w->report = NULL;
        es2_ui_hide(m->ui, p->window);
    }
}

/* 0x40d3db: the layout freed; visible, page and the counts := 0. */
static void panel_free(es2_mission_screen *m, es2_mission_panel *p) {
    es2_ui_widget *w = es2_ui_get(m->ui, p->window);
    es2_ui_hide(m->ui, p->window);
    if (w) w->report = NULL;
    es2_shell_report_free(&p->report);
    memset(&p->report, 0, sizeof p->report);
    p->has_text = 0;
    p->visible = 0;
    p->page = 0;
}

int es2_mission_screen_set_text(es2_mission_screen *m, int panel, const char *t) {
    es2_mission_panel *p;
    es2_ui_widget *w;
    uint8_t widths[256];
    int32_t height, rows;
    if (!m || !m->ui || !(p = panel_of(m, panel))) return -1;
    panel_free(m, p); /* 0x40d24a */
    w = es2_ui_get(m->ui, p->window);
    if (!w || !t || !w->font) { /* VSHELL faults in strlen on a null text */
        m->refused++;
        return -1;
    }
    /* 0x40d2a6: line height; 0x40d4e7: rows = (b - t) / height; 0x40d686 with
     * W = r - l */
    height = w->font->height;
    rows = height > 0 ? (w->rect.bottom - w->rect.top) / height : 0;
    es2_shell_font_widths(w->font, widths);
    if (rows <= 0 || es2_shell_report_layout((es2_span){(const uint8_t *)t, strlen(t)}, widths,
                                             (int16_t)(w->rect.right - w->rect.left), (int16_t)rows, &p->report,
                                             NULL) != ES2_OK) {
        memset(&p->report, 0, sizeof p->report);
        m->refused++;
        return -1;
    }
    for (size_t i = 0; i < p->report.lines; i++) /* setText of each line asserts at 100 bytes */
        if (strlen((const char *)p->report.text.data + p->report.line[i]) >= ES2_UI_TEXT) {
            es2_shell_report_free(&p->report);
            memset(&p->report, 0, sizeof p->report);
            m->refused++;
            return -1;
        }
    p->has_text = 1; /* visible 0, page 0: the lines stay hidden */
    return 0;
}

void es2_mission_screen_page(es2_mission_screen *m, int panel, int next) {
    es2_mission_panel *p;
    if (!m || !m->ui || !(p = panel_of(m, panel)) || !p->visible) return;
    if (next) { /* 0x40d80e: current < pages - 1 */
        if (p->page >= p->report.pages - 1) return;
        p->page++;
    } else { /* 0x40d7ed: current > 0 */
        if (p->page <= 0) return;
        p->page--;
    }
    panel_lines(m, p);
}

/* 0x445343's jump tables 0x44536e (previous, -1..4) and 0x44541a (new, 0..4)
 * name the panels by the same index. */
void es2_mission_screen_panel(es2_mission_screen *m, int panel) {
    es2_mission_panel *p;
    if (!m || !m->ui) return;
    es2_ui_redraw_tree(m->ui, id(m, ES2_MISSION_TEXT_FRAME));
    if ((p = panel_of(m, m->panel)) != NULL) {
        p->visible = 0;
        panel_lines(m, p);
    }
    if ((p = panel_of(m, panel)) != NULL) {
        p->visible = 1;
        panel_lines(m, p);
    }
    m->panel = (int16_t)panel; /* 0x4454ad, whatever the value */
}

void es2_mission_screen_buttons(es2_mission_screen *m, int page) {
    static const int frames[3] = {ES2_MISSION_BRIEFING, ES2_MISSION_OBJECTIVES, ES2_MISSION_INTELLIGENCE};
    int on = page == 1 || page == 4 ? ES2_MISSION_BRIEFING
             : page == 2          ? ES2_MISSION_OBJECTIVES
             : page == 3          ? ES2_MISSION_INTELLIGENCE
                                  : -1;
    if (!m || !m->ui) return;
    for (int k = 0; k < 3; k++) { /* 0x445289 */
        es2_ui_widget *w = widget(m, frames[k]);
        if (!w) continue;
        w->colour = 0x22;
        es2_ui_redraw_tree(m->ui, id(m, frames[k]));
    }
    if (on >= 0 && widget(m, on)) {
        widget(m, on)->colour = 0x20;
        es2_ui_redraw_tree(m->ui, id(m, on));
    }
}

/* ---- statistics ------------------------------------------------------------------- */

/* mission.31 .. .50 in construction order (0x443840..0x44444f) */
static const uint32_t statistic_globals[ES2_MISSION_STATISTICS] = {
    0x48d918, 0x48d920, 0x48d928, 0x48d91c, 0x48d924, 0x48d92c, 0x48d930, 0x48d934, 0x48d938, 0x48d93c,
    0x48d940, 0x48d944, 0x48d948, 0x48d94c, 0x48d958, 0x48d950, 0x48d95c, 0x48d954, 0x48d960, 0x48d964};

int es2_mission_statistic(uint32_t global) {
    for (int k = 0; k < ES2_MISSION_STATISTICS; k++)
        if (statistic_globals[k] == global) return ES2_MISSION_STATISTIC + k;
    return -1;
}

void es2_mission_screen_hide_statistics(es2_mission_screen *m) {
    if (!m || !m->ui) return;
    for (uint32_t g = 0x48d918; g <= 0x48d964; g += 4) hide(m, es2_mission_statistic(g)); /* 0x444fa4 */
}

void es2_mission_screen_statistics(es2_mission_screen *m, const es2_shell_debrief_texts *texts) {
    if (!m || !m->ui || !texts) return;
    for (int k = 0; k < ES2_SHELL_DEBRIEF_VALUES; k++) { /* 0x40fa8a..0x40fc6c */
        int control = es2_mission_statistic(texts->widget[k]);
        if (control < 0 || es2_ui_message_set(m->ui, id(m, control), texts->text[k], k < 3 ? 0 : 1, 0x29, 1))
            m->refused++;
    }
}

/* ---- images (0x4445c3, 0x4446d1, 0x44472f, 0x44483e) --------------------------- */

/* 0x4445c3(page): terradef into the Telecomm panel and drawn; page 0 the
 * theatre image (group < 5 th_earth, else th_moon) as layer 0 of the ESGrid
 * at (0, 0) with 0x40bd58's defaults. */
static void load_images(es2_mission_screen *m, int page) {
    es2_ui_widget *w = widget(m, ES2_MISSION_TELECOMM);
    m->terradef = image(m, "dba\\terradef.dba");
    if (w) {
        w->bitmap = m->terradef;
        draw(m, id(m, ES2_MISSION_TELECOMM));
    }
    if (page == 0) {
        es2_ui_layer layer;
        m->theatre_image = image(m, group(m) < 5 ? "dba\\th_earth.dba" : "dba\\th_moon.dba");
        memset(&layer, 0, sizeof layer);
        for (int i = 0; i < 10; i++) {
            layer.from[i] = 0x0e;
            layer.to[i] = 0x10;
        }
        layer.bitmap = m->theatre_image;
        (void)es2_ui_grid_set_layer(m->ui, id(m, ES2_MISSION_THEATRE_IMAGE), 0, &layer);
    }
}

/* 0x4446d1: the grid's layers off, both images freed (the Telecomm panel
 * keeps its pointer +0x5d, as VSHELL's does). */
static void unload_images(es2_mission_screen *m) {
    es2_ui_grid_clear(m->ui, id(m, ES2_MISSION_THEATRE_IMAGE));
    m->terradef = NULL;
    m->theatre_image = NULL;
}

/* 0x43a430(p) on the bay's ESPal [0x48d524]: show, word +0x45 := p, hide
 * (the hide selects and applies palette p). */
static void bay_palette(es2_mission_screen *m, uint16_t p) {
    es2_ui_widget *w;
    if (!m->bay || !(w = es2_ui_get(m->ui, m->bay->bay[ES2_BAY_PALETTE]))) return;
    es2_ui_show(m->ui, m->bay->bay[ES2_BAY_PALETTE]);
    w->palette = p;
    es2_ui_hide(m->ui, m->bay->bay[ES2_BAY_PALETTE]);
}

int es2_mission_screen_theatre(es2_mission_screen *m) {
    static const char *const pictures[4] = {"dba\\alph2.dba", "dba\\delt1.dba", "dba\\omic1.dba", "dba\\brav1.dba"};
    int16_t g, bx;
    es2_ui_widget *w;
    if (!m || !m->ui) return -1;
    g = group(m);
    bx = (int16_t)(g - 1);
    if (bx == 4) { /* 0x444755..0x4447c9 */
        if (m->movies) (void)es2_shell_movies_theatre(m->movies, g);
        bay_palette(m, 2);
        if (m->host.audio) {
            m->host.audio(m->host.ctx, ES2_MISSION_MUSIC_FADE_OUT);
            m->host.audio(m->host.ctx, ES2_MISSION_FOCUS_LOST);
        }
        return 1;
    }
    if (bx < 0 || bx > 3) { /* 0x4447ce reads past the four-entry copy of 0x478068 */
        m->refused++;
        return -1;
    }
    m->picture = image(m, pictures[bx]);
    if ((w = widget(m, ES2_MISSION_THEATRE)) != NULL) w->bitmap = m->picture;
    show(m, ES2_MISSION_THEATRE);
    if (m->ui->palette) m->ui->palette(m->ui, (uint16_t)(bx + 0xf)); /* 0x407b62(bx + 0xf) */
    return 0;
}

void es2_mission_screen_theatre_hide(es2_mission_screen *m) {
    if (!m || !m->ui) return;
    hide(m, ES2_MISSION_THEATRE);
    m->picture = NULL;
}

/* ---- open and leave ---------------------------------------------------------------- */

void es2_mission_screen_show(es2_mission_screen *m, int page) {
    static const es2_gui_rect rows_bottom = {7, 307, 632, 423}, rows_page0 = {7, 307, 632, 476};
    int16_t si, page16 = (int16_t)page;
    int16_t briefing = -1, debrief = -1;
    if (!m || !m->ui || !m->flow) return;
    si = (int16_t)(group(m) - 1); /* 0x444a12 */
    load_images(m, page);
    set_rect(m, ES2_MISSION_TEXT_FRAME, rows_bottom); /* 0x444a2b */
    if (m->host.movie_words) m->host.movie_words(m->host.ctx, &briefing, &debrief);
    if (page16 == 0) {
        /* sprintf("%s %s") into the 160-byte buffer [ebp-0xa0]; the entries
         * are copied at once (the host's buffer may be reused) */
        char title[ES2_UI_TEXT + 1], name[161], word[161];
        es2_mission_screen_hide_statistics(m);                                   /* 0x444a3c */
        (void)es2_mission_screen_set_text(m, ES2_MISSION_PANEL_CAMPAIGN,
                                          m->host.campaign ? m->host.campaign(m->host.ctx, si) : NULL);
        set_rect(m, ES2_MISSION_TEXT_FRAME, rows_page0);                         /* 0x444a61 */
        if (m->movies) es2_shell_movies_mission(m->movies, 0, group(m), briefing, debrief); /* 0x444a69..0x444add */
        snprintf(word, sizeof word, "%s", text(m, ES2_SHELL_SECTOR_WORD));      /* 0x444ae4 */
        snprintf(name, sizeof name, "%s", text(m, (uint32_t)(int16_t)(si + ES2_SHELL_SECTOR_FIRST))); /* 0x444af5 */
        /* setText asserts at 100 bytes: refused */
        if (es2_shell_sector_title((es2_span){(const uint8_t *)name, strlen(name)},
                                   (es2_span){(const uint8_t *)word, strlen(word)}, title, sizeof title, NULL) == ES2_OK)
            set_title(m, title);
        else
            m->refused++;
        show(m, ES2_MISSION_THEATRE_IMAGE);                                      /* 0x444b55 */
        m->state = 1;
    }
    if (page16 == 1) {
        es2_mission_screen_hide_statistics(m);                                   /* 0x444b6e */
        (void)es2_mission_screen_set_text(m, ES2_MISSION_PANEL_OBJECTIVES,
                                          m->host.report ? m->host.report(m->host.ctx, 2) : NULL);
        (void)es2_mission_screen_set_text(m, ES2_MISSION_PANEL_INTELLIGENCE,
                                          m->host.report ? m->host.report(m->host.ctx, 3) : NULL);
        (void)es2_mission_screen_set_text(m, ES2_MISSION_PANEL_BRIEFING,
                                          m->host.report ? m->host.report(m->host.ctx, 1) : NULL);
        m->map_palette = (int16_t)(si + 5);                                      /* 0x444bb2 */
        button_state(m, ES2_MISSION_OBJECTIVES, 0x29, 0x22, 1);
        button_state(m, ES2_MISSION_INTELLIGENCE, 0x29, 0x22, 1);
        button_state(m, ES2_MISSION_ROCK_AND_ROLL, 0x29, 0x22, 1);
        set_title(m, text(m, 0xb3));
        m->state = 2;
        m->map_pending = 1;                                                      /* 0x444ccf */
        if (m->movies) es2_shell_movies_mission(m->movies, 1, group(m), briefing, debrief); /* 0x444cd6..0x444d18 */
    }
    if (page16 == 4) {
        (void)es2_mission_screen_set_text(m, ES2_MISSION_PANEL_DEBRIEF,
                                          m->host.report ? m->host.report(m->host.ctx, 4) : NULL);
        m->map_palette = (int16_t)(si + 10);                                     /* 0x444d40 */
        button_state(m, ES2_MISSION_ROCK_AND_ROLL, 0x26, 0x26, 0);
        set_title(m, text(m, 0xb9));
        if (m->movies) es2_shell_movies_mission(m->movies, 4, group(m), briefing, debrief); /* 0x444dc8..0x444e0a */
    }
    /* 0x444e11..0x444f9c */
    show(m, ES2_MISSION_TITLE);
    show(m, ES2_MISSION_MAP_FRAME);
    show(m, ES2_MISSION_TELECOMM);
    show(m, ES2_MISSION_TEXT_FRAME);
    if (page16 == 0) {
        hide(m, ES2_MISSION_PAGE_PREVIOUS);
        hide(m, ES2_MISSION_PAGE_NEXT);
    } else {
        show(m, ES2_MISSION_PAGE_PREVIOUS);
        show(m, ES2_MISSION_PAGE_NEXT);
    }
    if (page16 == 1) {
        show(m, ES2_MISSION_BUTTON_ROW);
        for (int c = ES2_MISSION_BRIEFING; c <= ES2_MISSION_ROCK_AND_ROLL; c += 2) show(m, c);
        for (int k = 0; k < 6; k++) show(m, ES2_MISSION_ARROW + 2 * k);
        es2_mission_screen_buttons(m, page);                                     /* 0x444f07 */
    } else {
        hide(m, ES2_MISSION_BUTTON_ROW);
        for (int c = ES2_MISSION_BRIEFING; c <= ES2_MISSION_ROCK_AND_ROLL; c += 2) hide(m, c);
        for (int k = 0; k < 6; k++) hide(m, ES2_MISSION_ARROW + 2 * k);
    }
    es2_mission_screen_panel(m, page);                                           /* 0x444f97 */
    if (m->host.changed) m->host.changed(m->host.ctx, ES2_MISSION_SHOW, ES2_FLOW_DONE);
}

void es2_mission_screen_leave(es2_mission_screen *m) {
    if (!m || !m->ui) return;
    m->state = 0;                                         /* 0x445095 */
    unload_images(m);                                     /* 0x44509e */
    es2_mission_screen_hide_statistics(m);                /* 0x4450a3 */
    panel_free(m, &m->panels[ES2_MISSION_PANEL_BRIEFING]);
    panel_free(m, &m->panels[ES2_MISSION_PANEL_OBJECTIVES]);
    panel_free(m, &m->panels[ES2_MISSION_PANEL_INTELLIGENCE]);
    panel_free(m, &m->panels[ES2_MISSION_PANEL_DEBRIEF]);
    hide(m, ES2_MISSION_TITLE);
    hide(m, ES2_MISSION_MAP_FRAME);
    hide(m, ES2_MISSION_TELECOMM);
    hide(m, ES2_MISSION_TEXT_FRAME);
    hide(m, ES2_MISSION_BUTTON_ROW);
    hide(m, ES2_MISSION_THEATRE_IMAGE);
    if (m->host.changed) m->host.changed(m->host.ctx, ES2_MISSION_LEAVE, ES2_FLOW_DONE);
}

int es2_mission_screen_open(es2_mission_screen *m, int page, int click) {
    int16_t g;
    int p;
    if (!m || !m->ui || !m->flow || !m->bay) return -1;
    /* the leave 0x43a537 does nothing for the words -1, 0 and 1; the
     * callers (Use Default, ACCEPT) run right after the bay show (word -1) */
    if (m->bay->session.screen < -1 || m->bay->session.screen > 1) {
        m->refused++;
        return -1;
    }
    m->flow->page = page;                                 /* 0x43af0c */
    g = group(m);
    p = es2_palette_for_bay_screen(7, (int16_t)page, g);  /* 0x43b7f2(7) */
    if (p >= 0) bay_palette(m, (uint16_t)p);
    es2_mission_screen_show(m, page);                     /* 0x444873(page) */
    m->bay->session.screen = ES2_FLOW_WORD_MISSION;       /* 0x43af5a */
    (void)flow_event(m, ES2_FLOW_BAY_MISSION);            /* the flow's word 7 */
    if (click && m->ui->shell) /* 0x43af63..0x43afb2: a press and a release on MISSION */
        es2_gui_post_synthetic_click(&m->ui->shell->input, m->bay->bay[ES2_BAY_MISSION]);
    return 0;
}

/* ---- handlers ------------------------------------------------------------------------- */

es2_shell_flow_result es2_mission_screen_press(es2_mission_screen *m, uint32_t handler) {
    if (!m || !m->ui || !m->flow) return ES2_FLOW_IGNORED;
    switch (handler) {
    case ES2_MISSION_BRIEFING_HANDLER: /* 0x445a51: the page's panel */
        es2_mission_screen_buttons(m, m->flow->page);
        es2_mission_screen_panel(m, m->flow->page);
        return ES2_FLOW_DONE;
    case ES2_MISSION_OBJECTIVES_HANDLER: /* 0x445ac7 */
        es2_mission_screen_buttons(m, 2);
        es2_mission_screen_panel(m, 2);
        return ES2_FLOW_DONE;
    case ES2_MISSION_INTELLIGENCE_HANDLER: /* 0x445b30 */
        es2_mission_screen_buttons(m, 3);
        es2_mission_screen_panel(m, 3);
        return ES2_FLOW_DONE;
    case ES2_MISSION_PREVIOUS_HANDLER: /* 0x445c79: panels 1..4 only */
    case ES2_MISSION_NEXT_HANDLER:     /* 0x445d2a */
        es2_ui_redraw_tree(m->ui, id(m, ES2_MISSION_TEXT_FRAME));
        if (m->panel >= 1 && m->panel <= 4) es2_mission_screen_page(m, m->panel, handler == ES2_MISSION_NEXT_HANDLER);
        return ES2_FLOW_DONE;
    case ES2_FLOW_ROCK_AND_ROLL: /* 0x445b99: checks, warning box 0x44d90c(n), 0x40f68c, exit 2 */
        return flow_event(m, handler);
    case 0x445577u: case 0x445646u: case 0x445715u: case 0x4457e4u: case 0x4458b3u: case 0x445982u:
        if (m->host.map) m->host.map(m->host.ctx, handler);
        return ES2_FLOW_DONE;
    default: /* the Telecomm panel 0x4454b8: jmp to its epilogue (vshell_ui.md section 14) */
        return ES2_FLOW_IGNORED;
    }
}

static void on_control(es2_ui *ui, es2_ui_id window, const es2_gui_event *ev) {
    es2_mission_screen *m = screen_of(ui, window);
    es2_ui_widget *w = es2_ui_get(ui, window);
    (void)ev;
    if (m && w) (void)es2_mission_screen_press(m, w->original_handler);
}

/* ---- resolvers and build -------------------------------------------------------------- */

static const es2_dfn *resolve_font(void *context, uint32_t global) {
    es2_mission_screen *m = context;
    return m->host.font ? m->host.font(m->host.ctx, global) : NULL;
}

static const es2_bitmap *resolve_bitmap(void *context, const es2_ui_layout_ref *ref) {
    es2_mission_screen *m = context;
    if (!m->host.bitmap || ref->kind != ES2_UI_REF_DBA) return NULL; /* dba\miss_arw.dba */
    return m->host.bitmap(m->host.ctx, es2_ui_layout_file(ref->a), ref->b);
}

static const char *resolve_text(void *context, const es2_ui_layout_ref *ref) {
    es2_mission_screen *m = context;
    switch (ref->kind) {
    case ES2_UI_REF_ESTEXT: return text(m, (uint32_t)ref->a);
    case ES2_UI_REF_EXE_STRING: return ref->b == 1 ? " " : ""; /* 0x47807a, 0x47808d .. 0x478099: " " */
    default: return NULL;
    }
}

static es2_ui_handler resolve_handler(void *context, uint32_t address) {
    (void)context;
    switch (address) {
    case ES2_MISSION_TELECOMM_HANDLER: case ES2_MISSION_BRIEFING_HANDLER: case ES2_MISSION_OBJECTIVES_HANDLER:
    case ES2_MISSION_INTELLIGENCE_HANDLER: case ES2_MISSION_PREVIOUS_HANDLER: case ES2_MISSION_NEXT_HANDLER:
    case ES2_FLOW_ROCK_AND_ROLL:
    case 0x445577u: case 0x445646u: case 0x445715u: case 0x4457e4u: case 0x4458b3u: case 0x445982u:
        return on_control;
    default:
        return NULL;
    }
}

/* Original: VSHELL.EXE 0x442bc4 (the table is the layout table's; the five
 * ESREPORT objects 0x443686..0x44383b with FONT2 [0x46de00] under [0x48d8bc])
 * */
int es2_screen_mission_build(es2_ui *ui, void *ctx) {
    static const es2_gui_rect campaign = {10, 21, 613, 168}, others = {10, 21, 575, 115};
    /* construction order: [0x48d8fc], [0x48d900], [0x48d904], [0x48d908], [0x48d90c] */
    static const int order[ES2_MISSION_PANELS] = {ES2_MISSION_PANEL_CAMPAIGN, ES2_MISSION_PANEL_BRIEFING,
                                                  ES2_MISSION_PANEL_DEBRIEF, ES2_MISSION_PANEL_OBJECTIVES,
                                                  ES2_MISSION_PANEL_INTELLIGENCE};
    es2_mission_screen *m = ctx;
    es2_ui_resolver r;
    const es2_dfn *font;
    if (!ui || !m || !m->flow) return -1;
    m->ui = ui;
    r.context = m;
    r.font = resolve_font;
    r.bitmap = resolve_bitmap;
    r.text = resolve_text;
    r.handler = resolve_handler;
    memset(m->ids, 0, sizeof m->ids);
    if (es2_ui_layout_build(ui, ES2_UI_SCREEN_MISSION, &r, m->ids, ES2_SCREEN_MISSION_CONTROLS) <= 0) return -1;
    for (int k = 0; k < ES2_SCREEN_MISSION_CONTROLS; k++) {
        es2_ui_widget *w = es2_ui_get(ui, m->ids[k]);
        if (!w) return -1;
        w->user = m;
    }
    font = resolve_font(m, 0x46de00);
    memset(m->panels, 0, sizeof m->panels);
    for (int k = 0; k < ES2_MISSION_PANELS; k++) {
        es2_mission_panel *p = &m->panels[order[k]];
        p->window = es2_ui_report(ui, id(m, ES2_MISSION_TEXT_FRAME), order[k] ? others : campaign, font, NULL);
        if (!p->window) return -1;
        es2_ui_hide(ui, p->window); /* no line labels until a setText */
        es2_ui_get(ui, p->window)->user = m;
    }
    /* image values */
    m->panel = -1;
    m->state = 0;
    m->map_pending = 0;
    m->map_palette = -1;
    m->terradef = m->theatre_image = m->picture = NULL;
    return 0;
}

void es2_mission_screen_release(es2_mission_screen *m) {
    if (!m) return;
    for (int k = 0; k < ES2_MISSION_PANELS; k++) {
        es2_ui_widget *w = m->ui ? es2_ui_get(m->ui, m->panels[k].window) : NULL;
        if (w) w->report = NULL;
        es2_shell_report_free(&m->panels[k].report);
        memset(&m->panels[k].report, 0, sizeof m->panels[k].report);
        m->panels[k].has_text = 0;
    }
}
