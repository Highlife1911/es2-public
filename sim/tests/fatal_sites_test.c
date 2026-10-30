/* The error report 0x479c78 at its fatal sites (own_process.md, "the error report 0x479c78 at the sites that refused"), on a
 * synthetic image: zeroed host memory, the literals and objects each case needs
 * written by this test, the game heap over an emulated OS region. Every callee
 * a site's path reaches before and after the report is a recording stub of this
 * test (added before the seal where no row exists, the row's service replaced
 * after it); the path builders, the VOL-aware stream's constructor, _itoa and
 * the string rows are the sim's own. Per site: a missing file (an open status,
 * a failed read, an object of another class) calls the report with the
 * original's arguments (record 0x6c56c8, severity 5, the third argument, the
 * source-file string, the line, the text); the report stub returns, and the
 * next call is the one the original's bytes make after the call (that stub
 * stops the case). Then the report's own row on two sites: headless the run
 * stops refused at __exit 0x49679c with no continuation; with the process
 * record (a recorder) the outcome stays ES2_SIM_LOOP_EXITED with code 1 through
 * the load and the HUD call helpers. No game data. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "es2/sim/hud.h"
#include "es2/sim/load.h"
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

#define REPORT 0x00479c78u
#define RECORD 0x006c56c8u
/* The VOL-aware stream's vtable (written by 0x472f14) and this test's
 * methods in it (VAs no row uses). */
#define VT_VOL 0x004a0e10u
#define M_TELL 0x00401020u
#define M_SEEK 0x00401024u
#define M_READ 0x00401028u
#define VOL_OPEN 0x004730e8u
#define DTS_LOAD 0x0047a144u
#define VOL_DESTROY 0x00472f64u
#define STREAM_READ 0x004735b8u
#define R_OPEN 0x0047ad40u
#define R_READ 0x0047af98u
#define PATH_ROOT 0x0045ee00u
#define RT_OPEN 0x004967c6u
#define RT_WRITE 0x0049677eu
#define RT_CLOSE 0x00496874u
#define NEW 0x0047749cu
#define NEW_ARRAY 0x004774f0u
#define LOAD_BY_NAME 0x0045cdf4u
#define DIALOG_TEXTS 0x00435970u
#define PATH_BUFFER 0x006cbc0cu   /* 0x492b90's result */
#define DIR_BUFFER 0x006cbc2au    /* 0x492bec's result */

static uint8_t *image, *heap_bytes, *local;
static uint32_t local_guest;
static es2_sim_arena arena;
static es2_guest_memory memory;
static es2_sim_heap heap;
static es2_sim_services services;
static es2_sim_entry storage[4096];
static es2_sim_dispatch table;
static es2_sim_loop loop;
static es2_sim_load load;
static es2_sim_hud hud;
static es2_vt vt;
static uint32_t first_refused;
static unsigned refusals;
static void on_refuse(const es2_sim_refusal *r, void *user) {
    (void)user;
    if (!refusals) first_refused = r->va;
    ++refusals;
    if (getenv("FATAL_SITES_VERBOSE")) fprintf(stderr, "refused 0x%08x: %s\n", (unsigned)r->va, r->reason ? r->reason : "");
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
static void w32(uint32_t a, uint32_t v) { memory.write32(memory.context, a, v); }
static void w8(uint32_t a, uint8_t v) { es2_sim_write8(&memory, a, v); }
static void text(uint32_t a, const char *t) { do w8(a++, (uint8_t)*t); while (*t++); }

/* ---- the recording stubs */
typedef struct call { uint32_t va, s[6]; } call;
static call log_[256];
static unsigned logged, reports, report_at;
static int forward;                 /* the report's own row (the end-to-end cases) */
static uint32_t open_status[4];     /* per open call (0x4730e8, 0x47ad40) */
static unsigned opens;
static uint32_t read_tag, stream_read_al, root_string, rt_open_answer, load_answer;
typedef struct stub { uint32_t va; uint8_t slots; int row; es2_sim_service_fn orig; void *orig_user; } stub;
static stub stubs[] = {
    {REPORT, 6, 1, NULL, NULL},      {VOL_OPEN, 2, 0, NULL, NULL},   {DTS_LOAD, 2, 0, NULL, NULL},
    {VOL_DESTROY, 2, 0, NULL, NULL}, {STREAM_READ, 3, 0, NULL, NULL}, {M_TELL, 1, 0, NULL, NULL},
    {M_SEEK, 2, 0, NULL, NULL},      {M_READ, 3, 0, NULL, NULL},     {R_OPEN, 2, 1, NULL, NULL},
    {R_READ, 3, 1, NULL, NULL},      {PATH_ROOT, 1, 1, NULL, NULL},  {RT_OPEN, 3, 1, NULL, NULL},
    {RT_WRITE, 3, 1, NULL, NULL},    {RT_CLOSE, 1, 1, NULL, NULL},   {NEW, 1, 1, NULL, NULL},
    {NEW_ARRAY, 1, 1, NULL, NULL},   {LOAD_BY_NAME, 1, 1, NULL, NULL}, {DIALOG_TEXTS, 4, 1, NULL, NULL},
};
#define NSTUBS (sizeof(stubs) / sizeof(stubs[0]))
static void svc_stub(es2_sim_service_call *c, void *u) {
    const stub *st = u;
    if (forward && (st->va == REPORT || st->va == NEW || st->va == NEW_ARRAY)) { st->orig(c, st->orig_user); return; }
    if (logged < 256) {
        log_[logged].va = c->va;
        for (unsigned i = 0; i < 6; ++i) log_[logged].s[i] = i < st->slots ? c->slots[i] : 0;
    }
    ++logged;
    if (c->va == REPORT) { ++reports; report_at = logged - 1u; c->eax = 0; return; }
    if (reports && !forward && c->va != RT_OPEN && c->va != RT_WRITE && c->va != RT_CLOSE) {
        c->refused = 1; c->reason = "the continuation after the report (this test stops here)";
        return;
    }
    switch (c->va) {
    case VOL_OPEN: case R_OPEN:
        w32(c->slots[0] + 4u, opens < 4 ? open_status[opens] : 0);
        ++opens;
        if (c->va == R_OPEN) w32(c->slots[0], VT_VOL);
        c->eax = c->slots[0];
        break;
    case M_READ:
        if (c->slots[1] == 8u) { w32(c->slots[2], read_tag); w32(c->slots[2] + 4u, 0x10u); }
        else for (uint32_t i = 0; i < c->slots[1]; ++i) w8(c->slots[2] + i, 0);
        c->eax = 1;
        break;
    case STREAM_READ: c->eax = stream_read_al; break;
    case PATH_ROOT: c->eax = root_string; break;
    case RT_OPEN: c->eax = rt_open_answer; break;
    case RT_WRITE: c->eax = c->slots[2]; break;
    case LOAD_BY_NAME: c->eax = load_answer; break;
    case DTS_LOAD: c->eax = 0x00777000u; break;
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
    for (unsigned i = 0; i < NSTUBS; ++i)
        if (!stubs[i].row && es2_sim_dispatch_add_service(&table, stubs[i].va, "stub", svc_stub, &stubs[i], 0, stubs[i].slots)) {
            fprintf(stderr, "cannot add 0x%08x\n", (unsigned)stubs[i].va);
            return 1;
        }
    const char *error = NULL;
    uint32_t bad = 0;
    if (es2_sim_dispatch_seal(&table, &error, &bad)) return 1;
    for (unsigned i = 0; i < NSTUBS; ++i) {
        if (!stubs[i].row) continue;
        es2_sim_entry *e = (es2_sim_entry *)es2_sim_dispatch_find(&table, stubs[i].va);
        if (!e || e->kind != ES2_SIM_SERVICE) { fprintf(stderr, "no row 0x%08x\n", (unsigned)stubs[i].va); return 1; }
        stubs[i].orig = e->service;
        stubs[i].orig_user = e->service_user;
        e->service = svc_stub;
        e->service_user = &stubs[i];
    }
    return 0;
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
    es2_sim_load_headless(&load, &loop);
    es2_sim_load_bind(&load);
    es2_sim_hud_init(&hud, &loop);
    es2_sim_hud_bind(&hud);
    /* the VOL stream's methods */
    w32(VT_VOL + 4u, M_TELL);
    w32(VT_VOL + 8u, M_SEEK);
    w32(VT_VOL + 0x18u, M_READ);
    refusals = 0; first_refused = 0;
    logged = reports = report_at = 0;
    forward = 0;
    memset(open_status, 0, sizeof(open_status));
    opens = 0;
    read_tag = 0; stream_read_al = 1; root_string = 0; rt_open_answer = 5; load_answer = 0x00778000u;
}
static int row(uint32_t va, const uint32_t *slots, unsigned n) {
    uint32_t regs[3] = {0, 0, 0};
    uint32_t at = local_guest + 0x20000u;
    for (unsigned i = 0; i < n; ++i) w32(at + 4u * i, slots[i]);
    es2_sim_result r;
    memset(&r, 0, sizeof(r));
    return es2_sim_call(&table, va, (const uint32_t *)(uintptr_t)at, n, regs, &r);
}
/* The report was called once with (0x6c56c8, 5, a, file, line, text) and
 * the next call is `next` (its first slot `s0` unless `s0` is ~0). */
static int expect(uint32_t a, uint32_t file, uint32_t line, uint32_t txt, uint32_t next, uint32_t s0) {
    if (reports != 1 || report_at + 1u >= logged || logged > 256) {
        fprintf(stderr, "reports %u at %u of %u\n", reports, report_at, logged);
        return 1;
    }
    const call *k = &log_[report_at], *n = &log_[report_at + 1u];
    if (k->s[0] != RECORD || k->s[1] != 5u || k->s[2] != a || k->s[3] != file || k->s[4] != line || k->s[5] != txt) {
        fprintf(stderr, "report (0x%x, %u, 0x%x, 0x%x, 0x%x, 0x%x)\n", (unsigned)k->s[0], (unsigned)k->s[1], (unsigned)k->s[2],
                (unsigned)k->s[3], (unsigned)k->s[4], (unsigned)k->s[5]);
        return 1;
    }
    if (n->va != next || (s0 != ~0u && n->s[0] != s0)) {
        fprintf(stderr, "after the report: 0x%08x (0x%x)\n", (unsigned)n->va, (unsigned)n->s[0]);
        return 1;
    }
    return 0;
}
static uint32_t count_calls(uint32_t va) {
    uint32_t n = 0;
    for (unsigned i = 0; i < logged && i < 256; ++i) n += log_[i].va == va;
    return n;
}
static uint32_t path_of(uint32_t name, const char *t) { text(name, t); return name; }

/* ---- load.c: the launch peek 0x4614c0 (0x461524, 0x461587) */
static int launch_peek(void) {
    uint32_t world = local_guest + 0x30000u, flyer = world + 4u, root = world + 0x10u, ext = world + 0x40u;
    fresh();
    text(root, "data\\script");
    text(ext, "dat");
    w32(0x004a09f0u, ext);
    root_string = root;
    open_status[0] = 1;   /* data\script.dat did not open */
    CHECK(row(0x004614c0u, (const uint32_t[]){world, flyer}, 2) != 0);
    CHECK(expect(0, 0x0049f86du, 0xdeu, PATH_BUFFER, R_READ, ~0u) == 0);
    CHECK(log_[report_at + 1u].s[1] == 0x14u);   /* R.read(0x14, f-44h) */
    fresh();
    text(root, "data\\script");
    text(ext, "dat");
    w32(0x004a09f0u, ext);
    root_string = root;
    rt_open_answer = 0xffffffffu;   /* data\player.mec: _open fails, status 1 */
    CHECK(row(0x004614c0u, (const uint32_t[]){world, flyer}, 2) != 0);
    CHECK(count_calls(RT_OPEN) == 1 && count_calls(R_READ) == 1);
    CHECK(expect(0, 0x0049f886u, 0xe4u, 0x0049f88fu, M_READ, ~0u) == 0);   /* the first of the five reads, slot +0x18 */
    CHECK(log_[report_at + 1u].s[1] == 2u);
    return 0;
}
/* ---- load.c: 0x474cd8 (0x474d3e) and 0x474de4 (0x474e3c): an object of
 * another class */
static int shape_walks(void) {
    uint32_t out = local_guest + 0x30000u, path = path_of(out + 0x10u, "x.dts");
    fresh();
    read_tag = 0x00140001u;
    CHECK(row(0x00474cd8u, (const uint32_t[]){out, path, 0x00140008u, 0}, 4) != 0);
    CHECK(expect(0, 0x004a0f38u, 0x7eu, path, DTS_LOAD, ~0u) == 0);   /* skip 0: the object loaded */
    fresh();
    path = path_of(out + 0x10u, "x.dts");
    read_tag = 0x00140001u;
    CHECK(row(0x00474de4u, (const uint32_t[]){path, 1u, 0x00140008u}, 3) != 0);
    CHECK(expect(0, 0x004a0f42u, 0x9eu, path, M_READ, ~0u) == 0 && log_[report_at + 1u].s[1] == 8u);   /* the object skipped */
    return 0;
}
/* ---- load.c: the table reader 0x492f54 (0x492fab, 0x492feb) */
static int table_reader(void) {
    uint32_t dst = local_guest + 0x30000u, path = path_of(dst + 0x100u, "colors.dat");
    fresh();
    path = path_of(dst + 0x100u, "colors.dat");
    open_status[0] = 1;
    CHECK(row(0x00492f54u, (const uint32_t[]){dst, 0x36u, 1u, path}, 4) != 0);
    CHECK(expect(0, 0x004a69d4u, 0x15u, path, STREAM_READ, ~0u) == 0);
    CHECK(log_[report_at + 1u].s[1] == 0x36u && log_[report_at + 1u].s[2] == dst);
    fresh();
    path = path_of(dst + 0x100u, "colors.dat");
    stream_read_al = 0;   /* AL 0: the read failed */
    CHECK(row(0x00492f54u, (const uint32_t[]){dst, 0x36u, 1u, path}, 4) != 0);
    CHECK(expect(0, 0x004a69dcu, 0x17u, path, VOL_DESTROY, ~0u) == 0 && log_[report_at + 1u].s[1] == 2u);
    return 0;
}
/* ---- load.c: the damage display 0x4379cc (0x437a62) and the weapon
 * shapes 0x40f998 (0x40fa18) */
static int damage_and_weapons(void) {
    uint32_t name = local_guest + 0x30000u;
    fresh();
    text(name, "dmg_x");
    open_status[0] = 1;
    CHECK(row(0x004379ccu, (const uint32_t[]){name, 0}, 2) != 0);
    CHECK(count_calls(LOAD_BY_NAME) == 1);
    CHECK(expect(0, 0x0049bc54u, 0x48u, name, M_READ, ~0u) == 0 && log_[report_at + 1u].s[1] == 4u);
    fresh();
    text(0x004991e0u, "mechwpns");
    read_tag = 0x00140001u;   /* not 0x140008 */
    CHECK(row(0x0040f998u, NULL, 0) != 0);
    CHECK(expect(0, 0x004991e9u, 0x90u, DIR_BUFFER, NEW_ARRAY, 0x10u + 9u - 1u) == 0);   /* new[]((0x10 + 9) & -2) */
    return 0;
}
/* ---- hud_cockpit.c: the view shape 0x429c2c (0x429d14, 0x429d97) and the
 * panel layout through 0x431bf8 (0x4317c6) */
static int cockpit(void) {
    uint32_t d = local_guest + 0x30000u, shapes = d + 0x100u, ext = d + 0x200u;
    fresh();
    text(d + 0x2du, "cock");
    text(ext, "x0");
    w32(0x004a0a10u, ext);
    w32(d + 8u, shapes);
    open_status[0] = 1;
    CHECK(row(0x00429c2cu, (const uint32_t[]){d, 0}, 2) != 0);
    CHECK(count_calls(DTS_LOAD) == 1 && expect(0, 0x0049ae7au, 0x12eu, DIR_BUFFER, DTS_LOAD, ~0u) == 0);
    fresh();
    text(d + 0x2du, "cock");
    text(ext, "x0");
    w32(0x004a0a10u, ext);
    w32(d + 8u, shapes);
    w8(0x004d2538u, 1);
    open_status[1] = 1;   /* the second open (0x4730e8 directly) */
    CHECK(row(0x00429c2cu, (const uint32_t[]){d, 0}, 2) != 0);
    CHECK(opens == 2 && r32(shapes) == 0x00777000u && expect(0, 0x0049ae87u, 0x135u, DIR_BUFFER, DTS_LOAD, ~0u) == 0);
    fresh();
    uint32_t h = d, name = d + 0x300u;
    text(name, "panel");
    open_status[0] = 1;
    CHECK(row(0x00431bf8u, (const uint32_t[]){h, name}, 2) != 0);
    CHECK(expect(0, 0x0049b754u, 0x1d6u, DIR_BUFFER, NEW, 0x6a4u) == 0);
    return 0;
}
/* ---- hud_gauges.c: the radio record 0x44a7e8 (0x44a905, 0x44a992) */
static int radio(void) {
    uint32_t h = local_guest + 0x30000u;
    fresh();
    open_status[0] = 1;   /* pilot<x>.ofs */
    CHECK(row(0x0044a7e8u, (const uint32_t[]){h, 1u, 0}, 3) != 0);
    CHECK(count_calls(DIALOG_TEXTS) == 1 && expect(0, 0x0049d52du, 0x3eeu, 0x0049d536u, M_READ, ~0u) == 0);
    CHECK(log_[report_at + 1u].s[1] == 4u);
    fresh();
    load_answer = 0;   /* the portrait is missing */
    CHECK(row(0x0044a7e8u, (const uint32_t[]){h, 1u, 0}, 3) != 0);
    CHECK(count_calls(M_READ) == 0x1bu && count_calls(STREAM_READ) == 0x1bu);
    uint32_t r = h + 0x12du + 0x14eu;
    CHECK(r32(r + 0x18u) == 0 && (r32(r + 0x130u) >> 24) == 1u);   /* byte R+0x133 set before the report */
    CHECK(expect(0, 0x0049d55au, 0x406u, 0x0049d563u, NEW, 0x21u) == 0);   /* the first label */
    return 0;
}

/* ---- the report's own row on two converted sites */
static struct { unsigned boxes, exits; uint32_t status; } rec;
static uint32_t rec_box(void *user, uint32_t owner, const char *t, const char *c, uint32_t style) {
    (void)user; (void)owner; (void)t; (void)c; (void)style;
    ++rec.boxes;
    return 1;
}
static void rec_exit(void *user, uint32_t status) { (void)user; ++rec.exits; rec.status = status; }
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
    text(0x0049b754u, "g.cpp");
}
static int end_to_end(void) {
    uint32_t dst = local_guest + 0x30000u, path = dst + 0x100u, name = dst + 0x300u;
    es2_sim_result r;
    es2_sim_process_platform p = {rec_box, rec_exit, NULL, NULL, NULL};
    for (int windowed = 0; windowed < 2; ++windowed) {
        /* the table reader: the missing table */
        fresh(); report_literals(); forward = 1; memset(&rec, 0, sizeof(rec));
        text(path, "colors.dat");
        open_status[0] = 1;
        if (windowed) loop.process = &p;
        const uint32_t ts[4] = {dst, 0x36u, 1u, path};
        CHECK(es2_sim_loop_call(&loop, 0x00492f54u, ts, 4, &r) == 1 && count_calls(STREAM_READ) == 0);
        if (!windowed) CHECK(loop.outcome == ES2_SIM_LOOP_REFUSED && first_refused == 0x0049679cu && loop.platform.message_box == 1);
        else CHECK(loop.outcome == ES2_SIM_LOOP_EXITED && loop.exit_code == 1 && rec.boxes == 1 && rec.exits == 1 && rec.status == 1);
        /* the panel layout (the HUD's helper) */
        fresh(); report_literals(); forward = 1; memset(&rec, 0, sizeof(rec));
        text(name, "panel");
        open_status[0] = 1;
        if (windowed) loop.process = &p;
        const uint32_t ps[2] = {dst, name};
        CHECK(es2_sim_loop_call(&loop, 0x00431bf8u, ps, 2, &r) == 1 && count_calls(NEW) == 0);
        if (!windowed) CHECK(loop.outcome == ES2_SIM_LOOP_REFUSED && first_refused == 0x0049679cu && loop.platform.message_box == 1);
        else CHECK(loop.outcome == ES2_SIM_LOOP_EXITED && loop.exit_code == 1 && rec.boxes == 1 && rec.exits == 1 && rec.status == 1);
    }
    fresh();
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
    if (launch_peek() || shape_walks() || table_reader() || damage_and_weapons() || cockpit() || radio() || end_to_end()) return 1;
    puts("PASS: fatal sites: the 13 converted sites call the report 0x479c78 with the original's arguments and go on as the "
         "original's bytes do; the report's row ends headless at __exit and keeps EXITED (code 1) through the load and HUD helpers");
    return 0;
}
