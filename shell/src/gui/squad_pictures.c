/* The squad panel's Herc pictures, the diagram colours and the slot
 * marker (es2/shell/gui/squad_pictures.h).
 *
 * Contract: docs/reverse-engineering/vshell_ui.md section 36 and
 * docs/formats/herc_pictures.md. Original: VSHELL.EXE:
 * 0x414340..0x4146cc, 0x41508f..0x41547e (the readers), 0x41547f..0x415a14,
 * 0x4146cd..0x414a3b (the loads), 0x415a15, 0x414bd9 (the unloads),
 * 0x414cbe..0x41501e (the diagram), 0x415bff..0x415dc0 (the marker),
 * 0x43c830..0x43ce79 (the hot spots), 0x43cfa5..0x43d028 (the dispatch),
 * 0x41f287..0x41f598 (the geometry setters). */
#include "es2/shell/gui/squad_pictures.h"

#include <stdio.h>
#include <string.h>

#include "es2/shell/campaign.h"
#include "es2/shell/gui/service_bay.h"

/* ---- file names (the executable's tables 0x46ffd4, 0x47006c, 0x4700b4,
 * 0x4700dc, 0x470100, 0x47001c, 0x470044; type t = entry t) --------------- */
static const char *const arm_dat[ES2_HERC_TYPES] = {
    "gam\\arm_outl.dat", "gam\\arm_rapt.dat", "gam\\arm_toma.dat", "gam\\arm_sams.dat", "gam\\arm_colo.dat",
    "gam\\arm_apoc.dat", "gam\\arm_ogre.dat", "gam\\arm_mavr.dat", "gam\\arm_razr.dat"};
static const char *const rpr_dat[ES2_HERC_TYPES] = {
    "gam\\rpr_outl.dat", "gam\\rpr_rapt.dat", "gam\\rpr_toma.dat", "gam\\rpr_sams.dat", "gam\\rpr_colo.dat",
    "gam\\rpr_apoc.dat", "gam\\rpr_ogre.dat", "gam\\rpr_mavr.dat", "gam\\rpr_razr.dat"};
static const char *const bod_dba[ES2_HERC_TYPES] = {
    "dba\\out_bod.dba", "dba\\rap_bod.dba", "dba\\tom_bod.dba", "dba\\sam_bod.dba", "dba\\col_bod.dba",
    "dba\\apoc_bod.dba", "dba\\ogr_bod.dba", "dba\\mav_bod.dba", "dba\\fly_bod.dba"};
static const char *const wep_dba[ES2_HERC_TYPES] = {
    "dba\\out_wep.dba", "dba\\rap_wep.dba", "dba\\tom_wep.dba", "dba\\sam_wep.dba", "dba\\col_wep.dba",
    "dba\\apoc_wep.dba", "dba\\ogr_wep.dba", "dba\\mav_wep.dba", "dba\\fly_wep.dba"};
static const char *const out_dba[ES2_HERC_TYPES] = {
    "dba\\out_out.dba", "dba\\rap_out.dba", "dba\\tom_out.dba", "dba\\sam_out.dba", "dba\\col_out.dba",
    "dba\\apoc_out.dba", "dba\\ogr_out.dba", "dba\\mav_out.dba", "dba\\fly_out.dba"};
static const char *const rpr_dba[ES2_HERC_TYPES] = {
    "dba\\rpr_outl.dba", "dba\\rpr_rapt.dba", "dba\\rpr_toma.dba", "dba\\rpr_sams.dba", "dba\\rpr_colo.dba",
    "dba\\rpr_apoc.dba", "dba\\rpr_ogre.dba", "dba\\rpr_mavr.dba", "dba\\rpr_razr.dba"};
static const char *const int_dba[ES2_HERC_TYPES] = {
    "dba\\outl_int.dba", "dba\\rapt_int.dba", "dba\\toma_int.dba", "dba\\sams_int.dba", "dba\\colo_int.dba",
    "dba\\apoc_int.dba", "dba\\ogre_int.dba", "dba\\mavr_int.dba", "dba\\razr_int.dba"};
#define EMPTY_DBA "dba\\mt_3qtr.dba"   /* [0x46ff74], entry 9 of 0x4700b4 */
#define WPNS_DBA "dba\\rpr_wpns.dba"   /* [0x46ff28], entry 9 of 0x47001c */

/* ---- readers ------------------------------------------------------------------- */

typedef struct reader {
    es2_span f;
    size_t at;
    int bad;
} reader;

static int16_t word(reader *r) {
    if (r->bad || r->f.size - r->at < 2) {
        r->bad = 1;
        return 0;
    }
    int16_t v = (int16_t)(uint16_t)(r->f.data[r->at] | r->f.data[r->at + 1] << 8);
    r->at += 2;
    return v;
}
static int32_t dword(reader *r) {
    uint16_t lo = (uint16_t)word(r), hi = (uint16_t)word(r);
    return (int32_t)((uint32_t)lo | (uint32_t)hi << 16);
}

/* One record: word layer, dwords x and y, (arm: dwords x2 and y2), word
 * frame, word mirror (read into the low half of a dword whose high half the
 * reader never writes: 0 in the zero-initialised table). */
static void record(reader *r, es2_herc_picture *p, int wide) {
    memset(p, 0, sizeof *p);
    p->layer = word(r);
    p->x = dword(r);
    p->y = dword(r);
    if (wide) {
        p->x2 = dword(r);
        p->y2 = dword(r);
    }
    p->frame = word(r);
    p->mirror = (int32_t)(uint16_t)word(r);
}

/* The slot lists: word n, n x (word item ID, word count, count records). */
static int lists(reader *r, es2_herc_type_pictures *t, es2_herc_list *out, int wide) {
    int16_t n = word(r);
    if (r->bad || n < 0) return -1;
    for (int16_t i = 0; i < n; i++) {
        int16_t id = word(r), count = word(r);
        /* VSHELL indexes 33-entry tables by the ID without a check */
        if (r->bad || id < 0 || id >= ES2_HERC_ITEMS || count < 0 || t->used + count > ES2_HERC_RECORDS) return -1;
        out[id].count = count;
        out[id].first = t->used;
        for (int16_t k = 0; k < count; k++) {
            es2_herc_picture *p = &t->records[t->used++];
            record(r, p, wide);
            if (wide) { /* 0x415335, 0x415353, 0x41539f, 0x4153bd: inc dword */
                p->x++;
                p->y++;
                p->x2++;
                p->y2++;
            }
        }
        if (r->bad) return -1;
    }
    return 0;
}

/* Original: VSHELL.EXE 0x41508f, one type */
int es2_herc_arm_parse(es2_span file, es2_herc_type_pictures *t) {
    reader r = {file, 0, 0};
    if (!t || !file.data) return -1;
    memset(t->arm, 0, sizeof t->arm);
    t->arm_loaded = 0;
    record(&r, &t->body[0], 1);
    record(&r, &t->body[1], 1);
    if (r.bad || lists(&r, t, t->arm, 1) < 0) return -1;
    t->arm_loaded = 1;
    return 0;
}

/* Original: VSHELL.EXE 0x414340, one type */
int es2_herc_rpr_parse(es2_span file, es2_herc_type_pictures *t) {
    reader r = {file, 0, 0};
    int16_t n;
    if (!t || !file.data) return -1;
    memset(t->rpr, 0, sizeof t->rpr);
    t->rpr_loaded = 0;
    n = word(&r);
    if (r.bad || n < 0 || t->used + n > ES2_HERC_RECORDS) return -1;
    t->layer_count = n;
    t->layer_first = t->used;
    for (int16_t i = 0; i < n; i++) record(&r, &t->records[t->used++], 0);
    record(&r, &t->diagram, 0);
    if (r.bad || lists(&r, t, t->rpr, 0) < 0) return -1;
    t->rpr_loaded = 1;
    return 0;
}

/* Original: VSHELL.EXE 0x43c991..0x43ca84 */
int es2_squad_hots_parse(es2_span file, es2_squad_hots *h) {
    reader r = {file, 0, 0};
    int16_t n;
    if (!h || !file.data) return -1;
    memset(h, 0, sizeof *h);
    n = word(&r);
    if (r.bad || n != ES2_HERC_TYPES) return -1; /* asserted (0x43c9ae) */
    for (int i = 0; i < n; i++) {
        (void)word(&r); /* the type word: read, the entry is stored at index i */
        int16_t count = word(&r);
        if (r.bad || count < 0 || count > ES2_SQUAD_HOT_SPOTS) return -1;
        h->count[i] = count;
        for (int k = 0; k < count; k++)
            for (int c = 0; c < 4; c++) h->rect[i][k][c] = dword(&r);
        if (r.bad) return -1;
    }
    h->loaded = 1;
    return 0;
}

/* Original: VSHELL.EXE 0x4142f0 */
const es2_herc_picture *es2_herc_lookup(const es2_herc_type_pictures *t, int rpr, int16_t id, int16_t key) {
    const es2_herc_list *l;
    if (!t || id < 0 || id >= ES2_HERC_ITEMS) return NULL;
    l = rpr ? &t->rpr[id] : &t->arm[id];
    for (int16_t i = 0; i < l->count; i++)
        if (t->records[l->first + i].layer == key) return &t->records[l->first + i];
    return NULL;
}

int es2_squad_pictures_load(es2_squad_pictures *p) {
    int failed = 0;
    es2_span f;
    if (!p || !p->host.file) return -1;
    for (int t = 0; t < ES2_HERC_TYPES; t++) {
        es2_herc_type_pictures *tp = &p->type[t];
        memset(tp, 0, sizeof *tp);
        f = p->host.file(p->host.ctx, arm_dat[t]);
        if (es2_herc_arm_parse(f, tp) < 0) failed++;
        f = p->host.file(p->host.ctx, rpr_dat[t]);
        if (es2_herc_rpr_parse(f, tp) < 0) failed++;
    }
    if (es2_squad_hots_parse(p->host.file(p->host.ctx, "gam\\arm_hots.dat"), &p->arm_hots) < 0) failed++;
    if (es2_squad_hots_parse(p->host.file(p->host.ctx, "gam\\rpr_hots.dat"), &p->rpr_hots) < 0) failed++;
    p->loaded = 1;
    return failed;
}

/* ---- helpers on the panels -------------------------------------------------------- */

static es2_squad_pictures *pictures(const es2_service_bay *b) {
    return b && b->ui && b->pictures && b->pictures->loaded ? b->pictures : NULL;
}

static int record_of(es2_squad_pictures *p, int k, es2_squad_record *d) {
    memset(d, 0, sizeof *d);
    d->vehicle = -1;
    if (k < 0 || k > 7 || !p->host.record || !p->host.record(p->host.ctx, k, d) || !d->record) {
        memset(d, 0, sizeof *d);
        d->vehicle = -1;
        return 0;
    }
    if (d->vehicle < 0 || d->vehicle >= ES2_HERC_TYPES || d->capacity < 0 || d->capacity > 10) {
        p->refused++; /* VSHELL indexes its nine-entry tables by the word */
        d->vehicle = -1;
        return 0;
    }
    return 1;
}

static const es2_bitmap *frame(es2_squad_pictures *p, const char *file, int16_t n) {
    const es2_bitmap *bm = p->host.frame ? p->host.frame(p->host.ctx, file, n) : NULL;
    if (!bm) p->refused++; /* a frame outside the DBA: VSHELL reads past its pointer table */
    return bm;
}

/* 0x40bd58: x, y, bitmap 0; ten recolour pairs 0x0e -> 0x10 (off); mirror 0. */
static void layer_init(es2_ui_layer *l) {
    memset(l, 0, sizeof *l);
    for (int i = 0; i < 10; i++) {
        l->from[i] = 0x0e;
        l->to[i] = 0x10;
    }
}

static void set_layer(es2_service_bay *b, es2_ui_id grid, int index, const es2_ui_layer *l) { /* 0x40be7f */
    if (!grid) return;
    if (es2_ui_grid_set_layer(b->ui, grid, index, l) < 0) b->pictures->refused++;
}

static void reset_layer(es2_service_bay *b, es2_ui_id grid, int index) { /* 0x40bdf4 */
    es2_ui_layer l;
    layer_init(&l);
    set_layer(b, grid, index, &l);
}

static es2_ui_layer *layer_of(es2_service_bay *b, es2_ui_id grid, int index) {
    es2_ui_widget *w = es2_ui_get(b->ui, grid);
    if (!w || w->cls != ES2_UI_GRID || w->grid < 0 || index < 0 || index >= ES2_UI_GRID_LAYERS) return NULL;
    return &b->ui->layers[w->grid][index];
}

static es2_ui_id squad_grid(const es2_service_bay *b, int k) { return k >= 0 && k <= 8 ? b->squad[ES2_SQUAD_GRID + k] : 0; }
static es2_ui_id repair_grid(const es2_service_bay *b, int k) {
    return k >= 0 && k <= 7 && b->host.repair_grid ? b->host.repair_grid(b->host.ctx, (int16_t)k) : 0;
}

/* 0x41f599: the absolute rectangle from the parent's, for the window and its
 * descendants. */
static void place(es2_ui *ui, es2_ui_id id) {
    es2_ui_widget *w = es2_ui_get(ui, id), *p;
    if (!w) return;
    p = es2_ui_get(ui, w->parent);
    int32_t ox = p ? p->abs.left : 0, oy = p ? p->abs.top : 0;
    w->abs.left = ox + w->rect.left;
    w->abs.top = oy + w->rect.top;
    w->abs.right = ox + w->rect.right;
    w->abs.bottom = oy + w->rect.bottom;
    for (es2_ui_id c = w->first; c; c = ui->w[c - 1].next) place(ui, c);
}

/* Original: VSHELL.EXE 0x41f471 (move to (x, y) in the parent, clamped, size
 * kept; then 0x41f599 and the redraw 0x41f3ed). */
static void move_to(es2_ui *ui, es2_ui_id id, int32_t x, int32_t y) {
    es2_ui_widget *w = es2_ui_get(ui, id), *p;
    if (!w || !(p = es2_ui_get(ui, w->parent))) return;
    int32_t width = w->rect.right - w->rect.left, height = w->rect.bottom - w->rect.top;
    int32_t pw = p->rect.right - p->rect.left + 1, ph = p->rect.bottom - p->rect.top + 1;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (width + x + 1 > pw) x = pw - (width + 1);
    if (height + y + 1 > ph) y = ph - (height + 1);
    if (x < 0 || y < 0) return;
    w->rect.left = x;
    w->rect.top = y;
    w->rect.right = x + width;
    w->rect.bottom = y + height;
    place(ui, id);
    es2_ui_redraw_tree(ui, id);
}

/* Original: VSHELL.EXE 0x41f287 (width) and 0x41f33a (height): only when the
 * parent has room and no child reaches beyond the new edge; no redraw. (The
 * hover update when the cursor lies beyond the new edge, 0x41f2ef..0x41f331,
 * is not modelled: the panels are not under a capturing window.) */
static void set_width(es2_ui *ui, es2_ui_id id, int32_t n) {
    es2_ui_widget *w = es2_ui_get(ui, id), *p;
    if (!w || n <= 0 || !(p = es2_ui_get(ui, w->parent))) return;
    if (p->rect.right - p->rect.left + 1 < w->rect.left + n) return;
    int32_t right = w->rect.left + n - 1, abs_right = p->abs.left + right;
    for (es2_ui_id c = w->first; c; c = ui->w[c - 1].next)
        if (abs_right < ui->w[c - 1].abs.right) return;
    w->rect.right = right;
    w->abs.right = abs_right;
}
static void set_height(es2_ui *ui, es2_ui_id id, int32_t n) {
    es2_ui_widget *w = es2_ui_get(ui, id), *p;
    if (!w || n <= 0 || !(p = es2_ui_get(ui, w->parent))) return;
    if (p->rect.bottom - p->rect.top + 1 < w->rect.top + n) return;
    int32_t bottom = w->rect.top + n - 1, abs_bottom = p->abs.top + bottom;
    for (es2_ui_id c = w->first; c; c = ui->w[c - 1].next)
        if (abs_bottom < ui->w[c - 1].abs.bottom) return;
    w->rect.bottom = bottom;
    w->abs.bottom = abs_bottom;
}

static void geometry(es2_service_bay *b, es2_ui_id grid, int32_t x, int32_t y, int32_t w, int32_t h, int lines) {
    es2_ui_widget *g = es2_ui_get(b->ui, grid);
    if (!g) return;
    move_to(b->ui, grid, x, y);   /* 0x41f471 */
    set_width(b->ui, grid, w);    /* 0x41f287 */
    set_height(b->ui, grid, h);   /* 0x41f33a */
    g->grid_lines = (uint8_t)lines; /* +0x6e5 */
}

/* ---- 0x41547f: WEAPONS, BUILD, CREW ------------------------------------------------ */

/* The body or outline layer: finished -> the record's frame of the body DBA;
 * progress 0 -> the empty bay picture's frame `empty`; progress up to 50 ->
 * body frame `low`, else `high` (0x41566a..0x415723). */
static void body_layer(es2_service_bay *b, es2_squad_pictures *p, es2_ui_id grid, const es2_squad_record *d,
                       const es2_herc_picture *rec, int16_t empty, int16_t low, int16_t high, es2_ui_layer *l) {
    int16_t v = d->vehicle, n;
    const char *file = bod_dba[v];
    l->x = rec->x;
    l->y = rec->y;
    if (d->progress == 100) n = rec->frame;           /* 0x411088 */
    else if (d->progress == 0) file = EMPTY_DBA, n = empty;
    else n = d->progress < 0x33 ? low : high;         /* jg 0x32, signed */
    l->bitmap = frame(p, file, n);
    l->mirror = rec->mirror;
    set_layer(b, grid, rec->layer, l);
}

static void load_arm(es2_service_bay *b, es2_squad_pictures *p) {
    es2_ui_layer l;
    es2_ui_id none = squad_grid(b, 8);
    layer_init(&l);                                   /* 0x41548f, once */
    l.x = 1, l.y = 1, l.bitmap = frame(p, EMPTY_DBA, 0);
    set_layer(b, none, 0, &l);                        /* [0x48d5bc] */
    l.x = 1, l.y = 0x8c, l.bitmap = frame(p, EMPTY_DBA, 1);
    set_layer(b, none, 1, &l);
    for (int k = 0; k < 8; k++) {
        es2_ui_id grid = squad_grid(b, k);
        es2_squad_record d;
        geometry(b, grid, 5, 0x2b, 0xe7, 0x105, 0);
        if (!record_of(p, k, &d)) {                   /* vehicle -1: the empty bay */
            l.x = 1, l.y = 1, l.bitmap = frame(p, EMPTY_DBA, 0), l.mirror = 0;
            set_layer(b, grid, 0, &l);
            l.x = 1, l.y = 0x8c, l.bitmap = frame(p, EMPTY_DBA, 1), l.mirror = 0;
            set_layer(b, grid, 1, &l);
            continue;
        }
        const es2_herc_type_pictures *t = &p->type[d.vehicle];
        body_layer(b, p, grid, &d, &t->body[0], 0, 2, 4, &l);
        body_layer(b, p, grid, &d, &t->body[1], 1, 3, 5, &l);
        for (int16_t s = 0; s < d.capacity; s++) {
            int16_t id = d.item[s];
            const es2_herc_picture *r;
            if (id == -1 || id == 0) continue;
            r = es2_herc_lookup(t, 0, id, (int16_t)(s + 2));
            if (!r || r->frame == -1) continue;
            l.x = r->x, l.y = r->y, l.bitmap = frame(p, wep_dba[d.vehicle], r->frame), l.mirror = r->mirror;
            set_layer(b, grid, r->layer, &l);
        }
    }
}

/* ---- 0x4146cd: REPAIR ---------------------------------------------------------------- */

static void load_rpr(es2_service_bay *b, es2_squad_pictures *p) {
    es2_ui_layer l;
    layer_init(&l);                                   /* 0x4146dd, once: from[0] and the mirror carry over */
    for (int k = 0; k < 8; k++) {
        es2_ui_id grid = squad_grid(b, k), rgrid = repair_grid(b, k);
        es2_squad_record d;
        geometry(b, grid, 0x10, 0x2f, 0xd0, 0x100, 1);
        if (!record_of(p, k, &d)) continue;
        const es2_herc_type_pictures *t = &p->type[d.vehicle];
        int16_t v = d.vehicle;
        for (int16_t i = 0; i < t->layer_count; i++) {
            const es2_herc_picture *r = &t->records[t->layer_first + i];
            l.x = r->x, l.y = r->y, l.bitmap = frame(p, rpr_dba[v], r->frame), l.mirror = r->mirror;
            l.from[0] = 0x0e;
            set_layer(b, grid, r->layer, &l);
        }
        for (int16_t s = 0; s < d.capacity; s++) {
            int16_t id = d.item[s];
            const es2_herc_picture *r;
            if (id == -1 || id == 0) continue;
            if (!(r = es2_herc_lookup(t, 1, id, (int16_t)(s + 6)))) continue;
            l.x = r->x, l.y = r->y, l.bitmap = frame(p, WPNS_DBA, r->frame), l.mirror = r->mirror;
            l.from[0] = 0x0f;
            set_layer(b, grid, r->layer, &l);
        }
        /* REPAIR's internal picture: mirror 0, from[0] as the last layer left it */
        l.x = t->diagram.x, l.y = t->diagram.y, l.bitmap = frame(p, int_dba[v], t->diagram.frame), l.mirror = 0;
        set_layer(b, rgrid, t->diagram.layer, &l);
        if (v == 8) { /* 0x4149e6: the Razor's second internal frame */
            l.x = 0x1d, l.y = 0x0e, l.bitmap = frame(p, int_dba[8], 1);
            set_layer(b, rgrid, 1, &l);
        }
    }
}

/* 0x43c830(3)'s 0x414a3c for every mounted slot: VSHELL asserts (severity 4,
 * src\hercdisp.cpp line 0xe9) when the slot list lacks the record. */
static int rpr_data_error(es2_squad_pictures *p) {
    for (int k = 0; k < 8; k++) {
        es2_squad_record d;
        if (!record_of(p, k, &d)) continue;
        for (int16_t s = 0; s < d.capacity; s++) {
            int16_t id = d.item[s];
            char text[160];
            if (id == -1 || id == 0 || es2_herc_lookup(&p->type[d.vehicle], 1, id, (int16_t)(s + 6))) continue;
            const char *herc = p->host.text ? p->host.text(p->host.ctx, 0x6eu + (uint32_t)d.vehicle) : NULL;
            char name[64];
            snprintf(name, sizeof name, "%s", herc ? herc : "");
            const char *weapon = p->host.text ? p->host.text(p->host.ctx, 0x7eu + (uint32_t)id) : NULL;
            snprintf(text, sizeof text, "Data Error: Herc: %s   Socket: %d \tWeapon: %s", name, (int)s,
                     weapon ? weapon : "");
            p->data_errors++;
            if (p->host.data_error) p->host.data_error(p->host.ctx, text);
            return 1;
        }
    }
    return 0;
}

/* ---- the dispatch 0x43cfa5 / 0x43cfea ---------------------------------------------------- */

void es2_squad_pictures_panel(es2_service_bay *b, int word, int load) {
    es2_squad_pictures *p = pictures(b);
    if (!p) return;
    if (load) {
        switch (word) {
        case 2: case 4: case 6: load_arm(b, p); break;   /* 0x41547f (2: then 0x43c830(2)) */
        case 3:                                          /* 0x4146cd (ends in 0x414cbe), 0x43c830(3) */
            load_rpr(b, p);
            es2_squad_pictures_diagram(b);
            (void)rpr_data_error(p);
            break;
        default: break;
        }
        return;
    }
    switch (word) {
    case 2: case 4: case 6:                              /* 0x415a15 (2: then 0x43ce7a) */
        es2_ui_grid_clear(b->ui, squad_grid(b, 8));
        for (int k = 0; k < 8; k++) es2_ui_grid_clear(b->ui, squad_grid(b, k));
        break;
    case 3:                                              /* 0x414bd9, 0x43ce7a */
        for (int k = 0; k < 8; k++) {
            es2_ui_grid_clear(b->ui, squad_grid(b, k));
            es2_ui_grid_clear(b->ui, repair_grid(b, k));
        }
        break;
    default: break;
    }
}

/* ---- 0x414cbe -------------------------------------------------------------------------- */

static int16_t band_colour(const es2_service_bay *b, int16_t c) { return b->band_colour[es2_squad_band(b, c)]; }

void es2_squad_pictures_diagram(es2_service_bay *b) {
    es2_squad_pictures *p = pictures(b);
    es2_squad_record d;
    int16_t k, c;
    if (!p) return;
    k = b->session.bay;
    if (!record_of(p, k, &d)) return;                 /* 0x411164 = -1 (also for a bay below 0) */
    const es2_herc_type_pictures *t = &p->type[d.vehicle];
    if (b->session.tab == 0) {
        es2_ui_id grid = squad_grid(b, k);
        for (int16_t i = 0; i < t->layer_count; i++) {
            int16_t layer = t->records[t->layer_first + i].layer;
            int16_t group = layer >= 0x10 ? (int16_t)(layer - 0x10) : layer;
            es2_ui_layer *l = layer_of(b, grid, layer);
            if (es2_shell_condition(p->groups, d.words, 0, group, &c, NULL) != ES2_OK || !l) {
                p->refused++; /* VSHELL reads past the group table / asserts at 0x414d86 */
                continue;
            }
            l->to[0] = band_colour(b, c);
        }
        for (int16_t s = 0; s < d.capacity; s++) {
            es2_ui_layer *l = layer_of(b, grid, s + 6);
            if (es2_shell_condition(p->groups, d.words, 2, s, &c, NULL) != ES2_OK || !l) {
                p->refused++;
                continue;
            }
            l->to[0] = band_colour(b, c);
        }
        es2_ui_draw(b->ui, grid);                     /* vtable +4 */
    }
    if (b->session.tab == 1) {
        es2_ui_id rgrid = repair_grid(b, k);
        es2_ui_layer *l = layer_of(b, rgrid, 0);
        if (!l) return;
        for (int16_t s = 0; s < 9; s++) {             /* layer 0's ten pairs, nine used */
            l->from[s] = p->internal_from[s];
            if (es2_shell_condition(p->groups, d.words, 1, s, &c, NULL) != ES2_OK) {
                p->refused++;
                continue;
            }
            l->to[s] = band_colour(b, c);
        }
        es2_ui_draw(b->ui, rgrid);
    }
}

/* ---- 0x415bff and 0x43dfc8 --------------------------------------------------------------- */

void es2_squad_pictures_marker(es2_service_bay *b, int16_t slot) {
    es2_squad_pictures *p = pictures(b);
    es2_squad_record d;
    int16_t k, id;
    if (!p) return;
    k = b->session.bay;
    if (record_of(p, k, &d)) {
        es2_ui_id grid = squad_grid(b, k);
        const es2_herc_picture *r;
        es2_ui_layer l, *m;
        if (slot < 0 || slot >= 10) { /* VSHELL reads the pointer outside the ten */
            p->refused++;
            return;
        }
        id = d.item[slot];
        layer_init(&l);
        r = es2_herc_lookup(&p->type[d.vehicle], 0, id, (int16_t)(slot + 2));
        if (!r || r->frame == -1) {
            reset_layer(b, grid, slot + 2);           /* 0x415da0 */
        } else {
            if (id != 0) {                            /* the slot's weapon */
                l.x = r->x, l.y = r->y, l.bitmap = frame(p, wep_dba[d.vehicle], r->frame), l.mirror = r->mirror;
                set_layer(b, grid, r->layer, &l);
            } else {
                reset_layer(b, grid, slot + 2);
            }
            /* the marker: the outline DBA's frame at the second point, 0xba -> 0x63 */
            l.x = r->x2, l.y = r->y2, l.bitmap = frame(p, out_dba[d.vehicle], r->frame), l.mirror = r->mirror;
            l.from[0] = 0xba;
            set_layer(b, grid, 12, &l);
            if ((m = layer_of(b, grid, 12)) != NULL) m->to[0] = 0x63;   /* +0x315 */
        }
    }
    if (k >= 0 && k <= 7) es2_ui_redraw_tree(b->ui, squad_grid(b, k)); /* 0x43e2ca / 0x43e33e */
}

void es2_squad_pictures_marker_off(es2_service_bay *b) {
    es2_squad_pictures *p = pictures(b);
    if (!p) return;
    if (b->session.bay < 0 || b->session.bay > 7) { /* [0x48d598] is Registration's ACCEPT: own, nothing */
        p->refused++;
        return;
    }
    reset_layer(b, squad_grid(b, b->session.bay), 12);
}

/* ---- 0x43c830 (never shown) ------------------------------------------------------------- */

int es2_squad_hot_spots(const es2_service_bay *b, int word, int k, es2_squad_hot_spot *out, int max) {
    es2_squad_pictures *p = pictures(b);
    es2_squad_record d;
    int n = 0;
    if (!p || !out || max <= 0 || !record_of(p, k, &d)) return 0;
    if (word == 2) {
        const es2_squad_hots *h = &p->arm_hots;
        for (int16_t s = 0; s < d.capacity && n < max; s++) {
            if (s >= h->count[d.vehicle]) { /* VSHELL reads past the type's list */
                p->refused++;
                break;
            }
            memcpy(out[n].rect, h->rect[d.vehicle][s], sizeof out[n].rect);
            out[n++].handler = 0x43e7efu + 0x61u * (uint32_t)s; /* [0x48d61c + 4s] */
        }
    } else if (word == 3) {
        const es2_squad_hots *h = &p->rpr_hots;
        for (int16_t s = 0; s < h->count[d.vehicle] && n < max; s++) {
            memcpy(out[n].rect, h->rect[d.vehicle][s], sizeof out[n].rect);
            out[n++].handler = 0x434745u + 0x65u * (uint32_t)s; /* the External rows */
        }
        for (int16_t s = 0; s < d.capacity && n < max; s++) {
            int16_t id = d.item[s];
            const es2_herc_picture *r;
            const es2_bitmap *bm;
            if (id == -1 || id == 0) continue;
            r = es2_herc_lookup(&p->type[d.vehicle], 1, id, (int16_t)(s + 6));
            if (!r) break; /* the data error: VSHELL ends the process */
            bm = p->host.frame ? p->host.frame(p->host.ctx, WPNS_DBA, r->frame) : NULL;
            /* 0x414a3c: (x, y) to (x + width, y + height) of the frame */
            out[n].rect[0] = r->x;
            out[n].rect[1] = r->y;
            out[n].rect[2] = r->x + (bm ? (int32_t)bm->width : 0);
            out[n].rect[3] = r->y + (bm ? (int32_t)bm->height : 0);
            out[n++].handler = 0x434745u + 0x65u * (uint32_t)(s + 6); /* [0x48d634 + 4s] */
        }
    }
    return n;
}
