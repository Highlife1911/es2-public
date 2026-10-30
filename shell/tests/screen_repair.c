/* Frame hashes of the REPAIR screen 0x4326c7 and the Herc scrap
 * warning 0x4479b8 (screen test harness).
 *
 * Synthetic cases (main_menu_fixture.h's fixed-width font and seeded
 * palette; the memory state of repair_fixture.h) and one real case
 * (needs_data: dfn\font2.dfn, dpl\palette.dpl and ENG\ESTEXT.BIN from the
 * CD's VOL\SHELL0.VOL, SHELL1.VOL and LANG0.VOL over the same synthetic
 * state). Only this screen is drawn: the service bay's background, its bar
 * and the squad panel are the service bay's and stay black here. States: the
 * screen as the REPAIR bar button shows it with bay 0 selected, an External
 * row, a repair, REPAIR ALL, CANCEL, an Internal row (the grid swap), the
 * warning and its CANCEL and ACCEPT, the screen without a finished Herc and
 * with no salvage available. No capture-tour reference exists yet; the capture
 * states a capture tour needs are listed in vshell_ui.md section 30. */
#include "screen_harness.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "es2/formats/dpl.h"
#include "es2/formats/vfs.h"
#include "es2/formats/vol.h"
#include "es2/shell/gui/repair.h"
#include "main_menu_fixture.h"
#include "menu.h"
#include "repair_fixture.h"

typedef struct rp_state {
    es2_ui ui;
    es2_shell_flow flow;
    es2_shell_editor_session session;
    es2_shell_buffer save;
    es2_repair_screen screen;
    repair_fixture fx;
    int real;
} rp_state;

/* ---- resources ------------------------------------------------------------- */
typedef struct rp_resources {
    int ready;
    es2_dfn font;
    uint8_t records[256][4];
} rp_resources;
static rp_resources synthetic, real;
static uint8_t synthetic_font[16384];
static es2_span real_estext;

static int synthetic_load(void) {
    if (synthetic.ready) return 0;
    if (mmf_font(synthetic_font, &synthetic.font)) return 1;
    mmf_palette(synthetic.records);
    synthetic.ready = 1;
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

/* The VOLs stay mapped for the process (the font and the texts borrow them). */
static es2_vol vols[3];
static es2_vfs_source sources[3];
static es2_vfs vfs;
static int real_load(const char *root) {
    static const char *const names[3] = {"SHELL0.VOL", "SHELL1.VOL", "LANG0.VOL"};
    es2_vfs_copy c;
    char path[1024];
    if (real.ready) return real.ready < 0 ? 2 : 0;
    real.ready = -1;
    for (int i = 0; i < 3; i++) {
        size_t size = 0;
        uint8_t *data;
        snprintf(path, sizeof path, "%s/VOL/%s", root, names[i]);
        if (!(data = slurp(path, &size)) || es2_vol_open((es2_span){data, size}, &vols[i], NULL) != ES2_OK) return 2;
        sources[i].kind = ES2_VFS_VOL;
        sources[i].data.vol = &vols[i];
    }
    if (es2_vfs_open(sources, 3, &vfs, NULL) != ES2_OK) return 3;
    if (es2_vfs_find(&vfs, "dfn\\font2.dfn", &c, NULL) != ES2_OK || es2_dfn_parse(c.payload, &real.font, NULL) != ES2_OK)
        return 4;
    {
        es2_dpl dpl;
        if (es2_vfs_find(&vfs, es2_palette_file(ES2_PALETTE_MAIN), &c, NULL) != ES2_OK ||
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
    real_estext = c.payload;
    real.ready = 1;
    return 0;
}

static int is_real(void *ctx) { return ((rp_state *)ctx)->real; }
static const es2_dfn *h_font(void *ctx, uint32_t global) {
    return global == 0x46de00 ? &(is_real(ctx) ? &real : &synthetic)->font : NULL;
}
static const char *h_text(void *ctx, uint32_t id) {
    static char texts[128][32];
    static unsigned next;
    char *t = texts[next++ & 127];
    if (is_real(ctx)) {
        es2_span s;
        size_t n;
        if (es2m_bin_raw(real_estext, id, &s, NULL) != ES2_OK) return NULL;
        n = s.size < sizeof texts[0] - 1 ? s.size : sizeof texts[0] - 1;
        memcpy(t, s.data, n);
        t[n] = 0;
        return t;
    }
    snprintf(t, sizeof texts[0], "TEXT %X", (unsigned)id);
    return t;
}
/* The literals the table names are empty strings or the constructor's
 * placeholders, which every show overwrites. */
static const char *h_literal(void *ctx, uint32_t address) {
    (void)ctx;
    (void)address;
    return "";
}

static void on_event(void *context, const es2_gui_event *ev) { es2_ui_on_event(&((rp_state *)context)->ui, ev); }
static void on_timer(void *context, es2_gui_shell *shell) {
    rp_state *st = context;
    st->ui.now += 100;
    es2_ui_on_timer(&st->ui, shell);
}

enum { STATE_PLAIN, STATE_NO_HERC, STATE_NO_SALVAGE };

static int setup(es2_screen_session *s, void *state, int real_data, int variant, const char *root) {
    rp_state *st = state;
    es2_shell_flow_host fh;
    es2_gui_rect all = {0, 0, ES2_GUI_WIDTH - 1, ES2_GUI_HEIGHT - 1};
    uint8_t prefs[ES2_SHELL_PREFS_SIZE];
    es2_span file = {prefs, sizeof prefs};
    rp_resources *r;
    size_t n = 0;
    int code = real_data ? real_load(root) : synthetic_load();
    if (code) return code;
    st->real = real_data;
    r = real_data ? &real : &synthetic;
    rf_init(&st->fx);
    if (variant == STATE_NO_HERC)
        for (int k = 0; k < 3; k++) rf_w(st->fx.bays[k] + 70, 40);
    if (variant == STATE_NO_SALVAGE) rf_salvage(&st->fx, 0);
    if (!(st->save.data = rf_save(&st->fx, &n)) || rf_vfs(&st->fx)) return 19;
    st->save.size = n;
    if (es2_ui_init(&st->ui, s->shell)) return 20;
    st->ui.context = st;
    memset(&fh, 0, sizeof fh);
    memset(prefs, 0, sizeof prefs);
    prefs[ES2_SHELL_PREFS_MODE] = 1;
    es2_shell_flow_init(&st->flow, &fh, &file);
    es2_shell_editor_session_init(&st->session, 0);
    st->screen.flow = &st->flow;
    st->screen.session = &st->session;
    st->screen.vfs = &st->fx.vfs;
    st->screen.tables = (es2_span){st->fx.tables, sizeof st->fx.tables};
    st->screen.editor_tables = (es2_span){st->fx.editor, sizeof st->fx.editor};
    st->screen.save = &st->save;
    st->screen.host = (es2_repair_screen_host){st, h_font, h_text, h_literal, NULL, NULL};
    if (es2_screen_repair_build(&st->ui, &st->screen)) return 21;
    if (es2_screen_herc_scrap_box_build(&st->ui, &st->screen)) return 22;
    es2_ui_show(&st->ui, st->ui.root);
    memset(s->shell->frame.pixels, 0, ES2_GUI_PIXELS);
    s->shell->frame.realize_gate = 1;
    es2_gui_palette_select(&s->shell->frame, (const uint8_t(*)[4])r->records);
    es2_gui_palette_apply(&s->shell->frame);
    es2_gui_invalidate_window(&s->shell->frame, all);
    s->shell->input.interactive = 1;
    /* the REPAIR bar button 0x43a962 from the bay alone (word -1) */
    if (es2_shell_flow_event(&st->flow, ES2_FLOW_BAY_REPAIR, NULL) == ES2_FLOW_FAILED) return 23;
    if (es2_repair_screen_show(&st->screen, ES2_FLOW_WORD_BAY) != ES2_FLOW_DONE) return 24;
    s->screen.context = st;
    s->screen.handle = on_event;
    s->screen.timer = on_timer;
    return 0;
}
static int setup_plain(es2_screen_session *s, void *state, const char *root) {
    return setup(s, state, 0, STATE_PLAIN, root);
}
static int setup_no_herc(es2_screen_session *s, void *state, const char *root) {
    return setup(s, state, 0, STATE_NO_HERC, root);
}
static int setup_no_salvage(es2_screen_session *s, void *state, const char *root) {
    return setup(s, state, 0, STATE_NO_SALVAGE, root);
}
static int setup_real(es2_screen_session *s, void *state, const char *root) {
    return setup(s, state, 1, STATE_PLAIN, root);
}

/* ---- scripts ---------------------------------------------------------------- */
#define CLICK(control) "step 2\nclick " control "\nstep 2\n"
static const char script_shown[] = "step 2\nsnap shown\n";
static const char script_row[] = CLICK("repair.22") "snap after\n";
static const char script_repair[] = CLICK("repair.22") CLICK("repair.153") "snap after\n";
static const char script_all[] = CLICK("repair.160") "snap after\n";
static const char script_cancel[] = CLICK("repair.22") CLICK("repair.153") CLICK("repair.166") "snap after\n";
static const char script_internal[] = CLICK("repair.104") "snap after\n";
static const char script_box[] = CLICK("repair.164") "snap box\n" CLICK("herc_scrap_box.07") "snap after\n";
static const char script_accept[] = CLICK("repair.164") CLICK("herc_scrap_box.09") "snap after\n";

/* ---- expectations (our hashes; reference NULL until a capture tour)
 * -------------- */
#define T_SYN "98fd384cc75016998f60c80a25cbc264969c4900c6962998d4e01d91a1a9384c"
#define T_REAL "96b549df7c20ad4f6b89e13333204a31f25ffedaf43cbf4bcc330bef14f4042a"
#define F_SHOWN "65a9e898e0388915b8b834254e60eca1dddcd15d60052a503e72883a7021dfae"
static const es2_screen_expect expect_shown[] = {{"shown", F_SHOWN, T_SYN, NULL}};
#define AFTER(name, frame) static const es2_screen_expect name[] = {{"after", frame, T_SYN, NULL}};
#define F_ROW "6908f0920b4a5a2c46955757201133f70375f0bd9218f4bfc0dfcc89f8fc9b7e"
AFTER(expect_row, F_ROW)
AFTER(expect_repair, "3efe1703ac9ba44c62ac704602b4e95ac2e8f088454366f86e5ec3ae600cb031")
AFTER(expect_all, "cc63a9fa414f156806ca27d7f4e504f9a20bdc58f680e1bfeeeb48c09d565ab5")
AFTER(expect_internal, "361ab082411f6ed27134a68d4d6ebda92c72ae7121ab33b80450d3438a81e76d")
AFTER(expect_accept, "1f39e8c74778c5d0be41e33ab661acb8d849da27da948d7182561412bb04b3e3")
/* CANCEL restores the state of the show: the frame of the row click before the repair */
AFTER(expect_cancel, F_ROW)
/* the warning over the screen; its CANCEL leaves the screen as shown */
static const es2_screen_expect expect_box[] = {
    {"box", "de905ada72404cf6f69ef4d291ed0081a13ad35909097f60df564f05402322b3", T_SYN, NULL},
    {"after", F_SHOWN, T_SYN, NULL}};
static const es2_screen_expect expect_no_herc[] = {
    {"shown", "24c9591f4b04c152d05fbc06239043b6397a7ec5e765f1736b6a03ad0317922e", T_SYN, NULL}};
static const es2_screen_expect expect_no_salvage[] = {
    {"shown", "3d31acc34fded13e8996fe86cdb184d87b3e85f07a14b527e12eee248de27410", T_SYN, NULL}};
#define REAL_SHOWN "423250c7a11dc58d0d86c2b7d9f54c6fe4446368a9cc0dd01bafc4ddfc6dbcb3"
static const es2_screen_expect expect_real_shown[] = {{"shown", REAL_SHOWN, T_REAL, NULL}};
static const es2_screen_expect expect_real_box[] = {
    {"box", "da59c0a424fbeda0c838abec4dd61c91b6bb46587e0403b2a317a7681eae25c9", T_REAL, NULL},
    {"after", REAL_SHOWN, T_REAL, NULL}};

#define CASE(name, data, setup_fn, script, expect)                                                             \
    {"repair." name, "repair", data, sizeof(rp_state), setup_fn, script, expect,                               \
     sizeof(expect) / sizeof((expect)[0]), NULL}

static const es2_screen_case cases[] = {
    CASE("shown", 0, setup_plain, script_shown, expect_shown),
    CASE("external_row", 0, setup_plain, script_row, expect_row),
    CASE("repaired", 0, setup_plain, script_repair, expect_repair),
    CASE("repair_all", 0, setup_plain, script_all, expect_all),
    CASE("cancel", 0, setup_plain, script_cancel, expect_cancel),
    CASE("internal_row", 0, setup_plain, script_internal, expect_internal),
    CASE("scrap_box", 0, setup_plain, script_box, expect_box),
    CASE("scrap_accept", 0, setup_plain, script_accept, expect_accept),
    CASE("no_herc", 0, setup_no_herc, script_shown, expect_no_herc),
    CASE("no_salvage", 0, setup_no_salvage, script_shown, expect_no_salvage),
    CASE("real_shown", 1, setup_real, script_shown, expect_real_shown),
    CASE("real_scrap_box", 1, setup_real, script_box, expect_real_box),
};

ES2_SCREEN_SUITE(repair, cases)
