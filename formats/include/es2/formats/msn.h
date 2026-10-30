#ifndef ES2_FORMATS_MSN_H
#define ES2_FORMATS_MSN_H

#include "es2/formats/common.h"

#define ES2_MSN_LIST_COUNT 17u
#define ES2_MSN_ROUTE_LIST 7u

/* Lists retain their file order. Index 7 has variable-length route records.
 * A zero stride marks that list; its records include each ten-byte prefix
 * and the two-byte children. All other spans contain count * stride bytes.
 * Counts and the version field are excluded from the borrowed spans.
 */
typedef struct es2_msn_list {
    size_t count;
    size_t stride;
    es2_span records;
} es2_msn_list;

typedef struct es2_msn {
    uint16_t version;
    es2_msn_list lists[ES2_MSN_LIST_COUNT];
} es2_msn;

/* Parse only the version-5 framing documented in docs/formats/mission.md.
 * No allocation, reference resolution, or mission interpretation occurs.
 * Input must remain alive while any returned span is used. On failure,
 * out is unchanged; errors are relative to the beginning of input.
 */
es2_status es2_msn_parse(es2_span input, es2_msn *out, es2_error *error);

#endif
