/* The cockpit jolt and bump, original machine code against sim/src/cockpit.c
 * and the hud_load.h rows it works with (--cockpit; target
 * :differential_cockpit_test). Included after scene_cases.h; reuses
 * startup_cases.h's DATA reset and snapshots (su_reset, su_take, su_compare),
 * loop_cases.h's loop set-up (lc_reset) and audio_cases.h's register-capturing
 * machine call (au_call).
 *
 * Under test, on both sides (the original's machine code against the own rows):
 * the hit entries 0x434010 (jolt) and 0x434144 (bump), the jolt start 0x42d2f8,
 * the jolt step 0x42d4a8, the bump reset 0x42d820 (src/cockpit.c); the
 * per-frame jolt 0x43408c and bump 0x434194, the jolt end 0x42d624, the GC bias
 * 0x42d82c/0x42d854 and the jolt record 0x42d870; reached inside them and run
 * as code on both sides: the display getter 0x429820, the palette flashes
 * 0x42ea44/0x42ea70/0x42ea94 and the panel move 0x43271c with its widget and
 * slide moves.
 *
 * Recording stand-ins (a `jmp rel32` over the callee on the original's
 * side, a service at the VA on the own side; the rows 0x467828, 0x4303b0,
 * 0x488012, 0x486704, 0x488143 and 0x481350 replaced after the seal): the
 * clock 0x467828 and stream B 0x492ee0 (scripted answers, so the coupling
 * calls show in the log at their order), the palette swap 0x4303b0, the
 * strip save 0x488012, the fill 0x486704 (with the colour record GC+0x22c
 * points at), the strip restore 0x488143, the surface attribute 0x481350,
 * and the driver's page-origin and page-select slots [0x4a57e8]/[0x4a57f0]
 * (with the point they are handed). The fill and the restore may switch
 * the current view [0x6c5ed0] (the "view no longer current" branches).
 *
 * Compared after each case: the stand-in logs (order and arguments), DATA
 * and the heap, the world (GC+0x22c, which 0x42d2f8 leaves pointing at its
 * dead stack record, is compared as "written or not") and the registers
 * the contract names (EAX; AL for the per-frame functions, whose upper
 * bits are the panel move's leftovers; none for 0x42d4a8 and 0x42d624).
 *
 * Rest, part intro: the intro set-up 0x45d548 (the row intro_setup,
 * src/cockpit.c) on a random palette P = [0x49b020] (JB_PAL, 256 entries at
 * JB_PAL_ENTRIES). During the part only, the empty 0x42eb40 ('G'), the palette
 * writes 0x4303c4 ('E', with a hash of the 16 entries handed) and the fade
 * set-up 0x4308fc ('F', with both palette objects' fields) are stand-ins on
 * both sides; operator new / new[] run as code (the original's through _calloc
 * on the harness heap, compared with DATA). */
#pragma once
/* The headers this suite builds on (sim_main.c). */
#include "init_records_cases.h"
#include "startup_cases.h"
#include "loop_cases.h"
#include "audio_cases.h"
#include "es2/sim/hud.h"

#define JB_WORLD_SIZE 0x3800u
#define JB_GC 0x0000u        /* the graphics context (0x240) */
#define JB_V 0x0300u         /* the view surface */
#define JB_V2 0x0400u        /* another view the stand-ins may make current */
#define JB_D 0x0500u         /* the cockpit display */
#define JB_LISTS 0x0600u     /* the display's per-mode item lists (5 x 0x204) */
#define JB_P 0x1100u         /* the panel */
#define JB_W 0x1400u         /* the panel's root widget */
#define JB_CANVAS 0x1700u    /* the panel's canvas (+0x220) */
#define JB_RECTS 0x1a00u     /* 8 rectangles of 0x10 */
#define JB_ITEMS 0x1b00u     /* 16 items of 0x20 (entry = item + 4) */
#define JB_WORDS 0x1d00u     /* 16 words the kind-2 items point at */
#define JB_ROWS 0x1e00u      /* the row record */
#define JB_PAL 0x2000u       /* the palette P of 0x45d548 */
#define JB_PAL_ENTRIES 0x2400u
#define JB_PLAYER 0x2800u    /* --cockpit-rest: the drop's player P = [0x4d24e6] */
#define JB_CAMERA 0x2840u    /* the camera C = [0x4d24ea] */
#define JB_BITMAP 0x2860u    /* the intro bitmap B */
#define JB_RECORD 0x2880u    /* the loaded record (+8: the list) */
#define JB_LIST 0x2890u      /* the bitmap list ([0] = B) */
#define JB_HUD 0x28a0u       /* [[0x49b088]+0x1f5] and its method table */
#define JB_HUD_VT 0x28b0u
#define JB_PALOBJ 0x28c0u    /* the two palette objects (0x20 each) */
#define JB_PAL_VT 0x2900u
#define JB_GC2 0x2a00u       /* a second graphics context the present stand-in may make current */
#define JB_CLIP 0x2c80u      /* the panel part: the clip record of the current surface (0x230) */
#define JB_H 0x2f00u         /* the panel object H of 0x431bf8 (0x260) */
#define JB_GAUGES 0x3200u    /* 12 panel elements of 0x40 */
#define JB_VEHICLE 0x3500u   /* the player vehicle [H+0x203] and [[0x49b088]+0x203] (0x210) */
#define JB_VREC 0x3710u      /* its record [vehicle+0x1f2] (0x60) */

static BYTE *jb_world, *jb_world_start;   /* start + the kept side-0 result */
static es2_sim_entry jb_entries[4096];
static es2_sim_dispatch jb_dispatch;
static es2_sim_hud jb_hud;
static int jb_bad;
static unsigned jb_side;

/* ---- the stand-in log of a side */
static uint64_t jb_hash[2];
static unsigned jb_count[2];
static char jb_text[2][4096];
static size_t jb_len[2];
static unsigned jb_letters[128];
static void jb_note(char letter, uint32_t a, uint32_t b, uint32_t c) {
    uint64_t h = jb_hash[jb_side];
    const uint32_t v[4] = {(uint32_t)letter, a, b, c};
    for (unsigned i = 0; i < 4u; ++i) { h ^= v[i]; h *= 0x100000001b3ull; }
    jb_hash[jb_side] = h;
    ++jb_count[jb_side];
    if (!jb_side) ++jb_letters[(unsigned char)letter & 127u];
    if (jb_len[jb_side] + 48u < sizeof(jb_text[0]))
        jb_len[jb_side] += (size_t)sprintf(jb_text[jb_side] + jb_len[jb_side], "%c(%lx,%lx,%lx) ", letter, (unsigned long)a,
                                           (unsigned long)b, (unsigned long)c);
}
static uint32_t jb_get(uint32_t a) { return get32((const BYTE *)(uintptr_t)a); }
static void jb_put(uint32_t a, uint32_t v) { put32((BYTE *)(uintptr_t)a, v); }
static uint32_t jb_at(uint32_t offset) { return (uint32_t)(uintptr_t)jb_world + offset; }

/* ---- the scripted answers */
static uint32_t jb_clock[12], jb_rng[12], jb_attr[2];
static unsigned jb_clock_i, jb_rng_i, jb_switch;
static uint32_t jb_clock_step;   /* nonzero, the clock is clock[0] + i * step (it never stalls) */
static unsigned jr_fills;        /* the fill stand-in's calls (the drop's branch keys) */
static void jb_maybe_switch(void) {
    if (jb_switch && !--jb_switch) jb_put(0x006c5ed0u, jb_at(JB_V2));
}
/* An address outside the world and the image (a stack local of either
 * side) is logged as a tag. */
static uint32_t jb_norm(uint32_t v) {
    uint32_t w = (uint32_t)(uintptr_t)jb_world;
    if (v - w < JB_WORLD_SIZE || v - 0x00400000u < 0x00400000u) return v;
    return 0x57ac0000u;
}
static uint32_t jr_act(char l, const uint32_t *a);
static uint32_t jb_act(char l, const uint32_t *a) {
    switch (l) {
    case 'T': {
        uint32_t v = jb_clock_step ? jb_clock[0] + jb_clock_i * jb_clock_step : jb_clock[jb_clock_i < 12u ? jb_clock_i : 11u];
        ++jb_clock_i;
        jb_note('T', v, 0, 0);
        return v;
    }
    case 'N': {
        uint32_t v = jb_rng[jb_rng_i < 12u ? jb_rng_i : 11u];
        ++jb_rng_i;
        jb_note('N', a[0], v, 0);
        return v;
    }
    case 'W': jb_note('W', a[0], 0, 0); return a[0] * 3u + 1u;
    case 'S': jb_note('S', a[0], a[1], 0); jb_note('s', jb_get(a[0]), jb_get(a[0] + 4u), jb_get(a[0] + 8u)); return 0x5au;
    case 'R': {
        uint32_t rec = jb_get(jb_get(0x006c5f70u) + 0x22cu);
        jb_note('R', jb_norm(a[0]), jb_get(a[0]), jb_get(a[0] + 4u));
        jb_note('r', jb_get(a[0] + 8u), jb_get(a[0] + 12u), 0);
        jb_note('c', jb_get(rec), jb_get(rec + 4u), 0);
        ++jr_fills;
        jb_maybe_switch();
        return 0x6bu;
    }
    case 'B':
        jb_note('B', a[0], a[1], a[2]);
        jb_note('b', jb_get(jb_get(0x006c5f70u) + 0x208u), jb_get(jb_get(0x006c5f70u) + 0x20cu), 0);
        jb_maybe_switch();
        return 0x7cu;
    case 'A': jb_note('A', a[0], a[1], 0); return jb_attr[a[1] ? 1 : 0];
    case 'o': jb_note('o', a[0], jb_get(a[1]), jb_get(a[1] + 4u)); return 0x8du;
    case 'p': jb_note('p', a[0], 0, 0); return 0x9eu;
    case 'G': jb_note('G', a[0], 0, 0); return 0;
    case 'E': {
        uint32_t h = 2166136261u;
        for (unsigned i = 0; i < 16u; ++i) { h ^= jb_get(a[2] + 4u * i); h *= 16777619u; }
        jb_note('E', a[0], a[1], h);
        return 0;
    }
    case 'F':
        for (unsigned k = 0; k < 2u; ++k)
            jb_note('F', jb_get(a[k]), jb_get(a[k] + 4u) ^ jb_get(a[k] + 0xcu), jb_get(a[k] + 0x10u) ^ (jb_get(a[k] + 8u) - a[k]));
        jb_note('f', a[2], 0, 0);
        return 0xf00d0000u + a[2];
    default: return jr_act(l, a);
    }
}
#define JB_O(name, letter, n)                                                                                   \
    static uint32_t __cdecl jb_o_##name(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3) {                     \
        uint32_t a[4] = {a0, a1, a2, a3};                                                                       \
        for (unsigned i = (n); i < 4u; ++i) a[i] = 0;                                                           \
        return jb_act(letter, a);                                                                               \
    }
JB_O(clock, 'T', 0) JB_O(rng, 'N', 1) JB_O(swap, 'W', 1) JB_O(save, 'S', 2) JB_O(fill, 'R', 1) JB_O(restore, 'B', 3)
JB_O(attr, 'A', 2) JB_O(origin, 'o', 2) JB_O(page, 'p', 1)
JB_O(intro_empty, 'G', 1) JB_O(intro_entries, 'E', 3) JB_O(intro_fade, 'F', 3)
typedef struct jb_stub_t { uint32_t va; void *fn; char letter; unsigned slots; } jb_stub_t;
static const jb_stub_t jb_stubs[] = {
    {0x00467828u, (void *)jb_o_clock, 'T', 0}, {0x00492ee0u, (void *)jb_o_rng, 'N', 1},
    {0x004303b0u, (void *)jb_o_swap, 'W', 1}, {0x00488012u, (void *)jb_o_save, 'S', 2},
    {0x00486704u, (void *)jb_o_fill, 'R', 1}, {0x00488143u, (void *)jb_o_restore, 'B', 3},
    {0x00481350u, (void *)jb_o_attr, 'A', 2},
    /* the driver slots: no VA of the original */
    {0, (void *)jb_o_origin, 'o', 2}, {0, (void *)jb_o_page, 'p', 1},
};
#define JB_STUBS (sizeof(jb_stubs) / sizeof(jb_stubs[0]))
/* The part's own stand-ins after jb_stubs (index JB_STUBS + i). */
static const jb_stub_t *jb_extra;
static unsigned jb_extra_count;
static const jb_stub_t *jb_stub_at(unsigned i) { return i < JB_STUBS ? &jb_stubs[i] : &jb_extra[i - JB_STUBS]; }
static BYTE jb_saved[JB_STUBS + 64u][5];
static uint32_t jb_fn(void *f) { return (uint32_t)(uintptr_t)f; }
static int jb_patch(uint32_t va, void *target, BYTE saved[5], int install) {
    BYTE *site = mapped + (va - 0x400000u);
    DWORD previous;
    if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &previous)) return fail("cockpit: stub protection");
    if (install) { memcpy(saved, site, 5); site[0] = 0xe9; put32(site + 1, (uint32_t)(uintptr_t)target - (uint32_t)(uintptr_t)(site + 5)); }
    else memcpy(site, saved, 5);
    if (!VirtualProtect(site, 5, previous, &previous)) return fail("cockpit: stub restore protection");
    FlushInstructionCache(GetCurrentProcess(), site, 5);
    return 0;
}
static int jb_patch_all(int install) {
    for (unsigned i = 0; i < JB_STUBS + jb_extra_count; ++i)
        if (jb_stub_at(i)->va && jb_patch(jb_stub_at(i)->va, jb_stub_at(i)->fn, jb_saved[i], install)) return 1;
    return 0;
}

/* ---- the own side */
static void jb_s_stub(es2_sim_service_call *call, void *user) {
    unsigned i = (unsigned)(uintptr_t)user;
    uint32_t a[6] = {0, 0, 0, 0, 0, 0};
    for (unsigned k = 0; k < jb_stub_at(i)->slots; ++k) a[k] = call->slots[k];
    call->eax = jb_act(jb_stub_at(i)->letter, a);
}
static void jb_refuse(const es2_sim_refusal *r, void *user) {
    (void)user;
    fprintf(stderr, "FAIL: own cockpit: refused call to 0x%08lx (%s): %s\n", (unsigned long)r->va, r->name ? r->name : "?",
            r->reason ? r->reason : "");
    jb_bad = 1;
}
/* Stand-ins at VAs that have a service row (on this branch or after a merge)
 * are swapped in after the seal; es2_sim_dispatch_find works only then. */
static int jb_is_replaced(uint32_t va) {
    for (unsigned i = 0; i < es2_sim_service_count(); ++i) {
        uint32_t row = 0;
        es2_sim_service_row(i, &row);
        if (row == va) return 1;
    }
    return 0;
}
static int jb_dispatch_setup(void) {
    es2_sim_dispatch_init(&jb_dispatch, jb_entries, 4096, jb_refuse, NULL);
    static es2_sim_services s;
    s.memory = &ir_memory;
    s.heap = &ir_heap;
    s.vt = &lc_vt;
    s.fs0 = NULL;
    if (es2_sim_register_services(&jb_dispatch, &s)) return fail("cockpit: dispatch");
    for (unsigned i = 0; i < JB_STUBS + jb_extra_count; ++i) {
        uint32_t va = jb_stub_at(i)->va ? jb_stub_at(i)->va : jb_fn(jb_stub_at(i)->fn);
        if (!jb_is_replaced(va) &&
            es2_sim_dispatch_add_service(&jb_dispatch, va, "cockpit stub", jb_s_stub, (void *)(uintptr_t)i, 0, (uint8_t)jb_stub_at(i)->slots))
            return fail("cockpit: dispatch (stub)");
    }
    const char *error = NULL;
    uint32_t bad = 0;
    if (es2_sim_dispatch_seal(&jb_dispatch, &error, &bad)) return fail("cockpit: dispatch seal");
    for (unsigned i = 0; i < JB_STUBS + jb_extra_count; ++i) {
        uint32_t va = jb_stub_at(i)->va ? jb_stub_at(i)->va : jb_fn(jb_stub_at(i)->fn);
        if (!jb_is_replaced(va)) continue;
        es2_sim_entry *e = (es2_sim_entry *)es2_sim_dispatch_find(&jb_dispatch, va);
        if (!e || e->kind != ES2_SIM_SERVICE) return fail("cockpit: service missing");
        e->service = jb_s_stub;
        e->service_user = (void *)(uintptr_t)i;
    }
    return 0;
}

/* ---- the case script */
typedef struct jb_preset { uint32_t va, value; unsigned width; } jb_preset;
static jb_preset jb_presets[256];
static unsigned jb_preset_count;
static void jb_set32(uint32_t va, uint32_t v) { jb_presets[jb_preset_count++] = (jb_preset){va, v, 4}; }
static void jb_set16(uint32_t va, uint32_t v) { jb_presets[jb_preset_count++] = (jb_preset){va, v, 2}; }
static void jb_set8(uint32_t va, uint32_t v) { jb_presets[jb_preset_count++] = (jb_preset){va, v, 1}; }
static uint32_t jb_preset_of(uint32_t va) {
    uint32_t v = 0;
    for (unsigned i = 0; i < jb_preset_count; ++i)
        if (jb_presets[i].va == va) v = jb_presets[i].value;
    return v;
}
typedef struct jb_op { uint32_t va, a; unsigned n, mask; uint32_t b; } jb_op;
static jb_op jb_ops[16];
static unsigned jb_op_count;
static uint32_t jb_res[2][16][3];
#define JB_EAX 1u
#define JB_AL 8u
static void jb_op_add(uint32_t va, unsigned mask, unsigned n, uint32_t a) {
    jb_op *o = &jb_ops[jb_op_count++];
    o->va = va; o->mask = mask; o->n = n; o->a = a; o->b = 0;
}
static uint32_t jb_rand(uint32_t n) { return random32() % n; }
static uint32_t jb_signed(uint32_t range) { return (uint32_t)((int32_t)jb_rand(2u * range + 1u) - (int32_t)range); }
static void jb_w32(uint32_t offset, uint32_t v) { put32(jb_world_start + offset, v); }
static void jb_w16(uint32_t offset, uint32_t v) { put16(jb_world_start + offset, (uint16_t)v); }
static void jb_w8(uint32_t offset, uint32_t v) { jb_world_start[offset] = (BYTE)v; }
static uint32_t jb_r32(uint32_t offset) { return get32(jb_world_start + offset); }

/* The branch keys of the first call of a case, predicted from the presets
 * (the coverage the summary prints; each must be reached). */
enum {
    JK_HIT4, JK_HIT_END, JK_HIT_NOEND, JK_HIT_FLASH, JK_HIT_NOFLASH, JK_BUMP4, JK_BUMP,
    JK_START4, JK_START_TRANS, JK_START_PAGED, JK_START_UNPAGED, JK_START_M0, JK_START_M1, JK_START_M23, JK_START_M5,
    JK_START_FRONT, JK_START_AWAY,
    JK_STEP_OFF, JK_STEP4, JK_STEP_UP_S, JK_STEP_UP_D, JK_STEP_DOWN_S, JK_STEP_DOWN_D, JK_STEP_ROWS, JK_STEP_NOROWS,
    JK_STEP_K0, JK_STEP_K2, JK_STEP_K2NULL, JK_STEP_KOTHER, JK_STEP_SLIDE,
    JK_END_M23, JK_END_OTHER, JK_END_AWAY, JK_COUNT
};
static const char *const jb_key_names[JK_COUNT] = {
    "hit:mode4", "hit:running", "hit:idle", "hit:flash-set", "hit:flash-new", "bump:mode4", "bump:start",
    "start:mode4", "start:transition", "start:paged", "start:unpaged", "start:mode0", "start:mode1", "start:mode2/3", "start:mode5+",
    "start:view-current", "start:view-switched",
    "step:off", "step:mode4", "step:up-step", "step:up-dist", "step:down-step", "step:down-dist", "step:rows", "step:no-rows",
    "step:kind0", "step:kind2", "step:kind2-null", "step:kind-other", "step:panel-slide",
    "end:mode2/3", "end:other", "end:view-switched",
};
static unsigned jb_keys[JK_COUNT];

static void jb_items(void) {
    /* 16 items: e = item + 4, kind at e, kind 0 moves e + 0x10, kind 2 the word [e + 4]. */
    for (unsigned i = 0; i < 16u; ++i) {
        uint32_t it = JB_ITEMS + 0x20u * i;
        random_bytes(jb_world_start + it, 0x20u);
        static const uint32_t kinds[6] = {0, 0, 2, 2, 1, 3};
        jb_w32(it + 4u, kinds[jb_rand(6u)]);
        jb_w32(it + 8u, jb_rand(4u) ? jb_at(JB_WORDS + 2u * jb_rand(16u)) : 0u);
    }
    for (unsigned m = 0; m < 5u; ++m) {
        uint32_t list = JB_LISTS + 0x204u * m, n = jb_rand(6u);
        jb_w32(list, jb_rand(16u) ? n : 0xffffffffu);
        for (unsigned i = 0; i < n; ++i) jb_w32(list + 4u + 4u * i, jb_at(JB_ITEMS + 0x20u * jb_rand(16u) + 4u));
    }
}
static void jb_begin(void) {
    memset(jb_world_start, 0, JB_WORLD_SIZE);
    jb_preset_count = 0;
    jb_clock_step = 0;
    jb_op_count = 0;
    /* The clock: a monotone script from a random base; stream B. */
    uint32_t now = 0x10000u + jb_rand(0x100000u);
    for (unsigned i = 0; i < 12u; ++i) { jb_clock[i] = now; now += jb_rand(4u) ? jb_rand(25u) : 40u + jb_rand(40u); }
    for (unsigned i = 0; i < 12u; ++i) jb_rng[i] = random32();
    jb_attr[0] = jb_rand(8u) ? jb_rand(400u) : random32();
    jb_attr[1] = jb_rand(8u) ? jb_rand(300u) : random32();
    jb_switch = jb_rand(5u) ? 0u : 1u + jb_rand(2u);
    /* The world. */
    random_bytes(jb_world_start + JB_GC, 0x240u);
    for (unsigned k = 0; k < 2u; ++k) {
        uint32_t v = k ? JB_V2 : JB_V;
        random_bytes(jb_world_start + v, 0x100u);
        jb_w32(v + 0x88u, jb_rand(4u));
        jb_w32(v + 0x8cu, jb_rand(4u));
        for (unsigned p = 0; p < 4u; ++p) { jb_w32(v + 0x48u + 8u * p, jb_rand(640u)); jb_w32(v + 0x4cu + 8u * p, jb_rand(480u)); }
    }
    static const uint32_t modes[10] = {0, 0, 1, 1, 2, 2, 3, 3, 4, 5};
    jb_w32(JB_D + 0xcu, jb_at(JB_LISTS));
    jb_w32(JB_D + 0x14u, modes[jb_rand(10u)]);
    jb_w8(JB_D + 0x1cu, jb_rand(8u) ? 0u : 1u + jb_rand(255u));
    jb_w32(JB_D + 0x1fu, jb_at(JB_P));
    jb_items();
    for (unsigned i = 0; i < 16u; ++i) jb_w16(JB_WORDS + 2u * i, random32());
    jb_w32(JB_ROWS, random32());
    jb_w32(JB_ROWS + 4u, random32());
    /* The panel, its widget, canvas and rectangles. */
    random_bytes(jb_world_start + JB_P, 0x260u);
    random_bytes(jb_world_start + JB_W, 0x240u);
    random_bytes(jb_world_start + JB_CANVAS, 0x230u);
    for (unsigned i = 0; i < 8u; ++i)
        for (unsigned k = 0; k < 4u; ++k) jb_w32(JB_RECTS + 0x10u * i + 4u * k, jb_signed(600u));
    jb_w32(JB_P, jb_at(JB_W));
    jb_w32(JB_P + 4u, jb_at(JB_CANVAS));
    for (unsigned k = 0; k < 3u; ++k) jb_w32(JB_P + 0x21au + 4u * k, jb_at(JB_RECTS + 0x10u * k));
    jb_w8(JB_P + 0x23eu, jb_rand(3u) ? 0u : 1u + jb_rand(255u));
    uint32_t children = jb_rand(5u);
    for (unsigned k = 0; k < 4u; ++k) jb_w32(JB_P + 0x246u + 4u * k, jb_at(JB_RECTS + 0x10u * (3u + jb_rand(5u))));
    jb_w32(JB_P + 0x25au, children);
    /* DATA. */
    jb_set32(0x004cf99cu, jb_at(JB_D));
    jb_set32(0x006c5f70u, jb_at(JB_GC));
    jb_set32(0x006c5ed0u, jb_at(JB_V));
    jb_set32(0x004a57e8u, jb_fn((void *)jb_o_origin));
    jb_set32(0x004a57f0u, jb_fn((void *)jb_o_page));
    jb_set32(0x004a368au, random32());
    jb_set32(0x004a5af8u, jb_rand(3u) ? jb_at(JB_ROWS) : 0u);
    jb_set16(0x004d2566u, jb_rand(2u) ? 0u : 1u + jb_rand(0xffffu));
    jb_set32(0x004d3018u, jb_signed(200u));
    jb_set32(0x004d301cu, jb_signed(200u));
    jb_set32(0x004cf9a0u, jb_signed(200u));
    jb_set32(0x004cf9a4u, jb_signed(200u));
    jb_set16(0x004d3ba2u, random32());
    static const uint16_t scales[8] = {0, 0, 1, 1, 2, 3, 0x21, 0xffff};
    jb_set16(0x004d253cu, scales[jb_rand(8u)]);
    for (uint32_t a = 0x004cfa58u; a < 0x004cfa70u; a += 4u) jb_set32(a, jb_signed(300u));
    jb_set16(0x004cfa70u, jb_signed(30u));
    jb_set32(0x004cfa94u, random32());
    for (uint32_t a = 0x004cfa7au; a < 0x004cfa94u; a += 2u) jb_set16(a, jb_rand(640u));
    jb_set8(0x0049ac44u, jb_rand(4u) ? 1u : (jb_rand(2u) ? 0u : 0x80u));
    uint32_t base = jb_clock[0];
    jb_set32(0x0049b0fcu, jb_rand(3u) ? 0u : base - 20u + jb_rand(100u));
    jb_set32(0x0049b100u, jb_rand(2u) ? 0u : base - 10u + jb_rand(20u));
    uint32_t bump_end = jb_rand(3u) ? 0u : base + 1u + jb_rand(60u);
    jb_set32(0x0049b638u, bump_end);
    jb_set32(0x0049b634u, bump_end ? bump_end - 0x3cu : random32());
    jb_set32(0x004cfcb8u, jb_rand(0x10000u));
    jb_set32(0x0049aef8u, jb_rand(3u) ? random32() | 1u : 0u);
    jb_set32(0x004cfd08u, random32());
    jb_set32(0x004cfd0cu, random32());
    jb_set8(0x0049aef3u, jb_rand(2u));
    jb_set8(0x0049add5u, jb_rand(2u));
    jb_set32(0x004cfcbcu, random32());
    jb_set32(0x004cfcc0u, random32());
}
static void jr_side_reset(void);
static void jb_side_begin(unsigned side) {
    lc_reset(side, 47, 0x01000000u, 1);
    lc_loop.dispatch = &jb_dispatch;
    es2_sim_hud_init(&jb_hud, &lc_loop);
    es2_sim_hud_bind(&jb_hud);
    memcpy(jb_world, jb_world_start, JB_WORLD_SIZE);
    for (unsigned i = 0; i < jb_preset_count; ++i) {
        BYTE *p = mapped + (jb_presets[i].va - 0x400000u);
        if (jb_presets[i].width == 4) put32(p, jb_presets[i].value);
        else if (jb_presets[i].width == 2) put16(p, (uint16_t)jb_presets[i].value);
        else *p = (BYTE)jb_presets[i].value;
    }
    jb_side = side;
    jb_hash[side] = 0xcbf29ce484222325ull;
    jb_count[side] = 0;
    jb_len[side] = 0;
    jb_text[side][0] = 0;
    jb_clock_i = 0;
    jb_rng_i = 0;
    jr_side_reset();
}
static unsigned jb_switch_start;
static void jb_show(const char *what, unsigned k) {
    for (unsigned i = 0; i < jb_op_count; ++i)
        fprintf(stderr, "  op %u: 0x%08lx(0x%08lx)\n", i, (unsigned long)jb_ops[i].va, (unsigned long)jb_ops[i].a);
    fprintf(stderr, "  world at 0x%08lx; %s case %u\n", (unsigned long)(uintptr_t)jb_world, what, k);
}
static int jb_run(const char *what, unsigned k) {
    jb_switch_start = jb_switch;
    uint32_t gc_record = jb_r32(JB_GC + 0x22cu), gc2_record = jb_r32(JB_GC2 + 0x22cu);
    for (unsigned side = 0; side < 2u; ++side) {
        jb_switch = jb_switch_start;
        jb_side_begin(side);
        for (unsigned i = 0; i < jb_op_count; ++i) {
            jb_op *o = &jb_ops[i];
            uint32_t in[3] = {0, 0, 0}, *out = jb_res[side][i], a[2] = {o->a, o->b};
            jb_note('#', i, 0, 0);
            if (!side) au_call((uint32_t)(uintptr_t)(mapped + (o->va - 0x400000u)), a, o->n, in, out);
            else {
                es2_sim_result r;
                memset(&r, 0, sizeof(r));
                if (es2_sim_call(&jb_dispatch, o->va, a, o->n, in, &r)) jb_bad = 1;
                out[0] = r.eax; out[1] = r.edx; out[2] = r.ecx;
            }
            if (jb_bad) {
                fprintf(stderr, "FAIL: cockpit %s case %u op %u (0x%08lx): a call failed\n", what, k, i, (unsigned long)o->va);
                jb_show(what, k);
                return 1;
            }
        }
        /* GC+0x22c: the dead stack record of 0x42d2f8 (another address on each side). */
        if (get32(jb_world + JB_GC + 0x22cu) != gc_record) put32(jb_world + JB_GC + 0x22cu, 0xdead022cu);
        if (get32(jb_world + JB_GC2 + 0x22cu) != gc2_record) put32(jb_world + JB_GC2 + 0x22cu, 0xdead022cu);
        if (!side) memcpy(jb_world_start + JB_WORLD_SIZE, jb_world, JB_WORLD_SIZE);
        su_take(side);
    }
    if (jb_hash[0] != jb_hash[1] || jb_count[0] != jb_count[1]) {
        fprintf(stderr, "  original (%u): %s\n  own      (%u): %s\n", jb_count[0], jb_text[0], jb_count[1], jb_text[1]);
        jb_show(what, k);
        fprintf(stderr, "FAIL: cockpit %s case %u: the stand-in calls differ\n", what, k);
        return 1;
    }
    if (su_compare(what, k)) { jb_show(what, k); return 1; }
    if (memcmp(jb_world, jb_world_start + JB_WORLD_SIZE, JB_WORLD_SIZE)) {
        for (unsigned i = 0, shown = 0; i < JB_WORLD_SIZE && shown < 16u; ++i)
            if (jb_world[i] != jb_world_start[JB_WORLD_SIZE + i]) {
                fprintf(stderr, "  world +0x%04x original %02x own %02x\n", i, jb_world_start[JB_WORLD_SIZE + i], jb_world[i]);
                ++shown;
            }
        jb_show(what, k);
        fprintf(stderr, "FAIL: cockpit %s case %u: the world differs\n", what, k);
        return 1;
    }
    for (unsigned i = 0; i < jb_op_count; ++i) {
        uint32_t x = jb_res[0][i][0], y = jb_res[1][i][0];
        if (jb_ops[i].mask & JB_AL) { x &= 0xffu; y &= 0xffu; }
        else if (!(jb_ops[i].mask & JB_EAX)) continue;
        if (x == y) continue;
        fprintf(stderr, "FAIL: cockpit %s case %u op %u (0x%08lx): EAX original 0x%08lx own 0x%08lx\n", what, k, i,
                (unsigned long)jb_ops[i].va, (unsigned long)jb_res[0][i][0], (unsigned long)jb_res[1][i][0]);
        jb_show(what, k);
        return 1;
    }
    return 0;
}

/* The keys of the first call (from the presets and the world). */
static void jb_tally(void) {
    if (!jb_op_count) return;
    uint32_t va = jb_ops[0].va, mode = jb_r32(JB_D + 0x14u);
    int paged = (jb_preset_of(0x004d2566u) & 0xffffu) != 0;
    if (va == 0x00434010u) {
        if (mode == 4u) { ++jb_keys[JK_HIT4]; return; }
        ++jb_keys[jb_preset_of(0x0049b0fcu) ? JK_HIT_END : JK_HIT_NOEND];
        ++jb_keys[jb_preset_of(0x0049b100u) ? JK_HIT_FLASH : JK_HIT_NOFLASH];
    } else if (va == 0x00434144u) {
        ++jb_keys[mode == 4u ? JK_BUMP4 : JK_BUMP];
    } else if (va == 0x0042d2f8u) {
        if (mode == 4u) { ++jb_keys[JK_START4]; return; }
        if (jb_world_start[JB_D + 0x1cu]) { ++jb_keys[JK_START_TRANS]; return; }
        ++jb_keys[paged ? JK_START_PAGED : JK_START_UNPAGED];
        ++jb_keys[mode == 0u ? JK_START_M0 : mode == 1u ? JK_START_M1 : mode <= 3u ? JK_START_M23 : JK_START_M5];
        if (mode == 2u || mode == 3u) ++jb_keys[jb_switch == 1u ? JK_START_AWAY : JK_START_FRONT];
    } else if (va == 0x0042d4a8u) {
        if (!(jb_preset_of(0x0049ac44u) & 0xffu)) { ++jb_keys[JK_STEP_OFF]; return; }
        if (mode == 4u) { ++jb_keys[JK_STEP4]; return; }
        uint16_t y = (uint16_t)jb_preset_of(0x004cfa64u);
        int16_t down = (int16_t)(uint16_t)(y - (uint16_t)jb_preset_of(0x004cfa68u));
        int16_t up = (int16_t)(uint16_t)((uint16_t)jb_preset_of(0x004cfa6cu) - y);
        int32_t s = (int32_t)(jb_ops[0].a & 0xffffu);
        if (down < up) ++jb_keys[up > s ? JK_STEP_UP_S : JK_STEP_UP_D];
        else ++jb_keys[down > s ? JK_STEP_DOWN_S : JK_STEP_DOWN_D];
        ++jb_keys[jb_preset_of(0x004a5af8u) ? JK_STEP_ROWS : JK_STEP_NOROWS];
        uint32_t list = JB_LISTS + 0x204u * mode, n = jb_r32(list);
        for (uint32_t i = 0; (int32_t)i < (int32_t)n; ++i) {
            uint32_t e = jb_r32(list + 4u + 4u * i) - (uint32_t)(uintptr_t)jb_world, kind = jb_r32(e);
            if (kind == 0u) ++jb_keys[JK_STEP_K0];
            else if (kind == 2u) ++jb_keys[jb_r32(e + 4u) ? JK_STEP_K2 : JK_STEP_K2NULL];
            else ++jb_keys[JK_STEP_KOTHER];
        }
        if (jb_world_start[JB_P + 0x23eu]) ++jb_keys[JK_STEP_SLIDE];
    } else if (va == 0x0042d624u) {
        if (!(jb_preset_of(0x0049ac44u) & 0xffu) || mode == 4u) return;
        if (mode == 2u || mode == 3u) ++jb_keys[jb_switch ? JK_END_AWAY : JK_END_M23];
        else ++jb_keys[JK_END_OTHER];
    }
}

static uint32_t jb_step_arg(void) { return jb_rand(8u) ? jb_rand(8u) : (jb_rand(2u) ? jb_rand(0x10000u) : random32()); }
static void jb_add_random(void) {
    switch (jb_rand(11u)) {
    case 0: jb_op_add(0x00434010u, JB_EAX, 1, 0); break;
    case 1: jb_op_add(0x00434144u, JB_EAX, 0, 0); break;
    case 2: jb_op_add(0x0042d2f8u, JB_EAX, 1, jb_rand(4u) ? jb_rand(40u) : random32()); break;
    case 3: jb_op_add(0x0042d4a8u, 0, 1, jb_step_arg()); break;
    case 4: jb_op_add(0x0042d624u, 0, 0, 0); break;
    case 5: jb_op_add(0x0042d820u, JB_EAX, 1, random32()); break;
    case 6: jb_op_add(0x0042d82cu, JB_EAX, 1, random32()); break;
    case 7: jb_op_add(0x0042d854u, JB_EAX, 0, 0); break;
    case 8: jb_op_add(0x0042d870u, JB_EAX, 0, 0); break;
    case 9: jb_op_add(0x0043408cu, JB_AL, 1, jb_at(JB_P)); break;
    default: jb_op_add(0x00434194u, JB_AL, 1, jb_at(JB_P)); break;
    }
}
/* One call on a random state. */
static int jb_single_case(unsigned k) {
    jb_begin();
    jb_add_random();
    jb_tally();
    return jb_run("single", k);
}
/* A hit and the frames after it (a jolt or a bump played out), then a
 * random call or two. */
static int jb_sequence_case(unsigned k) {
    jb_begin();
    unsigned bump = jb_rand(2u);
    jb_op_add(bump ? 0x00434144u : 0x00434010u, JB_EAX, bump ? 0u : 1u, 0);
    for (unsigned i = 0, n = 2u + jb_rand(7u); i < n; ++i) jb_op_add(bump ? 0x00434194u : 0x0043408cu, JB_AL, 1, jb_at(JB_P));
    for (unsigned i = 0, n = jb_rand(3u); i < n; ++i) jb_add_random();
    jb_tally();
    return jb_run("sequence", k);
}

/* ---- the intro set-up 0x45d548 */
static const struct { uint32_t va; void *fn; char letter; unsigned slots; } jb_intro_stubs[3] = {
    {0x0042eb40u, (void *)jb_o_intro_empty, 'G', 1}, {0x004303c4u, (void *)jb_o_intro_entries, 'E', 3},
    {0x004308fcu, (void *)jb_o_intro_fade, 'F', 3},
};
static BYTE jb_intro_saved[3][5];
static es2_sim_entry *jb_intro_entry[3];
static es2_sim_service_fn jb_intro_fn[3];
static void *jb_intro_user[3];
static void jb_s_intro(es2_sim_service_call *call, void *user) {
    unsigned i = (unsigned)(uintptr_t)user;
    uint32_t a[4] = {0, 0, 0, 0};
    for (unsigned k = 0; k < jb_intro_stubs[i].slots; ++k) a[k] = call->slots[k];
    call->eax = jb_act(jb_intro_stubs[i].letter, a);
}
static int jb_intro_swap(int on) {
    for (unsigned i = 0; i < 3u; ++i) {
        if (on) {
            es2_sim_entry *e = (es2_sim_entry *)es2_sim_dispatch_find(&jb_dispatch, jb_intro_stubs[i].va);
            if (!e || e->kind != ES2_SIM_SERVICE) return fail("cockpit: intro callee row missing");
            jb_intro_entry[i] = e;
            jb_intro_fn[i] = e->service;
            jb_intro_user[i] = e->service_user;
            e->service = jb_s_intro;
            e->service_user = (void *)(uintptr_t)i;
        } else {
            jb_intro_entry[i]->service = jb_intro_fn[i];
            jb_intro_entry[i]->service_user = jb_intro_user[i];
        }
        if (jb_patch(jb_intro_stubs[i].va, jb_intro_stubs[i].fn, jb_intro_saved[i], on)) return 1;
    }
    return 0;
}
static unsigned jb_intro_cov[2];
static int jb_intro_case(unsigned k) {
    jb_begin();
    random_bytes(jb_world_start + JB_PAL, 0x20u);
    random_bytes(jb_world_start + JB_PAL_ENTRIES, 0x400u);
    if (jb_rand(4u) == 0)   /* entries with the low bit clear and set in every byte */
        for (unsigned i = 0; i < 0x400u; ++i) jb_world_start[JB_PAL_ENTRIES + i] = (BYTE)(jb_rand(2u) ? 0xffu : 0xfeu);
    jb_w32(JB_PAL + 8u, jb_at(JB_PAL_ENTRIES));
    jb_set32(0x0049b020u, jb_at(JB_PAL));
    jb_set32(0x004d24eau, jb_rand(4u) ? random32() : 0u);
    jb_set16(0x0049aef6u, random32());
    jb_set8(0x0049aef4u, jb_rand(2u) ? 1u : random32());
    for (uint32_t a = 0x004d2744u; a < 0x004d2754u; a += 4u) jb_set32(a, random32());
    ++jb_intro_cov[jb_world_start[JB_PAL_ENTRIES + 0x80u] == 0xffu || jb_world_start[JB_PAL_ENTRIES + 0x80u] == 0xfeu];
    jb_op_add(0x0045d548u, JB_EAX, 0, 0);
    return jb_run("intro", k);
}

static int cockpit_cases(void) {
    current_function = "cockpit";
    es2_sim_identity_memory(&ir_memory);
    es2_sim_rtl_memory(&ir_memory, &su_rtl);
    su_data_pristine = malloc(SU_DATA_SIZE);
    su_snap[0] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    su_snap[1] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    ir_region = ww_alloc(IR_REGION_SIZE);
    lc_stack = VirtualAlloc(NULL, 0x10000u, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    lc_bits = ww_alloc(LC_BITS_SIZE);
    jb_world = VirtualAlloc(NULL, JB_WORLD_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    jb_world_start = malloc(2u * JB_WORLD_SIZE);
    if (!su_data_pristine || !su_snap[0] || !su_snap[1] || !ir_region || !lc_stack || !lc_bits || !jb_world || !jb_world_start)
        return fail("cockpit: memory");
    if (get32(mapped + (SU_COUNTER - 0x400000u))) return fail("cockpit: the counter word is not free");
    su_pristine_take();
    /* Operator new / new[] of the intro reach _calloc on the harness heap */
    if (!ir_patch_import("cw3220.dll", "_calloc", (void *)ir_calloc) || !ir_patch_import("cw3220.dll", "_free", (void *)ir_free))
        return fail("cockpit: import slot not found");
    if (jb_dispatch_setup() || jb_patch_all(1)) return 1;
    int bad = 0;
    static const struct { const char *name; int (*run)(unsigned); unsigned cases; } parts[] = {
        {"single", jb_single_case, 6000}, {"sequence", jb_sequence_case, 2000}, {"intro", jb_intro_case, 500},
    };
    for (unsigned p = 0; !bad && p < sizeof(parts) / sizeof(parts[0]); ++p) {
        if (parts[p].run == jb_intro_case && jb_intro_swap(1)) { bad = 1; break; }
        for (unsigned k = 0; !bad && k < parts[p].cases; ++k) {
            current_case = k; current_case_valid = 1;
            bad = parts[p].run(k);
        }
        if (parts[p].run == jb_intro_case && jb_intro_swap(0)) bad = 1;
        if (!bad) printf("PASS: cockpit %s: %u cases identical\n", parts[p].name, parts[p].cases);
    }
    if (!bad) {
        printf("COCKPIT coverage (first calls):");
        for (unsigned i = 0; i < JK_COUNT; ++i) {
            printf(" %s=%u", jb_key_names[i], jb_keys[i]);
            if (!jb_keys[i]) bad = fail("cockpit: a branch was not reached");
        }
        printf("\nCOCKPIT stand-in calls (original):");
        static const char letters[] = "TNWSRBAopGEFf";
        for (const char *l = letters; *l; ++l) printf(" %c=%u", *l, jb_letters[(unsigned char)*l]);
        printf("\nCOCKPIT intro (HD-03 rest): random entries %u, low-bit entries %u\n", jb_intro_cov[0], jb_intro_cov[1]);
        if (!jb_intro_cov[0] || !jb_intro_cov[1] || !jb_letters['G'] || jb_letters['E'] < 2u || !jb_letters['F'])
            bad = fail("cockpit: the intro part missed a case");
    }
    current_case_valid = 0;
    if (jb_patch_all(0)) bad = 1;
    put32(mapped + 0x2cbee4u, 0);
    memcpy(mapped + SU_DATA_RVA, su_data_pristine, SU_DATA_SIZE);
    return bad;
}

/* The jolt/drop actions declared above (jr_act, jr_side_reset) are defined by
 * the cockpit_rest cases. */
#include "cockpit_rest_cases.h"
