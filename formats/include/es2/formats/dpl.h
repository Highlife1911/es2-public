#ifndef ES2_FORMATS_DPL_H
#define ES2_FORMATS_DPL_H

#include "es2/formats/common.h"

/* Borrowed records: RGB bytes and a flags byte, not RGBA. No conversion is
 * applied. In the documented bit-0 path RGB expands by multiplication by 4.
 * Counts, flags and reserved fields are preserved, not restricted to CD values.
 */
typedef struct es2_dpl {
    uint16_t colour_count;
    uint16_t reserved_word;
    uint16_t ramp_count;
    uint16_t ramp_base;
    es2_span colours;
    es2_span ramps;
} es2_dpl;

typedef struct es2_dpl_colour {
    uint8_t red, green, blue, flags;
} es2_dpl_colour;

typedef struct es2_dpl_ramp {
    uint16_t count;
    es2_span indices; /* Little-endian words; use common readers, no casts. */
} es2_dpl_ramp;

/* All spans borrow input for its lifetime; no allocations. Parse validates
 * every ramp index and exact file consumption. Output is valid on ES2_OK.
 * Zero-length ramps are retained: observed nonzero lengths are not maxima or
 * a proven grammar restriction. Accessors are bounded and allocate nothing.
 */
es2_status es2_dpl_parse(es2_span input, es2_dpl *out, es2_error *error);
es2_status es2_dpl_colour_at(const es2_dpl *palette, size_t index,
                             es2_dpl_colour *out, es2_error *error);
es2_status es2_dpl_ramp_at(const es2_dpl *palette, size_t index,
                           es2_dpl_ramp *out, es2_error *error);

#endif
