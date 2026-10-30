/* The _time row 0x4967fc (src/services_files.c) through the dispatch on a
 * synthetic guest. Headless: the default clock ES2_SIM_FILES_TIME_DEFAULT,
 * es2_sim_files_set_time's seconds returned exactly (fixed dates, 2,000
 * random values; the SYSTEMTIME against FileTimeToSystemTime), 0 gives -1,
 * a NULL pointer is not written, no clock (context NULL) and no bound file
 * services refuse. Wall clock: GetLocalTime and GetTimeZoneInformation as
 * the system answers them, the row's value within an hour of time() (the
 * model's US summer rule). No game data. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <windows.h>
#include "es2/sim/files.h"
#include "es2/sim/memory.h"
#include "es2/sim/services.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

#define VA_TIME 0x004967fcu
#define HEAP_BASE 0x20000000u
#define HEAP_SIZE 0x00100000u
#define REGION_BASE 0x21000000u
#define STACK_SIZE 0x00010000u

static uint8_t *heap_bytes, *region_bytes, *stack_bytes;
static es2_sim_arena arena;
static es2_guest_memory memory;
static es2_sim_heap heap;
static es2_vt vt;
static es2_sim_services services;
static es2_sim_entry storage[4096];
static es2_sim_dispatch table;
static es2_sim_files files;
static unsigned refusals;
static void on_refuse(const es2_sim_refusal *r, void *user) { (void)r; (void)user; ++refusals; }

static uint32_t next_reserve = HEAP_BASE;
static uint32_t os_alloc(void *c, uint32_t address, uint32_t size, uint32_t type, uint32_t protect) {
    (void)c; (void)protect;
    if (type & ES2_HEAP_MEM_RESERVE) {
        if (!address) address = next_reserve;
        if (address < HEAP_BASE || address + size > HEAP_BASE + HEAP_SIZE) return 0;
        next_reserve = address + size;
    }
    return address;
}
static uint32_t os_free(void *c, uint32_t a, uint32_t s, uint32_t t) { (void)c; (void)a; (void)s; (void)t; return 1; }
static uint8_t *stack_alloc(void) {
    for (uint32_t a = 0x30000000u; a < 0x38000000u; a += 0x01000000u) {
        void *p = VirtualAlloc((void *)(uintptr_t)a, STACK_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (p) return p;
    }
    return NULL;
}
static uint32_t out_slot(void) { return (uint32_t)(uintptr_t)stack_bytes + 0x800u; }
/* _time(p) through the dispatch; 0 and EAX, or 1 when refused. */
static int time_call(uint32_t p, uint32_t *eax) {
    uint32_t *s = (uint32_t *)(stack_bytes + 0x100);
    s[0] = p;
    const uint32_t regs[3] = {0x11111111u, 0x22222222u, 0x33333333u};
    es2_sim_result r;
    memset(&r, 0, sizeof(r));
    unsigned before = refusals;
    if (es2_sim_call(&table, VA_TIME, s, 1, regs, &r) || refusals != before) return 1;
    *eax = r.eax;
    return 0;
}
static uint32_t rd(uint32_t a) { return memory.read32(memory.context, a); }
static uint32_t seed = 0x5eed1997u;
static uint32_t random32(void) { seed = seed * 1103515245u + 12345u; return (seed >> 1) ^ (seed << 15); }

static int exact(uint32_t seconds) {
    es2_sim_files_set_time(&files, seconds);
    memory.write32(memory.context, out_slot(), 0xa5a5a5a5u);
    uint32_t eax = 0;
    CHECK(!time_call(out_slot(), &eax));
    if (eax != seconds || rd(out_slot()) != seconds) {
        fprintf(stderr, "FAIL: _time %lu gave %lu (stored %lu)\n", (unsigned long)seconds, (unsigned long)eax, (unsigned long)rd(out_slot()));
        return 1;
    }
    /* The SYSTEMTIME against Win32's own conversion. */
    ULARGE_INTEGER t;
    t.QuadPart = ((unsigned long long)seconds + 11644473600ull) * 10000000ull;
    FILETIME ft = {t.LowPart, t.HighPart};
    SYSTEMTIME st;
    CHECK(FileTimeToSystemTime(&ft, &st));
    if (memcmp(&st, files.local_time, 16)) {
        fprintf(stderr, "FAIL: SYSTEMTIME of %lu\n", (unsigned long)seconds);
        return 1;
    }
    return 0;
}

static int headless(void) {
    uint32_t eax = 0;
    CHECK(time_call(out_slot(), &eax) == 1);            /* not bound */
    es2_sim_files_bind(&files);
    memory.write32(memory.context, out_slot(), 0);
    CHECK(!time_call(out_slot(), &eax) && eax == ES2_SIM_FILES_TIME_DEFAULT && rd(out_slot()) == ES2_SIM_FILES_TIME_DEFAULT);
    CHECK(eax % 3u == 0);
    CHECK(!time_call(0, &eax) && eax == ES2_SIM_FILES_TIME_DEFAULT);
    static const uint32_t fixed[] = {1u, 59u, 60u, 3599u, 86399u, 86400u, 68169600u /* 1972-03-01 */, 68083200u /* 1972-02-28 */,
                                     951782400u /* 2000-02-29 */, 951868799u, 978307199u, 978307200u, 852076800u,
                                     1792800000u, 0x7ffffffeu, 0x7fffffffu};
    for (unsigned i = 0; i < sizeof(fixed) / sizeof(fixed[0]); ++i)
        if (exact(fixed[i])) return 1;
    for (unsigned i = 0; i < 2000u; ++i)
        if (exact(1u + random32() % 0x7fffffffu)) return 1;
    es2_sim_files_set_time(&files, 0);
    memory.write32(memory.context, out_slot(), 0);
    CHECK(!time_call(out_slot(), &eax) && eax == 0xffffffffu && rd(out_slot()) == 0xffffffffu);
    files.os_context = NULL;                             /* no clock: GetLocalTime answers nothing */
    CHECK(time_call(out_slot(), &eax) == 1);
    files.os_context = &files;
    return 0;
}

static int wall_clock(void) {
    es2_io_os call;
    memset(&call, 0, sizeof(call));
    call.function = ES2_IO_OS_GET_LOCAL_TIME;
    SYSTEMTIME a, b, got;
    GetLocalTime(&a);
    es2_sim_files_os_wall_clock(&files, &memory, &call);
    GetLocalTime(&b);
    memcpy(&got, call.out, 16);
    FILETIME fa, fb, fg;
    CHECK(SystemTimeToFileTime(&a, &fa) && SystemTimeToFileTime(&b, &fb) && SystemTimeToFileTime(&got, &fg));
    CHECK(CompareFileTime(&fa, &fg) <= 0 && CompareFileTime(&fg, &fb) <= 0);
    memset(&call, 0, sizeof(call));
    call.function = ES2_IO_OS_GET_TIME_ZONE;
    es2_sim_files_os_wall_clock(&files, &memory, &call);
    TIME_ZONE_INFORMATION zone;
    memset(&zone, 0, sizeof(zone));
    CHECK(call.result == GetTimeZoneInformation(&zone) && !memcmp(call.out, &zone, sizeof(zone)));
    memset(&call, 0, sizeof(call));
    call.function = ES2_IO_OS_GET_STD_HANDLE;              /* every other call is the headless one's */
    call.arg[0] = 0xfffffff5u;
    call.result = 7;
    es2_sim_files_os_wall_clock(&files, &memory, &call);
    CHECK(call.result == 0);
    files.os = es2_sim_files_os_wall_clock;
    es2_io_tzset(&files.env, &files.layout, 0);
    uint32_t eax = 0;
    time_t before = time(NULL);
    CHECK(!time_call(out_slot(), &eax) && rd(out_slot()) == eax);
    time_t after = time(NULL);
    long long low = (long long)before - 3602, high = (long long)after + 3602;
    if ((long long)eax < low || (long long)eax > high) {
        fprintf(stderr, "FAIL: wall-clock _time %lu, time() %lld..%lld\n", (unsigned long)eax, (long long)before, (long long)after);
        return 1;
    }
    files.os = es2_sim_files_os_headless;
    es2_io_tzset(&files.env, &files.layout, 0);
    es2_sim_files_set_time(&files, ES2_SIM_FILES_TIME_DEFAULT);
    CHECK(!time_call(out_slot(), &eax) && eax == ES2_SIM_FILES_TIME_DEFAULT);
    return 0;
}

int main(void) {
    heap_bytes = calloc(1, HEAP_SIZE);
    region_bytes = calloc(1, ES2_SIM_FILES_REGION_SIZE);
    stack_bytes = stack_alloc();
    CHECK(heap_bytes && region_bytes && stack_bytes);
    CHECK(es2_sim_arena_add(&arena, HEAP_BASE, heap_bytes, HEAP_SIZE));
    CHECK(es2_sim_arena_add(&arena, REGION_BASE, region_bytes, ES2_SIM_FILES_REGION_SIZE));
    CHECK(es2_sim_arena_add(&arena, (uint32_t)(uintptr_t)stack_bytes, stack_bytes, STACK_SIZE));
    es2_sim_arena_memory(&arena, &memory);
    es2_sim_heap_os os = {NULL, os_alloc, os_free};
    es2_sim_heap_init(&heap, &memory, &os, (uint32_t)(uintptr_t)stack_bytes + 0x10u, 0, 0, 0);
    es2_vt_enable(&vt, 1, 47, 0x01000000u, 0, 1);
    uint32_t fs0 = 0xffffffffu;
    services.memory = &memory; services.heap = &heap; services.vt = &vt; services.fs0 = &fs0;
    es2_sim_dispatch_init(&table, storage, 4096, on_refuse, NULL);
    CHECK(!es2_sim_register_services(&table, &services));
    const char *error = NULL;
    uint32_t bad = 0;
    CHECK(!es2_sim_dispatch_seal(&table, &error, &bad));
    const es2_sim_entry *e = es2_sim_dispatch_find(&table, VA_TIME);
    CHECK(e && e->kind == ES2_SIM_SERVICE);
    CHECK(!es2_sim_files_init(&files, &memory, REGION_BASE, &heap));
    if (headless() || wall_clock()) return 1;
    puts("PASS: _time 0x4967fc headless (default 852076800, 16 fixed and 2,000 random seconds exact with their SYSTEMTIME, "
         "0 gives -1, NULL pointer, no clock and no file services refused) and over the wall clock (GetLocalTime, "
         "GetTimeZoneInformation, within an hour of time())");
    return 0;
}
