/* The row of sim/dispatch/services/fullscreen.h (sim/src/present.c,
 * presentation.md, "The DirectDraw full-screen switch"), original machine
 * code against the own row (target :differential_fullscreen_test): the
 * switch 0x4667c8 with its callees 0x46658c, 0x4665c4 and 0x45f13c, which
 * run as the original's code. It reuses startup_cases.h's snapshots and
 * loop_cases.h's loop, as presentation_roots_cases.h does. Identifiers use fs_/FS_.
 *
 * Each case is 1..6 calls of the switch. Before every call both sides get the
 * same seeded state: the mode [0x4d255e] (0, 1 or random), the kept-rectangle
 * flag [0x4d3700] and the cursor sprite [0x4d3724] (0, 1 or random), the
 * window handle, the game size [0x4d253e]/[0x4d2542] (640 x 480, 320 x 200 or
 * random, negative and odd included), the rectangle 0x4d36cc, the four
 * object dwords 0x4d3710..0x4d371c (0 or one of the world's four objects) and
 * the panel H [0x49b088] (0; H without a widget; H with a widget G whose slot
 * +4 is a recording stub). DirectDraw is modelled as an object world: one
 * vtable whose slots Release +8, CreatePalette +0x14, CreateSurface +0x18,
 * GetAttachedSurface +0x30, SetCooperativeLevel +0x50, SetDisplayMode +0x54
 * and SetPalette +0x7c are recording stdcall stubs; DirectDrawCreate and
 * USER32's GetSystemMetrics, GetWindowRect, SetRect, AdjustWindowRectEx,
 * SetWindowPos, ClipCursor, SetCursorPos, ShowCursor and PostQuitMessage are
 * import stubs; the release 0x466730 and the timer kill 0x4650d0 are patched
 * with stubs. A seeded failure mask (each method may answer an error, the
 * surface's first and second try separately) is the own platform's dd_fail,
 * the seeded AdjustWindowRectEx margins its `adjust`, the world's objects its
 * dd_object. On the own side the platform's dd_note writes the same log; the
 * rows GetWindowRect 0x49694c, SetCursorPos 0x4969e2, 0x466730 and 0x4650d0
 * are replaced by the same stubs after the seal, Release and the widget slot
 * are services at the stubs' host addresses. Compared: EAX of every call,
 * DATA 0x497000..0x6cc000 with the heap, the stub log. EDX and ECX are not
 * compared (no caller reads them). Coverage: counters from the original
 * side; every one must be non-zero. */
#pragma once
#include "init_records_cases.h"
#include "startup_cases.h"
#include "loop_cases.h"
#include "es2/sim/present.h"

#define FS_WORLD_SIZE 0x1000u
#define FS_OBJECTS 0x0000u   /* DirectDraw, primary, back buffer, palette: 0x10 each */
#define FS_VTABLE 0x0100u
#define FS_HUD 0x0200u       /* H: 0x200 bytes */
#define FS_WIDGET 0x0400u    /* G and its vtable at +0x10 */

static es2_sim_entry fs_entries[4096];
static es2_sim_dispatch fs_dispatch;
static es2_sim_present fs_present;
static BYTE *fs_world;
static int fs_bad;
static unsigned fs_side;
static uint32_t fs_va(uint32_t off) { return (uint32_t)(uintptr_t)(fs_world + off); }
static uint32_t fs_r(uint32_t n) { return random32() % n; }
static uint32_t fs_object(unsigned i) { return fs_va(FS_OBJECTS + 0x10u * i); }

extern void fs_call(uint32_t target, const uint32_t *in, uint32_t *out);
__asm__(".text\n.globl _fs_call\n_fs_call:\n\tpushl %ebp\n\tmovl %esp, %ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n"
        "\tmovl 12(%ebp), %ebx\n\tmovl (%ebx), %eax\n\tmovl 4(%ebx), %edx\n\tmovl 8(%ebx), %ecx\n"
        "\tcall *8(%ebp)\n"
        "\tmovl 16(%ebp), %ebx\n\tmovl %eax, (%ebx)\n\tmovl %edx, 4(%ebx)\n\tmovl %ecx, 8(%ebx)\n"
        "\tleal -12(%ebp), %esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n");

/* ---- the stub log of a side */
static uint64_t fs_hash[2];
static unsigned fs_count[2];
static char fs_text[2][4096];
static size_t fs_len[2];
static void fs_note7(char letter, const uint32_t v[7]) {
    uint64_t h = fs_hash[fs_side] ^ (uint32_t)letter;
    h *= 0x100000001b3ull;
    for (unsigned i = 0; i < 7u; ++i) { h ^= v[i]; h *= 0x100000001b3ull; }
    fs_hash[fs_side] = h;
    ++fs_count[fs_side];
    if (fs_len[fs_side] + 96u < sizeof(fs_text[0]))
        fs_len[fs_side] += (size_t)sprintf(fs_text[fs_side] + fs_len[fs_side], "%c(%lx,%lx,%lx,%lx,%lx,%lx,%lx) ", letter,
                                           (unsigned long)v[0], (unsigned long)v[1], (unsigned long)v[2], (unsigned long)v[3],
                                           (unsigned long)v[4], (unsigned long)v[5], (unsigned long)v[6]);
}
static void fs_note(char letter, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    const uint32_t v[7] = {a, b, c, d, 0, 0, 0};
    fs_note7(letter, v);
}

/* ---- the case's answers */
static uint32_t fs_fail, fs_rect[4], fs_metric, fs_widget_answer;
static int32_t fs_adjust[4];

/* ---- coverage (original side) */
enum { FSC_TO_FULL, FSC_BACK, FSC_KEPT, FSC_READ, FSC_CREATE_FAIL, FSC_CREATE_OK, FSC_COOP_FAIL, FSC_COOP_OK, FSC_MODE_FAIL,
       FSC_MODE_OK, FSC_REL_BACK, FSC_REL_PRIMARY, FSC_REL_PALETTE, FSC_NOREL_BACK, FSC_NOREL_PRIMARY, FSC_NOREL_PALETTE,
       FSC_BELOW, FSC_AT, FSC_SURF_FAIL, FSC_SURF_OK, FSC_RETRY_FAIL, FSC_RETRY_OK, FSC_ATT_FAIL, FSC_ATT_OK, FSC_FAILED,
       FSC_PAL_FAIL, FSC_PAL_OK, FSC_SPRITE_FULL, FSC_NOSPRITE_FULL, FSC_SPRITE_BACK, FSC_NOSPRITE_BACK, FSC_NO_HUD, FSC_NO_WIDGET,
       FSC_WIDGET, FSC_COUNT };
static const char *const fs_cv_names[FSC_COUNT] = {
    "to full screen", "back", "rectangle kept", "rectangle read", "create error", "create", "cooperative error", "cooperative",
    "mode error", "mode", "release back", "release primary", "release palette", "no back", "no primary", "no palette",
    "below 640", "640 or more", "surface error", "surface", "retry error", "retry", "attached error", "attached", "failure path",
    "palette error", "palette", "sprite (full)", "no sprite (full)", "sprite (back)", "no sprite (back)", "no panel",
    "panel without widget", "panel widget"};
static unsigned fs_cv[FSC_COUNT];
static void fs_cover(unsigned i) { if (!fs_side) ++fs_cv[i]; }
static uint32_t fs_answer(uint32_t bit, unsigned fail_cv, unsigned ok_cv) {
    fs_cover(fs_fail & bit ? fail_cv : ok_cv);
    return fs_fail & bit ? ES2_SIM_DD_ERROR : 0;
}
static uint32_t fs_index(uint32_t o) { return o >= fs_object(0) && o < fs_object(4) ? (o - fs_object(0)) / 0x10u : 0xeeu; }

/* ---- the original side's stubs */
static uint32_t __stdcall fs_o_release(uint32_t self) { fs_note('R', fs_index(self), 0, 0, 0); return 0; }
static uint32_t __stdcall fs_o_palette(uint32_t self, uint32_t flags, uint32_t entries, uint32_t out, uint32_t outer) {
    const uint32_t v[7] = {self, flags, entries, out, outer, 0, 0};
    fs_note7('p', v);
    uint32_t e = fs_answer(ES2_SIM_DD_FAIL_PALETTE, FSC_PAL_FAIL, FSC_PAL_OK);
    if (!e) put32((BYTE *)(uintptr_t)out, fs_object(3));
    return e;
}
static uint32_t __stdcall fs_o_surface(uint32_t self, const BYTE *desc, uint32_t out, uint32_t outer) {
    uint32_t caps = get32(desc + 0x68);
    const uint32_t v[7] = {self, get32(desc), get32(desc + 4), get32(desc + 0x14), caps, out, outer};
    fs_note7('s', v);
    uint32_t e = caps & 0x4000u ? fs_answer(ES2_SIM_DD_FAIL_SURFACE, FSC_SURF_FAIL, FSC_SURF_OK)
                                : fs_answer(ES2_SIM_DD_FAIL_SURFACE_RETRY, FSC_RETRY_FAIL, FSC_RETRY_OK);
    if (!e) put32((BYTE *)(uintptr_t)out, fs_object(1));
    return e;
}
static uint32_t __stdcall fs_o_attached(uint32_t self, const uint32_t *caps, uint32_t out) {
    fs_note('a', self, *caps, out, 0);
    uint32_t e = fs_answer(ES2_SIM_DD_FAIL_ATTACHED, FSC_ATT_FAIL, FSC_ATT_OK);
    if (!e) put32((BYTE *)(uintptr_t)out, fs_object(2));
    return e;
}
static uint32_t __stdcall fs_o_cooperative(uint32_t self, uint32_t hwnd, uint32_t flags) {
    fs_note('c', self, hwnd, flags, 0);
    return fs_answer(ES2_SIM_DD_FAIL_COOPERATIVE, FSC_COOP_FAIL, FSC_COOP_OK);
}
static uint32_t __stdcall fs_o_mode(uint32_t self, uint32_t w, uint32_t h, uint32_t bpp) {
    fs_note('m', self, w, h, bpp);
    uint32_t e = fs_answer(ES2_SIM_DD_FAIL_MODE, FSC_MODE_FAIL, FSC_MODE_OK);
    if (!e) {
        fs_cover((int32_t)w < 0x280 ? FSC_BELOW : FSC_AT);
        fs_cover(get32(mapped + (0x004d3718u - 0x400000u)) ? FSC_REL_BACK : FSC_NOREL_BACK);
        fs_cover(get32(mapped + (0x004d3714u - 0x400000u)) ? FSC_REL_PRIMARY : FSC_NOREL_PRIMARY);
        fs_cover(get32(mapped + (0x004d371cu - 0x400000u)) ? FSC_REL_PALETTE : FSC_NOREL_PALETTE);
    }
    return e;
}
static uint32_t __stdcall fs_o_set_palette(uint32_t self, uint32_t palette) { fs_note('e', self, palette, 0, 0); return 0; }
static uint32_t __stdcall fs_o_create(uint32_t guid, uint32_t out, uint32_t outer) {
    fs_note('D', guid, out, outer, 0);
    uint32_t e = fs_answer(ES2_SIM_DD_FAIL_CREATE, FSC_CREATE_FAIL, FSC_CREATE_OK);
    if (!e) put32((BYTE *)(uintptr_t)out, fs_object(0));
    return e;
}
static uint32_t __stdcall fs_o_metrics(uint32_t index) { fs_note('M', index, 0, 0, 0); return fs_metric + index; }
static uint32_t __stdcall fs_o_window_rect(uint32_t hwnd, uint32_t rc) {
    fs_note('G', hwnd, rc, 0, 0);
    for (unsigned i = 0; i < 4u; ++i) put32((BYTE *)(uintptr_t)rc + 4u * i, fs_rect[i]);
    return 1;
}
static uint32_t __stdcall fs_o_set_rect(uint32_t rc, uint32_t l, uint32_t t, uint32_t r, uint32_t b) {
    fs_note('r', l, t, r, b);
    const uint32_t v[4] = {l, t, r, b};
    for (unsigned i = 0; i < 4u; ++i) put32((BYTE *)(uintptr_t)rc + 4u * i, v[i]);
    return 1;
}
static uint32_t __stdcall fs_o_adjust(uint32_t rc, uint32_t style, uint32_t menu, uint32_t ex) {
    BYTE *p = (BYTE *)(uintptr_t)rc;
    const uint32_t v[7] = {get32(p), get32(p + 4), get32(p + 8), get32(p + 12), style, menu, ex};
    fs_note7('A', v);
    for (unsigned i = 0; i < 4u; ++i) put32(p + 4u * i, get32(p + 4u * i) + (uint32_t)fs_adjust[i]);
    return 1;
}
static uint32_t __stdcall fs_o_window_pos(uint32_t hwnd, uint32_t after, uint32_t x, uint32_t y, uint32_t cx, uint32_t cy,
                                          uint32_t flags) {
    const uint32_t v[7] = {hwnd, after, x, y, cx, cy, flags};
    fs_note7('P', v);
    return 1;
}
static uint32_t __stdcall fs_o_clip(const uint32_t *r) { fs_note('K', r[0], r[1], r[2], r[3]); return 1; }
static uint32_t __stdcall fs_o_cursor(uint32_t x, uint32_t y) { fs_note('C', x, y, 0, 0); return 1; }
static uint32_t __stdcall fs_o_show(uint32_t show) { fs_note('U', show, 0, 0, 0); return 0; }
static uint32_t __stdcall fs_o_quit(uint32_t code) { fs_cover(FSC_FAILED); fs_note('Q', code, 0, 0, 0); return 0; }
static void *__cdecl fs_o_memset(void *d, int v, size_t n) { return memset(d, v, n); }
static uint32_t __cdecl fs_o_release_all(void) { fs_note('X', 0, 0, 0, 0); return 0x0badf00du; }
static uint32_t __cdecl fs_o_timer(void) { fs_note('T', 0, 0, 0, 0); return 0x0badf00du; }
static uint32_t __cdecl fs_o_widget(uint32_t g) { fs_note('W', g, 0, 0, 0); return fs_widget_answer; }

/* ---- the own side's stubs */
static void fs_s_release(es2_sim_service_call *sc, void *user) { (void)user; fs_note('R', fs_index(sc->slots[0]), 0, 0, 0); sc->eax = 0; }
static void fs_s_window_rect(es2_sim_service_call *sc, void *user) {
    (void)user;
    fs_note('G', sc->slots[0], sc->slots[1], 0, 0);
    for (unsigned i = 0; i < 4u; ++i) put32((BYTE *)(uintptr_t)sc->slots[1] + 4u * i, fs_rect[i]);
    sc->eax = 1;
}
static void fs_s_cursor(es2_sim_service_call *sc, void *user) { (void)user; fs_note('C', sc->slots[0], sc->slots[1], 0, 0); sc->eax = 1; }
static void fs_s_release_all(es2_sim_service_call *sc, void *user) { (void)user; fs_note('X', 0, 0, 0, 0); sc->eax = 0x0badf00du; }
static void fs_s_timer(es2_sim_service_call *sc, void *user) { (void)user; fs_note('T', 0, 0, 0, 0); sc->eax = 0x0badf00du; }
static void fs_s_widget(es2_sim_service_call *sc, void *user) { (void)user; fs_note('W', sc->slots[0], 0, 0, 0); sc->eax = fs_widget_answer; }
static void fs_s_dd(void *user, uint32_t id, const uint32_t a[7]) {
    (void)user;
    static const char letters[ES2_SIM_DD_CALLS] = {
        [ES2_SIM_DD_CREATE] = 'D', [ES2_SIM_DD_COOPERATIVE] = 'c', [ES2_SIM_DD_MODE] = 'm', [ES2_SIM_DD_RELEASE] = 'R',
        [ES2_SIM_DD_SURFACE] = 's', [ES2_SIM_DD_ATTACHED] = 'a', [ES2_SIM_DD_PALETTE] = 'p', [ES2_SIM_DD_SET_PALETTE] = 'e',
        [ES2_SIM_DD_METRICS] = 'M', [ES2_SIM_DD_SET_RECT] = 'r', [ES2_SIM_DD_ADJUST] = 'A', [ES2_SIM_DD_WINDOW_POS] = 'P',
        [ES2_SIM_DD_CLIP] = 'K', [ES2_SIM_DD_SHOW_CURSOR] = 'U', [ES2_SIM_DD_POST_QUIT] = 'Q'};
    char letter = id < ES2_SIM_DD_CALLS ? letters[id] : 0;
    if (!letter) { fprintf(stderr, "FAIL: own fullscreen: unexpected platform call %lu\n", (unsigned long)id); fs_bad = 1; return; }
    fs_note7(letter, a);
}

static const struct { uint32_t va; void *stub; } fs_patches[2] = {
    {0x00466730u, (void *)fs_o_release_all}, {0x004650d0u, (void *)fs_o_timer},
};
static BYTE fs_saved[2][5];
static int fs_patch(unsigned i, int install) {
    BYTE *site = mapped + (fs_patches[i].va - 0x400000u);
    DWORD previous;
    if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &previous)) return fail("fullscreen: stub protection");
    if (install) {
        memcpy(fs_saved[i], site, 5);
        site[0] = 0xe9;
        put32(site + 1, (uint32_t)(uintptr_t)fs_patches[i].stub - (uint32_t)(uintptr_t)(site + 5));
    } else memcpy(site, fs_saved[i], 5);
    if (!VirtualProtect(site, 5, previous, &previous)) return fail("fullscreen: stub restore protection");
    FlushInstructionCache(GetCurrentProcess(), site, 5);
    return 0;
}

/* ---- the own side */
static void fs_refuse(const es2_sim_refusal *r, void *user) {
    (void)user;
    fprintf(stderr, "FAIL: own fullscreen: refused call to 0x%08lx (%s): %s\n", (unsigned long)r->va, r->name ? r->name : "?",
            r->reason ? r->reason : "");
    fs_bad = 1;
}
static int fs_dispatch_setup(void) {
    es2_sim_dispatch_init(&fs_dispatch, fs_entries, 4096, fs_refuse, NULL);
    static es2_sim_services s;
    s.memory = &ir_memory;
    s.heap = &ir_heap;
    s.vt = &lc_vt;
    s.fs0 = NULL;
    if (es2_sim_register_services(&fs_dispatch, &s)) return fail("fullscreen: dispatch");
    if (es2_sim_dispatch_add_service(&fs_dispatch, (uint32_t)(uintptr_t)fs_o_release, "fullscreen Release stub", fs_s_release, NULL, 4, 1) ||
        es2_sim_dispatch_add_service(&fs_dispatch, (uint32_t)(uintptr_t)fs_o_widget, "fullscreen widget stub", fs_s_widget, NULL, 0, 1))
        return fail("fullscreen: dispatch (stubs)");
    const char *error = NULL;
    uint32_t bad = 0;
    if (es2_sim_dispatch_seal(&fs_dispatch, &error, &bad)) return fail("fullscreen: dispatch seal");
    static const struct { uint32_t va; es2_sim_service_fn fn; } rows[4] = {
        {0x0049694cu, fs_s_window_rect}, {0x004969e2u, fs_s_cursor}, {0x00466730u, fs_s_release_all}, {0x004650d0u, fs_s_timer},
    };
    for (unsigned i = 0; i < 4u; ++i) {
        es2_sim_entry *e = (es2_sim_entry *)es2_sim_dispatch_find(&fs_dispatch, rows[i].va);
        if (!e || e->kind != ES2_SIM_SERVICE) return fail("fullscreen: a callee row is missing");
        e->service = rows[i].fn;
        e->service_user = NULL;
    }
    const es2_sim_entry *e = es2_sim_dispatch_find(&fs_dispatch, 0x004667c8u);
    if (!e || e->kind != ES2_SIM_SERVICE) return fail("fullscreen: the row is missing");
    return 0;
}
static void fs_side_begin(unsigned side) {
    lc_reset(side, 47, 0x01000000u, 1);
    lc_loop.dispatch = &fs_dispatch;
    fs_side = side;
    fs_hash[side] = 0xcbf29ce484222325ull;
    fs_count[side] = 0;
    fs_len[side] = 0;
    fs_text[side][0] = 0;
    if (side) {
        es2_sim_present_headless(&fs_present, &lc_loop);
        es2_sim_present_platform *p = &fs_present.platform;
        for (unsigned i = 0; i < 4u; ++i) { p->dd_object[i] = fs_object(i); p->adjust[i] = fs_adjust[i]; }
        p->dd_fail = fs_fail;
        p->dd_note = fs_s_dd;
        es2_sim_present_bind(&fs_present);
    }
}

static uint32_t fs_size(uint32_t big, uint32_t small) {
    uint32_t r = fs_r(4u);
    return r == 0 ? big : r == 1u ? small : r == 2u ? (uint32_t)(int32_t)(fs_r(0x2000u) - 0x1000u) : random32();
}
static uint32_t fs_flag(void) { uint32_t r = fs_r(4u); return r < 2u ? 0 : r == 2u ? 1u : random32(); }

static int fs_case(unsigned k) {
    unsigned n = 1u + fs_r(6u);
    struct { uint32_t in[3], dd, kept, sprite, hwnd, w, h, rect[4], objects[4], hud; } c[6];
    uint32_t eax[2][6];
    for (unsigned i = 0; i < n; ++i) {
        for (unsigned j = 0; j < 3u; ++j) c[i].in[j] = random32();
        c[i].dd = fs_flag();
        c[i].kept = fs_flag();
        c[i].sprite = fs_flag();
        c[i].hwnd = random32();
        c[i].w = fs_size(640u, 320u);
        c[i].h = fs_size(480u, 200u);
        for (unsigned j = 0; j < 4u; ++j) c[i].rect[j] = fs_r(2u) ? fs_r(2000u) : random32();
        for (unsigned j = 0; j < 4u; ++j) c[i].objects[j] = fs_r(2u) ? fs_object(fs_r(4u)) : 0;
        c[i].hud = fs_r(3u);
    }
    /* failure bits: none in half of the cases, else each with 1/4 */
    fs_fail = 0;
    if (fs_r(2u))
        for (uint32_t bit = 1u; bit <= ES2_SIM_DD_FAIL_PALETTE; bit <<= 1)
            if (!fs_r(4u)) fs_fail |= bit;
    for (unsigned i = 0; i < 4u; ++i) { fs_rect[i] = random32(); fs_adjust[i] = (int32_t)fs_r(64u) - 32; }
    fs_metric = random32();
    fs_widget_answer = random32();
    for (unsigned side = 0; side < 2u; ++side) {
        fs_side_begin(side);
        for (unsigned i = 0; i < n && !fs_bad && !lc_bad; ++i) {
            put32(mapped + (0x004d255eu - 0x400000u), c[i].dd);
            put32(mapped + (0x004d3700u - 0x400000u), c[i].kept);
            put32(mapped + (0x004d3724u - 0x400000u), c[i].sprite);
            put32(mapped + (0x004d3610u - 0x400000u), c[i].hwnd);
            put32(mapped + (0x004d253eu - 0x400000u), c[i].w);
            put32(mapped + (0x004d2542u - 0x400000u), c[i].h);
            for (unsigned j = 0; j < 4u; ++j) put32(mapped + (0x004d36ccu + 4u * j - 0x400000u), c[i].rect[j]);
            for (unsigned j = 0; j < 4u; ++j) put32(mapped + (0x004d3710u + 4u * j - 0x400000u), c[i].objects[j]);
            put32(fs_world + FS_HUD + 0x1e9u, c[i].hud == 2u ? fs_va(FS_WIDGET) : 0);
            put32(mapped + (0x0049b088u - 0x400000u), c[i].hud ? fs_va(FS_HUD) : 0);
            if (!side) {
                uint32_t out[3];
                unsigned failed = fs_cv[FSC_FAILED];
                ++fs_cv[c[i].dd ? FSC_BACK : FSC_TO_FULL];
                if (!c[i].dd) ++fs_cv[c[i].kept ? FSC_KEPT : FSC_READ];
                fs_call((uint32_t)(uintptr_t)(mapped + (0x004667c8u - 0x400000u)), c[i].in, out);
                eax[0][i] = out[0];
                /* the sprite's ShowCursor and the panel: not on the failure path */
                if (fs_cv[FSC_FAILED] == failed) {
                    if (c[i].dd) ++fs_cv[c[i].sprite ? FSC_SPRITE_BACK : FSC_NOSPRITE_BACK];
                    else ++fs_cv[c[i].sprite ? FSC_SPRITE_FULL : FSC_NOSPRITE_FULL];
                    ++fs_cv[c[i].hud == 0 ? FSC_NO_HUD : c[i].hud == 1u ? FSC_NO_WIDGET : FSC_WIDGET];
                }
            } else {
                es2_sim_result r;
                memset(&r, 0, sizeof(r));
                if (es2_sim_call(&fs_dispatch, 0x004667c8u, NULL, 0, c[i].in, &r)) fs_bad = 1;
                eax[1][i] = r.eax;
            }
        }
        if (fs_bad || lc_bad) { fprintf(stderr, "FAIL: fullscreen case %u: a call failed (side %u)\n  log: %s\n", k, side, fs_text[side]); return 1; }
        su_take(side);
    }
    for (unsigned i = 0; i < n; ++i)
        if (eax[0][i] != eax[1][i]) {
            fprintf(stderr, "FAIL: fullscreen case %u: call %u EAX %08lx / %08lx\n  original: %s\n  own:      %s\n", k, i,
                    (unsigned long)eax[0][i], (unsigned long)eax[1][i], fs_text[0], fs_text[1]);
            return 1;
        }
    if (su_compare("fullscreen", k)) return 1;
    if (fs_hash[0] != fs_hash[1] || fs_count[0] != fs_count[1]) {
        fprintf(stderr, "FAIL: fullscreen case %u: stub calls differ (%u / %u)\n  original: %s\n  own:      %s\n", k, fs_count[0],
                fs_count[1], fs_text[0], fs_text[1]);
        return 1;
    }
    return 0;
}

static int fullscreen_cases(void) {
    current_function = "fullscreen";
    es2_sim_identity_memory(&ir_memory);
    es2_sim_rtl_memory(&ir_memory, &su_rtl);
    su_data_pristine = malloc(SU_DATA_SIZE);
    su_snap[0] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    su_snap[1] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    ir_region = ww_alloc(IR_REGION_SIZE);
    lc_stack = VirtualAlloc(NULL, 0x10000u, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    lc_bits = ww_alloc(LC_BITS_SIZE);
    fs_world = VirtualAlloc(NULL, FS_WORLD_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!su_data_pristine || !su_snap[0] || !su_snap[1] || !ir_region || !lc_stack || !lc_bits || !fs_world) return fail("fullscreen: memory");
    if (get32(mapped + (SU_COUNTER - 0x400000u))) return fail("fullscreen: the counter word is not free");
    su_pristine_take();
    static const struct { const char *dll, *name; void *stub; } stubs[] = {
        {"DDRAW.dll", "DirectDrawCreate", (void *)fs_o_create},      {"USER32.dll", "GetSystemMetrics", (void *)fs_o_metrics},
        {"USER32.dll", "GetWindowRect", (void *)fs_o_window_rect},   {"USER32.dll", "SetRect", (void *)fs_o_set_rect},
        {"USER32.dll", "AdjustWindowRectEx", (void *)fs_o_adjust},   {"USER32.dll", "SetWindowPos", (void *)fs_o_window_pos},
        {"USER32.dll", "ClipCursor", (void *)fs_o_clip},             {"USER32.dll", "SetCursorPos", (void *)fs_o_cursor},
        {"USER32.dll", "ShowCursor", (void *)fs_o_show},             {"USER32.dll", "PostQuitMessage", (void *)fs_o_quit},
        {"cw3220.dll", "_memset", (void *)fs_o_memset},
    };
    for (unsigned i = 0; i < sizeof(stubs) / sizeof(stubs[0]); ++i)
        if (!ir_patch_import(stubs[i].dll, stubs[i].name, stubs[i].stub)) {
            fprintf(stderr, "import %s!%s\n", stubs[i].dll, stubs[i].name);
            return fail("fullscreen: import slot not found");
        }
    for (unsigned i = 0; i < 2u; ++i)
        if (fs_patch(i, 1)) return 1;
    if (fs_dispatch_setup()) return 1;
    /* the objects, their vtable and the panel's widget (the same on both sides) */
    for (unsigned j = 0; j < 4u; ++j) put32(fs_world + FS_OBJECTS + 0x10u * j, fs_va(FS_VTABLE));
    static const struct { uint32_t slot; void *stub; } methods[] = {
        {0x08u, (void *)fs_o_release},   {0x14u, (void *)fs_o_palette},     {0x18u, (void *)fs_o_surface},
        {0x30u, (void *)fs_o_attached},  {0x50u, (void *)fs_o_cooperative}, {0x54u, (void *)fs_o_mode},
        {0x7cu, (void *)fs_o_set_palette},
    };
    for (unsigned i = 0; i < sizeof(methods) / sizeof(methods[0]); ++i)
        put32(fs_world + FS_VTABLE + methods[i].slot, (uint32_t)(uintptr_t)methods[i].stub);
    put32(fs_world + FS_WIDGET, fs_va(FS_WIDGET + 0x10u));
    put32(fs_world + FS_WIDGET + 0x14u, (uint32_t)(uintptr_t)fs_o_widget);
    int bad = 0;
    const unsigned cases = 3000;
    for (unsigned k = 0; !bad && k < cases; ++k) {
        current_case = k; current_case_valid = 1;
        bad = fs_case(k);
    }
    if (!bad) {
        printf("PASS: fullscreen: %u cases identical\n", cases);
        printf("fullscreen coverage:");
        for (unsigned i = 0; i < FSC_COUNT; ++i) printf(" %s %u%s", fs_cv_names[i], fs_cv[i], i + 1u < FSC_COUNT ? "," : "\n");
        for (unsigned i = 0; i < FSC_COUNT; ++i)
            if (!fs_cv[i]) { fprintf(stderr, "FAIL: fullscreen: path '%s' never taken\n", fs_cv_names[i]); bad = 1; }
    }
    current_case_valid = 0;
    for (unsigned i = 0; i < 2u; ++i) fs_patch(i, 0);
    es2_sim_present_bind(NULL);
    put32(mapped + 0x2cbee4u, 0);
    memcpy(mapped + SU_DATA_RVA, su_data_pristine, SU_DATA_SIZE);
    return bad;
}
