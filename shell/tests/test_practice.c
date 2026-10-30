#include "es2/shell/practice.h"
#include "es2/shell/vshell_store.h"
#include "es2/formats/sav.h"
#include "vshell_file.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Practice screen, INSTANT ACTION and the DEBUG dialog
 * (es2/shell/practice.h) on synthetic states. Every image, table and SAV
 * below is invented for the test; expected values follow from the contract
 * in vshell_ui.md section 15. The launch itself (new game and preparation
 * over the CD) is //shell:practice_real_test's. */
static unsigned checks, failures;
#define CHECK(x) do { checks++; if (!(x)) { failures++; fprintf(stderr, "line %d: %s\n", __LINE__, #x); } } while (0)
static void w(uint8_t *p, unsigned x) { p[0] = (uint8_t)x; p[1] = (uint8_t)(x >> 8); }
static void w32(uint8_t *p, uint32_t x) { w(p, x & 0xffffu); w(p + 2, x >> 16); }
static unsigned u(const uint8_t *p) { return p[0] + 256u * p[1]; }
static int s(const uint8_t *p) { unsigned x = u(p); return x < 32768 ? (int)x : (int)x - 65536; }

/* ---- a PE image with one section at RVA 0x73000 (raw 0x400, 0x7000 bytes) ---- */
static uint8_t image[0x7400];
static void build_image(void) {
    memset(image, 0, sizeof image);
    image[0] = 'M'; image[1] = 'Z';
    w32(image + 0x3c, 0x80);
    memcpy(image + 0x80, "PE\0\0", 4);
    w(image + 0x80 + 6, 1);           /* sections */
    w(image + 0x80 + 20, 0xe0);       /* optional header size */
    w32(image + 0x80 + 24 + 28, 0x400000);
    uint8_t *h = image + 0x80 + 24 + 0xe0;
    w32(h + 8, 0x7000); w32(h + 12, 0x73000); w32(h + 16, 0x7000); w32(h + 20, 0x400);
    for (unsigned i = 0; i < 12; i++) w(image + 0x400 + 0x6c56 + 2 * i, 10 + i);   /* VA 0x479c56 */
    memcpy(image + 0x400 + 0x6f70, "PILOTA", 7);                                   /* VA 0x479f70 */
    memcpy(image + 0x400 + 0x096b, "PILOTB", 7);                                   /* VA 0x47396b */
}

static void exe_tables(void) {
    uint8_t t[ES2_SHELL_PRACTICE_TABLES_SIZE], keep[ES2_SHELL_PRACTICE_TABLES_SIZE];
    es2_span span;
    es2_error e;
    build_image();
    /* Without a file reader (es2shell, es2menu) a file image is refused */
    es2_shell_set_file_reader(NULL);
    CHECK(es2_shell_exe_span((es2_span){image, sizeof image}, 0x479c56, 4, &span, &e) == ES2_ERR_ARGUMENT);
    CHECK(es2_shell_practice_tables((es2_span){image, sizeof image}, t, &e) == ES2_ERR_ARGUMENT);
    CHECK(es2_shell_practice_tables(ES2_VSHELL_STORE, t, &e) == ES2_OK);
    es2_test_vshell_file_install();
    CHECK(es2_shell_exe_span((es2_span){image, sizeof image}, 0x479c56, 4, &span, &e) == ES2_OK);
    CHECK(span.data == image + 0x400 + 0x6c56 && span.size == 4 && u(span.data) == 10);
    CHECK(es2_shell_practice_tables((es2_span){image, sizeof image}, t, &e) == ES2_OK);
    CHECK(u(t) == 10 && u(t + 22) == 21 && !strcmp((char *)t + 24, "PILOTA") && !strcmp((char *)t + 32, "PILOTB"));
    /* refusals: unmapped VA, past the section, bad magic, truncated, no NUL */
    memcpy(keep, t, sizeof t);
    CHECK(es2_shell_exe_span((es2_span){image, sizeof image}, 0x47a000, 1, &span, &e) == ES2_ERR_NOT_FOUND);
    CHECK(es2_shell_exe_span((es2_span){image, sizeof image}, 0x479ffe, 4, &span, &e) == ES2_ERR_NOT_FOUND);
    CHECK(es2_shell_exe_span((es2_span){image, sizeof image}, 0x300000, 1, &span, &e) == ES2_ERR_NOT_FOUND);
    CHECK(es2_shell_exe_span((es2_span){image, 0x20}, 0x479c56, 1, &span, &e) == ES2_ERR_TRUNCATED);
    CHECK(es2_shell_exe_span((es2_span){image, 0x7000}, 0x479c56, 4, &span, &e) == ES2_ERR_TRUNCATED);
    image[0x81] = 'X';
    CHECK(es2_shell_exe_span((es2_span){image, sizeof image}, 0x479c56, 4, &span, &e) == ES2_ERR_MAGIC);
    image[0x81] = 'E';
    memset(image + 0x400 + 0x6f70, 'A', 8);
    CHECK(es2_shell_practice_tables((es2_span){image, sizeof image}, t, &e) == ES2_ERR_STRING);
    CHECK(!memcmp(t, keep, sizeof t));
}

/* ---- screen state ------------------------------------------------------------ */
static uint8_t tables[ES2_SHELL_PRACTICE_TABLES_SIZE];
static void fixture_tables(void) {
    memset(tables, 0, sizeof tables);
    for (unsigned i = 0; i < 12; i++) w(tables + 2 * i, 0x40 + i);
    memcpy(tables + 24, "TRAINEE", 8);
    memcpy(tables + 32, "TRAINEX", 8);
}
static void open_session(es2_shell_prefs_session *session, uint8_t fill) {
    uint8_t file[ES2_SHELL_PREFS_SIZE];
    memset(file, fill, sizeof file);
    es2_span span = {file, sizeof file};
    es2_shell_prefs_session_open(&span, session);
}

static void screen(void) {
    es2_shell_practice p;
    es2_shell_prefs_session session, keep;
    uint8_t out[ES2_SHELL_PREFS_SIZE], file[ES2_SHELL_PREFS_SIZE];
    es2_error e;
    int changed = -1, applied = -1;
    fixture_tables();
    es2_shell_practice_open(&p);
    CHECK(p.index == -1 && p.preset_flag == 0 && es2_shell_practice_herc_enabled(&p));
    open_session(&session, 1);
    /* show: entry 0 (+0x28 := table[0]), then the mode byte through the
     * read-modify-write over the file as it is (its byte 0x30 stays the file's) */
    memset(file, 7, sizeof file);
    es2_span f = {file, sizeof file};
    CHECK(es2_shell_practice_show(&p, &session, tables, &f, out, &e) == ES2_OK);
    CHECK(p.index == 0 && session.current[ES2_SHELL_PREFS_HERC] == 0x40 && session.current[ES2_SHELL_PREFS_MODE] == 0);
    CHECK(out[ES2_SHELL_PREFS_MODE] == 0 && out[0x30] == 7 && out[ES2_SHELL_PREFS_HERC] == 7);
    CHECK(session.committed[ES2_SHELL_PREFS_HERC] == 1);
    CHECK(!es2_shell_practice_herc_enabled(&p));
    /* an entry already selected changes nothing, not even a Herc type set by hand */
    session.current[ES2_SHELL_PREFS_HERC] = 5;
    CHECK(es2_shell_practice_entry(&p, &session, tables, 0, &changed, &e) == ES2_OK && changed == 0);
    CHECK(session.current[ES2_SHELL_PREFS_HERC] == 5);
    CHECK(es2_shell_practice_entry(&p, &session, tables, 6, &changed, &e) == ES2_OK && changed == 1);
    CHECK(p.index == 6 && session.current[ES2_SHELL_PREFS_HERC] == 0x46 && es2_shell_practice_herc_enabled(&p));
    CHECK(es2_shell_practice_entry(&p, &session, tables, 3, &changed, &e) == ES2_OK && !es2_shell_practice_herc_enabled(&p));
    /* refusals leave everything */
    keep = session;
    CHECK(es2_shell_practice_entry(&p, &session, tables, 11, &changed, &e) == ES2_ERR_OFFSET);
    CHECK(es2_shell_practice_entry(&p, &session, tables, -1, &changed, &e) == ES2_ERR_OFFSET);
    CHECK(es2_shell_practice_entry(NULL, &session, tables, 1, &changed, &e) == ES2_ERR_ARGUMENT);
    CHECK(p.index == 3 && !memcmp(&keep, &session, sizeof keep));

    /* controls: left steps forward (0 at the modulus), right back (modulus - 1 below 0) */
    session.current[ES2_SHELL_PREFS_AMMO] = 0;
    CHECK(es2_shell_practice_control(&p, &session, ES2_SHELL_PRACTICE_AMMO, ES2_SHELL_CLICK_LEFT, &applied, &e) == ES2_OK);
    CHECK(applied == 1 && session.current[ES2_SHELL_PREFS_AMMO] == 1);
    CHECK(es2_shell_practice_control(&p, &session, ES2_SHELL_PRACTICE_AMMO, ES2_SHELL_CLICK_LEFT, &applied, &e) == ES2_OK);
    CHECK(session.current[ES2_SHELL_PREFS_AMMO] == 0);
    CHECK(es2_shell_practice_control(&p, &session, ES2_SHELL_PRACTICE_DIFFICULTY, ES2_SHELL_CLICK_RIGHT, &applied, &e) == ES2_OK);
    session.current[ES2_SHELL_PREFS_DIFFICULTY] = 0;
    CHECK(es2_shell_practice_control(&p, &session, ES2_SHELL_PRACTICE_DIFFICULTY, ES2_SHELL_CLICK_RIGHT, &applied, &e) == ES2_OK);
    CHECK(session.current[ES2_SHELL_PREFS_DIFFICULTY] == 3);
    /* a byte above the modulus just grows (forward) or shrinks (back) */
    session.current[ES2_SHELL_PREFS_TIME] = 5;
    CHECK(es2_shell_practice_control(&p, &session, ES2_SHELL_PRACTICE_TIME, ES2_SHELL_CLICK_LEFT, &applied, &e) == ES2_OK);
    CHECK(session.current[ES2_SHELL_PREFS_TIME] == 6);
    CHECK(es2_shell_practice_control(&p, &session, ES2_SHELL_PRACTICE_TIME, ES2_SHELL_CLICK_RIGHT, &applied, &e) == ES2_OK);
    CHECK(session.current[ES2_SHELL_PREFS_TIME] == 5);
    session.current[ES2_SHELL_PREFS_DAMAGE] = 255;
    CHECK(es2_shell_practice_control(&p, &session, ES2_SHELL_PRACTICE_DAMAGE, ES2_SHELL_CLICK_LEFT, &applied, &e) == ES2_OK);
    CHECK(session.current[ES2_SHELL_PREFS_DAMAGE] == 0);
    /* Herc Type: disabled for entries 0..3 (nothing, applied 0), 4..7 modulo 9 */
    session.current[ES2_SHELL_PREFS_HERC] = 8;
    CHECK(es2_shell_practice_control(&p, &session, ES2_SHELL_PRACTICE_HERC, ES2_SHELL_CLICK_LEFT, &applied, &e) == ES2_OK);
    CHECK(applied == 0 && session.current[ES2_SHELL_PREFS_HERC] == 8);
    CHECK(es2_shell_practice_entry(&p, &session, tables, 4, &changed, &e) == ES2_OK);
    session.current[ES2_SHELL_PREFS_HERC] = 8;
    CHECK(es2_shell_practice_control(&p, &session, ES2_SHELL_PRACTICE_HERC, ES2_SHELL_CLICK_LEFT, &applied, &e) == ES2_OK);
    CHECK(applied == 1 && session.current[ES2_SHELL_PREFS_HERC] == 0);
    CHECK(es2_shell_practice_control(&p, &session, ES2_SHELL_PRACTICE_HERC, ES2_SHELL_CLICK_RIGHT, &applied, &e) == ES2_OK);
    CHECK(session.current[ES2_SHELL_PREFS_HERC] == 8);
    keep = session;
    CHECK(es2_shell_practice_control(&p, &session, 5, ES2_SHELL_CLICK_LEFT, &applied, &e) == ES2_ERR_ARGUMENT);
    CHECK(es2_shell_practice_control(&p, &session, 0, 2, &applied, &e) == ES2_ERR_ARGUMENT);
    CHECK(!memcmp(&keep, &session, sizeof keep));
    /* labels: base + byte */
    CHECK(es2_shell_practice_label(ES2_SHELL_PRACTICE_HERC, &session) == 0x6e + 8);
    CHECK(es2_shell_practice_label(ES2_SHELL_PRACTICE_AMMO, &session) == 0x12a + session.current[0x25]);
    CHECK(es2_shell_practice_label(7, &session) == 0);

    /* Begin Mission: commit and write the current block; the launch */
    es2_shell_practice_launch l;
    session.current[ES2_SHELL_PREFS_DIFFICULTY] = 2;
    CHECK(es2_shell_practice_begin(&p, &session, tables, out, &l, &e) == ES2_OK);
    CHECK(!memcmp(out, session.current, sizeof out) && !memcmp(session.committed, session.current, sizeof out));
    CHECK(!strcmp(l.name, "TRAINEE") && l.rank == 2 && l.position == 4 && l.preset_index == 4 && l.preset_flag == 0);
    CHECK(!memcmp(l.controls, session.current + 0x25, 5));
    es2_shell_practice none;
    es2_shell_practice_open(&none);
    CHECK(es2_shell_practice_begin(&none, &session, tables, out, &l, &e) == ES2_ERR_OFFSET);
}

static void instant(void) {
    es2_shell_practice p, keep_p;
    es2_shell_prefs_session session, keep;
    uint8_t mode_out[ES2_SHELL_PREFS_SIZE], out[ES2_SHELL_PREFS_SIZE];
    es2_shell_practice_launch l;
    es2_error e;
    fixture_tables();
    es2_shell_practice_open(&p);
    open_session(&session, 0);
    session.current[ES2_SHELL_PREFS_MODE] = 1;
    session.current[ES2_SHELL_PREFS_DIFFICULTY] = 3;
    /* no file: the mode store writes the current block */
    CHECK(es2_shell_instant_action(&p, &session, tables, NULL, mode_out, out, &l, &e) == ES2_OK);
    CHECK(p.preset_flag == 1 && p.index == 8 && session.current[ES2_SHELL_PREFS_HERC] == 0x48);
    CHECK(es2_shell_practice_herc_enabled(&p));   /* positions 8..10 leave the button as it was */
    CHECK(session.current[ES2_SHELL_PREFS_INSTANT] == 1 && session.current[ES2_SHELL_PREFS_MODE] == 0);
    CHECK(mode_out[ES2_SHELL_PREFS_MODE] == 0 && mode_out[ES2_SHELL_PREFS_INSTANT] == 0 && mode_out[ES2_SHELL_PREFS_HERC] == 0);
    CHECK(!memcmp(out, session.current, sizeof out) && !memcmp(session.committed, session.current, sizeof out));
    CHECK(!strcmp(l.name, "TRAINEX") && l.rank == 3 && l.position == 8 && l.preset_flag == 1 && l.controls[3] == 0x48);
    CHECK(es2_shell_instant_action(&p, &session, tables, NULL, mode_out, out, &l, &e) == ES2_OK);
    CHECK(p.index == 9 && session.current[ES2_SHELL_PREFS_INSTANT] == 2);
    CHECK(es2_shell_instant_action(&p, &session, tables, NULL, mode_out, out, &l, &e) == ES2_OK);
    CHECK(p.index == 10 && session.current[ES2_SHELL_PREFS_INSTANT] == 0 && l.controls[3] == 0x4a);
    /* the same position twice keeps a Herc type stored since */
    session.current[ES2_SHELL_PREFS_HERC] = 1;
    p.index = 8;
    CHECK(es2_shell_instant_action(&p, &session, tables, NULL, mode_out, out, &l, &e) == ES2_OK);
    CHECK(session.current[ES2_SHELL_PREFS_HERC] == 1 && l.controls[3] == 1);
    /* +0x2e above 2: refused before anything changes */
    session.current[ES2_SHELL_PREFS_INSTANT] = 3;
    p.preset_flag = 0;
    keep = session;
    keep_p = p;
    CHECK(es2_shell_instant_action(&p, &session, tables, NULL, mode_out, out, &l, &e) == ES2_ERR_OFFSET);
    CHECK(!memcmp(&keep, &session, sizeof keep) && keep_p.index == p.index && p.preset_flag == 0);
    CHECK(es2_shell_instant_action(&p, &session, NULL, NULL, mode_out, out, &l, &e) == ES2_ERR_ARGUMENT);
}

static void debug_dialog(void) {
    char stem[32];
    es2_error e;
    CHECK(es2_shell_debug_mission("demo^01", stem, sizeof stem, &e) == ES2_OK && !strcmp(stem, "demo_01"));
    CHECK(es2_shell_debug_mission("C1_01", stem, sizeof stem, &e) == ES2_OK && !strcmp(stem, "C1_01"));
    strcpy(stem, "keep");
    CHECK(es2_shell_debug_mission("a^b^c", stem, sizeof stem, &e) == ES2_ERR_STRING);   /* only the first '^' */
    CHECK(es2_shell_debug_mission("", stem, sizeof stem, &e) == ES2_ERR_LENGTH);
    CHECK(es2_shell_debug_mission("ABCDEFGHIJKLMNOPQRSTUVWXYZ012345", stem, sizeof stem, &e) == ES2_ERR_LENGTH);
    CHECK(es2_shell_debug_mission("A B", stem, sizeof stem, &e) == ES2_ERR_STRING);
    CHECK(es2_shell_debug_mission("A", stem, 8, &e) == ES2_ERR_ARGUMENT);
    CHECK(!strcmp(stem, "keep"));
}

/* ---- SAVs --------------------------------------------------------------------- */
typedef struct fixture {
    uint8_t five[22], selection[152], choice[12], team[4], nine[18], tally[4], final[22], variables[2000];
    uint8_t prefixes[37][4], suffixes[37][25], bays[8][196];
    size_t bay_items[8];
    int bay_present[8];
    uint8_t flags[33];
} fixture;
static void pilot(uint8_t *x, int bay, int active, int order) {
    memset(x, 0, 25); w(x, (unsigned)bay); x[2] = (uint8_t)active; w(x + 3, 2); w(x + 5, (unsigned)order); w(x + 9, 100);
}
static void init(fixture *f) {
    memset(f, 0, sizeof *f);
    w(f->five, 5);
    for (int i = 0; i < 5; i++) w(f->five + 2 + 4 * i, (unsigned)i);
    w(f->selection, 0); w(f->selection + 2, 4);
    for (int i = 0; i < 36; i++) { pilot(f->suffixes[i], -1, 0, -1); w(f->prefixes[i] + 2, (unsigned)(10 + i)); }
    pilot(f->suffixes[36], 0, 1, 0);
    w(f->prefixes[36] + 2, 7);
    w(f->team, 0); w(f->team + 2, 1);
    for (int i = 0; i < 9; i++) w(f->nine + 2 * i, 3);
    w(f->final, 2);
    for (int i = 0; i < 33; i++) f->flags[i] = (uint8_t)(i & 1);
    for (int i = 0; i < 2000; i++) f->variables[i] = (uint8_t)i;
}
static size_t compose(fixture *f, uint8_t *out, size_t capacity) {
    es2_sav_image m;
    es2_sav_pilot roster[37];
    es2_sav_loadout loadouts[8];
    static uint8_t name[2] = {'P', 0};
    size_t n = 0, required = 0;
    es2_error e;
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
    m.five_entry_bank = (es2_span){f->five, 22}; m.selection = (es2_span){f->selection, 152}; m.roster = roster;
    m.roster_selection = (es2_span){f->choice, 12}; m.team = (es2_span){f->team, 4}; m.player = &roster[36];
    m.loadout_count = n; m.loadouts = loadouts; m.nine_word_bank = (es2_span){f->nine, 18};
    m.tally = (es2_span){f->tally, 4}; m.variables = (es2_span){f->variables, 2000}; m.final_fields = (es2_span){f->final, 22};
    CHECK(es2_sav_compose(&m, out, capacity, &required, &e) == ES2_OK);
    return required;
}

static void new_game_files(void) {
    static fixture f;
    static uint8_t sav[16384];
    es2_shell_buffer mec = {0}, var = {0};
    es2_error e;
    init(&f);
    size_t n = compose(&f, sav, sizeof sav);
    CHECK(es2_shell_practice_new_game_files((es2_span){sav, n}, &mec, &var, &e) == ES2_OK);
    CHECK(mec.size == 43 && u(mec.data) == 0 && u(mec.data + 2) == 1 && u(mec.data + 4) == 7 && u(mec.data + 6) == 2);
    CHECK(u(mec.data + 8) == 33 && !memcmp(mec.data + 10, f.flags, 33));
    CHECK(var.size == 2000 && !memcmp(var.data, f.variables, 2000));
    /* refusals: buffers in use, a hangar record, a squad capacity above 1 */
    CHECK(es2_shell_practice_new_game_files((es2_span){sav, n}, &mec, &var, &e) == ES2_ERR_ARGUMENT);
    free(mec.data); free(var.data); memset(&mec, 0, sizeof mec); memset(&var, 0, sizeof var);
    w(f.team, 2);
    n = compose(&f, sav, sizeof sav);
    CHECK(es2_shell_practice_new_game_files((es2_span){sav, n}, &mec, &var, &e) == ES2_ERR_UNSUPPORTED);
    w(f.team, 0);
    f.bay_present[0] = 1; w(f.bays[0], 1); w(f.bays[0] + 74, 3);
    n = compose(&f, sav, sizeof sav);
    CHECK(es2_shell_practice_new_game_files((es2_span){sav, n}, &mec, &var, &e) == ES2_ERR_UNSUPPORTED);
    CHECK(!mec.data && !var.data);
}

static void prepared_save(void) {
    static fixture f;
    static uint8_t base[16384], out[16384], defaults[164], items[60], bank[2000];
    static es2_shell_state after;
    es2_shell_output prepared = {0};
    es2_sav parsed, before;
    es2_sav_loadout r;
    es2_sav_pilot pp;
    size_t need = 0;
    es2_error e;
    init(&f);
    size_t n = compose(&f, base, sizeof base);
    /* vehicle table: IDs 0..8 at +128, capacity 3 + i at +146; item list: ID i at i */
    for (int i = 0; i < 9; i++) { w(defaults + 128 + 2 * i, (unsigned)i); w(defaults + 146 + 2 * i, (unsigned)(3 + i)); }
    for (int i = 0; i < 30; i++) w(items + 2 * i, (unsigned)i);
    es2_shell_save_tables t = {{defaults, 164}, {items, 60}};
    memset(&after, 0, sizeof after);
    es2_shell_equipment *q = &after.equipment;
    q->initialized = 1;
    q->capacity = 3; q->count = 2;
    for (int i = 0; i < 36; i++) { q->pilots[i].bay = -1; q->pilots[i].order = -1; }
    q->pilots[0].bay = 1; q->pilots[0].active = 1; q->pilots[0].order = 1;
    q->pilots[12].bay = 2; q->pilots[12].active = 0; q->pilots[12].order = 2;
    q->player.bay = 0; q->player.active = 1; q->player.order = 0;
    q->selected[1] = 4; q->next[2] = 9;
    for (int i = 0; i < 33; i++) q->flags[i] = 1;
    q->bays[0].present = 1; q->bays[0].vehicle = 3; q->bays[0].capacity = 6;
    q->bays[2].present = 1; q->bays[2].vehicle = 5; q->bays[2].capacity = 8;
    for (int b = 0; b < 3; b++) for (int i = 0; i < 33; i++) q->bays[b].condition[i] = 100;
    for (int b = 0; b < 3; b++) for (int i = 0; i < 10; i++) q->bays[b].secondary[i] = 5;
    q->bays[0].items[0] = 2; q->bays[0].items[3] = 15; q->bays[0].secondary[3] = 1; q->bays[0].items[4] = -1;
    q->bays[0].condition[23] = 77;
    q->bays[2].items[7] = 9;
    for (int i = 0; i < 2000; i++) bank[i] = (uint8_t)(i * 3);
    prepared.variables = (es2_shell_buffer){bank, 2000};
    CHECK(es2_shell_mode0_prepared_save((es2_span){base, n}, &t, &after, &prepared, NULL, 0, &need, &e) == ES2_OK);
    CHECK(need > n && need < sizeof out);
    CHECK(es2_shell_mode0_prepared_save((es2_span){base, n}, &t, &after, &prepared, out, sizeof out, &need, &e) == ES2_OK);
    CHECK(es2_sav_parse((es2_span){out, need}, &parsed, &e) == ES2_OK);
    CHECK(es2_sav_parse((es2_span){base, n}, &before, &e) == ES2_OK);
    CHECK(parsed.loadout_count == 2 && s(parsed.team.data) == 3 && s(parsed.team.data + 2) == 2);
    CHECK(!memcmp(parsed.variables.data, bank, 2000) && !memcmp(parsed.selection.data, before.selection.data, 152));
    CHECK(!memcmp(parsed.nine_word_bank.data, f.nine, 18) && !memcmp(parsed.final_fields.data, f.final, 22));
    CHECK(u(parsed.roster_selection.data + 2) == 4 && u(parsed.roster_selection.data + 10) == 9);
    CHECK(parsed.inventory[0].flag == 1 && parsed.inventory[2].flag == 1);
    CHECK(es2_sav_pilot_at(&parsed, 0, &pp, &e) == ES2_OK && s(pp.suffix.data) == 1 && pp.suffix.data[2] == 1 && s(pp.suffix.data + 5) == 1);
    CHECK(u(pp.suffix.data + 3) == 2 && u(pp.suffix.data + 9) == 100);   /* rank and the rest kept */
    CHECK(es2_sav_pilot_at(&parsed, 12, &pp, &e) == ES2_OK && s(pp.suffix.data) == 2 && pp.suffix.data[2] == 0);
    CHECK(s(parsed.player.suffix.data) == 0 && parsed.player.suffix.data[2] == 1);
    CHECK(es2_sav_loadout_at(&parsed, 0, &r, &e) == ES2_OK && r.index == 0 && r.count == 3);
    CHECK(s(r.fixed.data) == 3 && u(r.fixed.data + 2) == 3 && u(r.fixed.data + 70) == 100 && u(r.fixed.data + 72) == 0);
    CHECK(u(r.fixed.data + 74) == 6 && u(r.fixed.data + 4 + 2 * 23) == 77);
    CHECK(u(r.items.data) == 0 && u(r.items.data + 2) == 2 && u(r.items.data + 4) == 2 && u(r.items.data + 6) == 100);
    CHECK(u(r.items.data + 8) == 77 && u(r.items.data + 10) == 5);
    CHECK(u(r.items.data + 12) == 3 && u(r.items.data + 14) == 15 && u(r.items.data + 22) == 1);
    /* an authored -1 is kept, its list position 0xffff */
    CHECK(u(r.items.data + 24) == 4 && s(r.items.data + 26) == -1 && u(r.items.data + 28) == 0xffff);
    CHECK(es2_sav_loadout_at(&parsed, 1, &r, &e) == ES2_OK && r.index == 2 && r.count == 1 && u(r.fixed.data + 74) == 8);
    CHECK(u(r.items.data) == 7 && u(r.items.data + 2) == 9);
    /* refusals: a vehicle outside the table, no state, wrong tables, short bank */
    q->bays[2].vehicle = 40;
    CHECK(es2_shell_mode0_prepared_save((es2_span){base, n}, &t, &after, &prepared, out, sizeof out, &need, &e) == ES2_ERR_NOT_FOUND);
    q->bays[2].vehicle = 5;
    q->initialized = 0;
    CHECK(es2_shell_mode0_prepared_save((es2_span){base, n}, &t, &after, &prepared, out, sizeof out, &need, &e) == ES2_ERR_ARGUMENT);
    q->initialized = 1;
    es2_shell_save_tables bad = {{defaults, 160}, {items, 60}};
    CHECK(es2_shell_mode0_prepared_save((es2_span){base, n}, &bad, &after, &prepared, out, sizeof out, &need, &e) == ES2_ERR_ARGUMENT);
    prepared.variables.size = 1999;
    CHECK(es2_shell_mode0_prepared_save((es2_span){base, n}, &t, &after, &prepared, out, sizeof out, &need, &e) == ES2_ERR_ARGUMENT);
}

static void launch_arguments(void) {
    es2_shell_practice_launch l;
    es2_shell_rng rng;
    es2_shell_buffer save = {0};
    es2_shell_practice_result result;
    int16_t selectors[30] = {0};
    static uint8_t defaults[164], items[60];
    es2_shell_save_tables t = {{defaults, 164}, {items, 60}};
    es2_error e;
    memset(&l, 0, sizeof l);
    memset(&rng, 0, sizeof rng);
    memset(&result, 0, sizeof result);
    memcpy(l.name, "TRAINEE", 8);
    CHECK(es2_shell_practice_new_game(NULL, "GER", &t, &l, NULL, &rng, &save, &e) == ES2_ERR_ARGUMENT);
    CHECK(es2_shell_practice_run(NULL, "GER", &t, (es2_span){0}, &l, NULL, NULL, &rng, selectors, &result, &e) == ES2_ERR_ARGUMENT);
    l.position = -1;
    CHECK(es2_shell_practice_run(NULL, "GER", &t, (es2_span){0}, &l, NULL, NULL, &rng, selectors, &result, &e) == ES2_ERR_ARGUMENT);
    CHECK(!result.save.data && !result.new_game.data);
}

int main(void) {
    exe_tables();
    screen();
    instant();
    debug_dialog();
    new_game_files();
    prepared_save();
    launch_arguments();
    printf("%u checks, %u failures\n", checks, failures);
    return failures != 0;
}
