#include "es2/core/animation.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Hand-built scenarios for 0x478d38 in a synthetic guest arena. The exact
 * equivalence with the original is the in-vitro harness's job; these checks
 * document the state machine and run on every core platform. */
#define BASE 0x00030000u
#define SEQUENCE 0x00001000u
#define CYCLIC 0x00002000u
#define OTHER 0x00003000u
static uint8_t arena[4096];
static unsigned checks, other_calls;
static uint32_t other_slot;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"track advance line %d: %s\n",__LINE__,#x);return 1;}}while(0)
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
static uint64_t other(void *c, uint32_t clip, uint32_t slot, const uint32_t args[3]) {
    (void)c; (void)clip; ++other_calls; other_slot = slot; return (uint16_t)(args[0] + 7u);
}
static const es2_clip_classes classes = {SEQUENCE, CYCLIC, other, NULL};

/* Layout: manager, shape, hierarchy, flags, node poses/matrices, resource,
 * clip pointer array, two clips with three frames of duration 10, tracks. */
enum { M = 0x000, SHAPE = 0x060, HIER = 0x0a0, ENTRIES = 0x0b0, FLAGS = 0x0c0, POSES = 0x0d0,
       MATS = 0x100, RES = 0x180, CLIPS = 0x1a8, CLIP0 = 0x1c0, CLIP1 = 0x1e0, DUR = 0x200,
       MAP = 0x220, INDEX = 0x230, RPOSES = 0x240, T0 = 0x300, T1 = 0x360 };

static void clip(uint32_t at_offset, uint32_t vtable, uint16_t priority) {
    uint8_t *c = at(BASE + at_offset);
    w32(c, vtable); w16(c + 6, priority); w16(c + 8, 1); w16(c + 0x0a, 3);
    w32(c + 0x0c, BASE + DUR); w16(c + 0x10, 0); w32(c + 0x12, BASE + MAP); w32(c + 0x16, BASE + INDEX);
}

static void track(uint32_t offset, uint16_t frame, uint16_t next, uint16_t time) {
    uint8_t *t = at(BASE + offset);
    memset(t, 0, 0x52);
    w32(t, BASE + RES); w16(t + 4, 0); w16(t + 6, frame); w16(t + 8, 0); w16(t + 0x0a, next);
    w16(t + 0x0c, 0xffff); w16(t + 0x16, 9); w16(t + 0x1c, time); w16(t + 0x1e, 10);
    w16(t + 0x4e, 256);
}

static void setup(void) {
    memset(arena, 0xcd, sizeof(arena));
    uint8_t *m = at(BASE + M);
    w32(m + 4, BASE + SHAPE); w32(at(BASE + SHAPE + 0x2a), BASE + HIER);
    w16(at(BASE + HIER + 0x0a), 0); w32(at(BASE + HIER + 0x0c), BASE + ENTRIES);
    w16(m + 0x0c, 2); w32(m + 0x0e, BASE + FLAGS); w32(m + 0x12, BASE + POSES); w32(m + 0x16, BASE + MATS);
    w32(m + 0x1a, BASE + T0); w32(m + 0x1e, 0);
    w32(at(BASE + RES + 6), BASE + CLIPS); w32(at(BASE + RES + 0x12), BASE + RPOSES);
    w32(at(BASE + CLIPS), BASE + CLIP0); w32(at(BASE + CLIPS + 4), BASE + CLIP1);
    clip(CLIP0, CYCLIC, 0); clip(CLIP1, SEQUENCE, 0);
    for (unsigned f = 0; f < 3; ++f) w16(at(BASE + DUR + 6 * f), 10);
    for (unsigned i = 0; i < 3; ++i) w16(at(BASE + INDEX + 2 * i), 0);
    memset(at(BASE + RPOSES), 0, 12);
    memset(at(BASE + MATS), 0, 64);
}

int main(void) {
    uint8_t quarter[2050];
    for (unsigned i = 0; i <= 1024; ++i) w16(quarter + 2 * i, 16384 - 16 * i);

    /* Frame steps of both clip classes and the fallback. */
    setup();
    CHECK(es2_clip_step(&memory, &classes, BASE + CLIP0, 1, 1) == 2);
    CHECK(es2_clip_step(&memory, &classes, BASE + CLIP0, 1, 2) == 0);
    CHECK(es2_clip_step(&memory, &classes, BASE + CLIP0, 0, 0) == 2);
    CHECK(es2_clip_step(&memory, &classes, BASE + CLIP1, 1, 2) == 2);
    CHECK(es2_clip_step(&memory, &classes, BASE + CLIP1, 0, 0) == 0);
    CHECK(es2_clip_step(&memory, &classes, BASE + CLIP1, 0, 2) == 1);
    w32(at(BASE + CLIP1), OTHER);
    CHECK(es2_clip_step(&memory, &classes, BASE + CLIP1, 0, 5) == 12 && other_calls == 1 && other_slot == 0x24);
    CHECK(es2_clip_step(&memory, &classes, BASE + CLIP1, 1, 5) == 12 && other_calls == 2 && other_slot == 0x20);

    /* Forward over two frame boundaries of the cyclic clip: time 0 + 25 at
     * rate 1.0 passes frames 1 and 2 (duration 10 each), 5 units remain. */
    setup(); track(T0, 0, 1, 0);
    uint64_t r = es2_track_advance_registers(&memory, BASE + M, 0xffff0000u | 25u, &classes, quarter, 0);
    uint8_t *t = at(BASE + T0);
    CHECK(r16(t + 6) == 2 && r16(t + 0x0a) == 0 && r16(t + 0x1c) == 5 && r16(t + 0x1e) == 10);
    CHECK(r16(t + 0x20) == 1 && !(r16(t + 0x50) & 3));
    CHECK((uint32_t)r == BASE + FLAGS && (uint32_t)(r >> 32) == 2 && !at(BASE + FLAGS)[0] && !at(BASE + FLAGS)[1]);

    /* Backward: time 3 - 8 crosses into the previous frame. */
    setup(); track(T0, 2, 0, 3);
    (void)es2_track_advance_registers(&memory, BASE + M, (uint16_t)-8, &classes, quarter, 0);
    CHECK(r16(t + 6) == 1 && r16(t + 0x0a) == 2 && r16(t + 0x1c) == 5);

    /* A sequence clip stops at its first frame when stepping backward. */
    setup(); track(T0, 0, 1, 2); w16(at(BASE + T0 + 4), 1);
    (void)es2_track_advance_registers(&memory, BASE + M, (uint16_t)-8, &classes, quarter, 0);
    CHECK(r16(t + 6) == 0 && r16(t + 0x1c) == 0);

    /* Reaching the stop clip/frame sets the stopped bit; with stop flag 1 the
     * remaining time is dropped and later steps do nothing. */
    setup(); track(T0, 0, 1, 0); w16(t + 0x16, 0); w16(t + 0x18, 1); t[0x1a] = 1;
    (void)es2_track_advance_registers(&memory, BASE + M, 25, &classes, quarter, 0);
    CHECK(r16(t + 6) == 1 && r16(t + 0x1c) == 0 && (r16(t + 0x50) & 1));
    (void)es2_track_advance_registers(&memory, BASE + M, 25, &classes, quarter, 0);
    CHECK(r16(t + 6) == 1 && r16(t + 0x1c) == 0);

    /* Tracks are sorted ascending by their clip's priority word. */
    setup(); track(T0, 0, 1, 0); track(T1, 0, 1, 0);
    w16(at(BASE + T1 + 4), 1); w16(at(BASE + T1 + 0x0a), 1);
    w16(at(BASE + CLIP0 + 6), 3); w16(at(BASE + CLIP1 + 6), (uint16_t)-1);
    w32(at(BASE + M + 0x1e), BASE + T1); w32(at(BASE + M + 0x22), 0);
    (void)es2_track_advance_registers(&memory, BASE + M, 0, &classes, quarter, 0);
    CHECK(r32(at(BASE + M + 0x1a)) == BASE + T1 && r32(at(BASE + M + 0x1e)) == BASE + T0);

    /* Hierarchy: a changed root is rebuilt from its pose and its child is
     * rebuilt because the parent was; all flags end cleared. */
    setup(); w32(at(BASE + M + 0x1a), 0);
    w16(at(BASE + HIER + 0x0a), 2);
    w16(at(BASE + ENTRIES), 0xffff); w16(at(BASE + ENTRIES + 2), 0);
    w16(at(BASE + ENTRIES + 4), 0); w16(at(BASE + ENTRIES + 6), 1);
    at(BASE + FLAGS)[0] = 1; at(BASE + FLAGS)[1] = 0;
    memset(at(BASE + POSES), 0, 24); w32(at(BASE + POSES + 6), 1000); w32(at(BASE + POSES + 12 + 6), 50);
    r = es2_track_advance_registers(&memory, BASE + M, 0, &classes, quarter, 0);
    CHECK(r16(at(BASE + MATS)) == 0x4000 && r32(at(BASE + MATS + 20)) == 1000);
    CHECK(r32(at(BASE + MATS + 32 + 20)) == 1050 && !at(BASE + FLAGS)[0] && !at(BASE + FLAGS)[1]);
    CHECK((uint32_t)(r >> 32) == 2);

    /* Register results of the clip methods (slot, args, EAX/EDX at the call). */
    setup();
    const uint32_t frame2[3] = {0xabcd0002u, 0, 0}, frame0[3] = {0xabcd0000u, 0, 0};
    CHECK(es2_clip_call(&memory, &classes, BASE + CLIP1, 0x20, frame2, 0, 0) == ((uint64_t)2 << 32 | 0xabcd0002u));
    CHECK(es2_clip_call(&memory, &classes, BASE + CLIP0, 0x20, frame2, 0, 0) == ((uint64_t)0xabcd0002u << 32));
    CHECK(es2_clip_call(&memory, &classes, BASE + CLIP1, 0x24, frame0, 0, 0x77u) == ((uint64_t)0x77u << 32 | 0xabcd0000u));
    CHECK(es2_clip_call(&memory, &classes, BASE + CLIP0, 0x24, frame0, 0, 0) == ((uint64_t)0xabcd0002u << 32 | 0xabcd0002u));
    const uint32_t ahead[3] = {0x11110001u, 0x22220002u, 0}, same[3] = {0x11110002u, 0x22220002u, 5};
    CHECK(es2_clip_call(&memory, &classes, BASE + CLIP1, 0x28, ahead, 0, 0) == ((uint64_t)0x22220002u << 32 | 0x11110001u));
    CHECK(es2_clip_call(&memory, &classes, BASE + CLIP1, 0x28, same, 0, 0) == (uint64_t)0x22220002u << 32);
    CHECK(es2_clip_call(&memory, &classes, BASE + CLIP0, 0x2c, same, 0x99990000u, 0x55u) == ((uint64_t)0x55u << 32 | 0x99990001u));

    /* Target 0xffff stops the track: bit 0 set, trigger cleared, EAX = T,
     * EDX keeps its entry high word with the flags word. */
    setup(); track(T0, 1, 2, 3);
    r = es2_track_target_registers(&memory, &classes, BASE + T0, 0xffff, 0, 0x1234, 0x56780000u);
    CHECK(r == ((uint64_t)0x56781234u << 32 | (BASE + T0)) && (r16(t + 0x50) & 1) && r16(t + 0x0c) == 0xffff);

    /* Route: frame 2 of clip 0 has one transition (index 0) to clip 1 frame
     * 1 with duration 9 and pose 4; the target frame is any (0xffff). */
    setup(); track(T0, 1, 2, 3);
    uint8_t *transitions = at(BASE + 0x2c0);
    w32(at(BASE + RES + 0x1e), BASE + 0x2c0);
    w16(transitions, 9); w16(transitions + 2, 1); w16(transitions + 4, 1); w16(transitions + 6, 4);
    for (unsigned f = 0; f < 3; ++f) { w16(at(BASE + DUR + 6 * f + 2), f == 2); w16(at(BASE + DUR + 6 * f + 4), 0); }
    r = es2_track_target_registers(&memory, &classes, BASE + T0, 1, 0xffff, 0, 0);
    CHECK(r16(t + 0x0c) == 2 && r16(t + 0x0e) == 1 && r16(t + 0x10) == 1 && r16(t + 0x12) == 9 && r16(t + 0x14) == 4);
    CHECK(!(r16(t + 0x50) & 1));

    /* Snapshot then rollback restores actor matrix, track states and node
     * arrays; EAX = destination of the last copy, EDX = 32 * nodes. */
    setup(); track(T0, 1, 2, 3);
    uint8_t *actor = at(BASE + 0xd00);
    memset(actor, 0x11, 0x40); w32(actor + 0x34, BASE + M); w16(actor + 0x32, 1);
    for (unsigned i = 1; i < 10; ++i) w32(at(BASE + M + 0x1a + 4 * i), 0);
    static uint8_t saved_track[0x52];
    memcpy(saved_track, at(BASE + T0), 0x52);
    r = es2_actor_snapshot_registers(&memory, BASE + 0xd00, BASE + 0x400, quarter);
    CHECK(r == ((uint64_t)64 << 32 | (BASE + 0x400 + 0x4da)));
    CHECK(!memcmp(at(BASE + 0x420), saved_track, 0x52));
    memset(at(BASE + T0), 0, 0x52); memset(actor + 0x12, 0, 32);
    r = es2_actor_rollback_registers(&memory, BASE + 0xd00, BASE + 0x400, quarter);
    CHECK(r == ((uint64_t)64 << 32 | (BASE + MATS)));
    CHECK(!memcmp(at(BASE + T0), saved_track, 0x52) && actor[0x12] == 0x11 && actor[0x31] == 0x11);

    /* The standalone parts and their register results (guest addresses). */
    setup(); track(T0, 0, 1, 0);
    CHECK(es2_track_sort_registers(&memory, BASE + M) == ((uint64_t)(BASE + M + 0x1e) << 32 | 1u));
    track(T1, 0, 1, 0); w16(at(BASE + T1 + 4), 1); w16(at(BASE + CLIP1 + 6), (uint16_t)-1);
    w32(at(BASE + M + 0x1e), BASE + T1); w32(at(BASE + M + 0x22), 0);
    /* After the swap the last track is T0 (clip 0, priority 0): EAX = high
     * word of the clip pointer : priority, EDX = its clip index. */
    r = es2_track_sort_registers(&memory, BASE + M);
    CHECK(r == ((BASE + CLIP0) & 0xffff0000u) && r32(at(BASE + M + 0x1a)) == BASE + T1);
    /* A stopped track returns the step slot and the entry EDX. */
    setup(); track(T0, 0, 1, 0); w16(t + 0x50, 1); t[0x1a] = 1;
    r = es2_track_step_registers(&memory, &classes, BASE + T0, 0x12340019u, 0x9999u, quarter, 0);
    CHECK(r == ((uint64_t)0x9999u << 32 | 0x12340019u) && r16(t + 6) == 0);
    /* Rate 0: EAX = EDX = 0 (the shifted product and its sign). */
    w16(t + 0x50, 0); w16(t + 0x4e, 0);
    CHECK(es2_track_step_registers(&memory, &classes, BASE + T0, 25, 0x9999u, quarter, 0) == 0);
    /* Two frames forward as above: the last clip_pose leaves EAX = T and
     * EDX = 3 * pose index 0. */
    w16(t + 0x4e, 256);
    r = es2_track_step_registers(&memory, &classes, BASE + T0, 25, 0x9999u, quarter, 0);
    CHECK(r == BASE + T0 && r16(t + 6) == 2 && r16(t + 0x1c) == 5);
    /* No track: EAX = 0 and the sort's EDX = M+0x1a; the node pass returns
     * the memset registers. */
    setup(); w32(at(BASE + M + 0x1a), 0);
    CHECK(es2_track_list_registers(&memory, BASE + M, 25, &classes, quarter, 0) == (uint64_t)(BASE + M + 0x1a) << 32);
    CHECK(es2_node_matrices_registers(&memory, BASE + M, quarter, 0) == ((uint64_t)2 << 32 | (BASE + FLAGS)));

    printf("track advance: %u checks, 0 failures\n", checks);
    return 0;
}
