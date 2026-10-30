#ifndef ES2_SHELL_SAVE_H
#define ES2_SHELL_SAVE_H

#include "es2/shell/prepare.h"

/* SAV construction for the preparation boundary. Bytes are produced by the
 * formats:sav writer; this layer supplies VSHELL's field values. No file I/O.
 * Output follows the formats convention: NULL output/zero capacity queries the
 * size, errors leave output untouched. See vshell_preparation.md, "Loadout
 * SAV contract". */

/* Tables of VSHELL's image, passed in, not constants of this module
 * (es2shell and es2menu take them from the span store, vshell_store.h):
 * defaults as es2_shell_options.default_tables (164 bytes) and the thirty
 * item IDs whose positions VSHELL stores in item word +2 (60 bytes). */
typedef struct es2_shell_save_tables {
    es2_span defaults, item_indices;
} es2_shell_save_tables;

/* One mounted item. Without has_secondary the hangar mount rule applies:
 * missile racks 13..15 store 1, every other item 5. */
typedef struct es2_shell_mount {
    int16_t slot, item;
    int has_secondary;
    uint16_t secondary;
} es2_shell_mount;
typedef struct es2_shell_herc {
    int16_t vehicle;          /* ID from the executable's vehicle table */
    size_t count;             /* <= vehicle capacity */
    const es2_shell_mount *mounts;
} es2_shell_herc;
typedef struct es2_shell_member {
    uint8_t group, index;     /* roster group 0..2, pilot 0..11 in that group */
    es2_shell_herc herc;
} es2_shell_member;
typedef struct es2_shell_plan {
    int16_t group, position;  /* CAREER selection; caller validates it */
    es2_shell_herc player;    /* bay 0 */
    size_t team_count;        /* 0..3; member k gets bay k and order k */
    const es2_shell_member *team;
    const uint8_t *flags;     /* 33 bytes, or NULL: base flags | mounted IDs */
    int zero_variables;       /* nonzero clears the persistent bank */
} es2_shell_plan;

/* The SAV VSHELL writes right after its new-career loaders (inventory,
 * seed-zero roster, player, HERCS hangar), with the given player name
 * (1..29 bytes), rank and CAREER selection. */
es2_status es2_shell_default_save(const es2_vfs *vfs, const char *language,
    const es2_shell_save_tables *tables, const char *player_name,
    uint16_t player_rank, int16_t group, int16_t position,
    uint8_t *output, size_t capacity, size_t *required, es2_error *error);
/* New career (VSHELL 0x40e8a5, vshell_ui.md section 4.2): the default SAV
 * above, but the roster and player variant are drawn from *rng (the
 * preparation RNG at 0x482409, not a seed-zero stream), then the salvage
 * total becomes 100,000 + 1,000 x draw(11) from the same stream and the final
 * word 2. The variable bank is zero. Same output/size convention; *rng and
 * *salvage (optional) are updated only when output was written. */
es2_status es2_shell_new_game(const es2_vfs *vfs, const char *language,
    const es2_shell_save_tables *tables, const char *player_name,
    uint16_t player_rank, int16_t group, int16_t position, es2_shell_rng *rng,
    uint8_t *output, size_t capacity, size_t *required, uint32_t *salvage,
    es2_error *error);
/* Replaces the hangar with the plan's Hercs, assigns the team, selection and
 * availability flags; every other base byte is kept. */
es2_status es2_shell_plan_save(es2_span base, const es2_shell_save_tables *tables,
    const es2_shell_plan *plan, uint8_t *output, size_t capacity,
    size_t *required, es2_error *error);
uint16_t es2_shell_mount_secondary(int16_t item);
/* The SAV VSHELL holds after a mode-1 preparation of `base`: what its
 * save 0x40e933 writes after Use Default, before anything else changes the
 * state. Relative to base's writer image (stale tail dropped): the CAREER
 * selection is the prepared (group, position) (0x46fc58/0x46fc5a), the
 * variable bank is the prepared mission.var (0x40ef83 writes the bank
 * itself), the
 * briefing selection is prepared->briefing (0x4134f2), and the team words
 * plus the three selected pilots' active bytes are those of `after`
 * (0x410923 adjusts them to the authored team capacity). `after` is the
 * state es2_shell_prepare returned for es2_shell_restore_save(base).
 * Mode 0 changes far more (hangar, team construction) and is refused
 * (prepared->has_briefing = 0). Same output/size convention. */
es2_status es2_shell_prepared_save(es2_span base, int16_t group, int16_t position,
    const es2_shell_state *after,
    const es2_shell_output *prepared, uint8_t *output, size_t capacity,
    size_t *required, es2_error *error);
/* The SAV VSHELL writes for the state its load 0x40eaaa restored from
 * `file`: the reader 0x412781 prepends every inventory record, so
 * each of the 33 item lists comes out in reverse order; everything else is
 * the writer image of `file` (stale tail dropped). For files written by
 * VSHELL's own writers; applying it twice gives the writer image back. Same
 * output/size convention. */
es2_status es2_shell_loaded_save(es2_span file, uint8_t *output, size_t capacity,
    size_t *required, es2_error *error);

#endif
