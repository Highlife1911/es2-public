/* Frame hashes of the BUILD screen 0x445de8, the ARMORY screen
 * 0x4484c4 and the weapons scrap warning 0x447e3e (screen test harness).
 *
 * Synthetic cases only (main_menu_fixture.h: a fixed-width font and a seeded
 * palette; build_armory_fixture.h: a synthetic loaded game, the image list
 * gam\arm_weap.dat, the nine Herc layer lists and seeded images). The screens
 * are shown as the bay bar's BUILD 0x43aa62 and ARMORY 0x43ab62 do after the
 * bay (screen word -1), without the bay's own frame and squad panel.
 * States: BUILD shown (type 0, bay 0 with the player's Herc), type 1
 * selected, an empty bay with Build enabled, after a Build; ARMORY shown in
 * auto and in manual building, another row selected, an entry added (left)
 * and removed (right), the weapons scrap warning and the screen after its
 * ACCEPT. No capture-tour reference exists yet (vshell_ui.md section 31 lists
 * the capture states). */
#include "screen_harness.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "build_armory_fixture.h"
#include "main_menu_fixture.h"

typedef struct bas_state {
    es2_ui ui;
    es2_shell_flow flow;
    es2_shell_editor_session session;
    es2_build_armory_game game;
    es2_build_armory screen;
    baf fx;
} bas_state;

static es2_dfn font;
static uint8_t font_bytes[16384];
static uint8_t records[256][4];
static int ready;

static const es2_dfn *h_font(void *c, uint32_t g) { (void)c; return g == 0x46ddfc || g == 0x46de00 ? &font : NULL; }
static const es2_bitmap *h_bitmap(void *c, const char *file, int m) { return baf_bitmap(&((bas_state *)c)->fx, file, m); }
static const char *h_text(void *c, uint32_t id) {
    static char t[64][16];
    (void)c;
    snprintf(t[id & 63], sizeof t[0], "TEXT %X", (unsigned)id);
    return t[id & 63];
}
static const char *h_name(void *c, int id) {
    static char t[33][16];
    (void)c;
    if (id < 0 || id > 32) return NULL;
    snprintf(t[id], sizeof t[0], "WEAPON %d", id);
    return t[id];
}
static const char *h_info(void *c, int index) {
    static char t[16][24];
    static unsigned next;
    char *s = t[next++ & 15];
    (void)c;
    snprintf(s, sizeof t[0], "INFO LINE %d", index);
    return s;
}
static es2_span h_file(void *c, const char *name) { return baf_file(&((bas_state *)c)->fx, name); }

static void on_event(void *context, const es2_gui_event *ev) { es2_ui_on_event(&((bas_state *)context)->ui, ev); }
static void on_timer(void *context, es2_gui_shell *shell) {
    bas_state *st = context;
    st->ui.now += 100;
    es2_ui_on_timer(&st->ui, shell);
}

static int setup(es2_screen_session *s, void *state, int armory, uint32_t salvage, int building, int16_t bay) {
    bas_state *st = state;
    es2_shell_flow_host fh;
    es2_gui_rect all = {0, 0, ES2_GUI_WIDTH - 1, ES2_GUI_HEIGHT - 1};
    uint8_t prefs[ES2_SHELL_PREFS_SIZE] = {0};
    es2_span file = {prefs, sizeof prefs};
    es2_build_armory_host host = {st, h_font, h_bitmap, h_text, h_name, h_info, h_file, NULL, NULL, NULL};
    if (!ready) {
        if (mmf_font(font_bytes, &font)) return 1;
        mmf_palette(records);
        ready = 1;
    }
    baf_init(&st->fx);
    baf_salvage(&st->fx, salvage);
    if (baf_game(&st->fx, &st->game, &st->session, bay)) return 2;
    if (es2_ui_init(&st->ui, s->shell)) return 20;
    st->ui.context = st;
    memset(&fh, 0, sizeof fh);
    prefs[ES2_SHELL_PREFS_BUILD] = (uint8_t)building;
    prefs[ES2_SHELL_PREFS_MODE] = 1;
    es2_shell_flow_init(&st->flow, &fh, &file);
    es2_build_armory_init(&st->screen, &st->flow, &st->game, &host);
    if (es2_screen_build_build(&st->ui, &st->screen) || es2_screen_armory_build(&st->ui, &st->screen) ||
        es2_screen_weapon_scrap_box_build(&st->ui, &st->screen))
        return 21;
    es2_ui_show(&st->ui, st->ui.root);
    memset(s->shell->frame.pixels, 0, ES2_GUI_PIXELS);
    s->shell->frame.realize_gate = 1;
    es2_gui_palette_select(&s->shell->frame, (const uint8_t(*)[4])records);
    es2_gui_palette_apply(&s->shell->frame);
    es2_gui_invalidate_window(&s->shell->frame, all);
    s->shell->input.interactive = 1;
    if ((armory ? es2_armory_show(&st->screen) : es2_build_show(&st->screen)) != ES2_OK) return 22;
    s->screen.context = st;
    s->screen.handle = on_event;
    s->screen.timer = on_timer;
    return 0;
}
/* the SAV is freed by the next setup's fixture (one case at a time); the
 * last one at exit */
static int build_default(es2_screen_session *s, void *st, const char *r) { (void)r; return setup(s, st, 0, 50000, 0, 0); }
static int build_empty(es2_screen_session *s, void *st, const char *r) { (void)r; return setup(s, st, 0, 230000, 0, 2); }
static int armory_auto(es2_screen_session *s, void *st, const char *r) { (void)r; return setup(s, st, 1, 50000, 0, 0); }
static int armory_manual(es2_screen_session *s, void *st, const char *r) { (void)r; return setup(s, st, 1, 50000, 1, 0); }

#define SNAP(label) "step 2\nsnap " label "\n"
#define CLICK(control) "step 2\nclick " control "\nstep 2\nsnap after\n"
static const char script_shown[] = SNAP("shown");
/* row 1 selected (left), then its entry removed (right) */
static const char script_remove[] = "step 2\nclick armory.38\nstep 2\nclick armory.38 right\nstep 2\nsnap after\n";
static const char script_scrap[] = "step 2\nclick armory.10\nstep 2\nsnap box\nclick weapon_scrap_box.09\nstep 2\nsnap after\n";

#define T_SYN "98fd384cc75016998f60c80a25cbc264969c4900c6962998d4e01d91a1a9384c"
#define EXPECT(name, snap, frame) static const es2_screen_expect name[] = {{snap, frame, T_SYN, NULL}};
EXPECT(expect_build_shown, "shown", "c1c6cabaef43180b76ae8d97d9e3694f6ee61369022e0a5dffc25f2bc3734eee")
EXPECT(expect_build_type1, "after", "6dce78464803d2cb135e0a4836c65573ecf0e72d2de7464443e0086e318ff775")
EXPECT(expect_build_empty, "shown", "3ad9a18ebb5e97ba853d4c2901910854cd852b6861d897db837f9387bbdce4f1")
EXPECT(expect_build_built, "after", "1943cebd2152de1608a1f3be8ccfd9f393756fca649dc70ff212bf0e2d53519f")
EXPECT(expect_armory_shown, "shown", "a90586789aeb13162c9ab24c4747916920180404cf4f63cede6a78943119bb11")
EXPECT(expect_armory_manual, "shown", "355bac0f1908498500299acbbd4803507aacb0a2dace51a6f8d5d1e0ad39c690")
EXPECT(expect_armory_row, "after", "53a15d7297bcaec02b6e9b5549ded1e3dc747fd35bee163905ede14ba0f99fa9")
EXPECT(expect_armory_add, "after", "0b275da37df7253f092ea4d97cf6063672251e3089e3204e06e44f438f6f0bdb")
EXPECT(expect_armory_remove, "after", "e14251404f8b7a9549bfb64efb0b8a9836bb6e25dad5120e07339fea897385b6")
static const es2_screen_expect expect_armory_scrap[] = {
    {"box", "41fe61e2a307529dbc93b0af66827a261ede3683e67ff72fce3d822db3d3cda5", T_SYN, NULL},
    {"after", "e85f6c364a29a412ed4dfed3d4c3d192bc409b00f358d60975eda1b49ad037e0", T_SYN, NULL}};

#define CASE(name, screen, setup_fn, script, expect)                                                           \
    {"build_armory." name, screen, 0, sizeof(bas_state), setup_fn, script, expect,                             \
     sizeof(expect) / sizeof((expect)[0]), NULL}

static const es2_screen_case cases[] = {
    CASE("build_shown", "build", build_default, script_shown, expect_build_shown),
    CASE("build_type1", "build", build_default, CLICK("build.17"), expect_build_type1),
    CASE("build_empty_bay", "build", build_empty, script_shown, expect_build_empty),
    CASE("build_built", "build", build_empty, CLICK("build.78"), expect_build_built),
    CASE("armory_shown", "armory", armory_auto, script_shown, expect_armory_shown),
    CASE("armory_manual", "armory", armory_manual, script_shown, expect_armory_manual),
    CASE("armory_row", "armory", armory_auto, CLICK("armory.43"), expect_armory_row),
    CASE("armory_add", "armory", armory_manual, CLICK("armory.33"), expect_armory_add),
    CASE("armory_remove", "armory", armory_manual, script_remove, expect_armory_remove),
    CASE("armory_scrap", "armory", armory_auto, script_scrap, expect_armory_scrap),
};

ES2_SCREEN_SUITE(build_armory, cases)
