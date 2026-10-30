#include "es2/core/geometry.h"
#include <stdio.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "geometry line %d: %s\n", __LINE__, #x); return 1; } } while (0)
static void put32(uint8_t *p, uint32_t x) {
    for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(x >> (8 * i));
}

int main(void)
{
    /* Expected bit patterns derived from signed comparison of wrapped ABS,
     * SAR of the smaller magnitude, and modulo-2^32 addition (0x47de4c).
     * INT_MIN is still negative after ABS; it therefore sorts BEFORE zero. */
    static const uint32_t pairs[][3] = {
        {0,0,0}, {3,4,5}, {4,3,5}, {1,1,1}, {7,7,10},
        {0xfffffffd,4,5}, {3,0xfffffffc,5}, {0xffffffff,0xffffffff,1},
        {9999,0,9999}, {8000,4000,10000}, {8000,3999,9999},
        {0x7fffffff,0,0x7fffffff}, {0x7fffffff,1,0x7fffffff},
        {0x7fffffff,2,0x80000000}, {0x7fffffff,0x7fffffff,0xbffffffe},
        {0x80000000,0,0xc0000000}, {0x80000000,1,0xc0000001},
        {0x80000000,0x7fffffff,0x3fffffff},
        {0x80000000,0x80000000,0x40000000}, {0x80000001,2,0x80000000},
    };
    for (unsigned i = 0; i < sizeof(pairs)/sizeof(pairs[0]); ++i) {
        CHECK(es2_distance2(pairs[i][0], pairs[i][1]) == pairs[i][2]);
        CHECK(es2_distance2(pairs[i][1], pairs[i][0]) == pairs[i][2]);
        uint8_t v[10]; memset(v, 0xa5, sizeof(v));
        put32(v + 1, pairs[i][0]); put32(v + 5, pairs[i][1]);
        uint8_t copy[10]; memcpy(copy, v, sizeof(v));
        CHECK(es2_distance2_vector(v + 1) == pairs[i][2]);
        CHECK(!memcmp(copy, v, sizeof(v)));
    }
    /* 3-D weights: L + floor(M/4) + floor(M/16) + floor(M/32)
     * + floor(S/4), each term separately (0x47dea4..0x47deb5). */
    static const uint32_t triples[][4] = {
        {0,0,0,0}, {0,0,99,99}, {3,4,5,6}, {7,7,7,9},
        {32,32,32,51}, {31,31,31,46}, {100,32,8,113},
        {0xffffff9c,32,0xfffffff8,113},
        {0x80000000,0,0,0xe0000000},
        {0x80000000,0x80000000,0,0xb4000000},
        {0x80000000,0x80000000,0x80000000,0x34000000},
        {0x7fffffff,0x7fffffff,0x7fffffff,0xcbfffffb},
    };
    static const unsigned order[][3] = {{0,1,2},{0,2,1},{1,0,2},{1,2,0},{2,0,1},{2,1,0}};
    for (unsigned i = 0; i < sizeof(triples)/sizeof(triples[0]); ++i)
        for (unsigned j = 0; j < 6; ++j)
            CHECK(es2_distance3(triples[i][order[j][0]], triples[i][order[j][1]], triples[i][order[j][2]]) == triples[i][3]);

    /* Subtraction wraps BEFORE taking magnitudes: MAX - MIN is -1, not
     * 4294967295. Distinct heights prove the gate is not the XY metric. */
    uint8_t a[14], b[14], saved[28];
    memset(a, 0x9a, sizeof(a)); memset(b, 0x8b, sizeof(b));
    put32(a+1, 0x7fffffff); put32(b+1, 0x80000000);
    put32(a+5, 0x80000000); put32(b+5, 0x7fffffff);
    put32(a+9, 32); put32(b+9, 0);
    memcpy(saved, a, 14); memcpy(saved+14, b, 14);
    CHECK(es2_distance2_points(a+1,b+1) == 1);
    CHECK(es2_distance3_points(a+1,b+1) == 32);
    CHECK(es2_distance2_points(a+1,a+1) == 0);
    CHECK(es2_distance3_points(a+1,a+1) == 0);
    CHECK(!memcmp(saved,a,14) && !memcmp(saved+14,b,14));
    printf("geometry: %u checks passed\n", checks);
    return 0;
}
