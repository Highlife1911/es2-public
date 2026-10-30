#include "es2/formats/collision.h"

#define TRY(op) do { es2_status status_ = (op); if (status_ != ES2_OK) return status_; } while (0)

static int8_t signed_byte(uint8_t byte)
{ return (int8_t)(byte < 128 ? (int)byte : (int)byte - 256); }

static es2_status count(es2_reader *r, int wide, size_t *out)
{
    int32_t n;
    size_t offset = r->offset;
    if (wide) { TRY(es2_read_i32(r, &n)); }
    else { int16_t small; TRY(es2_read_i16(r, &small)); n = small; }
    if (n < 0) return es2_fail(r->error, ES2_ERR_COUNT, offset);
    *out = (size_t)n;
    return ES2_OK;
}

static es2_status run(es2_reader *r, size_t n, size_t stride, es2_span *out)
{
    if (n > es2_remaining(r) / stride)
        return es2_fail(r->error, ES2_ERR_TRUNCATED, r->offset);
    if (out != NULL) return es2_read_span(r, n * stride, out);
    return es2_skip(r, n * stride);
}

static es2_status item(es2_span span, size_t n, size_t stride, size_t index,
                       es2_reader *r, es2_error *error)
{
    es2_span bytes;
    if (index >= n) return es2_fail(error, ES2_ERR_OFFSET, index);
    if (index > SIZE_MAX / stride) return es2_fail(error, ES2_ERR_OFFSET, index);
    TRY(es2_subspan(span, index * stride, stride, &bytes, error));
    return es2_reader_init(r, bytes, error);
}

static es2_status col_shape(es2_reader *r, es2_col_shape *out)
{
    es2_col_shape s = {0};
    s.offset = r->offset;
    TRY(es2_read_i16(r, &s.region));
    if (s.region < 0) return es2_fail(r->error, ES2_ERR_OFFSET, s.offset);
    TRY(count(r, 0, &s.sphere_count));
    /* Original 0x40c80f sign-extends the low word of 8*S as the read size. */
    if (s.sphere_count > 4095) return es2_fail(r->error, ES2_ERR_COUNT, s.offset + 2);
    TRY(run(r, s.sphere_count, 8, &s.spheres));
    *out = s;
    return ES2_OK;
}

static es2_status col_group(es2_reader *r, es2_col_group *out, size_t *spheres)
{
    es2_col_group g = {0};
    size_t start, i;
    g.offset = r->offset;
    TRY(es2_read_i16(r, &g.part_id));
    TRY(count(r, 0, &g.shape_count));
    if (g.shape_count > es2_remaining(r) / 4)
        return es2_fail(r->error, ES2_ERR_TRUNCATED, r->offset);
    start = r->offset;
    for (i = 0; i < g.shape_count; ++i) {
        es2_col_shape s;
        TRY(col_shape(r, &s));
        *spheres += s.sphere_count; /* Each contributes eight bounded bytes. */
    }
    TRY(es2_subspan(r->span, start, r->offset - start, &g.shapes, r->error));
    *out = g;
    return ES2_OK;
}

es2_status es2_col_parse(es2_span input, es2_col *out, es2_error *error)
{
    es2_col c = {0};
    es2_reader r;
    size_t i;
    if (out == NULL) return es2_fail(error, ES2_ERR_ARGUMENT, 0);
    TRY(es2_reader_init(&r, input, error));
    c.input = input;
    TRY(count(&r, 0, &c.group_count));
    if (c.group_count > es2_remaining(&r) / 4)
        return es2_fail(error, ES2_ERR_TRUNCATED, r.offset);
    for (i = 0; i < c.group_count; ++i) {
        es2_col_group g;
        TRY(col_group(&r, &g, &c.sphere_count));
        c.shape_count += g.shape_count;
    }
    TRY(es2_reader_end(&r));
    *out = c;
    return ES2_OK;
}

es2_status es2_col_group_at(const es2_col *col, size_t index,
                          es2_col_group *out, es2_error *error)
{
    es2_reader r;
    es2_col_group g;
    size_t i, spheres = 0;
    if (col == NULL || out == NULL) return es2_fail(error, ES2_ERR_ARGUMENT, 0);
    if (index >= col->group_count) return es2_fail(error, ES2_ERR_OFFSET, index);
    TRY(es2_reader_init(&r, col->input, error));
    TRY(es2_skip(&r, 2));
    for (i = 0; i <= index; ++i) TRY(col_group(&r, &g, &spheres));
    *out = g;
    return ES2_OK;
}

es2_status es2_col_shape_at(const es2_col_group *group, size_t index,
                          es2_col_shape *out, es2_error *error)
{
    es2_reader r;
    es2_col_shape s;
    size_t i;
    if (group == NULL || out == NULL) return es2_fail(error, ES2_ERR_ARGUMENT, 0);
    if (index >= group->shape_count) return es2_fail(error, ES2_ERR_OFFSET, index);
    TRY(es2_reader_init(&r, group->shapes, error));
    for (i = 0; i <= index; ++i) TRY(col_shape(&r, &s));
    s.offset += group->offset + 4;
    *out = s;
    return ES2_OK;
}

es2_status es2_col_sphere_at(const es2_col_shape *shape, size_t index,
                           es2_col_sphere *out, es2_error *error)
{
    es2_reader r;
    es2_col_sphere s;
    if (shape == NULL || out == NULL) return es2_fail(error, ES2_ERR_ARGUMENT, 0);
    TRY(item(shape->spheres, shape->sphere_count, 8, index, &r, error));
    TRY(es2_read_i16(&r, &s.x)); TRY(es2_read_i16(&r, &s.y));
    TRY(es2_read_i16(&r, &s.z)); TRY(es2_read_i16(&r, &s.radius));
    *out = s;
    return ES2_OK;
}

es2_status es2_col_validate_regions(const es2_col *col, size_t region_count,
                                  es2_error *error)
{
    size_t i, j;
    if (col == NULL) return es2_fail(error, ES2_ERR_ARGUMENT, 0);
    for (i = 0; i < col->group_count; ++i) {
        es2_col_group g;
        TRY(es2_col_group_at(col, i, &g, error));
        for (j = 0; j < g.shape_count; ++j) {
            es2_col_shape s;
            TRY(es2_col_shape_at(&g, j, &s, error));
            if (s.region < 0 || (size_t)s.region >= region_count)
                return es2_fail(error, ES2_ERR_OFFSET, s.offset);
        }
    }
    return ES2_OK;
}

static es2_status dmg_region(es2_reader *r, es2_dmg_region *out)
{
    es2_dmg_region v = {0};
    v.offset = r->offset;
    TRY(es2_read_i16(r, &v.threshold));
    TRY(es2_read_span(r, 4, &v.parameters));
    v.debris_index = signed_byte(v.parameters.data[0]);
    v.cell_slot = signed_byte(v.parameters.data[1]);
    v.parent_region = signed_byte(v.parameters.data[2]);
    v.flags = v.parameters.data[3];
    TRY(count(r, 0, &v.link_count));
    TRY(run(r, v.link_count, 4, &v.links));
    *out = v;
    return ES2_OK;
}

es2_status es2_dmg_parse(es2_span input, es2_dmg *out, es2_error *error)
{
    es2_reader r;
    es2_dmg d = {0};
    size_t i, j;
    if (out == NULL) return es2_fail(error, ES2_ERR_ARGUMENT, 0);
    TRY(es2_reader_init(&r, input, error));
    d.input = input;
    TRY(count(&r, 0, &d.capacity_count));
    TRY(run(&r, d.capacity_count, 2, &d.capacities));
    TRY(count(&r, 0, &d.region_count));
    if (d.region_count > es2_remaining(&r) / 8)
        return es2_fail(error, ES2_ERR_TRUNCATED, r.offset);
    d.regions_offset = r.offset;
    for (i = 0; i < d.region_count; ++i) {
        es2_dmg_region region;
        TRY(dmg_region(&r, &region));
        if (region.debris_index < -1)
            return es2_fail(error, ES2_ERR_OFFSET, region.offset + 2);
        if (region.parent_region >= 0 && (size_t)region.parent_region >= d.region_count)
            return es2_fail(error, ES2_ERR_OFFSET, region.offset + 4);
        d.link_count += region.link_count;
        for (j = 0; j < region.link_count; ++j) {
            es2_dmg_link link;
            TRY(es2_dmg_link_at(&region, j, &link, error));
            if (link.capacity_index < 0 || (size_t)link.capacity_index >= d.capacity_count)
                return es2_fail(error, ES2_ERR_OFFSET, region.offset + 10 + 4*j);
        }
    }
    TRY(es2_reader_end(&r));
    TRY(es2_subspan(input, d.regions_offset, r.offset - d.regions_offset, &d.regions, error));
    *out = d;
    return ES2_OK;
}

es2_status es2_dmg_capacity_at(const es2_dmg *dmg, size_t index,
                             int16_t *out, es2_error *error)
{
    es2_reader r;
    if (dmg == NULL || out == NULL) return es2_fail(error, ES2_ERR_ARGUMENT, 0);
    TRY(item(dmg->capacities, dmg->capacity_count, 2, index, &r, error));
    return es2_read_i16(&r, out);
}

es2_status es2_dmg_region_at(const es2_dmg *dmg, size_t index,
                           es2_dmg_region *out, es2_error *error)
{
    es2_reader r;
    es2_dmg_region v;
    size_t i;
    if (dmg == NULL || out == NULL) return es2_fail(error, ES2_ERR_ARGUMENT, 0);
    if (index >= dmg->region_count) return es2_fail(error, ES2_ERR_OFFSET, index);
    TRY(es2_reader_init(&r, dmg->regions, error));
    for (i = 0; i <= index; ++i) TRY(dmg_region(&r, &v));
    v.offset += dmg->regions_offset;
    *out = v;
    return ES2_OK;
}

es2_status es2_dmg_link_at(const es2_dmg_region *region, size_t index,
                         es2_dmg_link *out, es2_error *error)
{
    es2_reader r;
    es2_dmg_link v;
    if (region == NULL || out == NULL) return es2_fail(error, ES2_ERR_ARGUMENT, 0);
    TRY(item(region->links, region->link_count, 4, index, &r, error));
    TRY(es2_read_i16(&r, &v.weight)); TRY(es2_read_i16(&r, &v.capacity_index));
    *out = v;
    return ES2_OK;
}

es2_status es2_dmg_validate_references(const es2_dmg *dmg, size_t cell_count,
                                     size_t debris_count, es2_error *error)
{
    size_t i;
    if (dmg == NULL) return es2_fail(error, ES2_ERR_ARGUMENT, 0);
    for (i = 0; i < dmg->region_count; ++i) {
        es2_dmg_region r;
        TRY(es2_dmg_region_at(dmg, i, &r, error));
        if (r.debris_index < -1 || (r.debris_index >= 0 && (size_t)r.debris_index >= debris_count))
            return es2_fail(error, ES2_ERR_OFFSET, r.offset + 2);
        if (r.cell_slot >= 0 && (size_t)r.cell_slot >= cell_count)
            return es2_fail(error, ES2_ERR_OFFSET, r.offset + 3);
    }
    return ES2_OK;
}

es2_status es2_gl_parse(es2_span input, es2_gl *out, es2_error *error)
{
    es2_gl g = {0};
    es2_reader r;
    if (out == NULL) return es2_fail(error, ES2_ERR_ARGUMENT, 0);
    TRY(es2_reader_init(&r, input, error));
    g.input = input;
    TRY(count(&r, 0, &g.count));
    TRY(run(&r, g.count, 26, &g.records));
    TRY(es2_reader_end(&r));
    *out = g;
    return ES2_OK;
}

es2_status es2_gl_slot_at(const es2_gl *gl, size_t index,
                        es2_gl_slot *out, es2_error *error)
{
    es2_reader r;
    es2_gl_slot s = {0};
    uint8_t byte;
    if (gl == NULL || out == NULL) return es2_fail(error, ES2_ERR_ARGUMENT, 0);
    TRY(item(gl->records, gl->count, 26, index, &r, error));
    s.offset = 2 + 26*index;
    TRY(es2_read_i16(&r, &s.part_id));
    TRY(es2_read_span(&r, 21, &s.parameters_0x02));
    TRY(es2_read_u8(&r, &byte));
    s.display_slot = signed_byte(byte);
    TRY(es2_read_span(&r, 2, &s.parameters_0x18));
    *out = s;
    return ES2_OK;
}

es2_status es2_gl_validate_regions(const es2_gl *gl, size_t region_count,
                                 es2_error *error)
{
    size_t i;
    if (gl == NULL) return es2_fail(error, ES2_ERR_ARGUMENT, 0);
    for (i = 0; i < gl->count; ++i) {
        es2_gl_slot s;
        int region;
        TRY(es2_gl_slot_at(gl, i, &s, error));
        region = 19 + s.display_slot;
        if (region < 0 || (size_t)region >= region_count)
            return es2_fail(error, ES2_ERR_OFFSET, s.offset + 23);
    }
    return ES2_OK;
}

es2_status es2_gl_find_display_slot(const es2_gl *gl, size_t display_slot,
                                  size_t *index, es2_error *error)
{
    size_t i;
    if (gl == NULL || index == NULL) return es2_fail(error, ES2_ERR_ARGUMENT, 0);
    for (i = 0; i < gl->count; ++i) {
        es2_gl_slot s;
        TRY(es2_gl_slot_at(gl, i, &s, error));
        if (s.display_slot >= 0 && (size_t)s.display_slot == display_slot) {
            *index = i;
            return ES2_OK;
        }
    }
    return es2_fail(error, ES2_ERR_NOT_FOUND, display_slot);
}

es2_status es2_gl_validate_capacities(const es2_gl *gl, size_t capacity_count,
                                    es2_error *error)
{
    size_t i;
    if (gl == NULL) return es2_fail(error, ES2_ERR_ARGUMENT, 0);
    for (i = 0; i < gl->count; ++i) {
        es2_gl_slot s;
        int capacity;
        TRY(es2_gl_slot_at(gl, i, &s, error));
        capacity = 12 + s.display_slot;
        if (capacity < 0 || (size_t)capacity >= capacity_count)
            return es2_fail(error, ES2_ERR_OFFSET, s.offset + 23);
    }
    return ES2_OK;
}

static es2_status pdg_view(es2_reader *r, es2_pdg_view *out)
{
    es2_pdg_view v = {0};
    size_t i;
    v.offset = r->offset;
    for (i = 0; i < 4; ++i) TRY(es2_read_i32(r, &v.coordinates[i]));
    TRY(count(r, 1, &v.rectangle_count));
    TRY(run(r, v.rectangle_count, 28, &v.rectangles));
    *out = v;
    return ES2_OK;
}

es2_status es2_pdg_parse(es2_pdg_kind kind, es2_span input,
                       es2_pdg *out, es2_error *error)
{
    es2_pdg p = {0};
    es2_reader r;
    size_t n, i, j, end;
    if (out == NULL || (kind != ES2_PDG_VEHICLE && kind != ES2_PDG_WEAPONS))
        return es2_fail(error, ES2_ERR_ARGUMENT, 0);
    TRY(es2_reader_init(&r, input, error));
    p.kind = kind; p.input = input;
    TRY(count(&r, 1, &n));
    if (kind == ES2_PDG_WEAPONS) {
        p.slot_count = n; p.slots_offset = r.offset;
        TRY(run(&r, n, 8, &p.slots));
    } else {
        if (n > 3) return es2_fail(error, ES2_ERR_COUNT, 0);
        p.view_count = n;
        for (i = 0; i < n; ++i) {
            es2_pdg_view v;
            TRY(pdg_view(&r, &v));
            p.rectangle_count += v.rectangle_count;
            for (j = 0; j < v.rectangle_count; ++j) {
                es2_pdg_rectangle rect;
                TRY(es2_pdg_rectangle_at(&v, j, &rect, error));
                if (rect.colour_index < 0 || rect.colour_index >= 27)
                    return es2_fail(error, ES2_ERR_OFFSET, rect.offset + 20);
            }
        }
        end = r.offset;
        TRY(es2_subspan(input, 4, end - 4, &p.views, error));
        TRY(count(&r, 1, &p.slot_count));
        p.slots_offset = r.offset;
        TRY(run(&r, p.slot_count, 20, &p.slots));
    }
    TRY(es2_reader_end(&r));
    *out = p;
    return ES2_OK;
}

es2_status es2_pdg_view_at(const es2_pdg *pdg, size_t index,
                         es2_pdg_view *out, es2_error *error)
{
    es2_reader r;
    es2_pdg_view v;
    size_t i;
    if (pdg == NULL || out == NULL || pdg->kind != ES2_PDG_VEHICLE)
        return es2_fail(error, ES2_ERR_ARGUMENT, 0);
    if (index >= pdg->view_count) return es2_fail(error, ES2_ERR_OFFSET, index);
    TRY(es2_reader_init(&r, pdg->views, error));
    for (i = 0; i <= index; ++i) TRY(pdg_view(&r, &v));
    v.offset += 4;
    *out = v;
    return ES2_OK;
}

es2_status es2_pdg_rectangle_at(const es2_pdg_view *view, size_t index,
                              es2_pdg_rectangle *out, es2_error *error)
{
    es2_reader r;
    es2_pdg_rectangle v = {0};
    size_t i;
    if (view == NULL || out == NULL) return es2_fail(error, ES2_ERR_ARGUMENT, 0);
    TRY(item(view->rectangles, view->rectangle_count, 28, index, &r, error));
    v.offset = view->offset + 20 + 28*index;
    TRY(es2_read_i32(&r, &v.selector));
    for (i = 0; i < 4; ++i) TRY(es2_read_i32(&r, &v.coordinates[i]));
    TRY(es2_read_i16(&r, &v.colour_index));
    TRY(es2_read_span(&r, 2, &v.ignored_0x16));
    TRY(es2_read_span(&r, 4, &v.parameters_0x18));
    *out = v;
    return ES2_OK;
}

es2_status es2_pdg_slot_at(const es2_pdg *pdg, size_t index,
                         es2_pdg_slot *out, es2_error *error)
{
    es2_reader r;
    es2_pdg_slot s = {0};
    if (pdg == NULL || out == NULL || pdg->kind != ES2_PDG_VEHICLE)
        return es2_fail(error, ES2_ERR_ARGUMENT, 0);
    TRY(item(pdg->slots, pdg->slot_count, 20, index, &r, error));
    s.offset = pdg->slots_offset + 20*index;
    TRY(es2_read_i32(&r, &s.x)); TRY(es2_read_i32(&r, &s.y));
    TRY(es2_read_i16(&r, &s.bitmap_offset));
    TRY(es2_read_span(&r, 2, &s.parameters_0x0a));
    TRY(es2_read_u32(&r, &s.alignment)); TRY(es2_read_i32(&r, &s.mode));
    *out = s;
    return ES2_OK;
}

es2_status es2_pdg_weapon_pair_at(const es2_pdg *pdg, size_t index,
                                int32_t out[2], es2_error *error)
{
    es2_reader r;
    int32_t x, y;
    if (pdg == NULL || out == NULL || pdg->kind != ES2_PDG_WEAPONS)
        return es2_fail(error, ES2_ERR_ARGUMENT, 0);
    TRY(item(pdg->slots, pdg->slot_count, 8, index, &r, error));
    TRY(es2_read_i32(&r, &x)); TRY(es2_read_i32(&r, &y));
    out[0] = x; out[1] = y;
    return ES2_OK;
}
