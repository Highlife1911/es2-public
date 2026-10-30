#include "es2/core/terrain.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Hand-computed heights on a 2 x 2 grid with 16-unit cells; exact
 * equivalence with the original is checked in vitro. */
#define BASE 0x00040000u
static uint8_t arena[1024];
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"terrain line %d: %s\n",__LINE__,#x);return 1;}}while(0)
static uint8_t *at(uint32_t a) { return arena + (a - BASE); }
static uint32_t r32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static void w32(uint8_t *p, uint32_t x) { for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(x >> (8 * i)); }
static uint32_t g_read32(void *c, uint32_t a) { (void)c; return r32(at(a)); }
static uint16_t g_read16(void *c, uint32_t a) { (void)c; return (uint16_t)(at(a)[0] | at(a)[1] << 8); }
static void g_write32(void *c, uint32_t a, uint32_t v) { (void)c; w32(at(a), v); }
static void g_write16(void *c, uint32_t a, uint16_t v) { (void)c; at(a)[0] = (uint8_t)v; at(a)[1] = (uint8_t)(v >> 8); }
static void *g_host(void *c, uint32_t a) { (void)c; return at(a); }
static const es2_guest_memory memory = {NULL, g_read32, g_read16, g_write32, g_write16, g_host};
enum { GRID = 0x000, CELLS = 0x200, POINT = 0x300 };

static uint64_t height(int32_t x, int32_t y) {
    w32(at(BASE + POINT), (uint32_t)x); w32(at(BASE + POINT + 4), (uint32_t)y);
    return es2_terrain_height_registers(&memory, BASE + GRID, BASE + POINT);
}

int main(void) {
    memset(arena, 0xcd, sizeof(arena));
    uint8_t *g = at(BASE + GRID);
    w32(g + 0xec, BASE + CELLS); w32(g + 0x100, 1); w32(g + 0x104, 1); w32(g + 0x108, 4);
    w32(g + 0x110, 0); w32(g + 0x118, 1);
    /* Heights: (0,0)=10, (1,0)=20, (0,1)=30, (1,1)=40; cell index = row*2 + column. */
    const uint8_t heights[4] = {10, 20, 30, 40};
    for (unsigned i = 0; i < 4; ++i) { at(BASE + CELLS + 16 * i)[0] = heights[i]; at(BASE + CELLS + 16 * i)[15] = 0; }

    /* Flags 0, first triangle: 10 + (10*4 >> 4) + (20*2 >> 4) = 14; EDX = high
     * word of 20*2. Second triangle: 40 + (-20 >> 4) + (-120 >> 4) = 30. */
    CHECK(height(4, 2) == 14);
    CHECK(height(14, 10) == ((uint64_t)30 << 32 | 30));
    /* Flags 2 (other diagonal): 20 + (-120 >> 4) + (40 >> 4) = 14, EDX = EAX. */
    at(BASE + CELLS)[15] = 2;
    CHECK(height(4, 2) == ((uint64_t)14 << 32 | 14));
    /* Flags 3 behave like the first flags-0 triangle. */
    at(BASE + CELLS)[15] = 3;
    CHECK(height(14, 10) == (uint64_t)(10 + (10 * 14 >> 4) + (20 * 10 >> 4)));
    /* Outside the grid: 0, EDX = column after the shift. */
    CHECK(height(-1, 0) == (uint64_t)0xffffffffu << 32);
    CHECK(height(40, 0) == (uint64_t)2 << 32);
    /* Last cell: corner (2,1) leaves the 4-cell array: 0, EDX = 0. */
    CHECK(height(20, 20) == 0);

    printf("terrain: %u checks, 0 failures\n", checks);
    return 0;
}
