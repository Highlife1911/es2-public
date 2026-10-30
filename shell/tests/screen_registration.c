/* Screen cases of the Registration screen (es2/shell/gui/registration.h)
 * on the screen test harness. The synthetic cases build the screen from the
 * layout table with a synthetic font, a seeded background and placeholder texts
 * ("T30" for ESTEXT 0x30, ...); the real case reads dfn\font2, dbm\bay2a_84,
 * dpl\palette.dpl and ENG\ESTEXT.BIN from the CD's VOL archives. A small
 * presenter in each case plays the main menu's part: START NEW GAME on the
 * flow, then the screen's show 0x43c29a; with `reopen` it does that again
 * whenever the flow returns to the main menu. VSHELL references do not exist
 * yet: the expectations are our own hashes. */
#include "screen_harness.h"

#include "es2/formats/dbm.h"
#include "es2/formats/dfn.h"
#include "es2/formats/dpl.h"
#include "es2/formats/vfs.h"
#include "es2/formats/vol.h"
#include "es2/shell/draw.h"
#include "es2/shell/flow.h"
#include "es2/shell/gui/registration.h"
#include "es2/shell/gui/widgets.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct reg_state {
    es2_screen_session *session;
    es2_ui ui;
    es2_shell_flow flow;
    es2_screen_registration reg;
    int reopen;
    int new_games;
    char new_name[64];
    int new_rank;
    char text[128];   /* the last ESTEXT answer */
    es2_span estext;  /* real case: the ESTEXT catalogue */
    unsigned clock;   /* ms per main-loop pass (0: the timer list's clock stands still) */
} reg_state;

/* ---- the flow's host: records the new game --------------------------------- */
static int host_store_mode(void *ctx, uint8_t mode) { (void)ctx; (void)mode; return 1; }
static int host_new_game(void *ctx, const char *name, uint16_t rank, int16_t *final_word) {
    reg_state *st = ctx;
    (void)final_word;
    st->new_games++;
    snprintf(st->new_name, sizeof st->new_name, "%s", name);
    st->new_rank = rank;
    return 1;
}

/* ---- synthetic resources ----------------------------------------------------- */
/* A DFN with codes 32..126, height 7, baseline 6, glyph widths 3..5. */
enum { FC = 32, FN = 95, FH = 7, FB = 6 };
static uint8_t font_bytes[16384];
static es2_dfn synthetic_font;
static size_t put16(uint8_t *p, size_t at, int v) { p[at] = (uint8_t)v; p[at + 1] = (uint8_t)((unsigned)v >> 8); return at + 2; }
static size_t put32(uint8_t *p, size_t at, uint32_t v) { put16(p, at, (int)(v & 0xffff)); return put16(p, at + 2, (int)(v >> 16)); }
static int glyph_width(int c) { return c == ' ' ? 3 : 3 + c % 3; }
static int make_font(void) {
    uint8_t *b = font_bytes;
    size_t at = 8, data_size = 0, data_at, off = 0;
    for (int i = 0; i < FN; i++) data_size += (size_t)glyph_width(FC + i) * FH;
    at = put16(b, at, FN); at = put16(b, at, 0); at = put16(b, at, FC); at = put16(b, at, FH);
    at = put16(b, at, -1); at = put16(b, at, FH); at = put16(b, at, 5); at = put16(b, at, 8);
    at = put16(b, at, 0); at = put16(b, at, FB); at = put16(b, at, 0); at = put32(b, at, (uint32_t)data_size);
    data_at = at;
    for (int i = 0; i < FN; i++) {
        int w = glyph_width(FC + i);
        for (int r = 0; r < FH; r++)
            for (int c = 0; c < w; c++)
                b[data_at + off + (size_t)r * (size_t)w + (size_t)c] = (uint8_t)((r * 3 + c + i) % 4 ? 41 : 0);
        off += (size_t)w * FH;
    }
    at = data_at + data_size;
    off = 0;
    for (int i = 0; i < FN; i++) {
        at = put32(b, at, (uint32_t)off);
        off += (size_t)glyph_width(FC + i) * FH;
    }
    for (int i = 0; i < FN; i++) b[at++] = (uint8_t)glyph_width(FC + i);
    b[0] = 5; b[1] = 0; b[2] = 0x28; b[3] = 0;
    put32(b, 4, (uint32_t)(at - 8));
    if ((at - 8) & 1) b[at++] = 0;
    return es2_dfn_parse((es2_span){font_bytes, at}, &synthetic_font, NULL) == ES2_OK;
}
static uint8_t background_pixels[ES2_GUI_PIXELS];
static es2_bitmap synthetic_background = {ES2_GUI_HEIGHT, ES2_GUI_WIDTH, 0, background_pixels};

static const char *synthetic_text(void *context, int id) {
    reg_state *st = context;
    snprintf(st->text, sizeof st->text, "T%02X", (unsigned)id & 0xffff);
    return st->text;
}

/* ---- the test presenter ------------------------------------------------------- */
static void present(void *context, const es2_gui_event *ev) {
    reg_state *st = context;
    es2_shell_flow_screen before = st->flow.screen;
    es2_ui_on_event(&st->ui, ev);
    if (st->reopen && before == ES2_FLOW_REGISTRATION && st->flow.screen == ES2_FLOW_MAIN_MENU) {
        (void)es2_shell_flow_event(&st->flow, ES2_FLOW_START_NEW_GAME, NULL);
        es2_screen_registration_show(&st->reg);
    }
}
static void timer(void *context, es2_gui_shell *shell) {
    reg_state *st = context;
    st->ui.now += st->clock;
    es2_ui_on_timer(&st->ui, shell);
}

/* The screen built and shown after START NEW GAME, with the given resources. */
static int open_screen(es2_screen_session *s, reg_state *st, const uint8_t records[256][4]) {
    es2_shell_flow_host host;
    memset(&host, 0, sizeof host);
    host.ctx = st;
    host.store_mode = host_store_mode;
    host.new_game = host_new_game;
    st->session = s;
    es2_shell_flow_init(&st->flow, &host, NULL);
    if (es2_ui_init(&st->ui, s->shell)) return 2;
    st->reg.flow = &st->flow;
    if (es2_screen_registration_build(&st->ui, &st->reg)) return 3;
    s->shell->frame.realize_gate = 1;
    es2_gui_palette_select(&s->shell->frame, records);
    s->shell->input.interactive = 1;
    s->screen.context = st;
    s->screen.handle = present;
    s->screen.timer = timer;
    es2_ui_show(&st->ui, st->ui.root);
    if (es2_shell_flow_event(&st->flow, ES2_FLOW_START_NEW_GAME, NULL) != ES2_FLOW_DONE) return 4;
    es2_screen_registration_show(&st->reg);
    return 0;
}

static int setup_synthetic(es2_screen_session *s, reg_state *st) {
    uint8_t records[256][4];
    if (!make_font()) return 1;
    es2_screen_fixture_image(background_pixels, ES2_GUI_WIDTH, ES2_GUI_HEIGHT, 30);
    es2_screen_fixture_palette(records, 30);
    st->reg.font_button = st->reg.font_label = &synthetic_font;
    st->reg.background = &synthetic_background;
    st->reg.estext = synthetic_text;
    st->reg.estext_context = st;
    return open_screen(s, st, (const uint8_t(*)[4])records);
}
static int setup(es2_screen_session *s, void *state, const char *data_root) {
    (void)data_root;
    return setup_synthetic(s, state);
}
static int setup_reopen(es2_screen_session *s, void *state, const char *data_root) {
    reg_state *st = state;
    (void)data_root;
    st->reopen = 1;
    return setup_synthetic(s, state);
}

/* Behaviour through the screen (no hashes): a failed check returns its
 * number as the setup's code. */
static int setup_behaviour(es2_screen_session *s, void *state, const char *data_root) {
    reg_state *st = state;
    es2_ui_widget *accept;
    int code = setup_synthetic(s, state);
    (void)data_root;
    if (code) return code;
    accept = es2_ui_get(&st->ui, st->reg.ids[ES2_REGISTRATION_ACCEPT]);
    /* built: ACCEPT disabled, label colour 38, frame 34; the skill label T35 */
    if (!accept || accept->enabled || accept->colour != 34) return 10;
    if (es2_ui_get(&st->ui, accept->label)->text_colour != 38) return 11;
    if (strcmp(es2_ui_get(&st->ui, st->reg.ids[ES2_REGISTRATION_SKILL_LABEL])->text, "T35")) return 12;
    /* the show posted a press on the field: after a step it edits with capture */
    if (es2_screen_run(s, "step\n")) return 13;
    if (!es2_ui_get(&st->ui, st->reg.ids[ES2_REGISTRATION_FIELD])->edit.active || !st->ui.capture) return 14;
    /* ACCEPT while disabled does nothing */
    if (es2_screen_run(s, "click registration.14\n") || st->flow.screen != ES2_FLOW_REGISTRATION || st->new_games) return 15;
    /* typing enables it (41 / 34); backspace to empty disables it (38 / 38) */
    if (es2_screen_run(s, "click registration.07\ntype AB\n")) return 16;
    if (strcmp(es2_screen_registration_name(&st->reg), "AB") || !accept->enabled || accept->colour != 34 ||
        es2_ui_get(&st->ui, accept->label)->text_colour != 41)
        return 17;
    if (es2_screen_run(s, "press backspace\npress backspace\n")) return 18;
    if (es2_screen_registration_name(&st->reg)[0] || accept->enabled || accept->colour != 38 ||
        es2_ui_get(&st->ui, accept->label)->text_colour != 38)
        return 19;
    /* SKILL: left and right release both step it; the label follows */
    if (es2_screen_run(s, "type ACE\nclick registration.10\nclick registration.10 right\n")) return 20;
    if (st->flow.skill != 2 || strcmp(es2_ui_get(&st->ui, st->reg.ids[ES2_REGISTRATION_SKILL_LABEL])->text, "T37"))
        return 21;
    /* CANCEL: hidden, main menu, nothing created */
    if (es2_screen_run(s, "click registration.12\n")) return 22;
    if (st->flow.screen != ES2_FLOW_MAIN_MENU || st->new_games ||
        !(es2_ui_get(&st->ui, st->reg.ids[ES2_REGISTRATION_BACKGROUND])->flags & ES2_UI_HIDDEN))
        return 23;
    /* shown again: skill, name and ACCEPT's state are kept (no reset) */
    (void)es2_shell_flow_event(&st->flow, ES2_FLOW_START_NEW_GAME, NULL);
    es2_screen_registration_show(&st->reg);
    if (es2_screen_run(s, "step\n")) return 24;
    if (st->flow.skill != 2 || strcmp(es2_screen_registration_name(&st->reg), "ACE") || !accept->enabled) return 25;
    if (strcmp(es2_ui_get(&st->ui, st->reg.ids[ES2_REGISTRATION_SKILL_LABEL])->text, "T37")) return 26;
    /* ACCEPT: the new game with the name and the skill as rank, page 0, bay */
    st->flow.page = 4;
    if (es2_screen_run(s, "click registration.14\n")) return 27;
    if (st->new_games != 1 || strcmp(st->new_name, "ACE") || st->new_rank != 2) return 28;
    if (st->flow.screen != ES2_FLOW_SERVICE_BAY || st->flow.page != 0 || st->flow.game != ES2_FLOW_GAME_CAMPAIGN) return 29;
    if (!(es2_ui_get(&st->ui, st->reg.ids[ES2_REGISTRATION_PANEL])->flags & ES2_UI_HIDDEN)) return 30;
    return 0;
}

/* ---- real data ----------------------------------------------------------------- */
static uint8_t *read_file(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    uint8_t *data = NULL;
    long n;
    if (!f) return NULL;
    if (!fseek(f, 0, SEEK_END) && (n = ftell(f)) >= 0 && !fseek(f, 0, SEEK_SET) && (data = malloc((size_t)n + 1))) {
        if (fread(data, 1, (size_t)n, f) != (size_t)n) { free(data); data = NULL; }
        else *size = (size_t)n;
    }
    fclose(f);
    return data;
}

static es2_vol real_vols[3];
static es2_vfs_source real_sources[3];
static es2_vfs real_vfs;
static es2_dfn real_font;
static es2_bitmap real_background;
static uint8_t real_background_pixels[ES2_GUI_PIXELS];
static uint8_t real_records[256][4];
static es2_span real_estext;
static int real_ready;

static int find(const char *name, es2_span *out) {
    es2_vfs_copy c;
    if (es2_vfs_find(&real_vfs, name, &c, NULL) != ES2_OK) return 0;
    *out = c.payload;
    return 1;
}
static int mount_real(const char *root) {
    static const char *const names[3] = {"SHELL0.VOL", "SHELL1.VOL", "LANG0.VOL"};
    es2_span span;
    es2_dbm dbm;
    es2_dpl dpl;
    if (real_ready) return real_ready > 0;
    real_ready = -1;
    for (int i = 0; i < 3; i++) {
        char path[1024];
        size_t size = 0;
        uint8_t *data;
        snprintf(path, sizeof path, "%s/%s", root, names[i]);
        if (!(data = read_file(path, &size))) {
            snprintf(path, sizeof path, "%s/VOL/%s", root, names[i]);
            data = read_file(path, &size);
        }
        if (!data || es2_vol_open((es2_span){data, size}, &real_vols[i], NULL) != ES2_OK) return 0;
        real_sources[i].kind = ES2_VFS_VOL;
        real_sources[i].data.vol = &real_vols[i];
    }
    if (es2_vfs_open(real_sources, 3, &real_vfs, NULL) != ES2_OK) return 0;
    if (!find("dfn\\font2.dfn", &span) || es2_dfn_parse(span, &real_font, NULL) != ES2_OK) return 0;
    if (!find("dbm\\bay2a_84.dbm", &span) || es2_dbm_parse(span, &dbm, NULL) != ES2_OK ||
        es2_draw_bitmap_from_dbm(&dbm, real_background_pixels, sizeof real_background_pixels, &real_background) !=
            ES2_DRAW_DONE)
        return 0;
    if (!find(es2_palette_file(ES2_PALETTE_MAIN), &span) || es2_dpl_parse(span, &dpl, NULL) != ES2_OK) return 0;
    for (size_t i = 0; i < 256 && (i + 1) * 4 <= dpl.colours.size; i++) memcpy(real_records[i], dpl.colours.data + 4 * i, 4);
    if (!find("ENG\\ESTEXT.BIN", &real_estext)) return 0;
    real_ready = 1;
    return 1;
}
static unsigned le16(const uint8_t *p) { return (unsigned)p[0] | (unsigned)p[1] << 8; }
static uint32_t le32(const uint8_t *p) { return (uint32_t)le16(p) | (uint32_t)le16(p + 2) << 16; }
/* An ESTEXT entry up to its NUL (u32 count, u32, count u16 offsets from 8 + 2 count). */
static const char *real_text(void *context, int id) {
    reg_state *st = context;
    const es2_span c = st->estext;
    size_t count, base, at, n = 0;
    st->text[0] = 0;
    if (c.size < 8 || id < 0) return st->text;
    count = le32(c.data);
    base = 8 + 2 * count;
    if ((size_t)id >= count || 8 + 2 * (size_t)id + 2 > c.size) return st->text;
    at = base + le16(c.data + 8 + 2 * (size_t)id);
    while (at + n < c.size && c.data[at + n] && n + 1 < sizeof st->text) n++;
    memcpy(st->text, c.data + at, n);
    st->text[n] = 0;
    return st->text;
}
static int setup_real(es2_screen_session *s, void *state, const char *data_root) {
    reg_state *st = state;
    if (!data_root || !mount_real(data_root)) return 1;
    if (es2_screen_main_menu_history(s, data_root, "07")) return 5; /* START NEW GAME over the menu */
    st->reg.font_button = st->reg.font_label = &real_font;
    st->reg.background = &real_background;
    st->estext = real_estext;
    st->reg.estext = real_text;
    st->reg.estext_context = st;
    st->clock = 100; /* 100 ms per pass, as the main menu's cases count */
    return open_screen(s, st, (const uint8_t(*)[4])real_records);
}

/* ---- cases ------------------------------------------------------------------------ */
static const es2_screen_expect start_expect[] = {
    {"start", "bbdcbdc6a393e8a4d5a4e8269607f9fabfbdd18ddcd39ac7b7a302775a4382f0",
     "1754c48bb19db91dbaeb427af741889d27155f908afff1c9c58bce5ec4a786b0", NULL},
};
static const es2_screen_expect tour_expect[] = {
    {"start", "bbdcbdc6a393e8a4d5a4e8269607f9fabfbdd18ddcd39ac7b7a302775a4382f0",
     "1754c48bb19db91dbaeb427af741889d27155f908afff1c9c58bce5ec4a786b0", NULL},
    {"typed", "e1f9ecfea6107dfe297fd10e4ed179e985e3c582d7231bd5a40d39ffabf2f635",
     "1754c48bb19db91dbaeb427af741889d27155f908afff1c9c58bce5ec4a786b0", NULL},
    {"skill1", "5d0529e96cd1bf5aecb936f64549c8694cef6c12ebe8a5e78ae7e236df63e663",
     "1754c48bb19db91dbaeb427af741889d27155f908afff1c9c58bce5ec4a786b0", NULL},
    {"skill0", "d38732a8b8e956c4ed04f9b3b93f5648a5b498de22080f2583e33eda70715343",
     "1754c48bb19db91dbaeb427af741889d27155f908afff1c9c58bce5ec4a786b0", NULL},
    {"empty", "b61591486de476696054d8d16b352157c8eafe0c931accaf3c7e666013506a90",
     "1754c48bb19db91dbaeb427af741889d27155f908afff1c9c58bce5ec4a786b0", NULL},
    {"cancelled", "db8d4d8529eaf9ed2901909272ee7ed04d9795ea043b7879c336ccf8c499d2b8",
     "1754c48bb19db91dbaeb427af741889d27155f908afff1c9c58bce5ec4a786b0", NULL},
};
static const es2_screen_expect reopen_expect[] = {
    {"before", "06dca3462aac9b3d9f478d98367902c238bdfd21c6f8d4abdfb7eafe87ca99ea",
     "1754c48bb19db91dbaeb427af741889d27155f908afff1c9c58bce5ec4a786b0", NULL},
    {"reopened", "cbeb97c871688080d51bccd7e98fd8b0b00f9bbb5b51eead0433c7dc5932153d",
     "1754c48bb19db91dbaeb427af741889d27155f908afff1c9c58bce5ec4a786b0", NULL},
    {"accepted", "db8d4d8529eaf9ed2901909272ee7ed04d9795ea043b7879c336ccf8c499d2b8",
     "1754c48bb19db91dbaeb427af741889d27155f908afff1c9c58bce5ec4a786b0", NULL},
};
static const es2_screen_expect behaviour_expect[] = {
    {"end", "db8d4d8529eaf9ed2901909272ee7ed04d9795ea043b7879c336ccf8c499d2b8",
     "1754c48bb19db91dbaeb427af741889d27155f908afff1c9c58bce5ec4a786b0", NULL},
};
static const es2_screen_expect real_expect[] = {
    {"open", "aa59e8a4c16ff1c84b939993747073166e4f40c6d5d74c515f6772fcbb1fa156",
     "96b549df7c20ad4f6b89e13333204a31f25ffedaf43cbf4bcc330bef14f4042a", NULL},
    {"start", "aa4735c17b6d266deacff9360d8bed978a8f851d64fd51242ed3661368843332",
     "96b549df7c20ad4f6b89e13333204a31f25ffedaf43cbf4bcc330bef14f4042a", "new_career:registration:empty"},
    {"typed", "616349b15e1fcee3e1e73490e8ed0091ed97351b2dae8f25b687814499a75311",
     "96b549df7c20ad4f6b89e13333204a31f25ffedaf43cbf4bcc330bef14f4042a", NULL},
    {"veteran", "9e6d4bbe29e4f4f4e7195f47c9e951b090ca8a98ad651ba28baff5d576971323",
     "96b549df7c20ad4f6b89e13333204a31f25ffedaf43cbf4bcc330bef14f4042a", NULL},
};

#define N(a) (sizeof(a) / sizeof((a)[0]))
static const es2_screen_case cases[] = {
    {"registration.start", "registration", 0, sizeof(reg_state), setup, "step\nsnap start\n", start_expect,
     N(start_expect), NULL},
    {"registration.tour", "registration", 0, sizeof(reg_state), setup,
     "step\n"
     "snap start\n"
     "type PILOT 7\n"
     "snap typed\n"
     "click registration.10\n"
     "snap skill1\n"
     "click registration.10\n"
     "click registration.10\n"
     "click registration.10\n"
     "snap skill0\n"
     "click registration.07\n"
     "press backspace\npress backspace\npress backspace\npress backspace\npress backspace\npress backspace\npress backspace\n"
     "snap empty\n"
     "click registration.12\n"
     "snap cancelled\n",
     tour_expect, N(tour_expect), NULL},
    {"registration.reopen", "registration", 0, sizeof(reg_state), setup_reopen,
     "step\n"
     "type ACE\n"
     "click registration.10\n"
     "click registration.10\n"
     "snap before\n"
     "click registration.12\n"
     "step\n"
     "snap reopened\n"
     "click registration.14\n"
     "snap accepted\n",
     reopen_expect, N(reopen_expect), NULL},
    {"registration.behaviour", "registration", 0, sizeof(reg_state), setup_behaviour, "step\nsnap end\n",
     behaviour_expect, N(behaviour_expect), NULL},
    {"registration.real", "registration", 1, sizeof(reg_state), setup_real,
     "step\n"
     "snap open\n"
     "step 28\n"
     "snap start\n"
     "type Pilot 7\n"
     "snap typed\n"
     "click registration.10\n"
     "click registration.10\n"
     "snap veteran\n",
     real_expect, N(real_expect), NULL},
};

ES2_SCREEN_SUITE(registration, cases)
