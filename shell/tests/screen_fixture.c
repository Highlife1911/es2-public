/* The harness's own fixture screen (no game data). A synthetic stand-in
 * on the rectangles of the layout table's main_menu: a seeded background image
 * drawn with the blitter, a panel, synthetic labels on the eleven ESButtonFont
 * buttons; a left press fills the button under the pointer, the release
 * restores it and writes the control id into a status line; key characters
 * are appended to an entry line. It is NOT VSHELL's main menu (that is
 * main_menu.c): it exercises the harness (click by name, text, snaps, hashes,
 * differences) the way a screen package will use it, and shows the pattern
 * of a screen file: cases, then one ES2_SCREEN_SUITE line. */
#include "screen_harness.h"

#include "es2/shell/draw.h"

#include <stdio.h>
#include <string.h>

typedef struct fixture_state {
    es2_screen_session *session;
    char pressed[48];
    char status[48];
    char entry[32];
} fixture_state;

enum { PANEL = 200, BUTTON = 210, PRESSED = 250, LABEL = 41, STATUS = 60 };

static const es2_gui_rect panel_rect = {144, 173, 482, 312};   /* main_menu.03 */
static const es2_gui_rect status_rect = {144, 320, 482, 330};
static const es2_gui_rect entry_rect = {144, 334, 482, 344};

static void fill(uint8_t *pixels, es2_gui_rect r, uint8_t v) {
    for (int y = r.top; y <= r.bottom; y++) memset(pixels + y * ES2_GUI_WIDTH + r.left, v, (size_t)(r.right - r.left + 1));
}

static void draw_button(uint8_t *pixels, const es2_screen_control *c, int pressed) {
    fill(pixels, c->rect, pressed ? PRESSED : BUTTON);
    const char *number = strchr(c->id, '.');
    char label[16];
    snprintf(label, sizeof label, "BUTTON %s", number ? number + 1 : c->id);
    es2_screen_fixture_text(pixels, c->rect.left + 4, c->rect.top + 4, label, LABEL);
}

static int is_menu_button(const es2_screen_control *c) {
    return !strncmp(c->id, "main_menu.", 10) && c->clickable && c->has_rect && !strcmp(c->cls, "ESButtonFont");
}

static uint8_t background[ES2_GUI_PIXELS];

static void draw_start(const es2_screen_layout *layout, uint8_t *pixels, uint8_t table[256][3]) {
    es2_screen_fixture_image(background, ES2_GUI_WIDTH, ES2_GUI_HEIGHT, 7);
    memset(pixels, 0, ES2_GUI_PIXELS);
    es2_frame frame = {pixels, ES2_GUI_PIXELS, ES2_GUI_WIDTH, ES2_GUI_HEIGHT, ES2_GUI_WIDTH};
    es2_bitmap b = {ES2_GUI_HEIGHT, ES2_GUI_WIDTH, 0, background};
    es2_draw_blit(&frame, &b, 0, 0, NULL, ES2_MIRROR_NONE);
    fill(pixels, panel_rect, PANEL);
    es2_screen_fixture_text(pixels, panel_rect.left + 8, panel_rect.top + 6, "FIXTURE MENU", LABEL);
    for (size_t i = 0; i < layout->count; i++)
        if (is_menu_button(&layout->controls[i])) draw_button(pixels, &layout->controls[i], 0);
    if (table) {
        uint8_t records[256][4];
        es2_screen_fixture_palette(records, 1);
        for (int i = 0; i < 256; i++)
            for (int k = 0; k < 3; k++) table[i][k] = (uint8_t)(records[i][k] << 2); /* flag bit 0: x 4 */
    }
}

static void redraw_line(es2_gui_shell *shell, es2_gui_rect r, const char *text) {
    fill(shell->frame.pixels, r, PANEL);
    es2_screen_fixture_text(shell->frame.pixels, r.left + 2, r.top + 2, text, STATUS);
    es2_gui_invalidate_window(&shell->frame, r);
}

static void handle(void *context, const es2_gui_event *ev) {
    fixture_state *st = context;
    es2_gui_shell *shell = st->session->shell;
    const es2_screen_layout *layout = st->session->layout;
    if (ev->type == ES2_GUI_EV_MOUSE && (ev->code == ES2_GUI_MOUSE_LEFT_DOWN || ev->code == ES2_GUI_MOUSE_LEFT_UP)) {
        for (size_t i = 0; i < layout->count; i++) {
            const es2_screen_control *c = &layout->controls[i];
            if (!is_menu_button(c) || ev->x < c->rect.left || ev->x > c->rect.right || ev->y < c->rect.top ||
                ev->y > c->rect.bottom)
                continue;
            int down = ev->code == ES2_GUI_MOUSE_LEFT_DOWN;
            draw_button(shell->frame.pixels, c, down);
            es2_gui_invalidate_window(&shell->frame, c->rect);
            if (down) snprintf(st->pressed, sizeof st->pressed, "%s", c->id);
            else if (!strcmp(st->pressed, c->id)) {
                snprintf(st->status, sizeof st->status, "%s", c->id);
                redraw_line(shell, status_rect, st->status);
            }
        }
    }
    if (ev->type == ES2_GUI_EV_KEY && ev->code >= 0x20 && ev->code < 0x80) {
        size_t n = strlen(st->entry);
        if (n + 1 < sizeof st->entry) {
            st->entry[n] = (char)ev->code;
            st->entry[n + 1] = 0;
            redraw_line(shell, entry_rect, st->entry);
        }
    }
}

static int setup(es2_screen_session *s, void *state, const char *data_root) {
    (void)data_root;
    fixture_state *st = state;
    st->session = s;
    draw_start(s->layout, s->shell->frame.pixels, NULL);
    uint8_t records[256][4];
    es2_screen_fixture_palette(records, 1);
    s->shell->frame.realize_gate = 1;
    es2_gui_palette_select(&s->shell->frame, (const uint8_t(*)[4])records);
    es2_gui_rect all = {0, 0, ES2_GUI_WIDTH - 1, ES2_GUI_HEIGHT - 1};
    es2_gui_invalidate_window(&s->shell->frame, all);
    s->shell->input.interactive = 1;
    s->screen.context = st;
    s->screen.handle = handle;
    return 0;
}

static const es2_screen_layout *fixture_layout;

/* The harness self-test sets the layout before it runs the suites. */
void es2_screen_fixture_set_layout(const es2_screen_layout *layout);
void es2_screen_fixture_set_layout(const es2_screen_layout *layout) { fixture_layout = layout; }

static int expected_picture(const char *snap, uint8_t pixels[ES2_GUI_PIXELS], uint8_t table[256][3]) {
    if (!fixture_layout || strcmp(snap, "start")) return 0;
    draw_start(fixture_layout, pixels, table);
    return 1;
}

static const es2_screen_expect start_expect[] = {
    {"start", "e719728a41b76c0378102839ab383504de7a32b739028846923216cb03c03632", "e7e654c67b5d8207aaba13c5c52b0451c5e2eb510d9cff04a1c1e7291e177fdd", NULL},
};

static const es2_screen_expect tour_expect[] = {
    {"start", "e719728a41b76c0378102839ab383504de7a32b739028846923216cb03c03632", "e7e654c67b5d8207aaba13c5c52b0451c5e2eb510d9cff04a1c1e7291e177fdd", NULL},
    {"pressed", "2db385ef333447db7ee2befbc1117b4fe96b428d597df2e012f12f1611b886f6", "e7e654c67b5d8207aaba13c5c52b0451c5e2eb510d9cff04a1c1e7291e177fdd", NULL},
    {"clicked", "94e5be57b12a71da977c90d88aacb650262e1aaf6dfcd759c55867e09b5ba29a", "e7e654c67b5d8207aaba13c5c52b0451c5e2eb510d9cff04a1c1e7291e177fdd", NULL},
    {"typed", "cb49c2b55b910ecfef32ffbb6d96bda67fedb5f0ea29653addced80e3e4b3c73", "e7e654c67b5d8207aaba13c5c52b0451c5e2eb510d9cff04a1c1e7291e177fdd", NULL},
};

const es2_screen_case es2_screen_fixture_cases[] = {
    {"fixture.start", "main_menu", 0, sizeof(fixture_state), setup, "step\nsnap start\n", start_expect,
     sizeof start_expect / sizeof start_expect[0], expected_picture},
    {"fixture.tour", "main_menu", 0, sizeof(fixture_state), setup,
     "step\n"
     "snap start\n"
     "move 160 205\n"
     "ldown\n"
     "step\n"
     "snap pressed\n"
     "lup\n"
     "step\n"
     "click main_menu.19\n"
     "snap clicked\n"
     "type Abc 12\n"
     "snap typed\n",
     tour_expect, sizeof tour_expect / sizeof tour_expect[0], expected_picture},
};

ES2_SCREEN_SUITE(fixture, es2_screen_fixture_cases)
