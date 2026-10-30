/* The BUILD screen 0x445de8, the ARMORY screen 0x4484c4 and the
 * weapons scrap warning 0x447e3e (es2/shell/gui/build_armory.h).
 *
 * Contract: docs/reverse-engineering/vshell_ui.md sections 4.7, 12.4, 17.4,
 * 17.5 and 31. Original: VSHELL.EXE 0x445de8..0x4479b7,
 * 0x447e3e..0x44b2fe and 0x414340..0x415fa0. */
#include "es2/shell/gui/build_armory.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "es2/formats/dat.h"
#include "es2/formats/sav.h"

/* ---- numbers the screens draw ------------------------------------------------ */

static uint16_t u16le(const uint8_t *p) { return (uint16_t)(p[0] | (unsigned)p[1] << 8); }
static int16_t s16le(const uint8_t *p) { return (int16_t)u16le(p); }
static int32_t s32le(const uint8_t *p) { return (int32_t)((uint32_t)u16le(p) | (uint32_t)u16le(p + 2) << 16); }

/* VSHELL's memory tables as the SAV and the game files fill them: the
 * weapon table 0x483cc8 + 0x1d c (cost +0x14, flag +0x16, count +0x17), the
 * five-entry bank 0x46fa14/0x46fa16, HERC_INF 0x483c38 + 16 t with the
 * nine-word bank at +0x0e. */
typedef struct numbers {
    int ok;
    uint8_t flag[33];
    int16_t count[33];
    uint16_t cost[33];
    int16_t free_count, five[5];
    int16_t nine[ES2_BUILD_TYPES];
    int16_t herc[ES2_BUILD_TYPES][5]; /* words +0, +2, +4, +6, +8 */
} numbers;

/* The cost word: (u16)(WEAPONS.DAT word * 1000) by item ID (0x4126xx). The
 * table is filled at start-up (0x401aa4 calls 0x4125e8 at 0x401d4e, before
 * the screens' construction 0x40180c at 0x401ff8), so it needs no game. */
static void load_costs(const es2_vfs *vfs, uint16_t cost[33]) {
    es2_vfs_copy c;
    es2_dat d;
    memset(cost, 0, 33 * sizeof cost[0]);
    if (vfs && es2_vfs_find(vfs, "GAM/WEAPONS.DAT", &c, NULL) == ES2_OK &&
        es2_dat_parse(ES2_DAT_SHELL_WEAPONS, c.payload, 0, &d, NULL) == ES2_OK)
        for (size_t i = 0; i < d.count; i++) {
            es2_dat_shell_weapon w;
            if (es2_dat_shell_weapon_at(&d, i, &w, NULL) != ES2_OK) break;
            if (w.weapon_id >= 0 && w.weapon_id <= 32)
                cost[w.weapon_id] = (uint16_t)((uint32_t)w.word_after_name * 1000u);
        }
}

static int load_numbers(const es2_build_armory_game *g, numbers *n) {
    es2_sav sav;
    es2_vfs_copy c;
    es2_dat d;
    memset(n, 0, sizeof *n);
    if (!g || !g->save.data || !g->vfs) return -1;
    if (es2_sav_parse((es2_span){g->save.data, g->save.size}, &sav, NULL) != ES2_OK) return -1;
    for (int i = 0; i < 33; i++) {
        n->flag[i] = sav.inventory[i].flag;
        n->count[i] = (int16_t)sav.inventory[i].count;
    }
    if (sav.five_entry_bank.size >= 22) {
        n->free_count = s16le(sav.five_entry_bank.data);
        for (int i = 0; i < 5; i++) {
            int16_t index = s16le(sav.five_entry_bank.data + 2 + 4 * i);
            if (index >= 0 && index < 5) n->five[index] = s16le(sav.five_entry_bank.data + 4 + 4 * i);
        }
    }
    if (sav.nine_word_bank.size >= 18)
        for (int i = 0; i < ES2_BUILD_TYPES; i++) n->nine[i] = s16le(sav.nine_word_bank.data + 2 * i);
    if (es2_vfs_find(g->vfs, "GAM/HERC_INF.DAT", &c, NULL) == ES2_OK &&
        es2_dat_parse(ES2_DAT_HERC_INF, c.payload, 0, &d, NULL) == ES2_OK)
        for (size_t i = 0; i < d.count && i < ES2_BUILD_TYPES; i++) {
            es2_dat_herc_info h;
            if (es2_dat_herc_info_at(&d, i, &h, NULL) != ES2_OK) break;
            n->herc[i][0] = h.herc_id;
            n->herc[i][1] = h.word_0x02;
            n->herc[i][2] = h.word_0x04;
            n->herc[i][3] = h.word_0x06;
            n->herc[i][4] = h.word_0x08;
        }
    load_costs(g->vfs, n->cost);
    n->ok = 1;
    return 0;
}

/* 0x412c66: entries of the bank equal to the category */
static int16_t in_bank(const numbers *n, int16_t c) {
    int16_t k = 0;
    for (int i = 0; i < 5; i++) k = (int16_t)(k + (n->five[i] == c));
    return k;
}

/* ---- helpers ------------------------------------------------------------------- */

static es2_ui_widget *W(es2_build_armory *b, es2_ui_id id) { return es2_ui_get(b->ui, id); }
static int has_game(const es2_build_armory *b) { return b->game && b->game->session; }
static const char *text_of(es2_build_armory *b, uint32_t id) {
    const char *t = b->host.text ? b->host.text(b->host.ctx, id) : NULL;
    return t ? t : "";
}
static uint8_t building(const es2_build_armory *b) {
    if (b->flow) return b->flow->prefs.current[ES2_SHELL_PREFS_BUILD];
    return has_game(b) ? (uint8_t)b->game->session->building : 0;
}
static int16_t row_category(const es2_build_armory *b, int row) {
    if (!b->game || !b->game->editor_tables.data || b->game->editor_tables.size < ES2_SHELL_EDITOR_TABLES_SIZE ||
        row < 0 || row >= ES2_SHELL_ARMORY_ROWS)
        return -1;
    return s16le(b->game->editor_tables.data + 46 + 2 * row);
}
/* 0x43fd87: the first index of an item ID in the row table 0x476a4c (its
 * 27th word is not among the editor tables: an ID found only there, or
 * nowhere, gives -1 here; own choice). */
static int slot_of(const es2_build_armory *b, int16_t id) {
    for (int r = 0; r < ES2_SHELL_ARMORY_ROWS; r++)
        if (row_category(b, r) == id) return r;
    return -1;
}

/* An ESMessage's text with its own alignment and colour (+0x45, +0xb5), drawn. */
static void own_text(es2_build_armory *b, es2_ui_id id, const char *text) {
    es2_ui_widget *w = W(b, id);
    if (w) (void)es2_ui_message_set(b->ui, id, text, w->align, w->text_colour, 1);
}
static es2_ui_id column(es2_build_armory *b, es2_ui_id quad, int k) {
    es2_ui_widget *w = W(b, quad);
    return w && w->cls == ES2_UI_QUAD ? w->columns[k] : 0;
}
static void quad_colours(es2_build_armory *b, es2_ui_id quad, int32_t colour) {
    for (int k = 0; k < 4; k++) es2_ui_quad_set_colour(b->ui, quad, k, colour);
}

/* A text button's state as 0x447064 and 0x449b87 write it: the label colour
 * (+0x55 -> +0xb5) and the frame colour (+0x4d), the tree redraw 0x41f3ed,
 * +0x49, then the class draw (vtable +4). */
static void button_state(es2_build_armory *b, es2_ui_id id, int on) {
    es2_ui_widget *w = W(b, id), *l;
    if (!w) return;
    if ((l = W(b, w->label))) l->text_colour = on ? 0x29 : 0x26;
    w->colour = on ? 0x22 : 0x26;
    es2_ui_redraw_tree(b->ui, id);
    W(b, id)->enabled = on;
    es2_ui_draw(b->ui, id);
}

/* One library operation on the loaded game (the SAV replaced on success). */
static int apply(es2_build_armory *b, int kind, int16_t a, int16_t bb) {
    es2_build_armory_game *g = b->game;
    es2_shell_editor_op op;
    es2_shell_buffer out = {NULL, 0};
    es2_error e;
    memset(&e, 0, sizeof e);
    if (!g || !g->session || !g->save.data) {
        b->last_status = ES2_ERR_ARGUMENT;
        return -1;
    }
    op.kind = kind;
    op.a = a;
    op.b = bb;
    op.applied = 0;
    g->session->building = building(b);
    b->last_status = es2_shell_editors(g->vfs, g->tables, g->editor_tables, (es2_span){g->save.data, g->save.size},
                                       g->session, &op, 1, &out, &b->view, &e);
    if (b->last_status != ES2_OK) return -1;
    free(g->save.data);
    g->save = out;
    return op.applied;
}

/* The view of the current state without an operation. */
static void view_now(es2_build_armory *b) {
    es2_build_armory_game *g = b->game;
    es2_shell_buffer out = {NULL, 0};
    es2_error e;
    memset(&e, 0, sizeof e);
    if (!g || !g->session || !g->save.data) return;
    g->session->building = building(b);
    if (es2_shell_editors(g->vfs, g->tables, g->editor_tables, (es2_span){g->save.data, g->save.size}, g->session,
                          NULL, 0, &out, &b->view, &e) == ES2_OK)
        free(out.data);
}

static int note(es2_build_armory *b, uint32_t handler, int applied) {
    b->last_handler = handler;
    b->last_applied = applied;
    b->events++;
    if (b->host.changed) b->host.changed(b->host.ctx, handler, applied);
    return applied;
}

/* ---- files --------------------------------------------------------------------- */

const char *es2_build_picture_file(int type, int dba) {
    static const char *const dat[ES2_BUILD_TYPES] = {
        "gam\\rpr_outl.dat", "gam\\rpr_rapt.dat", "gam\\rpr_toma.dat", "gam\\rpr_sams.dat", "gam\\rpr_colo.dat",
        "gam\\rpr_apoc.dat", "gam\\rpr_ogre.dat", "gam\\rpr_mavr.dat", "gam\\rpr_razr.dat"};
    static const char *const pic[ES2_BUILD_TYPES] = {
        "dba\\rpr_outl.dba", "dba\\rpr_rapt.dba", "dba\\rpr_toma.dba", "dba\\rpr_sams.dba", "dba\\rpr_colo.dba",
        "dba\\rpr_apoc.dba", "dba\\rpr_ogre.dba", "dba\\rpr_mavr.dba", "dba\\rpr_razr.dba"};
    if (type < 0 || type >= ES2_BUILD_TYPES) return NULL;
    return dba ? pic[type] : dat[type];
}

static es2_span file_of(es2_build_armory *b, const char *name) {
    es2_span none = {NULL, 0};
    return b->host.file ? b->host.file(b->host.ctx, name) : none;
}
static const es2_bitmap *member(es2_build_armory *b, const char *file, int m) {
    return b->host.bitmap && file ? b->host.bitmap(b->host.ctx, file, m) : NULL;
}

/* ---- resolver for the layout table
 * ----------------------------------------------- */

static const es2_dfn *resolve_font(void *context, uint32_t global) {
    es2_build_armory *b = context;
    return b->host.font ? b->host.font(b->host.ctx, global) : NULL;
}
static const es2_bitmap *resolve_bitmap(void *context, const es2_ui_layout_ref *ref) {
    (void)context;
    (void)ref; /* none of the three screens names a bitmap the builder makes */
    return NULL;
}
static const char *resolve_text(void *context, const es2_ui_layout_ref *ref) {
    es2_build_armory *b = context;
    switch (ref->kind) {
    case ES2_UI_REF_ESTEXT: return text_of(b, (uint32_t)ref->a);
    case ES2_UI_REF_EXE_STRING:
        /* the literals of the three constructors (0x478775..0x47877c,
         * 0x4791cc..0x4791e0): "1" (0x4791ce), "4" (0x4791d0), the empty
         * bank mark (0x4791da); the others are empty */
        switch (ref->a) {
        case 0x4791ce: return "1";
        case 0x4791d0: return "4";
        case 0x4791da: return "[   ]";
        default: return "";
        }
    case ES2_UI_REF_GLOBAL: {
        /* the ARMORY name column: the name pointer +0x10 of the weapon
         * record 0x483cc8 + 0x1d c, set at the table load from
         * <lang>\weapons.bin entry c (0x41267b..0x412690) */
        int32_t off = ref->a - 0x483cd8;
        if (off >= 0 && off % 0x1d == 0 && off / 0x1d <= 32 && b->host.weapon_name)
            return b->host.weapon_name(b->host.ctx, off / 0x1d);
        return NULL;
    }
    default: return NULL; /* the cost column (sprintf) is written after the build */
    }
}

static void on_control(es2_ui *ui, es2_ui_id window, const es2_gui_event *ev);
static void on_empty(es2_ui *ui, es2_ui_id window, const es2_gui_event *ev) {
    (void)ui;
    (void)window;
    (void)ev; /* 0x44a63b, 0x44a694: empty bodies (vshell_ui.md 17.4) */
}

static es2_ui_handler resolve_handler(void *context, uint32_t address) {
    (void)context;
    if (address == ES2_ARMORY_ON_PLUS || address == ES2_ARMORY_ON_MINUS) return on_empty;
    if (address >= ES2_BUILD_ON_ROW0 && address <= ES2_BUILD_ON_ROW0 + 8 * 0x61 && (address - ES2_BUILD_ON_ROW0) % 0x61 == 0)
        return on_control;
    if (address >= ES2_ARMORY_ON_ROW0 && address <= ES2_ARMORY_ON_ROW0 + 25 * 0x74 &&
        (address - ES2_ARMORY_ON_ROW0) % 0x74 == 0)
        return on_control;
    switch (address) {
    case ES2_BUILD_ON_SCRAP: case ES2_BUILD_ON_BUILD: case ES2_ARMORY_ON_CLEAR: case ES2_ARMORY_ON_SCRAP:
    case ES2_WEAPON_SCRAP_ON_CANCEL: case ES2_WEAPON_SCRAP_ON_ACCEPT:
        return on_control;
    default: return NULL;
    }
}

void es2_build_armory_init(es2_build_armory *b, es2_shell_flow *flow, es2_build_armory_game *game,
                           const es2_build_armory_host *host) {
    if (!b) return;
    memset(b, 0, sizeof *b);
    b->flow = flow;
    b->game = game;
    if (host) b->host = *host;
    b->box_category = -1;
}

static int build_screen(es2_build_armory *b, es2_ui *ui, int screen, es2_ui_id *ids, int n) {
    es2_ui_resolver r;
    if (!ui || !b) return -1;
    if (b->ui && b->ui != ui) return -1;
    b->ui = ui;
    r.context = b;
    r.font = resolve_font;
    r.bitmap = resolve_bitmap;
    r.text = resolve_text;
    r.handler = resolve_handler;
    memset(ids, 0, (size_t)n * sizeof ids[0]);
    if (es2_ui_layout_build(ui, screen, &r, ids, n) < 0) return -1;
    for (int k = 0; k < n; k++) {
        es2_ui_widget *w = es2_ui_get(ui, ids[k]);
        if (w) w->user = b;
    }
    return 0;
}

/* Original: VSHELL.EXE 0x445de8 */
int es2_screen_build_build(es2_ui *ui, void *ctx) {
    es2_build_armory *b = ctx;
    if (build_screen(b, ui, ES2_UI_SCREEN_BUILD, b ? b->build_ids : NULL, ES2_BUILD_CONTROLS)) return -1;
    for (int k = 0; k < ES2_BUILD_CONTROLS; k++)
        if (k != 1 && !b->build_ids[k]) return -1;
    return 0;
}

/* The cost column of the ARMORY rows: "%d" of the cost word / 1000 (the
 * constructor's 0x449847..0x449863). */
static void armory_costs(es2_build_armory *b, const uint16_t cost[33]) {
    char t[16];
    for (int r = 0; r < ES2_SHELL_ARMORY_ROWS; r++) {
        int16_t c = row_category(b, r);
        es2_ui_id col = column(b, b->armory_ids[ES2_ARMORY_ROW + 5 * r], 3);
        es2_ui_widget *w = W(b, col);
        if (!w || c < 0 || c > 32) continue;
        snprintf(t, sizeof t, "%d", (int)(cost[c] / 1000));
        (void)es2_ui_message_set(b->ui, col, t, w->align, w->text_colour, 0);
    }
}

/* Original: VSHELL.EXE 0x4484c4; the images 0x448712..0x448853: per record
 * of the first list of gam\arm_weap.dat (a word count; records of a word
 * item ID, a dword x, a dword y and a word frame), an ESBitmap under the
 * ESArm with (x, y, x + frame +6, y + frame +4), colour 0x27, frame off,
 * kept by the ID's row (0x43fd87). */
int es2_screen_armory_build(es2_ui *ui, void *ctx) {
    es2_build_armory *b = ctx;
    es2_span f;
    uint16_t cost[33];
    if (build_screen(b, ui, ES2_UI_SCREEN_ARMORY, b ? b->armory_ids : NULL, ES2_ARMORY_CONTROLS)) return -1;
    for (int k = 0; k < ES2_ARMORY_CONTROLS; k++)
        if (k != ES2_ARMORY_IMAGE && !b->armory_ids[k]) return -1;
    memset(b->images, 0, sizeof b->images);
    f = file_of(b, "gam\\arm_weap.dat");
    if (f.data && f.size >= 2) {
        int count = s16le(f.data);
        for (int i = 0; i < count && 2 + 12 * (size_t)(i + 1) <= f.size; i++) {
            const uint8_t *p = f.data + 2 + 12 * (size_t)i;
            int slot = slot_of(b, s16le(p));
            const es2_bitmap *bm = member(b, "dba\\arm_weap.dba", s16le(p + 10));
            es2_gui_rect rect;
            es2_ui_id id;
            rect.left = s32le(p + 2);
            rect.top = s32le(p + 6);
            rect.right = rect.left + (bm ? bm->width : 0);
            rect.bottom = rect.top + (bm ? bm->height : 0);
            id = es2_ui_bitmap(ui, b->armory_ids[ES2_ARMORY_ARM], rect, bm, NULL, 0x27);
            if (!id) return -1;
            W(b, id)->frame = 0;
            W(b, id)->flags = (W(b, id)->flags & ~ES2_UI_PENDING) | ES2_UI_HIDDEN;
            if (slot >= 0) b->images[slot] = id; /* a later record of the same row replaces it */
        }
    }
    /* The costs come from the start-up table, not from a loaded game
     * (the column stayed empty when the screens were built without one) */
    load_costs(b->game ? b->game->vfs : NULL, cost);
    armory_costs(b, cost);
    return 0;
}

/* Original: VSHELL.EXE 0x447e3e */
int es2_screen_weapon_scrap_box_build(es2_ui *ui, void *ctx) {
    es2_build_armory *b = ctx;
    if (build_screen(b, ui, ES2_UI_SCREEN_WEAPON_SCRAP_BOX, b ? b->scrap_ids : NULL, ES2_WEAPON_SCRAP_CONTROLS))
        return -1;
    for (int k = 0; k < ES2_WEAPON_SCRAP_CONTROLS; k++)
        if (!b->scrap_ids[k]) return -1;
    return 0;
}

/* ---- BUILD ----------------------------------------------------------------------- */

static es2_ui_id build_row(es2_build_armory *b, int t) { return b->build_ids[ES2_BUILD_ROW + 5 * t]; }
static es2_ui_id build_grid(es2_build_armory *b, int t) { return b->build_ids[ES2_BUILD_GRID + t]; }

/* Original: VSHELL.EXE 0x446ec5: rows with a nine-word bank entry: the four
 * columns' colour 0x27, enabled, drawn; the others disabled, colour 0x10. */
static void build_rows(es2_build_armory *b, const numbers *n) {
    for (int t = 0; t < ES2_BUILD_TYPES; t++) {
        es2_ui_id q = build_row(b, t);
        if (!W(b, q)) continue;
        if (n->nine[t]) {
            quad_colours(b, q, 0x27);
            W(b, q)->enabled = 1;
            es2_ui_draw(b->ui, q);
        } else {
            W(b, q)->enabled = 0;
            quad_colours(b, q, 0x10);
        }
    }
}

/* Original: VSHELL.EXE 0x447501: (salvage - bank cost) / 1000, unsigned, "%ld %s" with ESTEXT 0xc6. */
static void build_available(es2_build_armory *b) {
    char t[ES2_UI_TEXT];
    snprintf(t, sizeof t, "%ld %s", (long)((b->view.salvage - b->view.bank_cost) / 1000u), text_of(b, 0xc6));
    own_text(b, b->build_ids[ES2_BUILD_AVAILABLE], t);
}

/* Original: VSHELL.EXE 0x44738a: the selected type's HERC_INF +2, +4, +6, +8,
 * then 0x447501. VSHELL reads the record before HERC_INF while the type is
 * still -1 (the first show); here the four labels are then left as they are
 * (own choice: the type-0 selection follows at once and rewrites them). */
static void build_texts(es2_build_armory *b, const numbers *n, int type) {
    char t[ES2_UI_TEXT];
    if (type >= 0 && type < ES2_BUILD_TYPES) {
        snprintf(t, sizeof t, "%d %s", n->herc[type][1], text_of(b, 0xc6));
        own_text(b, b->build_ids[ES2_BUILD_TONS], t);
        snprintf(t, sizeof t, "%d %s", n->herc[type][2], text_of(b, 0xc7));
        own_text(b, b->build_ids[ES2_BUILD_SPEED], t);
        snprintf(t, sizeof t, "%d", n->herc[type][3]);
        own_text(b, b->build_ids[ES2_BUILD_WORD6], t);
        snprintf(t, sizeof t, "%d %s", n->herc[type][4], text_of(b, 0xc6));
        own_text(b, b->build_ids[ES2_BUILD_COST], t);
    }
    build_available(b);
}

/* Original: VSHELL.EXE 0x447064 (vshell_ui.md 12.4/12.5): a bay without a
 * record: Scrap off, Build by the strict unsigned cost rule; with a record:
 * Scrap by 0x411101 and the vehicle's bank entry, Build off. Without a bay
 * selection both are off (VSHELL reads hangar[-1]; editors.h). */
void es2_build_refresh(es2_build_armory *b) {
    if (!b || !b->ui) return;
    if (!b->view.has_record) {
        button_state(b, b->build_ids[ES2_BUILD_SCRAP], 0);
        button_state(b, b->build_ids[ES2_BUILD_BUILD], b->view.build_enabled);
    } else {
        button_state(b, b->build_ids[ES2_BUILD_SCRAP], b->view.scrap_enabled);
        button_state(b, b->build_ids[ES2_BUILD_BUILD], 0);
    }
}

/* Original: VSHELL.EXE 0x415dc1: per type, the layers of gam\rpr_<herc>.dat
 * (a word count; records of a word layer, dword x, dword y, word frame,
 * word mirror) into the type's ESGrid with the frame of dba\rpr_<herc>.dba;
 * each layer's first recolour pair 0x0e -> 0x0e. */
static void build_pictures(es2_build_armory *b) {
    for (int t = 0; t < ES2_BUILD_TYPES; t++) {
        es2_span f = file_of(b, es2_build_picture_file(t, 0));
        es2_ui_layer layer;
        int count;
        if (!f.data || f.size < 2) continue;
        count = s16le(f.data);
        memset(&layer, 0, sizeof layer); /* 0x40bd58 */
        for (int i = 0; i < 10; i++) {
            layer.from[i] = 0x0e;
            layer.to[i] = 0x10;
        }
        for (int i = 0; i < count && 2 + 14 * (size_t)(i + 1) <= f.size; i++) {
            const uint8_t *p = f.data + 2 + 14 * (size_t)i;
            int index = s16le(p);
            layer.x = s32le(p + 2);
            layer.y = s32le(p + 6);
            layer.bitmap = member(b, es2_build_picture_file(t, 1), s16le(p + 10));
            layer.mirror = u16le(p + 12); /* VSHELL reads a dword whose high word the loader leaves unset (H) */
            layer.from[0] = 0x0e;
            if (index < 0 || index >= ES2_UI_GRID_LAYERS) {
                b->ui->refused++; /* 0x415ec4: VSHELL asserts */
                continue;
            }
            (void)es2_ui_grid_set_layer(b->ui, build_grid(b, t), index, &layer);
            b->ui->layers[W(b, build_grid(b, t))->grid][index].to[0] = 0x0e; /* 0x415f17 */
        }
    }
}

/* Original: VSHELL.EXE 0x4472cb(type) without its library part: the old
 * row's column 0 back to 0x27, its frame 0x10, its grid hidden; the new
 * row's column 0 and frame 0x29, its grid shown; then 0x447064, 0x44738a. */
static void build_select_ui(es2_build_armory *b, const numbers *n, int old, int type) {
    if (old == type) return;
    if (old >= 0 && old < ES2_BUILD_TYPES) {
        es2_ui_quad_set_colour(b->ui, build_row(b, old), 0, 0x27);
        W(b, build_row(b, old))->colour = 0x10;
        es2_ui_redraw_tree(b->ui, build_row(b, old));
        es2_ui_hide(b->ui, build_grid(b, old));
    }
    if (type >= 0 && type < ES2_BUILD_TYPES) {
        es2_ui_quad_set_colour(b->ui, build_row(b, type), 0, 0x29);
        W(b, build_row(b, type))->colour = 0x29;
        es2_ui_redraw_tree(b->ui, build_row(b, type));
        es2_ui_show(b->ui, build_grid(b, type));
    }
    es2_build_refresh(b);
    build_texts(b, n, type);
}

/* Original: VSHELL.EXE 0x446f9d */
es2_status es2_build_show(es2_build_armory *b) {
    numbers n;
    int old;
    if (!b || !b->ui || !has_game(b)) return ES2_ERR_ARGUMENT;
    old = b->game->session->herc_type;
    if (b->game->session->screen != ES2_SHELL_SCREEN_BUILD) {
        if (apply(b, ES2_SHELL_EDITOR_OPEN, ES2_SHELL_SCREEN_BUILD, building(b)) < 0) return b->last_status;
    } else
        view_now(b);
    if (load_numbers(b->game, &n)) return ES2_ERR_ARGUMENT;
    build_rows(b, &n);
    build_texts(b, &n, old);
    /* 0x447064 with the old type (the view's cost is the new type's: the
     * same final state, see build_select_ui) */
    es2_build_refresh(b);
    build_pictures(b);
    es2_ui_show(b->ui, b->build_ids[ES2_BUILD_PANEL]);
    es2_ui_show(b->ui, b->build_ids[ES2_BUILD_LOWER]);
    es2_ui_show(b->ui, build_grid(b, 0));
    build_select_ui(b, &n, old, b->game->session->herc_type);
    return ES2_OK;
}

/* Original: VSHELL.EXE 0x446fde: 0x415f4c (every grid's layers off), then
 * hide [0x48d968], [0x48d9bc], [0x48d96c + 4 type]. */
void es2_build_hide(es2_build_armory *b) {
    int t;
    if (!b || !b->ui) return;
    for (t = 0; t < ES2_BUILD_TYPES; t++) es2_ui_grid_clear(b->ui, build_grid(b, t));
    es2_ui_hide(b->ui, b->build_ids[ES2_BUILD_PANEL]);
    es2_ui_hide(b->ui, b->build_ids[ES2_BUILD_LOWER]);
    t = has_game(b) ? b->game->session->herc_type : -1;
    if (t >= 0 && t < ES2_BUILD_TYPES) es2_ui_hide(b->ui, build_grid(b, t));
}

es2_status es2_build_select_bay(es2_build_armory *b, int16_t bay) {
    if (!b || !b->ui || !has_game(b)) return ES2_ERR_ARGUMENT;
    /* with the session shared with the service bay the selection may be
     * stored already (the service bay writes 0x482bc9 before its host call) */
    if (b->game->session->bay == bay) view_now(b);
    else if (apply(b, ES2_SHELL_EDITOR_BAY, bay, 0) < 0) return b->last_status;
    if (b->game->session->screen == ES2_SHELL_SCREEN_BUILD) es2_build_refresh(b);
    return ES2_OK;
}

static int build_type(es2_build_armory *b, int t, uint32_t handler) {
    numbers n;
    int old = has_game(b) ? b->game->session->herc_type : -1;
    int applied = apply(b, ES2_SHELL_EDITOR_BUILD_TYPE, (int16_t)t, 0);
    if (applied > 0 && !load_numbers(b->game, &n)) build_select_ui(b, &n, old, t);
    return note(b, handler, applied);
}

/* Original: VSHELL.EXE 0x4475ce: 0x40eed4(type), the squad panel's
 * 0x43e0d7, 0x43e167, 0x43da0c, then 0x447501 and 0x447064. */
static int build_build(es2_build_armory *b) {
    int applied = apply(b, ES2_SHELL_EDITOR_BUILD, 0, 0);
    if (applied >= 0) {
        if (b->host.squad) b->host.squad(b->host.ctx);
        build_available(b);
        es2_build_refresh(b);
    }
    return note(b, ES2_BUILD_ON_BUILD, applied);
}

/* ---- ARMORY ---------------------------------------------------------------------- */

static es2_ui_id armory_row(es2_build_armory *b, int r) { return b->armory_ids[ES2_ARMORY_ROW + 5 * r]; }

static void bank_mark(char *t, size_t size, int16_t k) {
    if (k) snprintf(t, size, "[ %1d ]", k); /* 0x4791ff / 0x479849 */
    else snprintf(t, size, "[   ]");        /* 0x479207 / 0x479843 */
}

/* Original: VSHELL.EXE 0x4499b9: rows whose category flag (+0x16) is set:
 * enabled, column 0 the bank mark and column 2 the item count (colour
 * 0x27, drawn), the four colours 0x27; the others disabled, colour 0x10. */
static void armory_rows(es2_build_armory *b, const numbers *n) {
    char t[16];
    for (int r = 0; r < ES2_SHELL_ARMORY_ROWS; r++) {
        es2_ui_id q = armory_row(b, r);
        int16_t c = row_category(b, r);
        if (!W(b, q) || c < 0 || c > 32) continue;
        if (n->flag[c]) {
            W(b, q)->enabled = 1;
            bank_mark(t, sizeof t, in_bank(n, c));
            (void)es2_ui_message_set(b->ui, column(b, q, 0), t, 0, 0x27, 1);
            snprintf(t, sizeof t, "%d", n->count[c]);
            (void)es2_ui_message_set(b->ui, column(b, q, 2), t, 1, 0x27, 1);
            quad_colours(b, q, 0x27);
        } else {
            W(b, q)->enabled = 0;
            quad_colours(b, q, 0x10);
        }
    }
}

/* Original: VSHELL.EXE 0x44a33b: the "+" label (salvage - bank cost, "%ld
 * %s" with ESTEXT 0xc8), the "-" label (bank cost, "%d %s"), the free count
 * and 5 - free ("%d"), all colour 0x29 centred; with a row selected its item
 * count (column 2, right, 0x29) and the row's tree redraw. */
static void armory_texts(es2_build_armory *b, const numbers *n) {
    char t[ES2_UI_TEXT];
    int row = has_game(b) ? b->game->session->armory_row : -1;
    es2_ui_widget *w;
    snprintf(t, sizeof t, "%ld %s", (long)(int32_t)(b->view.salvage - b->view.bank_cost), text_of(b, 0xc8));
    if ((w = W(b, b->armory_ids[ES2_ARMORY_AVAILABLE]))) (void)es2_ui_message_set(b->ui, w->label, t, 2, 0x29, 1);
    snprintf(t, sizeof t, "%d %s", (int)(int32_t)b->view.bank_cost, text_of(b, 0xc8));
    if ((w = W(b, b->armory_ids[ES2_ARMORY_BANK]))) (void)es2_ui_message_set(b->ui, w->label, t, 2, 0x29, 1);
    snprintf(t, sizeof t, "%d", n->free_count);
    (void)es2_ui_message_set(b->ui, b->armory_ids[ES2_ARMORY_FREE], t, 2, 0x29, 1);
    snprintf(t, sizeof t, "%d", 5 - n->free_count);
    (void)es2_ui_message_set(b->ui, b->armory_ids[ES2_ARMORY_USED], t, 2, 0x29, 1);
    if (row >= 0 && row < ES2_SHELL_ARMORY_ROWS) {
        int16_t c = row_category(b, row);
        snprintf(t, sizeof t, "%d", c >= 0 && c <= 32 ? n->count[c] : 0);
        (void)es2_ui_message_set(b->ui, column(b, armory_row(b, row), 2), t, 1, 0x29, 1);
        es2_ui_redraw_tree(b->ui, armory_row(b, row));
    }
}

/* Original: VSHELL.EXE 0x449df3..0x44a061 (also 0x44a0c2..0x44a32f): the
 * selection's screen side. The old row's columns 0x27, drawn, its image
 * hidden; the new row's columns 0x29, drawn, its image shown; the five
 * wpn_info.bin lines 5 slot .. 5 slot + 4 of the row's item ID; the ESArm's
 * tree redraw. */
static void armory_select_ui(es2_build_armory *b, int old, int row) {
    int slot;
    if (old >= 0 && old < ES2_SHELL_ARMORY_ROWS) {
        quad_colours(b, armory_row(b, old), 0x27);
        es2_ui_draw(b->ui, armory_row(b, old));
        if (b->images[old]) es2_ui_hide(b->ui, b->images[old]);
    }
    quad_colours(b, armory_row(b, row), 0x29);
    es2_ui_draw(b->ui, armory_row(b, row));
    if (b->images[row]) es2_ui_show(b->ui, b->images[row]);
    slot = slot_of(b, row_category(b, row));
    for (int k = 0; k < 5; k++) {
        const char *t = b->host.weapon_info ? b->host.weapon_info(b->host.ctx, 5 * slot + k) : NULL;
        own_text(b, b->armory_ids[ES2_ARMORY_INFO + k], t ? t : "");
    }
    es2_ui_redraw_tree(b->ui, b->armory_ids[ES2_ARMORY_ARM]);
}

/* Original: VSHELL.EXE 0x449d2f(row) and 0x44a06e(row) (left = 1): with
 * manual building on the selected row the bank edit (column 0's mark,
 * colour 0x29, and 0x44a33b), else the selection. */
static int armory_press_row(es2_build_armory *b, int row, int left, uint32_t handler) {
    numbers n;
    int old, applied, edit;
    if (!has_game(b)) return note(b, handler, -1);
    old = b->game->session->armory_row;
    edit = building(b) != 0 && old == row;
    applied = apply(b, ES2_SHELL_EDITOR_ARMORY_ROW, (int16_t)row, (int16_t)(left ? 1 : 0));
    if (applied < 0 || load_numbers(b->game, &n)) return note(b, handler, -1);
    if (edit) {
        if (applied > 0 || !left) {
            char t[16];
            bank_mark(t, sizeof t, in_bank(&n, row_category(b, row)));
            (void)es2_ui_message_set(b->ui, column(b, armory_row(b, row), 0), t, 0, 0x29, 1);
            armory_texts(b, &n);
        }
    } else if (applied > 0)
        armory_select_ui(b, old, row);
    return note(b, handler, applied);
}

/* Original: VSHELL.EXE 0x449ccc (Clear 0x44a584): with a row selected,
 * 0x412c31, column 0 "[   ]" (0x29) and 0x44a33b. */
static int armory_clear(es2_build_armory *b) {
    numbers n;
    int row = has_game(b) ? b->game->session->armory_row : -1, applied;
    if (row < 0) return note(b, ES2_ARMORY_ON_CLEAR, 0);
    applied = apply(b, ES2_SHELL_EDITOR_ARMORY_CLEAR, 0, 0);
    if (applied < 0 || load_numbers(b->game, &n)) return note(b, ES2_ARMORY_ON_CLEAR, -1);
    (void)es2_ui_message_set(b->ui, column(b, armory_row(b, row), 0), "[   ]", 0, 0x29, 1);
    armory_texts(b, &n);
    return note(b, ES2_ARMORY_ON_CLEAR, applied);
}

/* Original: VSHELL.EXE 0x449b87 */
es2_status es2_armory_show(es2_build_armory *b) {
    numbers n;
    int old;
    if (!b || !b->ui || !has_game(b)) return ES2_ERR_ARGUMENT;
    old = b->game->session->armory_row;
    if (b->game->session->screen != ES2_SHELL_SCREEN_ARMORY) {
        if (apply(b, ES2_SHELL_EDITOR_OPEN, ES2_SHELL_SCREEN_ARMORY, building(b)) < 0) return b->last_status;
    } else
        view_now(b);
    if (load_numbers(b->game, &n)) return ES2_ERR_ARGUMENT;
    button_state(b, b->armory_ids[ES2_ARMORY_CLEAR], building(b) != 0);
    armory_rows(b, &n);
    {
        /* 0x44a33b runs before 0x449d2f(0): the row is still the old one */
        int16_t now = b->game->session->armory_row;
        b->game->session->armory_row = (int16_t)old;
        armory_texts(b, &n);
        b->game->session->armory_row = now;
    }
    es2_ui_show(b->ui, b->armory_ids[ES2_ARMORY_PANEL]);
    es2_ui_show(b->ui, b->armory_ids[ES2_ARMORY_LIST]);
    if (b->game->session->armory_row != old || old != 0) armory_select_ui(b, old, b->game->session->armory_row);
    return ES2_OK;
}

/* Original: VSHELL.EXE 0x449c46: hide [0x48da64 + 4 row] (row -1 names
 * 0x48da60, the ESArm), the two panels, row := -1. */
void es2_armory_hide(es2_build_armory *b) {
    int row;
    if (!b || !b->ui) return;
    row = has_game(b) ? b->game->session->armory_row : -1;
    if (row >= 0 && row < ES2_SHELL_ARMORY_ROWS) {
        if (b->images[row]) es2_ui_hide(b->ui, b->images[row]);
    } else if (row == -1)
        es2_ui_hide(b->ui, b->armory_ids[ES2_ARMORY_ARM]);
    es2_ui_hide(b->ui, b->armory_ids[ES2_ARMORY_PANEL]);
    es2_ui_hide(b->ui, b->armory_ids[ES2_ARMORY_LIST]);
    if (b->game) b->game->session->armory_row = -1;
}

/* ---- the weapons scrap warning ---------------------------------------------------- */

/* Original: VSHELL.EXE 0x44a508 -> 0x448227(category): 0x48da50, the label
 * "%d %s" (0x412c8e, ESTEXT 0xcd), the shield shown, the box shown modally. */
static int armory_scrap(es2_build_armory *b) {
    char t[ES2_UI_TEXT];
    int row = has_game(b) ? b->game->session->armory_row : -1;
    if (row < 0) return note(b, ES2_ARMORY_ON_SCRAP, 0);
    view_now(b);
    b->box_category = row_category(b, row);
    snprintf(t, sizeof t, "%d %s", (int)(b->view.armory_scrap_value / 1000), text_of(b, 0xcd));
    own_text(b, b->scrap_ids[ES2_WEAPON_SCRAP_NUMBER], t);
    es2_ui_show(b->ui, b->scrap_ids[ES2_WEAPON_SCRAP_SHIELD]);
    es2_ui_modal_show(b->ui, b->scrap_ids[ES2_WEAPON_SCRAP_ALERT]);
    return note(b, ES2_ARMORY_ON_SCRAP, 1);
}

/* Original: VSHELL.EXE 0x4482ad */
static void box_hide(es2_build_armory *b) {
    es2_ui_hide(b->ui, b->scrap_ids[ES2_WEAPON_SCRAP_SHIELD]);
    es2_ui_modal_hide(b->ui, b->scrap_ids[ES2_WEAPON_SCRAP_ALERT]);
}

/* Original: VSHELL.EXE 0x448447: hide, 0x40ed6a(category) and 0x412a37 (the
 * library's scrap), rows 0x4499b9, texts 0x44a33b. */
static int box_accept(es2_build_armory *b) {
    numbers n;
    int applied;
    box_hide(b);
    applied = apply(b, ES2_SHELL_EDITOR_ARMORY_SCRAP, 0, 0);
    if (applied >= 0 && !load_numbers(b->game, &n)) {
        armory_rows(b, &n);
        armory_texts(b, &n);
    }
    return note(b, ES2_WEAPON_SCRAP_ON_ACCEPT, applied);
}

/* ---- dispatch ------------------------------------------------------------------- */

int es2_build_armory_press(es2_build_armory *b, uint32_t handler, int click) {
    if (!b || !b->ui) return -1;
    if (handler >= ES2_BUILD_ON_ROW0 && handler <= ES2_BUILD_ON_ROW0 + 8 * 0x61 && (handler - ES2_BUILD_ON_ROW0) % 0x61 == 0)
        return build_type(b, (int)((handler - ES2_BUILD_ON_ROW0) / 0x61), handler);
    if (handler >= ES2_ARMORY_ON_ROW0 && handler <= ES2_ARMORY_ON_ROW0 + 25 * 0x74 &&
        (handler - ES2_ARMORY_ON_ROW0) % 0x74 == 0)
        /* 0x44a75a: button byte 1 -> 0x449d2f, else 0x44a06e */
        return armory_press_row(b, (int)((handler - ES2_ARMORY_ON_ROW0) / 0x74), click == ES2_GUI_MOUSE_LEFT_UP, handler);
    switch (handler) {
    case ES2_BUILD_ON_BUILD: return build_build(b);
    case ES2_BUILD_ON_SCRAP: /* 0x447570 -> 0x447da1 */
        if (b->host.herc_scrap) b->host.herc_scrap(b->host.ctx);
        return note(b, handler, 0);
    case ES2_ARMORY_ON_CLEAR: return armory_clear(b);
    case ES2_ARMORY_ON_SCRAP: return armory_scrap(b);
    case ES2_WEAPON_SCRAP_ON_CANCEL: /* 0x4483e9 */
        box_hide(b);
        return note(b, handler, 0);
    case ES2_WEAPON_SCRAP_ON_ACCEPT: return box_accept(b);
    case ES2_ARMORY_ON_PLUS: case ES2_ARMORY_ON_MINUS: return note(b, handler, 0);
    default: return -1;
    }
}

static void on_control(es2_ui *ui, es2_ui_id window, const es2_gui_event *ev) {
    es2_ui_widget *w = es2_ui_get(ui, window);
    es2_build_armory *b = w ? (es2_build_armory *)w->user : NULL;
    if (b) (void)es2_build_armory_press(b, w->original_handler, ev ? ev->code : 0);
}
