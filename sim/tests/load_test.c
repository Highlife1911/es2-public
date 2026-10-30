/* The mission-load guest callees (sim/src/load.c) on a synthetic image: zeroed
 * host memory with the literals, vtables and objects each case needs written by
 * this test at the original's addresses, the game heap over an emulated OS
 * region, the main loop's call area at its own host address. The sim's real
 * services answer the heap, string, array-constructor, handler-list and load
 * rows; class tags come from this test's recording services. No game data. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "es2/sim/load.h"
#include "es2/sim/input.h"
#include "es2/sim/memory.h"
#include "es2/sim/services.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

#define IMAGE_BASE 0x00400000u
#define IMAGE_SIZE 0x002d8000u
#define HEAP_BASE 0x09000000u
#define HEAP_SIZE 0x00400000u
#define LOCAL_SIZE 0x00080000u
#define COUNTER (IMAGE_BASE + 0x2d7ff0u)
/* Class-tag getters of this test (VAs no row of services.h uses). */
#define TAG_GROUP 0x00401010u
#define TAG_LIST 0x00401014u
#define TAG_POLY 0x00401018u

static uint8_t *image, *heap_bytes, *local;
static uint32_t local_guest;
static es2_sim_arena arena;
static es2_guest_memory memory;
static es2_sim_heap heap;
static es2_sim_services services;
static es2_sim_entry storage[4096];
static es2_sim_dispatch table;
static es2_sim_loop loop;
static es2_sim_load load;
static es2_sim_input input;
static es2_journal journal;
static es2_vt vt;
static es2_sim_refusal last;
static uint32_t first_refused;
static unsigned refusals;
static void on_refuse(const es2_sim_refusal *r, void *user) {
    (void)user;
    if (!refusals) first_refused = r->va;
    last = *r;
    ++refusals;
    if (getenv("LOAD_TEST_VERBOSE")) fprintf(stderr, "refused 0x%08x: %s\n", (unsigned)r->va, r->reason ? r->reason : "");
}

static uint32_t next_heap = HEAP_BASE;
static uint32_t heap_alloc(void *c, uint32_t address, uint32_t size, uint32_t type, uint32_t protect) {
    (void)c; (void)protect;
    if (type & ES2_HEAP_MEM_RESERVE) {
        if (!address) address = next_heap;
        if (address < HEAP_BASE || address + size > HEAP_BASE + HEAP_SIZE) return 0;
        next_heap = address + size;
    }
    return address;
}
static uint32_t os_free(void *c, uint32_t a, uint32_t s, uint32_t t) { (void)c; (void)a; (void)s; (void)t; return 1; }

static uint32_t r32(uint32_t a) { return memory.read32(memory.context, a); }
static uint16_t r16(uint32_t a) { return memory.read16(memory.context, a); }
static uint8_t r8(uint32_t a) { return es2_sim_read8(&memory, a); }
static void w32(uint32_t a, uint32_t v) { memory.write32(memory.context, a, v); }
static void w16(uint32_t a, uint16_t v) { memory.write16(memory.context, a, v); }
static void w8(uint32_t a, uint8_t v) { es2_sim_write8(&memory, a, v); }
static void text(uint32_t a, const char *t) { do w8(a++, (uint8_t)*t); while (*t++); }
static void guest_string(uint32_t a, char *out, unsigned n) { unsigned i = 0; for (; i + 1 < n && r8(a + i); ++i) out[i] = (char)r8(a + i); out[i] = 0; }

static void svc_tag(es2_sim_service_call *c, void *u) {
    (void)u;
    c->eax = c->va == TAG_GROUP ? 0x00140014u : c->va == TAG_LIST ? 0x00140007u : 0x00140001u;
}
/* stand-ins for the shading pass's callees. normalize 0x46c23c (a registry
 * hook, no row) is added before the seal; view.c's rows 0x48c444 and 0x48c16c
 * are replaced after it. */
static unsigned normalized, selected, lit;
static void svc_normalize(es2_sim_service_call *c, void *u) { (void)u; (void)c; ++normalized; }
static void svc_select(es2_sim_service_call *c, void *u) { (void)u; c->eax = 0; ++selected; }
static void svc_light(es2_sim_service_call *c, void *u) { (void)u; c->eax = 0x1200u + (++lit); }
static int replace_row(uint32_t va, es2_sim_service_fn fn) {
    es2_sim_entry *e = (es2_sim_entry *)es2_sim_dispatch_find(&table, va);
    if (!e || e->kind != ES2_SIM_SERVICE) return 1;
    e->service = fn;
    return 0;
}
static int setup_dispatch(void) {
    es2_sim_dispatch_init(&table, storage, 4096, on_refuse, NULL);
    services.memory = &memory;
    services.heap = &heap;
    services.vt = &vt;
    static uint32_t fs0 = 0x00abcdefu;
    services.fs0 = &fs0;
    if (es2_sim_register_services(&table, &services)) return 1;
    static const uint32_t tags[] = {TAG_GROUP, TAG_LIST, TAG_POLY};
    for (unsigned i = 0; i < 3; ++i) if (es2_sim_dispatch_add_service(&table, tags[i], "tag", svc_tag, NULL, 0, 1)) return 1;
    if (es2_sim_dispatch_add_service(&table, 0x0046c23cu, "normalize stand-in", svc_normalize, NULL, 0, 2)) return 1;
    const char *error = NULL;
    uint32_t bad = 0;
    if (es2_sim_dispatch_seal(&table, &error, &bad)) return 1;
    return replace_row(0x0048c444u, svc_select) || replace_row(0x0048c16cu, svc_light);
}
static void fresh(void) {
    memset(image, 0, IMAGE_SIZE);
    w32(0x006cbee4u, COUNTER);
    memset(heap_bytes, 0, HEAP_SIZE);
    memset(local, 0, LOCAL_SIZE);
    next_heap = HEAP_BASE;
    es2_sim_heap_os os = {NULL, heap_alloc, os_free};
    es2_sim_heap_init(&heap, &memory, &os, local_guest + 0x7f000u, 0, 0, 0);
    memset(&vt, 0, sizeof(vt));
    es2_vt_enable(&vt, 1, 47, 0x01000000u, 0, 1);
    es2_sim_loop_defaults(&loop);
    loop.memory = &memory;
    loop.dispatch = &table;
    loop.vt = &vt;
    loop.stack_lo = local_guest;
    loop.stack_hi = local_guest + 0x10000u;
    es2_sim_loop_bind(&loop);
    es2_journal_free(&journal);
    es2_journal_init(&journal);
    es2_sim_input_headless(&input, &loop, &journal, 0, 0);
    es2_sim_input_bind(&input);
    es2_sim_load_headless(&load, &loop);
    es2_sim_load_bind(&load);
    refusals = 0;
}
static int row(uint32_t va, const uint32_t *slots, unsigned n, es2_sim_result *r) {
    uint32_t regs[3] = {0, 0, 0};
    uint32_t at = local_guest + 0x20000u;
    for (unsigned i = 0; i < n; ++i) w32(at + 4u * i, slots[i]);
    memset(r, 0, sizeof(*r));
    return es2_sim_call(&table, va, (const uint32_t *)(uintptr_t)at, n, regs, r);
}
static uint32_t alloc(uint32_t n) {
    es2_sim_result r;
    uint32_t s[1] = {n};
    return row(0x0047749cu, s, 1, &r) ? 0 : r.eax;
}

/* ---- 1. the graphics context 0x461294 */
static int graphics_context(void) {
    fresh();
    uint32_t s = alloc(0xa0u), p = alloc(0x22cu);
    CHECK(s && p);
    w32(0x006c5ed0u, s);
    w32(s + 0x40u, p);
    for (uint32_t i = 0; i < 0x22cu; i += 4u) w32(p + i, 0x1000u + i);
    es2_sim_result r;
    CHECK(row(0x00461294u, NULL, 0, &r) == 0 && refusals == 0);
    uint32_t o = r32(0x004d24ceu);
    CHECK(o && r.ecx == o && r.edx == 0);
    CHECK(r32(o) == 0x004a2e10u && r32(o + 4u) == 0x1000u && r32(o + 4u + 0x200u) == 0x1200u);
    CHECK(r32(o + 4u + 0x220u) == 0x1220u && r32(o + 0x208u) == 1u && r8(o + 0x230u) == 0 && r32(o + 0x231u) == 0);
    CHECK(r32(0x006c5f70u) == o && r32(s + 0x44u) == o + 4u && r32(0x004a3614u) == o + 4u);
    CHECK(r32(COUNTER) == 0);
    return 0;
}

/* ---- 2. the path builders 0x492bec / 0x492b90 */
static int paths(void) {
    fresh();
    uint32_t name = local_guest + 0x30000u, ext = name + 0x40u;
    text(0x004a6939u, ".");
    text(0x004a693bu, "\\");
    text(name, "ld_herc1.old");
    text(ext, "dpl");
    es2_sim_result r;
    uint32_t a[2] = {name, ext};
    CHECK(row(0x00492becu, a, 2, &r) == 0 && r.eax == 0x006cbc2au);
    char out[64];
    guest_string(0x006cbc2au, out, sizeof(out));
    CHECK(!strcmp(out, "dpl\\ld_herc1.dpl"));
    CHECK(row(0x00492b90u, a, 2, &r) == 0 && r.eax == 0x006cbc0cu);
    guest_string(0x006cbc0cu, out, sizeof(out));
    CHECK(!strcmp(out, "ld_herc1.dpl"));
    return 0;
}

/* ---- 3. the palette set 0x481fe0 with the headless display answers */
static int palette(void) {
    fresh();
    uint32_t p = alloc(0x14u), colours = alloc(0x400u), ranges = alloc(0x10u);
    w32(p + 8u, colours);
    w16(p + 0xcu, 2); w16(p + 0xeu, 3); w32(p + 0x10u, ranges);
    for (uint32_t i = 0; i < 0x100u; ++i) {
        w8(colours + 4u * i, (uint8_t)i); w8(colours + 4u * i + 1u, (uint8_t)(i >> 1)); w8(colours + 4u * i + 2u, 7);
        w8(colours + 4u * i + 3u, i == 5u ? 2u : 1u);   /* entry 5: raw bytes (bit 1), the rest scaled (bit 0) */
    }
    w32(ranges, 0x11111111u); w32(ranges + 4u, 0x22222222u); w32(ranges + 8u, 0x33333333u); w32(ranges + 12u, 0x44444444u);
    es2_sim_result r;
    uint32_t a[1] = {p};
    CHECK(row(0x00481fe0u, a, 1, &r) == 0 && refusals == 0);
    CHECK(r32(0x004a4fa5u + 8u * 3u) == 0x11111111u && r32(0x004a4fa5u + 8u * 4u + 4u) == 0x44444444u);
    CHECK(r32(0x004d3b2cu) == p && r16(0x004d3728u) == 0x300u && r16(0x004d372au) == 0x100u);
    /* NUMCOLORS -1: r = 0, no kept system entries; entry i = (2, 1, 0) of the local
     * table, flags 5; the local table = the scaled source (bytes << 2). */
    CHECK(r8(0x004d372cu + 4u * 9u) == (uint8_t)(9u << 2) && r8(0x004d372cu + 4u * 9u + 2u) == (uint8_t)(7u << 2));
    CHECK(r8(0x004d372cu + 4u * 9u + 3u) == 5u && r8(0x004d372cu + 4u * 5u) == 5u);
    CHECK(r32(0x004d302cu) == load.platform.palette_handle && load.platform.color_table_uploads == 1u);
    CHECK(load.platform.color_table[4u * 9u + 2u] == (uint8_t)(9u << 2));
    return 0;
}

/* ---- 4. the arena allocator 0x47a288 */
static int arena_alloc(void) {
    fresh();
    uint32_t rec = local_guest + 0x30000u;
    w32(rec, 0x0a000000u); w32(rec + 4u, 0x100u); w32(rec + 8u, 0xf0u);
    es2_sim_result r;
    uint32_t a[2] = {0x10u, rec};
    CHECK(row(0x0047a288u, a, 2, &r) == 0 && r.eax == 0x0a0000f0u && r32(rec + 8u) == 0x100u);
    CHECK(row(0x0047a288u, a, 2, &r) == 0 && r.eax >= HEAP_BASE && r32(rec + 8u) == 0x100u);   /* full: new[] */
    uint32_t b[2] = {8u, 0};
    CHECK(row(0x0047a2c8u, b, 2, &r) == 0 && r.eax >= HEAP_BASE);
    return 0;
}

/* ---- 5. the texture pages 0x469e14 */
static int textures(void) {
    fresh();
    uint32_t t = alloc(0x808u), b = alloc(0x1au), px = alloc(8u), out = alloc(0x14u), out2 = alloc(0x14u);
    for (uint32_t i = 0; i < 0x100u; ++i) { w32(t + 8u * i, 0); w32(t + 8u * i + 4u, 0x100u); }
    w32(t + 0x804u, 0);
    w16(b + 4u, 2); w16(b + 6u, 3); w32(b + 0xeu, px);
    static const uint8_t pixels[6] = {1, 2, 3, 4, 0, 6};
    for (uint32_t i = 0; i < 6u; ++i) w8(px + i, pixels[i]);
    es2_sim_result r;
    uint32_t a[3] = {t, b, out};
    CHECK(row(0x00469e14u, a, 3, &r) == 0);
    CHECK(r32(out) == 0 && r32(out + 4u) == 0 && r32(out + 8u) == 2u && r32(out + 0xcu) == 1u && r16(out + 0x12u) == 1u);
    CHECK(r8(0x004d4f04u) == 1 && r8(0x004d4f06u) == 3 && r8(0x004d4f04u + 0x100u) == 4 && r8(0x004d4f04u + 0x102u) == 6);
    CHECK(r32(t) == 3u && r32(t + 4u) == 0xfdu && r32(t + 8u) == 3u && r32(t + 0x10u) == 0);
    /* The same bitmap again lands beside it in rows 0 and 1. */
    a[2] = out2;
    CHECK(row(0x00469e14u, a, 3, &r) == 0 && r32(out2) == 3u && r32(out2 + 4u) == 0);
    /* A row too full everywhere: the next page is created (new(0x808)). */
    for (uint32_t i = 0; i < 0x100u; ++i) w32(t + 8u * i + 4u, 2u);
    CHECK(row(0x00469e14u, a, 3, &r) == 0 && r32(t + 0x800u) >= HEAP_BASE);
    CHECK(r32(r32(t + 0x800u) + 0x804u) == 1u && r16(out2 + 0x10u) == 1u && r32(out2) == 0);
    CHECK(r8(0x004d4f04u + 0x10000u) == 1);
    return 0;
}

/* ---- 6. the sound init 0x462410 without a context; with one it stops at
 * the context's constructor (a hook, not in this table) */
static int sound(void) {
    fresh();
    es2_sim_result r;
    uint32_t off[1] = {0};
    CHECK(row(0x00462410u, off, 1, &r) == 0 && refusals == 0);
    CHECK(r16(0x004d2d74u) == 5u && r16(0x004d2d68u) == 0 && r32(0x0049f8ecu) == 0);
    uint32_t v = r32(0x004d2d6cu);
    CHECK(v >= HEAP_BASE && r32(v) == 0xffffffffu && r32(v + 0x42u * 4u) == 0xffffffffu && r8(v + 0x3bu) == 0);
    CHECK(r32(0x004d2d70u) >= HEAP_BASE && r32(0x004d3b30u) == 0x00462398u);
    fresh();
    uint32_t on[1] = {1};
    CHECK(row(0x00462410u, on, 1, &r) != 0 && first_refused == 0x0046379cu && last.va == 0x00462410u);
    return 0;
}

/* ---- 7. the memory stream 0x47b36c with its read, seek and tell */
static int memory_stream(void) {
    fresh();
    uint32_t buf = alloc(0x40u), m = local_guest + 0x30000u, dst = alloc(0x60u);
    for (uint32_t i = 0; i < 0x40u; ++i) w8(buf + i, (uint8_t)(0x80u + i));
    es2_sim_result r;
    uint32_t c[3] = {m, 0x40u, buf};
    CHECK(row(0x0047b36cu, c, 3, &r) == 0 && r32(m) == 0x004a1788u && r32(m + 4u) == 0 && r32(m + 8u) == buf);
    CHECK(r32(m + 0xcu) == buf + 0x40u && r32(m + 0x10u) == buf && r32(COUNTER) == 2u && r.eax == m);
    uint32_t rd[3] = {m, 6u, dst};
    CHECK(row(0x0047b4b4u, rd, 3, &r) == 0 && (r.eax & 0xffu) == 1u && r8(dst) == 0x80u && r8(dst + 5u) == 0x85u && r8(dst + 6u) == 0);
    uint32_t tell[1] = {m};
    CHECK(row(0x0047b400u, tell, 1, &r) == 0 && r.eax == 6u);
    /* A short read copies the rest, leaves the cursor at the end and the
     * status 2 (end of data); the next read answers 0 without copying. */
    rd[1] = 0x40u; rd[2] = dst + 0x10u;
    CHECK(row(0x0047b4b4u, rd, 3, &r) == 0 && r.eax == 0 && r32(m + 4u) == 2u && r32(m + 0x10u) == buf + 0x40u);
    CHECK(r8(dst + 0x10u) == 0x86u && r8(dst + 0x10u + 0x39u) == 0xbfu && r8(dst + 0x10u + 0x3au) == 0);
    rd[1] = 1u; rd[2] = dst + 0x58u;
    CHECK(row(0x0047b4b4u, rd, 3, &r) == 0 && r.eax == 0 && r8(dst + 0x58u) == 0);
    /* Seek clears status 2; outside [0, size]: status 1. */
    uint32_t seek[2] = {m, 0x10u};
    CHECK(row(0x0047b410u, seek, 2, &r) == 0 && (r.eax & 0xffu) == 1u && r32(m + 4u) == 0 && r32(m + 0x10u) == buf + 0x10u);
    CHECK(row(0x0047b400u, tell, 1, &r) == 0 && r.eax == 0x10u);
    seek[1] = 0x50u;
    CHECK(row(0x0047b410u, seek, 2, &r) == 0 && r.eax == 0 && r32(m + 4u) == 1u);
    return 0;
}

/* ---- 8. the mount offset walk 0x40dd4c over a list with one group (class tags from the recording services) */
static int shape_walks(void) {
    fresh();
    uint32_t vt_list = 0x004a1000u, vt_group = 0x004a1020u, vt_poly = 0x004a1040u;
    w32(vt_list, TAG_LIST); w32(vt_group, TAG_GROUP); w32(vt_poly, TAG_POLY);
    uint32_t list = alloc(0x16u), group = alloc(0x28u), poly = alloc(0xeu), children = alloc(4u), items = alloc(4u);
    uint32_t verts = alloc(18u), index = alloc(4u), off = alloc(6u);
    w32(list, vt_list); w16(list + 0x10u, 1); w32(list + 0x12u, children); w32(children, group);
    w32(group, vt_group); w16(group + 0x22u, 3); w16(group + 0x26u, 1); w32(group + 0x1cu, items); w32(group + 0x14u, verts);
    w32(group + 0x10u, index); w32(items, poly);
    w32(poly, vt_poly); w16(poly + 6u, 0); w16(poly + 8u, 1); w16(poly + 0xau, 1);
    w16(index, 9); w16(index + 2u, 2);           /* the polygon uses vertex 0 (+6) and vertex 2 (index[1]) */
    for (uint32_t i = 0; i < 9u; ++i) w16(verts + 2u * i, (uint16_t)(10u * i));
    w16(off, 1); w16(off + 2u, 2); w16(off + 4u, 3);
    es2_sim_result r;
    uint32_t a[2] = {list, off};
    CHECK(row(0x0040dd4cu, a, 2, &r) == 0 && refusals == 0);
    CHECK(r16(verts) == 1u && r16(verts + 2u) == 12u && r16(verts + 4u) == 23u);     /* vertex 0 moved */
    CHECK(r16(verts + 6u) == 30u && r16(verts + 8u) == 40u);                         /* vertex 1 not */
    CHECK(r16(verts + 12u) == 61u && r16(verts + 16u) == 83u);                       /* vertex 2 moved */
    return 0;
}

/* ---- 9. small bodies: a constant, a member address, an element store, the
 * renderer reset */
static int small_bodies(void) {
    fresh();
    es2_sim_result r;
    CHECK(row(0x0048df34u, NULL, 0, &r) == 0 && r.eax == 0x0014000eu);
    uint32_t a[1] = {0x12340000u};
    CHECK(row(0x00406a77u, a, 1, &r) == 0 && r.eax == 0x12340000u && r.edx == 0x1234000au && r.ecx == 0x12340018u);
    uint32_t e = alloc(8u);
    w32(e, 0xffffffffu);
    uint32_t b[1] = {e};
    CHECK(row(0x00411892u, b, 1, &r) == 0 && r32(e) == 0 && r32(COUNTER) == 1u);
    w32(0x004a6488u, 3);
    CHECK(row(0x0048dd08u, NULL, 0, &r) == 0 && r.eax == 0x006c5ffcu && r8(0x006cbc04u) == 1u);
    CHECK(r16(0x006c5ffcu) == 0x4000u && r16(0x006c5ffcu + 0x10u) == 0x4000u && r32(0x006c5ff4u) == 0x006c5ffcu);
    CHECK(r32(0x004a1690u) == 1u && r32(0x006c5834u) == 0x004a63b0u && r32(0x006c5838u) == 3u);
    for (uint32_t i = 0; i < 0x20u; ++i) CHECK(r16(0x006c6068u + 2u * i) == i);   /* the default index list */
    return 0;
}

/* ---- 10. the terrain shading pass on a 2 x 2 grid, the cockpit
 * step, the cursor sprite store, the mono output */
static int load_rest(void) {
    fresh();
    es2_sim_result r;
    uint32_t g = alloc(0x120u), cells = alloc(0x40u);
    CHECK(g && cells);
    w32(g + 0x100u, 1); w32(g + 0x104u, 1); w32(g + 0xecu, cells); w32(g + 0x108u, 4); w32(g + 0x118u, 2);
    /* heights h00 10, h10 4, h01 6, h11 3: d = 13 - 10 > 0 */
    w8(cells, 10); w8(cells + 16u, 4); w8(cells + 32u, 6); w8(cells + 48u, 3);
    w8(cells + 0xfu, 0xffu);
    normalized = selected = lit = 0;
    uint32_t a[1] = {g};
    CHECK(row(0x0046c2e0u, a, 1, &r) == 0 && refusals == 0);
    CHECK(selected == 1u && normalized == 2u && lit == 8u);
    CHECK(r.eax == 2u && r.ecx == 2u && r.edx == 2u);
    CHECK(r8(cells + 0xfu) == 0xfcu);
    CHECK(r16(cells + 1u) == 12u && r16(cells + 3u) == 8u && r16(cells + 5u) == 16u);      /* (6, 4, 8) << 1 */
    CHECK(r16(cells + 7u) == 6u && r16(cells + 9u) == 2u && r16(cells + 0xbu) == 16u);     /* (3, 1, 8) << 1 */
    CHECK(r8(cells + 0xdu) == 1u && r8(cells + 0xeu) == 2u);
    CHECK(r16(cells + 16u + 1u) == 0 && r16(cells + 16u + 5u) == 0x800u && r16(cells + 16u + 0xbu) == 0x800u);
    CHECK(r8(cells + 16u + 0xdu) == 3u && r8(cells + 48u + 0xeu) == 8u);
    w32(g + 0x104u, 31);                                   /* 1 << 31 < 0: no row */
    CHECK(row(0x0046c2e0u, a, 1, &r) == 0 && r.eax == 0x80000000u && r.ecx == 0 && r.edx == 0);
    w32(g + 0x104u, 1); w32(g + 0x118u, 0);                /* the z division faults in the original */
    CHECK(row(0x0046c2e0u, a, 1, &r) != 0 && load.refused_va == 0x0046c208u);
    /* flat corners and 16 idiv 32 = 0: the zero normal faults in normalize */
    w32(g + 0x118u, 32);
    w8(cells, 5); w8(cells + 16u, 5); w8(cells + 32u, 5); w8(cells + 48u, 5);
    normalized = 0;
    load.refused_va = 0;
    CHECK(row(0x0046c2e0u, a, 1, &r) != 0 && load.refused_va == 0x0046c29eu && normalized == 0);
    /* the cockpit step */
    fresh();
    uint32_t h = alloc(0x240u), p = alloc(0x10u);
    w32(0x0049b088u, h); w32(h + 0x236u, p);
    w32(h + 0x226u, h + 0x232u);
    CHECK(row(0x00433f7cu, NULL, 0, &r) == 0 && r32(h + 0x22au) == h + 0x236u && r.eax == h && r.edx == h + 0x236u);
    w32(h + 0x226u, 0); w32(p + 4u, 0x5000u); w16(0x004d2566u, 1);
    CHECK(row(0x00433f7cu, NULL, 0, &r) == 0 && r32(h + 0x226u) == h + 0x236u && r.eax == h && r.edx == p && r.ecx == 0x5004u);
    w32(h + 0x226u, 0); w16(0x004d2566u, 0);
    CHECK(row(0x00433f7cu, NULL, 0, &r) == 0 && r.eax == 0x5004u && r.edx == p + 8u && r.ecx == 0x5004u);
    /* the cursor sprite, the mono output */
    uint32_t s[1] = {0x12345678u};
    CHECK(row(0x00465618u, s, 1, &r) == 0 && r32(0x004d3724u) == 0x12345678u && r.eax == 0x12345678u);
    w32(0x006cbc50u, 0xffffffffu);
    CHECK(row(0x004955c4u, NULL, 0, &r) == 0 && r.eax == 0 && r.ecx == 0x006cbc50u);
    w32(0x006cbc50u, 0x44u);
    CHECK(row(0x004955c4u, NULL, 0, &r) != 0 && load.refused_va == 0x00495741u);
    return 0;
}

int main(void) {
    image = calloc(1, IMAGE_SIZE);
    heap_bytes = calloc(1, HEAP_SIZE);
    for (uint32_t a = 0x30000000u; !local && a < 0x38000000u; a += 0x01000000u)
        local = VirtualAlloc((void *)(uintptr_t)a, LOCAL_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    CHECK(image && heap_bytes && local);
    local_guest = (uint32_t)(uintptr_t)local;
    CHECK(es2_sim_arena_add(&arena, IMAGE_BASE, image, IMAGE_SIZE));
    CHECK(es2_sim_arena_add(&arena, HEAP_BASE, heap_bytes, HEAP_SIZE));
    CHECK(es2_sim_arena_add(&arena, local_guest, local, LOCAL_SIZE));
    es2_sim_arena_memory(&arena, &memory);
    fresh();
    CHECK(setup_dispatch() == 0);
    if (graphics_context() || paths() || palette() || arena_alloc() || textures() || sound() || memory_stream() || shape_walks() ||
        small_bodies() || load_rest())
        return 1;
    puts("PASS: load: graphics context, path builders, palette set, arena allocator, texture pages, sound init, memory "
         "stream, the mount offset walk, small bodies, the terrain shading pass and the root's last callees");
    return 0;
}
