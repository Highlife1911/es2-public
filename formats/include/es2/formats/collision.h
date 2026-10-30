#ifndef ES2_FORMATS_COLLISION_H
#define ES2_FORMATS_COLLISION_H

#include "es2/formats/common.h"

/* Specification: docs/formats/collision.md. Borrowed immutable input, no I/O
 * or allocation. All counts and complete consumption are checked. Descriptors
 * must come from a successful parse. Offsets are relative to the whole file.
 * Unknown fields stay opaque; no runtime pointers or derived bounds are read.
 * Parse/get outputs are assigned only on success. */
typedef struct es2_col {
    es2_span input;
    size_t group_count, shape_count, sphere_count;
} es2_col;
typedef struct es2_col_group {
    es2_span shapes;
    size_t offset, shape_count;
    int16_t part_id;
} es2_col_group;
typedef struct es2_col_shape {
    es2_span spheres;
    size_t offset, sphere_count;
    int16_t region;
} es2_col_shape;
typedef struct es2_col_sphere { int16_t x, y, z, radius; } es2_col_sphere;
es2_status es2_col_parse(es2_span input, es2_col *out, es2_error *error);
es2_status es2_col_group_at(const es2_col *col, size_t index,
                          es2_col_group *out, es2_error *error);
es2_status es2_col_shape_at(const es2_col_group *group, size_t index,
                          es2_col_shape *out, es2_error *error);
es2_status es2_col_sphere_at(const es2_col_shape *shape, size_t index,
                           es2_col_sphere *out, es2_error *error);
/* COL rejects sphere counts above 4095 (original signed read-size limit).
 * Optional cross-file check. COL parse rejects negative region indices; only
 * the companion DMG establishes their upper bound. DTS IDs need caller lookup. */
es2_status es2_col_validate_regions(const es2_col *col, size_t region_count,
                                  es2_error *error);

typedef struct es2_dmg {
    es2_span input, capacities, regions;
    size_t capacity_count, region_count, link_count, regions_offset;
} es2_dmg;
typedef struct es2_dmg_region {
    es2_span parameters, links; /* Original four parameter bytes; links at +8. */
    size_t offset, link_count;
    int16_t threshold;
    int8_t debris_index, cell_slot, parent_region;
    uint8_t flags; /* Preserve all bits; individual effect meanings incomplete. */
} es2_dmg_region;
typedef struct es2_dmg_link { int16_t weight, capacity_index; } es2_dmg_link;
/* Link and nonnegative parent-region indices are checked within this file. */
es2_status es2_dmg_parse(es2_span input, es2_dmg *out, es2_error *error);
es2_status es2_dmg_capacity_at(const es2_dmg *dmg, size_t index,
                             int16_t *out, es2_error *error);
es2_status es2_dmg_region_at(const es2_dmg *dmg, size_t index,
                           es2_dmg_region *out, es2_error *error);
es2_status es2_dmg_link_at(const es2_dmg_region *region, size_t index,
                         es2_dmg_link *out, es2_error *error);
/* Cell count comes from DTS root 0. Debris count is default DEF_DEB plus the
 * DAT-selected per-vehicle debris list. Negative cells/parents disable the
 * reference; debris -1 selects generated part debris instead of a table. */
es2_status es2_dmg_validate_references(const es2_dmg *dmg, size_t cell_count,
                                     size_t debris_count, es2_error *error);

typedef struct es2_gl { es2_span input, records; size_t count; } es2_gl;
typedef struct es2_gl_slot {
    es2_span parameters_0x02, parameters_0x18;
    size_t offset;
    int16_t part_id;
    int8_t display_slot;
} es2_gl_slot;
es2_status es2_gl_parse(es2_span input, es2_gl *out, es2_error *error);
es2_status es2_gl_slot_at(const es2_gl *gl, size_t index,
                        es2_gl_slot *out, es2_error *error);
/* The consumer uses 19 + display_slot as a region. Extra GL slots without
 * PDG counterparts are legal; do not bound GL slots by PDG's visible count. */
es2_status es2_gl_validate_regions(const es2_gl *gl, size_t region_count,
                                 es2_error *error);
/* Weapon augmentation also uses 12 + display_slot as a DMG capacity index. */
es2_status es2_gl_validate_capacities(const es2_gl *gl, size_t capacity_count,
                                    es2_error *error);
es2_status es2_gl_find_display_slot(const es2_gl *gl, size_t display_slot,
                                  size_t *index, es2_error *error);

typedef enum es2_pdg_kind { ES2_PDG_VEHICLE, ES2_PDG_WEAPONS } es2_pdg_kind;
typedef struct es2_pdg {
    es2_pdg_kind kind;
    es2_span input, views, slots;
    size_t view_count, rectangle_count, slot_count, slots_offset;
} es2_pdg;
typedef struct es2_pdg_view {
    int32_t coordinates[4];
    es2_span rectangles;
    size_t offset, rectangle_count;
} es2_pdg_view;
typedef struct es2_pdg_rectangle {
    int32_t selector, coordinates[4];
    int16_t colour_index;
    es2_span ignored_0x16, parameters_0x18;
    size_t offset;
} es2_pdg_rectangle;
typedef struct es2_pdg_slot {
    int32_t x, y;
    int16_t bitmap_offset;
    es2_span parameters_0x0a;
    uint32_t alignment;
    int32_t mode;
    size_t offset;
} es2_pdg_slot;
/* Caller explicitly selects WEAPONS versus vehicle. Vehicle counts above
 * three exceed the original runtime allocation and are rejected. Color
 * references must fit the original 27-entry COLORS.DAT table. */
es2_status es2_pdg_parse(es2_pdg_kind kind, es2_span input,
                       es2_pdg *out, es2_error *error);
es2_status es2_pdg_view_at(const es2_pdg *pdg, size_t index,
                         es2_pdg_view *out, es2_error *error);
es2_status es2_pdg_rectangle_at(const es2_pdg_view *view, size_t index,
                              es2_pdg_rectangle *out, es2_error *error);
es2_status es2_pdg_slot_at(const es2_pdg *pdg, size_t index,
                         es2_pdg_slot *out, es2_error *error);
es2_status es2_pdg_weapon_pair_at(const es2_pdg *pdg, size_t index,
                                int32_t out[2], es2_error *error);

#endif
