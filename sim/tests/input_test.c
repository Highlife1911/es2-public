/* The input layer (sim/src/input.c) on a synthetic image: zeroed host memory
 * with the literals and tables the code reads written by this test at the
 * original's addresses, the game heap over an emulated OS region, the main
 * loop's call area at its own host address. The sim's real services answer the
 * heap, string, handler-list and clock calls and the input rows; the hooks the
 * set-up calls (prefs_load, keyjoy_load, prefs_store, mouse_queue) and fopen
 * are recording services. No game data. The comparison with the original's
 * machine code is //tools/reference-game/invitro:differential_input_layer_test. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
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
#define TYPE_RECORD (IMAGE_BASE + 0x2d7f00u)

static uint8_t *image, *heap_bytes, *local;
static uint32_t local_guest;
static es2_sim_arena arena;
static es2_guest_memory memory;
static es2_sim_heap heap;
static es2_sim_services services;
static es2_sim_entry storage[4096];
static es2_sim_dispatch table;
static es2_sim_loop loop;
static es2_sim_input input;
static es2_journal journal;
static es2_vt vt;
static es2_sim_refusal last;
static unsigned refusals;
static void on_refuse(const es2_sim_refusal *r, void *user) { (void)user; last = *r; ++refusals; }
static struct { uint32_t code, site; } faulted;
static void record_fault(void *user, uint32_t code, uint32_t site) { (void)user; faulted.code = code; faulted.site = site; }
static es2_sim_process_platform recorder = {NULL, NULL, NULL, NULL, record_fault};
static void flat_client(void *user, int32_t *width, int32_t *height) { (void)user; *width = 320; *height = 0; }

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
static void text(uint32_t a, const char *t) { do w8(a++, (uint8_t)*t); while (*t++); }
static void guest_string(uint32_t a, char *out, unsigned n) { unsigned i = 0; for (; i + 1 < n && r8(a + i); ++i) out[i] = (char)r8(a + i); out[i] = 0; }

/* ---- recording services */
static char calls[256];
static unsigned call_count;
static void note(char c) { if (call_count + 1 < sizeof(calls)) calls[call_count++] = c; calls[call_count] = 0; }
static uint32_t fopen_result, queued[8][2], queue_count;
static char opened[128];
static void svc_record(es2_sim_service_call *c, void *u) {
    (void)u;
    switch (c->va) {
    case 0x00459770u: note('p'); break;   /* prefs_load */
    case 0x0045b7a8u: note('k'); c->edx = 0x1234u; c->ecx = 0x5678u; break;   /* keyjoy_load */
    case 0x00459b94u: note('s'); break;   /* prefs_store */
    case 0x00496802u: note('f'); guest_string(c->slots[0], opened, sizeof(opened)); c->eax = fopen_result; break;
    case 0x00496838u: note('c'); c->eax = 0; break;
    case 0x00453078u: note('q'); if (queue_count < 8) { queued[queue_count][0] = c->slots[0]; queued[queue_count][1] = c->slots[1]; ++queue_count; } break;
    default: note('?'); break;
    }
}
static int profile_file(void *u, const char *name, uint8_t *buffer, uint32_t capacity, uint32_t *size) {
    (void)u;
    static const char ini[] = "[Keyjoy]\r\nTilt = Reverse\r\n;; comment\r\nRudder = Default\r\n";
    if (strcmp(name, "C:\\G\\data\\keyjoy.cfg")) return 1;
    memcpy(buffer, ini, sizeof(ini) - 1u < capacity ? sizeof(ini) - 1u : capacity);
    *size = (uint32_t)(sizeof(ini) - 1u);
    return 0;
}

static int setup_dispatch(void) {
    es2_sim_dispatch_init(&table, storage, 4096, on_refuse, NULL);
    services.memory = &memory;
    services.heap = &heap;
    services.vt = &vt;
    static uint32_t fs0 = 0x00abcdefu;
    services.fs0 = &fs0;
    if (es2_sim_register_services(&table, &services)) return 1;
    static const struct { uint32_t va; uint8_t slots; } extra[] = {
        {0x00459770u, 1}, {0x0045b7a8u, 0}, {0x00459b94u, 2}, {0x00453078u, 2},
    };
    for (unsigned i = 0; i < sizeof(extra) / sizeof(extra[0]); ++i)
        if (es2_sim_dispatch_add_service(&table, extra[i].va, "test", svc_record, NULL, 0, extra[i].slots)) return 1;
    for (unsigned i = 0; i < table.count; ++i)
        if (storage[i].va == 0x00496802u || storage[i].va == 0x00496838u) { storage[i].service = svc_record; storage[i].service_user = NULL; }
    const char *error = NULL;
    uint32_t bad = 0;
    return es2_sim_dispatch_seal(&table, &error, &bad);
}

/* The literals and tables the input code reads (each at its VA in the
 * reference image; the values here are this test's). */
static void image_literals(void) {
    memset(image, 0, IMAGE_SIZE);
    w32(0x006cbee4u, COUNTER);
    text(0x004d27bcu, "C:\\G\\");                  /* the install root */
    text(0x0049e994u, "data\\prefs.cfg");
    text(0x0049ea82u, "vol\\"); text(0x0049ea87u, ".vol"); text(0x0049ea8cu, "rb");
    text(0x0049f259u, "simvoice");
    text(0x0049ebe0u + 0x14u, "data\\keyjoy.cfg");
    w16(0x004a10e8u, 0x0101u);
    /* The joystick class: vtable 0x49ec74 (destructor, poll), [vt - 4] = 0,
     * the type record at [vt - 0xc] with the dword +0x20 = 2. */
    w32(0x0049ec74u, 0x0045c2e0u); w32(0x0049ec78u, 0x0045c330u); w32(0x0049ec70u, 0);
    w32(0x0049ec68u, TYPE_RECORD); w32(TYPE_RECORD + 0x20u, 2);
    w32(0x004d253eu, 320); w32(0x004d2542u, 240);
    w32(0x004d3040u, 640); w32(0x004d3044u, 480);
    w32(0x004d3610u, 0x00010001u);
    w32(0x004d36ccu, 100); w32(0x004d36d0u, 50); w32(0x004d36d4u, 328); w32(0x004d36d8u, 266);
    w8(0x004d2536u, 'g');
}

static void fresh(void) {
    image_literals();
    memset(heap_bytes, 0, HEAP_SIZE);
    memset(local, 0, LOCAL_SIZE);
    next_heap = HEAP_BASE;
    es2_sim_heap_os os = {NULL, heap_alloc, os_free};
    es2_sim_heap_init(&heap, &memory, &os, local_guest + 0x7f000u, 0, 0, 0);
    memset(&vt, 0, sizeof(vt));
    es2_vt_enable(&vt, 1, 47, 0x01000000u, 0, 1);
    es2_sim_loop_defaults(&loop);
    loop.memory = &memory;
    loop.dispatch = &table;
    loop.vt = &vt;
    loop.stack_lo = local_guest;
    loop.stack_hi = local_guest + 0x10000u;
    es2_sim_loop_bind(&loop);
    es2_journal_free(&journal);
    es2_journal_init(&journal);
    es2_sim_input_headless(&input, &loop, &journal, 0, 0);
    input.read_file = profile_file;
    es2_sim_input_bind(&input);
    call_count = 0; calls[0] = 0;
    refusals = 0; fopen_result = 0; queue_count = 0;
}
static int load(const char *t) { return es2_journal_load_text(&journal, t, strlen(t), NULL); }
static int row(uint32_t va, const uint32_t *slots, unsigned n, es2_sim_result *r) {
    uint32_t regs[3] = {0, 0, 0};
    /* Slots must be guest-addressable: copy them into the call area. */
    uint32_t at = local_guest + 0x20000u;
    for (unsigned i = 0; i < n; ++i) w32(at + 4u * i, slots[i]);
    return es2_sim_call(&table, va, (const uint32_t *)(uintptr_t)at, n, regs, r);
}

/* ---- 1. the set-up without a joystick, without the voice archive */
static int setup(void) {
    fresh();
    w8(0x004d1f38u + 0xcu, 1);   /* prefs byte 0xc: joystick type 1 (prefs_load is a recorder here) */
    es2_sim_result r;
    memset(&r, 0, sizeof(r));
    CHECK(es2_sim_input_setup(&input, &r) == 0 && refusals == 0);
    CHECK(!strcmp(calls, "pfk") && r.edx == 0x1234u && r.ecx == 0x5678u);
    CHECK(!strcmp(opened, "C:\\G\\vol\\simvoicg.vol") && r8(0x0049f260u) == 'g');
    /* The keyboard K (id 1) and the mouse M (id 2) from the heap, both active
     * and current; their window handlers in slots 0 and 1. */
    uint32_t k = r32(0x004d246cu) - 4u, m = r32(0x004d2470u) - 0x3au;
    CHECK(k >= HEAP_BASE && m >= HEAP_BASE && r32(0x004d249cu) == 1 && r32(0x004d24a0u) == 2);
    CHECK(r32(k + 9u) == 0x10000u && r32(k + 5u) == 1 && r8(k + 4u) == 1 && r8(0x004d248du) == 1);
    CHECK(r32(m + 0x3bu) == 2 && r8(m + 0x3au) == 1 && r8(0x004d248eu) == 1);
    CHECK(r32(0x006c5eb8u) == k && r32(0x006c5ebcu) == m);
    CHECK(r32(0x004d3b30u) == 0x00477becu && r32(0x004d3b34u) == 0x004809f8u && r32(0x004d3b38u) == 0);
    /* Hot keys: the empty callback, then the flight and command keys. */
    CHECK(r32(0x006bb8b8u) == 1 && r32(0x006bb8bcu) == 0x0049eabcu && r32(0x006bb8c0u) == 0x00477a7cu);
    CHECK(r32(0x006bb8c4u) == 1 && r32(0x006bb8c8u) == 0x0049eb0bu && r32(0x006bb8ccu) == 0x0045a324u);
    CHECK(r32(0x006bb8d0u) == 1 && r32(0x006bb8d4u) == 0x0049eb35u && r32(0x006bb8d8u) == 0x0045a498u);
    CHECK(r8(0x0049eaa5u) == 1);
    /* The prefs callbacks, the path, the voice result, the bytes it clears. */
    CHECK(r32(0x004d1fdcu) == 0x00459cb4u && r32(0x004d1fe0u) == 0x00459c88u && r32(0x004d1fe4u) == 0x00459ce0u);
    CHECK(r32(0x004d1fdcu + 4u * 0xeu) == 0x00459d3cu && r32(0x004d1fdcu + 4u * 8u) == 0x00459d68u);
    char path[64];
    guest_string(0x0049e81cu, path, sizeof(path));
    CHECK(!strcmp(path, "C:\\G\\data\\prefs.cfg"));
    CHECK(r8(0x0049e9a5u) == 0 && r8(0x0049f966u) == 0 && r8(0x0049e9a4u) == 0xf0u && r8(0x004d20b4u) == 0);
    /* No joystick: both caps calls fail (the journal's fallback), the device
     * is destroyed and its slot cleared; bytes 0xd..0x18 cleared. */
    CHECK(r32(0x006bb6a0u) == ES2_JOYERR_PARMS && r32(0x006bb6a4u) == ES2_JOYERR_PARMS && journal.fallbacks == 2);
    CHECK(r32(0x004d24a4u) == 0 && r32(0x006c5ec0u) == 0 && r8(0x004d248fu) == 0);
    CHECK(r16(0x006bb6a8u) == 1 && r8(0x006bb6acu) == 0 && r16(0x0049a06eu) == 0);
    /* The device block and the record's tables. */
    CHECK(r32(0x004d2390u) == 0x004d23f6u && r32(0x004d2310u) == 0x004d22c6u + 0xeu && r32(0x004d233cu) == 0x004d22c6u + 0x1du);
    CHECK(r32(0x004d2350u) == 0x004d23f6u + 0xdu && r32(0x004d235cu) == 0x004d23f6u + 0x19u);
    CHECK(r32(0x004d2340u) == 0x004d23c1u && r32(0x004d236cu) == 0x004d23d4u && r32(0x004d238cu) == 0x004d23f6u + 0x24u);
    CHECK(r32(COUNTER) == 0);
    return 0;
}

/* ---- 2. the device poll: a key, the mouse */
static int poll(void) {
    uint32_t k = r32(0x004d246cu) - 4u;
    w32(0x006bb8b0u, 5); w32(0x006bb8b4u, 4);
    w16(0x006bb6b0u + 8u, 0x3248u);
    w32(k, r32(k) | 2u);
    w32(0x006c5f50u, 77); w32(0x006c5f54u, 66); w16(0x006c5f58u, 3);
    es2_sim_result r;
    CHECK(es2_sim_device_poll(&input, &r) == 0 && refusals == 0 && r.eax == 0x004d23f6u);
    CHECK(r16(0x004d23f6u) == (0x3248u & 0xcfffu) && r32(0x006bb8b4u) == 5 && !(r32(k) & 2u));
    CHECK(r32(0x004d23f8u) == 77 && r32(0x004d23fcu) == 66 && r8(0x004d2400u) == 1 && r8(0x004d2401u) == 1 && r8(0x004d2402u) == 0);
    CHECK(r32(0x004d241fu) == 77 && r8(0x0049ec64u) == 1);
    CHECK(r.ecx == 77 && r.edx == 3);
    /* The reader for another id answers AL 1 and leaves the word. */
    CHECK(es2_sim_key_reader(&input, 0x004d23f6u, 2, &r) == 0 && (r.eax & 0xffu) == 1u);
    /* The key flush row. */
    uint32_t f[1] = {k};
    w32(0x006bb8b0u, 9); w32(k, 3);
    CHECK(row(0x00477abcu, f, 1, &r) == 0 && r32(0x006bb8b4u) == 9 && r32(k) == 1 && r.eax == k && r.edx == 9);
    return 0;
}

/* ---- 3. the platform rows: the journal's answers and fallbacks */
static int platform(void) {
    fresh();
    CHECK(load("{\"f\":3,\"api\":\"key\",\"a\":16,\"b\":-32768}\n"
               "{\"f\":3,\"api\":\"joypos\",\"a\":1,\"b\":0,\"hex\":\"3400000001000000\"}\n"
               "{\"f\":3,\"api\":\"cursor\",\"a\":12,\"b\":34,\"c\":1}\n") == 0);
    w32(0x004d257bu, 3);
    es2_sim_result r;
    uint32_t a[6];
    a[0] = 0x10;
    CHECK(row(0x0049695eu, a, 1, &r) == 0 && r.eax == 0xffff8000u);
    a[0] = 0x11;
    CHECK(row(0x0049695eu, a, 1, &r) == 0 && r.eax == 0 && journal.fallbacks == 1);
    /* joyGetPosEx: id 0 unplugged; id 1 with an 8-byte blob only copied
     * when the buffer's dwSize is 8. */
    uint32_t info = local_guest + 0x30000u;
    w32(info, 0x34u);
    a[0] = 0; a[1] = info;
    CHECK(row(0x00496a6cu, a, 2, &r) == 0 && r.eax == ES2_JOYERR_UNPLUGGED && journal.fallbacks == 2);
    a[0] = 1;
    CHECK(row(0x00496a6cu, a, 2, &r) == 0 && r.eax == 0 && r32(info + 4u) == 0);
    a[0] = 0; a[1] = local_guest + 0x30100u; a[2] = 0x328u;
    CHECK(row(0x00496a66u, a, 3, &r) == 0 && r.eax == ES2_JOYERR_PARMS && journal.fallbacks == 3);
    /* GetCursorPos: the entry, then the virtual cursor (320, 240). */
    a[0] = local_guest + 0x30200u;
    CHECK(row(0x0049696au, a, 1, &r) == 0 && r.eax == 1 && r32(a[0]) == 12 && r32(a[0] + 4u) == 34);
    CHECK(row(0x0049696au, a, 1, &r) == 0 && r32(a[0]) == 320 && r32(a[0] + 4u) == 240 && journal.fallbacks == 4);
    /* The window: the client area is the game size, the rectangle 0x4d36cc's. */
    a[0] = 0x00010001u; a[1] = local_guest + 0x30300u;
    CHECK(row(0x00496970u, a, 2, &r) == 0 && r.eax == 1 && r32(a[1] + 8u) == 320 && r32(a[1] + 12u) == 240 && r32(a[1]) == 0);
    CHECK(row(0x0049694cu, a, 2, &r) == 0 && r32(a[1]) == 100 && r32(a[1] + 4u) == 50 && r32(a[1] + 8u) == 428 && r32(a[1] + 12u) == 316);
    /* GetPrivateProfileStringA: a value, a missing key (the default), a
     * missing file, the cut to size - 1. */
    uint32_t s = local_guest + 0x30400u;
    text(s, "Keyjoy"); text(s + 0x10u, "Tilt"); text(s + 0x20u, "Rudder"); text(s + 0x30u, "Missile"); text(s + 0x40u, "");
    text(s + 0x50u, "C:\\G\\data\\keyjoy.cfg"); text(s + 0x70u, "C:\\G\\none.cfg");
    char out[64];
    uint32_t buf = s + 0x100u;
    uint32_t p[6] = {s, s + 0x10u, s + 0x40u, buf, 0x28u, s + 0x50u};
    CHECK(row(0x00496910u, p, 6, &r) == 0 && r.eax == 7);
    guest_string(buf, out, sizeof(out)); CHECK(!strcmp(out, "Reverse"));
    p[1] = s + 0x30u; text(s + 0x40u, "dflt");
    CHECK(row(0x00496910u, p, 6, &r) == 0 && r.eax == 4);
    guest_string(buf, out, sizeof(out)); CHECK(!strcmp(out, "dflt"));
    p[1] = s + 0x10u; p[5] = s + 0x70u;
    CHECK(row(0x00496910u, p, 6, &r) == 0);
    guest_string(buf, out, sizeof(out)); CHECK(!strcmp(out, "dflt"));
    p[5] = s + 0x50u; p[4] = 4;
    CHECK(row(0x00496910u, p, 6, &r) == 0 && r.eax == 3);
    guest_string(buf, out, sizeof(out)); CHECK(!strcmp(out, "Rev"));
    CHECK(input.profile_reads == 4 && refusals == 0);
    return 0;
}

/* ---- 4. the mouse: the queue set-up, a button through the handler */
static int mouse(void) {
    fresh();
    es2_sim_result r;
    CHECK(es2_sim_input_setup(&input, &r) == 0);
    CHECK(es2_sim_mouse_queue_setup(&input, &r) == 0 && refusals == 0 && r.eax == 0x1eu);
    CHECK(r32(0x006c5ed8u) == 1 && r32(0x006c5edcu) == 0x1eu && r32(0x006c5ee0u) == 0x00452cdcu && r16(0x0049dbd4u) == 0);
    CHECK(r32(0x004d1df8u) >= HEAP_BASE && r32(0x004d1e00u) >= HEAP_BASE && r32(0x004d1e04u) == 0x004d1df4u && r32(0x004d1decu) == 30);
    w8(0x004d1dd6u, 1);
    uint32_t result = 0;
    /* WM_LBUTTONDOWN at (100, 50): scale 640 << 15 / 320 = 0x10000 (x 1). */
    CHECK(es2_sim_mouse_handler(&input, 0x0201u, 1, 100u | 50u << 16, &result) == 0 && result == 1 && refusals == 0);
    CHECK(r32(0x004a2dccu) == 0x10000u && r32(0x006c5f50u) == 100 && r32(0x006c5f54u) == 50 && r16(0x006c5f58u) == 1);
    CHECK(queue_count == 1 && queued[0][0] == 0x006c5f50u && r32(0x004d1e0cu) == 1);
    /* WM_MOUSEMOVE: no callback (mask 0x1e), result 1; WM_RBUTTONDOWN: the
     * callback, result 0. */
    CHECK(es2_sim_mouse_handler(&input, 0x0200u, 0, 7u, &result) == 0 && result == 1 && queue_count == 1);
    CHECK(es2_sim_mouse_handler(&input, 0x0204u, 0, 8u, &result) == 0 && result == 0 && queue_count == 2 && r16(0x006c5f58u) == 2);
    /* The bounds over a surface of 320 x 240. */
    uint32_t surface = local_guest + 0x30000u, m = r32(0x004d2470u) - 0x3au;
    w32(surface + 8u, 320); w32(surface + 0xcu, 240);
    w32(0x006c5ed0u, surface);
    CHECK(es2_sim_mouse_bounds(&input) == 0 && r32(m + 0x16u) == 319 && r32(m + 0x1au) == 239 && r16(m + 0x1eu) == 0);
    /* A joystick id without an object is refused, not run (the destroyed
     * joystick of the set-up left its record in slot 3: cleared here). */
    w32(0x004d24a4u, 3);
    w32(0x004d2474u, 0);
    CHECK(es2_sim_device_poll(&input, &r) == 1 && refusals == 1 && last.va == 0x00477787u);
    /* With process_fault the same row ends like the original's read through
     * NULL; WM_SIZE with a client height of 0 ends at the IDIV 0x4808cf. */
    loop.process = &recorder;
    CHECK(es2_sim_device_poll(&input, &r) == 1 && refusals == 1);
    CHECK(loop.outcome == ES2_SIM_LOOP_EXITED && loop.exit_code == ES2_SIM_ACCESS_VIOLATION && faulted.site == 0x00477787u);
    loop.outcome = ES2_SIM_LOOP_RETURNED;
    w32(0x004d3610u, 0x00010001u);
    input.platform.client_size = flat_client;
    CHECK(es2_sim_mouse_handler(&input, 0x0005u, 0, 0, &result) == 1 && refusals == 1);
    CHECK(loop.outcome == ES2_SIM_LOOP_EXITED && loop.exit_code == 0xc0000094u && faulted.site == 0x004808cfu);
    loop.process = NULL;
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
    fresh();
    CHECK(setup_dispatch() == 0);
    if (setup() || poll() || platform() || mouse()) return 1;
    puts("PASS: input: the set-up's devices, hot keys, prefs callbacks, joystick attempt and tables; the device poll with "
         "a key and the mouse; the journal platform rows, the window rectangles and the profile reader; the mouse queue, "
         "handler, callback and bounds; a refusal, and with process_fault the access violation and the mouse scale's divide fault");
    return 0;
}
