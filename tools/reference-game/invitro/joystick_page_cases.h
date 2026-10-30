/* The joystick options page, original machine code against the rows of
 * sim/dispatch/services/joystick_page.h (sim/src/dialogs.c; input.md, "the
 * joystick page and the SDL3 joystick"). It builds on dialog_ctors_cases.h:
 * that suite's synthetic world, stub log, recording stubs and machine patches
 * are reused, its cases are not run. Identifiers use the prefix jz_/JZ_.
 *
 * Both sides start from the pristine DATA and an empty heap and build the
 * same seeded world: dialog_ctors.h's (surface, graphics context, bitmap set,
 * fonts, shifts, colours, presents word, driver slots) plus a control
 * record, a joystick object, a joystick summary (axes word, axis count
 * -1..9, the three option bytes), a defaults map, the page's method table
 * +4 (the frame) on a host stub, the layout byte 0x4d2571, the pref base
 * 0x4d2577, the prefs, their committed copies and callbacks (a host stub
 * or 0), the store guard and, in some cases, random entry tables (the
 * real ones never fill a row). One case: the constructor 0x457d38 (save
 * buffer and position random), the page state set at random (summary,
 * option values, chosen entries, column, highlight, pending gadget,
 * direction, default and focus), every gadget's method table replaced by
 * host stubs, then 1..8 of draw, run, select, keys, label, text, store,
 * row, next, back, column, commit and table with random arguments, then
 * the destructor 0x4585f4 (flag random, the kept bitmap sometimes
 * cleared). The run's control record and its step are stubs: the step
 * sets a random pending gadget and direction per pass and ends the loop
 * after a per-case number of passes. Real on both sides: dialog_ctors.h's rows the
 * constructor reaches (0x4541b8, 0x454f80, 0x4543b0), new and delete, the
 * clock 0x454ac0 and the page's own method table. Recording stubs on both
 * sides: modal_dialogs.h's, input_layer.h's and joystick_prefs.h's rows the page calls, the control
 * record, the button release, the prefs store, and the graphics and text
 * callees of dialog_ctors_cases.h. Compared: DATA 0x497000..0x6cc000 and
 * the heap, the stub log, EAX of every call (AL of the keys; not of the
 * row 0x459314 and the destructor). Coverage: counters from the original
 * side; every one must be non-zero. */
#pragma once
#include "dialog_ctors_cases.h"

#define JZ_OPS 8u
static es2_sim_entry jz_entries[4096];
static es2_sim_dispatch jz_dispatch;

/* ---- the extra world (allocated after dz_build in the same order) */
typedef struct jz_world { uint32_t record, joy, summary, map, gvt; } jz_world;
static jz_world jz_w, jz_w0;
static unsigned jz_passes, jz_pass_limit;
static uint32_t jz_page;

/* ---- coverage */
enum { JZC_SMALL, JZC_CENTRED, JZC_LAYOUT_A, JZC_LAYOUT_B, JZC_FULL_ROW, JZC_ONCE_FILL, JZC_ONCE_KEPT, JZC_DTOR_BITMAP,
       JZC_DTOR_NO_BITMAP, JZC_DTOR_DELETE, JZC_DRAW, JZC_LABEL_1, JZC_LABEL_OPT, JZC_LABEL_AXIS, JZC_LABEL_SKIP, JZC_LABEL_OUT,
       JZC_RUN_SUMMARY, JZC_RUN_NONE, JZC_RUN_ABSENT, JZC_RUN_CYCLE, JZC_RUN_STEP, JZC_RUN_DEFAULTS, JZC_RUN_STORE, JZC_RUN_OTHER,
       JZC_KEYS_STEP, JZC_KEYS_COLUMN, JZC_KEYS_BIT_IDLE, JZC_KEY_1, JZC_KEY_F, JZC_KEY_1C, JZC_KEY_1C_FOCUS, JZC_KEY_52,
       JZC_KEY_80F, JZC_KEY_OTHER, JZC_SEL_HIGHLIGHT, JZC_SEL_FIRST, JZC_SEL_COLUMN, JZC_SEL_PENDING, JZC_SEL_END, JZC_SEL_NONE,
       JZC_NEXT_WRAP, JZC_NEXT, JZC_BACK_WRAP, JZC_BACK, JZC_COL_NONE, JZC_COL_ROWS, JZC_STORE_FIRST, JZC_STORE_KEPT,
       JZC_COMMIT_CALL, JZC_COMMIT_SILENT, JZC_COMMIT_SAME, JZC_TABLE, JZC_TEXT, JZC_ROW_SAME, JZC_ROW_NEW, JZC_COUNT };
static const char *const jz_cv_names[JZC_COUNT] = {
    "at position", "centred", "entries A", "entries B", "full row", "once fill", "once kept", "dtor bitmap kept",
    "dtor bitmap cleared", "dtor delete", "draw", "label 1", "label option", "label axis", "label skipped", "label out of range",
    "run with axes", "run without axes", "run without joystick", "run cycle", "run step", "run defaults", "run store",
    "run other pending", "keys step", "keys column", "keys bit idle", "key 1", "key 0xf", "key 0x1c", "key 0x1c focus",
    "key 0x52", "key 0x80f", "key other", "select highlight", "select first highlight", "select column", "select pending",
    "select end", "select no gadget", "next wraps", "next", "back wraps", "back", "column none", "column rows", "store first",
    "store kept", "commit callback", "commit silent", "commit same", "table", "text", "row same", "row new",
};
static unsigned jz_cv[JZC_COUNT];

/* ---- the extra stubs: modal_dialogs.h's, input_layer.h's and joystick_prefs.h's rows, the record,
 * the step, the release, the store; host stubs for the gadget tables and
 * the prefs callbacks. */
static uint32_t jz_stub(char l, const uint32_t *s) {
    switch (l) {
    case '3': {   /* 0x454acc(P): the step; a pending gadget and direction, the end */
        uint32_t o = s[0];
        dz_note('3', o, jz_passes, 0);
        if (dz_answer(3u)) dz_put32(o + 0x45au, dz_answer(4u) ? 1u + dz_answer(14u) : dz_answer(2u) ? 0u : 15u + dz_answer(3u));
        dz_put8(o + 0x2ffu, (uint8_t)dz_answer(2u));
        if (!dz_side) {
            uint32_t a = dz_get(o + 0x45au), su = dz_get(o + 0x47au);
            if (a && su && get16((BYTE *)(uintptr_t)su))
                ++jz_cv[a <= 4u ? JZC_RUN_CYCLE : a <= 12u ? JZC_RUN_STEP : a == 13u ? JZC_RUN_DEFAULTS : a == 14u ? JZC_RUN_STORE : JZC_RUN_OTHER];
        }
        if (++jz_passes >= jz_pass_limit) dz_put8(o + 0x2d1u, (uint8_t)(1u + dz_answer(3u)));
        return dz_answer(0x100u);
    }
    case '*': {   /* 0x45a810: the control record */
        static const uint16_t keys[] = {0, 0, 0, 1, 0xf, 0x1c, 0x52, 0x80f, 0x30, 0x1c};
        uint32_t r = jz_w.record;
        for (uint32_t i = 0; i < 0x30u; i += 4u) dz_put32(r + i, 0);
        dz_put16(r, keys[dz_answer(10u)]);
        dz_put8(r + 0xdu, (uint8_t)(dz_answer(4u) ? 0u : 1u + dz_answer(255u)));
        for (uint32_t i = 0x17u; i < 0x1eu; ++i) dz_put8(r + i, (uint8_t)(dz_answer(8u) ? 0u : 1u));
        dz_note('*', dz_get(r), dz_get(r + 0xcu), dz_get(r + 0x14u) ^ dz_get(r + 0x18u) ^ dz_get(r + 0x1cu));
        return r;
    }
    case '@': dz_note('@', s[0], 0, 0); return dz_answer(6u) ? jz_w.joy : 0u;               /* 0x45c524(3) */
    case '#': dz_note('#', 0, 0, 0); return dz_answer(8u) ? jz_w.summary : 0u;               /* 0x477904 */
    case '$': dz_note('$', s[0], 0, 0); return jz_w.map;                                     /* 0x45a274(layout) */
    case '%':                                                                                /* 0x459958(i, v, flag) */
        dz_note('%', s[0], s[1], s[2]);
        if ((s[0] & 0xffu) < 0x36u) dz_put8(0x004d1f38u + (s[0] & 0xffu), (uint8_t)s[1]);
        return dz_answer(0x10000u) << 8 | (s[1] & 0xffu);
    case '9': case '!': dz_note(l, s[0], s[1], s[2]); return dz_answer(0x10000u) << 8 | dz_answer(3u);   /* the cycles */
    case '^': dz_note('^', s[0], s[1], dz_mix(s[1], 7)); return dz_answer(0x1000u);                     /* 0x459b94 */
    case ']': dz_note(']', s[0], 0, 0); return dz_answer(0x1000000u) << 8;                              /* a prefs callback */
    case '1': case '2': case '5': case '6': case '(': case ')': case '[': dz_note(l, s[0], 0, 0); return dz_answer(0x100u);
    default: dz_note(l, s[0], s[1], s[2]); return dz_answer(0x100u);                                   /* 4 7 8 & */
    }
}
#define JZ_O(N, L, K) \
    static uint32_t __cdecl jz_o_##N(uint32_t a, uint32_t b, uint32_t c, uint32_t d) { \
        uint32_t s[4] = {a, b, c, d}; \
        for (unsigned i = (K); i < 4u; ++i) s[i] = 0; \
        return jz_stub(L, s); \
    }
JZ_O(open, '1', 1) JZ_O(close, '2', 1) JZ_O(step, '3', 1) JZ_O(focus, '4', 2) JZ_O(next, '5', 1) JZ_O(previous, '6', 1)
JZ_O(press, '7', 3) JZ_O(key, '8', 2) JZ_O(cycle, '9', 3) JZ_O(back, '!', 3) JZ_O(joy, '@', 1) JZ_O(summary, '#', 0)
JZ_O(map, '$', 1) JZ_O(set, '%', 3) JZ_O(store, '^', 2) JZ_O(release, '&', 2) JZ_O(record, '*', 0)
JZ_O(gdraw, '(', 1) JZ_O(gredraw, ')', 1) JZ_O(gtick, '[', 1) JZ_O(callback, ']', 1)
#undef JZ_O
static const dz_stub_t jz_patched[] = {
    {0x0045464cu, (void *)jz_o_open, '1', 1}, {0x004548c8u, (void *)jz_o_close, '2', 1}, {0x00454accu, (void *)jz_o_step, '3', 1},
    {0x00454c98u, (void *)jz_o_focus, '4', 2}, {0x00454d98u, (void *)jz_o_next, '5', 1}, {0x00454dbcu, (void *)jz_o_previous, '6', 1},
    {0x00454de8u, (void *)jz_o_press, '7', 3}, {0x0045b734u, (void *)jz_o_key, '8', 2}, {0x004599d4u, (void *)jz_o_cycle, '9', 3},
    {0x00459a10u, (void *)jz_o_back, '!', 3}, {0x0045c524u, (void *)jz_o_joy, '@', 1}, {0x00477904u, (void *)jz_o_summary, '#', 0},
    {0x0045a274u, (void *)jz_o_map, '$', 1}, {0x00459958u, (void *)jz_o_set, '%', 3}, {0x00459b94u, (void *)jz_o_store, '^', 2},
    {0x004530bcu, (void *)jz_o_release, '&', 2}, {0x0045a810u, (void *)jz_o_record, '*', 0},
};
#define JZ_PATCHED (sizeof(jz_patched) / sizeof(jz_patched[0]))
static const dz_stub_t jz_hosts[] = {
    {0, (void *)jz_o_gdraw, '(', 1}, {0, (void *)jz_o_gredraw, ')', 1}, {0, (void *)jz_o_gtick, '[', 1}, {0, (void *)jz_o_callback, ']', 1},
};
#define JZ_HOSTS (sizeof(jz_hosts) / sizeof(jz_hosts[0]))
static uint32_t jz_host(char letter) {
    for (unsigned i = 0; i < JZ_HOSTS; ++i)
        if (jz_hosts[i].letter == letter) return (uint32_t)(uintptr_t)jz_hosts[i].stub;
    return 0;
}
static BYTE jz_saved[JZ_PATCHED][5];

/* ---- the own side */
static void jz_s_stub(es2_sim_service_call *call, void *user) {
    uintptr_t u = (uintptr_t)user;
    unsigned n = (unsigned)(u >> 8);
    uint32_t s[4] = {0, 0, 0, 0};
    for (unsigned i = 0; i < n && i < 4u; ++i) s[i] = call->slots[i];
    char l = (char)(u & 0xffu);
    int mine = 0;
    for (unsigned i = 0; i < JZ_PATCHED; ++i) mine |= jz_patched[i].letter == l;
    for (unsigned i = 0; i < JZ_HOSTS; ++i) mine |= jz_hosts[i].letter == l;
    call->eax = mine ? jz_stub(l, s) : dz_stub(l, s);
}
static int jz_stub_entry(uint32_t va, char letter, unsigned slots) {
    void *user = (void *)(uintptr_t)((unsigned char)letter | slots << 8);
    for (unsigned i = 0; i < jz_dispatch.count; ++i) {
        es2_sim_entry *e = &jz_dispatch.entries[i];
        if (e->va != va) continue;
        e->kind = ES2_SIM_SERVICE;
        e->name = "joystick page stub";
        e->descriptor = NULL;
        e->extension = NULL;
        e->service = jz_s_stub;
        e->service_user = user;
        e->pop_bytes = 0;
        e->stack_slots = (uint8_t)slots;
        return 0;
    }
    return es2_sim_dispatch_add_service(&jz_dispatch, va, "joystick page stub", jz_s_stub, user, 0, (uint8_t)slots);
}
static const uint32_t jz_rows[] = {0x00457cf8u, 0x00457d38u, 0x004585f4u, 0x0045866cu, 0x00458c84u, 0x00458d3cu, 0x00458ed8u, 0x00458fb8u,
                                   0x00459148u, 0x0045915cu, 0x00459314u, 0x0045933cu, 0x004593a8u, 0x00459414u, 0x00459894u};
static int jz_dispatch_setup(void) {
    es2_sim_dispatch_init(&jz_dispatch, jz_entries, 4096, dz_refuse, NULL);
    static es2_sim_services s;
    s.memory = &ir_memory;
    s.heap = &ir_heap;
    s.vt = &lc_vt;
    s.fs0 = NULL;
    if (es2_sim_register_services(&jz_dispatch, &s)) return fail("joystick page: dispatch");
    for (unsigned i = 0; i < DZ_PATCHED; ++i)
        if (jz_stub_entry(dz_patched[i].va, dz_patched[i].letter, dz_patched[i].slots)) return fail("joystick page: dispatch (stub)");
    for (unsigned i = 0; i < DZ_HOSTS; ++i)
        if (jz_stub_entry((uint32_t)(uintptr_t)dz_hosts[i].stub, dz_hosts[i].letter, dz_hosts[i].slots)) return fail("joystick page: dispatch (slot)");
    for (unsigned i = 0; i < JZ_PATCHED; ++i)
        if (jz_stub_entry(jz_patched[i].va, jz_patched[i].letter, jz_patched[i].slots)) return fail("joystick page: dispatch (own stub)");
    for (unsigned i = 0; i < JZ_HOSTS; ++i)
        if (jz_stub_entry((uint32_t)(uintptr_t)jz_hosts[i].stub, jz_hosts[i].letter, jz_hosts[i].slots)) return fail("joystick page: dispatch (own slot)");
    const char *error = NULL;
    uint32_t bad = 0;
    if (es2_sim_dispatch_seal(&jz_dispatch, &error, &bad)) {
        fprintf(stderr, "seal: %s at 0x%08lx\n", error ? error : "?", (unsigned long)bad);
        return fail("joystick page: dispatch seal");
    }
    for (unsigned i = 0; i < sizeof(jz_rows) / sizeof(jz_rows[0]); ++i) {
        const es2_sim_entry *e = es2_sim_dispatch_find(&jz_dispatch, jz_rows[i]);
        if (!e || e->kind != ES2_SIM_SERVICE || e->service == jz_s_stub) return fail("joystick page: a row is missing");
    }
    return 0;
}

/* ---- a side: dialog_ctors.h's world, then the extras */
static uint32_t jz_seed;
static uint32_t jz_rand(uint32_t n) {
    jz_seed = jz_seed * 1103515245u + 12345u;
    return (jz_seed >> 8) % n;
}
static int jz_side_begin(unsigned side, uint32_t seed, uint32_t answers) {
    if (dz_side_begin(side, seed, answers)) return 1;
    lc_loop.dispatch = &jz_dispatch;
    jz_seed = seed ^ 0x5a5a1234u;
    jz_passes = 0;
    jz_world *w = &jz_w;
    memset(w, 0, sizeof(*w));
    w->record = dz_alloc(0x30u);
    w->joy = dz_alloc(4u);
    dz_put8(w->joy, (uint8_t)(jz_rand(4u) ? 1u | jz_rand(256u) : jz_rand(128u) * 2u));
    w->summary = dz_alloc(8u);
    dz_put16(w->summary, (uint16_t)(jz_rand(5u) ? 1u + jz_rand(2u) : 0u));
    dz_put16(w->summary + 2u, (uint16_t)((int32_t)jz_rand(11u) - 1));
    for (uint32_t i = 4; i < 7u; ++i) dz_put8(w->summary + i, (uint8_t)(jz_rand(3u) ? 1u + jz_rand(3u) : 0u));
    w->map = dz_alloc(12u);
    for (uint32_t i = 0; i < 12u; ++i) dz_put8(w->map + i, (uint8_t)(i < 4u ? jz_rand(3u) : jz_rand(22u)));
    w->gvt = dz_alloc(0x10u);
    dz_put32(w->gvt, jz_host('('));
    dz_put32(w->gvt + 4u, jz_host(')'));
    dz_put32(w->gvt + 0xcu, jz_host('['));
    /* DATA: the page's frame slot, layout, pref base, prefs, guards, tables. */
    dz_set32(0x0049e804u + 4u, dz_host('f'));
    dz_set8(0x004d2571u, (uint8_t)jz_rand(2u));
    dz_set8(0x004d2577u, (uint8_t)(jz_rand(4u) ? jz_rand(0x2bu) : jz_rand(256u)));
    for (uint32_t n = 0; n < 0x36u; ++n) {
        uint8_t v = (uint8_t)jz_rand(21u);
        dz_set8(0x004d1f38u + n, v);
        dz_set8(0x004d1f6eu + n, jz_rand(3u) ? v : (uint8_t)jz_rand(21u));
        dz_set8(0x004d1fa4u + n, (uint8_t)jz_rand(256u));
        dz_set32(0x004d1fdcu + 4u * n, jz_rand(3u) ? 0u : jz_host(']'));
    }
    dz_set8(0x0049e5f0u, (uint8_t)(jz_rand(3u) ? 0u : 1u));
    dz_set8(0x0049e7d4u, (uint8_t)(jz_rand(2u) ? 0u : 1u + jz_rand(3u)));
    if (!jz_rand(4u))
        for (uint32_t i = 0; i < 2u * 104u; ++i) dz_set8(0x0049e5f1u + i, (uint8_t)(jz_rand(9u) ? 1u + jz_rand(20u) : 0u));
    if (side) {
        if (memcmp(&jz_w, &jz_w0, sizeof(jz_w))) return fail("joystick page: the sides' worlds differ (heap order)");
    } else {
        jz_w0 = jz_w;
    }
    return 0;
}
static uint32_t jz_call(uint32_t va, const uint32_t *a, unsigned n) {
    if (dz_side) {
        es2_sim_result res;
        uint32_t regs[3] = {0, 0, 0};
        memset(&res, 0, sizeof(res));
        if (es2_sim_call(&jz_dispatch, va, a, n, regs, &res)) dz_bad = 1;
        return res.eax;
    }
    uint32_t d = 0;
    return lc_machine(va, a, n, &d);
}

/* ---- the original side's coverage, read before a call */
static uint8_t jz_g8(uint32_t va) { return mapped[va - 0x400000u]; }
static int jz_axis_ok(uint32_t o, uint32_t i) {
    uint32_t s = dz_get(o + 0x47au);
    return s && (int32_t)i < (int32_t)(int16_t)get16((BYTE *)(uintptr_t)(s + 2u));
}
static uint32_t jz_slot(uint32_t o, uint32_t g) {
    for (int32_t k = 0; k < (int32_t)dz_get(o + 0x2cdu); ++k)
        if (dz_get(o + 0x291u + 4u * (uint32_t)k) == g) return (uint32_t)k;
    return 0xffffffffu;
}
static void jz_cover(unsigned op, uint32_t o, const uint32_t *a) {
    switch (op) {
    case 0: ++jz_cv[JZC_DRAW]; break;
    case 1: break;   /* the run: counted in the joystick stubs and the step */
    case 2: {
        uint32_t k = jz_slot(o, a[1]);
        if (k == 0xffffffffu) { ++jz_cv[JZC_SEL_NONE]; break; }
        if (k < 12u) ++jz_cv[dz_get(o + 0x472u) != 0xffffffffu ? JZC_SEL_HIGHLIGHT : JZC_SEL_FIRST];
        if (k >= 4u && k < 12u && k - 4u != dz_get(o + 0x476u)) ++jz_cv[JZC_SEL_COLUMN];
        else ++jz_cv[JZC_SEL_PENDING];
        if (k == 13u) ++jz_cv[JZC_SEL_END];
        break;
    }
    case 3: {
        uint32_t r = a[1];
        uint32_t first = 0xffffffffu;
        for (uint32_t n = 0; n < 8u && first == 0xffffffffu; ++n)
            if (n ? dz_get8(r + 0x16u + n) : dz_get8(r + 0xdu)) first = n;
        if (first != 0xffffffffu)
            ++jz_cv[first == dz_get(o + 0x476u) ? JZC_KEYS_STEP : jz_axis_ok(o, first) ? JZC_KEYS_COLUMN : JZC_KEYS_BIT_IDLE];
        switch (get16((BYTE *)(uintptr_t)r)) {
        case 1: ++jz_cv[JZC_KEY_1]; break;
        case 0xf: ++jz_cv[JZC_KEY_F]; break;
        case 0x1c: ++jz_cv[dz_get(o + 0x2f7u) != 0xffffffffu ? JZC_KEY_1C : JZC_KEY_1C_FOCUS]; break;
        case 0x52: ++jz_cv[JZC_KEY_52]; break;
        case 0x80f: ++jz_cv[JZC_KEY_80F]; break;
        default: ++jz_cv[JZC_KEY_OTHER]; break;
        }
        break;
    }
    case 4: {
        uint32_t k = a[1], s = dz_get(o + 0x47au);
        if (k > 12u || !k) { ++jz_cv[JZC_LABEL_OUT]; break; }
        int drawn = k == 1u ? s && get16((BYTE *)(uintptr_t)s) : k <= 4u ? s && dz_get8(s + 2u + k) : jz_axis_ok(o, k - 5u);
        ++jz_cv[!drawn ? JZC_LABEL_SKIP : k == 1u ? JZC_LABEL_1 : k <= 4u ? JZC_LABEL_OPT : JZC_LABEL_AXIS];
        break;
    }
    case 5: ++jz_cv[JZC_TEXT]; break;
    case 6: ++jz_cv[jz_g8(0x0049e7d4u) ? JZC_STORE_KEPT : JZC_STORE_FIRST]; break;
    case 7: ++jz_cv[a[1] == dz_get(o + 0x476u) ? JZC_ROW_SAME : JZC_ROW_NEW]; break;
    case 8: case 9: {
        uint8_t v = dz_get8(o + 0x462u + a[1]), n = dz_get8(o + 0x46au + a[1]);
        if (op == 8) ++jz_cv[(uint8_t)(v + 1u) > n ? JZC_NEXT_WRAP : JZC_NEXT];
        else ++jz_cv[(uint8_t)(v - 1u) == 0xffu ? JZC_BACK_WRAP : JZC_BACK];
        break;
    }
    case 10: ++jz_cv[a[1] == 0xffffffffu ? JZC_COL_NONE : JZC_COL_ROWS]; break;
    case 11: {
        int call = 0, change = 0;
        for (uint32_t n = 0; n < 0x36u; ++n) {
            if (jz_g8(0x004d1f6eu + n) == jz_g8(0x004d1f38u + n)) continue;
            change = 1;
            if ((a[0] & 0xffu) && get32(mapped + (0x004d1fdcu + 4u * n - 0x400000u))) call = 1;
        }
        ++jz_cv[call ? JZC_COMMIT_CALL : change ? JZC_COMMIT_SILENT : JZC_COMMIT_SAME];
        break;
    }
    default: ++jz_cv[JZC_TABLE]; break;
    }
}

/* ---- one case: constructor, state, ops, destructor */
static const struct { uint32_t va; unsigned n; uint32_t mask; const char *name; } jz_ops[13] = {
    {0x00458c84u, 1, ~0u, "draw"}, {0x0045866cu, 1, ~0u, "run"}, {0x00458ed8u, 3, ~0u, "select"}, {0x00458fb8u, 2, 0xffu, "keys"},
    {0x00458d3cu, 2, ~0u, "label"}, {0x00459148u, 2, ~0u, "text"}, {0x0045915cu, 1, ~0u, "store"}, {0x00459314u, 2, 0u, "row"},
    {0x0045933cu, 2, ~0u, "next"}, {0x004593a8u, 2, ~0u, "back"}, {0x00459414u, 2, ~0u, "column"}, {0x00459894u, 1, ~0u, "commit"},
    {0x00457cf8u, 2, ~0u, "table"},
};
static int jz_case(unsigned k) {
    uint32_t seed = random32(), answers = random32();
    uint32_t save = random32() % 3u == 0 ? 1u + random32() % 0xffu : 0x100u * (random32() % 2u);
    int at = random32() % 2u, flags = (int)(random32() % 4u), drop = random32() % 3u == 0;
    unsigned ops = 1u + random32() % JZ_OPS, list[JZ_OPS];
    uint32_t args[JZ_OPS][3], state[24];
    for (unsigned i = 0; i < 24u; ++i) state[i] = random32();
    for (unsigned i = 0; i < ops; ++i) {
        list[i] = random32() % 13u;
        args[i][0] = random32();
        args[i][1] = random32();
        args[i][2] = random32();
    }
    unsigned limit = 1u + random32() % 8u;
    static uint32_t eax[2][JZ_OPS + 1];
    for (unsigned side = 0; side < 2u; ++side) {
        if (jz_side_begin(side, seed, answers)) return 1;
        jz_pass_limit = limit;
        uint32_t o = dz_w.obj, a[3];
        jz_page = o;
        uint8_t once = jz_g8(0x0049e5f0u), layout = jz_g8(0x004d2571u);
        a[0] = o; a[1] = save; a[2] = at ? dz_w.small : 0u;
        eax[side][0] = jz_call(0x00457d38u, a, 3);
        if (dz_bad || lc_bad) { fprintf(stderr, "FAIL: joystick page case %u: the constructor failed (side %u)\n", k, side); return 1; }
        if (!side) {
            ++jz_cv[at ? JZC_SMALL : JZC_CENTRED];
            ++jz_cv[layout ? JZC_LAYOUT_B : JZC_LAYOUT_A];
            ++jz_cv[once ? JZC_ONCE_KEPT : JZC_ONCE_FILL];
            for (uint32_t i = 0; i < 8u; ++i)
                if (dz_get8(o + 0x46au + i) == 13u) { ++jz_cv[JZC_FULL_ROW]; break; }
        }
        /* The page state and the gadgets' tables. */
        for (uint32_t g = 0; g < dz_get(o + 0x2cdu) && g < 14u; ++g) dz_put32(dz_get(o + 0x291u + 4u * g) + 0x17u, jz_w.gvt);
        dz_put32(o + 0x47au, state[0] % 5u ? jz_w.summary : 0u);
        for (uint32_t m = 0; m < 4u; ++m) dz_put8(o + 0x45eu + m, (uint8_t)(state[1 + m] % 3u));
        for (uint32_t i = 0; i < 8u; ++i) dz_put8(o + 0x462u + i, (uint8_t)(state[5 + i] % 4u ? state[5 + i] % 13u : 0u));
        dz_put32(o + 0x476u, state[13] % 4u ? state[13] % 8u : 0xffffffffu);
        dz_put32(o + 0x472u, state[14] % 4u ? state[14] % 14u : 0xffffffffu);
        dz_put32(o + 0x45au, state[15] % 3u ? 0u : state[15] % 16u);
        dz_put8(o + 0x2ffu, (uint8_t)(state[16] % 2u));
        dz_put32(o + 0x2fbu, state[17] % 4u ? state[17] % 14u : 0xffffffffu);
        dz_put32(o + 0x2f7u, state[18] % 3u ? state[18] % 14u : 0xffffffffu);
        dz_put8(o + 0x2d1u, 0);
        for (unsigned i = 0; i < ops && !dz_bad && !lc_bad; ++i) {
            uint32_t x = args[i][0], y = args[i][1], z = args[i][2];
            a[0] = o;
            switch (list[i]) {
            case 1: dz_put8(o + 0x2d1u, 0); jz_passes = 0; break;
            case 2: a[1] = x % 8u ? dz_get(o + 0x291u + 4u * (y % 14u)) : y; a[2] = z % 4u; break;
            case 3: {
                uint32_t r = jz_w.record;
                static const uint16_t keys[] = {0, 1, 0xf, 0x1c, 0x52, 0x80f, 0x30, 0x80e};
                for (uint32_t b = 0; b < 0x30u; b += 4u) dz_put32(r + b, 0);
                dz_put16(r, keys[x % 8u]);
                dz_put8(r + 0xdu, (uint8_t)(y % 3u ? 0u : 1u + y % 200u));
                for (uint32_t b = 0; b < 7u; ++b) dz_put8(r + 0x17u + b, (uint8_t)((z >> (4u * b)) % 5u == 0));
                a[1] = r;
                break;
            }
            case 4: a[1] = x % 16u ? x % 14u : 0xffffffffu; break;
            case 5: a[1] = x % 14u; break;
            case 7: case 10: a[1] = x % 6u ? y % 8u : 0xffffffffu; break;
            case 8: case 9: a[1] = x % 8u; break;
            case 11: a[0] = x % 3u ? 1u + 0x100u * (y % 4u) : 0x100u * (y % 2u); break;
            case 12: a[0] = x % 6u ? x % 8u : (uint32_t)(int32_t)-1 & 0xffffu; a[1] = y % 7u ? y % 13u : 0xffffu; break;
            default: break;
            }
            if (side == 0) jz_cover(list[i], o, a);
            eax[side][1 + i] = jz_call(jz_ops[list[i]].va, a, jz_ops[list[i]].n) & jz_ops[list[i]].mask;
            if (side == 0 && list[i] == 1) {
                uint32_t su = dz_get(o + 0x47au);
                ++jz_cv[!su ? JZC_RUN_ABSENT : get16((BYTE *)(uintptr_t)su) ? JZC_RUN_SUMMARY : JZC_RUN_NONE];
            }
        }
        if (dz_bad || lc_bad) { fprintf(stderr, "FAIL: joystick page case %u: an op failed (side %u)\n", k, side); return 1; }
        if (drop) dz_put32(0x004d1f34u, 0u);
        if (!side) {
            ++jz_cv[dz_get(0x004d1f34u) ? JZC_DTOR_BITMAP : JZC_DTOR_NO_BITMAP];
            if (flags & 1) ++jz_cv[JZC_DTOR_DELETE];
        }
        a[0] = o; a[1] = (uint32_t)flags;
        jz_call(0x004585f4u, a, 2);
        if (dz_bad || lc_bad) { fprintf(stderr, "FAIL: joystick page case %u: the destructor failed (side %u)\n", k, side); return 1; }
        su_take(side);
    }
    for (unsigned i = 0; i <= ops; ++i)
        if (eax[0][i] != eax[1][i]) {
            fprintf(stderr, "FAIL: joystick page case %u: %s (op %u) EAX %08lx / %08lx\n", k, i ? jz_ops[list[i - 1]].name : "ctor", i,
                    (unsigned long)eax[0][i], (unsigned long)eax[1][i]);
            return 1;
        }
    if (su_compare("joystick page", k)) {
        fprintf(stderr, "  ops:");
        for (unsigned i = 0; i < ops; ++i) fprintf(stderr, " %s", jz_ops[list[i]].name);
        fprintf(stderr, "\n");
        return 1;
    }
    if (dz_hash[0] != dz_hash[1] || dz_count[0] != dz_count[1]) {
        fprintf(stderr, "FAIL: joystick page case %u: stub calls differ (%u / %u)\n  original: %s\n  own:      %s\n", k, dz_count[0], dz_count[1],
                dz_text[0], dz_text[1]);
        return 1;
    }
    return 0;
}

static int joystick_page_cases(void) {
    current_function = "joystick page";
    es2_sim_identity_memory(&ir_memory);
    es2_sim_rtl_memory(&ir_memory, &su_rtl);
    su_data_pristine = malloc(SU_DATA_SIZE);
    su_snap[0] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    su_snap[1] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    ir_region = ww_alloc(IR_REGION_SIZE);
    lc_stack = VirtualAlloc(NULL, 0x10000u, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    lc_bits = ww_alloc(LC_BITS_SIZE);
    if (!su_data_pristine || !su_snap[0] || !su_snap[1] || !ir_region || !lc_stack || !lc_bits) return fail("joystick page: memory");
    if (get32(mapped + (SU_COUNTER - 0x400000u))) return fail("joystick page: the counter word is not free");
    su_pristine_take();
    static const struct { const char *dll, *name; void *stub; } imports[] = {
        {"cw3220.dll", "_calloc", (void *)ir_calloc}, {"cw3220.dll", "_free", (void *)ir_free},
        {"KERNEL32.dll", "GetTickCount", (void *)lc_tick},
    };
    for (unsigned i = 0; i < sizeof(imports) / sizeof(imports[0]); ++i)
        if (!ir_patch_import(imports[i].dll, imports[i].name, imports[i].stub)) return fail("joystick page: import slot not found");
    for (unsigned i = 0; i < DZ_PATCHED; ++i)
        if (dz_patch(dz_patched[i].va, dz_patched[i].stub, dz_saved[i], 1)) return 1;
    for (unsigned i = 0; i < JZ_PATCHED; ++i)
        if (dz_patch(jz_patched[i].va, jz_patched[i].stub, jz_saved[i], 1)) return 1;
    if (jz_dispatch_setup()) return 1;
    int bad = 0;
    unsigned cases = 3000;
    for (unsigned k = 0; !bad && k < cases; ++k) {
        current_case = k; current_case_valid = 1;
        bad = jz_case(k);
    }
    if (!bad) printf("PASS: joystick page page: %u cases identical\n", cases);
    if (!bad) {
        printf("joystick page coverage:");
        for (unsigned i = 0; i < JZC_COUNT; ++i) printf(" %s %u%s", jz_cv_names[i], jz_cv[i], i + 1u < JZC_COUNT ? "," : "\n");
        for (unsigned i = 0; i < JZC_COUNT; ++i)
            if (!jz_cv[i]) { fprintf(stderr, "FAIL: joystick page: path '%s' never taken\n", jz_cv_names[i]); bad = 1; }
    }
    current_case_valid = 0;
    for (unsigned i = 0; i < JZ_PATCHED; ++i) dz_patch(jz_patched[i].va, NULL, jz_saved[i], 0);
    for (unsigned i = 0; i < DZ_PATCHED; ++i) dz_patch(dz_patched[i].va, NULL, dz_saved[i], 0);
    put32(mapped + 0x2cbee4u, 0);
    memcpy(mapped + SU_DATA_RVA, su_data_pristine, SU_DATA_SIZE);
    return bad;
}
