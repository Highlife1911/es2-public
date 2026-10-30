#ifndef ES2_SHELL_TESTS_WEAPONS_FIXTURE_H
#define ES2_SHELL_TESTS_WEAPONS_FIXTURE_H

/* Synthetic state for the WEAPONS screen's tests (no game data): an
 * engine over a composed SAV, a synthetic row table and synthetic ARM
 * answers, a synthetic gam\arm_weap.dat with seeded frames. None of the
 * numbers are VSHELL's or the CD's. Header-only, used by test_weapons.c and
 * screen_weapons.c. */
#include <stdint.h>
#include <string.h>

#include "es2/formats/sav.h"
#include "es2/shell/engine.h"

enum { WPF_FRAMES = 10 };

typedef struct wpf {
    es2_shell_engine_tables tables;
    es2_shell_hardpoints hardpoints;
    es2_vfs vfs;
    es2_shell_engine engine;
    uint8_t sav[8192];
    size_t sav_size;
    uint8_t arm_weap[512];
    size_t arm_weap_size;
    uint8_t pixels[WPF_FRAMES][40 * 30];
    es2_bitmap frames[WPF_FRAMES];
} wpf;

static inline void wpf_w16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static inline void wpf_w32(uint8_t *p, uint32_t v) { wpf_w16(p, v & 0xffffu); wpf_w16(p + 2, v >> 16); }

/* Row r holds ID r + 1 (rows 0..25), row 26 ID 0. */
static inline int wpf_row(int id) { return id == 0 ? 26 : id - 1; }

/* The SAV: bay 0 vehicle 1, three slots, finished, slot 0 item 5, slot 2
 * rack 13 with guidance 2; bay 1 vehicle 2, two slots, under construction;
 * bay 2 vehicle 1, three slots, finished, slot 1 item 30 (no list row). Inventory: 0 available;
 * 5 available, two items; 7 not available, one item; 13 available, none;
 * 14 available, one; 20 available, three. ARM: vehicle 1 takes 0 everywhere,
 * 5 in slots 0 and 1, 7 in 0, 13 in 1 and 2, 14 in 2, 20 in 1; vehicle 2
 * takes 0 and 5 in slot 1. `finished0` 0 makes bay 0 unfinished too. */
static inline int wpf_compose(wpf *f, int finished0) {
    static uint8_t inventory[33][40], prefixes[37][4], suffixes[37][25], bays[3][76 + 12 * 3];
    static uint8_t five[22], selection[152], choice[12], team[4], nine[18], tally[4], final_[22], variables[2000];
    static uint8_t name[2] = {'P', 0}, other[2] = {'Q', 0};
    size_t counts[33], items[3] = {2, 0, 1};
    uint8_t flags[33];
    es2_sav_image m;
    es2_sav_pilot pilots[37];
    es2_sav_loadout loadouts[3];
    memset(inventory, 0, sizeof inventory);
    memset(bays, 0, sizeof bays);
    memset(counts, 0, sizeof counts);
    memset(flags, 0, sizeof flags);
    memset(&m, 0, sizeof m);
    flags[0] = flags[5] = flags[13] = flags[14] = flags[20] = 1;
    counts[5] = 2; counts[7] = 1; counts[14] = 1; counts[20] = 3;
    for (int id = 0; id < 33; id++)
        for (size_t k = 0; k < counts[id]; k++) {
            uint8_t *r = inventory[id] + 10 * k;
            wpf_w16(r, (unsigned)id); wpf_w16(r + 2, (unsigned)id); wpf_w16(r + 4, (unsigned)(90 - 10 * k));
            wpf_w16(r + 6, 100); wpf_w16(r + 8, 5);
        }
    for (int i = 0; i < 33; i++) {
        m.inventory[i].flag = flags[i];
        m.inventory[i].count = counts[i];
        m.inventory[i].items = (es2_span){inventory[i], 10 * counts[i]};
    }
    for (int i = 0; i < 37; i++) {
        memset(suffixes[i], 0, 25);
        wpf_w16(suffixes[i], 0xffff);
        wpf_w16(suffixes[i] + 5, 0xffff);
        pilots[i].prefix = (es2_span){prefixes[i], 4};
        pilots[i].name = (es2_span){i == 36 ? name : other, 2};
        pilots[i].suffix = (es2_span){suffixes[i], 25};
        pilots[i].offset = 0;
    }
    wpf_w16(suffixes[36], 0);
    static const int vehicle[3] = {1, 2, 1}, capacity[3] = {3, 2, 3};
    for (int b = 0; b < 3; b++) {
        wpf_w16(bays[b], (unsigned)vehicle[b]);
        wpf_w16(bays[b] + 70, b == 1 || (b == 0 && !finished0) ? 40 : 100);
        wpf_w16(bays[b] + 74, (unsigned)capacity[b]);
    }
    /* bay 0: slot 0 item 5 (word +8 = 5), slot 2 rack 13 (guidance 2) */
    wpf_w16(bays[0] + 76, 0); wpf_w16(bays[0] + 78, 5); wpf_w16(bays[0] + 82, 95); wpf_w16(bays[0] + 86, 5);
    wpf_w16(bays[0] + 88, 2); wpf_w16(bays[0] + 90, 13); wpf_w16(bays[0] + 94, 70); wpf_w16(bays[0] + 98, 2);
    /* bay 2: slot 1 item 30, an ID of no list row */
    wpf_w16(bays[2] + 76, 1); wpf_w16(bays[2] + 78, 30); wpf_w16(bays[2] + 82, 60); wpf_w16(bays[2] + 86, 5);
    for (int b = 0; b < 3; b++) {
        loadouts[b].index = (int16_t)b;
        loadouts[b].fixed = (es2_span){bays[b], 76};
        loadouts[b].items = (es2_span){bays[b] + 76, 12 * items[b]};
        loadouts[b].count = items[b];
        loadouts[b].offset = 0;
    }
    m.five_entry_bank = (es2_span){five, 22};
    m.selection = (es2_span){selection, 152};
    m.roster = pilots;
    m.roster_selection = (es2_span){choice, 12};
    m.team = (es2_span){team, 4};
    m.player = &pilots[36];
    m.loadout_count = 3;
    m.loadouts = loadouts;
    m.nine_word_bank = (es2_span){nine, 18};
    m.tally = (es2_span){tally, 4};
    m.variables = (es2_span){variables, 2000};
    m.final_fields = (es2_span){final_, 22};
    wpf_w16(selection, 1);
    wpf_w16(team, 1); wpf_w16(team + 2, 1);
    wpf_w16(final_, 2);
    f->sav_size = 0;
    return es2_sav_compose(&m, f->sav, sizeof f->sav, &f->sav_size, NULL) == ES2_OK ? 0 : -1;
}

/* arm_weap.dat: list 1 one record per ID 1..26 except 20 (no image), list 2
 * the modes 0..3; frames 0..9 of 12 + k by 8 + k seeded pixels. */
static inline void wpf_files(wpf *f) {
    size_t at = 2;
    int n = 0;
    for (int id = 1; id <= 26; id++) {
        if (id == 20) continue;
        wpf_w16(f->arm_weap + at, (unsigned)id);
        wpf_w32(f->arm_weap + at + 2, (uint32_t)(20 + 9 * (id % 12)));
        wpf_w32(f->arm_weap + at + 6, (uint32_t)(6 + 7 * (id % 4)));
        wpf_w16(f->arm_weap + at + 10, (unsigned)(id % 6));
        at += 12;
        n++;
    }
    wpf_w16(f->arm_weap, (unsigned)n);
    wpf_w16(f->arm_weap + at, 4);
    at += 2;
    for (int m = 0; m < 4; m++) {
        wpf_w16(f->arm_weap + at, (unsigned)m);
        wpf_w32(f->arm_weap + at + 2, (uint32_t)(150 + 20 * m));
        wpf_w32(f->arm_weap + at + 6, 30);
        wpf_w16(f->arm_weap + at + 10, (unsigned)(6 + m));
        at += 12;
    }
    f->arm_weap_size = at;
    for (int k = 0; k < WPF_FRAMES; k++) {
        int w = 12 + k, h = 8 + k;
        for (int i = 0; i < w * h; i++) f->pixels[k][i] = (uint8_t)((i * 7 + k * 13) % 5 ? 96 + (i + k) % 40 : 0);
        f->frames[k] = (es2_bitmap){(uint16_t)h, (uint16_t)w, 0, f->pixels[k]};
    }
}

/* The engine with the state loaded. 0 = ok. */
static inline int wpf_open(wpf *f, int finished0) {
    es2_shell_engine_free(&f->engine);
    memset(&f->engine, 0, sizeof f->engine);
    memset(&f->tables, 0, sizeof f->tables);
    memset(&f->hardpoints, 0, sizeof f->hardpoints);
    memset(&f->vfs, 0, sizeof f->vfs);
    for (int i = 0; i < 112; i++) f->tables.prepare[i] = (uint8_t)(5 * i + 3);
    for (int r = 0; r < 26; r++) f->tables.rows[r] = (int16_t)(r + 1);
    f->tables.rows[26] = 0;
    f->hardpoints.slots[1][0] = 7; f->hardpoints.slots[1][5] = 3; f->hardpoints.slots[1][7] = 1;
    f->hardpoints.slots[1][13] = 6; f->hardpoints.slots[1][14] = 4; f->hardpoints.slots[1][20] = 2;
    f->hardpoints.slots[2][0] = 3; f->hardpoints.slots[2][5] = 2;
    f->engine.vfs = &f->vfs;
    f->engine.tables = &f->tables;
    f->engine.hardpoints = &f->hardpoints;
    memcpy(f->engine.language, "ENG", 4);
    wpf_files(f);
    if (wpf_compose(f, finished0)) return -1;
    if (es2_shell_engine_start(&f->engine, 0, NULL) != ES2_OK) return -2;
    return es2_shell_engine_set(&f->engine, (es2_span){f->sav, f->sav_size}, NULL) == ES2_OK ? 0 : -3;
}

#endif
