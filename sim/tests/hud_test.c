/* The HUD line's coupling calls (es2/sim/hud.h) on a synthetic image, checked against the
 * coupling table (hud.md, "the HUD line contract — coupling set and frame writes", the site table
 * and the per-frame order) with a hook for the measured table.
 *
 * Zeroed host memory with the objects each case needs written by this test
 * at the original's addresses, the game heap over an emulated OS region,
 * the main loop's call area at its own host address; the sim's real
 * services answer the heap, string and clock rows (virtual time); the
 * hooks the cases reach (sfx_play, button_press, camera_current,
 * dist_octagonal, sincos_pair, rotate_2d, panel_visible, hud_speed) are recording services of this
 * test at the hooks' VAs (the table holds no hooks). No game data.
 *
 * With ES2_T58_TABLE naming a text file of "site kind" lines (kind C, B or A), every traced site
 * must be listed there with the same kind; without it that comparison is reported as skipped. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "es2/sim/hud.h"
#include "es2/sim/load.h"
#include "es2/sim/memory.h"
#include "es2/sim/services.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

#define IMAGE_BASE 0x00400000u
#define IMAGE_SIZE 0x002d8000u
#define HEAP_BASE 0x09000000u
#define HEAP_SIZE 0x00400000u
#define LOCAL_SIZE 0x00080000u
#define COUNTER (IMAGE_BASE + 0x2d7ff0u)

/* Hooks the cases reach, as recording services. */
#define VA_SFX_PLAY 0x00462830u
#define VA_BUTTON_PRESS 0x00438d9cu
#define VA_CAMERA_CURRENT 0x0045edf8u
#define VA_DIST_OCTAGONAL 0x0047de4cu
#define VA_SINCOS 0x0047eb30u
#define VA_ROTATE 0x0047ffe8u
#define VA_VISIBLE 0x00431410u
#define VA_HUD_SPEED 0x0041bb3cu
/* The per-frame walk's callees without own rows (recorded). */
#define VA_CONDITION 0x004152ecu
#define VA_JOLT_STEP 0x0042d4a8u
#define VA_DISTANCE 0x004928d0u
#define VA_PORT_POST 0x00436a3cu
#define VA_RNG_NEXT 0x00492ee0u     /* a registry adapter in the own process */

static uint8_t *image, *heap_bytes, *local;
static uint32_t local_guest;
static es2_sim_arena arena;
static es2_guest_memory memory;
static es2_sim_heap heap;
static es2_sim_services services;
static es2_sim_entry storage[4096];
static es2_sim_dispatch table;
static es2_sim_loop loop;
static es2_sim_hud hud;
static es2_sim_hud_event trace[64];
static es2_vt vt;
static unsigned refusals;
static uint32_t first_refused;
static void on_refuse(const es2_sim_refusal *r, void *user) {
    (void)user;
    if (!refusals) first_refused = r->va;
    ++refusals;
    if (getenv("HUD_TEST_VERBOSE")) fprintf(stderr, "refused 0x%08x: %s\n", (unsigned)r->va, r->reason ? r->reason : "");
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
static uint8_t r8(uint32_t a) { return es2_sim_read8(&memory, a); }
static void w32(uint32_t a, uint32_t v) { memory.write32(memory.context, a, v); }
static void w16(uint32_t a, uint16_t v) { memory.write16(memory.context, a, v); }
static void w8(uint32_t a, uint8_t v) { es2_sim_write8(&memory, a, v); }
static void text(uint32_t a, const char *t) { do w8(a++, (uint8_t)*t); while (*t++); }
static void guest_string(uint32_t a, char *out, unsigned n) { unsigned i = 0; for (; i + 1 < n && r8(a + i); ++i) out[i] = (char)r8(a + i); out[i] = 0; }

/* The recording hooks: the call order (with the HUD trace position at the
 * call) and the arguments. */
typedef struct hook_call { uint32_t va, a0, a1, a2; unsigned at; } hook_call;
static hook_call hooks[32];
static unsigned hook_count;
static uint32_t distance = 500;
static uint32_t damaged_part = 3;      /* the part the condition stub reports damaged */
static uint16_t posted[8];             /* the event words posted to the port stub */
static unsigned posted_count;
static void svc_hook(es2_sim_service_call *c, void *u) {
    (void)u;
    if (hook_count < 32u) {
        hook_call *h = &hooks[hook_count];
        h->va = c->va;
        h->a0 = c->slots[0];  /* beyond a call's own slots: the next words of the call area */
        h->a1 = c->slots[1];
        h->a2 = c->slots[2];
        h->at = hud.trace_count;
    }
    ++hook_count;
    c->eax = 0;
    if (c->va == VA_DIST_OCTAGONAL) c->eax = distance;
    if (c->va == VA_CAMERA_CURRENT) c->eax = HEAP_BASE + 0x3f0000u;
    if (c->va == VA_VISIBLE) c->eax = 1;
    if (c->va == VA_HUD_SPEED) c->eax = 42;
    if (c->va == VA_DISTANCE) c->eax = distance;
    if (c->va == VA_RNG_NEXT) c->eax = 0x7ffe0000u + 0x1357u * hook_count;  /* high word set: only the low word counts */
    if (c->va == VA_CONDITION) c->eax = (c->slots[2] & 0xffffu) == damaged_part ? 0xc8u : 0u;
    if (c->va == VA_PORT_POST && posted_count < 8u) posted[posted_count++] = memory.read16(memory.context, c->slots[1]);
    if (c->va == VA_ROTATE) {  /* out <- in */
        w32(c->slots[2], r32(c->slots[0]));
        w32(c->slots[2] + 4u, r32(c->slots[0] + 4u));
    }
}
static int has_row(uint32_t va) {
    for (unsigned i = 0; i < es2_sim_service_count(); ++i) {
        uint32_t row = 0;
        es2_sim_service_row(i, &row);
        if (row == va) return 1;
    }
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
    static const struct { uint32_t va; unsigned slots; } h[] = {
        {VA_SFX_PLAY, 1}, {VA_BUTTON_PRESS, 3}, {VA_CAMERA_CURRENT, 0}, {VA_DIST_OCTAGONAL, 2}, {VA_SINCOS, 2}, {VA_ROTATE, 3},
        {VA_VISIBLE, 1}, {VA_HUD_SPEED, 1}, {VA_CONDITION, 3}, {VA_JOLT_STEP, 1}, {VA_DISTANCE, 2}, {VA_PORT_POST, 2}, {VA_RNG_NEXT, 1}};
    for (unsigned i = 0; i < sizeof(h) / sizeof(h[0]); ++i)
        if (!has_row(h[i].va) && es2_sim_dispatch_add_service(&table, h[i].va, "hook", svc_hook, NULL, 0, h[i].slots)) return 1;
    const char *error = NULL;
    uint32_t bad = 0;
    if (es2_sim_dispatch_seal(&table, &error, &bad)) return 1;
    /* Hooks at VAs that have a service row (0x42d4a8, the jolt step; 0x436a3c, the EventPort post,
     * sim/src/hud_widgets.c) replace the row after the seal; find works only then. */
    for (unsigned i = 0; i < sizeof(h) / sizeof(h[0]); ++i) {
        if (!has_row(h[i].va)) continue;
        es2_sim_entry *e = (es2_sim_entry *)es2_sim_dispatch_find(&table, h[i].va);
        if (!e || e->kind != ES2_SIM_SERVICE) return 1;
        e->service = svc_hook;
        e->service_user = NULL;
    }
    return 0;
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
    es2_sim_hud_init(&hud, &loop);
    hud.trace = trace;
    hud.trace_capacity = 64;
    es2_sim_hud_bind(&hud);
    refusals = 0;
    hook_count = 0;
    distance = 500;
    posted_count = 0;
}
static int row(uint32_t va, const uint32_t *slots, unsigned n, es2_sim_result *r) {
    uint32_t regs[3] = {0, 0, 0};
    uint32_t at = local_guest + 0x20000u;
    for (unsigned i = 0; i < n; ++i) w32(at + 4u * i, slots[i]);
    memset(r, 0, sizeof(*r));
    return es2_sim_call(&table, va, (const uint32_t *)(uintptr_t)at, n, regs, r);
}

/* ---- the coupling site table (hud.md, "The coupling sites of the HUD
 * line": every running site of the HUD area, with the load's). */
typedef struct site { uint32_t va; char kind; } site;
static const site d31[] = {
    {0x4328da, 'C'}, {0x4328ed, 'A'}, {0x43290e, 'A'}, {0x43291b, 'A'}, {0x43293d, 'C'}, {0x438dea, 'C'}, {0x434036, 'C'},
    {0x434053, 'B'}, {0x4340b0, 'C'}, {0x4340d1, 'B'}, {0x4340ef, 'B'}, {0x434161, 'C'}, {0x4341af, 'C'}, {0x434ecd, 'C'},
    {0x4351f6, 'C'}, {0x435635, 'C'}, {0x4352a1, 'C'}, {0x435b68, 'C'}, {0x435b9a, 'C'}, {0x435cba, 'B'}, {0x436a6c, 'B'},
    {0x435e50, 'A'}, {0x436adf, 'C'}, {0x436c35, 'A'}, {0x436c3f, 'A'}, {0x436c49, 'A'}, {0x436c53, 'A'}, {0x436c64, 'A'},
    {0x436c6e, 'A'}, {0x436cda, 'A'}, {0x436f27, 'C'}, {0x436f78, 'C'}, {0x438bde, 'C'}, {0x438d84, 'B'}, {0x438e31, 'A'},
    {0x43d87b, 'C'}, {0x43dce4, 'C'}, {0x43ed34, 'C'}, {0x440eb3, 'C'}, {0x441db7, 'C'}, {0x4437d3, 'C'}, {0x446401, 'C'},
    {0x446433, 'C'}, {0x446767, 'C'}, {0x449dc0, 'C'}, {0x44b631, 'C'}, {0x44b62c, 'A'}, {0x44b754, 'A'}, {0x44b814, 'A'},
    {0x44b350, 'C'}, {0x44b3ae, 'B'}, {0x44b165, 'B'}, {0x44b12a, 'A'}, {0x44b559, 'C'}, {0x44b57f, 'A'}, {0x44c9a3, 'C'},
    {0x44e9fc, 'C'}, {0x44cee4, 'A'}, {0x44ceee, 'A'}, {0x445c27, 'C'}, {0x449964, 'C'}, {0x44c303, 'C'}, {0x452ced, 'C'},
    {0x45311a, 'C'}, {0x4530ec, 'C'}, {0x471e1f, 'C'}, {0x471ea9, 'C'},
};
static char d31_kind(uint32_t va) {
    for (unsigned i = 0; i < sizeof(d31) / sizeof(d31[0]); ++i) if (d31[i].va == va) return d31[i].kind;
    return 0;
}
/* Every traced call: its site is in the coupling table with the same kind, and
 * the target is the kind's (the clock wrapper, the stream-B routines, a
 * sound entry). */
static int check_trace_against_d31(void) {
    for (unsigned i = 0; i < hud.trace_count && i < 64u; ++i) {
        const es2_sim_hud_event *e = &trace[i];
        CHECK(d31_kind(e->site) == (char)e->kind);
        if (e->kind == ES2_SIM_HUD_CLOCK) CHECK(e->target == 0x00467828u);
        if (e->kind == ES2_SIM_HUD_STREAM_B) CHECK(e->target == 0x00492ee0u || e->target == 0x00492f24u);
    }
    return 0;
}
/* The measured table (ES2_T58_TABLE), when given. */
static unsigned t58_checked;
static int check_trace_against_t58(void) {
    const char *path = getenv("ES2_T58_TABLE");
    if (!path) return 0;
    FILE *f = fopen(path, "r");
    CHECK(f != NULL);
    site rows[512];
    unsigned n = 0;
    char line[128];
    while (n < 512u && fgets(line, sizeof(line), f)) {
        unsigned va;
        char kind;
        if (sscanf(line, "%x %c", &va, &kind) == 2) { rows[n].va = va; rows[n].kind = kind; ++n; }
    }
    fclose(f);
    for (unsigned i = 0; i < hud.trace_count && i < 64u; ++i) {
        int found = 0;
        for (unsigned k = 0; k < n; ++k) if (rows[k].va == trace[i].site && rows[k].kind == (char)trace[i].kind) found = 1;
        CHECK(found);
        ++t58_checked;
    }
    return 0;
}
static int check_trace(void) { return check_trace_against_d31() || check_trace_against_t58(); }

/* ---- 1. the power-on start 0x438ddc: G+0x8c <- 1, G+0x90 <- the clock
 * (site 0x438dea). */
static int power_on(void) {
    fresh();
    uint32_t g = HEAP_BASE + 0x1000u;
    es2_sim_result r;
    CHECK(row(0x00438ddcu, (const uint32_t[]){g}, 1, &r) == 0 && refusals == 0);
    CHECK(r8(g + 0x8cu) == 1u);
    CHECK(hud.trace_count == 1u && trace[0].kind == ES2_SIM_HUD_CLOCK && trace[0].site == 0x00438deau);
    CHECK(r32(g + 0x90u) == trace[0].result && r.eax == trace[0].result);
    return check_trace();
}
/* ---- 2. the UI click callback 0x438e2c: sfx_play(0x11) (site 0x438e31). */
static int ui_click(void) {
    fresh();
    es2_sim_result r;
    CHECK(row(0x00438e2cu, (const uint32_t[]){HEAP_BASE + 0x2000u}, 1, &r) == 0 && refusals == 0);
    CHECK(hook_count == 1u && hooks[0].va == VA_SFX_PLAY && hooks[0].a0 == 0x11u);
    CHECK(hud.trace_count == 1u && trace[0].kind == ES2_SIM_HUD_SOUND && trace[0].site == 0x00438e31u && trace[0].argument == 0x11u);
    return check_trace();
}
/* ---- 3. the mission time 0x43dcd4: one clock read (site 0x43dce4) per
 * call; past the deadline 0x4d1804 the digits advance (09:59 -> 10:00)
 * and 0x4d17e4 is rebuilt; before it nothing else changes. */
static int mission_time(void) {
    fresh();
    text(0x004d17dcu, "09");
    text(0x004d17dfu, "59");
    w8(0x004d17e2u, ':');
    w8(0x004d17e3u, 0);
    w32(0x004d1804u, 0);
    es2_sim_result r;
    CHECK(row(0x0043dcd4u, (const uint32_t[]){0}, 1, &r) == 0 && refusals == 0);
    CHECK(hud.trace_count == 1u && trace[0].site == 0x0043dce4u);
    CHECK(r32(0x004d1804u) == trace[0].result + 60u);
    char s[16];
    guest_string(0x004d17e4u, s, sizeof s);
    CHECK(!strcmp(s, "10:00"));
    w32(0x004d1804u, 0xffffffffu);
    CHECK(row(0x0043dcd4u, (const uint32_t[]){0}, 1, &r) == 0 && refusals == 0);
    CHECK(hud.trace_count == 2u && trace[1].site == 0x0043dce4u);
    guest_string(0x004d17dcu, s, 3);
    CHECK(!strcmp(s, "10"));
    CHECK(r32(0x004d1804u) == 0xffffffffu);
    return check_trace();
}
/* ---- 4. the radar blip scan 0x43ec08 (coupling row 2c): the radar key
 * change presses button 11 of the MFD before any clock read; per enemy
 * (side 1, not marked) one clock read at 0x43ed34, in the off half of
 * the blink (bit 0x20) no blip; friends read no clock; the blip table. */
static int blip_scan(void) {
    fresh();
    uint32_t h = HEAP_BASE + 0x10000u, p = HEAP_BASE + 0x11000u, e = HEAP_BASE + 0x12000u, f = HEAP_BASE + 0x13000u;
    uint32_t mfd = HEAP_BASE + 0x14000u, rad = HEAP_BASE + 0x15000u, rng = HEAP_BASE + 0x16000u, list = HEAP_BASE + 0x17000u;
    uint32_t side0 = HEAP_BASE + 0x18000u, side1 = HEAP_BASE + 0x18100u;
    w32(0x0049b088u, h);
    w32(h + 0x203u, p);
    w32(h + 0x210u, e);
    w32(h + 0x1edu, mfd);
    w32(rad + 8u, rng);
    w32(rng + 0xcu, 1000u);
    w32(rad + 0x34u, 0);
    w8(p + 0x96u, 0);               /* the R key: radar on (1) */
    w32(p + 0x45u, side0);
    w32(p + 0x26u, 100u);
    w32(p + 0x2au, 200u);
    w32(e + 0x45u, side1);
    w8(side1 + 0x12u, 1);
    w32(e + 0x1aau, 150u);          /* the seen position */
    w32(e + 0x1aeu, 260u);
    w32(f + 0x45u, side0);
    w32(f + 0x26u, 90u);
    w32(f + 0x2au, 210u);
    w16(f + 0x1a8u, 2);             /* a vehicle */
    w32(list, e);
    w32(list + 4u, f);
    w32(0x004a9af8u, list);
    w16(0x004a9afeu, 2);
    w16(0x004d3b8eu, 0x31);
    w16(0x004d3b9eu, 0x42);
    es2_sim_result r;
    CHECK(row(0x0043ec08u, (const uint32_t[]){rad}, 1, &r) == 0 && refusals == 0);
    unsigned presses = 0;
    for (unsigned i = 0; i < hook_count; ++i)
        if (hooks[i].va == VA_BUTTON_PRESS) {
            ++presses;
            CHECK(hooks[i].a0 == mfd && hooks[i].a1 == 0xbu && hooks[i].a2 == 1u && hooks[i].at == 0u);
        }
    CHECK(presses == 1u && r32(rad + 0x34u) == 1u);
    CHECK(hud.trace_count == 1u && trace[0].site == 0x0043ed34u);
    int blink_off = (trace[0].result & 0x20u) != 0;
    CHECK(r32(rad + 0x518u) == (blink_off ? 1u : 2u));
    uint32_t b = rad + 0x68u;
    if (!blink_off) {
        CHECK(r32(b) == 50u && r32(b + 4u) == (uint32_t)-60 && r32(b + 8u) == 0x31u);
        CHECK(r32(rad + 0x51cu) == b && r32(rad + 0x520u) == 0u);
        b += 0xcu;
    }
    CHECK(r32(b) == (uint32_t)-10 && r32(b + 4u) == (uint32_t)-10 && r32(b + 8u) == 0x42u);
    /* No key change: no press; beyond the range: no blip, the clock still read. */
    hook_count = 0;
    distance = 2000;
    CHECK(row(0x0043ec08u, (const uint32_t[]){rad}, 1, &r) == 0 && refusals == 0);
    for (unsigned i = 0; i < hook_count; ++i) CHECK(hooks[i].va != VA_BUTTON_PRESS);
    CHECK(hud.trace_count == 2u && trace[1].site == 0x0043ed34u && r32(rad + 0x518u) == 0u);
    return check_trace();
}
/* ---- 5. the per-frame order of coupling row 2: the HUD's full draw 0x43d5f0
 * (visible, no gadgets): the mission time's clock read (0x43dce4) comes
 * before the radar's (0x43e114 -> 0x43f2d8 -> 0x43ec08, 0x43ed34), the
 * canvas is pushed and popped again, the texts are set. */
static int hud_order(void) {
    fresh();
    uint32_t panel = HEAP_BASE + 0x10000u, p = HEAP_BASE + 0x11000u, e = HEAP_BASE + 0x12000u, side0 = HEAP_BASE + 0x18000u;
    uint32_t side1 = HEAP_BASE + 0x18100u, mfd = HEAP_BASE + 0x14000u, rad = HEAP_BASE + 0x15000u, rng = HEAP_BASE + 0x16000u;
    uint32_t list = HEAP_BASE + 0x17000u, hd = HEAP_BASE + 0x19000u, thr = HEAP_BASE + 0x1a000u, pv = HEAP_BASE + 0x1b000u;
    uint32_t canvas = HEAP_BASE + 0x1c000u, surface = HEAP_BASE + 0x1d000u, label = HEAP_BASE + 0x1e000u, font = HEAP_BASE + 0x1e800u;
    uint32_t chassis = HEAP_BASE + 0x1f000u, frames = HEAP_BASE + 0x20000u;
    w32(0x0049b088u, panel);
    w32(panel + 4u, canvas);
    w32(panel + 0x203u, p);
    w32(panel + 0x1edu, mfd);
    w32(panel + 0x1e1u, thr);
    w32(mfd + 0xd9u, rad);
    w32(mfd + 0xcdu, HEAP_BASE + 0x21000u);     /* the MFD shows another page */
    w32(rad + 8u, rng);
    w32(rng + 0xcu, 1000u);
    w32(rad + 0x34u, 1u);
    w32(p, pv);                                 /* the player's slot +0x3c */
    w32(pv + 0x3cu, VA_CAMERA_CURRENT);
    w32(p + 0x45u, side0);
    w32(p + 0x1f2u, chassis);                   /* not a flyer: no altitude ladder */
    w32(e + 0x45u, side1);
    w8(side1 + 0x12u, 1);
    w32(e + 0x1aau, 150u);
    w32(list, e);
    w32(0x004a9af8u, list);
    w16(0x004a9afeu, 1);
    w32(0x006c5ed0u, surface);
    w32(0x006c5f70u, canvas);
    w32(0x004a3691u, 0x004a3699u);
    w32(0x004a3695u, 0x004a431du);
    /* The HUD radar (panel.c) draws through the BMP_8 driver slots into a 128x128 surface: the
     * ellipse 0x48817c through slot 0, the span writer 0x489f84; the point 0x4895ec through
     * slot 5, 0x48a361; the line 0x483a04 through slot 34, 0x48a3b0. */
    uint32_t rows = HEAP_BASE + 0x23000u, pixels = HEAP_BASE + 0x24000u;
    w32(0x004a57acu, 0x00489f84u);
    w32(0x004a57acu + 4u * 5u, 0x0048a361u);
    w32(0x004a57acu + 4u * 34u, 0x0048a3b0u);
    w32(0x004a3652u, rows);
    w32(0x004a3676u, 128u);
    w32(rows, pixels);
    for (uint32_t y = 0; y < 128u; ++y) w32(rows + 4u * (y + 1u), 128u * y);
    w32(hd + 0xd7u, HEAP_BASE + 0x22000u);
    w32(0x004d24e6u, p);
    w32(label, font);
    w16(font + 0xcu, 4u);
    /* The label texts draw (0x482634): a glyph offset table (all 0), zero-height glyphs */
    w32(font + 0x26u, HEAP_BASE + 0x25000u);
    w32(0x004a57acu + 4u * 26u, 0x004899acu);  /* the glyph blit, slot 26 */
    for (uint32_t k = 0; k < 4u; ++k) w32(0x004d17ccu + 4u * k, label);
    w32(0x004d1658u, 0x004d17e4u);
    w32(0x004d165cu, 0x004d17e4u);
    w32(0x0049cbccu, frames);
    w32(frames + 8u, frames + 0x100u);
    text(0x004d17dcu, "00");
    text(0x004d17dfu, "00");
    text(0x004d17e2u, ":");
    es2_sim_result r;
    CHECK(row(0x0043d5f0u, (const uint32_t[]){hd}, 1, &r) == 0 && refusals == 0);
    CHECK(hud.trace_count == 2u && trace[0].site == 0x0043dce4u && trace[1].site == 0x0043ed34u);
    CHECK(trace[1].result >= trace[0].result);  /* the virtual clock does not run backwards */
    CHECK(r8(0x0049b106u) == 0u && r32(0x006c5f70u) == canvas);   /* pushed and popped */
    char s[16];
    guest_string(0x004d17e4u, s, sizeof s);
    CHECK(!strcmp(s, "00:01"));
    CHECK(r32(rad + 0x2cu) == 1000u / 23u);     /* the radar scale */
    return check_trace();
}
/* ---- 6. the display set 0x42a1f0 with mode 4 (full-screen view): the
 * old mode kept in D+0x23, the panel hidden (byte +0x20f), the GC from
 * 0x4cfa98, the mode offsets cleared, no coupling call. */
static int display_mode4(void) {
    fresh();
    uint32_t d = HEAP_BASE + 0x10000u, panel = HEAP_BASE + 0x11000u, gc = HEAP_BASE + 0x12000u;
    w32(0x006c5f70u, gc);
    w32(d + 0x14u, 0);
    w32(d + 0x1fu, panel);
    for (uint32_t i = 0; i < 0x81u; ++i) w32(0x004cfa98u + 4u * i, 0x100u + i);
    for (uint32_t k = 0; k < 6u; ++k) w32(0x004cfa40u + 4u * k, 0x200u + k);
    w32(0x004cf9a0u, 7u);
    w32(0x004cf9a4u, 8u);
    /* The border fills (0x486704) draw on the context record [0x4a3614]; a
     * fill of a kind the fill ignores (7) keeps them free of a surface. */
    w32(0x004a3614u, gc);
    w32(gc + 0x228u, HEAP_BASE + 0x13000u);
    w32(HEAP_BASE + 0x13000u, 7u);
    es2_sim_result r;
    CHECK(row(0x0042a1f0u, (const uint32_t[]){d, 4u}, 2, &r) == 0 && refusals == 0);
    CHECK(r32(d + 0x14u) == 4u && r32(d + 0x23u) == 0u && r8(panel + 0x20fu) == 1u);
    CHECK(r32(gc + 4u) == 0x100u && r32(gc + 0x204u) == 0x180u && r32(gc + 0x208u) == 2u);
    CHECK(r32(gc + 0x210u) == 0x200u && r32(gc + 0x224u) == 0x205u);
    CHECK(r32(0x004cf9a0u) == 0u && r32(0x004cf9a4u) == 0u);
    CHECK(hud.trace_count == 0u);
    return 0;
}

/* ---- the per-frame walk of coupling rows 1a, 1b, 1c, 1i.
 * 7. the jolt 0x43408c: the clock (0x4340b0) before the two stream-B
 * draws (0x4340d1 toggle time, 0x4340ef jolt step) and the step's
 * argument; at the end time the jolt clears; mode 4 clears it at once. */
static int jolt_order(void) {
    fresh();
    uint32_t d = HEAP_BASE + 0x10000u;
    w32(0x004cf99cu, d);
    w32(0x0049b0fcu, 0xffffffffu);
    es2_sim_result r;
    CHECK(row(0x0043408cu, NULL, 0, &r) == 0 && refusals == 0);
    CHECK(hud.trace_count == 3u && trace[0].site == 0x004340b0u && trace[0].kind == ES2_SIM_HUD_CLOCK);
    CHECK(trace[1].site == 0x004340d1u && trace[2].site == 0x004340efu);
    uint32_t now = trace[0].result;
    CHECK(r32(0x0049b100u) == now + (trace[1].result & 0xffffu) % 10u);
    unsigned steps = 0;
    for (unsigned i = 0; i < hook_count; ++i)
        if (hooks[i].va == VA_JOLT_STEP) {
            ++steps;
            CHECK(hooks[i].a0 == (trace[2].result & 0xffffu) % 5u && hooks[i].at == 3u);
        }
    CHECK(steps == 1u && (r.eax & 0xffu) == 1u && r32(0x0049b0fcu) == 0xffffffffu);
    /* Before the next toggle time: only the step's draw. */
    w32(0x0049b100u, 0xfffffff0u);
    CHECK(row(0x0043408cu, NULL, 0, &r) == 0 && refusals == 0);
    CHECK(hud.trace_count == 5u && trace[3].site == 0x004340b0u && trace[4].site == 0x004340efu);
    /* At the end time: cleared, EAX 1, no draw. */
    w32(0x0049b0fcu, trace[3].result);
    CHECK(row(0x0043408cu, NULL, 0, &r) == 0 && refusals == 0);
    CHECK(hud.trace_count == 6u && r.eax == 1u && r32(0x0049b0fcu) == 0u);
    /* Mode 4: the end time clears before the clock; nothing else. */
    w32(d + 0x14u, 4u);
    w32(0x0049b0fcu, 0xffffffffu);
    w32(0x0049b100u, 0u);
    CHECK(row(0x0043408cu, NULL, 0, &r) == 0 && refusals == 0);
    CHECK(hud.trace_count == 6u && r.eax == 0u && r32(0x0049b0fcu) == 0u);
    return check_trace();
}
/* ---- 8. the bump 0x434194: the clock (0x4341af), the curve word halved
 * (word 0x4d253c zero) or shifted, its low word into the GC bias. */
static int bump_curve(void) {
    fresh();
    uint32_t d = HEAP_BASE + 0x10000u, gc = HEAP_BASE + 0x11000u;
    w32(0x004cf99cu, d);
    w32(0x006c5f70u, gc);
    w32(gc + 0x224u, 100u);
    for (uint32_t i = 0; i < 0x100u; ++i) w16(0x0049b046u + 2u * i, (uint16_t)-8);
    w32(0x0049b634u, es2_sim_clock(&loop));
    w32(0x0049b638u, 0xffffffffu);
    es2_sim_result r;
    CHECK(row(0x00434194u, NULL, 0, &r) == 0 && refusals == 0);
    CHECK(hud.trace_count == 1u && trace[0].site == 0x004341afu && (r.eax & 0xffu) == 1u);
    CHECK(r32(gc + 0x224u) == 100u + 0xfffcu && r32(0x004cfcb8u) == 0xfffcu);
    w16(0x004d253cu, 3u);
    CHECK(row(0x00434194u, NULL, 0, &r) == 0 && refusals == 0);
    CHECK(r32(gc + 0x224u) == 100u + 0xffe0u && r32(0x004cfcb8u) == 0xffe0u);
    /* Past the end time: the end clears, the bias stays, EAX 0. */
    w32(0x0049b638u, 1u);
    CHECK(row(0x00434194u, NULL, 0, &r) == 0 && refusals == 0);
    CHECK(r.eax == 0u && r32(0x0049b638u) == 0u && r32(gc + 0x224u) == 100u + 0xffe0u);
    /* The bias clear 0x42d854 (taken at the end time itself). */
    CHECK(row(0x0042d854u, NULL, 0, &r) == 0 && r.eax == gc && r.edx == 0xffe0u);
    CHECK(r32(gc + 0x224u) == 100u && r32(0x004cfcb8u) == 0u);
    return check_trace();
}
/* ---- 9. the power-on walk 0x432924: past 200 the parts are asked in
 * order until the first damaged one (class below 90), the event 0x22 goes
 * to the port's vtable; undamaged: 0x21. The damage class 0x438700. */
static int power_on_walk(void) {
    fresh();
    uint32_t h = HEAP_BASE + 0x10000u, p = HEAP_BASE + 0x11000u, port = HEAP_BASE + 0x12000u, vtab = HEAP_BASE + 0x12100u;
    w32(h + 0x241u, 1u);
    w32(h + 0x203u, p);
    w32(h + 0x20bu, port);
    w32(port, vtab);
    w32(vtab, VA_PORT_POST);
    es2_sim_result r;
    CHECK(row(0x00432924u, (const uint32_t[]){h}, 1, &r) == 0 && refusals == 0);
    CHECK(hud.trace_count == 1u && trace[0].site == 0x0043293du && r8(h + 0x245u) == 1u);
    unsigned asked = 0;
    for (unsigned i = 0; i < hook_count; ++i)
        if (hooks[i].va == VA_CONDITION) {
            CHECK(hooks[i].a0 == p + 0x206u && hooks[i].a1 == 0u && hooks[i].a2 == asked);
            ++asked;
        }
    CHECK(asked == 4u && posted_count == 1u && posted[0] == 0x22u);
    damaged_part = 99;
    CHECK(row(0x00432924u, (const uint32_t[]){h}, 1, &r) == 0 && refusals == 0);
    CHECK(posted_count == 2u && posted[1] == 0x21u);
    damaged_part = 3;
    CHECK(row(0x00438700u, (const uint32_t[]){0u}, 1, &r) == 0 && r.eax == 0u);
    CHECK(row(0x00438700u, (const uint32_t[]){0x30u}, 1, &r) == 0 && r.eax == 1u);
    CHECK(row(0x00438700u, (const uint32_t[]){0x100u}, 1, &r) == 0 && r.eax == 4u);
    return check_trace();
}
/* ---- 10. the nav marker 0x4349ac: armed and near posts 0x1d once and
 * disarms the marker; unarmed and far arms it. No coupling call. */
static int nav_marker(void) {
    fresh();
    uint32_t h = HEAP_BASE + 0x10000u, port = HEAP_BASE + 0x12000u, vtab = HEAP_BASE + 0x12100u;
    w32(0x0049b088u, h);
    w32(h + 0x20bu, port);
    w32(port, vtab);
    w32(vtab, VA_PORT_POST);
    w8(h + 0x26au, 1);
    w8(h + 0x26bu, 1);
    es2_sim_result r;
    CHECK(row(0x004349acu, NULL, 0, &r) == 0 && refusals == 0);
    CHECK(posted_count == 1u && posted[0] == 0x1du && r8(h + 0x26au) == 0u);
    w8(h + 0x26au, 1);
    w8(h + 0x26bu, 0);
    distance = 20000;
    CHECK(row(0x004349acu, NULL, 0, &r) == 0 && refusals == 0);
    CHECK(posted_count == 1u && r8(h + 0x26bu) == 1u && r8(h + 0x26au) == 1u && hud.trace_count == 0u);
    return 0;
}

/* ---- 11. the load root's detail rows 0x45d490/0x45d50c (synthetic
 * tables: the first threshold not below n selects) and the radar's range
 * units 0x434228 ((v idiv 1000) * 6). */
static int detail_rows(void) {
    fresh();
    static const int16_t thr[5] = {10, 20, 30, 40, 50};
    for (uint32_t k = 0; k < 5u; ++k) {
        w16(0x0049efccu + 2u * k, (uint16_t)thr[k]);
        w16(0x0049efd6u + 2u * k, (uint16_t)(-100 - (int)k));
        w16(0x0049efe0u + 2u * k, (uint16_t)(thr[k] + 10));
        w16(0x0049efeau + 2u * k, (uint16_t)(200 + k));
        w16(0x0049eff4u + 2u * k, (uint16_t)(300 + k));
    }
    es2_sim_result r;
    CHECK(row(0x0045d490u, (const uint32_t[]){25u}, 1, &r) == 0 && refusals == 0);
    CHECK(r32(0x00497368u) == (uint32_t)-102 && r32(0x0049736cu) == 201u && r32(0x004a9868u) == 301u);
    CHECK(r.eax == 301u && r.edx == 201u && r.ecx == 25u);
    CHECK(row(0x0045d490u, (const uint32_t[]){0x10063u}, 1, &r) == 0);   /* the word 99: no match */
    CHECK(r.eax == 5u && r.edx == 0x0049efeau && r32(0x00497368u) == (uint32_t)-102);
    /* 0x45d50c: thresholds at 0x49effe, values at 0x49f004 (the tables overlap). */
    w16(0x0049effeu, 0);
    w16(0x0049f000u, 5);
    w16(0x0049f002u, 9);
    w16(0x0049f004u, 7);
    w16(0x0049f006u, 11);
    w16(0x0049f008u, (uint16_t)-3);
    CHECK(row(0x0045d50cu, (const uint32_t[]){6u}, 1, &r) == 0);
    CHECK(r32(0x004a95b4u) == (uint32_t)-3 && r32(0x004a9dc4u) == (uint32_t)-3 && r.edx == 0x0049f008u && r.ecx == (uint32_t)-3);
    CHECK(row(0x00434228u, (const uint32_t[]){(uint32_t)-2500}, 1, &r) == 0 && r.eax == (uint32_t)-12);
    CHECK(row(0x00434228u, (const uint32_t[]){150000u}, 1, &r) == 0 && r.eax == 900u);
    return 0;
}

/* ---- the EventPort frame 0x436f70 after a pause: the paused total [0x4d1294]
 * past the clock makes the product negative, a scale of 6 makes it pass
 * 60 << 32; XOR EDX, EDX before the DIV 0x436f9c divides the product's low
 * dword only, so the original never faults there. Headless, windowed and with
 * a process record that has process_fault alike: [0x4d1290] <- [p+0x4af] less
 * (low dword) / 60, no refusal, no fault, the draw slot called. */
static struct { unsigned faults; uint32_t code, site; } fault_rec;
static void rec_fault(void *user, uint32_t code, uint32_t site) {
    (void)user;
    ++fault_rec.faults;
    fault_rec.code = code;
    fault_rec.site = site;
}
static int event_pause(void) {
    static const es2_sim_loop_window window;
    static es2_sim_process_platform process = {NULL, NULL, NULL, NULL, rec_fault};
    static const struct { uint16_t scale; int32_t elapsed; } cases[] = {
        {0, -0x3f000000}, {1, -5}, {6, 0x40000000}, {1, 47}, {0, 0},
    };
    uint32_t p = HEAP_BASE + 0x4000u, vtable = HEAP_BASE + 0x5000u;
    es2_sim_result r;
    for (int mode = 0; mode < 3; ++mode)
        for (unsigned k = 0; k < sizeof(cases) / sizeof(cases[0]); ++k) {
            for (int pass = 0; pass < 2; ++pass) {
                fresh();
                if (mode == 1) loop.window = &window;
                if (mode == 2) loop.process = &process;
                memset(&fault_rec, 0, sizeof(fault_rec));
                w32(p, vtable);
                w32(vtable + 0x18u, VA_SFX_PLAY);      /* the draw slot, recorded */
                w32(p + 0x4afu, 0x1234u);
                w32(p + 0x4ccu, 1);                    /* the step 0x435610 returns at once */
                w32(0x004d1290u, 0xdeadbeefu);
                w16(0x004d253au, cases[k].scale);
                /* pass 0 reads the clock; pass 1 sets the paused total from it */
                w32(0x004d1294u, 0);
                if (pass) w32(0x004d1294u, trace[0].result - (uint32_t)cases[k].elapsed);
                CHECK(row(0x00436f70u, (const uint32_t[]){p}, 1, &r) == 0);
                CHECK(hud.trace_count == 1u && trace[0].site == 0x00436f78u);
                CHECK(refusals == 0 && fault_rec.faults == 0 && loop.outcome != ES2_SIM_LOOP_EXITED);
                CHECK(hook_count == 1u && hooks[0].va == VA_SFX_PLAY && hooks[0].a0 == p);
                uint32_t elapsed = trace[0].result - r32(0x004d1294u), product = (0x23u << cases[k].scale) * elapsed;
                CHECK(r32(0x004d1290u) == 0x1234u - product / 60u);
                if (pass) CHECK(elapsed == (uint32_t)cases[k].elapsed);
            }
        }
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
    if (power_on() || ui_click() || mission_time() || blip_scan() || hud_order() || display_mode4() ||
        jolt_order() || bump_curve() || power_on_walk() || nav_marker() || detail_rows() || event_pause()) {
        if (refusals) fprintf(stderr, "first refusal 0x%08x\n", (unsigned)first_refused);
        return 1;
    }
    if (getenv("ES2_T58_TABLE")) printf("measured table: %u traced calls matched\n", t58_checked);
    else puts("measured table: not given (ES2_T58_TABLE unset), skipped");
    puts("PASS: hud: power-on clock, UI click sound, mission time, radar blip scan (key press, blink, range), the HUD "
         "draw's clock order, display mode 4, the per-frame jolt, bump, power-on walk and nav marker, the detail rows, the EventPort frame after a pause (refused headless, quotient 0 windowed); every traced site in hud.md's coupling table");
    return 0;
}
