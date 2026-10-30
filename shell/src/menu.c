/* es2menu.exe: a first own shell screen, text mode.
 *
 * Started by our launcher instead of VSHELL (ES2_LAUNCH_SHELL, see
 * launcher/src/es2launch.c), with the VSHELL argv appended. It shows a main
 * menu in the console (labels from the installation's ESTEXT.BIN), keeps a
 * campaign (through engine_io.h in this process), and answers the launcher
 * like VSHELL: 2 = start DBSIM with the prepared mission, 0 = quit. Every
 * failure answers 0.
 *
 * The Saved games screen (SAVE, RESTORE, EXIT over the ten user slots)
 * writes every slot the way VSHELL's save 0x40e933 does (es2/shell/slots.h).
 *
 * The service bay (MAIN MENU, SAVE, WEAPONS, MISSION) is shown after a new
 * career, CONTINUE, RESTORE and a continuing results path, with its WEAPONS
 * screen (es2/shell/hangar.h).
 *
 * Rock & Roll's launch checks with the warning box
 * (es2_shell_launch_check), the Replay box after a lost mission (Yes: slot 10
 * loaded, exit 2, DBSIM keeps its mission.var; No: slot 10 saved), the End of
 * game box from the final word in memory, and the whole Preferences screen
 * over VSHELL's two prefs blocks (es2_shell_prefs_session: music, sound
 * effects, resolution, display, repair, weapons building; Cancel reverts
 * every byte stored since the last Accept) are shown as VSHELL shows them.
 *
 * VSHELL's state is kept in this process (engine_io.h over
 * es2/shell/engine.h): the memory SAV image, slot 10 (11 in mode 0) at every
 * exit with a loaded game, the launch files in DATA. The
 * campaign directory is an optional evidence log (menu.ini chain_dir,
 * ES2_MENU_CHAIN_DIR).
 *
 * The flow (which screen and box is shown, the screen word, the
 * handlers' effects, the boxes' decisions, the -X dispatch and the exit
 * save) is es2/shell/flow.h. This file is its text presenter: it shows
 * flow.screen and flow.box, turns the numbered choices into the flow's
 * events (VSHELL's handlers), and is the flow's host (the file layer of
 * every effect).
 *
 * Contract, screen map and acceptance:
 * docs/reverse-engineering/launcher.md, sections "Own menu" and "Shell
 * state engine". */
#include "host.h"
#include "menu.h"
#include "es2/formats/cfg.h"
#include "es2/formats/sav.h"
#include "es2/formats/vfs.h"
#include "es2/formats/vol.h"
#include "es2/shell/campaign.h"
#include "es2/shell/flow.h"
#include "es2/shell/save.h"
#include "es2/shell/slots.h"
#include "es2/shell/movies.h"
#include "engine_io.h"
#include "win/help.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { EXIT_QUIT = ES2_FLOW_EXIT_QUIT, EXIT_DBSIM = ES2_FLOW_EXIT_DBSIM };
enum { SLOT_CAMPAIGN = ES2_FLOW_SLOT_CAMPAIGN, USER_SLOTS = ES2_FLOW_USER_SLOTS };   /* save index 10 (11 in mode 0), slots 0..9 */
enum { GAME_NONE = ES2_FLOW_GAME_NONE, GAME_CAMPAIGN = ES2_FLOW_GAME_CAMPAIGN, GAME_SLOT = ES2_FLOW_GAME_SLOT };

typedef struct menu {
    es2_host h;                  /* configuration, flow, engine, slots, movies (host.h) */
    es2_os_stream *log;
    char *script;              /* --input file contents */
    size_t script_size, script_at;
} menu;

/* ---- files ---------------------------------------------------------------- */
#define read_all es2_host_read
#define write_all es2_host_write
static void join(char *out, size_t capacity, const char *a, const char *b) {
    snprintf(out, capacity, "%s\\%s", a, b);
}

/* ---- output and input ------------------------------------------------------ */
static void say(menu *m, const char *format, ...) {
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    fflush(stdout);
    if (m->log) {
        va_start(args, format);
        es2_os_vfprintf(m->log, format, args);
        va_end(args);
        es2_os_fflush(m->log);
    }
}
static void say_span(menu *m, es2_span text) {
    fwrite(text.data, 1, text.size, stdout);
    fflush(stdout);
    if (m->log) { es2_os_fwrite(text.data, 1, text.size, m->log); es2_os_fflush(m->log); }
}

/* The scripted input (--input) is shared by every start of the menu in one
 * launcher run: the consumed byte count is kept in <menu>/input.pos. */
static void script_open(menu *m) {
    char path[ES2_OS_MAX_PATH];
    size_t n;
    uint8_t *pos;
    m->script = (char *)read_all(m->h.args.input, &m->script_size);
    join(path, sizeof path, m->h.dir, "input.pos");
    pos = read_all(path, &n);
    if (pos) { m->script_at = strtoul((char *)pos, NULL, 10); free(pos); }
    if (m->script_at > m->script_size) m->script_at = m->script_size;
}
static void script_save(menu *m) {
    char path[ES2_OS_MAX_PATH], text[32];
    join(path, sizeof path, m->h.dir, "input.pos");
    snprintf(text, sizeof text, "%zu\n", m->script_at);
    write_all(path, (const uint8_t *)text, strlen(text));
}
/* One line without its end. 0 at the end of the input (counts as QUIT). */
static int ask(menu *m, const char *prompt, char *line, size_t capacity) {
    size_t n = 0;
    say(m, "%s> ", prompt);
    if (m->h.args.input) {
        if (!m->script || m->script_at >= m->script_size) { say(m, "\n"); return 0; }
        while (m->script_at < m->script_size && m->script[m->script_at] != '\n') {
            if (n + 1 < capacity && m->script[m->script_at] != '\r') line[n++] = m->script[m->script_at];
            m->script_at++;
        }
        if (m->script_at < m->script_size) m->script_at++;
        line[n] = 0;
        script_save(m);
        say(m, "%s\n", line);
        return 1;
    }
    if (!fgets(line, (int)capacity, stdin)) { say(m, "\n"); return 0; }
    n = strlen(line);
    while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
    if (m->log) { es2_os_fprintf(m->log, "%s\n", line); es2_os_fflush(m->log); }
    return 1;
}
static int ask_number(menu *m, const char *prompt, int *value) {
    char line[64], *end;
    long v;
    if (!ask(m, prompt, line, sizeof line)) return 0;
    v = strtol(line, &end, 10);
    *value = (end == line || *end) ? -1 : (int)v;
    return 1;
}

/* ---- labels: ESTEXT.BIN by string ID (vshell_ui.md); own words otherwise -- */
static void label_in(menu *m, es2_span catalogue, unsigned id, const char *fallback) {
    es2_span text;
    es2_error e;
    if (catalogue.data && es2m_bin_string(catalogue, id, &text, &e) == ES2_OK && text.size) say_span(m, text);
    else say(m, "%s", fallback);
}
static void label(menu *m, unsigned id, const char *fallback) {
    label_in(m, m->h.estext, id, fallback);
}

/* String IDs of the VSHELL screens (vshell_ui.md sections 4.1-4.10). */
enum {
    T_MAIN_MENU = 0x02, T_NEW_GAME = 0x04, T_CONTINUE = 0x05, T_SAVE_RESTORE = 0x06, T_PREFERENCES = 0x0a,
    T_QUIT = 0x0e, T_SAVE = 0x1d, T_RESTORE = 0x1e, T_EXIT = 0x1f, T_SAVED_GAMES = 0x22,
    T_NAME = 0x24, T_SKILL_STAT = 0x25, T_SALVAGE = 0x2c, T_MISSION = 0x2d, T_SECTOR = 0x2e,
    T_REGISTRATION = 0x30, T_ENTER_NAME = 0x31,
    T_SKILL = 0x32, T_CANCEL = 0x33, T_ACCEPT = 0x34, T_ROOKIE = 0x35, T_ROCK_AND_ROLL = 0xb8,
    T_REPORT = 0xb9, T_PREFS_TITLE = 0x103, T_REPAIR_TITLE = 0x107, T_BUILD_TITLE = 0x108,
    T_BUILD_0 = 0x111, T_REPAIR_0 = 0x113, T_DISPLAY = 0x123, T_WINDOW = 0x124, T_END_OF_GAME = 0x12e,
    T_KILLED = 0x12f, T_WAR_LOST = 0x130, T_DEFEATED = 0x131, T_RESTORE_OR_NEW = 0x132,
    /* Service bay (vshell_ui.md 4.6, 4.7) */
    T_SERVICE_BAY = 0x0c, T_BAY_MAIN_MENU = 0x13, T_BAY_SAVE = 0x14, T_WEAPONS = 0x15, T_BAY_MISSION = 0x1a,
    T_HERC_NAME = 0x6e,        /* + vehicle (0x411480) */
    /* Boxes and the rest of Preferences (vshell_ui.md 4.5, 4.10, 11) */
    T_AUDIO = 0x106, T_MUSIC = 0x116, T_SFX = 0x117, T_RESOLUTION = 0x120, T_HIGH_RES = 0x121, T_LOW_RES = 0x122,
    T_ALERT = 0x126, T_ALERT_TEXT = 0x127, T_ALERT_ACCEPT = 0x10, T_OKAY = 0x133,
    T_REPLAY = 0x134, T_REPLAY_KILLED = 0x135, T_REPLAY_LOST = 0x137, T_YES = 0x139, T_NO = 0x13a,
    W_ROW = 3,                /* WPN_DESC.BIN: row text of item i = entry 3 * i (0x44009c) */
    W_GUIDANCE = 0x63          /* WPN_DESC.BIN: mode m = entry 0x63 + 3 * m (0x440547) */
};
static const char *skill_words[4] = {"skill 0", "skill 1", "skill 2", "skill 3"};
static const char *repair_words[3] = {"repair: automatic", "repair: own Herc by hand", "repair: by hand"};
static const char *build_words[2] = {"weapons: automatic", "weapons: by hand"};

/* ---- engine --------------------------------------------------------- */
/* The engine's failure on the screen. */
static void engine_error(menu *m) {
    es2_host_engine_error(&m->h);
}

/* ---- campaign status ------------------------------------------------------ */
typedef struct status {
    int campaign, finished, prepared;
    char save[ES2_OS_MAX_PATH], mission[40], reason[64];
    es2m_save_info info;
} status;
/* The campaign is the engine's memory state (a loaded game with final
 * word 2); its prepared mission is the CAREER position's. */
static int query(menu *m, status *st) {
    es2_error e;
    memset(st, 0, sizeof *st);
    if (m->h.flow.game != GAME_CAMPAIGN || !m->h.io.engine.memory.data ||
        es2m_save_info_read((es2_span){m->h.io.engine.memory.data, m->h.io.engine.memory.size}, &st->info, &e) != ES2_OK)
        return 1;
    st->campaign = st->prepared = 1;
    eio_mission_name(&m->h.io, st->mission, sizeof st->mission);
    return 1;
}
static void show_status(menu *m, const status *st) {
    if (m->h.flow.game != GAME_CAMPAIGN || !st->campaign) return;
    say(m, "  ");
    label(m, T_NAME, "Name:");
    say(m, " %s   ", st->info.name);
    label(m, T_SALVAGE, "Salvage:");
    say(m, " %lu   ", (unsigned long)st->info.salvage);
    label(m, T_MISSION, "Mission:");
    say(m, " %s\n", st->prepared ? st->mission : "-");
}
/* The End of game box 0x44d2bc as CONTINUE shows it (0x44d55f): its title,
 * the line for the final word (0 0x130, 1 0x131, 3 0x12f; any other value
 * sets no text: VSHELL keeps whatever the label held, here nothing), the
 * fixed line 0x132 and OKAY (0x44d60b only hides the box). */
static void end_text(menu *m, int final_word) {
    say(m, "\n== ");
    label(m, T_END_OF_GAME, "End of the campaign");
    say(m, " ==\n  ");
    if (final_word == 3) label(m, T_KILLED, "the player was killed.");
    else if (final_word == 0) label(m, T_WAR_LOST, "the campaign was lost.");
    else if (final_word == 1) label(m, T_DEFEATED, "the campaign was won.");
    say(m, "\n  ");
    label(m, T_RESTORE_OR_NEW, "Restore or start a new game.");
    say(m, "\n  [");
    label(m, T_OKAY, "OKAY");
    say(m, "]\n");
}
/* The two-line boxes: the launch warning 0x44d66c (title 0x13b,
 * lines 0x13c + 2n and 0x13d + 2n, OKAY) and the Replay box 0x44cdac. */
static void box_lines(menu *m, unsigned title, unsigned first, const char *fallback) {
    say(m, "\n== ");
    label(m, title, "!");
    say(m, " ==\n  ");
    label(m, first, fallback);
    say(m, "\n  ");
    label(m, first + 1, "");
    say(m, "\n");
}

/* ---- save slots (vshell_ui.md 4.3, es2/shell/slots.h): host.c ------------- */
static int slot_table(menu *m, es2_shell_slot_table *t) {
    return es2_host_slot_table(&m->h, t);
}

/* ---- screens -------------------------------------------------------------- */
/* VSHELL's edit-field rules for typed names (definitions at the end). */
static es2_status s27_slot_row(menu *m, int slot, const char *typed, char *row, es2_error *e);
static int s27_registration_name(menu *m, const char *name);

/* The Registration screen (0x43bd2e) after START NEW GAME: the name field,
 * the skill (SKILL 0x43c6ad steps the flow's 0x476248 to the chosen level),
 * ACCEPT 0x43c78b or CANCEL 0x43c728. Any refusal of the text menu is a
 * CANCEL (no write). */
static void registration(menu *m) {
    char name[64];
    int skill, choice, i;
    label(m, T_ENTER_NAME, "Pilot name");
    say(m, "\n");
    if (!ask(m, "name", name, sizeof name)) goto cancel;
    size_t n = strlen(name);
    int valid = n >= 1 && n <= 29;
    for (i = 0; valid && name[i]; i++) valid = name[i] >= 0x20 && name[i] < 0x7f && name[i] != '"';
    if (!valid) { say(m, "! the name needs 1..29 printable characters\n"); goto cancel; }
    if (!s27_registration_name(m, name)) goto cancel;
    label(m, T_SKILL, "Skill");
    say(m, "\n");
    for (i = 0; i < 4; i++) { say(m, "  %d  ", i + 1); label(m, T_ROOKIE + (unsigned)i, skill_words[i]); say(m, "\n"); }
    if (!ask_number(m, "skill", &skill)) goto cancel;
    if (skill < 1 || skill > 4) { say(m, "! no such skill level\n"); goto cancel; }
    say(m, "  1  "); label(m, T_ACCEPT, "Accept"); say(m, "\n  2  "); label(m, T_CANCEL, "Cancel"); say(m, "\n");
    if (!ask_number(m, "choice", &choice) || choice != 1) goto cancel;
    while (m->h.flow.skill != skill - 1) es2_shell_flow_event(&m->h.flow, ES2_FLOW_SKILL, NULL);
    es2_shell_flow_event(&m->h.flow, ES2_FLOW_REGISTRATION_ACCEPT, name);   /* the host reports a failure */
    return;
cancel:
    es2_shell_flow_event(&m->h.flow, ES2_FLOW_REGISTRATION_CANCEL, NULL);
}

static void saved_games_statistics(menu *m, const es2_shell_slot_table *t, int slot);   /* end of file */

/* Saved games (0x438c40), from SAVE/RESTORE 0x431b27 (mode 1) or the bay's
 * SAVE 0x43a768: the ten user slots as their rows, statistics of every used
 * slot; SAVE (only with a loaded game) names a slot and saves it on ACCEPT,
 * CANCEL writes nothing; RESTORE (only a used slot) loads it; EXIT returns to
 * the caller. VSHELL has no delete. One screen per call. */
static void saved_games(menu *m) {
    es2_shell_flow *f = &m->h.flow;
    es2_shell_slot_table t;
    char typed[96], row[ES2_SHELL_SLOT_TEXT];   /* the row may take up to 75 narrow characters */
    es2_error e;
    int choice, slot;
    say(m, "\n== ");
    label(m, T_SAVED_GAMES, "Saved games");
    say(m, " ==\n");
    if (!slot_table(m, &t)) goto exit;
    for (int i = 0; i < USER_SLOTS; i++) {
        say(m, "  %s\n", t.slot[i].text);
        if (t.slot[i].flag) saved_games_statistics(m, &t, i);
    }
    say(m, "  1  "); label(m, T_SAVE, "Save");
    say(m, "\n  2  "); label(m, T_RESTORE, "Restore");
    say(m, "\n  3  "); label(m, T_EXIT, "Exit");
    say(m, "\n");
    if (!ask_number(m, "choice", &choice) || choice == 3) goto exit;
    if (choice != 1 && choice != 2) { say(m, "? 1..3\n"); return; }
    if (choice == 1 && f->game == GAME_NONE) { say(m, "! no game loaded\n"); return; }
    if (!ask_number(m, "slot", &slot)) goto exit;
    if (slot < 1 || slot > USER_SLOTS) { say(m, "? 1..%d\n", USER_SLOTS); return; }
    es2_shell_flow_select_slot(f, slot - 1);   /* the row 0x437fef */
    if (choice == 2) {
        if (!es2_shell_flow_enabled(f, ES2_FLOW_SLOT_RESTORE, t.slot[slot - 1].flag)) {
            say(m, "! slot %d holds no game\n", slot);
            return;
        }
        es2_shell_flow_result restored = es2_shell_flow_event(f, ES2_FLOW_SLOT_RESTORE, NULL);
        if (restored == ES2_FLOW_REFUSED) {
            /* the slot file could not be loaded: the text menu leaves the screen */
            say(m, "! slot %d holds no game\n", slot);
            goto exit;
        }
        if (restored == ES2_FLOW_DONE) m->h.movies.group_shown = 0;   /* 0x4383c6 */
        return;
    }
    if (es2_shell_flow_event(f, ES2_FLOW_SLOT_SAVE, NULL) != ES2_FLOW_DONE) return;
    label(m, T_NAME, "Name:");
    say(m, "\n");
    if (!ask(m, "name", typed, sizeof typed)) goto cancel_exit;
    if (s27_slot_row(m, slot - 1, typed, row, &e) != ES2_OK) {   /* width rule */
        es2_shell_flow_event(f, ES2_FLOW_SLOT_CANCEL, NULL);
        return;
    }
    say(m, "  %s\n  1  ", row);
    label(m, T_ACCEPT, "Accept");
    say(m, "\n  2  ");
    label(m, T_CANCEL, "Cancel");
    say(m, "\n");
    if (!ask_number(m, "choice", &choice)) goto cancel_exit;
    es2_shell_flow_event(f, choice == 1 ? ES2_FLOW_SLOT_ACCEPT : ES2_FLOW_SLOT_CANCEL, row);
    return;
cancel_exit:
    es2_shell_flow_event(f, ES2_FLOW_SLOT_CANCEL, NULL);
exit:
    es2_shell_flow_event(f, ES2_FLOW_SLOT_EXIT, NULL);
}

/* Rock & Roll (0x445b99) on the state VSHELL holds (the engine's memory
 * state): a failed rule shows the warning box (the flow's box), otherwise
 * the mission goes to DBSIM (exit code 2). */
static void start_mission(menu *m) {
    status st;
    if (m->h.flow.game != GAME_CAMPAIGN || !query(m, &st) || !st.campaign) { say(m, "! no campaign loaded\n"); return; }
    es2_shell_flow_event(&m->h.flow, ES2_FLOW_ROCK_AND_ROLL, NULL);
    if (m->h.flow.quit) say(m, "[DBSIM: %s]\n", st.mission);
}

/* ---- service bay (vshell_ui.md 4.6, 4.7, section 10) ---------------------- */
/* Leaving the current screen (0x43a537) is the flow's (host_rebalance). */
typedef struct bay_view {
    int present[8], vehicle[8], capacity[8], item[8][10], secondary[8][10], player[8];
    char pilot[8][32];
    int row_item[27], row_count[27], row_enabled[27], rows;
} bay_view;
/* The engine's view of the bays (with bay and slot: also the weapon list). */
static int bay_query(menu *m, int bay, int slot, bay_view *v) {
    es2_shell_bay_view w;
    es2_error e;
    memset(v, 0, sizeof *v);
    if (!eio_open(&m->h.io)) { engine_error(m); return 0; }
    if (es2_shell_engine_bay_view(&m->h.io.engine, bay, slot, &w, &e) != ES2_OK) {
        say(m, "! the service bay could not be read (%s)\n", es2_status_string(e.code));
        return 0;
    }
    for (int b = 0; b < 8; b++) {
        if (!w.present[b] || w.capacity[b] > 10) continue;
        v->present[b] = 1;
        v->vehicle[b] = w.vehicle[b];
        v->capacity[b] = w.capacity[b];
        v->player[b] = w.pilot[b] == 36;
        memcpy(v->pilot[b], w.name[b], sizeof v->pilot[b]);
        for (int s = 0; s < 10; s++) { v->item[b][s] = w.item[b][s]; v->secondary[b][s] = w.secondary[b][s]; }
    }
    for (int r = 0; r < w.rows; r++) {
        v->row_item[r] = w.row_item[r];
        v->row_count[r] = w.row_count[r];
        v->row_enabled[r] = w.row_enabled[r];
    }
    v->rows = w.rows;
    return 1;
}
static void item_name(menu *m, int item) {
    char fallback[16];
    snprintf(fallback, sizeof fallback, "item %d", item);
    label_in(m, m->h.wpn_desc, W_ROW * (unsigned)(item < 0 ? 0 : item), fallback);
}
static void show_bay(menu *m, const bay_view *v, int bay) {
    char fallback[16];
    for (int b = 0; b < 8; b++) {
        if (!v->present[b]) continue;
        say(m, " %c%d  ", b == bay ? '*' : ' ', b + 1);
        snprintf(fallback, sizeof fallback, "Herc %d", v->vehicle[b]);
        label(m, T_HERC_NAME + (unsigned)v->vehicle[b], fallback);
        say(m, "  %s\n", v->pilot[b][0] ? v->pilot[b] : "-");
    }
    if (bay < 0) return;
    for (int s = 0; s < v->capacity[bay]; s++) {
        say(m, "      %d  ", s + 1);
        item_name(m, v->item[bay][s]);
        if (v->item[bay][s] >= 13 && v->item[bay][s] <= 16 && v->secondary[bay][s] < 4) {
            say(m, "  [");
            label_in(m, m->h.wpn_desc, W_GUIDANCE + 3u * (unsigned)v->secondary[bay][s], "guidance");
            say(m, "]");
        }
        say(m, "\n");
    }
}

/* WEAPONS (0x43ebbc, shown by 0x43fbd8): choose a bay (squad panel, not
 * serialized), a slot and a row of the weapon list; a click mounts as
 * 0x43fdac decides; missile racks get a guidance mode. Back returns to the
 * bay with the screen still WEAPONS (0x4758b8 = 2): the leave runs when the
 * next bay button is chosen. */
static void weapons(menu *m) {
    /* the bay selection 0x482bc9 is kept in the flow across visits; 0 after
     * a new game or a load */
    int bay = m->h.flow.bay_select, choice, slot, value;
    for (;;) {
        bay_view v;
        if (!bay_query(m, -1, -1, &v)) return;
        if (bay < 0 || bay > 7 || !v.present[bay]) {
            bay = -1;
            for (int i = 0; i < 8 && bay < 0; i++) if (v.present[i] && v.player[i]) bay = i;
            for (int i = 0; i < 8 && bay < 0; i++) if (v.present[i]) bay = i;
        }
        say(m, "\n== ");
        label(m, T_WEAPONS, "Weapons");
        say(m, " ==\n");
        if (bay < 0) { say(m, "! no Herc in the hangar\n"); return; }
        show_bay(m, &v, bay);
        say(m, "  1  bay\n  2  weapon\n  3  guidance\n  4  back\n");
        if (!ask_number(m, "choice", &choice) || choice == 4) return;
        if (choice == 1) {
            if (!ask_number(m, "bay", &value)) return;
            if (value < 1 || value > 8 || !v.present[value - 1]) say(m, "! no Herc in bay %d\n", value);
            else bay = m->h.flow.bay_select = value - 1;
            continue;
        }
        if (choice != 2 && choice != 3) { say(m, "? 1..4\n"); continue; }
        if (!ask_number(m, "slot", &slot)) return;
        if (slot < 1 || slot > v.capacity[bay]) { say(m, "? 1..%d\n", v.capacity[bay]); continue; }
        if (choice == 3) {
            int item = v.item[bay][slot - 1];
            if (item < 13 || item > 16) { say(m, "! no missile rack in slot %d\n", slot); continue; }
            for (int i = 0; i < 4; i++) {
                say(m, "  %d  ", i + 1);
                label_in(m, m->h.wpn_desc, W_GUIDANCE + 3u * (unsigned)i, "guidance");
                say(m, "\n");
            }
            if (!ask_number(m, "mode", &value)) return;
            if (value < 1 || value > 4) { say(m, "? 1..4\n"); continue; }
            es2_shell_bay_op op = {ES2_SHELL_BAY_GUIDANCE, (int16_t)bay, (int16_t)(slot - 1), (int16_t)(value - 1), 0, 0};
            if (eio_bay(&m->h.io, &op)) say(m, "[guidance set]\n");
            else engine_error(m);
            continue;
        }
        bay_view rows;
        if (!bay_query(m, bay, slot - 1, &rows)) return;
        for (int r = 0; r < rows.rows; r++) {
            say(m, "  %2d %c ", r + 1, rows.row_enabled[r] ? ' ' : '-');
            item_name(m, rows.row_item[r]);
            if (rows.row_item[r]) say(m, "  (%d)", rows.row_count[r]);
            say(m, "\n");
        }
        if (!ask_number(m, "row", &value)) return;
        if (value < 1 || value > rows.rows) { say(m, "? 1..%d\n", rows.rows); continue; }
        if (!rows.row_enabled[value - 1]) { say(m, "! not available for this slot\n"); continue; }
        /* a CLICK with the ARM answer of the bay's vehicle (0x41501f) */
        int16_t item = (int16_t)rows.row_item[value - 1];
        es2_shell_bay_op op = {ES2_SHELL_BAY_CLICK, (int16_t)bay, (int16_t)(slot - 1), item,
                               es2_shell_engine_admits(&m->h.io.engine, (int16_t)v.vehicle[bay], item, (int16_t)(slot - 1)), 0};
        if (eio_bay(&m->h.io, &op)) say(m, op.applied ? "[mounted]\n" : "[nothing to mount]\n");
        else engine_error(m);
    }
}

static void mission_texts(menu *m); static void debrief_texts(menu *m, es2_span results, es2_span sav);   /* end of file */
static void movies_play(menu *m, int build_palette); static void mission_movies(menu *m);
static void debrief_movie(menu *m);   /* end of file */
/* The service bay (0x43afd4, shown by 0x43b7f2 + 0x43b758: screen word -1)
 * after a new career, CONTINUE, RESTORE and a continuing results path. The
 * bar's buttons are the flow's events; the sub-screen shown is the screen
 * word: WEAPONS (2) the weapons prompts, MISSION (7) the mission texts and
 * Rock & Roll. A button equal to the word changes nothing in VSHELL; the
 * text menu shows that sub-screen again. REPAIR, BUILD, ARMORY and CREW
 * are the editors (editors.h). */
static void service_bay(menu *m) {
    es2_shell_flow *f = &m->h.flow;
    status st;
    int choice;
    memset(&st, 0, sizeof st);
    say(m, "\n== ");
    label(m, T_SERVICE_BAY, "Service bay");
    say(m, " ==\n");
    if (query(m, &st)) show_status(m, &st);
    say(m, "  1  "); label(m, T_BAY_MAIN_MENU, "Main menu");
    say(m, "\n  2  "); label(m, T_BAY_SAVE, "Save");
    say(m, "\n  3  "); label(m, T_WEAPONS, "Weapons");
    say(m, "\n  4  "); label(m, T_BAY_MISSION, "Mission");
    say(m, " ("); label(m, T_ROCK_AND_ROLL, "Rock & Roll");
    say(m, ")\n");
    if (!ask_number(m, "choice", &choice)) {   /* no leave: the exit saves the state as it is */
        es2_shell_flow_event(f, ES2_FLOW_QUIT_ACCEPT, NULL);
        return;
    }
    switch (choice) {
    case 1:   /* 0x43a66c: leave, hide the bay, save slot 10, main menu */
        es2_shell_flow_event(f, ES2_FLOW_BAY_MAIN_MENU, NULL);
        break;
    case 2:   /* 0x43a768: Saved games with caller mark 8 */
        es2_shell_flow_event(f, ES2_FLOW_BAY_SAVE, NULL);
        break;
    case 3:   /* 0x43a862 */
        es2_shell_flow_event(f, ES2_FLOW_BAY_WEAPONS, NULL);
        if (f->bay_word == ES2_FLOW_WORD_WEAPONS) weapons(m);
        break;
    case 4:   /* 0x43ad5a -> 0x43aee7: leave, mission screen; Rock & Roll */
        es2_shell_flow_event(f, ES2_FLOW_BAY_MISSION, NULL);
        if (f->bay_word == ES2_FLOW_WORD_MISSION) {
            mission_texts(m);   /* the mission screen's pages 0 and 1 */
            mission_movies(m);  /* their movies, played by the next main-loop step */
            start_mission(m);
        }
        break;
    default:
        say(m, "? 1..4\n");
        break;
    }
}

/* PREFERENCES (0x435598, shown by 0x436d45): every control changes the
 * current block of this process (es2_shell_prefs_session in the flow);
 * ACCEPT 0x4372e1 commits it and writes all 54 bytes (0x40dd0a), CANCEL
 * 0x437220 returns the current block to the committed one and writes
 * nothing. Numbers 1..5 are the first menu's; 6..8 the launch-check
 * options (vshell_ui.md section 11.3). The end of the input is a CANCEL (nothing written). */
static void checkbox(menu *m, unsigned id, const char *fallback, uint8_t value) {
    say(m, "[%c] ", value ? 'x' : ' ');   /* the box shows the byte (0x436e20: +0x69 := byte) */
    label(m, id, fallback);
}
static void preferences(menu *m) {
    es2_shell_flow *f = &m->h.flow;
    int choice, value, control;
    const uint8_t *p = f->prefs.current;
    uint8_t repair = p[ES2_SHELL_PREFS_REPAIR], build = p[ES2_SHELL_PREFS_BUILD], display = p[6], resolution = p[4];
    say(m, "\n== ");
    label(m, T_PREFS_TITLE, "Preferences");
    say(m, " ==\n  1  ");
    label(m, T_REPAIR_TITLE, "Repair:");
    say(m, " ");
    if (repair < 3) label(m, T_REPAIR_0 + repair, repair_words[repair]); else say(m, "%u", repair);
    say(m, "\n  2  ");
    label(m, T_BUILD_TITLE, "Weapons building:");
    say(m, " ");
    if (build < 2) label(m, T_BUILD_0 + build, build_words[build]); else say(m, "%u", build);
    say(m, "\n  3  ");
    label(m, T_DISPLAY, "Display:");
    say(m, " ");
    if (display < 2) label(m, T_WINDOW + display, display ? "full screen" : "window"); else say(m, "%u", display);
    say(m, "\n  4  ");
    label(m, T_ACCEPT, "Accept");
    say(m, "\n  5  ");
    label(m, T_CANCEL, "Cancel");
    say(m, "\n  ");
    label(m, T_AUDIO, "Audio:");
    say(m, "\n  6  ");
    checkbox(m, T_MUSIC, "Music", p[0]);
    say(m, "\n  7  ");
    checkbox(m, T_SFX, "Sound effects", p[1]);
    say(m, "\n  8  ");
    label(m, T_RESOLUTION, "Resolution");
    say(m, ": ");
    /* the radio pair shows byte 4: 0 High Res, 1 Low Res (0x4370b8) */
    if (resolution < 2) label(m, resolution ? T_LOW_RES : T_HIGH_RES, resolution ? "low" : "high");
    else say(m, "%u", resolution);
    say(m, "\n");
    if (!ask_number(m, "choice", &choice) || choice == 5) {
        es2_shell_flow_event(f, ES2_FLOW_PREFS_CANCEL, NULL);
        return;
    }
    if (choice == 4) {
        es2_shell_flow_event(f, ES2_FLOW_PREFS_ACCEPT, NULL);   /* the host reports a failed write */
        return;
    }
    if (choice == 6 || choice == 7) {   /* check boxes: byte 0 or 1 ^= 1 */
        es2_shell_flow_prefs_control(f, choice == 6 ? ES2_SHELL_PREFS_MUSIC_TOGGLE : ES2_SHELL_PREFS_SFX_TOGGLE);
        return;
    }
    int count = choice == 1 ? 3 : choice == 2 ? 2 : choice == 3 ? 2 : choice == 8 ? 2 : 0;
    if (!count) return;
    for (int i = 0; i < count; i++) {
        say(m, "  %d  ", i + 1);
        if (choice == 1) label(m, T_REPAIR_0 + (unsigned)i, repair_words[i]);
        else if (choice == 2) label(m, T_BUILD_0 + (unsigned)i, build_words[i]);
        else if (choice == 3) label(m, T_WINDOW + (unsigned)i, i ? "full screen" : "window");
        else label(m, i ? T_LOW_RES : T_HIGH_RES, i ? "low" : "high");
        say(m, "\n");
    }
    if (!ask_number(m, "value", &value)) {
        es2_shell_flow_event(f, ES2_FLOW_PREFS_CANCEL, NULL);
        return;
    }
    if (value < 1 || value > count) return;
    if (choice == 1) control = ES2_SHELL_PREFS_REPAIR_0 + value - 1;
    else if (choice == 2) control = ES2_SHELL_PREFS_BUILD_0 + value - 1;
    else if (choice == 8) control = value == 1 ? ES2_SHELL_PREFS_HIGH_RES : ES2_SHELL_PREFS_LOW_RES;
    else if (value == 1) control = ES2_SHELL_PREFS_WINDOW;
    else control = ES2_SHELL_PREFS_FULL_SCREEN;   /* 0x437608: the flow shows the Alert box */
    es2_shell_flow_prefs_control(f, control);
}

/* ---- boxes (vshell_ui.md 4.10, section 11) --------------------------------- */
/* The Replay box 0x44cdac after a lost mission (0x44d0e7: 3 "You were
 * killed", 0 "The war is lost"): Yes 0x44d1d4 (slot 10 loaded, exit 2) or
 * No 0x44d24d (slot 10 saved, main menu); the flow decides. The end of the
 * input closes the box (main menu; the exit saves slot 10). */
static void replay_box(menu *m) {
    es2_shell_flow *f = &m->h.flow;
    int choice;
    box_lines(m, T_REPLAY, f->box_value == 3 ? T_REPLAY_KILLED : T_REPLAY_LOST, "Replay the mission?");
    say(m, "  1  "); label(m, T_YES, "Yes");
    say(m, "\n  2  "); label(m, T_NO, "No");
    say(m, "\n");
    for (;;) {
        if (!ask_number(m, "choice", &choice)) {
            es2_shell_flow_box_close(f);
            return;
        }
        if (choice == 1 || choice == 2) break;
        say(m, "? 1..2\n");
    }
    if (choice == 2) {
        es2_shell_flow_event(f, ES2_FLOW_REPLAY_NO, NULL);
        return;
    }
    if (es2_shell_flow_event(f, ES2_FLOW_REPLAY_YES, NULL) == ES2_FLOW_REFUSED) say(m, "! slot 10 holds no game to replay\n");
    else say(m, "[DBSIM: replay]\n");
}
/* The box the flow shows; OKAY boxes are answered at once. */
static void show_box(menu *m) {
    es2_shell_flow *f = &m->h.flow;
    switch (f->box) {
    case ES2_FLOW_BOX_END_OF_GAME:
        end_text(m, f->box_value);
        es2_shell_flow_event(f, ES2_FLOW_END_OKAY, NULL);
        break;
    case ES2_FLOW_BOX_WARNING:   /* 0x44d66c: 0x44d90c(n) */
        box_lines(m, ES2_SHELL_WARNING_TITLE, ES2_SHELL_WARNING_LINE1 + 2u * (unsigned)f->box_value, "Launch refused.");
        say(m, "  [");
        label(m, T_OKAY, "OKAY");
        say(m, "]\n");
        es2_shell_flow_event(f, ES2_FLOW_WARNING_OKAY, NULL);
        break;
    case ES2_FLOW_BOX_REPLAY:
        replay_box(m);
        break;
    case ES2_FLOW_BOX_ALERT:     /* Full Screen 0x437608; only its ACCEPT 0x437678 stores the byte */
        say(m, "  ");
        label(m, T_ALERT, "Alert!");
        say(m, " ");
        label(m, T_ALERT_TEXT, "");
        say(m, "  [");
        label(m, T_ALERT_ACCEPT, "ACCEPT");
        say(m, "]\n");
        es2_shell_flow_event(f, ES2_FLOW_ALERT_ACCEPT, NULL);
        break;
    default:
        es2_shell_flow_box_close(f);
        break;
    }
}

/* -X3/-X4 (vshell_ui.md section 6): the results path is host.c's;
 * the text presenter shows the debrief. */
static void menu_results(void *ctx, const char *played, const es2_shell_results_output *out, es2_span results) {
    menu *m = ctx;
    label(m, T_MISSION, "Mission:");
    say(m, " %s\n", played);
    label(m, T_SALVAGE, "Salvage:");
    say(m, " %lu\n", (unsigned long)out->total);
    if (out->text.size) {
        say_span(m, (es2_span){out->text.data, out->text.size});
        say(m, "\n");
        debrief_texts(m, results, (es2_span){out->save.data, out->save.size});   /* page 4 */
    }
}
/* The results path's end as the flow decided it (the Replay and End boxes
 * follow through show_box). */
static void results_text(menu *m) {
    char mission[40];
    switch (m->h.flow.outcome) {
    case ES2_FLOW_OUTCOME_NO_SLOT:
        say(m, "! DBSIM returned %d, but no mission was handed to it (no slot %d)\n", m->h.args.state,
            es2_shell_slot_effective(SLOT_CAMPAIGN, m->h.flow.mode));
        break;
    case ES2_FLOW_OUTCOME_NEXT:   /* the DEBUG dialog's Use Default: the next mission at once */
        eio_mission_name(&m->h.io, mission, sizeof mission);
        label(m, T_MISSION, "Mission:");
        say(m, " %s\n", mission);
        debrief_movie(m);   /* page 4's movie */
        break;
    case ES2_FLOW_OUTCOME_WON:   /* 0x40f554: after the slot-10 save, victory.avi and credits.avi */
        es2_shell_movies_victory(&m->h.movies);
        movies_play(m, 1);
        break;
    case ES2_FLOW_OUTCOME_NEXT_FAILED:
        say(m, "! campaign stopped: %s\n", m->h.io.error);
        break;
    case ES2_FLOW_OUTCOME_OTHER_MODE:   /* mode 0 (practice): main menu */
        say(m, "! campaign stopped: stop-other-mode\n");
        break;
    case ES2_FLOW_OUTCOME_STOPPED:
        say(m, "! campaign stopped: stop-return-%d\n", m->h.flow.outcome_advance);
        break;
    default:   /* REJECTED: the host said why; REPLAY: boxes */
        break;
    }
}

static int practice_entry(menu *m, int choice);   /* at the end of the file */
static void practice_screen(menu *m);
static void main_menu(menu *m) {
    es2_shell_flow *f = &m->h.flow;
    status st;
    int choice;
    memset(&st, 0, sizeof st);
    say(m, "\n== ");
    label(m, T_MAIN_MENU, "Main menu");
    say(m, " ==\n");
    if (f->game == GAME_CAMPAIGN && query(m, &st)) show_status(m, &st);
    say(m, "  1  "); label(m, T_NEW_GAME, "New campaign");
    say(m, "\n  2  "); label(m, T_CONTINUE, "Continue");
    say(m, "\n  3  "); label(m, T_ROCK_AND_ROLL, "Start mission");
    say(m, "\n  4  "); label(m, T_PREFERENCES, "Options");
    say(m, "\n  5  "); label(m, T_QUIT, "Quit");
    say(m, "\n  6  "); label(m, T_SAVE_RESTORE, "Save/Restore");   /* the first menu's numbers kept */
    say(m, "\n  7  "); label(m, 0x07, "Practice missions");
    say(m, "\n  8  "); label(m, 0x03, "Instant action");
    say(m, "\n  9  "); label(m, 0x0b, "View demo");
    say(m, "\n  10 "); label(m, 0x0d, "Credits");
    if (m->h.help_policy != ES2_HELP_POLICY_HIDE) { say(m, "\n  11 "); label(m, 0x0f, "Online manual"); }
    say(m, "\n");
    if (!ask_number(m, "choice", &choice)) {
        es2_shell_flow_event(f, ES2_FLOW_QUIT_ACCEPT, NULL);
        return;
    }
    switch (choice) {
    case 1:   /* START NEW GAME 0x431a08: mode 1, Registration */
        say(m, "\n== ");
        label(m, T_REGISTRATION, "New career");
        say(m, " ==\n");
        es2_shell_flow_event(f, ES2_FLOW_START_NEW_GAME, NULL);
        break;
    case 2:   /* CONTINUE GAME 0x431a73 */
        say(m, "\n== ");
        label(m, T_CONTINUE, "Continue");
        say(m, " ==\n");
        es2_shell_flow_event(f, ES2_FLOW_CONTINUE_GAME, NULL);
        break;
    case 3: start_mission(m); break;   /* shortcut (VSHELL: bay, MISSION) */
    case 4: es2_shell_flow_event(f, ES2_FLOW_PREFERENCES_BUTTON, NULL); break;
    case 5: es2_shell_flow_event(f, ES2_FLOW_QUIT, NULL); break;
    case 6: es2_shell_flow_event(f, ES2_FLOW_SAVE_RESTORE, NULL); break;
    case 7: case 8: case 9: practice_entry(m, choice); break;
    case 10: es2_shell_movies_credits(&m->h.movies); movies_play(m, 0); break;   /* CREDITS 0x431c7b */
    case 11:   /* ONLINE MANUAL 0x431e1b (policy hide: not offered) */
        if (m->h.help_policy == ES2_HELP_POLICY_HIDE) { say(m, "? 1..6\n"); break; }
        es2_shell_flow_event(f, ES2_FLOW_ONLINE_MANUAL, NULL);
        break;
    default: say(m, "? 1..6\n"); break;
    }
}

/* The text presenter: the flow's box or screen, one step at a time, until
 * the quit flag. */
static void run(menu *m) {
    es2_shell_flow *f = &m->h.flow;
    while (!f->quit) {
        /* With -@ the DEBUG dialog of a mode-1 new career waits */
        if (!es2_host_debug_ask(&m->h)) break;
        if (f->box != ES2_FLOW_BOX_NONE) { show_box(m); continue; }
        switch (f->screen) {
        case ES2_FLOW_MAIN_MENU: main_menu(m); break;
        case ES2_FLOW_REGISTRATION: registration(m); break;
        case ES2_FLOW_SAVED_GAMES: saved_games(m); break;
        case ES2_FLOW_PREFERENCES: preferences(m); break;
        case ES2_FLOW_PRACTICE: practice_screen(m); break;
        case ES2_FLOW_SERVICE_BAY: service_bay(m); break;
        }
    }
}

/* What the host reports goes to the console and the transcript. */
static void menu_say(void *ctx, const char *data, size_t size) {
    say_span(ctx, (es2_span){(const uint8_t *)data, size});
}
/* The DEBUG dialog with -@: a practice launch, a mode-1 new career
 * and the results path's advance 2. */
static int menu_debug_choice(void *ctx, char *typed, size_t capacity) {
    menu *m = ctx;
    int choice;
    say(m, "\n== DEBUG ==\n  1  use the default mission\n  2  type a mission file name\n");
    if (!ask_number(m, "choice", &choice)) return 0;
    if (choice != 2) return 1;
    return ask(m, "mission", typed, capacity) ? 2 : 0;
}
int main(int argc, char **argv) {
    static menu m;
    char message[256], cwd[ES2_OS_MAX_PATH];
    int code = EXIT_QUIT;
    m.h.ui.ctx = &m;
    m.h.ui.say = menu_say;
    m.h.ui.results = menu_results;
    m.h.ui.debug_choice = menu_debug_choice;
    if (es2m_parse_args(argc, argv, &m.h.args, message, sizeof message) != 0) {
        fprintf(stderr, "es2menu: %s\n", message);
        return EXIT_QUIT;
    }
    if (!es2_host_config(&m.h)) {
        fprintf(stderr, "es2menu: %s\\menu.ini is missing or incomplete\n", m.h.args.menu_dir);
        return EXIT_QUIT;
    }
    char log_path[ES2_OS_MAX_PATH];
    join(log_path, sizeof log_path, m.h.dir, "menu.log");
    m.log = es2_os_fopen(log_path, "ab");   /* binary: the transcript keeps the game text bytes */
    es2_os_current_directory(cwd, sizeof cwd);
    if (m.log) es2_os_fprintf(m.log, "\n[start -X%d %s in %s]\n", m.h.args.state, m.h.args.language, cwd);
    if (!es2_host_check_directory(&m.h, cwd)) goto end;
    if (!es2_host_cd_check(&m.h)) goto end;   /* 0x401cac, before the game init */
    if (m.h.args.input) script_open(&m);
    es2_host_labels_open(&m.h);
    es2_host_start(&m.h, NULL /* no display, always windowed */);
    /* the entry dispatch 0x40188a: VSHELL treats 3 and 4 alike (0x46c274):
     * load slot 10, results path */
    if (es2_shell_flow_entry_of(m.h.args.state) == ES2_FLOW_ENTRY_RESULTS) {
        say(&m, "\n== ");
        label(&m, T_REPORT, "Mission report");
        say(&m, " ==\n");
    } else if (eio_abort(&m.h.io)) {
        /* DBSIM ended without results (1: error or ended from outside; 6:
         * demo): VSHELL starts fresh and reads nothing; the log records it. */
        say(&m, "! DBSIM ended without results (shell state %d); nothing was read\n", m.h.args.state);
    }
    /* Entry modes other than 3, 4 and 6 play the intro first (0x4018fd) */
    if (es2_shell_movies_intro(&m.h.movies, (int16_t)m.h.args.state)) movies_play(&m, 0);
    if (es2_shell_flow_start(&m.h.flow, m.h.args.state) == ES2_FLOW_ENTRY_RESULTS) {
        es2_host_debug_ask(&m.h);   /* advance 2 with -@ (0x40f647) */
        results_text(&m);
    }
    run(&m);
    /* Every exit with a loaded game saves slot 10 (0x402197 -> 0x40e933(10, 0)). */
    code = m.h.assert_exit ? 1 : es2_shell_flow_finish(&m.h.flow);   /* exit(1) skips the loop end 0x402197 */
end:
    say(&m, "[exit %d]\n", code);
    if (m.log) es2_os_fclose(m.log);
    es2_host_close(&m.h);
    free(m.script);
    return code;
}
/* The statistics of a used slot as the panel 0x4377bc shows them
 * (vshell_ui.md section 13): the record the start-up read 0x4309f0 makes
 * from the slot's SAV (VSHELL refreshes it from memory with 0x430ec4 after
 * a save or load, which gives the same values as the SAV just written), the
 * labels ESTEXT 0x24..0x2e, skill 0x35 + s, rank 0x39 + r, sector 0x76 + s,
 * salvage / 1000 with 0x2f, mission + 1. */
static void saved_games_statistics(menu *m, const es2_shell_slot_table *t, int slot) {
    static const char *kill_words[3] = {"Herc kills:", "Flyer kills:", "Base kills:"};
    char path[ES2_OS_MAX_PATH];
    size_t n;
    uint8_t *sav;
    es2_shell_stats record;
    es2_shell_stats_view v;
    es2_error e;
    memset(&record, 0, sizeof record);
    snprintf(path, sizeof path, "SAV\\%s", t->slot[slot].file);
    sav = read_all(path, &n);
    if (sav && es2_shell_stats_read((es2_span){sav, n}, &record, &e) == ES2_OK) {
        es2_shell_stats_show(&record, &v);
        say(m, "        ");
        label(m, 0x24, "Name:");
        say(m, " %s  ", v.name);
        label(m, 0x25, "Skill:");
        say(m, " ");
        label(m, (unsigned)v.skill_text, "?");
        say(m, "  ");
        label(m, 0x26, "Rank:");
        say(m, " ");
        label(m, (unsigned)v.rank_text, "?");
        say(m, "\n        (");
        label(m, 0x2a, "Current");
        say(m, "/");
        label(m, 0x2b, "Total");
        say(m, ")");
        for (int i = 0; i < 3; i++) {
            say(m, "  ");
            label(m, 0x27 + (unsigned)i, kill_words[i]);
            say(m, " %d/%d", v.kills[2 * i], v.kills[2 * i + 1]);
        }
        say(m, "\n        ");
        label(m, 0x2c, "Salvage:");
        say(m, " %ld ", (long)v.salvage);
        label(m, (unsigned)v.salvage_text, "Tons");
        say(m, "  ");
        label(m, 0x2e, "Sector:");
        say(m, " ");
        label(m, (unsigned)v.sector_text, "?");
        say(m, "  ");
        label(m, 0x2d, "Mission:");
        say(m, " %d\n", v.mission);
    }
    free(sav);
}

/* ---- PRACTICE MISSIONS, INSTANT ACTION, VIEW DEMO (vshell_ui.md section 15) ----
 * es2/shell/practice.h over the process's prefs blocks (m->h.flow.prefs). A launch is
 * VSHELL's new game in mode 0 plus the DEBUG dialog's posted Use Default:
 * computed here in C from VSHELL's tables in the span store and the
 * CD (cd_root of menu.ini: VOL\ZONES, SHELL0, LANG0), the four DATA
 * files written as VSHELL leaves them (script.dat and MISSION.STR without
 * truncation), the state kept as the loaded game so that the exit save
 * writes slot 11 (mode 0) with its three companions. With -@ (cheat byte
 * 0x482368) the dialog waits: Use Default or a typed mission file (ACCEPT
 * 0x44dc4d); INSTANT ACTION then exits with the new game's mission.var and
 * player.mec only. VIEW DEMO answers 5 (es2launch starts DBSIM -D). */
#include "es2/shell/practice.h"

enum { T_INSTANT = 0x03, T_PRACTICE = 0x07, T_VIEW_DEMO = 0x0b, T_PRACTICE_LIST = 0xf1, T_PRACTICE_ENTRY = 0xf2,
       T_PARAMETERS = 0xfa, T_PRACTICE_MAIN_MENU = 0x100, T_BEGIN_MISSION = 0x102 };
static const unsigned practice_control_names[5] = {0xfc, 0xfb, 0xfd, 0xff, 0xfe};   /* Ammo Damage Difficulty Herc Time */
static void practice_controls(menu *m) {
    char fallback[16];
    for (int c = 0; c < 5; c++) {
        unsigned id = es2_shell_practice_label(c, &m->h.flow.prefs);
        say(m, "  %2d/%2d  ", 11 + c, 21 + c);
        label(m, practice_control_names[c], "control");
        say(m, ": ");
        snprintf(fallback, sizeof fallback, "%u", m->h.flow.prefs.current[0x25 + c]);
        label(m, id, fallback);
        if (c == ES2_SHELL_PRACTICE_HERC && !es2_shell_practice_herc_enabled(&m->h.practice.state)) say(m, " (-)");
        say(m, "\n");
    }
}
/* PRACTICE MISSIONS (0x431f3a, screen 0x44b310), one step per call. Main
 * Menu (0x44c96a): no revert, the controls stay uncommitted; a failed launch
 * returns to the main menu as well. */
static void practice_screen(menu *m) {
    uint8_t out[ES2_SHELL_PREFS_SIZE];
    es2_error e;
    int choice, applied, changed;
    say(m, "\n== ");
    label(m, T_PRACTICE, "Practice missions");
    say(m, " ==\n");
    label(m, T_PRACTICE_LIST, "Missions:");
    say(m, "\n");
    for (int i = 0; i < ES2_SHELL_PRACTICE_ENTRIES; i++) {
        say(m, " %c%2d  ", m->h.practice.state.index == i ? '*' : ' ', i + 1);
        label(m, T_PRACTICE_ENTRY + (unsigned)i, "training mission");
        say(m, "\n");
    }
    label(m, T_PARAMETERS, "Parameters");
    say(m, " (11..15 next, 21..25 previous)\n");
    practice_controls(m);
    say(m, "   9  "); label(m, T_PRACTICE_MAIN_MENU, "Main menu");
    say(m, "\n  10  "); label(m, T_BEGIN_MISSION, "Begin mission");
    say(m, "\n");
    if (!ask_number(m, "choice", &choice)) {
        es2_shell_flow_event(&m->h.flow, ES2_FLOW_QUIT_ACCEPT, NULL);
        return;
    }
    if (choice >= 1 && choice <= 8) {
        es2_shell_practice_entry(&m->h.practice.state, &m->h.flow.prefs, m->h.practice.tables, choice - 1, &changed, &e);
    } else if ((choice >= 11 && choice <= 15) || (choice >= 21 && choice <= 25)) {
        es2_shell_practice_control(&m->h.practice.state, &m->h.flow.prefs, choice % 10 - 1,
                                   choice < 20 ? ES2_SHELL_CLICK_LEFT : ES2_SHELL_CLICK_RIGHT, &applied, &e);
        if (!applied) say(m, "! not available for this mission\n");
    } else if (choice == 9) {
        es2_shell_flow_event(&m->h.flow, ES2_FLOW_PRACTICE_MAIN_MENU, NULL);
    } else if (choice == 10) {
        es2_shell_practice_launch launch;
        if (es2_shell_practice_begin(&m->h.practice.state, &m->h.flow.prefs, m->h.practice.tables, out, &launch, &e) != ES2_OK ||
            !es2_host_practice_write_prefs(&m->h, out)) return;
        if (es2_host_practice_launch(&m->h, &launch, 0)) es2_shell_flow_dbsim(&m->h.flow);
        else es2_shell_flow_event(&m->h.flow, ES2_FLOW_PRACTICE_MAIN_MENU, NULL);
    } else say(m, "? 1..15, 21..25\n");
}
/* Main-menu entries 7..9. */
static int practice_entry(menu *m, int choice) {
    if (choice == 9) return es2_shell_flow_event(&m->h.flow, ES2_FLOW_VIEW_DEMO, NULL);   /* 0x431bfe: exit 5, quit flag */
    if (!es2_host_practice_open(&m->h)) return 0;
    if (choice == 7) {
        /* 0x431f3a: the practice library's show and its prefs write, then
         * the flow's screen (mode 0) */
        uint8_t out[ES2_SHELL_PREFS_SIZE];
        size_t size;
        uint8_t *file = read_all("DATA\\PREFS.CFG", &size);
        es2_span span = {file, size};
        es2_error e;
        es2_status status = es2_shell_practice_show(&m->h.practice.state, &m->h.flow.prefs, m->h.practice.tables,
                                                    file ? &span : NULL, out, &e);
        free(file);
        if (status != ES2_OK || !es2_host_practice_write_prefs(&m->h, out)) return 0;
        return es2_shell_flow_event(&m->h.flow, ES2_FLOW_PRACTICE_MISSIONS, NULL);
    }
    {
        /* INSTANT ACTION 0x431935 */
        uint8_t mode_out[ES2_SHELL_PREFS_SIZE], out[ES2_SHELL_PREFS_SIZE];
        size_t size;
        uint8_t *file = read_all("DATA\\PREFS.CFG", &size);
        es2_span span = {file, size};
        es2_shell_practice_launch launch;
        es2_error e;
        es2_status status = es2_shell_instant_action(&m->h.practice.state, &m->h.flow.prefs, m->h.practice.tables,
                                                     file ? &span : NULL, mode_out, out, &launch, &e);
        free(file);
        if (status != ES2_OK) {
            say(m, "! INSTANT ACTION refused: prefs.cfg +0x2e is %u (0..2)\n", m->h.flow.prefs.current[ES2_SHELL_PREFS_INSTANT]);
            return 0;
        }
        if (!es2_host_practice_write_prefs(&m->h, mode_out) || !es2_host_practice_write_prefs(&m->h, out)) return 0;
        es2_shell_flow_event(&m->h.flow, ES2_FLOW_INSTANT_ACTION, NULL);   /* mode 0 */
        if (es2_host_practice_launch(&m->h, &launch, 1)) es2_shell_flow_dbsim(&m->h.flow);
        return 1;
    }
}

/* The texts of the mission screen 0x442bc4 (vshell_ui.md section 16).
 * Pages 0 and 1 as the MISSION button shows them: the title "<sector name>
 * <0x7c>", the <lang>\campaign.str text of the CAREER group (0x40fd2d), then
 * the briefing, objectives and intelligence texts 0x4135bb composes from the
 * prepared mission's MISSION.STR and the selection of the SAV. Page 4 after
 * the debrief text of a continuing results path: the ten values 0x40f904
 * fills, from results.dat and the SAV the results path wrote. Whole texts;
 * the panels' page split (es2_shell_report_layout) is the graphical
 * screen's. */
#include "es2/shell/texts.h"
static void s11_text(menu *m, unsigned id, const char *fallback, es2_span text) {
    say(m, "\n  -- ");
    label(m, id, fallback);
    say(m, " --\n");
    say_span(m, text);
    say(m, "\n");
}
static void mission_texts(menu *m) {
    char path[ES2_OS_MAX_PATH], title[128];
    uint8_t *strings = NULL, selection[ES2_SHELL_BRIEFING_SIZE];
    size_t strings_size = 0;
    int16_t group = 0;
    es2_span name, word, text;
    es2_vfs_copy copy;
    es2_error e;
    /* The memory state and DATA\MISSION.STR, as VSHELL's 0x4135bb
     * reads them */
    if (m->h.flow.game != GAME_CAMPAIGN || !m->h.io.engine.memory.data ||
        es2_shell_briefing_selection((es2_span){m->h.io.engine.memory.data, m->h.io.engine.memory.size}, selection, &group,
                                     NULL, &e) != ES2_OK)
        return;
    /* page 0: title 0x444ae4 and the campaign text 0x444a42 */
    say(m, "\n== ");
    if (es2m_bin_raw(m->h.estext, (size_t)(uint16_t)es2_shell_sector_name_id(group), &name, &e) == ES2_OK &&
        es2m_bin_raw(m->h.estext, ES2_SHELL_SECTOR_WORD, &word, &e) == ES2_OK &&
        es2_shell_sector_title(name, word, title, sizeof title, &e) == ES2_OK)
        say(m, "%s", title);
    else
        say(m, "Sector");
    say(m, " ==\n");
    snprintf(path, sizeof path, "%s/CAMPAIGN.STR", m->h.args.language);
    if (m->h.estext.data && es2_vfs_find(&m->h.vfs, path, &copy, &e) == ES2_OK &&
        es2_shell_campaign_text(copy.payload, group, &text, &e) == ES2_OK) {
        say_span(m, text);
        say(m, "\n");
    }
    /* page 1 (0x444b6e): the three panels of 0x4135bb */
    {
        es2_shell_briefing_texts t;
        memset(&t, 0, sizeof t);
        strings = read_all("DATA\\MISSION.STR", &strings_size);
        say(m, "\n== ");
        label(m, 0xb3, "Mission map");
        say(m, " ==");
        if (strings && es2_shell_briefing_texts_compose((es2_span){strings, strings_size}, selection, &t, &e) == ES2_OK) {
            s11_text(m, 0xb4, "Mission briefing", (es2_span){t.text[ES2_SHELL_TEXT_BRIEFING].data, t.text[ES2_SHELL_TEXT_BRIEFING].size});
            s11_text(m, 0xb6, "Mission objectives", (es2_span){t.text[ES2_SHELL_TEXT_OBJECTIVES].data, t.text[ES2_SHELL_TEXT_OBJECTIVES].size});
            s11_text(m, 0xb7, "Intelligence report", (es2_span){t.text[ES2_SHELL_TEXT_INTELLIGENCE].data, t.text[ES2_SHELL_TEXT_INTELLIGENCE].size});
        } else
            say(m, "\n! the briefing texts could not be composed (%s)\n", strings ? es2_status_string(e.code) : "no MISSION.STR");
        es2_shell_briefing_texts_free(&t);
    }
    free(strings);
}
/* Page 4 (0x444d1f): the statistics labels of section 14.1 with the values
 * of 0x40f904, from DATA\results.dat as the results path read it and the SAV
 * the results path wrote (both in memory). */
static void debrief_texts(menu *m, es2_span results, es2_span sav) {
    es2_shell_debrief_words w;
    es2_shell_debrief_texts t;
    es2_span outcome = {0}, unit = {0};
    es2_error e;
    if (results.data && sav.data && es2_shell_debrief_words_after(results, sav, &w, &e) == ES2_OK) {
        if (m->h.estext.data) {
            es2m_bin_raw(m->h.estext, (size_t)(uint16_t)es2_shell_debrief_outcome_id(w.outcome), &outcome, &e);
            es2m_bin_raw(m->h.estext, ES2_SHELL_DEBRIEF_UNIT, &unit, &e);
        }
        if (es2_shell_debrief_texts_compose(&w, outcome, unit, &t, &e) == ES2_OK) {
            say(m, "\n  "); label(m, 0x145, "Mission outcome:"); say(m, " %s\n", t.text[0]);
            say(m, "  "); label(m, 0x148, "Salvage:"); say(m, " %s\n", t.text[1]);
            say(m, "  "); label(m, 0x14a, "Weapons recovered:"); say(m, " %s\n", t.text[2]);
            say(m, "  "); label(m, 0x14c, "Kills"); say(m, " | "); label(m, 0x150, "1");
            say(m, " | "); label(m, 0x151, "2"); say(m, " | "); label(m, 0x152, "3"); say(m, "\n");
            say(m, "  "); label(m, 0x14e, "You"); say(m, " | %s | %s | %s\n", t.text[3], t.text[5], t.text[7]);
            say(m, "  "); label(m, 0x14f, "Squad"); say(m, " | %s | %s | %s\n", t.text[4], t.text[6], t.text[8]);
            say(m, "  "); label(m, 0x14d, "Lost"); say(m, " | %s\n", t.text[9]);
        }
    }
}

/* ---- The typed names under VSHELL's edit-field rules ------------------
 * vshell_text.md ("Edit field"): a field takes a character only if it is in
 * its set, length + 1 < 90 and width + advance + 6 < r - l, the advances
 * being FONT2's (dfn\font2.dfn of SHELL1.VOL, VSHELL's archive order). The
 * console menu reads SHELL1.VOL from the game directory or from menu.ini's
 * cd_root; without it the Saved games row falls back to 24 characters
 * and the registration name to its set and length rule (documented
 * deviation: no glyph widths without the font). */
#include "es2/shell/gui/text.h"

static struct {
    int tried;
    uint8_t *vol_bytes;
    es2_vol vol;
    es2_dfn font;
    const es2_dfn *ready;
} s27;

static const es2_dfn *s27_font(menu *m) {
    char path[ES2_OS_MAX_PATH];
    size_t size = 0;
    es2_vfs_source source;
    es2_vfs vfs;
    es2_vfs_copy copy;
    if (s27.tried) return s27.ready;
    s27.tried = 1;
    s27.vol_bytes = read_all("VOL\\SHELL1.VOL", &size);
    if (!s27.vol_bytes && m->h.io.cd_root[0]) {   /* menu.ini's cd_root (host.c) */
        join(path, sizeof path, m->h.io.cd_root, "VOL\\SHELL1.VOL");
        s27.vol_bytes = read_all(path, &size);
    }
    if (!s27.vol_bytes || es2_vol_open((es2_span){s27.vol_bytes, size}, &s27.vol, NULL) != ES2_OK) return NULL;
    source.kind = ES2_VFS_VOL;
    source.data.vol = &s27.vol;
    if (es2_vfs_open(&source, 1, &vfs, NULL) != ES2_OK || es2_vfs_find(&vfs, "dfn\\font2.dfn", &copy, NULL) != ES2_OK ||
        es2_dfn_parse(copy.payload, &s27.font, NULL) != ES2_OK)
        return NULL;
    s27.ready = &s27.font;
    return s27.ready;
}

/* The Saved games naming row (0x437e62, 0x40c382; es2_shell_slot_row_typed). */
static es2_status s27_slot_row(menu *m, int slot, const char *typed, char *row, es2_error *e) {
    const es2_dfn *font = s27_font(m);
    es2_status s = font ? es2_shell_slot_row_typed(slot, typed, font, row, e) : es2_shell_slot_row(slot, typed, row, e);
    if (s == ES2_OK) return s;
    if (font) say(m, "! the row takes letters, digits, spaces and ^ while they fit its width\n");
    else say(m, "! up to %d letters, digits, spaces or ^\n", ES2_SHELL_SLOT_NAME_MAX);
    return s;
}

/* The registration name field (0x43c02c: set 0x476205, r - l = 206). */
static int s27_registration_name(menu *m, const char *name) {
    es2_shell_edit edit;
    es2_shell_text_rect rect = {0, 0, ES2_SHELL_EDIT_WIDTH_REGISTRATION, 16};
    es2_shell_edit_init(&edit, rect, s27_font(m));
    edit.allowed = es2_shell_edit_set_names;
    if (es2_shell_edit_type(&edit, name, NULL) == ES2_OK) return 1;
    say(m, "! the name field takes letters, digits and spaces while they fit its width\n");
    return 0;
}

/* ---- Movies (vshell_ui.md section 19) ---------------------------------
 * The text menu queues what VSHELL queues (es2/shell/movies.h) and runs the
 * player's control flow; instead of opening the avivideo device it prints the
 * movie it would play ("[movie avi\c1.avi]"). -a or --no-movies clear the
 * movie flag: nothing is queued, nothing printed. */
static int32_t movie_call(void *context, uint32_t action, int32_t arg, const es2_shell_movie_record *r) {
    menu *m = context;
    char path[64];
    (void)arg;
    switch (action) {
    case ES2_SHELL_MOVIE_OPEN_DEVICE: return 1;
    case ES2_SHELL_MOVIE_PLAY: {
        int language = !strcmp(m->h.args.language, "FRE") ? 1 : !strcmp(m->h.args.language, "GER") ? 2 : 0;
        if (es2_shell_movie_path(r->index, language, path, sizeof path) == 0) say(m, "[movie %s]\n", path);
        else say(m, "[movie %d: outside the table]\n", r->index);
        return ES2_SHELL_MOVIE_ENDED;
    }
    case ES2_SHELL_MOVIE_THEATRE_STEP:
        es2_shell_movies_theatre(&m->h.movies, m->h.movie_group);   /* 0x44472f: the dropship in theatre 5 */
        return 0;
    default:
        return 0;
    }
}
static int32_t movie_query(void *context, int what) {
    (void)context;
    return what == ES2_SHELL_MOVIE_Q_FOCUS;   /* no bay bar or panel in the text menu */
}
static void movies_play(menu *m, int build_palette) {
    es2_shell_movie_hooks hooks = {m, movie_call, movie_query};
    es2_shell_movies_play(&m->h.movies, build_palette, &hooks);
}
/* MISSION (0x43ad5a): page 0 while the group movies were not shown and the
 * CAREER position is 0, else page 1 (0x43ae49); the text menu shows page 1
 * after page 0 at once, so page 0's movies are followed by page 1's
 * briefing movie (VSHELL: when the player turns to page 1). Group,
 * position and the briefing word 0x48419c come from the memory state. */
static void mission_movies(menu *m) {
    uint8_t selection[ES2_SHELL_BRIEFING_SIZE];
    int16_t group = 0, position = 0, word;
    es2_error e;
    if (m->h.flow.game != ES2_FLOW_GAME_CAMPAIGN || !m->h.io.engine.memory.data) return;
    if (es2_shell_briefing_selection((es2_span){m->h.io.engine.memory.data, m->h.io.engine.memory.size}, selection,
                                     &group, &position, &e) == ES2_OK) {
        int page = es2_shell_movies_mission_page(&m->h.movies, position);
        word = (int16_t)(uint16_t)(selection[146] | selection[147] << 8);
        m->h.movie_group = group;
        es2_shell_movies_mission(&m->h.movies, page, group, word, 0);
        movies_play(m, 1);
        if (page == 0) {
            es2_shell_movies_mission(&m->h.movies, 1, group, word, 0);
            movies_play(m, 1);
        }
    }
}
/* Page 4 after a continuing results path: the debrief word 0x48419e (the
 * selected debrief record's +0x3e, kept by host_results) once per 0x477948,
 * with the CAREER group after the advancement. No debrief selected: none. */
static void debrief_movie(menu *m) {
    if (!m->h.debrief_selected) return;
    m->h.movie_group = m->h.debrief_group;
    es2_shell_movies_mission(&m->h.movies, 4, m->h.debrief_group, 0, m->h.debrief_word);
    movies_play(m, 1);
}
