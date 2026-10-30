#include "es2/formats/dat.h"
#include "test_support.h"

#include <string.h>

typedef struct dat_fixture {
    uint8_t bytes[2048];
    size_t size, external;
    size_t counts[32], count_widths[32], count_count;
} dat_fixture;

static void blank(dat_fixture *f, size_t bytes) { f->size += bytes; }
static void word(dat_fixture *f, uint16_t value)
{ test_put16(f->bytes + f->size, value); f->size += 2; }
static void count(dat_fixture *f, size_t value, size_t width)
{
    f->counts[f->count_count] = f->size;
    f->count_widths[f->count_count++] = width;
    if (width == 4) { test_put32(f->bytes + f->size, (uint32_t)value); f->size += 4; }
    else if (width == 1) f->bytes[f->size++] = (uint8_t)value;
    else word(f, (uint16_t)value);
}
static void group(dat_fixture *f, size_t children, size_t stride)
{ word(f, 17); count(f, children, 2); blank(f, children * stride); }
static void loadout(dat_fixture *f)
{
    word(f, 3); word(f, 87); word(f, 4); count(f, 2, 2);
    word(f, 0); word(f, 4); word(f, 88); word(f, 2);
    word(f, 7); word(f, 9); word(f, 65); word(f, 1);
}

/* Every fixture is invented here, including counts and values. Never reads CD. */
static void fixture(es2_dat_kind kind, dat_fixture *f)
{
    size_t i, stride;
    memset(f, 0, sizeof(*f));
    switch (kind) {
    case ES2_DAT_VEHICLE:
        blank(f, 216); f->bytes[0x56] = 'V'; f->bytes[0x57] = 0;
        memset(f->bytes + 0x58, 0xff, 10); /* Unused slot bytes are not padding. */
        test_put16(f->bytes + 2, 0x8000); test_put16(f->bytes + 4, 0x7fff);
        test_put16(f->bytes + 0xc2, 0); /* Parser does not run arithmetic. */
        test_put16(f->bytes + 0xc4, 43); break;
    case ES2_DAT_SKIMMER:
        blank(f, 46); f->bytes[18] = 'S'; f->bytes[19] = 0; break;
    case ES2_DAT_COLORS: blank(f, 54); break;
    case ES2_DAT_ZONE:
        blank(f, 16); test_put32(f->bytes, UINT32_MAX);
        test_put32(f->bytes + 8, UINT32_C(0x80000000)); break;
    case ES2_DAT_DAMAGE:
        blank(f, 34); count(f, 33, 2); blank(f, 66);
        test_put16(f->bytes, 0x8000); test_put16(f->bytes + 100, 0xffff); break;
    case ES2_DAT_FIRE:
        blank(f, 9); f->external = 5; break;
    case ES2_DAT_MAT0:
        count(f, 1, 4); blank(f, 8); blank(f, 7); break;
    case ES2_DAT_LOADOUT: case ES2_DAT_TRN_HERC:
        loadout(f); break;
    case ES2_DAT_HERCS:
        count(f, 1, 2); word(f, 11); loadout(f); break;
    case ES2_DAT_SIM_WEAPONS:
        count(f, 1, 2); blank(f, 6); count(f, 2, 2); blank(f, 8);
        word(f, 13); count(f, 3, 2); blank(f, 24); blank(f, 48);
        test_put16(f->bytes + 46 + 0x16, 0xffff);
        test_put16(f->bytes + 46 + 0x2a, 7); break;
    case ES2_DAT_SHELL_WEAPONS:
        count(f, 1, 2); word(f, 4); count(f, 3, 2);
        f->bytes[f->size++] = 'a'; f->bytes[f->size++] = 'b'; f->bytes[f->size++] = 0;
        word(f, 0xffff); f->bytes[f->size++] = 9; word(f, 77);
        count(f, 1, 2); word(f, 6); word(f, 55); word(f, 22); break;
    case ES2_DAT_DEBRIS: case ES2_DAT_CAREER:
        count(f, 2, 2); group(f, 2, kind == ES2_DAT_DEBRIS ? 14 : 2);
        group(f, 0, kind == ES2_DAT_DEBRIS ? 14 : 2);
        if (kind == ES2_DAT_DEBRIS) test_put16(f->bytes + 6 + 8, 0xffff);
        else test_put16(f->bytes + 6, 0x8000);
        break;
    case ES2_DAT_ARM:
        blank(f, 44); count(f, 2, 2); group(f, 2, 22); group(f, 0, 22); break;
    case ES2_DAT_RPR:
        count(f, 2, 2); blank(f, 42); count(f, 2, 2); group(f, 2, 14); group(f, 0, 14); break;
    case ES2_DAT_HOTSPOTS:
        count(f, 9, 2);
        for (i = 0; i < 9; ++i) group(f, 1, 16);
        break;
    case ES2_DAT_BFORMS:
        count(f, 1, 4); count(f, 2, 4); blank(f, 20); blank(f, 12);
        count(f, 2, 4); count(f, 3, 1); blank(f, 3); count(f, 0, 1); break;
    case ES2_DAT_BASES:
        count(f, 1, 2); blank(f, 18); count(f, 2, 2); blank(f, 60); blank(f, 28); break;
    case ES2_DAT_BASECOL:
        f->external = 2;
        count(f, 1, 2); word(f, 3); count(f, 2, 2);
        word(f, 1); count(f, 2, 2); blank(f, 16);
        word(f, 9); count(f, 0, 2); count(f, 0, 2); break;
    case ES2_DAT_SHADES:
        f->bytes[f->size++] = 7; f->bytes[f->size++] = 2;
        f->bytes[f->size++] = 4; f->bytes[f->size++] = 3; blank(f, 6);
        f->bytes[f->size++] = 2; f->bytes[f->size++] = 0; break;
    case ES2_DAT_MAP_INFO: case ES2_DAT_MAP_LOCS:
        group(f, 2, kind == ES2_DAT_MAP_INFO ? 4 : 34); break;
    case ES2_DAT_ARM_WEAP: case ES2_DAT_EXPLOS:
        stride = kind == ES2_DAT_ARM_WEAP ? 12 : 4;
        count(f, 2, 2); blank(f, 2 * stride); count(f, 1, 2);
        blank(f, kind == ES2_DAT_ARM_WEAP ? 12 : 40); break;
    default:
        stride = kind == ES2_DAT_PROJ ? 36 :
            kind == ES2_DAT_ROCKETS || kind == ES2_DAT_BULLETS || kind == ES2_DAT_THEATER ? 14 :
            kind == ES2_DAT_BEAM ? 6 : kind == ES2_DAT_LC_WPNS ? 22 :
            kind == ES2_DAT_HERC_INF ? 16 : kind == ES2_DAT_MFORMS ? 28 :
            kind == ES2_DAT_FFORMS ? 18 : kind == ES2_DAT_STYLES ? 64 : 4;
        count(f, 2, kind == ES2_DAT_TEX ? 4 : 2); blank(f, 2 * stride);
        if (kind == ES2_DAT_PROJ || kind == ES2_DAT_HERC_INF) test_put16(f->bytes + 2, 0x8000);
        break;
    }
}

static es2_status parse(es2_dat_kind kind, dat_fixture *f, es2_dat *table,
                         es2_error *error)
{
    es2_span input = {f->bytes, f->size};
    return es2_dat_parse(kind, input, f->external, table, error);
}

static void framing_tests(void)
{
    es2_dat_kind kind;
    dat_fixture f;
    es2_dat table, saved;
    es2_span bytes;
    es2_error error;
    size_t i, j, length;
    for (kind = ES2_DAT_SIM_WEAPONS; kind < ES2_DAT_KIND_COUNT; ++kind) {
        fixture(kind, &f);
        CHECK(parse(kind, &f, &table, &error) == ES2_OK);
        CHECK(table.kind == kind && table.input.data == f.bytes && table.input.size == f.size);
        CHECK(es2_dat_kind_name(kind) != NULL);
        for (i = 0; i < table.count; ++i) {
            CHECK(es2_dat_record_at(&table, i, &bytes, &error) == ES2_OK);
            CHECK(bytes.data >= f.bytes && bytes.size <= f.size);
        }
        CHECK(es2_dat_record_at(&table, table.count, &bytes, &error) == ES2_ERR_OFFSET);
        saved = table;
        length = f.size;
        for (i = 0; i < length; ++i) {
            es2_status st;
            int may_succeed = (kind == ES2_DAT_MAT0 && i >= 12) ||
                (i == 0 && (kind == ES2_DAT_TRN_HERC || kind == ES2_DAT_MAP_INFO || kind == ES2_DAT_MAP_LOCS));
            f.size = i;
            st = parse(kind, &f, &table, &error);
            CHECK(may_succeed ? st == ES2_OK : st != ES2_OK);
            if (st != ES2_OK) {
                CHECK(error.code == st && error.offset <= f.size);
                CHECK(memcmp(&table, &saved, sizeof(table)) == 0);
            } else table = saved;
        }
        f.size = length + 1;
        if (kind == ES2_DAT_MAT0) CHECK(parse(kind, &f, &table, &error) == ES2_OK && table.suffix.size == 8);
        else CHECK(parse(kind, &f, &table, &error) != ES2_OK);
        f.size = length;
        for (j = 0; j < f.count_count; ++j) {
            uint8_t old[4];
            size_t width = f.count_widths[j];
            memcpy(old, f.bytes + f.counts[j], width);
            memset(f.bytes + f.counts[j], 0xff, width);
            CHECK(parse(kind, &f, &table, &error) != ES2_OK);
            memcpy(f.bytes + f.counts[j], old, width);
        }
        CHECK(parse(kind, &f, &table, NULL) == ES2_OK);
        CHECK(es2_dat_record_at(&table, 0, NULL, &error) == ES2_ERR_ARGUMENT);
        CHECK(es2_dat_parse(kind, table.input, f.external, NULL, &error) == ES2_ERR_ARGUMENT);
    }
    bytes.data = NULL; bytes.size = 1;
    CHECK(es2_dat_parse(ES2_DAT_ZONE, bytes, 0, &table, &error) == ES2_ERR_ARGUMENT);
    bytes.size = 0;
    CHECK(es2_dat_parse(ES2_DAT_UNKNOWN, bytes, 0, &table, &error) == ES2_ERR_UNSUPPORTED);
    CHECK(es2_dat_parse(ES2_DAT_KIND_COUNT, bytes, 0, &table, &error) == ES2_ERR_UNSUPPORTED);
    CHECK(es2_dat_kind_name(ES2_DAT_KIND_COUNT) == NULL);
    CHECK(es2_dat_record_at(NULL, 0, &bytes, &error) == ES2_ERR_ARGUMENT);
}

static void typed_tests(void)
{
    dat_fixture f;
    es2_dat table;
    es2_error error;
    es2_dat_vehicle vehicle;
    es2_dat_skimmer skimmer;
    es2_dat_sim_weapon sim;
    es2_dat_herc_info herc;
    es2_dat_loadout load;
    es2_dat_loadout_slot slot;
    es2_dat_shell_weapon shell;
    es2_dat_item item;
    es2_dat_damage damage;
    es2_dat_zone zone;
    es2_dat_group group_value;
    es2_dat_debris_child debris;
    es2_dat_projectile projectile;
    es2_dat_career_group career;
    int16_t index;

    fixture(ES2_DAT_VEHICLE, &f); CHECK(parse(ES2_DAT_VEHICLE, &f, &table, &error) == ES2_OK);
    CHECK(es2_dat_vehicle_get(&table, &vehicle, &error) == ES2_OK);
    CHECK(vehicle.reverse_speed_parameter == INT16_MIN && vehicle.forward_speed_parameter == INT16_MAX);
    CHECK(vehicle.rescale_denominator == 0 && vehicle.rescale_numerator == 43);
    CHECK(vehicle.label.size == 1 && vehicle.debris_resource.size == 0 && vehicle.bytes_0x2e.size == 20);
    CHECK(es2_dat_vehicle_get(&table, NULL, &error) == ES2_ERR_ARGUMENT);
    memset(f.bytes + 0x56, 'x', 12); CHECK(parse(ES2_DAT_VEHICLE, &f, &table, &error) == ES2_ERR_STRING);
    f.bytes[0x56] = 0xff; CHECK(parse(ES2_DAT_VEHICLE, &f, &table, &error) == ES2_ERR_STRING);
    fixture(ES2_DAT_VEHICLE, &f); memset(f.bytes + 0xcc, 'x', 12);
    CHECK(parse(ES2_DAT_VEHICLE, &f, &table, &error) == ES2_ERR_STRING);

    fixture(ES2_DAT_SKIMMER, &f); CHECK(parse(ES2_DAT_SKIMMER, &f, &table, &error) == ES2_OK);
    CHECK(es2_dat_skimmer_get(&table, &skimmer, &error) == ES2_OK && skimmer.label.size == 1);
    CHECK(skimmer.parameters_0x00.size == 18 && skimmer.parameters_0x1e.size == 16);
    CHECK(es2_dat_vehicle_get(&table, &vehicle, &error) == ES2_ERR_ARGUMENT);

    fixture(ES2_DAT_SIM_WEAPONS, &f); CHECK(parse(ES2_DAT_SIM_WEAPONS, &f, &table, &error) == ES2_OK);
    CHECK(table.child_count == 2 && table.byte_list_count == 3);
    CHECK(es2_dat_sim_weapon_at(&table, 0, &sim, &error) == ES2_OK);
    CHECK(sim.count_a == 2 && sim.count_b == 3 && sim.selector == 13 && sim.tail.size == 48);
    CHECK(sim.discharge_amount == -1 && sim.cooldown_seed == 7 && sim.bytes_0x08.size == 6);
    CHECK(es2_dat_sim_weapon_at(&table, 1, &sim, &error) == ES2_ERR_OFFSET);
    fixture(ES2_DAT_HERC_INF, &f); CHECK(parse(ES2_DAT_HERC_INF, &f, &table, &error) == ES2_OK);
    CHECK(es2_dat_herc_info_at(&table, 0, &herc, &error) == ES2_OK && herc.herc_id == INT16_MIN);

    fixture(ES2_DAT_LOADOUT, &f); CHECK(parse(ES2_DAT_LOADOUT, &f, &table, &error) == ES2_OK);
    CHECK(es2_dat_loadout_at(&table, 0, &load, &error) == ES2_OK);
    CHECK(load.herc_id == 3 && load.word_0x02 == 87 && load.slot_count == 2 && !load.has_bay_slot);
    CHECK(es2_dat_loadout_slot_at(&load, 1, &slot, &error) == ES2_OK && slot.slot_id == 7 && slot.item_id == 9);
    CHECK(es2_dat_loadout_slot_at(&load, 2, &slot, &error) == ES2_ERR_OFFSET);
    CHECK(es2_dat_loadout_slot_at(NULL, 0, &slot, &error) == ES2_ERR_ARGUMENT);
    fixture(ES2_DAT_HERCS, &f); CHECK(parse(ES2_DAT_HERCS, &f, &table, &error) == ES2_OK);
    CHECK(es2_dat_loadout_at(&table, 0, &load, &error) == ES2_OK && load.has_bay_slot && load.bay_slot == 11);
    fixture(ES2_DAT_TRN_HERC, &f); CHECK(parse(ES2_DAT_TRN_HERC, &f, &table, &error) == ES2_OK);
    CHECK(table.support == ES2_DAT_DATA_FIT_ONLY && table.count == 1);
    CHECK(es2_dat_loadout_at(&table, 0, &load, &error) == ES2_OK && !load.has_bay_slot);

    fixture(ES2_DAT_SHELL_WEAPONS, &f); CHECK(parse(ES2_DAT_SHELL_WEAPONS, &f, &table, &error) == ES2_OK);
    CHECK(es2_dat_shell_weapon_at(&table, 0, &shell, &error) == ES2_OK);
    CHECK(shell.weapon_id == 4 && shell.name.size == 2 && shell.word_after_name == UINT16_MAX && shell.final_word == 77);
    CHECK(es2_dat_shell_item_at(&table, 0, &item, &error) == ES2_OK && item.item_id == 6 && item.word_0x02 == 55);
    CHECK(es2_dat_shell_item_at(&table, 1, &item, &error) == ES2_ERR_OFFSET);
    f.bytes[6] = 0; CHECK(parse(ES2_DAT_SHELL_WEAPONS, &f, &table, &error) == ES2_ERR_STRING);
    f.bytes[6] = 'a'; f.bytes[8] = 'x'; CHECK(parse(ES2_DAT_SHELL_WEAPONS, &f, &table, &error) == ES2_ERR_STRING);
    f.bytes[8] = 0; test_put16(f.bytes + 4, 0); CHECK(parse(ES2_DAT_SHELL_WEAPONS, &f, &table, &error) == ES2_ERR_COUNT);

    fixture(ES2_DAT_DAMAGE, &f); CHECK(parse(ES2_DAT_DAMAGE, &f, &table, &error) == ES2_OK);
    CHECK(es2_dat_damage_get(&table, &damage, &error) == ES2_OK);
    CHECK(damage.common_coefficient == INT16_MIN && damage.item_coefficients[32] == -1);
    test_put16(f.bytes + 34, 32); CHECK(parse(ES2_DAT_DAMAGE, &f, &table, &error) == ES2_ERR_COUNT);
    fixture(ES2_DAT_ZONE, &f); CHECK(parse(ES2_DAT_ZONE, &f, &table, &error) == ES2_OK);
    CHECK(es2_dat_zone_get(&table, &zone, &error) == ES2_OK);
    CHECK(zone.dword_0x00 == UINT32_MAX && zone.dword_0x08 == INT32_MIN);
    CHECK(es2_dat_damage_get(&table, &damage, &error) == ES2_ERR_ARGUMENT);
    CHECK(es2_dat_loadout_at(&table, 0, &load, &error) == ES2_ERR_ARGUMENT);

    fixture(ES2_DAT_DEBRIS, &f); CHECK(parse(ES2_DAT_DEBRIS, &f, &table, &error) == ES2_OK);
    CHECK(es2_dat_debris_group_at(&table, 0, &group_value, &error) == ES2_OK && group_value.count == 2);
    CHECK(es2_dat_debris_child_at(&group_value, 0, &debris, &error) == ES2_OK && debris.word_0x08 == -1);
    CHECK(es2_dat_debris_child_at(&group_value, 2, &debris, &error) == ES2_ERR_OFFSET);
    CHECK(es2_dat_debris_child_at(NULL, 0, &debris, &error) == ES2_ERR_ARGUMENT);
    fixture(ES2_DAT_PROJ, &f); CHECK(parse(ES2_DAT_PROJ, &f, &table, &error) == ES2_OK);
    CHECK(es2_dat_projectile_at(&table, 0, &projectile, &error) == ES2_OK);
    CHECK(projectile.selector_0x00 == INT16_MIN && projectile.parameters_0x04.size == 32);
    fixture(ES2_DAT_CAREER, &f); CHECK(parse(ES2_DAT_CAREER, &f, &table, &error) == ES2_OK);
    CHECK(es2_dat_career_group_at(&table, 0, &career, &error) == ES2_OK && career.id == 17 && career.count == 2);
    CHECK(es2_dat_career_index_at(&career, 0, &index, &error) == ES2_OK && index == INT16_MIN);
    CHECK(es2_dat_career_index_at(&career, 2, &index, &error) == ES2_ERR_OFFSET);
    CHECK(es2_dat_career_index_at(NULL, 0, &index, &error) == ES2_ERR_ARGUMENT);
}

static void special_tests(void)
{
    dat_fixture f;
    es2_dat table;
    es2_error error;
    fixture(ES2_DAT_BASECOL, &f); CHECK(parse(ES2_DAT_BASECOL, &f, &table, &error) == ES2_OK);
    CHECK(table.count == 2 && table.nested_group_count == 1 && table.byte_list_count == 2 && table.child_count == 2);
    f.external = 1; CHECK(parse(ES2_DAT_BASECOL, &f, &table, &error) == ES2_ERR_LENGTH);
    f.external = SIZE_MAX; CHECK(parse(ES2_DAT_BASECOL, &f, &table, &error) == ES2_ERR_TRUNCATED);
    fixture(ES2_DAT_FIRE, &f); f.external = 4;
    CHECK(parse(ES2_DAT_FIRE, &f, &table, &error) == ES2_ERR_LENGTH);
    f.external = 6; CHECK(parse(ES2_DAT_FIRE, &f, &table, &error) == ES2_ERR_TRUNCATED);
    f.external = SIZE_MAX; CHECK(parse(ES2_DAT_FIRE, &f, &table, &error) == ES2_ERR_TRUNCATED);
    fixture(ES2_DAT_BFORMS, &f); CHECK(parse(ES2_DAT_BFORMS, &f, &table, &error) == ES2_OK);
    CHECK(table.count == 1 && table.child_count == 2 && table.byte_list_count == 2);
    fixture(ES2_DAT_RPR, &f); CHECK(parse(ES2_DAT_RPR, &f, &table, &error) == ES2_OK);
    CHECK(table.secondary_count == 3 && table.secondary.size == 42 && table.child_count == 2);
    fixture(ES2_DAT_HOTSPOTS, &f); test_put16(f.bytes, 8);
    CHECK(parse(ES2_DAT_HOTSPOTS, &f, &table, &error) == ES2_ERR_COUNT);
    fixture(ES2_DAT_MAT0, &f); CHECK(parse(ES2_DAT_MAT0, &f, &table, &error) == ES2_OK);
    CHECK(table.count == 1 && table.records.size == 8 && table.suffix.size == 7 && table.suffix.data == f.bytes + 12);
    f.size = 12; CHECK(parse(ES2_DAT_MAT0, &f, &table, &error) == ES2_OK && table.suffix.size == 0);
    fixture(ES2_DAT_SHADES, &f); CHECK(parse(ES2_DAT_SHADES, &f, &table, &error) == ES2_OK && table.child_count == 3);
    f.bytes[1] = 255; CHECK(parse(ES2_DAT_SHADES, &f, &table, &error) == ES2_ERR_TRUNCATED);
    fixture(ES2_DAT_SHADES, &f); f.bytes[3] = 255;
    CHECK(parse(ES2_DAT_SHADES, &f, &table, &error) == ES2_ERR_TRUNCATED);
}

static void path_tests(void)
{
    static const struct { const char *path; es2_dat_kind kind; } cases[] = {
        {"DAT/WEAPONS.DAT", ES2_DAT_SIM_WEAPONS}, {"gam\\weapons.dat", ES2_DAT_SHELL_WEAPONS},
        {"WEAPONS.DAT", ES2_DAT_UNKNOWN}, {"wrong/weapons.dat", ES2_DAT_UNKNOWN},
        {"dat/outlaw.dat", ES2_DAT_VEHICLE}, {"dat/nope.dat", ES2_DAT_UNKNOWN},
        {"dat/base_deb.dat", ES2_DAT_DEBRIS}, {"dat/mforMS.dat", ES2_DAT_MFORMS},
        {"DATA/MFORMS.DAT", ES2_DAT_MFORMS}, {"DATA/MAT0.DAT", ES2_DAT_MAT0},
        {"GAM/ARM_APOC.DAT", ES2_DAT_ARM}, {"gam/rpr_sams.dat", ES2_DAT_RPR},
        {"gam/ini_outl.dat", ES2_DAT_LOADOUT}, {"gam/arm_bogus.dat", ES2_DAT_UNKNOWN},
        {"dat/zone1.dat", ES2_DAT_ZONE}, {"DAT/ZONE12345.DAT", ES2_DAT_ZONE},
        {"dat/zone.dat", ES2_DAT_UNKNOWN}, {"dat/zone12a.dat", ES2_DAT_UNKNOWN},
        {"dat/zone1.dat/extra", ES2_DAT_UNKNOWN}, {"", ES2_DAT_UNKNOWN},
        {"d", ES2_DAT_UNKNOWN}, {"dat/", ES2_DAT_UNKNOWN}, {NULL, ES2_DAT_UNKNOWN}
    };
    size_t i;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
        CHECK(es2_dat_classify(cases[i].path) == cases[i].kind);
}

int test_dat(void)
{
    framing_tests(); typed_tests(); special_tests(); path_tests();
    return 0;
}
