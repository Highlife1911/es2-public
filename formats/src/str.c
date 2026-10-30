#include "es2/formats/str.h"
#include <string.h>

/* Packed string file framing. docs/formats/str.md has the layout and the
 * original reader/writer addresses. Offsets in errors are file offsets. */

#define BODY_BASE 4u
#define MAX_BODY 0x7fffffffu

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int16_t le16s(const uint8_t *p)
{
    return (int16_t)(uint16_t)((unsigned)p[0] | (unsigned)p[1] << 8);
}

static es2_status fail_at(es2_error *error, es2_status status, size_t body_offset)
{
    return es2_fail(error, status, BODY_BASE + body_offset);
}

/* i16 N at *offset; N < 0 is rejected. */
static es2_status read_count(es2_span body, size_t *offset, size_t *count, es2_error *error)
{
    int16_t n;
    if (body.size - *offset < 2) return fail_at(error, ES2_ERR_TRUNCATED, *offset);
    n = le16s(body.data + *offset);
    if (n < 0) return fail_at(error, ES2_ERR_COUNT, *offset);
    *count = (size_t)n;
    *offset += 2;
    return ES2_OK;
}

/* One string at *offset: i16 L, L bytes ending in NUL, u8 E, E bytes. */
static es2_status read_string(es2_span body, size_t *offset, unsigned flags,
                              es2_str_string *out, es2_error *error)
{
    size_t at = *offset, length, extra = 0;
    int16_t l;
    es2_str_string value;
    if (body.size - at < 2) return fail_at(error, ES2_ERR_TRUNCATED, at);
    l = le16s(body.data + at);
    if (l < 1) return fail_at(error, ES2_ERR_STRING, at);
    length = (size_t)l;
    at += 2;
    /* The text, its NUL and the E byte must be present. */
    if (body.size - at < length + 1) return fail_at(error, ES2_ERR_TRUNCATED, at);
    if (body.data[at + length - 1] != 0) return fail_at(error, ES2_ERR_STRING, at + length - 1);
    value.text.data = body.data + at;
    value.text.size = length - 1;
    value.tag = body.data[at + length];
    value.aux.data = body.data + at + length + 1;
    value.aux.size = 0;
    if ((flags & ES2_STR_TAG_BYTE) == 0) {
        /* The E byte is sign-extended and added to the cursor by the game. */
        extra = value.tag;
        if (extra > 127) return fail_at(error, ES2_ERR_STRING, at + length);
        if (body.size - (at + length + 1) < extra) return fail_at(error, ES2_ERR_TRUNCATED, at + length + 1);
        value.aux.size = extra;
    }
    *offset = at + length + 1 + extra;
    if (out != NULL) *out = value;
    return ES2_OK;
}

es2_status es2_str_parse(es2_span input, size_t max_groups, unsigned flags,
                         es2_str *out, es2_error *error)
{
    es2_str value;
    size_t offset = 0, groups = 0, strings = 0, body_size;
    uint32_t declared;
    es2_status status;
    if (out == NULL || (input.data == NULL && input.size != 0) || (flags & ~ES2_STR_TAG_BYTE) != 0)
        return es2_fail(error, ES2_ERR_ARGUMENT, 0);
    if (input.size < BODY_BASE) return es2_fail(error, ES2_ERR_TRUNCATED, input.size);
    declared = le32(input.data);
    if (declared > input.size - BODY_BASE) return es2_fail(error, ES2_ERR_LENGTH, 0);
    body_size = declared;
    memset(&value, 0, sizeof value);
    value.input = input;
    value.body.data = input.data + BODY_BASE;
    value.body.size = body_size;
    value.trailing.data = input.data + BODY_BASE + body_size;
    value.trailing.size = input.size - BODY_BASE - body_size;
    value.flags = flags;
    while (max_groups != 0 ? groups < max_groups : offset < body_size) {
        size_t n, i;
        status = read_count(value.body, &offset, &n, error);
        if (status != ES2_OK) return status;
        for (i = 0; i < n; ++i) {
            status = read_string(value.body, &offset, flags, NULL, error);
            if (status != ES2_OK) return status;
        }
        ++groups;
        strings += n;
    }
    value.group_count = groups;
    value.string_count = strings;
    value.groups_end = offset;
    *out = value;
    es2_error_clear(error);
    return ES2_OK;
}

es2_status es2_str_cursor_init(const es2_str *str, es2_str_cursor *cursor, es2_error *error)
{
    if (str == NULL || cursor == NULL) return es2_fail(error, ES2_ERR_ARGUMENT, 0);
    cursor->str = str;
    cursor->offset = 0;
    cursor->group = 0;
    cursor->remaining = 0;
    es2_error_clear(error);
    return ES2_OK;
}

es2_status es2_str_next_group(es2_str_cursor *cursor, size_t *count, es2_error *error)
{
    size_t offset, n;
    es2_status status;
    if (cursor == NULL || cursor->str == NULL || count == NULL) return es2_fail(error, ES2_ERR_ARGUMENT, 0);
    if (cursor->remaining != 0 || cursor->group >= cursor->str->group_count)
        return es2_fail(error, ES2_ERR_NOT_FOUND, BODY_BASE + cursor->offset);
    offset = cursor->offset;
    status = read_count(cursor->str->body, &offset, &n, error);
    if (status != ES2_OK) return status;
    cursor->offset = offset;
    cursor->remaining = n;
    cursor->group += 1;
    *count = n;
    es2_error_clear(error);
    return ES2_OK;
}

es2_status es2_str_next_string(es2_str_cursor *cursor, es2_str_string *out, es2_error *error)
{
    size_t offset;
    es2_status status;
    if (cursor == NULL || cursor->str == NULL || out == NULL) return es2_fail(error, ES2_ERR_ARGUMENT, 0);
    if (cursor->remaining == 0) return es2_fail(error, ES2_ERR_NOT_FOUND, BODY_BASE + cursor->offset);
    offset = cursor->offset;
    status = read_string(cursor->str->body, &offset, cursor->str->flags, out, error);
    if (status != ES2_OK) return status;
    cursor->offset = offset;
    cursor->remaining -= 1;
    es2_error_clear(error);
    return ES2_OK;
}

es2_status es2_str_group_size(const es2_str *str, size_t group, size_t *count, es2_error *error)
{
    es2_str_cursor c;
    size_t n = 0, g;
    es2_str_string s;
    if (str == NULL || count == NULL) return es2_fail(error, ES2_ERR_ARGUMENT, 0);
    if (group >= str->group_count) return es2_fail(error, ES2_ERR_NOT_FOUND, group);
    (void)es2_str_cursor_init(str, &c, error);
    for (g = 0; g <= group; ++g) {
        es2_status status = es2_str_next_group(&c, &n, error);
        if (status != ES2_OK) return status;
        if (g == group) break;
        while (c.remaining != 0) {
            status = es2_str_next_string(&c, &s, error);
            if (status != ES2_OK) return status;
        }
    }
    *count = n;
    es2_error_clear(error);
    return ES2_OK;
}

es2_status es2_str_string_at(const es2_str *str, size_t group, size_t index,
                             es2_str_string *out, es2_error *error)
{
    es2_str_cursor c;
    size_t n = 0, g, i;
    es2_str_string s;
    if (str == NULL || out == NULL) return es2_fail(error, ES2_ERR_ARGUMENT, 0);
    if (group >= str->group_count) return es2_fail(error, ES2_ERR_NOT_FOUND, group);
    (void)es2_str_cursor_init(str, &c, error);
    for (g = 0; g <= group; ++g) {
        es2_status status = es2_str_next_group(&c, &n, error);
        if (status != ES2_OK) return status;
        if (g == group) break;
        while (c.remaining != 0) {
            status = es2_str_next_string(&c, &s, error);
            if (status != ES2_OK) return status;
        }
    }
    if (index >= n) return es2_fail(error, ES2_ERR_NOT_FOUND, index);
    for (i = 0; i <= index; ++i) {
        es2_status status = es2_str_next_string(&c, &s, error);
        if (status != ES2_OK) return status;
    }
    *out = s;
    es2_error_clear(error);
    return ES2_OK;
}

static void put16(uint8_t *p, unsigned v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

es2_status es2_str_write(const es2_str_group_in *groups, size_t group_count, unsigned flags,
                         uint8_t *output, size_t capacity, size_t *required,
                         es2_error *error)
{
    size_t total = 0, g, i, at;
    if ((groups == NULL && group_count != 0) || (output == NULL && capacity != 0) ||
        (flags & ~ES2_STR_TAG_BYTE) != 0)
        return es2_fail(error, ES2_ERR_ARGUMENT, 0);
    for (g = 0; g < group_count; ++g) {
        if (groups[g].count > 32767u || (groups[g].strings == NULL && groups[g].count != 0))
            return es2_fail(error, ES2_ERR_COUNT, g);
        total += 2;
        for (i = 0; i < groups[g].count; ++i) {
            const es2_str_string *s = &groups[g].strings[i];
            if ((s->text.data == NULL && s->text.size != 0) || (s->aux.data == NULL && s->aux.size != 0))
                return es2_fail(error, ES2_ERR_ARGUMENT, g);
            if (s->text.size > 32766u || memchr(s->text.data, 0, s->text.size) != NULL)
                return es2_fail(error, ES2_ERR_STRING, i);
            if (s->aux.size > 127u || ((flags & ES2_STR_TAG_BYTE) != 0 && s->aux.size != 0))
                return es2_fail(error, ES2_ERR_LENGTH, i);
            total += 2 + s->text.size + 1 + 1 + s->aux.size;
            if (total > MAX_BODY) return es2_fail(error, ES2_ERR_LENGTH, g);
        }
    }
    if (required != NULL) *required = BODY_BASE + total;
    if (output == NULL && capacity == 0) { es2_error_clear(error); return ES2_OK; }
    if (capacity < BODY_BASE + total) return es2_fail(error, ES2_ERR_CAPACITY, capacity);
    output[0] = (uint8_t)total;
    output[1] = (uint8_t)(total >> 8);
    output[2] = (uint8_t)(total >> 16);
    output[3] = (uint8_t)(total >> 24);
    at = BODY_BASE;
    for (g = 0; g < group_count; ++g) {
        put16(output + at, (unsigned)groups[g].count);
        at += 2;
        for (i = 0; i < groups[g].count; ++i) {
            const es2_str_string *s = &groups[g].strings[i];
            put16(output + at, (unsigned)(s->text.size + 1));
            at += 2;
            if (s->text.size != 0) memcpy(output + at, s->text.data, s->text.size);
            at += s->text.size;
            output[at++] = 0;
            output[at++] = (flags & ES2_STR_TAG_BYTE) != 0 ? s->tag : (uint8_t)s->aux.size;
            if (s->aux.size != 0) memcpy(output + at, s->aux.data, s->aux.size);
            at += s->aux.size;
        }
    }
    es2_error_clear(error);
    return ES2_OK;
}
