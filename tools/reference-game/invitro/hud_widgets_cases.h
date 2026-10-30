/* The HUD message queue, the ports and the gauge blink, original machine code
 * against sim/src/hud_widgets.c (--hud-widgets). It reuses startup_cases.h's
 * snapshots (su_reset, su_take, su_compare), loop_cases.h's virtual time and
 * loop (lc_reset, lc_tick, lc_machine) and input_layer_cases.h's pass-through
 * (ic_original).
 *
 * Both sides start from the pristine DATA and an empty heap and build the same
 * synthetic world from the same seeded generator (a port with a queue of up to
 * ten entries, the record and descriptor rows of the receives in DATA, the
 * stream-B state 0x4d260b, a gauge, the surface, the driver slots), so every
 * address is the same on both sides. Then a random sequence of 1..8 of the 19
 * rows runs on it. The other packages' callees are stubs that record letters
 * and values: on the original side host functions jumped to from the original
 * entries (0x433134, 0x453a10, 0x453a4c, 0x4311e0, 0x431210, 0x488143,
 * 0x487e60, 0x429820, 0x471f24, 0x471de0, 0x471e10, 0x471e88), on the own side
 * services at the same VAs (the rows among them replaced after the seal). The
 * vtable, callback and driver slots hold host stubs on both sides. The clock
 * 0x467828 runs as the original's code against the own clock row over the same
 * virtual time; stream B 0x492ee0 runs as the original's code on both sides.
 * Compared: DATA 0x497000..0x6cc000 and the heap, the stub log, EAX by mask,
 * and the own clock and stream-B sites against hud.md, "The coupling sites of
 * the HUD line". Coverage: per-row path counters from the world before and
 * after each call; every counter must be non-zero at the end. */
#pragma once
/* The headers this suite builds on (sim_main.c). */
#include "init_records_cases.h"
#include "startup_cases.h"
#include "loop_cases.h"
#include "input_layer_cases.h"
#include "es2/sim/hud.h"

#define WG_TRACE 4096u
static es2_sim_entry wg_entries[4096];
static es2_sim_dispatch wg_dispatch;
static es2_sim_hud wg_hud;
static es2_sim_hud_event wg_trace[WG_TRACE];
static int wg_bad;
static unsigned wg_side;

/* ---- the stub log of a side */
static uint64_t wg_hash[2];
static unsigned wg_count[2];
static char wg_text[2][16384];
static size_t wg_len[2];
static void wg_note(char letter, uint32_t a, uint32_t b, uint32_t c) {
    uint64_t h = wg_hash[wg_side];
    const uint32_t v[4] = {(uint32_t)letter, a, b, c};
    for (unsigned i = 0; i < 4u; ++i) { h ^= v[i]; h *= 0x100000001b3ull; }
    wg_hash[wg_side] = h;
    ++wg_count[wg_side];
    if (wg_len[wg_side] + 48u < sizeof(wg_text[0]))
        wg_len[wg_side] += (size_t)sprintf(wg_text[wg_side] + wg_len[wg_side], "%c(%lx,%lx,%lx) ", letter, (unsigned long)a,
                                           (unsigned long)b, (unsigned long)c);
}
static uint32_t wg_get(uint32_t a) { return get32((BYTE *)(uintptr_t)a); }
static uint8_t wg_get8(uint32_t a) { return *(BYTE *)(uintptr_t)a; }
static uint16_t wg_get16(uint32_t a) { return (uint16_t)(wg_get8(a) | wg_get8(a + 1u) << 8); }
static void wg_put8(uint32_t a, uint8_t v) { *(BYTE *)(uintptr_t)a = v; }
static void wg_put16(uint32_t a, uint16_t v) { put16((BYTE *)(uintptr_t)a, v); }
static void wg_put32(uint32_t a, uint32_t v) { put32((BYTE *)(uintptr_t)a, v); }

/* ---- the stubs' answers: a generator reset per side (the calls come in
 * the same order on both sides when the rows agree) */
static uint32_t wg_answer_seed;
static uint32_t wg_answer(uint32_t n) {
    wg_answer_seed = wg_answer_seed * 1103515245u + 12345u;
    return (wg_answer_seed >> 8) % n;
}
#define WG_ROWS 4u
static uint32_t wg_row[WG_ROWS], wg_display;
static uint32_t wg_phrase(uint32_t t, uint32_t v) { wg_note('N', t, v, 0); return wg_answer(4u) ? 0x1000u + wg_answer(0x100u) : 0u; }
static uint32_t wg_phrase_row(uint32_t t, uint32_t v) { wg_note('R', t, v, 0); return wg_answer(6u) ? wg_row[wg_answer(WG_ROWS)] : 0u; }
static uint32_t wg_wingman(uint32_t key) { wg_note('W', key, 0, 0); return 0xabcdef00u | key % 3u; }
static uint32_t wg_busy(uint32_t v) { wg_note('B', v, 0, 0); return wg_answer(2u) ? 0u : 0x100u * wg_answer(3u) + 1u; }
static uint32_t wg_step(uint32_t v, uint32_t x) { wg_note('P', v, x, 0); return 0x5500u | wg_answer(2u); }
/* The graphics, sound and path stand-ins. Arguments that point into a side's
 * own frame (the name blocks, the text point, the copied box, the fill records
 * through GC+0x22c) are logged by their contents. */
static uint32_t wg_pext;
static uint32_t wg_strhash(uint32_t a, unsigned max) {
    uint32_t h = 0x811c9dc5u;
    for (unsigned i = 0; i < max; ++i) { uint8_t b = wg_get8(a + i); h = (h ^ b) * 0x01000193u; if (!b) break; }
    return h;
}
static uint32_t wg_mix4(uint32_t a) {
    uint32_t h = 0x811c9dc5u;
    for (unsigned i = 0; i < 4u; ++i) h = (h ^ wg_get(a + 4u * i)) * 0x01000193u;
    return h;
}
static uint32_t wg_gc_now(void) { return get32(mapped + 0x2c5f70u); }
static uint32_t wg_width(uint32_t font, uint32_t text) { wg_note('w', font, text, wg_strhash(text, 0x60u)); return wg_answer(3u) ? wg_answer(200u) : 0xfffffff0u + wg_answer(16u); }
static void wg_string(uint32_t font, uint32_t pt, uint32_t text) {
    uint32_t gc = wg_gc_now();
    wg_note('x', font ^ text, wg_get(pt), wg_get(pt + 4u));
    wg_note('x', wg_get(gc + 0x210u), wg_get(gc + 0x218u), wg_get(gc + 0x220u));
}
static void wg_save(uint32_t a, uint32_t pixels) { wg_note('v', a, pixels, wg_mix4(a + 4u)); }
static void wg_fill(uint32_t r) {
    uint32_t record = wg_get(wg_gc_now() + 0x22cu);
    wg_note('f', wg_mix4(r), wg_get(record), wg_get(record + 4u));
}
static uint32_t wg_colour(uint32_t i) { wg_note('k', i, 0, 0); return wg_answer(256u); }
static uint32_t wg_visible(uint32_t r) { wg_note('q', wg_mix4(r), 0, 0); return 0x5500u | (wg_answer(4u) != 0); }
static uint32_t wg_voice(uint32_t path, uint32_t volume) { wg_note('O', path, volume, wg_strhash(path, 0x80u)); return 0; }
static uint32_t wg_path(uint32_t name, uint32_t ext) { wg_note('Y', wg_strhash(name, 40u), ext, 0); return wg_pext; }

/* ---- the original side's stubs */
static uint32_t __cdecl wg_o_wingman(uint32_t k) { return wg_wingman(k); }
static uint32_t __cdecl wg_o_phrase(uint32_t t, uint32_t v) { return wg_phrase(t, v); }
static uint32_t __cdecl wg_o_phrase_row(uint32_t t, uint32_t v) { return wg_phrase_row(t, v); }
static uint32_t __cdecl wg_o_push(void) { wg_note('G', 0, 0, 0); return wg_gc_now(); }
static uint32_t __cdecl wg_o_width(uint32_t f, uint32_t t) { return wg_width(f, t); }
static uint32_t __cdecl wg_o_string(uint32_t f, uint32_t p, uint32_t t) { wg_string(f, p, t); return 0; }
static uint32_t __cdecl wg_o_save(uint32_t a, uint32_t b) { wg_save(a, b); return 0; }
static uint32_t __cdecl wg_o_fill(uint32_t r) { wg_fill(r); return 0; }
static uint32_t __cdecl wg_o_colour(uint32_t i) { return wg_colour(i); }
static uint32_t __cdecl wg_o_visible(uint32_t r) { return wg_visible(r); }
static uint32_t __cdecl wg_o_sfx(uint32_t id) { wg_note('X', id, 0, 0); return 0; }
static uint32_t __cdecl wg_o_voice(uint32_t p, uint32_t v) { return wg_voice(p, v); }
static uint32_t __cdecl wg_o_path(uint32_t n, uint32_t e) { return wg_path(n, e); }
static uint32_t __cdecl wg_slot_text(uint32_t p, uint32_t e) { wg_note('t', p, e, 0); return 0; }
static uint32_t __cdecl wg_o_pop(void) { wg_note('g', 0, 0, 0); return 0; }
static uint32_t __cdecl wg_o_restore(uint32_t a, uint32_t b, uint32_t c) { wg_note('T', a, b, c); return 0; }
static uint32_t __cdecl wg_o_scroll(uint32_t a, uint32_t b) { wg_note('E', a, wg_get(b), wg_get(b + 4u)); return 0; }
static uint32_t __cdecl wg_o_display(void) { wg_note('D', 0, 0, 0); return wg_display; }
static uint32_t __cdecl wg_o_busy(uint32_t v) { return wg_busy(v); }
static uint32_t __cdecl wg_o_select(uint32_t v, uint32_t w) { wg_note('S', v, w, 0); return 0; }
static uint32_t __cdecl wg_o_start(uint32_t v, uint32_t p) { wg_note('V', v, p, 0); return 0; }
static uint32_t __cdecl wg_o_step(uint32_t v, uint32_t x) { return wg_step(v, x); }
/* The slot stubs: their host addresses are the VAs both sides call. */
static uint32_t __cdecl wg_slot_ask(uint32_t p) { wg_note('a', p, 0, 0); return 0x7700u | wg_answer(2u); }
static uint32_t __cdecl wg_slot_end(uint32_t p) { wg_note('c', p, 0, 0); return 0x6600u | wg_answer(2u); }
static uint32_t __cdecl wg_slot_draw(uint32_t p) { wg_note('d', p, 0, 0); return 0; }
static uint32_t __cdecl wg_cb_start(uint32_t e) { wg_note('s', e, 0, 0); return 0; }
static uint32_t __cdecl wg_cb_hide(uint32_t e) { wg_note('h', e, 0, 0); return 0; }
static uint32_t __cdecl wg_driver_page(uint32_t p) { wg_note('y', p, 0, 0); return 0; }
static uint32_t __cdecl wg_driver_sync(void) { wg_note('z', 0, 0, 0); return 0; }
static int wg_patch(uint32_t va, void *target, BYTE saved[5], int install) {
    BYTE *site = mapped + (va - 0x400000u);
    DWORD previous;
    if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &previous)) return fail("hud widgets: stub protection");
    if (install) { memcpy(saved, site, 5); site[0] = 0xe9; put32(site + 1, (uint32_t)(uintptr_t)target - (uint32_t)(uintptr_t)(site + 5)); }
    else memcpy(site, saved, 5);
    if (!VirtualProtect(site, 5, previous, &previous)) return fail("hud widgets: stub restore protection");
    FlushInstructionCache(GetCurrentProcess(), site, 5);
    return 0;
}
static const struct { uint32_t va; void *stub; char letter; unsigned slots; int row; } wg_patched[] = {
    {0x00433134u, (void *)wg_o_wingman, 'W', 1, 0}, {0x00453a10u, (void *)wg_o_phrase, 'N', 2, 1},
    {0x00453a4cu, (void *)wg_o_phrase_row, 'R', 2, 1}, {0x004311e0u, (void *)wg_o_push, 'G', 0, 1},
    {0x00431210u, (void *)wg_o_pop, 'g', 0, 1}, {0x00488143u, (void *)wg_o_restore, 'T', 3, 1},
    {0x00487e60u, (void *)wg_o_scroll, 'E', 2, 1}, {0x00429820u, (void *)wg_o_display, 'D', 0, 1},
    {0x00471f24u, (void *)wg_o_busy, 'B', 1, 1}, {0x00471de0u, (void *)wg_o_select, 'S', 2, 1},
    {0x00471e10u, (void *)wg_o_start, 'V', 2, 1}, {0x00471e88u, (void *)wg_o_step, 'P', 2, 1},
    {0x004824ecu, (void *)wg_o_width, 'w', 2, 1}, {0x00482634u, (void *)wg_o_string, 'x', 3, 1},
    {0x00488012u, (void *)wg_o_save, 'v', 2, 1}, {0x00486704u, (void *)wg_o_fill, 'f', 1, 1},
    {0x00434280u, (void *)wg_o_colour, 'k', 1, 1}, {0x00431410u, (void *)wg_o_visible, 'q', 1, 0},
    {0x00462830u, (void *)wg_o_sfx, 'X', 1, 0}, {0x00462940u, (void *)wg_o_voice, 'O', 2, 1},
    {0x00492b90u, (void *)wg_o_path, 'Y', 2, 1},
};
#define WG_PATCHED (sizeof(wg_patched) / sizeof(wg_patched[0]))
static BYTE wg_saved[WG_PATCHED][5];

/* ---- the own side */
static void wg_s_stub(es2_sim_service_call *call, void *user) {
    char letter = (char)(uintptr_t)user;
    const uint32_t *s = call->slots;
    uint32_t eax = 0;
    switch (letter) {
    case 'W': eax = wg_wingman(s[0]); break;
    case 'N': eax = wg_phrase(s[0], s[1]); break;
    case 'R': eax = wg_phrase_row(s[0], s[1]); break;
    case 'B': eax = wg_busy(s[0]); break;
    case 'P': eax = wg_step(s[0], s[1]); break;
    case 'G': case 'g': case 'D': case 'z':
        wg_note(letter, 0, 0, 0);
        if (letter == 'D') eax = wg_display;
        if (letter == 'G') eax = wg_gc_now();
        break;
    case 'w': eax = wg_width(s[0], s[1]); break;
    case 'x': wg_string(s[0], s[1], s[2]); break;
    case 'v': wg_save(s[0], s[1]); break;
    case 'f': wg_fill(s[0]); break;
    case 'k': eax = wg_colour(s[0]); break;
    case 'q': eax = wg_visible(s[0]); break;
    case 'X': wg_note('X', s[0], 0, 0); break;
    case 'O': eax = wg_voice(s[0], s[1]); break;
    case 'Y': eax = wg_path(s[0], s[1]); break;
    case 't': wg_note('t', s[0], s[1], 0); break;
    case 'T': wg_note('T', s[0], s[1], s[2]); break;
    case 'E': wg_note('E', s[0], wg_get(s[1]), wg_get(s[1] + 4u)); break;
    case 'S': case 'V': wg_note(letter, s[0], s[1], 0); break;
    case 'a': wg_note('a', s[0], 0, 0); eax = 0x7700u | wg_answer(2u); break;
    case 'c': wg_note('c', s[0], 0, 0); eax = 0x6600u | wg_answer(2u); break;
    default: wg_note(letter, s[0], 0, 0); break;   /* d s h y */
    }
    call->eax = eax;
}
static void wg_refuse(const es2_sim_refusal *r, void *user) {
    (void)user;
    fprintf(stderr, "FAIL: own hud widgets: refused call to 0x%08lx (%s): %s\n", (unsigned long)r->va, r->name ? r->name : "?",
            r->reason ? r->reason : "");
    wg_bad = 1;
}
/* The rows behind the stubs (saved at the seal). The phrase table and voice
 * select ops (and the CommandPort's text, which walks the phrases) run them for
 * real on both sides: the original's entry bytes restored, the own entry's row
 * put back, for that one call. */
static es2_sim_service_fn wg_real_fn[WG_PATCHED];
static void *wg_real_user[WG_PATCHED];
static int wg_real(uint32_t va, unsigned side, int on) {
    for (unsigned i = 0; i < WG_PATCHED; ++i) {
        if (wg_patched[i].va != va) continue;
        if (!side) return wg_patch(va, on ? NULL : wg_patched[i].stub, wg_saved[i], !on);
        es2_sim_entry *e = (es2_sim_entry *)es2_sim_dispatch_find(&wg_dispatch, va);
        if (!e || !wg_real_fn[i]) return fail("hud widgets: no real row");
        e->service = on ? wg_real_fn[i] : wg_s_stub;
        e->service_user = on ? wg_real_user[i] : (void *)(uintptr_t)wg_patched[i].letter;
        return 0;
    }
    return fail("hud widgets: not a patched entry");
}
static int wg_dispatch_setup(void) {
    es2_sim_dispatch_init(&wg_dispatch, wg_entries, 4096, wg_refuse, NULL);
    static es2_sim_services s;
    s.memory = &ir_memory;
    s.heap = &ir_heap;
    s.vt = &lc_vt;
    s.fs0 = NULL;
    if (es2_sim_register_services(&wg_dispatch, &s)) return fail("hud widgets: dispatch");
    for (unsigned i = 0; i < WG_PATCHED; ++i)
        if (!wg_patched[i].row &&
            es2_sim_dispatch_add_service(&wg_dispatch, wg_patched[i].va, "hud widgets stub", wg_s_stub, (void *)(uintptr_t)wg_patched[i].letter,
                                         0, (uint8_t)wg_patched[i].slots))
            return fail("hud widgets: dispatch (stub)");
    const struct { void *host; char letter; unsigned slots; } hosts[] = {
        {(void *)wg_slot_ask, 'a', 1}, {(void *)wg_slot_end, 'c', 1}, {(void *)wg_slot_draw, 'd', 1}, {(void *)wg_cb_start, 's', 1},
        {(void *)wg_cb_hide, 'h', 1}, {(void *)wg_driver_page, 'y', 1}, {(void *)wg_driver_sync, 'z', 0},
        {(void *)wg_slot_text, 't', 2},
    };
    for (unsigned i = 0; i < sizeof(hosts) / sizeof(hosts[0]); ++i)
        if (es2_sim_dispatch_add_service(&wg_dispatch, (uint32_t)(uintptr_t)hosts[i].host, "hud widgets slot", wg_s_stub,
                                         (void *)(uintptr_t)hosts[i].letter, 0, (uint8_t)hosts[i].slots))
            return fail("hud widgets: dispatch (slot)");
    /* Stream B: the original's machine code on both sides. */
    if (es2_sim_dispatch_add_service(&wg_dispatch, 0x00492ee0u, "rng_next (original)", ic_original, (void *)(uintptr_t)1u, 0, 1))
        return fail("hud widgets: dispatch (rng)");
    const char *error = NULL;
    uint32_t bad = 0;
    if (es2_sim_dispatch_seal(&wg_dispatch, &error, &bad)) return fail("hud widgets: dispatch seal");
    /* Rows of other packages that need the game's objects: replaced by the
     * recording stubs after the seal. */
    for (unsigned i = 0; i < WG_PATCHED; ++i) {
        if (!wg_patched[i].row) continue;
        es2_sim_entry *e = (es2_sim_entry *)es2_sim_dispatch_find(&wg_dispatch, wg_patched[i].va);
        if (!e || e->kind != ES2_SIM_SERVICE) return fail("hud widgets: service missing");
        wg_real_fn[i] = e->service;
        wg_real_user[i] = e->service_user;
        e->service = wg_s_stub;
        e->service_user = (void *)(uintptr_t)wg_patched[i].letter;
    }
    return 0;
}

/* ---- the synthetic world */
static uint32_t wg_seed;
static uint32_t wg_rand(uint32_t n) {
    wg_seed = wg_seed * 1103515245u + 12345u;
    return (wg_seed >> 8) % n;
}
typedef struct wg_world {
    uint32_t p, vt, rec[2], desc, gauge, surface, hold, globals, rect, item, now, key[2];
    uint32_t gc, clip, font, msg[2], vname;   /* hud_ports.h */
    uint32_t cp, ptab, prec[9], nrec, phr, cmsg[2], cdarg, pbuf, pv, phdr, vs, vr;   /* hud_command_port.h */
} wg_world;
/* A heap string of 0..max random capitals. */
static uint32_t wg_string_new(unsigned max) {
    uint32_t s = (uint32_t)(uintptr_t)ir_calloc(1, max + 1u);
    unsigned n = wg_rand(max + 1u);
    for (unsigned i = 0; i < n; ++i) wg_put8(s + i, (uint8_t)('A' + wg_rand(26u)));
    return s;
}
/* Codes 0..3 mostly; otherwise 0x36..0x38, one of the event switch's index-4
 * codes (0x436be2 table: 0c 0f 10 14 15), a word above 0x38, or 0..0x3f;
 * 0x36 picks index 2, 0x37/0x38 index 1. */
static uint16_t wg_code(void) {
    static const uint16_t four[5] = {0x0cu, 0x0fu, 0x10u, 0x14u, 0x15u};
    if (wg_rand(3u)) return (uint16_t)wg_rand(4u);
    switch (wg_rand(4u)) {
    case 0: return (uint16_t)(wg_rand(2u) ? 0x36u : 0x37u + wg_rand(2u));
    case 1: return four[wg_rand(5u)];
    case 2: return (uint16_t)(0x39u + wg_rand(0xffc7u));
    default: return (uint16_t)wg_rand(0x40u);
    }
}
static wg_world wg_w, wg_w0;
/* The RTL string imports the port text and voice path reach on the
 * original side; they run the sim's RTL rows, as the own side does. */
static uint32_t wg_ncat;   /* strncat calls of the original side */
static uint32_t __cdecl wg_strncat(uint32_t d, uint32_t s, uint32_t n) { uint32_t a[3] = {d, s, n}; ++wg_ncat; return su_rtl_call(ES2_RTL_STRNCAT, a); }
static uint32_t __cdecl wg_strncpy(uint32_t d, uint32_t s, uint32_t n) { uint32_t a[3] = {d, s, n}; return su_rtl_call(ES2_RTL_STRNCPY, a); }
static uint32_t wg_alloc(uint32_t n) { return (uint32_t)(uintptr_t)ir_calloc(1, n); }
static uint32_t wg_near(uint32_t now) { return now + wg_rand(60u) - 20u; }
static void wg_desc_fill(uint32_t d) {
    wg_put32(d, wg_rand(5u) ? 0x00c00000u + wg_rand(0x1000u) : 0u);
    wg_put8(d + 4u, (uint8_t)(wg_rand(3u) ? wg_rand(4u) : 2u + wg_rand(250u)));
    wg_put32(d + 5u, wg_rand(6u) ? wg_row[wg_rand(WG_ROWS)] : 0u);
}
/* The CommandPort (a copy of the port's queue part with its own current entry,
 * font +0x4df, width +0x4e3, height +0x4e7, line count +0x4eb, save-under
 * pixels +0x4d3/+0x4d7), the phrase table at 0x4d0444 (records: word length n
 * at v-2, n bytes of words and spaces ending in NUL, a byte b, b bytes, then
 * the next record; the bound ends just past the last record's start, so the
 * walk past it answers 0), the rows 0x4d08ed + 9k pointed at it in half of the
 * worlds (a count byte that never walks past the 0), the CommandPort's messages
 * (codes below 16), the CD root 0x4d280c, a path with or without a drive, a
 * free phrase probe (a random length word and byte around pv) and a voice
 * record for 0x471de0. */
static void wg_phrase_text(uint32_t s, unsigned max) {
    unsigned n = 0, shape = wg_rand(6u);
    if (shape == 0) { unsigned k = 0x40u + wg_rand(max - 0x40u); while (n < k) wg_put8(s + n++, (uint8_t)('A' + wg_rand(26u))); }
    else if (shape != 1) {
        unsigned total = wg_rand(max + 1u);
        if (wg_rand(5u) == 0) wg_put8(s + n++, ' ');
        while (n < total) {
            unsigned word = 1u + wg_rand(shape == 2 ? 30u : 12u);
            for (unsigned k = 0; k < word && n < total; ++k) wg_put8(s + n++, (uint8_t)('A' + wg_rand(26u)));
            if (n < total) wg_put8(s + n++, ' ');
        }
    }
    wg_put8(s + n, 0);
}
static void wg_build_command(wg_world *w) {
    uint32_t p = w->p, cp = w->cp = wg_alloc(0x500u);
    memcpy((void *)(uintptr_t)cp, (void *)(uintptr_t)p, 0x4d3u);
    unsigned current = wg_rand(4u);
    wg_put32(cp + 0x49au, current == 0 ? 0u : current == 1u ? w->item : cp + 4u + 0x31u * wg_rand(10u));
    wg_put32(cp + 0x4d3u, 0x00b20000u + wg_rand(0x100u));
    wg_put32(cp + 0x4d7u, 0x00b30000u + wg_rand(0x100u));
    wg_put32(cp + 0x4dfu, w->font);
    wg_put32(cp + 0x4e3u, (uint32_t)((int32_t)wg_rand(400u) - 50));
    wg_put32(cp + 0x4e7u, wg_rand(100u));
    wg_put32(cp + 0x4ebu, wg_rand(8u) ? wg_rand(7u) : 0xffffffffu);
    w->ptab = wg_alloc(0x480u);
    w->nrec = 2u + wg_rand(7u);
    uint32_t v = w->ptab + 4u;
    for (unsigned r = 0; r < w->nrec; ++r) {
        w->prec[r] = v;
        wg_phrase_text(v, 0x6fu);
        uint32_t n = (uint32_t)strlen((const char *)(uintptr_t)v) + 1u, b = wg_rand(4u);
        wg_put16(v - 2u, (uint16_t)n);
        wg_put8(v + n, (uint8_t)b);
        for (unsigned k = 0; k < b; ++k) wg_put8(v + n + 1u + k, (uint8_t)wg_rand(256u));
        v += n + 1u + b + 2u;
    }
    put32(mapped + 0xd0444u, w->ptab);
    put32(mapped + 0xd044cu, w->prec[w->nrec - 1u] - w->ptab + 1u);
    w->phr = wg_rand(2u);
    if (w->phr)
        for (unsigned k = 0; k < 16u; ++k) {
            uint32_t d = 0x004d08edu + 9u * k, j = wg_rand(w->nrec);
            wg_put32(d, w->prec[j]);
            wg_put8(d + 4u, (uint8_t)wg_rand(w->nrec - j + 2u));
        }
    for (unsigned k = 0; k < 2u; ++k) {
        w->cmsg[k] = wg_alloc(0x31u);
        wg_put16(w->cmsg[k], (uint16_t)wg_rand(16u));
    }
    unsigned root = wg_rand(30u);
    for (unsigned k = 0; k < root; ++k) mapped[0xd280cu + k] = (BYTE)('a' + wg_rand(26u));
    mapped[0xd280cu + root] = 0;
    w->cdarg = wg_string_new(20u);
    if (wg_rand(2u)) { wg_put8(w->cdarg, 'D'); wg_put8(w->cdarg + 1u, ':'); wg_put8(w->cdarg + 2u, 0); }
    static const uint16_t sets[] = {0, 0, 1, 2, 0x100u, 0x1234u};
    put16(mapped + 0xaa728u, sets[wg_rand(6u)]);
    put16(mapped + 0xd253cu, (uint16_t)wg_rand(2u));
    w->pbuf = wg_alloc(0x300u);
    w->pv = w->pbuf + 0x140u + wg_rand(0x40u);
    int32_t off = (int32_t)wg_rand(0x200u) - 0x100;
    wg_put16(w->pv - 2u, (uint16_t)off);
    wg_put8(w->pv + (uint32_t)off, (uint8_t)(wg_rand(3u) ? wg_rand(256u) : 0u));
    w->phdr = wg_alloc(12u);
    wg_put32(w->phdr, w->pbuf);
    wg_put32(w->phdr + 8u, 0x140u + wg_rand(0x50u));
    w->vs = wg_alloc(0x20u);
    w->vr = wg_alloc(8u);
    wg_put32(w->vs + 0x16u, w->vr);
    wg_put32(w->vr, wg_rand(6u) ? wg_rand(6u) : 0xfffffffeu);
    wg_put32(w->vr + 4u, 0x00c80000u + wg_rand(0x100u));
}
static void wg_build(uint32_t clock_start) {
    wg_world *w = &wg_w;
    memset(w, 0, sizeof(*w));
    w->now = (clock_start - get32(mapped + 0xd3b78u)) >> 4;
    w->p = wg_alloc(0x600u);
    w->vt = wg_alloc(0x40u);
    w->rec[0] = wg_alloc(0x20u);
    w->rec[1] = wg_alloc(0x20u);
    w->desc = wg_alloc(0x10u);
    w->gauge = wg_alloc(0x100u);
    w->surface = wg_alloc(0x100u);
    w->hold = wg_alloc(0x40u);
    w->globals = wg_alloc(0x100u);
    w->rect = wg_alloc(0x10u);
    w->item = wg_alloc(0x40u);
    wg_display = wg_alloc(0x20u);
    for (unsigned i = 0; i < WG_ROWS; ++i) {
        wg_row[i] = wg_alloc(0x10u);
        for (unsigned k = 0; k < 12u; ++k) wg_put8(wg_row[i] + k, (uint8_t)wg_rand(k < 3u ? 6u : k < 7u ? 4u : 256u));
    }
    w->key[0] = 0x00e00000u + wg_rand(0x100u);
    w->key[1] = 0x00e10000u + wg_rand(0x100u);
    /* The records and the heap descriptor. */
    for (unsigned i = 0; i < 2u; ++i) {
        for (unsigned k = 0; k < 0x18u; ++k) wg_put8(w->rec[i] + k, (uint8_t)wg_rand(256u));
        wg_put8(w->rec[i], (uint8_t)wg_rand(16u));
        wg_put32(w->rec[i] + 2u, wg_rand(3u) ? w->key[wg_rand(2u)] : 0u);
    }
    wg_put32(w->desc, 0x00d00000u + wg_rand(0x1000u));
    wg_put8(w->desc + 4u, 1);
    wg_put32(w->desc + 5u, wg_row[wg_rand(WG_ROWS)]);
    /* The descriptor rows in DATA: 0x4d0464 + 387*w + 9*code (w < 3),
     * 0x4d08ed and 0x4d0aaf + 9*code (code < 16). */
    for (unsigned k = 0; k < 16u; ++k) {
        for (unsigned v = 0; v < 3u; ++v) wg_desc_fill(0x004d0464u + 387u * v + 9u * k);
        wg_desc_fill(0x004d08edu + 9u * k);
        wg_desc_fill(0x004d0aafu + 9u * k);
    }
    /* Stream B: random seed bytes, the indices 24/55. */
    for (unsigned k = 0; k < 0x70u; ++k) mapped[0xd260bu + k] = (BYTE)wg_rand(256u);
    mapped[0xd260bu + 0x70u] = 24;
    mapped[0xd260bu + 0x71u] = 55;
    /* The port and its queue. */
    uint32_t p = w->p, b = p + 4u;
    wg_put32(p, w->vt);
    wg_put32(w->vt + 4u, (uint32_t)(uintptr_t)wg_slot_ask);
    wg_put32(w->vt + 0xcu, (uint32_t)(uintptr_t)wg_slot_end);
    wg_put32(w->vt + 8u, (uint32_t)(uintptr_t)wg_slot_text);
    wg_put32(w->vt + 0x18u, (uint32_t)(uintptr_t)wg_slot_draw);
    unsigned n = wg_rand(4u) == 0 ? 10u : wg_rand(11u);
    uint32_t order[10];
    for (unsigned k = 0; k < 10u; ++k) order[k] = k;
    for (unsigned k = 9; k > 0; --k) { unsigned j = wg_rand(k + 1u); uint32_t t = order[k]; order[k] = order[j]; order[j] = t; }
    for (unsigned k = 0; k < 10u; ++k) {
        wg_put32(b + 0x1eau + 4u * k, order[k]);
        wg_put8(b + 0x212u + order[k], k < n ? 1u : wg_rand(5u) == 0);
        uint32_t e = b + 0x31u * k;
        wg_put16(e, wg_code());
        wg_put32(e + 2u, wg_rand(3u) ? w->key[wg_rand(2u)] : 0u);
        wg_put8(e + 0x17u, (uint8_t)wg_rand(6u));
        wg_put32(e + 0x18u, 0x00f00000u + k);
        for (unsigned f = 0; f < 4u; ++f) wg_put32(e + 0x1cu + 4u * f, wg_near(w->now));
        wg_put8(e + 0x2cu, (uint8_t)wg_rand(2u));
    }
    wg_put16(b + 0x21cu, (uint16_t)n);
    unsigned current = wg_rand(4u);
    wg_put32(p + 0x49au, current == 0 ? 0u : current == 1u ? w->item : b + 0x31u * order[wg_rand(10u)]);
    wg_put16(w->item, wg_code());
    wg_put32(w->item + 2u, w->key[wg_rand(2u)]);
    for (unsigned f = 0; f < 4u; ++f) wg_put32(w->item + 0x1cu + 4u * f, wg_near(w->now));
    wg_put8(w->item + 0x2cu, (uint8_t)wg_rand(2u));
    wg_put8(p + 0x49eu, (uint8_t)wg_rand(2u));
    wg_put8(p + 0x4d2u, wg_rand(4u) == 0);
    wg_put8(p + 0x4c9u, (uint8_t)wg_rand(2u));
    wg_put8(p + 0x4cau, wg_rand(3u) == 0);
    wg_put8(p + 0x4cbu, wg_rand(3u) == 0);
    wg_put32(p + 0x4ccu, wg_rand(6u) == 0 ? w->now - wg_rand(9u) : 0u);
    wg_put32(p + 0x4a3u, 0x00b00000u + wg_rand(0x100u));
    for (unsigned k = 0; k < 4u; ++k) wg_put32(p + 0x4a7u + 4u * k, (uint32_t)((int32_t)wg_rand(200u) - 50 + (k >= 2u ? 100 : 0)));
    wg_put32(p + 0x4afu, wg_rand(0x10000u));
    wg_put8(p + 0x4b7u, (uint8_t)wg_rand(3u));
    wg_put8(p + 0x4b8u, (uint8_t)wg_rand(3u));
    wg_put32(p + 0x4b9u, (uint32_t)(uintptr_t)wg_cb_start);
    wg_put32(p + 0x4bdu, (uint32_t)(uintptr_t)wg_cb_start);
    wg_put32(p + 0x4c1u, (uint32_t)(uintptr_t)wg_cb_hide);
    wg_put32(p + 0x4c5u, (uint32_t)(uintptr_t)wg_cb_hide);
    wg_put32(p + 0x4d3u, 0x00b10000u + wg_rand(0x100u));
    for (unsigned k = 0; k < 4u; ++k) wg_put32(w->rect + 4u * k, (uint32_t)((int32_t)wg_rand(300u) - 100));
    /* The gauge. */
    uint32_t g = w->gauge;
    wg_put32(g + 0x6cu, wg_rand(2u) ? 0x00a00000u + wg_rand(0x100u) : 0u);
    wg_put16(g + 0x70u, (uint16_t)(wg_rand(2u) ? wg_rand(10u) : 0xfff0u + wg_rand(16u)));
    wg_put16(g + 0x72u, (uint16_t)(wg_rand(2u) ? wg_rand(10u) : 0xfff0u + wg_rand(16u)));
    wg_put16(g + 0x74u, wg_rand(5u) != 0);
    wg_put8(g + 0x76u, (uint8_t)wg_rand(2u));
    wg_put32(g + 0x78u, wg_rand(3u) == 0 ? 0u : wg_near(w->now));
    for (unsigned k = 0; k < 2u; ++k) {
        uint32_t lo = wg_rand(30u), hi = wg_rand(4u) == 0 ? lo : lo + 1u + wg_rand(60u);
        if (wg_rand(8u) == 0) hi = lo - 1u - wg_rand(5u);   /* hi below lo: the unsigned span */
        wg_put32(g + 0x7cu + 8u * k, lo);
        wg_put32(g + 0x80u + 8u * k, hi);
    }
    /* The globals. */
    put32(mapped + 0xd24e6u, w->globals);
    wg_put8(w->globals + 0x99u, wg_rand(4u) == 0);
    put32(mapped + 0xcf99cu, w->hold);
    wg_put8(w->hold + 0x1cu, wg_rand(4u) == 0);
    put32(mapped + 0x2c5ed0u, w->surface);
    wg_put32(w->surface + 0x88u, 0x11110000u + wg_rand(0x100u));
    wg_put32(w->surface + 0x8cu, 0x22220000u + wg_rand(0x100u));
    wg_put32(w->surface + 0x3cu, 0x33330000u + wg_rand(0x100u));
    put32(mapped + 0xa57ecu, (uint32_t)(uintptr_t)wg_driver_page);
    put32(mapped + 0xa57f0u, (uint32_t)(uintptr_t)wg_driver_page);
    put32(mapped + 0xa5828u, (uint32_t)(uintptr_t)wg_driver_sync);
    put32(mapped + 0xa5824u, (uint32_t)(uintptr_t)wg_driver_sync);
    put16(mapped + 0x9dd8cu, (uint16_t)wg_rand(2u));
    wg_put32(wg_display + 0x14u, wg_rand(3u) ? 4u : 3u);
    put32(mapped + 0xd1294u, wg_rand(4u) == 0 ? 0u : wg_rand(w->now / 2u + 1u));
    put16(mapped + 0xd253au, (uint16_t)wg_rand(2u));
    {
        /* Drawn from the seed outside the world's stream: the paused total
         * past the clock (a negative product in 0x436f70) or far below it,
         * and a scale up to 7 (with the latter a product past 60 << 32). */
        uint32_t ev = wg_seed * 0x9e3779b1u;
        ev ^= ev >> 15; ev *= 0x85ebca6bu; ev ^= ev >> 13;
        if (ev % 6u == 0) put32(mapped + 0xd1294u, w->now + 1u + (ev >> 8) % 0x7fffffu * 0x100u);
        if (ev % 6u == 1) put32(mapped + 0xd1294u, w->now - 0x08000000u - (ev >> 8) % 0x77ffffu * 0x100u);
        if ((ev >> 4) % 8u == 0) put16(mapped + 0xd253au, (uint16_t)(2u + (ev >> 7) % 6u));
    }
    /* The current graphics context (its origin +0x220/+0x224; the canvas push
     * answers it), the surface's clip record (0x480c34 copies it), the font,
     * the screen width, the switches, the colours, the language, the roster
     * (wingman index 0xabcdef00 | k lands on row k of the table at 334-byte
     * steps), the messages for the texts. */
    w->gc = wg_alloc(0x240u);
    for (unsigned k = 0; k < 0x240u; k += 4u) wg_put32(w->gc + k, wg_rand(0x10000u));
    wg_put32(w->gc + 0x220u, (uint32_t)((int32_t)wg_rand(80u) - 20));
    wg_put32(w->gc + 0x224u, (uint32_t)((int32_t)wg_rand(80u) - 20));
    put32(mapped + 0x2c5f70u, w->gc);
    w->clip = wg_alloc(0x230u);
    for (unsigned k = 0; k < 0x230u; k += 4u) wg_put32(w->clip + k, wg_rand(0x10000u));
    wg_put32(w->surface + 0x40u, w->clip);
    w->font = wg_alloc(0x20u);
    wg_put16(w->font + 0xau, (uint16_t)wg_rand(20u));
    wg_put16(w->font + 0x16u, (uint16_t)(wg_rand(4u) ? wg_rand(20u) : 0xfff0u + wg_rand(16u)));
    put32(mapped + 0x9b0b4u, w->font);
    put32(mapped + 0xd253eu, wg_rand(3u) ? 0x140u << wg_rand(2u) : wg_rand(0x400u));
    mapped[0xd1f3au] = (BYTE)wg_rand(3u);
    mapped[0xd1f3bu] = (BYTE)(wg_rand(2u) ? 0u : wg_rand(3u));
    mapped[0x9b7d2u] = wg_rand(4u) ? mapped[0x9b7d2u] : (BYTE)'0';
    put16(mapped + 0xd3ba2u, (uint16_t)(wg_rand(2u) ? wg_rand(256u) : 0xff00u + wg_rand(256u)));
    put16(mapped + 0xd3b8eu, (uint16_t)(wg_rand(2u) ? wg_rand(256u) : 0xff00u + wg_rand(256u)));
    static const BYTE language[] = {'r', 'e', 'f', 'g', 0xf2};
    mapped[0xd2536u] = language[wg_rand(5u)];
    uint32_t roster = wg_alloc(0x200u), table = wg_alloc(3u * 334u + 8u);
    put32(mapped + 0x9b088u, roster);
    for (unsigned k = 0; k < 3u; ++k) wg_put32(table + 334u * k, wg_string_new(20u));
    put32(mapped + 0x9d230u, table - 334u * 0xabcdef00u - 0x264u);
    put32(mapped + 0xd13acu, wg_string_new(20u));
    for (unsigned k = 0; k < 2u; ++k) {
        w->msg[k] = wg_alloc(0x31u);
        wg_put16(w->msg[k], wg_code());
        wg_put32(w->msg[k] + 2u, wg_rand(3u) ? w->key[wg_rand(2u)] : 0u);
        wg_put32(w->msg[k] + 0x18u, wg_string_new(wg_rand(2u) ? 30u : 120u));
    }
    w->vname = wg_string_new(12u);
    wg_pext = wg_string_new(12u);
    wg_put8(p + 0x4d3u, (uint8_t)(wg_rand(2u) ? 0u : 1u + wg_rand(255u)));
    wg_put32(p + 0x4e4u, w->now + 100u - wg_rand(600u));
    wg_put16(p + 0x4e8u, (uint16_t)(wg_rand(5u) ? wg_rand(4u) : 0xffffu));
    for (unsigned k = 0; k < 4u; ++k) wg_put32(p + 0x4d4u + 4u * k, (uint32_t)((int32_t)wg_rand(200u) - 50 + (k >= 2u ? 100 : 0)));
    wg_build_command(w);
}

/* ---- the rows */
enum { WG_INSERT, WG_HEAD, WG_ENTRY, WG_REMOVE, WG_DROP, WG_EXPIRE, WG_REMOVE_KEY, WG_STEP, WG_PICK, WG_CANCEL, WG_DONE,
       WG_DIALOG_RECEIVE, WG_DIALOG_HIDE, WG_DIALOG_FRAME, WG_EVENT_RECEIVE, WG_EVENT_FRAME, WG_REDRAW, WG_BLINK, WG_RANDOM,
       WG_DIALOG_SHOW, WG_DIALOG_TEXT, WG_EVENT_SHOW, WG_EVENT_TEXT, WG_EVENT_DRAW, WG_EVENT_HIDE, WG_PORT_RESTORE, WG_DIALOG_DRAW,
       WG_VOICE_PATH, WG_CMD_RECEIVE, WG_CMD_TEXT, WG_CMD_FRAME, WG_CMD_SHOW, WG_CD_PATH, WG_PHRASE_NEXT, WG_PHRASE_ROW,
       WG_VOICE_SELECT, WG_OPS };
static const struct { uint32_t va; unsigned n; uint32_t mask; const char *name; } wg_ops[WG_OPS] = {
    {0x00434e8cu, 3, 0, "queue insert"}, {0x00435074u, 1, ~0u, "queue head"}, {0x004350d4u, 2, ~0u, "queue entry"},
    {0x00435140u, 2, 0, "queue remove"}, {0x004351c0u, 1, ~0u, "queue drop"}, {0x004351ecu, 1, 0, "queue expire"},
    {0x00435240u, 3, 0xffu, "queue remove by key"}, {0x00435610u, 1, 0, "port step"},
    {0x00435a74u, 4, ~0u, "port pick"}, {0x00435ac8u, 3, 0xffu, "port cancel"}, {0x00435b14u, 2, ~0u, "port done"},
    {0x00435c48u, 2, 0, "dialog receive"}, {0x00436130u, 1, 0xffu, "dialog hide"}, {0x004361ccu, 1, 0, "dialog frame"},
    {0x00436a3cu, 2, 0, "event receive"}, {0x00436f70u, 1, 0, "event frame"}, {0x00437164u, 2, 0, "port redraw"},
    {0x00438bc0u, 1, 0, "gauge blink"}, {0x00438d6cu, 3, ~0u, "blink random"},
    {0x00435d9cu, 1, 0xffu, "dialog show"}, {0x00435d0cu, 2, ~0u, "dialog text"}, {0x00436abcu, 1, 0xffu, "event show"},
    {0x00436a0cu, 2, ~0u, "event text"}, {0x00436cecu, 1, 0, "event draw"}, {0x00436fd0u, 1, 0xffu, "event hide"},
    {0x00437044u, 1, 0, "port restore"}, {0x00437094u, 1, 0, "dialog draw"}, {0x0045ef9cu, 2, ~0u, "voice path"},
    {0x004362e4u, 2, 0, "command receive"}, {0x00436318u, 2, ~0u, "command text"}, {0x004365d0u, 1, 0, "command frame"},
    {0x0043660cu, 1, 0xffu, "command show"}, {0x0045ee60u, 1, ~0u, "cd path"}, {0x00453a10u, 2, ~0u, "phrase next"},
    {0x00453a4cu, 2, ~0u, "phrase row"}, {0x00471de0u, 2, ~0u, "voice select"},
};
/* The stubbed entry each op runs for real (0: none). */
static uint32_t wg_op_real(unsigned op) {
    switch (op) {
    case WG_CMD_TEXT: case WG_PHRASE_NEXT: return 0x00453a10u;
    case WG_PHRASE_ROW: return 0x00453a4cu;
    case WG_VOICE_SELECT: return 0x00471de0u;
    default: return 0;
    }
}
typedef struct wg_op { unsigned op; uint32_t a[4]; int full; } wg_op;
/* Arguments drawn from the world (the same on both sides). */
static void wg_args(wg_op *o) {
    const wg_world *w = &wg_w;
    uint32_t b = w->p + 4u;
    memset(o->a, 0, sizeof(o->a));
    switch (o->op) {
    case WG_INSERT: o->a[0] = b; o->a[1] = w->rec[random32() % 2u]; o->a[2] = w->desc; break;
    case WG_ENTRY: o->a[0] = b; o->a[1] = random32() % 4u == 0 ? 0xfffffffeu + random32() % 2u : random32() % 12u; break;
    case WG_REMOVE: o->a[0] = b; o->a[1] = random32() % 10u; break;
    case WG_REMOVE_KEY: case WG_CANCEL: {
        uint32_t e = random32() % 2u ? wg_get(w->p + 0x49au) : b + 0x31u * (random32() % 10u);
        o->a[0] = o->op == WG_CANCEL ? w->p : b;
        o->a[1] = e && random32() % 4u ? wg_get(e + 2u) : w->key[random32() % 2u];
        o->a[2] = (e && random32() % 4u ? wg_get16(e) : random32() % 4u) | (random32() % 2u ? 0x12340000u : 0u);
        break;
    }
    case WG_PICK: {
        static const uint8_t n[] = {0, 1, 2, 3, 0x81, 0xff};
        o->a[0] = w->p; o->a[1] = 0x004d0414u + 16u * (random32() % 3u);
        o->a[2] = 0x004d08edu + 9u * (random32() % 16u);
        o->a[3] = n[random32() % 6u] | (random32() % 2u ? 0x300u : 0u);
        break;
    }
    case WG_DONE: o->a[0] = w->p; o->a[1] = random32() % 4u ? wg_get(w->p + 0x49au) : w->item; break;
    case WG_DIALOG_RECEIVE: case WG_EVENT_RECEIVE: o->a[0] = w->p; o->a[1] = w->rec[random32() % 2u]; break;
    case WG_REDRAW: o->a[0] = w->p; o->a[1] = random32() % 3u; break;
    case WG_BLINK: o->a[0] = w->gauge; break;
    case WG_RANDOM: {
        uint32_t lo = random32() % 100u;
        o->a[0] = w->gauge; o->a[1] = lo; o->a[2] = random32() % 4u == 0 ? lo : random32() % 8u == 0 ? lo - 1u : lo + 1u + random32() % 1000u;
        break;
    }
    case WG_HEAD: case WG_DROP: case WG_EXPIRE: o->a[0] = b; break;
    case WG_DIALOG_SHOW: case WG_EVENT_SHOW: case WG_EVENT_DRAW:
        /* These read the current entry without a check: none, the head. */
        if (wg_get(w->p + 0x49au)) o->a[0] = w->p;
        else { o->op = WG_HEAD; o->a[0] = b; }
        break;
    case WG_DIALOG_TEXT: case WG_EVENT_TEXT: o->a[0] = w->p; o->a[1] = random32() % 5u == 0 ? 0u : w->msg[random32() % 2u]; break;
    case WG_VOICE_PATH: o->a[0] = w->vname; o->a[1] = 0x0049ba60u; break;
    case WG_CMD_RECEIVE: o->a[0] = w->cp; o->a[1] = w->rec[random32() % 2u]; break;
    case WG_CMD_TEXT:
        /* Entries only where the rows walk the phrase table. */
        o->a[0] = w->cp; o->a[1] = w->phr && random32() % 5u ? w->cmsg[random32() % 2u] : 0u;
        break;
    case WG_CMD_FRAME: o->a[0] = w->cp; break;
    case WG_CMD_SHOW:
        if (wg_get(w->cp + 0x49au)) o->a[0] = w->cp;
        else { o->op = WG_HEAD; o->a[0] = w->cp + 4u; }
        break;
    case WG_CD_PATH: o->a[0] = w->cdarg; break;
    case WG_PHRASE_NEXT: case WG_PHRASE_ROW:
        if (random32() % 2u) { o->a[0] = w->phdr; o->a[1] = w->pv; }
        else { o->a[0] = 0x004d0444u; o->a[1] = w->prec[random32() % w->nrec]; }
        break;
    case WG_VOICE_SELECT: {
        uint32_t n = wg_get(w->vr);
        o->a[0] = w->vs;
        switch (random32() % 5u) {
        case 0: o->a[1] = n; break;
        case 1: o->a[1] = n + 1u; break;
        case 2: o->a[1] = 0x80000000u | random32(); break;
        default: o->a[1] = random32() % 8u; break;
        }
        break;
    }
    default: o->a[0] = w->p; break;
    }
}

/* ---- coverage: the paths seen (from the world before and after a call
 * on the original side) */
enum { WGC_INSERT_FULL, WGC_INSERT_ROOM, WGC_INSERT_QUEUED, WGC_INSERT_NO_SLOT,
       WGC_REMOVE_EMPTY, WGC_REMOVE_SHIFT, WGC_EXPIRE_REMOVED, WGC_EXPIRE_KEPT, WGC_KEY_FOUND, WGC_KEY_MISSED,
       WGC_STEP_PAUSED, WGC_STEP_EMPTY, WGC_STEP_HEAD, WGC_STEP_EXPIRED, WGC_STEP_STARTED, WGC_STEP_WAIT, WGC_STEP_ASK_YES, WGC_STEP_ASK_NO,
       WGC_STEP_END_YES, WGC_STEP_END_NO, WGC_STEP_HOLD,
       WGC_PICK_ZERO, WGC_PICK_LOOP, WGC_CANCEL_CURRENT, WGC_CANCEL_STARTED, WGC_CANCEL_QUEUE, WGC_DONE_YES, WGC_DONE_NO,
       WGC_RECEIVE_DRAW, WGC_RECEIVE_ONE, WGC_RECEIVE_QUEUED, WGC_RECEIVE_SKIPPED, WGC_RECEIVE_SENDER, WGC_HIDE_OFF, WGC_HIDE_HELD, WGC_HIDE_DONE,
       WGC_FRAME_DRAW, WGC_FRAME_AUTO, WGC_REDRAW_OFF, WGC_REDRAW_MODE1, WGC_REDRAW_OTHER, WGC_REDRAW_SYNC, WGC_REDRAW_NOSYNC,
       WGC_BLINK_IDLE, WGC_BLINK_VOICE, WGC_BLINK_PLAIN, WGC_BLINK_DRAW, WGC_BLINK_FLIP, WGC_BLINK_WAIT, WGC_BLINK_START, WGC_BLINK_STEP_STOP,
       WGC_BLINK_STEP_GO, WGC_RANDOM_SAME, WGC_RANDOM_DRAW,
       WGC_DSHOW_GATE, WGC_DSHOW_VOICE, WGC_DSHOW_SILENT, WGC_DSHOW_QUIET, WGC_DSHOW_NOBOX, WGC_DSHOW_WING, WGC_DSHOW_PLAIN,
       WGC_DSHOW_SYNC, WGC_DSHOW_NOSYNC, WGC_ESHOW_GATE, WGC_ESHOW_DIRECT, WGC_ESHOW_REPEAT, WGC_ESHOW_VOICE, WGC_ESHOW_SOUND0,
       WGC_ESHOW_SOUND1, WGC_ESHOW_SOUND2, WGC_ESHOW_SOUND3, WGC_ESHOW_SOUND4, WGC_ESHOW_SOUND5, WGC_ESHOW_HIGH,
       WGC_EDRAW_OFF, WGC_EDRAW_UNSEEN, WGC_EDRAW_CENTRE, WGC_EDRAW_CLOCKX, WGC_EDRAW_STEADY, WGC_EDRAW_BLINK_ON, WGC_EDRAW_BLINK_OFF,
       WGC_EHIDE_OFF, WGC_EHIDE_DONE, WGC_RESTORE_OFF, WGC_RESTORE_DONE, WGC_DDRAW_OFF, WGC_DDRAW_DONE,
       WGC_DTEXT_NONE, WGC_DTEXT_SELF, WGC_DTEXT_WING, WGC_ETEXT_NONE, WGC_ETEXT_COPY, WGC_VPATH_LANGUAGE, WGC_VPATH_R,
       WGC_CRECV_QUEUED, WGC_CRECV_SKIPPED, WGC_CTEXT_NONE, WGC_CTEXT_EMPTY, WGC_CTEXT_ONE, WGC_CTEXT_MANY, WGC_CTEXT_SPLIT,
       WGC_CTEXT_NOSPLIT, WGC_CTEXT_NARROW, WGC_CTEXT_WIDE, WGC_CTEXT_WIDTH, WGC_CFRAME_DRAW, WGC_CFRAME_DONE, WGC_CFRAME_IDLE,
       WGC_CSHOW_GATE, WGC_CSHOW_NOBOX, WGC_CSHOW_BOX, WGC_CSHOW_LINES, WGC_CSHOW_VOICE, WGC_CSHOW_ROOT, WGC_CSHOW_SILENT,
       WGC_CSHOW_SYNC, WGC_CSHOW_NOSYNC, WGC_CDPATH_DRIVE, WGC_CDPATH_PLAIN, WGC_PNEXT_IN, WGC_PNEXT_OUT, WGC_PROW_ROW,
       WGC_PROW_ZERO, WGC_PROW_OUT, WGC_VSEL_YES, WGC_VSEL_EQUAL, WGC_VSEL_NO, WGC_EFRAME_PLAIN, WGC_EFRAME_NEGATIVE, WGC_EFRAME_WIDE, WGC_COUNT };
static const char *const wg_cv_names[WGC_COUNT] = {
    "insert full", "insert room", "insert queued", "insert no slot", "remove empty", "remove shift",
    "expire removed", "expire kept", "key found", "key missed", "step paused", "step empty", "step head", "step expired", "step started",
    "step wait", "step ask yes", "step ask no", "step end yes", "step end no", "step hold",
    "pick zero", "pick loop", "cancel current", "cancel started", "cancel queue", "done yes", "done no", "receive draw", "receive one",
    "receive queued", "receive skipped", "receive sender", "hide off", "hide held", "hide done", "frame draw", "frame auto",
    "redraw off", "redraw mode 1", "redraw other", "redraw sync", "redraw no sync", "blink idle", "blink voice", "blink plain",
    "blink draw", "blink flip", "blink wait", "blink start", "blink step stop", "blink step go", "random same", "random draw",
    "dialog show gated", "dialog show voice", "dialog show voice '0'", "dialog show no voice", "dialog show no box",
    "dialog show wingman colour", "dialog show plain colour", "dialog show sync", "dialog show no sync", "event show gated",
    "event show direct", "event show repeat", "event show voice", "event show sound 0", "event show sound 1", "event show sound 2",
    "event show sound 3", "event show sound 4", "event show sound 5", "event show code above 0x38", "event draw off",
    "event draw not visible", "event draw centred", "event draw clock x", "event draw steady", "event draw blink on",
    "event draw blink off", "event hide off", "event hide done", "restore off", "restore done", "dialog draw off",
    "dialog draw done", "dialog text none", "dialog text self", "dialog text wingman", "event text none", "event text copy",
    "voice path language", "voice path r",
    "command receive queued", "command receive skipped", "command text none", "command text no phrases", "command text one line",
    "command text lines", "command text split", "command text no split", "command text narrow", "command text wide",
    "command text width", "command frame draw", "command frame done", "command frame idle", "command show gated",
    "command show no box", "command show box", "command show lines", "command show voice", "command show voice cd root",
    "command show silent", "command show sync", "command show no sync", "cd path drive", "cd path plain", "phrase next in",
    "phrase next out", "phrase row row", "phrase row zero", "phrase row out", "voice select yes", "voice select n == count",
    "voice select no", "event frame plain product", "event frame negative product", "event frame product past 60 << 32"};
static unsigned wg_cv[WGC_COUNT];
typedef struct wg_pre { uint16_t count; uint8_t used, b49e, b4d2, b4c9, b4ca, b4cb, b76; uint32_t current, pause, t78, letters; uint16_t code; uint8_t b4d3;
                        uint8_t c49e, c4d2, c4c9; uint32_t ncat; } wg_pre;
static unsigned wg_used(uint32_t b) { unsigned n = 0; for (unsigned i = 0; i < 10u; ++i) n += wg_get8(b + 0x212u + i) != 0; return n; }
static void wg_take_pre(wg_pre *s) {
    uint32_t p = wg_w.p, g = wg_w.gauge;
    s->count = wg_get16(p + 4u + 0x21cu);
    s->used = (uint8_t)wg_used(p + 4u);
    s->current = wg_get(p + 0x49au);
    s->b49e = wg_get8(p + 0x49eu); s->b4d2 = wg_get8(p + 0x4d2u);
    s->b4c9 = wg_get8(p + 0x4c9u); s->b4ca = wg_get8(p + 0x4cau); s->b4cb = wg_get8(p + 0x4cbu);
    s->pause = wg_get(p + 0x4ccu);
    s->b76 = wg_get8(g + 0x76u);
    s->t78 = wg_get(g + 0x78u);
    s->letters = wg_count[0];
    s->code = s->current ? wg_get16(s->current) : 0;
    s->b4d3 = wg_get8(p + 0x4d3u);
    s->c49e = wg_get8(wg_w.cp + 0x49eu); s->c4d2 = wg_get8(wg_w.cp + 0x4d2u); s->c4c9 = wg_get8(wg_w.cp + 0x4c9u);
    s->ncat = wg_ncat;
}
static int wg_saw(char letter, unsigned from) {
    /* The stub letters of side 0 since the call began (from the report text). */
    char needle[3] = {letter, '(', 0};
    (void)from;
    return strstr(wg_text[0] + from, needle) != NULL;
}
static void wg_cover(const wg_op *o, const wg_pre *s, uint32_t eax, size_t text_from) {
    uint32_t p = wg_w.p, b = p + 4u, g = wg_w.gauge;
    uint16_t count = wg_get16(b + 0x21cu);
    int full_text = wg_len[0] + 48u < sizeof(wg_text[0]);
    switch (o->op) {
    case WG_INSERT:
        ++wg_cv[s->count == 10u ? WGC_INSERT_FULL : WGC_INSERT_ROOM];
        ++wg_cv[count != (uint16_t)(s->count - (s->count == 10u)) ? WGC_INSERT_QUEUED : WGC_INSERT_NO_SLOT];
        break;
    case WG_REMOVE: ++wg_cv[s->count ? WGC_REMOVE_SHIFT : WGC_REMOVE_EMPTY]; break;
    case WG_EXPIRE: ++wg_cv[count != s->count ? WGC_EXPIRE_REMOVED : WGC_EXPIRE_KEPT]; break;
    case WG_REMOVE_KEY: ++wg_cv[(eax & 0xffu) ? WGC_KEY_FOUND : WGC_KEY_MISSED]; break;
    case WG_STEP: case WG_DIALOG_FRAME: case WG_EVENT_FRAME: {
        if (o->op == WG_EVENT_FRAME) {
            /* The product of 0x436f70 from the clock before the call (the call reads it a little later). */
            int64_t product = (int64_t)(int32_t)(0x23u << (get16(mapped + 0xd253au) & 0x1fu)) *
                              (int32_t)(wg_w.now - get32(mapped + 0xd1294u));
            ++wg_cv[product < 0 ? WGC_EFRAME_NEGATIVE : product >= (int64_t)60 << 32 ? WGC_EFRAME_WIDE : WGC_EFRAME_PLAIN];
        }
        if (o->op == WG_DIALOG_FRAME && s->b49e && !s->b4d2) ++wg_cv[WGC_FRAME_DRAW];
        if (o->op == WG_DIALOG_FRAME && s->current && wg_get8(s->current + 0x2cu) && s->b4c9) ++wg_cv[WGC_FRAME_AUTO];
        if (s->pause) { ++wg_cv[WGC_STEP_PAUSED]; break; }
        if (!s->current && !wg_get(p + 0x49au) && !s->count) ++wg_cv[WGC_STEP_EMPTY];
        if (!s->current && wg_get(p + 0x49au)) ++wg_cv[WGC_STEP_HEAD];
        if (!full_text) break;
        if (wg_saw('a', text_from)) ++wg_cv[wg_get8(p + 0x4c9u) ? WGC_STEP_ASK_NO : WGC_STEP_ASK_YES];
        if (wg_saw('c', text_from)) ++wg_cv[wg_get(p + 0x49au) ? WGC_STEP_END_NO : WGC_STEP_END_YES];
        if (wg_saw('s', text_from) || (!s->b4c9 && wg_get8(p + 0x4c9u))) ++wg_cv[WGC_STEP_STARTED];
        if (s->current && !wg_get(p + 0x49au) && !wg_saw('c', text_from)) ++wg_cv[WGC_STEP_EXPIRED];
        if (wg_get(p + 0x49au) && !wg_saw('a', text_from) && !wg_saw('c', text_from) && !s->b4ca && !s->b4cb &&
            wg_get8(p + 0x4c9u) == s->b4c9) ++wg_cv[WGC_STEP_WAIT];
        if (wg_get(p + 0x49au) && (s->b4ca || s->b4cb) && !wg_saw('a', text_from) && !wg_saw('c', text_from)) ++wg_cv[WGC_STEP_HOLD];
        break;
    }
    case WG_PICK: ++wg_cv[(o->a[3] & 0xffu) ? WGC_PICK_LOOP : WGC_PICK_ZERO]; break;
    case WG_CANCEL:
        if (s->current && wg_get16(s->current) == (uint16_t)o->a[2] && wg_get(s->current + 2u) == o->a[1])
            ++wg_cv[s->b4c9 ? WGC_CANCEL_STARTED : WGC_CANCEL_CURRENT];
        else ++wg_cv[WGC_CANCEL_QUEUE];
        break;
    case WG_DONE: ++wg_cv[o->a[1] == s->current && s->b4c9 ? WGC_DONE_YES : WGC_DONE_NO]; break;
    case WG_DIALOG_RECEIVE: case WG_EVENT_RECEIVE:
        if (o->op == WG_DIALOG_RECEIVE && wg_get(o->a[1] + 2u)) ++wg_cv[WGC_RECEIVE_SENDER];
        ++wg_cv[wg_saw('N', text_from) ? WGC_RECEIVE_DRAW : WGC_RECEIVE_ONE];
        ++wg_cv[count != s->count || wg_used(b) != s->used ? WGC_RECEIVE_QUEUED : WGC_RECEIVE_SKIPPED];
        break;
    case WG_DIALOG_HIDE:
        ++wg_cv[!s->b49e || s->b4d2 ? WGC_HIDE_OFF : wg_saw('G', text_from) ? WGC_HIDE_DONE : WGC_HIDE_HELD];
        break;
    case WG_REDRAW:
        if (!s->b49e || s->b4d2) { ++wg_cv[WGC_REDRAW_OFF]; break; }
        ++wg_cv[o->a[1] == 1u ? WGC_REDRAW_MODE1 : WGC_REDRAW_OTHER];
        ++wg_cv[wg_count[0] - s->letters >= 9u ? WGC_REDRAW_SYNC : WGC_REDRAW_NOSYNC];
        break;
    case WG_BLINK:
        if (!wg_get16(g + 0x74u)) { ++wg_cv[WGC_BLINK_IDLE]; break; }
        ++wg_cv[wg_get(g + 0x6cu) ? WGC_BLINK_VOICE : WGC_BLINK_PLAIN];
        if (!s->t78) ++wg_cv[WGC_BLINK_DRAW];
        else if (wg_get8(g + 0x76u) != s->b76) ++wg_cv[WGC_BLINK_FLIP];
        else if (!wg_saw('B', text_from)) ++wg_cv[WGC_BLINK_WAIT];
        if (wg_saw('V', text_from)) ++wg_cv[WGC_BLINK_START];
        if (wg_saw('P', text_from)) ++wg_cv[wg_get8(g + 0x76u) != s->b76 ? WGC_BLINK_STEP_STOP : WGC_BLINK_STEP_GO];
        break;
    case WG_RANDOM: ++wg_cv[o->a[1] == o->a[2] ? WGC_RANDOM_SAME : WGC_RANDOM_DRAW]; break;
    case WG_DIALOG_SHOW: {
        int gated = !(eax & 0xffu);
        if (gated) { ++wg_cv[WGC_DSHOW_GATE]; break; }
        if (!full_text) break;
        ++wg_cv[wg_saw('O', text_from) ? WGC_DSHOW_VOICE : wg_saw('Y', text_from) ? WGC_DSHOW_SILENT : WGC_DSHOW_QUIET];
        if (!wg_saw('t', text_from)) { ++wg_cv[WGC_DSHOW_NOBOX]; break; }
        ++wg_cv[wg_saw('k', text_from) ? WGC_DSHOW_WING : WGC_DSHOW_PLAIN];
        ++wg_cv[wg_get(wg_display + 0x14u) != 4u ? WGC_DSHOW_SYNC : WGC_DSHOW_NOSYNC];
        break;
    }
    case WG_EVENT_SHOW:
        if (wg_get8(wg_w.globals + 0x99u)) { ++wg_cv[WGC_ESHOW_GATE]; break; }
        if (!full_text) break;
        if (wg_saw('O', text_from)) ++wg_cv[WGC_ESHOW_VOICE];
        if (!(eax & 0xffu)) { ++wg_cv[WGC_ESHOW_REPEAT]; break; }
        if (!wg_saw('t', text_from)) { ++wg_cv[WGC_ESHOW_DIRECT]; break; }
        if (s->code > 0x38u) ++wg_cv[WGC_ESHOW_HIGH];
        ++wg_cv[WGC_ESHOW_SOUND0 + (s->code > 0x38u ? 0u : mapped[0x36be2u + s->code])];
        break;
    case WG_EVENT_DRAW:
        if (wg_get8(wg_w.hold + 0x1cu) || !s->b49e || s->b4d2) { ++wg_cv[WGC_EDRAW_OFF]; break; }
        if (!wg_saw('w', text_from)) { ++wg_cv[WGC_EDRAW_UNSEEN]; break; }
        ++wg_cv[s->code == 0x36u ? WGC_EDRAW_CENTRE : WGC_EDRAW_CLOCKX];
        ++wg_cv[!s->b4d3 ? WGC_EDRAW_STEADY : wg_saw('x', text_from) ? WGC_EDRAW_BLINK_ON : WGC_EDRAW_BLINK_OFF];
        break;
    case WG_EVENT_HIDE: ++wg_cv[!s->b49e || s->b4d2 ? WGC_EHIDE_OFF : WGC_EHIDE_DONE]; break;
    case WG_PORT_RESTORE: ++wg_cv[wg_saw('T', text_from) ? WGC_RESTORE_DONE : WGC_RESTORE_OFF]; break;
    case WG_DIALOG_DRAW: ++wg_cv[wg_saw('T', text_from) ? WGC_DDRAW_DONE : WGC_DDRAW_OFF]; break;
    case WG_DIALOG_TEXT: ++wg_cv[!o->a[1] ? WGC_DTEXT_NONE : wg_get(o->a[1] + 2u) ? WGC_DTEXT_WING : WGC_DTEXT_SELF]; break;
    case WG_EVENT_TEXT: ++wg_cv[o->a[1] ? WGC_ETEXT_COPY : WGC_ETEXT_NONE]; break;
    case WG_VOICE_PATH: ++wg_cv[mapped[0xd2536u] == 'r' ? WGC_VPATH_R : WGC_VPATH_LANGUAGE]; break;
    case WG_CMD_RECEIVE: {
        uint32_t d = 0x004d08edu + 9u * wg_get8(o->a[1]);
        ++wg_cv[wg_get(d) && wg_get(d + 5u) ? WGC_CRECV_QUEUED : WGC_CRECV_SKIPPED];
        break;
    }
    case WG_CMD_TEXT: {
        uint32_t lines = wg_get(wg_w.cp + 0x4ebu);
        if (!o->a[1]) { ++wg_cv[WGC_CTEXT_NONE]; break; }
        ++wg_cv[get16(mapped + 0xd253au) ? WGC_CTEXT_WIDE : WGC_CTEXT_NARROW];
        if (!lines) { ++wg_cv[WGC_CTEXT_EMPTY]; break; }
        ++wg_cv[lines == 1u ? WGC_CTEXT_ONE : WGC_CTEXT_MANY];
        if (wg_ncat != s->ncat) ++wg_cv[WGC_CTEXT_SPLIT];
        if (!mapped[0xd0d8fu]) ++wg_cv[WGC_CTEXT_NOSPLIT];
        if (full_text && wg_saw('w', text_from)) ++wg_cv[WGC_CTEXT_WIDTH];
        break;
    }
    case WG_CMD_FRAME:
        if (full_text && wg_saw('d', text_from)) ++wg_cv[WGC_CFRAME_DRAW];
        ++wg_cv[s->c4c9 ? WGC_CFRAME_DONE : WGC_CFRAME_IDLE];
        break;
    case WG_CMD_SHOW: {
        if (!(eax & 0xffu)) { ++wg_cv[WGC_CSHOW_GATE]; break; }
        if (!full_text) break;
        if (!wg_saw('t', text_from)) ++wg_cv[WGC_CSHOW_NOBOX];
        else {
            ++wg_cv[WGC_CSHOW_BOX];
            if (wg_saw('x', text_from)) ++wg_cv[WGC_CSHOW_LINES];
            ++wg_cv[wg_get(wg_display + 0x14u) != 4u ? WGC_CSHOW_SYNC : WGC_CSHOW_NOSYNC];
        }
        if (!wg_saw('O', text_from)) ++wg_cv[WGC_CSHOW_SILENT];
        else ++wg_cv[mapped[0xaa728u] ? WGC_CSHOW_ROOT : WGC_CSHOW_VOICE];
        break;
    }
    case WG_CD_PATH: ++wg_cv[wg_get8(o->a[0] + 1u) == ':' ? WGC_CDPATH_DRIVE : WGC_CDPATH_PLAIN]; break;
    case WG_PHRASE_NEXT: ++wg_cv[eax ? WGC_PNEXT_IN : WGC_PNEXT_OUT]; break;
    case WG_PHRASE_ROW:
        ++wg_cv[eax ? WGC_PROW_ROW : wg_get(o->a[0]) + wg_get(o->a[0] + 8u) > o->a[1] ? WGC_PROW_ZERO : WGC_PROW_OUT];
        break;
    case WG_VOICE_SELECT: ++wg_cv[!(eax & 0xffu) ? WGC_VSEL_NO : o->a[1] == wg_get(wg_w.vr) ? WGC_VSEL_EQUAL : WGC_VSEL_YES]; break;
    default: break;
    }
}

/* ---- a case: one world, a sequence of rows */
static int wg_side_begin(unsigned side, uint32_t seed, uint32_t answers) {
    uint32_t clock = 0x01000000u + (seed % 0x100000u) * 16u;
    lc_reset(side, 47, clock, 1);
    lc_loop.dispatch = &wg_dispatch;
    wg_side = side;
    wg_hash[side] = 0xcbf29ce484222325ull;
    wg_count[side] = 0;
    wg_len[side] = 0;
    wg_text[side][0] = 0;
    wg_answer_seed = answers;
    wg_seed = seed;
    wg_build(clock);
    if (side) {
        if (memcmp(&wg_w, &wg_w0, sizeof(wg_w))) return fail("hud widgets: the sides' worlds differ (heap order)");
        es2_sim_hud_init(&wg_hud, &lc_loop);
        wg_hud.trace = wg_trace;
        wg_hud.trace_capacity = WG_TRACE;
        es2_sim_hud_bind(&wg_hud);
    } else {
        wg_w0 = wg_w;
    }
    return 0;
}
static uint32_t wg_own_call(uint32_t va, const uint32_t *a, unsigned n) {
    es2_sim_result r;
    uint32_t regs[3] = {0, 0, 0};
    memset(&r, 0, sizeof(r));
    if (es2_sim_call(&wg_dispatch, va, a, n, regs, &r)) wg_bad = 1;
    return r.eax;
}
static unsigned wg_calls[WG_OPS];
static int wg_case(unsigned k) {
    unsigned ops = 1u + random32() % 8u;
    static wg_op list[8];
    uint32_t seed = random32(), answers = random32();
    static uint32_t eax[2][8];
    for (unsigned i = 0; i < ops; ++i) list[i].op = random32() % WG_OPS;
    for (unsigned side = 0; side < 2u; ++side) {
        if (wg_side_begin(side, seed, answers)) return 1;
        for (unsigned i = 0; i < ops && !wg_bad && !lc_bad; ++i) {
            wg_op *o = &list[i];
            if (side == 0) wg_args(o);
            wg_pre pre;
            size_t from = wg_len[0];
            if (side == 0) wg_take_pre(&pre);
            uint32_t real = wg_op_real(o->op);
            if (real && wg_real(real, side, 1)) return 1;
            uint32_t r = side ? wg_own_call(wg_ops[o->op].va, o->a, wg_ops[o->op].n) : lc_machine(wg_ops[o->op].va, o->a, wg_ops[o->op].n, NULL);
            if (real && wg_real(real, side, 0)) return 1;
            if (side == 0) o->full = o->op == WG_EVENT_DRAW && wg_saw('w', from);
            eax[side][i] = r & (o->full ? ~0u : wg_ops[o->op].mask);
            if (side == 0) { wg_cover(o, &pre, r, from); ++wg_calls[o->op]; }
        }
        if (wg_bad || lc_bad) { fprintf(stderr, "FAIL: hud widgets case %u: a call failed (side %u)\n", k, side); return 1; }
        wg_put32(wg_w.gc + 0x22cu, 0);   /* frame addresses of the side */
        su_take(side);
    }
    for (unsigned i = 0; i < ops; ++i)
        if (eax[0][i] != eax[1][i]) {
            fprintf(stderr, "FAIL: hud widgets case %u: %s (call %u) EAX %08lx / %08lx\n", k, wg_ops[list[i].op].name, i,
                    (unsigned long)eax[0][i], (unsigned long)eax[1][i]);
            return 1;
        }
    if (su_compare("hud widgets", k)) {
        fprintf(stderr, "  rows:");
        for (unsigned i = 0; i < ops; ++i) fprintf(stderr, " %s", wg_ops[list[i].op].name);
        fprintf(stderr, "\n");
        return 1;
    }
    if (wg_hash[0] != wg_hash[1] || wg_count[0] != wg_count[1]) {
        fprintf(stderr, "FAIL: hud widgets case %u: stub calls differ (%u / %u)\n  original: %s\n  own:      %s\n", k, wg_count[0], wg_count[1],
                wg_text[0], wg_text[1]);
        return 1;
    }
    /* The own coupling sites (hud.md, "The coupling sites of the HUD line"). */
    static const uint32_t clocks[] = {0x00434ecdu, 0x004351f6u, 0x00435635u, 0x00436f78u, 0x00438bdeu, 0x00436adfu, 0x00436f27u};
    static const uint32_t draws[] = {0x00435cbau, 0x00436a6cu, 0x00438d84u};
    /* The sound sites with their sound (voice: 0x64 volume, the argument is the
     * path 0x4d293a). */
    static const uint32_t sounds[][2] = {{0x00435e50u, 0x004d293au}, {0x00436cdau, 0x004d293au}, {0x00436c35u, 0x18u}, {0x00436c3fu, 0x19u},
                                         {0x00436c49u, 0x1au}, {0x00436c53u, 0x1au}, {0x00436c64u, 0x1au}, {0x00436c6eu, 0x1au},
                                         /* The CommandPort's voice, the path or its CD-root copy */
                                         {0x00436991u, 0x004d293au}, {0x00436991u, 0x004d28acu}};
    unsigned n = wg_hud.trace_count < WG_TRACE ? wg_hud.trace_count : WG_TRACE;
    for (unsigned i = 0; i < n; ++i) {
        if (wg_trace[i].kind == ES2_SIM_HUD_SOUND) {
            unsigned ok = 0;
            for (unsigned j = 0; j < sizeof(sounds) / sizeof(sounds[0]); ++j)
                ok |= wg_trace[i].site == sounds[j][0] && wg_trace[i].argument == sounds[j][1];
            if (!ok) {
                fprintf(stderr, "FAIL: hud widgets case %u: sound site 0x%08lx (argument %lx) is not in the list\n", k,
                        (unsigned long)wg_trace[i].site, (unsigned long)wg_trace[i].argument);
                return 1;
            }
            continue;
        }
        const uint32_t *list_sites = wg_trace[i].kind == ES2_SIM_HUD_CLOCK ? clocks : wg_trace[i].kind == ES2_SIM_HUD_STREAM_B ? draws : NULL;
        unsigned count = wg_trace[i].kind == ES2_SIM_HUD_CLOCK ? 7u : 3u, ok = 0;
        if (!list_sites) {
            fprintf(stderr, "FAIL: hud widgets case %u: an own coupling call of kind %u (none expected)\n", k, (unsigned)wg_trace[i].kind);
            return 1;
        }
        for (unsigned j = 0; j < count; ++j) ok |= wg_trace[i].site == list_sites[j];
        if (!ok) {
            fprintf(stderr, "FAIL: hud widgets case %u: coupling site 0x%08lx is not in D-31's list\n", k, (unsigned long)wg_trace[i].site);
            return 1;
        }
    }
    return 0;
}

static int hud_widgets_cases(void) {
    current_function = "hud widgets";
    es2_sim_identity_memory(&ir_memory);
    es2_sim_rtl_memory(&ir_memory, &su_rtl);
    su_data_pristine = malloc(SU_DATA_SIZE);
    su_snap[0] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    su_snap[1] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    ir_region = ww_alloc(IR_REGION_SIZE);
    lc_stack = VirtualAlloc(NULL, 0x10000u, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    lc_bits = ww_alloc(LC_BITS_SIZE);
    if (!su_data_pristine || !su_snap[0] || !su_snap[1] || !ir_region || !lc_stack || !lc_bits) return fail("hud widgets: memory");
    if (get32(mapped + (SU_COUNTER - 0x400000u))) return fail("hud widgets: the counter word is not free");
    su_pristine_take();
    if (!ir_patch_import("KERNEL32.dll", "GetTickCount", (void *)lc_tick)) return fail("hud widgets: import slot not found");
    if (!ir_patch_import("cw3220.dll", "_strcat", (void *)su_strcat) ||
        !ir_patch_import("cw3220.dll", "_strncat", (void *)wg_strncat) ||
        !ir_patch_import("cw3220.dll", "_strncpy", (void *)wg_strncpy) ||
        !ir_patch_import("cw3220.dll", "_strlen", (void *)su_strlen))
        return fail("hud widgets: string import slot not found");
    for (unsigned i = 0; i < WG_PATCHED; ++i)
        if (wg_patch(wg_patched[i].va, wg_patched[i].stub, wg_saved[i], 1)) return 1;
    if (wg_dispatch_setup()) return 1;
    int bad = 0;
    const unsigned cases = 6000;
    for (unsigned k = 0; !bad && k < cases; ++k) {
        current_case = k; current_case_valid = 1;
        bad = wg_case(k);
    }
    if (!bad) {
        printf("PASS: hud widgets: %u cases identical\n", cases);
        printf("HUD WIDGETS rows called:");
        for (unsigned i = 0; i < WG_OPS; ++i) printf(" %s %u%s", wg_ops[i].name, wg_calls[i], i + 1u < WG_OPS ? "," : "\n");
        printf("HUD WIDGETS coverage:");
        for (unsigned i = 0; i < WGC_COUNT; ++i) printf(" %s %u%s", wg_cv_names[i], wg_cv[i], i + 1u < WGC_COUNT ? "," : "\n");
        for (unsigned i = 0; i < WGC_COUNT; ++i)
            if (!wg_cv[i]) { fprintf(stderr, "FAIL: hud widgets: path '%s' never taken\n", wg_cv_names[i]); bad = 1; }
    }
    current_case_valid = 0;
    for (unsigned i = 0; i < WG_PATCHED; ++i) wg_patch(wg_patched[i].va, NULL, wg_saved[i], 0);
    put32(mapped + 0x2cbee4u, 0);
    memcpy(mapped + SU_DATA_RVA, su_data_pristine, SU_DATA_SIZE);
    return bad;
}
