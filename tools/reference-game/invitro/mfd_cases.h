/* The MFD and HDD frames, their page dispatch and the MFD pages' frames,
 * original machine code against sim/src/mfd.c (--mfd; target
 * :differential_mfd_test). Included after gadgets_cases.h; reuses
 * startup_cases.h's DATA snapshots (su_take, su_compare), loop_cases.h's loop
 * set-up (lc_reset) and audio_cases.h's register-capturing machine call
 * (au_call). Identifiers: md_/MD_.
 *
 * Every case builds a synthetic MFD gauge G (its buttons, pages, rectangle,
 * saved context, power-up animation, alert block), the panel [0x49b088]
 * with the player, its selection object and the entry list, the display
 * [0x4cf99c], the current context [0x6c5f70], the HDD gauge H with its two
 * page objects, radio records and map page, the orders page, in an
 * identity-mapped buffer, presets the DATA words the code reads (the page
 * switch state 0x49cbc4/6/8, the button table 0x49cbd8, the alert tables
 * 0x49cb34/0x4d1c88/0x49cb94.., the title and order texts, the colours,
 * the shifts, the HDD target state 0x49d3dc and radio list 0x4d03c8, the
 * map limits 0x4aa640/8 and shift 0x49d6bc), then runs one or more rows on
 * both sides: side 0 the original's code, side 1 the sim's rows through a
 * dispatch of the sim's real services.
 *
 * Recorders on both sides (side 0: a jump patched over the entry; side 1:
 * a row at the same VA): the clock 0x467828 and part_condition 0x41b514
 * and panel_visible 0x431410 (scripted returns, so every branch is steered),
 * the delete 0x477548, 0x48349c, the gauge blink 0x438bc0, the fill
 * 0x486704 (the rectangle and the fill record at GC+0x22c), the rotated
 * blit 0x48185c (the point and size bytes), the labels 0x438920/0x438aac
 * (the texts), the panel copy 0x43122c, the blit 0x45c8bc (the point),
 * the page switch 0x446e60, the HDD executor 0x44a1a0, radio panel
 * 0x44b620, port 0x433158, status body 0x450c7c and page draw 0x449ee4;
 * the vtable slots (full draw, widget draw, page draw and frame) are host
 * functions with rows at their addresses on side 1. Every recorded call
 * also hashes the current context's 0x20c bytes, so the context save/load
 * order is compared. These callees are where the pixels are drawn; the
 * comparison is of the full drawing call stream with its bytes. damage_class
 * 0x438700, display_current 0x429820 and mfd_object 0x445234 run as the
 * original's code on side 0 and the sim's rows on side 1. Compared: the
 * recorder hash and count, DATA, the world (masked: GC+0x22c, a stack
 * address on both sides) and EAX where the contract defines it. */
#pragma once
/* The headers this suite builds on (sim_main.c). */
#include "init_records_cases.h"
#include "startup_cases.h"
#include "loop_cases.h"
#include "audio_cases.h"
#include "es2/sim/load.h"
#include "es2/sim/view.h"
#include "es2/sim/input.h"
#include "es2/sim/hud.h"

#define MD_WORLD_SIZE 0x2800u
#define MD_G 0x0000u       /* the MFD gauge, 0x400 */
#define MD_GVT 0x0400u     /* its vtable: +4 the full draw */
#define MD_WVT 0x0410u     /* the widgets' table: +0xc the widget draw */
#define MD_PVT 0x0420u     /* the pages' table: 0 the draw, +4 the frame */
#define MD_W 0x0440u       /* 16 widgets of 0x20 */
#define MD_PAGES 0x0640u   /* 6 MFD page objects of 0x60 */
#define MD_GC 0x0880u      /* the current context, 0x240 */
#define MD_E 0x0ac0u       /* the display [0x4cf99c] */
#define MD_PANEL 0x0ae0u   /* [0x49b088], 0x220 */
#define MD_M 0x0d00u       /* the alert block */
#define MD_BMP 0x0d40u     /* 8 bitmap headers of 0x10 */
#define MD_ANIM 0x0dc0u    /* the power-up animation */
#define MD_ALERT 0x0df0u   /* the alert animation */
#define MD_ASEQ 0x0e20u    /* +0 the step count, +4 the steps */
#define MD_STEPS 0x0e30u   /* 8 steps of 8 */
#define MD_ATBL 0x0e70u    /* +8 the bitmap pointers */
#define MD_ABMPS 0x0e80u   /* 4 pointers */
#define MD_LABEL 0x0ea0u   /* G+0x329's label */
#define MD_TLABEL 0x0ee0u  /* G+0xc9's label */
#define MD_TEXT 0x0f20u    /* 16 strings of 0x10 */
#define MD_SEL 0x1020u     /* the selection object [P+0x202] */
#define MD_KEYS 0x1040u    /* its key list */
#define MD_ENTRIES 0x1060u /* 4 entries of 0xd0 */
#define MD_D 0x13a0u       /* 4 entry data of 0x30 */
#define MD_Q 0x1460u       /* 4 cells of 8 */
#define MD_P 0x1480u       /* the player, 0x210 */
#define MD_T 0x1690u       /* the target, 0x1b0 */
#define MD_O 0x1840u       /* the MFD object [panel+0x1ed] */
#define MD_OS 0x1890u      /* [O+0x40] */
#define MD_F5 0x18b0u      /* [panel+0x1f5] */
#define MD_H 0x19c0u       /* the HDD gauge, 0x580 */
#define MD_HP 0x1f40u      /* 2 HDD page objects of 0x150 */
#define MD_HQ 0x21e0u      /* the map page's [P+0x10], 0x230 */
#define MD_V 0x2410u       /* 3 radio records of 0xa0 */
#define MD_X 0x2630u       /* the MFD page under test, 0x60 */
#define MD_ORD 0x2690u     /* the order dword [X+8] */
#define MD_CNT 0x26a0u     /* the counter [0x6cbee4] */
#define MD_HTBL 0x26b0u    /* [0x49cbcc]: +8 the bitmap pointers */
#define MD_HBMPS 0x26c0u   /* 16 pointers */
#define MD_PT 0x2700u      /* the helpers' points */
static BYTE *md_world, *md_world_start;
static es2_sim_entry md_entries[4096];
static es2_sim_dispatch md_dispatch;
static es2_sim_load md_load;
static es2_sim_view md_view;
static es2_sim_input md_input;
static es2_sim_hud md_hud;
static int md_bad;
static void md_refuse(const es2_sim_refusal *r, void *user) {
    (void)user;
    fprintf(stderr, "FAIL: own mfd: refused call to 0x%08lx (%s): %s\n", (unsigned long)r->va, r->name ? r->name : "?",
            r->reason ? r->reason : "");
    md_bad = 1;
}

/* ---- the recorders */
#define MD_KINDS 23u
static uint64_t md_hash[2];
static unsigned md_count[2], md_side_now, md_calls[2][MD_KINDS], md_kinds[MD_KINDS];
static char md_text[2][768];
static uint32_t md_clock[8], md_ret[MD_KINDS][4];
static uint32_t md_each[2][64], md_gch[2][64];   /* per call: a hash of its bytes (the first differing call is shown) */
static void md_mix(uint32_t v) {
    unsigned side = md_side_now, n = md_count[side];
    for (unsigned i = 0; i < 4u; ++i) { md_hash[side] ^= (BYTE)(v >> (8u * i)); md_hash[side] *= 0x100000001b3ull; }
    if (n && n <= 64u) md_each[side][n - 1u] = (md_each[side][n - 1u] ^ v) * 0x01000193u;
}
static void md_bytes(uint32_t va, uint32_t n) { for (uint32_t i = 0; i < n; ++i) md_mix(*(BYTE *)(uintptr_t)(va + i)); }
static void md_string(uint32_t va) {
    uint32_t n = 0;
    while (n < 64u && *(BYTE *)(uintptr_t)(va + n)) ++n;
    md_mix(n);
    md_bytes(va, n);
}
static uint32_t md_gc(void) { return get32(mapped + (0x006c5f70u - 0x400000u)); }
static void md_note(unsigned kind, const uint32_t *a, unsigned n) {
    unsigned side = md_side_now;
    if (md_count[side] < 64u) md_each[side][md_count[side]] = 0x811c9dc5u;
    ++md_count[side];
    md_mix(0xabc00000u + kind);
    for (unsigned i = 0; i < n; ++i) md_mix(a[i]);
    uint32_t gc = md_gc(), gh = 0x811c9dc5u;
    for (uint32_t i = 4u; i < 0x20cu; ++i) gh = (gh ^ *(BYTE *)(uintptr_t)(gc + i)) * 0x01000193u;   /* the context, without +0x22c */
    if (md_count[side] <= 64u) md_gch[side][md_count[side] - 1u] = gh;
    md_mix(gh);
    size_t used = strlen(md_text[side]);
    if (md_count[side] < 24u && used + 40u < sizeof(md_text[side]))
        snprintf(md_text[side] + used, sizeof(md_text[side]) - used, " %u(%lx,%lx)", kind, (unsigned long)(n ? a[0] : 0),
                 (unsigned long)(n > 1u ? a[1] : 0));
    if (side) ++md_kinds[kind];
}
/* One recorded call (the same on both sides); returns its EAX. */
static uint32_t md_record(unsigned kind, const uint32_t *a) {
    unsigned side = md_side_now, k = md_calls[side][kind]++;
    uint32_t ret = md_ret[kind][k & 3u];
    switch (kind) {
    case 1: md_note(1, NULL, 0); return md_clock[k & 7u];   /* the clock */
    case 2: {   /* part_condition(object, 0, n): the low words of the last two */
        uint32_t v[3] = {a[0], a[1] & 0xffffu, a[2] & 0xffffu};
        md_note(2, v, 3);
        return ret;
    }
    case 7: md_note(7, a, 1); md_bytes(a[0], 16u); md_bytes(get32((BYTE *)(uintptr_t)(md_gc() + 0x22cu)), 8u); return ret;
    case 8: {   /* 0x48185c(bitmap, point, angle, size, flags) */
        uint32_t v[3] = {a[0], a[2], a[4]};
        md_note(8, v, 3);
        md_bytes(a[1], 8u);
        md_bytes(a[3], 8u);
        return ret;
    }
    case 9: md_note(9, a, 1); md_bytes(a[0] + 0x1du, 4u); md_string(a[1]); return ret;   /* label(label, text) */
    case 10: {  /* label_key(widget, text, key) */
        uint32_t v[2] = {a[0], a[2]};
        md_note(10, v, 2);
        md_bytes(a[0], 4u);
        md_string(a[1]);
        return ret;
    }
    case 11: md_note(11, a, 3); return ret;   /* panel copy */
    case 12: md_note(12, a, 2); md_bytes(a[2], 8u); return ret;   /* blit_at(desc, bitmap, point) */
    case 5: case 13: case 17: md_note(kind, a, 2); return ret;
    case 16: md_note(16, NULL, 0); return ret;
    case 22: md_note(22, a, 1); md_bytes(a[0] + 0xcu, 4u); return ret;   /* a page frame: its state +0xc */
    default: md_note(kind, a, 1); return ret;
    }
}
#define MD_HOST0(k) static uint32_t __cdecl md_o##k(void) { return md_record(k, NULL); }
#define MD_HOST1(k) static uint32_t __cdecl md_o##k(uint32_t a) { const uint32_t s[1] = {a}; return md_record(k, s); }
#define MD_HOST2(k) static uint32_t __cdecl md_o##k(uint32_t a, uint32_t b) { const uint32_t s[2] = {a, b}; return md_record(k, s); }
#define MD_HOST3(k) static uint32_t __cdecl md_o##k(uint32_t a, uint32_t b, uint32_t c) { const uint32_t s[3] = {a, b, c}; return md_record(k, s); }
#define MD_HOST5(k) static uint32_t __cdecl md_o##k(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) { \
    const uint32_t s[5] = {a, b, c, d, e}; return md_record(k, s); }
MD_HOST0(1)
MD_HOST3(2)
MD_HOST1(3)
MD_HOST1(4)
MD_HOST2(5)
MD_HOST1(6)
MD_HOST1(7)
MD_HOST5(8)
MD_HOST2(9)
MD_HOST3(10)
MD_HOST3(11)
MD_HOST3(12)
MD_HOST2(13)
MD_HOST1(14)
MD_HOST1(15)
MD_HOST0(16)
MD_HOST2(17)
MD_HOST1(18)
MD_HOST1(19)
MD_HOST1(20)
MD_HOST1(21)
MD_HOST1(22)
static void md_s_stub(es2_sim_service_call *sc, void *user) { sc->eax = md_record((unsigned)(uintptr_t)user, sc->slots); }
static const struct { uint32_t va; void *stub; unsigned kind, slots; } md_stubs[] = {
    {0x00467828u, (void *)md_o1, 1, 0}, {0x0041b514u, (void *)md_o2, 2, 3}, {0x00431410u, (void *)md_o3, 3, 1},
    {0x00477548u, (void *)md_o4, 4, 1}, {0x0048349cu, (void *)md_o5, 5, 2}, {0x00438bc0u, (void *)md_o6, 6, 1},
    {0x00486704u, (void *)md_o7, 7, 1}, {0x0048185cu, (void *)md_o8, 8, 5}, {0x00438920u, (void *)md_o9, 9, 2},
    {0x00438aacu, (void *)md_o10, 10, 3}, {0x0043122cu, (void *)md_o11, 11, 3}, {0x0045c8bcu, (void *)md_o12, 12, 3},
    {0x00446e60u, (void *)md_o13, 13, 2}, {0x0044a1a0u, (void *)md_o14, 14, 1}, {0x0044b620u, (void *)md_o15, 15, 1},
    {0x00433158u, (void *)md_o16, 16, 0}, {0x00450c7cu, (void *)md_o17, 17, 2}, {0x00449ee4u, (void *)md_o18, 18, 1},
};
#define MD_STUBS (sizeof(md_stubs) / sizeof(md_stubs[0]))
static BYTE md_saved[MD_STUBS][5];
static int md_patch(uint32_t va, void *target, BYTE saved[5], int install) {
    BYTE *site = mapped + (va - 0x400000u);
    DWORD previous;
    if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &previous)) return fail("mfd: stub protection");
    if (install) { memcpy(saved, site, 5); site[0] = 0xe9; put32(site + 1, (uint32_t)(uintptr_t)target - (uint32_t)(uintptr_t)(site + 5)); }
    else memcpy(site, saved, 5);
    if (!VirtualProtect(site, 5, previous, &previous)) return fail("mfd: stub restore protection");
    FlushInstructionCache(GetCurrentProcess(), site, 5);
    return 0;
}
static int md_put(uint32_t va, const char *name, es2_sim_service_fn fn, void *user, unsigned slots) {
    for (unsigned i = 0; i < md_dispatch.count; ++i) {
        es2_sim_entry *e = &md_dispatch.entries[i];
        if (e->va != va) continue;
        e->kind = ES2_SIM_SERVICE; e->name = name; e->service = fn; e->service_user = user; e->pop_bytes = 0; e->stack_slots = (uint8_t)slots;
        return 0;
    }
    return es2_sim_dispatch_add_service(&md_dispatch, va, name, fn, user, 0, (uint8_t)slots);
}
static int md_dispatch_setup(void) {
    es2_sim_dispatch_init(&md_dispatch, md_entries, 4096, md_refuse, NULL);
    static es2_sim_services s;
    s.memory = &ir_memory;
    s.heap = &ir_heap;
    s.vt = &lc_vt;
    s.fs0 = NULL;
    if (es2_sim_register_services(&md_dispatch, &s)) return fail("mfd: dispatch");
    for (unsigned i = 0; i < MD_STUBS; ++i)
        if (md_put(md_stubs[i].va, "mfd recorder", md_s_stub, (void *)(uintptr_t)md_stubs[i].kind, md_stubs[i].slots)) return fail("mfd: dispatch");
    static const struct { void *host; unsigned kind; } hosts[] = {{(void *)md_o19, 19}, {(void *)md_o20, 20}, {(void *)md_o21, 21}, {(void *)md_o22, 22}};
    for (unsigned i = 0; i < 4u; ++i)
        if (md_put((uint32_t)(uintptr_t)hosts[i].host, "mfd slot", md_s_stub, (void *)(uintptr_t)hosts[i].kind, 1)) return fail("mfd: dispatch");
    const char *error = NULL;
    uint32_t bad = 0;
    if (es2_sim_dispatch_seal(&md_dispatch, &error, &bad)) return fail("mfd: dispatch seal");
    return 0;
}

/* ---- the case script */
typedef struct md_preset { uint32_t va, value; unsigned width; } md_preset;
static md_preset md_presets[320];
static unsigned md_preset_count;
static void md_set32(uint32_t va, uint32_t v) { md_presets[md_preset_count++] = (md_preset){va, v, 4}; }
static void md_set16(uint32_t va, uint32_t v) { md_presets[md_preset_count++] = (md_preset){va, v, 2}; }
static void md_set8(uint32_t va, uint32_t v) { md_presets[md_preset_count++] = (md_preset){va, v, 1}; }
typedef struct md_op { uint32_t va, a[6], eax_in; unsigned n, eax; } md_op;
static md_op md_ops[4];
static unsigned md_op_count;
static uint32_t md_res[2][4];
static uint32_t md_rand(uint32_t n) { return random32() % n; }
static uint32_t md_signed(uint32_t range) { return (uint32_t)((int32_t)md_rand(2u * range + 1u) - (int32_t)range); }
static uint32_t md_at(uint32_t offset) { return (uint32_t)(uintptr_t)md_world + offset; }
static void md_w32(uint32_t offset, uint32_t v) { put32(md_world_start + offset, v); }
static void md_w16(uint32_t offset, uint32_t v) { put16(md_world_start + offset, (uint16_t)v); }
static void md_w8(uint32_t offset, uint32_t v) { md_world_start[offset] = (BYTE)v; }
static uint32_t md_r32(uint32_t offset) { return get32(md_world_start + offset); }
static void md_op_add(uint32_t va, unsigned eax, unsigned n, const uint32_t *a) {
    md_op *o = &md_ops[md_op_count++];
    memset(o, 0, sizeof(*o));
    o->va = va; o->eax = eax; o->n = n; o->eax_in = md_rand(2u) ? random32() : 0u;
    for (unsigned i = 0; i < n; ++i) o->a[i] = a[i];
}
#define MD_OP(va, eax, ...) md_op_add((va), (eax), (unsigned)(sizeof((const uint32_t[]){__VA_ARGS__}) / sizeof(uint32_t)), (const uint32_t[]){__VA_ARGS__})
/* A value next to the bound b (or random). */
static uint32_t md_near(uint32_t b) {
    switch (md_rand(5u)) {
    case 0: return b - 1u;
    case 1: return b;
    case 2: return b + 1u;
    case 3: return b + md_signed(100u);
    default: return random32();
    }
}
static uint32_t md_text_at(unsigned i) { return md_at(MD_TEXT + 0x10u * (i & 15u)); }
/* The animation A: a step list of 1..6 steps, the step word below the count. */
static void md_anim(uint32_t a) {
    uint32_t count = 1u + md_rand(6u);
    md_w16(a, md_rand(3u));
    md_w16(a + 4u, md_rand(count));
    md_w32(a + 6u, md_rand(3u) ? random32() : 0u);
    md_w32(a + 0xau, md_rand(200u));
    md_w32(a + 0xeu, md_r32(a + 6u) + md_r32(a + 0xau));
    md_w32(a + 0x12u, md_at(MD_ASEQ));
    md_w32(a + 0x1eu, md_at(MD_ATBL));
    md_w32(MD_ASEQ, count);
    md_w32(MD_ASEQ + 4u, md_at(MD_STEPS));
    for (unsigned i = 0; i < 8u; ++i) {
        md_w32(MD_STEPS + 8u * i, md_rand(4u));
        md_w32(MD_STEPS + 8u * i + 4u, md_rand(4u) ? md_rand(60u) : 0u);
    }
}
/* The clock: values next to the case's thresholds. */
static void md_clocks(const uint32_t *bounds, unsigned n) {
    for (unsigned i = 0; i < 8u; ++i) md_clock[i] = md_near(bounds[md_rand(n)]);
}
/* The world every case shares and the DATA words. */
static void md_common(void) {
    memset(md_world_start, 0, MD_WORLD_SIZE);
    md_preset_count = 0;
    md_op_count = 0;
    for (unsigned k = 0; k < MD_KINDS; ++k)
        for (unsigned i = 0; i < 4u; ++i) md_ret[k][i] = random32();
    for (unsigned i = 0; i < 4u; ++i) md_ret[3][i] = md_rand(3u) ? random32() | 1u : random32() & 0xffffff00u;   /* visible */
    for (unsigned i = 0; i < 4u; ++i) md_ret[2][i] = md_rand(3u) ? md_rand(0x120u) : random32();   /* part_condition */
    for (unsigned i = 0; i < 8u; ++i) md_clock[i] = random32();
    /* the context and the strings */
    random_bytes(md_world_start + MD_GC, 0x240);
    for (unsigned i = 0; i < 16u; ++i) {
        static const char *const words[] = {"MFD", "", "STATUS", "ORDERS", "TGT", "A", "RADAR-2", "DMG"};
        strcpy((char *)md_world_start + MD_TEXT + 0x10u * i, words[md_rand(8u)]);
    }
    /* the panel and its objects */
    random_bytes(md_world_start + MD_PANEL, 0x220);
    md_w32(MD_PANEL + 0x203u, md_at(MD_P));
    md_w32(MD_PANEL + 0x1f9u, md_at(MD_M) - 0x766u);
    md_w32(MD_PANEL + 0x210u, md_rand(3u) ? md_at(MD_T) : 0u);
    md_w32(MD_PANEL + 0x1edu, md_at(MD_O));
    md_w32(MD_PANEL + 0x1f5u, md_at(MD_F5));
    random_bytes(md_world_start + MD_P, 0x210);
    md_w32(MD_P + 0x202u, md_at(MD_SEL));
    md_w8(MD_SEL + 0x1du, md_rand(4u) ? md_rand(4u) : 0xffu);
    md_w32(MD_SEL + 4u, md_at(MD_KEYS));
    for (unsigned i = 0; i < 4u; ++i) md_w32(MD_KEYS + 4u * i, md_rand(3u) ? md_at(MD_D + 0x30u * md_rand(4u)) : random32());
    uint32_t entries = md_rand(6u);
    md_w32(MD_PANEL + 0x98u, md_rand(8u) ? entries : (uint32_t)-1);
    for (unsigned i = 0; i < 10u; ++i) md_w32(MD_PANEL + 0x70u + 4u * i, md_rand(4u) ? md_at(MD_ENTRIES + 0xd0u * (i & 3u)) : 0u);
    for (unsigned i = 0; i < 4u; ++i) {
        uint32_t e = MD_ENTRIES + 0xd0u * i, d = MD_D + 0x30u * i, q = MD_Q + 8u * i;
        md_w32(e + 0xbdu, md_rand(5u) ? md_at(d) : 0u);
        md_w32(d + 0x20u, md_rand(5u) ? md_at(q) : 0u);
        md_w16(q, md_rand(3u) ? 0u : md_rand(3u));
        md_w16(q + 2u, md_rand(2u) ? 3u : md_rand(5u));
    }
    random_bytes(md_world_start + MD_T, 0x1b0);
    md_w16(MD_T + 0x1a8u, md_rand(3u) ? 0u : random32());
    random_bytes(md_world_start + MD_O, 0x50);
    md_w32(MD_O + 0x40u, md_at(MD_OS));
    md_w16(MD_OS + 0x1bu, md_rand(3u));
    random_bytes(md_world_start + MD_F5, 0x100);
    md_w8(MD_F5 + 0xd6u, md_rand(3u) ? 0u : random32());
    random_bytes(md_world_start + MD_E, 0x20);
    md_w32(MD_E + 0x14u, md_rand(5u) ? md_rand(4u) : 4u);
    md_w8(MD_E + 0x1cu, md_rand(2u) ? 0u : random32() | 1u);
    /* bitmaps */
    for (unsigned i = 0; i < 8u; ++i) {
        md_w16(MD_BMP + 0x10u * i + 4u, md_rand(4u) ? md_rand(64u) : random32());
        md_w16(MD_BMP + 0x10u * i + 6u, md_rand(4u) ? md_rand(64u) : random32());
    }
    for (unsigned i = 0; i < 4u; ++i) md_w32(MD_ABMPS + 4u * i, md_at(MD_BMP + 0x10u * i));
    md_w32(MD_ATBL + 8u, md_at(MD_ABMPS));
    md_w32(MD_HTBL + 8u, md_at(MD_HBMPS));
    for (unsigned i = 0; i < 16u; ++i) md_w32(MD_HBMPS + 4u * i, md_at(MD_BMP + 0x10u * (i & 7u)));
    md_w32(MD_CNT, random32());
    /* the widgets */
    for (unsigned i = 0; i < 16u; ++i) {
        uint32_t w = MD_W + 0x20u * i;
        random_bytes(md_world_start + w, 0x20);
        md_w32(w + 0x17u, md_at(MD_WVT));
    }
    md_w32(MD_WVT + 0xcu, (uint32_t)(uintptr_t)md_o20);
    md_w32(MD_GVT + 4u, (uint32_t)(uintptr_t)md_o19);
    md_w32(MD_PVT, (uint32_t)(uintptr_t)md_o21);
    md_w32(MD_PVT + 4u, (uint32_t)(uintptr_t)md_o22);
    for (unsigned i = 0; i < 6u; ++i) {
        random_bytes(md_world_start + MD_PAGES + 0x60u * i, 0x60);
        md_w32(MD_PAGES + 0x60u * i, md_at(MD_PVT));
    }
    /* DATA */
    md_set32(0x0049b088u, md_at(MD_PANEL));
    md_set32(0x004cf99cu, md_at(MD_E));
    md_set32(0x006c5f70u, md_at(MD_GC));
    md_set32(0x006cbee4u, md_at(MD_CNT));
    md_set32(0x0049cbccu, md_at(MD_HTBL));
    md_set16(0x004d253au, md_rand(3u));
    md_set16(0x004d253cu, md_rand(3u));
    md_set16(0x0049cbc6u, md_rand(2u) ? 0xffffu : md_rand(6u));
    md_set8(0x0049cbc8u, md_rand(2u));
    md_set16(0x0049cbc4u, md_rand(6u));
    for (unsigned i = 0; i < 6u * 13u; ++i) md_set8(0x0049cbd8u + i, md_rand(3u) == 0);
    for (unsigned i = 0; i < 16u; ++i) md_set16(0x0049cb34u + 2u * i, md_rand(3u));
    for (unsigned i = 0; i < 3u; ++i) {
        md_set32(0x004d1c88u + 4u * i, md_rand(3u) ? md_at(MD_ALERT) : 0u);
        md_set16(0x0049cb94u + 2u * i, random32());
        md_set16(0x0049cba0u + 2u * i, random32());
        md_set16(0x0049cbacu + 2u * i, random32());
        md_set16(0x0049cbb8u + 2u * i, random32());
    }
    for (unsigned i = 0; i < 6u; ++i) md_set32(0x004d135cu + 4u * i, md_text_at(md_rand(16u)));
    for (unsigned i = 0; i < 9u; ++i) {
        md_set32(0x004d12a8u + 4u * i, md_text_at(md_rand(16u)));
        md_set32(0x004d12f0u + 4u * i, md_text_at(md_rand(16u)));
    }
    md_set32(0x0049b0c4u, random32());
    md_set32(0x0049b0b8u, random32());
    md_set32(0x0049b0b0u, random32());
    md_set32(0x0049c394u, md_rand(3u) ? 0u : random32());
}
static void md_side(unsigned side) {
    lc_reset(side, 47, 0x01000000u, 1);
    lc_loop.dispatch = &md_dispatch;
    es2_sim_load_headless(&md_load, &lc_loop);
    es2_sim_load_bind(&md_load);
    memset(&md_view, 0, sizeof(md_view));
    md_view.loop = &lc_loop;
    es2_sim_view_bind(&md_view);
    es2_sim_input_headless(&md_input, &lc_loop, &lc_journal[side], 640, 480);
    es2_sim_input_bind(&md_input);
    unsigned headless = md_hud.headless;
    es2_sim_hud_init(&md_hud, &lc_loop);
    md_hud.headless = headless;
    es2_sim_hud_bind(&md_hud);
    memcpy(md_world, md_world_start, MD_WORLD_SIZE);
    for (unsigned i = 0; i < md_preset_count; ++i) {
        BYTE *p = mapped + (md_presets[i].va - 0x400000u);
        if (md_presets[i].width == 4) put32(p, md_presets[i].value);
        else if (md_presets[i].width == 2) put16(p, (uint16_t)md_presets[i].value);
        else *p = (BYTE)md_presets[i].value;
    }
    md_side_now = side;
    md_hash[side] = 0xcbf29ce484222325ull;
    md_count[side] = 0;
    md_text[side][0] = 0;
    memset(md_calls[side], 0, sizeof(md_calls[side]));
}
static int md_run(const char *what, unsigned k) {
    for (unsigned side = 0; side < 2u; ++side) {
        md_side(side);
        for (unsigned i = 0; i < md_op_count; ++i) {
            md_op *o = &md_ops[i];
            uint32_t in[3] = {o->eax_in, 0, 0}, out[3] = {0, 0, 0};
            static char where[200];
            snprintf(where, sizeof(where), "mfd (%s side %u op %u: 0x%08lx(0x%lx, 0x%lx))", what, side, i, (unsigned long)o->va,
                     (unsigned long)o->a[0], (unsigned long)o->a[1]);
            current_function = where;
            if (!side) au_call((uint32_t)(uintptr_t)(mapped + (o->va - 0x400000u)), o->a, o->n, in, out);
            else {
                es2_sim_result r;
                memset(&r, 0, sizeof(r));
                if (es2_sim_call(&md_dispatch, o->va, o->a, o->n, in, &r)) md_bad = 1;
                out[0] = r.eax;
            }
            md_res[side][i] = out[0];
            if (md_bad) { fprintf(stderr, "FAIL: mfd %s case %u op %u (0x%08lx): a call failed\n", what, k, i, (unsigned long)o->va); return 1; }
        }
        put32(md_world + MD_GC + 0x22cu, 0);   /* a stack address on both sides (the fill record) */
        if (!side) memcpy(md_world_start + MD_WORLD_SIZE, md_world, MD_WORLD_SIZE);
        su_take(side);
    }
    if (md_hash[0] != md_hash[1] || md_count[0] != md_count[1]) {
        fprintf(stderr, "FAIL: mfd %s case %u: recorded calls differ (%u / %u)\n  original:%s\n  own:     %s\n", what, k, md_count[0],
                md_count[1], md_text[0], md_text[1]);
        for (unsigned i = 0; i < md_count[0] && i < 64u; ++i)
            if (md_each[0][i] != md_each[1][i]) { fprintf(stderr, "  first differing call %u: %08lx / %08lx (context %08lx / %08lx)\n", i, (unsigned long)md_each[0][i], (unsigned long)md_each[1][i], (unsigned long)md_gch[0][i], (unsigned long)md_gch[1][i]); break; }
        return 1;
    }
    if (su_compare(what, k)) return 1;
    if (memcmp(md_world, md_world_start + MD_WORLD_SIZE, MD_WORLD_SIZE)) {
        for (unsigned i = 0, shown = 0; i < MD_WORLD_SIZE && shown < 16u; ++i)
            if (md_world[i] != md_world_start[MD_WORLD_SIZE + i]) {
                fprintf(stderr, "  world +0x%04x original %02x own %02x\n", i, md_world_start[MD_WORLD_SIZE + i], md_world[i]);
                ++shown;
            }
        fprintf(stderr, "FAIL: mfd %s case %u: the world differs (world at 0x%08lx)\n", what, k, (unsigned long)(uintptr_t)md_world);
        return 1;
    }
    for (unsigned i = 0; i < md_op_count; ++i)
        if (md_ops[i].eax && md_res[0][i] != md_res[1][i]) {
            fprintf(stderr, "FAIL: mfd %s case %u op %u (0x%08lx): EAX original 0x%08lx own 0x%08lx\n", what, k, i, (unsigned long)md_ops[i].va,
                    (unsigned long)md_res[0][i], (unsigned long)md_res[1][i]);
            return 1;
        }
    return 0;
}

/* ---- the MFD gauge */
static void md_gauge(void) {
    uint32_t g = MD_G;
    random_bytes(md_world_start + g, 0x350);
    md_w32(g, md_at(MD_GVT));
    for (unsigned i = 0; i < 13u; ++i) md_w32(g + 0x18u + 4u * i, md_at(MD_W + 0x20u * i));
    md_w32(g + 0x6cu, md_rand(2u) ? md_at(MD_ALERT) : 0u);
    md_w16(MD_ALERT, md_rand(3u));
    md_w8(g + 0x76u, md_rand(3u) ? 0u : 1u);
    md_w8(g + 0x77u, md_rand(2u));
    md_w8(g + 0x8cu, md_rand(6u) != 0);
    md_w8(g + 0x8du, md_rand(3u) != 0);
    md_w8(g + 0xb0u, md_rand(4u));
    md_w32(g + 0xc1u, md_rand(6u));
    md_w32(g + 0xc9u, md_at(MD_TLABEL));
    for (unsigned i = 0; i < 6u; ++i) md_w32(g + 0xcdu + 4u * i, md_at(MD_PAGES + 0x60u * i));
    for (unsigned i = 0; i < 6u; ++i) md_w8(g + 0xe5u + i, md_rand(3u) != 0);
    md_w32(g + 0xebu, md_rand(200u));
    md_w32(g + 0xefu, md_rand(200u));
    md_w32(g + 0x318u, md_rand(4u));
    md_w8(g + 0x328u, md_rand(2u));
    md_w32(g + 0x329u, md_at(MD_LABEL));
    md_w32(g + 0x331u, md_rand(3u) ? md_at(MD_ANIM) : 0u);
    md_anim(MD_ANIM);
    /* the alert block */
    md_w32(MD_M, md_rand(4u) ? md_text_at(md_rand(16u)) : 0u);
    static const uint32_t counts[] = {0, 1, 2, 2, 0xffffffffu};
    md_w32(MD_M + 0x1cu, counts[md_rand(5u)]);
    for (unsigned i = 0; i < 2u; ++i) {
        md_w32(MD_M + 4u + 4u * i, md_at(MD_BMP + 0x10u * md_rand(8u)));
        md_w32(MD_M + 0xcu + 8u * i, md_signed(300u));
        md_w32(MD_M + 0x10u + 8u * i, md_signed(300u));
    }
    md_w16(MD_M + 0x20u, random32());
    md_w16(MD_M + 0x22u, md_rand(3u) ? 2u : random32());
}
static unsigned md_cov_frame[12];
static int md_frame_case(unsigned k) {
    md_common();
    md_gauge();
    uint32_t g = MD_G;
    if (md_r32(g + 0xc1u) == 3u && !md_world_start[g + 0x8du]) md_w32(g + 0x331u, md_at(MD_ANIM));   /* the original reads it */
    uint32_t bounds[3] = {md_r32(g + 0x345u), md_r32(g + 0x100u), md_r32(MD_ANIM + 0xeu)};
    md_clocks(bounds, 3);
    MD_OP(0x00446350u, 0, md_at(g));
    unsigned before[MD_KINDS];
    memcpy(before, md_kinds, sizeof(before));
    int bad = md_run("frame", k);
    if (!bad) {
        uint32_t page = md_r32(g + 0xc1u);
        if (get32(md_world_start + MD_E + 0x14u) == 4u) ++md_cov_frame[0];
        else if (!md_world_start[g + 0x8du] && page == 3u) ++md_cov_frame[1];
        if (md_kinds[4] != before[4]) ++md_cov_frame[2];          /* the power-up's deletes */
        if (md_kinds[6] != before[6]) ++md_cov_frame[3];          /* the alert blink */
        if (md_kinds[7] != before[7]) ++md_cov_frame[4];          /* the alert page fill */
        if (md_kinds[8] != before[8]) ++md_cov_frame[5];          /* its bitmaps */
        if (md_kinds[11] != before[11]) ++md_cov_frame[6];        /* the panel copy */
        if (md_kinds[19] != before[19]) ++md_cov_frame[7];        /* the full draw */
        if (md_kinds[22] != before[22]) ++md_cov_frame[8 + (page == 0u || page == 4u)];   /* a page frame */
        if (md_kinds[13] != before[13]) ++md_cov_frame[10];       /* the page switch */
        if (md_kinds[20] != before[20]) ++md_cov_frame[11];       /* the buttons */
    }
    return bad;
}
static unsigned md_cov_helpers[10];
static int md_helper_case(unsigned k) {
    md_common();
    md_gauge();
    uint32_t g = MD_G;
    switch (md_rand(9u)) {
    case 0: MD_OP(0x004342e0u, 1, md_rand(4u) ? 2u : random32()); ++md_cov_helpers[0]; break;
    case 1: md_w16(g + 0x74u, md_rand(4u) ? md_rand(16u) : 0u); MD_OP(0x00446ddcu, 1, md_at(g)); ++md_cov_helpers[1]; break;
    case 2: MD_OP(0x00434310u, 1, md_at(MD_PANEL)); ++md_cov_helpers[2]; break;
    case 3: {
        uint32_t e = md_at(MD_ENTRIES + 0xd0u * md_rand(4u));
        MD_OP(md_rand(2u) ? 0x00440a3cu : 0x00440a64u, 1, e);
        ++md_cov_helpers[3];
        break;
    }
    case 4: MD_OP(0x0044718cu, 0, md_at(g)); ++md_cov_helpers[4]; break;
    case 5: {
        uint32_t bounds[2] = {md_r32(MD_ANIM + 0xeu), md_r32(MD_ANIM + 6u)};
        md_clocks(bounds, 2);
        md_w32(MD_PT, random32());
        md_w32(MD_PT + 4u, random32());
        MD_OP(0x00471e10u, 1, md_at(MD_ANIM), md_at(MD_PT));
        ++md_cov_helpers[5];
        break;
    }
    case 6: {
        /* a time beyond several steps */
        uint32_t span = 0;
        for (unsigned i = 0; i < 8u; ++i) span += md_r32(MD_STEPS + 8u * i + 4u);
        uint32_t bounds[3] = {md_r32(MD_ANIM + 0xeu), md_r32(MD_ANIM + 6u) + md_r32(MD_ANIM + 0xau) + span / 2u,
                              md_r32(MD_ANIM + 6u) + md_r32(MD_ANIM + 0xau) + span};
        md_clocks(bounds, 3);
        MD_OP(0x00471e88u, 1, md_at(MD_ANIM), md_rand(2u) ? random32() & 0xffffff00u : random32());
        ++md_cov_helpers[6];
        break;
    }
    case 7: MD_OP(0x00471f24u, 1, md_at(MD_ANIM)); ++md_cov_helpers[7]; break;
    default: {
        md_w32(MD_PT, random32());
        md_w32(MD_PT + 4u, random32());
        MD_OP(0x0045caa4u, 1, md_at(g), md_rand(5u) ? md_at(MD_BMP) : 0u, md_at(MD_PT), random32(), md_at(MD_PT + 8u), random32());
        ++md_cov_helpers[8];
        break;
    }
    }
    return md_run("helpers", k);
}

/* ---- the MFD pages */
static unsigned md_cov_pages[6];
static int md_page_case(unsigned k) {
    md_common();
    uint32_t x = MD_X;
    random_bytes(md_world_start + x, 0x60);
    md_w32(x, md_at(MD_PVT));
    switch (md_rand(5u)) {
    case 0:   /* the status page */
        md_w32(x + 0xcu, md_rand(2u) ? 0x64u : md_rand(3u) ? 0u : random32());
        md_w8(x + 0x34u, md_rand(2u));
        { uint32_t b[1] = {md_r32(x + 0x35u)}; md_clocks(b, 1); }
        MD_OP(0x0043b238u, 0, md_at(x));
        ++md_cov_pages[0];
        break;
    case 1:   /* the target page */
        md_w32(x + 0x1cu, md_rand(3u) ? 0u : random32());
        md_w8(x + 0x43u, md_rand(4u));
        MD_OP(0x00440314u, 0, md_at(x));
        ++md_cov_pages[1];
        break;
    default: {   /* the orders page */
        uint32_t order = md_rand(6u);
        md_w32(x + 8u, md_at(MD_ORD));
        md_w32(MD_ORD, order);
        for (unsigned i = 0; i < 6u; ++i) {
            md_w32(x + 0x14u + 4u * i, md_at(MD_W + 0x20u * i));
            md_w8(x + 0x2cu + i, md_rand(4u));
        }
        md_w32(x + 0x32u, md_rand(2u) ? order : md_rand(6u));
        md_w8(x + 0x36u, md_rand(3u) == 0);
        md_w16(x + 0x37u, md_rand(2u) ? 0xffffu : md_rand(6u));
        unsigned which = md_rand(3u);
        if (which == 0) MD_OP(0x0043f7ccu, 0, md_at(x));
        else if (which == 1) MD_OP(0x0043f8a0u, 0, md_at(x));
        else {
            static const uint32_t ns[] = {0, 1, 2, 3, 4, 5, 7, 8};
            MD_OP(0x0043fa5cu, 0, md_at(x), ns[md_rand(8u)], md_rand(2u) ? random32() & 0xffffff00u : random32());
        }
        ++md_cov_pages[2 + which];
        break;
    }
    }
    return md_run("pages", k);
}

/* ---- the HDD */
static void md_hdd(void) {
    uint32_t h = MD_H;
    random_bytes(md_world_start + h, 0x580);
    md_w32(h, md_at(MD_GVT));
    uint32_t buttons = md_rand(8u);
    md_w32(h + 0x68u, md_rand(8u) ? buttons : (uint32_t)-1);
    for (unsigned i = 0; i < 8u; ++i) md_w32(h + 0x18u + 4u * i, md_at(MD_W + 0x20u * i));
    md_w8(h + 0x76u, md_rand(2u));
    md_w8(h + 0x52fu, md_rand(2u));
    md_w8(h + 0xb0u, md_rand(4u));
    uint32_t page = md_rand(8u) ? md_rand(2u) : 2u;
    md_w32(h + 0x109u, page);
    for (unsigned i = 0; i < 2u; ++i) {
        uint32_t p = MD_HP + 0x150u * i;
        random_bytes(md_world_start + p, 0x150);
        md_w32(h + 0x101u + 4u * i, md_at(p));
        md_w32(p, md_at(MD_PVT));
    }
    md_w32(MD_HP + 0x150u + 0x7cu, md_rand(3u));   /* page 1's title index */
    for (unsigned i = 0; i < 4u; ++i) md_w8(h + 0x10du + i, md_rand(2u));
    if (page == 2u) { md_w8(h + 0x10fu, 0); md_w8(h + 0x111u, 0); }   /* page 2 has no page object (H+0x109) */
    md_w32(h + 0x51bu, md_at(MD_TLABEL));
    md_w16(h + 0x55cu, md_rand(2u) ? 4u : md_rand(8u));
    for (unsigned k = 0; k < 3u; ++k) {
        uint32_t v = MD_V + 0xa0u * k, s = h + 0x12du + 0x14eu * k;
        random_bytes(md_world_start + v, 0xa0);
        md_w8(v + 0x99u, md_rand(2u));
        md_w8(s + 0x147u, md_rand(2u));
        md_set32(0x004d03c8u + 4u * k, md_rand(4u) ? md_at(v) : 0u);
    }
    /* the map page (page 0's object) */
    uint32_t p = MD_HP;
    random_bytes(md_world_start + MD_HQ, 0x230);
    md_w32(p + 0x10u, md_at(MD_HQ));
    md_w32(MD_HQ + 0x220u, md_signed(0x400u));
    md_w32(p + 0x20u, md_signed(0x400u));
    md_w32(p + 0x145u, md_rand(0x2000u));
    uint32_t shift = md_rand(12u);
    md_set32(0x0049d6bcu, shift);
    uint32_t k = (uint32_t)((int32_t)((0u - md_r32(MD_HQ + 0x220u)) * md_r32(p + 0x20u)) >> shift);
    uint32_t lo = random32() % 0x100000u, hi = lo + random32() % 0x100000u;
    md_set32(0x004aa640u, lo);
    md_set32(0x004aa648u, hi);
    /* P+0x18 so that one of the rooms lands around the step */
    uint32_t step = md_r32(p + 0x145u);
    switch (md_rand(4u)) {
    case 0: md_w32(p + 0x18u, lo - 0xea60u + k + step + md_signed(step + 2u)); break;
    case 1: md_w32(p + 0x18u, hi + 0xea60u - k - step - md_signed(step + 2u)); break;
    case 2: md_w32(p + 0x18u, lo - 0xea60u + k + md_signed(3u)); break;
    default: md_w32(p + 0x18u, md_signed(0x200000u)); break;
    }
    md_set8(0x0049d6aeu, 0);
    md_set32(0x004d14e4u, random32());
    md_set32(0x004d14e8u, random32());
    md_set32(0x0049d3dcu, md_rand(2u) ? md_r32(MD_PANEL + 0x210u) : md_rand(2u) ? 0u : random32());
    md_set32(0x004d13d0u, md_rand(4u) ? md_text_at(md_rand(16u)) : 0u);
    for (unsigned i = 0; i < 3u; ++i) md_set32(0x004d13d8u + 4u * i, md_rand(4u) ? md_text_at(md_rand(16u)) : 0u);
}
static unsigned md_cov_hdd[12], md_cov_moved;
static int md_hdd_case(unsigned k) {
    md_common();
    md_hdd();
    uint32_t h = MD_H;
    unsigned which = md_rand(10u);
    if (which < 4u) {
        uint32_t b[1] = {md_r32(h + 0x530u)};
        md_clocks(b, 1);
        MD_OP(0x00449bf8u, 0, md_at(h));
    } else if (which == 4u) {
        if (md_r32(h + 0x109u) > 1u) md_w32(h + 0x109u, md_rand(2u));
        MD_OP(0x00449f64u, 0, md_at(h));
    } else if (which == 5u) {
        if (md_rand(2u)) md_w32(h + 0x109u, md_rand(2u) ? 2u : random32() | 4u);
        MD_OP(0x0044a704u, 0, md_at(h));
    } else if (which == 6u) {
        uint32_t n = md_r32(h + 0x68u);
        MD_OP(0x0044a174u, 1, md_at(h), md_rand(4u) && (int32_t)n > 0 ? md_at(MD_W + 0x20u * md_rand(n)) : random32());
    } else if (which == 7u) MD_OP(0x0044cff8u, 1, md_at(MD_HP));
    else if (which == 8u) MD_OP(0x0044d05cu, 1, md_at(MD_HP));
    else MD_OP(md_rand(2u) ? 0x00450b54u : 0x00450b74u, 1, md_at(MD_HP));
    unsigned before[MD_KINDS];
    memcpy(before, md_kinds, sizeof(before));
    uint8_t moved = 0;
    int bad = md_run("hdd", k);
    if (!bad) {
        moved = mapped[0x0049d6aeu - 0x400000u];
        ++md_cov_hdd[which];
        if (which < 4u) {
            if (md_kinds[18] != before[18]) ++md_cov_hdd[10];   /* the page draw */
            if (md_kinds[22] != before[22]) ++md_cov_hdd[11];   /* a page frame */
        }
        if (which == 7u || which == 8u) md_cov_moved += moved != 0;
    }
    return bad;
}

static int mfd_cases(void) {
    current_function = "mfd";
    es2_sim_identity_memory(&ir_memory);
    es2_sim_rtl_memory(&ir_memory, &su_rtl);
    su_data_pristine = malloc(SU_DATA_SIZE);
    su_snap[0] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    su_snap[1] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    ir_region = ww_alloc(IR_REGION_SIZE);
    lc_stack = VirtualAlloc(NULL, 0x10000u, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    lc_bits = ww_alloc(LC_BITS_SIZE);
    md_world = VirtualAlloc(NULL, MD_WORLD_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    md_world_start = malloc(2u * MD_WORLD_SIZE);
    if (!su_data_pristine || !su_snap[0] || !su_snap[1] || !ir_region || !lc_stack || !lc_bits || !md_world || !md_world_start)
        return fail("mfd: memory");
    su_pristine_take();
    static const struct { const char *dll, *name; void *stub; } stubs[] = {
        {"cw3220.dll", "_calloc", (void *)ir_calloc}, {"cw3220.dll", "_free", (void *)ir_free},
        {"cw3220.dll", "_memset", (void *)ir_memset}, {"cw3220.dll", "_memmove", (void *)ir_memmove},
    };
    for (unsigned i = 0; i < sizeof(stubs) / sizeof(stubs[0]); ++i)
        if (!ir_patch_import(stubs[i].dll, stubs[i].name, stubs[i].stub)) {
            fprintf(stderr, "import %s!%s\n", stubs[i].dll, stubs[i].name);
            return fail("mfd: import slot not found");
        }
    if (md_dispatch_setup()) return 1;
    for (unsigned i = 0; i < MD_STUBS; ++i) if (md_patch(md_stubs[i].va, md_stubs[i].stub, md_saved[i], 1)) return 1;
    int bad = 0;
    static const struct { const char *name; int (*run)(unsigned); unsigned cases; } parts[] = {
        {"frame", md_frame_case, 3000}, {"helpers", md_helper_case, 2000}, {"pages", md_page_case, 2000}, {"hdd", md_hdd_case, 3000},
    };
    /* Each part's cases run in chunks of MD_CHUNK; chunk c of the whole run
     * belongs to test shard c % count (suite_part_in_shard) and starts from its
     * own seed, so a chunk's cases do not depend on the shard layout. The
     * coverage lines count the shard's own chunks. */
    enum { MD_CHUNK = 500 };
    unsigned chunk = 0;
    for (unsigned p = 0; !bad && p < sizeof(parts) / sizeof(parts[0]); ++p) {
        unsigned owned = 0;
        for (unsigned first = 0; !bad && first < parts[p].cases; first += MD_CHUNK, ++chunk) {
            if (!suite_part_in_shard(chunk)) continue;
            unsigned end = parts[p].cases - first < MD_CHUNK ? parts[p].cases : first + MD_CHUNK;
            random_state = 0x48c06e21u ^ (chunk * 0x9e3779b9u);
            for (unsigned k = first; !bad && k < end; ++k) {
                current_case = k; current_case_valid = 1;
                bad = parts[p].run(k);
            }
            owned += end - first;
        }
        if (!bad && owned) printf("PASS: mfd %s: %u of %u cases identical\n", parts[p].name, owned, parts[p].cases);
    }
    for (unsigned i = 0; i < MD_STUBS; ++i) if (md_patch(md_stubs[i].va, md_stubs[i].stub, md_saved[i], 0)) bad = 1;
    if (!bad) {
        printf("MFD coverage: frame mode4 %u, power-up radar %u, deletes %u, blink %u, alert fill %u, alert bitmaps %u, panel copy %u, "
               "full draw %u, page frame other/0-4 %u/%u, page switch %u, buttons %u\n",
               md_cov_frame[0], md_cov_frame[1], md_cov_frame[2], md_cov_frame[3], md_cov_frame[4], md_cov_frame[5], md_cov_frame[6],
               md_cov_frame[7], md_cov_frame[8], md_cov_frame[9], md_cov_frame[10], md_cov_frame[11]);
        printf("MFD coverage: helpers 4342e0 %u, 446ddc %u, 434310 %u, 440a3c/64 %u, 44718c %u, 471e10 %u, 471e88 %u, 471f24 %u, 45caa4 %u; "
               "pages status %u, target %u, orders draw %u, frame %u, mark %u\n",
               md_cov_helpers[0], md_cov_helpers[1], md_cov_helpers[2], md_cov_helpers[3], md_cov_helpers[4], md_cov_helpers[5],
               md_cov_helpers[6], md_cov_helpers[7], md_cov_helpers[8], md_cov_pages[0], md_cov_pages[1], md_cov_pages[2], md_cov_pages[3],
               md_cov_pages[4]);
        printf("MFD coverage: hdd frame %u, page frame %u, title %u, button %u, scroll l/r %u/%u (moved %u), status %u, page draw %u, "
               "page frames %u; calls: clock %u, part %u, visible %u, delete %u, 48349c %u, blink %u, fill %u, rotated %u, label %u, "
               "label_key %u, copy %u, blit %u, page switch %u, executor %u, radio %u, port %u, status body %u, hdd page draw %u, "
               "slots %u/%u/%u/%u\n",
               md_cov_hdd[0] + md_cov_hdd[1] + md_cov_hdd[2] + md_cov_hdd[3], md_cov_hdd[4], md_cov_hdd[5], md_cov_hdd[6], md_cov_hdd[7],
               md_cov_hdd[8], md_cov_moved, md_cov_hdd[9], md_cov_hdd[10], md_cov_hdd[11], md_kinds[1],
               md_kinds[2], md_kinds[3], md_kinds[4], md_kinds[5], md_kinds[6], md_kinds[7], md_kinds[8], md_kinds[9], md_kinds[10],
               md_kinds[11], md_kinds[12], md_kinds[13], md_kinds[14], md_kinds[15], md_kinds[16], md_kinds[17], md_kinds[18],
               md_kinds[19], md_kinds[20], md_kinds[21], md_kinds[22]);
    }
    current_case_valid = 0;
    put32(mapped + 0x2cbee4u, 0);
    memcpy(mapped + SU_DATA_RVA, su_data_pristine, SU_DATA_SIZE);
    return bad;
}
