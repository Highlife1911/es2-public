#ifndef ES2_FORMATS_PLAYER_MEC_H
#define ES2_FORMATS_PLAYER_MEC_H
#include "es2/formats/dat.h"

/* Writer grammar in docs/formats/mission.md. Views borrow immutable input.
 * No allocation or I/O. Unknown header, pilot, condition and flag bytes survive.
 * Parse/get outputs are unchanged on error. Counts are signed nonnegative i16;
 * the writer's suffix count is exactly 33. Flag values are not restricted. */
typedef struct es2_mec {
    es2_span input, flags;
    size_t count;
    uint16_t header;
} es2_mec;
typedef struct es2_mec_record {
    size_t offset, count;
    int16_t vehicle_id;
    es2_span pilot, items, secondary, tail; /* tail: zero word + 26 + 20 + 20 */
} es2_mec_record;
typedef struct es2_mec_edit {
    size_t record, count;
    int16_t vehicle_id;
    const int16_t *items;
} es2_mec_edit;
es2_status es2_mec_parse(es2_span input, es2_mec *out, es2_error *error);
es2_status es2_mec_record_at(const es2_mec *mec, size_t index,
                            es2_mec_record *out, es2_error *error);
es2_status es2_mec_item_at(const es2_mec_record *record, size_t index,
                          int16_t *item, uint16_t *secondary, es2_error *error);
/* NULL output + zero capacity queries required size (returns ES2_OK).
 * Edits are optional, unique by record, and keep record order/count unchanged.
 * Resizing keeps secondary words by ordinal; new words use the explicit value
 * supplied by the caller (VSHELL's absent-slot default is 5). No tail is rebuilt.
 * Input, edits/items, required and output must not overlap. Capacity/validation
 * failures do not write output. Descriptors must come from successful parses. */
es2_status es2_mec_write(const es2_mec *mec, const es2_mec_edit *edits,
    size_t edit_count, uint16_t new_secondary, uint8_t *output, size_t capacity,
    size_t *required, es2_error *error);

enum {
    ES2_MEC_WARN_K = 1,       /* K differs from HERC_INF +6; meaning hypothetical */
    ES2_MEC_WARN_MOUNT = 2    /* item/slot not observed in supplied INI loadouts */
};
/* Tables must be successfully parsed at runtime, never embedded constants.
 * Refuses N=0, absent vehicle/item IDs and K > 10 (shell fixed slot allocation).
 * INI occupied counts are NOT capacities. Warnings are evidence gaps, not bans.
 * This does not validate mission team capacity, condition or secondary semantics. */
es2_status es2_mec_validate(const es2_mec *mec, const es2_dat *hercs,
    const es2_dat *weapons, const es2_dat *loadouts, size_t loadout_count,
    unsigned *warnings, es2_error *error);
#endif
