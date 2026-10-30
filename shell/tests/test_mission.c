/* The mission screen (es2/shell/gui/mission.h) on synthetic resources
 * (main_menu_fixture.h's font, seeded images, placeholder ESTEXT "T<id>",
 * generated texts) with a synthetic flow host, the movie queue and the
 * service bay for the open 0x43aee7 and the dropship's palette: the build,
 * the three pages of 0x444873, the leave 0x445095, the text panels 0x445343
 * and their pages, the buttons and their frames, Rock & Roll through the
 * flow, the inert Telecomm panel, the map hooks, the statistics and their
 * visibility rule (vshell_ui.md 14.2, a hypothesis), the theatre step, the
 * refusals. */
#include "es2/shell/gui/mission.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "main_menu_fixture.h"

static unsigned checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static uint8_t font_bytes[16384];
static es2_dfn font;

/* main_menu_fixture.h's font with 8 x 10 glyphs: a panel line of the
 * 4-pixel font would exceed setText's 100 bytes (565 / 4 columns). */
static int wide_font(uint8_t *bytes, es2_dfn *out) {
    enum { W = 8, H = 10 };
    size_t at = 8, data_size = (size_t)MMF_COUNT * W * H, data_at;
    at = mmf_put16(bytes, at, MMF_COUNT);
    at = mmf_put16(bytes, at, 0);
    at = mmf_put16(bytes, at, MMF_FIRST);
    at = mmf_put16(bytes, at, H);
    at = mmf_put16(bytes, at, W);
    at = mmf_put16(bytes, at, H);
    at = mmf_put16(bytes, at, 0);
    at = mmf_put16(bytes, at, 8);
    at = mmf_put16(bytes, at, 0);
    at = mmf_put16(bytes, at, H - 1);
    at = mmf_put16(bytes, at, 0);
    at = mmf_put32(bytes, at, (uint32_t)data_size);
    data_at = at;
    for (int g = 0; g < MMF_COUNT; g++)
        for (int k = 0; k < W * H; k++)
            bytes[data_at + (size_t)g * W * H + (size_t)k] = (uint8_t)(((unsigned)(g * 31 + k * 7) % 5) < 2 ? 41 : 0);
    at = data_at + data_size;
    for (int g = 0; g < MMF_COUNT; g++) at = mmf_put32(bytes, at, (uint32_t)(g * W * H));
    bytes[0] = 5;
    bytes[1] = 0;
    bytes[2] = 0x28;
    bytes[3] = 0;
    mmf_put32(bytes, 4, (uint32_t)(at - 8));
    if ((at - 8) & 1) bytes[at++] = 0;
    return es2_dfn_parse((es2_span){bytes, at}, out, NULL) == ES2_OK ? 0 : -1;
}
static uint8_t pixels[8][64 * 64];
static es2_bitmap images[8];
static es2_gui_shell *shell;
static es2_ui *ui;
static es2_shell_flow flow;
static es2_shell_movies movies;
static es2_service_bay bay;
static es2_mission_screen ms;

typedef struct host_log {
    int16_t group;
    int null_campaign, null_report, long_name;
    int launch_answer, warning;
    int maps, audio, changed, palettes;
    uint32_t map_handler, audio_address[4], changed_handler;
    uint16_t palette;
    char campaign[4096], report[5][4096];
} host_log;
static host_log hl;

static const es2_dfn *h_font(void *c, uint32_t g) { (void)c; return g == 0x46de00 || g == 0x46ddfc || g == 0x46de04 ? &font : NULL; }
static const es2_bitmap *h_bitmap(void *c, const char *file, int frame) {
    static const char *const names[7] = {"dba\\miss_arw.dba", "dba\\terradef.dba", "dba\\th_earth.dba", "dba\\th_moon.dba",
                                         "dba\\alph2.dba", "dba\\delt1.dba", "dba\\brav1.dba"};
    (void)c;
    for (int k = 0; k < 7; k++)
        if (!strcmp(file, names[k])) return k == 0 ? &images[7] : frame == 0 ? &images[k] : NULL;
    return NULL;
}
static const char *h_text(void *c, uint32_t id) {
    static char t[64][120];
    static unsigned n;
    char *s = t[n++ & 63];
    (void)c;
    if (hl.long_name && id == 0x78) {
        memset(s, 'N', 99);
        s[99] = 0;
        return s;
    }
    snprintf(s, sizeof t[0], "T%X", (unsigned)id);
    return s;
}
static int16_t h_group(void *c) { (void)c; return hl.group; }
static const char *h_campaign(void *c, int16_t g) { (void)c; (void)g; return hl.null_campaign ? NULL : hl.campaign; }
static const char *h_report(void *c, int panel) {
    (void)c;
    return hl.null_report || panel < 1 || panel > 4 ? NULL : hl.report[panel];
}
static void h_words(void *c, int16_t *b, int16_t *d) { (void)c; *b = 7; *d = 9; }
static void h_map(void *c, uint32_t h) { (void)c; hl.maps++; hl.map_handler = h; }
static void h_audio(void *c, uint32_t a) { (void)c; if (hl.audio < 4) hl.audio_address[hl.audio] = a; hl.audio++; }
static void h_changed(void *c, uint32_t h, es2_shell_flow_result r) { (void)c; (void)r; hl.changed++; hl.changed_handler = h; }
static void h_palette(es2_ui *u, uint16_t word) { (void)u; hl.palettes++; hl.palette = word; }
static int f_launch(void *c, int *warning) {
    (void)c;
    if (hl.launch_answer) return 1;
    *warning = hl.warning;
    return 0;
}

static void words(char *out, size_t size, const char *stem, int n) {
    size_t at = 0;
    out[0] = 0;
    for (int k = 0; k < n && at + 16 < size; k++) at += (size_t)snprintf(out + at, size - at, "%s%03d ", stem, k);
}

static void fresh(int16_t group) {
    es2_shell_flow_host fh;
    if (ui) {
        es2_mission_screen_release(&ms);
        es2_ui_release(ui);
    }
    free(ui);
    free(shell);
    memset(&hl, 0, sizeof hl);
    hl.group = group;
    words(hl.campaign, sizeof hl.campaign, "camp", 300); /* three pages */
    words(hl.report[1], sizeof hl.report[1], "brief", 400);
    words(hl.report[2], sizeof hl.report[2], "obj", 30);
    words(hl.report[3], sizeof hl.report[3], "intel", 50);
    words(hl.report[4], sizeof hl.report[4], "debr", 20);
    shell = calloc(1, sizeof *shell);
    ui = calloc(1, sizeof *ui);
    es2_gui_shell_init(shell, NULL, NULL, NULL);
    CHECK(es2_ui_init(ui, shell) == 0);
    es2_ui_install(ui, &shell->hooks);
    ui->palette = h_palette;
    memset(&fh, 0, sizeof fh);
    fh.launch = f_launch;
    es2_shell_flow_init(&flow, &fh, NULL);
    flow.mode = 1;
    flow.game = ES2_FLOW_GAME_CAMPAIGN;
    flow.final_word = 2;
    flow.screen = ES2_FLOW_SERVICE_BAY;
    es2_shell_movies_init(&movies, 1);
    memset(&bay, 0, sizeof bay);
    bay.flow = &flow;
    bay.host.font = h_font;
    CHECK(es2_screen_service_bay_build(ui, &bay) == 0);
    memset(&ms, 0, sizeof ms);
    ms.flow = &flow;
    ms.movies = &movies;
    ms.bay = &bay;
    ms.host = (es2_mission_screen_host){NULL, h_font, h_bitmap, h_text, h_group, h_campaign, h_report, h_words,
                                        h_map, h_audio, h_changed};
    const es2_ui_screen_entry *e = es2_ui_screen_find(ES2_UI_SCREEN_MISSION);
    CHECK(e && e->build(ui, &ms) == 0);
    es2_ui_show(ui, ui->root);
}
static void step(void) {
    ui->now += 100;
    es2_gui_step(shell);
}
static void steps(int n) { while (n-- > 0) step(); }
static es2_ui_widget *W(int control) { return es2_ui_get(ui, ms.ids[control]); }
static int visible(es2_ui_id id) {
    es2_ui_widget *w = es2_ui_get(ui, id);
    return w && !(w->flags & ES2_UI_HIDDEN);
}
static int shown(int control) { return visible(ms.ids[control]); }
static int pending(int control) { return W(control) && (W(control)->flags & ES2_UI_PENDING) != 0; }
static int panel_shown(int p) { return visible(ms.panels[p].window); }
static const char *label_of(int control) {
    es2_ui_widget *l = es2_ui_get(ui, W(control)->label);
    return l ? l->text : "";
}
static int label_colour(int control) {
    es2_ui_widget *l = es2_ui_get(ui, W(control)->label);
    return l ? l->text_colour : -1;
}
static void click_control(int control) {
    es2_ui_widget *w = W(control);
    int x = (w->abs.left + w->abs.right) / 2, y = (w->abs.top + w->abs.bottom) / 2;
    es2_gui_on_mouse(shell, ES2_GUI_WM_MOUSEMOVE, x, y, 0, 0);
    step();
    es2_gui_on_mouse(shell, ES2_GUI_WM_LBUTTONDOWN, x, y, 1, 0);
    step();
    es2_gui_on_mouse(shell, ES2_GUI_WM_LBUTTONUP, x, y, 0, 0);
    step();
}
static int queued(int16_t index) {
    for (int k = 0; k < ES2_SHELL_MOVIE_RING; k++)
        if (movies.ring[k].index == index) return k + 1;
    return 0;
}
static int queue_count(void) {
    int n = 0;
    for (int k = 0; k < ES2_SHELL_MOVIE_RING; k++) n += movies.ring[k].index != -1;
    return n;
}

static void test_build(void) {
    fresh(2);
    CHECK(es2_mission_statistic(0x48d918) == ES2_MISSION_STATISTIC);
    CHECK(es2_mission_statistic(0x48d91c) == ES2_MISSION_STATISTIC + 3);
    CHECK(es2_mission_statistic(0x48d964) == ES2_MISSION_STATISTIC + 19);
    CHECK(es2_mission_statistic(0x48d968) == -1 && es2_mission_statistic(0x48d91a) == -1);
    for (int k = 0; k < ES2_SCREEN_MISSION_CONTROLS; k++) CHECK(ms.ids[k] != 0);
    CHECK(ms.panel == -1 && ms.state == 0 && ms.map_pending == 0 && ms.map_palette == -1);
    CHECK(W(ES2_MISSION_TELECOMM)->original_handler == ES2_MISSION_TELECOMM_HANDLER);
    CHECK(W(ES2_MISSION_ROCK_AND_ROLL)->original_handler == ES2_FLOW_ROCK_AND_ROLL);
    CHECK(!strcmp(label_of(ES2_MISSION_BRIEFING), "TB4") && !strcmp(label_of(ES2_MISSION_ROCK_AND_ROLL), "TB8"));
    CHECK(!strcmp(label_of(ES2_MISSION_MAP_FRAME), "TB0") && !strcmp(label_of(ES2_MISSION_TEXT_FRAME), "TAF"));
    /* every top window hidden, the statistics pending under [0x48d8b8] */
    CHECK(!shown(ES2_MISSION_TITLE) && !shown(ES2_MISSION_MAP_FRAME) && !shown(ES2_MISSION_TEXT_FRAME));
    for (int k = 0; k < ES2_MISSION_STATISTICS; k++) CHECK(pending(ES2_MISSION_STATISTIC + k));
    CHECK(!strcmp(W(ES2_MISSION_STATISTIC)->text, "T145") && !strcmp(W(ES2_MISSION_STATISTIC + 9)->text, "T152"));
    /* the five panels: children of [0x48d8bc], hidden, empty; rectangles */
    for (int p = 0; p < ES2_MISSION_PANELS; p++) {
        es2_ui_widget *w = es2_ui_get(ui, ms.panels[p].window);
        CHECK(w && w->parent == ms.ids[ES2_MISSION_TEXT_FRAME] && (w->flags & ES2_UI_HIDDEN) && !w->report);
        CHECK(w->rect.left == 10 && w->rect.top == 21 && w->rect.right == (p ? 575 : 613) && w->rect.bottom == (p ? 115 : 168));
    }
    /* the arrows: miss_arw frames, " " labels */
    CHECK(W(ES2_MISSION_ARROW)->bitmap_normal == &images[7] && !strcmp(label_of(ES2_MISSION_ARROW), " "));
    CHECK(es2_mission_screen_press(&ms, 0x12345) == ES2_FLOW_IGNORED);
}

static void test_page0(void) {
    es2_ui_widget *frame;
    fresh(2);
    flow.page = 0;
    es2_mission_screen_show(&ms, 0);
    steps(2);
    /* title "%s %s": ESTEXT 0x77 + group - 1, then 0x7c */
    CHECK(!strcmp(label_of(ES2_MISSION_TITLE), "T78 T7C"));
    frame = W(ES2_MISSION_TEXT_FRAME);
    CHECK(frame->abs.top == 307 && frame->abs.bottom == 476 && frame->rect.bottom == 476);
    CHECK(es2_ui_get(ui, ms.panels[0].window)->abs.top == 307 + 21);
    CHECK(shown(ES2_MISSION_TITLE) && shown(ES2_MISSION_MAP_FRAME) && shown(ES2_MISSION_TELECOMM) &&
          shown(ES2_MISSION_TEXT_FRAME) && shown(ES2_MISSION_THEATRE_IMAGE));
    CHECK(!shown(ES2_MISSION_PAGE_PREVIOUS) && !shown(ES2_MISSION_PAGE_NEXT) && !shown(ES2_MISSION_BUTTON_ROW));
    for (int k = 0; k < 6; k++) CHECK(!shown(ES2_MISSION_ARROW + 2 * k));
    CHECK(panel_shown(0) && !panel_shown(1) && !panel_shown(4) && ms.panel == 0 && ms.panels[0].visible == 1);
    CHECK(ms.panels[0].has_text && ms.panels[0].report.lines > 1);
    CHECK(W(ES2_MISSION_TELECOMM)->bitmap == &images[1] && ms.terradef == &images[1]);
    CHECK(ui->layers[W(ES2_MISSION_THEATRE_IMAGE)->grid][0].bitmap == &images[2]);
    CHECK(ui->layers[W(ES2_MISSION_THEATRE_IMAGE)->grid][0].from[9] == 0x0e &&
          ui->layers[W(ES2_MISSION_THEATRE_IMAGE)->grid][0].to[0] == 0x10);
    CHECK(ms.state == 1 && ms.map_pending == 0);
    /* the statistics hidden for good (0x444fa4) */
    for (int k = 0; k < ES2_MISSION_STATISTICS; k++) CHECK(!shown(ES2_MISSION_STATISTIC + k) && !pending(ES2_MISSION_STATISTIC + k));
    /* movies: c2 (0x47, palette 3) and the theatre movie (0x4c, sequence 1) */
    CHECK(queued(0x47) && queued(0x4c) && movies.group_shown == 1 && queue_count() == 2);
    CHECK(movies.ring[queued(0x47) - 1].palette == 3 && movies.ring[queued(0x4c) - 1].sequence == 1);
    CHECK(hl.changed_handler == ES2_MISSION_SHOW);
    /* the page buttons do nothing for the campaign panel */
    CHECK(ms.panels[0].report.pages >= 3);
    CHECK(es2_mission_screen_press(&ms, ES2_MISSION_NEXT_HANDLER) == ES2_FLOW_DONE && ms.panels[0].page == 0);
    es2_mission_screen_page(&ms, 0, 1); /* the routine itself would turn it */
    CHECK(ms.panels[0].page == 1);
    es2_mission_screen_page(&ms, 0, 0);
    /* a second page-0 visit: no movies; the leave */
    es2_mission_screen_show(&ms, 0);
    CHECK(queue_count() == 2);
    es2_mission_screen_leave(&ms);
    steps(2);
    CHECK(!shown(ES2_MISSION_TITLE) && !shown(ES2_MISSION_TEXT_FRAME) && !shown(ES2_MISSION_THEATRE_IMAGE) && ms.state == 0);
    CHECK(ui->layers[W(ES2_MISSION_THEATRE_IMAGE)->grid][0].bitmap == NULL && !ms.terradef && !ms.theatre_image);
    CHECK(ms.panels[0].has_text && ms.panels[0].visible == 1); /* the leave keeps [0x48d8fc] */
    CHECK(W(ES2_MISSION_TELECOMM)->bitmap == &images[1]);     /* the stale pointer +0x5d stays */
    CHECK(hl.changed_handler == ES2_MISSION_LEAVE);
    /* theatre 5: th_moon; group 5's movies with palette 4 */
    fresh(5);
    es2_mission_screen_show(&ms, 0);
    CHECK(ui->layers[W(ES2_MISSION_THEATRE_IMAGE)->grid][0].bitmap == &images[3]);
    CHECK(queued(0x4a) && movies.ring[queued(0x4a) - 1].palette == 4 && queued(0x4f));
    CHECK(!strcmp(label_of(ES2_MISSION_TITLE), "T7B T7C"));
    /* movies off: nothing queued, the flag still set */
    fresh(1);
    movies.enabled = 0;
    es2_mission_screen_show(&ms, 0);
    CHECK(queue_count() == 0 && movies.group_shown == 1);
    /* refusals: no campaign text (VSHELL faults), a title of 100 bytes and more */
    fresh(1);
    hl.null_campaign = 1;
    hl.long_name = 0;
    es2_mission_screen_show(&ms, 0);
    CHECK(ms.refused == 1 && !ms.panels[0].has_text && !panel_shown(0) && ms.panel == 0);
    fresh(2);
    hl.long_name = 1;
    es2_mission_screen_show(&ms, 0);
    CHECK(ms.refused == 1 && !strcmp(label_of(ES2_MISSION_TITLE), " "));
}

static void test_page1(void) {
    fresh(3);
    flow.page = 1;
    es2_mission_screen_show(&ms, 1);
    steps(2);
    CHECK(!strcmp(label_of(ES2_MISSION_TITLE), "TB3"));
    CHECK(W(ES2_MISSION_TEXT_FRAME)->abs.bottom == 423);
    CHECK(shown(ES2_MISSION_BUTTON_ROW) && shown(ES2_MISSION_BRIEFING) && shown(ES2_MISSION_ROCK_AND_ROLL) &&
          shown(ES2_MISSION_PAGE_PREVIOUS) && shown(ES2_MISSION_PAGE_NEXT) && !shown(ES2_MISSION_THEATRE_IMAGE));
    for (int k = 0; k < 6; k++) CHECK(shown(ES2_MISSION_ARROW + 2 * k));
    for (int c = ES2_MISSION_OBJECTIVES; c <= ES2_MISSION_ROCK_AND_ROLL; c += 2)
        CHECK(W(c)->enabled == 1 && label_colour(c) == 0x29);
    CHECK(W(ES2_MISSION_BRIEFING)->colour == 0x20 && W(ES2_MISSION_OBJECTIVES)->colour == 0x22 &&
          W(ES2_MISSION_INTELLIGENCE)->colour == 0x22 && W(ES2_MISSION_ROCK_AND_ROLL)->colour == 0x22);
    CHECK(ms.panel == 1 && panel_shown(1) && !panel_shown(2) && !panel_shown(3));
    CHECK(ms.panels[1].has_text && ms.panels[2].has_text && ms.panels[3].has_text && !ms.panels[4].has_text);
    CHECK(ms.map_pending == 1 && ms.map_palette == 2 + 5 && ms.state == 2);
    CHECK(queued(7) && movies.ring[queued(7) - 1].palette == 7 && movies.briefing_shown == 1 && queue_count() == 1);
    /* Objectives, Intelligence, Briefing (the page's panel) */
    click_control(ES2_MISSION_OBJECTIVES);
    CHECK(ms.panel == 2 && panel_shown(2) && !panel_shown(1) && W(ES2_MISSION_OBJECTIVES)->colour == 0x20 &&
          W(ES2_MISSION_BRIEFING)->colour == 0x22);
    click_control(ES2_MISSION_INTELLIGENCE);
    CHECK(ms.panel == 3 && panel_shown(3) && !panel_shown(2) && W(ES2_MISSION_INTELLIGENCE)->colour == 0x20);
    click_control(ES2_MISSION_BRIEFING);
    CHECK(ms.panel == 1 && panel_shown(1) && W(ES2_MISSION_BRIEFING)->colour == 0x20 && W(ES2_MISSION_INTELLIGENCE)->colour == 0x22);
    /* pages of the briefing (9 rows of 70 columns: 400 words) */
    {
        int16_t pages = ms.panels[1].report.pages;
        CHECK(pages >= 3);
        click_control(ES2_MISSION_PAGE_PREVIOUS);
        CHECK(ms.panels[1].page == 0);
        /* the arrows auto-repeat (+0x61 = 1): a held click turns more than once */
        click_control(ES2_MISSION_PAGE_NEXT);
        CHECK(ms.panels[1].page >= 1 && es2_ui_get(ui, ms.panels[1].window)->page == ms.panels[1].page);
        CHECK(es2_mission_screen_press(&ms, ES2_MISSION_PREVIOUS_HANDLER) == ES2_FLOW_DONE);
        while (ms.panels[1].page > 0) (void)es2_mission_screen_press(&ms, ES2_MISSION_PREVIOUS_HANDLER);
        (void)es2_mission_screen_press(&ms, ES2_MISSION_NEXT_HANDLER);
        CHECK(ms.panels[1].page == 1 && es2_ui_get(ui, ms.panels[1].window)->page == 1);
        for (int k = 0; k < pages + 2; k++) (void)es2_mission_screen_press(&ms, ES2_MISSION_NEXT_HANDLER);
        CHECK(ms.panels[1].page == pages - 1);
        (void)es2_mission_screen_press(&ms, ES2_MISSION_PREVIOUS_HANDLER);
        CHECK(ms.panels[1].page == pages - 2);
        /* a hidden panel does not turn; the pages stay per panel */
        es2_mission_screen_page(&ms, 2, 1);
        CHECK(ms.panels[2].page == 0);
        click_control(ES2_MISSION_OBJECTIVES);
        click_control(ES2_MISSION_BRIEFING);
        CHECK(ms.panels[1].page == pages - 2);
    }
    /* Telecomm: nothing; the arrows: the mission map's hook */
    {
        int16_t panel = ms.panel;
        unsigned events = ms.events;
        click_control(ES2_MISSION_TELECOMM);
        CHECK(ms.panel == panel && ms.events == events && queue_count() == 1 && hl.maps == 0);
        CHECK(es2_mission_screen_press(&ms, ES2_MISSION_TELECOMM_HANDLER) == ES2_FLOW_IGNORED);
    }
    click_control(ES2_MISSION_ARROW + 4);
    CHECK(hl.maps == 1 && hl.map_handler == 0x445715);
    /* Rock & Roll: a warning (the bay word must be MISSION in the flow) */
    flow.bay_word = ES2_FLOW_WORD_MISSION;
    hl.warning = 2;
    click_control(ES2_MISSION_ROCK_AND_ROLL);
    CHECK(flow.box == ES2_FLOW_BOX_WARNING && flow.box_value == 2 && !flow.quit && ms.last_handler == ES2_FLOW_ROCK_AND_ROLL);
    CHECK(hl.changed_handler == ES2_FLOW_ROCK_AND_ROLL);
    (void)es2_shell_flow_event(&flow, ES2_FLOW_WARNING_OKAY, NULL);
    hl.launch_answer = 1;
    CHECK(es2_mission_screen_press(&ms, ES2_FLOW_ROCK_AND_ROLL) == ES2_FLOW_DONE && flow.quit && flow.exit_code == 2);
    /* a second page-1 visit: no briefing movie; refused texts */
    fresh(3);
    es2_mission_screen_show(&ms, 1);
    es2_mission_screen_leave(&ms);
    CHECK(!ms.panels[1].has_text && !ms.panels[2].has_text && !ms.panels[3].has_text && !panel_shown(1));
    es2_mission_screen_show(&ms, 1);
    CHECK(queue_count() == 1);
    hl.null_report = 1;
    es2_mission_screen_leave(&ms);
    es2_mission_screen_show(&ms, 1);
    CHECK(ms.refused == 3 && !panel_shown(1) && ms.panel == 1);
}

static void test_page4(void) {
    es2_shell_debrief_texts t;
    memset(&t, 0, sizeof t);
    for (int k = 0; k < ES2_SHELL_DEBRIEF_VALUES; k++) {
        static const uint32_t g[10] = {0x48d91c, 0x48d924, 0x48d92c, 0x48d94c, 0x48d958,
                                       0x48d950, 0x48d95c, 0x48d954, 0x48d960, 0x48d964};
        snprintf(t.text[k], sizeof t.text[k], "V%d", k);
        t.widget[k] = g[k];
    }
    /* the results-path entry: the statistics set before any page visit stay
     * pending and appear with page 4 */
    fresh(2);
    es2_mission_screen_statistics(&ms, &t);
    CHECK(!strcmp(W(ES2_MISSION_STATISTIC + 3)->text, "V0") && W(ES2_MISSION_STATISTIC + 3)->align == 0);
    CHECK(!strcmp(W(ES2_MISSION_STATISTIC + 13)->text, "V3") && W(ES2_MISSION_STATISTIC + 13)->align == 1);
    CHECK(!strcmp(W(ES2_MISSION_STATISTIC + 15)->text, "V5") && W(ES2_MISSION_STATISTIC + 19)->text_colour == 0x29);
    flow.page = 4;
    es2_mission_screen_show(&ms, 4);
    steps(2);
    CHECK(!strcmp(label_of(ES2_MISSION_TITLE), "TB9"));
    for (int k = 0; k < ES2_MISSION_STATISTICS; k++) CHECK(shown(ES2_MISSION_STATISTIC + k));
    CHECK(ms.panel == 4 && panel_shown(4) && ms.panels[4].has_text);
    CHECK(!shown(ES2_MISSION_BUTTON_ROW) && shown(ES2_MISSION_PAGE_NEXT) && !shown(ES2_MISSION_ARROW));
    CHECK(W(ES2_MISSION_ROCK_AND_ROLL)->enabled == 0 && W(ES2_MISSION_ROCK_AND_ROLL)->colour == 0x26 &&
          label_colour(ES2_MISSION_ROCK_AND_ROLL) == 0x26);
    CHECK(ms.map_palette == 1 + 10 && ms.map_pending == 0 && ms.state == 0);
    CHECK(queued(9) && movies.ring[queued(9) - 1].palette == 11 && movies.debrief_shown == 1 && queue_count() == 1);
    /* the Briefing handler names the page's panel: 4 (button row hidden) */
    CHECK(es2_mission_screen_press(&ms, ES2_MISSION_BRIEFING_HANDLER) == ES2_FLOW_DONE && ms.panel == 4 &&
          W(ES2_MISSION_BRIEFING)->colour == 0x20);
    /* after a leave the statistics stay hidden (vshell_ui.md 14.2, hypothesis)
     * */
    es2_mission_screen_leave(&ms);
    es2_mission_screen_show(&ms, 4);
    steps(2);
    for (int k = 0; k < ES2_MISSION_STATISTICS; k++) CHECK(!shown(ES2_MISSION_STATISTIC + k));
    CHECK(queue_count() == 1);
    /* page 1 after page 4 enables Rock & Roll again */
    es2_mission_screen_leave(&ms);
    es2_mission_screen_show(&ms, 1);
    CHECK(W(ES2_MISSION_ROCK_AND_ROLL)->enabled == 1 && W(ES2_MISSION_ROCK_AND_ROLL)->colour == 0x22);
    /* a widget address outside the twenty: refused */
    t.widget[0] = 0x48d968;
    es2_mission_screen_statistics(&ms, &t);
    CHECK(ms.refused == 1);
}

static void test_panel_rule(void) {
    fresh(2);
    /* 0x445343 with -1 and 5: no panel shown, the word stored */
    es2_mission_screen_panel(&ms, -1);
    CHECK(ms.panel == -1);
    CHECK(es2_mission_screen_set_text(&ms, 3, "a b c") == 0 && !ms.panels[3].visible && !panel_shown(3));
    es2_mission_screen_panel(&ms, 3);
    CHECK(ms.panels[3].visible == 1 && ms.panel == 3);
    es2_mission_screen_panel(&ms, 5);
    CHECK(ms.panels[3].visible == 0 && ms.panel == 5);
    /* setText resets visible and page */
    es2_mission_screen_panel(&ms, 3);
    CHECK(es2_mission_screen_set_text(&ms, 3, "x y") == 0 && ms.panels[3].visible == 0 && ms.panels[3].page == 0);
    CHECK(es2_mission_screen_set_text(&ms, 3, NULL) == -1 && es2_mission_screen_set_text(&ms, 7, "x") == -1);
    /* a word wider than the panel: the breaker's assertion */
    {
        static char wide[400];
        memset(wide, 'W', 300);
        wide[300] = 0;
        CHECK(es2_mission_screen_set_text(&ms, 1, wide) == -1 && !ms.panels[1].has_text);
    }
    /* frames 0x4452d9 */
    es2_mission_screen_buttons(&ms, 3);
    CHECK(W(ES2_MISSION_INTELLIGENCE)->colour == 0x20 && W(ES2_MISSION_BRIEFING)->colour == 0x22);
    es2_mission_screen_buttons(&ms, 0);
    CHECK(W(ES2_MISSION_INTELLIGENCE)->colour == 0x22 && W(ES2_MISSION_BRIEFING)->colour == 0x22 &&
          W(ES2_MISSION_OBJECTIVES)->colour == 0x22);
}

static void test_open_and_theatre(void) {
    fresh(2);
    es2_service_bay_show(&bay);
    hl.palettes = 0;
    CHECK(es2_mission_screen_open(&ms, 4, 1) == 0);
    CHECK(flow.page == 4 && bay.session.screen == 7 && flow.bay_word == ES2_FLOW_WORD_MISSION);
    CHECK(ms.panel == 4 && shown(ES2_MISSION_TITLE) && !strcmp(label_of(ES2_MISSION_TITLE), "TB9"));
    {   /* the synthetic press and release on MISSION */
        int n = 0;
        for (unsigned i = 0; i < shell->input.queue_count; i++) {
            const es2_gui_event *ev = &shell->input.queue[(shell->input.queue_head + i) % ES2_GUI_QUEUE_MAX];
            n += ev->synthetic && ev->target == bay.bay[ES2_BAY_MISSION];
        }
        CHECK(n == 2);
    }
    steps(2);
    CHECK(hl.palettes >= 1 && hl.palette == 11); /* 0x43b7f2(7): db_w2, applied by the ESPal's hide event */
    CHECK(bay.session.screen == 7 && flow.page == 4);
    /* refused with an editor's word or without the bay */
    bay.session.screen = 2;
    CHECK(es2_mission_screen_open(&ms, 4, 0) == -1 && ms.refused == 1);
    ms.bay = NULL;
    CHECK(es2_mission_screen_open(&ms, 4, 0) == -1);
    ms.bay = &bay;
    /* theatre step: group 2 -> delt1, palette 0x10 */
    hl.palettes = 0;
    CHECK(es2_mission_screen_theatre(&ms) == 0 && shown(ES2_MISSION_THEATRE) && W(ES2_MISSION_THEATRE)->bitmap == &images[5]);
    CHECK(hl.palette == 0x10 && ms.picture == &images[5]);
    es2_mission_screen_theatre_hide(&ms);
    CHECK(!shown(ES2_MISSION_THEATRE) && !ms.picture);
    /* group 4 -> brav1 */
    hl.group = 4;
    CHECK(es2_mission_screen_theatre(&ms) == 0 && W(ES2_MISSION_THEATRE)->bitmap == &images[6] && hl.palette == 0x12);
    /* group 5: the dropship, palette 2 through the bay, fade-out, focus lost */
    hl.group = 5;
    es2_shell_movies_init(&movies, 1);
    CHECK(es2_mission_screen_theatre(&ms) == 1 && queued(0x55) && hl.audio == 2);
    steps(1);
    CHECK(hl.audio_address[0] == ES2_MISSION_MUSIC_FADE_OUT && hl.audio_address[1] == ES2_MISSION_FOCUS_LOST && hl.palette == 2);
    /* group 0 and 6: refused (outside the table) */
    hl.group = 0;
    CHECK(es2_mission_screen_theatre(&ms) == -1);
    hl.group = 6;
    CHECK(es2_mission_screen_theatre(&ms) == -1);
}

int main(void) {
    if (wide_font(font_bytes, &font)) {
        printf("FAIL font\n");
        return 1;
    }
    for (int k = 0; k < 8; k++) {
        for (int i = 0; i < 64 * 64; i++) pixels[k][i] = (uint8_t)(16 + k * 20 + i % 13);
        images[k] = (es2_bitmap){k == 7 ? 30 : 64, k == 7 ? 30 : 64, 0, pixels[k]};
    }
    test_build();
    test_page0();
    test_page1();
    test_page4();
    test_panel_rule();
    test_open_and_theatre();
    if (ui) {
        es2_mission_screen_release(&ms);
        es2_ui_release(ui);
    }
    free(ui);
    free(shell);
    printf("mission_screen_test: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
