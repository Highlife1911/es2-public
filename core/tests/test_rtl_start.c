/* Unit test of core/src/rtl_start.c with a scripted env. Expected values are
 * hand-traced from the contract in docs/reference-game/rtl_imports.md
 * ("the runtime lifecycle"); the in-vitro harness compares the model with the
 * original DLL. */
#include "es2/core/rtl_start.h"

#include <stdio.h>
#include <string.h>

static unsigned failures, checks;
static void check(int passed, const char *expression, int line)
{
    ++checks;
    if (!passed) {
        if (failures < 30) fprintf(stderr, "rtl_start:%d: %s\n", line, expression);
        ++failures;
    }
}
#define CHECK(expression) check((expression) ? 1 : 0, #expression, __LINE__)

/* Guest memory: a flat window at BASE. */
#define BASE 0x10000000u
#define SIZE 0x10000u
static uint8_t memory[SIZE];
static unsigned outside;
static uint8_t read8(void *c, uint32_t a) { (void)c; if (a - BASE < SIZE) return memory[a - BASE]; ++outside; return 0; }
static void write8(void *c, uint32_t a, uint8_t v) { (void)c; if (a - BASE < SIZE) memory[a - BASE] = v; else ++outside; }
static uint32_t read32(void *c, uint32_t a) { return read8(c, a) | (uint32_t)read8(c, a + 1) << 8 | (uint32_t)read8(c, a + 2) << 16 | (uint32_t)read8(c, a + 3) << 24; }
static void put32(uint32_t a, uint32_t v) { for (unsigned i = 0; i < 4; ++i) memory[a - BASE + i] = (uint8_t)(v >> (8 * i)); }

/* Event log. */
enum { E_CALL, E_FPU, E_FRAME, E_ENV, E_CMD, E_HANDLE, E_FILE, E_VERSION, E_THREAD, E_WINDOW, E_BOX, E_EXIT };
typedef struct event { unsigned kind; uint32_t a, b, c, d, esp; char text[96]; } event;
static event events[512];
static unsigned event_count;
static event *add(unsigned kind, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t esp)
{
    static event spare;
    if (event_count >= 512) return &spare;
    event *e = &events[event_count++];
    memset(e, 0, sizeof(*e));
    e->kind = kind; e->a = a; e->b = b; e->c = c; e->d = d; e->esp = esp;
    return e;
}

/* Scripted behaviour. */
static es2_rtl_runtime rt;
static es2_rtl_start_env env;
static uint32_t winmain_result, winmain_popped, version_word, has_window;
static const char *file_name = "C:\\GAMES\\ES2\\DBSIM.EXE";
#define F_REGISTER 0x500u        /* a function that registers F_LATE through atexit */
#define F_LATE 0x600u
#define F_EXIT 0x700u            /* a function that calls exit(9) */
static void call(void *c, es2_rtl_guest_call *k)
{
    (void)c;
    event *e = add(E_CALL, k->kind, k->target, k->arg_count, k->arg_count ? k->args[0] : 0, k->esp);
    e->text[0] = (char)k->arg_count;
    memcpy(e->text + 4, k->args, sizeof(k->args));
    k->result = 0;
    k->popped = 0;
    if (k->kind == ES2_RTL_CALL_WINMAIN || k->kind == ES2_RTL_CALL_MAIN) { k->result = winmain_result; k->popped = winmain_popped; }
    if (k->target == F_REGISTER) es2_rtl_atexit(&rt, F_LATE);
    if (k->target == F_EXIT) es2_rtl_exit(&rt, &env, 0x9000u, 9);
}
static void fpu_reset(void *c, uint16_t w) { (void)c; add(E_FPU, w, 0, 0, 0, 0); }
static void register_frame(void *c, uint32_t r, uint32_t h) { (void)c; add(E_FRAME, r, h, 0, 0, 0); }
static uint32_t environment_strings(void *c) { (void)c; add(E_ENV, 0, 0, 0, 0, 0); return 0xe0e0e0e0u; }
#define CMD (BASE + 0x8000u)
static uint32_t command_line(void *c) { (void)c; add(E_CMD, 0, 0, 0, 0, 0); return CMD; }
static uint32_t module_handle(void *c, uint32_t n) { (void)c; add(E_HANDLE, n, 0, 0, 0, 0); return 0x400000u; }
static uint32_t module_file_name(void *c, char *b, uint32_t n)
{
    (void)c;
    add(E_FILE, n, 0, 0, 0, 0);
    size_t l = strlen(file_name);
    if (l >= n) l = n - 1;
    memcpy(b, file_name, l);
    b[l] = 0;
    return (uint32_t)l;
}
static uint32_t version(void *c) { (void)c; add(E_VERSION, 0, 0, 0, 0, 0); return version_word; }
static uint32_t thread_id(void *c) { (void)c; add(E_THREAD, 0, 0, 0, 0, 0); return 0x1234u; }
static int thread_has_window(void *c, uint32_t t) { (void)c; add(E_WINDOW, t, 0, 0, 0, 0); return (int)has_window; }
static void message_box(void *c, uint32_t text, const char *title, uint32_t style, uint32_t esp)
{
    (void)c;
    event *e = add(E_BOX, text, style, 0, 0, esp);
    snprintf(e->text, sizeof(e->text), "%s", title);
}
static void exit_process(void *c, uint32_t status, uint32_t esp) { (void)c; add(E_EXIT, status, 0, 0, 0, esp); }

static void reset(void)
{
    memset(memory, 0, sizeof(memory));
    event_count = 0;
    es2_rtl_runtime_init(&rt, 0xd0d0u, 0xab0u);
    memset(&env, 0, sizeof(env));
    env.read8 = read8; env.write8 = write8; env.read32 = read32; env.call = call;
    env.fpu_reset = fpu_reset; env.register_frame = register_frame; env.environment_strings = environment_strings;
    env.command_line = command_line; env.module_handle = module_handle; env.module_file_name = module_file_name;
    env.version = version; env.thread_id = thread_id; env.thread_has_window = thread_has_window;
    env.message_box = message_box; env.exit_process = exit_process;
    winmain_result = 0; winmain_popped = 16; version_word = 0x0a280105u; has_window = 0;
    file_name = "C:\\GAMES\\ES2\\DBSIM.EXE";
}

/* A module table at TABLE with init records at INIT and exit records at
 * EXITS: (flag, priority, function) triples. */
#define TABLE (BASE + 0x100u)
#define INIT (BASE + 0x200u)
#define EXITS (BASE + 0x400u)
static void record(uint32_t at, uint8_t done, uint8_t priority, uint32_t function)
{
    memory[at - BASE] = done;
    memory[at - BASE + 1] = priority;
    put32(at + 2, function);
}
static void table(uint32_t flags, unsigned inits, unsigned exits)
{
    put32(TABLE + 0x00, INIT);
    put32(TABLE + 0x04, INIT + 6 * inits);
    put32(TABLE + 0x08, EXITS);
    put32(TABLE + 0x0c, EXITS + 6 * exits);
    put32(TABLE + 0x10, flags);
    put32(TABLE + 0x18, 0x46538cu);
    put32(TABLE + 0x1c, 0x496510u);
    put32(TABLE + 0x20, 0x49653cu);
    put32(TABLE + 0x28, 0x4a7974u);
}
static void command(const char *s) { memcpy(memory + (CMD - BASE), s, strlen(s) + 1); }
static unsigned calls_of(es2_rtl_call_kind kind, uint32_t *targets, uint32_t *esps, unsigned room)
{
    unsigned n = 0;
    for (unsigned i = 0; i < event_count; ++i)
        if (events[i].kind == E_CALL && events[i].a == (uint32_t)kind) {
            if (n < room) { targets[n] = events[i].b; esps[n] = events[i].esp; }
            ++n;
        }
    return n;
}
static const event *find(unsigned kind)
{
    for (unsigned i = 0; i < event_count; ++i) if (events[i].kind == kind) return &events[i];
    return NULL;
}

static void test_selector(void)
{
    reset();
    table(1, 6, 6);
    /* Priorities 5, 3, 3, 7, a done 1, 3: init order 0x102, 0x103, 0x106, 0x101, 0x104. */
    static const uint8_t pri[6] = {5, 3, 3, 7, 1, 3};
    static const uint8_t done[6] = {0, 0, 0, 0, 1, 0};
    for (unsigned i = 0; i < 6; ++i) { record(INIT + 6 * i, done[i], pri[i], 0x101u + i); record(EXITS + 6 * i, done[i], pri[i], 0x201u + i); }
    rt.module_count = 1; rt.module_table = TABLE;
    static const uint32_t init_order[5] = {0x102, 0x103, 0x106, 0x101, 0x104};
    for (unsigned i = 0; i < 5; ++i) CHECK(es2_rtl_select_record(&rt, &env, 0) == init_order[i]);
    CHECK(es2_rtl_select_record(&rt, &env, 0) == 0);
    for (unsigned i = 0; i < 6; ++i) CHECK(memory[INIT - BASE + 6 * i] == 1);
    /* Exit: highest first, equal priorities last-in-table first. */
    static const uint32_t exit_order[5] = {0x204, 0x201, 0x206, 0x203, 0x202};
    for (unsigned i = 0; i < 5; ++i) CHECK(es2_rtl_select_record(&rt, &env, 1) == exit_order[i]);
    CHECK(es2_rtl_select_record(&rt, &env, 1) == 0);
    /* A record with function 0 ends the scan; an empty list selects nothing. */
    reset();
    table(1, 3, 0);
    record(INIT, 0, 9, 0x111); record(INIT + 6, 0, 1, 0); record(INIT + 12, 0, 0, 0x113);
    rt.module_count = 1; rt.module_table = TABLE;
    CHECK(es2_rtl_select_record(&rt, &env, 0) == 0x111);
    CHECK(es2_rtl_select_record(&rt, &env, 0) == 0);
    CHECK(memory[INIT + 12 - BASE] == 0);
    rt.module_count = 0;
    record(INIT, 0, 9, 0x111);
    CHECK(es2_rtl_select_record(&rt, &env, 0) == 0 && memory[INIT - BASE] == 0);
    /* Priority 0xff is still below the init threshold 0xffff; an exit
     * record of priority 0 is still at least 0. */
    reset();
    table(1, 1, 1);
    record(INIT, 0, 0xff, 0x121); record(EXITS, 0, 0, 0x221);
    rt.module_count = 1; rt.module_table = TABLE;
    CHECK(es2_rtl_select_record(&rt, &env, 0) == 0x121);
    CHECK(es2_rtl_select_record(&rt, &env, 1) == 0x221);
}

static void test_tail(void)
{
    static const struct { const char *line; unsigned offset; } cases[] = {
        {"\"C:\\ES 2\\DBSIM.EXE\"  -X3 -R1", 21},
        {"DBSIM.EXE -X3", 10},
        {"  \t DBSIM.EXE\t\t-a", 15},
        {"DBSIM.EXE", 9},
        {"\"unterminated name", 18},
        {"\"a\tb\" c", 3},
        {"\"\" rest", 3},
        {"", 0},
        {"name\"x\" y", 8},
    };
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        reset();
        command(cases[i].line);
        CHECK(es2_rtl_command_tail(&env, CMD) == CMD + cases[i].offset);
    }
}

static void test_startup_gui(void)
{
    reset();
    table(0xffff0001u, 3, 2);
    record(INIT, 0, 0x20, 0x101); record(INIT + 6, 0, 0x10, 0x102); record(INIT + 12, 0, 0x20, 0x103);
    record(EXITS, 0, 0x20, 0x201); record(EXITS + 6, 0, 0x20, 0x202);
    command("DBSIM.EXE -X2 -R1");
    winmain_result = 4;
    rt.exitbuf = 0x301; rt.exitfopen = 0x302; rt.exitopen = 0x303;
    es2_rtl_atexit(&rt, 0x401);
    es2_rtl_atexit(&rt, F_REGISTER);
    const uint32_t E = 0x0012ff00u;
    CHECK(es2_rtl_startup(&rt, &env, E, TABLE) == ES2_RTL_EXITED && rt.exited);
    CHECK(rt.gui == 1 && rt.module_count == 1 && rt.module_table == TABLE && rt.frame_record == E - 0xc);
    CHECK(rt.matherr == 0x496510u && rt.matherrl == 0x49653cu && rt.fmode == 0x4a7974u);
    CHECK(rt.environment == 0xe0e0e0e0u && rt.command_line == CMD && rt.atexit_count == 0);
    /* Order of the first events. */
    CHECK(event_count > 6 && events[0].kind == E_FPU && events[0].a == ES2_RTL_FPU_DEFAULT);
    CHECK(events[1].kind == E_FRAME && events[1].a == E - 0xc && events[1].b == ES2_RTL_FRAME_HANDLER_RVA);
    CHECK(events[2].kind == E_ENV && events[3].kind == E_CMD);
    uint32_t t[16], s[16];
    CHECK(calls_of(ES2_RTL_CALL_INIT_RECORD, t, s, 16) == 3 && t[0] == 0x102 && t[1] == 0x101 && t[2] == 0x103 && s[0] == E - 0x20 && s[2] == E - 0x20);
    const event *h = find(E_HANDLE);
    CHECK(h && h->a == 0);
    CHECK(calls_of(ES2_RTL_CALL_WINMAIN, t, s, 16) == 1 && t[0] == 0x46538cu && s[0] == E - 0x30);
    for (unsigned i = 0; i < event_count; ++i)
        if (events[i].kind == E_CALL && events[i].a == ES2_RTL_CALL_WINMAIN) {
            uint32_t a[4]; memcpy(a, events[i].text + 4, sizeof(a));
            CHECK(a[0] == 0x400000u && a[1] == 0 && a[2] == CMD + 10 && a[3] == 10 && events[i].c == 4);
        }
    /* exit(4) at X = E - 0x24 + 16: atexit LIFO, the late registration
     * first of the rest, records reversed, the three hooks, ExitProcess. */
    const uint32_t X = E - 0x24 + 16, Y = X - 0x14;
    CHECK(calls_of(ES2_RTL_CALL_ATEXIT, t, s, 16) == 3 && t[0] == F_REGISTER && t[1] == F_LATE && t[2] == 0x401 && s[0] == Y - 0xc);
    CHECK(calls_of(ES2_RTL_CALL_EXIT_RECORD, t, s, 16) == 2 && t[0] == 0x202 && t[1] == 0x201 && s[0] == Y - 0x1c);
    CHECK(calls_of(ES2_RTL_CALL_EXITBUF, t, s, 16) == 1 && t[0] == 0x301 && s[0] == Y - 0xc);
    CHECK(calls_of(ES2_RTL_CALL_EXITFOPEN, t, s, 16) == 1 && t[0] == 0x302);
    CHECK(calls_of(ES2_RTL_CALL_EXITOPEN, t, s, 16) == 1 && t[0] == 0x303);
    const event *x = find(E_EXIT);
    CHECK(x && x->a == 4 && x->esp == Y - 0x1c && x == &events[event_count - 1]);
    for (unsigned i = 0; i < 3; ++i) CHECK(memory[INIT - BASE + 6 * i] == 1);
    for (unsigned i = 0; i < 2; ++i) CHECK(memory[EXITS - BASE + 6 * i] == 1);
    CHECK(outside == 0);
}

static void test_startup_console(void)
{
    reset();
    table(2, 0, 0);
    rt.argc = 3; rt.argv = 0x7000; rt.envp = 0x7100;
    winmain_result = 0x55; winmain_popped = 0;
    const uint32_t E = 0x00200000u;
    CHECK(es2_rtl_startup(&rt, &env, E, TABLE) == ES2_RTL_EXITED);
    CHECK(rt.gui == 0 && find(E_HANDLE) == NULL);
    uint32_t t[4], s[4];
    CHECK(calls_of(ES2_RTL_CALL_MAIN, t, s, 4) == 1 && s[0] == E - 0x2c);
    for (unsigned i = 0; i < event_count; ++i)
        if (events[i].kind == E_CALL && events[i].a == ES2_RTL_CALL_MAIN) {
            uint32_t a[4]; memcpy(a, events[i].text + 4, sizeof(a));
            CHECK(a[0] == 3 && a[1] == 0x7000 && a[2] == 0x7100 && events[i].c == 3);
        }
    const event *x = find(E_EXIT);
    CHECK(x && x->a == 0x55 && x->esp == E - 0x24 - 0x14 - 0x1c);
    /* The hooks still at the dummy are called too. */
    CHECK(calls_of(ES2_RTL_CALL_EXITBUF, t, s, 4) == 1 && t[0] == 0xd0d0u);
}

static void test_exit_variants(void)
{
    uint32_t t[40], s[40];
    /* atexit: 32 entries, then 1. */
    reset();
    for (unsigned i = 0; i < 32; ++i) CHECK(es2_rtl_atexit(&rt, 0x800u + i) == 0);
    CHECK(es2_rtl_atexit(&rt, 0x900u) == 1 && rt.atexit_count == 32);
    CHECK(es2_rtl_exit(&rt, &env, 0x5000u, 7) == ES2_RTL_EXITED);
    CHECK(calls_of(ES2_RTL_CALL_ATEXIT, t, s, 40) == 32 && t[0] == 0x81fu && t[31] == 0x800u);
    /* _exit: nothing but ExitProcess. */
    reset();
    es2_rtl_atexit(&rt, 0x801u);
    rt.module_count = 1; rt.module_table = TABLE; table(1, 0, 1); record(EXITS, 0, 1, 0x201);
    CHECK(es2_rtl_quick_exit(&rt, &env, 0x5000u, 2) == ES2_RTL_EXITED);
    CHECK(event_count == 1 && events[0].kind == E_EXIT && events[0].a == 2 && events[0].esp == 0x5000u - 0x14 - 0x1c);
    CHECK(rt.atexit_count == 1 && memory[EXITS - BASE] == 0);
    /* _cexit: atexit, records, exitbuf; returns. _c_exit: nothing. */
    reset();
    es2_rtl_atexit(&rt, 0x801u);
    rt.module_count = 1; rt.module_table = TABLE; table(1, 0, 1); record(EXITS, 0, 1, 0x201);
    CHECK(es2_rtl_cexit(&rt, &env, 0x5000u) == ES2_RTL_RETURNED && !rt.exited);
    CHECK(event_count == 3 && events[0].b == 0x801u && events[0].esp == 0x5000u - 0x10 - 0xc && events[1].b == 0x201u &&
          events[1].esp == 0x5000u - 0x10 - 0x1c && events[2].a == ES2_RTL_CALL_EXITBUF);
    reset();
    es2_rtl_atexit(&rt, 0x801u);
    CHECK(es2_rtl_c_exit(&rt, &env, 0x5000u) == ES2_RTL_RETURNED && event_count == 0 && rt.atexit_count == 1);
    /* exit from inside an init record: the rest of the start-up is skipped. */
    reset();
    table(1, 2, 0);
    record(INIT, 0, 1, F_EXIT); record(INIT + 6, 0, 2, 0x102);
    command("x");
    CHECK(es2_rtl_startup(&rt, &env, 0x8000u, TABLE) == ES2_RTL_EXITED);
    CHECK(calls_of(ES2_RTL_CALL_INIT_RECORD, t, s, 4) == 1 && calls_of(ES2_RTL_CALL_WINMAIN, t, s, 4) == 0);
    const event *x = find(E_EXIT);
    CHECK(x && x->a == 9 && x->esp == 0x9000u - 0x14 - 0x1c && memory[INIT + 6 - BASE] == 0);
}

static void test_abort(void)
{
    const uint32_t X = 0x6000u;
    /* Default handler: message from raise's depth, _exit(3). */
    reset();
    CHECK(es2_rtl_abort(&rt, &env, X) == ES2_RTL_EXITED);
    const event *b = find(E_BOX), *x = find(E_EXIT);
    CHECK(b && b->a == 0xab0u && b->b == 0x12010u && b->esp == X - 0x20 - 0x74 && !strcmp(b->text, "DBSIM.EXE"));
    CHECK(x && x->a == 3 && x->esp == X - 0x20 - 0x14 - 0x1c);
    CHECK(find(E_THREAD) == NULL && find(E_FILE)->a == 0x50);
    /* Ignored: message from abort's depth. */
    reset();
    rt.sigabrt = 1;
    es2_rtl_atexit(&rt, 0x801u);
    CHECK(es2_rtl_abort(&rt, &env, X) == ES2_RTL_EXITED);
    b = find(E_BOX); x = find(E_EXIT);
    CHECK(b && b->esp == X - 0xc - 0x74 && x && x->a == 3 && x->esp == X - 0xc - 0x14 - 0x1c && rt.sigabrt == 1);
    CHECK(rt.atexit_count == 1);
    /* A handler: reset to default, called with (22) or (22, extra). */
    uint32_t t[4], s[4];
    reset();
    rt.sigabrt = 0x777u;
    CHECK(es2_rtl_abort(&rt, &env, X) == ES2_RTL_EXITED && rt.sigabrt == 0);
    CHECK(calls_of(ES2_RTL_CALL_SIGNAL, t, s, 4) == 1 && t[0] == 0x777u && s[0] == X - 0x1c && events[0].c == 1 && events[0].d == 22);
    reset();
    rt.sigabrt = 0x777u; rt.sigabrt_extra = 0x8c;
    CHECK(es2_rtl_abort(&rt, &env, X) == ES2_RTL_EXITED);
    CHECK(calls_of(ES2_RTL_CALL_SIGNAL, t, s, 4) == 1 && s[0] == X - 0x20 && events[0].c == 2);
    uint32_t a[4]; memcpy(a, events[0].text + 4, sizeof(a));
    CHECK(a[0] == 22 && a[1] == 0x8c);
    x = find(E_EXIT);
    CHECK(x && x->esp == X - 0xc - 0x14 - 0x1c);
}

static void test_error_exit(void)
{
    const uint32_t X = 0x7000u;
    reset();
    es2_rtl_atexit(&rt, 0x801u);
    CHECK(es2_rtl_error_exit(&rt, &env, X, 0x4a7978u) == ES2_RTL_EXITED);
    const event *b = find(E_BOX), *x = find(E_EXIT);
    CHECK(b && b->a == 0x4a7978u && b->esp == X - 0xc - 0x74 && x && x->a == 1 && x->esp == X - 0xc - 0x14 - 0x1c);
    CHECK(rt.atexit_count == 1);
    /* Titles and modality. */
    static const struct { const char *name, *title; uint32_t version, window, style; int thread; } cases[] = {
        {"C:\\GAMES\\ES2\\DBSIM.EXE", "DBSIM.EXE", 0x0a280105u, 0, 0x12010u, 0},
        {"D:DBSIM.EXE", "DBSIM.EXE", 0x80000004u, 1, 0x12010u, 1},
        {"DBSIM.EXE", "DBSIM.EXE", 0x80000004u, 0, 0x11010u, 1},
        {"A:\\B:C", "B:C", 0xc0000a00u, 0, 0x11010u, 1},
        {"x:y:z", "z", 0x7fffffffu, 0, 0x12010u, 0},
    };
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        reset();
        file_name = cases[i].name; version_word = cases[i].version; has_window = cases[i].window;
        es2_rtl_error_message(&env, 0x3000u, 0x1111u);
        b = find(E_BOX);
        CHECK(b && !strcmp(b->text, cases[i].title) && b->b == cases[i].style && b->esp == 0x3000u - 0x74);
        CHECK((find(E_THREAD) != NULL) == cases[i].thread && (find(E_WINDOW) != NULL) == cases[i].thread);
        if (cases[i].thread) CHECK(find(E_WINDOW)->a == 0x1234u);
    }
    /* A name of 80 characters or more is cut to 79. */
    reset();
    file_name = "C:\\0123456789012345678901234567890123456789012345678901234567890123456789\\ABCDEFGHIJKLMNOP";
    es2_rtl_error_message(&env, 0x3000u, 0x1111u);
    CHECK(!strcmp(find(E_BOX)->text, "ABCDE"));
}

/* Registers, frame words, return addresses and sites, hand-traced
 * from the listing (rtl_imports.md "the lifecycle group as own code"). */
static es2_rtl_guest_call h68_calls[32];
static es2_rtl_site h68_sites[32];
static unsigned h68_call_count, h68_site_count;
static void write32(void *c, uint32_t a, uint32_t v) { (void)c; if (a - BASE < SIZE - 3u) put32(a, v); else ++outside; }
static uint32_t word(uint32_t a) { return read32(NULL, a); }
/* The stack words a callee sees at its entry (from esp+4), per kind. */
static uint32_t h68_view[9][16];
static void h68_call(void *c, es2_rtl_guest_call *k)
{
    (void)c;
    if ((unsigned)k->kind < 9u)
        for (unsigned i = 0; i < 16; ++i) h68_view[k->kind][i] = word(k->esp + 4u + 4u * i);
    k->result = 0;
    k->popped = 0;
    k->out.ecx = 0xc0000000u + h68_call_count;   /* flows into the next atexit function */
    if (k->kind == ES2_RTL_CALL_WINMAIN) { k->result = 4; k->popped = 16; k->out.ecx = 0xecu; k->out.edx = 0xedu; }
    if (h68_call_count < 32) h68_calls[h68_call_count++] = *k;
}
static void h68_site(void *c, es2_rtl_site *s)
{
    (void)c;
    if (h68_site_count < 32) h68_sites[h68_site_count++] = *s;
    if (s->api == ES2_RTL_API_MODULE_HANDLE) { s->cpu.ecx = 0x1c1cu; s->cpu.edx = 0x1d1du; }
}
static const es2_rtl_guest_call *h68_find(es2_rtl_call_kind kind, unsigned nth)
{
    for (unsigned i = 0; i < h68_call_count; ++i) if (h68_calls[i].kind == kind && !nth--) return &h68_calls[i];
    return NULL;
}
static const es2_rtl_site *h68_find_site(es2_rtl_api api)
{
    for (unsigned i = 0; i < h68_site_count; ++i) if (h68_sites[i].api == api) return &h68_sites[i];
    return NULL;
}
static void test_h68_startup(void)
{
    reset();
    env.call = h68_call; env.write32 = write32; env.site = h68_site;
    h68_call_count = h68_site_count = 0;
    table(1, 1, 1);
    record(INIT, 0, 0x20, 0x101); record(EXITS, 0, 0x20, 0x201);
    command("\"DBSIM.EXE\" -X2");
    rt.code_base = 0x00f00000u;
    rt.stream_hooks = BASE + 0x700u;   /* CW3220's hook words in guest memory */
    put32(BASE + 0x700u, 0x311u); put32(BASE + 0x704u, 0x312u); put32(BASE + 0x708u, 0x313u);
    es2_rtl_atexit(&rt, 0x401u); es2_rtl_atexit(&rt, 0x402u);
    const es2_rtl_cpu entry = {0xa0a0u, 0xc0c0u, 0xd0d0u, 0xb0b0u, 0x5151u, 0xd1d1u, 0xe0e0u};
    rt.entry = entry;
    const uint32_t E = BASE + 0xf000u;
    put32(E - 0x10u, 0x12345678u);
    CHECK(es2_rtl_startup(&rt, &env, E, TABLE) == ES2_RTL_EXITED);
    /* __startup's frame as the init record sees it (from E-1ch): saved EDI,
     * ESI, EBX, the untouched local at E-10h, the SEH record (painted by the
     * env's register_frame, here 0), saved EBP. */
    const uint32_t *v = h68_view[ES2_RTL_CALL_INIT_RECORD];
    CHECK(v[0] == 0xd1d1u && v[1] == 0x5151u && v[2] == 0xb0b0u && v[3] == 0x12345678u && v[6] == 0xe0e0u);
    /* The init record: EAX = EBX = the function, ECX = EDX = the record, ESI the table, EDI 0, EBP E-4. */
    const es2_rtl_guest_call *k = h68_find(ES2_RTL_CALL_INIT_RECORD, 0);
    CHECK(k && k->regs.eax == 0x101u && k->regs.ebx == 0x101u && k->regs.ecx == INIT && k->regs.edx == INIT &&
          k->regs.esi == TABLE && k->regs.edi == 0 && k->regs.ebp == E - 4u && k->return_rva == 0x1a7a0u && k->record == INIT);
    /* GetEnvironmentStrings/GetCommandLineA at E-20h, GetModuleHandleA at E-30h with EBX = lpCmdLine, AL its byte, DL '"'. */
    const es2_rtl_site *s = h68_find_site(ES2_RTL_API_ENVIRONMENT_STRINGS);
    CHECK(s && s->esp == E - 0x20u && s->return_rva == 0x1a739u && s->cpu.eax == E - 0xcu && s->cpu.ecx == E - 0xcu && s->cpu.edx == 0 &&
          s->cpu.ebx == 0xb0b0u && s->cpu.esi == TABLE && s->cpu.edi == 0xd1d1u && s->cpu.ebp == E - 4u);
    s = h68_find_site(ES2_RTL_API_COMMAND_LINE);
    CHECK(s && s->esp == E - 0x20u && s->return_rva == 0x1a743u);
    s = h68_find_site(ES2_RTL_API_MODULE_HANDLE);
    CHECK(s && s->esp == E - 0x30u && s->return_rva == 0x1a80eu && s->cpu.ebx == CMD + 12u && s->cpu.eax == '-' &&
          s->cpu.ecx == 0 && s->cpu.edx == 0xff22u && s->cpu.edi == 0 && s->cpu.esi == TABLE);
    /* WinMain at E-30h: the arguments painted, EAX = hInstance, ECX/EDX from GetModuleHandleA. */
    k = h68_find(ES2_RTL_CALL_WINMAIN, 0);
    CHECK(k && k->esp == E - 0x30u && k->return_rva == 0x1a812u && k->regs.eax == 0x400000u && k->regs.ecx == 0x1c1cu &&
          k->regs.edx == 0x1d1du && k->regs.ebx == CMD + 12u && k->regs.esi == TABLE && k->regs.edi == 0 && k->regs.ebp == E - 4u);
    v = h68_view[ES2_RTL_CALL_WINMAIN];
    CHECK(v[0] == 0x400000u && v[1] == 0 && v[2] == CMD + 12u && v[3] == 10u && v[4] == 0xd1d1u && v[7] == 0x12345678u);
    /* exit at X = E-14h: [X+4] the result, [X] 41a81b; exit's frame; 4191a4's (Y = X-14h). */
    const uint32_t X = E - 0x14u, Y = X - 0x14u;
    CHECK(word(X + 4u) == 4u && word(X) == 0x00f1a81bu && word(X - 4u) == E - 4u && word(X - 8u) == 4u && word(X - 0xcu) == 0 &&
          word(X - 0x10u) == 0 && word(Y) == 0x00f1920cu && word(Y - 4u) == X - 4u && word(Y - 8u) == CMD + 12u);
    /* atexit functions last first: EAX the new count, EDX the function, EBX 0 (quick), ECX from the previous callee. */
    const es2_rtl_guest_call *a0 = h68_find(ES2_RTL_CALL_ATEXIT, 0), *a1 = h68_find(ES2_RTL_CALL_ATEXIT, 1);
    CHECK(a0 && a1 && a0->target == 0x402u && a0->regs.eax == 1u && a0->regs.edx == 0x402u && a0->regs.ebx == 0 && a0->regs.ecx == 0xecu &&
          a0->regs.ebp == Y - 4u && a0->esp == Y - 0xcu && a0->return_rva == 0x191c5u);
    CHECK(a1->target == 0x401u && a1->regs.eax == 0 && a1->regs.ecx == a0->out.ecx && a1->regs.esi == TABLE);
    /* The exit record at Y-1ch below 41a848's frame; the hooks read from guest memory with EAX 0, ECX 1, EDX 0. */
    k = h68_find(ES2_RTL_CALL_EXIT_RECORD, 0);
    CHECK(k && k->esp == Y - 0x1cu && k->return_rva == 0x1a84fu && k->regs.ebx == 0x201u && k->regs.ecx == EXITS && k->regs.ebp == Y - 4u);
    k = h68_find(ES2_RTL_CALL_EXITBUF, 0);
    CHECK(k && k->target == 0x311u && k->regs.eax == 0 && k->regs.ecx == 1u && k->regs.edx == 0 && k->regs.ebx == 0 && k->return_rva == 0x191d9u);
    CHECK(h68_find(ES2_RTL_CALL_EXITFOPEN, 0)->target == 0x312u && h68_find(ES2_RTL_CALL_EXITOPEN, 0)->return_rva == 0x191efu);
    /* ExitProcess at Y-1ch: its argument, 41a8c0's frame, the return into 4191a4. */
    s = h68_find_site(ES2_RTL_API_EXIT_PROCESS);
    CHECK(s && s->esp == Y - 0x1cu && s->return_rva == 0x1a8ccu && s->cpu.eax == 4u && s->cpu.ecx == 4u && s->cpu.ebp == Y - 0x14u);
    CHECK(word(Y - 0x18u) == 4u && word(Y - 0x14u) == Y - 4u && word(Y - 0x10u) == 0x00f191f8u && word(Y - 0xcu) == 4u);
    CHECK(outside == 0);
}
static void test_h68_exits(void)
{
    /* __exit from a caller at X: the frame words, ExitProcess's registers. */
    reset();
    env.call = h68_call; env.write32 = write32; env.site = h68_site;
    h68_call_count = h68_site_count = 0;
    rt.code_base = 0x00f00000u;
    const es2_rtl_cpu caller = {0xa1u, 0xc1u, 0xd1u, 0xb1u, 0x51u, 0xd2u, 0xe1u};
    rt.entry = caller;
    const uint32_t X = BASE + 0xe000u, Y = X - 0x14u;
    CHECK(es2_rtl_quick_exit(&rt, &env, X, 7u) == ES2_RTL_EXITED && h68_call_count == 0);
    CHECK(word(X - 4u) == 0xe1u && word(X - 8u) == 7u && word(X - 0xcu) == 0 && word(X - 0x10u) == 1u && word(X - 0x14u) == 0x00f19224u &&
          word(Y - 4u) == X - 4u && word(Y - 8u) == 0xb1u);
    const es2_rtl_site *s = h68_find_site(ES2_RTL_API_EXIT_PROCESS);
    CHECK(s && s->esp == Y - 0x1cu && s->cpu.eax == 7u && s->cpu.ecx == 7u && s->cpu.edx == 0xd1u && s->cpu.ebx == 1u &&
          s->cpu.esi == 0x51u && s->cpu.edi == 0xd2u && s->cpu.ebp == Y - 0x14u);
    /* abort with a handler in guest memory: the reset written there, the
     * handler with (22, extra) at X-20h, EAX the handler, EBX 9, ECX/ESI 22,
     * EDX the extra byte, EBP raise's frame; then the message from X-0ch. */
    reset();
    env.call = h68_call; env.write32 = write32; env.site = h68_site;
    h68_call_count = h68_site_count = 0;
    rt.code_base = 0x00f00000u;
    rt.sigabrt_slot = BASE + 0x720u; rt.sigabrt_extra_slot = BASE + 0x724u;
    put32(BASE + 0x720u, 0x777u); memory[0x724] = 0x8c;
    rt.entry = caller;
    CHECK(es2_rtl_abort(&rt, &env, X) == ES2_RTL_EXITED && word(BASE + 0x720u) == 0);
    const es2_rtl_guest_call *k = h68_find(ES2_RTL_CALL_SIGNAL, 0);
    CHECK(k && k->target == 0x777u && k->esp == X - 0x20u && k->return_rva == 0x1a359u && k->regs.eax == 0x777u && k->regs.ebx == 9u &&
          k->regs.ecx == 22u && k->regs.esi == 22u && k->regs.edx == 0x8cu && k->regs.ebp == X - 0xcu && k->regs.edi == 0xd2u);
    CHECK(word(X - 4u) == 0x00f1906du);
    s = h68_find_site(ES2_RTL_API_MODULE_FILE_NAME);
    CHECK(s && s->esp == X - 0xcu - 0x70u && s->guest == X - 0xcu - 0x58u && s->return_rva == 0x18f05u && s->cpu.esi == rt.abort_message &&
          s->cpu.ecx == 22u && s->cpu.ebx == 0xb1u && s->cpu.ebp == X - 0xcu - 4u);
    s = h68_find_site(ES2_RTL_API_MESSAGE_BOX);
    CHECK(s && s->esp == X - 0xcu - 0x74u && s->return_rva == 0x18f47u && word(s->esp + 0x10u) == 0x12010u && word(s->esp + 8u) == rt.abort_message);
    s = h68_find_site(ES2_RTL_API_EXIT_PROCESS);
    CHECK(s && s->esp == X - 0xcu - 0x14u - 0x1cu && s->cpu.eax == 3u && word(X - 8u) == 3u && word(X - 0xcu) == 0x00f1905eu);
    CHECK(outside == 0);
}

int main(void)
{
    test_h68_startup();
    test_h68_exits();
    test_selector();
    test_tail();
    test_startup_gui();
    test_startup_console();
    test_exit_variants();
    test_abort();
    test_error_exit();
    if (failures) {
        fprintf(stderr, "rtl_start: %u of %u checks failed\n", failures, checks);
        return 1;
    }
    printf("rtl_start: %u checks passed\n", checks);
    return 0;
}
