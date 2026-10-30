#include "es2/formats/player_mec.h"
#include <string.h>

static uint16_t word(const uint8_t *p)
{ return (uint16_t)((uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8)); }
static void put_word(uint8_t *p, uint16_t v)
{ p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static es2_status read_record(es2_reader *r, es2_mec_record *out)
{
    es2_mec_record v = {0};
    int16_t count;
    v.offset = r->offset;
    if (es2_read_span(r, 4, &v.pilot) != ES2_OK ||
        es2_read_i16(r, &v.vehicle_id) != ES2_OK ||
        es2_read_i16(r, &count) != ES2_OK) return r->status;
    if (count < 0) return es2_fail(r->error, ES2_ERR_COUNT, v.offset + 6);
    v.count = (size_t)count;
    if (es2_read_span(r, 2 * v.count, &v.items) != ES2_OK ||
        es2_read_span(r, 2 * v.count, &v.secondary) != ES2_OK ||
        es2_read_span(r, 68, &v.tail) != ES2_OK) return r->status;
    *out = v;
    return ES2_OK;
}
es2_status es2_mec_parse(es2_span input, es2_mec *out, es2_error *error)
{
    es2_reader r;
    es2_mec v = {0};
    es2_mec_record record;
    int16_t count;
    size_t i;
    es2_status s;
    if (!out) return es2_fail(error, ES2_ERR_ARGUMENT, 0);
    if (es2_reader_init(&r, input, error) != ES2_OK) return r.status;
    if (es2_read_u16(&r, &v.header) != ES2_OK ||
        es2_read_i16(&r, &count) != ES2_OK) return r.status;
    if (count < 0) return es2_fail(error, ES2_ERR_COUNT, 2);
    v.count = (size_t)count;
    for (i = 0; i < v.count; ++i) {
        s = read_record(&r, &record);
        if (s != ES2_OK) return s;
    }
    if (es2_read_i16(&r, &count) != ES2_OK) return r.status;
    if (count != 33) return es2_fail(error, ES2_ERR_COUNT, r.offset - 2);
    if (es2_read_span(&r, 33, &v.flags) != ES2_OK) return r.status;
    if (es2_reader_end(&r) != ES2_OK) return r.status;
    v.input = input;
    *out = v;
    return ES2_OK;
}
es2_status es2_mec_record_at(const es2_mec *mec, size_t index,
                            es2_mec_record *out, es2_error *error)
{
    es2_reader r;
    es2_mec_record v;
    size_t i;
    es2_status s;
    if (!mec || !out) return es2_fail(error, ES2_ERR_ARGUMENT, 0);
    if (index >= mec->count) return es2_fail(error, ES2_ERR_OFFSET, index);
    if (es2_reader_init(&r, mec->input, error) != ES2_OK) return r.status;
    if (es2_skip(&r, 4) != ES2_OK) return r.status;
    for (i = 0; i <= index; ++i) {
        s = read_record(&r, &v);
        if (s != ES2_OK) return s;
    }
    *out = v;
    return ES2_OK;
}
es2_status es2_mec_item_at(const es2_mec_record *record, size_t index,
                          int16_t *item, uint16_t *secondary, es2_error *error)
{
    es2_reader a, b;
    int16_t id;
    uint16_t value;
    if (!record || !item || !secondary) return es2_fail(error, ES2_ERR_ARGUMENT, 0);
    if (index >= record->count || index > SIZE_MAX / 2)
        return es2_fail(error, ES2_ERR_OFFSET, index);
    (void)es2_reader_init(&a, record->items, error);
    (void)es2_reader_init(&b, record->secondary, error);
    if (es2_skip(&a, 2 * index) != ES2_OK || es2_read_i16(&a, &id) != ES2_OK) return a.status;
    if (es2_skip(&b, 2 * index) != ES2_OK || es2_read_u16(&b, &value) != ES2_OK) return b.status;
    *item = id; *secondary = value;
    return ES2_OK;
}
static const es2_mec_edit *edit_at(const es2_mec_edit *edits, size_t n, size_t index)
{
    size_t i;
    for (i = 0; i < n; ++i) if (edits[i].record == index) return &edits[i];
    return NULL;
}
es2_status es2_mec_write(const es2_mec *mec, const es2_mec_edit *edits,
    size_t edit_count, uint16_t new_secondary, uint8_t *output, size_t capacity,
    size_t *required, es2_error *error)
{
    es2_mec checked;
    size_t i, j, size = 39, at = 4;
    es2_status s;
    if (!mec || !required || (edit_count && !edits) || (!output && capacity))
        return es2_fail(error, ES2_ERR_ARGUMENT, 0);
    s = es2_mec_parse(mec->input, &checked, error);
    if (s != ES2_OK) return s;
    for (i = 0; i < edit_count; ++i) {
        if (edits[i].record >= checked.count || edits[i].count > INT16_MAX ||
            (edits[i].count && !edits[i].items)) return es2_fail(error, ES2_ERR_ARGUMENT, i);
        for (j = 0; j < i; ++j) if (edits[j].record == edits[i].record)
            return es2_fail(error, ES2_ERR_ARGUMENT, i);
    }
    for (i = 0; i < checked.count; ++i) {
        es2_mec_record r;
        const es2_mec_edit *e = edit_at(edits, edit_count, i);
        size_t n;
        s = es2_mec_record_at(&checked, i, &r, error);
        if (s != ES2_OK) return s;
        n = 76 + 4 * (e ? e->count : r.count);
        if (n > SIZE_MAX - size) return es2_fail(error, ES2_ERR_LENGTH, r.offset);
        size += n;
    }
    *required = size;
    if (!output) return ES2_OK;
    if (capacity < size) return es2_fail(error, ES2_ERR_CAPACITY, 0);
    /* Reject overlap with borrowed template, including shifted/in-place writes. */
    if ((uintptr_t)output <= (uintptr_t)checked.input.data ?
        (uintptr_t)checked.input.data - (uintptr_t)output < size :
        (uintptr_t)output - (uintptr_t)checked.input.data < checked.input.size)
        return es2_fail(error, ES2_ERR_OVERLAP, 0);
    memcpy(output, checked.input.data, 4);
    for (i = 0; i < checked.count; ++i) {
        es2_mec_record r;
        const es2_mec_edit *e = edit_at(edits, edit_count, i);
        (void)es2_mec_record_at(&checked, i, &r, NULL);
        if (!e) {
            size_t n = 76 + 4 * r.count;
            memcpy(output + at, checked.input.data + r.offset, n); at += n;
        } else {
            memcpy(output + at, r.pilot.data, 4);
            put_word(output + at + 4, (uint16_t)e->vehicle_id);
            put_word(output + at + 6, (uint16_t)e->count); at += 8;
            for (j = 0; j < e->count; ++j) put_word(output + at + 2*j, (uint16_t)e->items[j]);
            at += 2 * e->count;
            for (j = 0; j < e->count; ++j)
                put_word(output + at + 2*j, j < r.count ? word(r.secondary.data + 2*j) : new_secondary);
            at += 2 * e->count;
            memcpy(output + at, r.tail.data, 68); at += 68;
        }
    }
    put_word(output + at, 33); memcpy(output + at + 2, checked.flags.data, 33);
    return ES2_OK;
}
es2_status es2_mec_validate(const es2_mec *mec, const es2_dat *hercs,
    const es2_dat *weapons, const es2_dat *loadouts, size_t loadout_count,
    unsigned *warnings, es2_error *error)
{
    size_t i, j, k, t;
    unsigned flags = 0;
    if (!mec || !hercs || !weapons || !warnings || (loadout_count && !loadouts) ||
        hercs->kind != ES2_DAT_HERC_INF || weapons->kind != ES2_DAT_SHELL_WEAPONS)
        return es2_fail(error, ES2_ERR_ARGUMENT, 0);
    /* DBSIM 0x4245c4..0x4245d0 immediately accesses the first record. */
    if (!mec->count) return es2_fail(error, ES2_ERR_COUNT, 2);
    for (i = 0; i < mec->count; ++i) {
        es2_mec_record r;
        es2_dat_herc_info h;
        int found = 0;
        es2_status s = es2_mec_record_at(mec, i, &r, error);
        if (s != ES2_OK) return s;
        if (r.count > 10) return es2_fail(error, ES2_ERR_COUNT, r.offset + 6);
        for (j = 0; j < hercs->count; ++j) {
            s = es2_dat_herc_info_at(hercs, j, &h, error);
            if (s != ES2_OK) return s;
            if (h.herc_id == r.vehicle_id) { found = 1; break; }
        }
        if (!found) return es2_fail(error, ES2_ERR_NOT_FOUND, r.offset + 4);
        if (h.word_0x06 < 0 || r.count != (size_t)h.word_0x06) flags |= ES2_MEC_WARN_K;
        for (j = 0; j < r.count; ++j) {
            int16_t id; uint16_t secondary;
            int mounted = 0;
            found = 0;
            s = es2_mec_item_at(&r, j, &id, &secondary, error);
            if (s != ES2_OK) return s;
            for (k = 0; k < weapons->count; ++k) {
                es2_dat_shell_weapon w;
                s = es2_dat_shell_weapon_at(weapons, k, &w, error);
                if (s != ES2_OK) return s;
                if (w.weapon_id == id) { found = 1; break; }
            }
            if (!found) return es2_fail(error, ES2_ERR_NOT_FOUND, r.offset + 8 + 2*j);
            /* Empty item zero is the documented writer's absent-slot value. */
            if (id == 0) mounted = 1;
            for (t = 0; t < loadout_count && !mounted; ++t) {
                for (k = 0; k < loadouts[t].count && !mounted; ++k) {
                    es2_dat_loadout l;
                    size_t q;
                    s = es2_dat_loadout_at(&loadouts[t], k, &l, error);
                    if (s != ES2_OK) return s;
                    if (l.herc_id != r.vehicle_id) continue;
                    for (q = 0; q < l.slot_count; ++q) {
                        es2_dat_loadout_slot slot;
                        s = es2_dat_loadout_slot_at(&l, q, &slot, error);
                        if (s != ES2_OK) return s;
                        if (slot.slot_id >= 0 && (size_t)slot.slot_id == j && slot.item_id == id) mounted = 1;
                    }
                }
            }
            if (!mounted) flags |= ES2_MEC_WARN_MOUNT;
        }
    }
    *warnings = flags;
    return ES2_OK;
}
