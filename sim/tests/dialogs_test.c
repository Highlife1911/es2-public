/* The modal dialogs (es2/sim/dialogs.h) on a synthetic image, headless: the
 * modal loop's passes are answered by the input journal's pump-keyed entries
 * (through the pump row and es2_sim_window_proc's handler list), the clock is
 * called at the dialogs' clock sites in the original order (the HUD trace), the mission-end
 * alert ends through its gadget, the stop word and the quit word, the key
 * handler moves the focus and presses, the release list holds a pressed
 * gadget's end state back, the ports pause and resume around the dialog, the
 * F11 options dialog toggles a pref, refreshes its label and stores, the pass
 * limit refuses a dialog without input, and WM_CLOSE/WM_QUERYENDSESSION reach
 * the command handler's quit dialog.
 *
 * Zeroed host memory with the objects each case needs written at
 * synthetic addresses (the dialog, its panel and gadgets at 0x620000..,
 * the globals at their original addresses), the main loop's call area at
 * its own host address; the sim's real rows answer the dialog rows, the
 * pump, the clock (virtual time), client_rect and prefs_set (a headless
 * input layer); the hooks (0x45a810 control record, 0x4530bc button
 * release, 0x45feb8 command key, 0x459b94 prefs store), the graphics
 * library's pixel work and the present, the label draw, the GC select,
 * the sound service, the port and the delete are recording services of
 * this test. No game data. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "es2/sim/cursor.h"
#include "es2/sim/dialogs.h"
#include "es2/sim/hud.h"
#include "es2/sim/input.h"
#include "es2/sim/memory.h"
#include "es2/sim/services.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

#define IMAGE_BASE 0x00400000u
#define IMAGE_SIZE 0x002d8000u
#define HEAP_BASE 0x09000000u
#define HEAP_SIZE 0x00400000u
#define LOCAL_SIZE 0x00080000u
#define COUNTER (IMAGE_BASE + 0x2d7ff0u)

/* The rows under test. */
#define VA_ALERT_RUN 0x00456000u
#define VA_CONFIRM_RUN 0x00457b00u
#define VA_OPTIONS_RUN 0x00456d68u
#define VA_KEYS 0x00454e2cu
#define VA_RELEASE_TICK 0x004530fcu
#define VA_PREFS_CYCLE 0x004599d4u
#define VA_PREFS_CYCLE_BACK 0x00459a10u
#define VA_ALERT_SELECT 0x0045617cu
#define VA_CONFIRM_SELECT 0x00457c4cu
#define VA_OPTIONS_SELECT 0x004573c4u
#define VA_FIRE 0x0045509cu
#define VA_TICK 0x00455080u
/* Hooks and rows this test answers. */
#define VA_CONTROL_RECORD 0x0045a810u
#define VA_BUTTON_RELEASE 0x004530bcu
#define VA_COMMAND_KEY 0x0045feb8u
#define VA_PREFS_STORE 0x00459b94u
#define VA_FILL 0x00488012u
#define VA_CLEAR 0x00487e60u
#define VA_PEN 0x00486e7cu
#define VA_PEN_B 0x0048993au
#define VA_FONT_LOAD 0x00430fb0u
#define VA_JOY_PAGE_CTOR 0x00457d38u   /* the joystick page's rows, stubbed after the seal */
#define VA_JOY_PAGE_RUN 0x0045866cu
#define VA_JOY_PAGE_DTOR 0x004585f4u
#define VA_PRESENT 0x00464a14u
#define VA_LABEL 0x00438920u
#define VA_GC 0x00480d44u
#define VA_SOUND 0x00464260u
#define VA_PORT_A 0x00433158u
#define VA_BLIT 0x00488143u
#define VA_DELETE 0x00477548u
#define VA_PATH_DIR 0x00492becu
/* Globals. */
#define VA_RECORD 0x004d22c6u
#define VA_STOP 0x004d2532u
#define VA_QUIT 0x004d24feu
#define VA_DEMO 0x004d2530u
#define VA_CLOSE_RESULT 0x004d370cu
#define VA_PRESENT_ON 0x004d2566u
#define VA_NEXT_PRESENT 0x004d1e64u
#define VA_PRESENT_PERIOD 0x004d1e60u
#define VA_DEPTH 0x0049dd8cu
#define VA_FLAG_A 0x0049eaa4u
#define VA_FLAG_B 0x0049dbecu
#define VA_HOLD_POINTER 0x004d1e04u
#define VA_COCKPIT 0x0049b088u
#define VA_SURFACE 0x006c5ed0u
#define VA_PAGE_B 0x004a368au
#define VA_DRV_F0 0x004a57f0u
#define VA_DRV_EC 0x004a57ecu
#define VA_DRV_28 0x004a5828u
#define VA_DRV_24 0x004a5824u
#define VA_FONT 0x0049dd90u
#define VA_SOUND_OBJECT 0x0049f8ecu
#define VA_DEVICE_ID 0x004d24a0u
#define VA_DEVICE_TABLE 0x004d2468u
#define VA_KEY_FLAGS 0x004d22f5u
#define VA_KEY_VALUES 0x004d22fdu
#define VA_HWND 0x004d3610u
#define VA_HALF_W 0x004d3040u
#define VA_HALF_H 0x004d3044u
#define VA_PAUSED_TOTAL 0x004d1294u
#define VA_PREFS 0x004d1f38u
#define VA_PREFS_PREVIOUS 0x004d1fa4u
#define VA_OPT_FLAG 0x0049e9a5u
#define VA_HANDLERS 0x004d3b30u
#define VA_DRAW_FLAG 0x0049dbdfu
/* Synthetic objects. */
#define OBJ 0x00620000u
#define DIALOG (OBJ + 0x0000u)          /* 0x800 bytes */
#define PANEL (OBJ + 0x1000u)           /* 0x400 bytes */
#define GADGET0 (OBJ + 0x2000u)         /* 0x40 bytes each, 12 */
#define GADGET(k) (GADGET0 + 0x40u * (k))
#define PORT_A (OBJ + 0x3000u)          /* 0x600 bytes */
#define PORT_B (OBJ + 0x3800u)
#define COCKPIT (OBJ + 0x4000u)
#define SURFACE (OBJ + 0x4400u)
#define HOLD (OBJ + 0x4800u)
#define LABELS (OBJ + 0x4900u)
#define VT_DIALOG (OBJ + 0x5000u)       /* vtables and gadget tables */
#define VT_GADGET (OBJ + 0x5040u)
#define VT_PART (OBJ + 0x5080u)
#define VT_PORT (OBJ + 0x50c0u)
/* Stub VAs (services of this test). */
#define STUB_DRAW 0x7ff00100u
#define STUB_GADGET_DRAW 0x7ff00104u
#define STUB_GADGET_REDRAW 0x7ff00108u
#define STUB_PART 0x7ff0010cu
#define STUB_PORT 0x7ff00110u
#define STUB_HANDLER 0x7ff00114u
#define STUB_HOOK 0x7ff00118u
#define STUB_DRIVER 0x7ff0011cu

static uint8_t *image, *heap_bytes, *local;
static uint32_t local_guest;
static es2_sim_arena arena;
static es2_guest_memory memory;
static es2_sim_heap heap;
static es2_sim_services services;
static es2_sim_entry storage[4096];
static es2_sim_dispatch table;
static es2_sim_loop loop;
static es2_sim_hud hud;
static es2_sim_hud_event trace[256];
static es2_sim_dialogs dialogs;
static es2_sim_input input;
static es2_journal journal;
static es2_vt vt;
static unsigned refusals;
static uint32_t first_refused;
static void on_refuse(const es2_sim_refusal *r, void *user) {
    (void)user;
    if (!refusals) first_refused = r->va;
    ++refusals;
    if (getenv("DIALOGS_TEST_VERBOSE")) fprintf(stderr, "refused 0x%08x: %s\n", (unsigned)r->va, r->reason ? r->reason : "");
}

static uint32_t next_heap = HEAP_BASE;
static uint32_t heap_alloc(void *c, uint32_t address, uint32_t size, uint32_t type, uint32_t protect) {
    (void)c; (void)protect;
    if (type & ES2_HEAP_MEM_RESERVE) {
        if (!address) address = next_heap;
        if (address < HEAP_BASE || address + size > HEAP_BASE + HEAP_SIZE) return 0;
        next_heap = address + size;
    }
    return address;
}
static uint32_t os_free(void *c, uint32_t a, uint32_t s, uint32_t t) { (void)c; (void)a; (void)s; (void)t; return 1; }

static uint32_t r32(uint32_t a) { return memory.read32(memory.context, a); }
static uint16_t r16(uint32_t a) { return memory.read16(memory.context, a); }
static uint8_t r8(uint32_t a) { return es2_sim_read8(&memory, a); }
static void w32(uint32_t a, uint32_t v) { memory.write32(memory.context, a, v); }
static void w16(uint32_t a, uint16_t v) { memory.write16(memory.context, a, v); }
static void w8(uint32_t a, uint8_t v) { es2_sim_write8(&memory, a, v); }

/* ---- the recording services: a letter per call with up to three slots
 * and the pass they fell in. */
typedef struct call_log { char letter; uint32_t va, a0, a1, a2; unsigned pass; } call_log;
static call_log calls[512];
static unsigned call_count;
static void note(char letter, es2_sim_service_call *c) {
    if (call_count < 512u) {
        call_log *e = &calls[call_count];
        e->letter = letter;
        e->va = c->va;
        e->a0 = c->slots[0];
        e->a1 = c->slots[1];
        e->a2 = c->slots[2];
        e->pass = dialogs.passes;
    }
    ++call_count;
}
static unsigned letters(char letter) {
    unsigned n = 0;
    for (unsigned i = 0; i < call_count && i < 512u; ++i) n += calls[i].letter == letter;
    return n;
}
static const call_log *nth(char letter, unsigned k) {
    for (unsigned i = 0; i < call_count && i < 512u; ++i)
        if (calls[i].letter == letter && !k--) return &calls[i];
    return NULL;
}

/* The control record 0x45a810 (a hook): the pump, the stop word on
 * WM_QUIT, the record cleared and filled from the pass script. */
typedef struct pass_input { uint16_t key; uint8_t click, tab, quit_word; int32_t x, y; uint32_t skip; } pass_input;
static pass_input script[32];
static unsigned script_count, script_index;
static void svc_control_record(es2_sim_service_call *c, void *u) {
    (void)u;
    note('R', c);
    uint32_t quit = 0;
    if (es2_sim_pump(&loop, &quit)) { c->refused = 1; c->reason = "the pump was refused"; return; }
    if (quit) w16(VA_STOP, 1);
    for (uint32_t i = 0; i < 0x30u; i += 4u) w32(VA_RECORD + i, 0);
    if (script_index < script_count) {
        const pass_input *p = &script[script_index];
        w16(VA_RECORD, p->key);
        w8(VA_RECORD + 0xdu, p->click);
        w8(VA_RECORD + 0x17u, p->tab);
        w32(VA_RECORD + 2u, (uint32_t)p->x);
        w32(VA_RECORD + 6u, (uint32_t)p->y);
        if (p->quit_word) w16(VA_QUIT, 1);
        vt.clock_ms += p->skip;   /* virtual time moved on between passes (the clock is ms >> 4) */
    }
    ++script_index;
    c->eax = VA_RECORD;
}
/* 0x4530bc(panel, gadget) (a hook): the release entry, due at clock + 10. */
static void svc_button_release(es2_sim_service_call *c, void *u) {
    (void)u;
    note('B', c);
    uint32_t p = c->slots[0], n = (uint32_t)(int16_t)r16(p + 0x396u);
    w16(p + 0x396u, (uint16_t)(n + 1u));
    w32(p + 0x356u + 8u * n, c->slots[1]);
    w32(p + 0x356u + 8u * n + 4u, es2_sim_clock(&loop) + 10u);
}
static uint32_t command_answer = 2;
static void svc_command_key(es2_sim_service_call *c, void *u) { (void)u; note('K', c); c->eax = command_answer; }
static void svc_record(es2_sim_service_call *c, void *u) { note((char)(uintptr_t)u, c); c->eax = 0; }
static void svc_present(es2_sim_service_call *c, void *u) { (void)u; note('V', c); c->eax = 1; }
static void svc_port_a(es2_sim_service_call *c, void *u) { (void)u; note('W', c); c->eax = PORT_A; }
static void svc_font_load(es2_sim_service_call *c, void *u) { (void)u; note('F', c); c->eax = 0x00777000u; }
static void svc_handler(es2_sim_service_call *c, void *u) { (void)u; note('H', c); c->eax = 1; }
/* The joystick page's run: ends the options dialog under it. */
static void svc_page_run(es2_sim_service_call *c, void *u) { (void)u; note('j', c); w8(DIALOG + 0x2d1u, 3); c->eax = 1; }

static int setup_dispatch(void) {
    es2_sim_dispatch_init(&table, storage, 4096, on_refuse, NULL);
    services.memory = &memory;
    services.heap = &heap;
    services.vt = &vt;
    static uint32_t fs0 = 0x00abcdefu;
    services.fs0 = &fs0;
    if (es2_sim_register_services(&table, &services)) return 1;
    static const struct { uint32_t va; char letter; unsigned slots; es2_sim_service_fn fn; } h[] = {
        {VA_CONTROL_RECORD, 'R', 0, svc_control_record}, {VA_BUTTON_RELEASE, 'B', 2, svc_button_release},
        {VA_COMMAND_KEY, 'K', 1, svc_command_key}, {VA_PREFS_STORE, 'S', 2, svc_record},
        {STUB_DRAW, 'D', 1, svc_record}, {STUB_GADGET_DRAW, 'g', 1, svc_record}, {STUB_GADGET_REDRAW, 'r', 1, svc_record},
        {STUB_PART, 'x', 1, svc_record}, {STUB_PORT, 'p', 1, svc_record}, {STUB_HANDLER, 'H', 4, svc_handler},
        {STUB_HOOK, 'h', 0, svc_record}, {STUB_DRIVER, 'd', 0, svc_record}};
    for (unsigned i = 0; i < sizeof(h) / sizeof(h[0]); ++i)
        if (es2_sim_dispatch_add_service(&table, h[i].va, "stub", h[i].fn, (void *)(uintptr_t)h[i].letter, 0, (uint8_t)h[i].slots)) return 1;
    const char *error = NULL;
    uint32_t bad = 0;
    if (es2_sim_dispatch_seal(&table, &error, &bad)) return 1;
    /* Rows of the real table that need the game's objects: replaced (after
     * the seal sorted the table) by recording stubs (the present, the label
     * draw, the GC select, the sound service, the port, the blit, the
     * delete, the path). */
    static const struct { uint32_t va; char letter; es2_sim_service_fn fn; } w[] = {
        {VA_PRESENT, 'V', svc_present}, {VA_LABEL, 'L', svc_record}, {VA_GC, 'G', svc_record}, {VA_SOUND, 'A', svc_record},
        {VA_PORT_A, 'W', svc_port_a}, {VA_BLIT, 'T', svc_record}, {VA_DELETE, 'f', svc_record}, {VA_PATH_DIR, 'N', svc_record},
        {VA_FILL, 'I', svc_record}, {VA_CLEAR, 'E', svc_record},   /* gfx.c's rows */
        {VA_PEN, 'P', svc_record},    /* gfx.c's row */
        {VA_PEN_B, 'Q', svc_record},  /* gfx.c's row */
        {VA_FONT_LOAD, 'F', svc_font_load}, /* presentation_loads.h's row font_load */
        {VA_JOY_PAGE_CTOR, 'J', svc_record}, {VA_JOY_PAGE_RUN, 'j', svc_page_run}, {VA_JOY_PAGE_DTOR, 'k', svc_record}}; /* joystick_page.h */
    for (unsigned i = 0; i < sizeof(w) / sizeof(w[0]); ++i) {
        es2_sim_entry *e = (es2_sim_entry *)es2_sim_dispatch_find(&table, w[i].va);
        if (!e || e->kind != ES2_SIM_SERVICE) return 1;
        e->service = w[i].fn;
        e->service_user = (void *)(uintptr_t)w[i].letter;
    }
    return 0;
}
static void fresh(const char *journal_text) {
    memset(image, 0, IMAGE_SIZE);
    es2_sim_cursor_calls_reset();
    w32(0x006cbee4u, COUNTER);
    memset(heap_bytes, 0, HEAP_SIZE);
    memset(local, 0, LOCAL_SIZE);
    next_heap = HEAP_BASE;
    es2_sim_heap_os os = {NULL, heap_alloc, os_free};
    es2_sim_heap_init(&heap, &memory, &os, local_guest + 0x7f000u, 0, 0, 0);
    memset(&vt, 0, sizeof(vt));
    es2_vt_enable(&vt, 1, 47, 0x01000000u, 0, 1);
    es2_journal_free(&journal);
    es2_journal_init(&journal);
    if (journal_text) es2_journal_load_text(&journal, journal_text, strlen(journal_text), NULL);
    es2_sim_loop_defaults(&loop);
    loop.memory = &memory;
    loop.dispatch = &table;
    loop.vt = &vt;
    loop.journal = &journal;
    loop.stack_lo = local_guest;
    loop.stack_hi = local_guest + 0x10000u;
    es2_sim_loop_bind(&loop);
    es2_sim_hud_init(&hud, &loop);
    hud.trace = trace;
    hud.trace_capacity = 256;
    es2_sim_hud_bind(&hud);
    es2_sim_dialogs_init(&dialogs);
    dialogs.pass_limit = 64;   /* a safety net: a dialog this test fails to end is refused, not hung */
    es2_sim_dialogs_bind(&dialogs);
    es2_sim_input_headless(&input, &loop, &journal, 640, 480);
    es2_sim_input_bind(&input);
    refusals = 0;
    call_count = 0;
    script_count = script_index = 0;
    memset(script, 0, sizeof(script));
    command_answer = 2;
    /* The globals every dialog reads. */
    w32(VA_HWND, 0x00010001u);
    w32(VA_HALF_W, 640);
    w32(VA_HALF_H, 480);
    w32(VA_COCKPIT, COCKPIT);
    w32(COCKPIT + 0x20bu, PORT_B);
    w32(VA_SURFACE, SURFACE);
    w32(SURFACE + 0x88u, 0x11110000u);
    w32(SURFACE + 0x8cu, 0x22220000u);
    w32(VA_HOLD_POINTER, HOLD);
    w32(HOLD, 0xdeadbeefu);
    w32(VA_FONT, 0x00555000u);
    w32(VA_DRV_EC, STUB_DRIVER);
    w32(VA_DRV_F0, STUB_DRIVER);
    w32(VA_DRV_28, STUB_DRIVER);
    w32(VA_DRV_24, STUB_DRIVER);
    w32(VA_PRESENT_PERIOD, 7);
    w32(VT_PORT + 0xcu, STUB_PORT);
    w32(PORT_A, VT_PORT);
    w32(PORT_B, VT_PORT);
    /* The vtables: the dialog's draw, select (set per case) and key handler;
     * the gadget table and the part table. */
    w32(VT_DIALOG, STUB_DRAW);
    w32(VT_DIALOG + 0x10u, VA_KEYS);
    w32(VT_GADGET, STUB_GADGET_DRAW);
    w32(VT_GADGET + 4u, STUB_GADGET_REDRAW);
    w32(VT_GADGET + 8u, VA_FIRE);
    w32(VT_GADGET + 0xcu, VA_TICK);
    w32(VT_PART + 8u, STUB_PART);
}
/* A dialog with `count` gadgets in a row (24 x 12 each at y 10) at origin
 * (100, 50), the panel, no surface, default gadget `fallback`. */
static void build_dialog(uint32_t select, unsigned count, uint32_t fallback) {
    w32(DIALOG, VT_DIALOG);
    w32(VT_DIALOG + 0xcu, select);
    w32(DIALOG + 4u, 100);
    w32(DIALOG + 8u, 50);
    w32(DIALOG + 0x1cu, 100); w32(DIALOG + 0x20u, 50); w32(DIALOG + 0x24u, 400); w32(DIALOG + 0x28u, 200);
    w32(DIALOG + 0x44u, 0x00444000u);
    w32(DIALOG + 0x285u, PANEL);
    w32(DIALOG + 0x28du, 0x33330000u);
    w32(DIALOG + 0x2cdu, count);
    w32(DIALOG + 0x2fbu, fallback);
    w32(DIALOG + 0x2f7u, 0xffffffffu);
    w32(DIALOG + 0x3b6u, 0xffffffffu);
    w32(DIALOG + 0x356u, 0xffffffffu);
    for (unsigned k = 0; k < count; ++k) {
        uint32_t g = GADGET(k);
        w32(g, 30u * k); w32(g + 4u, 10); w32(g + 8u, 30u * k + 24u); w32(g + 0xcu, 22);
        w32(g + 0x17u, VT_GADGET);
        w32(g + 0x20u, VT_PART);
        w32(g + 0x24u, DIALOG);
        w32(DIALOG + 0x291u + 4u * k, g);
    }
}
static int row(uint32_t va, const uint32_t *slots, unsigned n, es2_sim_result *r) {
    uint32_t regs[3] = {0, 0, 0};
    uint32_t at = local_guest + 0x20000u;
    for (unsigned i = 0; i < n; ++i) w32(at + 4u * i, slots[i]);
    memset(r, 0, sizeof(*r));
    return es2_sim_call(&table, va, (const uint32_t *)(uintptr_t)at, n, regs, r);
}
static int clock_sites(const uint32_t *sites, unsigned n) {
    unsigned k = 0;
    for (unsigned i = 0; i < hud.trace_count && i < 256u; ++i) {
        if (trace[i].kind != ES2_SIM_HUD_CLOCK) continue;
        if (k >= n || trace[i].site != sites[k]) {
            fprintf(stderr, "clock site %u: 0x%08x (expected 0x%08x)\n", k, (unsigned)trace[i].site, k < n ? (unsigned)sites[k] : 0u);
            return 0;
        }
        ++k;
    }
    return k == n;
}

/* ---- the mission-end alert answered by the journal: the journal's entry
 * (pump key 2 of frame 0) is delivered at the frame's third peek, the
 * first pass's pump (the open's pump and the open's hit test are the first
 * two), to the handler list; the second pass finds the present due (time
 * moved on); the third pass presses the focused gadget (key 0x1c) whose
 * select ends the dialog with 2, held back one pass while the release
 * entry is shown; the fourth pass expires the entry and ends; the clock
 * order, the port times and the ports' shifted entries. */
static int alert_journal(void) {
    fresh("{\"f\":0,\"p\":2,\"api\":\"msg\",\"a\":256,\"b\":81,\"c\":1}\n");
    build_dialog(VA_ALERT_SELECT, 2, 1);
    w32(VA_HANDLERS, STUB_HANDLER);
    w16(VA_PRESENT_ON, 1);
    w32(VA_DEVICE_ID, 1);
    w32(VA_DEVICE_TABLE + 4u, 0x00666000u + 0x3au);
    w32(VA_SOUND_OBJECT, 0x00123400u);
    w8(VA_DRAW_FLAG, 1);
    /* Port A: one indexed entry (index 2) with a time ahead and one behind,
     * an item ahead; port B: an item behind. */
    w16(PORT_A + 4u + 0x21cu, 1);
    w32(PORT_A + 4u + 0x1eau, 2);
    w32(PORT_A + 4u + 2u * 0x31u + 0x24u, 0xffffff00u);
    w32(PORT_A + 4u + 2u * 0x31u + 0x28u, 0);
    w32(PORT_A + 0x49au, OBJ + 0x3700u);
    w32(OBJ + 0x3700u + 0x1cu, 0xffffff00u);
    w32(OBJ + 0x3700u + 0x20u, 5);
    w32(PORT_B + 0x49au, OBJ + 0x3f00u);
    script[2].skip = 100;          /* pass 2: 6 clock units on: the present is due */
    script[3].key = 0x1cu;         /* pass 3 (script 0 is the open's hit test) */
    script[4].skip = 200;          /* pass 4: the release entry (+10) expires */
    script_count = 20;
    es2_sim_result r;
    CHECK(!row(VA_ALERT_RUN, (const uint32_t[]){DIALOG}, 1, &r) && !refusals);
    CHECK(r.eax == 0 && r8(DIALOG + 0x2d1u) == 2 && r8(DIALOG + 0x331u) == 0);
    /* The handler saw the key-down in the first pass. */
    CHECK(letters('H') == 1 && nth('H', 0)->a0 == 0x10001u && nth('H', 0)->a1 == 256u && nth('H', 0)->a2 == 81u && nth('H', 0)->pass == 1);
    CHECK(journal.injected == 1 && loop.platform.dispatches == 1);
    /* The press in pass 3: the gadget's fire (owner select -> state 2,
     * redraw, the part's table), the release entry (shown in state 1 by the
     * step, redrawn); the end held back one pass; pass 4 expires the entry
     * (state 0, redrawn) and ends. */
    CHECK(letters('B') == 1 && nth('B', 0)->a0 == PANEL && nth('B', 0)->a1 == GADGET(0) && nth('B', 0)->pass == 3);
    CHECK(dialogs.passes == 4 && script_index == 5 && letters('R') == 5);
    CHECK(loop.platform.peeks == 7);        /* the open's pump, the open's hit test, one per pass, one more for the delivered message */
    CHECK(letters('r') == 3 && nth('r', 1)->pass == 3 && nth('r', 2)->pass == 4 && letters('x') == 1);
    CHECK(r16(PANEL + 0x396u) == 0 && r16(GADGET(0) + 0x1bu) == 0);
    CHECK(letters('V') == 3 && nth('V', 0)->pass == 2 && nth('V', 1)->pass == 4 && nth('V', 2)->pass == 4);   /* the step's presents in passes 2 and 4, the close's */
    /* The open and close: the depth and flags, the record copy, the pages, the
     * port pause and resume, the cursor placed through its rows. */
    CHECK(r16(VA_DEPTH) == 0 && r8(VA_FLAG_A) == 1 && r8(VA_FLAG_B) == 1 && r32(HOLD) == 0);
    CHECK(r32(SURFACE + 0x38u) == 0x11110000u && r32(SURFACE + 0x3cu) == 0x33330000u && r32(VA_PAGE_B) == 0x33330000u);
    CHECK(r8(VA_DRAW_FLAG) == 1 && r8(DIALOG + 0x2f6u) == 1);
    uint32_t total = r32(VA_PAUSED_TOTAL), delta = r32(PORT_A + 4u + 2u * 0x31u + 0x24u) - 0xffffff00u;   /* port A's paused time */
    CHECK(r16(PORT_A + 0x4d0u) == 0 && r32(PORT_A + 0x4ccu) == 0 && r16(PORT_B + 0x4d0u) == 0 && delta >= 18u && delta < 40u);
    CHECK(total >= 2u * delta - 1u && total <= 2u * delta + 1u && r32(PORT_A + 4u + 2u * 0x31u + 0x28u) == 0);   /* both ports' */
    CHECK(r32(OBJ + 0x3700u + 0x1cu) == 0xffffff00u + delta && r32(OBJ + 0x3700u + 0x20u) == 5u + delta && r8(PORT_A + 0x4cbu) == 1);
    CHECK(r8(PORT_B + 0x4cbu) == 1 && r32(OBJ + 0x3f00u + 0x1cu) == 0);   /* port A marked at the open, port B's item behind */
    CHECK(hud.headless == 0 && es2_sim_cursor_calls_get()->set_pos == 2 && es2_sim_cursor_calls_get()->client_to_screen == 2);
    CHECK(letters('W') == 2 && letters('G') == 2 && letters('A') == dialogs.passes);
    CHECK(letters('I') == 0 && letters('T') == 0 && letters('f') == 1 && nth('f', 0)->a0 == PANEL);
    CHECK(r8(VA_KEY_FLAGS + 1) == 1 && r8(VA_KEY_VALUES + 4) == 1);
    /* The clock order: the open's two port pauses and present time; per
     * pass the release entries (site 0x45311a, from pass 3 on) and the
     * present check (0x454b51), pass 3 first the select's present time;
     * the close's two resumes (each 0x435b9a then its shift 0x4352a1). */
    static const uint32_t expected[] = {
        0x435b68u, 0x435b68u, 0x454ac0u,
        0x454b51u,
        0x454b51u,
        0x454ac0u, 0x45311au, 0x454b51u,
        0x45311au, 0x454b51u,
        0x435b9au, 0x4352a1u, 0x435b9au, 0x4352a1u};
    CHECK(clock_sites(expected, sizeof(expected) / sizeof(expected[0])));
    return 0;
}

/* ---- the stop word (WM_QUIT in the pass's pump) ends 0x456000 at once
 * with 1; 0x457b00 ignores it and runs to the quit word. */
static int stop_and_quit(void) {
    static const char quit_journal[] = "{\"f\":0,\"p\":1,\"api\":\"msg\",\"a\":18,\"b\":0,\"c\":0}\n";
    fresh(quit_journal);
    build_dialog(VA_CONFIRM_SELECT, 2, 0);
    script_count = 20;
    es2_sim_result r;
    CHECK(!row(VA_ALERT_RUN, (const uint32_t[]){DIALOG}, 1, &r) && !refusals);
    CHECK(r.eax == 1 && dialogs.passes == 1 && r16(VA_STOP) == 1 && r16(VA_DEPTH) == 0);
    fresh(quit_journal);
    build_dialog(VA_CONFIRM_SELECT, 2, 0);
    script[2].quit_word = 1;
    script_count = 20;
    CHECK(!row(VA_CONFIRM_RUN, (const uint32_t[]){DIALOG}, 1, &r) && !refusals);
    CHECK(r.eax == 1 && dialogs.passes == 3 && r16(VA_STOP) == 1 && r16(VA_QUIT) == 1);
    /* The confirm's select: slot 0 ends with 1 (slot 1: nothing). */
    fresh(NULL);
    build_dialog(VA_CONFIRM_SELECT, 2, 0);
    CHECK(!row(VA_CONFIRM_SELECT, (const uint32_t[]){DIALOG, GADGET(1)}, 2, &r) && r8(DIALOG + 0x2d1u) == 0);
    CHECK(!row(VA_CONFIRM_SELECT, (const uint32_t[]){DIALOG, GADGET(0)}, 2, &r) && r8(DIALOG + 0x2d1u) == 1);
    CHECK(!row(VA_ALERT_SELECT, (const uint32_t[]){DIALOG, GADGET(1)}, 2, &r) && r8(DIALOG + 0x2d1u) == 3);
    CHECK(r32(VA_NEXT_PRESENT) != 0 && hud.trace_count == 3);
    return 0;
}

/* ---- the key handler: focus moves, presses, the cursor placement. */
static int keys(void) {
    fresh(NULL);
    build_dialog(VA_ALERT_SELECT, 3, 2);
    w32(VA_DEVICE_ID, 2);
    w32(VA_DEVICE_TABLE + 8u, 0x00666000u + 0x3au);
    es2_sim_result r;
    uint32_t rec = VA_RECORD;
    /* Tab: the next slot from -1 is 0; the cursor at the gadget's centre
     * (placed through the cursor rows, the pen set); key flag 2. */
    w8(rec + 0x17u, 1);
    CHECK(!row(VA_KEYS, (const uint32_t[]){DIALOG, rec}, 2, &r) && (r.eax & 0xffu) == 1);
    CHECK(r32(DIALOG + 0x2f7u) == 0 && hud.headless == 0 && es2_sim_cursor_calls_get()->set_pos == 1 && letters('P') == 1 && r8(VA_KEY_FLAGS + 2) == 1);
    CHECK(r32(nth('P', 0)->a0) == 112u && r32(nth('P', 0)->a0 + 4u) == 66u);
    /* 0xf twice: 1 then 2; 0x52 wraps to 0; 0x80f back to 2. */
    w8(rec + 0x17u, 0);
    w16(rec, 0xf);
    CHECK(!row(VA_KEYS, (const uint32_t[]){DIALOG, rec}, 2, &r) && r32(DIALOG + 0x2f7u) == 1);
    CHECK(!row(VA_KEYS, (const uint32_t[]){DIALOG, rec}, 2, &r) && r32(DIALOG + 0x2f7u) == 2);
    w16(rec, 0x52);
    CHECK(!row(VA_KEYS, (const uint32_t[]){DIALOG, rec}, 2, &r) && r32(DIALOG + 0x2f7u) == 0);
    w16(rec, 0x80f);
    CHECK(!row(VA_KEYS, (const uint32_t[]){DIALOG, rec}, 2, &r) && r32(DIALOG + 0x2f7u) == 2);
    /* Unknown key: nothing, EAX 0. */
    w16(rec, 0x99);
    CHECK(!row(VA_KEYS, (const uint32_t[]){DIALOG, rec}, 2, &r) && r.eax == 0 && letters('B') == 0);
    /* Enter (1): the default gadget 2 is pressed (fire: select -> nothing
     * for slot 2, redraw, part; the release entry). */
    w16(rec, 1);
    CHECK(!row(VA_KEYS, (const uint32_t[]){DIALOG, rec}, 2, &r) && (r.eax & 0xffu) == 1);
    CHECK(letters('B') == 1 && nth('B', 0)->a1 == GADGET(2) && letters('r') == 1 && letters('x') == 1);
    /* A gadget in state 2 is not pressed. */
    w16(GADGET(2) + 0x1bu, 2);
    CHECK(!row(VA_KEYS, (const uint32_t[]){DIALOG, rec}, 2, &r) && letters('B') == 1);
    /* The click byte with no focus: focus 0 and key flag 1. */
    w32(DIALOG + 0x2f7u, 0xffffffffu);
    w16(rec, 0);
    w8(rec + 0xdu, 1);
    CHECK(!row(VA_KEYS, (const uint32_t[]){DIALOG, rec}, 2, &r) && r32(DIALOG + 0x2f7u) == 0 && r8(VA_KEY_FLAGS + 1) == 1);
    CHECK(!refusals);
    /* The hit test (-1 with a device): the control record's point over
     * gadget 1 (30..54 x 10..22), then below every gadget. */
    script[0].x = 40;
    script[0].y = 15;
    script_count = 1;
    CHECK(!row(0x00454c98u, (const uint32_t[]){DIALOG, 0xffffffffu}, 2, &r) && r32(DIALOG + 0x2f7u) == 1 && letters('R') == 1);
    script[0].y = 30;
    script_index = 0;
    CHECK(!row(0x00454c98u, (const uint32_t[]){DIALOG, 0xffffffffu}, 2, &r) && r32(DIALOG + 0x2f7u) == 0xffffffffu);
    return 0;
}

/* ---- the release list: an expired entry leaves it (state 0, redrawn),
 * a pending one shows state 1 once. */
static int release(void) {
    fresh(NULL);
    build_dialog(VA_ALERT_SELECT, 3, 0);
    uint32_t now = es2_sim_clock(&loop);
    w16(PANEL + 0x396u, 3);
    w32(PANEL + 0x356u, GADGET(0)); w32(PANEL + 0x35au, now + 1000u);
    w32(PANEL + 0x35eu, GADGET(1)); w32(PANEL + 0x362u, 0);
    w32(PANEL + 0x366u, GADGET(2)); w32(PANEL + 0x36au, now + 1000u);
    es2_sim_result r;
    CHECK(!row(VA_RELEASE_TICK, (const uint32_t[]){PANEL}, 1, &r) && !refusals);
    CHECK(r16(PANEL + 0x396u) == 2 && r32(PANEL + 0x35eu) == GADGET(2) && r32(PANEL + 0x362u) == now + 1000u);
    CHECK(r16(GADGET(0) + 0x1bu) == 1 && r16(GADGET(1) + 0x1bu) == 0 && r16(GADGET(2) + 0x1bu) == 1 && letters('r') == 3);
    CHECK(!row(VA_RELEASE_TICK, (const uint32_t[]){PANEL}, 1, &r) && letters('r') == 3 && hud.trace_count == 5);
    CHECK(!row(0x004531a4u, (const uint32_t[]){PANEL}, 1, &r) && r.eax == 2);
    w16(PANEL + 0x396u, 0xffffu);
    CHECK(!row(0x004531a4u, (const uint32_t[]){PANEL}, 1, &r) && r.eax == 0xffffffffu);
    return 0;
}

/* ---- the list's capacity: with 8 pending entries the press (Enter on the
 * default gadget) is refused at 0x4530bc (and so the key handler 0x454e2c)
 * before the ninth entry is written;
 * a scan with 9 entries is refused at 0x4530fc. */
static int overrun(void) {
    fresh(NULL);
    build_dialog(VA_ALERT_SELECT, 3, 2);
    es2_sim_result r;
    w16(PANEL + 0x396u, 8);
    w32(PANEL + 0x396u + 2u, 0x5a5a5a5au);
    w16(VA_RECORD, 1);
    CHECK(row(VA_KEYS, (const uint32_t[]){DIALOG, VA_RECORD}, 2, &r) && refusals == 2 && first_refused == VA_BUTTON_RELEASE);
    CHECK(letters('B') == 0 && r16(PANEL + 0x396u) == 8 && r32(PANEL + 0x398u) == 0x5a5a5a5au);
    fresh(NULL);
    build_dialog(VA_ALERT_SELECT, 3, 2);
    w16(PANEL + 0x396u, 9);
    CHECK(row(VA_RELEASE_TICK, (const uint32_t[]){PANEL}, 1, &r) && refusals == 2 && first_refused == VA_RELEASE_TICK);
    CHECK(letters('r') == 0);
    return 0;
}

/* ---- the options dialog: five tabs move the focus to gadget 5, the click
 * presses it (select -> pref 8 toggled, label 5 refreshed in two passes),
 * Enter presses the default gadget 10 (the store, end 3); no sound
 * object: the first four gadgets start in state 2 and gadget 0 (focused
 * first) is not pressable. */
static int options(void) {
    fresh(NULL);
    build_dialog(VA_OPTIONS_SELECT, 11, 10);
    w32(DIALOG + 0x346u, LABELS + 0x10u);
    w32(DIALOG + 0x38eu, 0x00aa0000u);
    w32(DIALOG + 0x392u, 0x00aa0001u);
    w32(DIALOG + 0x3bau, STUB_HOOK);
    script[0].key = 0x1cu;         /* presses the focused gadget 0: state 2, nothing */
    for (unsigned k = 1; k <= 5u; ++k) script[k].tab = 1;   /* focus 1..5 */
    script[6].click = 1;           /* presses gadget 5 (option 5: pref 8) */
    script[8].key = 1;             /* the default gadget 10: the store */
    script_count = 20;
    es2_sim_result r;
    CHECK(!row(VA_OPTIONS_RUN, (const uint32_t[]){DIALOG}, 1, &r) && !refusals);
    CHECK(r.eax == 1 && r8(DIALOG + 0x2d1u) == 3 && dialogs.passes == 9);
    CHECK(r16(GADGET(0) + 0x1bu) == 2 && r16(GADGET(3) + 0x1bu) == 2 && r16(GADGET(4) + 0x1bu) == 0);
    CHECK(letters('B') == 2 && nth('B', 0)->a1 == GADGET(5) && nth('B', 1)->a1 == GADGET(10));
    CHECK(r8(VA_PREFS + 8) == 1 && r8(VA_PREFS_PREVIOUS + 8) == 0 && r8(VA_PREFS) == 0);
    CHECK(r32(DIALOG + 0x3b6u) == 5 && r32(DIALOG + 0x2f7u) == 10 && r32(DIALOG + 0x356u) == 0xffffffffu);
    /* Label 5 drawn in passes 7 and 8 with the pref's value. */
    CHECK(letters('L') == 2 && nth('L', 0)->a0 == LABELS + 0x10u && nth('L', 0)->a1 == 0x00aa0001u && nth('L', 0)->pass == 7);
    CHECK(nth('L', 1)->pass == 8 && r8(DIALOG + 0x3c3u) == 0);
    CHECK(letters('S') == 1 && nth('S', 0)->a0 == 9 && nth('S', 0)->a1 == 0x0049e2dcu && nth('S', 0)->pass == 9);
    CHECK(letters('E') == 1 && letters('h') == 9 && letters('V') == 9 && letters('G') == 2 + 2 * 9);
    CHECK(r32(SURFACE + 0x3cu) == 0x33330000u && r8(VA_KEY_FLAGS + 2) == 1);
    /* The cycles: 7 (n 3) from 2 wraps to 0; back from 0 to 2; EAX. */
    w8(VA_PREFS + 7, 2);
    CHECK(!row(VA_PREFS_CYCLE, (const uint32_t[]){7, 3, 1}, 3, &r) && r.eax == 0 && r8(VA_PREFS + 7) == 0 && r8(VA_PREFS_PREVIOUS + 7) == 2);
    CHECK(!row(VA_PREFS_CYCLE_BACK, (const uint32_t[]){7, 3, 1}, 3, &r) && r.eax == 2 && r8(VA_PREFS + 7) == 2);
    CHECK(!row(0x0045999cu, (const uint32_t[]){8, 1}, 2, &r) && r.eax == 0 && r8(VA_PREFS + 8) == 0 && r8(VA_PREFS_PREVIOUS + 8) == 1);
    /* Option 4's label goes through the word table. */
    w8(VA_PREFS + 7, 1);
    w16(0x0049e2b2u + 2u, 0xfffeu);
    w32(DIALOG + 0x342u, LABELS + 0x40u);
    w32(DIALOG + 0x3a2u - 8u, 0x00bb0000u);
    CHECK(!row(0x00457210u, (const uint32_t[]){DIALOG, 4}, 2, &r) && letters('L') == 3 && nth('L', 2)->a0 == LABELS + 0x40u && nth('L', 2)->a1 == 0x00bb0000u);
    CHECK(!row(0x00457210u, (const uint32_t[]){DIALOG, 9}, 2, &r) && letters('L') == 3);
    /* Gadget 9: gadget 9 drawn, the page built in a local, run, the dialog
     * drawn, the page destroyed with 2 (the stubbed run ends the dialog). */
    fresh(NULL);
    build_dialog(VA_OPTIONS_SELECT, 11, 9);
    script[0].key = 1;
    script_count = 20;
    CHECK(!row(VA_OPTIONS_RUN, (const uint32_t[]){DIALOG}, 1, &r) && !refusals && r.eax == 1);
    CHECK(letters('J') == 1 && letters('j') == 1 && letters('k') == 1);
    unsigned at_page = 0;
    while (calls[at_page].letter != 'J') ++at_page;
    const call_log *ctor = &calls[at_page];
    CHECK(at_page > 0 && calls[at_page - 1].letter == 'g' && calls[at_page - 1].a0 == GADGET(9));
    CHECK(ctor->a0 >= local_guest && ctor->a0 + 0x480u <= local_guest + 0x10000u && ctor->a1 == 1 && ctor->a2 == 0);
    CHECK(calls[at_page + 1].letter == 'j' && calls[at_page + 1].a0 == ctor->a0 && calls[at_page + 2].letter == 'D');
    CHECK(nth('k', 0)->a0 == ctor->a0 && nth('k', 0)->a1 == 2 && r32(DIALOG + 0x356u) == 0xffffffffu);
    return 0;
}

/* ---- the pass limit: a dialog without input is refused after the limit. */
static int pass_limit(void) {
    fresh(NULL);
    build_dialog(VA_ALERT_SELECT, 2, 0);
    dialogs.pass_limit = 3;
    script_count = 0;
    es2_sim_result r;
    CHECK(row(VA_ALERT_RUN, (const uint32_t[]){DIALOG}, 1, &r) && refusals >= 1 && first_refused == VA_ALERT_RUN);
    CHECK(dialogs.passes == 4 && script_index == 3);
    return 0;
}

/* ---- WM_CLOSE and WM_QUERYENDSESSION: the command handler's quit dialog
 * (0x45feb8 with 0x410), its answer at 0x4d370c; the demo word stops
 * instead. */
static int close_messages(void) {
    fresh(NULL);
    uint32_t result = 7;
    CHECK(!es2_sim_window_proc(&loop, 0x10001u, 0x10u, 0, 0, &result) && result == 0);
    CHECK(letters('K') == 1 && nth('K', 0)->a0 == 0x410u && r16(VA_CLOSE_RESULT) == 2);
    command_answer = 1;
    CHECK(!es2_sim_window_proc(&loop, 0x10001u, 0x11u, 0, 0, &result) && result == 0 && r16(VA_CLOSE_RESULT) == 1);
    command_answer = 2;
    CHECK(!es2_sim_window_proc(&loop, 0x10001u, 0x11u, 0, 0, &result) && result == 1 && letters('K') == 3);
    w16(VA_DEMO, 1);
    CHECK(!es2_sim_window_proc(&loop, 0x10001u, 0x10u, 0, 0, &result) && letters('K') == 3 && r16(VA_STOP) == 1);
    CHECK(!refusals);
    return 0;
}

int main(void) {
    image = calloc(1, IMAGE_SIZE);
    heap_bytes = calloc(1, HEAP_SIZE);
    for (uint32_t a = 0x30000000u; !local && a < 0x38000000u; a += 0x01000000u)
        local = VirtualAlloc((void *)(uintptr_t)a, LOCAL_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    CHECK(image && heap_bytes && local);
    local_guest = (uint32_t)(uintptr_t)local;
    CHECK(es2_sim_arena_add(&arena, IMAGE_BASE, image, IMAGE_SIZE));
    CHECK(es2_sim_arena_add(&arena, HEAP_BASE, heap_bytes, HEAP_SIZE));
    CHECK(es2_sim_arena_add(&arena, local_guest, local, LOCAL_SIZE));
    es2_sim_arena_memory(&arena, &memory);
    es2_journal_init(&journal);
    fresh(NULL);
    CHECK(setup_dispatch() == 0);
    if (alert_journal() || stop_and_quit() || keys() || release() || overrun() || options() || pass_limit() || close_messages()) {
        if (refusals) fprintf(stderr, "first refusal 0x%08x\n", (unsigned)first_refused);
        return 1;
    }
    puts("PASS: dialogs: the alert answered by the journal's pump-keyed entry in its clock order, the stop and "
         "quit words, the key handler and hit test, the release list and its capacity, the options dialog (toggle, label, store, page 2 "
         "refused), the pass limit, WM_CLOSE/WM_QUERYENDSESSION through the quit dialog");
    return 0;
}
