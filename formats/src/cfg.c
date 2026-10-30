#include "es2/formats/cfg.h"
#include <string.h>

/* prefs.cfg block and the two INI files. Layouts and evidence:
 * docs/formats/cfg.md. */

es2_status es2_prefs_load(es2_span file, es2_prefs *out, es2_error *error)
{
    size_t n;
    if (out == NULL || (file.data == NULL && file.size != 0)) return es2_fail(error, ES2_ERR_ARGUMENT, 0);
    memset(out->byte, 0, sizeof out->byte);
    n = file.size < ES2_PREFS_SIZE ? file.size : ES2_PREFS_SIZE;
    if (n != 0) memcpy(out->byte, file.data, n);
    es2_error_clear(error);
    return ES2_OK;
}

void es2_prefs_serialize(const es2_prefs *prefs, uint8_t out[ES2_PREFS_SIZE])
{
    memcpy(out, prefs->byte, ES2_PREFS_SIZE);
}

es2_status es2_prefs_overlay(es2_span file, const es2_prefs *current,
                             const uint8_t *indices, size_t count,
                             uint8_t out[ES2_PREFS_SIZE], es2_error *error)
{
    uint8_t image[ES2_PREFS_SIZE];
    size_t i, n;
    if (current == NULL || out == NULL || (indices == NULL && count != 0) ||
        (file.data == NULL && file.size != 0))
        return es2_fail(error, ES2_ERR_ARGUMENT, 0);
    for (i = 0; i < count; ++i)
        if (indices[i] >= ES2_PREFS_SIZE) return es2_fail(error, ES2_ERR_OFFSET, i);
    memset(image, 0, sizeof image);
    n = file.size < ES2_PREFS_SIZE ? file.size : ES2_PREFS_SIZE;
    if (n != 0) memcpy(image, file.data, n);
    for (i = 0; i < count; ++i) image[indices[i]] = current->byte[indices[i]];
    memcpy(out, image, sizeof image);
    es2_error_clear(error);
    return ES2_OK;
}

es2_status es2_prefs_video_read(es2_span file, es2_prefs_video *out, es2_error *error)
{
    if (out == NULL || (file.data == NULL && file.size != 0)) return es2_fail(error, ES2_ERR_ARGUMENT, 0);
    if (file.size < 7) return es2_fail(error, ES2_ERR_TRUNCATED, file.size);
    out->resolution = file.data[4];
    out->display = file.data[6];
    es2_error_clear(error);
    return ES2_OK;
}

int es2_prefs_video_mode(uint8_t resolution_byte)
{
    return resolution_byte == 1 ? 0 : 3;
}

/* ---- INI subset ---------------------------------------------------------- */

static int is_blank(uint8_t c) { return c == ' ' || c == '\t'; }

static int upper(int c) { return c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c; }

/* Compares the bytes [p, p + n) with the NUL-terminated name, ignoring ASCII case. */
static int name_equal(const uint8_t *p, size_t n, const char *name)
{
    size_t i;
    for (i = 0; i < n; ++i) {
        if (name[i] == 0 || upper(p[i]) != upper((unsigned char)name[i])) return 0;
    }
    return name[n] == 0;
}

static int text_equal(const char *a, const char *b)
{
    size_t i;
    for (i = 0;; ++i) {
        if (upper((unsigned char)a[i]) != upper((unsigned char)b[i])) return 0;
        if (a[i] == 0) return 1;
    }
}

es2_status es2_cfg_ini_get(es2_span file, const char *section, const char *key,
                           char *out, size_t capacity, int *found, es2_error *error)
{
    size_t at = 0;
    int in_section = 0, hit = 0;
    if (section == NULL || key == NULL || out == NULL || capacity == 0 ||
        (file.data == NULL && file.size != 0))
        return es2_fail(error, ES2_ERR_ARGUMENT, 0);
    out[0] = 0;
    while (at < file.size && !hit) {
        const uint8_t *line = file.data + at;
        size_t end = at, n, b, e;
        while (end < file.size && file.data[end] != '\n') ++end;
        n = end - at;
        at = end < file.size ? end + 1 : end;
        if (n != 0 && line[n - 1] == '\r') --n;
        b = 0;
        while (b < n && is_blank(line[b])) ++b;
        e = n;
        while (e > b && is_blank(line[e - 1])) --e;
        if (b == e || line[b] == ';') continue;
        if (line[b] == '[') {
            size_t close = b + 1, s, t;
            while (close < e && line[close] != ']') ++close;
            if (close >= e) continue;
            s = b + 1;
            t = close;
            while (s < t && is_blank(line[s])) ++s;
            while (t > s && is_blank(line[t - 1])) --t;
            in_section = name_equal(line + s, t - s, section);
            continue;
        }
        if (in_section) {
            size_t eq = b, kt, v0, v1, copy;
            while (eq < e && line[eq] != '=') ++eq;
            if (eq >= e) continue;
            kt = eq;
            while (kt > b && is_blank(line[kt - 1])) --kt;
            if (!name_equal(line + b, kt - b, key)) continue;
            v0 = eq + 1;
            v1 = e;
            while (v0 < v1 && is_blank(line[v0])) ++v0;
            while (v1 > v0 && is_blank(line[v1 - 1])) --v1;
            if (v1 - v0 >= 2 && (line[v0] == '"' || line[v0] == '\'') && line[v1 - 1] == line[v0]) {
                ++v0;
                --v1;
            }
            copy = v1 - v0;
            if (copy > capacity - 1) copy = capacity - 1;
            if (copy != 0) memcpy(out, line + v0, copy);
            out[copy] = 0;
            hit = 1;
        }
    }
    if (found != NULL) *found = hit;
    es2_error_clear(error);
    return ES2_OK;
}

/* C runtime atol as the game uses it for Buffers and Rate: blanks, an optional
 * sign, decimal digits, stop at the first other character. */
static long cfg_atol(const char *s)
{
    long v = 0;
    int negative = 0;
    while (*s == ' ' || (*s >= '\t' && *s <= '\r')) ++s;
    if (*s == '-' || *s == '+') negative = *s++ == '-';
    while (*s >= '0' && *s <= '9') {
        if (v > (0x7fffffffL - (*s - '0')) / 10) { v = 0x7fffffffL; break; }
        v = v * 10 + (*s - '0');
        ++s;
    }
    return negative ? -v : v;
}

/* The game passes a 0x28-byte buffer: at most 39 characters. */
#define VALUE_BUFFER 0x28u

es2_status es2_cfg_sound_parse(es2_span file, es2_cfg_reader reader,
                               es2_cfg_sound *out, es2_error *error)
{
    char value[VALUE_BUFFER];
    es2_cfg_sound s;
    long number;
    es2_status status;
    if (out == NULL || (reader != ES2_CFG_READER_DBSIM && reader != ES2_CFG_READER_VSHELL))
        return es2_fail(error, ES2_ERR_ARGUMENT, 0);
    status = es2_cfg_ini_get(file, "Sound", "Driver", value, sizeof value, NULL, error);
    if (status != ES2_OK) return status;
    s.driver = (reader == ES2_CFG_READER_DBSIM && text_equal(value, "DirectSound"))
                   ? ES2_SOUND_DRIVER_DIRECTSOUND : ES2_SOUND_DRIVER_MME;
    status = es2_cfg_ini_get(file, "Sound", "Buffers", value, sizeof value, NULL, error);
    if (status != ES2_OK) return status;
    number = cfg_atol(value);
    s.buffers = (number >= 1 && number <= 0x40) ? (uint16_t)number : 5u;
    status = es2_cfg_ini_get(file, "Sound", "Rate", value, sizeof value, NULL, error);
    if (status != ES2_OK) return status;
    s.rate_code = cfg_atol(value) == 11 ? 0x10u : 0x20u;
    status = es2_cfg_ini_get(file, "Sound", "Width", value, sizeof value, NULL, error);
    if (status != ES2_OK) return status;
    s.width_code = text_equal(value, "Mono") ? 4u : 8u;
    *out = s;
    es2_error_clear(error);
    return ES2_OK;
}

static es2_status reverse_flag(es2_span file, const char *key, uint8_t *flag, es2_error *error)
{
    char value[VALUE_BUFFER];
    es2_status status = es2_cfg_ini_get(file, "Keyjoy", key, value, sizeof value, NULL, error);
    if (status != ES2_OK) return status;
    *flag = text_equal(value, "Reverse") ? 1u : 0u;
    return ES2_OK;
}

es2_status es2_cfg_keyjoy_parse(es2_span file, es2_cfg_keyjoy *out, es2_error *error)
{
    es2_cfg_keyjoy k;
    es2_status status;
    if (out == NULL) return es2_fail(error, ES2_ERR_ARGUMENT, 0);
    status = reverse_flag(file, "Tilt", &k.tilt, error);
    if (status != ES2_OK) return status;
    status = reverse_flag(file, "Backturn", &k.backturn, error);
    if (status != ES2_OK) return status;
    status = reverse_flag(file, "Missile", &k.missile, error);
    if (status != ES2_OK) return status;
    status = reverse_flag(file, "Rudder", &k.rudder, error);
    if (status != ES2_OK) return status;
    *out = k;
    es2_error_clear(error);
    return ES2_OK;
}

/* ---- tape files ---------------------------------------------------------- */

static const char *const tape_members[ES2_TAPE_MEMBERS] = {
    "script.dat", "player.mec", "mission.var", "prefs.cfg", "restore.dat", "object.str", "keyjoy.cfg"};

const char *es2_tape_member_name(unsigned index)
{
    return index < ES2_TAPE_MEMBERS ? tape_members[index] : NULL;
}

int es2_tape_member_in_tapes(unsigned index)
{
    return index == 3 || index == 6;
}

es2_status es2_tape_header_parse(es2_span file, es2_tape_header *out, es2_error *error)
{
    es2_tape_header h;
    es2_reader r;
    unsigned i;
    if (out == NULL || (file.data == NULL && file.size != 0)) return es2_fail(error, ES2_ERR_ARGUMENT, 0);
    if (es2_reader_init(&r, file, error) != ES2_OK) return r.status;
    for (i = 0; i < ES2_TAPE_MEMBERS; ++i) {
        uint32_t length;
        if (es2_read_u32(&r, &length) != ES2_OK || es2_read_span(&r, length, &h.member[i]) != ES2_OK) return r.status;
    }
    h.size = r.offset;
    *out = h;
    es2_error_clear(error);
    return ES2_OK;
}

es2_status es2_tape_entry_parse(es2_span file, size_t offset, int first,
                                es2_tape_entry *out, es2_error *error)
{
    es2_tape_entry e;
    es2_span rest;
    es2_reader r;
    unsigned i;
    if (out == NULL || (file.data == NULL && file.size != 0)) return es2_fail(error, ES2_ERR_ARGUMENT, 0);
    if (offset > file.size) return es2_fail(error, ES2_ERR_OFFSET, offset);
    rest.data = file.data == NULL ? NULL : file.data + offset;
    rest.size = file.size - offset;
    if (es2_reader_init(&r, rest, error) != ES2_OK) return r.status;
    memset(&e, 0, sizeof e);
    if (first && es2_read_span(&r, ES2_TAPE_PREFIX, &e.prefix) != ES2_OK) return r.status;
    if (es2_read_u16(&r, &e.key) != ES2_OK) return r.status;
    for (i = 0; i < 4; ++i)
        if (es2_read_u16(&r, &e.axis[i]) != ES2_OK) return r.status;
    if (es2_read_u16(&r, &e.time_scale) != ES2_OK || es2_read_u32(&r, &e.bits) != ES2_OK ||
        es2_read_u32(&r, &e.event_count) != ES2_OK || es2_read_u32(&r, &e.key_count) != ES2_OK)
        return r.status;
    if (e.event_count > es2_remaining(&r) / ES2_TAPE_EVENT ||
        es2_read_span(&r, (size_t)e.event_count * ES2_TAPE_EVENT, &e.events) != ES2_OK)
        return es2_fail(error, ES2_ERR_TRUNCATED, offset + r.offset);
    if (e.key_count > es2_remaining(&r) / 2u || es2_read_span(&r, (size_t)e.key_count * 2u, &e.keys) != ES2_OK)
        return es2_fail(error, ES2_ERR_TRUNCATED, offset + r.offset);
    e.size = r.offset;
    *out = e;
    es2_error_clear(error);
    return ES2_OK;
}
