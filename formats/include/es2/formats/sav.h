#ifndef ES2_FORMATS_SAV_H
#define ES2_FORMATS_SAV_H
#include "es2/formats/common.h"

/* VSHELL SAV framing, not a native memory image. All views borrow immutable
 * input; opaque values and any trailing bytes are retained. See mission.md.
 * Parse/accessor outputs are unchanged on error. Descriptors passed to accessors
 * must come from successful parsing and remain unchanged. No allocation or I/O. */
typedef struct es2_sav_inventory {
    uint8_t flag;
    size_t count;
    es2_span items; /* count records of five LE words */
} es2_sav_inventory;
typedef struct es2_sav_pilot {
    es2_span prefix, name, suffix; /* 4 bytes; name incl NUL; 25 bytes */
    size_t offset;
} es2_sav_pilot;
typedef struct es2_sav_loadout {
    int16_t index;
    es2_span fixed, items; /* 76 bytes; count entries (slot + five words) */
    size_t count, offset;
} es2_sav_loadout;
typedef struct es2_sav {
    es2_span input;
    es2_sav_inventory inventory[33];
    es2_span five_entry_bank, selection, roster, roster_selection;
    es2_span team, loadouts, nine_word_bank, tally, variables, final_fields, trailing;
    es2_sav_pilot player;
    size_t loadout_count, variable_offset;
    /* Length of the writer image: the original save writer emits exactly
     * input[0..reader_end). trailing is input[reader_end..]: bytes left over
     * from an older, longer file, because VSHELL rewrites a SAV without
     * truncating it (mission.md, "SAV write sequence and stale tail"). */
    size_t reader_end;
} es2_sav;

/* Replacement records (es2_sav_pilot / es2_sav_loadout used as input, offset
 * ignored) must lie in the loader-safe domain, which every SAV written by the
 * original writers satisfies (see mission.md, "SAV editing and building"):
 * - pilot: prefix 4 and suffix 25 bytes; name 1..30 bytes, the last one being
 *   its only NUL (the runtime name field holds 30 bytes);
 * - loadout list: at most 8 records with unique hangar indices 0..7; fixed 76
 *   bytes whose capacity word (+74) is 0..10; count <= capacity; items hold
 *   count records of a slot word (unique, < capacity) and five words whose
 *   first (item ID) is 0..32;
 * - inventory (compose only): item ID of every record equals its category.
 * Fixed-size spans must have exactly the documented size. */
typedef struct es2_sav_edit {
    const int16_t *variables; /* NULL preserves all 1,000 words */
    int set_position;
    int16_t group, position; /* raw indices; CAREER validation is caller policy */
    /* Further replacements; NULL (or set_loadouts=0) preserves the input. */
    const uint8_t *inventory_flags;   /* 33 availability bytes; item lists kept */
    const es2_sav_pilot *roster;      /* 36 records, groups of twelve */
    const uint16_t *roster_selection; /* 6 words: three selected, three next */
    const int16_t *team;              /* 2 words: capacity, active count */
    const es2_sav_pilot *player;
    int set_loadouts;                 /* nonzero replaces every hangar record */
    size_t loadout_count;
    const es2_sav_loadout *loadouts;
    const int16_t *nine_word_bank;    /* 9 words (HERC_INF +14 per vehicle) */
} es2_sav_edit;

/* Complete description for building a SAV without an input file. All spans
 * are caller-owned serialized bytes with the reader's sizes. */
typedef struct es2_sav_image {
    es2_sav_inventory inventory[33];
    es2_span five_entry_bank, selection; /* 22, 152 */
    const es2_sav_pilot *roster;         /* 36 records */
    es2_span roster_selection, team;     /* 12, 4 */
    const es2_sav_pilot *player;
    size_t loadout_count;
    const es2_sav_loadout *loadouts;
    es2_span nine_word_bank, tally, variables, final_fields; /* 18, 4, 2000, 22 */
    es2_span trailing;                   /* normally empty; stale tail, see es2_sav */
} es2_sav_image;

es2_status es2_sav_parse(es2_span input, es2_sav *out, es2_error *error);
es2_status es2_sav_pilot_at(const es2_sav *save, size_t index,
    es2_sav_pilot *out, es2_error *error); /* roster indices 0..35 */
es2_status es2_sav_loadout_at(const es2_sav *save, size_t index,
    es2_sav_loadout *out, es2_error *error);
/* Rebuilds framing and emits every opaque span, then save->trailing
 * unchanged after the (possibly resized) image. Without an edit the result is
 * the input byte for byte. To model what the original leaves on disk when the
 * image length changes, use es2_sav_write_over. NULL output/zero capacity
 * queries size. Refuses overlapping input/output,
 * edits and required. Capacity/validation errors do not modify output.
 * required is assigned after successful validation, including capacity errors.
 * Unedited sections are copied unchanged; replacements are validated as above. */
es2_status es2_sav_write(const es2_sav *save, const es2_sav_edit *edit,
    uint8_t *output, size_t capacity, size_t *required, es2_error *error);
/* On-disk result of the original save 0x40e933 writing the (edited) image
 * over an existing file `previous`: VSHELL opens it read/write without
 * truncation and writes the image sequentially from offset 0, so the file
 * afterwards is the image followed by previous[image length..] (nothing when
 * previous is not longer). save->trailing is not emitted. An empty previous
 * (no file) yields the bare writer image; previous == save->input with no
 * edit yields the input. previous is arbitrary bytes, may alias save->input
 * and must not overlap output or required. Otherwise the es2_sav_write
 * contract applies. */
es2_status es2_sav_write_over(const es2_sav *save, const es2_sav_edit *edit,
    es2_span previous, uint8_t *output, size_t capacity, size_t *required,
    es2_error *error);
/* The same non-truncating update for an image built elsewhere (for example
 * by es2_sav_compose with an empty trailing span): image followed by
 * previous[image.size..]. Neither span may overlap output or required. */
es2_status es2_sav_overlay(es2_span image, es2_span previous, uint8_t *output,
    size_t capacity, size_t *required, es2_error *error);
/* Serializes a complete image with the same output/size/overlap contract.
 * The result parses with es2_sav_parse and rewrites unchanged. */
es2_status es2_sav_compose(const es2_sav_image *image, uint8_t *output,
    size_t capacity, size_t *required, es2_error *error);
#endif
