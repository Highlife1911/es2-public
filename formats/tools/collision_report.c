#include "collision_report.h"
#include "es2/formats/collision.h"
#include "es2/formats/bnd.h"
#include "es2/formats/dts.h"
#include "es2/formats/dat.h"
#include "es2/formats/dbm.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

enum { COL, DMG, GL, PDG, BND, TYPE_COUNT };
static const char *const type_names[] = {"COL","DMG","GL","PDG","BND"};
typedef struct totals {
    uint64_t files, bytes, ok, opaque, failed, a, b, c, d, references, omitted, zeros;
} totals;
typedef struct part_set { uint8_t bits[8192]; size_t cells; } part_set;

static int ascii_equal(const char *a, const char *b)
{
    for (; *a && *b; ++a, ++b) {
        int x = (unsigned char)*a, y = (unsigned char)*b;
        if (x == '\\') x = '/';
        if (y == '\\') y = '/';
        if (x >= 'a' && x <= 'z') x -= 'a' - 'A';
        if (y >= 'a' && y <= 'z') y -= 'a' - 'A';
        if (x != y) return 0;
    }
    return *a == *b;
}

static es2_status collect_part(const es2_dts_object *object, void *user)
{
    part_set *set = (part_set *)user;
    es2_reader r;
    uint16_t id;
    if (object->root_index != 0) return ES2_OK;
    if (object->depth == 0 && (object->tag == 0x00140008 || object->tag == 0x001e0003)) {
        if (es2_reader_init(&r, object->body, NULL) != ES2_OK ||
            es2_skip(&r, 14) != ES2_OK ||
            es2_skip(&r, object->children[0].encoded.size) != ES2_OK ||
            es2_skip(&r, 2) != ES2_OK || es2_read_u16(&r, &id) != ES2_OK)
            return ES2_ERR_TRUNCATED;
        set->cells = id;
    }
    switch (object->tag) {
    case 0x00140005: case 0x00140007: case 0x00140008:
    case 0x0014000a: case 0x0014000b: case 0x0014000c:
    case 0x00140013: case 0x00140014: case 0x00140015: case 0x001e0003:
        if (es2_reader_init(&r, object->body, NULL) != ES2_OK ||
            es2_skip(&r, 2) != ES2_OK || es2_read_u16(&r, &id) != ES2_OK)
            return ES2_ERR_TRUNCATED;
        set->bits[id / 8] |= (uint8_t)(1u << (id % 8));
        break;
    default: break;
    }
    return ES2_OK;
}

static int has_part(const part_set *set, int16_t id)
{
    return id < 0 || (set->bits[(uint16_t)id / 8] & (1u << ((uint16_t)id % 8))) != 0;
}

#define TRY(op) do { es2_status status_ = (op); if (status_ != ES2_OK) return status_; } while (0)
static es2_status companion(const es2_vol *base, const char *stem, const char *ext,
                            es2_span *out, es2_error *error)
{
    char path[64];
    es2_vol_member m;
    int length = snprintf(path, sizeof path, "%s/%s.%s", ext, stem, ext);
    if (length < 0 || (size_t)length >= sizeof path)
        return es2_fail(error, ES2_ERR_LENGTH, 0);
    if (base == NULL) return es2_fail(error, ES2_ERR_NOT_FOUND, 0);
    TRY(es2_vol_find(base, path, &m, error));
    *out = m.payload;
    return ES2_OK;
}

static es2_status parts(const es2_vol *base, const char *stem, part_set *set, es2_error *error)
{
    es2_span bytes;
    es2_dts_stats stats;
    memset(set, 0, sizeof *set);
    TRY(companion(base, stem, "dts", &bytes, error));
    return es2_dts_walk(bytes, 64, collect_part, set, &stats, error);
}

static es2_status inspect(int type, es2_span bytes, const char *stem,
                          const es2_vol *base, totals *t, es2_error *error)
{
    size_t i, j;
    es2_span other;
    part_set set;
    es2_dmg damage;
    if (type == COL) {
        es2_col c;
        TRY(es2_col_parse(bytes, &c, error));
        t->a = c.group_count; t->b = c.shape_count; t->c = c.sphere_count;
        TRY(companion(base, stem, "dmg", &other, error));
        TRY(es2_dmg_parse(other, &damage, error));
        TRY(es2_col_validate_regions(&c, damage.region_count, error));
        TRY(parts(base, stem, &set, error));
        for (i = 0; i < c.group_count; ++i) {
            es2_col_group g;
            TRY(es2_col_group_at(&c, i, &g, error));
            if (!has_part(&set, g.part_id)) return es2_fail(error, ES2_ERR_OFFSET, g.offset);
            /* Exercise every typed sphere accessor on the real corpus. */
            for (j = 0; j < g.shape_count; ++j) {
                es2_col_shape s;
                size_t k;
                TRY(es2_col_shape_at(&g, j, &s, error));
                if (s.sphere_count == 0) ++t->zeros;
                for (k = 0; k < s.sphere_count; ++k) {
                    es2_col_sphere sphere;
                    TRY(es2_col_sphere_at(&s, k, &sphere, error));
                }
            }
        }
        t->references = c.group_count + c.shape_count;
    } else if (type == DMG) {
        es2_dat data;
        size_t debris_count;
        TRY(es2_dmg_parse(bytes, &damage, error));
        t->a = damage.capacity_count; t->b = damage.region_count; t->c = damage.link_count;
        TRY(parts(base, stem, &set, error));
        TRY(companion(base, "def_deb", "dat", &other, error));
        TRY(es2_dat_parse(ES2_DAT_DEBRIS, other, 0, &data, error));
        debris_count = data.count;
        TRY(companion(base, stem, "dat", &other, error));
        if (!ascii_equal(stem, "skimmer")) {
            es2_dat_vehicle vehicle;
            TRY(es2_dat_parse(ES2_DAT_VEHICLE, other, 0, &data, error));
            TRY(es2_dat_vehicle_get(&data, &vehicle, error));
            if (vehicle.debris_resource.size != 0) {
                char resource[13];
                if (vehicle.debris_resource.size >= sizeof resource) return ES2_ERR_LENGTH;
                memcpy(resource, vehicle.debris_resource.data, vehicle.debris_resource.size);
                resource[vehicle.debris_resource.size] = 0;
                TRY(companion(base, resource, "dat", &other, error));
                TRY(es2_dat_parse(ES2_DAT_DEBRIS, other, 0, &data, error));
                debris_count += data.count;
            }
        }
        TRY(es2_dmg_validate_references(&damage, set.cells, debris_count, error));
        t->references = damage.link_count + 3*damage.region_count;
    } else if (type == GL) {
        es2_gl g;
        es2_pdg display;
        TRY(es2_gl_parse(bytes, &g, error)); t->a = g.count;
        TRY(companion(base, stem, "dmg", &other, error));
        TRY(es2_dmg_parse(other, &damage, error));
        TRY(es2_gl_validate_regions(&g, damage.region_count, error));
        TRY(es2_gl_validate_capacities(&g, damage.capacity_count, error));
        TRY(companion(base, stem, "pdg", &other, error));
        TRY(es2_pdg_parse(ES2_PDG_VEHICLE, other, &display, error));
        TRY(parts(base, stem, &set, error));
        for (i = 0; i < g.count; ++i) {
            es2_gl_slot s;
            TRY(es2_gl_slot_at(&g, i, &s, error));
            if (!has_part(&set, s.part_id)) return es2_fail(error, ES2_ERR_OFFSET, s.offset);
            if (s.display_slot >= 0 && (size_t)s.display_slot >= display.slot_count) {
                ++t->omitted;
                printf("collision-exception type=GL member=%s.gl offset=%zu kind=slot-not-displayed-by-PDG\n",
                       stem, s.offset + 23);
            }
        }
        for (i = 0; i < display.slot_count; ++i) {
            size_t index;
            TRY(es2_gl_find_display_slot(&g, i, &index, error));
        }
        t->references = 3*g.count + display.slot_count;
    } else if (type == PDG) {
        es2_pdg p;
        es2_pdg_kind kind = ascii_equal(stem, "weapons") ? ES2_PDG_WEAPONS : ES2_PDG_VEHICLE;
        TRY(es2_pdg_parse(kind, bytes, &p, error));
        if (kind == ES2_PDG_WEAPONS) t->d = p.slot_count;
        else {
            t->a = p.view_count; t->b = p.rectangle_count; t->c = p.slot_count;
            t->references = p.rectangle_count;
        }
        for (i = 0; i < 2; ++i) {
            es2_dba images;
            TRY(companion(base, stem, i == 0 ? "dba" : "hba", &other, error));
            TRY(es2_dba_parse(other, &images, error));
            if ((kind == ES2_PDG_WEAPONS ? p.slot_count : p.view_count) > images.count)
                return es2_fail(error, ES2_ERR_OFFSET, 0);
            ++t->references;
        }
        for (i = 0; i < p.slot_count; ++i) {
            if (kind == ES2_PDG_WEAPONS) {
                int32_t pair[2]; TRY(es2_pdg_weapon_pair_at(&p, i, pair, error));
            } else {
                es2_pdg_slot slot; TRY(es2_pdg_slot_at(&p, i, &slot, error));
            }
        }
    } else {
        es2_bnd b;
        TRY(es2_bnd_inspect(bytes, &b, error));
        t->opaque = 1; /* No recovered grammar, deliberately not t->ok. */
    }
    if (type != BND) t->ok = 1;
    return ES2_OK;
}

static size_t expected(const char *type, const char *field, uint64_t actual, uint64_t wanted)
{
    if (actual == wanted) return 0;
    printf("collision-mismatch type=%s field=%s actual=%" PRIu64 " expected=%" PRIu64 "\n",
           type, field, actual, wanted);
    return 1;
}

size_t es2fmt_collision_report(const es2_vol *const *archives,
                              const char *const *names, size_t count)
{
    const es2_vol *base = NULL, *patch = NULL;
    totals all[TYPE_COUNT] = {{0}};
    size_t a, i, k, failures = 0;
    static const uint64_t file_counts[] = {22,22,21,23,84};
    static const uint64_t byte_counts[] = {7452,7554,3188,19996,4840};
    static const uint64_t record_counts[][4] = {
        {135,389,664,0}, {463,610,415,0}, {121,0,0,0}, {66,573,117,14}, {0,0,0,0}
    };
    static const uint64_t reference_counts[] = {524,2245,480,619,0};
    for (a = 0; a < count; ++a) {
        if (ascii_equal(names[a], "VOL/SIMVOL0.VOL")) base = archives[a];
        if (ascii_equal(names[a], "VOL/SIMPATCH.VOL")) patch = archives[a];
    }
    for (a = 0; a < count; ++a) for (i = 0; i < archives[a]->member_count; ++i) {
        es2_vol_member m;
        es2_error error;
        es2_status status;
        char filename[14], *dot;
        int type = -1;
        totals t = {0}, *sum;
        if (es2_vol_member_at(archives[a], i, &m, &error) != ES2_OK) { ++failures; continue; }
        if (m.name.size >= sizeof filename) { ++failures; continue; }
        memcpy(filename, m.name.data, m.name.size); filename[m.name.size] = 0;
        dot = strrchr(filename, '.');
        if (dot == NULL) continue;
        for (k = 0; k < TYPE_COUNT; ++k) if (ascii_equal(dot + 1, type_names[k])) type = (int)k;
        if (type < 0) continue;
        *dot = 0;
        t.files = 1; t.bytes = m.payload.size;
        status = inspect(type, m.payload, filename, base, &t, &error);
        if (status != ES2_OK) { ++failures; t.failed = 1; }
        printf("collision-member type=%s archive=%s path=%.*s%.*s bytes=%zu result=%s",
               type_names[type], names[a], (int)m.directory.size, (const char *)m.directory.data,
               (int)m.name.size, (const char *)m.name.data, m.payload.size,
               status != ES2_OK ? "failed" : type == BND ? "opaque-only" : "ok");
        if (status != ES2_OK) printf(" error=%s offset=%zu", es2_status_string(status), error.offset);
        else if (type == BND) printf(" exception=reader-unresolved opaque_bytes=%zu", m.payload.size);
        else printf(" accounted_bytes=%zu references=%" PRIu64, m.payload.size, t.references);
        putchar('\n');
        sum = &all[type];
        sum->files += t.files; sum->bytes += t.bytes; sum->ok += t.ok;
        sum->failed += t.failed; sum->opaque += t.opaque;
        sum->a += t.a; sum->b += t.b; sum->c += t.c; sum->d += t.d;
        sum->references += t.references; sum->omitted += t.omitted;
        sum->zeros += t.zeros;
    }
    for (k = 0; k < TYPE_COUNT; ++k) {
        const totals *t = &all[k];
        printf("collision-summary type=%s files=%" PRIu64 " bytes=%" PRIu64
               " ok=%" PRIu64 " opaque=%" PRIu64 " failed=%" PRIu64 " references=%" PRIu64,
               type_names[k], t->files, t->bytes, t->ok, t->opaque, t->failed, t->references);
        if (k == COL) printf(" groups=%" PRIu64 " shapes=%" PRIu64 " spheres=%" PRIu64 " zero_sphere_shapes=%" PRIu64, t->a,t->b,t->c,t->zeros);
        if (k == DMG) printf(" capacities=%" PRIu64 " regions=%" PRIu64 " links=%" PRIu64, t->a,t->b,t->c);
        if (k == GL) printf(" slots=%" PRIu64 " undisplayed_slots=%" PRIu64, t->a,t->omitted);
        if (k == PDG) printf(" views=%" PRIu64 " rectangles=%" PRIu64 " slots=%" PRIu64 " weapon_positions=%" PRIu64, t->a,t->b,t->c,t->d);
        if (k == BND) printf(" opaque_bytes=%" PRIu64 " original_reader=unresolved", t->bytes);
        putchar('\n');
        failures += expected(type_names[k], "files", t->files, file_counts[k]);
        failures += expected(type_names[k], "bytes", t->bytes, byte_counts[k]);
        failures += expected(type_names[k], "a", t->a, record_counts[k][0]);
        failures += expected(type_names[k], "b", t->b, record_counts[k][1]);
        failures += expected(type_names[k], "c", t->c, record_counts[k][2]);
        failures += expected(type_names[k], "d", t->d, record_counts[k][3]);
        failures += expected(type_names[k], "references", t->references, reference_counts[k]);
        failures += expected(type_names[k], "ok", t->ok, k == BND ? 0 : file_counts[k]);
        failures += expected(type_names[k], "opaque", t->opaque, k == BND ? 84 : 0);
    }
    failures += expected("GL", "undisplayed_slots", all[GL].omitted, 4);
    failures += expected("COL", "zero_sphere_shapes", all[COL].zeros, 166);
    if (base != NULL && patch != NULL) {
        static const char *const paths[] = {"pdg/spider.pdg","bnd/results.bnd"};
        for (i = 0; i < 2; ++i) {
            es2_vol_member old, newer;
            es2_error error;
            if (es2_vol_find(base, paths[i], &old, &error) != ES2_OK ||
                es2_vol_find(patch, paths[i], &newer, &error) != ES2_OK) { ++failures; continue; }
            printf("collision-precedence path=%s base_priority=%u patch_priority=%u winner=%s actual_request=%s\n",
                   paths[i], (unsigned)base->priority, (unsigned)patch->priority,
                   patch->priority > base->priority ? "SIMPATCH" : "unconfirmed", i == 0 ? "static-loader" : "unconfirmed");
            if (patch->priority <= base->priority) ++failures;
        }
    }
    return failures;
}
