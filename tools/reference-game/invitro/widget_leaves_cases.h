/* The live widget leaves (panel rows 0x434520, the three-shape destructor
 * 0x438e70, 0x4390b8, the mission clock 0x4520c8/ 0x4520d8/0x45216c/0x452240,
 * the layout scale 0x45229c, the hit test 0x4523b0, the rectangle place
 * 0x452454, the widget, slider and root constructors
 * 0x4524a0/0x4524d0/0x4526ec, the slider clamps 0x452520/ 0x452600 and the
 * root's press, release, move, find, draw, redraw and add
 * 0x4527c8/0x452898/0x45297c/0x452a28/0x452a70/0x452ab8/0x452c6c), original
 * machine code against sim/src/widget_leaves.c (--widget-leaves). It reuses
 * startup_cases.h's snapshots and loop_cases.h's virtual time and loop, as
 * hud_widgets_cases.h does.
 *
 * Both sides start from the pristine DATA and an empty heap and build the
 * same synthetic world from the same seeded generator: a root with up to
 * six items (square or round, every state and kind word) and sixteen item
 * slots, a panel with up to five rows and the panel GC 0x4d03a8, the
 * mission clock with its labels, digits 0x4d1db4.. and colon byte
 * 0x49dac4, the display record 0x4cf99c, the scroll globals, the layout
 * shift words 0x4d253a/0x4d253c, a three-shape widget, a surface with a
 * clip record, and eight points (in the items' root, local or panel
 * coordinates, or random). Then 1..8 of the 22 rows run on it. Within the
 * set the original runs its own code (0x4527c8 reaches the real 0x452a28,
 * 0x4523b0 and 0x452ab8); the own side goes through the dispatch to the
 * rows. The other packages' callees (0x4311e0, 0x431210, 0x43122c,
 * 0x431410, 0x438920, 0x496406, 0x477548) are recording stubs on both
 * sides; the method-table slots hold host stubs. The clock 0x467828, the
 * graphics context 0x480c34 and _strcat run for real (the own side: the
 * clock row, the inline construction, the RTL row). Compared: DATA
 * 0x497000..0x6cc000 and the heap, the stub log, EAX by mask, EDX where
 * the row sets it, and the own clock sites against {0x45213d}. Coverage:
 * per-path counters from the original side; every one must be non-zero. */
#pragma once
/* The headers this suite builds on (sim_main.c). */
#include "init_records_cases.h"
#include "startup_cases.h"
#include "loop_cases.h"
#include "es2/sim/hud.h"

#define LF_TRACE 1024u
#define LF_ITEMS 16u
#define LF_POINTS 8u
static es2_sim_entry lf_entries[4096];
static es2_sim_dispatch lf_dispatch;
static es2_sim_hud lf_hud;
static es2_sim_hud_event lf_trace[LF_TRACE];
static int lf_bad;
static unsigned lf_side;

/* ---- the stub log of a side */
static uint64_t lf_hash[2];
static unsigned lf_count[2], lf_letters[2][128];
static char lf_text[2][8192];
static size_t lf_len[2];
static void lf_note(char letter, uint32_t a, uint32_t b, uint32_t c) {
    uint64_t h = lf_hash[lf_side];
    const uint32_t v[4] = {(uint32_t)letter, a, b, c};
    for (unsigned i = 0; i < 4u; ++i) { h ^= v[i]; h *= 0x100000001b3ull; }
    lf_hash[lf_side] = h;
    ++lf_count[lf_side];
    ++lf_letters[lf_side][(unsigned char)letter & 127u];
    if (lf_len[lf_side] + 48u < sizeof(lf_text[0]))
        lf_len[lf_side] += (size_t)sprintf(lf_text[lf_side] + lf_len[lf_side], "%c(%lx,%lx,%lx) ", letter, (unsigned long)a,
                                           (unsigned long)b, (unsigned long)c);
}
static uint32_t lf_get(uint32_t a) { return get32((BYTE *)(uintptr_t)a); }
static uint8_t lf_get8(uint32_t a) { return *(BYTE *)(uintptr_t)a; }
static uint16_t lf_get16(uint32_t a) { return (uint16_t)(lf_get8(a) | lf_get8(a + 1u) << 8); }
static void lf_put8(uint32_t a, uint8_t v) { *(BYTE *)(uintptr_t)a = v; }
static void lf_put16(uint32_t a, uint16_t v) { put16((BYTE *)(uintptr_t)a, v); }
static void lf_put32(uint32_t a, uint32_t v) { put32((BYTE *)(uintptr_t)a, v); }
static uint32_t lf_mix4(uint32_t a) {
    uint32_t h = 0x811c9dc5u;
    for (unsigned i = 0; i < 4u; ++i) h = (h ^ lf_get(a + 4u * i)) * 0x01000193u;
    return h;
}
static uint32_t lf_strhash(uint32_t a) {
    uint32_t h = 0x811c9dc5u;
    for (unsigned i = 0; i < 32u; ++i) { uint8_t b = lf_get8(a + i); h = (h ^ b) * 0x01000193u; if (!b) break; }
    return h;
}

/* ---- the stubs' answers: a generator reset per side */
static uint32_t lf_answer_seed;
static uint32_t lf_answer(uint32_t n) {
    lf_answer_seed = lf_answer_seed * 1103515245u + 12345u;
    return (lf_answer_seed >> 8) % n;
}
/* One stub body for both sides; arguments that point into a side's frame
 * (the copied box, the local point) are logged by their contents. */
static uint32_t lf_stub(char letter, const uint32_t *s) {
    switch (letter) {
    case 'G': lf_note('G', 0, 0, 0); return 0x4400u + lf_answer(0x100u);
    case 'g': lf_note('g', 0, 0, 0); return 0x4500u + lf_answer(0x100u);
    case 'C': lf_note('C', s[0], lf_mix4(s[1]), s[2]); return 0;
    case 'q': lf_note('q', s[0], lf_mix4(s[0]), 0); return 0x5500u | (lf_answer(4u) != 0);
    case 'L': lf_note('L', s[0], s[1], lf_strhash(s[1])); return 0x6600u + lf_answer(0x100u);
    case 'R': lf_note('R', s[0], s[1], 0); return lf_answer(4u) ? lf_answer(3u) : 0xffffffffu;
    case 'X': lf_note('X', s[0], 0, 0); return 0;
    case 'p': lf_note('p', s[0], lf_get(s[1]), lf_get(s[1] + 4u)); return 0x7700u + lf_answer(0x100u);
    case 'u': lf_note('u', s[0], 0, 0); return 0x12340000u | lf_answer(0x10000u);
    case 'e': case 'K': lf_note(letter, s[0], s[1], 0); return 0x8800u + lf_answer(0x100u);
    default: lf_note(letter, s[0], 0, 0); return 0x9900u + lf_answer(0x100u);   /* D d r */
    }
}
/* ---- the original side's stubs */
static uint32_t __cdecl lf_o_push(void) { return lf_stub('G', NULL); }
static uint32_t __cdecl lf_o_pop(void) { return lf_stub('g', NULL); }
static uint32_t __cdecl lf_o_copy(uint32_t a, uint32_t b, uint32_t c) { const uint32_t s[3] = {a, b, c}; return lf_stub('C', s); }
static uint32_t __cdecl lf_o_visible(uint32_t a) { return lf_stub('q', &a); }
static uint32_t __cdecl lf_o_label(uint32_t a, uint32_t b) { const uint32_t s[2] = {a, b}; return lf_stub('L', s); }
static uint32_t __cdecl lf_o_rtti(uint32_t a, uint32_t b) { const uint32_t s[2] = {a, b}; return lf_stub('R', s); }
static uint32_t __cdecl lf_o_delete(uint32_t a) { return lf_stub('X', &a); }
/* The slot stubs: their host addresses are the VAs both sides call. */
static uint32_t __cdecl lf_slot_root(uint32_t a) { return lf_stub('D', &a); }
static uint32_t __cdecl lf_slot_draw(uint32_t a) { return lf_stub('d', &a); }
static uint32_t __cdecl lf_slot_redraw(uint32_t a) { return lf_stub('r', &a); }
static uint32_t __cdecl lf_slot_set(uint32_t a, uint32_t b) { const uint32_t s[2] = {a, b}; return lf_stub('e', s); }
static uint32_t __cdecl lf_slot_get(uint32_t a) { return lf_stub('u', &a); }
static uint32_t __cdecl lf_slot_press(uint32_t a, uint32_t b) { const uint32_t s[2] = {a, b}; return lf_stub('p', s); }
static uint32_t __cdecl lf_slot_shape(uint32_t a, uint32_t b) { const uint32_t s[2] = {a, b}; return lf_stub('K', s); }
static int lf_patch(uint32_t va, void *target, BYTE saved[5], int install) {
    BYTE *site = mapped + (va - 0x400000u);
    DWORD previous;
    if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &previous)) return fail("widget leaves: stub protection");
    if (install) { memcpy(saved, site, 5); site[0] = 0xe9; put32(site + 1, (uint32_t)(uintptr_t)target - (uint32_t)(uintptr_t)(site + 5)); }
    else memcpy(site, saved, 5);
    if (!VirtualProtect(site, 5, previous, &previous)) return fail("widget leaves: stub restore protection");
    FlushInstructionCache(GetCurrentProcess(), site, 5);
    return 0;
}
static const struct { uint32_t va; void *stub; char letter; unsigned slots; int row; } lf_patched[] = {
    {0x004311e0u, (void *)lf_o_push, 'G', 0, 1}, {0x00431210u, (void *)lf_o_pop, 'g', 0, 1},
    {0x0043122cu, (void *)lf_o_copy, 'C', 3, 1}, {0x00431410u, (void *)lf_o_visible, 'q', 1, 0},
    {0x00438920u, (void *)lf_o_label, 'L', 2, 1}, {0x00496406u, (void *)lf_o_rtti, 'R', 2, 1},
    {0x00477548u, (void *)lf_o_delete, 'X', 1, 1},
};
#define LF_PATCHED (sizeof(lf_patched) / sizeof(lf_patched[0]))
static BYTE lf_saved[LF_PATCHED][5];

/* ---- the own side */
static void lf_s_stub(es2_sim_service_call *call, void *user) { call->eax = lf_stub((char)(uintptr_t)user, call->slots); }
static void lf_refuse(const es2_sim_refusal *r, void *user) {
    (void)user;
    fprintf(stderr, "FAIL: own widget leaves: refused call to 0x%08lx (%s): %s\n", (unsigned long)r->va, r->name ? r->name : "?",
            r->reason ? r->reason : "");
    lf_bad = 1;
}
static int lf_dispatch_setup(void) {
    es2_sim_dispatch_init(&lf_dispatch, lf_entries, 4096, lf_refuse, NULL);
    static es2_sim_services s;
    s.memory = &ir_memory;
    s.heap = &ir_heap;
    s.vt = &lc_vt;
    s.fs0 = NULL;
    if (es2_sim_register_services(&lf_dispatch, &s)) return fail("widget leaves: dispatch");
    for (unsigned i = 0; i < LF_PATCHED; ++i)
        if (!lf_patched[i].row &&
            es2_sim_dispatch_add_service(&lf_dispatch, lf_patched[i].va, "widget leaves stub", lf_s_stub, (void *)(uintptr_t)lf_patched[i].letter,
                                         0, (uint8_t)lf_patched[i].slots))
            return fail("widget leaves: dispatch (stub)");
    const struct { void *host; char letter; unsigned slots; } hosts[] = {
        {(void *)lf_slot_root, 'D', 1}, {(void *)lf_slot_draw, 'd', 1}, {(void *)lf_slot_redraw, 'r', 1}, {(void *)lf_slot_set, 'e', 2},
        {(void *)lf_slot_get, 'u', 1}, {(void *)lf_slot_press, 'p', 2}, {(void *)lf_slot_shape, 'K', 2},
    };
    for (unsigned i = 0; i < sizeof(hosts) / sizeof(hosts[0]); ++i)
        if (es2_sim_dispatch_add_service(&lf_dispatch, (uint32_t)(uintptr_t)hosts[i].host, "widget leaves slot", lf_s_stub,
                                         (void *)(uintptr_t)hosts[i].letter, 0, (uint8_t)hosts[i].slots))
            return fail("widget leaves: dispatch (slot)");
    const char *error = NULL;
    uint32_t bad = 0;
    if (es2_sim_dispatch_seal(&lf_dispatch, &error, &bad)) return fail("widget leaves: dispatch seal");
    /* Rows of other packages: replaced by the recording stubs after the seal. */
    for (unsigned i = 0; i < LF_PATCHED; ++i) {
        if (!lf_patched[i].row) continue;
        es2_sim_entry *e = (es2_sim_entry *)es2_sim_dispatch_find(&lf_dispatch, lf_patched[i].va);
        if (!e || e->kind != ES2_SIM_SERVICE) return fail("widget leaves: service missing");
        e->service = lf_s_stub;
        e->service_user = (void *)(uintptr_t)lf_patched[i].letter;
    }
    return 0;
}

/* ---- the synthetic world */
static uint32_t lf_seed;
static uint32_t lf_rand(uint32_t n) {
    lf_seed = lf_seed * 1103515245u + 12345u;
    return (lf_seed >> 8) % n;
}
typedef struct lf_world {
    uint32_t now, root, rvt, ivt, item[LF_ITEMS], panel, pgc, clock, label[3], display, three, svt, shape[3];
    uint32_t surface, clip, rect, rect8, slider, fresh[2], pt[LF_POINTS], hp[4];
} lf_world;
static lf_world lf_w, lf_w0;
static uint32_t lf_alloc(uint32_t n) { return (uint32_t)(uintptr_t)ir_calloc(1, n); }
static uint8_t lf_digit(uint32_t top, uint32_t edge) {
    switch (lf_rand(8u)) {
    case 0: return (uint8_t)edge;
    case 1: return (uint8_t)(edge + 1u);
    case 2: return (uint8_t)lf_rand(256u);   /* a byte the signed compares see as negative or large */
    default: return (uint8_t)('0' + lf_rand(top));
    }
}
static int32_t lf_small(uint32_t n) { return (int32_t)lf_rand(2u * n + 1u) - (int32_t)n; }
/* A point inside (mostly) item j's rectangle, in item coordinates. */
static void lf_inside(const lf_world *w, unsigned j, int32_t *x, int32_t *y) {
    uint32_t it = w->item[j];
    int32_t x0 = (int32_t)lf_get(it), y0 = (int32_t)lf_get(it + 4u), x1 = (int32_t)lf_get(it + 8u), y1 = (int32_t)lf_get(it + 0xcu);
    *x = x0 + (int32_t)lf_rand((uint32_t)(x1 - x0 + 5)) - 2;
    *y = y0 + (int32_t)lf_rand((uint32_t)(y1 - y0 + 5)) - 2;
}
static void lf_build(uint32_t clock_start) {
    lf_world *w = &lf_w;
    memset(w, 0, sizeof(*w));
    w->now = (clock_start - get32(mapped + 0xd3b78u)) >> 4;
    /* The items and their method tables. */
    w->ivt = lf_alloc(0x20u);
    lf_put32(w->ivt, (uint32_t)(uintptr_t)lf_slot_draw);
    lf_put32(w->ivt + 4u, (uint32_t)(uintptr_t)lf_slot_redraw);
    lf_put32(w->ivt + 8u, (uint32_t)(uintptr_t)lf_slot_set);
    lf_put32(w->ivt + 0x10u, (uint32_t)(uintptr_t)lf_slot_get);
    lf_put32(w->ivt + 0x18u, (uint32_t)(uintptr_t)lf_slot_press);
    static const uint16_t states[] = {0, 0, 0, 1, 1, 2, 3};
    for (unsigned j = 0; j < LF_ITEMS; ++j) {
        uint32_t it = w->item[j] = lf_alloc(0x40u);
        int32_t x0 = (int32_t)lf_rand(200u), y0 = (int32_t)lf_rand(150u), wd = 1 + (int32_t)lf_rand(60u), ht = 1 + (int32_t)lf_rand(40u);
        if (lf_rand(10u) == 0) wd = -(int32_t)lf_rand(5u);   /* an empty or inverted box */
        lf_put32(it, (uint32_t)x0);
        lf_put32(it + 4u, (uint32_t)y0);
        lf_put32(it + 8u, (uint32_t)(x0 + wd));
        lf_put32(it + 0xcu, (uint32_t)(y0 + ht));
        lf_put8(it + 0x10u, lf_rand(3u) == 0);
        lf_put16(it + 0x11u, (uint16_t)(x0 + wd / 2 + lf_small(3u)));
        lf_put16(it + 0x13u, (uint16_t)(y0 + ht / 2 + lf_small(3u)));
        lf_put16(it + 0x15u, (uint16_t)(lf_rand(8u) ? lf_rand(40u) : 0x8000u + lf_rand(0x10u)));
        lf_put32(it + 0x17u, w->ivt);
        lf_put16(it + 0x1bu, states[lf_rand(7u)]);
        lf_put16(it + 0x1du, (uint16_t)(lf_rand(2u) ? 1u : lf_rand(3u)));
        for (unsigned k = 0x1fu; k < 0x40u; ++k) lf_put8(it + k, (uint8_t)lf_rand(256u));
    }
    /* The root: 0..6 items (a negative count word in a few worlds). */
    w->rvt = lf_alloc(0x20u);
    lf_put32(w->rvt, (uint32_t)(uintptr_t)lf_slot_root);
    uint32_t r = w->root = lf_alloc(0x300u);
    lf_put32(r, (uint32_t)lf_small(40u));
    lf_put32(r + 4u, (uint32_t)lf_small(40u));
    lf_put32(r + 8u, lf_get(r) + 300u);
    lf_put32(r + 0xcu, lf_get(r + 4u) + 200u);
    lf_put32(r + 0x17u, w->rvt);
    lf_put32(r + 0x227u, lf_rand(3u));
    lf_put16(r + 0x254u, (uint16_t)(lf_rand(12u) == 0 ? 0xffffu - lf_rand(2u) : lf_rand(7u)));
    for (unsigned j = 0; j < LF_ITEMS; ++j) lf_put32(r + 0x256u + 4u * j, w->item[lf_rand(4u) ? j : lf_rand(LF_ITEMS)]);
    /* The mouse globals. */
    lf_put16(0x0049dbdcu, (uint16_t)(lf_rand(3u) == 0 ? 0xffffu : lf_rand(7u)));
    lf_put8(0x0049dbdeu, (uint8_t)lf_rand(2u));
    lf_put8(0x0049db6cu, (uint8_t)lf_rand(8u));
    lf_put8(0x0049dbecu, (uint8_t)(lf_rand(2u) ? 0u : 1u + lf_rand(3u)));
    lf_put32(0x004d2556u, (uint32_t)lf_small(30u));
    lf_put32(0x004d255au, (uint32_t)lf_small(30u));
    /* The panel and its GC. */
    w->pgc = lf_alloc(0x230u);
    lf_put32(w->pgc + 0x220u, (uint32_t)lf_small(50u));
    lf_put32(w->pgc + 0x224u, (uint32_t)lf_small(50u));
    put32(mapped + 0xd03a8u, w->pgc);
    w->panel = lf_alloc(0x260u);
    for (unsigned j = 0; j < 5u; ++j) lf_put32(w->panel + 0x246u + 4u * j, w->item[lf_rand(LF_ITEMS)]);
    lf_put32(w->panel + 0x25au, lf_rand(10u) == 0 ? 0xffffffffu : lf_rand(6u));
    /* The mission clock, its labels, digits and colon. */
    w->display = lf_alloc(0x20u);
    lf_put8(w->display + 0x1cu, lf_rand(4u) == 0);
    put32(mapped + 0xcf99cu, w->display);
    w->clock = lf_alloc(0x100u);
    for (unsigned k = 0; k < 3u; ++k) {
        w->label[k] = lf_alloc(0x20u);
        for (unsigned f = 0; f < 0x20u; f += 4u) lf_put32(w->label[k] + f, lf_rand(400u));
    }
    lf_put8(w->clock + 0xb0u, (uint8_t)(lf_rand(2u) ? 0u : lf_rand(3u)));
    lf_put32(w->clock + 0xb1u, w->label[0]);
    lf_put32(w->clock + 0xb5u, w->label[1]);
    lf_put32(w->clock + 0xb9u, w->label[2]);
    lf_put32(w->clock + 0xbdu, lf_rand(8u) == 0 ? 0xffffffffu : w->now + lf_rand(80u) - 40u);
    static const uint8_t colons[] = {0, 1, 0, 1, 2, 3, 0xff};
    lf_put8(0x0049dac4u, colons[lf_rand(7u)]);
    lf_put8(0x004d1db4u, lf_digit(10u, '9'));
    lf_put8(0x004d1db5u, lf_digit(10u, '9'));
    lf_put8(0x004d1db6u, (uint8_t)(lf_rand(10u) == 0 ? 'X' : 0));
    lf_put8(0x004d1db7u, lf_digit(6u, '5'));
    lf_put8(0x004d1db8u, lf_digit(10u, '9'));
    lf_put8(0x004d1db9u, 0);
    lf_put8(0x004d1dbau, ':');
    lf_put8(0x004d1dbbu, 0);
    for (unsigned k = 0; k < 16u; ++k) lf_put8(0x004d1dbcu + k, (uint8_t)lf_rand(256u));
    /* The layout shifts (a count of 32 or more: the shl's modulo). */
    static const uint16_t shifts[] = {0, 1, 1, 2, 3, 31, 33, 0x101u};
    put16(mapped + 0xd253au, shifts[lf_rand(8u)]);
    put16(mapped + 0xd253cu, shifts[lf_rand(8u)]);
    /* The three-shape widget. */
    w->svt = lf_alloc(0x20u);
    lf_put32(w->svt + 0x18u, (uint32_t)(uintptr_t)lf_slot_shape);
    w->three = lf_alloc(0x40u);
    for (unsigned k = 0; k < 0x40u; ++k) lf_put8(w->three + k, (uint8_t)lf_rand(256u));
    lf_put32(w->three + 0x2cu, lf_rand(4u) ? 0x00c00000u + lf_rand(0x1000u) : 0u);
    for (unsigned k = 0; k < 3u; ++k) {
        w->shape[k] = lf_rand(3u) ? lf_alloc(0x10u) : 0u;
        if (w->shape[k]) lf_put32(w->shape[k], w->svt);
        lf_put32(w->three + 0x30u + 4u * k, w->shape[k]);
    }
    /* The surface's clip record for 0x480c34. */
    w->clip = lf_alloc(0x240u);
    for (unsigned k = 0; k < 0x240u; k += 4u) lf_put32(w->clip + k, lf_rand(0x10000u));
    w->surface = lf_alloc(0x80u);
    lf_put32(w->surface + 0x40u, w->clip);
    put32(mapped + 0x2c5ed0u, w->surface);
    /* Rectangles, a slider and two fresh objects. */
    w->rect = lf_alloc(0x10u);
    for (unsigned k = 0; k < 16u; k += 4u) lf_put32(w->rect + k, (uint32_t)lf_small(300u));
    w->rect8 = lf_alloc(0x28u);
    for (unsigned k = 0; k < 0x28u; k += 4u) lf_put32(w->rect8 + k, lf_rand(3u) ? (uint32_t)lf_small(500u) : lf_rand(0xffffffffu));
    w->slider = lf_alloc(0x40u);
    {
        int32_t x0 = lf_small(100u), y0 = lf_small(100u);
        lf_put32(w->slider, (uint32_t)x0);
        lf_put32(w->slider + 4u, (uint32_t)y0);
        lf_put32(w->slider + 8u, (uint32_t)(x0 + (int32_t)lf_rand(120u)));
        lf_put32(w->slider + 0xcu, (uint32_t)(y0 + (int32_t)lf_rand(120u)));
        lf_put16(w->slider + 0x28u, (uint16_t)lf_rand(0x10000u));
        lf_put16(w->slider + 0x2au, (uint16_t)lf_rand(0x10000u));
        lf_put16(w->slider + 0x2cu, (uint16_t)(lf_rand(6u) ? lf_rand(30u) : 0xfff0u + lf_rand(16u)));
    }
    for (unsigned k = 0; k < 2u; ++k) {
        w->fresh[k] = lf_alloc(0x260u);
        for (unsigned f = 0; f < 0x260u; ++f) lf_put8(w->fresh[k] + f, (uint8_t)lf_rand(256u));
    }
    /* The points: root coordinates of an item (the press and release
     * localise them), item coordinates, panel rows, or random. */
    for (unsigned k = 0; k < LF_POINTS; ++k) {
        uint32_t p = w->pt[k] = lf_alloc(8u);
        int32_t x = 0, y = 0;
        lf_inside(w, lf_rand(4u) ? lf_get16(r + 0x254u) < 16u && lf_get16(r + 0x254u) ? lf_rand(lf_get16(r + 0x254u)) : lf_rand(LF_ITEMS)
                                    : lf_rand(LF_ITEMS), &x, &y);
        switch (lf_rand(6u)) {
        case 0: case 1:
            x += (int32_t)lf_get(r);
            y += (int32_t)lf_get(r + 4u);
            if (lf_get8(0x0049dbecu)) { x -= (int32_t)lf_get(0x004d2556u); y -= (int32_t)lf_get(0x004d255au); }
            break;
        case 2: case 3: break;
        case 4: y -= (int32_t)lf_get(w->pgc + 0x224u); break;
        default: x = lf_small(300u); y = lf_small(300u); break;
        }
        lf_put32(p, (uint32_t)x);
        lf_put32(p + 4u, (uint32_t)y);
    }
    /* A point at (mostly) each of the first four items, for the hit test. */
    for (unsigned j = 0; j < 4u; ++j) {
        int32_t x = 0, y = 0;
        w->hp[j] = lf_alloc(8u);
        lf_inside(w, j, &x, &y);
        lf_put32(w->hp[j], (uint32_t)x);
        lf_put32(w->hp[j] + 4u, (uint32_t)y);
    }
}

/* ---- the rows */
enum { LF_PANEL_ROWS, LF_DESTROY, LF_PLUS4, LF_CLOCK_DRAW, LF_CLOCK_FRAME, LF_CLOCK_TICK, LF_TIME_TEXT, LF_SCALE4, LF_HIT,
       LF_RECT_PLACE, LF_WIDGET_INIT, LF_SLIDER_INIT, LF_SLIDER_H, LF_SLIDER_V, LF_ROOT_INIT, LF_PRESS, LF_RELEASE, LF_MOVE,
       LF_FIND, LF_DRAW, LF_REDRAW, LF_ADD, LF_OPS };
static const struct { uint32_t va; unsigned n; uint32_t mask; int edx; const char *name; } lf_ops[LF_OPS] = {
    {0x00434520u, 2, ~0u, 0, "panel rows"}, {0x00438e70u, 2, 0u, 0, "three-shape destroy"},
    {0x004390b8u, 1, ~0u, 0, "plus 4"}, {0x004520c8u, 1, ~0u, 0, "clock draw"},
    {0x004520d8u, 1, ~0u, 0, "clock frame"}, {0x0045216cu, 1, ~0u, 0, "clock tick"},
    {0x00452240u, 0, ~0u, 0, "time text"}, {0x0045229cu, 1, ~0u, 0, "layout scale"},
    {0x004523b0u, 2, ~0u, 0, "hit"}, {0x00452454u, 2, ~0u, 1, "rect place"},
    {0x004524a0u, 2, ~0u, 1, "widget init"}, {0x004524d0u, 6, ~0u, 1, "slider init"},
    {0x00452520u, 2, ~0u, 0, "slider h"}, {0x00452600u, 2, ~0u, 0, "slider v"},
    {0x004526ecu, 2, ~0u, 1, "root init"}, {0x004527c8u, 2, ~0u, 0, "root press"},
    {0x00452898u, 2, ~0u, 0, "root release"}, {0x0045297cu, 2, ~0u, 0, "root move"},
    {0x00452a28u, 2, ~0u, 0, "root find"}, {0x00452a70u, 1, ~0u, 0, "root draw"},
    {0x00452ab8u, 2, ~0u, 0, "item redraw"}, {0x00452c6cu, 2, ~0u, 1, "root add"},
};
typedef struct lf_op { unsigned op; uint32_t a[6]; uint32_t mask; } lf_op;
static uint32_t lf_point(void) { return lf_w.pt[random32() % LF_POINTS]; }
static void lf_args(lf_op *o) {
    const lf_world *w = &lf_w;
    memset(o->a, 0, sizeof(o->a));
    switch (o->op) {
    case LF_PANEL_ROWS: o->a[0] = w->panel; o->a[1] = lf_point(); break;
    case LF_DESTROY: o->a[0] = random32() % 6u ? w->three : 0u; o->a[1] = random32() % 3u ? random32() % 4u : random32(); break;
    case LF_PLUS4: o->a[0] = random32(); break;
    case LF_CLOCK_DRAW: case LF_CLOCK_FRAME: case LF_CLOCK_TICK: o->a[0] = w->clock; break;
    case LF_SCALE4: o->a[0] = w->rect8; break;
    case LF_HIT:
        if (random32() % 2u) { unsigned j = random32() % 4u; o->a[0] = w->item[j]; o->a[1] = w->hp[j]; break; }
        o->a[0] = w->item[random32() % LF_ITEMS]; o->a[1] = lf_point();
        break;
    case LF_RECT_PLACE: o->a[0] = random32() % 2u ? w->rect : w->item[random32() % LF_ITEMS]; o->a[1] = lf_point(); break;
    case LF_WIDGET_INIT: o->a[0] = w->fresh[random32() % 2u]; o->a[1] = w->rect; break;
    case LF_SLIDER_INIT:
        o->a[0] = w->fresh[random32() % 2u]; o->a[1] = w->rect;
        for (unsigned k = 2; k < 6u; ++k) o->a[k] = random32() % 2u ? random32() % 0x200u : random32();
        break;
    case LF_SLIDER_H: case LF_SLIDER_V: o->a[0] = random32() % 4u ? w->slider : w->item[random32() % LF_ITEMS]; o->a[1] = lf_point(); break;
    case LF_ROOT_INIT: o->a[0] = w->fresh[random32() % 2u]; o->a[1] = random32() % 2u ? w->rect : w->pt[0]; break;
    case LF_PRESS: case LF_RELEASE: case LF_MOVE: case LF_FIND: o->a[0] = w->root; o->a[1] = lf_point(); break;
    case LF_DRAW: o->a[0] = w->root; break;
    case LF_ADD:
        /* The add stores at the signed count and counts up: past the
         * sixteen slots (or at -1, onto the count word) the root's later
         * loops would walk garbage; such an add becomes a find. */
        if ((int16_t)lf_get16(w->root + 0x254u) < 0 || (int16_t)lf_get16(w->root + 0x254u) >= (int16_t)LF_ITEMS) {
            o->op = LF_FIND;
            lf_args(o);
            return;
        }
        o->a[0] = w->root; o->a[1] = w->item[random32() % LF_ITEMS];
        break;
    case LF_REDRAW: o->a[0] = w->root; o->a[1] = w->item[random32() % LF_ITEMS]; break;
    default: break;
    }
}
/* LF_ROOT_INIT's second rectangle (a point and what follows it) must stay
 * inside the allocation: the points are 8 bytes, so the rectangle is read
 * from w->rect when the point would be short. */
static void lf_fix(lf_op *o) {
    if (o->op == LF_ROOT_INIT && o->a[1] != lf_w.rect) o->a[1] = lf_w.rect;
}

/* ---- coverage (from the world before and after a call on the original side) */
enum { LFC_ROWS_NONE, LFC_ROW_IN, LFC_ROW_OUT, LFC_DESTROY_NULL, LFC_DESTROY_KEEP, LFC_DESTROY_DELETE, LFC_SHAPE_NULL, LFC_SHAPE_LIVE,
       LFC_FRAME_REDRAW, LFC_FRAME_COPY, LFC_FRAME_TICK, LFC_FRAME_WAIT, LFC_COLON_ON, LFC_COLON_OFF, LFC_MINUTE_CARRY, LFC_HOUR_STEP,
       LFC_HOUR_CARRY, LFC_HOUR_WRAP, LFC_LABELS, LFC_LABEL_COLON, LFC_HIDDEN, LFC_SHIFT_WIDE, LFC_HIT_SQUARE_IN, LFC_HIT_SQUARE_OUT,
       LFC_HIT_ROUND_IN, LFC_HIT_ROUND_OUT, LFC_H_LEFT, LFC_H_RIGHT, LFC_H_INSIDE, LFC_V_TOP, LFC_V_BOTTOM, LFC_V_INSIDE,
       LFC_PRESS_MISS, LFC_PRESS_GRAB, LFC_PRESS_PLAIN, LFC_PRESS_SCROLLED, LFC_RELEASE_MISS, LFC_RELEASE_SAME, LFC_RELEASE_FLAG,
       LFC_RELEASE_OTHER, LFC_MOVE_IDLE, LFC_MOVE_ON, LFC_MOVE_OFF, LFC_MOVE_NONE, LFC_FIND_HIT, LFC_FIND_NONE, LFC_FIND_DISABLED,
       LFC_DRAW_EMPTY, LFC_DRAW_ITEMS, LFC_ADD, LFC_INIT, LFC_COUNT };
static const char *const lf_cv_names[LFC_COUNT] = {
    "rows none", "row in", "row out", "destroy null", "destroy keep", "destroy delete", "shape null", "shape live",
    "frame redraw", "frame copy", "frame tick", "frame wait", "colon on", "colon off", "minute carry", "hour step",
    "hour carry", "hour wrap", "labels", "label colon", "hidden", "shift wide", "hit square in", "hit square out",
    "hit round in", "hit round out", "h left", "h right", "h inside", "v top", "v bottom", "v inside",
    "press miss", "press grab", "press plain", "press scrolled", "release miss", "release same", "release flag",
    "release other", "move idle", "move on", "move off", "move none", "find hit", "find none", "find disabled",
    "draw empty", "draw items", "add", "init",
};
static unsigned lf_cv[LFC_COUNT];
typedef struct lf_pre {
    uint8_t colon, mm1, mm0, hh1, hh0, b0, redraw, scrolled, flags;
    uint16_t pressed, count;
    unsigned letters[128];
} lf_pre;
static void lf_take_pre(lf_pre *p) {
    p->colon = lf_get8(0x0049dac4u);
    p->hh0 = lf_get8(0x004d1db4u); p->hh1 = lf_get8(0x004d1db5u);
    p->mm0 = lf_get8(0x004d1db7u); p->mm1 = lf_get8(0x004d1db8u);
    p->b0 = lf_get8(lf_w.clock + 0xb0u);
    p->redraw = lf_get8(lf_w.display + 0x1cu);
    p->scrolled = lf_get8(0x0049dbecu);
    p->flags = lf_get8(0x0049db6cu);
    p->pressed = lf_get16(0x0049dbdcu);
    p->count = lf_get16(lf_w.root + 0x254u);
    memcpy(p->letters, lf_letters[0], sizeof(p->letters));
}
static unsigned lf_new(const lf_pre *p, char letter) { return lf_letters[0][(unsigned)letter] - p->letters[(unsigned)letter]; }
static void lf_cover_tick(const lf_pre *p) {
    uint8_t colon = (uint8_t)(p->colon ^ 1u);
    ++lf_cv[colon == 1u ? LFC_COLON_ON : LFC_COLON_OFF];
    if (colon == 1u && (int8_t)(p->mm1 + 1) > 0x39) {
        ++lf_cv[LFC_MINUTE_CARRY];
        if ((int8_t)(p->mm0 + 1) > 0x35) {
            ++lf_cv[LFC_HOUR_STEP];
            if ((int8_t)(p->hh1 + 1) > 0x39) ++lf_cv[(int8_t)p->hh0 == 0x39 ? LFC_HOUR_WRAP : LFC_HOUR_CARRY];
        }
    }
}
static void lf_cover(const lf_op *o, const lf_pre *p, uint32_t eax) {
    switch (o->op) {
    case LF_PANEL_ROWS: {
        int32_t n = (int32_t)lf_get(lf_w.panel + 0x25au);
        if (n <= 0) ++lf_cv[LFC_ROWS_NONE];
        for (int32_t i = 0; i < n; ++i) ++lf_cv[lf_get16(lf_get(lf_w.panel + 0x246u + 4u * (uint32_t)i) + 0x1bu) ? LFC_ROW_OUT : LFC_ROW_IN];
        break;
    }
    case LF_DESTROY:
        if (!o->a[0]) { ++lf_cv[LFC_DESTROY_NULL]; break; }
        ++lf_cv[o->a[1] & 1u ? LFC_DESTROY_DELETE : LFC_DESTROY_KEEP];
        for (unsigned k = 0; k < 3u; ++k) ++lf_cv[lf_w.shape[k] ? LFC_SHAPE_LIVE : LFC_SHAPE_NULL];
        break;
    case LF_CLOCK_FRAME:
        if (p->redraw) ++lf_cv[LFC_FRAME_REDRAW];
        if (p->b0 || p->redraw) ++lf_cv[LFC_FRAME_COPY];
        if (lf_get8(lf_w.clock + 0xb0u) == 1u) { ++lf_cv[LFC_FRAME_TICK]; lf_cover_tick(p); } else ++lf_cv[LFC_FRAME_WAIT];
        break;
    case LF_CLOCK_DRAW: case LF_CLOCK_TICK: lf_cover_tick(p); break;
    case LF_SCALE4: if ((lf_get16(0x004d253au) | lf_get16(0x004d253cu)) >= 32u) ++lf_cv[LFC_SHIFT_WIDE]; break;
    case LF_HIT: ++lf_cv[lf_get8(o->a[0] + 0x10u) ? (eax ? LFC_HIT_ROUND_IN : LFC_HIT_ROUND_OUT) : (eax ? LFC_HIT_SQUARE_IN : LFC_HIT_SQUARE_OUT)]; break;
    case LF_SLIDER_H: {
        int32_t x = (int32_t)lf_get(o->a[1]), x0 = (int32_t)lf_get(o->a[0]);
        int32_t right = (int32_t)(lf_get(o->a[0] + 8u) - (uint32_t)(int32_t)(int16_t)lf_get16(o->a[0] + 0x2cu));
        ++lf_cv[x < x0 ? LFC_H_LEFT : right < x ? LFC_H_RIGHT : LFC_H_INSIDE];
        break;
    }
    case LF_SLIDER_V: {
        int32_t y = (int32_t)lf_get(o->a[1] + 4u);
        int32_t top = (int32_t)((uint32_t)(int32_t)(int16_t)lf_get16(o->a[0] + 0x2cu) + lf_get(o->a[0] + 4u));
        ++lf_cv[top > y ? LFC_V_TOP : y > (int32_t)lf_get(o->a[0] + 0xcu) ? LFC_V_BOTTOM : LFC_V_INSIDE];
        break;
    }
    case LF_PRESS:
        if (lf_get16(0x0049dbdcu) == 0xffffu) ++lf_cv[LFC_PRESS_MISS];
        else ++lf_cv[lf_new(p, 'p') ? LFC_PRESS_GRAB : LFC_PRESS_PLAIN];
        if (p->scrolled && lf_new(p, 'r')) ++lf_cv[LFC_PRESS_SCROLLED];
        break;
    case LF_RELEASE:
        if (!lf_new(p, 'r')) ++lf_cv[LFC_RELEASE_MISS];
        else if (!lf_new(p, 'u')) ++lf_cv[LFC_RELEASE_OTHER];
        else ++lf_cv[p->flags & 2u ? LFC_RELEASE_FLAG : LFC_RELEASE_SAME];
        break;
    case LF_MOVE:
        if (p->pressed == 0xffffu) ++lf_cv[LFC_MOVE_IDLE];
        else if (!lf_new(p, 'r')) ++lf_cv[LFC_MOVE_NONE];
        else ++lf_cv[lf_get16(lf_get(lf_w.root + 0x256u + 4u * (uint32_t)(int32_t)(int16_t)p->pressed) + 0x1bu) == 1u ? LFC_MOVE_ON : LFC_MOVE_OFF];
        break;
    case LF_FIND: {
        if (eax == 0xffffffffu) ++lf_cv[LFC_FIND_NONE];
        else ++lf_cv[LFC_FIND_HIT];
        /* A hit item passed over for its state word 2 before the answer. */
        uint32_t end = eax == 0xffffffffu ? (uint32_t)(int32_t)(int16_t)p->count : eax;
        for (uint32_t i = 0; (int32_t)i < (int32_t)end && i < LF_ITEMS; ++i)
            if (lf_get16(lf_get(lf_w.root + 0x256u + 4u * i) + 0x1bu) == 2u) { ++lf_cv[LFC_FIND_DISABLED]; break; }
        break;
    }
    case LF_DRAW: ++lf_cv[(int16_t)p->count > 0 ? LFC_DRAW_ITEMS : LFC_DRAW_EMPTY]; break;
    case LF_ADD: ++lf_cv[LFC_ADD]; break;
    case LF_WIDGET_INIT: case LF_SLIDER_INIT: case LF_ROOT_INIT: ++lf_cv[LFC_INIT]; break;
    default: break;
    }
    if (o->op == LF_CLOCK_DRAW || o->op == LF_CLOCK_TICK || (o->op == LF_CLOCK_FRAME && lf_get8(lf_w.clock + 0xb0u) == 1u)) {
        if (!lf_new(p, 'L')) ++lf_cv[LFC_HIDDEN];
        else { ++lf_cv[LFC_LABELS]; if (lf_new(p, 'L') == 3u) ++lf_cv[LFC_LABEL_COLON]; }
    }
}

static int lf_side_begin(unsigned side, uint32_t seed, uint32_t answers) {
    uint32_t clock = 0x01000000u + (seed % 0x100000u) * 16u;
    lc_reset(side, 47, clock, 1);
    lc_loop.dispatch = &lf_dispatch;
    lf_side = side;
    lf_hash[side] = 0xcbf29ce484222325ull;
    lf_count[side] = 0;
    memset(lf_letters[side], 0, sizeof(lf_letters[side]));
    lf_len[side] = 0;
    lf_text[side][0] = 0;
    lf_answer_seed = answers;
    lf_seed = seed;
    lf_build(clock);
    if (side) {
        if (memcmp(&lf_w, &lf_w0, sizeof(lf_w))) return fail("widget leaves: the sides' worlds differ (heap order)");
        es2_sim_hud_init(&lf_hud, &lc_loop);
        lf_hud.trace = lf_trace;
        lf_hud.trace_capacity = LF_TRACE;
        es2_sim_hud_bind(&lf_hud);
    } else {
        lf_w0 = lf_w;
    }
    return 0;
}
static uint32_t lf_own_call(uint32_t va, const uint32_t *a, unsigned n, uint32_t *edx) {
    es2_sim_result r;
    uint32_t regs[3] = {0, 0, 0};
    memset(&r, 0, sizeof(r));
    if (es2_sim_call(&lf_dispatch, va, a, n, regs, &r)) lf_bad = 1;
    *edx = r.edx;
    return r.eax;
}
static unsigned lf_calls[LF_OPS];
static int lf_case(unsigned k) {
    unsigned ops = 1u + random32() % 8u;
    static lf_op list[8];
    uint32_t seed = random32(), answers = random32();
    static uint32_t eax[2][8], edx[2][8];
    for (unsigned i = 0; i < ops; ++i) list[i].op = random32() % LF_OPS;
    for (unsigned side = 0; side < 2u; ++side) {
        if (lf_side_begin(side, seed, answers)) return 1;
        for (unsigned i = 0; i < ops && !lf_bad && !lc_bad; ++i) {
            lf_op *o = &list[i];
            lf_pre pre;
            if (side == 0) {
                lf_args(o);
                lf_fix(o);
                lf_take_pre(&pre);
                /* The move with no item pressed returns the entry EAX. */
                o->mask = o->op == LF_MOVE && pre.pressed == 0xffffu ? 0u : lf_ops[o->op].mask;
            }
            uint32_t d = 0;
            uint32_t r = side ? lf_own_call(lf_ops[o->op].va, o->a, lf_ops[o->op].n, &d) : lc_machine(lf_ops[o->op].va, o->a, lf_ops[o->op].n, &d);
            eax[side][i] = r & o->mask;
            edx[side][i] = lf_ops[o->op].edx ? d : 0u;
            if (side == 0) { lf_cover(o, &pre, r); ++lf_calls[o->op]; }
        }
        if (lf_bad || lc_bad) { fprintf(stderr, "FAIL: widget leaves case %u: a call failed (side %u)\n", k, side); return 1; }
        su_take(side);
    }
    for (unsigned i = 0; i < ops; ++i)
        if (eax[0][i] != eax[1][i] || edx[0][i] != edx[1][i]) {
            fprintf(stderr, "FAIL: widget leaves case %u: %s (call %u) EAX %08lx / %08lx, EDX %08lx / %08lx\n", k, lf_ops[list[i].op].name, i,
                    (unsigned long)eax[0][i], (unsigned long)eax[1][i], (unsigned long)edx[0][i], (unsigned long)edx[1][i]);
            return 1;
        }
    if (su_compare("widget leaves", k)) {
        fprintf(stderr, "  rows:");
        for (unsigned i = 0; i < ops; ++i) fprintf(stderr, " %s", lf_ops[list[i].op].name);
        fprintf(stderr, "\n");
        return 1;
    }
    if (lf_hash[0] != lf_hash[1] || lf_count[0] != lf_count[1]) {
        fprintf(stderr, "FAIL: widget leaves case %u: stub calls differ (%u / %u)\n  original: %s\n  own:      %s\n", k, lf_count[0], lf_count[1],
                lf_text[0], lf_text[1]);
        return 1;
    }
    /* The own coupling sites: only the clock at 0x45213d. */
    unsigned n = lf_hud.trace_count < LF_TRACE ? lf_hud.trace_count : LF_TRACE;
    for (unsigned i = 0; i < n; ++i)
        if (lf_trace[i].kind != ES2_SIM_HUD_CLOCK || lf_trace[i].site != 0x0045213du) {
            fprintf(stderr, "FAIL: widget leaves case %u: coupling call of kind %u at 0x%08lx (only the clock at 0x45213d expected)\n", k,
                    (unsigned)lf_trace[i].kind, (unsigned long)lf_trace[i].site);
            return 1;
        }
    return 0;
}

static int widget_leaves_cases(void) {
    current_function = "widget leaves";
    es2_sim_identity_memory(&ir_memory);
    es2_sim_rtl_memory(&ir_memory, &su_rtl);
    su_data_pristine = malloc(SU_DATA_SIZE);
    su_snap[0] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    su_snap[1] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    ir_region = ww_alloc(IR_REGION_SIZE);
    lc_stack = VirtualAlloc(NULL, 0x10000u, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    lc_bits = ww_alloc(LC_BITS_SIZE);
    if (!su_data_pristine || !su_snap[0] || !su_snap[1] || !ir_region || !lc_stack || !lc_bits) return fail("widget leaves: memory");
    if (get32(mapped + (SU_COUNTER - 0x400000u))) return fail("widget leaves: the counter word is not free");
    su_pristine_take();
    if (!ir_patch_import("KERNEL32.dll", "GetTickCount", (void *)lc_tick)) return fail("widget leaves: import slot not found");
    if (!ir_patch_import("cw3220.dll", "_strcat", (void *)su_strcat)) return fail("widget leaves: string import slot not found");
    for (unsigned i = 0; i < LF_PATCHED; ++i)
        if (lf_patch(lf_patched[i].va, lf_patched[i].stub, lf_saved[i], 1)) return 1;
    if (lf_dispatch_setup()) return 1;
    int bad = 0;
    const unsigned cases = 4000;
    for (unsigned k = 0; !bad && k < cases; ++k) {
        current_case = k; current_case_valid = 1;
        bad = lf_case(k);
    }
    if (!bad) {
        printf("PASS: widget leaves: %u cases identical\n", cases);
        printf("WIDGET LEAVES rows called:");
        for (unsigned i = 0; i < LF_OPS; ++i) printf(" %s %u%s", lf_ops[i].name, lf_calls[i], i + 1u < LF_OPS ? "," : "\n");
        printf("WIDGET LEAVES coverage:");
        for (unsigned i = 0; i < LFC_COUNT; ++i) printf(" %s %u%s", lf_cv_names[i], lf_cv[i], i + 1u < LFC_COUNT ? "," : "\n");
        for (unsigned i = 0; i < LFC_COUNT; ++i)
            if (!lf_cv[i]) { fprintf(stderr, "FAIL: widget leaves: path '%s' never taken\n", lf_cv_names[i]); bad = 1; }
    }
    current_case_valid = 0;
    for (unsigned i = 0; i < LF_PATCHED; ++i) lf_patch(lf_patched[i].va, NULL, lf_saved[i], 0);
    put32(mapped + 0x2cbee4u, 0);
    memcpy(mapped + SU_DATA_RVA, su_data_pristine, SU_DATA_SIZE);
    return bad;
}
