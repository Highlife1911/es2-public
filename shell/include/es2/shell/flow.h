#ifndef ES2_SHELL_FLOW_H
#define ES2_SHELL_FLOW_H

/* The shell flow as a front-end-independent state machine.
 *
 * The flow is VSHELL's screen logic without its drawing: which screen and
 * which modal box are shown, the service-bay screen word 0x4758b8 and its
 * leave dispatch 0x43a537, the handlers' effects on the process state, the
 * boxes' decisions, the -X entry dispatch 0x40188a and the exit save of the
 * loop end 0x402197. It does no file I/O and prints nothing: every effect
 * that touches a file or the engine's CD tables goes through the host's
 * callbacks (the file layer in shell/src/host.c over engine_io.h, shared
 * by both presenters). A presenter shows
 * flow.screen, flow.bay_word and flow.box, turns the player's input into
 * events and calls es2_shell_flow_event.
 *
 * Events are VSHELL's handlers, named by their addresses
 * (shell/data/vshell_layout.json, vshell_ui.md sections 4.1 to 4.10), so a
 * graphical presenter feeds the control's handler from the layout table and
 * the text presenter feeds the same values from its numbered choices.
 *
 * Contract: docs/reverse-engineering/vshell_ui.md, section 18; the state
 * model is vshell_state.md. */
#include "es2/shell/campaign.h"
#include <stddef.h>
#include <stdint.h>

/* Top-level screens (vshell_ui.md section 3). */
typedef enum es2_shell_flow_screen {
    ES2_FLOW_MAIN_MENU = 0,     /* 0x430fc4 */
    ES2_FLOW_REGISTRATION,      /* 0x43bd2e */
    ES2_FLOW_SAVED_GAMES,       /* 0x438c40 (from the main menu or the bay) */
    ES2_FLOW_PREFERENCES,       /* 0x435598 */
    ES2_FLOW_PRACTICE,          /* 0x44b310 */
    ES2_FLOW_SERVICE_BAY        /* 0x43afd4; its sub-screen is bay_word */
} es2_shell_flow_screen;

/* The screen word 0x4758b8 (vshell_ui.md section 1). */
enum {
    ES2_FLOW_WORD_BAY = -1,     /* the bay alone (bay show 0x43b758) */
    ES2_FLOW_WORD_NONE = 0,     /* bay hidden by MAIN MENU */
    ES2_FLOW_WORD_SAVE = 1, ES2_FLOW_WORD_WEAPONS = 2, ES2_FLOW_WORD_REPAIR = 3, ES2_FLOW_WORD_BUILD = 4,
    ES2_FLOW_WORD_ARMORY = 5, ES2_FLOW_WORD_CREW = 6, ES2_FLOW_WORD_MISSION = 7
};

/* Modal boxes (vshell_ui.md section 4.10); box_value is the box's text
 * selector. */
typedef enum es2_shell_flow_box {
    ES2_FLOW_BOX_NONE = 0,
    ES2_FLOW_BOX_END_OF_GAME,   /* 0x44d2bc; value: the final word (0, 1, 3, other: no line) */
    ES2_FLOW_BOX_WARNING,       /* 0x44d66c; value: the launch warning index n (lines 0x13c + 2n) */
    ES2_FLOW_BOX_REPLAY,        /* 0x44cdac; value: 3 killed, 0 war lost */
    ES2_FLOW_BOX_ALERT          /* Preferences' Full Screen alert (0x437608) */
} es2_shell_flow_box;

/* What the process holds as the loaded game (0x4826ee with the own
 * menu's distinction): nothing, a campaign in the service bay (final word
 * 2, its mission prepared), or a game loaded without one (a finished
 * career, a practice game). */
typedef enum es2_shell_flow_game {
    ES2_FLOW_GAME_NONE = 0, ES2_FLOW_GAME_CAMPAIGN, ES2_FLOW_GAME_SLOT
} es2_shell_flow_game;

/* Events: VSHELL handler addresses. */
typedef enum es2_shell_flow_handler {
    /* main menu 0x430fc4 */
    ES2_FLOW_INSTANT_ACTION = 0x431935,
    ES2_FLOW_START_NEW_GAME = 0x431a08,
    ES2_FLOW_CONTINUE_GAME = 0x431a73,
    ES2_FLOW_SAVE_RESTORE = 0x431b27,
    ES2_FLOW_PREFERENCES_BUTTON = 0x431b9b,
    ES2_FLOW_VIEW_DEMO = 0x431bfe,
    ES2_FLOW_QUIT = 0x431db6,
    ES2_FLOW_PRACTICE_MISSIONS = 0x431f3a,
    ES2_FLOW_ONLINE_MANUAL = 0x431e1b,        /* leads to the help routine 0x431e79 */
    /* Quit box 0x431fa8 (window close, Escape; the text menu's end of input) */
    ES2_FLOW_QUIT_ACCEPT = 0x432237,
    /* Registration 0x43bd2e */
    ES2_FLOW_SKILL = 0x43c6ad,
    ES2_FLOW_REGISTRATION_CANCEL = 0x43c728,
    ES2_FLOW_REGISTRATION_ACCEPT = 0x43c78b,  /* text: the typed name */
    /* Saved games 0x438c40 (the row selection is es2_shell_flow_select_slot, 0x437fef) */
    ES2_FLOW_SLOT_SAVE = 0x438263,
    ES2_FLOW_SLOT_RESTORE = 0x438393,
    ES2_FLOW_SLOT_EXIT = 0x438424,
    ES2_FLOW_SLOT_CANCEL = 0x4384aa,
    ES2_FLOW_SLOT_ACCEPT = 0x43868a,          /* text: the row text */
    /* Preferences 0x435598 (the controls are es2_shell_flow_prefs_control) */
    ES2_FLOW_PREFS_CANCEL = 0x437220,
    ES2_FLOW_PREFS_ACCEPT = 0x4372e1,
    ES2_FLOW_ALERT_ACCEPT = 0x437678,
    /* Practice 0x44b310 (entries and controls are es2/shell/practice.h) */
    ES2_FLOW_PRACTICE_MAIN_MENU = 0x44c96a,
    /* service bay bar 0x43afd4 */
    ES2_FLOW_BAY_MAIN_MENU = 0x43a66c,
    ES2_FLOW_BAY_SAVE = 0x43a768,
    ES2_FLOW_BAY_WEAPONS = 0x43a862,
    ES2_FLOW_BAY_REPAIR = 0x43a962,
    ES2_FLOW_BAY_BUILD = 0x43aa62,
    ES2_FLOW_BAY_ARMORY = 0x43ab62,
    ES2_FLOW_BAY_CREW = 0x43ac5a,
    ES2_FLOW_BAY_MISSION = 0x43ad5a,
    ES2_FLOW_BAY_ONLINE = 0x43a5bd,           /* the online image, the same help routine */
    /* mission screen 0x442bc4 */
    ES2_FLOW_ROCK_AND_ROLL = 0x445b99,
    /* boxes */
    ES2_FLOW_REPLAY_YES = 0x44d1d4,
    ES2_FLOW_REPLAY_NO = 0x44d24d,
    ES2_FLOW_END_OKAY = 0x44d60b,
    ES2_FLOW_WARNING_OKAY = 0x44da94
} es2_shell_flow_handler;

/* The answer of es2_shell_flow_event. */
typedef enum es2_shell_flow_result {
    ES2_FLOW_DONE = 0,     /* the handler ran */
    ES2_FLOW_IGNORED,      /* VSHELL does nothing (same bar button, disabled control, other screen) */
    ES2_FLOW_REFUSED,      /* the own menu's refusal: no game / no campaign / no slot (es2_shell_flow.refusal) */
    ES2_FLOW_FAILED        /* a host callback failed (the host reported it) */
} es2_shell_flow_result;

/* The entry dispatch 0x40188a. */
typedef enum es2_shell_flow_entry {
    ES2_FLOW_ENTRY_INTRO = 0,  /* -X0, 1, 2, 5, other: intro movies, main menu */
    ES2_FLOW_ENTRY_RESULTS,    /* -X3, -X4: slot 10, results path 0x40f09f */
    ES2_FLOW_ENTRY_MENU        /* -X6: main menu without the intro */
} es2_shell_flow_entry;

/* How the results path ended (presenter's text). */
typedef enum es2_shell_flow_outcome {
    ES2_FLOW_OUTCOME_NONE = 0,
    ES2_FLOW_OUTCOME_NO_SLOT,      /* slot 10 (11) held no game: main menu */
    ES2_FLOW_OUTCOME_REJECTED,     /* the host rejected the results: main menu */
    ES2_FLOW_OUTCOME_REPLAY,       /* lost: the Replay box */
    ES2_FLOW_OUTCOME_NEXT,         /* advance 2: next mission prepared, service bay */
    ES2_FLOW_OUTCOME_NEXT_FAILED,  /* advance 2, the preparation failed: main menu */
    ES2_FLOW_OUTCOME_WON,          /* advance 1: slot 10 saved, End box (final word 1), main menu */
    ES2_FLOW_OUTCOME_OTHER_MODE,   /* mode 0: main menu */
    ES2_FLOW_OUTCOME_STOPPED       /* any other advance value (outcome_advance): main menu */
} es2_shell_flow_outcome;

/* What the host's results path returns (es2_shell_results_output's words). */
typedef struct es2_shell_flow_results {
    int ending;            /* ES2_SHELL_RESULTS_* */
    int advance;           /* 0x4133eb's value with ES2_SHELL_RESULTS_ADVANCED */
    int16_t final_word;
} es2_shell_flow_results;

/* Where the DEBUG dialog 0x413305 waits with -@ (without it VSHELL
 * posts the Use Default click and the flow prepares at once). */
typedef enum es2_shell_flow_debug {
    ES2_FLOW_DEBUG_NONE = 0,
    ES2_FLOW_DEBUG_CAREER,   /* the end of a mode-1 new career, 0x413053 -> 0x41308e */
    ES2_FLOW_DEBUG_NEXT      /* the results path's advance 2, 0x40f647 */
} es2_shell_flow_debug;

/* Effects with I/O. Every callback gets host.ctx; 1 = success unless said
 * otherwise. The flow updates its own state around the calls. */
typedef struct es2_shell_flow_host {
    void *ctx;
    /* A main-menu entry's mode store 0x40ec56: prefs +0x2a written through
     * the single-byte read-modify-write 0x40dfa3 (flow.mode is set first). */
    int (*store_mode)(void *ctx, uint8_t mode);
    /* Preferences ACCEPT 0x4372e1: all 54 bytes (0x40dd0a). */
    int (*write_prefs)(void *ctx, const uint8_t block[ES2_SHELL_PREFS_SIZE]);
    /* The save 0x40e933(slot, text) of the loaded game (text NULL: keep the
     * slot text); slot 10 is 11 in mode 0. */
    int (*save)(void *ctx, int slot, const char *text);
    /* The load 0x40eaaa(slot, flag): 1 loaded (*final_word the slot's), 0
     * error, -1 the slot holds no game (nothing changes). */
    int (*load)(void *ctx, int slot, int16_t *final_word);
    /* Registration ACCEPT: 0x40e8a5(name, rank) and the Use Default
     * preparation. 1 done; 0 failed with nothing loaded; -1 failed with a
     * game loaded (*final_word its final word). */
    int (*new_game)(void *ctx, const char *name, uint16_t rank, int16_t *final_word);
    /* After a load with final word 2: the loaded slot becomes the campaign
     * (its launch files, or its CAREER mission prepared). */
    int (*campaign)(void *ctx);
    /* Rock & Roll 0x445b99: checks and 0x40f68c. 1 launched; 0 refused
     * (*warning the box index) or failed (*warning ES2_SHELL_LAUNCH_OK). */
    int (*launch)(void *ctx, int *warning);
    /* 0x412a37 with the weapons-building byte, run when WEAPONS, REPAIR or
     * BUILD is left. */
    int (*rebalance)(void *ctx, uint8_t build);
    /* The results path 0x40f09f after the slot-10 load. */
    int (*results)(void *ctx, es2_shell_flow_results *out);
    /* The DEBUG dialog's Use Default after a results path with advance 2. */
    int (*prepare_next)(void *ctx);
    /* The Replay box's Yes after its slot-10 load (evidence log only; may be NULL). */
    void (*replay)(void *ctx);
    /* The help routine 0x431e79 after its prefs write: the sound focus
     * is given up (0x42f8ff) and the manual shown (language.cfg ->
     * es2guide.hlp; shell/src/win/help.h). A presenter with a full-screen
     * display leaves it at the start of the event (0x4077df, before the
     * write). May be NULL. */
    void (*online_manual)(void *ctx);
    /* The display word 0x481f4c, read by Preferences' Full Screen
     * 0x437608 (0x437618..0x437628): nonzero = the display is full screen
     * now, and the handler does nothing. The display itself is the
     * presenter's (the toggle 0x407636). NULL: windowed (the text menu). */
    int (*full_screen)(void *ctx);
    /* With debug_cheat the answer of the waiting DEBUG dialog
     * (flow.debug): Use Default 0x44db5c (mission NULL: the CAREER entry of
     * the memory state) or ACCEPT 0x44dc4d (the stem of
     * es2_shell_debug_mission, msn\<stem>.msn), mode 1. 1 prepared. */
    int (*debug_prepare)(void *ctx, const char *mission);
} es2_shell_flow_host;

typedef struct es2_shell_flow {
    es2_shell_flow_host host;
    /* process state (vshell_state.md) */
    int16_t mode;                   /* 0x4826f0 */
    int16_t final_word;             /* 0x4826f2 */
    es2_shell_flow_game game;       /* 0x4826ee */
    es2_shell_prefs_session prefs;  /* 0x48259c / 0x4825d2 */
    /* screens */
    es2_shell_flow_screen screen;
    int bay_word;                   /* 0x4758b8, image -1 */
    int caller;                     /* 0x48d424: 0 main menu, 8 bay */
    int slot;                       /* selected slot 0x474fde, image 10 */
    int naming;                     /* naming mode 0x474fdc, image 0 */
    int skill;                      /* Registration skill 0x476248, image 0 */
    int bay_select;                 /* bay selection 0x482bc9 (hangar word +0x22), 0 after a new game or load */
    int page;                       /* mission-screen page word 0x481108, 0 (Registration ACCEPT 0; the results path's advance 2: 4 (0x40f62d)) */
    es2_shell_flow_box box;
    int box_value;
    /* process end */
    int quit;                       /* quit flag 0x46c268 */
    int exit_code;                  /* 0x46e348 */
    /* the last results path and the last refusal, for the presenter */
    es2_shell_flow_outcome outcome;
    int outcome_advance;
    int refusal;                    /* ES2_FLOW_NO_GAME, ES2_FLOW_NO_CAMPAIGN, ES2_FLOW_NO_SLOT_GAME */
    /* The cheat byte 0x482368 (-@, set by the host) and the DEBUG
     * dialog 0x413305 waiting for its answer on a mode-1 path */
    int debug_cheat;
    es2_shell_flow_debug debug;
} es2_shell_flow;

enum { ES2_FLOW_NO_GAME = 1, ES2_FLOW_NO_CAMPAIGN, ES2_FLOW_NO_SLOT_GAME };
enum { ES2_FLOW_EXIT_QUIT = 0, ES2_FLOW_EXIT_DBSIM = 2, ES2_FLOW_EXIT_DEMO = 5 };
enum { ES2_FLOW_SLOT_CAMPAIGN = 10, ES2_FLOW_USER_SLOTS = 10, ES2_FLOW_CALLER_MENU = 0, ES2_FLOW_CALLER_BAY = 8 };

/* Game init 0x40e736: the prefs blocks from prefs.cfg (file NULL: absent),
 * the mode word from byte +0x2a, final word 0, no game; the screen globals
 * at their image values; the main menu. */
void es2_shell_flow_init(es2_shell_flow *flow, const es2_shell_flow_host *host, const es2_span *prefs_file);
/* The entry kind of a -X value (0x40188a). */
es2_shell_flow_entry es2_shell_flow_entry_of(int state);
/* The entry dispatch: -X3/-X4 load slot 10 and run the results path (with
 * its decisions: Replay box, next mission and bay, End box, main menu);
 * the others show the main menu. Returns the entry kind. */
es2_shell_flow_entry es2_shell_flow_start(es2_shell_flow *flow, int state);
/* One handler. text: the typed name (Registration ACCEPT) or row text
 * (Saved games ACCEPT); NULL otherwise. */
es2_shell_flow_result es2_shell_flow_event(es2_shell_flow *flow, es2_shell_flow_handler event, const char *text);
/* Whether a control is enabled now (bay bar by mode 0x43b758; Saved games
 * SAVE 0x43807b and RESTORE 0x438126 for the selected slot, flag = its
 * slot-table flag; SAVE, RESTORE and EXIT are disabled and ACCEPT and
 * CANCEL enabled only while naming). */
int es2_shell_flow_enabled(const es2_shell_flow *flow, es2_shell_flow_handler event, int flag);
/* Saved games row selection 0x437fef: the selected slot (0..9). */
void es2_shell_flow_select_slot(es2_shell_flow *flow, int slot);
/* A Preferences control (es2_shell_prefs_control's ids); Full Screen
 * shows the Alert box instead, whose ACCEPT stores the byte, and only while
 * windowed (host.full_screen; in full screen it is IGNORED). */
es2_shell_flow_result es2_shell_flow_prefs_control(es2_shell_flow *flow, int control);
/* Practice and INSTANT ACTION (es2/shell/practice.h): the host runs
 * the practice library and its writes; the flow records the new game of
 * 0x40e8a5 in mode 0 (a game loaded without a campaign, final word 2) and
 * the launch (exit 2, quit flag). */
void es2_shell_flow_game_created(es2_shell_flow *flow);
void es2_shell_flow_dbsim(es2_shell_flow *flow);
/* The answer of the waiting DEBUG dialog (flow.debug): Use Default
 * (mission NULL) or ACCEPT (a stem); host.debug_prepare, then the service
 * bay (0x44dba3..0x44dbd4 / 0x44dca9..0x44dcd9: mode 1 never launches).
 * A failed preparation: main menu (outcome NEXT_FAILED on the results
 * path). IGNORED when no dialog waits. */
es2_shell_flow_result es2_shell_flow_debug_answer(es2_shell_flow *flow, const char *mission);
/* A box hidden without its button (the text presenter's end of input). */
void es2_shell_flow_box_close(es2_shell_flow *flow);
/* The loop end 0x402197: slot 10 (11 in mode 0) saved when a game is
 * loaded. Returns the exit code. */
int es2_shell_flow_finish(es2_shell_flow *flow);

#endif
