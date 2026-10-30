#include "es2/core/timing.h"
#include <stdio.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "timing line %d: %s\n", __LINE__, #x); return 1; } } while (0)
static void put32(uint8_t *p, uint32_t x) {
    for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(x >> (8*i));
}
static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}

int main(void)
{
    /* IDIV signed-word elapsed*256/reference; signed low-word comparisons
     * clamp only AX. Quotient high bits survive, including negative ones. */
    static const uint32_t scales[][3] = {
        {125,47,96}, {125,0,64}, {125,31,64}, {125,32,65},
        {125,219,448}, {125,220,450}, {125,221,450},
        {125,0xffd1,0xffff0040}, {0xff83,47,0xffff0040},
        {0xff83,0xffd1,96}, {1,127,450}, {1,128,64},
        {1,255,64}, {1,256,0x00010040}, {1,257,0x00010100},
        {1,32767,0x007f0040}, {1,32768,0xff800040},
        {0xffff,32768,0x00800040}, {1,0xff00,0xffff0040},
        {0x8000,0x8000,256}, {0x7fff,0x8000,0xffff0040},
    };
    for (unsigned i = 0; i < sizeof(scales)/sizeof(scales[0]); ++i)
        CHECK(es2_time_scale((uint16_t)scales[i][0], (uint16_t)scales[i][1]) == scales[i][2]);

    /* SUB then sign-bit test, NOT a saturating mathematical subtraction. */
    static const uint32_t words[][3] = {
        {10000,96,9904},{96,96,0},{95,96,0},{0,96,0},
        {0x8000,1,0x7fff},{0x7fff,0xffff,0},{0,0xffff,1},
        {0xffff,0xffff,0},{0,0x8000,0},{0x8000,0x8000,0},
        {0xffff,0x8000,0x7fff},{0x7fff,0,0x7fff},
    };
    uint8_t buffer[8];
    for (unsigned i = 0; i < sizeof(words)/sizeof(words[0]); ++i) {
        memset(buffer,0xa6,sizeof(buffer));
        buffer[2]=(uint8_t)words[i][0]; buffer[3]=(uint8_t)(words[i][0]>>8);
        CHECK(es2_timer_word_step(buffer+1,(uint16_t)words[i][1]) == words[i][2]);
        CHECK(((uint32_t)buffer[2] | ((uint32_t)buffer[3]<<8)) == words[i][2]);
        CHECK(buffer[0]==0xa6 && buffer[1]==0xa6 && buffer[4]==0xa6 && buffer[7]==0xa6);
    }
    static const uint32_t dwords[][3] = {
        {10000,96,9904},{96,96,0},{95,96,0},{0,96,0},
        {0x80000000,1,0x7fffffff},{0x7fffffff,0xffff,0},
        {0,0xffff,1},{0xffffffff,0xffff,0},{0,0x8000,32768},
        {0x80000000,0x8000,0},{0x80000000,0x7fff,0x7fff8001},
    };
    for (unsigned i = 0; i < sizeof(dwords)/sizeof(dwords[0]); ++i) {
        memset(buffer,0xa6,sizeof(buffer)); put32(buffer+2,dwords[i][0]);
        CHECK(es2_timer_dword_step(buffer+1,(uint16_t)dwords[i][1]) == dwords[i][2]);
        CHECK(get32(buffer+2) == dwords[i][2]);
        CHECK(buffer[0]==0xa6 && buffer[1]==0xa6 && buffer[6]==0xa6 && buffer[7]==0xa6);
    }
    static const uint32_t arms[][2] = {{0,0},{1,2048},{60,122880},{30000,61440000},
        {32767,0x03fff800},{0xffff,0xfffff800},{0x8000,0xfc000000}};
    for (unsigned i=0;i<sizeof(arms)/sizeof(arms[0]);++i) {
        memset(buffer,0xa6,sizeof(buffer));
        es2_timer_arm(buffer+1,(uint16_t)arms[i][0]);
        CHECK(get32(buffer+2)==arms[i][1]);
        CHECK(buffer[0]==0xa6 && buffer[1]==0xa6 && buffer[6]==0xa6 && buffer[7]==0xa6);
    }
    /* Published 47-ms periodic cadence: 104*96=9984; expiry on step 105. */
    put32(buffer+2,10000);
    for (unsigned i=1;i<=104;++i) CHECK(es2_timer_dword_step(buffer+1,96)==10000-i*96);
    CHECK(es2_timer_dword_step(buffer+1,96)==0);
    printf("timing: %u checks passed\n", checks);
    return 0;
}
