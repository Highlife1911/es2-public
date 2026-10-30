#include "es2/core/track.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"track line %d: %s\n",__LINE__,#x);return 1;}}while(0)
static void w16(uint8_t *p, uint32_t x) { p[0] = (uint8_t)x; p[1] = (uint8_t)(x >> 8); }
static uint32_t r32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint32_t host(const void *p) { return (uint32_t)(uintptr_t)p; }

static void random_fill(uint8_t *p, size_t n, uint32_t *state) {
    for (size_t i = 0; i < n; ++i) { *state = *state * 1103515245u + 12345u; p[i] = (uint8_t)(*state >> 16); }
}

int main(void) {
    uint8_t table[2050], track[0x60], copy[0x60], out[32], out2[32], parent[32], pose[12];
    uint32_t seed = 7;
    /* Synthetic ramp; deliberately not the game's table. */
    for (unsigned i = 0; i <= 1024; ++i) w16(table + 2 * i, 16384 - 16 * i);

    /* Disabled track: memory unchanged, incoming EAX/EDX returned. */
    random_fill(track, sizeof(track), &seed); w16(track + 0x20, 0); memcpy(copy, track, sizeof(track));
    uint64_t r = es2_track_forward_registers(track, table, 0x5a, 0x11111111u, 0x22222222u);
    CHECK(r == ((uint64_t)0x22222222u << 32 | 0x11111111u) && !memcmp(track, copy, sizeof(track)));
    r = es2_track_inverse_registers(track, table, 0x5a, 3, 4);
    CHECK(r == ((uint64_t)4 << 32 | 3) && !memcmp(track, copy, sizeof(track)));

    /* Disabled scaled copy: bytes 0..19 then Z, X, Y; EAX=out+20, EDX=Y. */
    memset(out, 0, sizeof(out));
    r = es2_track_scaled_registers(track, out, table, 0x5a);
    CHECK(!memcmp(out, track + 0x2e, 32) && (uint32_t)r == host(out + 20) && (uint32_t)(r >> 32) == r32(track + 0x46));
    /* Disabled inverse copy into T+0x2e: EAX=T+0x42, EDX=T+0x2e. */
    memcpy(parent, track, 32);
    r = es2_track_scaled_inverse_registers(track, parent, table, 0x5a);
    CHECK(!memcmp(track + 0x2e, parent, 32) && (uint32_t)r == host(track + 0x42) && (uint32_t)(r >> 32) == host(track + 0x2e));

    /* Zero angles give an identity local (mode 0): composition copies the
     * parent's rotation including its byte 19, whatever the padding. */
    random_fill(pose, sizeof(pose), &seed); memset(pose, 0, 6);
    random_fill(parent, sizeof(parent), &seed); parent[18] = 2; parent[19] = 0x77;
    es2_pose_parent_registers(pose, parent, out, table, 0x00);
    es2_pose_parent_registers(pose, parent, out2, table, 0xff);
    CHECK(!memcmp(out, out2, 32) && out[19] == 0x77);

    /* Rotating local with a non-rotating parent: the local is copied, so
     * byte 19 is the padding (stack residue in the original). */
    w16(pose + 4, 0x1234); parent[18] = 0;
    es2_pose_parent_registers(pose, parent, out, table, 0x00);
    es2_pose_parent_registers(pose, parent, out2, table, 0xff);
    CHECK(out[19] == 0x00 && out2[19] == 0xff);
    out[19] = out2[19]; CHECK(!memcmp(out, out2, 32));

    /* Full composition into T+0x2e: only T+0x41 depends on the padding. */
    random_fill(track, sizeof(track), &seed); w16(track + 0x20, 1); track[0x2e + 18] = 2;
    w16(track + 0x28, 0x0100); w16(track + 0x2a, 0x0200); w16(track + 0x2c, 0x0300);
    memcpy(copy, track, sizeof(track));
    r = es2_track_forward_registers(track, table, 0x00, 0, 0);
    CHECK((uint32_t)r == host(track + 0x42) && (uint32_t)(r >> 32) == host(track + 0x2e) && track[0x41] == 0x00);
    uint8_t first[0x60]; memcpy(first, track, sizeof(track)); memcpy(track, copy, sizeof(track));
    es2_track_forward_registers(track, table, 0xff, 0, 0);
    CHECK(track[0x41] == 0xff); track[0x41] = 0; CHECK(!memcmp(first, track, sizeof(track)));
    CHECK(!memcmp(first, copy, 0x2e) && !memcmp(first + 0x4e, copy + 0x4e, sizeof(track) - 0x4e));

    /* Scaled inverse of a zero pose at scale 1/1 is the identity: T+0x2e
     * becomes the input's rotation and translation. */
    random_fill(track, sizeof(track), &seed); w16(track + 0x20, 1); w16(track + 0x1c, 1); w16(track + 0x1e, 1);
    memset(track + 0x22, 0, 12);
    random_fill(parent, sizeof(parent), &seed); parent[18] = 2;
    r = es2_track_scaled_inverse_registers(track, parent, table, 0x5a);
    CHECK(!memcmp(track + 0x2e, parent, 32) && (uint32_t)r == host(track + 0x2e));

    printf("track: %u checks passed\n", checks);
    return 0;
}
