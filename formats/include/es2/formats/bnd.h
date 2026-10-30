#ifndef ES2_FORMATS_BND_H
#define ES2_FORMATS_BND_H

#include "es2/formats/common.h"

/* No original BND grammar is proved. This is a bounded opaque inspection,
 * NOT a format parser. ES2_OK validates only the caller's memory span.
 * A four-byte prefix candidate is reported when present, without asserting
 * that it is a tag, count or timestamp. See docs/formats/bnd.md. */
typedef struct es2_bnd {
    es2_span input;
    size_t opaque_offset, opaque_length;
    int has_prefix_candidate;
    uint32_t prefix_candidate;
} es2_bnd;
es2_status es2_bnd_inspect(es2_span input, es2_bnd *out, es2_error *error);

#endif
