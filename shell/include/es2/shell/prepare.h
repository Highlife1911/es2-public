#ifndef ES2_SHELL_PREPARE_H
#define ES2_SHELL_PREPARE_H

#include "es2/formats/vfs.h"
#include "es2/formats/player_mec.h"

/* No file I/O or process-global state. All input spans remain caller-owned.
 * State and output are unchanged on failure. Output must initially be zero;
 * release it with es2_shell_output_free before reuse. See shell/README.md. */
typedef struct es2_shell_rng {
    uint16_t words[56];
    uint8_t a, b;
} es2_shell_rng;

/* Preparation projection of player/roster/hangar state. No borrowed pointers. */
typedef struct es2_shell_pilot {
    uint16_t identity, variant, rank, companion;
    int16_t bay, order;
    uint8_t active;
} es2_shell_pilot;
typedef struct es2_shell_bay {
    int present;
    int16_t vehicle;
    uint16_t capacity, condition[33], secondary[10];
    int16_t items[10];
} es2_shell_bay;
typedef struct es2_shell_equipment {
    int initialized;
    int16_t capacity, count;
    es2_shell_pilot player, pilots[36];
    uint16_t selected[3], next[3];
    es2_shell_bay bays[8];
    uint8_t flags[33];
} es2_shell_equipment;
typedef struct es2_shell_state {
    int16_t variables[1000];
    int16_t reset_selectors[30];
    es2_shell_rng rng;
    es2_shell_equipment equipment;
} es2_shell_state;
typedef struct es2_shell_actor {
    uint16_t pilot[2];
    int16_t vehicle;
    size_t capacity;
    const int16_t *items;
    const uint16_t *secondary;
    uint16_t condition[33];
} es2_shell_actor;
typedef struct es2_shell_loadout {
    const es2_shell_actor *actors;
    size_t count;
    uint8_t flags[33];
} es2_shell_loadout;
typedef struct es2_shell_options {
    /* A bare mission stem, or NULL to resolve campaign indices through VFS. */
    const char *mission;
    const char *language; /* ENG, FRE or GER. NULL means GER. */
    int16_t group, position;
    int16_t mode, difficulty, player_value;
    uint8_t mode0_values[4]; /* header words 5,6,7,9 */
    /* Exactly one may be supplied. A template is parsed and reserialized. */
    es2_span player_template;
    const es2_shell_loadout *loadout;
    /* Runtime data, not embedded constants: 112 RNG + 16 pilot + 36 vehicle
     * bytes from the checked EXE. Required for CD defaults/mission equipment. */
    es2_span default_tables;
    uint8_t training_vehicle, training_preset_flag;
    int16_t training_preset_index;
    const char *training_preset_path; /* selected CD INI path, if branch used */
} es2_shell_options;

/* Restore the preparation projection and variable bank, retaining RNG/reset
 * selectors. Caller chooses mode/difficulty/position explicitly. */
es2_status es2_shell_restore_save(es2_span save, es2_shell_state *state,
    es2_error *error);
typedef struct es2_shell_buffer { uint8_t *data; size_t size; } es2_shell_buffer;
typedef struct es2_shell_output {
    es2_shell_buffer script, strings, variables, player;
    /* Mode 1 only (has_briefing = 1): the 148 briefing-selection bytes of
     * the SAV (counts and references at 0x4840cc..0x48419d) as 0x4134f2
     * derives them from the first retained list-4 record (0x41ce89). The
     * SAV of the state after the preparation: es2_shell_prepared_save. */
    int has_briefing;
    uint8_t briefing[148];
} es2_shell_output;

/* The launch-file writer 0x40f68c (Rock & Roll; also the last step of a
 * preparation) for the memory state `save` describes: mission.var is
 * the 2,000-byte variable bank (0x40ef83); player.mec holds word 0, the team
 * count word, the player's variant, rank and hangar record (0x410cdb), then
 * for orders 1 .. capacity - 1 the selected roster pilot with that order if
 * it is active (variant, rank, its bay's record), then 33 and the 33
 * availability bytes (0x412877). The team words are taken as stored (a
 * preparation sets them); a capacity outside 1..4, an active pilot without a
 * Herc or a count that differs from the records written are refused.
 * Both buffers must be empty; free their data. */
es2_status es2_shell_launch_files(es2_span save, es2_shell_buffer *player,
    es2_shell_buffer *variables, es2_error *error);

/* Initial 112-byte table is supplied by the caller (VSHELL's image;
 * es2shell and es2menu take it from the span store), not embedded here.
 * Seed reproduces the original signed-word discard loop. */
es2_status es2_shell_rng_init(es2_span table, uint16_t seed,
    es2_shell_rng *out, es2_error *error);
es2_status es2_shell_rng_draw(es2_shell_rng *state, int16_t bound,
    uint16_t *out, es2_error *error);
/* Resolve a CAREER position. name_capacity must be at least 40. */
es2_status es2_shell_campaign_mission(const es2_vfs *vfs, int16_t group,
    int16_t position, const char *language, char *name, size_t name_capacity,
    int16_t *group_id, es2_error *error);
es2_status es2_shell_prepare(const es2_vfs *vfs,
    const es2_shell_options *options, es2_shell_state *state,
    es2_shell_output *out, es2_error *error);
void es2_shell_output_free(es2_shell_output *out);

/* Debrief selection of VSHELL's alternate MSN reader (vshell_preparation.md,
 * "Debrief selection") for a bare mission stem. variables is the
 * 1,000-word bank at selection time; rng is advanced by type-1 conditions.
 * refs/count/aux are the copied record (0x484160, 0x48415e, 0x48419e);
 * passed is the number of passing list-5 records (0: the last record read,
 * unremapped, as the original leaves it); strings is the rewritten
 * MISSION.STR. out must be zero; release it with es2_shell_debrief_free. An
 * empty list 5 (original assertion) and a missing auxiliary record are
 * errors. rng and out are unchanged on failure. */
typedef struct es2_shell_debrief {
    int16_t refs[30], count, aux;
    size_t passed;
    es2_shell_buffer strings;
} es2_shell_debrief;
es2_status es2_shell_debrief_select(const es2_vfs *vfs, const char *mission,
    const char *language, const int16_t *variables, es2_shell_rng *rng,
    es2_shell_debrief *out, es2_error *error);
void es2_shell_debrief_free(es2_shell_debrief *out);

#endif
