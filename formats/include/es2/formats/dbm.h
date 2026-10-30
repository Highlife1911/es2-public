#ifndef ES2_FORMATS_DBM_H
#define ES2_FORMATS_DBM_H

#include "es2/formats/common.h"

typedef struct es2_dbm {
    uint16_t height, width;
    uint8_t bits_per_pixel, flags, compression;
    uint16_t colour_count;
    uint32_t body_size;
    size_t offset;
    size_t serialized_size; /* Includes the optional final zero pad. */
    es2_span pixels; /* Stored bytes; compressed streams include their U word. */
    es2_span colours; /* Optional four-byte colour records, borrowed. */
} es2_dbm;

typedef struct es2_dba {
    uint32_t count;
    size_t serialized_size;
    uint64_t total_pixel_bytes; /* Sum of declared width*height, not decoded. */
    es2_span children;
    es2_span trailing;
} es2_dba;

/* Headers and stored spans only, no allocations. DBM and DBA/HBA are distinct
 * tags. Parse requires exact consumption and zero alignment bytes. Known raw
 * 8-bit/layout-0 records additionally require P == width*height. Other header
 * encodings retain their spans but do not claim validated decoded pixels.
 * For nonzero compression the documented four-byte U prefix is required.
 * Outputs are valid only on ES2_OK and borrow input for its lifetime.
 */
es2_status es2_dbm_parse(es2_span input, es2_dbm *out, es2_error *error);
es2_status es2_dba_parse(es2_span input, es2_dba *out, es2_error *error);

/* Explicit prefix parsing exposes all remaining bytes in trailing. This is
 * intended for diagnostics such as the documented CORNERS.DBA anomaly;
 * callers must report the trailer rather than treating it as extra children.
 */
es2_status es2_dba_parse_prefix(es2_span input, es2_dba *out, es2_error *error);
es2_status es2_dba_image_at(const es2_dba *array, size_t index,
                            es2_dbm *out, es2_error *error);

/* Decode only 8-bit/layout-0 raw and the documented kind-1 RLE grammar into
 * caller storage. Kind 3 and other encodings return ES2_ERR_UNSUPPORTED.
 * The output buffer must not overlap stored bytes. RLE stops at the requested
 * output size, including partway through a run, as the documented reader does.
 * stored_consumed includes the U prefix, and exposes unconsumed stored bytes.
 * written/stored_consumed may be NULL; both are zero on failure. No allocations.
 * Synthetic tests exercise RLE; no compressed CD corpus validation is claimed.
 */
es2_status es2_dbm_decode(const es2_dbm *image, uint8_t *output, size_t capacity,
                          size_t *written, size_t *stored_consumed,
                          es2_error *error);

#endif
