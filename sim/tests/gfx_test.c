/* The graphics library's primitives and the scan converter (sim/src/gfx.c) on a
 * synthetic image: zeroed host memory at the original's addresses with the
 * globals the primitives read (the view, the context pointer, the driver table,
 * the ramp table) written by this test, a 64 x 48 8-bit surface with its row
 * table in an identity-mapped call area. Pixels are checked as counts of
 * changed bytes and FNV hashes against expectations derived from the contract
 * (rasterizer.md, "Graphics library primitives");
 * no pixel data is stored. The comparison with the original's machine code is
 * //tools/reference-game/invitro:differential_gfx_test. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "es2/sim/gfx.h"
#include "es2/sim/loop.h"
#include "es2/sim/memory.h"
#include "es2/sim/services.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

#define IMAGE_BASE 0x00400000u
#define IMAGE_SIZE 0x002d0000u
#define LOCAL_SIZE 0x00040000u   /* call area 0..0x10000, the world 0x10000.. */
#define W 64
#define H 48
/* World offsets (from local_guest + 0x10000). */
#define O_ROWS 0x000u
#define O_BITS 0x1000u
#define O_CONTEXT 0x2000u
#define O_ENTRY 0x2300u
#define O_SPANS 0x2400u
#define O_PEN 0x2800u
#define O_FILL 0x2900u
#define O_RECT 0x2a00u
#define O_POINT 0x2a40u
#define O_OUT 0x2b00u
#define O_HEADER 0x2c00u
#define O_PIXELS 0x2d00u
#define O_FONT 0x3000u
#define O_WIDTHS 0x3100u
#define O_OFFSETS 0x3200u
#define O_GLYPHS 0x3800u
#define O_TEXT 0x4000u
#define O_POLY 0x4100u
#define O_SCAN 0x4200u
#define O_GC 0x5000u
#define O_SAVE 0x5400u
#define O_SURFACE 0x5c00u

static uint8_t *image, *local;
static uint32_t local_guest, world;
static es2_sim_arena arena;
static es2_guest_memory memory;
static es2_sim_heap heap;
static es2_sim_services services;
static es2_sim_entry storage[4096];
static es2_sim_dispatch table;
static es2_sim_loop loop;
static es2_vt vt;
static es2_sim_refusal last;
static unsigned refusals;
static void on_refuse(const es2_sim_refusal *r, void *user) { (void)user; last = *r; ++refusals; }

static uint32_t r32(uint32_t a) { return memory.read32(memory.context, a); }
static uint8_t r8(uint32_t a) { return es2_sim_read8(&memory, a); }
static void w32(uint32_t a, uint32_t v) { memory.write32(memory.context, a, v); }
static void w16(uint32_t a, uint16_t v) { memory.write16(memory.context, a, v); }
static void w8(uint32_t a, uint8_t v) { es2_sim_write8(&memory, a, v); }
static uint32_t at(uint32_t off) { return world + off; }
static uint8_t *host(uint32_t off) { return local + 0x10000u + off; }
static void rect(uint32_t off, int32_t l, int32_t t, int32_t r, int32_t b) { w32(at(off), (uint32_t)l); w32(at(off) + 4u, (uint32_t)t); w32(at(off) + 8u, (uint32_t)r); w32(at(off) + 12u, (uint32_t)b); }
static void point(uint32_t off, int32_t x, int32_t y) { w32(at(off), (uint32_t)x); w32(at(off) + 4u, (uint32_t)y); }
static uint8_t *pixel(int32_t x, int32_t y) { return host(O_BITS) + y * W + x; }

/* ---- recording service for a driver slot the own code does not reconstruct (and the line) */
static unsigned line_calls, other_calls;
static uint32_t line_args[8][4];   /* the two points' values at the call (the own code reuses its locals) */
static void svc_line(es2_sim_service_call *c, void *u) {
    (void)u;
    if (line_calls < 8) for (unsigned i = 0; i < 4; ++i) line_args[line_calls][i] = r32(c->slots[i / 2] + 4u * (i & 1u));
    ++line_calls;
}
static void svc_other(es2_sim_service_call *c, void *u) { (void)u; (void)c; ++other_calls; }

static int setup_dispatch(void) {
    es2_sim_dispatch_init(&table, storage, 4096, on_refuse, NULL);
    services.memory = &memory;
    services.heap = &heap;
    services.vt = &vt;
    services.fs0 = NULL;
    if (es2_sim_register_services(&table, &services)) return 1;
    /* The line 0x483a04 has its own row; the recording stand-in replaces it after the seal. */
    int line_row = 0;
    for (unsigned i = 0; i < es2_sim_service_count(); ++i) {
        uint32_t row = 0;
        es2_sim_service_row(i, &row);
        if (row == 0x00483a04u) line_row = 1;
    }
    if (!line_row && es2_sim_dispatch_add_service(&table, 0x00483a04u, "line", svc_line, NULL, 0, 2)) return 1;
    if (es2_sim_dispatch_add_service(&table, 0x00412000u, "other slot", svc_other, NULL, 0, 1)) return 1;
    const char *error = NULL;
    uint32_t bad = 0;
    if (es2_sim_dispatch_seal(&table, &error, &bad)) return 1;
    if (line_row) {
        es2_sim_entry *e = (es2_sim_entry *)es2_sim_dispatch_find(&table, 0x00483a04u);
        if (!e || e->kind != ES2_SIM_SERVICE) return 1;
        e->service = svc_line;
        e->service_user = NULL;
    }
    return 0;
}

/* The driver table copy (the BMP_8 bodies, as 0x4a586c holds them). */
static const uint32_t slots[36] = {
    0x489f84, 0x48a016, 0x48a6c4, 0x48a74c, 0x48a340, 0x48a361, 0x48a384, 0x48a324, 0x48a1cc, 0x489830, 0x489836,
    0x48a1d4, 0x48a32c, 0x48978c, 0x4897cb, 0x48990e, 0x4898fc, 0x489902, 0x489908, 0x48a08a, 0x48a7a7, 0x48a440,
    0x48a48e, 0x48a4d4, 0x48a5b3, 0x48a601, 0x4899ac, 0x489c6e, 0x489ede, 0x48afc8, 0x4898d4, 0x4898df, 0x4898f6,
    0x48a924, 0x48a3b0, 0x48a8e2};

static void fresh(void) {
    memset(image, 0, IMAGE_SIZE);
    memset(local, 0, LOCAL_SIZE);
    w32(ES2_SIM_VA_GFX_CONTEXT, at(O_CONTEXT));
    w32(ES2_SIM_VA_GFX_VIEW_ROWS, at(O_ROWS));
    w32(ES2_SIM_VA_GFX_VIEW_ROWS + 4u, W);
    w32(ES2_SIM_VA_GFX_VIEW_ROWS + 8u, H);
    w32(ES2_SIM_VA_GFX_VIEW_FORMAT, 8u);
    w32(ES2_SIM_VA_GFX_VIEW_PITCH, W);
    w32(ES2_SIM_VA_GFX_VIEW_HEIGHT, H);
    w32(ES2_SIM_VA_GFX_SPANS_A, 0x004a3699u);
    w32(ES2_SIM_VA_GFX_SPANS_B, 0x004a431du);
    w32(ES2_SIM_VA_GFX_SURFACE, at(O_SURFACE));
    w32(ES2_SIM_VA_GFX_GC, at(O_GC));
    for (unsigned i = 0; i < 36u; ++i) w32(ES2_SIM_VA_GFX_SLOTS + 4u * i, slots[i]);
    w32(at(O_ROWS), at(O_BITS));
    for (int32_t y = 0; y < H + 8; ++y) w32(at(O_ROWS) + 4u + 4u * (uint32_t)y, (uint32_t)(y * W));
    /* Mode 0, clip rect 8..55 x 8..39, origin flag off, pen and fill kind 0. */
    rect(O_CONTEXT + 0x20cu, 8, 8, 55, 39);
    point(O_CONTEXT + 0x21cu, 8, 8);
    w32(at(O_CONTEXT) + 0x224u, at(O_PEN));
    w32(at(O_CONTEXT) + 0x228u, at(O_FILL));
    w32(at(O_PEN) + 4u, 0x11u);
    w32(at(O_FILL) + 4u, 0x22u);
    memset(&vt, 0, sizeof(vt));
    es2_vt_enable(&vt, 1, 47, 0x01000000u, 0, 1);
    es2_sim_loop_defaults(&loop);
    loop.memory = &memory;
    loop.dispatch = &table;
    loop.vt = &vt;
    loop.stack_lo = local_guest;
    loop.stack_hi = local_guest + 0x10000u;
    es2_sim_loop_bind(&loop);
    refusals = line_calls = other_calls = 0;
}

/* Changed bytes of the surface against a value, and an FNV-1a hash. */
static unsigned count_not(uint8_t v) { unsigned n = 0; for (unsigned i = 0; i < W * H; ++i) n += host(O_BITS)[i] != v; return n; }
static uint32_t hash_bits(void) { uint32_t h = 2166136261u; for (unsigned i = 0; i < W * H; ++i) { h ^= host(O_BITS)[i]; h *= 16777619u; } return h; }

/* ---- 1. clip helpers */
static int clip(void) {
    fresh();
    uint32_t eax = 1;
    rect(O_RECT, 2, 3, 20, 30);
    rect(O_RECT + 0x10u, 10, -5, 40, 12);
    CHECK(es2_sim_gfx_rect_clip(&loop, at(O_RECT), at(O_RECT + 0x10u), at(O_OUT), &eax) == 0 && eax == at(O_OUT));
    CHECK(r32(at(O_OUT)) == 10 && r32(at(O_OUT) + 4u) == 3 && r32(at(O_OUT) + 8u) == 20 && r32(at(O_OUT) + 12u) == 12);
    rect(O_RECT + 0x10u, 21, 0, 40, 12);
    CHECK(es2_sim_gfx_rect_clip(&loop, at(O_RECT), at(O_RECT + 0x10u), at(O_OUT), &eax) == 0 && eax == 0);
    /* The rect as spans: header {3, 28}, 28 pairs {2, 20}. */
    CHECK(es2_sim_gfx_rect_to_spans(&loop, at(O_RECT), at(O_OUT), &eax) == 0 && eax == at(O_OUT));
    CHECK(r32(at(O_OUT)) == (28u << 16 | 3u) && r32(at(O_OUT) + 4u) == (20u << 16 | 2u) && r32(at(O_OUT) + 4u + 27u * 4u) == (20u << 16 | 2u));
    rect(O_RECT + 0x20u, 0, 10, 5, 8);
    CHECK(es2_sim_gfx_rect_to_spans(&loop, at(O_RECT + 0x20u), at(O_OUT), &eax) == 1 && refusals == 1 && last.va == 0x004869fcu);
    /* Two span lists: a at rows 5..8 (x 0..9), b at rows 7..12 with row 8
     * disjoint and row 7 overlapping: out = {7, 1, {5, 9}}. */
    w16(at(O_SPANS), 5); w16(at(O_SPANS) + 2u, 4);
    for (unsigned i = 0; i < 4; ++i) w32(at(O_SPANS) + 4u + 4u * i, 9u << 16);
    w16(at(O_SPANS + 0x100u), 7); w16(at(O_SPANS + 0x100u) + 2u, 6);
    w32(at(O_SPANS + 0x100u) + 4u, 30u << 16 | 5u);
    w32(at(O_SPANS + 0x100u) + 8u, 30u << 16 | 20u);
    CHECK(es2_sim_gfx_spans_clip_spans(&loop, at(O_SPANS), at(O_SPANS + 0x100u), at(O_OUT), &eax) == 0 && eax == at(O_OUT));
    CHECK(r32(at(O_OUT)) == (1u << 16 | 7u) && r32(at(O_OUT) + 4u) == (9u << 16 | 5u));
    /* A rect over the list a: rows 6..20, x 3..7 -> {6, 3, {3,7} x 3}. */
    rect(O_RECT, 3, 6, 7, 20);
    CHECK(es2_sim_gfx_rect_clip_spans(&loop, at(O_RECT), at(O_SPANS), at(O_OUT), &eax) == 0 && eax == at(O_OUT));
    CHECK(r32(at(O_OUT)) == (3u << 16 | 6u) && r32(at(O_OUT) + 12u) == (7u << 16 | 3u));
    rect(O_RECT, 30, 6, 37, 20);
    CHECK(es2_sim_gfx_rect_clip_spans(&loop, at(O_RECT), at(O_SPANS), at(O_OUT), &eax) == 0 && eax == 0);
    return 0;
}

/* ---- 2. fills, the point, the ellipse */
static int fills(void) {
    fresh();
    /* Mode 0: the rect as given, 10 x 5 bytes of 0x22. */
    rect(O_RECT, 2, 3, 11, 7);
    CHECK(es2_sim_gfx_rect_fill(&loop, at(O_RECT)) == 0 && count_not(0) == 50 && *pixel(2, 3) == 0x22 && *pixel(11, 7) == 0x22 && *pixel(12, 7) == 0);
    /* Mode 1: clipped to 8..55 x 8..39: columns 8..11 of rows 8..12 = 20. */
    fresh();
    w32(at(O_CONTEXT) + 0x204u, 1);
    rect(O_RECT, 2, 3, 11, 12);
    CHECK(es2_sim_gfx_rect_fill(&loop, at(O_RECT)) == 0 && count_not(0) == 20 && *pixel(8, 8) == 0x22 && *pixel(7, 8) == 0);
    /* The origin flag shifts by (l, t) - origin: origin (6, 5) -> (+2, +3). */
    fresh();
    w32(at(O_CONTEXT) + 0x204u, 1);
    w32(at(O_CONTEXT) + 0x208u, 1);
    point(O_CONTEXT + 0x21cu, 6, 5);
    rect(O_RECT, 10, 10, 11, 11);
    CHECK(es2_sim_gfx_rect_fill(&loop, at(O_RECT)) == 0 && count_not(0) == 4 && *pixel(12, 13) == 0x22 && *pixel(10, 10) == 0);
    /* Mode 2 with a kind-2 region of three rows {10..12} x 20..29 and the
     * clip rect: 30 pixels. */
    fresh();
    w32(at(O_CONTEXT) + 0x204u, 2);
    w32(at(O_CONTEXT), 1);
    w32(at(O_CONTEXT) + 4u, at(O_ENTRY));
    w32(at(O_ENTRY), 2);
    w32(at(O_ENTRY) + 4u, at(O_SPANS));
    w16(at(O_SPANS), 10); w16(at(O_SPANS) + 2u, 3);
    for (unsigned i = 0; i < 3; ++i) w32(at(O_SPANS) + 4u + 4u * i, 29u << 16 | 20u);
    rect(O_RECT, 0, 0, 63, 47);
    CHECK(es2_sim_gfx_rect_fill(&loop, at(O_RECT)) == 0 && count_not(0) == 30 && *pixel(20, 10) == 0x22 && *pixel(29, 12) == 0x22);
    uint32_t region_hash = hash_bits();
    /* A fill colour with the high byte 4 reads the ramp table: entry 1 ->
     * table at O_OFFSETS (int16), scale 0x80: index = byte 2 & 0xfe. */
    fresh();
    w32(ES2_SIM_VA_GFX_RAMPS + 8u, at(O_OFFSETS));
    w16(ES2_SIM_VA_GFX_RAMPS + 12u, 0x80);
    w16(at(O_OFFSETS) + 6u, 0x77);
    w32(at(O_FILL) + 4u, 0x04060001u);
    rect(O_RECT, 0, 0, 1, 0);
    CHECK(es2_sim_gfx_rect_fill(&loop, at(O_RECT)) == 0 && count_not(0) == 2 && *pixel(0, 0) == 0x77);
    /* Fill kind 4: the outline through the line (four calls), nothing drawn here. */
    fresh();
    w32(at(O_FILL), 4);
    rect(O_RECT, 5, 6, 9, 10);
    CHECK(es2_sim_gfx_rect_fill(&loop, at(O_RECT)) == 0 && line_calls == 4 && count_not(0) == 0);
    CHECK(line_args[0][0] == 9 && line_args[0][1] == 6 && line_args[0][2] == 5 && line_args[0][3] == 6);   /* (r, t) -> (l, t) */
    CHECK(line_args[1][0] == 5 && line_args[1][1] == 10 && line_args[1][2] == 5 && line_args[1][3] == 6);  /* (l, b) -> (l, t) */
    CHECK(line_args[2][0] == 5 && line_args[2][1] == 10 && line_args[2][2] == 9 && line_args[2][3] == 10); /* (l, b) -> (r, b) */
    CHECK(line_args[3][0] == 9 && line_args[3][1] == 6 && line_args[3][2] == 9 && line_args[3][3] == 10);  /* (r, t) -> (r, b) */
    CHECK(r32(at(O_FILL)) == 4u);
    /* Fill kind 6 through the byte table: 0 -> 0x33. */
    fresh();
    w32(at(O_FILL), 6);
    w8(at(O_FILL) + 4u, 0x33);
    rect(O_RECT, 0, 0, 3, 0);
    CHECK(es2_sim_gfx_rect_fill(&loop, at(O_RECT)) == 0 && count_not(0) == 4 && *pixel(3, 0) == 0x33);
    /* The span fill: the list above shifted by the point (1, 1). */
    fresh();
    w16(at(O_SPANS), 10); w16(at(O_SPANS) + 2u, 3);
    for (unsigned i = 0; i < 3; ++i) w32(at(O_SPANS) + 4u + 4u * i, 29u << 16 | 20u);
    point(O_POINT, 1, 1);
    CHECK(es2_sim_gfx_span_fill(&loop, at(O_SPANS), at(O_POINT)) == 0 && count_not(0) == 30 && *pixel(21, 11) == 0x22 && *pixel(20, 10) == 0);
    CHECK(r32(at(O_SPANS)) == (3u << 16 | 10u));   /* the caller's list is untouched (copied into buffer A) */
    /* The point: pen colour 0x11 at (3, 4) in mode 0; outside the clip rect in mode 1 nothing. */
    fresh();
    point(O_POINT, 3, 4);
    CHECK(es2_sim_gfx_point(&loop, at(O_POINT)) == 0 && count_not(0) == 1 && *pixel(3, 4) == 0x11);
    w32(at(O_CONTEXT) + 0x204u, 1);
    point(O_POINT, 3, 5);
    CHECK(es2_sim_gfx_point(&loop, at(O_POINT)) == 0 && count_not(0) == 1);
    /* The pixel read slot. */
    uint32_t eax = 0, args[1] = {at(O_POINT)};
    point(O_POINT, 3, 4);
    CHECK(es2_sim_gfx_slot(&loop, 4u, args, 1, &eax) == 0 && eax == 0x11u);
    /* A slot whose table entry is not the BMP_8 body is dispatched. */
    w32(ES2_SIM_VA_GFX_SLOTS + 16u, 0x00412000u);
    CHECK(es2_sim_gfx_slot(&loop, 4u, args, 1, &eax) == 0 && other_calls == 1);
    /* The ellipse in 21 x 15 (half axes 10, 7): the rows cy - b .. cy + b
     * - 1 (14 rows; the row list's header word is -b, its count 2b), each
     * a solid span symmetric about cx, row y mirrored in row 2b - 1 - y;
     * the top row is 7 wide, the widest rows the full 21; a zero half
     * axis is refused. */
    fresh();
    rect(O_RECT, 10, 10, 30, 24);
    CHECK(es2_sim_gfx_ellipse(&loop, at(O_RECT)) == 0 && refusals == 0);
    unsigned filled = count_not(0);
    CHECK(filled > 14 * 21 / 2 && filled < 14 * 21);
    for (int32_t y = 0; y < 14; ++y) {
        int row = 0;
        for (int32_t x = 0; x < 21; ++x) {
            uint8_t a = *pixel(10 + x, 10 + y);
            row += a != 0;
            CHECK(a == *pixel(30 - x, 10 + y) && a == *pixel(10 + x, 23 - y));
        }
        CHECK(row > 0);
    }
    for (int32_t x = 0; x < 25; ++x) CHECK(*pixel(8 + x, 24) == 0 && *pixel(8 + x, 9) == 0);
    CHECK(*pixel(20, 10) == 0x22 && *pixel(16, 10) == 0 && *pixel(10, 16) == 0x22 && *pixel(30, 17) == 0x22 && *pixel(10, 10) == 0);
    rect(O_RECT, 10, 10, 11, 24);
    CHECK(es2_sim_gfx_ellipse(&loop, at(O_RECT)) == 1 && refusals == 1 && last.va == 0x0048817cu);
    /* Kind 4: the two arcs only, fewer pixels than the solid one; the fill kind restored. */
    fresh();
    w32(at(O_FILL), 4);
    rect(O_RECT, 10, 10, 30, 24);
    CHECK(es2_sim_gfx_ellipse(&loop, at(O_RECT)) == 0 && refusals == 0 && count_not(0) < filled && count_not(0) >= 30 && r32(at(O_FILL)) == 4u);
    (void)region_hash;
    return 0;
}

/* ---- 3. bitmaps: draw, grab, save/size/restore, scroll */
static void bitmap(int32_t w, int32_t h, uint8_t flags) {
    w16(at(O_HEADER), (uint16_t)h); w16(at(O_HEADER) + 2u, (uint16_t)w);
    w8(at(O_HEADER) + 4u, 8); w8(at(O_HEADER) + 5u, flags);
    w32(at(O_HEADER) + 6u, (uint32_t)(w * h));
    w32(at(O_HEADER) + 0xau, at(O_PIXELS));
    for (int32_t i = 0; i < w * h; ++i) w8(at(O_PIXELS) + (uint32_t)i, (uint8_t)(i % 3 ? 0x40 + i : 0));
}
static int bitmaps(void) {
    fresh();
    bitmap(4, 3, 0x10);
    point(O_POINT, 5, 6);
    /* Opaque: all 12 bytes at (5, 6) as is. */
    CHECK(es2_sim_gfx_bitmap_draw(&loop, at(O_HEADER), at(O_POINT), 0) == 0 && count_not(0) == 8 && *pixel(6, 6) == 0x41 && *pixel(8, 8) == 0x4b && *pixel(5, 6) == 0);
    uint32_t forward = hash_bits();
    /* Flipped vertically (placement 1): row 0 of the bitmap at the bottom. */
    memset(host(O_BITS), 0, W * H);
    CHECK(es2_sim_gfx_bitmap_draw(&loop, at(O_HEADER), at(O_POINT), 1) == 0 && count_not(0) == 8 && *pixel(6, 8) == 0x41 && *pixel(8, 6) == 0x4b);
    /* Placement 2: each row reversed (mirrored horizontally); 3: rotated by 180 degrees (the last byte first). */
    memset(host(O_BITS), 0, W * H);
    CHECK(es2_sim_gfx_bitmap_draw(&loop, at(O_HEADER), at(O_POINT), 2) == 0 && count_not(0) == 8 && *pixel(5, 8) == 0x4b && *pixel(7, 6) == 0x41 && *pixel(6, 6) == 0x42);
    memset(host(O_BITS), 0, W * H);
    CHECK(es2_sim_gfx_bitmap_draw(&loop, at(O_HEADER), at(O_POINT), 3) == 0 && count_not(0) == 8 && *pixel(5, 6) == 0x4b && *pixel(7, 8) == 0x41 && *pixel(6, 6) == 0x4a);
    /* Transparent: the zero bytes leave the surface (set to 0xee first). */
    fresh();
    memset(host(O_BITS), 0xee, W * H);
    bitmap(4, 3, 0);
    point(O_POINT, 5, 6);
    CHECK(es2_sim_gfx_bitmap_draw(&loop, at(O_HEADER), at(O_POINT), 0) == 0 && count_not(0xee) == 8 && *pixel(5, 6) == 0xee && *pixel(6, 6) == 0x41);
    /* Mode 1 clips to the clip rect: at (6, 6) only column 8 and rows 8 remain: (8, 8) = 0x4b (pixel 10 of the bitmap, index 10 % 3 != 0). */
    fresh();
    w32(at(O_CONTEXT) + 0x204u, 1);
    bitmap(4, 3, 0x10);
    point(O_POINT, 6, 6);
    CHECK(es2_sim_gfx_bitmap_draw(&loop, at(O_HEADER), at(O_POINT), 0) == 0 && count_not(0) == 2 && *pixel(8, 8) == 0x4a && *pixel(9, 8) == 0x4b);
    /* Off the surface: nothing, no refusal. */
    fresh();
    bitmap(4, 3, 0x10);
    point(O_POINT, 70, 6);
    CHECK(es2_sim_gfx_bitmap_draw(&loop, at(O_HEADER), at(O_POINT), 0) == 0 && count_not(0) == 0 && refusals == 0);
    /* The GLBitmap object (header at +4): compressed is refused, else drawn as 0x4887d8. */
    fresh();
    bitmap(4, 3, 0x10);
    point(O_POINT, 5, 6);
    memcpy(host(O_HEADER + 0x44u), host(O_HEADER), 0xeu);
    CHECK(es2_sim_gfx_bitmap_blit(&loop, at(O_HEADER + 0x40u), at(O_POINT), 0) == 0 && hash_bits() == forward);
    w16(at(O_HEADER + 0x40u) + 0x12u, 1);
    CHECK(es2_sim_gfx_bitmap_blit(&loop, at(O_HEADER + 0x40u), at(O_POINT), 0) == 1 && refusals == 1 && last.va == 0x004816a8u);
    /* The grab: the surface under the bitmap into its pixels (the header's
     * RLE nibble cleared), then drawing it back reproduces the surface. */
    fresh();
    for (unsigned i = 0; i < W * H; ++i) host(O_BITS)[i] = (uint8_t)(i * 7u + 1u);
    uint32_t before = hash_bits();
    bitmap(4, 3, 0x11);
    point(O_POINT, 5, 6);
    CHECK(es2_sim_gfx_bitmap_grab(&loop, at(O_HEADER), at(O_POINT), 0) == 0 && r8(at(O_HEADER) + 5u) == 0x10 && r8(at(O_PIXELS)) == *pixel(5, 6) && r8(at(O_PIXELS) + 11u) == *pixel(8, 8));
    memset(host(O_BITS), 0, W * H);
    for (unsigned i = 0; i < W * H; ++i) host(O_BITS)[i] = (uint8_t)(i * 7u + 1u);
    memset(pixel(5, 6), 0, 4); memset(pixel(5, 7), 0, 4); memset(pixel(5, 8), 0, 4);
    CHECK(es2_sim_gfx_bitmap_draw(&loop, at(O_HEADER), at(O_POINT), 0) == 0 && hash_bits() == before);
    /* Save, size and restore with a kind-0 record 10..13 x 20..21 (8 bytes). */
    fresh();
    for (unsigned i = 0; i < W * H; ++i) host(O_BITS)[i] = (uint8_t)(i * 7u + 1u);
    before = hash_bits();
    uint32_t eax = 0;
    w32(at(O_RECT), 0);
    rect(O_RECT + 4u, 10, 20, 13, 21);
    CHECK(es2_sim_gfx_size(&loop, at(O_RECT), &eax) == 0 && eax == 8u);
    CHECK(es2_sim_gfx_save(&loop, at(O_RECT), at(O_SAVE)) == 0 && r8(at(O_SAVE)) == *pixel(10, 20) && r8(at(O_SAVE) + 7u) == *pixel(13, 21));
    memset(pixel(10, 20), 0, 4); memset(pixel(10, 21), 0, 4);
    point(O_POINT, 10, 20);
    CHECK(es2_sim_gfx_restore(&loop, at(O_RECT), at(O_POINT), at(O_SAVE)) == 0 && hash_bits() == before);
    w32(at(O_RECT), 2);
    CHECK(es2_sim_gfx_save(&loop, at(O_RECT), at(O_SAVE)) == 1 && refusals == 1);
    /* Scroll the kind-0 rect 10..19 x 10..14 (the source) by (2, 1): the
     * destination 12..21 x 11..15 takes it, (12, 11) (10, 10)'s byte and
     * (21, 15) (19, 14)'s; (9, 9) outside stays. */
    fresh();
    for (unsigned i = 0; i < W * H; ++i) host(O_BITS)[i] = (uint8_t)(i * 7u + 1u);
    uint8_t a = *pixel(10, 10), b = *pixel(19, 14), c = *pixel(9, 9);
    w32(at(O_RECT), 0);
    rect(O_RECT + 4u, 10, 10, 19, 14);
    point(O_POINT, 2, 1);
    CHECK(es2_sim_gfx_scroll(&loop, at(O_RECT), at(O_POINT)) == 0 && *pixel(12, 11) == a && *pixel(19, 14) != b && *pixel(9, 9) == c && *pixel(21, 15) == b);
    /* And back by (-2, -1): the moved bytes return. */
    point(O_POINT, -2, -1);
    CHECK(es2_sim_gfx_scroll(&loop, at(O_RECT), at(O_POINT)) == 0 && *pixel(10, 10) == a);
    /* A kind-2 record (+4 points at the list): rows 20..21 x 0..9 by (1, 0). */
    w32(at(O_RECT), 2);
    w32(at(O_RECT) + 4u, at(O_SPANS));
    w16(at(O_SPANS), 20); w16(at(O_SPANS) + 2u, 2);
    w32(at(O_SPANS) + 4u, 9u << 16); w32(at(O_SPANS) + 8u, 9u << 16);
    a = *pixel(0, 20);
    point(O_POINT, 1, 0);
    CHECK(es2_sim_gfx_scroll(&loop, at(O_RECT), at(O_POINT)) == 0 && *pixel(1, 20) == a && refusals == 0);
    return 0;
}

/* ---- 4. text */
/* A font: first char 0x20, height 5, baseline 4, per-glyph widths 2 + ch
 * % 4 (or the fixed width), glyph ch at 32 * (ch - 0x20); 'A' (0x41,
 * width 3) holds the bytes 1..15; the text "ABA". */
static void font(int32_t fixed) {
    uint32_t f = at(O_FONT);
    w16(f + 8u, 0x20); w16(f + 0xau, 5); w16(f + 0xcu, (uint16_t)fixed); w8(f + 0x12u, 8); w16(f + 0x16u, 4);
    w32(f + 0x22u, at(O_GLYPHS)); w32(f + 0x26u, at(O_OFFSETS)); w32(f + 0x2au, at(O_WIDTHS));
    for (uint32_t ch = 0; ch < 0x60u; ++ch) { w8(at(O_WIDTHS) + ch, (uint8_t)(2u + ch % 4u)); w32(at(O_OFFSETS) + 4u * ch, 32u * ch); }
    for (uint32_t i = 0; i < 15u; ++i) w8(at(O_GLYPHS) + 32u * 0x21u + i, (uint8_t)(i + 1u));
    w8(at(O_TEXT), 'A'); w8(at(O_TEXT) + 1u, 'B'); w8(at(O_TEXT) + 2u, 'A'); w8(at(O_TEXT) + 3u, 0);
    point(O_POINT, 10, 20);
}
static int text(void) {
    fresh();
    font(-1);
    uint32_t f = at(O_FONT), eax = 0;
    CHECK(es2_sim_gfx_char_width(&loop, f, 'A', &eax) == 0 && eax == 3u);
    CHECK(es2_sim_gfx_char_width(&loop, f, 0x1200u | 'B', &eax) == 0 && eax == 4u);
    CHECK(es2_sim_gfx_font_height(&loop, f, &eax) == 0 && eax == 5u);
    CHECK(es2_sim_gfx_string_width(&loop, f, at(O_TEXT), &eax) == 0 && eax == 10u);
    /* The glyph at (10, 20): baseline 4 -> rows 16..20, 15 bytes. */
    CHECK(es2_sim_gfx_char(&loop, f, at(O_POINT), 'A') == 0 && count_not(0) == 15 && *pixel(10, 16) == 1 && *pixel(12, 20) == 15);
    CHECK(line_calls == 0);
    /* The GC's underline: the line thunk 0x482d44 from (10, 20) to (13, 20). */
    fresh(); font(-1);
    w8(at(O_GC) + 0x230u, 1);
    CHECK(es2_sim_gfx_char(&loop, f, at(O_POINT), 'A') == 0 && refusals == 0 && count_not(0) == 15 && line_calls == 1);
    CHECK(line_args[0][0] == 10u && line_args[0][1] == 20u && line_args[0][2] == 13u && line_args[0][3] == 20u);
    /* The string: A at 10, B (a blank glyph) at 13, A at 17: 30 bytes. */
    fresh(); font(-1);
    CHECK(es2_sim_gfx_string(&loop, f, at(O_POINT), at(O_TEXT)) == 0 && count_not(0) == 30 && *pixel(17, 16) == 1);
    /* Right-aligned to a GC width of 20: start at 10 + 20 - 10 = 20. */
    fresh(); font(-1);
    w32(at(O_GC) + 0x231u, 1); w32(at(O_GC) + 0x235u, 20);
    CHECK(es2_sim_gfx_string(&loop, f, at(O_POINT), at(O_TEXT)) == 0 && count_not(0) == 30 && *pixel(20, 16) == 1 && *pixel(10, 16) == 0);
    /* Centred: (20 - 10) >> 1 = 5 -> start 15. */
    fresh(); font(-1);
    w32(at(O_GC) + 0x231u, 2); w32(at(O_GC) + 0x235u, 20);
    CHECK(es2_sim_gfx_string(&loop, f, at(O_POINT), at(O_TEXT)) == 0 && *pixel(15, 16) == 1);
    /* A fixed width 6; without a GC the string is refused. */
    fresh(); font(6);
    w32(ES2_SIM_VA_GFX_GC, 0);
    CHECK(es2_sim_gfx_char_width(&loop, f, 'A', &eax) == 0 && eax == 6u);
    CHECK(es2_sim_gfx_string_width(&loop, f, at(O_TEXT), &eax) == 0 && eax == 18u);
    CHECK(es2_sim_gfx_string(&loop, f, at(O_POINT), at(O_TEXT)) == 1 && refusals == 1 && last.va == 0x00482634u);
    return 0;
}

/* ---- 5. the scan converter */
static int scan(void) {
    fresh();
    uint32_t p = at(O_POLY), out = at(O_SCAN), eax = 0;
    /* A rectangle 10..20 x 5..8: 4 rows, left 10, right 20. */
    point(O_POLY, 10, 5); point(O_POLY + 8u, 20, 5); point(O_POLY + 16u, 20, 8); point(O_POLY + 24u, 10, 8);
    CHECK(es2_sim_gfx_scan_convert(&loop, 4, p, out, &eax) == 0 && eax == 4u && r32(out) == 5u && r32(out + 4u) == 4u);
    for (unsigned r = 0; r < 4; ++r) CHECK(r32(out + 8u + 8u * r) == 10u && r32(out + 12u + 8u * r) == 20u);
    /* A triangle (10, 0), (14, 4), (6, 4): row y spans 10 - y .. 10 + y. */
    point(O_POLY, 10, 0); point(O_POLY + 8u, 14, 4); point(O_POLY + 16u, 6, 4);
    CHECK(es2_sim_gfx_scan_convert(&loop, 3, p, out, &eax) == 0 && eax == 5u && r32(out) == 0);
    for (unsigned r = 0; r < 5; ++r) CHECK(r32(out + 8u + 8u * r) == 10u - r && r32(out + 12u + 8u * r) == 10u + r);
    /* The same through 0x4930c1 (the sign of the area picks the order) and 0x493141 (reversed). */
    uint32_t eax2 = 0;
    CHECK(es2_sim_gfx_scan(&loop, 3, p, out, &eax2) == 0 && eax2 == eax && r32(out + 8u + 32u) == 6u);
    CHECK(es2_sim_gfx_scan_reversed(&loop, 3, p, out, &eax2) == 0 && r32(out + 4u) == 5u);
    /* A flat polygon: the topmost vertex is the one with the greater x (9),
     * the bottommost the other: one row {7, 1, 3, 9}, EAX = 9. */
    point(O_POLY, 3, 7); point(O_POLY + 8u, 9, 7);
    CHECK(es2_sim_gfx_scan_convert(&loop, 2, p, out, &eax) == 0 && eax == 9u && r32(out + 4u) == 1u && r32(out + 8u) == 3u && r32(out + 12u) == 9u);
    /* Too many rows is refused. */
    point(O_POLY, 0, 0); point(O_POLY + 8u, 5, 500); point(O_POLY + 16u, 0, 500);
    CHECK(es2_sim_gfx_scan_convert(&loop, 3, p, out, &eax) == 1 && refusals == 1 && last.va == 0x00493192u);
    return 0;
}

int main(void) {
    image = calloc(1, IMAGE_SIZE);
    for (uint32_t a = 0x30000000u; !local && a < 0x38000000u; a += 0x01000000u)
        local = VirtualAlloc((void *)(uintptr_t)a, LOCAL_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    CHECK(image && local);
    local_guest = (uint32_t)(uintptr_t)local;
    world = local_guest + 0x10000u;
    CHECK(es2_sim_arena_add(&arena, IMAGE_BASE, image, IMAGE_SIZE));
    CHECK(es2_sim_arena_add(&arena, local_guest, local, LOCAL_SIZE));   /* identity: slots are guest-addressable */
    es2_sim_arena_memory(&arena, &memory);
    fresh();
    CHECK(setup_dispatch() == 0);
    if (clip() || fills() || bitmaps() || text() || scan()) return 1;
    puts("PASS: gfx: the clip helpers, rect/span/point fills in modes 0-2 with regions and the origin shift, ramp and table "
         "colours, the outline through the line, the ellipse, bitmap draw in four placements, transparent and clipped, the "
         "grab, save/size/restore, scroll, glyph widths and strings with alignment, the scan converter and its refusals");
    return 0;
}
