#include "es2/core/animation.h"
#include "es2/core/track_pose.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Hand-built scenarios for 0x479ab0, 0x478b00, 0x479344 and 0x403d5c in a
 * synthetic guest arena; the exact equivalence with the original is the
 * in-vitro harness's job. The bridge records every call and emulates the
 * Q8 product and the word approach, so the turret arithmetic is visible. */
#define BASE 0x00030000u
#define IMAGE (BASE + 0x800u - 0x973e4u) /* puts the turret tables at BASE+0x800 */
static uint8_t arena[4096];
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"track pose line %d: %s\n",__LINE__,#x);return 1;}}while(0)
static uint8_t *at(uint32_t a) { return arena + (a - BASE); }
static uint32_t r32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t r16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static void w32(uint8_t *p, uint32_t x) { for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(x >> (8 * i)); }
static void w16(uint8_t *p, uint32_t x) { p[0] = (uint8_t)x; p[1] = (uint8_t)(x >> 8); }
static uint32_t g_read32(void *c, uint32_t a) { (void)c; return r32(at(a)); }
static uint16_t g_read16(void *c, uint32_t a) { (void)c; return r16(at(a)); }
static void g_write32(void *c, uint32_t a, uint32_t v) { (void)c; w32(at(a), v); }
static void g_write16(void *c, uint32_t a, uint16_t v) { (void)c; w16(at(a), v); }
static void *g_host(void *c, uint32_t a) { (void)c; return at(a); }
static const es2_guest_memory memory = {NULL, g_read32, g_read16, g_write32, g_write16, g_host, NULL};

typedef struct call_record { uint32_t function, args[4], regs[3]; unsigned count; } call_record;
static call_record calls[16];
static unsigned call_count;
static void bridge(void *c, uint32_t f, const uint32_t *a, unsigned n, es2_weapon_registers *r) {
    (void)c;
    call_record *v = &calls[call_count++ & 15];
    v->function = f; v->count = n; memcpy(v->args, a, 4 * n); v->regs[0] = r->eax; v->regs[1] = r->edx; v->regs[2] = r->ecx;
    if (f == IMAGE + 0x7e0a0u) { /* Q8 product */
        int64_t p = (int64_t)(int32_t)a[0] * (int32_t)a[1];
        r->eax = (uint32_t)(p >> 8); r->edx = (uint32_t)((uint64_t)p >> 32);
    } else if (f == IMAGE + 0x67adcu) { /* reach the target at once */
        w16(at(a[0]), a[1]); r->eax = 0; r->edx = a[1];
    } else { r->eax = 0x1111u * call_count; r->edx = 0x2222u * call_count; }
}
static const es2_weapon_graph graph = {IMAGE, 0, 0, 0, 0, bridge, NULL, NULL, NULL};

enum { T = 0x000, R = 0x060, LIST = 0x080, CLIP = 0x0a0, DUR = 0x0c0, B = 0x100, P = 0x340,
       MAP = 0x360, INDEX = 0x370, RPOSES = 0x390, POSES = 0x400, FLAGS = 0x440, M = 0x460 };

static int seek(void) {
    memset(arena, 0xcd, sizeof(arena));
    w32(at(BASE + T), BASE + R); w32(at(BASE + R + 6), BASE + LIST - 4u * 2u); w32(at(BASE + LIST), BASE + CLIP);
    w16(at(BASE + CLIP + 0x0a), 3); w32(at(BASE + CLIP + 0x0c), BASE + DUR);
    w16(at(BASE + DUR), 10); w16(at(BASE + DUR + 6), 20); w16(at(BASE + DUR + 12), 30); w16(at(BASE + DUR + 18), 0x7fff);
    static const struct { uint32_t fraction, frame, phase; } cases[] = {{0, 0, 0}, {0x2000, 1, 19}, {0x3fff, 2, 28}, {0xffff8000u, 0, 0xffffff8au}};
    for (unsigned i = 0; i < 4; ++i) {
        call_count = 0;
        uint64_t result = es2_track_seek_registers(&memory, &graph, BASE + T, 0xabcd0002u, cases[i].fraction);
        CHECK(call_count == 1 && calls[0].function == IMAGE + 0x792acu && calls[0].count == 4);
        CHECK(calls[0].args[0] == BASE + T && calls[0].args[1] == ((BASE + LIST) & 0xffff0000u) + 2u);
        CHECK(calls[0].args[2] == cases[i].frame && calls[0].args[3] == cases[i].phase);
        CHECK(calls[0].regs[0] == calls[0].args[1] && calls[0].regs[1] == BASE + T && calls[0].regs[2] == cases[i].phase);
        CHECK(result == ((uint64_t)0x2222u << 32 | 0x1111u));
    }
    return 0;
}

static int turret(void) {
    memset(arena, 0, sizeof(arena));
    uint8_t *table = at(BASE + 0x800);
    w16(table, 0x100); w16(table + 2, 0x80);           /* k: 1.0 and 0.5 in Q8 */
    w16(table + 4, 5); w16(table + 6, 6);              /* steps */
    w16(table + 8, 0xff00); w16(table + 10, 0x0100);   /* lower limits -256, 256 */
    w16(table + 12, 0x0050); w16(table + 14, 0x2000);  /* upper limits 80, 8192 */
    w16(at(BASE + P), 0x400); w16(at(BASE + P + 4), 0xf000);
    w16(at(BASE + B + 0x20d), 0x0040); w16(at(BASE + B + 0x20f), 0x1000);
    w32(at(BASE + B + 0x1f9), 0x11111111u); w32(at(BASE + B + 0x1fd), 0x22222222u);
    call_count = 0;
    uint64_t result = es2_turret_aim_registers(&memory, &graph, BASE + B, BASE + P);
    static const uint32_t order[9] = {0x7e0a0, 0x7e0a0, 0x67adc, 0x79344, 0x7e0a0, 0x7e0a0, 0x67adc, 0x79344, 0x02604};
    CHECK(call_count == 9);
    for (unsigned i = 0; i < 9; ++i) CHECK(calls[i].function == IMAGE + order[i]);
    /* Axis 0: t = 0x400 >> 3 = 128, 128 * 128 >> 8 = 64, 64 * 1.0 = 64; the
     * angle 0x40 + 64 = 0x80 is clamped to the upper limit 0x50. */
    CHECK(calls[0].args[0] == 128 && calls[0].args[1] == 128 && calls[0].regs[0] == 128 && calls[0].regs[1] == 128);
    CHECK(calls[1].args[0] == 64 && calls[1].args[1] == 0x100);
    CHECK(calls[2].args[0] == BASE + B + 0x209 && calls[2].args[1] == 64 && calls[2].args[2] == 5 && calls[2].regs[2] == BASE + B + 0x209);
    CHECK(r16(at(BASE + B + 0x20d)) == 0x50);
    CHECK(calls[3].args[0] == 0x11111111u && calls[3].args[1] == ((IMAGE + 0x973ecu) & 0xffff0000u) && calls[3].args[2] == 0x50 >> 2);
    CHECK(calls[3].regs[0] == BASE + B + 0x1f9 && calls[3].regs[1] == 0x11111111u);
    /* Axis 1: 0xf000 >> 3 = -512, clamped to -256, negated to 256;
     * 256 * 256 >> 8 = 256, * 0.5 = 128; 0x1000 + 128 = 0x1080 stays inside. */
    CHECK(calls[4].args[0] == 256 && calls[4].args[1] == 256);
    CHECK(calls[5].args[1] == 0x80 && calls[6].args[1] == 128 && calls[6].args[2] == 6);
    CHECK(r16(at(BASE + B + 0x20f)) == 0x1080 && calls[7].args[2] == 0x1080 >> 2 && (uint16_t)calls[7].args[1] == 1);
    CHECK(calls[8].args[0] == BASE + B && calls[8].args[1] == 100 && calls[8].regs[2] == BASE + B);
    CHECK(calls[8].regs[0] == 0x1111u * 8 && calls[8].regs[1] == 0x2222u * 8);
    CHECK(result == ((uint64_t)(0x2222u * 9) << 32 | 0x1111u * 9));
    return 0;
}

static int poses(void) {
    memset(arena, 0, sizeof(arena));
    w32(at(BASE + T), BASE + R); w32(at(BASE + R + 6), BASE + LIST); w32(at(BASE + R + 0x12), BASE + RPOSES);
    w32(at(BASE + LIST), BASE + CLIP);
    w16(at(BASE + CLIP + 0x10), 2); w32(at(BASE + CLIP + 0x12), BASE + MAP); w32(at(BASE + CLIP + 0x16), BASE + INDEX);
    w16(at(BASE + MAP), 1); w16(at(BASE + MAP + 2), 0);
    w16(at(BASE + INDEX), 1); w16(at(BASE + INDEX + 2), 7);
    w16(at(BASE + T + 0x1e), 10); w16(at(BASE + T + 0x1c), 5);
    for (unsigned i = 0; i < 12; ++i) at(BASE + RPOSES + 12)[i] = (uint8_t)(0x40 + i);
    /* Node 1 copies pose 1 (same index for both frames); node 0 is skipped
     * last, leaving the frame words over their addresses' high words. */
    uint64_t result = es2_track_poses_registers(&memory, BASE + T, BASE + POSES, BASE + FLAGS);
    CHECK(!memcmp(at(BASE + POSES + 12), at(BASE + RPOSES + 12), 12) && *at(BASE + FLAGS + 1) == 1 && *at(BASE + FLAGS) == 0);
    CHECK(result == ((uint64_t)(((BASE + INDEX) & 0xffff0000u) | 7) << 32 | (((BASE + INDEX) & 0xffff0000u) | 7)));
    w16(at(BASE + MAP + 2), 1);
    result = es2_track_poses_registers(&memory, BASE + T, BASE + POSES, BASE + FLAGS);
    CHECK(result == ((uint64_t)(BASE + FLAGS) << 32 | 1));
    w16(at(BASE + CLIP + 0x10), 0xffff);
    result = es2_track_poses_registers(&memory, BASE + T, BASE + POSES, BASE + FLAGS);
    CHECK(result == ((uint64_t)(BASE + MAP) << 32));
    /* pose_list without tracks returns track_count's registers. */
    w32(at(BASE + M + 0x1a), 0);
    CHECK(es2_pose_list_registers(&memory, BASE + M) == ((uint64_t)(BASE + M + 0x1a) << 32));
    w32(at(BASE + M + 0x1a), BASE + T); w32(at(BASE + M + 0x1e), 0);
    w32(at(BASE + M + 0x0e), BASE + FLAGS); w32(at(BASE + M + 0x12), BASE + POSES);
    CHECK(es2_pose_list_registers(&memory, BASE + M) == ((uint64_t)(BASE + MAP) << 32));
    return 0;
}

int main(void) {
    if (seek() || turret() || poses()) return 1;
    printf("PASS: track pose scenarios, %u checks\n", checks);
    return 0;
}
