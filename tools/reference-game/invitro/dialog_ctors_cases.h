/* The dialog constructors, destructors and draws and the F12/Alt+P options
 * session, original machine code against sim/src/dialogs.c (--dialog-ctors). It
 * reuses startup_cases.h's snapshots and loop_cases.h's virtual time and loop,
 * as widget_leaves_cases.h does.
 *
 * Both sides start from the pristine DATA and an empty heap and build the
 * same synthetic world from the same seeded generator: the dialog block, a
 * position, the current surface with its clip record and pages, the saved
 * graphics context, a bitmap set whose image table holds objects with a
 * method table, the display record 0x4cf99c, the shell record 0x4d24bc's
 * view, camera and size fields, a camera entry, the objective indices, the
 * resolution shifts, screen size, fonts, colours, scroll and the once
 * guards (each set or clear). Operator new and delete (through _calloc and
 * _free on the harness heap), the array constructor 0x49667c, the element
 * constructors 0x48de2f and 0x49545b, the graphics context 0x480c34 and,
 * within the set, the rows themselves run for real on the original's side
 * (0x455950 reaches the real 0x4541b8 and 0x454f80; the gadgets' table +0
 * is the real 0x455014). The other packages' callees are recording stubs on
 * both sides; the dialog tables' frame (+4), text (+0x14) and the options'
 * modal loop (+8) are written in DATA to host stubs on both sides, as are
 * the driver slots. Arguments that point into a side's frame (rectangles,
 * points, the view record) are logged by their contents or not at all.
 *
 * Parts:
 * - dialogs: one constructor (0x4541b8, 0x455924, 0x455950, 0x4561dc,
 *   0x4566e0, 0x457538) on a random-filled block, for the four dialogs
 *   mostly followed by their draw, then the destructor (0x4543b0, 0x455fac,
 *   0x456cf0, 0x457a9c) with a random flag.
 * - rows: 1..6 of 0x453824, 0x453848, 0x454f14, 0x454f50, 0x454f80,
 *   0x455014, 0x42d89c, 0x42d944, 0x45cfc8, each on its own blocks.
 * - session: 0x45cff0 with a scripted display (0x45fba4's stub moves the
 *   display's busy byte and word) and camera mode.
 * Compared: DATA 0x497000..0x6cc000 and the heap, the stub log, EAX (the
 * constructors, 0x453824, 0x454f14, 0x454f50, 0x454f80, 0x42d89c,
 * 0x42d944) and EDX (0x454f14, 0x454f50). ECX of 0x454f50 is not compared
 * (the machine call does not return it). Coverage: per-path counters from
 * the original side; every one must be non-zero. */
#pragma once
/* The headers this suite builds on (sim_main.c). */
#include "init_records_cases.h"
#include "startup_cases.h"
#include "loop_cases.h"
#include "es2/sim/dialogs.h"
#include "es2/sim/hud.h"
#include "es2/sim/input.h"

#define DZ_TRACE 2048u
#define DZ_IMAGES 64u
#define DZ_OPS 6u
static es2_sim_entry dz_entries[4096];
static es2_sim_dispatch dz_dispatch;
static es2_sim_hud dz_hud;
static es2_sim_hud_event dz_trace[DZ_TRACE];
static es2_sim_dialogs dz_dialogs;
static es2_sim_input dz_input;
static int dz_bad;
static unsigned dz_side;

/* ---- the stub log of a side */
static uint64_t dz_hash[2];
static unsigned dz_count[2], dz_letters[2][128];
static char dz_text[2][8192];
static size_t dz_len[2];
static void dz_note(char letter, uint32_t a, uint32_t b, uint32_t c) {
    uint64_t h = dz_hash[dz_side];
    const uint32_t v[4] = {(uint32_t)letter, a, b, c};
    for (unsigned i = 0; i < 4u; ++i) { h ^= v[i]; h *= 0x100000001b3ull; }
    dz_hash[dz_side] = h;
    ++dz_count[dz_side];
    ++dz_letters[dz_side][(unsigned char)letter & 127u];
    if (dz_len[dz_side] + 48u < sizeof(dz_text[0]))
        dz_len[dz_side] += (size_t)sprintf(dz_text[dz_side] + dz_len[dz_side], "%c(%lx,%lx,%lx) ", letter, (unsigned long)a,
                                           (unsigned long)b, (unsigned long)c);
}
static uint32_t dz_get(uint32_t a) { return get32((BYTE *)(uintptr_t)a); }
static uint8_t dz_get8(uint32_t a) { return *(BYTE *)(uintptr_t)a; }
static void dz_put8(uint32_t a, uint8_t v) { *(BYTE *)(uintptr_t)a = v; }
static void dz_put16(uint32_t a, uint16_t v) { put16((BYTE *)(uintptr_t)a, v); }
static void dz_put32(uint32_t a, uint32_t v) { put32((BYTE *)(uintptr_t)a, v); }
static uint32_t dz_mix(uint32_t a, unsigned words) {
    uint32_t h = 0x811c9dc5u;
    for (unsigned i = 0; i < words; ++i) h = (h ^ dz_get(a + 4u * i)) * 0x01000193u;
    return h;
}

/* ---- the world (filled in dz_build) */
static uint32_t dz_seed;
static uint32_t dz_rand(uint32_t n) {
    dz_seed = dz_seed * 1103515245u + 12345u;
    return (dz_seed >> 8) % n;
}
typedef struct dz_world {
    uint32_t obj, small, rect, spare, surface, clip, gc, bset, images, ivt, img[DZ_IMAGES], owner, ovt, display, view, cam, entry;
    uint32_t vrect, origin, indices, hb[DZ_OPS], hbuf[DZ_OPS], g[DZ_OPS], label[DZ_OPS];
} dz_world;
static dz_world dz_w, dz_w0;

/* ---- the stubs' answers and the display script (reset per side) */
static uint32_t dz_answer_seed;
static uint32_t dz_answer(uint32_t n) {
    dz_answer_seed = dz_answer_seed * 1103515245u + 12345u;
    return (dz_answer_seed >> 8) % n;
}
typedef struct dz_frame { uint8_t busy; uint32_t word; } dz_frame;
static dz_frame dz_script[16];
static unsigned dz_script_n, dz_script_i, dz_phase, dz_seen, dz_entries_null, dz_entries_4, dz_entries_other;
static uint32_t dz_stub(char l, const uint32_t *s) {
    switch (l) {
    case 'B': case 'P': dz_note(l, s[0], dz_mix(s[1], 4), 0); return s[0];
    case 'N': dz_note('N', s[0], s[1], 0); return 0x00777000u + dz_answer(0x100u);
    case 'D': dz_note('D', s[0], s[1], 0); return 0x00778000u + dz_answer(0x100u);
    case 'L': dz_note('L', s[0], 0, 0); return dz_w.bset;
    case 'c': dz_note('c', s[0], s[1], s[2]); return s[0];
    case 'p': dz_note('p', s[0], dz_mix(s[1], 2) ^ s[3], dz_mix(s[2], 4)); return 0;
    case 'b': dz_note('b', s[0], s[1], dz_mix(s[2], 2)); return 0;
    case 'S': dz_note('S', s[0], s[1], s[2] | s[3] << 16); return 0;
    case 'w': dz_note('w', s[0], s[1], 0); return dz_answer(200u);
    case 'z': dz_note('z', s[0], 0, 0); return 0x10u + dz_answer(0x80u);
    case 'n': dz_note('n', s[0], s[1], 0); return dz_answer(5u);
    case 'e': dz_note('e', s[0], 0, 0); return dz_answer(4u) ? 1u : 0u;
    case 't': dz_note('t', s[0], s[1], 0); return 0x00779000u + s[1];
    case 'V': dz_note('V', 0, 0, 0); return 0;                      /* the view record lies in a side's frame */
    case 'v': dz_note('v', s[1], 0, 0); return 0;
    case 'C': dz_note('C', s[0], s[1], s[2] ^ s[3] << 8); return 0;
    case 'E': {
        uint32_t e = dz_answer(4u) ? dz_w.entry : 0u;
        dz_note('E', e, 0, 0);
        if (!dz_side) ++*(!e ? &dz_entries_null : dz_get(e + 0x14u) == 4u ? &dz_entries_4 : &dz_entries_other);
        return e;
    }
    case 'Q': dz_note('Q', s[0], s[1], 0); dz_phase = 1; return 0;
    case 'Y': {
        dz_frame f = {0, 0xffffffffu};
        if (dz_script_i < dz_script_n) f = dz_script[dz_script_i];
        else if (dz_phase && !dz_seen) f.busy = 1;
        if (dz_phase && f.busy) dz_seen = 1;
        dz_note('Y', dz_script_i, f.busy, f.word);
        ++dz_script_i;
        uint32_t d = dz_get(0x004cf99cu);
        dz_put8(d + 0x1cu, f.busy);
        dz_put32(d + 0x18u, f.word);
        return 0;
    }
    default: dz_note(l, s[0], s[1], 0); return 0;   /* A T d h x R M U O K J H f m q Z G */
    }
}
/* One stub per letter with its argument count. */
#define DZ_O(L, N) \
    static uint32_t __cdecl dz_o_##L(uint32_t a, uint32_t b, uint32_t c, uint32_t d) { \
        uint32_t s[4] = {a, b, c, d}; \
        for (unsigned i = (N); i < 4u; ++i) s[i] = 0; \
        return dz_stub(#L[0], s); \
    }
DZ_O(B, 2) DZ_O(P, 2) DZ_O(A, 2) DZ_O(T, 2) DZ_O(S, 4) DZ_O(N, 2) DZ_O(D, 2) DZ_O(L, 1) DZ_O(c, 3) DZ_O(p, 4) DZ_O(d, 2)
DZ_O(b, 3) DZ_O(w, 2) DZ_O(z, 1) DZ_O(n, 2) DZ_O(h, 2) DZ_O(x, 2) DZ_O(e, 1) DZ_O(V, 1) DZ_O(v, 2) DZ_O(E, 0) DZ_O(R, 1)
DZ_O(M, 2) DZ_O(Q, 2) DZ_O(J, 0) DZ_O(Y, 0) DZ_O(U, 1) DZ_O(H, 0) DZ_O(O, 1) DZ_O(C, 4) DZ_O(K, 2)
DZ_O(f, 2) DZ_O(t, 2) DZ_O(m, 1) DZ_O(q, 2) DZ_O(Z, 0) DZ_O(G, 1)
#undef DZ_O
typedef struct dz_stub_t { uint32_t va; void *stub; char letter; unsigned slots; } dz_stub_t;
/* The callees of other packages (patched on the original's side). */
static const dz_stub_t dz_patched[] = {
    {0x004524a0u, (void *)dz_o_B, 'B', 2}, {0x004526ecu, (void *)dz_o_P, 'P', 2}, {0x00452c6cu, (void *)dz_o_A, 'A', 2},
    {0x00453878u, (void *)dz_o_T, 'T', 2}, {0x004539a0u, (void *)dz_o_S, 'S', 4}, {0x0045ef1cu, (void *)dz_o_N, 'N', 2},
    {0x00492becu, (void *)dz_o_D, 'D', 2}, {0x0045cdf4u, (void *)dz_o_L, 'L', 1}, {0x004387acu, (void *)dz_o_c, 'c', 3},
    {0x00438884u, (void *)dz_o_p, 'p', 4}, {0x00438920u, (void *)dz_o_d, 'd', 2}, {0x0045c8bcu, (void *)dz_o_b, 'b', 3},
    {0x004824ecu, (void *)dz_o_w, 'w', 2}, {0x00488045u, (void *)dz_o_z, 'z', 1}, {0x00496406u, (void *)dz_o_n, 'n', 2},
    {0x004838fdu, (void *)dz_o_h, 'h', 2}, {0x0048349cu, (void *)dz_o_x, 'x', 2}, {0x00496766u, (void *)dz_o_e, 'e', 1},
    {0x0048bd84u, (void *)dz_o_V, 'V', 1}, {0x0048bcc4u, (void *)dz_o_v, 'v', 2}, {0x00429820u, (void *)dz_o_E, 'E', 0},
    {0x0042da08u, (void *)dz_o_R, 'R', 1}, {0x0042a1f0u, (void *)dz_o_M, 'M', 2}, {0x0042a3f4u, (void *)dz_o_Q, 'Q', 2},
    {0x0045e208u, (void *)dz_o_J, 'J', 0}, {0x0045fba4u, (void *)dz_o_Y, 'Y', 0}, {0x004011a0u, (void *)dz_o_U, 'U', 1},
    {0x0045fca8u, (void *)dz_o_H, 'H', 0}, {0x0045edb0u, (void *)dz_o_O, 'O', 1}, {0x00401c74u, (void *)dz_o_C, 'C', 4},
    {0x0045746cu, (void *)dz_o_K, 'K', 2},
};
#define DZ_PATCHED (sizeof(dz_patched) / sizeof(dz_patched[0]))
/* Method-table and driver slots: their host addresses are the VAs both sides call. */
static const dz_stub_t dz_hosts[] = {
    {0, (void *)dz_o_f, 'f', 2}, {0, (void *)dz_o_t, 't', 2}, {0, (void *)dz_o_m, 'm', 1},
    {0, (void *)dz_o_q, 'q', 2}, {0, (void *)dz_o_Z, 'Z', 0}, {0, (void *)dz_o_G, 'G', 1},
};
#define DZ_HOSTS (sizeof(dz_hosts) / sizeof(dz_hosts[0]))
static uint32_t dz_host(char letter) {
    for (unsigned i = 0; i < DZ_HOSTS; ++i)
        if (dz_hosts[i].letter == letter) return (uint32_t)(uintptr_t)dz_hosts[i].stub;
    return 0;
}
static BYTE dz_saved[DZ_PATCHED][5];
static int dz_patch(uint32_t va, void *target, BYTE saved[5], int install) {
    BYTE *site = mapped + (va - 0x400000u);
    DWORD previous;
    if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &previous)) return fail("dialog ctors: stub protection");
    if (install) { memcpy(saved, site, 5); site[0] = 0xe9; put32(site + 1, (uint32_t)(uintptr_t)target - (uint32_t)(uintptr_t)(site + 5)); }
    else memcpy(site, saved, 5);
    if (!VirtualProtect(site, 5, previous, &previous)) return fail("dialog ctors: stub restore protection");
    FlushInstructionCache(GetCurrentProcess(), site, 5);
    return 0;
}

/* ---- the own side */
static void dz_s_stub(es2_sim_service_call *call, void *user) {
    uintptr_t u = (uintptr_t)user;
    unsigned n = (unsigned)(u >> 8);
    uint32_t s[4] = {0, 0, 0, 0};
    for (unsigned i = 0; i < n && i < 4u; ++i) s[i] = call->slots[i];
    call->eax = dz_stub((char)(u & 0xffu), s);
}
static void dz_refuse(const es2_sim_refusal *r, void *user) {
    (void)user;
    fprintf(stderr, "FAIL: own dialog ctors: refused call to 0x%08lx (%s): %s\n", (unsigned long)r->va, r->name ? r->name : "?",
            r->reason ? r->reason : "");
    dz_bad = 1;
}
/* Before the seal: a VA already in the table (another package's row or a
 * hook) becomes the stub in place, any other is added. */
static int dz_stub_entry(uint32_t va, char letter, unsigned slots) {
    void *user = (void *)(uintptr_t)((unsigned char)letter | slots << 8);
    for (unsigned i = 0; i < dz_dispatch.count; ++i) {
        es2_sim_entry *e = &dz_dispatch.entries[i];
        if (e->va != va) continue;
        e->kind = ES2_SIM_SERVICE;
        e->name = "dialog ctors stub";
        e->descriptor = NULL;
        e->extension = NULL;
        e->service = dz_s_stub;
        e->service_user = user;
        e->pop_bytes = 0;
        e->stack_slots = (uint8_t)slots;
        return 0;
    }
    return es2_sim_dispatch_add_service(&dz_dispatch, va, "dialog ctors stub", dz_s_stub, user, 0, (uint8_t)slots);
}
static int dz_dispatch_setup(void) {
    es2_sim_dispatch_init(&dz_dispatch, dz_entries, 4096, dz_refuse, NULL);
    static es2_sim_services s;
    s.memory = &ir_memory;
    s.heap = &ir_heap;
    s.vt = &lc_vt;
    s.fs0 = NULL;
    if (es2_sim_register_services(&dz_dispatch, &s)) return fail("dialog ctors: dispatch");
    for (unsigned i = 0; i < DZ_PATCHED; ++i)
        if (dz_stub_entry(dz_patched[i].va, dz_patched[i].letter, dz_patched[i].slots)) return fail("dialog ctors: dispatch (stub)");
    for (unsigned i = 0; i < DZ_HOSTS; ++i)
        if (dz_stub_entry((uint32_t)(uintptr_t)dz_hosts[i].stub, dz_hosts[i].letter, dz_hosts[i].slots)) return fail("dialog ctors: dispatch (slot)");
    const char *error = NULL;
    uint32_t bad = 0;
    if (es2_sim_dispatch_seal(&dz_dispatch, &error, &bad)) {
        fprintf(stderr, "seal: %s at 0x%08lx\n", error ? error : "?", (unsigned long)bad);
        return fail("dialog ctors: dispatch seal");
    }
    /* The rows under test must be own rows. */
    static const uint32_t rows[] = {0x004541b8u, 0x004543b0u, 0x00455950u, 0x004561dcu, 0x00457538u, 0x004566e0u, 0x0045cff0u,
                                    0x00453824u, 0x00453848u, 0x00454f14u, 0x00454f50u, 0x00454f80u, 0x00455014u, 0x00456084u,
                                    0x0042d89cu, 0x0042d944u, 0x0045cfc8u, 0x00455924u, 0x00455facu, 0x00456cf0u, 0x004570fcu,
                                    0x00457a9cu, 0x00457b74u};
    for (unsigned i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i) {
        const es2_sim_entry *e = es2_sim_dispatch_find(&dz_dispatch, rows[i]);
        if (!e || e->kind != ES2_SIM_SERVICE || e->service == dz_s_stub) return fail("dialog ctors: an H5-13 row is missing");
    }
    return 0;
}

/* ---- the synthetic world */
static uint32_t dz_alloc(uint32_t n) { return (uint32_t)(uintptr_t)ir_calloc(1, n); }
static uint32_t dz_alloc_random(uint32_t n) {
    uint32_t p = dz_alloc(n);
    for (uint32_t i = 0; i < n; ++i) dz_put8(p + i, (uint8_t)dz_rand(256u));
    return p;
}
static int32_t dz_small(uint32_t n) { return (int32_t)dz_rand(2u * n + 1u) - (int32_t)n; }
static void dz_set8(uint32_t va, uint8_t v) { mapped[va - 0x400000u] = v; }
static void dz_set16(uint32_t va, uint16_t v) { put16(mapped + (va - 0x400000u), v); }
static void dz_set32(uint32_t va, uint32_t v) { put32(mapped + (va - 0x400000u), v); }
static void dz_build(void) {
    dz_world *w = &dz_w;
    memset(w, 0, sizeof(*w));
    w->obj = dz_alloc_random(0x600u);
    w->small = dz_alloc(8u);
    dz_put32(w->small, (uint32_t)dz_small(200u));
    dz_put32(w->small + 4u, (uint32_t)dz_small(200u));
    w->rect = dz_alloc(0x10u);
    {
        int32_t x = dz_small(100u), y = dz_small(100u);
        dz_put32(w->rect, (uint32_t)x);
        dz_put32(w->rect + 4u, (uint32_t)y);
        dz_put32(w->rect + 8u, (uint32_t)(x + (int32_t)dz_rand(400u)));
        dz_put32(w->rect + 0xcu, (uint32_t)(y + (int32_t)dz_rand(300u)));
    }
    w->spare = dz_alloc(0x21u);
    /* The surface, its clip record and pages; the saved context. */
    w->clip = dz_alloc_random(0x240u);
    w->surface = dz_alloc_random(0x100u);
    dz_put32(w->surface + 0x40u, w->clip);
    dz_set32(0x006c5ed0u, w->surface);
    w->gc = dz_alloc_random(0x240u);
    dz_set32(0x006c5f70u, w->gc);
    /* The bitmap set and its images (method table +0x18: the virtual delete). */
    w->ivt = dz_alloc(0x20u);
    dz_put32(w->ivt + 0x18u, dz_host('q'));
    w->images = dz_alloc(4u * DZ_IMAGES);
    for (unsigned i = 0; i < DZ_IMAGES; ++i) {
        w->img[i] = dz_rand(8u) ? dz_alloc_random(0x10u) : 0u;
        if (w->img[i]) dz_put32(w->img[i], w->ivt);
        dz_put32(w->images + 4u * i, w->img[i]);
    }
    w->bset = dz_alloc_random(0x20u);
    dz_put32(w->bset + 8u, w->images);
    dz_set32(0x0049dd88u, w->bset);
    dz_set32(0x004d1efcu, dz_rand(2u) ? w->bset : 0u);
    /* The owner of the row part's buttons. */
    w->ovt = dz_alloc(0x20u);
    dz_put32(w->ovt + 0x14u, dz_host('t'));
    w->owner = dz_alloc(0x10u);
    dz_put32(w->owner, w->ovt);
    /* The display, the view, the camera, the camera entry. */
    w->display = dz_alloc(0x20u);
    dz_put8(w->display + 0x1cu, (uint8_t)dz_rand(2u));
    dz_put32(w->display + 0x18u, dz_rand(2u) ? 0xffffffffu : dz_rand(4u));
    dz_set32(0x004cf99cu, w->display);
    w->view = dz_alloc_random(0x20u);
    dz_put32(w->view + 0x14u, dz_rand(3u) ? 4u : dz_rand(8u));
    w->cam = dz_alloc_random(0x60u);
    dz_put32(w->cam + 0x4au, dz_rand(2u) ? 0u : 1u + dz_rand(9u));
    w->entry = dz_alloc_random(0x20u);
    dz_put32(w->entry + 0x14u, dz_rand(2u) ? 4u : dz_rand(8u));
    w->vrect = dz_alloc_random(0x10u);
    w->origin = dz_alloc_random(8u);
    /* The objective indices. */
    w->indices = dz_alloc(0x40u);
    for (unsigned i = 0; i < 0x20u; ++i) dz_put16(w->indices + 2u * i, (uint16_t)dz_rand(48u));
    static const uint16_t counts[] = {0, 1, 2, 3, 5, 7, 8, 12, 20, 0xffffu};
    dz_set16(0x004a9e44u, counts[dz_rand(10u)]);
    dz_set32(0x004a9e48u, w->indices);
    /* The row part's blocks. */
    for (unsigned i = 0; i < DZ_OPS; ++i) {
        w->hb[i] = dz_alloc_random(0x10u);
        w->hbuf[i] = dz_rand(2u) ? dz_alloc(0x20u) : 0u;
        dz_put32(w->hb[i], w->hbuf[i]);
        w->label[i] = dz_alloc_random(0x40u);
        w->g[i] = dz_alloc_random(0x50u);
        dz_put16(w->g[i] + 0x1bu, (uint16_t)dz_rand(4u));
        dz_put32(w->g[i] + 0x24u, w->owner);
        dz_put32(w->g[i] + 0x2cu, w->label[i]);
    }
    /* DATA: the shifts, screen, fonts, colours, scroll, presents, guards. */
    static const uint16_t shifts[] = {0, 0, 0, 1, 1, 2, 31, 33};
    dz_set16(0x004d253au, shifts[dz_rand(8u)]);
    dz_set16(0x004d253cu, shifts[dz_rand(8u)]);
    dz_set32(0x004d1e70u, dz_rand(2u) ? 640u : 1280u + dz_rand(9u));
    dz_set32(0x004d1e74u, dz_rand(2u) ? 480u : 960u + dz_rand(9u));
    dz_set8(0x004d256cu, (uint8_t)dz_rand(2u));
    static const uint32_t fonts[] = {0x004d1e18u, 0x004d1e1cu, 0x004d1e2cu, 0x004d1e20u, 0x004d1e24u, 0x004d1e28u, 0x004d1e98u};
    for (unsigned i = 0; i < sizeof(fonts) / sizeof(fonts[0]); ++i) dz_set32(fonts[i], 0x00550000u + dz_rand(0x10000u));
    dz_set32(0x004d2556u, (uint32_t)dz_small(30u));
    dz_set32(0x004d255au, (uint32_t)dz_small(30u));
    dz_set16(0x004d2566u, (uint16_t)dz_rand(2u));
    dz_set32(0x0049dbd8u, 0x00556000u + dz_rand(0x100u));
    dz_set16(0x004d3ba2u, (uint16_t)dz_rand(0x10000u));
    static const uint32_t guards[] = {0x0049e04cu, 0x0049e138u, 0x0049e2dau, 0x0049e3fcu, 0x0049ddfcu};
    for (unsigned i = 0; i < sizeof(guards) / sizeof(guards[0]); ++i) dz_set8(guards[i], dz_rand(3u) ? 0u : 1u);
    for (unsigned i = 0; i < 0x18u; i += 4u) dz_set32(0x004cfa28u + i, (uint32_t)dz_small(300u));
    /* The shell record G: the view, the camera, the forced word, the shift and the width. */
    dz_set32(0x004d24bcu + 4u, w->view);
    dz_set32(0x004d24bcu + 0x2au, 0x00557000u + dz_rand(0x100u));
    dz_set32(0x004d24bcu + 0x2eu, w->cam);
    dz_set16(0x004d24bcu + 0x36u, (uint16_t)dz_rand(0x10000u));
    dz_set16(0x004d24bcu + 0x80u, shifts[dz_rand(8u)]);
    dz_set32(0x004d24bcu + 0x82u, 320u << dz_rand(2u));
    dz_set16(0x004d267eu, (uint16_t)dz_rand(3u));
    dz_set8(0x0049b104u, (uint8_t)dz_rand(2u));
    dz_set8(0x0049dbdfu, (uint8_t)dz_rand(2u));
    /* The driver slots and the dialog tables' frame, text and modal slots. */
    dz_set32(0x004a5828u, dz_host('Z'));
    dz_set32(0x004a5824u, dz_host('Z'));
    dz_set32(0x004a57f0u, dz_host('G'));
    static const uint32_t tables[] = {0x0049e120u, 0x0049e1e4u, 0x0049e3e4u, 0x0049e4d0u};
    for (unsigned i = 0; i < 4u; ++i) {
        dz_set32(tables[i] + 4u, dz_host('f'));
        dz_set32(tables[i] + 0x14u, dz_host('t'));
    }
    dz_set32(0x0049e3e4u + 8u, dz_host('m'));
    /* The display script of the session. */
    dz_script_n = dz_rand(6u);
    for (unsigned i = 0; i < dz_script_n; ++i) {
        dz_script[i].busy = (uint8_t)dz_rand(2u);
        dz_script[i].word = dz_rand(2u) ? 0xffffffffu : dz_rand(3u);
    }
}

static int dz_side_begin(unsigned side, uint32_t seed, uint32_t answers) {
    uint32_t clock = 0x01000000u + (seed % 0x100000u) * 16u;
    lc_reset(side, 47, clock, 1);
    lc_loop.dispatch = &dz_dispatch;
    es2_journal_free(&lc_journal[side]);
    es2_journal_init(&lc_journal[side]);
    dz_side = side;
    dz_hash[side] = 0xcbf29ce484222325ull;
    dz_count[side] = 0;
    memset(dz_letters[side], 0, sizeof(dz_letters[side]));
    dz_len[side] = 0;
    dz_text[side][0] = 0;
    dz_answer_seed = answers;
    dz_script_i = dz_phase = dz_seen = 0;
    dz_seed = seed;
    dz_build();
    if (side) {
        if (memcmp(&dz_w, &dz_w0, sizeof(dz_w))) return fail("dialog ctors: the sides' worlds differ (heap order)");
        lc_loop.journal = &lc_journal[1];
        es2_sim_hud_init(&dz_hud, &lc_loop);
        dz_hud.trace = dz_trace;
        dz_hud.trace_capacity = DZ_TRACE;
        es2_sim_hud_bind(&dz_hud);
        es2_sim_dialogs_init(&dz_dialogs);
        dz_dialogs.pass_limit = 400;
        es2_sim_dialogs_bind(&dz_dialogs);
        es2_sim_input_headless(&dz_input, &lc_loop, &lc_journal[1], 640, 480);
        es2_sim_input_bind(&dz_input);
    } else {
        dz_w0 = dz_w;
    }
    return 0;
}
static uint32_t dz_call(uint32_t va, const uint32_t *a, unsigned n, uint32_t *edx) {
    uint32_t d = 0, r;
    if (dz_side) {
        es2_sim_result res;
        uint32_t regs[3] = {0, 0, 0};
        memset(&res, 0, sizeof(res));
        if (es2_sim_call(&dz_dispatch, va, a, n, regs, &res)) dz_bad = 1;
        r = res.eax;
        d = res.edx;
    } else {
        r = lc_machine(va, a, n, &d);
    }
    if (edx) *edx = d;
    return r;
}

/* ---- coverage */
enum { DZC_BASE, DZC_ALERT_BASE, DZC_ALERT_NONE, DZC_ALERT_ONE, DZC_ALERT_TWO, DZC_ALERT_5, DZC_ALERT_2, DZC_PAUSE_0, DZC_PAUSE_1,
       DZC_PAUSE_OTHER, DZC_OPTIONS, DZC_OBJECTIVES, DZC_OBJ_LINES, DZC_SMALL, DZC_CENTRED, DZC_SAVE, DZC_NO_SAVE, DZC_ONCE_FILL,
       DZC_ONCE_KEPT, DZC_DRAW, DZC_DTOR_KEEP, DZC_DTOR_DELETE, DZC_OPT_BITMAP, DZC_OPT_NO_BITMAP, DZC_ROW_TB_INIT, DZC_ROW_TB_FREE, DZC_ROW_TB_NULL,
       DZC_ROW_TB_BUFFER, DZC_ROW_RECT, DZC_ROW_CENTRED, DZC_ROW_BINIT, DZC_ROW_BDRAW, DZC_ROW_PRESENT_ON, DZC_ROW_VSET,
       DZC_ROW_VRESET, DZC_ROW_IDLE, DZC_SESSION_4, DZC_SESSION_WAIT, DZC_SESSION_FRAMES, DZC_ENTRY_NULL, DZC_ENTRY_4,
       DZC_ENTRY_OTHER, DZC_COUNT };
static const char *const dz_cv_names[DZC_COUNT] = {
    "base", "alert base", "alert no button", "alert one button", "alert two buttons", "alert status 5", "alert status 2",
    "pause 0", "pause 1", "pause other", "options", "objectives", "objective lines", "at position", "centred", "save buffer",
    "no save buffer", "once fill", "once kept", "draw", "dtor keep", "dtor delete", "options bitmap kept", "options bitmap cleared", "row text init",
    "row text free", "row text null", "row text buffer", "row rect", "row centred", "row button init", "row button draw",
    "row presents on", "row view set", "row view reset", "row idle", "session mode 4", "session wait", "session frames",
    "entry null", "entry kind 4", "entry other",
};
static unsigned dz_cv[DZC_COUNT];

/* ---- dialogs: a constructor, its draw, its destructor */
enum { DZK_BASE, DZK_ALERT_BASE, DZK_ALERT, DZK_PAUSE, DZK_OPTIONS, DZK_OBJECTIVES, DZK_KINDS };
static const struct { uint32_t ctor, draw, dtor, guard; const char *name; } dz_kinds[DZK_KINDS] = {
    {0x004541b8u, 0, 0x004543b0u, 0, "base"},
    {0x00455924u, 0, 0x004543b0u, 0, "alert base"},
    {0x00455950u, 0x00456084u, 0x00455facu, 0x0049e04cu, "alert"},
    {0x004561dcu, 0x00456084u, 0x004543b0u, 0x0049e138u, "pause"},
    {0x004566e0u, 0x004570fcu, 0x00456cf0u, 0x0049e2dau, "options"},
    {0x00457538u, 0x00457b74u, 0x00457a9cu, 0x0049e3fcu, "objectives"},
};
static unsigned dz_kind_calls[DZK_KINDS];
static const uint8_t dz_alert_class[20] = {0, 0, 2, 2, 1, 1, 1, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 1};
static int dz_dialog_case(unsigned k) {
    unsigned kind = random32() % DZK_KINDS;
    uint32_t seed = random32(), answers = random32();
    uint32_t status = random32() % 4u ? random32() % 0x16u : random32() % 3u, save = random32() % 3u == 0 ? 1u + random32() % 0xffu : 0x100u * (random32() % 2u);
    int at = random32() % 2u, draw = random32() % 5u != 0, flags = (int)(random32() % 4u), drop = random32() % 3u == 0;
    uint32_t eax[2] = {0, 0};
    for (unsigned side = 0; side < 2u; ++side) {
        if (dz_side_begin(side, seed, answers)) return 1;
        uint32_t o = dz_w.obj, small = at ? dz_w.small : 0u, a[4];
        uint8_t guard = dz_kinds[kind].guard ? dz_get8(dz_kinds[kind].guard) : 0u;
        switch (kind) {
        case DZK_BASE: a[0] = o; a[1] = save; a[2] = dz_w.rect; eax[side] = dz_call(0x004541b8u, a, 3, NULL); break;
        case DZK_ALERT_BASE: a[0] = o; a[1] = status; a[2] = save; a[3] = dz_w.rect; eax[side] = dz_call(0x00455924u, a, 4, NULL); break;
        case DZK_ALERT: case DZK_PAUSE: a[0] = o; a[1] = status; a[2] = save; a[3] = small; eax[side] = dz_call(dz_kinds[kind].ctor, a, 4, NULL); break;
        case DZK_OPTIONS: a[0] = o; a[1] = save; eax[side] = dz_call(0x004566e0u, a, 2, NULL); break;
        default: a[0] = o; a[1] = save; a[2] = small; eax[side] = dz_call(0x00457538u, a, 3, NULL); break;
        }
        if (dz_bad || lc_bad) { fprintf(stderr, "FAIL: dialog ctors %s case %u: the constructor failed (side %u)\n", dz_kinds[kind].name, k, side); return 1; }
        if (kind <= DZK_ALERT_BASE) dz_put32(o + 0x44u, dz_w.spare);   /* the base sets no title label */
        if (draw && dz_kinds[kind].draw) { a[0] = o; dz_call(dz_kinds[kind].draw, a, 1, NULL); }
        /* The constructor keeps its bitmap in [0x4d1efc]; cleared here for the destructor's other branch. */
        if (kind == DZK_OPTIONS && drop) dz_put32(0x004d1efcu, 0u);
        int kept = kind == DZK_OPTIONS && dz_get(0x004d1efcu) != 0u;
        a[0] = o; a[1] = (uint32_t)flags;
        if (!dz_bad && !lc_bad) dz_call(dz_kinds[kind].dtor, a, 2, NULL);
        if (dz_bad || lc_bad) { fprintf(stderr, "FAIL: dialog ctors %s case %u: a call failed (side %u)\n", dz_kinds[kind].name, k, side); return 1; }
        su_take(side);
        if (side == 0) {
            ++dz_kind_calls[kind];
            static const unsigned names[DZK_KINDS] = {DZC_BASE, DZC_ALERT_BASE, 0, 0, DZC_OPTIONS, DZC_OBJECTIVES};
            if (kind == DZK_ALERT) {
                ++dz_cv[status > 0x13u || !dz_alert_class[status] ? DZC_ALERT_NONE : dz_alert_class[status] == 2 ? DZC_ALERT_ONE : DZC_ALERT_TWO];
                if (status == 5u) ++dz_cv[DZC_ALERT_5];
                if (status == 2u) ++dz_cv[DZC_ALERT_2];
            } else if (kind == DZK_PAUSE) {
                ++dz_cv[status == 0u ? DZC_PAUSE_0 : status == 1u ? DZC_PAUSE_1 : DZC_PAUSE_OTHER];
            } else {
                ++dz_cv[names[kind]];
            }
            if (kind == DZK_OBJECTIVES && (int16_t)get16(mapped + 0xa9e44u) > 0) ++dz_cv[DZC_OBJ_LINES];
            if (kind >= DZK_ALERT && kind != DZK_OPTIONS) ++dz_cv[at ? DZC_SMALL : DZC_CENTRED];
            ++dz_cv[save & 0xffu ? DZC_SAVE : DZC_NO_SAVE];
            if (dz_kinds[kind].guard) ++dz_cv[guard ? DZC_ONCE_KEPT : DZC_ONCE_FILL];
            if (draw && dz_kinds[kind].draw) ++dz_cv[DZC_DRAW];
            ++dz_cv[flags & 1 ? DZC_DTOR_DELETE : DZC_DTOR_KEEP];
            if (kind == DZK_OPTIONS) ++dz_cv[kept ? DZC_OPT_BITMAP : DZC_OPT_NO_BITMAP];
        }
    }
    if (eax[0] != eax[1]) {
        fprintf(stderr, "FAIL: dialog ctors %s case %u: EAX %08lx / %08lx\n", dz_kinds[kind].name, k, (unsigned long)eax[0], (unsigned long)eax[1]);
        return 1;
    }
    if (su_compare("dialog ctors", k)) { fprintf(stderr, "  part dialogs, %s\n", dz_kinds[kind].name); return 1; }
    if (dz_hash[0] != dz_hash[1] || dz_count[0] != dz_count[1]) {
        fprintf(stderr, "FAIL: dialog ctors %s case %u: stub calls differ (%u / %u)\n  original: %s\n  own:      %s\n", dz_kinds[kind].name, k,
                dz_count[0], dz_count[1], dz_text[0], dz_text[1]);
        return 1;
    }
    return 0;
}

/* ---- rows */
enum { DZR_TB_INIT, DZR_TB_FREE, DZR_RECT, DZR_CENTRED, DZR_BINIT, DZR_BDRAW, DZR_VSET, DZR_VRESET, DZR_IDLE, DZR_OPS };
static const struct { uint32_t va; unsigned n; uint32_t mask; int edx; const char *name; } dz_rows[DZR_OPS] = {
    {0x00453824u, 1, ~0u, 0, "text init"}, {0x00453848u, 2, 0u, 0, "text free"}, {0x00454f14u, 4, ~0u, 1, "rect"},
    {0x00454f50u, 2, ~0u, 1, "centred"}, {0x00454f80u, 4, ~0u, 0, "button init"}, {0x00455014u, 1, 0u, 0, "button draw"},
    {0x0042d89cu, 2, ~0u, 0, "view set"}, {0x0042d944u, 0, ~0u, 0, "view reset"}, {0x0045cfc8u, 0, 0u, 0, "idle"},
};
static unsigned dz_row_calls[DZR_OPS];
static int dz_rows_case(unsigned k) {
    unsigned ops = 1u + random32() % DZ_OPS, list[DZ_OPS];
    uint32_t args[DZ_OPS][4], seed = random32(), answers = random32();
    static uint32_t eax[2][DZ_OPS], edx[2][DZ_OPS];
    for (unsigned i = 0; i < ops; ++i) {
        list[i] = random32() % DZR_OPS;
        for (unsigned j = 0; j < 4u; ++j) args[i][j] = random32() % 2u ? random32() % 0x200u : random32();
    }
    for (unsigned side = 0; side < 2u; ++side) {
        if (dz_side_begin(side, seed, answers)) return 1;
        for (unsigned i = 0; i < ops && !dz_bad && !lc_bad; ++i) {
            uint32_t a[4] = {args[i][0], args[i][1], args[i][2], args[i][3]};
            switch (list[i]) {
            case DZR_TB_INIT: a[0] = dz_w.hb[i]; break;
            case DZR_TB_FREE: a[0] = args[i][0] % 6u ? dz_w.hb[i] : 0u; a[1] &= 3u; break;
            case DZR_BINIT: a[0] = dz_w.g[i]; a[1] = dz_w.rect; a[2] = dz_w.owner; break;
            case DZR_BDRAW: a[0] = dz_w.g[i]; break;
            case DZR_VSET: a[0] = dz_w.vrect; a[1] = dz_w.origin; break;
            default: break;
            }
            if (side == 0) {
                ++dz_row_calls[list[i]];
                static const unsigned names[DZR_OPS] = {DZC_ROW_TB_INIT, DZC_ROW_TB_FREE, DZC_ROW_RECT, DZC_ROW_CENTRED, DZC_ROW_BINIT,
                                                        DZC_ROW_BDRAW, DZC_ROW_VSET, DZC_ROW_VRESET, DZC_ROW_IDLE};
                ++dz_cv[names[list[i]]];
                if (list[i] == DZR_TB_FREE) ++dz_cv[!a[0] ? DZC_ROW_TB_NULL : dz_get(a[0]) ? DZC_ROW_TB_BUFFER : DZC_ROW_TB_FREE];
                if (list[i] == DZR_BDRAW && get16(mapped + 0xd2566u)) ++dz_cv[DZC_ROW_PRESENT_ON];
            }
            uint32_t d = 0, r = dz_call(dz_rows[list[i]].va, a, dz_rows[list[i]].n, &d);
            eax[side][i] = r & dz_rows[list[i]].mask;
            edx[side][i] = dz_rows[list[i]].edx ? d : 0u;
            /* A freed text block is not freed again by a later op. */
            if (list[i] == DZR_TB_FREE && a[0] && (a[1] & 1u)) { /* the block itself is gone */ }
        }
        if (dz_bad || lc_bad) { fprintf(stderr, "FAIL: dialog ctors rows case %u: a call failed (side %u)\n", k, side); return 1; }
        su_take(side);
    }
    for (unsigned i = 0; i < ops; ++i)
        if (eax[0][i] != eax[1][i] || edx[0][i] != edx[1][i]) {
            fprintf(stderr, "FAIL: dialog ctors rows case %u: %s (op %u) EAX %08lx / %08lx, EDX %08lx / %08lx\n", k, dz_rows[list[i]].name, i,
                    (unsigned long)eax[0][i], (unsigned long)eax[1][i], (unsigned long)edx[0][i], (unsigned long)edx[1][i]);
            return 1;
        }
    if (su_compare("dialog ctors", k)) {
        fprintf(stderr, "  part rows:");
        for (unsigned i = 0; i < ops; ++i) fprintf(stderr, " %s", dz_rows[list[i]].name);
        fprintf(stderr, "\n");
        return 1;
    }
    if (dz_hash[0] != dz_hash[1] || dz_count[0] != dz_count[1]) {
        fprintf(stderr, "FAIL: dialog ctors rows case %u: stub calls differ (%u / %u)\n  original: %s\n  own:      %s\n", k, dz_count[0], dz_count[1],
                dz_text[0], dz_text[1]);
        return 1;
    }
    return 0;
}

/* ---- the session */
static int dz_session_case(unsigned k) {
    uint32_t seed = random32(), answers = random32();
    for (unsigned side = 0; side < 2u; ++side) {
        if (dz_side_begin(side, seed, answers)) return 1;
        uint32_t mode = dz_get(dz_w.view + 0x14u);
        uint8_t busy = dz_get8(dz_w.display + 0x1cu);
        uint32_t word = dz_get(dz_w.display + 0x18u);
        dz_call(0x0045cff0u, NULL, 0, NULL);
        if (dz_bad || lc_bad) { fprintf(stderr, "FAIL: dialog ctors session case %u: the call failed (side %u)\n", k, side); return 1; }
        su_take(side);
        if (side == 0) {
            if (mode == 4u) ++dz_cv[DZC_SESSION_4];
            else {
                ++dz_cv[DZC_SESSION_FRAMES];
                if (busy || word != 0xffffffffu) ++dz_cv[DZC_SESSION_WAIT];
            }
        }
    }
    if (su_compare("dialog ctors", k)) { fprintf(stderr, "  part session\n"); return 1; }
    if (dz_hash[0] != dz_hash[1] || dz_count[0] != dz_count[1]) {
        fprintf(stderr, "FAIL: dialog ctors session case %u: stub calls differ (%u / %u)\n  original: %s\n  own:      %s\n", k, dz_count[0],
                dz_count[1], dz_text[0], dz_text[1]);
        return 1;
    }
    return 0;
}

static int dialog_ctors_cases(void) {
    current_function = "dialog ctors";
    es2_sim_identity_memory(&ir_memory);
    es2_sim_rtl_memory(&ir_memory, &su_rtl);
    su_data_pristine = malloc(SU_DATA_SIZE);
    su_snap[0] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    su_snap[1] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    ir_region = ww_alloc(IR_REGION_SIZE);
    lc_stack = VirtualAlloc(NULL, 0x10000u, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    lc_bits = ww_alloc(LC_BITS_SIZE);
    if (!su_data_pristine || !su_snap[0] || !su_snap[1] || !ir_region || !lc_stack || !lc_bits) return fail("dialog ctors: memory");
    if (get32(mapped + (SU_COUNTER - 0x400000u))) return fail("dialog ctors: the counter word is not free");
    su_pristine_take();
    static const struct { const char *dll, *name; void *stub; } imports[] = {
        {"cw3220.dll", "_calloc", (void *)ir_calloc}, {"cw3220.dll", "_free", (void *)ir_free},
        {"KERNEL32.dll", "GetTickCount", (void *)lc_tick},
    };
    for (unsigned i = 0; i < sizeof(imports) / sizeof(imports[0]); ++i)
        if (!ir_patch_import(imports[i].dll, imports[i].name, imports[i].stub)) return fail("dialog ctors: import slot not found");
    for (unsigned i = 0; i < DZ_PATCHED; ++i)
        if (dz_patch(dz_patched[i].va, dz_patched[i].stub, dz_saved[i], 1)) return 1;
    if (dz_dispatch_setup()) return 1;
    int bad = 0;
    static const struct { const char *name; int (*run)(unsigned); unsigned cases; } parts[] = {
        {"dialogs", dz_dialog_case, 1500}, {"rows", dz_rows_case, 1500}, {"session", dz_session_case, 750},
    };
    for (unsigned p = 0; !bad && p < sizeof(parts) / sizeof(parts[0]); ++p) {
        for (unsigned k = 0; !bad && k < parts[p].cases; ++k) {
            current_case = k; current_case_valid = 1;
            bad = parts[p].run(k);
        }
        if (!bad) printf("PASS: dialog ctors %s: %u cases identical\n", parts[p].name, parts[p].cases);
    }
    if (!bad) {
        dz_cv[DZC_ENTRY_NULL] = dz_entries_null;
        dz_cv[DZC_ENTRY_4] = dz_entries_4;
        dz_cv[DZC_ENTRY_OTHER] = dz_entries_other;
        printf("DIALOG CTORS kinds:");
        for (unsigned i = 0; i < DZK_KINDS; ++i) printf(" %s %u%s", dz_kinds[i].name, dz_kind_calls[i], i + 1u < DZK_KINDS ? "," : "\n");
        printf("DIALOG CTORS rows called:");
        for (unsigned i = 0; i < DZR_OPS; ++i) printf(" %s %u%s", dz_rows[i].name, dz_row_calls[i], i + 1u < DZR_OPS ? "," : "\n");
        printf("DIALOG CTORS coverage:");
        for (unsigned i = 0; i < DZC_COUNT; ++i) printf(" %s %u%s", dz_cv_names[i], dz_cv[i], i + 1u < DZC_COUNT ? "," : "\n");
        for (unsigned i = 0; i < DZC_COUNT; ++i)
            if (!dz_cv[i]) { fprintf(stderr, "FAIL: dialog ctors: path '%s' never taken\n", dz_cv_names[i]); bad = 1; }
    }
    current_case_valid = 0;
    for (unsigned i = 0; i < DZ_PATCHED; ++i) dz_patch(dz_patched[i].va, NULL, dz_saved[i], 0);
    put32(mapped + 0x2cbee4u, 0);
    memcpy(mapped + SU_DATA_RVA, su_data_pristine, SU_DATA_SIZE);
    return bad;
}
