#ifndef ES2_SHELL_CAMPAIGN_H
#define ES2_SHELL_CAMPAIGN_H

#include "es2/shell/prepare.h"

/* Campaign advancement after a mission: VSHELL's results path 0x40f09f with
 * its debrief selection (vshell_preparation.md, "Results path" and
 * "Debrief selection"). Input is a SAV as VSHELL restores it, the returned
 * mission.var and results.dat; output is the SAV the original writers would
 * produce from the resulting state, the next CAREER position and the debrief.
 * No file I/O or process-global state; outputs are unchanged on failure. */

/* Executable tables of VSHELL.EXE, read at run time from the span store
 * (vshell_store.h, generated and verified against the file), concatenated
 * in this order: part groups (48 bytes, VA 0x46f9e4),
 * repair thresholds (12, 0x46fec4), repair levels (12, 0x46feb8), unlock
 * values (66, 0x46fb80), unlock variables (66, 0x46fbc2), item IDs of
 * variables 21..49 (58, 0x46fa20), item index list (60, 0x46f9a8). */
#define ES2_SHELL_RESULTS_TABLES_SIZE 322u

typedef struct es2_shell_results_input {
    es2_span save, variables, results; /* SAV; mission.var (2,000); results.dat */
    es2_span tables;                   /* ES2_SHELL_RESULTS_TABLES_SIZE bytes */
    const char *language;              /* ENG, FRE or GER (NULL: GER) */
    int16_t mode;                      /* shell mode word 0x4826f0 */
    uint8_t repair_option, restock_option; /* option bytes 0x4825c8/0x4825c9 */
    es2_shell_rng rng;                 /* state at 0x482409 */
    int set_position;                  /* nonzero: replace the SAV position */
    int16_t group, position;
} es2_shell_results_input;

enum {
    ES2_SHELL_RESULTS_PLAYER_LOST = 3, /* player +0x2b reached 0: final word 3 */
    ES2_SHELL_RESULTS_OTHER_MODE = 4,  /* mode != 1: path ends before advancing */
    ES2_SHELL_RESULTS_ADVANCED = 5     /* advance holds 0x4133eb's return value */
};

typedef struct es2_shell_results_output {
    es2_shell_buffer save;             /* SAV image of the resulting state */
    es2_shell_buffer strings;          /* MISSION.STR rewritten by the debrief reader */
    es2_shell_buffer text;             /* composed debrief text (return 2 only) */
    es2_shell_rng rng;
    int ending, advance;               /* advance is 0, 1 or 2 (or -1) */
    int16_t group, position, final_word, lost;
    uint32_t total;
    int debrief;                       /* nonzero when the debrief was selected */
    es2_shell_debrief selected;        /* its strings field is not used */
    int16_t added;                     /* items added from variables 21..49 */
} es2_shell_results_output;

es2_status es2_shell_results(const es2_vfs *vfs, const es2_shell_results_input *in,
    es2_shell_results_output *out, es2_error *error);
void es2_shell_results_free(es2_shell_results_output *out);

/* The shell options in VSHELL's preferences file data\prefs.cfg (54
 * bytes, VA 0x48259c; vshell_preparation.md, "shell options"). DBSIM
 * reads the same file but never these three bytes. */
#define ES2_SHELL_PREFS_SIZE 54u
enum {
    ES2_SHELL_PREFS_MODE = 0x2a,   /* copied into the mode word 0x4826f0 at start-up */
    ES2_SHELL_PREFS_REPAIR = 0x2c, /* 0x4825c8, "Repair Options" */
    ES2_SHELL_PREFS_BUILD = 0x2d   /* 0x4825c9, "Weapons Building" */
};
enum { ES2_SHELL_MODE_SINGLE = 0, ES2_SHELL_MODE_CAMPAIGN = 1 }; /* main menu entries */
enum { ES2_SHELL_REPAIR_AUTO_ALL = 0, ES2_SHELL_REPAIR_MANUAL_MINE = 1, ES2_SHELL_REPAIR_MANUAL_ALL = 2 };
enum { ES2_SHELL_BUILD_AUTO = 0, ES2_SHELL_BUILD_MANUAL = 1 };

/* The block as VSHELL loads it at start-up: zero, then the first (up to) 54
 * bytes of the file; file NULL (or no data) is a file that does not open. */
void es2_shell_prefs_load(const es2_span *file, uint8_t prefs[ES2_SHELL_PREFS_SIZE]);
/* Mode, repair and weapons-building options a restarted VSHELL hands to the
 * results path; the other fields of *in are left alone. */
void es2_shell_prefs_options(const uint8_t prefs[ES2_SHELL_PREFS_SIZE], es2_shell_results_input *in);
/* The bytes the UI controls store: mode 0/1, repair 0..2, build 0..1; -1
 * keeps a byte. Other values are rejected before anything changes. */
es2_status es2_shell_prefs_choose(uint8_t prefs[ES2_SHELL_PREFS_SIZE], int mode, int repair, int build,
    es2_error *error);

/* The preferences blocks of one VSHELL process (vshell_ui.md section
 * 11.3): `current` (0x48259c) is what every control changes and every
 * reader uses, `committed` (0x4825d2) what Cancel returns to. No file I/O:
 * the functions return the 54 bytes VSHELL writes, the caller writes them
 * (a truncating write). */
typedef struct es2_shell_prefs_session {
    uint8_t current[ES2_SHELL_PREFS_SIZE], committed[ES2_SHELL_PREFS_SIZE];
} es2_shell_prefs_session;
/* Start-up 0x40dc44: both blocks as es2_shell_prefs_load gives them. */
void es2_shell_prefs_session_open(const es2_span *file, es2_shell_prefs_session *session);
/* A single-byte store with the read-modify-write 0x40df90/0x40dfa3 (the
 * main menu's mode store 0x40ec56 is index 0x2a): current[index] := value;
 * the written image starts as a copy of the current block, the first (up
 * to) 54 bytes of the file replace its start when the file opens, then byte
 * `index` is the current one again. `file` NULL or without data: no file.
 * The committed block is not touched. */
es2_status es2_shell_prefs_store(es2_shell_prefs_session *session, const es2_span *file, size_t index,
    uint8_t value, uint8_t out[ES2_SHELL_PREFS_SIZE], es2_error *error);
/* Controls of the Preferences screen 0x435598 (each changes `current` only). */
enum {
    ES2_SHELL_PREFS_MUSIC_TOGGLE = 1, /* 0x437351: byte 0 ^= 1 (and CD music on/off) */
    ES2_SHELL_PREFS_SFX_TOGGLE,       /* 0x4373b2: byte 1 ^= 1 */
    ES2_SHELL_PREFS_HIGH_RES,         /* 0x437758: byte 4 := 0 */
    ES2_SHELL_PREFS_LOW_RES,          /* 0x4376f7: byte 4 := 1 */
    ES2_SHELL_PREFS_WINDOW,           /* 0x437597: byte 6 := 0 */
    ES2_SHELL_PREFS_FULL_SCREEN,      /* 0x437608 -> Alert box, its ACCEPT 0x437678: byte 6 := 1 */
    ES2_SHELL_PREFS_REPAIR_0,         /* 0x437413 / 0x437474 / 0x4374d5: byte 0x2c := 0 / 1 / 2 */
    ES2_SHELL_PREFS_REPAIR_1,
    ES2_SHELL_PREFS_REPAIR_2,
    ES2_SHELL_PREFS_BUILD_0,          /* 0x437536 / 0x437696: byte 0x2d := 0 / 1 */
    ES2_SHELL_PREFS_BUILD_1
};
es2_status es2_shell_prefs_control(es2_shell_prefs_session *session, int control, es2_error *error);
/* ACCEPT 0x4372e1: committed := current (0x40dd6e), then the writer 0x40dd0a
 * writes the whole current block (out). */
void es2_shell_prefs_accept(es2_shell_prefs_session *session, uint8_t out[ES2_SHELL_PREFS_SIZE]);
/* CANCEL 0x437220: current := committed (0x40ddb6), every byte, including
 * bytes other screens stored since the last commit (e.g. the mode byte). No
 * file write. */
void es2_shell_prefs_cancel(es2_shell_prefs_session *session);

/* The launch checks of Rock & Roll 0x445b99 (vshell_ui.md sections
 * 4.8 and 11.1) for the memory state `save` describes. The box index is
 * VSHELL's 0x44d90c argument; its two text lines are ESTEXT entries
 * ES2_SHELL_WARNING_LINE1 + 2 * n and + 1, the title 0x13b. */
enum {
    ES2_SHELL_LAUNCH_OK = -1,
    ES2_SHELL_LAUNCH_NOT_FUNCTIONAL = 0, /* 0x4110c1 failed */
    ES2_SHELL_LAUNCH_UNARMED = 1,        /* 0x411135 failed */
    ES2_SHELL_LAUNCH_NO_HERC = 2,        /* player bay 0x482b82 = -1 */
    ES2_SHELL_LAUNCH_SQUAD_UNARMED = 3   /* 0x40fc7e failed */
};
enum { ES2_SHELL_WARNING_TITLE = 0x13b, ES2_SHELL_WARNING_LINE1 = 0x13c };
typedef struct es2_shell_launch_check_result {
    int16_t player_bay;
    /* Answers 1/0 of 0x4110c1 and 0x411135 for the player's bay (-1 when
     * the bay is -1: Rock & Roll does not call them then) and of 0x40fc7e
     * (-1 when it would read outside the hangar: an active squad pilot whose
     * bay is -1; an error only when Rock & Roll reaches that check). */
    int functional, armed, squad_armed;
    int warning;  /* ES2_SHELL_LAUNCH_OK or the box index */
} es2_shell_launch_check_result;
es2_status es2_shell_launch_check(es2_span save, es2_shell_launch_check_result *out, es2_error *error);

/* The condition helpers of a hangar record (vshell_preparation.md
 * "Condition helpers", vshell_ui.md section 36). `words` are the record's 33
 * condition words (memory +0x08, SAV fixed bytes 4..69), signed.
 *
 * 0x4121f8(rec + 8, capacity): the 16-bit wrapping sum of words 0..12, 13..21
 * and 23 .. 22 + capacity, sign-extended and divided (idiv, truncating) by
 * capacity + 22. Refused (ES2_ERR_ARGUMENT) for a capacity outside 0..10
 * (the SAV readers accept no other; VSHELL would read past the words or, at
 * -22, divide by zero). */
es2_status es2_shell_overall_condition(const int16_t words[33], int16_t capacity, int16_t *out, es2_error *error);
/* The same over a SAV hangar record's 76 fixed bytes (words at +4, the
 * capacity at +74, memory +0x4c). */
es2_status es2_shell_record_condition(es2_span fixed, int16_t *out, es2_error *error);
/* 0x41232a(rec + 8, family, index): family 0 the signed average of the words
 * the part group lists (`groups`: the 24 words at 0x46f9e4, the first 48
 * bytes of the results tables; four per group, a list ends at -1), family 1
 * word 13 + index, family 2 word 23 + index. Refused when a word index falls
 * outside 0..32 or a group list is empty or unterminated. */
es2_status es2_shell_condition(const int16_t groups[24], const int16_t words[33], int16_t family, int16_t index,
                               int16_t *out, es2_error *error);

#endif
