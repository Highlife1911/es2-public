/* The shell state engine (es2/shell/engine.h). No file I/O. */
#include "es2/shell/engine.h"
#include "es2/shell/bytes.h"
#include "es2/shell/practice.h"
#include "es2/formats/sav.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TRY(x) do { es2_status try_status = (x); if (try_status != ES2_OK) return try_status; } while (0)


/* ---- executable tables ------------------------------------------------------ */
static es2_status copy_span(es2_span exe, uint32_t va, size_t size, uint8_t *out, es2_error *e) {
    es2_span s;
    TRY(es2_shell_exe_span(exe, va, size, &s, e));
    memcpy(out, s.data, size);
    return ES2_OK;
}
/* A NUL-terminated string of at most capacity - 1 bytes at the VA the pointer
 * at `pointer_va` holds (the span may end at the section end). */
static es2_status pointed_string(es2_span exe, uint32_t pointer_va, char *out, size_t capacity, es2_error *e) {
    uint8_t pointer[4];
    size_t n;
    es2_span s;
    TRY(copy_span(exe, pointer_va, 4, pointer, e));
    for (n = capacity; n > 0; n--)
        if (es2_shell_exe_span(exe, es2_le32(pointer), n, &s, e) == ES2_OK) break;
    if (!n || !memchr(s.data, 0, n)) return es2_fail(e, ES2_ERR_STRING, pointer_va);
    memcpy(out, s.data, strlen((const char *)s.data) + 1);
    return ES2_OK;
}
es2_status es2_shell_engine_tables_read(es2_span exe, es2_shell_engine_tables *out, es2_error *e) {
    /* vshell_preparation.md (default tables, preset paths) and "Results path"
     * (the results spans); the WEAPONS rows and ARM names are
     * those es2loadout's catalogue reads (launcher.md). */
    static const uint32_t prepare[3][2] = {{0x47f850, 112}, {0x46f72c, 16}, {0x46f868, 36}};
    static const uint32_t results[7][2] = {{0x46f9e4, 48}, {0x46fec4, 12}, {0x46feb8, 12}, {0x46fb80, 66},
                                           {0x46fbc2, 66}, {0x46fa20, 58}, {0x46f9a8, 60}};
    es2_shell_engine_tables t;
    uint8_t rows[2 * ES2_SHELL_WEAPON_ROWS];
    size_t at = 0, i;
    if (!out || !exe.data) return es2_fail(e, ES2_ERR_ARGUMENT, 0);
    memset(&t, 0, sizeof t);
    for (i = 0; i < 3; i++) { TRY(copy_span(exe, prepare[i][0], prepare[i][1], t.prepare + at, e)); at += prepare[i][1]; }
    for (i = 0; i < 9; i++) {
        char path[64];
        TRY(pointed_string(exe, 0x47083c + 4u * (uint32_t)i, path, sizeof path, e));
        memcpy(t.prepare + 164 + 64 * i, path, strlen(path));
    }
    for (i = 0, at = 0; i < 7; i++) { TRY(copy_span(exe, results[i][0], results[i][1], t.results + at, e)); at += results[i][1]; }
    TRY(copy_span(exe, 0x476a4c, sizeof rows, rows, e));
    for (i = 0; i < ES2_SHELL_WEAPON_ROWS; i++) t.rows[i] = es2_le16s(rows + 2 * i);
    for (i = 0; i < 9; i++) TRY(pointed_string(exe, 0x47006c + 4u * (uint32_t)i, t.arm[i], sizeof t.arm[i], e));
    *out = t;
    es2_error_clear(e);
    return ES2_OK;
}

/* ---- ARM hardpoint tables ---------------------------------------------------- */
es2_status es2_shell_arm_parse(es2_span arm, uint16_t slots[33], es2_error *e) {
    es2_reader r;
    int16_t groups, item, count, slot;
    uint16_t out[33];
    if (!slots) return es2_fail(e, ES2_ERR_ARGUMENT, 0);
    memset(out, 0, sizeof out);
    TRY(es2_reader_init(&r, arm, e));
    TRY(es2_skip(&r, 44));
    TRY(es2_read_i16(&r, &groups));
    if (groups < 0) return es2_fail(e, ES2_ERR_COUNT, r.offset - 2);
    for (int g = 0; g < groups; g++) {
        TRY(es2_read_i16(&r, &item));
        TRY(es2_read_i16(&r, &count));
        if (count < 0) return es2_fail(e, ES2_ERR_COUNT, r.offset - 2);
        for (int k = 0; k < count; k++) {
            TRY(es2_read_i16(&r, &slot));
            TRY(es2_skip(&r, 20));
            /* the first word is the slot plus two; only slots 0..15 of items
             * 0..32 can be asked for (ten slots, 33 item IDs) */
            if (item >= 0 && item < 33 && slot >= 2 && slot < 18) out[item] |= (uint16_t)(1u << (slot - 2));
        }
    }
    TRY(es2_reader_end(&r));
    memcpy(slots, out, sizeof out);
    return ES2_OK;
}
es2_status es2_shell_hardpoints_read(const es2_vfs *vfs, const es2_shell_engine_tables *tables,
                                     es2_shell_hardpoints *out, es2_error *e) {
    es2_shell_hardpoints h;
    es2_vfs_copy copy;
    if (!vfs || !tables || !out) return es2_fail(e, ES2_ERR_ARGUMENT, 0);
    for (int v = 0; v < 9; v++) {
        TRY(es2_vfs_find(vfs, tables->arm[v], &copy, e));
        TRY(es2_shell_arm_parse(copy.payload, h.slots[v], e));
    }
    *out = h;
    return ES2_OK;
}

/* ---- state ------------------------------------------------------------------- */
static es2_status keep(es2_shell_engine *g, const uint8_t *data, size_t size, es2_error *e) {
    uint8_t *copy = malloc(size ? size : 1);
    if (!copy) return es2_fail(e, ES2_ERR_CAPACITY, size);
    memcpy(copy, data, size);
    free(g->memory.data);
    g->memory.data = copy;
    g->memory.size = size;
    return ES2_OK;
}
static int ready(const es2_shell_engine *g) {
    return g && g->vfs && g->tables && g->hardpoints &&
           (!strcmp(g->language, "ENG") || !strcmp(g->language, "FRE") || !strcmp(g->language, "GER"));
}
es2_status es2_shell_engine_start(es2_shell_engine *g, uint16_t draws, es2_error *e) {
    es2_shell_rng rng;
    if (!ready(g) || draws > 32767) return es2_fail(e, ES2_ERR_ARGUMENT, 0);
    TRY(es2_shell_rng_init((es2_span){g->tables->prepare, 112}, draws, &rng, e));
    es2_shell_engine_free(g);
    g->loaded = 0;
    g->final_word = 0;
    g->seed = draws;
    g->rng = rng;
    memset(g->selectors, 0, sizeof g->selectors);
    return ES2_OK;
}
void es2_shell_engine_free(es2_shell_engine *g) {
    if (!g) return;
    free(g->memory.data);
    g->memory.data = NULL;
    g->memory.size = 0;
}
static es2_status final_word(es2_span sav, int16_t *out, es2_error *e) {
    es2_sav s;
    TRY(es2_sav_parse(sav, &s, e));
    *out = es2_le16s(s.final_fields.data);   /* the first of the final fields (as es2m_save_info_read) */
    return ES2_OK;
}
es2_status es2_shell_engine_set(es2_shell_engine *g, es2_span sav, es2_error *e) {
    int16_t word;
    if (!g || !sav.data) return es2_fail(e, ES2_ERR_ARGUMENT, 0);
    TRY(final_word(sav, &word, e));
    TRY(keep(g, sav.data, sav.size, e));
    g->loaded = 1;
    g->final_word = word;
    return ES2_OK;
}

es2_status es2_shell_engine_new_game(es2_shell_engine *g, const char *name, uint16_t rank, uint32_t *salvage,
                                     es2_error *e) {
    es2_shell_save_tables t;
    es2_shell_rng rng;
    uint8_t *out;
    size_t size = 0;
    uint32_t total = 0;
    es2_status status;
    if (!ready(g) || !name) return es2_fail(e, ES2_ERR_ARGUMENT, 0);
    t.defaults = (es2_span){g->tables->prepare, 164};
    t.item_indices = (es2_span){g->tables->results + ES2_SHELL_RESULTS_TABLES_SIZE - 60, 60};
    rng = g->rng;
    TRY(es2_shell_new_game(g->vfs, g->language, &t, name, rank, 1, 0, &rng, NULL, 0, &size, NULL, e));
    if (!(out = malloc(size))) return es2_fail(e, ES2_ERR_CAPACITY, size);
    status = es2_shell_new_game(g->vfs, g->language, &t, name, rank, 1, 0, &rng, out, size, &size, &total, e);
    if (status == ES2_OK) {
        free(g->memory.data);
        g->memory.data = out;
        g->memory.size = size;
        g->rng = rng;
        g->loaded = 1;
        g->final_word = 2;   /* 0x40e8a8 */
        if (salvage) *salvage = total;
    } else
        free(out);
    return status;
}

es2_status es2_shell_engine_load(es2_shell_engine *g, es2_span file, es2_error *e) {
    uint8_t *out;
    size_t size = 0;
    int16_t word;
    es2_status status;
    if (!g || !file.data) return es2_fail(e, ES2_ERR_ARGUMENT, 0);
    TRY(final_word(file, &word, e));
    TRY(es2_shell_loaded_save(file, NULL, 0, &size, e));
    if (!(out = malloc(size))) return es2_fail(e, ES2_ERR_CAPACITY, size);
    status = es2_shell_loaded_save(file, out, size, &size, e);
    if (status != ES2_OK) { free(out); return status; }
    free(g->memory.data);
    g->memory.data = out;
    g->memory.size = size;
    g->loaded = 1;           /* 0x40ebeb */
    g->final_word = word;    /* 0x40ebc3 */
    return ES2_OK;
}

/* ---- preparation --------------------------------------------------------------- */
void es2_shell_engine_prepared_free(es2_shell_engine_prepared *p) {
    if (!p) return;
    es2_shell_output_free(&p->files);
    free(p->input.data);
    memset(p, 0, sizeof *p);
}
/* es2chain's hangar policy: the player's bay word and its change. */
static es2_status hangar_policy(const es2_shell_engine *g, es2_span sav, es2_shell_buffer *input, int16_t *from,
                                int16_t *to, es2_error *e) {
    es2_sav s;
    int used[8] = {0}, roster[8] = {0};
    int16_t player;
    size_t i, offset;
    TRY(es2_sav_parse(sav, &s, e));
    player = es2_le16s(s.player.suffix.data);
    for (i = 0; i < s.loadout_count; i++) {
        es2_sav_loadout l;
        TRY(es2_sav_loadout_at(&s, i, &l, e));
        if (l.index >= 0 && l.index < 8) used[l.index] = 1;
    }
    for (i = 0; i < 36; i++) {
        es2_sav_pilot p;
        int16_t bay;
        TRY(es2_sav_pilot_at(&s, i, &p, e));
        bay = es2_le16s(p.suffix.data);
        if (bay >= 0 && bay < 8) roster[bay] = 1;
    }
    *from = *to = -2;
    if (!(input->data = malloc(sav.size ? sav.size : 1))) return es2_fail(e, ES2_ERR_CAPACITY, sav.size);
    memcpy(input->data, sav.data, sav.size);
    input->size = sav.size;
    if (player >= 0 && player < 8 && used[player]) return ES2_OK;
    for (i = 0; i < 8 && *to < 0; i++)
        if (used[i] && !roster[i]) *to = (int16_t)i;
    if (!g->lowest_free || *to < 0) {
        free(input->data);
        input->data = NULL;
        input->size = 0;
        *to = -2;
        return es2_fail(e, ES2_ERR_UNSUPPORTED, (size_t)(s.player.suffix.data - sav.data));
    }
    *from = player;
    offset = (size_t)(s.player.suffix.data - sav.data);
    input->data[offset] = (uint8_t)*to;
    input->data[offset + 1] = 0;
    return ES2_OK;
}
static es2_status engine_prepare(es2_shell_engine *g, const char *mission, es2_shell_engine_prepared *out, es2_error *e) {
    es2_shell_engine_prepared p;
    es2_shell_options o;
    es2_shell_state state;
    es2_sav s;
    uint8_t *sav = NULL;
    size_t size = 0;
    int16_t id;
    es2_status status;
    if (!ready(g) || !out || !g->loaded || !g->memory.data) return es2_fail(e, ES2_ERR_ARGUMENT, 0);
    memset(&p, 0, sizeof p);
    TRY(es2_sav_parse((es2_span){g->memory.data, g->memory.size}, &s, e));
    p.group = es2_le16s(s.selection.data);
    p.position = es2_le16s(s.selection.data + 2);
    if (p.group < 1 || p.group > 5) return es2_fail(e, ES2_ERR_ARGUMENT, (size_t)(s.selection.data - s.input.data));
    TRY(hangar_policy(g, (es2_span){g->memory.data, g->memory.size}, &p.input, &p.hangar_from, &p.hangar_to, e));
    /* the DEBUG dialog's ACCEPT 0x44dc4d prepares msn\<stem>.msn with the
     * CAREER position left as it is (vshell_ui.md section 15.6) */
    if (mission) { snprintf(p.mission, sizeof p.mission, "%s", mission); status = ES2_OK; }
    else status = es2_shell_campaign_mission(g->vfs, p.group, p.position, g->language, p.mission, sizeof p.mission, &id, e);
    memset(&state, 0, sizeof state);
    if (status == ES2_OK) status = es2_shell_restore_save((es2_span){p.input.data, p.input.size}, &state, e);
    p.rng_in = g->rng;
    memcpy(p.selectors_in, g->selectors, sizeof p.selectors_in);
    state.rng = g->rng;
    memcpy(state.reset_selectors, g->selectors, sizeof state.reset_selectors);
    /* es2chain's request: mode 1, difficulty 0 (0x4826ea has no writer,
     * vshell_state.md), the SAV's bank, the default tables and preset path 0.
     * */
    memset(&o, 0, sizeof o);
    o.mission = mission;
    o.language = g->language;
    o.group = p.group;
    o.position = p.position;
    o.mode = 1;
    o.default_tables = (es2_span){g->tables->prepare, 164};
    o.training_preset_path = (const char *)g->tables->prepare + 164;
    if (status == ES2_OK) status = es2_shell_prepare(g->vfs, &o, &state, &p.files, e);
    if (status == ES2_OK && !p.files.has_briefing) status = es2_fail(e, ES2_ERR_UNSUPPORTED, 0);
    if (status == ES2_OK)
        status = es2_shell_prepared_save((es2_span){p.input.data, p.input.size}, p.group, p.position, &state, &p.files,
                                         NULL, 0, &size, e);
    if (status == ES2_OK && !(sav = malloc(size))) status = es2_fail(e, ES2_ERR_CAPACITY, size);
    if (status == ES2_OK)
        status = es2_shell_prepared_save((es2_span){p.input.data, p.input.size}, p.group, p.position, &state, &p.files,
                                         sav, size, &size, e);
    if (status != ES2_OK) {
        free(sav);
        es2_shell_engine_prepared_free(&p);
        return status;
    }
    free(g->memory.data);
    g->memory.data = sav;
    g->memory.size = size;
    g->rng = state.rng;
    memcpy(g->selectors, state.reset_selectors, sizeof g->selectors);
    *out = p;
    return ES2_OK;
}
es2_status es2_shell_engine_prepare(es2_shell_engine *g, es2_shell_engine_prepared *out, es2_error *e) {
    return engine_prepare(g, NULL, out, e);
}
es2_status es2_shell_engine_prepare_mission(es2_shell_engine *g, const char *mission, es2_shell_engine_prepared *out,
                                            es2_error *e) {
    if (!mission || !mission[0]) return es2_fail(e, ES2_ERR_ARGUMENT, 0);
    return engine_prepare(g, mission, out, e);
}

/* ---- results path ----------------------------------------------------------------- */
es2_status es2_shell_engine_results(es2_shell_engine *g, es2_span slot, es2_span variables, es2_span results,
                                    uint8_t repair, uint8_t restock, es2_shell_results_output *out, es2_error *e) {
    es2_shell_results_input in;
    es2_shell_results_output r;
    es2_sav s;
    uint8_t *copy;
    if (!ready(g) || !out || !slot.data) return es2_fail(e, ES2_ERR_ARGUMENT, 0);
    TRY(es2_sav_parse(slot, &s, e));
    memset(&in, 0, sizeof in);
    memset(&r, 0, sizeof r);
    in.save = (es2_span){slot.data, s.reader_end};
    in.variables = variables;
    in.results = results;
    in.tables = (es2_span){g->tables->results, ES2_SHELL_RESULTS_TABLES_SIZE};
    in.language = g->language;
    in.mode = g->mode;
    in.repair_option = repair;
    in.restock_option = restock;
    in.rng = g->rng;
    TRY(es2_shell_results(g->vfs, &in, &r, e));
    if (!(copy = malloc(r.save.size ? r.save.size : 1))) {
        es2_shell_results_free(&r);
        return es2_fail(e, ES2_ERR_CAPACITY, r.save.size);
    }
    memcpy(copy, r.save.data, r.save.size);
    free(g->memory.data);
    g->memory.data = copy;
    g->memory.size = r.save.size;
    g->final_word = r.final_word;
    g->rng = r.rng;
    g->loaded = 1;
    *out = r;
    return ES2_OK;
}

/* ---- service bay ----------------------------------------------------------------- */
int es2_shell_engine_admits(const es2_shell_engine *g, int16_t vehicle, int16_t item, int16_t slot) {
    /* 0x4142f0 on the ARM tables of the bay's vehicle word (0x41501f) */
    return g && g->hardpoints && vehicle >= 0 && vehicle < 9 && item >= 0 && item < 33 && slot >= 0 && slot < 16 &&
           (g->hardpoints->slots[vehicle][item] >> slot & 1u);
}
es2_status es2_shell_engine_bay_view(const es2_shell_engine *g, int bay, int slot, es2_shell_bay_view *out,
                                     es2_error *e) {
    es2_shell_bay_view v;
    es2_sav s;
    size_t i, j;
    if (!ready(g) || !out || !g->memory.data) return es2_fail(e, ES2_ERR_ARGUMENT, 0);
    TRY(es2_sav_parse((es2_span){g->memory.data, g->memory.size}, &s, e));
    memset(&v, 0, sizeof v);
    for (i = 0; i < 8; i++) v.pilot[i] = -1;
    for (i = 0; i < s.loadout_count; i++) {
        es2_sav_loadout l;
        TRY(es2_sav_loadout_at(&s, i, &l, e));
        if (l.index < 0 || l.index >= 8) continue;
        v.present[l.index] = 1;
        v.vehicle[l.index] = es2_le16s(l.fixed.data);
        v.capacity[l.index] = (uint16_t)es2_le16(l.fixed.data + 74);
        for (j = 0; j < l.count; j++) {
            const uint8_t *r = l.items.data + 12 * j;
            unsigned k = es2_le16(r);
            if (k < 10) { v.item[l.index][k] = es2_le16s(r + 2); v.secondary[l.index][k] = (uint16_t)es2_le16(r + 10); }
        }
    }
    /* 0x410884: the player first, then the first of the three selected
     * roster pilots naming the bay; replaced (dead) pilots keep their bay */
    if (s.roster_selection.size < 6) return es2_fail(e, ES2_ERR_LENGTH, 0);
    for (i = 0; i <= 3; i++) {
        es2_sav_pilot p;
        size_t index = 36;
        int16_t b;
        if (i == 0) p = s.player;
        else {
            unsigned selected = es2_le16(s.roster_selection.data + 2 * (i - 1));
            if (selected > 11) continue;
            index = (i - 1) * 12 + selected;
            TRY(es2_sav_pilot_at(&s, index, &p, e));
        }
        b = es2_le16s(p.suffix.data);
        if (b < 0 || b >= 8 || !v.present[b] || v.pilot[b] >= 0) continue;
        v.pilot[b] = (int)index;
        j = p.name.size ? p.name.size - 1 : 0;
        if (j >= sizeof v.name[b]) j = sizeof v.name[b] - 1;
        memcpy(v.name[b], p.name.data, j);
    }
    if (bay >= 0 && slot >= 0) {
        if (bay >= 8 || slot >= 16) return es2_fail(e, ES2_ERR_ARGUMENT, 0);
        v.rows = ES2_SHELL_WEAPON_ROWS;
        for (i = 0; i < ES2_SHELL_WEAPON_ROWS; i++) {
            int16_t item = g->tables->rows[i];
            v.row_item[i] = item;
            if (item < 0 || item >= 33) continue;
            v.row_count[i] = (int)s.inventory[item].count;
            v.row_available[i] = s.inventory[item].flag;
            v.row_admitted[i] = v.present[bay] && es2_shell_engine_admits(g, v.vehicle[bay], item, (int16_t)slot);
            v.row_enabled[i] = v.row_available[i] && v.row_admitted[i];
        }
    }
    *out = v;
    return ES2_OK;
}
es2_status es2_shell_engine_bay(es2_shell_engine *g, es2_shell_bay_op *op, es2_error *e) {
    es2_shell_buffer out = {0};
    if (!ready(g) || !op || !g->memory.data) return es2_fail(e, ES2_ERR_ARGUMENT, 0);
    TRY(es2_shell_service_bay(g->vfs, (es2_span){g->tables->results, ES2_SHELL_RESULTS_TABLES_SIZE},
                              (es2_span){g->memory.data, g->memory.size}, op, 1, &out, e));
    free(g->memory.data);
    g->memory = out;
    return ES2_OK;
}

es2_status es2_shell_engine_launch(const es2_shell_engine *g, es2_shell_launch_check_result *check,
                                   es2_shell_buffer *player, es2_shell_buffer *variables, es2_error *e) {
    es2_shell_launch_check_result r;
    es2_span sav;
    if (!g || !check || !player || !variables || player->data || variables->data || !g->memory.data)
        return es2_fail(e, ES2_ERR_ARGUMENT, 0);
    sav = (es2_span){g->memory.data, g->memory.size};
    TRY(es2_shell_launch_check(sav, &r, e));
    if (r.warning == ES2_SHELL_LAUNCH_OK) TRY(es2_shell_launch_files(sav, player, variables, e));
    *check = r;
    return ES2_OK;
}
