/* The modal dialogs of DBSIM, original machine code against sim/src/dialogs.c
 * (--dialogs). It reuses startup_cases.h's snapshots (su_reset, su_take,
 * su_compare), loop_cases.h's virtual time, message model (lc_peek,
 * lc_translate, lc_tick) and loop, and input_layer_cases.h's pass-through
 * (ic_original).
 *
 * Both sides start from the pristine DATA and an empty heap, allocate the
 * same synthetic objects in the same order (the dialog, its panel and
 * gadgets, two ports, the cockpit, the surface, the tables) and fill them
 * from the same seeded generator, so every address is the same on both
 * sides. The hooks and the other packages' callees are stubs: on the
 * original side host functions jumped to from the original entries
 * (0x45a810 control record with the original pump, 0x45feb8, 0x459b94,
 * 0x488012, 0x487e60, 0x488143, 0x464a14, 0x438920, 0x480d44, 0x464260,
 * 0x433158, 0x430fb0) and import stubs (PeekMessageA, TranslateMessage,
 * DispatchMessageA to the original window procedure, SetCursor,
 * GetTickCount, GetClientRect, ClientToScreen, SetCursorPos, DefWindowProcA,
 * the CW3220 heap); on the own side services at the same VAs that record
 * the same letters and values. The vtable and driver slots hold host stubs
 * on both sides (the original calls them, the own side dispatches them as
 * services at their addresses). 0x486e7c, 0x48993a and the release hook
 * 0x4530bc run as the original's machine code on both sides.
 *
 * Parts:
 * - alert: 0x456000 or 0x457b00 over a random journal (the pump-keyed
 *   entries, WM_QUIT) and a random pass script written by the control
 *   record stub (keys, click and tab bytes, the point, the quit word, the
 *   virtual time moved on between passes); the script's end sets the quit
 *   word every pass.
 * - options: 0x456d68 with eleven gadgets (gadget 9, the second page, kept
 *   in state 2: not pressable) over the same inputs.
 * - rows: random sequences of the single rows (the open and close around
 *   them, the step, the key handler, focus, presses, ticks, selects, the
 *   release list, the ports, the key flag, the device, the panel rows, the
 *   options' step, label, page, store, the prefs toggles and cycles, the
 *   pen save, the label draw, the accessors).
 * - wndproc: WM_CLOSE and WM_QUERYENDSESSION through 0x466034 against
 *   es2_sim_window_proc (the quit dialog's answer, the demo word).
 * Compared: DATA 0x497000..0x6cc000 and the heap, the stub log (letters and values),
 * EAX (AL for 0x454e2c and the prefs rows), the messages dispatched, the peeks,
 * SetCursor, the journal's state, SetCursorPos against the own SetCursorPos row's
 * calls, the virtual time, and the own clock sites against main_loop.md, "the modal
 * dialogs". */
#pragma once
/* The headers this suite builds on (sim_main.c). */
#include "init_records_cases.h"
#include "startup_cases.h"
#include "loop_cases.h"
#include "input_layer_cases.h"
#include "es2/sim/cursor.h"
#include "es2/sim/dialogs.h"
#include "es2/sim/hud.h"
#include "es2/sim/input.h"

#define DC_TRACE 8192u
static es2_sim_entry dc_entries[4096];
static es2_sim_dispatch dc_dispatch;
static es2_sim_hud dc_hud;
static es2_sim_hud_event dc_trace[DC_TRACE];
static es2_sim_dialogs dc_dialogs;
static es2_sim_input dc_input;
static int dc_bad;
static unsigned dc_side;

/* ---- the stub log of a side: letters with up to three values, hashed;
 * the first part kept for the report */
static uint64_t dc_hash[2];
static unsigned dc_count[2];
static char dc_text[2][3072];
static size_t dc_len[2];
static void dc_note(char letter, uint32_t a, uint32_t b, uint32_t c) {
    uint64_t h = dc_hash[dc_side];
    const uint32_t v[4] = {(uint32_t)letter, a, b, c};
    for (unsigned i = 0; i < 4u; ++i) { h ^= v[i]; h *= 0x100000001b3ull; }
    dc_hash[dc_side] = h;
    ++dc_count[dc_side];
    if (dc_len[dc_side] + 48u < sizeof(dc_text[0]))
        dc_len[dc_side] += (size_t)sprintf(dc_text[dc_side] + dc_len[dc_side], "%c(%lx,%lx,%lx) ", letter, (unsigned long)a,
                                           (unsigned long)b, (unsigned long)c);
}
static uint32_t dc_strhash(uint32_t p) {
    uint32_t h = 2166136261u;
    for (const unsigned char *s = (const unsigned char *)(uintptr_t)p; p && *s; ++s) { h ^= *s; h *= 16777619u; }
    return h;
}
static uint32_t dc_get(uint32_t a) { return get32((BYTE *)(uintptr_t)a); }

/* ---- the pass script the control record stub writes (both sides) */
typedef struct dc_pass { uint16_t key; uint8_t click, tab, quit_word; int32_t x, y; uint32_t skip; } dc_pass;
static dc_pass dc_script[48];
static unsigned dc_script_count, dc_script_index;
#define DC_RECORD 0x004d22c6u
static uint32_t dc_control(uint32_t quit) {
    dc_note('R', quit, 0, 0);
    if (quit) put16(mapped + 0xd2532u, 1);
    memset(mapped + (DC_RECORD - 0x400000u), 0, 0x30);
    if (dc_script_index < dc_script_count) {
        const dc_pass *p = &dc_script[dc_script_index];
        put16(mapped + (DC_RECORD - 0x400000u), p->key);
        mapped[DC_RECORD - 0x400000u + 0xdu] = p->click;
        mapped[DC_RECORD - 0x400000u + 0x17u] = p->tab;
        put32(mapped + (DC_RECORD - 0x400000u + 2u), (uint32_t)p->x);
        put32(mapped + (DC_RECORD - 0x400000u + 6u), (uint32_t)p->y);
        if (p->quit_word) put16(mapped + 0xd24feu, 1);
        lc_vt.clock_ms += p->skip;
    } else {
        /* The script's end: the quit word ends every dialog; far past it the
         * time jumps so that held-back ends (release entries) expire. */
        put16(mapped + 0xd24feu, 1);
        if (dc_script_index > 150u) lc_vt.clock_ms += 1000u;
    }
    ++dc_script_index;
    return DC_RECORD;
}

/* ---- the original side's stubs (jumped to from the original entries) */
static uint32_t dc_answer, dc_port_a_object;
static unsigned dc_cursor_pos;
static uint32_t __cdecl dc_o_control(void) { return dc_control(lc_machine(0x00464c78u, NULL, 0, NULL)); }
static uint32_t __cdecl dc_o_command(uint32_t w) { dc_note('K', w, 0, 0); return dc_answer; }
static uint32_t __cdecl dc_o_store(uint32_t a, uint32_t b) { dc_note('S', a, b, 0); return 0; }
static uint32_t __cdecl dc_o_fill(uint32_t r, uint32_t s) { dc_note('I', r, s, 0); return 0; }
static uint32_t __cdecl dc_o_clear(uint32_t r, uint32_t p) { dc_note('E', r, dc_get(p), dc_get(p + 4u)); return 0; }
static uint32_t __cdecl dc_o_blit(uint32_t a, uint32_t b, uint32_t s) { dc_note('T', a, b, s); return 0; }
static uint32_t __cdecl dc_o_present(void) { dc_note('V', 0, 0, 0); return 1; }
static uint32_t __cdecl dc_o_label(uint32_t a, uint32_t b) { dc_note('L', a, b, 0); return 0; }
static uint32_t __cdecl dc_o_gc(uint32_t a) { dc_note('G', a, 0, 0); return 0; }
static uint32_t __cdecl dc_o_sound(uint32_t a) { dc_note('A', a, 0, 0); return 0; }
static uint32_t __cdecl dc_o_port_a(void) { dc_note('W', 0, 0, 0); return dc_port_a_object; }
static uint32_t __cdecl dc_o_font(uint32_t path) { dc_note('F', dc_strhash(path), 0, 0); return 0x00556000u; }
/* 0x492bec (mission_load.h's path join) is stubbed on both sides: the name itself is
 * the path. */
static uint32_t __cdecl dc_o_path(uint32_t name, uint32_t dir) { dc_note('P', name, dir, 0); return name; }
/* The table, callback and driver stubs: their host addresses are the VAs
 * both sides call. */
static uint32_t __cdecl dc_draw(uint32_t d) { dc_note('D', d, 0, 0); return 0; }
static uint32_t __cdecl dc_gadget_draw(uint32_t g) { dc_note('g', g, 0, 0); return 0; }
static uint32_t __cdecl dc_gadget_redraw(uint32_t g) { dc_note('r', g, 0, 0); return 0; }
static uint32_t __cdecl dc_part(uint32_t p) { dc_note('x', p, 0, 0); return 0; }
static uint32_t __cdecl dc_port(uint32_t q) { dc_note('p', q, 0, 0); return 0; }
static uint32_t __cdecl dc_hook(void) { dc_note('h', 0, 0, 0); return 0; }
static uint32_t __cdecl dc_cb_open(void) { dc_note('o', 0, 0, 0); return 0; }
static uint32_t __cdecl dc_cb_close(void) { dc_note('c', 0, 0, 0); return 0; }
static uint32_t __cdecl dc_driver_page(uint32_t p) { dc_note('d', p, 0, 0); return 0; }
static uint32_t __cdecl dc_driver_sync(void) { dc_note('s', 0, 0, 0); return 0; }
static uint32_t __stdcall dc_handler(uint32_t h, uint32_t m, uint32_t w, uint32_t l) { (void)h; dc_note('H', m, w, l); return 1; }
/* The imports. */
static uint32_t __stdcall dc_client_rect(uint32_t w, uint32_t *r) { (void)w; dc_note('C', 0, 0, 0); r[0] = 0; r[1] = 0; r[2] = 640; r[3] = 480; return 1; }
static uint32_t __stdcall dc_client_to_screen(uint32_t w, uint32_t p) { (void)w; (void)p; return 1; }
static uint32_t __stdcall dc_set_cursor_pos(int32_t x, int32_t y) { (void)x; (void)y; ++dc_cursor_pos; return 1; }
static uint32_t __stdcall dc_dispatch_message(const uint32_t *msg) {
    lc_record(0, msg[1], msg[2], msg[3]);
    es2_hook_result r;
    uint32_t a[4] = {msg[0], msg[1], msg[2], msg[3]};
    es2_hook_call c = {mapped + 0x66034u, ES2_ABI_STDCALL, 4, a, initial_fpu, &r, NULL};
    es2_hook_machine_call(&c);
    return r.eax;
}
static int dc_patch(uint32_t va, void *target, BYTE saved[5], int install) {
    BYTE *site = mapped + (va - 0x400000u);
    DWORD previous;
    if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &previous)) return fail("dialogs: stub protection");
    if (install) { memcpy(saved, site, 5); site[0] = 0xe9; put32(site + 1, (uint32_t)(uintptr_t)target - (uint32_t)(uintptr_t)(site + 5)); }
    else memcpy(site, saved, 5);
    if (!VirtualProtect(site, 5, previous, &previous)) return fail("dialogs: stub restore protection");
    FlushInstructionCache(GetCurrentProcess(), site, 5);
    return 0;
}
static const struct { uint32_t va; void *stub; } dc_patched[] = {
    {0x0045a810u, (void *)dc_o_control}, {0x0045feb8u, (void *)dc_o_command}, {0x00459b94u, (void *)dc_o_store},
    {0x00488012u, (void *)dc_o_fill}, {0x00487e60u, (void *)dc_o_clear}, {0x00488143u, (void *)dc_o_blit},
    {0x00464a14u, (void *)dc_o_present}, {0x00438920u, (void *)dc_o_label}, {0x00480d44u, (void *)dc_o_gc},
    {0x00464260u, (void *)dc_o_sound}, {0x00433158u, (void *)dc_o_port_a}, {0x00430fb0u, (void *)dc_o_font},
    {0x00492becu, (void *)dc_o_path},
};
static BYTE dc_saved[sizeof(dc_patched) / sizeof(dc_patched[0])][5];

/* ---- the own side: the sim services, the stubs as services, the
 * pass-throughs */
static es2_sim_service_fn dc_client_rect_real;
static void *dc_client_rect_user;
static void dc_s_client_rect(es2_sim_service_call *call, void *user) {
    (void)user;
    dc_note('C', 0, 0, 0);
    dc_client_rect_real(call, dc_client_rect_user);
}
static void dc_s_stub(es2_sim_service_call *call, void *user) {
    char letter = (char)(uintptr_t)user;
    const uint32_t *s = call->slots;
    switch (letter) {
    case 'R': {
        uint32_t quit = 0;
        if (es2_sim_pump(&lc_loop, &quit)) { call->refused = 1; call->reason = "the pump was refused"; return; }
        call->eax = dc_control(quit);
        return;
    }
    case 'K': dc_note('K', s[0], 0, 0); call->eax = dc_answer; return;
    case 'S': case 'I': case 'L': dc_note(letter, s[0], s[1], 0); return;
    case 'E': dc_note('E', s[0], dc_get(s[1]), dc_get(s[1] + 4u)); return;
    case 'T': dc_note('T', s[0], s[1], s[2]); return;
    case 'V': dc_note('V', 0, 0, 0); call->eax = 1; return;
    case 'W': dc_note('W', 0, 0, 0); call->eax = dc_port_a_object; return;
    case 'F': dc_note('F', dc_strhash(s[0]), 0, 0); call->eax = 0x00556000u; return;
    case 'P': dc_note('P', s[0], s[1], 0); call->eax = s[0]; return;
    case 'H': lc_record(1, s[1], s[2], s[3]); dc_note('H', s[1], s[2], s[3]); call->eax = 1; return;
    case 'h': case 'o': case 'c': case 's': dc_note(letter, 0, 0, 0); return;
    default: dc_note(letter, s[0], 0, 0); return;   /* G A D g r x p d */
    }
}
static void dc_refuse(const es2_sim_refusal *r, void *user) {
    (void)user;
    fprintf(stderr, "FAIL: own dialogs: refused call to 0x%08lx (%s): %s\n", (unsigned long)r->va, r->name ? r->name : "?",
            r->reason ? r->reason : "");
    dc_bad = 1;
}
static int dc_dispatch_setup(void) {
    es2_sim_dispatch_init(&dc_dispatch, dc_entries, 4096, dc_refuse, NULL);
    static es2_sim_services s;
    s.memory = &ir_memory;
    s.heap = &ir_heap;
    s.vt = &lc_vt;
    s.fs0 = NULL;
    if (es2_sim_register_services(&dc_dispatch, &s)) return fail("dialogs: dispatch");
    const struct { uint32_t va; char letter; unsigned slots, stdcall_bytes; } stubs[] = {
        {0x0045a810u, 'R', 0, 0}, {0x0045feb8u, 'K', 1, 0}, {0x00459b94u, 'S', 2, 0},
        {(uint32_t)(uintptr_t)dc_draw, 'D', 1, 0}, {(uint32_t)(uintptr_t)dc_gadget_draw, 'g', 1, 0},
        {(uint32_t)(uintptr_t)dc_gadget_redraw, 'r', 1, 0}, {(uint32_t)(uintptr_t)dc_part, 'x', 1, 0},
        {(uint32_t)(uintptr_t)dc_port, 'p', 1, 0}, {(uint32_t)(uintptr_t)dc_hook, 'h', 0, 0},
        {(uint32_t)(uintptr_t)dc_cb_open, 'o', 0, 0}, {(uint32_t)(uintptr_t)dc_cb_close, 'c', 0, 0},
        {(uint32_t)(uintptr_t)dc_driver_page, 'd', 1, 0}, {(uint32_t)(uintptr_t)dc_driver_sync, 's', 0, 0},
        {(uint32_t)(uintptr_t)dc_handler, 'H', 4, 16},
    };
    for (unsigned i = 0; i < sizeof(stubs) / sizeof(stubs[0]); ++i)
        if (es2_sim_dispatch_add_service(&dc_dispatch, stubs[i].va, "dialog stub", dc_s_stub, (void *)(uintptr_t)stubs[i].letter,
                                         stubs[i].stdcall_bytes, (uint8_t)stubs[i].slots))
            return fail("dialogs: dispatch (stub)");
    static const struct { uint32_t va; unsigned slots; const char *name; } passed[] = {
        {0x004530bcu, 2, "button release (original)"},
    };
    for (unsigned i = 0; i < sizeof(passed) / sizeof(passed[0]); ++i)
        if (es2_sim_dispatch_add_service(&dc_dispatch, passed[i].va, passed[i].name, ic_original, (void *)(uintptr_t)passed[i].slots, 0,
                                         (uint8_t)passed[i].slots))
            return fail("dialogs: dispatch (pass-through)");
    const char *error = NULL;
    uint32_t bad = 0;
    if (es2_sim_dispatch_seal(&dc_dispatch, &error, &bad)) return fail("dialogs: dispatch seal");
    /* Rows that need the game's objects: replaced (after the seal sorted
     * the table) by the recording stubs (0x488012/0x487e60: gfx_primitives.h's rows). */
    static const struct { uint32_t va; char letter; } replaced[] = {
        {0x00464a14u, 'V'}, {0x00438920u, 'L'}, {0x00480d44u, 'G'}, {0x00464260u, 'A'}, {0x00433158u, 'W'}, {0x00488143u, 'T'}, {0x00492becu, 'P'},
        {0x00488012u, 'I'}, {0x00487e60u, 'E'}, /* gfx_primitives.h's rows */
        {0x00430fb0u, 'F'},                     /* presentation_loads.h's row font_load */
    };
    for (unsigned i = 0; i < sizeof(replaced) / sizeof(replaced[0]); ++i) {
        es2_sim_entry *e = (es2_sim_entry *)es2_sim_dispatch_find(&dc_dispatch, replaced[i].va);
        if (!e || e->kind != ES2_SIM_SERVICE) return fail("dialogs: service missing");
        e->service = dc_s_stub;
        e->service_user = (void *)(uintptr_t)replaced[i].letter;
    }
    /* 0x486e7c is gfx_blit.h's row: passed to the original. */
    es2_sim_entry *e = (es2_sim_entry *)es2_sim_dispatch_find(&dc_dispatch, 0x00486e7cu);
    if (!e || e->kind != ES2_SIM_SERVICE) return fail("dialogs: pen set missing");
    e->service = ic_original;
    e->service_user = (void *)(uintptr_t)1u;
    /* 0x48993a (pen set B) is gfx_bmp8.h's row: passed to the original. */
    e = (es2_sim_entry *)es2_sim_dispatch_find(&dc_dispatch, 0x0048993au);
    if (!e || e->kind != ES2_SIM_SERVICE) return fail("dialogs: pen set B missing");
    e->service = ic_original;
    e->service_user = (void *)(uintptr_t)1u;
    e = (es2_sim_entry *)es2_sim_dispatch_find(&dc_dispatch, 0x00496970u);
    if (!e || e->kind != ES2_SIM_SERVICE) return fail("dialogs: client_rect missing");
    dc_client_rect_real = e->service;
    dc_client_rect_user = e->service_user;
    e->service = dc_s_client_rect;
    return 0;
}

/* ---- the synthetic world of a case, built the same on both sides from a
 * seeded generator (the addresses come from the emulated heap in order) */
static uint32_t dc_seed;
static uint32_t dc_rand(uint32_t n) {
    dc_seed = dc_seed * 1103515245u + 12345u;
    return (dc_seed >> 8) % n;
}
typedef struct dc_world {
    uint32_t d, p, g[12], count, fallback, port[2], cockpit, surface, hold, labels, tables, device, item[2], spare;
    uint32_t vt_dialog, vt_gadget, vt_part, vt_port, now;
} dc_world;
static dc_world dc_w, dc_w0;
static uint32_t dc_alloc(uint32_t n) { return (uint32_t)(uintptr_t)ir_calloc(1, n); }
#define DC_ALERT 0
#define DC_OPTIONS 1
#define DC_ROWS 2
#define DC_WNDPROC 3
static void dc_put8(uint32_t a, uint8_t v) { *(BYTE *)(uintptr_t)a = v; }
static void dc_put16(uint32_t a, uint16_t v) { put16((BYTE *)(uintptr_t)a, v); }
static void dc_put32(uint32_t a, uint32_t v) { put32((BYTE *)(uintptr_t)a, v); }
static uint32_t dc_near(uint32_t now) { return now + dc_rand(12u) - 4u; }
/* Builds the world for `kind`, after lc_reset; `select` chooses the dialog's
 * select row. */
static void dc_build(unsigned kind, uint32_t clock_start) {
    dc_world *w = &dc_w;
    memset(w, 0, sizeof(*w));
    w->count = kind == DC_OPTIONS ? 11u : 1u + dc_rand(kind == DC_ALERT ? 4u : 11u);
    w->d = dc_alloc(0x800u);
    w->p = dc_alloc(0x400u);
    for (unsigned k = 0; k < 12u; ++k) w->g[k] = dc_alloc(0x40u);
    w->port[0] = dc_alloc(0x600u);
    w->port[1] = dc_alloc(0x600u);
    w->cockpit = dc_alloc(0x300u);
    w->surface = dc_alloc(0x100u);
    w->hold = dc_alloc(0x10u);
    w->labels = dc_alloc(0x100u);
    w->tables = dc_alloc(0x100u);
    w->device = dc_alloc(0x100u);
    w->item[0] = dc_alloc(0x40u);
    w->item[1] = dc_alloc(0x40u);
    w->spare = dc_alloc(0x30u);
    w->vt_dialog = w->tables;
    w->vt_gadget = w->tables + 0x40u;
    w->vt_part = w->tables + 0x80u;
    w->vt_port = w->tables + 0xc0u;
    w->now = (clock_start - get32(mapped + 0xd3b78u)) >> 4;   /* the clock at the start */
    dc_port_a_object = w->port[0];
    /* The tables. */
    dc_put32(w->vt_dialog, (uint32_t)(uintptr_t)dc_draw);
    static const uint32_t selects[3] = {0x0045617cu, 0x00457c4cu, 0x004573c4u};
    dc_put32(w->vt_dialog + 0xcu, kind == DC_OPTIONS ? selects[2] : kind == DC_ALERT ? selects[dc_rand(2u)] : selects[dc_rand(3u)]);
    dc_put32(w->vt_dialog + 0x10u, 0x00454e2cu);
    dc_put32(w->vt_gadget, (uint32_t)(uintptr_t)dc_gadget_draw);
    dc_put32(w->vt_gadget + 4u, (uint32_t)(uintptr_t)dc_gadget_redraw);
    dc_put32(w->vt_gadget + 8u, 0x0045509cu);
    dc_put32(w->vt_gadget + 0xcu, 0x00455080u);
    dc_put32(w->vt_part + 8u, (uint32_t)(uintptr_t)dc_part);
    dc_put32(w->vt_port + 0xcu, (uint32_t)(uintptr_t)dc_port);
    /* The dialog. */
    uint32_t d = w->d;
    dc_put32(d, w->vt_dialog);
    dc_put32(d + 4u, dc_rand(200u));
    dc_put32(d + 8u, dc_rand(200u));
    dc_put32(d + 0x1cu, dc_rand(100u)); dc_put32(d + 0x20u, dc_rand(100u));
    dc_put32(d + 0x24u, 200u + dc_rand(300u)); dc_put32(d + 0x28u, 100u + dc_rand(200u));
    dc_put32(d + 0x2cu, dc_rand(2u) ? 0x33000000u + dc_rand(0x10000u) : 0u);
    dc_put32(d + 0x44u, 0x00444000u + dc_rand(0x100u));
    dc_put32(d + 0x285u, w->p);
    dc_put32(d + 0x289u, dc_rand(2u) ? 0x00555000u + dc_rand(0x100u) : 0u);
    dc_put32(d + 0x28du, 0x33330000u + dc_rand(0x100u));
    dc_put32(d + 0x2cdu, w->count);
    dc_put8(d + 0x2ffu, (uint8_t)dc_rand(2u));
    w->fallback = dc_rand(4u) ? dc_rand(w->count) : 0xffffffffu;
    if (kind == DC_OPTIONS) w->fallback = dc_rand(4u) ? 10u : dc_rand(9u);
    dc_put32(d + 0x2fbu, w->fallback);
    dc_put32(d + 0x2f7u, kind == DC_ROWS && dc_rand(2u) ? dc_rand(w->count) : 0xffffffffu);
    dc_put32(d + 0x3b6u, kind == DC_ROWS && dc_rand(2u) ? dc_rand(w->count) : 0xffffffffu);
    dc_put32(d + 0x356u, 0xffffffffu);
    dc_put8(d + 0x331u, kind == DC_ROWS && dc_rand(4u) == 0 ? (uint8_t)(1u + dc_rand(3u)) : 0u);
    for (unsigned k = 0; k < 9u; ++k) dc_put32(d + 0x332u + 4u * k, w->labels + 0x10u * k);
    for (unsigned k = 0; k < 10u; ++k) dc_put32(d + 0x38eu + 4u * k, 0x00aa0000u + dc_rand(0x1000u));   /* the value tables +0x38e, +0x396, +0x3a2..+0x3b5 */
    for (unsigned k = 0; k < 8u; ++k) dc_put32(d + 0x362u + 4u * k, 0x00ab0000u + dc_rand(0x1000u));
    for (unsigned k = 0; k < 0x40u; k += 4u) dc_put32(w->labels + k, 0x00ac0000u + dc_rand(0x1000u));
    dc_put32(d + 0x3bau, dc_rand(2u) ? (uint32_t)(uintptr_t)dc_hook : 0u);
    if (kind != DC_ALERT)
        for (unsigned k = 0; k < 9u; ++k) dc_put8(d + 0x3beu + k, dc_rand(3u) == 0 ? (uint8_t)(1u + dc_rand(2u)) : 0u);
    for (unsigned k = 0; k < 12u; ++k) {
        uint32_t g = w->g[k], x0 = 30u * k + dc_rand(5u), y0 = 10u + dc_rand(5u);
        dc_put32(g, x0); dc_put32(g + 4u, y0); dc_put32(g + 8u, x0 + 10u + dc_rand(20u)); dc_put32(g + 0xcu, y0 + 8u + dc_rand(10u));
        dc_put32(g + 0x17u, w->vt_gadget);
        dc_put16(g + 0x1bu, (uint16_t)dc_rand(4u));
        dc_put8(g + 0x1fu, dc_rand(3u) == 0 ? (uint8_t)(1u + dc_rand(2u)) : 0u);
        dc_put32(g + 0x20u, w->vt_part);
        dc_put32(g + 0x24u, d);
        dc_put32(d + 0x291u + 4u * k, g);   /* all twelve slots: the rows part raises the count */
    }
    if (kind == DC_OPTIONS) dc_put16(w->g[9] + 0x1bu, 2);   /* the second page: never pressed */
    /* The panel's release list. */
    unsigned entries = dc_rand(3u);
    dc_put16(w->p + 0x396u, (uint16_t)entries);
    for (unsigned k = 0; k < entries; ++k) {
        dc_put32(w->p + 0x356u + 8u * k, w->g[dc_rand(w->count)]);
        dc_put32(w->p + 0x35au + 8u * k, dc_near(w->now));
    }
    /* The ports. */
    for (unsigned i = 0; i < 2u; ++i) {
        uint32_t q = w->port[i], b = q + 4u;
        dc_put32(q, w->vt_port);
        unsigned n = dc_rand(4u);
        dc_put16(b + 0x21cu, (uint16_t)n);
        for (unsigned k = 0; k < n; ++k) {
            dc_put32(b + 0x1eau + 4u * k, k + 1u);
            dc_put32(b + (k + 1u) * 0x31u + 0x24u, dc_near(w->now));
            dc_put32(b + (k + 1u) * 0x31u + 0x28u, dc_near(w->now));
        }
        if (dc_rand(4u)) {
            dc_put32(q + 0x49au, w->item[i]);
            dc_put32(w->item[i] + 0x1cu, dc_near(w->now));
            dc_put32(w->item[i] + 0x20u, dc_near(w->now) + 5u);
        }
        if (dc_rand(4u) == 0) { dc_put32(q + 0x4ccu, w->now - 1u - dc_rand(5u)); dc_put16(q + 0x4d0u, (uint16_t)(1u + dc_rand(2u))); }
        dc_put8(q + 0x4cbu, (uint8_t)dc_rand(2u));
    }
    /* The globals. */
    put32(mapped + 0xd3610u, 0x00010001u);
    put32(mapped + 0xd3040u, dc_rand(2u) ? 640u : 1280u);
    put32(mapped + 0xd3044u, dc_rand(2u) ? 480u : 960u);
    put32(mapped + 0x9b088u, w->cockpit);
    dc_put32(w->cockpit + 0x20bu, w->port[1]);
    put32(mapped + 0x2c5ed0u, w->surface);
    dc_put32(w->surface + 0x88u, 0x11110000u + dc_rand(0x100u));
    dc_put32(w->surface + 0x8cu, 0x22220000u + dc_rand(0x100u));
    put32(mapped + 0x2c5f70u, 0x00777700u + dc_rand(0x100u));
    put32(mapped + 0xd1e04u, w->hold);
    dc_put32(w->hold, dc_rand(0x10000u));
    put32(mapped + 0x9dd90u, dc_rand(4u) ? 0x00555000u + dc_rand(0x100u) : 0u);
    put32(mapped + 0xa57ecu, (uint32_t)(uintptr_t)dc_driver_page);
    put32(mapped + 0xa57f0u, (uint32_t)(uintptr_t)dc_driver_page);
    put32(mapped + 0xa5828u, (uint32_t)(uintptr_t)dc_driver_sync);
    put32(mapped + 0xa5824u, (uint32_t)(uintptr_t)dc_driver_sync);
    put16(mapped + 0xd2566u, (uint16_t)dc_rand(2u));
    put32(mapped + 0xd1e60u, 1u + dc_rand(20u));
    put32(mapped + 0xd1e64u, dc_near(w->now));
    put32(mapped + 0x9f8ecu, dc_rand(2u) ? 0x00123400u + dc_rand(0x100u) : 0u);
    put32(mapped + 0x9dd94u, dc_rand(2u) ? (uint32_t)(uintptr_t)dc_cb_open : 0u);
    put32(mapped + 0x9dd98u, dc_rand(2u) ? (uint32_t)(uintptr_t)dc_cb_close : 0u);
    put16(mapped + 0x9dd8cu, (uint16_t)dc_rand(3u));
    mapped[0x9dbdfu] = (BYTE)dc_rand(2u);
    mapped[0x9eaa4u] = (BYTE)dc_rand(2u);
    mapped[0x9dbecu] = (BYTE)dc_rand(2u);
    put32(mapped + 0xa3618u, dc_rand(0x10000u));
    put32(mapped + 0xa3628u, dc_rand(0x10000u));
    put32(mapped + 0xa362cu, dc_rand(0x10000u));
    put32(mapped + 0xa3646u, dc_rand(2u));
    put32(mapped + 0xa361cu, dc_rand(2u));
    put32(mapped + 0xd1294u, dc_rand(1000u));
    for (unsigned k = 0; k < 16u; ++k) { mapped[0xd1f38u + k] = (BYTE)dc_rand(3u); mapped[0xd1fa4u + k] = (BYTE)dc_rand(3u); }
    memset(mapped + 0xd1fdcu, 0, 0x40);   /* the prefs callbacks: none */
    for (unsigned k = 0; k < 20u; ++k) put16(mapped + 0x9e2b2u + 2u * k, (uint16_t)dc_rand(4u));
    mapped[0x9e9a5u] = (BYTE)dc_rand(2u);
    put32(mapped + 0xd3720u, dc_rand(2u) ? 0x00010003u : 0u);
    put32(mapped + 0xd257bu, dc_rand(4u));
    put16(mapped + 0xd2530u, (uint16_t)dc_rand(2u));
    put16(mapped + 0xd2532u, kind == DC_ROWS && dc_rand(8u) == 0 ? 1u : 0u);
    put16(mapped + 0xd24feu, 0);
    uint32_t id = dc_rand(3u);
    put32(mapped + 0xd24a0u, id);
    put32(mapped + 0xd2468u + 4u * id, id ? w->device + 0x3au : 0u);
    for (unsigned k = 0; k < 0x30u; ++k) mapped[DC_RECORD - 0x400000u + k] = (BYTE)dc_rand(256u);
    put16(mapped + (DC_RECORD - 0x400000u), (uint16_t)dc_rand(2u));
    for (unsigned k = 0; k < 8u; ++k) { mapped[0xd22f5u + k] = (BYTE)dc_rand(2u); mapped[0xd22fdu + k] = (BYTE)dc_rand(2u); }
    for (unsigned k = 0; k < 10u; ++k) put32(mapped + 0xd3b30u + 4u * k, 0);
    if (kind != DC_WNDPROC) put32(mapped + 0xd3b30u, (uint32_t)(uintptr_t)dc_handler);
    dc_answer = dc_rand(4u);
}

/* ---- a side's run around a part */
static char dc_journal_text[4096];
static size_t dc_journal_size;
static struct { uint32_t peeks, dispatches, cursor_calls, last_cursor, cursor_pos, def_calls; es2_vt vt; } dc_state[2];
static int dc_side_begin(unsigned side, unsigned kind, uint32_t seed) {
    uint32_t clock = 0x01000000u + (seed % 0x100000u) * 16u;
    lc_reset(side, 47, clock, 1);
    lc_loop.dispatch = &dc_dispatch;
    dc_side = side;
    dc_hash[side] = 0xcbf29ce484222325ull;
    dc_count[side] = 0;
    dc_len[side] = 0;
    dc_text[side][0] = 0;
    dc_cursor_pos = 0;
    es2_sim_cursor_calls_reset();
    dc_script_index = 0;
    es2_journal_free(&lc_journal[side]);
    es2_journal_init(&lc_journal[side]);
    if (es2_journal_load_text(&lc_journal[side], dc_journal_text, dc_journal_size, NULL)) return fail("dialogs: journal");
    lc_posted_count = 0; lc_quit = 0; lc_peeks = 0;
    lc_dispatch_count[side] = 0; lc_cursor_calls[side] = 0; lc_last_cursor[side] = 0; lc_def_calls[side] = 0;
    dc_seed = seed;
    dc_build(kind, clock);
    if (side) {
        if (memcmp(&dc_w, &dc_w0, sizeof(dc_w))) return fail("dialogs: the sides' worlds differ (heap order)");
        lc_loop.journal = &lc_journal[1];
        es2_sim_hud_init(&dc_hud, &lc_loop);
        dc_hud.trace = dc_trace;
        dc_hud.trace_capacity = DC_TRACE;
        es2_sim_hud_bind(&dc_hud);
        es2_sim_dialogs_init(&dc_dialogs);
        dc_dialogs.pass_limit = 400;
        es2_sim_dialogs_bind(&dc_dialogs);
        es2_sim_input_headless(&dc_input, &lc_loop, &lc_journal[1], 640, 480);
        es2_sim_input_bind(&dc_input);
    } else {
        dc_w0 = dc_w;
    }
    return 0;
}
static void dc_side_end(unsigned side) {
    if (side) {
        dc_state[1].peeks = lc_loop.platform.peeks;
        dc_state[1].dispatches = lc_loop.platform.dispatches;
        dc_state[1].cursor_calls = lc_loop.platform.set_cursor;
        dc_state[1].last_cursor = lc_loop.platform.last_cursor;
        dc_state[1].cursor_pos = es2_sim_cursor_calls_get()->set_pos;   /* the SetCursorPos row */
        dc_state[1].def_calls = lc_loop.platform.def_window_proc;
    } else {
        dc_state[0].peeks = lc_peeks;
        dc_state[0].dispatches = lc_dispatch_count[0];
        dc_state[0].cursor_calls = lc_cursor_calls[0];
        dc_state[0].last_cursor = lc_last_cursor[0];
        dc_state[0].cursor_pos = dc_cursor_pos;
        dc_state[0].def_calls = lc_def_calls[0];
    }
    dc_state[side].vt = lc_vt;
    for (unsigned i = 0; i < 10u; ++i) put32(mapped + 0xd3b30u + 4u * i, 0);
    for (unsigned k = 0; k < sizeof(dc_patched) / sizeof(dc_patched[0]); ++k) (void)k;
    su_take(side);
}
static int dc_compare(const char *what, unsigned k) {
    if (su_compare(what, k)) return 1;
    const es2_journal *a = &lc_journal[0], *b = &lc_journal[1];
    if (dc_hash[0] != dc_hash[1] || dc_count[0] != dc_count[1]) {
        fprintf(stderr, "FAIL: dialogs %s case %u: stub calls differ (%u / %u)\n  original: %s\n  own:      %s\n", what, k, dc_count[0],
                dc_count[1], dc_text[0], dc_text[1]);
        return 1;
    }
    if (memcmp(&dc_state[0], &dc_state[1], sizeof(dc_state[0])) ||
        lc_dispatch_count[0] != lc_dispatch_count[1] ||
        memcmp(lc_dispatched[0], lc_dispatched[1], sizeof(lc_dispatched[0][0]) * (lc_dispatch_count[0] < 64u ? lc_dispatch_count[0] : 64u))) {
        fprintf(stderr, "FAIL: dialogs %s case %u: peeks %lu / %lu, dispatched %lu / %lu, SetCursor %lu / %lu, SetCursorPos %lu / %lu, "
                        "DefWindowProc %lu / %lu, virtual time %s\n", what, k, (unsigned long)dc_state[0].peeks, (unsigned long)dc_state[1].peeks,
                (unsigned long)dc_state[0].dispatches, (unsigned long)dc_state[1].dispatches, (unsigned long)dc_state[0].cursor_calls,
                (unsigned long)dc_state[1].cursor_calls, (unsigned long)dc_state[0].cursor_pos, (unsigned long)dc_state[1].cursor_pos,
                (unsigned long)dc_state[0].def_calls, (unsigned long)dc_state[1].def_calls,
                memcmp(&dc_state[0].vt, &dc_state[1].vt, sizeof(es2_vt)) ? "differs" : "same");
        return 1;
    }
    if (a->injected != b->injected || a->pump_count != b->pump_count || a->pump_frame != b->pump_frame ||
        memcmp(a->keys, b->keys, sizeof(a->keys)) || a->mouse_x != b->mouse_x || a->mouse_y != b->mouse_y ||
        memcmp(a->cursor, b->cursor, sizeof(a->cursor))) {
        fprintf(stderr, "FAIL: dialogs %s case %u: the journals' state differs (injected %lu / %lu)\n", what, k, (unsigned long)a->injected,
                (unsigned long)b->injected);
        return 1;
    }
    /* The own clock sites (main_loop.md, "the modal dialogs"). */
    static const uint32_t sites[] = {0x00435b68u, 0x004352a1u, 0x00435b9au, 0x0045311au, 0x00454ac0u, 0x00454b51u};
    unsigned n = dc_hud.trace_count < DC_TRACE ? dc_hud.trace_count : DC_TRACE;
    for (unsigned i = 0; i < n; ++i) {
        if (dc_trace[i].kind != ES2_SIM_HUD_CLOCK) continue;
        unsigned ok = 0;
        for (unsigned j = 0; j < sizeof(sites) / sizeof(sites[0]); ++j) ok |= dc_trace[i].site == sites[j];
        if (!ok) {
            fprintf(stderr, "FAIL: dialogs %s case %u: clock site 0x%08lx is not in D-32's list\n", what, k, (unsigned long)dc_trace[i].site);
            return 1;
        }
    }
    return 0;
}
/* A random journal (frames from 0, the pump keys) and pass script. */
static void dc_inputs(unsigned passes) {
    size_t n = 0;
    unsigned entries = random32() % 12u;
    static const uint32_t messages[] = {0x100u, 0x101u, 0x104u, 0x105u, 0x200u, 0x201u, 0x202u, 0x100u, 0x12u};
    static const uint32_t keys[] = {0x41u, 0x57u, 0x0du, 0x20u, 0x1bu, 0x25u, 0x70u, 0x10u};
    uint32_t frame = 0;
    for (unsigned i = 0; i < entries && n + 128 < sizeof(dc_journal_text); ++i) {
        if (random32() % 4u == 0) frame += random32() % 2u;
        uint32_t m = messages[random32() % 9u], w = m < 0x200u ? keys[random32() % 8u] : random32() % 3u;
        uint32_t l = m < 0x200u ? 1u | (random32() % 0x60u) << 16 : (random32() % 640u) | (random32() % 480u) << 16;
        if (m == 0x12u) { w = 0; l = 0; }
        unsigned p = random32() % 2u ? random32() % 12u : 0u;
        n += (size_t)sprintf(dc_journal_text + n, "{\"f\":%lu,\"p\":%u,\"api\":\"msg\",\"a\":%lu,\"b\":%lu,\"c\":%ld}\n", (unsigned long)frame, p,
                             (unsigned long)m, (unsigned long)w, (long)(int32_t)l);
    }
    dc_journal_size = n;
    static const uint16_t words[] = {0, 0, 0, 1, 0xf, 0x52, 0x1c, 0x80f, 0x99, 1, 0x1c};
    dc_script_count = passes;
    for (unsigned p = 0; p < passes; ++p) {
        dc_pass *s = &dc_script[p];
        s->key = words[random32() % 11u];
        s->click = random32() % 5u == 0;
        s->tab = random32() % 5u == 0;
        s->quit_word = random32() % 20u == 0;
        s->x = (int32_t)(random32() % 400u) - 20;
        s->y = (int32_t)(random32() % 60u) - 10;
        s->skip = random32() % 4u == 0 ? random32() % 400u : random32() % 20u;
    }
}
static uint32_t dc_own_call(uint32_t va, const uint32_t *a, unsigned n) {
    es2_sim_result r;
    uint32_t regs[3] = {0, 0, 0};
    memset(&r, 0, sizeof(r));
    if (es2_sim_call(&dc_dispatch, va, a, n, regs, &r)) dc_bad = 1;
    return r.eax;
}

/* ---- alert and options: the modal loops */
static unsigned dc_passes_total, dc_journal_delivered, dc_ends[4];
static int dc_modal_case(unsigned k, unsigned kind) {
    uint32_t va = kind == DC_OPTIONS ? 0x00456d68u : random32() % 2u ? 0x00456000u : 0x00457b00u;
    dc_inputs(1u + random32() % 12u);
    uint32_t seed = random32(), eax[2];
    for (unsigned side = 0; side < 2u; ++side) {
        if (dc_side_begin(side, kind, seed)) return 1;
        uint32_t a[1] = {dc_w.d};
        eax[side] = side ? dc_own_call(va, a, 1) : lc_machine(va, a, 1, NULL);
        if (dc_bad || lc_bad) { fprintf(stderr, "FAIL: dialogs %s case %u: a call failed\n", kind == DC_OPTIONS ? "options" : "alert", k); return 1; }
        dc_side_end(side);
    }
    if (eax[0] != eax[1]) {
        fprintf(stderr, "FAIL: dialogs %s case %u: EAX %08lx / %08lx\n", kind == DC_OPTIONS ? "options" : "alert", k, (unsigned long)eax[0],
                (unsigned long)eax[1]);
        return 1;
    }
    if (dc_compare(kind == DC_OPTIONS ? "options" : "alert", k)) return 1;
    dc_passes_total += dc_dialogs.passes;
    dc_journal_delivered += (unsigned)lc_journal[1].injected;
    uint8_t end = *(BYTE *)(uintptr_t)(dc_w.d + 0x2d1u);
    ++dc_ends[end & 3u];
    return 0;
}
static int dc_alert_case(unsigned k) { return dc_modal_case(k, DC_ALERT); }
static int dc_options_case(unsigned k) { return dc_modal_case(k, DC_OPTIONS); }

/* ---- rows: random sequences of the single rows */
typedef struct dc_op { uint32_t va; unsigned n, kind[3]; uint32_t a[3]; unsigned eax_mask; uint8_t record[0x30]; int set_record; } dc_op;
enum { DK_LIT, DK_DIALOG, DK_PANEL, DK_GADGET, DK_PORT, DK_PORT_TABLE, DK_RECORD, DK_SPARE };
static uint32_t dc_resolve(unsigned kind, uint32_t v) {
    switch (kind) {
    case DK_DIALOG: return dc_w.d;
    case DK_PANEL: return dc_w.p;
    case DK_GADGET: return dc_w.g[v % dc_w.count];
    case DK_PORT: return dc_w.port[v & 1u];
    case DK_PORT_TABLE: return dc_w.port[v & 1u] + 4u;
    case DK_RECORD: return DC_RECORD;
    case DK_SPARE: return v ? dc_w.spare : 0u;
    default: return v;
    }
}
static void dc_op_make(dc_op *o, unsigned count) {
    memset(o, 0, sizeof(*o));
    unsigned choice = random32() % 34u;
    static const uint16_t words[] = {0, 0, 1, 0xf, 0x52, 0x1c, 0x80f, 0x99};
#define OP(VA, N, K0, A0, K1, A1, K2, A2, MASK) do { o->va = (VA); o->n = (N); o->kind[0] = (K0); o->a[0] = (A0); o->kind[1] = (K1); \
    o->a[1] = (A1); o->kind[2] = (K2); o->a[2] = (A2); o->eax_mask = (MASK); } while (0)
    switch (choice) {
    case 0: OP(0x00454ac0u, 0, 0, 0, 0, 0, 0, 0, 0xffffffffu); break;
    case 1: OP(0x00454accu, 1, DK_DIALOG, 0, 0, 0, 0, 0, 0); break;
    case 2: case 3:
        OP(0x00454e2cu, 2, DK_DIALOG, 0, DK_RECORD, 0, 0, 0, 0xffu);
        o->set_record = 1;
        for (unsigned i = 0; i < 0x30u; ++i) o->record[i] = (uint8_t)(random32() % 256u);
        put16(o->record, words[random32() % 8u]);
        o->record[0xd] = random32() % 4u == 0;
        o->record[0x17] = random32() % 4u == 0;
        put32(o->record + 2u, (uint32_t)((int32_t)(random32() % 400u) - 20));
        put32(o->record + 6u, (uint32_t)((int32_t)(random32() % 60u) - 10));
        break;
    case 4: OP(0x00454c98u, 2, DK_DIALOG, 0, DK_LIT, random32() % 3u ? random32() % count : 0xffffffffu, 0, 0, 0); break;
    case 5: OP(0x00454d98u, 1, DK_DIALOG, 0, 0, 0, 0, 0, 0); break;
    case 6: OP(0x00454dbcu, 1, DK_DIALOG, 0, 0, 0, 0, 0, 0); break;
    case 7: OP(0x00454de8u, 3, DK_DIALOG, 0, DK_LIT, random32() % count, DK_LIT, random32() % 4u, 0); break;
    case 8: OP(0x00455080u, 1, DK_GADGET, random32(), 0, 0, 0, 0, 0); break;
    case 9: OP(0x0045509cu, 2, DK_GADGET, random32(), DK_LIT, random32() % 4u, 0, 0, 0); break;
    case 10: OP(0x0045617cu, 2, DK_DIALOG, 0, DK_GADGET, random32(), 0, 0, 0); break;
    case 11: OP(0x00457c4cu, 2, DK_DIALOG, 0, DK_GADGET, random32(), 0, 0, 0); break;
    case 12: OP(0x004573c4u, 3, DK_DIALOG, 0, DK_GADGET, random32(), DK_LIT, random32() % 4u, 0); break;
    case 13: OP(0x004530fcu, 1, DK_PANEL, 0, 0, 0, 0, 0, 0); break;
    case 14: OP(0x004531a4u, 1, DK_PANEL, 0, 0, 0, 0, 0, 0xffffffffu); break;
    case 15: OP(0x00435b38u, 2, DK_PORT, random32(), DK_LIT, random32() % 2u ? 0u : 0x1234u, 0, 0, 0); break;
    case 16: OP(0x00435b58u, 1, DK_PORT, random32(), 0, 0, 0, 0, 0); break;
    case 17: OP(0x00435b80u, 1, DK_PORT, random32(), 0, 0, 0, 0, 0); break;
    case 18: OP(0x00435298u, 2, DK_PORT_TABLE, random32(), DK_LIT, random32() % 50u, 0, 0, 0); break;
    case 19: OP(0x0045b734u, 2, DK_LIT, random32() % 8u | (random32() % 2u ? 0x100u : 0u), DK_LIT, random32() % 3u, 0, 0, 0); break;
    case 20: OP(random32() % 2u ? 0x0045bf68u : 0x0045bd8cu, 0, 0, 0, 0, 0, 0, 0, 0xffffffffu); break;
    case 21: OP(0x00452bbcu, 1, DK_LIT, 0x00666000u + random32() % 0x100u, 0, 0, 0, 0, 0); break;
    case 22: OP(0x0045719cu, 1, DK_DIALOG, 0, 0, 0, 0, 0, 0); break;
    case 23: OP(0x00457210u, 2, DK_DIALOG, 0, DK_LIT, random32() % 11u, 0, 0, 0); break;
    case 24: OP(0x00457480u, 1, DK_DIALOG, 0, 0, 0, 0, 0, 0); break;
    case 25: OP(0x004574e8u, 1, DK_DIALOG, 0, 0, 0, 0, 0, 0); break;
    case 26: OP(0x0045999cu, 2, DK_LIT, random32() % 12u, DK_LIT, random32() % 2u, 0, 0, 0xffu); break;
    case 27: OP(0x004599d4u, 3, DK_LIT, random32() % 12u, DK_LIT, 2u + random32() % 4u, DK_LIT, random32() % 2u, 0xffu); break;
    case 28: OP(0x00459a10u, 3, DK_LIT, random32() % 12u, DK_LIT, 2u + random32() % 4u, DK_LIT, random32() % 2u, 0xffu); break;
    case 29: OP(0x00454b8cu, 2, DK_DIALOG, 0, DK_LIT, 0x00555000u + random32() % 0x100u, 0, 0, 0); break;   /* a font in DATA: the original reads [f+4] */
    case 30: OP(0x00454c2cu, 1, DK_DIALOG, 0, 0, 0, 0, 0, 0); break;
    case 31: OP(0x00454b74u, 2, DK_DIALOG, 0, DK_LIT, 0x00ad0000u + random32() % 0x100u, 0, 0, 0); break;
    case 32:
        switch (random32() % 4u) {
        case 0: OP(0x004561c8u, 2, DK_DIALOG, 0, DK_LIT, random32() % 4u, 0, 0, 0xffffffffu); break;
        case 1: OP(0x00457c88u, 1, DK_DIALOG, 0, 0, 0, 0, 0, 0xffffffffu); break;
        case 2: OP(0x00457458u, 2, DK_DIALOG, 0, DK_LIT, random32() % 8u, 0, 0, 0xffffffffu); break;
        default: OP(0x0045746cu, 2, DK_DIALOG, 0, DK_LIT, random32() % 2u ? (uint32_t)(uintptr_t)dc_hook : 0u, 0, 0, 0); break;
        }
        break;
    default: OP(0x00452c04u, 1, DK_SPARE, random32() % 2u, 0, 0, 0, 0, 0); break;
    }
#undef OP
}
static int dc_rows_case(unsigned k) {
    dc_inputs(1u + random32() % 6u);
    unsigned count = 1u + random32() % 11u, ops = 1u + random32() % 10u;
    int open = random32() % 2u == 0, freed = 0;
    static dc_op list[16];
    for (unsigned i = 0; i < ops; ++i) {
        dc_op_make(&list[i], count);
        /* 0x452c04 frees the spare block once; 0x457210's label row reads
         * the first eleven tables only. */
        if (list[i].va == 0x00452c04u && list[i].a[0]) { if (freed) list[i].a[0] = 0; freed = 1; }
    }
    uint32_t seed = random32();
    static uint32_t eax[2][16];
    for (unsigned side = 0; side < 2u; ++side) {
        if (dc_side_begin(side, DC_ROWS, seed)) return 1;
        /* The rows part fixes the gadget count the generator chose. */
        if (dc_w.count != count) { dc_w.count = count; dc_put32(dc_w.d + 0x2cdu, count); }
        uint32_t a[3];
        if (open) {
            a[0] = dc_w.d;
            if (side) dc_own_call(0x0045464cu, a, 1); else lc_machine(0x0045464cu, a, 1, NULL);
        }
        for (unsigned i = 0; i < ops && !dc_bad && !lc_bad; ++i) {
            const dc_op *o = &list[i];
            if (o->set_record) memcpy(mapped + (DC_RECORD - 0x400000u), o->record, 0x30);
            for (unsigned j = 0; j < 3u; ++j) a[j] = dc_resolve(o->kind[j], o->a[j]);
            eax[side][i] = (side ? dc_own_call(o->va, a, o->n) : lc_machine(o->va, a, o->n, NULL)) & o->eax_mask;
        }
        if (open && !dc_bad && !lc_bad) {
            a[0] = dc_w.d;
            if (side) dc_own_call(0x004548c8u, a, 1); else lc_machine(0x004548c8u, a, 1, NULL);
        }
        if (dc_bad || lc_bad) { fprintf(stderr, "FAIL: dialogs rows case %u: a call failed\n", k); return 1; }
        dc_side_end(side);
    }
    for (unsigned i = 0; i < ops; ++i)
        if (eax[0][i] != eax[1][i]) {
            fprintf(stderr, "FAIL: dialogs rows case %u op %u (0x%08lx): EAX %08lx / %08lx\n", k, i, (unsigned long)list[i].va,
                    (unsigned long)eax[0][i], (unsigned long)eax[1][i]);
            return 1;
        }
    return dc_compare("rows", k);
}

/* ---- wndproc: WM_CLOSE and WM_QUERYENDSESSION */
static int dc_wndproc_case(unsigned k) {
    dc_journal_size = 0;
    dc_script_count = 0;
    unsigned count = 1u + random32() % 4u;
    uint32_t m[4], answers[4], demo[4], results[2][4];
    for (unsigned i = 0; i < count; ++i) { m[i] = random32() % 2u ? 0x10u : 0x11u; answers[i] = random32() % 4u; demo[i] = random32() % 4u == 0; }
    uint32_t seed = random32();
    for (unsigned side = 0; side < 2u; ++side) {
        if (dc_side_begin(side, DC_WNDPROC, seed)) return 1;
        for (unsigned i = 0; i < count; ++i) {
            dc_answer = answers[i];
            put16(mapped + 0xd2530u, (uint16_t)demo[i]);
            if (!side) {
                es2_hook_result r;
                uint32_t a[4] = {0x00010001u, m[i], 0, 0};
                es2_hook_call c = {mapped + 0x66034u, ES2_ABI_STDCALL, 4, a, initial_fpu, &r, NULL};
                es2_hook_machine_call(&c);
                results[0][i] = r.eax;
            } else if (es2_sim_window_proc(&lc_loop, 0x00010001u, m[i], 0, 0, &results[1][i])) {
                dc_bad = 1;
            }
        }
        if (dc_bad || lc_bad) { fprintf(stderr, "FAIL: dialogs wndproc case %u: a call failed\n", k); return 1; }
        dc_side_end(side);
    }
    if (memcmp(results[0], results[1], 4u * count)) {
        fprintf(stderr, "FAIL: dialogs wndproc case %u: results differ\n", k);
        return 1;
    }
    return dc_compare("wndproc", k);
}

static int dialogs_cases(void) {
    current_function = "dialogs";
    es2_sim_identity_memory(&ir_memory);
    es2_sim_rtl_memory(&ir_memory, &su_rtl);
    su_data_pristine = malloc(SU_DATA_SIZE);
    su_snap[0] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    su_snap[1] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    ir_region = ww_alloc(IR_REGION_SIZE);
    lc_stack = VirtualAlloc(NULL, 0x10000u, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    lc_bits = ww_alloc(LC_BITS_SIZE);
    if (!su_data_pristine || !su_snap[0] || !su_snap[1] || !ir_region || !lc_stack || !lc_bits) return fail("dialogs: memory");
    if (get32(mapped + (SU_COUNTER - 0x400000u))) return fail("dialogs: the counter word is not free");
    su_pristine_take();
    static const struct { const char *dll, *name; void *stub; } stubs[] = {
        {"cw3220.dll", "_calloc", (void *)lc_calloc}, {"cw3220.dll", "_free", (void *)lc_free},
        {"cw3220.dll", "_memset", (void *)lc_memset}, {"KERNEL32.dll", "GetTickCount", (void *)lc_tick},
        {"cw3220.dll", "_strchr", (void *)su_strchr}, {"cw3220.dll", "_strlen", (void *)su_strlen},
        {"cw3220.dll", "_strcat", (void *)su_strcat}, {"cw3220.dll", "_memcpy", (void *)su_memcpy},
        {"USER32.dll", "PeekMessageA", (void *)lc_peek}, {"USER32.dll", "TranslateMessage", (void *)lc_translate},
        {"USER32.dll", "DispatchMessageA", (void *)dc_dispatch_message}, {"USER32.dll", "SetCursor", (void *)lc_set_cursor},
        {"USER32.dll", "DefWindowProcA", (void *)lc_def}, {"USER32.dll", "GetClientRect", (void *)dc_client_rect},
        {"USER32.dll", "ClientToScreen", (void *)dc_client_to_screen}, {"USER32.dll", "SetCursorPos", (void *)dc_set_cursor_pos},
    };
    for (unsigned i = 0; i < sizeof(stubs) / sizeof(stubs[0]); ++i)
        if (!ir_patch_import(stubs[i].dll, stubs[i].name, stubs[i].stub)) {
            fprintf(stderr, "import %s!%s\n", stubs[i].dll, stubs[i].name);
            return fail("dialogs: import slot not found");
        }
    for (unsigned i = 0; i < sizeof(dc_patched) / sizeof(dc_patched[0]); ++i)
        if (dc_patch(dc_patched[i].va, dc_patched[i].stub, dc_saved[i], 1)) return 1;
    if (dc_dispatch_setup()) return 1;
    es2_journal_init(&lc_journal[0]);
    es2_journal_init(&lc_journal[1]);
    int bad = 0;
    static const struct { const char *name; int (*run)(unsigned); unsigned cases; } parts[] = {
        {"alert", dc_alert_case, 1500}, {"options", dc_options_case, 800}, {"rows", dc_rows_case, 2000}, {"wndproc", dc_wndproc_case, 1000},
    };
    for (unsigned p = 0; !bad && p < sizeof(parts) / sizeof(parts[0]); ++p) {
        for (unsigned k = 0; !bad && k < parts[p].cases; ++k) {
            current_case = k; current_case_valid = 1;
            bad = parts[p].run(k);
        }
        if (!bad) printf("PASS: dialogs %s: %u cases identical\n", parts[p].name, parts[p].cases);
    }
    if (!bad) printf("DIALOGS coverage: %u modal passes, %u journal entries delivered, ends by state: %u stop/quit, %u gadget 0, %u gadget 1 / store\n",
                     dc_passes_total, dc_journal_delivered, dc_ends[1], dc_ends[2], dc_ends[3]);
    current_case_valid = 0;
    for (unsigned i = 0; i < sizeof(dc_patched) / sizeof(dc_patched[0]); ++i) dc_patch(dc_patched[i].va, NULL, dc_saved[i], 0);
    put32(mapped + 0x2cbee4u, 0);
    memcpy(mapped + SU_DATA_RVA, su_data_pristine, SU_DATA_SIZE);
    return bad;
}
