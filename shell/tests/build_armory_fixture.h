#ifndef ES2_SHELL_TESTS_BUILD_ARMORY_FIXTURE_H
#define ES2_SHELL_TESTS_BUILD_ARMORY_FIXTURE_H

/* A synthetic loaded game for the BUILD and ARMORY screens' tests (no
 * game data): the editor library's tables, GAM/HERC_INF.DAT, DAMAGE.DAT and
 * WEAPONS.DAT, a SAV composed with es2_sav_compose, gam\arm_weap.dat and
 * the nine gam\rpr_*.dat layer lists, small seeded images for the DBA
 * members. Every value is invented for the tests. Header-only, used by
 * test_build_armory.c and screen_build_armory.c. */
#include <stdlib.h>
#include <string.h>

#include "es2/formats/sav.h"
#include "es2/formats/vfs.h"
#include "es2/shell/gui/build_armory.h"

typedef struct baf {
    uint8_t tables[322], editor[98], herc[2 + 144], damage[102], weapons[2 + 33 * 11 + 2];
    uint8_t inventory[33][10 * 8];
    size_t inventory_count[33];
    uint8_t flags[33];
    uint8_t five[22], selection[152], choice[12], team[4], nine[18], tally[4], final[22], variables[2000];
    uint8_t prefixes[37][4], suffixes[37][25], bays[8][196];
    size_t bay_items[8];
    int bay_present[8];
    /* files of the host */
    uint8_t arm_weap[2 + 26 * 12], rpr[9][2 + 3 * 14];
    uint8_t images[8][40 * 20];
    es2_bitmap bitmaps[8];
    /* the vfs */
    es2_vfs_file files[3];
    es2_vfs_source source;
    es2_vfs vfs;
} baf;

static inline void baf_w(uint8_t *p, unsigned x) {
    p[0] = (uint8_t)x;
    p[1] = (uint8_t)(x >> 8);
}
static inline void baf_w32(uint8_t *p, uint32_t x) {
    baf_w(p, x & 65535u);
    baf_w(p + 2, x >> 16);
}
static inline void baf_pilot(uint8_t *x, int bay, int active, int order) {
    memset(x, 0, 25);
    baf_w(x, (unsigned)bay);
    x[2] = (uint8_t)active;
    baf_w(x + 5, (unsigned)order);
    baf_w(x + 9, 100);
}
static inline void baf_bay(baf *f, int index, int vehicle, int capacity) {
    uint8_t *b = f->bays[index];
    memset(b, 0, 196);
    baf_w(b, (unsigned)vehicle);
    baf_w(b + 2, (unsigned)vehicle);
    for (int i = 0; i < 33; i++) baf_w(b + 4 + 2 * i, 100);
    baf_w(b + 70, 100);
    baf_w(b + 74, (unsigned)capacity);
    f->bay_present[index] = 1;
    f->bay_items[index] = 0;
}
static inline void baf_item(baf *f, int category) {
    uint8_t *p = f->inventory[category] + 10 * f->inventory_count[category]++;
    baf_w(p, (unsigned)category);
    baf_w(p + 2, (unsigned)category);
    baf_w(p + 4, 100);
    baf_w(p + 6, 100);
    baf_w(p + 8, 5);
}
static inline void baf_salvage(baf *f, uint32_t t) { baf_w32(f->tally, t); }
/* bank entry i := category (0 free); the free count follows */
static inline void baf_bank(baf *f, int i, int category) {
    int free_count = 0;
    baf_w(f->five + 4 + 4 * i, (unsigned)category);
    for (int k = 0; k < 5; k++) free_count += f->five[4 + 4 * k] == 0 && f->five[5 + 4 * k] == 0;
    baf_w(f->five, (unsigned)free_count);
}

/* Herc types 0..8 (vehicle t, +2 = 30 + 5t, +4 = 60 + t, +6 = t, +8 = 100 +
 * 10t); the nine-word bank enables types 0, 1, 2, 4; WEAPONS: category c
 * costs (1 + c % 4) * 1000; ARMORY row r = category r + 1, flags set for
 * every category except 5 and 9; two items of category 1, one of 3; bay 0:
 * the player's Herc of vehicle 1, bay 1 a wingman's, bays 2..7 empty;
 * salvage 50,000; the bank: one entry of category 2, free 4. */
static inline void baf_init(baf *f) {
    static const int groups[24] = {0, 1, -1, 0, 2, 4, -1, 0, 3, 5, -1, 0, 6, -1, 0, 0, 7, 9, 11, -1, 8, 10, 12, -1};
    static const int t[] = {90, 80, 60, 30, 1, 0}, l[] = {100, 89, 79, 59, 29, 0}, band[] = {89, 79, 59, 29, 0},
                     cap[] = {3, 5, 5, 8, 9, 9, 10, 4, 7};
    uint8_t *p;
    memset(f, 0, sizeof *f);
    for (int i = 0; i < 24; i++) baf_w(f->tables + 2 * i, (unsigned)groups[i]);
    for (int i = 0; i < 6; i++) {
        baf_w(f->tables + 48 + 2 * i, (unsigned)t[i]);
        baf_w(f->tables + 60 + 2 * i, (unsigned)l[i]);
    }
    for (int i = 0; i < 33; i++) baf_w(f->tables + 138 + 2 * i, 65535);
    for (int i = 0; i < 30; i++) baf_w(f->tables + 262 + 2 * i, (unsigned)i);
    for (int i = 0; i < 5; i++) baf_w(f->editor + 2 * i, (unsigned)band[i]);
    for (int i = 0; i < 9; i++) {
        baf_w(f->editor + 10 + 2 * i, (unsigned)i);
        baf_w(f->editor + 28 + 2 * i, (unsigned)cap[i]);
    }
    for (int i = 0; i < 26; i++) baf_w(f->editor + 46 + 2 * i, (unsigned)(i + 1));
    baf_w(f->herc, 9);
    for (int i = 0; i < 9; i++) {
        p = f->herc + 2 + 16 * i;
        baf_w(p, (unsigned)i);
        baf_w(p + 2, (unsigned)(30 + 5 * i));
        baf_w(p + 4, (unsigned)(60 + i));
        baf_w(p + 6, (unsigned)i);
        baf_w(p + 8, (unsigned)(100 + 10 * i));
        baf_w(p + 12, (unsigned)(2 + i));
    }
    baf_w(f->damage, 1024);
    for (int i = 0; i < 15; i++) baf_w(f->damage + 2 + 2 * i, 1);
    baf_w(f->damage + 32, 1024);
    baf_w(f->damage + 34, 33);
    for (int i = 0; i < 33; i++) baf_w(f->damage + 36 + 2 * i, 100);
    p = f->weapons;
    baf_w(p, 33);
    p += 2;
    for (int i = 0; i < 33; i++) {
        baf_w(p, (unsigned)i);
        baf_w(p + 2, 2);
        p[4] = 'W';
        p[5] = 0;
        baf_w(p + 6, (unsigned)(1 + i % 4));
        p[8] = 1;
        baf_w(p + 9, i ? (unsigned)i : 99u);
        p += 11;
    }
    baf_w(p, 0);
    for (int i = 0; i < 33; i++) f->flags[i] = i != 5 && i != 9;
    baf_item(f, 1);
    baf_item(f, 1);
    baf_item(f, 3);
    for (int i = 0; i < 5; i++) baf_w(f->five + 2 + 4 * i, (unsigned)i);
    baf_bank(f, 0, 2);
    for (int i = 0; i < 36; i++) baf_pilot(f->suffixes[i], -1, 0, -1);
    baf_pilot(f->suffixes[0], 1, 1, 1);
    baf_pilot(f->suffixes[36], 0, 1, 0);
    for (int i = 0; i < 3; i++) {
        baf_w(f->choice + 2 * i, 0);
        baf_w(f->choice + 6 + 2 * i, 1);
    }
    baf_w(f->team, 4);
    baf_w(f->team + 2, 2);
    baf_w(f->nine, 1);
    baf_w(f->nine + 2, 1);
    baf_w(f->nine + 4, 1);
    baf_w(f->nine + 8, 1);
    baf_bay(f, 0, 1, 3);
    baf_bay(f, 1, 2, 2);
    baf_salvage(f, 50000);
    /* host files: 26 images, one per row (frame r % 4); three layers per type */
    baf_w(f->arm_weap, 26);
    for (int r = 0; r < 26; r++) {
        p = f->arm_weap + 2 + 12 * r;
        baf_w(p, (unsigned)(r + 1));
        baf_w32(p + 2, (uint32_t)(20 + (r % 4) * 70));
        baf_w32(p + 6, (uint32_t)(10 + (r % 3) * 30));
        baf_w(p + 10, (unsigned)(r % 4));
    }
    for (int k = 0; k < 9; k++) {
        baf_w(f->rpr[k], 3);
        for (int i = 0; i < 3; i++) {
            p = f->rpr[k] + 2 + 14 * i;
            baf_w(p, (unsigned)i);
            baf_w32(p + 2, (uint32_t)(10 + 30 * i + 4 * k));
            baf_w32(p + 6, (uint32_t)(20 + 50 * i));
            baf_w(p + 10, (unsigned)(4 + (i + k) % 4));
            baf_w(p + 12, (unsigned)(i == 2));
        }
    }
    for (int m = 0; m < 8; m++) {
        for (int i = 0; i < 40 * 20; i++) f->images[m][i] = (uint8_t)((i * 7 + m * 13) % 23 ? 0x30 + m * 9 + i % 5 : 0);
        f->bitmaps[m] = (es2_bitmap){20, 40, 0, f->images[m]};
    }
}

static inline size_t baf_compose(baf *f, uint8_t *image, size_t capacity) {
    es2_sav_image m;
    es2_sav_pilot roster[37];
    es2_sav_loadout loadouts[8];
    static uint8_t name[2] = {'P', 0};
    size_t n = 0, required = 0;
    memset(&m, 0, sizeof m);
    for (int i = 0; i < 33; i++) {
        m.inventory[i].flag = f->flags[i];
        m.inventory[i].count = f->inventory_count[i];
        m.inventory[i].items = (es2_span){f->inventory[i], 10 * f->inventory_count[i]};
    }
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
    if (es2_sav_compose(&m, image, capacity, &required, NULL) != ES2_OK) return 0;
    return required;
}

/* The game over the fixture: the vfs, the tables, a malloc'd SAV, the
 * session (image values, bay `bay`). 0 = ok. */
static inline int baf_game(baf *f, es2_build_armory_game *g, es2_shell_editor_session *session, int16_t bay) {
    static uint8_t image[16384];
    size_t n = baf_compose(f, image, sizeof image);
    memset(g, 0, sizeof *g);
    if (!n) return -1;
    f->files[0] = (es2_vfs_file){"GAM/HERC_INF.DAT", {f->herc, sizeof f->herc}};
    f->files[1] = (es2_vfs_file){"GAM/DAMAGE.DAT", {f->damage, sizeof f->damage}};
    f->files[2] = (es2_vfs_file){"GAM/WEAPONS.DAT", {f->weapons, sizeof f->weapons}};
    f->source.kind = ES2_VFS_LOOSE;
    f->source.data.loose.files = f->files;
    f->source.data.loose.count = 3;
    if (es2_vfs_open(&f->source, 1, &f->vfs, NULL) != ES2_OK) return -1;
    g->vfs = &f->vfs;
    g->tables = (es2_span){f->tables, sizeof f->tables};
    g->editor_tables = (es2_span){f->editor, sizeof f->editor};
    if (!(g->save.data = malloc(n))) return -1;
    memcpy(g->save.data, image, n);
    g->save.size = n;
    es2_shell_editor_session_init(session, bay);
    g->session = session;
    return 0;
}

/* Host file and bitmap lookups over the fixture. */
static inline es2_span baf_file(baf *f, const char *name) {
    es2_span none = {NULL, 0};
    if (!strcmp(name, "gam\\arm_weap.dat")) return (es2_span){f->arm_weap, sizeof f->arm_weap};
    for (int t = 0; t < 9; t++)
        if (!strcmp(name, es2_build_picture_file(t, 0))) return (es2_span){f->rpr[t], sizeof f->rpr[t]};
    return none;
}
static inline const es2_bitmap *baf_bitmap(baf *f, const char *file, int m) {
    if (!strcmp(file, "dba\\arm_weap.dba") && m >= 0 && m < 4) return &f->bitmaps[m];
    for (int t = 0; t < 9; t++)
        if (!strcmp(file, es2_build_picture_file(t, 1)) && m >= 4 && m < 8) return &f->bitmaps[m];
    return NULL;
}

#endif
