#ifndef ES2_FORMATS_DAT_H
#define ES2_FORMATS_DAT_H

#include "es2/formats/common.h"

/* Layout specification: docs/formats/dat.md only. No allocation or I/O.
 * W/D and hypothetical meanings are exposed as unsigned raw values or spans,
 * never promoted to physical units. Counts come from the input, except the
 * original fixed 33-item DAMAGE and nine-group hotspot contracts.
 */
typedef enum es2_dat_kind {
    ES2_DAT_UNKNOWN = 0,
    ES2_DAT_SIM_WEAPONS, ES2_DAT_VEHICLE, ES2_DAT_SKIMMER,
    ES2_DAT_DEBRIS, ES2_DAT_PROJ, ES2_DAT_ROCKETS, ES2_DAT_BULLETS,
    ES2_DAT_BEAM, ES2_DAT_LC_WPNS, ES2_DAT_HERC_INF, ES2_DAT_LOADOUT,
    ES2_DAT_HERCS, ES2_DAT_TRN_HERC, ES2_DAT_SHELL_WEAPONS,
    ES2_DAT_DAMAGE, ES2_DAT_ARM, ES2_DAT_RPR, ES2_DAT_ARM_WEAP,
    ES2_DAT_HOTSPOTS, ES2_DAT_MFORMS, ES2_DAT_FFORMS, ES2_DAT_BFORMS,
    ES2_DAT_BASES, ES2_DAT_BASECOL, ES2_DAT_EXPLOS, ES2_DAT_COLORS,
    ES2_DAT_FIRE, ES2_DAT_SHADES, ES2_DAT_MAT0, ES2_DAT_ZONE,
    ES2_DAT_CAREER, ES2_DAT_THEATER, ES2_DAT_MAP_INFO, ES2_DAT_MAP_LOCS,
    ES2_DAT_TEX, ES2_DAT_STYLES, ES2_DAT_KIND_COUNT
} es2_dat_kind;

typedef enum es2_dat_support {
    ES2_DAT_READER_FRAMING, ES2_DAT_DATA_FIT_ONLY,
    ES2_DAT_EXTERNAL_COUNT, ES2_DAT_PREFIX_WITH_SUFFIX
} es2_dat_support;

typedef struct es2_dat {
    es2_dat_kind kind;
    es2_dat_support support;
    es2_span input;
    es2_span records;   /* Complete primary record run, without its count. */
    es2_span secondary; /* Second run (RPR initial images; ARM initial pair). */
    es2_span suffix;    /* MAT0 unread bytes only; never active records. */
    size_t count;
    size_t secondary_count;
    size_t child_count; /* Leaf children; SIM_WEAPONS A entries / BASECOL points. */
    size_t byte_list_count; /* BFORMS byte lists / BASECOL shapes / weapon B entries. */
    size_t nested_group_count; /* BASECOL groups. Zero for other schemas. */
} es2_dat;

/* Full path, ASCII case folding and slash equivalence; no basename fallback.
 * Loose DATA/MAT0.DAT and DATA/MFORMS.DAT are explicitly recognized.
 * Inventory family names are classified; ZONE accepts any nonempty decimal
 * suffix, without restricting it to the observed zone-number list.
 * Unknown returns UNKNOWN.
 */
es2_dat_kind es2_dat_classify(const char *full_path);
const char *es2_dat_kind_name(es2_dat_kind kind);
/* FIRE: external_count is the number of payload bytes, not a header value.
 * BASECOL: external_count is the number of previously parsed BASES records.
 * All other kinds ignore external_count. Unknown external counts must not be
 * guessed by a caller wishing to claim original-reader validation.
 * Strict exact consumption, except MAT0's explicit suffix. Data-only schemas
 * and TRN_HERC are framing conveniences, not confirmed consumer contracts.
 * Output is assigned only on success. All spans borrow unchanged input.
 */
es2_status es2_dat_parse(es2_dat_kind kind, es2_span input,
                         size_t external_count, es2_dat *out, es2_error *error);
/* Retrieve a complete primary record. Bounds checked; variable runs scanned.
 * Use common readers on opaque spans, not native struct casts.
 */
es2_status es2_dat_record_at(const es2_dat *table, size_t index,
                             es2_span *out, es2_error *error);

typedef struct es2_dat_vehicle {
    es2_span raw;
    es2_span bytes_0x00, bytes_0x06, bytes_0x2e, bytes_0x42, bytes_0x50;
    es2_span bytes_0x62, bytes_0x6e, bytes_0x96, bytes_0xc8;
    int16_t reverse_speed_parameter, forward_speed_parameter;
    int16_t parameter_0x2c, branch_selector, parameter_0x6c;
    int16_t resource_index, hud_scale, rescale_denominator, rescale_numerator;
    int16_t word_0xc6; /* File value, before the loader overwrites it. */
    es2_span label, debris_resource; /* Text before first NUL, not full slots. */
} es2_dat_vehicle;
es2_status es2_dat_vehicle_get(const es2_dat *table, es2_dat_vehicle *out,
                               es2_error *error);
typedef struct es2_dat_skimmer {
    es2_span parameters_0x00, label, parameters_0x1e;
} es2_dat_skimmer;
es2_status es2_dat_skimmer_get(const es2_dat *table, es2_dat_skimmer *out,
                               es2_error *error);

typedef struct es2_dat_sim_weapon {
    uint16_t words_0x00[3], selector;
    size_t count_a, count_b;
    es2_span array_a, array_b, tail;
    int16_t model_indices[4];
    es2_span bytes_0x08;
    uint32_t dword_0x0e;
    uint16_t word_0x12;
    int16_t threshold, discharge_amount, initial_ammunition;
    uint16_t firing_layout;
    int16_t projectile_selection, offsets_0x1e[3];
    uint16_t words_0x24[3]; /* Triple's signedness is not proved. */
    int16_t cooldown_seed;
    uint16_t word_0x2c, word_0x2e;
} es2_dat_sim_weapon;
es2_status es2_dat_sim_weapon_at(const es2_dat *table, size_t index,
                                 es2_dat_sim_weapon *out, es2_error *error);

typedef struct es2_dat_herc_info {
    int16_t herc_id, word_0x02, word_0x04, word_0x06, word_0x08;
    uint16_t word_0x0a;
    int16_t build_progress_divisor;
    uint16_t word_0x0e;
} es2_dat_herc_info;
es2_status es2_dat_herc_info_at(const es2_dat *table, size_t index,
                                es2_dat_herc_info *out, es2_error *error);
typedef struct es2_dat_loadout {
    int16_t herc_id, bay_slot; /* bay_slot is present only for HERCS. */
    int has_bay_slot;
    uint16_t word_0x02, word_0x04;
    size_t slot_count;
    es2_span slots;
} es2_dat_loadout;
typedef struct es2_dat_loadout_slot {
    int16_t slot_id, item_id;
    uint16_t word_0x04, word_0x06;
} es2_dat_loadout_slot;
es2_status es2_dat_loadout_at(const es2_dat *table, size_t index,
                              es2_dat_loadout *out, es2_error *error);
es2_status es2_dat_loadout_slot_at(const es2_dat_loadout *loadout, size_t index,
                                   es2_dat_loadout_slot *out, es2_error *error);

typedef struct es2_dat_shell_weapon {
    int16_t weapon_id;
    es2_span name; /* Excludes exactly one terminating NUL. */
    uint16_t word_after_name;
    uint8_t byte_after_word;
    uint16_t final_word; /* Cost/availability/priority semantics hypothetical. */
} es2_dat_shell_weapon;
typedef struct es2_dat_item {
    int16_t item_id;
    uint16_t word_0x02, word_0x04;
} es2_dat_item;
es2_status es2_dat_shell_weapon_at(const es2_dat *table, size_t index,
                                   es2_dat_shell_weapon *out, es2_error *error);
es2_status es2_dat_shell_item_at(const es2_dat *table, size_t index,
                                 es2_dat_item *out, es2_error *error);
typedef struct es2_dat_damage {
    int16_t common_coefficient, coefficients_0x02[6], coefficients_0x0e[9];
    int16_t second_common_coefficient, item_coefficients[33];
} es2_dat_damage;
es2_status es2_dat_damage_get(const es2_dat *table, es2_dat_damage *out,
                              es2_error *error);
typedef struct es2_dat_zone {
    uint32_t dword_0x00, dword_0x04;
    int32_t dword_0x08;
    uint32_t dword_0x0c;
} es2_dat_zone;
es2_status es2_dat_zone_get(const es2_dat *table, es2_dat_zone *out,
                            es2_error *error);

typedef struct es2_dat_group {
    uint16_t id; /* Raw group ID for debris; signed accessor below for CAREER. */
    size_t count;
    es2_span children;
} es2_dat_group;
typedef struct es2_dat_debris_child {
    uint16_t word_0x00, word_0x02, word_0x04, word_0x06;
    int16_t word_0x08, word_0x0a; /* Signed -1 sentinels confirmed. */
    uint16_t word_0x0c;
} es2_dat_debris_child;
es2_status es2_dat_debris_group_at(const es2_dat *table, size_t index,
                                   es2_dat_group *out, es2_error *error);
es2_status es2_dat_debris_child_at(const es2_dat_group *group, size_t index,
                                   es2_dat_debris_child *out, es2_error *error);
typedef struct es2_dat_projectile {
    int16_t selector_0x00, selector_0x02;
    es2_span parameters_0x04;
} es2_dat_projectile;
es2_status es2_dat_projectile_at(const es2_dat *table, size_t index,
                                 es2_dat_projectile *out, es2_error *error);
typedef struct es2_dat_career_group {
    int16_t id;
    size_t count;
    es2_span string_indices;
} es2_dat_career_group;
es2_status es2_dat_career_group_at(const es2_dat *table, size_t index,
                                   es2_dat_career_group *out, es2_error *error);
es2_status es2_dat_career_index_at(const es2_dat_career_group *group, size_t index,
                                   int16_t *out, es2_error *error);

#endif
