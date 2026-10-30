/* The headless platform (script in, report out). See headless.h. */
#include "es2/shell/gui/headless.h"

#include "es2/formats/sha256.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum message_kind { MSG_MOUSE, MSG_KEY, MSG_FOCUS, MSG_PAINT, MSG_CLOSE, MSG_QUIT } message_kind;
typedef struct message {
    message_kind kind;
    int a, b, c;          /* mouse: message, x, y; key: vk, up; focus: gained */
    int shift, alt, ctrl; /* key modifiers; mouse: MK_ bits in shift (left), alt (right) */
} message;

typedef struct headless {
    es2_gui_shell *shell;
    es2_gui_hooks user;
    char *report;
    size_t length, capacity;
    int failed;
    message *pending;
    size_t pending_count, pending_capacity;
    int held_left, held_right;
    unsigned pass;
} headless;

static void out(headless *h, const char *format, ...) {
    char line[512];
    va_list ap;
    va_start(ap, format);
    int n = vsnprintf(line, sizeof line, format, ap);
    va_end(ap);
    if (n < 0) return;
    if ((size_t)n >= sizeof line) n = (int)sizeof line - 1;
    if (h->length + (size_t)n + 2 > h->capacity) {
        size_t capacity = h->capacity ? h->capacity * 2 : 4096;
        while (capacity < h->length + (size_t)n + 2) capacity *= 2;
        char *grown = realloc(h->report, capacity);
        if (!grown) { h->failed = 1; return; }
        h->report = grown;
        h->capacity = capacity;
    }
    memcpy(h->report + h->length, line, (size_t)n);
    h->length += (size_t)n;
    h->report[h->length++] = '\n';
    h->report[h->length] = 0;
}

void es2_gui_hash_frame(const es2_gui_frame *frame, char hex[65]) {
    es2f_sha256_of(frame->pixels, sizeof frame->pixels, hex);
}

void es2_gui_hash_table(const es2_gui_frame *frame, char hex[65]) {
    uint8_t bytes[768];
    for (int i = 0; i < 256; i++) {
        bytes[3 * i] = frame->table[i][2];
        bytes[3 * i + 1] = frame->table[i][1];
        bytes[3 * i + 2] = frame->table[i][0];
    }
    es2f_sha256_of(bytes, sizeof bytes, hex);
}

void es2_gui_hash_screen(const es2_gui_screen *screen, char hex[65]) {
    es2f_sha256 s;
    es2f_sha256_init(&s);
    uint8_t row[ES2_GUI_WIDTH * 4];
    for (int y = 0; y < ES2_GUI_HEIGHT; y++) {
        for (int x = 0; x < ES2_GUI_WIDTH; x++) {
            uint32_t v = screen->rgb[y * ES2_GUI_WIDTH + x];
            row[4 * x] = (uint8_t)v;
            row[4 * x + 1] = (uint8_t)(v >> 8);
            row[4 * x + 2] = (uint8_t)(v >> 16);
            row[4 * x + 3] = (uint8_t)(v >> 24);
        }
        es2f_sha256_update(&s, row, sizeof row);
    }
    es2f_sha256_hex(&s, hex);
}

unsigned es2_gui_key_by_name(const char *name) {
    static const struct { const char *name; unsigned vk; } names[] = {
        {"esc", 0x1b},   {"enter", 0x0d}, {"tab", 0x09},     {"backspace", 0x08}, {"space", 0x20},
        {"up", 0x26},    {"down", 0x28},  {"left", 0x25},    {"right", 0x27},     {"home", 0x24},
        {"end", 0x23},   {"pgup", 0x21},  {"pgdn", 0x22},    {"ins", 0x2d},       {"del", 0x2e},
        {"ctrl", 0x11},  {"alt", 0x12},   {"shift", 0x10},   {"caps", 0x14},      {"numlock", 0x90},
        {"scroll", 0x91}, {"kpminus", 0x6d}, {"kpplus", 0x6b}, {"clear", 0x0c},
    };
    if (!strncmp(name, "vk:", 3)) return (unsigned)strtoul(name + 3, NULL, 0) & 0xff;
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
        if (!strcmp(name, names[i].name)) return names[i].vk;
    if (name[0] == 'f' && name[1] >= '1' && name[1] <= '9') {
        int n = atoi(name + 1);
        if (n >= 1 && n <= 12) return 0x70u + (unsigned)(n - 1);
    }
    if (name[0] && !name[1]) {
        int c = toupper((unsigned char)name[0]);
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return (unsigned)c;
    }
    return 0;
}

static void queue_message(headless *h, message m) {
    if (h->pending_count == h->pending_capacity) {
        size_t capacity = h->pending_capacity ? h->pending_capacity * 2 : 64;
        message *grown = realloc(h->pending, capacity * sizeof *grown);
        if (!grown) { h->failed = 1; return; }
        h->pending = grown;
        h->pending_capacity = capacity;
    }
    h->pending[h->pending_count++] = m;
}

static int pump(void *context, es2_gui_shell *shell) {
    headless *h = context;
    int quit = 0;
    for (size_t i = 0; i < h->pending_count && !quit; i++) {
        const message *m = &h->pending[i];
        switch (m->kind) {
        case MSG_MOUSE: es2_gui_on_mouse(shell, (es2_gui_mouse_message)m->a, m->b, m->c, m->shift, m->alt); break;
        case MSG_KEY: es2_gui_on_key(shell, (unsigned)m->a, m->b, m->shift, m->alt, m->ctrl); break;
        case MSG_FOCUS: es2_gui_on_focus(shell, m->a); break;
        case MSG_PAINT: es2_gui_on_paint(shell); break;
        case MSG_CLOSE: es2_gui_on_close(shell); break;
        case MSG_QUIT: quit = 1; break;
        }
    }
    h->pending_count = 0;
    return quit;
}

static void report_present(void *context, es2_gui_shell *shell, es2_gui_present_kind kind, const es2_gui_rect *rects,
                           int count) {
    (void)rects;
    headless *h = context;
    char frame[65], table[65], screen[65];
    es2_gui_hash_frame(&shell->frame, frame);
    es2_gui_hash_table(&shell->frame, table);
    es2_gui_hash_screen(&shell->screen, screen);
    static const char *const kinds[] = {"dirty", "full", "clear"};
    out(h, "present %u pass=%u kind=%s rects=%d frame=%s table=%s screen=%s", shell->presents, h->pass, kinds[kind],
        count, frame, table, screen);
}

static void report_cursor(void *context, int busy) { out(context, "cursor %s", busy ? "busy" : "arrow"); }
static void report_fullscreen(void *context, int on) { out(context, "fullscreen %d", on); }

static void report_event(void *context, const es2_gui_event *ev) {
    headless *h = context;
    out(h, "event %u type=0x%x code=0x%x x=%ld y=%ld synthetic=%u target=%lu", h->pass, (unsigned)ev->type,
        (unsigned)ev->code, (long)ev->x, (long)ev->y, (unsigned)ev->synthetic, (unsigned long)ev->target);
    if (h->user.handle) h->user.handle(h->user.context, ev);
}

static void report_system(void *context, es2_gui_shell *shell, es2_gui_system_action action) {
    headless *h = context;
    static const char *const names[] = {"none", "quit_box", "leave_fullscreen", "toggle_fullscreen"};
    out(h, "system %u %s", h->pass, names[action]);
    if (h->user.system) h->user.system(h->user.context, shell, action);
}

static void forward_timer(void *c, es2_gui_shell *s) { headless *h = c; if (h->user.timer) h->user.timer(h->user.context, s); }
static void forward_movie(void *c, es2_gui_shell *s) { headless *h = c; if (h->user.movie) h->user.movie(h->user.context, s); }
static void forward_map(void *c, es2_gui_shell *s) { headless *h = c; if (h->user.map) h->user.map(h->user.context, s); }
static void forward_focus(void *c, es2_gui_shell *s, int g) { headless *h = c; if (h->user.focus) h->user.focus(h->user.context, s, g); }

static int parse_ints(const char *s, int *v, int n) {
    for (int i = 0; i < n; i++) {
        char *end;
        long x = strtol(s, &end, 0);
        if (end == s) return 0;
        v[i] = (int)x;
        s = end;
    }
    while (isspace((unsigned char)*s)) s++;
    return *s == 0;
}

/* Synthetic palette: a seeded linear congruential sequence, 6-bit values, flag 1. */
static void seeded_palette(uint8_t records[256][4], unsigned seed) {
    uint32_t x = seed * 2654435761u + 1;
    for (int i = 0; i < 256; i++)
        for (int c = 0; c < 4; c++) {
            x = x * 1103515245u + 12345u;
            records[i][c] = c == 3 ? 1 : (uint8_t)((x >> 16) & 63);
        }
}

static int key_message(headless *h, const char *args, int down, int up) {
    char name[32];
    int n = 0;
    if (sscanf(args, "%31s%n", name, &n) != 1) return 0;
    unsigned vk = es2_gui_key_by_name(name);
    if (!vk) return 0;
    message m = {MSG_KEY, (int)vk, 0, 0, 0, 0, 0};
    const char *rest = args + n;
    char word[32];
    while (sscanf(rest, "%31s%n", word, &n) == 1) {
        if (!strcmp(word, "shift")) m.shift = 1;
        else if (!strcmp(word, "alt")) m.alt = 1;
        else if (!strcmp(word, "ctrl")) m.ctrl = 1;
        else return 0;
        rest += n;
    }
    if (down) queue_message(h, m);
    if (up) { m.b = 1; queue_message(h, m); }
    return 1;
}

static int command(headless *h, const char *verb, const char *args) {
    es2_gui_shell *shell = h->shell;
    int v[5];
    if (!strcmp(verb, "move") && parse_ints(args, v, 2)) {
        message m = {MSG_MOUSE, ES2_GUI_WM_MOUSEMOVE, v[0], v[1], h->held_left, h->held_right, 0};
        queue_message(h, m);
        return 1;
    }
    static const char *const buttons[] = {"ldown", "lup", "rdown", "rup"};
    for (int i = 0; i < 4; i++) {
        if (strcmp(verb, buttons[i]) || !parse_ints(args, v, 0)) continue;
        if (i == 0) h->held_left = 1;
        if (i == 1) h->held_left = 0;
        if (i == 2) h->held_right = 1;
        if (i == 3) h->held_right = 0;
        /* the message position is the pointer's last position */
        message m = {MSG_MOUSE, ES2_GUI_WM_LBUTTONDOWN + i, 0, 0, h->held_left, h->held_right, 0};
        int x = shell->input.cursor_x, y = shell->input.cursor_y;
        for (size_t k = h->pending_count; k-- > 0;)
            if (h->pending[k].kind == MSG_MOUSE) { x = h->pending[k].b; y = h->pending[k].c; break; }
        m.b = x;
        m.c = y;
        queue_message(h, m);
        return 1;
    }
    if (!strcmp(verb, "key")) return key_message(h, args, 1, 1);
    if (!strcmp(verb, "keydown")) return key_message(h, args, 1, 0);
    if (!strcmp(verb, "keyup")) return key_message(h, args, 0, 1);
    if (!strcmp(verb, "focus") && parse_ints(args, v, 1)) {
        message m = {MSG_FOCUS, v[0] != 0, 0, 0, 0, 0, 0};
        queue_message(h, m);
        return 1;
    }
    if (!strcmp(verb, "step")) {
        int count = 1;
        if (!parse_ints(args, v, 0)) {
            if (!parse_ints(args, v, 1) || v[0] < 0) return 0;
            count = v[0];
        }
        for (int i = 0; i < count; i++) {
            h->pass++;
            if (!es2_gui_step(shell)) { shell->quit = 1; break; }
        }
        return 1;
    }
    if (parse_ints(args, v, 0)) {
        message m = {MSG_PAINT, 0, 0, 0, 0, 0, 0};
        if (!strcmp(verb, "paint")) { queue_message(h, m); return 1; }
        if (!strcmp(verb, "close")) { m.kind = MSG_CLOSE; queue_message(h, m); return 1; }
        if (!strcmp(verb, "quit")) { m.kind = MSG_QUIT; queue_message(h, m); return 1; }
        if (!strcmp(verb, "pattern")) { es2_gui_test_pattern(shell); return 1; }
        if (!strcmp(verb, "clear")) { es2_gui_shell_clear(shell); return 1; }
        if (!strcmp(verb, "present")) { es2_gui_shell_present_full(shell); return 1; }
        return 0;
    }
    if (parse_ints(args, v, 1)) {
        if (!strcmp(verb, "interactive")) { shell->input.interactive = v[0] != 0; return 1; }
        if (!strcmp(verb, "movie")) { shell->input.movie_open = v[0] != 0; return 1; }
        if (!strcmp(verb, "gate")) { shell->frame.realize_gate = v[0] != 0; return 1; }
        if (!strcmp(verb, "busy")) { es2_gui_set_busy(shell, v[0]); return 1; }
        if (!strcmp(verb, "fullscreen")) { shell->fullscreen = v[0] != 0; return 1; }
        if (!strcmp(verb, "click")) { es2_gui_post_synthetic_click(&shell->input, (uint32_t)v[0]); return 1; }
        if (!strcmp(verb, "palette")) {
            uint8_t records[256][4];
            seeded_palette(records, (unsigned)v[0]);
            es2_gui_palette_select(&shell->frame, (const uint8_t(*)[4])records);
            return 1;
        }
    }
    if (!strcmp(verb, "fill") && parse_ints(args, v, 5)) {
        es2_gui_rect r = {v[0], v[1], v[2], v[3]};
        for (int y = r.top < 0 ? 0 : r.top; y <= r.bottom && y < ES2_GUI_HEIGHT; y++)
            for (int x = r.left < 0 ? 0 : r.left; x <= r.right && x < ES2_GUI_WIDTH; x++)
                shell->frame.pixels[y * ES2_GUI_WIDTH + x] = (uint8_t)v[4];
        es2_gui_invalidate_window(&shell->frame, r);
        return 1;
    }
    if (!strcmp(verb, "invalidate") && parse_ints(args, v, 4)) {
        es2_gui_rect r = {v[0], v[1], v[2], v[3]};
        es2_gui_invalidate_rect(&shell->frame, r);
        return 1;
    }
    return 0;
}

int es2_gui_headless_run(const char *script, const es2_gui_args *args, const es2_gui_hooks *hooks,
                         es2_gui_headless_result *result) {
    memset(result, 0, sizeof *result);
    headless h;
    memset(&h, 0, sizeof h);
    if (hooks) h.user = *hooks;
    h.shell = malloc(sizeof *h.shell);
    if (!h.shell) return -1;
    es2_gui_platform platform = {&h, pump, report_present, report_cursor, report_fullscreen};
    es2_gui_hooks own = {&h, report_event, forward_timer, forward_movie, forward_map, report_system, forward_focus};
    es2_gui_shell_init(h.shell, args, &platform, &own);
    const char *p = script ? script : "";
    unsigned line_number = 0;
    while (*p && !h.shell->quit) {
        const char *end = strchr(p, '\n');
        size_t n = end ? (size_t)(end - p) : strlen(p);
        char line[256];
        line_number++;
        if (n >= sizeof line) n = sizeof line - 1;
        memcpy(line, p, n);
        line[n] = 0;
        p = end ? end + 1 : p + strlen(p);
        char *hash = strchr(line, '#');
        if (hash) *hash = 0;
        char *s = line;
        while (isspace((unsigned char)*s)) s++;
        size_t len = strlen(s);
        while (len && isspace((unsigned char)s[len - 1])) s[--len] = 0;
        if (!*s) continue;
        char verb[32];
        int used = 0;
        if (sscanf(s, "%31s%n", verb, &used) != 1) continue;
        if (!command(&h, verb, s + used)) {
            result->errors++;
            out(&h, "error %u %s", line_number, s);
        }
    }
    out(&h, "end passes=%u presents=%u quit=%d", h.pass, h.shell->presents, h.shell->quit);
    result->passes = (int)h.pass;
    free(h.pending);
    free(h.shell);
    if (h.failed) {
        free(h.report);
        return -1;
    }
    result->report = h.report;
    result->length = h.length;
    return 0;
}
