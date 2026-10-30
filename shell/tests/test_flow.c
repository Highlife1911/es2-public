/* The shell flow (es2/shell/flow.h) over a synthetic host that
 * records every effect: each handler, transition, box decision, the entry
 * dispatch and the exit save. */
#include "es2/shell/flow.h"
#include <stdio.h>
#include <string.h>

static unsigned checks, failures;
#define CHECK(x) do { checks++; if (!(x)) { failures++; fprintf(stderr, "line %d: %s\n", __LINE__, #x); } } while (0)

/* The synthetic host: what it is asked, and what it answers. */
typedef struct fake {
    char log[1024];
    int store_ok, write_ok, save_ok, load_answer, new_game_answer, campaign_ok, launch_ok, launch_warning;
    int results_ok, prepare_ok;
    int16_t slot_final_word, new_game_final_word;
    es2_shell_flow_results results;
    uint8_t written[ES2_SHELL_PREFS_SIZE];
} fake;
static void note(fake *f, const char *format, int a, int b) {
    char item[64];
    snprintf(item, sizeof item, format, a, b);
    if (strlen(f->log) + strlen(item) + 2 < sizeof f->log) { strcat(f->log, item); strcat(f->log, ";"); }
}
static int f_store_mode(void *ctx, uint8_t mode) { fake *f = ctx; note(f, "mode %d", mode, 0); return f->store_ok; }
static int f_write_prefs(void *ctx, const uint8_t b[ES2_SHELL_PREFS_SIZE]) {
    fake *f = ctx;
    memcpy(f->written, b, ES2_SHELL_PREFS_SIZE);
    note(f, "prefs", 0, 0);
    return f->write_ok;
}
static int f_save(void *ctx, int slot, const char *text) { fake *f = ctx; note(f, text ? "save %d text" : "save %d", slot, 0); return f->save_ok; }
static int f_load(void *ctx, int slot, int16_t *final_word) {
    fake *f = ctx;
    note(f, "load %d", slot, 0);
    if (f->load_answer > 0) *final_word = f->slot_final_word;
    return f->load_answer;
}
static int f_new_game(void *ctx, const char *name, uint16_t rank, int16_t *final_word) {
    fake *f = ctx;
    note(f, "new %d %d", rank, (int)strlen(name));
    if (f->new_game_answer < 0) *final_word = f->new_game_final_word;
    return f->new_game_answer;
}
static int f_campaign(void *ctx) { fake *f = ctx; note(f, "campaign", 0, 0); return f->campaign_ok; }
static int f_launch(void *ctx, int *warning) {
    fake *f = ctx;
    note(f, "launch", 0, 0);
    *warning = f->launch_ok ? ES2_SHELL_LAUNCH_OK : f->launch_warning;
    return f->launch_ok;
}
static int f_rebalance(void *ctx, uint8_t build) { fake *f = ctx; note(f, "rebalance %d", build, 0); return 1; }
static int f_results(void *ctx, es2_shell_flow_results *r) {
    fake *f = ctx;
    note(f, "results", 0, 0);
    if (f->results_ok) *r = f->results;
    return f->results_ok;
}
static int f_prepare_next(void *ctx) { fake *f = ctx; note(f, "next", 0, 0); return f->prepare_ok; }
static void f_replay(void *ctx) { note(ctx, "replay", 0, 0); }
static void f_online(void *ctx) { note(ctx, "manual", 0, 0); }
static int f_debug_prepare(void *ctx, const char *mission) {
    fake *f = ctx;
    if (mission) note(f, "debug typed %d", (int)strlen(mission), 0);
    else note(f, "debug default", 0, 0);
    return f->prepare_ok;
}

static fake host_state;
/* A flow over a fresh host that answers yes to everything; prefs.cfg with
 * mode byte `mode` and weapons building `build`. */
static es2_shell_flow *start(int mode, int build) {
    static es2_shell_flow flow;
    static const es2_shell_flow_host host = {&host_state, f_store_mode, f_write_prefs, f_save, f_load, f_new_game,
                                             f_campaign, f_launch, f_rebalance, f_results, f_prepare_next, f_replay,
                                             f_online, NULL, f_debug_prepare};
    uint8_t prefs[ES2_SHELL_PREFS_SIZE] = {0};
    es2_span span = {prefs, sizeof prefs};
    memset(&host_state, 0, sizeof host_state);
    host_state.store_ok = host_state.write_ok = host_state.save_ok = host_state.load_answer = 1;
    host_state.new_game_answer = host_state.campaign_ok = host_state.launch_ok = 1;
    host_state.results_ok = host_state.prepare_ok = 1;
    host_state.slot_final_word = 2;
    prefs[ES2_SHELL_PREFS_MODE] = (uint8_t)mode;
    prefs[ES2_SHELL_PREFS_BUILD] = (uint8_t)build;
    es2_shell_flow_init(&flow, &host, &span);
    return &flow;
}
static int logged(const char *expected) { return !strcmp(host_state.log, expected); }
static void clear_log(void) { host_state.log[0] = 0; }

/* A campaign in the bay through a new career (skill 2). */
static es2_shell_flow *in_bay(int build) {
    es2_shell_flow *f = start(1, build);
    es2_shell_flow_event(f, ES2_FLOW_START_NEW_GAME, NULL);
    es2_shell_flow_event(f, ES2_FLOW_SKILL, NULL);
    es2_shell_flow_event(f, ES2_FLOW_SKILL, NULL);
    es2_shell_flow_event(f, ES2_FLOW_REGISTRATION_ACCEPT, "ACE");
    clear_log();
    return f;
}

/* (vshell_state.md): the bay selection 0x482bc9 stays across bay visits and is
 * 0 after a new game and after every load. */
static void bay_selection(void) {
    es2_shell_flow *f = in_bay(0);
    CHECK(f->bay_select == 0);
    f->bay_select = 3;   /* the WEAPONS bay button */
    es2_shell_flow_event(f, ES2_FLOW_BAY_WEAPONS, NULL);
    es2_shell_flow_event(f, ES2_FLOW_BAY_MAIN_MENU, NULL);
    CHECK(f->bay_select == 3);
    host_state.load_answer = 1;
    CHECK(es2_shell_flow_event(f, ES2_FLOW_CONTINUE_GAME, NULL) == ES2_FLOW_DONE && f->bay_select == 0);
    f->bay_select = 5;
    es2_shell_flow_event(f, ES2_FLOW_BAY_MAIN_MENU, NULL);
    es2_shell_flow_event(f, ES2_FLOW_START_NEW_GAME, NULL);
    es2_shell_flow_event(f, ES2_FLOW_REGISTRATION_ACCEPT, "ACE");
    CHECK(f->bay_select == 0);
    f->bay_select = 2;
    es2_shell_flow_game_created(f);
    CHECK(f->bay_select == 0);
}

static void init_and_entry(void) {
    es2_shell_flow *f = start(0, 1);
    CHECK(f->mode == 0 && f->final_word == 0 && f->game == ES2_FLOW_GAME_NONE && f->screen == ES2_FLOW_MAIN_MENU);
    CHECK(f->bay_word == -1 && f->slot == 10 && f->naming == 0 && f->skill == 0 && f->caller == 0);
    CHECK(f->box == ES2_FLOW_BOX_NONE && !f->quit && f->exit_code == 0);
    f = start(1, 0);
    CHECK(f->mode == 1);
    /* absent prefs.cfg: zero blocks */
    {
        es2_shell_flow g;
        es2_shell_flow_init(&g, NULL, NULL);
        CHECK(g.mode == 0 && g.prefs.current[ES2_SHELL_PREFS_BUILD] == 0);
    }
    CHECK(es2_shell_flow_entry_of(3) == ES2_FLOW_ENTRY_RESULTS && es2_shell_flow_entry_of(4) == ES2_FLOW_ENTRY_RESULTS);
    CHECK(es2_shell_flow_entry_of(6) == ES2_FLOW_ENTRY_MENU);
    for (int s = -1; s <= 9; s++)
        if (s != 3 && s != 4 && s != 6) CHECK(es2_shell_flow_entry_of(s) == ES2_FLOW_ENTRY_INTRO);
    /* -X1: nothing is read; the main menu */
    f = start(1, 0);
    CHECK(es2_shell_flow_start(f, 1) == ES2_FLOW_ENTRY_INTRO && f->screen == ES2_FLOW_MAIN_MENU && logged(""));
    CHECK(es2_shell_flow_start(f, 6) == ES2_FLOW_ENTRY_MENU && logged(""));
    /* the exit without a game saves nothing */
    CHECK(es2_shell_flow_finish(f) == 0 && logged(""));
}

static void results_path(void) {
    es2_shell_flow *f;
    /* slot 10 empty (or unreadable): main menu, nothing loaded */
    f = start(1, 0);
    host_state.load_answer = -1;
    CHECK(es2_shell_flow_start(f, 3) == ES2_FLOW_ENTRY_RESULTS && f->outcome == ES2_FLOW_OUTCOME_NO_SLOT);
    CHECK(logged("load 10;") && f->game == ES2_FLOW_GAME_NONE && f->screen == ES2_FLOW_MAIN_MENU);
    f = start(1, 0);
    host_state.load_answer = 0;
    es2_shell_flow_start(f, 4);
    CHECK(f->outcome == ES2_FLOW_OUTCOME_NO_SLOT && f->game == ES2_FLOW_GAME_NONE);
    /* rejected results: main menu with the campaign loaded */
    f = start(1, 0);
    host_state.results_ok = 0;
    es2_shell_flow_start(f, 3);
    CHECK(f->outcome == ES2_FLOW_OUTCOME_REJECTED && f->game == ES2_FLOW_GAME_CAMPAIGN && logged("load 10;results;"));
    CHECK(es2_shell_flow_finish(f) == 0 && logged("load 10;results;save 10;"));
    /* advance 2: next preparation, the bay */
    f = start(1, 0);
    host_state.results = (es2_shell_flow_results){ES2_SHELL_RESULTS_ADVANCED, 2, 2};
    es2_shell_flow_start(f, 4);
    CHECK(f->outcome == ES2_FLOW_OUTCOME_NEXT && f->screen == ES2_FLOW_SERVICE_BAY && f->bay_word == -1);
    CHECK(logged("load 10;results;next;") && f->final_word == 2 && f->box == ES2_FLOW_BOX_NONE);
    CHECK(f->page == 4); /* 0x40f62d: the debrief page */
    /* advance 2, the preparation fails: main menu */
    f = start(1, 0);
    host_state.results = (es2_shell_flow_results){ES2_SHELL_RESULTS_ADVANCED, 2, 2};
    host_state.prepare_ok = 0;
    es2_shell_flow_start(f, 3);
    CHECK(f->outcome == ES2_FLOW_OUTCOME_NEXT_FAILED && f->screen == ES2_FLOW_MAIN_MENU && f->page == 4);
    /* advance 1: final word 1, slot 10 saved, End box with line 1 */
    f = start(1, 0);
    host_state.results = (es2_shell_flow_results){ES2_SHELL_RESULTS_ADVANCED, 1, 1};
    es2_shell_flow_start(f, 3);
    CHECK(f->outcome == ES2_FLOW_OUTCOME_WON && logged("load 10;results;save 10;") && f->final_word == 1);
    CHECK(f->box == ES2_FLOW_BOX_END_OF_GAME && f->box_value == 1 && f->screen == ES2_FLOW_MAIN_MENU);
    CHECK(es2_shell_flow_event(f, ES2_FLOW_START_NEW_GAME, NULL) == ES2_FLOW_IGNORED);   /* the box is modal */
    CHECK(es2_shell_flow_event(f, ES2_FLOW_END_OKAY, NULL) == ES2_FLOW_DONE && f->box == ES2_FLOW_BOX_NONE);
    /* other mode, other advance values */
    f = start(0, 0);
    host_state.results = (es2_shell_flow_results){ES2_SHELL_RESULTS_OTHER_MODE, 0, 2};
    es2_shell_flow_start(f, 3);
    CHECK(f->outcome == ES2_FLOW_OUTCOME_OTHER_MODE && f->screen == ES2_FLOW_MAIN_MENU);
    f = start(1, 0);
    host_state.results = (es2_shell_flow_results){ES2_SHELL_RESULTS_ADVANCED, 7, 2};
    es2_shell_flow_start(f, 3);
    CHECK(f->outcome == ES2_FLOW_OUTCOME_STOPPED && f->outcome_advance == 7);
    f = start(1, 0);
    host_state.results = (es2_shell_flow_results){1, 0, 2};   /* another ending */
    es2_shell_flow_start(f, 3);
    CHECK(f->outcome == ES2_FLOW_OUTCOME_STOPPED && f->box == ES2_FLOW_BOX_NONE);
}

static void replay_box(void) {
    es2_shell_flow *f;
    /* player lost: box value 3; Yes loads slot 10 and launches */
    f = start(1, 0);
    host_state.results = (es2_shell_flow_results){ES2_SHELL_RESULTS_PLAYER_LOST, 0, 3};
    es2_shell_flow_start(f, 3);
    CHECK(f->outcome == ES2_FLOW_OUTCOME_REPLAY && f->box == ES2_FLOW_BOX_REPLAY && f->box_value == 3 && f->final_word == 3);
    CHECK(es2_shell_flow_event(f, ES2_FLOW_END_OKAY, NULL) == ES2_FLOW_IGNORED && f->box == ES2_FLOW_BOX_REPLAY);
    clear_log();
    CHECK(es2_shell_flow_event(f, ES2_FLOW_REPLAY_YES, NULL) == ES2_FLOW_DONE);
    CHECK(logged("load 10;replay;") && f->quit && f->exit_code == 2 && f->game == ES2_FLOW_GAME_CAMPAIGN);
    CHECK(f->final_word == 2 && f->box == ES2_FLOW_BOX_NONE);
    CHECK(es2_shell_flow_event(f, ES2_FLOW_START_NEW_GAME, NULL) == ES2_FLOW_IGNORED);   /* after the quit flag */
    CHECK(es2_shell_flow_finish(f) == 2 && logged("load 10;replay;save 10;"));
    /* advance 0 (war lost): box value 0; No saves slot 10, main menu */
    f = start(1, 0);
    host_state.results = (es2_shell_flow_results){ES2_SHELL_RESULTS_ADVANCED, 0, 0};
    es2_shell_flow_start(f, 4);
    CHECK(f->box == ES2_FLOW_BOX_REPLAY && f->box_value == 0);
    clear_log();
    CHECK(es2_shell_flow_event(f, ES2_FLOW_REPLAY_NO, NULL) == ES2_FLOW_DONE && logged("save 10;"));
    CHECK(f->box == ES2_FLOW_BOX_NONE && f->screen == ES2_FLOW_MAIN_MENU && !f->quit && f->final_word == 0);
    /* Yes with slot 10 gone: refused, main menu */
    f = start(1, 0);
    host_state.results = (es2_shell_flow_results){ES2_SHELL_RESULTS_PLAYER_LOST, 0, 3};
    es2_shell_flow_start(f, 3);
    host_state.load_answer = -1;
    CHECK(es2_shell_flow_event(f, ES2_FLOW_REPLAY_YES, NULL) == ES2_FLOW_REFUSED && f->refusal == ES2_FLOW_NO_SLOT_GAME);
    CHECK(!f->quit && f->box == ES2_FLOW_BOX_NONE && f->screen == ES2_FLOW_MAIN_MENU);
    /* the text presenter's end of input closes the box */
    f = start(1, 0);
    host_state.results = (es2_shell_flow_results){ES2_SHELL_RESULTS_PLAYER_LOST, 0, 3};
    es2_shell_flow_start(f, 3);
    es2_shell_flow_box_close(f);
    CHECK(f->box == ES2_FLOW_BOX_NONE && f->screen == ES2_FLOW_MAIN_MENU);
}

static void new_career(void) {
    es2_shell_flow *f = start(0, 0);
    CHECK(es2_shell_flow_event(f, ES2_FLOW_START_NEW_GAME, NULL) == ES2_FLOW_DONE);
    CHECK(logged("mode 1;") && f->mode == 1 && f->screen == ES2_FLOW_REGISTRATION);
    /* SKILL steps (value + 1) mod 4 */
    for (int i = 1; i <= 5; i++) {
        es2_shell_flow_event(f, ES2_FLOW_SKILL, NULL);
        CHECK(f->skill == i % 4);
    }
    CHECK(es2_shell_flow_event(f, ES2_FLOW_QUIT, NULL) == ES2_FLOW_IGNORED);   /* not on this screen */
    CHECK(es2_shell_flow_event(f, ES2_FLOW_REGISTRATION_CANCEL, NULL) == ES2_FLOW_DONE);
    CHECK(f->screen == ES2_FLOW_MAIN_MENU && logged("mode 1;") && f->game == ES2_FLOW_GAME_NONE);
    /* ACCEPT: the new game with the skill as rank, the bay */
    es2_shell_flow_event(f, ES2_FLOW_START_NEW_GAME, NULL);
    clear_log();
    CHECK(f->skill == 1);   /* showing Registration again keeps 0x476248 (no writer but SKILL) */
    f->page = 4;
    CHECK(es2_shell_flow_event(f, ES2_FLOW_REGISTRATION_ACCEPT, "PILOT") == ES2_FLOW_DONE);
    CHECK(f->page == 0);    /* 0x43c7d1 */
    CHECK(logged("new 1 5;") && f->game == ES2_FLOW_GAME_CAMPAIGN && f->final_word == 2);
    CHECK(f->screen == ES2_FLOW_SERVICE_BAY && f->bay_word == -1);
    CHECK(es2_shell_flow_finish(f) == 0 && logged("new 1 5;save 10;"));
    /* failed with a game loaded / with nothing loaded */
    f = start(1, 0);
    es2_shell_flow_event(f, ES2_FLOW_START_NEW_GAME, NULL);
    host_state.new_game_answer = -1;
    host_state.new_game_final_word = 2;
    CHECK(es2_shell_flow_event(f, ES2_FLOW_REGISTRATION_ACCEPT, "X") == ES2_FLOW_FAILED);
    CHECK(f->game == ES2_FLOW_GAME_SLOT && f->final_word == 2 && f->screen == ES2_FLOW_MAIN_MENU);
    f = start(1, 0);
    es2_shell_flow_event(f, ES2_FLOW_START_NEW_GAME, NULL);
    host_state.new_game_answer = 0;
    f->page = 4;
    CHECK(es2_shell_flow_event(f, ES2_FLOW_REGISTRATION_ACCEPT, "X") == ES2_FLOW_FAILED);
    CHECK(f->game == ES2_FLOW_GAME_NONE && f->screen == ES2_FLOW_MAIN_MENU && f->page == 4);
    /* the mode store fails: stays in the main menu, the mode word is set */
    f = start(0, 0);
    host_state.store_ok = 0;
    CHECK(es2_shell_flow_event(f, ES2_FLOW_START_NEW_GAME, NULL) == ES2_FLOW_FAILED);
    CHECK(f->mode == 1 && f->screen == ES2_FLOW_MAIN_MENU);
}

static void continue_game(void) {
    es2_shell_flow *f;
    /* slot 10 with final word 2: the campaign, selected slot 10 */
    f = start(0, 0);
    f->slot = 3;
    CHECK(es2_shell_flow_event(f, ES2_FLOW_CONTINUE_GAME, NULL) == ES2_FLOW_DONE);
    CHECK(logged("mode 1;load 10;campaign;") && f->slot == 10 && f->game == ES2_FLOW_GAME_CAMPAIGN);
    CHECK(f->screen == ES2_FLOW_SERVICE_BAY && f->bay_word == -1);
    /* the campaign cannot be prepared: failed, the slot stays loaded */
    f = start(1, 0);
    host_state.campaign_ok = 0;
    CHECK(es2_shell_flow_event(f, ES2_FLOW_CONTINUE_GAME, NULL) == ES2_FLOW_FAILED);
    CHECK(f->game == ES2_FLOW_GAME_SLOT && f->screen == ES2_FLOW_MAIN_MENU);
    /* final words 0, 1, 3, 9: the End box with that word, the game stays loaded */
    const int16_t words[4] = {0, 1, 3, 9};
    for (int i = 0; i < 4; i++) {
        f = start(1, 0);
        host_state.slot_final_word = words[i];
        CHECK(es2_shell_flow_event(f, ES2_FLOW_CONTINUE_GAME, NULL) == ES2_FLOW_DONE);
        CHECK(f->box == ES2_FLOW_BOX_END_OF_GAME && f->box_value == words[i] && f->game == ES2_FLOW_GAME_SLOT);
        CHECK(f->screen == ES2_FLOW_MAIN_MENU && logged("mode 1;load 10;"));
        es2_shell_flow_event(f, ES2_FLOW_END_OKAY, NULL);
        CHECK(es2_shell_flow_finish(f) == 0 && logged("mode 1;load 10;save 10;"));
    }
    /* slot 10 without flag: the final word in memory (0 at start: war lost) */
    f = start(1, 0);
    host_state.load_answer = -1;
    CHECK(es2_shell_flow_event(f, ES2_FLOW_CONTINUE_GAME, NULL) == ES2_FLOW_DONE);
    CHECK(f->box == ES2_FLOW_BOX_END_OF_GAME && f->box_value == 0 && f->game == ES2_FLOW_GAME_NONE);
    /* ... final word 2 in memory with a campaign: the bay; without: main menu */
    f = in_bay(0);
    es2_shell_flow_event(f, ES2_FLOW_BAY_MAIN_MENU, NULL);
    host_state.load_answer = -1;
    CHECK(es2_shell_flow_event(f, ES2_FLOW_CONTINUE_GAME, NULL) == ES2_FLOW_DONE && f->screen == ES2_FLOW_SERVICE_BAY);
    f = start(1, 0);
    es2_shell_flow_game_created(f);   /* a practice game: loaded, final word 2, no campaign */
    host_state.load_answer = -1;
    CHECK(es2_shell_flow_event(f, ES2_FLOW_CONTINUE_GAME, NULL) == ES2_FLOW_DONE && f->screen == ES2_FLOW_MAIN_MENU);
    CHECK(f->box == ES2_FLOW_BOX_NONE);
    /* a load error: failed, nothing changes */
    f = start(1, 0);
    host_state.load_answer = 0;
    CHECK(es2_shell_flow_event(f, ES2_FLOW_CONTINUE_GAME, NULL) == ES2_FLOW_FAILED && f->game == ES2_FLOW_GAME_NONE);
    CHECK(f->box == ES2_FLOW_BOX_NONE);
    /* the mode store fails: nothing loaded */
    f = start(0, 0);
    host_state.store_ok = 0;
    CHECK(es2_shell_flow_event(f, ES2_FLOW_CONTINUE_GAME, NULL) == ES2_FLOW_FAILED && logged("mode 1;"));
}

static void saved_games(void) {
    es2_shell_flow *f = start(0, 0);
    CHECK(es2_shell_flow_event(f, ES2_FLOW_SAVE_RESTORE, NULL) == ES2_FLOW_DONE);
    CHECK(logged("mode 1;") && f->screen == ES2_FLOW_SAVED_GAMES && f->caller == 0);
    /* SAVE without a game: refused; slot 10 selected: disabled */
    es2_shell_flow_select_slot(f, 4);
    CHECK(!es2_shell_flow_enabled(f, ES2_FLOW_SLOT_SAVE, 0));
    CHECK(es2_shell_flow_event(f, ES2_FLOW_SLOT_SAVE, NULL) == ES2_FLOW_REFUSED && f->refusal == ES2_FLOW_NO_GAME);
    CHECK(es2_shell_flow_event(f, ES2_FLOW_SLOT_ACCEPT, "row") == ES2_FLOW_IGNORED && logged("mode 1;"));
    /* RESTORE needs the slot's flag */
    CHECK(!es2_shell_flow_enabled(f, ES2_FLOW_SLOT_RESTORE, 0) && es2_shell_flow_enabled(f, ES2_FLOW_SLOT_RESTORE, 1));
    es2_shell_flow_select_slot(f, 10);
    CHECK(!es2_shell_flow_enabled(f, ES2_FLOW_SLOT_RESTORE, 1));
    CHECK(es2_shell_flow_event(f, ES2_FLOW_SLOT_RESTORE, NULL) == ES2_FLOW_IGNORED);
    /* RESTORE of slot 4 (final word 2): load, save as slot 10, campaign, bay */
    es2_shell_flow_select_slot(f, 4);
    clear_log();
    CHECK(es2_shell_flow_event(f, ES2_FLOW_SLOT_RESTORE, NULL) == ES2_FLOW_DONE);
    CHECK(logged("load 4;save 10;campaign;") && f->game == ES2_FLOW_GAME_CAMPAIGN && f->screen == ES2_FLOW_SERVICE_BAY);
    CHECK(f->bay_word == -1);
    /* SAVE with a game: naming, then ACCEPT saves the row; CANCEL writes nothing */
    f = in_bay(0);
    es2_shell_flow_event(f, ES2_FLOW_BAY_MAIN_MENU, NULL);
    es2_shell_flow_event(f, ES2_FLOW_SAVE_RESTORE, NULL);
    es2_shell_flow_select_slot(f, 2);
    clear_log();
    CHECK(es2_shell_flow_enabled(f, ES2_FLOW_SLOT_SAVE, 0));
    CHECK(es2_shell_flow_event(f, ES2_FLOW_SLOT_SAVE, NULL) == ES2_FLOW_DONE && f->naming == 1);
    CHECK(!es2_shell_flow_enabled(f, ES2_FLOW_SLOT_SAVE, 0));
    CHECK(es2_shell_flow_event(f, ES2_FLOW_SLOT_SAVE, NULL) == ES2_FLOW_IGNORED);
    CHECK(es2_shell_flow_event(f, ES2_FLOW_SLOT_ACCEPT, " 3. ALPHA") == ES2_FLOW_DONE && f->naming == 0);
    CHECK(logged("save 2 text;"));
    es2_shell_flow_event(f, ES2_FLOW_SLOT_SAVE, NULL);
    CHECK(es2_shell_flow_event(f, ES2_FLOW_SLOT_CANCEL, NULL) == ES2_FLOW_DONE && f->naming == 0 && logged("save 2 text;"));
    CHECK(f->slot == 10);   /* CANCEL ends with 0x437fef(10) */
    host_state.save_ok = 0;
    es2_shell_flow_select_slot(f, 2);
    es2_shell_flow_event(f, ES2_FLOW_SLOT_SAVE, NULL);
    CHECK(es2_shell_flow_event(f, ES2_FLOW_SLOT_ACCEPT, "x") == ES2_FLOW_FAILED);
    /* EXIT with caller 0: main menu */
    CHECK(es2_shell_flow_event(f, ES2_FLOW_SLOT_EXIT, NULL) == ES2_FLOW_DONE && f->screen == ES2_FLOW_MAIN_MENU);
    /* RESTORE of a finished career (final words 0, 1, 3, 9): VSHELL shows the
     * bay without testing the final word (0x4383cd..0x4383d5); no End box, no
     * campaign preparation, the game stays loaded without a campaign */
    {
        const int16_t words[4] = {0, 1, 3, 9};
        for (int i = 0; i < 4; i++) {
            f = start(1, 0);
            es2_shell_flow_event(f, ES2_FLOW_SAVE_RESTORE, NULL);
            es2_shell_flow_select_slot(f, 0);
            host_state.slot_final_word = words[i];
            clear_log();
            CHECK(es2_shell_flow_event(f, ES2_FLOW_SLOT_RESTORE, NULL) == ES2_FLOW_DONE && logged("load 0;save 10;"));
            CHECK(f->box == ES2_FLOW_BOX_NONE && f->screen == ES2_FLOW_SERVICE_BAY && f->bay_word == -1);
            CHECK(f->game == ES2_FLOW_GAME_SLOT && f->final_word == words[i] && f->slot == 10);
            CHECK(es2_shell_flow_event(f, ES2_FLOW_ROCK_AND_ROLL, NULL) == ES2_FLOW_IGNORED);   /* not the mission screen */
            CHECK(es2_shell_flow_finish(f) == 0 && logged("load 0;save 10;save 10;"));
        }
    }
    /* RESTORE selects slot 10 on its way out (the hide 0x43a3f6) */
    f = start(1, 0);
    es2_shell_flow_event(f, ES2_FLOW_SAVE_RESTORE, NULL);
    es2_shell_flow_select_slot(f, 7);
    CHECK(es2_shell_flow_event(f, ES2_FLOW_SLOT_RESTORE, NULL) == ES2_FLOW_DONE && f->slot == 10);
    CHECK(f->game == ES2_FLOW_GAME_CAMPAIGN && f->screen == ES2_FLOW_SERVICE_BAY);
    /* while naming: SAVE, RESTORE and EXIT disabled, ACCEPT and CANCEL enabled;
     * CANCEL and EXIT select slot 10 (0x43863b, 0x43a3f6) */
    f = in_bay(0);
    es2_shell_flow_event(f, ES2_FLOW_BAY_MAIN_MENU, NULL);
    es2_shell_flow_event(f, ES2_FLOW_SAVE_RESTORE, NULL);
    CHECK(!es2_shell_flow_enabled(f, ES2_FLOW_SLOT_ACCEPT, 0) && !es2_shell_flow_enabled(f, ES2_FLOW_SLOT_CANCEL, 0));
    CHECK(es2_shell_flow_event(f, ES2_FLOW_SLOT_CANCEL, NULL) == ES2_FLOW_IGNORED);
    es2_shell_flow_select_slot(f, 6);
    CHECK(es2_shell_flow_event(f, ES2_FLOW_SLOT_SAVE, NULL) == ES2_FLOW_DONE);
    CHECK(!es2_shell_flow_enabled(f, ES2_FLOW_SLOT_RESTORE, 1) && !es2_shell_flow_enabled(f, ES2_FLOW_SLOT_EXIT, 0));
    CHECK(es2_shell_flow_enabled(f, ES2_FLOW_SLOT_ACCEPT, 0) && es2_shell_flow_enabled(f, ES2_FLOW_SLOT_CANCEL, 0));
    clear_log();
    CHECK(es2_shell_flow_event(f, ES2_FLOW_SLOT_RESTORE, NULL) == ES2_FLOW_IGNORED && logged(""));
    CHECK(es2_shell_flow_event(f, ES2_FLOW_SLOT_EXIT, NULL) == ES2_FLOW_IGNORED && f->screen == ES2_FLOW_SAVED_GAMES);
    CHECK(es2_shell_flow_event(f, ES2_FLOW_SLOT_CANCEL, NULL) == ES2_FLOW_DONE && f->slot == 10 && f->naming == 0);
    CHECK(es2_shell_flow_enabled(f, ES2_FLOW_SLOT_EXIT, 0));
    es2_shell_flow_select_slot(f, 6);
    es2_shell_flow_event(f, ES2_FLOW_SLOT_SAVE, NULL);
    CHECK(es2_shell_flow_event(f, ES2_FLOW_SLOT_ACCEPT, " 7. B") == ES2_FLOW_DONE && f->slot == 6 && logged("save 6 text;"));
    CHECK(es2_shell_flow_event(f, ES2_FLOW_SLOT_EXIT, NULL) == ES2_FLOW_DONE && f->slot == 10);
    CHECK(f->screen == ES2_FLOW_MAIN_MENU);
    /* RESTORE whose slot cannot be loaded: refused, the screen stays */
    f = start(1, 0);
    es2_shell_flow_event(f, ES2_FLOW_SAVE_RESTORE, NULL);
    es2_shell_flow_select_slot(f, 5);
    host_state.load_answer = 0;
    CHECK(es2_shell_flow_event(f, ES2_FLOW_SLOT_RESTORE, NULL) == ES2_FLOW_REFUSED && f->refusal == ES2_FLOW_NO_SLOT_GAME);
    CHECK(f->screen == ES2_FLOW_SAVED_GAMES && f->game == ES2_FLOW_GAME_NONE);
    /* RESTORE whose campaign cannot be prepared: failed, main menu */
    f = start(1, 0);
    es2_shell_flow_event(f, ES2_FLOW_SAVE_RESTORE, NULL);
    es2_shell_flow_select_slot(f, 5);
    host_state.campaign_ok = 0;
    CHECK(es2_shell_flow_event(f, ES2_FLOW_SLOT_RESTORE, NULL) == ES2_FLOW_FAILED && f->screen == ES2_FLOW_MAIN_MENU);
    /* the mode store fails: the screen is not shown */
    f = start(0, 0);
    host_state.store_ok = 0;
    CHECK(es2_shell_flow_event(f, ES2_FLOW_SAVE_RESTORE, NULL) == ES2_FLOW_FAILED && f->screen == ES2_FLOW_MAIN_MENU);
    /* EXIT with another caller mark: nothing (0x438424) */
    f = start(1, 0);
    es2_shell_flow_event(f, ES2_FLOW_SAVE_RESTORE, NULL);
    f->caller = 3;
    CHECK(es2_shell_flow_event(f, ES2_FLOW_SLOT_EXIT, NULL) == ES2_FLOW_DONE && f->screen == ES2_FLOW_SAVED_GAMES);
}

static void service_bay(void) {
    es2_shell_flow *f = in_bay(1);
    /* WEAPONS: no leave from -1; again: nothing; SAVE leaves WEAPONS (0x412a37
     * with the build byte), caller 8, no mode store */
    CHECK(es2_shell_flow_event(f, ES2_FLOW_BAY_WEAPONS, NULL) == ES2_FLOW_DONE && f->bay_word == 2 && logged(""));
    CHECK(es2_shell_flow_event(f, ES2_FLOW_BAY_WEAPONS, NULL) == ES2_FLOW_IGNORED && logged(""));
    CHECK(es2_shell_flow_event(f, ES2_FLOW_BAY_SAVE, NULL) == ES2_FLOW_DONE && logged("rebalance 1;"));
    CHECK(f->bay_word == 1 && f->caller == 8 && f->screen == ES2_FLOW_SAVED_GAMES);
    /* EXIT with caller 8: the bay show, word -1 */
    CHECK(es2_shell_flow_event(f, ES2_FLOW_SLOT_EXIT, NULL) == ES2_FLOW_DONE);
    CHECK(f->screen == ES2_FLOW_SERVICE_BAY && f->bay_word == -1);
    /* REPAIR and BUILD leave through 0x412a37 too; ARMORY, CREW, MISSION and SAVE do not */
    const struct { es2_shell_flow_handler button; int word; int rebalances; } steps[] = {
        {ES2_FLOW_BAY_REPAIR, 3, 0}, {ES2_FLOW_BAY_BUILD, 4, 1}, {ES2_FLOW_BAY_ARMORY, 5, 1}, {ES2_FLOW_BAY_CREW, 6, 0},
        {ES2_FLOW_BAY_MISSION, 7, 0}, {ES2_FLOW_BAY_WEAPONS, 2, 0}, {ES2_FLOW_BAY_MISSION, 7, 1}};
    for (size_t i = 0; i < sizeof steps / sizeof steps[0]; i++) {
        clear_log();
        CHECK(es2_shell_flow_event(f, steps[i].button, NULL) == ES2_FLOW_DONE && f->bay_word == steps[i].word);
        CHECK(logged(steps[i].rebalances ? "rebalance 1;" : ""));
    }
    /* Rock & Roll needs the mission screen */
    clear_log();
    f->bay_word = 2;
    CHECK(es2_shell_flow_event(f, ES2_FLOW_ROCK_AND_ROLL, NULL) == ES2_FLOW_IGNORED && logged(""));
    f->bay_word = 7;
    /* MAIN MENU: leave, word 0, save slot 10, main menu */
    es2_shell_flow_event(f, ES2_FLOW_BAY_WEAPONS, NULL);
    clear_log();
    CHECK(es2_shell_flow_event(f, ES2_FLOW_BAY_MAIN_MENU, NULL) == ES2_FLOW_DONE);
    CHECK(logged("rebalance 1;save 10;") && f->bay_word == 0 && f->screen == ES2_FLOW_MAIN_MENU);
    /* bay buttons on another screen, and MAIN MENU while the word is 0 */
    CHECK(es2_shell_flow_event(f, ES2_FLOW_BAY_WEAPONS, NULL) == ES2_FLOW_IGNORED);
    f->screen = ES2_FLOW_SERVICE_BAY;
    CHECK(es2_shell_flow_event(f, ES2_FLOW_BAY_MAIN_MENU, NULL) == ES2_FLOW_IGNORED);
    /* mode 0 enables WEAPONS and CREW only (0x43b758) */
    f->mode = 0;
    CHECK(es2_shell_flow_enabled(f, ES2_FLOW_BAY_WEAPONS, 0) && es2_shell_flow_enabled(f, ES2_FLOW_BAY_CREW, 0));
    /* 0x43b758 sets only the five editor buttons: MAIN MENU, SAVE, MISSION stay enabled */
    CHECK(es2_shell_flow_enabled(f, ES2_FLOW_BAY_MAIN_MENU, 0) && es2_shell_flow_enabled(f, ES2_FLOW_BAY_SAVE, 0));
    CHECK(es2_shell_flow_enabled(f, ES2_FLOW_BAY_MISSION, 0) && !es2_shell_flow_enabled(f, ES2_FLOW_BAY_REPAIR, 0));
    CHECK(!es2_shell_flow_enabled(f, ES2_FLOW_BAY_BUILD, 0) && !es2_shell_flow_enabled(f, ES2_FLOW_BAY_ARMORY, 0));
    CHECK(es2_shell_flow_event(f, ES2_FLOW_BAY_REPAIR, NULL) == ES2_FLOW_IGNORED && f->bay_word == 0);
    CHECK(es2_shell_flow_event(f, ES2_FLOW_BAY_CREW, NULL) == ES2_FLOW_DONE && f->bay_word == 6);
}

static void rock_and_roll(void) {
    es2_shell_flow *f = in_bay(0);
    es2_shell_flow_event(f, ES2_FLOW_BAY_MISSION, NULL);
    /* a refused rule: the warning box with its index, the bay stays */
    host_state.launch_ok = 0;
    host_state.launch_warning = 4;
    CHECK(es2_shell_flow_event(f, ES2_FLOW_ROCK_AND_ROLL, NULL) == ES2_FLOW_DONE && logged("launch;"));
    CHECK(f->box == ES2_FLOW_BOX_WARNING && f->box_value == 4 && !f->quit);
    CHECK(es2_shell_flow_event(f, ES2_FLOW_ROCK_AND_ROLL, NULL) == ES2_FLOW_IGNORED);   /* modal */
    CHECK(es2_shell_flow_event(f, ES2_FLOW_WARNING_OKAY, NULL) == ES2_FLOW_DONE && f->box == ES2_FLOW_BOX_NONE);
    CHECK(f->screen == ES2_FLOW_SERVICE_BAY && f->bay_word == 7);
    /* a failure without a box (ES2_ERR_OFFSET) */
    host_state.launch_warning = ES2_SHELL_LAUNCH_OK;
    CHECK(es2_shell_flow_event(f, ES2_FLOW_ROCK_AND_ROLL, NULL) == ES2_FLOW_FAILED && f->box == ES2_FLOW_BOX_NONE);
    /* launched: exit 2, quit flag; the exit saves slot 10 */
    host_state.launch_ok = 1;
    clear_log();
    CHECK(es2_shell_flow_event(f, ES2_FLOW_ROCK_AND_ROLL, NULL) == ES2_FLOW_DONE && f->quit && f->exit_code == 2);
    CHECK(es2_shell_flow_finish(f) == 2 && logged("launch;save 10;"));
    /* the text menu's shortcut from the main menu; refused without a campaign
     * */
    f = start(1, 0);
    CHECK(es2_shell_flow_event(f, ES2_FLOW_ROCK_AND_ROLL, NULL) == ES2_FLOW_REFUSED && f->refusal == ES2_FLOW_NO_CAMPAIGN);
    CHECK(logged(""));
    f = in_bay(0);
    es2_shell_flow_event(f, ES2_FLOW_BAY_MAIN_MENU, NULL);
    CHECK(es2_shell_flow_event(f, ES2_FLOW_ROCK_AND_ROLL, NULL) == ES2_FLOW_DONE && f->exit_code == 2);
}

static void preferences(void) {
    es2_shell_flow *f = start(1, 0);
    uint8_t committed;
    CHECK(es2_shell_flow_prefs_control(f, ES2_SHELL_PREFS_MUSIC_TOGGLE) == ES2_FLOW_IGNORED);   /* screen not shown */
    CHECK(es2_shell_flow_event(f, ES2_FLOW_PREFERENCES_BUTTON, NULL) == ES2_FLOW_DONE && f->screen == ES2_FLOW_PREFERENCES);
    CHECK(logged(""));   /* no mode store */
    CHECK(es2_shell_flow_prefs_control(f, ES2_SHELL_PREFS_MUSIC_TOGGLE) == ES2_FLOW_DONE && f->prefs.current[0] == 1);
    CHECK(es2_shell_flow_prefs_control(f, ES2_SHELL_PREFS_BUILD_1) == ES2_FLOW_DONE && f->prefs.current[ES2_SHELL_PREFS_BUILD] == 1);
    /* Full Screen: the Alert box first; only its ACCEPT stores byte 6 */
    CHECK(es2_shell_flow_prefs_control(f, ES2_SHELL_PREFS_FULL_SCREEN) == ES2_FLOW_DONE && f->box == ES2_FLOW_BOX_ALERT);
    CHECK(f->prefs.current[6] == 0);
    CHECK(es2_shell_flow_event(f, ES2_FLOW_PREFS_ACCEPT, NULL) == ES2_FLOW_IGNORED);   /* modal */
    CHECK(es2_shell_flow_prefs_control(f, ES2_SHELL_PREFS_WINDOW) == ES2_FLOW_IGNORED);
    CHECK(es2_shell_flow_event(f, ES2_FLOW_ALERT_ACCEPT, NULL) == ES2_FLOW_DONE && f->box == ES2_FLOW_BOX_NONE);
    CHECK(f->prefs.current[6] == 1);
    /* CANCEL: back to the committed block, nothing written */
    CHECK(es2_shell_flow_event(f, ES2_FLOW_PREFS_CANCEL, NULL) == ES2_FLOW_DONE && f->screen == ES2_FLOW_MAIN_MENU);
    CHECK(f->prefs.current[0] == 0 && f->prefs.current[6] == 0 && f->prefs.current[ES2_SHELL_PREFS_BUILD] == 0 && logged(""));
    /* ACCEPT: the 54 bytes written */
    es2_shell_flow_event(f, ES2_FLOW_PREFERENCES_BUTTON, NULL);
    es2_shell_flow_prefs_control(f, ES2_SHELL_PREFS_REPAIR_2);
    CHECK(es2_shell_flow_event(f, ES2_FLOW_PREFS_ACCEPT, NULL) == ES2_FLOW_DONE && logged("prefs;"));
    committed = f->prefs.committed[ES2_SHELL_PREFS_REPAIR];
    CHECK(host_state.written[ES2_SHELL_PREFS_REPAIR] == 2 && committed == 2 && host_state.written[ES2_SHELL_PREFS_MODE] == 1);
    CHECK(f->screen == ES2_FLOW_MAIN_MENU);
    host_state.write_ok = 0;
    es2_shell_flow_event(f, ES2_FLOW_PREFERENCES_BUTTON, NULL);
    CHECK(es2_shell_flow_event(f, ES2_FLOW_PREFS_ACCEPT, NULL) == ES2_FLOW_FAILED && f->screen == ES2_FLOW_MAIN_MENU);
    /* leaving WEAPONS uses the current block's build byte */
    f = in_bay(0);
    f->prefs.current[ES2_SHELL_PREFS_BUILD] = 1;
    es2_shell_flow_event(f, ES2_FLOW_BAY_WEAPONS, NULL);
    es2_shell_flow_event(f, ES2_FLOW_BAY_MISSION, NULL);
    CHECK(logged("rebalance 1;"));
}

static void practice_quit_demo(void) {
    es2_shell_flow *f = start(1, 0);
    CHECK(es2_shell_flow_event(f, ES2_FLOW_PRACTICE_MISSIONS, NULL) == ES2_FLOW_DONE);
    CHECK(f->mode == 0 && f->screen == ES2_FLOW_PRACTICE && logged(""));
    CHECK(es2_shell_flow_event(f, ES2_FLOW_START_NEW_GAME, NULL) == ES2_FLOW_IGNORED);
    CHECK(es2_shell_flow_event(f, ES2_FLOW_PRACTICE_MAIN_MENU, NULL) == ES2_FLOW_DONE && f->screen == ES2_FLOW_MAIN_MENU);
    /* a practice launch: the mode-0 new game, then DBSIM; the exit saves (slot 11 by the host) */
    es2_shell_flow_event(f, ES2_FLOW_PRACTICE_MISSIONS, NULL);
    es2_shell_flow_game_created(f);
    CHECK(f->game == ES2_FLOW_GAME_SLOT && f->final_word == 2);
    es2_shell_flow_dbsim(f);
    CHECK(f->quit && es2_shell_flow_finish(f) == 2 && logged("save 10;"));
    /* INSTANT ACTION: mode 0, the screen stays */
    f = start(1, 0);
    CHECK(es2_shell_flow_event(f, ES2_FLOW_INSTANT_ACTION, NULL) == ES2_FLOW_DONE && f->mode == 0);
    CHECK(f->screen == ES2_FLOW_MAIN_MENU && !f->quit);
    /* VIEW DEMO: exit 5 */
    CHECK(es2_shell_flow_event(f, ES2_FLOW_VIEW_DEMO, NULL) == ES2_FLOW_DONE && f->quit && f->exit_code == 5);
    CHECK(es2_shell_flow_finish(f) == 5 && logged(""));
    /* QUIT and the Quit box on every screen: exit 0 */
    f = start(1, 0);
    CHECK(es2_shell_flow_event(f, ES2_FLOW_QUIT, NULL) == ES2_FLOW_DONE && f->quit && f->exit_code == 0);
    f = in_bay(0);
    es2_shell_flow_event(f, ES2_FLOW_BAY_WEAPONS, NULL);
    CHECK(es2_shell_flow_event(f, ES2_FLOW_QUIT, NULL) == ES2_FLOW_IGNORED);
    CHECK(es2_shell_flow_event(f, ES2_FLOW_QUIT_ACCEPT, NULL) == ES2_FLOW_DONE && f->quit);
    /* no leave at the exit: the state is saved as it is */
    CHECK(es2_shell_flow_finish(f) == 0 && logged("save 10;"));
    /* unknown handlers do nothing */
    f = start(1, 0);
    CHECK(es2_shell_flow_event(f, (es2_shell_flow_handler)0x401000, NULL) == ES2_FLOW_IGNORED && logged(""));
}

/* ONLINE MANUAL 0x431e1b and the bay's online image 0x43a5bd run the
 * help routine 0x431e79: byte 6 := 0, committed := current, the 54 bytes
 * written, then the manual (the write before the manual). */
static void online_manual(void) {
    es2_shell_flow *f = start(1, 1);
    uint8_t before[ES2_SHELL_PREFS_SIZE];
    /* current differs from committed in several bytes, byte 6 = 1 */
    es2_shell_flow_event(f, ES2_FLOW_PREFERENCES_BUTTON, NULL);
    es2_shell_flow_prefs_control(f, ES2_SHELL_PREFS_MUSIC_TOGGLE);
    es2_shell_flow_prefs_control(f, ES2_SHELL_PREFS_REPAIR_1);
    es2_shell_flow_prefs_control(f, ES2_SHELL_PREFS_FULL_SCREEN);
    es2_shell_flow_event(f, ES2_FLOW_ALERT_ACCEPT, NULL);
    CHECK(f->prefs.current[6] == 1 && f->prefs.committed[6] == 0 && f->prefs.committed[0] == 0);
    es2_shell_flow_event(f, ES2_FLOW_PREFS_CANCEL, NULL);   /* back to the main menu, blocks equal again */
    f->prefs.current[6] = 1;
    f->prefs.current[0] = 1;                                 /* uncommitted bytes */
    memcpy(before, f->prefs.current, sizeof before);
    clear_log();
    CHECK(es2_shell_flow_event(f, ES2_FLOW_ONLINE_MANUAL, NULL) == ES2_FLOW_DONE);
    CHECK(logged("prefs;manual;"));
    CHECK(f->screen == ES2_FLOW_MAIN_MENU && f->box == ES2_FLOW_BOX_NONE && !f->quit);
    CHECK(f->prefs.current[6] == 0 && f->prefs.committed[6] == 0);
    CHECK(f->prefs.current[0] == 1 && f->prefs.committed[0] == 1);    /* 0x40dd6e(0) commits everything */
    CHECK(!memcmp(f->prefs.current, f->prefs.committed, sizeof f->prefs.current));
    CHECK(!memcmp(host_state.written, f->prefs.current, sizeof host_state.written));
    for (size_t i = 0; i < sizeof before; i++)
        if (i != 6) CHECK(host_state.written[i] == before[i]);
    /* a failed write does not stop the routine: the manual is still shown */
    host_state.write_ok = 0;
    clear_log();
    CHECK(es2_shell_flow_event(f, ES2_FLOW_ONLINE_MANUAL, NULL) == ES2_FLOW_FAILED && logged("prefs;manual;"));
    /* without a host callback only the prefs are written */
    {
        es2_shell_flow g;
        es2_shell_flow_host host;
        memset(&host, 0, sizeof host);
        host.ctx = &host_state;
        host.write_prefs = f_write_prefs;
        host_state.write_ok = 1;
        es2_shell_flow_init(&g, &host, NULL);
        g.prefs.current[6] = 1;
        clear_log();
        CHECK(es2_shell_flow_event(&g, ES2_FLOW_ONLINE_MANUAL, NULL) == ES2_FLOW_DONE && logged("prefs;"));
        CHECK(host_state.written[6] == 0);
        es2_shell_flow_init(&g, NULL, NULL);
        CHECK(es2_shell_flow_event(&g, ES2_FLOW_ONLINE_MANUAL, NULL) == ES2_FLOW_DONE);
    }
    /* only the main menu has the button; a box blocks it; the bay has its own */
    f = start(1, 0);
    es2_shell_flow_event(f, ES2_FLOW_PREFERENCES_BUTTON, NULL);
    CHECK(es2_shell_flow_event(f, ES2_FLOW_ONLINE_MANUAL, NULL) == ES2_FLOW_IGNORED && logged(""));
    CHECK(es2_shell_flow_event(f, ES2_FLOW_BAY_ONLINE, NULL) == ES2_FLOW_IGNORED && logged(""));
    es2_shell_flow_prefs_control(f, ES2_SHELL_PREFS_FULL_SCREEN);   /* the Alert box */
    CHECK(es2_shell_flow_event(f, ES2_FLOW_ONLINE_MANUAL, NULL) == ES2_FLOW_IGNORED && logged("") && f->box == ES2_FLOW_BOX_ALERT);
    f = start(1, 0);
    CHECK(es2_shell_flow_event(f, ES2_FLOW_BAY_ONLINE, NULL) == ES2_FLOW_IGNORED && logged(""));   /* not in the bay */
    f = in_bay(0);
    f->prefs.current[6] = 1;
    CHECK(es2_shell_flow_event(f, ES2_FLOW_BAY_ONLINE, NULL) == ES2_FLOW_DONE && logged("prefs;manual;"));
    CHECK(f->screen == ES2_FLOW_SERVICE_BAY && f->bay_word == ES2_FLOW_WORD_BAY && f->prefs.current[6] == 0);
    CHECK(host_state.written[6] == 0 && f->game == ES2_FLOW_GAME_CAMPAIGN);
    CHECK(es2_shell_flow_event(f, ES2_FLOW_ONLINE_MANUAL, NULL) == ES2_FLOW_IGNORED);   /* the main menu's button */
    /* no bay_word change: another bar screen stays */
    es2_shell_flow_event(f, ES2_FLOW_BAY_WEAPONS, NULL);
    clear_log();
    CHECK(es2_shell_flow_event(f, ES2_FLOW_BAY_ONLINE, NULL) == ES2_FLOW_DONE && f->bay_word == ES2_FLOW_WORD_WEAPONS);
    CHECK(logged("prefs;manual;"));   /* no leave, no rebalance */
    /* after the quit flag nothing runs */
    es2_shell_flow_event(f, ES2_FLOW_QUIT_ACCEPT, NULL);
    clear_log();
    CHECK(es2_shell_flow_event(f, ES2_FLOW_BAY_ONLINE, NULL) == ES2_FLOW_IGNORED && logged(""));
}

/* Full Screen 0x437608 shows the Alert box only while windowed
 * ([0x481f4c] = 0, 0x437618..0x437628); in full screen it does nothing. */
static int display_word;
static int f_full_screen(void *ctx) { note(ctx, "display", 0, 0); return display_word; }
static void full_screen_alert(void) {
    es2_shell_flow *f = start(1, 0);
    es2_shell_flow_host h = f->host;
    h.full_screen = f_full_screen;
    f->host = h;
    es2_shell_flow_event(f, ES2_FLOW_PREFERENCES_BUTTON, NULL);
    display_word = 1;
    CHECK(es2_shell_flow_prefs_control(f, ES2_SHELL_PREFS_FULL_SCREEN) == ES2_FLOW_IGNORED);
    CHECK(f->box == ES2_FLOW_BOX_NONE && f->prefs.current[6] == 0 && f->screen == ES2_FLOW_PREFERENCES && logged("display;"));
    CHECK(es2_shell_flow_event(f, ES2_FLOW_ALERT_ACCEPT, NULL) == ES2_FLOW_IGNORED && f->prefs.current[6] == 0);
    /* the other controls do not ask */
    clear_log();
    CHECK(es2_shell_flow_prefs_control(f, ES2_SHELL_PREFS_WINDOW) == ES2_FLOW_DONE && logged(""));
    display_word = 0;
    CHECK(es2_shell_flow_prefs_control(f, ES2_SHELL_PREFS_FULL_SCREEN) == ES2_FLOW_DONE && f->box == ES2_FLOW_BOX_ALERT);
    CHECK(es2_shell_flow_event(f, ES2_FLOW_ALERT_ACCEPT, NULL) == ES2_FLOW_DONE && f->prefs.current[6] == 1);
    /* a screen not shown is not asked */
    es2_shell_flow_event(f, ES2_FLOW_PREFS_CANCEL, NULL);
    clear_log();
    CHECK(es2_shell_flow_prefs_control(f, ES2_SHELL_PREFS_FULL_SCREEN) == ES2_FLOW_IGNORED && logged(""));
}

/* With -@ (debug_cheat) the DEBUG dialog 0x413305 waits at the end
 * of a mode-1 new career (0x41308e) and on advance 2 (0x40f647); the answer
 * prepares (Use Default or the typed name) and shows the bay. */
static void debug_dialog(void) {
    es2_shell_flow *f;
    /* without -@ nothing waits */
    f = in_bay(0);
    CHECK(f->debug == ES2_FLOW_DEBUG_NONE && f->screen == ES2_FLOW_SERVICE_BAY);
    CHECK(es2_shell_flow_debug_answer(f, NULL) == ES2_FLOW_IGNORED && logged(""));
    /* new career: the game exists (final word 2, page 0), no bay, no preparation */
    f = start(1, 0);
    f->debug_cheat = 1;
    es2_shell_flow_event(f, ES2_FLOW_START_NEW_GAME, NULL);
    clear_log();
    CHECK(es2_shell_flow_event(f, ES2_FLOW_REGISTRATION_ACCEPT, "ACE") == ES2_FLOW_DONE);
    CHECK(logged("new 0 3;") && f->debug == ES2_FLOW_DEBUG_CAREER && f->screen == ES2_FLOW_MAIN_MENU);
    CHECK(f->game == ES2_FLOW_GAME_CAMPAIGN && f->final_word == 2 && f->page == 0 && f->bay_select == 0);
    CHECK(es2_shell_flow_debug_answer(f, NULL) == ES2_FLOW_DONE && logged("new 0 3;debug default;"));
    CHECK(f->debug == ES2_FLOW_DEBUG_NONE && f->screen == ES2_FLOW_SERVICE_BAY && f->bay_word == -1);
    CHECK(es2_shell_flow_debug_answer(f, NULL) == ES2_FLOW_IGNORED);
    /* ACCEPT with a typed name; mode 1 never launches */
    f = start(1, 0);
    f->debug_cheat = 1;
    es2_shell_flow_event(f, ES2_FLOW_START_NEW_GAME, NULL);
    es2_shell_flow_event(f, ES2_FLOW_REGISTRATION_ACCEPT, "ACE");
    clear_log();
    CHECK(es2_shell_flow_debug_answer(f, "TRAIN8") == ES2_FLOW_DONE && logged("debug typed 6;"));
    CHECK(f->screen == ES2_FLOW_SERVICE_BAY && !f->quit && f->exit_code == 0);
    /* a refused name or failed preparation: main menu, the game stays */
    f = start(1, 0);
    f->debug_cheat = 1;
    es2_shell_flow_event(f, ES2_FLOW_START_NEW_GAME, NULL);
    es2_shell_flow_event(f, ES2_FLOW_REGISTRATION_ACCEPT, "ACE");
    host_state.prepare_ok = 0;
    CHECK(es2_shell_flow_debug_answer(f, "X") == ES2_FLOW_FAILED && f->screen == ES2_FLOW_MAIN_MENU);
    CHECK(f->debug == ES2_FLOW_DEBUG_NONE && f->game == ES2_FLOW_GAME_CAMPAIGN && f->outcome == ES2_FLOW_OUTCOME_NONE);
    /* a failed new game: no dialog */
    f = start(1, 0);
    f->debug_cheat = 1;
    host_state.new_game_answer = 0;
    es2_shell_flow_event(f, ES2_FLOW_START_NEW_GAME, NULL);
    CHECK(es2_shell_flow_event(f, ES2_FLOW_REGISTRATION_ACCEPT, "ACE") == ES2_FLOW_FAILED && f->debug == ES2_FLOW_DEBUG_NONE);
    /* advance 2: page 4, outcome NEXT, the dialog waits, then the bay */
    f = start(1, 0);
    f->debug_cheat = 1;
    host_state.results = (es2_shell_flow_results){ES2_SHELL_RESULTS_ADVANCED, 2, 2};
    es2_shell_flow_start(f, 4);
    CHECK(logged("load 10;results;") && f->debug == ES2_FLOW_DEBUG_NEXT && f->outcome == ES2_FLOW_OUTCOME_NEXT);
    CHECK(f->screen == ES2_FLOW_MAIN_MENU && f->page == 4 && f->final_word == 2);
    CHECK(es2_shell_flow_debug_answer(f, "C1_02") == ES2_FLOW_DONE && logged("load 10;results;debug typed 5;"));
    CHECK(f->screen == ES2_FLOW_SERVICE_BAY && f->outcome == ES2_FLOW_OUTCOME_NEXT && f->page == 4);
    /* advance 2, the answer fails: NEXT_FAILED */
    f = start(1, 0);
    f->debug_cheat = 1;
    host_state.results = (es2_shell_flow_results){ES2_SHELL_RESULTS_ADVANCED, 2, 2};
    es2_shell_flow_start(f, 3);
    host_state.prepare_ok = 0;
    CHECK(es2_shell_flow_debug_answer(f, NULL) == ES2_FLOW_FAILED && f->outcome == ES2_FLOW_OUTCOME_NEXT_FAILED);
    CHECK(f->screen == ES2_FLOW_MAIN_MENU);
    /* other endings do not show the dialog */
    f = start(1, 0);
    f->debug_cheat = 1;
    host_state.results = (es2_shell_flow_results){ES2_SHELL_RESULTS_ADVANCED, 1, 1};
    es2_shell_flow_start(f, 3);
    CHECK(f->debug == ES2_FLOW_DEBUG_NONE && f->outcome == ES2_FLOW_OUTCOME_WON);
}

int main(void) {
    debug_dialog();
    full_screen_alert();
    online_manual();
    init_and_entry();
    bay_selection();
    results_path();
    replay_box();
    new_career();
    continue_game();
    saved_games();
    service_bay();
    rock_and_roll();
    preferences();
    practice_quit_demo();
    printf("flow_test: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
