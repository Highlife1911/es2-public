#ifndef ES2_SHELL_ENGINE_H
#define ES2_SHELL_ENGINE_H

#include "es2/shell/campaign.h"
#include "es2/shell/hangar.h"
#include "es2/shell/save.h"

/* The state VSHELL holds in one process, and the operations of the own
 * shell on it (docs/reverse-engineering/launcher.md, "Shell state engine";
 * the globals and their lifetimes are vshell_state.md).
 *
 * The memory state is a SAV image as the original writers would emit it for
 * VSHELL's memory (every item list head first: es2_shell_prepared_save,
 * es2_shell_loaded_save and es2_shell_service_bay produce and take this
 * form). Next to it the engine keeps the loaded flag 0x4826ee, the mode word
 * 0x4826f0, the final word 0x4826f2, the preparation RNG 0x482409 and the
 * thirty reset selectors 0x48553e. Everything else VSHELL carries between
 * processes is in files (SAV\, DATA\, prefs.cfg), which the caller reads and
 * writes: the engine does no file I/O. Outputs are unchanged on failure. */

/* Executable tables of VSHELL.EXE, read at run time through es2_shell_exe_span
 * (the own process passes ES2_VSHELL_STORE, vshell_store.h): `prepare` as
 * es2_shell_options.default_tables plus the
 * nine preset paths (0x47f850 112, 0x46f72c 16, 0x46f868 36, then the
 * 64-byte NUL-padded strings the pointers at 0x47083c name), `results` as
 * es2_shell_results_input.tables (ES2_SHELL_RESULTS_TABLES_SIZE), the 27
 * item IDs of the WEAPONS list rows (0x476a4c) and the nine ARM file names
 * the pointers at 0x47006c name, in that order (the hardpoint tables
 * 0x41501f indexes by the hangar record's vehicle word). */
#define ES2_SHELL_ENGINE_PREPARE_TABLES 740u
#define ES2_SHELL_WEAPON_ROWS 27
typedef struct es2_shell_engine_tables {
    uint8_t prepare[ES2_SHELL_ENGINE_PREPARE_TABLES];
    uint8_t results[ES2_SHELL_RESULTS_TABLES_SIZE];
    int16_t rows[ES2_SHELL_WEAPON_ROWS];
    char arm[9][32];
} es2_shell_engine_tables;
es2_status es2_shell_engine_tables_read(es2_span exe, es2_shell_engine_tables *out, es2_error *error);

/* The ARM tables: for vehicle v and item i, bit s of slots[v][i] is set when
 * the item's group holds a record for slot s (the record's first word is the
 * slot plus two; group 0 lists every slot). es2_shell_arm_parse reads one
 * ARM file: 44 bytes of two images, a group count, then per group the item,
 * a record count and that many 22-byte records; the whole file is used. */
typedef struct es2_shell_hardpoints {
    uint16_t slots[9][33];
} es2_shell_hardpoints;
es2_status es2_shell_arm_parse(es2_span arm, uint16_t slots[33], es2_error *error);
es2_status es2_shell_hardpoints_read(const es2_vfs *vfs, const es2_shell_engine_tables *tables,
    es2_shell_hardpoints *out, es2_error *error);

typedef struct es2_shell_engine {
    const es2_vfs *vfs;                     /* the CD archives (ZONES, SHELL0, LANG0) */
    const es2_shell_engine_tables *tables;
    const es2_shell_hardpoints *hardpoints;
    char language[4];                       /* ENG, FRE or GER */
    int lowest_free;                        /* hangar policy, see es2_shell_engine_prepare */
    es2_shell_buffer memory;                /* SAV image of the state; empty while nothing is loaded */
    int loaded;                             /* 0x4826ee */
    int16_t mode;                           /* 0x4826f0 */
    int16_t final_word;                     /* 0x4826f2 */
    uint16_t seed;                          /* the draw count of this process start */
    es2_shell_rng rng;                      /* 0x482409 */
    int16_t selectors[30];                  /* 0x48553e */
} es2_shell_engine;

/* A process start (vshell_state.md; vshell_preparation.md, "RNG use in the
 * menus and the start-up seed"): nothing loaded, final word 0, the selectors
 * zero (BSS), and the RNG reset with `draws` draws (0x407b42: GetTickCount()
 * & 127, which the caller supplies). The mode word is the caller's (prefs
 * +0x2a, 0x40e751). vfs, tables, hardpoints, language and lowest_free must be
 * set before; the memory buffer is released. */
es2_status es2_shell_engine_start(es2_shell_engine *engine, uint16_t draws, es2_error *error);
void es2_shell_engine_free(es2_shell_engine *engine);
/* Replaces the memory state by a copy of `sav` (loaded, final word as stored
 * in it); used by practice and instant action, whose new game is
 * es2/shell/practice.h. */
es2_status es2_shell_engine_set(es2_shell_engine *engine, es2_span sav, es2_error *error);

/* New career 0x40e8a5 (Registration ACCEPT): es2_shell_new_game at CAREER
 * (1, 0) from the engine's RNG; loaded, final word 2. */
es2_status es2_shell_engine_new_game(es2_shell_engine *engine, const char *name, uint16_t rank,
    uint32_t *salvage, es2_error *error);
/* The load 0x40eaaa of a slot file: the memory state becomes the one the
 * original readers restore (es2_shell_loaded_save), the final word the
 * slot's; loaded. The caller copies the companions. */
es2_status es2_shell_engine_load(es2_shell_engine *engine, es2_span file, es2_error *error);

/* The preparation of the CAREER mission of the memory state (the DEBUG
 * dialog's Use Default, mode 1): es2_shell_prepare from the memory state with
 * the engine's RNG and selectors, then the memory state becomes
 * es2_shell_prepared_save. When the player's bay holds no hangar record and
 * lowest_free is set, the player first gets the lowest bay no roster pilot
 * names (es2chain's policy, not VSHELL's: VSHELL's hangar waits for a
 * choice); without lowest_free that state is refused (ES2_ERR_UNSUPPORTED). A
 * CAREER group outside 1..5 is refused (ES2_ERR_ARGUMENT). *out (zeroed by the
 * caller) receives the four files and the inputs; free with
 * es2_shell_engine_prepared_free. */
typedef struct es2_shell_engine_prepared {
    es2_shell_output files;
    es2_shell_buffer input;                 /* the SAV the preparation started from (after the policy) */
    es2_shell_rng rng_in;
    int16_t selectors_in[30];
    int16_t group, position;
    char mission[40];
    int16_t hangar_from, hangar_to;         /* the policy's change, or -2, -2 */
} es2_shell_engine_prepared;
es2_status es2_shell_engine_prepare(es2_shell_engine *engine, es2_shell_engine_prepared *out, es2_error *error);
/* The same for the DEBUG dialog's ACCEPT 0x44dc4d in mode 1: the mission stem
 * (es2_shell_debug_mission) instead of the CAREER entry; the position stays. */
es2_status es2_shell_engine_prepare_mission(es2_shell_engine *engine, const char *mission,
    es2_shell_engine_prepared *out, es2_error *error);
void es2_shell_engine_prepared_free(es2_shell_engine_prepared *out);

/* The results path 0x40f09f of a -X3/-X4 start after the slot load: `slot`
 * is the slot file VSHELL loaded (its writer image is used), `variables` and
 * `results` are DATA\mission.var and DATA\results.dat, repair and restock
 * the option bytes of the start-up prefs block; the mode is the engine's. On
 * success the memory state becomes out->save, the final word out->final_word
 * and the RNG out->rng. *out must be zeroed; free with es2_shell_results_free. */
es2_status es2_shell_engine_results(es2_shell_engine *engine, es2_span slot, es2_span variables, es2_span results,
    uint8_t repair, uint8_t restock, es2_shell_results_output *out, es2_error *error);

/* The service bay's view of the memory state: every bay with a hangar
 * record (vehicle word, capacity, slot items and their word +8) and its
 * pilot (the player, else the first selected roster pilot naming it), and for a
 * bay and slot (both >= 0) the 27 rows of the WEAPONS list: item, list
 * length, availability byte, ARM answer, enabled (0x440256: both). */
typedef struct es2_shell_bay_view {
    int present[8];
    int16_t vehicle[8];
    uint16_t capacity[8];
    int16_t item[8][10];
    uint16_t secondary[8][10];
    int pilot[8];                           /* -1 none, 36 the player, else the roster index */
    char name[8][32];
    int rows;                               /* 0, or ES2_SHELL_WEAPON_ROWS */
    int16_t row_item[ES2_SHELL_WEAPON_ROWS];
    int row_count[ES2_SHELL_WEAPON_ROWS], row_available[ES2_SHELL_WEAPON_ROWS];
    int row_admitted[ES2_SHELL_WEAPON_ROWS], row_enabled[ES2_SHELL_WEAPON_ROWS];
} es2_shell_bay_view;
es2_status es2_shell_engine_bay_view(const es2_shell_engine *engine, int bay, int slot, es2_shell_bay_view *out,
    es2_error *error);
/* The ARM answer of a WEAPONS click (es2_shell_bay_op.admitted). */
int es2_shell_engine_admits(const es2_shell_engine *engine, int16_t vehicle, int16_t item, int16_t slot);
/* One es2_shell_service_bay operation on the memory state (CLICK: the
 * caller fills `admitted` with es2_shell_engine_admits). */
es2_status es2_shell_engine_bay(es2_shell_engine *engine, es2_shell_bay_op *op, es2_error *error);

/* Rock & Roll 0x445b99: the launch checks; when they pass, player.mec and
 * mission.var of 0x40f68c for the memory state (both buffers empty; free
 * them). A failed check returns ES2_OK with check->warning set and no files. */
es2_status es2_shell_engine_launch(const es2_shell_engine *engine, es2_shell_launch_check_result *check,
    es2_shell_buffer *player, es2_shell_buffer *variables, es2_error *error);

#endif
