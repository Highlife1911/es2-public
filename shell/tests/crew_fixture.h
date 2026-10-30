#ifndef ES2_SHELL_TESTS_CREW_FIXTURE_H
#define ES2_SHELL_TESTS_CREW_FIXTURE_H

/* A synthetic game state for the CREW screen's tests and the editors
 * library as the screen's host. Every table, catalogue, name and SAV here
 * is invented for the tests (the layout of the files follows
 * test_editors.c's fixture). The host keeps the SAV image, runs
 * es2_shell_editors for the screen's edit callback and serves the team and
 * squad-panel views read back from the SAV.
 *
 * State: capacity cw->capacity (default 4), active count 3. Player (roster
 * 36, portrait 0) in bay 0, skill 2. Group 0's selected pilot (roster 0,
 * "BAKER", portrait 3) bay 1, order 1, active; group 1's (roster 12, "COLE",
 * portrait 7) bay 2, order 2, active; group 2's (roster 24, "DIAZ",
 * portrait 11) no bay, no order. Bays: 0 vehicle 1, 1 vehicle 2, 2 vehicle
 * 0, 3 vehicle 8 (no pilot), all finished; 4 vehicle 5 under construction
 * (40); 5..7 empty. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "es2/formats/sav.h"
#include "es2/formats/vfs.h"
#include "es2/shell/editors.h"
#include "es2/shell/gui/crew.h"
#include "es2/shell/gui/service_bay.h"

typedef struct crew_world {
    uint8_t tables[322], editor[98], herc[2 + 144], damage[102], weapons[2 + 33 * 11 + 2];
    uint8_t five[22], selection[152], choice[12], team[4], nine[18], tally[4], final[22], variables[2000];
    uint8_t prefixes[37][4], suffixes[37][25], names[37][16], bays[8][196];
    int bay_present[8];
    es2_vfs_file files[3];
    es2_vfs_source source;
    es2_vfs vfs;
    uint8_t image[16384];
    size_t size;
    int ok;               /* the image is valid */
    unsigned edits, failures;
    /* the views (pointers into this storage) */
    es2_sav sav;
    char name[37][32], herc_name[8][16];
} crew_world;

static inline void cw_w(uint8_t *p, unsigned x) { p[0] = (uint8_t)x; p[1] = (uint8_t)(x >> 8); }
static inline int cw_s(const uint8_t *p) {
    unsigned x = p[0] + 256u * p[1];
    return x < 32768 ? (int)x : (int)x - 65536;
}

static inline void cw_pilot(crew_world *f, int i, const char *name, int portrait, int bay, int active, int order, int skill) {
    uint8_t *x = f->suffixes[i];
    memset(x, 0, 25);
    cw_w(x, (unsigned)bay);          /* +0x22 */
    x[2] = (uint8_t)active;          /* +0x24 */
    cw_w(x + 3, (unsigned)skill);    /* +0x25 */
    cw_w(x + 5, (unsigned)order);    /* +0x27 */
    cw_w(x + 9, 100);
    memset(f->prefixes[i], 0, 4);
    cw_w(f->prefixes[i], (unsigned)portrait); /* +0 */
    snprintf((char *)f->names[i], sizeof f->names[i], "%s", name);
}
static inline void cw_bay(crew_world *f, int k, int vehicle, int finished) {
    uint8_t *b = f->bays[k];
    memset(b, 0, 196);
    cw_w(b, (unsigned)vehicle);
    cw_w(b + 2, (unsigned)vehicle);
    for (int i = 0; i < 33; i++) cw_w(b + 4 + 2 * i, 100);
    cw_w(b + 70, finished ? 100u : 40u);
    cw_w(b + 74, 3);
    f->bay_present[k] = 1;
}

/* Builds the SAV image from the fields; 0 = ok. */
static inline int cw_compose(crew_world *f) {
    es2_sav_image m;
    es2_sav_pilot roster[37];
    es2_sav_loadout loadouts[8];
    uint8_t inventory[1] = {0};
    size_t n = 0, required = 0;
    es2_error e;
    memset(&m, 0, sizeof m);
    for (int i = 0; i < 33; i++) {
        m.inventory[i].flag = 1;
        m.inventory[i].count = 0;
        m.inventory[i].items = (es2_span){inventory, 0};
    }
    for (int i = 0; i < 37; i++) {
        roster[i].prefix = (es2_span){f->prefixes[i], 4};
        roster[i].name = (es2_span){f->names[i], strlen((char *)f->names[i]) + 1};
        roster[i].suffix = (es2_span){f->suffixes[i], 25};
        roster[i].offset = 0;
    }
    for (int k = 0; k < 8; k++)
        if (f->bay_present[k]) {
            loadouts[n].index = (int16_t)k;
            loadouts[n].fixed = (es2_span){f->bays[k], 76};
            loadouts[n].items = (es2_span){f->bays[k] + 76, 0};
            loadouts[n].count = 0;
            loadouts[n].offset = 0;
            n++;
        }
    m.five_entry_bank = (es2_span){f->five, 22};
    m.selection = (es2_span){f->selection, 152};
    m.roster = roster;
    m.roster_selection = (es2_span){f->choice, 12};
    m.team = (es2_span){f->team, 4};
    m.player = &roster[36];
    m.loadout_count = n;
    m.loadouts = loadouts;
    m.nine_word_bank = (es2_span){f->nine, 18};
    m.tally = (es2_span){f->tally, 4};
    m.variables = (es2_span){f->variables, 2000};
    m.final_fields = (es2_span){f->final, 22};
    if (es2_sav_compose(&m, f->image, sizeof f->image, &required, &e) != ES2_OK) return 1;
    f->size = required;
    f->ok = es2_sav_parse((es2_span){f->image, f->size}, &f->sav, &e) == ES2_OK;
    return f->ok ? 0 : 2;
}

/* The default state (see the top); capacity 4. Call cw_compose after
 * changing fields. */
static inline void cw_init(crew_world *f) {
    uint8_t *p;
    const int groups[24] = {0, 1, -1, 0, 2, 4, -1, 0, 3, 5, -1, 0, 6, -1, 0, 0, 7, 9, 11, -1, 8, 10, 12, -1};
    const int t[] = {90, 80, 60, 30, 1, 0}, l[] = {100, 89, 79, 59, 29, 0}, band[] = {89, 79, 59, 29, 0},
              cap[] = {3, 5, 5, 8, 9, 9, 10, 4, 7};
    memset(f, 0, sizeof *f);
    for (int i = 0; i < 24; i++) cw_w(f->tables + 2 * i, (unsigned)groups[i]);
    for (int i = 0; i < 6; i++) {
        cw_w(f->tables + 48 + 2 * i, (unsigned)t[i]);
        cw_w(f->tables + 60 + 2 * i, (unsigned)l[i]);
    }
    for (int i = 0; i < 33; i++) cw_w(f->tables + 138 + 2 * i, 65535);
    for (int i = 0; i < 30; i++) cw_w(f->tables + 262 + 2 * i, (unsigned)i);
    for (int i = 0; i < 5; i++) cw_w(f->editor + 2 * i, (unsigned)band[i]);
    for (int i = 0; i < 9; i++) {
        cw_w(f->editor + 10 + 2 * i, (unsigned)i);
        cw_w(f->editor + 28 + 2 * i, (unsigned)cap[i]);
    }
    for (int i = 0; i < 26; i++) cw_w(f->editor + 46 + 2 * i, (unsigned)(i + 1));
    cw_w(f->herc, 9);
    for (int i = 0; i < 9; i++) {
        p = f->herc + 2 + 16 * i;
        cw_w(p, (unsigned)i);
        cw_w(p + 8, 100);
        cw_w(p + 12, (unsigned)(2 + i));
    }
    cw_w(f->damage, 1024);
    for (int i = 0; i < 15; i++) cw_w(f->damage + 2 + 2 * i, 1);
    cw_w(f->damage + 32, 1024);
    cw_w(f->damage + 34, 33);
    for (int i = 0; i < 33; i++) cw_w(f->damage + 36 + 2 * i, 100);
    p = f->weapons;
    cw_w(p, 33);
    p += 2;
    for (int i = 0; i < 33; i++) {
        cw_w(p, (unsigned)i);
        cw_w(p + 2, 2);
        p[4] = 'W';
        p[5] = 0;
        cw_w(p + 6, 2);
        p[8] = 1;
        cw_w(p + 9, i ? (unsigned)i : 99u);
        p += 11;
    }
    cw_w(f->five, 5);
    for (int i = 0; i < 5; i++) cw_w(f->five + 2 + 4 * i, (unsigned)i);
    for (int i = 0; i < 36; i++) cw_pilot(f, i, "X", i, -1, 0, -1, 0);
    cw_pilot(f, 0, "BAKER", 3, 1, 1, 1, 1);
    cw_pilot(f, 12, "COLE", 7, 2, 1, 2, 3);
    cw_pilot(f, 24, "DIAZ", 11, -1, 0, -1, 0);
    cw_pilot(f, 36, "PLAYER", 0, 0, 1, 0, 2);
    for (int i = 0; i < 3; i++) {
        cw_w(f->choice + 2 * i, 0);
        cw_w(f->choice + 6 + 2 * i, 1);
    }
    cw_w(f->team, 4);
    cw_w(f->team + 2, 3);
    for (int i = 0; i < 9; i++) cw_w(f->nine + 2 * i, 1);
    cw_bay(f, 0, 1, 1);
    cw_bay(f, 1, 2, 1);
    cw_bay(f, 2, 0, 1);
    cw_bay(f, 3, 8, 1);
    cw_bay(f, 4, 5, 0);
    cw_w(f->tally, 50000);
    f->files[0] = (es2_vfs_file){"GAM/HERC_INF.DAT", {f->herc, sizeof f->herc}};
    f->files[1] = (es2_vfs_file){"GAM/DAMAGE.DAT", {f->damage, sizeof f->damage}};
    f->files[2] = (es2_vfs_file){"GAM/WEAPONS.DAT", {f->weapons, sizeof f->weapons}};
}

/* Opens the in-memory files (after the fixture has its final address). */
static inline int cw_open(crew_world *f) {
    f->files[0].data = (es2_span){f->herc, sizeof f->herc};
    f->files[1].data = (es2_span){f->damage, sizeof f->damage};
    f->files[2].data = (es2_span){f->weapons, sizeof f->weapons};
    f->source.kind = ES2_VFS_LOOSE;
    f->source.data.loose.files = f->files;
    f->source.data.loose.count = 3;
    if (es2_vfs_open(&f->source, 1, &f->vfs, NULL) != ES2_OK) return 1;
    return cw_compose(f);
}

/* ---- the SAV, read back ----------------------------------------------------------- */
static const uint8_t *cw_suffix(crew_world *f, int i, const char **name, int16_t *portrait) {
    es2_sav_pilot p;
    if (i == 36) p = f->sav.player;
    else if (es2_sav_pilot_at(&f->sav, (size_t)i, &p, NULL) != ES2_OK) return NULL;
    if (name) {
        size_t n = p.name.size < sizeof f->name[i] - 1 ? p.name.size : sizeof f->name[i] - 1;
        memcpy(f->name[i], p.name.data, n);
        f->name[i][n] = 0;
        *name = f->name[i];
    }
    if (portrait) *portrait = (int16_t)cw_s(p.prefix.data);
    return p.suffix.data;
}
/* roster index of group g's selected pilot */
static inline int cw_selected(const crew_world *f, int g) { return 12 * g + cw_s(f->sav.roster_selection.data + 2 * g); }
static const uint8_t *cw_fixed(crew_world *f, int k) {
    for (size_t i = 0; i < f->sav.loadout_count; i++) {
        es2_sav_loadout l;
        if (es2_sav_loadout_at(&f->sav, i, &l, NULL) == ES2_OK && l.index == k) return l.fixed.data;
    }
    return NULL;
}
static inline int16_t cw_word(crew_world *f, int pilot, unsigned offset) {
    const uint8_t *x = cw_suffix(f, pilot, NULL, NULL);
    return x ? (int16_t)cw_s(x + offset - 0x22) : -1;
}
static inline int cw_active(crew_world *f, int pilot) {
    const uint8_t *x = cw_suffix(f, pilot, NULL, NULL);
    return x ? x[2] : -1;
}
static inline int16_t cw_count(const crew_world *f) { return (int16_t)cw_s(f->sav.team.data + 2); }

/* ---- the host ------------------------------------------------------------------------ */
static inline void cw_view(crew_world *f, int i, es2_crew_pilot *p) {
    const uint8_t *x = cw_suffix(f, i, &p->name, &p->portrait);
    if (!x) return;
    p->bay = (int16_t)cw_s(x);
    p->skill = (int16_t)cw_s(x + 3);
    p->order = (int16_t)cw_s(x + 5);
}
static inline int cw_team(void *ctx, es2_crew_team *t) {
    crew_world *f = ctx;
    if (!f->ok) return 0;
    t->capacity = (int16_t)cw_s(f->sav.team.data);
    cw_view(f, 36, &t->player);
    for (int g = 0; g < 3; g++) cw_view(f, cw_selected(f, g), &t->group[g]);
    for (int k = 0; k < 8; k++) {
        const uint8_t *b = cw_fixed(f, k);
        t->vehicle[k] = b ? (int16_t)cw_s(b) : -1;
    }
    return 1;
}
static inline es2_status cw_edit(void *ctx, es2_shell_editor_session *session, es2_shell_editor_op *op) {
    crew_world *f = ctx;
    es2_shell_buffer out = {NULL, 0};
    es2_error e;
    es2_status st;
    f->edits++;
    st = es2_shell_editors(&f->vfs, (es2_span){f->tables, 322}, (es2_span){f->editor, 98}, (es2_span){f->image, f->size},
                           session, op, 1, &out, NULL, &e);
    if (st == ES2_OK && out.size <= sizeof f->image) {
        memcpy(f->image, out.data, out.size);
        f->size = out.size;
        f->ok = es2_sav_parse((es2_span){f->image, f->size}, &f->sav, &e) == ES2_OK;
    } else {
        f->failures++;
    }
    free(out.data);
    return st;
}
/* the squad panel's view of bay k (0x410844: the player, else the first
 * selected pilot with that bay) */
static inline int cw_bay_view(void *ctx, int k, es2_squad_bay *out) {
    crew_world *f = ctx;
    const uint8_t *b;
    int pilot = -1;
    if (!f->ok || !(b = cw_fixed(f, k))) return 0;
    out->record = 1;
    out->finished = cw_s(b + 70) == 100;
    out->vehicle = (int16_t)cw_s(b);
    snprintf(f->herc_name[k], sizeof f->herc_name[k], "HERC%d", k);
    out->herc = f->herc_name[k];
    out->condition = (int16_t)cw_s(b + 4);
    out->progress = (int16_t)cw_s(b + 70);
    if (cw_word(f, 36, 0x22) == k) pilot = 36;
    for (int g = 0; g < 3 && pilot < 0; g++)
        if (cw_word(f, cw_selected(f, g), 0x22) == k) pilot = cw_selected(f, g);
    if (pilot >= 0) {
        cw_suffix(f, pilot, &out->pilot, NULL);
        out->skill = cw_word(f, pilot, 0x25);
    }
    return 1;
}

#endif
