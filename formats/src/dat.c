#include "es2/formats/dat.h"

#include <string.h>

#define TRY(operation) do { es2_status st_ = (operation); if (st_ != ES2_OK) return st_; } while (0)

static es2_status bad(es2_reader *r, es2_status status, size_t offset)
{
    if (r->status == ES2_OK) {
        r->status = status;
        (void)es2_fail(r->error, status, offset);
    }
    return r->status;
}

static es2_status count16(es2_reader *r, size_t *out)
{
    int16_t value;
    size_t offset = r->offset;
    TRY(es2_read_i16(r, &value));
    if (value < 0) return bad(r, ES2_ERR_COUNT, offset);
    *out = (size_t)value;
    return ES2_OK;
}

static es2_status count32(es2_reader *r, size_t *out)
{
    int32_t value;
    size_t offset = r->offset;
    TRY(es2_read_i32(r, &value));
    if (value < 0) return bad(r, ES2_ERR_COUNT, offset);
    *out = (size_t)value;
    return ES2_OK;
}

static es2_status elements(es2_reader *r, size_t count, size_t stride,
                            es2_span *out)
{
    if (count > es2_remaining(r) / stride)
        return bad(r, ES2_ERR_TRUNCATED, r->offset);
    if (out != NULL) return es2_read_span(r, count * stride, out);
    return es2_skip(r, count * stride);
}

static es2_status slot_text(es2_span slot, es2_span *text, es2_error *error,
                            size_t offset, int final_only)
{
    size_t i;
    for (i = 0; i < slot.size; ++i) {
        if (slot.data[i] == 0) {
            if (final_only && i != slot.size - 1)
                return es2_fail(error, ES2_ERR_STRING, offset + i);
            text->data = slot.data;
            text->size = i;
            return ES2_OK;
        }
        if (slot.data[i] > 127)
            return es2_fail(error, ES2_ERR_STRING, offset + i);
    }
    return es2_fail(error, ES2_ERR_STRING, offset + slot.size);
}

static size_t fixed_stride(es2_dat_kind kind)
{
    switch (kind) {
    case ES2_DAT_PROJ: return 36;
    case ES2_DAT_ROCKETS: case ES2_DAT_BULLETS: return 14;
    case ES2_DAT_BEAM: return 6;
    case ES2_DAT_LC_WPNS: return 22;
    case ES2_DAT_HERC_INF: return 16;
    case ES2_DAT_MFORMS: return 28;
    case ES2_DAT_FFORMS: return 18;
    case ES2_DAT_MAT0: return 8;
    case ES2_DAT_THEATER: return 14;
    case ES2_DAT_TEX: return 4;
    case ES2_DAT_STYLES: return 64;
    case ES2_DAT_ARM_WEAP: return 12;
    case ES2_DAT_EXPLOS: return 4;
    case ES2_DAT_VEHICLE: return 216;
    case ES2_DAT_SKIMMER: return 46;
    case ES2_DAT_ZONE: return 16;
    case ES2_DAT_COLORS: return 54;
    case ES2_DAT_DAMAGE: return 102;
    default: return 0;
    }
}

/* Walk one variable primary record, retaining neutral byte spans in the view.
 * Every loop consumes a count or record byte range; no implicit alignment.
 */
static es2_status record_read(es2_dat_kind kind, es2_reader *r,
                               size_t *children, size_t *lists, size_t *groups)
{
    size_t n = 0, m = 0, i, j;
    uint8_t byte;
    uint16_t word;
    es2_span text, ignored;
    switch (kind) {
    case ES2_DAT_SIM_WEAPONS:
        TRY(es2_skip(r, 6)); TRY(count16(r, &n));
        TRY(elements(r, n, 4, NULL)); *children += n;
        TRY(es2_skip(r, 2)); TRY(count16(r, &n));
        TRY(elements(r, n, 8, NULL)); *lists += n;
        return es2_skip(r, 48);
    case ES2_DAT_DEBRIS: case ES2_DAT_ARM: case ES2_DAT_RPR:
    case ES2_DAT_HOTSPOTS: case ES2_DAT_CAREER:
        TRY(es2_skip(r, 2)); TRY(count16(r, &n));
        m = kind == ES2_DAT_DEBRIS || kind == ES2_DAT_RPR ? 14 :
            kind == ES2_DAT_ARM ? 22 : kind == ES2_DAT_HOTSPOTS ? 16 : 2;
        TRY(elements(r, n, m, NULL)); *children += n;
        return ES2_OK;
    case ES2_DAT_LOADOUT: case ES2_DAT_HERCS: case ES2_DAT_TRN_HERC:
        TRY(es2_skip(r, kind == ES2_DAT_HERCS ? 8 : 6));
        TRY(count16(r, &n)); TRY(elements(r, n, 8, NULL));
        *children += n; return ES2_OK;
    case ES2_DAT_SHELL_WEAPONS:
        TRY(es2_skip(r, 2)); TRY(count16(r, &n));
        if (n == 0) return bad(r, ES2_ERR_COUNT, r->offset - 2);
        i = r->offset; TRY(es2_read_span(r, n, &text));
        TRY(slot_text(text, &ignored, r->error, i, 1));
        return es2_skip(r, 5);
    case ES2_DAT_BFORMS:
        TRY(count32(r, &n)); TRY(elements(r, n, 10, NULL));
        *children += n; TRY(es2_skip(r, 12)); TRY(count32(r, &m));
        for (i = 0; i < m; ++i) {
            TRY(es2_read_u8(r, &byte));
            if (byte > 127) return bad(r, ES2_ERR_COUNT, r->offset - 1);
            TRY(es2_skip(r, byte));
        }
        *lists += m; return ES2_OK;
    case ES2_DAT_BASES:
        TRY(es2_skip(r, 18)); TRY(count16(r, &n));
        TRY(elements(r, n, 30, NULL)); *children += n;
        return es2_skip(r, 28);
    case ES2_DAT_BASECOL:
        TRY(count16(r, &n));
        *groups += n;
        for (i = 0; i < n; ++i) {
            TRY(es2_skip(r, 2)); TRY(count16(r, &m));
            for (j = 0; j < m; ++j) {
                size_t p = 0;
                TRY(es2_skip(r, 2)); TRY(count16(r, &p));
                TRY(elements(r, p, 8, NULL)); *children += p;
            }
            *lists += m;
        }
        return ES2_OK;
    case ES2_DAT_SHADES:
        TRY(es2_skip(r, 1)); TRY(es2_read_u8(r, &byte));
        TRY(elements(r, byte, 2, NULL)); *children += byte;
        return ES2_OK;
    case ES2_DAT_MAP_INFO: case ES2_DAT_MAP_LOCS:
        TRY(es2_skip(r, 2)); TRY(es2_read_u16(r, &word));
        TRY(elements(r, word, kind == ES2_DAT_MAP_INFO ? 4 : 34, NULL));
        *children += word; return ES2_OK;
    default: return es2_fail(r->error, ES2_ERR_UNSUPPORTED, r->offset);
    }
}

static es2_status run(es2_dat_kind kind, es2_reader *r, size_t count,
                       es2_span *bytes, size_t *children, size_t *lists, size_t *groups)
{
    size_t start = r->offset, i, stride = fixed_stride(kind);
    if (stride != 0) TRY(elements(r, count, stride, NULL));
    else for (i = 0; i < count; ++i) TRY(record_read(kind, r, children, lists, groups));
    return es2_subspan(r->span, start, r->offset - start, bytes, r->error);
}

es2_status es2_dat_parse(es2_dat_kind kind, es2_span input,
                         size_t external_count, es2_dat *out, es2_error *error)
{
    es2_dat value = {0};
    es2_reader r;
    size_t start, n = 0;
    uint16_t u16;
    uint32_t u32;
    uint8_t u8;
    es2_span text, ignored;
    if (out == NULL) return es2_fail(error, ES2_ERR_ARGUMENT, 0);
    TRY(es2_reader_init(&r, input, error));
    if (kind <= ES2_DAT_UNKNOWN || kind >= ES2_DAT_KIND_COUNT)
        return es2_fail(error, ES2_ERR_UNSUPPORTED, 0);
    value.kind = kind; value.input = input; value.support = ES2_DAT_READER_FRAMING;
    switch (kind) {
    case ES2_DAT_VEHICLE: case ES2_DAT_SKIMMER:
        value.count = 1;
        TRY(elements(&r, 1, fixed_stride(kind), &value.records));
        n = kind == ES2_DAT_VEHICLE ? 0x56 : 0x12;
        TRY(es2_subspan(input, n, 12, &text, error));
        TRY(slot_text(text, &ignored, error, n, 0));
        if (kind == ES2_DAT_VEHICLE) {
            TRY(es2_subspan(input, 0xcc, 12, &text, error));
            TRY(slot_text(text, &ignored, error, 0xcc, 0));
        }
        break;
    case ES2_DAT_COLORS: case ES2_DAT_ZONE:
        value.count = 1;
        TRY(elements(&r, 1, fixed_stride(kind), &value.records));
        break;
    case ES2_DAT_DAMAGE:
        TRY(es2_skip(&r, 34)); TRY(count16(&r, &n));
        if (n != 33) return es2_fail(error, ES2_ERR_COUNT, 34);
        TRY(elements(&r, n, 2, NULL));
        value.count = 1; value.child_count = n;
        TRY(es2_subspan(input, 0, r.offset, &value.records, error));
        break;
    case ES2_DAT_FIRE:
        value.support = ES2_DAT_EXTERNAL_COUNT;
        TRY(es2_read_span(&r, 4, &value.secondary));
        value.secondary_count = 1;
        TRY(es2_read_span(&r, external_count, &value.records));
        value.count = external_count;
        break;
    case ES2_DAT_MAT0:
        value.support = ES2_DAT_PREFIX_WITH_SUFFIX;
        TRY(count32(&r, &value.count));
        TRY(elements(&r, value.count, 8, &value.records));
        TRY(es2_read_span(&r, es2_remaining(&r), &value.suffix));
        break;
    case ES2_DAT_THEATER: case ES2_DAT_STYLES: case ES2_DAT_TEX:
        value.support = ES2_DAT_DATA_FIT_ONLY;
        if (kind == ES2_DAT_TEX) {
            TRY(es2_read_u32(&r, &u32)); value.count = (size_t)u32;
        } else { TRY(es2_read_u16(&r, &u16)); value.count = u16; }
        TRY(elements(&r, value.count, fixed_stride(kind), &value.records));
        break;
    case ES2_DAT_LOADOUT:
        value.count = 1;
        TRY(run(kind, &r, 1, &value.records, &value.child_count, &value.byte_list_count, &value.nested_group_count));
        break;
    case ES2_DAT_TRN_HERC: case ES2_DAT_MAP_INFO: case ES2_DAT_MAP_LOCS:
        value.support = ES2_DAT_DATA_FIT_ONLY;
        start = r.offset;
        while (es2_remaining(&r) != 0) {
            TRY(record_read(kind, &r, &value.child_count, &value.byte_list_count, &value.nested_group_count));
            ++value.count;
        }
        TRY(es2_subspan(input, start, r.offset - start, &value.records, error));
        break;
    case ES2_DAT_ARM:
        value.secondary_count = 2;
        TRY(elements(&r, 2, 22, &value.secondary));
        TRY(count16(&r, &value.count));
        TRY(run(kind, &r, value.count, &value.records, &value.child_count, &value.byte_list_count, &value.nested_group_count));
        break;
    case ES2_DAT_RPR:
        TRY(count16(&r, &value.secondary_count));
        ++value.secondary_count; /* The separately serialized extra R14. */
        TRY(elements(&r, value.secondary_count, 14, &value.secondary));
        TRY(count16(&r, &value.count));
        TRY(run(kind, &r, value.count, &value.records, &value.child_count, &value.byte_list_count, &value.nested_group_count));
        break;
    case ES2_DAT_BASECOL:
        value.support = ES2_DAT_EXTERNAL_COUNT;
        value.count = external_count;
        if (external_count > es2_remaining(&r) / 2)
            return es2_fail(error, ES2_ERR_TRUNCATED, 0);
        TRY(run(kind, &r, value.count, &value.records, &value.child_count, &value.byte_list_count, &value.nested_group_count));
        break;
    case ES2_DAT_BFORMS:
        TRY(count32(&r, &value.count));
        TRY(run(kind, &r, value.count, &value.records, &value.child_count, &value.byte_list_count, &value.nested_group_count));
        break;
    case ES2_DAT_SHADES:
        TRY(es2_read_span(&r, 1, &value.secondary)); /* Raw capacity byte. */
        value.secondary_count = 1;
        TRY(es2_read_u8(&r, &u8)); value.count = u8;
        TRY(run(kind, &r, value.count, &value.records, &value.child_count, &value.byte_list_count, &value.nested_group_count));
        break;
    default:
        TRY(count16(&r, &value.count));
        if (kind == ES2_DAT_HOTSPOTS && value.count != 9)
            return es2_fail(error, ES2_ERR_COUNT, 0);
        TRY(run(kind, &r, value.count, &value.records, &value.child_count, &value.byte_list_count, &value.nested_group_count));
        if (kind == ES2_DAT_SHELL_WEAPONS || kind == ES2_DAT_ARM_WEAP || kind == ES2_DAT_EXPLOS) {
            TRY(count16(&r, &value.secondary_count));
            n = kind == ES2_DAT_SHELL_WEAPONS ? 6 : kind == ES2_DAT_ARM_WEAP ? 12 : 40;
            TRY(elements(&r, value.secondary_count, n, &value.secondary));
        }
        break;
    }
    TRY(es2_reader_end(&r));
    *out = value;
    return ES2_OK;
}

es2_status es2_dat_record_at(const es2_dat *table, size_t index,
                             es2_span *out, es2_error *error)
{
    es2_reader r;
    size_t i, start = 0, children = 0, lists = 0, groups = 0, stride;
    if (table == NULL || out == NULL) return es2_fail(error, ES2_ERR_ARGUMENT, index);
    if (index >= table->count) return es2_fail(error, ES2_ERR_OFFSET, index);
    stride = table->kind == ES2_DAT_FIRE ? 1 : fixed_stride(table->kind);
    if (stride != 0) {
        if (index > table->records.size / stride)
            return es2_fail(error, ES2_ERR_OFFSET, index);
        return es2_subspan(table->records, index * stride, stride, out, error);
    }
    TRY(es2_reader_init(&r, table->records, error));
    for (i = 0; i <= index; ++i) {
        start = r.offset;
        TRY(record_read(table->kind, &r, &children, &lists, &groups));
    }
    return es2_subspan(table->records, start, r.offset - start, out, error);
}

static unsigned char fold(unsigned char c)
{
    if (c >= 'A' && c <= 'Z') return (unsigned char)(c + ('a' - 'A'));
    return c == '/' ? '\\' : c;
}

static int path_equal(const char *a, const char *b)
{
    while (*a != 0 && *b != 0) {
        if (fold((unsigned char)*a) != fold((unsigned char)*b)) return 0;
        ++a; ++b;
    }
    return *a == *b;
}

static int basename_in(const char *name, const char *const *names, size_t count)
{
    size_t i;
    for (i = 0; i < count; ++i) if (path_equal(name, names[i])) return 1;
    return 0;
}

es2_dat_kind es2_dat_classify(const char *path)
{
    static const char *const vehicles[] = {
        "OUTLAW.DAT", "RAPTOR2.DAT", "TOMAHAWK.DAT", "SAMSON.DAT", "COLOSSUS.DAT",
        "APOCA.DAT", "OGRE.DAT", "MAVERICK.DAT", "RAZOR.DAT", "MONGOOSE.DAT",
        "STINGRAY.DAT", "MIRIMAC.DAT", "RAMSES.DAT", "ACHILLES.DAT", "HYPERION.DAT",
        "PITBULL.DAT", "SPIDER.DAT", "DIABLO.DAT", "HEADHUNT.DAT", "SCARAB.DAT", "CERBERUS.DAT"
    };
    static const char *const debris[] = {
        "OUTL_DEB.DAT", "RAPT_DEB.DAT", "TOMA_DEB.DAT", "SAMS_DEB.DAT", "COLO_DEB.DAT",
        "APOC_DEB.DAT", "OGRE_DEB.DAT", "MAVE_DEB.DAT", "MONG_DEB.DAT", "STIN_DEB.DAT",
        "MIRI_DEB.DAT", "RAMS_DEB.DAT", "ACHI_DEB.DAT", "HYPE_DEB.DAT", "PITB_DEB.DAT",
        "DIAB_DEB.DAT", "HEAD_DEB.DAT", "SCAR_DEB.DAT", "CERB_DEB.DAT", "BASE_DEB.DAT", "DEF_DEB.DAT"
    };
    static const char *const suffixes[] = {
        "APOC.DAT", "COLO.DAT", "MAVR.DAT", "OGRE.DAT", "OUTL.DAT", "RAPT.DAT", "RAZR.DAT", "SAMS.DAT", "TOMA.DAT"
    };
    static const struct { const char *path; es2_dat_kind kind; } names[] = {
        {"dat\\WEAPONS.DAT", ES2_DAT_SIM_WEAPONS}, {"dat\\SKIMMER.DAT", ES2_DAT_SKIMMER},
        {"dat\\PROJ.DAT", ES2_DAT_PROJ}, {"dat\\ROCKETS.DAT", ES2_DAT_ROCKETS},
        {"dat\\BULLETS.DAT", ES2_DAT_BULLETS}, {"dat\\BEAM.DAT", ES2_DAT_BEAM},
        {"dat\\LC_WPNS.DAT", ES2_DAT_LC_WPNS}, {"dat\\MFORMS.DAT", ES2_DAT_MFORMS},
        {"dat\\FFORMS.DAT", ES2_DAT_FFORMS}, {"dat\\BFORMS.DAT", ES2_DAT_BFORMS},
        {"dat\\BASES.DAT", ES2_DAT_BASES}, {"dat\\BASECOL.DAT", ES2_DAT_BASECOL},
        {"dat\\EXPLOS.DAT", ES2_DAT_EXPLOS}, {"dat\\EXPLOS2.DAT", ES2_DAT_EXPLOS},
        {"dat\\COLORS.DAT", ES2_DAT_COLORS}, {"dat\\FIRE.DAT", ES2_DAT_FIRE},
        {"dat\\SHADES.DAT", ES2_DAT_SHADES}, {"dat\\MAT0.DAT", ES2_DAT_MAT0},
        {"dat\\TEX.DAT", ES2_DAT_TEX}, {"dat\\STYLES.DAT", ES2_DAT_STYLES},
        {"DATA\\MAT0.DAT", ES2_DAT_MAT0}, {"DATA\\MFORMS.DAT", ES2_DAT_MFORMS},
        {"GAM\\HERC_INF.DAT", ES2_DAT_HERC_INF}, {"GAM\\HERCS.DAT", ES2_DAT_HERCS},
        {"GAM\\TRN_HERC.DAT", ES2_DAT_TRN_HERC}, {"GAM\\WEAPONS.DAT", ES2_DAT_SHELL_WEAPONS},
        {"GAM\\DAMAGE.DAT", ES2_DAT_DAMAGE}, {"GAM\\ARM_WEAP.DAT", ES2_DAT_ARM_WEAP},
        {"GAM\\ARM_HOTS.DAT", ES2_DAT_HOTSPOTS}, {"GAM\\RPR_HOTS.DAT", ES2_DAT_HOTSPOTS},
        {"GAM\\CAREER.DAT", ES2_DAT_CAREER}, {"GAM\\THEATER.DAT", ES2_DAT_THEATER},
        {"GAM\\MAP_INFO.DAT", ES2_DAT_MAP_INFO}, {"GAM\\MAP_LOCS.DAT", ES2_DAT_MAP_LOCS}
    };
    size_t i, length;
    if (path == NULL) return ES2_DAT_UNKNOWN;
    length = strlen(path);
    for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        if (path_equal(path, names[i].path)) return names[i].kind;
    if (length >= 4 && fold((unsigned char)path[0]) == 'd' &&
        fold((unsigned char)path[1]) == 'a' && fold((unsigned char)path[2]) == 't' &&
        fold((unsigned char)path[3]) == '\\') {
        if (basename_in(path + 4, vehicles, sizeof(vehicles) / sizeof(vehicles[0]))) return ES2_DAT_VEHICLE;
        if (basename_in(path + 4, debris, sizeof(debris) / sizeof(debris[0]))) return ES2_DAT_DEBRIS;
        if (length > 12 && fold((unsigned char)path[4]) == 'z' &&
            fold((unsigned char)path[5]) == 'o' && fold((unsigned char)path[6]) == 'n' &&
            fold((unsigned char)path[7]) == 'e') {
            i = 8;
            while (path[i] >= '0' && path[i] <= '9') ++i;
            if (i > 8 && path_equal(path + i, ".DAT")) return ES2_DAT_ZONE;
        }
    }
    if (length >= 8 && fold((unsigned char)path[0]) == 'g' &&
        fold((unsigned char)path[1]) == 'a' && fold((unsigned char)path[2]) == 'm' &&
        fold((unsigned char)path[3]) == '\\' && path[7] == '_' &&
        basename_in(path + 8, suffixes, sizeof(suffixes) / sizeof(suffixes[0]))) {
        if (fold((unsigned char)path[4]) == 'a' && fold((unsigned char)path[5]) == 'r' && fold((unsigned char)path[6]) == 'm') return ES2_DAT_ARM;
        if (fold((unsigned char)path[4]) == 'r' && fold((unsigned char)path[5]) == 'p' && fold((unsigned char)path[6]) == 'r') return ES2_DAT_RPR;
        if (fold((unsigned char)path[4]) == 'i' && fold((unsigned char)path[5]) == 'n' && fold((unsigned char)path[6]) == 'i') return ES2_DAT_LOADOUT;
    }
    return ES2_DAT_UNKNOWN;
}

const char *es2_dat_kind_name(es2_dat_kind kind)
{
    static const char *const names[ES2_DAT_KIND_COUNT] = {
        "unknown", "sim-weapons", "vehicle", "skimmer", "debris", "proj", "rockets", "bullets",
        "beam", "lc-wpns", "herc-inf", "loadout", "hercs", "trn-herc", "shell-weapons", "damage",
        "arm", "rpr", "arm-weap", "hotspots", "mforms", "fforms", "bforms", "bases", "basecol",
        "explos", "colors", "fire", "shades", "mat0", "zone", "career", "theater", "map-info", "map-locs", "tex", "styles"
    };
    return kind >= ES2_DAT_UNKNOWN && kind < ES2_DAT_KIND_COUNT ? names[kind] : NULL;
}

static es2_status typed(const es2_dat *table, es2_dat_kind kind,
                         const void *out, es2_error *error)
{
    if (table == NULL || out == NULL || table->kind != kind)
        return es2_fail(error, ES2_ERR_ARGUMENT, 0);
    return ES2_OK;
}

static es2_status signed_word(es2_span input, size_t offset, int16_t *out,
                               es2_error *error)
{
    es2_reader r;
    TRY(es2_reader_init(&r, input, error)); TRY(es2_skip(&r, offset));
    return es2_read_i16(&r, out);
}

es2_status es2_dat_vehicle_get(const es2_dat *table, es2_dat_vehicle *out,
                               es2_error *error)
{
    es2_dat_vehicle v;
    es2_span slot;
    TRY(typed(table, ES2_DAT_VEHICLE, out, error));
    v.raw = table->input;
    TRY(es2_subspan(v.raw, 0x00, 2, &v.bytes_0x00, error));
    TRY(es2_subspan(v.raw, 0x06, 38, &v.bytes_0x06, error));
    TRY(es2_subspan(v.raw, 0x2e, 20, &v.bytes_0x2e, error));
    TRY(es2_subspan(v.raw, 0x42, 12, &v.bytes_0x42, error));
    TRY(es2_subspan(v.raw, 0x50, 6, &v.bytes_0x50, error));
    TRY(es2_subspan(v.raw, 0x62, 10, &v.bytes_0x62, error));
    TRY(es2_subspan(v.raw, 0x6e, 38, &v.bytes_0x6e, error));
    TRY(es2_subspan(v.raw, 0x96, 42, &v.bytes_0x96, error));
    TRY(es2_subspan(v.raw, 0xc8, 4, &v.bytes_0xc8, error));
    TRY(signed_word(v.raw, 0x02, &v.reverse_speed_parameter, error));
    TRY(signed_word(v.raw, 0x04, &v.forward_speed_parameter, error));
    TRY(signed_word(v.raw, 0x2c, &v.parameter_0x2c, error));
    TRY(signed_word(v.raw, 0x4e, &v.branch_selector, error));
    TRY(signed_word(v.raw, 0x6c, &v.parameter_0x6c, error));
    TRY(signed_word(v.raw, 0x94, &v.resource_index, error));
    TRY(signed_word(v.raw, 0xc0, &v.hud_scale, error));
    TRY(signed_word(v.raw, 0xc2, &v.rescale_denominator, error));
    TRY(signed_word(v.raw, 0xc4, &v.rescale_numerator, error));
    TRY(signed_word(v.raw, 0xc6, &v.word_0xc6, error));
    TRY(es2_subspan(v.raw, 0x56, 12, &slot, error));
    TRY(slot_text(slot, &v.label, error, 0x56, 0));
    TRY(es2_subspan(v.raw, 0xcc, 12, &slot, error));
    TRY(slot_text(slot, &v.debris_resource, error, 0xcc, 0));
    *out = v; return ES2_OK;
}

es2_status es2_dat_skimmer_get(const es2_dat *table, es2_dat_skimmer *out,
                               es2_error *error)
{
    es2_dat_skimmer v;
    es2_span slot;
    TRY(typed(table, ES2_DAT_SKIMMER, out, error));
    TRY(es2_subspan(table->input, 0, 18, &v.parameters_0x00, error));
    TRY(es2_subspan(table->input, 18, 12, &slot, error));
    TRY(slot_text(slot, &v.label, error, 18, 0));
    TRY(es2_subspan(table->input, 30, 16, &v.parameters_0x1e, error));
    *out = v; return ES2_OK;
}

es2_status es2_dat_sim_weapon_at(const es2_dat *table, size_t index,
                                 es2_dat_sim_weapon *out, es2_error *error)
{
    es2_dat_sim_weapon v;
    es2_span record = {0};
    es2_reader r;
    size_t i;
    TRY(typed(table, ES2_DAT_SIM_WEAPONS, out, error));
    TRY(es2_dat_record_at(table, index, &record, error));
    TRY(es2_reader_init(&r, record, error));
    for (i = 0; i < 3; ++i) TRY(es2_read_u16(&r, &v.words_0x00[i]));
    TRY(count16(&r, &v.count_a)); TRY(elements(&r, v.count_a, 4, &v.array_a));
    TRY(es2_read_u16(&r, &v.selector)); TRY(count16(&r, &v.count_b));
    TRY(elements(&r, v.count_b, 8, &v.array_b));
    TRY(es2_subspan(record, r.offset, 48, &v.tail, error));
    for (i = 0; i < 4; ++i) TRY(es2_read_i16(&r, &v.model_indices[i]));
    TRY(es2_read_span(&r, 6, &v.bytes_0x08)); TRY(es2_read_u32(&r, &v.dword_0x0e));
    TRY(es2_read_u16(&r, &v.word_0x12)); TRY(es2_read_i16(&r, &v.threshold));
    TRY(es2_read_i16(&r, &v.discharge_amount)); TRY(es2_read_i16(&r, &v.initial_ammunition));
    TRY(es2_read_u16(&r, &v.firing_layout)); TRY(es2_read_i16(&r, &v.projectile_selection));
    for (i = 0; i < 3; ++i) TRY(es2_read_i16(&r, &v.offsets_0x1e[i]));
    for (i = 0; i < 3; ++i) TRY(es2_read_u16(&r, &v.words_0x24[i]));
    TRY(es2_read_i16(&r, &v.cooldown_seed)); TRY(es2_read_u16(&r, &v.word_0x2c));
    TRY(es2_read_u16(&r, &v.word_0x2e)); TRY(es2_reader_end(&r));
    *out = v; return ES2_OK;
}

es2_status es2_dat_herc_info_at(const es2_dat *table, size_t index,
                                es2_dat_herc_info *out, es2_error *error)
{
    es2_dat_herc_info v;
    es2_span record = {0};
    es2_reader r;
    TRY(typed(table, ES2_DAT_HERC_INF, out, error));
    TRY(es2_dat_record_at(table, index, &record, error));
    TRY(es2_reader_init(&r, record, error));
    TRY(es2_read_i16(&r, &v.herc_id)); TRY(es2_read_i16(&r, &v.word_0x02));
    TRY(es2_read_i16(&r, &v.word_0x04)); TRY(es2_read_i16(&r, &v.word_0x06));
    TRY(es2_read_i16(&r, &v.word_0x08)); TRY(es2_read_u16(&r, &v.word_0x0a));
    TRY(es2_read_i16(&r, &v.build_progress_divisor)); TRY(es2_read_u16(&r, &v.word_0x0e));
    *out = v; return ES2_OK;
}

es2_status es2_dat_loadout_at(const es2_dat *table, size_t index,
                              es2_dat_loadout *out, es2_error *error)
{
    es2_dat_loadout v = {0};
    es2_span record = {0};
    es2_reader r;
    if (table == NULL || out == NULL || (table->kind != ES2_DAT_LOADOUT &&
        table->kind != ES2_DAT_HERCS && table->kind != ES2_DAT_TRN_HERC))
        return es2_fail(error, ES2_ERR_ARGUMENT, index);
    TRY(es2_dat_record_at(table, index, &record, error));
    TRY(es2_reader_init(&r, record, error));
    v.has_bay_slot = table->kind == ES2_DAT_HERCS;
    if (v.has_bay_slot) TRY(es2_read_i16(&r, &v.bay_slot));
    TRY(es2_read_i16(&r, &v.herc_id)); TRY(es2_read_u16(&r, &v.word_0x02));
    TRY(es2_read_u16(&r, &v.word_0x04)); TRY(count16(&r, &v.slot_count));
    TRY(elements(&r, v.slot_count, 8, &v.slots));
    *out = v; return ES2_OK;
}

es2_status es2_dat_loadout_slot_at(const es2_dat_loadout *loadout, size_t index,
                                   es2_dat_loadout_slot *out, es2_error *error)
{
    es2_dat_loadout_slot v;
    es2_span record = {0};
    es2_reader r;
    if (loadout == NULL || out == NULL) return es2_fail(error, ES2_ERR_ARGUMENT, index);
    if (index >= loadout->slot_count || index > loadout->slots.size / 8)
        return es2_fail(error, ES2_ERR_OFFSET, index);
    TRY(es2_subspan(loadout->slots, index * 8, 8, &record, error));
    TRY(es2_reader_init(&r, record, error));
    TRY(es2_read_i16(&r, &v.slot_id)); TRY(es2_read_i16(&r, &v.item_id));
    TRY(es2_read_u16(&r, &v.word_0x04)); TRY(es2_read_u16(&r, &v.word_0x06));
    *out = v; return ES2_OK;
}

es2_status es2_dat_shell_weapon_at(const es2_dat *table, size_t index,
                                   es2_dat_shell_weapon *out, es2_error *error)
{
    es2_dat_shell_weapon v;
    es2_span record = {0}, name;
    es2_reader r;
    size_t length = 0;
    TRY(typed(table, ES2_DAT_SHELL_WEAPONS, out, error));
    TRY(es2_dat_record_at(table, index, &record, error));
    TRY(es2_reader_init(&r, record, error));
    TRY(es2_read_i16(&r, &v.weapon_id)); TRY(count16(&r, &length));
    TRY(es2_read_span(&r, length, &name));
    TRY(slot_text(name, &v.name, error, 4, 1));
    TRY(es2_read_u16(&r, &v.word_after_name)); TRY(es2_read_u8(&r, &v.byte_after_word));
    TRY(es2_read_u16(&r, &v.final_word));
    *out = v; return ES2_OK;
}

es2_status es2_dat_shell_item_at(const es2_dat *table, size_t index,
                                 es2_dat_item *out, es2_error *error)
{
    es2_dat_item v;
    es2_span record = {0};
    es2_reader r;
    TRY(typed(table, ES2_DAT_SHELL_WEAPONS, out, error));
    if (index >= table->secondary_count || index > table->secondary.size / 6)
        return es2_fail(error, ES2_ERR_OFFSET, index);
    TRY(es2_subspan(table->secondary, index * 6, 6, &record, error));
    TRY(es2_reader_init(&r, record, error));
    TRY(es2_read_i16(&r, &v.item_id)); TRY(es2_read_u16(&r, &v.word_0x02));
    TRY(es2_read_u16(&r, &v.word_0x04));
    *out = v; return ES2_OK;
}

es2_status es2_dat_damage_get(const es2_dat *table, es2_dat_damage *out,
                              es2_error *error)
{
    es2_dat_damage v;
    es2_reader r;
    size_t i;
    TRY(typed(table, ES2_DAT_DAMAGE, out, error));
    TRY(es2_reader_init(&r, table->input, error));
    TRY(es2_read_i16(&r, &v.common_coefficient));
    for (i = 0; i < 6; ++i) TRY(es2_read_i16(&r, &v.coefficients_0x02[i]));
    for (i = 0; i < 9; ++i) TRY(es2_read_i16(&r, &v.coefficients_0x0e[i]));
    TRY(es2_read_i16(&r, &v.second_common_coefficient)); TRY(es2_skip(&r, 2));
    for (i = 0; i < 33; ++i) TRY(es2_read_i16(&r, &v.item_coefficients[i]));
    *out = v; return ES2_OK;
}

es2_status es2_dat_zone_get(const es2_dat *table, es2_dat_zone *out,
                            es2_error *error)
{
    es2_dat_zone v;
    es2_reader r;
    TRY(typed(table, ES2_DAT_ZONE, out, error));
    TRY(es2_reader_init(&r, table->input, error));
    TRY(es2_read_u32(&r, &v.dword_0x00)); TRY(es2_read_u32(&r, &v.dword_0x04));
    TRY(es2_read_i32(&r, &v.dword_0x08)); TRY(es2_read_u32(&r, &v.dword_0x0c));
    *out = v; return ES2_OK;
}

es2_status es2_dat_debris_group_at(const es2_dat *table, size_t index,
                                   es2_dat_group *out, es2_error *error)
{
    es2_dat_group v;
    es2_span record = {0};
    es2_reader r;
    TRY(typed(table, ES2_DAT_DEBRIS, out, error));
    TRY(es2_dat_record_at(table, index, &record, error));
    TRY(es2_reader_init(&r, record, error));
    TRY(es2_read_u16(&r, &v.id)); TRY(count16(&r, &v.count));
    TRY(elements(&r, v.count, 14, &v.children));
    *out = v; return ES2_OK;
}

es2_status es2_dat_debris_child_at(const es2_dat_group *group, size_t index,
                                   es2_dat_debris_child *out, es2_error *error)
{
    es2_dat_debris_child v;
    es2_span record = {0};
    es2_reader r;
    if (group == NULL || out == NULL) return es2_fail(error, ES2_ERR_ARGUMENT, index);
    if (index >= group->count || index > group->children.size / 14)
        return es2_fail(error, ES2_ERR_OFFSET, index);
    TRY(es2_subspan(group->children, index * 14, 14, &record, error));
    TRY(es2_reader_init(&r, record, error));
    TRY(es2_read_u16(&r, &v.word_0x00)); TRY(es2_read_u16(&r, &v.word_0x02));
    TRY(es2_read_u16(&r, &v.word_0x04)); TRY(es2_read_u16(&r, &v.word_0x06));
    TRY(es2_read_i16(&r, &v.word_0x08)); TRY(es2_read_i16(&r, &v.word_0x0a));
    TRY(es2_read_u16(&r, &v.word_0x0c));
    *out = v; return ES2_OK;
}

es2_status es2_dat_projectile_at(const es2_dat *table, size_t index,
                                 es2_dat_projectile *out, es2_error *error)
{
    es2_dat_projectile v;
    es2_span record = {0};
    es2_reader r;
    TRY(typed(table, ES2_DAT_PROJ, out, error));
    TRY(es2_dat_record_at(table, index, &record, error));
    TRY(es2_reader_init(&r, record, error));
    TRY(es2_read_i16(&r, &v.selector_0x00)); TRY(es2_read_i16(&r, &v.selector_0x02));
    TRY(es2_read_span(&r, 32, &v.parameters_0x04));
    *out = v; return ES2_OK;
}

es2_status es2_dat_career_group_at(const es2_dat *table, size_t index,
                                   es2_dat_career_group *out, es2_error *error)
{
    es2_dat_career_group v;
    es2_span record = {0};
    es2_reader r;
    TRY(typed(table, ES2_DAT_CAREER, out, error));
    TRY(es2_dat_record_at(table, index, &record, error));
    TRY(es2_reader_init(&r, record, error));
    TRY(es2_read_i16(&r, &v.id)); TRY(count16(&r, &v.count));
    TRY(elements(&r, v.count, 2, &v.string_indices));
    *out = v; return ES2_OK;
}

es2_status es2_dat_career_index_at(const es2_dat_career_group *group, size_t index,
                                   int16_t *out, es2_error *error)
{
    if (group == NULL || out == NULL) return es2_fail(error, ES2_ERR_ARGUMENT, index);
    if (index >= group->count || index > group->string_indices.size / 2)
        return es2_fail(error, ES2_ERR_OFFSET, index);
    return signed_word(group->string_indices, index * 2, out, error);
}
