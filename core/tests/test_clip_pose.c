#include "es2/core/pose.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Synthetic guest address space: 4 KiB at guest address 0x00010000. */
#define BASE 0x00010000u
static uint8_t arena[4096];
static unsigned faults, checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"clip_pose line %d: %s\n",__LINE__,#x);return 1;}}while(0)

static uint8_t *at(uint32_t address, unsigned size) {
    uint32_t offset = address - BASE;
    if (offset > sizeof(arena) - size) { ++faults; return NULL; }
    return arena + offset;
}
static uint32_t g_read32(void *c, uint32_t a) {
    (void)c; const uint8_t *p = at(a, 4);
    return p ? (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24 : 0;
}
static uint16_t g_read16(void *c, uint32_t a) {
    (void)c; const uint8_t *p = at(a, 2);
    return p ? (uint16_t)((uint16_t)p[0] | (uint16_t)p[1] << 8) : 0;
}
static void g_write32(void *c, uint32_t a, uint32_t v) {
    (void)c; uint8_t *p = at(a, 4);
    if (p) for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (8 * i));
}
static void g_write16(void *c, uint32_t a, uint16_t v) {
    (void)c; uint8_t *p = at(a, 2);
    if (p) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
}
static void *g_host(void *c, uint32_t a) { (void)c; return at(a, 1); }
static const es2_guest_memory memory = {NULL, g_read32, g_read16, g_write32, g_write16, g_host};

enum { TRACK = 0x100, RESOURCE = 0x200, CLIP = 0x300, LIST = 0x400, INDEX = 0x500, POSE = 0x600, POSE2 = 0x700 };

/* Builds a graph in which (clip, frame, stride, pose) select the fixed slots,
 * whatever their values, by choosing the base pointers modulo 2^32. */
static void build(uint32_t clip, uint32_t frame, int16_t stride, int16_t pose, uint16_t enabled) {
    memset(arena, 0xcd, sizeof(arena));
    uint32_t index = frame * (uint32_t)(int32_t)stride;
    g_write32(NULL, BASE + TRACK, BASE + RESOURCE);
    g_write32(NULL, BASE + RESOURCE + 6, BASE + LIST - 4u * clip);
    g_write32(NULL, BASE + LIST, BASE + CLIP);
    g_write16(NULL, BASE + CLIP + 8, enabled);
    g_write16(NULL, BASE + CLIP + 0x10, (uint16_t)stride);
    g_write32(NULL, BASE + CLIP + 0x16, BASE + INDEX - 2u * index);
    g_write16(NULL, BASE + INDEX, (uint16_t)pose);
    g_write32(NULL, BASE + RESOURCE + 0x12, BASE + POSE - 12u * (uint32_t)(int32_t)pose);
    for (unsigned i = 0; i < 12; ++i) arena[POSE + i] = (uint8_t)(0x10 + i);
}

int main(void) {
    uint8_t before[sizeof(arena)];
    uint64_t r;

    /* Disabled clip: only T+0x20 changes; EDX keeps the full clip argument. */
    build(0xffff8000u, 5, 3, 1, 0);
    memcpy(before, arena, sizeof(arena));
    r = es2_clip_pose_registers(&memory, BASE + TRACK, 0xffff8000u, 5);
    CHECK((uint32_t)r == BASE + TRACK && (uint32_t)(r >> 32) == 0xffff8000u);
    CHECK(arena[TRACK + 0x20] == 0 && arena[TRACK + 0x21] == 0);
    arena[TRACK + 0x20] = before[TRACK + 0x20]; arena[TRACK + 0x21] = before[TRACK + 0x21];
    CHECK(!memcmp(arena, before, sizeof(arena)));

    /* Negative clip, frame, stride and pose index with wrapped addresses. */
    build(0xfffffff9u, 0xfffffffdu, -7, -2, 1);
    memcpy(before, arena, sizeof(arena));
    r = es2_clip_pose_registers(&memory, BASE + TRACK, 0xfffffff9u, 0xfffffffdu);
    CHECK((uint32_t)r == BASE + TRACK && (uint32_t)(r >> 32) == (uint32_t)-6);
    CHECK(!memcmp(arena + TRACK + 0x22, arena + POSE + 6, 6));
    CHECK(!memcmp(arena + TRACK + 0x28, arena + POSE, 6));
    CHECK(arena[TRACK + 0x20] == 1 && arena[TRACK + 0x21] == 0);
    memcpy(arena + TRACK + 0x20, before + TRACK + 0x20, 14);
    CHECK(!memcmp(arena, before, sizeof(arena)));

    /* Full 32-bit frame*stride product wraps like IMUL's low half. */
    build(3, 0x40000001u, 0x7fff, 0x7fff, 0x8000);
    r = es2_clip_pose_registers(&memory, BASE + TRACK, 3, 0x40000001u);
    CHECK((uint32_t)(r >> 32) == 3u * 0x7fffu && !memcmp(arena + TRACK + 0x28, arena + POSE, 6));

    /* Alias: resource+0x12 is T+0x22, so the angle read uses the pose base
     * that the translation write just stored there, as in the original. */
    build(0, 0, 0, 0, 1);
    g_write32(NULL, BASE + TRACK, BASE + TRACK + 0x22 - 0x12);
    g_write32(NULL, BASE + TRACK + 0x22 - 0x12 + 6, BASE + LIST);
    g_write32(NULL, BASE + TRACK + 0x22, BASE + POSE);
    g_write32(NULL, BASE + POSE + 6, BASE + POSE2);
    for (unsigned i = 0; i < 6; ++i) arena[POSE2 + i] = (uint8_t)(0x80 + i);
    r = es2_clip_pose_registers(&memory, BASE + TRACK, 0, 0);
    CHECK(g_read32(NULL, BASE + TRACK + 0x22) == BASE + POSE2);
    CHECK(!memcmp(arena + TRACK + 0x28, arena + POSE2, 6));

    CHECK(faults == 0);
    printf("clip_pose: %u checks passed\n", checks);
    return 0;
}
