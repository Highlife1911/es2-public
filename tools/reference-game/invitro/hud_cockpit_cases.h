/* (--hud-cockpit; target :differential_hud_cockpit_test): the cockpit layout
 * and panel readers of the load and the display object's per-frame readers,
 * original machine code against sim/src/hud_cockpit.c and sim/src/hud_load.c
 * through the full service dispatch. Built on cockpit_cases.h's world
 * (jb_world), case script (jb_begin, presets, ops, jb_run) and patching
 * (jb_patch), with stand-ins of its own (prefix hc_): the base stand-ins of
 * cockpit_cases.h are not installed here, because the own construction calls
 * the palette swap 0x4303b0 in C.
 *
 * Part display: the display constructor 0x429660 (row display_ctor) on a
 * random world and a scripted view file: run as code on both sides the
 * views 0x429834, the region reader 0x42dcf0, the span lists 0x48bab4, the
 * view shape 0x429c2c, the panel object 0x431008 with its texts 0x431098,
 * the message tables 0x437598, the language path 0x45ef1c, the shape loads
 * 0x430f58/0x430fb0, the palette pieces 0x4303c4/0x4303b0/0x43048c, the
 * region area 0x488045, operator new/new[] (the original's through _calloc
 * on the harness heap) and the RTL thunks (_itoa, _strncpy, _strcat,
 * _memset); on the original's side the frame records of 0x49630c.
 * Stand-ins: the path 0x492bec ('d', logs both strings), 0x492b90 ('+',
 * hands a world string), the VOL stream 0x472f14 ('w', a stream with the
 * read slot +0x18 'r'), stream_open 0x4730e8 ('o'), stream_read 0x4735b8
 * ('g'; 'r' and 'g' read one scripted byte stream in call order),
 * stream_destroy 0x472f64 ('k'), dts_load 0x47a144 ('l', a numbered tag),
 * the shape by name 0x474cb0 ('b', world shapes with colour tables and a
 * slot +0x18 'x'), the RTTI count 0x496406 ('n'), the strings 0x453878 ('s')
 * and 0x4539a0 ('t'), the palette set 0x481fe0 ('P') and upload 0x465cc4
 * ('Q') and the panel constructor 0x431bf8 ('C', accepted by --cockpit-rest).
 *
 * Part fonts: 0x4544c0 (row hud_setup) with the font loads 0x453630/
 * 0x453684 run as code; stand-ins the path, the stream pieces, dts_load,
 * the surface attribute 0x481350 ('A') and the load 0x45cdf4 ('L').
 *
 * Part shape: the view shape 0x429c2c (row view_shape) alone, i = 0..5.
 *
 * Part frame: 1 to 4 calls of 0x42a3f4, 0x429828, 0x42db10, 0x42db18,
 * 0x432b14 (with 0x433b20, 0x45b750, 0x429820 and 0x42a3f4 run as code on
 * the original's side) and 0x433c54; stand-ins the sub-object's slot +0xc
 * ('v') and the cursor draws 0x48992e ('<') and 0x486e70 ('>').
 *
 * Compared after each case: the stand-in logs, DATA and the heap, the
 * world, EAX (AL and the upper bits for 0x432b14). Not reached: the
 * refusals (the fatal report on a stream error, operator new returning
 * NULL, a row-table region area, more than 12 spans in a row). */
#pragma once
/* The headers this suite builds on (sim_main.c). */
#include "init_records_cases.h"
#include "startup_cases.h"
#include "loop_cases.h"
#include "cockpit_cases.h"

/* The world (offsets in jb_world past jb_begin's use of the same areas). */
#define HC_CT 0x0600u        /* 3 colour tables of 0x200 */
#define HC_SH 0x0c00u        /* 3 shapes of 0x20: [0] the method table, [8] the colour table */
#define HC_SVT 0x0c60u       /* the stream's method table (+0x18 read) */
#define HC_XVT 0x0c80u       /* the shapes' method table (+0x18 destroy) */
#define HC_PAL2 0x0ca0u      /* the palette [0x49aef8] (+8 its entries at HC_PAL2E) */
#define HC_STR 0x0d00u       /* 16 strings of 16 bytes */
#define HC_VSH 0x0e00u       /* the shape part's [D+8] (8 dwords) */
#define HC_VIEWS 0x0e40u     /* the frame part's 6 view records of 0x20 */
#define HC_CUR 0x0f00u       /* 7 cursor images of 0x10 */
#define HC_CELL 0x0f80u      /* the frame part's image cell */
#define HC_NAMES 0x0fa0u     /* 4 chassis name pointers */
#define HC_PAL2E 0x3200u     /* 0x110 bytes of entries */
static uint32_t hc_str(unsigned i) { return jb_at(HC_STR + 16u * i); }

static BYTE hc_file[0x2000];
static unsigned hc_file_n, hc_at[2], hc_tags[2], hc_paths[2], hc_shapes[2], hc_kills[2];
static uint32_t hc_rtti, hc_attr[2], hc_load, hc_slot, hc_draw;
static uint32_t hc_hash(uint32_t p) {
    uint32_t h = 2166136261u;
    if (!p) return 0;
    for (unsigned i = 0; i < 32u; ++i) {
        BYTE b = *(const BYTE *)(uintptr_t)(p + i);
        h ^= b; h *= 16777619u;
        if (!b) break;
    }
    return h;
}
static void hc_read(uint32_t n, uint32_t buf) {
    for (uint32_t i = 0; i < n; ++i) {
        unsigned at = hc_at[jb_side]++;
        *(BYTE *)(uintptr_t)(buf + i) = at < hc_file_n ? hc_file[at] : 0;
    }
}
static uint32_t hc_act(char l, const uint32_t *a) {
    unsigned s = jb_side;
    switch (l) {
    case 'd': jb_note('d', hc_hash(a[0]), hc_hash(a[1]), 0); return 0x7a700000u + hc_paths[s]++;
    case '+': jb_note('+', hc_hash(a[0]), hc_hash(a[1]), 0); return hc_str(15);
    case 'w': case 'o':
        jb_note(l, a[1], 0, 0);
        jb_put(a[0], jb_at(HC_SVT));
        jb_put(a[0] + 4u, 0);
        return 0;
    case 'r': case 'g': jb_note(l, a[1], 0, 0); hc_read(a[1], a[2]); return a[1];
    case 'k': jb_note('k', a[1], 0, 0); return 0x6b000000u + hc_kills[s]++;
    case 'l': jb_note('l', a[1], 0, 0); return 0x5a000000u + hc_tags[s]++;
    case 'b': {
        unsigned i = hc_shapes[s]++;
        jb_note('b', a[0], i, 0);
        return jb_at(HC_SH + 0x20u * (1u + (i & 1u)));
    }
    case 'n': jb_note('n', jb_norm(a[0]), a[1], 0); return hc_rtti;
    case 'x': jb_note('x', a[0], a[1], 0); return 0x78000000u ^ a[0];
    case 's': jb_note('s', a[0], a[1], 0); return 0;
    case 't': jb_note('t', a[0] ^ a[1], a[2], a[3]); return 0;
    case 'P': case 'Q': jb_note(l, a[0], 0, 0); return 0;
    case 'C': jb_note('C', jb_norm(a[0]), hc_hash(a[1]), 0); return 0x43000000u;
    case 'A': jb_note('A', a[0], a[1], 0); return hc_attr[a[1] ? 1 : 0];
    case 'L': jb_note('L', a[0], 0, 0); return hc_load;
    case 'v': jb_note('v', a[0], a[1], 0); return hc_slot;
    case '<': case '>': jb_note(l, jb_norm(a[0]), jb_norm(a[1]), 0); return hc_draw ^ (uint32_t)l;
    default: jb_note('?', 0, 0, 0); return 0;
    }
}
#define HC_O(name, letter, n)                                                                                   \
    static uint32_t __cdecl hc_o_##name(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3) {                     \
        uint32_t a[4] = {a0, a1, a2, a3};                                                                       \
        for (unsigned i = (n); i < 4u; ++i) a[i] = 0;                                                           \
        return hc_act(letter, a);                                                                               \
    }
HC_O(path, 'd', 2) HC_O(path_ext, '+', 2) HC_O(vol, 'w', 2) HC_O(open, 'o', 2) HC_O(sread, 'r', 3) HC_O(read, 'g', 3)
HC_O(destroy, 'k', 2) HC_O(dts, 'l', 2) HC_O(by_name, 'b', 1) HC_O(rtti, 'n', 2) HC_O(shape_slot, 'x', 2)
HC_O(strings, 's', 2) HC_O(group, 't', 4) HC_O(pal_set, 'P', 1) HC_O(pal_upload, 'Q', 1) HC_O(panel, 'C', 2)
HC_O(attr, 'A', 2) HC_O(load, 'L', 1) HC_O(slot, 'v', 2) HC_O(paged, '<', 2) HC_O(unpaged, '>', 2)
static const jb_stub_t hc_stubs[] = {
    {0x00492becu, (void *)hc_o_path, 'd', 2}, {0x00492b90u, (void *)hc_o_path_ext, '+', 2},
    {0x00472f14u, (void *)hc_o_vol, 'w', 2}, {0x004730e8u, (void *)hc_o_open, 'o', 2},
    {0x004735b8u, (void *)hc_o_read, 'g', 3}, {0x00472f64u, (void *)hc_o_destroy, 'k', 2},
    {0x0047a144u, (void *)hc_o_dts, 'l', 2}, {0x00474cb0u, (void *)hc_o_by_name, 'b', 1},
    {0x00496406u, (void *)hc_o_rtti, 'n', 2}, {0x00453878u, (void *)hc_o_strings, 's', 2},
    {0x004539a0u, (void *)hc_o_group, 't', 4}, {0x00481fe0u, (void *)hc_o_pal_set, 'P', 1},
    {0x00465cc4u, (void *)hc_o_pal_upload, 'Q', 1}, {0x00431bf8u, (void *)hc_o_panel, 'C', 2},
    {0x00481350u, (void *)hc_o_attr, 'A', 2}, {0x0045cdf4u, (void *)hc_o_load, 'L', 1},
    {0x0048992eu, (void *)hc_o_paged, '<', 2}, {0x00486e70u, (void *)hc_o_unpaged, '>', 2},
    /* method-table slots: no VA of the original */
    {0, (void *)hc_o_sread, 'r', 3}, {0, (void *)hc_o_shape_slot, 'x', 2}, {0, (void *)hc_o_slot, 'v', 2},
};
#define HC_STUBS (sizeof(hc_stubs) / sizeof(hc_stubs[0]))
static BYTE hc_saved[HC_STUBS][5];
static int hc_patch_all(int install) {
    for (unsigned i = 0; i < HC_STUBS; ++i)
        if (hc_stubs[i].va && jb_patch(hc_stubs[i].va, hc_stubs[i].fn, hc_saved[i], install)) return 1;
    return 0;
}
static void hc_s_stub(es2_sim_service_call *call, void *user) {
    unsigned i = (unsigned)(uintptr_t)user;
    uint32_t a[4] = {0, 0, 0, 0};
    for (unsigned k = 0; k < hc_stubs[i].slots; ++k) a[k] = call->slots[k];
    call->eax = hc_act(hc_stubs[i].letter, a);
}
static int hc_dispatch_setup(void) {
    es2_sim_dispatch_init(&jb_dispatch, jb_entries, 4096, jb_refuse, NULL);
    static es2_sim_services s;
    s.memory = &ir_memory;
    s.heap = &ir_heap;
    s.vt = &lc_vt;
    s.fs0 = NULL;
    if (es2_sim_register_services(&jb_dispatch, &s)) return fail("hud-cockpit: dispatch");
    for (unsigned i = 0; i < HC_STUBS; ++i) {
        uint32_t va = hc_stubs[i].va ? hc_stubs[i].va : jb_fn(hc_stubs[i].fn);
        if (!jb_is_replaced(va) &&
            es2_sim_dispatch_add_service(&jb_dispatch, va, "hud-cockpit stub", hc_s_stub, (void *)(uintptr_t)i, 0, (uint8_t)hc_stubs[i].slots))
            return fail("hud-cockpit: dispatch (stub)");
    }
    const char *error = NULL;
    uint32_t bad = 0;
    if (es2_sim_dispatch_seal(&jb_dispatch, &error, &bad)) return fail("hud-cockpit: dispatch seal");
    for (unsigned i = 0; i < HC_STUBS; ++i) {
        uint32_t va = hc_stubs[i].va ? hc_stubs[i].va : jb_fn(hc_stubs[i].fn);
        if (!jb_is_replaced(va)) continue;
        es2_sim_entry *e = (es2_sim_entry *)es2_sim_dispatch_find(&jb_dispatch, va);
        if (!e || e->kind != ES2_SIM_SERVICE) return fail("hud-cockpit: service missing");
        e->service = hc_s_stub;
        e->service_user = (void *)(uintptr_t)i;
    }
    return 0;
}

/* The RTL imports the load reaches (the original's side; the own side has the rows). */
static uint32_t __cdecl hc_strncpy(uint32_t d, uint32_t s, uint32_t n) { uint32_t a[3] = {d, s, n}; return su_rtl_call(ES2_RTL_STRNCPY, a); }
static uint32_t __cdecl hc_memset(uint32_t d, uint32_t v, uint32_t n) { uint32_t a[3] = {d, v, n}; return su_rtl_call(ES2_RTL_MEMSET, a); }
static uint32_t __cdecl hc_itoa(uint32_t v, uint32_t s, uint32_t r) { uint32_t a[3] = {v, s, r}; return su_rtl_call(ES2_RTL_ITOA, a); }
/* ---- coverage: keys set while a case is built */
enum {
    HK_RES3, HK_RES_OTHER, HK_SHAPE_PRESET, HK_SHAPE_LOAD, HK_NAME_OVERRIDE, HK_NAME_TABLE, HK_VIEWS0, HK_VIEWS4,
    HK_SHAPE_SKIP, HK_SECOND, HK_RECTS, HK_ROWS, HK_ROW_SKIP, HK_X_SHIFT, HK_TEXTS18, HK_NO_TEXTS18, HK_TEXTS7,
    HK_NO_TEXTS7, HK_PANEL_SET, HK_PANEL_KEPT, HK_PAL_SET, HK_PAL_UPLOAD, HK_AREA_RECT, HK_AREA_OTHER, HK_SCALE_INIT,
    HK_SCALE_KEPT,
    HK_FONT_HIRES, HK_FONT_LOWRES, HK_ALERT_LOAD, HK_ALERT_KEPT,
    HK_VS_EARLY4, HK_VS_EARLY_SET, HK_VS_LOAD, HK_VS_SECOND, HK_VS_DIGIT2,
    HK_REQ_PENDING, HK_REQ_HIGH, HK_REQ_STORE, HK_REQ_NONE, HK_VIS_VIEW, HK_VIS_TRANS, HK_VIS_NO,
    HK_IN_ZONES, HK_IN_OFF, HK_IN_TAPE, HK_IN_HIT, HK_IN_MISS, HK_IN_REQUESTS, HK_IN_FIRST, HK_IN_MODE1, HK_IN_HELD,
    HK_IN_RELEASE, HK_CUR_OFF, HK_CUR_NEW, HK_CUR_SAME, HK_CUR_BACK, HK_CUR_NONE, HK_CUR_PAGED, HK_CUR_UNPAGED,
    HK_CUR_NULL4, HK_COUNT
};
static const char *const hc_key_names[HK_COUNT] = {
    "display:res3", "display:res-other", "display:shape-preset", "display:shape-load", "display:name-override",
    "display:name-table", "display:views0", "display:views4", "display:shape-skip", "display:second-load",
    "display:rects", "display:row-tables", "display:row-skip", "display:x-shift", "display:texts18",
    "display:no-texts18", "display:texts7", "display:no-texts7", "display:panel-set", "display:panel-kept",
    "display:palette-set", "display:palette-upload", "display:area-rect", "display:area-other",
    "display:scale-init", "display:scale-kept",
    "fonts:hires", "fonts:lowres", "fonts:alert-load", "fonts:alert-kept",
    "shape:early4", "shape:early-set", "shape:load", "shape:second", "shape:digit2",
    "request:pending", "request:high", "request:store", "request:none", "visible:view", "visible:transition",
    "visible:no", "input:zones", "input:zones-off", "input:tape", "input:zone-hit", "input:zone-miss",
    "input:requests", "input:toggle-first", "input:toggle-mode1", "input:toggle-held", "input:release",
    "cursor:off", "cursor:new", "cursor:same", "cursor:back", "cursor:none", "cursor:paged", "cursor:unpaged",
    "cursor:null-image",
};
static unsigned hc_keys[HK_COUNT];
static void hc_key(unsigned k) { ++hc_keys[k]; }

/* The common state of every part. */
static void hc_begin(void) {
    jb_begin();
    memset(jb_world_start + HC_CT, 0, 0x1100u - HC_CT);
    random_bytes(jb_world_start + HC_CT, 0x600u);
    for (unsigned i = 0; i < 3u; ++i) {
        uint32_t sh = HC_SH + 0x20u * i;
        random_bytes(jb_world_start + sh, 0x20u);
        jb_w32(sh, jb_at(HC_XVT));
        jb_w32(sh + 8u, jb_at(HC_CT + 0x200u * i));
    }
    jb_w32(HC_SVT + 0x18u, jb_fn((void *)hc_o_sread));
    jb_w32(HC_XVT + 0x18u, jb_fn((void *)hc_o_shape_slot));
    /* two-letter extensions (the digit goes after them), names, the language file */
    static const char *const strings[16] = {"lo", "hi", "vl", "vh", "cx", "dx", "pa", "ck", "MECH0", "MECH1", "MECH2",
                                            "MECH3", "OVRIDE", "", "", "pnl.str"};
    for (unsigned i = 0; i < 16u; ++i) memcpy(jb_world_start + HC_STR + 16u * i, strings[i], strlen(strings[i]));
    for (unsigned i = 0; i < 4u; ++i) jb_w32(HC_NAMES + 4u * i, hc_str(8u + i));
    memset(hc_at, 0, sizeof(hc_at));
    memset(hc_tags, 0, sizeof(hc_tags));
    memset(hc_paths, 0, sizeof(hc_paths));
    memset(hc_shapes, 0, sizeof(hc_shapes));
    memset(hc_kills, 0, sizeof(hc_kills));
    hc_file_n = 0;
    hc_rtti = jb_signed(4u);
    hc_attr[0] = random32();
    hc_attr[1] = random32();
    hc_load = random32();
    hc_slot = random32();
    hc_draw = random32();
    jb_set32(0x004a0a24u, hc_str(0));
    jb_set32(0x004a0a0cu, hc_str(1));
    jb_set32(0x004a0a28u, hc_str(2));
    jb_set32(0x004a0a10u, hc_str(3));
    jb_set32(0x004a0a14u, hc_str(4));
    jb_set32(0x004a09fcu, hc_str(5));
}
static void hc_put8(unsigned v) { if (hc_file_n < sizeof(hc_file)) hc_file[hc_file_n++] = (BYTE)v; }
static void hc_put16(unsigned v) { hc_put8(v & 0xffu); hc_put8(v >> 8 & 0xffu); }
static void hc_put32(uint32_t v) { hc_put16(v & 0xffffu); hc_put16(v >> 16); }

/* 0x429660(D, P) on a random world and view file. */
static int hc_display_case(unsigned k) {
    hc_begin();
    unsigned sx = jb_rand(4u) ? jb_rand(2u) : 2u, sy = jb_rand(4u) ? jb_rand(2u) : 2u, res3 = !jb_rand(3u);
    jb_set16(0x004d253au, sx);
    jb_set16(0x004d253cu, sy);
    jb_set8(0x004d2537u, res3 ? 3u : jb_rand(3u));
    hc_key(res3 ? HK_RES3 : HK_RES_OTHER);
    unsigned first = jb_rand(2u);
    jb_set8(0x0049aca4u, first ? 0u : 1u + jb_rand(3u));
    if (!first) { jb_set32(0x0049ac9cu, jb_rand(2u)); jb_set32(0x0049aca0u, jb_rand(2u)); }
    if (res3) hc_key(first ? HK_SCALE_INIT : HK_SCALE_KEPT);
    unsigned preset = jb_rand(2u);
    jb_set32(0x0049ac48u, preset ? jb_at(HC_SH) : 0u);
    hc_key(preset ? HK_SHAPE_PRESET : HK_SHAPE_LOAD);
    /* the vehicle P and its record */
    uint32_t P = JB_VEHICLE, rec = JB_VREC;
    random_bytes(jb_world_start + P, 0x210u);
    random_bytes(jb_world_start + rec, 0x70u);
    jb_w32(P + 0x1f2u, jb_at(rec));
    jb_w16(rec + 0x6cu, jb_rand(4u));
    jb_w16(rec + 0x52u, jb_rand(4u));
    jb_set32(0x004a9d60u, jb_at(HC_NAMES));
    unsigned override = !jb_rand(4u);
    jb_set32(0x0049ac50u, override ? hc_str(12) : 0u);
    jb_set16(0x0049ac4cu, random32());
    hc_key(override ? HK_NAME_OVERRIDE : HK_NAME_TABLE);
    random_bytes(jb_world_start + JB_D, 0x40u);
    /* the view file */
    unsigned shape_skip = !jb_rand(4u), views = jb_rand(5u);
    if (views == 4u && !jb_rand(2u)) shape_skip = 1;   /* view 3 writes [D+8][4], past new[](0x10); kept rare */
    jb_set16(0x004d2680u, shape_skip ? 1u + jb_rand(0xffffu) : 0u);
    if (shape_skip) hc_key(HK_SHAPE_SKIP);
    if (!views) hc_key(HK_VIEWS0);
    if (views == 4u) hc_key(HK_VIEWS4);
    unsigned second = !jb_rand(3u);
    jb_set8(0x004d2538u, second ? 1u : jb_rand(2u) * 2u);
    if (second && views && !shape_skip) hc_key(HK_SECOND);
    hc_put32(views);
    for (unsigned i = 0; i < 8u * views; ++i) hc_put32(jb_rand(4u) ? jb_rand(320u) : random32());
    unsigned ys = res3 ? 0u : sy, xs = res3 ? 0u : sx;
    for (unsigned v = 0; v < views; ++v) {
        unsigned n1 = jb_rand(3u), n2 = jb_rand(3u - (n1 > 1u));
        hc_put16(n1);
        for (unsigned i = 0; i < n1; ++i) {
            unsigned y0 = jb_rand(200u), x0 = jb_rand(300u);
            hc_put16(y0); hc_put16(y0 + jb_rand(40u)); hc_put16(x0); hc_put16(x0 + jb_rand(40u));
            hc_key(HK_RECTS);
        }
        hc_put16(n2);
        for (unsigned i = 0; i < n2; ++i) {
            unsigned rows = 1u + jb_rand(24u);
            hc_put16(jb_rand(200u)); hc_put16(rows);
            for (unsigned r = 0; r < rows; ++r) {
                unsigned x0 = jb_rand(300u);
                hc_put16(x0); hc_put16(jb_rand(6u) ? x0 + jb_rand(30u) : x0 - jb_rand(4u));
            }
            hc_key(HK_ROWS);
            if (ys) hc_key(HK_ROW_SKIP);
            if (xs) hc_key(HK_X_SHIFT);
        }
    }
    /* the panel object's texts and shapes */
    unsigned t18 = !jb_rand(3u), t7 = !jb_rand(3u);
    jb_set32(0x0049b0acu, t18 ? 0u : random32() | 1u);
    jb_set32(0x0049b08cu, t7 ? 0u : random32() | 1u);
    hc_key(t18 ? HK_TEXTS18 : HK_NO_TEXTS18);
    hc_key(t7 ? HK_TEXTS7 : HK_NO_TEXTS7);
    jb_set32(0x004a9af8u, random32());
    jb_set8(0x004d2536u, 'A' + jb_rand(4u));
    unsigned panel_set = jb_rand(2u);
    jb_set32(0x0049b088u, panel_set ? 0u : jb_at(JB_P));
    hc_key(panel_set ? HK_PANEL_SET : HK_PANEL_KEPT);
    /* the palettes */
    random_bytes(jb_world_start + JB_PAL, 0x20u);
    random_bytes(jb_world_start + JB_PAL_ENTRIES, 0x400u);
    jb_w32(JB_PAL + 8u, jb_at(JB_PAL_ENTRIES));
    random_bytes(jb_world_start + HC_PAL2, 0x20u);
    random_bytes(jb_world_start + HC_PAL2E, 0x110u);
    jb_w32(HC_PAL2 + 8u, jb_at(HC_PAL2E));
    jb_set32(0x0049b020u, jb_at(JB_PAL));
    jb_set32(0x0049aef8u, jb_at(HC_PAL2));
    unsigned pal_set = jb_rand(2u);
    jb_set32(0x0049b024u, pal_set ? jb_at(HC_PAL2) : jb_at(JB_PAL));
    hc_key(pal_set ? HK_PAL_SET : HK_PAL_UPLOAD);
    unsigned dirty = jb_rand(2u);
    jb_set8(0x0049b030u, dirty);
    if (dirty) { jb_set32(0x0049b028u, jb_rand(0x60u)); jb_set32(0x0049b02cu, jb_rand(0x60u)); }
    /* the area 0x4cfa7e (type 0: a rectangle; another type: the type) */
    unsigned rect = jb_rand(3u) != 0u;
    jb_set32(0x004cfa7eu, rect ? 0u : 1u + jb_rand(0x300u));
    if (rect) {
        uint32_t x0 = jb_rand(100u), y0 = jb_rand(100u);
        jb_set32(0x004cfa82u, x0); jb_set32(0x004cfa86u, y0);
        jb_set32(0x004cfa8au, x0 + jb_rand(64u)); jb_set32(0x004cfa8eu, y0 + jb_rand(64u));
    }
    hc_key(rect ? HK_AREA_RECT : HK_AREA_OTHER);
    for (uint32_t a = 0x0049ac54u; a <= 0x0049ac60u; a += 4u) jb_set32(a, jb_signed(300u));
    for (uint32_t a = 0x004cfa40u; a < 0x004cfa50u; a += 4u) jb_set32(a, random32());
    jb_op_add(0x00429660u, JB_EAX, 2, jb_at(JB_D));
    jb_ops[jb_op_count - 1u].b = jb_at(P);
    return jb_run("display", k);
}

/* 0x4544c0() (the fonts and the alert sound). */
static int hc_fonts_case(unsigned k) {
    hc_begin();
    unsigned hires = jb_rand(2u);
    jb_set8(0x004d2537u, hires ? 3u : jb_rand(3u));
    hc_key(hires ? HK_FONT_HIRES : HK_FONT_LOWRES);
    jb_set16(0x004d253au, jb_rand(4u) ? jb_rand(3u) : random32());
    jb_set16(0x004d253cu, jb_rand(4u) ? jb_rand(3u) : random32());
    unsigned alert = jb_rand(2u);
    jb_set32(0x0049dd88u, alert ? 0u : random32() | 1u);
    hc_key(alert ? HK_ALERT_LOAD : HK_ALERT_KEPT);
    jb_set8(0x004d256cu, jb_rand(2u));
    jb_op_add(0x004544c0u, JB_EAX, 0, 0);
    return jb_run("fonts", k);
}

/* 0x429c2c(D, i). */
static int hc_shape_case(unsigned k) {
    hc_begin();
    random_bytes(jb_world_start + JB_D, 0x40u);
    jb_w32(JB_D + 8u, jb_at(HC_VSH));
    memcpy(jb_world_start + JB_D + 0x2du, "MECH2", 6);
    uint32_t i = jb_rand(6u);
    for (unsigned j = 0; j < 8u; ++j) jb_w32(HC_VSH + 4u * j, jb_rand(3u) ? 0u : random32());
    unsigned set = jb_world_start[HC_VSH + 4u * i] | jb_world_start[HC_VSH + 4u * i + 1u] | jb_world_start[HC_VSH + 4u * i + 2u] |
                   jb_world_start[HC_VSH + 4u * i + 3u];
    jb_set8(0x004d2537u, jb_rand(2u) ? 3u : 0u);
    unsigned second = jb_rand(2u);
    jb_set8(0x004d2538u, second ? 1u : 0u);
    if (i == 4u) hc_key(HK_VS_EARLY4);
    else if (set) hc_key(HK_VS_EARLY_SET);
    else {
        hc_key(HK_VS_LOAD);
        if (second && i == 0u) hc_key(HK_VS_SECOND);
        if ((i < 3u ? i : i - 1u) == 2u) hc_key(HK_VS_DIGIT2);
    }
    jb_op_add(0x00429c2cu, JB_EAX, 2, jb_at(JB_D));
    jb_ops[jb_op_count - 1u].b = i;
    return jb_run("shape", k);
}

/* ---- part frame */
static void hc_frame_world(void) {
    /* D: mode, pending request, view records, transition byte, panel */
    jb_w32(JB_D + 4u, jb_at(HC_VIEWS));
    for (unsigned i = 0; i < 6u; ++i) jb_w32(HC_VIEWS + 0x20u * i + 0xcu, jb_rand(3u) ? jb_signed(3u) : random32());
    jb_w32(JB_D + 0x14u, jb_rand(6u));
    jb_w32(JB_D + 0x18u, jb_rand(3u) ? 0xffffffffu : jb_rand(8u));
    /* H = JB_P: canvas origins, the three zone rectangles, the cursor fields */
    uint32_t cv = JB_CANVAS;
    for (unsigned i = 0; i < 2u; ++i) { jb_w32(cv + 0x210u + 4u * i, jb_rand(64u)); jb_w32(cv + 0x220u + 4u * i, jb_rand(64u)); }
    for (unsigned r = 0; r < 3u; ++r) {
        uint32_t x0 = jb_rand(200u), y0 = jb_rand(200u), at = JB_RECTS + 0x10u * r;
        jb_w32(at, x0); jb_w32(at + 4u, y0);
        jb_w32(at + 8u, jb_rand(6u) ? x0 + jb_rand(100u) : x0 - 1u); jb_w32(at + 0xcu, y0 + jb_rand(100u));
    }
    jb_w32(JB_P + 0x1f9u, jb_at(JB_HUD));
    jb_w32(JB_HUD, jb_at(JB_HUD_VT));
    jb_w32(JB_HUD_VT + 0xcu, jb_fn((void *)hc_o_slot));
    jb_w32(JB_P + 0x22eu, jb_rand(3u) ? 0u : 1u + jb_rand(6u));
    uint32_t self = jb_at(JB_P + 0x232u);
    jb_w32(JB_P + 0x226u, jb_rand(2u) ? self : jb_at(HC_CELL));
    jb_w32(JB_P + 0x22au, jb_at(HC_CELL));
    for (unsigned i = 0; i < 7u; ++i) {
        uint32_t c = HC_CUR + 0x10u * i;
        random_bytes(jb_world_start + c, 0x10u);
        if (!jb_rand(4u)) jb_w32(c + 4u, 0);
        jb_set32(0x0049b08cu + 4u * i, jb_at(c));
    }
    jb_w32(HC_CELL, jb_at(HC_CUR + 0x10u * jb_rand(7u)));
    /* the input record and the point */
    uint32_t rec = JB_ROWS;
    random_bytes(jb_world_start + rec, 0x24u);
    for (unsigned i = 0; i < 5u; ++i) jb_w8(rec + (i < 4u ? 0x1eu + i : 0xbu), jb_rand(3u) ? 0u : 1u + jb_rand(255u));
    jb_w32(rec + 2u, jb_rand(300u));
    jb_w32(rec + 6u, jb_rand(300u));
    jb_set8(0x0049b105u, jb_rand(4u) ? 1u + jb_rand(255u) : 0u);
    jb_set16(0x004d24d6u, jb_rand(4u) ? 0u : 1u + jb_rand(0xffffu));
    jb_set32(0x004d2306u, jb_rand(300u));
    jb_set32(0x004d230au, jb_rand(300u));
    jb_set8(0x0049b550u, jb_rand(2u));
}
static uint32_t hc_wr32(uint32_t o) { return jb_r32(o); }
/* The keys of an op, from the state before the case (first op only). */
static void hc_frame_keys(uint32_t va, uint32_t a, uint32_t b) {
    uint32_t d = JB_D, m = hc_wr32(d + 0x14u), pend = hc_wr32(d + 0x18u);
    (void)a;
    if (va == 0x0042a3f4u) {
        if (pend != 0xffffffffu) { hc_key(HK_REQ_PENDING); return; }
        if (b > 6u) { hc_key(HK_REQ_HIGH); return; }
        int store = (b == 0 && m == 0) || (b == 1 && m == 1) || (b == 2 && m != 4) || (b == 3 && m == 4) ||
                    (b == 4 && (m == 0 || m == 3)) || (b == 5 && (m == 0 || m == 2)) || (b == 6 && (m == 2 || m == 3));
        hc_key(store ? HK_REQ_STORE : HK_REQ_NONE);
    } else if (va == 0x0042db18u) {
        if ((int32_t)hc_wr32(HC_VIEWS + 0x20u * m + 0xcu) > 0) hc_key(HK_VIS_VIEW);
        else hc_key(jb_world_start[d + 0x1cu] ? HK_VIS_TRANS : HK_VIS_NO);
    } else if (va == 0x00432b14u) {
        uint32_t rec = JB_ROWS;
        if (jb_world_start[JB_P + 0x23eu]) {
            if (!(jb_preset_of(0x0049b105u) & 0xffu)) hc_key(HK_IN_OFF);
            else {
                hc_key(HK_IN_ZONES);
                int tape = (jb_preset_of(0x004d24d6u) & 0xffffu) != 0;
                if (tape) hc_key(HK_IN_TAPE);
                int32_t x = (int32_t)(tape ? jb_preset_of(0x004d2306u) : hc_wr32(rec + 2u)) - (int32_t)hc_wr32(JB_CANVAS + 0x210u) +
                            (int32_t)hc_wr32(JB_CANVAS + 0x220u);
                int32_t y = (int32_t)(tape ? jb_preset_of(0x004d230au) : hc_wr32(rec + 6u)) - (int32_t)hc_wr32(JB_CANVAS + 0x214u) +
                            (int32_t)hc_wr32(JB_CANVAS + 0x224u);
                int hit = 0;
                for (unsigned r = 0; r < 3u && !hit; ++r) {
                    uint32_t at = JB_RECTS + 0x10u * r;
                    hit = x >= (int32_t)hc_wr32(at) && x <= (int32_t)hc_wr32(at + 8u) && y >= (int32_t)hc_wr32(at + 4u) &&
                          y <= (int32_t)hc_wr32(at + 0xcu);
                }
                hc_key(hit ? HK_IN_HIT : HK_IN_MISS);
            }
        }
        if (jb_world_start[rec + 0x1eu] | jb_world_start[rec + 0x1fu] | jb_world_start[rec + 0x20u] | jb_world_start[rec + 0x21u])
            hc_key(HK_IN_REQUESTS);
        if (!jb_world_start[rec + 0xbu]) hc_key(HK_IN_RELEASE);
        else if (jb_preset_of(0x0049b550u) & 0xffu) hc_key(HK_IN_HELD);
        else { hc_key(HK_IN_FIRST); if (m == 1u) hc_key(HK_IN_MODE1); }
    } else if (va == 0x00433c54u) {
        if (!(jb_preset_of(0x0049b105u) & 0xffu)) { hc_key(HK_CUR_OFF); return; }
        uint32_t z = hc_wr32(JB_P + 0x22eu), self = jb_at(JB_P + 0x232u), s;
        if (z) {
            hc_key(hc_wr32(JB_P + 0x226u) == self ? HK_CUR_SAME : HK_CUR_NEW);
            s = jb_preset_of(0x0049b08cu + 4u * z);
        } else if (hc_wr32(JB_P + 0x226u) == self) {
            hc_key(HK_CUR_BACK);
            s = hc_wr32(HC_CELL);
        } else { hc_key(HK_CUR_NONE); return; }
        hc_key((jb_preset_of(0x004d2566u) & 0xffffu) ? HK_CUR_PAGED : HK_CUR_UNPAGED);
        if (!jb_r32(s - (uint32_t)(uintptr_t)jb_world + 4u)) hc_key(HK_CUR_NULL4);
    }
}
static void hc_add_frame_op(int first) {
    uint32_t va, a = 0, b = 0;
    unsigned n = 0, mask = JB_EAX;
    switch (jb_rand(7u)) {
    case 0: case 1: va = 0x0042a3f4u; n = 2; a = jb_at(JB_D); b = jb_rand(5u) ? jb_rand(7u) : (jb_rand(2u) ? 7u + jb_rand(4u) : random32()); break;
    case 2: va = jb_rand(2u) ? 0x00429828u : 0x0042db10u; n = va == 0x00429828u; a = jb_at(JB_D); break;
    case 3: va = 0x0042db18u; break;
    case 4: case 5: va = 0x00432b14u; n = 2; a = jb_at(JB_P); b = jb_at(JB_ROWS); break;
    default: va = 0x00433c54u; n = 1; a = jb_at(JB_P); break;
    }
    if (first) hc_frame_keys(va, a, b);
    jb_op_add(va, mask, n, a);
    jb_ops[jb_op_count - 1u].b = b;
}
static int hc_frame_case(unsigned k) {
    hc_begin();
    hc_frame_world();
    for (unsigned i = 0, n = 1u + jb_rand(4u); i < n; ++i) hc_add_frame_op(i == 0);
    if (jb_world_start[JB_P + 0x23eu] && jb_rand(2u)) { /* the zone, then the image it selects */
        jb_op_add(0x00432b14u, JB_EAX, 2, jb_at(JB_P));
        jb_ops[jb_op_count - 1u].b = jb_at(JB_ROWS);
        jb_op_add(0x00433c54u, JB_EAX, 1, jb_at(JB_P));
    }
    return jb_run("frame", k);
}

static int hud_cockpit_cases(void) {
    current_function = "hud-cockpit";
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
        return fail("hud-cockpit: memory");
    if (get32(mapped + (SU_COUNTER - 0x400000u))) return fail("hud-cockpit: the counter word is not free");
    su_pristine_take();
    if (!ir_patch_import("cw3220.dll", "_calloc", (void *)ir_calloc) || !ir_patch_import("cw3220.dll", "_free", (void *)ir_free) ||
        !ir_patch_import("cw3220.dll", "_strncpy", (void *)hc_strncpy) || !ir_patch_import("cw3220.dll", "_memset", (void *)hc_memset) ||
        !ir_patch_import("cw3220.dll", "_itoa", (void *)hc_itoa) || !ir_patch_import("cw3220.dll", "_strcat", (void *)su_strcat) ||
        !ir_patch_import("cw3220.dll", "_strlen", (void *)su_strlen))
        return fail("hud-cockpit: import slot not found");
    if (hc_dispatch_setup() || hc_patch_all(1)) return 1;
    int bad = 0;
    static const struct { const char *name; int (*run)(unsigned); unsigned cases; } parts[] = {
        {"display", hc_display_case, 1500}, {"fonts", hc_fonts_case, 300}, {"shape", hc_shape_case, 1000},
        {"frame", hc_frame_case, 6000},
    };
    for (unsigned p = 0; !bad && p < sizeof(parts) / sizeof(parts[0]); ++p) {
        for (unsigned k = 0; !bad && k < parts[p].cases; ++k) {
            current_case = k; current_case_valid = 1;
            bad = parts[p].run(k);
        }
        if (!bad) printf("PASS: hud-cockpit %s: %u cases identical\n", parts[p].name, parts[p].cases);
    }
    if (!bad) {
        printf("HUD-COCKPIT coverage:");
        for (unsigned i = 0; i < HK_COUNT; ++i) {
            printf(" %s=%u", hc_key_names[i], hc_keys[i]);
            if (!hc_keys[i]) bad = fail("hud-cockpit: a branch was not reached");
        }
        printf("\nHUD-COCKPIT stand-in calls (original):");
        static const char letters[] = "d+wogrkl bnxstPQCALv<>";
        for (const char *l = letters; *l; ++l)
            if (*l != ' ') printf(" %c=%u", *l, jb_letters[(unsigned char)*l]);
        printf("\n");
    }
    current_case_valid = 0;
    if (hc_patch_all(0)) bad = 1;
    put32(mapped + 0x2cbee4u, 0);
    memcpy(mapped + SU_DATA_RVA, su_data_pristine, SU_DATA_SIZE);
    return bad;
}
