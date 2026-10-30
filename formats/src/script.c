#include "es2/formats/script.h"

/* The record readers of DBSIM's pass 1, docs/formats/mission.md's list table. */
static const size_t strides[ES2_SCRIPT_LIST_COUNT] = {
    12, 2, 0, 6, 74, 24, 134, 92, 52, 14, 156, 54, 2
};

size_t es2_script_stride(es2_script_list list)
{
    return (unsigned)list < ES2_SCRIPT_LIST_COUNT ? strides[list] : 0;
}

static es2_status read_count(es2_reader *r, size_t *count)
{
    size_t at = r->offset;
    int16_t n;
    if (es2_read_i16(r, &n) != ES2_OK) return r->status;
    if (n < 0) return es2_fail(r->error, ES2_ERR_COUNT, at);
    *count = (size_t)n;
    return ES2_OK;
}

/* One route: K, then K indices below `coordinates`. */
static es2_status read_route(es2_reader *r, size_t coordinates, es2_span *refs, size_t *count)
{
    es2_span v;
    size_t k, i;
    es2_status s = read_count(r, &k);
    if (s != ES2_OK) return s;
    if (es2_read_span(r, 2 * k, &v) != ES2_OK) return r->status;
    for (i = 0; i < k; ++i) {
        int16_t index = (int16_t)(uint16_t)((uint16_t)v.data[2 * i] |
                                            (uint16_t)((uint16_t)v.data[2 * i + 1] << 8));
        if (index < 0 || (size_t)index >= coordinates)
            return es2_fail(r->error, ES2_ERR_OFFSET, (size_t)(v.data - r->span.data) + 2 * i);
    }
    *refs = v;
    *count = k;
    return ES2_OK;
}

es2_status es2_script_parse(es2_span input, es2_script *out, es2_error *error)
{
    es2_reader r;
    es2_script v = {0};
    size_t i, j;
    es2_status s;
    if (!out) return es2_fail(error, ES2_ERR_ARGUMENT, 0);
    if (es2_reader_init(&r, input, error) != ES2_OK) return r.status;
    for (i = 0; i < 10; ++i)
        if (es2_read_i16(&r, &v.header[i]) != ES2_OK) return r.status;
    for (i = 0; i < ES2_SCRIPT_LIST_COUNT; ++i) {
        es2_script_part *p = &v.lists[i];
        size_t start;
        p->offset = r.offset;
        if ((s = read_count(&r, &p->count)) != ES2_OK) return s;
        start = r.offset;
        if (i == ES2_SCRIPT_ROUTES) {
            for (j = 0; j < p->count; ++j) {
                es2_span refs;
                size_t k;
                s = read_route(&r, v.lists[ES2_SCRIPT_COORDINATES].count, &refs, &k);
                if (s != ES2_OK) return s;
                v.route_references += k;
            }
        } else if (es2_skip(&r, strides[i] * p->count) != ES2_OK) {
            return r.status;
        }
        p->records = (es2_span){input.data + start, r.offset - start};
    }
    v.end = r.offset;
    v.trailing = (es2_span){input.data + r.offset, input.size - r.offset};
    v.input = input;
    *out = v;
    return ES2_OK;
}

es2_status es2_script_route_at(const es2_script *script, size_t index,
                               es2_span *references, size_t *offset, es2_error *error)
{
    const es2_script_part *routes;
    es2_reader r;
    es2_span refs = {0};
    size_t i, k, at = 0;
    es2_status s;
    if (!script || !references || !offset) return es2_fail(error, ES2_ERR_ARGUMENT, 0);
    routes = &script->lists[ES2_SCRIPT_ROUTES];
    if (index >= routes->count) return es2_fail(error, ES2_ERR_OFFSET, index);
    if (es2_reader_init(&r, script->input, error) != ES2_OK) return r.status;
    if (es2_skip(&r, routes->offset + 2) != ES2_OK) return r.status;
    for (i = 0; i <= index; ++i) {
        at = r.offset;
        s = read_route(&r, script->lists[ES2_SCRIPT_COORDINATES].count, &refs, &k);
        if (s != ES2_OK) return s;
    }
    *references = refs;
    *offset = at;
    return ES2_OK;
}
