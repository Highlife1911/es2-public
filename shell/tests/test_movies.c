/* Unit tests of the movie queue, selection rules and player control flow
 * (es2/shell/movies.h) on synthetic states; no original data, no
 * window, nothing played. */
#include "es2/shell/movies.h"
#include <stdio.h>
#include <string.h>

static unsigned checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static int count(const es2_shell_movies *m) {
    int n = 0;
    for (int i = 0; i < ES2_SHELL_MOVIE_RING; i++) n += m->ring[i].index != -1;
    return n;
}

static void names(void) {
    char path[64];
    CHECK(es2_shell_movie_name(-1) == NULL && es2_shell_movie_name(86) == NULL);
    CHECK(!strcmp(es2_shell_movie_name(0), "avi\\pt1.avi"));
    CHECK(!strcmp(es2_shell_movie_name(ES2_SHELL_MOVIE_GROUP), "avi\\c1.avi"));
    CHECK(!strcmp(es2_shell_movie_name(ES2_SHELL_MOVIE_THEATRE + 4), "avi\\luna.avi"));
    CHECK(!strcmp(es2_shell_movie_name(ES2_SHELL_MOVIE_DROPSHIP), "avi\\dropship.avi"));
    CHECK(!strcmp(es2_shell_movie_name(ES2_SHELL_MOVIE_CREDITS), "avi\\credits.avi"));
    for (int i = 0; i < ES2_SHELL_MOVIE_NAMES; i++) {
        const char *n = es2_shell_movie_name(i);
        CHECK(n && !strncmp(n, "avi\\", 4) && strlen(n) > 8 && !strcmp(n + strlen(n) - 4, ".avi"));
    }
    CHECK(es2_shell_movie_path(ES2_SHELL_MOVIE_INTRO_1, 0, path, sizeof path) == 0 && !strcmp(path, "avi\\intr_pt1.avi"));
    CHECK(es2_shell_movie_path(ES2_SHELL_MOVIE_INTRO_1, 1, path, sizeof path) == 0 && !strcmp(path, "avf\\intr_pt1.avi"));
    CHECK(es2_shell_movie_path(ES2_SHELL_MOVIE_INTRO_2, 2, path, sizeof path) == 0 && !strcmp(path, "avg\\intr_pt2.avi"));
    CHECK(es2_shell_movie_path(ES2_SHELL_MOVIE_INTRO_2, 7, path, sizeof path) == 0 && !strcmp(path, "avi\\intr_pt2.avi"));
    CHECK(es2_shell_movie_path(ES2_SHELL_MOVIE_GROUP, 2, path, sizeof path) == 0 && !strcmp(path, "avi\\c1.avi"));
    CHECK(es2_shell_movie_path(86, 0, path, sizeof path) == -1);
    CHECK(es2_shell_movie_path(-1, 0, path, sizeof path) == -1);
    CHECK(es2_shell_movie_path(0, 0, path, 11) == -1);   /* "avi\pt1.avi" needs 12 */
    CHECK(es2_shell_movie_path(0, 0, path, 12) == 0);
    CHECK(es2_shell_movie_path(0, 0, NULL, 12) == -1);
}

static void queue(void) {
    es2_shell_movies m;
    uint8_t image[280];
    es2_shell_movie_rect r = {1, 2, 3, 4};
    es2_shell_movies_init(&m, 1);
    CHECK(m.enabled == 1 && m.write == 0 && m.play == 0 && count(&m) == 0);
    CHECK(m.ring[3].palette == -1 && m.ring[3].rect.x == 0 && m.ring[3].sequence == 0);
    /* -1 is "queued" while a record is empty: never stored */
    CHECK(es2_shell_movie_queued(&m, -1) && !es2_shell_movie_enqueue(&m, -1, r, -1, 0, 0));
    CHECK(es2_shell_movie_queued(&m, 0xffff));   /* low word compare */
    CHECK(es2_shell_movie_enqueue(&m, 0x10005, r, 0x20007, 9, 1) == 1);
    CHECK(m.ring[0].index == 5 && m.ring[0].palette == 7 && m.ring[0].callback == 9 && m.ring[0].sequence == 1);
    CHECK(m.ring[0].rect.width == 3 && m.write == 1);
    CHECK(!es2_shell_movie_enqueue(&m, 5, r, -1, 0, 0));        /* already queued */
    CHECK(!es2_shell_movie_enqueue(&m, 0x70005, r, -1, 0, 0));  /* same low word */
    es2_shell_movies_image(&m, image);
    CHECK(image[0] == 5 && image[1] == 0 && image[2] == 1 && image[6] == 2 && image[10] == 3 && image[14] == 4);
    CHECK(image[18] == 7 && image[20] == 1 && image[24] == 9 && image[28] == 0xff && image[29] == 0xff);
    /* flag 0: nothing */
    m.enabled = 0;
    CHECK(!es2_shell_movie_enqueue(&m, 6, r, -1, 0, 0) && m.write == 1);
    m.enabled = 1;
    /* fill: nine more; the ring is then full and -1 can be stored */
    for (int i = 0; i < 9; i++) CHECK(es2_shell_movie_enqueue(&m, 10 + i, r, -1, 0, 0) == 1);
    CHECK(m.write == 0 && count(&m) == 10 && !es2_shell_movie_queued(&m, -1));
    /* no full-ring check: the oldest record is overwritten */
    CHECK(es2_shell_movie_enqueue(&m, 30, r, -1, 0, 0) == 1 && m.ring[0].index == 30 && m.write == 1);
    CHECK(es2_shell_movie_enqueue(&m, -1, r, -1, 0, 0) == 1 && m.ring[1].index == -1);
}

static void selection(void) {
    es2_shell_movies m;
    es2_shell_movies_init(&m, 1);
    CHECK(es2_shell_movies_intro(&m, 0) == 1 && m.ring[0].index == 0x44 && m.ring[1].index == 0x45);
    CHECK(m.ring[0].rect.x == 0x20 && m.ring[0].rect.y == 0x3c && m.ring[0].rect.width == 0x240 && m.ring[0].rect.height == 0x168);
    CHECK(m.ring[1].palette == -1 && m.ring[1].sequence == 0);
    for (int x = -2; x < 9; x++) {
        es2_shell_movies n;
        es2_shell_movies_init(&n, 1);
        CHECK(es2_shell_movies_intro(&n, (int16_t)x) == (x != 3 && x != 4 && x != 6));
        CHECK(count(&n) == (x != 3 && x != 4 && x != 6 ? 2 : 0));
    }
    es2_shell_movies_init(&m, 0);
    CHECK(es2_shell_movies_intro(&m, 1) == 1 && count(&m) == 0);   /* -a: due, nothing queued */
    es2_shell_movies_init(&m, 1);
    es2_shell_movies_credits(&m);
    CHECK(count(&m) == 1 && m.ring[0].index == 0x54);
    es2_shell_movies_init(&m, 1);
    es2_shell_movies_victory(&m);
    CHECK(count(&m) == 2 && m.ring[0].index == 0x53 && m.ring[1].index == 0x54);

    /* MISSION page rule */
    es2_shell_movies_init(&m, 1);
    CHECK(es2_shell_movies_mission_page(&m, 0) == 0 && es2_shell_movies_mission_page(&m, 1) == 1);
    m.group_shown = 1;
    CHECK(es2_shell_movies_mission_page(&m, 0) == 1);

    /* page 0: group and theatre movie, once */
    for (int g = 1; g <= 5; g++) {
        es2_shell_movies_init(&m, 1);
        es2_shell_movies_mission(&m, 0, (int16_t)g, 0x10, 0x20);
        CHECK(count(&m) == 2 && m.group_shown && !m.briefing_shown && !m.debrief_shown);
        CHECK(m.ring[0].index == 0x46 + g - 1 && m.ring[0].palette == (g >= 5 ? 4 : 3) && m.ring[0].sequence == 0);
        CHECK(m.ring[0].rect.x == 0x15 && m.ring[0].rect.y == 0x56 && m.ring[0].rect.width == 0xef && m.ring[0].rect.height == 0xb3);
        CHECK(m.ring[1].index == 0x4b + g - 1 && m.ring[1].palette == -1 && m.ring[1].sequence == 1);
        CHECK(m.ring[1].rect.x == 0x122 && m.ring[1].rect.y == 0x43 && m.ring[1].rect.width == 0x127 && m.ring[1].rect.height == 0xe2);
        es2_shell_movies_mission(&m, 0, (int16_t)g, 0x10, 0x20);
        CHECK(count(&m) == 2);
    }
    /* group 0 and 7: indices below and past the named block (the table is not checked) */
    es2_shell_movies_init(&m, 1);
    es2_shell_movies_mission(&m, 0, 0, 0, 0);
    CHECK(m.ring[0].index == 0x45 && m.ring[0].palette == 3 && m.ring[1].index == 0x4a);
    es2_shell_movies_init(&m, 1);
    es2_shell_movies_mission(&m, 0, 7, 0, 0);
    CHECK(m.ring[0].index == 0x4c && m.ring[0].palette == 4 && m.ring[1].index == 0x51);
    /* page 1: briefing word with palette group - 1 + 5, once per flag; -1 never queued */
    es2_shell_movies_init(&m, 1);
    es2_shell_movies_mission(&m, 1, 3, 0x30, 0x31);
    CHECK(count(&m) == 1 && m.ring[0].index == 0x30 && m.ring[0].palette == 7 && m.briefing_shown && !m.group_shown);
    es2_shell_movies_mission(&m, 1, 3, 0x32, 0x31);
    CHECK(count(&m) == 1);
    es2_shell_movies_init(&m, 1);
    es2_shell_movies_mission(&m, 1, 3, -1, 0x31);
    CHECK(count(&m) == 0 && m.briefing_shown);   /* the flag is set anyway */
    /* page 4: debrief word with palette group - 1 + 10 */
    es2_shell_movies_init(&m, 1);
    es2_shell_movies_mission(&m, 4, 2, 0x30, 0x31);
    CHECK(count(&m) == 1 && m.ring[0].index == 0x31 && m.ring[0].palette == 11 && m.debrief_shown);
    /* the same movie as a queued one is skipped */
    es2_shell_movies_init(&m, 1);
    es2_shell_movies_mission(&m, 1, 2, 0x31, 0);
    es2_shell_movies_mission(&m, 4, 2, 0x30, 0x31);
    CHECK(count(&m) == 1 && m.debrief_shown);
    /* other pages: nothing */
    es2_shell_movies_init(&m, 1);
    es2_shell_movies_mission(&m, 2, 2, 0x30, 0x31);
    es2_shell_movies_mission(&m, 3, 2, 0x30, 0x31);
    CHECK(count(&m) == 0 && !m.group_shown && !m.briefing_shown && !m.debrief_shown);

    /* theatre step: only group 5 queues the dropship */
    for (int g = -1; g <= 7; g++) {
        es2_shell_movies_init(&m, 1);
        CHECK(es2_shell_movies_theatre(&m, (int16_t)g) == (g == 5));
        CHECK(count(&m) == (g == 5) && (g != 5 || m.ring[0].index == 0x55));
    }
    /* once-flag writers */
    es2_shell_movies_init(&m, 1);
    m.group_shown = m.briefing_shown = m.debrief_shown = 1;
    es2_shell_movies_load(&m, 0);
    CHECK(m.group_shown && !m.briefing_shown && !m.debrief_shown);
    m.briefing_shown = m.debrief_shown = 1;
    es2_shell_movies_load(&m, 1);
    CHECK(!m.group_shown && !m.briefing_shown && !m.debrief_shown);
    m.group_shown = m.briefing_shown = m.debrief_shown = 1;
    es2_shell_movies_new_career(&m);
    CHECK(!m.group_shown && !m.briefing_shown && !m.debrief_shown);
}

/* ---- player ---------------------------------------------------------------- */
typedef struct trace {
    uint32_t action[512];
    int32_t arg[512];
    int16_t played[32];
    int n, plays;
    int bar, panel, fullscreen, page, quit, focus_after;
    int open_result, play_result[8], prompt_result;
    es2_shell_movies *m;
    int16_t group;
    int gate_drains, gate_open_drains, gate_prompt; /* the bound gate seen by DRAIN and the CD prompt */
} trace;
static int32_t t_call(void *c, uint32_t action, int32_t arg, const es2_shell_movie_record *r) {
    trace *t = c;
    if (t->n < 512) { t->action[t->n] = action; t->arg[t->n] = arg; t->n++; }
    if (t->m && t->m->gate && action == ES2_SHELL_MOVIE_DRAIN) {
        t->gate_drains++;
        if (*t->m->gate != 1) t->gate_open_drains++;
    }
    if (t->m && t->m->gate && action == ES2_SHELL_MOVIE_CD_PROMPT) t->gate_prompt = *t->m->gate;
    switch (action) {
    case ES2_SHELL_MOVIE_OPEN_DEVICE: return t->open_result;
    case ES2_SHELL_MOVIE_PLAY: {
        int32_t result = t->play_result[t->plays < 8 ? t->plays : 7];
        if (t->plays < 32) t->played[t->plays] = r->index;
        t->plays++;
        return result;
    }
    case ES2_SHELL_MOVIE_CD_PROMPT:
        if (!t->prompt_result) t->quit = 1;
        return t->prompt_result;
    case ES2_SHELL_MOVIE_INTRO_ERROR: t->quit = 1; return 0;
    case ES2_SHELL_MOVIE_THEATRE_STEP: es2_shell_movies_theatre(t->m, t->group); return 0;
    case ES2_SHELL_MOVIE_PUMP: if (t->focus_after > 0) t->focus_after--; return 0;
    default: return 0;
    }
}
static int32_t t_query(void *c, int what) {
    trace *t = c;
    switch (what) {
    case ES2_SHELL_MOVIE_Q_BAY_BAR: return t->bar;
    case ES2_SHELL_MOVIE_Q_PANEL: return t->panel;
    case ES2_SHELL_MOVIE_Q_FULLSCREEN: return t->fullscreen;
    case ES2_SHELL_MOVIE_Q_PAGE: return t->page;
    case ES2_SHELL_MOVIE_Q_FOCUS: return t->focus_after == 0;
    case ES2_SHELL_MOVIE_Q_QUIT: return t->quit;
    default: return 0;
    }
}
static int has(const trace *t, uint32_t action, int32_t arg) {
    for (int i = 0; i < t->n; i++) if (t->action[i] == action && t->arg[i] == arg) return 1;
    return 0;
}
static int times(const trace *t, uint32_t action) {
    int k = 0;
    for (int i = 0; i < t->n; i++) k += t->action[i] == action;
    return k;
}
static int first(const trace *t, uint32_t action) {
    for (int i = 0; i < t->n; i++) if (t->action[i] == action) return i;
    return -1;
}

static void player(void) {
    es2_shell_movies m;
    trace t;
    es2_shell_movie_hooks h = {&t, t_call, t_query};
    /* empty queue: nothing */
    es2_shell_movies_init(&m, 1);
    memset(&t, 0, sizeof t); t.m = &m;
    CHECK(es2_shell_movies_play(&m, 1, &h) == 0 && t.n == 0 && m.active == 0);
    /* flag 0: nothing even with records */
    es2_shell_movies_init(&m, 1);
    es2_shell_movies_credits(&m);
    m.enabled = 0;
    CHECK(es2_shell_movies_play(&m, 1, &h) == 0 && t.n == 0 && count(&m) == 1);

    /* intro: two records played, no palette, rings emptied, final fade-in */
    es2_shell_movies_init(&m, 1);
    es2_shell_movies_intro(&m, 0);
    memset(&t, 0, sizeof t); t.m = &m;
    t.open_result = 1; t.play_result[0] = t.play_result[1] = ES2_SHELL_MOVIE_ENDED; t.fullscreen = 1;
    CHECK(es2_shell_movies_play(&m, 0, &h) == 0);
    CHECK(t.plays == 2 && t.played[0] == 0x44 && t.played[1] == 0x45 && count(&m) == 0 && m.play == 2);
    CHECK(t.action[0] == ES2_SHELL_MOVIE_FADE_OUT && t.action[1] == ES2_SHELL_MOVIE_FOCUS_LOST);
    CHECK(times(&t, ES2_SHELL_MOVIE_PALETTE) == 0 && has(&t, ES2_SHELL_MOVIE_OPEN_DEVICE, 0));
    CHECK(times(&t, ES2_SHELL_MOVIE_CLEAR) == 2 && times(&t, ES2_SHELL_MOVIE_CLOSE_DEVICE) == 2);
    CHECK(times(&t, ES2_SHELL_MOVIE_DRAIN) == 0);   /* intro: no drain */
    CHECK(t.action[t.n - 2] == ES2_SHELL_MOVIE_FOCUS_GAINED && t.action[t.n - 1] == ES2_SHELL_MOVIE_FADE_IN);
    CHECK(has(&t, ES2_SHELL_MOVIE_SHOW, ES2_SHELL_MOVIE_OBJ_ROOT) == 0);   /* full screen: no hide/show */
    /* intro skipped by the user: the second one is not opened */
    es2_shell_movies_init(&m, 1);
    es2_shell_movies_intro(&m, 2);
    memset(&t, 0, sizeof t); t.m = &m;
    t.open_result = 1; t.play_result[0] = ES2_SHELL_MOVIE_SKIPPED;
    es2_shell_movies_play(&m, 0, &h);
    CHECK(t.plays == 1 && m.intro_skipped && times(&t, ES2_SHELL_MOVIE_OPEN_DEVICE) == 1 && count(&m) == 0);
    CHECK(has(&t, ES2_SHELL_MOVIE_HIDE, ES2_SHELL_MOVIE_OBJ_ROOT) && has(&t, ES2_SHELL_MOVIE_SHOW, ES2_SHELL_MOVIE_OBJ_ROOT));
    /* a skip outside the intro latches nothing */
    es2_shell_movies_init(&m, 1);
    es2_shell_movies_credits(&m);
    memset(&t, 0, sizeof t); t.m = &m;
    t.open_result = 1; t.play_result[0] = ES2_SHELL_MOVIE_SKIPPED;
    es2_shell_movies_play(&m, 0, &h);
    CHECK(!m.intro_skipped && has(&t, ES2_SHELL_MOVIE_PALETTE, 1) && times(&t, ES2_SHELL_MOVIE_DRAIN) == 0);
    /* the device does not open: nothing played, records emptied all the same */
    es2_shell_movies_init(&m, 1);
    es2_shell_movies_victory(&m);
    memset(&t, 0, sizeof t); t.m = &m;
    es2_shell_movies_play(&m, 1, &h);
    CHECK(t.plays == 0 && count(&m) == 0 && times(&t, ES2_SHELL_MOVIE_OPEN_DEVICE) == 2 && has(&t, ES2_SHELL_MOVIE_OPEN_DEVICE, 1));
    CHECK(times(&t, ES2_SHELL_MOVIE_CLOSE_DEVICE) == 0 && times(&t, ES2_SHELL_MOVIE_DRAIN) == 1);   /* victory drains, credits not */
    /* intro that cannot open: error, quit, the record stays */
    es2_shell_movies_init(&m, 1);
    es2_shell_movies_intro(&m, 0);
    memset(&t, 0, sizeof t); t.m = &m;
    t.open_result = 1; t.play_result[0] = ES2_SHELL_MOVIE_FAILED;
    CHECK(es2_shell_movies_play(&m, 0, &h) == 1 && has(&t, ES2_SHELL_MOVIE_INTRO_ERROR, 0) && count(&m) == 2);
    CHECK(times(&t, ES2_SHELL_MOVIE_FADE_IN) == 0);
    /* other movie that cannot open: CD prompt, Retry, then plays */
    es2_shell_movies_init(&m, 1);
    es2_shell_movies_credits(&m);
    memset(&t, 0, sizeof t); t.m = &m;
    t.open_result = 1; t.play_result[0] = ES2_SHELL_MOVIE_FAILED; t.play_result[1] = ES2_SHELL_MOVIE_FAILED;
    t.play_result[2] = ES2_SHELL_MOVIE_ENDED; t.prompt_result = 1;
    CHECK(es2_shell_movies_play(&m, 0, &h) == 0 && t.plays == 3 && times(&t, ES2_SHELL_MOVIE_CD_PROMPT) == 2);
    CHECK(count(&m) == 0 && m.active == 1);
    /* CD prompt quit: the record stays, no fade-in */
    es2_shell_movies_init(&m, 1);
    es2_shell_movies_credits(&m);
    memset(&t, 0, sizeof t); t.m = &m;
    t.open_result = 1; t.play_result[0] = ES2_SHELL_MOVIE_FAILED; t.prompt_result = 0;
    CHECK(es2_shell_movies_play(&m, 0, &h) == 1 && count(&m) == 1 && times(&t, ES2_SHELL_MOVIE_FADE_IN) == 0);
    /* quit flag already set: nothing opened after the device */
    es2_shell_movies_init(&m, 1);
    es2_shell_movies_credits(&m);
    memset(&t, 0, sizeof t); t.m = &m;
    t.open_result = 1; t.quit = 1;
    CHECK(es2_shell_movies_play(&m, 0, &h) == 1 && t.plays == 0 && count(&m) == 1);

    /* mission page 0 in theatre 5 with the bay bar and focus pumping */
    es2_shell_movies_init(&m, 1);
    es2_shell_movies_mission(&m, 0, 5, 0, 0);
    memset(&t, 0, sizeof t); t.m = &m; t.group = 5;
    t.open_result = 1; t.play_result[0] = t.play_result[1] = t.play_result[2] = ES2_SHELL_MOVIE_ENDED;
    t.bar = 1; t.page = 0; t.focus_after = 3;
    CHECK(es2_shell_movies_play(&m, 1, &h) == 0);
    CHECK(t.plays == 3 && t.played[0] == 0x4a && t.played[1] == 0x4f && t.played[2] == 0x55 && count(&m) == 0);
    CHECK(has(&t, ES2_SHELL_MOVIE_PALETTE, 4) && times(&t, ES2_SHELL_MOVIE_THEATRE_STEP) == 1);
    CHECK(times(&t, ES2_SHELL_MOVIE_BAY_BUTTONS) == 2);   /* not before the dropship */
    CHECK(has(&t, ES2_SHELL_MOVIE_BAY_SELECT, 1) && times(&t, ES2_SHELL_MOVIE_TEARDOWN) == 1);
    CHECK(times(&t, ES2_SHELL_MOVIE_PUMP) == 5);          /* two extra rounds for the focus */
    /* the dropship clears esi: the final fade-in follows */
    CHECK(t.action[t.n - 1] == ES2_SHELL_MOVIE_FADE_IN && times(&t, ES2_SHELL_MOVIE_FADE_IN) == 2);
    CHECK(first(&t, ES2_SHELL_MOVIE_TEARDOWN) < first(&t, ES2_SHELL_MOVIE_THEATRE_STEP));
    /* theatre 2 with the theatre panel visible: wait, hide, no final fade-in */
    es2_shell_movies_init(&m, 1);
    es2_shell_movies_mission(&m, 0, 2, 0, 0);
    memset(&t, 0, sizeof t); t.m = &m; t.group = 2;
    t.open_result = 1; t.play_result[0] = t.play_result[1] = ES2_SHELL_MOVIE_ENDED;
    t.bar = 1; t.panel = 1; t.page = 1;
    es2_shell_movies_play(&m, 1, &h);
    CHECK(t.plays == 2 && times(&t, ES2_SHELL_MOVIE_WAIT) == 2 && has(&t, ES2_SHELL_MOVIE_REPAINT, ES2_SHELL_MOVIE_OBJ_TREES));
    CHECK(times(&t, ES2_SHELL_MOVIE_FADE_IN) == 1 && t.action[t.n - 1] != ES2_SHELL_MOVIE_FADE_IN);
    CHECK(has(&t, ES2_SHELL_MOVIE_TEXT_PANEL, 1) && has(&t, ES2_SHELL_MOVIE_PALETTE, 3));
    /* callback word */
    es2_shell_movies_init(&m, 1);
    es2_shell_movie_enqueue(&m, 7, es2_shell_movie_rect_full, -1, 0x1234, 0);
    memset(&t, 0, sizeof t); t.m = &m;
    es2_shell_movies_play(&m, 0, &h);
    CHECK(has(&t, ES2_SHELL_MOVIE_CALLBACK, 0x1234) && count(&m) == 0);
    /* ring wrap: play index advances modulo 10 */
    es2_shell_movies_init(&m, 1);
    for (int round = 0; round < 3; round++)
        for (int i = 0; i < 4; i++) {
            es2_shell_movie_enqueue(&m, 0x20 + i, es2_shell_movie_rect_full, -1, 0, 0);
            if (i == 3) { memset(&t, 0, sizeof t); t.m = &m; es2_shell_movies_play(&m, 0, &h); }
        }
    CHECK(m.write == 2 && m.play == 2 && count(&m) == 0);
    /* the bound gate (es2_ui.gate) follows 0x470fb0: 1 at every drain of a
     * mission page's run (the buttons ignore clicks queued in its fades), 0
     * while the CD prompt waits, still 1 after the run, 0 at the next call
     * on the empty ring (0x41e9fc; the main loop's 0x4020b2) */
    {
        int gate = 0;
        es2_shell_movies_init(&m, 1);
        m.gate = &gate;
        es2_shell_movies_mission(&m, 0, 1, 0, 0);
        memset(&t, 0, sizeof t); t.m = &m; t.group = 1;
        t.open_result = 1; t.play_result[0] = ES2_SHELL_MOVIE_FAILED;
        t.play_result[1] = t.play_result[2] = ES2_SHELL_MOVIE_ENDED; t.prompt_result = 1;
        t.bar = 1; t.page = 0; t.gate_prompt = -1;
        CHECK(es2_shell_movies_play(&m, 1, &h) == 0 && t.plays == 3 && count(&m) == 0);
        CHECK(t.gate_drains >= 3 && t.gate_open_drains == 0 && t.gate_prompt == 0);
        CHECK(gate == 1 && m.active == 1);
        CHECK(es2_shell_movies_play(&m, 1, &h) == 0 && gate == 0 && m.active == 0);
    }
}

static void strings(void) {
    char b[80];
    CHECK(es2_shell_mci_open(b, sizeof b, "D:\\avi\\c1.avi", 1234) > 0 &&
          !strcmp(b, "open D:\\avi\\c1.avi alias mov style child parent 1234"));
    CHECK(es2_shell_mci_open(b, 20, "D:\\avi\\c1.avi", 1234) == -1);
    CHECK(es2_shell_mci_open(b, sizeof b, NULL, 1) == -1);
    CHECK(es2_shell_mci_palette(b, sizeof b, 0) > 0 && !strcmp(b, "setvideo mov palette handle to 0"));
    CHECK(es2_shell_mci_palette(b, 10, 0) == -1);
    CHECK(!strcmp(es2_shell_mci_open_device, "open avivideo") && !strcmp(es2_shell_mci_close, "close mov"));
}

/* ---- CD check and CD prompt ---------------------------------------- */
typedef struct cdprobe { int calls, result; char seen[128]; } cdprobe;
static int cd_probe(void *c, const char *path) {
    cdprobe *p = c;
    p->calls++;
    snprintf(p->seen, sizeof p->seen, "%s", path);
    return p->result;
}
typedef struct cdbox { int calls, answers[4]; } cdbox;
static int cd_box(void *c) { cdbox *b = c; int a = b->answers[b->calls < 4 ? b->calls : 3]; b->calls++; return a; }
static void cdcheck(void) {
    cdprobe p = {0, 1, ""};
    char path[64];
    CHECK(es2_shell_cd_policy_parse(NULL) == ES2_SHELL_CD_ORIGINAL && es2_shell_cd_policy_parse("") == ES2_SHELL_CD_ORIGINAL);
    CHECK(es2_shell_cd_policy_parse("original") == ES2_SHELL_CD_ORIGINAL && es2_shell_cd_policy_parse("skip") == ES2_SHELL_CD_SKIP);
    CHECK(es2_shell_cd_policy_parse("Skip") == -1 && es2_shell_cd_policy_parse("no") == -1);
    CHECK(es2_shell_cd_check(ES2_SHELL_CD_ORIGINAL, "E:\\", cd_probe, &p, path, sizeof path) == 0);
    CHECK(p.calls == 1 && !strcmp(p.seen, "E:\\avi\\pt1.avi") && !strcmp(path, p.seen));
    p.result = 0;
    CHECK(es2_shell_cd_check(ES2_SHELL_CD_ORIGINAL, "E:\\", cd_probe, &p, NULL, 0) == 5 && p.calls == 2);
    CHECK(es2_shell_cd_check(ES2_SHELL_CD_SKIP, "E:\\", cd_probe, &p, path, sizeof path) == 0 && p.calls == 2);
    CHECK(es2_shell_cd_check(ES2_SHELL_CD_ORIGINAL, "E:\\", NULL, &p, path, sizeof path) == -1);
    CHECK(es2_shell_cd_check(ES2_SHELL_CD_ORIGINAL, "E:\\", cd_probe, &p, path, 14) == -1 && p.calls == 2);
    CHECK(es2_shell_cd_check(ES2_SHELL_CD_ORIGINAL, "E:\\", cd_probe, &p, path, 15) == 5 && p.calls == 3);
    {
        cdbox b = {0, {1, 0, 0, 0}};
        int quit = 0;
        CHECK(es2_shell_cd_prompt(ES2_SHELL_CD_ORIGINAL, cd_box, &b, &quit) == 1 && !quit && b.calls == 1);
        CHECK(es2_shell_cd_prompt(ES2_SHELL_CD_ORIGINAL, cd_box, &b, &quit) == 0 && quit == 1 && b.calls == 2);
        quit = 0;
        CHECK(es2_shell_cd_prompt(ES2_SHELL_CD_SKIP, cd_box, &b, &quit) == 0 && !quit && b.calls == 2);
        CHECK(es2_shell_cd_prompt(ES2_SHELL_CD_ORIGINAL, NULL, NULL, &quit) == 0 && quit == 1);
        CHECK(es2_shell_cd_prompt(ES2_SHELL_CD_ORIGINAL, cd_box, &b, NULL) == 0);
    }
}

int main(void) {
    names();
    queue();
    selection();
    player();
    strings();
    cdcheck();
    printf("%u checks, %u failures\n", checks, failures);
    return failures != 0;
}
