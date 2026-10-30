#include "es2/formats/dts.h"

#include <string.h>

/* Class identifiers and layouts are specified in docs/formats/dts.md. */
static const uint32_t dts_tags[ES2_DTS_TAG_COUNT] = {
    0x00140001u, 0x00140002u, 0x00140003u, 0x00140005u,
    0x00140007u, 0x00140008u, 0x00140009u, 0x0014000au,
    0x0014000bu, 0x0014000cu, 0x0014000eu, 0x0014000fu,
    0x00140010u, 0x00140011u, 0x00140012u, 0x00140013u,
    0x00140014u, 0x00140015u, 0x001e0001u, 0x001e0002u,
    0x001e0003u, 0x001e0004u
};

static const char *const dts_names[ES2_DTS_TAG_COUNT] = {
    "TSPoly", "TSSolidPoly", "TSShadePoly", "TSBasePart",
    "TSPartList", "TSShape", "TSGouraudPoly", "TSBSPGroup",
    "TSCellAnimPart", "TSDetailPart", "TSNullObj", "TSTexture4Poly",
    "TSAliasSolidPoly", "TSAliasShadedPoly", "TSAliasGouraudPoly",
    "TSBitmapPart", "TSGroup", "TSBSPPart", "ANSequence", "ANAnimList",
    "ANShape", "ANCyclicSequence"
};

typedef struct dts_context {
    size_t max_depth;
    es2_dts_visitor visitor;
    void *user;
    es2_dts_stats *stats;
    es2_error *error;
} dts_context;

typedef struct dts_cursor {
    es2_reader reader;
    size_t origin;
    dts_context *context;
} dts_cursor;

typedef enum dts_expected {
    DTS_ANY,
    DTS_POLYGON,
    DTS_ANIMATION,
    DTS_SEQUENCE
} dts_expected;

#define DTS_TRY(operation) do { \
    es2_status dts_result_ = (operation); \
    if (dts_result_ != ES2_OK) return dts_result_; \
} while (0)

static size_t dts_tag_index(uint32_t tag)
{
    size_t i;
    for (i = 0; i < ES2_DTS_TAG_COUNT; ++i) {
        if (dts_tags[i] == tag) return i;
    }
    return ES2_DTS_TAG_COUNT;
}

uint32_t es2_dts_tag_at(size_t index)
{
    return index < ES2_DTS_TAG_COUNT ? dts_tags[index] : 0;
}

const char *es2_dts_tag_name(uint32_t tag)
{
    size_t i = dts_tag_index(tag);
    return i < ES2_DTS_TAG_COUNT ? dts_names[i] : NULL;
}

static es2_status dts_result(dts_cursor *cursor, es2_status status)
{
    if (status != ES2_OK) {
        return es2_fail(cursor->context->error, status,
                        cursor->origin + cursor->reader.offset);
    }
    return ES2_OK;
}

static es2_status dts_word(dts_cursor *cursor, uint16_t *value)
{
    return dts_result(cursor, es2_read_u16(&cursor->reader, value));
}

static es2_status dts_dword(dts_cursor *cursor, uint32_t *value)
{
    return dts_result(cursor, es2_read_u32(&cursor->reader, value));
}

static es2_status dts_skip(dts_cursor *cursor, size_t bytes)
{
    return dts_result(cursor, es2_skip(&cursor->reader, bytes));
}

static es2_status dts_array(dts_cursor *cursor, size_t count, size_t stride)
{
    if (stride != 0 && count > SIZE_MAX / stride) {
        return es2_fail(cursor->context->error, ES2_ERR_COUNT,
                        cursor->origin + cursor->reader.offset);
    }
    return dts_skip(cursor, count * stride);
}

static int dts_is_polygon(uint32_t tag)
{
    return tag == 0x00140001u || tag == 0x00140002u ||
           tag == 0x00140003u || tag == 0x00140009u ||
           tag == 0x0014000fu || tag == 0x00140010u ||
           tag == 0x00140011u || tag == 0x00140012u;
}

static int dts_matches(uint32_t tag, dts_expected expected)
{
    switch (expected) {
    case DTS_ANY: return 1;
    case DTS_POLYGON: return dts_is_polygon(tag);
    case DTS_ANIMATION: return tag == 0x001e0002u;
    case DTS_SEQUENCE: return tag == 0x001e0001u || tag == 0x001e0004u;
    }
    return 0;
}

static es2_status dts_object(dts_cursor *stream, size_t depth,
                             size_t root_index, size_t parent_offset,
                             dts_expected expected);

static es2_status dts_children(dts_cursor *body, es2_dts_object *object,
                               uint16_t count, dts_expected expected)
{
    size_t start = body->reader.offset;
    size_t i;
    es2_dts_children *run = &object->children[object->child_run_count];
    /* Each child needs at least its eight-byte header. */
    if ((size_t)count > es2_remaining(&body->reader) / 8u) {
        return es2_fail(body->context->error, ES2_ERR_TRUNCATED,
                        body->origin + start);
    }
    for (i = 0; i < count; ++i) {
        DTS_TRY(dts_object(body, object->depth + 1, object->root_index,
                            object->offset, expected));
    }
    run->encoded.data = body->reader.span.data + start;
    run->encoded.size = body->reader.offset - start;
    run->count = count;
    ++object->child_run_count;
    return ES2_OK;
}

static es2_status dts_part_list(dts_cursor *body, es2_dts_object *object,
                                uint16_t *count)
{
    DTS_TRY(dts_skip(body, 12));
    DTS_TRY(dts_word(body, count));
    return dts_children(body, object, *count, DTS_ANY);
}

static es2_status dts_shape(dts_cursor *body, es2_dts_object *object)
{
    uint16_t children, transforms, slots;
    DTS_TRY(dts_part_list(body, object, &children));
    DTS_TRY(dts_word(body, &transforms));
    DTS_TRY(dts_word(body, &slots));
    DTS_TRY(dts_array(body, slots, 2));
    return dts_array(body, transforms, 32);
}

static es2_status dts_group(dts_cursor *body, es2_dts_object *object)
{
    uint16_t indices, points, surfaces, polygons;
    DTS_TRY(dts_skip(body, 12));
    DTS_TRY(dts_word(body, &indices));
    DTS_TRY(dts_word(body, &points));
    DTS_TRY(dts_word(body, &surfaces));
    DTS_TRY(dts_word(body, &polygons));
    DTS_TRY(dts_array(body, indices, 2));
    DTS_TRY(dts_array(body, points, 6));
    DTS_TRY(dts_array(body, surfaces, 4));
    return dts_children(body, object, polygons, DTS_POLYGON);
}

static es2_status dts_sequence(dts_cursor *body)
{
    uint16_t frames, parts;
    DTS_TRY(dts_skip(body, 6));
    DTS_TRY(dts_word(body, &frames));
    DTS_TRY(dts_array(body, frames, 6));
    DTS_TRY(dts_word(body, &parts));
    DTS_TRY(dts_array(body, parts, 2));
    return dts_array(body, frames, (size_t)parts * 2u);
}

static es2_status dts_animation(dts_cursor *body, es2_dts_object *object)
{
    uint16_t count;
    DTS_TRY(dts_word(body, &count));
    DTS_TRY(dts_children(body, object, count, DTS_SEQUENCE));
    DTS_TRY(dts_word(body, &count));
    DTS_TRY(dts_array(body, count, 8));
    DTS_TRY(dts_word(body, &count));
    DTS_TRY(dts_array(body, count, 12));
    DTS_TRY(dts_word(body, &count));
    DTS_TRY(dts_array(body, count, 2));
    DTS_TRY(dts_word(body, &count));
    return dts_array(body, count, 4);
}

static es2_status dts_body(dts_cursor *body, es2_dts_object *object)
{
    uint16_t count, nodes;
    switch (object->tag) {
    case 0x00140001u: return dts_skip(body, 8);
    case 0x00140002u:
    case 0x00140003u:
    case 0x0014000fu:
    case 0x00140010u:
    case 0x00140011u: return dts_skip(body, 10);
    case 0x00140009u:
    case 0x00140012u:
    case 0x00140005u: return dts_skip(body, 12);
    case 0x0014000eu: return ES2_OK;
    case 0x00140013u: return dts_skip(body, 16);
    case 0x00140007u: return dts_part_list(body, object, &count);
    case 0x00140008u: return dts_shape(body, object);
    case 0x0014000bu:
        DTS_TRY(dts_part_list(body, object, &count));
        return dts_skip(body, 2);
    case 0x0014000cu:
        DTS_TRY(dts_part_list(body, object, &count));
        return dts_array(body, count, 2);
    case 0x00140015u:
        DTS_TRY(dts_part_list(body, object, &count));
        DTS_TRY(dts_word(body, &nodes));
        DTS_TRY(dts_array(body, nodes, 14));
        return dts_array(body, nodes, 2);
    case 0x00140014u: return dts_group(body, object);
    case 0x0014000au:
        DTS_TRY(dts_group(body, object));
        DTS_TRY(dts_word(body, &nodes));
        return dts_array(body, nodes, 10);
    case 0x001e0001u:
    case 0x001e0004u: return dts_sequence(body);
    case 0x001e0002u: return dts_animation(body, object);
    case 0x001e0003u:
        DTS_TRY(dts_shape(body, object));
        return dts_children(body, object, 1, DTS_ANIMATION);
    }
    return es2_fail(body->context->error, ES2_ERR_UNSUPPORTED, object->offset);
}

static es2_status dts_object(dts_cursor *stream, size_t depth,
                             size_t root_index, size_t parent_offset,
                             dts_expected expected)
{
    es2_dts_object object;
    dts_cursor body;
    uint32_t length;
    size_t index, start = stream->reader.offset;
    size_t position = stream->origin + start;
    dts_context *context = stream->context;
    if (depth > context->max_depth) {
        return es2_fail(context->error, ES2_ERR_DEPTH, position);
    }
    memset(&object, 0, sizeof(object));
    object.offset = position;
    object.depth = depth;
    object.root_index = root_index;
    object.parent_offset = parent_offset;
    DTS_TRY(dts_dword(stream, &object.tag));
    DTS_TRY(dts_dword(stream, &length));
    index = dts_tag_index(object.tag);
    if (index == ES2_DTS_TAG_COUNT) {
        return es2_fail(context->error, ES2_ERR_UNSUPPORTED, position);
    }
    if (!dts_matches(object.tag, expected)) {
        return es2_fail(context->error, ES2_ERR_MAGIC, position);
    }
    body.origin = stream->origin + stream->reader.offset;
    DTS_TRY(dts_result(stream, es2_read_span(&stream->reader, length,
                                            &object.body)));
    if (((stream->origin + stream->reader.offset) & 1u) != 0) {
        DTS_TRY(dts_skip(stream, 1));
    }
    object.encoded.data = stream->reader.span.data + start;
    object.encoded.size = stream->reader.offset - start;
    body.context = context;
    DTS_TRY(es2_reader_init(&body.reader, object.body, NULL));
    DTS_TRY(dts_body(&body, &object));
    DTS_TRY(dts_result(&body, es2_reader_end(&body.reader)));
    ++context->stats->objects;
    ++context->stats->counts[index];
    if (depth > context->stats->max_depth) context->stats->max_depth = depth;
    if (context->visitor != NULL &&
        context->visitor(&object, context->user) != ES2_OK) {
        return es2_fail(context->error, ES2_ERR_CALLBACK, position);
    }
    return ES2_OK;
}

es2_status es2_dts_walk(es2_span input, size_t max_depth,
                        es2_dts_visitor visitor, void *user,
                        es2_dts_stats *stats, es2_error *error)
{
    dts_context context;
    dts_cursor stream;
    es2_error_clear(error);
    if (stats == NULL) return es2_fail(error, ES2_ERR_ARGUMENT, 0);
    memset(stats, 0, sizeof(*stats));
    if (max_depth > ES2_DTS_DEPTH_LIMIT) {
        return es2_fail(error, ES2_ERR_ARGUMENT, 0);
    }
    DTS_TRY(es2_reader_init(&stream.reader, input, error));
    if (input.size == 0) return es2_fail(error, ES2_ERR_TRUNCATED, 0);
    context.max_depth = max_depth;
    context.visitor = visitor;
    context.user = user;
    context.stats = stats;
    context.error = error;
    stream.context = &context;
    stream.origin = 0;
    /* Nested cursor errors use input-relative offsets, not body-relative ones. */
    stream.reader.error = NULL;
    while (es2_remaining(&stream.reader) != 0) {
        DTS_TRY(dts_object(&stream, 0, stats->roots, SIZE_MAX, DTS_ANY));
        ++stats->roots;
    }
    return ES2_OK;
}
