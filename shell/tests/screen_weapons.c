/* Frame hashes of the WEAPONS screen 0x43ebbc (screen test harness).
 *
 * Synthetic cases (main_menu_fixture.h's font and palette, weapons_fixture.h's
 * engine state, arm_weap.dat and frames) and real cases (needs_data:
 * dfn\font2.dfn, dba\arm_weap.dba, gam\arm_weap.dat, dpl\arming.dpl,
 * ENG\ESTEXT.BIN and ENG\WPN_DESC.BIN from the CD's VOLs; the row table, the
 * ARM file names and the literals from the hash-checked VER95\VSHELL.EXE at
 * run time; the state is a new career of the state engine). The screen is drawn
 * alone (the bar and the squad panel are the service bay's). States: the show
 * (no slot, row 0), slot next once (slot 0), three times (the rack slot with
 * the guidance panel), a guidance button, a row click that mounts, slot prev.
 * No capture-tour reference exists yet; the capture states a capture tour needs
 * are listed in vshell_ui.md section 29. */
#include "screen_harness.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "es2/formats/dbm.h"
#include "es2/formats/dpl.h"
#include "es2/formats/vfs.h"
#include "es2/formats/vol.h"
#include "es2/shell/gui/weapons.h"
#include "es2/shell/practice.h"
#include "main_menu_fixture.h"
#include "menu.h"
#include "weapons_fixture.h"
#include "es2/formats/sha256.h"
#include "vshell_file.h"

#define VSHELL_SHA256 "1534aa8f732bdecd826484d893ad58b8a5e6ebd4d25ab3158a2469b8f69ba16e"

typedef struct wp_state {
    es2_ui ui;
    es2_weapons_screen screen;
    int16_t bay;
    int real;
} wp_state;

/* ---- resources ------------------------------------------------------------- */
static int synthetic_ready;
static es2_dfn synthetic_font;
static uint8_t synthetic_font_bytes[16384];
static uint8_t synthetic_records[256][4];
static wpf fx;

typedef struct wp_real {
    int ready;
    es2_dfn font;
    uint8_t records[256][4];
    es2_span estext, desc, arm_weap;
    es2_dba dba;
    es2_bitmap frames[64];
    uint8_t *scratch[64];
    uint8_t *exe;
    size_t exe_size;
    es2_shell_engine_tables tables;
    es2_shell_hardpoints hardpoints;
    es2_shell_engine engine;
} wp_real;
static wp_real real;

static int synthetic_load(void) {
    if (synthetic_ready) return 0;
    if (mmf_font(synthetic_font_bytes, &synthetic_font)) return 1;
    mmf_palette(synthetic_records);
    synthetic_ready = 1;
    return 0;
}

static uint8_t *slurp(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    uint8_t *data = NULL;
    long n;
    if (!f) return NULL;
    if (!fseek(f, 0, SEEK_END) && (n = ftell(f)) > 0 && !fseek(f, 0, SEEK_SET) && (data = malloc((size_t)n))) {
        if (fread(data, 1, (size_t)n, f) != (size_t)n) {
            free(data);
            data = NULL;
        } else
            *size = (size_t)n;
    }
    fclose(f);
    return data;
}

/* The VOLs stay mapped for the process (the font, the frames and the texts borrow them). */
static es2_vol vols[4];
static es2_vfs_source sources[4];
static es2_vfs vfs;
static int real_load(const char *root) {
    static const char *const names[4] = {"ZONES.VOL", "SHELL0.VOL", "SHELL1.VOL", "LANG0.VOL"};
    es2_vfs_copy c;
    char path[1024], hash[65];
    if (real.ready) return real.ready < 0 ? 2 : 0;
    real.ready = -1;
    for (int i = 0; i < 4; i++) {
        size_t size = 0;
        uint8_t *data;
        snprintf(path, sizeof path, "%s/VOL/%s", root, names[i]);
        if (!(data = slurp(path, &size)) || es2_vol_open((es2_span){data, size}, &vols[i], NULL) != ES2_OK) return 2;
        sources[i].kind = ES2_VFS_VOL;
        sources[i].data.vol = &vols[i];
    }
    if (es2_vfs_open(sources, 4, &vfs, NULL) != ES2_OK) return 3;
    if (es2_vfs_find(&vfs, "dfn\\font2.dfn", &c, NULL) != ES2_OK || es2_dfn_parse(c.payload, &real.font, NULL) != ES2_OK)
        return 4;
    if (es2_vfs_find(&vfs, "dba\\arm_weap.dba", &c, NULL) != ES2_OK || es2_dba_parse(c.payload, &real.dba, NULL) != ES2_OK)
        return 5;
    if (es2_vfs_find(&vfs, "gam\\arm_weap.dat", &c, NULL) != ES2_OK) return 5;
    real.arm_weap = c.payload;
    {
        es2_dpl dpl;
        if (es2_vfs_find(&vfs, es2_palette_file(ES2_PALETTE_ARMING), &c, NULL) != ES2_OK ||
            es2_dpl_parse(c.payload, &dpl, NULL) != ES2_OK)
            return 6;
        memset(real.records, 0, sizeof real.records);
        for (size_t i = 0; i < 256 && i < dpl.colour_count; i++) {
            es2_dpl_colour col;
            if (es2_dpl_colour_at(&dpl, i, &col, NULL) != ES2_OK) return 6;
            real.records[i][0] = col.red;
            real.records[i][1] = col.green;
            real.records[i][2] = col.blue;
            real.records[i][3] = col.flags;
        }
    }
    if (es2_vfs_find(&vfs, "ENG/ESTEXT.BIN", &c, NULL) != ES2_OK) return 7;
    real.estext = c.payload;
    if (es2_vfs_find(&vfs, "ENG/WPN_DESC.BIN", &c, NULL) != ES2_OK) return 7;
    real.desc = c.payload;
    snprintf(path, sizeof path, "%s/VER95/VSHELL.EXE", root);
    if (!(real.exe = slurp(path, &real.exe_size))) return 8;
    es2f_sha256_of(real.exe, real.exe_size, hash);
    if (strcmp(hash, VSHELL_SHA256)) return 9;
    es2_test_vshell_file_install();   /* the file through the tests' own section walk */
    if (es2_shell_engine_tables_read((es2_span){real.exe, real.exe_size}, &real.tables, NULL) != ES2_OK) return 10;
    if (es2_shell_hardpoints_read(&vfs, &real.tables, &real.hardpoints, NULL) != ES2_OK) return 11;
    real.ready = 1;
    return 0;
}

static int is_real(void *ctx) { return ((wp_state *)ctx)->real; }
static const es2_dfn *h_font(void *ctx, uint32_t global) {
    if (global != 0x46de00) return NULL;
    return is_real(ctx) ? &real.font : &synthetic_font;
}
static const es2_bitmap *h_frame(void *ctx, const char *file, int frame) {
    if (strcmp(file, "dba\\arm_weap.dba")) return NULL;
    if (!is_real(ctx)) return frame >= 0 && frame < WPF_FRAMES ? &fx.frames[frame] : NULL;
    if (frame < 0 || frame >= 64) return NULL;
    if (!real.scratch[frame]) {
        es2_dbm dbm;
        uint8_t *s = malloc(640 * 480);
        if (!s || es2_dba_image_at(&real.dba, (size_t)frame, &dbm, NULL) != ES2_OK ||
            es2_draw_bitmap_from_dbm(&dbm, s, 640 * 480, &real.frames[frame]) != ES2_DRAW_DONE) {
            free(s);
            return NULL;
        }
        real.scratch[frame] = s;
    }
    return &real.frames[frame];
}
static es2_span h_file(void *ctx, const char *name) {
    if (strcmp(name, "gam\\arm_weap.dat")) return (es2_span){NULL, 0};
    return is_real(ctx) ? real.arm_weap : (es2_span){fx.arm_weap, fx.arm_weap_size};
}
static const char *catalogue(es2_span cat, uint32_t id) {
    static char texts[64][104];
    static unsigned next;
    char *t = texts[next++ & 63];
    es2_span s;
    size_t n;
    if (es2m_bin_raw(cat, id, &s, NULL) != ES2_OK) return NULL;
    n = s.size < sizeof texts[0] - 1 ? s.size : sizeof texts[0] - 1;
    memcpy(t, s.data, n);
    t[n] = 0;
    return t;
}
static const char *synthetic_text(char prefix, uint32_t id) {
    static char texts[64][24];
    static unsigned next;
    char *t = texts[next++ & 63];
    snprintf(t, sizeof texts[0], "%c%X TEXT", prefix, (unsigned)id);
    return t;
}
static const char *h_text(void *ctx, uint32_t id) {
    return is_real(ctx) ? catalogue(real.estext, id) : synthetic_text('T', id);
}
static const char *h_desc(void *ctx, uint32_t id) {
    return is_real(ctx) ? catalogue(real.desc, id) : synthetic_text('D', id);
}
static const char *h_literal(void *ctx, uint32_t address) {
    es2_span s;
    if (!is_real(ctx)) {
        if (address == 0x4774c1) return "  ";
        if (address == 0x476ddb || address == 0x476de1) return "0";
        if (address == 0x476de3) return "<";
        if (address == 0x476de5) return ">";
        return "";
    }
    if (es2_shell_exe_span((es2_span){real.exe, real.exe_size}, address, 2, &s, NULL) != ES2_OK ||
        !memchr(s.data, 0, s.size))
        return NULL;
    return (const char *)s.data;
}

static void on_event(void *context, const es2_gui_event *ev) { es2_ui_on_event(&((wp_state *)context)->ui, ev); }
static void on_timer(void *context, es2_gui_shell *shell) {
    wp_state *st = context;
    st->ui.now += 100;
    es2_ui_on_timer(&st->ui, shell);
}

static int setup(es2_screen_session *s, void *state, int real_data, const char *root) {
    wp_state *st = state;
    es2_gui_rect all = {0, 0, ES2_GUI_WIDTH - 1, ES2_GUI_HEIGHT - 1};
    const uint8_t(*records)[4];
    int code = real_data ? real_load(root) : synthetic_load();
    if (code) return code;
    st->real = real_data;
    if (real_data) {
        uint32_t salvage = 0;
        es2_shell_engine_free(&real.engine);
        memset(&real.engine, 0, sizeof real.engine);
        real.engine.vfs = &vfs;
        real.engine.tables = &real.tables;
        real.engine.hardpoints = &real.hardpoints;
        memcpy(real.engine.language, "ENG", 4);
        real.engine.mode = 1;
        if (es2_shell_engine_start(&real.engine, 0, NULL) != ES2_OK ||
            es2_shell_engine_new_game(&real.engine, "REAL", 0, &salvage, NULL) != ES2_OK)
            return 12;
        st->screen.engine = &real.engine;
        records = (const uint8_t(*)[4])real.records;
    } else {
        if (wpf_open(&fx, 1)) return 12;
        st->screen.engine = &fx.engine;
        records = (const uint8_t(*)[4])synthetic_records;
    }
    if (es2_ui_init(&st->ui, s->shell)) return 20;
    st->ui.context = st;
    st->bay = 0;
    st->screen.bay = &st->bay;
    st->screen.host = (es2_weapons_screen_host){st, h_font, h_frame, h_file, h_text, h_desc, h_literal, NULL, NULL, NULL};
    if (es2_screen_weapons_build(&st->ui, &st->screen)) return 21;
    if (st->screen.refused) return 22;
    es2_ui_show(&st->ui, st->ui.root);
    memset(s->shell->frame.pixels, 0, ES2_GUI_PIXELS);
    s->shell->frame.realize_gate = 1;
    es2_gui_palette_select(&s->shell->frame, records);
    es2_gui_palette_apply(&s->shell->frame);
    es2_gui_invalidate_window(&s->shell->frame, all);
    s->shell->input.interactive = 1;
    es2_weapons_screen_show(&st->screen, -1); /* WEAPONS from the bay alone (word -1) */
    s->screen.context = st;
    s->screen.handle = on_event;
    s->screen.timer = on_timer;
    return 0;
}
static int setup_synthetic(es2_screen_session *s, void *state, const char *root) { return setup(s, state, 0, root); }
static int setup_real(es2_screen_session *s, void *state, const char *root) { return setup(s, state, 1, root); }

/* ---- scripts ---------------------------------------------------------------- */
#define NEXT "click weapons.159\nstep 2\n"
static const char script_shown[] = "step 2\nsnap shown\n";
static const char script_next[] = "step 2\n" NEXT "snap after\n";
/* synthetic slot 2 holds the rack 13: the guidance panel */
static const char script_rack[] = "step 2\n" NEXT NEXT NEXT "snap after\n";
static const char script_sarh[] = "step 2\n" NEXT NEXT NEXT "click weapons.12\nstep 2\nsnap after\n";
/* slot 1 is empty: row 4 (ID 5) mounts */
static const char script_mount[] = "step 2\n" NEXT NEXT "click weapons.41\nstep 2\nsnap after\n";
static const char script_prev[] = "step 2\nclick weapons.157\nstep 2\nsnap after\n";

/* ---- expectations (our hashes; reference NULL until a capture tour)
 * -------------- */
#define T_SYN "98fd384cc75016998f60c80a25cbc264969c4900c6962998d4e01d91a1a9384c"
#define T_REAL "744dc2f654b97b0e0cd30247585d27d7f7efaf2964fc3b14c04e873d8a4d9587"
static const es2_screen_expect expect_shown[] = {{"shown", "fb00995ffb07f7358452cb06323778d1e57bf133aa796072649476af2823e5c5", T_SYN, NULL}};
#define AFTER(name, frame) static const es2_screen_expect name[] = {{"after", frame, T_SYN, NULL}};
AFTER(expect_next, "83d24bf4f2c9242ce8f1811a30677a6d8025cf253e1cba27df1673f52507afd5")
#define F_RACK "ae6eeba4855a4ca3ef35d65d5b3c63437ab6db243f37e4bb5a5772dcc4245abf"
AFTER(expect_rack, F_RACK)
AFTER(expect_sarh, "3f26ecd77f64f5afc9a4869af10ca1e0e11a5251fce94fd484240a049c74716d")
AFTER(expect_mount, "cb8f2adfe1e6a341c95ea816e7b230e3e163aea865411989822c40c7f27a015d")
/* prev from no slot is capacity - 1: the rack slot, the same frame */
AFTER(expect_prev, F_RACK)
/* VSHELL's game_r:weapons:from_bay is this panel (equal in its area)
 * with the bay's tab bar, the herc picture and the squad panel of the career;
 * //shell:gui_tour_real_test compares es2shell's whole frame with it (equal). */
static const es2_screen_expect expect_real_shown[] = {{"shown", "21ebd1fa6a33099e247528c9636d4adfc4d0bfb3d6d2ebf22c88cd32888259a9", T_REAL, NULL}};
static const es2_screen_expect expect_real_next[] = {{"after", "f262b6205c1ae852cee36ce8337eb5b935b19eb70adcb63833d775246fe64c54", T_REAL, NULL}};

#define CASE(name, data, setup_fn, script, expect)                                                             \
    {"weapons." name, "weapons", data, sizeof(wp_state), setup_fn, script, expect,                             \
     sizeof(expect) / sizeof((expect)[0]), NULL}

static const es2_screen_case cases[] = {
    CASE("shown", 0, setup_synthetic, script_shown, expect_shown),
    CASE("slot_next", 0, setup_synthetic, script_next, expect_next),
    CASE("slot_rack", 0, setup_synthetic, script_rack, expect_rack),
    CASE("guidance_sarh", 0, setup_synthetic, script_sarh, expect_sarh),
    CASE("mount", 0, setup_synthetic, script_mount, expect_mount),
    CASE("slot_prev", 0, setup_synthetic, script_prev, expect_prev),
    CASE("real_shown", 1, setup_real, script_shown, expect_real_shown),
    CASE("real_slot_next", 1, setup_real, script_next, expect_real_next),
};

ES2_SCREEN_SUITE(weapons, cases)
