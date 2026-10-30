/* The bar draws 0x439118/0x439398/0x4395e8 with their fills, the LED segments
 * 0x439758, the shields gauge's draw 0x443758, frame 0x443770 (with the
 * power-on sweep 0x4437cc inline) and balance numbers 0x444a90, the energy
 * pool's frame 0x445008, the MFD's Alt order keys 0x446c38, the system button's
 * action 0x434910 with its event-base entries 0x434dd2, 0x434ddc, 0x434de6, and
 * the scroll trigger's event 0x434df0: original machine code against
 * sim/src/panel.c (differential_panel_bars_test). It reuses startup_cases.h's
 * snapshots and loop_cases.h's virtual time and loop, as panel_gadgets_cases.h
 * does.
 *
 * Both sides start from the pristine DATA and an empty heap and build the
 * same synthetic world from the same seeded generator: the view with its
 * context and two pens, the GC, the panel H with its system list, scroll
 * zones and the [H+0x1e9] object, the display D, four bars (three with
 * short spans for the LED loops, one with any scale), the shields gauge,
 * the energy pool with an own context, the MFD with its menu E, and four
 * system gadgets. Each case runs 1..6 of the 14 rows; before every call
 * the same per-call script is written on both sides (bytes of the gauges,
 * the system count and the playback word, [D+0x14], the scale of a bar).
 * The other packages' callees (rect_fill 0x486704, the line 0x483a04, the
 * visibility 0x431410, the clock 0x467828, the shield palette 0x443918,
 * the label 0x438920, the GC select 0x480d44, the panel copy 0x43122c, the
 * menu code 0x43f9c0, the MFD order 0x447248 and key 0x447158, 0x45f070,
 * 0x4669c4, 0x4667c8, the current display 0x429820, the view set 0x42a3f4
 * and the system button draw 0x434748) are recording stubs on both sides;
 * the visibility, clock and menu code answer from the call's script. The
 * fill and line stubs record the rectangle or points and the pen's colour,
 * so a colour set late or restored early shows. A bar's method slot is a
 * host stub or one of the bar draws themselves. The pool frame leaves
 * GC+0x22c pointing at its dead local; the stubs record what it points at
 * while it is live and each call records whether it was written, then it
 * is set to a mark. Compared: DATA 0x497000..0x6cc000 and the heap, the
 * stub log, EAX by mask (0 for 0x434ddc, whose empty target leaves the
 * entry EAX), and the own coupling trace (one clock event at 0x4437d3 per
 * own clock call, with the clock's value). Coverage: per-path counters from
 * the original side's script; every one must be non-zero. */
#pragma once
/* The headers this suite builds on (sim_main.c). */
#include "init_records_cases.h"
#include "startup_cases.h"
#include "loop_cases.h"
#include "es2/sim/hud.h"

#define PB_TRACE 64u
#define PB_MARK 0x5a5a0c22u
static es2_sim_entry pb_entries[4096];
static es2_sim_dispatch pb_dispatch;
static es2_sim_hud pb_hud;
static es2_sim_hud_event pb_trace[PB_TRACE];
static int pb_bad;
static unsigned pb_side;

static uint32_t pb_get(uint32_t a) { return get32((BYTE *)(uintptr_t)a); }
static void pb_put8(uint32_t a, uint8_t v) { *(BYTE *)(uintptr_t)a = v; }
static void pb_put16(uint32_t a, uint16_t v) { put16((BYTE *)(uintptr_t)a, v); }
static void pb_put32(uint32_t a, uint32_t v) { put32((BYTE *)(uintptr_t)a, v); }

/* ---- the world (filled by pb_build) */
typedef struct pb_world {
    uint32_t view, ctx, pen, pen2, gc, h, hobj, hvt, d, bar[4], bvt[4], sg, label[2], pool, pctx, m, e, sys[4], zone[4];
} pb_world;
static pb_world pb_w, pb_w0;

/* ---- the stub log of a side */
static uint64_t pb_hash[2];
static unsigned pb_count[2];
static char pb_text[2][8192];
static size_t pb_len[2];
static void pb_note(char letter, uint32_t a, uint32_t b, uint32_t c) {
    uint64_t h = pb_hash[pb_side];
    const uint32_t v[4] = {(uint32_t)letter, a, b, c};
    for (unsigned i = 0; i < 4u; ++i) { h ^= v[i]; h *= 0x100000001b3ull; }
    pb_hash[pb_side] = h;
    ++pb_count[pb_side];
    if (pb_len[pb_side] + 48u < sizeof(pb_text[0]))
        pb_len[pb_side] += (size_t)sprintf(pb_text[pb_side] + pb_len[pb_side], "%c(%lx,%lx,%lx) ", letter, (unsigned long)a,
                                           (unsigned long)b, (unsigned long)c);
}
/* What GC+0x22c points at while the pool frame's local is live. */
static uint32_t pb_gc_local(void) {
    uint32_t p = pb_get(pb_w.gc + 0x22cu);
    return p == PB_MARK ? 0xfeedu : pb_get(p);
}

/* ---- the call script: answers of the visibility (in call order), the
 * clock's offset from [G+0x90], the menu code */
static uint8_t pb_vis[2];
static unsigned pb_vis_used;
static uint32_t pb_clock_t, pb_menu;
static uint32_t pb_answer_seed;
static uint32_t pb_answer(uint32_t n) {
    pb_answer_seed = pb_answer_seed * 1103515245u + 12345u;
    return (pb_answer_seed >> 8) % n;
}
static unsigned pb_clocks[2];
static uint32_t pb_clock_values[2][PB_TRACE];
static unsigned pb_kinds[128];
/* One stub body for both sides; `n` the argument slots. */
static uint32_t pb_stub(char letter, const uint32_t *s, unsigned n) {
    if (pb_side == 0) ++pb_kinds[(unsigned char)letter & 0x7fu];
    switch (letter) {
    case 'F': {   /* rect_fill(rect): the rectangle and the bar pen's colour */
        uint32_t p = pb_get(pb_get(pb_get(0x006c5ed0u) + 0x44u) + 0x228u);
        pb_note('F', pb_get(s[0]), pb_get(s[0] + 4u), pb_get(s[0] + 8u));
        pb_note('f', pb_get(s[0] + 0xcu), pb_get(p + 4u), pb_gc_local());
        return 0x48670000u + pb_answer(0x10000u);
    }
    case 'L': {   /* the line (p1, p2): the points and the LED pen record */
        uint32_t q = pb_get(pb_get(pb_get(0x006c5ed0u) + 0x44u) - 4u + 0x228u);
        pb_note('L', pb_get(s[0]), pb_get(s[0] + 4u), pb_get(s[1]));
        pb_note('l', pb_get(s[1] + 4u), pb_get(q), pb_get(q + 4u));
        return 0x483a0000u + pb_answer(0x10000u);
    }
    case 'V': {   /* the visibility: AL from the script */
        uint8_t al = pb_vis_used < 2u ? pb_vis[pb_vis_used] : 1u;
        ++pb_vis_used;
        pb_note('V', s[0], al, 0);
        return (pb_answer(0x1000000u) << 8) | al;
    }
    case 'K': {   /* the clock: [G+0x90] + the scripted offset */
        uint32_t v = pb_get(pb_w.sg + 0x90u) + pb_clock_t;
        if (pb_clocks[pb_side] < PB_TRACE) pb_clock_values[pb_side][pb_clocks[pb_side]] = v;
        ++pb_clocks[pb_side];
        pb_note('K', v, 0, 0);
        return v;
    }
    case 'T': {   /* the label (label, text): the text up to its NUL (8 bytes at most) */
        uint8_t t[8] = {0};
        for (unsigned i = 0; i < 8u && (t[i] = *(BYTE *)(uintptr_t)(s[1] + i)) != 0; ++i) {}
        pb_note('T', s[0], get32(t), get32(t + 4));
        return 0x43892000u + pb_answer(0x100u);
    }
    case 'M':   /* the menu code */
        pb_note('M', s[0], pb_menu, 0);
        return pb_menu;
    case 'D':   /* the current display */
        pb_note('D', 0, 0, 0);
        return pb_w.d;
    default:
        pb_note(letter, n > 0u ? s[0] : 0, n > 1u ? s[1] : 0, n > 2u ? s[2] : 0);
        if (n) pb_note('_', pb_gc_local(), 0, 0);
        return ((uint32_t)(unsigned char)letter << 24) | pb_answer(0x1000000u);
    }
}
/* ---- the original side's stubs (cdecl; unused slots are not read) */
#define PB_O(letter, slots) \
    static uint32_t __cdecl pb_o_##letter(uint32_t a, uint32_t b, uint32_t c) { const uint32_t s[3] = {a, b, c}; return pb_stub(#letter[0], s, slots); }
PB_O(F, 1)
PB_O(L, 2)
PB_O(V, 1)
PB_O(K, 0)
PB_O(S, 1)
PB_O(T, 2)
PB_O(G, 1)
PB_O(C, 3)
PB_O(M, 1)
PB_O(O, 1)
PB_O(k, 3)
PB_O(x, 0)
PB_O(y, 1)
PB_O(z, 0)
PB_O(D, 0)
PB_O(W, 2)
PB_O(B, 1)
PB_O(b, 2)
PB_O(h, 1)
/* _itoa, an import: the runtime's own (as gadgets_cases.h does). */
static uint32_t __cdecl pb_itoa(uint32_t v, uint32_t buffer, uint32_t radix) { uint32_t a[3] = {v, buffer, radix}; return su_rtl_call(ES2_RTL_ITOA, a); }
static int pb_patch(uint32_t va, void *target, BYTE saved[5], int install) {
    BYTE *site = mapped + (va - 0x400000u);
    DWORD previous;
    if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &previous)) return fail("panel bars: stub protection");
    if (install) { memcpy(saved, site, 5); site[0] = 0xe9; put32(site + 1, (uint32_t)(uintptr_t)target - (uint32_t)(uintptr_t)(site + 5)); }
    else memcpy(site, saved, 5);
    if (!VirtualProtect(site, 5, previous, &previous)) return fail("panel bars: stub restore protection");
    FlushInstructionCache(GetCurrentProcess(), site, 5);
    return 0;
}
static const struct { uint32_t va; void *stub; char letter; unsigned slots; } pb_patched[] = {
    {0x00486704u, (void *)pb_o_F, 'F', 1}, {0x00483a04u, (void *)pb_o_L, 'L', 2}, {0x00431410u, (void *)pb_o_V, 'V', 1},
    {0x00467828u, (void *)pb_o_K, 'K', 0}, {0x00443918u, (void *)pb_o_S, 'S', 1}, {0x00438920u, (void *)pb_o_T, 'T', 2},
    {0x00480d44u, (void *)pb_o_G, 'G', 1}, {0x0043122cu, (void *)pb_o_C, 'C', 3}, {0x0043f9c0u, (void *)pb_o_M, 'M', 1},
    {0x00447248u, (void *)pb_o_O, 'O', 1}, {0x00447158u, (void *)pb_o_k, 'k', 3}, {0x0045f070u, (void *)pb_o_x, 'x', 0},
    {0x004669c4u, (void *)pb_o_y, 'y', 1}, {0x004667c8u, (void *)pb_o_z, 'z', 0}, {0x00429820u, (void *)pb_o_D, 'D', 0},
    {0x0042a3f4u, (void *)pb_o_W, 'W', 2}, {0x00434748u, (void *)pb_o_B, 'B', 1},
};
#define PB_PATCHED (sizeof(pb_patched) / sizeof(pb_patched[0]))
static BYTE pb_saved[PB_PATCHED][5];
/* The method-slot stubs: their host addresses are the VAs both sides call. */
static const struct { void *host; char letter; unsigned slots; } pb_hosts[] = {
    {(void *)pb_o_b, 'b', 2}, {(void *)pb_o_h, 'h', 1},
};

/* ---- the own side */
static void pb_s_stub(es2_sim_service_call *call, void *user) {
    uintptr_t u = (uintptr_t)user;
    call->eax = pb_stub((char)(u & 0xffu), call->slots, (unsigned)(u >> 8));
}
static void pb_refuse(const es2_sim_refusal *r, void *user) {
    (void)user;
    fprintf(stderr, "FAIL: own panel bars: refused call to 0x%08lx (%s): %s\n", (unsigned long)r->va, r->name ? r->name : "?",
            r->reason ? r->reason : "");
    pb_bad = 1;
}
static int pb_registered(uint32_t va) {
    for (unsigned i = 0; i < pb_dispatch.count; ++i) if (pb_dispatch.entries[i].va == va) return 1;
    return 0;
}
/* VAs without an entry get the recorder before the seal; rows and hooks
 * are replaced by it after the seal. */
static int pb_dispatch_setup(void) {
    es2_sim_dispatch_init(&pb_dispatch, pb_entries, 4096, pb_refuse, NULL);
    static es2_sim_services s;
    s.memory = &ir_memory;
    s.heap = &ir_heap;
    s.vt = &lc_vt;
    s.fs0 = NULL;
    if (es2_sim_register_services(&pb_dispatch, &s)) return fail("panel bars: dispatch");
    static int replace[PB_PATCHED];
    for (unsigned i = 0; i < PB_PATCHED; ++i) {
        void *user = (void *)(uintptr_t)((uint32_t)(unsigned char)pb_patched[i].letter | (pb_patched[i].slots << 8));
        replace[i] = pb_registered(pb_patched[i].va);
        if (!replace[i] &&
            es2_sim_dispatch_add_service(&pb_dispatch, pb_patched[i].va, "panel bars stub", pb_s_stub, user, 0, (uint8_t)pb_patched[i].slots))
            return fail("panel bars: dispatch (stub)");
    }
    for (unsigned i = 0; i < sizeof(pb_hosts) / sizeof(pb_hosts[0]); ++i) {
        void *user = (void *)(uintptr_t)((uint32_t)(unsigned char)pb_hosts[i].letter | (pb_hosts[i].slots << 8));
        if (es2_sim_dispatch_add_service(&pb_dispatch, (uint32_t)(uintptr_t)pb_hosts[i].host, "panel bars slot", pb_s_stub, user, 0,
                                         (uint8_t)pb_hosts[i].slots))
            return fail("panel bars: dispatch (slot)");
    }
    const char *error = NULL;
    uint32_t bad = 0;
    if (es2_sim_dispatch_seal(&pb_dispatch, &error, &bad)) return fail("panel bars: dispatch seal");
    for (unsigned i = 0; i < PB_PATCHED; ++i) {
        if (!replace[i]) continue;
        es2_sim_entry *e = (es2_sim_entry *)es2_sim_dispatch_find(&pb_dispatch, pb_patched[i].va);
        if (!e) return fail("panel bars: a replaced entry is missing");
        e->kind = ES2_SIM_SERVICE; e->name = "panel bars stub"; e->service = pb_s_stub;
        e->service_user = (void *)(uintptr_t)((uint32_t)(unsigned char)pb_patched[i].letter | (pb_patched[i].slots << 8));
        e->pop_bytes = 0; e->stack_slots = (uint8_t)pb_patched[i].slots;
    }
    return 0;
}

/* ---- the synthetic world */
static uint32_t pb_seed;
static uint32_t pb_rand(uint32_t n) {
    pb_seed = pb_seed * 1103515245u + 12345u;
    return (pb_seed >> 8) % n;
}
static uint32_t pb_alloc(uint32_t n) {
    uint32_t a = (uint32_t)(uintptr_t)ir_calloc(1, n);
    for (uint32_t f = 0; f < n; ++f) pb_put8(a + f, (uint8_t)pb_rand(256u));
    return a;
}
static const uint32_t pb_bar_draws[3] = {0x00439118u, 0x00439398u, 0x004395e8u};
/* A short-span bar: x0 0..40, x1 at x0 - 4 .. x0 + 40, the rectangle B+4
 * in the same range (the LED loops stay short). */
static void pb_bar_span(uint32_t b) {
    uint32_t x0 = pb_rand(41u) - 4u;
    pb_put32(b + 0x18u, x0);
    pb_put32(b + 0x1cu, x0 + pb_rand(45u) - 4u);
    pb_put32(b + 4u, pb_rand(41u) - 4u);
    pb_put32(b + 0xcu, pb_get(b + 4u) + pb_rand(45u) - 4u);
}
static void pb_build(void) {
    pb_world *w = &pb_w;
    memset(w, 0, sizeof(*w));
    w->view = pb_alloc(0x80u);
    w->ctx = pb_alloc(0x300u);
    w->pen = pb_alloc(0x10u);
    w->pen2 = pb_alloc(0x10u);
    pb_put32(w->view + 0x44u, w->ctx);
    pb_put32(w->ctx + 0x228u, w->pen);
    pb_put32(w->ctx + 0x224u, w->pen2);   /* [G'+0x228], G' = ctx - 4 */
    w->gc = pb_alloc(0x300u);
    pb_put32(w->gc + 0x22cu, PB_MARK);
    w->hvt = pb_alloc(0x10u);
    pb_put32(w->hvt + 4u, (uint32_t)(uintptr_t)pb_o_h);
    w->hobj = pb_alloc(0x20u);
    pb_put32(w->hobj, w->hvt);
    w->h = pb_alloc(0x300u);
    pb_put32(w->h + 0x1e9u, w->hobj);
    w->d = pb_alloc(0x40u);
    for (unsigned t = 0; t < 4u; ++t) {
        w->bvt[t] = pb_alloc(0x10u);
        pb_put32(w->bvt[t], t ? pb_bar_draws[t - 1u] : (uint32_t)(uintptr_t)pb_o_b);
    }
    for (unsigned i = 0; i < 4u; ++i) {
        uint32_t b = w->bar[i] = pb_alloc(0x40u);
        /* bar 3 (any scale) never reaches the LED draw */
        pb_put32(b, w->bvt[i == 3u ? pb_rand(3u) : pb_rand(4u)]);
        pb_bar_span(b);
        pb_put32(b + 0x14u, pb_rand(81u) - 40u);
    }
    w->label[0] = pb_alloc(0x40u);
    w->label[1] = pb_alloc(0x40u);
    w->sg = pb_alloc(0x100u);
    pb_put32(w->sg + 0xc4u, w->label[0]);
    pb_put32(w->sg + 0xc8u, w->label[1]);
    w->pctx = pb_alloc(0x300u);
    w->pool = pb_alloc(0x100u);
    w->e = pb_alloc(0x40u);
    w->m = pb_alloc(0x100u);
    pb_put32(w->m + 0xd1u, w->e);
    for (unsigned i = 0; i < 4u; ++i) w->sys[i] = pb_alloc(0x40u);
    for (unsigned i = 0; i < 4u; ++i) {
        w->zone[i] = pb_alloc(0x40u);
        pb_put32(w->zone[i] + 0x20u, w->h);
    }
    pb_put32(w->h + 0x21au, w->zone[0]);
    pb_put32(w->h + 0x21eu, w->zone[1]);
    pb_put32(w->h + 0x222u, w->zone[2]);
    for (unsigned i = 0; i < 4u; ++i) pb_put32(w->h + 0x246u + 4u * i, w->sys[pb_rand(5u) % 4u]);
    pb_put32(0x006c5ed0u, w->view);
    pb_put32(0x006c5f70u, w->gc);
    pb_put32(0x0049b088u, w->h);
}

/* ---- the rows */
enum { PB_BAR_POS, PB_BAR_NEG, PB_BAR_LED, PB_LED, PB_SH_DRAW, PB_SH_FRAME, PB_SH_NUMBERS, PB_POOL, PB_MFD, PB_SYSTEM, PB_E4DD2,
       PB_E4DDC, PB_E4DE6, PB_SCROLL, PB_OPS };
static const struct { uint32_t va; unsigned n; const char *name; } pb_ops[PB_OPS] = {
    {0x00439118u, 2, "bar draw"}, {0x00439398u, 2, "bar draw negative"}, {0x004395e8u, 2, "bar draw LED"},
    {0x00439758u, 2, "LED segments"}, {0x00443758u, 1, "shields draw"}, {0x00443770u, 1, "shields frame"},
    {0x00444a90u, 1, "shield numbers"}, {0x00445008u, 1, "pool frame"}, {0x00446c38u, 2, "MFD Alt order"},
    {0x00434910u, 1, "system action"}, {0x00434dd2u, 1, "entry 434dd2"}, {0x00434ddcu, 1, "entry 434ddc"},
    {0x00434de6u, 1, "entry 434de6"}, {0x00434df0u, 1, "scroll event"},
};
typedef struct pb_set { uint32_t va, value; unsigned width; } pb_set;
typedef struct pb_op {
    unsigned op, sets;
    uint32_t a[2], mask, clock_t, menu;
    uint8_t vis[2];
    pb_set set[8];
} pb_op;
static void pb_set_add(pb_op *o, uint32_t va, uint32_t value, unsigned width) { o->set[o->sets++] = (pb_set){va, value, width}; }
static void pb_apply(const pb_op *o) {
    for (unsigned i = 0; i < o->sets; ++i) {
        const pb_set *s = &o->set[i];
        if (s->width == 1u) pb_put8(s->va, (uint8_t)s->value);
        else if (s->width == 2u) pb_put16(s->va, (uint16_t)s->value);
        else pb_put32(s->va, s->value);
    }
    pb_vis[0] = o->vis[0];
    pb_vis[1] = o->vis[1];
    pb_vis_used = 0;
    pb_clock_t = o->clock_t;
    pb_menu = o->menu;
}
static uint32_t pb_signed_small(void) { return random32() % 4u ? random32() % 601u - 300u : random32(); }
/* A balance value whose two numbers fit the 8-byte locals (seven
 * characters and the NUL), the 32-bit product wrapping or not. */
static uint32_t pb_balance(void) {
    for (;;) {
        uint32_t x = random32() % 3u ? random32() % 0x00800001u - 0x00400000u : random32();
        int32_t a = (int32_t)(x * 200u) >> 10;
        if (a > -1000000 && a < 1000000 && 200 - a > -1000000 && 200 - a < 1000000) return x;
    }
}
static void pb_args(pb_op *o) {
    const pb_world *w = &pb_w;
    o->sets = 0;
    o->mask = o->op == PB_E4DDC ? 0u : ~0u;
    for (unsigned i = 0; i < 2u; ++i) o->vis[i] = random32() % 4u == 0 ? 0u : (uint8_t)(1u + random32() % 255u);
    o->menu = random32() % 3u == 0 ? random32() % 10u : random32() % 2u ? 4u : 7u;
    o->clock_t = 0;
    switch (o->op) {
    case PB_BAR_POS: case PB_BAR_NEG: case PB_BAR_LED: {
        uint32_t b = w->bar[o->op == PB_BAR_LED ? random32() % 3u : random32() % 4u];
        o->a[0] = b;
        o->a[1] = random32() % 3u ? random32() : (random32() << 16) | (random32() % 0x200u);
        if (b == w->bar[3]) pb_set_add(o, b + 0x14u, random32() % 3u ? random32() : random32() % 81u - 40u, 4);
        else if (random32() % 6u == 0) pb_set_add(o, b + 0x14u, 0, 4);
        else pb_set_add(o, b + 0x14u, random32() % 81u - 40u, 4);
        /* the span: x0 and x1 short again */
        uint32_t x0 = random32() % 41u - 4u;
        pb_set_add(o, b + 0x18u, x0, 4);
        pb_set_add(o, b + 0x1cu, x0 + random32() % 45u - 4u, 4);
        break;
    }
    case PB_LED: {
        uint32_t b = w->bar[random32() % 3u];
        o->a[0] = b;
        o->a[1] = b + 4u;
        uint32_t x0 = random32() % 41u - 4u;
        pb_set_add(o, b + 4u, x0, 4);
        pb_set_add(o, b + 0xcu, x0 + random32() % 45u - 6u, 4);
        break;
    }
    case PB_SH_DRAW: case PB_SH_FRAME: case PB_SH_NUMBERS: {
        uint32_t g = w->sg;
        o->a[0] = g;
        pb_set_add(o, g + 0x8cu, random32() % 5u ? 1u + random32() % 255u : 0u, 1);
        pb_set_add(o, g + 0x8du, random32() % 3u == 0 ? 1u + random32() % 255u : 0u, 1);
        pb_set_add(o, g + 0xb0u, random32() % 5u, 1);
        uint32_t cc = pb_signed_small(), d0 = pb_signed_small();
        pb_set_add(o, g + 0xccu, cc, 4);
        pb_set_add(o, g + 0xd0u, d0, 4);
        pb_set_add(o, g + 0x90u, random32(), 4);
        pb_set_add(o, g + 0xbdu, pb_balance(), 4);
        switch (random32() % 7u) {
        case 0: o->clock_t = cc - 1u; break;
        case 1: o->clock_t = cc; break;
        case 2: o->clock_t = cc + 1u; break;
        case 3: o->clock_t = d0 - 1u; break;
        case 4: o->clock_t = d0; break;
        case 5: o->clock_t = d0 + 1u; break;
        default: o->clock_t = pb_signed_small(); break;
        }
        break;
    }
    case PB_POOL: {
        uint32_t g = w->pool;
        o->a[0] = g;
        pb_set_add(o, g + 0xb5u, w->bar[random32() % 4u], 4);
        pb_set_add(o, g + 0xbfu, random32() % 3u ? w->pctx : 0u, 4);
        pb_set_add(o, g + 0xb0u, random32() % 4u ? 2u : random32() % 5u, 1);
        break;
    }
    case PB_MFD: {
        static const uint16_t codes[] = {0x21e, 0x222, 0x223, 0x218, 0x221, 0x22e, 0x212};
        o->a[0] = w->m;
        uint32_t c;
        switch (random32() % 5u) {
        case 0: c = 0x212u + random32() % 0x1du; break;           /* anywhere in the table */
        case 1: c = random32() % 2u ? 0x211u - random32() % 4u : 0x22fu + random32() % 8u; break;   /* outside */
        default: c = codes[random32() % 7u]; break;
        }
        o->a[1] = random32() % 4u == 0 ? (random32() << 16) | c : c;
        break;
    }
    case PB_SYSTEM: case PB_E4DD2: case PB_E4DDC: case PB_E4DE6: {
        uint32_t b = random32() % 6u == 0 ? random32() : w->sys[random32() % 4u];
        o->a[0] = o->op == PB_SYSTEM ? b : b + 0x20u;
        pb_set_add(o, 0x004d24d6u, random32() % 5u == 0 ? 1u + random32() % 0xffffu : 0u, 2);
        pb_set_add(o, w->h + 0x25au, random32() % 6u ? random32() % 7u - 1u : random32() | 0x80000000u, 4);   /* at most 5: the list ends at the count */
        break;
    }
    case PB_SCROLL:
        o->a[0] = w->zone[random32() % 4u];
        pb_set_add(o, w->d + 0x14u, random32() % 8u ? random32() % 5u : random32(), 4);
        break;
    default: break;
    }
}

/* ---- coverage (from the script and the world before a call, original side) */
enum { PBC_POS_F, PBC_POS_B, PBC_NEG_F, PBC_NEG_B, PBC_LEDBAR_F, PBC_LEDBAR_B, PBC_K0, PBC_WIDE, PBC_LED_ODD, PBC_LED_EVEN,
       PBC_LED_EMPTY, PBC_SF_HIDDEN, PBC_SF_OFF, PBC_SF_SWEPT, PBC_SF_SWEEP_BOTH, PBC_SF_SWEEP_FRONT, PBC_SF_SWEEP_NONE,
       PBC_SF_SWEEP_REAR, PBC_SF_ONE, PBC_SF_TWO, PBC_SF_OTHER, PBC_SN_HIDDEN, PBC_SN_SHOWN, PBC_SN_WRAP, PBC_PL_HIDDEN,
       PBC_PL_STATE, PBC_PL_NOCTX, PBC_PL_CTX, PBC_PL_HOST, PBC_PL_DRAW, PBC_MF_ITEM, PBC_MF_CHECK_OK, PBC_MF_CHECK_FAIL,
       PBC_MF_TABLE0, PBC_MF_OUT, PBC_MF_HIGH, PBC_SY_PLAY, PBC_SY_EMPTY, PBC_SY_I0, PBC_SY_I1, PBC_SY_LATER, PBC_SY_NONE,
       PBC_SC_B0, PBC_SC_B1, PBC_SC_BX, PBC_SC_L0, PBC_SC_L3, PBC_SC_LX, PBC_SC_R0, PBC_SC_R2, PBC_SC_RX, PBC_SC_NONE, PBC_COUNT };
static const char *const pb_cv_names[PBC_COUNT] = {
    "bar forward", "bar backward", "negative forward", "negative backward", "LED bar forward", "LED bar backward", "scale 0",
    "scale wide", "LED odd start", "LED even start", "LED no line", "frame hidden", "frame off", "frame swept",
    "sweep both reached", "sweep front reached", "sweep none reached", "sweep rear only", "frame state 1", "frame state 2",
    "frame state other", "numbers hidden", "numbers shown", "numbers wrapped", "pool hidden", "pool state not 2", "pool no context",
    "pool context", "pool host slot", "pool bar draw", "MFD item", "MFD check passes", "MFD check fails", "MFD table 0",
    "MFD outside", "MFD high bits", "system playback", "system count below 1", "system i 0", "system i 1", "system later i",
    "system no match", "scroll bottom 0", "scroll bottom 1", "scroll bottom other", "scroll left 0", "scroll left 3",
    "scroll left other", "scroll right 0", "scroll right 2", "scroll right other", "scroll no zone",
};
static unsigned pb_cv[PBC_COUNT];
static void pb_cover_bar(unsigned kind, uint32_t b) {
    int32_t k = (int32_t)pb_get(b + 0x14u);
    int forward = kind == 1u ? k < 0 : k > 0;
    if (!k) ++pb_cv[PBC_K0];
    if (k > 40 || k < -40) ++pb_cv[PBC_WIDE];
    ++pb_cv[(kind == 0u ? PBC_POS_F : kind == 1u ? PBC_NEG_F : PBC_LEDBAR_F) + (forward ? 0u : 1u)];
}
static void pb_cover(const pb_op *o) {
    const pb_world *w = &pb_w;
    switch (o->op) {
    case PB_BAR_POS: case PB_BAR_NEG: case PB_BAR_LED: pb_cover_bar(o->op - PB_BAR_POS, o->a[0]); break;
    case PB_LED: {
        int32_t x0 = (int32_t)pb_get(o->a[1]), x1 = (int32_t)pb_get(o->a[1] + 8u);
        ++pb_cv[x0 & 1 ? PBC_LED_ODD : PBC_LED_EVEN];
        if (x1 < x0) ++pb_cv[PBC_LED_EMPTY];
        break;
    }
    case PB_SH_FRAME: {
        uint32_t g = o->a[0];
        if (!o->vis[0]) { ++pb_cv[PBC_SF_HIDDEN]; break; }
        if (!*(BYTE *)(uintptr_t)(g + 0x8cu)) { ++pb_cv[PBC_SF_OFF]; break; }
        uint8_t state = *(BYTE *)(uintptr_t)(g + 0xb0u);
        if (*(BYTE *)(uintptr_t)(g + 0x8du)) ++pb_cv[PBC_SF_SWEPT];
        else {
            int32_t t = (int32_t)o->clock_t, cc = (int32_t)pb_get(g + 0xccu), d0 = (int32_t)pb_get(g + 0xd0u);
            ++pb_cv[cc <= t ? d0 <= t ? PBC_SF_SWEEP_BOTH : PBC_SF_SWEEP_FRONT : d0 <= t ? PBC_SF_SWEEP_REAR : PBC_SF_SWEEP_NONE];
            state = 2;
        }
        ++pb_cv[state == 1u ? PBC_SF_ONE : state == 2u ? PBC_SF_TWO : PBC_SF_OTHER];
        if (state == 2u) ++pb_cv[o->vis[1] ? PBC_SN_SHOWN : PBC_SN_HIDDEN];
        break;
    }
    case PB_SH_DRAW: case PB_SH_NUMBERS: {
        int64_t p = (int64_t)(int32_t)pb_get(o->a[0] + 0xbdu) * 200;
        ++pb_cv[o->vis[0] ? PBC_SN_SHOWN : PBC_SN_HIDDEN];
        if (o->vis[0] && p != (int64_t)(int32_t)(uint32_t)p) ++pb_cv[PBC_SN_WRAP];
        break;
    }
    case PB_POOL: {
        uint32_t g = o->a[0];
        if (!o->vis[0]) { ++pb_cv[PBC_PL_HIDDEN]; break; }
        if (*(BYTE *)(uintptr_t)(g + 0xb0u) != 2u) { ++pb_cv[PBC_PL_STATE]; break; }
        ++pb_cv[pb_get(g + 0xbfu) ? PBC_PL_CTX : PBC_PL_NOCTX];
        ++pb_cv[pb_get(pb_get(pb_get(g + 0xb5u))) == (uint32_t)(uintptr_t)pb_o_b ? PBC_PL_HOST : PBC_PL_DRAW];
        break;
    }
    case PB_MFD: {
        uint32_t c = o->a[1] & 0xffffu;
        if (o->a[1] > 0xffffu) ++pb_cv[PBC_MF_HIGH];
        if (c == 0x21eu || c == 0x222u || c == 0x223u || c == 0x218u || c == 0x221u) ++pb_cv[PBC_MF_ITEM];
        else if (c == 0x22eu || c == 0x212u) ++pb_cv[o->menu == (c == 0x22eu ? 4u : 7u) ? PBC_MF_CHECK_OK : PBC_MF_CHECK_FAIL];
        else ++pb_cv[c >= 0x212u && c <= 0x22eu ? PBC_MF_TABLE0 : PBC_MF_OUT];
        break;
    }
    case PB_SYSTEM: case PB_E4DD2: {
        uint32_t b = o->op == PB_SYSTEM ? o->a[0] : o->a[0] - 0x20u;
        if (*(uint16_t *)(uintptr_t)0x004d24d6u) { ++pb_cv[PBC_SY_PLAY]; break; }
        int32_t n = (int32_t)pb_get(w->h + 0x25au), i = 0;
        if (n < 1) ++pb_cv[PBC_SY_EMPTY];
        while (i < n && pb_get(w->h + 0x246u + 4u * (uint32_t)i) != b) ++i;
        ++pb_cv[i >= n ? PBC_SY_NONE : i == 0 ? PBC_SY_I0 : i == 1 ? PBC_SY_I1 : PBC_SY_LATER];
        break;
    }
    case PB_SCROLL: {
        uint32_t z = o->a[0], n = pb_get(w->d + 0x14u);
        if (z == w->zone[0]) ++pb_cv[n == 0u ? PBC_SC_B0 : n == 1u ? PBC_SC_B1 : PBC_SC_BX];
        else if (z == w->zone[1]) ++pb_cv[n == 0u ? PBC_SC_L0 : n == 3u ? PBC_SC_L3 : PBC_SC_LX];
        else if (z == w->zone[2]) ++pb_cv[n == 0u ? PBC_SC_R0 : n == 2u ? PBC_SC_R2 : PBC_SC_RX];
        else ++pb_cv[PBC_SC_NONE];
        break;
    }
    default: break;
    }
}

static int pb_side_begin(unsigned side, uint32_t seed, uint32_t answers) {
    lc_reset(side, 47, 0x01000000u + (seed % 0x100000u) * 16u, 1);
    lc_loop.dispatch = &pb_dispatch;
    pb_side = side;
    pb_hash[side] = 0xcbf29ce484222325ull;
    pb_count[side] = 0;
    pb_len[side] = 0;
    pb_text[side][0] = 0;
    pb_clocks[side] = 0;
    pb_answer_seed = answers;
    pb_seed = seed;
    pb_build();
    if (side) {
        if (memcmp(&pb_w, &pb_w0, sizeof(pb_w))) return fail("panel bars: the sides' worlds differ (heap order)");
        es2_sim_hud_init(&pb_hud, &lc_loop);
        pb_hud.trace = pb_trace;
        pb_hud.trace_capacity = PB_TRACE;
        es2_sim_hud_bind(&pb_hud);
    } else {
        pb_w0 = pb_w;
    }
    return 0;
}
static uint32_t pb_own_call(uint32_t va, const uint32_t *a, unsigned n) {
    es2_sim_result r;
    uint32_t regs[3] = {0, 0, 0};
    memset(&r, 0, sizeof(r));
    if (es2_sim_call(&pb_dispatch, va, a, n, regs, &r)) pb_bad = 1;
    return r.eax;
}
static unsigned pb_calls[PB_OPS];
static int pb_case(unsigned k) {
    unsigned ops = 1u + random32() % 6u;
    static pb_op list[6];
    uint32_t seed = random32(), answers = random32();
    static uint32_t eax[2][6];
    for (unsigned i = 0; i < ops; ++i) list[i].op = random32() % PB_OPS;
    for (unsigned side = 0; side < 2u; ++side) {
        if (pb_side_begin(side, seed, answers)) return 1;
        for (unsigned i = 0; i < ops && !pb_bad && !lc_bad; ++i) {
            pb_op *o = &list[i];
            if (side == 0) pb_args(o);
            pb_apply(o);
            if (side == 0) { pb_cover(o); ++pb_calls[o->op]; }
            uint32_t d = 0;
            uint32_t r = side ? pb_own_call(pb_ops[o->op].va, o->a, pb_ops[o->op].n) : lc_machine(pb_ops[o->op].va, o->a, pb_ops[o->op].n, &d);
            eax[side][i] = r & o->mask;
            /* the pool frame's dangling local: whether it was written, then the mark */
            pb_note('g', pb_get(pb_w.gc + 0x22cu) != PB_MARK, 0, 0);
            pb_put32(pb_w.gc + 0x22cu, PB_MARK);
        }
        if (pb_bad || lc_bad) { fprintf(stderr, "FAIL: panel bars case %u: a call failed (side %u)\n", k, side); return 1; }
        su_take(side);
    }
    for (unsigned i = 0; i < ops; ++i)
        if (eax[0][i] != eax[1][i]) {
            fprintf(stderr, "FAIL: panel bars case %u: %s (call %u) EAX %08lx / %08lx\n", k, pb_ops[list[i].op].name, i,
                    (unsigned long)eax[0][i], (unsigned long)eax[1][i]);
            return 1;
        }
    if (su_compare("panel bars", k)) {
        fprintf(stderr, "  rows:");
        for (unsigned i = 0; i < ops; ++i) fprintf(stderr, " %s", pb_ops[list[i].op].name);
        fprintf(stderr, "\n");
        return 1;
    }
    if (pb_hash[0] != pb_hash[1] || pb_count[0] != pb_count[1]) {
        fprintf(stderr, "FAIL: panel bars case %u: stub calls differ (%u / %u)\n  original: %s\n  own:      %s\n", k, pb_count[0], pb_count[1],
                pb_text[0], pb_text[1]);
        return 1;
    }
    /* the coupling trace: one clock event per own clock call, at 0x4437d3 */
    if (pb_hud.trace_count != pb_clocks[1]) {
        fprintf(stderr, "FAIL: panel bars case %u: %u coupling events for %u clock calls\n", k, pb_hud.trace_count, pb_clocks[1]);
        return 1;
    }
    for (unsigned i = 0; i < pb_hud.trace_count && i < PB_TRACE; ++i) {
        const es2_sim_hud_event *e = &pb_trace[i];
        if (e->kind != ES2_SIM_HUD_CLOCK || e->site != 0x004437d3u || e->target != 0x00467828u || e->argument != 0 ||
            e->result != pb_clock_values[1][i]) {
            fprintf(stderr, "FAIL: panel bars case %u: coupling event %u is %c site %08lx target %08lx result %08lx\n", k, i, e->kind,
                    (unsigned long)e->site, (unsigned long)e->target, (unsigned long)e->result);
            return 1;
        }
    }
    return 0;
}

static int panel_bars_cases(void) {
    current_function = "panel bars";
    es2_sim_identity_memory(&ir_memory);
    es2_sim_rtl_memory(&ir_memory, &su_rtl);
    su_data_pristine = malloc(SU_DATA_SIZE);
    su_snap[0] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    su_snap[1] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    ir_region = ww_alloc(IR_REGION_SIZE);
    lc_stack = VirtualAlloc(NULL, 0x10000u, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    lc_bits = ww_alloc(LC_BITS_SIZE);
    if (!su_data_pristine || !su_snap[0] || !su_snap[1] || !ir_region || !lc_stack || !lc_bits) return fail("panel bars: memory");
    if (get32(mapped + (SU_COUNTER - 0x400000u))) return fail("panel bars: the counter word is not free");
    su_pristine_take();
    if (!ir_patch_import("KERNEL32.dll", "GetTickCount", (void *)lc_tick)) return fail("panel bars: import slot not found");
    if (!ir_patch_import("cw3220.dll", "_itoa", (void *)pb_itoa)) return fail("panel bars: import slot _itoa not found");
    for (unsigned i = 0; i < PB_PATCHED; ++i)
        if (pb_patch(pb_patched[i].va, pb_patched[i].stub, pb_saved[i], 1)) return 1;
    if (pb_dispatch_setup()) return 1;
    int bad = 0;
    const unsigned cases = 4000;
    for (unsigned k = 0; !bad && k < cases; ++k) {
        current_case = k; current_case_valid = 1;
        bad = pb_case(k);
    }
    if (!bad) {
        printf("PASS: panel bars: %u cases identical\n", cases);
        printf("PANEL BARS rows called:");
        for (unsigned i = 0; i < PB_OPS; ++i) printf(" %s %u%s", pb_ops[i].name, pb_calls[i], i + 1u < PB_OPS ? "," : "\n");
        printf("PANEL BARS coverage:");
        for (unsigned i = 0; i < PBC_COUNT; ++i) printf(" %s %u%s", pb_cv_names[i], pb_cv[i], i + 1u < PBC_COUNT ? "," : "\n");
        printf("PANEL BARS stub calls: fill %u, line %u, visible %u, clock %u, palette %u, label %u, select %u, copy %u, menu %u, "
               "order %u, key %u, 45f070 %u, 4669c4 %u, 4667c8 %u, display %u, view set %u, system draw %u, bar slot %u, 1e9 slot %u\n",
               pb_kinds['F'], pb_kinds['L'], pb_kinds['V'], pb_kinds['K'], pb_kinds['S'], pb_kinds['T'], pb_kinds['G'], pb_kinds['C'],
               pb_kinds['M'], pb_kinds['O'], pb_kinds['k'], pb_kinds['x'], pb_kinds['y'], pb_kinds['z'], pb_kinds['D'], pb_kinds['W'],
               pb_kinds['B'], pb_kinds['b'], pb_kinds['h']);
        for (unsigned i = 0; i < PBC_COUNT; ++i)
            if (!pb_cv[i]) { fprintf(stderr, "FAIL: panel bars: path '%s' never taken\n", pb_cv_names[i]); bad = 1; }
        for (unsigned i = 0; i < PB_OPS; ++i)
            if (!pb_calls[i]) { fprintf(stderr, "FAIL: panel bars: row '%s' never called\n", pb_ops[i].name); bad = 1; }
    }
    current_case_valid = 0;
    for (unsigned i = 0; i < PB_PATCHED; ++i) pb_patch(pb_patched[i].va, NULL, pb_saved[i], 0);
    put32(mapped + 0x2cbee4u, 0);
    memcpy(mapped + SU_DATA_RVA, su_data_pristine, SU_DATA_SIZE);
    return bad;
}
