/* The panel gadgets' method-table bodies (the range gauge's items 0x4420a4 and
 * key 0x4421c8, the weapon toggle's 0x44244c/0x442480, the weapon button's
 * 0x4428fc/0x442948, the power slider's 0x442c10, the range toggle's
 * 0x442dbc/0x442df0) and the twelve event-base entries 0x4433c7..0x443435,
 * original machine code against sim/src/hud_gadgets.c
 * (differential_panel_gadgets_test). It reuses startup_cases.h's snapshots and
 * loop_cases.h's virtual time and loop, as widget_leaves_cases.h does.
 *
 * Both sides start from the pristine DATA and an empty heap and build the
 * same synthetic world from the same seeded generator: six gadgets (each
 * redraw byte 0, 1, 2, 3 or 0xff), two method tables for them, two event-
 * base tables, two owners, and a range gauge whose four items are gadgets.
 * Then 1..8 of the 21 rows run on it, with a key word (0x26, 0x29, those
 * with high bits set, others) or a value (odd or even). Within the set the
 * original runs its own code (an entry reaches the real method). The other
 * packages' callees (the panel copy 0x43122c, the toggle draw 0x4423bc, the
 * press 0x438d9c and the entries' other targets 0x442cb0, 0x43901c,
 * 0x442b60, 0x4426e8) are recording stubs on both sides; the method-table
 * slots hold host stubs. Some stubs change the world the way a method may
 * (the redraw byte, a gadget's method table, its event base's table, its
 * owner), so a read placed before instead of after a call shows. Compared:
 * DATA 0x497000..0x6cc000 and the heap, the stub log, EAX by mask (0 where
 * the original leaves the entry EAX; AL where it leaves AL computed), and
 * the own coupling trace (empty). Coverage: per-path counters from the
 * original side; every one must be non-zero. */
#pragma once
/* The headers this suite builds on (sim_main.c). */
#include "init_records_cases.h"
#include "startup_cases.h"
#include "loop_cases.h"
#include "es2/sim/hud.h"

#define PG_TRACE 256u
#define PG_GADGETS 6u
static es2_sim_entry pg_entries[4096];
static es2_sim_dispatch pg_dispatch;
static es2_sim_hud pg_hud;
static es2_sim_hud_event pg_trace[PG_TRACE];
static int pg_bad;
static unsigned pg_side;

/* ---- the stub log of a side */
static uint64_t pg_hash[2];
static unsigned pg_count[2];
static char pg_text[2][8192];
static size_t pg_len[2];
static void pg_note(char letter, uint32_t a, uint32_t b, uint32_t c) {
    uint64_t h = pg_hash[pg_side];
    const uint32_t v[4] = {(uint32_t)letter, a, b, c};
    for (unsigned i = 0; i < 4u; ++i) { h ^= v[i]; h *= 0x100000001b3ull; }
    pg_hash[pg_side] = h;
    ++pg_count[pg_side];
    if (pg_len[pg_side] + 48u < sizeof(pg_text[0]))
        pg_len[pg_side] += (size_t)sprintf(pg_text[pg_side] + pg_len[pg_side], "%c(%lx,%lx,%lx) ", letter, (unsigned long)a,
                                           (unsigned long)b, (unsigned long)c);
}
static uint8_t pg_get8(uint32_t a) { return *(BYTE *)(uintptr_t)a; }
static void pg_put8(uint32_t a, uint8_t v) { *(BYTE *)(uintptr_t)a = v; }
static void pg_put32(uint32_t a, uint32_t v) { put32((BYTE *)(uintptr_t)a, v); }

/* ---- the world (filled by pg_build) */
typedef struct pg_world {
    uint32_t gadget[PG_GADGETS], vt[2], eb[2], owner[2], ovt[2], gauge;
} pg_world;
static pg_world pg_w, pg_w0;

/* ---- the stubs' answers: a generator reset per side */
static uint32_t pg_answer_seed;
static uint32_t pg_answer(uint32_t n) {
    pg_answer_seed = pg_answer_seed * 1103515245u + 12345u;
    return (pg_answer_seed >> 8) % n;
}
/* Side effects (counted on the original side): a stub changes what the
 * caller reads next. */
enum { PGE_REDRAW, PGE_TABLE, PGE_BASE, PGE_OWNER, PGE_COUNT };
static unsigned pg_effects[PGE_COUNT];
static void pg_effect(unsigned kind) { if (pg_side == 0) ++pg_effects[kind]; }
/* One stub body for both sides. */
static uint32_t pg_stub(char letter, const uint32_t *s) {
    switch (letter) {
    case 'C':   /* the panel copy (mode, rect, palette) */
        pg_note('C', s[0], s[1], s[2]);
        if (pg_answer(3u) == 0) { pg_put8(s[1] + 0x1fu, (uint8_t)pg_answer(4u)); pg_effect(PGE_REDRAW); }
        return 0x1100u + pg_answer(0x100u);
    case 'P': pg_note('P', s[0], s[1], s[2]); return 0x2200u + pg_answer(0x100u);   /* the press */
    case 'T': case 'd': case 'D':   /* the toggle draw, a gadget's slot 0 */
        pg_note(letter, s[0], 0, 0);
        switch (pg_answer(4u)) {
        case 0: pg_put8(s[0] + 0x1fu, (uint8_t)pg_answer(4u)); pg_effect(PGE_REDRAW); break;
        case 1: pg_put32(s[0] + 0x17u, pg_w.vt[1]); pg_effect(PGE_TABLE); break;
        default: break;
        }
        return 0x3300u + pg_answer(0x100u);
    case 'o': case 'O':   /* an owner's slot 0 (owner, gadget, value) */
        pg_note(letter, s[0], s[1], s[2]);
        if (pg_answer(3u) == 0) { pg_put32(s[1] + 0x17u, pg_w.vt[1]); pg_effect(PGE_TABLE); }
        return 0x4400u + pg_answer(0x100u);
    case 'r': case 'R':   /* a gadget's slot +4 */
        pg_note(letter, s[0], 0, 0);
        if (pg_answer(3u) == 0) { pg_put32(s[0] + 0x20u, pg_w.eb[1]); pg_effect(PGE_BASE); }
        return 0x5500u + pg_answer(0x100u);
    case 'e': case 'E':   /* an event base's slot +8 (the base at gadget+0x20) */
        pg_note(letter, s[0], 0, 0);
        if (pg_answer(3u) == 0) { pg_put32(s[0] - 0x20u + 0x24u, pg_w.owner[1]); pg_effect(PGE_OWNER); }
        return 0x6600u + pg_answer(0x100u);
    case 'm': pg_note('m', s[0], s[1], 0); return 0x7700u + pg_answer(0x100u);   /* 0x43901c (S, v) */
    default: pg_note(letter, s[0], 0, 0); return 0x8800u + pg_answer(0x100u);   /* i I a b w */
    }
}
/* ---- the original side's stubs */
static uint32_t __cdecl pg_o_copy(uint32_t a, uint32_t b, uint32_t c) { const uint32_t s[3] = {a, b, c}; return pg_stub('C', s); }
static uint32_t __cdecl pg_o_press(uint32_t a, uint32_t b, uint32_t c) { const uint32_t s[3] = {a, b, c}; return pg_stub('P', s); }
static uint32_t __cdecl pg_o_draw(uint32_t a) { return pg_stub('T', &a); }
static uint32_t __cdecl pg_o_range_draw(uint32_t a) { return pg_stub('a', &a); }
static uint32_t __cdecl pg_o_moved(uint32_t a, uint32_t b) { const uint32_t s[2] = {a, b}; return pg_stub('m', s); }
static uint32_t __cdecl pg_o_slider_draw(uint32_t a) { return pg_stub('b', &a); }
static uint32_t __cdecl pg_o_button_draw(uint32_t a) { return pg_stub('w', &a); }
/* The slot stubs: their host addresses are the VAs both sides call. */
static uint32_t __cdecl pg_slot_d(uint32_t a) { return pg_stub('d', &a); }
static uint32_t __cdecl pg_slot_D(uint32_t a) { return pg_stub('D', &a); }
static uint32_t __cdecl pg_slot_r(uint32_t a) { return pg_stub('r', &a); }
static uint32_t __cdecl pg_slot_R(uint32_t a) { return pg_stub('R', &a); }
static uint32_t __cdecl pg_slot_i(uint32_t a) { return pg_stub('i', &a); }
static uint32_t __cdecl pg_slot_I(uint32_t a) { return pg_stub('I', &a); }
static uint32_t __cdecl pg_slot_e(uint32_t a) { return pg_stub('e', &a); }
static uint32_t __cdecl pg_slot_E(uint32_t a) { return pg_stub('E', &a); }
static uint32_t __cdecl pg_slot_o(uint32_t a, uint32_t b, uint32_t c) { const uint32_t s[3] = {a, b, c}; return pg_stub('o', s); }
static uint32_t __cdecl pg_slot_O(uint32_t a, uint32_t b, uint32_t c) { const uint32_t s[3] = {a, b, c}; return pg_stub('O', s); }
static int pg_patch(uint32_t va, void *target, BYTE saved[5], int install) {
    BYTE *site = mapped + (va - 0x400000u);
    DWORD previous;
    if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &previous)) return fail("panel gadgets: stub protection");
    if (install) { memcpy(saved, site, 5); site[0] = 0xe9; put32(site + 1, (uint32_t)(uintptr_t)target - (uint32_t)(uintptr_t)(site + 5)); }
    else memcpy(site, saved, 5);
    if (!VirtualProtect(site, 5, previous, &previous)) return fail("panel gadgets: stub restore protection");
    FlushInstructionCache(GetCurrentProcess(), site, 5);
    return 0;
}
static const struct { uint32_t va; void *stub; char letter; unsigned slots; int row; } pg_patched[] = {
    {0x0043122cu, (void *)pg_o_copy, 'C', 3, 1}, {0x00438d9cu, (void *)pg_o_press, 'P', 3, 0},
    {0x004423bcu, (void *)pg_o_draw, 'T', 1, 1}, {0x00442cb0u, (void *)pg_o_range_draw, 'a', 1, 1},
    {0x0043901cu, (void *)pg_o_moved, 'm', 2, 1}, {0x00442b60u, (void *)pg_o_slider_draw, 'b', 1, 1},
    {0x004426e8u, (void *)pg_o_button_draw, 'w', 1, 1},
};
#define PG_PATCHED (sizeof(pg_patched) / sizeof(pg_patched[0]))
static BYTE pg_saved[PG_PATCHED][5];

/* ---- the own side */
static void pg_s_stub(es2_sim_service_call *call, void *user) { call->eax = pg_stub((char)(uintptr_t)user, call->slots); }
static void pg_refuse(const es2_sim_refusal *r, void *user) {
    (void)user;
    fprintf(stderr, "FAIL: own panel gadgets: refused call to 0x%08lx (%s): %s\n", (unsigned long)r->va, r->name ? r->name : "?",
            r->reason ? r->reason : "");
    pg_bad = 1;
}
static const struct { void *host; char letter; unsigned slots; } pg_hosts[] = {
    {(void *)pg_slot_d, 'd', 1}, {(void *)pg_slot_D, 'D', 1}, {(void *)pg_slot_r, 'r', 1}, {(void *)pg_slot_R, 'R', 1},
    {(void *)pg_slot_i, 'i', 1}, {(void *)pg_slot_I, 'I', 1}, {(void *)pg_slot_e, 'e', 1}, {(void *)pg_slot_E, 'E', 1},
    {(void *)pg_slot_o, 'o', 3}, {(void *)pg_slot_O, 'O', 3},
};
static int pg_dispatch_setup(void) {
    es2_sim_dispatch_init(&pg_dispatch, pg_entries, 4096, pg_refuse, NULL);
    static es2_sim_services s;
    s.memory = &ir_memory;
    s.heap = &ir_heap;
    s.vt = &lc_vt;
    s.fs0 = NULL;
    if (es2_sim_register_services(&pg_dispatch, &s)) return fail("panel gadgets: dispatch");
    for (unsigned i = 0; i < PG_PATCHED; ++i)
        if (!pg_patched[i].row &&
            es2_sim_dispatch_add_service(&pg_dispatch, pg_patched[i].va, "panel gadgets stub", pg_s_stub, (void *)(uintptr_t)pg_patched[i].letter,
                                         0, (uint8_t)pg_patched[i].slots))
            return fail("panel gadgets: dispatch (stub)");
    for (unsigned i = 0; i < sizeof(pg_hosts) / sizeof(pg_hosts[0]); ++i)
        if (es2_sim_dispatch_add_service(&pg_dispatch, (uint32_t)(uintptr_t)pg_hosts[i].host, "panel gadgets slot", pg_s_stub,
                                         (void *)(uintptr_t)pg_hosts[i].letter, 0, (uint8_t)pg_hosts[i].slots))
            return fail("panel gadgets: dispatch (slot)");
    const char *error = NULL;
    uint32_t bad = 0;
    if (es2_sim_dispatch_seal(&pg_dispatch, &error, &bad)) return fail("panel gadgets: dispatch seal");
    /* Rows of other packages: replaced by the recording stubs after the seal. */
    for (unsigned i = 0; i < PG_PATCHED; ++i) {
        if (!pg_patched[i].row) continue;
        es2_sim_entry *e = (es2_sim_entry *)es2_sim_dispatch_find(&pg_dispatch, pg_patched[i].va);
        if (!e || e->kind != ES2_SIM_SERVICE) return fail("panel gadgets: service missing");
        e->service = pg_s_stub;
        e->service_user = (void *)(uintptr_t)pg_patched[i].letter;
    }
    return 0;
}

/* ---- the synthetic world */
static uint32_t pg_seed;
static uint32_t pg_rand(uint32_t n) {
    pg_seed = pg_seed * 1103515245u + 12345u;
    return (pg_seed >> 8) % n;
}
static uint32_t pg_alloc(uint32_t n) { return (uint32_t)(uintptr_t)ir_calloc(1, n); }
static void pg_build(void) {
    pg_world *w = &pg_w;
    memset(w, 0, sizeof(*w));
    for (unsigned t = 0; t < 2u; ++t) {
        w->vt[t] = pg_alloc(0x20u);
        pg_put32(w->vt[t], (uint32_t)(uintptr_t)(t ? pg_slot_D : pg_slot_d));
        pg_put32(w->vt[t] + 4u, (uint32_t)(uintptr_t)(t ? pg_slot_R : pg_slot_r));
        pg_put32(w->vt[t] + 0xcu, (uint32_t)(uintptr_t)(t ? pg_slot_I : pg_slot_i));
        w->eb[t] = pg_alloc(0x10u);
        pg_put32(w->eb[t] + 8u, (uint32_t)(uintptr_t)(t ? pg_slot_E : pg_slot_e));
        w->ovt[t] = pg_alloc(0x10u);
        pg_put32(w->ovt[t], (uint32_t)(uintptr_t)(t ? pg_slot_O : pg_slot_o));
        w->owner[t] = pg_alloc(0x20u);
        pg_put32(w->owner[t], w->ovt[t]);
    }
    static const uint8_t redraw[] = {0, 1, 1, 2, 2, 3, 0xff};
    for (unsigned k = 0; k < PG_GADGETS; ++k) {
        uint32_t g = w->gadget[k] = pg_alloc(0x100u);
        for (unsigned f = 0; f < 0x100u; ++f) pg_put8(g + f, (uint8_t)pg_rand(256u));
        pg_put32(g + 0x17u, w->vt[pg_rand(4u) == 0]);
        pg_put8(g + 0x1fu, redraw[pg_rand(7u)]);
        pg_put32(g + 0x20u, w->eb[pg_rand(4u) == 0]);
        pg_put32(g + 0x24u, w->owner[pg_rand(4u) == 0]);
    }
    w->gauge = pg_alloc(0x80u);
    for (unsigned f = 0; f < 0x80u; ++f) pg_put8(w->gauge + f, (uint8_t)pg_rand(256u));
    for (unsigned j = 0; j < 4u; ++j) pg_put32(w->gauge + 0x18u + 4u * j, w->gadget[pg_rand(PG_GADGETS)]);
}

/* ---- the rows; `base` is the event base's offset an entry's argument
 * points at (0: the gadget itself). */
enum { PG_ITEMS, PG_KEY, PG_TOGGLE_REDRAW, PG_TOGGLE_CHANGED, PG_BUTTON_REDRAW, PG_BUTTON_CHANGED, PG_SLIDER_REDRAW,
       PG_RTOGGLE_REDRAW, PG_RTOGGLE_CHANGED, PG_E4433C7, PG_E4433D1, PG_E4433DB, PG_E4433E5, PG_E4433EF, PG_E4433F9,
       PG_E443403, PG_E44340D, PG_E443417, PG_E443421, PG_E44342B, PG_E443435, PG_OPS };
static const struct { uint32_t va; unsigned n; uint32_t base; unsigned target; const char *name; } pg_ops[PG_OPS] = {
    {0x004420a4u, 1, 0, PG_ITEMS, "range items"}, {0x004421c8u, 2, 0, PG_KEY, "range key"},
    {0x0044244cu, 1, 0, PG_TOGGLE_REDRAW, "toggle redraw"}, {0x00442480u, 2, 0, PG_TOGGLE_CHANGED, "toggle changed"},
    {0x004428fcu, 1, 0, PG_BUTTON_REDRAW, "button redraw"}, {0x00442948u, 2, 0, PG_BUTTON_CHANGED, "button changed"},
    {0x00442c10u, 1, 0, PG_SLIDER_REDRAW, "slider redraw"}, {0x00442dbcu, 1, 0, PG_RTOGGLE_REDRAW, "range toggle redraw"},
    {0x00442df0u, 2, 0, PG_RTOGGLE_CHANGED, "range toggle changed"},
    {0x004433c7u, 2, 0x20u, PG_RTOGGLE_CHANGED, "entry 4433c7"}, {0x004433d1u, 1, 0x20u, PG_RTOGGLE_REDRAW, "entry 4433d1"},
    {0x004433dbu, 1, 0x20u, PG_OPS, "entry 4433db"}, {0x004433e5u, 2, 0x3eu, PG_OPS, "entry 4433e5"},
    {0x004433efu, 1, 0x3eu, PG_SLIDER_REDRAW, "entry 4433ef"}, {0x004433f9u, 1, 0x3eu, PG_OPS, "entry 4433f9"},
    {0x00443403u, 2, 0x20u, PG_BUTTON_CHANGED, "entry 443403"}, {0x0044340du, 1, 0x20u, PG_BUTTON_REDRAW, "entry 44340d"},
    {0x00443417u, 1, 0x20u, PG_OPS, "entry 443417"}, {0x00443421u, 2, 0x20u, PG_TOGGLE_CHANGED, "entry 443421"},
    {0x0044342bu, 1, 0x20u, PG_TOGGLE_REDRAW, "entry 44342b"}, {0x00443435u, 1, 0x20u, PG_OPS, "entry 443435"},
};
typedef struct pg_op { unsigned op; uint32_t a[2]; uint32_t mask; } pg_op;
static void pg_args(pg_op *o) {
    const pg_world *w = &pg_w;
    o->a[0] = o->op == PG_ITEMS || o->op == PG_KEY ? w->gauge : w->gadget[random32() % PG_GADGETS] + pg_ops[o->op].base;
    switch (random32() % 6u) {
    case 0: o->a[1] = 0x26u; break;
    case 1: o->a[1] = 0x29u; break;
    case 2: o->a[1] = (random32() << 16) | (random32() % 2u ? 0x26u : 0x29u); break;   /* only the word counts */
    case 3: o->a[1] = 0x26u + random32() % 4u; break;
    default: o->a[1] = random32(); break;
    }
}
/* The EAX the original leaves determined: the last callee's answer, or AL
 * computed from the redraw byte, or nothing (the entry EAX). */
static uint32_t pg_mask(const pg_op *o) {
    unsigned t = pg_ops[o->op].target;
    uint32_t g = o->a[0] - pg_ops[o->op].base;
    uint8_t s = t < PG_OPS && t != PG_ITEMS && t != PG_KEY ? pg_get8(g + 0x1fu) : 0;
    switch (t) {
    case PG_TOGGLE_REDRAW: case PG_RTOGGLE_REDRAW: case PG_BUTTON_REDRAW: return s == 1u || s == 2u ? ~0u : 0xffu;
    case PG_RTOGGLE_CHANGED: return o->a[1] & 1u ? ~0u : 0u;
    default: return ~0u;
    }
}

/* ---- coverage (from the world before a call on the original side) */
enum { PGC_ITEMS, PGC_KEY_26, PGC_KEY_29, PGC_KEY_HIGH, PGC_KEY_OTHER, PGC_TOGGLE_COPY, PGC_TOGGLE_DRAW, PGC_TOGGLE_NONE,
       PGC_TOGGLE_CHANGED, PGC_BUTTON_COPY, PGC_BUTTON_DRAW, PGC_BUTTON_NONE, PGC_BUTTON_CHANGED, PGC_SLIDER_DRAW, PGC_SLIDER_NONE,
       PGC_RTOGGLE_COPY, PGC_RTOGGLE_DRAW, PGC_RTOGGLE_NONE, PGC_RTOGGLE_ODD, PGC_RTOGGLE_EVEN, PGC_ENTRY_OTHER, PGC_COUNT };
static const char *const pg_cv_names[PGC_COUNT] = {
    "items", "key 26", "key 29", "key high bits", "key other", "toggle copy", "toggle draw", "toggle none", "toggle changed",
    "button copy", "button draw", "button none", "button changed", "slider draw", "slider none", "range toggle copy",
    "range toggle draw", "range toggle none", "range toggle odd", "range toggle even", "entry to another row",
};
static unsigned pg_cv[PGC_COUNT];
static void pg_cover(const pg_op *o) {
    unsigned t = pg_ops[o->op].target;
    uint32_t g = o->a[0] - pg_ops[o->op].base;
    uint8_t s = t < PG_OPS && t != PG_ITEMS && t != PG_KEY ? pg_get8(g + 0x1fu) : 0;
    unsigned three = s == 1u ? 0u : s == 2u ? 1u : 2u;
    uint16_t key = (uint16_t)o->a[1];
    switch (t) {
    case PG_ITEMS: ++pg_cv[PGC_ITEMS]; break;
    case PG_KEY:
        ++pg_cv[key == 0x26u ? PGC_KEY_26 : key == 0x29u ? PGC_KEY_29 : PGC_KEY_OTHER];
        if ((key == 0x26u || key == 0x29u) && o->a[1] > 0xffffu) ++pg_cv[PGC_KEY_HIGH];
        break;
    case PG_TOGGLE_REDRAW: ++pg_cv[PGC_TOGGLE_COPY + three]; break;
    case PG_TOGGLE_CHANGED: ++pg_cv[PGC_TOGGLE_CHANGED]; break;
    case PG_BUTTON_REDRAW: ++pg_cv[PGC_BUTTON_COPY + three]; break;
    case PG_BUTTON_CHANGED: ++pg_cv[PGC_BUTTON_CHANGED]; break;
    case PG_SLIDER_REDRAW: ++pg_cv[s == 2u ? PGC_SLIDER_DRAW : PGC_SLIDER_NONE]; break;
    case PG_RTOGGLE_REDRAW: ++pg_cv[PGC_RTOGGLE_COPY + three]; break;
    case PG_RTOGGLE_CHANGED: ++pg_cv[o->a[1] & 1u ? PGC_RTOGGLE_ODD : PGC_RTOGGLE_EVEN]; break;
    default: ++pg_cv[PGC_ENTRY_OTHER]; break;
    }
}

static int pg_side_begin(unsigned side, uint32_t seed, uint32_t answers) {
    lc_reset(side, 47, 0x01000000u + (seed % 0x100000u) * 16u, 1);
    lc_loop.dispatch = &pg_dispatch;
    pg_side = side;
    pg_hash[side] = 0xcbf29ce484222325ull;
    pg_count[side] = 0;
    pg_len[side] = 0;
    pg_text[side][0] = 0;
    pg_answer_seed = answers;
    pg_seed = seed;
    pg_build();
    if (side) {
        if (memcmp(&pg_w, &pg_w0, sizeof(pg_w))) return fail("panel gadgets: the sides' worlds differ (heap order)");
        es2_sim_hud_init(&pg_hud, &lc_loop);
        pg_hud.trace = pg_trace;
        pg_hud.trace_capacity = PG_TRACE;
        es2_sim_hud_bind(&pg_hud);
    } else {
        pg_w0 = pg_w;
    }
    return 0;
}
static uint32_t pg_own_call(uint32_t va, const uint32_t *a, unsigned n) {
    es2_sim_result r;
    uint32_t regs[3] = {0, 0, 0};
    memset(&r, 0, sizeof(r));
    if (es2_sim_call(&pg_dispatch, va, a, n, regs, &r)) pg_bad = 1;
    return r.eax;
}
static unsigned pg_calls[PG_OPS];
static int pg_case(unsigned k) {
    unsigned ops = 1u + random32() % 8u;
    static pg_op list[8];
    uint32_t seed = random32(), answers = random32();
    static uint32_t eax[2][8];
    for (unsigned i = 0; i < ops; ++i) list[i].op = random32() % PG_OPS;
    for (unsigned side = 0; side < 2u; ++side) {
        if (pg_side_begin(side, seed, answers)) return 1;
        for (unsigned i = 0; i < ops && !pg_bad && !lc_bad; ++i) {
            pg_op *o = &list[i];
            if (side == 0) {
                pg_args(o);
                o->mask = pg_mask(o);
                pg_cover(o);
                ++pg_calls[o->op];
            }
            uint32_t d = 0;
            uint32_t r = side ? pg_own_call(pg_ops[o->op].va, o->a, pg_ops[o->op].n) : lc_machine(pg_ops[o->op].va, o->a, pg_ops[o->op].n, &d);
            eax[side][i] = r & o->mask;
        }
        if (pg_bad || lc_bad) { fprintf(stderr, "FAIL: panel gadgets case %u: a call failed (side %u)\n", k, side); return 1; }
        su_take(side);
    }
    for (unsigned i = 0; i < ops; ++i)
        if (eax[0][i] != eax[1][i]) {
            fprintf(stderr, "FAIL: panel gadgets case %u: %s (call %u) EAX %08lx / %08lx\n", k, pg_ops[list[i].op].name, i,
                    (unsigned long)eax[0][i], (unsigned long)eax[1][i]);
            return 1;
        }
    if (su_compare("panel gadgets", k)) {
        fprintf(stderr, "  rows:");
        for (unsigned i = 0; i < ops; ++i) fprintf(stderr, " %s", pg_ops[list[i].op].name);
        fprintf(stderr, "\n");
        return 1;
    }
    if (pg_hash[0] != pg_hash[1] || pg_count[0] != pg_count[1]) {
        fprintf(stderr, "FAIL: panel gadgets case %u: stub calls differ (%u / %u)\n  original: %s\n  own:      %s\n", k, pg_count[0], pg_count[1],
                pg_text[0], pg_text[1]);
        return 1;
    }
    if (pg_hud.trace_count) {
        fprintf(stderr, "FAIL: panel gadgets case %u: %u coupling calls (none expected)\n", k, pg_hud.trace_count);
        return 1;
    }
    return 0;
}

static int panel_gadgets_cases(void) {
    current_function = "panel gadgets";
    es2_sim_identity_memory(&ir_memory);
    es2_sim_rtl_memory(&ir_memory, &su_rtl);
    su_data_pristine = malloc(SU_DATA_SIZE);
    su_snap[0] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    su_snap[1] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    ir_region = ww_alloc(IR_REGION_SIZE);
    lc_stack = VirtualAlloc(NULL, 0x10000u, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    lc_bits = ww_alloc(LC_BITS_SIZE);
    if (!su_data_pristine || !su_snap[0] || !su_snap[1] || !ir_region || !lc_stack || !lc_bits) return fail("panel gadgets: memory");
    if (get32(mapped + (SU_COUNTER - 0x400000u))) return fail("panel gadgets: the counter word is not free");
    su_pristine_take();
    if (!ir_patch_import("KERNEL32.dll", "GetTickCount", (void *)lc_tick)) return fail("panel gadgets: import slot not found");
    for (unsigned i = 0; i < PG_PATCHED; ++i)
        if (pg_patch(pg_patched[i].va, pg_patched[i].stub, pg_saved[i], 1)) return 1;
    if (pg_dispatch_setup()) return 1;
    int bad = 0;
    const unsigned cases = 3000;
    for (unsigned k = 0; !bad && k < cases; ++k) {
        current_case = k; current_case_valid = 1;
        bad = pg_case(k);
    }
    if (!bad) {
        printf("PASS: panel gadgets: %u cases identical\n", cases);
        printf("PANEL GADGETS rows called:");
        for (unsigned i = 0; i < PG_OPS; ++i) printf(" %s %u%s", pg_ops[i].name, pg_calls[i], i + 1u < PG_OPS ? "," : "\n");
        printf("PANEL GADGETS coverage:");
        for (unsigned i = 0; i < PGC_COUNT; ++i) printf(" %s %u%s", pg_cv_names[i], pg_cv[i], i + 1u < PGC_COUNT ? "," : "\n");
        printf("PANEL GADGETS stub effects: redraw %u, table %u, base %u, owner %u\n", pg_effects[PGE_REDRAW], pg_effects[PGE_TABLE],
               pg_effects[PGE_BASE], pg_effects[PGE_OWNER]);
        for (unsigned i = 0; i < PGC_COUNT; ++i)
            if (!pg_cv[i]) { fprintf(stderr, "FAIL: panel gadgets: path '%s' never taken\n", pg_cv_names[i]); bad = 1; }
        for (unsigned i = 0; i < PG_OPS; ++i)
            if (!pg_calls[i]) { fprintf(stderr, "FAIL: panel gadgets: row '%s' never called\n", pg_ops[i].name); bad = 1; }
        for (unsigned i = 0; i < PGE_COUNT; ++i)
            if (!pg_effects[i]) { fprintf(stderr, "FAIL: panel gadgets: stub effect %u never taken\n", i); bad = 1; }
    }
    current_case_valid = 0;
    for (unsigned i = 0; i < PG_PATCHED; ++i) pg_patch(pg_patched[i].va, NULL, pg_saved[i], 0);
    put32(mapped + 0x2cbee4u, 0);
    memcpy(mapped + SU_DATA_RVA, su_data_pristine, SU_DATA_SIZE);
    return bad;
}
