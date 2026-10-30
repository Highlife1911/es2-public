#include "es2/core/animation.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Synthetic guest arena at guest address 0x00020000; synthetic tables. */
#define BASE 0x00020000u
static uint8_t arena[1024];
static unsigned checks, advance_calls;
static uint32_t advance_manager, advance_step;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"animation line %d: %s\n",__LINE__,#x);return 1;}}while(0)
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

enum { ACTOR = 0x000, MANAGER = 0x100, MATRIX = 0x140, TRACK = 0x180, INPUT = 0x200 };
static uint8_t prepared[32];
static void advance(void *context, uint32_t manager, uint32_t step) {
    (void)context; ++advance_calls; advance_manager = manager; advance_step = step;
    memcpy(at(r32(at(manager + 0x16))), prepared, 32);
}

int main(void) {
    uint8_t quarter[2050], large[16386], small[898], correction[513];
    for (unsigned i = 0; i <= 1024; ++i) w16(quarter + 2 * i, 16384 - 16 * i);
    for (unsigned i = 0; i <= 8192; ++i) w16(large + 2 * i, i);
    for (unsigned i = 0; i <= 448; ++i) w16(small + 2 * i, 3 * i);
    for (unsigned i = 0; i <= 512; ++i) correction[i] = (uint8_t)(i % 17);
    const es2_math_tables tables = {quarter, large, small, correction};

    /* manager_reset without a track: EAX=0, EDX unchanged, no write. */
    memset(arena, 0xcd, sizeof(arena));
    w32(at(BASE + MANAGER + 0x16), BASE + MATRIX); w32(at(BASE + MANAGER + 0x1a), 0);
    uint8_t before[sizeof(arena)]; memcpy(before, arena, sizeof(arena));
    uint64_t r = es2_manager_reset_registers(&memory, BASE + MANAGER, at(BASE + INPUT), quarter, 0, 0x12345678u);
    CHECK(r == (uint64_t)0x12345678u << 32 && !memcmp(arena, before, sizeof(arena)));

    /* With a disabled track: the track matrix and the animation matrix both
     * receive the input; EAX = matrix+0x14, EDX = matrix. */
    w32(at(BASE + MANAGER + 0x1a), BASE + TRACK); w16(at(BASE + TRACK + 0x20), 0);
    for (unsigned i = 0; i < 32; ++i) at(BASE + INPUT)[i] = (uint8_t)(7 * i + 1);
    r = es2_manager_reset_registers(&memory, BASE + MANAGER, at(BASE + INPUT), quarter, 0, 0);
    CHECK((uint32_t)r == BASE + MATRIX + 0x14 && (uint32_t)(r >> 32) == BASE + MATRIX);
    CHECK(!memcmp(at(BASE + MATRIX), at(BASE + INPUT), 32) && !memcmp(at(BASE + TRACK + 0x2e), at(BASE + INPUT), 32));

    /* animation_step with an identity animation (mode 0, zero translation)
     * leaves position and angles, rebuilds and then clears the cache flag,
     * resets the animation matrix and passes (manager, step) to the advance. */
    memset(arena, 0xcd, sizeof(arena));
    w32(at(BASE + MANAGER + 0x16), BASE + MATRIX); w32(at(BASE + MANAGER + 0x1a), 0);
    memset(prepared, 0, 32); w16(prepared, 0x4000); w16(prepared + 6, 0x4000); w16(prepared + 16, 0x4000);
    uint8_t *a = at(BASE + ACTOR);
    w16(a + 0x0c, 0); w16(a + 0x0e, 0); w16(a + 0x10, 0x1234); w16(a + 0x32, 0);
    w32(a + 0x26, 1000); w32(a + 0x2a, (uint32_t)-2000); w32(a + 0x2e, 3000);
    r = es2_animation_step_registers(&memory, BASE + ACTOR, BASE + MANAGER, 0xbeef0007u, &tables, advance, NULL, 0);
    CHECK((uint32_t)r == BASE + MATRIX && (uint32_t)(r >> 32) == 0x20);
    CHECK(advance_calls == 1 && advance_manager == BASE + MANAGER && advance_step == 0xbeef0007u);
    CHECK(r32(a + 0x26) == 1000 && r32(a + 0x2a) == (uint32_t)-2000 && r32(a + 0x2e) == 3000);
    CHECK(r16(a + 0x0c) == 0 && r16(a + 0x0e) == 0 && r16(a + 0x10) == 0x1234 && r16(a + 0x32) == 0);
    CHECK(a[0x12 + 18] == 1); /* Euler ran: only the third angle set gives mode 1 (was 0xcd). */
    uint8_t identity[32] = {0}; w16(identity, 0x4000); w16(identity + 6, 0x4000); w16(identity + 16, 0x4000);
    CHECK(!memcmp(at(BASE + MATRIX), identity, 32));

    /* A pure translation in the animation moves the actor by the actor's
     * rotation applied to it; with zero actor angles that is the identity. */
    w16(a + 0x10, 0);
    w32(prepared + 20, 60); w32(prepared + 24, (uint32_t)-70); w32(prepared + 28, 80);
    r = es2_animation_step_registers(&memory, BASE + ACTOR, BASE + MANAGER, 1, &tables, advance, NULL, 0);
    CHECK(r32(a + 0x26) == 1060 && r32(a + 0x2a) == (uint32_t)-2070 && r32(a + 0x2e) == 3080);

    /* The clip methods by address (clip C with count word C+0Ah). */
    {
        const uint32_t C = BASE + 0x300, e[3] = {0xaaaa1111u, 0xbbbb2222u, 0xcccc3333u};
        uint32_t ecx, args[3];
        w16(at(C + 0x0a), 5);
        args[0] = 0x77770003u; args[1] = 0; args[2] = 0;
        r = es2_clip_method_registers(&memory, 0x478760u, C, args, e, &ecx);   /* 3 < 4: +1 */
        CHECK(r == ((uint64_t)4 << 32 | 0x77770004u) && ecx == 3);
        args[0] = 0x77770004u;
        r = es2_clip_method_registers(&memory, 0x478760u, C, args, e, &ecx);   /* at the end: stays */
        CHECK(r == ((uint64_t)4 << 32 | 0x77770004u) && ecx == 4);
        args[0] = 0x0000ffffu;                                                /* -1 < 4 */
        r = es2_clip_method_registers(&memory, 0x478760u, C, args, e, &ecx);
        CHECK(r == ((uint64_t)4 << 32 | 0x00010000u) && ecx == 0xffffffffu);
        r = es2_clip_method_registers(&memory, 0x4787e4u, C, args, e, &ecx);
        CHECK(r == ((uint64_t)0x0000ffffu << 32 | 0x00010000u) && ecx == 0xffffffffu);
        args[0] = 0x12340004u;                                                /* cyclic wraps to 0 */
        r = es2_clip_method_registers(&memory, 0x4787e4u, C, args, e, &ecx);
        CHECK(r == ((uint64_t)0x12340004u << 32) && ecx == 4);
        args[0] = 0x12340000u;
        r = es2_clip_method_registers(&memory, 0x47877cu, C, args, e, &ecx);
        CHECK(r == ((uint64_t)e[1] << 32 | 0x12340000u) && ecx == e[2]);
        r = es2_clip_method_registers(&memory, 0x478804u, C, args, e, &ecx);
        CHECK(r == ((uint64_t)0x12340004u << 32 | 0x12340004u) && ecx == e[2]);
        w16(at(C + 0x0a), 0);                                                 /* n = 0 borrows */
        r = es2_clip_method_registers(&memory, 0x478804u, C, args, e, &ecx);
        CHECK(r == ((uint64_t)0x1233ffffu << 32 | 0x1233ffffu));
        args[0] = 0x00010001u;
        r = es2_clip_method_registers(&memory, 0x47877cu, C, args, e, &ecx);
        CHECK((uint32_t)r == 0x00010000u);
        r = es2_clip_method_registers(&memory, 0x478804u, C, args, e, &ecx);
        CHECK(r == ((uint64_t)0x00010001u << 32 | 0x00010000u));
        /* reached: b beyond a, equal with c = 0, equal with c != 0, short of it */
        args[0] = 0x55550003u; args[1] = 0x66660004u; args[2] = 0x99990000u;
        r = es2_clip_method_registers(&memory, 0x47878cu, C, args, e, &ecx);
        CHECK(r == ((uint64_t)0x66660004u << 32 | 0x55550001u) && ecx == e[2]);
        r = es2_clip_method_registers(&memory, 0x4787b8u, C, args, e, &ecx);
        CHECK(r == ((uint64_t)0x66660004u << 32));
        args[1] = 0x00000003u;
        r = es2_clip_method_registers(&memory, 0x4787b8u, C, args, e, &ecx);
        CHECK(r == ((uint64_t)3 << 32 | 0x55550001u));
        args[2] = 1;
        r = es2_clip_method_registers(&memory, 0x47878cu, C, args, e, &ecx);
        CHECK(r == ((uint64_t)3 << 32));
        args[1] = 0x8000u;                                                    /* signed: -32768 < 3 */
        r = es2_clip_method_registers(&memory, 0x4787b8u, C, args, e, &ecx);
        CHECK((uint32_t)r == 0x55550001u);
        r = es2_clip_method_registers(&memory, 0x478820u, C, args, e, &ecx);
        CHECK(r == ((uint64_t)e[1] << 32 | 0xaaaa0001u) && ecx == e[2]);
        r = es2_clip_method_registers(&memory, 0x47882cu, C, args, e, &ecx);
        CHECK(r == ((uint64_t)e[1] << 32 | 0xaaaa0001u));
    }

    printf("animation: %u checks passed\n", checks);
    return 0;
}
