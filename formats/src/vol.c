#include "es2/formats/vol.h"
#include <string.h>

static uint8_t folded(uint8_t c)
{
    if (c >= (uint8_t)'A' && c <= (uint8_t)'Z') c = (uint8_t)(c + ('a' - 'A'));
    return c == (uint8_t)'/' ? (uint8_t)'\\' : c;
}

static int same_name(es2_span a, es2_span b)
{
    size_t i;
    if (a.size != b.size) return 0;
    for (i = 0; i < a.size; ++i) if (folded(a.data[i]) != folded(b.data[i])) return 0;
    return 1;
}

es2_status es2_vol_directory_at(const es2_vol *vol, size_t index,
                               es2_span *out, es2_error *error)
{
    es2_reader r;
    size_t i;
    if (vol == NULL || out == NULL) return es2_fail(error, ES2_ERR_ARGUMENT, 0);
    if (index >= vol->directory_count) return es2_fail(error, ES2_ERR_OFFSET, index);
    if (es2_reader_init(&r, vol->directory_bytes, error) != ES2_OK) return r.status;
    for (i = 0; i <= index; ++i) {
        size_t start = r.offset;
        uint8_t c;
        do {
            if (es2_read_u8(&r, &c) != ES2_OK) return r.status;
        } while (c != 0);
        if (i == index)
            return es2_subspan(vol->directory_bytes, start, r.offset - start - 1, out, error);
    }
    return es2_fail(error, ES2_ERR_OFFSET, index);
}

es2_status es2_vol_member_at(const es2_vol *vol, size_t index,
                            es2_vol_member *out, es2_error *error)
{
    es2_vol_member m = {0};
    es2_span slot, block;
    es2_reader r;
    uint32_t offset, length;
    size_t n;
    es2_status status;
    if (vol == NULL || out == NULL) return es2_fail(error, ES2_ERR_ARGUMENT, 0);
    if (index >= vol->member_count || index > SIZE_MAX / 18)
        return es2_fail(error, ES2_ERR_OFFSET, index);
    status = es2_subspan(vol->member_bytes, index * 18, 18, &slot, error);
    if (status != ES2_OK) return status;
    for (n = 0; n < 13 && slot.data[n] != 0; ++n) {
        uint8_t c = slot.data[n];
        if (c < 32 || c >= 127 || c == '/' || c == '\\')
            return es2_fail(error, ES2_ERR_STRING, vol->table_offset + index * 18 + n);
    }
    if (n == 0 || n == 13)
        return es2_fail(error, ES2_ERR_STRING, vol->table_offset + index * 18);
    m.name.data = slot.data; m.name.size = n;
    m.directory_index = slot.data[13];
    status = es2_vol_directory_at(vol, m.directory_index, &m.directory, error);
    if (status != ES2_OK) return status;
    (void)es2_reader_init(&r, slot, error);
    (void)es2_skip(&r, 14);
    if (es2_read_u32(&r, &offset) != ES2_OK) return r.status;
    m.block_offset = offset;
    if (m.block_offset < vol->blocks_offset)
        return es2_fail(error, ES2_ERR_OVERLAP, m.block_offset);
    status = es2_subspan(vol->input, m.block_offset, 9, &block, error);
    if (status != ES2_OK) return status;
    (void)es2_reader_init(&r, block, error);
    (void)es2_read_u8(&r, &m.kind);
    (void)es2_read_u32(&r, &length);
    (void)es2_read_u32(&r, &m.metadata);
    if (m.kind != 2) return es2_fail(error, ES2_ERR_UNSUPPORTED, m.block_offset);
    if (length == 0) return es2_fail(error, ES2_ERR_LENGTH, m.block_offset + 1);
    m.payload_offset = m.block_offset + 9; /* subspan proved this addition safe */
    m.length = length;
    status = es2_subspan(vol->input, m.payload_offset, m.length, &m.payload, error);
    if (status != ES2_OK) return status;
    if (m.length == vol->input.size - m.payload_offset)
        return es2_fail(error, ES2_ERR_TRUNCATED, vol->input.size);
    if (m.payload.data[m.length - 1] != vol->input.data[m.payload_offset + m.length])
        return es2_fail(error, ES2_ERR_LENGTH, m.payload_offset + m.length);
    *out = m;
    return ES2_OK;
}

es2_status es2_vol_open(es2_span input, es2_vol *out, es2_error *error)
{
    es2_vol v = {0};
    es2_reader r, directories;
    es2_span magic;
    uint8_t directory_count;
    uint16_t directory_bytes, member_count;
    uint32_t table_bytes;
    size_t i, total = 0;
    es2_status status;
    if (out == NULL) return es2_fail(error, ES2_ERR_ARGUMENT, 0);
    if (es2_reader_init(&r, input, error) != ES2_OK) return r.status;
    if (es2_read_span(&r, 4, &magic) != ES2_OK) return r.status;
    if (memcmp(magic.data, "VOLN", 4) != 0) return es2_fail(error, ES2_ERR_MAGIC, 0);
    (void)es2_read_u8(&r, &v.flag0); (void)es2_read_u8(&r, &v.flag1);
    (void)es2_read_u16(&r, &v.reserved); (void)es2_read_u8(&r, &v.priority);
    (void)es2_read_u8(&r, &directory_count);
    if (es2_read_u16(&r, &directory_bytes) != ES2_OK) return r.status;
    v.directory_count = directory_count;
    if (es2_read_span(&r, directory_bytes, &v.directory_bytes) != ES2_OK) return r.status;
    (void)es2_reader_init(&directories, v.directory_bytes, error);
    for (i = 0; i < v.directory_count; ++i) {
        size_t start = directories.offset;
        uint8_t c, last = 0;
        do {
            if (es2_read_u8(&directories, &c) != ES2_OK)
                return es2_fail(error, ES2_ERR_STRING, 12 + directories.offset);
            if (c != 0) {
                if (c < 32 || c >= 127 || c == '/')
                    return es2_fail(error, ES2_ERR_STRING, 12 + directories.offset - 1);
                last = c;
            }
        } while (c != 0);
        if (directories.offset - start < 3 || last != '\\')
            return es2_fail(error, ES2_ERR_STRING, 12 + start);
    }
    if (directories.offset != directories.span.size)
        return es2_fail(error, ES2_ERR_COUNT, 12 + directories.offset);
    (void)es2_read_u16(&r, &member_count);
    if (es2_read_u32(&r, &table_bytes) != ES2_OK) return r.status;
    v.member_count = member_count;
    if (table_bytes != (uint32_t)member_count * 18)
        return es2_fail(error, ES2_ERR_LENGTH, r.offset - 4);
    v.table_offset = r.offset;
    if (es2_read_span(&r, table_bytes, &v.member_bytes) != ES2_OK) return r.status;
    v.blocks_offset = r.offset; v.input = input;
    for (i = 0; i < v.member_count; ++i) {
        es2_vol_member current;
        size_t j, end;
        status = es2_vol_member_at(&v, i, &current, error);
        if (status != ES2_OK) return status;
        end = current.payload_offset + current.length + 1;
        for (j = 0; j < i; ++j) {
            es2_vol_member previous;
            size_t previous_end;
            status = es2_vol_member_at(&v, j, &previous, error);
            if (status != ES2_OK) return status;
            previous_end = previous.payload_offset + previous.length + 1;
            if (current.block_offset < previous_end && previous.block_offset < end)
                return es2_fail(error, ES2_ERR_OVERLAP, current.block_offset);
            if (same_name(current.name, previous.name) && same_name(current.directory, previous.directory))
                return es2_fail(error, ES2_ERR_STRING, v.table_offset + i * 18);
        }
        /* The checked difference avoids overflow even on 32-bit size_t. */
        if (end - current.block_offset > input.size - v.blocks_offset - total)
            return es2_fail(error, ES2_ERR_OVERLAP, current.block_offset);
        total += end - current.block_offset;
    }
    if (total != input.size - v.blocks_offset)
        return es2_fail(error, ES2_ERR_LENGTH, v.blocks_offset);
    *out = v;
    es2_error_clear(error);
    return ES2_OK;
}

es2_status es2_vol_read_tables(const es2_vol *vol,
    es2_vol_directory *directories, size_t directory_capacity,
    es2_vol_member *members, size_t member_capacity, es2_error *error)
{
    size_t i;
    es2_status status;
    if (vol == NULL) return es2_fail(error, ES2_ERR_ARGUMENT, 0);
    if (directory_capacity < vol->directory_count || member_capacity < vol->member_count)
        return es2_fail(error, ES2_ERR_CAPACITY, 0);
    if ((directories == NULL && vol->directory_count != 0) || (members == NULL && vol->member_count != 0))
        return es2_fail(error, ES2_ERR_ARGUMENT, 0);
    for (i = 0; i < vol->directory_count; ++i) {
        status = es2_vol_directory_at(vol, i, &directories[i].name, error);
        if (status != ES2_OK) return status;
    }
    for (i = 0; i < vol->member_count; ++i) {
        status = es2_vol_member_at(vol, i, &members[i], error);
        if (status != ES2_OK) return status;
    }
    return ES2_OK;
}

es2_status es2_vol_find(const es2_vol *vol, const char *path,
                       es2_vol_member *out, es2_error *error)
{
    size_t i, length;
    if (vol == NULL || path == NULL || out == NULL) return es2_fail(error, ES2_ERR_ARGUMENT, 0);
    length = strlen(path);
    for (i = 0; i < vol->member_count; ++i) {
        es2_vol_member m;
        size_t j;
        es2_status status = es2_vol_member_at(vol, i, &m, error);
        if (status != ES2_OK) return status;
        if (m.directory.size > SIZE_MAX - m.name.size || length != m.directory.size + m.name.size) continue;
        for (j = 0; j < length; ++j) {
            uint8_t c = j < m.directory.size ? m.directory.data[j] : m.name.data[j - m.directory.size];
            if (folded(c) != folded((uint8_t)path[j])) break;
        }
        if (j == length) { *out = m; return ES2_OK; }
    }
    return es2_fail(error, ES2_ERR_NOT_FOUND, 0);
}

es2_status es2_vol_payload(const es2_vol *vol, size_t index,
                          es2_span *out, es2_error *error)
{
    es2_vol_member member;
    es2_status status;
    if (out == NULL) return es2_fail(error, ES2_ERR_ARGUMENT, 0);
    status = es2_vol_member_at(vol, index, &member, error);
    if (status == ES2_OK) *out = member.payload;
    return status;
}
