/* The start-up (sim/src/startup.c, volumes.c) and the file services
 * (src/services_files.c) on a synthetic image and synthetic installations in
 * the test's temporary directory. The image range is zeroed host memory with
 * the program literals the start-up names written by this test; the frame area
 * and CW3220's tables lie in a range mapped at its own host address (so
 * argument slots are guest-addressable); the game heap and CW3220's heap run
 * over emulated OS regions. The sim's real services answer the heap, string and
 * file calls; the hooks the start-up calls (prefs_video, stage_dispatch, the
 * VOL-aware stream, the tape functions, the mission load) are recording
 * services. No game data. The comparison with the original's machine code is
 * //tools/reference-game/invitro:differential_startup_test. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "es2/sim/files.h"
#include "es2/sim/memory.h"
#include "es2/sim/services.h"
#include "es2/sim/startup.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

#define IMAGE_BASE 0x00400000u
#define IMAGE_SIZE 0x002d8000u
#define HEAP_BASE 0x09000000u
#define HEAP_SIZE 0x00400000u
/* assert.log's separator: 0x50 bytes as the original writes them (78 dashes, CR LF). */
#define SEPARATOR "------------------------------------------------------------------------------\r\n"
#define LOCAL_SIZE 0x00010000u          /* frame area 0x0000..0x8000, CW3220 tables 0x8000, the command line 0xa000 */

static uint8_t *image, *heap_bytes, *local;
static uint32_t local_guest;
static es2_sim_arena arena;
static es2_guest_memory memory;
static es2_sim_heap heap;
static es2_sim_services services;
static es2_sim_entry storage[4096];
static es2_sim_dispatch table;
static es2_sim_files files;
static es2_sim_refusal last;
static unsigned refusals;
static void on_refuse(const es2_sim_refusal *r, void *user) { (void)user; last = *r; ++refusals; }

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
static void w8(uint32_t a, uint8_t v) { es2_sim_write8(&memory, a, v); }
static void text(uint32_t a, const char *t) { do w8(a++, (uint8_t)*t); while (*t++); }
static int same(uint32_t a, const char *t) {
    for (;; ++a, ++t) { if (r8(a) != (uint8_t)*t) return 0; if (!*t) return 1; }
}

/* ---- recording services for the hooks and original functions the start-up calls */
static char calls[512];
static unsigned call_count;
static uint32_t prefs_arg[8], stage_arg[8], tape_paths[16], tape_files[16];
static unsigned prefs_count, stage_count, tape_count, load_count, dib_count;
static uint32_t stream_size, stream_pos;
static const uint8_t *stream_text;
static char stream_name[64];
static void note(char c) { if (call_count + 1 < sizeof(calls)) calls[call_count++] = c; calls[call_count] = 0; }
static void svc_prefs(es2_sim_service_call *c, void *u) { (void)u; note('v'); if (prefs_count < 8) prefs_arg[prefs_count] = c->slots[0]; ++prefs_count; }
static void svc_stage(es2_sim_service_call *c, void *u) { (void)u; note('s'); if (stage_count < 8) stage_arg[stage_count] = c->slots[0]; ++stage_count; }
static void svc_stream_open(es2_sim_service_call *c, void *u) {
    (void)u; note('o');
    unsigned i = 0;
    for (uint32_t p = c->slots[1]; i + 1 < sizeof(stream_name) && r8(p + i); ++i) stream_name[i] = (char)r8(p + i);
    stream_name[i] = 0;
    stream_pos = 0;
    w32(c->slots[0] + 4u, stream_text ? 0u : 1u);
    c->eax = stream_text ? 1u : 0u;
}
static void svc_stream_read(es2_sim_service_call *c, void *u) {
    (void)u; note('r');
    uint32_t s = c->slots[0], n = c->slots[1], buf = c->slots[2];
    if (r32(s + 4u) || stream_pos + n > stream_size) { w32(s + 4u, 2); c->eax = 0; return; }
    for (uint32_t i = 0; i < n; ++i) w8(buf + i, stream_text[stream_pos + i]);
    stream_pos += n;
    c->eax = 1;
}
static void svc_stream_destroy(es2_sim_service_call *c, void *u) { (void)u; note('d'); c->eax = 0; }
static void svc_tape(es2_sim_service_call *c, void *u) {
    (void)u; note(c->va == 0x0045cca4u ? 'T' : 'R');
    if (tape_count < 16) { tape_files[tape_count] = c->slots[0]; tape_paths[tape_count] = 0; }
    /* keep the path text: the static buffer is reused by the next call */
    static char kept[16][64];
    unsigned i = 0;
    for (; i + 1 < 64 && r8(c->slots[1] + i); ++i) kept[tape_count % 16][i] = (char)r8(c->slots[1] + i);
    kept[tape_count % 16][i] = 0;
    if (tape_count < 16) tape_paths[tape_count] = (uint32_t)(uintptr_t)kept[tape_count];
    ++tape_count;
}
static void svc_load(es2_sim_service_call *c, void *u) { (void)u; (void)c; note('L'); ++load_count; }
static void svc_dib(es2_sim_service_call *c, void *u) { (void)u; (void)c; note('D'); ++dib_count; }

/* ---- the synthetic image: the literals the start-up reads (program text of
 * this test, at the original's addresses) */
static void image_literals(void) {
    memset(image, 0, IMAGE_SIZE);
    static const struct { uint32_t va; const char *t; } lit[] = {
        {0x004a0876u, "dbsim"}, {0x0049f442u, "data\\drive.cfg"}, {0x0049f451u, "rt"}, {0x0049f454u, "MAIN.CPP"},
        {0x0049f45du, "%s"}, {0x0049f460u, "\\"}, {0x0049f462u, ""}, {0x0049f463u, "%s"}, {0x0049f466u, ""},
        {0x0049f467u, ".vol"}, {0x0049f46cu, "error.str"}, {0x0049f299u, "SPRUNKNOWN"}, {0x0049f3ddu, "eggplant"},
        {0x0049f3e6u, "EGGPLANT"}, {0x0049f35bu, "tap"}, {0x0049f35fu, "wb"}, {0x0049f362u, "data\\script.dat"},
        {0x0049f372u, "data\\player.mec"}, {0x0049f382u, "data\\mission.var"}, {0x0049f393u, "data\\prefs.cfg"},
        {0x0049f3a2u, "data\\restore.dat"}, {0x0049f3b3u, "data\\object.str"}, {0x0049f3c3u, "data\\keyjoy.cfg"},
        {0x0049f2abu, "tap"}, {0x0049f2afu, "rb"}, {0x0049f2b2u, "prefs.cfg"}, {0x0049f2bcu, "data\\script.dat"},
        {0x0049f2ccu, "data\\player.mec"}, {0x0049f2dcu, "data\\mission.var"}, {0x0049f2edu, "prefs.cfg"},
        {0x0049f2f7u, "data\\restore.dat"}, {0x0049f308u, "data\\object.str"}, {0x0049f318u, "keyjoy.cfg"},
        {0x0049f323u, "data\\prefs.cfg"}, {0x0049f332u, "rb"}, {0x0049f335u, "tapes\\prefs.cfg"}, {0x0049f345u, "rb"},
        {0x0049f348u, "tapes\\prefs.cfg"}, {0x0049f358u, "wb"}, {0x0049f3d3u, "wb"}, {0x0049f3d6u, "rb"}, {0x0049f3d9u, "dmp"},
        {0x004a6939u, "."}, {0x0049f3efu, "tapes\\"}, {0x0049f3f6u, "data\\"},
        {0x004a159cu, "Internal error:\n"}, {0x004a1662u, "\n"}, {0x004a1635u, "A::%s: L %d in \"%s\""}, {0x004a1651u, ": %s"},
        {0x004a1664u, "Error"}, {0x004a1540u, "assert.log"}, {0x004a154bu, SEPARATOR}, {0x004a1614u, "\r\n"},
        {0x0049f4a3u, "MAIN.CPP"}, {0x0049f4acu, "need memory"}, {0x00490000u, "fatal"},
        {0x004a0890u, ""}, {0x004a0891u, "vol\\"}, {0x004a0896u, "*"}, {0x004a0898u, "vol\\"}, {0x004a089du, "vol\\"},
        {0x004a08a2u, "*"}, {0x004a08a4u, "vol\\"}, {0x00490010u, "VOL"},
    };
    for (unsigned i = 0; i < sizeof(lit) / sizeof(lit[0]); ++i) text(lit[i].va, lit[i].t);
    for (unsigned s = 0; s < 7u; ++s) w32(0x004a15f4u + 4u * s, 0x00490000u);   /* severity names: "fatal" */
    w32(0x004a1610u, 1);                       /* the separator flag of assert.log */
    w32(0x004a1c2cu, 0x00490010u);             /* -> the VOL signature literal */
    w32(0x004a0e10u + 0x18u, 0x004735b8u);     /* the VOL-aware stream's read slot */
    w32(0x006cbee4u, IMAGE_BASE + 0x2d7ff0u);  /* the construction counter's word */
}

/* ---- the synthetic installation (the working directory) */
static char root_dir[MAX_PATH], here[MAX_PATH];
static void put(const char *name, const void *p, size_t n) {
    FILE *f = fopen(name, "wb");
    if (f) { fwrite(p, 1, n, f); fclose(f); }
}
static size_t get(const char *name, uint8_t *out, size_t capacity) {
    FILE *f = fopen(name, "rb");
    if (!f) return (size_t)-1;
    size_t n = fread(out, 1, capacity, f);
    fclose(f);
    return n;
}
static int install(const char *name, const char *drive) {
    char dir[MAX_PATH];
    snprintf(dir, sizeof(dir), "%s\\%s", root_dir, name);
    CreateDirectoryA(dir, NULL);
    if (!SetCurrentDirectoryA(dir)) return 1;
    CreateDirectoryA("data", NULL);
    CreateDirectoryA("tapes", NULL);
    CreateDirectoryA("vol", NULL);
    if (drive) put("data\\drive.cfg", drive, strlen(drive));
    return 0;
}

/* error.str's content as the stream serves it: a size dword, then group 0
 * (one string) and the two groups the game main reads (5 and 1 strings). */
static uint8_t strings[256];
static void make_strings(void) {
    static const char *const g1[] = {"a", "b", "c", "d", "eggplant text"}, *const g2[] = {"caption"};
    uint32_t n = 4;
    struct { const char *const *s; unsigned count; } groups[2] = {{g1, 5}, {g2, 1}};
    for (unsigned g = 0; g < 2; ++g) {
        strings[n++] = (uint8_t)groups[g].count; strings[n++] = 0;
        for (unsigned i = 0; i < groups[g].count; ++i) {
            size_t length = strlen(groups[g].s[i]) + 1;
            strings[n++] = (uint8_t)length; strings[n++] = 0;
            memcpy(strings + n, groups[g].s[i], length); n += (uint32_t)length;
            strings[n++] = 0;
        }
    }
    uint32_t body = n - 4u;
    memcpy(strings, &body, 4);
    stream_text = strings;
    stream_size = n;
}

static int setup_dispatch(void) {
    es2_sim_dispatch_init(&table, storage, 4096, on_refuse, NULL);
    services.memory = &memory;
    services.heap = &heap;
    services.vt = NULL;
    services.fs0 = NULL;
    if (es2_sim_register_services(&table, &services)) return 1;
    struct { uint32_t va; es2_sim_service_fn f; uint8_t slots; } extra[] = {
        {0x0045e510u, svc_prefs, 1}, {0x00401d94u, svc_stage, 1}, {0x004730e8u, svc_stream_open, 2},
        {0x004735b8u, svc_stream_read, 3}, {0x00472f64u, svc_stream_destroy, 2}, {0x0045cca4u, svc_tape, 2},
        {0x0045cd5cu, svc_tape, 2}, {0x00461608u, svc_load, 0}, {0x00464d44u, svc_dib, 1},
    };
    for (unsigned i = 0; i < sizeof(extra) / sizeof(extra[0]); ++i)
        if (es2_sim_dispatch_add_service(&table, extra[i].va, "test", extra[i].f, NULL, 0, extra[i].slots)) return 1;
    const char *error = NULL;
    uint32_t bad = 0;
    return es2_sim_dispatch_seal(&table, &error, &bad);
}

/* A fresh guest for one run: image literals, heaps, CW3220's tables, the
 * start-up context with `line` as DBSIM's command line. */
static es2_sim_startup start;
/* The process platform's recorder. */
static struct {
    unsigned boxes, exits, post_quit_at_exit;
    uint32_t owner[4], style[4], status;
    char text[4][0x200], caption[4][64];
} rec;
static uint32_t rec_box(void *user, uint32_t owner, const char *t, const char *c, uint32_t style) {
    (void)user;
    if (rec.boxes < 4) {
        rec.owner[rec.boxes] = owner; rec.style[rec.boxes] = style;
        snprintf(rec.text[rec.boxes], sizeof(rec.text[0]), "%s", t);
        snprintf(rec.caption[rec.boxes], sizeof(rec.caption[0]), "%s", c);
    }
    ++rec.boxes;
    return 1;
}
static void rec_exit(void *user, uint32_t status) {
    (void)user;
    ++rec.exits;
    rec.status = status;
    rec.post_quit_at_exit = start.platform.post_quit;
}
static void events(void *u, const char *kind, const char *t) { (void)u; (void)kind; (void)t; }
static int fresh(const char *line) {
    image_literals();
    memset(heap_bytes, 0, HEAP_SIZE);
    memset(local, 0, LOCAL_SIZE);
    next_heap = HEAP_BASE;
    es2_sim_heap_os os = {NULL, heap_alloc, os_free};
    es2_sim_heap_init(&heap, &memory, &os, local_guest + 0xf000u, 0, 0, 0);
    if (es2_sim_files_init(&files, &memory, local_guest + 0x8000u, &heap)) return 1;
    es2_sim_files_bind(&files);
    /* GetCommandLineA's form: the program name first (the tail skips it). */
    char whole[600];
    snprintf(whole, sizeof(whole), "\"dbsim.EXE\" %s", line);
    text(local_guest + 0xa000u, whole);
    memset(&start, 0, sizeof(start));
    es2_sim_startup_defaults(&start);
    start.memory = &memory;
    start.dispatch = &table;
    start.frame_lo = local_guest;
    start.frame_hi = local_guest + 0x8000u;
    start.command_line = local_guest + 0xa000u;
    start.event = events;
    call_count = 0; calls[0] = 0;
    prefs_count = stage_count = tape_count = load_count = dib_count = refusals = 0;
    make_strings();
    return 0;
}

/* A synthetic archive (vol.md layout) with `members` one-byte members. */
static void volume(const char *path, uint32_t mask, uint8_t priority, unsigned members) {
    uint8_t v[512];
    size_t n = 0;
    memcpy(v, "VOLN", 4); n = 4;
    memcpy(v + n, &mask, 4); n += 4;
    v[n++] = priority; v[n++] = 1;
    v[n++] = 3; v[n++] = 0;
    memcpy(v + n, "d\\", 3); n += 3;
    v[n++] = (uint8_t)members; v[n++] = 0;
    uint32_t bytes = 18u * members;
    memcpy(v + n, &bytes, 4); n += 4;
    size_t records = n;
    n += bytes;
    for (unsigned m = 0; m < members; ++m) {
        uint8_t *r = v + records + 18u * m;
        memset(r, 0, 18);
        r[0] = (uint8_t)('A' + m);
        uint32_t at = (uint32_t)n;
        memcpy(r + 14, &at, 4);
        v[n++] = 2; uint32_t one = 1; memcpy(v + n, &one, 4); n += 4; memset(v + n, 0, 4); n += 4; v[n++] = 'x'; v[n++] = 'x';
    }
    put(path, v, n);
}

int main(void) {
    const char *tmp = getenv("TEST_TMPDIR");
    GetCurrentDirectoryA(MAX_PATH, here);
    snprintf(root_dir, sizeof(root_dir), "%s\\startup", tmp && *tmp ? tmp : ".");
    CreateDirectoryA(root_dir, NULL);
    image = calloc(1, IMAGE_SIZE); heap_bytes = calloc(1, HEAP_SIZE);
    for (uint32_t a = 0x30000000u; !local && a < 0x38000000u; a += 0x01000000u)
        local = VirtualAlloc((void *)(uintptr_t)a, LOCAL_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    CHECK(image && heap_bytes && local);
    local_guest = (uint32_t)(uintptr_t)local;
    CHECK(es2_sim_arena_add(&arena, IMAGE_BASE, image, IMAGE_SIZE));
    CHECK(es2_sim_arena_add(&arena, HEAP_BASE, heap_bytes, HEAP_SIZE));
    CHECK(es2_sim_arena_add(&arena, local_guest, local, LOCAL_SIZE));   /* identity: slots are guest-addressable */
    es2_sim_arena_memory(&arena, &memory);
    CHECK(fresh("x") == 0);
    CHECK(setup_dispatch() == 0);

    /* 1. The launcher's line in an installation with drive.cfg, error.str
     * (served by the stream), two archives (one of another mask): the run
     * stops with the deferred DIB section before the mission load. */
    CHECK(install("full", "C:\\CD\r\nC:\\INSTALL\r\n") == 0);
    volume("vol\\a.vol", 1, 5, 2);
    volume("vol\\b.vol", 0x100, 5, 1);
    CHECK(fresh("dummy -eggplant -X2 -R7 -G -SPRUNKNOWN -s") == 0);
    CHECK(es2_sim_startup_run(&start) == ES2_SIM_STARTUP_REFUSED);
    CHECK(refusals == 1 && last.va == 0x00464d44u && start.refused_va == 0x00464d44u);
    CHECK(start.h_instance == 0x00400000u && r32(0x004d36c4u) == 0x00400000u);
    CHECK(same(start.cmd_line, "dummy -eggplant -X2 -R7 -G -SPRUNKNOWN -s"));
    CHECK(start.argc == 8u);
    /* WinMain: prefs_video(0) first, no -v; the window's stores. */
    CHECK(prefs_count == 1 && prefs_arg[0] == 0);
    CHECK(r32(0x004d3610u) == start.platform.window && r32(0x004d3720u) == start.platform.cursor_wait);
    CHECK(r32(0x004d36f4u) == 640u && r32(0x004d36f8u) == 480u);
    CHECK(r32(0x004d304cu) == 0x28u && r16(0x004d3058u) == 1u && r16(0x004d305au) == 8u);
    /* The game main: roots, exit code (2 from -X, reset to 0), parser stores. */
    CHECK(same(0x004d280cu, "C:\\CD\\") && same(0x004d27bcu, "") && same(0x006c56dcu, ""));
    CHECK(r32(0x004d27b8u) == 0);
    CHECK(r32(0x004d2573u) == 7u && r8(0x004d2536u) == 'g' && r32(0x0049ef38u) == 1u && r8(0x004d24c8u) == 1u);
    CHECK(r16(0x004d2524u) == 1u);
    CHECK(stage_count == 2 && stage_arg[0] == 0 && stage_arg[1] == 1);
    CHECK(r32(0x004a088cu) == 0x400u);            /* 0x467c34(0x200), then the manager's 0x400 */
    CHECK(start.archives == 1u && r32(0x004a0d18u) != 0 && r8(r32(0x004a0d18u) + 0x78u) == 1u);
    uint32_t archive = r32(r32(0x004a0d18u));
    CHECK(same(archive + 8u, "vol\\a.vol") && r8(archive + 0x1cu) == 5u && r8(archive + 0x1du) == 1u && r16(archive + 0x1eu) == 2u);
    CHECK(r32(archive + 0x20u) == 1u && same(r32(r32(archive + 0x24u)), "d\\"));
    CHECK(strcmp(stream_name, "error.str") == 0 && strcmp(calls, "vssorrd") == 0);
    CHECK(r16(0x004d2680u) == 0 && load_count == 0 && dib_count == 0);
    /* The counter: holder + 1; the stream's two increments (the fake
     * destructor does not decrement); archives balance (rejected: the image's
     * type record is 0 here, so -2 per rejected read stream). */
    CHECK((int32_t)r32(IMAGE_BASE + 0x2d7ff0u) == 1 + 2 - 2);

    /* 2. With the DIB section built (a service), the load is called: LOADED. */
    CHECK(fresh("-eggplant") == 0);
    start.defer_dib = 0;
    CHECK(es2_sim_startup_run(&start) == ES2_SIM_STARTUP_LOADED && dib_count == 1 && load_count == 1 && refusals == 0);

    /* 3. Without -eggplant: the message box (group 1 entry 4, group 2 entry
     * 0), the holder's buffer deleted, WinMain returns the exit code 0. */
    CHECK(fresh("-X5") == 0);
    CHECK(es2_sim_startup_run(&start) == ES2_SIM_STARTUP_RETURNED && start.exit_code == 0);
    CHECK(strcmp(start.platform.message_text, "eggplant text") == 0 && strcmp(start.platform.message_caption, "caption") == 0);
    CHECK(start.platform.post_quit == 1 && start.platform.clip_cursor == 1 && start.archives == 1u);

    /* 4. Tape record: -r writes name.tap through tape_save (7 paths, root
     * prefixed), closes it, sets the record word; -d after it creates the
     * .dmp and closes it again. */
    CHECK(fresh("-eggplant -rrun1 -d") == 0);
    CHECK(es2_sim_startup_run(&start) == ES2_SIM_STARTUP_REFUSED);
    CHECK(tape_count == 7 && strcmp((const char *)(uintptr_t)tape_paths[0], "data\\script.dat") == 0 &&
          strcmp((const char *)(uintptr_t)tape_paths[6], "data\\keyjoy.cfg") == 0 && tape_files[0] != 0);
    CHECK(r16(0x004d24d8u) == 1u && r32(0x004d24deu) != 0);
    uint8_t buf[256];
    CHECK(get("run1.tap", buf, sizeof(buf)) == 0 && get("run1.dmp", buf, sizeof(buf)) == 0);

    /* 5. Playback: -p restores through tape_restore (prefs and keyjoy under
     * tapes\), then copies bytes 0..3 and 8..10 of data\prefs.cfg into
     * tapes\prefs.cfg. */
    uint8_t original[0x36], tape_prefs[0x36];
    for (unsigned i = 0; i < 0x36u; ++i) { original[i] = (uint8_t)(0x10 + i); tape_prefs[i] = (uint8_t)(0xa0 + i); }
    put("data\\prefs.cfg", original, sizeof(original));
    put("tapes\\prefs.cfg", tape_prefs, sizeof(tape_prefs));
    CHECK(fresh("-eggplant -prun1") == 0);
    CHECK(es2_sim_startup_run(&start) == ES2_SIM_STARTUP_REFUSED && last.va == 0x00464d44u);
    CHECK(tape_count == 7 && strcmp((const char *)(uintptr_t)tape_paths[3], "tapes\\prefs.cfg") == 0 &&
          strcmp((const char *)(uintptr_t)tape_paths[6], "tapes\\keyjoy.cfg") == 0);
    CHECK(same(0x0049e81cu, "tapes\\prefs.cfg") && r16(0x004d24d6u) == 1u);
    CHECK(get("tapes\\prefs.cfg", buf, sizeof(buf)) == 0x36u);
    for (unsigned i = 0; i < 0x36u; ++i) CHECK(buf[i] == ((i < 4u || (i >= 8u && i < 11u)) ? original[i] : tape_prefs[i]));

    /* 6. Low memory: below 12 MB the arena is set up (0x476df4, runtime_leaves.h's
     * row) and the start-up goes on to the DIB section 0x464d44 (refused:
     * not own code); below 7.6 MB the fatal report. */
    CHECK(fresh("-eggplant") == 0);
    start.platform.total_phys = 0x00b00000u;
    CHECK(es2_sim_startup_run(&start) == ES2_SIM_STARTUP_REFUSED && last.va == 0x00464d44u && r16(0x004d2680u) == 1u);
    CHECK(fresh("-eggplant -l") == 0);
    CHECK(es2_sim_startup_run(&start) == ES2_SIM_STARTUP_REFUSED && last.va == 0x00464d44u);
    DeleteFileA("assert.log");
    CHECK(fresh("-eggplant") == 0);
    start.platform.total_phys = 0x00700000u;
    CHECK(es2_sim_startup_run(&start) == ES2_SIM_STARTUP_EXIT && start.exit_code == 1);
    CHECK(strstr(start.platform.message_text, "A::fatal: L 657 in \"MAIN.CPP\": need memory") != NULL);
    size_t n = get("assert.log", buf, sizeof(buf) - 1);
    CHECK(n != (size_t)-1);
    buf[n] = 0;
    /* Text mode (_fmode): the model's _write turns each LF into CR LF. */
    CHECK(memcmp(buf, "------", 6) == 0 && strstr((const char *)buf, "A::fatal: L 657 in \"MAIN.CPP\": need memory\r") != NULL);

    /* 7. DirectDraw requested (-Z1): the switch 0x4667c8 is called (refused
     * here: no main loop is bound). */
    CHECK(fresh("-eggplant -Z1") == 0);
    CHECK(es2_sim_startup_run(&start) == ES2_SIM_STARTUP_REFUSED && last.va == 0x004667c8u && r32(0x004d36fcu) == 1u);

    /* 8. More than 26 arguments: refused in the split. */
    char many[256] = "";
    for (int i = 0; i < 27; ++i) strcat(many, " a");
    CHECK(fresh(many) == 0);
    CHECK(es2_sim_startup_run(&start) == ES2_SIM_STARTUP_REFUSED && last.va == 0x00465351u);

    /* 9. No drive.cfg: the fatal report with the path, exit 1. */
    CHECK(install("nodrive", NULL) == 0);
    CHECK(fresh("-eggplant") == 0);
    CHECK(es2_sim_startup_run(&start) == ES2_SIM_STARTUP_EXIT && start.exit_code == 1);
    CHECK(strstr(start.platform.message_text, "L 572 in \"MAIN.CPP\": data\\drive.cfg") != NULL);
    CHECK(get("assert.log", buf, sizeof(buf)) != (size_t)-1);

    /* 10. An empty drive.cfg: fscanf gives EOF (-1, nonzero), so the root is
     * the (here zeroed) buffer plus "\" (the original's quirk). A second token
     * longer than 80 bytes runs into the first buffer before the copy. */
    CHECK(install("empty", "") == 0);
    CHECK(fresh("-eggplant") == 0);
    CHECK(es2_sim_startup_run(&start) == ES2_SIM_STARTUP_REFUSED && same(0x004d280cu, "\\"));
    char longer[256];
    snprintf(longer, sizeof(longer), "C:\\CD\r\nC:\\%0100d\r\n", 0);
    CHECK(install("long", longer) == 0);
    CHECK(fresh("-eggplant") == 0);
    CHECK(es2_sim_startup_run(&start) == ES2_SIM_STARTUP_REFUSED && last.va == 0x00464d44u);
    CHECK(same(0x004d280cu, "00000000000000000000000"));   /* the token's bytes 80..102 */
    snprintf(longer, sizeof(longer), "C:\\CD\r\nC:\\%0170d\r\n", 0);
    CHECK(install("longer", longer) == 0);
    CHECK(fresh("-eggplant") == 0);
    CHECK(es2_sim_startup_run(&start) == ES2_SIM_STARTUP_REFUSED && last.va == 0x0045f21eu);

    /* 11. The file services directly: fopen/fwrite/fseek/ftell/fread/fclose
     * and the path helper's registers. */
    CHECK(install("files", "C:\\CD\r\n") == 0);
    CHECK(fresh("x") == 0);
    text(local_guest + 0xb000u, "data\\x.bin"); text(local_guest + 0xb020u, "w+b"); text(local_guest + 0xb030u, "hello");
    uint32_t a[4] = {local_guest + 0xb000u, local_guest + 0xb020u}, fp = 0, eax = 0;
    CHECK(es2_sim_files_call(&files, ES2_IO_FOPEN, a, 2, &fp) == 0 && fp != 0);
    uint32_t wv[4] = {local_guest + 0xb030u, 1, 5, fp};
    CHECK(es2_sim_files_call(&files, ES2_IO_FWRITE, wv, 4, &eax) == 0 && eax == 5u);
    uint32_t sk[3] = {fp, 1, 0};
    CHECK(es2_sim_files_call(&files, ES2_IO_FSEEK, sk, 3, &eax) == 0 && eax == 0);
    uint32_t tl[1] = {fp};
    CHECK(es2_sim_files_call(&files, ES2_IO_FTELL, tl, 1, &eax) == 0 && eax == 1u);
    uint32_t rd[4] = {local_guest + 0xb040u, 1, 4, fp};
    CHECK(es2_sim_files_call(&files, ES2_IO_FREAD, rd, 4, &eax) == 0 && eax == 4u && same(local_guest + 0xb040u, "ello"));
    CHECK(es2_sim_files_call(&files, ES2_IO_FCLOSE, tl, 1, &eax) == 0 && eax == 0);
    uint32_t regs[3];
    text(0x004d27bcu, "R\\");
    text(local_guest + 0xb060u, "X:data\\a");
    es2_sim_path_root(&memory, local_guest + 0xb060u, regs);
    CHECK(regs[0] == 0x004d285cu && same(0x004d285cu, "R\\data\\a") && regs[1] == 7u && regs[2] == 0);
    /* A float conversion is outside the model: refused, nothing done. */
    text(local_guest + 0xb080u, "%f");
    uint32_t sc[16] = {fp, local_guest + 0xb080u, local_guest + 0xb090u};
    CHECK(es2_sim_files_call(&files, ES2_IO_FSCANF, sc, 16, &eax) == 1);

    /* 12. the process platform (a recorder). The eggplant box:
     * owner 0, style MB_ICONEXCLAMATION, no process end. */
    es2_sim_process_platform p = {rec_box, rec_exit, NULL, NULL, NULL};
    CHECK(install("full", "C:\\CD\r\nC:\\INSTALL\r\n") == 0);
    CHECK(fresh("-X5") == 0);
    memset(&rec, 0, sizeof(rec));
    start.process = &p;
    CHECK(es2_sim_startup_run(&start) == ES2_SIM_STARTUP_RETURNED && start.exit_code == 0 && rec.exits == 0);
    CHECK(rec.boxes == 1 && rec.owner[0] == 0 && rec.style[0] == 0x30u && !strcmp(rec.text[0], "eggplant text") &&
          !strcmp(rec.caption[0], "caption"));
    /* No drive.cfg: the fatal box (style 0, caption 0x4a1664, the whole
     * text), then __exit(1) after PostQuitMessage. */
    CHECK(install("nodrive", NULL) == 0);
    CHECK(fresh("-eggplant") == 0);
    memset(&rec, 0, sizeof(rec));
    start.process = &p;
    CHECK(es2_sim_startup_run(&start) == ES2_SIM_STARTUP_EXIT && start.exit_code == 1);
    CHECK(rec.boxes == 1 && rec.owner[0] == 0 && rec.style[0] == 0 && !strcmp(rec.caption[0], "Error"));
    CHECK(!strncmp(rec.text[0], "Internal error:\n\n", 17) && strstr(rec.text[0], "L 572 in \"MAIN.CPP\": data\\drive.cfg"));
    CHECK(rec.exits == 1 && rec.status == 1 && rec.post_quit_at_exit == 1);
    /* assert.log cannot be opened (a directory): headless _abort is
     * refused; with the record CW3220's box and __exit(3). */
    CHECK(install("abort", NULL) == 0);
    CreateDirectoryA("assert.log", NULL);
    CHECK(fresh("-eggplant") == 0);
    CHECK(es2_sim_startup_run(&start) == ES2_SIM_STARTUP_REFUSED && last.va == 0x0049688cu);
    CHECK(fresh("-eggplant") == 0);
    memset(&rec, 0, sizeof(rec));
    start.process = &p;
    CHECK(es2_sim_startup_run(&start) == ES2_SIM_STARTUP_EXIT && start.exit_code == 3);
    CHECK(rec.boxes == 2 && rec.style[0] == 0 && rec.style[1] == 0x00012010u && rec.owner[1] == 0);
    CHECK(!strcmp(rec.text[1], "Abnormal program termination") && !strcmp(rec.caption[1], "DBSIM.EXE"));
    CHECK(rec.exits == 1 && rec.status == 3);

    SetCurrentDirectoryA(here);
    printf("PASS: start-up on synthetic installations (deferred DIB stop, load call, -eggplant refusal, tape record and "
           "playback, memory paths, fatal reports with assert.log, DirectDraw, argument limit, drive.cfg quirks, file services, the process platform's boxes and exits)\n");
    return 0;
}
