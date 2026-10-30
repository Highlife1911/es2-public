#ifndef ES2_SHELL_HOST_H
#define ES2_SHELL_HOST_H

/* The own shell's host, shared by both presenters of the flow: the text
 * menu es2menu.exe (shell/src/menu.c) and the graphical shell es2shell.exe
 * (shell/src/gui/app.c). It is the file layer of every flow effect
 * (es2/shell/flow.h's host callbacks over engine_io.h), the configuration
 * (menu.ini), the installation's text catalogues, the save slots, the
 * practice launch, the movie queue state of the process, the online manual
 * and the start-up CD check. It shows nothing itself: every line it reports
 * goes to the presenter's `say`, and the presenter's parts of the results
 * path, the DEBUG dialog of -@ and VSHELL's assertion box are callbacks.
 * Both presenters write the same files from the same choices
 * (docs/reverse-engineering/launcher.md, section "Graphical shell"). */
#include "engine_io.h"
#include "menu.h"
#include "win/help.h"
#include "es2/formats/vfs.h"
#include "es2/formats/vol.h"
#include "es2/shell/flow.h"
#include "es2/shell/movies.h"
#include "es2/shell/practice.h"
#include "es2/shell/slots.h"
#include <stdarg.h>
#include <stdio.h>

#define ES2_HOST_PATH 260

/* What the presenter supplies. Every member may be NULL. */
typedef struct es2_host_ui {
    void *ctx;
    /* Bytes the host reports (messages and game text), in order. */
    void (*say)(void *ctx, const char *data, size_t size);
    /* The results path's screen part after the host's results path ran:
     * `played` the mission it ran for, `out` its output, `results` a copy of
     * DATA\results.dat as read. */
    void (*results)(void *ctx, const char *played, const es2_shell_results_output *out, es2_span results);
    /* The DEBUG dialog of a practice launch with -@ (0x44dd38): 2 a typed
     * mission file name (into typed), 0 ends the launch (the dialog stays,
     * VSHELL quits), ES2_HOST_DEBUG_PENDING the presenter shows the dialog
     * and answers later (es2_host_practice_debug_answer), any other value
     * Use Default. NULL: Use Default. On entry `typed` holds the dialog's
     * default name (the CAREER entry of the launch, empty when unknown). */
    int (*debug_choice)(void *ctx, char *typed, size_t capacity);
    /* VSHELL's severity-4 assertion box (0x44e5b0): the text, before the
     * process ends with exit code 1 (the text menu: nothing more). */
    void (*assertion)(void *ctx, const char *text);
} es2_host_ui;

typedef struct es2_host_practice {
    int ready, failed;
    es2_shell_practice state;           /* 0x479c54, 0x473784 for the process */
    uint8_t *vols[3];                   /* cd_root's VOLs (the tables come from the span store) */
    es2_vol vol[3];
    es2_vfs_source sources[3];
    es2_vfs vfs;
    uint8_t defaults[164], items[60], paths[9 * 64], tables[ES2_SHELL_PRACTICE_TABLES_SIZE];
} es2_host_practice;

typedef struct es2_host {
    es2m_args args;
    char dir[ES2_HOST_PATH], game[ES2_HOST_PATH], seed[16];
    es2_host_ui ui;
    /* the installation's catalogues (VOL\LANG0.VOL of the game directory) */
    uint8_t *lang_vol;
    es2_vol vol;
    es2_vfs_source source;
    es2_vfs vfs;
    es2_span estext;           /* <language>/ESTEXT.BIN, empty when unavailable */
    es2_span empty_text;       /* its entry 0x21 as stored (the slot table appends it) */
    es2_span wpn_desc;         /* <language>/WPN_DESC.BIN */
    /* The flow (screen word, loaded flag, mode word, final word,
     * prefs blocks); the game state itself is the engine's memory image */
    es2_shell_flow flow;
    char slot_file[ES2_HOST_PATH];  /* the slot file the game was loaded from */
    int companions;                 /* its three launch-file copies were there */
    eio io;                         /* engine, CD and evidence log */
    es2_help_policy help_policy;    /* menu.ini online_manual */
    int assert_exit, assert_rule;   /* an assertion (exit 1) was reported / assert.log's rule written */
    /* The movie queue and once-flags, the CAREER group of the theatre
     * step (0x44472f), the last results path's debrief word 0x48419e */
    es2_shell_movies movies;
    int16_t movie_group;
    int debrief_selected;
    int16_t debrief_word, debrief_group;
    /* menu.ini cd_check and the stream the start-up check keeps open */
    int cd_policy;
    FILE *cd_stream;
    /* menu.ini primary = allowed (configure-shell.ps1 -AllowPrimary); without
     * it native-r111 is refused */
    int allow_primary;
    /* The practice tables and the CD's archives, once per process */
    es2_host_practice practice;
    /* A practice launch whose DEBUG dialog (-@) waits for its answer */
    struct {
        int pending;
        es2_shell_practice_launch launch;
        es2_shell_buffer game;          /* the new game's SAV image */
        int asked;                      /* the flow's dialog (flow.debug) is shown by the presenter */
    } debug;
} es2_host;

/* Files of the game directory (the process's current directory). */
uint8_t *es2_host_read(const char *path, size_t *size);
int es2_host_write(const char *path, const uint8_t *data, size_t size);

/* Reports through ui.say (printf-style; at most 2047 bytes per call). */
void es2_host_say(es2_host *h, const char *format, ...);
/* The engine's failure ("! <reason>"). */
void es2_host_engine_error(es2_host *h);

/* menu.ini of args.menu_dir: game_dir, seed, cd_root, core, hangar,
 * chain_dir, cd_check, online_manual (and ES2_MENU_CHAIN_DIR, ES2_MENU_SEED).
 * 0 when it is missing, incomplete or names a bad policy. */
int es2_host_config(es2_host *h);
/* The primary installation rule and the game
 * directory: 1 when the process may go on, else the reason was said. */
int es2_host_check_directory(es2_host *h, const char *cwd);
/* The start-up CD check 0x401cac (1 = go on). */
int es2_host_cd_check(es2_host *h);
/* ESTEXT.BIN and WPN_DESC.BIN of the language (VOL\LANG0.VOL). */
void es2_host_labels_open(es2_host *h);
/* Game init 0x40e736 and the process start: the flow over the shared
 * callbacks (full_screen as given, NULL for a windowless presenter), the
 * movie flag, the engine (RNG draws from menu.ini's seed, ES2_MENU_SEED or
 * the tick count). */
void es2_host_start(es2_host *h, int (*full_screen)(void *ctx));
void es2_host_close(es2_host *h);

/* The slot table as VSHELL's start-up reader 0x40e380 leaves it (1 = read). */
int es2_host_slot_table(es2_host *h, es2_shell_slot_table *t);

/* The practice tables, a launch (new game in mode 0 and the posted Use
 * Default; returns ES2_FLOW_EXIT_DBSIM or 0) and the prefs write. */
int es2_host_practice_open(es2_host *h);
int es2_host_practice_launch(es2_host *h, const es2_shell_practice_launch *launch, int instant);
int es2_host_practice_write_prefs(es2_host *h, const uint8_t out[ES2_SHELL_PREFS_SIZE]);

/* The DEBUG dialog answered later. es2_host_practice_launch returns
 * ES2_HOST_PRACTICE_PENDING when ui.debug_choice returned
 * ES2_HOST_DEBUG_PENDING; the dialog's Use Default (typed NULL) or ACCEPT
 * (the field's text) then runs the preparation: ES2_FLOW_EXIT_DBSIM or 0
 * (no launch pending, a refused name or a failed preparation). */
enum { ES2_HOST_DEBUG_PENDING = 3, ES2_HOST_PRACTICE_PENDING = 0x100 };
int es2_host_practice_debug_answer(es2_host *h, const char *typed);
/* The DEBUG dialog of the mode-1 paths (flow.debug: the end of a
 * new career 0x41308e, the results path's advance 2 0x40f647) with -@.
 * es2_host_debug_ask, after every flow event: nothing waits or it was
 * answered at once (1; ui.debug_choice 1 or 2), the presenter shows it
 * (ES2_HOST_DEBUG_PENDING, answer with es2_host_debug_answer: typed NULL for
 * Use Default, else the field's text), or the end of input (0: the quit
 * flag is set). The default name is the CAREER entry of the memory state. */
int es2_host_debug_ask(es2_host *h);
es2_shell_flow_result es2_host_debug_answer(es2_host *h, const char *typed);
/* error.str's string `index` of group `group` (0 messages, 1
 * captions) as VSHELL's start-up reader keeps it (the game directory's
 * error.str, else the CD's ERROR.STR); 0 and an empty text when the slot is
 * not filled. */
int es2_host_error_text(es2_host *h, int group, int index, char *out, size_t capacity);
/* The language directory of VSHELL's language word 0x48235e
 * (0x408bb5): ENG, FRE, GER for 0, 1, 2, "" (no prefix) for any other. */
const char *es2_host_language_dir(int language);

#endif
