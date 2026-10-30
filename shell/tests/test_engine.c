#include "es2/shell/engine.h"
#include "es2/shell/practice.h"
#include "es2/formats/sav.h"
#include "es2/formats/vfs.h"
#include "vshell_file.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Engine (es2/shell/engine.h) on synthetic data only: an invented PE
 * image for the table reader, invented ARM files, a composed SAV. The
 * operations that need the CD (new game, preparation, results path) are
 * covered by //shell:menu_real_test against the original. */
static unsigned checks, failures;
#define CHECK(x) do { checks++; if (!(x)) { failures++; fprintf(stderr, "line %d: %s\n", __LINE__, #x); } } while (0)
static void w16(uint8_t *p, unsigned x) { p[0] = (uint8_t)x; p[1] = (uint8_t)(x >> 8); }
static void w32(uint8_t *p, uint32_t x) { w16(p, x & 0xffff); w16(p + 2, x >> 16); }

/* ---- ARM files ---------------------------------------------------------------- */
/* 44 bytes, groups, then (item, count, count x (slot + 2, 20 bytes)). */
static size_t arm(uint8_t *out, const int *spec) {
    size_t n = 44;
    int groups = *spec++;
    memset(out, 0, 512);
    w16(out + n, (unsigned)groups); n += 2;
    for (int g = 0; g < groups; g++) {
        int item = *spec++, count = *spec++;
        w16(out + n, (unsigned)item); w16(out + n + 2, (unsigned)count); n += 4;
        for (int k = 0; k < count; k++) { w16(out + n, (unsigned)(*spec++ + 2)); n += 22; }
    }
    return n;
}
static void arm_files(void) {
    uint8_t file[512];
    uint16_t slots[33];
    es2_error e;
    const int spec[] = {3, 0, 3, 0, 1, 2, 5, 2, 0, 2, 13, 1, 2};
    size_t n = arm(file, spec);
    CHECK(es2_shell_arm_parse((es2_span){file, n}, slots, &e) == ES2_OK);
    CHECK(slots[0] == 7 && slots[5] == 5 && slots[13] == 4 && slots[1] == 0);
    /* refusals: truncated, trailing byte, negative counts, no output */
    memset(slots, 0xee, sizeof slots);
    CHECK(es2_shell_arm_parse((es2_span){file, n - 1}, slots, &e) == ES2_ERR_TRUNCATED && slots[0] == 0xeeee);
    CHECK(es2_shell_arm_parse((es2_span){file, n + 1}, slots, &e) != ES2_OK && slots[0] == 0xeeee);
    w16(file + 44, 0xffff);
    CHECK(es2_shell_arm_parse((es2_span){file, 46}, slots, &e) == ES2_ERR_COUNT);
    w16(file + 44, 1); w16(file + 48, 0xffff);
    CHECK(es2_shell_arm_parse((es2_span){file, 50}, slots, &e) == ES2_ERR_COUNT);
    CHECK(es2_shell_arm_parse((es2_span){file, n}, NULL, &e) == ES2_ERR_ARGUMENT);
    /* items above 32 and slots outside 0..15 are ignored */
    const int odd[] = {2, 40, 1, 3, 4, 2, 20, 6};
    n = arm(file, odd);
    CHECK(es2_shell_arm_parse((es2_span){file, n}, slots, &e) == ES2_OK && slots[4] == 1u << 6 && slots[3] == 0);
}

/* ---- executable tables -------------------------------------------------------- */
enum { RAW = 0x400, SECTION_VA = 0x46f000, SECTION_SIZE = 0x11000 };
static uint8_t *pe_image(size_t *size) {
    uint8_t *p = calloc(1, RAW + SECTION_SIZE);
    *size = RAW + SECTION_SIZE;
    w32(p + 0x3c, 0x40);
    memcpy(p + 0x40, "PE\0\0", 4);
    w16(p + 0x40 + 6, 1);
    w16(p + 0x40 + 20, 224);
    w32(p + 0x40 + 24 + 28, 0x400000);
    uint8_t *h = p + 0x40 + 24 + 224;
    w32(h + 12, SECTION_VA - 0x400000);
    w32(h + 16, SECTION_SIZE);
    w32(h + 20, RAW);
    return p;
}
static uint8_t *at(uint8_t *image, uint32_t va) { return image + RAW + (va - SECTION_VA); }
static void fill_tables(uint8_t *image) {
    for (int i = 0; i < 112; i++) at(image, 0x47f850)[i] = (uint8_t)(i + 1);
    for (int i = 0; i < 16; i++) at(image, 0x46f72c)[i] = (uint8_t)(0x80 + i);
    for (int i = 0; i < 36; i++) at(image, 0x46f868)[i] = (uint8_t)(0x40 + i);
    for (int i = 0; i < 9; i++) {
        w32(at(image, 0x47083c) + 4 * i, 0x47fa00u + 64u * (uint32_t)i);
        snprintf((char *)at(image, 0x47fa00u + 64u * (uint32_t)i), 64, "PRESET%d.MEC", i);
        w32(at(image, 0x47006c) + 4 * i, 0x47fe00u + 32u * (uint32_t)i);
        snprintf((char *)at(image, 0x47fe00u + 32u * (uint32_t)i), 32, "ARM/ARM_%d.DAT", i);
    }
    for (int i = 0; i < 27; i++) w16(at(image, 0x476a4c) + 2 * i, (unsigned)(i < 4 ? (int[]){0, 5, 7, 13}[i] : 0));
    at(image, 0x46f9e4)[0] = 0xa1;   /* first results span */
    at(image, 0x46f9a8)[59] = 0xa7;  /* last byte of the last one */
}
static void tables(void) {
    size_t size;
    uint8_t *image = pe_image(&size);
    es2_shell_engine_tables t;
    es2_error e;
    memset(&t, 0x5a, sizeof t);
    CHECK(es2_shell_engine_tables_read((es2_span){image, size}, &t, &e) == ES2_ERR_STRING && t.rows[0] == 0x5a5a);
    fill_tables(image);
    CHECK(es2_shell_engine_tables_read((es2_span){image, size}, &t, &e) == ES2_OK);
    CHECK(t.prepare[0] == 1 && t.prepare[111] == 112 && t.prepare[112] == 0x80 && t.prepare[128] == 0x40 &&
          t.prepare[163] == 0x40 + 35);
    CHECK(!strcmp((char *)t.prepare + 164, "PRESET0.MEC") && !strcmp((char *)t.prepare + 164 + 64 * 8, "PRESET8.MEC") &&
          t.prepare[164 + 11] == 0 && t.prepare[ES2_SHELL_ENGINE_PREPARE_TABLES - 1] == 0);
    CHECK(t.results[0] == 0xa1 && t.results[ES2_SHELL_RESULTS_TABLES_SIZE - 1] == 0xa7);
    CHECK(t.rows[0] == 0 && t.rows[1] == 5 && t.rows[3] == 13 && !strcmp(t.arm[0], "ARM/ARM_0.DAT") && !strcmp(t.arm[8], "ARM/ARM_8.DAT"));
    /* an unterminated name, a pointer outside the image, no image */
    memset(at(image, 0x47fe00), 'X', 32);
    CHECK(es2_shell_engine_tables_read((es2_span){image, size}, &t, &e) == ES2_ERR_STRING);
    snprintf((char *)at(image, 0x47fe00), 32, "ARM/ARM_0.DAT");
    w32(at(image, 0x47006c), 0x700000);
    CHECK(es2_shell_engine_tables_read((es2_span){image, size}, &t, &e) != ES2_OK);
    w32(at(image, 0x47006c), 0x47fe00);
    CHECK(es2_shell_engine_tables_read((es2_span){NULL, 0}, &t, &e) == ES2_ERR_ARGUMENT);
    CHECK(es2_shell_engine_tables_read((es2_span){image, 0x300}, &t, &e) != ES2_OK);
    /* hardpoints from a loose VFS with the nine ARM files */
    {
        static uint8_t files[9][512];
        es2_vfs_file loose[9];
        es2_vfs_source source;
        es2_vfs vfs;
        es2_shell_hardpoints h;
        const int one[] = {1, 5, 1, 1}, none[] = {0};
        CHECK(es2_shell_engine_tables_read((es2_span){image, size}, &t, &e) == ES2_OK);
        for (int i = 0; i < 9; i++) {
            loose[i].path = t.arm[i];
            loose[i].data = (es2_span){files[i], arm(files[i], i == 2 ? one : none)};
        }
        source.kind = ES2_VFS_LOOSE;
        source.data.loose.files = loose;
        source.data.loose.count = 9;
        CHECK(es2_vfs_open(&source, 1, &vfs, &e) == ES2_OK);
        CHECK(es2_shell_hardpoints_read(&vfs, &t, &h, &e) == ES2_OK && h.slots[2][5] == 2 && h.slots[1][5] == 0);
        source.data.loose.count = 8;   /* ARM_8 missing */
        CHECK(es2_vfs_open(&source, 1, &vfs, &e) == ES2_OK);
        CHECK(es2_shell_hardpoints_read(&vfs, &t, &h, &e) == ES2_ERR_NOT_FOUND);
    }
    free(image);
}

/* ---- the engine on a composed SAV --------------------------------------------- */
typedef struct fixture {
    es2_shell_engine_tables tables;
    es2_shell_hardpoints hardpoints;
    es2_vfs vfs;
    es2_shell_engine engine;
    uint8_t inventory[33][40], prefixes[37][4], suffixes[37][25], bays[2][76 + 12 * 3];
    uint8_t five[22], selection[152], choice[12], team[4], nine[18], tally[4], final[22], variables[2000];
    size_t counts[33], items[2];
    uint8_t flags[33];
} fixture;
static uint8_t sav[8192];
static size_t compose(fixture *f) {
    es2_sav_image m;
    es2_sav_pilot pilots[37];
    es2_sav_loadout loadouts[2];
    static uint8_t name[2] = {'P', 0}, other[2] = {'Q', 0};
    size_t required = 0;
    es2_error e;
    memset(&m, 0, sizeof m);
    for (int i = 0; i < 33; i++) {
        m.inventory[i].flag = f->flags[i];
        m.inventory[i].count = f->counts[i];
        m.inventory[i].items = (es2_span){f->inventory[i], 10 * f->counts[i]};
    }
    for (int i = 0; i < 37; i++) {
        pilots[i].prefix = (es2_span){f->prefixes[i], 4};
        pilots[i].name = (es2_span){i == 36 ? name : other, 2};
        pilots[i].suffix = (es2_span){f->suffixes[i], 25};
        pilots[i].offset = 0;
    }
    for (int i = 0; i < 2; i++) {
        loadouts[i].index = (int16_t)i;
        loadouts[i].fixed = (es2_span){f->bays[i], 76};
        loadouts[i].items = (es2_span){f->bays[i] + 76, 12 * f->items[i]};
        loadouts[i].count = f->items[i];
        loadouts[i].offset = 0;
    }
    m.five_entry_bank = (es2_span){f->five, 22};
    m.selection = (es2_span){f->selection, 152};
    m.roster = pilots;
    m.roster_selection = (es2_span){f->choice, 12};
    m.team = (es2_span){f->team, 4};
    m.player = &pilots[36];
    m.loadout_count = 2;
    m.loadouts = loadouts;
    m.nine_word_bank = (es2_span){f->nine, 18};
    m.tally = (es2_span){f->tally, 4};
    m.variables = (es2_span){f->variables, 2000};
    m.final_fields = (es2_span){f->final, 22};
    CHECK(es2_sav_compose(&m, sav, sizeof sav, &required, &e) == ES2_OK);
    return required;
}
static void init(fixture *f) {
    memset(f, 0, sizeof *f);
    for (int i = 0; i < 112; i++) f->tables.prepare[i] = (uint8_t)(3 * i + 1);
    f->tables.rows[1] = 5; f->tables.rows[2] = 7; f->tables.rows[3] = 13;
    f->hardpoints.slots[1][0] = 7; f->hardpoints.slots[1][5] = 5; f->hardpoints.slots[1][13] = 4;
    f->engine.vfs = &f->vfs;
    f->engine.tables = &f->tables;
    f->engine.hardpoints = &f->hardpoints;
    memcpy(f->engine.language, "GER", 4);
    /* the player in bay 0 (vehicle 1, three slots, item 13 in slot 2 with
     * guidance 2), roster pilot 3 in bay 1 (vehicle 2, two slots, empty);
     * pilot 1 also names bay 1 but is not selected (a replaced pilot) */
    for (int i = 0; i < 37; i++) { memset(f->suffixes[i], 0, 25); w16(f->suffixes[i], 0xffff); w16(f->suffixes[i] + 5, 0xffff); }
    w16(f->suffixes[36], 0); w16(f->suffixes[3], 1); w16(f->suffixes[1], 1); w16(f->choice, 3);
    w16(f->bays[0], 1); w16(f->bays[0] + 74, 3);
    w16(f->bays[0] + 76, 2); w16(f->bays[0] + 78, 13); w16(f->bays[0] + 86, 2); f->items[0] = 1;
    w16(f->bays[1], 2); w16(f->bays[1] + 74, 2);
    f->flags[0] = f->flags[5] = f->flags[13] = 1;
    for (int k = 0; k < 2; k++) { uint8_t *r = f->inventory[5] + 10 * k; w16(r, 5); w16(r + 2, 5); w16(r + 4, 90); w16(r + 6, 100); w16(r + 8, 5); }
    f->counts[5] = 2;
    w16(f->inventory[7], 7); w16(f->inventory[7] + 2, 7); f->counts[7] = 1;
    w16(f->selection, 1);
    w16(f->team, 1); w16(f->team + 2, 1);
    w16(f->final, 3);
}
static void engine_state(void) {
    static fixture f;
    es2_shell_rng want;
    es2_error e;
    size_t n;
    init(&f);
    n = compose(&f);
    /* start: reset + n draws, selectors zero, nothing loaded */
    f.engine.selectors[4] = 9;
    f.engine.final_word = 2;
    CHECK(es2_shell_engine_start(&f.engine, 5, &e) == ES2_OK);
    CHECK(es2_shell_rng_init((es2_span){f.tables.prepare, 112}, 5, &want, &e) == ES2_OK &&
          !memcmp(&want, &f.engine.rng, sizeof want) && f.engine.seed == 5);
    CHECK(f.engine.selectors[4] == 0 && f.engine.final_word == 0 && !f.engine.loaded && !f.engine.memory.data);
    CHECK(es2_shell_engine_start(&f.engine, 40000, &e) == ES2_ERR_ARGUMENT && f.engine.seed == 5);
    memcpy(f.engine.language, "XYZ", 4);
    CHECK(es2_shell_engine_start(&f.engine, 1, &e) == ES2_ERR_ARGUMENT);
    memcpy(f.engine.language, "GER", 4);
    f.engine.tables = NULL;
    CHECK(es2_shell_engine_start(&f.engine, 1, &e) == ES2_ERR_ARGUMENT);
    f.engine.tables = &f.tables;
    /* nothing loaded: every state operation refuses */
    {
        es2_shell_bay_view v;
        es2_shell_bay_op op = {ES2_SHELL_BAY_GUIDANCE, 0, 2, 1, 0, 0};
        es2_shell_engine_prepared p;
        es2_shell_launch_check_result r;
        es2_shell_buffer mec = {0}, var = {0};
        es2_shell_results_output o;
        memset(&p, 0, sizeof p);
        memset(&o, 0, sizeof o);
        CHECK(es2_shell_engine_bay_view(&f.engine, -1, -1, &v, &e) == ES2_ERR_ARGUMENT);
        CHECK(es2_shell_engine_bay(&f.engine, &op, &e) == ES2_ERR_ARGUMENT);
        CHECK(es2_shell_engine_prepare(&f.engine, &p, &e) == ES2_ERR_ARGUMENT);
        CHECK(es2_shell_engine_launch(&f.engine, &r, &mec, &var, &e) == ES2_ERR_ARGUMENT);
        CHECK(es2_shell_engine_results(&f.engine, (es2_span){NULL, 0}, (es2_span){0}, (es2_span){0}, 0, 0, &o, &e) ==
              ES2_ERR_ARGUMENT);
        CHECK(es2_shell_engine_set(&f.engine, (es2_span){sav, 10}, &e) != ES2_OK && !f.engine.loaded);
        CHECK(es2_shell_engine_load(&f.engine, (es2_span){sav, 10}, &e) != ES2_OK && !f.engine.loaded);
    }
    /* set: a copy, loaded, the SAV's final word */
    CHECK(es2_shell_engine_set(&f.engine, (es2_span){sav, n}, &e) == ES2_OK);
    CHECK(f.engine.loaded && f.engine.final_word == 3 && f.engine.memory.size == n && !memcmp(f.engine.memory.data, sav, n));
    /* load: the state the original readers restore */
    {
        uint8_t loaded[8192];
        size_t m = 0;
        CHECK(es2_shell_loaded_save((es2_span){sav, n}, loaded, sizeof loaded, &m, &e) == ES2_OK);
        es2_shell_engine_free(&f.engine);
        f.engine.loaded = 0;
        CHECK(es2_shell_engine_load(&f.engine, (es2_span){sav, n}, &e) == ES2_OK && f.engine.loaded &&
              f.engine.final_word == 3 && f.engine.memory.size == m && !memcmp(f.engine.memory.data, loaded, m));
        CHECK(es2_shell_engine_set(&f.engine, (es2_span){sav, n}, &e) == ES2_OK);
    }
    /* the bay view */
    {
        es2_shell_bay_view v;
        CHECK(es2_shell_engine_bay_view(&f.engine, -1, -1, &v, &e) == ES2_OK && !v.rows);
        CHECK(v.present[0] && v.present[1] && !v.present[2] && v.vehicle[0] == 1 && v.capacity[0] == 3 &&
              v.vehicle[1] == 2 && v.capacity[1] == 2);
        CHECK(v.item[0][2] == 13 && v.secondary[0][2] == 2 && v.item[0][0] == 0 && v.pilot[0] == 36 &&
              !strcmp(v.name[0], "P") && v.pilot[1] == 3 && !strcmp(v.name[1], "Q") && v.pilot[2] == -1);
        CHECK(es2_shell_engine_bay_view(&f.engine, 0, 2, &v, &e) == ES2_OK && v.rows == ES2_SHELL_WEAPON_ROWS);
        CHECK(v.row_item[1] == 5 && v.row_count[1] == 2 && v.row_available[1] && v.row_admitted[1] && v.row_enabled[1]);
        CHECK(v.row_item[2] == 7 && v.row_count[2] == 1 && !v.row_available[2] && !v.row_admitted[2] && !v.row_enabled[2]);
        CHECK(v.row_item[3] == 13 && v.row_count[3] == 0 && v.row_enabled[3] && v.row_item[0] == 0 && v.row_enabled[0]);
        CHECK(es2_shell_engine_bay_view(&f.engine, 0, 1, &v, &e) == ES2_OK && v.row_available[1] && !v.row_admitted[1] &&
              !v.row_enabled[1] && v.row_admitted[0]);
        CHECK(es2_shell_engine_bay_view(&f.engine, 1, 0, &v, &e) == ES2_OK && !v.row_admitted[0] && !v.row_enabled[1]);
        CHECK(es2_shell_engine_bay_view(&f.engine, 3, 0, &v, &e) == ES2_OK && !v.row_admitted[0]);   /* no record */
        CHECK(es2_shell_engine_bay_view(&f.engine, 8, 0, &v, &e) == ES2_ERR_ARGUMENT);
        CHECK(es2_shell_engine_bay_view(&f.engine, 0, 16, &v, &e) == ES2_ERR_ARGUMENT);
    }
    /* the ARM answer */
    CHECK(es2_shell_engine_admits(&f.engine, 1, 5, 2) && !es2_shell_engine_admits(&f.engine, 1, 5, 1) &&
          !es2_shell_engine_admits(&f.engine, 9, 5, 0) && !es2_shell_engine_admits(&f.engine, 1, 33, 0) &&
          !es2_shell_engine_admits(&f.engine, 1, 5, 16) && !es2_shell_engine_admits(&f.engine, -1, 0, 0) &&
          !es2_shell_engine_admits(NULL, 1, 5, 0));
    /* operations on the memory state */
    {
        es2_shell_bay_view v;
        es2_shell_bay_op guidance = {ES2_SHELL_BAY_GUIDANCE, 0, 2, 1, 0, 0}, empty = {ES2_SHELL_BAY_GUIDANCE, 0, 0, 1, 0, 0};
        es2_shell_bay_op click = {ES2_SHELL_BAY_CLICK, 0, 0, 5, 1, 0};
        CHECK(es2_shell_engine_bay(&f.engine, &guidance, &e) == ES2_OK && guidance.applied);
        CHECK(es2_shell_engine_bay_view(&f.engine, -1, -1, &v, &e) == ES2_OK && v.secondary[0][2] == 1);
        size_t before = f.engine.memory.size;
        uint8_t copy[8192];
        memcpy(copy, f.engine.memory.data, before);
        CHECK(es2_shell_engine_bay(&f.engine, &empty, &e) != ES2_OK && f.engine.memory.size == before &&
              !memcmp(copy, f.engine.memory.data, before));
        CHECK(es2_shell_engine_bay(&f.engine, &click, &e) == ES2_OK && click.applied);
        CHECK(es2_shell_engine_bay_view(&f.engine, 0, 0, &v, &e) == ES2_OK && v.item[0][0] == 5 && v.row_count[1] == 1);
    }
    /* Rock & Roll: the files exactly when the checks pass */
    {
        es2_shell_launch_check_result r;
        es2_shell_buffer mec = {0}, var = {0};
        CHECK(es2_shell_engine_launch(&f.engine, &r, &mec, &var, &e) == ES2_OK);
        CHECK(r.warning == ES2_SHELL_LAUNCH_OK ? mec.size && var.size == 2000 : !mec.data && !var.data);
        free(mec.data);
        free(var.data);
    }
    /* the preparation: CAREER group 0 refused; the player without a Herc
     * refused without the policy (memory unchanged) */
    {
        es2_shell_engine_prepared p;
        memset(&p, 0, sizeof p);
        w16(f.selection, 0);
        n = compose(&f);
        CHECK(es2_shell_engine_set(&f.engine, (es2_span){sav, n}, &e) == ES2_OK);
        CHECK(es2_shell_engine_prepare(&f.engine, &p, &e) == ES2_ERR_ARGUMENT);
        w16(f.selection, 6);
        n = compose(&f);
        CHECK(es2_shell_engine_set(&f.engine, (es2_span){sav, n}, &e) == ES2_OK);
        CHECK(es2_shell_engine_prepare(&f.engine, &p, &e) == ES2_ERR_ARGUMENT);
        w16(f.selection, 1);
        w16(f.suffixes[36], 5);   /* bay 5 has no record */
        n = compose(&f);
        CHECK(es2_shell_engine_set(&f.engine, (es2_span){sav, n}, &e) == ES2_OK);
        f.engine.lowest_free = 0;
        CHECK(es2_shell_engine_prepare(&f.engine, &p, &e) == ES2_ERR_UNSUPPORTED && !p.input.data &&
              f.engine.memory.size == n && !memcmp(f.engine.memory.data, sav, n));
        /* bay 1 belongs to roster pilot 3, bay 0 is free: the policy would
         * pick 0, then the preparation needs the CD (a loose VFS without
         * CAREER.DAT refuses) and the memory state stays */
        es2_vfs_source none;
        none.kind = ES2_VFS_LOOSE;
        none.data.loose.files = NULL;
        none.data.loose.count = 0;
        CHECK(es2_vfs_open(&none, 1, &f.vfs, &e) == ES2_OK);
        f.engine.lowest_free = 1;
        CHECK(es2_shell_engine_prepare(&f.engine, &p, &e) != ES2_OK && !p.input.data && f.engine.memory.size == n &&
              !memcmp(f.engine.memory.data, sav, n));
    }
    es2_shell_engine_free(&f.engine);
    CHECK(!f.engine.memory.data);
    es2_shell_engine_free(NULL);
}

int main(void) {
    es2_test_vshell_file_install();   /* the synthetic PE images through the tests' section walk */
    arm_files();
    tables();
    engine_state();
    printf("engine_test: %u checks, %u failures\n", checks, failures);
    return failures != 0;
}
