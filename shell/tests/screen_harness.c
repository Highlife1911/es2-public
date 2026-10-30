/* The shell screen test harness (see screen_harness.h). */
#include "screen_harness.h"

#include "es2/formats/sha256.h"
#include "es2/shell/gui/headless.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#else
#include <dirent.h>
#endif

/* ===== a small JSON reader (bounds-checked, depth-limited) =================== */

typedef enum jtype { J_NULL, J_BOOL, J_NUMBER, J_STRING, J_ARRAY, J_OBJECT } jtype;

typedef struct jnode {
    jtype type;
    double number;
    char *string;       /* J_STRING value */
    char *key;          /* member name inside an object */
    struct jnode *child, *next;
    size_t count;
} jnode;

typedef struct jparser {
    const char *p, *end;
    int depth;
    const char *error;
    size_t error_at;
    const char *start;
} jparser;

#define JSON_MAX_DEPTH 64

static void jfree(jnode *n) {
    while (n) {
        jnode *next = n->next;
        jfree(n->child);
        free(n->string);
        free(n->key);
        free(n);
        n = next;
    }
}

static int jfail(jparser *j, const char *message) {
    if (!j->error) {
        j->error = message;
        j->error_at = (size_t)(j->p - j->start);
    }
    return 0;
}

static void jspace(jparser *j) {
    while (j->p < j->end && (*j->p == ' ' || *j->p == '\t' || *j->p == '\n' || *j->p == '\r')) j->p++;
}

static int put_utf8(char *out, size_t *n, size_t cap, unsigned c) {
    if (c < 0x80) {
        if (*n + 1 > cap) return 0;
        out[(*n)++] = (char)c;
    } else if (c < 0x800) {
        if (*n + 2 > cap) return 0;
        out[(*n)++] = (char)(0xc0 | (c >> 6));
        out[(*n)++] = (char)(0x80 | (c & 0x3f));
    } else {
        if (*n + 3 > cap) return 0;
        out[(*n)++] = (char)(0xe0 | (c >> 12));
        out[(*n)++] = (char)(0x80 | ((c >> 6) & 0x3f));
        out[(*n)++] = (char)(0x80 | (c & 0x3f));
    }
    return 1;
}

static char *jstring(jparser *j) {
    if (j->p >= j->end || *j->p != '"') { jfail(j, "string expected"); return NULL; }
    j->p++;
    const char *s = j->p;
    while (s < j->end && *s != '"') s += (*s == '\\' && s + 1 < j->end) ? 2 : 1;
    if (s >= j->end) { jfail(j, "unterminated string"); return NULL; }
    size_t cap = (size_t)(s - j->p) + 1; /* escapes never grow the text */
    char *out = malloc(cap);
    if (!out) { jfail(j, "out of memory"); return NULL; }
    size_t n = 0;
    while (j->p < s) {
        unsigned char c = (unsigned char)*j->p++;
        if (c < 0x20) { free(out); jfail(j, "control character in string"); return NULL; }
        if (c != '\\') { out[n++] = (char)c; continue; }
        char e = *j->p++;
        unsigned u = 0;
        switch (e) {
        case '"': u = '"'; break;
        case '\\': u = '\\'; break;
        case '/': u = '/'; break;
        case 'b': u = 8; break;
        case 'f': u = 12; break;
        case 'n': u = 10; break;
        case 'r': u = 13; break;
        case 't': u = 9; break;
        case 'u':
            if (s - j->p < 4) { free(out); jfail(j, "short \\u escape"); return NULL; }
            for (int i = 0; i < 4; i++) {
                char h = *j->p++;
                u <<= 4;
                if (h >= '0' && h <= '9') u |= (unsigned)(h - '0');
                else if (h >= 'a' && h <= 'f') u |= (unsigned)(h - 'a' + 10);
                else if (h >= 'A' && h <= 'F') u |= (unsigned)(h - 'A' + 10);
                else { free(out); jfail(j, "bad \\u escape"); return NULL; }
            }
            if (u >= 0xd800 && u <= 0xdfff) u = '?'; /* surrogates: not needed by our tables */
            if (u == 0) { free(out); jfail(j, "NUL in string"); return NULL; }
            break;
        default: free(out); jfail(j, "bad escape"); return NULL;
        }
        if (!put_utf8(out, &n, cap - 1, u)) { free(out); jfail(j, "escape too long"); return NULL; }
    }
    out[n] = 0;
    j->p = s + 1;
    return out;
}

static jnode *jvalue(jparser *j);

static jnode *jnew(jparser *j, jtype type) {
    jnode *n = calloc(1, sizeof *n);
    if (!n) jfail(j, "out of memory");
    else n->type = type;
    return n;
}

static jnode *jcontainer(jparser *j, jtype type) {
    char close = type == J_ARRAY ? ']' : '}';
    if (++j->depth > JSON_MAX_DEPTH) { jfail(j, "nesting too deep"); return NULL; }
    j->p++;
    jnode *n = jnew(j, type);
    if (!n) return NULL;
    jnode **tail = &n->child;
    jspace(j);
    if (j->p < j->end && *j->p == close) { j->p++; j->depth--; return n; }
    for (;;) {
        char *key = NULL;
        jspace(j);
        if (type == J_OBJECT) {
            key = jstring(j);
            if (!key) break;
            jspace(j);
            if (j->p >= j->end || *j->p != ':') { free(key); jfail(j, "':' expected"); break; }
            j->p++;
        }
        jnode *v = jvalue(j);
        if (!v) { free(key); break; }
        v->key = key;
        *tail = v;
        tail = &v->next;
        n->count++;
        jspace(j);
        if (j->p < j->end && *j->p == ',') { j->p++; continue; }
        if (j->p < j->end && *j->p == close) { j->p++; j->depth--; return n; }
        jfail(j, type == J_ARRAY ? "',' or ']' expected" : "',' or '}' expected");
        break;
    }
    jfree(n);
    return NULL;
}

static int jword(jparser *j, const char *w) {
    size_t n = strlen(w);
    if ((size_t)(j->end - j->p) < n || memcmp(j->p, w, n)) return 0;
    j->p += n;
    return 1;
}

static jnode *jvalue(jparser *j) {
    jspace(j);
    if (j->p >= j->end) { jfail(j, "value expected"); return NULL; }
    char c = *j->p;
    if (c == '{') return jcontainer(j, J_OBJECT);
    if (c == '[') return jcontainer(j, J_ARRAY);
    if (c == '"') {
        char *s = jstring(j);
        if (!s) return NULL;
        jnode *n = jnew(j, J_STRING);
        if (!n) { free(s); return NULL; }
        n->string = s;
        return n;
    }
    if (jword(j, "null")) return jnew(j, J_NULL);
    if (jword(j, "true")) { jnode *n = jnew(j, J_BOOL); if (n) n->number = 1; return n; }
    if (jword(j, "false")) return jnew(j, J_BOOL);
    if (c == '-' || (c >= '0' && c <= '9')) {
        char buffer[64];
        size_t n = 0;
        while (j->p + n < j->end && n < sizeof buffer - 1 && strchr("+-0123456789.eE", j->p[n])) n++;
        if (j->p + n < j->end && strchr("+-0123456789.eE", j->p[n])) { jfail(j, "number too long"); return NULL; }
        memcpy(buffer, j->p, n);
        buffer[n] = 0;
        char *stop;
        double v = strtod(buffer, &stop);
        if (stop != buffer + n) { jfail(j, "bad number"); return NULL; }
        j->p += n;
        jnode *node = jnew(j, J_NUMBER);
        if (node) node->number = v;
        return node;
    }
    jfail(j, "unexpected character");
    return NULL;
}

static jnode *jparse(const char *text, size_t length, char *err, size_t err_size) {
    jparser j = {text, text + length, 0, NULL, 0, text};
    jnode *root = jvalue(&j);
    if (root) {
        jspace(&j);
        if (j.p != j.end) { jfail(&j, "text after the value"); jfree(root); root = NULL; }
    }
    if (!root && err && err_size) snprintf(err, err_size, "JSON: %s at byte %zu", j.error ? j.error : "?", j.error_at);
    return root;
}

static const jnode *jget(const jnode *object, const char *key) {
    if (!object || object->type != J_OBJECT) return NULL;
    for (const jnode *c = object->child; c; c = c->next)
        if (c->key && !strcmp(c->key, key)) return c;
    return NULL;
}

static const char *jstr(const jnode *object, const char *key) {
    const jnode *n = jget(object, key);
    return n && n->type == J_STRING ? n->string : NULL;
}

static int read_file(const char *path, char **text, size_t *length) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    size_t cap = 1 << 16, n = 0;
    char *buffer = malloc(cap);
    while (buffer) {
        if (n == cap) {
            char *grown = cap < (64u << 20) ? realloc(buffer, cap * 2) : NULL; /* 64 MiB limit */
            if (!grown) { free(buffer); buffer = NULL; break; }
            buffer = grown;
            cap *= 2;
        }
        size_t got = fread(buffer + n, 1, cap - n, f);
        n += got;
        if (got == 0) break;
    }
    int bad = ferror(f);
    fclose(f);
    if (!buffer || bad) { free(buffer); return -1; }
    *text = buffer;
    *length = n;
    return 0;
}

static void copy_text(char *dst, size_t size, const char *src) {
    snprintf(dst, size, "%s", src ? src : "");
}

/* ===== layout table ============================================================ */

static int rect_of(const jnode *a, es2_gui_rect *r) {
    if (!a || a->type != J_ARRAY || a->count != 4) return 0;
    double v[4];
    int i = 0;
    for (const jnode *c = a->child; c; c = c->next, i++) {
        if (c->type != J_NUMBER || c->number < -100000 || c->number > 100000 || c->number != (double)(int32_t)c->number)
            return 0;
        v[i] = c->number;
    }
    r->left = (int32_t)v[0];
    r->top = (int32_t)v[1];
    r->right = (int32_t)v[2];
    r->bottom = (int32_t)v[3];
    return 1;
}

int es2_screen_layout_parse(const char *text, size_t length, es2_screen_layout *out, char *err, size_t err_size) {
    memset(out, 0, sizeof *out);
    jnode *root = jparse(text, length, err, err_size);
    if (!root) return -1;
    const jnode *schema = jget(root, "schema");
    const jnode *screens = jget(root, "screens");
    int result = -1;
    if (!schema || schema->type != J_NUMBER || schema->number != 1) {
        snprintf(err, err_size, "layout: schema 1 expected");
        goto done;
    }
    if (!screens || screens->type != J_ARRAY) {
        snprintf(err, err_size, "layout: no screens array");
        goto done;
    }
    size_t total = 0;
    for (const jnode *s = screens->child; s; s = s->next) {
        const jnode *controls = jget(s, "controls");
        if (!jstr(s, "name") || !controls || controls->type != J_ARRAY) {
            snprintf(err, err_size, "layout: screen without name or controls");
            goto done;
        }
        total += controls->count;
    }
    out->controls = calloc(total ? total : 1, sizeof *out->controls);
    if (!out->controls) { snprintf(err, err_size, "out of memory"); goto done; }
    for (const jnode *s = screens->child; s; s = s->next) {
        for (const jnode *c = jget(s, "controls")->child; c; c = c->next) {
            const char *id = jstr(c, "id"), *cls = jstr(c, "class");
            if (!id || !cls || strlen(id) >= sizeof out->controls->id || strlen(cls) >= sizeof out->controls->cls) {
                snprintf(err, err_size, "layout: control %zu: bad id or class", out->count);
                es2_screen_layout_free(out);
                goto done;
            }
            es2_screen_control *k = &out->controls[out->count++];
            copy_text(k->id, sizeof k->id, id);
            copy_text(k->cls, sizeof k->cls, cls);
            copy_text(k->owner, sizeof k->owner, jstr(c, "owner"));
            const jnode *r = jget(c, "screen_rect");
            if (r && r->type != J_NULL) {
                if (!rect_of(r, &k->rect)) {
                    snprintf(err, err_size, "layout: %s: screen_rect is not four integers", id);
                    es2_screen_layout_free(out);
                    goto done;
                }
                k->has_rect = 1;
            }
            k->text = jget(jget(c, "args"), "font") != NULL && strcmp(cls, "ESTitle") != 0;
            const jnode *h = jget(c, "handler");
            int handler = h && ((h->type == J_NUMBER && h->number != 0) || h->type == J_STRING);
            k->clickable = handler && !k->owner[0];
        }
    }
    result = 0;
done:
    jfree(root);
    return result;
}

int es2_screen_layout_load(const char *path, es2_screen_layout *out, char *err, size_t err_size) {
    char *text;
    size_t length;
    memset(out, 0, sizeof *out);
    if (read_file(path, &text, &length)) {
        snprintf(err, err_size, "cannot read %s", path);
        return -1;
    }
    int r = es2_screen_layout_parse(text, length, out, err, err_size);
    free(text);
    return r;
}

void es2_screen_layout_free(es2_screen_layout *layout) {
    free(layout->controls);
    layout->controls = NULL;
    layout->count = 0;
}

const es2_screen_control *es2_screen_layout_find(const es2_screen_layout *layout, const char *id) {
    if (!layout) return NULL;
    for (size_t i = 0; i < layout->count; i++)
        if (!strcmp(layout->controls[i].id, id)) return &layout->controls[i];
    return NULL;
}

static int32_t floor_half(int32_t v) { return v >= 0 ? v / 2 : -((-v + 1) / 2); }

int es2_screen_control_centre(const es2_screen_control *control, int32_t *x, int32_t *y) {
    if (!control || !control->has_rect) return 0;
    *x = floor_half(control->rect.left + control->rect.right);
    *y = floor_half(control->rect.top + control->rect.bottom);
    return 1;
}

/* ===== references (frames.json) ================================================ */

static int is_sha(const char *s) {
    if (!s || strlen(s) != 64) return 0;
    for (int i = 0; i < 64; i++)
        if (!isxdigit((unsigned char)s[i])) return 0;
    return 1;
}

int es2_screen_references_parse(const char *text, size_t length, es2_screen_references *out, char *err,
                                size_t err_size) {
    memset(out, 0, sizeof *out);
    jnode *root = jparse(text, length, err, err_size);
    if (!root) return -1;
    int result = -1;
    const jnode *schema = jget(root, "schema"), *screens = jget(root, "screens");
    const char *kind = jstr(root, "kind");
    if (!schema || schema->type != J_NUMBER || schema->number != 1 || !kind || strcmp(kind, "es2-vshell-screens")) {
        snprintf(err, err_size, "frames.json: schema 1 of kind es2-vshell-screens expected");
        goto done;
    }
    if (!screens || screens->type != J_ARRAY) {
        snprintf(err, err_size, "frames.json: no screens array");
        goto done;
    }
    out->items = calloc(screens->count ? screens->count : 1, sizeof *out->items);
    if (!out->items) { snprintf(err, err_size, "out of memory"); goto done; }
    for (const jnode *s = screens->child; s; s = s->next) {
        const char *run = jstr(s, "run"), *screen = jstr(s, "screen"), *state = jstr(s, "state");
        const char *index = jstr(s, "index_sha256");
        const jnode *palette = jget(s, "palette_sha256"), *step = jget(s, "step");
        size_t i = out->count;
        if (!run || !screen || !state || !is_sha(index) ||
            !(palette && (palette->type == J_NULL || (palette->type == J_STRING && is_sha(palette->string))))) {
            snprintf(err, err_size, "frames.json: screens[%zu]: run, screen, state, index_sha256, palette_sha256 expected",
                     i);
            es2_screen_references_free(out);
            goto done;
        }
        es2_screen_reference *r = &out->items[out->count++];
        if (snprintf(r->key, sizeof r->key, "%s:%s:%s", run, screen, state) >= (int)sizeof r->key) {
            snprintf(err, err_size, "frames.json: screens[%zu]: labels too long", i);
            es2_screen_references_free(out);
            goto done;
        }
        copy_text(r->run, sizeof r->run, run);
        r->step = step && step->type == J_NUMBER ? (int)step->number : -1;
        copy_text(r->index_sha256, sizeof r->index_sha256, index);
        copy_text(r->palette_sha256, sizeof r->palette_sha256, palette->type == J_STRING ? palette->string : "");
        for (char *c = r->index_sha256; *c; c++) *c = (char)tolower((unsigned char)*c);
        for (char *c = r->palette_sha256; *c; c++) *c = (char)tolower((unsigned char)*c);
    }
    result = 0;
done:
    jfree(root);
    return result;
}

int es2_screen_references_load(const char *path, es2_screen_references *out, char *err, size_t err_size) {
    char *text;
    size_t length;
    memset(out, 0, sizeof *out);
    if (read_file(path, &text, &length)) {
        snprintf(err, err_size, "cannot read %s", path);
        return -1;
    }
    int r = es2_screen_references_parse(text, length, out, err, err_size);
    free(text);
    if (r == 0) {
        copy_text(out->session, sizeof out->session, path);
        char *slash = strrchr(out->session, '/'), *back = strrchr(out->session, '\\');
        if (back > slash) slash = back;
        if (slash) *slash = 0;
        else copy_text(out->session, sizeof out->session, ".");
    }
    return r;
}

void es2_screen_references_free(es2_screen_references *refs) {
    free(refs->items);
    refs->items = NULL;
    refs->count = 0;
}

const es2_screen_reference *es2_screen_references_find(const es2_screen_references *refs, const char *key) {
    if (!refs) return NULL;
    for (size_t i = 0; i < refs->count; i++)
        if (!strcmp(refs->items[i].key, key)) return &refs->items[i];
    return NULL;
}

static int file_exists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f) fclose(f);
    return f != NULL;
}

int es2_screen_references_locate(char *path, size_t size) {
    const char *explicit_path = getenv("ES2_VSHELL_SCREENS");
    if (explicit_path && *explicit_path) {
        size_t n = strlen(explicit_path);
        if (n >= 11 && !strcmp(explicit_path + n - 11, "frames.json"))
            snprintf(path, size, "%s", explicit_path);
        else
            snprintf(path, size, "%s/frames.json", explicit_path);
        return 1; /* configured: a missing file is an error for the caller */
    }
    const char *repo = getenv("ES2_DATA_REPO");
    const char *root = getenv("ES2_DATA_ROOT");
    char logs[512];
    if (repo && *repo)
        snprintf(logs, sizeof logs, "%s/logs", repo);
    else if (root && *root)
        snprintf(logs, sizeof logs, "%s/../../logs", root);
    else
        return 0;
    char best[256] = "";
#ifdef _WIN32
    char pattern[600];
    snprintf(pattern, sizeof pattern, "%s/vshell-screens-*", logs);
    struct _finddata_t found;
    intptr_t h = _findfirst(pattern, &found);
    if (h != -1) {
        do {
            char candidate[1024];
            snprintf(candidate, sizeof candidate, "%s/%s/frames.json", logs, found.name);
            if ((found.attrib & _A_SUBDIR) && file_exists(candidate) && strcmp(found.name, best) > 0)
                copy_text(best, sizeof best, found.name);
        } while (_findnext(h, &found) == 0);
        _findclose(h);
    }
#else
    DIR *d = opendir(logs);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d))) {
            char candidate[1024];
            if (strncmp(e->d_name, "vshell-screens-", 15)) continue;
            snprintf(candidate, sizeof candidate, "%s/%s/frames.json", logs, e->d_name);
            if (file_exists(candidate) && strcmp(e->d_name, best) > 0) copy_text(best, sizeof best, e->d_name);
        }
        closedir(d);
    }
#endif
    if (!best[0]) return 0;
    snprintf(path, size, "%s/%s/frames.json", logs, best);
    return 1;
}

/* ===== difference ============================================================== */

const char *es2_screen_class_name(es2_screen_class c) {
    static const char *const names[] = {"same", "palette", "geometry", "font", "state", "unknown"};
    return (unsigned)c < sizeof names / sizeof names[0] ? names[c] : "?";
}

static int in_rect(const es2_gui_rect *r, int x, int y) {
    return x >= r->left && x <= r->right && y >= r->top && y <= r->bottom;
}

static const es2_screen_control *control_at(const es2_screen_layout *layout, const char *screen, int x, int y) {
    if (!layout || !screen) return NULL;
    size_t prefix = strlen(screen);
    const es2_screen_control *best = NULL;
    long best_area = 0;
    for (size_t i = 0; i < layout->count; i++) {
        const es2_screen_control *c = &layout->controls[i];
        if (!c->has_rect || strncmp(c->id, screen, prefix) || c->id[prefix] != '.') continue;
        if (!in_rect(&c->rect, x, y)) continue;
        long area = (long)(c->rect.right - c->rect.left + 1) * (long)(c->rect.bottom - c->rect.top + 1);
        if (!best || area < best_area) { best = c; best_area = area; }
    }
    return best;
}

/* Bounding box of the 8-connected region of differing pixels around (sx, sy). */
static es2_gui_rect region(const uint8_t *a, const uint8_t *b, int sx, int sy) {
    es2_gui_rect r = {sx, sy, sx, sy};
    uint8_t *seen = calloc(ES2_GUI_PIXELS, 1);
    int32_t *stack = malloc(sizeof(int32_t) * ES2_GUI_PIXELS);
    if (!seen || !stack) { free(seen); free(stack); return r; }
    size_t top = 0;
    stack[top++] = sy * ES2_GUI_WIDTH + sx;
    seen[sy * ES2_GUI_WIDTH + sx] = 1;
    while (top) {
        int32_t i = stack[--top];
        int x = i % ES2_GUI_WIDTH, y = i / ES2_GUI_WIDTH;
        if (x < r.left) r.left = x;
        if (x > r.right) r.right = x;
        if (y < r.top) r.top = y;
        if (y > r.bottom) r.bottom = y;
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++) {
                int nx = x + dx, ny = y + dy;
                if (nx < 0 || ny < 0 || nx >= ES2_GUI_WIDTH || ny >= ES2_GUI_HEIGHT) continue;
                int32_t k = ny * ES2_GUI_WIDTH + nx;
                if (seen[k] || a[k] == b[k]) continue;
                seen[k] = 1;
                stack[top++] = k;
            }
    }
    free(seen);
    free(stack);
    return r;
}

/* Smaller blocks match some shift by chance too often to be called a move. */
#define GEOMETRY_MIN_AREA 16

/* Is the block r of `actual` the block of `expected` moved by (dx, dy)? */
static int moved_by(const uint8_t *expected, const uint8_t *actual, es2_gui_rect r, int dx, int dy) {
    for (int y = r.top; y <= r.bottom; y++)
        for (int x = r.left; x <= r.right; x++) {
            int sx = x - dx, sy = y - dy;
            if (sx < 0 || sy < 0 || sx >= ES2_GUI_WIDTH || sy >= ES2_GUI_HEIGHT) return 0;
            if (actual[y * ES2_GUI_WIDTH + x] != expected[sy * ES2_GUI_WIDTH + sx]) return 0;
        }
    return 1;
}

void es2_screen_compare(const uint8_t expected[ES2_GUI_PIXELS], const uint8_t expected_table[256][3],
                        const uint8_t actual[ES2_GUI_PIXELS], const uint8_t actual_table[256][3],
                        const es2_screen_layout *layout, const char *screen, es2_screen_diff *out) {
    memset(out, 0, sizeof *out);
    out->first_entry = out->last_entry = -1;
    for (int i = 0; i < 256; i++)
        if (memcmp(expected_table[i], actual_table[i], 3)) {
            if (out->first_entry < 0) out->first_entry = i;
            out->last_entry = i;
        }
    out->table_differs = out->first_entry >= 0;
    size_t first = ES2_GUI_PIXELS;
    for (size_t i = 0; i < ES2_GUI_PIXELS; i++)
        if (expected[i] != actual[i]) {
            if (first == ES2_GUI_PIXELS) first = i;
            out->pixels++;
        }
    if (first == ES2_GUI_PIXELS) {
        out->cls = out->table_differs ? ES2_SCREEN_PALETTE : ES2_SCREEN_SAME;
        if (out->table_differs) {
            es2_gui_rect all = {0, 0, ES2_GUI_WIDTH - 1, ES2_GUI_HEIGHT - 1};
            out->rect = all;
        }
        return;
    }
    int fx = (int)(first % ES2_GUI_WIDTH), fy = (int)(first / ES2_GUI_WIDTH);
    out->rect = region(expected, actual, fx, fy);
    const es2_screen_control *c = control_at(layout, screen, fx, fy);
    if (c) copy_text(out->control, sizeof out->control, c->id);
    long area = (long)(out->rect.right - out->rect.left + 1) * (long)(out->rect.bottom - out->rect.top + 1);
    for (int d = 1; d <= 8 && area >= GEOMETRY_MIN_AREA; d++) /* nearest shift first */
        for (int dy = -d; dy <= d; dy++)
            for (int dx = -d; dx <= d; dx++) {
                if (abs(dx) != d && abs(dy) != d) continue;
                if (moved_by(expected, actual, out->rect, dx, dy)) { out->cls = ES2_SCREEN_GEOMETRY; return; }
            }
    out->cls = c && c->text ? ES2_SCREEN_FONT : ES2_SCREEN_STATE;
}

void es2_screen_diff_format(const es2_screen_diff *d, char *line, size_t size) {
    if (d->cls == ES2_SCREEN_SAME) { snprintf(line, size, "class=same"); return; }
    if (d->cls == ES2_SCREEN_PALETTE) {
        if (d->first_entry < 0) snprintf(line, size, "class=palette (colour table hash differs)");
        else snprintf(line, size, "class=palette entries=%d..%d", d->first_entry, d->last_entry);
        return;
    }
    if (d->cls == ES2_SCREEN_UNKNOWN) {
        snprintf(line, size, "class=unknown (index hash differs; no reference frame to compare with)%s",
                 d->table_differs ? " table=differs" : "");
        return;
    }
    snprintf(line, size, "class=%s rect=(%ld,%ld,%ld,%ld) control=%s pixels=%zu%s", es2_screen_class_name(d->cls),
             (long)d->rect.left, (long)d->rect.top, (long)d->rect.right, (long)d->rect.bottom,
             d->control[0] ? d->control : "-", d->pixels, d->table_differs ? " table=differs" : "");
}

/* ===== session ================================================================= */

typedef enum message_kind { MSG_MOUSE, MSG_KEY, MSG_FOCUS, MSG_PAINT, MSG_CLOSE, MSG_QUIT } message_kind;
typedef struct es2_screen_message {
    message_kind kind;
    int a, b, c;          /* mouse: message, x, y; key: vk, up; focus: gained */
    int m1, m2, m3;       /* mouse: MK_LBUTTON, MK_RBUTTON; key: shift, alt, ctrl */
} es2_screen_message;

static void *grow(void *items, size_t *capacity, size_t count, size_t size) {
    if (count < *capacity) return items;
    size_t next = *capacity ? *capacity * 2 : 32;
    void *grown = realloc(items, next * size);
    if (grown) *capacity = next;
    return grown;
}

static void queue_message(es2_screen_session *s, es2_screen_message m) {
    es2_screen_message *g = grow(s->pending, &s->pending_capacity, s->pending_count, sizeof m);
    if (!g) { s->errors++; return; }
    s->pending = g;
    s->pending[s->pending_count++] = m;
}

/* The platform pump: deliver the queued window messages as the own shell's
 * headless platform does (headless.c), in order, then report WM_QUIT. */
static int pump(void *context, es2_gui_shell *shell) {
    es2_screen_session *s = context;
    int quit = 0;
    for (size_t i = 0; i < s->pending_count && !quit; i++) {
        const es2_screen_message *m = &s->pending[i];
        switch (m->kind) {
        case MSG_MOUSE: es2_gui_on_mouse(shell, (es2_gui_mouse_message)m->a, m->b, m->c, m->m1, m->m2); break;
        case MSG_KEY: es2_gui_on_key(shell, (unsigned)m->a, m->b, m->m1, m->m2, m->m3); break;
        case MSG_FOCUS: es2_gui_on_focus(shell, m->a); break;
        case MSG_PAINT: es2_gui_on_paint(shell); break;
        case MSG_CLOSE: es2_gui_on_close(shell); break;
        case MSG_QUIT: quit = 1; break;
        }
    }
    s->pending_count = 0;
    return quit;
}

static void table_hash(const es2_gui_frame *frame, char hex[65]) {
    es2f_sha256_of(frame->table, sizeof frame->table, hex); /* red, green, blue per entry: the capture tour's form */
}

static void on_present(void *context, es2_gui_shell *shell, es2_gui_present_kind kind, const es2_gui_rect *rects,
                       int count) {
    (void)rects;
    (void)count;
    es2_screen_session *s = context;
    es2_screen_present *g = grow(s->presents, &s->present_capacity, s->present_count, sizeof *g);
    if (!g) { s->errors++; return; }
    s->presents = g;
    es2_screen_present *p = &s->presents[s->present_count++];
    p->pass = s->pass;
    p->seq = shell->presents;
    p->kind = kind;
    es2_gui_hash_frame(&shell->frame, p->frame);
    table_hash(&shell->frame, p->table);
}

static void on_event(void *context, const es2_gui_event *ev) {
    es2_screen_session *s = context;
    es2_gui_event *g = grow(s->events, &s->event_capacity, s->event_count, sizeof *g);
    if (g) {
        s->events = g;
        s->events[s->event_count++] = *ev;
    } else {
        s->errors++;
    }
    if (s->screen.handle) s->screen.handle(s->screen.context, ev);
}

static void fwd_timer(void *c, es2_gui_shell *sh) { es2_screen_session *s = c; if (s->screen.timer) s->screen.timer(s->screen.context, sh); }
static void fwd_movie(void *c, es2_gui_shell *sh) { es2_screen_session *s = c; if (s->screen.movie) s->screen.movie(s->screen.context, sh); }
static void fwd_map(void *c, es2_gui_shell *sh) { es2_screen_session *s = c; if (s->screen.map) s->screen.map(s->screen.context, sh); }
static void fwd_system(void *c, es2_gui_shell *sh, es2_gui_system_action a) { es2_screen_session *s = c; if (s->screen.system) s->screen.system(s->screen.context, sh, a); }
static void fwd_focus(void *c, es2_gui_shell *sh, int g) { es2_screen_session *s = c; if (s->screen.focus) s->screen.focus(s->screen.context, sh, g); }

int es2_screen_session_open(es2_screen_session *s, const es2_screen_layout *layout, const es2_gui_args *args,
                            const es2_gui_hooks *screen) {
    memset(s, 0, sizeof *s);
    s->layout = layout;
    if (screen) s->screen = *screen;
    s->shell = malloc(sizeof *s->shell);
    if (!s->shell) return -1;
    es2_gui_platform platform = {s, pump, on_present, NULL, NULL};
    es2_gui_hooks own = {s, on_event, fwd_timer, fwd_movie, fwd_map, fwd_system, fwd_focus};
    es2_gui_shell_init(s->shell, args, &platform, &own);
    return 0;
}

void es2_screen_session_close(es2_screen_session *s) {
    for (size_t i = 0; i < s->snap_count; i++) free(s->snaps[i].pixels);
    free(s->snaps);
    free(s->presents);
    free(s->events);
    free(s->pending);
    free(s->shell);
    memset(s, 0, sizeof *s);
}

static void session_error(es2_screen_session *s, unsigned line, const char *text, const char *why) {
    s->errors++;
    if (!s->error[0]) snprintf(s->error, sizeof s->error, "script line %u: %s (%s)", line, why, text);
}

static int steps(es2_screen_session *s, int count) {
    for (int i = 0; i < count && !s->shell->quit; i++) {
        s->pass++;
        if (!es2_gui_step(s->shell)) s->shell->quit = 1;
    }
    return 1;
}

static int parse_ints(const char *p, int *v, int n) {
    for (int i = 0; i < n; i++) {
        char *end;
        long x = strtol(p, &end, 0);
        if (end == p || x < -100000 || x > 100000) return 0;
        v[i] = (int)x;
        p = end;
    }
    while (isspace((unsigned char)*p)) p++;
    return *p == 0;
}

static void mouse(es2_screen_session *s, es2_gui_mouse_message msg, int x, int y) {
    es2_screen_message m = {MSG_MOUSE, msg, x, y, s->held_left, s->held_right, 0};
    queue_message(s, m);
}

static void button(es2_screen_session *s, int which) { /* 0 ldown, 1 lup, 2 rdown, 3 rup */
    if (which == 0) s->held_left = 1;
    if (which == 1) s->held_left = 0;
    if (which == 2) s->held_right = 1;
    if (which == 3) s->held_right = 0;
    int x = s->shell->input.cursor_x, y = s->shell->input.cursor_y; /* the pointer's last position */
    for (size_t k = s->pending_count; k-- > 0;)
        if (s->pending[k].kind == MSG_MOUSE) { x = s->pending[k].b; y = s->pending[k].c; break; }
    mouse(s, (es2_gui_mouse_message)(ES2_GUI_WM_LBUTTONDOWN + which), x, y);
}

static int key_args(const char *args, unsigned *vk, int mods[3]) {
    char name[32];
    int n = 0;
    if (sscanf(args, "%31s%n", name, &n) != 1) return 0;
    *vk = es2_gui_key_by_name(name);
    if (!*vk) return 0;
    mods[0] = mods[1] = mods[2] = 0;
    const char *rest = args + n;
    char word[32];
    while (sscanf(rest, "%31s%n", word, &n) == 1) {
        if (!strcmp(word, "shift")) mods[0] = 1;
        else if (!strcmp(word, "alt")) mods[1] = 1;
        else if (!strcmp(word, "ctrl")) mods[2] = 1;
        else return 0;
        rest += n;
    }
    return 1;
}

static void key(es2_screen_session *s, unsigned vk, int up, const int mods[3]) {
    es2_screen_message m = {MSG_KEY, (int)vk, up, 0, mods[0], mods[1], mods[2]};
    queue_message(s, m);
}

/* the capture tour's key form (vshell_presentation.md, "Journal"). */
static void press(es2_screen_session *s, unsigned vk, const int mods[3]) {
    key(s, vk, 0, mods);
    steps(s, 2);
    key(s, vk, 1, mods);
    steps(s, 1);
}

/* The key and Shift state that give character c in VSHELL's tables. */
static int key_for_character(unsigned c, unsigned *vk, int *shift) {
    if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
    for (int sh = 0; sh < 2; sh++)
        for (unsigned v = 1; v < 256; v++) {
            unsigned index = es2_gui_key_index(v);
            if (index && es2_gui_key_character(index | (sh ? ES2_GUI_KEY_SHIFT : 0)) == c) {
                *vk = v;
                *shift = sh;
                return 1;
            }
        }
    return 0;
}

static void snap(es2_screen_session *s, const char *label) {
    es2_screen_snap *g = grow(s->snaps, &s->snap_capacity, s->snap_count, sizeof *g);
    if (!g) { s->errors++; return; }
    s->snaps = g;
    es2_screen_snap *p = &s->snaps[s->snap_count];
    memset(p, 0, sizeof *p);
    p->pixels = malloc(ES2_GUI_PIXELS);
    if (!p->pixels) { s->errors++; return; }
    s->snap_count++;
    copy_text(p->label, sizeof p->label, label);
    p->pass = s->pass;
    memcpy(p->pixels, s->shell->frame.pixels, ES2_GUI_PIXELS);
    memcpy(p->rgb, s->shell->frame.table, sizeof p->rgb);
    es2_gui_hash_frame(&s->shell->frame, p->frame);
    table_hash(&s->shell->frame, p->table);
    es2_gui_hash_screen(&s->shell->screen, p->screen);
}

static int command(es2_screen_session *s, unsigned line, const char *text, const char *verb, const char *args) {
    int v[2];
    if (!strcmp(verb, "step")) {
        if (parse_ints(args, v, 0)) return steps(s, 1);
        if (parse_ints(args, v, 1) && v[0] >= 0) return steps(s, v[0]);
        return 0;
    }
    if (!strcmp(verb, "move") && parse_ints(args, v, 2)) { mouse(s, ES2_GUI_WM_MOUSEMOVE, v[0], v[1]); return 1; }
    static const char *const buttons[] = {"ldown", "lup", "rdown", "rup"};
    for (int i = 0; i < 4; i++)
        if (!strcmp(verb, buttons[i]) && parse_ints(args, v, 0)) { button(s, i); return 1; }
    if (!strcmp(verb, "key") || !strcmp(verb, "keydown") || !strcmp(verb, "keyup") || !strcmp(verb, "press")) {
        unsigned vk;
        int mods[3];
        if (!key_args(args, &vk, mods)) return 0;
        if (!strcmp(verb, "press")) { press(s, vk, mods); return 1; }
        if (strcmp(verb, "keyup")) key(s, vk, 0, mods);
        if (strcmp(verb, "keydown")) key(s, vk, 1, mods);
        return 1;
    }
    if (!strcmp(verb, "type")) {
        while (isspace((unsigned char)*args)) args++;
        if (!*args) return 0;
        for (const char *c = args; *c; c++) { /* refuse the whole line before sending anything */
            unsigned vk;
            int shift;
            if (!key_for_character((unsigned char)*c, &vk, &shift)) {
                session_error(s, line, text, "character has no key in VSHELL's tables");
                return 1;
            }
        }
        for (const char *c = args; *c; c++) {
            unsigned vk;
            int shift, mods[3] = {0, 0, 0};
            key_for_character((unsigned char)*c, &vk, &shift);
            mods[0] = shift;
            press(s, vk, mods);
        }
        return 1;
    }
    if (!strcmp(verb, "click")) {
        char id[64], side[16] = "";
        int n = sscanf(args, "%63s %15s", id, side);
        if (n < 1 || (n == 2 && strcmp(side, "right"))) return 0;
        const es2_screen_control *c = es2_screen_layout_find(s->layout, id);
        int32_t x, y;
        if (!c) { session_error(s, line, text, "control not in the layout table"); return 1; }
        if (!es2_screen_control_centre(c, &x, &y)) { session_error(s, line, text, "control has no screen_rect"); return 1; }
        int down = n == 2 ? 2 : 0;
        /* the capture tour's click form (vshell_widgets.md 7.6 homing): three
         * moves in one pass */
        mouse(s, ES2_GUI_WM_MOUSEMOVE, ES2_GUI_WIDTH - 1, ES2_GUI_HEIGHT - 1);
        mouse(s, ES2_GUI_WM_MOUSEMOVE, 0, 0);
        mouse(s, ES2_GUI_WM_MOUSEMOVE, (int)x, (int)y);
        steps(s, 2);
        button(s, down);
        steps(s, 2);
        button(s, down + 1);
        steps(s, 1);
        return 1;
    }
    if (!strcmp(verb, "snap")) {
        char label[64];
        if (sscanf(args, "%63s", label) != 1 || strlen(label) >= sizeof s->snaps->label) return 0;
        snap(s, label);
        return 1;
    }
    if (parse_ints(args, v, 0)) {
        es2_screen_message m = {MSG_PAINT, 0, 0, 0, 0, 0, 0};
        if (!strcmp(verb, "paint")) { queue_message(s, m); return 1; }
        if (!strcmp(verb, "close")) { m.kind = MSG_CLOSE; queue_message(s, m); return 1; }
        if (!strcmp(verb, "quit")) { m.kind = MSG_QUIT; queue_message(s, m); return 1; }
        return 0;
    }
    if (parse_ints(args, v, 1)) {
        if (!strcmp(verb, "focus")) {
            es2_screen_message m = {MSG_FOCUS, v[0] != 0, 0, 0, 0, 0, 0};
            queue_message(s, m);
            return 1;
        }
        if (!strcmp(verb, "interactive")) { s->shell->input.interactive = v[0] != 0; return 1; }
        if (!strcmp(verb, "movie")) { s->shell->input.movie_open = v[0] != 0; return 1; }
    }
    return 0;
}

int es2_screen_run(es2_screen_session *s, const char *script) {
    int before = s->errors;
    const char *p = script ? script : "";
    unsigned number = 0;
    while (*p) {
        const char *end = strchr(p, '\n');
        size_t n = end ? (size_t)(end - p) : strlen(p);
        char line[256];
        number++;
        int too_long = n >= sizeof line;
        if (too_long) n = sizeof line - 1;
        memcpy(line, p, n);
        line[n] = 0;
        p = end ? end + 1 : p + strlen(p);
        char *hash = strchr(line, '#');
        if (hash) *hash = 0;
        char *t = line;
        while (isspace((unsigned char)*t)) t++;
        size_t len = strlen(t);
        while (len && isspace((unsigned char)t[len - 1])) t[--len] = 0;
        if (too_long) { session_error(s, number, t, "line too long"); continue; }
        if (!*t) continue;
        char verb[32];
        int used = 0;
        if (sscanf(t, "%31s%n", verb, &used) != 1) continue;
        if (s->shell->quit) { session_error(s, number, t, "the loop has ended"); continue; }
        if (!command(s, number, t, verb, t + used)) session_error(s, number, t, "not understood");
    }
    return s->errors - before;
}

const es2_screen_snap *es2_screen_find_snap(const es2_screen_session *s, const char *label) {
    for (size_t i = 0; i < s->snap_count; i++)
        if (!strcmp(s->snaps[i].label, label)) return &s->snaps[i];
    return NULL;
}

/* ===== suites and cases ======================================================== */

static es2_screen_suite *registry;

void es2_screen_register(es2_screen_suite *suite) {
    suite->next = registry;
    registry = suite;
}

size_t es2_screen_suites(const es2_screen_suite **out, size_t max) {
    size_t n = 0;
    for (es2_screen_suite *s = registry; s; s = s->next) {
        if (n < max) {
            size_t i = n;
            while (i > 0 && strcmp(out[i - 1]->name, s->name) > 0) { out[i] = out[i - 1]; i--; }
            out[i] = s;
        }
        n++;
    }
    return n;
}

/* Reads <session>/private/dumps/<run>/<hash>.idx (and .rgb for the table) when
 * the capture tour ran with -Dump. Returns 1 when the frame was found. */
static int read_dump(const es2_screen_references *refs, const es2_screen_reference *r, uint8_t *pixels,
                     uint8_t table[256][3], int *have_table) {
    char path[1024];
    char *text;
    size_t length;
    *have_table = 0;
    if (!refs->session[0]) return 0;
    snprintf(path, sizeof path, "%s/private/dumps/%s/%s.idx", refs->session, r->run, r->index_sha256);
    if (read_file(path, &text, &length)) return 0;
    int ok = length == ES2_GUI_PIXELS;
    if (ok) memcpy(pixels, text, ES2_GUI_PIXELS);
    free(text);
    if (ok && r->palette_sha256[0]) {
        snprintf(path, sizeof path, "%s/private/dumps/%s/%s.rgb", refs->session, r->run, r->palette_sha256);
        if (!read_file(path, &text, &length)) {
            if (length == 768) { memcpy(table, text, 768); *have_table = 1; }
            free(text);
        }
    }
    return ok;
}

/* On a reference mismatch the own frame goes to Bazel's undeclared outputs
 * (<case>.<snap>.idx and .rgb, the capture tour's dump form), outside the
 * repository, for a look next to the reference dump. */
static void write_actual(const es2_screen_case *c, const es2_screen_snap *snap) {
    const char *dir = getenv("TEST_UNDECLARED_OUTPUTS_DIR");
    if (!dir || !*dir) return;
    char path[1024];
    snprintf(path, sizeof path, "%s/%s.%s.idx", dir, c->name, snap->label);
    FILE *f = fopen(path, "wb");
    if (f) { fwrite(snap->pixels, 1, ES2_GUI_PIXELS, f); fclose(f); }
    snprintf(path, sizeof path, "%s/%s.%s.rgb", dir, c->name, snap->label);
    f = fopen(path, "wb");
    if (f) { fwrite(snap->rgb, 1, 768, f); fclose(f); }
}

static const es2_screen_known *known_table = es2_screen_known_differences;
static size_t known_count;
static int known_default = 1;

void es2_screen_known_use(const es2_screen_known *table, size_t count) {
    known_default = table == NULL;
    known_table = table ? table : es2_screen_known_differences;
    known_count = count;
}

static const char *known_class(const char *name, const char *snap, const char *key) {
    size_t n = known_default ? es2_screen_known_difference_count : known_count;
    for (size_t i = 0; i < n; i++)
        if (!strcmp(known_table[i].name, name) && !strcmp(known_table[i].snap, snap) &&
            !strcmp(known_table[i].reference, key))
            return known_table[i].cls;
    return NULL;
}

static int check_reference(const es2_screen_case *c, const es2_screen_layout *layout, const es2_screen_references *refs,
                           const es2_screen_snap *snap, const char *key) {
    const char *known = known_class(c->name, snap->label, key);
    const es2_screen_reference *r = es2_screen_references_find(refs, key);
    if (!r) {
        printf("SKIP %s %s: reference %s not among the %zu captured screens\n", c->name, snap->label, key,
               refs->count);
        return -1;
    }
    int index_same = !strcmp(r->index_sha256, snap->frame);
    int table_same = !r->palette_sha256[0] || !strcmp(r->palette_sha256, snap->table);
    if (index_same && table_same) {
        if (known) {
            printf("FAIL %s %s = reference %s: the known %s difference is gone (drop its screen_known.c row)\n", c->name, snap->label,
                   key, known);
            return 0;
        }
        printf("ok   %s %s = reference %s\n", c->name, snap->label, key);
        return 1;
    }
    es2_screen_diff d;
    memset(&d, 0, sizeof d);
    if (index_same) {
        d.cls = ES2_SCREEN_PALETTE;
        d.first_entry = d.last_entry = -1;
    } else {
        d.cls = ES2_SCREEN_UNKNOWN;
        d.table_differs = !table_same;
    }
    uint8_t *pixels = malloc(ES2_GUI_PIXELS);
    uint8_t table[256][3];
    int have_table = 0;
    if (pixels && read_dump(refs, r, pixels, table, &have_table)) {
        if (!have_table) memcpy(table, snap->rgb, sizeof table);
        es2_screen_compare(pixels, (const uint8_t(*)[3])table, snap->pixels, (const uint8_t(*)[3])snap->rgb, layout,
                           c->screen, &d);
        if (!have_table && !table_same) d.table_differs = 1;
    }
    free(pixels);
    char line[256];
    es2_screen_diff_format(&d, line, sizeof line);
    write_actual(c, snap);
    if (known && (d.cls == ES2_SCREEN_UNKNOWN || !strcmp(es2_screen_class_name(d.cls), known))) {
        printf("known %s %s != reference %s: %s\n", c->name, snap->label, key, line);
        return 1;
    }
    printf("FAIL %s %s != reference %s: %s%s%s\n", c->name, snap->label, key, line, known ? "; known: " : "",
           known ? known : "");
    return 0;
}

static int check_expectation(const es2_screen_case *c, const es2_screen_layout *layout, const es2_screen_snap *snap,
                             const es2_screen_expect *e) {
    int frame_ok = !e->frame || !strcmp(e->frame, snap->frame);
    int table_ok = !e->table || !strcmp(e->table, snap->table);
    if (frame_ok && table_ok) return 1;
    es2_screen_diff d;
    memset(&d, 0, sizeof d);
    d.cls = frame_ok ? ES2_SCREEN_PALETTE : ES2_SCREEN_UNKNOWN;
    d.first_entry = d.last_entry = -1;
    d.table_differs = !table_ok;
    uint8_t *pixels = malloc(ES2_GUI_PIXELS);
    uint8_t table[256][3];
    if (pixels && c->expected_picture && c->expected_picture(snap->label, pixels, table))
        es2_screen_compare(pixels, (const uint8_t(*)[3])table, snap->pixels, (const uint8_t(*)[3])snap->rgb, layout,
                           c->screen, &d);
    free(pixels);
    char line[256];
    es2_screen_diff_format(&d, line, sizeof line);
    printf("FAIL %s %s: expected frame %.12s table %.12s: %s\n", c->name, snap->label, e->frame ? e->frame : "-",
           e->table ? e->table : "-", line);
    return 0;
}

void es2_screen_run_case(const es2_screen_case *c, const es2_screen_layout *layout, const es2_screen_references *refs,
                         const char *data_root, es2_screen_totals *totals) {
    if (c->needs_data && (!data_root || !*data_root)) {
        printf("SKIP %s: ES2_DATA_ROOT not set\n", c->name);
        totals->skipped++;
        return;
    }
    es2_screen_session s;
    void *state = c->state_size ? calloc(1, c->state_size) : NULL;
    if ((c->state_size && !state) || es2_screen_session_open(&s, layout, NULL, NULL)) {
        printf("FAIL %s: out of memory\n", c->name);
        free(state);
        totals->failed++;
        return;
    }
    int failed = 0;
    int code = c->setup ? c->setup(&s, state, data_root) : 0;
    if (code) {
        printf("FAIL %s: setup returned %d\n", c->name, code);
        failed = 1;
    } else {
        es2_screen_run(&s, c->script);
        if (s.errors) {
            printf("FAIL %s: %d script error(s); first: %s\n", c->name, s.errors, s.error);
            failed = 1;
        }
        for (size_t i = 0; i < s.snap_count; i++)
            printf("snap %s %s pass=%u frame=%s table=%s screen=%s\n", c->name, s.snaps[i].label, s.snaps[i].pass,
                   s.snaps[i].frame, s.snaps[i].table, s.snaps[i].screen);
        printf("info %s passes=%u presents=%zu events=%zu\n", c->name, s.pass, s.present_count, s.event_count);
        for (size_t i = 0; i < c->expect_count; i++) {
            const es2_screen_expect *e = &c->expect[i];
            const es2_screen_snap *snap = es2_screen_find_snap(&s, e->snap);
            if (!snap) {
                printf("FAIL %s: no snap %s\n", c->name, e->snap);
                failed = 1;
                continue;
            }
            if (!check_expectation(c, layout, snap, e)) failed = 1;
            if (e->reference) {
                if (!refs) printf("SKIP %s %s: reference %s (no capture directory configured)\n", c->name, snap->label,
                                  e->reference);
                else if (check_reference(c, layout, refs, snap, e->reference) == 0) failed = 1;
            }
        }
    }
    if (failed) totals->failed++;
    else totals->passed++;
    printf("%s %s\n", failed ? "FAIL" : "PASS", c->name);
    es2_screen_session_close(&s);
    free(state);
}

/* ===== synthetic fixtures ====================================================== */

static uint32_t next_random(uint32_t *x) {
    *x = *x * 1103515245u + 12345u;
    return *x >> 16;
}

void es2_screen_fixture_image(uint8_t *pixels, int w, int h, unsigned seed) {
    uint32_t x = seed * 2654435761u + 7;
    for (int y = 0; y < h; y++)
        for (int i = 0; i < w; i++) {
            uint8_t v = (uint8_t)(16 + ((i / 4 + y / 4 + seed) % 64) + (next_random(&x) & 3));
            if ((i * 7 + y * 13 + (int)seed) % 97 == 0) v = 0; /* a few keyed pixels */
            pixels[y * w + i] = v;
        }
}

void es2_screen_fixture_palette(uint8_t records[256][4], unsigned seed) {
    uint32_t x = seed * 2246822519u + 3;
    for (int i = 0; i < 256; i++) {
        for (int c = 0; c < 3; c++) records[i][c] = (uint8_t)(next_random(&x) & 63);
        records[i][3] = 1;
    }
}

int es2_screen_fixture_text(uint8_t pixels[ES2_GUI_PIXELS], int x, int y, const char *text, uint8_t colour) {
    int start = x;
    for (const char *c = text; *c; c++, x += 6) {
        unsigned code = (unsigned char)*c;
        if (code == ' ') continue;
        uint32_t bits = code * 2654435761u ^ 0x5bd1e995u;
        for (int row = 0; row < 7; row++)
            for (int col = 0; col < 5; col++) {
                if (!((bits >> ((row * 5 + col) % 32)) & 1) && row != 6) continue;
                int px = x + col, py = y + row;
                if (px >= 0 && py >= 0 && px < ES2_GUI_WIDTH && py < ES2_GUI_HEIGHT)
                    pixels[py * ES2_GUI_WIDTH + px] = colour;
            }
    }
    return x - start;
}
