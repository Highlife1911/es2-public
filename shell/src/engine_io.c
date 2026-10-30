/* File layer of the own shell and the evidence log (engine_io.h).
 * Standard C plus a few Win32 calls (directories, time); no process starts. */
#include "engine_io.h"
#include "es2/shell/practice.h"
#include "es2/shell/vshell_store.h"
#include "es2/formats/sav.h"
#include "es2/formats/sha256.h"
#include "es2/platform/os.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const LAUNCH_FILES[4] = {"script.dat", "MISSION.STR", "mission.var", "player.mec"};

/* SHA-256 for the log's hashes: formats/sha256.h. */
static void sha_hex(const uint8_t *data, size_t size, char out[65]) { es2f_sha256_of(data, size, out); }

/* ---- files --------------------------------------------------------------------- */
uint8_t *eio_read(const char *path, size_t *size) {
    es2_os_stream *f = es2_os_fopen(path, "rb");
    uint8_t *data = NULL;
    long n;
    *size = 0;
    if (!f) return NULL;
    if (!es2_os_fseek(f, 0, SEEK_END) && (n = es2_os_ftell(f)) >= 0 && !es2_os_fseek(f, 0, SEEK_SET) && (data = malloc((size_t)n + 1))) {
        if (es2_os_fread(data, 1, (size_t)n, f) != (size_t)n) { free(data); data = NULL; }
        else { data[n] = 0; *size = (size_t)n; }
    }
    es2_os_fclose(f);
    return data;
}
int eio_write(const char *path, const uint8_t *data, size_t size) {
    es2_os_stream *f = es2_os_fopen(path, "wb");
    int ok = f && es2_os_fwrite(data, 1, size, f) == size;
    if (f && es2_os_fclose(f)) ok = 0;
    return ok;
}
int eio_write_over(const char *path, const uint8_t *data, size_t size) {
    size_t old_size = 0;
    uint8_t *old = eio_read(path, &old_size);
    int ok;
    if (!old || old_size <= size) { free(old); return eio_write(path, data, size); }
    memcpy(old, data, size);
    ok = eio_write(path, old, old_size);
    free(old);
    return ok;
}
static void join(char *out, size_t capacity, const char *a, const char *b) { snprintf(out, capacity, "%s\\%s", a, b); }

/* ---- the CD ------------------------------------------------------------------------ */
static int fail(eio *x, const char *what, const es2_error *e) {
    snprintf(x->error, sizeof x->error, "%s%s%s", what, e ? ": " : "", e ? es2_status_string(e->code) : "");
    return 0;
}
void eio_start(eio *x, uint16_t draws, int16_t mode) {
    x->draws = draws;
    x->engine.mode = mode;
    eio_open(x);   /* without a CD every operation fails with x->error */
}
int eio_open(eio *x) {
    static const char *names[3] = {"ZONES.VOL", "SHELL0.VOL", "LANG0.VOL"};
    char path[EIO_PATH];
    size_t size;
    es2_error e = {0};
    int16_t mode = x->engine.mode;
    if (x->opened) return 1;
    if (x->failed) return 0;
    x->failed = 1;
    if (!x->cd_root[0]) return fail(x, "menu.ini names no cd_root", NULL);
    /* VSHELL's tables come from the span store (vshell_store.h);
     * cd_root's VER95\VSHELL.EXE is not opened */
    if (es2_shell_engine_tables_read(ES2_VSHELL_STORE, &x->tables, &e) != ES2_OK)
        return fail(x, "the VSHELL tables of the span store could not be read", &e);
    for (int i = 0; i < 3; i++) {
        snprintf(path, sizeof path, "%s\\VOL\\%s", x->cd_root, names[i]);
        x->vols[i] = eio_read(path, &size);
        if (!x->vols[i] || es2_vol_open((es2_span){x->vols[i], size}, &x->vol[i], &e) != ES2_OK)
            return fail(x, "a CD archive could not be opened", x->vols[i] ? &e : NULL);
        x->sources[i].kind = ES2_VFS_VOL;
        x->sources[i].data.vol = &x->vol[i];
    }
    if (es2_vfs_open(x->sources, 3, &x->vfs, &e) != ES2_OK) return fail(x, "the CD archives", &e);
    if (es2_shell_hardpoints_read(&x->vfs, &x->tables, &x->hardpoints, &e) != ES2_OK)
        return fail(x, "the ARM tables", &e);
    x->engine.vfs = &x->vfs;
    x->engine.tables = &x->tables;
    x->engine.hardpoints = &x->hardpoints;
    snprintf(x->engine.language, sizeof x->engine.language, "%s", x->language);
    x->engine.lowest_free = x->lowest_free;
    if (es2_shell_engine_start(&x->engine, x->draws, &e) != ES2_OK) return fail(x, "the RNG start", &e);
    x->engine.mode = mode;
    x->opened = 1;
    x->failed = 0;
    return 1;
}

/* ---- a small JSON tree for chain.json ------------------------------------------- */
enum { J_NULL, J_BOOL, J_INT, J_STR, J_ARR, J_OBJ };
typedef struct jv {
    int t;
    long long n;
    char *s;
    size_t count, cap;
    char **keys;
    struct jv **v;
} jv;
static char *copy_string(const char *s) {
    size_t n = strlen(s) + 1;
    char *d = malloc(n);
    if (d) memcpy(d, s, n);
    return d;
}
static jv *jnew(int t) { jv *v = calloc(1, sizeof *v); if (v) v->t = t; return v; }
static jv *jint(long long n) { jv *v = jnew(J_INT); if (v) v->n = n; return v; }
static jv *jbool(int b) { jv *v = jnew(J_BOOL); if (v) v->n = !!b; return v; }
static jv *jstr(const char *s) {
    jv *v = jnew(J_STR);
    if (v && !(v->s = copy_string(s ? s : ""))) { free(v); v = NULL; }
    return v;
}
static void jfree(jv *v) {
    if (!v) return;
    for (size_t i = 0; i < v->count; i++) { if (v->keys) free(v->keys[i]); jfree(v->v[i]); }
    free(v->keys); free(v->v); free(v->s); free(v);
}
static jv *jget(const jv *o, const char *key) {
    if (!o || o->t != J_OBJ) return NULL;
    for (size_t i = 0; i < o->count; i++) if (!strcmp(o->keys[i], key)) return o->v[i];
    return NULL;
}
static int grow(jv *a) {
    if (a->count < a->cap) return 1;
    size_t cap = a->cap ? 2 * a->cap : 8;
    jv **v = realloc(a->v, cap * sizeof *v);
    if (!v) return 0;
    a->v = v;
    if (a->t == J_OBJ) {
        char **k = realloc(a->keys, cap * sizeof *k);
        if (!k) return 0;
        a->keys = k;
    }
    a->cap = cap;
    return 1;
}
static void jset(jv *o, const char *key, jv *value) {
    if (!o || o->t != J_OBJ || !value) { jfree(value); return; }
    for (size_t i = 0; i < o->count; i++)
        if (!strcmp(o->keys[i], key)) { jfree(o->v[i]); o->v[i] = value; return; }
    if (!grow(o) || !(o->keys[o->count] = copy_string(key))) { jfree(value); return; }
    o->v[o->count++] = value;
}
static void jpush(jv *a, jv *value) {
    if (!a || a->t != J_ARR || !value || !grow(a)) { jfree(value); return; }
    a->v[a->count++] = value;
}
static jv *jclone(const jv *v) {
    jv *c;
    if (!v) return NULL;
    if (v->t == J_STR) return jstr(v->s);
    if (!(c = jnew(v->t))) return NULL;
    c->n = v->n;
    for (size_t i = 0; i < v->count; i++) {
        if (v->t == J_OBJ) jset(c, v->keys[i], jclone(v->v[i]));
        else jpush(c, jclone(v->v[i]));
    }
    return c;
}
static long long jnum(const jv *o, const char *key, long long fallback) {
    jv *v = jget(o, key);
    return v && (v->t == J_INT || v->t == J_BOOL) ? v->n : fallback;
}
static const char *jtext(const jv *o, const char *key) {
    jv *v = jget(o, key);
    return v && v->t == J_STR ? v->s : NULL;
}
static int jnull(const jv *o, const char *key) {
    jv *v = jget(o, key);
    return !v || v->t == J_NULL || (v->t == J_BOOL && !v->n);
}
/* parser */
typedef struct jp { const char *p, *end; int ok; } jp;
static void ws(jp *j) { while (j->p < j->end && (*j->p == ' ' || *j->p == '\n' || *j->p == '\r' || *j->p == '\t')) j->p++; }
static char *jparse_string(jp *j) {
    size_t cap = 32, n = 0;
    char *s = malloc(cap);
    if (!s || j->p >= j->end || *j->p != '"') { free(s); j->ok = 0; return NULL; }
    j->p++;
    while (j->p < j->end && *j->p != '"') {
        unsigned c = (unsigned char)*j->p++;
        if (c == '\\' && j->p < j->end) {
            c = (unsigned char)*j->p++;
            if (c == 'n') c = '\n'; else if (c == 't') c = '\t'; else if (c == 'r') c = '\r';
            else if (c == 'b') c = '\b'; else if (c == 'f') c = '\f';
            else if (c == 'u' && j->end - j->p >= 4) {
                char hex[5] = {j->p[0], j->p[1], j->p[2], j->p[3], 0};
                c = (unsigned)strtoul(hex, NULL, 16);
                j->p += 4;
                if (c > 0xff) c = '?';   /* the log writes only bytes */
            }
        }
        if (n + 2 > cap) { char *t = realloc(s, cap *= 2); if (!t) { free(s); j->ok = 0; return NULL; } s = t; }
        s[n++] = (char)c;
    }
    if (j->p >= j->end) { free(s); j->ok = 0; return NULL; }
    j->p++;
    s[n] = 0;
    return s;
}
static jv *jparse_value(jp *j, int depth) {
    jv *v = NULL;
    ws(j);
    if (j->p >= j->end || depth > 64) { j->ok = 0; return NULL; }
    if (*j->p == '{' || *j->p == '[') {
        int object = *j->p == '{';
        v = jnew(object ? J_OBJ : J_ARR);
        j->p++;
        ws(j);
        if (j->p < j->end && *j->p == (object ? '}' : ']')) { j->p++; return v; }
        while (j->ok && v) {
            char *key = NULL;
            ws(j);
            if (object) {
                key = jparse_string(j);
                ws(j);
                if (!key || j->p >= j->end || *j->p != ':') { free(key); j->ok = 0; break; }
                j->p++;
            }
            jv *item = jparse_value(j, depth + 1);
            if (!j->ok) { free(key); jfree(item); break; }
            if (object) jset(v, key, item); else jpush(v, item);
            free(key);
            ws(j);
            if (j->p < j->end && *j->p == ',') { j->p++; continue; }
            if (j->p < j->end && *j->p == (object ? '}' : ']')) { j->p++; return v; }
            j->ok = 0;
        }
        jfree(v);
        return NULL;
    }
    if (*j->p == '"') {
        char *s = jparse_string(j);
        if (!s) return NULL;
        v = jnew(J_STR);
        if (v) v->s = s; else free(s);
        return v;
    }
    if (j->end - j->p >= 4 && !strncmp(j->p, "null", 4)) { j->p += 4; return jnew(J_NULL); }
    if (j->end - j->p >= 4 && !strncmp(j->p, "true", 4)) { j->p += 4; return jbool(1); }
    if (j->end - j->p >= 5 && !strncmp(j->p, "false", 5)) { j->p += 5; return jbool(0); }
    if (*j->p == '-' || (*j->p >= '0' && *j->p <= '9')) {
        char *end;
        long long n = strtoll(j->p, &end, 10);
        j->p = end;
        while (j->p < j->end && strchr("0123456789.eE+-", *j->p)) j->p++;   /* no fractions in the log */
        return jint(n);
    }
    j->ok = 0;
    return NULL;
}
static void jdump(es2_os_stream *f, const jv *v, int indent) {
    switch (v->t) {
    case J_NULL: es2_os_fputs("null", f); return;
    case J_BOOL: es2_os_fputs(v->n ? "true" : "false", f); return;
    case J_INT: es2_os_fprintf(f, "%lld", v->n); return;
    case J_STR:
        es2_os_fputc('"', f);
        for (const unsigned char *s = (const unsigned char *)v->s; *s; s++) {
            if (*s == '"' || *s == '\\') es2_os_fprintf(f, "\\%c", *s);
            else if (*s == '\n') es2_os_fputs("\\n", f);
            else if (*s < 0x20 || *s >= 0x7f) es2_os_fprintf(f, "\\u%04x", *s);
            else es2_os_fputc(*s, f);
        }
        es2_os_fputc('"', f);
        return;
    default:
        if (!v->count) { es2_os_fputs(v->t == J_OBJ ? "{}" : "[]", f); return; }
        es2_os_fputc(v->t == J_OBJ ? '{' : '[', f);
        for (size_t i = 0; i < v->count; i++) {
            es2_os_fprintf(f, "%s\n%*s", i ? "," : "", indent + 2, "");
            if (v->t == J_OBJ) {
                jv key = {J_STR, 0, v->keys[i], 0, 0, NULL, NULL};
                jdump(f, &key, 0);
                es2_os_fputs(": ", f);
            }
            jdump(f, v->v[i], indent + 2);
        }
        es2_os_fprintf(f, "\n%*s%c", indent, "", v->t == J_OBJ ? '}' : ']');
    }
}

/* ---- the evidence log ------------------------------------------------------------- */
static void utc(char out[40]) {
    es2_os_time t;
    es2_os_system_time(&t);
    snprintf(out, 40, "%04u-%02u-%02uT%02u:%02u:%02u.%03u000+00:00", t.year, t.month, t.day, t.hour, t.minute,
             t.second, t.milliseconds);
}
static jv *jnow(void) { char t[40]; utc(t); return jstr(t); }
static char *hex(const uint8_t *data, size_t size) {
    char *s = malloc(2 * size + 1);
    if (!s) return NULL;
    for (size_t i = 0; i < size; i++) snprintf(s + 2 * i, 3, "%02x", data[i]);
    s[2 * size] = 0;
    return s;
}
static jv *jhex(const uint8_t *data, size_t size) { char *s = hex(data, size); jv *v = jstr(s); free(s); return v; }
static void rng_bytes(const es2_shell_rng *r, uint8_t out[114]) {
    for (int i = 0; i < 56; i++) { out[2 * i] = (uint8_t)r->words[i]; out[2 * i + 1] = (uint8_t)(r->words[i] >> 8); }
    out[112] = r->a;
    out[113] = r->b;
}
static jv *jrng(const es2_shell_rng *r) { uint8_t b[114]; rng_bytes(r, b); return jhex(b, 114); }
static jv *jselectors(const int16_t s[30]) {
    uint8_t b[60];
    for (int i = 0; i < 30; i++) { b[2 * i] = (uint8_t)s[i]; b[2 * i + 1] = (uint8_t)((uint16_t)s[i] >> 8); }
    return jhex(b, 60);
}
static jv *jfile(const uint8_t *data, size_t size) {
    char h[65];
    jv *o = jnew(J_OBJ);
    sha_hex(data, size, h);
    jset(o, "size", jint((long long)size));
    jset(o, "sha256", jstr(h));
    return o;
}
static jv *jfile_state(const char *path) {
    size_t size;
    uint8_t *data = eio_read(path, &size);
    int64_t mtime;
    jv *o;
    if (!data) return jnew(J_NULL);
    o = jfile(data, size);
    free(data);
    if (es2_os_file_mtime(path, &mtime)) jset(o, "mtime_ns", jint((long long)mtime * 1000000000LL));
    return o;
}
static int log_on(const eio *x) { return x->log_dir[0] != 0; }
static void log_line(eio *x, const char *format, ...) {
    char path[EIO_PATH], t[40];
    es2_os_stream *f;
    va_list args;
    if (!x->chain) return;
    join(path, sizeof path, x->campaign, "chain.log");
    if (!(f = es2_os_fopen(path, "ab"))) return;
    utc(t);
    es2_os_fprintf(f, "%s ", t);
    va_start(args, format);
    es2_os_vfprintf(f, format, args);
    va_end(args);
    es2_os_fputc('\n', f);
    es2_os_fclose(f);
}
static void log_save(eio *x) {
    char path[EIO_PATH], temporary[EIO_PATH];
    es2_os_stream *f;
    if (!x->chain) return;
    join(path, sizeof path, x->campaign, "chain.json");
    join(temporary, sizeof temporary, x->campaign, "chain.tmp");
    if (!(f = es2_os_fopen(temporary, "wb"))) return;
    jdump(f, (jv *)x->chain, 0);
    es2_os_fputc('\n', f);
    if (!es2_os_fclose(f)) es2_os_move_file_replace(temporary, path);
}
static void log_drop(eio *x) {
    jfree((jv *)x->chain);
    x->chain = NULL;
    x->campaign[0] = 0;
}
/* The campaign `current` names, parsed (NULL without one). */
static jv *log_current(eio *x) {
    char path[EIO_PATH], name[EIO_PATH];
    size_t size, n;
    uint8_t *text;
    if (x->chain) return (jv *)x->chain;
    if (!log_on(x)) return NULL;
    join(path, sizeof path, x->log_dir, "current");
    if (!(text = eio_read(path, &size))) return NULL;
    for (n = 0; n < size && n + 1 < sizeof name && text[n] != '\r' && text[n] != '\n'; n++) name[n] = (char)text[n];
    name[n] = 0;
    free(text);
    if (!n) return NULL;
    join(x->campaign, sizeof x->campaign, x->log_dir, name);
    join(path, sizeof path, x->campaign, "chain.json");
    if (!(text = eio_read(path, &size))) { x->campaign[0] = 0; return NULL; }
    jp j = {(const char *)text, (const char *)text + size, 1};
    jv *v = jparse_value(&j, 0);
    free(text);
    if (!j.ok || !v || jnum(v, "schema", 0) != 1) { jfree(v); x->campaign[0] = 0; return NULL; }
    x->chain = v;
    return v;
}
static int write_in(eio *x, const char *name, const uint8_t *data, size_t size) {
    char path[EIO_PATH];
    join(path, sizeof path, x->campaign, name);
    return eio_write(path, data, size);
}
static jv *pending(eio *x) {
    jv *c = log_current(x);
    jv *p = jget(c, "pending");
    return c && p && p->t == J_OBJ ? p : NULL;
}
static jv *mission_record(eio *x, long long index) {
    jv *m = jget((jv *)x->chain, "missions");
    return m && m->t == J_ARR && index >= 0 && (size_t)index < m->count ? m->v[index] : NULL;
}
static void set_save(eio *x, const char *name) { jset((jv *)x->chain, "save", jstr(name)); }
/* A new campaign record campaign-NNN for `sav` (es2chain init). */
static int log_create(eio *x, const uint8_t *sav, size_t size, const es2_shell_rng *rng, jv *source) {
    char path[EIO_PATH], name[32];
    int16_t group = 0, position = 0;
    es2_sav s;
    es2_error e;
    jv *c, *initial, *file;
    char h[65];
    log_drop(x);
    if (!log_on(x)) { jfree(source); return 0; }
    es2_os_create_directory(x->log_dir);
    for (int i = 0; i < 1000; i++) {
        snprintf(name, sizeof name, "campaign-%03d", i);
        join(path, sizeof path, x->log_dir, name);
        if (es2_os_file_attributes(path) == ES2_OS_INVALID_FILE_ATTRIBUTES) break;
    }
    if (!es2_os_create_directory(path)) { jfree(source); return 0; }
    snprintf(x->campaign, sizeof x->campaign, "%s", path);
    if (es2_sav_parse((es2_span){sav, size}, &s, &e) == ES2_OK) {
        group = (int16_t)(s.selection.data[0] | s.selection.data[1] << 8);
        position = (int16_t)(s.selection.data[2] | s.selection.data[3] << 8);
    }
    write_in(x, "save-000.sav", sav, size);
    c = jnew(J_OBJ);
    jset(c, "schema", jint(1));
    jset(c, "created_utc", jnow());
    jset(c, "core", jstr(x->core));
    jset(c, "cd_root", jstr(x->cd_root));
    jset(c, "game_dir", jstr(x->game));
    jset(c, "language", jstr(x->language));
    jset(c, "mission_limit", jint(0));
    jset(c, "repair_option", jint(0));
    jset(c, "restock_option", jint(0));
    jset(c, "keep_data", jbool(1));
    jset(c, "hangar", jstr(x->lowest_free ? "lowest-free" : "stop"));
    jset(c, "results_on_exit_1", jbool(0));
    initial = jnew(J_OBJ);
    file = jfile(sav, size);
    jset(file, "file", jstr("save-000.sav"));
    jset(initial, "save", file);
    file = jnew(J_ARR);
    jpush(file, jint(group));
    jpush(file, jint(position));
    jset(initial, "position", file);
    jset(initial, "seed", jint(x->draws));
    {
        uint8_t b[114];
        rng_bytes(rng, b);
        sha_hex(b, 114, h);
        jset(initial, "rng_sha256", jstr(h));
    }
    jset(c, "initial", initial);
    jset(c, "save", jstr("save-000.sav"));
    jset(c, "rng", jrng(rng));
    jset(c, "selectors", jselectors(x->engine.selectors));
    jset(c, "completed", jint(0));
    jset(c, "pending", jnew(J_NULL));
    jset(c, "finished", jnew(J_NULL));
    jset(c, "data_backup", jnew(J_NULL));
    jset(c, "data_restored", jnew(J_NULL));
    jset(c, "missions", jnew(J_ARR));
    jset(c, "starts", jnew(J_ARR));
    {
        jv *menu = jnew(J_OBJ);
        jset(menu, "source", source);
        jset(menu, "created_utc", jnow());
        jset(c, "menu", menu);
    }
    x->chain = c;
    join(path, sizeof path, x->log_dir, "current");
    snprintf(name, sizeof name, "%s\n", strrchr(x->campaign, '\\') + 1);
    eio_write(path, (const uint8_t *)name, strlen(name));
    log_line(x, "menu: campaign created from %s", jtext(source, "kind"));
    log_save(x);
    return 1;
}
static void log_finish(eio *x, const char *reason, const char *detail) {
    jv *f = jnew(J_OBJ);
    jset(f, "reason", jstr(reason));
    jset(f, "utc", jnow());
    jset(f, "detail", detail ? jstr(detail) : jnew(J_NULL));
    jset(f, "restored", jnew(J_ARR));
    jset((jv *)x->chain, "finished", f);
    jset((jv *)x->chain, "pending", jnew(J_NULL));
    log_line(x, "finished %s%s%s restored=", reason, detail ? ": " : "", detail ? detail : "");
}
/* The mission record of a preparation (es2chain prepare_mission and the
 * menu's prepared_state). */
static void log_prepared(eio *x, const es2_shell_engine_prepared *p) {
    jv *c = (jv *)x->chain, *missions = jget(c, "missions"), *record, *files;
    char name[48], input[48];
    long long index;
    if (!c || !missions) return;
    index = (long long)missions->count;
    snprintf(input, sizeof input, "%s", jtext(c, "save") ? jtext(c, "save") : "save-000.sav");
    record = jnew(J_OBJ);
    jset(record, "index", jint(index));
    jset(record, "group", jint(p->group));
    jset(record, "position", jint(p->position));
    jset(record, "mission", jstr(p->mission));
    if (p->hangar_to >= 0) {
        jv *h = jnew(J_OBJ);
        snprintf(name, sizeof name, "save-%03lld-hangar.sav", index);
        write_in(x, name, p->input.data, p->input.size);
        jset(h, "player_bay", jint(p->hangar_from));
        jset(h, "chosen", jint(p->hangar_to));
        jset(h, "save", jstr(name));
        snprintf(input, sizeof input, "%s", name);
        log_line(x, "hangar policy: player bay %d -> %d", p->hangar_from, p->hangar_to);
        jset(record, "hangar", h);
    } else
        jset(record, "hangar", jnew(J_NULL));
    jset(record, "input_save", jstr(input));
    jset(record, "prepared_utc", jnow());
    jset(record, "input_rng", jrng(&p->rng_in));
    jset(record, "input_selectors", jselectors(p->selectors_in));
    snprintf(name, sizeof name, "mission-%03lld", index);
    {
        char path[EIO_PATH];
        join(path, sizeof path, x->campaign, name);
        es2_os_create_directory(path);
    }
    files = jnew(J_OBJ);
    {
        const es2_shell_buffer *b[4] = {&p->files.script, &p->files.strings, &p->files.variables, &p->files.player};
        for (int i = 0; i < 4; i++) {
            char file[64];
            snprintf(file, sizeof file, "%s\\%s", name, LAUNCH_FILES[i]);
            write_in(x, file, b[i]->data, b[i]->size);
            jset(files, LAUNCH_FILES[i], jfile(b[i]->data, b[i]->size));
        }
    }
    jset(record, "files", files);
    jset(record, "output_rng", jrng(&x->engine.rng));
    jset(record, "output_selectors", jselectors(x->engine.selectors));
    jset(c, "rng", jrng(&x->engine.rng));
    jset(c, "selectors", jselectors(x->engine.selectors));
    {
        char path[EIO_PATH];
        snprintf(path, sizeof path, "DATA\\results.dat");
        jset(record, "results_before", jfile_state(path));
    }
    log_line(x, "prepared mission %lld %s (%d,%d) data=1", index, p->mission, p->group, p->position);
    snprintf(name, sizeof name, "save-%03lld-prepared.sav", index);
    write_in(x, name, x->engine.memory.data, x->engine.memory.size);
    jset(record, "prepared_save", jstr(name));
    jset(record, "launched", jnew(J_NULL));
    set_save(x, name);
    log_line(x, "menu: SAV after the preparation of mission %lld: %s", index, name);
    jset(c, "pending", jclone(record));
    jpush(missions, record);
}

/* ---- operations --------------------------------------------------------------------- */
static int write_files(eio *x, const es2_shell_output *o) {
    (void)x;
    return eio_write_over("DATA\\script.dat", o->script.data, o->script.size) &&
           eio_write_over("DATA\\MISSION.STR", o->strings.data, o->strings.size) &&
           eio_write("DATA\\mission.var", o->variables.data, o->variables.size) &&
           eio_write("DATA\\player.mec", o->player.data, o->player.size);
}
/* The preparation of the memory state's CAREER mission into DATA (logged
 * into the current record). */
static int prepare_as(eio *x, const char *mission) {
    es2_shell_engine_prepared p;
    es2_error e = {0};
    memset(&p, 0, sizeof p);
    if ((mission ? es2_shell_engine_prepare_mission(&x->engine, mission, &p, &e)
                 : es2_shell_engine_prepare(&x->engine, &p, &e)) != ES2_OK) {
        fail(x, e.code == ES2_ERR_UNSUPPORTED ? "the player has no Herc; the VSHELL hangar needs a choice"
                                              : "the preparation failed", &e);
        if (x->chain) { log_finish(x, e.code == ES2_ERR_UNSUPPORTED ? "stop-player-without-herc"
                                                                      : "stop-preparation-rejected", x->error);
                        log_save(x); }
        return 0;
    }
    if (!write_files(x, &p.files)) {
        es2_shell_engine_prepared_free(&p);
        return fail(x, "DATA could not be written", NULL);
    }
    if (x->chain) { log_prepared(x, &p); log_save(x); }
    es2_shell_engine_prepared_free(&p);
    return 1;
}
static int prepare(eio *x) { return prepare_as(x, NULL); }
int eio_new_game(eio *x, const char *name, uint16_t rank, uint32_t *salvage, int defer) {
    es2_error e = {0};
    uint32_t total = 0;
    if (!eio_open(x)) return 0;
    if (es2_shell_engine_new_game(&x->engine, name, rank, &total, &e) != ES2_OK) return fail(x, "the new career failed", &e);
    if (salvage) *salvage = total;
    if (log_on(x)) {
        jv *source = jnew(J_OBJ);
        jset(source, "kind", jstr("new-game"));
        jset(source, "name", jstr(name));
        jset(source, "skill", jint(rank));
        jset(source, "seed", jint(x->draws));
        jset(source, "salvage", jint(total));
        log_create(x, x->engine.memory.data, x->engine.memory.size, &x->engine.rng, source);
    }
    return defer ? 1 : prepare(x);
}
static size_t writer_end(const uint8_t *sav, size_t size) {
    es2_sav s;
    es2_error e;
    return es2_sav_parse((es2_span){sav, size}, &s, &e) == ES2_OK ? s.reader_end : 0;
}
int eio_loaded(eio *x, const char *slot_path, const uint8_t *file, size_t size, int companions) {
    jv *c;
    char h[65];
    if (!eio_open(x)) return 0;
    if (x->engine.final_word != 2) return 1;
    if (!companions) {
        /* Deviation: a slot without its launch-file copies */
        if (log_on(x)) {
            jv *source = jnew(J_OBJ);
            jset(source, "kind", jstr("slot"));
            jset(source, "file", jstr(slot_path));
            sha_hex(file, size, h);
            jset(source, "sha256", jstr(h));
            jset(source, "seed", jint(x->draws));
            sha_hex(x->engine.memory.data, x->engine.memory.size, h);
            jset(source, "loaded_sha256", jstr(h));
            log_create(x, x->engine.memory.data, x->engine.memory.size, &x->engine.rng, source);
        }
        return prepare(x);
    }
    if (!log_on(x)) return 1;
    c = log_current(x);
    if (c && jnull(c, "finished") && pending(x)) {
        char path[EIO_PATH];
        size_t mine_size = 0, a, b;
        uint8_t *mine;
        join(path, sizeof path, x->campaign, jtext(c, "save") ? jtext(c, "save") : "-");
        mine = eio_read(path, &mine_size);
        a = mine ? writer_end(mine, mine_size) : 0;
        b = writer_end(file, size);
        if (a && a == b && !memcmp(mine, file, a)) {
            long long index = jnum(pending(x), "index", 0);
            char name[64];
            int k = 0;
            for (;; k++) {
                snprintf(name, sizeof name, "save-%03lld-loaded-%d.sav", index, k);
                join(path, sizeof path, x->campaign, name);
                if (es2_os_file_attributes(path) == ES2_OS_INVALID_FILE_ATTRIBUTES) break;
            }
            write_in(x, name, x->engine.memory.data, x->engine.memory.size);
            set_save(x, name);
            log_line(x, "menu: slot %s loaded again: %s", slot_path, name);
            log_save(x);
            free(mine);
            return 1;
        }
        free(mine);
    }
    /* Another career with its copies: a record whose mission is the slot's. */
    {
        jv *source = jnew(J_OBJ), *record, *files;
        es2_shell_buffer mec = {0}, var = {0};
        es2_error e;
        char mission[40] = "-", dir[EIO_PATH];
        jset(source, "kind", jstr("slot"));
        jset(source, "file", jstr(slot_path));
        sha_hex(file, size, h);
        jset(source, "sha256", jstr(h));
        jset(source, "seed", jint(x->draws));
        sha_hex(x->engine.memory.data, x->engine.memory.size, h);
        jset(source, "loaded_sha256", jstr(h));
        jset(source, "companions", jbool(1));
        if (!log_create(x, x->engine.memory.data, x->engine.memory.size, &x->engine.rng, source)) return 1;
        c = (jv *)x->chain;
        eio_mission_name(x, mission, sizeof mission);
        record = jnew(J_OBJ);
        jset(record, "index", jint(0));
        jset(record, "group", jint(jget(jget(c, "initial"), "position")->v[0]->n));
        jset(record, "position", jint(jget(jget(c, "initial"), "position")->v[1]->n));
        jset(record, "mission", jstr(mission));
        jset(record, "from_slot", jbool(1));
        jset(record, "input_save", jstr("save-000.sav"));
        jset(record, "hangar", jnew(J_NULL));
        join(dir, sizeof dir, x->campaign, "mission-000");
        es2_os_create_directory(dir);
        files = jnew(J_OBJ);
        es2_shell_launch_files((es2_span){x->engine.memory.data, x->engine.memory.size}, &mec, &var, &e);
        for (int i = 0; i < 4; i++) {
            char path[EIO_PATH], name[64];
            size_t n = 0;
            uint8_t *data;
            snprintf(path, sizeof path, "DATA\\%s", LAUNCH_FILES[i]);
            data = i == 2 ? (var.data ? var.data : NULL) : eio_read(path, &n);
            if (i == 2) n = var.size;
            if (!data) continue;
            snprintf(name, sizeof name, "mission-000\\%s", LAUNCH_FILES[i]);
            write_in(x, name, data, n);
            jset(files, LAUNCH_FILES[i], jfile(data, n));
            if (i != 2) free(data);
        }
        free(mec.data);
        free(var.data);
        jset(record, "files", files);
        jset(record, "launched", jnew(J_NULL));
        jset(c, "pending", jclone(record));
        jpush(jget(c, "missions"), record);
        log_line(x, "menu: mission of slot %s with its copies", slot_path);
        log_save(x);
    }
    return 1;
}
int eio_launch(eio *x, int *warning) {
    es2_shell_launch_check_result r;
    es2_shell_buffer mec = {0}, var = {0};
    es2_error e = {0};
    jv *p;
    *warning = ES2_SHELL_LAUNCH_OK;
    if (!eio_open(x)) return 0;
    if (es2_shell_engine_launch(&x->engine, &r, &mec, &var, &e) != ES2_OK)
        return fail(x, "the launch checks refused the state", &e);
    *warning = r.warning;
    if (r.warning != ES2_SHELL_LAUNCH_OK) return 0;
    /* 0x40f68c */
    if (!eio_write("DATA\\mission.var", var.data, var.size) || !eio_write("DATA\\player.mec", mec.data, mec.size)) {
        free(mec.data); free(var.data);
        return fail(x, "DATA could not be written", NULL);
    }
    if (log_on(x) && (p = pending(x)) && jnull((jv *)x->chain, "finished")) {
        long long index = jnum(p, "index", 0);
        jv *record = mission_record(x, index), *steps = jget(record, "bay");
        int applied = 0, from_slot = record && !jnull(record, "from_slot");
        char t[40];
        for (size_t i = 0; steps && i < steps->count; i++) applied |= (int)jnum(steps->v[i], "applied", 0);
        /* The files DBSIM is handed: after service-bay changes, and always for
         * a slot's own copies, whose record holds the slot's copy of
         * player.mec, not the one 0x40f68c writes at the launch. */
        if ((applied || from_slot) && record) {
            char dir[EIO_PATH], name[96];
            jv *files = jnew(J_OBJ);
            snprintf(name, sizeof name, "mission-%03lld\\launch", index);
            join(dir, sizeof dir, x->campaign, name);
            es2_os_create_directory(dir);
            for (int i = 0; i < 4; i++) {
                char path[EIO_PATH];
                size_t n = 0;
                uint8_t *data;
                snprintf(name, sizeof name, "mission-%03lld\\%s", index, LAUNCH_FILES[i]);
                join(path, sizeof path, x->campaign, name);
                data = i == 2 ? var.data : i == 3 ? mec.data : eio_read(path, &n);
                if (i >= 2) n = i == 2 ? var.size : mec.size;
                if (!data) continue;
                snprintf(name, sizeof name, "mission-%03lld\\launch\\%s", index, LAUNCH_FILES[i]);
                write_in(x, name, data, n);
                jset(files, LAUNCH_FILES[i], jfile(data, n));
                if (i < 2) free(data);
            }
            jset(record, "launch_files", files);
            jset(record, "launch_save", jstr(jtext((jv *)x->chain, "save")));
            log_line(x, applied ? "menu: launch files after the service bay" : "menu: launch files of the slot's copies");
        }
        jset(p, "results_before", jfile_state("DATA\\results.dat"));
        utc(t);
        jset(p, "launched", jstr(t));
        if (record) jset(record, "launched", jstr(t));
        log_line(x, "menu: launch mission %lld %s", index, jtext(p, "mission") ? jtext(p, "mission") : "-");
        log_save(x);
    }
    free(mec.data);
    free(var.data);
    return 1;
}
int eio_abort(eio *x) {
    jv *p = log_on(x) ? pending(x) : NULL, *start;
    if (!p || jnull(p, "launched")) return 0;
    start = jnew(J_OBJ);
    jset(start, "utc", jnow());
    jset(start, "state", jint(x->state));
    jset(start, "flags", jnew(J_ARR));
    jset(start, "action", jstr("abort"));
    jset(start, "seed", jint(x->draws));
    jpush(jget((jv *)x->chain, "starts"), start);
    jset(p, "launched", jnew(J_NULL));
    {
        jv *record = mission_record(x, jnum(p, "index", -1));
        if (record) jset(record, "launched", jnew(J_NULL));
    }
    log_line(x, "menu: mission %lld not played (shell state %d); nothing read or written", jnum(p, "index", -1), x->state);
    log_save(x);
    return 1;
}
static const char *ending_name(int ending) {
    return ending == ES2_SHELL_RESULTS_PLAYER_LOST ? "player-lost" : ending == ES2_SHELL_RESULTS_OTHER_MODE ? "other-mode"
         : ending == ES2_SHELL_RESULTS_ADVANCED ? "advanced" : NULL;
}
/* es2chain continuation() with no mission limit. */
static void continuation(const es2_shell_results_output *o, char *out, size_t capacity) {
    if (o->ending == ES2_SHELL_RESULTS_PLAYER_LOST) snprintf(out, capacity, "stop-player-lost");
    else if (o->ending == ES2_SHELL_RESULTS_OTHER_MODE) snprintf(out, capacity, "stop-other-mode");
    else if (o->advance != 2) snprintf(out, capacity, "stop-return-%d", o->advance);
    else snprintf(out, capacity, "continue");
}
int eio_results(eio *x, const uint8_t *slot, size_t slot_size, uint8_t repair, uint8_t restock,
                es2_shell_results_output *out, uint8_t **results_out, size_t *results_size) {
    size_t var_size = 0, res_size = 0;
    uint8_t *var = eio_read("DATA\\mission.var", &var_size), *res = eio_read("DATA\\results.dat", &res_size);
    es2_shell_rng start;
    es2_error e = {0};
    es2_status status;
    jv *p = NULL, *played = NULL;
    char path[EIO_PATH], name[64], reason[32];
    *results_out = NULL;
    *results_size = 0;
    if (!eio_open(x)) { free(var); free(res); return 0; }
    start = x->engine.rng;
    if (log_on(x) && (p = pending(x)) && !jnull(p, "launched")) played = mission_record(x, jnum(p, "index", -1));
    if (played) {
        jv *s = jnew(J_OBJ), *prefs = jnew(J_OBJ);
        size_t mine_size = 0, end = writer_end(slot, slot_size);
        uint8_t *mine;
        jset(s, "utc", jnow());
        jset(s, "state", jint(x->state));
        jset(s, "flags", jnew(J_ARR));
        jset(s, "action", jstr("advance"));
        jset(s, "seed", jint(x->draws));
        jset(prefs, "mode", jint(x->engine.mode));
        jset(prefs, "repair", jint(repair));
        jset(prefs, "restock", jint(restock));
        jset(s, "prefs", prefs);
        jpush(jget((jv *)x->chain, "starts"), s);
        jset((jv *)x->chain, "repair_option", jint(repair));
        jset((jv *)x->chain, "restock_option", jint(restock));
        /* The results path starts from slot 10 as the exit for DBSIM
         * wrote it; the record names the SAV file holding that image. */
        join(path, sizeof path, x->campaign, jtext((jv *)x->chain, "save") ? jtext((jv *)x->chain, "save") : "-");
        mine = eio_read(path, &mine_size);
        if (mine && end && mine_size == end && !memcmp(mine, slot, end))
            snprintf(name, sizeof name, "%s", jtext((jv *)x->chain, "save"));
        else {
            snprintf(name, sizeof name, "save-%03lld-results.sav", jnum(played, "index", 0));
            write_in(x, name, slot, end);
        }
        free(mine);
        jset(played, "results_save", jstr(name));
        jset(played, "results_rng", jrng(&start));
        jset(played, "seed", jint(x->draws));
    }
    if (!var || !res) {
        free(var); free(res);
        if (played) { log_finish(x, "stop-results-rejected", "DATA\\results.dat or mission.var is missing"); log_save(x); }
        return fail(x, "DATA\\results.dat or DATA\\mission.var is missing", NULL);
    }
    status = es2_shell_engine_results(&x->engine, (es2_span){slot, slot_size}, (es2_span){var, var_size},
                                      (es2_span){res, res_size}, repair, restock, out, &e);
    if (status == ES2_OK && out->debrief && out->strings.data &&
        !eio_write_over("DATA\\MISSION.STR", out->strings.data, out->strings.size))   /* 0x418014 in the debrief path */
        fail(x, "DATA\\MISSION.STR could not be written", NULL);
    if (played) {
        long long index = jnum(played, "index", 0);
        jv *summary = jnew(J_OBJ), *dbsim = jnew(J_OBJ), *c = (jv *)x->chain;
        snprintf(name, sizeof name, "mission-%03lld\\dbsim", index);
        join(path, sizeof path, x->campaign, name);
        es2_os_create_directory(path);
        snprintf(name, sizeof name, "mission-%03lld\\dbsim\\results.dat", index);
        write_in(x, name, res, res_size);
        snprintf(name, sizeof name, "mission-%03lld\\dbsim\\mission.var", index);
        write_in(x, name, var, var_size);
        jset(played, "dbsim_state", jint(x->state));
        jset(dbsim, "results", jfile(res, res_size));
        jset(dbsim, "mission_var", jfile(var, var_size));
        if (status != ES2_OK) {
            jset(summary, "success", jbool(0));
            jset(summary, "error", jstr(es2_status_string(e.code)));
        } else {
            jv *next = jnew(J_OBJ), *save;
            const char *ending = ending_name(out->ending);
            char mission[40];
            int16_t id;
            jset(summary, "success", jbool(1));
            jset(summary, "ending", ending ? jstr(ending) : jint(out->ending));
            jset(summary, "advance", jint(out->advance));
            jset(summary, "final_word", jint(out->final_word));
            jset(summary, "total", jint(out->total));
            jset(summary, "lost_pilots", jint(out->lost));
            jset(next, "group", jint(out->group));
            jset(next, "position", jint(out->position));
            jset(next, "mission", es2_shell_campaign_mission(&x->vfs, out->group, out->position, x->language, mission,
                                                             sizeof mission, &id, &e) == ES2_OK ? jstr(mission) : jnew(J_NULL));
            jset(summary, "next", next);
            snprintf(name, sizeof name, "save-%03lld.sav", index + 1);
            write_in(x, name, out->save.data, out->save.size);
            save = jfile(out->save.data, out->save.size);
            jset(save, "file", jstr(name));
            jset(summary, "save", save);
            jset(summary, "rng_state", jrng(&out->rng));
            if (out->debrief) {
                jv *d = jnew(J_OBJ), *refs = jnew(J_ARR);
                for (int i = 0; i < 30; i++) jpush(refs, jint(out->selected.refs[i]));
                jset(d, "references", refs);
                jset(d, "count", jint(out->selected.count));
                jset(d, "auxiliary", jint(out->selected.aux));
                jset(d, "passed_records", jint((long long)out->selected.passed));
                jset(d, "mission_str", jfile(out->strings.data, out->strings.size));
                jset(d, "text", jfile(out->text.data, out->text.size));
                jset(summary, "debrief", d);
            }
            set_save(x, name);
            jset(c, "rng", jrng(&out->rng));
        }
        jset(summary, "dbsim", dbsim);
        jset(played, "advance", summary);
        jset(c, "pending", jnew(J_NULL));
        jset(c, "completed", jint(jnum(c, "completed", 0) + 1));
        if (status == ES2_OK && out->text.size) {
            snprintf(name, sizeof name, "mission-%03lld\\debrief.txt", index);
            write_in(x, name, out->text.data, out->text.size);
        }
        if (status != ES2_OK) log_finish(x, "stop-results-rejected", es2_status_string(e.code));
        else {
            continuation(out, reason, sizeof reason);
            log_line(x, "advanced: ending %d advance %d next (%d,%d)", out->ending, out->advance, out->group, out->position);
            if (strcmp(reason, "continue")) log_finish(x, reason, NULL);
        }
        log_save(x);
    }
    free(var);
    if (status != ES2_OK) { free(res); return fail(x, "results rejected", &e); }
    *results_out = res;
    *results_size = res_size;
    return 1;
}
int eio_prepare_next(eio *x) {
    if (!eio_open(x)) return 0;
    return prepare(x);
}
int eio_prepare_mission(eio *x, const char *mission) {
    if (!eio_open(x)) return 0;
    return prepare_as(x, mission);
}
int eio_bay(eio *x, es2_shell_bay_op *op) {
    es2_error e = {0};
    jv *p, *record, *steps, *step, *list;
    char before[64], name[64];
    if (!eio_open(x)) return 0;
    if (es2_shell_engine_bay(&x->engine, op, &e) != ES2_OK) return fail(x, "the service-bay operation failed", &e);
    if (!log_on(x) || !(p = pending(x)) || !(record = mission_record(x, jnum(p, "index", -1)))) return 1;
    if (!(steps = jget(record, "bay"))) { jset(record, "bay", jnew(J_ARR)); steps = jget(record, "bay"); }
    snprintf(before, sizeof before, "%s", jtext((jv *)x->chain, "save") ? jtext((jv *)x->chain, "save") : "-");
    snprintf(name, sizeof name, "save-%03lld-bay-%zu.sav", jnum(p, "index", 0), steps->count);
    write_in(x, name, x->engine.memory.data, x->engine.memory.size);
    step = jnew(J_OBJ);
    list = jnew(J_ARR);
    jpush(list, jint(op->kind));
    jpush(list, jint(op->bay));
    jpush(list, jint(op->slot));
    jpush(list, jint(op->value));
    jpush(list, jint(op->admitted));
    jset(step, "op", list);
    jset(step, "applied", jint(op->applied));
    jset(step, "input", jstr(before));
    jset(step, "output", jstr(name));
    jset(step, "utc", jnow());
    jpush(steps, step);
    jset(p, "bay", jclone(steps));
    set_save(x, name);
    log_line(x, "menu: service bay (%d, %d, %d, %d, %d) applied=%d: %s", op->kind, op->bay, op->slot, op->value,
             op->admitted, op->applied, name);
    log_save(x);
    return 1;
}
int eio_replay(eio *x) {
    jv *c = log_on(x) ? log_current(x) : NULL, *finished, *missions, *played, *record, *files;
    const char *reason;
    char name[64], dir[EIO_PATH];
    long long index;
    if (!c) return 1;
    finished = jget(c, "finished");
    reason = jtext(finished, "reason");
    missions = jget(c, "missions");
    if (!reason || (strcmp(reason, "stop-player-lost") && strcmp(reason, "stop-return-0")) || !missions ||
        !missions->count || !jget(played = missions->v[missions->count - 1], "advance"))
        return 1;
    index = (long long)missions->count;
    snprintf(name, sizeof name, "save-%03lld-replay.sav", index);
    write_in(x, name, x->engine.memory.data, x->engine.memory.size);
    snprintf(dir, sizeof dir, "mission-%03lld", index);
    {
        char path[EIO_PATH];
        join(path, sizeof path, x->campaign, dir);
        es2_os_create_directory(path);
    }
    files = jnew(J_OBJ);
    for (int i = 0; i < 4; i++) {
        char path[EIO_PATH], file[96];
        size_t n;
        uint8_t *data;
        snprintf(path, sizeof path, "DATA\\%s", LAUNCH_FILES[i]);
        if (!(data = eio_read(path, &n))) continue;
        snprintf(file, sizeof file, "%s\\%s", dir, LAUNCH_FILES[i]);
        write_in(x, file, data, n);
        jset(files, LAUNCH_FILES[i], jfile(data, n));
        free(data);
    }
    record = jnew(J_OBJ);
    jset(record, "index", jint(index));
    jset(record, "group", jint(jnum(played, "group", 0)));
    jset(record, "position", jint(jnum(played, "position", 0)));
    jset(record, "mission", jclone(jget(played, "mission")));
    jset(record, "replay_of", jint(jnum(played, "index", 0)));
    jset(record, "input_save", jstr(name));
    jset(record, "files", files);
    jset(record, "prepared_utc", jnow());
    jset(record, "input_rng", jclone(jget(c, "rng")));
    jset(record, "input_selectors", jclone(jget(c, "selectors")));
    jset(record, "output_rng", jclone(jget(c, "rng")));
    jset(record, "output_selectors", jclone(jget(c, "selectors")));
    jset(record, "hangar", jnew(J_NULL));
    jset(record, "results_before", jfile_state("DATA\\results.dat"));
    jset(record, "launched", jnow());
    jset(c, "pending", jclone(record));
    jpush(missions, record);
    set_save(x, name);
    jset(c, "finished", jnew(J_NULL));
    log_line(x, "menu: Replay of mission %lld as mission %lld (after %s); SAV %s", jnum(played, "index", 0), index,
             reason, name);
    log_save(x);
    return 1;
}
void eio_mission_name(eio *x, char *out, size_t capacity) {
    es2_sav s;
    es2_error e;
    char name[40];
    int16_t id;
    snprintf(out, capacity, "-");
    if (!eio_open(x) || !x->engine.memory.data ||
        es2_sav_parse((es2_span){x->engine.memory.data, x->engine.memory.size}, &s, &e) != ES2_OK)
        return;
    if (es2_shell_campaign_mission(&x->vfs, (int16_t)(s.selection.data[0] | s.selection.data[1] << 8),
                                   (int16_t)(s.selection.data[2] | s.selection.data[3] << 8), x->language, name,
                                   sizeof name, &id, &e) == ES2_OK)
        snprintf(out, capacity, "%s", name);
}
void eio_close(eio *x) {
    log_drop(x);
    es2_shell_engine_free(&x->engine);
    for (int i = 0; i < 3; i++) free(x->vols[i]);
    memset(x->vols, 0, sizeof x->vols);
    x->opened = 0;
}
