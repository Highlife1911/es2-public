/* The VSHELL input journal (parser and scheduler; no Win32 call, no
 * game memory). The hooks that feed it and inject its messages are in
 * vshell_capture.h. Contract: docs/reverse-engineering/vshell_widgets.md
 * section 7.6 (the journal forms) and vshell_presentation.md "capture tour".
 *
 * A journal is JSONL, one flat object per line, keyed by VSHELL's Windows
 * pump 0x406328: a *pass* is one call of the pump (PeekMessageA until the
 * queue is empty). Entries run in file order:
 *
 *   {"op":"mark","screen":"main_menu","state":"idle"}  labels for the records
 *   {"op":"wait","passes":40,"ms":1500}       both limits must be reached
 *   {"op":"snap"}                             hash the frame now (record "snap")
 *   {"op":"click","x":231,"y":208}            click at a frame point
 *   {"op":"key","vk":13,"mods":0}             key down, later key up
 *   {"op":"close"}                            WM_CLOSE (quit flag, vshell_process.md section 7)
 *
 * "control":"<screen>.<NN>" (vshell_layout.json) is resolved into x/y by
 * capture-vshell-screens.ps1 before the proxy reads the journal; an
 * unresolved control is a parse error, so a raw journal never clicks (0,0).
 * Unknown keys are ignored; empty lines are skipped; any other malformed line
 * refuses the whole journal (its line number is reported).
 *
 * Click (vshell_widgets.md section 7.6, "click at a logical point"): pass A injects WM_MOUSEMOVE
 * to the far client corner (639,479), to (0,0) and to the target. VSHELL
 * posts every mouse event at GetCursorPos, which the capture answers with
 * the last injected point (vshell_capture.h), so all three moves land
 * on the target; the first two only keep the motion from the centre
 * non-zero, so a target at (320,240) is still posted (a move without
 * motion is deleted). Pass B (VJ_GAP passes later) WM_LBUTTONDOWN,
 * pass C WM_LBUTTONUP, both at the target. Key: WM_KEYDOWN (WM_SYSKEYDOWN
 * with Alt) in one pass, the key-up form VJ_GAP passes later; mods bit 0
 * Shift, bit 1 Ctrl, bit 2 Alt answer GetKeyState while the key message is
 * handled (0x405230..0x405263 asks VK_SHIFT, VK_MENU, VK_CONTROL).
 * Click, key, snap and close wait for a *ready* pass (VSHELL focused, no
 * movie open; the hook decides); mark and wait do not. */
#ifndef ES2_VSHELL_JOURNAL_H
#define ES2_VSHELL_JOURNAL_H
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define VJ_LINE_MAX 512u
#define VJ_LABEL_MAX 40u
#define VJ_ENTRY_MAX 4096u
#define VJ_GAP 2u               /* passes between the phases of a click or key */
#define VJ_FAR_X 639
#define VJ_FAR_Y 479

enum { VJ_OP_MARK, VJ_OP_WAIT, VJ_OP_SNAP, VJ_OP_CLICK, VJ_OP_KEY, VJ_OP_CLOSE, VJ_OP_COUNT };
static const char *const vj_op_names[VJ_OP_COUNT] = {"mark", "wait", "snap", "click", "key", "close"};

/* Message numbers (winuser.h values, repeated so this header needs no Windows include). */
#define VJ_WM_CLOSE 0x0010u
#define VJ_WM_KEYDOWN 0x0100u
#define VJ_WM_KEYUP 0x0101u
#define VJ_WM_SYSKEYDOWN 0x0104u
#define VJ_WM_SYSKEYUP 0x0105u
#define VJ_WM_MOUSEMOVE 0x0200u
#define VJ_WM_LBUTTONDOWN 0x0201u
#define VJ_WM_LBUTTONUP 0x0202u
#define VJ_MK_LBUTTON 0x0001u

typedef struct VjEntry {
    int op;
    int32_t x, y;
    uint32_t vk, mods, passes, ms, line;
    char screen[VJ_LABEL_MAX], state[VJ_LABEL_MAX];
} VjEntry;

typedef struct VjMsg { uint32_t message, wparam, lparam, mods; } VjMsg;

/* ---- parsing ---------------------------------------------------------------------------------- */
/* The value after "name": in [line, end), or NULL. Keys are matched as whole quoted strings. */
static const char *vj_field(const char *line, const char *end, const char *name) {
    size_t length = strlen(name);
    for (const char *p = line; p + length + 2 < end; ++p) {
        if (p[0] != '"' || memcmp(p + 1, name, length) || p[1 + length] != '"') continue;
        const char *v = p + 2 + length;
        while (v < end && (*v == ' ' || *v == '\t')) ++v;
        if (v >= end || *v != ':') continue;
        ++v;
        while (v < end && (*v == ' ' || *v == '\t')) ++v;
        return v < end ? v : NULL;
    }
    return NULL;
}

/* 1 = found and valid, 0 = absent, -1 = present but not a decimal integer in [lo, hi]. */
static int vj_int(const char *line, const char *end, const char *name, long lo, long hi, long *out) {
    const char *v = vj_field(line, end, name);
    if (!v) return 0;
    const char *p = v;
    if (p < end && *p == '-') ++p;
    if (p >= end || *p < '0' || *p > '9') return -1;
    long value = 0;
    for (; p < end && *p >= '0' && *p <= '9'; ++p) {
        value = value * 10 + (*p - '0');
        if (value > 100000000L) return -1;
    }
    if (*v == '-') value = -value;
    if (value < lo || value > hi) return -1;
    *out = value;
    return 1;
}

/* Labels: 1..VJ_LABEL_MAX-1 characters of [A-Za-z0-9_.-] (they are written unescaped into JSON). */
static int vj_label(const char *line, const char *end, const char *name, char out[VJ_LABEL_MAX]) {
    const char *v = vj_field(line, end, name);
    if (!v) return 0;
    if (*v != '"') return -1;
    size_t n = 0;
    for (++v; v < end && *v != '"'; ++v) {
        char c = *v;
        int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-';
        if (!ok || n + 1 >= VJ_LABEL_MAX) return -1;
        out[n++] = c;
    }
    if (v >= end || !n) return -1;
    out[n] = 0;
    return 1;
}

/* Parses one line. Returns 1 entry, 0 blank line, -1 error. */
static int vj_parse_line(const char *line, const char *end, uint32_t number, VjEntry *e) {
    while (line < end && (*line == ' ' || *line == '\t' || *line == '\r')) ++line;
    while (end > line && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r')) --end;
    if (line == end) return 0;
    if (*line != '{' || end[-1] != '}' || (size_t)(end - line) > VJ_LINE_MAX) return -1;
    memset(e, 0, sizeof(*e));
    e->line = number;
    const char *op = vj_field(line, end, "op");
    if (!op || *op != '"') return -1;
    e->op = -1;
    for (int i = 0; i < VJ_OP_COUNT; ++i) {
        size_t n = strlen(vj_op_names[i]);
        if (op + n + 1 < end && !memcmp(op + 1, vj_op_names[i], n) && op[1 + n] == '"') e->op = i;
    }
    if (e->op < 0) return -1;
    if (vj_field(line, end, "control") && e->op == VJ_OP_CLICK && !vj_field(line, end, "x")) return -1;
    long a = 0, b = 0;
    switch (e->op) {
    case VJ_OP_MARK:
        if (vj_label(line, end, "screen", e->screen) != 1) return -1;
        if (vj_label(line, end, "state", e->state) < 0) return -1;
        return 1;
    case VJ_OP_WAIT: {
        int p = vj_int(line, end, "passes", 0, 10000000L, &a), m = vj_int(line, end, "ms", 0, 3600000L, &b);
        if (p < 0 || m < 0 || (!p && !m)) return -1;
        e->passes = (uint32_t)a; e->ms = (uint32_t)b;
        return 1;
    }
    case VJ_OP_CLICK:
        if (vj_int(line, end, "x", 0, VJ_FAR_X, &a) != 1 || vj_int(line, end, "y", 0, VJ_FAR_Y, &b) != 1) return -1;
        e->x = (int32_t)a; e->y = (int32_t)b;
        return 1;
    case VJ_OP_KEY: {
        if (vj_int(line, end, "vk", 1, 254, &a) != 1) return -1;
        int m = vj_int(line, end, "mods", 0, 7, &b);
        if (m < 0) return -1;
        e->vk = (uint32_t)a; e->mods = m ? (uint32_t)b : 0;
        return 1;
    }
    default:
        return 1;   /* snap, close */
    }
}

/* Parses a whole journal. Returns the entry count, or -(line number) of the first bad line
 * (-1 - VJ_ENTRY_MAX when there are too many entries). `entries` holds VJ_ENTRY_MAX. */
static long vj_parse(const char *text, size_t size, VjEntry *entries) {
    long count = 0;
    uint32_t number = 0;
    const char *p = text, *stop = text + size;
    while (p < stop) {
        const char *eol = memchr(p, '\n', (size_t)(stop - p));
        const char *end = eol ? eol : stop;
        ++number;
        if (count >= (long)VJ_ENTRY_MAX) return -1 - (long)VJ_ENTRY_MAX;
        int r = vj_parse_line(p, end, number, &entries[count]);
        if (r < 0) return -(long)number;
        count += r;
        p = eol ? eol + 1 : stop;
    }
    return count;
}

/* ---- scheduling ------------------------------------------------------------------------------- */
enum { VJ_EVENT_STEP, VJ_EVENT_MARK, VJ_EVENT_SNAP, VJ_EVENT_DONE };
typedef void (*VjEventFn)(void *context, int kind, uint32_t step, const VjEntry *entry);

typedef struct VjRunner {
    const VjEntry *entries;
    uint32_t count, index, phase;
    uint32_t phase_pass, phase_ms;   /* pass and clock at which the current phase began */
    int started, done;
    char screen[VJ_LABEL_MAX], state[VJ_LABEL_MAX];
    VjEventFn on_event; void *context;
} VjRunner;

static void vj_init(VjRunner *r, const VjEntry *entries, uint32_t count, VjEventFn on_event, void *context) {
    memset(r, 0, sizeof(*r));
    r->entries = entries; r->count = count; r->on_event = on_event; r->context = context;
    strcpy(r->screen, "start");
    if (!count) r->done = 1;
}

static uint32_t vj_lparam(int32_t x, int32_t y) { return ((uint32_t)(uint16_t)y << 16) | (uint16_t)x; }

static void vj_advance(VjRunner *r) {
    ++r->index; r->phase = 0; r->started = 0;
    if (r->index >= r->count) {
        r->done = 1;
        if (r->on_event) r->on_event(r->context, VJ_EVENT_DONE, r->index, NULL);
    }
}

/* Called once at the start of each pump pass. Fills `out` (room for 4) with the messages to inject
 * in this pass and returns their number. `ready`: VSHELL focused and no movie open. Clock in ms. */
static unsigned vj_begin_pass(VjRunner *r, uint32_t pass, uint32_t now_ms, int ready, VjMsg out[4]) {
    while (!r->done) {
        const VjEntry *e = &r->entries[r->index];
        int needs_ready = e->op != VJ_OP_MARK && e->op != VJ_OP_WAIT;
        if (r->phase == 0 && !r->started) {
            if (needs_ready && !ready) return 0;
            r->started = 1; r->phase_pass = pass; r->phase_ms = now_ms;
            if (r->on_event) r->on_event(r->context, VJ_EVENT_STEP, r->index, e);
        }
        switch (e->op) {
        case VJ_OP_MARK:
            strcpy(r->screen, e->screen);
            strcpy(r->state, e->state);
            if (r->on_event) r->on_event(r->context, VJ_EVENT_MARK, r->index, e);
            vj_advance(r);
            continue;
        case VJ_OP_WAIT:
            if (pass - r->phase_pass < e->passes || now_ms - r->phase_ms < e->ms) return 0;
            vj_advance(r);
            continue;
        case VJ_OP_SNAP:
            if (r->on_event) r->on_event(r->context, VJ_EVENT_SNAP, r->index, e);
            vj_advance(r);
            continue;
        case VJ_OP_CLOSE:
            out[0] = (VjMsg){VJ_WM_CLOSE, 0, 0, 0};
            vj_advance(r);
            return 1;
        case VJ_OP_CLICK: {
            uint32_t at = vj_lparam(e->x, e->y);
            if (r->phase == 0) {
                out[0] = (VjMsg){VJ_WM_MOUSEMOVE, 0, vj_lparam(VJ_FAR_X, VJ_FAR_Y), 0};
                out[1] = (VjMsg){VJ_WM_MOUSEMOVE, 0, 0, 0};
                out[2] = (VjMsg){VJ_WM_MOUSEMOVE, 0, at, 0};
                r->phase = 1; r->phase_pass = pass;
                return 3;
            }
            if (pass - r->phase_pass < VJ_GAP || !ready) return 0;
            if (r->phase == 1) {
                out[0] = (VjMsg){VJ_WM_LBUTTONDOWN, VJ_MK_LBUTTON, at, 0};
                r->phase = 2; r->phase_pass = pass;
                return 1;
            }
            out[0] = (VjMsg){VJ_WM_LBUTTONUP, 0, at, 0};
            vj_advance(r);
            return 1;
        }
        case VJ_OP_KEY: {
            int alt = (e->mods & 4) != 0;
            if (r->phase == 0) {
                out[0] = (VjMsg){alt ? VJ_WM_SYSKEYDOWN : VJ_WM_KEYDOWN, e->vk, 1u, e->mods};
                r->phase = 1; r->phase_pass = pass;
                return 1;
            }
            if (pass - r->phase_pass < VJ_GAP || !ready) return 0;
            out[0] = (VjMsg){alt ? VJ_WM_SYSKEYUP : VJ_WM_KEYUP, e->vk, 0xc0000001u, e->mods};
            vj_advance(r);
            return 1;
        }
        default:
            vj_advance(r);
            continue;
        }
    }
    return 0;
}
#endif
