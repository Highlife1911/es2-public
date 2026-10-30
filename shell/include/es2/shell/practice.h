#ifndef ES2_SHELL_PRACTICE_H
#define ES2_SHELL_PRACTICE_H

#include "es2/shell/campaign.h"
#include "es2/shell/save.h"

/* Practice missions, INSTANT ACTION, VIEW DEMO and the DEBUG mission-file
 * dialog (vshell_ui.md section 15). The screens' state lives in the
 * prefs blocks of the process (es2_shell_prefs_session) and in two
 * globals (es2_shell_practice). A practice launch is VSHELL's new game
 * 0x40e8a5 in mode 0 followed by the DEBUG dialog's posted Use Default
 * 0x44db5c (a mode-0 preparation of CAREER group 0); es2_shell_practice_run
 * computes its four launch files and the SAV of the state after it. No file
 * I/O; on failure the state, the session and every output stay unchanged. */

/* Executable data of VSHELL.EXE (from the span store, see
 * vshell_store.h), concatenated in this order: the Herc type of each group-0
 * position (12 words, VA 0x479c56; only the low byte is used), the player
 * name of Begin Mission (8 bytes, 0x479f70) and of INSTANT ACTION (8 bytes,
 * 0x47396b), both NUL-terminated. */
#define ES2_SHELL_PRACTICE_TABLES_SIZE 40u
/* A span at virtual address `va` of VSHELL.EXE's data. With ES2_VSHELL_STORE
 * (vshell_store.h) from the span store: a read outside its runs is
 * ES2_ERR_NOT_FOUND. The own processes know no other image; any other
 * `image` goes to the file reader a test installed (es2_shell_set_file_reader:
 * the real tests that compare the store with the reference file), and is
 * ES2_ERR_ARGUMENT without one. Every successful read is noted for the
 * measurement switch ES2_SHELL_SPAN_LOG (off by default). */
es2_status es2_shell_exe_span(es2_span image, uint32_t va, size_t size, es2_span *out, es2_error *error);
/* The reader es2_shell_exe_span uses for an image other than the
 * store, NULL (the default, and the only state in es2shell and es2menu) for
 * none. Only tests install one (shell/tests/vshell_file.h: a PE section walk
 * over a file image held in memory). */
typedef es2_status (*es2_shell_file_reader)(es2_span image, uint32_t va, size_t size, es2_span *out,
    es2_error *error);
void es2_shell_set_file_reader(es2_shell_file_reader reader);
/* The 40 bytes above from ES2_VSHELL_STORE (or a test's file image). */
es2_status es2_shell_practice_tables(es2_span image, uint8_t out[ES2_SHELL_PRACTICE_TABLES_SIZE],
    es2_error *error);

/* Prefs bytes of the practice screen and of INSTANT ACTION (the mode-0
 * controls c1..c5 of the preparation, then the instant-action rotation). */
enum {
    ES2_SHELL_PREFS_AMMO = 0x25,       /* c1, header word 5: Limited / Unlimited (0x12a + v) */
    ES2_SHELL_PREFS_DAMAGE = 0x26,     /* c2, word 6: Vulnerable / Invulnerable (0x128 + v) */
    ES2_SHELL_PREFS_DIFFICULTY = 0x27, /* c3, word 7 and the player rank: ROOKIE..ELITE (0x35 + v) */
    ES2_SHELL_PREFS_HERC = 0x28,       /* c4, the preset loadout: Outlaw..Razor (0x6e + v) */
    ES2_SHELL_PREFS_TIME = 0x29,       /* c5, word 9: Day / Night (0x12c + v) */
    ES2_SHELL_PREFS_INSTANT = 0x2e     /* INSTANT ACTION: group-0 position 8 + v, then v + 1 mod 3 */
};
/* The five parameter buttons of the practice screen (handlers 0x44c676,
 * 0x44c5b9, 0x44c733, 0x44c8ad, 0x44c7f0). */
enum {
    ES2_SHELL_PRACTICE_AMMO, ES2_SHELL_PRACTICE_DAMAGE, ES2_SHELL_PRACTICE_DIFFICULTY,
    ES2_SHELL_PRACTICE_HERC, ES2_SHELL_PRACTICE_TIME
};
/* The mouse button whose release runs a text button's handler (0x409d8a;
 * event byte +0x24): left steps a control forward (0x40de5b), right back
 * (0x40de88). */
enum { ES2_SHELL_CLICK_LEFT = 1, ES2_SHELL_CLICK_RIGHT = 3 };
enum { ES2_SHELL_PRACTICE_ENTRIES = 8, ES2_SHELL_EXIT_DEMO = 5 };

/* The two globals: the selected group-0 position 0x479c54 (-1 in the image)
 * and the preset flag 0x473784 (0 in the image; INSTANT ACTION sets it and
 * nothing clears it, so for the rest of the process practice entries 4..7
 * no longer use the Herc Type preset loadout). */
typedef struct es2_shell_practice {
    int16_t index;
    uint32_t preset_flag;
    int herc_enabled;   /* the Herc Type button's +0x49 ([0x48dc1c]) */
} es2_shell_practice;
void es2_shell_practice_open(es2_shell_practice *practice);
/* Herc Type: disabled by entries 0..3, enabled by 4..7 (0x44c40c); positions
 * 8..10 leave it as it was. */
int es2_shell_practice_herc_enabled(const es2_shell_practice *practice);
/* The ESTEXT ID of a control's value label (base + the current byte, as the
 * handlers compute it; any byte value). */
unsigned es2_shell_practice_label(int control, const es2_shell_prefs_session *session);

/* PRACTICE MISSIONS 0x431f3a: the screen's show 0x44c322 selects entry 0
 * (es2_shell_practice_entry), then the main menu's mode store 0x40ec56(0)
 * (es2_shell_prefs_store of byte 0x2a; `out` is what it writes). */
es2_status es2_shell_practice_show(es2_shell_practice *practice, es2_shell_prefs_session *session,
    const uint8_t tables[ES2_SHELL_PRACTICE_TABLES_SIZE], const es2_span *file,
    uint8_t out[ES2_SHELL_PREFS_SIZE], es2_error *error);
/* An entry 0x44c40c(i), i = 0..7 (INSTANT ACTION: 8..10): nothing when i is
 * already selected (*changed 0); otherwise prefs +0x28 := the table's Herc
 * type of i and the selection := i. */
es2_status es2_shell_practice_entry(es2_shell_practice *practice, es2_shell_prefs_session *session,
    const uint8_t tables[ES2_SHELL_PRACTICE_TABLES_SIZE], int index, int *changed, es2_error *error);
/* A parameter button released with `click` (1 or 3): forward, v + 1 and 0
 * when that equals the modulus (2, 2, 4, 9, 2; a larger byte just grows), or
 * back, v - 1 and modulus - 1 when that is 0xff. Herc Type while disabled:
 * *applied 0, nothing changes. */
es2_status es2_shell_practice_control(es2_shell_practice *practice, es2_shell_prefs_session *session,
    int control, int click, int *applied, es2_error *error);

/* What a launch hands to es2_shell_practice_run. */
typedef struct es2_shell_practice_launch {
    char name[8];             /* player name of the new game ("TRAINEE" from the EXE) */
    uint16_t rank;            /* prefs +0x27 (the handler pushes the byte) */
    int16_t position;         /* CAREER group 0, position 0x479c54 (0x413053 in mode 0) */
    uint8_t controls[5];      /* prefs +0x25..+0x29 when the preparation runs */
    uint8_t preset_flag;      /* 0x473784 */
    int16_t preset_index;     /* 0x479c54 */
} es2_shell_practice_launch;
/* Begin Mission 0x44ca26: commit (0x40dd6e) and the writer 0x40dd0a (`out`,
 * the whole current block); the launch of the selected entry. Refused while
 * no entry is selected. Main Menu 0x44c96a has no state effect: the current
 * block keeps the controls uncommitted (a later Preferences CANCEL reverts
 * them, an ACCEPT writes them). */
es2_status es2_shell_practice_begin(const es2_shell_practice *practice, es2_shell_prefs_session *session,
    const uint8_t tables[ES2_SHELL_PRACTICE_TABLES_SIZE], uint8_t out[ES2_SHELL_PREFS_SIZE],
    es2_shell_practice_launch *launch, es2_error *error);
/* INSTANT ACTION 0x431935: preset flag := 1; the mode store 0x40ec56(0)
 * (`mode_out`, read-modify-write over `file`); 0x44c58b: entry 8 + prefs
 * +0x2e, +0x2e stepped forward modulo 3, commit, writer (`prefs_out`); the
 * launch. A byte +0x2e above 2 is refused before anything changes (the
 * original reads past the Herc type table and prepares a position group 0
 * does not have). */
es2_status es2_shell_instant_action(es2_shell_practice *practice, es2_shell_prefs_session *session,
    const uint8_t tables[ES2_SHELL_PRACTICE_TABLES_SIZE], const es2_span *file,
    uint8_t mode_out[ES2_SHELL_PREFS_SIZE], uint8_t prefs_out[ES2_SHELL_PREFS_SIZE],
    es2_shell_practice_launch *launch, es2_error *error);

/* DEBUG dialog ACCEPT 0x44dc4d: "msn\%s.msn" of the typed text with its first
 * '^' replaced by '_' (0x46b994, 0x44dc8f); the mission stem it prepares is
 * written to `stem` (capacity >= 32). Texts of 1..31 letters, digits, '_'
 * after that replacement are accepted. */
es2_status es2_shell_debug_mission(const char *text, char *stem, size_t capacity, es2_error *error);

/* The launch as VSHELL runs it: new game 0x40e8a5(name, rank) in mode 0
 * (es2_shell_new_game, whose hangar 0x410b11 leaves empty in mode 0; three
 * parts of the memory state are not touched by 0x40e8a5 and come from
 * `carry`, the SAV of the game the process held before, when there was one:
 * the briefing selection 0x4840cc (148 bytes), the nine-word bank 0x483c46
 * (18) and the 20 bytes at 0x4833ac; without `carry` they are a fresh
 * process's: zero, HERC_INF's words, zero), then
 * the posted Use Default 0x44db5c: es2_shell_prepare of CAREER (0, position)
 * (or of `mission`, the DEBUG dialog's ACCEPT) in mode 0 with the controls,
 * difficulty 0, the preset index and flag, and preset path c4 of
 * `preset_paths` (nine NUL-padded 64-byte paths of the pointer table
 * 0x47083c, es2prep's layout; may be empty when the branch is not taken).
 * *rng and selectors[30] are VSHELL's preparation RNG and reset selectors;
 * they advance on success. Outputs: the four launch files, the SAV right
 * after the new game and the SAV of the state after the preparation (what
 * the exit save 0x40e933(10) writes to slot 11 in mode 0). */
typedef struct es2_shell_practice_result {
    es2_shell_output files;
    es2_shell_buffer new_game, save;
} es2_shell_practice_result;
es2_status es2_shell_practice_run(const es2_vfs *vfs, const char *language,
    const es2_shell_save_tables *tables, es2_span preset_paths,
    const es2_shell_practice_launch *launch, const es2_span *carry, const char *mission,
    es2_shell_rng *rng, int16_t selectors[30], es2_shell_practice_result *out, es2_error *error);
void es2_shell_practice_result_free(es2_shell_practice_result *out);
/* The two halves of es2_shell_practice_run, for callers that hold the state
 * between them (INSTANT ACTION's own 0x40f68c runs in between, and with the
 * cheat byte -@ the dialog waits for Use Default or ACCEPT): the SAV after
 * the new game (*rng advances by its draws; `save` must be empty), and the
 * preparation of that SAV (`files` and `save` must be empty). */
es2_status es2_shell_practice_new_game(const es2_vfs *vfs, const char *language,
    const es2_shell_save_tables *tables, const es2_shell_practice_launch *launch,
    const es2_span *carry, es2_shell_rng *rng, es2_shell_buffer *save, es2_error *error);
es2_status es2_shell_practice_prepare(const es2_vfs *vfs, const char *language,
    const es2_shell_save_tables *tables, es2_span preset_paths,
    const es2_shell_practice_launch *launch, const char *mission, es2_span new_game,
    es2_shell_rng *rng, int16_t selectors[30], es2_shell_output *files, es2_shell_buffer *save,
    es2_error *error);
/* INSTANT ACTION's own 0x40f68c (0x4319aa), which runs before the posted
 * Use Default on the new game's state: mission.var = the (cleared) bank,
 * player.mec = word 0, the team count, the player's variant and rank, no
 * hangar record (the mode-0 hangar is empty: 0x410cdb writes nothing for a
 * bay without one), no squad (team capacity 0), then 33 and the 33
 * availability bytes. These are the files DBSIM gets when the cheat byte -@
 * keeps the dialog from posting its click. Other states (a hangar record, a
 * capacity above 1) are refused: es2_shell_launch_files covers them. Both
 * buffers must be empty. */
es2_status es2_shell_practice_new_game_files(es2_span new_game, es2_shell_buffer *player,
    es2_shell_buffer *variables, es2_error *error);
/* The SAV of the state after a mode-0 preparation of `base` (the slots' open
 * case): base's writer image with the team words, the roster and player
 * assignments (bay, active, order), the roster selection and every hangar
 * record taken from `after` (es2_shell_prepare's state), and the variable
 * bank = the prepared mission.var. Same output/size convention as save.h. */
es2_status es2_shell_mode0_prepared_save(es2_span base, const es2_shell_save_tables *tables,
    const es2_shell_state *after, const es2_shell_output *prepared, uint8_t *output,
    size_t capacity, size_t *required, es2_error *error);

#endif
