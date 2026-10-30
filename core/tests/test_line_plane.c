#include "es2/core/terrain.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Hand-computed segment/plane intersections (0x47e610); exact equivalence
 * with the original is checked in vitro (line_plane_cases.h). */
#define BASE 0x00050000u
static uint8_t arena[256];
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"line_plane line %d: %s\n",__LINE__,#x);return 1;}}while(0)
static uint8_t *at(uint32_t a) { return arena + (a - BASE); }
static uint32_t r32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static void w32(uint8_t *p, uint32_t x) { for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(x >> (8 * i)); }
static uint32_t g_read32(void *c, uint32_t a) { (void)c; return r32(at(a)); }
static uint16_t g_read16(void *c, uint32_t a) { (void)c; return (uint16_t)(at(a)[0] | at(a)[1] << 8); }
static void g_write32(void *c, uint32_t a, uint32_t v) { (void)c; w32(at(a), v); }
static void g_write16(void *c, uint32_t a, uint16_t v) { (void)c; at(a)[0] = (uint8_t)v; at(a)[1] = (uint8_t)(v >> 8); }
static void *g_host(void *c, uint32_t a) { (void)c; return at(a); }
static const es2_guest_memory memory = {NULL, g_read32, g_read16, g_write32, g_write16, g_host, NULL};
enum { PLANE = 0x00, SEG = 0x20, OUT = 0x41, FRAME = 0x1000 };

static void plane(int32_t x, int32_t y, int32_t z, int32_t c) {
    w32(at(BASE + PLANE), (uint32_t)x); w32(at(BASE + PLANE + 4), (uint32_t)y);
    w32(at(BASE + PLANE + 8), (uint32_t)z); w32(at(BASE + PLANE + 12), (uint32_t)c);
}
static void segment(int32_t x0, int32_t y0, int32_t z0, int32_t x1, int32_t y1, int32_t z1) {
    const int32_t v[6] = {x0, y0, z0, x1, y1, z1};
    for (unsigned i = 0; i < 6; ++i) w32(at(BASE + SEG + 4 * i), (uint32_t)v[i]);
}
static uint64_t run(uint32_t out) { return es2_line_plane_registers(&memory, FRAME, BASE + PLANE, BASE + SEG, out); }
static int32_t out_word(unsigned k) { return (int32_t)r32(at(BASE + OUT + 4 * k)); }
static const uint64_t hit = (uint64_t)(FRAME - 0x28u) << 32 | ((BASE + OUT) & 0xffffff00u) | 1u;

int main(void) {
    memset(arena, 0xcd, sizeof(arena));
    /* Plane z = 10 crossed upwards: den = 20, num = 10, t = 1/2. */
    plane(0, 0, 1, -10); segment(0, 0, 0, 100, 50, 20);
    CHECK(run(BASE + OUT) == hit);
    CHECK(out_word(0) == 50 && out_word(1) == 25 && out_word(2) == 10);
    /* Downwards: den = -20, num = -10. */
    segment(4, 6, 20, 4, 6, 0);
    CHECK(run(BASE + OUT) == hit && out_word(0) == 4 && out_word(1) == 6 && out_word(2) == 10);
    /* Endpoints: num = 0 (start on the plane) and num = den (end on it). */
    segment(1, 2, 10, 9, 9, 30);
    CHECK(run(BASE + OUT) == hit && out_word(0) == 1 && out_word(1) == 2 && out_word(2) == 10);
    segment(1, 2, 0, 9, 9, 10);
    CHECK(run(BASE + OUT) == hit && out_word(0) == 9 && out_word(1) == 9 && out_word(2) == 10);
    /* Truncation toward zero per axis: 7/2 -> 3, -7/2 -> -3. */
    plane(0, 0, 2, -2); segment(0, 0, 0, 7, -7, 2);
    CHECK(run(BASE + OUT) == hit && out_word(0) == 3 && out_word(1) == -3 && out_word(2) == 1);
    /* Parallel: den = 1*2 + 2*(-1) = 0; EDX = n.y * D.y. */
    memset(at(BASE + OUT), 0xee, 12);
    plane(1, 2, 0, 5); segment(0, 0, 0, 2, -1, 9);
    CHECK(run(BASE + OUT) == (uint64_t)0xfffffffeu << 32);
    CHECK(r32(at(BASE + OUT)) == 0xeeeeeeeeu);
    /* Plane behind the start: num < 0 with den > 0; EDX = n.y * S.y. */
    plane(0, 7, 1, -10); segment(0, 3, 0, 0, 3, 5);
    CHECK(run(BASE + OUT) == (uint64_t)21 << 32);
    /* Beyond the end: num > den. */
    plane(0, 0, 1, -10); segment(0, 0, 0, 0, 0, 5);
    CHECK(run(BASE + OUT) == 0);
    /* den < 0: num > 0 and num < den both miss. */
    segment(0, 0, 5, 0, 0, 0);
    CHECK(run(BASE + OUT) == 0);
    segment(0, 0, 30, 0, 0, 20);
    CHECK(run(BASE + OUT) == 0);
    /* Wrapping products: den = 0x10000 * 0x10000 = 2^32 wraps to 0. */
    plane(0x10000, 0, 0, 0); segment(0, 0, 0, 0x10000, 0, 0);
    CHECK(run(BASE + OUT) == 0);
    /* out overlapping the segment: out = S + 4 writes S.y before it is
     * reread for the second word (X: 0 + 50 -> S.y; Y: 50 + 25). */
    plane(0, 0, 1, -10); segment(0, 0, 0, 100, 50, 20);
    CHECK(es2_line_plane_registers(&memory, FRAME, BASE + PLANE, BASE + SEG, BASE + SEG + 4) ==
          ((uint64_t)(FRAME - 0x28u) << 32 | ((BASE + SEG + 4) & 0xffffff00u) | 1u));
    CHECK(r32(at(BASE + SEG + 4)) == 50 && r32(at(BASE + SEG + 8)) == 75 && r32(at(BASE + SEG + 12)) == 10 + 75);

    printf("line_plane: %u checks, 0 failures\n", checks);
    return 0;
}
