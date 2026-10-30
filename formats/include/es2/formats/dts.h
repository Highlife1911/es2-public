#ifndef ES2_FORMATS_DTS_H
#define ES2_FORMATS_DTS_H

#include "es2/formats/common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ES2_DTS_TAG_COUNT 22u
/* Implementation stack-safety limit, not a limit asserted by the format. */
#define ES2_DTS_DEPTH_LIMIT 256u

typedef struct es2_dts_children {
    es2_span encoded;
    size_t count;
} es2_dts_children;

typedef struct es2_dts_object {
    uint32_t tag;
    size_t offset;
    size_t root_index;
    size_t depth;
    size_t parent_offset; /* SIZE_MAX for a root. */
    es2_span body;
    es2_span encoded; /* Header, body and any final alignment byte. */
    /* ANShape separates its part children from the trailing ANAnimList. */
    es2_dts_children children[2];
    size_t child_run_count;
} es2_dts_object;

typedef struct es2_dts_stats {
    size_t roots;
    size_t objects;
    size_t max_depth;
    size_t counts[ES2_DTS_TAG_COUNT];
} es2_dts_stats;

typedef es2_status (*es2_dts_visitor)(const es2_dts_object *object, void *user);

/* The count-array index maps to this stable tag order. Invalid index -> 0. */
uint32_t es2_dts_tag_at(size_t index);
/* Returns a static class-name string, or NULL for an unsupported tag. */
const char *es2_dts_tag_name(uint32_t tag);

/*
 * Walk a nonempty stream of the documented DTS object classes. This generic
 * framing API accepts any supported root class; game DTS assets use TSShape
 * or ANShape roots. Body and child boundaries must consume exactly; geometry,
 * material, transform and animation reference semantics are not validated.
 *
 * No allocation or I/O. Spans borrow input memory. Visitor calls are postorder
 * (children before their parent), and the object descriptor is temporary.
 * NULL visitor disables callbacks. A non-OK callback result becomes
 * ES2_ERR_CALLBACK. max_depth counts a root as zero and must not exceed the
 * implementation's ES2_DTS_DEPTH_LIMIT. Recursion is explicitly bounded.
 *
 * stats is required and zeroed on entry. On failure it contains partial counts,
 * and earlier callbacks may already have run; discard any partially built tree.
 * Error offsets refer to the input stream, including nested-body errors.
 */
es2_status es2_dts_walk(es2_span input, size_t max_depth,
                        es2_dts_visitor visitor, void *user,
                        es2_dts_stats *stats, es2_error *error);

#ifdef __cplusplus
}
#endif

#endif
