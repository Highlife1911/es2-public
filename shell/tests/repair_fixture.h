#ifndef ES2_SHELL_TESTS_REPAIR_FIXTURE_H
#define ES2_SHELL_TESTS_REPAIR_FIXTURE_H

/* A synthetic memory state for the REPAIR screen's tests (no game
 * data): the executable tables, GAM/HERC_INF.DAT, GAM/DAMAGE.DAT and
 * GAM/WEAPONS.DAT, and a SAV composed with the formats writer. Every value is
 * invented for the tests (the shapes follow test_editors.c). Header
 * only, used by test_repair_screen.c and screen_repair.c.
 *
 * Bay 0: the player's, vehicle 1, capacity 3, item 3 in slot 0 and item 5 in
 * slot 2 (slot 1 empty), damaged; bay 1: roster pilot 0 (order 1), vehicle 8,
 * capacity 2, damaged; bay 2: roster pilot 12 (order 2), vehicle 0, intact.
 * Salvage 100,000; the five-entry bank holds categories 0..4 (cost 0, 2,000
 * each: 8,000). */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "es2/formats/sav.h"
#include "es2/formats/vfs.h"
#include "es2/shell/campaign.h"
#include "es2/shell/editors.h"

typedef struct repair_fixture {
    uint8_t tables[ES2_SHELL_RESULTS_TABLES_SIZE], editor[ES2_SHELL_EDITOR_TABLES_SIZE];
    uint8_t herc[2 + 144], damage[102], weapons[2 + 33 * 11 + 2];
    uint8_t flags[33], five[22], selection[152], choice[12], team[4], nine[18], tally[4], final[22];
    uint8_t variables[2000], prefixes[37][4], suffixes[37][25], bays[8][196];
    size_t bay_items[8];
    int bay_present[8];
    es2_vfs_file files[3];
    es2_vfs_source source;
    es2_vfs vfs;
} repair_fixture;

static inline void rf_w(uint8_t *p, unsigned x) { p[0] = (uint8_t)x; p[1] = (uint8_t)(x >> 8); }
static inline void rf_pilot(uint8_t *x, int bay, int active, int order) {
    memset(x, 0, 25);
    rf_w(x, (unsigned)bay);
    x[2] = (uint8_t)active;
    rf_w(x + 5, (unsigned)order);
    rf_w(x + 9, 100);
}
static inline void rf_bay(repair_fixture *f, int index, int vehicle, int capacity, int condition) {
    uint8_t *b = f->bays[index];
    memset(b, 0, 196);
    rf_w(b, (unsigned)vehicle);
    rf_w(b + 2, (unsigned)vehicle);
    for (int i = 0; i < 33; i++) rf_w(b + 4 + 2 * i, (unsigned)condition);
    rf_w(b + 70, 100);
    rf_w(b + 74, (unsigned)capacity);
    f->bay_present[index] = 1;
    f->bay_items[index] = 0;
}
static inline void rf_cond(repair_fixture *f, int index, int word, int value) {
    rf_w(f->bays[index] + 4 + 2 * word, (unsigned)value);
}
static inline void rf_mount(repair_fixture *f, int index, int slot, int item, int condition) {
    uint8_t *r = f->bays[index] + 76 + 12 * f->bay_items[index]++;
    rf_w(r, (unsigned)slot);
    rf_w(r + 2, (unsigned)item);
    rf_w(r + 4, (unsigned)item);
    rf_w(r + 6, 100);
    rf_w(r + 8, 100);
    rf_w(r + 10, 5);
    rf_cond(f, index, 23 + slot, condition);
}
static inline void rf_salvage(repair_fixture *f, uint32_t t) {
    rf_w(f->tally, t & 65535u);
    rf_w(f->tally + 2, t >> 16);
}

static inline void rf_init(repair_fixture *f) {
    static const int groups[24] = {0, 1, -1, 0, 2, 4, -1, 0, 3, 5, -1, 0, 6, -1, 0, 0, 7, 9, 11, -1, 8, 10, 12, -1};
    static const int t[] = {90, 80, 60, 30, 1, 0}, l[] = {100, 89, 79, 59, 29, 0}, band[] = {89, 79, 59, 29, 0},
                     cap[] = {3, 5, 5, 8, 9, 9, 10, 4, 7};
    static const int damage[13] = {95, 85, 70, 50, 20, 0, 100, 88, 77, 66, 55, 44, 33};
    uint8_t *p;
    memset(f, 0, sizeof *f);
    for (int i = 0; i < 24; i++) rf_w(f->tables + 2 * i, (unsigned)groups[i]);
    for (int i = 0; i < 6; i++) {
        rf_w(f->tables + 48 + 2 * i, (unsigned)t[i]);
        rf_w(f->tables + 60 + 2 * i, (unsigned)l[i]);
    }
    for (int i = 0; i < 33; i++) rf_w(f->tables + 138 + 2 * i, 65535);
    for (int i = 0; i < 30; i++) rf_w(f->tables + 262 + 2 * i, (unsigned)i);
    for (int i = 0; i < 5; i++) rf_w(f->editor + 2 * i, (unsigned)band[i]);
    for (int i = 0; i < 9; i++) {
        rf_w(f->editor + 10 + 2 * i, (unsigned)i);
        rf_w(f->editor + 28 + 2 * i, (unsigned)cap[i]);
    }
    for (int i = 0; i < 26; i++) rf_w(f->editor + 46 + 2 * i, (unsigned)(i + 1));
    rf_w(f->herc, 9);
    for (int i = 0; i < 9; i++) {
        p = f->herc + 2 + 16 * i;
        rf_w(p, (unsigned)i);
        rf_w(p + 8, 100);
        rf_w(p + 12, (unsigned)(2 + i));
    }
    rf_w(f->damage, 1024);
    for (int i = 0; i < 15; i++) rf_w(f->damage + 2 + 2 * i, 1);
    rf_w(f->damage + 32, 1024);
    rf_w(f->damage + 34, 33);
    for (int i = 0; i < 33; i++) rf_w(f->damage + 36 + 2 * i, 100);
    p = f->weapons;
    rf_w(p, 33);
    p += 2;
    for (int i = 0; i < 33; i++) {
        rf_w(p, (unsigned)i);
        rf_w(p + 2, 2);
        p[4] = 'W';
        p[5] = 0;
        rf_w(p + 6, 2);
        p[8] = 1;
        rf_w(p + 9, i ? (unsigned)i : 99u);
        p += 11;
    }
    rf_w(p, 0);
    for (int i = 0; i < 33; i++) f->flags[i] = 1;
    rf_w(f->five, 5);
    for (int i = 0; i < 5; i++) rf_w(f->five + 2 + 4 * i, (unsigned)i);
    for (int i = 0; i < 36; i++) rf_pilot(f->suffixes[i], -1, 0, -1);
    rf_pilot(f->suffixes[0], 1, 1, 1);
    rf_pilot(f->suffixes[12], 2, 1, 2);
    rf_pilot(f->suffixes[36], 0, 1, 0);
    for (int i = 0; i < 3; i++) {
        rf_w(f->choice + 2 * i, 0);
        rf_w(f->choice + 6 + 2 * i, 1);
    }
    rf_w(f->team, 4);
    rf_w(f->team + 2, 3);
    for (int i = 0; i < 9; i++) rf_w(f->nine + 2 * i, 1);
    rf_bay(f, 0, 1, 3, 100);
    for (int i = 0; i < 13; i++) rf_cond(f, 0, i, damage[i]);
    rf_cond(f, 0, 13, 45);
    rf_cond(f, 0, 18, 75);
    rf_cond(f, 0, 15, 89); /* band limits: 89 is band 1, 0 band 5 */
    rf_cond(f, 0, 16, 0);
    rf_mount(f, 0, 0, 3, 64);
    rf_mount(f, 0, 2, 5, 100);
    rf_bay(f, 1, 8, 2, 60);
    rf_cond(f, 1, 13, 51);
    rf_cond(f, 1, 14, 51);
    rf_cond(f, 1, 18, 51);
    rf_cond(f, 1, 21, 51);
    rf_bay(f, 2, 0, 3, 100);
    rf_salvage(f, 100000);
}

/* The SAV of the fixture (malloc'd; *size its length); NULL on failure. */
static inline uint8_t *rf_save(repair_fixture *f, size_t *size) {
    es2_sav_image m;
    es2_sav_pilot roster[37];
    es2_sav_loadout loadouts[8];
    static uint8_t name[2] = {'P', 0};
    size_t n = 0, required = 0;
    uint8_t *out;
    memset(&m, 0, sizeof m);
    for (int i = 0; i < 33; i++) m.inventory[i].flag = f->flags[i];
    for (int i = 0; i < 37; i++) {
        roster[i].prefix = (es2_span){f->prefixes[i], 4};
        roster[i].name = (es2_span){name, 2};
        roster[i].suffix = (es2_span){f->suffixes[i], 25};
        roster[i].offset = 0;
    }
    for (int i = 0; i < 8; i++)
        if (f->bay_present[i]) {
            loadouts[n].index = (int16_t)i;
            loadouts[n].fixed = (es2_span){f->bays[i], 76};
            loadouts[n].items = (es2_span){f->bays[i] + 76, 12 * f->bay_items[i]};
            loadouts[n].count = f->bay_items[i];
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
    if (es2_sav_compose(&m, NULL, 0, &required, NULL) != ES2_OK && !required) return NULL;
    if (!(out = malloc(required))) return NULL;
    if (es2_sav_compose(&m, out, required, &required, NULL) != ES2_OK) {
        free(out);
        return NULL;
    }
    *size = required;
    return out;
}

/* The fixture's three game files as a loose VFS (borrowing f). 0 = ok. */
static inline int rf_vfs(repair_fixture *f) {
    f->files[0] = (es2_vfs_file){"GAM/HERC_INF.DAT", {f->herc, sizeof f->herc}};
    f->files[1] = (es2_vfs_file){"GAM/DAMAGE.DAT", {f->damage, sizeof f->damage}};
    f->files[2] = (es2_vfs_file){"GAM/WEAPONS.DAT", {f->weapons, sizeof f->weapons}};
    f->source.kind = ES2_VFS_LOOSE;
    f->source.data.loose.files = f->files;
    f->source.data.loose.count = 3;
    return es2_vfs_open(&f->source, 1, &f->vfs, NULL) == ES2_OK ? 0 : -1;
}

#endif
