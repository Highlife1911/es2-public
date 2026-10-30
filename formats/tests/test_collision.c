#include "test_support.h"
#include "es2/formats/collision.h"
#include "es2/formats/bnd.h"
#include <string.h>

/* Deliberately invented values and sizes. No real resource is a fixture. */
static void col_fixture(uint8_t b[35])
{
    memset(b, 0, 35);
    test_put16(b, 2);                /* Two groups, second empty. */
    test_put16(b + 2, UINT16_MAX);   /* Actor-relative part. */
    test_put16(b + 4, 2);            /* Two regions. */
    test_put16(b + 6, 1); test_put16(b + 8, 1);
    test_put16(b + 10, (uint16_t)-23); test_put16(b + 12, 47);
    test_put16(b + 14, 82); test_put16(b + 16, 13);
    test_put16(b + 18, 3); test_put16(b + 20, 1);
    test_put16(b + 22, 7); test_put16(b + 24, 9);
    test_put16(b + 26, 11); test_put16(b + 28, 5);
    /* Last group is completed by the caller. */
}

static void check_col(void)
{
    uint8_t b[35], saved[35];
    es2_col c, sentinel = {{NULL,0},777,888,999};
    es2_col_group g;
    es2_col_shape s;
    es2_col_sphere sphere;
    es2_error e;
    size_t i;
    col_fixture(b);
    test_put16(b + 30, 43); test_put16(b + 32, 0); b[34] = 0;
    memcpy(saved, b, sizeof b);
    CHECK(es2_col_parse((es2_span){b,34}, &c, &e) == ES2_OK);
    CHECK(c.group_count == 2 && c.shape_count == 2 && c.sphere_count == 2);
    CHECK(es2_col_group_at(&c, 0, &g, &e) == ES2_OK);
    CHECK(g.offset == 2 && g.part_id == -1 && g.shape_count == 2 && g.shapes.size == 24);
    CHECK(es2_col_shape_at(&g, 1, &s, &e) == ES2_OK);
    CHECK(s.offset == 18 && s.region == 3 && s.sphere_count == 1);
    CHECK(es2_col_shape_at(&g, 0, &s, &e) == ES2_OK);
    CHECK(es2_col_sphere_at(&s, 0, &sphere, &e) == ES2_OK);
    CHECK(sphere.x == -23 && sphere.y == 47 && sphere.z == 82 && sphere.radius == 13);
    CHECK(es2_col_sphere_at(&s, 1, &sphere, &e) == ES2_ERR_OFFSET);
    CHECK(es2_col_shape_at(&g, 2, &s, &e) == ES2_ERR_OFFSET);
    CHECK(es2_col_group_at(&c, 1, &g, &e) == ES2_OK && g.shape_count == 0);
    CHECK(es2_col_group_at(&c, 2, &g, &e) == ES2_ERR_OFFSET);
    CHECK(es2_col_validate_regions(&c, 4, &e) == ES2_OK);
    CHECK(es2_col_validate_regions(&c, 3, &e) == ES2_ERR_OFFSET && e.offset == 18);
    for (i = 0; i < 34; ++i) {
        c = sentinel;
        CHECK(es2_col_parse((es2_span){b,i}, &c, &e) != ES2_OK);
        CHECK(c.group_count == 777 && c.shape_count == 888 && c.sphere_count == 999);
    }
    CHECK(es2_col_parse((es2_span){b,35}, &c, &e) == ES2_ERR_LENGTH);
    for (i = 0; i < 6; ++i) {
        static const size_t counts[] = {0,4,8,20,32,6};
        memcpy(b, saved, sizeof b); test_put16(b + counts[i], UINT16_MAX);
        CHECK(es2_col_parse((es2_span){b,34}, &c, &e) != ES2_OK);
        memcpy(b, saved, sizeof b); test_put16(b + counts[i], INT16_MAX);
        if (i != 5) CHECK(es2_col_parse((es2_span){b,34}, &c, &e) != ES2_OK);
        else {
            CHECK(es2_col_parse((es2_span){b,34}, &c, &e) == ES2_OK);
            CHECK(es2_col_validate_regions(&c, 4, &e) == ES2_ERR_OFFSET);
        }
    }
    memcpy(b, saved, sizeof b); test_put16(b + 8, 4096);
    CHECK(es2_col_parse((es2_span){b,34}, &c, &e) == ES2_ERR_COUNT && e.offset == 8);
    test_put16(b, 0);
    CHECK(es2_col_parse((es2_span){b,2}, &c, &e) == ES2_OK);
    CHECK(es2_col_validate_regions(&c, 0, &e) == ES2_OK);
    CHECK(es2_col_parse((es2_span){NULL,1}, &c, &e) == ES2_ERR_ARGUMENT);
    CHECK(es2_col_parse((es2_span){b,2}, NULL, &e) == ES2_ERR_ARGUMENT);
    memset(b, 0, sizeof b); test_put16(b, 1); test_put16(b + 4, 1);
    CHECK(es2_col_parse((es2_span){b,10}, &c, &e) == ES2_OK && c.shape_count == 1 && c.sphere_count == 0);
    {
        uint8_t limit[10 + 4096*8] = {0};
        test_put16(limit, 1); test_put16(limit + 2, UINT16_MAX);
        test_put16(limit + 4, 1); test_put16(limit + 8, 4095);
        test_put16(limit + 10 + 4094*8 + 6, 7);
        CHECK(es2_col_parse((es2_span){limit,sizeof limit - 8}, &c, &e) == ES2_OK && c.sphere_count == 4095);
        CHECK(es2_col_group_at(&c, 0, &g, &e) == ES2_OK);
        CHECK(es2_col_shape_at(&g, 0, &s, &e) == ES2_OK);
        CHECK(es2_col_sphere_at(&s, 4094, &sphere, &e) == ES2_OK && sphere.radius == 7);
        test_put16(limit + 8, 4096);
        /* Fully present bytes still exceed the original signed read length. */
        CHECK(es2_col_parse((es2_span){limit,sizeof limit}, &c, &e) == ES2_ERR_COUNT && e.offset == 8);
    }
}

static void check_dmg(void)
{
    uint8_t b[33] = {0}, saved[33];
    es2_dmg d, sentinel = {0};
    es2_dmg_region r;
    es2_dmg_link link;
    es2_error e;
    int16_t capacity;
    size_t i;
    test_put16(b, 2); test_put16(b + 2, 73); test_put16(b + 4, 131);
    test_put16(b + 6, 2); /* Two regions, second without links. */
    test_put16(b + 8, 37); b[10] = 0xff; b[11] = 1; b[12] = 1; b[13] = 0xfd;
    test_put16(b + 14, 2);
    test_put16(b + 16, 11); test_put16(b + 18, 1);
    test_put16(b + 20, 7); test_put16(b + 22, 0);
    test_put16(b + 24, 29); b[28] = 0xff;
    memcpy(saved, b, sizeof b);
    CHECK(es2_dmg_parse((es2_span){b,32}, &d, &e) == ES2_OK);
    CHECK(d.capacity_count == 2 && d.region_count == 2 && d.link_count == 2 && d.regions_offset == 8);
    CHECK(es2_dmg_capacity_at(&d, 1, &capacity, &e) == ES2_OK && capacity == 131);
    CHECK(es2_dmg_capacity_at(&d, 2, &capacity, &e) == ES2_ERR_OFFSET);
    CHECK(es2_dmg_region_at(&d, 0, &r, &e) == ES2_OK);
    CHECK(r.offset == 8 && r.threshold == 37 && r.parameters.size == 4 && r.parameters.data[0] == 0xff);
    CHECK(r.debris_index == -1 && r.cell_slot == 1 && r.parent_region == 1 && r.flags == 0xfd);
    CHECK(es2_dmg_validate_references(&d, 2, 1, &e) == ES2_OK);
    CHECK(es2_dmg_validate_references(&d, 1, 1, &e) == ES2_ERR_OFFSET && e.offset == 11);
    CHECK(es2_dmg_validate_references(&d, 2, 0, &e) == ES2_ERR_OFFSET && e.offset == 26);
    CHECK(es2_dmg_link_at(&r, 0, &link, &e) == ES2_OK && link.weight == 11 && link.capacity_index == 1);
    CHECK(es2_dmg_link_at(&r, 2, &link, &e) == ES2_ERR_OFFSET);
    CHECK(es2_dmg_region_at(&d, 1, &r, &e) == ES2_OK && r.link_count == 0 && r.offset == 24);
    CHECK(es2_dmg_region_at(&d, 2, &r, &e) == ES2_ERR_OFFSET);
    sentinel.capacity_count = 321;
    for (i = 0; i < 32; ++i) {
        d = sentinel;
        CHECK(es2_dmg_parse((es2_span){b,i}, &d, &e) != ES2_OK);
        CHECK(d.capacity_count == 321);
    }
    CHECK(es2_dmg_parse((es2_span){b,33}, &d, &e) == ES2_ERR_LENGTH);
    for (i = 0; i < 4; ++i) {
        static const size_t offsets[] = {0,6,14,30};
        memcpy(b, saved, sizeof b); test_put16(b + offsets[i], UINT16_MAX);
        CHECK(es2_dmg_parse((es2_span){b,32}, &d, &e) == ES2_ERR_COUNT);
        memcpy(b, saved, sizeof b); test_put16(b + offsets[i], INT16_MAX);
        CHECK(es2_dmg_parse((es2_span){b,32}, &d, &e) == ES2_ERR_TRUNCATED);
    }
    memcpy(b, saved, sizeof b); test_put16(b + 18, 2);
    CHECK(es2_dmg_parse((es2_span){b,32}, &d, &e) == ES2_ERR_OFFSET && e.offset == 18);
    test_put16(b + 18, UINT16_MAX);
    CHECK(es2_dmg_parse((es2_span){b,32}, &d, &e) == ES2_ERR_OFFSET);
    memcpy(b, saved, sizeof b); b[12] = 2;
    CHECK(es2_dmg_parse((es2_span){b,32}, &d, &e) == ES2_ERR_OFFSET && e.offset == 12);
    memcpy(b, saved, sizeof b); b[10] = 0xfe;
    CHECK(es2_dmg_parse((es2_span){b,32}, &d, &e) == ES2_ERR_OFFSET && e.offset == 10);
    memcpy(b, saved, sizeof b); b[11] = 0x80; b[12] = 0x80;
    CHECK(es2_dmg_parse((es2_span){b,32}, &d, &e) == ES2_OK);
    CHECK(es2_dmg_validate_references(&d, 1, 1, &e) == ES2_OK);
    memset(b, 0, sizeof b);
    CHECK(es2_dmg_parse((es2_span){b,4}, &d, &e) == ES2_OK);
    CHECK(es2_dmg_parse((es2_span){NULL,4}, &d, &e) == ES2_ERR_ARGUMENT);
    CHECK(es2_dmg_parse((es2_span){b,4}, NULL, &e) == ES2_ERR_ARGUMENT);
}

static void check_gl(void)
{
    uint8_t b[55] = {0};
    es2_gl g;
    es2_gl_slot s;
    es2_error e;
    size_t i, match = 123;
    test_put16(b, 2); test_put16(b + 2, 17); test_put16(b + 28, 31);
    b[25] = 0; b[51] = 7; b[26] = 0xf4;
    CHECK(es2_gl_parse((es2_span){b,54}, &g, &e) == ES2_OK && g.count == 2);
    CHECK(es2_gl_slot_at(&g, 0, &s, &e) == ES2_OK);
    CHECK(s.part_id == 17 && s.display_slot == 0 && s.parameters_0x18.data[0] == 0xf4);
    CHECK(es2_gl_slot_at(&g, 1, &s, &e) == ES2_OK && s.offset == 28 && s.display_slot == 7);
    CHECK(es2_gl_validate_regions(&g, 27, &e) == ES2_OK);
    CHECK(es2_gl_validate_regions(&g, 26, &e) == ES2_ERR_OFFSET && e.offset == 51);
    CHECK(es2_gl_validate_capacities(&g, 20, &e) == ES2_OK);
    CHECK(es2_gl_validate_capacities(&g, 19, &e) == ES2_ERR_OFFSET && e.offset == 51);
    CHECK(es2_gl_validate_capacities(NULL, 20, &e) == ES2_ERR_ARGUMENT);
    /* Extra GL slots are valid even when PDG displays only slot zero. */
    CHECK(es2_gl_find_display_slot(&g, 0, &match, &e) == ES2_OK && match == 0);
    CHECK(es2_gl_find_display_slot(&g, 7, &match, &e) == ES2_OK && match == 1);
    CHECK(es2_gl_find_display_slot(&g, 1, &match, &e) == ES2_ERR_NOT_FOUND && match == 1);
    CHECK(es2_gl_slot_at(&g, 2, &s, &e) == ES2_ERR_OFFSET);
    for (i = 0; i < 54; ++i) CHECK(es2_gl_parse((es2_span){b,i}, &g, &e) != ES2_OK);
    CHECK(es2_gl_parse((es2_span){b,55}, &g, &e) == ES2_ERR_LENGTH);
    test_put16(b, UINT16_MAX);
    CHECK(es2_gl_parse((es2_span){b,54}, &g, &e) == ES2_ERR_COUNT);
    test_put16(b, INT16_MAX);
    CHECK(es2_gl_parse((es2_span){b,54}, &g, &e) == ES2_ERR_TRUNCATED);
    test_put16(b, 2); b[51] = 0x80;
    CHECK(es2_gl_parse((es2_span){b,54}, &g, &e) == ES2_OK);
    CHECK(es2_gl_slot_at(&g, 1, &s, &e) == ES2_OK && s.display_slot == -128);
    CHECK(es2_gl_validate_regions(&g, 29, &e) == ES2_ERR_OFFSET);
    CHECK(es2_gl_validate_capacities(&g, 22, &e) == ES2_ERR_OFFSET && e.offset == 51);
    test_put16(b, 0);
    CHECK(es2_gl_parse((es2_span){b,2}, &g, &e) == ES2_OK);
    CHECK(es2_gl_parse((es2_span){NULL,2}, &g, &e) == ES2_ERR_ARGUMENT);
}

static void check_pdg(void)
{
    uint8_t b[97] = {0}, saved[97];
    es2_pdg p, sentinel = {0};
    es2_pdg_view v;
    es2_pdg_rectangle rect;
    es2_pdg_slot slot;
    es2_error e;
    int32_t pair[2];
    size_t i;
    test_put32(b, 2); /* Two views, first has one rectangle; second empty. */
    test_put32(b + 4, (uint32_t)-17); test_put32(b + 8, 9);
    test_put32(b + 20, 1);
    test_put32(b + 24, 6); test_put32(b + 28, (uint32_t)-5); test_put16(b + 44, 26);
    b[46] = 0xaa; b[47] = 0x99; b[48] = 0xe7;
    test_put32(b + 72, 1); /* W, after view 2 at 52..71. */
    test_put32(b + 76, (uint32_t)-3); test_put32(b + 80, 51);
    test_put16(b + 84, (uint16_t)-2); b[86] = 0xcf;
    test_put32(b + 88, 0x24); test_put32(b + 92, 2);
    memcpy(saved, b, sizeof b);
    CHECK(es2_pdg_parse(ES2_PDG_VEHICLE, (es2_span){b,96}, &p, &e) == ES2_OK);
    CHECK(p.view_count == 2 && p.rectangle_count == 1 && p.slot_count == 1 && p.slots_offset == 76);
    CHECK(es2_pdg_view_at(&p, 0, &v, &e) == ES2_OK && v.offset == 4 && v.coordinates[0] == -17);
    CHECK(es2_pdg_rectangle_at(&v, 0, &rect, &e) == ES2_OK);
    CHECK(rect.offset == 24 && rect.selector == 6 && rect.coordinates[0] == -5 && rect.colour_index == 26);
    CHECK(rect.ignored_0x16.size == 2 && rect.ignored_0x16.data[1] == 0x99 && rect.parameters_0x18.data[0] == 0xe7);
    CHECK(es2_pdg_rectangle_at(&v, 1, &rect, &e) == ES2_ERR_OFFSET);
    CHECK(es2_pdg_view_at(&p, 1, &v, &e) == ES2_OK && v.offset == 52 && v.rectangle_count == 0);
    CHECK(es2_pdg_view_at(&p, 2, &v, &e) == ES2_ERR_OFFSET);
    CHECK(es2_pdg_slot_at(&p, 0, &slot, &e) == ES2_OK);
    CHECK(slot.offset == 76 && slot.x == -3 && slot.y == 51 && slot.bitmap_offset == -2 && slot.alignment == 0x24 && slot.mode == 2);
    CHECK(es2_pdg_slot_at(&p, 1, &slot, &e) == ES2_ERR_OFFSET);
    CHECK(es2_pdg_weapon_pair_at(&p, 0, pair, &e) == ES2_ERR_ARGUMENT);
    sentinel.view_count = 712;
    for (i = 0; i < 96; ++i) {
        p = sentinel;
        CHECK(es2_pdg_parse(ES2_PDG_VEHICLE, (es2_span){b,i}, &p, &e) != ES2_OK);
        CHECK(p.view_count == 712);
    }
    CHECK(es2_pdg_parse(ES2_PDG_VEHICLE, (es2_span){b,97}, &p, &e) == ES2_ERR_LENGTH);
    for (i = 0; i < 4; ++i) {
        static const size_t offsets[] = {0,20,68,72};
        memcpy(b, saved, sizeof b); test_put32(b + offsets[i], UINT32_MAX);
        CHECK(es2_pdg_parse(ES2_PDG_VEHICLE, (es2_span){b,96}, &p, &e) == ES2_ERR_COUNT);
        memcpy(b, saved, sizeof b); test_put32(b + offsets[i], INT32_MAX);
        CHECK(es2_pdg_parse(ES2_PDG_VEHICLE, (es2_span){b,96}, &p, &e) != ES2_OK);
    }
    memcpy(b, saved, sizeof b); test_put32(b, 4);
    CHECK(es2_pdg_parse(ES2_PDG_VEHICLE, (es2_span){b,96}, &p, &e) == ES2_ERR_COUNT);
    memcpy(b, saved, sizeof b); test_put16(b + 44, 27);
    CHECK(es2_pdg_parse(ES2_PDG_VEHICLE, (es2_span){b,96}, &p, &e) == ES2_ERR_OFFSET && e.offset == 44);
    test_put16(b + 44, UINT16_MAX);
    CHECK(es2_pdg_parse(ES2_PDG_VEHICLE, (es2_span){b,96}, &p, &e) == ES2_ERR_OFFSET);
    memset(b, 0, sizeof b);
    CHECK(es2_pdg_parse(ES2_PDG_VEHICLE, (es2_span){b,8}, &p, &e) == ES2_OK);
    test_put32(b, 3); /* Three empty views, no slots. */
    CHECK(es2_pdg_parse(ES2_PDG_VEHICLE, (es2_span){b,68}, &p, &e) == ES2_OK);
    test_put32(b, 2); test_put32(b + 4, INT32_MIN); test_put32(b + 8, INT32_MAX);
    CHECK(es2_pdg_parse(ES2_PDG_WEAPONS, (es2_span){b,20}, &p, &e) == ES2_OK && p.slot_count == 2);
    CHECK(es2_pdg_weapon_pair_at(&p, 0, pair, &e) == ES2_OK && pair[0] == INT32_MIN && pair[1] == INT32_MAX);
    CHECK(es2_pdg_slot_at(&p, 0, &slot, &e) == ES2_ERR_ARGUMENT);
    CHECK(es2_pdg_weapon_pair_at(&p, 2, pair, &e) == ES2_ERR_OFFSET);
    for (i = 0; i < 20; ++i) CHECK(es2_pdg_parse(ES2_PDG_WEAPONS, (es2_span){b,i}, &p, &e) != ES2_OK);
    CHECK(es2_pdg_parse(ES2_PDG_WEAPONS, (es2_span){b,21}, &p, &e) == ES2_ERR_LENGTH);
    test_put32(b, UINT32_MAX);
    CHECK(es2_pdg_parse(ES2_PDG_WEAPONS, (es2_span){b,20}, &p, &e) == ES2_ERR_COUNT);
    test_put32(b, INT32_MAX);
    CHECK(es2_pdg_parse(ES2_PDG_WEAPONS, (es2_span){b,20}, &p, &e) == ES2_ERR_TRUNCATED);
    CHECK(es2_pdg_parse((es2_pdg_kind)99, (es2_span){b,20}, &p, &e) == ES2_ERR_ARGUMENT);
    CHECK(es2_pdg_parse(ES2_PDG_WEAPONS, (es2_span){NULL,20}, &p, &e) == ES2_ERR_ARGUMENT);
}

static void check_bnd(void)
{
    uint8_t b[9] = {0};
    es2_bnd v;
    es2_error e;
    size_t i;
    test_put32(b, UINT32_C(0x12345678));
    /* Arbitrary lengths, including odd/empty, are inspectable. This makes no
     * assertion of BND validity, so there is no fake truncated-file rule. */
    for (i = 0; i <= sizeof b; ++i) {
        CHECK(es2_bnd_inspect((es2_span){b,i}, &v, &e) == ES2_OK);
        CHECK(v.opaque_offset == 0 && v.opaque_length == i && v.input.data == b);
        CHECK(v.has_prefix_candidate == (i >= 4));
        if (i >= 4) CHECK(v.prefix_candidate == UINT32_C(0x12345678));
    }
    CHECK(es2_bnd_inspect((es2_span){NULL,0}, &v, &e) == ES2_OK);
    CHECK(es2_bnd_inspect((es2_span){NULL,1}, &v, &e) == ES2_ERR_ARGUMENT);
    CHECK(es2_bnd_inspect((es2_span){b,9}, NULL, &e) == ES2_ERR_ARGUMENT);
}

int test_collision(void)
{
    check_col(); check_dmg(); check_gl(); check_pdg(); check_bnd();
    return 0;
}
