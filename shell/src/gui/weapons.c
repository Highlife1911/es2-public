/* The WEAPONS screen 0x43ebbc (es2/shell/gui/weapons.h).
 *
 * Contract: docs/reverse-engineering/vshell_ui.md sections 4.7, 10.2, 17.1
 * and 29. Original: VSHELL.EXE: the constructor
 * 0x43ebbc..0x43fbd7, show 0x43fbd8, hide 0x43fc62, the guidance panel
 * 0x43fc87 / 0x43fcd4, the row lookup 0x43fd87, the row selection 0x43fdac,
 * the list refresh 0x440256, the guidance 0x4403f9, the handlers
 * 0x440626..0x4414f5, the slot selection 0x43e242 with prev/next 0x43e3d9 /
 * 0x43e399, the mount 0x43e2d4 and the store 0x43e346. The state changes
 * are the service bay's (es2_shell_service_bay through es2_shell_engine_bay).
 * */
#include "es2/shell/gui/weapons.h"
#include "es2/shell/bytes.h"

#include <stdio.h>
#include <string.h>

#include "es2/formats/sav.h"

/* ---- the memory state ---------------------------------------------------------- */

/* What the handlers read of the hangar and the inventory. */
typedef struct state {
    int loaded;
    es2_shell_bay_view v;   /* bays, and the 27 rows' items, counts and availability bytes */
    int finished[8];        /* 0x411088: a record whose word +0x4a (SAV fixed +70) is 100 */
} state;

static int32_t le32(const uint8_t *p) {
    return (int32_t)((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24);
}

static void read_state(const es2_weapons_screen *ws, state *st) {
    const es2_shell_engine *g = ws->engine;
    es2_sav s;
    memset(st, 0, sizeof *st);
    if (!g || !g->memory.data || es2_shell_engine_bay_view(g, 0, 0, &st->v, NULL) != ES2_OK) return;
    if (es2_sav_parse((es2_span){g->memory.data, g->memory.size}, &s, NULL) != ES2_OK) return;
    for (size_t i = 0; i < s.loadout_count; i++) {
        es2_sav_loadout l;
        if (es2_sav_loadout_at(&s, i, &l, NULL) != ES2_OK) return;
        if (l.index >= 0 && l.index < 8 && l.fixed.size >= 72) st->finished[l.index] = es2_le16s(l.fixed.data + 70) == 100;
    }
    st->loaded = 1;
}

static int16_t bay_of(const es2_weapons_screen *ws) { return ws->bay ? *ws->bay : ws->own_bay; }
/* [0x482ba7 + 4 bay] != 0. A selection of -1 reads the word before the
 * hangar (a pilot pointer): treated as no record (own). */
static int has_record(const state *st, int16_t bay) { return st->loaded && bay >= 0 && bay < 8 && st->v.present[bay]; }
static int16_t row_id(const es2_weapons_screen *ws, int row) {
    return ws->engine && ws->engine->tables && row >= 0 && row < ES2_WEAPONS_ROWS ? ws->engine->tables->rows[row] : 0;
}
/* The slot item's ID (record +0x50 + 4 slot, word +0): 0 empty, -1 without a record. */
static int16_t slot_item(const es2_weapons_screen *ws, const state *st) {
    int16_t bay = bay_of(ws);
    if (!has_record(st, bay)) return -1;
    return ws->slot >= 0 && ws->slot < 10 ? st->v.item[bay][ws->slot] : 0;
}
/* Its word +8 (the guidance mode): 5 for an empty slot, -1 without a record. */
static int16_t slot_word(const es2_weapons_screen *ws, const state *st) {
    int16_t bay = bay_of(ws), item = slot_item(ws, st);
    if (item < 0) return -1;
    return item == 0 ? 5 : (int16_t)st->v.secondary[bay][ws->slot];
}
static int row_of_id(const state *st, int16_t id) { /* 0x43fd87 */
    for (int r = 0; r < ES2_WEAPONS_ROWS; r++)
        if (st->v.row_item[r] == id) return r;
    return -1;
}
static int item_ok(int16_t id) { return id >= 0 && id < 33; }
static int available(const state *st, int16_t id) { /* availability byte +0x16 (0x483cde + 0x1d ID) */
    for (int r = 0; r < ES2_WEAPONS_ROWS; r++)
        if (st->v.row_item[r] == id) return st->v.row_available[r];
    return 0;
}
static int count_of(const state *st, int16_t id) { /* the list count +0x17 */
    for (int r = 0; r < ES2_WEAPONS_ROWS; r++)
        if (st->v.row_item[r] == id) return st->v.row_count[r];
    return 0;
}
/* Original: VSHELL.EXE 0x41501f */
static int admits(const es2_weapons_screen *ws, const state *st, int16_t id) {
    int16_t bay = bay_of(ws);
    if (ws->slot == -1) return 1;
    if (!has_record(st, bay) || !item_ok(id)) return 0; /* 0x411164 = -1: a table before 0x484702 (own: no) */
    return es2_shell_engine_admits(ws->engine, st->v.vehicle[bay], id, ws->slot) && available(st, id);
}

/* ---- windows ------------------------------------------------------------------- */

static es2_ui_id id_of(const es2_weapons_screen *ws, int control) { return ws->ids[control]; }
/* [0x48d680 + 4 k] */
static es2_ui_id pointer(const es2_weapons_screen *ws, int k) {
    if (k == -1) return ws->ids[ES2_WEAPONS_ARM];
    return k >= 0 && k < ES2_WEAPONS_POINTERS ? ws->pointers[k] : 0;
}
static es2_ui_id row_window(const es2_weapons_screen *ws, int row) { return id_of(ws, ES2_WEAPONS_ROW0 + 5 * row); }

static const char *estext(const es2_weapons_screen *ws, uint32_t id) {
    return ws->host.text ? ws->host.text(ws->host.ctx, id) : NULL;
}
static const char *desc(const es2_weapons_screen *ws, uint32_t index) {
    return ws->host.desc ? ws->host.desc(ws->host.ctx, index) : NULL;
}
static const char *literal(const es2_weapons_screen *ws, uint32_t address) {
    return ws->host.literal ? ws->host.literal(ws->host.ctx, address) : NULL;
}
/* 0x40b8bb(label, text, label +0x45, label +0xb5, 1) */
static void set_text(es2_weapons_screen *ws, es2_ui_id label, const char *text) {
    es2_ui_widget *l = es2_ui_get(ws->ui, label);
    if (l && es2_ui_message_set(ws->ui, label, text, l->align, l->text_colour, 1) < 0) ws->refused++;
}
/* The three description lines [0x48d710], [0x48d714], [0x48d718] := wpn_desc first .. first + 2. */
static void set_lines(es2_weapons_screen *ws, uint32_t first) {
    for (int k = 0; k < 3; k++) set_text(ws, id_of(ws, ES2_WEAPONS_TEXT0 + k), desc(ws, first + (uint32_t)k));
}
/* The four columns' +0xb5 (0x40ab3b or the direct stores). */
static void row_colours(es2_weapons_screen *ws, int row, int32_t colour) {
    for (int c = 0; c < 4; c++) es2_ui_quad_set_colour(ws->ui, row_window(ws, row), c, colour);
}
static void set_frame_colour(es2_weapons_screen *ws, es2_ui_id id, int32_t colour) {
    es2_ui_widget *w = es2_ui_get(ws->ui, id);
    if (w) w->colour = colour;
}
static int hidden(es2_weapons_screen *ws, es2_ui_id id) {
    es2_ui_widget *w = es2_ui_get(ws->ui, id);
    return !w || (w->flags & ES2_UI_HIDDEN);
}
/* The guidance buttons by mode (0x4403f9's two switches): SARH 0, ARH 1, ARM 2, EO 3. */
static es2_ui_id mode_button(const es2_weapons_screen *ws, int32_t mode) {
    static const int control[4] = {ES2_WEAPONS_GUIDE_SARH, ES2_WEAPONS_GUIDE_ARH, ES2_WEAPONS_GUIDE_ARM,
                                   ES2_WEAPONS_GUIDE_EO};
    return mode >= 0 && mode < 4 ? ws->ids[control[mode]] : 0;
}

/* ---- the list, the panel, the guidance ------------------------------------------ */

/* Original: VSHELL.EXE 0x440256 */
static void refresh_list(es2_weapons_screen *ws, const state *st) {
    for (int b = 0; b < ES2_WEAPONS_ROWS; b++) {
        int16_t id = st->v.row_item[b];
        es2_ui_id row = row_window(ws, b);
        es2_ui_widget *w = es2_ui_get(ws->ui, row);
        char count[16];
        const char *text = count;
        if (!w) continue;
        /* 0x44026a: "  " for ID 0 (0x4774c1), else sprintf("%d" (0x4774c4), +0x17 as a signed word) */
        if (id == 0) text = literal(ws, 0x4774c1);
        else snprintf(count, sizeof count, "%d", (int)(int16_t)count_of(st, id));
        if (available(st, id)) es2_ui_message_set(ws->ui, w->columns[3], text, 1, 0x27, 1);
        if (!available(st, id)) {
            row_colours(ws, b, 0x10); /* 0x40ab3b x 4, no draw */
            w->enabled = 0;
        } else if (admits(ws, st, id)) {
            row_colours(ws, b, 0x27);
            w->enabled = 1;
            es2_ui_draw(ws->ui, row);
        } else {
            row_colours(ws, b, 0x25);
            w->enabled = 0;
            es2_ui_draw(ws->ui, row);
        }
    }
}

/* Original: VSHELL.EXE 0x43fc87 */
static void panel_show(es2_weapons_screen *ws) {
    if (ws->panel) return;
    es2_ui_show(ws->ui, id_of(ws, ES2_WEAPONS_GUIDE_ARM));
    es2_ui_show(ws->ui, id_of(ws, ES2_WEAPONS_GUIDE_ARH));
    es2_ui_show(ws->ui, id_of(ws, ES2_WEAPONS_GUIDE_SARH));
    es2_ui_show(ws->ui, id_of(ws, ES2_WEAPONS_GUIDE_EO));
    es2_ui_show(ws->ui, id_of(ws, ES2_WEAPONS_DESCRIPTION));
    ws->panel = 1;
}

/* Original: VSHELL.EXE 0x43fcd4 */
static void panel_hide(es2_weapons_screen *ws) {
    static const int order[4] = {ES2_WEAPONS_GUIDE_SARH, ES2_WEAPONS_GUIDE_ARH, ES2_WEAPONS_GUIDE_ARM,
                                 ES2_WEAPONS_GUIDE_EO};
    if (!ws->panel) return;
    for (int k = 0; k < 4; k++) {
        set_frame_colour(ws, id_of(ws, order[k]), 0x22);
        es2_ui_redraw_tree(ws->ui, id_of(ws, order[k]));
    }
    es2_ui_hide(ws->ui, id_of(ws, ES2_WEAPONS_GUIDE_ARM));
    es2_ui_hide(ws->ui, id_of(ws, ES2_WEAPONS_GUIDE_ARH));
    es2_ui_hide(ws->ui, id_of(ws, ES2_WEAPONS_GUIDE_SARH));
    es2_ui_hide(ws->ui, id_of(ws, ES2_WEAPONS_GUIDE_EO));
    es2_ui_hide(ws->ui, id_of(ws, ES2_WEAPONS_DESCRIPTION));
    ws->panel = 0;
}

static void changed(es2_weapons_screen *ws, uint32_t handler) {
    if (ws->host.changed) ws->host.changed(ws->host.ctx, handler);
}

/* Original: VSHELL.EXE 0x43e346: the selected slot's item word +8 := mode.
 * The library's store takes the missile racks 13..16 and modes 0..3; any
 * other store that would change the word is not made (own: no path of the
 * screen reaches one, vshell_ui.md section 29). */
static void store_mode(es2_weapons_screen *ws, const state *st, int mode) {
    int16_t item = slot_item(ws, st), bay = bay_of(ws);
    es2_shell_bay_op op;
    if (ws->slot == -1 || item <= 0 || slot_word(ws, st) == mode) return;
    if (item < 13 || item > 16 || mode < 0 || mode > 3) {
        ws->refused++;
        return;
    }
    memset(&op, 0, sizeof op);
    op.kind = ES2_SHELL_BAY_GUIDANCE;
    op.bay = bay;
    op.slot = ws->slot;
    op.value = (int16_t)mode;
    if (es2_shell_engine_bay(ws->engine, &op, NULL) == ES2_OK && op.applied) ws->stores++;
    else ws->refused++;
}

/* Original: VSHELL.EXE 0x4403f9 */
void es2_weapons_screen_guidance(es2_weapons_screen *ws, int mode, int flag) {
    state st;
    es2_ui_id b;
    if (!ws || !ws->ui) return;
    if (ws->guidance != -1) es2_ui_hide(ws->ui, pointer(ws, 26 + ws->guidance));
    if ((b = mode_button(ws, ws->guidance_previous))) { /* 0x440420: the previous button normal */
        set_frame_colour(ws, b, 0x22);
        es2_ui_redraw_tree(ws->ui, b);
    }
    if ((b = mode_button(ws, mode))) { /* 0x44049d: the new one highlighted */
        set_frame_colour(ws, b, 0x20);
        es2_ui_redraw_tree(ws->ui, b);
    }
    if (flag) { /* 0x440518 */
        es2_ui_hide(ws->ui, pointer(ws, ws->row));
        es2_ui_show(ws->ui, pointer(ws, 26 + mode));
        set_lines(ws, 0x63u + 3u * (uint32_t)mode);
        ws->guidance = (int16_t)mode;
    }
    ws->guidance_previous = mode;
    read_state(ws, &st);
    store_mode(ws, &st, mode);
    es2_ui_redraw_tree(ws->ui, id_of(ws, ES2_WEAPONS_ARM));
}

/* Original: VSHELL.EXE 0x43e2d4 (the mount needs the flag 0x476df4). */
static void mount(es2_weapons_screen *ws, const state *st, int16_t id, int flag) {
    int16_t bay = bay_of(ws);
    if (ws->slot == -1) return;
    if (flag && has_record(st, bay)) {
        es2_shell_bay_op op;
        memset(&op, 0, sizeof op);
        op.kind = ES2_SHELL_BAY_MOUNT;
        op.bay = bay;
        op.slot = ws->slot;
        op.value = id;
        if (es2_shell_engine_bay(ws->engine, &op, NULL) == ES2_OK) ws->mounts++;
        else ws->refused++; /* a slot beyond the record's capacity (0x411b10 writes past it) */
    }
    if (ws->host.slot_shown) ws->host.slot_shown(ws->host.ctx, ws->slot);
}

/* Original: VSHELL.EXE 0x43fdac */
int es2_weapons_screen_row(es2_weapons_screen *ws, int r, int flag) {
    state st;
    int16_t id;
    if (!ws || !ws->ui || r < 0 || r >= ES2_WEAPONS_ROWS) {
        if (ws) ws->refused++;
        return -1;
    }
    if (r == ws->row && ws->guidance == -1 && ws->slot == -1) return 0; /* 0x43fdb4..0x43fdd7 */
    read_state(ws, &st);
    id = row_id(ws, r);
    if (ws->slot != -1 && id != 0) { /* 0x43fddd..0x43fec4 */
        if (slot_word(ws, &st) != id && count_of(&st, id) == 0 && slot_item(ws, &st) != id) return 0;
    }
    mount(ws, &st, id, flag);
    read_state(ws, &st);
    refresh_list(ws, &st); /* 0x43fedc */
    if (ws->row != -1) { /* 0x43fee1: the previous row by 0x41501f only (no availability test) */
        es2_ui_id prev = row_window(ws, ws->row);
        es2_ui_widget *w = es2_ui_get(ws->ui, prev);
        if (w) {
            int on = admits(ws, &st, row_id(ws, ws->row));
            row_colours(ws, ws->row, on ? 0x27 : 0x25);
            w->enabled = on;
            es2_ui_draw(ws->ui, prev);
            w->colour = 0x10;
            es2_ui_redraw_tree(ws->ui, prev);
        }
        es2_ui_hide(ws->ui, pointer(ws, ws->row));
    }
    if (ws->guidance != -1 && !hidden(ws, pointer(ws, 26 + ws->guidance))) /* 0x43ffde */
        es2_ui_hide(ws->ui, pointer(ws, 26 + ws->guidance));
    { /* 0x440013: the new row */
        es2_ui_id row = row_window(ws, r);
        es2_ui_widget *w = es2_ui_get(ws->ui, row);
        if (w) {
            row_colours(ws, r, 0x29);
            w->colour = 0x29;
            es2_ui_redraw_tree(ws->ui, row);
            w->enabled = 1;
            es2_ui_draw(ws->ui, row);
        }
    }
    es2_ui_show(ws->ui, id == 0 ? id_of(ws, ES2_WEAPONS_NO_IMAGE) : pointer(ws, r));
    set_lines(ws, 3u * (uint32_t)(uint16_t)id);
    if (id >= 13 && id <= 16) { /* 0x440169 */
        es2_ui_widget *b = es2_ui_get(ws->ui, id_of(ws, ES2_WEAPONS_DESCRIPTION));
        if (b && es2_ui_message_set(ws->ui, b->label, estext(ws, 0x7eu + (uint32_t)id), 2, 0x29, 1) < 0) ws->refused++;
        panel_show(ws);
        if (ws->slot != -1) es2_weapons_screen_guidance(ws, slot_word(ws, &st), 0);
    } else {
        panel_hide(ws);
        ws->guidance = -1;
    }
    es2_ui_redraw_tree(ws->ui, id_of(ws, ES2_WEAPONS_ARM));
    ws->row = (int16_t)r;
    return 1;
}

/* Original: VSHELL.EXE 0x43e242 */
int es2_weapons_screen_select_slot(es2_weapons_screen *ws, int slot) {
    state st;
    int16_t item;
    int row;
    if (!ws || !ws->ui) return -1;
    if (slot == ws->slot) return 0;
    read_state(ws, &st);
    if (slot < -1 || slot >= 10 || !has_record(&st, bay_of(ws))) { /* own: VSHELL reads outside the record */
        ws->refused++;
        return -1;
    }
    ws->slot = (int16_t)slot;
    item = slot_item(ws, &st);
    row = row_of_id(&st, item);
    if (row < 0) ws->refused++; /* 0x43fdac(-1) reads before the ID table (own: not run) */
    else (void)es2_weapons_screen_row(ws, row, 0);
    if (ws->host.slot_shown) ws->host.slot_shown(ws->host.ctx, ws->slot);
    return 1;
}

/* Original: VSHELL.EXE 0x43df86..0x43dfe7 (the squad panel's WEAPONS case) */
void es2_weapons_screen_bay_changed(es2_weapons_screen *ws) {
    if (!ws || !ws->ui) return;
    ws->slot = -1;
    (void)es2_weapons_screen_row(ws, 0, 0);
}

/* ---- show, hide ----------------------------------------------------------------- */

/* Original: VSHELL.EXE 0x43fbd8 */
void es2_weapons_screen_show(es2_weapons_screen *ws, int previous) {
    state st;
    int16_t bay;
    if (!ws || !ws->ui) return;
    es2_ui_show(ws->ui, id_of(ws, ES2_WEAPONS_PANEL));
    es2_ui_show(ws->ui, id_of(ws, ES2_WEAPONS_LIST_PANEL));
    es2_ui_show(ws->ui, id_of(ws, ES2_WEAPONS_ARM));
    read_state(ws, &st);
    bay = bay_of(ws);
    if (bay == -1 || !has_record(&st, bay) || !st.finished[bay]) { /* 0x43fbfd..0x43fc49 */
        int16_t first = -1;
        for (int k = 0; k < 8 && first < 0; k++) /* 0x41124e */
            if (has_record(&st, (int16_t)k) && st.finished[k]) first = (int16_t)k;
        if (ws->host.select_bay) ws->host.select_bay(ws->host.ctx, first, previous);
        else if (ws->bay) *ws->bay = first;
        else ws->own_bay = first;
    }
    ws->slot = -1;
    (void)es2_weapons_screen_row(ws, 0, 0);
}

/* Original: VSHELL.EXE 0x43fc62 */
void es2_weapons_screen_hide(es2_weapons_screen *ws) {
    if (!ws || !ws->ui) return;
    es2_ui_hide(ws->ui, id_of(ws, ES2_WEAPONS_PANEL));
    es2_ui_hide(ws->ui, id_of(ws, ES2_WEAPONS_LIST_PANEL));
    es2_ui_hide(ws->ui, id_of(ws, ES2_WEAPONS_ARM));
}

/* ---- handlers ------------------------------------------------------------------- */

static int row_of_handler(uint32_t handler) {
    for (int r = 0; r < ES2_WEAPONS_ROWS; r++)
        if (handler == ES2_WEAPONS_ROW_HANDLER0 + 0x6fu * (uint32_t)r) return r;
    return -1;
}
static int mode_of_handler(uint32_t handler) {
    switch (handler) {
    case ES2_WEAPONS_SARH_HANDLER: return 0;
    case ES2_WEAPONS_ARH_HANDLER: return 1;
    case ES2_WEAPONS_ARM_HANDLER: return 2;
    case ES2_WEAPONS_EO_HANDLER: return 3;
    default: return -1;
    }
}

int es2_weapons_screen_press(es2_weapons_screen *ws, uint32_t handler) {
    int r, k, result;
    unsigned mounts, stores;
    if (!ws || !ws->ui) return -1;
    mounts = ws->mounts;
    stores = ws->stores;
    ws->last_handler = handler;
    ws->events++;
    if ((r = row_of_handler(handler)) >= 0) /* 0x440990 + 0x6f r: flag 0x476df4 around 0x43fdac(r) */
        result = es2_weapons_screen_row(ws, r, 1);
    else if ((k = mode_of_handler(handler)) >= 0) { /* 0x4403f9(mode, 1) */
        es2_weapons_screen_guidance(ws, k, 1);
        result = 1;
    } else if (handler == ES2_WEAPONS_DESCRIPTION_HANDLER) /* 0x4407ba: 0x43fdac([0x476df6]) without the flag */
        result = ws->row < 0 ? (ws->refused++, -1) : es2_weapons_screen_row(ws, ws->row, 0);
    else if (handler == ES2_WEAPONS_PREV_HANDLER || handler == ES2_WEAPONS_NEXT_HANDLER) {
        /* 0x43e3d9: slot > 0 ? slot - 1 : capacity - 1; 0x43e399: (slot + 1) % capacity */
        state st;
        int16_t bay;
        int capacity;
        read_state(ws, &st);
        bay = bay_of(ws);
        if (!has_record(&st, bay) || !(capacity = st.v.capacity[bay])) { /* own: VSHELL divides by 0 / reads a pilot */
            ws->refused++;
            return -1;
        }
        if (handler == ES2_WEAPONS_PREV_HANDLER) k = ws->slot > 0 ? ws->slot - 1 : capacity - 1;
        else k = (ws->slot + 1) % capacity;
        result = es2_weapons_screen_select_slot(ws, k);
    } else
        return -1;
    if (ws->mounts != mounts || ws->stores != stores) changed(ws, handler);
    return result;
}

static es2_weapons_screen *screen_of(es2_ui *ui, es2_ui_id window) {
    es2_ui_widget *w = es2_ui_get(ui, window);
    return w ? (es2_weapons_screen *)w->user : NULL;
}
static void on_handler(es2_ui *ui, es2_ui_id window, const es2_gui_event *ev) {
    es2_weapons_screen *ws = screen_of(ui, window);
    es2_ui_widget *w = es2_ui_get(ui, window);
    (void)ev; /* every handler ignores the button code */
    if (ws && w) (void)es2_weapons_screen_press(ws, w->original_handler);
}

/* ---- construction --------------------------------------------------------------- */

int es2_weapons_records(es2_span file, es2_weapons_record *out, int capacity) {
    size_t at = 0;
    int n = 0;
    if (!file.data && file.size) return -1;
    for (int list = 1; list <= 2; list++) {
        int16_t count;
        if (file.size - at < 2) return -1;
        count = es2_le16s(file.data + at);
        at += 2;
        for (int k = 0; k < count; k++) { /* a negative count reads nothing (cmp si, ax; jge) */
            if (file.size - at < 12) return -1;
            if (out && n < capacity) {
                out[n].id = es2_le16s(file.data + at);
                out[n].x = le32(file.data + at + 2);
                out[n].y = le32(file.data + at + 6);
                out[n].frame = es2_le16s(file.data + at + 10);
                out[n].list = list;
            }
            n++;
            at += 12;
        }
    }
    return n; /* bytes after the second list are not read */
}

static const es2_dfn *resolve_font(void *context, uint32_t global) {
    es2_weapons_screen *ws = context;
    return ws->host.font ? ws->host.font(ws->host.ctx, global) : NULL;
}
static const es2_bitmap *resolve_bitmap(void *context, const es2_ui_layout_ref *ref) {
    (void)context;
    (void)ref;
    return NULL; /* the screen's only bitmaps are the record lists' */
}
static const char *resolve_text(void *context, const es2_ui_layout_ref *ref) {
    es2_weapons_screen *ws = context;
    if (ref->kind == ES2_UI_REF_ESTEXT) return estext(ws, (uint32_t)ref->a);
    if (ref->kind == ES2_UI_REF_EXE_STRING) return literal(ws, (uint32_t)ref->a);
    return NULL;
}
static es2_ui_handler resolve_handler(void *context, uint32_t address) {
    (void)context;
    if (row_of_handler(address) >= 0 || mode_of_handler(address) >= 0 || address == ES2_WEAPONS_DESCRIPTION_HANDLER ||
        address == ES2_WEAPONS_PREV_HANDLER || address == ES2_WEAPONS_NEXT_HANDLER)
        return on_handler;
    return NULL;
}

/* Moves `id` to the oldest end of its parent's child list (the record lists
 * are built at the templates' place, before [0x48d6f8], in VSHELL). */
static void make_oldest(es2_ui *ui, es2_ui_id id) {
    es2_ui_widget *w = es2_ui_get(ui, id), *p, *t;
    es2_ui_id tail;
    if (!w || !(p = es2_ui_get(ui, w->parent)) || p->first != id || !w->next) return;
    p->first = w->next;
    es2_ui_get(ui, w->next)->newer = 0;
    for (tail = p->first; (t = es2_ui_get(ui, tail))->next; tail = t->next) {}
    t->next = id;
    w->newer = tail;
    w->next = 0;
}

/* Original: VSHELL.EXE 0x43ee2a..0x43f082: one ESBitmap per record on the
 * ESArm, rectangle (x, y, x + width, y + height) of its frame of
 * dba\arm_weap.dba, colour 0x27, frame flag +0x51 := 0; list 1 kept at
 * 0x48d680 + 4 0x43fd87(ID), list 2 at 0x48d6e8 + 4 ID. */
static int build_records(es2_weapons_screen *ws) {
    es2_weapons_record rec[64];
    es2_ui_id made[64];
    es2_span file = ws->host.file ? ws->host.file(ws->host.ctx, "gam\\arm_weap.dat") : (es2_span){NULL, 0};
    int n, built = 0;
    if (!file.data || !file.size) return 0;
    n = es2_weapons_records(file, rec, 64);
    if (n < 0 || n > 64) return -1;
    for (int k = 0; k < n; k++) {
        const es2_bitmap *b = ws->host.frame ? ws->host.frame(ws->host.ctx, "dba\\arm_weap.dba", rec[k].frame) : NULL;
        int slot;
        es2_ui_id id;
        es2_ui_widget *w;
        if (rec[k].list == 1) {
            slot = -1;
            for (int r = 0; r < ES2_WEAPONS_ROWS; r++)
                if (row_id(ws, r) == rec[k].id) { slot = r; break; }
        } else
            slot = 26 + rec[k].id;
        /* own: a record outside the array would overwrite [0x48d67c] or a later global; a missing frame
         * (the original dereferences the frame table) */
        if (slot < 0 || slot >= 30 || !b) {
            ws->refused++;
            continue;
        }
        id = es2_ui_bitmap(ws->ui, id_of(ws, ES2_WEAPONS_ARM),
                           (es2_gui_rect){rec[k].x, rec[k].y, rec[k].x + b->width, rec[k].y + b->height}, b, NULL, 0x27);
        if (!(w = es2_ui_get(ws->ui, id))) return -1;
        w->frame = 0;
        w->user = ws;
        ws->pointers[slot] = id;
        made[built++] = id;
    }
    for (int k = built - 1; k >= 0; k--) make_oldest(ws->ui, made[k]);
    return 0;
}

/* Original: VSHELL.EXE 0x43ebbc */
int es2_screen_weapons_build(es2_ui *ui, void *ctx) {
    es2_weapons_screen *ws = ctx;
    es2_ui_resolver r;
    state st;
    if (!ui || !ws) return -1;
    ws->ui = ui;
    r.context = ws;
    r.font = resolve_font;
    r.bitmap = resolve_bitmap;
    r.text = resolve_text;
    r.handler = resolve_handler;
    memset(ws->ids, 0, sizeof ws->ids);
    memset(ws->pointers, 0, sizeof ws->pointers);
    if (es2_ui_layout_build(ui, ES2_UI_SCREEN_WEAPONS, &r, ws->ids, ES2_WEAPONS_CONTROLS) < 0) return -1;
    for (int k = 0; k < ES2_WEAPONS_CONTROLS; k++) {
        es2_ui_widget *w = es2_ui_get(ui, ws->ids[k]);
        if (w) w->user = ws;
        else if (k != 5 && k != 6) return -1; /* 5, 6: the record lists' templates */
    }
    if (build_records(ws) < 0) return -1;
    ws->pointers[30] = ws->ids[ES2_WEAPONS_NO_IMAGE];
    ws->pointers[31] = ws->ids[ES2_WEAPONS_GUIDE_ARM];
    /* 0x43f858..0x43f892 (and 0x43f9b8..): rows enabled by the availability byte at construction */
    read_state(ws, &st);
    for (int k = 0; k < ES2_WEAPONS_ROWS; k++) {
        es2_ui_widget *w = es2_ui_get(ui, row_window(ws, k));
        if (w) w->enabled = st.loaded && available(&st, row_id(ws, k));
    }
    /* the image values of the screen globals (0x476658, 0x476df6, 0x476df8, 0x476dfc, 0x476a82) */
    ws->slot = -1;
    ws->row = -1;
    ws->guidance = -1;
    ws->guidance_previous = -1;
    ws->panel = 0;
    return 0;
}
