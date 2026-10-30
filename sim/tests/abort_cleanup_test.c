/* The mission start's abort-notify clean-up 0x461304 and the remap table's
 * release 0x46a4ac (src/loop.c; own_process.md, "the mission start's
 * clean-up 0x461304 and the call helpers' EXITED"), on a synthetic image:
 * zeroed host memory, the game heap over an emulated OS region, the sim's own
 * rows with this test's recording stubs in place of the callees (the row's
 * service replaced after the seal).
 *  1. The rows: per tape stream, dump stream and remap flag, the calls in
 *     the original's order with their arguments and entry EAX, the globals
 *     cleared, and EAX, EDX and ECX as the listing leaves them (without a
 *     tape the dump is not looked at).
 *  2. The error report 0x479c78 (its own row) with 0x461304 in its notify
 *     list: the clean-up runs before the box; headless the run stops
 *     refused at __exit 0x49679c, with a recording process record it ends
 *     EXITED with code 1.
 *  3. A process end under a call helper: the
 *     sound close 0x462734 (audio.c's own row) reaches 0x466010, whose stub
 *     calls the report with the recording record (the process ends there)
 *     and refuses. The outcome stays ES2_SIM_LOOP_EXITED with code 1 through
 *     audio.c's call helper and 0x461304's. No game data. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "es2/sim/audio.h"
#include "es2/sim/loop.h"
#include "es2/sim/memory.h"
#include "es2/sim/services.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

#define IMAGE_BASE 0x00400000u
#define IMAGE_SIZE 0x002d8000u
#define HEAP_BASE 0x09000000u
#define HEAP_SIZE 0x00400000u
#define LOCAL_SIZE 0x00080000u
#define COUNTER (IMAGE_BASE + 0x2d7ff0u)

#define CLEANUP 0x00461304u
#define REMAP 0x0046a4acu
#define SOUND_CLOSE 0x00462734u
#define FCLOSE 0x00496838u
#define OP_DELETE 0x00477548u
#define HANDLER_REMOVE 0x00466010u
#define REPORT 0x00479c78u
#define RECORD 0x006c56c8u
#define RT_OPEN 0x004967c6u
#define RT_WRITE 0x0049677eu
#define RT_CLOSE 0x00496874u
#define TAPE 0x004d24e2u
#define DUMP 0x004d24deu
#define CODE 0x004d27b8u
#define REMAP_FLAG 0x004a0918u
#define REMAP_TABLE 0x006b4f20u
#define REMAP_CLEARED 0x006b4f12u
#define NOTIFY_HEAD 0x006c56d8u
#define SOUND_CONTEXT 0x0049f8ecu
#define CD_DEVICE 0x004a0e2cu

static uint8_t *image, *heap_bytes, *local;
static uint32_t local_guest;
static es2_sim_arena arena;
static es2_guest_memory memory;
static es2_sim_heap heap;
static es2_sim_services services;
static es2_sim_entry storage[4096];
static es2_sim_dispatch table;
static es2_sim_loop loop;
static es2_sim_audio audio;
static es2_vt vt;
static uint32_t first_refused;
static unsigned refusals;
static void on_refuse(const es2_sim_refusal *r, void *user) {
    (void)user;
    if (!refusals) first_refused = r->va;
    ++refusals;
    if (getenv("ABORT_CLEANUP_VERBOSE")) fprintf(stderr, "refused 0x%08x: %s\n", (unsigned)r->va, r->reason ? r->reason : "");
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
static void w32(uint32_t a, uint32_t v) { memory.write32(memory.context, a, v); }
static void w16(uint32_t a, uint16_t v) { memory.write16(memory.context, a, v); }
static void w8(uint32_t a, uint8_t v) { es2_sim_write8(&memory, a, v); }
static void text(uint32_t a, const char *t) { do w8(a++, (uint8_t)*t); while (*t++); }

/* ---- the recording stubs */
typedef struct call { uint32_t va, s0, eax, edx, ecx; } call;
static call log_[64];
static unsigned logged;
typedef struct stub { uint32_t va; int real; es2_sim_service_fn orig; void *orig_user; } stub;
static stub stubs[] = {
    {SOUND_CLOSE, 0, NULL, NULL}, {FCLOSE, 0, NULL, NULL},  {OP_DELETE, 0, NULL, NULL},   {HANDLER_REMOVE, 0, NULL, NULL},
    {RT_OPEN, 0, NULL, NULL},     {RT_WRITE, 0, NULL, NULL}, {RT_CLOSE, 0, NULL, NULL},
};
#define NSTUBS (sizeof(stubs) / sizeof(stubs[0]))
static stub *stub_of(uint32_t va) {
    for (unsigned i = 0; i < NSTUBS; ++i) if (stubs[i].va == va) return &stubs[i];
    return NULL;
}
static const es2_sim_process_platform *exit_record;   /* case 3: 0x466010's stub ends the process */
static unsigned fcloses;
static void svc_stub(es2_sim_service_call *c, void *u) {
    const stub *st = u;
    if (st->real) { st->orig(c, st->orig_user); return; }
    if (logged < 64) {
        call *k = &log_[logged];
        k->va = c->va; k->s0 = c->slots[0]; k->eax = c->eax; k->edx = c->edx; k->ecx = c->ecx;
    }
    ++logged;
    switch (c->va) {
    case SOUND_CLOSE: c->eax = 0xa1a1a1a1u; c->edx = 0xa2a2a2a2u; c->ecx = 0xa3a3a3a3u; break;
    case FCLOSE: c->eax = 0xf0f0f000u + fcloses++; c->edx = 0xfefefefeu; c->ecx = 0xfcfcfcfcu; break;
    case OP_DELETE: c->eax = 0xd1d1d1d1u; c->edx = 0xd2d2d2d2u; c->ecx = 0xd3d3d3d3u; break;
    case HANDLER_REMOVE:
        if (exit_record) {
            /* the process ends under this call: the report with the record */
            const uint32_t s[6] = {RECORD, 5u, 0, 0x004a69d4u, 0x15u, 0};
            es2_sim_result r;
            loop.process = exit_record;
            es2_sim_loop_call(&loop, REPORT, s, 6, &r);
            c->refused = 1; c->reason = "the process ended under 0x466010 (this test)";
        }
        c->eax = 0;
        break;
    case RT_OPEN: c->eax = 5; break;
    case RT_WRITE: c->eax = c->slots[2]; break;
    default: c->eax = 0; break;
    }
}
static int setup_dispatch(void) {
    es2_sim_dispatch_init(&table, storage, 4096, on_refuse, NULL);
    services.memory = &memory;
    services.heap = &heap;
    services.vt = &vt;
    static uint32_t fs0 = 0x00abcdefu;
    services.fs0 = &fs0;
    if (es2_sim_register_services(&table, &services)) return 1;
    const char *error = NULL;
    uint32_t bad = 0;
    if (es2_sim_dispatch_seal(&table, &error, &bad)) return 1;
    for (unsigned i = 0; i < NSTUBS; ++i) {
        es2_sim_entry *e = (es2_sim_entry *)es2_sim_dispatch_find(&table, stubs[i].va);
        if (!e || e->kind != ES2_SIM_SERVICE) { fprintf(stderr, "no row 0x%08x\n", (unsigned)stubs[i].va); return 1; }
        stubs[i].orig = e->service;
        stubs[i].orig_user = e->service_user;
        e->service = svc_stub;
        e->service_user = &stubs[i];
    }
    return 0;
}
static void report_literals(void) {
    text(0x004a159cu, "HEAD");
    text(0x004a1662u, "|");
    text(0x004a1664u, "Cap5");
    text(0x004a1540u, "t.log");
    text(0x004a1635u, "%s:%d:%s");
    text(0x004a1651u, "=%s");
    text(0x004a1614u, "\r\n");
    text(0x004c7010u, "Sev5");
    w32(0x004a15f4u + 20u, 0x004c7010u);
    text(0x004a69d4u, "f.cpp");
}
static void fresh(void) {
    memset(image, 0, IMAGE_SIZE);
    w32(0x006cbee4u, COUNTER);
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
    es2_sim_audio_headless(&audio, &loop, NULL);
    es2_sim_audio_bind(&audio);
    refusals = 0; first_refused = 0;
    logged = 0; fcloses = 0;
    exit_record = NULL;
    for (unsigned i = 0; i < NSTUBS; ++i) stubs[i].real = 0;
    report_literals();
}
static const uint32_t ENTRY[3] = {0x11111111u, 0x22222222u, 0x33333333u};
static int row(uint32_t va, es2_sim_result *r) {
    uint32_t at = local_guest + 0x20000u;
    memset(r, 0, sizeof(*r));
    return es2_sim_call(&table, va, (const uint32_t *)(uintptr_t)at, 0, ENTRY, r);
}

/* ---- 1. the rows */
static int rows(void) {
    for (unsigned k = 0; k < 6u; ++k) {
        unsigned streams = k % 3u;   /* 0: no tape (a dump set), 1: the tape only, 2: both */
        int held = k >= 3u;
        uint32_t tape = streams ? 0x09001000u : 0, dump = streams != 1u ? 0x09002000u : 0, block = 0x09003000u;
        fresh();
        w32(TAPE, tape); w32(DUMP, dump);
        w16(REMAP_FLAG, held ? 1u : 0u);
        w32(REMAP_TABLE, block);
        w32(REMAP_CLEARED, 0x5a5a5a5au);
        w32(CODE, 4u);
        es2_sim_result r;
        CHECK(row(CLEANUP, &r) == 0 && refusals == 0);
        unsigned i = 0;
        /* the sound close, entered with the row's entry registers */
        CHECK(log_[i].va == SOUND_CLOSE && log_[i].eax == ENTRY[0] && log_[i].edx == ENTRY[1] && log_[i].ecx == ENTRY[2]);
        ++i;
        uint32_t eax = 0, edx = 0xa2a2a2a2u, ecx = 0xa3a3a3a3u;
        if (tape) {
            CHECK(log_[i].va == FCLOSE && log_[i].s0 == tape && log_[i].eax == tape && log_[i].edx == 0xa2a2a2a2u && log_[i].ecx == 0xa3a3a3a3u);
            ++i;
            edx = 0; ecx = tape;
            if (dump) {
                CHECK(log_[i].va == FCLOSE && log_[i].s0 == dump && log_[i].eax == dump && log_[i].edx == 0 && log_[i].ecx == tape);
                ++i;
                eax = 0xf0f0f001u; ecx = dump;
            }
        }
        if (held) {
            CHECK(log_[i].va == OP_DELETE && log_[i].s0 == block && log_[i].eax == block && log_[i].edx == edx && log_[i].ecx == ecx);
            ++i;
            eax = 0xd1d1d1d1u; edx = 0;
        }
        CHECK(logged == i);
        CHECK(r.eax == eax && r.edx == edx && r.ecx == 0);
        CHECK(r32(TAPE) == 0 && r32(DUMP) == (tape ? 0u : dump) && r32(CODE) == 0);
        CHECK(r16(REMAP_FLAG) == 0 && r32(REMAP_TABLE) == block && r32(REMAP_CLEARED) == (held ? 0u : 0x5a5a5a5au));
    }
    /* 0x46a4ac alone: without the flag a bare RET (the entry registers). */
    fresh();
    es2_sim_result r;
    CHECK(row(REMAP, &r) == 0 && logged == 0 && r.eax == ENTRY[0] && r.edx == ENTRY[1] && r.ecx == ENTRY[2]);
    fresh();
    w16(REMAP_FLAG, 7u);
    w32(REMAP_TABLE, 0x09004000u);
    CHECK(row(REMAP, &r) == 0 && logged == 1 && log_[0].va == OP_DELETE && log_[0].eax == 0x09004000u && log_[0].edx == ENTRY[1] &&
          log_[0].ecx == ENTRY[2]);
    CHECK(r.eax == 0xd1d1d1d1u && r.edx == 0 && r.ecx == 0x09004000u && r16(REMAP_FLAG) == 0 && r32(REMAP_TABLE) == 0x09004000u);
    return 0;
}

/* ---- 2. the report with 0x461304 in its notify list */
static struct { unsigned boxes, exits; uint32_t status, sounds_at_box; } rec;
static uint32_t rec_box(void *user, uint32_t owner, const char *t, const char *c, uint32_t style) {
    (void)user; (void)owner; (void)t; (void)c; (void)style;
    ++rec.boxes;
    rec.sounds_at_box = logged;
    return 1;
}
static void rec_exit(void *user, uint32_t status) { (void)user; ++rec.exits; rec.status = status; }
static int report_notify(void) {
    es2_sim_process_platform p = {rec_box, rec_exit, NULL, NULL, NULL};
    for (int windowed = 0; windowed < 2; ++windowed) {
        fresh();
        memset(&rec, 0, sizeof(rec));
        uint32_t node = local_guest + 0x30000u;
        w32(node, 0); w32(node + 4u, CLEANUP);
        w32(NOTIFY_HEAD, node);
        w32(TAPE, 0x09001000u);
        w32(CODE, 4u);
        if (windowed) loop.process = &p;
        const uint32_t s[6] = {RECORD, 5u, 0, 0x004a69d4u, 0x15u, 0};
        es2_sim_result r;
        CHECK(es2_sim_loop_call(&loop, REPORT, s, 6, &r) == 1);
        CHECK(log_[0].va == SOUND_CLOSE && log_[1].va == FCLOSE && log_[1].s0 == 0x09001000u);
        CHECK(r32(TAPE) == 0 && r32(CODE) == 0);
        if (!windowed) CHECK(loop.outcome == ES2_SIM_LOOP_REFUSED && first_refused == 0x0049679cu && loop.platform.message_box == 1);
        else CHECK(loop.outcome == ES2_SIM_LOOP_EXITED && loop.exit_code == 1 && rec.boxes == 1 && rec.sounds_at_box >= 2u &&
                   rec.exits == 1 && rec.status == 1);
    }
    return 0;
}

/* ---- 3. a process end under audio.c's call helper */
static int exited_through_helpers(void) {
    es2_sim_process_platform p = {rec_box, rec_exit, NULL, NULL, NULL};
    fresh();
    memset(&rec, 0, sizeof(rec));
    stub_of(SOUND_CLOSE)->real = 1;   /* audio.c's row */
    exit_record = &p;
    w32(SOUND_CONTEXT, 0x09005000u);
    w32(CD_DEVICE, 0xffffffffu);
    w32(TAPE, 0x09001000u);
    es2_sim_result r;
    CHECK(es2_sim_loop_call(&loop, CLEANUP, NULL, 0, &r) == 1);
    CHECK(logged >= 1 && log_[0].va == HANDLER_REMOVE && log_[0].s0 == 0x00462398u);
    for (unsigned i = 1; i < logged && i < 64u; ++i)   /* the report's log calls, nothing of 0x461304 after the process end */
        CHECK(log_[i].va == RT_OPEN || log_[i].va == RT_WRITE || log_[i].va == RT_CLOSE);
    CHECK(rec.boxes == 1 && rec.exits == 1 && rec.status == 1);
    CHECK(loop.outcome == ES2_SIM_LOOP_EXITED && loop.exit_code == 1);
    CHECK(r32(TAPE) == 0x09001000u);
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
    fresh();
    CHECK(setup_dispatch() == 0);
    if (rows() || report_notify() || exited_through_helpers()) return 1;
    puts("PASS: abort cleanup: 0x461304 and 0x46a4ac call, clear and leave the registers as the listing does; the report runs "
         "0x461304 before its box (headless refused at __exit, windowed EXITED 1); a process end under audio.c's call "
         "helper stays EXITED with code 1");
    return 0;
}
